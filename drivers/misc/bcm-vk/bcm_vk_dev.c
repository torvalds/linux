// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018-2020 Broadcom.
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>

#include "bcm_vk.h"

#define PCI_DEVICE_ID_VALKYRIE	0x5e87
#define PCI_DEVICE_ID_VIPER	0x5e88

/* MSIX usages */
#define VK_MSIX_MSGQ_MAX		3
#define VK_MSIX_NOTF_MAX		1
#define VK_MSIX_TTY_MAX			BCM_VK_NUM_TTY
#define VK_MSIX_IRQ_MAX			(VK_MSIX_MSGQ_MAX + VK_MSIX_NOTF_MAX + \
					 VK_MSIX_TTY_MAX)
#define VK_MSIX_IRQ_MIN_REQ             (VK_MSIX_MSGQ_MAX + VK_MSIX_NOTF_MAX)

/* Number of bits set in DMA mask*/
#define BCM_VK_DMA_BITS			64

static int bcm_vk_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err;
	int i;
	int irq;
	struct bcm_vk *vk;
	struct device *dev = &pdev->dev;

	vk = kzalloc(sizeof(*vk), GFP_KERNEL);
	if (!vk)
		return -ENOMEM;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev, "Cannot enable PCI device\n");
		goto err_free_exit;
	}
	vk->pdev = pci_dev_get(pdev);

	err = pci_request_regions(pdev, DRV_MODULE_NAME);
	if (err) {
		dev_err(dev, "Cannot obtain PCI resources\n");
		goto err_disable_pdev;
	}

	/* make sure DMA is good */
	err = dma_set_mask_and_coherent(&pdev->dev,
					DMA_BIT_MASK(BCM_VK_DMA_BITS));
	if (err) {
		dev_err(dev, "failed to set DMA mask\n");
		goto err_disable_pdev;
	}

	pci_set_master(pdev);
	pci_set_drvdata(pdev, vk);

	irq = pci_alloc_irq_vectors(pdev,
				    1,
				    VK_MSIX_IRQ_MAX,
				    PCI_IRQ_MSI | PCI_IRQ_MSIX);

	if (irq < VK_MSIX_IRQ_MIN_REQ) {
		dev_err(dev, "failed to get min %d MSIX interrupts, irq(%d)\n",
			VK_MSIX_IRQ_MIN_REQ, irq);
		err = (irq >= 0) ? -EINVAL : irq;
		goto err_disable_pdev;
	}

	if (irq != VK_MSIX_IRQ_MAX)
		dev_warn(dev, "Number of IRQs %d allocated - requested(%d).\n",
			 irq, VK_MSIX_IRQ_MAX);

	for (i = 0; i < MAX_BAR; i++) {
		/* multiple by 2 for 64 bit BAR mapping */
		vk->bar[i] = pci_ioremap_bar(pdev, i * 2);
		if (!vk->bar[i]) {
			dev_err(dev, "failed to remap BAR%d\n", i);
			goto err_iounmap;
		}
	}

	return 0;

err_iounmap:
	for (i = 0; i < MAX_BAR; i++) {
		if (vk->bar[i])
			pci_iounmap(pdev, vk->bar[i]);
	}
	pci_release_regions(pdev);

err_disable_pdev:
	pci_free_irq_vectors(pdev);
	pci_disable_device(pdev);
	pci_dev_put(pdev);

err_free_exit:
	kfree(vk);

	return err;
}

static void bcm_vk_remove(struct pci_dev *pdev)
{
	int i;
	struct bcm_vk *vk = pci_get_drvdata(pdev);

	for (i = 0; i < MAX_BAR; i++) {
		if (vk->bar[i])
			pci_iounmap(pdev, vk->bar[i]);
	}

	pci_release_regions(pdev);
	pci_free_irq_vectors(pdev);
	pci_disable_device(pdev);
}

static const struct pci_device_id bcm_vk_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_VALKYRIE), },
	{ PCI_DEVICE(PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_VIPER), },
	{ }
};
MODULE_DEVICE_TABLE(pci, bcm_vk_ids);

static struct pci_driver pci_driver = {
	.name     = DRV_MODULE_NAME,
	.id_table = bcm_vk_ids,
	.probe    = bcm_vk_probe,
	.remove   = bcm_vk_remove,
};
module_pci_driver(pci_driver);

MODULE_DESCRIPTION("Broadcom VK Host Driver");
MODULE_AUTHOR("Scott Branden <scott.branden@broadcom.com>");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
