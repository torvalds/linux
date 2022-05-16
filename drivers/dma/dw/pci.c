// SPDX-License-Identifier: GPL-2.0
/*
 * PCI driver for the Synopsys DesignWare DMA Controller
 *
 * Copyright (C) 2013 Intel Corporation
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/device.h>

#include "internal.h"

static int dw_pci_probe(struct pci_dev *pdev, const struct pci_device_id *pid)
{
	const struct dw_dma_chip_pdata *drv_data = (void *)pid->driver_data;
	struct dw_dma_chip_pdata *data;
	struct dw_dma_chip *chip;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, 1 << 0, pci_name(pdev));
	if (ret) {
		dev_err(&pdev->dev, "I/O memory remapping failed\n");
		return ret;
	}

	pci_set_master(pdev);
	pci_try_set_mwi(pdev);

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	ret = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	data = devm_kmemdup(&pdev->dev, drv_data, sizeof(*drv_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	chip->id = pdev->devfn;
	chip->regs = pcim_iomap_table(pdev)[0];
	chip->irq = pdev->irq;
	chip->pdata = data->pdata;

	data->chip = chip;

	ret = data->probe(chip);
	if (ret)
		return ret;

	dw_dma_acpi_controller_register(chip->dw);

	pci_set_drvdata(pdev, data);

	return 0;
}

static void dw_pci_remove(struct pci_dev *pdev)
{
	struct dw_dma_chip_pdata *data = pci_get_drvdata(pdev);
	struct dw_dma_chip *chip = data->chip;
	int ret;

	dw_dma_acpi_controller_free(chip->dw);

	ret = data->remove(chip);
	if (ret)
		dev_warn(&pdev->dev, "can't remove device properly: %d\n", ret);
}

#ifdef CONFIG_PM_SLEEP

static int dw_pci_suspend_late(struct device *dev)
{
	struct dw_dma_chip_pdata *data = dev_get_drvdata(dev);
	struct dw_dma_chip *chip = data->chip;

	return do_dw_dma_disable(chip);
};

static int dw_pci_resume_early(struct device *dev)
{
	struct dw_dma_chip_pdata *data = dev_get_drvdata(dev);
	struct dw_dma_chip *chip = data->chip;

	return do_dw_dma_enable(chip);
};

#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops dw_pci_dev_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(dw_pci_suspend_late, dw_pci_resume_early)
};

static const struct pci_device_id dw_pci_id_table[] = {
	/* Medfield (GPDMA) */
	{ PCI_VDEVICE(INTEL, 0x0827), (kernel_ulong_t)&dw_dma_chip_pdata },

	/* BayTrail */
	{ PCI_VDEVICE(INTEL, 0x0f06), (kernel_ulong_t)&dw_dma_chip_pdata },
	{ PCI_VDEVICE(INTEL, 0x0f40), (kernel_ulong_t)&dw_dma_chip_pdata },

	/* Merrifield */
	{ PCI_VDEVICE(INTEL, 0x11a2), (kernel_ulong_t)&idma32_chip_pdata },

	/* Braswell */
	{ PCI_VDEVICE(INTEL, 0x2286), (kernel_ulong_t)&dw_dma_chip_pdata },
	{ PCI_VDEVICE(INTEL, 0x22c0), (kernel_ulong_t)&dw_dma_chip_pdata },

	/* Elkhart Lake iDMA 32-bit (PSE DMA) */
	{ PCI_VDEVICE(INTEL, 0x4bb4), (kernel_ulong_t)&idma32_chip_pdata },
	{ PCI_VDEVICE(INTEL, 0x4bb5), (kernel_ulong_t)&idma32_chip_pdata },
	{ PCI_VDEVICE(INTEL, 0x4bb6), (kernel_ulong_t)&idma32_chip_pdata },

	/* Haswell */
	{ PCI_VDEVICE(INTEL, 0x9c60), (kernel_ulong_t)&dw_dma_chip_pdata },

	/* Broadwell */
	{ PCI_VDEVICE(INTEL, 0x9ce0), (kernel_ulong_t)&dw_dma_chip_pdata },

	{ }
};
MODULE_DEVICE_TABLE(pci, dw_pci_id_table);

static struct pci_driver dw_pci_driver = {
	.name		= "dw_dmac_pci",
	.id_table	= dw_pci_id_table,
	.probe		= dw_pci_probe,
	.remove		= dw_pci_remove,
	.driver	= {
		.pm	= &dw_pci_dev_pm_ops,
	},
};

module_pci_driver(dw_pci_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Synopsys DesignWare DMA Controller PCI driver");
MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
