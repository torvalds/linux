/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * omap iommu: main structures
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 */

#ifndef _OMAP_IOMMU_H
#define _OMAP_IOMMU_H

#include <linux/bitops.h>
#include <linux/iommu.h>

#define for_each_iotlb_cr(obj, n, __i, cr)				\
	for (__i = 0;							\
	     (__i < (n)) && (cr = __iotlb_read_cr((obj), __i), true);	\
	     __i++)

struct iotlb_entry {
	u32 da;
	u32 pa;
	u32 pgsz, prsvd, valid;
	u32 endian, elsz, mixed;
};

/**
 * struct omap_iommu_device - omap iommu device data
 * @pgtable:	page table used by an omap iommu attached to a domain
 * @iommu_dev:	pointer to store an omap iommu instance attached to a domain
 */
struct omap_iommu_device {
	u32 *pgtable;
	struct omap_iommu *iommu_dev;
};

/**
 * struct omap_iommu_domain - omap iommu domain
 * @num_iommus: number of iommus in this domain
 * @iommus:	omap iommu device data for all iommus in this domain
 * @dev:	Device using this domain.
 * @lock:	domain lock, should be taken when attaching/detaching
 * @domain:	generic domain handle used by iommu core code
 */
struct omap_iommu_domain {
	u32 num_iommus;
	struct omap_iommu_device *iommus;
	struct device *dev;
	spinlock_t lock;
	struct iommu_domain domain;
};

struct omap_iommu {
	const char	*name;
	void __iomem	*regbase;
	struct regmap	*syscfg;
	struct device	*dev;
	struct iommu_domain *domain;
	struct dentry	*debug_dir;

	spinlock_t	iommu_lock;	/* global for this whole object */

	/*
	 * We don't change iopgd for a situation like pgd for a task,
	 * but share it globally for each iommu.
	 */
	u32		*iopgd;
	spinlock_t	page_table_lock; /* protect iopgd */
	dma_addr_t	pd_dma;

	int		nr_tlb_entries;

	void *ctx; /* iommu context: registres saved area */

	struct cr_regs *cr_ctx;
	u32 num_cr_ctx;

	int has_bus_err_back;
	u32 id;

	struct iommu_device iommu;
	bool has_iommu_driver;

	u8 pwrst;
};

/**
 * struct omap_iommu_arch_data - omap iommu private data
 * @iommu_dev: handle of the OMAP iommu device
 * @dev: handle of the iommu device
 *
 * This is an omap iommu private data object, which binds an iommu user
 * to its iommu device. This object should be placed at the iommu user's
 * dev_archdata so generic IOMMU API can be used without having to
 * utilize omap-specific plumbing anymore.
 */
struct omap_iommu_arch_data {
	struct omap_iommu *iommu_dev;
	struct device *dev;
};

struct cr_regs {
	u32 cam;
	u32 ram;
};

struct iotlb_lock {
	short base;
	short vict;
};

/*
 * MMU Register offsets
 */
#define MMU_REVISION		0x00
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
#define MMU_GP_REG		0x88

#define MMU_REG_SIZE		256

/*
 * MMU Register bit definitions
 */
/* IRQSTATUS & IRQENABLE */
#define MMU_IRQ_MULTIHITFAULT	BIT(4)
#define MMU_IRQ_TABLEWALKFAULT	BIT(3)
#define MMU_IRQ_EMUMISS		BIT(2)
#define MMU_IRQ_TRANSLATIONFAULT	BIT(1)
#define MMU_IRQ_TLBMISS		BIT(0)

#define __MMU_IRQ_FAULT		\
	(MMU_IRQ_MULTIHITFAULT | MMU_IRQ_EMUMISS | MMU_IRQ_TRANSLATIONFAULT)
#define MMU_IRQ_MASK		\
	(__MMU_IRQ_FAULT | MMU_IRQ_TABLEWALKFAULT | MMU_IRQ_TLBMISS)
#define MMU_IRQ_TWL_MASK	(__MMU_IRQ_FAULT | MMU_IRQ_TABLEWALKFAULT)
#define MMU_IRQ_TLB_MISS_MASK	(__MMU_IRQ_FAULT | MMU_IRQ_TLBMISS)

/* MMU_CNTL */
#define MMU_CNTL_SHIFT		1
#define MMU_CNTL_MASK		(7 << MMU_CNTL_SHIFT)
#define MMU_CNTL_EML_TLB	BIT(3)
#define MMU_CNTL_TWL_EN		BIT(2)
#define MMU_CNTL_MMU_EN		BIT(1)

/* CAM */
#define MMU_CAM_VATAG_SHIFT	12
#define MMU_CAM_VATAG_MASK \
	((~0UL >> MMU_CAM_VATAG_SHIFT) << MMU_CAM_VATAG_SHIFT)
#define MMU_CAM_P		BIT(3)
#define MMU_CAM_V		BIT(2)
#define MMU_CAM_PGSZ_MASK	3
#define MMU_CAM_PGSZ_1M		(0 << 0)
#define MMU_CAM_PGSZ_64K	(1 << 0)
#define MMU_CAM_PGSZ_4K		(2 << 0)
#define MMU_CAM_PGSZ_16M	(3 << 0)

/* RAM */
#define MMU_RAM_PADDR_SHIFT	12
#define MMU_RAM_PADDR_MASK \
	((~0UL >> MMU_RAM_PADDR_SHIFT) << MMU_RAM_PADDR_SHIFT)

#define MMU_RAM_ENDIAN_SHIFT	9
#define MMU_RAM_ENDIAN_MASK	BIT(MMU_RAM_ENDIAN_SHIFT)
#define MMU_RAM_ENDIAN_LITTLE	(0 << MMU_RAM_ENDIAN_SHIFT)
#define MMU_RAM_ENDIAN_BIG	BIT(MMU_RAM_ENDIAN_SHIFT)

#define MMU_RAM_ELSZ_SHIFT	7
#define MMU_RAM_ELSZ_MASK	(3 << MMU_RAM_ELSZ_SHIFT)
#define MMU_RAM_ELSZ_8		(0 << MMU_RAM_ELSZ_SHIFT)
#define MMU_RAM_ELSZ_16		(1 << MMU_RAM_ELSZ_SHIFT)
#define MMU_RAM_ELSZ_32		(2 << MMU_RAM_ELSZ_SHIFT)
#define MMU_RAM_ELSZ_NONE	(3 << MMU_RAM_ELSZ_SHIFT)
#define MMU_RAM_MIXED_SHIFT	6
#define MMU_RAM_MIXED_MASK	BIT(MMU_RAM_MIXED_SHIFT)
#define MMU_RAM_MIXED		MMU_RAM_MIXED_MASK

#define MMU_GP_REG_BUS_ERR_BACK_EN	0x1

#define get_cam_va_mask(pgsz)				\
	(((pgsz) == MMU_CAM_PGSZ_16M) ? 0xff000000 :	\
	 ((pgsz) == MMU_CAM_PGSZ_1M)  ? 0xfff00000 :	\
	 ((pgsz) == MMU_CAM_PGSZ_64K) ? 0xffff0000 :	\
	 ((pgsz) == MMU_CAM_PGSZ_4K)  ? 0xfffff000 : 0)

/*
 * DSP_SYSTEM registers and bit definitions (applicable only for DRA7xx DSP)
 */
#define DSP_SYS_REVISION		0x00
#define DSP_SYS_MMU_CONFIG		0x18
#define DSP_SYS_MMU_CONFIG_EN_SHIFT	4

/*
 * utilities for super page(16MB, 1MB, 64KB and 4KB)
 */

#define iopgsz_max(bytes)			\
	(((bytes) >= SZ_16M) ? SZ_16M :		\
	 ((bytes) >= SZ_1M)  ? SZ_1M  :		\
	 ((bytes) >= SZ_64K) ? SZ_64K :		\
	 ((bytes) >= SZ_4K)  ? SZ_4K  :	0)

#define bytes_to_iopgsz(bytes)				\
	(((bytes) == SZ_16M) ? MMU_CAM_PGSZ_16M :	\
	 ((bytes) == SZ_1M)  ? MMU_CAM_PGSZ_1M  :	\
	 ((bytes) == SZ_64K) ? MMU_CAM_PGSZ_64K :	\
	 ((bytes) == SZ_4K)  ? MMU_CAM_PGSZ_4K  : -1)

#define iopgsz_to_bytes(iopgsz)				\
	(((iopgsz) == MMU_CAM_PGSZ_16M)	? SZ_16M :	\
	 ((iopgsz) == MMU_CAM_PGSZ_1M)	? SZ_1M  :	\
	 ((iopgsz) == MMU_CAM_PGSZ_64K)	? SZ_64K :	\
	 ((iopgsz) == MMU_CAM_PGSZ_4K)	? SZ_4K  : 0)

#define iopgsz_ok(bytes) (bytes_to_iopgsz(bytes) >= 0)

/*
 * global functions
 */

struct cr_regs __iotlb_read_cr(struct omap_iommu *obj, int n);
void iotlb_lock_get(struct omap_iommu *obj, struct iotlb_lock *l);
void iotlb_lock_set(struct omap_iommu *obj, struct iotlb_lock *l);

#ifdef CONFIG_OMAP_IOMMU_DEBUG
void omap_iommu_debugfs_init(void);
void omap_iommu_debugfs_exit(void);

void omap_iommu_debugfs_add(struct omap_iommu *obj);
void omap_iommu_debugfs_remove(struct omap_iommu *obj);
#else
static inline void omap_iommu_debugfs_init(void) { }
static inline void omap_iommu_debugfs_exit(void) { }

static inline void omap_iommu_debugfs_add(struct omap_iommu *obj) { }
static inline void omap_iommu_debugfs_remove(struct omap_iommu *obj) { }
#endif

/*
 * register accessors
 */
static inline u32 iommu_read_reg(struct omap_iommu *obj, size_t offs)
{
	return __raw_readl(obj->regbase + offs);
}

static inline void iommu_write_reg(struct omap_iommu *obj, u32 val, size_t offs)
{
	__raw_writel(val, obj->regbase + offs);
}

static inline int iotlb_cr_valid(struct cr_regs *cr)
{
	if (!cr)
		return -EINVAL;

	return cr->cam & MMU_CAM_V;
}

#endif /* _OMAP_IOMMU_H */
