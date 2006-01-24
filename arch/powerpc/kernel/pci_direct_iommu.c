/*
 * Support for DMA from PCI devices to main memory on
 * machines without an iommu or with directly addressable
 * RAM (typically a pmac with 2Gb of RAM or less)
 *
 * Copyright (C) 2003 Benjamin Herrenschmidt (benh@kernel.crashing.org)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>

#include <asm/sections.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <asm/abs_addr.h>
#include <asm/ppc-pci.h>

static void *pci_direct_alloc_coherent(struct device *hwdev, size_t size,
				   dma_addr_t *dma_handle, gfp_t flag)
{
	void *ret;

	ret = (void *)__get_free_pages(flag, get_order(size));
	if (ret != NULL) {
		memset(ret, 0, size);
		*dma_handle = virt_to_abs(ret);
	}
	return ret;
}

static void pci_direct_free_coherent(struct device *hwdev, size_t size,
				 void *vaddr, dma_addr_t dma_handle)
{
	free_pages((unsigned long)vaddr, get_order(size));
}

static dma_addr_t pci_direct_map_single(struct device *hwdev, void *ptr,
		size_t size, enum dma_data_direction direction)
{
	return virt_to_abs(ptr);
}

static void pci_direct_unmap_single(struct device *hwdev, dma_addr_t dma_addr,
		size_t size, enum dma_data_direction direction)
{
}

static int pci_direct_map_sg(struct device *hwdev, struct scatterlist *sg,
		int nents, enum dma_data_direction direction)
{
	int i;

	for (i = 0; i < nents; i++, sg++) {
		sg->dma_address = page_to_phys(sg->page) + sg->offset;
		sg->dma_length = sg->length;
	}

	return nents;
}

static void pci_direct_unmap_sg(struct device *hwdev, struct scatterlist *sg,
		int nents, enum dma_data_direction direction)
{
}

static int pci_direct_dma_supported(struct device *dev, u64 mask)
{
	return mask < 0x100000000ull;
}

void __init pci_direct_iommu_init(void)
{
	pci_dma_ops.alloc_coherent = pci_direct_alloc_coherent;
	pci_dma_ops.free_coherent = pci_direct_free_coherent;
	pci_dma_ops.map_single = pci_direct_map_single;
	pci_dma_ops.unmap_single = pci_direct_unmap_single;
	pci_dma_ops.map_sg = pci_direct_map_sg;
	pci_dma_ops.unmap_sg = pci_direct_unmap_sg;
	pci_dma_ops.dma_supported = pci_direct_dma_supported;
}
