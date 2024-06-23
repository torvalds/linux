// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) Tehuti Networks Ltd. */

#include <linux/pci.h>

#include "tn40.h"

static int tn40_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int ret;

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(&pdev->dev, "failed to set DMA mask.\n");
		goto err_disable_device;
	}
	return 0;
err_disable_device:
	pci_disable_device(pdev);
	return ret;
}

static void tn40_remove(struct pci_dev *pdev)
{
	pci_disable_device(pdev);
}

static const struct pci_device_id tn40_id_table[] = {
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_TEHUTI, 0x4022,
			 PCI_VENDOR_ID_TEHUTI, 0x3015) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_TEHUTI, 0x4022,
			 PCI_VENDOR_ID_DLINK, 0x4d00) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_TEHUTI, 0x4022,
			 PCI_VENDOR_ID_ASUSTEK, 0x8709) },
	{ PCI_DEVICE_SUB(PCI_VENDOR_ID_TEHUTI, 0x4022,
			 PCI_VENDOR_ID_EDIMAX, 0x8103) },
	{ }
};

static struct pci_driver tn40_driver = {
	.name = TN40_DRV_NAME,
	.id_table = tn40_id_table,
	.probe = tn40_probe,
	.remove = tn40_remove,
};

module_pci_driver(tn40_driver);

MODULE_DEVICE_TABLE(pci, tn40_id_table);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Tehuti Network TN40xx Driver");
