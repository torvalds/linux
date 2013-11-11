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
#include <asm/xen/swiotlb-xen.h>
#include <asm/iommu_table.h>
int swiotlb __read_mostly;

static void *x86_swiotlb_alloc_coherent(struct device *hwdev, size_t size,
					dma_addr_t *dma_handle, gfp_t flags,
					struct dma_attrs *attrs)
{
	void *vaddr;

	vaddr = dma_generic_alloc_coherent(hwdev, size, dma_handle, flags,
					   attrs);
	if (vaddr)
		return vaddr;

	return swiotlb_alloc_coherent(hwdev, size, dma_handle, flags);
}

static void x86_swiotlb_free_coherent(struct device *dev, size_t size,
				      void *vaddr, dma_addr_t dma_addr,
				      struct dma_attrs *attrs)
{
	swiotlb_free_coherent(dev, size, vaddr, dma_addr);
}

static struct dma_map_ops swiotlb_dma_ops = {
	.mapping_error = swiotlb_dma_mapping_error,
	.alloc = x86_swiotlb_alloc_coherent,
	.free = x86_swiotlb_free_coherent,
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
 * pci_swiotlb_detect_override - set swiotlb to 1 if necessary
 *
 * This returns non-zero if we are forced to use swiotlb (by the boot
 * option).
 */
int __init pci_swiotlb_detect_override(void)
{
	int use_swiotlb = swiotlb | swiotlb_force;

	if (swiotlb_force)
		swiotlb = 1;

	return use_swiotlb;
}
IOMMU_INIT_FINISH(pci_swiotlb_detect_override,
		  pci_xen_swiotlb_detect,
		  pci_swiotlb_init,
		  pci_swiotlb_late_init);

/*
 * if 4GB or more detected (and iommu=off not set) return 1
 * and set swiotlb to 1.
 */
int __init pci_swiotlb_detect_4gb(void)
{
	/* don't initialize swiotlb if iommu=off (no_iommu=1) */
#ifdef CONFIG_X86_64
	if (!no_iommu && max_pfn > MAX_DMA32_PFN)
		swiotlb = 1;
#endif
	return swiotlb;
}
IOMMU_INIT(pci_swiotlb_detect_4gb,
	   pci_swiotlb_detect_override,
	   pci_swiotlb_init,
	   pci_swiotlb_late_init);

void __init pci_swiotlb_init(void)
{
	if (swiotlb) {
		swiotlb_init(0);
		dma_ops = &swiotlb_dma_ops;
	}
}

void __init pci_swiotlb_late_init(void)
{
	/* An IOMMU turned us off. */
	if (!swiotlb)
		swiotlb_free();
	else {
		printk(KERN_INFO "PCI-DMA: "
		       "Using software bounce buffering for IO (SWIOTLB)\n");
		swiotlb_print_info();
	}
}
