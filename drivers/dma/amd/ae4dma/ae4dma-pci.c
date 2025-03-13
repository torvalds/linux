// SPDX-License-Identifier: GPL-2.0
/*
 * AMD AE4DMA driver
 *
 * Copyright (c) 2024, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */

#include "ae4dma.h"

static int ae4_get_irqs(struct ae4_device *ae4)
{
	struct ae4_msix *ae4_msix = ae4->ae4_msix;
	struct pt_device *pt = &ae4->pt;
	struct device *dev = pt->dev;
	struct pci_dev *pdev;
	int i, v, ret;

	pdev = to_pci_dev(dev);

	for (v = 0; v < ARRAY_SIZE(ae4_msix->msix_entry); v++)
		ae4_msix->msix_entry[v].entry = v;

	ret = pci_alloc_irq_vectors(pdev, v, v, PCI_IRQ_MSIX);
	if (ret != v) {
		if (ret > 0)
			pci_free_irq_vectors(pdev);

		dev_err(dev, "could not enable MSI-X (%d), trying MSI\n", ret);
		ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
		if (ret < 0) {
			dev_err(dev, "could not enable MSI (%d)\n", ret);
			return ret;
		}

		ret = pci_irq_vector(pdev, 0);
		if (ret < 0) {
			pci_free_irq_vectors(pdev);
			return ret;
		}

		for (i = 0; i < MAX_AE4_HW_QUEUES; i++)
			ae4->ae4_irq[i] = ret;

	} else {
		ae4_msix->msix_count = ret;
		for (i = 0; i < MAX_AE4_HW_QUEUES; i++)
			ae4->ae4_irq[i] = ae4_msix->msix_entry[i].vector;
	}

	return ret;
}

static void ae4_free_irqs(struct ae4_device *ae4)
{
	struct ae4_msix *ae4_msix = ae4->ae4_msix;
	struct pt_device *pt = &ae4->pt;
	struct device *dev = pt->dev;
	struct pci_dev *pdev;

	pdev = to_pci_dev(dev);

	if (ae4_msix && (ae4_msix->msix_count || ae4->ae4_irq[MAX_AE4_HW_QUEUES - 1]))
		pci_free_irq_vectors(pdev);
}

static void ae4_deinit(struct ae4_device *ae4)
{
	ae4_free_irqs(ae4);
}

static int ae4_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct ae4_device *ae4;
	struct pt_device *pt;
	int bar_mask;
	int ret = 0;

	ae4 = devm_kzalloc(dev, sizeof(*ae4), GFP_KERNEL);
	if (!ae4)
		return -ENOMEM;

	ae4->ae4_msix = devm_kzalloc(dev, sizeof(struct ae4_msix), GFP_KERNEL);
	if (!ae4->ae4_msix)
		return -ENOMEM;

	ret = pcim_enable_device(pdev);
	if (ret)
		goto ae4_error;

	bar_mask = pci_select_bars(pdev, IORESOURCE_MEM);
	ret = pcim_iomap_regions(pdev, bar_mask, "ae4dma");
	if (ret)
		goto ae4_error;

	pt = &ae4->pt;
	pt->dev = dev;
	pt->ver = AE4_DMA_VERSION;

	pt->io_regs = pcim_iomap_table(pdev)[0];
	if (!pt->io_regs) {
		ret = -ENOMEM;
		goto ae4_error;
	}

	ret = ae4_get_irqs(ae4);
	if (ret < 0)
		goto ae4_error;

	pci_set_master(pdev);

	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(48));

	dev_set_drvdata(dev, ae4);

	ret = ae4_core_init(ae4);
	if (ret)
		goto ae4_error;

	return 0;

ae4_error:
	ae4_deinit(ae4);

	return ret;
}

static void ae4_pci_remove(struct pci_dev *pdev)
{
	struct ae4_device *ae4 = dev_get_drvdata(&pdev->dev);

	ae4_destroy_work(ae4);
	ae4_deinit(ae4);
}

static const struct pci_device_id ae4_pci_table[] = {
	{ PCI_VDEVICE(AMD, 0x14C8), },
	{ PCI_VDEVICE(AMD, 0x14DC), },
	{ PCI_VDEVICE(AMD, 0x149B), },
	/* Last entry must be zero */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ae4_pci_table);

static struct pci_driver ae4_pci_driver = {
	.name = "ae4dma",
	.id_table = ae4_pci_table,
	.probe = ae4_pci_probe,
	.remove = ae4_pci_remove,
};

module_pci_driver(ae4_pci_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AMD AE4DMA driver");
