// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/delay.h>

#include "mt76x02_mcu.h"

int mt76x02_mcu_msg_send(struct mt76_dev *mdev, int cmd, const void *data,
			 int len, bool wait_resp)
{
	struct mt76x02_dev *dev = container_of(mdev, struct mt76x02_dev, mt76);
	unsigned long expires = jiffies + HZ;
	struct sk_buff *skb;
	u32 tx_info;
	int ret;
	u8 seq;

	if (dev->mcu_timeout)
		return -EIO;

	skb = mt76_mcu_msg_alloc(mdev, data, len);
	if (!skb)
		return -ENOMEM;

	mutex_lock(&mdev->mcu.mutex);

	seq = ++mdev->mcu.msg_seq & 0xf;
	if (!seq)
		seq = ++mdev->mcu.msg_seq & 0xf;

	tx_info = MT_MCU_MSG_TYPE_CMD |
		  FIELD_PREP(MT_MCU_MSG_CMD_TYPE, cmd) |
		  FIELD_PREP(MT_MCU_MSG_CMD_SEQ, seq) |
		  FIELD_PREP(MT_MCU_MSG_PORT, CPU_TX_PORT) |
		  FIELD_PREP(MT_MCU_MSG_LEN, skb->len);

	ret = mt76_tx_queue_skb_raw(dev, MT_TXQ_MCU, skb, tx_info);
	if (ret)
		goto out;

	while (wait_resp) {
		u32 *rxfce;
		bool check_seq = false;

		skb = mt76_mcu_get_response(&dev->mt76, expires);
		if (!skb) {
			dev_err(mdev->dev,
				"MCU message %d (seq %d) timed out\n", cmd,
				seq);
			ret = -ETIMEDOUT;
			dev->mcu_timeout = 1;
			break;
		}

		rxfce = (u32 *)skb->cb;

		if (seq == FIELD_GET(MT_RX_FCE_INFO_CMD_SEQ, *rxfce))
			check_seq = true;

		dev_kfree_skb(skb);
		if (check_seq)
			break;
	}

out:
	mutex_unlock(&mdev->mcu.mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76x02_mcu_msg_send);

int mt76x02_mcu_function_select(struct mt76x02_dev *dev, enum mcu_function func,
				u32 val)
{
	struct {
		__le32 id;
		__le32 value;
	} __packed __aligned(4) msg = {
		.id = cpu_to_le32(func),
		.value = cpu_to_le32(val),
	};
	bool wait = false;

	if (func != Q_SELECT)
		wait = true;

	return mt76_mcu_send_msg(dev, CMD_FUN_SET_OP, &msg, sizeof(msg), wait);
}
EXPORT_SYMBOL_GPL(mt76x02_mcu_function_select);

int mt76x02_mcu_set_radio_state(struct mt76x02_dev *dev, bool on)
{
	struct {
		__le32 mode;
		__le32 level;
	} __packed __aligned(4) msg = {
		.mode = cpu_to_le32(on ? RADIO_ON : RADIO_OFF),
		.level = cpu_to_le32(0),
	};

	return mt76_mcu_send_msg(dev, CMD_POWER_SAVING_OP, &msg, sizeof(msg),
				 false);
}
EXPORT_SYMBOL_GPL(mt76x02_mcu_set_radio_state);

int mt76x02_mcu_calibrate(struct mt76x02_dev *dev, int type, u32 param)
{
	struct {
		__le32 id;
		__le32 value;
	} __packed __aligned(4) msg = {
		.id = cpu_to_le32(type),
		.value = cpu_to_le32(param),
	};
	bool is_mt76x2e = mt76_is_mmio(&dev->mt76) && is_mt76x2(dev);
	int ret;

	if (is_mt76x2e)
		mt76_rmw(dev, MT_MCU_COM_REG0, BIT(31), 0);

	ret = mt76_mcu_send_msg(dev, CMD_CALIBRATION_OP, &msg, sizeof(msg),
				true);
	if (ret)
		return ret;

	if (is_mt76x2e &&
	    WARN_ON(!mt76_poll_msec(dev, MT_MCU_COM_REG0,
				    BIT(31), BIT(31), 100)))
		return -ETIMEDOUT;

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_mcu_calibrate);

int mt76x02_mcu_cleanup(struct mt76x02_dev *dev)
{
	struct sk_buff *skb;

	mt76_wr(dev, MT_MCU_INT_LEVEL, 1);
	usleep_range(20000, 30000);

	while ((skb = skb_dequeue(&dev->mt76.mcu.res_q)) != NULL)
		dev_kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_mcu_cleanup);

void mt76x02_set_ethtool_fwver(struct mt76x02_dev *dev,
			       const struct mt76x02_fw_header *h)
{
	u16 bld = le16_to_cpu(h->build_ver);
	u16 ver = le16_to_cpu(h->fw_ver);

	snprintf(dev->mt76.hw->wiphy->fw_version,
		 sizeof(dev->mt76.hw->wiphy->fw_version),
		 "%d.%d.%02d-b%x",
		 (ver >> 12) & 0xf, (ver >> 8) & 0xf, ver & 0xf, bld);
}
EXPORT_SYMBOL_GPL(mt76x02_set_ethtool_fwver);
