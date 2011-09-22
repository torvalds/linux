/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 * DMA mapping callbacks...
 * As alloc_coherent is the only DMA callback being used currently, that's
 * the only thing implemented properly.  The rest need looking into...
 */

#include <linux/dma-mapping.h>
#include <linux/dma-debug.h>

#include <asm/cpuinfo.h>
#include <asm/spr_defs.h>
#include <asm/tlbflush.h>

static int page_set_nocache(pte_t *pte, unsigned long addr,
			    unsigned long next, struct mm_walk *walk)
{
	unsigned long cl;

	pte_val(*pte) |= _PAGE_CI;

	/*
	 * Flush the page out of the TLB so that the new page flags get
	 * picked up next time there's an access
	 */
	flush_tlb_page(NULL, addr);

	/* Flush page out of dcache */
	for (cl = __pa(addr); cl < __pa(next); cl += cpuinfo.dcache_block_size)
		mtspr(SPR_DCBFR, cl);

	return 0;
}

static int page_clear_nocache(pte_t *pte, unsigned long addr,
			      unsigned long next, struct mm_walk *walk)
{
	pte_val(*pte) &= ~_PAGE_CI;

	/*
	 * Flush the page out of the TLB so that the new page flags get
	 * picked up next time there's an access
	 */
	flush_tlb_page(NULL, addr);

	return 0;
}

/*
 * Alloc "coherent" memory, which for OpenRISC means simply uncached.
 *
 * This function effectively just calls __get_free_pages, sets the
 * cache-inhibit bit on those pages, and makes sure that the pages are
 * flushed out of the cache before they are used.
 *
 */
void *or1k_dma_alloc_coherent(struct device *dev, size_t size,
			      dma_addr_t *dma_handle, gfp_t gfp)
{
	unsigned long va;
	void *page;
	struct mm_walk walk = {
		.pte_entry = page_set_nocache,
		.mm = &init_mm
	};

	page = alloc_pages_exact(size, gfp);
	if (!page)
		return NULL;

	/* This gives us the real physical address of the first page. */
	*dma_handle = __pa(page);

	va = (unsigned long)page;

	/*
	 * We need to iterate through the pages, clearing the dcache for
	 * them and setting the cache-inhibit bit.
	 */
	if (walk_page_range(va, va + size, &walk)) {
		free_pages_exact(page, size);
		return NULL;
	}

	return (void *)va;
}

void or1k_dma_free_coherent(struct device *dev, size_t size, void *vaddr,
			    dma_addr_t dma_handle)
{
	unsigned long va = (unsigned long)vaddr;
	struct mm_walk walk = {
		.pte_entry = page_clear_nocache,
		.mm = &init_mm
	};

	/* walk_page_range shouldn't be able to fail here */
	WARN_ON(walk_page_range(va, va + size, &walk));

	free_pages_exact(vaddr, size);
}

dma_addr_t or1k_map_page(struct device *dev, struct page *page,
			 unsigned long offset, size_t size,
			 enum dma_data_direction dir,
			 struct dma_attrs *attrs)
{
	unsigned long cl;
	dma_addr_t addr = page_to_phys(page) + offset;

	switch (dir) {
	case DMA_TO_DEVICE:
		/* Flush the dcache for the requested range */
		for (cl = addr; cl < addr + size;
		     cl += cpuinfo.dcache_block_size)
			mtspr(SPR_DCBFR, cl);
		break;
	case DMA_FROM_DEVICE:
		/* Invalidate the dcache for the requested range */
		for (cl = addr; cl < addr + size;
		     cl += cpuinfo.dcache_block_size)
			mtspr(SPR_DCBIR, cl);
		break;
	default:
		/*
		 * NOTE: If dir == DMA_BIDIRECTIONAL then there's no need to
		 * flush nor invalidate the cache here as the area will need
		 * to be manually synced anyway.
		 */
		break;
	}

	return addr;
}

void or1k_unmap_page(struct device *dev, dma_addr_t dma_handle,
		     size_t size, enum dma_data_direction dir,
		     struct dma_attrs *attrs)
{
	/* Nothing special to do here... */
}

int or1k_map_sg(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir,
		struct dma_attrs *attrs)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i) {
		s->dma_address = or1k_map_page(dev, sg_page(s), s->offset,
					       s->length, dir, NULL);
	}

	return nents;
}

void or1k_unmap_sg(struct device *dev, struct scatterlist *sg,
		   int nents, enum dma_data_direction dir,
		   struct dma_attrs *attrs)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i) {
		or1k_unmap_page(dev, sg_dma_address(s), sg_dma_len(s), dir, NULL);
	}
}

void or1k_sync_single_for_cpu(struct device *dev,
			      dma_addr_t dma_handle, size_t size,
			      enum dma_data_direction dir)
{
	unsigned long cl;
	dma_addr_t addr = dma_handle;

	/* Invalidate the dcache for the requested range */
	for (cl = addr; cl < addr + size; cl += cpuinfo.dcache_block_size)
		mtspr(SPR_DCBIR, cl);
}

void or1k_sync_single_for_device(struct device *dev,
			         dma_addr_t dma_handle, size_t size,
			         enum dma_data_direction dir)
{
	unsigned long cl;
	dma_addr_t addr = dma_handle;

	/* Flush the dcache for the requested range */
	for (cl = addr; cl < addr + size; cl += cpuinfo.dcache_block_size)
		mtspr(SPR_DCBFR, cl);
}

/* Number of entries preallocated for DMA-API debugging */
#define PREALLOC_DMA_DEBUG_ENTRIES (1 << 16)

static int __init dma_init(void)
{
	dma_debug_init(PREALLOC_DMA_DEBUG_ENTRIES);

	return 0;
}
fs_initcall(dma_init);
