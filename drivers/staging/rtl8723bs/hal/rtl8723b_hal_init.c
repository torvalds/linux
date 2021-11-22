// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2013 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include <linux/firmware.h>
#include <linux/slab.h>
#include <drv_types.h>
#include <rtw_debug.h>
#include <rtl8723b_hal.h>
#include "hal_com_h2c.h"

static void _FWDownloadEnable(struct adapter *padapter, bool enable)
{
	u8 tmp, count = 0;

	if (enable) {
		/*  8051 enable */
		tmp = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
		rtw_write8(padapter, REG_SYS_FUNC_EN+1, tmp|0x04);

		tmp = rtw_read8(padapter, REG_MCUFWDL);
		rtw_write8(padapter, REG_MCUFWDL, tmp|0x01);

		do {
			tmp = rtw_read8(padapter, REG_MCUFWDL);
			if (tmp & 0x01)
				break;
			rtw_write8(padapter, REG_MCUFWDL, tmp|0x01);
			msleep(1);
		} while (count++ < 100);

		/*  8051 reset */
		tmp = rtw_read8(padapter, REG_MCUFWDL+2);
		rtw_write8(padapter, REG_MCUFWDL+2, tmp&0xf7);
	} else {
		/*  MCU firmware download disable. */
		tmp = rtw_read8(padapter, REG_MCUFWDL);
		rtw_write8(padapter, REG_MCUFWDL, tmp&0xfe);
	}
}

static int _BlockWrite(struct adapter *padapter, void *buffer, u32 buffSize)
{
	int ret = _SUCCESS;

	u32 blockSize_p1 = 4; /*  (Default) Phase #1 : PCI muse use 4-byte write to download FW */
	u32 blockSize_p2 = 8; /*  Phase #2 : Use 8-byte, if Phase#1 use big size to write FW. */
	u32 blockSize_p3 = 1; /*  Phase #3 : Use 1-byte, the remnant of FW image. */
	u32 blockCount_p1 = 0, blockCount_p2 = 0, blockCount_p3 = 0;
	u32 remainSize_p1 = 0, remainSize_p2 = 0;
	u8 *bufferPtr = buffer;
	u32 i = 0, offset = 0;

/* 	printk("====>%s %d\n", __func__, __LINE__); */

	/* 3 Phase #1 */
	blockCount_p1 = buffSize / blockSize_p1;
	remainSize_p1 = buffSize % blockSize_p1;

	for (i = 0; i < blockCount_p1; i++) {
		ret = rtw_write32(padapter, (FW_8723B_START_ADDRESS + i * blockSize_p1), *((u32 *)(bufferPtr + i * blockSize_p1)));
		if (ret == _FAIL) {
			printk("====>%s %d i:%d\n", __func__, __LINE__, i);
			goto exit;
		}
	}

	/* 3 Phase #2 */
	if (remainSize_p1) {
		offset = blockCount_p1 * blockSize_p1;

		blockCount_p2 = remainSize_p1/blockSize_p2;
		remainSize_p2 = remainSize_p1%blockSize_p2;
	}

	/* 3 Phase #3 */
	if (remainSize_p2) {
		offset = (blockCount_p1 * blockSize_p1) + (blockCount_p2 * blockSize_p2);

		blockCount_p3 = remainSize_p2 / blockSize_p3;

		for (i = 0; i < blockCount_p3; i++) {
			ret = rtw_write8(padapter, (FW_8723B_START_ADDRESS + offset + i), *(bufferPtr + offset + i));

			if (ret == _FAIL) {
				printk("====>%s %d i:%d\n", __func__, __LINE__, i);
				goto exit;
			}
		}
	}
exit:
	return ret;
}

static int _PageWrite(
	struct adapter *padapter,
	u32 page,
	void *buffer,
	u32 size
)
{
	u8 value8;
	u8 u8Page = (u8) (page & 0x07);

	value8 = (rtw_read8(padapter, REG_MCUFWDL+2) & 0xF8) | u8Page;
	rtw_write8(padapter, REG_MCUFWDL+2, value8);

	return _BlockWrite(padapter, buffer, size);
}

static int _WriteFW(struct adapter *padapter, void *buffer, u32 size)
{
	/*  Since we need dynamic decide method of dwonload fw, so we call this function to get chip version. */
	/*  We can remove _ReadChipVersion from ReadpadapterInfo8192C later. */
	int ret = _SUCCESS;
	u32 pageNums, remainSize;
	u32 page, offset;
	u8 *bufferPtr = buffer;

	pageNums = size / MAX_DLFW_PAGE_SIZE;
	remainSize = size % MAX_DLFW_PAGE_SIZE;

	for (page = 0; page < pageNums; page++) {
		offset = page * MAX_DLFW_PAGE_SIZE;
		ret = _PageWrite(padapter, page, bufferPtr+offset, MAX_DLFW_PAGE_SIZE);

		if (ret == _FAIL) {
			printk("====>%s %d\n", __func__, __LINE__);
			goto exit;
		}
	}

	if (remainSize) {
		offset = pageNums * MAX_DLFW_PAGE_SIZE;
		page = pageNums;
		ret = _PageWrite(padapter, page, bufferPtr+offset, remainSize);

		if (ret == _FAIL) {
			printk("====>%s %d\n", __func__, __LINE__);
			goto exit;
		}
	}

exit:
	return ret;
}

void _8051Reset8723(struct adapter *padapter)
{
	u8 cpu_rst;
	u8 io_rst;


	/*  Reset 8051(WLMCU) IO wrapper */
	/*  0x1c[8] = 0 */
	/*  Suggested by Isaac@SD1 and Gimmy@SD1, coding by Lucas@20130624 */
	io_rst = rtw_read8(padapter, REG_RSV_CTRL+1);
	io_rst &= ~BIT(0);
	rtw_write8(padapter, REG_RSV_CTRL+1, io_rst);

	cpu_rst = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
	cpu_rst &= ~BIT(2);
	rtw_write8(padapter, REG_SYS_FUNC_EN+1, cpu_rst);

	/*  Enable 8051 IO wrapper */
	/*  0x1c[8] = 1 */
	io_rst = rtw_read8(padapter, REG_RSV_CTRL+1);
	io_rst |= BIT(0);
	rtw_write8(padapter, REG_RSV_CTRL+1, io_rst);

	cpu_rst = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
	cpu_rst |= BIT(2);
	rtw_write8(padapter, REG_SYS_FUNC_EN+1, cpu_rst);
}

u8 g_fwdl_chksum_fail;

static s32 polling_fwdl_chksum(
	struct adapter *adapter, u32 min_cnt, u32 timeout_ms
)
{
	s32 ret = _FAIL;
	u32 value32;
	unsigned long start = jiffies;
	u32 cnt = 0;

	/* polling CheckSum report */
	do {
		cnt++;
		value32 = rtw_read32(adapter, REG_MCUFWDL);
		if (value32 & FWDL_ChkSum_rpt || adapter->bSurpriseRemoved || adapter->bDriverStopped)
			break;
		yield();
	} while (jiffies_to_msecs(jiffies-start) < timeout_ms || cnt < min_cnt);

	if (!(value32 & FWDL_ChkSum_rpt)) {
		goto exit;
	}

	if (g_fwdl_chksum_fail) {
		g_fwdl_chksum_fail--;
		goto exit;
	}

	ret = _SUCCESS;

exit:

	return ret;
}

u8 g_fwdl_wintint_rdy_fail;

static s32 _FWFreeToGo(struct adapter *adapter, u32 min_cnt, u32 timeout_ms)
{
	s32 ret = _FAIL;
	u32 value32;
	unsigned long start = jiffies;
	u32 cnt = 0;

	value32 = rtw_read32(adapter, REG_MCUFWDL);
	value32 |= MCUFWDL_RDY;
	value32 &= ~WINTINI_RDY;
	rtw_write32(adapter, REG_MCUFWDL, value32);

	_8051Reset8723(adapter);

	/*  polling for FW ready */
	do {
		cnt++;
		value32 = rtw_read32(adapter, REG_MCUFWDL);
		if (value32 & WINTINI_RDY || adapter->bSurpriseRemoved || adapter->bDriverStopped)
			break;
		yield();
	} while (jiffies_to_msecs(jiffies - start) < timeout_ms || cnt < min_cnt);

	if (!(value32 & WINTINI_RDY)) {
		goto exit;
	}

	if (g_fwdl_wintint_rdy_fail) {
		g_fwdl_wintint_rdy_fail--;
		goto exit;
	}

	ret = _SUCCESS;

exit:

	return ret;
}

#define IS_FW_81xxC(padapter)	(((GET_HAL_DATA(padapter))->FirmwareSignature & 0xFFF0) == 0x88C0)

void rtl8723b_FirmwareSelfReset(struct adapter *padapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	u8 u1bTmp;
	u8 Delay = 100;

	if (
		!(IS_FW_81xxC(padapter) && ((pHalData->FirmwareVersion < 0x21) || (pHalData->FirmwareVersion == 0x21 && pHalData->FirmwareSubVersion < 0x01)))
	) { /*  after 88C Fw v33.1 */
		/* 0x1cf = 0x20. Inform 8051 to reset. 2009.12.25. tynli_test */
		rtw_write8(padapter, REG_HMETFR+3, 0x20);

		u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
		while (u1bTmp & BIT2) {
			Delay--;
			if (Delay == 0)
				break;
			udelay(50);
			u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
		}

		if (Delay == 0) {
			/* force firmware reset */
			u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
			rtw_write8(padapter, REG_SYS_FUNC_EN+1, u1bTmp&(~BIT2));
		}
	}
}

/*  */
/* 	Description: */
/* 		Download 8192C firmware code. */
/*  */
/*  */
s32 rtl8723b_FirmwareDownload(struct adapter *padapter, bool  bUsedWoWLANFw)
{
	s32 rtStatus = _SUCCESS;
	u8 write_fw = 0;
	unsigned long fwdl_start_time;
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	struct rt_firmware *pFirmware;
	struct rt_firmware *pBTFirmware;
	struct rt_firmware_hdr *pFwHdr = NULL;
	u8 *pFirmwareBuf;
	u32 FirmwareLen;
	const struct firmware *fw;
	struct device *device = dvobj_to_dev(padapter->dvobj);
	u8 *fwfilepath;
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	u8 tmp_ps;

	pFirmware = kzalloc(sizeof(struct rt_firmware), GFP_KERNEL);
	if (!pFirmware)
		return _FAIL;
	pBTFirmware = kzalloc(sizeof(struct rt_firmware), GFP_KERNEL);
	if (!pBTFirmware) {
		kfree(pFirmware);
		return _FAIL;
	}
	tmp_ps = rtw_read8(padapter, 0xa3);
	tmp_ps &= 0xf8;
	tmp_ps |= 0x02;
	/* 1. write 0xA3[:2:0] = 3b'010 */
	rtw_write8(padapter, 0xa3, tmp_ps);
	/* 2. read power_state = 0xA0[1:0] */
	tmp_ps = rtw_read8(padapter, 0xa0);
	tmp_ps &= 0x03;
	if (tmp_ps != 0x01)
		pdbgpriv->dbg_downloadfw_pwr_state_cnt++;

	fwfilepath = "rtlwifi/rtl8723bs_nic.bin";

	pr_info("rtl8723bs: acquire FW from file:%s\n", fwfilepath);

	rtStatus = request_firmware(&fw, fwfilepath, device);
	if (rtStatus) {
		pr_err("Request firmware failed with error 0x%x\n", rtStatus);
		rtStatus = _FAIL;
		goto exit;
	}

	if (!fw) {
		pr_err("Firmware %s not available\n", fwfilepath);
		rtStatus = _FAIL;
		goto exit;
	}

	if (fw->size > FW_8723B_SIZE) {
		rtStatus = _FAIL;
		goto exit;
	}

	pFirmware->fw_buffer_sz = kmemdup(fw->data, fw->size, GFP_KERNEL);
	if (!pFirmware->fw_buffer_sz) {
		rtStatus = _FAIL;
		goto exit;
	}

	pFirmware->fw_length = fw->size;
	release_firmware(fw);
	if (pFirmware->fw_length > FW_8723B_SIZE) {
		rtStatus = _FAIL;
		netdev_emerg(padapter->pnetdev,
			     "Firmware size:%u exceed %u\n",
			     pFirmware->fw_length, FW_8723B_SIZE);
		goto release_fw1;
	}

	pFirmwareBuf = pFirmware->fw_buffer_sz;
	FirmwareLen = pFirmware->fw_length;

	/*  To Check Fw header. Added by tynli. 2009.12.04. */
	pFwHdr = (struct rt_firmware_hdr *)pFirmwareBuf;

	pHalData->FirmwareVersion =  le16_to_cpu(pFwHdr->version);
	pHalData->FirmwareSubVersion = le16_to_cpu(pFwHdr->subversion);
	pHalData->FirmwareSignature = le16_to_cpu(pFwHdr->signature);

	if (IS_FW_HEADER_EXIST_8723B(pFwHdr)) {
		/*  Shift 32 bytes for FW header */
		pFirmwareBuf = pFirmwareBuf + 32;
		FirmwareLen = FirmwareLen - 32;
	}

	/*  Suggested by Filen. If 8051 is running in RAM code, driver should inform Fw to reset by itself, */
	/*  or it will cause download Fw fail. 2010.02.01. by tynli. */
	if (rtw_read8(padapter, REG_MCUFWDL) & RAM_DL_SEL) { /* 8051 RAM code */
		rtw_write8(padapter, REG_MCUFWDL, 0x00);
		rtl8723b_FirmwareSelfReset(padapter);
	}

	_FWDownloadEnable(padapter, true);
	fwdl_start_time = jiffies;
	while (
		!padapter->bDriverStopped &&
		!padapter->bSurpriseRemoved &&
		(write_fw++ < 3 || jiffies_to_msecs(jiffies - fwdl_start_time) < 500)
	) {
		/* reset FWDL chksum */
		rtw_write8(padapter, REG_MCUFWDL, rtw_read8(padapter, REG_MCUFWDL)|FWDL_ChkSum_rpt);

		rtStatus = _WriteFW(padapter, pFirmwareBuf, FirmwareLen);
		if (rtStatus != _SUCCESS)
			continue;

		rtStatus = polling_fwdl_chksum(padapter, 5, 50);
		if (rtStatus == _SUCCESS)
			break;
	}
	_FWDownloadEnable(padapter, false);
	if (_SUCCESS != rtStatus)
		goto fwdl_stat;

	rtStatus = _FWFreeToGo(padapter, 10, 200);
	if (_SUCCESS != rtStatus)
		goto fwdl_stat;

fwdl_stat:

exit:
	kfree(pFirmware->fw_buffer_sz);
	kfree(pFirmware);
release_fw1:
	kfree(pBTFirmware);
	return rtStatus;
}

void rtl8723b_InitializeFirmwareVars(struct adapter *padapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);

	/*  Init Fw LPS related. */
	adapter_to_pwrctl(padapter)->fw_current_in_ps_mode = false;

	/* Init H2C cmd. */
	rtw_write8(padapter, REG_HMETFR, 0x0f);

	/*  Init H2C counter. by tynli. 2009.12.09. */
	pHalData->LastHMEBoxNum = 0;
/* pHalData->H2CQueueHead = 0; */
/* pHalData->H2CQueueTail = 0; */
/* pHalData->H2CStopInsertQueue = false; */
}

static void rtl8723b_free_hal_data(struct adapter *padapter)
{
}

/*  */
/* 				Efuse related code */
/*  */
static u8 hal_EfuseSwitchToBank(
	struct adapter *padapter, u8 bank, bool bPseudoTest
)
{
	u8 bRet = false;
	u32 value32 = 0;
#ifdef HAL_EFUSE_MEMORY
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	struct efuse_hal *pEfuseHal = &pHalData->EfuseHal;
#endif


	if (bPseudoTest) {
#ifdef HAL_EFUSE_MEMORY
		pEfuseHal->fakeEfuseBank = bank;
#else
		fakeEfuseBank = bank;
#endif
		bRet = true;
	} else {
		value32 = rtw_read32(padapter, EFUSE_TEST);
		bRet = true;
		switch (bank) {
		case 0:
			value32 = (value32 & ~EFUSE_SEL_MASK) | EFUSE_SEL(EFUSE_WIFI_SEL_0);
			break;
		case 1:
			value32 = (value32 & ~EFUSE_SEL_MASK) | EFUSE_SEL(EFUSE_BT_SEL_0);
			break;
		case 2:
			value32 = (value32 & ~EFUSE_SEL_MASK) | EFUSE_SEL(EFUSE_BT_SEL_1);
			break;
		case 3:
			value32 = (value32 & ~EFUSE_SEL_MASK) | EFUSE_SEL(EFUSE_BT_SEL_2);
			break;
		default:
			value32 = (value32 & ~EFUSE_SEL_MASK) | EFUSE_SEL(EFUSE_WIFI_SEL_0);
			bRet = false;
			break;
		}
		rtw_write32(padapter, EFUSE_TEST, value32);
	}

	return bRet;
}

static void Hal_GetEfuseDefinition(
	struct adapter *padapter,
	u8 efuseType,
	u8 type,
	void *pOut,
	bool bPseudoTest
)
{
	switch (type) {
	case TYPE_EFUSE_MAX_SECTION:
		{
			u8 *pMax_section;
			pMax_section = pOut;

			if (efuseType == EFUSE_WIFI)
				*pMax_section = EFUSE_MAX_SECTION_8723B;
			else
				*pMax_section = EFUSE_BT_MAX_SECTION;
		}
		break;

	case TYPE_EFUSE_REAL_CONTENT_LEN:
		{
			u16 *pu2Tmp;
			pu2Tmp = pOut;

			if (efuseType == EFUSE_WIFI)
				*pu2Tmp = EFUSE_REAL_CONTENT_LEN_8723B;
			else
				*pu2Tmp = EFUSE_BT_REAL_CONTENT_LEN;
		}
		break;

	case TYPE_AVAILABLE_EFUSE_BYTES_BANK:
		{
			u16 *pu2Tmp;
			pu2Tmp = pOut;

			if (efuseType == EFUSE_WIFI)
				*pu2Tmp = (EFUSE_REAL_CONTENT_LEN_8723B-EFUSE_OOB_PROTECT_BYTES);
			else
				*pu2Tmp = (EFUSE_BT_REAL_BANK_CONTENT_LEN-EFUSE_PROTECT_BYTES_BANK);
		}
		break;

	case TYPE_AVAILABLE_EFUSE_BYTES_TOTAL:
		{
			u16 *pu2Tmp;
			pu2Tmp = pOut;

			if (efuseType == EFUSE_WIFI)
				*pu2Tmp = (EFUSE_REAL_CONTENT_LEN_8723B-EFUSE_OOB_PROTECT_BYTES);
			else
				*pu2Tmp = (EFUSE_BT_REAL_CONTENT_LEN-(EFUSE_PROTECT_BYTES_BANK*3));
		}
		break;

	case TYPE_EFUSE_MAP_LEN:
		{
			u16 *pu2Tmp;
			pu2Tmp = pOut;

			if (efuseType == EFUSE_WIFI)
				*pu2Tmp = EFUSE_MAX_MAP_LEN;
			else
				*pu2Tmp = EFUSE_BT_MAP_LEN;
		}
		break;

	case TYPE_EFUSE_PROTECT_BYTES_BANK:
		{
			u8 *pu1Tmp;
			pu1Tmp = pOut;

			if (efuseType == EFUSE_WIFI)
				*pu1Tmp = EFUSE_OOB_PROTECT_BYTES;
			else
				*pu1Tmp = EFUSE_PROTECT_BYTES_BANK;
		}
		break;

	case TYPE_EFUSE_CONTENT_LEN_BANK:
		{
			u16 *pu2Tmp;
			pu2Tmp = pOut;

			if (efuseType == EFUSE_WIFI)
				*pu2Tmp = EFUSE_REAL_CONTENT_LEN_8723B;
			else
				*pu2Tmp = EFUSE_BT_REAL_BANK_CONTENT_LEN;
		}
		break;

	default:
		{
			u8 *pu1Tmp;
			pu1Tmp = pOut;
			*pu1Tmp = 0;
		}
		break;
	}
}

#define VOLTAGE_V25		0x03

/*  */
/* 	The following is for compile ok */
/* 	That should be merged with the original in the future */
/*  */
#define EFUSE_ACCESS_ON_8723			0x69	/*  For RTL8723 only. */
#define REG_EFUSE_ACCESS_8723			0x00CF	/*  Efuse access protection for RTL8723 */

/*  */
static void Hal_BT_EfusePowerSwitch(
	struct adapter *padapter, u8 bWrite, u8 PwrState
)
{
	u8 tempval;
	if (PwrState) {
		/*  enable BT power cut */
		/*  0x6A[14] = 1 */
		tempval = rtw_read8(padapter, 0x6B);
		tempval |= BIT(6);
		rtw_write8(padapter, 0x6B, tempval);

		/*  Attention!! Between 0x6A[14] and 0x6A[15] setting need 100us delay */
		/*  So don't write 0x6A[14]= 1 and 0x6A[15]= 0 together! */
		msleep(1);
		/*  disable BT output isolation */
		/*  0x6A[15] = 0 */
		tempval = rtw_read8(padapter, 0x6B);
		tempval &= ~BIT(7);
		rtw_write8(padapter, 0x6B, tempval);
	} else {
		/*  enable BT output isolation */
		/*  0x6A[15] = 1 */
		tempval = rtw_read8(padapter, 0x6B);
		tempval |= BIT(7);
		rtw_write8(padapter, 0x6B, tempval);

		/*  Attention!! Between 0x6A[14] and 0x6A[15] setting need 100us delay */
		/*  So don't write 0x6A[14]= 1 and 0x6A[15]= 0 together! */

		/*  disable BT power cut */
		/*  0x6A[14] = 1 */
		tempval = rtw_read8(padapter, 0x6B);
		tempval &= ~BIT(6);
		rtw_write8(padapter, 0x6B, tempval);
	}

}
static void Hal_EfusePowerSwitch(
	struct adapter *padapter, u8 bWrite, u8 PwrState
)
{
	u8 tempval;
	u16 tmpV16;


	if (PwrState) {
		/*  To avoid cannot access efuse regsiters after disable/enable several times during DTM test. */
		/*  Suggested by SD1 IsaacHsu. 2013.07.08, added by tynli. */
		tempval = rtw_read8(padapter, SDIO_LOCAL_BASE|SDIO_REG_HSUS_CTRL);
		if (tempval & BIT(0)) { /*  SDIO local register is suspend */
			u8 count = 0;


			tempval &= ~BIT(0);
			rtw_write8(padapter, SDIO_LOCAL_BASE|SDIO_REG_HSUS_CTRL, tempval);

			/*  check 0x86[1:0]= 10'2h, wait power state to leave suspend */
			do {
				tempval = rtw_read8(padapter, SDIO_LOCAL_BASE|SDIO_REG_HSUS_CTRL);
				tempval &= 0x3;
				if (tempval == 0x02)
					break;

				count++;
				if (count >= 100)
					break;

				mdelay(10);
			} while (1);
		}

		rtw_write8(padapter, REG_EFUSE_ACCESS_8723, EFUSE_ACCESS_ON_8723);

		/*  Reset: 0x0000h[28], default valid */
		tmpV16 =  rtw_read16(padapter, REG_SYS_FUNC_EN);
		if (!(tmpV16 & FEN_ELDR)) {
			tmpV16 |= FEN_ELDR;
			rtw_write16(padapter, REG_SYS_FUNC_EN, tmpV16);
		}

		/*  Clock: Gated(0x0008h[5]) 8M(0x0008h[1]) clock from ANA, default valid */
		tmpV16 = rtw_read16(padapter, REG_SYS_CLKR);
		if ((!(tmpV16 & LOADER_CLK_EN))  || (!(tmpV16 & ANA8M))) {
			tmpV16 |= (LOADER_CLK_EN | ANA8M);
			rtw_write16(padapter, REG_SYS_CLKR, tmpV16);
		}

		if (bWrite) {
			/*  Enable LDO 2.5V before read/write action */
			tempval = rtw_read8(padapter, EFUSE_TEST+3);
			tempval &= 0x0F;
			tempval |= (VOLTAGE_V25 << 4);
			rtw_write8(padapter, EFUSE_TEST+3, (tempval | 0x80));

			/* rtw_write8(padapter, REG_EFUSE_ACCESS, EFUSE_ACCESS_ON); */
		}
	} else {
		rtw_write8(padapter, REG_EFUSE_ACCESS, EFUSE_ACCESS_OFF);

		if (bWrite) {
			/*  Disable LDO 2.5V after read/write action */
			tempval = rtw_read8(padapter, EFUSE_TEST+3);
			rtw_write8(padapter, EFUSE_TEST+3, (tempval & 0x7F));
		}

	}
}

static void hal_ReadEFuse_WiFi(
	struct adapter *padapter,
	u16 _offset,
	u16 _size_byte,
	u8 *pbuf,
	bool bPseudoTest
)
{
#ifdef HAL_EFUSE_MEMORY
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	struct efuse_hal *pEfuseHal = &pHalData->EfuseHal;
#endif
	u8 *efuseTbl = NULL;
	u16 eFuse_Addr = 0;
	u8 offset, wden;
	u8 efuseHeader, efuseExtHdr, efuseData;
	u16 i, total, used;
	u8 efuse_usage = 0;

	/*  */
	/*  Do NOT excess total size of EFuse table. Added by Roger, 2008.11.10. */
	/*  */
	if ((_offset + _size_byte) > EFUSE_MAX_MAP_LEN)
		return;

	efuseTbl = rtw_malloc(EFUSE_MAX_MAP_LEN);
	if (!efuseTbl)
		return;

	/*  0xff will be efuse default value instead of 0x00. */
	memset(efuseTbl, 0xFF, EFUSE_MAX_MAP_LEN);

	/*  switch bank back to bank 0 for later BT and wifi use. */
	hal_EfuseSwitchToBank(padapter, 0, bPseudoTest);

	while (AVAILABLE_EFUSE_ADDR(eFuse_Addr)) {
		efuse_OneByteRead(padapter, eFuse_Addr++, &efuseHeader, bPseudoTest);
		if (efuseHeader == 0xFF)
			break;

		/*  Check PG header for section num. */
		if (EXT_HEADER(efuseHeader)) { /* extended header */
			offset = GET_HDR_OFFSET_2_0(efuseHeader);

			efuse_OneByteRead(padapter, eFuse_Addr++, &efuseExtHdr, bPseudoTest);
			if (ALL_WORDS_DISABLED(efuseExtHdr))
				continue;

			offset |= ((efuseExtHdr & 0xF0) >> 1);
			wden = (efuseExtHdr & 0x0F);
		} else {
			offset = ((efuseHeader >> 4) & 0x0f);
			wden = (efuseHeader & 0x0f);
		}

		if (offset < EFUSE_MAX_SECTION_8723B) {
			u16 addr;
			/*  Get word enable value from PG header */

			addr = offset * PGPKT_DATA_SIZE;
			for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++) {
				/*  Check word enable condition in the section */
				if (!(wden & (0x01<<i))) {
					efuse_OneByteRead(padapter, eFuse_Addr++, &efuseData, bPseudoTest);
					efuseTbl[addr] = efuseData;

					efuse_OneByteRead(padapter, eFuse_Addr++, &efuseData, bPseudoTest);
					efuseTbl[addr+1] = efuseData;
				}
				addr += 2;
			}
		} else {
			eFuse_Addr += Efuse_CalculateWordCnts(wden)*2;
		}
	}

	/*  Copy from Efuse map to output pointer memory!!! */
	for (i = 0; i < _size_byte; i++)
		pbuf[i] = efuseTbl[_offset+i];

	/*  Calculate Efuse utilization */
	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, &total, bPseudoTest);
	used = eFuse_Addr - 1;
	efuse_usage = (u8)((used*100)/total);
	if (bPseudoTest) {
#ifdef HAL_EFUSE_MEMORY
		pEfuseHal->fakeEfuseUsedBytes = used;
#else
		fakeEfuseUsedBytes = used;
#endif
	} else {
		rtw_hal_set_hwreg(padapter, HW_VAR_EFUSE_BYTES, (u8 *)&used);
		rtw_hal_set_hwreg(padapter, HW_VAR_EFUSE_USAGE, (u8 *)&efuse_usage);
	}

	kfree(efuseTbl);
}

static void hal_ReadEFuse_BT(
	struct adapter *padapter,
	u16 _offset,
	u16 _size_byte,
	u8 *pbuf,
	bool bPseudoTest
)
{
#ifdef HAL_EFUSE_MEMORY
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	struct efuse_hal *pEfuseHal = &pHalData->EfuseHal;
#endif
	u8 *efuseTbl;
	u8 bank;
	u16 eFuse_Addr;
	u8 efuseHeader, efuseExtHdr, efuseData;
	u8 offset, wden;
	u16 i, total, used;
	u8 efuse_usage;


	/*  */
	/*  Do NOT excess total size of EFuse table. Added by Roger, 2008.11.10. */
	/*  */
	if ((_offset + _size_byte) > EFUSE_BT_MAP_LEN)
		return;

	efuseTbl = rtw_malloc(EFUSE_BT_MAP_LEN);
	if (!efuseTbl)
		return;

	/*  0xff will be efuse default value instead of 0x00. */
	memset(efuseTbl, 0xFF, EFUSE_BT_MAP_LEN);

	EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_AVAILABLE_EFUSE_BYTES_BANK, &total, bPseudoTest);

	for (bank = 1; bank < 3; bank++) { /*  8723b Max bake 0~2 */
		if (hal_EfuseSwitchToBank(padapter, bank, bPseudoTest) == false)
			goto exit;

		eFuse_Addr = 0;

		while (AVAILABLE_EFUSE_ADDR(eFuse_Addr)) {
			efuse_OneByteRead(padapter, eFuse_Addr++, &efuseHeader, bPseudoTest);
			if (efuseHeader == 0xFF)
				break;

			/*  Check PG header for section num. */
			if (EXT_HEADER(efuseHeader)) { /* extended header */
				offset = GET_HDR_OFFSET_2_0(efuseHeader);

				efuse_OneByteRead(padapter, eFuse_Addr++, &efuseExtHdr, bPseudoTest);
				if (ALL_WORDS_DISABLED(efuseExtHdr))
					continue;


				offset |= ((efuseExtHdr & 0xF0) >> 1);
				wden = (efuseExtHdr & 0x0F);
			} else {
				offset = ((efuseHeader >> 4) & 0x0f);
				wden = (efuseHeader & 0x0f);
			}

			if (offset < EFUSE_BT_MAX_SECTION) {
				u16 addr;

				addr = offset * PGPKT_DATA_SIZE;
				for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++) {
					/*  Check word enable condition in the section */
					if (!(wden & (0x01<<i))) {
						efuse_OneByteRead(padapter, eFuse_Addr++, &efuseData, bPseudoTest);
						efuseTbl[addr] = efuseData;

						efuse_OneByteRead(padapter, eFuse_Addr++, &efuseData, bPseudoTest);
						efuseTbl[addr+1] = efuseData;
					}
					addr += 2;
				}
			} else {
				eFuse_Addr += Efuse_CalculateWordCnts(wden)*2;
			}
		}

		if ((eFuse_Addr - 1) < total)
			break;

	}

	/*  switch bank back to bank 0 for later BT and wifi use. */
	hal_EfuseSwitchToBank(padapter, 0, bPseudoTest);

	/*  Copy from Efuse map to output pointer memory!!! */
	for (i = 0; i < _size_byte; i++)
		pbuf[i] = efuseTbl[_offset+i];

	/*  */
	/*  Calculate Efuse utilization. */
	/*  */
	EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, &total, bPseudoTest);
	used = (EFUSE_BT_REAL_BANK_CONTENT_LEN*(bank-1)) + eFuse_Addr - 1;
	efuse_usage = (u8)((used*100)/total);
	if (bPseudoTest) {
#ifdef HAL_EFUSE_MEMORY
		pEfuseHal->fakeBTEfuseUsedBytes = used;
#else
		fakeBTEfuseUsedBytes = used;
#endif
	} else {
		rtw_hal_set_hwreg(padapter, HW_VAR_EFUSE_BT_BYTES, (u8 *)&used);
		rtw_hal_set_hwreg(padapter, HW_VAR_EFUSE_BT_USAGE, (u8 *)&efuse_usage);
	}

exit:
	kfree(efuseTbl);
}

static void Hal_ReadEFuse(
	struct adapter *padapter,
	u8 efuseType,
	u16 _offset,
	u16 _size_byte,
	u8 *pbuf,
	bool bPseudoTest
)
{
	if (efuseType == EFUSE_WIFI)
		hal_ReadEFuse_WiFi(padapter, _offset, _size_byte, pbuf, bPseudoTest);
	else
		hal_ReadEFuse_BT(padapter, _offset, _size_byte, pbuf, bPseudoTest);
}

static u16 hal_EfuseGetCurrentSize_WiFi(
	struct adapter *padapter, bool bPseudoTest
)
{
#ifdef HAL_EFUSE_MEMORY
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	struct efuse_hal *pEfuseHal = &pHalData->EfuseHal;
#endif
	u16 efuse_addr = 0;
	u16 start_addr = 0; /*  for debug */
	u8 hoffset = 0, hworden = 0;
	u8 efuse_data, word_cnts = 0;
	u32 count = 0; /*  for debug */


	if (bPseudoTest) {
#ifdef HAL_EFUSE_MEMORY
		efuse_addr = (u16)pEfuseHal->fakeEfuseUsedBytes;
#else
		efuse_addr = (u16)fakeEfuseUsedBytes;
#endif
	} else
		rtw_hal_get_hwreg(padapter, HW_VAR_EFUSE_BYTES, (u8 *)&efuse_addr);

	start_addr = efuse_addr;

	/*  switch bank back to bank 0 for later BT and wifi use. */
	hal_EfuseSwitchToBank(padapter, 0, bPseudoTest);

	count = 0;
	while (AVAILABLE_EFUSE_ADDR(efuse_addr)) {
		if (efuse_OneByteRead(padapter, efuse_addr, &efuse_data, bPseudoTest) == false)
			goto error;

		if (efuse_data == 0xFF)
			break;

		if ((start_addr != 0) && (efuse_addr == start_addr)) {
			count++;

			efuse_data = 0xFF;
			if (count < 4) {
				/*  try again! */

				if (count > 2) {
					/*  try again form address 0 */
					efuse_addr = 0;
					start_addr = 0;
				}

				continue;
			}

			goto error;
		}

		if (EXT_HEADER(efuse_data)) {
			hoffset = GET_HDR_OFFSET_2_0(efuse_data);
			efuse_addr++;
			efuse_OneByteRead(padapter, efuse_addr, &efuse_data, bPseudoTest);
			if (ALL_WORDS_DISABLED(efuse_data))
				continue;

			hoffset |= ((efuse_data & 0xF0) >> 1);
			hworden = efuse_data & 0x0F;
		} else {
			hoffset = (efuse_data>>4) & 0x0F;
			hworden = efuse_data & 0x0F;
		}

		word_cnts = Efuse_CalculateWordCnts(hworden);
		efuse_addr += (word_cnts*2)+1;
	}

	if (bPseudoTest) {
#ifdef HAL_EFUSE_MEMORY
		pEfuseHal->fakeEfuseUsedBytes = efuse_addr;
#else
		fakeEfuseUsedBytes = efuse_addr;
#endif
	} else
		rtw_hal_set_hwreg(padapter, HW_VAR_EFUSE_BYTES, (u8 *)&efuse_addr);

	goto exit;

error:
	/*  report max size to prevent write efuse */
	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, &efuse_addr, bPseudoTest);

exit:

	return efuse_addr;
}

static u16 hal_EfuseGetCurrentSize_BT(struct adapter *padapter, u8 bPseudoTest)
{
#ifdef HAL_EFUSE_MEMORY
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	struct efuse_hal *pEfuseHal = &pHalData->EfuseHal;
#endif
	u16 btusedbytes;
	u16 efuse_addr;
	u8 bank, startBank;
	u8 hoffset = 0, hworden = 0;
	u8 efuse_data, word_cnts = 0;
	u16 retU2 = 0;

	if (bPseudoTest) {
#ifdef HAL_EFUSE_MEMORY
		btusedbytes = pEfuseHal->fakeBTEfuseUsedBytes;
#else
		btusedbytes = fakeBTEfuseUsedBytes;
#endif
	} else
		rtw_hal_get_hwreg(padapter, HW_VAR_EFUSE_BT_BYTES, (u8 *)&btusedbytes);

	efuse_addr = (u16)((btusedbytes%EFUSE_BT_REAL_BANK_CONTENT_LEN));
	startBank = (u8)(1+(btusedbytes/EFUSE_BT_REAL_BANK_CONTENT_LEN));

	EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_AVAILABLE_EFUSE_BYTES_BANK, &retU2, bPseudoTest);

	for (bank = startBank; bank < 3; bank++) {
		if (hal_EfuseSwitchToBank(padapter, bank, bPseudoTest) == false)
			/* bank = EFUSE_MAX_BANK; */
			break;

		/*  only when bank is switched we have to reset the efuse_addr. */
		if (bank != startBank)
			efuse_addr = 0;
#if 1

		while (AVAILABLE_EFUSE_ADDR(efuse_addr)) {
			if (efuse_OneByteRead(padapter, efuse_addr,
					      &efuse_data, bPseudoTest) == false)
				/* bank = EFUSE_MAX_BANK; */
				break;

			if (efuse_data == 0xFF)
				break;

			if (EXT_HEADER(efuse_data)) {
				hoffset = GET_HDR_OFFSET_2_0(efuse_data);
				efuse_addr++;
				efuse_OneByteRead(padapter, efuse_addr, &efuse_data, bPseudoTest);

				if (ALL_WORDS_DISABLED(efuse_data)) {
					efuse_addr++;
					continue;
				}

/* 				hoffset = ((hoffset & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1); */
				hoffset |= ((efuse_data & 0xF0) >> 1);
				hworden = efuse_data & 0x0F;
			} else {
				hoffset = (efuse_data>>4) & 0x0F;
				hworden =  efuse_data & 0x0F;
			}

			word_cnts = Efuse_CalculateWordCnts(hworden);
			/* read next header */
			efuse_addr += (word_cnts*2)+1;
		}
#else
	while (
		bContinual &&
		efuse_OneByteRead(padapter, efuse_addr, &efuse_data, bPseudoTest) &&
		AVAILABLE_EFUSE_ADDR(efuse_addr)
	) {
			if (efuse_data != 0xFF) {
				if ((efuse_data&0x1F) == 0x0F) { /* extended header */
					hoffset = efuse_data;
					efuse_addr++;
					efuse_OneByteRead(padapter, efuse_addr, &efuse_data, bPseudoTest);
					if ((efuse_data & 0x0F) == 0x0F) {
						efuse_addr++;
						continue;
					} else {
						hoffset = ((hoffset & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
						hworden = efuse_data & 0x0F;
					}
				} else {
					hoffset = (efuse_data>>4) & 0x0F;
					hworden =  efuse_data & 0x0F;
				}
				word_cnts = Efuse_CalculateWordCnts(hworden);
				/* read next header */
				efuse_addr = efuse_addr + (word_cnts*2)+1;
			} else
				bContinual = false;
		}
#endif


		/*  Check if we need to check next bank efuse */
		if (efuse_addr < retU2)
			break; /*  don't need to check next bank. */
	}

	retU2 = ((bank-1)*EFUSE_BT_REAL_BANK_CONTENT_LEN)+efuse_addr;
	if (bPseudoTest) {
		pEfuseHal->fakeBTEfuseUsedBytes = retU2;
	} else {
		pEfuseHal->BTEfuseUsedBytes = retU2;
	}

	return retU2;
}

static u16 Hal_EfuseGetCurrentSize(
	struct adapter *padapter, u8 efuseType, bool bPseudoTest
)
{
	u16 ret = 0;

	if (efuseType == EFUSE_WIFI)
		ret = hal_EfuseGetCurrentSize_WiFi(padapter, bPseudoTest);
	else
		ret = hal_EfuseGetCurrentSize_BT(padapter, bPseudoTest);

	return ret;
}

static u8 Hal_EfuseWordEnableDataWrite(
	struct adapter *padapter,
	u16 efuse_addr,
	u8 word_en,
	u8 *data,
	bool bPseudoTest
)
{
	u16 tmpaddr = 0;
	u16 start_addr = efuse_addr;
	u8 badworden = 0x0F;
	u8 tmpdata[PGPKT_DATA_SIZE];

	memset(tmpdata, 0xFF, PGPKT_DATA_SIZE);

	if (!(word_en & BIT(0))) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(padapter, start_addr++, data[0], bPseudoTest);
		efuse_OneByteWrite(padapter, start_addr++, data[1], bPseudoTest);

		efuse_OneByteRead(padapter, tmpaddr, &tmpdata[0], bPseudoTest);
		efuse_OneByteRead(padapter, tmpaddr+1, &tmpdata[1], bPseudoTest);
		if ((data[0] != tmpdata[0]) || (data[1] != tmpdata[1])) {
			badworden &= (~BIT(0));
		}
	}
	if (!(word_en & BIT(1))) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(padapter, start_addr++, data[2], bPseudoTest);
		efuse_OneByteWrite(padapter, start_addr++, data[3], bPseudoTest);

		efuse_OneByteRead(padapter, tmpaddr, &tmpdata[2], bPseudoTest);
		efuse_OneByteRead(padapter, tmpaddr+1, &tmpdata[3], bPseudoTest);
		if ((data[2] != tmpdata[2]) || (data[3] != tmpdata[3])) {
			badworden &= (~BIT(1));
		}
	}

	if (!(word_en & BIT(2))) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(padapter, start_addr++, data[4], bPseudoTest);
		efuse_OneByteWrite(padapter, start_addr++, data[5], bPseudoTest);

		efuse_OneByteRead(padapter, tmpaddr, &tmpdata[4], bPseudoTest);
		efuse_OneByteRead(padapter, tmpaddr+1, &tmpdata[5], bPseudoTest);
		if ((data[4] != tmpdata[4]) || (data[5] != tmpdata[5])) {
			badworden &= (~BIT(2));
		}
	}

	if (!(word_en & BIT(3))) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(padapter, start_addr++, data[6], bPseudoTest);
		efuse_OneByteWrite(padapter, start_addr++, data[7], bPseudoTest);

		efuse_OneByteRead(padapter, tmpaddr, &tmpdata[6], bPseudoTest);
		efuse_OneByteRead(padapter, tmpaddr+1, &tmpdata[7], bPseudoTest);
		if ((data[6] != tmpdata[6]) || (data[7] != tmpdata[7])) {
			badworden &= (~BIT(3));
		}
	}

	return badworden;
}

static s32 Hal_EfusePgPacketRead(
	struct adapter *padapter,
	u8 offset,
	u8 *data,
	bool bPseudoTest
)
{
	u8 efuse_data, word_cnts = 0;
	u16 efuse_addr = 0;
	u8 hoffset = 0, hworden = 0;
	u8 i;
	u8 max_section = 0;
	s32	ret;


	if (!data)
		return false;

	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_EFUSE_MAX_SECTION, &max_section, bPseudoTest);
	if (offset > max_section)
		return false;

	memset(data, 0xFF, PGPKT_DATA_SIZE);
	ret = true;

	/*  */
	/*  <Roger_TODO> Efuse has been pre-programmed dummy 5Bytes at the end of Efuse by CP. */
	/*  Skip dummy parts to prevent unexpected data read from Efuse. */
	/*  By pass right now. 2009.02.19. */
	/*  */
	while (AVAILABLE_EFUSE_ADDR(efuse_addr)) {
		if (efuse_OneByteRead(padapter, efuse_addr++, &efuse_data, bPseudoTest) == false) {
			ret = false;
			break;
		}

		if (efuse_data == 0xFF)
			break;

		if (EXT_HEADER(efuse_data)) {
			hoffset = GET_HDR_OFFSET_2_0(efuse_data);
			efuse_OneByteRead(padapter, efuse_addr++, &efuse_data, bPseudoTest);
			if (ALL_WORDS_DISABLED(efuse_data))
				continue;

			hoffset |= ((efuse_data & 0xF0) >> 1);
			hworden = efuse_data & 0x0F;
		} else {
			hoffset = (efuse_data>>4) & 0x0F;
			hworden =  efuse_data & 0x0F;
		}

		if (hoffset == offset) {
			for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++) {
				/*  Check word enable condition in the section */
				if (!(hworden & (0x01<<i))) {
					efuse_OneByteRead(padapter, efuse_addr++, &efuse_data, bPseudoTest);
					data[i*2] = efuse_data;

					efuse_OneByteRead(padapter, efuse_addr++, &efuse_data, bPseudoTest);
					data[(i*2)+1] = efuse_data;
				}
			}
		} else {
			word_cnts = Efuse_CalculateWordCnts(hworden);
			efuse_addr += word_cnts*2;
		}
	}

	return ret;
}

static u8 hal_EfusePgCheckAvailableAddr(
	struct adapter *padapter, u8 efuseType, u8 bPseudoTest
)
{
	u16 max_available = 0;
	u16 current_size;


	EFUSE_GetEfuseDefinition(padapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, &max_available, bPseudoTest);

	current_size = Efuse_GetCurrentSize(padapter, efuseType, bPseudoTest);
	if (current_size >= max_available)
		return false;

	return true;
}

static void hal_EfuseConstructPGPkt(
	u8 offset,
	u8 word_en,
	u8 *pData,
	struct pgpkt_struct *pTargetPkt
)
{
	memset(pTargetPkt->data, 0xFF, PGPKT_DATA_SIZE);
	pTargetPkt->offset = offset;
	pTargetPkt->word_en = word_en;
	efuse_WordEnableDataRead(word_en, pData, pTargetPkt->data);
	pTargetPkt->word_cnts = Efuse_CalculateWordCnts(pTargetPkt->word_en);
}

static u8 hal_EfusePartialWriteCheck(
	struct adapter *padapter,
	u8 efuseType,
	u16 *pAddr,
	struct pgpkt_struct *pTargetPkt,
	u8 bPseudoTest
)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	struct efuse_hal *pEfuseHal = &pHalData->EfuseHal;
	u8 bRet = false;
	u16 startAddr = 0, efuse_max_available_len = 0, efuse_max = 0;
	u8 efuse_data = 0;

	EFUSE_GetEfuseDefinition(padapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, &efuse_max_available_len, bPseudoTest);
	EFUSE_GetEfuseDefinition(padapter, efuseType, TYPE_EFUSE_CONTENT_LEN_BANK, &efuse_max, bPseudoTest);

	if (efuseType == EFUSE_WIFI) {
		if (bPseudoTest) {
#ifdef HAL_EFUSE_MEMORY
			startAddr = (u16)pEfuseHal->fakeEfuseUsedBytes;
#else
			startAddr = (u16)fakeEfuseUsedBytes;
#endif
		} else
			rtw_hal_get_hwreg(padapter, HW_VAR_EFUSE_BYTES, (u8 *)&startAddr);
	} else {
		if (bPseudoTest) {
#ifdef HAL_EFUSE_MEMORY
			startAddr = (u16)pEfuseHal->fakeBTEfuseUsedBytes;
#else
			startAddr = (u16)fakeBTEfuseUsedBytes;
#endif
		} else
			rtw_hal_get_hwreg(padapter, HW_VAR_EFUSE_BT_BYTES, (u8 *)&startAddr);
	}
	startAddr %= efuse_max;

	while (1) {
		if (startAddr >= efuse_max_available_len) {
			bRet = false;
			break;
		}

		if (efuse_OneByteRead(padapter, startAddr, &efuse_data, bPseudoTest) && (efuse_data != 0xFF)) {
#if 1
			bRet = false;
			break;
#else
			if (EXT_HEADER(efuse_data)) {
				cur_header = efuse_data;
				startAddr++;
				efuse_OneByteRead(padapter, startAddr, &efuse_data, bPseudoTest);
				if (ALL_WORDS_DISABLED(efuse_data)) {
					bRet = false;
					break;
				} else {
					curPkt.offset = ((cur_header & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
					curPkt.word_en = efuse_data & 0x0F;
				}
			} else {
				cur_header  =  efuse_data;
				curPkt.offset = (cur_header>>4) & 0x0F;
				curPkt.word_en = cur_header & 0x0F;
			}

			curPkt.word_cnts = Efuse_CalculateWordCnts(curPkt.word_en);
			/*  if same header is found but no data followed */
			/*  write some part of data followed by the header. */
			if (
				(curPkt.offset == pTargetPkt->offset) &&
				(hal_EfuseCheckIfDatafollowed(padapter, curPkt.word_cnts, startAddr+1, bPseudoTest) == false) &&
				wordEnMatched(pTargetPkt, &curPkt, &matched_wden) == true
			) {
				/*  Here to write partial data */
				badworden = Efuse_WordEnableDataWrite(padapter, startAddr+1, matched_wden, pTargetPkt->data, bPseudoTest);
				if (badworden != 0x0F) {
					u32 PgWriteSuccess = 0;
					/*  if write fail on some words, write these bad words again */
					if (efuseType == EFUSE_WIFI)
						PgWriteSuccess = Efuse_PgPacketWrite(padapter, pTargetPkt->offset, badworden, pTargetPkt->data, bPseudoTest);
					else
						PgWriteSuccess = Efuse_PgPacketWrite_BT(padapter, pTargetPkt->offset, badworden, pTargetPkt->data, bPseudoTest);

					if (!PgWriteSuccess) {
						bRet = false;	/*  write fail, return */
						break;
					}
				}
				/*  partial write ok, update the target packet for later use */
				for (i = 0; i < 4; i++) {
					if ((matched_wden & (0x1<<i)) == 0) { /*  this word has been written */
						pTargetPkt->word_en |= (0x1<<i);	/*  disable the word */
					}
				}
				pTargetPkt->word_cnts = Efuse_CalculateWordCnts(pTargetPkt->word_en);
			}
			/*  read from next header */
			startAddr = startAddr + (curPkt.word_cnts*2) + 1;
#endif
		} else {
			/*  not used header, 0xff */
			*pAddr = startAddr;
			bRet = true;
			break;
		}
	}

	return bRet;
}

static u8 hal_EfusePgPacketWrite1ByteHeader(
	struct adapter *padapter,
	u8 efuseType,
	u16 *pAddr,
	struct pgpkt_struct *pTargetPkt,
	u8 bPseudoTest
)
{
	u8 pg_header = 0, tmp_header = 0;
	u16 efuse_addr = *pAddr;
	u8 repeatcnt = 0;

	pg_header = ((pTargetPkt->offset << 4) & 0xf0) | pTargetPkt->word_en;

	do {
		efuse_OneByteWrite(padapter, efuse_addr, pg_header, bPseudoTest);
		efuse_OneByteRead(padapter, efuse_addr, &tmp_header, bPseudoTest);
		if (tmp_header != 0xFF)
			break;
		if (repeatcnt++ > EFUSE_REPEAT_THRESHOLD_)
			return false;

	} while (1);

	if (tmp_header != pg_header)
		return false;

	*pAddr = efuse_addr;

	return true;
}

static u8 hal_EfusePgPacketWrite2ByteHeader(
	struct adapter *padapter,
	u8 efuseType,
	u16 *pAddr,
	struct pgpkt_struct *pTargetPkt,
	u8 bPseudoTest
)
{
	u16 efuse_addr, efuse_max_available_len = 0;
	u8 pg_header = 0, tmp_header = 0;
	u8 repeatcnt = 0;

	EFUSE_GetEfuseDefinition(padapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_BANK, &efuse_max_available_len, bPseudoTest);

	efuse_addr = *pAddr;
	if (efuse_addr >= efuse_max_available_len)
		return false;

	pg_header = ((pTargetPkt->offset & 0x07) << 5) | 0x0F;

	do {
		efuse_OneByteWrite(padapter, efuse_addr, pg_header, bPseudoTest);
		efuse_OneByteRead(padapter, efuse_addr, &tmp_header, bPseudoTest);
		if (tmp_header != 0xFF)
			break;
		if (repeatcnt++ > EFUSE_REPEAT_THRESHOLD_)
			return false;

	} while (1);

	if (tmp_header != pg_header)
		return false;

	/*  to write ext_header */
	efuse_addr++;
	pg_header = ((pTargetPkt->offset & 0x78) << 1) | pTargetPkt->word_en;

	do {
		efuse_OneByteWrite(padapter, efuse_addr, pg_header, bPseudoTest);
		efuse_OneByteRead(padapter, efuse_addr, &tmp_header, bPseudoTest);
		if (tmp_header != 0xFF)
			break;
		if (repeatcnt++ > EFUSE_REPEAT_THRESHOLD_)
			return false;

	} while (1);

	if (tmp_header != pg_header) /* offset PG fail */
		return false;

	*pAddr = efuse_addr;

	return true;
}

static u8 hal_EfusePgPacketWriteHeader(
	struct adapter *padapter,
	u8 efuseType,
	u16 *pAddr,
	struct pgpkt_struct *pTargetPkt,
	u8 bPseudoTest
)
{
	u8 bRet = false;

	if (pTargetPkt->offset >= EFUSE_MAX_SECTION_BASE)
		bRet = hal_EfusePgPacketWrite2ByteHeader(padapter, efuseType, pAddr, pTargetPkt, bPseudoTest);
	else
		bRet = hal_EfusePgPacketWrite1ByteHeader(padapter, efuseType, pAddr, pTargetPkt, bPseudoTest);

	return bRet;
}

static u8 hal_EfusePgPacketWriteData(
	struct adapter *padapter,
	u8 efuseType,
	u16 *pAddr,
	struct pgpkt_struct *pTargetPkt,
	u8 bPseudoTest
)
{
	u16 efuse_addr;
	u8 badworden;


	efuse_addr = *pAddr;
	badworden = Efuse_WordEnableDataWrite(padapter, efuse_addr+1, pTargetPkt->word_en, pTargetPkt->data, bPseudoTest);
	if (badworden != 0x0F)
		return false;

	return true;
}

static s32 Hal_EfusePgPacketWrite(
	struct adapter *padapter,
	u8 offset,
	u8 word_en,
	u8 *pData,
	bool bPseudoTest
)
{
	struct pgpkt_struct targetPkt;
	u16 startAddr = 0;
	u8 efuseType = EFUSE_WIFI;

	if (!hal_EfusePgCheckAvailableAddr(padapter, efuseType, bPseudoTest))
		return false;

	hal_EfuseConstructPGPkt(offset, word_en, pData, &targetPkt);

	if (!hal_EfusePartialWriteCheck(padapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return false;

	if (!hal_EfusePgPacketWriteHeader(padapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return false;

	if (!hal_EfusePgPacketWriteData(padapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return false;

	return true;
}

static bool Hal_EfusePgPacketWrite_BT(
	struct adapter *padapter,
	u8 offset,
	u8 word_en,
	u8 *pData,
	bool bPseudoTest
)
{
	struct pgpkt_struct targetPkt;
	u16 startAddr = 0;
	u8 efuseType = EFUSE_BT;

	if (!hal_EfusePgCheckAvailableAddr(padapter, efuseType, bPseudoTest))
		return false;

	hal_EfuseConstructPGPkt(offset, word_en, pData, &targetPkt);

	if (!hal_EfusePartialWriteCheck(padapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return false;

	if (!hal_EfusePgPacketWriteHeader(padapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return false;

	if (!hal_EfusePgPacketWriteData(padapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return false;

	return true;
}

static struct hal_version ReadChipVersion8723B(struct adapter *padapter)
{
	u32 value32;
	struct hal_version ChipVersion;
	struct hal_com_data *pHalData;

/* YJ, TODO, move read chip type here */
	pHalData = GET_HAL_DATA(padapter);

	value32 = rtw_read32(padapter, REG_SYS_CFG);
	ChipVersion.ICType = CHIP_8723B;
	ChipVersion.ChipType = ((value32 & RTL_ID) ? TEST_CHIP : NORMAL_CHIP);
	ChipVersion.VendorType = ((value32 & VENDOR_ID) ? CHIP_VENDOR_UMC : CHIP_VENDOR_TSMC);
	ChipVersion.CUTVersion = (value32 & CHIP_VER_RTL_MASK)>>CHIP_VER_RTL_SHIFT; /*  IC version (CUT) */

	/*  For regulator mode. by tynli. 2011.01.14 */
	pHalData->RegulatorMode = ((value32 & SPS_SEL) ? RT_LDO_REGULATOR : RT_SWITCHING_REGULATOR);

	value32 = rtw_read32(padapter, REG_GPIO_OUTSTS);
	ChipVersion.ROMVer = ((value32 & RF_RL_ID) >> 20);	/*  ROM code version. */

	/*  For multi-function consideration. Added by Roger, 2010.10.06. */
	pHalData->MultiFunc = RT_MULTI_FUNC_NONE;
	value32 = rtw_read32(padapter, REG_MULTI_FUNC_CTRL);
	pHalData->MultiFunc |= ((value32 & WL_FUNC_EN) ? RT_MULTI_FUNC_WIFI : 0);
	pHalData->MultiFunc |= ((value32 & BT_FUNC_EN) ? RT_MULTI_FUNC_BT : 0);
	pHalData->MultiFunc |= ((value32 & GPS_FUNC_EN) ? RT_MULTI_FUNC_GPS : 0);
	pHalData->PolarityCtl = ((value32 & WL_HWPDN_SL) ? RT_POLARITY_HIGH_ACT : RT_POLARITY_LOW_ACT);
#if 1
	dump_chip_info(ChipVersion);
#endif
	pHalData->VersionID = ChipVersion;

	return ChipVersion;
}

static void rtl8723b_read_chip_version(struct adapter *padapter)
{
	ReadChipVersion8723B(padapter);
}

void rtl8723b_InitBeaconParameters(struct adapter *padapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	u16 val16;
	u8 val8;


	val8 = DIS_TSF_UDT;
	val16 = val8 | (val8 << 8); /*  port0 and port1 */

	/*  Enable prot0 beacon function for PSTDMA */
	val16 |= EN_BCN_FUNCTION;

	rtw_write16(padapter, REG_BCN_CTRL, val16);

	/*  TODO: Remove these magic number */
	rtw_write16(padapter, REG_TBTT_PROHIBIT, 0x6404);/*  ms */
	/*  Firmware will control REG_DRVERLYINT when power saving is enable, */
	/*  so don't set this register on STA mode. */
	if (check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE) == false)
		rtw_write8(padapter, REG_DRVERLYINT, DRIVER_EARLY_INT_TIME_8723B); /*  5ms */
	rtw_write8(padapter, REG_BCNDMATIM, BCN_DMA_ATIME_INT_TIME_8723B); /*  2ms */

	/*  Suggested by designer timchen. Change beacon AIFS to the largest number */
	/*  beacause test chip does not contension before sending beacon. by tynli. 2009.11.03 */
	rtw_write16(padapter, REG_BCNTCFG, 0x660F);

	pHalData->RegBcnCtrlVal = rtw_read8(padapter, REG_BCN_CTRL);
	pHalData->RegTxPause = rtw_read8(padapter, REG_TXPAUSE);
	pHalData->RegFwHwTxQCtrl = rtw_read8(padapter, REG_FWHW_TXQ_CTRL+2);
	pHalData->RegReg542 = rtw_read8(padapter, REG_TBTT_PROHIBIT+2);
	pHalData->RegCR_1 = rtw_read8(padapter, REG_CR+1);
}

void _InitBurstPktLen_8723BS(struct adapter *Adapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);

	rtw_write8(Adapter, 0x4c7, rtw_read8(Adapter, 0x4c7)|BIT(7)); /* enable single pkt ampdu */
	rtw_write8(Adapter, REG_RX_PKT_LIMIT_8723B, 0x18);		/* for VHT packet length 11K */
	rtw_write8(Adapter, REG_MAX_AGGR_NUM_8723B, 0x1F);
	rtw_write8(Adapter, REG_PIFS_8723B, 0x00);
	rtw_write8(Adapter, REG_FWHW_TXQ_CTRL_8723B, rtw_read8(Adapter, REG_FWHW_TXQ_CTRL)&(~BIT(7)));
	if (pHalData->AMPDUBurstMode)
		rtw_write8(Adapter, REG_AMPDU_BURST_MODE_8723B,  0x5F);
	rtw_write8(Adapter, REG_AMPDU_MAX_TIME_8723B, 0x70);

	/*  ARFB table 9 for 11ac 5G 2SS */
	rtw_write32(Adapter, REG_ARFR0_8723B, 0x00000010);
	if (IS_NORMAL_CHIP(pHalData->VersionID))
		rtw_write32(Adapter, REG_ARFR0_8723B+4, 0xfffff000);
	else
		rtw_write32(Adapter, REG_ARFR0_8723B+4, 0x3e0ff000);

	/*  ARFB table 10 for 11ac 5G 1SS */
	rtw_write32(Adapter, REG_ARFR1_8723B, 0x00000010);
	rtw_write32(Adapter, REG_ARFR1_8723B+4, 0x003ff000);
}

static void ResumeTxBeacon(struct adapter *padapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);

	pHalData->RegFwHwTxQCtrl |= BIT(6);
	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+1, 0xff);
	pHalData->RegReg542 |= BIT(0);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+2, pHalData->RegReg542);
}

static void StopTxBeacon(struct adapter *padapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);

	pHalData->RegFwHwTxQCtrl &= ~BIT(6);
	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+1, 0x64);
	pHalData->RegReg542 &= ~BIT(0);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+2, pHalData->RegReg542);

	CheckFwRsvdPageContent(padapter);  /*  2010.06.23. Added by tynli. */
}

static void _BeaconFunctionEnable(struct adapter *padapter, u8 Enable, u8 Linked)
{
	rtw_write8(padapter, REG_BCN_CTRL, DIS_TSF_UDT | EN_BCN_FUNCTION | DIS_BCNQ_SUB);
	rtw_write8(padapter, REG_RD_CTRL+1, 0x6F);
}

static void rtl8723b_SetBeaconRelatedRegisters(struct adapter *padapter)
{
	u8 val8;
	u32 value32;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	u32 bcn_ctrl_reg;

	/* reset TSF, enable update TSF, correcting TSF On Beacon */

	/* REG_BCN_INTERVAL */
	/* REG_BCNDMATIM */
	/* REG_ATIMWND */
	/* REG_TBTT_PROHIBIT */
	/* REG_DRVERLYINT */
	/* REG_BCN_MAX_ERR */
	/* REG_BCNTCFG (0x510) */
	/* REG_DUAL_TSF_RST */
	/* REG_BCN_CTRL (0x550) */


	bcn_ctrl_reg = REG_BCN_CTRL;

	/*  */
	/*  ATIM window */
	/*  */
	rtw_write16(padapter, REG_ATIMWND, 2);

	/*  */
	/*  Beacon interval (in unit of TU). */
	/*  */
	rtw_write16(padapter, REG_BCN_INTERVAL, pmlmeinfo->bcn_interval);

	rtl8723b_InitBeaconParameters(padapter);

	rtw_write8(padapter, REG_SLOT, 0x09);

	/*  */
	/*  Reset TSF Timer to zero, added by Roger. 2008.06.24 */
	/*  */
	value32 = rtw_read32(padapter, REG_TCR);
	value32 &= ~TSFRST;
	rtw_write32(padapter, REG_TCR, value32);

	value32 |= TSFRST;
	rtw_write32(padapter, REG_TCR, value32);

	/*  NOTE: Fix test chip's bug (about contention windows's randomness) */
	if (check_fwstate(&padapter->mlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE|WIFI_AP_STATE) == true) {
		rtw_write8(padapter, REG_RXTSF_OFFSET_CCK, 0x50);
		rtw_write8(padapter, REG_RXTSF_OFFSET_OFDM, 0x50);
	}

	_BeaconFunctionEnable(padapter, true, true);

	ResumeTxBeacon(padapter);
	val8 = rtw_read8(padapter, bcn_ctrl_reg);
	val8 |= DIS_BCNQ_SUB;
	rtw_write8(padapter, bcn_ctrl_reg, val8);
}

static void rtl8723b_GetHalODMVar(
	struct adapter *Adapter,
	enum hal_odm_variable eVariable,
	void *pValue1,
	void *pValue2
)
{
	GetHalODMVar(Adapter, eVariable, pValue1, pValue2);
}

static void rtl8723b_SetHalODMVar(
	struct adapter *Adapter,
	enum hal_odm_variable eVariable,
	void *pValue1,
	bool bSet
)
{
	SetHalODMVar(Adapter, eVariable, pValue1, bSet);
}

static void hal_notch_filter_8723b(struct adapter *adapter, bool enable)
{
	if (enable)
		rtw_write8(adapter, rOFDM0_RxDSP+1, rtw_read8(adapter, rOFDM0_RxDSP+1) | BIT1);
	else
		rtw_write8(adapter, rOFDM0_RxDSP+1, rtw_read8(adapter, rOFDM0_RxDSP+1) & ~BIT1);
}

static void UpdateHalRAMask8723B(struct adapter *padapter, u32 mac_id, u8 rssi_level)
{
	u32 mask, rate_bitmap;
	u8 shortGIrate = false;
	struct sta_info *psta;
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	struct dm_priv *pdmpriv = &pHalData->dmpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	if (mac_id >= NUM_STA) /* CAM_SIZE */
		return;

	psta = pmlmeinfo->FW_sta_info[mac_id].psta;
	if (!psta)
		return;

	shortGIrate = query_ra_short_GI(psta);

	mask = psta->ra_mask;

	rate_bitmap = 0xffffffff;
	rate_bitmap = ODM_Get_Rate_Bitmap(&pHalData->odmpriv, mac_id, mask, rssi_level);

	mask &= rate_bitmap;

	rate_bitmap = hal_btcoex_GetRaMask(padapter);
	mask &= ~rate_bitmap;

	if (pHalData->fw_ractrl) {
		rtl8723b_set_FwMacIdConfig_cmd(padapter, mac_id, psta->raid, psta->bw_mode, shortGIrate, mask);
	}

	/* set correct initial date rate for each mac_id */
	pdmpriv->INIDATA_RATE[mac_id] = psta->init_rate;
}


void rtl8723b_set_hal_ops(struct hal_ops *pHalFunc)
{
	pHalFunc->free_hal_data = &rtl8723b_free_hal_data;

	pHalFunc->dm_init = &rtl8723b_init_dm_priv;

	pHalFunc->read_chip_version = &rtl8723b_read_chip_version;

	pHalFunc->UpdateRAMaskHandler = &UpdateHalRAMask8723B;

	pHalFunc->set_bwmode_handler = &PHY_SetBWMode8723B;
	pHalFunc->set_channel_handler = &PHY_SwChnl8723B;
	pHalFunc->set_chnl_bw_handler = &PHY_SetSwChnlBWMode8723B;

	pHalFunc->set_tx_power_level_handler = &PHY_SetTxPowerLevel8723B;
	pHalFunc->get_tx_power_level_handler = &PHY_GetTxPowerLevel8723B;

	pHalFunc->hal_dm_watchdog = &rtl8723b_HalDmWatchDog;
	pHalFunc->hal_dm_watchdog_in_lps = &rtl8723b_HalDmWatchDog_in_LPS;


	pHalFunc->SetBeaconRelatedRegistersHandler = &rtl8723b_SetBeaconRelatedRegisters;

	pHalFunc->Add_RateATid = &rtl8723b_Add_RateATid;

	pHalFunc->run_thread = &rtl8723b_start_thread;
	pHalFunc->cancel_thread = &rtl8723b_stop_thread;

	pHalFunc->read_bbreg = &PHY_QueryBBReg_8723B;
	pHalFunc->write_bbreg = &PHY_SetBBReg_8723B;
	pHalFunc->read_rfreg = &PHY_QueryRFReg_8723B;
	pHalFunc->write_rfreg = &PHY_SetRFReg_8723B;

	/*  Efuse related function */
	pHalFunc->BTEfusePowerSwitch = &Hal_BT_EfusePowerSwitch;
	pHalFunc->EfusePowerSwitch = &Hal_EfusePowerSwitch;
	pHalFunc->ReadEFuse = &Hal_ReadEFuse;
	pHalFunc->EFUSEGetEfuseDefinition = &Hal_GetEfuseDefinition;
	pHalFunc->EfuseGetCurrentSize = &Hal_EfuseGetCurrentSize;
	pHalFunc->Efuse_PgPacketRead = &Hal_EfusePgPacketRead;
	pHalFunc->Efuse_PgPacketWrite = &Hal_EfusePgPacketWrite;
	pHalFunc->Efuse_WordEnableDataWrite = &Hal_EfuseWordEnableDataWrite;
	pHalFunc->Efuse_PgPacketWrite_BT = &Hal_EfusePgPacketWrite_BT;

	pHalFunc->GetHalODMVarHandler = &rtl8723b_GetHalODMVar;
	pHalFunc->SetHalODMVarHandler = &rtl8723b_SetHalODMVar;

	pHalFunc->xmit_thread_handler = &hal_xmit_handler;
	pHalFunc->hal_notch_filter = &hal_notch_filter_8723b;

	pHalFunc->c2h_handler = c2h_handler_8723b;
	pHalFunc->c2h_id_filter_ccx = c2h_id_filter_ccx_8723b;

	pHalFunc->fill_h2c_cmd = &FillH2CCmd8723B;
}

void rtl8723b_InitAntenna_Selection(struct adapter *padapter)
{
	u8 val;

	val = rtw_read8(padapter, REG_LEDCFG2);
	/*  Let 8051 take control antenna setting */
	val |= BIT(7); /*  DPDT_SEL_EN, 0x4C[23] */
	rtw_write8(padapter, REG_LEDCFG2, val);
}

void rtl8723b_init_default_value(struct adapter *padapter)
{
	struct hal_com_data *pHalData;
	struct dm_priv *pdmpriv;
	u8 i;


	pHalData = GET_HAL_DATA(padapter);
	pdmpriv = &pHalData->dmpriv;

	padapter->registrypriv.wireless_mode = WIRELESS_11BG_24N;

	/*  init default value */
	pHalData->fw_ractrl = false;
	pHalData->bIQKInitialized = false;
	if (!adapter_to_pwrctl(padapter)->bkeepfwalive)
		pHalData->LastHMEBoxNum = 0;

	pHalData->bIQKInitialized = false;

	/*  init dm default value */
	pdmpriv->TM_Trigger = 0;/* for IQK */
/* 	pdmpriv->binitialized = false; */
/* 	pdmpriv->prv_traffic_idx = 3; */
/* 	pdmpriv->initialize = 0; */

	pdmpriv->ThermalValue_HP_index = 0;
	for (i = 0; i < HP_THERMAL_NUM; i++)
		pdmpriv->ThermalValue_HP[i] = 0;

	/*  init Efuse variables */
	pHalData->EfuseUsedBytes = 0;
	pHalData->EfuseUsedPercentage = 0;
#ifdef HAL_EFUSE_MEMORY
	pHalData->EfuseHal.fakeEfuseBank = 0;
	pHalData->EfuseHal.fakeEfuseUsedBytes = 0;
	memset(pHalData->EfuseHal.fakeEfuseContent, 0xFF, EFUSE_MAX_HW_SIZE);
	memset(pHalData->EfuseHal.fakeEfuseInitMap, 0xFF, EFUSE_MAX_MAP_LEN);
	memset(pHalData->EfuseHal.fakeEfuseModifiedMap, 0xFF, EFUSE_MAX_MAP_LEN);
	pHalData->EfuseHal.BTEfuseUsedBytes = 0;
	pHalData->EfuseHal.BTEfuseUsedPercentage = 0;
	memset(pHalData->EfuseHal.BTEfuseContent, 0xFF, EFUSE_MAX_BT_BANK*EFUSE_MAX_HW_SIZE);
	memset(pHalData->EfuseHal.BTEfuseInitMap, 0xFF, EFUSE_BT_MAX_MAP_LEN);
	memset(pHalData->EfuseHal.BTEfuseModifiedMap, 0xFF, EFUSE_BT_MAX_MAP_LEN);
	pHalData->EfuseHal.fakeBTEfuseUsedBytes = 0;
	memset(pHalData->EfuseHal.fakeBTEfuseContent, 0xFF, EFUSE_MAX_BT_BANK*EFUSE_MAX_HW_SIZE);
	memset(pHalData->EfuseHal.fakeBTEfuseInitMap, 0xFF, EFUSE_BT_MAX_MAP_LEN);
	memset(pHalData->EfuseHal.fakeBTEfuseModifiedMap, 0xFF, EFUSE_BT_MAX_MAP_LEN);
#endif
}

u8 GetEEPROMSize8723B(struct adapter *padapter)
{
	u8 size = 0;
	u32 cr;

	cr = rtw_read16(padapter, REG_9346CR);
	/*  6: EEPROM used is 93C46, 4: boot from E-Fuse. */
	size = (cr & BOOT_FROM_EEPROM) ? 6 : 4;

	return size;
}

/*  */
/*  */
/*  LLT R/W/Init function */
/*  */
/*  */
s32 rtl8723b_InitLLTTable(struct adapter *padapter)
{
	unsigned long start, passing_time;
	u32 val32;
	s32 ret;


	ret = _FAIL;

	val32 = rtw_read32(padapter, REG_AUTO_LLT);
	val32 |= BIT_AUTO_INIT_LLT;
	rtw_write32(padapter, REG_AUTO_LLT, val32);

	start = jiffies;

	do {
		val32 = rtw_read32(padapter, REG_AUTO_LLT);
		if (!(val32 & BIT_AUTO_INIT_LLT)) {
			ret = _SUCCESS;
			break;
		}

		passing_time = jiffies_to_msecs(jiffies - start);
		if (passing_time > 1000)
			break;

		msleep(1);
	} while (1);

	return ret;
}

static void hal_get_chnl_group_8723b(u8 channel, u8 *group)
{
	if (1  <= channel && channel <= 2)
		*group = 0;
	else if (3  <= channel && channel <= 5)
		*group = 1;
	else if (6  <= channel && channel <= 8)
		*group = 2;
	else if (9  <= channel && channel <= 11)
		*group = 3;
	else if (12 <= channel && channel <= 14)
		*group = 4;
}

void Hal_InitPGData(struct adapter *padapter, u8 *PROMContent)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);

	if (!pEEPROM->bautoload_fail_flag) { /*  autoload OK. */
		if (!pEEPROM->EepromOrEfuse) {
			/*  Read EFUSE real map to shadow. */
			EFUSE_ShadowMapUpdate(padapter, EFUSE_WIFI, false);
			memcpy((void *)PROMContent, (void *)pEEPROM->efuse_eeprom_data, HWSET_MAX_SIZE_8723B);
		}
	} else {/* autoload fail */
		if (!pEEPROM->EepromOrEfuse)
			EFUSE_ShadowMapUpdate(padapter, EFUSE_WIFI, false);
		memcpy((void *)PROMContent, (void *)pEEPROM->efuse_eeprom_data, HWSET_MAX_SIZE_8723B);
	}
}

void Hal_EfuseParseIDCode(struct adapter *padapter, u8 *hwinfo)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
/* 	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter); */
	u16 EEPROMId;


	/*  Checl 0x8129 again for making sure autoload status!! */
	EEPROMId = le16_to_cpu(*((__le16 *)hwinfo));
	if (EEPROMId != RTL_EEPROM_ID) {
		pEEPROM->bautoload_fail_flag = true;
	} else
		pEEPROM->bautoload_fail_flag = false;
}

static void Hal_ReadPowerValueFromPROM_8723B(
	struct adapter *Adapter,
	struct TxPowerInfo24G *pwrInfo24G,
	u8 *PROMContent,
	bool AutoLoadFail
)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);
	u32 rfPath, eeAddr = EEPROM_TX_PWR_INX_8723B, group, TxCount = 0;

	memset(pwrInfo24G, 0, sizeof(struct TxPowerInfo24G));

	if (0xFF == PROMContent[eeAddr+1])
		AutoLoadFail = true;

	if (AutoLoadFail) {
		for (rfPath = 0; rfPath < MAX_RF_PATH; rfPath++) {
			/* 2.4G default value */
			for (group = 0; group < MAX_CHNL_GROUP_24G; group++) {
				pwrInfo24G->IndexCCK_Base[rfPath][group] = EEPROM_DEFAULT_24G_INDEX;
				pwrInfo24G->IndexBW40_Base[rfPath][group] = EEPROM_DEFAULT_24G_INDEX;
			}

			for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {
				if (TxCount == 0) {
					pwrInfo24G->BW20_Diff[rfPath][0] = EEPROM_DEFAULT_24G_HT20_DIFF;
					pwrInfo24G->OFDM_Diff[rfPath][0] = EEPROM_DEFAULT_24G_OFDM_DIFF;
				} else {
					pwrInfo24G->BW20_Diff[rfPath][TxCount] = EEPROM_DEFAULT_DIFF;
					pwrInfo24G->BW40_Diff[rfPath][TxCount] = EEPROM_DEFAULT_DIFF;
					pwrInfo24G->CCK_Diff[rfPath][TxCount] = EEPROM_DEFAULT_DIFF;
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] = EEPROM_DEFAULT_DIFF;
				}
			}
		}

		return;
	}

	pHalData->bTXPowerDataReadFromEEPORM = true;		/* YJ, move, 120316 */

	for (rfPath = 0; rfPath < MAX_RF_PATH; rfPath++) {
		/* 2 2.4G default value */
		for (group = 0; group < MAX_CHNL_GROUP_24G; group++) {
			pwrInfo24G->IndexCCK_Base[rfPath][group] =	PROMContent[eeAddr++];
			if (pwrInfo24G->IndexCCK_Base[rfPath][group] == 0xFF)
				pwrInfo24G->IndexCCK_Base[rfPath][group] = EEPROM_DEFAULT_24G_INDEX;
		}

		for (group = 0; group < MAX_CHNL_GROUP_24G-1; group++) {
			pwrInfo24G->IndexBW40_Base[rfPath][group] =	PROMContent[eeAddr++];
			if (pwrInfo24G->IndexBW40_Base[rfPath][group] == 0xFF)
				pwrInfo24G->IndexBW40_Base[rfPath][group] =	EEPROM_DEFAULT_24G_INDEX;
		}

		for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {
			if (TxCount == 0) {
				pwrInfo24G->BW40_Diff[rfPath][TxCount] = 0;
				if (PROMContent[eeAddr] == 0xFF)
					pwrInfo24G->BW20_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_24G_HT20_DIFF;
				else {
					pwrInfo24G->BW20_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0xf0)>>4;
					if (pwrInfo24G->BW20_Diff[rfPath][TxCount] & BIT3)		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->BW20_Diff[rfPath][TxCount] |= 0xF0;
				}

				if (PROMContent[eeAddr] == 0xFF)
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] = EEPROM_DEFAULT_24G_OFDM_DIFF;
				else {
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] = (PROMContent[eeAddr]&0x0f);
					if (pwrInfo24G->OFDM_Diff[rfPath][TxCount] & BIT3)		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->OFDM_Diff[rfPath][TxCount] |= 0xF0;
				}
				pwrInfo24G->CCK_Diff[rfPath][TxCount] = 0;
				eeAddr++;
			} else {
				if (PROMContent[eeAddr] == 0xFF)
					pwrInfo24G->BW40_Diff[rfPath][TxCount] = EEPROM_DEFAULT_DIFF;
				else {
					pwrInfo24G->BW40_Diff[rfPath][TxCount] = (PROMContent[eeAddr]&0xf0)>>4;
					if (pwrInfo24G->BW40_Diff[rfPath][TxCount] & BIT3)		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->BW40_Diff[rfPath][TxCount] |= 0xF0;
				}

				if (PROMContent[eeAddr] == 0xFF)
					pwrInfo24G->BW20_Diff[rfPath][TxCount] = EEPROM_DEFAULT_DIFF;
				else {
					pwrInfo24G->BW20_Diff[rfPath][TxCount] = (PROMContent[eeAddr]&0x0f);
					if (pwrInfo24G->BW20_Diff[rfPath][TxCount] & BIT3)		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->BW20_Diff[rfPath][TxCount] |= 0xF0;
				}
				eeAddr++;

				if (PROMContent[eeAddr] == 0xFF)
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] = EEPROM_DEFAULT_DIFF;
				else {
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] = (PROMContent[eeAddr]&0xf0)>>4;
					if (pwrInfo24G->OFDM_Diff[rfPath][TxCount] & BIT3)		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->OFDM_Diff[rfPath][TxCount] |= 0xF0;
				}

				if (PROMContent[eeAddr] == 0xFF)
					pwrInfo24G->CCK_Diff[rfPath][TxCount] = EEPROM_DEFAULT_DIFF;
				else {
					pwrInfo24G->CCK_Diff[rfPath][TxCount] = (PROMContent[eeAddr]&0x0f);
					if (pwrInfo24G->CCK_Diff[rfPath][TxCount] & BIT3)		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->CCK_Diff[rfPath][TxCount] |= 0xF0;
				}
				eeAddr++;
			}
		}
	}
}


void Hal_EfuseParseTxPowerInfo_8723B(
	struct adapter *padapter, u8 *PROMContent, bool AutoLoadFail
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	struct TxPowerInfo24G	pwrInfo24G;
	u8 	rfPath, ch, TxCount = 1;

	Hal_ReadPowerValueFromPROM_8723B(padapter, &pwrInfo24G, PROMContent, AutoLoadFail);
	for (rfPath = 0 ; rfPath < MAX_RF_PATH ; rfPath++) {
		for (ch = 0 ; ch < CHANNEL_MAX_NUMBER; ch++) {
			u8 group = 0;

			hal_get_chnl_group_8723b(ch + 1, &group);

			if (ch == 14-1) {
				pHalData->Index24G_CCK_Base[rfPath][ch] = pwrInfo24G.IndexCCK_Base[rfPath][5];
				pHalData->Index24G_BW40_Base[rfPath][ch] = pwrInfo24G.IndexBW40_Base[rfPath][group];
			} else {
				pHalData->Index24G_CCK_Base[rfPath][ch] = pwrInfo24G.IndexCCK_Base[rfPath][group];
				pHalData->Index24G_BW40_Base[rfPath][ch] = pwrInfo24G.IndexBW40_Base[rfPath][group];
			}
		}

		for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {
			pHalData->CCK_24G_Diff[rfPath][TxCount] = pwrInfo24G.CCK_Diff[rfPath][TxCount];
			pHalData->OFDM_24G_Diff[rfPath][TxCount] = pwrInfo24G.OFDM_Diff[rfPath][TxCount];
			pHalData->BW20_24G_Diff[rfPath][TxCount] = pwrInfo24G.BW20_Diff[rfPath][TxCount];
			pHalData->BW40_24G_Diff[rfPath][TxCount] = pwrInfo24G.BW40_Diff[rfPath][TxCount];
		}
	}

	/*  2010/10/19 MH Add Regulator recognize for CU. */
	if (!AutoLoadFail) {
		pHalData->EEPROMRegulatory = (PROMContent[EEPROM_RF_BOARD_OPTION_8723B]&0x7);	/* bit0~2 */
		if (PROMContent[EEPROM_RF_BOARD_OPTION_8723B] == 0xFF)
			pHalData->EEPROMRegulatory = (EEPROM_DEFAULT_BOARD_OPTION&0x7);	/* bit0~2 */
	} else
		pHalData->EEPROMRegulatory = 0;
}

void Hal_EfuseParseBTCoexistInfo_8723B(
	struct adapter *padapter, u8 *hwinfo, bool AutoLoadFail
)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	u8 tempval;
	u32 tmpu4;

	if (!AutoLoadFail) {
		tmpu4 = rtw_read32(padapter, REG_MULTI_FUNC_CTRL);
		if (tmpu4 & BT_FUNC_EN)
			pHalData->EEPROMBluetoothCoexist = true;
		else
			pHalData->EEPROMBluetoothCoexist = false;

		pHalData->EEPROMBluetoothType = BT_RTL8723B;

		tempval = hwinfo[EEPROM_RF_BT_SETTING_8723B];
		if (tempval != 0xFF) {
			pHalData->EEPROMBluetoothAntNum = tempval & BIT(0);
			/*  EFUSE_0xC3[6] == 0, S1(Main)-RF_PATH_A; */
			/*  EFUSE_0xC3[6] == 1, S0(Aux)-RF_PATH_B */
			pHalData->ant_path = (tempval & BIT(6))? RF_PATH_B : RF_PATH_A;
		} else {
			pHalData->EEPROMBluetoothAntNum = Ant_x1;
			if (pHalData->PackageType == PACKAGE_QFN68)
				pHalData->ant_path = RF_PATH_B;
			else
				pHalData->ant_path = RF_PATH_A;
		}
	} else {
		pHalData->EEPROMBluetoothCoexist = false;
		pHalData->EEPROMBluetoothType = BT_RTL8723B;
		pHalData->EEPROMBluetoothAntNum = Ant_x1;
		pHalData->ant_path = RF_PATH_A;
	}

	if (padapter->registrypriv.ant_num > 0) {
		switch (padapter->registrypriv.ant_num) {
		case 1:
			pHalData->EEPROMBluetoothAntNum = Ant_x1;
			break;
		case 2:
			pHalData->EEPROMBluetoothAntNum = Ant_x2;
			break;
		default:
			break;
		}
	}

	hal_btcoex_SetBTCoexist(padapter, pHalData->EEPROMBluetoothCoexist);
	hal_btcoex_SetChipType(padapter, pHalData->EEPROMBluetoothType);
	hal_btcoex_SetPgAntNum(padapter, pHalData->EEPROMBluetoothAntNum == Ant_x2 ? 2 : 1);
	if (pHalData->EEPROMBluetoothAntNum == Ant_x1)
		hal_btcoex_SetSingleAntPath(padapter, pHalData->ant_path);
}

void Hal_EfuseParseEEPROMVer_8723B(
	struct adapter *padapter, u8 *hwinfo, bool AutoLoadFail
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);

	if (!AutoLoadFail)
		pHalData->EEPROMVersion = hwinfo[EEPROM_VERSION_8723B];
	else
		pHalData->EEPROMVersion = 1;
}



void Hal_EfuseParsePackageType_8723B(
	struct adapter *padapter, u8 *hwinfo, bool AutoLoadFail
)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	u8 package;
	u8 efuseContent;

	Efuse_PowerSwitch(padapter, false, true);
	efuse_OneByteRead(padapter, 0x1FB, &efuseContent, false);
	Efuse_PowerSwitch(padapter, false, false);

	package = efuseContent & 0x7;
	switch (package) {
	case 0x4:
		pHalData->PackageType = PACKAGE_TFBGA79;
		break;
	case 0x5:
		pHalData->PackageType = PACKAGE_TFBGA90;
		break;
	case 0x6:
		pHalData->PackageType = PACKAGE_QFN68;
		break;
	case 0x7:
		pHalData->PackageType = PACKAGE_TFBGA80;
		break;

	default:
		pHalData->PackageType = PACKAGE_DEFAULT;
		break;
	}
}


void Hal_EfuseParseVoltage_8723B(
	struct adapter *padapter, u8 *hwinfo, bool AutoLoadFail
)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);

	/* memcpy(pEEPROM->adjuseVoltageVal, &hwinfo[EEPROM_Voltage_ADDR_8723B], 1); */
	pEEPROM->adjuseVoltageVal = (hwinfo[EEPROM_Voltage_ADDR_8723B] & 0xf0) >> 4;
}

void Hal_EfuseParseChnlPlan_8723B(
	struct adapter *padapter, u8 *hwinfo, bool AutoLoadFail
)
{
	padapter->mlmepriv.ChannelPlan = hal_com_config_channel_plan(
		padapter,
		hwinfo ? hwinfo[EEPROM_ChannelPlan_8723B] : 0xFF,
		padapter->registrypriv.channel_plan,
		RT_CHANNEL_DOMAIN_WORLD_NULL,
		AutoLoadFail
	);

	Hal_ChannelPlanToRegulation(padapter, padapter->mlmepriv.ChannelPlan);
}

void Hal_EfuseParseCustomerID_8723B(
	struct adapter *padapter, u8 *hwinfo, bool AutoLoadFail
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);

	if (!AutoLoadFail)
		pHalData->EEPROMCustomerID = hwinfo[EEPROM_CustomID_8723B];
	else
		pHalData->EEPROMCustomerID = 0;
}

void Hal_EfuseParseAntennaDiversity_8723B(
	struct adapter *padapter,
	u8 *hwinfo,
	bool AutoLoadFail
)
{
}

void Hal_EfuseParseXtal_8723B(
	struct adapter *padapter, u8 *hwinfo, bool AutoLoadFail
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);

	if (!AutoLoadFail) {
		pHalData->CrystalCap = hwinfo[EEPROM_XTAL_8723B];
		if (pHalData->CrystalCap == 0xFF)
			pHalData->CrystalCap = EEPROM_Default_CrystalCap_8723B;	   /* what value should 8812 set? */
	} else
		pHalData->CrystalCap = EEPROM_Default_CrystalCap_8723B;
}


void Hal_EfuseParseThermalMeter_8723B(
	struct adapter *padapter, u8 *PROMContent, u8 AutoLoadFail
)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);

	/*  */
	/*  ThermalMeter from EEPROM */
	/*  */
	if (!AutoLoadFail)
		pHalData->EEPROMThermalMeter = PROMContent[EEPROM_THERMAL_METER_8723B];
	else
		pHalData->EEPROMThermalMeter = EEPROM_Default_ThermalMeter_8723B;

	if ((pHalData->EEPROMThermalMeter == 0xff) || AutoLoadFail) {
		pHalData->bAPKThermalMeterIgnore = true;
		pHalData->EEPROMThermalMeter = EEPROM_Default_ThermalMeter_8723B;
	}
}


void Hal_ReadRFGainOffset(
	struct adapter *Adapter, u8 *PROMContent, bool AutoloadFail
)
{
	/*  */
	/*  BB_RF Gain Offset from EEPROM */
	/*  */

	if (!AutoloadFail) {
		Adapter->eeprompriv.EEPROMRFGainOffset = PROMContent[EEPROM_RF_GAIN_OFFSET];
		Adapter->eeprompriv.EEPROMRFGainVal = EFUSE_Read1Byte(Adapter, EEPROM_RF_GAIN_VAL);
	} else {
		Adapter->eeprompriv.EEPROMRFGainOffset = 0;
		Adapter->eeprompriv.EEPROMRFGainVal = 0xFF;
	}
}

u8 BWMapping_8723B(struct adapter *Adapter, struct pkt_attrib *pattrib)
{
	u8 BWSettingOfDesc = 0;
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);

	if (pHalData->CurrentChannelBW == CHANNEL_WIDTH_40) {
		if (pattrib->bwmode == CHANNEL_WIDTH_40)
			BWSettingOfDesc = 1;
		else
			BWSettingOfDesc = 0;
	} else
		BWSettingOfDesc = 0;

	/* if (pTcb->bBTTxPacket) */
	/* 	BWSettingOfDesc = 0; */

	return BWSettingOfDesc;
}

u8 SCMapping_8723B(struct adapter *Adapter, struct pkt_attrib *pattrib)
{
	u8 SCSettingOfDesc = 0;
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);

	if (pHalData->CurrentChannelBW == CHANNEL_WIDTH_40) {
		if (pattrib->bwmode == CHANNEL_WIDTH_40) {
			SCSettingOfDesc = HT_DATA_SC_DONOT_CARE;
		} else if (pattrib->bwmode == CHANNEL_WIDTH_20) {
			if (pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) {
				SCSettingOfDesc = HT_DATA_SC_20_UPPER_OF_40MHZ;
			} else if (pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) {
				SCSettingOfDesc = HT_DATA_SC_20_LOWER_OF_40MHZ;
			} else {
				SCSettingOfDesc = HT_DATA_SC_DONOT_CARE;
			}
		}
	} else {
		SCSettingOfDesc = HT_DATA_SC_DONOT_CARE;
	}

	return SCSettingOfDesc;
}

static void rtl8723b_cal_txdesc_chksum(struct tx_desc *ptxdesc)
{
	u16 *usPtr = (u16 *)ptxdesc;
	u32 count;
	u32 index;
	u16 checksum = 0;


	/*  Clear first */
	ptxdesc->txdw7 &= cpu_to_le32(0xffff0000);

	/*  checksume is always calculated by first 32 bytes, */
	/*  and it doesn't depend on TX DESC length. */
	/*  Thomas, Lucas@SD4, 20130515 */
	count = 16;

	for (index = 0; index < count; index++) {
		checksum |= le16_to_cpu(*(__le16 *)(usPtr + index));
	}

	ptxdesc->txdw7 |= cpu_to_le32(checksum & 0x0000ffff);
}

static u8 fill_txdesc_sectype(struct pkt_attrib *pattrib)
{
	u8 sectype = 0;
	if ((pattrib->encrypt > 0) && !pattrib->bswenc) {
		switch (pattrib->encrypt) {
		/*  SEC_TYPE */
		case _WEP40_:
		case _WEP104_:
		case _TKIP_:
		case _TKIP_WTMIC_:
			sectype = 1;
			break;

		case _AES_:
			sectype = 3;
			break;

		case _NO_PRIVACY_:
		default:
			break;
		}
	}
	return sectype;
}

static void fill_txdesc_vcs_8723b(struct adapter *padapter, struct pkt_attrib *pattrib, struct txdesc_8723b *ptxdesc)
{
	if (pattrib->vcs_mode) {
		switch (pattrib->vcs_mode) {
		case RTS_CTS:
			ptxdesc->rtsen = 1;
			/*  ENABLE HW RTS */
			ptxdesc->hw_rts_en = 1;
			break;

		case CTS_TO_SELF:
			ptxdesc->cts2self = 1;
			break;

		case NONE_VCS:
		default:
			break;
		}

		ptxdesc->rtsrate = 8; /*  RTS Rate =24M */
		ptxdesc->rts_ratefb_lmt = 0xF;

		if (padapter->mlmeextpriv.mlmext_info.preamble_mode == PREAMBLE_SHORT)
			ptxdesc->rts_short = 1;

		/*  Set RTS BW */
		if (pattrib->ht_en)
			ptxdesc->rts_sc = SCMapping_8723B(padapter, pattrib);
	}
}

static void fill_txdesc_phy_8723b(struct adapter *padapter, struct pkt_attrib *pattrib, struct txdesc_8723b *ptxdesc)
{
	if (pattrib->ht_en) {
		ptxdesc->data_bw = BWMapping_8723B(padapter, pattrib);

		ptxdesc->data_sc = SCMapping_8723B(padapter, pattrib);
	}
}

static void rtl8723b_fill_default_txdesc(
	struct xmit_frame *pxmitframe, u8 *pbuf
)
{
	struct adapter *padapter;
	struct hal_com_data *pHalData;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;
	struct pkt_attrib *pattrib;
	struct txdesc_8723b *ptxdesc;
	s32 bmcst;

	memset(pbuf, 0, TXDESC_SIZE);

	padapter = pxmitframe->padapter;
	pHalData = GET_HAL_DATA(padapter);
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &(pmlmeext->mlmext_info);

	pattrib = &pxmitframe->attrib;
	bmcst = IS_MCAST(pattrib->ra);

	ptxdesc = (struct txdesc_8723b *)pbuf;

	if (pxmitframe->frame_tag == DATA_FRAMETAG) {
		u8 drv_userate = 0;

		ptxdesc->macid = pattrib->mac_id; /*  CAM_ID(MAC_ID) */
		ptxdesc->rate_id = pattrib->raid;
		ptxdesc->qsel = pattrib->qsel;
		ptxdesc->seq = pattrib->seqnum;

		ptxdesc->sectype = fill_txdesc_sectype(pattrib);
		fill_txdesc_vcs_8723b(padapter, pattrib, ptxdesc);

		if (pattrib->icmp_pkt == 1 && padapter->registrypriv.wifi_spec == 1)
			drv_userate = 1;

		if (
			(pattrib->ether_type != 0x888e) &&
			(pattrib->ether_type != 0x0806) &&
			(pattrib->ether_type != 0x88B4) &&
			(pattrib->dhcp_pkt != 1) &&
			(drv_userate != 1)
		) {
			/*  Non EAP & ARP & DHCP type data packet */

			if (pattrib->ampdu_en) {
				ptxdesc->agg_en = 1; /*  AGG EN */
				ptxdesc->max_agg_num = 0x1f;
				ptxdesc->ampdu_density = pattrib->ampdu_spacing;
			} else
				ptxdesc->bk = 1; /*  AGG BK */

			fill_txdesc_phy_8723b(padapter, pattrib, ptxdesc);

			ptxdesc->data_ratefb_lmt = 0x1F;

			if (!pHalData->fw_ractrl) {
				ptxdesc->userate = 1;

				if (pHalData->dmpriv.INIDATA_RATE[pattrib->mac_id] & BIT(7))
					ptxdesc->data_short = 1;

				ptxdesc->datarate = pHalData->dmpriv.INIDATA_RATE[pattrib->mac_id] & 0x7F;
			}

			if (padapter->fix_rate != 0xFF) { /*  modify data rate by iwpriv */
				ptxdesc->userate = 1;
				if (padapter->fix_rate & BIT(7))
					ptxdesc->data_short = 1;

				ptxdesc->datarate = (padapter->fix_rate & 0x7F);
				ptxdesc->disdatafb = 1;
			}

			if (pattrib->ldpc)
				ptxdesc->data_ldpc = 1;
			if (pattrib->stbc)
				ptxdesc->data_stbc = 1;
		} else {
			/*  EAP data packet and ARP packet. */
			/*  Use the 1M data rate to send the EAP/ARP packet. */
			/*  This will maybe make the handshake smooth. */

			ptxdesc->bk = 1; /*  AGG BK */
			ptxdesc->userate = 1; /*  driver uses rate */
			if (pmlmeinfo->preamble_mode == PREAMBLE_SHORT)
				ptxdesc->data_short = 1;/*  DATA_SHORT */
			ptxdesc->datarate = MRateToHwRate(pmlmeext->tx_rate);
		}

		ptxdesc->usb_txagg_num = pxmitframe->agg_num;
	} else if (pxmitframe->frame_tag == MGNT_FRAMETAG) {
		ptxdesc->macid = pattrib->mac_id; /*  CAM_ID(MAC_ID) */
		ptxdesc->qsel = pattrib->qsel;
		ptxdesc->rate_id = pattrib->raid; /*  Rate ID */
		ptxdesc->seq = pattrib->seqnum;
		ptxdesc->userate = 1; /*  driver uses rate, 1M */

		ptxdesc->mbssid = pattrib->mbssid & 0xF;

		ptxdesc->rty_lmt_en = 1; /*  retry limit enable */
		if (pattrib->retry_ctrl) {
			ptxdesc->data_rt_lmt = 6;
		} else {
			ptxdesc->data_rt_lmt = 12;
		}

		ptxdesc->datarate = MRateToHwRate(pmlmeext->tx_rate);

		/*  CCX-TXRPT ack for xmit mgmt frames. */
		if (pxmitframe->ack_report) {
			ptxdesc->spe_rpt = 1;
			ptxdesc->sw_define = (u8)(GET_PRIMARY_ADAPTER(padapter)->xmitpriv.seq_no);
		}
	} else {
		ptxdesc->macid = pattrib->mac_id; /*  CAM_ID(MAC_ID) */
		ptxdesc->rate_id = pattrib->raid; /*  Rate ID */
		ptxdesc->qsel = pattrib->qsel;
		ptxdesc->seq = pattrib->seqnum;
		ptxdesc->userate = 1; /*  driver uses rate */
		ptxdesc->datarate = MRateToHwRate(pmlmeext->tx_rate);
	}

	ptxdesc->pktlen = pattrib->last_txcmdsz;
	ptxdesc->offset = TXDESC_SIZE + OFFSET_SZ;

	if (bmcst)
		ptxdesc->bmc = 1;

	/* 2009.11.05. tynli_test. Suggested by SD4 Filen for FW LPS.
	 * (1) The sequence number of each non-Qos frame / broadcast /
	 * multicast / mgnt frame should be controlled by Hw because Fw
	 * will also send null data which we cannot control when Fw LPS
	 * enable.
	 * --> default enable non-Qos data sequense number. 2010.06.23.
	 * by tynli.
	 * (2) Enable HW SEQ control for beacon packet, because we use
	 * Hw beacon.
	 * (3) Use HW Qos SEQ to control the seq num of Ext port non-Qos
	 * packets.
	 * 2010.06.23. Added by tynli.
	 */
	if (!pattrib->qos_en) /*  Hw set sequence number */
		ptxdesc->en_hwseq = 1; /*  HWSEQ_EN */
}

/* Description:
 *
 * Parameters:
 *	pxmitframe	xmitframe
 *	pbuf		where to fill tx desc
 */
void rtl8723b_update_txdesc(struct xmit_frame *pxmitframe, u8 *pbuf)
{
	struct tx_desc *pdesc;

	rtl8723b_fill_default_txdesc(pxmitframe, pbuf);
	pdesc = (struct tx_desc *)pbuf;
	rtl8723b_cal_txdesc_chksum(pdesc);
}

/*  */
/*  Description: In normal chip, we should send some packet to Hw which will be used by Fw */
/* 			in FW LPS mode. The function is to fill the Tx descriptor of this packets, then */
/* 			Fw can tell Hw to send these packet derectly. */
/*  Added by tynli. 2009.10.15. */
/*  */
/* type1:pspoll, type2:null */
void rtl8723b_fill_fake_txdesc(
	struct adapter *padapter,
	u8 *pDesc,
	u32 BufferLen,
	u8 IsPsPoll,
	u8 IsBTQosNull,
	u8 bDataFrame
)
{
	/*  Clear all status */
	memset(pDesc, 0, TXDESC_SIZE);

	SET_TX_DESC_FIRST_SEG_8723B(pDesc, 1); /* bFirstSeg; */
	SET_TX_DESC_LAST_SEG_8723B(pDesc, 1); /* bLastSeg; */

	SET_TX_DESC_OFFSET_8723B(pDesc, 0x28); /*  Offset = 32 */

	SET_TX_DESC_PKT_SIZE_8723B(pDesc, BufferLen); /*  Buffer size + command header */
	SET_TX_DESC_QUEUE_SEL_8723B(pDesc, QSLT_MGNT); /*  Fixed queue of Mgnt queue */

	/*  Set NAVUSEHDR to prevent Ps-poll AId filed to be changed to error vlaue by Hw. */
	if (IsPsPoll) {
		SET_TX_DESC_NAV_USE_HDR_8723B(pDesc, 1);
	} else {
		SET_TX_DESC_HWSEQ_EN_8723B(pDesc, 1); /*  Hw set sequence number */
		SET_TX_DESC_HWSEQ_SEL_8723B(pDesc, 0);
	}

	if (IsBTQosNull) {
		SET_TX_DESC_BT_INT_8723B(pDesc, 1);
	}

	SET_TX_DESC_USE_RATE_8723B(pDesc, 1); /*  use data rate which is set by Sw */
	SET_TX_DESC_OWN_8723B((u8 *)pDesc, 1);

	SET_TX_DESC_TX_RATE_8723B(pDesc, DESC8723B_RATE1M);

	/*  */
	/*  Encrypt the data frame if under security mode excepct null data. Suggested by CCW. */
	/*  */
	if (bDataFrame) {
		u32 EncAlg;

		EncAlg = padapter->securitypriv.dot11PrivacyAlgrthm;
		switch (EncAlg) {
		case _NO_PRIVACY_:
			SET_TX_DESC_SEC_TYPE_8723B(pDesc, 0x0);
			break;
		case _WEP40_:
		case _WEP104_:
		case _TKIP_:
			SET_TX_DESC_SEC_TYPE_8723B(pDesc, 0x1);
			break;
		case _SMS4_:
			SET_TX_DESC_SEC_TYPE_8723B(pDesc, 0x2);
			break;
		case _AES_:
			SET_TX_DESC_SEC_TYPE_8723B(pDesc, 0x3);
			break;
		default:
			SET_TX_DESC_SEC_TYPE_8723B(pDesc, 0x0);
			break;
		}
	}

	/*  USB interface drop packet if the checksum of descriptor isn't correct. */
	/*  Using this checksum can let hardware recovery from packet bulk out error (e.g. Cancel URC, Bulk out error.). */
	rtl8723b_cal_txdesc_chksum((struct tx_desc *)pDesc);
}

static void hw_var_set_opmode(struct adapter *padapter, u8 variable, u8 *val)
{
	u8 val8;
	u8 mode = *((u8 *)val);

	{
		/*  disable Port0 TSF update */
		val8 = rtw_read8(padapter, REG_BCN_CTRL);
		val8 |= DIS_TSF_UDT;
		rtw_write8(padapter, REG_BCN_CTRL, val8);

		/*  set net_type */
		Set_MSR(padapter, mode);

		if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_)) {
			{
				StopTxBeacon(padapter);
			}

			/*  disable atim wnd */
			rtw_write8(padapter, REG_BCN_CTRL, DIS_TSF_UDT|EN_BCN_FUNCTION|DIS_ATIM);
			/* rtw_write8(padapter, REG_BCN_CTRL, 0x18); */
		} else if (mode == _HW_STATE_ADHOC_) {
			ResumeTxBeacon(padapter);
			rtw_write8(padapter, REG_BCN_CTRL, DIS_TSF_UDT|EN_BCN_FUNCTION|DIS_BCNQ_SUB);
		} else if (mode == _HW_STATE_AP_) {

			ResumeTxBeacon(padapter);

			rtw_write8(padapter, REG_BCN_CTRL, DIS_TSF_UDT|DIS_BCNQ_SUB);

			/* Set RCR */
			rtw_write32(padapter, REG_RCR, 0x7000208e);/* CBSSID_DATA must set to 0, reject ICV_ERR packet */
			/* enable to rx data frame */
			rtw_write16(padapter, REG_RXFLTMAP2, 0xFFFF);
			/* enable to rx ps-poll */
			rtw_write16(padapter, REG_RXFLTMAP1, 0x0400);

			/* Beacon Control related register for first time */
			rtw_write8(padapter, REG_BCNDMATIM, 0x02); /*  2ms */

			/* rtw_write8(padapter, REG_BCN_MAX_ERR, 0xFF); */
			rtw_write8(padapter, REG_ATIMWND, 0x0a); /*  10ms */
			rtw_write16(padapter, REG_BCNTCFG, 0x00);
			rtw_write16(padapter, REG_TBTT_PROHIBIT, 0xff04);
			rtw_write16(padapter, REG_TSFTR_SYN_OFFSET, 0x7fff);/*  +32767 (~32ms) */

			/* reset TSF */
			rtw_write8(padapter, REG_DUAL_TSF_RST, BIT(0));

			/* enable BCN0 Function for if1 */
			/* don't enable update TSF0 for if1 (due to TSF update when beacon/probe rsp are received) */
			rtw_write8(padapter, REG_BCN_CTRL, (DIS_TSF_UDT|EN_BCN_FUNCTION|EN_TXBCN_RPT|DIS_BCNQ_SUB));

			/* SW_BCN_SEL - Port0 */
			/* rtw_write8(Adapter, REG_DWBCN1_CTRL_8192E+2, rtw_read8(Adapter, REG_DWBCN1_CTRL_8192E+2) & ~BIT4); */
			rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);

			/*  select BCN on port 0 */
			rtw_write8(
				padapter,
				REG_CCK_CHECK_8723B,
				(rtw_read8(padapter, REG_CCK_CHECK_8723B)&~BIT_BCN_PORT_SEL)
			);

			/*  dis BCN1 ATIM  WND if if2 is station */
			val8 = rtw_read8(padapter, REG_BCN_CTRL_1);
			val8 |= DIS_ATIM;
			rtw_write8(padapter, REG_BCN_CTRL_1, val8);
		}
	}
}

static void hw_var_set_macaddr(struct adapter *padapter, u8 variable, u8 *val)
{
	u8 idx = 0;
	u32 reg_macid;

	reg_macid = REG_MACID;

	for (idx = 0 ; idx < 6; idx++)
		rtw_write8(GET_PRIMARY_ADAPTER(padapter), (reg_macid+idx), val[idx]);
}

static void hw_var_set_bssid(struct adapter *padapter, u8 variable, u8 *val)
{
	u8 idx = 0;
	u32 reg_bssid;

	reg_bssid = REG_BSSID;

	for (idx = 0 ; idx < 6; idx++)
		rtw_write8(padapter, (reg_bssid+idx), val[idx]);
}

static void hw_var_set_bcn_func(struct adapter *padapter, u8 variable, u8 *val)
{
	u32 bcn_ctrl_reg;

	bcn_ctrl_reg = REG_BCN_CTRL;

	if (*(u8 *)val)
		rtw_write8(padapter, bcn_ctrl_reg, (EN_BCN_FUNCTION | EN_TXBCN_RPT));
	else {
		u8 val8;
		val8 = rtw_read8(padapter, bcn_ctrl_reg);
		val8 &= ~(EN_BCN_FUNCTION | EN_TXBCN_RPT);

		/*  Always enable port0 beacon function for PSTDMA */
		if (REG_BCN_CTRL == bcn_ctrl_reg)
			val8 |= EN_BCN_FUNCTION;

		rtw_write8(padapter, bcn_ctrl_reg, val8);
	}
}

static void hw_var_set_correct_tsf(struct adapter *padapter, u8 variable, u8 *val)
{
	u8 val8;
	u64 tsf;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;


	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	tsf = pmlmeext->TSFValue-do_div(pmlmeext->TSFValue, (pmlmeinfo->bcn_interval*1024))-1024; /* us */

	if (
		((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) ||
		((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
	)
		StopTxBeacon(padapter);

	{
		/*  disable related TSF function */
		val8 = rtw_read8(padapter, REG_BCN_CTRL);
		val8 &= ~EN_BCN_FUNCTION;
		rtw_write8(padapter, REG_BCN_CTRL, val8);

		rtw_write32(padapter, REG_TSFTR, tsf);
		rtw_write32(padapter, REG_TSFTR+4, tsf>>32);

		/*  enable related TSF function */
		val8 = rtw_read8(padapter, REG_BCN_CTRL);
		val8 |= EN_BCN_FUNCTION;
		rtw_write8(padapter, REG_BCN_CTRL, val8);
	}

	if (
		((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) ||
		((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
	)
		ResumeTxBeacon(padapter);
}

static void hw_var_set_mlme_disconnect(struct adapter *padapter, u8 variable, u8 *val)
{
	u8 val8;

	/*  Set RCR to not to receive data frame when NO LINK state */
	/* rtw_write32(padapter, REG_RCR, rtw_read32(padapter, REG_RCR) & ~RCR_ADF); */
	/*  reject all data frames */
	rtw_write16(padapter, REG_RXFLTMAP2, 0);

	/*  reset TSF */
	rtw_write8(padapter, REG_DUAL_TSF_RST, BIT(0));

	/*  disable update TSF */
	val8 = rtw_read8(padapter, REG_BCN_CTRL);
	val8 |= DIS_TSF_UDT;
	rtw_write8(padapter, REG_BCN_CTRL, val8);
}

static void hw_var_set_mlme_sitesurvey(struct adapter *padapter, u8 variable, u8 *val)
{
	u32 value_rcr, rcr_clear_bit, reg_bcn_ctl;
	u16 value_rxfltmap2;
	u8 val8;
	struct hal_com_data *pHalData;
	struct mlme_priv *pmlmepriv;


	pHalData = GET_HAL_DATA(padapter);
	pmlmepriv = &padapter->mlmepriv;

	reg_bcn_ctl = REG_BCN_CTRL;

	rcr_clear_bit = RCR_CBSSID_BCN;

	/*  config RCR to receive different BSSID & not to receive data frame */
	value_rxfltmap2 = 0;

	if ((check_fwstate(pmlmepriv, WIFI_AP_STATE) == true))
		rcr_clear_bit = RCR_CBSSID_BCN;

	value_rcr = rtw_read32(padapter, REG_RCR);

	if (*((u8 *)val)) {
		/*  under sitesurvey */
		value_rcr &= ~(rcr_clear_bit);
		rtw_write32(padapter, REG_RCR, value_rcr);

		rtw_write16(padapter, REG_RXFLTMAP2, value_rxfltmap2);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE)) {
			/*  disable update TSF */
			val8 = rtw_read8(padapter, reg_bcn_ctl);
			val8 |= DIS_TSF_UDT;
			rtw_write8(padapter, reg_bcn_ctl, val8);
		}

		/*  Save original RRSR setting. */
		pHalData->RegRRSR = rtw_read16(padapter, REG_RRSR);
	} else {
		/*  sitesurvey done */
		if (check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)))
			/*  enable to rx data frame */
			rtw_write16(padapter, REG_RXFLTMAP2, 0xFFFF);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE)) {
			/*  enable update TSF */
			val8 = rtw_read8(padapter, reg_bcn_ctl);
			val8 &= ~DIS_TSF_UDT;
			rtw_write8(padapter, reg_bcn_ctl, val8);
		}

		value_rcr |= rcr_clear_bit;
		rtw_write32(padapter, REG_RCR, value_rcr);

		/*  Restore original RRSR setting. */
		rtw_write16(padapter, REG_RRSR, pHalData->RegRRSR);
	}
}

static void hw_var_set_mlme_join(struct adapter *padapter, u8 variable, u8 *val)
{
	u8 val8;
	u16 val16;
	u32 val32;
	u8 RetryLimit;
	u8 type;
	struct mlme_priv *pmlmepriv;
	struct eeprom_priv *pEEPROM;


	RetryLimit = 0x30;
	type = *(u8 *)val;
	pmlmepriv = &padapter->mlmepriv;
	pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);

	if (type == 0) { /*  prepare to join */
		/* enable to rx data frame.Accept all data frame */
		/* rtw_write32(padapter, REG_RCR, rtw_read32(padapter, REG_RCR)|RCR_ADF); */
		rtw_write16(padapter, REG_RXFLTMAP2, 0xFFFF);

		val32 = rtw_read32(padapter, REG_RCR);
		if (padapter->in_cta_test)
			val32 &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN);/*  RCR_ADF */
		else
			val32 |= RCR_CBSSID_DATA|RCR_CBSSID_BCN;
		rtw_write32(padapter, REG_RCR, val32);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true)
			RetryLimit = (pEEPROM->CustomerID == RT_CID_CCX) ? 7 : 48;
		else /*  Ad-hoc Mode */
			RetryLimit = 0x7;
	} else if (type == 1) /* joinbss_event call back when join res < 0 */
		rtw_write16(padapter, REG_RXFLTMAP2, 0x00);
	else if (type == 2) { /* sta add event call back */
		/* enable update TSF */
		val8 = rtw_read8(padapter, REG_BCN_CTRL);
		val8 &= ~DIS_TSF_UDT;
		rtw_write8(padapter, REG_BCN_CTRL, val8);

		if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE))
			RetryLimit = 0x7;
	}

	val16 = (RetryLimit << RETRY_LIMIT_SHORT_SHIFT) | (RetryLimit << RETRY_LIMIT_LONG_SHIFT);
	rtw_write16(padapter, REG_RL, val16);
}

void CCX_FwC2HTxRpt_8723b(struct adapter *padapter, u8 *pdata, u8 len)
{

#define	GET_8723B_C2H_TX_RPT_LIFE_TIME_OVER(_Header)	LE_BITS_TO_1BYTE((_Header + 0), 6, 1)
#define	GET_8723B_C2H_TX_RPT_RETRY_OVER(_Header)	LE_BITS_TO_1BYTE((_Header + 0), 7, 1)

	if (GET_8723B_C2H_TX_RPT_RETRY_OVER(pdata) | GET_8723B_C2H_TX_RPT_LIFE_TIME_OVER(pdata)) {
		rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_CCX_PKT_FAIL);
	}
/*
	else if (seq_no != padapter->xmitpriv.seq_no) {
		rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_CCX_PKT_FAIL);
	}
*/
	else
		rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_SUCCESS);
}

s32 c2h_id_filter_ccx_8723b(u8 *buf)
{
	struct c2h_evt_hdr_88xx *c2h_evt = (struct c2h_evt_hdr_88xx *)buf;
	s32 ret = false;
	if (c2h_evt->id == C2H_CCX_TX_RPT)
		ret = true;

	return ret;
}


s32 c2h_handler_8723b(struct adapter *padapter, u8 *buf)
{
	struct c2h_evt_hdr_88xx *pC2hEvent = (struct c2h_evt_hdr_88xx *)buf;
	s32 ret = _SUCCESS;

	if (!pC2hEvent) {
		ret = _FAIL;
		goto exit;
	}

	switch (pC2hEvent->id) {
	case C2H_AP_RPT_RSP:
		break;
	case C2H_DBG:
		{
		}
		break;

	case C2H_CCX_TX_RPT:
/* 			CCX_FwC2HTxRpt(padapter, QueueID, pC2hEvent->payload); */
		break;

	case C2H_EXT_RA_RPT:
/* 			C2HExtRaRptHandler(padapter, pC2hEvent->payload, C2hEvent.CmdLen); */
		break;

	case C2H_HW_INFO_EXCH:
		break;

	case C2H_8723B_BT_INFO:
		hal_btcoex_BtInfoNotify(padapter, pC2hEvent->plen, pC2hEvent->payload);
		break;

	default:
		break;
	}

	/*  Clear event to notify FW we have read the command. */
	/*  Note: */
	/* 	If this field isn't clear, the FW won't update the next command message. */
/* 	rtw_write8(padapter, REG_C2HEVT_CLEAR, C2H_EVT_HOST_CLOSE); */
exit:
	return ret;
}

static void process_c2h_event(struct adapter *padapter, struct c2h_evt_hdr_t *pC2hEvent, u8 *c2hBuf)
{
	if (!c2hBuf)
		return;

	switch (pC2hEvent->CmdID) {
	case C2H_AP_RPT_RSP:
		break;
	case C2H_DBG:
		{
		}
		break;

	case C2H_CCX_TX_RPT:
/* 			CCX_FwC2HTxRpt(padapter, QueueID, tmpBuf); */
		break;

	case C2H_EXT_RA_RPT:
/* 			C2HExtRaRptHandler(padapter, tmpBuf, C2hEvent.CmdLen); */
		break;

	case C2H_HW_INFO_EXCH:
		break;

	case C2H_8723B_BT_INFO:
		hal_btcoex_BtInfoNotify(padapter, pC2hEvent->CmdLen, c2hBuf);
		break;

	default:
		break;
	}
}

void C2HPacketHandler_8723B(struct adapter *padapter, u8 *pbuffer, u16 length)
{
	struct c2h_evt_hdr_t	C2hEvent;
	u8 *tmpBuf = NULL;
	C2hEvent.CmdID = pbuffer[0];
	C2hEvent.CmdSeq = pbuffer[1];
	C2hEvent.CmdLen = length-2;
	tmpBuf = pbuffer+2;

	process_c2h_event(padapter, &C2hEvent, tmpBuf);
	/* c2h_handler_8723b(padapter,&C2hEvent); */
}

void SetHwReg8723B(struct adapter *padapter, u8 variable, u8 *val)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	u8 val8;
	u32 val32;

	switch (variable) {
	case HW_VAR_MEDIA_STATUS:
		val8 = rtw_read8(padapter, MSR) & 0x0c;
		val8 |= *val;
		rtw_write8(padapter, MSR, val8);
		break;

	case HW_VAR_MEDIA_STATUS1:
		val8 = rtw_read8(padapter, MSR) & 0x03;
		val8 |= *val << 2;
		rtw_write8(padapter, MSR, val8);
		break;

	case HW_VAR_SET_OPMODE:
		hw_var_set_opmode(padapter, variable, val);
		break;

	case HW_VAR_MAC_ADDR:
		hw_var_set_macaddr(padapter, variable, val);
		break;

	case HW_VAR_BSSID:
		hw_var_set_bssid(padapter, variable, val);
		break;

	case HW_VAR_BASIC_RATE:
	{
		struct mlme_ext_info *mlmext_info = &padapter->mlmeextpriv.mlmext_info;
		u16 BrateCfg = 0;
		u16 rrsr_2g_force_mask = (RRSR_11M|RRSR_5_5M|RRSR_1M);
		u16 rrsr_2g_allow_mask = (RRSR_24M|RRSR_12M|RRSR_6M|RRSR_CCK_RATES);

		HalSetBrateCfg(padapter, val, &BrateCfg);

		/* apply force and allow mask */
		BrateCfg |= rrsr_2g_force_mask;
		BrateCfg &= rrsr_2g_allow_mask;

		/* IOT consideration */
		if (mlmext_info->assoc_AP_vendor == HT_IOT_PEER_CISCO) {
			/* if peer is cisco and didn't use ofdm rate, we enable 6M ack */
			if ((BrateCfg & (RRSR_24M|RRSR_12M|RRSR_6M)) == 0)
				BrateCfg |= RRSR_6M;
		}

		pHalData->BasicRateSet = BrateCfg;

		/*  Set RRSR rate table. */
		rtw_write16(padapter, REG_RRSR, BrateCfg);
		rtw_write8(padapter, REG_RRSR+2, rtw_read8(padapter, REG_RRSR+2)&0xf0);
	}
		break;

	case HW_VAR_TXPAUSE:
		rtw_write8(padapter, REG_TXPAUSE, *val);
		break;

	case HW_VAR_BCN_FUNC:
		hw_var_set_bcn_func(padapter, variable, val);
		break;

	case HW_VAR_CORRECT_TSF:
		hw_var_set_correct_tsf(padapter, variable, val);
		break;

	case HW_VAR_CHECK_BSSID:
		{
			u32 val32;
			val32 = rtw_read32(padapter, REG_RCR);
			if (*val)
				val32 |= RCR_CBSSID_DATA|RCR_CBSSID_BCN;
			else
				val32 &= ~(RCR_CBSSID_DATA|RCR_CBSSID_BCN);
			rtw_write32(padapter, REG_RCR, val32);
		}
		break;

	case HW_VAR_MLME_DISCONNECT:
		hw_var_set_mlme_disconnect(padapter, variable, val);
		break;

	case HW_VAR_MLME_SITESURVEY:
		hw_var_set_mlme_sitesurvey(padapter, variable,  val);

		hal_btcoex_ScanNotify(padapter, *val?true:false);
		break;

	case HW_VAR_MLME_JOIN:
		hw_var_set_mlme_join(padapter, variable, val);

		switch (*val) {
		case 0:
			/*  prepare to join */
			hal_btcoex_ConnectNotify(padapter, true);
			break;
		case 1:
			/*  joinbss_event callback when join res < 0 */
			hal_btcoex_ConnectNotify(padapter, false);
			break;
		case 2:
			/*  sta add event callback */
/* 				rtw_btcoex_MediaStatusNotify(padapter, RT_MEDIA_CONNECT); */
			break;
		}
		break;

	case HW_VAR_ON_RCR_AM:
		val32 = rtw_read32(padapter, REG_RCR);
		val32 |= RCR_AM;
		rtw_write32(padapter, REG_RCR, val32);
		break;

	case HW_VAR_OFF_RCR_AM:
		val32 = rtw_read32(padapter, REG_RCR);
		val32 &= ~RCR_AM;
		rtw_write32(padapter, REG_RCR, val32);
		break;

	case HW_VAR_BEACON_INTERVAL:
		rtw_write16(padapter, REG_BCN_INTERVAL, *((u16 *)val));
		break;

	case HW_VAR_SLOT_TIME:
		rtw_write8(padapter, REG_SLOT, *val);
		break;

	case HW_VAR_RESP_SIFS:
		/* SIFS_Timer = 0x0a0a0808; */
		/* RESP_SIFS for CCK */
		rtw_write8(padapter, REG_RESP_SIFS_CCK, val[0]); /*  SIFS_T2T_CCK (0x08) */
		rtw_write8(padapter, REG_RESP_SIFS_CCK+1, val[1]); /* SIFS_R2T_CCK(0x08) */
		/* RESP_SIFS for OFDM */
		rtw_write8(padapter, REG_RESP_SIFS_OFDM, val[2]); /* SIFS_T2T_OFDM (0x0a) */
		rtw_write8(padapter, REG_RESP_SIFS_OFDM+1, val[3]); /* SIFS_R2T_OFDM(0x0a) */
		break;

	case HW_VAR_ACK_PREAMBLE:
		{
			u8 regTmp;
			u8 bShortPreamble = *val;

			/*  Joseph marked out for Netgear 3500 TKIP channel 7 issue.(Temporarily) */
			/* regTmp = (pHalData->nCur40MhzPrimeSC)<<5; */
			regTmp = 0;
			if (bShortPreamble)
				regTmp |= 0x80;
			rtw_write8(padapter, REG_RRSR+2, regTmp);
		}
		break;

	case HW_VAR_CAM_EMPTY_ENTRY:
		{
			u8 ucIndex = *val;
			u8 i;
			u32 ulCommand = 0;
			u32 ulContent = 0;
			u32 ulEncAlgo = CAM_AES;

			for (i = 0; i < CAM_CONTENT_COUNT; i++) {
				/*  filled id in CAM config 2 byte */
				if (i == 0) {
					ulContent |= (ucIndex & 0x03) | ((u16)(ulEncAlgo)<<2);
					/* ulContent |= CAM_VALID; */
				} else
					ulContent = 0;

				/*  polling bit, and No Write enable, and address */
				ulCommand = CAM_CONTENT_COUNT*ucIndex+i;
				ulCommand = ulCommand | CAM_POLLINIG | CAM_WRITE;
				/*  write content 0 is equall to mark invalid */
				rtw_write32(padapter, WCAMI, ulContent);  /* mdelay(40); */
				rtw_write32(padapter, RWCAM, ulCommand);  /* mdelay(40); */
			}
		}
		break;

	case HW_VAR_CAM_INVALID_ALL:
		rtw_write32(padapter, RWCAM, BIT(31)|BIT(30));
		break;

	case HW_VAR_CAM_WRITE:
		{
			u32 cmd;
			u32 *cam_val = (u32 *)val;

			rtw_write32(padapter, WCAMI, cam_val[0]);

			cmd = CAM_POLLINIG | CAM_WRITE | cam_val[1];
			rtw_write32(padapter, RWCAM, cmd);
		}
		break;

	case HW_VAR_AC_PARAM_VO:
		rtw_write32(padapter, REG_EDCA_VO_PARAM, *((u32 *)val));
		break;

	case HW_VAR_AC_PARAM_VI:
		rtw_write32(padapter, REG_EDCA_VI_PARAM, *((u32 *)val));
		break;

	case HW_VAR_AC_PARAM_BE:
		pHalData->AcParam_BE = ((u32 *)(val))[0];
		rtw_write32(padapter, REG_EDCA_BE_PARAM, *((u32 *)val));
		break;

	case HW_VAR_AC_PARAM_BK:
		rtw_write32(padapter, REG_EDCA_BK_PARAM, *((u32 *)val));
		break;

	case HW_VAR_ACM_CTRL:
		{
			u8 ctrl = *((u8 *)val);
			u8 hwctrl = 0;

			if (ctrl != 0) {
				hwctrl |= AcmHw_HwEn;

				if (ctrl & BIT(1)) /*  BE */
					hwctrl |= AcmHw_BeqEn;

				if (ctrl & BIT(2)) /*  VI */
					hwctrl |= AcmHw_ViqEn;

				if (ctrl & BIT(3)) /*  VO */
					hwctrl |= AcmHw_VoqEn;
			}

			rtw_write8(padapter, REG_ACMHWCTRL, hwctrl);
		}
		break;

	case HW_VAR_AMPDU_FACTOR:
		{
			u32 AMPDULen =  (*((u8 *)val));

			if (AMPDULen < HT_AGG_SIZE_32K)
				AMPDULen = (0x2000 << (*((u8 *)val)))-1;
			else
				AMPDULen = 0x7fff;

			rtw_write32(padapter, REG_AMPDU_MAX_LENGTH_8723B, AMPDULen);
		}
		break;

	case HW_VAR_H2C_FW_PWRMODE:
		{
			u8 psmode = *val;

			/*  Forece leave RF low power mode for 1T1R to prevent conficting setting in Fw power */
			/*  saving sequence. 2010.06.07. Added by tynli. Suggested by SD3 yschang. */
			if (psmode != PS_MODE_ACTIVE) {
				ODM_RF_Saving(&pHalData->odmpriv, true);
			}

			/* if (psmode != PS_MODE_ACTIVE)	{ */
			/* 	rtl8723b_set_lowpwr_lps_cmd(padapter, true); */
			/*  else { */
			/* 	rtl8723b_set_lowpwr_lps_cmd(padapter, false); */
			/*  */
			rtl8723b_set_FwPwrMode_cmd(padapter, psmode);
		}
		break;
	case HW_VAR_H2C_PS_TUNE_PARAM:
		rtl8723b_set_FwPsTuneParam_cmd(padapter);
		break;

	case HW_VAR_H2C_FW_JOINBSSRPT:
		rtl8723b_set_FwJoinBssRpt_cmd(padapter, *val);
		break;

	case HW_VAR_INITIAL_GAIN:
		{
			struct dig_t *pDigTable = &pHalData->odmpriv.DM_DigTable;
			u32 rx_gain = *(u32 *)val;

			if (rx_gain == 0xff) {/* restore rx gain */
				ODM_Write_DIG(&pHalData->odmpriv, pDigTable->BackupIGValue);
			} else {
				pDigTable->BackupIGValue = pDigTable->CurIGValue;
				ODM_Write_DIG(&pHalData->odmpriv, rx_gain);
			}
		}
		break;

	case HW_VAR_EFUSE_USAGE:
		pHalData->EfuseUsedPercentage = *val;
		break;

	case HW_VAR_EFUSE_BYTES:
		pHalData->EfuseUsedBytes = *((u16 *)val);
		break;

	case HW_VAR_EFUSE_BT_USAGE:
#ifdef HAL_EFUSE_MEMORY
		pHalData->EfuseHal.BTEfuseUsedPercentage = *val;
#endif
		break;

	case HW_VAR_EFUSE_BT_BYTES:
#ifdef HAL_EFUSE_MEMORY
		pHalData->EfuseHal.BTEfuseUsedBytes = *((u16 *)val);
#else
		BTEfuseUsedBytes = *((u16 *)val);
#endif
		break;

	case HW_VAR_FIFO_CLEARN_UP:
		{
			#define RW_RELEASE_EN		BIT(18)
			#define RXDMA_IDLE			BIT(17)

			struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
			u8 trycnt = 100;

			/*  pause tx */
			rtw_write8(padapter, REG_TXPAUSE, 0xff);

			/*  keep sn */
			padapter->xmitpriv.nqos_ssn = rtw_read16(padapter, REG_NQOS_SEQ);

			if (!pwrpriv->bkeepfwalive) {
				/* RX DMA stop */
				val32 = rtw_read32(padapter, REG_RXPKT_NUM);
				val32 |= RW_RELEASE_EN;
				rtw_write32(padapter, REG_RXPKT_NUM, val32);
				do {
					val32 = rtw_read32(padapter, REG_RXPKT_NUM);
					val32 &= RXDMA_IDLE;
					if (val32)
						break;
				} while (--trycnt);

				/*  RQPN Load 0 */
				rtw_write16(padapter, REG_RQPN_NPQ, 0);
				rtw_write32(padapter, REG_RQPN, 0x80000000);
				mdelay(2);
			}
		}
		break;

	case HW_VAR_APFM_ON_MAC:
		pHalData->bMacPwrCtrlOn = *val;
		break;

	case HW_VAR_NAV_UPPER:
		{
			u32 usNavUpper = *((u32 *)val);

			if (usNavUpper > HAL_NAV_UPPER_UNIT_8723B * 0xFF)
				break;

			usNavUpper = DIV_ROUND_UP(usNavUpper,
						  HAL_NAV_UPPER_UNIT_8723B);
			rtw_write8(padapter, REG_NAV_UPPER, (u8)usNavUpper);
		}
		break;

	case HW_VAR_H2C_MEDIA_STATUS_RPT:
		{
			u16 mstatus_rpt = (*(u16 *)val);
			u8 mstatus, macId;

			mstatus = (u8) (mstatus_rpt & 0xFF);
			macId = (u8)(mstatus_rpt >> 8);
			rtl8723b_set_FwMediaStatusRpt_cmd(padapter, mstatus, macId);
		}
		break;
	case HW_VAR_BCN_VALID:
		{
			/*  BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2, write 1 to clear, Clear by sw */
			val8 = rtw_read8(padapter, REG_TDECTRL+2);
			val8 |= BIT(0);
			rtw_write8(padapter, REG_TDECTRL+2, val8);
		}
		break;

	case HW_VAR_DL_BCN_SEL:
		{
			/*  SW_BCN_SEL - Port0 */
			val8 = rtw_read8(padapter, REG_DWBCN1_CTRL_8723B+2);
			val8 &= ~BIT(4);
			rtw_write8(padapter, REG_DWBCN1_CTRL_8723B+2, val8);
		}
		break;

	case HW_VAR_DO_IQK:
		pHalData->bNeedIQK = true;
		break;

	case HW_VAR_DL_RSVD_PAGE:
		if (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == true)
			rtl8723b_download_BTCoex_AP_mode_rsvd_page(padapter);
		else
			rtl8723b_download_rsvd_page(padapter, RT_MEDIA_CONNECT);
		break;

	case HW_VAR_MACID_SLEEP:
		/*  Input is MACID */
		val32 = *(u32 *)val;
		if (val32 > 31)
			break;

		val8 = (u8)val32; /*  macid is between 0~31 */

		val32 = rtw_read32(padapter, REG_MACID_SLEEP);
		if (val32 & BIT(val8))
			break;
		val32 |= BIT(val8);
		rtw_write32(padapter, REG_MACID_SLEEP, val32);
		break;

	case HW_VAR_MACID_WAKEUP:
		/*  Input is MACID */
		val32 = *(u32 *)val;
		if (val32 > 31)
			break;

		val8 = (u8)val32; /*  macid is between 0~31 */

		val32 = rtw_read32(padapter, REG_MACID_SLEEP);
		if (!(val32 & BIT(val8)))
			break;
		val32 &= ~BIT(val8);
		rtw_write32(padapter, REG_MACID_SLEEP, val32);
		break;

	default:
		SetHwReg(padapter, variable, val);
		break;
	}
}

void GetHwReg8723B(struct adapter *padapter, u8 variable, u8 *val)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	u8 val8;
	u16 val16;

	switch (variable) {
	case HW_VAR_TXPAUSE:
		*val = rtw_read8(padapter, REG_TXPAUSE);
		break;

	case HW_VAR_BCN_VALID:
		{
			/*  BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2 */
			val8 = rtw_read8(padapter, REG_TDECTRL+2);
			*val = (BIT(0) & val8) ? true : false;
		}
		break;

	case HW_VAR_FWLPS_RF_ON:
		{
			/*  When we halt NIC, we should check if FW LPS is leave. */
			u32 valRCR;

			if (
				padapter->bSurpriseRemoved  ||
				(adapter_to_pwrctl(padapter)->rf_pwrstate == rf_off)
			) {
				/*  If it is in HW/SW Radio OFF or IPS state, we do not check Fw LPS Leave, */
				/*  because Fw is unload. */
				*val = true;
			} else {
				valRCR = rtw_read32(padapter, REG_RCR);
				valRCR &= 0x00070000;
				if (valRCR)
					*val = false;
				else
					*val = true;
			}
		}
		break;

	case HW_VAR_EFUSE_USAGE:
		*val = pHalData->EfuseUsedPercentage;
		break;

	case HW_VAR_EFUSE_BYTES:
		*((u16 *)val) = pHalData->EfuseUsedBytes;
		break;

	case HW_VAR_EFUSE_BT_USAGE:
#ifdef HAL_EFUSE_MEMORY
		*val = pHalData->EfuseHal.BTEfuseUsedPercentage;
#endif
		break;

	case HW_VAR_EFUSE_BT_BYTES:
#ifdef HAL_EFUSE_MEMORY
		*((u16 *)val) = pHalData->EfuseHal.BTEfuseUsedBytes;
#else
		*((u16 *)val) = BTEfuseUsedBytes;
#endif
		break;

	case HW_VAR_APFM_ON_MAC:
		*val = pHalData->bMacPwrCtrlOn;
		break;
	case HW_VAR_CHK_HI_QUEUE_EMPTY:
		val16 = rtw_read16(padapter, REG_TXPKT_EMPTY);
		*val = (val16 & BIT(10)) ? true:false;
		break;
	default:
		GetHwReg(padapter, variable, val);
		break;
	}
}

/* Description:
 *	Change default setting of specified variable.
 */
u8 SetHalDefVar8723B(struct adapter *padapter, enum hal_def_variable variable, void *pval)
{
	u8 bResult;

	bResult = _SUCCESS;

	switch (variable) {
	default:
		bResult = SetHalDefVar(padapter, variable, pval);
		break;
	}

	return bResult;
}

/* Description:
 *	Query setting of specified variable.
 */
u8 GetHalDefVar8723B(struct adapter *padapter, enum hal_def_variable variable, void *pval)
{
	u8 bResult;

	bResult = _SUCCESS;

	switch (variable) {
	case HAL_DEF_MAX_RECVBUF_SZ:
		*((u32 *)pval) = MAX_RECVBUF_SZ;
		break;

	case HAL_DEF_RX_PACKET_OFFSET:
		*((u32 *)pval) = RXDESC_SIZE + DRVINFO_SZ*8;
		break;

	case HW_VAR_MAX_RX_AMPDU_FACTOR:
		/*  Stanley@BB.SD3 suggests 16K can get stable performance */
		/*  The experiment was done on SDIO interface */
		/*  coding by Lucas@20130730 */
		*(u32 *)pval = IEEE80211_HT_MAX_AMPDU_16K;
		break;
	case HAL_DEF_TX_LDPC:
	case HAL_DEF_RX_LDPC:
		*((u8 *)pval) = false;
		break;
	case HAL_DEF_TX_STBC:
		*((u8 *)pval) = 0;
		break;
	case HAL_DEF_RX_STBC:
		*((u8 *)pval) = 1;
		break;
	case HAL_DEF_EXPLICIT_BEAMFORMER:
	case HAL_DEF_EXPLICIT_BEAMFORMEE:
		*((u8 *)pval) = false;
		break;

	case HW_DEF_RA_INFO_DUMP:
		{
			u8 mac_id = *(u8 *)pval;
			u32 cmd;

			cmd = 0x40000100 | mac_id;
			rtw_write32(padapter, REG_HMEBOX_DBG_2_8723B, cmd);
			msleep(10);
			rtw_read32(padapter, 0x2F0);	// info 1

			cmd = 0x40000400 | mac_id;
			rtw_write32(padapter, REG_HMEBOX_DBG_2_8723B, cmd);
			msleep(10);
			rtw_read32(padapter, 0x2F0);	// info 1
			rtw_read32(padapter, 0x2F4);	// info 2
			rtw_read32(padapter, 0x2F8);	// rate mask 1
			rtw_read32(padapter, 0x2FC);	// rate mask 2
		}
		break;

	case HAL_DEF_TX_PAGE_BOUNDARY:
		if (!padapter->registrypriv.wifi_spec) {
			*(u8 *)pval = TX_PAGE_BOUNDARY_8723B;
		} else {
			*(u8 *)pval = WMM_NORMAL_TX_PAGE_BOUNDARY_8723B;
		}
		break;

	case HAL_DEF_MACID_SLEEP:
		*(u8 *)pval = true; /*  support macid sleep */
		break;

	default:
		bResult = GetHalDefVar(padapter, variable, pval);
		break;
	}

	return bResult;
}

void rtl8723b_start_thread(struct adapter *padapter)
{
	struct xmit_priv *xmitpriv = &padapter->xmitpriv;

	xmitpriv->SdioXmitThread = kthread_run(rtl8723bs_xmit_thread, padapter, "RTWHALXT");
}

void rtl8723b_stop_thread(struct adapter *padapter)
{
	struct xmit_priv *xmitpriv = &padapter->xmitpriv;

	/*  stop xmit_buf_thread */
	if (xmitpriv->SdioXmitThread) {
		complete(&xmitpriv->SdioXmitStart);
		wait_for_completion(&xmitpriv->SdioXmitTerminate);
		xmitpriv->SdioXmitThread = NULL;
	}
}
