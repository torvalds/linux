/*
 * omap iommu: pagetable definitions
 *
 * Copyright (C) 2008-2010 Nokia Corporation
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * "L2 table" address mask and size definitions.
 */
#define IOPGD_SHIFT		20
#define IOPGD_SIZE		(1UL << IOPGD_SHIFT)
#define IOPGD_MASK		(~(IOPGD_SIZE - 1))

/*
 * "section" address mask and size definitions.
 */
#define IOSECTION_SHIFT		20
#define IOSECTION_SIZE		(1UL << IOSECTION_SHIFT)
#define IOSECTION_MASK		(~(IOSECTION_SIZE - 1))

/*
 * "supersection" address mask and size definitions.
 */
#define IOSUPER_SHIFT		24
#define IOSUPER_SIZE		(1UL << IOSUPER_SHIFT)
#define IOSUPER_MASK		(~(IOSUPER_SIZE - 1))

#define PTRS_PER_IOPGD		(1UL << (32 - IOPGD_SHIFT))
#define IOPGD_TABLE_SIZE	(PTRS_PER_IOPGD * sizeof(u32))

/*
 * "small page" address mask and size definitions.
 */
#define IOPTE_SHIFT		12
#define IOPTE_SIZE		(1UL << IOPTE_SHIFT)
#define IOPTE_MASK		(~(IOPTE_SIZE - 1))

/*
 * "large page" address mask and size definitions.
 */
#define IOLARGE_SHIFT		16
#define IOLARGE_SIZE		(1UL << IOLARGE_SHIFT)
#define IOLARGE_MASK		(~(IOLARGE_SIZE - 1))

#define PTRS_PER_IOPTE		(1UL << (IOPGD_SHIFT - IOPTE_SHIFT))
#define IOPTE_TABLE_SIZE	(PTRS_PER_IOPTE * sizeof(u32))

#define IOPAGE_MASK		IOPTE_MASK

/**
 * omap_iommu_translate() - va to pa translation
 * @d:		omap iommu descriptor
 * @va:		virtual address
 * @mask:	omap iommu descriptor mask
 *
 * va to pa translation
 */
static inline phys_addr_t omap_iommu_translate(u32 d, u32 va, u32 mask)
{
	return (d & mask) | (va & (~mask));
}

/*
 * some descriptor attributes.
 */
#define IOPGD_TABLE		(1 << 0)
#define IOPGD_SECTION		(2 << 0)
#define IOPGD_SUPER		(1 << 18 | 2 << 0)

#define iopgd_is_table(x)	(((x) & 3) == IOPGD_TABLE)
#define iopgd_is_section(x)	(((x) & (1 << 18 | 3)) == IOPGD_SECTION)
#define iopgd_is_super(x)	(((x) & (1 << 18 | 3)) == IOPGD_SUPER)

#define IOPTE_SMALL		(2 << 0)
#define IOPTE_LARGE		(1 << 0)

#define iopte_is_small(x)	(((x) & 2) == IOPTE_SMALL)
#define iopte_is_large(x)	(((x) & 3) == IOPTE_LARGE)

/* to find an entry in a page-table-directory */
#define iopgd_index(da)		(((da) >> IOPGD_SHIFT) & (PTRS_PER_IOPGD - 1))
#define iopgd_offset(obj, da)	((obj)->iopgd + iopgd_index(da))

#define iopgd_page_paddr(iopgd)	(*iopgd & ~((1 << 10) - 1))
#define iopgd_page_vaddr(iopgd)	((u32 *)phys_to_virt(iopgd_page_paddr(iopgd)))

/* to find an entry in the second-level page table. */
#define iopte_index(da)		(((da) >> IOPTE_SHIFT) & (PTRS_PER_IOPTE - 1))
#define iopte_offset(iopgd, da)	(iopgd_page_vaddr(iopgd) + iopte_index(da))
