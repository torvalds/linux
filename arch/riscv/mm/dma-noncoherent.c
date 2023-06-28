// SPDX-License-Identifier: GPL-2.0-only
/*
 * RISC-V specific functions to support DMA for non-coherent devices
 *
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 */

#include <linux/dma-map-ops.h>

#include <soc/sifive/sifive_l2_cache.h>

void arch_sync_dma_for_device(phys_addr_t paddr, size_t size, enum dma_data_direction dir)
{
	if (sifive_l2_handle_noncoherent())
		sifive_l2_flush_range(paddr, size);
}

void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size, enum dma_data_direction dir)
{
	if (sifive_l2_handle_noncoherent())
		sifive_l2_flush_range(paddr, size);
}

void *arch_dma_set_uncached(void *addr, size_t size)
{
	if (sifive_l2_handle_noncoherent())
		return sifive_l2_set_uncached(addr, size);

	return addr;
}

void arch_dma_clear_uncached(void *addr, size_t size)
{
	if (sifive_l2_handle_noncoherent())
		sifive_l2_clear_uncached(addr, size);
}

void arch_dma_prep_coherent(struct page *page, size_t size)
{
	void *flush_addr = page_address(page);

	memset(flush_addr, 0, size);
	if (sifive_l2_handle_noncoherent())
		sifive_l2_flush_range(__pa(flush_addr), size);
}

void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
		const struct iommu_ops *iommu, bool coherent)
{
	/* If a specific device is dma-coherent, set it here */
	dev->dma_coherent = coherent;
}
