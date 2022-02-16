// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#include <linux/firmware.h>
#include "../include/rtw_fw.h"

#define MAX_REG_BOLCK_SIZE	196
#define FW_8188E_START_ADDRESS	0x1000
#define MAX_PAGE_SIZE		4096

#define IS_FW_HEADER_EXIST(_fwhdr)				\
	((le16_to_cpu(_fwhdr->Signature) & 0xFFF0) == 0x92C0 ||	\
	(le16_to_cpu(_fwhdr->Signature) & 0xFFF0) == 0x88C0 ||	\
	(le16_to_cpu(_fwhdr->Signature) & 0xFFF0) == 0x2300 ||	\
	(le16_to_cpu(_fwhdr->Signature) & 0xFFF0) == 0x88E0)

/*  This structure must be careful with byte-ordering */

struct rt_firmware_hdr {
	/*  8-byte alinment required */
	/*  LONG WORD 0 ---- */
	__le16		Signature;	/* 92C0: test chip; 92C,
					 * 88C0: test chip; 88C1: MP A-cut;
					 * 92C1: MP A-cut */
	u8		Category;	/*  AP/NIC and USB/PCI */
	u8		Function;	/*  Reserved for different FW function
					 *  indcation, for further use when
					 *  driver needs to download different
					 *  FW for different conditions */
	__le16		Version;	/*  FW Version */
	u8		Subversion;	/*  FW Subversion, default 0x00 */
	u16		Rsvd1;

	/*  LONG WORD 1 ---- */
	u8		Month;	/*  Release time Month field */
	u8		Date;	/*  Release time Date field */
	u8		Hour;	/*  Release time Hour field */
	u8		Minute;	/*  Release time Minute field */
	__le16		RamCodeSize;	/*  The size of RAM code */
	u8		Foundry;
	u8		Rsvd2;

	/*  LONG WORD 2 ---- */
	__le32		SvnIdx;	/*  The SVN entry index */
	u32		Rsvd3;

	/*  LONG WORD 3 ---- */
	u32		Rsvd4;
	u32		Rsvd5;
};

static void fw_download_enable(struct adapter *padapter, bool enable)
{
	u8 tmp;

	if (enable) {
		/*  MCU firmware download enable. */
		tmp = rtw_read8(padapter, REG_MCUFWDL);
		rtw_write8(padapter, REG_MCUFWDL, tmp | 0x01);

		/*  8051 reset */
		tmp = rtw_read8(padapter, REG_MCUFWDL + 2);
		rtw_write8(padapter, REG_MCUFWDL + 2, tmp & 0xf7);
	} else {
		/*  MCU firmware download disable. */
		tmp = rtw_read8(padapter, REG_MCUFWDL);
		rtw_write8(padapter, REG_MCUFWDL, tmp & 0xfe);

		/*  Reserved for fw extension. */
		rtw_write8(padapter, REG_MCUFWDL + 1, 0x00);
	}
}

static int block_write(struct adapter *padapter, void *buffer, u32 buffSize)
{
	int ret = _SUCCESS;
	u32	blockSize_p1 = 4;	/*  (Default) Phase #1 : PCI muse use 4-byte write to download FW */
	u32	blockSize_p2 = 8;	/*  Phase #2 : Use 8-byte, if Phase#1 use big size to write FW. */
	u32	blockSize_p3 = 1;	/*  Phase #3 : Use 1-byte, the remnant of FW image. */
	u32	blockCount_p1 = 0, blockCount_p2 = 0, blockCount_p3 = 0;
	u32	remainSize_p1 = 0, remainSize_p2 = 0;
	u8 *bufferPtr	= (u8 *)buffer;
	u32	i = 0, offset = 0;

	blockSize_p1 = MAX_REG_BOLCK_SIZE;

	/* 3 Phase #1 */
	blockCount_p1 = buffSize / blockSize_p1;
	remainSize_p1 = buffSize % blockSize_p1;

	for (i = 0; i < blockCount_p1; i++) {
		ret = rtw_writeN(padapter, (FW_8188E_START_ADDRESS + i * blockSize_p1), blockSize_p1, (bufferPtr + i * blockSize_p1));
		if (ret == _FAIL)
			goto exit;
	}

	/* 3 Phase #2 */
	if (remainSize_p1) {
		offset = blockCount_p1 * blockSize_p1;

		blockCount_p2 = remainSize_p1 / blockSize_p2;
		remainSize_p2 = remainSize_p1 % blockSize_p2;

		for (i = 0; i < blockCount_p2; i++) {
			ret = rtw_writeN(padapter, (FW_8188E_START_ADDRESS + offset + i * blockSize_p2), blockSize_p2, (bufferPtr + offset + i * blockSize_p2));

			if (ret == _FAIL)
				goto exit;
		}
	}

	/* 3 Phase #3 */
	if (remainSize_p2) {
		offset = (blockCount_p1 * blockSize_p1) + (blockCount_p2 * blockSize_p2);

		blockCount_p3 = remainSize_p2 / blockSize_p3;

		for (i = 0; i < blockCount_p3; i++) {
			ret = rtw_write8(padapter, (FW_8188E_START_ADDRESS + offset + i), *(bufferPtr + offset + i));

			if (ret == _FAIL)
				goto exit;
		}
	}

exit:
	return ret;
}

static int page_write(struct adapter *padapter, u32 page, void *buffer, u32 size)
{
	u8 value8;
	u8 u8Page = (u8)(page & 0x07);

	value8 = (rtw_read8(padapter, REG_MCUFWDL + 2) & 0xF8) | u8Page;
	rtw_write8(padapter, REG_MCUFWDL + 2, value8);

	return block_write(padapter, buffer, size);
}

static int write_fw(struct adapter *padapter, void *buffer, u32 size)
{
	/*  Since we need dynamic decide method of dwonload fw, so we call this function to get chip version. */
	/*  We can remove _ReadChipVersion from ReadpadapterInfo8192C later. */
	int ret = _SUCCESS;
	u32	pageNums, remainSize;
	u32	page, offset;
	u8 *bufferPtr = (u8 *)buffer;

	pageNums = size / MAX_PAGE_SIZE;
	remainSize = size % MAX_PAGE_SIZE;

	for (page = 0; page < pageNums; page++) {
		offset = page * MAX_PAGE_SIZE;
		ret = page_write(padapter, page, bufferPtr + offset, MAX_PAGE_SIZE);

		if (ret == _FAIL)
			goto exit;
	}
	if (remainSize) {
		offset = pageNums * MAX_PAGE_SIZE;
		page = pageNums;
		ret = page_write(padapter, page, bufferPtr + offset, remainSize);

		if (ret == _FAIL)
			goto exit;
	}
exit:
	return ret;
}

void rtw_reset_8051(struct adapter *padapter)
{
	u8 val8;

	val8 = rtw_read8(padapter, REG_SYS_FUNC_EN + 1);
	rtw_write8(padapter, REG_SYS_FUNC_EN + 1, val8 & (~BIT(2)));
	rtw_write8(padapter, REG_SYS_FUNC_EN + 1, val8 | (BIT(2)));
}

static int fw_free_to_go(struct adapter *padapter)
{
	u32	counter = 0;
	u32	value32;

	/*  polling CheckSum report */
	do {
		value32 = rtw_read32(padapter, REG_MCUFWDL);
		if (value32 & FWDL_CHKSUM_RPT)
			break;
	} while (counter++ < POLLING_READY_TIMEOUT_COUNT);

	if (counter >= POLLING_READY_TIMEOUT_COUNT)
		return _FAIL;

	value32 = rtw_read32(padapter, REG_MCUFWDL);
	value32 |= MCUFWDL_RDY;
	value32 &= ~WINTINI_RDY;
	rtw_write32(padapter, REG_MCUFWDL, value32);

	rtw_reset_8051(padapter);

	/*  polling for FW ready */
	counter = 0;
	do {
		value32 = rtw_read32(padapter, REG_MCUFWDL);
		if (value32 & WINTINI_RDY)
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
	u8 write_fw_retry = 0;
	u32 fwdl_start_time;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct device *device = dvobj_to_dev(dvobj);
	struct rt_firmware_hdr *fwhdr = NULL;
	u16 fw_version, fw_subversion, fw_signature;
	u8 *fw_data;
	u32 fw_size;
	static int log_version;

	if (!dvobj->firmware.data)
		ret = load_firmware(&dvobj->firmware, device);
	if (ret == _FAIL) {
		dvobj->firmware.data = NULL;
		goto exit;
	}
	fw_data = dvobj->firmware.data;
	fw_size = dvobj->firmware.size;

	/*  To Check Fw header. Added by tynli. 2009.12.04. */
	fwhdr = (struct rt_firmware_hdr *)dvobj->firmware.data;

	fw_version = le16_to_cpu(fwhdr->Version);
	fw_subversion = fwhdr->Subversion;
	fw_signature = le16_to_cpu(fwhdr->Signature);

	if (!log_version++)
		pr_info("%sFirmware Version %d, SubVersion %d, Signature 0x%x\n",
			DRIVER_PREFIX, fw_version, fw_subversion, fw_signature);

	if (IS_FW_HEADER_EXIST(fwhdr)) {
		/*  Shift 32 bytes for FW header */
		fw_data = fw_data + 32;
		fw_size = fw_size - 32;
	}

	/*  Suggested by Filen. If 8051 is running in RAM code, driver should inform Fw to reset by itself, */
	/*  or it will cause download Fw fail. 2010.02.01. by tynli. */
	if (rtw_read8(padapter, REG_MCUFWDL) & RAM_DL_SEL) { /* 8051 RAM code */
		rtw_write8(padapter, REG_MCUFWDL, 0x00);
		rtw_reset_8051(padapter);
	}

	fw_download_enable(padapter, true);
	fwdl_start_time = jiffies;
	while (1) {
		/* reset the FWDL chksum */
		rtw_write8(padapter, REG_MCUFWDL, rtw_read8(padapter, REG_MCUFWDL) | FWDL_CHKSUM_RPT);

		ret = write_fw(padapter, fw_data, fw_size);

		if (ret == _SUCCESS ||
		    (rtw_get_passing_time_ms(fwdl_start_time) > 500 && write_fw_retry++ >= 3))
			break;
	}
	fw_download_enable(padapter, false);
	if (ret != _SUCCESS)
		goto exit;

	ret = fw_free_to_go(padapter);
	if (ret != _SUCCESS)
		goto exit;

exit:
	return ret;
}
