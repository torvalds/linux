/*
 * PCI interface driver for DW SPI Core
 *
 * Copyright (c) 2009, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
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

static int spi_pci_probe(struct pci_dev *pdev,
	const struct pci_device_id *ent)
{
	struct dw_spi_pci *dwpci;
	struct dw_spi *dws;
	int pci_bar = 0;
	int ret;

	dev_info(&pdev->dev, "found PCI SPI controller(ID: %04x:%04x)\n",
		pdev->vendor, pdev->device);

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

	ret = pcim_iomap_regions(pdev, 1, dev_name(&pdev->dev));
	if (ret)
		return ret;

	dws->regs = pcim_iomap_table(pdev)[pci_bar];

	dws->bus_num = 0;
	dws->num_cs = 4;
	dws->irq = pdev->irq;

	/*
	 * Specific handling for Intel MID paltforms, like dma setup,
	 * clock rate, FIFO depth.
	 */
	if (pdev->device == 0x0800) {
		ret = dw_spi_mid_init(dws);
		if (ret)
			return ret;
	}

	ret = dw_spi_add_host(&pdev->dev, dws);
	if (ret)
		return ret;

	/* PCI hook and SPI hook use the same drv data */
	pci_set_drvdata(pdev, dwpci);

	return 0;
}

static void spi_pci_remove(struct pci_dev *pdev)
{
	struct dw_spi_pci *dwpci = pci_get_drvdata(pdev);

	dw_spi_remove_host(&dwpci->dws);
}

#ifdef CONFIG_PM
static int spi_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct dw_spi_pci *dwpci = pci_get_drvdata(pdev);
	int ret;

	ret = dw_spi_suspend_host(&dwpci->dws);
	if (ret)
		return ret;
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));
	return ret;
}

static int spi_resume(struct pci_dev *pdev)
{
	struct dw_spi_pci *dwpci = pci_get_drvdata(pdev);
	int ret;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	return dw_spi_resume_host(&dwpci->dws);
}
#else
#define spi_suspend	NULL
#define spi_resume	NULL
#endif

static const struct pci_device_id pci_ids[] = {
	/* Intel MID platform SPI controller 0 */
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x0800) },
	{},
};

static struct pci_driver dw_spi_driver = {
	.name =		DRIVER_NAME,
	.id_table =	pci_ids,
	.probe =	spi_pci_probe,
	.remove =	spi_pci_remove,
	.suspend =	spi_suspend,
	.resume	=	spi_resume,
};

module_pci_driver(dw_spi_driver);

MODULE_AUTHOR("Feng Tang <feng.tang@intel.com>");
MODULE_DESCRIPTION("PCI interface driver for DW SPI Core");
MODULE_LICENSE("GPL v2");
