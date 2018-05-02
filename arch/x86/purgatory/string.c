/*
 * Simple string functions.
 *
 * Copyright (C) 2014 Red Hat Inc.
 *
 * Author:
 *       Vivek Goyal <vgoyal@redhat.com>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <linux/types.h>

#include "../boot/string.c"

void *memcpy(void *dst, const void *src, size_t len)
{
	return __builtin_memcpy(dst, src, len);
}

void *memset(void *dst, int c, size_t len)
{
	return __builtin_memset(dst, c, len);
}
