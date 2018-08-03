/*
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 * Copyright (C) 2014 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/usb.h>
#include <linux/skbuff.h>

#include "mt76x0.h"
#include "dma.h"
#include "mcu.h"
#include "usb.h"
#include "trace.h"

#define MCU_FW_URB_MAX_PAYLOAD		0x38f8
#define MCU_FW_URB_SIZE			(MCU_FW_URB_MAX_PAYLOAD + 12)
#define MCU_RESP_URB_SIZE		1024

static inline int firmware_running(struct mt76x0_dev *dev)
{
	return mt76_rr(dev, MT_MCU_COM_REG0) == 1;
}

static inline void skb_put_le32(struct sk_buff *skb, u32 val)
{
	put_unaligned_le32(val, skb_put(skb, 4));
}

static inline void mt76x0_dma_skb_wrap_cmd(struct sk_buff *skb,
					    u8 seq, enum mcu_cmd cmd)
{
	WARN_ON(mt76x0_dma_skb_wrap(skb, CPU_TX_PORT, DMA_COMMAND,
				     FIELD_PREP(MT_TXD_CMD_SEQ, seq) |
				     FIELD_PREP(MT_TXD_CMD_TYPE, cmd)));
}

static inline void trace_mt76x0_mcu_msg_send_cs(struct mt76_dev *dev,
					    struct sk_buff *skb, bool need_resp)
{
	u32 i, csum = 0;

	for (i = 0; i < skb->len / 4; i++)
		csum ^= get_unaligned_le32(skb->data + i * 4);

	trace_mt76x0_mcu_msg_send(dev, skb, csum, need_resp);
}

static struct sk_buff *
mt76x0_mcu_msg_alloc(struct mt76x0_dev *dev, const void *data, int len)
{
	struct sk_buff *skb;

	WARN_ON(len % 4); /* if length is not divisible by 4 we need to pad */

	skb = alloc_skb(len + MT_DMA_HDR_LEN + 4, GFP_KERNEL);
	if (skb) {
		skb_reserve(skb, MT_DMA_HDR_LEN);
		memcpy(skb_put(skb, len), data, len);
	}
	return skb;
}

static void mt76x0_read_resp_regs(struct mt76x0_dev *dev, int len)
{
	int i;
	int n = dev->mcu.reg_pairs_len;
	u8 *buf = dev->mcu.resp.buf;

	buf += 4;
	len -= 8;

	if (dev->mcu.burst_read) {
		u32 reg = dev->mcu.reg_pairs[0].reg - dev->mcu.reg_base;

		WARN_ON_ONCE(len/4 != n);
		for (i = 0; i < n; i++) {
			u32 val = get_unaligned_le32(buf + 4*i);

			dev->mcu.reg_pairs[i].reg = reg++;
			dev->mcu.reg_pairs[i].value = val;
		}
	} else {
		WARN_ON_ONCE(len/8 != n);
		for (i = 0; i < n; i++) {
			u32 reg = get_unaligned_le32(buf + 8*i) - dev->mcu.reg_base;
			u32 val = get_unaligned_le32(buf + 8*i + 4);

			WARN_ON_ONCE(dev->mcu.reg_pairs[i].reg != reg);
			dev->mcu.reg_pairs[i].value = val;
		}
	}
}

static int mt76x0_mcu_wait_resp(struct mt76x0_dev *dev, u8 seq)
{
	struct urb *urb = dev->mcu.resp.urb;
	u32 rxfce;
	int urb_status, ret, try = 5;

	while (try--) {
		if (!wait_for_completion_timeout(&dev->mcu.resp_cmpl,
						 msecs_to_jiffies(300))) {
			dev_warn(dev->mt76.dev, "Warning: %s retrying\n", __func__);
			continue;
		}

		/* Make copies of important data before reusing the urb */
		rxfce = get_unaligned_le32(dev->mcu.resp.buf);
		urb_status = urb->status * mt76x0_urb_has_error(urb);

		if (urb_status == 0 && dev->mcu.reg_pairs)
			mt76x0_read_resp_regs(dev, urb->actual_length);

		ret = mt76x0_usb_submit_buf(dev, USB_DIR_IN, MT_EP_IN_CMD_RESP,
					     &dev->mcu.resp, GFP_KERNEL,
					     mt76x0_complete_urb,
					     &dev->mcu.resp_cmpl);
		if (ret)
			return ret;

		if (urb_status)
			dev_err(dev->mt76.dev, "Error: MCU resp urb failed:%d\n",
				urb_status);

		if (FIELD_GET(MT_RXD_CMD_INFO_CMD_SEQ, rxfce) == seq &&
		    FIELD_GET(MT_RXD_CMD_INFO_EVT_TYPE, rxfce) == CMD_DONE)
			return 0;

		dev_err(dev->mt76.dev, "Error: MCU resp evt:%lx seq:%hhx-%lx!\n",
			FIELD_GET(MT_RXD_CMD_INFO_EVT_TYPE, rxfce),
			seq, FIELD_GET(MT_RXD_CMD_INFO_CMD_SEQ, rxfce));
	}

	dev_err(dev->mt76.dev, "Error: %s timed out\n", __func__);
	return -ETIMEDOUT;
}

static int
__mt76x0_mcu_msg_send(struct mt76x0_dev *dev, struct sk_buff *skb,
		      enum mcu_cmd cmd, bool wait_resp)
{
	struct usb_device *usb_dev = mt76x0_to_usb_dev(dev);
	unsigned cmd_pipe = usb_sndbulkpipe(usb_dev,
					    dev->out_ep[MT_EP_OUT_INBAND_CMD]);
	int sent, ret;
	u8 seq = 0;

	if (wait_resp)
		while (!seq)
			seq = ++dev->mcu.msg_seq & 0xf;

	mt76x0_dma_skb_wrap_cmd(skb, seq, cmd);

	if (dev->mcu.resp_cmpl.done)
		dev_err(dev->mt76.dev, "Error: MCU response pre-completed!\n");

	trace_mt76x0_mcu_msg_send_cs(&dev->mt76, skb, wait_resp);
	trace_mt76x0_submit_urb_sync(&dev->mt76, cmd_pipe, skb->len);

	ret = usb_bulk_msg(usb_dev, cmd_pipe, skb->data, skb->len, &sent, 500);
	if (ret) {
		dev_err(dev->mt76.dev, "Error: send MCU cmd failed:%d\n", ret);
		goto out;
	}
	if (sent != skb->len)
		dev_err(dev->mt76.dev, "Error: %s sent != skb->len\n", __func__);

	if (wait_resp)
		ret = mt76x0_mcu_wait_resp(dev, seq);

out:
	return ret;
}

static int
mt76x0_mcu_msg_send(struct mt76x0_dev *dev, struct sk_buff *skb,
		     enum mcu_cmd cmd, bool wait_resp)
{
	int ret;

	if (test_bit(MT76_REMOVED, &dev->mt76.state))
		return 0;

	mutex_lock(&dev->mcu.mutex);
	ret = __mt76x0_mcu_msg_send(dev, skb, cmd, wait_resp);
	mutex_unlock(&dev->mcu.mutex);

	consume_skb(skb);

	return ret;
}

int mt76x0_mcu_function_select(struct mt76x0_dev *dev,
			       enum mcu_function func, u32 val)
{
	struct sk_buff *skb;
	struct {
		__le32 id;
		__le32 value;
	} __packed __aligned(4) msg = {
		.id = cpu_to_le32(func),
		.value = cpu_to_le32(val),
	};

	skb = mt76x0_mcu_msg_alloc(dev, &msg, sizeof(msg));
	if (!skb)
		return -ENOMEM;
	return mt76x0_mcu_msg_send(dev, skb, CMD_FUN_SET_OP, func == 5);
}

int
mt76x0_mcu_calibrate(struct mt76x0_dev *dev, enum mcu_calibrate cal, u32 val)
{
	struct sk_buff *skb;
	struct {
		__le32 id;
		__le32 value;
	} __packed __aligned(4) msg = {
		.id = cpu_to_le32(cal),
		.value = cpu_to_le32(val),
	};

	skb = mt76x0_mcu_msg_alloc(dev, &msg, sizeof(msg));
	if (!skb)
		return -ENOMEM;
	return mt76x0_mcu_msg_send(dev, skb, CMD_CALIBRATION_OP, true);
}

int mt76x0_write_reg_pairs(struct mt76x0_dev *dev, u32 base,
			   const struct mt76_reg_pair *data, int n)
{
	const int max_vals_per_cmd = INBAND_PACKET_MAX_LEN / 8;
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

	ret = mt76x0_mcu_msg_send(dev, skb, CMD_RANDOM_WRITE, cnt == n);
	if (ret)
		return ret;

	return mt76x0_write_reg_pairs(dev, base, data + cnt, n - cnt);
}

int mt76x0_read_reg_pairs(struct mt76x0_dev *dev, u32 base,
			  struct mt76_reg_pair *data, int n)
{
	const int max_vals_per_cmd = INBAND_PACKET_MAX_LEN / 8;
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

	mutex_lock(&dev->mcu.mutex);

	dev->mcu.reg_pairs = data;
	dev->mcu.reg_pairs_len = n;
	dev->mcu.reg_base = base;
	dev->mcu.burst_read = false;

	ret = __mt76x0_mcu_msg_send(dev, skb, CMD_RANDOM_READ, true);

	dev->mcu.reg_pairs = NULL;

	mutex_unlock(&dev->mcu.mutex);

	consume_skb(skb);

	return ret;

}

int mt76x0_burst_write_regs(struct mt76x0_dev *dev, u32 offset,
			     const u32 *data, int n)
{
	const int max_regs_per_cmd = INBAND_PACKET_MAX_LEN / 4 - 1;
	struct sk_buff *skb;
	int cnt, i, ret;

	if (!n)
		return 0;

	cnt = min(max_regs_per_cmd, n);

	skb = alloc_skb(cnt * 4 + MT_DMA_HDR_LEN + 4, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;
	skb_reserve(skb, MT_DMA_HDR_LEN);

	skb_put_le32(skb, MT_MCU_MEMMAP_WLAN + offset);
	for (i = 0; i < cnt; i++)
		skb_put_le32(skb, data[i]);

	ret = mt76x0_mcu_msg_send(dev, skb, CMD_BURST_WRITE, cnt == n);
	if (ret)
		return ret;

	return mt76x0_burst_write_regs(dev, offset + cnt * 4,
					data + cnt, n - cnt);
}

#if 0
static int mt76x0_burst_read_regs(struct mt76x0_dev *dev, u32 base,
				  struct mt76_reg_pair *data, int n)
{
	const int max_vals_per_cmd = INBAND_PACKET_MAX_LEN / 4 - 1;
	struct sk_buff *skb;
	int cnt, ret;

	if (!n)
		return 0;

	cnt = min(max_vals_per_cmd, n);
	if (cnt != n)
		return -EINVAL;

	skb = alloc_skb(cnt * 4 + MT_DMA_HDR_LEN + 4, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;
	skb_reserve(skb, MT_DMA_HDR_LEN);

	skb_put_le32(skb, base + data[0].reg);
	skb_put_le32(skb, n);

	mutex_lock(&dev->mcu.mutex);

	dev->mcu.reg_pairs = data;
	dev->mcu.reg_pairs_len = n;
	dev->mcu.reg_base = base;
	dev->mcu.burst_read = true;

	ret = __mt76x0_mcu_msg_send(dev, skb, CMD_BURST_READ, true);

	dev->mcu.reg_pairs = NULL;

	mutex_unlock(&dev->mcu.mutex);

	consume_skb(skb);

	return ret;
}
#endif

struct mt76_fw_header {
	__le32 ilm_len;
	__le32 dlm_len;
	__le16 build_ver;
	__le16 fw_ver;
	u8 pad[4];
	char build_time[16];
};

struct mt76_fw {
	struct mt76_fw_header hdr;
	u8 ivb[MT_MCU_IVB_SIZE];
	u8 ilm[];
};

static int __mt76x0_dma_fw(struct mt76x0_dev *dev,
			    const struct mt76x0_dma_buf *dma_buf,
			    const void *data, u32 len, u32 dst_addr)
{
	DECLARE_COMPLETION_ONSTACK(cmpl);
	struct mt76x0_dma_buf buf = *dma_buf; /* we need to fake length */
	__le32 reg;
	u32 val;
	int ret;

	reg = cpu_to_le32(FIELD_PREP(MT_TXD_INFO_TYPE, DMA_COMMAND) |
			  FIELD_PREP(MT_TXD_INFO_D_PORT, CPU_TX_PORT) |
			  FIELD_PREP(MT_TXD_INFO_LEN, len));
	memcpy(buf.buf, &reg, sizeof(reg));
	memcpy(buf.buf + sizeof(reg), data, len);
	memset(buf.buf + sizeof(reg) + len, 0, 8);

	ret = mt76x0_vendor_single_wr(dev, MT_VEND_WRITE_FCE,
				       MT_FCE_DMA_ADDR, dst_addr);
	if (ret)
		return ret;
	len = roundup(len, 4);
	ret = mt76x0_vendor_single_wr(dev, MT_VEND_WRITE_FCE,
				       MT_FCE_DMA_LEN, len << 16);
	if (ret)
		return ret;

	buf.len = MT_DMA_HDR_LEN + len + 4;
	ret = mt76x0_usb_submit_buf(dev, USB_DIR_OUT, MT_EP_OUT_INBAND_CMD,
				     &buf, GFP_KERNEL,
				     mt76x0_complete_urb, &cmpl);
	if (ret)
		return ret;

	if (!wait_for_completion_timeout(&cmpl, msecs_to_jiffies(1000))) {
		dev_err(dev->mt76.dev, "Error: firmware upload timed out\n");
		usb_kill_urb(buf.urb);
		return -ETIMEDOUT;
	}
	if (mt76x0_urb_has_error(buf.urb)) {
		dev_err(dev->mt76.dev, "Error: firmware upload urb failed:%d\n",
			buf.urb->status);
		return buf.urb->status;
	}

	val = mt76_rr(dev, MT_TX_CPU_FROM_FCE_CPU_DESC_IDX);
	val++;
	mt76_wr(dev, MT_TX_CPU_FROM_FCE_CPU_DESC_IDX, val);

	msleep(5);

	return 0;
}

static int
mt76x0_dma_fw(struct mt76x0_dev *dev, struct mt76x0_dma_buf *dma_buf,
	       const void *data, int len, u32 dst_addr)
{
	int n, ret;

	if (len == 0)
		return 0;

	n = min(MCU_FW_URB_MAX_PAYLOAD, len);
	ret = __mt76x0_dma_fw(dev, dma_buf, data, n, dst_addr);
	if (ret)
		return ret;

#if 0
	if (!mt76_poll_msec(dev, MT_MCU_COM_REG1, BIT(31), BIT(31), 500))
		return -ETIMEDOUT;
#endif

	return mt76x0_dma_fw(dev, dma_buf, data + n, len - n, dst_addr + n);
}

static int
mt76x0_upload_firmware(struct mt76x0_dev *dev, const struct mt76_fw *fw)
{
	struct mt76x0_dma_buf dma_buf;
	void *ivb;
	u32 ilm_len, dlm_len;
	int i, ret;

	ivb = kmemdup(fw->ivb, sizeof(fw->ivb), GFP_KERNEL);
	if (!ivb)
		return -ENOMEM;
	if (mt76x0_usb_alloc_buf(dev, MCU_FW_URB_SIZE, &dma_buf)) {
		ret = -ENOMEM;
		goto error;
	}

	ilm_len = le32_to_cpu(fw->hdr.ilm_len) - sizeof(fw->ivb);
	dev_dbg(dev->mt76.dev, "loading FW - ILM %u + IVB %zu\n",
		ilm_len, sizeof(fw->ivb));
	ret = mt76x0_dma_fw(dev, &dma_buf, fw->ilm, ilm_len, sizeof(fw->ivb));
	if (ret)
		goto error;

	dlm_len = le32_to_cpu(fw->hdr.dlm_len);
	dev_dbg(dev->mt76.dev, "loading FW - DLM %u\n", dlm_len);
	ret = mt76x0_dma_fw(dev, &dma_buf, fw->ilm + ilm_len,
			     dlm_len, MT_MCU_DLM_OFFSET);
	if (ret)
		goto error;

	ret = mt76x0_vendor_request(dev, MT_VEND_DEV_MODE, USB_DIR_OUT,
				     0x12, 0, ivb, sizeof(fw->ivb));
	if (ret < 0)
		goto error;
	ret = 0;

	for (i = 100; i && !firmware_running(dev); i--)
		msleep(10);
	if (!i) {
		ret = -ETIMEDOUT;
		goto error;
	}

	dev_dbg(dev->mt76.dev, "Firmware running!\n");
error:
	kfree(ivb);
	mt76x0_usb_free_buf(dev, &dma_buf);

	return ret;
}

static int mt76x0_load_firmware(struct mt76x0_dev *dev)
{
	const struct firmware *fw;
	const struct mt76_fw_header *hdr;
	int len, ret;
	u32 val;

	mt76_wr(dev, MT_USB_DMA_CFG, (MT_USB_DMA_CFG_RX_BULK_EN |
					 MT_USB_DMA_CFG_TX_BULK_EN));

	if (firmware_running(dev))
		return 0;

	ret = request_firmware(&fw, MT7610_FIRMWARE, dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data || fw->size < sizeof(*hdr))
		goto err_inv_fw;

	hdr = (const struct mt76_fw_header *) fw->data;

	if (le32_to_cpu(hdr->ilm_len) <= MT_MCU_IVB_SIZE)
		goto err_inv_fw;

	len = sizeof(*hdr);
	len += le32_to_cpu(hdr->ilm_len);
	len += le32_to_cpu(hdr->dlm_len);

	if (fw->size != len)
		goto err_inv_fw;

	val = le16_to_cpu(hdr->fw_ver);
	dev_dbg(dev->mt76.dev,
		 "Firmware Version: %d.%d.%02d Build: %x Build time: %.16s\n",
		 (val >> 12) & 0xf, (val >> 8) & 0xf, val & 0xf,
		 le16_to_cpu(hdr->build_ver), hdr->build_time);

	len = le32_to_cpu(hdr->ilm_len);

	mt76_wr(dev, 0x1004, 0x2c);

	mt76_set(dev, MT_USB_DMA_CFG, (MT_USB_DMA_CFG_RX_BULK_EN |
				       MT_USB_DMA_CFG_TX_BULK_EN) |
				       FIELD_PREP(MT_USB_DMA_CFG_RX_BULK_AGG_TOUT, 0x20));
	mt76x0_vendor_reset(dev);
	msleep(5);
/*
	mt76x0_rmw(dev, MT_PBF_CFG, 0, (MT_PBF_CFG_TX0Q_EN |
					 MT_PBF_CFG_TX1Q_EN |
					 MT_PBF_CFG_TX2Q_EN |
					 MT_PBF_CFG_TX3Q_EN));
*/

	mt76_wr(dev, MT_FCE_PSE_CTRL, 1);

	/* FCE tx_fs_base_ptr */
	mt76_wr(dev, MT_TX_CPU_FROM_FCE_BASE_PTR, 0x400230);
	/* FCE tx_fs_max_cnt */
	mt76_wr(dev, MT_TX_CPU_FROM_FCE_MAX_COUNT, 1);
	/* FCE pdma enable */
	mt76_wr(dev, MT_FCE_PDMA_GLOBAL_CONF, 0x44);
	/* FCE skip_fs_en */
	mt76_wr(dev, MT_FCE_SKIP_FS, 3);

	val = mt76_rr(dev, MT_USB_DMA_CFG);
	val |= MT_USB_DMA_CFG_TX_WL_DROP;
	mt76_wr(dev, MT_USB_DMA_CFG, val);
	val &= ~MT_USB_DMA_CFG_TX_WL_DROP;
	mt76_wr(dev, MT_USB_DMA_CFG, val);

	ret = mt76x0_upload_firmware(dev, (const struct mt76_fw *)fw->data);
	release_firmware(fw);

	mt76_wr(dev, MT_FCE_PSE_CTRL, 1);

	return ret;

err_inv_fw:
	dev_err(dev->mt76.dev, "Invalid firmware image\n");
	release_firmware(fw);
	return -ENOENT;
}

int mt76x0_mcu_init(struct mt76x0_dev *dev)
{
	int ret;

	mutex_init(&dev->mcu.mutex);

	ret = mt76x0_load_firmware(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_MCU_RUNNING, &dev->mt76.state);

	return 0;
}

int mt76x0_mcu_cmd_init(struct mt76x0_dev *dev)
{
	int ret;

	ret = mt76x0_mcu_function_select(dev, Q_SELECT, 1);
	if (ret)
		return ret;

	init_completion(&dev->mcu.resp_cmpl);
	if (mt76x0_usb_alloc_buf(dev, MCU_RESP_URB_SIZE, &dev->mcu.resp)) {
		mt76x0_usb_free_buf(dev, &dev->mcu.resp);
		return -ENOMEM;
	}

	ret = mt76x0_usb_submit_buf(dev, USB_DIR_IN, MT_EP_IN_CMD_RESP,
				     &dev->mcu.resp, GFP_KERNEL,
				     mt76x0_complete_urb, &dev->mcu.resp_cmpl);
	if (ret) {
		mt76x0_usb_free_buf(dev, &dev->mcu.resp);
		return ret;
	}

	return 0;
}

void mt76x0_mcu_cmd_deinit(struct mt76x0_dev *dev)
{
	usb_kill_urb(dev->mcu.resp.urb);
	mt76x0_usb_free_buf(dev, &dev->mcu.resp);
}
