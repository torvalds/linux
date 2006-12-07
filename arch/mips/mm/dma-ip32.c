/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000  Ani Joshi <ajoshi@unixbox.com>
 * Copyright (C) 2000, 2001  Ralf Baechle <ralf@gnu.org>
 * Copyright (C) 2005 Ilya A. Volynets-Evenbakh <ilya@total-knowledge.com>
 * swiped from i386, and cloned for MIPS by Geert, polished by Ralf.
 * IP32 changes by Ilya.
 */
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/dma-mapping.h>

#include <asm/cache.h>
#include <asm/io.h>
#include <asm/ip32/crime.h>

/*
 * Warning on the terminology - Linux calls an uncached area coherent;
 * MIPS terminology calls memory areas with hardware maintained coherency
 * coherent.
 */

/*
 * Few notes.
 * 1. CPU sees memory as two chunks: 0-256M@0x0, and the rest @0x40000000+256M
 * 2. PCI sees memory as one big chunk @0x0 (or we could use 0x40000000 for native-endian)
 * 3. All other devices see memory as one big chunk at 0x40000000
 * 4. Non-PCI devices will pass NULL as struct device*
 * Thus we translate differently, depending on device.
 */

#define RAM_OFFSET_MASK	0x3fffffff

void *dma_alloc_noncoherent(struct device *dev, size_t size,
	dma_addr_t * dma_handle, gfp_t gfp)
{
	void *ret;
	/* ignore region specifiers */
	gfp &= ~(__GFP_DMA | __GFP_HIGHMEM);

	if (dev == NULL || (dev->coherent_dma_mask < 0xffffffff))
		gfp |= GFP_DMA;
	ret = (void *) __get_free_pages(gfp, get_order(size));

	if (ret != NULL) {
		unsigned long addr = virt_to_phys(ret)&RAM_OFFSET_MASK;
		memset(ret, 0, size);
		if(dev==NULL)
		    addr+= CRIME_HI_MEM_BASE;
		*dma_handle = addr;
	}

	return ret;
}

EXPORT_SYMBOL(dma_alloc_noncoherent);

void *dma_alloc_coherent(struct device *dev, size_t size,
	dma_addr_t * dma_handle, gfp_t gfp)
{
	void *ret;

	ret = dma_alloc_noncoherent(dev, size, dma_handle, gfp);
	if (ret) {
		dma_cache_wback_inv((unsigned long) ret, size);
		ret = UNCAC_ADDR(ret);
	}

	return ret;
}

EXPORT_SYMBOL(dma_alloc_coherent);

void dma_free_noncoherent(struct device *dev, size_t size, void *vaddr,
	dma_addr_t dma_handle)
{
	free_pages((unsigned long) vaddr, get_order(size));
}

EXPORT_SYMBOL(dma_free_noncoherent);

void dma_free_coherent(struct device *dev, size_t size, void *vaddr,
	dma_addr_t dma_handle)
{
	unsigned long addr = (unsigned long) vaddr;

	addr = CAC_ADDR(addr);
	free_pages(addr, get_order(size));
}

EXPORT_SYMBOL(dma_free_coherent);

static inline void __dma_sync(unsigned long addr, size_t size,
	enum dma_data_direction direction)
{
	switch (direction) {
	case DMA_TO_DEVICE:
		dma_cache_wback(addr, size);
		break;

	case DMA_FROM_DEVICE:
		dma_cache_inv(addr, size);
		break;

	case DMA_BIDIRECTIONAL:
		dma_cache_wback_inv(addr, size);
		break;

	default:
		BUG();
	}
}

dma_addr_t dma_map_single(struct device *dev, void *ptr, size_t size,
	enum dma_data_direction direction)
{
	unsigned long addr = (unsigned long) ptr;

	switch (direction) {
	case DMA_TO_DEVICE:
		dma_cache_wback(addr, size);
		break;

	case DMA_FROM_DEVICE:
		dma_cache_inv(addr, size);
		break;

	case DMA_BIDIRECTIONAL:
		dma_cache_wback_inv(addr, size);
		break;

	default:
		BUG();
	}

	addr = virt_to_phys(ptr)&RAM_OFFSET_MASK;
	if(dev == NULL)
	    addr+=CRIME_HI_MEM_BASE;
	return (dma_addr_t)addr;
}

EXPORT_SYMBOL(dma_map_single);

void dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
	enum dma_data_direction direction)
{
	switch (direction) {
	case DMA_TO_DEVICE:
		break;

	case DMA_FROM_DEVICE:
		break;

	case DMA_BIDIRECTIONAL:
		break;

	default:
		BUG();
	}
}

EXPORT_SYMBOL(dma_unmap_single);

int dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
	enum dma_data_direction direction)
{
	int i;

	BUG_ON(direction == DMA_NONE);

	for (i = 0; i < nents; i++, sg++) {
		unsigned long addr;

		addr = (unsigned long) page_address(sg->page)+sg->offset;
		if (addr)
			__dma_sync(addr, sg->length, direction);
		addr = __pa(addr)&RAM_OFFSET_MASK;
		if(dev == NULL)
			addr +=  CRIME_HI_MEM_BASE;
		sg->dma_address = (dma_addr_t)addr;
	}

	return nents;
}

EXPORT_SYMBOL(dma_map_sg);

dma_addr_t dma_map_page(struct device *dev, struct page *page,
	unsigned long offset, size_t size, enum dma_data_direction direction)
{
	unsigned long addr;

	BUG_ON(direction == DMA_NONE);

	addr = (unsigned long) page_address(page) + offset;
	dma_cache_wback_inv(addr, size);
	addr = __pa(addr)&RAM_OFFSET_MASK;
	if(dev == NULL)
		addr +=  CRIME_HI_MEM_BASE;

	return (dma_addr_t)addr;
}

EXPORT_SYMBOL(dma_map_page);

void dma_unmap_page(struct device *dev, dma_addr_t dma_address, size_t size,
	enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	if (direction != DMA_TO_DEVICE) {
		unsigned long addr;

		dma_address&=RAM_OFFSET_MASK;
		addr = dma_address + PAGE_OFFSET;
		if(dma_address>=256*1024*1024)
			addr+=CRIME_HI_MEM_BASE;
		dma_cache_wback_inv(addr, size);
	}
}

EXPORT_SYMBOL(dma_unmap_page);

void dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nhwentries,
	enum dma_data_direction direction)
{
	unsigned long addr;
	int i;

	BUG_ON(direction == DMA_NONE);

	if (direction == DMA_TO_DEVICE)
		return;

	for (i = 0; i < nhwentries; i++, sg++) {
		addr = (unsigned long) page_address(sg->page);
		if (!addr)
			continue;
		dma_cache_wback_inv(addr + sg->offset, sg->length);
	}
}

EXPORT_SYMBOL(dma_unmap_sg);

void dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle,
	size_t size, enum dma_data_direction direction)
{
	unsigned long addr;

	BUG_ON(direction == DMA_NONE);

	dma_handle&=RAM_OFFSET_MASK;
	addr = dma_handle + PAGE_OFFSET;
	if(dma_handle>=256*1024*1024)
	    addr+=CRIME_HI_MEM_BASE;
	__dma_sync(addr, size, direction);
}

EXPORT_SYMBOL(dma_sync_single_for_cpu);

void dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle,
	size_t size, enum dma_data_direction direction)
{
	unsigned long addr;

	BUG_ON(direction == DMA_NONE);

	dma_handle&=RAM_OFFSET_MASK;
	addr = dma_handle + PAGE_OFFSET;
	if(dma_handle>=256*1024*1024)
	    addr+=CRIME_HI_MEM_BASE;
	__dma_sync(addr, size, direction);
}

EXPORT_SYMBOL(dma_sync_single_for_device);

void dma_sync_single_range_for_cpu(struct device *dev, dma_addr_t dma_handle,
	unsigned long offset, size_t size, enum dma_data_direction direction)
{
	unsigned long addr;

	BUG_ON(direction == DMA_NONE);

	dma_handle&=RAM_OFFSET_MASK;
	addr = dma_handle + offset + PAGE_OFFSET;
	if(dma_handle>=256*1024*1024)
	    addr+=CRIME_HI_MEM_BASE;
	__dma_sync(addr, size, direction);
}

EXPORT_SYMBOL(dma_sync_single_range_for_cpu);

void dma_sync_single_range_for_device(struct device *dev, dma_addr_t dma_handle,
	unsigned long offset, size_t size, enum dma_data_direction direction)
{
	unsigned long addr;

	BUG_ON(direction == DMA_NONE);

	dma_handle&=RAM_OFFSET_MASK;
	addr = dma_handle + offset + PAGE_OFFSET;
	if(dma_handle>=256*1024*1024)
	    addr+=CRIME_HI_MEM_BASE;
	__dma_sync(addr, size, direction);
}

EXPORT_SYMBOL(dma_sync_single_range_for_device);

void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg, int nelems,
	enum dma_data_direction direction)
{
	int i;

	BUG_ON(direction == DMA_NONE);

	/* Make sure that gcc doesn't leave the empty loop body.  */
	for (i = 0; i < nelems; i++, sg++)
		__dma_sync((unsigned long)page_address(sg->page),
		           sg->length, direction);
}

EXPORT_SYMBOL(dma_sync_sg_for_cpu);

void dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg, int nelems,
	enum dma_data_direction direction)
{
	int i;

	BUG_ON(direction == DMA_NONE);

	/* Make sure that gcc doesn't leave the empty loop body.  */
	for (i = 0; i < nelems; i++, sg++)
		__dma_sync((unsigned long)page_address(sg->page),
		           sg->length, direction);
}

EXPORT_SYMBOL(dma_sync_sg_for_device);

int dma_mapping_error(dma_addr_t dma_addr)
{
	return 0;
}

EXPORT_SYMBOL(dma_mapping_error);

int dma_supported(struct device *dev, u64 mask)
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

EXPORT_SYMBOL(dma_supported);

int dma_is_consistent(struct device *dev, dma_addr_t dma_addr)
{
	return 1;
}

EXPORT_SYMBOL(dma_is_consistent);

void dma_cache_sync(void *vaddr, size_t size, enum dma_data_direction direction)
{
	if (direction == DMA_NONE)
		return;

	dma_cache_wback_inv((unsigned long)vaddr, size);
}

EXPORT_SYMBOL(dma_cache_sync);

