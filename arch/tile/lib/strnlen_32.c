/*
 * Copyright 2013 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/module.h>

size_t strnlen(const char *s, size_t count)
{
	/* Get an aligned pointer. */
	const uintptr_t s_int = (uintptr_t) s;
	const uint32_t *p = (const uint32_t *)(s_int & -4);
	size_t bytes_read = sizeof(*p) - (s_int & (sizeof(*p) - 1));
	size_t len;
	uint32_t v, bits;

	/* Avoid page fault risk by not reading any bytes when count is 0. */
	if (count == 0)
		return 0;

	/* Read first word, but force bytes before the string to be nonzero. */
	v = *p | ((1 << ((s_int << 3) & 31)) - 1);

	while ((bits = __insn_seqb(v, 0)) == 0) {
		if (bytes_read >= count) {
			/* Read COUNT bytes and didn't find the terminator. */
			return count;
		}
		v = *++p;
		bytes_read += sizeof(v);
	}

	len = ((const char *) p) + (__insn_ctz(bits) >> 3) - s;
	return (len < count ? len : count);
}
EXPORT_SYMBOL(strnlen);
