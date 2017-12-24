// SPDX-License-Identifier: GPL-2.0
/* Glue code to lib/swiotlb.c */

#include <linux/pci.h>
#include <linux/gfp.h>
#include <linux/cache.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/swiotlb.h>
#include <asm/dma.h>
#include <asm/iommu.h>
#include <asm/machvec.h>

int swiotlb __read_mostly;
EXPORT_SYMBOL(swiotlb);

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
