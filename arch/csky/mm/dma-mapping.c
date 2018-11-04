// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/cache.h>
#include <linux/dma-mapping.h>
#include <linux/dma-contiguous.h>
#include <linux/dma-noncoherent.h>
#include <linux/genalloc.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/types.h>
#include <linux/version.h>
#include <asm/cache.h>

static int __init atomic_pool_init(void)
{
	return dma_atomic_pool_init(GFP_KERNEL, pgprot_noncached(PAGE_KERNEL));
}
postcore_initcall(atomic_pool_init);

void arch_dma_prep_coherent(struct page *page, size_t size)
{
	if (PageHighMem(page)) {
		unsigned int count = PAGE_ALIGN(size) >> PAGE_SHIFT;

		do {
			void *ptr = kmap_atomic(page);
			size_t _size = (size < PAGE_SIZE) ? size : PAGE_SIZE;

			memset(ptr, 0, _size);
			dma_wbinv_range((unsigned long)ptr,
					(unsigned long)ptr + _size);

			kunmap_atomic(ptr);

			page++;
			size -= PAGE_SIZE;
			count--;
		} while (count);
	} else {
		void *ptr = page_address(page);

		memset(ptr, 0, size);
		dma_wbinv_range((unsigned long)ptr, (unsigned long)ptr + size);
	}
}

static inline void cache_op(phys_addr_t paddr, size_t size,
			    void (*fn)(unsigned long start, unsigned long end))
{
	struct page *page = pfn_to_page(paddr >> PAGE_SHIFT);
	unsigned int offset = paddr & ~PAGE_MASK;
	size_t left = size;
	unsigned long start;

	do {
		size_t len = left;

		if (PageHighMem(page)) {
			void *addr;

			if (offset + len > PAGE_SIZE) {
				if (offset >= PAGE_SIZE) {
					page += offset >> PAGE_SHIFT;
					offset &= ~PAGE_MASK;
				}
				len = PAGE_SIZE - offset;
			}

			addr = kmap_atomic(page);
			start = (unsigned long)(addr + offset);
			fn(start, start + len);
			kunmap_atomic(addr);
		} else {
			start = (unsigned long)phys_to_virt(paddr);
			fn(start, start + size);
		}
		offset = 0;
		page++;
		left -= len;
	} while (left);
}

void arch_sync_dma_for_device(struct device *dev, phys_addr_t paddr,
			      size_t size, enum dma_data_direction dir)
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

void arch_sync_dma_for_cpu(struct device *dev, phys_addr_t paddr,
			   size_t size, enum dma_data_direction dir)
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
