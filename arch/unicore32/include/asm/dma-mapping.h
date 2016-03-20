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

#include <asm/memory.h>
#include <asm/cacheflush.h>

extern struct dma_map_ops swiotlb_dma_map_ops;

static inline struct dma_map_ops *get_dma_ops(struct device *dev)
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

static inline void dma_cache_sync(struct device *dev, void *vaddr,
		size_t size, enum dma_data_direction direction)
{
	unsigned long start = (unsigned long)vaddr;
	unsigned long end   = start + size;

	switch (direction) {
	case DMA_NONE:
		BUG();
	case DMA_FROM_DEVICE:
	case DMA_BIDIRECTIONAL:	/* writeback and invalidate */
		__cpuc_dma_flush_range(start, end);
		break;
	case DMA_TO_DEVICE:		/* writeback only */
		__cpuc_dma_clean_range(start, end);
		break;
	}
}

#endif /* __KERNEL__ */
#endif
