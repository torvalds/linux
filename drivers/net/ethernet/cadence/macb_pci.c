// SPDX-License-Identifier: GPL-2.0-only
/*
 * DOC: Cadence GEM PCI wrapper.
 *
 * Copyright (C) 2016 Cadence Design Systems - https://www.cadence.com
 *
 * Authors: Rafal Ozieblo <rafalo@cadence.com>
 *	    Bartosz Folta <bfolta@cadence.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include "macb.h"

#define PCI_DRIVER_NAME "macb_pci"
#define PLAT_DRIVER_NAME "macb"

#define CDNS_VENDOR_ID 0x17cd
#define CDNS_DEVICE_ID 0xe007

#define GEM_PCLK_RATE 50000000
#define GEM_HCLK_RATE 50000000

static int macb_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int err;
	struct platform_device *plat_dev;
	struct platform_device_info plat_info;
	struct macb_platform_data plat_data;
	struct resource res[2];

	/* enable pci device */
	err = pcim_enable_device(pdev);
	if (err < 0) {
		dev_err(&pdev->dev, "Enabling PCI device has failed: %d", err);
		return err;
	}

	pci_set_master(pdev);

	/* set up resources */
	memset(res, 0x00, sizeof(struct resource) * ARRAY_SIZE(res));
	res[0].start = pci_resource_start(pdev, 0);
	res[0].end = pci_resource_end(pdev, 0);
	res[0].name = PCI_DRIVER_NAME;
	res[0].flags = IORESOURCE_MEM;
	res[1].start = pci_irq_vector(pdev, 0);
	res[1].name = PCI_DRIVER_NAME;
	res[1].flags = IORESOURCE_IRQ;

	dev_info(&pdev->dev, "EMAC physical base addr: %pa\n",
		 &res[0].start);

	/* set up macb platform data */
	memset(&plat_data, 0, sizeof(plat_data));

	/* initialize clocks */
	plat_data.pclk = clk_register_fixed_rate(&pdev->dev, "pclk", NULL, 0,
						 GEM_PCLK_RATE);
	if (IS_ERR(plat_data.pclk)) {
		err = PTR_ERR(plat_data.pclk);
		goto err_pclk_register;
	}

	plat_data.hclk = clk_register_fixed_rate(&pdev->dev, "hclk", NULL, 0,
						 GEM_HCLK_RATE);
	if (IS_ERR(plat_data.hclk)) {
		err = PTR_ERR(plat_data.hclk);
		goto err_hclk_register;
	}

	/* set up platform device info */
	memset(&plat_info, 0, sizeof(plat_info));
	plat_info.parent = &pdev->dev;
	plat_info.fwnode = pdev->dev.fwnode;
	plat_info.name = PLAT_DRIVER_NAME;
	plat_info.id = pdev->devfn;
	plat_info.res = res;
	plat_info.num_res = ARRAY_SIZE(res);
	plat_info.data = &plat_data;
	plat_info.size_data = sizeof(plat_data);
	plat_info.dma_mask = pdev->dma_mask;

	/* register platform device */
	plat_dev = platform_device_register_full(&plat_info);
	if (IS_ERR(plat_dev)) {
		err = PTR_ERR(plat_dev);
		goto err_plat_dev_register;
	}

	pci_set_drvdata(pdev, plat_dev);

	return 0;

err_plat_dev_register:
	clk_unregister(plat_data.hclk);

err_hclk_register:
	clk_unregister(plat_data.pclk);

err_pclk_register:
	return err;
}

static void macb_remove(struct pci_dev *pdev)
{
	struct platform_device *plat_dev = pci_get_drvdata(pdev);
	struct macb_platform_data *plat_data = dev_get_platdata(&plat_dev->dev);

	platform_device_unregister(plat_dev);
	clk_unregister(plat_data->pclk);
	clk_unregister(plat_data->hclk);
}

static const struct pci_device_id dev_id_table[] = {
	{ PCI_DEVICE(CDNS_VENDOR_ID, CDNS_DEVICE_ID), },
	{ 0, }
};

static struct pci_driver macb_pci_driver = {
	.name     = PCI_DRIVER_NAME,
	.id_table = dev_id_table,
	.probe    = macb_probe,
	.remove	  = macb_remove,
};

module_pci_driver(macb_pci_driver);
MODULE_DEVICE_TABLE(pci, dev_id_table);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cadence NIC PCI wrapper");
