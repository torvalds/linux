// SPDX-License-Identifier: ISC
/* Copyright (C) 2021 MediaTek Inc. */

#include "mt7921.h"
#include "mcu.h"

static int mt7921e_driver_own(struct mt7921_dev *dev)
{
	u32 reg = mt7921_reg_map_l1(dev, MT_TOP_LPCR_HOST_BAND0);

	mt76_wr(dev, reg, MT_TOP_LPCR_HOST_DRV_OWN);
	if (!mt76_poll_msec(dev, reg, MT_TOP_LPCR_HOST_FW_OWN,
			    0, 500)) {
		dev_err(dev->mt76.dev, "Timeout for driver own\n");
		return -EIO;
	}

	return 0;
}

static int
mt7921_mcu_send_message(struct mt76_dev *mdev, struct sk_buff *skb,
			int cmd, int *seq)
{
	struct mt7921_dev *dev = container_of(mdev, struct mt7921_dev, mt76);
	enum mt76_mcuq_id txq = MT_MCUQ_WM;
	int ret;

	ret = mt7921_mcu_fill_message(mdev, skb, cmd, seq);
	if (ret)
		return ret;

	if (cmd == MCU_CMD_FW_SCATTER)
		txq = MT_MCUQ_FWDL;

	return mt76_tx_queue_skb_raw(dev, mdev->q_mcu[txq], skb, 0);
}

int mt7921e_mcu_init(struct mt7921_dev *dev)
{
	static const struct mt76_mcu_ops mt7921_mcu_ops = {
		.headroom = sizeof(struct mt7921_mcu_txd),
		.mcu_skb_send_msg = mt7921_mcu_send_message,
		.mcu_parse_response = mt7921_mcu_parse_response,
		.mcu_restart = mt7921_mcu_restart,
	};
	int err;

	dev->mt76.mcu_ops = &mt7921_mcu_ops;

	err = mt7921e_driver_own(dev);
	if (err)
		return err;

	err = mt7921_run_firmware(dev);

	mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[MT_MCUQ_FWDL], false);

	return err;
}

int mt7921e_mcu_drv_pmctrl(struct mt7921_dev *dev)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt76_connac_pm *pm = &dev->pm;
	int i, err = 0;

	for (i = 0; i < MT7921_DRV_OWN_RETRY_COUNT; i++) {
		mt76_wr(dev, MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_CLR_OWN);
		if (mt76_poll_msec(dev, MT_CONN_ON_LPCTL,
				   PCIE_LPCR_HOST_OWN_SYNC, 0, 50))
			break;
	}

	if (i == MT7921_DRV_OWN_RETRY_COUNT) {
		dev_err(dev->mt76.dev, "driver own failed\n");
		err = -EIO;
		goto out;
	}

	mt7921_wpdma_reinit_cond(dev);
	clear_bit(MT76_STATE_PM, &mphy->state);

	pm->stats.last_wake_event = jiffies;
	pm->stats.doze_time += pm->stats.last_wake_event -
			       pm->stats.last_doze_event;
out:
	return err;
}

int mt7921e_mcu_fw_pmctrl(struct mt7921_dev *dev)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt76_connac_pm *pm = &dev->pm;
	int i, err = 0;

	for (i = 0; i < MT7921_DRV_OWN_RETRY_COUNT; i++) {
		mt76_wr(dev, MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_SET_OWN);
		if (mt76_poll_msec(dev, MT_CONN_ON_LPCTL,
				   PCIE_LPCR_HOST_OWN_SYNC, 4, 50))
			break;
	}

	if (i == MT7921_DRV_OWN_RETRY_COUNT) {
		dev_err(dev->mt76.dev, "firmware own failed\n");
		clear_bit(MT76_STATE_PM, &mphy->state);
		err = -EIO;
	}

	pm->stats.last_doze_event = jiffies;
	pm->stats.awake_time += pm->stats.last_doze_event -
				pm->stats.last_wake_event;

	return err;
}
