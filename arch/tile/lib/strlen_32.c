/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
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

size_t strlen(const char *s)
{
	/* Get an aligned pointer. */
	const uintptr_t s_int = (uintptr_t) s;
	const uint32_t *p = (const uint32_t *)(s_int & -4);

	/* Read the first word, but force bytes before the string to be nonzero.
	 * This expression works because we know shift counts are taken mod 32.
	 */
	uint32_t v = *p | ((1 << (s_int << 3)) - 1);

	uint32_t bits;
	while ((bits = __insn_seqb(v, 0)) == 0)
		v = *++p;

	return ((const char *)p) + (__insn_ctz(bits) >> 3) - s;
}
EXPORT_SYMBOL(strlen);
