/*
 * MEN Chameleon Bus.
 *
 * Copyright (C) 2014 MEN Mikroelektronik GmbH (www.men.de)
 * Author: Johannes Thumshirn <johannes.thumshirn@men.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/mcb.h>

#include "mcb-internal.h"

struct priv {
	struct mcb_bus *bus;
	void __iomem *base;
};

static int mcb_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct priv *priv;
	phys_addr_t mapbase;
	int ret;
	int num_cells;
	unsigned long flags;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable PCI device\n");
		return -ENODEV;
	}

	mapbase = pci_resource_start(pdev, 0);
	if (!mapbase) {
		dev_err(&pdev->dev, "No PCI resource\n");
		goto err_start;
	}

	ret = pci_request_region(pdev, 0, KBUILD_MODNAME);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request PCI BARs\n");
		goto err_start;
	}

	priv->base = pci_iomap(pdev, 0, 0);
	if (!priv->base) {
		dev_err(&pdev->dev, "Cannot ioremap\n");
		ret = -ENOMEM;
		goto err_ioremap;
	}

	flags = pci_resource_flags(pdev, 0);
	if (flags & IORESOURCE_IO) {
		ret = -ENOTSUPP;
		dev_err(&pdev->dev,
			"IO mapped PCI devices are not supported\n");
		goto err_ioremap;
	}

	pci_set_drvdata(pdev, priv);

	priv->bus = mcb_alloc_bus();

	ret = chameleon_parse_cells(priv->bus, mapbase, priv->base);
	if (ret < 0)
		goto err_drvdata;
	num_cells = ret;

	dev_dbg(&pdev->dev, "Found %d cells\n", num_cells);

	mcb_bus_add_devices(priv->bus);

err_drvdata:
	pci_iounmap(pdev, priv->base);
err_ioremap:
	pci_release_region(pdev, 0);
err_start:
	pci_disable_device(pdev);
	return ret;
}

static void mcb_pci_remove(struct pci_dev *pdev)
{
	struct priv *priv = pci_get_drvdata(pdev);

	mcb_release_bus(priv->bus);
}

static const struct pci_device_id mcb_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEN, PCI_DEVICE_ID_MEN_CHAMELEON) },
	{ 0 },
};
MODULE_DEVICE_TABLE(pci, mcb_pci_tbl);

static struct pci_driver mcb_pci_driver = {
	.name = "mcb-pci",
	.id_table = mcb_pci_tbl,
	.probe = mcb_pci_probe,
	.remove = mcb_pci_remove,
};

module_pci_driver(mcb_pci_driver);

MODULE_AUTHOR("Johannes Thumshirn <johannes.thumshirn@men.de>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MCB over PCI support");
