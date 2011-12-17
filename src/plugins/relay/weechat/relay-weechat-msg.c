/*
 * Copyright (C) 2003-2011 Sebastien Helleu <flashcode@flashtux.org>
 *
 * This file is part of WeeChat, the extensible chat client.
 *
 * WeeChat is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * WeeChat is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with WeeChat.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * relay-weechat-msg.c: build binary messages for WeeChat protocol
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <arpa/inet.h>

#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

#include "../../weechat-plugin.h"
#include "../relay.h"
#include "relay-weechat.h"
#include "relay-weechat-msg.h"
#include "../relay-buffer.h"
#include "../relay-client.h"
#include "../relay-config.h"
#include "../relay-raw.h"


/*
 * relay_weechat_msg_new: build a new message (for sending to client)
 */

struct t_relay_weechat_msg *
relay_weechat_msg_new (const char *id)
{
    struct t_relay_weechat_msg *new_msg;

    new_msg = malloc (sizeof (*new_msg));
    if (!new_msg)
        return NULL;

    new_msg->id = (id) ? strdup (id) : NULL;
    new_msg->data = malloc (RELAY_WEECHAT_MSG_INITIAL_ALLOC);
    if (!new_msg->data)
    {
        free (new_msg);
        return NULL;
    }
    new_msg->data_alloc = RELAY_WEECHAT_MSG_INITIAL_ALLOC;
    new_msg->data_size = 0;

    /* add size and compression flag (they will be set later) */
    relay_weechat_msg_add_int (new_msg, 0);
    relay_weechat_msg_add_char (new_msg, 0);

    /* add id */
    relay_weechat_msg_add_string (new_msg, id);

    return new_msg;
}

/*
 * relay_weechat_msg_add_bytes: add some bytes to a message
 */

void
relay_weechat_msg_add_bytes (struct t_relay_weechat_msg *msg,
                             const void *buffer, int size)
{
    char *ptr;

    if (!msg || !msg->data)
        return;

    while (msg->data_size + size > msg->data_alloc)
    {
        msg->data_alloc *= 2;
        ptr = realloc (msg->data, msg->data_alloc);
        if (!ptr)
        {
            free (msg->data);
            msg->data = NULL;
            msg->data_alloc = 0;
            msg->data_size = 0;
            return;
        }
        msg->data = ptr;
    }

    memcpy (msg->data + msg->data_size, buffer, size);
    msg->data_size += size;
}

/*
 * relay_weechat_msg_set_bytes: set some bytes in a message
 */

void
relay_weechat_msg_set_bytes (struct t_relay_weechat_msg *msg,
                             int position, const void *buffer, int size)
{
    if (!msg || !msg->data || (position + size) > msg->data_size)
        return;

    memcpy (msg->data + position, buffer, size);
}

/*
 * relay_weechat_msg_add_type: add type to a message
 */

void
relay_weechat_msg_add_type (struct t_relay_weechat_msg *msg, const char *string)
{
    if (string)
        relay_weechat_msg_add_bytes (msg, string, strlen (string));
}

/*
 * relay_weechat_msg_add_char: add a char to a message
 */

void
relay_weechat_msg_add_char (struct t_relay_weechat_msg *msg, char c)
{
    relay_weechat_msg_add_bytes (msg, &c, 1);
}

/*
 * relay_weechat_msg_add_int: add an integer to a message
 */

void
relay_weechat_msg_add_int (struct t_relay_weechat_msg *msg, int value)
{
    uint32_t value32;

    value32 = htonl ((uint32_t)value);
    relay_weechat_msg_add_bytes (msg, &value32, 4);
}

/*
 * relay_weechat_msg_add_long: add a long integer to a message
 */

void
relay_weechat_msg_add_long (struct t_relay_weechat_msg *msg, long value)
{
    char str_long[128];
    unsigned char length;

    snprintf (str_long, sizeof (str_long), "%ld", value);
    length = strlen (str_long);
    relay_weechat_msg_add_bytes (msg, &length, 1);
    relay_weechat_msg_add_bytes (msg, str_long, length);
}

/*
 * relay_weechat_msg_add_string: add length + string to a message
 */

void
relay_weechat_msg_add_string (struct t_relay_weechat_msg *msg,
                              const char *string)
{
    int length;

    if (string)
    {
        length = strlen (string);
        relay_weechat_msg_add_int (msg, length);
        if (length > 0)
            relay_weechat_msg_add_bytes (msg, string, length);
    }
    else
    {
        relay_weechat_msg_add_int (msg, -1);
    }
}

/*
 * relay_weechat_msg_add_buffer: add buffer (length + data) to a message
 */

void
relay_weechat_msg_add_buffer (struct t_relay_weechat_msg *msg,
                              void *buffer, int length)
{
    if (buffer)
    {
        relay_weechat_msg_add_int (msg, length);
        if (length > 0)
            relay_weechat_msg_add_bytes (msg, buffer, length);
    }
    else
    {
        relay_weechat_msg_add_int (msg, -1);
    }
}

/*
 * relay_weechat_msg_add_pointer: add a pointer to a message
 */

void
relay_weechat_msg_add_pointer (struct t_relay_weechat_msg *msg, void *pointer)
{
    char str_pointer[128];
    unsigned char length;

    snprintf (str_pointer, sizeof (str_pointer),
              "%lx", (long unsigned int)pointer);
    length = strlen (str_pointer);
    relay_weechat_msg_add_bytes (msg, &length, 1);
    relay_weechat_msg_add_bytes (msg, str_pointer, length);
}

/*
 * relay_weechat_msg_add_time: add a time to a message
 */

void
relay_weechat_msg_add_time (struct t_relay_weechat_msg *msg, time_t time)
{
    char str_time[128];
    unsigned char length;

    snprintf (str_time, sizeof (str_time), "%ld", time);
    length = strlen (str_time);
    relay_weechat_msg_add_bytes (msg, &length, 1);
    relay_weechat_msg_add_bytes (msg, str_time, length);
}

/*
 * relay_weechat_msg_add_hdata_path: recursively add hdata for a path
 *                                   return number of hdata objects added in
 *                                   message
 */

int
relay_weechat_msg_add_hdata_path (struct t_relay_weechat_msg *msg,
                                  char **list_path,
                                  int index_path,
                                  void **path_pointers,
                                  struct t_hdata *hdata,
                                  void *pointer,
                                  char **list_keys)
{
    int num_added, i, count, count_all, type;
    char *pos, *pos2, *str_count, *error;
    void *sub_pointer;
    struct t_hdata *sub_hdata;
    const char *sub_hdata_name;

    num_added = 0;

    count_all = 0;
    count = 0;
    pos = strchr (list_path[index_path], '(');
    if (pos)
    {
        pos2 = strchr (pos + 1, ')');
        if (pos2 && (pos2 > pos + 1))
        {
            str_count = weechat_strndup (pos + 1, pos2 - (pos + 1));
            if (str_count)
            {
                if (strcmp (str_count, "*") == 0)
                    count_all = 1;
                else
                {
                    error = NULL;
                    count = (int)strtol (str_count, &error, 10);
                    if (error && !error[0])
                    {
                        if (count > 0)
                            count--;
                        else if (count < 0)
                            count++;
                    }
                    else
                        count = 0;
                }
                free (str_count);
            }
        }
    }

    while (pointer)
    {
        path_pointers[index_path] = pointer;

        if (list_path[index_path + 1])
        {
            /* recursive call with next path */
            pos = strchr (list_path[index_path + 1], '(');
            if (pos)
                pos[0] = '\0';
            sub_pointer = weechat_hdata_pointer (hdata, pointer, list_path[index_path + 1]);
            sub_hdata_name = weechat_hdata_get_var_hdata (hdata, list_path[index_path + 1]);
            if (pos)
                pos[0] = '(';
            if (sub_pointer && sub_hdata_name)
            {
                sub_hdata = weechat_hdata_get (sub_hdata_name);
                if (sub_hdata)
                {
                    num_added += relay_weechat_msg_add_hdata_path (msg,
                                                                   list_path,
                                                                   index_path + 1,
                                                                   path_pointers,
                                                                   sub_hdata,
                                                                   sub_pointer,
                                                                   list_keys);
                }
            }
        }
        else
        {
            /* last path? then get pointer + values and fill message with them */
            for (i = 0; list_path[i]; i++)
            {
                relay_weechat_msg_add_pointer (msg, path_pointers[i]);
            }
            for (i = 0; list_keys[i]; i++)
            {
                type = weechat_hdata_get_var_type (hdata, list_keys[i]);
                if ((type >= 0) && (type != WEECHAT_HDATA_OTHER))
                {
                    switch (type)
                    {
                        case WEECHAT_HDATA_CHAR:
                            relay_weechat_msg_add_char (msg,
                                                        weechat_hdata_char (hdata,
                                                                            pointer,
                                                                            list_keys[i]));
                            break;
                        case WEECHAT_HDATA_INTEGER:
                            relay_weechat_msg_add_int (msg,
                                                       weechat_hdata_integer (hdata,
                                                                              pointer,
                                                                              list_keys[i]));
                            break;
                        case WEECHAT_HDATA_LONG:
                            relay_weechat_msg_add_long (msg,
                                                        weechat_hdata_long (hdata,
                                                                            pointer,
                                                                            list_keys[i]));
                            break;
                        case WEECHAT_HDATA_STRING:
                            relay_weechat_msg_add_string (msg,
                                                          weechat_hdata_string (hdata,
                                                                                pointer,
                                                                                list_keys[i]));
                            break;
                        case WEECHAT_HDATA_POINTER:
                            relay_weechat_msg_add_pointer (msg,
                                                           weechat_hdata_pointer (hdata,
                                                                                  pointer,
                                                                                  list_keys[i]));
                            break;
                        case WEECHAT_HDATA_TIME:
                            relay_weechat_msg_add_time (msg,
                                                        weechat_hdata_time (hdata,
                                                                            pointer,
                                                                            list_keys[i]));
                            break;
                    }
                }
            }
            num_added++;
        }
        if (count_all)
        {
            pointer = weechat_hdata_move (hdata, pointer, 1);
        }
        else if (count == 0)
            pointer = NULL;
        else if (count > 0)
        {
            pointer = weechat_hdata_move (hdata, pointer, 1);
            count--;
        }
        else
        {
            pointer = weechat_hdata_move (hdata, pointer, -1);
            count++;
        }
        if (!pointer)
            break;
    }

    return num_added;
}

/*
 * relay_weechat_msg_add_hdata: add a hdata to a message
 *                              path has format:
 *                                  hdata_head:ptr->var->var->...->var
 *                                where ptr can be a list name or a
 *                                pointer (0x12345)
 *                              keys is optional: if not NULL,
 *                              comma-separated list of keys to return
 *                              for hdata
 */

void
relay_weechat_msg_add_hdata (struct t_relay_weechat_msg *msg,
                             const char *path, const char *keys)
{
    struct t_hdata *ptr_hdata_head, *ptr_hdata;
    char *hdata_head, *pos, **list_keys, *keys_types, **list_path;
    char *path_returned;
    const char *hdata_name;
    void *pointer, **path_pointers;
    long unsigned int value;
    int num_keys, num_path, i, type, pos_count, count, rc;
    uint32_t count32;

    hdata_head = NULL;
    list_keys = NULL;
    num_keys = 0;
    keys_types = NULL;
    list_path = NULL;
    num_path = 0;
    path_returned = NULL;

    /* extract hdata name (head) from path */
    pos = strchr (path, ':');
    if (!pos)
        goto end;
    hdata_head = weechat_strndup (path, pos - path);
    if (!hdata_head)
        goto end;
    ptr_hdata_head = weechat_hdata_get (hdata_head);
    if (!ptr_hdata_head)
        goto end;

    /* split path */
    list_path = weechat_string_split (pos + 1, "/", 0, 0, &num_path);
    if (!list_path)
        goto end;

    /* extract pointer from first path (direct pointer or list name) */
    pointer = NULL;
    pos = strchr (list_path[0], '(');
    if (pos)
        pos[0] = '\0';
    if (strncmp (list_path[0], "0x", 2) == 0)
    {
        rc = sscanf (list_path[0], "%lx", &value);
        if ((rc != EOF) && (rc != 0))
            pointer = (void *)value;
    }
    else
        pointer = weechat_hdata_get_list (ptr_hdata_head, list_path[0]);
    if (pos)
        pos[0] = '(';
    if (!pointer)
        goto end;

    /*
     * build string with path where:
     * - counters are removed
     * - variable names are replaced by hdata name
     */
    path_returned = malloc (strlen (path) * 2);
    if (!path_returned)
        goto end;
    ptr_hdata = ptr_hdata_head;
    strcpy (path_returned, hdata_head);
    hdata_name = hdata_head;
    for (i = 1; i < num_path; i++)
    {
        pos = strchr (list_path[i], '(');
        if (pos)
            pos[0] = '\0';
        hdata_name = weechat_hdata_get_var_hdata (ptr_hdata, list_path[i]);
        if (!hdata_name)
            goto end;
        ptr_hdata = weechat_hdata_get (hdata_name);
        if (!ptr_hdata)
            goto end;
        strcat (path_returned, "/");
        strcat (path_returned, hdata_name);
        if (pos)
            pos[0] = '(';
    }

    /* split keys */
    if (!keys)
        keys = weechat_hdata_get_string (ptr_hdata, "var_keys");
    list_keys = weechat_string_split (keys, ",", 0, 0, &num_keys);
    if (!list_keys)
        goto end;

    /* build string with list of keys with types: "key1:type1,key2:type2,..." */
    keys_types = malloc (strlen (keys) + (num_keys * 8) + 1);
    if (!keys_types)
        goto end;
    keys_types[0] = '\0';
    for (i = 0; i < num_keys; i++)
    {
        type = weechat_hdata_get_var_type (ptr_hdata, list_keys[i]);
        if ((type >= 0) && (type != WEECHAT_HDATA_OTHER))
        {
            if (keys_types[0])
                strcat (keys_types, ",");
            strcat (keys_types, list_keys[i]);
            strcat (keys_types, ":");
            switch (type)
            {
                case WEECHAT_HDATA_CHAR:
                    strcat (keys_types, RELAY_WEECHAT_MSG_OBJ_CHAR);
                    break;
                case WEECHAT_HDATA_INTEGER:
                    strcat (keys_types, RELAY_WEECHAT_MSG_OBJ_INT);
                    break;
                case WEECHAT_HDATA_LONG:
                    strcat (keys_types, RELAY_WEECHAT_MSG_OBJ_LONG);
                    break;
                case WEECHAT_HDATA_STRING:
                    strcat (keys_types, RELAY_WEECHAT_MSG_OBJ_STRING);
                    break;
                case WEECHAT_HDATA_POINTER:
                    strcat (keys_types, RELAY_WEECHAT_MSG_OBJ_POINTER);
                    break;
                case WEECHAT_HDATA_TIME:
                    strcat (keys_types, RELAY_WEECHAT_MSG_OBJ_TIME);
                    break;
            }
        }
    }
    if (!keys_types[0])
        goto end;

    /* start hdata in message */
    relay_weechat_msg_add_type (msg, RELAY_WEECHAT_MSG_OBJ_HDATA);
    relay_weechat_msg_add_string (msg, path_returned);
    relay_weechat_msg_add_string (msg, keys_types);

    /* "count" will be set later, with number of objects in hdata */
    pos_count = msg->data_size;
    count = 0;
    relay_weechat_msg_add_int (msg, 0);
    path_pointers = malloc (sizeof (*path_pointers) * num_path);
    if (path_pointers)
    {
        count = relay_weechat_msg_add_hdata_path (msg,
                                                  list_path,
                                                  0,
                                                  path_pointers,
                                                  ptr_hdata_head,
                                                  pointer,
                                                  list_keys);
    }
    count32 = htonl ((uint32_t)count);
    relay_weechat_msg_set_bytes (msg, pos_count, &count32, 4);

end:
    if (list_keys)
        weechat_string_free_split (list_keys);
    if (keys_types)
        free (keys_types);
    if (list_path)
        weechat_string_free_split (list_path);
    if (path_returned)
        free (path_returned);
    if (hdata_head)
        free (hdata_head);
}

/*
 * relay_weechat_msg_add_infolist: add an infolist to a message
 */

void
relay_weechat_msg_add_infolist (struct t_relay_weechat_msg *msg,
                                const char *name,
                                void *pointer,
                                const char *arguments)
{
    struct t_infolist *ptr_infolist;
    const char *fields;
    char **list_fields;
    void *buf_ptr;
    int num_fields, i, buf_size;
    int pos_count_items, count_items, pos_count_vars, count_vars;
    uint32_t count32;

    ptr_infolist = weechat_infolist_get (name, pointer, arguments);
    if (!ptr_infolist)
        return;

    /* start infolist in message */
    relay_weechat_msg_add_type (msg, RELAY_WEECHAT_MSG_OBJ_INFOLIST);
    relay_weechat_msg_add_string (msg, name);

    /* count of items will be set later, with number of items in infolist */
    pos_count_items = msg->data_size;
    count_items = 0;
    relay_weechat_msg_add_int (msg, 0);

    while (weechat_infolist_next (ptr_infolist))
    {
        fields = weechat_infolist_fields (ptr_infolist);
        if (fields)
        {
            list_fields = weechat_string_split (fields, ",", 0, 0, &num_fields);
            if (list_fields)
            {
                count_items++;
                pos_count_vars = msg->data_size;
                count_vars = 0;
                relay_weechat_msg_add_int (msg, 0);
                for (i = 0; i < num_fields; i++)
                {
                    if (strlen (list_fields[i]) > 2)
                    {
                        count_vars++;
                        relay_weechat_msg_add_string (msg, list_fields[i] + 2);
                        switch (list_fields[i][0])
                        {
                            case 'i':
                                relay_weechat_msg_add_type (msg, RELAY_WEECHAT_MSG_OBJ_INT);
                                relay_weechat_msg_add_int (msg,
                                                           weechat_infolist_integer (ptr_infolist,
                                                                                     list_fields[i] + 2));
                                break;
                            case 's':
                                relay_weechat_msg_add_type (msg, RELAY_WEECHAT_MSG_OBJ_STRING);
                                relay_weechat_msg_add_string (msg,
                                                              weechat_infolist_string (ptr_infolist,
                                                                                       list_fields[i] + 2));
                                break;
                            case 'p':
                                relay_weechat_msg_add_type (msg, RELAY_WEECHAT_MSG_OBJ_POINTER);
                                relay_weechat_msg_add_pointer (msg,
                                                               weechat_infolist_pointer (ptr_infolist,
                                                                                         list_fields[i] + 2));
                                break;
                            case 'b':
                                relay_weechat_msg_add_type (msg, RELAY_WEECHAT_MSG_OBJ_BUFFER);
                                buf_ptr = weechat_infolist_buffer (ptr_infolist,
                                                                   list_fields[i] + 2,
                                                                   &buf_size);
                                relay_weechat_msg_add_buffer (msg, buf_ptr, buf_size);
                                break;
                            case 't':
                                relay_weechat_msg_add_type (msg, RELAY_WEECHAT_MSG_OBJ_TIME);
                                relay_weechat_msg_add_time (msg,
                                                            weechat_infolist_time (ptr_infolist,
                                                                                   list_fields[i] + 2));
                                break;
                        }
                    }
                }

                /* set count of variables in item */
                count32 = htonl ((uint32_t)count_vars);
                relay_weechat_msg_set_bytes (msg, pos_count_vars, &count32, 4);

                weechat_string_free_split (list_fields);
            }
        }
    }

    /* set count of items */
    count32 = htonl ((uint32_t)count_items);
    relay_weechat_msg_set_bytes (msg, pos_count_items, &count32, 4);

    weechat_infolist_free (ptr_infolist);
}

/*
 * relay_weechat_msg_add_nicklist_buffer: add nicklist for a buffer, as hdata
 *                                        object
 *                                        return number of nicks+groups added
 *                                        in message
 */

int
relay_weechat_msg_add_nicklist_buffer (struct t_relay_weechat_msg *msg,
                                       struct t_gui_buffer *buffer)
{
    int count;
    struct t_hdata *ptr_hdata_group, *ptr_hdata_nick;
    struct t_gui_nick_group *ptr_group;
    struct t_gui_nick *ptr_nick;

    count = 0;

    ptr_hdata_group = weechat_hdata_get ("nick_group");
    ptr_hdata_nick = weechat_hdata_get ("nick");

    ptr_group = NULL;
    ptr_nick = NULL;
    weechat_nicklist_get_next_item (buffer, &ptr_group, &ptr_nick);
    while (ptr_group || ptr_nick)
    {
        if (ptr_nick)
        {
            relay_weechat_msg_add_pointer (msg, buffer);
            relay_weechat_msg_add_pointer (msg, ptr_nick);
            relay_weechat_msg_add_char (msg, 0); /* group */
            relay_weechat_msg_add_char (msg,
                                        (char)weechat_hdata_integer(ptr_hdata_nick,
                                                                    ptr_nick,
                                                                    "visible"));
            relay_weechat_msg_add_int (msg,
                                       weechat_hdata_integer (ptr_hdata_nick,
                                                              ptr_nick,
                                                              "level"));
            relay_weechat_msg_add_string (msg,
                                          weechat_hdata_string (ptr_hdata_nick,
                                                                ptr_nick,
                                                                "name"));
            relay_weechat_msg_add_string (msg,
                                          weechat_hdata_string (ptr_hdata_nick,
                                                                ptr_nick,
                                                                "color"));
            relay_weechat_msg_add_string (msg,
                                          weechat_hdata_string (ptr_hdata_nick,
                                                                ptr_nick,
                                                                "prefix"));
            relay_weechat_msg_add_string (msg,
                                          weechat_hdata_string (ptr_hdata_nick,
                                                                ptr_nick,
                                                                "prefix_color"));
            count++;
        }
        else
        {
            relay_weechat_msg_add_pointer (msg, buffer);
            relay_weechat_msg_add_pointer (msg, ptr_group);
            relay_weechat_msg_add_char (msg, 1); /* group */
            relay_weechat_msg_add_char (msg,
                                        (char)weechat_hdata_integer(ptr_hdata_group,
                                                                    ptr_group,
                                                                    "visible"));
            relay_weechat_msg_add_int (msg,
                                       weechat_hdata_integer (ptr_hdata_group,
                                                              ptr_group,
                                                              "level"));
            relay_weechat_msg_add_string (msg,
                                          weechat_hdata_string (ptr_hdata_group,
                                                                ptr_group,
                                                                "name"));
            relay_weechat_msg_add_string (msg,
                                          weechat_hdata_string (ptr_hdata_group,
                                                                ptr_group,
                                                                "color"));
            relay_weechat_msg_add_string (msg, NULL); /* prefix */
            relay_weechat_msg_add_string (msg, NULL); /* prefix_color */
            count++;
        }
        weechat_nicklist_get_next_item (buffer, &ptr_group, &ptr_nick);
    }

    return count;
}

/*
 * relay_weechat_msg_add_nicklist: add nicklist for one or all buffers, as
 *                                 hdata object
 */

void
relay_weechat_msg_add_nicklist (struct t_relay_weechat_msg *msg,
                                struct t_gui_buffer *buffer)
{
    struct t_hdata *ptr_hdata;
    struct t_gui_buffer *ptr_buffer;
    int pos_count, count;
    uint32_t count32;

    relay_weechat_msg_add_type (msg, RELAY_WEECHAT_MSG_OBJ_HDATA);
    relay_weechat_msg_add_string (msg, "buffer/nicklist_item");
    relay_weechat_msg_add_string (msg,
                                  "group:chr,visible:chr,level:int,"
                                  "name:str,color:str,"
                                  "prefix:str,prefix_color:str");

    /* "count" will be set later, with number of objects in hdata */
    pos_count = msg->data_size;
    count = 0;
    relay_weechat_msg_add_int (msg, 0);

    if (buffer)
        count += relay_weechat_msg_add_nicklist_buffer (msg, buffer);
    else
    {
        ptr_hdata = weechat_hdata_get ("buffer");
        ptr_buffer = weechat_hdata_get_list (ptr_hdata, "gui_buffers");
        while (ptr_buffer)
        {
            count += relay_weechat_msg_add_nicklist_buffer (msg, ptr_buffer);
            ptr_buffer = weechat_hdata_move (ptr_hdata, ptr_buffer, 1);
        }
    }

    count32 = htonl ((uint32_t)count);
    relay_weechat_msg_set_bytes (msg, pos_count, &count32, 4);
}

/*
 * relay_weechat_msg_send: send a message
 */

void
relay_weechat_msg_send (struct t_relay_client *client,
                        struct t_relay_weechat_msg *msg,
                        int display_in_raw_buffer)
{
    uint32_t size32;
    char compression;
#ifdef HAVE_ZLIB
    int rc, num_sent;
    Bytef *dest;
    uLongf dest_size;
    struct timeval tv1, tv2;
    long time_diff;

    if (RELAY_WEECHAT_DATA(client, compression)
        && (weechat_config_integer (relay_config_network_compression_level) > 0))
    {
        dest_size = compressBound (msg->data_size - 5);
        dest = malloc (dest_size + 5);
        if (dest)
        {
            gettimeofday (&tv1, NULL);
            rc = compress2 (dest + 5, &dest_size,
                            (Bytef *)(msg->data + 5), msg->data_size - 5,
                            weechat_config_integer (relay_config_network_compression_level));
            gettimeofday (&tv2, NULL);
            time_diff = weechat_util_timeval_diff (&tv1, &tv2);
            if ((rc == Z_OK) && ((int)dest_size + 5 < msg->data_size))
            {
                /* set size and compression flag */
                size32 = htonl ((uint32_t)(dest_size + 5));
                memcpy (dest, &size32, 4);
                dest[4] = 1;

                /* send compressed data */
                num_sent = send (client->sock, dest, dest_size + 5, 0);

                /* display message in raw buffer */
                if (display_in_raw_buffer)
                {
                    relay_raw_print (client, RELAY_RAW_FLAG_SEND,
                                     "obj: %d/%d bytes (%d%%, %ldms), id: %s",
                                     (int)dest_size + 5,
                                     msg->data_size,
                                     100 - ((((int)dest_size + 5) * 100) / msg->data_size),
                                     time_diff,
                                     msg->id);
                    if (num_sent < 0)
                    {
                        relay_raw_print (client, RELAY_RAW_FLAG_SEND,
                                         "error: %s", strerror (errno));
                    }
                }

                if (num_sent > 0)
                {
                    client->bytes_sent += num_sent;
                    relay_buffer_refresh (NULL);
                }

                free (dest);
                return;
            }
            free (dest);
        }
    }
#endif

    /* set size and compression flag */
    size32 = htonl ((uint32_t)msg->data_size);
    relay_weechat_msg_set_bytes (msg, 0, &size32, 4);
    compression = 0;
    relay_weechat_msg_set_bytes (msg, 4, &compression, 1);

    /* send uncompressed data */
    num_sent = send (client->sock, msg->data, msg->data_size, 0);

    /* display message in raw buffer */
    if (display_in_raw_buffer)
    {
        relay_raw_print (client, RELAY_RAW_FLAG_SEND,
                         "obj: %d bytes", msg->data_size);
        if (num_sent < 0)
        {
            relay_raw_print (client, RELAY_RAW_FLAG_SEND,
                             "error: %s", strerror (errno));
        }
    }

    if (num_sent > 0)
    {
        client->bytes_sent += num_sent;
        relay_buffer_refresh (NULL);
    }
}

/*
 * relay_weechat_msg_free: free a message
 */

void
relay_weechat_msg_free (struct t_relay_weechat_msg *msg)
{
    if (msg->id)
        free (msg->id);
    if (msg->data)
        free (msg->data);

    free (msg);
}

/*
 * relay_weechat_sendf: send formatted data to client
 */

int
relay_weechat_sendf (struct t_relay_client *client, const char *format, ...)
{
    char str_length[8];
    int length_vbuffer, num_sent, total_sent;

    if (!client)
        return 0;

    weechat_va_format (format);
    if (!vbuffer)
        return 0;
    length_vbuffer = strlen (vbuffer);

    total_sent = 0;

    snprintf (str_length, sizeof (str_length), "%07d", length_vbuffer);

    num_sent = send (client->sock, str_length, 7, 0);
    client->bytes_sent += 7;
    total_sent += num_sent;
    if (num_sent >= 0)
    {
        num_sent = send (client->sock, vbuffer, length_vbuffer, 0);
        client->bytes_sent += length_vbuffer;
        total_sent += num_sent;
    }

    if (num_sent < 0)
    {
        weechat_printf (NULL,
                        _("%s%s: error sending data to client %d (%s)"),
                        weechat_prefix ("error"), RELAY_PLUGIN_NAME,
                        client->id, strerror (errno));
    }

    return total_sent;
}