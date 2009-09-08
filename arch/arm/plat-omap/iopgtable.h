/*
 * omap iommu: pagetable definitions
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __PLAT_OMAP_IOMMU_H
#define __PLAT_OMAP_IOMMU_H

#define IOPGD_SHIFT		20
#define IOPGD_SIZE		(1 << IOPGD_SHIFT)
#define IOPGD_MASK		(~(IOPGD_SIZE - 1))
#define IOSECTION_MASK		IOPGD_MASK
#define PTRS_PER_IOPGD		(1 << (32 - IOPGD_SHIFT))
#define IOPGD_TABLE_SIZE	(PTRS_PER_IOPGD * sizeof(u32))

#define IOSUPER_SIZE		(IOPGD_SIZE << 4)
#define IOSUPER_MASK		(~(IOSUPER_SIZE - 1))

#define IOPTE_SHIFT		12
#define IOPTE_SIZE		(1 << IOPTE_SHIFT)
#define IOPTE_MASK		(~(IOPTE_SIZE - 1))
#define IOPAGE_MASK		IOPTE_MASK
#define PTRS_PER_IOPTE		(1 << (IOPGD_SHIFT - IOPTE_SHIFT))
#define IOPTE_TABLE_SIZE	(PTRS_PER_IOPTE * sizeof(u32))

#define IOLARGE_SIZE		(IOPTE_SIZE << 4)
#define IOLARGE_MASK		(~(IOLARGE_SIZE - 1))

#define IOPGD_TABLE		(1 << 0)
#define IOPGD_SECTION		(2 << 0)
#define IOPGD_SUPER		(1 << 18 | 2 << 0)

#define IOPTE_SMALL		(2 << 0)
#define IOPTE_LARGE		(1 << 0)

#define iopgd_index(da)		(((da) >> IOPGD_SHIFT) & (PTRS_PER_IOPGD - 1))
#define iopgd_offset(obj, da)	((obj)->iopgd + iopgd_index(da))

#define iopte_paddr(iopgd)	(*iopgd & ~((1 << 10) - 1))
#define iopte_vaddr(iopgd)	((u32 *)phys_to_virt(iopte_paddr(iopgd)))

#define iopte_index(da)		(((da) >> IOPTE_SHIFT) & (PTRS_PER_IOPTE - 1))
#define iopte_offset(iopgd, da)	(iopte_vaddr(iopgd) + iopte_index(da))

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
