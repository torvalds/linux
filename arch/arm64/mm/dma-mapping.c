// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
 */

#include <linux/gfp.h>
#include <linux/cache.h>
#include <linux/dma-noncoherent.h>
#include <linux/dma-iommu.h>

#include <asm/cacheflush.h>

pgprot_t arch_dma_mmap_pgprot(struct device *dev, pgprot_t prot,
		unsigned long attrs)
{
	if (!dev_is_dma_coherent(dev) || (attrs & DMA_ATTR_WRITE_COMBINE))
		return pgprot_writecombine(prot);
	return prot;
}

void arch_sync_dma_for_device(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir)
{
	__dma_map_area(phys_to_virt(paddr), size, dir);
}

void arch_sync_dma_for_cpu(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir)
{
	__dma_unmap_area(phys_to_virt(paddr), size, dir);
}

void arch_dma_prep_coherent(struct page *page, size_t size)
{
	__dma_flush_area(page_address(page), size);
}

static int __init arm64_dma_init(void)
{
	WARN_TAINT(ARCH_DMA_MINALIGN < cache_line_size(),
		   TAINT_CPU_OUT_OF_SPEC,
		   "ARCH_DMA_MINALIGN smaller than CTR_EL0.CWG (%d < %d)",
		   ARCH_DMA_MINALIGN, cache_line_size());
	return dma_atomic_pool_init(GFP_DMA32, __pgprot(PROT_NORMAL_NC));
}
arch_initcall(arm64_dma_init);

#ifdef CONFIG_IOMMU_DMA
void arch_teardown_dma_ops(struct device *dev)
{
	dev->dma_ops = NULL;
}
#endif

void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
			const struct iommu_ops *iommu, bool coherent)
{
	dev->dma_coherent = coherent;
	if (iommu)
		iommu_setup_dma_ops(dev, dma_base, size);

#ifdef CONFIG_XEN
	if (xen_initial_domain())
		dev->dma_ops = xen_dma_ops;
#endif
}
