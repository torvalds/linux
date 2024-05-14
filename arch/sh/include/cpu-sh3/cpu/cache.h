/* SPDX-License-Identifier: GPL-2.0
 *
 * include/asm-sh/cpu-sh3/cache.h
 *
 * Copyright (C) 1999 Niibe Yutaka
 */
#ifndef __ASM_CPU_SH3_CACHE_H
#define __ASM_CPU_SH3_CACHE_H

#define L1_CACHE_SHIFT	4

#define SH_CACHE_VALID		1
#define SH_CACHE_UPDATED	2
#define SH_CACHE_COMBINED	4
#define SH_CACHE_ASSOC		8

#define SH_CCR		0xffffffec	/* Address of Cache Control Register */

#define CCR_CACHE_CE	0x01	/* Cache Enable */
#define CCR_CACHE_WT	0x02	/* Write-Through (for P0,U0,P3) (else writeback) */
#define CCR_CACHE_CB	0x04	/* Write-Back (for P1) (else writethrough) */
#define CCR_CACHE_CF	0x08	/* Cache Flush */
#define CCR_CACHE_ORA	0x20	/* RAM mode */

#define CACHE_OC_ADDRESS_ARRAY	0xf0000000
#define CACHE_PHYSADDR_MASK	0x1ffffc00

#define CCR_CACHE_ENABLE	CCR_CACHE_CE
#define CCR_CACHE_INVALIDATE	CCR_CACHE_CF

#if defined(CONFIG_CPU_SUBTYPE_SH7705) || \
    defined(CONFIG_CPU_SUBTYPE_SH7710) || \
    defined(CONFIG_CPU_SUBTYPE_SH7720) || \
    defined(CONFIG_CPU_SUBTYPE_SH7721)
#define CCR3_REG	0xa40000b4
#define CCR_CACHE_16KB  0x00010000
#define CCR_CACHE_32KB	0x00020000
#endif

#endif /* __ASM_CPU_SH3_CACHE_H */
