/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __ASM_OPENRISC_DMA_MAPPING_H
#define __ASM_OPENRISC_DMA_MAPPING_H

/*
 * See Documentation/DMA-API-HOWTO.txt and
 * Documentation/DMA-API.txt for documentation.
 *
 * This file is written with the intention of eventually moving over
 * to largely using asm-generic/dma-mapping-common.h in its place.
 */

#include <linux/dma-debug.h>
#include <asm-generic/dma-coherent.h>
#include <linux/kmemcheck.h>

#define DMA_ERROR_CODE		(~(dma_addr_t)0x0)


#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)

void *or1k_dma_alloc_coherent(struct device *dev, size_t size,
			      dma_addr_t *dma_handle, gfp_t flag);
void or1k_dma_free_coherent(struct device *dev, size_t size, void *vaddr,
			    dma_addr_t dma_handle);
dma_addr_t or1k_map_page(struct device *dev, struct page *page,
			 unsigned long offset, size_t size,
			 enum dma_data_direction dir,
			 struct dma_attrs *attrs);
void or1k_unmap_page(struct device *dev, dma_addr_t dma_handle,
		     size_t size, enum dma_data_direction dir,
		     struct dma_attrs *attrs);
int or1k_map_sg(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir,
		struct dma_attrs *attrs);
void or1k_unmap_sg(struct device *dev, struct scatterlist *sg,
		   int nents, enum dma_data_direction dir,
		   struct dma_attrs *attrs);
void or1k_sync_single_for_cpu(struct device *dev,
			      dma_addr_t dma_handle, size_t size,
			      enum dma_data_direction dir);
void or1k_sync_single_for_device(struct device *dev,
			         dma_addr_t dma_handle, size_t size,
			         enum dma_data_direction dir);

static inline void *dma_alloc_coherent(struct device *dev, size_t size,
					dma_addr_t *dma_handle, gfp_t flag)
{
	void *memory;

	memory = or1k_dma_alloc_coherent(dev, size, dma_handle, flag);

	debug_dma_alloc_coherent(dev, size, *dma_handle, memory);
	return memory;
}

static inline void dma_free_coherent(struct device *dev, size_t size,
				     void *cpu_addr, dma_addr_t dma_handle)
{
	debug_dma_free_coherent(dev, size, cpu_addr, dma_handle);
	or1k_dma_free_coherent(dev, size, cpu_addr, dma_handle);
}

static inline dma_addr_t dma_map_single(struct device *dev, void *ptr,
					size_t size,
					enum dma_data_direction dir)
{
	dma_addr_t addr;

	kmemcheck_mark_initialized(ptr, size);
	BUG_ON(!valid_dma_direction(dir));
	addr = or1k_map_page(dev, virt_to_page(ptr),
			     (unsigned long)ptr & ~PAGE_MASK, size,
			     dir, NULL);
	debug_dma_map_page(dev, virt_to_page(ptr),
			   (unsigned long)ptr & ~PAGE_MASK, size,
			   dir, addr, true);
	return addr;
}

static inline void dma_unmap_single(struct device *dev, dma_addr_t addr,
					  size_t size,
					  enum dma_data_direction dir)
{
	BUG_ON(!valid_dma_direction(dir));
	or1k_unmap_page(dev, addr, size, dir, NULL);
	debug_dma_unmap_page(dev, addr, size, dir, true);
}

static inline int dma_map_sg(struct device *dev, struct scatterlist *sg,
				   int nents, enum dma_data_direction dir)
{
	int i, ents;
	struct scatterlist *s;

	for_each_sg(sg, s, nents, i)
		kmemcheck_mark_initialized(sg_virt(s), s->length);
	BUG_ON(!valid_dma_direction(dir));
	ents = or1k_map_sg(dev, sg, nents, dir, NULL);
	debug_dma_map_sg(dev, sg, nents, ents, dir);

	return ents;
}

static inline void dma_unmap_sg(struct device *dev, struct scatterlist *sg,
				      int nents, enum dma_data_direction dir)
{
	BUG_ON(!valid_dma_direction(dir));
	debug_dma_unmap_sg(dev, sg, nents, dir);
	or1k_unmap_sg(dev, sg, nents, dir, NULL);
}

static inline dma_addr_t dma_map_page(struct device *dev, struct page *page,
				      size_t offset, size_t size,
				      enum dma_data_direction dir)
{
	dma_addr_t addr;

	kmemcheck_mark_initialized(page_address(page) + offset, size);
	BUG_ON(!valid_dma_direction(dir));
	addr = or1k_map_page(dev, page, offset, size, dir, NULL);
	debug_dma_map_page(dev, page, offset, size, dir, addr, false);

	return addr;
}

static inline void dma_unmap_page(struct device *dev, dma_addr_t addr,
				  size_t size, enum dma_data_direction dir)
{
	BUG_ON(!valid_dma_direction(dir));
	or1k_unmap_page(dev, addr, size, dir, NULL);
	debug_dma_unmap_page(dev, addr, size, dir, true);
}

static inline void dma_sync_single_for_cpu(struct device *dev, dma_addr_t addr,
					   size_t size,
					   enum dma_data_direction dir)
{
	BUG_ON(!valid_dma_direction(dir));
	or1k_sync_single_for_cpu(dev, addr, size, dir);
	debug_dma_sync_single_for_cpu(dev, addr, size, dir);
}

static inline void dma_sync_single_for_device(struct device *dev,
					      dma_addr_t addr, size_t size,
					      enum dma_data_direction dir)
{
	BUG_ON(!valid_dma_direction(dir));
	or1k_sync_single_for_device(dev, addr, size, dir);
	debug_dma_sync_single_for_device(dev, addr, size, dir);
}

static inline int dma_supported(struct device *dev, u64 dma_mask)
{
	/* Support 32 bit DMA mask exclusively */
	return dma_mask == DMA_BIT_MASK(32);
}

static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return 0;
}

static inline int dma_set_mask(struct device *dev, u64 dma_mask)
{
	if (!dev->dma_mask || !dma_supported(dev, dma_mask))
		return -EIO;

	*dev->dma_mask = dma_mask;

	return 0;
}
#endif	/* __ASM_OPENRISC_DMA_MAPPING_H */
