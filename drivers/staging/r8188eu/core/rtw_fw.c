// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#include <linux/firmware.h>
#include "../include/rtw_fw.h"

#define MAX_REG_BLOCK_SIZE	196
#define FW_8188E_START_ADDRESS	0x1000
#define MAX_PAGE_SIZE		4096

#define IS_FW_HEADER_EXIST(_fwhdr)				\
	((le16_to_cpu(_fwhdr->signature) & 0xFFF0) == 0x92C0 ||	\
	(le16_to_cpu(_fwhdr->signature) & 0xFFF0) == 0x88C0 ||	\
	(le16_to_cpu(_fwhdr->signature) & 0xFFF0) == 0x2300 ||	\
	(le16_to_cpu(_fwhdr->signature) & 0xFFF0) == 0x88E0)

struct rt_firmware_hdr {
	__le16	signature;	/* 92C0: test chip; 92C,
				 * 88C0: test chip; 88C1: MP A-cut;
				 * 92C1: MP A-cut */
	u8	category;	/* AP/NIC and USB/PCI */
	u8	function;	/* Reserved for different FW function
				 * indcation, for further use when
				 * driver needs to download different
				 * FW for different conditions */
	__le16	version;	/* FW Version */
	u8	subversion;	/* FW Subversion, default 0x00 */
	u8	rsvd1;
	u8	month;		/* Release time Month field */
	u8	date;		/* Release time Date field */
	u8	hour;		/* Release time Hour field */
	u8	minute;		/* Release time Minute field */
	__le16	ramcodesize;	/* The size of RAM code */
	u8	foundry;
	u8	rsvd2;
	__le32	svnidx;		/* The SVN entry index */
	__le32	rsvd3;
	__le32	rsvd4;
	__le32	rsvd5;
};

static_assert(sizeof(struct rt_firmware_hdr) == 32);

static void fw_download_enable(struct adapter *padapter, bool enable)
{
	u8 tmp;
	int res;

	if (enable) {
		/*  MCU firmware download enable. */
		res = rtw_read8(padapter, REG_MCUFWDL, &tmp);
		if (res)
			return;

		rtw_write8(padapter, REG_MCUFWDL, tmp | 0x01);

		/*  8051 reset */
		res = rtw_read8(padapter, REG_MCUFWDL + 2, &tmp);
		if (res)
			return;

		rtw_write8(padapter, REG_MCUFWDL + 2, tmp & 0xf7);
	} else {
		/*  MCU firmware download disable. */
		res = rtw_read8(padapter, REG_MCUFWDL, &tmp);
		if (res)
			return;

		rtw_write8(padapter, REG_MCUFWDL, tmp & 0xfe);

		/*  Reserved for fw extension. */
		rtw_write8(padapter, REG_MCUFWDL + 1, 0x00);
	}
}

static int block_write(struct adapter *padapter, u8 *buffer, u32 size)
{
	int ret = _SUCCESS;
	u32 blocks, block_size, remain;
	u32 i, offset, addr;
	u8 *data;

	block_size = MAX_REG_BLOCK_SIZE;

	blocks = size / block_size;
	remain = size % block_size;

	for (i = 0; i < blocks; i++) {
		addr = FW_8188E_START_ADDRESS + i * block_size;
		data = buffer + i * block_size;

		ret = rtw_writeN(padapter, addr, block_size, data);
		if (ret == _FAIL)
			goto exit;
	}

	if (remain) {
		offset = blocks * block_size;
		block_size = 8;

		blocks = remain / block_size;
		remain = remain % block_size;

		for (i = 0; i < blocks; i++) {
			addr = FW_8188E_START_ADDRESS + offset + i * block_size;
			data = buffer + offset + i * block_size;

			ret = rtw_writeN(padapter, addr, block_size, data);
			if (ret == _FAIL)
				goto exit;
		}
	}

	if (remain) {
		offset += blocks * block_size;

		/* block size 1 */
		blocks = remain;

		for (i = 0; i < blocks; i++) {
			addr = FW_8188E_START_ADDRESS + offset + i;
			data = buffer + offset + i;

			ret = rtw_write8(padapter, addr, *data);
			if (ret == _FAIL)
				goto exit;
		}
	}

exit:
	return ret;
}

static int page_write(struct adapter *padapter, u32 page, u8 *buffer, u32 size)
{
	u8 value8;
	u8 u8Page = (u8)(page & 0x07);
	int res;

	res = rtw_read8(padapter, REG_MCUFWDL + 2, &value8);
	if (res)
		return _FAIL;

	value8 = (value8 & 0xF8) | u8Page;
	rtw_write8(padapter, REG_MCUFWDL + 2, value8);

	return block_write(padapter, buffer, size);
}

static int write_fw(struct adapter *padapter, u8 *buffer, u32 size)
{
	/*  Since we need dynamic decide method of dwonload fw, so we call this function to get chip version. */
	/*  We can remove _ReadChipVersion from ReadpadapterInfo8192C later. */
	int ret = _SUCCESS;
	u32	pageNums, remainSize;
	u32	page, offset;

	pageNums = size / MAX_PAGE_SIZE;
	remainSize = size % MAX_PAGE_SIZE;

	for (page = 0; page < pageNums; page++) {
		offset = page * MAX_PAGE_SIZE;
		ret = page_write(padapter, page, buffer + offset, MAX_PAGE_SIZE);

		if (ret == _FAIL)
			goto exit;
	}
	if (remainSize) {
		offset = pageNums * MAX_PAGE_SIZE;
		page = pageNums;
		ret = page_write(padapter, page, buffer + offset, remainSize);

		if (ret == _FAIL)
			goto exit;
	}
exit:
	return ret;
}

void rtw_reset_8051(struct adapter *padapter)
{
	u8 val8;
	int res;

	res = rtw_read8(padapter, REG_SYS_FUNC_EN + 1, &val8);
	if (res)
		return;

	rtw_write8(padapter, REG_SYS_FUNC_EN + 1, val8 & (~BIT(2)));
	rtw_write8(padapter, REG_SYS_FUNC_EN + 1, val8 | (BIT(2)));
}

static int fw_free_to_go(struct adapter *padapter)
{
	u32	counter = 0;
	u32	value32;
	int res;

	/*  polling CheckSum report */
	do {
		res = rtw_read32(padapter, REG_MCUFWDL, &value32);
		if (res)
			continue;

		if (value32 & FWDL_CHKSUM_RPT)
			break;
	} while (counter++ < POLLING_READY_TIMEOUT_COUNT);

	if (counter >= POLLING_READY_TIMEOUT_COUNT)
		return _FAIL;

	res = rtw_read32(padapter, REG_MCUFWDL, &value32);
	if (res)
		return _FAIL;

	value32 |= MCUFWDL_RDY;
	value32 &= ~WINTINI_RDY;
	rtw_write32(padapter, REG_MCUFWDL, value32);

	rtw_reset_8051(padapter);

	/*  polling for FW ready */
	counter = 0;
	do {
		res = rtw_read32(padapter, REG_MCUFWDL, &value32);
		if (!res && value32 & WINTINI_RDY)
			return _SUCCESS;

		udelay(5);
	} while (counter++ < POLLING_READY_TIMEOUT_COUNT);

	return _FAIL;
}

static int load_firmware(struct rt_firmware *rtfw, struct device *device)
{
	int ret = _SUCCESS;
	const struct firmware *fw;
	const char *fw_name = "rtlwifi/rtl8188eufw.bin";
	int err = request_firmware(&fw, fw_name, device);

	if (err) {
		pr_err("Request firmware failed with error 0x%x\n", err);
		ret = _FAIL;
		goto exit;
	}
	if (!fw) {
		pr_err("Firmware %s not available\n", fw_name);
		ret = _FAIL;
		goto exit;
	}

	rtfw->data = kmemdup(fw->data, fw->size, GFP_KERNEL);
	if (!rtfw->data) {
		pr_err("Failed to allocate rtfw->data\n");
		ret = _FAIL;
		goto exit;
	}
	rtfw->size = fw->size;

exit:
	release_firmware(fw);
	return ret;
}

int rtl8188e_firmware_download(struct adapter *padapter)
{
	int ret = _SUCCESS;
	u8 reg;
	unsigned long fwdl_timeout;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct device *device = dvobj_to_dev(dvobj);
	struct rt_firmware_hdr *fwhdr = NULL;
	u8 *fw_data;
	u32 fw_size;

	if (!dvobj->firmware.data)
		ret = load_firmware(&dvobj->firmware, device);
	if (ret == _FAIL) {
		dvobj->firmware.data = NULL;
		goto exit;
	}
	fw_data = dvobj->firmware.data;
	fw_size = dvobj->firmware.size;

	fwhdr = (struct rt_firmware_hdr *)dvobj->firmware.data;

	if (IS_FW_HEADER_EXIST(fwhdr)) {
		dev_info_once(device, "Firmware Version %d, SubVersion %d, Signature 0x%x\n",
			      le16_to_cpu(fwhdr->version), fwhdr->subversion,
			      le16_to_cpu(fwhdr->signature));

		fw_data = fw_data + sizeof(struct rt_firmware_hdr);
		fw_size = fw_size - sizeof(struct rt_firmware_hdr);
	}

	/*  Suggested by Filen. If 8051 is running in RAM code, driver should inform Fw to reset by itself, */
	/*  or it will cause download Fw fail. 2010.02.01. by tynli. */
	ret = rtw_read8(padapter, REG_MCUFWDL, &reg);
	if (ret) {
		ret = _FAIL;
		goto exit;
	}

	if (reg & RAM_DL_SEL) { /* 8051 RAM code */
		rtw_write8(padapter, REG_MCUFWDL, 0x00);
		rtw_reset_8051(padapter);
	}

	fw_download_enable(padapter, true);
	fwdl_timeout = jiffies + msecs_to_jiffies(500);
	do {
		/* reset the FWDL chksum */
		ret = rtw_read8(padapter, REG_MCUFWDL, &reg);
		if (ret) {
			ret = _FAIL;
			continue;
		}

		rtw_write8(padapter, REG_MCUFWDL, reg | FWDL_CHKSUM_RPT);

		ret = write_fw(padapter, fw_data, fw_size);
		if (ret == _SUCCESS)
			break;
	} while (!time_after(jiffies, fwdl_timeout));

	fw_download_enable(padapter, false);
	if (ret != _SUCCESS)
		goto exit;

	ret = fw_free_to_go(padapter);
	if (ret != _SUCCESS)
		goto exit;

exit:
	return ret;
}
