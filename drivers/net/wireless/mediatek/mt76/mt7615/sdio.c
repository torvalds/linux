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

#include "mt7615.h"
#include "sdio.h"
#include "mac.h"
#include "mcu.h"

static const struct sdio_device_id mt7663s_table[] = {
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, 0x7603) },
	{ }	/* Terminating entry */
};

static u32 mt7663s_read_whisr(struct mt76_dev *dev)
{
	return sdio_readl(dev->sdio.func, MCR_WHISR, NULL);
}

u32 mt7663s_read_pcr(struct mt7615_dev *dev)
{
	struct mt76_sdio *sdio = &dev->mt76.sdio;

	return sdio_readl(sdio->func, MCR_WHLPCR, NULL);
}

static u32 mt7663s_read_mailbox(struct mt76_dev *dev, u32 offset)
{
	struct sdio_func *func = dev->sdio.func;
	u32 val = ~0, status;
	int err;

	sdio_claim_host(func);

	sdio_writel(func, offset, MCR_H2DSM0R, &err);
	if (err < 0) {
		dev_err(dev->dev, "failed setting address [err=%d]\n", err);
		goto out;
	}

	sdio_writel(func, H2D_SW_INT_READ, MCR_WSICR, &err);
	if (err < 0) {
		dev_err(dev->dev, "failed setting read mode [err=%d]\n", err);
		goto out;
	}

	err = readx_poll_timeout(mt7663s_read_whisr, dev, status,
				 status & H2D_SW_INT_READ, 0, 1000000);
	if (err < 0) {
		dev_err(dev->dev, "query whisr timeout\n");
		goto out;
	}

	sdio_writel(func, H2D_SW_INT_READ, MCR_WHISR, &err);
	if (err < 0) {
		dev_err(dev->dev, "failed setting read mode [err=%d]\n", err);
		goto out;
	}

	val = sdio_readl(func, MCR_H2DSM0R, &err);
	if (err < 0) {
		dev_err(dev->dev, "failed reading h2dsm0r [err=%d]\n", err);
		goto out;
	}

	if (val != offset) {
		dev_err(dev->dev, "register mismatch\n");
		val = ~0;
		goto out;
	}

	val = sdio_readl(func, MCR_D2HRM1R, &err);
	if (err < 0)
		dev_err(dev->dev, "failed reading d2hrm1r [err=%d]\n", err);

out:
	sdio_release_host(func);

	return val;
}

static void mt7663s_write_mailbox(struct mt76_dev *dev, u32 offset, u32 val)
{
	struct sdio_func *func = dev->sdio.func;
	u32 status;
	int err;

	sdio_claim_host(func);

	sdio_writel(func, offset, MCR_H2DSM0R, &err);
	if (err < 0) {
		dev_err(dev->dev, "failed setting address [err=%d]\n", err);
		goto out;
	}

	sdio_writel(func, val, MCR_H2DSM1R, &err);
	if (err < 0) {
		dev_err(dev->dev,
			"failed setting write value [err=%d]\n", err);
		goto out;
	}

	sdio_writel(func, H2D_SW_INT_WRITE, MCR_WSICR, &err);
	if (err < 0) {
		dev_err(dev->dev, "failed setting write mode [err=%d]\n", err);
		goto out;
	}

	err = readx_poll_timeout(mt7663s_read_whisr, dev, status,
				 status & H2D_SW_INT_WRITE, 0, 1000000);
	if (err < 0) {
		dev_err(dev->dev, "query whisr timeout\n");
		goto out;
	}

	sdio_writel(func, H2D_SW_INT_WRITE, MCR_WHISR, &err);
	if (err < 0) {
		dev_err(dev->dev, "failed setting write mode [err=%d]\n", err);
		goto out;
	}

	val = sdio_readl(func, MCR_H2DSM0R, &err);
	if (err < 0) {
		dev_err(dev->dev, "failed reading h2dsm0r [err=%d]\n", err);
		goto out;
	}

	if (val != offset)
		dev_err(dev->dev, "register mismatch\n");

out:
	sdio_release_host(func);
}

static u32 mt7663s_rr(struct mt76_dev *dev, u32 offset)
{
	if (test_bit(MT76_STATE_MCU_RUNNING, &dev->phy.state))
		return dev->mcu_ops->mcu_rr(dev, offset);
	else
		return mt7663s_read_mailbox(dev, offset);
}

static void mt7663s_wr(struct mt76_dev *dev, u32 offset, u32 val)
{
	if (test_bit(MT76_STATE_MCU_RUNNING, &dev->phy.state))
		dev->mcu_ops->mcu_wr(dev, offset, val);
	else
		mt7663s_write_mailbox(dev, offset, val);
}

static u32 mt7663s_rmw(struct mt76_dev *dev, u32 offset, u32 mask, u32 val)
{
	val |= mt7663s_rr(dev, offset) & ~mask;
	mt7663s_wr(dev, offset, val);

	return val;
}

static void mt7663s_write_copy(struct mt76_dev *dev, u32 offset,
			       const void *data, int len)
{
	const u32 *val = data;
	int i;

	for (i = 0; i < len / sizeof(u32); i++) {
		mt7663s_wr(dev, offset, val[i]);
		offset += sizeof(u32);
	}
}

static void mt7663s_read_copy(struct mt76_dev *dev, u32 offset,
			      void *data, int len)
{
	u32 *val = data;
	int i;

	for (i = 0; i < len / sizeof(u32); i++) {
		val[i] = mt7663s_rr(dev, offset);
		offset += sizeof(u32);
	}
}

static int mt7663s_wr_rp(struct mt76_dev *dev, u32 base,
			 const struct mt76_reg_pair *data,
			 int len)
{
	int i;

	for (i = 0; i < len; i++) {
		mt7663s_wr(dev, data->reg, data->value);
		data++;
	}

	return 0;
}

static int mt7663s_rd_rp(struct mt76_dev *dev, u32 base,
			 struct mt76_reg_pair *data,
			 int len)
{
	int i;

	for (i = 0; i < len; i++) {
		data->value = mt7663s_rr(dev, data->reg);
		data++;
	}

	return 0;
}

static void mt7663s_init_work(struct work_struct *work)
{
	struct mt7615_dev *dev;

	dev = container_of(work, struct mt7615_dev, mcu_work);
	if (mt7663s_mcu_init(dev))
		return;

	mt7615_init_work(dev);
}

static int mt7663s_hw_init(struct mt7615_dev *dev, struct sdio_func *func)
{
	u32 status, ctrl;
	int ret;

	sdio_claim_host(func);

	ret = sdio_enable_func(func);
	if (ret < 0)
		goto release;

	/* Get ownership from the device */
	sdio_writel(func, WHLPCR_INT_EN_CLR | WHLPCR_FW_OWN_REQ_CLR,
		    MCR_WHLPCR, &ret);
	if (ret < 0)
		goto disable_func;

	ret = readx_poll_timeout(mt7663s_read_pcr, dev, status,
				 status & WHLPCR_IS_DRIVER_OWN, 2000, 1000000);
	if (ret < 0) {
		dev_err(dev->mt76.dev, "Cannot get ownership from device");
		goto disable_func;
	}

	ret = sdio_set_block_size(func, 512);
	if (ret < 0)
		goto disable_func;

	/* Enable interrupt */
	sdio_writel(func, WHLPCR_INT_EN_SET, MCR_WHLPCR, &ret);
	if (ret < 0)
		goto disable_func;

	ctrl = WHIER_RX0_DONE_INT_EN | WHIER_TX_DONE_INT_EN;
	sdio_writel(func, ctrl, MCR_WHIER, &ret);
	if (ret < 0)
		goto disable_func;

	/* set WHISR as read clear and Rx aggregation number as 16 */
	ctrl = FIELD_PREP(MAX_HIF_RX_LEN_NUM, 16);
	sdio_writel(func, ctrl, MCR_WHCR, &ret);
	if (ret < 0)
		goto disable_func;

	ret = sdio_claim_irq(func, mt7663s_sdio_irq);
	if (ret < 0)
		goto disable_func;

	sdio_release_host(func);

	return 0;

disable_func:
	sdio_disable_func(func);
release:
	sdio_release_host(func);

	return ret;
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
		.rr = mt7663s_rr,
		.rmw = mt7663s_rmw,
		.wr = mt7663s_wr,
		.write_copy = mt7663s_write_copy,
		.read_copy = mt7663s_read_copy,
		.wr_rp = mt7663s_wr_rp,
		.rd_rp = mt7663s_rd_rp,
		.type = MT76_BUS_SDIO,
	};
	struct ieee80211_ops *ops;
	struct mt7615_dev *dev;
	struct mt76_dev *mdev;
	int i, ret;

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

	ret = mt7663s_hw_init(dev, func);
	if (ret)
		goto error;

	mdev->rev = (mt76_rr(dev, MT_HW_CHIPID) << 16) |
		    (mt76_rr(dev, MT_HW_REV) & 0xff);
	dev_dbg(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	mdev->sdio.intr_data = devm_kmalloc(mdev->dev,
					    sizeof(struct mt76s_intr),
					    GFP_KERNEL);
	if (!mdev->sdio.intr_data) {
		ret = -ENOMEM;
		goto error;
	}

	for (i = 0; i < ARRAY_SIZE(mdev->sdio.xmit_buf); i++) {
		mdev->sdio.xmit_buf[i] = devm_kmalloc(mdev->dev,
						      MT76S_XMIT_BUF_SZ,
						      GFP_KERNEL);
		if (!mdev->sdio.xmit_buf[i]) {
			ret = -ENOMEM;
			goto error;
		}
	}

	ret = mt76s_alloc_queues(&dev->mt76);
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

	mt76_tx_status_check(&mdev->mt76, NULL, true);

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
