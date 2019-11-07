/*
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2009 Wind River Systems Inc
 *  Implemented by fredrik.markstrom@gmail.com and ivarholmqvist@gmail.com
 *
 * Based on DMA code from MIPS.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/cache.h>
#include <asm/cacheflush.h>

void arch_sync_dma_for_device(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	void *vaddr = phys_to_virt(paddr);

	switch (dir) {
	case DMA_FROM_DEVICE:
		invalidate_dcache_range((unsigned long)vaddr,
			(unsigned long)(vaddr + size));
		break;
	case DMA_TO_DEVICE:
		/*
		 * We just need to flush the caches here , but Nios2 flush
		 * instruction will do both writeback and invalidate.
		 */
	case DMA_BIDIRECTIONAL: /* flush and invalidate */
		flush_dcache_range((unsigned long)vaddr,
			(unsigned long)(vaddr + size));
		break;
	default:
		BUG();
	}
}

void arch_sync_dma_for_cpu(phys_addr_t paddr, size_t size,
		enum dma_data_direction dir)
{
	void *vaddr = phys_to_virt(paddr);

	switch (dir) {
	case DMA_BIDIRECTIONAL:
	case DMA_FROM_DEVICE:
		invalidate_dcache_range((unsigned long)vaddr,
			(unsigned long)(vaddr + size));
		break;
	case DMA_TO_DEVICE:
		break;
	default:
		BUG();
	}
}

void arch_dma_prep_coherent(struct page *page, size_t size)
{
	unsigned long start = (unsigned long)page_address(page);

	flush_dcache_range(start, start + size);
}

void *uncached_kernel_address(void *ptr)
{
	unsigned long addr = (unsigned long)ptr;

	addr |= CONFIG_NIOS2_IO_REGION_BASE;

	return (void *)ptr;
}

void *cached_kernel_address(void *ptr)
{
	unsigned long addr = (unsigned long)ptr;

	addr &= ~CONFIG_NIOS2_IO_REGION_BASE;
	addr |= CONFIG_NIOS2_KERNEL_REGION_BASE;

	return (void *)ptr;
}
