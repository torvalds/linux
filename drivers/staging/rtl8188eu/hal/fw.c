/******************************************************************************
 *
 * Copyright(c) 2009-2013  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "fw.h"
#include "drv_types.h"
#include "usb_ops_linux.h"
#include "rtl8188e_spec.h"
#include "rtl8188e_hal.h"

#include <linux/firmware.h>
#include <linux/kmemleak.h>

static void _rtl88e_enable_fw_download(struct adapter *adapt, bool enable)
{
	u8 tmp;

	if (enable) {
		tmp = usb_read8(adapt, REG_MCUFWDL);
		usb_write8(adapt, REG_MCUFWDL, tmp | 0x01);

		tmp = usb_read8(adapt, REG_MCUFWDL + 2);
		usb_write8(adapt, REG_MCUFWDL + 2, tmp & 0xf7);
	} else {
		tmp = usb_read8(adapt, REG_MCUFWDL);
		usb_write8(adapt, REG_MCUFWDL, tmp & 0xfe);

		usb_write8(adapt, REG_MCUFWDL + 1, 0x00);
	}
}

static void _rtl88e_fw_block_write(struct adapter *adapt,
				   const u8 *buffer, u32 size)
{
	u32 blk_sz = sizeof(u32);
	const u8 *byte_buffer;
	const u32 *dword_buffer = (u32 *)buffer;
	u32 i, write_address, blk_cnt, remain;

	blk_cnt = size / blk_sz;
	remain = size % blk_sz;

	write_address = FW_8192C_START_ADDRESS;

	for (i = 0; i < blk_cnt; i++, write_address += blk_sz)
		usb_write32(adapt, write_address, dword_buffer[i]);

	byte_buffer = buffer + blk_cnt * blk_sz;
	for (i = 0; i < remain; i++, write_address++)
		usb_write8(adapt, write_address, byte_buffer[i]);
}

static void _rtl88e_fw_page_write(struct adapter *adapt,
				  u32 page, const u8 *buffer, u32 size)
{
	u8 value8;
	u8 u8page = (u8)(page & 0x07);

	value8 = (usb_read8(adapt, REG_MCUFWDL + 2) & 0xF8) | u8page;

	usb_write8(adapt, (REG_MCUFWDL + 2), value8);
	_rtl88e_fw_block_write(adapt, buffer, size);
}

static void _rtl88e_write_fw(struct adapter *adapt, u8 *buffer, u32 size)
{
	u8 *buf_ptr = buffer;
	u32 page_no, remain;
	u32 page, offset;

	page_no = size / FW_8192C_PAGE_SIZE;
	remain = size % FW_8192C_PAGE_SIZE;

	for (page = 0; page < page_no; page++) {
		offset = page * FW_8192C_PAGE_SIZE;
		_rtl88e_fw_page_write(adapt, page, (buf_ptr + offset),
				      FW_8192C_PAGE_SIZE);
	}

	if (remain) {
		offset = page_no * FW_8192C_PAGE_SIZE;
		page = page_no;
		_rtl88e_fw_page_write(adapt, page, (buf_ptr + offset), remain);
	}
}

static void rtl88e_firmware_selfreset(struct adapter *adapt)
{
	u8 u1b_tmp;

	u1b_tmp = usb_read8(adapt, REG_SYS_FUNC_EN+1);
	usb_write8(adapt, REG_SYS_FUNC_EN+1, (u1b_tmp & (~BIT(2))));
	usb_write8(adapt, REG_SYS_FUNC_EN+1, (u1b_tmp | BIT(2)));
}

static int _rtl88e_fw_free_to_go(struct adapter *adapt)
{
	int err = -EIO;
	u32 counter = 0;
	u32 value32;

	do {
		value32 = usb_read32(adapt, REG_MCUFWDL);
		if (value32 & FWDL_ChkSum_rpt)
			break;
	} while (counter++ < POLLING_READY_TIMEOUT_COUNT);

	if (counter >= POLLING_READY_TIMEOUT_COUNT)
		goto exit;

	value32 = usb_read32(adapt, REG_MCUFWDL);
	value32 |= MCUFWDL_RDY;
	value32 &= ~WINTINI_RDY;
	usb_write32(adapt, REG_MCUFWDL, value32);

	rtl88e_firmware_selfreset(adapt);
	counter = 0;

	do {
		value32 = usb_read32(adapt, REG_MCUFWDL);
		if (value32 & WINTINI_RDY) {
			err = 0;
			goto exit;
		}

		udelay(FW_8192C_POLLING_DELAY);

	} while (counter++ < POLLING_READY_TIMEOUT_COUNT);

exit:
	return err;
}

int rtl88eu_download_fw(struct adapter *adapt)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapt);
	struct device *device = dvobj_to_dev(dvobj);
	const struct firmware *fw;
	const char fw_name[] = "rtlwifi/rtl8188eufw.bin";
	struct rtl92c_firmware_header *pfwheader = NULL;
	u8 *download_data, *fw_data;
	size_t download_size;
	unsigned int trailing_zeros_length;

	if (request_firmware(&fw, fw_name, device)) {
		dev_err(device, "Firmware %s not available\n", fw_name);
		return -ENOENT;
	}

	if (fw->size > FW_8188E_SIZE) {
		dev_err(device, "Firmware size exceed 0x%X. Check it.\n",
			FW_8188E_SIZE);
		release_firmware(fw);
		return -1;
	}

	trailing_zeros_length = (4 - fw->size % 4) % 4;

	fw_data = kmalloc(fw->size + trailing_zeros_length, GFP_KERNEL);
	if (!fw_data) {
		release_firmware(fw);
		return -ENOMEM;
	}

	memcpy(fw_data, fw->data, fw->size);
	memset(fw_data + fw->size, 0, trailing_zeros_length);

	pfwheader = (struct rtl92c_firmware_header *)fw_data;

	if (IS_FW_HEADER_EXIST(pfwheader)) {
		download_data = fw_data + 32;
		download_size = fw->size + trailing_zeros_length - 32;
	} else {
		download_data = fw_data;
		download_size = fw->size + trailing_zeros_length;
	}

	release_firmware(fw);

	if (usb_read8(adapt, REG_MCUFWDL) & RAM_DL_SEL) {
		usb_write8(adapt, REG_MCUFWDL, 0);
		rtl88e_firmware_selfreset(adapt);
	}
	_rtl88e_enable_fw_download(adapt, true);
	usb_write8(adapt, REG_MCUFWDL, usb_read8(adapt, REG_MCUFWDL) | FWDL_ChkSum_rpt);
	_rtl88e_write_fw(adapt, download_data, download_size);
	_rtl88e_enable_fw_download(adapt, false);

	kfree(fw_data);
	return _rtl88e_fw_free_to_go(adapt);
}
