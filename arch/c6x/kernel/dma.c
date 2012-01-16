/*
 *  Copyright (C) 2011 Texas Instruments Incorporated
 *  Author: Mark Salter <msalter@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/scatterlist.h>

#include <asm/cacheflush.h>

static void c6x_dma_sync(dma_addr_t handle, size_t size,
			 enum dma_data_direction dir)
{
	unsigned long paddr = handle;

	BUG_ON(!valid_dma_direction(dir));

	switch (dir) {
	case DMA_FROM_DEVICE:
		L2_cache_block_invalidate(paddr, paddr + size);
		break;
	case DMA_TO_DEVICE:
		L2_cache_block_writeback(paddr, paddr + size);
		break;
	case DMA_BIDIRECTIONAL:
		L2_cache_block_writeback_invalidate(paddr, paddr + size);
		break;
	default:
		break;
	}
}

dma_addr_t dma_map_single(struct device *dev, void *ptr, size_t size,
			  enum dma_data_direction dir)
{
	dma_addr_t addr = virt_to_phys(ptr);

	c6x_dma_sync(addr, size, dir);

	debug_dma_map_page(dev, virt_to_page(ptr),
			   (unsigned long)ptr & ~PAGE_MASK, size,
			   dir, addr, true);
	return addr;
}
EXPORT_SYMBOL(dma_map_single);


void dma_unmap_single(struct device *dev, dma_addr_t handle,
		      size_t size, enum dma_data_direction dir)
{
	c6x_dma_sync(handle, size, dir);

	debug_dma_unmap_page(dev, handle, size, dir, true);
}
EXPORT_SYMBOL(dma_unmap_single);


int dma_map_sg(struct device *dev, struct scatterlist *sglist,
	       int nents, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sglist, sg, nents, i)
		sg->dma_address = dma_map_single(dev, sg_virt(sg), sg->length,
						 dir);

	debug_dma_map_sg(dev, sglist, nents, nents, dir);

	return nents;
}
EXPORT_SYMBOL(dma_map_sg);


void dma_unmap_sg(struct device *dev, struct scatterlist *sglist,
		  int nents, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sglist, sg, nents, i)
		dma_unmap_single(dev, sg_dma_address(sg), sg->length, dir);

	debug_dma_unmap_sg(dev, sglist,	nents, dir);
}
EXPORT_SYMBOL(dma_unmap_sg);

void dma_sync_single_for_cpu(struct device *dev, dma_addr_t handle,
			     size_t size, enum dma_data_direction dir)
{
	c6x_dma_sync(handle, size, dir);

	debug_dma_sync_single_for_cpu(dev, handle, size, dir);
}
EXPORT_SYMBOL(dma_sync_single_for_cpu);


void dma_sync_single_for_device(struct device *dev, dma_addr_t handle,
				size_t size, enum dma_data_direction dir)
{
	c6x_dma_sync(handle, size, dir);

	debug_dma_sync_single_for_device(dev, handle, size, dir);
}
EXPORT_SYMBOL(dma_sync_single_for_device);


void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sglist,
			 int nents, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sglist, sg, nents, i)
		dma_sync_single_for_cpu(dev, sg_dma_address(sg),
					sg->length, dir);

	debug_dma_sync_sg_for_cpu(dev, sglist, nents, dir);
}
EXPORT_SYMBOL(dma_sync_sg_for_cpu);


void dma_sync_sg_for_device(struct device *dev, struct scatterlist *sglist,
			    int nents, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sglist, sg, nents, i)
		dma_sync_single_for_device(dev, sg_dma_address(sg),
					   sg->length, dir);

	debug_dma_sync_sg_for_device(dev, sglist, nents, dir);
}
EXPORT_SYMBOL(dma_sync_sg_for_device);


/* Number of entries preallocated for DMA-API debugging */
#define PREALLOC_DMA_DEBUG_ENTRIES (1 << 16)

static int __init dma_init(void)
{
	dma_debug_init(PREALLOC_DMA_DEBUG_ENTRIES);

	return 0;
}
fs_initcall(dma_init);
