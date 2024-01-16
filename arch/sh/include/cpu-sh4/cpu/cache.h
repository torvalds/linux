/* SPDX-License-Identifier: GPL-2.0
 *
 * include/asm-sh/cpu-sh4/cache.h
 *
 * Copyright (C) 1999 Niibe Yutaka
 */
#ifndef __ASM_CPU_SH4_CACHE_H
#define __ASM_CPU_SH4_CACHE_H

#define L1_CACHE_SHIFT	5

#define SH_CACHE_VALID		1
#define SH_CACHE_UPDATED	2
#define SH_CACHE_COMBINED	4
#define SH_CACHE_ASSOC		8

#define SH_CCR		0xff00001c	/* Address of Cache Control Register */
#define CCR_CACHE_OCE	0x0001	/* Operand Cache Enable */
#define CCR_CACHE_WT	0x0002	/* Write-Through (for P0,U0,P3) (else writeback)*/
#define CCR_CACHE_CB	0x0004	/* Copy-Back (for P1) (else writethrough) */
#define CCR_CACHE_OCI	0x0008	/* OC Invalidate */
#define CCR_CACHE_ORA	0x0020	/* OC RAM Mode */
#define CCR_CACHE_OIX	0x0080	/* OC Index Enable */
#define CCR_CACHE_ICE	0x0100	/* Instruction Cache Enable */
#define CCR_CACHE_ICI	0x0800	/* IC Invalidate */
#define CCR_CACHE_IIX	0x8000	/* IC Index Enable */
#ifndef CONFIG_CPU_SH4A
#define CCR_CACHE_EMODE	0x80000000	/* EMODE Enable */
#endif

/* Default CCR setup: 8k+16k-byte cache,P1-wb,enable */
#define CCR_CACHE_ENABLE	(CCR_CACHE_OCE|CCR_CACHE_ICE)
#define CCR_CACHE_INVALIDATE	(CCR_CACHE_OCI|CCR_CACHE_ICI)

#define CACHE_IC_ADDRESS_ARRAY	0xf0000000
#define CACHE_OC_ADDRESS_ARRAY	0xf4000000

#define RAMCR			0xFF000074

#endif /* __ASM_CPU_SH4_CACHE_H */

