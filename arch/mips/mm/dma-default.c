/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000  Ani Joshi <ajoshi@unixbox.com>
 * Copyright (C) 2000, 2001, 06  Ralf Baechle <ralf@linux-mips.org>
 * swiped from i386, and cloned for MIPS by Geert, polished by Ralf.
 */

#include <linux/types.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <linux/gfp.h>

#include <asm/cache.h>
#include <asm/io.h>

#include <dma-coherence.h>

static inline unsigned long dma_addr_to_virt(struct device *dev,
	dma_addr_t dma_addr)
{
	unsigned long addr = plat_dma_addr_to_phys(dev, dma_addr);

	return (unsigned long)phys_to_virt(addr);
}

/*
 * Warning on the terminology - Linux calls an uncached area coherent;
 * MIPS terminology calls memory areas with hardware maintained coherency
 * coherent.
 */

static inline int cpu_is_noncoherent_r10000(struct device *dev)
{
	return !plat_device_is_coherent(dev) &&
	       (current_cpu_type() == CPU_R10000 ||
	       current_cpu_type() == CPU_R12000);
}

static gfp_t massage_gfp_flags(const struct device *dev, gfp_t gfp)
{
	gfp_t dma_flag;

	/* ignore region specifiers */
	gfp &= ~(__GFP_DMA | __GFP_DMA32 | __GFP_HIGHMEM);

#ifdef CONFIG_ISA
	if (dev == NULL)
		dma_flag = __GFP_DMA;
	else
#endif
#if defined(CONFIG_ZONE_DMA32) && defined(CONFIG_ZONE_DMA)
	     if (dev->coherent_dma_mask < DMA_BIT_MASK(32))
			dma_flag = __GFP_DMA;
	else if (dev->coherent_dma_mask < DMA_BIT_MASK(64))
			dma_flag = __GFP_DMA32;
	else
#endif
#if defined(CONFIG_ZONE_DMA32) && !defined(CONFIG_ZONE_DMA)
	     if (dev->coherent_dma_mask < DMA_BIT_MASK(64))
		dma_flag = __GFP_DMA32;
	else
#endif
#if defined(CONFIG_ZONE_DMA) && !defined(CONFIG_ZONE_DMA32)
	     if (dev->coherent_dma_mask < DMA_BIT_MASK(64))
		dma_flag = __GFP_DMA;
	else
#endif
		dma_flag = 0;

	/* Don't invoke OOM killer */
	gfp |= __GFP_NORETRY;

	return gfp | dma_flag;
}

void *dma_alloc_noncoherent(struct device *dev, size_t size,
	dma_addr_t * dma_handle, gfp_t gfp)
{
	void *ret;

	gfp = massage_gfp_flags(dev, gfp);

	ret = (void *) __get_free_pages(gfp, get_order(size));

	if (ret != NULL) {
		memset(ret, 0, size);
		*dma_handle = plat_map_dma_mem(dev, ret, size);
	}

	return ret;
}
EXPORT_SYMBOL(dma_alloc_noncoherent);

static void *mips_dma_alloc_coherent(struct device *dev, size_t size,
	dma_addr_t * dma_handle, gfp_t gfp)
{
	void *ret;

	if (dma_alloc_from_coherent(dev, size, dma_handle, &ret))
		return ret;

	gfp = massage_gfp_flags(dev, gfp);

	ret = (void *) __get_free_pages(gfp, get_order(size));

	if (ret) {
		memset(ret, 0, size);
		*dma_handle = plat_map_dma_mem(dev, ret, size);

		if (!plat_device_is_coherent(dev)) {
			dma_cache_wback_inv((unsigned long) ret, size);
			ret = UNCAC_ADDR(ret);
		}
	}

	return ret;
}


void dma_free_noncoherent(struct device *dev, size_t size, void *vaddr,
	dma_addr_t dma_handle)
{
	plat_unmap_dma_mem(dev, dma_handle, size, DMA_BIDIRECTIONAL);
	free_pages((unsigned long) vaddr, get_order(size));
}
EXPORT_SYMBOL(dma_free_noncoherent);

static void mips_dma_free_coherent(struct device *dev, size_t size, void *vaddr,
	dma_addr_t dma_handle)
{
	unsigned long addr = (unsigned long) vaddr;
	int order = get_order(size);

	if (dma_release_from_coherent(dev, order, vaddr))
		return;

	plat_unmap_dma_mem(dev, dma_handle, size, DMA_BIDIRECTIONAL);

	if (!plat_device_is_coherent(dev))
		addr = CAC_ADDR(addr);

	free_pages(addr, get_order(size));
}

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

static void mips_dma_unmap_page(struct device *dev, dma_addr_t dma_addr,
	size_t size, enum dma_data_direction direction, struct dma_attrs *attrs)
{
	if (cpu_is_noncoherent_r10000(dev))
		__dma_sync(dma_addr_to_virt(dev, dma_addr), size,
		           direction);

	plat_unmap_dma_mem(dev, dma_addr, size, direction);
}

static int mips_dma_map_sg(struct device *dev, struct scatterlist *sg,
	int nents, enum dma_data_direction direction, struct dma_attrs *attrs)
{
	int i;

	for (i = 0; i < nents; i++, sg++) {
		unsigned long addr;

		addr = (unsigned long) sg_virt(sg);
		if (!plat_device_is_coherent(dev) && addr)
			__dma_sync(addr, sg->length, direction);
		sg->dma_address = plat_map_dma_mem(dev,
				                   (void *)addr, sg->length);
	}

	return nents;
}

static dma_addr_t mips_dma_map_page(struct device *dev, struct page *page,
	unsigned long offset, size_t size, enum dma_data_direction direction,
	struct dma_attrs *attrs)
{
	unsigned long addr;

	addr = (unsigned long) page_address(page) + offset;

	if (!plat_device_is_coherent(dev))
		__dma_sync(addr, size, direction);

	return plat_map_dma_mem(dev, (void *)addr, size);
}

static void mips_dma_unmap_sg(struct device *dev, struct scatterlist *sg,
	int nhwentries, enum dma_data_direction direction,
	struct dma_attrs *attrs)
{
	unsigned long addr;
	int i;

	for (i = 0; i < nhwentries; i++, sg++) {
		if (!plat_device_is_coherent(dev) &&
		    direction != DMA_TO_DEVICE) {
			addr = (unsigned long) sg_virt(sg);
			if (addr)
				__dma_sync(addr, sg->length, direction);
		}
		plat_unmap_dma_mem(dev, sg->dma_address, sg->length, direction);
	}
}

static void mips_dma_sync_single_for_cpu(struct device *dev,
	dma_addr_t dma_handle, size_t size, enum dma_data_direction direction)
{
	if (cpu_is_noncoherent_r10000(dev)) {
		unsigned long addr;

		addr = dma_addr_to_virt(dev, dma_handle);
		__dma_sync(addr, size, direction);
	}
}

static void mips_dma_sync_single_for_device(struct device *dev,
	dma_addr_t dma_handle, size_t size, enum dma_data_direction direction)
{
	plat_extra_sync_for_device(dev);
	if (!plat_device_is_coherent(dev)) {
		unsigned long addr;

		addr = dma_addr_to_virt(dev, dma_handle);
		__dma_sync(addr, size, direction);
	}
}

static void mips_dma_sync_sg_for_cpu(struct device *dev,
	struct scatterlist *sg, int nelems, enum dma_data_direction direction)
{
	int i;

	/* Make sure that gcc doesn't leave the empty loop body.  */
	for (i = 0; i < nelems; i++, sg++) {
		if (cpu_is_noncoherent_r10000(dev))
			__dma_sync((unsigned long)page_address(sg_page(sg)),
			           sg->length, direction);
	}
}

static void mips_dma_sync_sg_for_device(struct device *dev,
	struct scatterlist *sg, int nelems, enum dma_data_direction direction)
{
	int i;

	/* Make sure that gcc doesn't leave the empty loop body.  */
	for (i = 0; i < nelems; i++, sg++) {
		if (!plat_device_is_coherent(dev))
			__dma_sync((unsigned long)page_address(sg_page(sg)),
			           sg->length, direction);
	}
}

int mips_dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return plat_dma_mapping_error(dev, dma_addr);
}

int mips_dma_supported(struct device *dev, u64 mask)
{
	return plat_dma_supported(dev, mask);
}

void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
			 enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	plat_extra_sync_for_device(dev);
	if (!plat_device_is_coherent(dev))
		__dma_sync((unsigned long)vaddr, size, direction);
}

EXPORT_SYMBOL(dma_cache_sync);

static struct dma_map_ops mips_default_dma_map_ops = {
	.alloc_coherent = mips_dma_alloc_coherent,
	.free_coherent = mips_dma_free_coherent,
	.map_page = mips_dma_map_page,
	.unmap_page = mips_dma_unmap_page,
	.map_sg = mips_dma_map_sg,
	.unmap_sg = mips_dma_unmap_sg,
	.sync_single_for_cpu = mips_dma_sync_single_for_cpu,
	.sync_single_for_device = mips_dma_sync_single_for_device,
	.sync_sg_for_cpu = mips_dma_sync_sg_for_cpu,
	.sync_sg_for_device = mips_dma_sync_sg_for_device,
	.mapping_error = mips_dma_mapping_error,
	.dma_supported = mips_dma_supported
};

struct dma_map_ops *mips_dma_map_ops = &mips_default_dma_map_ops;
EXPORT_SYMBOL(mips_dma_map_ops);

#define PREALLOC_DMA_DEBUG_ENTRIES (1 << 16)

static int __init mips_dma_init(void)
{
	dma_debug_init(PREALLOC_DMA_DEBUG_ENTRIES);

	return 0;
}
fs_initcall(mips_dma_init);
