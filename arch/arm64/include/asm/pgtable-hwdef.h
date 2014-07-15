/*
 * Copyright (C) 2012 ARM Ltd.
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_PGTABLE_HWDEF_H
#define __ASM_PGTABLE_HWDEF_H

#if CONFIG_ARM64_PGTABLE_LEVELS == 2
#include <asm/pgtable-2level-hwdef.h>
#elif CONFIG_ARM64_PGTABLE_LEVELS == 3
#include <asm/pgtable-3level-hwdef.h>
#else
#include <asm/pgtable-4level-hwdef.h>
#endif

/*
 * Hardware page table definitions.
 *
 * Level 1 descriptor (PUD).
 */
#define PUD_TYPE_TABLE		(_AT(pudval_t, 3) << 0)
#define PUD_TABLE_BIT		(_AT(pgdval_t, 1) << 1)
#define PUD_TYPE_MASK		(_AT(pgdval_t, 3) << 0)
#define PUD_TYPE_SECT		(_AT(pgdval_t, 1) << 0)

/*
 * Level 2 descriptor (PMD).
 */
#define PMD_TYPE_MASK		(_AT(pmdval_t, 3) << 0)
#define PMD_TYPE_FAULT		(_AT(pmdval_t, 0) << 0)
#define PMD_TYPE_TABLE		(_AT(pmdval_t, 3) << 0)
#define PMD_TYPE_SECT		(_AT(pmdval_t, 1) << 0)
#define PMD_TABLE_BIT		(_AT(pmdval_t, 1) << 1)

/*
 * Section
 */
#define PMD_SECT_VALID		(_AT(pmdval_t, 1) << 0)
#define PMD_SECT_PROT_NONE	(_AT(pmdval_t, 1) << 58)
#define PMD_SECT_USER		(_AT(pmdval_t, 1) << 6)		/* AP[1] */
#define PMD_SECT_RDONLY		(_AT(pmdval_t, 1) << 7)		/* AP[2] */
#define PMD_SECT_S		(_AT(pmdval_t, 3) << 8)
#define PMD_SECT_AF		(_AT(pmdval_t, 1) << 10)
#define PMD_SECT_NG		(_AT(pmdval_t, 1) << 11)
#define PMD_SECT_PXN		(_AT(pmdval_t, 1) << 53)
#define PMD_SECT_UXN		(_AT(pmdval_t, 1) << 54)

/*
 * AttrIndx[2:0] encoding (mapping attributes defined in the MAIR* registers).
 */
#define PMD_ATTRINDX(t)		(_AT(pmdval_t, (t)) << 2)
#define PMD_ATTRINDX_MASK	(_AT(pmdval_t, 7) << 2)

/*
 * Level 3 descriptor (PTE).
 */
#define PTE_TYPE_MASK		(_AT(pteval_t, 3) << 0)
#define PTE_TYPE_FAULT		(_AT(pteval_t, 0) << 0)
#define PTE_TYPE_PAGE		(_AT(pteval_t, 3) << 0)
#define PTE_TABLE_BIT		(_AT(pteval_t, 1) << 1)
#define PTE_USER		(_AT(pteval_t, 1) << 6)		/* AP[1] */
#define PTE_RDONLY		(_AT(pteval_t, 1) << 7)		/* AP[2] */
#define PTE_SHARED		(_AT(pteval_t, 3) << 8)		/* SH[1:0], inner shareable */
#define PTE_AF			(_AT(pteval_t, 1) << 10)	/* Access Flag */
#define PTE_NG			(_AT(pteval_t, 1) << 11)	/* nG */
#define PTE_PXN			(_AT(pteval_t, 1) << 53)	/* Privileged XN */
#define PTE_UXN			(_AT(pteval_t, 1) << 54)	/* User XN */

/*
 * AttrIndx[2:0] encoding (mapping attributes defined in the MAIR* registers).
 */
#define PTE_ATTRINDX(t)		(_AT(pteval_t, (t)) << 2)
#define PTE_ATTRINDX_MASK	(_AT(pteval_t, 7) << 2)

/*
 * 2nd stage PTE definitions
 */
#define PTE_S2_RDONLY		(_AT(pteval_t, 1) << 6)   /* HAP[2:1] */
#define PTE_S2_RDWR		(_AT(pteval_t, 3) << 6)   /* HAP[2:1] */

#define PMD_S2_RDWR		(_AT(pmdval_t, 3) << 6)   /* HAP[2:1] */

/*
 * Memory Attribute override for Stage-2 (MemAttr[3:0])
 */
#define PTE_S2_MEMATTR(t)	(_AT(pteval_t, (t)) << 2)
#define PTE_S2_MEMATTR_MASK	(_AT(pteval_t, 0xf) << 2)

/*
 * EL2/HYP PTE/PMD definitions
 */
#define PMD_HYP			PMD_SECT_USER
#define PTE_HYP			PTE_USER

/*
 * Highest possible physical address supported.
 */
#define PHYS_MASK_SHIFT		(48)
#define PHYS_MASK		((UL(1) << PHYS_MASK_SHIFT) - 1)

/*
 * TCR flags.
 */
#define TCR_TxSZ(x)		(((UL(64) - (x)) << 16) | ((UL(64) - (x)) << 0))
#define TCR_IRGN_NC		((UL(0) << 8) | (UL(0) << 24))
#define TCR_IRGN_WBWA		((UL(1) << 8) | (UL(1) << 24))
#define TCR_IRGN_WT		((UL(2) << 8) | (UL(2) << 24))
#define TCR_IRGN_WBnWA		((UL(3) << 8) | (UL(3) << 24))
#define TCR_IRGN_MASK		((UL(3) << 8) | (UL(3) << 24))
#define TCR_ORGN_NC		((UL(0) << 10) | (UL(0) << 26))
#define TCR_ORGN_WBWA		((UL(1) << 10) | (UL(1) << 26))
#define TCR_ORGN_WT		((UL(2) << 10) | (UL(2) << 26))
#define TCR_ORGN_WBnWA		((UL(3) << 10) | (UL(3) << 26))
#define TCR_ORGN_MASK		((UL(3) << 10) | (UL(3) << 26))
#define TCR_SHARED		((UL(3) << 12) | (UL(3) << 28))
#define TCR_TG0_4K		(UL(0) << 14)
#define TCR_TG0_64K		(UL(1) << 14)
#define TCR_TG0_16K		(UL(2) << 14)
#define TCR_TG1_16K		(UL(1) << 30)
#define TCR_TG1_4K		(UL(2) << 30)
#define TCR_TG1_64K		(UL(3) << 30)
#define TCR_ASID16		(UL(1) << 36)
#define TCR_TBI0		(UL(1) << 37)

#endif
