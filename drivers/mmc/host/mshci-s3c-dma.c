/*
* linux/drivers/mmc/host/mshci-s3c-dma.c
* Mobile Storage Host Controller Interface driver
*
* Copyright (c) 2011 Samsung Electronics Co., Ltd.
*		http://www.samsung.com
*

* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or (at
* your option) any later version.
*
*/
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>

#include <asm/memory.h>
#include <asm/highmem.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/sizes.h>

#include <linux/mmc/host.h>

#include "mshci.h"


static void mshci_s3c_dma_cache_maint_page(struct page *page, 
	unsigned long offset, size_t size, enum dma_data_direction dir,
	void (*op)(const void *, size_t, int), int flush_type, int enable)
{
	/*
	 * A single sg entry may refer to multiple physically contiguous
	 * pages.  But we still need to process highmem pages individually.
	 * If highmem is not configured then the bulk of this loop gets
	 * optimized out.
	 */
	size_t left = size;
	do {
		size_t len = left;
		void *vaddr;

		if (PageHighMem(page)) {
			if (len + offset > PAGE_SIZE) {
				if (offset >= PAGE_SIZE) {
					page += offset / PAGE_SIZE;
					offset %= PAGE_SIZE;
				}
				len = PAGE_SIZE - offset;
			}
			vaddr = kmap_high_get(page);
			if (vaddr) {
				vaddr += offset;
				if (flush_type == 0 && enable)
					op(vaddr, len, dir);
				kunmap_high(page);
			} else if (cache_is_vipt()) {
                                /* unmapped pages might still be cached */
                                vaddr = kmap_atomic(page);
                                op(vaddr + offset, len, dir);
                                kunmap_atomic(vaddr);
			}
		} else {
			vaddr = page_address(page) + offset;
			if (flush_type == 0 && enable)
				op(vaddr, len, dir);
		}
		offset = 0;
		page++;
		left -= len;

	} while (left);
}


void mshci_s3c_dma_page_cpu_to_dev(struct page *page, unsigned long off,
	size_t size, enum dma_data_direction dir, int flush_type)
{
	unsigned long paddr;

	if (dir != DMA_FROM_DEVICE) {
		mshci_s3c_dma_cache_maint_page(page, off, size, dir, 
			dmac_map_area, 
			flush_type, 1);

		paddr = page_to_phys(page) + off;
		if (flush_type != 2) {
			outer_clean_range(paddr, paddr + size);
		}
		/* FIXME: non-speculating: flush on bidirectional mappings? */
	} else {
		paddr = page_to_phys(page) + off;

		/* if flush all L1 cache,
		   L2 cache dose not neet to be clean. 
		   because, all buffer dose not have split space */
		if (flush_type != 2) {
			outer_clean_range(paddr, paddr + size);
			outer_inv_range(paddr, paddr + size);
		}
		/* FIXME: non-speculating: flush on bidirectional mappings? */

		mshci_s3c_dma_cache_maint_page(page, off, size, dir, 
			dmac_unmap_area, 
			flush_type, 1);
	}
}


static inline dma_addr_t mshci_s3c_dma_map_page(struct device *dev, 
		struct page *page, unsigned long offset, size_t size, 
		enum dma_data_direction dir, int flush_type)
{
	BUG_ON(!valid_dma_direction(dir));

	mshci_s3c_dma_page_cpu_to_dev(page, offset, size, dir, flush_type);

	return pfn_to_dma(dev, page_to_pfn(page)) + offset;
}

int mshci_s3c_dma_map_sg(struct mshci_host *host, struct device *dev, 
	struct scatterlist *sg,	int nents, enum dma_data_direction dir,
	int flush_type)
{
	struct scatterlist *s;
	int i, j;

	BUG_ON(!valid_dma_direction(dir));

	if (flush_type == 2) {
		spin_unlock_irqrestore(&host->lock, host->sl_flags);
		flush_all_cpu_caches();
		outer_flush_all();
		spin_lock_irqsave(&host->lock, host->sl_flags);
	} else if(flush_type == 1) {
		spin_unlock_irqrestore(&host->lock, host->sl_flags);
		flush_all_cpu_caches();
		spin_lock_irqsave(&host->lock, host->sl_flags);
	}

	for_each_sg(sg, s, nents, i) {
		s->dma_address = mshci_s3c_dma_map_page(dev, sg_page(s), 
				s->offset, s->length, dir, flush_type);
		if (dma_mapping_error(dev, s->dma_address)) {
			goto bad_mapping;
		}
	}

	debug_dma_map_sg(dev, sg, nents, nents, dir);

	/* in case of invaldating cache, invaldating L2 cache
	   must be done prior to invaldating L1 cache */
#if 0
	if (dir == DMA_FROM_DEVICE) {
		if (flush_type == 1) {
			spin_unlock_irqrestore(&host->lock,  host->sl_flags);
			flush_all_cpu_caches();
			spin_lock_irqsave(&host->lock, host->sl_flags);
		}
	}
#endif
	return nents;

 bad_mapping:
	for_each_sg(sg, s, i, j)
		dma_unmap_page(dev, sg_dma_address(s), sg_dma_len(s), dir);
	return 0;
}

void mshci_s3c_dma_page_dev_to_cpu(struct page *page, unsigned long off,
	size_t size, enum dma_data_direction dir, int flush_type)
{

	unsigned long paddr = page_to_phys(page) + off;

	/* FIXME: non-speculating: not required */
	/* don't bother invalidating if DMA to device */

	mshci_s3c_dma_cache_maint_page(page, off, size, dir, dmac_unmap_area,
				flush_type, 0);
}


static inline void mshci_s3c_dma_unmap_page(struct device *dev, 
		dma_addr_t handle, size_t size, 
		enum dma_data_direction dir, int flush_type)
{
	mshci_s3c_dma_page_dev_to_cpu(pfn_to_page(dma_to_pfn(dev, handle)), \
				handle & ~PAGE_MASK, size, dir, flush_type);
}


void mshci_s3c_dma_unmap_sg(struct mshci_host *host,
		struct device *dev, struct scatterlist *sg, 
		int nents, enum dma_data_direction dir, int flush_type)
{
#if 1	
	struct scatterlist *s;
	int i;

	if (dir == DMA_TO_DEVICE)
		for_each_sg(sg, s, nents, i)
			mshci_s3c_dma_unmap_page(dev, sg_dma_address(s), 
						sg_dma_len(s),dir, flush_type);
#endif	
}

MODULE_DESCRIPTION("Samsung MSHCI (HSMMC) own dma map functions");
MODULE_AUTHOR("Hyunsung Jang, <hs79.jang@samsung.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:s3c-mshci");

