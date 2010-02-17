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
	int i, count = 0;

	if (dma_initialized == 0)
		dma_alloc_init(_ramend - DMA_UNCACHED_REGION, _ramend);

	spin_lock_irqsave(&dma_page_lock, flags);

	for (i = 0; i < dma_pages;) {
		if (test_bit(i++, dma_page) == 0) {
			if (++count == pages) {
				while (count--)
					__set_bit(--i, dma_page);

				ret = dma_base + (i << PAGE_SHIFT);
				break;
			}
		} else
			count = 0;
	}
	spin_unlock_irqrestore(&dma_page_lock, flags);
	return ret;
}

static void __free_dma_pages(unsigned long addr, unsigned int pages)
{
	unsigned long page = (addr - dma_base) >> PAGE_SHIFT;
	unsigned long flags;
	int i;

	if ((page + pages) > dma_pages) {
		printk(KERN_ERR "%s: freeing outside range.\n", __func__);
		BUG();
	}

	spin_lock_irqsave(&dma_page_lock, flags);
	for (i = page; i < page + pages; i++)
		__clear_bit(i, dma_page);

	spin_unlock_irqrestore(&dma_page_lock, flags);
}

void *dma_alloc_coherent(struct device *dev, size_t size,
			 dma_addr_t *dma_handle, gfp_t gfp)
{
	void *ret;

	ret = (void *)__alloc_dma_pages(get_pages(size));

	if (ret) {
		memset(ret, 0, size);
		*dma_handle = virt_to_phys(ret);
	}

	return ret;
}
EXPORT_SYMBOL(dma_alloc_coherent);

void
dma_free_coherent(struct device *dev, size_t size, void *vaddr,
		  dma_addr_t dma_handle)
{
	__free_dma_pages((unsigned long)vaddr, get_pages(size));
}
EXPORT_SYMBOL(dma_free_coherent);

/*
 * Streaming DMA mappings
 */
void __dma_sync(dma_addr_t addr, size_t size,
		enum dma_data_direction dir)
{
	_dma_sync(addr, size, dir);
}
EXPORT_SYMBOL(__dma_sync);

int
dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	   enum dma_data_direction direction)
{
	int i;

	for (i = 0; i < nents; i++, sg++) {
		sg->dma_address = (dma_addr_t) sg_virt(sg);
		__dma_sync(sg_dma_address(sg), sg_dma_len(sg), direction);
	}

	return nents;
}
EXPORT_SYMBOL(dma_map_sg);

void dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
			    int nelems, enum dma_data_direction direction)
{
	int i;

	for (i = 0; i < nelems; i++, sg++) {
		sg->dma_address = (dma_addr_t) sg_virt(sg);
		__dma_sync(sg_dma_address(sg), sg_dma_len(sg), direction);
	}
}
EXPORT_SYMBOL(dma_sync_sg_for_device);
