/*
 * arch/xtensa/pci-dma.c
 *
 * DMA coherent memory allocation.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Copyright (C) 2002 - 2005 Tensilica Inc.
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
#include <asm/io.h>
#include <asm/cacheflush.h>

/*
 * Note: We assume that the full memory space is always mapped to 'kseg'
 *	 Otherwise we have to use page attributes (not implemented).
 */

void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *handle, int gfp)
{
	void *ret;

	/* ignore region speicifiers */
	gfp &= ~(__GFP_DMA | __GFP_HIGHMEM);

	if (dev == NULL || (*dev->dma_mask < 0xffffffff))
		gfp |= GFP_DMA;
	ret = (void *)__get_free_pages(gfp, get_order(size));

	if (ret != NULL) {
		memset(ret, 0, size);
		*handle = virt_to_bus(ret);
	}
	return (void*) BYPASS_ADDR((unsigned long)ret);
}

void dma_free_coherent(struct device *hwdev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	free_pages(CACHED_ADDR((unsigned long)vaddr), get_order(size));
}


void consistent_sync(void *vaddr, size_t size, int direction)
{
	switch (direction) {
	case PCI_DMA_NONE:
		BUG();
	case PCI_DMA_FROMDEVICE:        /* invalidate only */
		__invalidate_dcache_range((unsigned long)vaddr,
				          (unsigned long)size);
		break;

	case PCI_DMA_TODEVICE:          /* writeback only */
	case PCI_DMA_BIDIRECTIONAL:     /* writeback and invalidate */
		__flush_invalidate_dcache_range((unsigned long)vaddr,
				    		(unsigned long)size);
		break;
	}
}
