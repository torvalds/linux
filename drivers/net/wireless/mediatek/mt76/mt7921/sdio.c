// SPDX-License-Identifier: ISC
/* Copyright (C) 2021 MediaTek Inc.
 *
 */

#include <linux/kernel.h>
#include <linux/iopoll.h>
#include <linux/module.h>

#include <linux/mmc/host.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>

#include "mt7921.h"
#include "../sdio.h"
#include "mac.h"
#include "mcu.h"

static const struct sdio_device_id mt7921s_table[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, 0x7901) },
	{ }	/* Terminating entry */
};

static void mt7921s_txrx_worker(struct mt76_worker *w)
{
	struct mt76_sdio *sdio = container_of(w, struct mt76_sdio,
					      txrx_worker);
	struct mt76_dev *mdev = container_of(sdio, struct mt76_dev, sdio);
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);

	if (!mt76_connac_pm_ref(&dev->mphy, &dev->pm)) {
		queue_work(mdev->wq, &dev->pm.wake_work);
		return;
	}

	mt76s_txrx_worker(sdio);
	mt76_connac_pm_unref(&dev->mphy, &dev->pm);
}

static void mt7921s_unregister_device(struct mt7921_dev *dev)
{
	struct mt76_connac_pm *pm = &dev->pm;

	cancel_work_sync(&dev->init_work);
	mt76_unregister_device(&dev->mt76);
	cancel_delayed_work_sync(&pm->ps_work);
	cancel_work_sync(&pm->wake_work);

	mt76s_deinit(&dev->mt76);
	mt7921s_wfsys_reset(dev);
	skb_queue_purge(&dev->mt76.mcu.res_q);

	mt76_free_device(&dev->mt76);
}

static int mt7921s_parse_intr(struct mt76_dev *dev, struct mt76s_intr *intr)
{
	struct mt76_sdio *sdio = &dev->sdio;
	struct mt7921_sdio_intr *irq_data = sdio->intr_data;
	int i, err;

	sdio_claim_host(sdio->func);
	err = sdio_readsb(sdio->func, irq_data, MCR_WHISR, sizeof(*irq_data));
	sdio_release_host(sdio->func);

	if (err < 0)
		return err;

	if (irq_data->rx.num[0] > 16 ||
	    irq_data->rx.num[1] > 128)
		return -EINVAL;

	intr->isr = irq_data->isr;
	intr->rec_mb = irq_data->rec_mb;
	intr->tx.wtqcr = irq_data->tx.wtqcr;
	intr->rx.num = irq_data->rx.num;
	for (i = 0; i < 2 ; i++) {
		if (!i)
			intr->rx.len[0] = irq_data->rx.len0;
		else
			intr->rx.len[1] = irq_data->rx.len1;
	}

	return 0;
}

static int mt7921s_probe(struct sdio_func *func,
			 const struct sdio_device_id *id)
{
	static const struct mt76_driver_ops drv_ops = {
		.txwi_size = MT_SDIO_TXD_SIZE,
		.survey_flags = SURVEY_INFO_TIME_TX |
				SURVEY_INFO_TIME_RX |
				SURVEY_INFO_TIME_BSS_RX,
		.tx_prepare_skb = mt7921_usb_sdio_tx_prepare_skb,
		.tx_complete_skb = mt7921_usb_sdio_tx_complete_skb,
		.tx_status_data = mt7921_usb_sdio_tx_status_data,
		.rx_skb = mt7921_queue_rx_skb,
		.rx_check = mt7921_rx_check,
		.sta_ps = mt7921_sta_ps,
		.sta_add = mt7921_mac_sta_add,
		.sta_assoc = mt7921_mac_sta_assoc,
		.sta_remove = mt7921_mac_sta_remove,
		.update_survey = mt7921_update_channel,
	};
	static const struct mt76_bus_ops mt7921s_ops = {
		.rr = mt76s_rr,
		.rmw = mt76s_rmw,
		.wr = mt76s_wr,
		.write_copy = mt76s_write_copy,
		.read_copy = mt76s_read_copy,
		.wr_rp = mt76s_wr_rp,
		.rd_rp = mt76s_rd_rp,
		.type = MT76_BUS_SDIO,
	};
	static const struct mt7921_hif_ops mt7921_sdio_ops = {
		.init_reset = mt7921s_init_reset,
		.reset = mt7921s_mac_reset,
		.mcu_init = mt7921s_mcu_init,
		.drv_own = mt7921s_mcu_drv_pmctrl,
		.fw_own = mt7921s_mcu_fw_pmctrl,
	};

	struct mt7921_dev *dev;
	struct mt76_dev *mdev;
	int ret;

	mdev = mt76_alloc_device(&func->dev, sizeof(*dev), &mt7921_ops,
				 &drv_ops);
	if (!mdev)
		return -ENOMEM;

	dev = container_of(mdev, struct mt7921_dev, mt76);
	dev->hif_ops = &mt7921_sdio_ops;

	sdio_set_drvdata(func, dev);

	ret = mt76s_init(mdev, func, &mt7921s_ops);
	if (ret < 0)
		goto error;

	ret = mt76s_hw_init(mdev, func, MT76_CONNAC2_SDIO);
	if (ret)
		goto error;

	mdev->rev = (mt76_rr(dev, MT_HW_CHIPID) << 16) |
		    (mt76_rr(dev, MT_HW_REV) & 0xff);
	dev_dbg(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	mdev->sdio.parse_irq = mt7921s_parse_intr;
	mdev->sdio.intr_data = devm_kmalloc(mdev->dev,
					    sizeof(struct mt7921_sdio_intr),
					    GFP_KERNEL);
	if (!mdev->sdio.intr_data) {
		ret = -ENOMEM;
		goto error;
	}

	ret = mt76s_alloc_rx_queue(mdev, MT_RXQ_MAIN);
	if (ret)
		goto error;

	ret = mt76s_alloc_rx_queue(mdev, MT_RXQ_MCU);
	if (ret)
		goto error;

	ret = mt76s_alloc_tx(mdev);
	if (ret)
		goto error;

	ret = mt76_worker_setup(mt76_hw(dev), &mdev->sdio.txrx_worker,
				mt7921s_txrx_worker, "sdio-txrx");
	if (ret)
		goto error;

	sched_set_fifo_low(mdev->sdio.txrx_worker.task);

	ret = mt7921_register_device(dev);
	if (ret)
		goto error;

	return 0;

error:
	mt76s_deinit(&dev->mt76);
	mt76_free_device(&dev->mt76);

	return ret;
}

static void mt7921s_remove(struct sdio_func *func)
{
	struct mt7921_dev *dev = sdio_get_drvdata(func);

	mt7921s_unregister_device(dev);
}

static int mt7921s_suspend(struct device *__dev)
{
	struct sdio_func *func = dev_to_sdio_func(__dev);
	struct mt7921_dev *dev = sdio_get_drvdata(func);
	struct mt76_connac_pm *pm = &dev->pm;
	struct mt76_dev *mdev = &dev->mt76;
	int err;

	pm->suspended = true;
	set_bit(MT76_STATE_SUSPEND, &mdev->phy.state);

	flush_work(&dev->reset_work);
	cancel_delayed_work_sync(&pm->ps_work);
	cancel_work_sync(&pm->wake_work);

	err = mt7921_mcu_drv_pmctrl(dev);
	if (err < 0)
		goto restore_suspend;

	/* always enable deep sleep during suspend to reduce
	 * power consumption
	 */
	mt76_connac_mcu_set_deep_sleep(mdev, true);

	mt76_txq_schedule_all(&dev->mphy);
	mt76_worker_disable(&mdev->tx_worker);
	mt76_worker_disable(&mdev->sdio.status_worker);
	cancel_work_sync(&mdev->sdio.stat_work);
	clear_bit(MT76_READING_STATS, &dev->mphy.state);
	mt76_tx_status_check(mdev, true);

	mt76_worker_schedule(&mdev->sdio.txrx_worker);
	wait_event_timeout(dev->mt76.sdio.wait,
			   mt76s_txqs_empty(&dev->mt76), 5 * HZ);

	/* It is supposed that SDIO bus is idle at the point */
	err = mt76_connac_mcu_set_hif_suspend(mdev, true);
	if (err)
		goto restore_worker;

	mt76_worker_disable(&mdev->sdio.txrx_worker);
	mt76_worker_disable(&mdev->sdio.net_worker);

	err = mt7921_mcu_fw_pmctrl(dev);
	if (err)
		goto restore_txrx_worker;

	sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);

	return 0;

restore_txrx_worker:
	mt76_worker_enable(&mdev->sdio.net_worker);
	mt76_worker_enable(&mdev->sdio.txrx_worker);
	mt76_connac_mcu_set_hif_suspend(mdev, false);

restore_worker:
	mt76_worker_enable(&mdev->tx_worker);
	mt76_worker_enable(&mdev->sdio.status_worker);

	if (!pm->ds_enable)
		mt76_connac_mcu_set_deep_sleep(mdev, false);

restore_suspend:
	clear_bit(MT76_STATE_SUSPEND, &mdev->phy.state);
	pm->suspended = false;

	if (err < 0)
		mt7921_reset(&dev->mt76);

	return err;
}

static int mt7921s_resume(struct device *__dev)
{
	struct sdio_func *func = dev_to_sdio_func(__dev);
	struct mt7921_dev *dev = sdio_get_drvdata(func);
	struct mt76_connac_pm *pm = &dev->pm;
	struct mt76_dev *mdev = &dev->mt76;
	int err;

	clear_bit(MT76_STATE_SUSPEND, &mdev->phy.state);

	err = mt7921_mcu_drv_pmctrl(dev);
	if (err < 0)
		goto failed;

	mt76_worker_enable(&mdev->tx_worker);
	mt76_worker_enable(&mdev->sdio.txrx_worker);
	mt76_worker_enable(&mdev->sdio.status_worker);
	mt76_worker_enable(&mdev->sdio.net_worker);

	/* restore previous ds setting */
	if (!pm->ds_enable)
		mt76_connac_mcu_set_deep_sleep(mdev, false);

	err = mt76_connac_mcu_set_hif_suspend(mdev, false);
failed:
	pm->suspended = false;

	if (err < 0)
		mt7921_reset(&dev->mt76);

	return err;
}

MODULE_DEVICE_TABLE(sdio, mt7921s_table);
MODULE_FIRMWARE(MT7921_FIRMWARE_WM);
MODULE_FIRMWARE(MT7921_ROM_PATCH);

static DEFINE_SIMPLE_DEV_PM_OPS(mt7921s_pm_ops, mt7921s_suspend, mt7921s_resume);

static struct sdio_driver mt7921s_driver = {
	.name		= KBUILD_MODNAME,
	.probe		= mt7921s_probe,
	.remove		= mt7921s_remove,
	.id_table	= mt7921s_table,
	.drv.pm		= pm_sleep_ptr(&mt7921s_pm_ops),
};
module_sdio_driver(mt7921s_driver);
MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_LICENSE("Dual BSD/GPL");
