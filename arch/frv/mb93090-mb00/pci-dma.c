/* pci-dma.c: Dynamic DMA mapping support for the FRV CPUs that have MMUs
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/highmem.h>
#include <linux/scatterlist.h>
#include <asm/io.h>

void *dma_alloc_coherent(struct device *hwdev, size_t size, dma_addr_t *dma_handle, gfp_t gfp)
{
	void *ret;

	ret = consistent_alloc(gfp, size, dma_handle);
	if (ret)
		memset(ret, 0, size);

	return ret;
}

EXPORT_SYMBOL(dma_alloc_coherent);

void dma_free_coherent(struct device *hwdev, size_t size, void *vaddr, dma_addr_t dma_handle)
{
	consistent_free(vaddr);
}

EXPORT_SYMBOL(dma_free_coherent);

dma_addr_t dma_map_single(struct device *dev, void *ptr, size_t size,
			  enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	frv_cache_wback_inv((unsigned long) ptr, (unsigned long) ptr + size);

	return virt_to_bus(ptr);
}

EXPORT_SYMBOL(dma_map_single);

int dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	       enum dma_data_direction direction)
{
	unsigned long dampr2;
	void *vaddr;
	int i;

	BUG_ON(direction == DMA_NONE);

	dampr2 = __get_DAMPR(2);

	for (i = 0; i < nents; i++) {
		vaddr = kmap_atomic_primary(sg_page(&sg[i]), __KM_CACHE);

		frv_dcache_writeback((unsigned long) vaddr,
				     (unsigned long) vaddr + PAGE_SIZE);

	}

	kunmap_atomic_primary(vaddr, __KM_CACHE);
	if (dampr2) {
		__set_DAMPR(2, dampr2);
		__set_IAMPR(2, dampr2);
	}

	return nents;
}

EXPORT_SYMBOL(dma_map_sg);

dma_addr_t dma_map_page(struct device *dev, struct page *page, unsigned long offset,
			size_t size, enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	flush_dcache_page(page);
	return (dma_addr_t) page_to_phys(page) + offset;
}

EXPORT_SYMBOL(dma_map_page);
