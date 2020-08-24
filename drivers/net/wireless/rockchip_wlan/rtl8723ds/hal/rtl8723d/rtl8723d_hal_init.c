/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#define _HAL_INIT_C_

#include <rtl8723d_hal.h>
#include "hal_com_h2c.h"
#include <hal_com.h>
#include "hal8723d_fw.h"

#ifndef CONFIG_DLFW_TXPKT
#define DL_FW_MAX 15
#else
#define FW_DOWNLOAD_SIZE_8723D 8192
#endif

static void
_FWDownloadEnable(
		PADAPTER		padapter,
		BOOLEAN			enable
)
{
	u8	tmp, count = 0;

	if (enable) {
		/* 8051 enable */
		tmp = rtw_read8(padapter, REG_SYS_FUNC_EN + 1);
		rtw_write8(padapter, REG_SYS_FUNC_EN + 1, tmp | 0x04);

		tmp = rtw_read8(padapter, REG_MCUFWDL);
		rtw_write8(padapter, REG_MCUFWDL, tmp | 0x01);

		do {
			tmp = rtw_read8(padapter, REG_MCUFWDL);
			if (tmp & 0x01)
				break;
			rtw_write8(padapter, REG_MCUFWDL, tmp | 0x01);
			rtw_msleep_os(1);
		} while (count++ < 100);
		if (count > 0)
			RTW_INFO("%s: !!!!!!!!Write 0x80 Fail!: count = %d\n", __func__, count);

		/* 8051 reset */
		tmp = rtw_read8(padapter, REG_MCUFWDL + 2);
		rtw_write8(padapter, REG_MCUFWDL + 2, tmp & 0xf7);
	} else {
		/* MCU firmware download disable. */
		tmp = rtw_read8(padapter, REG_MCUFWDL);
		rtw_write8(padapter, REG_MCUFWDL, tmp & 0xfe);
	}
}

#ifdef CONFIG_USB_HCI
static int _BlockWrite(struct _ADAPTER *padapter, void *buffer, u32 buffSize)
{
	int ret = _SUCCESS;

	u32 blockSize_p1 = 254;	/* Phase #1 */
	u32 blockSize_p2 = 8;	/* Phase #2 : Use 8-byte, if Phase#1 use big size to write FW. */
	u32 blockSize_p3 = 1;	/* Phase #3 : Use 1-byte, the remnant of FW image. */
	u32 blockCount_p1 = 0, blockCount_p2 = 0, blockCount_p3 = 0;
	u32 remainSize_p1 = 0, remainSize_p2 = 0;
	u8 *bufferPtr = (u8 *)buffer;
	u32 i = 0, offset = 0;


	/* Phase #1 */
	blockCount_p1 = buffSize / blockSize_p1;
	remainSize_p1 = buffSize % blockSize_p1;

	for (i = 0; i < blockCount_p1; i++) {
		ret = rtw_writeN(padapter,
				 (FW_8723D_START_ADDRESS + i * blockSize_p1),
				 blockSize_p1, (bufferPtr + i * blockSize_p1));
		if (ret == _FAIL) {
			RTW_ERR("====>%s %d i:%d\n", __func__, __LINE__, i);
			goto exit;
		}
	}

	/* Phase #2 */
	if (remainSize_p1) {
		offset = blockCount_p1 * blockSize_p1;

		blockCount_p2 = remainSize_p1 / blockSize_p2;
		remainSize_p2 = remainSize_p1 % blockSize_p2;

		for (i = 0; i < blockCount_p2; i++) {
			ret = rtw_writeN(padapter,
					 (FW_8723D_START_ADDRESS + offset + i * blockSize_p2),
					 blockSize_p2,
					 (bufferPtr + offset + i * blockSize_p2));
			if (ret == _FAIL)
				goto exit;
		}
	}

	/* Phase #3 */
	if (remainSize_p2) {
		offset = (blockCount_p1 * blockSize_p1) + (blockCount_p2 * blockSize_p2);

		blockCount_p3 = remainSize_p2 / blockSize_p3;

		for (i = 0 ; i < blockCount_p3 ; i++) {
			ret = rtw_write8(padapter, (FW_8723D_START_ADDRESS + offset + i), *(bufferPtr + offset + i));
			if (ret == _FAIL) {
				RTW_ERR("====>%s %d i:%d\n", __func__, __LINE__, i);
				goto exit;
			}
		}
	}

exit:
	return ret;
}
#else /* !CONFIG_USB_HCI */
static int _BlockWrite(struct _ADAPTER *padapter, void *buffer, u32 buffSize)
{
	int ret = _SUCCESS;

	u32 blockSize_p1 = 4;	/* Phase #1 : PCI muse use 4-byte write to download FW */
				/* Phase #2 : write the remnant of FW image. */
	u32 blockCount_p1 = 0;
	u32 remainSize_p1 = 0;
	u8 *bufferPtr = (u8 *)buffer;
	u32 i = 0, offset = 0;
#ifdef CONFIG_PCI_HCI
	u8 remainFW[4] = {0, 0, 0, 0};
#endif


	/* Phase #1 */
	blockCount_p1 = buffSize / blockSize_p1;
	remainSize_p1 = buffSize % blockSize_p1;

	for (i = 0; i < blockCount_p1; i++) {
		ret = rtw_write32(padapter,
				  (FW_8723D_START_ADDRESS + i * blockSize_p1),
				  le32_to_cpu(*((u32 *)(bufferPtr + i * blockSize_p1))));
		if (ret == _FAIL) {
			RTW_ERR("====>%s %d i:%d\n", __func__, __LINE__, i);
			goto exit;
		}
	}

	/* Phase #2 */
	if (remainSize_p1) {
		offset = (blockCount_p1 * blockSize_p1);
		bufferPtr += offset;
#ifdef CONFIG_PCI_HCI
		switch (remainSize_p1) {
		case 3:
			remainFW[2] = *(bufferPtr + 2);
		case 2:
			remainFW[1] = *(bufferPtr + 1);
		case 1:
			remainFW[0] = *(bufferPtr);
			ret = rtw_write32(padapter,
					  (FW_8723D_START_ADDRESS + offset),
					  le32_to_cpu(*(u32 *)remainFW));
		}
#else /* !CONFIG_PCI_HCI */
		for (i = 0; i < remainSize_p1; i++) {
			ret = rtw_write8(padapter,
					 (FW_8723D_START_ADDRESS + offset + i),
					 *(bufferPtr + i));
			if (ret == _FAIL) {
				RTW_ERR("====>%s %d i:%d\n", __func__, __LINE__, i);
				goto exit;
			}
		}
#endif /* !CONFIG_PCI_HCI */
	}

exit:
	return ret;
}
#endif /* !CONFIG_USB_HCI */

static int
_PageWrite(
			PADAPTER	padapter,
			u32			page,
			void			*buffer,
			u32			size
)
{
	u8 value8;
	u8 u8Page = (u8)(page & 0x07);

	value8 = (rtw_read8(padapter, REG_MCUFWDL + 2) & 0xF8) | u8Page;
	rtw_write8(padapter, REG_MCUFWDL + 2, value8);

	return _BlockWrite(padapter, buffer, size);
}
#ifdef CONFIG_PCI_HCI
static void
_FillDummy(
	u8		*pFwBuf,
	u32	*pFwLen
)
{
	u32	FwLen = *pFwLen;
	u8	remain = (u8)(FwLen % 4);

	remain = (remain == 0) ? 0 : (4 - remain);

	while (remain > 0) {
		pFwBuf[FwLen] = 0;
		FwLen++;
		remain--;
	}

	*pFwLen = FwLen;
}
#endif
static int
_WriteFW(
		PADAPTER padapter,
		void *buffer,
		u32 size
)
{
	/* Since we need dynamic decide method of dwonload fw, so we call this function to get chip version. */
	int ret = _SUCCESS;
	u32 pageNums, remainSize;
	u32 page, offset;
	u8 *bufferPtr = (u8 *)buffer;

#ifdef CONFIG_PCI_HCI
	/* 20100120 Joseph: Add for 88CE normal chip. */
	/* Fill in zero to make firmware image to dword alignment. */
	_FillDummy(bufferPtr, &size);
#endif

	pageNums = size / MAX_DLFW_PAGE_SIZE;
	/* RT_ASSERT((pageNums <= 4), ("Page numbers should not greater then 4\n")); */
	remainSize = size % MAX_DLFW_PAGE_SIZE;

	for (page = 0; page < pageNums; page++) {
		offset = page * MAX_DLFW_PAGE_SIZE;
		ret = _PageWrite(padapter, page, bufferPtr + offset, MAX_DLFW_PAGE_SIZE);

		if (ret == _FAIL) {
			printk(KERN_ERR "====>%s %d\n", __func__, __LINE__);
			goto exit;
		}
	}
	if (remainSize) {
		offset = pageNums * MAX_DLFW_PAGE_SIZE;
		page = pageNums;
		ret = _PageWrite(padapter, page, bufferPtr + offset, remainSize);

		if (ret == _FAIL) {
			printk(KERN_ERR "====>%s %d\n", __func__, __LINE__);
			goto exit;
		}
	}

exit:
	return ret;
}

void _8051Reset8723(PADAPTER padapter)
{
	u8 cpu_rst;
	u8 io_rst;

#if 0
	io_rst = rtw_read8(padapter, REG_RSV_CTRL);
	rtw_write8(padapter, REG_RSV_CTRL, io_rst & (~BIT(1)));
#endif

	/* Reset 8051(WLMCU) IO wrapper */
	/* 0x1c[8] = 0 */
	/* Suggested by Isaac@SD1 and Gimmy@SD1, coding by Lucas@20130624 */
	io_rst = rtw_read8(padapter, REG_RSV_CTRL + 1);
	io_rst &= ~BIT(0);
	rtw_write8(padapter, REG_RSV_CTRL + 1, io_rst);

	cpu_rst = rtw_read8(padapter, REG_SYS_FUNC_EN + 1);
	cpu_rst &= ~BIT(2);
	rtw_write8(padapter, REG_SYS_FUNC_EN + 1, cpu_rst);

#if 0
	io_rst = rtw_read8(padapter, REG_RSV_CTRL);
	rtw_write8(padapter, REG_RSV_CTRL, io_rst & (~BIT(1)));
#endif

	/* Enable 8051 IO wrapper	 */
	/* 0x1c[8] = 1 */
	io_rst = rtw_read8(padapter, REG_RSV_CTRL + 1);
	io_rst |= BIT(0);
	rtw_write8(padapter, REG_RSV_CTRL + 1, io_rst);

	cpu_rst = rtw_read8(padapter, REG_SYS_FUNC_EN + 1);
	cpu_rst |= BIT(2);
	rtw_write8(padapter, REG_SYS_FUNC_EN + 1, cpu_rst);

	RTW_INFO("%s: Finish\n", __FUNCTION__);
}

static s32 polling_fwdl_chksum(_adapter *adapter, u32 min_cnt, u32 timeout_ms)
{
	s32 ret = _FAIL;
	u32 value32;
	systime start = rtw_get_current_time();
	u32 cnt = 0;

	/* polling CheckSum report */
	do {
		cnt++;
		value32 = rtw_read32(adapter, REG_MCUFWDL);
		if (value32 & FWDL_ChkSum_rpt || RTW_CANNOT_IO(adapter))
			break;
		rtw_yield_os();
	} while (rtw_get_passing_time_ms(start) < timeout_ms || cnt < min_cnt);

	if (!(value32 & FWDL_ChkSum_rpt))
		goto exit;

	if (rtw_fwdl_test_trigger_chksum_fail())
		goto exit;

	ret = _SUCCESS;

exit:
	RTW_INFO("%s: Checksum report %s! (%u, %dms), REG_MCUFWDL:0x%08x\n", __FUNCTION__
		, (ret == _SUCCESS) ? "OK" : "Fail", cnt, rtw_get_passing_time_ms(start), value32);

	return ret;
}

static s32 _FWFreeToGo(_adapter *adapter, u32 min_cnt, u32 timeout_ms)
{
	s32 ret = _FAIL;
	u32	value32;
	systime start = rtw_get_current_time();
	u32 cnt = 0;
	u32 value_to_check = 0;
	u32 value_expected = (MCUFWDL_RDY | FWDL_ChkSum_rpt | WINTINI_RDY | RAM_DL_SEL);

	value32 = rtw_read32(adapter, REG_MCUFWDL);
	value32 |= MCUFWDL_RDY;
	value32 &= ~WINTINI_RDY;
	rtw_write32(adapter, REG_MCUFWDL, value32);

	_8051Reset8723(adapter);

	/*  polling for FW ready */
	do {
		cnt++;
		value32 = rtw_read32(adapter, REG_MCUFWDL);
		value_to_check = value32 & value_expected;
		if ((value_to_check == value_expected) || RTW_CANNOT_IO(adapter))
			break;
		rtw_yield_os();
	} while (rtw_get_passing_time_ms(start) < timeout_ms || cnt < min_cnt);

	if (value_to_check != value_expected)
		goto exit;

	if (rtw_fwdl_test_trigger_wintint_rdy_fail())
		goto exit;

	ret = _SUCCESS;

exit:
	RTW_INFO("%s: Polling FW ready %s! (%u, %dms), REG_MCUFWDL:0x%08x\n", __FUNCTION__
		, (ret == _SUCCESS) ? "OK" : "Fail", cnt, rtw_get_passing_time_ms(start), value32);

	return ret;
}

#define IS_FW_81xxC(padapter)	(((GET_HAL_DATA(padapter))->FirmwareSignature & 0xFFF0) == 0x88C0)

void rtl8723d_FirmwareSelfReset(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8	u1bTmp;
	u8	Delay = 100;

	if (!(IS_FW_81xxC(padapter) &&
	      ((pHalData->firmware_version < 0x21) ||
	       (pHalData->firmware_version == 0x21 &&
		pHalData->firmware_sub_version < 0x01)))) { /* after 88C Fw v33.1 */
		/* 0x1cf=0x20. Inform 8051 to reset. 2009.12.25. tynli_test */
		rtw_write8(padapter, REG_HMETFR + 3, 0x20);

		u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN + 1);
		while (u1bTmp & BIT(2)) {
			Delay--;
			if (Delay == 0)
				break;
			rtw_udelay_os(50);
			u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN + 1);
		}

		if (Delay == 0) {
			/* force firmware reset */
			u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN + 1);
			rtw_write8(padapter, REG_SYS_FUNC_EN + 1, u1bTmp & (~BIT(2)));
		}
	}
}

#ifdef CONFIG_FILE_FWIMG
	u8 FwBuffer[FW_8723D_SIZE];
#endif /* CONFIG_FILE_FWIMG */

#ifdef CONFIG_MP_INCLUDED
int _WriteBTFWtoTxPktBuf8723D(
			PADAPTER	Adapter,
			void			*buffer,
			u32			FwBufLen,
			u8			times
)
{
	int			rtStatus = _SUCCESS;
	/* u32				value32; */
	/* u8				numHQ, numLQ, numPubQ;//, txpktbuf_bndy; */
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	/* PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo); */
	u8				BcnValidReg;
	u8				count = 0, DLBcnCount = 0;
	u8 *FwbufferPtr = (u8 *)buffer;
	/* PRT_TCB			pTcb, ptempTcb; */
	/* PRT_TX_LOCAL_BUFFER pBuf; */

	u8 *ReservedPagePacket = NULL;
	u8 *pGenBufReservedPagePacket = NULL;
	u32				TotalPktLen, txpktbuf_bndy;
	/* u8				tmpReg422; */
	/* u8				u1bTmp; */
	u8			*pframe;
	struct xmit_priv	*pxmitpriv = &(Adapter->xmitpriv);
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	u8			txdesc_offset = TXDESC_OFFSET;
	u8			val8, RegFwHwTxQCtrl;
#ifdef CONFIG_PCI_HCI
	u8			u1bTmp;
#endif

#if 1/* #ifdef CONFIG_PCI_HCI */
	TotalPktLen = FwBufLen;
#else
	TotalPktLen = FwBufLen + pHalData->HWDescHeadLength;
#endif

	if ((TotalPktLen + TXDESC_OFFSET) > MAX_CMDBUF_SZ) {
		RTW_INFO(" WARNING %s => Total packet len = %d > MAX_CMDBUF_SZ:%d\n"
			, __FUNCTION__, (TotalPktLen + TXDESC_OFFSET), MAX_CMDBUF_SZ);
		return _FAIL;
	}

	pGenBufReservedPagePacket = rtw_zmalloc(TotalPktLen);/* GetGenTempBuffer (Adapter, TotalPktLen); */
	if (!pGenBufReservedPagePacket)
		return _FAIL;

	ReservedPagePacket = (u8 *)pGenBufReservedPagePacket;

	_rtw_memset(ReservedPagePacket, 0, TotalPktLen);

#if 1/* #ifdef CONFIG_PCI_HCI */
	_rtw_memcpy(ReservedPagePacket, FwbufferPtr, FwBufLen);

#else
	PlatformMoveMemory(ReservedPagePacket + Adapter->HWDescHeadLength , FwbufferPtr, FwBufLen);
#endif

	/* --------------------------------------------------------- */
	/* 1. Pause BCN */
	/* --------------------------------------------------------- */
	/* Set REG_CR bit 8. DMA beacon by SW. */
#ifdef CONFIG_PCI_HCI
	u1bTmp = PlatformEFIORead1Byte(Adapter, REG_CR + 1);
	PlatformEFIOWrite1Byte(Adapter,  REG_CR + 1, (u1bTmp | BIT(0)));
#else
	/* Remove for temparaily because of the code on v2002 is not sync to MERGE_TMEP for USB/SDIO. */
	/* De not remove this part on MERGE_TEMP. by tynli. */
#endif

	/* Disable Hw protection for a time which revserd for Hw sending beacon. */
	/* Fix download reserved page packet fail that access collision with the protection time. */
	/* 2010.05.11. Added by tynli. */
	val8 = rtw_read8(Adapter, REG_BCN_CTRL);
	val8 &= ~EN_BCN_FUNCTION;
	val8 |= DIS_TSF_UDT;
	rtw_write8(Adapter, REG_BCN_CTRL, val8);

#if 0/* #ifdef CONFIG_PCI_HCI */
	tmpReg422 = PlatformEFIORead1Byte(Adapter, REG_FWHW_TXQ_CTRL + 2);
	if (tmpReg422 & BIT(6))
		bRecover = TRUE;
	PlatformEFIOWrite1Byte(Adapter, REG_FWHW_TXQ_CTRL + 2,  tmpReg422 & (~BIT(6)));
#else
	/* Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame. */
	RegFwHwTxQCtrl = PlatformEFIORead1Byte(Adapter, REG_FWHW_TXQ_CTRL + 2);

	RegFwHwTxQCtrl &= (~BIT(6));
	PlatformEFIOWrite1Byte(Adapter, REG_FWHW_TXQ_CTRL + 2, RegFwHwTxQCtrl);
#endif

	/* --------------------------------------------------------- */
	/* 2. Adjust LLT table to an even boundary. */
	/* --------------------------------------------------------- */
#if 0/* #ifdef CONFIG_SDIO_HCI */
	txpktbuf_bndy = 10; /* rsvd page start address should be an even value.														 */
	rtStatus =	InitLLTTable8723DS(Adapter, txpktbuf_bndy);
	if (RT_STATUS_SUCCESS != rtStatus) {
		RTW_INFO("_CheckWLANFwPatchBTFwReady_8723D(): Failed to init LLT!\n");
		return RT_STATUS_FAILURE;
	}

	/* Init Tx boundary. */
	PlatformEFIOWrite1Byte(Adapter, REG_DWBCN0_CTRL_8723D + 1, (u8)txpktbuf_bndy);
#endif


	/* --------------------------------------------------------- */
	/* 3. Write Fw to Tx packet buffer by reseverd page. */
	/* --------------------------------------------------------- */
	do {
		/* download rsvd page. */
		/* Clear beacon valid check bit. */
		BcnValidReg = PlatformEFIORead1Byte(Adapter, REG_TDECTRL + 2);
		PlatformEFIOWrite1Byte(Adapter, REG_TDECTRL + 2, BcnValidReg & (~BIT(0)));

		/* BT patch is big, we should set 0x209 < 0x40 suggested from Gimmy */

		PlatformEFIOWrite1Byte(Adapter, REG_TDECTRL + 1, (0x90 - 0x20 * (times - 1)));
		RTW_INFO("0x209:0x%x\n", PlatformEFIORead1Byte(Adapter, REG_TDECTRL + 1));

#if 0
		/* Acquice TX spin lock before GetFwBuf and send the packet to prevent system deadlock. */
		/* Advertised by Roger. Added by tynli. 2010.02.22. */
		PlatformAcquireSpinLock(Adapter, RT_TX_SPINLOCK);
		if (MgntGetFWBuffer(Adapter, &pTcb, &pBuf)) {
			PlatformMoveMemory(pBuf->Buffer.VirtualAddress, ReservedPagePacket, TotalPktLen);
			CmdSendPacket(Adapter, pTcb, pBuf, TotalPktLen, DESC_PACKET_TYPE_NORMAL, FALSE);
		} else
			dbgdump("SetFwRsvdPagePkt(): MgntGetFWBuffer FAIL!!!!!!!!.\n");
		PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);
#else
		/*---------------------------------------------------------
		tx reserved_page_packet
		----------------------------------------------------------*/
		pmgntframe = rtw_alloc_cmdxmitframe(pxmitpriv);
		if (pmgntframe == NULL) {
			rtStatus = _FAIL;
			goto exit;
		}
		/* update attribute */
		pattrib = &pmgntframe->attrib;
		update_mgntframe_attrib(Adapter, pattrib);

		pattrib->qsel = QSLT_BEACON;
		pattrib->pktlen = pattrib->last_txcmdsz = FwBufLen;

		/* _rtw_memset(pmgntframe->buf_addr, 0, TotalPktLen+txdesc_size); */
		/* pmgntframe->buf_addr = ReservedPagePacket ; */

		_rtw_memcpy((u8 *)(pmgntframe->buf_addr + txdesc_offset), ReservedPagePacket, FwBufLen);
		RTW_INFO("[%d]===>TotalPktLen + TXDESC_OFFSET TotalPacketLen:%d\n", DLBcnCount, (FwBufLen + txdesc_offset));

#ifdef CONFIG_PCI_HCI
		dump_mgntframe(Adapter, pmgntframe);
#else
		dump_mgntframe_and_wait(Adapter, pmgntframe, 100);
#endif

#endif
#if 1
		/* check rsvd page download OK. */
		BcnValidReg = PlatformEFIORead1Byte(Adapter, REG_TDECTRL + 2);
		while (!(BcnValidReg & BIT(0)) && count < 200) {
			count++;
			/* PlatformSleepUs(10); */
			rtw_msleep_os(1);
			BcnValidReg = PlatformEFIORead1Byte(Adapter, REG_TDECTRL + 2);
		}
		DLBcnCount++;
		/* RTW_INFO("##0x208:%08x,0x210=%08x\n",PlatformEFIORead4Byte(Adapter, REG_TDECTRL),PlatformEFIORead4Byte(Adapter, 0x210)); */

		PlatformEFIOWrite1Byte(Adapter, REG_TDECTRL + 2, BcnValidReg);

	} while ((!(BcnValidReg & BIT(0))) && DLBcnCount < 5);


#endif
	if (DLBcnCount >= 5) {
		RTW_INFO(" check rsvd page download OK DLBcnCount =%d\n", DLBcnCount);
		rtStatus = _FAIL;
		goto exit;
	}

	if (!(BcnValidReg & BIT(0))) {
		RTW_INFO("_WriteFWtoTxPktBuf(): 1 Download RSVD page failed!\n");
		rtStatus = _FAIL;
		goto exit;
	}

	/* --------------------------------------------------------- */
	/* 4. Set Tx boundary to the initial value */
	/* --------------------------------------------------------- */


	/* --------------------------------------------------------- */
	/* 5. Reset beacon setting to the initial value. */
	/*	 After _CheckWLANFwPatchBTFwReady(). */
	/* --------------------------------------------------------- */

exit:

	if (pGenBufReservedPagePacket) {
		RTW_INFO("_WriteBTFWtoTxPktBuf8723D => rtw_mfree pGenBufReservedPagePacket!\n");
		rtw_mfree((u8 *)pGenBufReservedPagePacket, TotalPktLen);
	}
	return rtStatus;
}



/*
 * Description: Determine the contents of H2C BT_FW_PATCH Command sent to FW.
 * 2011.10.20 by tynli
 *   */
void
SetFwBTFwPatchCmd(
		PADAPTER	Adapter,
		u16		FwSize
)
{
	u8 u1BTFwPatchParm[H2C_BT_FW_PATCH_LEN] = {0};
	u8 addr0 = 0;
	u8 addr1 = 0xa0;
	u8 addr2 = 0x10;
	u8 addr3 = 0x80;


	SET_8723D_H2CCMD_BT_FW_PATCH_SIZE(u1BTFwPatchParm, FwSize);
	SET_8723D_H2CCMD_BT_FW_PATCH_ADDR0(u1BTFwPatchParm, addr0);
	SET_8723D_H2CCMD_BT_FW_PATCH_ADDR1(u1BTFwPatchParm, addr1);
	SET_8723D_H2CCMD_BT_FW_PATCH_ADDR2(u1BTFwPatchParm, addr2);
	SET_8723D_H2CCMD_BT_FW_PATCH_ADDR3(u1BTFwPatchParm, addr3);

	FillH2CCmd8723D(Adapter, H2C_8723D_BT_FW_PATCH, H2C_BT_FW_PATCH_LEN, u1BTFwPatchParm);

}

void
SetFwBTPwrCmd(
		PADAPTER	Adapter,
		u8	PwrIdx
)
{
	u8		u1BTPwrIdxParm[H2C_FORCE_BT_TXPWR_LEN] = {0};

	SET_8723D_H2CCMD_BT_PWR_IDX(u1BTPwrIdxParm, PwrIdx);


	FillH2CCmd8723D(Adapter, H2C_8723D_FORCE_BT_TXPWR, H2C_FORCE_BT_TXPWR_LEN, u1BTPwrIdxParm);
}

/*
 * Description: WLAN Fw will write BT Fw to BT XRAM and signal driver.
 *
 * 2011.10.20. by tynli.
 *   */
int
_CheckWLANFwPatchBTFwReady(
	PADAPTER Adapter,
	BOOLEAN bRecover
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32	count = 0;
	u8	u1bTmp;
	int ret = _FAIL;

	/* --------------------------------------------------------- */
	/* Check if BT FW patch procedure is ready. */
	/* --------------------------------------------------------- */
	do {
		u1bTmp = PlatformEFIORead1Byte(Adapter, REG_HMEBOX_DBG_0_8723D);
		if ((u1bTmp & BIT(6)) || (u1bTmp & BIT(7))) {
			ret = _SUCCESS;
			break;
		}

		count++;
		rtw_msleep_os(50); /* 50ms */
	} while (!((u1bTmp & BIT(6)) || (u1bTmp & BIT(7))) && count < 50);




	/* --------------------------------------------------------- */
	/* Reset beacon setting to the initial value. */
	/* --------------------------------------------------------- */
#if 0/* #ifdef CONFIG_PCI_HCI */
	if (LLT_table_init(Adapter, FALSE, 0) == RT_STATUS_FAILURE) {
		dbgdump("Init self define for BT Fw patch LLT table fail.\n");
		/* return RT_STATUS_FAILURE; */
	}
#endif
	u1bTmp = rtw_read8(Adapter, REG_BCN_CTRL);
	u1bTmp |= EN_BCN_FUNCTION;
	u1bTmp &= ~DIS_TSF_UDT;
	rtw_write8(Adapter, REG_BCN_CTRL, u1bTmp);

	/* To make sure that if there exists an adapter which would like to send beacon. */
	/* If exists, the origianl value of 0x422[6] will be 1, we should check this to */
	/* prevent from setting 0x422[6] to 0 after download reserved page, or it will cause */
	/* the beacon cannot be sent by HW. */
	/* 2010.06.23. Added by tynli. */
	if (bRecover) {
		u1bTmp = PlatformEFIORead1Byte(Adapter, REG_FWHW_TXQ_CTRL + 2);
		PlatformEFIOWrite1Byte(Adapter, REG_FWHW_TXQ_CTRL + 2, (u1bTmp | BIT(6)));
	}

	/* Clear CR[8] or beacon packet will not be send to TxBuf anymore. */
	u1bTmp = PlatformEFIORead1Byte(Adapter, REG_CR + 1);
	PlatformEFIOWrite1Byte(Adapter, REG_CR + 1, (u1bTmp & (~BIT(0))));

	return ret;
}

int ReservedPage_Compare(PADAPTER Adapter, PRT_MP_FIRMWARE pFirmware, u32 BTPatchSize)
{
	u8 temp, ret, lastBTsz;
	u32 u1bTmp = 0, address_start = 0, count = 0, i = 0;
	u8	*myBTFwBuffer = NULL;

	myBTFwBuffer = rtw_zmalloc(BTPatchSize);
	if (myBTFwBuffer == NULL) {
		RTW_INFO("%s can't be executed due to the failed malloc.\n", __FUNCTION__);
		Adapter->mppriv.bTxBufCkFail = _TRUE;
		return _FALSE;
	}

	temp = rtw_read8(Adapter, 0x209);

	address_start = (temp * 128) / 8;

	rtw_write32(Adapter, 0x140, 0x00000000);
	rtw_write32(Adapter, 0x144, 0x00000000);
	rtw_write32(Adapter, 0x148, 0x00000000);

	rtw_write8(Adapter, 0x106, 0x69);

	for (i = 0; i < (BTPatchSize / 8); i++) {
		rtw_write32(Adapter, 0x140, address_start + 5 + i);

		/* polling until reg 0x140[23]=1; */
		do {
			u1bTmp = rtw_read32(Adapter, 0x140);
			if (u1bTmp & BIT(23)) {
				ret = _SUCCESS;
				break;
			}
			count++;
			RTW_INFO("0x140=%x, wait for 10 ms (%d) times.\n", u1bTmp, count);
			rtw_msleep_os(10); /* 10ms */
		} while (!(u1bTmp & BIT(23)) && count < 50);

		myBTFwBuffer[i * 8 + 0] = rtw_read8(Adapter, 0x144);
		myBTFwBuffer[i * 8 + 1] = rtw_read8(Adapter, 0x145);
		myBTFwBuffer[i * 8 + 2] = rtw_read8(Adapter, 0x146);
		myBTFwBuffer[i * 8 + 3] = rtw_read8(Adapter, 0x147);
		myBTFwBuffer[i * 8 + 4] = rtw_read8(Adapter, 0x148);
		myBTFwBuffer[i * 8 + 5] = rtw_read8(Adapter, 0x149);
		myBTFwBuffer[i * 8 + 6] = rtw_read8(Adapter, 0x14a);
		myBTFwBuffer[i * 8 + 7] = rtw_read8(Adapter, 0x14b);
	}

	rtw_write32(Adapter, 0x140, address_start + 5 + BTPatchSize / 8);

	lastBTsz = BTPatchSize % 8;

	/* polling until reg 0x140[23]=1; */
	u1bTmp = 0;
	count = 0;
	do {
		u1bTmp = rtw_read32(Adapter, 0x140);
		if (u1bTmp & BIT(23)) {
			ret = _SUCCESS;
			break;
		}
		count++;
		RTW_INFO("0x140=%x, wait for 10 ms (%d) times.\n", u1bTmp, count);
		rtw_msleep_os(10); /* 10ms */
	} while (!(u1bTmp & BIT(23)) && count < 50);

	for (i = 0; i < lastBTsz; i++)
		myBTFwBuffer[(BTPatchSize / 8) * 8 + i] = rtw_read8(Adapter, (0x144 + i));


	for (i = 0; i < BTPatchSize; i++) {
		if (myBTFwBuffer[i] != pFirmware->szFwBuffer[i]) {
			RTW_INFO(" In direct myBTFwBuffer[%d]=%x , pFirmware->szFwBuffer=%x\n", i, myBTFwBuffer[i], pFirmware->szFwBuffer[i]);
			Adapter->mppriv.bTxBufCkFail = _TRUE;
			break;
		}
	}

	if (myBTFwBuffer != NULL)
		rtw_mfree(myBTFwBuffer, BTPatchSize);

	return _TRUE;
}

/* As the size of bt firmware is more than 16k which is too big for some platforms, we divide it
 * into four parts to transfer. The last parameter of _WriteBTFWtoTxPktBuf8723D is used to indicate
 * the location of every part. We call the first 4096 byte of bt firmware as part 1, the second 4096
 * part as part 2, the third 4096 part as part 3, the remain as part 4. First we transform the part
 * 4 and set the register 0x209 to 0x90, then the 32 bytes description are added to the head of part
 * 4, and those bytes are putted at the location 0x90. Second we transform the part 3 and set the
 * register 0x209 to 0x70. The 32 bytes description and part 3(4196 bytes) are putted at the location
 * 0x70. It can contain 4196 bytes between 0x70 and 0x90. So the last 32 bytes os part 3 will cover the
 * 32 bytes description of part4. Using this method, we can put the whole bt firmware to 0x30 and only
 * has 32 bytes descrption at the head of part 1.
*/
s32 FirmwareDownloadBT(PADAPTER padapter, PRT_MP_FIRMWARE pFirmware)
{
	s32 rtStatus;
	u8 *pBTFirmwareBuf;
	u32 BTFirmwareLen;
	u8 download_time;
	s8 i;
	BOOLEAN bRecover = _FALSE;
	u8 RegFwHwTxQCtrl;

	rtStatus = _SUCCESS;
	pBTFirmwareBuf = NULL;
	BTFirmwareLen = 0;

#if 0
	/* */
	/* Patch BT Fw. Download BT RAM code to Tx packet buffer. */
	/* */
	if (GET_HAL_DATA(padapter)->bBTFWReady) {
		RTW_INFO("%s: BT Firmware is ready!!\n", __FUNCTION__);
		return _FAIL;
	}

#ifdef CONFIG_FILE_FWIMG
	if (rtw_is_file_readable(rtw_fw_mp_bt_file_path) == _TRUE) {
		RTW_INFO("%s: acquire MP BT FW from file:%s\n", __FUNCTION__, rtw_fw_mp_bt_file_path);

		rtStatus = rtw_retrieve_from_file(rtw_fw_mp_bt_file_path, FwBuffer, FW_8723D_SIZE);
		BTFirmwareLen = rtStatus >= 0 ? rtStatus : 0;
		pBTFirmwareBuf = FwBuffer;
	} else
#endif /* CONFIG_FILE_FWIMG */
	{
#ifdef CONFIG_EMBEDDED_FWIMG
		RTW_INFO("%s: Download MP BT FW from header\n", __FUNCTION__);

		pBTFirmwareBuf = (u8 *)Rtl8723DFwBTImgArray;
		BTFirmwareLen = Rtl8723DFwBTImgArrayLength;
		pFirmware->szFwBuffer = pBTFirmwareBuf;
		pFirmware->ulFwLength = BTFirmwareLen;
#endif /* CONFIG_EMBEDDED_FWIMG */
	}

	RTW_INFO("%s: MP BT Firmware size=%d\n", __FUNCTION__, BTFirmwareLen);

	/* for h2c cam here should be set to  true */
	GET_HAL_DATA(padapter)->bFWReady = _TRUE;

	download_time = (BTFirmwareLen + 4095) / 4096;
	RTW_INFO("%s: download_time is %d\n", __FUNCTION__, download_time);
	RegFwHwTxQCtrl = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL + 2);

	if (RegFwHwTxQCtrl & BIT(6))
		bRecover = _TRUE;

	/* Download BT patch Fw. */
	for (i = (download_time - 1); i >= 0; i--) {
		if (i == (download_time - 1)) {
			rtStatus = _WriteBTFWtoTxPktBuf8723D(padapter, pBTFirmwareBuf + (4096 * i), (BTFirmwareLen - (4096 * i)), 1);
			RTW_INFO("%s: start %d, len %d, time 1\n", __FUNCTION__, 4096 * i, BTFirmwareLen - (4096 * i));
		} else {
			rtStatus = _WriteBTFWtoTxPktBuf8723D(padapter, pBTFirmwareBuf + (4096 * i), 4096, (download_time - i));
			RTW_INFO("%s: start %d, len 4096, time %d\n", __FUNCTION__, 4096 * i, download_time - i);
		}

		if (rtStatus != _SUCCESS) {
			RTW_INFO("%s: BT Firmware download to Tx packet buffer fail!\n", __FUNCTION__);
			GET_HAL_DATA(padapter)->bBTFWReady = _FALSE;
			return rtStatus;
		}
	}

	ReservedPage_Compare(padapter, pFirmware, BTFirmwareLen);

	GET_HAL_DATA(padapter)->bBTFWReady = _TRUE;
	SetFwBTFwPatchCmd(padapter, (u16)BTFirmwareLen);
	rtStatus = _CheckWLANFwPatchBTFwReady(padapter, bRecover);

	RTW_INFO("<===%s: return %s!\n", __FUNCTION__, rtStatus == _SUCCESS ? "SUCCESS" : "FAIL");
#endif

	return rtStatus;
}
#endif /* CONFIG_MP_INCLUDED */

#if defined(CONFIG_SDIO_HCI) && defined(CONFIG_DLFW_TXPKT)
u8 send_fw_packet(PADAPTER padapter, u8 *pRam_code, u32 length)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct xmit_buf xmit_buf_tmp;
	struct submit_ctx sctx_tmp;
	u8 *pTx_data_buffer = NULL;
	u8 *pTmp_buffer = NULL;
	u8 bRet = 0, value8 = 0, res = _FAIL;
	u32 modify_ram_size = 0;
	u32 tmp_size = 0, tmp_value = 0;
	u32 i = 0, counter = 0;
	u32 dwDataLength = 0, writeLength = 0;

	/* Due to SDIO can not send 32K packet */
	if (FW_DOWNLOAD_SIZE_8723D == length)
		length--;

	modify_ram_size = length << 2;

	pTx_data_buffer = rtw_zmalloc(modify_ram_size);

	if (NULL == pTx_data_buffer) {
		RTW_INFO("Allocate buffer fail!!\n");
		return _FALSE;
	}

	_rtw_memset(pTx_data_buffer, 0, modify_ram_size);

	/* Transfer to new format */
	tmp_size = length >> 1;
	for (i = 0; i <= tmp_size; i++) {
		*(pTx_data_buffer + i * 8) = *(pRam_code + i * 2);
		*(pTx_data_buffer + i * 8 + 1) = *(pRam_code + i * 2 + 1);
	}

	/* Gen TX_DESC */
	_rtw_memset(pTx_data_buffer, 0, TXDESC_SIZE);
	pTmp_buffer = pTx_data_buffer;
	SET_TX_DESC_QUEUE_SEL_8723D(pTmp_buffer, QSLT_BEACON);
	SET_TX_DESC_PKT_SIZE_8723D(pTmp_buffer, modify_ram_size - TXDESC_SIZE);
	SET_TX_DESC_OFFSET_8723D(pTmp_buffer, TXDESC_SIZE);

	/* Send packet */
	xmit_buf_tmp.pdata = pTx_data_buffer;
	xmit_buf_tmp.len = modify_ram_size;
	rtw_sctx_init(&sctx_tmp, 10);
	xmit_buf_tmp.sctx = &sctx_tmp;

	res = rtw_write_port(padapter,
			     pdvobjpriv->Queue2Pipe[BCN_QUEUE_INX],
			     xmit_buf_tmp.len,
			     (u8 *)&xmit_buf_tmp);
	if (res == _FAIL) {
		RTW_INFO("rtw_write_port fail\n");
		return _FAIL;
	}

	/* check if DMA is OK */
	counter = 100;
	do {
		if (0 == counter) {
			RTW_INFO("DMA time out!!\n");
			return _FALSE;
		}
		value8 = rtw_read8(padapter, REG_DWBCN0_CTRL_8723D + 2);
		counter--;
	} while (0 == (value8 & BIT(0)));

	rtw_write8(padapter, REG_DWBCN0_CTRL_8723D + 2, value8);

	/* Modify ram code by IO method */
	tmp_value = rtw_read8(padapter, REG_MCUFWDL + 1);
	/* Disable DMA */
	rtw_write8(padapter, REG_MCUFWDL + 1, (u8)tmp_value & ~(BIT(5)));
	tmp_value = (tmp_value >> 6) << 1;
	/* Set page start address */
	rtw_write8(padapter, REG_MCUFWDL + 2,
		   (rtw_read8(padapter, REG_MCUFWDL + 2) & 0xF8) | tmp_value);
	tmp_size = TXDESC_SIZE >> 2; /* 10bytes */

	_BlockWrite(padapter, pRam_code, tmp_size);

	if (pTmp_buffer != NULL)
		rtw_mfree((u8 *)pTmp_buffer, modify_ram_size);

	return _TRUE;
}
#endif /* CONFIG_SDIO_HCI */

/*
 *	Description:
 *		Download 8192C firmware code.
 *
 *   */
s32 rtl8723d_FirmwareDownload(PADAPTER padapter, BOOLEAN  bUsedWoWLANFw)
{
	s32	rtStatus = _SUCCESS;
	u8 write_fw = 0;
	systime fwdl_start_time;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			*FwImage;
	u32			FwImageLen;
	u8			*pFwImageFileName;
#ifdef CONFIG_WOWLAN
	u8			*FwImageWoWLAN;
	u32			FwImageWoWLANLen;
#endif
	u8			*pucMappedFile = NULL;
	PRT_FIRMWARE_8723D	pFirmware = NULL;
	PRT_8723D_FIRMWARE_HDR		pFwHdr = NULL;
	u8			*pFirmwareBuf;
	u32			FirmwareLen;
#ifdef CONFIG_FILE_FWIMG
	u8 *fwfilepath;
#endif /* CONFIG_FILE_FWIMG */
	u8			value8;
	u16			value16;
	u32			value32;
	u8			dma_iram_sel;
	u16		new_chk_sum = 0;
	u32		send_pkt_size, pkt_size_tmp;
	u32		mem_offset;
	u32			counter;
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);


	pFirmware = (PRT_FIRMWARE_8723D)rtw_zmalloc(sizeof(RT_FIRMWARE_8723D));

	if (!pFirmware) {
		rtStatus = _FAIL;
		goto exit;
	}

	{
		u8 tmp_ps = 0, tmp_rf = 0;

		tmp_ps = rtw_read8(padapter, 0xa3);
		tmp_ps &= 0xf8;
		tmp_ps |= 0x02;
		/* 1. write 0xA3[:2:0] = 3b'010 */
		rtw_write8(padapter, 0xa3, tmp_ps);
		/* 2. read power_state = 0xA0[1:0] */
		tmp_ps = rtw_read8(padapter, 0xa0);
		tmp_ps &= 0x03;
		if (tmp_ps != 0x01) {
			RTW_INFO(FUNC_ADPT_FMT" tmp_ps=%x\n",
				 FUNC_ADPT_ARG(padapter), tmp_ps);
			pdbgpriv->dbg_downloadfw_pwr_state_cnt++;
		}
	}

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_PreLoadFirmware(padapter);
#endif /* CONFIG_BT_COEXIST */

#ifdef CONFIG_FILE_FWIMG
#ifdef CONFIG_WOWLAN
	if (bUsedWoWLANFw)
		fwfilepath = rtw_fw_wow_file_path;
	else
#endif /* CONFIG_WOWLAN */
	{
		fwfilepath = rtw_fw_file_path;
	}
#endif /* CONFIG_FILE_FWIMG */

#ifdef CONFIG_FILE_FWIMG
	if (rtw_is_file_readable(fwfilepath) == _TRUE) {
		RTW_INFO("%s acquire FW from file:%s\n", __FUNCTION__, fwfilepath);
		pFirmware->eFWSource = FW_SOURCE_IMG_FILE;
	} else
#endif /* CONFIG_FILE_FWIMG */
	{
#ifdef CONFIG_EMBEDDED_FWIMG
		pFirmware->eFWSource = FW_SOURCE_HEADER_FILE;
#else /* !CONFIG_EMBEDDED_FWIMG */
		pFirmware->eFWSource = FW_SOURCE_IMG_FILE; /* We should decided by Reg. */
#endif /* !CONFIG_EMBEDDED_FWIMG */
	}

	switch (pFirmware->eFWSource) {
	case FW_SOURCE_IMG_FILE:
#ifdef CONFIG_FILE_FWIMG
		rtStatus = rtw_retrieve_from_file(fwfilepath, FwBuffer, FW_8723D_SIZE);
		pFirmware->ulFwLength = rtStatus >= 0 ? rtStatus : 0;
		pFirmware->szFwBuffer = FwBuffer;
#endif /* CONFIG_FILE_FWIMG */
		break;

	case FW_SOURCE_HEADER_FILE:
		if (bUsedWoWLANFw) {
	#ifdef CONFIG_WOWLAN
			if (pwrpriv->wowlan_mode) {
				pFirmware->szFwBuffer = array_mp_8723d_fw_wowlan;
				pFirmware->ulFwLength = array_length_mp_8723d_fw_wowlan;
				RTW_INFO(" ===> %s fw: %s, size: %d\n",
					 __FUNCTION__, "WoWLAN", pFirmware->ulFwLength);
			}
	#endif /* CONFIG_WOWLAN */

	#ifdef CONFIG_AP_WOWLAN
			if (pwrpriv->wowlan_ap_mode) {
				pFirmware->szFwBuffer = array_mp_8723d_fw_ap;
				pFirmware->ulFwLength = array_length_mp_8723d_fw_ap;
				RTW_INFO(" ===> %s fw: %s, size: %d\n",
					 __FUNCTION__, "AP_WoWLAN", pFirmware->ulFwLength);
			}
	#endif /* CONFIG_AP_WOWLAN */
		} else {
			pFirmware->szFwBuffer = array_mp_8723d_fw_nic;
			pFirmware->ulFwLength = array_length_mp_8723d_fw_nic;
			RTW_INFO("%s fw: %s, size: %d\n", __FUNCTION__, "FW_NIC", pFirmware->ulFwLength);
		}
		break;
	}

	if ((pFirmware->ulFwLength - 32) > FW_8723D_SIZE) {
		rtStatus = _FAIL;
		RTW_ERR("Firmware size:%u exceed %u\n",
			pFirmware->ulFwLength, FW_8723D_SIZE);
		goto exit;
	}

	pFirmwareBuf = pFirmware->szFwBuffer;
	FirmwareLen = pFirmware->ulFwLength;

	/* To Check Fw header. Added by tynli. 2009.12.04. */
	pFwHdr = (PRT_8723D_FIRMWARE_HDR)pFirmwareBuf;

	pHalData->firmware_version =  le16_to_cpu(pFwHdr->Version);
	pHalData->firmware_sub_version = le16_to_cpu(pFwHdr->Subversion);
	pHalData->FirmwareSignature = le16_to_cpu(pFwHdr->Signature);

	RTW_INFO("%s: fw_ver=%x fw_subver=%04x sig=0x%x, Month=%02x, Date=%02x, Hour=%02x, Minute=%02x\n",
		 __func__, pHalData->firmware_version,
		 pHalData->firmware_sub_version, pHalData->FirmwareSignature
		 , pFwHdr->Month, pFwHdr->Date, pFwHdr->Hour, pFwHdr->Minute);

	if (IS_FW_HEADER_EXIST_8723D(pFwHdr)) {
		RTW_INFO("%s(): Shift for fw header!\n", __FUNCTION__);
		/* Shift 32 bytes for FW header */
		pFirmwareBuf = pFirmwareBuf + 32;
		FirmwareLen = FirmwareLen - 32;
	}

	fwdl_start_time = rtw_get_current_time();

	/* To check if FW already exists before download FW */
	if (rtw_read8(padapter, REG_MCUFWDL) & RAM_DL_SEL) {
		RTW_INFO("%s: FW exists before download FW\n", __func__);
		rtw_write8(padapter, REG_MCUFWDL, 0x00);
		_8051Reset8723(padapter);
	}

#ifndef CONFIG_DLFW_TXPKT
	RTW_INFO("%s by IO write!\n", __func__);

	_FWDownloadEnable(padapter, _TRUE);

	while (!RTW_CANNOT_IO(padapter) &&
	       (write_fw++ < DL_FW_MAX ||
		rtw_get_passing_time_ms(fwdl_start_time) < 500)) {
		/* reset FWDL chksum */
		rtw_write8(padapter, REG_MCUFWDL,
			   rtw_read8(padapter, REG_MCUFWDL) | FWDL_ChkSum_rpt);

		rtStatus = _WriteFW(padapter, pFirmwareBuf, FirmwareLen);
		if (rtStatus != _SUCCESS)
			continue;

		rtStatus = polling_fwdl_chksum(padapter, 2, 10);
		if (rtStatus == _SUCCESS) {
			RTW_INFO("%s: download FW count:%d\n", __func__,
				 write_fw);
			break;
		} else {
			rtw_mdelay_os(10);
		}
	}
#else
	RTW_INFO("%s by Tx pkt write!\n", __func__);

	if ((rtw_read8(padapter, REG_MCUFWDL) & MCUFWDL_RDY) == 0) {
		/*
		 * SDIO DMA condition:
		 * all queue must be 256 (0x100 = 0x20 + 0xE0)
		*/

		value32 = 0x802000E0;
		rtw_write32(padapter, REG_RQPN, value32);

		/* Set beacon boundary to TXFIFO header */
		rtw_write8(padapter, REG_BCNQ_BDNY, 0);
		rtw_write16(padapter, REG_DWBCN0_CTRL_8723D + 1, BIT(8));

		/* SDIO need read this register before send packet */
		rtw_read32(padapter, 0x10250020);

		_FWDownloadEnable(padapter, _TRUE);

		/* Get original check sum */
		new_chk_sum = *(pFirmwareBuf + FirmwareLen - 2) |
			      ((u16)*(pFirmwareBuf + FirmwareLen - 1) << 8);

		/* Send ram code flow */
		dma_iram_sel = 0;
		mem_offset = 0;
		pkt_size_tmp = FirmwareLen;
		while (0 != pkt_size_tmp) {
			if (pkt_size_tmp >= FW_DOWNLOAD_SIZE_8723D) {
				send_pkt_size = FW_DOWNLOAD_SIZE_8723D;
				/* Modify check sum value */
				new_chk_sum = (u16)(new_chk_sum ^ (((send_pkt_size - 1) << 2) - TXDESC_SIZE));
			} else {
				send_pkt_size = pkt_size_tmp;
				new_chk_sum = (u16)(new_chk_sum ^ ((send_pkt_size << 2) - TXDESC_SIZE));

			}

			if (send_pkt_size == pkt_size_tmp) {
				/* last partition packet, write new check sum to ram code file */
				*(pFirmwareBuf + FirmwareLen - 2) = new_chk_sum & 0xFF;
				*(pFirmwareBuf + FirmwareLen - 1) = (new_chk_sum & 0xFF00) >> 8;
			}

			/* IRAM select */
			rtw_write8(padapter, REG_MCUFWDL + 1,
				(rtw_read8(padapter, REG_MCUFWDL + 1) & 0x3F) | (dma_iram_sel << 6));
			/* Enable DMA */
			rtw_write8(padapter, REG_MCUFWDL + 1,
				rtw_read8(padapter, REG_MCUFWDL + 1) | BIT(5));

			if (_FALSE == send_fw_packet(padapter, pFirmwareBuf + mem_offset, send_pkt_size)) {
				RTW_INFO("%s: Send FW fail !\n", __FUNCTION__);
				rtStatus = _FAIL;
				goto DLFW_FAIL;
			}

			dma_iram_sel++;
			mem_offset += send_pkt_size;
			pkt_size_tmp -= send_pkt_size;
		}
	} else {
		RTW_INFO("%s: Download FW fail since MCUFWDL_RDY is not set!\n", __FUNCTION__);
		rtStatus = _FAIL;
		goto DLFW_FAIL;
	}
#endif

	_FWDownloadEnable(padapter, _FALSE);

	if (rtStatus == _SUCCESS)
		rtStatus = _FWFreeToGo(padapter, 10, 200);
	else
		goto DLFW_FAIL;

	if (_SUCCESS != rtStatus)
		goto DLFW_FAIL;

DLFW_FAIL:
	if (rtStatus == _FAIL) {
		/* Disable FWDL_EN */
		value8 = rtw_read8(padapter, REG_MCUFWDL);
		value8 = (value8 & ~(BIT(0)) & ~(BIT(1)));
		rtw_write8(padapter, REG_MCUFWDL, value8);
	}

	RTW_INFO("%s %s. write_fw:%u, %dms\n"
		 , __FUNCTION__, (rtStatus == _SUCCESS) ? "success" : "fail"
		 , write_fw
		 , rtw_get_passing_time_ms(fwdl_start_time)
		);

exit:
	if (pFirmware)
		rtw_mfree((u8 *)pFirmware, sizeof(RT_FIRMWARE_8723D));

	rtl8723d_InitializeFirmwareVars(padapter);

	RTW_INFO(" <=== %s()\n", __FUNCTION__);

	return rtStatus;
}

void rtl8723d_InitializeFirmwareVars(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);

	/* Init Fw LPS related. */
	adapter_to_pwrctl(padapter)->bFwCurrentInPSMode = _FALSE;

	/* Init H2C cmd. */
	rtw_write8(padapter, REG_HMETFR, 0x0f);

	/* Init H2C counter. by tynli. 2009.12.09. */
	pHalData->LastHMEBoxNum = 0;
	/*	pHalData->H2CQueueHead = 0;
	 *	pHalData->H2CQueueTail = 0;
	 *	pHalData->H2CStopInsertQueue = _FALSE; */
}

/* ***********************************************************
 *				Efuse related code
 * *********************************************************** */
static u8
hal_EfuseSwitchToBank(
	PADAPTER	padapter,
	u8			bank,
	u8			bPseudoTest)
{
	u8 bRet = _FALSE;
	u32 value32 = 0;
#ifdef HAL_EFUSE_MEMORY
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	PEFUSE_HAL pEfuseHal = &pHalData->EfuseHal;
#endif


	RTW_INFO("%s: Efuse switch bank to %d\n", __FUNCTION__, bank);
	if (bPseudoTest) {
#ifdef HAL_EFUSE_MEMORY
		pEfuseHal->fakeEfuseBank = bank;
#else
		fakeEfuseBank = bank;
#endif
		bRet = _TRUE;
	} else {
		value32 = rtw_read32(padapter, EFUSE_TEST);
		bRet = _TRUE;
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
			bRet = _FALSE;
			break;
		}
		rtw_write32(padapter, EFUSE_TEST, value32);
	}

	return bRet;
}

static void
Hal_GetEfuseDefinition(
	PADAPTER	padapter,
	u8			efuseType,
	u8			type,
	void		*pOut,
	u8			bPseudoTest)
{
	switch (type) {
	case TYPE_EFUSE_MAX_SECTION: {
		u8 *pMax_section;

		pMax_section = (u8 *)pOut;

		if (efuseType == EFUSE_WIFI)
			*pMax_section = EFUSE_MAX_SECTION_8723D;
		else
			*pMax_section = EFUSE_BT_MAX_SECTION;
	}
	break;

	case TYPE_EFUSE_REAL_CONTENT_LEN: {
		u16 *pu2Tmp;

		pu2Tmp = (u16 *)pOut;

		if (efuseType == EFUSE_WIFI)
			*pu2Tmp = EFUSE_REAL_CONTENT_LEN_8723D;
		else
			*pu2Tmp = EFUSE_BT_REAL_CONTENT_LEN;
	}
	break;

	case TYPE_AVAILABLE_EFUSE_BYTES_BANK: {
		u16	*pu2Tmp;

		pu2Tmp = (u16 *)pOut;

		if (efuseType == EFUSE_WIFI)
			*pu2Tmp = (EFUSE_REAL_CONTENT_LEN_8723D - EFUSE_OOB_PROTECT_BYTES);
		else
			*pu2Tmp = (EFUSE_BT_REAL_BANK_CONTENT_LEN - EFUSE_PROTECT_BYTES_BANK);
	}
	break;

	case TYPE_AVAILABLE_EFUSE_BYTES_TOTAL: {
		u16 *pu2Tmp;

		pu2Tmp = (u16 *)pOut;

		if (efuseType == EFUSE_WIFI)
			*pu2Tmp = (EFUSE_REAL_CONTENT_LEN_8723D - EFUSE_OOB_PROTECT_BYTES);
		else
			*pu2Tmp = (EFUSE_BT_REAL_CONTENT_LEN - (EFUSE_PROTECT_BYTES_BANK * 3));
	}
	break;

	case TYPE_EFUSE_MAP_LEN: {
		u16 *pu2Tmp;

		pu2Tmp = (u16 *)pOut;

		if (efuseType == EFUSE_WIFI)
			*pu2Tmp = EFUSE_MAP_LEN_8723D;
		else
			*pu2Tmp = EFUSE_BT_MAP_LEN;
	}
	break;

	case TYPE_EFUSE_PROTECT_BYTES_BANK: {
		u8 *pu1Tmp;

		pu1Tmp = (u8 *)pOut;

		if (efuseType == EFUSE_WIFI)
			*pu1Tmp = EFUSE_OOB_PROTECT_BYTES;
		else
			*pu1Tmp = EFUSE_PROTECT_BYTES_BANK;
	}
	break;

	case TYPE_EFUSE_CONTENT_LEN_BANK: {
		u16 *pu2Tmp;

		pu2Tmp = (u16 *)pOut;

		if (efuseType == EFUSE_WIFI)
			*pu2Tmp = EFUSE_REAL_CONTENT_LEN_8723D;
		else
			*pu2Tmp = EFUSE_BT_REAL_BANK_CONTENT_LEN;
	}
	break;

	default: {
		u8 *pu1Tmp;

		pu1Tmp = (u8 *)pOut;
		*pu1Tmp = 0;
	}
	break;
	}
}

#define VOLTAGE_V25		0x03
#define LDOE25_SHIFT	28

/* *****************************************************************
 *	The following is for compile ok
 *	That should be merged with the original in the future
 * ***************************************************************** */
#define EFUSE_ACCESS_ON_8723			0x69	/* For RTL8723 only. */
#define EFUSE_ACCESS_OFF_8723			0x00	/* For RTL8723 only. */
#define REG_EFUSE_ACCESS_8723			0x00CF	/* Efuse access protection for RTL8723 */

/* ***************************************************************** */
static void Hal_BT_EfusePowerSwitch(
	PADAPTER	padapter,
	u8			bWrite,
	u8			PwrState)
{
	u8 tempval;

	if (PwrState == _TRUE) {
		/* enable BT power cut */
		/* 0x6A[14] = 1 */
		tempval = rtw_read8(padapter, 0x6B);
		tempval |= BIT(6);
		rtw_write8(padapter, 0x6B, tempval);

		/* Attention!! Between 0x6A[14] and 0x6A[15] setting need 100us delay */
		/* So don't write 0x6A[14]=1 and 0x6A[15]=0 together! */
		rtw_usleep_os(100);
		/* disable BT output isolation */
		/* 0x6A[15] = 0 */
		tempval = rtw_read8(padapter, 0x6B);
		tempval &= ~BIT(7);
		rtw_write8(padapter, 0x6B, tempval);
	} else {
		/* enable BT output isolation */
		/* 0x6A[15] = 1 */
		tempval = rtw_read8(padapter, 0x6B);
		tempval |= BIT(7);
		rtw_write8(padapter, 0x6B, tempval);

		/* Attention!! Between 0x6A[14] and 0x6A[15] setting need 100us delay */
		/* So don't write 0x6A[14]=1 and 0x6A[15]=0 together! */

		/* disable BT power cut */
		/* 0x6A[14] = 1 */
		tempval = rtw_read8(padapter, 0x6B);
		tempval &= ~BIT(6);
		rtw_write8(padapter, 0x6B, tempval);
	}

}
static void
Hal_EfusePowerSwitch(
	PADAPTER	padapter,
	u8			bWrite,
	u8			PwrState)
{
	u8	tempval;
	u16	tmpV16;


	if (PwrState == _TRUE) {
#ifdef CONFIG_SDIO_HCI
		/* To avoid cannot access efuse regsiters after disable/enable several times during DTM test. */
		/* Suggested by SD1 IsaacHsu. 2013.07.08, added by tynli. */
		tempval = rtw_read8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HSUS_CTRL);
		if (tempval & BIT(0)) { /* SDIO local register is suspend */
			u8 count = 0;


			tempval &= ~BIT(0);
			rtw_write8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HSUS_CTRL, tempval);

			/* check 0x86[1:0]=10'2h, wait power state to leave suspend */
			do {
				tempval = rtw_read8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HSUS_CTRL);
				tempval &= 0x3;
				if (tempval == 0x02)
					break;

				count++;
				if (count >= 100)
					break;

				rtw_mdelay_os(10);
			} while (1);

			if (count >= 100) {
				RTW_INFO(FUNC_ADPT_FMT ": Leave SDIO local register suspend fail! Local 0x86=%#X\n",
					 FUNC_ADPT_ARG(padapter), tempval);
			} else {
				RTW_INFO(FUNC_ADPT_FMT ": Leave SDIO local register suspend OK! Local 0x86=%#X\n",
					 FUNC_ADPT_ARG(padapter), tempval);
			}
		}
#endif /* CONFIG_SDIO_HCI */

		rtw_write8(padapter, REG_EFUSE_ACCESS_8723, EFUSE_ACCESS_ON_8723);

		/* Reset: 0x0000h[28], default valid */
		tmpV16 =  rtw_read16(padapter, REG_SYS_FUNC_EN);
		if (!(tmpV16 & FEN_ELDR)) {
			tmpV16 |= FEN_ELDR;
			rtw_write16(padapter, REG_SYS_FUNC_EN, tmpV16);
		}

		/* Clock: Gated(0x0008h[5]) 8M(0x0008h[1]) clock from ANA, default valid */
		tmpV16 = rtw_read16(padapter, REG_SYS_CLKR);
		if ((!(tmpV16 & LOADER_CLK_EN))  || (!(tmpV16 & ANA8M))) {
			tmpV16 |= (LOADER_CLK_EN | ANA8M);
			rtw_write16(padapter, REG_SYS_CLKR, tmpV16);
		}

		if (bWrite == _TRUE) {
			/* Enable LDO 2.5V before read/write action */
			tempval = rtw_read8(padapter, EFUSE_TEST + 3);
			tempval &= 0x0F;
			tempval |= (VOLTAGE_V25 << 4);
			rtw_write8(padapter, EFUSE_TEST + 3, (tempval | 0x80));

			/* rtw_write8(padapter, REG_EFUSE_ACCESS, EFUSE_ACCESS_ON); */
		}
	} else {
		rtw_write8(padapter, REG_EFUSE_ACCESS, EFUSE_ACCESS_OFF);

		if (bWrite == _TRUE) {
			/* Disable LDO 2.5V after read/write action */
			tempval = rtw_read8(padapter, EFUSE_TEST + 3);
			rtw_write8(padapter, EFUSE_TEST + 3, (tempval & 0x7F));
		}

	}
}

static void
hal_ReadEFuse_WiFi(
	PADAPTER	padapter,
	u16			_offset,
	u16			_size_byte,
	u8			*pbuf,
	u8			bPseudoTest)
{
#ifdef HAL_EFUSE_MEMORY
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PEFUSE_HAL		pEfuseHal = &pHalData->EfuseHal;
#endif
	u8	*efuseTbl = NULL;
	u16	eFuse_Addr = 0;
	u8	offset, wden;
	u8	efuseHeader, efuseExtHdr, efuseData;
	u16	i, total, used;
	u8	efuse_usage = 0;

	/* RTW_INFO("YJ: ====>%s():_offset=%d _size_byte=%d bPseudoTest=%d\n", __func__, _offset, _size_byte, bPseudoTest); */
	/* */
	/* Do NOT excess total size of EFuse table. Added by Roger, 2008.11.10. */
	/* */
	if ((_offset + _size_byte) > EFUSE_MAX_MAP_LEN) {
		RTW_INFO("%s: Invalid offset(%#x) with read bytes(%#x)!!\n", __FUNCTION__, _offset, _size_byte);
		return;
	}

	efuseTbl = (u8 *)rtw_malloc(EFUSE_MAX_MAP_LEN);
	if (efuseTbl == NULL) {
		RTW_INFO("%s: alloc efuseTbl fail!\n", __FUNCTION__);
		return;
	}
	/* 0xff will be efuse default value instead of 0x00. */
	_rtw_memset(efuseTbl, 0xFF, EFUSE_MAX_MAP_LEN);


#ifdef CONFIG_DEBUG
	if (0) {
		for (i = 0; i < 256; i++)
			/* ReadEFuseByte(padapter, i, &efuseTbl[i], _FALSE); */
			efuse_OneByteRead(padapter, i, &efuseTbl[i], _FALSE);
		RTW_INFO("Efuse Content:\n");
		for (i = 0; i < 256; i++) {
			if (i % 16 == 0)
				printk(KERN_ERR"\n");
			printk(KERN_ERR"%02X ", efuseTbl[i]);
		}
		printk(KERN_ERR"\n");
	}
#endif


	/* switch bank back to bank 0 for later BT and wifi use. */
	hal_EfuseSwitchToBank(padapter, 0, bPseudoTest);

	while (AVAILABLE_EFUSE_ADDR(eFuse_Addr)) {
		/* ReadEFuseByte(padapter, eFuse_Addr++, &efuseHeader, bPseudoTest); */
		efuse_OneByteRead(padapter, eFuse_Addr++, &efuseHeader, bPseudoTest);
		if (efuseHeader == 0xFF) {
			RTW_INFO("%s: data end at address=%#x\n", __FUNCTION__, eFuse_Addr - 1);
			break;
		}
		/* RTW_INFO("%s: efuse[0x%X]=0x%02X\n", __FUNCTION__, eFuse_Addr-1, efuseHeader); */

		/* Check PG header for section num. */
		if (EXT_HEADER(efuseHeader)) {	/* extended header */
			offset = GET_HDR_OFFSET_2_0(efuseHeader);
			/* RTW_INFO("%s: extended header offset=0x%X\n", __FUNCTION__, offset); */

			/* ReadEFuseByte(padapter, eFuse_Addr++, &efuseExtHdr, bPseudoTest); */
			efuse_OneByteRead(padapter, eFuse_Addr++, &efuseExtHdr, bPseudoTest);
			/* RTW_INFO("%s: efuse[0x%X]=0x%02X\n", __FUNCTION__, eFuse_Addr-1, efuseExtHdr); */
			if (ALL_WORDS_DISABLED(efuseExtHdr))
				continue;

			offset |= ((efuseExtHdr & 0xF0) >> 1);
			wden = (efuseExtHdr & 0x0F);
		} else {
			offset = ((efuseHeader >> 4) & 0x0f);
			wden = (efuseHeader & 0x0f);
		}
		/* RTW_INFO("%s: Offset=%d Worden=0x%X\n", __FUNCTION__, offset, wden); */

		if (offset < EFUSE_MAX_SECTION_8723D) {
			u16 addr;
			/* Get word enable value from PG header
			*			RTW_INFO("%s: Offset=%d Worden=0x%X\n", __FUNCTION__, offset, wden); */

			addr = offset * PGPKT_DATA_SIZE;
			for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++) {
				/* Check word enable condition in the section */
				if (!(wden & (0x01 << i))) {
					efuseData = 0;
					/* ReadEFuseByte(padapter, eFuse_Addr++, &efuseData, bPseudoTest); */
					efuse_OneByteRead(padapter, eFuse_Addr++, &efuseData, bPseudoTest);
					/*					RTW_INFO("%s: efuse[%#X]=0x%02X\n", __FUNCTION__, eFuse_Addr-1, efuseData); */
					efuseTbl[addr] = efuseData;

					efuseData = 0;
					/* ReadEFuseByte(padapter, eFuse_Addr++, &efuseData, bPseudoTest); */
					efuse_OneByteRead(padapter, eFuse_Addr++, &efuseData, bPseudoTest);
					/*					RTW_INFO("%s: efuse[%#X]=0x%02X\n", __FUNCTION__, eFuse_Addr-1, efuseData); */
					efuseTbl[addr + 1] = efuseData;
				}
				addr += 2;
			}
		} else {
			RTW_INFO(KERN_ERR "%s: offset(%d) is illegal!!\n", __FUNCTION__, offset);
			eFuse_Addr += Efuse_CalculateWordCnts(wden) * 2;
		}
	}

	/* Copy from Efuse map to output pointer memory!!! */
	for (i = 0; i < _size_byte; i++)
		pbuf[i] = efuseTbl[_offset + i];

	/* Calculate Efuse utilization */
	total = 0;
	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, &total, bPseudoTest);
	used = eFuse_Addr - 1;
	if (total)
		efuse_usage = (u8)((used * 100) / total);
	else
		efuse_usage = 100;
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

	if (efuseTbl)
		rtw_mfree(efuseTbl, EFUSE_MAX_MAP_LEN);
}

static void
hal_ReadEFuse_BT(
	PADAPTER	padapter,
	u16			_offset,
	u16			_size_byte,
	u8			*pbuf,
	u8			bPseudoTest
)
{
#ifdef HAL_EFUSE_MEMORY
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PEFUSE_HAL		pEfuseHal = &pHalData->EfuseHal;
#endif
	u8	*efuseTbl;
	u8	bank;
	u16	eFuse_Addr;
	u8	efuseHeader, efuseExtHdr, efuseData;
	u8	offset, wden;
	u16	i, total, used;
	u8	efuse_usage;


	/* */
	/* Do NOT excess total size of EFuse table. Added by Roger, 2008.11.10. */
	/* */
	if ((_offset + _size_byte) > EFUSE_BT_MAP_LEN) {
		RTW_INFO("%s: Invalid offset(%#x) with read bytes(%#x)!!\n", __FUNCTION__, _offset, _size_byte);
		return;
	}

	efuseTbl = rtw_malloc(EFUSE_BT_MAP_LEN);
	if (efuseTbl == NULL) {
		RTW_INFO("%s: efuseTbl malloc fail!\n", __FUNCTION__);
		return;
	}
	/* 0xff will be efuse default value instead of 0x00. */
	_rtw_memset(efuseTbl, 0xFF, EFUSE_BT_MAP_LEN);

	total = 0;
	EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_AVAILABLE_EFUSE_BYTES_BANK, &total, bPseudoTest);

	for (bank = 1; bank < 3; bank++) { /* 8723d Max bake 0~2 */
		if (hal_EfuseSwitchToBank(padapter, bank, bPseudoTest) == _FALSE) {
			RTW_INFO("%s: hal_EfuseSwitchToBank Fail!!\n", __FUNCTION__);
			goto exit;
		}

		eFuse_Addr = 0;

		while (AVAILABLE_EFUSE_ADDR(eFuse_Addr)) {
			/* ReadEFuseByte(padapter, eFuse_Addr++, &efuseHeader, bPseudoTest); */
			efuse_OneByteRead(padapter, eFuse_Addr++, &efuseHeader, bPseudoTest);
			if (efuseHeader == 0xFF)
				break;
			RTW_INFO("%s: efuse[%#X]=0x%02x (header)\n", __FUNCTION__, (((bank - 1) * EFUSE_REAL_CONTENT_LEN_8723D) + eFuse_Addr - 1), efuseHeader);

			/* Check PG header for section num. */
			if (EXT_HEADER(efuseHeader)) {	/* extended header */
				offset = GET_HDR_OFFSET_2_0(efuseHeader);
				RTW_INFO("%s: extended header offset_2_0=0x%X\n", __FUNCTION__, offset);

				/* ReadEFuseByte(padapter, eFuse_Addr++, &efuseExtHdr, bPseudoTest); */
				efuse_OneByteRead(padapter, eFuse_Addr++, &efuseExtHdr, bPseudoTest);
				RTW_INFO("%s: efuse[%#X]=0x%02x (ext header)\n", __FUNCTION__, (((bank - 1) * EFUSE_REAL_CONTENT_LEN_8723D) + eFuse_Addr - 1), efuseExtHdr);
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

				/* Get word enable value from PG header */
				RTW_INFO("%s: Offset=%d Worden=%#X\n", __FUNCTION__, offset, wden);

				addr = offset * PGPKT_DATA_SIZE;
				for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++) {
					/* Check word enable condition in the section */
					if (!(wden & (0x01 << i))) {
						efuseData = 0;
						/* ReadEFuseByte(padapter, eFuse_Addr++, &efuseData, bPseudoTest); */
						efuse_OneByteRead(padapter, eFuse_Addr++, &efuseData, bPseudoTest);
						RTW_INFO("%s: efuse[%#X]=0x%02X\n", __FUNCTION__, eFuse_Addr - 1, efuseData);
						efuseTbl[addr] = efuseData;

						efuseData = 0;
						/* ReadEFuseByte(padapter, eFuse_Addr++, &efuseData, bPseudoTest); */
						efuse_OneByteRead(padapter, eFuse_Addr++, &efuseData, bPseudoTest);
						RTW_INFO("%s: efuse[%#X]=0x%02X\n", __FUNCTION__, eFuse_Addr - 1, efuseData);
						efuseTbl[addr + 1] = efuseData;
					}
					addr += 2;
				}
			} else {
				RTW_INFO("%s: offset(%d) is illegal!!\n", __FUNCTION__, offset);
				eFuse_Addr += Efuse_CalculateWordCnts(wden) * 2;
			}
		}

		if ((eFuse_Addr - 1) < total) {
			RTW_INFO("%s: bank(%d) data end at %#x\n", __FUNCTION__, bank, eFuse_Addr - 1);
			break;
		}
	}

	/* switch bank back to bank 0 for later BT and wifi use. */
	hal_EfuseSwitchToBank(padapter, 0, bPseudoTest);

	/* Copy from Efuse map to output pointer memory!!! */
	for (i = 0; i < _size_byte; i++)
		pbuf[i] = efuseTbl[_offset + i];

	/* */
	/* Calculate Efuse utilization. */
	/* */
	EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, &total, bPseudoTest);
	used = (EFUSE_BT_REAL_BANK_CONTENT_LEN * (bank - 1)) + eFuse_Addr - 1;
	RTW_INFO("%s: bank(%d) data end at %#x ,used =%d\n", __FUNCTION__, bank, eFuse_Addr - 1, used);
	efuse_usage = (u8)((used * 100) / total);
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
	if (efuseTbl)
		rtw_mfree(efuseTbl, EFUSE_BT_MAP_LEN);
}

static void
Hal_ReadEFuse(
	PADAPTER	padapter,
	u8			efuseType,
	u16			_offset,
	u16			_size_byte,
	u8			*pbuf,
	u8			bPseudoTest)
{
	if (efuseType == EFUSE_WIFI)
		hal_ReadEFuse_WiFi(padapter, _offset, _size_byte, pbuf, bPseudoTest);
	else
		hal_ReadEFuse_BT(padapter, _offset, _size_byte, pbuf, bPseudoTest);
}

static u16
hal_EfuseGetCurrentSize_WiFi(
	PADAPTER	padapter,
	u8			bPseudoTest)
{
#ifdef HAL_EFUSE_MEMORY
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PEFUSE_HAL		pEfuseHal = &pHalData->EfuseHal;
#endif
	u16	efuse_addr = 0;
	u16 start_addr = 0; /* for debug */
	u8	hoffset = 0, hworden = 0;
	u8	efuse_data, word_cnts = 0;
	u32 count = 0; /* for debug */


	if (bPseudoTest) {
#ifdef HAL_EFUSE_MEMORY
		efuse_addr = (u16)pEfuseHal->fakeEfuseUsedBytes;
#else
		efuse_addr = (u16)fakeEfuseUsedBytes;
#endif
	} else
		rtw_hal_get_hwreg(padapter, HW_VAR_EFUSE_BYTES, (u8 *)&efuse_addr);
	start_addr = efuse_addr;
	RTW_INFO("%s: start_efuse_addr=0x%X\n", __FUNCTION__, efuse_addr);

	/* switch bank back to bank 0 for later BT and wifi use. */
	hal_EfuseSwitchToBank(padapter, 0, bPseudoTest);

#if 0 /* for debug test */
	efuse_OneByteRead(padapter, 0x1FF, &efuse_data, bPseudoTest);
	RTW_INFO(FUNC_ADPT_FMT ": efuse raw 0x1FF=0x%02X\n",
		 FUNC_ADPT_ARG(padapter), efuse_data);
	efuse_data = 0xFF;
#endif /* for debug test */

	count = 0;
	while (AVAILABLE_EFUSE_ADDR(efuse_addr)) {
#if 1
		if (efuse_OneByteRead(padapter, efuse_addr, &efuse_data, bPseudoTest) == _FALSE) {
			RTW_INFO(KERN_ERR "%s: efuse_OneByteRead Fail! addr=0x%X !!\n", __FUNCTION__, efuse_addr);
			goto error;
		}
#else
		ReadEFuseByte(padapter, efuse_addr, &efuse_data, bPseudoTest);
#endif

		if (efuse_data == 0xFF)
			break;

		if ((start_addr != 0) && (efuse_addr == start_addr)) {
			count++;
			RTW_INFO(FUNC_ADPT_FMT ": [WARNING] efuse raw 0x%X=0x%02X not 0xFF!!(%d times)\n",
				FUNC_ADPT_ARG(padapter), efuse_addr, efuse_data, count);

			efuse_data = 0xFF;
			if (count < 4) {
				/* try again! */

				if (count > 2) {
					/* try again form address 0 */
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
			hoffset = (efuse_data >> 4) & 0x0F;
			hworden = efuse_data & 0x0F;
		}

		word_cnts = Efuse_CalculateWordCnts(hworden);
		efuse_addr += (word_cnts * 2) + 1;
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
	/* report max size to prevent write efuse */
	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, &efuse_addr, bPseudoTest);

exit:
	RTW_INFO("%s: CurrentSize=%d\n", __FUNCTION__, efuse_addr);

	return efuse_addr;
}

static u16
hal_EfuseGetCurrentSize_BT(
	PADAPTER	padapter,
	u8			bPseudoTest)
{
#ifdef HAL_EFUSE_MEMORY
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PEFUSE_HAL		pEfuseHal = &pHalData->EfuseHal;
#endif
	u16 btusedbytes;
	u16	efuse_addr;
	u8	bank, startBank;
	u8	hoffset = 0, hworden = 0;
	u8	efuse_data, word_cnts = 0;
	u16	retU2 = 0;
	u8 bContinual = _TRUE;


	if (bPseudoTest) {
#ifdef HAL_EFUSE_MEMORY
		btusedbytes = pEfuseHal->fakeBTEfuseUsedBytes;
#else
		btusedbytes = fakeBTEfuseUsedBytes;
#endif
	} else {
		btusedbytes = 0;
		rtw_hal_get_hwreg(padapter, HW_VAR_EFUSE_BT_BYTES, (u8 *)&btusedbytes);
	}
	efuse_addr = (u16)((btusedbytes % EFUSE_BT_REAL_BANK_CONTENT_LEN));
	startBank = (u8)(1 + (btusedbytes / EFUSE_BT_REAL_BANK_CONTENT_LEN));

	RTW_INFO("%s: start from bank=%d addr=0x%X\n", __FUNCTION__, startBank, efuse_addr);

	EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_AVAILABLE_EFUSE_BYTES_BANK, &retU2, bPseudoTest);

	for (bank = startBank; bank < 3; bank++) {
		if (hal_EfuseSwitchToBank(padapter, bank, bPseudoTest) == _FALSE) {
			RTW_INFO(KERN_ERR "%s: switch bank(%d) Fail!!\n", __FUNCTION__, bank);
			/* bank = EFUSE_MAX_BANK; */
			break;
		}

		/* only when bank is switched we have to reset the efuse_addr. */
		if (bank != startBank)
			efuse_addr = 0;
#if 1

		while (AVAILABLE_EFUSE_ADDR(efuse_addr)) {
			if (efuse_OneByteRead(padapter, efuse_addr, &efuse_data, bPseudoTest) == _FALSE) {
				RTW_INFO(KERN_ERR "%s: efuse_OneByteRead Fail! addr=0x%X !!\n", __FUNCTION__, efuse_addr);
				/* bank = EFUSE_MAX_BANK; */
				break;
			}
			RTW_INFO("%s: efuse_OneByteRead ! addr=0x%X !efuse_data=0x%X! bank =%d\n", __FUNCTION__, efuse_addr, efuse_data, bank);

			if (efuse_data == 0xFF)
				break;

			if (EXT_HEADER(efuse_data)) {
				hoffset = GET_HDR_OFFSET_2_0(efuse_data);
				efuse_addr++;
				efuse_OneByteRead(padapter, efuse_addr, &efuse_data, bPseudoTest);
				RTW_INFO("%s: efuse_OneByteRead EXT_HEADER ! addr=0x%X !efuse_data=0x%X! bank =%d\n", __FUNCTION__, efuse_addr, efuse_data, bank);

				if (ALL_WORDS_DISABLED(efuse_data)) {
					efuse_addr++;
					continue;
				}

				/*				hoffset = ((hoffset & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1); */
				hoffset |= ((efuse_data & 0xF0) >> 1);
				hworden = efuse_data & 0x0F;
			} else {
				hoffset = (efuse_data >> 4) & 0x0F;
				hworden =  efuse_data & 0x0F;
			}

			RTW_INFO(FUNC_ADPT_FMT": Offset=%d Worden=%#X\n",
				 FUNC_ADPT_ARG(padapter), hoffset, hworden);

			word_cnts = Efuse_CalculateWordCnts(hworden);
			/* read next header */
			efuse_addr += (word_cnts * 2) + 1;
		}
#else
		while (bContinual &&
		       efuse_OneByteRead(padapter, efuse_addr , &efuse_data, bPseudoTest) &&
		       AVAILABLE_EFUSE_ADDR(efuse_addr)) {
			if (efuse_data != 0xFF) {
				if ((efuse_data & 0x1F) == 0x0F) {	/* extended header */
					hoffset = efuse_data;
					efuse_addr++;
					efuse_OneByteRead(padapter, efuse_addr , &efuse_data, bPseudoTest);
					if ((efuse_data & 0x0F) == 0x0F) {
						efuse_addr++;
						continue;
					} else {
						hoffset = ((hoffset & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
						hworden = efuse_data & 0x0F;
					}
				} else {
					hoffset = (efuse_data >> 4) & 0x0F;
					hworden =  efuse_data & 0x0F;
				}
				word_cnts = Efuse_CalculateWordCnts(hworden);
				/* read next header							 */
				efuse_addr = efuse_addr + (word_cnts * 2) + 1;
			} else
				bContinual = _FALSE;
		}
#endif


		/* Check if we need to check next bank efuse */
		if (efuse_addr < retU2)
			break;/* don't need to check next bank. */
	}
#if 0
	retU2 = ((bank - 1) * EFUSE_BT_REAL_BANK_CONTENT_LEN) + efuse_addr;
	if (bPseudoTest) {
#ifdef HAL_EFUSE_MEMORY
		pEfuseHal->fakeBTEfuseUsedBytes = retU2;
#else
		fakeBTEfuseUsedBytes = retU2;
#endif
	} else
		rtw_hal_set_hwreg(padapter, HW_VAR_EFUSE_BT_BYTES, (u8 *)&retU2);
#else
	retU2 = ((bank - 1) * EFUSE_BT_REAL_BANK_CONTENT_LEN) + efuse_addr;
	if (bPseudoTest) {
		pEfuseHal->fakeBTEfuseUsedBytes = retU2;
		/* RT_DISP(FEEPROM, EFUSE_PG, ("Hal_EfuseGetCurrentSize_BT92C(), already use %u bytes\n", pEfuseHal->fakeBTEfuseUsedBytes)); */
	} else {
		pEfuseHal->BTEfuseUsedBytes = retU2;
		/* RT_DISP(FEEPROM, EFUSE_PG, ("Hal_EfuseGetCurrentSize_BT92C(), already use %u bytes\n", pEfuseHal->BTEfuseUsedBytes)); */
	}
#endif

	RTW_INFO("%s: CurrentSize=%d\n", __FUNCTION__, retU2);
	return retU2;
}

static u16
Hal_EfuseGetCurrentSize(
	PADAPTER	pAdapter,
	u8			efuseType,
	u8			bPseudoTest)
{
	u16	ret = 0;

	if (efuseType == EFUSE_WIFI)
		ret = hal_EfuseGetCurrentSize_WiFi(pAdapter, bPseudoTest);
	else
		ret = hal_EfuseGetCurrentSize_BT(pAdapter, bPseudoTest);

	return ret;
}

static u8
Hal_EfuseWordEnableDataWrite(
	PADAPTER	padapter,
	u16			efuse_addr,
	u8			word_en,
	u8			*data,
	u8			bPseudoTest)
{
	u16	tmpaddr = 0;
	u16	start_addr = efuse_addr;
	u8	badworden = 0x0F;
	u8	tmpdata[PGPKT_DATA_SIZE];


	/*	RTW_INFO("%s: efuse_addr=%#x word_en=%#x\n", __FUNCTION__, efuse_addr, word_en); */
	_rtw_memset(tmpdata, 0xFF, PGPKT_DATA_SIZE);

	if (!(word_en & BIT(0))) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(padapter, start_addr++, data[0], bPseudoTest);
		efuse_OneByteWrite(padapter, start_addr++, data[1], bPseudoTest);

		efuse_OneByteRead(padapter, tmpaddr, &tmpdata[0], bPseudoTest);
		efuse_OneByteRead(padapter, tmpaddr + 1, &tmpdata[1], bPseudoTest);
		if ((data[0] != tmpdata[0]) || (data[1] != tmpdata[1]))
			badworden &= (~BIT(0));
	}
	if (!(word_en & BIT(1))) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(padapter, start_addr++, data[2], bPseudoTest);
		efuse_OneByteWrite(padapter, start_addr++, data[3], bPseudoTest);

		efuse_OneByteRead(padapter, tmpaddr, &tmpdata[2], bPseudoTest);
		efuse_OneByteRead(padapter, tmpaddr + 1, &tmpdata[3], bPseudoTest);
		if ((data[2] != tmpdata[2]) || (data[3] != tmpdata[3]))
			badworden &= (~BIT(1));
	}
	if (!(word_en & BIT(2))) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(padapter, start_addr++, data[4], bPseudoTest);
		efuse_OneByteWrite(padapter, start_addr++, data[5], bPseudoTest);

		efuse_OneByteRead(padapter, tmpaddr, &tmpdata[4], bPseudoTest);
		efuse_OneByteRead(padapter, tmpaddr + 1, &tmpdata[5], bPseudoTest);
		if ((data[4] != tmpdata[4]) || (data[5] != tmpdata[5]))
			badworden &= (~BIT(2));
	}
	if (!(word_en & BIT(3))) {
		tmpaddr = start_addr;
		efuse_OneByteWrite(padapter, start_addr++, data[6], bPseudoTest);
		efuse_OneByteWrite(padapter, start_addr++, data[7], bPseudoTest);

		efuse_OneByteRead(padapter, tmpaddr, &tmpdata[6], bPseudoTest);
		efuse_OneByteRead(padapter, tmpaddr + 1, &tmpdata[7], bPseudoTest);
		if ((data[6] != tmpdata[6]) || (data[7] != tmpdata[7]))
			badworden &= (~BIT(3));
	}

	return badworden;
}

static s32
Hal_EfusePgPacketRead(
	PADAPTER	padapter,
	u8			offset,
	u8			*data,
	u8			bPseudoTest)
{
	u8	bDataEmpty = _TRUE;
	u8	efuse_data, word_cnts = 0;
	u16	efuse_addr = 0;
	u8	hoffset = 0, hworden = 0;
	u8	i;
	u8	max_section = 0;
	s32	ret;


	if (data == NULL)
		return _FALSE;

	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_EFUSE_MAX_SECTION, &max_section, bPseudoTest);
	if (offset > max_section) {
		RTW_INFO("%s: Packet offset(%d) is illegal(>%d)!\n", __FUNCTION__, offset, max_section);
		return _FALSE;
	}

	_rtw_memset(data, 0xFF, PGPKT_DATA_SIZE);
	ret = _TRUE;

	/* */
	/* <Roger_TODO> Efuse has been pre-programmed dummy 5Bytes at the end of Efuse by CP. */
	/* Skip dummy parts to prevent unexpected data read from Efuse. */
	/* By pass right now. 2009.02.19. */
	/* */
	while (AVAILABLE_EFUSE_ADDR(efuse_addr)) {
		if (efuse_OneByteRead(padapter, efuse_addr++, &efuse_data, bPseudoTest) == _FALSE) {
			ret = _FALSE;
			break;
		}

		if (efuse_data == 0xFF)
			break;

		if (EXT_HEADER(efuse_data)) {
			hoffset = GET_HDR_OFFSET_2_0(efuse_data);
			efuse_OneByteRead(padapter, efuse_addr++, &efuse_data, bPseudoTest);
			if (ALL_WORDS_DISABLED(efuse_data)) {
				RTW_INFO("%s: Error!! All words disabled!\n", __FUNCTION__);
				continue;
			}

			hoffset |= ((efuse_data & 0xF0) >> 1);
			hworden = efuse_data & 0x0F;
		} else {
			hoffset = (efuse_data >> 4) & 0x0F;
			hworden =  efuse_data & 0x0F;
		}

		if (hoffset == offset) {
			for (i = 0; i < EFUSE_MAX_WORD_UNIT; i++) {
				/* Check word enable condition in the section */
				if (!(hworden & (0x01 << i))) {
					/* ReadEFuseByte(padapter, efuse_addr++, &efuse_data, bPseudoTest); */
					efuse_OneByteRead(padapter, efuse_addr++, &efuse_data, bPseudoTest);
					/*					RTW_INFO("%s: efuse[%#X]=0x%02X\n", __FUNCTION__, efuse_addr+tmpidx, efuse_data); */
					data[i * 2] = efuse_data;

					/* ReadEFuseByte(padapter, efuse_addr++, &efuse_data, bPseudoTest); */
					efuse_OneByteRead(padapter, efuse_addr++, &efuse_data, bPseudoTest);
					/*					RTW_INFO("%s: efuse[%#X]=0x%02X\n", __FUNCTION__, efuse_addr+tmpidx, efuse_data); */
					data[(i * 2) + 1] = efuse_data;
				}
			}
		} else {
			word_cnts = Efuse_CalculateWordCnts(hworden);
			efuse_addr += word_cnts * 2;
		}
	}

	return ret;
}

static u8
hal_EfusePgCheckAvailableAddr(
	PADAPTER	pAdapter,
	u8			efuseType,
	u8		bPseudoTest)
{
	u16	max_available = 0;
	u16 current_size;


	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, &max_available, bPseudoTest);
	/*	RTW_INFO("%s: max_available=%d\n", __FUNCTION__, max_available); */

	current_size = Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest);
	if (current_size >= max_available) {
		RTW_INFO("%s: Error!! current_size(%d)>max_available(%d)\n", __FUNCTION__, current_size, max_available);
		return _FALSE;
	}
	return _TRUE;
}

static void
hal_EfuseConstructPGPkt(
	u8				offset,
	u8				word_en,
	u8				*pData,
	PPGPKT_STRUCT	pTargetPkt)
{
	_rtw_memset(pTargetPkt->data, 0xFF, PGPKT_DATA_SIZE);
	pTargetPkt->offset = offset;
	pTargetPkt->word_en = word_en;
	efuse_WordEnableDataRead(word_en, pData, pTargetPkt->data);
	pTargetPkt->word_cnts = Efuse_CalculateWordCnts(pTargetPkt->word_en);
}

#if 0
static u8
wordEnMatched(
	PPGPKT_STRUCT	pTargetPkt,
	PPGPKT_STRUCT	pCurPkt,
	u8				*pWden)
{
	u8	match_word_en = 0x0F;	/* default all words are disabled */
	u8	i;

	/* check if the same words are enabled both target and current PG packet */
	if (((pTargetPkt->word_en & BIT(0)) == 0) &&
	    ((pCurPkt->word_en & BIT(0)) == 0)) {
		match_word_en &= ~BIT(0);				/* enable word 0 */
	}
	if (((pTargetPkt->word_en & BIT(1)) == 0) &&
	    ((pCurPkt->word_en & BIT(1)) == 0)) {
		match_word_en &= ~BIT(1);				/* enable word 1 */
	}
	if (((pTargetPkt->word_en & BIT(2)) == 0) &&
	    ((pCurPkt->word_en & BIT(2)) == 0)) {
		match_word_en &= ~BIT(2);				/* enable word 2 */
	}
	if (((pTargetPkt->word_en & BIT(3)) == 0) &&
	    ((pCurPkt->word_en & BIT(3)) == 0)) {
		match_word_en &= ~BIT(3);				/* enable word 3 */
	}

	*pWden = match_word_en;

	if (match_word_en != 0xf)
		return _TRUE;
	else
		return _FALSE;
}

static u8
hal_EfuseCheckIfDatafollowed(
	PADAPTER		pAdapter,
	u8				word_cnts,
	u16				startAddr,
	u8				bPseudoTest)
{
	u8 bRet = _FALSE;
	u8 i, efuse_data;

	for (i = 0; i < (word_cnts * 2); i++) {
		if (efuse_OneByteRead(pAdapter, (startAddr + i) , &efuse_data, bPseudoTest) == _FALSE) {
			RTW_INFO("%s: efuse_OneByteRead FAIL!!\n", __FUNCTION__);
			bRet = _TRUE;
			break;
		}

		if (efuse_data != 0xFF) {
			bRet = _TRUE;
			break;
		}
	}

	return bRet;
}
#endif

static u8
hal_EfusePartialWriteCheck(
	PADAPTER		padapter,
	u8				efuseType,
	u16				*pAddr,
	PPGPKT_STRUCT	pTargetPkt,
	u8				bPseudoTest)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	PEFUSE_HAL		pEfuseHal = &pHalData->EfuseHal;
	u8	bRet = _FALSE;
	u16	startAddr = 0, efuse_max_available_len = 0, efuse_max = 0;
	u8	efuse_data = 0;
#if 0
	u8	i, cur_header = 0;
	u8	new_wden = 0, matched_wden = 0, badworden = 0;
	PGPKT_STRUCT	curPkt;
#endif


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
	RTW_INFO("%s: startAddr=%#X\n", __FUNCTION__, startAddr);

	while (1) {
		if (startAddr >= efuse_max_available_len) {
			bRet = _FALSE;
			RTW_INFO("%s: startAddr(%d) >= efuse_max_available_len(%d)\n",
				__FUNCTION__, startAddr, efuse_max_available_len);
			break;
		}

		if (efuse_OneByteRead(padapter, startAddr, &efuse_data, bPseudoTest) && (efuse_data != 0xFF)) {
#if 1
			bRet = _FALSE;
			RTW_INFO("%s: Something Wrong! last bytes(%#X=0x%02X) is not 0xFF\n",
				 __FUNCTION__, startAddr, efuse_data);
			break;
#else
			if (EXT_HEADER(efuse_data)) {
				cur_header = efuse_data;
				startAddr++;
				efuse_OneByteRead(padapter, startAddr, &efuse_data, bPseudoTest);
				if (ALL_WORDS_DISABLED(efuse_data)) {
					RTW_INFO("%s: Error condition, all words disabled!", __FUNCTION__);
					bRet = _FALSE;
					break;
				} /*else*/
				{
					curPkt.offset = ((cur_header & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
					curPkt.word_en = efuse_data & 0x0F;
				}
			} else {
				cur_header  =  efuse_data;
				curPkt.offset = (cur_header >> 4) & 0x0F;
				curPkt.word_en = cur_header & 0x0F;
			}

			curPkt.word_cnts = Efuse_CalculateWordCnts(curPkt.word_en);
			/* if same header is found but no data followed */
			/* write some part of data followed by the header. */
			if ((curPkt.offset == pTargetPkt->offset) &&
			    (hal_EfuseCheckIfDatafollowed(padapter, curPkt.word_cnts, startAddr + 1, bPseudoTest) == _FALSE) &&
			    wordEnMatched(pTargetPkt, &curPkt, &matched_wden) == _TRUE) {
				RTW_INFO("%s: Need to partial write data by the previous wrote header\n", __FUNCTION__);
				/* Here to write partial data */
				badworden = Efuse_WordEnableDataWrite(padapter, startAddr + 1, matched_wden, pTargetPkt->data, bPseudoTest);
				if (badworden != 0x0F) {
					u32	PgWriteSuccess = 0;
					/* if write fail on some words, write these bad words again */
					if (efuseType == EFUSE_WIFI)
						PgWriteSuccess = Efuse_PgPacketWrite(padapter, pTargetPkt->offset, badworden, pTargetPkt->data, bPseudoTest);
					else
						PgWriteSuccess = Efuse_PgPacketWrite_BT(padapter, pTargetPkt->offset, badworden, pTargetPkt->data, bPseudoTest);

					if (!PgWriteSuccess) {
						bRet = _FALSE;	/* write fail, return */
						break;
					}
				}
				/* partial write ok, update the target packet for later use */
				for (i = 0; i < 4; i++) {
					if ((matched_wden & (0x1 << i)) == 0) {	/* this word has been written */
						pTargetPkt->word_en |= (0x1 << i);	/* disable the word */
					}
				}
				pTargetPkt->word_cnts = Efuse_CalculateWordCnts(pTargetPkt->word_en);
			}
			/* read from next header */
			startAddr = startAddr + (curPkt.word_cnts * 2) + 1;
#endif
		} else {
			/* not used header, 0xff */
			*pAddr = startAddr;
			/*			RTW_INFO("%s: Started from unused header offset=%d\n", __FUNCTION__, startAddr)); */
			bRet = _TRUE;
			break;
		}
	}

	return bRet;
}

static u8
hal_EfusePgPacketWrite1ByteHeader(
	PADAPTER		pAdapter,
	u8				efuseType,
	u16				*pAddr,
	PPGPKT_STRUCT	pTargetPkt,
	u8				bPseudoTest)
{
	u8	bRet = _FALSE;
	u8	pg_header = 0, tmp_header = 0;
	u16	efuse_addr = *pAddr;
	u8	repeatcnt = 0;


	/*	RTW_INFO("%s\n", __FUNCTION__); */
	pg_header = ((pTargetPkt->offset << 4) & 0xf0) | pTargetPkt->word_en;

	do {
		efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
		efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);
		if (tmp_header != 0xFF)
			break;
		if (repeatcnt++ > EFUSE_REPEAT_THRESHOLD_) {
			RTW_INFO("%s: Repeat over limit for pg_header!!\n", __FUNCTION__);
			return _FALSE;
		}
	} while (1);

	if (tmp_header != pg_header) {
		RTW_INFO(KERN_ERR "%s: PG Header Fail!!(pg=0x%02X read=0x%02X)\n", __FUNCTION__, pg_header, tmp_header);
		return _FALSE;
	}

	*pAddr = efuse_addr;

	return _TRUE;
}

static u8
hal_EfusePgPacketWrite2ByteHeader(
	PADAPTER		padapter,
	u8				efuseType,
	u16				*pAddr,
	PPGPKT_STRUCT	pTargetPkt,
	u8				bPseudoTest)
{
	u16	efuse_addr, efuse_max_available_len = 0;
	u8	pg_header = 0, tmp_header = 0;
	u8	repeatcnt = 0;


	/*	RTW_INFO("%s\n", __FUNCTION__); */
	EFUSE_GetEfuseDefinition(padapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_BANK, &efuse_max_available_len, bPseudoTest);

	efuse_addr = *pAddr;
	if (efuse_addr >= efuse_max_available_len) {
		RTW_INFO("%s: addr(%d) over available(%d)!!\n", __FUNCTION__, efuse_addr, efuse_max_available_len);
		return _FALSE;
	}

	pg_header = ((pTargetPkt->offset & 0x07) << 5) | 0x0F;
	/*	RTW_INFO("%s: pg_header=0x%x\n", __FUNCTION__, pg_header); */

	do {
		efuse_OneByteWrite(padapter, efuse_addr, pg_header, bPseudoTest);
		efuse_OneByteRead(padapter, efuse_addr, &tmp_header, bPseudoTest);
		if (tmp_header != 0xFF)
			break;
		if (repeatcnt++ > EFUSE_REPEAT_THRESHOLD_) {
			RTW_INFO("%s: Repeat over limit for pg_header!!\n", __FUNCTION__);
			return _FALSE;
		}
	} while (1);

	if (tmp_header != pg_header) {
		RTW_INFO(KERN_ERR "%s: PG Header Fail!!(pg=0x%02X read=0x%02X)\n", __FUNCTION__, pg_header, tmp_header);
		return _FALSE;
	}

	/* to write ext_header */
	efuse_addr++;
	pg_header = ((pTargetPkt->offset & 0x78) << 1) | pTargetPkt->word_en;

	do {
		efuse_OneByteWrite(padapter, efuse_addr, pg_header, bPseudoTest);
		efuse_OneByteRead(padapter, efuse_addr, &tmp_header, bPseudoTest);
		if (tmp_header != 0xFF)
			break;
		if (repeatcnt++ > EFUSE_REPEAT_THRESHOLD_) {
			RTW_INFO("%s: Repeat over limit for ext_header!!\n", __FUNCTION__);
			return _FALSE;
		}
	} while (1);

	if (tmp_header != pg_header) {	/* offset PG fail */
		RTW_INFO(KERN_ERR "%s: PG EXT Header Fail!!(pg=0x%02X read=0x%02X)\n", __FUNCTION__, pg_header, tmp_header);
		return _FALSE;
	}

	*pAddr = efuse_addr;

	return _TRUE;
}

static u8
hal_EfusePgPacketWriteHeader(
	PADAPTER		padapter,
	u8				efuseType,
	u16				*pAddr,
	PPGPKT_STRUCT	pTargetPkt,
	u8				bPseudoTest)
{
	u8 bRet = _FALSE;

	if (pTargetPkt->offset >= EFUSE_MAX_SECTION_BASE)
		bRet = hal_EfusePgPacketWrite2ByteHeader(padapter, efuseType, pAddr, pTargetPkt, bPseudoTest);
	else
		bRet = hal_EfusePgPacketWrite1ByteHeader(padapter, efuseType, pAddr, pTargetPkt, bPseudoTest);

	return bRet;
}

static u8
hal_EfusePgPacketWriteData(
	PADAPTER		pAdapter,
	u8				efuseType,
	u16				*pAddr,
	PPGPKT_STRUCT	pTargetPkt,
	u8				bPseudoTest)
{
	u16	efuse_addr;
	u8	badworden;


	efuse_addr = *pAddr;
	badworden = Efuse_WordEnableDataWrite(pAdapter, efuse_addr + 1, pTargetPkt->word_en, pTargetPkt->data, bPseudoTest);
	if (badworden != 0x0F) {
		RTW_INFO("%s: Fail!!\n", __FUNCTION__);
		return _FALSE;
	}

	/*	RTW_INFO("%s: ok\n", __FUNCTION__); */
	return _TRUE;
}

static s32
Hal_EfusePgPacketWrite(
	PADAPTER	padapter,
	u8			offset,
	u8			word_en,
	u8			*pData,
	u8			bPseudoTest)
{
	PGPKT_STRUCT targetPkt;
	u16 startAddr = 0;
	u8 efuseType = EFUSE_WIFI;

	if (!hal_EfusePgCheckAvailableAddr(padapter, efuseType, bPseudoTest))
		return _FALSE;

	hal_EfuseConstructPGPkt(offset, word_en, pData, &targetPkt);

	if (!hal_EfusePartialWriteCheck(padapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return _FALSE;

	if (!hal_EfusePgPacketWriteHeader(padapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return _FALSE;

	if (!hal_EfusePgPacketWriteData(padapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return _FALSE;

	return _TRUE;
}

static u8
Hal_EfusePgPacketWrite_BT(
	PADAPTER	pAdapter,
	u8			offset,
	u8			word_en,
	u8			*pData,
	u8			bPseudoTest)
{
	PGPKT_STRUCT targetPkt;
	u16 startAddr = 0;
	u8 efuseType = EFUSE_BT;

	if (!hal_EfusePgCheckAvailableAddr(pAdapter, efuseType, bPseudoTest))
		return _FALSE;

	hal_EfuseConstructPGPkt(offset, word_en, pData, &targetPkt);

	if (!hal_EfusePartialWriteCheck(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return _FALSE;

	if (!hal_EfusePgPacketWriteHeader(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return _FALSE;

	if (!hal_EfusePgPacketWriteData(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return _FALSE;

	return _TRUE;
}


static void read_chip_version_8723d(PADAPTER padapter)
{
	u32				value32;
	HAL_DATA_TYPE	*pHalData;

	pHalData = GET_HAL_DATA(padapter);

	value32 = rtw_read32(padapter, REG_SYS_CFG);
	pHalData->version_id.ICType = CHIP_8723D;
	pHalData->version_id.ChipType = ((value32 & RTL_ID) ? TEST_CHIP : NORMAL_CHIP);
	pHalData->version_id.RFType = RF_TYPE_1T1R;
	pHalData->version_id.VendorType = ((value32 & VENDOR_ID) ? CHIP_VENDOR_UMC : CHIP_VENDOR_TSMC);
	pHalData->version_id.CUTVersion = (value32 & CHIP_VER_RTL_MASK) >> CHIP_VER_RTL_SHIFT; /* IC version (CUT) */

	/* For regulator mode. by tynli. 2011.01.14 */
	pHalData->RegulatorMode = ((value32 & SPS_SEL) ? RT_LDO_REGULATOR : RT_SWITCHING_REGULATOR);

	value32 = rtw_read32(padapter, REG_GPIO_OUTSTS);
	pHalData->version_id.ROMVer = ((value32 & RF_RL_ID) >> 20);	/* ROM code version. */

	/* For multi-function consideration. Added by Roger, 2010.10.06. */
	pHalData->MultiFunc = RT_MULTI_FUNC_NONE;
	value32 = rtw_read32(padapter, REG_MULTI_FUNC_CTRL);
	pHalData->MultiFunc |= ((value32 & WL_FUNC_EN) ? RT_MULTI_FUNC_WIFI : 0);
	pHalData->MultiFunc |= ((value32 & BT_FUNC_EN) ? RT_MULTI_FUNC_BT : 0);
	pHalData->MultiFunc |= ((value32 & GPS_FUNC_EN) ? RT_MULTI_FUNC_GPS : 0);
	pHalData->PolarityCtl = ((value32 & WL_HWPDN_SL) ? RT_POLARITY_HIGH_ACT : RT_POLARITY_LOW_ACT);

	/*
		if( IS_B_CUT(pHalData->version_id) || IS_C_CUT(pHalData->version_id))
		{
			RTW_INFO(" IS_B/C_CUT SWR up 1 level !!!!!!!!!!!!!!!!!\n");
			phy_set_mac_reg(padapter, 0x14, BIT(23)|BIT(22)|BIT(21)|BIT(20), 0x5);
		}else if ( IS_D_CUT(pHalData->version_id))
		{
			RTW_INFO(" IS_D_CUT SKIP SWR !!!!!!!!!!!!!!!!!\n");
		}
	*/

#if 1
	dump_chip_info(pHalData->version_id);
#endif

}


void rtl8723d_InitBeaconParameters(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u16 val16;
	u8 val8;


	val8 = DIS_TSF_UDT;
	val16 = val8 | (val8 << 8); /* port0 and port1 */
#ifdef CONFIG_BT_COEXIST
	/* Enable prot0 beacon function for PSTDMA */
	val16 |= EN_BCN_FUNCTION;
#endif

#ifdef CONFIG_CONCURRENT_MODE
	val16 |= (EN_BCN_FUNCTION << 8);
#endif
	rtw_write16(padapter, REG_BCN_CTRL, val16);

	/* TBTT setup time */
	rtw_write8(padapter, REG_TBTT_PROHIBIT, TBTT_PROHIBIT_SETUP_TIME);

	/* TBTT hold time: 0x540[19:8] */
	rtw_write8(padapter, REG_TBTT_PROHIBIT + 1, TBTT_PROHIBIT_HOLD_TIME_STOP_BCN & 0xFF);
	rtw_write8(padapter, REG_TBTT_PROHIBIT + 2,
		(rtw_read8(padapter, REG_TBTT_PROHIBIT + 2) & 0xF0) | (TBTT_PROHIBIT_HOLD_TIME_STOP_BCN >> 8));

	/* Firmware will control REG_DRVERLYINT when power saving is enable, */
	/* so don't set this register on STA mode. */
	if (check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE) == _FALSE)
		rtw_write8(padapter, REG_DRVERLYINT, DRIVER_EARLY_INT_TIME_8723D); /* 5ms */
	rtw_write8(padapter, REG_BCNDMATIM, BCN_DMA_ATIME_INT_TIME_8723D); /* 2ms */

	/* Suggested by designer timchen. Change beacon AIFS to the largest number */
	/* beacause test chip does not contension before sending beacon. by tynli. 2009.11.03 */
	rtw_write16(padapter, REG_BCNTCFG, 0x4413);

}

void rtl8723d_InitBeaconMaxError(PADAPTER padapter, u8 InfraMode)
{
#ifdef CONFIG_ADHOC_WORKAROUND_SETTING
	rtw_write8(padapter, REG_BCN_MAX_ERR, 0xFF);
#else
	/* rtw_write8(Adapter, REG_BCN_MAX_ERR, (InfraMode ? 0xFF : 0x10)); */
#endif
}

void _InitMacAPLLSetting_8723D(PADAPTER Adapter)
{

	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	u16 RegValue;
	u8	afe;

	RegValue = rtw_read16(Adapter, REG_AFE_CTRL_4_8723D);
	RegValue |= BIT(4);
	RegValue |= BIT(15);
	rtw_write16(Adapter, REG_AFE_CTRL_4_8723D, RegValue);

	/*
	 *	8723D with 24MHz xtal has VCO noise issue
	 *  This will cause some TRx test fail
	 *	Therefore, set MAC GM parameter for 24MHz xtal
	 *	AFE[0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 ] =
	 *	[ 40M 25M 13M 19.2M 20M 26M 38.4M 17.664M 16M 14.318M 12M 52M 48M 27M 24M ]
	 */
	afe = (pHalData->efuse_eeprom_data[4] >>4);
	if( afe == 14) {
		rtw_write32(Adapter, 0x2c, (rtw_read32(Adapter, 0x2c) | BIT28));
		rtw_write32(Adapter, 0x24, (rtw_read32(Adapter, 0x24) & 0xFFFFFF0F));
		rtw_write32(Adapter, 0x7c, ((rtw_read32(Adapter, 0x7c) | BIT29) & (~BIT28)));
	}

}

static void _BeaconFunctionEnable(PADAPTER padapter, u8 Enable, u8 Linked)
{
	rtw_write8(padapter, REG_BCN_CTRL, DIS_TSF_UDT | EN_BCN_FUNCTION | DIS_BCNQ_SUB);
	rtw_write8(padapter, REG_RD_CTRL + 1, 0x6F);
}

static void rtl8723d_SetBeaconRelatedRegisters(PADAPTER padapter)
{
	u8 val8;
	u32 value32;
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	u32 bcn_ctrl_reg;

	/* reset TSF, enable update TSF, correcting TSF On Beacon */

	/* REG_MBSSID_BCN_SPACE */
	/* REG_BCNDMATIM */
	/* REG_ATIMWND */
	/* REG_TBTT_PROHIBIT */
	/* REG_DRVERLYINT */
	/* REG_BCN_MAX_ERR */
	/* REG_BCNTCFG //(0x510) */
	/* REG_DUAL_TSF_RST */
	/* REG_BCN_CTRL //(0x550) */


	bcn_ctrl_reg = REG_BCN_CTRL;
#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->hw_port == HW_PORT1)
		bcn_ctrl_reg = REG_BCN_CTRL_1;
#endif

	/* */
	/* ATIM window */
	/* */
	rtw_write16(padapter, REG_ATIMWND, 2);

	/* */
	/* Beacon interval (in unit of TU). */
	/* */
	rtw_hal_set_hwreg(padapter, HW_VAR_BEACON_INTERVAL, (u8 *)&pmlmeinfo->bcn_interval);

	rtl8723d_InitBeaconParameters(padapter);

	rtw_write8(padapter, REG_SLOT, 0x09);

	/* */
	/* Reset TSF Timer to zero, added by Roger. 2008.06.24 */
	/* */
	value32 = rtw_read32(padapter, REG_TCR);
	value32 &= ~TSFRST;
	rtw_write32(padapter, REG_TCR, value32);

	value32 |= TSFRST;
	rtw_write32(padapter, REG_TCR, value32);

	/* NOTE: Fix test chip's bug (about contention windows's randomness) */
	if (check_fwstate(&padapter->mlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE | WIFI_AP_STATE | WIFI_MESH_STATE) == _TRUE) {
		rtw_write8(padapter, REG_RXTSF_OFFSET_CCK, 0x50);
		rtw_write8(padapter, REG_RXTSF_OFFSET_OFDM, 0x50);
	}

	_BeaconFunctionEnable(padapter, _TRUE, _TRUE);

	ResumeTxBeacon(padapter);
	val8 = rtw_read8(padapter, bcn_ctrl_reg);
	val8 |= DIS_BCNQ_SUB;
	rtw_write8(padapter, bcn_ctrl_reg, val8);
}

void hal_notch_filter_8723d(_adapter *adapter, bool enable)
{
	if (enable) {
		RTW_INFO("Enable notch filter\n");
		rtw_write8(adapter, rOFDM0_RxDSP + 1, rtw_read8(adapter, rOFDM0_RxDSP + 1) | BIT(1));
	} else {
		RTW_INFO("Disable notch filter\n");
		rtw_write8(adapter, rOFDM0_RxDSP + 1, rtw_read8(adapter, rOFDM0_RxDSP + 1) & ~BIT(1));
	}
}

/*
 * Description: In normal chip, we should send some packet to Hw which will be used by Fw
 *			in FW LPS mode. The function is to fill the Tx descriptor of this packets, then
 *			Fw can tell Hw to send these packet derectly.
 * Added by tynli. 2009.10.15.
 *
 * type1:pspoll, type2:null */
void rtl8723d_fill_fake_txdesc(
	PADAPTER	padapter,
	u8			*pDesc,
	u32			BufferLen,
	u8			IsPsPoll,
	u8			IsBTQosNull,
	u8			bDataFrame)
{
	/* Clear all status */
	_rtw_memset(pDesc, 0, TXDESC_SIZE);

	SET_TX_DESC_OFFSET_8723D(pDesc, 0x28); /* Offset = 32 */

	SET_TX_DESC_PKT_SIZE_8723D(pDesc, BufferLen); /* Buffer size + command header */
	SET_TX_DESC_QUEUE_SEL_8723D(pDesc, QSLT_MGNT); /* Fixed queue of Mgnt queue */

	/* Set NAVUSEHDR to prevent Ps-poll AId filed to be changed to error vlaue by Hw. */
	if (_TRUE == IsPsPoll)
		SET_TX_DESC_NAV_USE_HDR_8723D(pDesc, 1);
	else {
		SET_TX_DESC_HWSEQ_EN_8723D(pDesc, 1); /* Hw set sequence number */
		SET_TX_DESC_HWSEQ_SEL_8723D(pDesc, 0);
	}

	if (_TRUE == IsBTQosNull)
		SET_TX_DESC_BT_INT_8723D(pDesc, 1);

	SET_TX_DESC_USE_RATE_8723D(pDesc, 1); /* use data rate which is set by Sw */

	SET_TX_DESC_TX_RATE_8723D(pDesc, DESC8723D_RATE1M);

	/* */
	/* Encrypt the data frame if under security mode excepct null data. Suggested by CCW. */
	/* */
	if (_TRUE == bDataFrame) {
		u32 EncAlg;

		EncAlg = padapter->securitypriv.dot11PrivacyAlgrthm;
		switch (EncAlg) {
		case _NO_PRIVACY_:
			SET_TX_DESC_SEC_TYPE_8723D(pDesc, 0x0);
			break;
		case _WEP40_:
		case _WEP104_:
		case _TKIP_:
			SET_TX_DESC_SEC_TYPE_8723D(pDesc, 0x1);
			break;
		case _SMS4_:
			SET_TX_DESC_SEC_TYPE_8723D(pDesc, 0x2);
			break;
		case _AES_:
			SET_TX_DESC_SEC_TYPE_8723D(pDesc, 0x3);
			break;
		default:
			SET_TX_DESC_SEC_TYPE_8723D(pDesc, 0x0);
			break;
		}
	}

#if defined(CONFIG_USB_HCI)
	/*
	 * USB interface drop packet if the checksum of descriptor isn't correct.
	 * Using this checksum can let hardware recovery from packet bulk out error (e.g. Cancel URC, Bulk out error.).
	 */
	rtl8723d_cal_txdesc_chksum((struct tx_desc *)pDesc);
#endif
}

void rtl8723d_InitAntenna_Selection(PADAPTER padapter)
{
	rtw_write8(padapter, REG_LEDCFG2, 0x82);
}

void rtl8723d_CheckAntenna_Selection(PADAPTER padapter)
{
#if 0
	PHAL_DATA_TYPE pHalData;
	u8 val;


	pHalData = GET_HAL_DATA(padapter);

	val = rtw_read8(padapter, REG_LEDCFG2);
	/* Let 8051 take control antenna setting */
	if (!(val & BIT(7))) {
		val |= BIT(7); /* DPDT_SEL_EN, 0x4C[23] */
		rtw_write8(padapter, REG_LEDCFG2, val);
	}
#endif
}
void rtl8723d_DeinitAntenna_Selection(PADAPTER padapter)
{
#if 0
	PHAL_DATA_TYPE pHalData;
	u8 val;


	pHalData = GET_HAL_DATA(padapter);
	val = rtw_read8(padapter, REG_LEDCFG2);
	/* Let 8051 take control antenna setting */
	val &= ~BIT(7); /* DPDT_SEL_EN, clear 0x4C[23] */
	rtw_write8(padapter, REG_LEDCFG2, val);
#endif
}

void init_hal_spec_8723d(_adapter *adapter)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);

	hal_spec->ic_name = "rtl8723d";
	hal_spec->macid_num = 16;
	hal_spec->sec_cam_ent_num = 32;
	hal_spec->sec_cap = SEC_CAP_CHK_BMC;
	hal_spec->macid_cap = MACID_DROP_INDIRECT;
	hal_spec->macid_txrpt = 0x8100;
	hal_spec->macid_txrpt_pgsz = 16;

	hal_spec->rfpath_num_2g = 2;
	hal_spec->rfpath_num_5g = 0;
	hal_spec->rf_reg_path_num = hal_spec->rf_reg_path_avail_num = 1;
	hal_spec->rf_reg_trx_path_bmp = 0x11;
	hal_spec->max_tx_cnt = 1;

	hal_spec->tx_nss_num = 1;
	hal_spec->rx_nss_num = 1;
	hal_spec->band_cap = BAND_CAP_2G;
	hal_spec->bw_cap = BW_CAP_20M | BW_CAP_40M;
	hal_spec->port_num = 3;
	hal_spec->proto_cap = PROTO_CAP_11B | PROTO_CAP_11G | PROTO_CAP_11N;

	hal_spec->txgi_max = 63;
	hal_spec->txgi_pdbm = 2;

	hal_spec->wl_func = 0
			    | WL_FUNC_P2P
			    | WL_FUNC_MIRACAST
			    | WL_FUNC_TDLS
			    ;

	hal_spec->tx_aclt_unit_factor = 8;

	hal_spec->pg_txpwr_saddr = 0x10;
	hal_spec->pg_txgi_diff_factor = 1;

	rtw_macid_ctl_init_sleep_reg(adapter_to_macidctl(adapter)
		, REG_MACID_SLEEP, 0, 0, 0);
}

void rtl8723d_init_default_value(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	u8 i;

	pHalData = GET_HAL_DATA(padapter);

	/* init default value */
	pHalData->fw_ractrl = _FALSE;
	if (!adapter_to_pwrctl(padapter)->bkeepfwalive)
		pHalData->LastHMEBoxNum = 0;

	/* init phydm default value */
	pHalData->bIQKInitialized = _FALSE;

	/* init Efuse variables */
	pHalData->EfuseUsedBytes = 0;
	pHalData->EfuseUsedPercentage = 0;
#ifdef HAL_EFUSE_MEMORY
	pHalData->EfuseHal.fakeEfuseBank = 0;
	pHalData->EfuseHal.fakeEfuseUsedBytes = 0;
	_rtw_memset(pHalData->EfuseHal.fakeEfuseContent, 0xFF, EFUSE_MAX_HW_SIZE);
	_rtw_memset(pHalData->EfuseHal.fakeEfuseInitMap, 0xFF, EFUSE_MAX_MAP_LEN);
	_rtw_memset(pHalData->EfuseHal.fakeEfuseModifiedMap, 0xFF, EFUSE_MAX_MAP_LEN);
	pHalData->EfuseHal.BTEfuseUsedBytes = 0;
	pHalData->EfuseHal.BTEfuseUsedPercentage = 0;
	_rtw_memset(pHalData->EfuseHal.BTEfuseContent, 0xFF, EFUSE_MAX_BT_BANK * EFUSE_MAX_HW_SIZE);
	_rtw_memset(pHalData->EfuseHal.BTEfuseInitMap, 0xFF, EFUSE_BT_MAX_MAP_LEN);
	_rtw_memset(pHalData->EfuseHal.BTEfuseModifiedMap, 0xFF, EFUSE_BT_MAX_MAP_LEN);
	pHalData->EfuseHal.fakeBTEfuseUsedBytes = 0;
	_rtw_memset(pHalData->EfuseHal.fakeBTEfuseContent, 0xFF, EFUSE_MAX_BT_BANK * EFUSE_MAX_HW_SIZE);
	_rtw_memset(pHalData->EfuseHal.fakeBTEfuseInitMap, 0xFF, EFUSE_BT_MAX_MAP_LEN);
	_rtw_memset(pHalData->EfuseHal.fakeBTEfuseModifiedMap, 0xFF, EFUSE_BT_MAX_MAP_LEN);
#endif
	pHalData->need_restore = _FALSE;
}

u8 GetEEPROMSize8723D(PADAPTER padapter)
{
	u8 size = 0;
	u32	cr;

	cr = rtw_read16(padapter, REG_9346CR);
	/* 6: EEPROM used is 93C46, 4: boot from E-Fuse. */
	size = (cr & BOOT_FROM_EEPROM) ? 6 : 4;

	RTW_INFO("EEPROM type is %s\n", size == 4 ? "E-FUSE" : "93C46");

	return size;
}

/* -------------------------------------------------------------------------
 *
 * LLT R/W/Init function
 *
 * ------------------------------------------------------------------------- */
s32 rtl8723d_InitLLTTable(PADAPTER padapter)
{
	systime start;
	u32 passing_time;
	u32 val32;
	s32 ret;


	ret = _FAIL;

	val32 = rtw_read32(padapter, REG_AUTO_LLT);
	val32 |= BIT_AUTO_INIT_LLT;
	rtw_write32(padapter, REG_AUTO_LLT, val32);

	start = rtw_get_current_time();

	do {
		val32 = rtw_read32(padapter, REG_AUTO_LLT);
		if (!(val32 & BIT_AUTO_INIT_LLT)) {
			ret = _SUCCESS;
			break;
		}

		passing_time = rtw_get_passing_time_ms(start);
		if (passing_time > 1000) {
			RTW_INFO("%s: FAIL!! REG_AUTO_LLT(0x%X)=%08x\n",
				 __FUNCTION__, REG_AUTO_LLT, val32);
			break;
		}

		rtw_usleep_os(2);
	} while (1);

	return ret;
}

#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
void _DisableGPIO(PADAPTER	padapter)
{
	/***************************************
	j. GPIO_PIN_CTRL 0x44[31:0]=0x000
	k.Value = GPIO_PIN_CTRL[7:0]
	l. GPIO_PIN_CTRL 0x44[31:0] = 0x00FF0000 | (value <<8);
	m. GPIO_MUXCFG 0x42 [15:0] = 0x0780
	n. LEDCFG 0x4C[15:0] = 0x8080
	***************************************/
	u8	value8;
	u16	value16;
	u32	value32;
	u32	u4bTmp;


	/* 1. Disable GPIO[7:0] */
	rtw_write16(padapter, REG_GPIO_PIN_CTRL + 2, 0x0000);
	value32 = rtw_read32(padapter, REG_GPIO_PIN_CTRL) & 0xFFFF00FF;
	u4bTmp = value32 & 0x000000FF;
	value32 |= ((u4bTmp << 8) | 0x00FF0000);
	rtw_write32(padapter, REG_GPIO_PIN_CTRL, value32);


	/* 2. Disable GPIO[10:8] */
	rtw_write8(padapter, REG_MAC_PINMUX_CFG, 0x00);
	value16 = rtw_read16(padapter, REG_GPIO_IO_SEL) & 0xFF0F;
	value8 = (u8)(value16 & 0x000F);
	value16 |= ((value8 << 4) | 0x0780);
	rtw_write16(padapter, REG_GPIO_IO_SEL, value16);


	/* 3. Disable LED0 & 1 */
	rtw_write16(padapter, REG_LEDCFG0, 0x8080);

} /* end of _DisableGPIO() */

void _DisableRFAFEAndResetBB8723D(PADAPTER padapter)
{
	/**************************************
	a.	TXPAUSE 0x522[7:0] = 0xFF
	b.	RF path 0 offset 0x00 = 0x00
	c.	APSD_CTRL 0x600[7:0] = 0x40
	d.	SYS_FUNC_EN 0x02[7:0] = 0x16
	e.	SYS_FUNC_EN 0x02[7:0] = 0x14
	***************************************/
	enum rf_path eRFPath = RF_PATH_A, value8 = 0;

	rtw_write8(padapter, REG_TXPAUSE, 0xFF);

	phy_set_rf_reg(padapter, eRFPath, 0x0, bMaskByte0, 0x0);

	value8 |= APSDOFF;
	rtw_write8(padapter, REG_APSD_CTRL, value8);/* 0x40 */

	/* Set BB reset at first */
	value8 = 0;
	value8 |= (FEN_USBD | FEN_USBA | FEN_BB_GLB_RSTn);
	rtw_write8(padapter, REG_SYS_FUNC_EN, value8); /* 0x16 */

	/* Set global reset. */
	value8 &= ~FEN_BB_GLB_RSTn;
	rtw_write8(padapter, REG_SYS_FUNC_EN, value8); /* 0x14 */

	/* 2010/08/12 MH We need to set BB/GLBAL reset to save power for SS mode. */

}

void _DisableRFAFEAndResetBB(PADAPTER padapter)
{
	_DisableRFAFEAndResetBB8723D(padapter);
}

void _ResetDigitalProcedure1_8723D(PADAPTER padapter, BOOLEAN bWithoutHWSM)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if (IS_FW_81xxC(padapter) && (pHalData->firmware_version <= 0x20)) {
#if 0
		/*****************************
				f.	SYS_FUNC_EN 0x03[7:0]=0x54
				g.	MCUFWDL 0x80[7:0]=0
		******************************/
		u32	value32 = 0;

		rtw_write8(padapter, REG_SYS_FUNC_EN + 1, 0x54);
		rtw_write8(padapter, REG_MCUFWDL, 0);
#else
		/*****************************
		f.	MCUFWDL 0x80[7:0]=0
		g.	SYS_FUNC_EN 0x02[10]= 0
		h.	SYS_FUNC_EN 0x02[15-12]= 5
		i.     SYS_FUNC_EN 0x02[10]= 1
		******************************/
		u16 valu16 = 0;

		rtw_write8(padapter, REG_MCUFWDL, 0);

		valu16 = rtw_read16(padapter, REG_SYS_FUNC_EN);
		rtw_write16(padapter, REG_SYS_FUNC_EN, (valu16 & (~FEN_CPUEN)));/* reset MCU ,8051 */

		valu16 = rtw_read16(padapter, REG_SYS_FUNC_EN) & 0x0FFF;
		rtw_write16(padapter, REG_SYS_FUNC_EN, (valu16 | (FEN_HWPDN | FEN_ELDR))); /* reset MAC */

		valu16 = rtw_read16(padapter, REG_SYS_FUNC_EN);
		rtw_write16(padapter, REG_SYS_FUNC_EN, (valu16 | FEN_CPUEN));/* enable MCU ,8051 */
#endif
	} else {
		u8 retry_cnts = 0;

		/* 2010/08/12 MH For USB SS, we can not stop 8051 when we are trying to */
		/* enter IPS/HW&SW radio off. For S3/S4/S5/Disable, we can stop 8051 because */
		/* we will init FW when power on again. */
		/* if(!pDevice->RegUsbSS) */
		{	/* If we want to SS mode, we can not reset 8051. */
			if (rtw_read8(padapter, REG_MCUFWDL) & BIT(1)) {
				/* IF fw in RAM code, do reset */


				if (pHalData->bFWReady) {
					/* 2010/08/25 MH According to RD alfred's suggestion, we need to disable other */
					/* HRCV INT to influence 8051 reset. */
					rtw_write8(padapter, REG_FWIMR, 0x20);
					/* 2011/02/15 MH According to Alex's suggestion, close mask to prevent incorrect FW write operation. */
					rtw_write8(padapter, REG_FTIMR, 0x00);
					rtw_write8(padapter, REG_FSIMR, 0x00);

					rtw_write8(padapter, REG_HMETFR + 3, 0x20); /* 8051 reset by self */

					while ((retry_cnts++ < 100) && (FEN_CPUEN & rtw_read16(padapter, REG_SYS_FUNC_EN))) {
						rtw_udelay_os(50);/* us */
						/* 2010/08/25 For test only We keep on reset 5051 to prevent fail. */
						/* rtw_write8(padapter, REG_HMETFR+3, 0x20);//8051 reset by self */
					}
					/*					RT_ASSERT((retry_cnts < 100), ("8051 reset failed!\n")); */

					if (retry_cnts >= 100) {
						/* if 8051 reset fail we trigger GPIO 0 for LA */
						/* rtw_write32(	padapter, */
						/*						REG_GPIO_PIN_CTRL, */
						/*						0x00010100); */
						/* 2010/08/31 MH According to Filen's info, if 8051 reset fail, reset MAC directly. */
						rtw_write8(padapter, REG_SYS_FUNC_EN + 1, 0x50);	/* Reset MAC and Enable 8051 */
						rtw_mdelay_os(10);
					}

				}
			}

			rtw_write8(padapter, REG_SYS_FUNC_EN + 1, 0x54);	/* Reset MAC and Enable 8051 */
			rtw_write8(padapter, REG_MCUFWDL, 0);
		}
	}

	/* if(pDevice->RegUsbSS) */
	/* bWithoutHWSM = TRUE;	// Sugest by Filen and Issau. */

	if (bWithoutHWSM) {
		/* HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(padapter); */
		/*****************************
			Without HW auto state machine
		g.	SYS_CLKR 0x08[15:0] = 0x30A3
		h.	AFE_PLL_CTRL 0x28[7:0] = 0x80
		i.	AFE_XTAL_CTRL 0x24[15:0] = 0x880F
		j.	SYS_ISO_CTRL 0x00[7:0] = 0xF9
		******************************/
		/* rtw_write16(padapter, REG_SYS_CLKR, 0x30A3); */
		/* if(!pDevice->RegUsbSS) */
		/* 2011/01/26 MH SD4 Scott suggest to fix UNC-B cut bug. */
		rtw_write16(padapter, REG_SYS_CLKR, 0x70A3);  /* modify to 0x70A3 by Scott. */
		rtw_write8(padapter, REG_AFE_PLL_CTRL, 0x80);
		rtw_write16(padapter, REG_AFE_XTAL_CTRL, 0x880F);
		/* if(!pDevice->RegUsbSS) */
		rtw_write8(padapter, REG_SYS_ISO_CTRL, 0xF9);
	} else {
		/* Disable all RF/BB power */
		rtw_write8(padapter, REG_RF_CTRL, 0x00);
	}

}

void _ResetDigitalProcedure1(PADAPTER padapter, BOOLEAN bWithoutHWSM)
{
	_ResetDigitalProcedure1_8723D(padapter, bWithoutHWSM);
}

void _ResetDigitalProcedure2(PADAPTER padapter)
{
	/* HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(padapter);
	*****************************
	k.	SYS_FUNC_EN 0x03[7:0] = 0x44
	l.	SYS_CLKR 0x08[15:0] = 0x3083
	m.	SYS_ISO_CTRL 0x01[7:0] = 0x83
	******************************/
	/* rtw_write8(padapter, REG_SYS_FUNC_EN+1, 0x44); //marked by Scott. */
	/* 2011/01/26 MH SD4 Scott suggest to fix UNC-B cut bug. */
	rtw_write16(padapter, REG_SYS_CLKR, 0x70a3); /* modify to 0x70a3 by Scott. */
	rtw_write8(padapter, REG_SYS_ISO_CTRL + 1, 0x82); /* modify to 0x82 by Scott. */
}

void _DisableAnalog(PADAPTER padapter, BOOLEAN bWithoutHWSM)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	u16 value16 = 0;
	u8 value8 = 0;


	if (bWithoutHWSM) {
		/*****************************
		n.	LDOA15_CTRL 0x20[7:0] = 0x04
		o.	LDOV12D_CTRL 0x21[7:0] = 0x54
		r.	When driver call disable, the ASIC will turn off remaining clock automatically
		******************************/

		rtw_write8(padapter, REG_LDOA15_CTRL, 0x04);
		/* rtw_write8(padapter, REG_LDOV12D_CTRL, 0x54); */

		value8 = rtw_read8(padapter, REG_LDOV12D_CTRL);
		value8 &= (~LDV12_EN);
		rtw_write8(padapter, REG_LDOV12D_CTRL, value8);
	}

	/*****************************
	h.	SPS0_CTRL 0x11[7:0] = 0x23
	i.	APS_FSMCO 0x04[15:0] = 0x4802
	******************************/
	value8 = 0x23;

	rtw_write8(padapter, REG_SPS0_CTRL, value8);

	if (bWithoutHWSM) {
		/* value16 |= (APDM_HOST | AFSM_HSUS |PFM_ALDN); */
		/* 2010/08/31 According to Filen description, we need to use HW to shut down 8051 automatically. */
		/* Because suspend operation need the asistance of 8051 to wait for 3ms. */
		value16 |= (APDM_HOST | AFSM_HSUS | PFM_ALDN);
	} else
		value16 |= (APDM_HOST | AFSM_HSUS | PFM_ALDN);

	rtw_write16(padapter, REG_APS_FSMCO, value16);/* 0x4802 */

	rtw_write8(padapter, REG_RSV_CTRL, 0x0e);

#if 0
	/* tynli_test for suspend mode. */
	if (!bWithoutHWSM)
		rtw_write8(padapter, 0xfe10, 0x19);
#endif

}

/* HW Auto state machine */
s32 CardDisableHWSM(PADAPTER padapter, u8 resetMCU)
{
	int rtStatus = _SUCCESS;


	if (RTW_CANNOT_RUN(padapter))
		return rtStatus;

	/* ==== RF Off Sequence ==== */
	_DisableRFAFEAndResetBB(padapter);

	/* ==== Reset digital sequence   ====== */
	_ResetDigitalProcedure1(padapter, _FALSE);

	/* ==== Pull GPIO PIN to balance level and LED control ====== */
	_DisableGPIO(padapter);

	/* ==== Disable analog sequence === */
	_DisableAnalog(padapter, _FALSE);


	return rtStatus;
}

/* without HW Auto state machine */
s32 CardDisableWithoutHWSM(PADAPTER padapter)
{
	s32 rtStatus = _SUCCESS;


	if (RTW_CANNOT_RUN(padapter))
		return rtStatus;


	/* ==== RF Off Sequence ==== */
	_DisableRFAFEAndResetBB(padapter);

	/* ==== Reset digital sequence   ====== */
	_ResetDigitalProcedure1(padapter, _TRUE);

	/* ==== Pull GPIO PIN to balance level and LED control ====== */
	_DisableGPIO(padapter);

	/* ==== Reset digital sequence   ====== */
	_ResetDigitalProcedure2(padapter);

	/* ==== Disable analog sequence === */
	_DisableAnalog(padapter, _TRUE);

	return rtStatus;
}
#endif /* CONFIG_USB_HCI || CONFIG_SDIO_HCI || CONFIG_GSPI_HCI */

void
Hal_InitPGData(
	PADAPTER	padapter,
	u8			*PROMContent)
{

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u32			i;
	u16			value16;

	if (_FALSE == pHalData->bautoload_fail_flag) {
		/* autoload OK.
		*		if (IS_BOOT_FROM_EEPROM(padapter)) */
		if (_TRUE == pHalData->EepromOrEfuse) {
			/* Read all Content from EEPROM or EFUSE. */
			for (i = 0; i < HWSET_MAX_SIZE_8723D; i += 2) {
				/*				value16 = EF2Byte(ReadEEprom(pAdapter, (u16) (i>>1)));
				 *				*((u16*)(&PROMContent[i])) = value16; */
			}
		} else {
			/* Read EFUSE real map to shadow. */
			EFUSE_ShadowMapUpdate(padapter, EFUSE_WIFI, _FALSE);
			_rtw_memcpy((void *)PROMContent, (void *)pHalData->efuse_eeprom_data, HWSET_MAX_SIZE_8723D);
		}
	} else {
		/* autoload fail */
		/*		pHalData->AutoloadFailFlag = _TRUE; */
		/* update to default value 0xFF */
		if (_FALSE == pHalData->EepromOrEfuse)
			EFUSE_ShadowMapUpdate(padapter, EFUSE_WIFI, _FALSE);
		_rtw_memcpy((void *)PROMContent, (void *)pHalData->efuse_eeprom_data, HWSET_MAX_SIZE_8723D);
	}

#ifdef CONFIG_EFUSE_CONFIG_FILE
	if (check_phy_efuse_tx_power_info_valid(padapter) == _FALSE) {
		if (Hal_readPGDataFromConfigFile(padapter) != _SUCCESS)
			RTW_ERR("invalid phy efuse and read from file fail, will use driver default!!\n");
	}
#endif
}

void
Hal_EfuseParseIDCode(
		PADAPTER	padapter,
		u8			*hwinfo
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u16			EEPROMId;


	/* Checl 0x8129 again for making sure autoload status!! */
	EEPROMId = le16_to_cpu(*((u16 *)hwinfo));
	if (EEPROMId != RTL_EEPROM_ID) {
		RTW_INFO("EEPROM ID(%#x) is invalid!!\n", EEPROMId);
		pHalData->bautoload_fail_flag = _TRUE;
	} else
		pHalData->bautoload_fail_flag = _FALSE;

}
void
Hal_EfuseParseTxPowerInfo_8723D(
		PADAPTER		padapter,
		u8			*PROMContent,
		BOOLEAN			AutoLoadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	pHalData->txpwr_pg_mode = TXPWR_PG_WITH_PWR_IDX;

	/* 2010/10/19 MH Add Regulator recognize for CU. */
	if (!AutoLoadFail) {
		pHalData->EEPROMRegulatory = (PROMContent[EEPROM_RF_BOARD_OPTION_8723D] & 0x7);	/* bit0~2 */
		if (PROMContent[EEPROM_RF_BOARD_OPTION_8723D] == 0xFF)
			pHalData->EEPROMRegulatory = (EEPROM_DEFAULT_BOARD_OPTION & 0x7);	/* bit0~2 */
	} else
		pHalData->EEPROMRegulatory = 0;
}

void
Hal_EfuseParseBoardType_8723D(
		PADAPTER	Adapter,
		u8			*PROMContent,
		BOOLEAN		AutoloadFail
)
{


	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	if (!AutoloadFail) {
		pHalData->InterfaceSel = (PROMContent[EEPROM_RF_BOARD_OPTION_8723D] & 0xE0) >> 5;
		if (PROMContent[EEPROM_RF_BOARD_OPTION_8723D] == 0xFF)
			pHalData->InterfaceSel = (EEPROM_DEFAULT_BOARD_OPTION & 0xE0) >> 5;
	} else
		pHalData->InterfaceSel = 0;

}

void
Hal_EfuseParseBTCoexistInfo_8723D(
		PADAPTER padapter,
		u8 *hwinfo,
		BOOLEAN AutoLoadFail
)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u8 tempval;
	u32 tmpu4;

	if (!AutoLoadFail) {
		tmpu4 = rtw_read32(padapter, REG_MULTI_FUNC_CTRL);
		if (tmpu4 & BT_FUNC_EN)
			pHalData->EEPROMBluetoothCoexist = _TRUE;
		else
			pHalData->EEPROMBluetoothCoexist = _FALSE;

		pHalData->EEPROMBluetoothType = BT_RTL8723D;

		tempval = hwinfo[EEPROM_RF_BT_SETTING_8723D];
		if (tempval != 0xFF) {
			/* 0:Ant_x2, 1:Ant_x1 */
			pHalData->EEPROMBluetoothAntNum = tempval & BIT(0);
			/*
			 * EFUSE_0xC3[6] == 0, Wi-Fi at BTGS1(Main, Ant2) - RF_PATH_A (default)
			 * EFUSE_0xC3[6] == 1, Wi-Fi at BTGS0( Aux, Ant1) - RF_PATH_B
			 */
			pHalData->ant_path = (tempval & BIT(6)) ? RF_PATH_B : RF_PATH_A;
		} else {
			pHalData->EEPROMBluetoothAntNum = Ant_x1;
			pHalData->ant_path = RF_PATH_A;
		}
	} else {
		if (padapter->registrypriv.mp_mode == 1)
			pHalData->EEPROMBluetoothCoexist = _TRUE;
		else
			pHalData->EEPROMBluetoothCoexist = _FALSE;

		pHalData->EEPROMBluetoothType = BT_RTL8723D;
		pHalData->EEPROMBluetoothAntNum = Ant_x1;
		pHalData->ant_path = RF_PATH_A;
	}

#ifdef CONFIG_BT_COEXIST
	if (padapter->registrypriv.ant_num > 0) {
		RTW_INFO("%s: Apply driver defined antenna number(%d) to replace origin(%d)\n"
			 , __func__
			 , padapter->registrypriv.ant_num
			 , pHalData->EEPROMBluetoothAntNum == Ant_x2 ? 2 : 1);

		switch (padapter->registrypriv.ant_num) {
		case 1:
			pHalData->EEPROMBluetoothAntNum = Ant_x1;
			break;
		case 2:
			pHalData->EEPROMBluetoothAntNum = Ant_x2;
			break;
		default:
			RTW_INFO("%s: Discard invalid driver defined antenna number(%d)!\n"
				 , __func__, padapter->registrypriv.ant_num);
			break;
		}
	}
#endif /* CONFIG_BT_COEXIST */

	RTW_INFO("%s: %s BT-coex, ant_num=%d\n"
		 , __func__
		, pHalData->EEPROMBluetoothCoexist == _TRUE ? "Enable" : "Disable"
		 , pHalData->EEPROMBluetoothAntNum == Ant_x2 ? 2 : 1);
}

void
Hal_EfuseParseEEPROMVer_8723D(
		PADAPTER		padapter,
		u8			*hwinfo,
		BOOLEAN			AutoLoadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if (!AutoLoadFail)
		pHalData->EEPROMVersion = hwinfo[EEPROM_VERSION_8723D];
	else
		pHalData->EEPROMVersion = 1;
}

void
Hal_EfuseParseVoltage_8723D(
		PADAPTER		pAdapter,
		u8			*hwinfo,
		BOOLEAN	AutoLoadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	/* _rtw_memcpy(pHalData->adjuseVoltageVal, &hwinfo[EEPROM_Voltage_ADDR_8723D], 1); */
	RTW_INFO("%s hwinfo[EEPROM_Voltage_ADDR_8723D] =%02x\n", __func__, hwinfo[EEPROM_Voltage_ADDR_8723D]);
	pHalData->adjuseVoltageVal = (hwinfo[EEPROM_Voltage_ADDR_8723D] & 0xf0) >> 4;
	RTW_INFO("%s pHalData->adjuseVoltageVal =%x\n", __func__, pHalData->adjuseVoltageVal);
}

void
Hal_EfuseParseChnlPlan_8723D(
		PADAPTER		padapter,
		u8			*hwinfo,
		BOOLEAN			AutoLoadFail
)
{
	hal_com_config_channel_plan(
		padapter
		, hwinfo ? &hwinfo[EEPROM_COUNTRY_CODE_8723D] : NULL
		, hwinfo ? hwinfo[EEPROM_ChannelPlan_8723D] : 0xFF
		, padapter->registrypriv.alpha2
		, padapter->registrypriv.channel_plan
		, AutoLoadFail
	);
}

void
Hal_EfuseParseCustomerID_8723D(
		PADAPTER		padapter,
		u8			*hwinfo,
		BOOLEAN			AutoLoadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if (!AutoLoadFail)
		pHalData->EEPROMCustomerID = hwinfo[EEPROM_CustomID_8723D];
	else
		pHalData->EEPROMCustomerID = 0;
}

void
Hal_EfuseParseAntennaDiversity_8723D(
		PADAPTER		pAdapter,
		u8				*hwinfo,
		BOOLEAN			AutoLoadFail
)
{
#ifdef CONFIG_ANTENNA_DIVERSITY
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(pAdapter);
	struct registry_priv	*registry_par = &pAdapter->registrypriv;
	u8				get_efuse_div_type;

	if (pHalData->EEPROMBluetoothAntNum == Ant_x1)
		pHalData->AntDivCfg = 0;
	else {
		if (registry_par->antdiv_cfg == 2) /* 0:OFF , 1:ON, 2:By EFUSE */
			pHalData->AntDivCfg = 1;
		else
			pHalData->AntDivCfg = registry_par->antdiv_cfg;
	}

	pHalData->TRxAntDivType = S0S1_TRX_HW_ANTDIV; /* it's the only diversity-type for 8723D*/
	pHalData->with_extenal_ant_switch = ((hwinfo[EEPROM_RF_BT_SETTING_8723D] & BIT7) >> 7);

	if (pHalData->AntDivCfg != 0) {

		get_efuse_div_type = hwinfo[EEPROM_RFE_OPTION_8723D];

		if (get_efuse_div_type == 0x11) {
			pHalData->b_fix_tx_ant = NO_FIX_TX_ANT;
		} else if (get_efuse_div_type == 0x13) {
			pHalData->b_fix_tx_ant = FIX_TX_AT_MAIN;/* RX diversity only*/
		} else
			pHalData->AntDivCfg = FALSE;
	}

	RTW_INFO("%s: AntDivCfg=%d, AntDivType=%d\n",
		 __FUNCTION__, pHalData->AntDivCfg, pHalData->TRxAntDivType);
#endif
}

void
Hal_EfuseParseXtal_8723D(
		PADAPTER		pAdapter,
		u8			*hwinfo,
		BOOLEAN		AutoLoadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if (!AutoLoadFail) {
		pHalData->crystal_cap = hwinfo[EEPROM_XTAL_8723D];
		if (pHalData->crystal_cap == 0xFF)
			pHalData->crystal_cap = EEPROM_Default_CrystalCap_8723D;	   /* what value should 8812 set? */
	} else
		pHalData->crystal_cap = EEPROM_Default_CrystalCap_8723D;
}


void
Hal_EfuseParseThermalMeter_8723D(
	PADAPTER	padapter,
	u8			*PROMContent,
	u8			AutoLoadFail
)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

	/* */
	/* ThermalMeter from EEPROM */
	/* */
	if (_FALSE == AutoLoadFail)
		pHalData->eeprom_thermal_meter = PROMContent[EEPROM_THERMAL_METER_8723D];
	else
		pHalData->eeprom_thermal_meter = EEPROM_Default_ThermalMeter_8723D;

	if ((pHalData->eeprom_thermal_meter == 0xff) || (_TRUE == AutoLoadFail)) {
		pHalData->odmpriv.rf_calibrate_info.is_apk_thermal_meter_ignore = _TRUE;
		pHalData->eeprom_thermal_meter = EEPROM_Default_ThermalMeter_8723D;
	}

}


void Hal_ReadRFGainOffset(
			PADAPTER		Adapter,
			u8			*PROMContent,
			BOOLEAN		AutoloadFail)
{
#ifdef CONFIG_RF_POWER_TRIM
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct kfree_data_t *kfree_data = &pHalData->kfree_data;
	u8 pg_pwrtrim = 0xFF, pg_therm = 0xFF;

	efuse_OneByteRead(Adapter,
		PPG_BB_GAIN_2G_TX_OFFSET_8723D, &pg_pwrtrim, _FALSE);
	efuse_OneByteRead(Adapter,
		PPG_THERMAL_OFFSET_8723D, &pg_therm, _FALSE);

	if (pg_pwrtrim != 0xFF) {
		kfree_data->bb_gain[BB_GAIN_2G][PPG_8723D_S1]
			= KFREE_BB_GAIN_2G_TX_OFFSET(pg_pwrtrim & PPG_BB_GAIN_2G_TX_OFFSET_MASK);
		kfree_data->bb_gain[BB_GAIN_2G][PPG_8723D_S0]
			= KFREE_BB_GAIN_2G_TXB_OFFSET(pg_pwrtrim & PPG_BB_GAIN_2G_TXB_OFFSET_MASK);
		kfree_data->flag |= KFREE_FLAG_ON;
	}

	if (pg_therm != 0xFF) {
		kfree_data->thermal
			= KFREE_THERMAL_OFFSET(pg_therm  & PPG_THERMAL_OFFSET_MASK);
		/* kfree_data->flag |= KFREE_FLAG_THERMAL_K_ON; */ /* Default disable thermel kfree by realsil Alan 20160428 */
	}

	if (kfree_data->flag & KFREE_FLAG_THERMAL_K_ON)
		pHalData->eeprom_thermal_meter += kfree_data->thermal;

	RTW_INFO("kfree Pwr Trim flag:%u\n", kfree_data->flag);
	if (kfree_data->flag & KFREE_FLAG_ON) {
		RTW_INFO("bb_gain(S1):%d\n", kfree_data->bb_gain[BB_GAIN_2G][PPG_8723D_S1]);
		RTW_INFO("bb_gain(S0):%d\n", kfree_data->bb_gain[BB_GAIN_2G][PPG_8723D_S0]);
	}
	if (kfree_data->flag & KFREE_FLAG_THERMAL_K_ON)
		RTW_INFO("thermal:%d\n", kfree_data->thermal);
#endif /*CONFIG_RF_POWER_TRIM */
}

u8
BWMapping_8723D(
		PADAPTER		Adapter,
		struct pkt_attrib	*pattrib
)
{
	u8	BWSettingOfDesc = 0;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);

	/* RTW_INFO("BWMapping pHalData->current_channel_bw %d, pattrib->bwmode %d\n",pHalData->current_channel_bw,pattrib->bwmode); */

	if (pHalData->current_channel_bw == CHANNEL_WIDTH_80) {
		if (pattrib->bwmode == CHANNEL_WIDTH_80)
			BWSettingOfDesc = 2;
		else if (pattrib->bwmode == CHANNEL_WIDTH_40)
			BWSettingOfDesc = 1;
		else
			BWSettingOfDesc = 0;
	} else if (pHalData->current_channel_bw == CHANNEL_WIDTH_40) {
		if ((pattrib->bwmode == CHANNEL_WIDTH_40) || (pattrib->bwmode == CHANNEL_WIDTH_80))
			BWSettingOfDesc = 1;
		else
			BWSettingOfDesc = 0;
	} else
		BWSettingOfDesc = 0;

	/* if(pTcb->bBTTxPacket) */
	/*	BWSettingOfDesc = 0; */

	return BWSettingOfDesc;
}

u8	SCMapping_8723D(PADAPTER Adapter, struct pkt_attrib *pattrib)
{
	u8	SCSettingOfDesc = 0;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);

	/* RTW_INFO("SCMapping: pHalData->current_channel_bw %d, pHalData->nCur80MhzPrimeSC %d, pHalData->nCur40MhzPrimeSC %d\n",pHalData->current_channel_bw,pHalData->nCur80MhzPrimeSC,pHalData->nCur40MhzPrimeSC); */

	if (pHalData->current_channel_bw == CHANNEL_WIDTH_80) {
		if (pattrib->bwmode == CHANNEL_WIDTH_80)
			SCSettingOfDesc = VHT_DATA_SC_DONOT_CARE;
		else if (pattrib->bwmode == CHANNEL_WIDTH_40) {
			if (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
				SCSettingOfDesc = VHT_DATA_SC_40_LOWER_OF_80MHZ;
			else if (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
				SCSettingOfDesc = VHT_DATA_SC_40_UPPER_OF_80MHZ;
			else
				RTW_INFO("SCMapping: DONOT CARE Mode Setting\n");
		} else {
			if ((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER))
				SCSettingOfDesc = VHT_DATA_SC_20_LOWEST_OF_80MHZ;
			else if ((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER))
				SCSettingOfDesc = VHT_DATA_SC_20_LOWER_OF_80MHZ;
			else if ((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER))
				SCSettingOfDesc = VHT_DATA_SC_20_UPPER_OF_80MHZ;
			else if ((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER))
				SCSettingOfDesc = VHT_DATA_SC_20_UPPERST_OF_80MHZ;
			else
				RTW_INFO("SCMapping: DONOT CARE Mode Setting\n");
		}
	} else if (pHalData->current_channel_bw == CHANNEL_WIDTH_40) {
		/* RTW_INFO("SCMapping: HT Case: pHalData->current_channel_bw %d, pHalData->nCur40MhzPrimeSC %d\n",pHalData->current_channel_bw,pHalData->nCur40MhzPrimeSC); */

		if (pattrib->bwmode == CHANNEL_WIDTH_40)
			SCSettingOfDesc = VHT_DATA_SC_DONOT_CARE;
		else if (pattrib->bwmode == CHANNEL_WIDTH_20) {
			if (pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
				SCSettingOfDesc = VHT_DATA_SC_20_UPPER_OF_80MHZ;
			else if (pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
				SCSettingOfDesc = VHT_DATA_SC_20_LOWER_OF_80MHZ;
			else
				SCSettingOfDesc = VHT_DATA_SC_DONOT_CARE;
		}
	} else
		SCSettingOfDesc = VHT_DATA_SC_DONOT_CARE;

	return SCSettingOfDesc;
}

void fill_txdesc_force_bmc_camid(struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	if ((pattrib->encrypt > 0) && (!pattrib->bswenc)
	    && (pattrib->bmc_camid != INVALID_SEC_MAC_CAM_ID)) {

		SET_TX_DESC_EN_DESC_ID_8723D(ptxdesc, 1);
		SET_TX_DESC_MACID_8723D(ptxdesc, pattrib->bmc_camid);
	}
}

void fill_txdesc_bmc_tx_rate(struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	SET_TX_DESC_USE_RATE_8723D(ptxdesc, 1);
	SET_TX_DESC_TX_RATE_8723D(ptxdesc, MRateToHwRate(pattrib->rate));
	SET_TX_DESC_DISABLE_FB_8723D(ptxdesc, 1);
}

static u8 fill_txdesc_sectype(struct pkt_attrib *pattrib)
{
	u8 sectype = 0;

	if ((pattrib->encrypt > 0) && !pattrib->bswenc) {
		switch (pattrib->encrypt) {
		/* SEC_TYPE */
		case _WEP40_:
		case _WEP104_:
		case _TKIP_:
		case _TKIP_WTMIC_:
			sectype = 1;
			break;

#ifdef CONFIG_WAPI_SUPPORT
		case _SMS4_:
			sectype = 2;
			break;
#endif
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

static void fill_txdesc_vcs_8723d(PADAPTER padapter, struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	/* RTW_INFO("cvs_mode=%d\n", pattrib->vcs_mode); */

	if (pattrib->vcs_mode) {
		switch (pattrib->vcs_mode) {
		case RTS_CTS:
			SET_TX_DESC_RTS_ENABLE_8723D(ptxdesc, 1);
			SET_TX_DESC_HW_RTS_ENABLE_8723D(ptxdesc, 1);
			break;

		case CTS_TO_SELF:
			SET_TX_DESC_CTS2SELF_8723D(ptxdesc, 1);
			break;

		case NONE_VCS:
		default:
			break;
		}

		SET_TX_DESC_RTS_RATE_8723D(ptxdesc, 8); /* RTS Rate=24M */
		SET_TX_DESC_RTS_RATE_FB_LIMIT_8723D(ptxdesc, 0xF);

		if (padapter->mlmeextpriv.mlmext_info.preamble_mode == PREAMBLE_SHORT)
			SET_TX_DESC_RTS_SHORT_8723D(ptxdesc, 1);

		/* Set RTS BW */
		if (pattrib->ht_en)
			SET_TX_DESC_RTS_SC_8723D(ptxdesc, SCMapping_8723D(padapter, pattrib));
	}
}

static void fill_txdesc_phy_8723d(PADAPTER padapter, struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	/* RTW_INFO("bwmode=%d, ch_off=%d\n", pattrib->bwmode, pattrib->ch_offset); */

	if (pattrib->ht_en) {
		SET_TX_DESC_DATA_BW_8723D(ptxdesc, BWMapping_8723D(padapter, pattrib));
		SET_TX_DESC_DATA_SC_8723D(ptxdesc, SCMapping_8723D(padapter, pattrib));
	}
}

static void rtl8723d_fill_default_txdesc(
	struct xmit_frame *pxmitframe,
	u8 *pbuf)
{
	PADAPTER padapter;
	HAL_DATA_TYPE *pHalData;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;
	struct pkt_attrib *pattrib;
	s32 bmcst;

	_rtw_memset(pbuf, 0, TXDESC_SIZE);

	padapter = pxmitframe->padapter;
	pHalData = GET_HAL_DATA(padapter);
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &(pmlmeext->mlmext_info);

	pattrib = &pxmitframe->attrib;
	bmcst = IS_MCAST(pattrib->ra);

	if (pHalData->rf_type == RF_1T1R)
		SET_TX_DESC_PATH_A_EN_8723D(pbuf, 1);

	if (pxmitframe->frame_tag == DATA_FRAMETAG) {
		u8 drv_userate = 0;

		SET_TX_DESC_MACID_8723D(pbuf, pattrib->mac_id);
		SET_TX_DESC_RATE_ID_8723D(pbuf, pattrib->raid);
		SET_TX_DESC_QUEUE_SEL_8723D(pbuf, pattrib->qsel);
		SET_TX_DESC_SEQ_8723D(pbuf, pattrib->seqnum);

		SET_TX_DESC_SEC_TYPE_8723D(pbuf, fill_txdesc_sectype(pattrib));

		if (bmcst)
			fill_txdesc_force_bmc_camid(pattrib, pbuf);

		fill_txdesc_vcs_8723d(padapter, pattrib, pbuf);

#ifdef CONFIG_P2P
		if (!rtw_p2p_chk_state(&padapter->wdinfo, P2P_STATE_NONE)) {
			if (pattrib->icmp_pkt == 1 && padapter->registrypriv.wifi_spec == 1)
				drv_userate = 1;
		}
#endif

		if ((pattrib->ether_type != 0x888e) &&
		    (pattrib->ether_type != 0x0806) &&
		    (pattrib->ether_type != 0x88B4) &&
		    (pattrib->dhcp_pkt != 1) &&
		    (drv_userate != 1)
#ifdef CONFIG_AUTO_AP_MODE
		    && (pattrib->pctrl != _TRUE)
#endif
		   ) {
			/* Non EAP & ARP & DHCP type data packet */

			if (pattrib->ampdu_en == _TRUE) {
				SET_TX_DESC_AGG_ENABLE_8723D(pbuf, 1);
				SET_TX_DESC_MAX_AGG_NUM_8723D(pbuf, 0x1F);
				SET_TX_DESC_AMPDU_DENSITY_8723D(pbuf, pattrib->ampdu_spacing);
			} else
				SET_TX_DESC_BK_8723D(pbuf, 1);

			fill_txdesc_phy_8723d(padapter, pattrib, pbuf);

			SET_TX_DESC_DATA_RATE_FB_LIMIT_8723D(pbuf, 0x1F);

			if (pHalData->fw_ractrl == _FALSE) {
				SET_TX_DESC_USE_RATE_8723D(pbuf, 1);

				if (pHalData->INIDATA_RATE[pattrib->mac_id] & BIT(7))
					SET_TX_DESC_DATA_SHORT_8723D(pbuf, 1);

				SET_TX_DESC_TX_RATE_8723D(pbuf, pHalData->INIDATA_RATE[pattrib->mac_id] & 0x7F);
			}
			if (bmcst)
				fill_txdesc_bmc_tx_rate(pattrib, pbuf);

			/* modify data rate by iwpriv */
			if (padapter->fix_rate != 0xFF) {
				SET_TX_DESC_USE_RATE_8723D(pbuf, 1);
				if (padapter->fix_rate & BIT(7))
					SET_TX_DESC_DATA_SHORT_8723D(pbuf, 1);
				SET_TX_DESC_TX_RATE_8723D(pbuf, padapter->fix_rate & 0x7F);
				if (!padapter->data_fb)
					SET_TX_DESC_DISABLE_FB_8723D(pbuf, 1);
			}

			if (pattrib->stbc)
				SET_TX_DESC_DATA_STBC_8723D(pbuf, 1);

#ifdef CONFIG_CMCC_TEST
			SET_TX_DESC_DATA_SHORT_8723D(pbuf, 1); /* use cck short premble */
#endif
		} else {
			/* EAP data packet and ARP packet. */
			/* Use the 1M data rate to send the EAP/ARP packet. */
			/* This will maybe make the handshake smooth. */

			SET_TX_DESC_BK_8723D(pbuf, 1);
			SET_TX_DESC_USE_RATE_8723D(pbuf, 1);
			if (pmlmeinfo->preamble_mode == PREAMBLE_SHORT)
				SET_TX_DESC_DATA_SHORT_8723D(pbuf, 1);
#ifdef CONFIG_IP_R_MONITOR
			if((pattrib->ether_type == ETH_P_ARP) &&
				(IsSupportedTxOFDM(padapter->registrypriv.wireless_mode))) 
				SET_TX_DESC_TX_RATE_8723D(pbuf, MRateToHwRate(IEEE80211_OFDM_RATE_6MB));
			 else
#endif/*CONFIG_IP_R_MONITOR*/
				SET_TX_DESC_TX_RATE_8723D(pbuf, MRateToHwRate(pmlmeext->tx_rate));

			RTW_INFO(FUNC_ADPT_FMT ": SP Packet(0x%04X) rate=0x%x SeqNum = %d\n",
				FUNC_ADPT_ARG(padapter), pattrib->ether_type, MRateToHwRate(pmlmeext->tx_rate), pattrib->seqnum);
		}

#if defined(CONFIG_USB_TX_AGGREGATION) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		SET_TX_DESC_USB_TXAGG_NUM_8723D(pbuf, pxmitframe->agg_num);
#endif

#ifdef CONFIG_TDLS
#ifdef CONFIG_XMIT_ACK
		/* CCX-TXRPT ack for xmit mgmt frames. */
		if (pxmitframe->ack_report) {
#ifdef DBG_CCX
			RTW_INFO("%s set spe_rpt\n", __func__);
#endif
			SET_TX_DESC_CCX_8723D(pbuf, 1);
			SET_TX_DESC_SW_DEFINE_8723D(pbuf, (u8)(GET_PRIMARY_ADAPTER(padapter)->xmitpriv.seq_no));
		}
#endif /* CONFIG_XMIT_ACK */
#endif
	} else if (pxmitframe->frame_tag == MGNT_FRAMETAG) {

		SET_TX_DESC_MACID_8723D(pbuf, pattrib->mac_id);
		SET_TX_DESC_QUEUE_SEL_8723D(pbuf, pattrib->qsel);
		SET_TX_DESC_RATE_ID_8723D(pbuf, pattrib->raid);
		SET_TX_DESC_SEQ_8723D(pbuf, pattrib->seqnum);
		SET_TX_DESC_USE_RATE_8723D(pbuf, 1);

		SET_TX_DESC_MBSSID_8723D(pbuf, pattrib->mbssid & 0xF);

		SET_TX_DESC_RETRY_LIMIT_ENABLE_8723D(pbuf, 1);
#ifdef CONFIG_CUSTOMER_ALIBABA_GENERAL
		SET_TX_DESC_DATA_RETRY_LIMIT_8723D(pbuf, 48);
#else
		if (pattrib->retry_ctrl == _TRUE)
			SET_TX_DESC_DATA_RETRY_LIMIT_8723D(pbuf, 6);
		else
			SET_TX_DESC_DATA_RETRY_LIMIT_8723D(pbuf, 12);
#endif
		SET_TX_DESC_TX_RATE_8723D(pbuf, MRateToHwRate(pattrib->rate));

#ifdef CONFIG_XMIT_ACK
		/* CCX-TXRPT ack for xmit mgmt frames. */
		if (pxmitframe->ack_report) {
#ifdef DBG_CCX
			RTW_INFO("%s set spe_rpt\n", __FUNCTION__);
#endif
			SET_TX_DESC_CCX_8723D(pbuf, 1);
			SET_TX_DESC_SW_DEFINE_8723D(pbuf, (u8)(GET_PRIMARY_ADAPTER(padapter)->xmitpriv.seq_no));
		}
#endif /* CONFIG_XMIT_ACK */
	} else if (pxmitframe->frame_tag == TXAGG_FRAMETAG) {
	}
#ifdef CONFIG_MP_INCLUDED
	else if (pxmitframe->frame_tag == MP_FRAMETAG) {
		fill_txdesc_for_mp(padapter, pbuf);
	}
#endif
	else {

		SET_TX_DESC_MACID_8723D(pbuf, pattrib->mac_id);
		SET_TX_DESC_RATE_ID_8723D(pbuf, pattrib->raid);
		SET_TX_DESC_QUEUE_SEL_8723D(pbuf, pattrib->qsel);
		SET_TX_DESC_SEQ_8723D(pbuf, pattrib->seqnum);
		SET_TX_DESC_USE_RATE_8723D(pbuf, 1);
		SET_TX_DESC_TX_RATE_8723D(pbuf, MRateToHwRate(pmlmeext->tx_rate));
	}

	SET_TX_DESC_PKT_SIZE_8723D(pbuf, pattrib->last_txcmdsz);

	{
		u8 pkt_offset, offset;

		pkt_offset = 0;
		offset = TXDESC_SIZE;
#ifdef CONFIG_USB_HCI
		pkt_offset = pxmitframe->pkt_offset;
		offset += (pxmitframe->pkt_offset >> 3);
#endif /* CONFIG_USB_HCI */

#ifdef CONFIG_TX_EARLY_MODE
		if (pxmitframe->frame_tag == DATA_FRAMETAG) {
			pkt_offset = 1;
			offset += EARLY_MODE_INFO_SIZE;
		}
#endif /* CONFIG_TX_EARLY_MODE */

		SET_TX_DESC_PKT_OFFSET_8723D(pbuf, pkt_offset);
		SET_TX_DESC_OFFSET_8723D(pbuf, offset);
	}

	if (bmcst)
		SET_TX_DESC_BMC_8723D(pbuf, 1);

	/* 2009.11.05. tynli_test. Suggested by SD4 Filen for FW LPS. */
	/* (1) The sequence number of each non-Qos frame / broadcast / multicast / */
	/* mgnt frame should be controlled by Hw because Fw will also send null data */
	/* which we cannot control when Fw LPS enable. */
	/* --> default enable non-Qos data sequense number. 2010.06.23. by tynli. */
	/* (2) Enable HW SEQ control for beacon packet, because we use Hw beacon. */
	/* (3) Use HW Qos SEQ to control the seq num of Ext port non-Qos packets. */
	/* 2010.06.23. Added by tynli. */
	if (!pattrib->qos_en)
		SET_TX_DESC_HWSEQ_EN_8723D(pbuf, 1);

#ifdef CONFIG_ANTENNA_DIVERSITY
	if (!bmcst && pattrib->psta)
		odm_set_tx_ant_by_tx_info(adapter_to_phydm(padapter), pbuf, pattrib->psta->cmn.mac_id);
#endif
}

/*
 *	Description:
 *
 *	Parameters:
 *		pxmitframe	xmitframe
 *		pbuf		where to fill tx desc
 */
void rtl8723d_update_txdesc(struct xmit_frame *pxmitframe, u8 *pbuf)
{
	rtl8723d_fill_default_txdesc(pxmitframe, pbuf);

#if defined(CONFIG_USB_HCI)
	rtl8723d_cal_txdesc_chksum((struct tx_desc *)pbuf);
#endif
}

static void hw_var_set_monitor(PADAPTER adapter, u8 variable, u8 *val)
{
#ifdef CONFIG_WIFI_MONITOR
	u32 tmp_32bit;
	struct net_device *ndev = adapter->pnetdev;
	struct mon_reg_backup *mon = &GET_HAL_DATA(adapter)->mon_backup;

	mon->known_rcr = 1;
	rtw_hal_get_hwreg(adapter, HW_VAR_RCR, (u8 *)& mon->rcr);

	/* Receive all type */
	tmp_32bit = RCR_AAP | RCR_APP_PHYST_RXFF;

	if (ndev->type == ARPHRD_IEEE80211_RADIOTAP) {
		/* Append FCS */
		tmp_32bit |= RCR_APPFCS;
	}

	rtw_hal_set_hwreg(adapter, HW_VAR_RCR, (u8 *)& tmp_32bit);

	/* Receive all data frames */
	mon->known_rxfilter = 1;
	mon->rxfilter0 = rtw_read16(adapter, REG_RXFLTMAP0_8723D);
	mon->rxfilter1 = rtw_read16(adapter, REG_RXFLTMAP1_8723D);
	mon->rxfilter2 = rtw_read16(adapter, REG_RXFLTMAP2_8723D);
	rtw_write16(adapter, REG_RXFLTMAP0_8723D, 0xFFFF);
	rtw_write16(adapter, REG_RXFLTMAP1_8723D, 0xFFFF);
	rtw_write16(adapter, REG_RXFLTMAP2_8723D, 0xFFFF);
#endif /* CONFIG_WIFI_MONITOR */
}

static void hw_var_set_opmode(PADAPTER padapter, u8 variable, u8 *val)
{
	u8 val8;
	u8 mode = *((u8 *)val);
	static u8 isMonitor = _FALSE;

	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(padapter);

	if (isMonitor == _TRUE) {
#ifdef CONFIG_WIFI_MONITOR
		struct mon_reg_backup *backup = &GET_HAL_DATA(padapter)->mon_backup;

		if (backup->known_rcr) {
			backup->known_rcr = 0;
			rtw_hal_set_hwreg(padapter, HW_VAR_RCR, (u8 *)&backup->rcr);
			rtw_hal_rcr_set_chk_bssid(padapter, MLME_ACTION_NONE);
		}
		if (backup->known_rxfilter) {
			backup->known_rxfilter = 0;
			rtw_write16(padapter, REG_RXFLTMAP0_8723D, backup->rxfilter0);
			rtw_write16(padapter, REG_RXFLTMAP1_8723D, backup->rxfilter1);
			rtw_write16(padapter, REG_RXFLTMAP2_8723D, backup->rxfilter2);
		}
#endif /* CONFIG_WIFI_MONITOR */
		isMonitor = _FALSE;
	}

	if (mode == _HW_STATE_MONITOR_) {
		isMonitor = _TRUE;
		/* set net_type */
		Set_MSR(padapter, _HW_STATE_NOLINK_);

		hw_var_set_monitor(padapter, variable, val);
		return;
	}
	/* set mac addr to mac register */
	rtw_hal_set_hwreg(padapter, HW_VAR_MAC_ADDR,
			  adapter_mac_addr(padapter));

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->hw_port == HW_PORT1) {
		/* disable Port1 TSF update */
		rtw_iface_disable_tsf_update(padapter);

		Set_MSR(padapter, mode);

		RTW_INFO("#### %s()-%d hw_port(%d) mode=%d ####\n",
			 __func__, __LINE__, padapter->hw_port, mode);

		if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_)) {
			if (!rtw_mi_get_ap_num(padapter) && !rtw_mi_get_mesh_num(padapter)) {
				StopTxBeacon(padapter);
#ifdef CONFIG_PCI_HCI
				UpdateInterruptMask8723DE(padapter, 0, 0, RT_BCN_INT_MASKS, 0);
#else /* !CONFIG_PCI_HCI */
#ifdef CONFIG_INTERRUPT_BASED_TXBCN

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
				rtw_write8(padapter, REG_DRVERLYINT, 0x05);/* restore early int time to 5ms */
				UpdateInterruptMask8723DU(padapter, _TRUE, 0, IMR_BCNDMAINT0_8723D);
#endif /* CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
				UpdateInterruptMask8723DU(padapter, _TRUE , 0, (IMR_TXBCN0ERR_8723D | IMR_TXBCN0OK_8723D));
#endif /* CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR */

#endif /* CONFIG_INTERRUPT_BASED_TXBCN */
#endif /* !CONFIG_PCI_HCI */
			}

			/* disable atim wnd */
			rtw_write8(padapter, REG_BCN_CTRL_1, DIS_TSF_UDT | DIS_ATIM | EN_BCN_FUNCTION);
		} else if (mode == _HW_STATE_ADHOC_) {
			ResumeTxBeacon(padapter);
			rtw_write8(padapter, REG_BCN_CTRL_1, DIS_TSF_UDT | EN_BCN_FUNCTION | DIS_BCNQ_SUB);
		} else if (mode == _HW_STATE_AP_) {
#ifdef CONFIG_PCI_HCI
			UpdateInterruptMask8723DE(padapter, RT_BCN_INT_MASKS, 0, 0, 0);
#else /* !CONFIG_PCI_HCI */
#ifdef CONFIG_INTERRUPT_BASED_TXBCN

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			UpdateInterruptMask8723DU(padapter, _TRUE, IMR_BCNDMAINT0_8723D, 0);
#endif /* CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
			UpdateInterruptMask8723DU(padapter, _TRUE, (IMR_TXBCN0ERR_8723D | IMR_TXBCN0OK_8723D), 0);
#endif /* CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR */

#endif /* CONFIG_INTERRUPT_BASED_TXBCN */
#endif /* !CONFIG_PCI_HCI */

			rtw_write8(padapter, REG_BCN_CTRL_1, DIS_TSF_UDT | DIS_BCNQ_SUB);

			/* enable to rx data frame*/
			rtw_write16(padapter, REG_RXFLTMAP2, 0xFFFF);
			/* enable to rx ps-poll */
			rtw_write16(padapter, REG_RXFLTMAP1, 0x0400);

			/* Beacon Control related register for first time */
			rtw_write8(padapter, REG_BCNDMATIM, 0x02); /* 2ms */

			/* rtw_write8(padapter, REG_BCN_MAX_ERR, 0xFF); */
			rtw_write8(padapter, REG_ATIMWND_1, 0x0c); /* 13ms for port1 */

			rtw_write16(padapter, REG_TSFTR_SYN_OFFSET, 0x7fff);/* +32767 (~32ms) */

			/* reset TSF2 */
			rtw_write8(padapter, REG_DUAL_TSF_RST, BIT(1));

			/* enable BCN1 Function for if2 */
			/* don't enable update TSF1 for if2 (due to TSF update when beacon/probe rsp are received) */
			rtw_write8(padapter, REG_BCN_CTRL_1, (DIS_TSF_UDT | EN_BCN_FUNCTION | EN_TXBCN_RPT | DIS_BCNQ_SUB));

			/* SW_BCN_SEL - Port1 */
			/* rtw_write8(Adapter, REG_DWBCN1_CTRL_8192E+2, rtw_read8(Adapter, REG_DWBCN1_CTRL_8192E+2)|BIT4); */
			rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);

			/* select BCN on port 1 */
			rtw_write8(padapter, REG_CCK_CHECK_8723D,
				(rtw_read8(padapter, REG_CCK_CHECK_8723D) | BIT_BCN_PORT_SEL));

			/* BCN1 TSF will sync to BCN0 TSF with offset(0x518) if if1_sta linked */
			/* rtw_write8(padapter, REG_BCN_CTRL_1, rtw_read8(padapter, REG_BCN_CTRL_1)|BIT(5)); */
			/* rtw_write8(padapter, REG_DUAL_TSF_RST, BIT(3)); */

			/* dis BCN0 ATIM  WND if if1 is station */
			rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL) | DIS_ATIM);

#ifdef CONFIG_TSF_RESET_OFFLOAD
			/* Reset TSF for STA+AP concurrent mode */
			if (DEV_STA_LD_NUM(adapter_to_dvobj(padapter))) {
				if (rtw_hal_reset_tsf(padapter, HW_PORT1) == _FAIL)
					RTW_INFO("ERROR! %s()-%d: Reset port1 TSF fail\n",
						 __FUNCTION__, __LINE__);
			}
#endif /* CONFIG_TSF_RESET_OFFLOAD */
		}
	} else /* else for port0 */
#endif /* CONFIG_CONCURRENT_MODE */
	{
#ifdef CONFIG_MI_WITH_MBSSID_CAM /*For Port0 - MBSS CAM*/
		hw_var_set_opmode_mbid(padapter, mode);
#else
		/* disable Port0 TSF update */
		rtw_iface_disable_tsf_update(padapter);

		/* set net_type */
		Set_MSR(padapter, mode);
		RTW_INFO("#### %s() -%d hw_port(0) mode = %d ####\n",
			 __func__, __LINE__, mode);

		if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_)) {
#ifdef CONFIG_CONCURRENT_MODE
			if (!rtw_mi_get_ap_num(padapter) && !rtw_mi_get_mesh_num(padapter)) {
#else
			{
#endif /* CONFIG_CONCURRENT_MODE */
				StopTxBeacon(padapter);
#ifdef CONFIG_PCI_HCI
				UpdateInterruptMask8723DE(padapter, 0, 0, RT_BCN_INT_MASKS, 0);
#else /* !CONFIG_PCI_HCI */
#ifdef CONFIG_INTERRUPT_BASED_TXBCN
#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
				rtw_write8(padapter, REG_DRVERLYINT, 0x05); /* restore early int time to 5ms */
				UpdateInterruptMask8812AU(padapter, _TRUE, 0, IMR_BCNDMAINT0_8723D);
#endif /* CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
				UpdateInterruptMask8812AU(padapter, _TRUE , 0, (IMR_TXBCN0ERR_8723D | IMR_TXBCN0OK_8723D));
#endif /* CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR */

#endif /* CONFIG_INTERRUPT_BASED_TXBCN */
#endif /* !CONFIG_PCI_HCI */
			}

			/* disable atim wnd */
			rtw_write8(padapter, REG_BCN_CTRL, DIS_TSF_UDT | EN_BCN_FUNCTION | DIS_ATIM);
			/* rtw_write8(padapter,REG_BCN_CTRL, DIS_TSF_UDT | EN_BCN_FUNCTION); */
		} else if (mode == _HW_STATE_ADHOC_) {
			ResumeTxBeacon(padapter);
			rtw_write8(padapter, REG_BCN_CTRL, DIS_TSF_UDT | EN_BCN_FUNCTION | DIS_BCNQ_SUB);
		} else if (mode == _HW_STATE_AP_) {
#ifdef CONFIG_PCI_HCI
			UpdateInterruptMask8723DE(padapter, RT_BCN_INT_MASKS, 0, 0, 0);
#else /* !CONFIG_PCI_HCI */
#ifdef CONFIG_INTERRUPT_BASED_TXBCN
#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			UpdateInterruptMask8723DU(padapter, _TRUE , IMR_BCNDMAINT0_8723D, 0);
#endif /* CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
			UpdateInterruptMask8723DU(padapter, _TRUE , (IMR_TXBCN0ERR_8723D | IMR_TXBCN0OK_8723D), 0);
#endif /* CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR */

#endif /* CONFIG_INTERRUPT_BASED_TXBCN */
#endif

			rtw_write8(padapter, REG_BCN_CTRL, DIS_TSF_UDT | DIS_BCNQ_SUB);

			/* enable to rx data frame */
			rtw_write16(padapter, REG_RXFLTMAP2, 0xFFFF);
			/* enable to rx ps-poll */
			rtw_write16(padapter, REG_RXFLTMAP1, 0x0400);

			/* Beacon Control related register for first time */
			rtw_write8(padapter, REG_BCNDMATIM, 0x02); /* 2ms */

			/* rtw_write8(padapter, REG_BCN_MAX_ERR, 0xFF); */
			rtw_write8(padapter, REG_ATIMWND, 0x0c); /* 13ms */

			rtw_write16(padapter, REG_TSFTR_SYN_OFFSET, 0x7fff);/* +32767 (~32ms) */

			/* reset TSF */
			rtw_write8(padapter, REG_DUAL_TSF_RST, BIT(0));

			/* enable BCN0 Function for if1 */
			/* don't enable update TSF0 for if1 (due to TSF update when beacon/probe rsp are received) */
			rtw_write8(padapter, REG_BCN_CTRL, (DIS_TSF_UDT | EN_BCN_FUNCTION | EN_TXBCN_RPT | DIS_BCNQ_SUB));

			/* SW_BCN_SEL - Port0 */
			/* rtw_write8(Adapter, REG_DWBCN1_CTRL_8192E+2, rtw_read8(Adapter, REG_DWBCN1_CTRL_8192E+2) & ~BIT4); */
			rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);

			/* select BCN on port 0 */
			rtw_write8(padapter, REG_CCK_CHECK_8723D,
				(rtw_read8(padapter, REG_CCK_CHECK_8723D) & ~BIT_BCN_PORT_SEL));

			/* dis BCN1 ATIM  WND if if2 is station */
			val8 = rtw_read8(padapter, REG_BCN_CTRL_1);
			val8 |= DIS_ATIM;
			rtw_write8(padapter, REG_BCN_CTRL_1, val8);
#ifdef CONFIG_TSF_RESET_OFFLOAD
			/* Reset TSF for STA+AP concurrent mode */
			if (DEV_STA_LD_NUM(adapter_to_dvobj(padapter))) {
				if (rtw_hal_reset_tsf(padapter, HW_PORT0) == _FAIL)
					RTW_INFO("ERROR! %s()-%d: Reset port0 TSF fail\n",
						 __FUNCTION__, __LINE__);
			}
#endif /* CONFIG_TSF_RESET_OFFLOAD */
		}
#endif /* !CONFIG_MI_WITH_MBSSID_CAM */
	}
}

void CCX_FwC2HTxRpt_8723d(PADAPTER padapter, u8 *pdata, u8 len)
{
	u8 seq_no;

#define	GET_8723D_C2H_TX_RPT_LIFE_TIME_OVER(_Header)	LE_BITS_TO_1BYTE((_Header + 0), 6, 1)
#define	GET_8723D_C2H_TX_RPT_RETRY_OVER(_Header)	LE_BITS_TO_1BYTE((_Header + 0), 7, 1)

	/* RTW_INFO("%s, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n", __func__,  */
	/**pdata, *(pdata+1), *(pdata+2), *(pdata+3), *(pdata+4), *(pdata+5), *(pdata+6), *(pdata+7)); */

	seq_no = *(pdata + 6);

#ifdef CONFIG_XMIT_ACK
	if (GET_8723D_C2H_TX_RPT_RETRY_OVER(pdata) | GET_8723D_C2H_TX_RPT_LIFE_TIME_OVER(pdata))
		rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_CCX_PKT_FAIL);
	/*
	else if(seq_no != padapter->xmitpriv.seq_no) {
		RTW_INFO("tx_seq_no=%d, rpt_seq_no=%d\n", padapter->xmitpriv.seq_no, seq_no);
		rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_CCX_PKT_FAIL);
	}
	*/
	else
		rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_SUCCESS);
#endif
}

s32 c2h_handler_8723d(_adapter *adapter, u8 id, u8 seq, u8 plen, u8 *payload)
{
	s32 ret = _SUCCESS;

	switch (id) {
	case C2H_CCX_TX_RPT:
		CCX_FwC2HTxRpt_8723d(adapter, payload, plen);
		break;
	default:
		ret = _FAIL;
		break;
	}

	return ret;
}

u8 SetHwReg8723D(PADAPTER padapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8 ret = _SUCCESS;
	u8 val8;
	u16 val16;
	u32 val32;


	switch (variable) {
	case HW_VAR_SET_OPMODE:
		hw_var_set_opmode(padapter, variable, val);
		break;

	case HW_VAR_BASIC_RATE:
		rtw_var_set_basic_rate(padapter, val);
		break;

	case HW_VAR_TXPAUSE:
		rtw_write8(padapter, REG_TXPAUSE, *val);
		break;

	case HW_VAR_SLOT_TIME:
		rtw_write8(padapter, REG_SLOT, *val);
		break;

	case HW_VAR_RESP_SIFS:
#if 0
		/* SIFS for OFDM Data ACK */
		rtw_write8(padapter, REG_SIFS_CTX + 1, val[0]);
		/* SIFS for OFDM consecutive tx like CTS data! */
		rtw_write8(padapter, REG_SIFS_TRX + 1, val[1]);

		rtw_write8(padapter, REG_SPEC_SIFS + 1, val[0]);
		rtw_write8(padapter, REG_MAC_SPEC_SIFS + 1, val[0]);

		/* 20100719 Joseph: Revise SIFS setting due to Hardware register definition change. */
		rtw_write8(padapter, REG_R2T_SIFS + 1, val[0]);
		rtw_write8(padapter, REG_T2T_SIFS + 1, val[0]);

#else
		/* SIFS_Timer = 0x0a0a0808; */
		/* RESP_SIFS for CCK */
		rtw_write8(padapter, REG_RESP_SIFS_CCK, val[0]); /* SIFS_T2T_CCK (0x08) */
		rtw_write8(padapter, REG_RESP_SIFS_CCK + 1, val[1]); /* SIFS_R2T_CCK(0x08) */
		/* RESP_SIFS for OFDM */
		rtw_write8(padapter, REG_RESP_SIFS_OFDM, val[2]); /* SIFS_T2T_OFDM (0x0a) */
		rtw_write8(padapter, REG_RESP_SIFS_OFDM + 1, val[3]); /* SIFS_R2T_OFDM(0x0a) */
#endif
		break;

	case HW_VAR_ACK_PREAMBLE: {
		u8 regTmp;
		u8 bShortPreamble = *val;

		/* Joseph marked out for Netgear 3500 TKIP channel 7 issue.(Temporarily) */
		/* regTmp = (pHalData->nCur40MhzPrimeSC)<<5; */
		regTmp = rtw_read8(padapter, REG_WMAC_TRXPTCL_CTL + 2);
		if (bShortPreamble) {
			regTmp |= BIT(1);/*668[17]*/
		} else {
			regTmp &= ~BIT(1);
		}
		rtw_write8(padapter, REG_WMAC_TRXPTCL_CTL + 2, regTmp);
	}
		break;

	case HW_VAR_CAM_INVALID_ALL:
		rtw_write32(padapter, REG_CAMCMD, BIT(31) | BIT(30));
		break;

	case HW_VAR_AC_PARAM_VO:
		rtw_write32(padapter, REG_EDCA_VO_PARAM, *((u32 *)val));
		break;

	case HW_VAR_AC_PARAM_VI:
		rtw_write32(padapter, REG_EDCA_VI_PARAM, *((u32 *)val));
		break;

	case HW_VAR_AC_PARAM_BE:
		pHalData->ac_param_be = ((u32 *)(val))[0];
		rtw_write32(padapter, REG_EDCA_BE_PARAM, *((u32 *)val));
		break;

	case HW_VAR_AC_PARAM_BK:
		rtw_write32(padapter, REG_EDCA_BK_PARAM, *((u32 *)val));
		break;

	case HW_VAR_ACM_CTRL: {
		u8 ctrl = *((u8 *)val);
		u8 hwctrl = 0;

		if (ctrl != 0) {
			hwctrl |= AcmHw_HwEn;

		if (ctrl & BIT(3)) /* BE */
			hwctrl |= AcmHw_BeqEn;

		if (ctrl & BIT(2)) /* VI */
			hwctrl |= AcmHw_ViqEn;

		if (ctrl & BIT(1)) /* VO */
			hwctrl |= AcmHw_VoqEn;
		}

		RTW_INFO("[HW_VAR_ACM_CTRL] Write 0x%02X\n", hwctrl);
		rtw_write8(padapter, REG_ACMHWCTRL, hwctrl);
	}
		break;
#ifdef CONFIG_80211N_HT
	case HW_VAR_AMPDU_FACTOR: {
		u32	AMPDULen = (*((u8 *)val));

		if (AMPDULen < HT_AGG_SIZE_32K)
			AMPDULen = (0x2000 << (*((u8 *)val))) - 1;
		else
			AMPDULen = 0x7fff;

		rtw_write32(padapter, REG_AMPDU_MAX_LENGTH_8723D, AMPDULen);
	}
		break;
#endif /* CONFIG_80211N_HT */
	case HW_VAR_H2C_FW_PWRMODE: {
		u8 psmode = *val;

		/* if (psmode != PS_MODE_ACTIVE)	{ */
			/*rtl8723d_set_lowpwr_lps_cmd(padapter, _TRUE); */
		/* } else { */
			/*	rtl8723d_set_lowpwr_lps_cmd(padapter, _FALSE); */
		/* } */
		rtl8723d_set_FwPwrMode_cmd(padapter, psmode);
	}
		break;
	case HW_VAR_H2C_PS_TUNE_PARAM:
		rtl8723d_set_FwPsTuneParam_cmd(padapter);
		break;

	case HW_VAR_H2C_FW_JOINBSSRPT:
		rtl8723d_set_FwJoinBssRpt_cmd(padapter, *val);
#ifdef CONFIG_LPS_POFF
		rtl8723d_lps_poff_h2c_ctrl(padapter, *val);
#endif
		break;

	case HW_VAR_DL_RSVD_PAGE:
#ifdef CONFIG_BT_COEXIST
		if (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == _TRUE)
			rtl8723d_download_BTCoex_AP_mode_rsvd_page(padapter);
		else
#endif /* CONFIG_BT_COEXIST */
		{
			rtl8723d_download_rsvd_page(padapter, RT_MEDIA_CONNECT);
		}
		break;

#ifdef CONFIG_P2P
	case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
		rtl8723d_set_p2p_ps_offload_cmd(padapter, *val);
		break;
#endif /* CONFIG_P2P */
#ifdef CONFIG_LPS_POFF
	case HW_VAR_LPS_POFF_INIT:
		rtl8723d_lps_poff_init(padapter);
		break;
	case HW_VAR_LPS_POFF_DEINIT:
		rtl8723d_lps_poff_deinit(padapter);
		break;
	case HW_VAR_LPS_POFF_SET_MODE:
		rtl8723d_lps_poff_set_ps_mode(padapter, *val);
		break;
	case HW_VAR_LPS_POFF_WOW_EN:
		rtl8723d_lps_poff_h2c_ctrl(padapter, *val);
		break;
#endif /*CONFIG_LPS_POFF*/

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

	case HW_VAR_FIFO_CLEARN_UP: {
#define RW_RELEASE_EN		BIT(18)
#define RXDMA_IDLE			BIT(17)

		struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
		u8 trycnt = 100;

		/* pause tx */
		rtw_write8(padapter, REG_TXPAUSE, 0xff);

		/* keep sn */
		padapter->xmitpriv.nqos_ssn = rtw_read16(padapter, REG_NQOS_SEQ);

		if (pwrpriv->bkeepfwalive != _TRUE) {
			/* RX DMA stop */
			val32 = rtw_read32(padapter, REG_RXPKT_NUM);
			val32 |= RW_RELEASE_EN;
			rtw_write32(padapter, REG_RXPKT_NUM, val32);
			do {
				val32 = rtw_read32(padapter, REG_RXPKT_NUM);
				val32 &= RXDMA_IDLE;
				if (val32)
					break;

				RTW_INFO("%s: [HW_VAR_FIFO_CLEARN_UP] val=%x times:%d\n", __FUNCTION__, val32, trycnt);
			} while (--trycnt);
			if (trycnt == 0)
				RTW_INFO("[HW_VAR_FIFO_CLEARN_UP] Stop RX DMA failed......\n");

			/* RQPN Load 0 */
			rtw_write16(padapter, REG_RQPN_NPQ, 0);
			rtw_write32(padapter, REG_RQPN, 0x80000000);
			rtw_mdelay_os(2);
		}
	}
	break;

	case HW_VAR_RESTORE_HW_SEQ:
		/* restore Sequence No. */
		rtw_write8(padapter, 0x4dc, padapter->xmitpriv.nqos_ssn);
		break;

#ifdef CONFIG_CONCURRENT_MODE
	case HW_VAR_CHECK_TXBUF: {
		u32 i;
		u8 RetryLimit = 0x01;
		u32 reg_200, reg_204;

		val16 = BIT_SRL(RetryLimit) | BIT_LRL(RetryLimit);
		rtw_write16(padapter, REG_RETRY_LIMIT, val16);

		for (i = 0; i < 200; i++) { /* polling 200x10=2000 msec */
			reg_200 = rtw_read32(padapter, 0x200);
			reg_204 = rtw_read32(padapter, 0x204);
			if (reg_200 != reg_204) {
			/* RTW_INFO("packet in tx packet buffer - 0x204=%x, 0x200=%x (%d)\n", rtw_read32(padapter, 0x204), rtw_read32(padapter, 0x200), i); */
				rtw_msleep_os(10);
			} else {
				RTW_INFO("[HW_VAR_CHECK_TXBUF] no packet in tx packet buffer (%d)\n", i);
				break;
			}
		}

		if (reg_200 != reg_204)
			RTW_INFO("packets in tx buffer - 0x204=%x, 0x200=%x\n", reg_204, reg_200);

		RetryLimit = RL_VAL_STA;
		val16 = BIT_SRL(RetryLimit) | BIT_LRL(RetryLimit);
		rtw_write16(padapter, REG_RETRY_LIMIT, val16);
	}
		break;
#endif /* CONFIG_CONCURRENT_MODE */

	case HW_VAR_NAV_UPPER: {
		u32 usNavUpper = *((u32 *)val);

		if (usNavUpper > HAL_NAV_UPPER_UNIT_8723D * 0xFF) {
		break;
	}

		/* The value of ((usNavUpper + HAL_NAV_UPPER_UNIT_8723D - 1) / HAL_NAV_UPPER_UNIT_8723D) */
		/* is getting the upper integer. */
		usNavUpper = (usNavUpper + HAL_NAV_UPPER_UNIT_8723D - 1) / HAL_NAV_UPPER_UNIT_8723D;
		rtw_write8(padapter, REG_NAV_UPPER, (u8)usNavUpper);
	}
		break;

	case HW_VAR_BCN_VALID:
#ifdef CONFIG_CONCURRENT_MODE
		if (padapter->hw_port == HW_PORT1) {
			val8 = rtw_read8(padapter,  REG_DWBCN1_CTRL_8723D + 2);
			val8 |= BIT(0);
			rtw_write8(padapter, REG_DWBCN1_CTRL_8723D + 2, val8);
		} else
#endif /* CONFIG_CONCURRENT_MODE */
		{
		/* BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2, write 1 to clear, Clear by sw */
			val8 = rtw_read8(padapter, REG_TDECTRL + 2);
			val8 |= BIT(0);
			rtw_write8(padapter, REG_TDECTRL + 2, val8);
		}
		break;
	case HW_VAR_DL_BCN_SEL:
#ifdef CONFIG_CONCURRENT_MODE
		if (padapter->hw_port == HW_PORT1) {
			/* SW_BCN_SEL - Port1 */
			val8 = rtw_read8(padapter, REG_DWBCN1_CTRL_8723D + 2);
			val8 |= BIT(4);
			rtw_write8(padapter, REG_DWBCN1_CTRL_8723D + 2, val8);
		} else
#endif /* CONFIG_CONCURRENT_MODE */
		{
			/* SW_BCN_SEL - Port0 */
			val8 = rtw_read8(padapter, REG_DWBCN1_CTRL_8723D + 2);
			val8 &= ~BIT(4);
			rtw_write8(padapter, REG_DWBCN1_CTRL_8723D + 2, val8);
		}
		break;

#if defined(CONFIG_TDLS) && defined(CONFIG_TDLS_CH_SW)
	case HW_VAR_TDLS_BCN_EARLY_C2H_RPT:
		rtl8723d_set_BcnEarly_C2H_Rpt_cmd(padapter, *val);
		break;
#endif	
	default:
		ret = SetHwReg(padapter, variable, val);
		break;
	}

	return ret;
}

struct qinfo_8723d {
	u32 head:8;
	u32 pkt_num:7;
	u32 tail:8;
	u32 ac:2;
	u32 macid:7;
};

struct bcn_qinfo_8723d {
	u16 head:8;
	u16 pkt_num:8;
};

void dump_qinfo_8723d(void *sel, struct qinfo_8723d *info, const char *tag)
{
	/* if (info->pkt_num) */
	RTW_PRINT_SEL(sel, "%shead:0x%02x, tail:0x%02x, pkt_num:%u, macid:%u, ac:%u\n"
		, tag ? tag : "", info->head, info->tail,
		info->pkt_num, info->macid, info->ac);
}

void dump_bcn_qinfo_8723d(void *sel, struct bcn_qinfo_8723d *info, const char *tag)
{
	/* if (info->pkt_num) */
	RTW_PRINT_SEL(sel, "%shead:0x%02x, pkt_num:%u\n"
		      , tag ? tag : "", info->head, info->pkt_num);
}

void dump_mac_qinfo_8723d(void *sel, _adapter *adapter)
{
	u32 q0_info;
	u32 q1_info;
	u32 q2_info;
	u32 q3_info;
	u32 q4_info;
	u32 q5_info;
	u32 q6_info;
	u32 q7_info;
	u32 mg_q_info;
	u32 hi_q_info;
	u16 bcn_q_info;

	q0_info = rtw_read32(adapter, REG_Q0_INFO);
	q1_info = rtw_read32(adapter, REG_Q1_INFO);
	q2_info = rtw_read32(adapter, REG_Q2_INFO);
	q3_info = rtw_read32(adapter, REG_Q3_INFO);
	q4_info = rtw_read32(adapter, REG_Q4_INFO);
	q5_info = rtw_read32(adapter, REG_Q5_INFO);
	q6_info = rtw_read32(adapter, REG_Q6_INFO);
	q7_info = rtw_read32(adapter, REG_Q7_INFO);
	mg_q_info = rtw_read32(adapter, REG_MGQ_INFO);
	hi_q_info = rtw_read32(adapter, REG_HGQ_INFO);
	bcn_q_info = rtw_read16(adapter, REG_BCNQ_INFO);

	dump_qinfo_8723d(sel, (struct qinfo_8723d *)&q0_info, "Q0 ");
	dump_qinfo_8723d(sel, (struct qinfo_8723d *)&q1_info, "Q1 ");
	dump_qinfo_8723d(sel, (struct qinfo_8723d *)&q2_info, "Q2 ");
	dump_qinfo_8723d(sel, (struct qinfo_8723d *)&q3_info, "Q3 ");
	dump_qinfo_8723d(sel, (struct qinfo_8723d *)&q4_info, "Q4 ");
	dump_qinfo_8723d(sel, (struct qinfo_8723d *)&q5_info, "Q5 ");
	dump_qinfo_8723d(sel, (struct qinfo_8723d *)&q6_info, "Q6 ");
	dump_qinfo_8723d(sel, (struct qinfo_8723d *)&q7_info, "Q7 ");
	dump_qinfo_8723d(sel, (struct qinfo_8723d *)&mg_q_info, "MG ");
	dump_qinfo_8723d(sel, (struct qinfo_8723d *)&hi_q_info, "HI ");
	dump_bcn_qinfo_8723d(sel, (struct bcn_qinfo_8723d *)&bcn_q_info, "BCN ");
}

static void dump_mac_txfifo_8723d(void *sel, _adapter *adapter)
{
	u32 rqpn, rqpn_npq;
	u32 hpq, lpq, npq, epq, pubq;

	rqpn = rtw_read32(adapter, REG_FIFOPAGE);
	rqpn_npq = rtw_read32(adapter, REG_RQPN_NPQ);

	hpq = (rqpn & 0xFF);
	lpq = ((rqpn & 0xFF00)>>8);
	pubq = ((rqpn & 0xFF0000)>>16);
	npq = ((rqpn_npq & 0xFF00)>>8);
	epq = ((rqpn_npq & 0xFF000000)>>24);

	RTW_PRINT_SEL(sel, "Tx: available page num: ");
	if ((hpq == 0xEA) && (hpq == lpq) && (hpq == pubq))
		RTW_PRINT_SEL(sel, "N/A (reg val = 0xea)\n");
	else
		RTW_PRINT_SEL(sel, "HPQ: %d, LPQ: %d, NPQ: %d, EPQ: %d, PUBQ: %d\n"
			, hpq, lpq, npq, epq, pubq);
}

void rtl8723d_read_wmmedca_reg(PADAPTER adapter, u16 *vo_params, u16 *vi_params, u16 *be_params, u16 *bk_params)
{
	u8 vo_reg_params[4];
	u8 vi_reg_params[4];
	u8 be_reg_params[4];
	u8 bk_reg_params[4];

	GetHwReg8723D(adapter, HW_VAR_AC_PARAM_VO, vo_reg_params);
	GetHwReg8723D(adapter, HW_VAR_AC_PARAM_VI, vi_reg_params);
	GetHwReg8723D(adapter, HW_VAR_AC_PARAM_BE, be_reg_params);
	GetHwReg8723D(adapter, HW_VAR_AC_PARAM_BK, bk_reg_params);

	vo_params[0] = vo_reg_params[0];
	vo_params[1] = vo_reg_params[1] & 0x0F;
	vo_params[2] = (vo_reg_params[1] & 0xF0) >> 4;
	vo_params[3] = ((vo_reg_params[3] << 8) | (vo_reg_params[2])) * 32;

	vi_params[0] = vi_reg_params[0];
	vi_params[1] = vi_reg_params[1] & 0x0F;
	vi_params[2] = (vi_reg_params[1] & 0xF0) >> 4;
	vi_params[3] = ((vi_reg_params[3] << 8) | (vi_reg_params[2])) * 32;

	be_params[0] = be_reg_params[0];
	be_params[1] = be_reg_params[1] & 0x0F;
	be_params[2] = (be_reg_params[1] & 0xF0) >> 4;
	be_params[3] = ((be_reg_params[3] << 8) | (be_reg_params[2])) * 32;

	bk_params[0] = bk_reg_params[0];
	bk_params[1] = bk_reg_params[1] & 0x0F;
	bk_params[2] = (bk_reg_params[1] & 0xF0) >> 4;
	bk_params[3] = ((bk_reg_params[3] << 8) | (bk_reg_params[2])) * 32;

	vo_params[1] = (1 << vo_params[1]) - 1;
	vo_params[2] = (1 << vo_params[2]) - 1;
	vi_params[1] = (1 << vi_params[1]) - 1;
	vi_params[2] = (1 << vi_params[2]) - 1;
	be_params[1] = (1 << be_params[1]) - 1;
	be_params[2] = (1 << be_params[2]) - 1;
	bk_params[1] = (1 << bk_params[1]) - 1;
	bk_params[2] = (1 << bk_params[2]) - 1;
}

void GetHwReg8723D(PADAPTER padapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u8 val8;
	u16 val16;
	u32 val32;

	switch (variable) {
	case HW_VAR_TXPAUSE:
		*val = rtw_read8(padapter, REG_TXPAUSE);
		break;

	case HW_VAR_BCN_VALID:
#ifdef CONFIG_CONCURRENT_MODE
		if (padapter->hw_port == HW_PORT1) {
			val8 = rtw_read8(padapter, REG_DWBCN1_CTRL_8723D + 2);
			*val = (BIT(0) & val8) ? _TRUE : _FALSE;
		} else
#endif
		{
			/* BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2 */
			val8 = rtw_read8(padapter, REG_TDECTRL + 2);
			*val = (BIT(0) & val8) ? _TRUE : _FALSE;
		}
		break;

	case HW_VAR_AC_PARAM_VO:
		val32 = rtw_read32(padapter, REG_EDCA_VO_PARAM);
		val[0] = val32 & 0xFF;
		val[1] = (val32 >> 8) & 0xFF;
		val[2] = (val32 >> 16) & 0xFF;
		val[3] = (val32 >> 24) & 0x07;
		break;

	case HW_VAR_AC_PARAM_VI:
		val32 = rtw_read32(padapter, REG_EDCA_VI_PARAM);
		val[0] = val32 & 0xFF;
		val[1] = (val32 >> 8) & 0xFF;
		val[2] = (val32 >> 16) & 0xFF;
		val[3] = (val32 >> 24) & 0x07;
		break;

	case HW_VAR_AC_PARAM_BE:
		val32 = rtw_read32(padapter, REG_EDCA_BE_PARAM);
		val[0] = val32 & 0xFF;
		val[1] = (val32 >> 8) & 0xFF;
		val[2] = (val32 >> 16) & 0xFF;
		val[3] = (val32 >> 24) & 0x07;
		break;

	case HW_VAR_AC_PARAM_BK:
		val32 = rtw_read32(padapter, REG_EDCA_BK_PARAM);
		val[0] = val32 & 0xFF;
		val[1] = (val32 >> 8) & 0xFF;
		val[2] = (val32 >> 16) & 0xFF;
		val[3] = (val32 >> 24) & 0x07;
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

	case HW_VAR_CHK_HI_QUEUE_EMPTY:
		val16 = rtw_read16(padapter, REG_TXPKT_EMPTY);
		*val = (val16 & BIT(10)) ? _TRUE : _FALSE;
		break;
	case HW_VAR_CHK_MGQ_CPU_EMPTY:
		val16 = rtw_read16(padapter, REG_TXPKT_EMPTY);
		*val = (val16 & BIT(8)) ? _TRUE : _FALSE;
		break;
#ifdef CONFIG_WOWLAN
	case HW_VAR_RPWM_TOG:
		*val = rtw_read8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HRPWM1) & BIT(7);
		break;
	case HW_VAR_WAKEUP_REASON:
		*val = rtw_read8(padapter, REG_WOWLAN_WAKE_REASON);
		if (*val == 0xEA)
			*val = 0;
		break;
	case HW_VAR_SYS_CLKR:
		*val = rtw_read8(padapter, REG_SYS_CLKR);
		break;
#endif
	case HW_VAR_DUMP_MAC_QUEUE_INFO:
		dump_mac_qinfo_8723d(val, padapter);
		break;
	case HW_VAR_DUMP_MAC_TXFIFO:
		dump_mac_txfifo_8723d(val, padapter);
		break;
	default:
		GetHwReg(padapter, variable, val);
		break;
	}
}

/*
 *	Description:
 *		Change default setting of specified variable.
 */
u8 SetHalDefVar8723D(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval)
{
	PHAL_DATA_TYPE pHalData;
	u8 bResult;

	bResult = _SUCCESS;

	switch (variable) {
	default:
		bResult = SetHalDefVar(padapter, variable, pval);
		break;
	}

	return bResult;
}

void hal_ra_info_dump(_adapter *padapter , void *sel)
{
	int i;
	u8 mac_id;
	u32 cmd;
	u32 ra_info1, ra_info2, bw_set;
	u32 rate_mask1, rate_mask2;
	u8 curr_tx_rate, curr_tx_sgi, hight_rate, lowest_rate;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	HAL_DATA_TYPE *HalData = GET_HAL_DATA(padapter);

	for (i = 0; i < macid_ctl->num; i++) {

		if (rtw_macid_is_used(macid_ctl, i) && !rtw_macid_is_bmc(macid_ctl, i)) {

			mac_id = (u8) i;
			_RTW_PRINT_SEL(sel , "============ RA status check  Mac_id:%d ===================\n", mac_id);

			cmd = 0x40000100 | mac_id;
			rtw_write32(padapter, REG_HMEBOX_DBG_2_8723D, cmd);
			rtw_msleep_os(10);
			ra_info1 = rtw_read32(padapter, 0x2F0);
			curr_tx_sgi = rtw_get_current_tx_sgi(padapter, macid_ctl->sta[mac_id]);
			curr_tx_rate = rtw_get_current_tx_rate(padapter, macid_ctl->sta[mac_id]);

			_RTW_PRINT_SEL(sel , "[ ra_info1:0x%08x ] =>cur_tx_rate= %s,cur_sgi:%d\n", ra_info1, HDATA_RATE(curr_tx_rate), curr_tx_sgi);
			_RTW_PRINT_SEL(sel , "[ ra_info1:0x%08x ] => PWRSTS = 0x%02x\n", ra_info1, (ra_info1 >> 8)  & 0x07);

			cmd = 0x40000400 | mac_id;
			rtw_write32(padapter, REG_HMEBOX_DBG_2_8723D, cmd);
			rtw_msleep_os(10);
			ra_info1 = rtw_read32(padapter, 0x2F0);
			ra_info2 = rtw_read32(padapter, 0x2F4);
			rate_mask1 = rtw_read32(padapter, 0x2F8);
			rate_mask2 = rtw_read32(padapter, 0x2FC);
			hight_rate = ra_info2 & 0xFF;
			lowest_rate = (ra_info2 >> 8)  & 0xFF;
			bw_set = (ra_info1 >> 8)  & 0xFF;

			_RTW_PRINT_SEL(sel , "[ ra_info1:0x%08x ] => VHT_EN=0x%02x, ", ra_info1, (ra_info1 >> 24) & 0xFF);


			switch (bw_set) {

			case CHANNEL_WIDTH_20:
				_RTW_PRINT_SEL(sel , "BW_setting=20M\n");
				break;

			case CHANNEL_WIDTH_40:
				_RTW_PRINT_SEL(sel , "BW_setting=40M\n");
				break;

			case CHANNEL_WIDTH_80:
				_RTW_PRINT_SEL(sel , "BW_setting=80M\n");
				break;

			case CHANNEL_WIDTH_160:
				_RTW_PRINT_SEL(sel , "BW_setting=160M\n");
				break;

			default:
				_RTW_PRINT_SEL(sel , "BW_setting=0x%02x\n", bw_set);
				break;

			}

			_RTW_PRINT_SEL(sel , "[ ra_info1:0x%08x ] =>RSSI=%d, DISRA=0x%02x\n",
				       ra_info1,
				       ra_info1 & 0xFF,
				       (ra_info1 >> 16) & 0xFF);

			_RTW_PRINT_SEL(sel , "[ ra_info2:0x%08x ] =>hight_rate=%s, lowest_rate=%s, SGI=0x%02x, RateID=%d\n",
				       ra_info2,
				       HDATA_RATE(hight_rate),
				       HDATA_RATE(lowest_rate),
				       (ra_info2 >> 16) & 0xFF,
				       (ra_info2 >> 24) & 0xFF);

			_RTW_PRINT_SEL(sel , "rate_mask2=0x%08x, rate_mask1=0x%08x\n", rate_mask2, rate_mask1);

		}
	}
}

/*
 *	Description:
 *		Query setting of specified variable.
 */
u8 GetHalDefVar8723D(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval)
{
	PHAL_DATA_TYPE pHalData;
	u8 bResult;

	pHalData = GET_HAL_DATA(padapter);
	bResult = _SUCCESS;

	switch (variable) {
	case HAL_DEF_MAX_RECVBUF_SZ:
		*((u32 *)pval) = MAX_RECVBUF_SZ;
		break;
	case HAL_DEF_RX_PACKET_OFFSET:
#ifdef CONFIG_TRX_BD_ARCH
		*((u32 *)pval) = RX_WIFI_INFO_SIZE + DRVINFO_SZ * 8;
#else
		*((u32 *)pval) = RXDESC_SIZE + DRVINFO_SZ * 8;
#endif
		break;

	case HW_VAR_MAX_RX_AMPDU_FACTOR:
		/* Stanley@BB.SD3 suggests 16K can get stable performance */
		/* The experiment was done on SDIO interface */
		/* coding by Lucas@20130730 */
		*(HT_CAP_AMPDU_FACTOR *)pval = MAX_AMPDU_FACTOR_16K;
		break;
	case HW_VAR_BEST_AMPDU_DENSITY:
		*((u32 *)pval) = AMPDU_DENSITY_VALUE_7;
		break;
	case HAL_DEF_TX_LDPC:
	case HAL_DEF_RX_LDPC:
		*((u8 *)pval) = _FALSE;
		break;
	case HAL_DEF_RX_STBC:
		*((u8 *)pval) = 1;
		break;
	case HAL_DEF_EXPLICIT_BEAMFORMER:
	case HAL_DEF_EXPLICIT_BEAMFORMEE:
		*((u8 *)pval) = _FALSE;
		break;

	case HW_DEF_RA_INFO_DUMP:
		hal_ra_info_dump(padapter, pval);
		break;

	case HAL_DEF_TX_PAGE_BOUNDARY:
		if (!padapter->registrypriv.wifi_spec)
			*(u8 *)pval = TX_PAGE_BOUNDARY_8723D;
		else
			*(u8 *)pval = WMM_NORMAL_TX_PAGE_BOUNDARY_8723D;
		break;
	case HAL_DEF_TX_PAGE_SIZE:
		*((u32 *)pval) = PAGE_SIZE_128;
		break;
	case HAL_DEF_RX_DMA_SZ_WOW:
		*(u32 *)pval = RX_DMA_SIZE_8723D - RESV_FMWF;
		break;
	case HAL_DEF_RX_DMA_SZ:
		*(u32 *)pval = RX_DMA_BOUNDARY_8723D + 1;
		break;
	case HAL_DEF_RX_PAGE_SIZE:
		*((u32 *)pval) = 8;
		break;
	default:
		bResult = GetHalDefVar(padapter, variable, pval);
		break;
	}

	return bResult;
}

#ifdef CONFIG_WOWLAN
void Hal_DetectWoWMode(PADAPTER pAdapter)
{
	adapter_to_pwrctl(pAdapter)->bSupportRemoteWakeup = _TRUE;
	RTW_INFO("%s\n", __func__);
}
#endif /* CONFIG_WOWLAN */

void rtl8723d_start_thread(_adapter *padapter)
{
#if (defined CONFIG_SDIO_HCI) || (defined CONFIG_GSPI_HCI)
#ifndef CONFIG_SDIO_TX_TASKLET
	struct xmit_priv *xmitpriv = &padapter->xmitpriv;

	if (xmitpriv->SdioXmitThread == NULL) {
		RTW_INFO(FUNC_ADPT_FMT " start RTWHALXT\n", FUNC_ADPT_ARG(padapter));
		xmitpriv->SdioXmitThread = kthread_run(rtl8723ds_xmit_thread, padapter, "RTWHALXT");
		if (IS_ERR(xmitpriv->SdioXmitThread)) {
			RTW_ERR("%s: start rtl8723ds_xmit_thread FAIL!!\n", __func__);
			xmitpriv->SdioXmitThread = NULL;
		}
	}
#endif
#endif
}

void rtl8723d_stop_thread(_adapter *padapter)
{
#if (defined CONFIG_SDIO_HCI) || (defined CONFIG_GSPI_HCI)
#ifndef CONFIG_SDIO_TX_TASKLET
	struct xmit_priv *xmitpriv = &padapter->xmitpriv;

	/* stop xmit_buf_thread */
	if (xmitpriv->SdioXmitThread) {
		_rtw_up_sema(&xmitpriv->SdioXmitSema);
		#ifdef SDIO_FREE_XMIT_BUF_SEMA
		rtw_sdio_free_xmitbuf_sema_up(xmitpriv);
		rtw_sdio_free_xmitbuf_sema_down(xmitpriv);
		#endif
		rtw_thread_stop(xmitpriv->SdioXmitThread);
		xmitpriv->SdioXmitThread = NULL;
	}
#endif
#endif
}

#if defined(CONFIG_CHECK_BT_HANG) && defined(CONFIG_BT_COEXIST)
void rtl8723ds_init_checkbthang_workqueue(_adapter *adapter)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	adapter->priv_checkbt_wq = alloc_workqueue("sdio_wq", 0, 0);
#else
	adapter->priv_checkbt_wq = create_workqueue("sdio_wq");
#endif
	INIT_DELAYED_WORK(&adapter->checkbt_work, (void *)check_bt_status_work);
}

void rtl8723ds_free_checkbthang_workqueue(_adapter *adapter)
{
	if (adapter->priv_checkbt_wq) {
		cancel_delayed_work_sync(&adapter->checkbt_work);
		flush_workqueue(adapter->priv_checkbt_wq);
		destroy_workqueue(adapter->priv_checkbt_wq);
		adapter->priv_checkbt_wq = NULL;
	}
}

void rtl8723ds_cancle_checkbthang_workqueue(_adapter *adapter)
{
	if (adapter->priv_checkbt_wq)
		cancel_delayed_work_sync(&adapter->checkbt_work);
}

void rtl8723ds_hal_check_bt_hang(_adapter *adapter)
{
	if (adapter->priv_checkbt_wq)
		queue_delayed_work(adapter->priv_checkbt_wq, &(adapter->checkbt_work), 0);
}
#endif

void rtl8723d_set_hal_ops(struct hal_ops *pHalFunc)
{
	pHalFunc->dm_init = &rtl8723d_init_dm_priv;
	pHalFunc->dm_deinit = &rtl8723d_deinit_dm_priv;
	pHalFunc->read_chip_version = read_chip_version_8723d;
	pHalFunc->set_chnl_bw_handler = &PHY_SetSwChnlBWMode8723D;
	pHalFunc->set_tx_power_level_handler = &PHY_SetTxPowerLevel8723D;
	pHalFunc->set_tx_power_index_handler = PHY_SetTxPowerIndex_8723D;
	pHalFunc->get_tx_power_index_handler = hal_com_get_txpwr_idx;
	pHalFunc->hal_dm_watchdog = &rtl8723d_HalDmWatchDog;

	pHalFunc->SetBeaconRelatedRegistersHandler = &rtl8723d_SetBeaconRelatedRegisters;
	pHalFunc->run_thread = &rtl8723d_start_thread;
	pHalFunc->cancel_thread = &rtl8723d_stop_thread;
	pHalFunc->read_bbreg = &PHY_QueryBBReg_8723D;
	pHalFunc->write_bbreg = &PHY_SetBBReg_8723D;
	pHalFunc->read_rfreg = &PHY_QueryRFReg_8723D;
	pHalFunc->write_rfreg = &PHY_SetRFReg_8723D;
	pHalFunc->read_wmmedca_reg = &rtl8723d_read_wmmedca_reg;

	/* Efuse related function */
	pHalFunc->BTEfusePowerSwitch = &Hal_BT_EfusePowerSwitch;
	pHalFunc->EfusePowerSwitch = &Hal_EfusePowerSwitch;
	pHalFunc->ReadEFuse = &Hal_ReadEFuse;
	pHalFunc->EFUSEGetEfuseDefinition = &Hal_GetEfuseDefinition;
	pHalFunc->EfuseGetCurrentSize = &Hal_EfuseGetCurrentSize;
	pHalFunc->Efuse_PgPacketRead = &Hal_EfusePgPacketRead;
	pHalFunc->Efuse_PgPacketWrite = &Hal_EfusePgPacketWrite;
	pHalFunc->Efuse_WordEnableDataWrite = &Hal_EfuseWordEnableDataWrite;
	pHalFunc->Efuse_PgPacketWrite_BT = &Hal_EfusePgPacketWrite_BT;

#ifdef DBG_CONFIG_ERROR_DETECT
	pHalFunc->sreset_init_value = &sreset_init_value;
	pHalFunc->sreset_reset_value = &sreset_reset_value;
	pHalFunc->silentreset = &sreset_reset;
	pHalFunc->sreset_xmit_status_check = &rtl8723d_sreset_xmit_status_check;
	pHalFunc->sreset_linked_status_check  = &rtl8723d_sreset_linked_status_check;
	pHalFunc->sreset_get_wifi_status  = &sreset_get_wifi_status;
	pHalFunc->sreset_inprogress = &sreset_inprogress;
#endif
	pHalFunc->GetHalODMVarHandler = GetHalODMVar;
	pHalFunc->SetHalODMVarHandler = SetHalODMVar;

#ifdef CONFIG_XMIT_THREAD_MODE
	pHalFunc->xmit_thread_handler = &hal_xmit_handler;
#endif
	pHalFunc->hal_notch_filter = &hal_notch_filter_8723d;
	pHalFunc->c2h_handler = c2h_handler_8723d;
	pHalFunc->fill_h2c_cmd = &FillH2CCmd8723D;
	pHalFunc->fill_fake_txdesc = &rtl8723d_fill_fake_txdesc;
	pHalFunc->fw_dl = &rtl8723d_FirmwareDownload;
	pHalFunc->hal_get_tx_buff_rsvd_page_num = &GetTxBufferRsvdPageNum8723D;
}

