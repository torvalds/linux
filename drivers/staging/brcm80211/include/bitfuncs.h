/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _BITFUNCS_H
#define _BITFUNCS_H

#include <typedefs.h>

/* local prototypes */
static inline uint32 find_msbit(uint32 x);

/*
 * find_msbit: returns index of most significant set bit in x, with index
 *   range defined as 0-31.  NOTE: returns zero if input is zero.
 */

#if defined(USE_PENTIUM_BSR) && defined(__GNUC__)

/*
 * Implementation for Pentium processors and gcc.  Note that this
 * instruction is actually very slow on some processors (e.g., family 5,
 * model 2, stepping 12, "Pentium 75 - 200"), so we use the generic
 * implementation instead.
 */
static inline uint32 find_msbit(uint32 x)
{
	uint msbit;
 __asm__("bsrl %1,%0" : "=r"(msbit)
 :		"r"(x));
	return msbit;
}

#else				/* !USE_PENTIUM_BSR || !__GNUC__ */

/*
 * Generic Implementation
 */

#define DB_POW_MASK16	0xffff0000
#define DB_POW_MASK8	0x0000ff00
#define DB_POW_MASK4	0x000000f0
#define DB_POW_MASK2	0x0000000c
#define DB_POW_MASK1	0x00000002

static inline uint32 find_msbit(uint32 x)
{
	uint32 temp_x = x;
	uint msbit = 0;
	if (temp_x & DB_POW_MASK16) {
		temp_x >>= 16;
		msbit = 16;
	}
	if (temp_x & DB_POW_MASK8) {
		temp_x >>= 8;
		msbit += 8;
	}
	if (temp_x & DB_POW_MASK4) {
		temp_x >>= 4;
		msbit += 4;
	}
	if (temp_x & DB_POW_MASK2) {
		temp_x >>= 2;
		msbit += 2;
	}
	if (temp_x & DB_POW_MASK1) {
		msbit += 1;
	}
	return msbit;
}

#endif				/* USE_PENTIUM_BSR && __GNUC__ */

#endif				/* _BITFUNCS_H */
