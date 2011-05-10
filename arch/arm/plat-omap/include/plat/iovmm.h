/*
 * omap iommu: simple virtual address space management
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __IOMMU_MMAP_H
#define __IOMMU_MMAP_H

struct iovm_struct {
	struct iommu		*iommu;	/* iommu object which this belongs to */
	u32			da_start; /* area definition */
	u32			da_end;
	u32			flags; /* IOVMF_: see below */
	struct list_head	list; /* linked in ascending order */
	const struct sg_table	*sgt; /* keep 'page' <-> 'da' mapping */
	void			*va; /* mpu side mapped address */
};

/*
 * IOVMF_FLAGS: attribute for iommu virtual memory area(iovma)
 *
 * lower 16 bit is used for h/w and upper 16 bit is for s/w.
 */
#define IOVMF_SW_SHIFT		16
#define IOVMF_HW_SIZE		(1 << IOVMF_SW_SHIFT)
#define IOVMF_HW_MASK		(IOVMF_HW_SIZE - 1)
#define IOVMF_SW_MASK		(~IOVMF_HW_MASK)UL

/*
 * iovma: h/w flags derived from cam and ram attribute
 */
#define IOVMF_CAM_MASK		(~((1 << 10) - 1))
#define IOVMF_RAM_MASK		(~IOVMF_CAM_MASK)

#define IOVMF_PGSZ_MASK		(3 << 0)
#define IOVMF_PGSZ_1M		MMU_CAM_PGSZ_1M
#define IOVMF_PGSZ_64K		MMU_CAM_PGSZ_64K
#define IOVMF_PGSZ_4K		MMU_CAM_PGSZ_4K
#define IOVMF_PGSZ_16M		MMU_CAM_PGSZ_16M

#define IOVMF_ENDIAN_MASK	(1 << 9)
#define IOVMF_ENDIAN_BIG	MMU_RAM_ENDIAN_BIG
#define IOVMF_ENDIAN_LITTLE	MMU_RAM_ENDIAN_LITTLE

#define IOVMF_ELSZ_MASK		(3 << 7)
#define IOVMF_ELSZ_8		MMU_RAM_ELSZ_8
#define IOVMF_ELSZ_16		MMU_RAM_ELSZ_16
#define IOVMF_ELSZ_32		MMU_RAM_ELSZ_32
#define IOVMF_ELSZ_NONE		MMU_RAM_ELSZ_NONE

#define IOVMF_MIXED_MASK	(1 << 6)
#define IOVMF_MIXED		MMU_RAM_MIXED

/*
 * iovma: s/w flags, used for mapping and umapping internally.
 */
#define IOVMF_MMIO		(1 << IOVMF_SW_SHIFT)
#define IOVMF_ALLOC		(2 << IOVMF_SW_SHIFT)
#define IOVMF_ALLOC_MASK	(3 << IOVMF_SW_SHIFT)

/* "superpages" is supported just with physically linear pages */
#define IOVMF_DISCONT		(1 << (2 + IOVMF_SW_SHIFT))
#define IOVMF_LINEAR		(2 << (2 + IOVMF_SW_SHIFT))
#define IOVMF_LINEAR_MASK	(3 << (2 + IOVMF_SW_SHIFT))

#define IOVMF_DA_FIXED		(1 << (4 + IOVMF_SW_SHIFT))


extern struct iovm_struct *find_iovm_area(struct iommu *obj, u32 da);
extern u32 iommu_vmap(struct iommu *obj, u32 da,
			const struct sg_table *sgt, u32 flags);
extern struct sg_table *iommu_vunmap(struct iommu *obj, u32 da);
extern u32 iommu_vmalloc(struct iommu *obj, u32 da, size_t bytes,
			   u32 flags);
extern void iommu_vfree(struct iommu *obj, const u32 da);
extern u32 iommu_kmap(struct iommu *obj, u32 da, u32 pa, size_t bytes,
			u32 flags);
extern void iommu_kunmap(struct iommu *obj, u32 da);
extern u32 iommu_kmalloc(struct iommu *obj, u32 da, size_t bytes,
			   u32 flags);
extern void iommu_kfree(struct iommu *obj, u32 da);

extern void *da_to_va(struct iommu *obj, u32 da);

#endif /* __IOMMU_MMAP_H */
