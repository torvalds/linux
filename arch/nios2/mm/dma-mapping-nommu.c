/*
 * Dynamic DMA mapping support.
 *
 * We never have any address translations to worry about, so this
 * is just alloc/free.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/export.h>
#include <linux/string.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>

void *dma_alloc_coherent(struct device *dev, size_t size,
			 dma_addr_t *dma_handle, gfp_t gfp)
{
	void *ret;
	/* ignore region specifiers */
	gfp &= ~(__GFP_DMA | __GFP_HIGHMEM);

	if (dev == NULL || (*dev->dma_mask < 0xffffffff))
		gfp |= GFP_DMA;
	ret = (void *)__get_free_pages(gfp, get_order(size));

	if (ret != NULL) {
		memset(ret, 0, size);
		*dma_handle = (dma_addr_t)ret;
		ret = ioremap((unsigned long)ret, size);
	}
	return ret;
}
EXPORT_SYMBOL(dma_alloc_coherent);

void dma_free_coherent(struct device *dev, size_t size,
			void *vaddr, dma_addr_t dma_handle)
{
	free_pages((unsigned long)dma_handle, get_order(size));
}
EXPORT_SYMBOL(dma_free_coherent);

void dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle,
				size_t size, enum dma_data_direction dir)
{
}
EXPORT_SYMBOL(dma_sync_single_for_cpu);

void dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle,
				size_t size, enum dma_data_direction dir)
{
	unsigned long addr;

	BUG_ON(dir == DMA_NONE);

	addr = dma_handle + PAGE_OFFSET;
	__dma_sync(addr, size, dir);
}
EXPORT_SYMBOL(dma_sync_single_for_device);

void dma_sync_single_range_for_cpu(struct device *dev, dma_addr_t dma_handle,
					unsigned long offset, size_t size,
					enum dma_data_direction dir)
{
}
EXPORT_SYMBOL(dma_sync_single_range_for_cpu);

void dma_sync_single_range_for_device(struct device *dev, dma_addr_t dma_handle,
					unsigned long offset, size_t size,
					enum dma_data_direction dir)
{
	unsigned long addr;

	BUG_ON(dir == DMA_NONE);

	addr = dma_handle + offset + PAGE_OFFSET;
	__dma_sync(addr, size, dir);
}
EXPORT_SYMBOL(dma_sync_single_range_for_device);

void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
			 int nents, enum dma_data_direction dir)
{
}
EXPORT_SYMBOL(dma_sync_sg_for_cpu);

void dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
				int nelems, enum dma_data_direction dir)
{
	int i;

	BUG_ON(dir == DMA_NONE);

	/* Make sure that gcc doesn't leave the empty loop body.  */
	for_each_sg(sg, sg, nelems, i) {
		__dma_sync((unsigned long)sg_virt(sg), sg->length, dir);
	}
}
EXPORT_SYMBOL(dma_sync_sg_for_device);

dma_addr_t dma_map_page(struct device *dev, struct page *page,
			unsigned long offset, size_t size,
			enum dma_data_direction dir)
{
	return dma_map_single(dev, page_address(page) + offset, size, dir);
}
EXPORT_SYMBOL(dma_map_page);

void dma_unmap_page(struct device *dev, dma_addr_t address,
			size_t size, enum dma_data_direction dir)
{
}
EXPORT_SYMBOL(dma_unmap_page);

int dma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction dir)
{
	int i;

	BUG_ON(dir == DMA_NONE);

	for (i = 0; i < nents; i++, sg++) {
		sg->dma_address = dma_map_single(dev, sg_virt(sg),
						 sg->length, dir);
	}

	return nents;
}
EXPORT_SYMBOL(dma_map_sg);

void dma_unmap_sg(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir)
{
	BUG_ON(dir == DMA_NONE);
}
EXPORT_SYMBOL(dma_unmap_sg);
