// SPDX-License-Identifier: GPL-2.0-only
/*
 * Simple string functions.
 *
 * Copyright (C) 2014 Red Hat Inc.
 *
 * Author:
 *       Vivek Goyal <vgoyal@redhat.com>
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
