/*
 *  Based on linux/arch/arm/mm/dma-mapping.c
 *
 *  Copyright (C) 2000-2004 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/export.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>

#include <asm/cachetype.h>
#include <asm/cacheflush.h>
#include <asm/outercache.h>
#include <asm/cp15.h>

#include "dma.h"

/*
 *  dma_noop_ops is used if
 *   - MMU/MPU is off
 *   - cpu is v7m w/o cache support
 *   - device is coherent
 *  otherwise arm_nommu_dma_ops is used.
 *
 *  arm_nommu_dma_ops rely on consistent DMA memory (please, refer to
 *  [1] on how to declare such memory).
 *
 *  [1] Documentation/devicetree/bindings/reserved-memory/reserved-memory.txt
 */

static void *arm_nommu_dma_alloc(struct device *dev, size_t size,
				 dma_addr_t *dma_handle, gfp_t gfp,
				 unsigned long attrs)

{
	const struct dma_map_ops *ops = &dma_noop_ops;

	/*
	 * We are here because:
	 * - no consistent DMA region has been defined, so we can't
	 *   continue.
	 * - there is no space left in consistent DMA region, so we
	 *   only can fallback to generic allocator if we are
	 *   advertised that consistency is not required.
	 */

	if (attrs & DMA_ATTR_NON_CONSISTENT)
		return ops->alloc(dev, size, dma_handle, gfp, attrs);

	WARN_ON_ONCE(1);
	return NULL;
}

static void arm_nommu_dma_free(struct device *dev, size_t size,
			       void *cpu_addr, dma_addr_t dma_addr,
			       unsigned long attrs)
{
	const struct dma_map_ops *ops = &dma_noop_ops;

	if (attrs & DMA_ATTR_NON_CONSISTENT)
		ops->free(dev, size, cpu_addr, dma_addr, attrs);
	else
		WARN_ON_ONCE(1);

	return;
}

static void __dma_page_cpu_to_dev(phys_addr_t paddr, size_t size,
				  enum dma_data_direction dir)
{
	dmac_map_area(__va(paddr), size, dir);

	if (dir == DMA_FROM_DEVICE)
		outer_inv_range(paddr, paddr + size);
	else
		outer_clean_range(paddr, paddr + size);
}

static void __dma_page_dev_to_cpu(phys_addr_t paddr, size_t size,
				  enum dma_data_direction dir)
{
	if (dir != DMA_TO_DEVICE) {
		outer_inv_range(paddr, paddr + size);
		dmac_unmap_area(__va(paddr), size, dir);
	}
}

static dma_addr_t arm_nommu_dma_map_page(struct device *dev, struct page *page,
					 unsigned long offset, size_t size,
					 enum dma_data_direction dir,
					 unsigned long attrs)
{
	dma_addr_t handle = page_to_phys(page) + offset;

	__dma_page_cpu_to_dev(handle, size, dir);

	return handle;
}

static void arm_nommu_dma_unmap_page(struct device *dev, dma_addr_t handle,
				     size_t size, enum dma_data_direction dir,
				     unsigned long attrs)
{
	__dma_page_dev_to_cpu(handle, size, dir);
}


static int arm_nommu_dma_map_sg(struct device *dev, struct scatterlist *sgl,
				int nents, enum dma_data_direction dir,
				unsigned long attrs)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sgl, sg, nents, i) {
		sg_dma_address(sg) = sg_phys(sg);
		sg_dma_len(sg) = sg->length;
		__dma_page_cpu_to_dev(sg_dma_address(sg), sg_dma_len(sg), dir);
	}

	return nents;
}

static void arm_nommu_dma_unmap_sg(struct device *dev, struct scatterlist *sgl,
				   int nents, enum dma_data_direction dir,
				   unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i)
		__dma_page_dev_to_cpu(sg_dma_address(sg), sg_dma_len(sg), dir);
}

static void arm_nommu_dma_sync_single_for_device(struct device *dev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	__dma_page_cpu_to_dev(handle, size, dir);
}

static void arm_nommu_dma_sync_single_for_cpu(struct device *dev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	__dma_page_cpu_to_dev(handle, size, dir);
}

static void arm_nommu_dma_sync_sg_for_device(struct device *dev, struct scatterlist *sgl,
					     int nents, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i)
		__dma_page_cpu_to_dev(sg_dma_address(sg), sg_dma_len(sg), dir);
}

static void arm_nommu_dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sgl,
					  int nents, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i)
		__dma_page_dev_to_cpu(sg_dma_address(sg), sg_dma_len(sg), dir);
}

const struct dma_map_ops arm_nommu_dma_ops = {
	.alloc			= arm_nommu_dma_alloc,
	.free			= arm_nommu_dma_free,
	.map_page		= arm_nommu_dma_map_page,
	.unmap_page		= arm_nommu_dma_unmap_page,
	.map_sg			= arm_nommu_dma_map_sg,
	.unmap_sg		= arm_nommu_dma_unmap_sg,
	.sync_single_for_device	= arm_nommu_dma_sync_single_for_device,
	.sync_single_for_cpu	= arm_nommu_dma_sync_single_for_cpu,
	.sync_sg_for_device	= arm_nommu_dma_sync_sg_for_device,
	.sync_sg_for_cpu	= arm_nommu_dma_sync_sg_for_cpu,
};
EXPORT_SYMBOL(arm_nommu_dma_ops);

static const struct dma_map_ops *arm_nommu_get_dma_map_ops(bool coherent)
{
	return coherent ? &dma_noop_ops : &arm_nommu_dma_ops;
}

void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
			const struct iommu_ops *iommu, bool coherent)
{
	const struct dma_map_ops *dma_ops;

	if (IS_ENABLED(CONFIG_CPU_V7M)) {
		/*
		 * Cache support for v7m is optional, so can be treated as
		 * coherent if no cache has been detected. Note that it is not
		 * enough to check if MPU is in use or not since in absense of
		 * MPU system memory map is used.
		 */
		dev->archdata.dma_coherent = (cacheid) ? coherent : true;
	} else {
		/*
		 * Assume coherent DMA in case MMU/MPU has not been set up.
		 */
		dev->archdata.dma_coherent = (get_cr() & CR_M) ? coherent : true;
	}

	dma_ops = arm_nommu_get_dma_map_ops(dev->archdata.dma_coherent);

	set_dma_ops(dev, dma_ops);
}

void arch_teardown_dma_ops(struct device *dev)
{
}

#define PREALLOC_DMA_DEBUG_ENTRIES	4096

static int __init dma_debug_do_init(void)
{
	dma_debug_init(PREALLOC_DMA_DEBUG_ENTRIES);
	return 0;
}
core_initcall(dma_debug_do_init);
