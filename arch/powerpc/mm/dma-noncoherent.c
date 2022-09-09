// SPDX-License-Identifier: GPL-2.0-only
/*
 *  PowerPC version derived from arch/arm/mm/consistent.c
 *    Copyright (C) 2001 Dan Malek (dmalek@jlc.net)
 *
 *  Copyright (C) 2000 Russell King
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/highmem.h>
#include <linux/dma-direct.h>
#include <linux/dma-map-ops.h>

#include <asm/tlbflush.h>
#include <asm/dma.h>

/*
 * make an area consistent.
 */
static void __dma_sync(void *vaddr, size_t size, int direction)
{
	unsigned long start = (unsigned long)vaddr;
	unsigned long end   = start + size;

	switch (direction) {
	case DMA_NONE:
		BUG();
	case DMA_FROM_DEVICE:
		/*
		 * invalidate only when cache-line aligned otherwise there is
		 * the potential for discarding uncommitted data from the cache
		 */
		if ((start | end) & (L1_CACHE_BYTES - 1))
			flush_dcache_range(start, end);
		else
			invalidate_dcache_range(start, end);
		break;
	case DMA_TO_DEVICE:		/* writeback only */
		clean_dcache_range(start, end);
		break;
	case DMA_BIDIRECTIONAL:	/* writeback and invalidate */
		flush_dcache_range(start, end);
		break;
	}
}

#ifdef CONFIG_HIGHMEM
/*
 * __dma_sync_page() implementation for systems using highmem.
 * In this case, each page of a buffer must be kmapped/kunmapped
 * in order to have a virtual address for __dma_sync(). This must
 * not sleep so kmap_atomic()/kunmap_atomic() are used.
 *
 * Note: yes, it is possible and correct to have a buffer extend
 * beyond the first page.
 */
static inline void __dma_sync_page_highmem(struct page *page,
		unsigned long offset, size_t size, int direction)
{
	size_t seg_size = min((size_t)(PAGE_SIZE - offset), size);
	size_t cur_size = seg_size;
	unsigned long flags, start, seg_offset = offset;
	int nr_segs = 1 + ((size - seg_size) + PAGE_SIZE - 1)/PAGE_SIZE;
	int seg_nr = 0;

	local_irq_save(flags);

	do {
		start = (unsigned long)kmap_atomic(page + seg_nr) + seg_offset;

		/* Sync this buffer segment */
		__dma_sync((void *)start, seg_size, direction);
		kunmap_atomic((void *)start);
		seg_nr++;

		/* Calculate next buffer segment size */
		seg_size = min((size_t)PAGE_SIZE, size - cur_size);

		/* Add the segment size to our running total */
		cur_size += seg_size;
		seg_offset = 0;
	} while (seg_nr < nr_segs);

	local_irq_restore(flags);
}
#endif /* CONFIG_HIGHMEM */

/*
 * __dma_sync_page makes memory consistent. identical to __dma_sync, but
 * takes a struct page instead of a virtual address
 */
static void __dma_sync_page(phys_addr_t paddr, size_t size, int dir)
{
	struct page *page = pfn_to_page(paddr >> PAGE_SHIFT);
	unsigned offset = paddr & ~PAGE_MASK;

#ifdef CONFIG_HIGHMEM
	__dma_sync_page_highmem(page, offset, size, dir);
#else
	unsigned long start = (unsigned long)page_address(page) + offset;
	__dma_sync((void *)start, size, dir);
#endif
}

void arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	__dma_sync_page(paddr, size, dir);
}

void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	__dma_sync_page(paddr, size, dir);
}

void arch_dma_prep_coherent(struct page *page, size_t size)
{
	unsigned long kaddr = (unsigned long)page_address(page);

	flush_dcache_range(kaddr, kaddr + size);
}
