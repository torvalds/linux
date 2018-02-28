/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_DMA_DIRECT_H
#define __ASM_DMA_DIRECT_H

#include <linux/jump_label.h>
#include <linux/swiotlb.h>

#include <asm/cache.h>

DECLARE_STATIC_KEY_FALSE(swiotlb_noncoherent_bounce);

static inline dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	dma_addr_t dev_addr = (dma_addr_t)paddr;

	return dev_addr - ((dma_addr_t)dev->dma_pfn_offset << PAGE_SHIFT);
}

static inline phys_addr_t dma_to_phys(struct device *dev, dma_addr_t dev_addr)
{
	phys_addr_t paddr = (phys_addr_t)dev_addr;

	return paddr + ((phys_addr_t)dev->dma_pfn_offset << PAGE_SHIFT);
}

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	if (!dev->dma_mask)
		return false;

	/*
	 * Force swiotlb buffer bouncing when ARCH_DMA_MINALIGN < CWG. The
	 * swiotlb bounce buffers are aligned to (1 << IO_TLB_SHIFT).
	 */
	if (static_branch_unlikely(&swiotlb_noncoherent_bounce) &&
	    !is_device_dma_coherent(dev) &&
	    !is_swiotlb_buffer(dma_to_phys(dev, addr)))
		return false;

	return addr + size - 1 <= *dev->dma_mask;
}

#endif /* __ASM_DMA_DIRECT_H */
