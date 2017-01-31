/*
 * Dynamic DMA mapping support
 *
 * Copyright 2005-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#include <linux/types.h>
#include <linux/gfp.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/export.h>
#include <linux/bitmap.h>

static spinlock_t dma_page_lock;
static unsigned long *dma_page;
static unsigned int dma_pages;
static unsigned long dma_base;
static unsigned long dma_size;
static unsigned int dma_initialized;

static void dma_alloc_init(unsigned long start, unsigned long end)
{
	spin_lock_init(&dma_page_lock);
	dma_initialized = 0;

	dma_page = (unsigned long *)__get_free_page(GFP_KERNEL);
	memset(dma_page, 0, PAGE_SIZE);
	dma_base = PAGE_ALIGN(start);
	dma_size = PAGE_ALIGN(end) - PAGE_ALIGN(start);
	dma_pages = dma_size >> PAGE_SHIFT;
	memset((void *)dma_base, 0, DMA_UNCACHED_REGION);
	dma_initialized = 1;

	printk(KERN_INFO "%s: dma_page @ 0x%p - %d pages at 0x%08lx\n", __func__,
	       dma_page, dma_pages, dma_base);
}

static inline unsigned int get_pages(size_t size)
{
	return ((size - 1) >> PAGE_SHIFT) + 1;
}

static unsigned long __alloc_dma_pages(unsigned int pages)
{
	unsigned long ret = 0, flags;
	unsigned long start;

	if (dma_initialized == 0)
		dma_alloc_init(_ramend - DMA_UNCACHED_REGION, _ramend);

	spin_lock_irqsave(&dma_page_lock, flags);

	start = bitmap_find_next_zero_area(dma_page, dma_pages, 0, pages, 0);
	if (start < dma_pages) {
		ret = dma_base + (start << PAGE_SHIFT);
		bitmap_set(dma_page, start, pages);
	}
	spin_unlock_irqrestore(&dma_page_lock, flags);
	return ret;
}

static void __free_dma_pages(unsigned long addr, unsigned int pages)
{
	unsigned long page = (addr - dma_base) >> PAGE_SHIFT;
	unsigned long flags;

	if ((page + pages) > dma_pages) {
		printk(KERN_ERR "%s: freeing outside range.\n", __func__);
		BUG();
	}

	spin_lock_irqsave(&dma_page_lock, flags);
	bitmap_clear(dma_page, page, pages);
	spin_unlock_irqrestore(&dma_page_lock, flags);
}

static void *bfin_dma_alloc(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs)
{
	void *ret;

	ret = (void *)__alloc_dma_pages(get_pages(size));

	if (ret) {
		memset(ret, 0, size);
		*dma_handle = virt_to_phys(ret);
	}

	return ret;
}

static void bfin_dma_free(struct device *dev, size_t size, void *vaddr,
		  dma_addr_t dma_handle, unsigned long attrs)
{
	__free_dma_pages((unsigned long)vaddr, get_pages(size));
}

/*
 * Streaming DMA mappings
 */
void __dma_sync(dma_addr_t addr, size_t size,
		enum dma_data_direction dir)
{
	__dma_sync_inline(addr, size, dir);
}
EXPORT_SYMBOL(__dma_sync);

static int bfin_dma_map_sg(struct device *dev, struct scatterlist *sg_list,
		int nents, enum dma_data_direction direction,
		unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sg_list, sg, nents, i) {
		sg->dma_address = (dma_addr_t) sg_virt(sg);

		if (attrs & DMA_ATTR_SKIP_CPU_SYNC)
			continue;

		__dma_sync(sg_dma_address(sg), sg_dma_len(sg), direction);
	}

	return nents;
}

static void bfin_dma_sync_sg_for_device(struct device *dev,
		struct scatterlist *sg_list, int nelems,
		enum dma_data_direction direction)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sg_list, sg, nelems, i) {
		sg->dma_address = (dma_addr_t) sg_virt(sg);
		__dma_sync(sg_dma_address(sg), sg_dma_len(sg), direction);
	}
}

static dma_addr_t bfin_dma_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size, enum dma_data_direction dir,
		unsigned long attrs)
{
	dma_addr_t handle = (dma_addr_t)(page_address(page) + offset);

	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		_dma_sync(handle, size, dir);

	return handle;
}

static inline void bfin_dma_sync_single_for_device(struct device *dev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	_dma_sync(handle, size, dir);
}

struct dma_map_ops bfin_dma_ops = {
	.alloc			= bfin_dma_alloc,
	.free			= bfin_dma_free,

	.map_page		= bfin_dma_map_page,
	.map_sg			= bfin_dma_map_sg,

	.sync_single_for_device	= bfin_dma_sync_single_for_device,
	.sync_sg_for_device	= bfin_dma_sync_sg_for_device,
};
EXPORT_SYMBOL(bfin_dma_ops);
