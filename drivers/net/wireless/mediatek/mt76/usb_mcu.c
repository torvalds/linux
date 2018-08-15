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

#include <linux/firmware.h>

#include "mt76.h"
#include "dma.h"

#define MT_CMD_HDR_LEN			4

#define MT_FCE_DMA_ADDR			0x0230
#define MT_FCE_DMA_LEN			0x0234

#define MT_TX_CPU_FROM_FCE_CPU_DESC_IDX	0x09a8

struct sk_buff *mt76u_mcu_msg_alloc(const void *data, int len)
{
	struct sk_buff *skb;

	skb = alloc_skb(MT_CMD_HDR_LEN + len + 8, GFP_KERNEL);
	if (!skb)
		return NULL;

	skb_reserve(skb, MT_CMD_HDR_LEN);
	skb_put_data(skb, data, len);

	return skb;
}
EXPORT_SYMBOL_GPL(mt76u_mcu_msg_alloc);

void mt76u_mcu_complete_urb(struct urb *urb)
{
	struct completion *cmpl = urb->context;

	complete(cmpl);
}
EXPORT_SYMBOL_GPL(mt76u_mcu_complete_urb);

static int mt76u_mcu_wait_resp(struct mt76_dev *dev, u8 seq)
{
	struct mt76_usb *usb = &dev->usb;
	struct mt76u_buf *buf = &usb->mcu.res;
	int i, ret;
	u32 rxfce;

	for (i = 0; i < 5; i++) {
		if (!wait_for_completion_timeout(&usb->mcu.cmpl,
						 msecs_to_jiffies(300)))
			continue;

		if (buf->urb->status)
			return -EIO;

		rxfce = get_unaligned_le32(sg_virt(&buf->urb->sg[0]));
		ret = mt76u_submit_buf(dev, USB_DIR_IN,
				       MT_EP_IN_CMD_RESP,
				       buf, GFP_KERNEL,
				       mt76u_mcu_complete_urb,
				       &usb->mcu.cmpl);
		if (ret)
			return ret;

		if (seq == FIELD_GET(MT_RX_FCE_INFO_CMD_SEQ, rxfce))
			return 0;

		dev_err(dev->dev, "error: MCU resp evt:%lx seq:%hhx-%lx\n",
			FIELD_GET(MT_RX_FCE_INFO_EVT_TYPE, rxfce),
			seq, FIELD_GET(MT_RX_FCE_INFO_CMD_SEQ, rxfce));
	}

	dev_err(dev->dev, "error: %s timed out\n", __func__);
	return -ETIMEDOUT;
}

int mt76u_mcu_send_msg(struct mt76_dev *dev, struct sk_buff *skb,
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

	mutex_lock(&usb->mcu.mutex);

	pipe = usb_sndbulkpipe(udev, usb->out_ep[MT_EP_OUT_INBAND_CMD]);
	if (wait_resp) {
		seq = ++usb->mcu.msg_seq & 0xf;
		if (!seq)
			seq = ++usb->mcu.msg_seq & 0xf;
	}

	info = FIELD_PREP(MT_MCU_MSG_CMD_SEQ, seq) |
	       FIELD_PREP(MT_MCU_MSG_CMD_TYPE, cmd) |
	       MT_MCU_MSG_TYPE_CMD;
	ret = mt76u_skb_dma_info(skb, CPU_TX_PORT, info);
	if (ret)
		goto out;

	ret = usb_bulk_msg(udev, pipe, skb->data, skb->len, &sent, 500);
	if (ret)
		goto out;

	if (wait_resp)
		ret = mt76u_mcu_wait_resp(dev, seq);

out:
	mutex_unlock(&usb->mcu.mutex);

	consume_skb(skb);

	return ret;
}
EXPORT_SYMBOL_GPL(mt76u_mcu_send_msg);

void mt76u_mcu_fw_reset(struct mt76_dev *dev)
{
	mt76u_vendor_request(dev, MT_VEND_DEV_MODE,
			     USB_DIR_OUT | USB_TYPE_VENDOR,
			     0x1, 0, NULL, 0);
}
EXPORT_SYMBOL_GPL(mt76u_mcu_fw_reset);

static int
__mt76u_mcu_fw_send_data(struct mt76_dev *dev, struct mt76u_buf *buf,
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

	mt76u_single_wr(dev, MT_VEND_WRITE_FCE,
			MT_FCE_DMA_ADDR, dst_addr);
	len = roundup(len, 4);
	mt76u_single_wr(dev, MT_VEND_WRITE_FCE,
			MT_FCE_DMA_LEN, len << 16);

	buf->len = MT_CMD_HDR_LEN + len + sizeof(info);
	err = mt76u_submit_buf(dev, USB_DIR_OUT,
			       MT_EP_OUT_INBAND_CMD,
			       buf, GFP_KERNEL,
			       mt76u_mcu_complete_urb, &cmpl);
	if (err < 0)
		return err;

	if (!wait_for_completion_timeout(&cmpl,
					 msecs_to_jiffies(1000))) {
		dev_err(dev->dev, "firmware upload timed out\n");
		usb_kill_urb(buf->urb);
		return -ETIMEDOUT;
	}

	if (mt76u_urb_error(buf->urb)) {
		dev_err(dev->dev, "firmware upload failed: %d\n",
			buf->urb->status);
		return buf->urb->status;
	}

	val = mt76u_rr(dev, MT_TX_CPU_FROM_FCE_CPU_DESC_IDX);
	val++;
	mt76u_wr(dev, MT_TX_CPU_FROM_FCE_CPU_DESC_IDX, val);

	return 0;
}

int mt76u_mcu_fw_send_data(struct mt76_dev *dev, const void *data,
			   int data_len, u32 max_payload, u32 offset)
{
	int err, len, pos = 0, max_len = max_payload - 8;
	struct mt76u_buf buf;

	err = mt76u_buf_alloc(dev, &buf, 1, max_payload, max_payload,
			      GFP_KERNEL);
	if (err < 0)
		return err;

	while (data_len > 0) {
		len = min_t(int, data_len, max_len);
		err = __mt76u_mcu_fw_send_data(dev, &buf, data + pos,
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
EXPORT_SYMBOL_GPL(mt76u_mcu_fw_send_data);

int mt76u_mcu_init_rx(struct mt76_dev *dev)
{
	struct mt76_usb *usb = &dev->usb;
	int err;

	err = mt76u_buf_alloc(dev, &usb->mcu.res, 1,
			      MCU_RESP_URB_SIZE, MCU_RESP_URB_SIZE,
			      GFP_KERNEL);
	if (err < 0)
		return err;

	err = mt76u_submit_buf(dev, USB_DIR_IN, MT_EP_IN_CMD_RESP,
			       &usb->mcu.res, GFP_KERNEL,
			       mt76u_mcu_complete_urb,
			       &usb->mcu.cmpl);
	if (err < 0)
		mt76u_buf_free(&usb->mcu.res);

	return err;
}
EXPORT_SYMBOL_GPL(mt76u_mcu_init_rx);
