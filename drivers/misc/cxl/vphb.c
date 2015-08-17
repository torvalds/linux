/*
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/pci.h>
#include <misc/cxl.h>
#include "cxl.h"

static int cxl_dma_set_mask(struct pci_dev *pdev, u64 dma_mask)
{
	if (dma_mask < DMA_BIT_MASK(64)) {
		pr_info("%s only 64bit DMA supported on CXL", __func__);
		return -EIO;
	}

	*(pdev->dev.dma_mask) = dma_mask;
	return 0;
}

static int cxl_pci_probe_mode(struct pci_bus *bus)
{
	return PCI_PROBE_NORMAL;
}

static int cxl_setup_msi_irqs(struct pci_dev *pdev, int nvec, int type)
{
	return -ENODEV;
}

static void cxl_teardown_msi_irqs(struct pci_dev *pdev)
{
	/*
	 * MSI should never be set but need still need to provide this call
	 * back.
	 */
}

static bool cxl_pci_enable_device_hook(struct pci_dev *dev)
{
	struct pci_controller *phb;
	struct cxl_afu *afu;
	struct cxl_context *ctx;

	phb = pci_bus_to_host(dev->bus);
	afu = (struct cxl_afu *)phb->private_data;
	set_dma_ops(&dev->dev, &dma_direct_ops);
	set_dma_offset(&dev->dev, PAGE_OFFSET);

	/*
	 * Allocate a context to do cxl things too.  If we eventually do real
	 * DMA ops, we'll need a default context to attach them to
	 */
	ctx = cxl_dev_context_init(dev);
	if (!ctx)
		return false;
	dev->dev.archdata.cxl_ctx = ctx;

	return (cxl_afu_check_and_enable(afu) == 0);
}

static void cxl_pci_disable_device(struct pci_dev *dev)
{
	struct cxl_context *ctx = cxl_get_context(dev);

	if (ctx) {
		if (ctx->status == STARTED) {
			dev_err(&dev->dev, "Default context started\n");
			return;
		}
		dev->dev.archdata.cxl_ctx = NULL;
		cxl_release_context(ctx);
	}
}

static resource_size_t cxl_pci_window_alignment(struct pci_bus *bus,
						unsigned long type)
{
	return 1;
}

static void cxl_pci_reset_secondary_bus(struct pci_dev *dev)
{
	/* Should we do an AFU reset here ? */
}

static int cxl_pcie_cfg_record(u8 bus, u8 devfn)
{
	return (bus << 8) + devfn;
}

static unsigned long cxl_pcie_cfg_addr(struct pci_controller* phb,
				       u8 bus, u8 devfn, int offset)
{
	int record = cxl_pcie_cfg_record(bus, devfn);

	return (unsigned long)phb->cfg_addr + ((unsigned long)phb->cfg_data * record) + offset;
}


static int cxl_pcie_config_info(struct pci_bus *bus, unsigned int devfn,
				int offset, int len,
				volatile void __iomem **ioaddr,
				u32 *mask, int *shift)
{
	struct pci_controller *phb;
	struct cxl_afu *afu;
	unsigned long addr;

	phb = pci_bus_to_host(bus);
	if (phb == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;
	afu = (struct cxl_afu *)phb->private_data;

	if (cxl_pcie_cfg_record(bus->number, devfn) > afu->crs_num)
		return PCIBIOS_DEVICE_NOT_FOUND;
	if (offset >= (unsigned long)phb->cfg_data)
		return PCIBIOS_BAD_REGISTER_NUMBER;
	addr = cxl_pcie_cfg_addr(phb, bus->number, devfn, offset);

	*ioaddr = (void *)(addr & ~0x3ULL);
	*shift = ((addr & 0x3) * 8);
	switch (len) {
	case 1:
		*mask = 0xff;
		break;
	case 2:
		*mask = 0xffff;
		break;
	default:
		*mask = 0xffffffff;
		break;
	}
	return 0;
}

static int cxl_pcie_read_config(struct pci_bus *bus, unsigned int devfn,
				int offset, int len, u32 *val)
{
	volatile void __iomem *ioaddr;
	int shift, rc;
	u32 mask;

	rc = cxl_pcie_config_info(bus, devfn, offset, len, &ioaddr,
				  &mask, &shift);
	if (rc)
		return rc;

	/* Can only read 32 bits */
	*val = (in_le32(ioaddr) >> shift) & mask;
	return PCIBIOS_SUCCESSFUL;
}

static int cxl_pcie_write_config(struct pci_bus *bus, unsigned int devfn,
				 int offset, int len, u32 val)
{
	volatile void __iomem *ioaddr;
	u32 v, mask;
	int shift, rc;

	rc = cxl_pcie_config_info(bus, devfn, offset, len, &ioaddr,
				  &mask, &shift);
	if (rc)
		return rc;

	/* Can only write 32 bits so do read-modify-write */
	mask <<= shift;
	val <<= shift;

	v = (in_le32(ioaddr) & ~mask) || (val & mask);

	out_le32(ioaddr, v);
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops cxl_pcie_pci_ops =
{
	.read = cxl_pcie_read_config,
	.write = cxl_pcie_write_config,
};


static struct pci_controller_ops cxl_pci_controller_ops =
{
	.probe_mode = cxl_pci_probe_mode,
	.enable_device_hook = cxl_pci_enable_device_hook,
	.disable_device = cxl_pci_disable_device,
	.release_device = cxl_pci_disable_device,
	.window_alignment = cxl_pci_window_alignment,
	.reset_secondary_bus = cxl_pci_reset_secondary_bus,
	.setup_msi_irqs = cxl_setup_msi_irqs,
	.teardown_msi_irqs = cxl_teardown_msi_irqs,
	.dma_set_mask = cxl_dma_set_mask,
};

int cxl_pci_vphb_add(struct cxl_afu *afu)
{
	struct pci_dev *phys_dev;
	struct pci_controller *phb, *phys_phb;

	phys_dev = to_pci_dev(afu->adapter->dev.parent);
	phys_phb = pci_bus_to_host(phys_dev->bus);

	/* Alloc and setup PHB data structure */
	phb = pcibios_alloc_controller(phys_phb->dn);

	if (!phb)
		return -ENODEV;

	/* Setup parent in sysfs */
	phb->parent = &phys_dev->dev;

	/* Setup the PHB using arch provided callback */
	phb->ops = &cxl_pcie_pci_ops;
	phb->cfg_addr = afu->afu_desc_mmio + afu->crs_offset;
	phb->cfg_data = (void *)(u64)afu->crs_len;
	phb->private_data = afu;
	phb->controller_ops = cxl_pci_controller_ops;

	/* Scan the bus */
	pcibios_scan_phb(phb);
	if (phb->bus == NULL)
		return -ENXIO;

	/* Claim resources. This might need some rework as well depending
	 * whether we are doing probe-only or not, like assigning unassigned
	 * resources etc...
	 */
	pcibios_claim_one_bus(phb->bus);

	/* Add probed PCI devices to the device model */
	pci_bus_add_devices(phb->bus);

	afu->phb = phb;

	return 0;
}


void cxl_pci_vphb_remove(struct cxl_afu *afu)
{
	struct pci_controller *phb;

	/* If there is no configuration record we won't have one of these */
	if (!afu || !afu->phb)
		return;

	phb = afu->phb;

	pci_remove_root_bus(phb->bus);
}

struct cxl_afu *cxl_pci_to_afu(struct pci_dev *dev)
{
	struct pci_controller *phb;

	phb = pci_bus_to_host(dev->bus);

	return (struct cxl_afu *)phb->private_data;
}
EXPORT_SYMBOL_GPL(cxl_pci_to_afu);

unsigned int cxl_pci_to_cfg_record(struct pci_dev *dev)
{
	return cxl_pcie_cfg_record(dev->bus->number, dev->devfn);
}
EXPORT_SYMBOL_GPL(cxl_pci_to_cfg_record);
