/*
 * Copyright (C) 2006 Benjamin Herrenschmidt, IBM Corporation
 *
 * Provide default implementations of the DMA mapping callbacks for
 * directly mapped busses.
 */

#include <linux/device.h>
#include <linux/dma-direct.h>
#include <linux/dma-debug.h>
#include <linux/gfp.h>
#include <linux/memblock.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <asm/vio.h>
#include <asm/bug.h>
#include <asm/machdep.h>
#include <asm/swiotlb.h>
#include <asm/iommu.h>

/*
 * Generic direct DMA implementation
 *
 * This implementation supports a per-device offset that can be applied if
 * the address at which memory is visible to devices is not 0. Platform code
 * can set archdata.dma_data to an unsigned long holding the offset. By
 * default the offset is PCI_DRAM_OFFSET.
 */

int dma_nommu_map_sg(struct device *dev, struct scatterlist *sgl,
		int nents, enum dma_data_direction direction,
		unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i) {
		sg->dma_address = phys_to_dma(dev, sg_phys(sg));
		sg->dma_length = sg->length;

		if (attrs & DMA_ATTR_SKIP_CPU_SYNC)
			continue;

		__dma_sync_page(sg_page(sg), sg->offset, sg->length, direction);
	}

	return nents;
}

static void dma_nommu_unmap_sg(struct device *dev, struct scatterlist *sgl,
				int nents, enum dma_data_direction direction,
				unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i)
		__dma_sync_page(sg_page(sg), sg->offset, sg->length, direction);
}

dma_addr_t dma_nommu_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size,
		enum dma_data_direction dir, unsigned long attrs)
{
	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		__dma_sync_page(page, offset, size, dir);

	return phys_to_dma(dev, page_to_phys(page)) + offset;
}

static inline void dma_nommu_unmap_page(struct device *dev,
					 dma_addr_t dma_address,
					 size_t size,
					 enum dma_data_direction direction,
					 unsigned long attrs)
{
	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		__dma_sync(bus_to_virt(dma_address), size, direction);
}

#ifdef CONFIG_NOT_COHERENT_CACHE
static inline void dma_nommu_sync_sg(struct device *dev,
		struct scatterlist *sgl, int nents,
		enum dma_data_direction direction)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i)
		__dma_sync_page(sg_page(sg), sg->offset, sg->length, direction);
}

static inline void dma_nommu_sync_single(struct device *dev,
					  dma_addr_t dma_handle, size_t size,
					  enum dma_data_direction direction)
{
	__dma_sync(bus_to_virt(dma_handle), size, direction);
}
#endif

const struct dma_map_ops dma_nommu_ops = {
#ifdef CONFIG_NOT_COHERENT_CACHE
	.alloc				= __dma_nommu_alloc_coherent,
	.free				= __dma_nommu_free_coherent,
#else
	.alloc				= dma_direct_alloc,
	.free				= dma_direct_free,
#endif
	.map_sg				= dma_nommu_map_sg,
	.unmap_sg			= dma_nommu_unmap_sg,
	.dma_supported			= dma_direct_supported,
	.map_page			= dma_nommu_map_page,
	.unmap_page			= dma_nommu_unmap_page,
	.get_required_mask		= dma_direct_get_required_mask,
#ifdef CONFIG_NOT_COHERENT_CACHE
	.sync_single_for_cpu 		= dma_nommu_sync_single,
	.sync_single_for_device 	= dma_nommu_sync_single,
	.sync_sg_for_cpu 		= dma_nommu_sync_sg,
	.sync_sg_for_device 		= dma_nommu_sync_sg,
#endif
};
EXPORT_SYMBOL(dma_nommu_ops);

static int __init dma_init(void)
{
#ifdef CONFIG_IBMVIO
	dma_debug_add_bus(&vio_bus_type);
#endif

       return 0;
}
fs_initcall(dma_init);

