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

void arch_sync_dma_for_device(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir)
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

void arch_sync_dma_for_cpu(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir)
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

void *arch_dma_alloc(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs)
{
	void *ret;

	/* optimized page clearing */
	gfp |= __GFP_ZERO;

	if (dev == NULL || (dev->coherent_dma_mask < 0xffffffff))
		gfp |= GFP_DMA;

	ret = (void *) __get_free_pages(gfp, get_order(size));
	if (ret != NULL) {
		*dma_handle = virt_to_phys(ret);
		flush_dcache_range((unsigned long) ret,
			(unsigned long) ret + size);
		ret = UNCAC_ADDR(ret);
	}

	return ret;
}

void arch_dma_free(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_handle, unsigned long attrs)
{
	unsigned long addr = (unsigned long) CAC_ADDR((unsigned long) vaddr);

	free_pages(addr, get_order(size));
}
