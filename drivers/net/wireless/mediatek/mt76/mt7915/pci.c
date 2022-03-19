// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc.
 *
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "mt7915.h"
#include "mac.h"
#include "../trace.h"

static LIST_HEAD(hif_list);
static DEFINE_SPINLOCK(hif_lock);
static u32 hif_idx;

static const struct pci_device_id mt7915_pci_device_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7915) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7906) },
	{ },
};

static const struct pci_device_id mt7915_hif_device_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7916) },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x790a) },
	{ },
};

static struct mt7915_hif *mt7915_pci_get_hif2(u32 idx)
{
	struct mt7915_hif *hif;
	u32 val;

	spin_lock_bh(&hif_lock);

	list_for_each_entry(hif, &hif_list, list) {
		val = readl(hif->regs + MT_PCIE_RECOG_ID);
		val &= MT_PCIE_RECOG_ID_MASK;
		if (val != idx)
			continue;

		get_device(hif->dev);
		goto out;
	}
	hif = NULL;

out:
	spin_unlock_bh(&hif_lock);

	return hif;
}

static void mt7915_put_hif2(struct mt7915_hif *hif)
{
	if (!hif)
		return;

	put_device(hif->dev);
}

static struct mt7915_hif *mt7915_pci_init_hif2(struct pci_dev *pdev)
{
	hif_idx++;
	if (!pci_get_device(PCI_VENDOR_ID_MEDIATEK, 0x7916, NULL) &&
	    !pci_get_device(PCI_VENDOR_ID_MEDIATEK, 0x790a, NULL))
		return NULL;

	writel(hif_idx | MT_PCIE_RECOG_ID_SEM,
	       pcim_iomap_table(pdev)[0] + MT_PCIE_RECOG_ID);

	return mt7915_pci_get_hif2(hif_idx);
}

static int mt7915_pci_hif2_probe(struct pci_dev *pdev)
{
	struct mt7915_hif *hif;

	hif = devm_kzalloc(&pdev->dev, sizeof(*hif), GFP_KERNEL);
	if (!hif)
		return -ENOMEM;

	hif->dev = &pdev->dev;
	hif->regs = pcim_iomap_table(pdev)[0];
	hif->irq = pdev->irq;
	spin_lock_bh(&hif_lock);
	list_add(&hif->list, &hif_list);
	spin_unlock_bh(&hif_lock);
	pci_set_drvdata(pdev, hif);

	return 0;
}

static int mt7915_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	struct mt7915_dev *dev;
	struct mt76_dev *mdev;
	struct mt7915_hif *hif2;
	int irq;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	mt76_pci_disable_aspm(pdev);

	if (id->device == 0x7916 || id->device == 0x790a)
		return mt7915_pci_hif2_probe(pdev);

	dev = mt7915_mmio_probe(&pdev->dev, pcim_iomap_table(pdev)[0],
				id->device);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	mdev = &dev->mt76;
	mt7915_wfsys_reset(dev);
	hif2 = mt7915_pci_init_hif2(pdev);

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		goto free_device;

	irq = pdev->irq;
	ret = devm_request_irq(mdev->dev, irq, mt7915_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto free_irq_vector;

	mt76_wr(dev, MT_INT_MASK_CSR, 0);

	/* master switch of PCIe tnterrupt enable */
	mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);

	if (hif2) {
		dev->hif2 = hif2;

		mt76_wr(dev, MT_INT1_MASK_CSR, 0);
		/* master switch of PCIe tnterrupt enable */
		if (is_mt7915(mdev))
			mt76_wr(dev, MT_PCIE1_MAC_INT_ENABLE, 0xff);
		else
			mt76_wr(dev, MT_PCIE1_MAC_INT_ENABLE_MT7916, 0xff);

		ret = devm_request_irq(mdev->dev, dev->hif2->irq,
				       mt7915_irq_handler, IRQF_SHARED,
				       KBUILD_MODNAME "-hif", dev);
		if (ret)
			goto free_hif2;
	}

	ret = mt7915_register_device(dev);
	if (ret)
		goto free_hif2_irq;

	return 0;

free_hif2_irq:
	if (dev->hif2)
		devm_free_irq(mdev->dev, dev->hif2->irq, dev);
free_hif2:
	if (dev->hif2)
		put_device(dev->hif2->dev);
	devm_free_irq(mdev->dev, irq, dev);
free_irq_vector:
	pci_free_irq_vectors(pdev);
free_device:
	mt76_free_device(&dev->mt76);

	return ret;
}

static void mt7915_hif_remove(struct pci_dev *pdev)
{
	struct mt7915_hif *hif = pci_get_drvdata(pdev);

	list_del(&hif->list);
}

static void mt7915_pci_remove(struct pci_dev *pdev)
{
	struct mt76_dev *mdev;
	struct mt7915_dev *dev;

	mdev = pci_get_drvdata(pdev);
	dev = container_of(mdev, struct mt7915_dev, mt76);
	mt7915_put_hif2(dev->hif2);
	mt7915_unregister_device(dev);
}

struct pci_driver mt7915_hif_driver = {
	.name		= KBUILD_MODNAME "_hif",
	.id_table	= mt7915_hif_device_table,
	.probe		= mt7915_pci_probe,
	.remove		= mt7915_hif_remove,
};

struct pci_driver mt7915_pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt7915_pci_device_table,
	.probe		= mt7915_pci_probe,
	.remove		= mt7915_pci_remove,
};

MODULE_DEVICE_TABLE(pci, mt7915_pci_device_table);
MODULE_DEVICE_TABLE(pci, mt7915_hif_device_table);
MODULE_FIRMWARE(MT7915_FIRMWARE_WA);
MODULE_FIRMWARE(MT7915_FIRMWARE_WM);
MODULE_FIRMWARE(MT7915_ROM_PATCH);
MODULE_FIRMWARE(MT7916_FIRMWARE_WA);
MODULE_FIRMWARE(MT7916_FIRMWARE_WM);
MODULE_FIRMWARE(MT7916_ROM_PATCH);
