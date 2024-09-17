// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/misc/xillybus_pcie.c
 *
 * Copyright 2011 Xillybus Ltd, http://xillybus.com
 *
 * Driver for the Xillybus FPGA/host framework using PCI Express.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include "xillybus.h"

MODULE_DESCRIPTION("Xillybus driver for PCIe");
MODULE_AUTHOR("Eli Billauer, Xillybus Ltd.");
MODULE_ALIAS("xillybus_pcie");
MODULE_LICENSE("GPL v2");

#define PCI_DEVICE_ID_XILLYBUS		0xebeb

#define PCI_VENDOR_ID_ACTEL		0x11aa
#define PCI_VENDOR_ID_LATTICE		0x1204

static const char xillyname[] = "xillybus_pcie";

static const struct pci_device_id xillyids[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_XILINX, PCI_DEVICE_ID_XILLYBUS)},
	{PCI_DEVICE(PCI_VENDOR_ID_ALTERA, PCI_DEVICE_ID_XILLYBUS)},
	{PCI_DEVICE(PCI_VENDOR_ID_ACTEL, PCI_DEVICE_ID_XILLYBUS)},
	{PCI_DEVICE(PCI_VENDOR_ID_LATTICE, PCI_DEVICE_ID_XILLYBUS)},
	{ /* End: all zeroes */ }
};

static int xilly_probe(struct pci_dev *pdev,
		       const struct pci_device_id *ent)
{
	struct xilly_endpoint *endpoint;
	int rc;

	endpoint = xillybus_init_endpoint(&pdev->dev);

	if (!endpoint)
		return -ENOMEM;

	pci_set_drvdata(pdev, endpoint);

	endpoint->owner = THIS_MODULE;

	rc = pcim_enable_device(pdev);
	if (rc) {
		dev_err(endpoint->dev,
			"pcim_enable_device() failed. Aborting.\n");
		return rc;
	}

	/* L0s has caused packet drops. No power saving, thank you. */

	pci_disable_link_state(pdev, PCIE_LINK_STATE_L0S);

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		dev_err(endpoint->dev,
			"Incorrect BAR configuration. Aborting.\n");
		return -ENODEV;
	}

	rc = pcim_iomap_regions(pdev, 0x01, xillyname);
	if (rc) {
		dev_err(endpoint->dev,
			"pcim_iomap_regions() failed. Aborting.\n");
		return rc;
	}

	endpoint->registers = pcim_iomap_table(pdev)[0];

	pci_set_master(pdev);

	/* Set up a single MSI interrupt */
	if (pci_enable_msi(pdev)) {
		dev_err(endpoint->dev,
			"Failed to enable MSI interrupts. Aborting.\n");
		return -ENODEV;
	}
	rc = devm_request_irq(&pdev->dev, pdev->irq, xillybus_isr, 0,
			      xillyname, endpoint);
	if (rc) {
		dev_err(endpoint->dev,
			"Failed to register MSI handler. Aborting.\n");
		return -ENODEV;
	}

	/*
	 * Some (old and buggy?) hardware drops 64-bit addressed PCIe packets,
	 * even when the PCIe driver claims that a 64-bit mask is OK. On the
	 * other hand, on some architectures, 64-bit addressing is mandatory.
	 * So go for the 64-bit mask only when failing is the other option.
	 */

	if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(32))) {
		endpoint->dma_using_dac = 0;
	} else if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(64))) {
		endpoint->dma_using_dac = 1;
	} else {
		dev_err(endpoint->dev, "Failed to set DMA mask. Aborting.\n");
		return -ENODEV;
	}

	return xillybus_endpoint_discovery(endpoint);
}

static void xilly_remove(struct pci_dev *pdev)
{
	struct xilly_endpoint *endpoint = pci_get_drvdata(pdev);

	xillybus_endpoint_remove(endpoint);
}

MODULE_DEVICE_TABLE(pci, xillyids);

static struct pci_driver xillybus_driver = {
	.name = xillyname,
	.id_table = xillyids,
	.probe = xilly_probe,
	.remove = xilly_remove,
};

module_pci_driver(xillybus_driver);
