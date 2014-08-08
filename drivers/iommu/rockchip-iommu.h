/*
 * Data structure definition for Rockchip IOMMU driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_PLAT_IOMMU_H
#define __ASM_PLAT_IOMMU_H

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/genalloc.h>
#include <linux/iommu.h>

#include <linux/rockchip-iovmm.h>


struct rk_iovmm {
	struct iommu_domain *domain; /* iommu domain for this iovmm */
	struct gen_pool *vmm_pool;
	struct list_head regions_list;	/* list of rk_vm_region */
	spinlock_t lock; /* lock for updating regions_list */
};

struct iommu_drvdata {
	struct list_head node; /* entry of rk_iommu_domain.clients */
	struct device *iommu;	/*  IOMMU's device descriptor */
	struct device *dev;	/* Owner of  IOMMU */
	int num_res_mem;
	int num_res_irq;
	const char *dbgname;
	void __iomem **res_bases;
	int activations;
	rwlock_t lock;
	struct iommu_domain *domain; /* domain given to iommu_attach_device() */
	rockchip_iommu_fault_handler_t fault_handler;
	unsigned long pgtable;
	struct rk_iovmm vmm;
};

#ifdef CONFIG_ROCKCHIP_IOVMM

#define IOVA_START 0x10000000
#define IOVM_SIZE (SZ_1G - SZ_4K) /* last 4K is for error values */

struct rk_vm_region {
	struct list_head node;
	dma_addr_t start;
	size_t size;
};

static inline struct rk_iovmm *rockchip_get_iovmm(struct device *dev)
{
	struct iommu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);

	BUG_ON(!dev->archdata.iommu || !data);

	return &data->vmm;
}

int rockchip_init_iovmm(struct device *iommu, struct rk_iovmm *vmm);
#else
static inline int rockchip_init_iovmm(struct device *iommu,
				struct rk_iovmm *vmm)
{
	return -ENOSYS;
}
#endif


#ifdef CONFIG_ROCKCHIP_IOMMU

/**
* rockchip_iommu_disable() - disable iommu mmu of ip
* @owner: The device whose IOMMU is about to be disabled.
*
* This function disable  iommu to transfer address
 * from virtual address to physical address
 */
bool rockchip_iommu_disable(struct device *owner);

/**
 * rockchip_iommu_tlb_invalidate() - flush all TLB entry in iommu
 * @owner: The device whose IOMMU.
 *
 * This function flush all TLB entry in iommu
 */
void rockchip_iommu_tlb_invalidate(struct device *owner);

#else /* CONFIG_ROCKCHIP_IOMMU */
static inline bool rockchip_iommu_disable(struct device *owner)
{
	return false;
}
static inline void rockchip_iommu_tlb_invalidate(struct device *owner)
{
	return false;
}

#endif

#endif	/*__ASM_PLAT_IOMMU_H*/
