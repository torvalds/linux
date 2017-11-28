/*
 * linux/arch/unicore32/include/asm/dma-mapping.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __UNICORE_DMA_MAPPING_H__
#define __UNICORE_DMA_MAPPING_H__

#ifdef __KERNEL__

#include <linux/mm_types.h>
#include <linux/scatterlist.h>
#include <linux/swiotlb.h>

extern const struct dma_map_ops swiotlb_dma_map_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return &swiotlb_dma_map_ops;
}

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	if (dev && dev->dma_mask)
		return addr + size - 1 <= *dev->dma_mask;

	return 1;
}

static inline dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	return paddr;
}

static inline phys_addr_t dma_to_phys(struct device *dev, dma_addr_t daddr)
{
	return daddr;
}

static inline void dma_mark_clean(void *addr, size_t size) {}

#endif /* __KERNEL__ */
#endif
