/*
 *  PowerPC version derived from arch/arm/mm/consistent.c
 *    Copyright (C) 2001 Dan Malek (dmalek@jlc.net)
 *
 *  Copyright (C) 2000 Russell King
 *
 * Consistent memory allocators.  Used for DMA devices that want to
 * share uncached memory with the processor core.  The function return
 * is the virtual address and 'dma_handle' is the physical address.
 * Mostly stolen from the ARM port, with some changes for PowerPC.
 *						-- Dan
 *
 * Reorganized to get rid of the arch-specific consistent_* functions
 * and provide non-coherent implementations for the DMA API. -Matt
 *
 * Added in_interrupt() safe dma_alloc_coherent()/dma_free_coherent()
 * implementation. This is pulled straight from ARM and barely
 * modified. -Matt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/highmem.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>

#include <asm/tlbflush.h>

/*
 * Allocate DMA-coherent memory space and return both the kernel remapped
 * virtual and bus address for that space.
 */
void *
__dma_alloc_coherent(size_t size, dma_addr_t *handle, gfp_t gfp)
{
	struct page *page;
	unsigned long order;
	int i;
	unsigned int nr_pages = PAGE_ALIGN(size)>>PAGE_SHIFT;
	unsigned int array_size = nr_pages * sizeof(struct page *);
	struct page **pages;
	struct page *end;
	u64 mask = 0x00ffffff, limit; /* ISA default */
	struct vm_struct *area;

	BUG_ON(!mem_init_done);
	size = PAGE_ALIGN(size);
	limit = (mask + 1) & ~mask;
	if (limit && size >= limit) {
		printk(KERN_WARNING "coherent allocation too big (requested "
				"%#x mask %#Lx)\n", size, mask);
		return NULL;
	}

	order = get_order(size);

	if (mask != 0xffffffff)
		gfp |= GFP_DMA;

	page = alloc_pages(gfp, order);
	if (!page)
		goto no_page;

	end = page + (1 << order);

	/*
	 * Invalidate any data that might be lurking in the
	 * kernel direct-mapped region for device DMA.
	 */
	{
		unsigned long kaddr = (unsigned long)page_address(page);
		memset(page_address(page), 0, size);
		flush_dcache_range(kaddr, kaddr + size);
	}

	split_page(page, order);

	/*
	 * Set the "dma handle"
	 */
	*handle = page_to_phys(page);

	area = get_vm_area_caller(size, VM_IOREMAP,
			__builtin_return_address(1));
	if (!area)
		goto out_free_pages;

	if (array_size > PAGE_SIZE) {
		pages = vmalloc(array_size);
		area->flags |= VM_VPAGES;
	} else {
		pages = kmalloc(array_size, GFP_KERNEL);
	}
	if (!pages)
		goto out_free_area;

	area->pages = pages;
	area->nr_pages = nr_pages;

	for (i = 0; i < nr_pages; i++)
		pages[i] = page + i;

	if (map_vm_area(area, pgprot_noncached(PAGE_KERNEL), &pages))
		goto out_unmap;

	/*
	 * Free the otherwise unused pages.
	 */
	page += nr_pages;
	while (page < end) {
		__free_page(page);
		page++;
	}

	return area->addr;
out_unmap:
	vunmap(area->addr);
	if (array_size > PAGE_SIZE)
		vfree(pages);
	else
		kfree(pages);
	goto out_free_pages;
out_free_area:
	free_vm_area(area);
out_free_pages:
	if (page)
		__free_pages(page, order);
no_page:
	return NULL;
}
EXPORT_SYMBOL(__dma_alloc_coherent);

/*
 * free a page as defined by the above mapping.
 */
void __dma_free_coherent(size_t size, void *vaddr)
{
	vfree(vaddr);

}
EXPORT_SYMBOL(__dma_free_coherent);

/*
 * make an area consistent.
 */
void __dma_sync(void *vaddr, size_t size, int direction)
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
		if ((start & (L1_CACHE_BYTES - 1)) || (size & (L1_CACHE_BYTES - 1)))
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
EXPORT_SYMBOL(__dma_sync);

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
		start = (unsigned long)kmap_atomic(page + seg_nr,
				KM_PPC_SYNC_PAGE) + seg_offset;

		/* Sync this buffer segment */
		__dma_sync((void *)start, seg_size, direction);
		kunmap_atomic((void *)start, KM_PPC_SYNC_PAGE);
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
void __dma_sync_page(struct page *page, unsigned long offset,
	size_t size, int direction)
{
#ifdef CONFIG_HIGHMEM
	__dma_sync_page_highmem(page, offset, size, direction);
#else
	unsigned long start = (unsigned long)page_address(page) + offset;
	__dma_sync((void *)start, size, direction);
#endif
}
EXPORT_SYMBOL(__dma_sync_page);
