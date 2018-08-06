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

#include "mt76x2u.h"
#include "mt76x2_eeprom.h"

#define MT_CMD_HDR_LEN			4
#define MT_INBAND_PACKET_MAX_LEN	192
#define MT_MCU_MEMMAP_WLAN		0x410000

#define MCU_FW_URB_MAX_PAYLOAD		0x3900
#define MCU_ROM_PATCH_MAX_PAYLOAD	2048

#define MT76U_MCU_ILM_OFFSET		0x80000
#define MT76U_MCU_DLM_OFFSET		0x110000
#define MT76U_MCU_ROM_PATCH_OFFSET	0x90000

static int
mt76x2u_mcu_function_select(struct mt76x2_dev *dev, enum mcu_function func,
			    u32 val)
{
	struct {
		__le32 id;
		__le32 value;
	} __packed __aligned(4) msg = {
		.id = cpu_to_le32(func),
		.value = cpu_to_le32(val),
	};
	struct sk_buff *skb;

	skb = mt76u_mcu_msg_alloc(&msg, sizeof(msg));
	if (!skb)
		return -ENOMEM;
	return mt76u_mcu_send_msg(&dev->mt76, skb, CMD_FUN_SET_OP,
				  func != Q_SELECT);
}

int mt76x2u_mcu_set_radio_state(struct mt76x2_dev *dev, bool val)
{
	struct {
		__le32 mode;
		__le32 level;
	} __packed __aligned(4) msg = {
		.mode = cpu_to_le32(val ? RADIO_ON : RADIO_OFF),
		.level = cpu_to_le32(0),
	};
	struct sk_buff *skb;

	skb = mt76u_mcu_msg_alloc(&msg, sizeof(msg));
	if (!skb)
		return -ENOMEM;
	return mt76u_mcu_send_msg(&dev->mt76, skb, CMD_POWER_SAVING_OP,
				  false);
}

int mt76x2u_mcu_load_cr(struct mt76x2_dev *dev, u8 type, u8 temp_level,
			u8 channel)
{
	struct {
		u8 cr_mode;
		u8 temp;
		u8 ch;
		u8 _pad0;
		__le32 cfg;
	} __packed __aligned(4) msg = {
		.cr_mode = type,
		.temp = temp_level,
		.ch = channel,
	};
	struct sk_buff *skb;
	u32 val;

	val = BIT(31);
	val |= (mt76x2_eeprom_get(dev, MT_EE_NIC_CONF_0) >> 8) & 0x00ff;
	val |= (mt76x2_eeprom_get(dev, MT_EE_NIC_CONF_1) << 8) & 0xff00;
	msg.cfg = cpu_to_le32(val);

	/* first set the channel without the extension channel info */
	skb = mt76u_mcu_msg_alloc(&msg, sizeof(msg));
	if (!skb)
		return -ENOMEM;
	return mt76u_mcu_send_msg(&dev->mt76, skb, CMD_LOAD_CR, true);
}

int mt76x2u_mcu_set_channel(struct mt76x2_dev *dev, u8 channel, u8 bw,
			    u8 bw_index, bool scan)
{
	struct {
		u8 idx;
		u8 scan;
		u8 bw;
		u8 _pad0;

		__le16 chainmask;
		u8 ext_chan;
		u8 _pad1;

	} __packed __aligned(4) msg = {
		.idx = channel,
		.scan = scan,
		.bw = bw,
		.chainmask = cpu_to_le16(dev->chainmask),
	};
	struct sk_buff *skb;

	/* first set the channel without the extension channel info */
	skb = mt76u_mcu_msg_alloc(&msg, sizeof(msg));
	if (!skb)
		return -ENOMEM;

	mt76u_mcu_send_msg(&dev->mt76, skb, CMD_SWITCH_CHANNEL_OP, true);

	usleep_range(5000, 10000);

	msg.ext_chan = 0xe0 + bw_index;
	skb = mt76u_mcu_msg_alloc(&msg, sizeof(msg));
	if (!skb)
		return -ENOMEM;

	return mt76u_mcu_send_msg(&dev->mt76, skb, CMD_SWITCH_CHANNEL_OP, true);
}

int mt76x2u_mcu_calibrate(struct mt76x2_dev *dev, enum mcu_calibration type,
			  u32 val)
{
	struct {
		__le32 id;
		__le32 value;
	} __packed __aligned(4) msg = {
		.id = cpu_to_le32(type),
		.value = cpu_to_le32(val),
	};
	struct sk_buff *skb;

	skb = mt76u_mcu_msg_alloc(&msg, sizeof(msg));
	if (!skb)
		return -ENOMEM;
	return mt76u_mcu_send_msg(&dev->mt76, skb, CMD_CALIBRATION_OP, true);
}

int mt76x2u_mcu_init_gain(struct mt76x2_dev *dev, u8 channel, u32 gain,
			  bool force)
{
	struct {
		__le32 channel;
		__le32 gain_val;
	} __packed __aligned(4) msg = {
		.channel = cpu_to_le32(channel),
		.gain_val = cpu_to_le32(gain),
	};
	struct sk_buff *skb;

	if (force)
		msg.channel |= cpu_to_le32(BIT(31));

	skb = mt76u_mcu_msg_alloc(&msg, sizeof(msg));
	if (!skb)
		return -ENOMEM;
	return mt76u_mcu_send_msg(&dev->mt76, skb, CMD_INIT_GAIN_OP, true);
}

int mt76x2u_mcu_set_dynamic_vga(struct mt76x2_dev *dev, u8 channel, bool ap,
				bool ext, int rssi, u32 false_cca)
{
	struct {
		__le32 channel;
		__le32 rssi_val;
		__le32 false_cca_val;
	} __packed __aligned(4) msg = {
		.rssi_val = cpu_to_le32(rssi),
		.false_cca_val = cpu_to_le32(false_cca),
	};
	struct sk_buff *skb;
	u32 val = channel;

	if (ap)
		val |= BIT(31);
	if (ext)
		val |= BIT(30);
	msg.channel = cpu_to_le32(val);

	skb = mt76u_mcu_msg_alloc(&msg, sizeof(msg));
	if (!skb)
		return -ENOMEM;
	return mt76u_mcu_send_msg(&dev->mt76, skb, CMD_DYNC_VGA_OP, true);
}

int mt76x2u_mcu_tssi_comp(struct mt76x2_dev *dev,
			  struct mt76x2_tssi_comp *tssi_data)
{
	struct {
		__le32 id;
		struct mt76x2_tssi_comp data;
	} __packed __aligned(4) msg = {
		.id = cpu_to_le32(MCU_CAL_TSSI_COMP),
		.data = *tssi_data,
	};
	struct sk_buff *skb;

	skb = mt76u_mcu_msg_alloc(&msg, sizeof(msg));
	if (!skb)
		return -ENOMEM;
	return mt76u_mcu_send_msg(&dev->mt76, skb, CMD_CALIBRATION_OP, true);
}

static void mt76x2u_mcu_load_ivb(struct mt76x2_dev *dev)
{
	mt76u_vendor_request(&dev->mt76, MT_VEND_DEV_MODE,
			     USB_DIR_OUT | USB_TYPE_VENDOR,
			     0x12, 0, NULL, 0);
}

static void mt76x2u_mcu_enable_patch(struct mt76x2_dev *dev)
{
	struct mt76_usb *usb = &dev->mt76.usb;
	const u8 data[] = {
		0x6f, 0xfc, 0x08, 0x01,
		0x20, 0x04, 0x00, 0x00,
		0x00, 0x09, 0x00,
	};

	memcpy(usb->data, data, sizeof(data));
	mt76u_vendor_request(&dev->mt76, MT_VEND_DEV_MODE,
			     USB_DIR_OUT | USB_TYPE_CLASS,
			     0x12, 0, usb->data, sizeof(data));
}

static void mt76x2u_mcu_reset_wmt(struct mt76x2_dev *dev)
{
	struct mt76_usb *usb = &dev->mt76.usb;
	u8 data[] = {
		0x6f, 0xfc, 0x05, 0x01,
		0x07, 0x01, 0x00, 0x04
	};

	memcpy(usb->data, data, sizeof(data));
	mt76u_vendor_request(&dev->mt76, MT_VEND_DEV_MODE,
			     USB_DIR_OUT | USB_TYPE_CLASS,
			     0x12, 0, usb->data, sizeof(data));
}

static int mt76x2u_mcu_load_rom_patch(struct mt76x2_dev *dev)
{
	bool rom_protect = !is_mt7612(dev);
	struct mt76x2_patch_header *hdr;
	u32 val, patch_mask, patch_reg;
	const struct firmware *fw;
	int err;

	if (rom_protect &&
	    !mt76_poll_msec(dev, MT_MCU_SEMAPHORE_03, 1, 1, 600)) {
		dev_err(dev->mt76.dev,
			"could not get hardware semaphore for ROM PATCH\n");
		return -ETIMEDOUT;
	}

	if (mt76xx_rev(dev) >= MT76XX_REV_E3) {
		patch_mask = BIT(0);
		patch_reg = MT_MCU_CLOCK_CTL;
	} else {
		patch_mask = BIT(1);
		patch_reg = MT_MCU_COM_REG0;
	}

	if (rom_protect && (mt76_rr(dev, patch_reg) & patch_mask)) {
		dev_info(dev->mt76.dev, "ROM patch already applied\n");
		return 0;
	}

	err = request_firmware(&fw, MT7662U_ROM_PATCH, dev->mt76.dev);
	if (err < 0)
		return err;

	if (!fw || !fw->data || fw->size <= sizeof(*hdr)) {
		dev_err(dev->mt76.dev, "failed to load firmware\n");
		err = -EIO;
		goto out;
	}

	hdr = (struct mt76x2_patch_header *)fw->data;
	dev_info(dev->mt76.dev, "ROM patch build: %.15s\n", hdr->build_time);

	/* enable USB_DMA_CFG */
	val = MT_USB_DMA_CFG_RX_BULK_EN |
	      MT_USB_DMA_CFG_TX_BULK_EN |
	      FIELD_PREP(MT_USB_DMA_CFG_RX_BULK_AGG_TOUT, 0x20);
	mt76_wr(dev, MT_VEND_ADDR(CFG, MT_USB_U3DMA_CFG), val);

	/* vendor reset */
	mt76u_mcu_fw_reset(&dev->mt76);
	usleep_range(5000, 10000);

	/* enable FCE to send in-band cmd */
	mt76_wr(dev, MT_FCE_PSE_CTRL, 0x1);
	/* FCE tx_fs_base_ptr */
	mt76_wr(dev, MT_TX_CPU_FROM_FCE_BASE_PTR, 0x400230);
	/* FCE tx_fs_max_cnt */
	mt76_wr(dev, MT_TX_CPU_FROM_FCE_MAX_COUNT, 0x1);
	/* FCE pdma enable */
	mt76_wr(dev, MT_FCE_PDMA_GLOBAL_CONF, 0x44);
	/* FCE skip_fs_en */
	mt76_wr(dev, MT_FCE_SKIP_FS, 0x3);

	err = mt76u_mcu_fw_send_data(&dev->mt76, fw->data + sizeof(*hdr),
				     fw->size - sizeof(*hdr),
				     MCU_ROM_PATCH_MAX_PAYLOAD,
				     MT76U_MCU_ROM_PATCH_OFFSET);
	if (err < 0) {
		err = -EIO;
		goto out;
	}

	mt76x2u_mcu_enable_patch(dev);
	mt76x2u_mcu_reset_wmt(dev);
	mdelay(20);

	if (!mt76_poll_msec(dev, patch_reg, patch_mask, patch_mask, 100)) {
		dev_err(dev->mt76.dev, "failed to load ROM patch\n");
		err = -ETIMEDOUT;
	}

out:
	if (rom_protect)
		mt76_wr(dev, MT_MCU_SEMAPHORE_03, 1);
	release_firmware(fw);
	return err;
}

static int mt76x2u_mcu_load_firmware(struct mt76x2_dev *dev)
{
	u32 val, dlm_offset = MT76U_MCU_DLM_OFFSET;
	const struct mt76x2_fw_header *hdr;
	int err, len, ilm_len, dlm_len;
	const struct firmware *fw;

	err = request_firmware(&fw, MT7662U_FIRMWARE, dev->mt76.dev);
	if (err < 0)
		return err;

	if (!fw || !fw->data || fw->size < sizeof(*hdr)) {
		err = -EINVAL;
		goto out;
	}

	hdr = (const struct mt76x2_fw_header *)fw->data;
	ilm_len = le32_to_cpu(hdr->ilm_len);
	dlm_len = le32_to_cpu(hdr->dlm_len);
	len = sizeof(*hdr) + ilm_len + dlm_len;
	if (fw->size != len) {
		err = -EINVAL;
		goto out;
	}

	val = le16_to_cpu(hdr->fw_ver);
	dev_info(dev->mt76.dev, "Firmware Version: %d.%d.%02d\n",
		 (val >> 12) & 0xf, (val >> 8) & 0xf, val & 0xf);

	val = le16_to_cpu(hdr->build_ver);
	dev_info(dev->mt76.dev, "Build: %x\n", val);
	dev_info(dev->mt76.dev, "Build Time: %.16s\n", hdr->build_time);

	/* vendor reset */
	mt76u_mcu_fw_reset(&dev->mt76);
	usleep_range(5000, 10000);

	/* enable USB_DMA_CFG */
	val = MT_USB_DMA_CFG_RX_BULK_EN |
	      MT_USB_DMA_CFG_TX_BULK_EN |
	      FIELD_PREP(MT_USB_DMA_CFG_RX_BULK_AGG_TOUT, 0x20);
	mt76_wr(dev, MT_VEND_ADDR(CFG, MT_USB_U3DMA_CFG), val);
	/* enable FCE to send in-band cmd */
	mt76_wr(dev, MT_FCE_PSE_CTRL, 0x1);
	/* FCE tx_fs_base_ptr */
	mt76_wr(dev, MT_TX_CPU_FROM_FCE_BASE_PTR, 0x400230);
	/* FCE tx_fs_max_cnt */
	mt76_wr(dev, MT_TX_CPU_FROM_FCE_MAX_COUNT, 0x1);
	/* FCE pdma enable */
	mt76_wr(dev, MT_FCE_PDMA_GLOBAL_CONF, 0x44);
	/* FCE skip_fs_en */
	mt76_wr(dev, MT_FCE_SKIP_FS, 0x3);

	/* load ILM */
	err = mt76u_mcu_fw_send_data(&dev->mt76, fw->data + sizeof(*hdr),
				     ilm_len, MCU_FW_URB_MAX_PAYLOAD,
				     MT76U_MCU_ILM_OFFSET);
	if (err < 0) {
		err = -EIO;
		goto out;
	}

	/* load DLM */
	if (mt76xx_rev(dev) >= MT76XX_REV_E3)
		dlm_offset += 0x800;
	err = mt76u_mcu_fw_send_data(&dev->mt76,
				     fw->data + sizeof(*hdr) + ilm_len,
				     dlm_len, MCU_FW_URB_MAX_PAYLOAD,
				     dlm_offset);
	if (err < 0) {
		err = -EIO;
		goto out;
	}

	mt76x2u_mcu_load_ivb(dev);
	if (!mt76_poll_msec(dev, MT_MCU_COM_REG0, 1, 1, 100)) {
		dev_err(dev->mt76.dev, "firmware failed to start\n");
		err = -ETIMEDOUT;
		goto out;
	}

	mt76_set(dev, MT_MCU_COM_REG0, BIT(1));
	/* enable FCE to send in-band cmd */
	mt76_wr(dev, MT_FCE_PSE_CTRL, 0x1);
	dev_dbg(dev->mt76.dev, "firmware running\n");

out:
	release_firmware(fw);
	return err;
}

int mt76x2u_mcu_fw_init(struct mt76x2_dev *dev)
{
	int err;

	err = mt76x2u_mcu_load_rom_patch(dev);
	if (err < 0)
		return err;

	return mt76x2u_mcu_load_firmware(dev);
}

int mt76x2u_mcu_init(struct mt76x2_dev *dev)
{
	int err;

	err = mt76x2u_mcu_function_select(dev, Q_SELECT, 1);
	if (err < 0)
		return err;

	return mt76x2u_mcu_set_radio_state(dev, true);
}

void mt76x2u_mcu_deinit(struct mt76x2_dev *dev)
{
	struct mt76_usb *usb = &dev->mt76.usb;

	usb_kill_urb(usb->mcu.res.urb);
	mt76u_buf_free(&usb->mcu.res);
}
