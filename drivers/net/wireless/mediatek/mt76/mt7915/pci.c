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

void mt7915_dual_hif_set_irq_mask(struct mt7915_dev *dev, bool write_reg,
				  u32 clear, u32 set)
{
	struct mt76_dev *mdev = &dev->mt76;
	unsigned long flags;

	spin_lock_irqsave(&mdev->mmio.irq_lock, flags);

	mdev->mmio.irqmask &= ~clear;
	mdev->mmio.irqmask |= set;

	if (write_reg) {
		mt76_wr(dev, MT_INT_MASK_CSR, mdev->mmio.irqmask);
		mt76_wr(dev, MT_INT1_MASK_CSR, mdev->mmio.irqmask);
	}

	spin_unlock_irqrestore(&mdev->mmio.irq_lock, flags);
}

static struct mt7915_hif *
mt7915_pci_get_hif2(struct mt7915_dev *dev)
{
	struct mt7915_hif *hif;
	u32 val;

	spin_lock_bh(&hif_lock);

	list_for_each_entry(hif, &hif_list, list) {
		val = readl(hif->regs + MT_PCIE_RECOG_ID);
		val &= MT_PCIE_RECOG_ID_MASK;
		if (val != dev->hif_idx)
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

static void
mt7915_rx_poll_complete(struct mt76_dev *mdev, enum mt76_rxq_id q)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	static const u32 rx_irq_mask[] = {
		[MT_RXQ_MAIN] = MT_INT_RX_DONE_DATA0,
		[MT_RXQ_EXT] = MT_INT_RX_DONE_DATA1,
		[MT_RXQ_MCU] = MT_INT_RX_DONE_WM,
		[MT_RXQ_MCU_WA] = MT_INT_RX_DONE_WA,
		[MT_RXQ_EXT_WA] = MT_INT_RX_DONE_WA_EXT,
	};

	mt7915_irq_enable(dev, rx_irq_mask[q]);
}

/* TODO: support 2/4/6/8 MSI-X vectors */
static void mt7915_irq_tasklet(struct tasklet_struct *t)
{
	struct mt7915_dev *dev = from_tasklet(dev, t, irq_tasklet);
	u32 intr, intr1, mask;

	mt76_wr(dev, MT_INT_MASK_CSR, 0);
	if (dev->hif2)
		mt76_wr(dev, MT_INT1_MASK_CSR, 0);

	intr = mt76_rr(dev, MT_INT_SOURCE_CSR);
	intr &= dev->mt76.mmio.irqmask;
	mt76_wr(dev, MT_INT_SOURCE_CSR, intr);

	if (dev->hif2) {
		intr1 = mt76_rr(dev, MT_INT1_SOURCE_CSR);
		intr1 &= dev->mt76.mmio.irqmask;
		mt76_wr(dev, MT_INT1_SOURCE_CSR, intr1);

		intr |= intr1;
	}

	trace_dev_irq(&dev->mt76, intr, dev->mt76.mmio.irqmask);

	mask = intr & MT_INT_RX_DONE_ALL;
	if (intr & MT_INT_TX_DONE_MCU)
		mask |= MT_INT_TX_DONE_MCU;

	mt7915_irq_disable(dev, mask);

	if (intr & MT_INT_TX_DONE_MCU)
		napi_schedule(&dev->mt76.tx_napi);

	if (intr & MT_INT_RX_DONE_DATA0)
		napi_schedule(&dev->mt76.napi[MT_RXQ_MAIN]);

	if (intr & MT_INT_RX_DONE_DATA1)
		napi_schedule(&dev->mt76.napi[MT_RXQ_EXT]);

	if (intr & MT_INT_RX_DONE_WM)
		napi_schedule(&dev->mt76.napi[MT_RXQ_MCU]);

	if (intr & MT_INT_RX_DONE_WA)
		napi_schedule(&dev->mt76.napi[MT_RXQ_MCU_WA]);

	if (intr & MT_INT_RX_DONE_WA_EXT)
		napi_schedule(&dev->mt76.napi[MT_RXQ_EXT_WA]);

	if (intr & MT_INT_MCU_CMD) {
		u32 val = mt76_rr(dev, MT_MCU_CMD);

		mt76_wr(dev, MT_MCU_CMD, val);
		if (val & MT_MCU_CMD_ERROR_MASK) {
			dev->reset_state = val;
			ieee80211_queue_work(mt76_hw(dev), &dev->reset_work);
			wake_up(&dev->reset_wait);
		}
	}
}

static irqreturn_t mt7915_irq_handler(int irq, void *dev_instance)
{
	struct mt7915_dev *dev = dev_instance;

	mt76_wr(dev, MT_INT_MASK_CSR, 0);
	if (dev->hif2)
		mt76_wr(dev, MT_INT1_MASK_CSR, 0);

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mphy.state))
		return IRQ_NONE;

	tasklet_schedule(&dev->irq_tasklet);

	return IRQ_HANDLED;
}

static void mt7915_pci_init_hif2(struct mt7915_dev *dev)
{
	struct mt7915_hif *hif;

	dev->hif_idx = ++hif_idx;
	if (!pci_get_device(PCI_VENDOR_ID_MEDIATEK, 0x7916, NULL))
		return;

	mt76_wr(dev, MT_PCIE_RECOG_ID, dev->hif_idx | MT_PCIE_RECOG_ID_SEM);

	hif = mt7915_pci_get_hif2(dev);
	if (!hif)
		return;

	dev->hif2 = hif;

	mt76_wr(dev, MT_INT1_MASK_CSR, 0);

	if (devm_request_irq(dev->mt76.dev, hif->irq, mt7915_irq_handler,
			     IRQF_SHARED, KBUILD_MODNAME "-hif", dev)) {
		mt7915_put_hif2(hif);
		hif = NULL;
	}

	/* master switch of PCIe tnterrupt enable */
	mt76_wr(dev, MT_PCIE1_MAC_INT_ENABLE, 0xff);
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
	static const struct mt76_driver_ops drv_ops = {
		/* txwi_size = txd size + txp size */
		.txwi_size = MT_TXD_SIZE + sizeof(struct mt7915_txp),
		.drv_flags = MT_DRV_TXWI_NO_FREE | MT_DRV_HW_MGMT_TXQ,
		.survey_flags = SURVEY_INFO_TIME_TX |
				SURVEY_INFO_TIME_RX |
				SURVEY_INFO_TIME_BSS_RX,
		.token_size = MT7915_TOKEN_SIZE,
		.tx_prepare_skb = mt7915_tx_prepare_skb,
		.tx_complete_skb = mt7915_tx_complete_skb,
		.rx_skb = mt7915_queue_rx_skb,
		.rx_check = mt7915_rx_check,
		.rx_poll_complete = mt7915_rx_poll_complete,
		.sta_ps = mt7915_sta_ps,
		.sta_add = mt7915_mac_sta_add,
		.sta_remove = mt7915_mac_sta_remove,
		.update_survey = mt7915_update_channel,
	};
	struct mt7915_dev *dev;
	struct mt76_dev *mdev;
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

	mdev = mt76_alloc_device(&pdev->dev, sizeof(*dev), &mt7915_ops,
				 &drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct mt7915_dev, mt76);

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		goto free;

	ret = mt7915_mmio_init(mdev, pcim_iomap_table(pdev)[0], pdev->irq);
	if (ret)
		goto error;

	tasklet_setup(&dev->irq_tasklet, mt7915_irq_tasklet);

	mt76_wr(dev, MT_INT_MASK_CSR, 0);

	/* master switch of PCIe tnterrupt enable */
	mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);

	ret = devm_request_irq(mdev->dev, pdev->irq, mt7915_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto error;

	mt7915_pci_init_hif2(dev);

	ret = mt7915_register_device(dev);
	if (ret)
		goto free_irq;

	return 0;
free_irq:
	devm_free_irq(mdev->dev, pdev->irq, dev);
error:
	pci_free_irq_vectors(pdev);
free:
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

static struct pci_driver mt7915_hif_driver = {
	.name		= KBUILD_MODNAME "_hif",
	.id_table	= mt7915_hif_device_table,
	.probe		= mt7915_pci_probe,
	.remove		= mt7915_hif_remove,
};

static struct pci_driver mt7915_pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt7915_pci_device_table,
	.probe		= mt7915_pci_probe,
	.remove		= mt7915_pci_remove,
};

static int __init mt7915_init(void)
{
	int ret;

	ret = pci_register_driver(&mt7915_hif_driver);
	if (ret)
		return ret;

	ret = pci_register_driver(&mt7915_pci_driver);
	if (ret)
		pci_unregister_driver(&mt7915_hif_driver);

	return ret;
}

static void __exit mt7915_exit(void)
{
    pci_unregister_driver(&mt7915_pci_driver);
    pci_unregister_driver(&mt7915_hif_driver);
}

module_init(mt7915_init);
module_exit(mt7915_exit);

MODULE_DEVICE_TABLE(pci, mt7915_pci_device_table);
MODULE_DEVICE_TABLE(pci, mt7915_hif_device_table);
MODULE_FIRMWARE(MT7915_FIRMWARE_WA);
MODULE_FIRMWARE(MT7915_FIRMWARE_WM);
MODULE_FIRMWARE(MT7915_ROM_PATCH);
MODULE_LICENSE("Dual BSD/GPL");
