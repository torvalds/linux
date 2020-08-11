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

static const struct pci_device_id mt7915_pci_device_table[] = {
	{ PCI_DEVICE(0x14c3, 0x7915) },
	{ },
};

static void
mt7915_rx_poll_complete(struct mt76_dev *mdev, enum mt76_rxq_id q)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);

	mt7915_irq_enable(dev, MT_INT_RX_DONE(q));
}

/* TODO: support 2/4/6/8 MSI-X vectors */
static irqreturn_t mt7915_irq_handler(int irq, void *dev_instance)
{
	struct mt7915_dev *dev = dev_instance;
	u32 intr, mask;

	intr = mt76_rr(dev, MT_INT_SOURCE_CSR);
	intr &= dev->mt76.mmio.irqmask;
	mt76_wr(dev, MT_INT_SOURCE_CSR, intr);

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mphy.state))
		return IRQ_NONE;

	trace_dev_irq(&dev->mt76, intr, dev->mt76.mmio.irqmask);

	mask = intr & MT_INT_RX_DONE_ALL;
	if (intr & MT_INT_TX_DONE_ALL)
		mask |= MT_INT_TX_DONE_ALL;

	mt7915_irq_disable(dev, mask);

	if (intr & MT_INT_TX_DONE_ALL)
		napi_schedule(&dev->mt76.tx_napi);

	if (intr & MT_INT_RX_DONE_DATA)
		napi_schedule(&dev->mt76.napi[0]);

	if (intr & MT_INT_RX_DONE_WM)
		napi_schedule(&dev->mt76.napi[1]);

	if (intr & MT_INT_RX_DONE_WA)
		napi_schedule(&dev->mt76.napi[2]);

	if (intr & MT_INT_MCU_CMD) {
		u32 val = mt76_rr(dev, MT_MCU_CMD);

		mt76_wr(dev, MT_MCU_CMD, val);
		if (val & MT_MCU_CMD_ERROR_MASK) {
			dev->reset_state = val;
			ieee80211_queue_work(mt76_hw(dev), &dev->reset_work);
			wake_up(&dev->reset_wait);
		}
	}

	return IRQ_HANDLED;
}

static int
mt7915_alloc_device(struct pci_dev *pdev, struct mt7915_dev *dev)
{
#define NUM_BANDS	2
	int i;
	s8 **sku;

	sku = devm_kzalloc(&pdev->dev, NUM_BANDS * sizeof(*sku), GFP_KERNEL);
	if (!sku)
		return -ENOMEM;

	for (i = 0; i < NUM_BANDS; i++) {
		sku[i] = devm_kzalloc(&pdev->dev, MT7915_SKU_TABLE_SIZE *
				      sizeof(**sku), GFP_KERNEL);
		if (!sku[i])
			return -ENOMEM;
	}
	dev->rate_power = sku;

	return 0;
}

static int mt7915_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	static const struct mt76_driver_ops drv_ops = {
		/* txwi_size = txd size + txp size */
		.txwi_size = MT_TXD_SIZE + sizeof(struct mt7915_txp),
		.drv_flags = MT_DRV_TXWI_NO_FREE | MT_DRV_HW_MGMT_TXQ |
			     MT_DRV_AMSDU_OFFLOAD,
		.survey_flags = SURVEY_INFO_TIME_TX |
				SURVEY_INFO_TIME_RX |
				SURVEY_INFO_TIME_BSS_RX,
		.tx_prepare_skb = mt7915_tx_prepare_skb,
		.tx_complete_skb = mt7915_tx_complete_skb,
		.rx_skb = mt7915_queue_rx_skb,
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

	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	mdev = mt76_alloc_device(&pdev->dev, sizeof(*dev), &mt7915_ops,
				 &drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct mt7915_dev, mt76);
	ret = mt7915_alloc_device(pdev, dev);
	if (ret)
		return ret;

	mt76_mmio_init(&dev->mt76, pcim_iomap_table(pdev)[0]);
	mdev->rev = (mt7915_l1_rr(dev, MT_HW_CHIPID) << 16) |
		    (mt7915_l1_rr(dev, MT_HW_REV) & 0xff);
	dev_dbg(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	mt76_wr(dev, MT_INT_MASK_CSR, 0);

	/* master switch of PCIe tnterrupt enable */
	mt7915_l1_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);

	ret = devm_request_irq(mdev->dev, pdev->irq, mt7915_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto error;

	ret = mt7915_register_device(dev);
	if (ret)
		goto error;

	return 0;
error:
	ieee80211_free_hw(mt76_hw(dev));
	return ret;
}

static void mt7915_pci_remove(struct pci_dev *pdev)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);

	mt7915_unregister_device(dev);
}

struct pci_driver mt7915_pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt7915_pci_device_table,
	.probe		= mt7915_pci_probe,
	.remove		= mt7915_pci_remove,
};

module_pci_driver(mt7915_pci_driver);

MODULE_DEVICE_TABLE(pci, mt7915_pci_device_table);
MODULE_FIRMWARE(MT7915_FIRMWARE_WA);
MODULE_FIRMWARE(MT7915_FIRMWARE_WM);
MODULE_FIRMWARE(MT7915_ROM_PATCH);
MODULE_LICENSE("Dual BSD/GPL");
