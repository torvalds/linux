/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *
 ******************************************************************************/

#define _RTL8192D_HAL_INIT_C_
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_efuse.h>
#include <hal_intf.h>
#include <usb_hal.h>
#include <rtl8192d_hal.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>

atomic_t GlobalMutexForGlobaladapterList = ATOMIC_INIT(0);
atomic_t GlobalMutexForMac0_2G_Mac1_5G = ATOMIC_INIT(0);
atomic_t GlobalMutexForPowerAndEfuse = ATOMIC_INIT(0);
atomic_t GlobalMutexForPowerOnAndPowerOff = ATOMIC_INIT(0);
static atomic_t GlobalMutexForFwDownload = ATOMIC_INIT(0);
#ifdef CONFIG_DUALMAC_CONCURRENT
atomic_t GlobalCounterForMutex = ATOMIC_INIT(0);
bool GlobalFirstConfigurationForNormalChip = true;
#endif

static bool _IsFWDownloaded(struct rtw_adapter *adapter)
{
	return ((rtw_read32(adapter, REG_MCUFWDL) & MCUFWDL_RDY) ? true : false);
}

static void _FWDownloadEnable(struct rtw_adapter *adapter, bool enable)
{
	u8	tmp;

	if (enable) {
		#ifdef DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE
		{
			u8 val;
			if ((val = rtw_read8(adapter, REG_MCUFWDL)))
				DBG_8192D("DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE %s:%d REG_MCUFWDL:0x%02x\n", __func__, __LINE__, val);
		}
		#endif
		/*  8051 enable */
		tmp = rtw_read8(adapter, REG_SYS_FUNC_EN+1);
		rtw_write8(adapter, REG_SYS_FUNC_EN+1, tmp|0x04);

		/*  MCU firmware download enable. */
		tmp = rtw_read8(adapter, REG_MCUFWDL);
		rtw_write8(adapter, REG_MCUFWDL, tmp|0x01);

		/*  8051 reset */
		tmp = rtw_read8(adapter, REG_MCUFWDL+2);
		rtw_write8(adapter, REG_MCUFWDL+2, tmp&0xf7);
	} else {
		/*  MCU firmware download enable. */
		tmp = rtw_read8(adapter, REG_MCUFWDL);
		rtw_write8(adapter, REG_MCUFWDL, tmp&0xfe);
	}
}

static int _BlockWrite_92d(struct rtw_adapter *adapter, void *buffer, u32 size)
{
	int ret = _SUCCESS;
	u32			blockSize8 = sizeof(u64);
	u32			blocksize4 = sizeof(u32);
	u32			blockSize = 64;
	u8*			bufferPtr = (u8*)buffer;
	u32*		pu4BytePtr = (u32*)buffer;
	u32			i, offset, blockCount, remainSize, remain8, remain4, blockCount8, blockCount4;

	blockCount = size / blockSize;
	remain8 = size % blockSize;
	for (i = 0; i < blockCount; i++) {
		offset = i * blockSize;
		ret = rtw_writeN(adapter, (FW_8192D_START_ADDRESS + offset), 64, (bufferPtr + offset));

		if (ret == _FAIL)
			goto exit;
	}

	if (remain8) {
		offset = blockCount * blockSize;

		blockCount8 = remain8/blockSize8;
		remain4 = remain8%blockSize8;
		for (i = 0; i < blockCount8; i++) {
			ret = rtw_writeN(adapter, (FW_8192D_START_ADDRESS + offset+i*blockSize8), 8, (bufferPtr + offset+i*blockSize8));

			if (ret == _FAIL)
				goto exit;
		}

		if (remain4) {
			offset = blockCount * blockSize+blockCount8*blockSize8;
			blockCount4 = remain4/blocksize4;
			remainSize = remain8%blocksize4;

			for (i = 0; i < blockCount4; i++) {
				ret = rtw_write32(adapter, (FW_8192D_START_ADDRESS + offset+i*blocksize4), le32_to_cpu(*(__le32 *)(pu4BytePtr+ offset/4+i)));

				if (ret == _FAIL)
					goto exit;
			}

			if (remainSize) {
				offset = blockCount * blockSize+blockCount8*blockSize8+blockCount4*blocksize4;
				for (i = 0; i < remainSize; i++) {
					ret = rtw_write8(adapter, (FW_8192D_START_ADDRESS + offset + i), *(bufferPtr +offset+ i));

					if (ret == _FAIL)
						goto exit;
				}
			}

		}

	}

exit:
	return ret;
}

static int _PageWrite(struct rtw_adapter *adapter, u32 page,
		      void *buffer, u32 size)
{
	u8 value8;
	u8 u8page = (u8)(page & 0x07);

	value8 = (rtw_read8(adapter, REG_MCUFWDL+2)& 0xF8) | u8page;
	rtw_write8(adapter, REG_MCUFWDL+2, value8);
	return _BlockWrite_92d(adapter, buffer, size);
}

static int _WriteFW(struct rtw_adapter *adapter, void *buffer, u32 size)
{
	int ret = _SUCCESS;
	/*  Since we need to dynamically decide method of download fw,
	 *  we call this function to get chip version.
	 *  We can remove _ReadChipVersion from ReadadapterInfo8192C later.
	 */
	u32 pageNums, remainSize;
	u32 page, offset;
	u8 *bufferPtr = (u8*)buffer;

	pageNums = size / MAX_PAGE_SIZE;
	remainSize = size % MAX_PAGE_SIZE;

	for (page = 0; page < pageNums;  page++) {
		offset = page *MAX_PAGE_SIZE;
		ret = _PageWrite(adapter, page, (bufferPtr+offset), MAX_PAGE_SIZE);

		if (ret == _FAIL)
			goto exit;
	}
	if (remainSize) {
		offset = pageNums *MAX_PAGE_SIZE;
		page = pageNums;
		ret = _PageWrite(adapter, page, (bufferPtr+offset), remainSize);

		if (ret == _FAIL)
			goto exit;
	}
	DBG_8192D("_WriteFW Done- for Normal chip.\n");

exit:
	return ret;
}

static int _FWFreeToGo_92D(struct rtw_adapter *adapter)
{
	u32 counter = 0;
	u32 value32;
	/*  polling CheckSum report */
	do {
		value32 = rtw_read32(adapter, REG_MCUFWDL);
	} while ((counter ++ < POLLING_READY_TIMEOUT_COUNT) &&
		 (!(value32 & FWDL_ChkSum_rpt)));

	if (counter >= POLLING_READY_TIMEOUT_COUNT) {
		DBG_8192D("chksum report faill ! REG_MCUFWDL:0x%08x .\n", value32);
		return _FAIL;
	}
	DBG_8192D("Checksum report OK ! REG_MCUFWDL:0x%08x .\n", value32);

	value32 = rtw_read32(adapter, REG_MCUFWDL);
	value32 |= MCUFWDL_RDY;
	rtw_write32(adapter, REG_MCUFWDL, value32);
	return _SUCCESS;
}

void rtl8192d_FirmwareSelfReset(struct rtw_adapter *adapter)
{
	u8	u1bTmp;
	u8	Delay = 100;

	rtw_write8(adapter, REG_FSIMR, 0x00);
	/*  disable other HRCV INT to influence 8051 reset. */
	rtw_write8(adapter, REG_FWIMR, 0x20);
	/*  close mask to prevent incorrect FW write operation. */
	rtw_write8(adapter, REG_FTIMR, 0x00);

	/* 0x1cf = 0x20. Inform 8051 to reset. 2009.12.25. tynli_test */
	rtw_write8(adapter, REG_HMETFR+3, 0x20);

	u1bTmp = rtw_read8(adapter, REG_SYS_FUNC_EN+1);
	while (u1bTmp & BIT2) {
		Delay--;
		if (Delay == 0)
			break;
		rtw_udelay_os(50);
		u1bTmp = rtw_read8(adapter, REG_SYS_FUNC_EN+1);
	}

	if ((u1bTmp&BIT2) && (Delay == 0)) {
		rtw_write8(adapter, REG_FWIMR, 0x00);
		/* debug reset fail */
		pr_info("FirmwareDownload92C(): Fail! 0x1c = %x, 0x130 =>%08x, 0x134 =>%08x, 0x138 =>%08x, 0x1c4 =>%08x\n, 0x1cc =>%08x, , 0x80 =>%08x , 0x1c0 =>%08x\n",
		        rtw_read32(adapter, 0x1c), rtw_read32(adapter, 0x130),
			rtw_read32(adapter, 0x134), rtw_read32(adapter, 0x138),
			rtw_read32(adapter, 0x1c4), rtw_read32(adapter, 0x1cc),
			rtw_read32(adapter, 0x80), rtw_read32(adapter, 0x1c0));
	}
}

/*  description :polling fw ready */
static int _FWInit(struct rtw_adapter *adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u32 counter = 0;

	DBG_8192D("FW already have download\n");

	/*  polling for FW ready */
	counter = 0;
	do {
		if (!pHalData->interfaceIndex) {
			if (rtw_read8(adapter, FW_MAC0_ready) & mac0_ready) {
				DBG_8192D("Polling FW ready success!! FW_MAC0_ready:0x%x\n",
					  rtw_read8(adapter, FW_MAC0_ready));
				return _SUCCESS;
			}
			rtw_udelay_os(5);
		} else {
			if (rtw_read8(adapter, FW_MAC1_ready) &mac1_ready) {
				DBG_8192D("Polling FW ready success!! FW_MAC1_ready:0x%x\n",
					  rtw_read8(adapter, FW_MAC1_ready));
				return _SUCCESS;
			}
			rtw_udelay_os(5);
		}

	} while (counter++ < POLLING_READY_TIMEOUT_COUNT);

	if (pHalData->interfaceIndex == 0) {
		DBG_8192D("Polling FW ready fail!! MAC0 FW init not ready:0x%x\n",
			  rtw_read8(adapter, FW_MAC0_ready));
	} else {
		DBG_8192D("Polling FW ready fail!! MAC1 FW init not ready:0x%x\n",
			  rtw_read8(adapter, FW_MAC1_ready));
	}

	DBG_8192D("Polling FW ready fail!! REG_MCUFWDL:0x%x\n",
		  rtw_read32(adapter, REG_MCUFWDL));
	return _FAIL;
}

static bool get_fw_from_file(struct rtw_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct device *device = dvobj_to_dev(dvobj);
	const struct firmware *fw;
	bool rtstatus = false;
#ifdef CONFIG_WOWLAN
	const char *fw_name = "rtlwifi/rtl8192dufw_wol.bin";
#else
	const char *fw_name = "rtlwifi/rtl8192dufw.bin";
#endif /* CONFIG_WOWLAN */

	if (request_firmware(&fw, fw_name, device) || !fw) {
		pr_err("Firmware %s not available\n", fw_name);
		return false;
	}
	if (fw->size > FW_8192D_SIZE) {
		pr_err("Firmware size exceeds 0x%x. Check it.\n",
		       FW_8192D_SIZE);
		goto exit;
	}

	adapter->firmware = kmalloc(sizeof(struct rt_firmware_92d), GFP_KERNEL);
	if (!adapter->firmware) {
		goto exit;
	}
	adapter->firmware->buffer = vzalloc(fw->size);
	if (!adapter->firmware->buffer) {
		kfree(adapter->firmware);
		adapter->firmware = NULL;
		goto exit;
	}
	memcpy(adapter->firmware->buffer, fw->data, fw->size);
	adapter->firmware->length = fw->size;
	pr_info("r8192du: Loaded firmware file %s of %d bytes\n",
		fw_name, adapter->firmware->length);
	rtstatus = true;
exit:
	release_firmware(fw);
	return rtstatus;
}
#ifdef CONFIG_WOWLAN
MODULE_FIRMWARE("rtlwifi/rtl8192dufw_wol.bin");
#else
MODULE_FIRMWARE("rtlwifi/rtl8192dufw.bin");
#endif /* CONFIG_WOWLAN */

/*	Description: Download 8192D firmware code. */
int FirmwareDownload92D(struct rtw_adapter *adapter, bool bUsedWoWLANFw)
{
	int rtStatus = _SUCCESS;
	u8 writeFW_retry = 0;
	u32 fwdl_start_time;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct rt_8192d_firmware_hdr *pFwHdr = NULL;
	u8 *pFirmwareBuf;
	u32 FirmwareLen;
	u8 value;
	u32 count;
	bool bFwDownloaded = false, bFwDownloadInProcess = false;

	if (adapter->bSurpriseRemoved)
		return _FAIL;

	/* Single MAC Single PHY units break if external firmware is loaded */
	if (pHalData->MacPhyMode92D == SINGLEMAC_SINGLEPHY)
		return _SUCCESS;

 	if (!adapter->firmware || !adapter->firmware->buffer) {
		if (!get_fw_from_file(adapter)) {
			rtStatus = _FAIL;
			adapter->firmware = NULL;
			goto Exit;
		}
	}
	pFirmwareBuf = adapter->firmware->buffer;
	FirmwareLen = adapter->firmware->length;

	/*  To Check Fw header. Added by tynli. 2009.12.04. */
	pFwHdr = (struct rt_8192d_firmware_hdr *)adapter->firmware->buffer;

	pHalData->FirmwareVersion =  le16_to_cpu(pFwHdr->Version);
	pHalData->FirmwareSubVersion = pFwHdr->Subversion;

	if (IS_FW_HEADER_EXIST(pFwHdr)) {
		pFirmwareBuf = pFirmwareBuf + 32;
		FirmwareLen = FirmwareLen -32;
	}

#ifdef CONFIG_WOWLAN
	/* write 0x5 BIT(3), don't suspend to reset MAC */
	if (bUsedWoWLANFw) {
		u8 test;
		test = rtw_read8(adapter, REG_APS_FSMCO+1);
		test &= ~BIT(3);
		rtw_write8(adapter, REG_APS_FSMCO+1, test);
	}

#endif /* CONFIG_WOWLAN */
	ACQUIRE_GLOBAL_MUTEX(GlobalMutexForFwDownload);
	if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY ||
	    pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY) {
		bFwDownloaded = _IsFWDownloaded(adapter);
		if ((rtw_read8(adapter, 0x1f)&BIT5) == BIT5)
			bFwDownloadInProcess = true;
		else
			bFwDownloadInProcess = false;

		if (bFwDownloaded) {
			RELEASE_GLOBAL_MUTEX(GlobalMutexForFwDownload);
			goto Exit;
		} else if (bFwDownloadInProcess) {
			RELEASE_GLOBAL_MUTEX(GlobalMutexForFwDownload);
			for (count = 0; count < 5000; count++) {
				rtw_udelay_os(500);
				ACQUIRE_GLOBAL_MUTEX(GlobalMutexForFwDownload);
				bFwDownloaded = _IsFWDownloaded(adapter);
				if ((rtw_read8(adapter, 0x1f)&BIT5) == BIT5)
					bFwDownloadInProcess = true;
				else
					bFwDownloadInProcess = false;
				RELEASE_GLOBAL_MUTEX(GlobalMutexForFwDownload);
				if (bFwDownloaded)
					goto Exit;
				else if (!bFwDownloadInProcess)
					break;
				else
					DBG_8192D("Wait for another mac download fw\n");
			}
			ACQUIRE_GLOBAL_MUTEX(GlobalMutexForFwDownload);
			value = rtw_read8(adapter, 0x1f);
			value|= BIT5;
			rtw_write8(adapter, 0x1f, value);
			RELEASE_GLOBAL_MUTEX(GlobalMutexForFwDownload);
		} else {
			value = rtw_read8(adapter, 0x1f);
			value|= BIT5;
			rtw_write8(adapter, 0x1f, value);
			RELEASE_GLOBAL_MUTEX(GlobalMutexForFwDownload);
		}

		/*  Suggested by Filen. If 8051 is running in RAM code, driver should inform Fw to reset by itself, */
		/*  or it will cause download Fw fail. 2010.02.01. by tynli. */
		if (rtw_read8(adapter, REG_MCUFWDL)&BIT7) /* 8051 RAM code */
		{
			DBG_8192D("Firmware self reset\n");
			rtl8192d_FirmwareSelfReset(adapter);
			rtw_write8(adapter, REG_MCUFWDL, 0x00);
		}

		_FWDownloadEnable(adapter, true);
		fwdl_start_time = rtw_get_current_time();
		while (1) {
			/* reset the FWDL chksum */
			rtw_write8(adapter, REG_MCUFWDL, rtw_read8(adapter, REG_MCUFWDL)|FWDL_ChkSum_rpt);

			rtStatus = _WriteFW(adapter, pFirmwareBuf, FirmwareLen);

			if (rtStatus == _SUCCESS ||
			    (rtw_get_passing_time_ms(fwdl_start_time) > 500 &&
			    writeFW_retry++ >= 3))
				break;

			DBG_8192D("%s writeFW_retry:%u, time after fwdl_start_time:%ums\n",
				  __func__, writeFW_retry,
				  rtw_get_passing_time_ms(fwdl_start_time));
		}
		_FWDownloadEnable(adapter, false);
		if (_SUCCESS != rtStatus) {
			DBG_8192D("DL Firmware failed!\n");
			goto Exit;
		}

		ACQUIRE_GLOBAL_MUTEX(GlobalMutexForFwDownload);
		rtStatus = _FWFreeToGo_92D(adapter);
		/*  download fw over, clear 0x1f[5] */
		value = rtw_read8(adapter, 0x1f);
		value&= (~BIT5);
		rtw_write8(adapter, 0x1f, value);
		RELEASE_GLOBAL_MUTEX(GlobalMutexForFwDownload);

		if (_SUCCESS != rtStatus) {
			DBG_8192D("Firmware is not ready to run!\n");
			goto Exit;
		}
	}

Exit:
	rtStatus = _FWInit(adapter);
	return rtStatus;
}

#ifdef CONFIG_WOWLAN
void InitializeFirmwareVars92D(struct rtw_adapter *adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct pwrctrl_priv *pwrpriv;
	pwrpriv = &adapter->pwrctrlpriv;

	/*  Init Fw LPS related. */
	adapter->pwrctrlpriv.bFwCurrentInPSMode = false;

	pwrpriv->bkeepfwalive = true;
	/* Init H2C counter. by tynli. 2009.12.09. */
	pHalData->LastHMEBoxNum = 0;
}

/*  Description: Prepare some information to Fw for WoWLAN.
 *			(1) Download wowlan Fw.
 *			(2) Download RSVD page packets.
 *			(3) Enable AP offload if needed.
 */
void SetFwRelatedForWoWLAN8192DU(struct rtw_adapter *padapter, u8 bHostIsGoingtoSleep)
{
	int	status = _FAIL;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(padapter);
	u8	 bRecover = false;

	if (bHostIsGoingtoSleep) {
		/*  1. Before WoWLAN we need to re-download WoWLAN Fw. */
		status = FirmwareDownload92D(padapter, bHostIsGoingtoSleep);
		if (status != _SUCCESS) {
			DBG_8192D("ConfigFwRelatedForWoWLAN8192DU(): Re-Download Firmware failed!!\n");
			return;
		} else {
			DBG_8192D("ConfigFwRelatedForWoWLAN8192DU(): Re-Download Firmware Success !!\n");
		}

		/*  */
		/*  2. Re-Init the variables about Fw related setting. */
		/*  */
		InitializeFirmwareVars92D(padapter);
	}
}
#endif /* CONFIG_WOWLAN */

/* chnl" begins from 0. It's not a real channel. */
/* channel_info[chnl]" is a real channel. */
static u8 Hal_GetChnlGroupfromArray(u8 chnl)
{
	u8	group = 0;
	u8	channel_info[59] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122, 124, 126, 128, 130, 132, 134, 136, 138, 140, 149, 151, 153, 155, 157, 159, 161, 163, 165};

	if (channel_info[chnl] <= 3)			/*  Chanel 1-3 */
		group = 0;
	else if (channel_info[chnl] <= 9)		/*  Channel 4-9 */
		group = 1;
	else	if (channel_info[chnl] <= 14)				/*  Channel 10-14 */
		group = 2;
	/*  For TX_POWER_FOR_5G_BAND */
	else if (channel_info[chnl] <= 44)
		group = 3;
	else if (channel_info[chnl] <= 54)
		group = 4;
	else if (channel_info[chnl] <= 64)
		group = 5;
	else if (channel_info[chnl] <= 112)
		group = 6;
	else if (channel_info[chnl] <= 126)
		group = 7;
	else if (channel_info[chnl] <= 140)
		group = 8;
	else if (channel_info[chnl] <= 153)
		group = 9;
	else if (channel_info[chnl] <= 159)
		group = 10;
	else
		group = 11;

	return group;
}

void
rtl8192d_ReadChipVersion(
	struct rtw_adapter *			adapter
	)
{
	u32	value32;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	enum VERSION_8192D	ChipVersion = VERSION_TEST_CHIP_88C;

	value32 = rtw_read32(adapter, REG_SYS_CFG);
	DBG_8192D("ReadChipVersion8192D 0xF0 = 0x%x\n", value32);

	ChipVersion = (enum VERSION_8192D)(VERSION_NORMAL_CHIP_92D_SINGLEPHY | CHIP_92D);

	/* Decide TestChip or NormalChip here. */
	/* 92D's RF_type will be decided when the reg0x2c is filled. */
	if (!(value32 & 0x000f0000))
	{ /* Test or Normal Chip:  hardward id 0xf0[19:16] = 0 test chip */
		ChipVersion = VERSION_TEST_CHIP_92D_SINGLEPHY;
		DBG_8192D("TEST CHIP!!!\n");
	}
	else
	{
		ChipVersion = (enum VERSION_8192D)(ChipVersion | NORMAL_CHIP);
		DBG_8192D("Normal CHIP!!!\n");
	}

	pHalData->VersionID = ChipVersion;
}

/*  */
/*  */
/*	Channel Plan */
/*  */
/*  */

void
rtl8192d_EfuseParseChnlPlan(
	struct rtw_adapter *	adapter,
	u8*			hwinfo,
	bool		AutoLoadFail
	)
{
	adapter->mlmepriv.ChannelPlan = hal_com_get_channel_plan(
		adapter
		, hwinfo?hwinfo[EEPROM_CHANNEL_PLAN]:0xFF
		, adapter->registrypriv.channel_plan
		, RT_CHANNEL_DOMAIN_WORLD_WIDE_5G
		, AutoLoadFail
	);

	DBG_8192D("mlmepriv.ChannelPlan = 0x%02x\n" , adapter->mlmepriv.ChannelPlan);
}

/*  */
/*  */
/*	EEPROM Power index mapping */
/*  */
/*  */

static void
hal_ReadPowerValueFromPROM92D(
	struct rtw_adapter *adapter,
	struct tx_power_info *pwrInfo,
	u8*			PROMContent,
	bool			AutoLoadFail
	)
{
	u32	rfPath, eeAddr, group, offset1, offset2 = 0;
	u8	i = 0;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	memset(pwrInfo, 0, sizeof(struct tx_power_info));

	if (AutoLoadFail) {
		for (group = 0; group < CHANNEL_GROUP_MAX; group++) {
			for (rfPath = 0; rfPath < RF_PATH_MAX; rfPath++) {
				if (group < CHANNEL_GROUP_MAX_2G) {
					pwrInfo->CCKIndex[rfPath][group]		= EEPROM_Default_TxPowerLevel_2G;
					pwrInfo->HT40_1SIndex[rfPath][group]		= EEPROM_Default_TxPowerLevel_2G;
				} else {
					pwrInfo->HT40_1SIndex[rfPath][group]		= EEPROM_Default_TxPowerLevel_5G;
				}
				pwrInfo->HT40_2SIndexDiff[rfPath][group]	= EEPROM_Default_HT40_2SDiff;
				pwrInfo->HT20IndexDiff[rfPath][group]		= EEPROM_Default_HT20_Diff;
				pwrInfo->OFDMIndexDiff[rfPath][group]	= EEPROM_Default_LegacyHTTxPowerDiff;
				pwrInfo->HT40MaxOffset[rfPath][group]	= EEPROM_Default_HT40_PwrMaxOffset;
				pwrInfo->HT20MaxOffset[rfPath][group]	= EEPROM_Default_HT20_PwrMaxOffset;
			}
		}

		for (i = 0; i < 3; i++)
		{
			pwrInfo->TSSI_A_5G[i] = EEPROM_Default_TSSI;
			pwrInfo->TSSI_B_5G[i] = EEPROM_Default_TSSI;
		}
		pHalData->bNOPG = true;
		return;
	}

	/* Maybe autoload OK, buf the tx power index vlaue is not filled. */
	/* If we find it, we set it default value. */
	for (rfPath = 0; rfPath < RF_PATH_MAX; rfPath++) {
		for (group = 0; group < CHANNEL_GROUP_MAX_2G; group++) {
			eeAddr = EEPROM_CCK_TX_PWR_INX_2G + (rfPath * 3) + group;
			pwrInfo->CCKIndex[rfPath][group] =
				(PROMContent[eeAddr] == 0xFF)?(eeAddr>0x7B?EEPROM_Default_TxPowerLevel_5G:EEPROM_Default_TxPowerLevel_2G):PROMContent[eeAddr];
			if (PROMContent[eeAddr] == 0xFF)
				pHalData->bNOPG = true;
		}
	}
	for (rfPath = 0; rfPath < RF_PATH_MAX; rfPath++) {
		for (group = 0; group < CHANNEL_GROUP_MAX; group++) {
			offset1 = group / 3;
			offset2 = group % 3;
			eeAddr = EEPROM_HT40_1S_TX_PWR_INX_2G+ (rfPath * 3) + offset2 + offset1*21;
			pwrInfo->HT40_1SIndex[rfPath][group] =
				(PROMContent[eeAddr] == 0xFF)?(eeAddr>0x7B?EEPROM_Default_TxPowerLevel_5G:EEPROM_Default_TxPowerLevel_2G):PROMContent[eeAddr];
		}
	}

	/* These just for 92D efuse offset. */
	for (group = 0; group < CHANNEL_GROUP_MAX; group++) {
		for (rfPath = 0; rfPath < RF_PATH_MAX; rfPath++) {
			offset1 = group / 3;
			offset2 = group % 3;

			if (PROMContent[EEPROM_HT40_2S_TX_PWR_INX_DIFF_2G+ offset2 + offset1*21] != 0xFF)
				pwrInfo->HT40_2SIndexDiff[rfPath][group] =
					(PROMContent[EEPROM_HT40_2S_TX_PWR_INX_DIFF_2G+ offset2 + offset1*21] >> (rfPath * 4)) & 0xF;
			else
				pwrInfo->HT40_2SIndexDiff[rfPath][group]	= EEPROM_Default_HT40_2SDiff;

			if (PROMContent[EEPROM_HT20_TX_PWR_INX_DIFF_2G + offset2 + offset1*21] != 0xFF)
			{
				pwrInfo->HT20IndexDiff[rfPath][group] =
					(PROMContent[EEPROM_HT20_TX_PWR_INX_DIFF_2G+ offset2 + offset1*21] >> (rfPath * 4)) & 0xF;
				if (pwrInfo->HT20IndexDiff[rfPath][group] & BIT3)	/* 4bit sign number to 8 bit sign number */
					pwrInfo->HT20IndexDiff[rfPath][group] |= 0xF0;
			}
			else
			{
				pwrInfo->HT20IndexDiff[rfPath][group]		= EEPROM_Default_HT20_Diff;
			}

			if (PROMContent[EEPROM_OFDM_TX_PWR_INX_DIFF_2G + offset2 + offset1*21] != 0xFF)
				pwrInfo->OFDMIndexDiff[rfPath][group] =
					(PROMContent[EEPROM_OFDM_TX_PWR_INX_DIFF_2G + offset2 + offset1*21] >> (rfPath * 4)) & 0xF;
			else
				pwrInfo->OFDMIndexDiff[rfPath][group]	= EEPROM_Default_LegacyHTTxPowerDiff;

			if (PROMContent[EEPROM_HT40_MAX_PWR_OFFSET_2G + offset2 + offset1*21] != 0xFF)
				pwrInfo->HT40MaxOffset[rfPath][group] =
					(PROMContent[EEPROM_HT40_MAX_PWR_OFFSET_2G + offset2 + offset1*21] >> (rfPath * 4)) & 0xF;
			else
				pwrInfo->HT40MaxOffset[rfPath][group]	= EEPROM_Default_HT40_PwrMaxOffset;

			if (PROMContent[EEPROM_HT20_MAX_PWR_OFFSET_2G + offset2 + offset1*21] != 0xFF)
				pwrInfo->HT20MaxOffset[rfPath][group] =
					(PROMContent[EEPROM_HT20_MAX_PWR_OFFSET_2G + offset2 + offset1*21] >> (rfPath * 4)) & 0xF;
			else
				pwrInfo->HT20MaxOffset[rfPath][group]	= EEPROM_Default_HT20_PwrMaxOffset;

		}
	}

	if (PROMContent[EEPROM_TSSI_A_5G] != 0xFF) {
		/* 5GL */
		pwrInfo->TSSI_A_5G[0] = PROMContent[EEPROM_TSSI_A_5G] & 0x3F;	/* 0:5] */
		pwrInfo->TSSI_B_5G[0] = PROMContent[EEPROM_TSSI_B_5G] & 0x3F;

		/* 5GM */
		pwrInfo->TSSI_A_5G[1] = PROMContent[EEPROM_TSSI_AB_5G] & 0x3F;
		pwrInfo->TSSI_B_5G[1] = (PROMContent[EEPROM_TSSI_AB_5G] & 0xC0) >> 6 |
							(PROMContent[EEPROM_TSSI_AB_5G+1] & 0x0F) << 2;

		/* 5GH */
		pwrInfo->TSSI_A_5G[2] = (PROMContent[EEPROM_TSSI_AB_5G+1] & 0xF0) >> 4 |
							(PROMContent[EEPROM_TSSI_AB_5G+2] & 0x03) << 4;
		pwrInfo->TSSI_B_5G[2] = (PROMContent[EEPROM_TSSI_AB_5G+2] & 0xFC) >> 2;
	}
	else
	{
		for (i = 0; i < 3; i++)
		{
			pwrInfo->TSSI_A_5G[i] = EEPROM_Default_TSSI;
			pwrInfo->TSSI_B_5G[i] = EEPROM_Default_TSSI;
		}
	}
}

void
rtl8192d_ReadTxPowerInfo(
	struct rtw_adapter *		adapter,
	u8*			PROMContent,
	bool			AutoLoadFail
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct tx_power_info pwrInfo;
	u32			rfPath, ch, group;
	u8			pwr, diff, tempval[2], i;

	hal_ReadPowerValueFromPROM92D(adapter, &pwrInfo, PROMContent, AutoLoadFail);

	if (!AutoLoadFail)
	{
		pHalData->EEPROMRegulatory = (PROMContent[EEPROM_RF_OPT1]&0x7);	/* bit0~2 */
		pHalData->EEPROMThermalMeter = PROMContent[EEPROM_THERMAL_METER]&0x1f;
		pHalData->CrystalCap = PROMContent[EEPROM_XTAL_K];
		tempval[0] = PROMContent[EEPROM_IQK_DELTA]&0x03;
		tempval[1] = (PROMContent[EEPROM_LCK_DELTA]&0x0C) >> 2;
		pHalData->bTXPowerDataReadFromEEPORM = true;
		if (IS_92D_D_CUT(pHalData->VersionID)||IS_92D_E_CUT(pHalData->VersionID))
		{
			pHalData->InternalPA5G[0] = !((PROMContent[EEPROM_TSSI_A_5G] & BIT6) >> 6);
			pHalData->InternalPA5G[1] = !((PROMContent[EEPROM_TSSI_B_5G] & BIT6) >> 6);
			DBG_8192D("Is D/E cut, Internal PA0 %d Internal PA1 %d\n", pHalData->InternalPA5G[0], pHalData->InternalPA5G[1]);
		}
		pHalData->EEPROMC9 = PROMContent[EEPROM_RF_OPT6];
		pHalData->EEPROMCC = PROMContent[EEPROM_RF_OPT7];
	}
	else
	{
		pHalData->EEPROMRegulatory = 0;
		pHalData->EEPROMThermalMeter = EEPROM_Default_ThermalMeter;
		pHalData->CrystalCap = EEPROM_Default_CrystalCap;
		tempval[0] = tempval[1] = 3;
	}

	pHalData->PAMode = PA_MODE_INTERNAL_SP3T;

	if (pHalData->EEPROMC9 == 0xFF || AutoLoadFail)
	{
		switch (pHalData->PAMode)
		{
			/* external pa */
			case 0:
				pHalData->EEPROMC9 = EEPROM_Default_externalPA_C9;
				pHalData->EEPROMCC = EEPROM_Default_externalPA_CC;
				pHalData->InternalPA5G[0] = false;
				pHalData->InternalPA5G[1] = false;
				break;

			/*  internal pa - SP3T */
			case 1:
				pHalData->EEPROMC9 = EEPROM_Default_internalPA_SP3T_C9;
				pHalData->EEPROMCC = EEPROM_Default_internalPA_SP3T_CC;
				pHalData->InternalPA5G[0] = true;
				pHalData->InternalPA5G[1] = true;
				break;

			/* intermal pa = SPDT */
			case 2:
				pHalData->EEPROMC9 = EEPROM_Default_internalPA_SPDT_C9;
				pHalData->EEPROMCC = EEPROM_Default_internalPA_SPDT_CC;
				pHalData->InternalPA5G[0] = true;
				pHalData->InternalPA5G[1] = true;
				break;

			default:
				break;
		}
	}
	DBG_8192D("PHY_SetPAMode mode %d, c9 = 0x%x cc = 0x%x interface index %d\n", pHalData->PAMode, pHalData->EEPROMC9, pHalData->EEPROMCC, pHalData->interfaceIndex);

	/* Use default value to fill parameters if efuse is not filled on some place. */

	/*  ThermalMeter from EEPROM */
	if (pHalData->EEPROMThermalMeter < 0x06 || pHalData->EEPROMThermalMeter > 0x1c)
		pHalData->EEPROMThermalMeter = 0x12;

	pdmpriv->ThermalMeter[0] = pHalData->EEPROMThermalMeter;

	/* check XTAL_K */
	if (pHalData->CrystalCap == 0xFF)
		pHalData->CrystalCap = 0;

	if (pHalData->EEPROMRegulatory >3)
		pHalData->EEPROMRegulatory = 0;

	for (i = 0; i < 2; i++)
	{
		switch (tempval[i])
		{
			case 0:
				tempval[i] = 2;
				break;

			case 1:
				tempval[i] = 4;
				break;

			case 2:
				tempval[i] = 6;
				break;

			case 3:
			default:
				tempval[i] = 0;
				break;
		}
	}

	/* this is suggested by Mimic, every 4 steps to redo IQK, every 7 steps to redo LCK */
	pdmpriv->Delta_IQK = 4;
	pdmpriv->Delta_LCK = 7;
	if (pHalData->EEPROMC9 == 0xFF)
		pHalData->EEPROMC9 = 0x00;

	for (rfPath = 0; rfPath < RF_PATH_MAX; rfPath++) {
		for (ch = 0; ch < CHANNEL_MAX_NUMBER; ch++) {
			group = Hal_GetChnlGroupfromArray((u8)ch);

			if (ch < CHANNEL_MAX_NUMBER_2G)
				pHalData->TxPwrLevelCck[rfPath][ch]		= pwrInfo.CCKIndex[rfPath][group];
			pHalData->TxPwrLevelHT40_1S[rfPath][ch]	= pwrInfo.HT40_1SIndex[rfPath][group];

			pHalData->TxPwrHt20Diff[rfPath][ch]		= pwrInfo.HT20IndexDiff[rfPath][group];
			pHalData->TxPwrLegacyHtDiff[rfPath][ch]	= pwrInfo.OFDMIndexDiff[rfPath][group];
			pHalData->PwrGroupHT20[rfPath][ch]		= pwrInfo.HT20MaxOffset[rfPath][group];
			pHalData->PwrGroupHT40[rfPath][ch]		= pwrInfo.HT40MaxOffset[rfPath][group];

			pwr		= pwrInfo.HT40_1SIndex[rfPath][group];
			diff	= pwrInfo.HT40_2SIndexDiff[rfPath][group];

			pHalData->TxPwrLevelHT40_2S[rfPath][ch]  = (pwr > diff) ? (pwr - diff) : 0;
		}
	}

#if DBG

	for (rfPath = 0; rfPath < RF_PATH_MAX; rfPath++) {
		for (ch = 0; ch < CHANNEL_MAX_NUMBER; ch++) {
			if (ch < CHANNEL_MAX_NUMBER_2G)
			{
				DBG_8192D("RF(%d)-Ch(%d) [CCK / HT40_1S / HT40_2S] = [0x%x / 0x%x / 0x%x]\n",
					rfPath, ch,
					pHalData->TxPwrLevelCck[rfPath][ch],
					pHalData->TxPwrLevelHT40_1S[rfPath][ch],
					pHalData->TxPwrLevelHT40_2S[rfPath][ch]);
			}
			else
			{
				DBG_8192D("RF(%d)-Ch(%d) [HT40_1S / HT40_2S] = [0x%x / 0x%x]\n",
					rfPath, ch,
					pHalData->TxPwrLevelHT40_1S[rfPath][ch],
					pHalData->TxPwrLevelHT40_2S[rfPath][ch]);
			}
		}
	}

	for (ch = 0; ch < CHANNEL_MAX_NUMBER; ch++) {
		DBG_8192D("RF-A Ht20 to HT40 Diff[%d] = 0x%x\n", ch, pHalData->TxPwrHt20Diff[RF_PATH_A][ch]);
	}

	for (ch = 0; ch < CHANNEL_MAX_NUMBER; ch++) {
		DBG_8192D("RF-A Legacy to Ht40 Diff[%d] = 0x%x\n", ch, pHalData->TxPwrLegacyHtDiff[RF_PATH_A][ch]);
	}

	for (ch = 0; ch < CHANNEL_MAX_NUMBER; ch++) {
		DBG_8192D("RF-B Ht20 to HT40 Diff[%d] = 0x%x\n", ch, pHalData->TxPwrHt20Diff[RF_PATH_B][ch]);
	}

	for (ch = 0; ch < CHANNEL_MAX_NUMBER; ch++) {
		DBG_8192D("RF-B Legacy to HT40 Diff[%d] = 0x%x\n", ch, pHalData->TxPwrLegacyHtDiff[RF_PATH_B][ch]);
	}

#endif
}

/*  */
/*	Description: */
/*		Reset Dual Mac Mode Switch related settings */
/*  */
/*	Assumption: */
/*  */
void rtl8192d_ResetDualMacSwitchVariables(
		struct rtw_adapter *			adapter
)
{
}

u8 GetEEPROMSize8192D(struct rtw_adapter * adapter)
{
	u8	size = 0;
	u32	curRCR;

	curRCR = rtw_read16(adapter, REG_9346CR);
	size = (curRCR & BOOT_FROM_EEPROM) ? 6 : 4; /*  6: EEPROM used is 93C46, 4: boot from E-Fuse. */

	pr_info("r8192du: EEPROM type is %s", size == 4 ? "E-FUSE" : "93C46");

	return size;
}

/************************************************************
Function: Synchrosize for power off with dual mac
*************************************************************/
bool
PHY_CheckPowerOffFor8192D(
	struct rtw_adapter *   adapter
)
{
	u8 u1bTmp;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	if (pHalData->MacPhyMode92D == SINGLEMAC_SINGLEPHY) {
		u1bTmp = rtw_read8(adapter, REG_MAC0);
		rtw_write8(adapter, REG_MAC0, u1bTmp&(~MAC0_ON));
		return true;
	}

	ACQUIRE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);
	if (pHalData->interfaceIndex == 0) {
		u1bTmp = rtw_read8(adapter, REG_MAC0);
		rtw_write8(adapter, REG_MAC0, u1bTmp&(~MAC0_ON));
		u1bTmp = rtw_read8(adapter, REG_MAC1);
		u1bTmp &= MAC1_ON;

	} else {
		u1bTmp = rtw_read8(adapter, REG_MAC1);
		rtw_write8(adapter, REG_MAC1, u1bTmp&(~MAC1_ON));
		u1bTmp = rtw_read8(adapter, REG_MAC0);
		u1bTmp &= MAC0_ON;
	}

	if (u1bTmp)
	{
		RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);
		return false;
	}

	u1bTmp = rtw_read8(adapter, REG_POWER_OFF_IN_PROCESS);
	u1bTmp|= BIT7;
	rtw_write8(adapter, REG_POWER_OFF_IN_PROCESS, u1bTmp);

	RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);
	return true;
}

/************************************************************
Function: Synchrosize for power off/on with dual mac
*************************************************************/
void
PHY_SetPowerOnFor8192D(
	struct rtw_adapter *adapter
)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8	value8;
	u16	i;

	/*  notice fw know band status  0x81[1]/0x53[1] = 0: 5G, 1: 2G */
	if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
	{
		value8 = rtw_read8(adapter, (pHalData->interfaceIndex == 0?REG_MAC0:REG_MAC1));
		value8 |= BIT1;
		rtw_write8(adapter, (pHalData->interfaceIndex == 0?REG_MAC0:REG_MAC1), value8);
	}
	else
	{
		value8 = rtw_read8(adapter, (pHalData->interfaceIndex == 0?REG_MAC0:REG_MAC1));
		value8 &= (~BIT1);
		rtw_write8(adapter, (pHalData->interfaceIndex == 0?REG_MAC0:REG_MAC1), value8);
	}

	if (pHalData->MacPhyMode92D == SINGLEMAC_SINGLEPHY)
	{
		value8 = rtw_read8(adapter, REG_MAC0);
		rtw_write8(adapter, REG_MAC0, value8|MAC0_ON);
	}
	else
	{
		ACQUIRE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);
		if (pHalData->interfaceIndex == 0)
		{
			value8 = rtw_read8(adapter, REG_MAC0);
			rtw_write8(adapter, REG_MAC0, value8|MAC0_ON);
		}
		else
		{
			value8 = rtw_read8(adapter, REG_MAC1);
			rtw_write8(adapter, REG_MAC1, value8|MAC1_ON);
		}
		value8 = rtw_read8(adapter, REG_POWER_OFF_IN_PROCESS);
		RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);

		for (i = 0;i<200;i++)
		{
			if ((value8&BIT7) == 0)
			{
				break;
			}
			else
			{
				rtw_udelay_os(500);
				ACQUIRE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);
				value8 = rtw_read8(adapter, REG_POWER_OFF_IN_PROCESS);
				RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);
			}
		}

		if (i == 200)
			DBG_8192D("Another mac power off over time\n");
	}
}

void rtl8192d_free_hal_data(struct rtw_adapter * padapter)
{

	DBG_8192D("===== rtl8192du_free_hal_data =====\n");

	kfree(padapter->HalData);
#ifdef CONFIG_DUALMAC_CONCURRENT
	GlobalFirstConfigurationForNormalChip = true;
#endif

}

/*  */
/*				Efuse related code */
/*  */
enum{
		VOLTAGE_V25						= 0x03,
		LDOE25_SHIFT						= 28 ,
	};

static void rtl8192d_EfusePowerSwitch(struct rtw_adapter *adapter, u8 bWrite,
				      u8 PwrState)
{
	u8	tempval;
	u16	tmpV16;

	if (PwrState) {
		/*  1.2V Power: From VDDON with Power Cut(0x0000h[15]), defualt valid */
		tmpV16 = rtw_read16(adapter, REG_SYS_ISO_CTRL);
		if (!(tmpV16 & PWC_EV12V)) {
			tmpV16 |= PWC_EV12V;
			 rtw_write16(adapter, REG_SYS_ISO_CTRL, tmpV16);
		}
		/*  Reset: 0x0000h[28], default valid */
		tmpV16 = rtw_read16(adapter, REG_SYS_FUNC_EN);
		if (!(tmpV16 & FEN_ELDR)) {
			tmpV16 |= FEN_ELDR;
			rtw_write16(adapter, REG_SYS_FUNC_EN, tmpV16);
		}

		/*  Clock: Gated(0x0008h[5]) 8M(0x0008h[1]) clock from ANA, default valid */
		tmpV16 = rtw_read16(adapter, REG_SYS_CLKR);
		if ((!(tmpV16 & LOADER_CLK_EN)) || (!(tmpV16 & ANA8M))) {
			tmpV16 |= (LOADER_CLK_EN | ANA8M);
			rtw_write16(adapter, REG_SYS_CLKR, tmpV16);
		}

		if (bWrite == true) {
			/*  Enable LDO 2.5V before read/write action */
			tempval = rtw_read8(adapter, EFUSE_TEST+3);
			tempval &= 0x0F;
			tempval |= (VOLTAGE_V25 << 4);
			rtw_write8(adapter, EFUSE_TEST+3, (tempval | 0x80));
		}
	} else {
		if (bWrite == true) {
			/*  Disable LDO 2.5V after read/write action */
			tempval = rtw_read8(adapter, EFUSE_TEST+3);
			rtw_write8(adapter, EFUSE_TEST+3, (tempval & 0x7F));
		}
	}
}

static void ReadEFuse_RTL8192D(struct rtw_adapter *adapter, u8 efuseType,
			       u16 _offset, u16 _size_byte, u8 *pbuf,
			       bool bPseudoTest)
{
	u8	efuseTbl[EFUSE_MAP_LEN];
	u8	rtemp8[1];
	u16	eFuse_Addr = 0;
	u8	offset, wren;
	u16  i, j;
	u16	eFuseWord[EFUSE_MAX_SECTION][EFUSE_MAX_WORD_UNIT];
	u16	efuse_utilized = 0;
	u8	u1temp = 0;

	/*  Do NOT excess total size of EFuse table. Added by Roger, 2008.11.10. */
	if ((_offset + _size_byte)>EFUSE_MAP_LEN) {
		/*  total E-Fuse table is 128bytes */
		DBG_8192D("ReadEFuse(): Invalid offset(%#x) with read bytes(%#x)!!\n", _offset, _size_byte);
		return;
	}

	/*  0. Refresh efuse init map as all oxFF. */
	for (i = 0; i < EFUSE_MAX_SECTION; i++)
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++)
			eFuseWord[i][j] = 0xFFFF;
	/*  1. Read the first byte to check if efuse is empty!!! */
	ReadEFuseByte(adapter, eFuse_Addr, rtemp8, bPseudoTest);
	if (*rtemp8 != 0xFF) {
		efuse_utilized++;
		eFuse_Addr++;
	} else {
		return;
	}

	/*  2. Read real efuse content. Filter PG header and every section data. */
	while ((*rtemp8 != 0xFF) && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN)) {
		/*  Check PG header for section num. */
		if ((*rtemp8 & 0x1F) == 0x0F) {		/* extended header */
			u1temp = ((*rtemp8 & 0xE0) >> 5);

			ReadEFuseByte(adapter, eFuse_Addr, rtemp8, bPseudoTest);

			if ((*rtemp8 & 0x0F) == 0x0F) {
				eFuse_Addr++;
				ReadEFuseByte(adapter, eFuse_Addr, rtemp8, bPseudoTest);

				if (*rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN))
					eFuse_Addr++;
				continue;
			} else {
				offset = ((*rtemp8 & 0xF0) >> 1) | u1temp;
				wren = (*rtemp8 & 0x0F);
				eFuse_Addr++;
			}
		} else {
			offset = ((*rtemp8 >> 4) & 0x0f);
			wren = (*rtemp8 & 0x0f);
		}

		if (offset < EFUSE_MAX_SECTION) {
			/*  Get word enable value from PG header */

			for (i = 0; i<EFUSE_MAX_WORD_UNIT; i++) {
				/*  Check word enable condition in the section */
				if (!(wren & 0x01)) {
					ReadEFuseByte(adapter, eFuse_Addr, rtemp8, bPseudoTest);
					eFuse_Addr++;
					efuse_utilized++;
					eFuseWord[offset][i] = (*rtemp8 & 0xff);
					if (eFuse_Addr >= EFUSE_REAL_CONTENT_LEN)
						break;
					ReadEFuseByte(adapter, eFuse_Addr, rtemp8, bPseudoTest);
					eFuse_Addr++;
					efuse_utilized++;
					eFuseWord[offset][i] |= (((u16)*rtemp8 << 8) & 0xff00);

					if (eFuse_Addr >= EFUSE_REAL_CONTENT_LEN)
						break;
				}
				wren >>= 1;
			}
		}

		/*  Read next PG header */
		ReadEFuseByte(adapter, eFuse_Addr, rtemp8, bPseudoTest);

		if (*rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN)) {
			efuse_utilized++;
			eFuse_Addr++;
		}
	}

	/*  3. Collect 16 sections and 4 word unit into Efuse map. */
	for (i = 0; i<EFUSE_MAX_SECTION; i++) {
		for (j = 0; j<EFUSE_MAX_WORD_UNIT; j++) {
			efuseTbl[(i*8)+(j*2)]= (eFuseWord[i][j] & 0xff);
			efuseTbl[(i*8)+((j*2)+1)]= ((eFuseWord[i][j] >> 8) & 0xff);
		}
	}
	/*  4. Copy from Efuse map to output pointer memory!!! */
	for (i = 0; i<_size_byte; i++)
		pbuf[i] = efuseTbl[_offset+i];
	/*  5. Calculate Efuse utilization. */
	rtw_hal_set_hwreg(adapter, HW_VAR_EFUSE_BYTES, (u8*)&efuse_utilized);
}

static void hal_EfuseUpdateNormalChipVersion_92D(struct rtw_adapter *adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	enum VERSION_8192D	ChipVer = pHalData->VersionID;
	u8	CutValue[2];
	u16	ChipValue = 0;

	ReadEFuseByte(adapter, EEPROME_CHIP_VERSION_H, &CutValue[1], false);
	ReadEFuseByte(adapter, EEPROME_CHIP_VERSION_L, &CutValue[0], false);

	ChipValue = (CutValue[1] << 8) | CutValue[0];
	switch (ChipValue) {
	case 0xAA55:
		ChipVer = (enum VERSION_8192D)(ChipVer | C_CUT_VERSION);
		pr_cont(", C-CUT chip\n");
		break;
	case 0x9966:
		ChipVer = (enum VERSION_8192D)(ChipVer | D_CUT_VERSION);
		pr_cont(", D-CUT chip\n");
		break;
	case 0xCC33:
		ChipVer = (enum VERSION_8192D)(ChipVer | E_CUT_VERSION);
		pr_cont(", E-CUT chip\n");
		break;
	default:
		ChipVer = (enum VERSION_8192D)(ChipVer | D_CUT_VERSION);
		pr_cont(", Unknown CUT chip\n");
		break;
	}
	pHalData->VersionID = ChipVer;
}

static bool hal_EfuseMacMode_ISVS_92D(struct rtw_adapter *adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8	PartNo;
	bool bResult = false;

	/*  92D VS not support dual mac mode */
	if (IS_NORMAL_CHIP92D(pHalData->VersionID)) {
		ReadEFuseByte(adapter, EEPROM_DEF_PART_NO, &PartNo, false);

		if ((((PartNo & 0xc0) ==  PARTNO_92D_NIC)&&((PartNo & 0x0c) == PARTNO_SINGLE_BAND_VS))||
			(((PartNo & 0xF0) == PARTNO_92D_NIC_REMARK) &&((PartNo & 0x0F) == PARTNO_SINGLE_BAND_VS_REMARK)))
		{
			bResult = true;
		} else if (PartNo == 0x00) {
			ReadEFuseByte(adapter, EEPROM_DEF_PART_NO+1, &PartNo, false);
			if ((((PartNo & 0xc0) ==  PARTNO_92D_NIC)&&((PartNo & 0x0c) == PARTNO_SINGLE_BAND_VS))||
				(((PartNo & 0xF0) == PARTNO_92D_NIC_REMARK) &&((PartNo & 0x0F) == PARTNO_SINGLE_BAND_VS_REMARK)))
			{
				bResult = true;
			}
		}
	}
	return bResult;
}

static void rtl8192d_ReadEFuse(struct rtw_adapter *adapter, u8 efuseType,
			       u16 _offset, u16 _size_byte, u8 *pbuf,
			       bool bPseudoTest)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	ReadEFuse_RTL8192D(adapter, efuseType, _offset, _size_byte, pbuf, bPseudoTest);

	hal_EfuseUpdateNormalChipVersion_92D(adapter);
	pHalData->bIsVS = hal_EfuseMacMode_ISVS_92D(adapter);
}

static void rtl8192d_EFUSE_GetEfuseDefinition(struct rtw_adapter *adapter,
					      u8 efuseType, u8 type,
					      void **pOut, bool bPseudoTest)
{
	switch (type) {
	case TYPE_EFUSE_MAX_SECTION:
		{
			u8	*pMax_section;
			pMax_section = (u8 *)pOut;
			*pMax_section = EFUSE_MAX_SECTION;
		}
		break;
	case TYPE_EFUSE_REAL_CONTENT_LEN:
		{
			u16 *pu2Tmp;
			pu2Tmp = (u16 *)pOut;
			*pu2Tmp = EFUSE_REAL_CONTENT_LEN;
		}
		break;
	case TYPE_AVAILABLE_EFUSE_BYTES_BANK:
		{
			u16	*pu2Tmp;
			pu2Tmp = (u16 *)pOut;
			*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN-EFUSE_OOB_PROTECT_BYTES);
		}
		break;
	case TYPE_AVAILABLE_EFUSE_BYTES_TOTAL:
		{
			u16 *pu2Tmp;
			pu2Tmp = (u16 *)pOut;
			*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN-EFUSE_OOB_PROTECT_BYTES);
		}
		break;
	case TYPE_EFUSE_MAP_LEN:
		{
			u16 *pu2Tmp;
			pu2Tmp = (u16 *)pOut;
			*pu2Tmp = (u16)EFUSE_MAP_LEN;
		}
		break;
	case TYPE_EFUSE_PROTECT_BYTES_BANK:
		{
			u8 *pu1Tmp;
			pu1Tmp = (u8 *)pOut;
			*pu1Tmp = (u8)(EFUSE_OOB_PROTECT_BYTES);
		}
		break;
	default: {
		u8 *pu1Tmp;
		pu1Tmp = (u8 *)pOut;
		*pu1Tmp = 0;
		break; }
	}
}

static u16 rtl8192d_EfuseGetCurrentSize(struct rtw_adapter *adapter,
					u8 efuseType, bool bPseudoTest)
{
	int	bContinual = true;
	u16	efuse_addr = 0;
	u8	hoffset = 0, hworden = 0;
	u8	efuse_data, word_cnts = 0;

	while (bContinual &&
	       efuse_OneByteRead(adapter, efuse_addr, &efuse_data, bPseudoTest) &&
	       (efuse_addr  < EFUSE_REAL_CONTENT_LEN)) {
		if (efuse_data!= 0xFF) {
			if ((efuse_data&0x1F) == 0x0F) {		/* extended header */
				hoffset = efuse_data;
				efuse_addr++;
				efuse_OneByteRead(adapter, efuse_addr, &efuse_data, bPseudoTest);
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
		} else {
			bContinual = false;
		}
	}
	return efuse_addr;
}

static int
rtl8192d_Efuse_PgPacketRead(	struct rtw_adapter *	adapter,
					u8			offset,
					u8			*data,
					bool		bPseudoTest)
{
	u8	ReadState = PG_STATE_HEADER;
	int	bContinual = true;
	int	bDataEmpty = true;
	u8	efuse_data, word_cnts = 0;
	u16	efuse_addr = 0;
	u8	hoffset = 0, hworden = 0;
	u8	tmpidx = 0;
	u8	tmpdata[8];
	u8	tmp_header = 0;

	if (data == NULL)
		return false;
	if (offset >= EFUSE_MAX_SECTION)
		return false;

	memset((void *)data, 0xff, sizeof(u8)*PGPKT_DATA_SIZE);
	memset((void *)tmpdata, 0xff, sizeof(u8)*PGPKT_DATA_SIZE);

	/*  <Roger_TODO> Efuse has been pre-programmed dummy 5Bytes at the end of Efuse by CP. */
	/*  Skip dummy parts to prevent unexpected data read from Efuse. */
	/*  By pass right now. 2009.02.19. */
	while (bContinual && (efuse_addr  < EFUSE_REAL_CONTENT_LEN)) {
		/*   Header Read ------------- */
		if (ReadState & PG_STATE_HEADER) {
			if (efuse_OneByteRead(adapter, efuse_addr, &efuse_data, bPseudoTest) &&
			    (efuse_data!= 0xFF)) {
				if ((efuse_data & 0x1F) == 0x0F) {
					tmp_header = efuse_data;
					efuse_addr++;
					efuse_OneByteRead(adapter, efuse_addr, &efuse_data, bPseudoTest);
					if ((efuse_data & 0x0F) != 0x0F) {
						hoffset = ((tmp_header & 0xE0) >> 5) |
							  ((efuse_data & 0xF0) >> 1);
						hworden = efuse_data & 0x0F;
					} else {
						efuse_addr++;
						continue;
					}
				} else {
					hoffset = (efuse_data>>4) & 0x0F;
					hworden =  efuse_data & 0x0F;
				}
				word_cnts = Efuse_CalculateWordCnts(hworden);
				bDataEmpty = true;

				if (hoffset == offset) {
					for (tmpidx = 0; tmpidx < word_cnts*2; tmpidx++) {
						if (efuse_OneByteRead(adapter, efuse_addr+1+tmpidx, &efuse_data, bPseudoTest)) {
							tmpdata[tmpidx] = efuse_data;
							if (efuse_data!= 0xff)
								bDataEmpty = false;
						}
					}
					if (bDataEmpty == false) {
						ReadState = PG_STATE_DATA;
					} else {/* read next header */
						efuse_addr = efuse_addr + (word_cnts*2)+1;
						ReadState = PG_STATE_HEADER;
					}
				} else {/* read next header */
					efuse_addr = efuse_addr + (word_cnts*2)+1;
					ReadState = PG_STATE_HEADER;
				}

			} else {
				bContinual = false;
			}
		}
		/*   Data section Read ------------- */
		else if (ReadState & PG_STATE_DATA) {
			efuse_WordEnableDataRead(hworden, tmpdata, data);
			efuse_addr = efuse_addr + (word_cnts*2)+1;
			ReadState = PG_STATE_HEADER;
		}
	}

	if ((data[0]== 0xff) &&(data[1]== 0xff) && (data[2]== 0xff)  && (data[3]== 0xff) &&
	    (data[4]== 0xff) &&(data[5]== 0xff) && (data[6]== 0xff)  && (data[7]== 0xff))
		return false;
	else
		return true;
}

static int
rtl8192d_Efuse_PgPacketWrite(struct rtw_adapter *	adapter,
					u8			offset,
					u8			word_en,
					u8			*data,
					bool		bPseudoTest)
{
	u8	WriteState = PG_STATE_HEADER;
	int	bContinual = true, bDataEmpty = true, bResult = true;
	u16	efuse_addr = 0;
	u8	efuse_data;
	u8	pg_header = 0, pg_header_temp = 0;
	u8	tmp_word_cnts = 0, target_word_cnts = 0;
	u8	tmp_header, match_word_en, tmp_word_en;
	struct pg_pkt_struct_a target_pkt;
	struct pg_pkt_struct_a tmp_pkt = {
		.offset = 0,
		.word_en = 0,
		.data ={0, 0, 0, 0, 0, 0, 0, 0},
		.word_cnts = 0
	};
	u8	originaldata[sizeof(u8)*8];
	u8	tmpindex = 0, badworden = 0x0F;
	static int	repeat_times;
	bool		bExtendedHeader = false;
	u8	efuseType = EFUSE_WIFI;

	/*  */
	/*  <Roger_Notes> Efuse has been pre-programmed dummy 5Bytes at the end of Efuse by CP. */
	/*  So we have to prevent unexpected data string connection, which will cause */
	/*  incorrect data auto-load from HW. The total size is equal or smaller than 498bytes */
	/*  (i.e., offset 0~497, and dummy 1bytes) expected after CP test. */
	/*  2009.02.19. */
	/*  */
	if (Efuse_GetCurrentSize(adapter, efuseType, bPseudoTest) >= (EFUSE_REAL_CONTENT_LEN-EFUSE_OOB_PROTECT_BYTES))
		return false;

	/*  Init the 8 bytes content as 0xff */
	target_pkt.offset = offset;
	target_pkt.word_en = word_en;

	memset((void *)target_pkt.data, 0xFF, sizeof(u8)*8);

	efuse_WordEnableDataRead(word_en, data, target_pkt.data);
	target_word_cnts = Efuse_CalculateWordCnts(target_pkt.word_en);

	/*  <Roger_Notes> Efuse has been pre-programmed dummy 5Bytes at the end of Efuse by CP. */
	/*  So we have to prevent unexpected data string connection, which will cause */
	/*  incorrect data auto-load from HW. Dummy 1bytes is additional. */
	while (bContinual && (efuse_addr  < (EFUSE_REAL_CONTENT_LEN-EFUSE_OOB_PROTECT_BYTES))) {

		if (WriteState == PG_STATE_HEADER) {
			bDataEmpty = true;
			badworden = 0x0F;
			/*   so ******************* */
			if (efuse_OneByteRead(adapter, efuse_addr, &efuse_data, bPseudoTest) &&
			    (efuse_data!= 0xFF)) {
				if ((efuse_data&0x1F) == 0x0F) {		/* extended header */
					tmp_header = efuse_data;
					efuse_addr++;
					efuse_OneByteRead(adapter, efuse_addr, &efuse_data, bPseudoTest);
					if ((efuse_data & 0x0F) == 0x0F) {	/* wren fail */
						efuse_addr++;
						continue;
					} else {
						tmp_pkt.offset = ((tmp_header & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
						tmp_pkt.word_en = efuse_data & 0x0F;
					}
				} else {
					tmp_header  =  efuse_data;
					tmp_pkt.offset	= (tmp_header>>4) & 0x0F;
					tmp_pkt.word_en = tmp_header & 0x0F;
				}
				tmp_word_cnts =  Efuse_CalculateWordCnts(tmp_pkt.word_en);

				/*   so-1 ******************* */
				if (tmp_pkt.offset  != target_pkt.offset) {
					efuse_addr = efuse_addr + (tmp_word_cnts*2) +1; /* Next pg_packet */
#if (EFUSE_ERROE_HANDLE == 1)
					WriteState = PG_STATE_HEADER;
#endif
				} else {		/* write the same offset */
					/*   so-2 ******************* */
					for (tmpindex = 0; tmpindex < (tmp_word_cnts*2); tmpindex++) {
						if (efuse_OneByteRead(adapter, (efuse_addr+1+tmpindex), &efuse_data, bPseudoTest) &&
						    (efuse_data != 0xFF))
							bDataEmpty = false;
					}
					/*   so-2-1 ******************* */
					if (!bDataEmpty) {
						efuse_addr = efuse_addr + (tmp_word_cnts*2) +1; /* Next pg_packet */
#if (EFUSE_ERROE_HANDLE == 1)
						WriteState = PG_STATE_HEADER;
#endif
					} else {/*   so-2-2 ******************* */

						match_word_en = 0x0F;			/* same bit as original wren */
						if (!((target_pkt.word_en&BIT0)|(tmp_pkt.word_en&BIT0)))
							 match_word_en &= (~BIT0);
						if (!((target_pkt.word_en&BIT1)|(tmp_pkt.word_en&BIT1)))
							 match_word_en &= (~BIT1);
						if (!((target_pkt.word_en&BIT2)|(tmp_pkt.word_en&BIT2)))
							 match_word_en &= (~BIT2);
						if (!((target_pkt.word_en&BIT3)|(tmp_pkt.word_en&BIT3)))
							 match_word_en &= (~BIT3);

						/*   so-2-2-A ******************* */
						if ((match_word_en&0x0F)!= 0x0F) {
							badworden = Efuse_WordEnableDataWrite(adapter, efuse_addr+1, tmp_pkt.word_en , target_pkt.data, bPseudoTest);

							/*   so-2-2-A-1 ******************* */
							if (0x0F != (badworden&0x0F)) {
								u8	reorg_offset = offset;
								u8	reorg_worden = badworden;
								Efuse_PgPacketWrite(adapter, reorg_offset, reorg_worden, target_pkt.data, bPseudoTest);
							}

							tmp_word_en = 0x0F;		/* not the same bit as original wren */
							if ((target_pkt.word_en&BIT0)^(match_word_en&BIT0))
								tmp_word_en &= (~BIT0);
							if ((target_pkt.word_en&BIT1)^(match_word_en&BIT1))
								tmp_word_en &=  (~BIT1);
							if ((target_pkt.word_en&BIT2)^(match_word_en&BIT2))
								tmp_word_en &= (~BIT2);
							if ((target_pkt.word_en&BIT3)^(match_word_en&BIT3))
								tmp_word_en &= (~BIT3);

							/*   so-2-2-A-2 ******************* */
							if ((tmp_word_en&0x0F)!= 0x0F) {
								/* reorganize other pg packet */
								efuse_addr = Efuse_GetCurrentSize(adapter, efuseType, bPseudoTest);
								target_pkt.offset = offset;
								target_pkt.word_en = tmp_word_en;
							} else {
								bContinual = false;
							}
#if (EFUSE_ERROE_HANDLE == 1)
							WriteState = PG_STATE_HEADER;
							repeat_times++;
							if (repeat_times>EFUSE_REPEAT_THRESHOLD_) {
								bContinual = false;
								bResult = false;
							}
#endif
						} else {/*   so-2-2-B ******************* */
							/* reorganize other pg packet */
							efuse_addr = efuse_addr + (2*tmp_word_cnts) +1;/* next pg packet addr */
							target_pkt.offset = offset;
							target_pkt.word_en = target_pkt.word_en;
#if (EFUSE_ERROE_HANDLE == 1)
							WriteState = PG_STATE_HEADER;
#endif
						}
					}
				}
			} else		/*   s1: header == oxff  ******************* */
			{
				bExtendedHeader = false;

				if (target_pkt.offset >= EFUSE_MAX_SECTION_BASE) {
					pg_header = ((target_pkt.offset &0x07) << 5) | 0x0F;

					efuse_OneByteWrite(adapter, efuse_addr, pg_header, bPseudoTest);
					efuse_OneByteRead(adapter, efuse_addr, &tmp_header, bPseudoTest);

					while (tmp_header == 0xFF) {
						repeat_times++;

						if (repeat_times>EFUSE_REPEAT_THRESHOLD_) {
							bContinual = false;
							bResult = false;
							efuse_addr++;
							break;
						}
						efuse_OneByteWrite(adapter, efuse_addr, pg_header, bPseudoTest);
						efuse_OneByteRead(adapter, efuse_addr, &tmp_header, bPseudoTest);
					}

					if (!bContinual)
						break;

					if (tmp_header == pg_header) {
						efuse_addr++;
						pg_header_temp = pg_header;
						pg_header = ((target_pkt.offset & 0x78) << 1) | target_pkt.word_en;

						efuse_OneByteWrite(adapter, efuse_addr, pg_header, bPseudoTest);
						efuse_OneByteRead(adapter, efuse_addr, &tmp_header, bPseudoTest);

						while (tmp_header == 0xFF) {
							repeat_times++;

							if (repeat_times > EFUSE_REPEAT_THRESHOLD_) {
								bContinual = false;
								bResult = false;
								break;
							}
							efuse_OneByteWrite(adapter, efuse_addr, pg_header, bPseudoTest);
							efuse_OneByteRead(adapter, efuse_addr, &tmp_header, bPseudoTest);
						}

						if (!bContinual)
							break;

						if ((tmp_header & 0x0F) == 0x0F)	/* wren PG fail */
						{
							repeat_times++;

							if (repeat_times>EFUSE_REPEAT_THRESHOLD_) {
								bContinual = false;
								bResult = false;
								break;
							} else {
								efuse_addr++;
								continue;
							}
						} else if (pg_header != tmp_header)	/* offset PG fail */
						{
							bExtendedHeader = true;
							tmp_pkt.offset = ((pg_header_temp & 0xE0) >> 5) | ((tmp_header & 0xF0) >> 1);
							tmp_pkt.word_en =  tmp_header & 0x0F;
							tmp_word_cnts =  Efuse_CalculateWordCnts(tmp_pkt.word_en);
						}
					} else if ((tmp_header & 0x1F) == 0x0F)		/* wrong extended header */
					{
						efuse_addr+= 2;
						continue;
					}
				} else {
					pg_header = ((target_pkt.offset << 4)&0xf0) |target_pkt.word_en;
					efuse_OneByteWrite(adapter, efuse_addr, pg_header, bPseudoTest);
					efuse_OneByteRead(adapter, efuse_addr, &tmp_header, bPseudoTest);
				}

				if (tmp_header == pg_header) {
					/*   s1-1******************* */
					WriteState = PG_STATE_DATA;
				}
#if (EFUSE_ERROE_HANDLE == 1)
				else if (tmp_header == 0xFF) {/*   s1-3: if Write or read func doesn't work ******************* */
					/* efuse_addr doesn't change */
					WriteState = PG_STATE_HEADER;
					repeat_times++;
					if (repeat_times>EFUSE_REPEAT_THRESHOLD_) {
						bContinual = false;
						bResult = false;
					}
				}
#endif
				else {/*   s1-2 : fixed the header procedure ******************* */
					if (!bExtendedHeader) {
						tmp_pkt.offset = (tmp_header>>4) & 0x0F;
						tmp_pkt.word_en =  tmp_header & 0x0F;
						tmp_word_cnts =  Efuse_CalculateWordCnts(tmp_pkt.word_en);
					}

					/*   s1-2-A :cover the exist data ******************* */
					memset((void *)originaldata, 0xff, sizeof(u8)*8);

					if (Efuse_PgPacketRead(adapter, tmp_pkt.offset, originaldata, bPseudoTest))
					{	/* check if data exist */
						badworden = Efuse_WordEnableDataWrite(adapter, efuse_addr+1, tmp_pkt.word_en, originaldata, bPseudoTest);
						/*  */
						if (0x0F != (badworden&0x0F)) {
							u8	reorg_offset = tmp_pkt.offset;
							u8	reorg_worden = badworden;
							Efuse_PgPacketWrite(adapter, reorg_offset, reorg_worden, originaldata, bPseudoTest);
							efuse_addr = Efuse_GetCurrentSize(adapter, efuseType, bPseudoTest);
						} else {
							efuse_addr = efuse_addr + (tmp_word_cnts*2) +1; /* Next pg_packet */
						}
					}
					 /*   s1-2-B: wrong address******************* */
					else {
						efuse_addr = efuse_addr + (tmp_word_cnts*2) +1; /* Next pg_packet */
					}

#if (EFUSE_ERROE_HANDLE == 1)
					WriteState = PG_STATE_HEADER;
					repeat_times++;
					if (repeat_times>EFUSE_REPEAT_THRESHOLD_) {
						bContinual = false;
						bResult = false;
					}
#endif
				}
			}
		}
		/* write data state */
		else if (WriteState == PG_STATE_DATA) {
			/*   s1-1  ******************* */
			badworden = 0x0f;
			badworden = Efuse_WordEnableDataWrite(adapter, efuse_addr+1, target_pkt.word_en, target_pkt.data , bPseudoTest);
			if ((badworden&0x0F) == 0x0F) {
				/*   s1-1-A ******************* */
				bContinual = false;
			} else {
				/* reorganize other pg packet **  s1-1-B **/
				efuse_addr = efuse_addr + (2*target_word_cnts) +1;/* next pg packet addr */

				target_pkt.offset = offset;
				target_pkt.word_en = badworden;
				target_word_cnts =  Efuse_CalculateWordCnts(target_pkt.word_en);
#if (EFUSE_ERROE_HANDLE == 1)
				WriteState = PG_STATE_HEADER;
				repeat_times++;
				if (repeat_times>EFUSE_REPEAT_THRESHOLD_) {
					bContinual = false;
					bResult = false;
				}
#endif
			}
		}
	}
	return bResult;
}

static u8 rtl8192d_Efuse_WordEnableDataWrite(struct rtw_adapter *adapter,
					     u16 efuse_addr, u8 word_en,
					     u8 *data, bool bPseudoTest)
{
	u16	tmpaddr = 0;
	u16	start_addr = efuse_addr;
	u8	badworden = 0x0F;
	u8	tmpdata[8];

	memset((void *)tmpdata, 0xff, PGPKT_DATA_SIZE);
	if (!(word_en&BIT0)) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(adapter, start_addr++, data[0], bPseudoTest);
		efuse_OneByteWrite(adapter, start_addr++, data[1], bPseudoTest);

		efuse_OneByteRead(adapter, tmpaddr, &tmpdata[0], bPseudoTest);
		efuse_OneByteRead(adapter, tmpaddr+1, &tmpdata[1], bPseudoTest);
		if ((data[0]!= tmpdata[0])||(data[1]!= tmpdata[1])) {
			badworden &= (~BIT0);
		}
	}
	if (!(word_en&BIT1)) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(adapter, start_addr++, data[2], bPseudoTest);
		efuse_OneByteWrite(adapter, start_addr++, data[3], bPseudoTest);

		efuse_OneByteRead(adapter, tmpaddr    , &tmpdata[2], bPseudoTest);
		efuse_OneByteRead(adapter, tmpaddr+1, &tmpdata[3], bPseudoTest);
		if ((data[2]!= tmpdata[2])||(data[3]!= tmpdata[3]))
			badworden &= (~BIT1);
	}
	if (!(word_en&BIT2)) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(adapter, start_addr++, data[4], bPseudoTest);
		efuse_OneByteWrite(adapter, start_addr++, data[5], bPseudoTest);

		efuse_OneByteRead(adapter, tmpaddr, &tmpdata[4], bPseudoTest);
		efuse_OneByteRead(adapter, tmpaddr+1, &tmpdata[5], bPseudoTest);
		if ((data[4]!= tmpdata[4])||(data[5]!= tmpdata[5]))
			badworden &= (~BIT2);
	}
	if (!(word_en&BIT3)) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(adapter, start_addr++, data[6], bPseudoTest);
		efuse_OneByteWrite(adapter, start_addr++, data[7], bPseudoTest);

		efuse_OneByteRead(adapter, tmpaddr, &tmpdata[6], bPseudoTest);
		efuse_OneByteRead(adapter, tmpaddr+1, &tmpdata[7], bPseudoTest);
		if ((data[6]!= tmpdata[6])||(data[7]!= tmpdata[7]))
			badworden &= (~BIT3);
	}
	return badworden;
}

static void hal_notch_filter_8192d(struct rtw_adapter *adapter, bool enable)
{
	if (enable) {
		DBG_8192D("Enable notch filter\n");
		rtw_write8(adapter, rOFDM0_RxDSP+1, rtw_read8(adapter, rOFDM0_RxDSP+1) | BIT1);
	} else {
		DBG_8192D("Disable notch filter\n");
		rtw_write8(adapter, rOFDM0_RxDSP+1, rtw_read8(adapter, rOFDM0_RxDSP+1) & ~BIT1);
	}
}

static s32 c2h_id_filter_ccx_8192d(u8 id)
{
	s32 ret = false;
	if (id == C2H_CCX_TX_RPT)
		ret = true;

	return ret;
}

static s32 c2h_handler_8192d(struct rtw_adapter *padapter, struct c2h_evt_hdr *c2h_evt)
{
	s32 ret = _SUCCESS;
	u8 i = 0;

	if (c2h_evt == NULL) {
		DBG_8192D("%s c2h_evt is NULL\n", __func__);
		ret = _FAIL;
		goto exit;
	}

	switch (c2h_evt->id) {
	case C2H_CCX_TX_RPT:
		handle_txrpt_ccx_8192d(padapter, c2h_evt->payload);
		break;
	default:
		ret = _FAIL;
		break;
	}

exit:
	return ret;
}

void rtl8192d_set_hal_ops(struct hal_ops *pHalFunc)
{
	pHalFunc->free_hal_data = &rtl8192d_free_hal_data;

	pHalFunc->dm_init = &rtl8192d_init_dm_priv;
	pHalFunc->dm_deinit = &rtl8192d_deinit_dm_priv;
	pHalFunc->read_chip_version = &rtl8192d_ReadChipVersion;

	pHalFunc->set_bwmode_handler = &PHY_SetBWMode8192D;
	pHalFunc->set_channel_handler = &PHY_SwChnl8192D;

	pHalFunc->hal_dm_watchdog = &rtl8192d_HalDmWatchDog;

	pHalFunc->Add_RateATid = &rtl8192d_Add_RateATid;

	pHalFunc->read_bbreg = &rtl8192d_PHY_QueryBBReg;
	pHalFunc->write_bbreg = &rtl8192d_PHY_SetBBReg;
	pHalFunc->read_rfreg = &rtl8192d_PHY_QueryRFReg;
	pHalFunc->write_rfreg = &rtl8192d_PHY_SetRFReg;

	/* Efuse related function */
	pHalFunc->EfusePowerSwitch = &rtl8192d_EfusePowerSwitch;
	pHalFunc->ReadEFuse = &rtl8192d_ReadEFuse;
	pHalFunc->EFUSEGetEfuseDefinition = &rtl8192d_EFUSE_GetEfuseDefinition;
	pHalFunc->EfuseGetCurrentSize = &rtl8192d_EfuseGetCurrentSize;
	pHalFunc->Efuse_PgPacketRead = &rtl8192d_Efuse_PgPacketRead;
	pHalFunc->Efuse_PgPacketWrite = &rtl8192d_Efuse_PgPacketWrite;
	pHalFunc->Efuse_WordEnableDataWrite = &rtl8192d_Efuse_WordEnableDataWrite;

	pHalFunc->hal_notch_filter = &hal_notch_filter_8192d;

	pHalFunc->c2h_handler = c2h_handler_8192d;
	pHalFunc->c2h_id_filter_ccx = c2h_id_filter_ccx_8192d;
}
