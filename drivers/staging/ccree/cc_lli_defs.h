/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _CC_LLI_DEFS_H_
#define _CC_LLI_DEFS_H_

#include <linux/types.h>

#include "cc_bitops.h"

/* Max DLLI size
 *  AKA DX_DSCRPTR_QUEUE_WORD1_DIN_SIZE_BIT_SIZE
 */
#define DLLI_SIZE_BIT_SIZE	0x18

#define CC_MAX_MLLI_ENTRY_SIZE 0x10000

#define LLI_SET_ADDR(__lli_p, __addr) do {				\
		u32 *lli_p = (u32 *)__lli_p;				\
		typeof(__addr) addr = __addr;				\
									\
		BITFIELD_SET(lli_p[LLI_WORD0_OFFSET],			\
			LLI_LADDR_BIT_OFFSET,				\
			LLI_LADDR_BIT_SIZE, (addr & U32_MAX));		\
									\
		BITFIELD_SET(lli_p[LLI_WORD1_OFFSET],			\
			LLI_HADDR_BIT_OFFSET,				\
			LLI_HADDR_BIT_SIZE, MSB64(addr));		\
	} while (0)

#define LLI_SET_SIZE(lli_p, size)					\
		BITFIELD_SET(((u32 *)(lli_p))[LLI_WORD1_OFFSET],	\
		LLI_SIZE_BIT_OFFSET, LLI_SIZE_BIT_SIZE, size)

/* Size of entry */
#define LLI_ENTRY_WORD_SIZE 2
#define LLI_ENTRY_BYTE_SIZE (LLI_ENTRY_WORD_SIZE * sizeof(u32))

/* Word0[31:0] = ADDR[31:0] */
#define LLI_WORD0_OFFSET 0
#define LLI_LADDR_BIT_OFFSET 0
#define LLI_LADDR_BIT_SIZE 32
/* Word1[31:16] = ADDR[47:32]; Word1[15:0] = SIZE */
#define LLI_WORD1_OFFSET 1
#define LLI_SIZE_BIT_OFFSET 0
#define LLI_SIZE_BIT_SIZE 16
#define LLI_HADDR_BIT_OFFSET 16
#define LLI_HADDR_BIT_SIZE 16

#endif /*_CC_LLI_DEFS_H_*/
