/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_DMA_MAPPING_H
#define _ASM_TILE_DMA_MAPPING_H

#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/cache.h>
#include <linux/io.h>

#ifdef __tilegx__
#define ARCH_HAS_DMA_GET_REQUIRED_MASK
#endif

extern const struct dma_map_ops *tile_dma_map_ops;
extern const struct dma_map_ops *gx_pci_dma_map_ops;
extern const struct dma_map_ops *gx_legacy_pci_dma_map_ops;
extern const struct dma_map_ops *gx_hybrid_pci_dma_map_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return tile_dma_map_ops;
}

static inline dma_addr_t get_dma_offset(struct device *dev)
{
	return dev->archdata.dma_offset;
}

static inline void set_dma_offset(struct device *dev, dma_addr_t off)
{
	dev->archdata.dma_offset = off;
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

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	if (!dev->dma_mask)
		return 0;

	return addr + size - 1 <= *dev->dma_mask;
}

#define HAVE_ARCH_DMA_SET_MASK 1
int dma_set_mask(struct device *dev, u64 mask);

#endif /* _ASM_TILE_DMA_MAPPING_H */
