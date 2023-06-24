// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/cache.h>
#include <linux/dma-map-ops.h>
#include <linux/genalloc.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/types.h>
#include <asm/cache.h>

static inline void cache_op(phys_addr_t paddr, size_t size,
			    void (*fn)(unsigned long start, unsigned long end))
{
	struct page *page    = phys_to_page(paddr);
	void *start          = __va(page_to_phys(page));
	unsigned long offset = offset_in_page(paddr);
	size_t left          = size;

	do {
		size_t len = left;

		if (offset + len > PAGE_SIZE)
			len = PAGE_SIZE - offset;

		if (PageHighMem(page)) {
			start = kmap_atomic(page);

			fn((unsigned long)start + offset,
					(unsigned long)start + offset + len);

			kunmap_atomic(start);
		} else {
			fn((unsigned long)start + offset,
					(unsigned long)start + offset + len);
		}
		offset = 0;

		page++;
		start += PAGE_SIZE;
		left -= len;
	} while (left);
}

static void dma_wbinv_set_zero_range(unsigned long start, unsigned long end)
{
	memset((void *)start, 0, end - start);
	dma_wbinv_range(start, end);
}

void arch_dma_prep_coherent(struct page *page, size_t size)
{
	cache_op(page_to_phys(page), size, dma_wbinv_set_zero_range);
}

void arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
		cache_op(paddr, size, dma_wb_range);
		break;
	case DMA_FROM_DEVICE:
	case DMA_BIDIRECTIONAL:
		cache_op(paddr, size, dma_wbinv_range);
		break;
	default:
		BUG();
	}
}

void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
		return;
	case DMA_FROM_DEVICE:
	case DMA_BIDIRECTIONAL:
		cache_op(paddr, size, dma_inv_range);
		break;
	default:
		BUG();
	}
}
