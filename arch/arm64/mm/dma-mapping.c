// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
 */

#include <linux/gfp.h>
#include <linux/cache.h>
#include <linux/dma-map-ops.h>
#include <linux/dma-iommu.h>
#include <xen/xen.h>
#include <xen/swiotlb-xen.h>
#include <trace/hooks/iommu.h>

#include <asm/cacheflush.h>

void arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	__dma_map_area(phys_to_virt(paddr), size, dir);
}

void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	__dma_unmap_area(phys_to_virt(paddr), size, dir);
}

void arch_dma_prep_coherent(struct page *page, size_t size)
{
	__dma_flush_area(page_address(page), size);
}

#ifdef CONFIG_IOMMU_DMA
void arch_teardown_dma_ops(struct device *dev)
{
	dev->dma_ops = NULL;
}
#endif

void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
			const struct iommu_ops *iommu, bool coherent)
{
	int cls = cache_line_size_of_cpu();

	WARN_TAINT(!coherent && cls > ARCH_DMA_MINALIGN,
		   TAINT_CPU_OUT_OF_SPEC,
		   "%s %s: ARCH_DMA_MINALIGN smaller than CTR_EL0.CWG (%d < %d)",
		   dev_driver_string(dev), dev_name(dev),
		   ARCH_DMA_MINALIGN, cls);

	dev->dma_coherent = coherent;
	if (iommu) {
		iommu_setup_dma_ops(dev, dma_base, size);
		trace_android_vh_iommu_setup_dma_ops(dev, dma_base, size);
	}

#ifdef CONFIG_XEN
	if (xen_initial_domain())
		dev->dma_ops = &xen_swiotlb_dma_ops;
#endif
}
