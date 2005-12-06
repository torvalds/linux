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
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/highmem.h>
#include <asm/io.h>

void *dma_alloc_coherent(struct device *hwdev, size_t size, dma_addr_t *dma_handle, gfp_t gfp)
{
	void *ret;

	ret = consistent_alloc(gfp, size, dma_handle);
	if (ret)
		memset(ret, 0, size);

	return ret;
}

void dma_free_coherent(struct device *hwdev, size_t size, void *vaddr, dma_addr_t dma_handle)
{
	consistent_free(vaddr);
}

/*
 * Map a single buffer of the indicated size for DMA in streaming mode.
 * The 32-bit bus address to use is returned.
 *
 * Once the device is given the dma address, the device owns this memory
 * until either pci_unmap_single or pci_dma_sync_single is performed.
 */
dma_addr_t dma_map_single(struct device *dev, void *ptr, size_t size,
			  enum dma_data_direction direction)
{
	if (direction == DMA_NONE)
                BUG();

	frv_cache_wback_inv((unsigned long) ptr, (unsigned long) ptr + size);

	return virt_to_bus(ptr);
}

/*
 * Map a set of buffers described by scatterlist in streaming
 * mode for DMA.  This is the scather-gather version of the
 * above pci_map_single interface.  Here the scatter gather list
 * elements are each tagged with the appropriate dma address
 * and length.  They are obtained via sg_dma_{address,length}(SG).
 *
 * NOTE: An implementation may be able to use a smaller number of
 *       DMA address/length pairs than there are SG table elements.
 *       (for example via virtual mapping capabilities)
 *       The routine returns the number of addr/length pairs actually
 *       used, at most nents.
 *
 * Device ownership issues as mentioned above for pci_map_single are
 * the same here.
 */
int dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	       enum dma_data_direction direction)
{
	unsigned long dampr2;
	void *vaddr;
	int i;

	if (direction == DMA_NONE)
                BUG();

	dampr2 = __get_DAMPR(2);

	for (i = 0; i < nents; i++) {
		vaddr = kmap_atomic(sg[i].page, __KM_CACHE);

		frv_dcache_writeback((unsigned long) vaddr,
				     (unsigned long) vaddr + PAGE_SIZE);

	}

	kunmap_atomic(vaddr, __KM_CACHE);
	if (dampr2) {
		__set_DAMPR(2, dampr2);
		__set_IAMPR(2, dampr2);
	}

	return nents;
}

dma_addr_t dma_map_page(struct device *dev, struct page *page, unsigned long offset,
			size_t size, enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	flush_dcache_page(page);
	return (dma_addr_t) page_to_phys(page) + offset;
}
