/*
 * PCI interface driver for DW SPI Core
 *
 * Copyright (c) 2009, 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/module.h>

#include "spi-dw.h"

#define DRIVER_NAME "dw_spi_pci"

struct dw_spi_pci {
	struct pci_dev	*pdev;
	struct dw_spi	dws;
};

struct spi_pci_desc {
	int	(*setup)(struct dw_spi *);
};

static struct spi_pci_desc spi_pci_mid_desc = {
	.setup = dw_spi_mid_init,
};

static int spi_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct dw_spi_pci *dwpci;
	struct dw_spi *dws;
	struct spi_pci_desc *desc = (struct spi_pci_desc *)ent->driver_data;
	int pci_bar = 0;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	dwpci = devm_kzalloc(&pdev->dev, sizeof(struct dw_spi_pci),
			GFP_KERNEL);
	if (!dwpci)
		return -ENOMEM;

	dwpci->pdev = pdev;
	dws = &dwpci->dws;

	/* Get basic io resource and map it */
	dws->paddr = pci_resource_start(pdev, pci_bar);

	ret = pcim_iomap_regions(pdev, 1 << pci_bar, pci_name(pdev));
	if (ret)
		return ret;

	dws->regs = pcim_iomap_table(pdev)[pci_bar];

	dws->bus_num = 0;
	dws->num_cs = 4;
	dws->irq = pdev->irq;

	/*
	 * Specific handling for paltforms, like dma setup,
	 * clock rate, FIFO depth.
	 */
	if (desc && desc->setup) {
		ret = desc->setup(dws);
		if (ret)
			return ret;
	}

	ret = dw_spi_add_host(&pdev->dev, dws);
	if (ret)
		return ret;

	/* PCI hook and SPI hook use the same drv data */
	pci_set_drvdata(pdev, dwpci);

	dev_info(&pdev->dev, "found PCI SPI controller(ID: %04x:%04x)\n",
		pdev->vendor, pdev->device);

	return 0;
}

static void spi_pci_remove(struct pci_dev *pdev)
{
	struct dw_spi_pci *dwpci = pci_get_drvdata(pdev);

	dw_spi_remove_host(&dwpci->dws);
}

#ifdef CONFIG_PM_SLEEP
static int spi_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct dw_spi_pci *dwpci = pci_get_drvdata(pdev);

	return dw_spi_suspend_host(&dwpci->dws);
}

static int spi_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct dw_spi_pci *dwpci = pci_get_drvdata(pdev);

	return dw_spi_resume_host(&dwpci->dws);
}
#endif

static SIMPLE_DEV_PM_OPS(dw_spi_pm_ops, spi_suspend, spi_resume);

static const struct pci_device_id pci_ids[] = {
	/* Intel MID platform SPI controller 0 */
	{ PCI_VDEVICE(INTEL, 0x0800), (kernel_ulong_t)&spi_pci_mid_desc},
	{},
};

static struct pci_driver dw_spi_driver = {
	.name =		DRIVER_NAME,
	.id_table =	pci_ids,
	.probe =	spi_pci_probe,
	.remove =	spi_pci_remove,
	.driver         = {
		.pm     = &dw_spi_pm_ops,
	},
};

module_pci_driver(dw_spi_driver);

MODULE_AUTHOR("Feng Tang <feng.tang@intel.com>");
MODULE_DESCRIPTION("PCI interface driver for DW SPI Core");
MODULE_LICENSE("GPL v2");
