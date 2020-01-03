// SPDX-License-Identifier: GPL-2.0
/*
 * Microsemi Switchtec(tm) PCIe Management Driver
 * Copyright (c) 2019, Logan Gunthorpe <logang@deltatee.com>
 * Copyright (c) 2019, GigaIO Networks, Inc
 */

#include "dmaengine.h"

#include <linux/dmaengine.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/pci.h>

MODULE_DESCRIPTION("PLX ExpressLane PEX PCI Switch DMA Engine");
MODULE_VERSION("0.1");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Logan Gunthorpe");

struct plx_dma_dev {
	struct dma_device dma_dev;
	struct dma_chan dma_chan;
	void __iomem *bar;
};

static void plx_dma_release(struct dma_device *dma_dev)
{
	struct plx_dma_dev *plxdev =
		container_of(dma_dev, struct plx_dma_dev, dma_dev);

	put_device(dma_dev->dev);
	kfree(plxdev);
}

static int plx_dma_create(struct pci_dev *pdev)
{
	struct plx_dma_dev *plxdev;
	struct dma_device *dma;
	struct dma_chan *chan;
	int rc;

	plxdev = kzalloc(sizeof(*plxdev), GFP_KERNEL);
	if (!plxdev)
		return -ENOMEM;

	plxdev->bar = pcim_iomap_table(pdev)[0];

	dma = &plxdev->dma_dev;
	dma->chancnt = 1;
	INIT_LIST_HEAD(&dma->channels);
	dma->copy_align = DMAENGINE_ALIGN_1_BYTE;
	dma->dev = get_device(&pdev->dev);

	dma->device_release = plx_dma_release;

	chan = &plxdev->dma_chan;
	chan->device = dma;
	dma_cookie_init(chan);
	list_add_tail(&chan->device_node, &dma->channels);

	rc = dma_async_device_register(dma);
	if (rc) {
		pci_err(pdev, "Failed to register dma device: %d\n", rc);
		free_irq(pci_irq_vector(pdev, 0),  plxdev);
		kfree(plxdev);
		return rc;
	}

	pci_set_drvdata(pdev, plxdev);

	return 0;
}

static int plx_dma_probe(struct pci_dev *pdev,
			 const struct pci_device_id *id)
{
	int rc;

	rc = pcim_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(48));
	if (rc)
		rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (rc)
		return rc;

	rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(48));
	if (rc)
		rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	if (rc)
		return rc;

	rc = pcim_iomap_regions(pdev, 1, KBUILD_MODNAME);
	if (rc)
		return rc;

	rc = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (rc <= 0)
		return rc;

	pci_set_master(pdev);

	rc = plx_dma_create(pdev);
	if (rc)
		goto err_free_irq_vectors;

	pci_info(pdev, "PLX DMA Channel Registered\n");

	return 0;

err_free_irq_vectors:
	pci_free_irq_vectors(pdev);
	return rc;
}

static void plx_dma_remove(struct pci_dev *pdev)
{
	struct plx_dma_dev *plxdev = pci_get_drvdata(pdev);

	free_irq(pci_irq_vector(pdev, 0),  plxdev);

	plxdev->bar = NULL;
	dma_async_device_unregister(&plxdev->dma_dev);

	pci_free_irq_vectors(pdev);
}

static const struct pci_device_id plx_dma_pci_tbl[] = {
	{
		.vendor		= PCI_VENDOR_ID_PLX,
		.device		= 0x87D0,
		.subvendor	= PCI_ANY_ID,
		.subdevice	= PCI_ANY_ID,
		.class		= PCI_CLASS_SYSTEM_OTHER << 8,
		.class_mask	= 0xFFFFFFFF,
	},
	{0}
};
MODULE_DEVICE_TABLE(pci, plx_dma_pci_tbl);

static struct pci_driver plx_dma_pci_driver = {
	.name           = KBUILD_MODNAME,
	.id_table       = plx_dma_pci_tbl,
	.probe          = plx_dma_probe,
	.remove		= plx_dma_remove,
};
module_pci_driver(plx_dma_pci_driver);
