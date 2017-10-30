/*
 * Dynamic DMA mapping support.
 *
 * On cris there is no hardware dynamic DMA address translation,
 * so consistent alloc/free are merely page allocation/freeing.
 * The rest of the dynamic DMA mapping interface is implemented
 * in asm/pci.h.
 *
 * Borrowed from i386.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/gfp.h>
#include <asm/io.h>

static void *v32_dma_alloc(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs)
{
	void *ret;

	/* ignore region specifiers */
	gfp &= ~(__GFP_DMA | __GFP_HIGHMEM);

	if (dev == NULL || (dev->coherent_dma_mask < 0xffffffff))
		gfp |= GFP_DMA;

	ret = (void *)__get_free_pages(gfp,  get_order(size));

	if (ret != NULL) {
		memset(ret, 0, size);
		*dma_handle = virt_to_phys(ret);
	}
	return ret;
}

static void v32_dma_free(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_handle, unsigned long attrs)
{
	free_pages((unsigned long)vaddr, get_order(size));
}

static inline dma_addr_t v32_dma_map_page(struct device *dev,
		struct page *page, unsigned long offset, size_t size,
		enum dma_data_direction direction, unsigned long attrs)
{
	return page_to_phys(page) + offset;
}

static inline int v32_dma_map_sg(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction direction,
		unsigned long attrs)
{
	printk("Map sg\n");
	return nents;
}

static inline int v32_dma_supported(struct device *dev, u64 mask)
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

const struct dma_map_ops v32_dma_ops = {
	.alloc			= v32_dma_alloc,
	.free			= v32_dma_free,
	.map_page		= v32_dma_map_page,
	.map_sg                 = v32_dma_map_sg,
	.dma_supported		= v32_dma_supported,
};
EXPORT_SYMBOL(v32_dma_ops);
