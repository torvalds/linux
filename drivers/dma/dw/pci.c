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

struct dw_dma_pci_data {
	const struct dw_dma_platform_data *pdata;
	int (*probe)(struct dw_dma_chip *chip);
};

static const struct dw_dma_pci_data dw_pci_data = {
	.probe = dw_dma_probe,
};

static const struct dw_dma_platform_data idma32_pdata = {
	.nr_channels = 8,
	.chan_allocation_order = CHAN_ALLOCATION_ASCENDING,
	.chan_priority = CHAN_PRIORITY_ASCENDING,
	.block_size = 131071,
	.nr_masters = 1,
	.data_width = {4},
	.multi_block = {1, 1, 1, 1, 1, 1, 1, 1},
};

static const struct dw_dma_pci_data idma32_pci_data = {
	.pdata = &idma32_pdata,
	.probe = idma32_dma_probe,
};

static int dw_pci_probe(struct pci_dev *pdev, const struct pci_device_id *pid)
{
	const struct dw_dma_pci_data *data = (void *)pid->driver_data;
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

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	chip->id = pdev->devfn;
	chip->regs = pcim_iomap_table(pdev)[0];
	chip->irq = pdev->irq;
	chip->pdata = data->pdata;

	ret = data->probe(chip);
	if (ret)
		return ret;

	pci_set_drvdata(pdev, chip);

	return 0;
}

static void dw_pci_remove(struct pci_dev *pdev)
{
	struct dw_dma_chip *chip = pci_get_drvdata(pdev);
	int ret;

	ret = dw_dma_remove(chip);
	if (ret)
		dev_warn(&pdev->dev, "can't remove device properly: %d\n", ret);
}

#ifdef CONFIG_PM_SLEEP

static int dw_pci_suspend_late(struct device *dev)
{
	struct pci_dev *pci = to_pci_dev(dev);
	struct dw_dma_chip *chip = pci_get_drvdata(pci);

	return do_dw_dma_disable(chip);
};

static int dw_pci_resume_early(struct device *dev)
{
	struct pci_dev *pci = to_pci_dev(dev);
	struct dw_dma_chip *chip = pci_get_drvdata(pci);

	return do_dw_dma_enable(chip);
};

#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops dw_pci_dev_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(dw_pci_suspend_late, dw_pci_resume_early)
};

static const struct pci_device_id dw_pci_id_table[] = {
	/* Medfield (GPDMA) */
	{ PCI_VDEVICE(INTEL, 0x0827), (kernel_ulong_t)&dw_pci_data },

	/* BayTrail */
	{ PCI_VDEVICE(INTEL, 0x0f06), (kernel_ulong_t)&dw_pci_data },
	{ PCI_VDEVICE(INTEL, 0x0f40), (kernel_ulong_t)&dw_pci_data },

	/* Merrifield */
	{ PCI_VDEVICE(INTEL, 0x11a2), (kernel_ulong_t)&idma32_pci_data },

	/* Braswell */
	{ PCI_VDEVICE(INTEL, 0x2286), (kernel_ulong_t)&dw_pci_data },
	{ PCI_VDEVICE(INTEL, 0x22c0), (kernel_ulong_t)&dw_pci_data },

	/* Haswell */
	{ PCI_VDEVICE(INTEL, 0x9c60), (kernel_ulong_t)&dw_pci_data },

	/* Broadwell */
	{ PCI_VDEVICE(INTEL, 0x9ce0), (kernel_ulong_t)&dw_pci_data },

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
