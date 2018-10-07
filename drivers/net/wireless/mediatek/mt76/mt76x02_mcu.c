/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/delay.h>

#include "mt76.h"
#include "mt76x02_mcu.h"
#include "mt76x02_dma.h"

struct sk_buff *mt76x02_mcu_msg_alloc(const void *data, int len)
{
	struct sk_buff *skb;

	skb = alloc_skb(len, GFP_KERNEL);
	if (!skb)
		return NULL;
	memcpy(skb_put(skb, len), data, len);

	return skb;
}
EXPORT_SYMBOL_GPL(mt76x02_mcu_msg_alloc);

static struct sk_buff *
mt76x02_mcu_get_response(struct mt76_dev *dev, unsigned long expires)
{
	unsigned long timeout;

	if (!time_is_after_jiffies(expires))
		return NULL;

	timeout = expires - jiffies;
	wait_event_timeout(dev->mmio.mcu.wait,
			   !skb_queue_empty(&dev->mmio.mcu.res_q),
			   timeout);
	return skb_dequeue(&dev->mmio.mcu.res_q);
}

static int
mt76x02_tx_queue_mcu(struct mt76_dev *dev, enum mt76_txq_id qid,
		     struct sk_buff *skb, int cmd, int seq)
{
	struct mt76_queue *q = &dev->q_tx[qid];
	struct mt76_queue_buf buf;
	dma_addr_t addr;
	u32 tx_info;

	tx_info = MT_MCU_MSG_TYPE_CMD |
		  FIELD_PREP(MT_MCU_MSG_CMD_TYPE, cmd) |
		  FIELD_PREP(MT_MCU_MSG_CMD_SEQ, seq) |
		  FIELD_PREP(MT_MCU_MSG_PORT, CPU_TX_PORT) |
		  FIELD_PREP(MT_MCU_MSG_LEN, skb->len);

	addr = dma_map_single(dev->dev, skb->data, skb->len,
			      DMA_TO_DEVICE);
	if (dma_mapping_error(dev->dev, addr))
		return -ENOMEM;

	buf.addr = addr;
	buf.len = skb->len;
	spin_lock_bh(&q->lock);
	dev->queue_ops->add_buf(dev, q, &buf, 1, tx_info, skb, NULL);
	dev->queue_ops->kick(dev, q);
	spin_unlock_bh(&q->lock);

	return 0;
}

int mt76x02_mcu_msg_send(struct mt76_dev *dev, struct sk_buff *skb,
			 int cmd, bool wait_resp)
{
	unsigned long expires = jiffies + HZ;
	int ret;
	u8 seq;

	if (!skb)
		return -EINVAL;

	mutex_lock(&dev->mmio.mcu.mutex);

	seq = ++dev->mmio.mcu.msg_seq & 0xf;
	if (!seq)
		seq = ++dev->mmio.mcu.msg_seq & 0xf;

	ret = mt76x02_tx_queue_mcu(dev, MT_TXQ_MCU, skb, cmd, seq);
	if (ret)
		goto out;

	while (wait_resp) {
		u32 *rxfce;
		bool check_seq = false;

		skb = mt76x02_mcu_get_response(dev, expires);
		if (!skb) {
			dev_err(dev->dev,
				"MCU message %d (seq %d) timed out\n", cmd,
				seq);
			ret = -ETIMEDOUT;
			break;
		}

		rxfce = (u32 *) skb->cb;

		if (seq == FIELD_GET(MT_RX_FCE_INFO_CMD_SEQ, *rxfce))
			check_seq = true;

		dev_kfree_skb(skb);
		if (check_seq)
			break;
	}

out:
	mutex_unlock(&dev->mmio.mcu.mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76x02_mcu_msg_send);

int mt76x02_mcu_function_select(struct mt76_dev *dev,
				enum mcu_function func,
				u32 val, bool wait_resp)
{
	struct sk_buff *skb;
	struct {
	    __le32 id;
	    __le32 value;
	} __packed __aligned(4) msg = {
	    .id = cpu_to_le32(func),
	    .value = cpu_to_le32(val),
	};

	skb = dev->mcu_ops->mcu_msg_alloc(&msg, sizeof(msg));
	return dev->mcu_ops->mcu_send_msg(dev, skb, CMD_FUN_SET_OP,
					  wait_resp);
}
EXPORT_SYMBOL_GPL(mt76x02_mcu_function_select);

int mt76x02_mcu_set_radio_state(struct mt76_dev *dev, bool on,
				bool wait_resp)
{
	struct sk_buff *skb;
	struct {
		__le32 mode;
		__le32 level;
	} __packed __aligned(4) msg = {
		.mode = cpu_to_le32(on ? RADIO_ON : RADIO_OFF),
		.level = cpu_to_le32(0),
	};

	skb = dev->mcu_ops->mcu_msg_alloc(&msg, sizeof(msg));
	return dev->mcu_ops->mcu_send_msg(dev, skb, CMD_POWER_SAVING_OP,
					  wait_resp);
}
EXPORT_SYMBOL_GPL(mt76x02_mcu_set_radio_state);

int mt76x02_mcu_calibrate(struct mt76_dev *dev, int type,
			  u32 param, bool wait)
{
	struct sk_buff *skb;
	struct {
		__le32 id;
		__le32 value;
	} __packed __aligned(4) msg = {
		.id = cpu_to_le32(type),
		.value = cpu_to_le32(param),
	};
	int ret;

	if (wait)
		dev->bus->rmw(dev, MT_MCU_COM_REG0, BIT(31), 0);

	skb = dev->mcu_ops->mcu_msg_alloc(&msg, sizeof(msg));
	ret = dev->mcu_ops->mcu_send_msg(dev, skb, CMD_CALIBRATION_OP, true);
	if (ret)
		return ret;

	if (wait &&
	    WARN_ON(!__mt76_poll_msec(dev, MT_MCU_COM_REG0,
				      BIT(31), BIT(31), 100)))
		return -ETIMEDOUT;

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_mcu_calibrate);

int mt76x02_mcu_cleanup(struct mt76_dev *dev)
{
	struct sk_buff *skb;

	dev->bus->wr(dev, MT_MCU_INT_LEVEL, 1);
	usleep_range(20000, 30000);

	while ((skb = skb_dequeue(&dev->mmio.mcu.res_q)) != NULL)
		dev_kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_mcu_cleanup);

void mt76x02_set_ethtool_fwver(struct mt76_dev *dev,
			       const struct mt76x02_fw_header *h)
{
	u16 bld = le16_to_cpu(h->build_ver);
	u16 ver = le16_to_cpu(h->fw_ver);

	snprintf(dev->hw->wiphy->fw_version,
		 sizeof(dev->hw->wiphy->fw_version),
		 "%d.%d.%02d-b%x",
		 (ver >> 12) & 0xf, (ver >> 8) & 0xf, ver & 0xf, bld);
}
EXPORT_SYMBOL_GPL(mt76x02_set_ethtool_fwver);
