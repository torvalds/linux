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

void *memchr(const void *s, int c, size_t n)
{
	/* Get an aligned pointer. */
	const uintptr_t s_int = (uintptr_t) s;
	const uint32_t *p = (const uint32_t *)(s_int & -4);

	/* Create four copies of the byte for which we are looking. */
	const uint32_t goal = 0x01010101 * (uint8_t) c;

	/* Read the first word, but munge it so that bytes before the array
	 * will not match goal.
	 *
	 * Note that this shift count expression works because we know
	 * shift counts are taken mod 32.
	 */
	const uint32_t before_mask = (1 << (s_int << 3)) - 1;
	uint32_t v = (*p | before_mask) ^ (goal & before_mask);

	/* Compute the address of the last byte. */
	const char *const last_byte_ptr = (const char *)s + n - 1;

	/* Compute the address of the word containing the last byte. */
	const uint32_t *const last_word_ptr =
	    (const uint32_t *)((uintptr_t) last_byte_ptr & -4);

	uint32_t bits;
	char *ret;

	if (__builtin_expect(n == 0, 0)) {
		/* Don't dereference any memory if the array is empty. */
		return NULL;
	}

	while ((bits = __insn_seqb(v, goal)) == 0) {
		if (__builtin_expect(p == last_word_ptr, 0)) {
			/* We already read the last word in the array,
			 * so give up.
			 */
			return NULL;
		}
		v = *++p;
	}

	/* We found a match, but it might be in a byte past the end
	 * of the array.
	 */
	ret = ((char *)p) + (__insn_ctz(bits) >> 3);
	return (ret <= last_byte_ptr) ? ret : NULL;
}
EXPORT_SYMBOL(memchr);
