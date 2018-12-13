/*
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

#include <linux/module.h>
#include <linux/firmware.h>

#include "mt76x02.h"
#include "mt76x02_mcu.h"
#include "mt76x02_usb.h"

#define MT_CMD_HDR_LEN			4

#define MT_FCE_DMA_ADDR			0x0230
#define MT_FCE_DMA_LEN			0x0234

#define MT_TX_CPU_FROM_FCE_CPU_DESC_IDX	0x09a8

static struct sk_buff *
mt76x02u_mcu_msg_alloc(const void *data, int len)
{
	struct sk_buff *skb;

	skb = alloc_skb(MT_CMD_HDR_LEN + len + 8, GFP_KERNEL);
	if (!skb)
		return NULL;

	skb_reserve(skb, MT_CMD_HDR_LEN);
	skb_put_data(skb, data, len);

	return skb;
}

static void
mt76x02u_multiple_mcu_reads(struct mt76_dev *dev, u8 *data, int len)
{
	struct mt76_usb *usb = &dev->usb;
	u32 reg, val;
	int i;

	if (usb->mcu.burst) {
		WARN_ON_ONCE(len / 4 != usb->mcu.rp_len);

		reg = usb->mcu.rp[0].reg - usb->mcu.base;
		for (i = 0; i < usb->mcu.rp_len; i++) {
			val = get_unaligned_le32(data + 4 * i);
			usb->mcu.rp[i].reg = reg++;
			usb->mcu.rp[i].value = val;
		}
	} else {
		WARN_ON_ONCE(len / 8 != usb->mcu.rp_len);

		for (i = 0; i < usb->mcu.rp_len; i++) {
			reg = get_unaligned_le32(data + 8 * i) -
			      usb->mcu.base;
			val = get_unaligned_le32(data + 8 * i + 4);

			WARN_ON_ONCE(usb->mcu.rp[i].reg != reg);
			usb->mcu.rp[i].value = val;
		}
	}
}

static int mt76x02u_mcu_wait_resp(struct mt76_dev *dev, u8 seq)
{
	struct mt76_usb *usb = &dev->usb;
	struct mt76u_buf *buf = &usb->mcu.res;
	struct urb *urb = buf->urb;
	int i, ret;
	u32 rxfce;
	u8 *data;

	for (i = 0; i < 5; i++) {
		if (!wait_for_completion_timeout(&usb->mcu.cmpl,
						 msecs_to_jiffies(300)))
			continue;

		if (urb->status)
			return -EIO;

		data = sg_virt(&urb->sg[0]);
		if (usb->mcu.rp)
			mt76x02u_multiple_mcu_reads(dev, data + 4,
						    urb->actual_length - 8);

		rxfce = get_unaligned_le32(data);
		ret = mt76u_submit_buf(dev, USB_DIR_IN,
				       MT_EP_IN_CMD_RESP,
				       buf, GFP_KERNEL,
				       mt76u_mcu_complete_urb,
				       &usb->mcu.cmpl);
		if (ret)
			return ret;

		if (seq == FIELD_GET(MT_RX_FCE_INFO_CMD_SEQ, rxfce) &&
		    FIELD_GET(MT_RX_FCE_INFO_EVT_TYPE, rxfce) == EVT_CMD_DONE)
			return 0;

		dev_err(dev->dev, "error: MCU resp evt:%lx seq:%hhx-%lx\n",
			FIELD_GET(MT_RX_FCE_INFO_EVT_TYPE, rxfce),
			seq, FIELD_GET(MT_RX_FCE_INFO_CMD_SEQ, rxfce));
	}

	dev_err(dev->dev, "error: %s timed out\n", __func__);
	return -ETIMEDOUT;
}

static int
__mt76x02u_mcu_send_msg(struct mt76_dev *dev, struct sk_buff *skb,
			int cmd, bool wait_resp)
{
	struct usb_interface *intf = to_usb_interface(dev->dev);
	struct usb_device *udev = interface_to_usbdev(intf);
	struct mt76_usb *usb = &dev->usb;
	unsigned int pipe;
	int ret, sent;
	u8 seq = 0;
	u32 info;

	if (test_bit(MT76_REMOVED, &dev->state))
		return 0;

	pipe = usb_sndbulkpipe(udev, usb->out_ep[MT_EP_OUT_INBAND_CMD]);
	if (wait_resp) {
		seq = ++usb->mcu.msg_seq & 0xf;
		if (!seq)
			seq = ++usb->mcu.msg_seq & 0xf;
	}

	info = FIELD_PREP(MT_MCU_MSG_CMD_SEQ, seq) |
	       FIELD_PREP(MT_MCU_MSG_CMD_TYPE, cmd) |
	       MT_MCU_MSG_TYPE_CMD;
	ret = mt76x02u_skb_dma_info(skb, CPU_TX_PORT, info);
	if (ret)
		return ret;

	ret = usb_bulk_msg(udev, pipe, skb->data, skb->len, &sent, 500);
	if (ret)
		return ret;

	if (wait_resp)
		ret = mt76x02u_mcu_wait_resp(dev, seq);

	consume_skb(skb);

	return ret;
}

static int
mt76x02u_mcu_send_msg(struct mt76_dev *dev, int cmd, const void *data,
		      int len, bool wait_resp)
{
	struct mt76_usb *usb = &dev->usb;
	struct sk_buff *skb;
	int err;

	skb = mt76x02u_mcu_msg_alloc(data, len);
	if (!skb)
		return -ENOMEM;

	mutex_lock(&usb->mcu.mutex);
	err = __mt76x02u_mcu_send_msg(dev, skb, cmd, wait_resp);
	mutex_unlock(&usb->mcu.mutex);

	return err;
}

static inline void skb_put_le32(struct sk_buff *skb, u32 val)
{
	put_unaligned_le32(val, skb_put(skb, 4));
}

static int
mt76x02u_mcu_wr_rp(struct mt76_dev *dev, u32 base,
		   const struct mt76_reg_pair *data, int n)
{
	const int CMD_RANDOM_WRITE = 12;
	const int max_vals_per_cmd = MT_INBAND_PACKET_MAX_LEN / 8;
	struct mt76_usb *usb = &dev->usb;
	struct sk_buff *skb;
	int cnt, i, ret;

	if (!n)
		return 0;

	cnt = min(max_vals_per_cmd, n);

	skb = alloc_skb(cnt * 8 + MT_DMA_HDR_LEN + 4, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;
	skb_reserve(skb, MT_DMA_HDR_LEN);

	for (i = 0; i < cnt; i++) {
		skb_put_le32(skb, base + data[i].reg);
		skb_put_le32(skb, data[i].value);
	}

	mutex_lock(&usb->mcu.mutex);
	ret = __mt76x02u_mcu_send_msg(dev, skb, CMD_RANDOM_WRITE, cnt == n);
	mutex_unlock(&usb->mcu.mutex);
	if (ret)
		return ret;

	return mt76x02u_mcu_wr_rp(dev, base, data + cnt, n - cnt);
}

static int
mt76x02u_mcu_rd_rp(struct mt76_dev *dev, u32 base,
		   struct mt76_reg_pair *data, int n)
{
	const int CMD_RANDOM_READ = 10;
	const int max_vals_per_cmd = MT_INBAND_PACKET_MAX_LEN / 8;
	struct mt76_usb *usb = &dev->usb;
	struct sk_buff *skb;
	int cnt, i, ret;

	if (!n)
		return 0;

	cnt = min(max_vals_per_cmd, n);
	if (cnt != n)
		return -EINVAL;

	skb = alloc_skb(cnt * 8 + MT_DMA_HDR_LEN + 4, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;
	skb_reserve(skb, MT_DMA_HDR_LEN);

	for (i = 0; i < cnt; i++) {
		skb_put_le32(skb, base + data[i].reg);
		skb_put_le32(skb, data[i].value);
	}

	mutex_lock(&usb->mcu.mutex);

	usb->mcu.rp = data;
	usb->mcu.rp_len = n;
	usb->mcu.base = base;
	usb->mcu.burst = false;

	ret = __mt76x02u_mcu_send_msg(dev, skb, CMD_RANDOM_READ, true);

	usb->mcu.rp = NULL;

	mutex_unlock(&usb->mcu.mutex);

	return ret;
}

void mt76x02u_mcu_fw_reset(struct mt76x02_dev *dev)
{
	mt76u_vendor_request(&dev->mt76, MT_VEND_DEV_MODE,
			     USB_DIR_OUT | USB_TYPE_VENDOR,
			     0x1, 0, NULL, 0);
}
EXPORT_SYMBOL_GPL(mt76x02u_mcu_fw_reset);

static int
__mt76x02u_mcu_fw_send_data(struct mt76x02_dev *dev, struct mt76u_buf *buf,
			    const void *fw_data, int len, u32 dst_addr)
{
	u8 *data = sg_virt(&buf->urb->sg[0]);
	DECLARE_COMPLETION_ONSTACK(cmpl);
	__le32 info;
	u32 val;
	int err;

	info = cpu_to_le32(FIELD_PREP(MT_MCU_MSG_PORT, CPU_TX_PORT) |
			   FIELD_PREP(MT_MCU_MSG_LEN, len) |
			   MT_MCU_MSG_TYPE_CMD);

	memcpy(data, &info, sizeof(info));
	memcpy(data + sizeof(info), fw_data, len);
	memset(data + sizeof(info) + len, 0, 4);

	mt76u_single_wr(&dev->mt76, MT_VEND_WRITE_FCE,
			MT_FCE_DMA_ADDR, dst_addr);
	len = roundup(len, 4);
	mt76u_single_wr(&dev->mt76, MT_VEND_WRITE_FCE,
			MT_FCE_DMA_LEN, len << 16);

	buf->len = MT_CMD_HDR_LEN + len + sizeof(info);
	err = mt76u_submit_buf(&dev->mt76, USB_DIR_OUT,
			       MT_EP_OUT_INBAND_CMD,
			       buf, GFP_KERNEL,
			       mt76u_mcu_complete_urb, &cmpl);
	if (err < 0)
		return err;

	if (!wait_for_completion_timeout(&cmpl,
					 msecs_to_jiffies(1000))) {
		dev_err(dev->mt76.dev, "firmware upload timed out\n");
		usb_kill_urb(buf->urb);
		return -ETIMEDOUT;
	}

	if (mt76u_urb_error(buf->urb)) {
		dev_err(dev->mt76.dev, "firmware upload failed: %d\n",
			buf->urb->status);
		return buf->urb->status;
	}

	val = mt76_rr(dev, MT_TX_CPU_FROM_FCE_CPU_DESC_IDX);
	val++;
	mt76_wr(dev, MT_TX_CPU_FROM_FCE_CPU_DESC_IDX, val);

	return 0;
}

int mt76x02u_mcu_fw_send_data(struct mt76x02_dev *dev, const void *data,
			      int data_len, u32 max_payload, u32 offset)
{
	int err, len, pos = 0, max_len = max_payload - 8;
	struct mt76u_buf buf;

	err = mt76u_buf_alloc(&dev->mt76, &buf, 1, max_payload, max_payload,
			      GFP_KERNEL);
	if (err < 0)
		return err;

	while (data_len > 0) {
		len = min_t(int, data_len, max_len);
		err = __mt76x02u_mcu_fw_send_data(dev, &buf, data + pos,
						  len, offset + pos);
		if (err < 0)
			break;

		data_len -= len;
		pos += len;
		usleep_range(5000, 10000);
	}
	mt76u_buf_free(&buf);

	return err;
}
EXPORT_SYMBOL_GPL(mt76x02u_mcu_fw_send_data);

void mt76x02u_init_mcu(struct mt76_dev *dev)
{
	static const struct mt76_mcu_ops mt76x02u_mcu_ops = {
		.mcu_send_msg = mt76x02u_mcu_send_msg,
		.mcu_wr_rp = mt76x02u_mcu_wr_rp,
		.mcu_rd_rp = mt76x02u_mcu_rd_rp,
	};

	dev->mcu_ops = &mt76x02u_mcu_ops;
}
EXPORT_SYMBOL_GPL(mt76x02u_init_mcu);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>");
MODULE_LICENSE("Dual BSD/GPL");
