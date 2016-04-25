/* MN10300 Dynamic DMA mapping support
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * Derived from: arch/i386/kernel/pci-dma.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/gfp.h>
#include <linux/export.h>
#include <asm/io.h>

static unsigned long pci_sram_allocated = 0xbc000000;

static void *mn10300_dma_alloc(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, struct dma_attrs *attrs)
{
	unsigned long addr;
	void *ret;

	pr_debug("dma_alloc_coherent(%s,%zu,%x)\n",
		 dev ? dev_name(dev) : "?", size, gfp);

	if (0xbe000000 - pci_sram_allocated >= size) {
		size = (size + 255) & ~255;
		addr = pci_sram_allocated;
		pci_sram_allocated += size;
		ret = (void *) addr;
		goto done;
	}

	/* ignore region specifiers */
	gfp &= ~(__GFP_DMA | __GFP_HIGHMEM);

	if (dev == NULL || dev->coherent_dma_mask < 0xffffffff)
		gfp |= GFP_DMA;

	addr = __get_free_pages(gfp, get_order(size));
	if (!addr)
		return NULL;

	/* map the coherent memory through the uncached memory window */
	ret = (void *) (addr | 0x20000000);

	/* fill the memory with obvious rubbish */
	memset((void *) addr, 0xfb, size);

	/* write back and evict all cache lines covering this region */
	mn10300_dcache_flush_inv_range2(virt_to_phys((void *) addr), PAGE_SIZE);

done:
	*dma_handle = virt_to_bus((void *) addr);
	printk("dma_alloc_coherent() = %p [%x]\n", ret, *dma_handle);
	return ret;
}

static void mn10300_dma_free(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_handle, struct dma_attrs *attrs)
{
	unsigned long addr = (unsigned long) vaddr & ~0x20000000;

	if (addr >= 0x9c000000)
		return;

	free_pages(addr, get_order(size));
}

static int mn10300_dma_map_sg(struct device *dev, struct scatterlist *sglist,
		int nents, enum dma_data_direction direction,
		struct dma_attrs *attrs)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sglist, sg, nents, i) {
		BUG_ON(!sg_page(sg));

		sg->dma_address = sg_phys(sg);
	}

	mn10300_dcache_flush_inv();
	return nents;
}

static dma_addr_t mn10300_dma_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size,
		enum dma_data_direction direction, struct dma_attrs *attrs)
{
	return page_to_bus(page) + offset;
}

static void mn10300_dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle,
				size_t size, enum dma_data_direction direction)
{
	mn10300_dcache_flush_inv();
}

static void mn10300_dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
			    int nelems, enum dma_data_direction direction)
{
	mn10300_dcache_flush_inv();
}

static int mn10300_dma_supported(struct device *dev, u64 mask)
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

struct dma_map_ops mn10300_dma_ops = {
	.alloc			= mn10300_dma_alloc,
	.free			= mn10300_dma_free,
	.map_page		= mn10300_dma_map_page,
	.map_sg			= mn10300_dma_map_sg,
	.sync_single_for_device	= mn10300_dma_sync_single_for_device,
	.sync_sg_for_device	= mn10300_dma_sync_sg_for_device,
};
