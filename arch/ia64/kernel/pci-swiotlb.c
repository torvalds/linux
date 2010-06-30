/* Glue code to lib/swiotlb.c */

#include <linux/pci.h>
#include <linux/gfp.h>
#include <linux/cache.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>

#include <asm/swiotlb.h>
#include <asm/dma.h>
#include <asm/iommu.h>
#include <asm/machvec.h>

int swiotlb __read_mostly;
EXPORT_SYMBOL(swiotlb);

static void *ia64_swiotlb_alloc_coherent(struct device *dev, size_t size,
					 dma_addr_t *dma_handle, gfp_t gfp)
{
	if (dev->coherent_dma_mask != DMA_BIT_MASK(64))
		gfp |= GFP_DMA;
	return swiotlb_alloc_coherent(dev, size, dma_handle, gfp);
}

struct dma_map_ops swiotlb_dma_ops = {
	.alloc_coherent = ia64_swiotlb_alloc_coherent,
	.free_coherent = swiotlb_free_coherent,
	.map_page = swiotlb_map_page,
	.unmap_page = swiotlb_unmap_page,
	.map_sg = swiotlb_map_sg_attrs,
	.unmap_sg = swiotlb_unmap_sg_attrs,
	.sync_single_for_cpu = swiotlb_sync_single_for_cpu,
	.sync_single_for_device = swiotlb_sync_single_for_device,
	.sync_sg_for_cpu = swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device = swiotlb_sync_sg_for_device,
	.dma_supported = swiotlb_dma_supported,
	.mapping_error = swiotlb_dma_mapping_error,
};

void __init swiotlb_dma_init(void)
{
	dma_ops = &swiotlb_dma_ops;
	swiotlb_init(1);
}

void __init pci_swiotlb_init(void)
{
	if (!iommu_detected) {
#ifdef CONFIG_IA64_GENERIC
		swiotlb = 1;
		printk(KERN_INFO "PCI-DMA: Re-initialize machine vector.\n");
		machvec_init("dig");
		swiotlb_init(1);
		dma_ops = &swiotlb_dma_ops;
#else
		panic("Unable to find Intel IOMMU");
#endif
	}
}
