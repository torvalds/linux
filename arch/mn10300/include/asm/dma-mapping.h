/* DMA mapping routines for the MN10300 arch
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_DMA_MAPPING_H
#define _ASM_DMA_MAPPING_H

#include <linux/mm.h>
#include <linux/scatterlist.h>

#include <asm/cache.h>
#include <asm/io.h>

/*
 * See Documentation/DMA-API.txt for the description of how the
 * following DMA API should work.
 */

extern void *dma_alloc_coherent(struct device *dev, size_t size,
				dma_addr_t *dma_handle, int flag);

extern void dma_free_coherent(struct device *dev, size_t size,
			      void *vaddr, dma_addr_t dma_handle);

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent((d), (s), (h), (f))
#define dma_free_noncoherent(d, s, v, h)  dma_free_coherent((d), (s), (v), (h))

static inline
dma_addr_t dma_map_single(struct device *dev, void *ptr, size_t size,
			  enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	mn10300_dcache_flush_inv();
	return virt_to_bus(ptr);
}

static inline
void dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
		      enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
}

static inline
int dma_map_sg(struct device *dev, struct scatterlist *sglist, int nents,
	       enum dma_data_direction direction)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(!valid_dma_direction(direction));
	WARN_ON(nents == 0 || sglist[0].length == 0);

	for_each_sg(sglist, sg, nents, i) {
		BUG_ON(!sg_page(sg));

		sg->dma_address = sg_phys(sg);
	}

	mn10300_dcache_flush_inv();
	return nents;
}

static inline
void dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nhwentries,
		  enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
}

static inline
dma_addr_t dma_map_page(struct device *dev, struct page *page,
			unsigned long offset, size_t size,
			enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	return page_to_bus(page) + offset;
}

static inline
void dma_unmap_page(struct device *dev, dma_addr_t dma_address, size_t size,
		    enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
}

static inline
void dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle,
			     size_t size, enum dma_data_direction direction)
{
}

static inline
void dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle,
				size_t size, enum dma_data_direction direction)
{
	mn10300_dcache_flush_inv();
}

static inline
void dma_sync_single_range_for_cpu(struct device *dev, dma_addr_t dma_handle,
				   unsigned long offset, size_t size,
				   enum dma_data_direction direction)
{
}

static inline void
dma_sync_single_range_for_device(struct device *dev, dma_addr_t dma_handle,
				 unsigned long offset, size_t size,
				 enum dma_data_direction direction)
{
	mn10300_dcache_flush_inv();
}


static inline
void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
			 int nelems, enum dma_data_direction direction)
{
}

static inline
void dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
			    int nelems, enum dma_data_direction direction)
{
	mn10300_dcache_flush_inv();
}

static inline
int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return 0;
}

static inline
int dma_supported(struct device *dev, u64 mask)
{
	/*
	 * we fall back to GFP_DMA when the mask isn't all 1s, so we can't
	 * guarantee allocations that must be within a tighter range than
	 * GFP_DMA
	 */
	if (mask < 0x00ffffff)
		return 0;
	return 1;
}

static inline
int dma_set_mask(struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	*dev->dma_mask = mask;
	return 0;
}

static inline
void dma_cache_sync(void *vaddr, size_t size,
		    enum dma_data_direction direction)
{
	mn10300_dcache_flush_inv();
}

/* Not supported for now */
static inline int dma_mmap_coherent(struct device *dev,
				    struct vm_area_struct *vma, void *cpu_addr,
				    dma_addr_t dma_addr, size_t size)
{
	return -EINVAL;
}

static inline int dma_get_sgtable(struct device *dev, struct sg_table *sgt,
				  void *cpu_addr, dma_addr_t dma_addr,
				  size_t size)
{
	return -EINVAL;
}

#endif
