/*
 * Copyright (C) 2006 Ralf Baechle <ralf@linux-mips.org>
 * Copyright (C) 2009 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __ASM_MACH_BCM3384_DMA_COHERENCE_H
#define __ASM_MACH_BCM3384_DMA_COHERENCE_H

struct device;

extern dma_addr_t plat_map_dma_mem(struct device *dev, void *addr, size_t size);
extern dma_addr_t plat_map_dma_mem_page(struct device *dev, struct page *page);
extern unsigned long plat_dma_addr_to_phys(struct device *dev,
	dma_addr_t dma_addr);

static inline void plat_unmap_dma_mem(struct device *dev, dma_addr_t dma_addr,
	size_t size, enum dma_data_direction direction)
{
}

static inline int plat_dma_supported(struct device *dev, u64 mask)
{
	/*
	 * we fall back to GFP_DMA when the mask isn't all 1s,
	 * so we can't guarantee allocations that must be
	 * within a tighter range than GFP_DMA..
	 */
	if (mask < DMA_BIT_MASK(24))
		return 0;

	return 1;
}

static inline int plat_device_is_coherent(struct device *dev)
{
	return 0;
}

#endif /* __ASM_MACH_BCM3384_DMA_COHERENCE_H */
