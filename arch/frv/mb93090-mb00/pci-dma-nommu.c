/* pci-dma-nommu.c: Dynamic DMA mapping support for the FRV
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Woodhouse (dwmw2@infradead.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <asm/io.h>

#if 1
#define DMA_SRAM_START	dma_coherent_mem_start
#define DMA_SRAM_END	dma_coherent_mem_end
#else // Use video RAM on Matrox
#define DMA_SRAM_START	0xe8900000
#define DMA_SRAM_END	0xe8a00000
#endif

struct dma_alloc_record {
	struct list_head	list;
	unsigned long		ofs;
	unsigned long		len;
};

static DEFINE_SPINLOCK(dma_alloc_lock);
static LIST_HEAD(dma_alloc_list);

static void *frv_dma_alloc(struct device *hwdev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs)
{
	struct dma_alloc_record *new;
	struct list_head *this = &dma_alloc_list;
	unsigned long flags;
	unsigned long start = DMA_SRAM_START;
	unsigned long end;

	if (!DMA_SRAM_START) {
		printk("%s called without any DMA area reserved!\n", __func__);
		return NULL;
	}

	new = kmalloc(sizeof (*new), GFP_ATOMIC);
	if (!new)
		return NULL;

	/* Round up to a reasonable alignment */
	new->len = (size + 31) & ~31;

	spin_lock_irqsave(&dma_alloc_lock, flags);

	list_for_each (this, &dma_alloc_list) {
		struct dma_alloc_record *this_r = list_entry(this, struct dma_alloc_record, list);
		end = this_r->ofs;

		if (end - start >= size)
			goto gotone;

		start = this_r->ofs + this_r->len;
	}
	/* Reached end of list. */
	end = DMA_SRAM_END;
	this = &dma_alloc_list;

	if (end - start >= size) {
	gotone:
		new->ofs = start;
		list_add_tail(&new->list, this);
		spin_unlock_irqrestore(&dma_alloc_lock, flags);

		*dma_handle = start;
		return (void *)start;
	}

	kfree(new);
	spin_unlock_irqrestore(&dma_alloc_lock, flags);
	return NULL;
}

static void frv_dma_free(struct device *hwdev, size_t size, void *vaddr,
		dma_addr_t dma_handle, unsigned long attrs)
{
	struct dma_alloc_record *rec;
	unsigned long flags;

	spin_lock_irqsave(&dma_alloc_lock, flags);

	list_for_each_entry(rec, &dma_alloc_list, list) {
		if (rec->ofs == dma_handle) {
			list_del(&rec->list);
			kfree(rec);
			spin_unlock_irqrestore(&dma_alloc_lock, flags);
			return;
		}
	}
	spin_unlock_irqrestore(&dma_alloc_lock, flags);
	BUG();
}

static int frv_dma_map_sg(struct device *dev, struct scatterlist *sglist,
		int nents, enum dma_data_direction direction,
		unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(direction == DMA_NONE);

	if (attrs & DMA_ATTR_SKIP_CPU_SYNC)
		return nents;

	for_each_sg(sglist, sg, nents, i) {
		frv_cache_wback_inv(sg_dma_address(sg),
				    sg_dma_address(sg) + sg_dma_len(sg));
	}

	return nents;
}

static dma_addr_t frv_dma_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size,
		enum dma_data_direction direction, unsigned long attrs)
{
	BUG_ON(direction == DMA_NONE);

	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		flush_dcache_page(page);

	return (dma_addr_t) page_to_phys(page) + offset;
}

static void frv_dma_sync_single_for_device(struct device *dev,
		dma_addr_t dma_handle, size_t size,
		enum dma_data_direction direction)
{
	flush_write_buffers();
}

static void frv_dma_sync_sg_for_device(struct device *dev,
		struct scatterlist *sg, int nelems,
		enum dma_data_direction direction)
{
	flush_write_buffers();
}


static int frv_dma_supported(struct device *dev, u64 mask)
{
        /*
         * we fall back to GFP_DMA when the mask isn't all 1s,
         * so we can't guarantee allocations that must be
         * within a tighter range than GFP_DMA..
         */
        if (mask < 0x00ffffff)
                return 0;
	return 1;
}

const struct dma_map_ops frv_dma_ops = {
	.alloc			= frv_dma_alloc,
	.free			= frv_dma_free,
	.map_page		= frv_dma_map_page,
	.map_sg			= frv_dma_map_sg,
	.sync_single_for_device	= frv_dma_sync_single_for_device,
	.sync_sg_for_device	= frv_dma_sync_sg_for_device,
	.dma_supported		= frv_dma_supported,
};
EXPORT_SYMBOL(frv_dma_ops);
