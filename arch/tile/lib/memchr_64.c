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

void *memchr(const void *s, int c, size_t n)
{
	const uint64_t *last_word_ptr;
	const uint64_t *p;
	const char *last_byte_ptr;
	uintptr_t s_int;
	uint64_t goal, before_mask, v, bits;
	char *ret;

	if (__builtin_expect(n == 0, 0)) {
		/* Don't dereference any memory if the array is empty. */
		return NULL;
	}

	/* Get an aligned pointer. */
	s_int = (uintptr_t) s;
	p = (const uint64_t *)(s_int & -8);

	/* Create eight copies of the byte for which we are looking. */
	goal = 0x0101010101010101ULL * (uint8_t) c;

	/* Read the first word, but munge it so that bytes before the array
	 * will not match goal.
	 *
	 * Note that this shift count expression works because we know
	 * shift counts are taken mod 64.
	 */
	before_mask = (1ULL << (s_int << 3)) - 1;
	v = (*p | before_mask) ^ (goal & before_mask);

	/* Compute the address of the last byte. */
	last_byte_ptr = (const char *)s + n - 1;

	/* Compute the address of the word containing the last byte. */
	last_word_ptr = (const uint64_t *)((uintptr_t) last_byte_ptr & -8);

	while ((bits = __insn_v1cmpeq(v, goal)) == 0) {
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
