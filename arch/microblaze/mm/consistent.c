/*
 * Microblaze support for cache consistent memory.
 * Copyright (C) 2010 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2010 PetaLogix
 * Copyright (C) 2005 John Williams <jwilliams@itee.uq.edu.au>
 *
 * Based on PowerPC version derived from arch/arm/mm/consistent.c
 * Copyright (C) 2001 Dan Malek (dmalek@jlc.net)
 * Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/stddef.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/pci.h>
#include <linux/interrupt.h>

#include <asm/pgalloc.h>
#include <linux/io.h>
#include <linux/hardirq.h>
#include <asm/mmu_context.h>
#include <asm/mmu.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>
#include <asm/cpuinfo.h>

#ifndef CONFIG_MMU

/* I have to use dcache values because I can't relate on ram size */
#define UNCACHED_SHADOW_MASK (cpuinfo.dcache_high - cpuinfo.dcache_base + 1)

/*
 * Consistent memory allocators. Used for DMA devices that want to
 * share uncached memory with the processor core.
 * My crufty no-MMU approach is simple. In the HW platform we can optionally
 * mirror the DDR up above the processor cacheable region.  So, memory accessed
 * in this mirror region will not be cached.  It's alloced from the same
 * pool as normal memory, but the handle we return is shifted up into the
 * uncached region.  This will no doubt cause big problems if memory allocated
 * here is not also freed properly. -- JW
 */
void *consistent_alloc(int gfp, size_t size, dma_addr_t *dma_handle)
{
	struct page *page, *end, *free;
	unsigned long order;
	void *ret, *virt;

	if (in_interrupt())
		BUG();

	size = PAGE_ALIGN(size);
	order = get_order(size);

	page = alloc_pages(gfp, order);
	if (!page)
		goto no_page;

	/* We could do with a page_to_phys and page_to_bus here. */
	virt = page_address(page);
	ret = ioremap(virt_to_phys(virt), size);
	if (!ret)
		goto no_remap;

	/*
	 * Here's the magic!  Note if the uncached shadow is not implemented,
	 * it's up to the calling code to also test that condition and make
	 * other arranegments, such as manually flushing the cache and so on.
	 */
#ifdef CONFIG_XILINX_UNCACHED_SHADOW
	ret = (void *)((unsigned) ret | UNCACHED_SHADOW_MASK);
#endif
	/* dma_handle is same as physical (shadowed) address */
	*dma_handle = (dma_addr_t)ret;

	/*
	 * free wasted pages.  We skip the first page since we know
	 * that it will have count = 1 and won't require freeing.
	 * We also mark the pages in use as reserved so that
	 * remap_page_range works.
	 */
	page = virt_to_page(virt);
	free = page + (size >> PAGE_SHIFT);
	end  = page + (1 << order);

	for (; page < end; page++) {
		init_page_count(page);
		if (page >= free)
			__free_page(page);
		else
			SetPageReserved(page);
	}

	return ret;
no_remap:
	__free_pages(page, order);
no_page:
	return NULL;
}

#else

void *consistent_alloc(int gfp, size_t size, dma_addr_t *dma_handle)
{
	int order, err, i;
	unsigned long page, va, flags;
	phys_addr_t pa;
	struct vm_struct *area;
	void	 *ret;

	if (in_interrupt())
		BUG();

	/* Only allocate page size areas. */
	size = PAGE_ALIGN(size);
	order = get_order(size);

	page = __get_free_pages(gfp, order);
	if (!page) {
		BUG();
		return NULL;
	}

	/*
	 * we need to ensure that there are no cachelines in use,
	 * or worse dirty in this area.
	 */
	flush_dcache_range(virt_to_phys(page), virt_to_phys(page) + size);

	/* Allocate some common virtual space to map the new pages. */
	area = get_vm_area(size, VM_ALLOC);
	if (area == NULL) {
		free_pages(page, order);
		return NULL;
	}
	va = (unsigned long) area->addr;
	ret = (void *)va;

	/* This gives us the real physical address of the first page. */
	*dma_handle = pa = virt_to_bus((void *)page);

	/* MS: This is the whole magic - use cache inhibit pages */
	flags = _PAGE_KERNEL | _PAGE_NO_CACHE;

	/*
	 * Set refcount=1 on all pages in an order>0
	 * allocation so that vfree() will actually
	 * free all pages that were allocated.
	 */
	if (order > 0) {
		struct page *rpage = virt_to_page(page);
		for (i = 1; i < (1 << order); i++)
			init_page_count(rpage+i);
	}

	err = 0;
	for (i = 0; i < size && err == 0; i += PAGE_SIZE)
		err = map_page(va+i, pa+i, flags);

	if (err) {
		vfree((void *)va);
		return NULL;
	}

	return ret;
}
#endif /* CONFIG_MMU */
EXPORT_SYMBOL(consistent_alloc);

/*
 * free page(s) as defined by the above mapping.
 */
void consistent_free(void *vaddr)
{
	if (in_interrupt())
		BUG();

	/* Clear SHADOW_MASK bit in address, and free as per usual */
#ifdef CONFIG_XILINX_UNCACHED_SHADOW
	vaddr = (void *)((unsigned)vaddr & ~UNCACHED_SHADOW_MASK);
#endif
	vfree(vaddr);
}
EXPORT_SYMBOL(consistent_free);

/*
 * make an area consistent.
 */
void consistent_sync(void *vaddr, size_t size, int direction)
{
	unsigned long start;
	unsigned long end;

	start = (unsigned long)vaddr;

	/* Convert start address back down to unshadowed memory region */
#ifdef CONFIG_XILINX_UNCACHED_SHADOW
	start &= ~UNCACHED_SHADOW_MASK;
#endif
	end = start + size;

	switch (direction) {
	case PCI_DMA_NONE:
		BUG();
	case PCI_DMA_FROMDEVICE:	/* invalidate only */
		flush_dcache_range(start, end);
		break;
	case PCI_DMA_TODEVICE:		/* writeback only */
		flush_dcache_range(start, end);
		break;
	case PCI_DMA_BIDIRECTIONAL:	/* writeback and invalidate */
		flush_dcache_range(start, end);
		break;
	}
}
EXPORT_SYMBOL(consistent_sync);

/*
 * consistent_sync_page makes memory consistent. identical
 * to consistent_sync, but takes a struct page instead of a
 * virtual address
 */
void consistent_sync_page(struct page *page, unsigned long offset,
	size_t size, int direction)
{
	unsigned long start = (unsigned long)page_address(page) + offset;
	consistent_sync((void *)start, size, direction);
}
EXPORT_SYMBOL(consistent_sync_page);
