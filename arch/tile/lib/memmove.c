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

void *memmove(void *dest, const void *src, size_t n)
{
	if ((const char *)src >= (char *)dest + n
	    || (char *)dest >= (const char *)src + n) {
		/* We found no overlap, so let memcpy do all the heavy
		 * lifting (prefetching, etc.)
		 */
		return memcpy(dest, src, n);
	}

	if (n != 0) {
		const uint8_t *in;
		uint8_t x;
		uint8_t *out;
		int stride;

		if (src < dest) {
			/* copy backwards */
			in = (const uint8_t *)src + n - 1;
			out = (uint8_t *)dest + n - 1;
			stride = -1;
		} else {
			/* copy forwards */
			in = (const uint8_t *)src;
			out = (uint8_t *)dest;
			stride = 1;
		}

		/* Manually software-pipeline this loop. */
		x = *in;
		in += stride;

		while (--n != 0) {
			*out = x;
			out += stride;
			x = *in;
			in += stride;
		}

		*out = x;
	}

	return dest;
}
EXPORT_SYMBOL(memmove);
