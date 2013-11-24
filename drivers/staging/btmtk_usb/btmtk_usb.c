/*
 *  MediaTek Bluetooth USB Driver
 *
 *  Copyright (C) 2013, MediaTek co.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *  or on the worldwide web at
 *  http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/completion.h>
#include <linux/firmware.h>
#include <linux/usb.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btmtk_usb.h"

#define VERSION "1.0.4"
#define MT7650_FIRMWARE	"mt7650.bin"
#define MT7662_FIRMWARE	"mt7662.bin"

static struct usb_driver btmtk_usb_driver;


static int btmtk_usb_load_rom_patch(struct btmtk_usb_data *);
static int btmtk_usb_load_fw(struct btmtk_usb_data *);

static void hex_dump(char *str, u8 *src_buf, u32 src_buf_len)
{
	unsigned char *pt;
	int x;

	pt = src_buf;

	BT_DBG("%s: %p, len = %d\n", str, src_buf, src_buf_len);

	for (x = 0; x < src_buf_len; x++) {
		if (x % 16 == 0)
			BT_DBG("0x%04x : ", x);
		BT_DBG("%02x ", ((unsigned char)pt[x]));
		if (x % 16 == 15)
			BT_DBG("\n");
	}

	BT_DBG("\n");
}

static int btmtk_usb_reset(struct usb_device *udev)
{
	int ret;

	BT_DBG("%s\n", __func__);

	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0x01,
			DEVICE_VENDOR_REQUEST_OUT, 0x01, 0x00,
			NULL, 0x00, CONTROL_TIMEOUT_JIFFIES);

	if (ret < 0) {
		BT_ERR("%s error(%d)\n", __func__, ret);
		return ret;
	}

	if (ret > 0)
		ret = 0;

	return ret;
}

static int btmtk_usb_io_read32(struct btmtk_usb_data *data, u32 reg, u32 *val)
{
	u8 request = data->r_request;
	struct usb_device *udev = data->udev;
	int ret;
	__le32 val_le;

	ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0), request,
			DEVICE_VENDOR_REQUEST_IN, 0x0, reg, data->io_buf,
			4, CONTROL_TIMEOUT_JIFFIES);

	if (ret < 0) {
		*val = 0xffffffff;
		BT_ERR("%s error(%d), reg=%x, value=%x\n",
				__func__, ret, reg, *val);
		return ret;
	}

	memmove(&val_le, data->io_buf, 4);

	*val = le32_to_cpu(val_le);

	if (ret > 0)
		ret = 0;

	return ret;
}

static int btmtk_usb_io_write32(struct btmtk_usb_data *data, u32 reg, u32 val)
{
	u16 value, index;
	u8 request = data->w_request;
	struct usb_device *udev = data->udev;
	int ret;

	index = (u16)reg;
	value = val & 0x0000ffff;

	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), request,
			DEVICE_VENDOR_REQUEST_OUT, value, index,
			NULL, 0, CONTROL_TIMEOUT_JIFFIES);

	if (ret < 0) {
		BT_ERR("%s error(%d), reg=%x, value=%x\n",
				__func__, ret, reg, val);
		return ret;
	}

	index = (u16)(reg + 2);
	value = (val & 0xffff0000) >> 16;

	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0),
				request, DEVICE_VENDOR_REQUEST_OUT,
				value, index, NULL, 0, CONTROL_TIMEOUT_JIFFIES);

	if (ret < 0) {
		BT_ERR("%s error(%d), reg=%x, value=%x\n",
				__func__, ret, reg, val);
		return ret;
	}

	if (ret > 0)
		ret = 0;

	return ret;
}

static int btmtk_usb_switch_iobase(struct btmtk_usb_data *data, int base)
{
	int ret = 0;

	switch (base) {
	case SYSCTL:
		data->w_request = 0x42;
		data->r_request = 0x47;
		break;
	case WLAN:
		data->w_request = 0x02;
		data->r_request = 0x07;
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

static void btmtk_usb_cap_init(struct btmtk_usb_data *data)
{
	const struct firmware	*firmware;
	struct usb_device   *udev = data->udev;
	int ret;

	btmtk_usb_io_read32(data, 0x00, &data->chip_id);

	BT_DBG("chip id = %x\n", data->chip_id);

	if (is_mt7630(data) || is_mt7650(data)) {
		data->need_load_fw = 1;
		data->need_load_rom_patch = 0;
		ret = request_firmware(&firmware, MT7650_FIRMWARE, &udev->dev);
		if (ret < 0) {
			if (ret == -ENOENT) {
				BT_ERR("Firmware file \"%s\" not found\n",
						MT7650_FIRMWARE);
			} else {
				BT_ERR("Firmware file \"%s\" request failed (err=%d)\n",
					MT7650_FIRMWARE, ret);
			}
		} else {
			BT_DBG("Firmware file \"%s\" Found\n",
					MT7650_FIRMWARE);
			/* load firmware here */
			data->firmware = firmware;
			btmtk_usb_load_fw(data);
		}
		release_firmware(firmware);
	} else if (is_mt7632(data) || is_mt7662(data)) {
		data->need_load_fw = 0;
		data->need_load_rom_patch = 1;
		data->rom_patch_offset = 0x90000;
		ret = request_firmware(&firmware, MT7662_FIRMWARE, &udev->dev);
		if (ret < 0) {
			if (ret == -ENOENT) {
				BT_ERR("Firmware file \"%s\" not found\n",
						MT7662_FIRMWARE);
			} else {
				BT_ERR("Firmware file \"%s\" request failed (err=%d)\n",
					MT7662_FIRMWARE, ret);
			}
		} else {
		    BT_DBG("Firmware file \"%s\" Found\n", MT7662_FIRMWARE);
		    /* load rom patch here */
		    data->firmware = firmware;
		    data->rom_patch_len = firmware->size;
		    btmtk_usb_load_rom_patch(data);
		}
		release_firmware(firmware);
	} else {
		BT_ERR("unknow chip(%x)\n", data->chip_id);
	}
}

static u16 checksume16(u8 *pData, int len)
{
	int sum = 0;

	while (len > 1) {
		sum += *((u16 *)pData);

		pData = pData + 2;

		if (sum & 0x80000000)
			sum = (sum & 0xFFFF) + (sum >> 16);

		len -= 2;
	}

	if (len)
		sum += *((u8 *)pData);

	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);

	return ~sum;
}

static int btmtk_usb_chk_crc(struct btmtk_usb_data *data, u32 checksum_len)
{
	int ret = 0;
	struct usb_device *udev = data->udev;

	BT_DBG("%s\n", __func__);

	memmove(data->io_buf, &data->rom_patch_offset, 4);
	memmove(&data->io_buf[4], &checksum_len, 4);

	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0x1,
			DEVICE_VENDOR_REQUEST_IN, 0x20, 0x00, data->io_buf,
			8, CONTROL_TIMEOUT_JIFFIES);

	if (ret < 0)
		BT_ERR("%s error(%d)\n", __func__, ret);

	return ret;
}

static u16 btmtk_usb_get_crc(struct btmtk_usb_data *data)
{
	int ret = 0;
	struct usb_device *udev = data->udev;
	u16 crc, count = 0;
	__le16 crc_le;

	BT_DBG("%s\n", __func__);

	while (1) {
		ret = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0),
					0x01, DEVICE_VENDOR_REQUEST_IN,
					0x21, 0x00, data->io_buf, 2,
					CONTROL_TIMEOUT_JIFFIES);

		if (ret < 0) {
			crc = 0xFFFF;
			BT_ERR("%s error(%d)\n", __func__, ret);
		}

		memmove(&crc_le, data->io_buf, 2);

		crc = le16_to_cpu(crc_le);

		if (crc != 0xFFFF)
			break;

		mdelay(100);

		if (count++ > 100) {
			BT_ERR("Query CRC over %d times\n", count);
			break;
		}
	}

	return crc;
}

static int btmtk_usb_reset_wmt(struct btmtk_usb_data *data)
{
	int ret = 0;

	/* reset command */
	u8 cmd[8] = {0x6F, 0xFC, 0x05, 0x01, 0x07, 0x01, 0x00, 0x04};

	memmove(data->io_buf, cmd, 8);

	BT_DBG("%s\n", __func__);

	ret = usb_control_msg(data->udev, usb_sndctrlpipe(data->udev, 0), 0x01,
				DEVICE_CLASS_REQUEST_OUT, 0x12, 0x00,
				data->io_buf, 8, CONTROL_TIMEOUT_JIFFIES);

	if (ret)
		BT_ERR("%s:(%d)\n", __func__, ret);

	return ret;
}

static void load_rom_patch_complete(struct urb *urb)
{

	struct completion *sent_to_mcu_done = (struct completion *)urb->context;

	complete(sent_to_mcu_done);
}

static int btmtk_usb_load_rom_patch(struct btmtk_usb_data *data)
{
	u32 loop = 0;
	u32 value;
	s32 sent_len;
	int ret = 0, total_checksum = 0;
	struct urb *urb;
	u32 patch_len = 0;
	u32 cur_len = 0;
	dma_addr_t data_dma;
	struct completion sent_to_mcu_done;
	int first_block = 1;
	unsigned char phase;
	void *buf;
	char *pos;
	unsigned int pipe;
	pipe = usb_sndbulkpipe(data->udev, data->bulk_tx_ep->bEndpointAddress);

	if (!data->firmware) {
		BT_ERR("%s:please assign a rom patch\n", __func__);
		return -1;
	}

load_patch_protect:
	btmtk_usb_switch_iobase(data, WLAN);
	btmtk_usb_io_read32(data, SEMAPHORE_03, &value);
	loop++;

	if (((value & 0x01) == 0x00) && (loop < 600)) {
		mdelay(1);
		goto load_patch_protect;
	}

	btmtk_usb_io_write32(data, 0x1004, 0x2c);

	btmtk_usb_switch_iobase(data, SYSCTL);

	btmtk_usb_io_write32(data, 0x1c, 0x30);

	/* Enable USB_DMA_CFG */
	btmtk_usb_io_write32(data, 0x9018, 0x00c00020);

	btmtk_usb_switch_iobase(data, WLAN);

	/* check ROM patch if upgrade */
	btmtk_usb_io_read32(data, COM_REG0, &value);

	if ((value & 0x02) == 0x02)
		goto error0;

	urb = usb_alloc_urb(0, GFP_ATOMIC);

	if (!urb) {
		ret = -ENOMEM;
		goto error0;
	}

	buf = usb_alloc_coherent(data->udev, UPLOAD_PATCH_UNIT,
			GFP_ATOMIC, &data_dma);

	if (!buf) {
		ret = -ENOMEM;
		goto error1;
	}

	pos = buf;
	BT_DBG("loading rom patch");

	init_completion(&sent_to_mcu_done);

	cur_len = 0x00;
	patch_len = data->rom_patch_len - PATCH_INFO_SIZE;

	/* loading rom patch */
	while (1) {
		s32 sent_len_max = UPLOAD_PATCH_UNIT - PATCH_HEADER_SIZE;
		sent_len = min_t(s32, (patch_len - cur_len), sent_len_max);

		BT_DBG("patch_len = %d\n", patch_len);
		BT_DBG("cur_len = %d\n", cur_len);
		BT_DBG("sent_len = %d\n", sent_len);

		if (sent_len <= 0)
			break;

		if (first_block == 1) {
			if (sent_len < sent_len_max)
				phase = PATCH_PHASE3;
			else
				phase = PATCH_PHASE1;
			first_block = 0;
		} else if (sent_len == sent_len_max) {
			phase = PATCH_PHASE2;
		} else {
			phase = PATCH_PHASE3;
		}

		/* prepare HCI header */
		pos[0] = 0x6F;
		pos[1] = 0xFC;
		pos[2] = (sent_len + 5) & 0xFF;
		pos[3] = ((sent_len + 5) >> 8) & 0xFF;

		/* prepare WMT header */
		pos[4] = 0x01;
		pos[5] = 0x01;
		pos[6] = (sent_len + 1) & 0xFF;
		pos[7] = ((sent_len + 1) >> 8) & 0xFF;

		pos[8] = phase;

		memcpy(&pos[9],
			data->firmware->data + PATCH_INFO_SIZE + cur_len,
			sent_len);

		BT_DBG("sent_len + PATCH_HEADER_SIZE = %d, phase = %d\n",
				sent_len + PATCH_HEADER_SIZE, phase);

		usb_fill_bulk_urb(urb,
				data->udev,
				pipe,
				buf,
				sent_len + PATCH_HEADER_SIZE,
				load_rom_patch_complete,
				&sent_to_mcu_done);

		urb->transfer_dma = data_dma;
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

		ret = usb_submit_urb(urb, GFP_ATOMIC);

		if (ret)
			goto error2;

		if (!wait_for_completion_timeout(&sent_to_mcu_done,
					msecs_to_jiffies(1000))) {
			usb_kill_urb(urb);
			BT_ERR("upload rom_patch timeout\n");
			goto error2;
		}

		BT_DBG(".");

		mdelay(200);

		cur_len += sent_len;

	}

	total_checksum = checksume16(
			(u8 *)data->firmware->data + PATCH_INFO_SIZE,
			patch_len);

	BT_DBG("Send checksum req..\n");

	btmtk_usb_chk_crc(data, patch_len);

	mdelay(20);

	if (total_checksum != btmtk_usb_get_crc(data)) {
		BT_ERR("checksum fail!, local(0x%x) <> fw(0x%x)\n",
				total_checksum, btmtk_usb_get_crc(data));
		ret = -1;
		goto error2;
	}

	mdelay(20);

	ret = btmtk_usb_reset_wmt(data);

	mdelay(20);

error2:
	usb_free_coherent(data->udev, UPLOAD_PATCH_UNIT, buf, data_dma);
error1:
	usb_free_urb(urb);
error0:
	btmtk_usb_io_write32(data, SEMAPHORE_03, 0x1);
	return ret;
}


static int load_fw_iv(struct btmtk_usb_data *data)
{
	int ret;
	struct usb_device *udev = data->udev;
	char *buf = kmalloc(64, GFP_ATOMIC);

	memmove(buf, data->firmware->data + 32, 64);

	ret = usb_control_msg(udev, usb_sndctrlpipe(udev, 0), 0x01,
			DEVICE_VENDOR_REQUEST_OUT, 0x12, 0x0, buf, 64,
			CONTROL_TIMEOUT_JIFFIES);

	if (ret < 0) {
		BT_ERR("%s error(%d) step4\n", __func__, ret);
		kfree(buf);
		return ret;
	}

	if (ret > 0)
		ret = 0;

	kfree(buf);

	return ret;
}

static void load_fw_complete(struct urb *urb)
{

	struct completion *sent_to_mcu_done = (struct completion *)urb->context;

	complete(sent_to_mcu_done);
}

static int btmtk_usb_load_fw(struct btmtk_usb_data *data)
{
	struct usb_device *udev = data->udev;
	struct urb *urb;
	void *buf;
	u32 cur_len = 0;
	u32 packet_header = 0;
	__le32 packet_header_le;
	u32 value;
	u32 ilm_len = 0, dlm_len = 0;
	u16 fw_ver, build_ver;
	u32 loop = 0;
	dma_addr_t data_dma;
	int ret = 0, sent_len;
	struct completion sent_to_mcu_done;
	unsigned int pipe;
	pipe = usb_sndbulkpipe(data->udev, data->bulk_tx_ep->bEndpointAddress);

	if (!data->firmware) {
		BT_ERR("%s:please assign a fw\n", __func__);
		return -1;
	}

	BT_DBG("bulk_tx_ep = %x\n", data->bulk_tx_ep->bEndpointAddress);

loadfw_protect:
	btmtk_usb_switch_iobase(data, WLAN);
	btmtk_usb_io_read32(data, SEMAPHORE_00, &value);
	loop++;

	if (((value & 0x1) == 0) && (loop < 10000))
		goto loadfw_protect;

	/* check MCU if ready */
	btmtk_usb_io_read32(data, COM_REG0, &value);

	if ((value & 0x01) == 0x01)
		goto error0;

	/* Enable MPDMA TX and EP2 load FW mode */
	btmtk_usb_io_write32(data, 0x238, 0x1c000000);

	btmtk_usb_reset(udev);
	mdelay(100);

	ilm_len = (*(data->firmware->data + 3) << 24)
			| (*(data->firmware->data + 2) << 16)
			| (*(data->firmware->data + 1) << 8)
			| (*data->firmware->data);

	dlm_len = (*(data->firmware->data + 7) << 24)
			| (*(data->firmware->data + 6) << 16)
			| (*(data->firmware->data + 5) << 8)
			| (*(data->firmware->data + 4));

	fw_ver = (*(data->firmware->data + 11) << 8) |
	      (*(data->firmware->data + 10));

	build_ver = (*(data->firmware->data + 9) << 8) |
		 (*(data->firmware->data + 8));

	BT_DBG("fw version:%d.%d.%02d ",
			(fw_ver & 0xf000) >> 8,
			(fw_ver & 0x0f00) >> 8,
			(fw_ver & 0x00ff));

	BT_DBG("build:%x\n", build_ver);

	BT_DBG("build Time =");

	for (loop = 0; loop < 16; loop++)
		BT_DBG("%c", *(data->firmware->data + 16 + loop));

	BT_DBG("\n");

	BT_DBG("ILM length = %d(bytes)\n", ilm_len);
	BT_DBG("DLM length = %d(bytes)\n", dlm_len);

	btmtk_usb_switch_iobase(data, SYSCTL);

	/* U2M_PDMA rx_ring_base_ptr */
	btmtk_usb_io_write32(data, 0x790, 0x400230);

	/* U2M_PDMA rx_ring_max_cnt */
	btmtk_usb_io_write32(data, 0x794, 0x1);

	/* U2M_PDMA cpu_idx */
	btmtk_usb_io_write32(data, 0x798, 0x1);

	/* U2M_PDMA enable */
	btmtk_usb_io_write32(data, 0x704, 0x44);

	urb = usb_alloc_urb(0, GFP_ATOMIC);

	if (!urb) {
		ret = -ENOMEM;
		goto error1;
	}

	buf = usb_alloc_coherent(udev, 14592, GFP_ATOMIC, &data_dma);

	if (!buf) {
		ret = -ENOMEM;
		goto error2;
	}

	BT_DBG("loading fw");

	init_completion(&sent_to_mcu_done);

	btmtk_usb_switch_iobase(data, SYSCTL);

	cur_len = 0x40;

	/* Loading ILM */
	while (1) {
		sent_len = min_t(s32, (ilm_len - cur_len), 14336);

		if (sent_len > 0) {
			packet_header &= ~(0xffffffff);
			packet_header |= (sent_len << 16);
			packet_header_le = cpu_to_le32(packet_header);

			memmove(buf, &packet_header_le, 4);
			memmove(buf + 4, data->firmware->data + 32 + cur_len,
					sent_len);

			/* U2M_PDMA descriptor */
			btmtk_usb_io_write32(data, 0x230, cur_len);

			while ((sent_len % 4) != 0)
				sent_len++;

			/* U2M_PDMA length */
			btmtk_usb_io_write32(data, 0x234, sent_len << 16);

			usb_fill_bulk_urb(urb,
					udev,
					pipe,
					buf,
					sent_len + 4,
					load_fw_complete,
					&sent_to_mcu_done);

			urb->transfer_dma = data_dma;
			urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

			ret = usb_submit_urb(urb, GFP_ATOMIC);

			if (ret)
				goto error3;

			if (!wait_for_completion_timeout(&sent_to_mcu_done,
						msecs_to_jiffies(1000))) {
				usb_kill_urb(urb);
				BT_ERR("upload ilm fw timeout\n");
				goto error3;
			}

			BT_DBG(".");

			mdelay(200);

			cur_len += sent_len;
		} else {
			break;
		}
	}

	init_completion(&sent_to_mcu_done);
	cur_len = 0x00;

	/* Loading DLM */
	while (1) {
		sent_len = min_t(s32, (dlm_len - cur_len), 14336);

		if (sent_len <= 0)
			break;

		packet_header &= ~(0xffffffff);
		packet_header |= (sent_len << 16);
		packet_header_le = cpu_to_le32(packet_header);

		memmove(buf, &packet_header_le, 4);
		memmove(buf + 4,
			data->firmware->data + 32 + ilm_len + cur_len,
			sent_len);

		/* U2M_PDMA descriptor */
		btmtk_usb_io_write32(data, 0x230, 0x80000 + cur_len);

		while ((sent_len % 4) != 0) {
			BT_DBG("sent_len is not divided by 4\n");
			sent_len++;
		}

		/* U2M_PDMA length */
		btmtk_usb_io_write32(data, 0x234, sent_len << 16);

		usb_fill_bulk_urb(urb,
				udev,
				pipe,
				buf,
				sent_len + 4,
				load_fw_complete,
				&sent_to_mcu_done);

		urb->transfer_dma = data_dma;
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

		ret = usb_submit_urb(urb, GFP_ATOMIC);

		if (ret)
			goto error3;

		if (!wait_for_completion_timeout(&sent_to_mcu_done,
					msecs_to_jiffies(1000))) {
			usb_kill_urb(urb);
			BT_ERR("upload dlm fw timeout\n");
			goto error3;
		}

		BT_DBG(".");

		mdelay(500);

		cur_len += sent_len;

	}

	/* upload 64bytes interrupt vector */
	ret = load_fw_iv(data);
	mdelay(100);

	btmtk_usb_switch_iobase(data, WLAN);

	/* check MCU if ready */
	loop = 0;

	do {
		btmtk_usb_io_read32(data, COM_REG0, &value);

		if (value == 0x01)
			break;

		mdelay(10);
		loop++;
	} while (loop <= 100);

	if (loop > 1000) {
		BT_ERR("wait for 100 times\n");
		ret = -ENODEV;
	}

error3:
	usb_free_coherent(udev, 14592, buf, data_dma);
error2:
	usb_free_urb(urb);
error1:
	/* Disbale load fw mode */
	btmtk_usb_io_read32(data, 0x238, &value);
	value = value & ~(0x10000000);
	btmtk_usb_io_write32(data,  0x238, value);
error0:
	btmtk_usb_io_write32(data, SEMAPHORE_00, 0x1);
	return ret;
}

static int inc_tx(struct btmtk_usb_data *data)
{
	unsigned long flags;
	int rv;

	spin_lock_irqsave(&data->txlock, flags);
	rv = test_bit(BTUSB_SUSPENDING, &data->flags);
	if (!rv)
		data->tx_in_flight++;
	spin_unlock_irqrestore(&data->txlock, flags);

	return rv;
}

static void btmtk_usb_intr_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btmtk_usb_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s: %s urb %p status %d count %d\n", __func__, hdev->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (urb->status == 0) {
		hdev->stat.byte_rx += urb->actual_length;

		hex_dump("hci event", urb->transfer_buffer, urb->actual_length);

		if (hci_recv_fragment(hdev, HCI_EVENT_PKT,
						urb->transfer_buffer,
						urb->actual_length) < 0) {
			BT_ERR("%s corrupted event packet", hdev->name);
			hdev->stat.err_rx++;
		}
	}

	if (!test_bit(BTUSB_INTR_RUNNING, &data->flags))
		return;

	usb_mark_last_busy(data->udev);
	usb_anchor_urb(urb, &data->intr_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);

	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected */
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p failed to resubmit (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

static int btmtk_usb_submit_intr_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btmtk_usb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;

	BT_DBG("%s\n", __func__);

	if (!data->intr_ep)
		return -ENODEV;

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;

	size = le16_to_cpu(data->intr_ep->wMaxPacketSize);

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvintpipe(data->udev, data->intr_ep->bEndpointAddress);

	usb_fill_int_urb(urb, data->udev, pipe, buf, size,
						btmtk_usb_intr_complete, hdev,
						data->intr_ep->bInterval);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_anchor_urb(urb, &data->intr_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;

}

static void btmtk_usb_bulk_in_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btmtk_usb_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s:%s urb %p status %d count %d", __func__, hdev->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (urb->status == 0) {
		hdev->stat.byte_rx += urb->actual_length;

		if (hci_recv_fragment(hdev, HCI_ACLDATA_PKT,
						urb->transfer_buffer,
						urb->actual_length) < 0) {
			BT_ERR("%s corrupted ACL packet", hdev->name);
			hdev->stat.err_rx++;
		}
	}

	if (!test_bit(BTUSB_BULK_RUNNING, &data->flags))
		return;

	usb_anchor_urb(urb, &data->bulk_anchor);
	usb_mark_last_busy(data->udev);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected */
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p failed to resubmit (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

static int btmtk_usb_submit_bulk_in_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btmtk_usb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size = HCI_MAX_FRAME_SIZE;

	BT_DBG("%s:%s\n", __func__, hdev->name);

	if (!data->bulk_rx_ep)
		return -ENODEV;

	urb = usb_alloc_urb(0, mem_flags);
	if (!urb)
		return -ENOMEM;

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvbulkpipe(data->udev, data->bulk_rx_ep->bEndpointAddress);

	usb_fill_bulk_urb(urb, data->udev, pipe, buf, size,
			btmtk_usb_bulk_in_complete, hdev);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_mark_last_busy(data->udev);
	usb_anchor_urb(urb, &data->bulk_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btmtk_usb_isoc_in_complete(struct urb *urb)

{
	struct hci_dev *hdev = urb->context;
	struct btmtk_usb_data *data = hci_get_drvdata(hdev);
	int i, err;

	BT_DBG("%s: %s urb %p status %d count %d", __func__, hdev->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (urb->status == 0) {
		for (i = 0; i < urb->number_of_packets; i++) {
			unsigned int offset = urb->iso_frame_desc[i].offset;
			unsigned int length;
			length = urb->iso_frame_desc[i].actual_length;

			if (urb->iso_frame_desc[i].status)
				continue;

			hdev->stat.byte_rx += length;

			if (hci_recv_fragment(hdev, HCI_SCODATA_PKT,
						urb->transfer_buffer + offset,
								length) < 0) {
				BT_ERR("%s corrupted SCO packet", hdev->name);
				hdev->stat.err_rx++;
			}
		}
	}

	if (!test_bit(BTUSB_ISOC_RUNNING, &data->flags))
		return;

	usb_anchor_urb(urb, &data->isoc_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		/* -EPERM: urb is being killed;
		 * -ENODEV: device got disconnected */
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p failed to resubmit (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

static inline void __fill_isoc_descriptor(struct urb *urb, int len, int mtu)
{
	int i, offset = 0;

	BT_DBG("len %d mtu %d", len, mtu);

	for (i = 0; i < BTUSB_MAX_ISOC_FRAMES && len >= mtu;
					i++, offset += mtu, len -= mtu) {
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = mtu;
	}

	if (len && i < BTUSB_MAX_ISOC_FRAMES) {
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = len;
		i++;
	}

	urb->number_of_packets = i;
}

static int btmtk_usb_submit_isoc_in_urb(struct hci_dev *hdev, gfp_t mem_flags)
{
	struct btmtk_usb_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;

	BT_DBG("%s\n", __func__);

	if (!data->isoc_rx_ep)
		return -ENODEV;

	urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, mem_flags);
	if (!urb)
		return -ENOMEM;

	size = le16_to_cpu(data->isoc_rx_ep->wMaxPacketSize) *
						BTUSB_MAX_ISOC_FRAMES;

	buf = kmalloc(size, mem_flags);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvisocpipe(data->udev, data->isoc_rx_ep->bEndpointAddress);

	usb_fill_int_urb(urb, data->udev, pipe, buf, size,
			btmtk_usb_isoc_in_complete, hdev,
			data->isoc_rx_ep->bInterval);

	urb->transfer_flags  = URB_FREE_BUFFER | URB_ISO_ASAP;

	__fill_isoc_descriptor(urb, size,
			le16_to_cpu(data->isoc_rx_ep->wMaxPacketSize));

	usb_anchor_urb(urb, &data->isoc_anchor);

	err = usb_submit_urb(urb, mem_flags);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static int btmtk_usb_open(struct hci_dev *hdev)
{
	struct btmtk_usb_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s\n", __func__);

	err = usb_autopm_get_interface(data->intf);
	if (err < 0)
		return err;

	data->intf->needs_remote_wakeup = 1;

	if (test_and_set_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (test_and_set_bit(BTUSB_INTR_RUNNING, &data->flags))
		goto done;

	err = btmtk_usb_submit_intr_urb(hdev, GFP_KERNEL);
	if (err < 0)
		goto failed;

	err = btmtk_usb_submit_bulk_in_urb(hdev, GFP_KERNEL);
	if (err < 0) {
		usb_kill_anchored_urbs(&data->intr_anchor);
		goto failed;
	}

	set_bit(BTUSB_BULK_RUNNING, &data->flags);
	btmtk_usb_submit_bulk_in_urb(hdev, GFP_KERNEL);

done:
	usb_autopm_put_interface(data->intf);
	return 0;

failed:
	clear_bit(BTUSB_INTR_RUNNING, &data->flags);
	clear_bit(HCI_RUNNING, &hdev->flags);
	usb_autopm_put_interface(data->intf);
	return err;
}

static void btmtk_usb_stop_traffic(struct btmtk_usb_data *data)
{
	BT_DBG("%s\n", __func__);

	usb_kill_anchored_urbs(&data->intr_anchor);
	usb_kill_anchored_urbs(&data->bulk_anchor);
	usb_kill_anchored_urbs(&data->isoc_anchor);
}

static int btmtk_usb_close(struct hci_dev *hdev)
{
	struct btmtk_usb_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s\n", __func__);

	if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	cancel_work_sync(&data->work);
	cancel_work_sync(&data->waker);

	clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
	clear_bit(BTUSB_BULK_RUNNING, &data->flags);
	clear_bit(BTUSB_INTR_RUNNING, &data->flags);

	btmtk_usb_stop_traffic(data);

	err = usb_autopm_get_interface(data->intf);
	if (err < 0)
		goto failed;

	data->intf->needs_remote_wakeup = 0;
	usb_autopm_put_interface(data->intf);

failed:
	usb_scuttle_anchored_urbs(&data->deferred);
	return 0;
}

static int btmtk_usb_flush(struct hci_dev *hdev)
{
	struct btmtk_usb_data *data = hci_get_drvdata(hdev);

	BT_DBG("%s\n", __func__);

	usb_kill_anchored_urbs(&data->tx_anchor);

	return 0;
}

static void btmtk_usb_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct hci_dev *hdev = (struct hci_dev *)skb->dev;
	struct btmtk_usb_data *data = hci_get_drvdata(hdev);

	BT_DBG("%s: %s urb %p status %d count %d\n", __func__, hdev->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (!urb->status)
		hdev->stat.byte_tx += urb->transfer_buffer_length;
	else
		hdev->stat.err_tx++;

done:
	spin_lock(&data->txlock);
	data->tx_in_flight--;
	spin_unlock(&data->txlock);

	kfree(urb->setup_packet);

	kfree_skb(skb);
}

static void btmtk_usb_isoc_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;

	BT_DBG("%s: %s urb %p status %d count %d", __func__, hdev->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (!urb->status)
		hdev->stat.byte_tx += urb->transfer_buffer_length;
	else
		hdev->stat.err_tx++;

done:
	kfree(urb->setup_packet);

	kfree_skb(skb);
}

static int btmtk_usb_send_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *)skb->dev;
	struct btmtk_usb_data *data = hci_get_drvdata(hdev);
	struct usb_ctrlrequest *dr;
	struct urb *urb;
	unsigned int pipe;
	int err;

	BT_DBG("%s\n", __func__);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return -EBUSY;

	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (!urb)
			return -ENOMEM;

		dr = kmalloc(sizeof(*dr), GFP_ATOMIC);
		if (!dr) {
			usb_free_urb(urb);
			return -ENOMEM;
		}

		dr->bRequestType = data->cmdreq_type;
		dr->bRequest     = 0;
		dr->wIndex       = 0;
		dr->wValue       = 0;
		dr->wLength      = __cpu_to_le16(skb->len);

		pipe = usb_sndctrlpipe(data->udev, 0x00);

		if (test_bit(HCI_RUNNING, &hdev->flags)) {
			u16 op_code;
			memcpy(&op_code, skb->data, 2);
			BT_DBG("ogf = %x\n", (op_code & 0xfc00) >> 10);
			BT_DBG("ocf = %x\n", op_code & 0x03ff);
			hex_dump("hci command", skb->data, skb->len);

		}

		usb_fill_control_urb(urb, data->udev, pipe, (void *) dr,
				skb->data, skb->len,
				btmtk_usb_tx_complete, skb);

		hdev->stat.cmd_tx++;
		break;

	case HCI_ACLDATA_PKT:
		if (!data->bulk_tx_ep)
			return -ENODEV;

		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (!urb)
			return -ENOMEM;

		pipe = usb_sndbulkpipe(data->udev,
					data->bulk_tx_ep->bEndpointAddress);

		usb_fill_bulk_urb(urb, data->udev, pipe, skb->data,
				skb->len, btmtk_usb_tx_complete, skb);

		hdev->stat.acl_tx++;
		BT_DBG("HCI_ACLDATA_PKT:\n");
		break;

	case HCI_SCODATA_PKT:
		if (!data->isoc_tx_ep || hdev->conn_hash.sco_num < 1)
			return -ENODEV;

		urb = usb_alloc_urb(BTUSB_MAX_ISOC_FRAMES, GFP_ATOMIC);
		if (!urb)
			return -ENOMEM;

		pipe = usb_sndisocpipe(data->udev,
					data->isoc_tx_ep->bEndpointAddress);

		usb_fill_int_urb(urb, data->udev, pipe,
				skb->data, skb->len, btmtk_usb_isoc_tx_complete,
				skb, data->isoc_tx_ep->bInterval);

		urb->transfer_flags  = URB_ISO_ASAP;

		__fill_isoc_descriptor(urb, skb->len,
				le16_to_cpu(data->isoc_tx_ep->wMaxPacketSize));

		hdev->stat.sco_tx++;
		BT_DBG("HCI_SCODATA_PKT:\n");
		goto skip_waking;

	default:
		return -EILSEQ;
	}

	err = inc_tx(data);

	if (err) {
		usb_anchor_urb(urb, &data->deferred);
		schedule_work(&data->waker);
		err = 0;
		goto done;
	}

skip_waking:
	usb_anchor_urb(urb, &data->tx_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		if (err != -EPERM && err != -ENODEV)
			BT_ERR("%s urb %p submission failed (%d)",
						hdev->name, urb, -err);
		kfree(urb->setup_packet);
		usb_unanchor_urb(urb);
	} else {
		usb_mark_last_busy(data->udev);
	}

done:
	usb_free_urb(urb);
	return err;
}

static void btmtk_usb_notify(struct hci_dev *hdev, unsigned int evt)
{
	struct btmtk_usb_data *data = hci_get_drvdata(hdev);

	BT_DBG("%s evt %d", hdev->name, evt);

	if (hdev->conn_hash.sco_num != data->sco_num) {
		data->sco_num = hdev->conn_hash.sco_num;
		schedule_work(&data->work);
	}
}

static inline int __set_isoc_interface(struct hci_dev *hdev, int altsetting)
{
	struct btmtk_usb_data *data = hci_get_drvdata(hdev);
	struct usb_interface *intf = data->isoc;
	struct usb_endpoint_descriptor *ep_desc;
	int i, err;

	if (!data->isoc)
		return -ENODEV;

	err = usb_set_interface(data->udev, 1, altsetting);
	if (err < 0) {
		BT_ERR("%s setting interface failed (%d)", hdev->name, -err);
		return err;
	}

	data->isoc_altsetting = altsetting;

	data->isoc_tx_ep = NULL;
	data->isoc_rx_ep = NULL;

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep_desc = &intf->cur_altsetting->endpoint[i].desc;

		if (!data->isoc_tx_ep && usb_endpoint_is_isoc_out(ep_desc)) {
			data->isoc_tx_ep = ep_desc;
			continue;
		}

		if (!data->isoc_rx_ep && usb_endpoint_is_isoc_in(ep_desc)) {
			data->isoc_rx_ep = ep_desc;
			continue;
		}
	}

	if (!data->isoc_tx_ep || !data->isoc_rx_ep) {
		BT_ERR("%s invalid SCO descriptors", hdev->name);
		return -ENODEV;
	}

	return 0;
}

static void btmtk_usb_work(struct work_struct *work)
{
	struct btmtk_usb_data *data = container_of(work, struct btmtk_usb_data,
			work);
	struct hci_dev *hdev = data->hdev;
	int new_alts;
	int err;

	BT_DBG("%s\n", __func__);

	if (hdev->conn_hash.sco_num > 0) {
		if (!test_bit(BTUSB_DID_ISO_RESUME, &data->flags)) {
			err = usb_autopm_get_interface(data->isoc ?
					data->isoc : data->intf);
			if (err < 0) {
				clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
				usb_kill_anchored_urbs(&data->isoc_anchor);
				return;
			}

			set_bit(BTUSB_DID_ISO_RESUME, &data->flags);
		}

		if (hdev->voice_setting & 0x0020) {
			static const int alts[3] = { 2, 4, 5 };
			new_alts = alts[hdev->conn_hash.sco_num - 1];
		} else {
			new_alts = hdev->conn_hash.sco_num;
		}

		if (data->isoc_altsetting != new_alts) {
			clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
			usb_kill_anchored_urbs(&data->isoc_anchor);

			if (__set_isoc_interface(hdev, new_alts) < 0)
				return;
		}

		if (!test_and_set_bit(BTUSB_ISOC_RUNNING, &data->flags)) {
			if (btmtk_usb_submit_isoc_in_urb(hdev, GFP_KERNEL) < 0)
				clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
			else
				btmtk_usb_submit_isoc_in_urb(hdev, GFP_KERNEL);
		}
	} else {
		clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
		usb_kill_anchored_urbs(&data->isoc_anchor);

		__set_isoc_interface(hdev, 0);

		if (test_and_clear_bit(BTUSB_DID_ISO_RESUME, &data->flags))
			usb_autopm_put_interface(data->isoc ?
					 data->isoc : data->intf);
	}
}

static void btmtk_usb_waker(struct work_struct *work)
{
	struct btmtk_usb_data *data = container_of(work, struct btmtk_usb_data,
			waker);
	int err;

	err = usb_autopm_get_interface(data->intf);

	if (err < 0)
		return;

	usb_autopm_put_interface(data->intf);
}

static int btmtk_usb_probe(struct usb_interface *intf,
					const struct usb_device_id *id)
{
	struct btmtk_usb_data *data;
	struct usb_endpoint_descriptor *ep_desc;
	int i, err;
	struct hci_dev *hdev;

	/* interface numbers are hardcoded in the spec */
	if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
		return -ENODEV;

	data = kzalloc(sizeof(*data), GFP_KERNEL);

	if (!data)
		return -ENOMEM;

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep_desc = &intf->cur_altsetting->endpoint[i].desc;

		if (!data->intr_ep && usb_endpoint_is_int_in(ep_desc)) {
			data->intr_ep = ep_desc;
			continue;
		}

		if (!data->bulk_tx_ep && usb_endpoint_is_bulk_out(ep_desc)) {
			data->bulk_tx_ep = ep_desc;
			continue;
		}

		if (!data->bulk_rx_ep && usb_endpoint_is_bulk_in(ep_desc)) {
			data->bulk_rx_ep = ep_desc;
			continue;
		}
	}

	if (!data->intr_ep || !data->bulk_tx_ep || !data->bulk_rx_ep) {
		kfree(data);
		return -ENODEV;
	}

	data->cmdreq_type = USB_TYPE_CLASS;

	data->udev = interface_to_usbdev(intf);
	data->intf = intf;

	spin_lock_init(&data->lock);
	INIT_WORK(&data->work, btmtk_usb_work);
	INIT_WORK(&data->waker, btmtk_usb_waker);
	spin_lock_init(&data->txlock);

	init_usb_anchor(&data->tx_anchor);
	init_usb_anchor(&data->intr_anchor);
	init_usb_anchor(&data->bulk_anchor);
	init_usb_anchor(&data->isoc_anchor);
	init_usb_anchor(&data->deferred);

	hdev = hci_alloc_dev();
	if (!hdev) {
		kfree(data);
		return -ENOMEM;
	}

	hdev->bus = HCI_USB;

	hci_set_drvdata(hdev, data);

	data->hdev = hdev;

	SET_HCIDEV_DEV(hdev, &intf->dev);

	hdev->open     = btmtk_usb_open;
	hdev->close    = btmtk_usb_close;
	hdev->flush    = btmtk_usb_flush;
	hdev->send     = btmtk_usb_send_frame;
	hdev->notify   = btmtk_usb_notify;

	/* Interface numbers are hardcoded in the specification */
	data->isoc = usb_ifnum_to_if(data->udev, 1);

	if (data->isoc) {
		err = usb_driver_claim_interface(&btmtk_usb_driver,
							data->isoc, data);
		if (err < 0) {
			hci_free_dev(hdev);
			kfree(data);
			return err;
		}
	}

	data->io_buf = kmalloc(256, GFP_KERNEL);
	if (!data->io_buf) {
		hci_free_dev(hdev);
		kfree(data);
		return -ENOMEM;
	}

	btmtk_usb_switch_iobase(data, WLAN);

	btmtk_usb_cap_init(data);

	err = hci_register_dev(hdev);
	if (err < 0) {
		hci_free_dev(hdev);
		kfree(data);
		return err;
	}

	usb_set_intfdata(intf, data);

	return 0;
}

static void btmtk_usb_disconnect(struct usb_interface *intf)
{
	struct btmtk_usb_data *data = usb_get_intfdata(intf);
	struct hci_dev *hdev;

	BT_DBG("%s\n", __func__);

	if (!data)
		return;

	hdev = data->hdev;
	usb_set_intfdata(data->intf, NULL);

	if (data->isoc)
		usb_set_intfdata(data->isoc, NULL);

	hci_unregister_dev(hdev);

	if (intf == data->isoc)
		usb_driver_release_interface(&btmtk_usb_driver, data->intf);
	else if (data->isoc)
		usb_driver_release_interface(&btmtk_usb_driver, data->isoc);

	hci_free_dev(hdev);

	kfree(data->io_buf);

	kfree(data);
}

#ifdef CONFIG_PM
static int btmtk_usb_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct btmtk_usb_data *data = usb_get_intfdata(intf);

	BT_DBG("%s\n", __func__);

	if (data->suspend_count++)
		return 0;

	spin_lock_irq(&data->txlock);
	if (!(PMSG_IS_AUTO(message) && data->tx_in_flight)) {
		set_bit(BTUSB_SUSPENDING, &data->flags);
		spin_unlock_irq(&data->txlock);
	} else {
		spin_unlock_irq(&data->txlock);
		data->suspend_count--;
		return -EBUSY;
	}

	cancel_work_sync(&data->work);

	btmtk_usb_stop_traffic(data);
	usb_kill_anchored_urbs(&data->tx_anchor);

	return 0;
}

static void play_deferred(struct btmtk_usb_data *data)
{
	struct urb *urb;
	int err;

	while ((urb = usb_get_from_anchor(&data->deferred))) {
		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err < 0)
			break;

		data->tx_in_flight++;
	}

	usb_scuttle_anchored_urbs(&data->deferred);
}

static int btmtk_usb_resume(struct usb_interface *intf)
{
	struct btmtk_usb_data *data = usb_get_intfdata(intf);
	struct hci_dev *hdev = data->hdev;
	int err = 0;

	BT_DBG("%s\n", __func__);

	if (--data->suspend_count)
		return 0;

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (test_bit(BTUSB_INTR_RUNNING, &data->flags)) {
		err = btmtk_usb_submit_intr_urb(hdev, GFP_NOIO);
		if (err < 0) {
			clear_bit(BTUSB_INTR_RUNNING, &data->flags);
			goto failed;
		}
	}

	if (test_bit(BTUSB_BULK_RUNNING, &data->flags)) {
		err = btmtk_usb_submit_bulk_in_urb(hdev, GFP_NOIO);
		if (err < 0) {
			clear_bit(BTUSB_BULK_RUNNING, &data->flags);
			goto failed;
		}

		btmtk_usb_submit_bulk_in_urb(hdev, GFP_NOIO);
	}

	if (test_bit(BTUSB_ISOC_RUNNING, &data->flags)) {
		if (btmtk_usb_submit_isoc_in_urb(hdev, GFP_NOIO) < 0)
			clear_bit(BTUSB_ISOC_RUNNING, &data->flags);
		else
			btmtk_usb_submit_isoc_in_urb(hdev, GFP_NOIO);
	}

	spin_lock_irq(&data->txlock);
	play_deferred(data);
	clear_bit(BTUSB_SUSPENDING, &data->flags);
	spin_unlock_irq(&data->txlock);
	schedule_work(&data->work);

	return 0;

failed:
	usb_scuttle_anchored_urbs(&data->deferred);
done:
	spin_lock_irq(&data->txlock);
	clear_bit(BTUSB_SUSPENDING, &data->flags);
	spin_unlock_irq(&data->txlock);

	return err;
}
#endif

static struct usb_device_id btmtk_usb_table[] = {
	/* Mediatek MT7650 */
	{ USB_DEVICE(0x0e8d, 0x7650) },
	{ USB_DEVICE(0x0e8d, 0x7630) },
	{ USB_DEVICE(0x0e8d, 0x763e) },
	/* Mediatek MT662 */
	{ USB_DEVICE(0x0e8d, 0x7662) },
	{ USB_DEVICE(0x0e8d, 0x7632) },
	{ }	/* Terminating entry */
};

static struct usb_driver btmtk_usb_driver = {
	.name		= "btmtk_usb",
	.probe		= btmtk_usb_probe,
	.disconnect	= btmtk_usb_disconnect,
#ifdef CONFIG_PM
	.suspend	= btmtk_usb_suspend,
	.resume		= btmtk_usb_resume,
#endif
	.id_table	= btmtk_usb_table,
	.supports_autosuspend = 1,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(btmtk_usb_driver);

MODULE_DESCRIPTION("Mediatek Bluetooth USB driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(MT7650_FIRMWARE);
MODULE_FIRMWARE(MT7662_FIRMWARE);
