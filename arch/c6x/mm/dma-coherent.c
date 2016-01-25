/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot <aurelien.jacquiot@ti.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  DMA uncached mapping support.
 *
 *  Using code pulled from ARM
 *  Copyright (C) 2000-2004 Russell King
 *
 */
#include <linux/slab.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/memblock.h>

#include <asm/page.h>

/*
 * DMA coherent memory management, can be redefined using the memdma=
 * kernel command line
 */

/* none by default */
static phys_addr_t dma_base;
static u32 dma_size;
static u32 dma_pages;

static unsigned long *dma_bitmap;

/* bitmap lock */
static DEFINE_SPINLOCK(dma_lock);

/*
 * Return a DMA coherent and contiguous memory chunk from the DMA memory
 */
static inline u32 __alloc_dma_pages(int order)
{
	unsigned long flags;
	u32 pos;

	spin_lock_irqsave(&dma_lock, flags);
	pos = bitmap_find_free_region(dma_bitmap, dma_pages, order);
	spin_unlock_irqrestore(&dma_lock, flags);

	return dma_base + (pos << PAGE_SHIFT);
}

static void __free_dma_pages(u32 addr, int order)
{
	unsigned long flags;
	u32 pos = (addr - dma_base) >> PAGE_SHIFT;

	if (addr < dma_base || (pos + (1 << order)) >= dma_pages) {
		printk(KERN_ERR "%s: freeing outside range.\n", __func__);
		BUG();
	}

	spin_lock_irqsave(&dma_lock, flags);
	bitmap_release_region(dma_bitmap, pos, order);
	spin_unlock_irqrestore(&dma_lock, flags);
}

/*
 * Allocate DMA coherent memory space and return both the kernel
 * virtual and DMA address for that space.
 */
void *c6x_dma_alloc(struct device *dev, size_t size, dma_addr_t *handle,
		gfp_t gfp, struct dma_attrs *attrs)
{
	u32 paddr;
	int order;

	if (!dma_size || !size)
		return NULL;

	order = get_count_order(((size - 1) >> PAGE_SHIFT) + 1);

	paddr = __alloc_dma_pages(order);

	if (handle)
		*handle = paddr;

	if (!paddr)
		return NULL;

	return phys_to_virt(paddr);
}

/*
 * Free DMA coherent memory as defined by the above mapping.
 */
void c6x_dma_free(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_handle, struct dma_attrs *attrs)
{
	int order;

	if (!dma_size || !size)
		return;

	order = get_count_order(((size - 1) >> PAGE_SHIFT) + 1);

	__free_dma_pages(virt_to_phys(vaddr), order);
}

/*
 * Initialise the coherent DMA memory allocator using the given uncached region.
 */
void __init coherent_mem_init(phys_addr_t start, u32 size)
{
	phys_addr_t bitmap_phys;

	if (!size)
		return;

	printk(KERN_INFO
	       "Coherent memory (DMA) region start=0x%x size=0x%x\n",
	       start, size);

	dma_base = start;
	dma_size = size;

	/* allocate bitmap */
	dma_pages = dma_size >> PAGE_SHIFT;
	if (dma_size & (PAGE_SIZE - 1))
		++dma_pages;

	bitmap_phys = memblock_alloc(BITS_TO_LONGS(dma_pages) * sizeof(long),
				     sizeof(long));

	dma_bitmap = phys_to_virt(bitmap_phys);
	memset(dma_bitmap, 0, dma_pages * PAGE_SIZE);
}
