/*
 * DMA coherent memory allocation.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Copyright (C) 2002 - 2005 Tensilica Inc.
 * Copyright (C) 2015 Cadence Design Systems Inc.
 *
 * Based on version for i386.
 *
 * Chris Zankel <chris@zankel.net>
 * Joe Taylor <joe@tensilica.com, joetylr@yahoo.com>
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/gfp.h>
#include <linux/module.h>
#include <asm/io.h>
#include <asm/cacheflush.h>

void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
		    enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_BIDIRECTIONAL:
		__flush_invalidate_dcache_range((unsigned long)vaddr, size);
		break;

	case DMA_FROM_DEVICE:
		__invalidate_dcache_range((unsigned long)vaddr, size);
		break;

	case DMA_TO_DEVICE:
		__flush_dcache_range((unsigned long)vaddr, size);
		break;

	case DMA_NONE:
		BUG();
		break;
	}
}
EXPORT_SYMBOL(dma_cache_sync);

static void xtensa_sync_single_for_cpu(struct device *dev,
				       dma_addr_t dma_handle, size_t size,
				       enum dma_data_direction dir)
{
	void *vaddr;

	switch (dir) {
	case DMA_BIDIRECTIONAL:
	case DMA_FROM_DEVICE:
		vaddr = bus_to_virt(dma_handle);
		__invalidate_dcache_range((unsigned long)vaddr, size);
		break;

	case DMA_NONE:
		BUG();
		break;

	default:
		break;
	}
}

static void xtensa_sync_single_for_device(struct device *dev,
					  dma_addr_t dma_handle, size_t size,
					  enum dma_data_direction dir)
{
	void *vaddr;

	switch (dir) {
	case DMA_BIDIRECTIONAL:
	case DMA_TO_DEVICE:
		vaddr = bus_to_virt(dma_handle);
		__flush_dcache_range((unsigned long)vaddr, size);
		break;

	case DMA_NONE:
		BUG();
		break;

	default:
		break;
	}
}

static void xtensa_sync_sg_for_cpu(struct device *dev,
				   struct scatterlist *sg, int nents,
				   enum dma_data_direction dir)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i) {
		xtensa_sync_single_for_cpu(dev, sg_dma_address(s),
					   sg_dma_len(s), dir);
	}
}

static void xtensa_sync_sg_for_device(struct device *dev,
				      struct scatterlist *sg, int nents,
				      enum dma_data_direction dir)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i) {
		xtensa_sync_single_for_device(dev, sg_dma_address(s),
					      sg_dma_len(s), dir);
	}
}

/*
 * Note: We assume that the full memory space is always mapped to 'kseg'
 *	 Otherwise we have to use page attributes (not implemented).
 */

static void *xtensa_dma_alloc(struct device *dev, size_t size,
			      dma_addr_t *handle, gfp_t flag,
			      struct dma_attrs *attrs)
{
	unsigned long ret;
	unsigned long uncached = 0;

	/* ignore region speicifiers */

	flag &= ~(__GFP_DMA | __GFP_HIGHMEM);

	if (dev == NULL || (dev->coherent_dma_mask < 0xffffffff))
		flag |= GFP_DMA;
	ret = (unsigned long)__get_free_pages(flag, get_order(size));

	if (ret == 0)
		return NULL;

	/* We currently don't support coherent memory outside KSEG */

	BUG_ON(ret < XCHAL_KSEG_CACHED_VADDR ||
	       ret > XCHAL_KSEG_CACHED_VADDR + XCHAL_KSEG_SIZE - 1);

	uncached = ret + XCHAL_KSEG_BYPASS_VADDR - XCHAL_KSEG_CACHED_VADDR;
	*handle = virt_to_bus((void *)ret);
	__invalidate_dcache_range(ret, size);

	return (void *)uncached;
}

static void xtensa_dma_free(struct device *hwdev, size_t size, void *vaddr,
			    dma_addr_t dma_handle, struct dma_attrs *attrs)
{
	unsigned long addr = (unsigned long)vaddr +
		XCHAL_KSEG_CACHED_VADDR - XCHAL_KSEG_BYPASS_VADDR;

	BUG_ON(addr < XCHAL_KSEG_CACHED_VADDR ||
	       addr > XCHAL_KSEG_CACHED_VADDR + XCHAL_KSEG_SIZE - 1);

	free_pages(addr, get_order(size));
}

static dma_addr_t xtensa_map_page(struct device *dev, struct page *page,
				  unsigned long offset, size_t size,
				  enum dma_data_direction dir,
				  struct dma_attrs *attrs)
{
	dma_addr_t dma_handle = page_to_phys(page) + offset;

	BUG_ON(PageHighMem(page));
	xtensa_sync_single_for_device(dev, dma_handle, size, dir);
	return dma_handle;
}

static void xtensa_unmap_page(struct device *dev, dma_addr_t dma_handle,
			      size_t size, enum dma_data_direction dir,
			      struct dma_attrs *attrs)
{
	xtensa_sync_single_for_cpu(dev, dma_handle, size, dir);
}

static int xtensa_map_sg(struct device *dev, struct scatterlist *sg,
			 int nents, enum dma_data_direction dir,
			 struct dma_attrs *attrs)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i) {
		s->dma_address = xtensa_map_page(dev, sg_page(s), s->offset,
						 s->length, dir, attrs);
	}
	return nents;
}

static void xtensa_unmap_sg(struct device *dev,
			    struct scatterlist *sg, int nents,
			    enum dma_data_direction dir,
			    struct dma_attrs *attrs)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i) {
		xtensa_unmap_page(dev, sg_dma_address(s),
				  sg_dma_len(s), dir, attrs);
	}
}

int xtensa_dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return 0;
}

struct dma_map_ops xtensa_dma_map_ops = {
	.alloc = xtensa_dma_alloc,
	.free = xtensa_dma_free,
	.map_page = xtensa_map_page,
	.unmap_page = xtensa_unmap_page,
	.map_sg = xtensa_map_sg,
	.unmap_sg = xtensa_unmap_sg,
	.sync_single_for_cpu = xtensa_sync_single_for_cpu,
	.sync_single_for_device = xtensa_sync_single_for_device,
	.sync_sg_for_cpu = xtensa_sync_sg_for_cpu,
	.sync_sg_for_device = xtensa_sync_sg_for_device,
	.mapping_error = xtensa_dma_mapping_error,
};
EXPORT_SYMBOL(xtensa_dma_map_ops);

#define PREALLOC_DMA_DEBUG_ENTRIES (1 << 16)

static int __init xtensa_dma_init(void)
{
	dma_debug_init(PREALLOC_DMA_DEBUG_ENTRIES);
	return 0;
}
fs_initcall(xtensa_dma_init);
