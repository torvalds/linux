// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCI interface driver for DW SPI Core
 *
 * Copyright (c) 2009, 2014 Intel Corporation.
 */

#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/module.h>

#include "spi-dw.h"

#define DRIVER_NAME "dw_spi_pci"

struct spi_pci_desc {
	int	(*setup)(struct dw_spi *);
	u16	num_cs;
	u16	bus_num;
};

static struct spi_pci_desc spi_pci_mid_desc_1 = {
	.setup = dw_spi_mid_init,
	.num_cs = 5,
	.bus_num = 0,
};

static struct spi_pci_desc spi_pci_mid_desc_2 = {
	.setup = dw_spi_mid_init,
	.num_cs = 2,
	.bus_num = 1,
};

static int spi_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct dw_spi *dws;
	struct spi_pci_desc *desc = (struct spi_pci_desc *)ent->driver_data;
	int pci_bar = 0;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	dws = devm_kzalloc(&pdev->dev, sizeof(*dws), GFP_KERNEL);
	if (!dws)
		return -ENOMEM;

	/* Get basic io resource and map it */
	dws->paddr = pci_resource_start(pdev, pci_bar);

	ret = pcim_iomap_regions(pdev, 1 << pci_bar, pci_name(pdev));
	if (ret)
		return ret;

	dws->regs = pcim_iomap_table(pdev)[pci_bar];
	dws->irq = pdev->irq;

	/*
	 * Specific handling for platforms, like dma setup,
	 * clock rate, FIFO depth.
	 */
	if (desc) {
		dws->num_cs = desc->num_cs;
		dws->bus_num = desc->bus_num;

		if (desc->setup) {
			ret = desc->setup(dws);
			if (ret)
				return ret;
		}
	} else {
		return -ENODEV;
	}

	ret = dw_spi_add_host(&pdev->dev, dws);
	if (ret)
		return ret;

	/* PCI hook and SPI hook use the same drv data */
	pci_set_drvdata(pdev, dws);

	dev_info(&pdev->dev, "found PCI SPI controller(ID: %04x:%04x)\n",
		pdev->vendor, pdev->device);

	return 0;
}

static void spi_pci_remove(struct pci_dev *pdev)
{
	struct dw_spi *dws = pci_get_drvdata(pdev);

	dw_spi_remove_host(dws);
}

#ifdef CONFIG_PM_SLEEP
static int spi_suspend(struct device *dev)
{
	struct dw_spi *dws = dev_get_drvdata(dev);

	return dw_spi_suspend_host(dws);
}

static int spi_resume(struct device *dev)
{
	struct dw_spi *dws = dev_get_drvdata(dev);

	return dw_spi_resume_host(dws);
}
#endif

static SIMPLE_DEV_PM_OPS(dw_spi_pm_ops, spi_suspend, spi_resume);

static const struct pci_device_id pci_ids[] = {
	/* Intel MID platform SPI controller 0 */
	/*
	 * The access to the device 8086:0801 is disabled by HW, since it's
	 * exclusively used by SCU to communicate with MSIC.
	 */
	/* Intel MID platform SPI controller 1 */
	{ PCI_VDEVICE(INTEL, 0x0800), (kernel_ulong_t)&spi_pci_mid_desc_1},
	/* Intel MID platform SPI controller 2 */
	{ PCI_VDEVICE(INTEL, 0x0812), (kernel_ulong_t)&spi_pci_mid_desc_2},
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
