/* Glue code to lib/swiotlb.c */

#include <linux/pci.h>
#include <linux/cache.h>
#include <linux/module.h>
#include <linux/swiotlb.h>
#include <linux/bootmem.h>
#include <linux/dma-mapping.h>

#include <asm/iommu.h>
#include <asm/swiotlb.h>
#include <asm/dma.h>

int swiotlb __read_mostly;

static void *x86_swiotlb_alloc_coherent(struct device *hwdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flags)
{
	void *vaddr;

	vaddr = dma_generic_alloc_coherent(hwdev, size, dma_handle, flags);
	if (vaddr)
		return vaddr;

	return swiotlb_alloc_coherent(hwdev, size, dma_handle, flags);
}

static struct dma_map_ops swiotlb_dma_ops = {
	.mapping_error = swiotlb_dma_mapping_error,
	.alloc_coherent = x86_swiotlb_alloc_coherent,
	.free_coherent = swiotlb_free_coherent,
	.sync_single_for_cpu = swiotlb_sync_single_for_cpu,
	.sync_single_for_device = swiotlb_sync_single_for_device,
	.sync_single_range_for_cpu = swiotlb_sync_single_range_for_cpu,
	.sync_single_range_for_device = swiotlb_sync_single_range_for_device,
	.sync_sg_for_cpu = swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device = swiotlb_sync_sg_for_device,
	.map_sg = swiotlb_map_sg_attrs,
	.unmap_sg = swiotlb_unmap_sg_attrs,
	.map_page = swiotlb_map_page,
	.unmap_page = swiotlb_unmap_page,
	.dma_supported = NULL,
};

void __init pci_swiotlb_init(void)
{
	/* don't initialize swiotlb if iommu=off (no_iommu=1) */
#ifdef CONFIG_X86_64
	if ((!iommu_detected && !no_iommu && max_pfn > MAX_DMA32_PFN))
		swiotlb = 1;
#endif
	if (swiotlb_force)
		swiotlb = 1;
	if (swiotlb) {
		printk(KERN_INFO "PCI-DMA: Using software bounce buffering for IO (SWIOTLB)\n");
		swiotlb_init();
		dma_ops = &swiotlb_dma_ops;
	}
}
