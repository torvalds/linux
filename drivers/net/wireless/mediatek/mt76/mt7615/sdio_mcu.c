// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020 MediaTek Inc.
 *
 * Author: Felix Fietkau <nbd@nbd.name>
 *	   Lorenzo Bianconi <lorenzo@kernel.org>
 *	   Sean Wang <sean.wang@mediatek.com>
 */
#include <linux/kernel.h>
#include <linux/mmc/sdio_func.h>
#include <linux/module.h>
#include <linux/iopoll.h>

#include "mt7615.h"
#include "mac.h"
#include "mcu.h"
#include "regs.h"
#include "sdio.h"

static int mt7663s_mcu_init_sched(struct mt7615_dev *dev)
{
	struct mt76_sdio *sdio = &dev->mt76.sdio;
	u32 pse0, ple, pse1, txdwcnt;

	pse0 = mt76_get_field(dev, MT_PSE_PG_HIF0_GROUP, MT_HIF0_MIN_QUOTA);
	pse1 = mt76_get_field(dev, MT_PSE_PG_HIF1_GROUP, MT_HIF1_MIN_QUOTA);
	ple = mt76_get_field(dev, MT_PLE_PG_HIF0_GROUP, MT_HIF0_MIN_QUOTA);
	txdwcnt = mt76_get_field(dev, MT_PP_TXDWCNT,
				 MT_PP_TXDWCNT_TX1_ADD_DW_CNT);

	mutex_lock(&sdio->sched.lock);

	sdio->sched.pse_data_quota = pse0;
	sdio->sched.ple_data_quota = ple;
	sdio->sched.pse_mcu_quota = pse1;
	sdio->sched.deficit = txdwcnt << 2;

	mutex_unlock(&sdio->sched.lock);

	return 0;
}

static int
mt7663s_mcu_send_message(struct mt76_dev *mdev, struct sk_buff *skb,
			 int cmd, bool wait_resp)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	int ret, seq;

	mutex_lock(&mdev->mcu.mutex);

	mt7615_mcu_fill_msg(dev, skb, cmd, &seq);
	ret = mt76_tx_queue_skb_raw(dev, MT_TXQ_MCU, skb, 0);
	if (ret)
		goto out;

	mt76_queue_kick(dev, mdev->q_tx[MT_TXQ_MCU].q);
	if (wait_resp)
		ret = mt7615_mcu_wait_response(dev, cmd, seq);

out:
	mutex_unlock(&mdev->mcu.mutex);

	return ret;
}

static int mt7663s_mcu_drv_pmctrl(struct mt7615_dev *dev)
{
	struct sdio_func *func = dev->mt76.sdio.func;
	struct mt76_phy *mphy = &dev->mt76.phy;
	u32 status;
	int ret;

	if (!test_and_clear_bit(MT76_STATE_PM, &mphy->state))
		goto out;

	sdio_claim_host(func);

	sdio_writel(func, WHLPCR_FW_OWN_REQ_CLR, MCR_WHLPCR, NULL);

	ret = readx_poll_timeout(mt7663s_read_pcr, dev, status,
				 status & WHLPCR_IS_DRIVER_OWN, 2000, 1000000);
	if (ret < 0) {
		dev_err(dev->mt76.dev, "Cannot get ownership from device");
		set_bit(MT76_STATE_PM, &mphy->state);
		sdio_release_host(func);

		return ret;
	}

	sdio_release_host(func);

out:
	dev->pm.last_activity = jiffies;

	return 0;
}

static int mt7663s_mcu_fw_pmctrl(struct mt7615_dev *dev)
{
	struct sdio_func *func = dev->mt76.sdio.func;
	struct mt76_phy *mphy = &dev->mt76.phy;
	u32 status;
	int ret;

	if (test_and_set_bit(MT76_STATE_PM, &mphy->state))
		return 0;

	sdio_claim_host(func);

	sdio_writel(func, WHLPCR_FW_OWN_REQ_SET, MCR_WHLPCR, NULL);

	ret = readx_poll_timeout(mt7663s_read_pcr, dev, status,
				 !(status & WHLPCR_IS_DRIVER_OWN), 2000, 1000000);
	if (ret < 0) {
		dev_err(dev->mt76.dev, "Cannot set ownership to device");
		clear_bit(MT76_STATE_PM, &mphy->state);
	}

	sdio_release_host(func);

	return ret;
}

int mt7663s_mcu_init(struct mt7615_dev *dev)
{
	static const struct mt76_mcu_ops mt7663s_mcu_ops = {
		.headroom = sizeof(struct mt7615_mcu_txd),
		.tailroom = MT_USB_TAIL_SIZE,
		.mcu_skb_send_msg = mt7663s_mcu_send_message,
		.mcu_send_msg = mt7615_mcu_msg_send,
		.mcu_restart = mt7615_mcu_restart,
		.mcu_rr = mt7615_mcu_reg_rr,
		.mcu_wr = mt7615_mcu_reg_wr,
	};
	struct mt7615_mcu_ops *mcu_ops;
	int ret;

	ret = mt7663s_mcu_drv_pmctrl(dev);
	if (ret)
		return ret;

	dev->mt76.mcu_ops = &mt7663s_mcu_ops,

	ret = mt76_get_field(dev, MT_CONN_ON_MISC, MT_TOP_MISC2_FW_N9_RDY);
	if (ret) {
		mt7615_mcu_restart(&dev->mt76);
		if (!mt76_poll_msec(dev, MT_CONN_ON_MISC,
				    MT_TOP_MISC2_FW_N9_RDY, 0, 500))
			return -EIO;
	}

	ret = __mt7663_load_firmware(dev);
	if (ret)
		return ret;

	mcu_ops = devm_kmemdup(dev->mt76.dev, dev->mcu_ops, sizeof(*mcu_ops),
			       GFP_KERNEL);
	if (!mcu_ops)
		return -ENOMEM;

	mcu_ops->set_drv_ctrl = mt7663s_mcu_drv_pmctrl;
	mcu_ops->set_fw_ctrl = mt7663s_mcu_fw_pmctrl;
	dev->mcu_ops = mcu_ops;

	ret = mt7663s_mcu_init_sched(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state);

	return 0;
}
