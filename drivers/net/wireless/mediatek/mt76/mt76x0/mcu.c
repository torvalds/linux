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

	skb = mt76u_mcu_msg_alloc(&msg, sizeof(msg));
	if (!skb)
		return -ENOMEM;
	return mt76u_mcu_send_msg(&dev->mt76, skb, CMD_FUN_SET_OP,
				  func == 5);
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

	skb = mt76u_mcu_msg_alloc(&msg, sizeof(msg));
	if (!skb)
		return -ENOMEM;
	return mt76u_mcu_send_msg(&dev->mt76, skb, CMD_CALIBRATION_OP,
				  true);
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

	ret = mt76u_mcu_send_msg(&dev->mt76, skb, CMD_RANDOM_WRITE,
				 cnt == n);
	if (ret)
		return ret;

	return mt76x0_write_reg_pairs(dev, base, data + cnt, n - cnt);
}

int mt76x0_read_reg_pairs(struct mt76x0_dev *dev, u32 base,
			  struct mt76_reg_pair *data, int n)
{
	const int max_vals_per_cmd = INBAND_PACKET_MAX_LEN / 8;
	struct mt76_usb *usb = &dev->mt76.usb;
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

	ret = __mt76u_mcu_send_msg(&dev->mt76, skb, CMD_RANDOM_READ,
				   true);

	usb->mcu.rp = NULL;

	mutex_unlock(&usb->mcu.mutex);

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

	ret = mt76u_mcu_send_msg(&dev->mt76, skb, CMD_BURST_WRITE,
				 cnt == n);
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
	struct mt76_usb *usb = &dev->mt76.usb;
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

	mutex_lock(&usb->mcu.mutex);

	usb->mcu.rp = data;
	usb->mcu.rp_len = n;
	usb->mcu.base = base;
	usb->mcu.burst = false;

	ret = __mt76u_mcu_send_msg(&dev->mt76, skb, CMD_BURST_READ, true);

	usb->mcu.rp = NULL;

	mutex_unlock(&usb->mcu.mutex);

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

static int
mt76x0_upload_firmware(struct mt76x0_dev *dev, const struct mt76_fw *fw)
{
	void *ivb;
	u32 ilm_len, dlm_len;
	int i, ret;

	ivb = kmemdup(fw->ivb, sizeof(fw->ivb), GFP_KERNEL);
	if (!ivb)
		return -ENOMEM;

	ilm_len = le32_to_cpu(fw->hdr.ilm_len) - sizeof(fw->ivb);
	dev_dbg(dev->mt76.dev, "loading FW - ILM %u + IVB %zu\n",
		ilm_len, sizeof(fw->ivb));
	ret = mt76u_mcu_fw_send_data(&dev->mt76, fw->ilm, ilm_len,
				     MCU_FW_URB_MAX_PAYLOAD,
				     sizeof(fw->ivb));
	if (ret)
		goto error;

	dlm_len = le32_to_cpu(fw->hdr.dlm_len);
	dev_dbg(dev->mt76.dev, "loading FW - DLM %u\n", dlm_len);
	ret = mt76u_mcu_fw_send_data(&dev->mt76, fw->ilm + ilm_len,
				     dlm_len, MCU_FW_URB_MAX_PAYLOAD,
				     MT_MCU_DLM_OFFSET);
	if (ret)
		goto error;

	ret = mt76u_vendor_request(&dev->mt76, MT_VEND_DEV_MODE,
				   USB_DIR_OUT | USB_TYPE_VENDOR,
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
	mt76u_mcu_fw_reset(&dev->mt76);
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
	val |= MT_USB_DMA_CFG_UDMA_TX_WL_DROP;
	mt76_wr(dev, MT_USB_DMA_CFG, val);
	val &= ~MT_USB_DMA_CFG_UDMA_TX_WL_DROP;
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

	ret = mt76x0_load_firmware(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_MCU_RUNNING, &dev->mt76.state);

	return 0;
}

int mt76x0_mcu_cmd_init(struct mt76x0_dev *dev)
{
	int ret = mt76x0_mcu_function_select(dev, Q_SELECT, 1);
	if (ret)
		return ret;

	return mt76u_mcu_init_rx(&dev->mt76);
}
