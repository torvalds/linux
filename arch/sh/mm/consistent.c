/*
 * arch/sh/mm/consistent.c
 *
 * Copyright (C) 2004  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <asm/cacheflush.h>
#include <asm/addrspace.h>
#include <asm/io.h>

void *consistent_alloc(gfp_t gfp, size_t size, dma_addr_t *handle)
{
	struct page *page, *end, *free;
	void *ret;
	int order;

	size = PAGE_ALIGN(size);
	order = get_order(size);

	page = alloc_pages(gfp, order);
	if (!page)
		return NULL;
	split_page(page, order);

	ret = page_address(page);
	memset(ret, 0, size);
	*handle = virt_to_phys(ret);

	/*
	 * We must flush the cache before we pass it on to the device
	 */
	dma_cache_wback_inv(ret, size);

	page = virt_to_page(ret);
	free = page + (size >> PAGE_SHIFT);
	end  = page + (1 << order);

	while (++page < end) {
		/* Free any unused pages */
		if (page >= free) {
			__free_page(page);
		}
	}

	return P2SEGADDR(ret);
}

void consistent_free(void *vaddr, size_t size)
{
	unsigned long addr = P1SEGADDR((unsigned long)vaddr);
	struct page *page=virt_to_page(addr);
	int num_pages=(size+PAGE_SIZE-1) >> PAGE_SHIFT;
	int i;

	for(i=0;i<num_pages;i++) {
		__free_page((page+i));
	}
}

void consistent_sync(void *vaddr, size_t size, int direction)
{
	void * p1addr = (void*) P1SEGADDR((unsigned long)vaddr);

	switch (direction) {
	case DMA_FROM_DEVICE:		/* invalidate only */
		dma_cache_inv(p1addr, size);
		break;
	case DMA_TO_DEVICE:		/* writeback only */
		dma_cache_wback(p1addr, size);
		break;
	case DMA_BIDIRECTIONAL:		/* writeback and invalidate */
		dma_cache_wback_inv(p1addr, size);
		break;
	default:
		BUG();
	}
}

EXPORT_SYMBOL(consistent_alloc);
EXPORT_SYMBOL(consistent_free);
EXPORT_SYMBOL(consistent_sync);

