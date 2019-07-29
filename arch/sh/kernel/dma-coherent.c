// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2004 - 2007  Paul Mundt
 */
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/dma-noncoherent.h>
#include <linux/module.h>
#include <asm/cacheflush.h>
#include <asm/addrspace.h>

void *arch_dma_alloc(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs)
{
	void *ret, *ret_nocache;
	int order = get_order(size);

	gfp |= __GFP_ZERO;

	ret = (void *)__get_free_pages(gfp, order);
	if (!ret)
		return NULL;

	/*
	 * Pages from the page allocator may have data present in
	 * cache. So flush the cache before using uncached memory.
	 */
	arch_sync_dma_for_device(dev, virt_to_phys(ret), size,
			DMA_BIDIRECTIONAL);

	ret_nocache = (void __force *)ioremap_nocache(virt_to_phys(ret), size);
	if (!ret_nocache) {
		free_pages((unsigned long)ret, order);
		return NULL;
	}

	split_page(pfn_to_page(virt_to_phys(ret) >> PAGE_SHIFT), order);

	*dma_handle = virt_to_phys(ret);
	if (!WARN_ON(!dev))
		*dma_handle -= PFN_PHYS(dev->dma_pfn_offset);

	return ret_nocache;
}

void arch_dma_free(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_handle, unsigned long attrs)
{
	int order = get_order(size);
	unsigned long pfn = (dma_handle >> PAGE_SHIFT);
	int k;

	if (!WARN_ON(!dev))
		pfn += dev->dma_pfn_offset;

	for (k = 0; k < (1 << order); k++)
		__free_pages(pfn_to_page(pfn + k), 0);

	iounmap(vaddr);
}

void arch_sync_dma_for_device(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir)
{
	void *addr = sh_cacheop_vaddr(phys_to_virt(paddr));

	switch (dir) {
	case DMA_FROM_DEVICE:		/* invalidate only */
		__flush_invalidate_region(addr, size);
		break;
	case DMA_TO_DEVICE:		/* writeback only */
		__flush_wback_region(addr, size);
		break;
	case DMA_BIDIRECTIONAL:		/* writeback and invalidate */
		__flush_purge_region(addr, size);
		break;
	default:
		BUG();
	}
}
