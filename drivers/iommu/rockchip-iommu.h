/*
 * Data structure definition for Rockchip IOMMU driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/genalloc.h>
#include <linux/iommu.h>

#include <linux/rockchip/sysmmu.h>

#ifdef CONFIG_ROCKCHIP_IOVMM

#define IOVA_START 0x10000000
#define IOVM_SIZE (SZ_1G - SZ_4K) /* last 4K is for error values */

struct rk_vm_region {
	struct list_head node;
	dma_addr_t start;
	size_t size;
};

struct rk_iovmm {
	struct iommu_domain *domain; /* iommu domain for this iovmm */
	struct gen_pool *vmm_pool;
	struct list_head regions_list;	/* list of rk_vm_region */
	spinlock_t lock; /* lock for updating regions_list */
};
#endif


struct sysmmu_drvdata {
	struct list_head node; /* entry of rk_iommu_domain.clients */
	struct device *sysmmu;	/* System MMU's device descriptor */
	struct device *dev;	/* Owner of system MMU */
	int num_res_mem;
	int num_res_irq;	
	const char *dbgname;
	void __iomem **res_bases;
	int activations;
	rwlock_t lock;
	struct iommu_domain *domain; /* domain given to iommu_attach_device() */
	sysmmu_fault_handler_t fault_handler;
	unsigned long pgtable;
#ifdef CONFIG_ROCKCHIP_IOVMM
	struct rk_iovmm vmm;
#endif
};

#ifdef CONFIG_ROCKCHIP_IOVMM
static inline struct rk_iovmm *rockchip_get_iovmm(struct device *dev)
{
	struct sysmmu_drvdata *data = dev_get_drvdata(dev->archdata.iommu);

	BUG_ON(!dev->archdata.iommu || !data);

	return &data->vmm;
}

int rockchip_init_iovmm(struct device *sysmmu, struct rk_iovmm *vmm);
#else
#define rockchip_init_iovmm(sysmmu, vmm) 0
#endif
