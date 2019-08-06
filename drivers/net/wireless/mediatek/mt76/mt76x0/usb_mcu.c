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
#include <linux/kernel.h>
#include <linux/firmware.h>

#include "mt76x0.h"
#include "mcu.h"
#include "../mt76x02_usb.h"

#define MCU_FW_URB_MAX_PAYLOAD		0x38f8
#define MCU_FW_URB_SIZE			(MCU_FW_URB_MAX_PAYLOAD + 12)

static int
mt76x0u_upload_firmware(struct mt76x02_dev *dev,
			const struct mt76x02_fw_header *hdr)
{
	u8 *fw_payload = (u8 *)(hdr + 1);
	u32 ilm_len, dlm_len;
	void *ivb;
	int err;

	ivb = kmemdup(fw_payload, MT_MCU_IVB_SIZE, GFP_KERNEL);
	if (!ivb)
		return -ENOMEM;

	ilm_len = le32_to_cpu(hdr->ilm_len) - MT_MCU_IVB_SIZE;
	dev_dbg(dev->mt76.dev, "loading FW - ILM %u + IVB %u\n",
		ilm_len, MT_MCU_IVB_SIZE);
	err = mt76x02u_mcu_fw_send_data(dev, fw_payload + MT_MCU_IVB_SIZE,
					ilm_len, MCU_FW_URB_MAX_PAYLOAD,
					MT_MCU_IVB_SIZE);
	if (err)
		goto out;

	dlm_len = le32_to_cpu(hdr->dlm_len);
	dev_dbg(dev->mt76.dev, "loading FW - DLM %u\n", dlm_len);
	err = mt76x02u_mcu_fw_send_data(dev,
					fw_payload + le32_to_cpu(hdr->ilm_len),
					dlm_len, MCU_FW_URB_MAX_PAYLOAD,
					MT_MCU_DLM_OFFSET);
	if (err)
		goto out;

	err = mt76u_vendor_request(&dev->mt76, MT_VEND_DEV_MODE,
				   USB_DIR_OUT | USB_TYPE_VENDOR,
				   0x12, 0, ivb, MT_MCU_IVB_SIZE);
	if (err < 0)
		goto out;

	if (!mt76_poll_msec(dev, MT_MCU_COM_REG0, 1, 1, 1000)) {
		dev_err(dev->mt76.dev, "Firmware failed to start\n");
		err = -ETIMEDOUT;
		goto out;
	}

	dev_dbg(dev->mt76.dev, "Firmware running!\n");

out:
	kfree(ivb);

	return err;
}

static int mt76x0_get_firmware(struct mt76x02_dev *dev,
			       const struct firmware **fw)
{
	int err;

	/* try to load mt7610e fw if available
	 * otherwise fall back to mt7610u one
	 */
	err = firmware_request_nowarn(fw, MT7610E_FIRMWARE, dev->mt76.dev);
	if (err) {
		dev_info(dev->mt76.dev, "%s not found, switching to %s",
			 MT7610E_FIRMWARE, MT7610U_FIRMWARE);
		return request_firmware(fw, MT7610U_FIRMWARE,
					dev->mt76.dev);
	}
	return 0;
}

static int mt76x0u_load_firmware(struct mt76x02_dev *dev)
{
	const struct firmware *fw;
	const struct mt76x02_fw_header *hdr;
	int len, ret;
	u32 val;

	mt76_wr(dev, MT_USB_DMA_CFG, (MT_USB_DMA_CFG_RX_BULK_EN |
				      MT_USB_DMA_CFG_TX_BULK_EN));

	if (mt76x0_firmware_running(dev))
		return 0;

	ret = mt76x0_get_firmware(dev, &fw);
	if (ret)
		return ret;

	if (!fw || !fw->data || fw->size < sizeof(*hdr))
		goto err_inv_fw;

	hdr = (const struct mt76x02_fw_header *)fw->data;

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

	mt76_set(dev, MT_USB_DMA_CFG,
		 (MT_USB_DMA_CFG_RX_BULK_EN | MT_USB_DMA_CFG_TX_BULK_EN) |
		 FIELD_PREP(MT_USB_DMA_CFG_RX_BULK_AGG_TOUT, 0x20));
	mt76x02u_mcu_fw_reset(dev);
	usleep_range(5000, 6000);
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

	ret = mt76x0u_upload_firmware(dev, hdr);
	release_firmware(fw);

	mt76_wr(dev, MT_FCE_PSE_CTRL, 1);

	return ret;

err_inv_fw:
	dev_err(dev->mt76.dev, "Invalid firmware image\n");
	release_firmware(fw);
	return -ENOENT;
}

int mt76x0u_mcu_init(struct mt76x02_dev *dev)
{
	int ret;

	ret = mt76x0u_load_firmware(dev);
	if (ret < 0)
		return ret;

	set_bit(MT76_STATE_MCU_RUNNING, &dev->mt76.state);

	return 0;
}
