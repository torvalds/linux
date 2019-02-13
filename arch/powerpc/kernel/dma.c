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

static u64 __maybe_unused get_pfn_limit(struct device *dev)
{
	u64 pfn = (dev->coherent_dma_mask >> PAGE_SHIFT) + 1;

#ifdef CONFIG_SWIOTLB
	if (dev->bus_dma_mask && dev->dma_ops == &powerpc_swiotlb_dma_ops)
		pfn = min_t(u64, pfn, dev->bus_dma_mask >> PAGE_SHIFT);
#endif

	return pfn;
}

int dma_nommu_dma_supported(struct device *dev, u64 mask)
{
#ifdef CONFIG_PPC64
	u64 limit = phys_to_dma(dev, (memblock_end_of_DRAM() - 1));

	/* Limit fits in the mask, we are good */
	if (mask >= limit)
		return 1;

#ifdef CONFIG_FSL_SOC
	/*
	 * Freescale gets another chance via ZONE_DMA, however
	 * that will have to be refined if/when they support iommus
	 */
	return 1;
#endif
	/* Sorry ... */
	return 0;
#else
	return 1;
#endif
}

#ifndef CONFIG_NOT_COHERENT_CACHE
void *__dma_nommu_alloc_coherent(struct device *dev, size_t size,
				  dma_addr_t *dma_handle, gfp_t flag,
				  unsigned long attrs)
{
	void *ret;
	struct page *page;
	int node = dev_to_node(dev);
#ifdef CONFIG_FSL_SOC
	u64 pfn = get_pfn_limit(dev);
	int zone;

	/*
	 * This code should be OK on other platforms, but we have drivers that
	 * don't set coherent_dma_mask. As a workaround we just ifdef it. This
	 * whole routine needs some serious cleanup.
	 */

	zone = dma_pfn_limit_to_zone(pfn);
	if (zone < 0) {
		dev_err(dev, "%s: No suitable zone for pfn %#llx\n",
			__func__, pfn);
		return NULL;
	}

	switch (zone) {
#ifdef CONFIG_ZONE_DMA
	case ZONE_DMA:
		flag |= GFP_DMA;
		break;
#endif
	};
#endif /* CONFIG_FSL_SOC */

	page = alloc_pages_node(node, flag, get_order(size));
	if (page == NULL)
		return NULL;
	ret = page_address(page);
	memset(ret, 0, size);
	*dma_handle = phys_to_dma(dev,__pa(ret));

	return ret;
}

void __dma_nommu_free_coherent(struct device *dev, size_t size,
				void *vaddr, dma_addr_t dma_handle,
				unsigned long attrs)
{
	free_pages((unsigned long)vaddr, get_order(size));
}
#endif /* !CONFIG_NOT_COHERENT_CACHE */

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
	.alloc				= __dma_nommu_alloc_coherent,
	.free				= __dma_nommu_free_coherent,
	.map_sg				= dma_nommu_map_sg,
	.unmap_sg			= dma_nommu_unmap_sg,
	.dma_supported			= dma_nommu_dma_supported,
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

