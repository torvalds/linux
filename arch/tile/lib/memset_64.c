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
#include <arch/chip.h>
#include "string-endian.h"

void *memset(void *s, int c, size_t n)
{
	uint64_t *out64;
	int n64, to_align64;
	uint64_t v64;
	uint8_t *out8 = s;

	/* Experimentation shows that a trivial tight loop is a win up until
	 * around a size of 20, where writing a word at a time starts to win.
	 */
#define BYTE_CUTOFF 20

#if BYTE_CUTOFF < 7
	/* This must be at least at least this big, or some code later
	 * on doesn't work.
	 */
#error "BYTE_CUTOFF is too small"
#endif

	if (n < BYTE_CUTOFF) {
		/* Strangely, this turns out to be the tightest way to
		 * write this loop.
		 */
		if (n != 0) {
			do {
				/* Strangely, combining these into one line
				 * performs worse.
				 */
				*out8 = c;
				out8++;
			} while (--n != 0);
		}

		return s;
	}

	/* Align 'out8'. We know n >= 7 so this won't write past the end. */
	while (((uintptr_t) out8 & 7) != 0) {
		*out8++ = c;
		--n;
	}

	/* Align 'n'. */
	while (n & 7)
		out8[--n] = c;

	out64 = (uint64_t *) out8;
	n64 = n >> 3;

	/* Tile input byte out to 64 bits. */
	v64 = copy_byte(c);

	/* This must be at least 8 or the following loop doesn't work. */
#define CACHE_LINE_SIZE_IN_DOUBLEWORDS (CHIP_L2_LINE_SIZE() / 8)

	/* Determine how many words we need to emit before the 'out32'
	 * pointer becomes aligned modulo the cache line size.
	 */
	to_align64 = (-((uintptr_t)out64 >> 3)) &
		(CACHE_LINE_SIZE_IN_DOUBLEWORDS - 1);

	/* Only bother aligning and using wh64 if there is at least
	 * one full cache line to process.  This check also prevents
	 * overrunning the end of the buffer with alignment words.
	 */
	if (to_align64 <= n64 - CACHE_LINE_SIZE_IN_DOUBLEWORDS) {
		int lines_left;

		/* Align out64 mod the cache line size so we can use wh64. */
		n64 -= to_align64;
		for (; to_align64 != 0; to_align64--) {
			*out64 = v64;
			out64++;
		}

		/* Use unsigned divide to turn this into a right shift. */
		lines_left = (unsigned)n64 / CACHE_LINE_SIZE_IN_DOUBLEWORDS;

		do {
			/* Only wh64 a few lines at a time, so we don't
			 * exceed the maximum number of victim lines.
			 */
			int x = ((lines_left < CHIP_MAX_OUTSTANDING_VICTIMS())
				  ? lines_left
				  : CHIP_MAX_OUTSTANDING_VICTIMS());
			uint64_t *wh = out64;
			int i = x;
			int j;

			lines_left -= x;

			do {
				__insn_wh64(wh);
				wh += CACHE_LINE_SIZE_IN_DOUBLEWORDS;
			} while (--i);

			for (j = x * (CACHE_LINE_SIZE_IN_DOUBLEWORDS / 4);
			     j != 0; j--) {
				*out64++ = v64;
				*out64++ = v64;
				*out64++ = v64;
				*out64++ = v64;
			}
		} while (lines_left != 0);

		/* We processed all full lines above, so only this many
		 * words remain to be processed.
		 */
		n64 &= CACHE_LINE_SIZE_IN_DOUBLEWORDS - 1;
	}

	/* Now handle any leftover values. */
	if (n64 != 0) {
		do {
			*out64 = v64;
			out64++;
		} while (--n64 != 0);
	}

	return s;
}
EXPORT_SYMBOL(memset);
