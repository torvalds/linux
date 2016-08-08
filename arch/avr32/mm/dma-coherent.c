/*
 *  Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/dma-mapping.h>
#include <linux/gfp.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/device.h>
#include <linux/scatterlist.h>

#include <asm/processor.h>
#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/addrspace.h>

void dma_cache_sync(struct device *dev, void *vaddr, size_t size, int direction)
{
	/*
	 * No need to sync an uncached area
	 */
	if (PXSEG(vaddr) == P2SEG)
		return;

	switch (direction) {
	case DMA_FROM_DEVICE:		/* invalidate only */
		invalidate_dcache_region(vaddr, size);
		break;
	case DMA_TO_DEVICE:		/* writeback only */
		clean_dcache_region(vaddr, size);
		break;
	case DMA_BIDIRECTIONAL:		/* writeback and invalidate */
		flush_dcache_region(vaddr, size);
		break;
	default:
		BUG();
	}
}
EXPORT_SYMBOL(dma_cache_sync);

static struct page *__dma_alloc(struct device *dev, size_t size,
				dma_addr_t *handle, gfp_t gfp)
{
	struct page *page, *free, *end;
	int order;

	/* Following is a work-around (a.k.a. hack) to prevent pages
	 * with __GFP_COMP being passed to split_page() which cannot
	 * handle them.  The real problem is that this flag probably
	 * should be 0 on AVR32 as it is not supported on this
	 * platform--see CONFIG_HUGETLB_PAGE. */
	gfp &= ~(__GFP_COMP);

	size = PAGE_ALIGN(size);
	order = get_order(size);

	page = alloc_pages(gfp, order);
	if (!page)
		return NULL;
	split_page(page, order);

	/*
	 * When accessing physical memory with valid cache data, we
	 * get a cache hit even if the virtual memory region is marked
	 * as uncached.
	 *
	 * Since the memory is newly allocated, there is no point in
	 * doing a writeback. If the previous owner cares, he should
	 * have flushed the cache before releasing the memory.
	 */
	invalidate_dcache_region(phys_to_virt(page_to_phys(page)), size);

	*handle = page_to_bus(page);
	free = page + (size >> PAGE_SHIFT);
	end = page + (1 << order);

	/*
	 * Free any unused pages
	 */
	while (free < end) {
		__free_page(free);
		free++;
	}

	return page;
}

static void __dma_free(struct device *dev, size_t size,
		       struct page *page, dma_addr_t handle)
{
	struct page *end = page + (PAGE_ALIGN(size) >> PAGE_SHIFT);

	while (page < end)
		__free_page(page++);
}

static void *avr32_dma_alloc(struct device *dev, size_t size,
		dma_addr_t *handle, gfp_t gfp, unsigned long attrs)
{
	struct page *page;
	dma_addr_t phys;

	page = __dma_alloc(dev, size, handle, gfp);
	if (!page)
		return NULL;
	phys = page_to_phys(page);

	if (attrs & DMA_ATTR_WRITE_COMBINE) {
		/* Now, map the page into P3 with write-combining turned on */
		*handle = phys;
		return __ioremap(phys, size, _PAGE_BUFFER);
	} else {
		return phys_to_uncached(phys);
	}
}

static void avr32_dma_free(struct device *dev, size_t size,
		void *cpu_addr, dma_addr_t handle, unsigned long attrs)
{
	struct page *page;

	if (attrs & DMA_ATTR_WRITE_COMBINE) {
		iounmap(cpu_addr);

		page = phys_to_page(handle);
	} else {
		void *addr = phys_to_cached(uncached_to_phys(cpu_addr));

		pr_debug("avr32_dma_free addr %p (phys %08lx) size %u\n",
			 cpu_addr, (unsigned long)handle, (unsigned)size);

		BUG_ON(!virt_addr_valid(addr));
		page = virt_to_page(addr);
	}

	__dma_free(dev, size, page, handle);
}

static dma_addr_t avr32_dma_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size,
		enum dma_data_direction direction, unsigned long attrs)
{
	void *cpu_addr = page_address(page) + offset;

	dma_cache_sync(dev, cpu_addr, size, direction);
	return virt_to_bus(cpu_addr);
}

static int avr32_dma_map_sg(struct device *dev, struct scatterlist *sglist,
		int nents, enum dma_data_direction direction,
		unsigned long attrs)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sglist, sg, nents, i) {
		char *virt;

		sg->dma_address = page_to_bus(sg_page(sg)) + sg->offset;
		virt = sg_virt(sg);
		dma_cache_sync(dev, virt, sg->length, direction);
	}

	return nents;
}

static void avr32_dma_sync_single_for_device(struct device *dev,
		dma_addr_t dma_handle, size_t size,
		enum dma_data_direction direction)
{
	dma_cache_sync(dev, bus_to_virt(dma_handle), size, direction);
}

static void avr32_dma_sync_sg_for_device(struct device *dev,
		struct scatterlist *sglist, int nents,
		enum dma_data_direction direction)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sglist, sg, nents, i)
		dma_cache_sync(dev, sg_virt(sg), sg->length, direction);
}

struct dma_map_ops avr32_dma_ops = {
	.alloc			= avr32_dma_alloc,
	.free			= avr32_dma_free,
	.map_page		= avr32_dma_map_page,
	.map_sg			= avr32_dma_map_sg,
	.sync_single_for_device	= avr32_dma_sync_single_for_device,
	.sync_sg_for_device	= avr32_dma_sync_sg_for_device,
};
EXPORT_SYMBOL(avr32_dma_ops);
