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
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <asm/page.h>

int no_iommu __read_mostly;
#ifdef CONFIG_IOMMU_DEBUG
int force_iommu __read_mostly = 1;
#else
int force_iommu __read_mostly;
#endif

static int __init pci_iommu_init(void)
{
	if (iommu_detected)
		intel_iommu_init();

	return 0;
}

/* Must execute after PCI subsystem */
fs_initcall(pci_iommu_init);
