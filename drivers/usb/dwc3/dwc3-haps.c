// SPDX-License-Identifier: GPL-2.0
/**
 * dwc3-haps.c - Synopsys HAPS PCI Specific glue layer
 *
 * Copyright (C) 2018 Synopsys, Inc.
 *
 * Authors: Thinh Nguyen <thinhn@synopsys.com>,
 *          John Youn <johnyoun@synopsys.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/property.h>

/**
 * struct dwc3_haps - Driver private structure
 * @dwc3: child dwc3 platform_device
 * @pci: our link to PCI bus
 */
struct dwc3_haps {
	struct platform_device *dwc3;
	struct pci_dev *pci;
};

static const struct property_entry initial_properties[] = {
	PROPERTY_ENTRY_BOOL("snps,usb3_lpm_capable"),
	PROPERTY_ENTRY_BOOL("snps,has-lpm-erratum"),
	PROPERTY_ENTRY_BOOL("snps,dis_enblslpm_quirk"),
	PROPERTY_ENTRY_BOOL("linux,sysdev_is_parent"),
	{ },
};

static int dwc3_haps_probe(struct pci_dev *pci,
			   const struct pci_device_id *id)
{
	struct dwc3_haps	*dwc;
	struct device		*dev = &pci->dev;
	struct resource		res[2];
	int			ret;

	ret = pcim_enable_device(pci);
	if (ret) {
		dev_err(dev, "failed to enable pci device\n");
		return -ENODEV;
	}

	pci_set_master(pci);

	dwc = devm_kzalloc(dev, sizeof(*dwc), GFP_KERNEL);
	if (!dwc)
		return -ENOMEM;

	dwc->dwc3 = platform_device_alloc("dwc3", PLATFORM_DEVID_AUTO);
	if (!dwc->dwc3)
		return -ENOMEM;

	memset(res, 0x00, sizeof(struct resource) * ARRAY_SIZE(res));

	res[0].start	= pci_resource_start(pci, 0);
	res[0].end	= pci_resource_end(pci, 0);
	res[0].name	= "dwc_usb3";
	res[0].flags	= IORESOURCE_MEM;

	res[1].start	= pci->irq;
	res[1].name	= "dwc_usb3";
	res[1].flags	= IORESOURCE_IRQ;

	ret = platform_device_add_resources(dwc->dwc3, res, ARRAY_SIZE(res));
	if (ret) {
		dev_err(dev, "couldn't add resources to dwc3 device\n");
		goto err;
	}

	dwc->pci = pci;
	dwc->dwc3->dev.parent = dev;

	ret = platform_device_add_properties(dwc->dwc3, initial_properties);
	if (ret)
		goto err;

	ret = platform_device_add(dwc->dwc3);
	if (ret) {
		dev_err(dev, "failed to register dwc3 device\n");
		goto err;
	}

	pci_set_drvdata(pci, dwc);

	return 0;
err:
	platform_device_put(dwc->dwc3);
	return ret;
}

static void dwc3_haps_remove(struct pci_dev *pci)
{
	struct dwc3_haps *dwc = pci_get_drvdata(pci);

	platform_device_unregister(dwc->dwc3);
}

static const struct pci_device_id dwc3_haps_id_table[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_SYNOPSYS,
			   PCI_DEVICE_ID_SYNOPSYS_HAPSUSB3),
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_SYNOPSYS,
			   PCI_DEVICE_ID_SYNOPSYS_HAPSUSB3_AXI),
	},
	{
		PCI_DEVICE(PCI_VENDOR_ID_SYNOPSYS,
			   PCI_DEVICE_ID_SYNOPSYS_HAPSUSB31),
	},
	{  }	/* Terminating Entry */
};
MODULE_DEVICE_TABLE(pci, dwc3_haps_id_table);

static struct pci_driver dwc3_haps_driver = {
	.name		= "dwc3-haps",
	.id_table	= dwc3_haps_id_table,
	.probe		= dwc3_haps_probe,
	.remove		= dwc3_haps_remove,
};

MODULE_AUTHOR("Thinh Nguyen <thinhn@synopsys.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Synopsys HAPS PCI Glue Layer");

module_pci_driver(dwc3_haps_driver);
