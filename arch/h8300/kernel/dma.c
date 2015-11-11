/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/scatterlist.h>
#include <linux/module.h>
#include <asm/pgalloc.h>

static void *dma_alloc(struct device *dev, size_t size,
		       dma_addr_t *dma_handle, gfp_t gfp,
		       struct dma_attrs *attrs)
{
	void *ret;

	/* ignore region specifiers */
	gfp &= ~(__GFP_DMA | __GFP_HIGHMEM);

	if (dev == NULL || (*dev->dma_mask < 0xffffffff))
		gfp |= GFP_DMA;
	ret = (void *)__get_free_pages(gfp, get_order(size));

	if (ret != NULL) {
		memset(ret, 0, size);
		*dma_handle = virt_to_phys(ret);
	}
	return ret;
}

static void dma_free(struct device *dev, size_t size,
		     void *vaddr, dma_addr_t dma_handle,
		     struct dma_attrs *attrs)

{
	free_pages((unsigned long)vaddr, get_order(size));
}

static dma_addr_t map_page(struct device *dev, struct page *page,
				  unsigned long offset, size_t size,
				  enum dma_data_direction direction,
				  struct dma_attrs *attrs)
{
	return page_to_phys(page) + offset;
}

static int map_sg(struct device *dev, struct scatterlist *sgl,
		  int nents, enum dma_data_direction direction,
		  struct dma_attrs *attrs)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i) {
		sg->dma_address = sg_phys(sg);
	}

	return nents;
}

struct dma_map_ops h8300_dma_map_ops = {
	.alloc = dma_alloc,
	.free = dma_free,
	.map_page = map_page,
	.map_sg = map_sg,
};
EXPORT_SYMBOL(h8300_dma_map_ops);
