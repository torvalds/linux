/*
 * arch/sh/mm/consistent.c
 *
 * Copyright (C) 2004 - 2007  Paul Mundt
 *
 * Declared coherent memory functions based on arch/x86/kernel/pci-dma_32.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <asm/cacheflush.h>
#include <asm/addrspace.h>
#include <asm/io.h>

struct dma_coherent_mem {
	void		*virt_base;
	u32		device_base;
	int		size;
	int		flags;
	unsigned long	*bitmap;
};

void *dma_alloc_coherent(struct device *dev, size_t size,
			   dma_addr_t *dma_handle, gfp_t gfp)
{
	void *ret;
	struct dma_coherent_mem *mem = dev ? dev->dma_mem : NULL;
	int order = get_order(size);

	if (mem) {
		int page = bitmap_find_free_region(mem->bitmap, mem->size,
						     order);
		if (page >= 0) {
			*dma_handle = mem->device_base + (page << PAGE_SHIFT);
			ret = mem->virt_base + (page << PAGE_SHIFT);
			memset(ret, 0, size);
			return ret;
		}
		if (mem->flags & DMA_MEMORY_EXCLUSIVE)
			return NULL;
	}

	ret = (void *)__get_free_pages(gfp, order);

	if (ret != NULL) {
		memset(ret, 0, size);
		/*
		 * Pages from the page allocator may have data present in
		 * cache. So flush the cache before using uncached memory.
		 */
		dma_cache_sync(NULL, ret, size, DMA_BIDIRECTIONAL);
		*dma_handle = virt_to_phys(ret);
	}
	return ret;
}
EXPORT_SYMBOL(dma_alloc_coherent);

void dma_free_coherent(struct device *dev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	struct dma_coherent_mem *mem = dev ? dev->dma_mem : NULL;
	int order = get_order(size);

	if (mem && vaddr >= mem->virt_base && vaddr < (mem->virt_base + (mem->size << PAGE_SHIFT))) {
		int page = (vaddr - mem->virt_base) >> PAGE_SHIFT;

		bitmap_release_region(mem->bitmap, page, order);
	} else {
		WARN_ON(irqs_disabled());	/* for portability */
		BUG_ON(mem && mem->flags & DMA_MEMORY_EXCLUSIVE);
		free_pages((unsigned long)vaddr, order);
	}
}
EXPORT_SYMBOL(dma_free_coherent);

int dma_declare_coherent_memory(struct device *dev, dma_addr_t bus_addr,
				dma_addr_t device_addr, size_t size, int flags)
{
	void __iomem *mem_base = NULL;
	int pages = size >> PAGE_SHIFT;
	int bitmap_size = BITS_TO_LONGS(pages) * sizeof(long);

	if ((flags & (DMA_MEMORY_MAP | DMA_MEMORY_IO)) == 0)
		goto out;
	if (!size)
		goto out;
	if (dev->dma_mem)
		goto out;

	/* FIXME: this routine just ignores DMA_MEMORY_INCLUDES_CHILDREN */

	mem_base = ioremap_nocache(bus_addr, size);
	if (!mem_base)
		goto out;

	dev->dma_mem = kmalloc(sizeof(struct dma_coherent_mem), GFP_KERNEL);
	if (!dev->dma_mem)
		goto out;
	dev->dma_mem->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!dev->dma_mem->bitmap)
		goto free1_out;

	dev->dma_mem->virt_base = mem_base;
	dev->dma_mem->device_base = device_addr;
	dev->dma_mem->size = pages;
	dev->dma_mem->flags = flags;

	if (flags & DMA_MEMORY_MAP)
		return DMA_MEMORY_MAP;

	return DMA_MEMORY_IO;

 free1_out:
	kfree(dev->dma_mem);
 out:
	if (mem_base)
		iounmap(mem_base);
	return 0;
}
EXPORT_SYMBOL(dma_declare_coherent_memory);

void dma_release_declared_memory(struct device *dev)
{
	struct dma_coherent_mem *mem = dev->dma_mem;

	if (!mem)
		return;
	dev->dma_mem = NULL;
	iounmap(mem->virt_base);
	kfree(mem->bitmap);
	kfree(mem);
}
EXPORT_SYMBOL(dma_release_declared_memory);

void *dma_mark_declared_memory_occupied(struct device *dev,
					dma_addr_t device_addr, size_t size)
{
	struct dma_coherent_mem *mem = dev->dma_mem;
	int pages = (size + (device_addr & ~PAGE_MASK) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	int pos, err;

	if (!mem)
		return ERR_PTR(-EINVAL);

	pos = (device_addr - mem->device_base) >> PAGE_SHIFT;
	err = bitmap_allocate_region(mem->bitmap, pos, get_order(pages));
	if (err != 0)
		return ERR_PTR(err);
	return mem->virt_base + (pos << PAGE_SHIFT);
}
EXPORT_SYMBOL(dma_mark_declared_memory_occupied);

void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
		    enum dma_data_direction direction)
{
#ifdef CONFIG_CPU_SH5
	void *p1addr = vaddr;
#else
	void *p1addr = (void*) P1SEGADDR((unsigned long)vaddr);
#endif

	switch (direction) {
	case DMA_FROM_DEVICE:		/* invalidate only */
		__flush_invalidate_region(p1addr, size);
		break;
	case DMA_TO_DEVICE:		/* writeback only */
		__flush_wback_region(p1addr, size);
		break;
	case DMA_BIDIRECTIONAL:		/* writeback and invalidate */
		__flush_purge_region(p1addr, size);
		break;
	default:
		BUG();
	}
}
EXPORT_SYMBOL(dma_cache_sync);
