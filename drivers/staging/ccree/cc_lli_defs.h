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

#define CC_MAX_MLLI_ENTRY_SIZE 0xFFFF

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

#define LLI_SIZE_MASK GENMASK((LLI_SIZE_BIT_SIZE - 1), LLI_SIZE_BIT_OFFSET)
#define LLI_HADDR_MASK GENMASK( \
			       (LLI_HADDR_BIT_OFFSET + LLI_HADDR_BIT_SIZE - 1),\
				LLI_HADDR_BIT_OFFSET)

static inline void cc_lli_set_addr(u32 *lli_p, dma_addr_t addr)
{
	lli_p[LLI_WORD0_OFFSET] = (addr & U32_MAX);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	lli_p[LLI_WORD1_OFFSET] &= ~LLI_HADDR_MASK;
	lli_p[LLI_WORD1_OFFSET] |= FIELD_PREP(LLI_HADDR_MASK, (addr >> 16));
#endif /* CONFIG_ARCH_DMA_ADDR_T_64BIT */
}

static inline void cc_lli_set_size(u32 *lli_p, u16 size)
{
	lli_p[LLI_WORD1_OFFSET] &= ~LLI_SIZE_MASK;
	lli_p[LLI_WORD1_OFFSET] |= FIELD_PREP(LLI_SIZE_MASK, size);
}

#endif /*_CC_LLI_DEFS_H_*/
