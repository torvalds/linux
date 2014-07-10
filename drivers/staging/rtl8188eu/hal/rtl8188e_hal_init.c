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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _HAL_INIT_C_

#include <linux/firmware.h>
#include <linux/vmalloc.h>
#include <drv_types.h>
#include <rtw_efuse.h>

#include <rtl8188e_hal.h>

#include <rtw_iol.h>

void iol_mode_enable(struct adapter *padapter, u8 enable)
{
	u8 reg_0xf0 = 0;

	if (enable) {
		/* Enable initial offload */
		reg_0xf0 = usb_read8(padapter, REG_SYS_CFG);
		usb_write8(padapter, REG_SYS_CFG, reg_0xf0|SW_OFFLOAD_EN);

		if (!padapter->bFWReady) {
			DBG_88E("bFWReady == false call reset 8051...\n");
			_8051Reset88E(padapter);
		}

	} else {
		/* disable initial offload */
		reg_0xf0 = usb_read8(padapter, REG_SYS_CFG);
		usb_write8(padapter, REG_SYS_CFG, reg_0xf0 & ~SW_OFFLOAD_EN);
	}
}

s32 iol_execute(struct adapter *padapter, u8 control)
{
	s32 status = _FAIL;
	u8 reg_0x88 = 0;
	u32 start = 0, passing_time = 0;

	control = control&0x0f;
	reg_0x88 = usb_read8(padapter, REG_HMEBOX_E0);
	usb_write8(padapter, REG_HMEBOX_E0,  reg_0x88|control);

	start = jiffies;
	while ((reg_0x88 = usb_read8(padapter, REG_HMEBOX_E0)) & control &&
	       (passing_time = rtw_get_passing_time_ms(start)) < 1000) {
		;
	}

	reg_0x88 = usb_read8(padapter, REG_HMEBOX_E0);
	status = (reg_0x88 & control) ? _FAIL : _SUCCESS;
	if (reg_0x88 & control<<4)
		status = _FAIL;
	return status;
}

static s32 iol_InitLLTTable(struct adapter *padapter, u8 txpktbuf_bndy)
{
	s32 rst = _SUCCESS;
	iol_mode_enable(padapter, 1);
	usb_write8(padapter, REG_TDECTRL+1, txpktbuf_bndy);
	rst = iol_execute(padapter, CMD_INIT_LLT);
	iol_mode_enable(padapter, 0);
	return rst;
}


s32 rtl8188e_iol_efuse_patch(struct adapter *padapter)
{
	s32	result = _SUCCESS;

	DBG_88E("==> %s\n", __func__);
	if (rtw_IOL_applied(padapter)) {
		iol_mode_enable(padapter, 1);
		result = iol_execute(padapter, CMD_READ_EFUSE_MAP);
		if (result == _SUCCESS)
			result = iol_execute(padapter, CMD_EFUSE_PATCH);

		iol_mode_enable(padapter, 0);
	}
	return result;
}

static s32 iol_ioconfig(struct adapter *padapter, u8 iocfg_bndy)
{
	s32 rst = _SUCCESS;

	usb_write8(padapter, REG_TDECTRL+1, iocfg_bndy);
	rst = iol_execute(padapter, CMD_IOCONFIG);
	return rst;
}

static int rtl8188e_IOL_exec_cmds_sync(struct adapter *adapter, struct xmit_frame *xmit_frame, u32 max_wating_ms, u32 bndy_cnt)
{
	struct pkt_attrib *pattrib = &xmit_frame->attrib;
	u8 i;
	int ret = _FAIL;

	if (rtw_IOL_append_END_cmd(xmit_frame) != _SUCCESS)
		goto exit;
	if (rtw_usb_bulk_size_boundary(adapter, TXDESC_SIZE+pattrib->last_txcmdsz)) {
		if (rtw_IOL_append_END_cmd(xmit_frame) != _SUCCESS)
			goto exit;
	}

	dump_mgntframe_and_wait(adapter, xmit_frame, max_wating_ms);

	iol_mode_enable(adapter, 1);
	for (i = 0; i < bndy_cnt; i++) {
		u8 page_no = 0;
		page_no = i*2;
		ret = iol_ioconfig(adapter, page_no);
		if (ret != _SUCCESS)
			break;
	}
	iol_mode_enable(adapter, 0);
exit:
	/* restore BCN_HEAD */
	usb_write8(adapter, REG_TDECTRL+1, 0);
	return ret;
}

void rtw_IOL_cmd_tx_pkt_buf_dump(struct adapter *Adapter, int data_len)
{
	u32 fifo_data, reg_140;
	u32 addr, rstatus, loop = 0;
	u16 data_cnts = (data_len/8)+1;
	u8 *pbuf = vzalloc(data_len+10);
	DBG_88E("###### %s ######\n", __func__);

	usb_write8(Adapter, REG_PKT_BUFF_ACCESS_CTRL, TXPKT_BUF_SELECT);
	if (pbuf) {
		for (addr = 0; addr < data_cnts; addr++) {
			usb_write32(Adapter, 0x140, addr);
			msleep(1);
			loop = 0;
			do {
				rstatus = (reg_140 = usb_read32(Adapter, REG_PKTBUF_DBG_CTRL)&BIT24);
				if (rstatus) {
					fifo_data = usb_read32(Adapter, REG_PKTBUF_DBG_DATA_L);
					memcpy(pbuf+(addr*8), &fifo_data, 4);

					fifo_data = usb_read32(Adapter, REG_PKTBUF_DBG_DATA_H);
					memcpy(pbuf+(addr*8+4), &fifo_data, 4);
				}
				msleep(1);
			} while (!rstatus && (loop++ < 10));
		}
		rtw_IOL_cmd_buf_dump(Adapter, data_len, pbuf);
		vfree(pbuf);
	}
	DBG_88E("###### %s ######\n", __func__);
}

static void _FWDownloadEnable(struct adapter *padapter, bool enable)
{
	u8 tmp;

	if (enable) {
		/*  MCU firmware download enable. */
		tmp = usb_read8(padapter, REG_MCUFWDL);
		usb_write8(padapter, REG_MCUFWDL, tmp | 0x01);

		/*  8051 reset */
		tmp = usb_read8(padapter, REG_MCUFWDL+2);
		usb_write8(padapter, REG_MCUFWDL+2, tmp&0xf7);
	} else {
		/*  MCU firmware download disable. */
		tmp = usb_read8(padapter, REG_MCUFWDL);
		usb_write8(padapter, REG_MCUFWDL, tmp&0xfe);

		/*  Reserved for fw extension. */
		usb_write8(padapter, REG_MCUFWDL+1, 0x00);
	}
}

#define MAX_REG_BOLCK_SIZE	196

static int _BlockWrite(struct adapter *padapter, void *buffer, u32 buffSize)
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

	if (blockCount_p1) {
		RT_TRACE(_module_hal_init_c_, _drv_notice_,
			 ("_BlockWrite: [P1] buffSize(%d) blockSize_p1(%d) blockCount_p1(%d) remainSize_p1(%d)\n",
			 buffSize, blockSize_p1, blockCount_p1, remainSize_p1));
	}

	for (i = 0; i < blockCount_p1; i++) {
		ret = usb_writeN(padapter, (FW_8188E_START_ADDRESS + i * blockSize_p1), blockSize_p1, (bufferPtr + i * blockSize_p1));
		if (ret == _FAIL)
			goto exit;
	}

	/* 3 Phase #2 */
	if (remainSize_p1) {
		offset = blockCount_p1 * blockSize_p1;

		blockCount_p2 = remainSize_p1/blockSize_p2;
		remainSize_p2 = remainSize_p1%blockSize_p2;

		if (blockCount_p2) {
				RT_TRACE(_module_hal_init_c_, _drv_notice_,
					 ("_BlockWrite: [P2] buffSize_p2(%d) blockSize_p2(%d) blockCount_p2(%d) remainSize_p2(%d)\n",
					 (buffSize-offset), blockSize_p2 , blockCount_p2, remainSize_p2));
		}

		for (i = 0; i < blockCount_p2; i++) {
			ret = usb_writeN(padapter, (FW_8188E_START_ADDRESS + offset + i*blockSize_p2), blockSize_p2, (bufferPtr + offset + i*blockSize_p2));

			if (ret == _FAIL)
				goto exit;
		}
	}

	/* 3 Phase #3 */
	if (remainSize_p2) {
		offset = (blockCount_p1 * blockSize_p1) + (blockCount_p2 * blockSize_p2);

		blockCount_p3 = remainSize_p2 / blockSize_p3;

		RT_TRACE(_module_hal_init_c_, _drv_notice_,
			 ("_BlockWrite: [P3] buffSize_p3(%d) blockSize_p3(%d) blockCount_p3(%d)\n",
			 (buffSize-offset), blockSize_p3, blockCount_p3));

		for (i = 0; i < blockCount_p3; i++) {
			ret = usb_write8(padapter, (FW_8188E_START_ADDRESS + offset + i), *(bufferPtr + offset + i));

			if (ret == _FAIL)
				goto exit;
		}
	}

exit:
	return ret;
}

static int _PageWrite(struct adapter *padapter, u32 page, void *buffer, u32 size)
{
	u8 value8;
	u8 u8Page = (u8)(page & 0x07);

	value8 = (usb_read8(padapter, REG_MCUFWDL+2) & 0xF8) | u8Page;
	usb_write8(padapter, REG_MCUFWDL+2, value8);

	return _BlockWrite(padapter, buffer, size);
}

static int _WriteFW(struct adapter *padapter, void *buffer, u32 size)
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
		ret = _PageWrite(padapter, page, bufferPtr+offset, MAX_PAGE_SIZE);

		if (ret == _FAIL)
			goto exit;
	}
	if (remainSize) {
		offset = pageNums * MAX_PAGE_SIZE;
		page = pageNums;
		ret = _PageWrite(padapter, page, bufferPtr+offset, remainSize);

		if (ret == _FAIL)
			goto exit;
	}
	RT_TRACE(_module_hal_init_c_, _drv_info_, ("_WriteFW Done- for Normal chip.\n"));
exit:
	return ret;
}

void _8051Reset88E(struct adapter *padapter)
{
	u8 u1bTmp;

	u1bTmp = usb_read8(padapter, REG_SYS_FUNC_EN+1);
	usb_write8(padapter, REG_SYS_FUNC_EN+1, u1bTmp&(~BIT2));
	usb_write8(padapter, REG_SYS_FUNC_EN+1, u1bTmp|(BIT2));
	DBG_88E("=====> _8051Reset88E(): 8051 reset success .\n");
}

static s32 _FWFreeToGo(struct adapter *padapter)
{
	u32	counter = 0;
	u32	value32;

	/*  polling CheckSum report */
	do {
		value32 = usb_read32(padapter, REG_MCUFWDL);
		if (value32 & FWDL_ChkSum_rpt)
			break;
	} while (counter++ < POLLING_READY_TIMEOUT_COUNT);

	if (counter >= POLLING_READY_TIMEOUT_COUNT) {
		DBG_88E("%s: chksum report fail! REG_MCUFWDL:0x%08x\n", __func__, value32);
		return _FAIL;
	}
	DBG_88E("%s: Checksum report OK! REG_MCUFWDL:0x%08x\n", __func__, value32);

	value32 = usb_read32(padapter, REG_MCUFWDL);
	value32 |= MCUFWDL_RDY;
	value32 &= ~WINTINI_RDY;
	usb_write32(padapter, REG_MCUFWDL, value32);

	_8051Reset88E(padapter);

	/*  polling for FW ready */
	counter = 0;
	do {
		value32 = usb_read32(padapter, REG_MCUFWDL);
		if (value32 & WINTINI_RDY) {
			DBG_88E("%s: Polling FW ready success!! REG_MCUFWDL:0x%08x\n", __func__, value32);
			return _SUCCESS;
		}
		udelay(5);
	} while (counter++ < POLLING_READY_TIMEOUT_COUNT);

	DBG_88E("%s: Polling FW ready fail!! REG_MCUFWDL:0x%08x\n", __func__, value32);
	return _FAIL;
}

#define IS_FW_81xxC(padapter)	(((GET_HAL_DATA(padapter))->FirmwareSignature & 0xFFF0) == 0x88C0)

static int load_firmware(struct rt_firmware *pFirmware, struct device *device)
{
	int rtstatus = _SUCCESS;
	const struct firmware *fw;
	const char fw_name[] = "rtlwifi/rtl8188eufw.bin";

	if (request_firmware(&fw, fw_name, device)) {
		rtstatus = _FAIL;
		goto exit;
	}
	if (!fw) {
		pr_err("Firmware %s not available\n", fw_name);
		rtstatus = _FAIL;
		goto exit;
	}
	if (fw->size > FW_8188E_SIZE) {
		rtstatus = _FAIL;
		RT_TRACE(_module_hal_init_c_, _drv_err_,
			 ("Firmware size exceed 0x%X. Check it.\n",
			 FW_8188E_SIZE));
		goto exit;
	}

	pFirmware->szFwBuffer = kzalloc(FW_8188E_SIZE, GFP_KERNEL);
	if (!pFirmware->szFwBuffer) {
		rtstatus = _FAIL;
		goto exit;
	}
	memcpy(pFirmware->szFwBuffer, fw->data, fw->size);
	pFirmware->ulFwLength = fw->size;
	release_firmware(fw);

	DBG_88E_LEVEL(_drv_info_,
		      "+%s: !bUsedWoWLANFw, FmrmwareLen:%d+\n", __func__,
		      pFirmware->ulFwLength);
exit:
	return rtstatus;
}

s32 rtl8188e_FirmwareDownload(struct adapter *padapter)
{
	s32	rtStatus = _SUCCESS;
	u8 writeFW_retry = 0;
	u32 fwdl_start_time;
	struct hal_data_8188e *pHalData = GET_HAL_DATA(padapter);
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct device *device = dvobj_to_dev(dvobj);
	struct rt_firmware_hdr *pFwHdr = NULL;
	u8 *pFirmwareBuf;
	u32 FirmwareLen;
	static int log_version;

	RT_TRACE(_module_hal_init_c_, _drv_info_, ("+%s\n", __func__));
	if (!dvobj->firmware.szFwBuffer)
		rtStatus = load_firmware(&dvobj->firmware, device);
	if (rtStatus == _FAIL) {
		dvobj->firmware.szFwBuffer = NULL;
		goto Exit;
	}
	pFirmwareBuf = dvobj->firmware.szFwBuffer;
	FirmwareLen = dvobj->firmware.ulFwLength;

	/*  To Check Fw header. Added by tynli. 2009.12.04. */
	pFwHdr = (struct rt_firmware_hdr *)dvobj->firmware.szFwBuffer;

	pHalData->FirmwareVersion =  le16_to_cpu(pFwHdr->Version);
	pHalData->FirmwareSubVersion = pFwHdr->Subversion;
	pHalData->FirmwareSignature = le16_to_cpu(pFwHdr->Signature);

	if (!log_version++)
		pr_info("%sFirmware Version %d, SubVersion %d, Signature 0x%x\n",
			DRIVER_PREFIX, pHalData->FirmwareVersion,
			pHalData->FirmwareSubVersion, pHalData->FirmwareSignature);

	if (IS_FW_HEADER_EXIST(pFwHdr)) {
		/*  Shift 32 bytes for FW header */
		pFirmwareBuf = pFirmwareBuf + 32;
		FirmwareLen = FirmwareLen - 32;
	}

	/*  Suggested by Filen. If 8051 is running in RAM code, driver should inform Fw to reset by itself, */
	/*  or it will cause download Fw fail. 2010.02.01. by tynli. */
	if (usb_read8(padapter, REG_MCUFWDL) & RAM_DL_SEL) { /* 8051 RAM code */
		usb_write8(padapter, REG_MCUFWDL, 0x00);
		_8051Reset88E(padapter);
	}

	_FWDownloadEnable(padapter, true);
	fwdl_start_time = jiffies;
	while (1) {
		/* reset the FWDL chksum */
		usb_write8(padapter, REG_MCUFWDL, usb_read8(padapter, REG_MCUFWDL) | FWDL_ChkSum_rpt);

		rtStatus = _WriteFW(padapter, pFirmwareBuf, FirmwareLen);

		if (rtStatus == _SUCCESS ||
		    (rtw_get_passing_time_ms(fwdl_start_time) > 500 && writeFW_retry++ >= 3))
			break;

		DBG_88E("%s writeFW_retry:%u, time after fwdl_start_time:%ums\n",
			__func__, writeFW_retry, rtw_get_passing_time_ms(fwdl_start_time)
		);
	}
	_FWDownloadEnable(padapter, false);
	if (_SUCCESS != rtStatus) {
		DBG_88E("DL Firmware failed!\n");
		goto Exit;
	}

	rtStatus = _FWFreeToGo(padapter);
	if (_SUCCESS != rtStatus) {
		DBG_88E("DL Firmware failed!\n");
		goto Exit;
	}
	RT_TRACE(_module_hal_init_c_, _drv_info_, ("Firmware is ready to run!\n"));
Exit:
	return rtStatus;
}

void rtl8188e_InitializeFirmwareVars(struct adapter *padapter)
{
	struct hal_data_8188e *pHalData = GET_HAL_DATA(padapter);

	/*  Init Fw LPS related. */
	padapter->pwrctrlpriv.bFwCurrentInPSMode = false;

	/*  Init H2C counter. by tynli. 2009.12.09. */
	pHalData->LastHMEBoxNum = 0;
}

static void rtl8188e_free_hal_data(struct adapter *padapter)
{
	kfree(padapter->HalData);
	padapter->HalData = NULL;
}

static struct HAL_VERSION ReadChipVersion8188E(struct adapter *padapter)
{
	u32				value32;
	struct HAL_VERSION		ChipVersion;
	struct hal_data_8188e	*pHalData;

	pHalData = GET_HAL_DATA(padapter);

	value32 = usb_read32(padapter, REG_SYS_CFG);
	ChipVersion.ICType = CHIP_8188E;
	ChipVersion.ChipType = ((value32 & RTL_ID) ? TEST_CHIP : NORMAL_CHIP);

	ChipVersion.RFType = RF_TYPE_1T1R;
	ChipVersion.VendorType = ((value32 & VENDOR_ID) ? CHIP_VENDOR_UMC : CHIP_VENDOR_TSMC);
	ChipVersion.CUTVersion = (value32 & CHIP_VER_RTL_MASK)>>CHIP_VER_RTL_SHIFT; /*  IC version (CUT) */

	/*  For regulator mode. by tynli. 2011.01.14 */
	pHalData->RegulatorMode = ((value32 & TRP_BT_EN) ? RT_LDO_REGULATOR : RT_SWITCHING_REGULATOR);

	ChipVersion.ROMVer = 0;	/*  ROM code version. */
	pHalData->MultiFunc = RT_MULTI_FUNC_NONE;

	dump_chip_info(ChipVersion);

	pHalData->VersionID = ChipVersion;

	if (IS_1T2R(ChipVersion)) {
		pHalData->rf_type = RF_1T2R;
		pHalData->NumTotalRFPath = 2;
	} else if (IS_2T2R(ChipVersion)) {
		pHalData->rf_type = RF_2T2R;
		pHalData->NumTotalRFPath = 2;
	} else{
		pHalData->rf_type = RF_1T1R;
		pHalData->NumTotalRFPath = 1;
	}

	MSG_88E("RF_Type is %x!!\n", pHalData->rf_type);

	return ChipVersion;
}

static void rtl8188e_read_chip_version(struct adapter *padapter)
{
	ReadChipVersion8188E(padapter);
}

static void rtl8188e_SetHalODMVar(struct adapter *Adapter, enum hal_odm_variable eVariable, void *pValue1, bool bSet)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);
	struct odm_dm_struct *podmpriv = &pHalData->odmpriv;
	switch (eVariable) {
	case HAL_ODM_STA_INFO:
		{
			struct sta_info *psta = (struct sta_info *)pValue1;
			if (bSet) {
				DBG_88E("### Set STA_(%d) info\n", psta->mac_id);
				ODM_CmnInfoPtrArrayHook(podmpriv, ODM_CMNINFO_STA_STATUS, psta->mac_id, psta);
				ODM_RAInfo_Init(podmpriv, psta->mac_id);
			} else {
				DBG_88E("### Clean STA_(%d) info\n", psta->mac_id);
				ODM_CmnInfoPtrArrayHook(podmpriv, ODM_CMNINFO_STA_STATUS, psta->mac_id, NULL);
		       }
		}
		break;
	case HAL_ODM_P2P_STATE:
			ODM_CmnInfoUpdate(podmpriv, ODM_CMNINFO_WIFI_DIRECT, bSet);
		break;
	case HAL_ODM_WIFI_DISPLAY_STATE:
			ODM_CmnInfoUpdate(podmpriv, ODM_CMNINFO_WIFI_DISPLAY, bSet);
		break;
	default:
		break;
	}
}

static void hal_notch_filter_8188e(struct adapter *adapter, bool enable)
{
	if (enable) {
		DBG_88E("Enable notch filter\n");
		usb_write8(adapter, rOFDM0_RxDSP+1, usb_read8(adapter, rOFDM0_RxDSP+1) | BIT1);
	} else {
		DBG_88E("Disable notch filter\n");
		usb_write8(adapter, rOFDM0_RxDSP+1, usb_read8(adapter, rOFDM0_RxDSP+1) & ~BIT1);
	}
}
void rtl8188e_set_hal_ops(struct hal_ops *pHalFunc)
{
	pHalFunc->free_hal_data = &rtl8188e_free_hal_data;

	pHalFunc->dm_init = &rtl8188e_init_dm_priv;

	pHalFunc->read_chip_version = &rtl8188e_read_chip_version;

	pHalFunc->set_bwmode_handler = &PHY_SetBWMode8188E;
	pHalFunc->set_channel_handler = &PHY_SwChnl8188E;

	pHalFunc->hal_dm_watchdog = &rtl8188e_HalDmWatchDog;

	pHalFunc->Add_RateATid = &rtl8188e_Add_RateATid;

	pHalFunc->AntDivBeforeLinkHandler = &AntDivBeforeLink8188E;
	pHalFunc->AntDivCompareHandler = &AntDivCompare8188E;
	pHalFunc->read_bbreg = &rtl8188e_PHY_QueryBBReg;
	pHalFunc->write_bbreg = &rtl8188e_PHY_SetBBReg;
	pHalFunc->read_rfreg = &rtl8188e_PHY_QueryRFReg;
	pHalFunc->write_rfreg = &rtl8188e_PHY_SetRFReg;

	pHalFunc->sreset_init_value = &sreset_init_value;
	pHalFunc->sreset_get_wifi_status  = &sreset_get_wifi_status;

	pHalFunc->SetHalODMVarHandler = &rtl8188e_SetHalODMVar;

	pHalFunc->IOL_exec_cmds_sync = &rtl8188e_IOL_exec_cmds_sync;

	pHalFunc->hal_notch_filter = &hal_notch_filter_8188e;
}

u8 GetEEPROMSize8188E(struct adapter *padapter)
{
	u8 size = 0;
	u32	cr;

	cr = usb_read16(padapter, REG_9346CR);
	/*  6: EEPROM used is 93C46, 4: boot from E-Fuse. */
	size = (cr & BOOT_FROM_EEPROM) ? 6 : 4;

	MSG_88E("EEPROM type is %s\n", size == 4 ? "E-FUSE" : "93C46");

	return size;
}

/*  */
/*  */
/*  LLT R/W/Init function */
/*  */
/*  */
static s32 _LLTWrite(struct adapter *padapter, u32 address, u32 data)
{
	s32	status = _SUCCESS;
	s32	count = 0;
	u32	value = _LLT_INIT_ADDR(address) | _LLT_INIT_DATA(data) | _LLT_OP(_LLT_WRITE_ACCESS);
	u16	LLTReg = REG_LLT_INIT;

	usb_write32(padapter, LLTReg, value);

	/* polling */
	do {
		value = usb_read32(padapter, LLTReg);
		if (_LLT_NO_ACTIVE == _LLT_OP_VALUE(value))
			break;

		if (count > POLLING_LLT_THRESHOLD) {
			RT_TRACE(_module_hal_init_c_, _drv_err_, ("Failed to polling write LLT done at address %d!\n", address));
			status = _FAIL;
			break;
		}
	} while (count++);

	return status;
}

s32 InitLLTTable(struct adapter *padapter, u8 txpktbuf_bndy)
{
	s32	status = _FAIL;
	u32	i;
	u32	Last_Entry_Of_TxPktBuf = LAST_ENTRY_OF_TX_PKT_BUFFER;/*  176, 22k */

	if (rtw_IOL_applied(padapter)) {
		status = iol_InitLLTTable(padapter, txpktbuf_bndy);
	} else {
		for (i = 0; i < (txpktbuf_bndy - 1); i++) {
			status = _LLTWrite(padapter, i, i + 1);
			if (_SUCCESS != status)
				return status;
		}

		/*  end of list */
		status = _LLTWrite(padapter, (txpktbuf_bndy - 1), 0xFF);
		if (_SUCCESS != status)
			return status;

		/*  Make the other pages as ring buffer */
		/*  This ring buffer is used as beacon buffer if we config this MAC as two MAC transfer. */
		/*  Otherwise used as local loopback buffer. */
		for (i = txpktbuf_bndy; i < Last_Entry_Of_TxPktBuf; i++) {
			status = _LLTWrite(padapter, i, (i + 1));
			if (_SUCCESS != status)
				return status;
		}

		/*  Let last entry point to the start entry of ring buffer */
		status = _LLTWrite(padapter, Last_Entry_Of_TxPktBuf, txpktbuf_bndy);
		if (_SUCCESS != status) {
			return status;
		}
	}

	return status;
}

void
Hal_InitPGData88E(struct adapter *padapter)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);

	if (!pEEPROM->bautoload_fail_flag) { /*  autoload OK. */
		if (!is_boot_from_eeprom(padapter)) {
			/*  Read EFUSE real map to shadow. */
			EFUSE_ShadowMapUpdate(padapter, EFUSE_WIFI);
		}
	} else {/* autoload fail */
		RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("AutoLoad Fail reported from CR9346!!\n"));
		/* update to default value 0xFF */
		if (!is_boot_from_eeprom(padapter))
			EFUSE_ShadowMapUpdate(padapter, EFUSE_WIFI);
	}
}

void
Hal_EfuseParseIDCode88E(
		struct adapter *padapter,
		u8 *hwinfo
	)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	u16			EEPROMId;

	/*  Checl 0x8129 again for making sure autoload status!! */
	EEPROMId = le16_to_cpu(*((__le16 *)hwinfo));
	if (EEPROMId != RTL_EEPROM_ID) {
		DBG_88E("EEPROM ID(%#x) is invalid!!\n", EEPROMId);
		pEEPROM->bautoload_fail_flag = true;
	} else {
		pEEPROM->bautoload_fail_flag = false;
	}

	DBG_88E("EEPROM ID = 0x%04x\n", EEPROMId);
}

static void Hal_ReadPowerValueFromPROM_8188E(struct txpowerinfo24g *pwrInfo24G, u8 *PROMContent, bool AutoLoadFail)
{
	u32 rfPath, eeAddr = EEPROM_TX_PWR_INX_88E, group, TxCount = 0;

	memset(pwrInfo24G, 0, sizeof(struct txpowerinfo24g));

	if (AutoLoadFail) {
		for (rfPath = 0; rfPath < MAX_RF_PATH; rfPath++) {
			/* 2.4G default value */
			for (group = 0; group < MAX_CHNL_GROUP_24G; group++) {
				pwrInfo24G->IndexCCK_Base[rfPath][group] =	EEPROM_DEFAULT_24G_INDEX;
				pwrInfo24G->IndexBW40_Base[rfPath][group] =	EEPROM_DEFAULT_24G_INDEX;
			}
			for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {
				if (TxCount == 0) {
					pwrInfo24G->BW20_Diff[rfPath][0] = EEPROM_DEFAULT_24G_HT20_DIFF;
					pwrInfo24G->OFDM_Diff[rfPath][0] = EEPROM_DEFAULT_24G_OFDM_DIFF;
				} else {
					pwrInfo24G->BW20_Diff[rfPath][TxCount] = EEPROM_DEFAULT_DIFF;
					pwrInfo24G->BW40_Diff[rfPath][TxCount] = EEPROM_DEFAULT_DIFF;
					pwrInfo24G->CCK_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] = EEPROM_DEFAULT_DIFF;
				}
			}
		}
		return;
	}

	for (rfPath = 0; rfPath < MAX_RF_PATH; rfPath++) {
		/* 2.4G default value */
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
				if (PROMContent[eeAddr] == 0xFF) {
					pwrInfo24G->BW20_Diff[rfPath][TxCount] = EEPROM_DEFAULT_24G_HT20_DIFF;
				} else {
					pwrInfo24G->BW20_Diff[rfPath][TxCount] = (PROMContent[eeAddr]&0xf0)>>4;
					if (pwrInfo24G->BW20_Diff[rfPath][TxCount] & BIT3)		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->BW20_Diff[rfPath][TxCount] |= 0xF0;
				}

				if (PROMContent[eeAddr] == 0xFF) {
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_24G_OFDM_DIFF;
				} else {
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0x0f);
					if (pwrInfo24G->OFDM_Diff[rfPath][TxCount] & BIT3)		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->OFDM_Diff[rfPath][TxCount] |= 0xF0;
				}
				pwrInfo24G->CCK_Diff[rfPath][TxCount] = 0;
				eeAddr++;
			} else {
				if (PROMContent[eeAddr] == 0xFF) {
					pwrInfo24G->BW40_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
				} else {
					pwrInfo24G->BW40_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0xf0)>>4;
					if (pwrInfo24G->BW40_Diff[rfPath][TxCount] & BIT3)		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->BW40_Diff[rfPath][TxCount] |= 0xF0;
				}

				if (PROMContent[eeAddr] == 0xFF) {
					pwrInfo24G->BW20_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
				} else {
					pwrInfo24G->BW20_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0x0f);
					if (pwrInfo24G->BW20_Diff[rfPath][TxCount] & BIT3)		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->BW20_Diff[rfPath][TxCount] |= 0xF0;
				}
				eeAddr++;

				if (PROMContent[eeAddr] == 0xFF) {
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] = EEPROM_DEFAULT_DIFF;
				} else {
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0xf0)>>4;
					if (pwrInfo24G->OFDM_Diff[rfPath][TxCount] & BIT3)		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->OFDM_Diff[rfPath][TxCount] |= 0xF0;
				}

				if (PROMContent[eeAddr] == 0xFF) {
					pwrInfo24G->CCK_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
				} else {
					pwrInfo24G->CCK_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0x0f);
					if (pwrInfo24G->CCK_Diff[rfPath][TxCount] & BIT3)		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->CCK_Diff[rfPath][TxCount] |= 0xF0;
				}
				eeAddr++;
			}
		}
	}
}

static u8 Hal_GetChnlGroup88E(u8 chnl, u8 *pGroup)
{
	u8 bIn24G = true;

	if (chnl <= 14) {
		bIn24G = true;

		if (chnl < 3)			/*  Channel 1-2 */
			*pGroup = 0;
		else if (chnl < 6)		/*  Channel 3-5 */
			*pGroup = 1;
		else	 if (chnl < 9)		/*  Channel 6-8 */
			*pGroup = 2;
		else if (chnl < 12)		/*  Channel 9-11 */
			*pGroup = 3;
		else if (chnl < 14)		/*  Channel 12-13 */
			*pGroup = 4;
		else if (chnl == 14)		/*  Channel 14 */
			*pGroup = 5;
	} else {
		bIn24G = false;

		if (chnl <= 40)
			*pGroup = 0;
		else if (chnl <= 48)
			*pGroup = 1;
		else	 if (chnl <= 56)
			*pGroup = 2;
		else if (chnl <= 64)
			*pGroup = 3;
		else if (chnl <= 104)
			*pGroup = 4;
		else if (chnl <= 112)
			*pGroup = 5;
		else if (chnl <= 120)
			*pGroup = 5;
		else if (chnl <= 128)
			*pGroup = 6;
		else if (chnl <= 136)
			*pGroup = 7;
		else if (chnl <= 144)
			*pGroup = 8;
		else if (chnl <= 153)
			*pGroup = 9;
		else if (chnl <= 161)
			*pGroup = 10;
		else if (chnl <= 177)
			*pGroup = 11;
	}
	return bIn24G;
}

void Hal_ReadPowerSavingMode88E(struct adapter *padapter, u8 *hwinfo, bool AutoLoadFail)
{
	if (AutoLoadFail) {
		padapter->pwrctrlpriv.bHWPowerdown = false;
		padapter->pwrctrlpriv.bSupportRemoteWakeup = false;
	} else {
		/* hw power down mode selection , 0:rf-off / 1:power down */

		if (padapter->registrypriv.hwpdn_mode == 2)
			padapter->pwrctrlpriv.bHWPowerdown = (hwinfo[EEPROM_RF_FEATURE_OPTION_88E] & BIT4);
		else
			padapter->pwrctrlpriv.bHWPowerdown = padapter->registrypriv.hwpdn_mode;

		/*  decide hw if support remote wakeup function */
		/*  if hw supported, 8051 (SIE) will generate WeakUP signal(D+/D- toggle) when autoresume */
		padapter->pwrctrlpriv.bSupportRemoteWakeup = (hwinfo[EEPROM_USB_OPTIONAL_FUNCTION0] & BIT1) ? true : false;

		DBG_88E("%s...bHWPwrPindetect(%x)-bHWPowerdown(%x) , bSupportRemoteWakeup(%x)\n", __func__,
		padapter->pwrctrlpriv.bHWPwrPindetect, padapter->pwrctrlpriv.bHWPowerdown , padapter->pwrctrlpriv.bSupportRemoteWakeup);

		DBG_88E("### PS params =>  power_mgnt(%x), usbss_enable(%x) ###\n", padapter->registrypriv.power_mgnt, padapter->registrypriv.usbss_enable);
	}
}

void Hal_ReadTxPowerInfo88E(struct adapter *padapter, u8 *PROMContent, bool AutoLoadFail)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(padapter);
	struct txpowerinfo24g pwrInfo24G;
	u8 rfPath, ch, group;
	u8 bIn24G, TxCount;

	Hal_ReadPowerValueFromPROM_8188E(&pwrInfo24G, PROMContent, AutoLoadFail);

	if (!AutoLoadFail)
		pHalData->bTXPowerDataReadFromEEPORM = true;

	for (rfPath = 0; rfPath < pHalData->NumTotalRFPath; rfPath++) {
		for (ch = 0; ch < CHANNEL_MAX_NUMBER; ch++) {
			bIn24G = Hal_GetChnlGroup88E(ch, &group);
			if (bIn24G) {
				pHalData->Index24G_CCK_Base[rfPath][ch] = pwrInfo24G.IndexCCK_Base[rfPath][group];
				if (ch == 14)
					pHalData->Index24G_BW40_Base[rfPath][ch] = pwrInfo24G.IndexBW40_Base[rfPath][4];
				else
					pHalData->Index24G_BW40_Base[rfPath][ch] = pwrInfo24G.IndexBW40_Base[rfPath][group];
			}
			if (bIn24G) {
				DBG_88E("======= Path %d, Channel %d =======\n", rfPath, ch);
				DBG_88E("Index24G_CCK_Base[%d][%d] = 0x%x\n", rfPath, ch , pHalData->Index24G_CCK_Base[rfPath][ch]);
				DBG_88E("Index24G_BW40_Base[%d][%d] = 0x%x\n", rfPath, ch , pHalData->Index24G_BW40_Base[rfPath][ch]);
			}
		}
		for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {
			pHalData->CCK_24G_Diff[rfPath][TxCount] = pwrInfo24G.CCK_Diff[rfPath][TxCount];
			pHalData->OFDM_24G_Diff[rfPath][TxCount] = pwrInfo24G.OFDM_Diff[rfPath][TxCount];
			pHalData->BW20_24G_Diff[rfPath][TxCount] = pwrInfo24G.BW20_Diff[rfPath][TxCount];
			pHalData->BW40_24G_Diff[rfPath][TxCount] = pwrInfo24G.BW40_Diff[rfPath][TxCount];
			DBG_88E("======= TxCount %d =======\n", TxCount);
			DBG_88E("CCK_24G_Diff[%d][%d] = %d\n", rfPath, TxCount, pHalData->CCK_24G_Diff[rfPath][TxCount]);
			DBG_88E("OFDM_24G_Diff[%d][%d] = %d\n", rfPath, TxCount, pHalData->OFDM_24G_Diff[rfPath][TxCount]);
			DBG_88E("BW20_24G_Diff[%d][%d] = %d\n", rfPath, TxCount, pHalData->BW20_24G_Diff[rfPath][TxCount]);
			DBG_88E("BW40_24G_Diff[%d][%d] = %d\n", rfPath, TxCount, pHalData->BW40_24G_Diff[rfPath][TxCount]);
		}
	}

	/*  2010/10/19 MH Add Regulator recognize for CU. */
	if (!AutoLoadFail) {
		pHalData->EEPROMRegulatory = (PROMContent[EEPROM_RF_BOARD_OPTION_88E]&0x7);	/* bit0~2 */
		if (PROMContent[EEPROM_RF_BOARD_OPTION_88E] == 0xFF)
			pHalData->EEPROMRegulatory = (EEPROM_DEFAULT_BOARD_OPTION&0x7);	/* bit0~2 */
	} else {
		pHalData->EEPROMRegulatory = 0;
	}
	DBG_88E("EEPROMRegulatory = 0x%x\n", pHalData->EEPROMRegulatory);
}

void Hal_EfuseParseXtal_8188E(struct adapter *pAdapter, u8 *hwinfo, bool AutoLoadFail)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(pAdapter);

	if (!AutoLoadFail) {
		pHalData->CrystalCap = hwinfo[EEPROM_XTAL_88E];
		if (pHalData->CrystalCap == 0xFF)
			pHalData->CrystalCap = EEPROM_Default_CrystalCap_88E;
	} else {
		pHalData->CrystalCap = EEPROM_Default_CrystalCap_88E;
	}
	DBG_88E("CrystalCap: 0x%2x\n", pHalData->CrystalCap);
}

void Hal_EfuseParseBoardType88E(struct adapter *pAdapter, u8 *hwinfo, bool AutoLoadFail)
{
	struct hal_data_8188e *pHalData = GET_HAL_DATA(pAdapter);

	if (!AutoLoadFail)
		pHalData->BoardType = ((hwinfo[EEPROM_RF_BOARD_OPTION_88E]&0xE0)>>5);
	else
		pHalData->BoardType = 0;
	DBG_88E("Board Type: 0x%2x\n", pHalData->BoardType);
}

void Hal_EfuseParseEEPROMVer88E(struct adapter *padapter, u8 *hwinfo, bool AutoLoadFail)
{
	struct hal_data_8188e *pHalData = GET_HAL_DATA(padapter);

	if (!AutoLoadFail) {
		pHalData->EEPROMVersion = hwinfo[EEPROM_VERSION_88E];
		if (pHalData->EEPROMVersion == 0xFF)
			pHalData->EEPROMVersion = EEPROM_Default_Version;
	} else {
		pHalData->EEPROMVersion = 1;
	}
	RT_TRACE(_module_hci_hal_init_c_, _drv_info_,
		 ("Hal_EfuseParseEEPROMVer(), EEVer = %d\n",
		 pHalData->EEPROMVersion));
}

void rtl8188e_EfuseParseChnlPlan(struct adapter *padapter, u8 *hwinfo, bool AutoLoadFail)
{
	padapter->mlmepriv.ChannelPlan =
		 hal_com_get_channel_plan(padapter,
					  hwinfo ? hwinfo[EEPROM_ChannelPlan_88E] : 0xFF,
					  padapter->registrypriv.channel_plan,
					  RT_CHANNEL_DOMAIN_WORLD_WIDE_13, AutoLoadFail);

	DBG_88E("mlmepriv.ChannelPlan = 0x%02x\n", padapter->mlmepriv.ChannelPlan);
}

void Hal_EfuseParseCustomerID88E(struct adapter *padapter, u8 *hwinfo, bool AutoLoadFail)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(padapter);

	if (!AutoLoadFail) {
		pHalData->EEPROMCustomerID = hwinfo[EEPROM_CUSTOMERID_88E];
	} else {
		pHalData->EEPROMCustomerID = 0;
		pHalData->EEPROMSubCustomerID = 0;
	}
	DBG_88E("EEPROM Customer ID: 0x%2x\n", pHalData->EEPROMCustomerID);
}

void Hal_ReadAntennaDiversity88E(struct adapter *pAdapter, u8 *PROMContent, bool AutoLoadFail)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(pAdapter);
	struct registry_priv	*registry_par = &pAdapter->registrypriv;

	if (!AutoLoadFail) {
		/*  Antenna Diversity setting. */
		if (registry_par->antdiv_cfg == 2) { /*  2:By EFUSE */
			pHalData->AntDivCfg = (PROMContent[EEPROM_RF_BOARD_OPTION_88E]&0x18)>>3;
			if (PROMContent[EEPROM_RF_BOARD_OPTION_88E] == 0xFF)
				pHalData->AntDivCfg = (EEPROM_DEFAULT_BOARD_OPTION&0x18)>>3;
		} else {
			pHalData->AntDivCfg = registry_par->antdiv_cfg;  /*  0:OFF , 1:ON, 2:By EFUSE */
		}

		if (registry_par->antdiv_type == 0) {
			/* If TRxAntDivType is AUTO in advanced setting, use EFUSE value instead. */
			pHalData->TRxAntDivType = PROMContent[EEPROM_RF_ANTENNA_OPT_88E];
			if (pHalData->TRxAntDivType == 0xFF)
				pHalData->TRxAntDivType = CG_TRX_HW_ANTDIV; /*  For 88EE, 1Tx and 1RxCG are fixed.(1Ant, Tx and RxCG are both on aux port) */
		} else {
			pHalData->TRxAntDivType = registry_par->antdiv_type;
		}

		if (pHalData->TRxAntDivType == CG_TRX_HW_ANTDIV || pHalData->TRxAntDivType == CGCS_RX_HW_ANTDIV)
			pHalData->AntDivCfg = 1; /*  0xC1[3] is ignored. */
	} else {
		pHalData->AntDivCfg = 0;
		pHalData->TRxAntDivType = pHalData->TRxAntDivType; /*  The value in the driver setting of device manager. */
	}
	DBG_88E("EEPROM : AntDivCfg = %x, TRxAntDivType = %x\n", pHalData->AntDivCfg, pHalData->TRxAntDivType);
}

void Hal_ReadThermalMeter_88E(struct adapter *Adapter, u8 *PROMContent, bool AutoloadFail)
{
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(Adapter);

	/*  ThermalMeter from EEPROM */
	if (!AutoloadFail)
		pHalData->EEPROMThermalMeter = PROMContent[EEPROM_THERMAL_METER_88E];
	else
		pHalData->EEPROMThermalMeter = EEPROM_Default_ThermalMeter_88E;

	if (pHalData->EEPROMThermalMeter == 0xff || AutoloadFail) {
		pHalData->bAPKThermalMeterIgnore = true;
		pHalData->EEPROMThermalMeter = EEPROM_Default_ThermalMeter_88E;
	}
	DBG_88E("ThermalMeter = 0x%x\n", pHalData->EEPROMThermalMeter);
}

/*  This function is used only for 92C to set REG_BCN_CTRL(0x550) register. */
/*  We just reserve the value of the register in variable pHalData->RegBcnCtrlVal and then operate */
/*  the value of the register via atomic operation. */
/*  This prevents from race condition when setting this register. */
/*  The value of pHalData->RegBcnCtrlVal is initialized in HwConfigureRTL8192CE() function. */

void SetBcnCtrlReg(struct adapter *padapter, u8 SetBits, u8 ClearBits)
{
	struct hal_data_8188e *pHalData;

	pHalData = GET_HAL_DATA(padapter);

	pHalData->RegBcnCtrlVal |= SetBits;
	pHalData->RegBcnCtrlVal &= ~ClearBits;

	usb_write8(padapter, REG_BCN_CTRL, (u8)pHalData->RegBcnCtrlVal);
}
