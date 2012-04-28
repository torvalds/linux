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

#ifndef _ASM_TILE_CHECKSUM_H
#define _ASM_TILE_CHECKSUM_H

#include <asm-generic/checksum.h>

/* Allow us to provide a more optimized do_csum(). */
__wsum do_csum(const unsigned char *buff, int len);
#define do_csum do_csum

/*
 * Return the sum of all the 16-bit subwords in a long.
 * This sums two subwords on a 32-bit machine, and four on 64 bits.
 * The implementation does two vector adds to capture any overflow.
 */
static inline unsigned int csum_long(unsigned long x)
{
	unsigned long ret;
#ifdef __tilegx__
	ret = __insn_v2sadu(x, 0);
	ret = __insn_v2sadu(ret, 0);
#else
	ret = __insn_sadh_u(x, 0);
	ret = __insn_sadh_u(ret, 0);
#endif
	return ret;
}

#endif /* _ASM_TILE_CHECKSUM_H */
