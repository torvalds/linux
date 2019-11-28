// SPDX-License-Identifier: GPL-2.0-or-later
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
 * DMA mapping callbacks...
 * As alloc_coherent is the only DMA callback being used currently, that's
 * the only thing implemented properly.  The rest need looking into...
 */

#include <linux/dma-noncoherent.h>
#include <linux/pagewalk.h>

#include <asm/cpuinfo.h>
#include <asm/spr_defs.h>
#include <asm/tlbflush.h>

static int
page_set_nocache(pte_t *pte, unsigned long addr,
		 unsigned long next, struct mm_walk *walk)
{
	unsigned long cl;
	struct cpuinfo_or1k *cpuinfo = &cpuinfo_or1k[smp_processor_id()];

	pte_val(*pte) |= _PAGE_CI;

	/*
	 * Flush the page out of the TLB so that the new page flags get
	 * picked up next time there's an access
	 */
	flush_tlb_page(NULL, addr);

	/* Flush page out of dcache */
	for (cl = __pa(addr); cl < __pa(next); cl += cpuinfo->dcache_block_size)
		mtspr(SPR_DCBFR, cl);

	return 0;
}

static const struct mm_walk_ops set_nocache_walk_ops = {
	.pte_entry		= page_set_nocache,
};

static int
page_clear_nocache(pte_t *pte, unsigned long addr,
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

static const struct mm_walk_ops clear_nocache_walk_ops = {
	.pte_entry		= page_clear_nocache,
};

/*
 * Alloc "coherent" memory, which for OpenRISC means simply uncached.
 *
 * This function effectively just calls __get_free_pages, sets the
 * cache-inhibit bit on those pages, and makes sure that the pages are
 * flushed out of the cache before they are used.
 *
 * If the NON_CONSISTENT attribute is set, then this function just
 * returns "normal", cachable memory.
 *
 * There are additional flags WEAK_ORDERING and WRITE_COMBINE to take
 * into consideration here, too.  All current known implementations of
 * the OR1K support only strongly ordered memory accesses, so that flag
 * is being ignored for now; uncached but write-combined memory is a
 * missing feature of the OR1K.
 */
void *
arch_dma_alloc(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs)
{
	unsigned long va;
	void *page;

	page = alloc_pages_exact(size, gfp | __GFP_ZERO);
	if (!page)
		return NULL;

	/* This gives us the real physical address of the first page. */
	*dma_handle = __pa(page);

	va = (unsigned long)page;

	/*
	 * We need to iterate through the pages, clearing the dcache for
	 * them and setting the cache-inhibit bit.
	 */
	if (walk_page_range(&init_mm, va, va + size, &set_nocache_walk_ops,
			NULL)) {
		free_pages_exact(page, size);
		return NULL;
	}

	return (void *)va;
}

void
arch_dma_free(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_handle, unsigned long attrs)
{
	unsigned long va = (unsigned long)vaddr;

	/* walk_page_range shouldn't be able to fail here */
	WARN_ON(walk_page_range(&init_mm, va, va + size,
			&clear_nocache_walk_ops, NULL));

	free_pages_exact(vaddr, size);
}

void arch_sync_dma_for_device(phys_addr_t addr, size_t size,
		enum dma_data_direction dir)
{
	unsigned long cl;
	struct cpuinfo_or1k *cpuinfo = &cpuinfo_or1k[smp_processor_id()];

	switch (dir) {
	case DMA_TO_DEVICE:
		/* Flush the dcache for the requested range */
		for (cl = addr; cl < addr + size;
		     cl += cpuinfo->dcache_block_size)
			mtspr(SPR_DCBFR, cl);
		break;
	case DMA_FROM_DEVICE:
		/* Invalidate the dcache for the requested range */
		for (cl = addr; cl < addr + size;
		     cl += cpuinfo->dcache_block_size)
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
}
