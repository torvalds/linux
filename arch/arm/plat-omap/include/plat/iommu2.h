/*
 * omap iommu: omap2 architecture specific definitions
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MACH_IOMMU2_H
#define __MACH_IOMMU2_H

#include <linux/io.h>

/*
 * MMU Register offsets
 */
#define MMU_REVISION		0x00
#define MMU_SYSCONFIG		0x10
#define MMU_SYSSTATUS		0x14
#define MMU_IRQSTATUS		0x18
#define MMU_IRQENABLE		0x1c
#define MMU_WALKING_ST		0x40
#define MMU_CNTL		0x44
#define MMU_FAULT_AD		0x48
#define MMU_TTB			0x4c
#define MMU_LOCK		0x50
#define MMU_LD_TLB		0x54
#define MMU_CAM			0x58
#define MMU_RAM			0x5c
#define MMU_GFLUSH		0x60
#define MMU_FLUSH_ENTRY		0x64
#define MMU_READ_CAM		0x68
#define MMU_READ_RAM		0x6c
#define MMU_EMU_FAULT_AD	0x70

#define MMU_REG_SIZE		256

/*
 * MMU Register bit definitions
 */
#define MMU_LOCK_BASE_SHIFT	10
#define MMU_LOCK_BASE_MASK	(0x1f << MMU_LOCK_BASE_SHIFT)
#define MMU_LOCK_BASE(x)	\
	((x & MMU_LOCK_BASE_MASK) >> MMU_LOCK_BASE_SHIFT)

#define MMU_LOCK_VICT_SHIFT	4
#define MMU_LOCK_VICT_MASK	(0x1f << MMU_LOCK_VICT_SHIFT)
#define MMU_LOCK_VICT(x)	\
	((x & MMU_LOCK_VICT_MASK) >> MMU_LOCK_VICT_SHIFT)

#define MMU_CAM_VATAG_SHIFT	12
#define MMU_CAM_VATAG_MASK \
	((~0UL >> MMU_CAM_VATAG_SHIFT) << MMU_CAM_VATAG_SHIFT)
#define MMU_CAM_P		(1 << 3)
#define MMU_CAM_V		(1 << 2)
#define MMU_CAM_PGSZ_MASK	3
#define MMU_CAM_PGSZ_1M		(0 << 0)
#define MMU_CAM_PGSZ_64K	(1 << 0)
#define MMU_CAM_PGSZ_4K		(2 << 0)
#define MMU_CAM_PGSZ_16M	(3 << 0)

#define MMU_RAM_PADDR_SHIFT	12
#define MMU_RAM_PADDR_MASK \
	((~0UL >> MMU_RAM_PADDR_SHIFT) << MMU_RAM_PADDR_SHIFT)
#define MMU_RAM_ENDIAN_SHIFT	9
#define MMU_RAM_ENDIAN_MASK	(1 << MMU_RAM_ENDIAN_SHIFT)
#define MMU_RAM_ENDIAN_BIG	(1 << MMU_RAM_ENDIAN_SHIFT)
#define MMU_RAM_ENDIAN_LITTLE	(0 << MMU_RAM_ENDIAN_SHIFT)
#define MMU_RAM_ELSZ_SHIFT	7
#define MMU_RAM_ELSZ_MASK	(3 << MMU_RAM_ELSZ_SHIFT)
#define MMU_RAM_ELSZ_8		(0 << MMU_RAM_ELSZ_SHIFT)
#define MMU_RAM_ELSZ_16		(1 << MMU_RAM_ELSZ_SHIFT)
#define MMU_RAM_ELSZ_32		(2 << MMU_RAM_ELSZ_SHIFT)
#define MMU_RAM_ELSZ_NONE	(3 << MMU_RAM_ELSZ_SHIFT)
#define MMU_RAM_MIXED_SHIFT	6
#define MMU_RAM_MIXED_MASK	(1 << MMU_RAM_MIXED_SHIFT)
#define MMU_RAM_MIXED		MMU_RAM_MIXED_MASK

/*
 * register accessors
 */
static inline u32 iommu_read_reg(struct iommu *obj, size_t offs)
{
	return __raw_readl(obj->regbase + offs);
}

static inline void iommu_write_reg(struct iommu *obj, u32 val, size_t offs)
{
	__raw_writel(val, obj->regbase + offs);
}

#endif /* __MACH_IOMMU2_H */
