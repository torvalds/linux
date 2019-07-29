// SPDX-License-Identifier: GPL-2.0
/* Glue code to lib/swiotlb.c */

#include <linux/pci.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/swiotlb.h>
#include <linux/memblock.h>
#include <linux/dma-direct.h>
#include <linux/mem_encrypt.h>

#include <asm/iommu.h>
#include <asm/swiotlb.h>
#include <asm/dma.h>
#include <asm/xen/swiotlb-xen.h>
#include <asm/iommu_table.h>

int swiotlb __read_mostly;

/*
 * pci_swiotlb_detect_override - set swiotlb to 1 if necessary
 *
 * This returns non-zero if we are forced to use swiotlb (by the boot
 * option).
 */
int __init pci_swiotlb_detect_override(void)
{
	if (swiotlb_force == SWIOTLB_FORCE)
		swiotlb = 1;

	return swiotlb;
}
IOMMU_INIT_FINISH(pci_swiotlb_detect_override,
		  pci_xen_swiotlb_detect,
		  pci_swiotlb_init,
		  pci_swiotlb_late_init);

/*
 * If 4GB or more detected (and iommu=off not set) or if SME is active
 * then set swiotlb to 1 and return 1.
 */
int __init pci_swiotlb_detect_4gb(void)
{
	/* don't initialize swiotlb if iommu=off (no_iommu=1) */
	if (!no_iommu && max_possible_pfn > MAX_DMA32_PFN)
		swiotlb = 1;

	/*
	 * If SME is active then swiotlb will be set to 1 so that bounce
	 * buffers are allocated and used for devices that do not support
	 * the addressing range required for the encryption mask.
	 */
	if (sme_active())
		swiotlb = 1;

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
		swiotlb_exit();
	else {
		printk(KERN_INFO "PCI-DMA: "
		       "Using software bounce buffering for IO (SWIOTLB)\n");
		swiotlb_print_info();
	}
}
