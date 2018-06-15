// SPDX-License-Identifier: GPL-2.0
#include <linux/dma-mapping.h>
#include <linux/swiotlb.h>

static void *mips_swiotlb_alloc(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs)
{
	void *ret = swiotlb_alloc(dev, size, dma_handle, gfp, attrs);

	mb();
	return ret;
}

static dma_addr_t mips_swiotlb_map_page(struct device *dev,
		struct page *page, unsigned long offset, size_t size,
		enum dma_data_direction dir, unsigned long attrs)
{
	dma_addr_t daddr = swiotlb_map_page(dev, page, offset, size,
					dir, attrs);
	mb();
	return daddr;
}

static int mips_swiotlb_map_sg(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	int r = swiotlb_map_sg_attrs(dev, sg, nents, dir, attrs);
	mb();

	return r;
}

static void mips_swiotlb_sync_single_for_device(struct device *dev,
		dma_addr_t dma_handle, size_t size, enum dma_data_direction dir)
{
	swiotlb_sync_single_for_device(dev, dma_handle, size, dir);
	mb();
}

static void mips_swiotlb_sync_sg_for_device(struct device *dev,
		struct scatterlist *sg, int nents, enum dma_data_direction dir)
{
	swiotlb_sync_sg_for_device(dev, sg, nents, dir);
	mb();
}

const struct dma_map_ops mips_swiotlb_ops = {
	.alloc			= mips_swiotlb_alloc,
	.free			= swiotlb_free,
	.map_page		= mips_swiotlb_map_page,
	.unmap_page		= swiotlb_unmap_page,
	.map_sg			= mips_swiotlb_map_sg,
	.unmap_sg		= swiotlb_unmap_sg_attrs,
	.sync_single_for_cpu	= swiotlb_sync_single_for_cpu,
	.sync_single_for_device	= mips_swiotlb_sync_single_for_device,
	.sync_sg_for_cpu	= swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device	= mips_swiotlb_sync_sg_for_device,
	.mapping_error		= swiotlb_dma_mapping_error,
	.dma_supported		= swiotlb_dma_supported,
};
EXPORT_SYMBOL(mips_swiotlb_ops);
