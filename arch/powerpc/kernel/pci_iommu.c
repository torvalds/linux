/*
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 *
 * Rewrite, cleanup, new allocation schemes:
 * Copyright (C) 2004 Olof Johansson, IBM Corporation
 *
 * Dynamic DMA mapping support, platform-independent parts.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */


#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/iommu.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/ppc-pci.h>

/*
 * We can use ->sysdata directly and avoid the extra work in
 * pci_device_to_OF_node since ->sysdata will have been initialised
 * in the iommu init code for all devices.
 */
#define PCI_GET_DN(dev) ((struct device_node *)((dev)->sysdata))

static inline struct iommu_table *device_to_table(struct device *hwdev)
{
	struct pci_dev *pdev;

	if (!hwdev) {
		pdev = ppc64_isabridge_dev;
		if (!pdev)
			return NULL;
	} else
		pdev = to_pci_dev(hwdev);

	return PCI_DN(PCI_GET_DN(pdev))->iommu_table;
}


static inline unsigned long device_to_mask(struct device *hwdev)
{
	struct pci_dev *pdev;

	if (!hwdev) {
		pdev = ppc64_isabridge_dev;
		if (!pdev) /* This is the best guess we can do */
			return 0xfffffffful;
	} else
		pdev = to_pci_dev(hwdev);

	if (pdev->dma_mask)
		return pdev->dma_mask;

	/* Assume devices without mask can take 32 bit addresses */
	return 0xfffffffful;
}


/* Allocates a contiguous real buffer and creates mappings over it.
 * Returns the virtual address of the buffer and sets dma_handle
 * to the dma address (mapping) of the first page.
 */
static void *pci_iommu_alloc_coherent(struct device *hwdev, size_t size,
			   dma_addr_t *dma_handle, gfp_t flag)
{
	return iommu_alloc_coherent(device_to_table(hwdev), size, dma_handle,
			device_to_mask(hwdev), flag);
}

static void pci_iommu_free_coherent(struct device *hwdev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	iommu_free_coherent(device_to_table(hwdev), size, vaddr, dma_handle);
}

/* Creates TCEs for a user provided buffer.  The user buffer must be 
 * contiguous real kernel storage (not vmalloc).  The address of the buffer
 * passed here is the kernel (virtual) address of the buffer.  The buffer
 * need not be page aligned, the dma_addr_t returned will point to the same
 * byte within the page as vaddr.
 */
static dma_addr_t pci_iommu_map_single(struct device *hwdev, void *vaddr,
		size_t size, enum dma_data_direction direction)
{
	return iommu_map_single(device_to_table(hwdev), vaddr, size,
			        device_to_mask(hwdev), direction);
}


static void pci_iommu_unmap_single(struct device *hwdev, dma_addr_t dma_handle,
		size_t size, enum dma_data_direction direction)
{
	iommu_unmap_single(device_to_table(hwdev), dma_handle, size, direction);
}


static int pci_iommu_map_sg(struct device *pdev, struct scatterlist *sglist,
		int nelems, enum dma_data_direction direction)
{
	return iommu_map_sg(pdev, device_to_table(pdev), sglist,
			nelems, device_to_mask(pdev), direction);
}

static void pci_iommu_unmap_sg(struct device *pdev, struct scatterlist *sglist,
		int nelems, enum dma_data_direction direction)
{
	iommu_unmap_sg(device_to_table(pdev), sglist, nelems, direction);
}

/* We support DMA to/from any memory page via the iommu */
static int pci_iommu_dma_supported(struct device *dev, u64 mask)
{
	struct iommu_table *tbl = device_to_table(dev);

	if (!tbl || tbl->it_offset > mask) {
		printk(KERN_INFO "Warning: IOMMU table offset too big for device mask\n");
		if (tbl)
			printk(KERN_INFO "mask: 0x%08lx, table offset: 0x%08lx\n",
				mask, tbl->it_offset);
		else
			printk(KERN_INFO "mask: 0x%08lx, table unavailable\n",
				mask);
		return 0;
	} else
		return 1;
}

void pci_iommu_init(void)
{
	pci_dma_ops.alloc_coherent = pci_iommu_alloc_coherent;
	pci_dma_ops.free_coherent = pci_iommu_free_coherent;
	pci_dma_ops.map_single = pci_iommu_map_single;
	pci_dma_ops.unmap_single = pci_iommu_unmap_single;
	pci_dma_ops.map_sg = pci_iommu_map_sg;
	pci_dma_ops.unmap_sg = pci_iommu_unmap_sg;
	pci_dma_ops.dma_supported = pci_iommu_dma_supported;
}
