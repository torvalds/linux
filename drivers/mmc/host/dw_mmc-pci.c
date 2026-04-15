// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Synopsys DesignWare Multimedia Card PCI Interface driver
 *
 * Copyright (C) 2012 Vayavya Labs Pvt. Ltd.
 */

#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/pci-epf.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include "dw_mmc.h"
#include "dw_mmc-pltfm.h"

#define SYNOPSYS_DW_MCI_VENDOR_ID 0x700
#define SYNOPSYS_DW_MCI_DEVICE_ID 0x1107
/* Defining the Capabilities */
#define DW_MCI_CAPABILITIES (MMC_CAP_4_BIT_DATA | MMC_CAP_MMC_HIGHSPEED |\
				MMC_CAP_SD_HIGHSPEED | MMC_CAP_8_BIT_DATA |\
				MMC_CAP_SDIO_IRQ)

static const struct dw_mci_drv_data pci_drv_data = {
	.common_caps = DW_MCI_CAPABILITIES,
};

static int dw_mci_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *entries)
{
	struct dw_mci *host;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	host = dw_mci_alloc_host(&pdev->dev);
	if (IS_ERR(host))
		return PTR_ERR(host);

	host->irq = pdev->irq;
	host->irq_flags = IRQF_SHARED;
	host->fifo_depth = 32;
	host->detect_delay_ms = 200;
	host->bus_hz = 33 * 1000 * 1000;
	host->drv_data = &pci_drv_data;

	host->regs = pcim_iomap_region(pdev, BAR_2, pci_name(pdev));
	if (IS_ERR(host->regs))
		return PTR_ERR(host->regs);

	pci_set_master(pdev);

	ret = dw_mci_probe(host);
	if (ret)
		return ret;

	pci_set_drvdata(pdev, host);

	return 0;
}

static void dw_mci_pci_remove(struct pci_dev *pdev)
{
	struct dw_mci *host = pci_get_drvdata(pdev);

	dw_mci_remove(host);
}

static const struct pci_device_id dw_mci_pci_id[] = {
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
		.pm =   pm_ptr(&dw_mci_pmops),
	},
};

module_pci_driver(dw_mci_pci_driver);

MODULE_DESCRIPTION("DW Multimedia Card PCI Interface driver");
MODULE_AUTHOR("Shashidhar Hiremath <shashidharh@vayavyalabs.com>");
MODULE_LICENSE("GPL v2");
