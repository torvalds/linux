/*
 * DMA implementation for Hexagon
 *
 * Copyright (c) 2010-2012, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/dma-mapping.h>
#include <linux/bootmem.h>
#include <linux/genalloc.h>
#include <asm/dma-mapping.h>
#include <linux/module.h>
#include <asm/page.h>

struct dma_map_ops *dma_ops;
EXPORT_SYMBOL(dma_ops);

int bad_dma_address;  /*  globals are automatically initialized to zero  */

static inline void *dma_addr_to_virt(dma_addr_t dma_addr)
{
	return phys_to_virt((unsigned long) dma_addr);
}

int dma_supported(struct device *dev, u64 mask)
{
	if (mask == DMA_BIT_MASK(32))
		return 1;
	else
		return 0;
}
EXPORT_SYMBOL(dma_supported);

int dma_set_mask(struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	*dev->dma_mask = mask;

	return 0;
}
EXPORT_SYMBOL(dma_set_mask);

static struct gen_pool *coherent_pool;


/* Allocates from a pool of uncached memory that was reserved at boot time */

static void *hexagon_dma_alloc_coherent(struct device *dev, size_t size,
				 dma_addr_t *dma_addr, gfp_t flag,
				 struct dma_attrs *attrs)
{
	void *ret;

	/*
	 * Our max_low_pfn should have been backed off by 16MB in
	 * mm/init.c to create DMA coherent space.  Use that as the VA
	 * for the pool.
	 */

	if (coherent_pool == NULL) {
		coherent_pool = gen_pool_create(PAGE_SHIFT, -1);

		if (coherent_pool == NULL)
			panic("Can't create %s() memory pool!", __func__);
		else
			gen_pool_add(coherent_pool,
				pfn_to_virt(max_low_pfn),
				hexagon_coherent_pool_size, -1);
	}

	ret = (void *) gen_pool_alloc(coherent_pool, size);

	if (ret) {
		memset(ret, 0, size);
		*dma_addr = (dma_addr_t) virt_to_phys(ret);
	} else
		*dma_addr = ~0;

	return ret;
}

static void hexagon_free_coherent(struct device *dev, size_t size, void *vaddr,
				  dma_addr_t dma_addr, struct dma_attrs *attrs)
{
	gen_pool_free(coherent_pool, (unsigned long) vaddr, size);
}

static int check_addr(const char *name, struct device *hwdev,
		      dma_addr_t bus, size_t size)
{
	if (hwdev && hwdev->dma_mask && !dma_capable(hwdev, bus, size)) {
		if (*hwdev->dma_mask >= DMA_BIT_MASK(32))
			printk(KERN_ERR
				"%s: overflow %Lx+%zu of device mask %Lx\n",
				name, (long long)bus, size,
				(long long)*hwdev->dma_mask);
		return 0;
	}
	return 1;
}

static int hexagon_map_sg(struct device *hwdev, struct scatterlist *sg,
			  int nents, enum dma_data_direction dir,
			  struct dma_attrs *attrs)
{
	struct scatterlist *s;
	int i;

	WARN_ON(nents == 0 || sg[0].length == 0);

	for_each_sg(sg, s, nents, i) {
		s->dma_address = sg_phys(s);
		if (!check_addr("map_sg", hwdev, s->dma_address, s->length))
			return 0;

		s->dma_length = s->length;

		flush_dcache_range(dma_addr_to_virt(s->dma_address),
				   dma_addr_to_virt(s->dma_address + s->length));
	}

	return nents;
}

/*
 * address is virtual
 */
static inline void dma_sync(void *addr, size_t size,
			    enum dma_data_direction dir)
{
	switch (dir) {
	case DMA_TO_DEVICE:
		hexagon_clean_dcache_range((unsigned long) addr,
		(unsigned long) addr + size);
		break;
	case DMA_FROM_DEVICE:
		hexagon_inv_dcache_range((unsigned long) addr,
		(unsigned long) addr + size);
		break;
	case DMA_BIDIRECTIONAL:
		flush_dcache_range((unsigned long) addr,
		(unsigned long) addr + size);
		break;
	default:
		BUG();
	}
}

/**
 * hexagon_map_page() - maps an address for device DMA
 * @dev:	pointer to DMA device
 * @page:	pointer to page struct of DMA memory
 * @offset:	offset within page
 * @size:	size of memory to map
 * @dir:	transfer direction
 * @attrs:	pointer to DMA attrs (not used)
 *
 * Called to map a memory address to a DMA address prior
 * to accesses to/from device.
 *
 * We don't particularly have many hoops to jump through
 * so far.  Straight translation between phys and virtual.
 *
 * DMA is not cache coherent so sync is necessary; this
 * seems to be a convenient place to do it.
 *
 */
static dma_addr_t hexagon_map_page(struct device *dev, struct page *page,
				   unsigned long offset, size_t size,
				   enum dma_data_direction dir,
				   struct dma_attrs *attrs)
{
	dma_addr_t bus = page_to_phys(page) + offset;
	WARN_ON(size == 0);

	if (!check_addr("map_single", dev, bus, size))
		return bad_dma_address;

	dma_sync(dma_addr_to_virt(bus), size, dir);

	return bus;
}

static void hexagon_sync_single_for_cpu(struct device *dev,
					dma_addr_t dma_handle, size_t size,
					enum dma_data_direction dir)
{
	dma_sync(dma_addr_to_virt(dma_handle), size, dir);
}

static void hexagon_sync_single_for_device(struct device *dev,
					dma_addr_t dma_handle, size_t size,
					enum dma_data_direction dir)
{
	dma_sync(dma_addr_to_virt(dma_handle), size, dir);
}

struct dma_map_ops hexagon_dma_ops = {
	.alloc		= hexagon_dma_alloc_coherent,
	.free		= hexagon_free_coherent,
	.map_sg		= hexagon_map_sg,
	.map_page	= hexagon_map_page,
	.sync_single_for_cpu = hexagon_sync_single_for_cpu,
	.sync_single_for_device = hexagon_sync_single_for_device,
	.is_phys	= 1,
};

void __init hexagon_dma_init(void)
{
	if (dma_ops)
		return;

	dma_ops = &hexagon_dma_ops;
}
