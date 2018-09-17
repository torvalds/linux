// SPDX-License-Identifier: GPL-2.0
/*
 * Dynamic DMA mapping support.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/dmar.h>
#include <asm/iommu.h>
#include <asm/machvec.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <asm/page.h>

dma_addr_t bad_dma_address __read_mostly;
EXPORT_SYMBOL(bad_dma_address);

int no_iommu __read_mostly;
#ifdef CONFIG_IOMMU_DEBUG
int force_iommu __read_mostly = 1;
#else
int force_iommu __read_mostly;
#endif

int iommu_pass_through;

extern struct dma_map_ops intel_dma_ops;

static int __init pci_iommu_init(void)
{
	if (iommu_detected)
		intel_iommu_init();

	return 0;
}

/* Must execute after PCI subsystem */
fs_initcall(pci_iommu_init);

void pci_iommu_shutdown(void)
{
	return;
}

void __init
iommu_dma_init(void)
{
	return;
}

int iommu_dma_supported(struct device *dev, u64 mask)
{
	/* Copied from i386. Doesn't make much sense, because it will
	   only work for pci_alloc_coherent.
	   The caller just has to use GFP_DMA in this case. */
	if (mask < DMA_BIT_MASK(24))
		return 0;

	return 1;
}
EXPORT_SYMBOL(iommu_dma_supported);

void __init pci_iommu_alloc(void)
{
	dma_ops = &intel_dma_ops;

	intel_dma_ops.sync_single_for_cpu = machvec_dma_sync_single;
	intel_dma_ops.sync_sg_for_cpu = machvec_dma_sync_sg;
	intel_dma_ops.sync_single_for_device = machvec_dma_sync_single;
	intel_dma_ops.sync_sg_for_device = machvec_dma_sync_sg;
	intel_dma_ops.dma_supported = iommu_dma_supported;

	/*
	 * The order of these functions is important for
	 * fall-back/fail-over reasons
	 */
	detect_intel_iommu();

#ifdef CONFIG_SWIOTLB
	if (!iommu_detected) {
#ifdef CONFIG_IA64_GENERIC
		printk(KERN_INFO "PCI-DMA: Re-initialize machine vector.\n");
		machvec_init("dig");
		swiotlb_dma_init();
#else
		panic("Unable to find Intel IOMMU");
#endif /* CONFIG_IA64_GENERIC */
	}
#endif /* CONFIG_SWIOTLB */
}
