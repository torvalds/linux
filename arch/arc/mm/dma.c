/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/dma-noncoherent.h>
#include <asm/cache.h>
#include <asm/cacheflush.h>

/*
 * ARCH specific callbacks for generic noncoherent DMA ops (dma/noncoherent.c)
 *  - hardware IOC not available (or "dma-coherent" not set for device in DT)
 *  - But still handle both coherent and non-coherent requests from caller
 *
 * For DMA coherent hardware (IOC) generic code suffices
 */
void *arch_dma_alloc(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs)
{
	unsigned long order = get_order(size);
	struct page *page;
	phys_addr_t paddr;
	void *kvaddr;
	bool need_coh = !(attrs & DMA_ATTR_NON_CONSISTENT);

	/*
	 * __GFP_HIGHMEM flag is cleared by upper layer functions
	 * (in include/linux/dma-mapping.h) so we should never get a
	 * __GFP_HIGHMEM here.
	 */
	BUG_ON(gfp & __GFP_HIGHMEM);

	page = alloc_pages(gfp, order);
	if (!page)
		return NULL;

	/* This is linear addr (0x8000_0000 based) */
	paddr = page_to_phys(page);

	*dma_handle = paddr;

	/*
	 * A coherent buffer needs MMU mapping to enforce non-cachability.
	 * kvaddr is kernel Virtual address (0x7000_0000 based).
	 */
	if (need_coh) {
		kvaddr = ioremap_nocache(paddr, size);
		if (kvaddr == NULL) {
			__free_pages(page, order);
			return NULL;
		}
	} else {
		kvaddr = (void *)(u32)paddr;
	}

	/*
	 * Evict any existing L1 and/or L2 lines for the backing page
	 * in case it was used earlier as a normal "cached" page.
	 * Yeah this bit us - STAR 9000898266
	 *
	 * Although core does call flush_cache_vmap(), it gets kvaddr hence
	 * can't be used to efficiently flush L1 and/or L2 which need paddr
	 * Currently flush_cache_vmap nukes the L1 cache completely which
	 * will be optimized as a separate commit
	 */
	if (need_coh)
		dma_cache_wback_inv(paddr, size);

	return kvaddr;
}

void arch_dma_free(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_handle, unsigned long attrs)
{
	phys_addr_t paddr = dma_handle;
	struct page *page = virt_to_page(paddr);

	if (!(attrs & DMA_ATTR_NON_CONSISTENT))
		iounmap((void __force __iomem *)vaddr);

	__free_pages(page, get_order(size));
}

int arch_dma_mmap(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs)
{
	unsigned long user_count = vma_pages(vma);
	unsigned long count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long pfn = __phys_to_pfn(dma_addr);
	unsigned long off = vma->vm_pgoff;
	int ret = -ENXIO;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (dma_mmap_from_dev_coherent(dev, vma, cpu_addr, size, &ret))
		return ret;

	if (off < count && user_count <= (count - off)) {
		ret = remap_pfn_range(vma, vma->vm_start,
				      pfn + off,
				      user_count << PAGE_SHIFT,
				      vma->vm_page_prot);
	}

	return ret;
}

/*
 * Cache operations depending on function and direction argument, inspired by
 * https://lkml.org/lkml/2018/5/18/979
 * "dma_sync_*_for_cpu and direction=TO_DEVICE (was Re: [PATCH 02/20]
 * dma-mapping: provide a generic dma-noncoherent implementation)"
 *
 *          |   map          ==  for_device     |   unmap     ==  for_cpu
 *          |----------------------------------------------------------------
 * TO_DEV   |   writeback        writeback      |   none          none
 * FROM_DEV |   invalidate       invalidate     |   invalidate*   invalidate*
 * BIDIR    |   writeback+inv    writeback+inv  |   invalidate    invalidate
 *
 *     [*] needed for CPU speculative prefetches
 *
 * NOTE: we don't check the validity of direction argument as it is done in
 * upper layer functions (in include/linux/dma-mapping.h)
 */

void arch_sync_dma_for_device(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
		dma_cache_wback(paddr, size);
		break;

	case DMA_FROM_DEVICE:
		dma_cache_inv(paddr, size);
		break;

	case DMA_BIDIRECTIONAL:
		dma_cache_wback_inv(paddr, size);
		break;

	default:
		break;
	}
}

void arch_sync_dma_for_cpu(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
		break;

	/* FROM_DEVICE invalidate needed if speculative CPU prefetch only */
	case DMA_FROM_DEVICE:
	case DMA_BIDIRECTIONAL:
		dma_cache_inv(paddr, size);
		break;

	default:
		break;
	}
}

/*
 * Plug in coherent or noncoherent dma ops
 */
void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
			const struct iommu_ops *iommu, bool coherent)
{
	/*
	 * IOC hardware snoops all DMA traffic keeping the caches consistent
	 * with memory - eliding need for any explicit cache maintenance of
	 * DMA buffers - so we can use dma_direct cache ops.
	 */
	if (is_isa_arcv2() && ioc_enable && coherent) {
		set_dma_ops(dev, &dma_direct_ops);
		dev_info(dev, "use dma_direct_ops cache ops\n");
	} else {
		set_dma_ops(dev, &dma_noncoherent_ops);
		dev_info(dev, "use dma_noncoherent_ops cache ops\n");
	}
}
