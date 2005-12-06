/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000,2002-2005 Silicon Graphics, Inc. All rights reserved.
 *
 * Routines for PCI DMA mapping.  See Documentation/DMA-API.txt for
 * a description of how these routines should be used.
 */

#include <linux/module.h>
#include <asm/dma.h>
#include <asm/sn/pcibr_provider.h>
#include <asm/sn/pcibus_provider_defs.h>
#include <asm/sn/pcidev.h>
#include <asm/sn/sn_sal.h>

#define SG_ENT_VIRT_ADDRESS(sg)	(page_address((sg)->page) + (sg)->offset)
#define SG_ENT_PHYS_ADDRESS(SG)	virt_to_phys(SG_ENT_VIRT_ADDRESS(SG))

/**
 * sn_dma_supported - test a DMA mask
 * @dev: device to test
 * @mask: DMA mask to test
 *
 * Return whether the given PCI device DMA address mask can be supported
 * properly.  For example, if your device can only drive the low 24-bits
 * during PCI bus mastering, then you would pass 0x00ffffff as the mask to
 * this function.  Of course, SN only supports devices that have 32 or more
 * address bits when using the PMU.
 */
int sn_dma_supported(struct device *dev, u64 mask)
{
	BUG_ON(dev->bus != &pci_bus_type);

	if (mask < 0x7fffffff)
		return 0;
	return 1;
}
EXPORT_SYMBOL(sn_dma_supported);

/**
 * sn_dma_set_mask - set the DMA mask
 * @dev: device to set
 * @dma_mask: new mask
 *
 * Set @dev's DMA mask if the hw supports it.
 */
int sn_dma_set_mask(struct device *dev, u64 dma_mask)
{
	BUG_ON(dev->bus != &pci_bus_type);

	if (!sn_dma_supported(dev, dma_mask))
		return 0;

	*dev->dma_mask = dma_mask;
	return 1;
}
EXPORT_SYMBOL(sn_dma_set_mask);

/**
 * sn_dma_alloc_coherent - allocate memory for coherent DMA
 * @dev: device to allocate for
 * @size: size of the region
 * @dma_handle: DMA (bus) address
 * @flags: memory allocation flags
 *
 * dma_alloc_coherent() returns a pointer to a memory region suitable for
 * coherent DMA traffic to/from a PCI device.  On SN platforms, this means
 * that @dma_handle will have the %PCIIO_DMA_CMD flag set.
 *
 * This interface is usually used for "command" streams (e.g. the command
 * queue for a SCSI controller).  See Documentation/DMA-API.txt for
 * more information.
 */
void *sn_dma_alloc_coherent(struct device *dev, size_t size,
			    dma_addr_t * dma_handle, gfp_t flags)
{
	void *cpuaddr;
	unsigned long phys_addr;
	int node;
	struct pci_dev *pdev = to_pci_dev(dev);
	struct sn_pcibus_provider *provider = SN_PCIDEV_BUSPROVIDER(pdev);

	BUG_ON(dev->bus != &pci_bus_type);

	/*
	 * Allocate the memory.
	 */
	node = pcibus_to_node(pdev->bus);
	if (likely(node >=0)) {
		struct page *p = alloc_pages_node(node, GFP_ATOMIC, get_order(size));

		if (likely(p))
			cpuaddr = page_address(p);
		else
			return NULL;
	} else
		cpuaddr = (void *)__get_free_pages(GFP_ATOMIC, get_order(size));

	if (unlikely(!cpuaddr))
		return NULL;

	memset(cpuaddr, 0x0, size);

	/* physical addr. of the memory we just got */
	phys_addr = __pa(cpuaddr);

	/*
	 * 64 bit address translations should never fail.
	 * 32 bit translations can fail if there are insufficient mapping
	 * resources.
	 */

	*dma_handle = provider->dma_map_consistent(pdev, phys_addr, size);
	if (!*dma_handle) {
		printk(KERN_ERR "%s: out of ATEs\n", __FUNCTION__);
		free_pages((unsigned long)cpuaddr, get_order(size));
		return NULL;
	}

	return cpuaddr;
}
EXPORT_SYMBOL(sn_dma_alloc_coherent);

/**
 * sn_pci_free_coherent - free memory associated with coherent DMAable region
 * @dev: device to free for
 * @size: size to free
 * @cpu_addr: kernel virtual address to free
 * @dma_handle: DMA address associated with this region
 *
 * Frees the memory allocated by dma_alloc_coherent(), potentially unmapping
 * any associated IOMMU mappings.
 */
void sn_dma_free_coherent(struct device *dev, size_t size, void *cpu_addr,
			  dma_addr_t dma_handle)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct sn_pcibus_provider *provider = SN_PCIDEV_BUSPROVIDER(pdev);

	BUG_ON(dev->bus != &pci_bus_type);

	provider->dma_unmap(pdev, dma_handle, 0);
	free_pages((unsigned long)cpu_addr, get_order(size));
}
EXPORT_SYMBOL(sn_dma_free_coherent);

/**
 * sn_dma_map_single - map a single page for DMA
 * @dev: device to map for
 * @cpu_addr: kernel virtual address of the region to map
 * @size: size of the region
 * @direction: DMA direction
 *
 * Map the region pointed to by @cpu_addr for DMA and return the
 * DMA address.
 *
 * We map this to the one step pcibr_dmamap_trans interface rather than
 * the two step pcibr_dmamap_alloc/pcibr_dmamap_addr because we have
 * no way of saving the dmamap handle from the alloc to later free
 * (which is pretty much unacceptable).
 *
 * TODO: simplify our interface;
 *       figure out how to save dmamap handle so can use two step.
 */
dma_addr_t sn_dma_map_single(struct device *dev, void *cpu_addr, size_t size,
			     int direction)
{
	dma_addr_t dma_addr;
	unsigned long phys_addr;
	struct pci_dev *pdev = to_pci_dev(dev);
	struct sn_pcibus_provider *provider = SN_PCIDEV_BUSPROVIDER(pdev);

	BUG_ON(dev->bus != &pci_bus_type);

	phys_addr = __pa(cpu_addr);
	dma_addr = provider->dma_map(pdev, phys_addr, size);
	if (!dma_addr) {
		printk(KERN_ERR "%s: out of ATEs\n", __FUNCTION__);
		return 0;
	}
	return dma_addr;
}
EXPORT_SYMBOL(sn_dma_map_single);

/**
 * sn_dma_unmap_single - unamp a DMA mapped page
 * @dev: device to sync
 * @dma_addr: DMA address to sync
 * @size: size of region
 * @direction: DMA direction
 *
 * This routine is supposed to sync the DMA region specified
 * by @dma_handle into the coherence domain.  On SN, we're always cache
 * coherent, so we just need to free any ATEs associated with this mapping.
 */
void sn_dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
			 int direction)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct sn_pcibus_provider *provider = SN_PCIDEV_BUSPROVIDER(pdev);

	BUG_ON(dev->bus != &pci_bus_type);

	provider->dma_unmap(pdev, dma_addr, direction);
}
EXPORT_SYMBOL(sn_dma_unmap_single);

/**
 * sn_dma_unmap_sg - unmap a DMA scatterlist
 * @dev: device to unmap
 * @sg: scatterlist to unmap
 * @nhwentries: number of scatterlist entries
 * @direction: DMA direction
 *
 * Unmap a set of streaming mode DMA translations.
 */
void sn_dma_unmap_sg(struct device *dev, struct scatterlist *sg,
		     int nhwentries, int direction)
{
	int i;
	struct pci_dev *pdev = to_pci_dev(dev);
	struct sn_pcibus_provider *provider = SN_PCIDEV_BUSPROVIDER(pdev);

	BUG_ON(dev->bus != &pci_bus_type);

	for (i = 0; i < nhwentries; i++, sg++) {
		provider->dma_unmap(pdev, sg->dma_address, direction);
		sg->dma_address = (dma_addr_t) NULL;
		sg->dma_length = 0;
	}
}
EXPORT_SYMBOL(sn_dma_unmap_sg);

/**
 * sn_dma_map_sg - map a scatterlist for DMA
 * @dev: device to map for
 * @sg: scatterlist to map
 * @nhwentries: number of entries
 * @direction: direction of the DMA transaction
 *
 * Maps each entry of @sg for DMA.
 */
int sn_dma_map_sg(struct device *dev, struct scatterlist *sg, int nhwentries,
		  int direction)
{
	unsigned long phys_addr;
	struct scatterlist *saved_sg = sg;
	struct pci_dev *pdev = to_pci_dev(dev);
	struct sn_pcibus_provider *provider = SN_PCIDEV_BUSPROVIDER(pdev);
	int i;

	BUG_ON(dev->bus != &pci_bus_type);

	/*
	 * Setup a DMA address for each entry in the scatterlist.
	 */
	for (i = 0; i < nhwentries; i++, sg++) {
		phys_addr = SG_ENT_PHYS_ADDRESS(sg);
		sg->dma_address = provider->dma_map(pdev,
						    phys_addr, sg->length);

		if (!sg->dma_address) {
			printk(KERN_ERR "%s: out of ATEs\n", __FUNCTION__);

			/*
			 * Free any successfully allocated entries.
			 */
			if (i > 0)
				sn_dma_unmap_sg(dev, saved_sg, i, direction);
			return 0;
		}

		sg->dma_length = sg->length;
	}

	return nhwentries;
}
EXPORT_SYMBOL(sn_dma_map_sg);

void sn_dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle,
				size_t size, int direction)
{
	BUG_ON(dev->bus != &pci_bus_type);
}
EXPORT_SYMBOL(sn_dma_sync_single_for_cpu);

void sn_dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle,
				   size_t size, int direction)
{
	BUG_ON(dev->bus != &pci_bus_type);
}
EXPORT_SYMBOL(sn_dma_sync_single_for_device);

void sn_dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
			    int nelems, int direction)
{
	BUG_ON(dev->bus != &pci_bus_type);
}
EXPORT_SYMBOL(sn_dma_sync_sg_for_cpu);

void sn_dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
			       int nelems, int direction)
{
	BUG_ON(dev->bus != &pci_bus_type);
}
EXPORT_SYMBOL(sn_dma_sync_sg_for_device);

int sn_dma_mapping_error(dma_addr_t dma_addr)
{
	return 0;
}
EXPORT_SYMBOL(sn_dma_mapping_error);

char *sn_pci_get_legacy_mem(struct pci_bus *bus)
{
	if (!SN_PCIBUS_BUSSOFT(bus))
		return ERR_PTR(-ENODEV);

	return (char *)(SN_PCIBUS_BUSSOFT(bus)->bs_legacy_mem | __IA64_UNCACHED_OFFSET);
}

int sn_pci_legacy_read(struct pci_bus *bus, u16 port, u32 *val, u8 size)
{
	unsigned long addr;
	int ret;
	struct ia64_sal_retval isrv;

	/*
	 * First, try the SN_SAL_IOIF_PCI_SAFE SAL call which can work
	 * around hw issues at the pci bus level.  SGI proms older than
	 * 4.10 don't implment this.
	 */

	SAL_CALL(isrv, SN_SAL_IOIF_PCI_SAFE,
		pci_domain_nr(bus), bus->number,
		0, /* io */
		0, /* read */
		port, size, __pa(val));

	if (isrv.status == 0)
		return size;

	/*
	 * If the above failed, retry using the SAL_PROBE call which should
	 * be present in all proms (but which cannot work round PCI chipset
	 * bugs).  This code is retained for compatability with old
	 * pre-4.10 proms, and should be removed at some point in the future.
	 */

	if (!SN_PCIBUS_BUSSOFT(bus))
		return -ENODEV;

	addr = SN_PCIBUS_BUSSOFT(bus)->bs_legacy_io | __IA64_UNCACHED_OFFSET;
	addr += port;

	ret = ia64_sn_probe_mem(addr, (long)size, (void *)val);

	if (ret == 2)
		return -EINVAL;

	if (ret == 1)
		*val = -1;

	return size;
}

int sn_pci_legacy_write(struct pci_bus *bus, u16 port, u32 val, u8 size)
{
	int ret = size;
	unsigned long paddr;
	unsigned long *addr;
	struct ia64_sal_retval isrv;

	/*
	 * First, try the SN_SAL_IOIF_PCI_SAFE SAL call which can work
	 * around hw issues at the pci bus level.  SGI proms older than
	 * 4.10 don't implment this.
	 */

	SAL_CALL(isrv, SN_SAL_IOIF_PCI_SAFE,
		pci_domain_nr(bus), bus->number,
		0, /* io */
		1, /* write */
		port, size, __pa(&val));

	if (isrv.status == 0)
		return size;

	/*
	 * If the above failed, retry using the SAL_PROBE call which should
	 * be present in all proms (but which cannot work round PCI chipset
	 * bugs).  This code is retained for compatability with old
	 * pre-4.10 proms, and should be removed at some point in the future.
	 */

	if (!SN_PCIBUS_BUSSOFT(bus)) {
		ret = -ENODEV;
		goto out;
	}

	/* Put the phys addr in uncached space */
	paddr = SN_PCIBUS_BUSSOFT(bus)->bs_legacy_io | __IA64_UNCACHED_OFFSET;
	paddr += port;
	addr = (unsigned long *)paddr;

	switch (size) {
	case 1:
		*(volatile u8 *)(addr) = (u8)(val);
		break;
	case 2:
		*(volatile u16 *)(addr) = (u16)(val);
		break;
	case 4:
		*(volatile u32 *)(addr) = (u32)(val);
		break;
	default:
		ret = -EINVAL;
		break;
	}
 out:
	return ret;
}
