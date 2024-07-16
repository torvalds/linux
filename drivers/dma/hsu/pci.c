// SPDX-License-Identifier: GPL-2.0-only
/*
 * PCI driver for the High Speed UART DMA
 *
 * Copyright (C) 2015 Intel Corporation
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *
 * Partially based on the bits found in drivers/tty/serial/mfd.c.
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "hsu.h"

#define HSU_PCI_DMASR		0x00
#define HSU_PCI_DMAISR		0x04

#define HSU_PCI_CHAN_OFFSET	0x100

#define PCI_DEVICE_ID_INTEL_MFLD_HSU_DMA	0x081e
#define PCI_DEVICE_ID_INTEL_MRFLD_HSU_DMA	0x1192

static irqreturn_t hsu_pci_irq(int irq, void *dev)
{
	struct hsu_dma_chip *chip = dev;
	unsigned long dmaisr;
	unsigned short i;
	u32 status;
	int ret = 0;
	int err;

	dmaisr = readl(chip->regs + HSU_PCI_DMAISR);
	for_each_set_bit(i, &dmaisr, chip->hsu->nr_channels) {
		err = hsu_dma_get_status(chip, i, &status);
		if (err > 0)
			ret |= 1;
		else if (err == 0)
			ret |= hsu_dma_do_irq(chip, i, status);
	}

	return IRQ_RETVAL(ret);
}

static void hsu_pci_dma_remove(void *chip)
{
	hsu_dma_remove(chip);
}

static int hsu_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
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

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		return ret;

	chip->dev = &pdev->dev;
	chip->regs = pcim_iomap_table(pdev)[0];
	chip->length = pci_resource_len(pdev, 0);
	chip->offset = HSU_PCI_CHAN_OFFSET;
	chip->irq = pci_irq_vector(pdev, 0);

	ret = hsu_dma_probe(chip);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, hsu_pci_dma_remove, chip);
	if (ret)
		return ret;

	ret = devm_request_irq(dev, chip->irq, hsu_pci_irq, 0, "hsu_dma_pci", chip);
	if (ret)
		return ret;

	/*
	 * On Intel Tangier B0 and Anniedale the interrupt line, disregarding
	 * to have different numbers, is shared between HSU DMA and UART IPs.
	 * Thus on such SoCs we are expecting that IRQ handler is called in
	 * UART driver only. Instead of handling the spurious interrupt
	 * from HSU DMA here and waste CPU time and delay HSU UART interrupt
	 * handling, disable the interrupt entirely.
	 */
	if (pdev->device == PCI_DEVICE_ID_INTEL_MRFLD_HSU_DMA)
		disable_irq_nosync(chip->irq);

	pci_set_drvdata(pdev, chip);

	return 0;
}

static const struct pci_device_id hsu_pci_id_table[] = {
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_MFLD_HSU_DMA), 0 },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_MRFLD_HSU_DMA), 0 },
	{ }
};
MODULE_DEVICE_TABLE(pci, hsu_pci_id_table);

static struct pci_driver hsu_pci_driver = {
	.name		= "hsu_dma_pci",
	.id_table	= hsu_pci_id_table,
	.probe		= hsu_pci_probe,
};

module_pci_driver(hsu_pci_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("High Speed UART DMA PCI driver");
MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
