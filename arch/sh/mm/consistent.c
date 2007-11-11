/*
 * arch/sh/mm/consistent.c
 *
 * Copyright (C) 2004 - 2007  Paul Mundt
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
	void *ret, *vp;
	int order;

	size = PAGE_ALIGN(size);
	order = get_order(size);

	page = alloc_pages(gfp, order);
	if (!page)
		return NULL;
	split_page(page, order);

	ret = page_address(page);
	*handle = virt_to_phys(ret);

	vp = ioremap_nocache(*handle, size);
	if (!vp) {
		free_pages((unsigned long)ret, order);
		return NULL;
	}

	memset(vp, 0, size);

	/*
	 * We must flush the cache before we pass it on to the device
	 */
	dma_cache_sync(NULL, ret, size, DMA_BIDIRECTIONAL);

	page = virt_to_page(ret);
	free = page + (size >> PAGE_SHIFT);
	end  = page + (1 << order);

	while (++page < end) {
		/* Free any unused pages */
		if (page >= free) {
			__free_page(page);
		}
	}

	return vp;
}
EXPORT_SYMBOL(consistent_alloc);

void consistent_free(void *vaddr, size_t size, dma_addr_t dma_handle)
{
	struct page *page;
	unsigned long addr;

	addr = (unsigned long)phys_to_virt((unsigned long)dma_handle);
	page = virt_to_page(addr);

	free_pages(addr, get_order(size));

	iounmap(vaddr);
}
EXPORT_SYMBOL(consistent_free);

void consistent_sync(void *vaddr, size_t size, int direction)
{
#ifdef CONFIG_CPU_SH5
	void *p1addr = vaddr;
#else
	void *p1addr = (void*) P1SEGADDR((unsigned long)vaddr);
#endif

	switch (direction) {
	case DMA_FROM_DEVICE:		/* invalidate only */
		__flush_invalidate_region(p1addr, size);
		break;
	case DMA_TO_DEVICE:		/* writeback only */
		__flush_wback_region(p1addr, size);
		break;
	case DMA_BIDIRECTIONAL:		/* writeback and invalidate */
		__flush_purge_region(p1addr, size);
		break;
	default:
		BUG();
	}
}
EXPORT_SYMBOL(consistent_sync);
