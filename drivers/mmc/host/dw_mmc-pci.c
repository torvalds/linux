/*
 * Synopsys DesignWare Multimedia Card PCI Interface driver
 *
 * Copyright (C) 2012 Vayavya Labs Pvt. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/dw_mmc.h>
#include "dw_mmc.h"

#define PCI_BAR_NO 2
#define COMPLETE_BAR 0
#define SYNOPSYS_DW_MCI_VENDOR_ID 0x700
#define SYNOPSYS_DW_MCI_DEVICE_ID 0x1107
/* Defining the Capabilities */
#define DW_MCI_CAPABILITIES (MMC_CAP_4_BIT_DATA | MMC_CAP_MMC_HIGHSPEED |\
				MMC_CAP_SD_HIGHSPEED | MMC_CAP_8_BIT_DATA |\
				MMC_CAP_SDIO_IRQ)

static struct dw_mci_board pci_board_data = {
	.num_slots			= 1,
	.caps				= DW_MCI_CAPABILITIES,
	.bus_hz				= 33 * 1000 * 1000,
	.detect_delay_ms		= 200,
	.fifo_depth			= 32,
};

static int dw_mci_pci_probe(struct pci_dev *pdev,
				  const struct pci_device_id *entries)
{
	struct dw_mci *host;
	int ret;

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	if (pci_request_regions(pdev, "dw_mmc_pci")) {
		ret = -ENODEV;
		goto err_disable_dev;
	}

	host = kzalloc(sizeof(struct dw_mci), GFP_KERNEL);
	if (!host) {
		ret = -ENOMEM;
		goto err_release;
	}

	host->irq = pdev->irq;
	host->irq_flags = IRQF_SHARED;
	host->dev = &pdev->dev;
	host->pdata = &pci_board_data;

	host->regs = pci_iomap(pdev, PCI_BAR_NO, COMPLETE_BAR);
	if (!host->regs) {
		ret = -EIO;
		goto err_unmap;
	}

	pci_set_drvdata(pdev, host);
	ret = dw_mci_probe(host);
	if (ret)
		goto err_probe_failed;
	return ret;

err_probe_failed:
	pci_iounmap(pdev, host->regs);
err_unmap:
	kfree(host);
err_release:
	pci_release_regions(pdev);
err_disable_dev:
	pci_disable_device(pdev);
	return ret;
}

static void dw_mci_pci_remove(struct pci_dev *pdev)
{
	struct dw_mci *host = pci_get_drvdata(pdev);

	dw_mci_remove(host);
	pci_set_drvdata(pdev, NULL);
	pci_release_regions(pdev);
	pci_iounmap(pdev, host->regs);
	kfree(host);
	pci_disable_device(pdev);
}

#ifdef CONFIG_PM_SLEEP
static int dw_mci_pci_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct dw_mci *host = pci_get_drvdata(pdev);

	return dw_mci_suspend(host);
}

static int dw_mci_pci_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct dw_mci *host = pci_get_drvdata(pdev);

	return dw_mci_resume(host);
}
#else
#define dw_mci_pci_suspend	NULL
#define dw_mci_pci_resume	NULL
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(dw_mci_pci_pmops, dw_mci_pci_suspend, dw_mci_pci_resume);

static DEFINE_PCI_DEVICE_TABLE(dw_mci_pci_id) = {
	{ PCI_DEVICE(SYNOPSYS_DW_MCI_VENDOR_ID, SYNOPSYS_DW_MCI_DEVICE_ID) },
	{}
};
MODULE_DEVICE_TABLE(pci, dw_mci_pci_id);

static struct pci_driver dw_mci_pci_driver = {
	.name		= "dw_mmc_pci",
	.id_table	= dw_mci_pci_id,
	.probe		= dw_mci_pci_probe,
	.remove		= dw_mci_pci_remove,
	.driver		=	{
		.pm =   &dw_mci_pci_pmops
	},
};

module_pci_driver(dw_mci_pci_driver);

MODULE_DESCRIPTION("DW Multimedia Card PCI Interface driver");
MODULE_AUTHOR("Shashidhar Hiremath <shashidharh@vayavyalabs.com>");
MODULE_LICENSE("GPL v2");
