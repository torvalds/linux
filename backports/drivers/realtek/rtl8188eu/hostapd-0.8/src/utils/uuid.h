/*
 * Universally Unique IDentifier (UUID)
 * Copyright (c) 2008, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef UUID_H
#define UUID_H

#define UUID_LEN 16

int uuid_str2bin(const char *str, u8 *bin);
int uuid_bin2str(const u8 *bin, char *str, size_t max_len);
int is_nil_uuid(const u8 *uuid);

#endif /* UUID_H */
