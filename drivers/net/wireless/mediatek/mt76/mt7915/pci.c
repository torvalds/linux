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
	{ },
};

static const struct pci_device_id mt7915_hif_device_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7916) },
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
	if (!pci_get_device(PCI_VENDOR_ID_MEDIATEK, 0x7916, NULL))
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
	struct mt7915_hif *hif2;
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

	if (id->device == 0x7916)
		return mt7915_pci_hif2_probe(pdev);

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		return ret;

	hif2 = mt7915_pci_init_hif2(pdev);

	ret = mt7915_mmio_probe(&pdev->dev, pcim_iomap_table(pdev)[0],
				id->device, pdev->irq, hif2);
	if (!ret)
		return 0;

	pci_free_irq_vectors(pdev);

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
