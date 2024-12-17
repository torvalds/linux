/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/include/asm/pgtable-3level-hwdef.h
 *
 * Copyright (C) 2011 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
 */
#ifndef _ASM_PGTABLE_3LEVEL_HWDEF_H
#define _ASM_PGTABLE_3LEVEL_HWDEF_H

/*
 * Hardware page table definitions.
 *
 * + Level 1/2 descriptor
 *   - common
 */
#define PUD_TABLE_BIT		(_AT(pmdval_t, 1) << 1)
#define PMD_TYPE_MASK		(_AT(pmdval_t, 3) << 0)
#define PMD_TYPE_FAULT		(_AT(pmdval_t, 0) << 0)
#define PMD_TYPE_TABLE		(_AT(pmdval_t, 3) << 0)
#define PMD_TYPE_SECT		(_AT(pmdval_t, 1) << 0)
#define PMD_TABLE_BIT		(_AT(pmdval_t, 1) << 1)
#define PMD_BIT4		(_AT(pmdval_t, 0))
#define PMD_DOMAIN(x)		(_AT(pmdval_t, 0))
#define PMD_APTABLE_SHIFT	(61)
#define PMD_APTABLE		(_AT(pgdval_t, 3) << PGD_APTABLE_SHIFT)
#define PMD_PXNTABLE		(_AT(pgdval_t, 1) << 59)

/*
 *   - section
 */
#define PMD_SECT_BUFFERABLE	(_AT(pmdval_t, 1) << 2)
#define PMD_SECT_CACHEABLE	(_AT(pmdval_t, 1) << 3)
#define PMD_SECT_USER		(_AT(pmdval_t, 1) << 6)		/* AP[1] */
#define PMD_SECT_AP2		(_AT(pmdval_t, 1) << 7)		/* read only */
#define PMD_SECT_S		(_AT(pmdval_t, 3) << 8)
#define PMD_SECT_AF		(_AT(pmdval_t, 1) << 10)
#define PMD_SECT_nG		(_AT(pmdval_t, 1) << 11)
#define PMD_SECT_PXN		(_AT(pmdval_t, 1) << 53)
#define PMD_SECT_XN		(_AT(pmdval_t, 1) << 54)
#define PMD_SECT_AP_WRITE	(_AT(pmdval_t, 0))
#define PMD_SECT_AP_READ	(_AT(pmdval_t, 0))
#define PMD_SECT_AP1		(_AT(pmdval_t, 1) << 6)
#define PMD_SECT_TEX(x)		(_AT(pmdval_t, 0))

/*
 * AttrIndx[2:0] encoding (mapping attributes defined in the MAIR* registers).
 */
#define PMD_SECT_UNCACHED	(_AT(pmdval_t, 0) << 2)	/* strongly ordered */
#define PMD_SECT_BUFFERED	(_AT(pmdval_t, 1) << 2)	/* normal non-cacheable */
#define PMD_SECT_WT		(_AT(pmdval_t, 2) << 2)	/* normal inner write-through */
#define PMD_SECT_WB		(_AT(pmdval_t, 3) << 2)	/* normal inner write-back */
#define PMD_SECT_WBWA		(_AT(pmdval_t, 7) << 2)	/* normal inner write-alloc */
#define PMD_SECT_CACHE_MASK	(_AT(pmdval_t, 7) << 2)

/*
 * + Level 3 descriptor (PTE)
 */
#define PTE_TYPE_MASK		(_AT(pteval_t, 3) << 0)
#define PTE_TYPE_FAULT		(_AT(pteval_t, 0) << 0)
#define PTE_TYPE_PAGE		(_AT(pteval_t, 3) << 0)
#define PTE_TABLE_BIT		(_AT(pteval_t, 1) << 1)
#define PTE_BUFFERABLE		(_AT(pteval_t, 1) << 2)		/* AttrIndx[0] */
#define PTE_CACHEABLE		(_AT(pteval_t, 1) << 3)		/* AttrIndx[1] */
#define PTE_AP2			(_AT(pteval_t, 1) << 7)		/* AP[2] */
#define PTE_EXT_SHARED		(_AT(pteval_t, 3) << 8)		/* SH[1:0], inner shareable */
#define PTE_EXT_AF		(_AT(pteval_t, 1) << 10)	/* Access Flag */
#define PTE_EXT_NG		(_AT(pteval_t, 1) << 11)	/* nG */
#define PTE_EXT_PXN		(_AT(pteval_t, 1) << 53)	/* PXN */
#define PTE_EXT_XN		(_AT(pteval_t, 1) << 54)	/* XN */

/*
 * 40-bit physical address supported.
 */
#define PHYS_MASK_SHIFT		(40)
#define PHYS_MASK		((1ULL << PHYS_MASK_SHIFT) - 1)

#ifndef CONFIG_CPU_TTBR0_PAN
/*
 * TTBR0/TTBR1 split (PAGE_OFFSET):
 *   0x40000000: T0SZ = 2, T1SZ = 0 (not used)
 *   0x80000000: T0SZ = 0, T1SZ = 1
 *   0xc0000000: T0SZ = 0, T1SZ = 2
 *
 * Only use this feature if PHYS_OFFSET <= PAGE_OFFSET, otherwise
 * booting secondary CPUs would end up using TTBR1 for the identity
 * mapping set up in TTBR0.
 */
#if defined CONFIG_VMSPLIT_2G
#define TTBR1_OFFSET	16			/* skip two L1 entries */
#elif defined CONFIG_VMSPLIT_3G
#define TTBR1_OFFSET	(4096 * (1 + 3))	/* only L2, skip pgd + 3*pmd */
#else
#define TTBR1_OFFSET	0
#endif

#define TTBR1_SIZE	(((PAGE_OFFSET >> 30) - 1) << 16)
#else
/*
 * With CONFIG_CPU_TTBR0_PAN enabled, TTBR1 is only used during uaccess
 * disabled regions when TTBR0 is disabled.
 */
#define TTBR1_OFFSET	0			/* pointing to swapper_pg_dir */
#define TTBR1_SIZE	0			/* TTBR1 size controlled via TTBCR.T0SZ */
#endif

/*
 * TTBCR register bits.
 *
 * The ORGN0 and IRGN0 bits enables different forms of caching when
 * walking the translation table. Clearing these bits (which is claimed
 * to be the reset default) means "normal memory, [outer|inner]
 * non-cacheable"
 */
#define TTBCR_EAE		(1 << 31)
#define TTBCR_IMP		(1 << 30)
#define TTBCR_SH1_MASK		(3 << 28)
#define TTBCR_ORGN1_MASK	(3 << 26)
#define TTBCR_IRGN1_MASK	(3 << 24)
#define TTBCR_EPD1		(1 << 23)
#define TTBCR_A1		(1 << 22)
#define TTBCR_T1SZ_MASK		(7 << 16)
#define TTBCR_SH0_MASK		(3 << 12)
#define TTBCR_ORGN0_MASK	(3 << 10)
#define TTBCR_IRGN0_MASK	(3 << 8)
#define TTBCR_EPD0		(1 << 7)
#define TTBCR_T0SZ_MASK		(7 << 0)

#endif
