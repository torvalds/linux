// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _HAL_INIT_C_

#include "../include/linux/firmware.h"
#include "../include/drv_types.h"
#include "../include/rtw_efuse.h"
#include "../include/rtl8188e_hal.h"
#include "../include/rtw_iol.h"
#include "../include/usb_ops.h"

static void iol_mode_enable(struct adapter *padapter, u8 enable)
{
	u8 reg_0xf0 = 0;

	if (enable) {
		/* Enable initial offload */
		reg_0xf0 = rtw_read8(padapter, REG_SYS_CFG);
		rtw_write8(padapter, REG_SYS_CFG, reg_0xf0 | SW_OFFLOAD_EN);

		if (!padapter->bFWReady) {
			DBG_88E("bFWReady == false call reset 8051...\n");
			_8051Reset88E(padapter);
		}

	} else {
		/* disable initial offload */
		reg_0xf0 = rtw_read8(padapter, REG_SYS_CFG);
		rtw_write8(padapter, REG_SYS_CFG, reg_0xf0 & ~SW_OFFLOAD_EN);
	}
}

static s32 iol_execute(struct adapter *padapter, u8 control)
{
	s32 status = _FAIL;
	u8 reg_0x88 = 0;
	u32 start = 0, passing_time = 0;

	control = control & 0x0f;
	reg_0x88 = rtw_read8(padapter, REG_HMEBOX_E0);
	rtw_write8(padapter, REG_HMEBOX_E0,  reg_0x88 | control);

	start = jiffies;
	while ((reg_0x88 = rtw_read8(padapter, REG_HMEBOX_E0)) & control &&
	       (passing_time = rtw_get_passing_time_ms(start)) < 1000) {
		;
	}

	reg_0x88 = rtw_read8(padapter, REG_HMEBOX_E0);
	status = (reg_0x88 & control) ? _FAIL : _SUCCESS;
	if (reg_0x88 & control << 4)
		status = _FAIL;
	return status;
}

static s32 iol_InitLLTTable(struct adapter *padapter, u8 txpktbuf_bndy)
{
	s32 rst = _SUCCESS;
	iol_mode_enable(padapter, 1);
	rtw_write8(padapter, REG_TDECTRL + 1, txpktbuf_bndy);
	rst = iol_execute(padapter, CMD_INIT_LLT);
	iol_mode_enable(padapter, 0);
	return rst;
}

static void
efuse_phymap_to_logical(u8 *phymap, u16 _offset, u16 _size_byte, u8  *pbuf)
{
	u8 *efuseTbl = NULL;
	u8 rtemp8;
	u16	eFuse_Addr = 0;
	u8 offset, wren;
	u16	i, j;
	u16	**eFuseWord = NULL;
	u16	efuse_utilized = 0;
	u8 u1temp = 0;

	efuseTbl = kzalloc(EFUSE_MAP_LEN_88E, GFP_KERNEL);
	if (!efuseTbl) {
		DBG_88E("%s: alloc efuseTbl fail!\n", __func__);
		goto exit;
	}

	eFuseWord = rtw_malloc2d(EFUSE_MAX_SECTION_88E, EFUSE_MAX_WORD_UNIT, sizeof(u16));
	if (!eFuseWord) {
		DBG_88E("%s: alloc eFuseWord fail!\n", __func__);
		goto exit;
	}

	/*  0. Refresh efuse init map as all oxFF. */
	for (i = 0; i < EFUSE_MAX_SECTION_88E; i++)
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++)
			eFuseWord[i][j] = 0xFFFF;

	/*  */
	/*  1. Read the first byte to check if efuse is empty!!! */
	/*  */
	/*  */
	rtemp8 = *(phymap + eFuse_Addr);
	if (rtemp8 != 0xFF) {
		efuse_utilized++;
		eFuse_Addr++;
	} else {
		DBG_88E("EFUSE is empty efuse_Addr-%d efuse_data =%x\n", eFuse_Addr, rtemp8);
		goto exit;
	}

	/*  */
	/*  2. Read real efuse content. Filter PG header and every section data. */
	/*  */
	while ((rtemp8 != 0xFF) && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E)) {
		/*  Check PG header for section num. */
		if ((rtemp8 & 0x1F) == 0x0F) {		/* extended header */
			u1temp = ((rtemp8 & 0xE0) >> 5);
			rtemp8 = *(phymap + eFuse_Addr);
			if ((rtemp8 & 0x0F) == 0x0F) {
				eFuse_Addr++;
				rtemp8 = *(phymap + eFuse_Addr);

				if (rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E))
					eFuse_Addr++;
				continue;
			} else {
				offset = ((rtemp8 & 0xF0) >> 1) | u1temp;
				wren = (rtemp8 & 0x0F);
				eFuse_Addr++;
			}
		} else {
			offset = ((rtemp8 >> 4) & 0x0f);
			wren = (rtemp8 & 0x0f);
		}

		if (offset < EFUSE_MAX_SECTION_88E) {
			/*  Get word enable value from PG header */
			for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++) {
				/*  Check word enable condition in the section */
				if (!(wren & 0x01)) {
					rtemp8 = *(phymap + eFuse_Addr);
					eFuse_Addr++;
					efuse_utilized++;
					eFuseWord[offset][i] = (rtemp8 & 0xff);
					if (eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E)
						break;
					rtemp8 = *(phymap + eFuse_Addr);
					eFuse_Addr++;
					efuse_utilized++;
					eFuseWord[offset][i] |= (((u16)rtemp8 << 8) & 0xff00);

					if (eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E)
						break;
				}
				wren >>= 1;
			}
		}
		/*  Read next PG header */
		rtemp8 = *(phymap + eFuse_Addr);

		if (rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E)) {
			efuse_utilized++;
			eFuse_Addr++;
		}
	}

	/*  */
	/*  3. Collect 16 sections and 4 word unit into Efuse map. */
	/*  */
	for (i = 0; i < EFUSE_MAX_SECTION_88E; i++) {
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++) {
			efuseTbl[(i * 8) + (j * 2)] = (eFuseWord[i][j] & 0xff);
			efuseTbl[(i * 8) + ((j * 2) + 1)] = ((eFuseWord[i][j] >> 8) & 0xff);
		}
	}

	/*  */
	/*  4. Copy from Efuse map to output pointer memory!!! */
	/*  */
	for (i = 0; i < _size_byte; i++)
		pbuf[i] = efuseTbl[_offset + i];

	/*  */
	/*  5. Calculate Efuse utilization. */
	/*  */

exit:
	kfree(efuseTbl);
	kfree(eFuseWord);
}

static void efuse_read_phymap_from_txpktbuf(
	struct adapter  *adapter,
	int bcnhead,	/* beacon head, where FW store len(2-byte) and efuse physical map. */
	u8 *content,	/* buffer to store efuse physical map */
	u16 *size	/* for efuse content: the max byte to read. will update to byte read */
	)
{
	u16 dbg_addr = 0;
	u32 start  = 0, passing_time = 0;
	u8 reg_0x143 = 0;
	__le32 lo32 = 0, hi32 = 0;
	u16 len = 0, count = 0;
	int i = 0;
	u16 limit = *size;

	u8 *pos = content;

	if (bcnhead < 0) /* if not valid */
		bcnhead = rtw_read8(adapter, REG_TDECTRL + 1);

	DBG_88E("%s bcnhead:%d\n", __func__, bcnhead);

	rtw_write8(adapter, REG_PKT_BUFF_ACCESS_CTRL, TXPKT_BUF_SELECT);

	dbg_addr = bcnhead * 128 / 8; /* 8-bytes addressing */

	while (1) {
		rtw_write16(adapter, REG_PKTBUF_DBG_ADDR, dbg_addr + i);

		rtw_write8(adapter, REG_TXPKTBUF_DBG, 0);
		start = jiffies;
		while (!(reg_0x143 = rtw_read8(adapter, REG_TXPKTBUF_DBG)) &&
		       (passing_time = rtw_get_passing_time_ms(start)) < 1000) {
			DBG_88E("%s polling reg_0x143:0x%02x, reg_0x106:0x%02x\n", __func__, reg_0x143, rtw_read8(adapter, 0x106));
			rtw_usleep_os(100);
		}

		/* data from EEPROM needs to be in LE */
		lo32 = cpu_to_le32(rtw_read32(adapter, REG_PKTBUF_DBG_DATA_L));
		hi32 = cpu_to_le32(rtw_read32(adapter, REG_PKTBUF_DBG_DATA_H));

		if (i == 0) {
			/* Although lenc is only used in a debug statement,
			 * do not remove it as the rtw_read16() call consumes
			 * 2 bytes from the EEPROM source.
			 */
			u16 lenc = rtw_read16(adapter, REG_PKTBUF_DBG_DATA_L);

			len = le32_to_cpu(lo32) & 0x0000ffff;

			limit = (len - 2 < limit) ? len - 2 : limit;

			DBG_88E("%s len:%u, lenc:%u\n", __func__, len, lenc);

			memcpy(pos, ((u8 *)&lo32) + 2, (limit >= count + 2) ? 2 : limit - count);
			count += (limit >= count + 2) ? 2 : limit - count;
			pos = content + count;
		} else {
			memcpy(pos, ((u8 *)&lo32), (limit >= count + 4) ? 4 : limit - count);
			count += (limit >= count + 4) ? 4 : limit - count;
			pos = content + count;
		}

		if (limit > count && len - 2 > count) {
			memcpy(pos, (u8 *)&hi32, (limit >= count + 4) ? 4 : limit - count);
			count += (limit >= count + 4) ? 4 : limit - count;
			pos = content + count;
		}

		if (limit <= count || len - 2 <= count)
			break;
		i++;
	}
	rtw_write8(adapter, REG_PKT_BUFF_ACCESS_CTRL, DISABLE_TRXPKT_BUF_ACCESS);
	DBG_88E("%s read count:%u\n", __func__, count);
	*size = count;
}

static s32 iol_read_efuse(struct adapter *padapter, u8 txpktbuf_bndy, u16 offset, u16 size_byte, u8 *logical_map)
{
	s32 status = _FAIL;
	u8 physical_map[512];
	u16 size = 512;

	rtw_write8(padapter, REG_TDECTRL + 1, txpktbuf_bndy);
	memset(physical_map, 0xFF, 512);
	rtw_write8(padapter, REG_PKT_BUFF_ACCESS_CTRL, TXPKT_BUF_SELECT);
	status = iol_execute(padapter, CMD_READ_EFUSE_MAP);
	if (status == _SUCCESS)
		efuse_read_phymap_from_txpktbuf(padapter, txpktbuf_bndy, physical_map, &size);
	efuse_phymap_to_logical(physical_map, offset, size_byte, logical_map);
	return status;
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

	rtw_write8(padapter, REG_TDECTRL + 1, iocfg_bndy);
	rst = iol_execute(padapter, CMD_IOCONFIG);
	return rst;
}

int rtl8188e_IOL_exec_cmds_sync(struct adapter *adapter, struct xmit_frame *xmit_frame, u32 max_wating_ms, u32 bndy_cnt)
{
	struct pkt_attrib *pattrib = &xmit_frame->attrib;
	u8 i;
	int ret = _FAIL;

	if (rtw_IOL_append_END_cmd(xmit_frame) != _SUCCESS)
		goto exit;
	if (rtw_usb_bulk_size_boundary(adapter, TXDESC_SIZE + pattrib->last_txcmdsz)) {
		if (rtw_IOL_append_END_cmd(xmit_frame) != _SUCCESS)
			goto exit;
	}

	dump_mgntframe_and_wait(adapter, xmit_frame, max_wating_ms);

	iol_mode_enable(adapter, 1);
	for (i = 0; i < bndy_cnt; i++) {
		u8 page_no = 0;
		page_no = i * 2;
		ret = iol_ioconfig(adapter, page_no);
		if (ret != _SUCCESS)
			break;
	}
	iol_mode_enable(adapter, 0);
exit:
	/* restore BCN_HEAD */
	rtw_write8(adapter, REG_TDECTRL + 1, 0);
	return ret;
}

static void _FWDownloadEnable(struct adapter *padapter, bool enable)
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

static int _PageWrite(struct adapter *padapter, u32 page, void *buffer, u32 size)
{
	u8 value8;
	u8 u8Page = (u8)(page & 0x07);

	value8 = (rtw_read8(padapter, REG_MCUFWDL + 2) & 0xF8) | u8Page;
	rtw_write8(padapter, REG_MCUFWDL + 2, value8);

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
		ret = _PageWrite(padapter, page, bufferPtr + offset, MAX_PAGE_SIZE);

		if (ret == _FAIL)
			goto exit;
	}
	if (remainSize) {
		offset = pageNums * MAX_PAGE_SIZE;
		page = pageNums;
		ret = _PageWrite(padapter, page, bufferPtr + offset, remainSize);

		if (ret == _FAIL)
			goto exit;
	}
exit:
	return ret;
}

void _8051Reset88E(struct adapter *padapter)
{
	u8 u1bTmp;

	u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN + 1);
	rtw_write8(padapter, REG_SYS_FUNC_EN + 1, u1bTmp & (~BIT(2)));
	rtw_write8(padapter, REG_SYS_FUNC_EN + 1, u1bTmp | (BIT(2)));
	DBG_88E("=====> _8051Reset88E(): 8051 reset success .\n");
}

static s32 _FWFreeToGo(struct adapter *padapter)
{
	u32	counter = 0;
	u32	value32;

	/*  polling CheckSum report */
	do {
		value32 = rtw_read32(padapter, REG_MCUFWDL);
		if (value32 & FWDL_ChkSum_rpt)
			break;
	} while (counter++ < POLLING_READY_TIMEOUT_COUNT);

	if (counter >= POLLING_READY_TIMEOUT_COUNT) {
		DBG_88E("%s: chksum report fail! REG_MCUFWDL:0x%08x\n", __func__, value32);
		return _FAIL;
	}
	DBG_88E("%s: Checksum report OK! REG_MCUFWDL:0x%08x\n", __func__, value32);

	value32 = rtw_read32(padapter, REG_MCUFWDL);
	value32 |= MCUFWDL_RDY;
	value32 &= ~WINTINI_RDY;
	rtw_write32(padapter, REG_MCUFWDL, value32);

	_8051Reset88E(padapter);

	/*  polling for FW ready */
	counter = 0;
	do {
		value32 = rtw_read32(padapter, REG_MCUFWDL);
		if (value32 & WINTINI_RDY) {
			DBG_88E("%s: Polling FW ready success!! REG_MCUFWDL:0x%08x\n", __func__, value32);
			return _SUCCESS;
		}
		udelay(5);
	} while (counter++ < POLLING_READY_TIMEOUT_COUNT);

	DBG_88E("%s: Polling FW ready fail!! REG_MCUFWDL:0x%08x\n", __func__, value32);
	return _FAIL;
}

static int load_firmware(struct rt_firmware *pFirmware, struct device *device)
{
	s32	rtStatus = _SUCCESS;
	const struct firmware *fw;
	const char *fw_name = "rtlwifi/rtl8188eufw.bin";
	int err = request_firmware(&fw, fw_name, device);

	if (err) {
		pr_err("Request firmware failed with error 0x%x\n", err);
		rtStatus = _FAIL;
		goto Exit;
	}
	if (!fw) {
		pr_err("Firmware %s not available\n", fw_name);
		rtStatus = _FAIL;
		goto Exit;
	}
	if (fw->size > FW_8188E_SIZE) {
		rtStatus = _FAIL;
		goto Exit;
	}

	pFirmware->szFwBuffer = kzalloc(FW_8188E_SIZE, GFP_KERNEL);
	if (!pFirmware->szFwBuffer) {
		pr_err("Failed to allocate pFirmware->szFwBuffer\n");
		rtStatus = _FAIL;
		goto Exit;
	}
	memcpy(pFirmware->szFwBuffer, fw->data, fw->size);
	pFirmware->ulFwLength = fw->size;
	release_firmware(fw);
	dev_dbg(device, "!bUsedWoWLANFw, FmrmwareLen:%d+\n", pFirmware->ulFwLength);

Exit:
	return rtStatus;
}

s32 rtl8188e_FirmwareDownload(struct adapter *padapter)
{
	s32	rtStatus = _SUCCESS;
	u8 writeFW_retry = 0;
	u32 fwdl_start_time;
	struct hal_data_8188e *pHalData = &padapter->haldata;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct device *device = dvobj_to_dev(dvobj);
	struct rt_firmware_hdr *pFwHdr = NULL;
	u8 *pFirmwareBuf;
	u32 FirmwareLen;
	static int log_version;

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
	if (rtw_read8(padapter, REG_MCUFWDL) & RAM_DL_SEL) { /* 8051 RAM code */
		rtw_write8(padapter, REG_MCUFWDL, 0x00);
		_8051Reset88E(padapter);
	}

	_FWDownloadEnable(padapter, true);
	fwdl_start_time = jiffies;
	while (1) {
		/* reset the FWDL chksum */
		rtw_write8(padapter, REG_MCUFWDL, rtw_read8(padapter, REG_MCUFWDL) | FWDL_ChkSum_rpt);

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

Exit:
	return rtStatus;
}

void rtl8188e_InitializeFirmwareVars(struct adapter *padapter)
{
	struct hal_data_8188e *pHalData = &padapter->haldata;

	/*  Init Fw LPS related. */
	padapter->pwrctrlpriv.bFwCurrentInPSMode = false;

	/*  Init H2C counter. by tynli. 2009.12.09. */
	pHalData->LastHMEBoxNum = 0;
}

/*  */
/*			Efuse related code */
/*  */
enum{
		VOLTAGE_V25						= 0x03,
		LDOE25_SHIFT						= 28,
	};

void rtl8188e_EfusePowerSwitch(struct adapter *pAdapter, u8 PwrState)
{
	u16	tmpV16;

	if (PwrState) {
		rtw_write8(pAdapter, REG_EFUSE_ACCESS, EFUSE_ACCESS_ON);

		/*  1.2V Power: From VDDON with Power Cut(0x0000h[15]), defualt valid */
		tmpV16 = rtw_read16(pAdapter, REG_SYS_ISO_CTRL);
		if (!(tmpV16 & PWC_EV12V)) {
			tmpV16 |= PWC_EV12V;
			rtw_write16(pAdapter, REG_SYS_ISO_CTRL, tmpV16);
		}
		/*  Reset: 0x0000h[28], default valid */
		tmpV16 =  rtw_read16(pAdapter, REG_SYS_FUNC_EN);
		if (!(tmpV16 & FEN_ELDR)) {
			tmpV16 |= FEN_ELDR;
			rtw_write16(pAdapter, REG_SYS_FUNC_EN, tmpV16);
		}

		/*  Clock: Gated(0x0008h[5]) 8M(0x0008h[1]) clock from ANA, default valid */
		tmpV16 = rtw_read16(pAdapter, REG_SYS_CLKR);
		if ((!(tmpV16 & LOADER_CLK_EN))  || (!(tmpV16 & ANA8M))) {
			tmpV16 |= (LOADER_CLK_EN | ANA8M);
			rtw_write16(pAdapter, REG_SYS_CLKR, tmpV16);
		}
	} else {
		rtw_write8(pAdapter, REG_EFUSE_ACCESS, EFUSE_ACCESS_OFF);
	}
}

static void Hal_EfuseReadEFuse88E(struct adapter *Adapter,
	u16			_offset,
	u16			_size_byte,
	u8 *pbuf)
{
	u8 *efuseTbl = NULL;
	u8 rtemp8[1];
	u16	eFuse_Addr = 0;
	u8 offset, wren;
	u16	i, j;
	u16	**eFuseWord = NULL;
	u16	efuse_utilized = 0;
	u8 u1temp = 0;

	/*  */
	/*  Do NOT excess total size of EFuse table. Added by Roger, 2008.11.10. */
	/*  */
	if ((_offset + _size_byte) > EFUSE_MAP_LEN_88E) {/*  total E-Fuse table is 512bytes */
		DBG_88E("Hal_EfuseReadEFuse88E(): Invalid offset(%#x) with read bytes(%#x)!!\n", _offset, _size_byte);
		goto exit;
	}

	efuseTbl = kzalloc(EFUSE_MAP_LEN_88E, GFP_KERNEL);
	if (!efuseTbl) {
		DBG_88E("%s: alloc efuseTbl fail!\n", __func__);
		goto exit;
	}

	eFuseWord = rtw_malloc2d(EFUSE_MAX_SECTION_88E, EFUSE_MAX_WORD_UNIT, sizeof(u16));
	if (!eFuseWord) {
		DBG_88E("%s: alloc eFuseWord fail!\n", __func__);
		goto exit;
	}

	/*  0. Refresh efuse init map as all oxFF. */
	for (i = 0; i < EFUSE_MAX_SECTION_88E; i++)
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++)
			eFuseWord[i][j] = 0xFFFF;

	/*  */
	/*  1. Read the first byte to check if efuse is empty!!! */
	/*  */
	/*  */
	ReadEFuseByte(Adapter, eFuse_Addr, rtemp8);
	if (*rtemp8 != 0xFF) {
		efuse_utilized++;
		eFuse_Addr++;
	} else {
		DBG_88E("EFUSE is empty efuse_Addr-%d efuse_data =%x\n", eFuse_Addr, *rtemp8);
		goto exit;
	}

	/*  */
	/*  2. Read real efuse content. Filter PG header and every section data. */
	/*  */
	while ((*rtemp8 != 0xFF) && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E)) {
		/*  Check PG header for section num. */
		if ((*rtemp8 & 0x1F) == 0x0F) {		/* extended header */
			u1temp = ((*rtemp8 & 0xE0) >> 5);

			ReadEFuseByte(Adapter, eFuse_Addr, rtemp8);

			if ((*rtemp8 & 0x0F) == 0x0F) {
				eFuse_Addr++;
				ReadEFuseByte(Adapter, eFuse_Addr, rtemp8);

				if (*rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E))
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

		if (offset < EFUSE_MAX_SECTION_88E) {
			/*  Get word enable value from PG header */

			for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++) {
				/*  Check word enable condition in the section */
				if (!(wren & 0x01)) {
					ReadEFuseByte(Adapter, eFuse_Addr, rtemp8);
					eFuse_Addr++;
					efuse_utilized++;
					eFuseWord[offset][i] = (*rtemp8 & 0xff);
					if (eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E)
						break;
					ReadEFuseByte(Adapter, eFuse_Addr, rtemp8);
					eFuse_Addr++;
					efuse_utilized++;
					eFuseWord[offset][i] |= (((u16)*rtemp8 << 8) & 0xff00);
					if (eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E)
						break;
				}
				wren >>= 1;
			}
		}

		/*  Read next PG header */
		ReadEFuseByte(Adapter, eFuse_Addr, rtemp8);

		if (*rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E)) {
			efuse_utilized++;
			eFuse_Addr++;
		}
	}

	/*  3. Collect 16 sections and 4 word unit into Efuse map. */
	for (i = 0; i < EFUSE_MAX_SECTION_88E; i++) {
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++) {
			efuseTbl[(i * 8) + (j * 2)] = (eFuseWord[i][j] & 0xff);
			efuseTbl[(i * 8) + ((j * 2) + 1)] = ((eFuseWord[i][j] >> 8) & 0xff);
		}
	}

	/*  4. Copy from Efuse map to output pointer memory!!! */
	for (i = 0; i < _size_byte; i++)
		pbuf[i] = efuseTbl[_offset + i];

	/*  5. Calculate Efuse utilization. */
	SetHwReg8188EU(Adapter, HW_VAR_EFUSE_BYTES, (u8 *)&eFuse_Addr);

exit:
	kfree(efuseTbl);
	kfree(eFuseWord);
}

static void ReadEFuseByIC(struct adapter *Adapter, u16 _offset, u16 _size_byte, u8 *pbuf)
{
	int ret = _FAIL;
	if (rtw_IOL_applied(Adapter)) {
		rtl8188eu_InitPowerOn(Adapter);

		iol_mode_enable(Adapter, 1);
		ret = iol_read_efuse(Adapter, 0, _offset, _size_byte, pbuf);
		iol_mode_enable(Adapter, 0);

		if (_SUCCESS == ret)
			return;
	}

	Hal_EfuseReadEFuse88E(Adapter, _offset, _size_byte, pbuf);
}

void rtl8188e_ReadEFuse(struct adapter *Adapter, u16 _offset, u16 _size_byte, u8 *pbuf)
{
	ReadEFuseByIC(Adapter, _offset, _size_byte, pbuf);
}

void rtl8188e_read_chip_version(struct adapter *padapter)
{
	u32				value32;
	struct HAL_VERSION		ChipVersion;
	struct hal_data_8188e *pHalData = &padapter->haldata;

	value32 = rtw_read32(padapter, REG_SYS_CFG);
	ChipVersion.ChipType = ((value32 & RTL_ID) ? TEST_CHIP : NORMAL_CHIP);

	ChipVersion.VendorType = ((value32 & VENDOR_ID) ? CHIP_VENDOR_UMC : CHIP_VENDOR_TSMC);
	ChipVersion.CUTVersion = (value32 & CHIP_VER_RTL_MASK) >> CHIP_VER_RTL_SHIFT; /*  IC version (CUT) */
	ChipVersion.ROMVer = 0;	/*  ROM code version. */

	dump_chip_info(ChipVersion);

	pHalData->VersionID = ChipVersion;
}

void rtl8188e_SetHalODMVar(struct adapter *Adapter, void *pValue1, bool bSet)
{
	struct hal_data_8188e *pHalData = &Adapter->haldata;
	struct odm_dm_struct *podmpriv = &pHalData->odmpriv;
	struct sta_info *psta = (struct sta_info *)pValue1;

	if (bSet) {
		DBG_88E("### Set STA_(%d) info\n", psta->mac_id);
		podmpriv->pODM_StaInfo[psta->mac_id] = psta;
		ODM_RAInfo_Init(podmpriv, psta->mac_id);
	} else {
		DBG_88E("### Clean STA_(%d) info\n", psta->mac_id);
		podmpriv->pODM_StaInfo[psta->mac_id] = NULL;
	}
}

void hal_notch_filter_8188e(struct adapter *adapter, bool enable)
{
	if (enable) {
		DBG_88E("Enable notch filter\n");
		rtw_write8(adapter, rOFDM0_RxDSP + 1, rtw_read8(adapter, rOFDM0_RxDSP + 1) | BIT(1));
	} else {
		DBG_88E("Disable notch filter\n");
		rtw_write8(adapter, rOFDM0_RxDSP + 1, rtw_read8(adapter, rOFDM0_RxDSP + 1) & ~BIT(1));
	}
}

u8 GetEEPROMSize8188E(struct adapter *padapter)
{
	u8 size = 0;
	u32	cr;

	cr = rtw_read16(padapter, REG_9346CR);
	/*  6: EEPROM used is 93C46, 4: boot from E-Fuse. */
	size = (cr & BOOT_FROM_EEPROM) ? 6 : 4;

	netdev_dbg(padapter->pnetdev, "EEPROM type is %s\n",
		   size == 4 ? "E-FUSE" : "93C46");

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

	rtw_write32(padapter, LLTReg, value);

	/* polling */
	do {
		value = rtw_read32(padapter, LLTReg);
		if (_LLT_NO_ACTIVE == _LLT_OP_VALUE(value))
			break;

		if (count > POLLING_LLT_THRESHOLD) {
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
Hal_EfuseParseIDCode88E(
		struct adapter *padapter,
		u8 *hwinfo
	)
{
	struct eeprom_priv *pEEPROM = &padapter->eeprompriv;
	u16			EEPROMId;

	/*  Check 0x8129 again for making sure autoload status!! */
	EEPROMId = le16_to_cpu(*((__le16 *)hwinfo));
	if (EEPROMId != RTL_EEPROM_ID) {
		pr_err("EEPROM ID(%#x) is invalid!!\n", EEPROMId);
		pEEPROM->bautoload_fail_flag = true;
	} else {
		pEEPROM->bautoload_fail_flag = false;
	}

	pr_info("EEPROM ID = 0x%04x\n", EEPROMId);
}

static void Hal_ReadPowerValueFromPROM_8188E(struct txpowerinfo24g *pwrInfo24G, u8 *PROMContent, bool AutoLoadFail)
{
	u32 rfPath, eeAddr = EEPROM_TX_PWR_INX_88E, group, TxCount = 0;

	memset(pwrInfo24G, 0, sizeof(struct txpowerinfo24g));

	if (AutoLoadFail) {
		for (rfPath = 0; rfPath < RF_PATH_MAX; rfPath++) {
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

	for (rfPath = 0; rfPath < RF_PATH_MAX; rfPath++) {
		/* 2.4G default value */
		for (group = 0; group < MAX_CHNL_GROUP_24G; group++) {
			pwrInfo24G->IndexCCK_Base[rfPath][group] =	PROMContent[eeAddr++];
			if (pwrInfo24G->IndexCCK_Base[rfPath][group] == 0xFF)
				pwrInfo24G->IndexCCK_Base[rfPath][group] = EEPROM_DEFAULT_24G_INDEX;
		}
		for (group = 0; group < MAX_CHNL_GROUP_24G - 1; group++) {
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
					pwrInfo24G->BW20_Diff[rfPath][TxCount] = (PROMContent[eeAddr] & 0xf0) >> 4;
					if (pwrInfo24G->BW20_Diff[rfPath][TxCount] & BIT(3))		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->BW20_Diff[rfPath][TxCount] |= 0xF0;
				}

				if (PROMContent[eeAddr] == 0xFF) {
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_24G_OFDM_DIFF;
				} else {
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	(PROMContent[eeAddr] & 0x0f);
					if (pwrInfo24G->OFDM_Diff[rfPath][TxCount] & BIT(3))		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->OFDM_Diff[rfPath][TxCount] |= 0xF0;
				}
				pwrInfo24G->CCK_Diff[rfPath][TxCount] = 0;
				eeAddr++;
			} else {
				if (PROMContent[eeAddr] == 0xFF) {
					pwrInfo24G->BW40_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
				} else {
					pwrInfo24G->BW40_Diff[rfPath][TxCount] =	(PROMContent[eeAddr] & 0xf0) >> 4;
					if (pwrInfo24G->BW40_Diff[rfPath][TxCount] & BIT(3))		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->BW40_Diff[rfPath][TxCount] |= 0xF0;
				}

				if (PROMContent[eeAddr] == 0xFF) {
					pwrInfo24G->BW20_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
				} else {
					pwrInfo24G->BW20_Diff[rfPath][TxCount] =	(PROMContent[eeAddr] & 0x0f);
					if (pwrInfo24G->BW20_Diff[rfPath][TxCount] & BIT(3))		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->BW20_Diff[rfPath][TxCount] |= 0xF0;
				}
				eeAddr++;

				if (PROMContent[eeAddr] == 0xFF) {
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] = EEPROM_DEFAULT_DIFF;
				} else {
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	(PROMContent[eeAddr] & 0xf0) >> 4;
					if (pwrInfo24G->OFDM_Diff[rfPath][TxCount] & BIT(3))		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->OFDM_Diff[rfPath][TxCount] |= 0xF0;
				}

				if (PROMContent[eeAddr] == 0xFF) {
					pwrInfo24G->CCK_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
				} else {
					pwrInfo24G->CCK_Diff[rfPath][TxCount] =	(PROMContent[eeAddr] & 0x0f);
					if (pwrInfo24G->CCK_Diff[rfPath][TxCount] & BIT(3))		/* 4bit sign number to 8 bit sign number */
						pwrInfo24G->CCK_Diff[rfPath][TxCount] |= 0xF0;
				}
				eeAddr++;
			}
		}
	}
}

static void hal_get_chnl_group_88e(u8 chnl, u8 *group)
{
	if (chnl < 3)			/*  Channel 1-2 */
		*group = 0;
	else if (chnl < 6)		/*  Channel 3-5 */
		*group = 1;
	else if (chnl < 9)		/*  Channel 6-8 */
		*group = 2;
	else if (chnl < 12)		/*  Channel 9-11 */
		*group = 3;
	else if (chnl < 14)		/*  Channel 12-13 */
		*group = 4;
	else if (chnl == 14)		/*  Channel 14 */
		*group = 5;
}

void Hal_ReadPowerSavingMode88E(struct adapter *padapter, u8 *hwinfo, bool AutoLoadFail)
{
	if (AutoLoadFail) {
		padapter->pwrctrlpriv.bSupportRemoteWakeup = false;
	} else {
		/* hw power down mode selection , 0:rf-off / 1:power down */

		/*  decide hw if support remote wakeup function */
		/*  if hw supported, 8051 (SIE) will generate WeakUP signal(D+/D- toggle) when autoresume */
		padapter->pwrctrlpriv.bSupportRemoteWakeup = (hwinfo[EEPROM_USB_OPTIONAL_FUNCTION0] & BIT(1)) ? true : false;

		DBG_88E("%s , bSupportRemoteWakeup(%x)\n", __func__,
			padapter->pwrctrlpriv.bSupportRemoteWakeup);

		DBG_88E("### PS params =>  power_mgnt(%x), usbss_enable(%x) ###\n", padapter->registrypriv.power_mgnt, padapter->registrypriv.usbss_enable);
	}
}

void Hal_ReadTxPowerInfo88E(struct adapter *padapter, u8 *PROMContent, bool AutoLoadFail)
{
	struct hal_data_8188e *pHalData = &padapter->haldata;
	struct txpowerinfo24g pwrInfo24G;
	u8 ch, group;
	u8 TxCount;

	Hal_ReadPowerValueFromPROM_8188E(&pwrInfo24G, PROMContent, AutoLoadFail);

	for (ch = 0; ch < CHANNEL_MAX_NUMBER; ch++) {
		hal_get_chnl_group_88e(ch, &group);

		pHalData->Index24G_CCK_Base[ch] = pwrInfo24G.IndexCCK_Base[0][group];
		if (ch == 14)
			pHalData->Index24G_BW40_Base[ch] = pwrInfo24G.IndexBW40_Base[0][4];
		else
			pHalData->Index24G_BW40_Base[ch] = pwrInfo24G.IndexBW40_Base[0][group];

		DBG_88E("======= Path 0, Channel %d =======\n", ch);
		DBG_88E("Index24G_CCK_Base[%d] = 0x%x\n", ch, pHalData->Index24G_CCK_Base[ch]);
		DBG_88E("Index24G_BW40_Base[%d] = 0x%x\n", ch, pHalData->Index24G_BW40_Base[ch]);
	}
	for (TxCount = 0; TxCount < MAX_TX_COUNT; TxCount++) {
		pHalData->OFDM_24G_Diff[TxCount] = pwrInfo24G.OFDM_Diff[0][TxCount];
		pHalData->BW20_24G_Diff[TxCount] = pwrInfo24G.BW20_Diff[0][TxCount];
		DBG_88E("======= TxCount %d =======\n", TxCount);
		DBG_88E("OFDM_24G_Diff[%d] = %d\n", TxCount, pHalData->OFDM_24G_Diff[TxCount]);
		DBG_88E("BW20_24G_Diff[%d] = %d\n", TxCount, pHalData->BW20_24G_Diff[TxCount]);
	}

	/*  2010/10/19 MH Add Regulator recognize for CU. */
	if (!AutoLoadFail) {
		pHalData->EEPROMRegulatory = (PROMContent[EEPROM_RF_BOARD_OPTION_88E] & 0x7);	/* bit0~2 */
		if (PROMContent[EEPROM_RF_BOARD_OPTION_88E] == 0xFF)
			pHalData->EEPROMRegulatory = (EEPROM_DEFAULT_BOARD_OPTION & 0x7);	/* bit0~2 */
	} else {
		pHalData->EEPROMRegulatory = 0;
	}
	DBG_88E("EEPROMRegulatory = 0x%x\n", pHalData->EEPROMRegulatory);
}

void Hal_EfuseParseXtal_8188E(struct adapter *pAdapter, u8 *hwinfo, bool AutoLoadFail)
{
	struct hal_data_8188e *pHalData = &pAdapter->haldata;

	if (!AutoLoadFail) {
		pHalData->CrystalCap = hwinfo[EEPROM_XTAL_88E];
		if (pHalData->CrystalCap == 0xFF)
			pHalData->CrystalCap = EEPROM_Default_CrystalCap_88E;
	} else {
		pHalData->CrystalCap = EEPROM_Default_CrystalCap_88E;
	}
	DBG_88E("CrystalCap: 0x%2x\n", pHalData->CrystalCap);
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

void Hal_ReadAntennaDiversity88E(struct adapter *pAdapter, u8 *PROMContent, bool AutoLoadFail)
{
	struct hal_data_8188e *pHalData = &pAdapter->haldata;
	struct registry_priv	*registry_par = &pAdapter->registrypriv;

	if (!AutoLoadFail) {
		/*  Antenna Diversity setting. */
		if (registry_par->antdiv_cfg == 2) { /*  2:By EFUSE */
			pHalData->AntDivCfg = (PROMContent[EEPROM_RF_BOARD_OPTION_88E] & 0x18) >> 3;
			if (PROMContent[EEPROM_RF_BOARD_OPTION_88E] == 0xFF)
				pHalData->AntDivCfg = (EEPROM_DEFAULT_BOARD_OPTION & 0x18) >> 3;
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
	}
	DBG_88E("EEPROM : AntDivCfg = %x, TRxAntDivType = %x\n", pHalData->AntDivCfg, pHalData->TRxAntDivType);
}

void Hal_ReadThermalMeter_88E(struct adapter *Adapter, u8 *PROMContent, bool AutoloadFail)
{
	struct hal_data_8188e *pHalData = &Adapter->haldata;

	/*  ThermalMeter from EEPROM */
	if (!AutoloadFail)
		pHalData->EEPROMThermalMeter = PROMContent[EEPROM_THERMAL_METER_88E];
	else
		pHalData->EEPROMThermalMeter = EEPROM_Default_ThermalMeter_88E;

	if (pHalData->EEPROMThermalMeter == 0xff || AutoloadFail)
		pHalData->EEPROMThermalMeter = EEPROM_Default_ThermalMeter_88E;

	DBG_88E("ThermalMeter = 0x%x\n", pHalData->EEPROMThermalMeter);
}
