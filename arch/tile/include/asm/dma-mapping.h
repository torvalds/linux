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

/*
 * Note that on x86 and powerpc, there is a "struct dma_mapping_ops"
 * that is used for all the DMA operations.  For now, we don't have an
 * equivalent on tile, because we only have a single way of doing DMA.
 * (Tilera bug 7994 to use dma_mapping_ops.)
 */

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)

extern dma_addr_t dma_map_single(struct device *dev, void *ptr, size_t size,
			  enum dma_data_direction);
extern void dma_unmap_single(struct device *dev, dma_addr_t dma_addr,
			     size_t size, enum dma_data_direction);
extern int dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	       enum dma_data_direction);
extern void dma_unmap_sg(struct device *dev, struct scatterlist *sg,
			 int nhwentries, enum dma_data_direction);
extern dma_addr_t dma_map_page(struct device *dev, struct page *page,
			       unsigned long offset, size_t size,
			       enum dma_data_direction);
extern void dma_unmap_page(struct device *dev, dma_addr_t dma_address,
			   size_t size, enum dma_data_direction);
extern void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
				int nelems, enum dma_data_direction);
extern void dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
				   int nelems, enum dma_data_direction);


void *dma_alloc_coherent(struct device *dev, size_t size,
			   dma_addr_t *dma_handle, gfp_t flag);

void dma_free_coherent(struct device *dev, size_t size,
			 void *vaddr, dma_addr_t dma_handle);

extern void dma_sync_single_for_cpu(struct device *, dma_addr_t, size_t,
				    enum dma_data_direction);
extern void dma_sync_single_for_device(struct device *, dma_addr_t,
				       size_t, enum dma_data_direction);
extern void dma_sync_single_range_for_cpu(struct device *, dma_addr_t,
					  unsigned long offset, size_t,
					  enum dma_data_direction);
extern void dma_sync_single_range_for_device(struct device *, dma_addr_t,
					     unsigned long offset, size_t,
					     enum dma_data_direction);
extern void dma_cache_sync(void *vaddr, size_t, enum dma_data_direction);

static inline int
dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return 0;
}

static inline int
dma_supported(struct device *dev, u64 mask)
{
	return 1;
}

static inline int
dma_set_mask(struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	*dev->dma_mask = mask;

	return 0;
}

#endif /* _ASM_TILE_DMA_MAPPING_H */
