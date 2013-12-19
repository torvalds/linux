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
#include <arch/chip.h>

void *memset(void *s, int c, size_t n)
{
	uint32_t *out32;
	int n32;
	uint32_t v16, v32;
	uint8_t *out8 = s;
	int to_align32;

	/* Experimentation shows that a trivial tight loop is a win up until
	 * around a size of 20, where writing a word at a time starts to win.
	 */
#define BYTE_CUTOFF 20

#if BYTE_CUTOFF < 3
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

	/* Align 'out8'. We know n >= 3 so this won't write past the end. */
	while (((uintptr_t) out8 & 3) != 0) {
		*out8++ = c;
		--n;
	}

	/* Align 'n'. */
	while (n & 3)
		out8[--n] = c;

	out32 = (uint32_t *) out8;
	n32 = n >> 2;

	/* Tile input byte out to 32 bits. */
	v16 = __insn_intlb(c, c);
	v32 = __insn_intlh(v16, v16);

	/* This must be at least 8 or the following loop doesn't work. */
#define CACHE_LINE_SIZE_IN_WORDS (CHIP_L2_LINE_SIZE() / 4)

	/* Determine how many words we need to emit before the 'out32'
	 * pointer becomes aligned modulo the cache line size.
	 */
	to_align32 =
		(-((uintptr_t)out32 >> 2)) & (CACHE_LINE_SIZE_IN_WORDS - 1);

	/* Only bother aligning and using wh64 if there is at least
	 * one full cache line to process.  This check also prevents
	 * overrunning the end of the buffer with alignment words.
	 */
	if (to_align32 <= n32 - CACHE_LINE_SIZE_IN_WORDS) {
		int lines_left;

		/* Align out32 mod the cache line size so we can use wh64. */
		n32 -= to_align32;
		for (; to_align32 != 0; to_align32--) {
			*out32 = v32;
			out32++;
		}

		/* Use unsigned divide to turn this into a right shift. */
		lines_left = (unsigned)n32 / CACHE_LINE_SIZE_IN_WORDS;

		do {
			/* Only wh64 a few lines at a time, so we don't
			 * exceed the maximum number of victim lines.
			 */
			int x = ((lines_left < CHIP_MAX_OUTSTANDING_VICTIMS())
				  ? lines_left
				  : CHIP_MAX_OUTSTANDING_VICTIMS());
			uint32_t *wh = out32;
			int i = x;
			int j;

			lines_left -= x;

			do {
				__insn_wh64(wh);
				wh += CACHE_LINE_SIZE_IN_WORDS;
			} while (--i);

			for (j = x * (CACHE_LINE_SIZE_IN_WORDS / 4);
			     j != 0; j--) {
				*out32++ = v32;
				*out32++ = v32;
				*out32++ = v32;
				*out32++ = v32;
			}
		} while (lines_left != 0);

		/* We processed all full lines above, so only this many
		 * words remain to be processed.
		 */
		n32 &= CACHE_LINE_SIZE_IN_WORDS - 1;
	}

	/* Now handle any leftover values. */
	if (n32 != 0) {
		do {
			*out32 = v32;
			out32++;
		} while (--n32 != 0);
	}

	return s;
}
EXPORT_SYMBOL(memset);
