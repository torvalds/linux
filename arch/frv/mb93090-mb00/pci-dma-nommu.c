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

void *dma_alloc_coherent(struct device *hwdev, size_t size, dma_addr_t *dma_handle, gfp_t gfp)
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

EXPORT_SYMBOL(dma_alloc_coherent);

void dma_free_coherent(struct device *hwdev, size_t size, void *vaddr, dma_addr_t dma_handle)
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

EXPORT_SYMBOL(dma_free_coherent);

dma_addr_t dma_map_single(struct device *dev, void *ptr, size_t size,
			  enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	frv_cache_wback_inv((unsigned long) ptr, (unsigned long) ptr + size);

	return virt_to_bus(ptr);
}

EXPORT_SYMBOL(dma_map_single);

int dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	       enum dma_data_direction direction)
{
	int i;

	for (i=0; i<nents; i++)
		frv_cache_wback_inv(sg_dma_address(&sg[i]),
				    sg_dma_address(&sg[i]) + sg_dma_len(&sg[i]));

	BUG_ON(direction == DMA_NONE);

	return nents;
}

EXPORT_SYMBOL(dma_map_sg);

dma_addr_t dma_map_page(struct device *dev, struct page *page, unsigned long offset,
			size_t size, enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	flush_dcache_page(page);
	return (dma_addr_t) page_to_phys(page) + offset;
}

EXPORT_SYMBOL(dma_map_page);
