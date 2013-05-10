/*
 * include/asm-sh/cpu-sh2/cache.h
 *
 * Copyright (C) 2003 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH2_CACHE_H
#define __ASM_CPU_SH2_CACHE_H

#define L1_CACHE_SHIFT	4

#define SH_CACHE_VALID		1
#define SH_CACHE_UPDATED	2
#define SH_CACHE_COMBINED	4
#define SH_CACHE_ASSOC		8

#if defined(CONFIG_CPU_SUBTYPE_SH7619)
#define CCR		0xffffffec

#define CCR_CACHE_CE	0x01	/* Cache enable */
#define CCR_CACHE_WT	0x02    /* CCR[bit1=1,bit2=1] */
				/* 0x00000000-0x7fffffff: Write-through  */
				/* 0x80000000-0x9fffffff: Write-back     */
                                /* 0xc0000000-0xdfffffff: Write-through  */
#define CCR_CACHE_CB	0x04    /* CCR[bit1=0,bit2=0] */
				/* 0x00000000-0x7fffffff: Write-back     */
				/* 0x80000000-0x9fffffff: Write-through  */
                                /* 0xc0000000-0xdfffffff: Write-back     */
#define CCR_CACHE_CF	0x08	/* Cache invalidate */

#define CACHE_OC_ADDRESS_ARRAY	0xf0000000
#define CACHE_OC_DATA_ARRAY	0xf1000000

#define CCR_CACHE_ENABLE	CCR_CACHE_CE
#define CCR_CACHE_INVALIDATE	CCR_CACHE_CF
#define CACHE_PHYSADDR_MASK	0x1ffffc00

#endif

#endif /* __ASM_CPU_SH2_CACHE_H */
