// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * DMA coherent memory allocation.
 *
 * Copyright (C) 2002 - 2005 Tensilica Inc.
 * Copyright (C) 2015 Cadence Design Systems Inc.
 *
 * Based on version for i386.
 *
 * Chris Zankel <chris@zankel.net>
 * Joe Taylor <joe@tensilica.com, joetylr@yahoo.com>
 */

#include <linux/dma-contiguous.h>
#include <linux/dma-noncoherent.h>
#include <linux/dma-direct.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <asm/cacheflush.h>
#include <asm/io.h>
#include <asm/platform.h>

static void do_cache_op(phys_addr_t paddr, size_t size,
			void (*fn)(unsigned long, unsigned long))
{
	unsigned long off = paddr & (PAGE_SIZE - 1);
	unsigned long pfn = PFN_DOWN(paddr);
	struct page *page = pfn_to_page(pfn);

	if (!PageHighMem(page))
		fn((unsigned long)phys_to_virt(paddr), size);
	else
		while (size > 0) {
			size_t sz = min_t(size_t, size, PAGE_SIZE - off);
			void *vaddr = kmap_atomic(page);

			fn((unsigned long)vaddr + off, sz);
			kunmap_atomic(vaddr);
			off = 0;
			++page;
			size -= sz;
		}
}

void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_BIDIRECTIONAL:
	case DMA_FROM_DEVICE:
		do_cache_op(paddr, size, __invalidate_dcache_range);
		break;

	case DMA_NONE:
		BUG();
		break;

	default:
		break;
	}
}

void arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_BIDIRECTIONAL:
	case DMA_TO_DEVICE:
		if (XCHAL_DCACHE_IS_WRITEBACK)
			do_cache_op(paddr, size, __flush_dcache_range);
		break;

	case DMA_NONE:
		BUG();
		break;

	default:
		break;
	}
}

void arch_dma_prep_coherent(struct page *page, size_t size)
{
	__invalidate_dcache_range((unsigned long)page_address(page), size);
}

/*
 * Memory caching is platform-dependent in noMMU xtensa configurations.
 * This function should be implemented in platform code in order to enable
 * coherent DMA memory operations when CONFIG_MMU is not enabled.
 */
#ifdef CONFIG_MMU
void *arch_dma_set_uncached(void *p, size_t size)
{
	return p + XCHAL_KSEG_BYPASS_VADDR - XCHAL_KSEG_CACHED_VADDR;
}
#endif /* CONFIG_MMU */
