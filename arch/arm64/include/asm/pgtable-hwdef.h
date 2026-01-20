/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_PGTABLE_HWDEF_H
#define __ASM_PGTABLE_HWDEF_H

#include <asm/memory.h>

#define PTDESC_ORDER 3

/* Number of VA bits resolved by a single translation table level */
#define PTDESC_TABLE_SHIFT	(PAGE_SHIFT - PTDESC_ORDER)

/*
 * Number of page-table levels required to address 'va_bits' wide
 * address, without section mapping. We resolve the top (va_bits - PAGE_SHIFT)
 * bits with PTDESC_TABLE_SHIFT bits at each page table level. Hence:
 *
 *  levels = DIV_ROUND_UP((va_bits - PAGE_SHIFT), PTDESC_TABLE_SHIFT)
 *
 * where DIV_ROUND_UP(n, d) => (((n) + (d) - 1) / (d))
 *
 * We cannot include linux/kernel.h which defines DIV_ROUND_UP here
 * due to build issues. So we open code DIV_ROUND_UP here:
 *
 *	((((va_bits) - PAGE_SHIFT) + PTDESC_TABLE_SHIFT - 1) / PTDESC_TABLE_SHIFT)
 *
 * which gets simplified as :
 */
#define ARM64_HW_PGTABLE_LEVELS(va_bits) \
	(((va_bits) - PTDESC_ORDER - 1) / PTDESC_TABLE_SHIFT)

/*
 * Size mapped by an entry at level n ( -1 <= n <= 3)
 * We map PTDESC_TABLE_SHIFT at all translation levels and PAGE_SHIFT bits
 * in the final page. The maximum number of translation levels supported by
 * the architecture is 5. Hence, starting at level n, we have further
 * ((4 - n) - 1) levels of translation excluding the offset within the page.
 * So, the total number of bits mapped by an entry at level n is :
 *
 *  ((4 - n) - 1) * PTDESC_TABLE_SHIFT + PAGE_SHIFT
 *
 * Rearranging it a bit we get :
 *   (4 - n) * PTDESC_TABLE_SHIFT + PTDESC_ORDER
 */
#define ARM64_HW_PGTABLE_LEVEL_SHIFT(n)	(PTDESC_TABLE_SHIFT * (4 - (n)) + PTDESC_ORDER)

#define PTRS_PER_PTE		(1 << PTDESC_TABLE_SHIFT)

/*
 * PMD_SHIFT determines the size a level 2 page table entry can map.
 */
#if CONFIG_PGTABLE_LEVELS > 2
#define PMD_SHIFT		ARM64_HW_PGTABLE_LEVEL_SHIFT(2)
#define PMD_SIZE		(_AC(1, UL) << PMD_SHIFT)
#define PMD_MASK		(~(PMD_SIZE-1))
#define PTRS_PER_PMD		(1 << PTDESC_TABLE_SHIFT)
#endif

/*
 * PUD_SHIFT determines the size a level 1 page table entry can map.
 */
#if CONFIG_PGTABLE_LEVELS > 3
#define PUD_SHIFT		ARM64_HW_PGTABLE_LEVEL_SHIFT(1)
#define PUD_SIZE		(_AC(1, UL) << PUD_SHIFT)
#define PUD_MASK		(~(PUD_SIZE-1))
#define PTRS_PER_PUD		(1 << PTDESC_TABLE_SHIFT)
#endif

#if CONFIG_PGTABLE_LEVELS > 4
#define P4D_SHIFT		ARM64_HW_PGTABLE_LEVEL_SHIFT(0)
#define P4D_SIZE		(_AC(1, UL) << P4D_SHIFT)
#define P4D_MASK		(~(P4D_SIZE-1))
#define PTRS_PER_P4D		(1 << PTDESC_TABLE_SHIFT)
#endif

/*
 * PGDIR_SHIFT determines the size a top-level page table entry can map
 * (depending on the configuration, this level can be -1, 0, 1 or 2).
 */
#define PGDIR_SHIFT		ARM64_HW_PGTABLE_LEVEL_SHIFT(4 - CONFIG_PGTABLE_LEVELS)
#define PGDIR_SIZE		(_AC(1, UL) << PGDIR_SHIFT)
#define PGDIR_MASK		(~(PGDIR_SIZE-1))
#define PTRS_PER_PGD		(1 << (VA_BITS - PGDIR_SHIFT))

/*
 * Contiguous page definitions.
 */
#define CONT_PTE_SHIFT		(CONFIG_ARM64_CONT_PTE_SHIFT + PAGE_SHIFT)
#define CONT_PTES		(1 << (CONT_PTE_SHIFT - PAGE_SHIFT))
#define CONT_PTE_SIZE		(CONT_PTES * PAGE_SIZE)
#define CONT_PTE_MASK		(~(CONT_PTE_SIZE - 1))

#define CONT_PMD_SHIFT		(CONFIG_ARM64_CONT_PMD_SHIFT + PMD_SHIFT)
#define CONT_PMDS		(1 << (CONT_PMD_SHIFT - PMD_SHIFT))
#define CONT_PMD_SIZE		(CONT_PMDS * PMD_SIZE)
#define CONT_PMD_MASK		(~(CONT_PMD_SIZE - 1))

/*
 * Hardware page table definitions.
 *
 * Level -1 descriptor (PGD).
 */
#define PGD_TYPE_TABLE		(_AT(pgdval_t, 3) << 0)
#define PGD_TYPE_MASK		(_AT(pgdval_t, 3) << 0)
#define PGD_TABLE_AF		(_AT(pgdval_t, 1) << 10)	/* Ignored if no FEAT_HAFT */
#define PGD_TABLE_PXN		(_AT(pgdval_t, 1) << 59)
#define PGD_TABLE_UXN		(_AT(pgdval_t, 1) << 60)

/*
 * Level 0 descriptor (P4D).
 */
#define P4D_TYPE_TABLE		(_AT(p4dval_t, 3) << 0)
#define P4D_TYPE_MASK		(_AT(p4dval_t, 3) << 0)
#define P4D_TYPE_SECT		(_AT(p4dval_t, 1) << 0)
#define P4D_SECT_RDONLY		(_AT(p4dval_t, 1) << 7)		/* AP[2] */
#define P4D_TABLE_AF		(_AT(p4dval_t, 1) << 10)	/* Ignored if no FEAT_HAFT */
#define P4D_TABLE_PXN		(_AT(p4dval_t, 1) << 59)
#define P4D_TABLE_UXN		(_AT(p4dval_t, 1) << 60)

/*
 * Level 1 descriptor (PUD).
 */
#define PUD_TYPE_TABLE		(_AT(pudval_t, 3) << 0)
#define PUD_TYPE_MASK		(_AT(pudval_t, 3) << 0)
#define PUD_TYPE_SECT		(_AT(pudval_t, 1) << 0)
#define PUD_SECT_RDONLY		(_AT(pudval_t, 1) << 7)		/* AP[2] */
#define PUD_TABLE_AF		(_AT(pudval_t, 1) << 10)	/* Ignored if no FEAT_HAFT */
#define PUD_TABLE_PXN		(_AT(pudval_t, 1) << 59)
#define PUD_TABLE_UXN		(_AT(pudval_t, 1) << 60)

/*
 * Level 2 descriptor (PMD).
 */
#define PMD_TYPE_MASK		(_AT(pmdval_t, 3) << 0)
#define PMD_TYPE_TABLE		(_AT(pmdval_t, 3) << 0)
#define PMD_TYPE_SECT		(_AT(pmdval_t, 1) << 0)
#define PMD_TABLE_AF		(_AT(pmdval_t, 1) << 10)	/* Ignored if no FEAT_HAFT */

/*
 * Section
 */
#define PMD_SECT_USER		(_AT(pmdval_t, 1) << 6)		/* AP[1] */
#define PMD_SECT_RDONLY		(_AT(pmdval_t, 1) << 7)		/* AP[2] */
#define PMD_SECT_S		(_AT(pmdval_t, 3) << 8)
#define PMD_SECT_AF		(_AT(pmdval_t, 1) << 10)
#define PMD_SECT_NG		(_AT(pmdval_t, 1) << 11)
#define PMD_SECT_CONT		(_AT(pmdval_t, 1) << 52)
#define PMD_SECT_PXN		(_AT(pmdval_t, 1) << 53)
#define PMD_SECT_UXN		(_AT(pmdval_t, 1) << 54)
#define PMD_TABLE_PXN		(_AT(pmdval_t, 1) << 59)
#define PMD_TABLE_UXN		(_AT(pmdval_t, 1) << 60)

/*
 * AttrIndx[2:0] encoding (mapping attributes defined in the MAIR* registers).
 */
#define PMD_ATTRINDX(t)		(_AT(pmdval_t, (t)) << 2)
#define PMD_ATTRINDX_MASK	(_AT(pmdval_t, 7) << 2)

/*
 * Level 3 descriptor (PTE).
 */
#define PTE_VALID		(_AT(pteval_t, 1) << 0)
#define PTE_TYPE_MASK		(_AT(pteval_t, 3) << 0)
#define PTE_TYPE_PAGE		(_AT(pteval_t, 3) << 0)
#define PTE_USER		(_AT(pteval_t, 1) << 6)		/* AP[1] */
#define PTE_RDONLY		(_AT(pteval_t, 1) << 7)		/* AP[2] */
#define PTE_SHARED		(_AT(pteval_t, 3) << 8)		/* SH[1:0], inner shareable */
#define PTE_AF			(_AT(pteval_t, 1) << 10)	/* Access Flag */
#define PTE_NG			(_AT(pteval_t, 1) << 11)	/* nG */
#define PTE_GP			(_AT(pteval_t, 1) << 50)	/* BTI guarded */
#define PTE_DBM			(_AT(pteval_t, 1) << 51)	/* Dirty Bit Management */
#define PTE_CONT		(_AT(pteval_t, 1) << 52)	/* Contiguous range */
#define PTE_PXN			(_AT(pteval_t, 1) << 53)	/* Privileged XN */
#define PTE_UXN			(_AT(pteval_t, 1) << 54)	/* User XN */
#define PTE_SWBITS_MASK		_AT(pteval_t, (BIT(63) | GENMASK(58, 55)))

#define PTE_ADDR_LOW		(((_AT(pteval_t, 1) << (50 - PAGE_SHIFT)) - 1) << PAGE_SHIFT)
#ifdef CONFIG_ARM64_PA_BITS_52
#ifdef CONFIG_ARM64_64K_PAGES
#define PTE_ADDR_HIGH		(_AT(pteval_t, 0xf) << 12)
#define PTE_ADDR_HIGH_SHIFT	36
#define PHYS_TO_PTE_ADDR_MASK	(PTE_ADDR_LOW | PTE_ADDR_HIGH)
#else
#define PTE_ADDR_HIGH		(_AT(pteval_t, 0x3) << 8)
#define PTE_ADDR_HIGH_SHIFT	42
#define PHYS_TO_PTE_ADDR_MASK	GENMASK_ULL(49, 8)
#endif
#endif

/*
 * AttrIndx[2:0] encoding (mapping attributes defined in the MAIR* registers).
 */
#define PTE_ATTRINDX(t)		(_AT(pteval_t, (t)) << 2)
#define PTE_ATTRINDX_MASK	(_AT(pteval_t, 7) << 2)

/*
 * PIIndex[3:0] encoding (Permission Indirection Extension)
 */
#define PTE_PI_IDX_0	6	/* AP[1], USER */
#define PTE_PI_IDX_1	51	/* DBM */
#define PTE_PI_IDX_2	53	/* PXN */
#define PTE_PI_IDX_3	54	/* UXN */

/*
 * POIndex[2:0] encoding (Permission Overlay Extension)
 */
#define PTE_PO_IDX_0	(_AT(pteval_t, 1) << 60)
#define PTE_PO_IDX_1	(_AT(pteval_t, 1) << 61)
#define PTE_PO_IDX_2	(_AT(pteval_t, 1) << 62)

#define PTE_PO_IDX_MASK		GENMASK_ULL(62, 60)


/*
 * Memory Attribute override for Stage-2 (MemAttr[3:0])
 */
#define PTE_S2_MEMATTR(t)	(_AT(pteval_t, (t)) << 2)

/*
 * Hierarchical permission for Stage-1 tables
 */
#define S1_TABLE_AP		(_AT(pmdval_t, 3) << 61)

#define TTBR_CNP_BIT		(UL(1) << 0)

/*
 * TCR flags.
 */
#define TCR_T0SZ(x)		((UL(64) - (x)) << TCR_EL1_T0SZ_SHIFT)
#define TCR_T1SZ(x)		((UL(64) - (x)) << TCR_EL1_T1SZ_SHIFT)

#define TCR_T0SZ_MASK		TCR_EL1_T0SZ_MASK
#define TCR_T1SZ_MASK		TCR_EL1_T1SZ_MASK

#define TCR_EPD0_MASK		TCR_EL1_EPD0_MASK
#define TCR_EPD1_MASK		TCR_EL1_EPD1_MASK

#define TCR_IRGN0_MASK		TCR_EL1_IRGN0_MASK
#define TCR_IRGN0_WBWA		(TCR_EL1_IRGN0_WBWA << TCR_EL1_IRGN0_SHIFT)

#define TCR_ORGN0_MASK		TCR_EL1_ORGN0_MASK
#define TCR_ORGN0_WBWA		(TCR_EL1_ORGN0_WBWA << TCR_EL1_ORGN0_SHIFT)

#define TCR_SH0_MASK		TCR_EL1_SH0_MASK
#define TCR_SH0_INNER		(TCR_EL1_SH0_INNER << TCR_EL1_SH0_SHIFT)

#define TCR_SH1_MASK		TCR_EL1_SH1_MASK

#define TCR_TG0_SHIFT		TCR_EL1_TG0_SHIFT
#define TCR_TG0_MASK		TCR_EL1_TG0_MASK
#define TCR_TG0_4K		(TCR_EL1_TG0_4K << TCR_EL1_TG0_SHIFT)
#define TCR_TG0_64K		(TCR_EL1_TG0_64K << TCR_EL1_TG0_SHIFT)
#define TCR_TG0_16K		(TCR_EL1_TG0_16K << TCR_EL1_TG0_SHIFT)

#define TCR_TG1_SHIFT		TCR_EL1_TG1_SHIFT
#define TCR_TG1_MASK		TCR_EL1_TG1_MASK
#define TCR_TG1_16K		(TCR_EL1_TG1_16K << TCR_EL1_TG1_SHIFT)
#define TCR_TG1_4K		(TCR_EL1_TG1_4K << TCR_EL1_TG1_SHIFT)
#define TCR_TG1_64K		(TCR_EL1_TG1_64K << TCR_EL1_TG1_SHIFT)

#define TCR_IPS_SHIFT		TCR_EL1_IPS_SHIFT
#define TCR_IPS_MASK		TCR_EL1_IPS_MASK
#define TCR_A1			TCR_EL1_A1
#define TCR_ASID16		TCR_EL1_AS
#define TCR_TBI0		TCR_EL1_TBI0
#define TCR_TBI1		TCR_EL1_TBI1
#define TCR_HA			TCR_EL1_HA
#define TCR_HD			TCR_EL1_HD
#define TCR_HPD0		TCR_EL1_HPD0
#define TCR_HPD1		TCR_EL1_HPD1
#define TCR_TBID0		TCR_EL1_TBID0
#define TCR_TBID1		TCR_EL1_TBID1
#define TCR_E0PD0		TCR_EL1_E0PD0
#define TCR_E0PD1		TCR_EL1_E0PD1
#define TCR_DS			TCR_EL1_DS

/*
 * TTBR.
 */
#ifdef CONFIG_ARM64_PA_BITS_52
/*
 * TTBR_ELx[1] is RES0 in this configuration.
 */
#define TTBR_BADDR_MASK_52	GENMASK_ULL(47, 2)
#endif

#ifdef CONFIG_ARM64_VA_BITS_52
/* Must be at least 64-byte aligned to prevent corruption of the TTBR */
#define TTBR1_BADDR_4852_OFFSET	(((UL(1) << (52 - PGDIR_SHIFT)) - \
				 (UL(1) << (48 - PGDIR_SHIFT))) * 8)
#endif

#endif
