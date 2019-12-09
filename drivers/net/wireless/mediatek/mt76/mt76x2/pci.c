// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "mt76x2.h"

static const struct pci_device_id mt76pci_device_table[] = {
	{ PCI_DEVICE(0x14c3, 0x7662) },
	{ PCI_DEVICE(0x14c3, 0x7612) },
	{ PCI_DEVICE(0x14c3, 0x7602) },
	{ },
};

static int
mt76pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	static const struct mt76_driver_ops drv_ops = {
		.txwi_size = sizeof(struct mt76x02_txwi),
		.drv_flags = MT_DRV_TX_ALIGNED4_SKBS |
			     MT_DRV_SW_RX_AIRTIME,
		.survey_flags = SURVEY_INFO_TIME_TX,
		.update_survey = mt76x02_update_channel,
		.tx_prepare_skb = mt76x02_tx_prepare_skb,
		.tx_complete_skb = mt76x02_tx_complete_skb,
		.rx_skb = mt76x02_queue_rx_skb,
		.rx_poll_complete = mt76x02_rx_poll_complete,
		.sta_ps = mt76x02_sta_ps,
		.sta_add = mt76x02_sta_add,
		.sta_remove = mt76x02_sta_remove,
	};
	struct mt76x02_dev *dev;
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

	mdev = mt76_alloc_device(&pdev->dev, sizeof(*dev), &mt76x2_ops,
				 &drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct mt76x02_dev, mt76);
	mt76_mmio_init(mdev, pcim_iomap_table(pdev)[0]);
	mt76x2_reset_wlan(dev, false);

	mdev->rev = mt76_rr(dev, MT_ASIC_VERSION);
	dev_info(mdev->dev, "ASIC revision: %08x\n", mdev->rev);

	ret = devm_request_irq(mdev->dev, pdev->irq, mt76x02_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto error;

	ret = mt76x2_register_device(dev);
	if (ret)
		goto error;

	/* Fix up ASPM configuration */

	/* RG_SSUSB_G1_CDR_BIR_LTR = 0x9 */
	mt76_rmw_field(dev, 0x15a10, 0x1f << 16, 0x9);

	/* RG_SSUSB_G1_CDR_BIC_LTR = 0xf */
	mt76_rmw_field(dev, 0x15a0c, 0xf << 28, 0xf);

	/* RG_SSUSB_CDR_BR_PE1D = 0x3 */
	mt76_rmw_field(dev, 0x15c58, 0x3 << 6, 0x3);

	mt76_pci_disable_aspm(pdev);

	return 0;

error:
	ieee80211_free_hw(mt76_hw(dev));
	return ret;
}

static void
mt76pci_remove(struct pci_dev *pdev)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);

	mt76_unregister_device(mdev);
	mt76x2_cleanup(dev);
	mt76_free_device(mdev);
}

MODULE_DEVICE_TABLE(pci, mt76pci_device_table);
MODULE_FIRMWARE(MT7662_FIRMWARE);
MODULE_FIRMWARE(MT7662_ROM_PATCH);
MODULE_LICENSE("Dual BSD/GPL");

static struct pci_driver mt76pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt76pci_device_table,
	.probe		= mt76pci_probe,
	.remove		= mt76pci_remove,
};

module_pci_driver(mt76pci_driver);
