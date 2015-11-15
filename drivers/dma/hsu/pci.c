/*
 * PCI driver for the High Speed UART DMA
 *
 * Copyright (C) 2015 Intel Corporation
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *
 * Partially based on the bits found in drivers/tty/serial/mfd.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "hsu.h"

#define HSU_PCI_DMASR		0x00
#define HSU_PCI_DMAISR		0x04

#define HSU_PCI_CHAN_OFFSET	0x100

static irqreturn_t hsu_pci_irq(int irq, void *dev)
{
	struct hsu_dma_chip *chip = dev;
	u32 dmaisr;
	unsigned short i;
	irqreturn_t ret = IRQ_NONE;

	dmaisr = readl(chip->regs + HSU_PCI_DMAISR);
	for (i = 0; i < chip->hsu->nr_channels; i++) {
		if (dmaisr & 0x1)
			ret |= hsu_dma_irq(chip, i);
		dmaisr >>= 1;
	}

	return ret;
}

static int hsu_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct hsu_dma_chip *chip;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
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
	chip->regs = pcim_iomap_table(pdev)[0];
	chip->length = pci_resource_len(pdev, 0);
	chip->offset = HSU_PCI_CHAN_OFFSET;
	chip->irq = pdev->irq;

	pci_enable_msi(pdev);

	ret = hsu_dma_probe(chip);
	if (ret)
		return ret;

	ret = request_irq(chip->irq, hsu_pci_irq, 0, "hsu_dma_pci", chip);
	if (ret)
		goto err_register_irq;

	pci_set_drvdata(pdev, chip);

	return 0;

err_register_irq:
	hsu_dma_remove(chip);
	return ret;
}

static void hsu_pci_remove(struct pci_dev *pdev)
{
	struct hsu_dma_chip *chip = pci_get_drvdata(pdev);

	free_irq(chip->irq, chip);
	hsu_dma_remove(chip);
}

static const struct pci_device_id hsu_pci_id_table[] = {
	{ PCI_VDEVICE(INTEL, 0x081e), 0 },
	{ PCI_VDEVICE(INTEL, 0x1192), 0 },
	{ }
};
MODULE_DEVICE_TABLE(pci, hsu_pci_id_table);

static struct pci_driver hsu_pci_driver = {
	.name		= "hsu_dma_pci",
	.id_table	= hsu_pci_id_table,
	.probe		= hsu_pci_probe,
	.remove		= hsu_pci_remove,
};

module_pci_driver(hsu_pci_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("High Speed UART DMA PCI driver");
MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
