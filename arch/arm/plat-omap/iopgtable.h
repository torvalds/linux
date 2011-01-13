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

#ifndef __PLAT_OMAP_IOMMU_H
#define __PLAT_OMAP_IOMMU_H

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

/*
 * some descriptor attributes.
 */
#define IOPGD_TABLE		(1 << 0)
#define IOPGD_SECTION		(2 << 0)
#define IOPGD_SUPER		(1 << 18 | 2 << 0)

#define iopgd_is_table(x)	(((x) & 3) == IOPGD_TABLE)

#define IOPTE_SMALL		(2 << 0)
#define IOPTE_LARGE		(1 << 0)

/* to find an entry in a page-table-directory */
#define iopgd_index(da)		(((da) >> IOPGD_SHIFT) & (PTRS_PER_IOPGD - 1))
#define iopgd_offset(obj, da)	((obj)->iopgd + iopgd_index(da))

#define iopgd_page_paddr(iopgd)	(*iopgd & ~((1 << 10) - 1))
#define iopgd_page_vaddr(iopgd)	((u32 *)phys_to_virt(iopgd_page_paddr(iopgd)))

/* to find an entry in the second-level page table. */
#define iopte_index(da)		(((da) >> IOPTE_SHIFT) & (PTRS_PER_IOPTE - 1))
#define iopte_offset(iopgd, da)	(iopgd_page_vaddr(iopgd) + iopte_index(da))

static inline u32 iotlb_init_entry(struct iotlb_entry *e, u32 da, u32 pa,
				   u32 flags)
{
	memset(e, 0, sizeof(*e));

	e->da		= da;
	e->pa		= pa;
	e->valid	= 1;
	/* FIXME: add OMAP1 support */
	e->pgsz		= flags & MMU_CAM_PGSZ_MASK;
	e->endian	= flags & MMU_RAM_ENDIAN_MASK;
	e->elsz		= flags & MMU_RAM_ELSZ_MASK;
	e->mixed	= flags & MMU_RAM_MIXED_MASK;

	return iopgsz_to_bytes(e->pgsz);
}

#define to_iommu(dev)							\
	(struct iommu *)platform_get_drvdata(to_platform_device(dev))

#endif /* __PLAT_OMAP_IOMMU_H */
