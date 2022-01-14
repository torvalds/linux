// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc.
 *
 * Author: Felix Fietkau <nbd@nbd.name>
 *	   Lorenzo Bianconi <lorenzo@kernel.org>
 *	   Sean Wang <sean.wang@mediatek.com>
 */

#include <linux/kernel.h>
#include <linux/iopoll.h>
#include <linux/module.h>

#include <linux/mmc/host.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>

#include "../sdio.h"
#include "mt7615.h"
#include "mac.h"
#include "mcu.h"

static const struct sdio_device_id mt7663s_table[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, 0x7603) },
	{ }	/* Terminating entry */
};

static void mt7663s_txrx_worker(struct mt76_worker *w)
{
	struct mt76_sdio *sdio = container_of(w, struct mt76_sdio,
					      txrx_worker);
	struct mt76_dev *mdev = container_of(sdio, struct mt76_dev, sdio);
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);

	if (!mt76_connac_pm_ref(&dev->mphy, &dev->pm)) {
		queue_work(mdev->wq, &dev->pm.wake_work);
		return;
	}
	mt76s_txrx_worker(sdio);
	mt76_connac_pm_unref(&dev->mphy, &dev->pm);
}

static void mt7663s_init_work(struct work_struct *work)
{
	struct mt7615_dev *dev;

	dev = container_of(work, struct mt7615_dev, mcu_work);
	if (mt7663s_mcu_init(dev))
		return;

	mt7615_init_work(dev);
}

static int mt7663s_parse_intr(struct mt76_dev *dev, struct mt76s_intr *intr)
{
	struct mt76_sdio *sdio = &dev->sdio;
	struct mt7663s_intr *irq_data = sdio->intr_data;
	int i, err;

	sdio_claim_host(sdio->func);
	err = sdio_readsb(sdio->func, irq_data, MCR_WHISR, sizeof(*irq_data));
	sdio_release_host(sdio->func);

	if (err)
		return err;

	intr->isr = irq_data->isr;
	intr->rec_mb = irq_data->rec_mb;
	intr->tx.wtqcr = irq_data->tx.wtqcr;
	intr->rx.num = irq_data->rx.num;
	for (i = 0; i < 2 ; i++)
		intr->rx.len[i] = irq_data->rx.len[i];

	return 0;
}

static int mt7663s_probe(struct sdio_func *func,
			 const struct sdio_device_id *id)
{
	static const struct mt76_driver_ops drv_ops = {
		.txwi_size = MT_USB_TXD_SIZE,
		.drv_flags = MT_DRV_RX_DMA_HDR,
		.tx_prepare_skb = mt7663_usb_sdio_tx_prepare_skb,
		.tx_complete_skb = mt7663_usb_sdio_tx_complete_skb,
		.tx_status_data = mt7663_usb_sdio_tx_status_data,
		.rx_skb = mt7615_queue_rx_skb,
		.sta_ps = mt7615_sta_ps,
		.sta_add = mt7615_mac_sta_add,
		.sta_remove = mt7615_mac_sta_remove,
		.update_survey = mt7615_update_channel,
	};
	static const struct mt76_bus_ops mt7663s_ops = {
		.rr = mt76s_rr,
		.rmw = mt76s_rmw,
		.wr = mt76s_wr,
		.write_copy = mt76s_write_copy,
		.read_copy = mt76s_read_copy,
		.wr_rp = mt76s_wr_rp,
		.rd_rp = mt76s_rd_rp,
		.type = MT76_BUS_SDIO,
	};
	struct ieee80211_ops *ops;
	struct mt7615_dev *dev;
	struct mt76_dev *mdev;
	int ret;

	ops = devm_kmemdup(&func->dev, &mt7615_ops, sizeof(mt7615_ops),
			   GFP_KERNEL);
	if (!ops)
		return -ENOMEM;

	mdev = mt76_alloc_device(&func->dev, sizeof(*dev), ops, &drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct mt7615_dev, mt76);

	INIT_WORK(&dev->mcu_work, mt7663s_init_work);
	dev->reg_map = mt7663_usb_sdio_reg_map;
	dev->ops = ops;
	sdio_set_drvdata(func, dev);

	ret = mt76s_init(mdev, func, &mt7663s_ops);
	if (ret < 0)
		goto error;

	ret = mt76s_hw_init(mdev, func, MT76_CONNAC_SDIO);
	if (ret)
		goto error;

	mdev->rev = (mt76_rr(dev, MT_HW_CHIPID) << 16) |
		    (mt76_rr(dev, MT_HW_REV) & 0xff);
	dev_dbg(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	mdev->sdio.parse_irq = mt7663s_parse_intr;
	mdev->sdio.intr_data = devm_kmalloc(mdev->dev,
					    sizeof(struct mt7663s_intr),
					    GFP_KERNEL);
	if (!mdev->sdio.intr_data) {
		ret = -ENOMEM;
		goto error;
	}

	ret = mt76s_alloc_rx_queue(mdev, MT_RXQ_MAIN);
	if (ret)
		goto error;

	ret = mt76s_alloc_tx(mdev);
	if (ret)
		goto error;

	ret = mt76_worker_setup(mt76_hw(dev), &mdev->sdio.txrx_worker,
				mt7663s_txrx_worker, "sdio-txrx");
	if (ret)
		goto error;

	sched_set_fifo_low(mdev->sdio.txrx_worker.task);

	ret = mt7663_usb_sdio_register_device(dev);
	if (ret)
		goto error;

	return 0;

error:
	mt76s_deinit(&dev->mt76);
	mt76_free_device(&dev->mt76);

	return ret;
}

static void mt7663s_remove(struct sdio_func *func)
{
	struct mt7615_dev *dev = sdio_get_drvdata(func);

	if (!test_and_clear_bit(MT76_STATE_INITIALIZED, &dev->mphy.state))
		return;

	ieee80211_unregister_hw(dev->mt76.hw);
	mt76s_deinit(&dev->mt76);
	mt76_free_device(&dev->mt76);
}

#ifdef CONFIG_PM
static int mt7663s_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct mt7615_dev *mdev = sdio_get_drvdata(func);
	int err;

	if (!test_bit(MT76_STATE_SUSPEND, &mdev->mphy.state) &&
	    mt7615_firmware_offload(mdev)) {
		int err;

		err = mt76_connac_mcu_set_hif_suspend(&mdev->mt76, true);
		if (err < 0)
			return err;
	}

	sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);

	err = mt7615_mcu_set_fw_ctrl(mdev);
	if (err)
		return err;

	mt76_worker_disable(&mdev->mt76.sdio.txrx_worker);
	mt76_worker_disable(&mdev->mt76.sdio.status_worker);
	mt76_worker_disable(&mdev->mt76.sdio.net_worker);

	cancel_work_sync(&mdev->mt76.sdio.stat_work);
	clear_bit(MT76_READING_STATS, &mdev->mphy.state);

	mt76_tx_status_check(&mdev->mt76, true);

	return 0;
}

static int mt7663s_resume(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct mt7615_dev *mdev = sdio_get_drvdata(func);
	int err;

	mt76_worker_enable(&mdev->mt76.sdio.txrx_worker);
	mt76_worker_enable(&mdev->mt76.sdio.status_worker);
	mt76_worker_enable(&mdev->mt76.sdio.net_worker);

	err = mt7615_mcu_set_drv_ctrl(mdev);
	if (err)
		return err;

	if (!test_bit(MT76_STATE_SUSPEND, &mdev->mphy.state) &&
	    mt7615_firmware_offload(mdev))
		err = mt76_connac_mcu_set_hif_suspend(&mdev->mt76, false);

	return err;
}

static const struct dev_pm_ops mt7663s_pm_ops = {
	.suspend = mt7663s_suspend,
	.resume = mt7663s_resume,
};
#endif

MODULE_DEVICE_TABLE(sdio, mt7663s_table);
MODULE_FIRMWARE(MT7663_OFFLOAD_FIRMWARE_N9);
MODULE_FIRMWARE(MT7663_OFFLOAD_ROM_PATCH);
MODULE_FIRMWARE(MT7663_FIRMWARE_N9);
MODULE_FIRMWARE(MT7663_ROM_PATCH);

static struct sdio_driver mt7663s_driver = {
	.name		= KBUILD_MODNAME,
	.probe		= mt7663s_probe,
	.remove		= mt7663s_remove,
	.id_table	= mt7663s_table,
#ifdef CONFIG_PM
	.drv = {
		.pm = &mt7663s_pm_ops,
	}
#endif
};
module_sdio_driver(mt7663s_driver);

MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_LICENSE("Dual BSD/GPL");
