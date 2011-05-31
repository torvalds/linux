/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
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

#undef strlen

size_t strlen(const char *s)
{
	/* Get an aligned pointer. */
	const uintptr_t s_int = (uintptr_t) s;
	const uint64_t *p = (const uint64_t *)(s_int & -8);

	/* Read the first word, but force bytes before the string to be nonzero.
	 * This expression works because we know shift counts are taken mod 64.
	 */
	uint64_t v = *p | ((1ULL << (s_int << 3)) - 1);

	uint64_t bits;
	while ((bits = __insn_v1cmpeqi(v, 0)) == 0)
		v = *++p;

	return ((const char *)p) + (__insn_ctz(bits) >> 3) - s;
}
EXPORT_SYMBOL(strlen);
