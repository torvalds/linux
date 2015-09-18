/*
 * bdc_pci.c - BRCM BDC USB3.0 device controller PCI interface file.
 *
 * Copyright (C) 2014 Broadcom Corporation
 *
 * Author: Ashwini Pahuja
 *
 * Based on drivers under drivers/usb/
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/platform_device.h>

#include "bdc.h"

#define BDC_PCI_PID 0x1570

struct bdc_pci {
	struct device *dev;
	struct platform_device *bdc;
};

static int bdc_setup_msi(struct pci_dev *pci)
{
	int ret;

	ret = pci_enable_msi(pci);
	if (ret) {
		pr_err("failed to allocate MSI entry\n");
		return ret;
	}

	return ret;
}

static int bdc_pci_probe(struct pci_dev *pci, const struct pci_device_id *id)
{
	struct resource res[2];
	struct platform_device *bdc;
	struct bdc_pci *glue;
	int ret = -ENOMEM;

	glue = devm_kzalloc(&pci->dev, sizeof(*glue), GFP_KERNEL);
	if (!glue)
		return -ENOMEM;

	glue->dev = &pci->dev;
	ret = pci_enable_device(pci);
	if (ret) {
		dev_err(&pci->dev, "failed to enable pci device\n");
		return -ENODEV;
	}
	pci_set_master(pci);

	bdc = platform_device_alloc(BRCM_BDC_NAME, PLATFORM_DEVID_AUTO);
	if (!bdc)
		return -ENOMEM;

	memset(res, 0x00, sizeof(struct resource) * ARRAY_SIZE(res));
	bdc_setup_msi(pci);

	res[0].start	= pci_resource_start(pci, 0);
	res[0].end	= pci_resource_end(pci, 0);
	res[0].name	= BRCM_BDC_NAME;
	res[0].flags	= IORESOURCE_MEM;

	res[1].start	= pci->irq;
	res[1].name	= BRCM_BDC_NAME;
	res[1].flags	= IORESOURCE_IRQ;

	ret = platform_device_add_resources(bdc, res, ARRAY_SIZE(res));
	if (ret) {
		dev_err(&pci->dev,
			"couldn't add resources to bdc device\n");
		return ret;
	}

	pci_set_drvdata(pci, glue);

	dma_set_coherent_mask(&bdc->dev, pci->dev.coherent_dma_mask);

	bdc->dev.dma_mask = pci->dev.dma_mask;
	bdc->dev.dma_parms = pci->dev.dma_parms;
	bdc->dev.parent = &pci->dev;
	glue->bdc = bdc;

	ret = platform_device_add(bdc);
	if (ret) {
		dev_err(&pci->dev, "failed to register bdc device\n");
		platform_device_put(bdc);
		return ret;
	}

	return 0;
}

static void bdc_pci_remove(struct pci_dev *pci)
{
	struct bdc_pci *glue = pci_get_drvdata(pci);

	platform_device_unregister(glue->bdc);
	pci_disable_msi(pci);
}

static struct pci_device_id bdc_pci_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, BDC_PCI_PID), },
	{} /* Terminating Entry */
};

MODULE_DEVICE_TABLE(pci, bdc_pci_id_table);

static struct pci_driver bdc_pci_driver = {
	.name = "bdc-pci",
	.id_table = bdc_pci_id_table,
	.probe = bdc_pci_probe,
	.remove = bdc_pci_remove,
};

MODULE_AUTHOR("Ashwini Pahuja <ashwini.linux@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("BRCM BDC USB3 PCI Glue layer");
module_pci_driver(bdc_pci_driver);
