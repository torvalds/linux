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
	.sync_sg_for_cpu = swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device = swiotlb_sync_sg_for_device,
	.map_sg = swiotlb_map_sg_attrs,
	.unmap_sg = swiotlb_unmap_sg_attrs,
	.map_page = swiotlb_map_page,
	.unmap_page = swiotlb_unmap_page,
	.dma_supported = NULL,
};

/*
 * pci_swiotlb_detect - set swiotlb to 1 if necessary
 *
 * This returns non-zero if we are forced to use swiotlb (by the boot
 * option).
 */
int __init pci_swiotlb_detect(void)
{
	int use_swiotlb = swiotlb | swiotlb_force;

	/* don't initialize swiotlb if iommu=off (no_iommu=1) */
#ifdef CONFIG_X86_64
	if (!no_iommu && max_pfn > MAX_DMA32_PFN)
		swiotlb = 1;
#endif
	if (swiotlb_force)
		swiotlb = 1;

	return use_swiotlb;
}

void __init pci_swiotlb_init(void)
{
	if (swiotlb) {
		swiotlb_init(0);
		dma_ops = &swiotlb_dma_ops;
	}
}
