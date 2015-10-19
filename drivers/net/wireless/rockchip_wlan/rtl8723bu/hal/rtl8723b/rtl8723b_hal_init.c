/******************************************************************************
 *
 * Copyright(c) 2007 - 2013 Realtek Corporation. All rights reserved.
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

#include <rtl8723b_hal.h>
#include "rtw_bt_mp.h"
#include "hal_com_h2c.h"
#include <hal_com.h>

static VOID
_FWDownloadEnable(
	IN	PADAPTER		padapter,
	IN	BOOLEAN			enable
	)
{
	u8	tmp, count = 0;

	if(enable)
	{
		// 8051 enable
		tmp = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
		rtw_write8(padapter, REG_SYS_FUNC_EN+1, tmp|0x04);

		tmp = rtw_read8(padapter, REG_MCUFWDL);
		rtw_write8(padapter, REG_MCUFWDL, tmp|0x01);

		do{
			tmp = rtw_read8(padapter, REG_MCUFWDL);
			if(tmp & 0x01)
				break;
			rtw_write8(padapter, REG_MCUFWDL, tmp|0x01);
			rtw_msleep_os(1);
		}while(count++<100);
		if(count > 0)
			DBG_871X("%s: !!!!!!!!Write 0x80 Fail!: count = %d\n", __func__, count);

		// 8051 reset
		tmp = rtw_read8(padapter, REG_MCUFWDL+2);
		rtw_write8(padapter, REG_MCUFWDL+2, tmp&0xf7);
	}
	else
	{
		// MCU firmware download disable.
		tmp = rtw_read8(padapter, REG_MCUFWDL);
		rtw_write8(padapter, REG_MCUFWDL, tmp&0xfe);
	}
}

static int
_BlockWrite(
	IN		PADAPTER		padapter,
	IN		PVOID		buffer,
	IN		u32			buffSize
	)
{
	int ret = _SUCCESS;

	u32			blockSize_p1 = 4;	// (Default) Phase #1 : PCI muse use 4-byte write to download FW
	u32			blockSize_p2 = 8;	// Phase #2 : Use 8-byte, if Phase#1 use big size to write FW.
	u32			blockSize_p3 = 1;	// Phase #3 : Use 1-byte, the remnant of FW image.
	u32			blockCount_p1 = 0, blockCount_p2 = 0, blockCount_p3 = 0;
	u32			remainSize_p1 = 0, remainSize_p2 = 0;
	u8			*bufferPtr	= (u8*)buffer;
	u32			i=0, offset=0;
#ifdef CONFIG_PCI_HCI
	u8			remainFW[4] = {0, 0, 0, 0};
	u8			*p = NULL;
#endif

#ifdef CONFIG_USB_HCI
	blockSize_p1 = 254;
#endif

//	printk("====>%s %d\n", __func__, __LINE__);

	//3 Phase #1
	blockCount_p1 = buffSize / blockSize_p1;
	remainSize_p1 = buffSize % blockSize_p1;

	if (blockCount_p1) {
		RT_TRACE(_module_hal_init_c_, _drv_notice_,
				("_BlockWrite: [P1] buffSize(%d) blockSize_p1(%d) blockCount_p1(%d) remainSize_p1(%d)\n",
				buffSize, blockSize_p1, blockCount_p1, remainSize_p1));
	}

	for (i = 0; i < blockCount_p1; i++)
	{
#ifdef CONFIG_USB_HCI
		ret = rtw_writeN(padapter, (FW_8723B_START_ADDRESS + i * blockSize_p1), blockSize_p1, (bufferPtr + i * blockSize_p1));
#else
		ret = rtw_write32(padapter, (FW_8723B_START_ADDRESS + i * blockSize_p1), le32_to_cpu(*((u32*)(bufferPtr + i * blockSize_p1))));
#endif
		if(ret == _FAIL) {
			printk("====>%s %d i:%d\n", __func__, __LINE__, i);
			goto exit;
		}
	}

#ifdef CONFIG_PCI_HCI
	p = (u8*)((u32*)(bufferPtr + blockCount_p1 * blockSize_p1));
	if (remainSize_p1) {
		switch (remainSize_p1) {
		case 0:
			break;
		case 3:
			remainFW[2]=*(p+2);
		case 2: 	
			remainFW[1]=*(p+1);
		case 1: 	
			remainFW[0]=*(p);
			ret = rtw_write32(padapter, (FW_8723B_START_ADDRESS + blockCount_p1 * blockSize_p1), 
				 le32_to_cpu(*(u32*)remainFW));	
		}
		return ret;
	}
#endif

	//3 Phase #2
	if (remainSize_p1)
	{
		offset = blockCount_p1 * blockSize_p1;

		blockCount_p2 = remainSize_p1/blockSize_p2;
		remainSize_p2 = remainSize_p1%blockSize_p2;

		if (blockCount_p2) {
				RT_TRACE(_module_hal_init_c_, _drv_notice_,
						("_BlockWrite: [P2] buffSize_p2(%d) blockSize_p2(%d) blockCount_p2(%d) remainSize_p2(%d)\n",
						(buffSize-offset), blockSize_p2 ,blockCount_p2, remainSize_p2));
		}

#ifdef CONFIG_USB_HCI
		for (i = 0; i < blockCount_p2; i++) {
			ret = rtw_writeN(padapter, (FW_8723B_START_ADDRESS + offset + i*blockSize_p2), blockSize_p2, (bufferPtr + offset + i*blockSize_p2));

			if(ret == _FAIL)
				goto exit;
		}
#endif
	}

	//3 Phase #3
	if (remainSize_p2)
	{
		offset = (blockCount_p1 * blockSize_p1) + (blockCount_p2 * blockSize_p2);

		blockCount_p3 = remainSize_p2 / blockSize_p3;

		RT_TRACE(_module_hal_init_c_, _drv_notice_,
				("_BlockWrite: [P3] buffSize_p3(%d) blockSize_p3(%d) blockCount_p3(%d)\n",
				(buffSize-offset), blockSize_p3, blockCount_p3));

		for(i = 0 ; i < blockCount_p3 ; i++){
			ret = rtw_write8(padapter, (FW_8723B_START_ADDRESS + offset + i), *(bufferPtr + offset + i));

			if(ret == _FAIL) {
				printk("====>%s %d i:%d\n", __func__, __LINE__, i);
				goto exit;
			}
		}
	}
exit:
	return ret;
}

static int
_PageWrite(
	IN		PADAPTER	padapter,
	IN		u32			page,
	IN		PVOID		buffer,
	IN		u32			size
	)
{
	u8 value8;
	u8 u8Page = (u8) (page & 0x07) ;

	value8 = (rtw_read8(padapter, REG_MCUFWDL+2) & 0xF8) | u8Page ;
	rtw_write8(padapter, REG_MCUFWDL+2,value8);

	return _BlockWrite(padapter,buffer,size);
}

static VOID
_FillDummy(
	u8*		pFwBuf,
	u32*	pFwLen
	)
{
	u32	FwLen = *pFwLen;
	u8	remain = (u8)(FwLen%4);
	remain = (remain==0)?0:(4-remain);

	while(remain>0)
	{
		pFwBuf[FwLen] = 0;
		FwLen++;
		remain--;
	}

	*pFwLen = FwLen;
}

static int
_WriteFW(
	IN		PADAPTER		padapter,
	IN		PVOID			buffer,
	IN		u32			size
	)
{
	// Since we need dynamic decide method of dwonload fw, so we call this function to get chip version.
	int ret = _SUCCESS;
	u32 	pageNums,remainSize ;
	u32 	page, offset;
	u8		*bufferPtr = (u8*)buffer;

#ifdef CONFIG_PCI_HCI
	// 20100120 Joseph: Add for 88CE normal chip.
	// Fill in zero to make firmware image to dword alignment.
	_FillDummy(bufferPtr, &size);
#endif

	pageNums = size / MAX_DLFW_PAGE_SIZE ;
	//RT_ASSERT((pageNums <= 4), ("Page numbers should not greater then 4 \n"));
	remainSize = size % MAX_DLFW_PAGE_SIZE;

	for (page = 0; page < pageNums; page++) {
		offset = page * MAX_DLFW_PAGE_SIZE;
		ret = _PageWrite(padapter, page, bufferPtr+offset, MAX_DLFW_PAGE_SIZE);

		if(ret == _FAIL) {
			printk("====>%s %d\n", __func__, __LINE__);
			goto exit;
		}
	}
	if (remainSize) {
		offset = pageNums * MAX_DLFW_PAGE_SIZE;
		page = pageNums;
		ret = _PageWrite(padapter, page, bufferPtr+offset, remainSize);

		if(ret == _FAIL) {
			printk("====>%s %d\n", __func__, __LINE__);
			goto exit;
		}
	}
	RT_TRACE(_module_hal_init_c_, _drv_info_, ("_WriteFW Done- for Normal chip.\n"));

exit:
	return ret;
}

void _8051Reset8723(PADAPTER padapter)
{
	u8 cpu_rst;
	u8 io_rst;

	io_rst = rtw_read8(padapter, REG_RSV_CTRL);
	rtw_write8(padapter, REG_RSV_CTRL, io_rst&(~BIT1));
	// Reset 8051(WLMCU) IO wrapper
	// 0x1c[8] = 0
	// Suggested by Isaac@SD1 and Gimmy@SD1, coding by Lucas@20130624
	io_rst = rtw_read8(padapter, REG_RSV_CTRL+1);
	io_rst &= ~BIT(0);
	rtw_write8(padapter, REG_RSV_CTRL+1, io_rst);

	cpu_rst = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
	cpu_rst &= ~BIT(2);
	rtw_write8(padapter, REG_SYS_FUNC_EN+1, cpu_rst);

	io_rst = rtw_read8(padapter, REG_RSV_CTRL);
	rtw_write8(padapter, REG_RSV_CTRL, io_rst&(~BIT1));
	// Enable 8051 IO wrapper	
	// 0x1c[8] = 1
	io_rst = rtw_read8(padapter, REG_RSV_CTRL+1);
	io_rst |= BIT(0);
	rtw_write8(padapter, REG_RSV_CTRL+1, io_rst);

	cpu_rst = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
	cpu_rst |= BIT(2);
	rtw_write8(padapter, REG_SYS_FUNC_EN+1, cpu_rst);

	DBG_8192C("%s: Finish\n", __FUNCTION__);
}

static s32 polling_fwdl_chksum(_adapter *adapter, u32 min_cnt, u32 timeout_ms)
{
	s32 ret = _FAIL;
	u32 value32;
	u32 start = rtw_get_current_time();
	u32 cnt = 0;

	/* polling CheckSum report */
	do {
		cnt++;
		value32 = rtw_read32(adapter, REG_MCUFWDL);
		if (value32 & FWDL_ChkSum_rpt || adapter->bSurpriseRemoved || adapter->bDriverStopped)
			break;
		rtw_yield_os();
	} while (rtw_get_passing_time_ms(start) < timeout_ms || cnt < min_cnt);

	if (!(value32 & FWDL_ChkSum_rpt)) {
		goto exit;
	}

	if (rtw_fwdl_test_trigger_chksum_fail())
		goto exit;

	ret = _SUCCESS;

exit:
	DBG_871X("%s: Checksum report %s! (%u, %dms), REG_MCUFWDL:0x%08x\n", __FUNCTION__
	, (ret==_SUCCESS)?"OK":"Fail", cnt, rtw_get_passing_time_ms(start), value32);

	return ret;
}

static s32 _FWFreeToGo(_adapter *adapter, u32 min_cnt, u32 timeout_ms)
{
	s32 ret = _FAIL;
	u32	value32;
	u32 start = rtw_get_current_time();
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
		rtw_yield_os();
	} while (rtw_get_passing_time_ms(start) < timeout_ms || cnt < min_cnt);

	if (!(value32 & WINTINI_RDY)) {
		goto exit;
	}

	if (rtw_fwdl_test_trigger_wintint_rdy_fail())
		goto exit;

	ret = _SUCCESS;

exit:
	DBG_871X("%s: Polling FW ready %s! (%u, %dms), REG_MCUFWDL:0x%08x\n", __FUNCTION__
		, (ret==_SUCCESS)?"OK":"Fail", cnt, rtw_get_passing_time_ms(start), value32);

	return ret;
}

#define IS_FW_81xxC(padapter)	(((GET_HAL_DATA(padapter))->FirmwareSignature & 0xFFF0) == 0x88C0)

void rtl8723b_FirmwareSelfReset(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8	u1bTmp;
	u8	Delay = 100;

	if (!(IS_FW_81xxC(padapter) &&
		  ((pHalData->FirmwareVersion < 0x21) ||
		   (pHalData->FirmwareVersion == 0x21 &&
		    pHalData->FirmwareSubVersion < 0x01)))) // after 88C Fw v33.1
	{
		//0x1cf=0x20. Inform 8051 to reset. 2009.12.25. tynli_test
		rtw_write8(padapter, REG_HMETFR+3, 0x20);

		u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
		while (u1bTmp & BIT2)
		{
			Delay--;
			if(Delay == 0)
				break;
			rtw_udelay_os(50);
			u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
		}
		RT_TRACE(_module_hal_init_c_, _drv_notice_, ("-%s: 8051 reset success (%d)\n", __FUNCTION__, Delay));

		if (Delay == 0)
		{
			RT_TRACE(_module_hal_init_c_, _drv_notice_, ("%s: Force 8051 reset!!!\n", __FUNCTION__));
			//force firmware reset
			u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
			rtw_write8(padapter, REG_SYS_FUNC_EN+1, u1bTmp&(~BIT2));
		}
	}
}

#ifdef CONFIG_FILE_FWIMG
extern char *rtw_fw_file_path;
extern char *rtw_fw_wow_file_path;
#ifdef CONFIG_MP_INCLUDED
extern char *rtw_fw_mp_bt_file_path;
#endif // CONFIG_MP_INCLUDED
u8 FwBuffer[FW_8723B_SIZE];
#endif // CONFIG_FILE_FWIMG

#ifdef CONFIG_MP_INCLUDED
int _WriteBTFWtoTxPktBuf8723B(
	IN		PADAPTER		Adapter,
	IN		PVOID			buffer,
	IN		u4Byte			FwBufLen,
	IN		u1Byte			times
	)
{
	int			rtStatus = _SUCCESS;
	//u4Byte				value32;
	//u1Byte				numHQ, numLQ, numPubQ;//, txpktbuf_bndy;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	//PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);
	u1Byte				BcnValidReg;
	u1Byte				count=0, DLBcnCount=0;
	pu1Byte 			FwbufferPtr = (pu1Byte)buffer;
	//PRT_TCB 			pTcb, ptempTcb;
	//PRT_TX_LOCAL_BUFFER pBuf;
	BOOLEAN 			bRecover=_FALSE;
	pu1Byte 			ReservedPagePacket = NULL;
	pu1Byte 			pGenBufReservedPagePacket = NULL;
	u4Byte				TotalPktLen,txpktbuf_bndy;
	//u1Byte				tmpReg422;
	//u1Byte				u1bTmp;
	u8 			*pframe;
	struct xmit_priv	*pxmitpriv = &(Adapter->xmitpriv);
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	u8 			txdesc_offset = TXDESC_OFFSET;
	u8			val8;
#if (DEV_BUS_TYPE == RT_PCI_INTERFACE)
	u8			u1bTmp;
#endif

#if 1//(DEV_BUS_TYPE == RT_PCI_INTERFACE)
	TotalPktLen = FwBufLen;
#else
	TotalPktLen = FwBufLen+pHalData->HWDescHeadLength;
#endif

	if((TotalPktLen+TXDESC_OFFSET) > MAX_CMDBUF_SZ)
	{
		DBG_871X(" WARNING %s => Total packet len = %d > MAX_CMDBUF_SZ:%d \n"
			,__FUNCTION__,(TotalPktLen+TXDESC_OFFSET),MAX_CMDBUF_SZ);
		return _FAIL;
	}

	pGenBufReservedPagePacket = rtw_zmalloc(TotalPktLen);//GetGenTempBuffer (Adapter, TotalPktLen);
	if (!pGenBufReservedPagePacket)
		return _FAIL;

	ReservedPagePacket = (u1Byte *)pGenBufReservedPagePacket;

	_rtw_memset(ReservedPagePacket, 0, TotalPktLen);

#if 1//(DEV_BUS_TYPE == RT_PCI_INTERFACE)
	_rtw_memcpy(ReservedPagePacket, FwbufferPtr, FwBufLen);

#else
	PlatformMoveMemory(ReservedPagePacket+Adapter->HWDescHeadLength , FwbufferPtr, FwBufLen);
#endif

	//---------------------------------------------------------
	// 1. Pause BCN
	//---------------------------------------------------------
	//Set REG_CR bit 8. DMA beacon by SW.
#if (DEV_BUS_TYPE == RT_PCI_INTERFACE)
	u1bTmp = PlatformEFIORead1Byte(Adapter, REG_CR+1);
	PlatformEFIOWrite1Byte(Adapter,  REG_CR+1, (u1bTmp|BIT0));
#else
	// Remove for temparaily because of the code on v2002 is not sync to MERGE_TMEP for USB/SDIO.
	// De not remove this part on MERGE_TEMP. by tynli.
	//pHalData->RegCR_1 |= (BIT0);
	//PlatformEFIOWrite1Byte(Adapter,  REG_CR+1, pHalData->RegCR_1);
#endif

	// Disable Hw protection for a time which revserd for Hw sending beacon.
	// Fix download reserved page packet fail that access collision with the protection time.
	// 2010.05.11. Added by tynli.
	val8 = rtw_read8(Adapter, REG_BCN_CTRL);
	val8 &= ~EN_BCN_FUNCTION;
	val8 |= DIS_TSF_UDT;
	rtw_write8(Adapter, REG_BCN_CTRL, val8);

#if 0//(DEV_BUS_TYPE == RT_PCI_INTERFACE)
	tmpReg422 = PlatformEFIORead1Byte(Adapter, REG_FWHW_TXQ_CTRL+2);
	if( tmpReg422&BIT6)
		bRecover = TRUE;
	PlatformEFIOWrite1Byte(Adapter, REG_FWHW_TXQ_CTRL+2,  tmpReg422&(~BIT6));
#else
	// Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame.
	if(pHalData->RegFwHwTxQCtrl & BIT(6))
		bRecover=_TRUE;
	PlatformEFIOWrite1Byte(Adapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl&(~BIT(6))));
	pHalData->RegFwHwTxQCtrl &= (~ BIT(6));
#endif

	//---------------------------------------------------------
	// 2. Adjust LLT table to an even boundary.
	//---------------------------------------------------------
#if 0//(DEV_BUS_TYPE == RT_SDIO_INTERFACE)
	txpktbuf_bndy = 10; // rsvd page start address should be an even value. 														
	rtStatus =	InitLLTTable8723BS(Adapter, txpktbuf_bndy);
	if(RT_STATUS_SUCCESS != rtStatus){
		DBG_8192C("_CheckWLANFwPatchBTFwReady_8723B(): Failed to init LLT!\n");
		return RT_STATUS_FAILURE;
	}
	
	// Init Tx boundary.
	PlatformEFIOWrite1Byte(Adapter, REG_DWBCN0_CTRL_8723B+1, (u1Byte)txpktbuf_bndy);	
#endif


	//---------------------------------------------------------
	// 3. Write Fw to Tx packet buffer by reseverd page.
	//---------------------------------------------------------
	do
	{
		// download rsvd page.
		// Clear beacon valid check bit.
		BcnValidReg = PlatformEFIORead1Byte(Adapter, REG_TDECTRL+2);
		PlatformEFIOWrite1Byte(Adapter, REG_TDECTRL+2, BcnValidReg&(~BIT(0)));

		//BT patch is big, we should set 0x209 < 0x40 suggested from Gimmy
		RT_TRACE(_module_mp_, _drv_info_,("0x209:%x\n",
					PlatformEFIORead1Byte(Adapter, REG_TDECTRL+1)));//209 < 0x40

		PlatformEFIOWrite1Byte(Adapter, REG_TDECTRL+1, (0x90-0x20*(times-1)));
		DBG_871X("0x209:0x%x\n", PlatformEFIORead1Byte(Adapter, REG_TDECTRL+1));
		RT_TRACE(_module_mp_, _drv_info_,("0x209:%x\n",
					PlatformEFIORead1Byte(Adapter, REG_TDECTRL+1)));

#if 0
		// Acquice TX spin lock before GetFwBuf and send the packet to prevent system deadlock.
		// Advertised by Roger. Added by tynli. 2010.02.22.
		PlatformAcquireSpinLock(Adapter, RT_TX_SPINLOCK);
		if(MgntGetFWBuffer(Adapter, &pTcb, &pBuf))
		{
			PlatformMoveMemory(pBuf->Buffer.VirtualAddress, ReservedPagePacket, TotalPktLen);
			CmdSendPacket(Adapter, pTcb, pBuf, TotalPktLen, DESC_PACKET_TYPE_NORMAL, FALSE);
		}
		else
			dbgdump("SetFwRsvdPagePkt(): MgntGetFWBuffer FAIL!!!!!!!!.\n");
		PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);
#else
		/*---------------------------------------------------------
		tx reserved_page_packet
		----------------------------------------------------------*/
			if ((pmgntframe = rtw_alloc_cmdxmitframe(pxmitpriv)) == NULL) {
					rtStatus = _FAIL;
					goto exit;
			}
			//update attribute
			pattrib = &pmgntframe->attrib;
			update_mgntframe_attrib(Adapter, pattrib);

			pattrib->qsel = QSLT_BEACON;
			pattrib->pktlen = pattrib->last_txcmdsz = FwBufLen ;

			//_rtw_memset(pmgntframe->buf_addr, 0, TotalPktLen+txdesc_size);
			//pmgntframe->buf_addr = ReservedPagePacket ;

			_rtw_memcpy( (u8*) (pmgntframe->buf_addr + txdesc_offset), ReservedPagePacket, FwBufLen);
			DBG_871X("[%d]===>TotalPktLen + TXDESC_OFFSET TotalPacketLen:%d \n", DLBcnCount, (FwBufLen + txdesc_offset));
			
#ifdef CONFIG_PCI_HCI
			dump_mgntframe(Adapter, pmgntframe);
#else
			dump_mgntframe_and_wait(Adapter, pmgntframe, 100);
#endif

#endif
#if 1
		// check rsvd page download OK.
		BcnValidReg = PlatformEFIORead1Byte(Adapter, REG_TDECTRL+2);
		while(!(BcnValidReg & BIT(0)) && count <200)
		{
			count++;
			//PlatformSleepUs(10);
			rtw_msleep_os(1);
			BcnValidReg = PlatformEFIORead1Byte(Adapter, REG_TDECTRL+2);
			RT_TRACE(_module_mp_, _drv_notice_,("Poll 0x20A = %x\n", BcnValidReg));
		}
		DLBcnCount++;
		//DBG_871X("##0x208:%08x,0x210=%08x\n",PlatformEFIORead4Byte(Adapter, REG_TDECTRL),PlatformEFIORead4Byte(Adapter, 0x210));

		PlatformEFIOWrite1Byte(Adapter, REG_TDECTRL+2,BcnValidReg);
		
	}while((!(BcnValidReg&BIT(0))) && DLBcnCount<5);


#endif
	if(DLBcnCount >=5){
		DBG_871X(" check rsvd page download OK DLBcnCount =%d  \n",DLBcnCount);
		rtStatus = _FAIL;
		goto exit;
	}

	if(!(BcnValidReg&BIT(0)))
	{
		DBG_871X("_WriteFWtoTxPktBuf(): 1 Download RSVD page failed!\n");
		rtStatus = _FAIL;
		goto exit;
	}

	//---------------------------------------------------------
	// 4. Set Tx boundary to the initial value
	//---------------------------------------------------------


	//---------------------------------------------------------
	// 5. Reset beacon setting to the initial value.
	//	 After _CheckWLANFwPatchBTFwReady().
	//---------------------------------------------------------

exit:

	if(pGenBufReservedPagePacket)
	{
		DBG_871X("_WriteBTFWtoTxPktBuf8723B => rtw_mfree pGenBufReservedPagePacket!\n");
		rtw_mfree((u8*)pGenBufReservedPagePacket, TotalPktLen);
		}
	return rtStatus;
}



//
// Description: Determine the contents of H2C BT_FW_PATCH Command sent to FW.
// 2011.10.20 by tynli
//
void
SetFwBTFwPatchCmd(
	IN PADAPTER	Adapter,
	IN u16		FwSize
	)
{
	u8 u1BTFwPatchParm[H2C_BT_FW_PATCH_LEN]={0};
	u8 addr0 = 0;
	u8 addr1 = 0xa0;
	u8 addr2 = 0x10;
	u8 addr3 = 0x80;
	
	RT_TRACE(_module_mp_, _drv_notice_,("SetFwBTFwPatchCmd(): FwSize = %d\n", FwSize));

	SET_8723B_H2CCMD_BT_FW_PATCH_SIZE(u1BTFwPatchParm, FwSize);
	SET_8723B_H2CCMD_BT_FW_PATCH_ADDR0(u1BTFwPatchParm, addr0);
	SET_8723B_H2CCMD_BT_FW_PATCH_ADDR1(u1BTFwPatchParm, addr1);
	SET_8723B_H2CCMD_BT_FW_PATCH_ADDR2(u1BTFwPatchParm, addr2);
	SET_8723B_H2CCMD_BT_FW_PATCH_ADDR3(u1BTFwPatchParm, addr3);

	FillH2CCmd8723B(Adapter, H2C_8723B_BT_FW_PATCH, H2C_BT_FW_PATCH_LEN, u1BTFwPatchParm);

	RT_TRACE(_module_mp_, _drv_notice_,("<----SetFwBTFwPatchCmd(): FwSize = %d \n", FwSize));
}

void
SetFwBTPwrCmd(
	IN PADAPTER	Adapter,
	IN u1Byte	PwrIdx
	)
{
	u1Byte		u1BTPwrIdxParm[H2C_FORCE_BT_TXPWR_LEN]={0};

	RT_TRACE(_module_mp_, _drv_info_,("SetFwBTPwrCmd(): idx = %d\n", PwrIdx));
	SET_8723B_H2CCMD_BT_PWR_IDX(u1BTPwrIdxParm, PwrIdx);

	RT_TRACE(_module_mp_, _drv_info_,("SetFwBTPwrCmd(): %x %x %x\n",
		u1BTPwrIdxParm[0],u1BTPwrIdxParm[1],u1BTPwrIdxParm[2]));

	FillH2CCmd8723B(Adapter, H2C_8723B_FORCE_BT_TXPWR, H2C_FORCE_BT_TXPWR_LEN, u1BTPwrIdxParm);
}

//
// Description: WLAN Fw will write BT Fw to BT XRAM and signal driver.
//
// 2011.10.20. by tynli.
//
int
_CheckWLANFwPatchBTFwReady(
	IN	PADAPTER			Adapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u4Byte	count=0;
	u1Byte	u1bTmp;
	int ret = _FAIL;

	//---------------------------------------------------------
	// Check if BT FW patch procedure is ready.
	//---------------------------------------------------------
	do{
		u1bTmp = PlatformEFIORead1Byte(Adapter, REG_HMEBOX_DBG_0_8723B);
		if((u1bTmp&BIT6) || (u1bTmp&BIT7))
		{
			ret = _SUCCESS;
			break;
		}

		count++;
		RT_TRACE(_module_mp_, _drv_info_,("0x88=%x, wait for 50 ms (%d) times.\n",
					u1bTmp, count));
		rtw_msleep_os(50); // 50ms
	}while(!((u1bTmp&BIT6) || (u1bTmp&BIT7)) && count < 50);

	RT_TRACE(_module_mp_, _drv_notice_,("_CheckWLANFwPatchBTFwReady():"
				" Polling ready bit 0x88[7] for %d times.\n", count));

	if(count >= 50)
	{
		RT_TRACE(_module_mp_, _drv_notice_,("_CheckWLANFwPatchBTFwReady():"
				" Polling ready bit 0x88[7] FAIL!!\n"));
	}

	//---------------------------------------------------------
	// Reset beacon setting to the initial value.
	//---------------------------------------------------------
#if 0//(DEV_BUS_TYPE == RT_PCI_INTERFACE)
	if(LLT_table_init(Adapter, FALSE, 0) == RT_STATUS_FAILURE)
	{
		dbgdump("Init self define for BT Fw patch LLT table fail.\n");
		//return RT_STATUS_FAILURE;
	}
#endif
	u1bTmp = rtw_read8(Adapter, REG_BCN_CTRL);
	u1bTmp |= EN_BCN_FUNCTION;
	u1bTmp &= ~DIS_TSF_UDT;
	rtw_write8(Adapter, REG_BCN_CTRL, u1bTmp);

	// To make sure that if there exists an adapter which would like to send beacon.
	// If exists, the origianl value of 0x422[6] will be 1, we should check this to
	// prevent from setting 0x422[6] to 0 after download reserved page, or it will cause
	// the beacon cannot be sent by HW.
	// 2010.06.23. Added by tynli.
#if 0//(DEV_BUS_TYPE == RT_PCI_INTERFACE)
	u1bTmp = PlatformEFIORead1Byte(Adapter, REG_FWHW_TXQ_CTRL+2);
	PlatformEFIOWrite1Byte(Adapter, REG_FWHW_TXQ_CTRL+2, (u1bTmp|BIT6));
#else
	PlatformEFIOWrite1Byte(Adapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl|BIT(6)));
	pHalData->RegFwHwTxQCtrl |= BIT(6);
#endif

	// Clear CR[8] or beacon packet will not be send to TxBuf anymore.
	u1bTmp = PlatformEFIORead1Byte(Adapter, REG_CR_8723B+1);
	PlatformEFIOWrite1Byte(Adapter, REG_CR_8723B+1, (u1bTmp&(~BIT0)));

	return ret;
}

int ReservedPage_Compare(PADAPTER Adapter,PRT_MP_FIRMWARE pFirmware,u32 BTPatchSize)
{
	u8 temp,ret,lastBTsz;
	u32 u1bTmp=0,address_start=0,count=0,i=0;
	u8	*myBTFwBuffer = NULL;

	myBTFwBuffer = rtw_zmalloc(BTPatchSize);
	if (myBTFwBuffer == NULL)
	{
		DBG_871X("%s can't be executed due to the failed malloc.\n", __FUNCTION__);
		Adapter->mppriv.bTxBufCkFail=_TRUE;
		return _FALSE;
	}
	
	temp=rtw_read8(Adapter,0x209);
	
	address_start=(temp*128)/8;
	
	rtw_write32(Adapter,0x140,0x00000000);
	rtw_write32(Adapter,0x144,0x00000000);
	rtw_write32(Adapter,0x148,0x00000000);

	rtw_write8(Adapter,0x106,0x69);
	
	for(i=0;i<(BTPatchSize/8);i++)
	{
		rtw_write32(Adapter,0x140,address_start+5+i) ;		  
			
		//polling until reg 0x140[23]=1;
		do{
			u1bTmp = rtw_read32(Adapter, 0x140);
			if(u1bTmp&BIT(23))
			{
				ret = _SUCCESS;
				break;
			}
			count++;
			DBG_871X("0x140=%x, wait for 10 ms (%d) times.\n",u1bTmp, count);
			rtw_msleep_os(10); // 10ms
		}while(!(u1bTmp&BIT(23)) && count < 50);
		
			myBTFwBuffer[i*8+0]=rtw_read8(Adapter, 0x144);
			myBTFwBuffer[i*8+1]=rtw_read8(Adapter, 0x145);
			myBTFwBuffer[i*8+2]=rtw_read8(Adapter, 0x146); 
			myBTFwBuffer[i*8+3]=rtw_read8(Adapter, 0x147);
			myBTFwBuffer[i*8+4]=rtw_read8(Adapter, 0x148);
			myBTFwBuffer[i*8+5]=rtw_read8(Adapter, 0x149);
			myBTFwBuffer[i*8+6]=rtw_read8(Adapter, 0x14a);
			myBTFwBuffer[i*8+7]=rtw_read8(Adapter, 0x14b);
	}
	
	rtw_write32(Adapter,0x140,address_start+5+BTPatchSize/8) ;			  

	lastBTsz=BTPatchSize%8;
	
	//polling until reg 0x140[23]=1;
	u1bTmp=0;
	count=0;
	do{
			u1bTmp = rtw_read32(Adapter, 0x140);
			if(u1bTmp&BIT(23))
			{
				ret = _SUCCESS;
				break;
			}
			count++;
			DBG_871X("0x140=%x, wait for 10 ms (%d) times.\n",u1bTmp, count);
			rtw_msleep_os(10); // 10ms
	}while(!(u1bTmp&BIT(23)) && count < 50);

	for(i=0;i<lastBTsz;i++)
	{
		myBTFwBuffer[(BTPatchSize/8)*8+i] = rtw_read8(Adapter, (0x144+i));

	}

	for(i=0;i<BTPatchSize;i++)
	{
		if(myBTFwBuffer[i]!= pFirmware->szFwBuffer[i])
		{
			DBG_871X(" In direct myBTFwBuffer[%d]=%x , pFirmware->szFwBuffer=%x\n",i, myBTFwBuffer[i],pFirmware->szFwBuffer[i]);
			Adapter->mppriv.bTxBufCkFail=_TRUE;
			break;
		}
	}

	if (myBTFwBuffer != NULL)
	{
		rtw_mfree(myBTFwBuffer, BTPatchSize);
	}

	return _TRUE;
}

/* As the size of bt firmware is more than 16k which is too big for some platforms, we divide it
 * into four parts to transfer. The last parameter of _WriteBTFWtoTxPktBuf8723B is used to indicate
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


	rtStatus = _SUCCESS;
	pBTFirmwareBuf = NULL;
	BTFirmwareLen = 0;

	//
	// Patch BT Fw. Download BT RAM code to Tx packet buffer.
	//
	if (padapter->bBTFWReady) {
		DBG_8192C("%s: BT Firmware is ready!!\n", __FUNCTION__);
		return _FAIL;
	}

#ifdef CONFIG_FILE_FWIMG
	if (rtw_is_file_readable(rtw_fw_mp_bt_file_path) == _TRUE)
	{
		DBG_8192C("%s: accquire MP BT FW from file:%s\n", __FUNCTION__, rtw_fw_mp_bt_file_path);

		rtStatus = rtw_retrive_from_file(rtw_fw_mp_bt_file_path, FwBuffer, FW_8723B_SIZE);
		BTFirmwareLen = rtStatus>=0?rtStatus:0;
		pBTFirmwareBuf = FwBuffer;
	}
	else
#endif // CONFIG_FILE_FWIMG
	{
#ifdef CONFIG_EMBEDDED_FWIMG
		DBG_8192C("%s: Download MP BT FW from header\n", __FUNCTION__);

		pBTFirmwareBuf = (u8*)Rtl8723BFwBTImgArray;
		BTFirmwareLen = Rtl8723BFwBTImgArrayLength;
		pFirmware->szFwBuffer = pBTFirmwareBuf;
		pFirmware->ulFwLength = BTFirmwareLen;
#endif // CONFIG_EMBEDDED_FWIMG
	}

	DBG_8192C("%s: MP BT Firmware size=%d\n", __FUNCTION__, BTFirmwareLen);

	// for h2c cam here should be set to  true
	padapter->bFWReady = _TRUE;

	download_time = (BTFirmwareLen + 4095) / 4096;
	DBG_8192C("%s: download_time is %d\n", __FUNCTION__, download_time);

	// Download BT patch Fw.
	for (i = (download_time-1); i >= 0; i--)
	{
		if (i == (download_time - 1))
		{
			rtStatus = _WriteBTFWtoTxPktBuf8723B(padapter, pBTFirmwareBuf+(4096*i), (BTFirmwareLen-(4096*i)), 1);
			DBG_8192C("%s: start %d, len %d, time 1\n", __FUNCTION__, 4096*i, BTFirmwareLen-(4096*i));
		}
		else
		{
			rtStatus = _WriteBTFWtoTxPktBuf8723B(padapter, pBTFirmwareBuf+(4096*i), 4096, (download_time-i));
			DBG_8192C("%s: start %d, len 4096, time %d\n", __FUNCTION__, 4096*i, download_time-i);
		}

		if (rtStatus != _SUCCESS)
		{
			DBG_8192C("%s: BT Firmware download to Tx packet buffer fail!\n", __FUNCTION__);
			padapter->bBTFWReady = _FALSE;
			return rtStatus;
		}
	}

	ReservedPage_Compare(padapter,pFirmware,BTFirmwareLen);

	padapter->bBTFWReady = _TRUE;
	SetFwBTFwPatchCmd(padapter, (u16)BTFirmwareLen);
	rtStatus = _CheckWLANFwPatchBTFwReady(padapter);

	DBG_8192C("<===%s: return %s!\n", __FUNCTION__, rtStatus==_SUCCESS?"SUCCESS":"FAIL");
	return rtStatus;
}
#endif // CONFIG_MP_INCLUDED

//
//	Description:
//		Download 8192C firmware code.
//
//
s32 rtl8723b_FirmwareDownload(PADAPTER padapter, BOOLEAN  bUsedWoWLANFw)
{
	s32	rtStatus = _SUCCESS;
	u8 write_fw = 0;
	u32 fwdl_start_time;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	s8 			R8723BFwImageFileName[] ={RTL8723B_FW_IMG};
	u8			*FwImage;
	u32			FwImageLen;
	u8			*pFwImageFileName;
#ifdef CONFIG_WOWLAN
	u8			*FwImageWoWLAN;
	u32			FwImageWoWLANLen;
#endif	
	u8			*pucMappedFile = NULL;
	PRT_FIRMWARE_8723B	pFirmware = NULL;
	PRT_FIRMWARE_8723B	pBTFirmware = NULL;
	PRT_8723B_FIRMWARE_HDR		pFwHdr = NULL;
	u8			*pFirmwareBuf;
	u32			FirmwareLen;
#ifdef CONFIG_FILE_FWIMG
	u8 *fwfilepath;
#endif // CONFIG_FILE_FWIMG
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);

	RT_TRACE(_module_hal_init_c_, _drv_info_, ("+%s\n", __FUNCTION__));
#ifdef CONFIG_WOWLAN
	RT_TRACE(_module_hal_init_c_, _drv_notice_, ("+%s, bUsedWoWLANFw:%d\n", __FUNCTION__,bUsedWoWLANFw));
#endif
	pFirmware = (PRT_FIRMWARE_8723B)rtw_zmalloc(sizeof(RT_FIRMWARE_8723B));

	if(!pFirmware)
	{
		rtStatus = _FAIL;
		goto exit;
	}

	{
			u8 tmp_ps=0, tmp_rf=0;
			tmp_ps=rtw_read8(padapter,0xa3);
			tmp_ps&=0xf8;
			tmp_ps|=0x02;
			//1. write 0xA3[:2:0] = 3b'010
			rtw_write8(padapter, 0xa3, tmp_ps);
			//2. read power_state = 0xA0[1:0]
			tmp_ps=rtw_read8(padapter,0xa0);
			tmp_ps&=0x03;
			if(tmp_ps != 0x01)
			{
				DBG_871X(FUNC_ADPT_FMT" tmp_ps=%x \n",FUNC_ADPT_ARG(padapter), tmp_ps);
				pdbgpriv->dbg_downloadfw_pwr_state_cnt++;
			}
	}

	rtw_btcoex_PreLoadFirmware(padapter);
	
#ifdef CONFIG_FILE_FWIMG
#ifdef CONFIG_WOWLAN
	if (bUsedWoWLANFw)
	{
		fwfilepath = rtw_fw_wow_file_path;
	}
	else
#endif // CONFIG_WOWLAN
	{
		fwfilepath = rtw_fw_file_path;
	}
#endif // CONFIG_FILE_FWIMG

#ifdef CONFIG_FILE_FWIMG
	if (rtw_is_file_readable(fwfilepath) == _TRUE)
	{
		DBG_8192C("%s accquire FW from file:%s\n", __FUNCTION__, fwfilepath);
		pFirmware->eFWSource = FW_SOURCE_IMG_FILE;
	}
	else
#endif // CONFIG_FILE_FWIMG
	{
#ifdef CONFIG_EMBEDDED_FWIMG
		pFirmware->eFWSource = FW_SOURCE_HEADER_FILE;
#else // !CONFIG_EMBEDDED_FWIMG
		pFirmware->eFWSource = FW_SOURCE_IMG_FILE; // We should decided by Reg.
#endif // !CONFIG_EMBEDDED_FWIMG
	}

	switch(pFirmware->eFWSource)
	{
		case FW_SOURCE_IMG_FILE:
#ifdef CONFIG_FILE_FWIMG
			rtStatus = rtw_retrive_from_file(fwfilepath, FwBuffer, FW_8723B_SIZE);
			pFirmware->ulFwLength = rtStatus>=0?rtStatus:0;
			pFirmware->szFwBuffer = FwBuffer;
#endif // CONFIG_FILE_FWIMG
			break;

		case FW_SOURCE_HEADER_FILE:
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
		if (bUsedWoWLANFw) {
			if (!pwrpriv->wowlan_ap_mode) {
				ODM_ConfigFWWithHeaderFile(&pHalData->odmpriv,
						CONFIG_FW_WoWLAN,
						(u8*)&pFirmware->szFwBuffer,
						&pFirmware->ulFwLength);

				DBG_8192C(" ===> %s fw: %s, size: %d\n",
						__FUNCTION__, "WoWLAN",
						pFirmware->ulFwLength);
			} else {
				ODM_ConfigFWWithHeaderFile(&pHalData->odmpriv,
						CONFIG_FW_AP_WoWLAN,
						(u8*)&pFirmware->szFwBuffer,
						&pFirmware->ulFwLength);

				DBG_8192C(" ===> %s fw: %s, size: %d\n",
						__FUNCTION__, "AP_WoWLAN",
						pFirmware->ulFwLength);
			}
		} else
#endif // CONFIG_WOWLAN
			{
				if(padapter->registrypriv.mp_mode ==0)
				{
	 				ODM_ConfigFWWithHeaderFile(&pHalData->odmpriv, CONFIG_FW_NIC,
						(u8*)&pFirmware->szFwBuffer, &pFirmware->ulFwLength);
		 			DBG_8192C("%s fw: %s, size: %d\n", __FUNCTION__, "FW_NIC", pFirmware->ulFwLength);
				}
				else
				{
					ODM_ConfigFWWithHeaderFile(&pHalData->odmpriv, CONFIG_FW_MP,
						(u8*)&pFirmware->szFwBuffer, &pFirmware->ulFwLength);
		 			DBG_8192C("%s fw: %s, size: %d\n", __FUNCTION__, "FW_MP", pFirmware->ulFwLength);
				}
			}
			break;
	}

	if (pFirmware->ulFwLength > FW_8723B_SIZE) {
		rtStatus = _FAIL;
		DBG_871X_LEVEL(_drv_emerg_, "Firmware size:%u exceed %u\n", pFirmware->ulFwLength, FW_8723B_SIZE);
		goto exit;
	}

	pFirmwareBuf = pFirmware->szFwBuffer;
	FirmwareLen = pFirmware->ulFwLength;

	// To Check Fw header. Added by tynli. 2009.12.04.
	pFwHdr = (PRT_8723B_FIRMWARE_HDR)pFirmwareBuf;

	pHalData->FirmwareVersion =  le16_to_cpu(pFwHdr->Version);
	pHalData->FirmwareSubVersion = le16_to_cpu(pFwHdr->Subversion);
	pHalData->FirmwareSignature = le16_to_cpu(pFwHdr->Signature);

	DBG_871X("%s: fw_ver=%x fw_subver=%04x sig=0x%x, Month=%02x, Date=%02x, Hour=%02x, Minute=%02x\n",
		  __FUNCTION__, pHalData->FirmwareVersion, pHalData->FirmwareSubVersion, pHalData->FirmwareSignature
		  ,pFwHdr->Month,pFwHdr->Date,pFwHdr->Hour,pFwHdr->Minute);

	if (IS_FW_HEADER_EXIST_8723B(pFwHdr))
	{
		DBG_871X("%s(): Shift for fw header!\n", __func__);
		// Shift 32 bytes for FW header
		pFirmwareBuf = pFirmwareBuf + 32;
		FirmwareLen = FirmwareLen - 32;
	}

	// Suggested by Filen. If 8051 is running in RAM code, driver should inform Fw to reset by itself,
	// or it will cause download Fw fail. 2010.02.01. by tynli.
	if (rtw_read8(padapter, REG_MCUFWDL) & RAM_DL_SEL) //8051 RAM code
	{
		rtw_write8(padapter, REG_MCUFWDL, 0x00);
		rtl8723b_FirmwareSelfReset(padapter);
	}

	_FWDownloadEnable(padapter, _TRUE);
	fwdl_start_time = rtw_get_current_time();
	while(!padapter->bDriverStopped && !padapter->bSurpriseRemoved
			&& (write_fw++ < 3 || rtw_get_passing_time_ms(fwdl_start_time) < 500))
	{
		/* reset FWDL chksum */
		rtw_write8(padapter, REG_MCUFWDL, rtw_read8(padapter, REG_MCUFWDL)|FWDL_ChkSum_rpt);

		rtStatus = _WriteFW(padapter, pFirmwareBuf, FirmwareLen);
		if (rtStatus != _SUCCESS)
			continue;

		rtStatus = polling_fwdl_chksum(padapter, 5, 50);
		if (rtStatus == _SUCCESS)
			break;
	}
	_FWDownloadEnable(padapter, _FALSE);
	if(_SUCCESS != rtStatus)
		goto fwdl_stat;

	rtStatus = _FWFreeToGo(padapter, 10, 200);
	if (_SUCCESS != rtStatus)
		goto fwdl_stat;

#ifdef CONFIG_MP_INCLUDED//BT_MP
	if (padapter->registrypriv.mp_mode == 1)
	{
		//rtw_write8(padapter, 0x81, rtw_read8(padapter, 0x81)|BIT0);
		DBG_871X("rtl8723b_FirmwareDownload go to FirmwareDownloadBT !\n");
		pBTFirmware = (PRT_FIRMWARE_8723B)rtw_zmalloc(sizeof(RT_FIRMWARE_8723B));
		if(!pBTFirmware)
		{
			rtStatus = _FAIL;
			goto exit;
		}
		FirmwareDownloadBT(padapter, (PRT_MP_FIRMWARE)pBTFirmware);
	}
#endif

fwdl_stat:
	DBG_871X("FWDL %s. write_fw:%u, %dms\n"
		, (rtStatus == _SUCCESS)?"success":"fail"
		, write_fw
		, rtw_get_passing_time_ms(fwdl_start_time)
	);

exit:
	if (pFirmware)
		rtw_mfree((u8*)pFirmware, sizeof(RT_FIRMWARE_8723B));
	if (pBTFirmware)
		rtw_mfree((u8*)pBTFirmware, sizeof(RT_FIRMWARE_8723B));
	DBG_871X(" <=== rtl8723b_FirmwareDownload()\n");
	return rtStatus;
}

void rtl8723b_InitializeFirmwareVars(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);

	// Init Fw LPS related.
	adapter_to_pwrctl(padapter)->bFwCurrentInPSMode = _FALSE;

	//Init H2C cmd.
	rtw_write8(padapter, REG_HMETFR, 0x0f);
	
	// Init H2C counter. by tynli. 2009.12.09.
	pHalData->LastHMEBoxNum = 0;
//	pHalData->H2CQueueHead = 0;
//	pHalData->H2CQueueTail = 0;
//	pHalData->H2CStopInsertQueue = _FALSE;
}

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
//===========================================

//
// Description: Prepare some information to Fw for WoWLAN.
//					(1) Download wowlan Fw.
//					(2) Download RSVD page packets.
//					(3) Enable AP offload if needed.
//
// 2011.04.12 by tynli.
//
VOID
SetFwRelatedForWoWLAN8723b(
		IN		PADAPTER			padapter,
		IN		u8					bHostIsGoingtoSleep
)
{
		int				status=_FAIL;
		HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
		u8				bRecover = _FALSE;
	//
	// 1. Before WoWLAN we need to re-download WoWLAN Fw.
	//
	status = rtl8723b_FirmwareDownload(padapter, bHostIsGoingtoSleep);
	if(status != _SUCCESS) {
		DBG_871X("SetFwRelatedForWoWLAN8723b(): Re-Download Firmware failed!!\n");
		return;
	} else {
		DBG_871X("SetFwRelatedForWoWLAN8723b(): Re-Download Firmware Success !!\n");
	}
	//
	// 2. Re-Init the variables about Fw related setting.
	//
	rtl8723b_InitializeFirmwareVars(padapter);
}
#endif //CONFIG_WOWLAN

//===========================================================
//				Efuse related code
//===========================================================
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


	DBG_8192C("%s: Efuse switch bank to %d\n", __FUNCTION__, bank);
	if (bPseudoTest)
	{
#ifdef HAL_EFUSE_MEMORY
		pEfuseHal->fakeEfuseBank = bank;
#else
		fakeEfuseBank = bank;
#endif
		bRet = _TRUE;
	}
	else
	{
		value32 = rtw_read32(padapter, EFUSE_TEST);
		bRet = _TRUE;
		switch (bank)
		{
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
	switch (type)
	{
		case TYPE_EFUSE_MAX_SECTION:
			{
				u8 *pMax_section;
				pMax_section = (u8*)pOut;

				if (efuseType == EFUSE_WIFI)
					*pMax_section = EFUSE_MAX_SECTION_8723B;
				else
					*pMax_section = EFUSE_BT_MAX_SECTION;
			}
			break;

		case TYPE_EFUSE_REAL_CONTENT_LEN:
			{
				u16 *pu2Tmp;
				pu2Tmp = (u16*)pOut;

				if (efuseType == EFUSE_WIFI)
					*pu2Tmp = EFUSE_REAL_CONTENT_LEN_8723B;
				else
					*pu2Tmp = EFUSE_BT_REAL_CONTENT_LEN;
			}
			break;

		case TYPE_AVAILABLE_EFUSE_BYTES_BANK:
			{
				u16	*pu2Tmp;
				pu2Tmp = (u16*)pOut;

				if (efuseType == EFUSE_WIFI)
					*pu2Tmp = (EFUSE_REAL_CONTENT_LEN_8723B-EFUSE_OOB_PROTECT_BYTES);
				else
					*pu2Tmp = (EFUSE_BT_REAL_BANK_CONTENT_LEN-EFUSE_PROTECT_BYTES_BANK);
			}
			break;

		case TYPE_AVAILABLE_EFUSE_BYTES_TOTAL:
			{
				u16 *pu2Tmp;
				pu2Tmp = (u16*)pOut;

				if (efuseType == EFUSE_WIFI)
					*pu2Tmp = (EFUSE_REAL_CONTENT_LEN_8723B-EFUSE_OOB_PROTECT_BYTES);
				else
					*pu2Tmp = (EFUSE_BT_REAL_CONTENT_LEN-(EFUSE_PROTECT_BYTES_BANK*3));
			}
			break;

		case TYPE_EFUSE_MAP_LEN:
			{
				u16 *pu2Tmp;
				pu2Tmp = (u16*)pOut;

				if (efuseType == EFUSE_WIFI)
					*pu2Tmp = EFUSE_MAX_MAP_LEN;
				else
					*pu2Tmp = EFUSE_BT_MAP_LEN;
			}
			break;

		case TYPE_EFUSE_PROTECT_BYTES_BANK:
			{
				u8 *pu1Tmp;
				pu1Tmp = (u8*)pOut;

				if (efuseType == EFUSE_WIFI)
					*pu1Tmp = EFUSE_OOB_PROTECT_BYTES;
				else
					*pu1Tmp = EFUSE_PROTECT_BYTES_BANK;
			}
			break;

		case TYPE_EFUSE_CONTENT_LEN_BANK:
			{
				u16 *pu2Tmp;
				pu2Tmp = (u16*)pOut;

				if (efuseType == EFUSE_WIFI)
					*pu2Tmp = EFUSE_REAL_CONTENT_LEN_8723B;
				else
					*pu2Tmp = EFUSE_BT_REAL_BANK_CONTENT_LEN;
			}
			break;

		default:
			{
				u8 *pu1Tmp;
				pu1Tmp = (u8*)pOut;
				*pu1Tmp = 0;
			}
			break;
	}
}

#define VOLTAGE_V25		0x03
#define LDOE25_SHIFT	28

//=================================================================
//	The following is for compile ok
//	That should be merged with the original in the future
//=================================================================
#define EFUSE_ACCESS_ON_8723			0x69	// For RTL8723 only.
#define EFUSE_ACCESS_OFF_8723			0x00	// For RTL8723 only.
#define REG_EFUSE_ACCESS_8723			0x00CF	// Efuse access protection for RTL8723

//=================================================================
static void Hal_BT_EfusePowerSwitch(
	PADAPTER	padapter,
	u8			bWrite,
	u8			PwrState)
{
	u8 tempval;
	if (PwrState == _TRUE)
	{
		// enable BT power cut
		// 0x6A[14] = 1
		tempval = rtw_read8(padapter, 0x6B);
		tempval |= BIT(6);
		rtw_write8(padapter, 0x6B, tempval);
		
		// Attention!! Between 0x6A[14] and 0x6A[15] setting need 100us delay
		// So don't wirte 0x6A[14]=1 and 0x6A[15]=0 together!
		rtw_usleep_os(100);
		// disable BT output isolation
		// 0x6A[15] = 0
		tempval = rtw_read8(padapter, 0x6B);
		tempval &= ~BIT(7);
		rtw_write8(padapter, 0x6B, tempval);
	}
	else
	{
		// enable BT output isolation
		// 0x6A[15] = 1
		tempval = rtw_read8(padapter, 0x6B);
		tempval |= BIT(7);
		rtw_write8(padapter, 0x6B, tempval);

		// Attention!! Between 0x6A[14] and 0x6A[15] setting need 100us delay
		// So don't wirte 0x6A[14]=1 and 0x6A[15]=0 together!

		// disable BT power cut
		// 0x6A[14] = 1
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


	if (PwrState == _TRUE)
	{
#ifdef CONFIG_SDIO_HCI
		// To avoid cannot access efuse regsiters after disable/enable several times during DTM test. 
		// Suggested by SD1 IsaacHsu. 2013.07.08, added by tynli. 
		tempval = rtw_read8(padapter, SDIO_LOCAL_BASE|SDIO_REG_HSUS_CTRL);
		if (tempval & BIT(0)) // SDIO local register is suspend
		{
			u8 count = 0;


			tempval &= ~BIT(0);
			rtw_write8(padapter, SDIO_LOCAL_BASE|SDIO_REG_HSUS_CTRL, tempval);

			// check 0x86[1:0]=10'2h, wait power state to leave suspend
 			do {
				tempval = rtw_read8(padapter, SDIO_LOCAL_BASE|SDIO_REG_HSUS_CTRL);
				tempval &= 0x3;
				if (tempval == 0x02)
					break;

				count++;
				if (count >= 100)
					break;

				rtw_mdelay_os(10);
			} while (1);

			if (count >= 100)
			{
				DBG_8192C(FUNC_ADPT_FMT ": Leave SDIO local register suspend fail! Local 0x86=%#X\n",
					FUNC_ADPT_ARG(padapter), tempval);
			}
			else
			{
				DBG_8192C(FUNC_ADPT_FMT ": Leave SDIO local register suspend OK! Local 0x86=%#X\n",
					FUNC_ADPT_ARG(padapter), tempval);
		}
		}
#endif // CONFIG_SDIO_HCI

		rtw_write8(padapter, REG_EFUSE_ACCESS_8723, EFUSE_ACCESS_ON_8723);	

		// Reset: 0x0000h[28], default valid
		tmpV16 =  rtw_read16(padapter, REG_SYS_FUNC_EN);
		if (!(tmpV16 & FEN_ELDR)) {
			tmpV16 |= FEN_ELDR ;
			rtw_write16(padapter, REG_SYS_FUNC_EN, tmpV16);
		}

		// Clock: Gated(0x0008h[5]) 8M(0x0008h[1]) clock from ANA, default valid
		tmpV16 = rtw_read16(padapter, REG_SYS_CLKR);
		if ((!(tmpV16 & LOADER_CLK_EN))  || (!(tmpV16 & ANA8M))) {
			tmpV16 |= (LOADER_CLK_EN | ANA8M) ;
			rtw_write16(padapter, REG_SYS_CLKR, tmpV16);
		}

		if (bWrite == _TRUE)
		{
			// Enable LDO 2.5V before read/write action
			tempval = rtw_read8(padapter, EFUSE_TEST+3);
			tempval &= 0x0F;
			tempval |= (VOLTAGE_V25 << 4);
			rtw_write8(padapter, EFUSE_TEST+3, (tempval | 0x80));

			//rtw_write8(padapter, REG_EFUSE_ACCESS, EFUSE_ACCESS_ON);
		}
	}
	else
	{
		rtw_write8(padapter, REG_EFUSE_ACCESS, EFUSE_ACCESS_OFF);

		if (bWrite == _TRUE) {
			// Disable LDO 2.5V after read/write action
			tempval = rtw_read8(padapter, EFUSE_TEST+3);
			rtw_write8(padapter, EFUSE_TEST+3, (tempval & 0x7F));
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
	u16	eFuse_Addr=0;
	u8	offset, wden;
	u8	efuseHeader, efuseExtHdr, efuseData;
	u16	i, total, used;
	u8	efuse_usage = 0;

	//DBG_871X("YJ: ====>%s():_offset=%d _size_byte=%d bPseudoTest=%d\n", __func__, _offset, _size_byte, bPseudoTest);
	//
	// Do NOT excess total size of EFuse table. Added by Roger, 2008.11.10.
	//
	if ((_offset+_size_byte) > EFUSE_MAX_MAP_LEN)
	{
		DBG_8192C("%s: Invalid offset(%#x) with read bytes(%#x)!!\n", __FUNCTION__, _offset, _size_byte);
		return;
	}

	efuseTbl = (u8*)rtw_malloc(EFUSE_MAX_MAP_LEN);
	if (efuseTbl == NULL)
	{
		DBG_8192C("%s: alloc efuseTbl fail!\n", __FUNCTION__);
		return;
	}
	// 0xff will be efuse default value instead of 0x00.
	_rtw_memset(efuseTbl, 0xFF, EFUSE_MAX_MAP_LEN);


#ifdef CONFIG_DEBUG
if(0)
{
	for(i=0; i<256; i++)
		//ReadEFuseByte(padapter, i, &efuseTbl[i], _FALSE);
		efuse_OneByteRead(padapter, i, &efuseTbl[i], _FALSE);
	DBG_871X("Efuse Content:\n");
	for(i=0; i<256; i++)
	{
		if (i % 16 == 0)
			printk("\n");
		printk("%02X ", efuseTbl[i]);
	}
	printk("\n");
}
#endif


	// switch bank back to bank 0 for later BT and wifi use.
	hal_EfuseSwitchToBank(padapter, 0, bPseudoTest);

	while (AVAILABLE_EFUSE_ADDR(eFuse_Addr))
	{
		//ReadEFuseByte(padapter, eFuse_Addr++, &efuseHeader, bPseudoTest);
		efuse_OneByteRead(padapter, eFuse_Addr++, &efuseHeader, bPseudoTest);
		if (efuseHeader == 0xFF)
		{
			DBG_8192C("%s: data end at address=%#x\n", __FUNCTION__, eFuse_Addr-1);
			break;
		}
		//DBG_8192C("%s: efuse[0x%X]=0x%02X\n", __FUNCTION__, eFuse_Addr-1, efuseHeader);

		// Check PG header for section num.
		if (EXT_HEADER(efuseHeader))		//extended header
		{
			offset = GET_HDR_OFFSET_2_0(efuseHeader);
			//DBG_8192C("%s: extended header offset=0x%X\n", __FUNCTION__, offset);

			//ReadEFuseByte(padapter, eFuse_Addr++, &efuseExtHdr, bPseudoTest);
			efuse_OneByteRead(padapter, eFuse_Addr++, &efuseExtHdr, bPseudoTest);
			//DBG_8192C("%s: efuse[0x%X]=0x%02X\n", __FUNCTION__, eFuse_Addr-1, efuseExtHdr);
			if (ALL_WORDS_DISABLED(efuseExtHdr))
			{
				continue;
			}

			offset |= ((efuseExtHdr & 0xF0) >> 1);
			wden = (efuseExtHdr & 0x0F);
		}
		else
		{
			offset = ((efuseHeader >> 4) & 0x0f);
			wden = (efuseHeader & 0x0f);
		}
		//DBG_8192C("%s: Offset=%d Worden=0x%X\n", __FUNCTION__, offset, wden);

		if (offset < EFUSE_MAX_SECTION_8723B)
		{
			u16 addr;
			// Get word enable value from PG header
//			DBG_8192C("%s: Offset=%d Worden=0x%X\n", __FUNCTION__, offset, wden);

			addr = offset * PGPKT_DATA_SIZE;
			for (i=0; i<EFUSE_MAX_WORD_UNIT; i++)
			{
				// Check word enable condition in the section
				if (!(wden & (0x01<<i)))
				{
					efuseData = 0;
					//ReadEFuseByte(padapter, eFuse_Addr++, &efuseData, bPseudoTest);
					efuse_OneByteRead(padapter, eFuse_Addr++, &efuseData, bPseudoTest);
//					DBG_8192C("%s: efuse[%#X]=0x%02X\n", __FUNCTION__, eFuse_Addr-1, efuseData);
					efuseTbl[addr] = efuseData;

					efuseData = 0;
					//ReadEFuseByte(padapter, eFuse_Addr++, &efuseData, bPseudoTest);
					efuse_OneByteRead(padapter, eFuse_Addr++, &efuseData, bPseudoTest);
//					DBG_8192C("%s: efuse[%#X]=0x%02X\n", __FUNCTION__, eFuse_Addr-1, efuseData);
					efuseTbl[addr+1] = efuseData;
				}
				addr += 2;
			}
		}
		else
		{
			DBG_8192C(KERN_ERR "%s: offset(%d) is illegal!!\n", __FUNCTION__, offset);
			eFuse_Addr += Efuse_CalculateWordCnts(wden)*2;
		}
	}

	// Copy from Efuse map to output pointer memory!!!
	for (i=0; i<_size_byte; i++)
		pbuf[i] = efuseTbl[_offset+i];

#ifdef CONFIG_DEBUG
if(1)
{
	DBG_871X("Efuse Realmap:\n");
	for(i=0; i<_size_byte; i++)
	{
		if (i % 16 == 0)
			printk("\n");
		printk("%02X ", pbuf[i]);
	}
	printk("\n");
}
#endif
	// Calculate Efuse utilization
	total = 0;
	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, &total, bPseudoTest);
	used = eFuse_Addr - 1;
	if (total)
		efuse_usage = (u8)((used*100)/total);
	else
		efuse_usage = 100;
	if (bPseudoTest)
	{
#ifdef HAL_EFUSE_MEMORY
		pEfuseHal->fakeEfuseUsedBytes = used;
#else
		fakeEfuseUsedBytes = used;
#endif
	}
	else
	{
		rtw_hal_set_hwreg(padapter, HW_VAR_EFUSE_BYTES, (u8*)&used);
		rtw_hal_set_hwreg(padapter, HW_VAR_EFUSE_USAGE, (u8*)&efuse_usage);
	}

	if (efuseTbl)
		rtw_mfree(efuseTbl, EFUSE_MAX_MAP_LEN);
}

static VOID
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


	//
	// Do NOT excess total size of EFuse table. Added by Roger, 2008.11.10.
	//
	if ((_offset+_size_byte) > EFUSE_BT_MAP_LEN)
	{
		DBG_8192C("%s: Invalid offset(%#x) with read bytes(%#x)!!\n", __FUNCTION__, _offset, _size_byte);
		return;
	}

	efuseTbl = rtw_malloc(EFUSE_BT_MAP_LEN);
	if (efuseTbl == NULL) {
		DBG_8192C("%s: efuseTbl malloc fail!\n", __FUNCTION__);
		return;
	}
	// 0xff will be efuse default value instead of 0x00.
	_rtw_memset(efuseTbl, 0xFF, EFUSE_BT_MAP_LEN);

	total = 0;
	EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_AVAILABLE_EFUSE_BYTES_BANK, &total, bPseudoTest);

	for (bank=1; bank<3; bank++) // 8723b Max bake 0~2
	{
		if (hal_EfuseSwitchToBank(padapter, bank, bPseudoTest) == _FALSE)
		{
			DBG_8192C("%s: hal_EfuseSwitchToBank Fail!!\n", __FUNCTION__);
			goto exit;
		}

		eFuse_Addr = 0;

		while (AVAILABLE_EFUSE_ADDR(eFuse_Addr))
		{
			//ReadEFuseByte(padapter, eFuse_Addr++, &efuseHeader, bPseudoTest);
			efuse_OneByteRead(padapter, eFuse_Addr++, &efuseHeader, bPseudoTest);
			if (efuseHeader == 0xFF) break;
			DBG_8192C("%s: efuse[%#X]=0x%02x (header)\n", __FUNCTION__, (((bank-1)*EFUSE_REAL_CONTENT_LEN_8723B)+eFuse_Addr-1), efuseHeader);

			// Check PG header for section num.
			if (EXT_HEADER(efuseHeader))		//extended header
			{
				offset = GET_HDR_OFFSET_2_0(efuseHeader);
				DBG_8192C("%s: extended header offset_2_0=0x%X\n", __FUNCTION__, offset);

				//ReadEFuseByte(padapter, eFuse_Addr++, &efuseExtHdr, bPseudoTest);
				efuse_OneByteRead(padapter, eFuse_Addr++, &efuseExtHdr, bPseudoTest);
				DBG_8192C("%s: efuse[%#X]=0x%02x (ext header)\n", __FUNCTION__, (((bank-1)*EFUSE_REAL_CONTENT_LEN_8723B)+eFuse_Addr-1), efuseExtHdr);
				if (ALL_WORDS_DISABLED(efuseExtHdr))
				{
					continue;
				}

				offset |= ((efuseExtHdr & 0xF0) >> 1);
				wden = (efuseExtHdr & 0x0F);
			}
			else
			{
				offset = ((efuseHeader >> 4) & 0x0f);
				wden = (efuseHeader & 0x0f);
			}

			if (offset < EFUSE_BT_MAX_SECTION)
			{
				u16 addr;

				// Get word enable value from PG header
				DBG_8192C("%s: Offset=%d Worden=%#X\n", __FUNCTION__, offset, wden);

				addr = offset * PGPKT_DATA_SIZE;
				for (i=0; i<EFUSE_MAX_WORD_UNIT; i++)
				{
					// Check word enable condition in the section
					if (!(wden & (0x01<<i)))
					{
						efuseData = 0;
						//ReadEFuseByte(padapter, eFuse_Addr++, &efuseData, bPseudoTest);
						efuse_OneByteRead(padapter, eFuse_Addr++, &efuseData, bPseudoTest);
						DBG_8192C("%s: efuse[%#X]=0x%02X\n", __FUNCTION__, eFuse_Addr-1, efuseData);
						efuseTbl[addr] = efuseData;

						efuseData = 0;
						//ReadEFuseByte(padapter, eFuse_Addr++, &efuseData, bPseudoTest);
						efuse_OneByteRead(padapter, eFuse_Addr++, &efuseData, bPseudoTest);
						DBG_8192C("%s: efuse[%#X]=0x%02X\n", __FUNCTION__, eFuse_Addr-1, efuseData);
						efuseTbl[addr+1] = efuseData;
					}
					addr += 2;
				}
			}
			else
			{
				DBG_8192C("%s: offset(%d) is illegal!!\n", __FUNCTION__, offset);
				eFuse_Addr += Efuse_CalculateWordCnts(wden)*2;
			}
		}

		if ((eFuse_Addr-1) < total)
		{
			DBG_8192C("%s: bank(%d) data end at %#x\n", __FUNCTION__, bank, eFuse_Addr-1);
			break;
		}
	}

	// switch bank back to bank 0 for later BT and wifi use.
	hal_EfuseSwitchToBank(padapter, 0, bPseudoTest);

	// Copy from Efuse map to output pointer memory!!!
	for (i=0; i<_size_byte; i++)
		pbuf[i] = efuseTbl[_offset+i];

	//
	// Calculate Efuse utilization.
	//
	EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, &total, bPseudoTest);
	used = (EFUSE_BT_REAL_BANK_CONTENT_LEN*(bank-1)) + eFuse_Addr - 1;
	DBG_8192C("%s: bank(%d) data end at %#x ,used =%d\n", __FUNCTION__, bank, eFuse_Addr-1,used);
	if (total)
		efuse_usage = (u8)((used*100)/total);
	else
		efuse_usage = 100;
	if (bPseudoTest)
	{
#ifdef HAL_EFUSE_MEMORY
		pEfuseHal->fakeBTEfuseUsedBytes = used;
#else
		fakeBTEfuseUsedBytes = used;
#endif
	}
	else
	{
		rtw_hal_set_hwreg(padapter, HW_VAR_EFUSE_BT_BYTES, (u8*)&used);
		rtw_hal_set_hwreg(padapter, HW_VAR_EFUSE_BT_USAGE, (u8*)&efuse_usage);
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
	u16	efuse_addr=0;
	u16 start_addr = 0; // for debug
	u8	hoffset=0, hworden=0;
	u8	efuse_data, word_cnts=0;
	u32 count = 0; // for debug


	if (bPseudoTest)
	{
#ifdef HAL_EFUSE_MEMORY
		efuse_addr = (u16)pEfuseHal->fakeEfuseUsedBytes;
#else
		efuse_addr = (u16)fakeEfuseUsedBytes;
#endif
	}
	else
	{
		rtw_hal_get_hwreg(padapter, HW_VAR_EFUSE_BYTES, (u8*)&efuse_addr);
	}
	start_addr = efuse_addr;
	DBG_8192C("%s: start_efuse_addr=0x%X\n", __FUNCTION__, efuse_addr);

	// switch bank back to bank 0 for later BT and wifi use.
	hal_EfuseSwitchToBank(padapter, 0, bPseudoTest);

#if 0 // for debug test
	efuse_OneByteRead(padapter, 0x1FF, &efuse_data, bPseudoTest);
	DBG_8192C(FUNC_ADPT_FMT ": efuse raw 0x1FF=0x%02X\n",
		FUNC_ADPT_ARG(padapter), efuse_data);
	efuse_data = 0xFF;
#endif // for debug test

	count = 0;
	while (AVAILABLE_EFUSE_ADDR(efuse_addr))
	{
#if 1
		if (efuse_OneByteRead(padapter, efuse_addr, &efuse_data, bPseudoTest) == _FALSE)
		{
			DBG_8192C(KERN_ERR "%s: efuse_OneByteRead Fail! addr=0x%X !!\n", __FUNCTION__, efuse_addr);
			goto error;
		}
#else
		ReadEFuseByte(padapter, efuse_addr, &efuse_data, bPseudoTest);
#endif

		if (efuse_data == 0xFF) break;

		if ((start_addr != 0) && (efuse_addr == start_addr))
		{
			count++;
			DBG_8192C(FUNC_ADPT_FMT ": [WARNING] efuse raw 0x%X=0x%02X not 0xFF!!(%d times)\n",
				FUNC_ADPT_ARG(padapter), efuse_addr, efuse_data, count);

			efuse_data = 0xFF;
			if (count < 4)
			{
				// try again!

				if (count > 2)
				{
					// try again form address 0
					efuse_addr = 0;
					start_addr = 0;
				}

				continue;
			}

			goto error;
		}

		if (EXT_HEADER(efuse_data))
		{
			hoffset = GET_HDR_OFFSET_2_0(efuse_data);
			efuse_addr++;
			efuse_OneByteRead(padapter, efuse_addr, &efuse_data, bPseudoTest);
			if (ALL_WORDS_DISABLED(efuse_data))
			{
				continue;
			}

			hoffset |= ((efuse_data & 0xF0) >> 1);
			hworden = efuse_data & 0x0F;
		}
		else
		{
			hoffset = (efuse_data>>4) & 0x0F;
			hworden = efuse_data & 0x0F;
		}

		word_cnts = Efuse_CalculateWordCnts(hworden);
		efuse_addr += (word_cnts*2)+1;
	}

	if (bPseudoTest)
	{
#ifdef HAL_EFUSE_MEMORY
		pEfuseHal->fakeEfuseUsedBytes = efuse_addr;
#else
		fakeEfuseUsedBytes = efuse_addr;
#endif
	}
	else
	{
		rtw_hal_set_hwreg(padapter, HW_VAR_EFUSE_BYTES, (u8*)&efuse_addr);
	}
	
	goto exit;

error:
	// report max size to prevent wirte efuse
	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, &efuse_addr, bPseudoTest);

exit:
	DBG_8192C("%s: CurrentSize=%d\n", __FUNCTION__, efuse_addr);

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
	u8	hoffset=0, hworden=0;
	u8	efuse_data, word_cnts=0;
	u16	retU2=0;
	u8 bContinual = _TRUE;


	if (bPseudoTest)
	{
#ifdef HAL_EFUSE_MEMORY
		btusedbytes = pEfuseHal->fakeBTEfuseUsedBytes;
#else
		btusedbytes = fakeBTEfuseUsedBytes;
#endif
	}
	else
	{
		btusedbytes = 0;
		rtw_hal_get_hwreg(padapter, HW_VAR_EFUSE_BT_BYTES, (u8*)&btusedbytes);
	}
	efuse_addr = (u16)((btusedbytes%EFUSE_BT_REAL_BANK_CONTENT_LEN));
	startBank = (u8)(1+(btusedbytes/EFUSE_BT_REAL_BANK_CONTENT_LEN));

	DBG_8192C("%s: start from bank=%d addr=0x%X\n", __FUNCTION__, startBank, efuse_addr);

	EFUSE_GetEfuseDefinition(padapter, EFUSE_BT, TYPE_AVAILABLE_EFUSE_BYTES_BANK, &retU2, bPseudoTest);

	for (bank=startBank; bank<3; bank++)
	{
		if (hal_EfuseSwitchToBank(padapter, bank, bPseudoTest) == _FALSE)
		{
			DBG_8192C(KERN_ERR "%s: switch bank(%d) Fail!!\n", __FUNCTION__, bank);
			//bank = EFUSE_MAX_BANK;
			break;
		}

		// only when bank is switched we have to reset the efuse_addr.
		if (bank != startBank)
			efuse_addr = 0;
#if 1

		while (AVAILABLE_EFUSE_ADDR(efuse_addr))
		{
			if (efuse_OneByteRead(padapter, efuse_addr, &efuse_data, bPseudoTest) == _FALSE)
			{
				DBG_8192C(KERN_ERR "%s: efuse_OneByteRead Fail! addr=0x%X !!\n", __FUNCTION__, efuse_addr);
				//bank = EFUSE_MAX_BANK;
				break;
			}
			DBG_8192C("%s: efuse_OneByteRead ! addr=0x%X !efuse_data=0x%X! bank =%d\n", __FUNCTION__, efuse_addr,efuse_data,bank);

			if (efuse_data == 0xFF) break;

			if (EXT_HEADER(efuse_data))
			{
				hoffset = GET_HDR_OFFSET_2_0(efuse_data);
				efuse_addr++;
				efuse_OneByteRead(padapter, efuse_addr, &efuse_data, bPseudoTest);
				DBG_8192C("%s: efuse_OneByteRead EXT_HEADER ! addr=0x%X !efuse_data=0x%X! bank =%d\n", __FUNCTION__, efuse_addr,efuse_data,bank);
			
				if (ALL_WORDS_DISABLED(efuse_data))
				{
					efuse_addr++;
					continue;
				}

//				hoffset = ((hoffset & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
				hoffset |= ((efuse_data & 0xF0) >> 1);
				hworden = efuse_data & 0x0F;
			}
			else
			{
				hoffset = (efuse_data>>4) & 0x0F;
				hworden =  efuse_data & 0x0F;
			}

			DBG_8192C(FUNC_ADPT_FMT": Offset=%d Worden=%#X\n",
				FUNC_ADPT_ARG(padapter), hoffset, hworden);

			word_cnts = Efuse_CalculateWordCnts(hworden);
			//read next header
			efuse_addr += (word_cnts*2)+1;
		}
#else	
	while (	bContinual && 
			efuse_OneByteRead(padapter, efuse_addr ,&efuse_data, bPseudoTest) && 
			AVAILABLE_EFUSE_ADDR(efuse_addr))
		{	
			if(efuse_data!=0xFF)
			{
				if((efuse_data&0x1F) == 0x0F)		//extended header
				{
					hoffset = efuse_data;
					efuse_addr++;
					efuse_OneByteRead(padapter, efuse_addr ,&efuse_data, bPseudoTest);
					if((efuse_data & 0x0F) == 0x0F)
					{
						efuse_addr++;
						continue;
					}
					else
					{
						hoffset = ((hoffset & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
						hworden = efuse_data & 0x0F;
					}
				}		
				else
				{
					hoffset = (efuse_data>>4) & 0x0F;
					hworden =  efuse_data & 0x0F;									
				}
				word_cnts = Efuse_CalculateWordCnts(hworden);
				//read next header							
				efuse_addr = efuse_addr + (word_cnts*2)+1;						
			}
			else
			{
				bContinual = _FALSE ;			
			}
		}
#endif			
	

		// Check if we need to check next bank efuse
		if (efuse_addr < retU2)
		{
			break;// don't need to check next bank.
		}
	}
#if 0
	retU2 = ((bank-1)*EFUSE_BT_REAL_BANK_CONTENT_LEN) + efuse_addr;
	if (bPseudoTest)
	{
#ifdef HAL_EFUSE_MEMORY
		pEfuseHal->fakeBTEfuseUsedBytes = retU2;
#else
		fakeBTEfuseUsedBytes = retU2;
#endif
	}
	else
	{
		rtw_hal_set_hwreg(padapter, HW_VAR_EFUSE_BT_BYTES, (u8*)&retU2);
	}
#else
	retU2 = ((bank-1)*EFUSE_BT_REAL_BANK_CONTENT_LEN)+efuse_addr;
	if(bPseudoTest)
	{
		pEfuseHal->fakeBTEfuseUsedBytes = retU2;
		//RT_DISP(FEEPROM, EFUSE_PG, ("Hal_EfuseGetCurrentSize_BT92C(), already use %u bytes\n", pEfuseHal->fakeBTEfuseUsedBytes));
	}
	else
	{
		pEfuseHal->BTEfuseUsedBytes = retU2;
		//RT_DISP(FEEPROM, EFUSE_PG, ("Hal_EfuseGetCurrentSize_BT92C(), already use %u bytes\n", pEfuseHal->BTEfuseUsedBytes));
	}
#endif

	DBG_8192C("%s: CurrentSize=%d\n", __FUNCTION__, retU2);
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


//	DBG_8192C("%s: efuse_addr=%#x word_en=%#x\n", __FUNCTION__, efuse_addr, word_en);
	_rtw_memset(tmpdata, 0xFF, PGPKT_DATA_SIZE);

	if (!(word_en & BIT(0)))
	{
		tmpaddr = start_addr;
		efuse_OneByteWrite(padapter, start_addr++, data[0], bPseudoTest);
		efuse_OneByteWrite(padapter, start_addr++, data[1], bPseudoTest);

		efuse_OneByteRead(padapter, tmpaddr, &tmpdata[0], bPseudoTest);
		efuse_OneByteRead(padapter, tmpaddr+1, &tmpdata[1], bPseudoTest);
		if ((data[0]!=tmpdata[0]) || (data[1]!=tmpdata[1])) {
			badworden &= (~BIT(0));
		}
	}
	if (!(word_en & BIT(1)))
	{
		tmpaddr = start_addr;
		efuse_OneByteWrite(padapter, start_addr++, data[2], bPseudoTest);
		efuse_OneByteWrite(padapter, start_addr++, data[3], bPseudoTest);

		efuse_OneByteRead(padapter, tmpaddr, &tmpdata[2], bPseudoTest);
		efuse_OneByteRead(padapter, tmpaddr+1, &tmpdata[3], bPseudoTest);
		if ((data[2]!=tmpdata[2]) || (data[3]!=tmpdata[3])) {
			badworden &= (~BIT(1));
		}
	}
	if (!(word_en & BIT(2)))
	{
		tmpaddr = start_addr;
		efuse_OneByteWrite(padapter, start_addr++, data[4], bPseudoTest);
		efuse_OneByteWrite(padapter, start_addr++, data[5], bPseudoTest);

		efuse_OneByteRead(padapter, tmpaddr, &tmpdata[4], bPseudoTest);
		efuse_OneByteRead(padapter, tmpaddr+1, &tmpdata[5], bPseudoTest);
		if ((data[4]!=tmpdata[4]) || (data[5]!=tmpdata[5])) {
			badworden &= (~BIT(2));
		}
	}
	if (!(word_en & BIT(3)))
	{
		tmpaddr = start_addr;
		efuse_OneByteWrite(padapter, start_addr++, data[6], bPseudoTest);
		efuse_OneByteWrite(padapter, start_addr++, data[7], bPseudoTest);

		efuse_OneByteRead(padapter, tmpaddr, &tmpdata[6], bPseudoTest);
		efuse_OneByteRead(padapter, tmpaddr+1, &tmpdata[7], bPseudoTest);
		if ((data[6]!=tmpdata[6]) || (data[7]!=tmpdata[7])) {
			badworden &= (~BIT(3));
		}
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
	u8	efuse_data, word_cnts=0;
	u16	efuse_addr=0;
	u8	hoffset=0, hworden=0;
	u8	i;
	u8	max_section = 0;
	s32	ret;


	if (data == NULL)
		return _FALSE;

	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_EFUSE_MAX_SECTION, &max_section, bPseudoTest);
	if (offset > max_section)
	{
		DBG_8192C("%s: Packet offset(%d) is illegal(>%d)!\n", __FUNCTION__, offset, max_section);
		return _FALSE;
	}

	_rtw_memset(data, 0xFF, PGPKT_DATA_SIZE);
	ret = _TRUE;

	//
	// <Roger_TODO> Efuse has been pre-programmed dummy 5Bytes at the end of Efuse by CP.
	// Skip dummy parts to prevent unexpected data read from Efuse.
	// By pass right now. 2009.02.19.
	//
	while (AVAILABLE_EFUSE_ADDR(efuse_addr))
	{
		if (efuse_OneByteRead(padapter, efuse_addr++, &efuse_data, bPseudoTest) == _FALSE)
		{
			ret = _FALSE;
			break;
		}

		if (efuse_data == 0xFF) break;

		if (EXT_HEADER(efuse_data))
		{
			hoffset = GET_HDR_OFFSET_2_0(efuse_data);
			efuse_OneByteRead(padapter, efuse_addr++, &efuse_data, bPseudoTest);
			if (ALL_WORDS_DISABLED(efuse_data))
			{
				DBG_8192C("%s: Error!! All words disabled!\n", __FUNCTION__);
				continue;
			}

			hoffset |= ((efuse_data & 0xF0) >> 1);
			hworden = efuse_data & 0x0F;
		}
		else
		{
			hoffset = (efuse_data>>4) & 0x0F;
			hworden =  efuse_data & 0x0F;
		}

		if (hoffset == offset)
		{
			for (i=0; i<EFUSE_MAX_WORD_UNIT; i++)
			{
				// Check word enable condition in the section
				if (!(hworden & (0x01<<i)))
				{
					//ReadEFuseByte(padapter, efuse_addr++, &efuse_data, bPseudoTest);
					efuse_OneByteRead(padapter, efuse_addr++, &efuse_data, bPseudoTest);
//					DBG_8192C("%s: efuse[%#X]=0x%02X\n", __FUNCTION__, efuse_addr+tmpidx, efuse_data);
					data[i*2] = efuse_data;

					//ReadEFuseByte(padapter, efuse_addr++, &efuse_data, bPseudoTest);
					efuse_OneByteRead(padapter, efuse_addr++, &efuse_data, bPseudoTest);
//					DBG_8192C("%s: efuse[%#X]=0x%02X\n", __FUNCTION__, efuse_addr+tmpidx, efuse_data);
					data[(i*2)+1] = efuse_data;
				}
			}
		}
		else
		{
			word_cnts = Efuse_CalculateWordCnts(hworden);
			efuse_addr += word_cnts*2;
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
	u16	max_available=0;
	u16 current_size;


	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, &max_available, bPseudoTest);
//	DBG_8192C("%s: max_available=%d\n", __FUNCTION__, max_available);

	current_size = Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest);
	if (current_size >= max_available)
	{
		DBG_8192C("%s: Error!! current_size(%d)>max_available(%d)\n", __FUNCTION__, current_size, max_available);
		return _FALSE;
	}
	return _TRUE;
}

static void
hal_EfuseConstructPGPkt(
	u8 				offset,
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
	u8	match_word_en = 0x0F;	// default all words are disabled
	u8	i;

	// check if the same words are enabled both target and current PG packet
	if (((pTargetPkt->word_en & BIT(0)) == 0) &&
		((pCurPkt->word_en & BIT(0)) == 0))
	{
		match_word_en &= ~BIT(0);				// enable word 0
	}
	if (((pTargetPkt->word_en & BIT(1)) == 0) &&
		((pCurPkt->word_en & BIT(1)) == 0))
	{
		match_word_en &= ~BIT(1);				// enable word 1
	}
	if (((pTargetPkt->word_en & BIT(2)) == 0) &&
		((pCurPkt->word_en & BIT(2)) == 0))
	{
		match_word_en &= ~BIT(2);				// enable word 2
	}
	if (((pTargetPkt->word_en & BIT(3)) == 0) &&
		((pCurPkt->word_en & BIT(3)) == 0))
	{
		match_word_en &= ~BIT(3);				// enable word 3
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
	u8 bRet=_FALSE;
	u8 i, efuse_data;

	for (i=0; i<(word_cnts*2); i++)
	{
		if (efuse_OneByteRead(pAdapter, (startAddr+i) ,&efuse_data, bPseudoTest) == _FALSE)
		{
			DBG_8192C("%s: efuse_OneByteRead FAIL!!\n", __FUNCTION__);
			bRet = _TRUE;
			break;
		}

		if (efuse_data != 0xFF)
		{
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
	u8	bRet=_FALSE;
	u16	startAddr=0, efuse_max_available_len=0, efuse_max=0;
	u8	efuse_data=0;
#if 0
	u8	i, cur_header=0;
	u8	new_wden=0, matched_wden=0, badworden=0;
	PGPKT_STRUCT	curPkt;
#endif


	EFUSE_GetEfuseDefinition(padapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, &efuse_max_available_len, bPseudoTest);
	EFUSE_GetEfuseDefinition(padapter, efuseType, TYPE_EFUSE_CONTENT_LEN_BANK, &efuse_max, bPseudoTest);

	if (efuseType == EFUSE_WIFI)
	{
		if (bPseudoTest)
		{
#ifdef HAL_EFUSE_MEMORY
			startAddr = (u16)pEfuseHal->fakeEfuseUsedBytes;
#else
			startAddr = (u16)fakeEfuseUsedBytes;
#endif
		}
		else
		{
			rtw_hal_get_hwreg(padapter, HW_VAR_EFUSE_BYTES, (u8*)&startAddr);
		}
	}
	else
	{
		if (bPseudoTest)
		{
#ifdef HAL_EFUSE_MEMORY
			startAddr = (u16)pEfuseHal->fakeBTEfuseUsedBytes;
#else
			startAddr = (u16)fakeBTEfuseUsedBytes;
#endif
		}
		else
		{
			rtw_hal_get_hwreg(padapter, HW_VAR_EFUSE_BT_BYTES, (u8*)&startAddr);
		}
	}
	startAddr %= efuse_max;
	DBG_8192C("%s: startAddr=%#X\n", __FUNCTION__, startAddr);

	while (1)
	{
		if (startAddr >= efuse_max_available_len)
		{
			bRet = _FALSE;
			DBG_8192C("%s: startAddr(%d) >= efuse_max_available_len(%d)\n",
				__FUNCTION__, startAddr, efuse_max_available_len);
			break;
		}

		if (efuse_OneByteRead(padapter, startAddr, &efuse_data, bPseudoTest) && (efuse_data!=0xFF))
		{
#if 1
			bRet = _FALSE;
			DBG_8192C("%s: Something Wrong! last bytes(%#X=0x%02X) is not 0xFF\n",
				__FUNCTION__, startAddr, efuse_data);
			break;
#else
			if (EXT_HEADER(efuse_data))
			{
				cur_header = efuse_data;
				startAddr++;
				efuse_OneByteRead(padapter, startAddr, &efuse_data, bPseudoTest);
				if (ALL_WORDS_DISABLED(efuse_data))
				{
					DBG_8192C("%s: Error condition, all words disabled!", __FUNCTION__);
					bRet = _FALSE;
					break;
				}
				else
				{
					curPkt.offset = ((cur_header & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
					curPkt.word_en = efuse_data & 0x0F;
				}
			}
			else
			{
				cur_header  =  efuse_data;
				curPkt.offset = (cur_header>>4) & 0x0F;
				curPkt.word_en = cur_header & 0x0F;
			}

			curPkt.word_cnts = Efuse_CalculateWordCnts(curPkt.word_en);
			// if same header is found but no data followed
			// write some part of data followed by the header.
			if ((curPkt.offset == pTargetPkt->offset) &&
				(hal_EfuseCheckIfDatafollowed(padapter, curPkt.word_cnts, startAddr+1, bPseudoTest) == _FALSE) &&
				wordEnMatched(pTargetPkt, &curPkt, &matched_wden) == _TRUE)
			{
				DBG_8192C("%s: Need to partial write data by the previous wrote header\n", __FUNCTION__);
				// Here to write partial data
				badworden = Efuse_WordEnableDataWrite(padapter, startAddr+1, matched_wden, pTargetPkt->data, bPseudoTest);
				if (badworden != 0x0F)
				{
					u32	PgWriteSuccess=0;
					// if write fail on some words, write these bad words again
					if (efuseType == EFUSE_WIFI)
						PgWriteSuccess = Efuse_PgPacketWrite(padapter, pTargetPkt->offset, badworden, pTargetPkt->data, bPseudoTest);
					else
						PgWriteSuccess = Efuse_PgPacketWrite_BT(padapter, pTargetPkt->offset, badworden, pTargetPkt->data, bPseudoTest);

					if (!PgWriteSuccess)
					{
						bRet = _FALSE;	// write fail, return
						break;
					}
				}
				// partial write ok, update the target packet for later use
				for (i=0; i<4; i++)
				{
					if ((matched_wden & (0x1<<i)) == 0)	// this word has been written
					{
						pTargetPkt->word_en |= (0x1<<i);	// disable the word
					}
				}
				pTargetPkt->word_cnts = Efuse_CalculateWordCnts(pTargetPkt->word_en);
			}
			// read from next header
			startAddr = startAddr + (curPkt.word_cnts*2) + 1;
#endif
		}
		else
		{
			// not used header, 0xff
			*pAddr = startAddr;
//			DBG_8192C("%s: Started from unused header offset=%d\n", __FUNCTION__, startAddr));
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
	u8	bRet=_FALSE;
	u8	pg_header=0, tmp_header=0;
	u16	efuse_addr=*pAddr;
	u8	repeatcnt=0;


//	DBG_8192C("%s\n", __FUNCTION__);
	pg_header = ((pTargetPkt->offset << 4) & 0xf0) | pTargetPkt->word_en;

	do {
		efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
		efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);
		if (tmp_header != 0xFF) break;
		if (repeatcnt++ > EFUSE_REPEAT_THRESHOLD_)
		{
			DBG_8192C("%s: Repeat over limit for pg_header!!\n", __FUNCTION__);
			return _FALSE;
		}
	} while (1);

	if (tmp_header != pg_header)
	{
		DBG_8192C(KERN_ERR "%s: PG Header Fail!!(pg=0x%02X read=0x%02X)\n", __FUNCTION__, pg_header, tmp_header);
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
	u16	efuse_addr, efuse_max_available_len=0;
	u8	pg_header=0, tmp_header=0;
	u8	repeatcnt=0;


//	DBG_8192C("%s\n", __FUNCTION__);
	EFUSE_GetEfuseDefinition(padapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_BANK, &efuse_max_available_len, bPseudoTest);

	efuse_addr = *pAddr;
	if (efuse_addr >= efuse_max_available_len)
	{
		DBG_8192C("%s: addr(%d) over avaliable(%d)!!\n", __FUNCTION__, efuse_addr, efuse_max_available_len);
		return _FALSE;
	}

	pg_header = ((pTargetPkt->offset & 0x07) << 5) | 0x0F;
//	DBG_8192C("%s: pg_header=0x%x\n", __FUNCTION__, pg_header);

	do {
		efuse_OneByteWrite(padapter, efuse_addr, pg_header, bPseudoTest);
		efuse_OneByteRead(padapter, efuse_addr, &tmp_header, bPseudoTest);
		if (tmp_header != 0xFF) break;
		if (repeatcnt++ > EFUSE_REPEAT_THRESHOLD_)
		{
			DBG_8192C("%s: Repeat over limit for pg_header!!\n", __FUNCTION__);
			return _FALSE;
		}
	} while (1);

	if (tmp_header != pg_header)
	{
		DBG_8192C(KERN_ERR "%s: PG Header Fail!!(pg=0x%02X read=0x%02X)\n", __FUNCTION__, pg_header, tmp_header);
		return _FALSE;
	}

	// to write ext_header
	efuse_addr++;
	pg_header = ((pTargetPkt->offset & 0x78) << 1) | pTargetPkt->word_en;

	do {
		efuse_OneByteWrite(padapter, efuse_addr, pg_header, bPseudoTest);
		efuse_OneByteRead(padapter, efuse_addr, &tmp_header, bPseudoTest);
		if (tmp_header != 0xFF) break;
		if (repeatcnt++ > EFUSE_REPEAT_THRESHOLD_)
		{
			DBG_8192C("%s: Repeat over limit for ext_header!!\n", __FUNCTION__);
			return _FALSE;
		}
	} while (1);

	if (tmp_header != pg_header)	//offset PG fail
	{
		DBG_8192C(KERN_ERR "%s: PG EXT Header Fail!!(pg=0x%02X read=0x%02X)\n", __FUNCTION__, pg_header, tmp_header);
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
	u8 bRet=_FALSE;

	if (pTargetPkt->offset >= EFUSE_MAX_SECTION_BASE)
	{
		bRet = hal_EfusePgPacketWrite2ByteHeader(padapter, efuseType, pAddr, pTargetPkt, bPseudoTest);
	}
	else
	{
		bRet = hal_EfusePgPacketWrite1ByteHeader(padapter, efuseType, pAddr, pTargetPkt, bPseudoTest);
	}

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
	badworden = Efuse_WordEnableDataWrite(pAdapter, efuse_addr+1, pTargetPkt->word_en, pTargetPkt->data, bPseudoTest);
	if (badworden != 0x0F)
	{
		DBG_8192C("%s: Fail!!\n", __FUNCTION__);
		return _FALSE;
	}

//	DBG_8192C("%s: ok\n", __FUNCTION__);
	return _TRUE;
}

static s32
Hal_EfusePgPacketWrite(
	PADAPTER	padapter,
	u8 			offset,
	u8			word_en,
	u8			*pData,
	u8			bPseudoTest)
{
	PGPKT_STRUCT targetPkt;
	u16 startAddr=0;
	u8 efuseType=EFUSE_WIFI;

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
	u8 			offset,
	u8			word_en,
	u8			*pData,
	u8			bPseudoTest)
{
	PGPKT_STRUCT targetPkt;
	u16 startAddr=0;
	u8 efuseType=EFUSE_BT;

	if(!hal_EfusePgCheckAvailableAddr(pAdapter, efuseType, bPseudoTest))
		return _FALSE;

	hal_EfuseConstructPGPkt(offset, word_en, pData, &targetPkt);

	if(!hal_EfusePartialWriteCheck(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return _FALSE;

	if(!hal_EfusePgPacketWriteHeader(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return _FALSE;

	if(!hal_EfusePgPacketWriteData(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return _FALSE;

	return _TRUE;
}


static void read_chip_version_8723b(PADAPTER padapter)
{
	u32				value32;	
	HAL_DATA_TYPE	*pHalData;
	pHalData = GET_HAL_DATA(padapter);

	value32 = rtw_read32(padapter, REG_SYS_CFG);
	pHalData->VersionID.ICType = CHIP_8723B;
	pHalData->VersionID.ChipType = ((value32 & RTL_ID) ? TEST_CHIP : NORMAL_CHIP);
	pHalData->VersionID.RFType = RF_TYPE_1T1R ;
	pHalData->VersionID.VendorType = ((value32 & VENDOR_ID) ? CHIP_VENDOR_UMC : CHIP_VENDOR_TSMC);
	pHalData->VersionID.CUTVersion = (value32 & CHIP_VER_RTL_MASK)>>CHIP_VER_RTL_SHIFT; // IC version (CUT)

	// For regulator mode. by tynli. 2011.01.14
	pHalData->RegulatorMode = ((value32 & SPS_SEL) ? RT_LDO_REGULATOR : RT_SWITCHING_REGULATOR);

	value32 = rtw_read32(padapter, REG_GPIO_OUTSTS);
	pHalData->VersionID.ROMVer = ((value32 & RF_RL_ID) >> 20);	// ROM code version.

	// For multi-function consideration. Added by Roger, 2010.10.06.
	pHalData->MultiFunc = RT_MULTI_FUNC_NONE;
	value32 = rtw_read32(padapter, REG_MULTI_FUNC_CTRL);
	pHalData->MultiFunc |= ((value32 & WL_FUNC_EN) ? RT_MULTI_FUNC_WIFI : 0);
	pHalData->MultiFunc |= ((value32 & BT_FUNC_EN) ? RT_MULTI_FUNC_BT : 0);
	pHalData->MultiFunc |= ((value32 & GPS_FUNC_EN) ? RT_MULTI_FUNC_GPS : 0);
	pHalData->PolarityCtl = ((value32 & WL_HWPDN_SL) ? RT_POLARITY_HIGH_ACT : RT_POLARITY_LOW_ACT);

	rtw_hal_config_rftype(padapter);

/*	// mark for chage to use efuse
	if( IS_B_CUT(pHalData->VersionID) || IS_C_CUT(pHalData->VersionID))
	{
		MSG_8192C(" IS_B/C_CUT SWR up 1 level !!!!!!!!!!!!!!!!!\n");
		PHY_SetMacReg(padapter, 0x14, BIT23|BIT22|BIT21|BIT20, 0x5); //MAC reg 0x14[23:20] = 4b'0101 (SWR 1.220V)
	}else if ( IS_D_CUT(pHalData->VersionID))
	{
		MSG_8192C(" IS_D_CUT SKIP SWR !!!!!!!!!!!!!!!!!\n");
	}
*/

#if 1
	dump_chip_info(pHalData->VersionID);
#endif

}


void rtl8723b_InitBeaconParameters(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u16 val16;
	u8 val8;


	val8 = DIS_TSF_UDT;
	val16 = val8 | (val8 << 8); // port0 and port1
#ifdef CONFIG_BT_COEXIST
	// Enable prot0 beacon function for PSTDMA
	val16 |= EN_BCN_FUNCTION;
#endif
	rtw_write16(padapter, REG_BCN_CTRL, val16);

	// TODO: Remove these magic number
	rtw_write16(padapter, REG_TBTT_PROHIBIT, 0x6404);// ms
	// Firmware will control REG_DRVERLYINT when power saving is enable,
	// so don't set this register on STA mode.
	if (check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE) == _FALSE)
		rtw_write8(padapter, REG_DRVERLYINT, DRIVER_EARLY_INT_TIME_8723B); // 5ms
	rtw_write8(padapter, REG_BCNDMATIM, BCN_DMA_ATIME_INT_TIME_8723B); // 2ms

	// Suggested by designer timchen. Change beacon AIFS to the largest number
	// beacause test chip does not contension before sending beacon. by tynli. 2009.11.03
	rtw_write16(padapter, REG_BCNTCFG, 0x660F);
	
	pHalData->RegBcnCtrlVal = rtw_read8(padapter, REG_BCN_CTRL);
	pHalData->RegTxPause = rtw_read8(padapter, REG_TXPAUSE); 
	pHalData->RegFwHwTxQCtrl = rtw_read8(padapter, REG_FWHW_TXQ_CTRL+2);
	pHalData->RegReg542 = rtw_read8(padapter, REG_TBTT_PROHIBIT+2);
	pHalData->RegCR_1 = rtw_read8(padapter, REG_CR+1);
}

void rtl8723b_InitBeaconMaxError(PADAPTER padapter, u8 InfraMode)
{
#ifdef CONFIG_ADHOC_WORKAROUND_SETTING
	rtw_write8(padapter, REG_BCN_MAX_ERR, 0xFF);
#else
	//rtw_write8(Adapter, REG_BCN_MAX_ERR, (InfraMode ? 0xFF : 0x10));
#endif
}

void	_InitBurstPktLen_8723BS(PADAPTER Adapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);

	rtw_write8(Adapter, 0x4c7,rtw_read8(Adapter, 0x4c7)|BIT(7)); //enable single pkt ampdu
	rtw_write8(Adapter, REG_RX_PKT_LIMIT_8723B, 0x18);		//for VHT packet length 11K
	rtw_write8(Adapter, REG_MAX_AGGR_NUM_8723B, 0x1F);
	rtw_write8(Adapter, REG_PIFS_8723B, 0x00);
	rtw_write8(Adapter, REG_FWHW_TXQ_CTRL_8723B, rtw_read8(Adapter, REG_FWHW_TXQ_CTRL)&(~BIT(7)));
	if(pHalData->AMPDUBurstMode)
	{
		rtw_write8(Adapter,REG_AMPDU_BURST_MODE_8723B,  0x5F);
	}
	rtw_write8(Adapter, REG_AMPDU_MAX_TIME_8723B, 0x70);

	// ARFB table 9 for 11ac 5G 2SS
	rtw_write32(Adapter, REG_ARFR0_8723B, 0x00000010);
	if(IS_NORMAL_CHIP(pHalData->VersionID))
		rtw_write32(Adapter, REG_ARFR0_8723B+4, 0xfffff000);
	else
		rtw_write32(Adapter, REG_ARFR0_8723B+4, 0x3e0ff000);

	// ARFB table 10 for 11ac 5G 1SS
	rtw_write32(Adapter, REG_ARFR1_8723B, 0x00000010);
	rtw_write32(Adapter, REG_ARFR1_8723B+4, 0x003ff000);
}

static void ResumeTxBeacon(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);


	// 2010.03.01. Marked by tynli. No need to call workitem beacause we record the value
	// which should be read from register to a global variable.

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("+ResumeTxBeacon\n"));

	pHalData->RegFwHwTxQCtrl |= BIT(6);
	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+1, 0xff);
	pHalData->RegReg542 |= BIT(0);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+2, pHalData->RegReg542);
}

static void StopTxBeacon(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);


	// 2010.03.01. Marked by tynli. No need to call workitem beacause we record the value
	// which should be read from register to a global variable.

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("+StopTxBeacon\n"));

	pHalData->RegFwHwTxQCtrl &= ~BIT(6);
	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+1, 0x64);
	pHalData->RegReg542 &= ~BIT(0);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+2, pHalData->RegReg542);

	CheckFwRsvdPageContent(padapter);  // 2010.06.23. Added by tynli.
}

static void _BeaconFunctionEnable(PADAPTER padapter, u8 Enable, u8 Linked)
{
	rtw_write8(padapter, REG_BCN_CTRL, DIS_TSF_UDT | EN_BCN_FUNCTION | DIS_BCNQ_SUB);
	rtw_write8(padapter, REG_RD_CTRL+1, 0x6F);
}

static void rtl8723b_SetBeaconRelatedRegisters(PADAPTER padapter)
{
	u8 val8;
	u32 value32;
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	u32 bcn_ctrl_reg;

	//reset TSF, enable update TSF, correcting TSF On Beacon

	//REG_BCN_INTERVAL
	//REG_BCNDMATIM
	//REG_ATIMWND
	//REG_TBTT_PROHIBIT
	//REG_DRVERLYINT
	//REG_BCN_MAX_ERR
	//REG_BCNTCFG //(0x510)
	//REG_DUAL_TSF_RST
	//REG_BCN_CTRL //(0x550)


	bcn_ctrl_reg = REG_BCN_CTRL;
#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->iface_type == IFACE_PORT1)
		bcn_ctrl_reg = REG_BCN_CTRL_1;
#endif

	//
	// ATIM window
	//
	rtw_write16(padapter, REG_ATIMWND, 2);

	//
	// Beacon interval (in unit of TU).
	//
	rtw_write16(padapter, REG_BCN_INTERVAL, pmlmeinfo->bcn_interval);

	rtl8723b_InitBeaconParameters(padapter);

	rtw_write8(padapter, REG_SLOT, 0x09);

	//
	// Reset TSF Timer to zero, added by Roger. 2008.06.24
	//
	value32 = rtw_read32(padapter, REG_TCR);
	value32 &= ~TSFRST;
	rtw_write32(padapter, REG_TCR, value32);

	value32 |= TSFRST;
	rtw_write32(padapter, REG_TCR, value32);

	// NOTE: Fix test chip's bug (about contention windows's randomness)
	if (check_fwstate(&padapter->mlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE|WIFI_AP_STATE) == _TRUE)
	{
		rtw_write8(padapter, REG_RXTSF_OFFSET_CCK, 0x50);
		rtw_write8(padapter, REG_RXTSF_OFFSET_OFDM, 0x50);
	}

	_BeaconFunctionEnable(padapter, _TRUE, _TRUE);

	ResumeTxBeacon(padapter);
	val8 = rtw_read8(padapter, bcn_ctrl_reg);
	val8 |= DIS_BCNQ_SUB;
	rtw_write8(padapter, bcn_ctrl_reg, val8);
}

void rtl8723b_GetHalODMVar(
	PADAPTER				Adapter,
	HAL_ODM_VARIABLE		eVariable,
	PVOID					pValue1,
	PVOID					pValue2)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T podmpriv = &pHalData->odmpriv;
	switch(eVariable){
		default:
			GetHalODMVar(Adapter,eVariable,pValue1,pValue2);
			break;
	}
}

void rtl8723b_SetHalODMVar(
	PADAPTER				Adapter,
	HAL_ODM_VARIABLE		eVariable,
	PVOID					pValue1,
	BOOLEAN					bSet)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T podmpriv = &pHalData->odmpriv;
	switch(eVariable){		
		default:
			SetHalODMVar(Adapter,eVariable,pValue1,bSet);
			break;
	}
}
void hal_notch_filter_8723b(_adapter *adapter, bool enable)
{
	if (enable) {
		DBG_871X("Enable notch filter\n");
		rtw_write8(adapter, rOFDM0_RxDSP+1, rtw_read8(adapter, rOFDM0_RxDSP+1) | BIT1);
	} else {
		DBG_871X("Disable notch filter\n");
		rtw_write8(adapter, rOFDM0_RxDSP+1, rtw_read8(adapter, rOFDM0_RxDSP+1) & ~BIT1);
	}
}

u8 rtl8723b_MRateIdxToARFRId(PADAPTER padapter, u8 rate_idx)
{
	u8 ret = 0;
	RT_RF_TYPE_DEF_E rftype = (RT_RF_TYPE_DEF_E)GET_RF_TYPE(padapter);
	switch(rate_idx){

	case RATR_INX_WIRELESS_NGB:
		if(rftype == RF_1T1R)
			ret = 1;
		else 
			ret = 0;
		break;

	case RATR_INX_WIRELESS_N:
	case RATR_INX_WIRELESS_NG:
		if(rftype == RF_1T1R)
			ret = 5;
		else
			ret = 4;
		break;

	case RATR_INX_WIRELESS_NB:
		if(rftype == RF_1T1R)
			ret = 3;
		else 
			ret = 2;
		break;

	case RATR_INX_WIRELESS_GB:
		ret = 6;
		break;

	case RATR_INX_WIRELESS_G:
		ret = 7;
		break;	

	case RATR_INX_WIRELESS_B:
		ret = 8;
		break;

	case RATR_INX_WIRELESS_MC:
		if(padapter->mlmeextpriv.cur_wireless_mode & WIRELESS_11BG_24N)
			ret = 6;
		else
			ret = 7;
		break;
	case RATR_INX_WIRELESS_AC_N:
		if(rftype == RF_1T1R)// || padapter->MgntInfo.VHTHighestOperaRate <= MGN_VHT1SS_MCS9)
			ret = 10;
		else
			ret = 9;
		break;

	default:
		ret = 0;
		break;
	}	

	return ret;
}

void UpdateHalRAMask8723B(PADAPTER padapter, u32 mac_id, u8 rssi_level)
{
	u32	mask,rate_bitmap;
	u8	shortGIrate = _FALSE;
	struct sta_info	*psta;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	DBG_871X("%s(): mac_id=%d rssi_level=%d\n", __func__, mac_id, rssi_level);

	if (mac_id >= NUM_STA) //CAM_SIZE
	{
		return;
	}

	psta = pmlmeinfo->FW_sta_info[mac_id].psta;
	if(psta == NULL)
	{
		return;
	}

	shortGIrate = query_ra_short_GI(psta);

	mask = psta->ra_mask;

	rate_bitmap = 0xffffffff;					
	rate_bitmap = ODM_Get_Rate_Bitmap(&pHalData->odmpriv,mac_id,mask,rssi_level);
	DBG_871X("%s => mac_id:%d, networkType:0x%02x, mask:0x%08x\n\t ==> rssi_level:%d, rate_bitmap:0x%08x\n",
			__FUNCTION__,mac_id,psta->wireless_mode,mask,rssi_level,rate_bitmap);

	mask &= rate_bitmap;

#ifdef CONFIG_BT_COEXIST
	rate_bitmap = rtw_btcoex_GetRaMask(padapter);
	mask &= ~rate_bitmap;
#endif // CONFIG_BT_COEXIST

#ifdef CONFIG_CMCC_TEST
#ifdef CONFIG_BT_COEXIST
	if(pmlmeext->cur_wireless_mode & WIRELESS_11G) {
		if (mac_id == 0) {
			DBG_871X("CMCC_BT update raid entry, mask=0x%x\n", mask);
			//mask &=0xffffffc0; //disable CCK & <12M OFDM rate for 11G mode for CMCC
			mask &=0xffffff00; //disable CCK & <24M OFDM rate for 11G mode for CMCC
			DBG_871X("CMCC_BT update raid entry, mask=0x%x\n", mask);
		}
	}
#endif
#endif

	if(pHalData->fw_ractrl == _TRUE)
	{
		rtl8723b_set_FwMacIdConfig_cmd(padapter, mac_id, psta->raid, psta->bw_mode, shortGIrate, mask);
	}

	//set correct initial date rate for each mac_id
	pHalData->INIDATA_RATE[mac_id] = psta->init_rate;
	DBG_871X("%s(): mac_id=%d raid=0x%x bw=%d mask=0x%x init_rate=0x%x\n", __func__, mac_id, psta->raid, psta->bw_mode, mask, psta->init_rate);
}

#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
void rtl8723b_cal_txdesc_chksum(struct tx_desc *ptxdesc)
{
	u16	*usPtr = (u16*)ptxdesc;
	u32 count;
	u32 index;
	u16 checksum = 0;


	// Clear first
	ptxdesc->txdw7 &= cpu_to_le32(0xffff0000);

	// checksume is always calculated by first 32 bytes,
	// and it doesn't depend on TX DESC length.
	// Thomas,Lucas@SD4,20130515
	count = 16;

	for (index = 0; index < count; index++) {
		checksum ^= le16_to_cpu(*(usPtr + index));
	}

	ptxdesc->txdw7 |= cpu_to_le32(checksum & 0x0000ffff);
}
#endif


//
// Description: In normal chip, we should send some packet to Hw which will be used by Fw
//			in FW LPS mode. The function is to fill the Tx descriptor of this packets, then
//			Fw can tell Hw to send these packet derectly.
// Added by tynli. 2009.10.15.
//
//type1:pspoll, type2:null
void rtl8723b_fill_fake_txdesc(
	PADAPTER	padapter,
	u8*			pDesc,
	u32			BufferLen,
	u8			IsPsPoll,
	u8			IsBTQosNull,
	u8			bDataFrame)
{
	// Clear all status
	_rtw_memset(pDesc, 0, TXDESC_SIZE);

	SET_TX_DESC_FIRST_SEG_8723B(pDesc, 1); //bFirstSeg;
	SET_TX_DESC_LAST_SEG_8723B(pDesc, 1); //bLastSeg;

	SET_TX_DESC_OFFSET_8723B(pDesc, 0x28); // Offset = 32

	SET_TX_DESC_PKT_SIZE_8723B(pDesc, BufferLen); // Buffer size + command header
	SET_TX_DESC_QUEUE_SEL_8723B(pDesc, QSLT_MGNT); // Fixed queue of Mgnt queue

	// Set NAVUSEHDR to prevent Ps-poll AId filed to be changed to error vlaue by Hw.
	if (_TRUE == IsPsPoll)
	{
		SET_TX_DESC_NAV_USE_HDR_8723B(pDesc, 1);
	}
	else
	{
		SET_TX_DESC_HWSEQ_EN_8723B(pDesc, 1); // Hw set sequence number
		SET_TX_DESC_HWSEQ_SEL_8723B(pDesc, 0);
	}

	if (_TRUE ==IsBTQosNull)
	{
		SET_TX_DESC_BT_INT_8723B(pDesc, 1);
	}

	SET_TX_DESC_USE_RATE_8723B(pDesc, 1); // use data rate which is set by Sw
	SET_TX_DESC_OWN_8723B((pu1Byte)pDesc, 1);

	SET_TX_DESC_TX_RATE_8723B(pDesc, DESC8723B_RATE1M);

	//
	// Encrypt the data frame if under security mode excepct null data. Suggested by CCW.
	//
	if (_TRUE ==bDataFrame)
	{
		u32 EncAlg;

		EncAlg = padapter->securitypriv.dot11PrivacyAlgrthm;
		switch (EncAlg)
		{
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

#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	// USB interface drop packet if the checksum of descriptor isn't correct.
	// Using this checksum can let hardware recovery from packet bulk out error (e.g. Cancel URC, Bulk out error.).
	rtl8723b_cal_txdesc_chksum((struct tx_desc*)pDesc);
#endif
}

void rtl8723b_set_hal_ops(struct hal_ops *pHalFunc)
{
	pHalFunc->dm_init = &rtl8723b_init_dm_priv;
	pHalFunc->dm_deinit = &rtl8723b_deinit_dm_priv;

	pHalFunc->read_chip_version = read_chip_version_8723b;

	pHalFunc->UpdateRAMaskHandler = &UpdateHalRAMask8723B;

	pHalFunc->set_bwmode_handler = &PHY_SetBWMode8723B;
	pHalFunc->set_channel_handler = &PHY_SwChnl8723B;
	pHalFunc->set_chnl_bw_handler = &PHY_SetSwChnlBWMode8723B;

	pHalFunc->set_tx_power_level_handler = &PHY_SetTxPowerLevel8723B;
	pHalFunc->get_tx_power_level_handler = &PHY_GetTxPowerLevel8723B;

	pHalFunc->hal_dm_watchdog = &rtl8723b_HalDmWatchDog;
#ifdef CONFIG_LPS_LCLK_WD_TIMER
	pHalFunc->hal_dm_watchdog_in_lps = &rtl8723b_HalDmWatchDog_in_LPS;
#endif

#ifdef CONFIG_C2H_PACKET_EN
	pHalFunc->SetHwRegHandlerWithBuf = &SetHwRegWithBuf8723B;
#endif // CONFIG_C2H_PACKET_EN
	
	pHalFunc->SetBeaconRelatedRegistersHandler = &rtl8723b_SetBeaconRelatedRegisters;

	pHalFunc->Add_RateATid = &rtl8723b_Add_RateATid;

	pHalFunc->run_thread= &rtl8723b_start_thread;
	pHalFunc->cancel_thread= &rtl8723b_stop_thread;

	pHalFunc->read_bbreg = &PHY_QueryBBReg_8723B;
	pHalFunc->write_bbreg = &PHY_SetBBReg_8723B;
	pHalFunc->read_rfreg = &PHY_QueryRFReg_8723B;
	pHalFunc->write_rfreg = &PHY_SetRFReg_8723B;

	// Efuse related function
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
	pHalFunc->sreset_xmit_status_check = &rtl8723b_sreset_xmit_status_check;
	pHalFunc->sreset_linked_status_check  = &rtl8723b_sreset_linked_status_check;
	pHalFunc->sreset_get_wifi_status  = &sreset_get_wifi_status;
	pHalFunc->sreset_inprogress= &sreset_inprogress;
#endif
	pHalFunc->GetHalODMVarHandler = &rtl8723b_GetHalODMVar;
	pHalFunc->SetHalODMVarHandler = &rtl8723b_SetHalODMVar;

#ifdef CONFIG_XMIT_THREAD_MODE
	pHalFunc->xmit_thread_handler = &hal_xmit_handler;
#endif
	pHalFunc->hal_notch_filter = &hal_notch_filter_8723b;

	pHalFunc->c2h_handler = c2h_handler_8723b;
	pHalFunc->c2h_id_filter_ccx = c2h_id_filter_ccx_8723b;

	pHalFunc->fill_h2c_cmd = &FillH2CCmd8723B;
	pHalFunc->fill_fake_txdesc = &rtl8723b_fill_fake_txdesc;
#ifdef CONFIG_WOWLAN
	pHalFunc->hal_set_wowlan_fw = &SetFwRelatedForWoWLAN8723b;
#endif
	pHalFunc->hal_get_tx_buff_rsvd_page_num = &GetTxBufferRsvdPageNum8723B;
}

void rtl8723b_InitAntenna_Selection(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	u8 val;


	pHalData = GET_HAL_DATA(padapter);

	val = rtw_read8(padapter, REG_LEDCFG2);
	// Let 8051 take control antenna settting
	val |= BIT(7); // DPDT_SEL_EN, 0x4C[23]
	rtw_write8(padapter, REG_LEDCFG2, val);
}

void rtl8723b_CheckAntenna_Selection(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	u8 val;


	pHalData = GET_HAL_DATA(padapter);

	val = rtw_read8(padapter, REG_LEDCFG2);
	// Let 8051 take control antenna settting
	if(!(val &BIT(7))){
		val |= BIT(7); // DPDT_SEL_EN, 0x4C[23]
		rtw_write8(padapter, REG_LEDCFG2, val);
	}
}
void rtl8723b_DeinitAntenna_Selection(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	u8 val;


	pHalData = GET_HAL_DATA(padapter);
	val = rtw_read8(padapter, REG_LEDCFG2);
	// Let 8051 take control antenna settting
	val &= ~BIT(7); // DPDT_SEL_EN, clear 0x4C[23]
	rtw_write8(padapter, REG_LEDCFG2, val);

}

void rtl8723b_init_default_value(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData; 
	u8 i;
	pHalData = GET_HAL_DATA(padapter);

	padapter->registrypriv.wireless_mode = WIRELESS_11BG_24N;

	// init default value
	pHalData->fw_ractrl = _FALSE;
	if (!adapter_to_pwrctl(padapter)->bkeepfwalive)
		pHalData->LastHMEBoxNum = 0;

	/* hal capability values */
	pHalData->macid_num = MACID_NUM_8723B;
	pHalData->cam_entry_num = CAM_ENTRY_NUM_8723B;

	//init phydm default value
	pHalData->bIQKInitialized = _FALSE;
	pHalData->odmpriv.RFCalibrateInfo.TM_Trigger = 0;//for IQK
	pHalData->odmpriv.RFCalibrateInfo.ThermalValue_HP_index = 0;
	for(i = 0; i < HP_THERMAL_NUM; i++)
		pHalData->odmpriv.RFCalibrateInfo.ThermalValue_HP[i] = 0;

	// init Efuse variables
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
	_rtw_memset(pHalData->EfuseHal.BTEfuseContent, 0xFF, EFUSE_MAX_BT_BANK*EFUSE_MAX_HW_SIZE);
	_rtw_memset(pHalData->EfuseHal.BTEfuseInitMap, 0xFF, EFUSE_BT_MAX_MAP_LEN);
	_rtw_memset(pHalData->EfuseHal.BTEfuseModifiedMap, 0xFF, EFUSE_BT_MAX_MAP_LEN);
	pHalData->EfuseHal.fakeBTEfuseUsedBytes = 0;
	_rtw_memset(pHalData->EfuseHal.fakeBTEfuseContent, 0xFF, EFUSE_MAX_BT_BANK*EFUSE_MAX_HW_SIZE);
	_rtw_memset(pHalData->EfuseHal.fakeBTEfuseInitMap, 0xFF, EFUSE_BT_MAX_MAP_LEN);
	_rtw_memset(pHalData->EfuseHal.fakeBTEfuseModifiedMap, 0xFF, EFUSE_BT_MAX_MAP_LEN);
#endif
}

u8 GetEEPROMSize8723B(PADAPTER padapter)
{
	u8 size = 0;
	u32	cr;

	cr = rtw_read16(padapter, REG_9346CR);
	// 6: EEPROM used is 93C46, 4: boot from E-Fuse.
	size = (cr & BOOT_FROM_EEPROM) ? 6 : 4;

	MSG_8192C("EEPROM type is %s\n", size==4 ? "E-FUSE" : "93C46");

	return size;
}

//-------------------------------------------------------------------------
//
// LLT R/W/Init function
//
//-------------------------------------------------------------------------
s32 rtl8723b_InitLLTTable(PADAPTER padapter)
{
	u32 start, passing_time;
	u32 val32;
	s32 ret;


	ret = _FAIL;

	val32 = rtw_read32(padapter, REG_AUTO_LLT);
	val32 |= BIT_AUTO_INIT_LLT;
	rtw_write32(padapter, REG_AUTO_LLT, val32);

	start = rtw_get_current_time();

	do {
		val32 = rtw_read32(padapter, REG_AUTO_LLT);
		if (!(val32 & BIT_AUTO_INIT_LLT))
		{
			ret = _SUCCESS;
			break;
		}

		passing_time = rtw_get_passing_time_ms(start);
		if (passing_time > 1000)
		{
			DBG_8192C("%s: FAIL!! REG_AUTO_LLT(0x%X)=%08x\n",
				__FUNCTION__, REG_AUTO_LLT, val32);
			break;
		}

		rtw_usleep_os(2);
	} while(1);

	return ret;
}

#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
void _DisableGPIO(PADAPTER	padapter)
{
/***************************************
j. GPIO_PIN_CTRL 0x44[31:0]=0x000		//
k.Value = GPIO_PIN_CTRL[7:0]
l. GPIO_PIN_CTRL 0x44[31:0] = 0x00FF0000 | (value <<8); //write external PIN level
m. GPIO_MUXCFG 0x42 [15:0] = 0x0780
n. LEDCFG 0x4C[15:0] = 0x8080
***************************************/
	u8	value8;
	u16	value16;
	u32	value32;
	u32	u4bTmp;


	//1. Disable GPIO[7:0]
	rtw_write16(padapter, REG_GPIO_PIN_CTRL+2, 0x0000);
	value32 = rtw_read32(padapter, REG_GPIO_PIN_CTRL) & 0xFFFF00FF;
	u4bTmp = value32 & 0x000000FF;
	value32 |= ((u4bTmp<<8) | 0x00FF0000);
	rtw_write32(padapter, REG_GPIO_PIN_CTRL, value32);

	
	//2. Disable GPIO[10:8]
	rtw_write8(padapter, REG_MAC_PINMUX_CFG, 0x00);
	value16 = rtw_read16(padapter, REG_GPIO_IO_SEL) & 0xFF0F;
	value8 = (u8) (value16&0x000F);
	value16 |= ((value8<<4) | 0x0780);
	rtw_write16(padapter, REG_GPIO_IO_SEL, value16);
	

	//3. Disable LED0 & 1
	rtw_write16(padapter, REG_LEDCFG0, 0x8080);
	
//	RT_TRACE(COMP_INIT, DBG_LOUD, ("======> Disable GPIO and LED.\n"));
} //end of _DisableGPIO()

void _DisableRFAFEAndResetBB8723B(PADAPTER padapter)
{
/**************************************
a.	TXPAUSE 0x522[7:0] = 0xFF             //Pause MAC TX queue
b.	RF path 0 offset 0x00 = 0x00            // disable RF
c. 	APSD_CTRL 0x600[7:0] = 0x40
d.	SYS_FUNC_EN 0x02[7:0] = 0x16		//reset BB state machine
e.	SYS_FUNC_EN 0x02[7:0] = 0x14		//reset BB state machine
***************************************/
    	u8 eRFPath = 0, value8 = 0;

	rtw_write8(padapter, REG_TXPAUSE, 0xFF);

	PHY_SetRFReg(padapter, (RF_PATH)eRFPath, 0x0, bMaskByte0, 0x0);

	value8 |= APSDOFF;
	rtw_write8(padapter, REG_APSD_CTRL, value8);//0x40

	// Set BB reset at first
	value8 = 0 ;
	value8 |= (FEN_USBD | FEN_USBA | FEN_BB_GLB_RSTn);
	rtw_write8(padapter, REG_SYS_FUNC_EN, value8 );//0x16

	// Set global reset.
	value8 &= ~FEN_BB_GLB_RSTn;
	rtw_write8(padapter, REG_SYS_FUNC_EN, value8); //0x14

	// 2010/08/12 MH We need to set BB/GLBAL reset to save power for SS mode.

//	RT_TRACE(COMP_INIT, DBG_LOUD, ("======> RF off and reset BB.\n"));
}

void _DisableRFAFEAndResetBB(PADAPTER padapter)
{
	_DisableRFAFEAndResetBB8723B(padapter);
}

void _ResetDigitalProcedure1_8723B(PADAPTER padapter, BOOLEAN bWithoutHWSM)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if (IS_FW_81xxC(padapter) && (pHalData->FirmwareVersion <= 0x20))
	{
		#if 0
/*****************************
		f.	SYS_FUNC_EN 0x03[7:0]=0x54		// reset MAC register, DCORE
		g.	MCUFWDL 0x80[7:0]=0				// reset MCU ready status
******************************/
	u32	value32 = 0;
		rtw_write8(padapter, REG_SYS_FUNC_EN+1, 0x54);
		rtw_write8(padapter, REG_MCUFWDL, 0);
		#else
		/*****************************
		f.	MCUFWDL 0x80[7:0]=0				// reset MCU ready status
		g.	SYS_FUNC_EN 0x02[10]= 0			// reset MCU register, (8051 reset)
		h.	SYS_FUNC_EN 0x02[15-12]= 5		// reset MAC register, DCORE
		i.     SYS_FUNC_EN 0x02[10]= 1			// enable MCU register, (8051 enable)
		******************************/
			u16 valu16 = 0;
			rtw_write8(padapter, REG_MCUFWDL, 0);

			valu16 = rtw_read16(padapter, REG_SYS_FUNC_EN);
			rtw_write16(padapter, REG_SYS_FUNC_EN, (valu16 & (~FEN_CPUEN)));//reset MCU ,8051

			valu16 = rtw_read16(padapter, REG_SYS_FUNC_EN)&0x0FFF;
			rtw_write16(padapter, REG_SYS_FUNC_EN, (valu16 |(FEN_HWPDN|FEN_ELDR)));//reset MAC

			valu16 = rtw_read16(padapter, REG_SYS_FUNC_EN);
			rtw_write16(padapter, REG_SYS_FUNC_EN, (valu16 | FEN_CPUEN));//enable MCU ,8051
		#endif
	}
	else
	{
		u8 retry_cnts = 0;

		// 2010/08/12 MH For USB SS, we can not stop 8051 when we are trying to
		// enter IPS/HW&SW radio off. For S3/S4/S5/Disable, we can stop 8051 because
		// we will init FW when power on again.
		//if(!pDevice->RegUsbSS)
		{	// If we want to SS mode, we can not reset 8051.
			if(rtw_read8(padapter, REG_MCUFWDL) & BIT1)
			{ //IF fw in RAM code, do reset


				if(padapter->bFWReady)
				{
					// 2010/08/25 MH Accordign to RD alfred's suggestion, we need to disable other
					// HRCV INT to influence 8051 reset.
					rtw_write8(padapter, REG_FWIMR, 0x20);
					// 2011/02/15 MH According to Alex's suggestion, close mask to prevent incorrect FW write operation.
					rtw_write8(padapter, REG_FTIMR, 0x00);
					rtw_write8(padapter, REG_FSIMR, 0x00);

					rtw_write8(padapter, REG_HMETFR+3, 0x20);//8051 reset by self

					while( (retry_cnts++ <100) && (FEN_CPUEN &rtw_read16(padapter, REG_SYS_FUNC_EN)))
					{
						rtw_udelay_os(50);//us
						// 2010/08/25 For test only We keep on reset 5051 to prevent fail.
						//rtw_write8(padapter, REG_HMETFR+3, 0x20);//8051 reset by self
					}
//					RT_ASSERT((retry_cnts < 100), ("8051 reset failed!\n"));

					if (retry_cnts >= 100)
					{
						// if 8051 reset fail we trigger GPIO 0 for LA
						//rtw_write32(	padapter,
						//						REG_GPIO_PIN_CTRL,
						//						0x00010100);
						// 2010/08/31 MH According to Filen's info, if 8051 reset fail, reset MAC directly.
						rtw_write8(padapter, REG_SYS_FUNC_EN+1, 0x50);	//Reset MAC and Enable 8051
						rtw_mdelay_os(10);
					}
//					else
//					RT_TRACE(COMP_INIT, DBG_LOUD, ("=====> 8051 reset success (%d) .\n",retry_cnts));
				}
			}
//			else
//			{
//				RT_TRACE(COMP_INIT, DBG_LOUD, ("=====> 8051 in ROM.\n"));
//			}
			rtw_write8(padapter, REG_SYS_FUNC_EN+1, 0x54);	//Reset MAC and Enable 8051
			rtw_write8(padapter, REG_MCUFWDL, 0);
		}
	}

	//if(pDevice->RegUsbSS)
		//bWithoutHWSM = TRUE;	// Sugest by Filen and Issau.

	if(bWithoutHWSM)
	{
		//HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(padapter);
	/*****************************
		Without HW auto state machine
	g.	SYS_CLKR 0x08[15:0] = 0x30A3			//disable MAC clock
	h.	AFE_PLL_CTRL 0x28[7:0] = 0x80			//disable AFE PLL
	i.	AFE_XTAL_CTRL 0x24[15:0] = 0x880F		//gated AFE DIG_CLOCK
	j.	SYS_ISO_CTRL 0x00[7:0] = 0xF9			// isolated digital to PON
	******************************/
		//rtw_write16(padapter, REG_SYS_CLKR, 0x30A3);
		//if(!pDevice->RegUsbSS)
		// 2011/01/26 MH SD4 Scott suggest to fix UNC-B cut bug.
		rtw_write16(padapter, REG_SYS_CLKR, 0x70A3);  //modify to 0x70A3 by Scott.
		rtw_write8(padapter, REG_AFE_PLL_CTRL, 0x80);
		rtw_write16(padapter, REG_AFE_XTAL_CTRL, 0x880F);
		//if(!pDevice->RegUsbSS)
			rtw_write8(padapter, REG_SYS_ISO_CTRL, 0xF9);
	}
	else
	{
		// Disable all RF/BB power
		rtw_write8(padapter, REG_RF_CTRL, 0x00);
	}
//	RT_TRACE(COMP_INIT, DBG_LOUD, ("======> Reset Digital.\n"));

}

void _ResetDigitalProcedure1(PADAPTER padapter, BOOLEAN bWithoutHWSM)
{
	_ResetDigitalProcedure1_8723B(padapter, bWithoutHWSM);
}

void _ResetDigitalProcedure2(PADAPTER padapter)
{
	//HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(padapter);
/*****************************
k.	SYS_FUNC_EN 0x03[7:0] = 0x44			// disable ELDR runction
l.	SYS_CLKR 0x08[15:0] = 0x3083			// disable ELDR clock
m.	SYS_ISO_CTRL 0x01[7:0] = 0x83			// isolated ELDR to PON
******************************/
	//rtw_write8(padapter, REG_SYS_FUNC_EN+1, 0x44); //marked by Scott.
	// 2011/01/26 MH SD4 Scott suggest to fix UNC-B cut bug.
	rtw_write16(padapter, REG_SYS_CLKR, 0x70a3); //modify to 0x70a3 by Scott.
	rtw_write8(padapter, REG_SYS_ISO_CTRL+1, 0x82); //modify to 0x82 by Scott.
}

void _DisableAnalog(PADAPTER padapter, BOOLEAN bWithoutHWSM)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	u16 value16 = 0;
	u8 value8 = 0;


	if (bWithoutHWSM)
	{
		/*****************************
		n.	LDOA15_CTRL 0x20[7:0] = 0x04		// disable A15 power
		o.	LDOV12D_CTRL 0x21[7:0] = 0x54		// disable digital core power
		r.	When driver call disable, the ASIC will turn off remaining clock automatically
		******************************/

		rtw_write8(padapter, REG_LDOA15_CTRL, 0x04);
		//rtw_write8(padapter, REG_LDOV12D_CTRL, 0x54);

		value8 = rtw_read8(padapter, REG_LDOV12D_CTRL);
		value8 &= (~LDV12_EN);
		rtw_write8(padapter, REG_LDOV12D_CTRL, value8);
//		RT_TRACE(COMP_INIT, DBG_LOUD, (" REG_LDOV12D_CTRL Reg0x21:0x%02x.\n",value8));
	}

	/*****************************
	h.	SPS0_CTRL 0x11[7:0] = 0x23			//enter PFM mode
	i.	APS_FSMCO 0x04[15:0] = 0x4802		// set USB suspend
	******************************/
	value8 = 0x23;

	rtw_write8(padapter, REG_SPS0_CTRL, value8);

	if(bWithoutHWSM)
	{
		//value16 |= (APDM_HOST | /*AFSM_HSUS |*/PFM_ALDN);
		// 2010/08/31 According to Filen description, we need to use HW to shut down 8051 automatically.
		// Becasue suspend operatione need the asistance of 8051 to wait for 3ms.
		value16 |= (APDM_HOST | AFSM_HSUS | PFM_ALDN);
	}
	else
	{
		value16 |= (APDM_HOST | AFSM_HSUS | PFM_ALDN);
	}

	rtw_write16(padapter, REG_APS_FSMCO, value16);//0x4802

	rtw_write8(padapter, REG_RSV_CTRL, 0x0e);

#if 0
	//tynli_test for suspend mode.
	if(!bWithoutHWSM){
		rtw_write8(padapter, 0xfe10, 0x19);
	}
#endif

//	RT_TRACE(COMP_INIT, DBG_LOUD, ("======> Disable Analog Reg0x04:0x%04x.\n",value16));
}

// HW Auto state machine
s32 CardDisableHWSM(PADAPTER padapter, u8 resetMCU)
{
	int rtStatus = _SUCCESS;


	if (padapter->bSurpriseRemoved){
		return rtStatus;
	}
	//==== RF Off Sequence ====
	_DisableRFAFEAndResetBB(padapter);

	//  ==== Reset digital sequence   ======
	_ResetDigitalProcedure1(padapter, _FALSE);

	//  ==== Pull GPIO PIN to balance level and LED control ======
	_DisableGPIO(padapter);

	//  ==== Disable analog sequence ===
	_DisableAnalog(padapter, _FALSE);

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("======> Card disable finished.\n"));

	return rtStatus;
}

// without HW Auto state machine
s32 CardDisableWithoutHWSM(PADAPTER padapter)
{
	s32 rtStatus = _SUCCESS;


	//RT_TRACE(COMP_INIT, DBG_LOUD, ("======> Card Disable Without HWSM .\n"));
	if (padapter->bSurpriseRemoved) {
		return rtStatus;
	}

	//==== RF Off Sequence ====
	_DisableRFAFEAndResetBB(padapter);

	//  ==== Reset digital sequence   ======
	_ResetDigitalProcedure1(padapter, _TRUE);

	//  ==== Pull GPIO PIN to balance level and LED control ======
	_DisableGPIO(padapter);

	//  ==== Reset digital sequence   ======
	_ResetDigitalProcedure2(padapter);

	//  ==== Disable analog sequence ===
	_DisableAnalog(padapter, _TRUE);

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("<====== Card Disable Without HWSM .\n"));
	return rtStatus;
}
#endif // CONFIG_USB_HCI || CONFIG_SDIO_HCI || CONFIG_GSPI_HCI

BOOLEAN 
Hal_GetChnlGroup8723B(
	IN	u8 Channel,
	OUT u8 *pGroup
	)
{
	BOOLEAN bIn24G=TRUE;

	if(Channel <= 14)
	{
		bIn24G=TRUE;

		if      (1  <= Channel && Channel <= 2 )   *pGroup = 0;
		else if (3  <= Channel && Channel <= 5 )   *pGroup = 1;
		else if (6  <= Channel && Channel <= 8 )   *pGroup = 2;
		else if (9  <= Channel && Channel <= 11)   *pGroup = 3;
		else if (12 <= Channel && Channel <= 14)   *pGroup = 4;
		else
		{
			RT_TRACE(_module_hci_hal_init_c_, _drv_notice_,("==>Hal_GetChnlGroup8723B in 2.4 G, but Channel %d in Group not found \n", Channel));
		}
	}
	else
	{
		bIn24G=FALSE;

		if      (36   <= Channel && Channel <=  42)   *pGroup = 0;
		else if (44   <= Channel && Channel <=  48)   *pGroup = 1;
		else if (50   <= Channel && Channel <=  58)   *pGroup = 2;
		else if (60   <= Channel && Channel <=  64)   *pGroup = 3;
		else if (100  <= Channel && Channel <= 106)   *pGroup = 4;
		else if (108  <= Channel && Channel <= 114)   *pGroup = 5;
		else if (116  <= Channel && Channel <= 122)   *pGroup = 6;
		else if (124  <= Channel && Channel <= 130)   *pGroup = 7;
		else if (132  <= Channel && Channel <= 138)   *pGroup = 8;
		else if (140  <= Channel && Channel <= 144)   *pGroup = 9;
		else if (149  <= Channel && Channel <= 155)   *pGroup = 10;
		else if (157  <= Channel && Channel <= 161)   *pGroup = 11;
		else if (165  <= Channel && Channel <= 171)   *pGroup = 12;
		else if (173  <= Channel && Channel <= 177)   *pGroup = 13;
		else
		{
			RT_TRACE(_module_hci_hal_init_c_, _drv_notice_,("==>Hal_GetChnlGroup8723B in 5G, but Channel %d in Group not found \n",Channel));
		}

	}
	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("<==Hal_GetChnlGroup8723B,  (%s) Channel = %d, Group =%d,\n", 
                                  (bIn24G) ? "2.4G" : "5G", Channel, *pGroup));
	return bIn24G;
}

void
Hal_InitPGData(
	PADAPTER	padapter,
	u8			*PROMContent)
{

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u32			i;
	u16			value16;

	if(_FALSE == pHalData->bautoload_fail_flag)
	{ // autoload OK.
//		if (IS_BOOT_FROM_EEPROM(padapter))
		if (_TRUE == pHalData->EepromOrEfuse)
		{
			// Read all Content from EEPROM or EFUSE.
			for(i = 0; i < HWSET_MAX_SIZE_8723B; i += 2)
			{
//				value16 = EF2Byte(ReadEEprom(pAdapter, (u2Byte) (i>>1)));
//				*((u16*)(&PROMContent[i])) = value16;
			}
		}
		else
		{
			// Read EFUSE real map to shadow.
			EFUSE_ShadowMapUpdate(padapter, EFUSE_WIFI, _FALSE);
			_rtw_memcpy((void*)PROMContent, (void*)pHalData->efuse_eeprom_data, HWSET_MAX_SIZE_8723B);
		}
	}
	else
	{//autoload fail
		RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("AutoLoad Fail reported from CR9346!!\n"));
//		pHalData->AutoloadFailFlag = _TRUE;
		//update to default value 0xFF
		if (_FALSE == pHalData->EepromOrEfuse)
			EFUSE_ShadowMapUpdate(padapter, EFUSE_WIFI, _FALSE);
		_rtw_memcpy((void*)PROMContent, (void*)pHalData->efuse_eeprom_data, HWSET_MAX_SIZE_8723B);
	}
}

void
Hal_EfuseParseIDCode(
	IN	PADAPTER	padapter,
	IN	u8			*hwinfo
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u16			EEPROMId;


	// Checl 0x8129 again for making sure autoload status!!
	EEPROMId = le16_to_cpu(*((u16*)hwinfo));
	if (EEPROMId != RTL_EEPROM_ID)
	{
		DBG_8192C("EEPROM ID(%#x) is invalid!!\n", EEPROMId);
		pHalData->bautoload_fail_flag = _TRUE;
	}
	else
	{
		pHalData->bautoload_fail_flag = _FALSE;
	}

	RT_TRACE(_module_hal_init_c_, _drv_notice_, ("EEPROM ID=0x%04x\n", EEPROMId));
}

static void
Hal_EEValueCheck(
	IN		u8		EEType,
	IN		PVOID		pInValue,
	OUT		PVOID		pOutValue
	)
{
	switch(EEType)
	{
		case EETYPE_TX_PWR:
			{
				u8	*pIn, *pOut;
				pIn = (u8*)pInValue;
				pOut = (u8*)pOutValue;
				if(*pIn <= 63)
				{
					*pOut = *pIn;
				}
				else
				{
					RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("EETYPE_TX_PWR, value=%d is invalid, set to default=0x%x\n",
						*pIn, EEPROM_Default_TxPowerLevel));
					*pOut = EEPROM_Default_TxPowerLevel;
				}
			}
			break;
		default:
			break;
	}
}

static u8
Hal_GetChnlGroup(
	IN	u8 chnl
	)
{
	u8	group=0;

	if (chnl < 3)			// Cjanel 1-3
		group = 0;
	else if (chnl < 9)		// Channel 4-9
		group = 1;
	else					// Channel 10-14
		group = 2;

	return group;
}

void 
Hal_ReadPowerValueFromPROM_8723B(
	IN	PADAPTER 		Adapter,
	IN	PTxPowerInfo24G	pwrInfo24G,
	IN	u8 				* PROMContent,
	IN	BOOLEAN			AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u4Byte rfPath, eeAddr=EEPROM_TX_PWR_INX_8723B, group,TxCount=0;

	_rtw_memset(pwrInfo24G, 0, sizeof(TxPowerInfo24G));	

	if(0xFF == PROMContent[eeAddr+1])  
		AutoLoadFail = TRUE;
	
	if(AutoLoadFail)
	{	
		DBG_871X("%s(): Use Default value!\n", __func__);
		for(rfPath = 0 ; rfPath < MAX_RF_PATH ; rfPath++)
		{
			//2.4G default value
			for(group = 0 ; group < MAX_CHNL_GROUP_24G; group++)
			{
				pwrInfo24G->IndexCCK_Base[rfPath][group] =	EEPROM_DEFAULT_24G_INDEX;
				pwrInfo24G->IndexBW40_Base[rfPath][group] =	EEPROM_DEFAULT_24G_INDEX;
			}
			for(TxCount=0;TxCount<MAX_TX_COUNT;TxCount++)
			{
				if(TxCount==0)
				{
					pwrInfo24G->BW20_Diff[rfPath][0] =	EEPROM_DEFAULT_24G_HT20_DIFF;
					pwrInfo24G->OFDM_Diff[rfPath][0] =	EEPROM_DEFAULT_24G_OFDM_DIFF;
				}
				else
				{
					pwrInfo24G->BW20_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
					pwrInfo24G->BW40_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
					pwrInfo24G->CCK_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
				}
			}
		}
		
		return;
	}	

	pHalData->bTXPowerDataReadFromEEPORM = TRUE;		//YJ,move,120316
	
	for(rfPath = 0 ; rfPath < MAX_RF_PATH ; rfPath++)
	{
		//2 2.4G default value
		for(group = 0 ; group < MAX_CHNL_GROUP_24G; group++)
		{
			pwrInfo24G->IndexCCK_Base[rfPath][group] =	PROMContent[eeAddr++];
			if(pwrInfo24G->IndexCCK_Base[rfPath][group] == 0xFF)
			{
				pwrInfo24G->IndexCCK_Base[rfPath][group] = EEPROM_DEFAULT_24G_INDEX;
			}
		}
		for(group = 0 ; group < MAX_CHNL_GROUP_24G-1; group++)
		{
			pwrInfo24G->IndexBW40_Base[rfPath][group] =	PROMContent[eeAddr++];
			if(pwrInfo24G->IndexBW40_Base[rfPath][group] == 0xFF)
				pwrInfo24G->IndexBW40_Base[rfPath][group] =	EEPROM_DEFAULT_24G_INDEX;
		}			
		for(TxCount=0;TxCount<MAX_TX_COUNT;TxCount++)
		{
			if(TxCount==0)
			{
				pwrInfo24G->BW40_Diff[rfPath][TxCount] = 0;
				pwrInfo24G->BW20_Diff[rfPath][TxCount] = (PROMContent[eeAddr]&0xf0)>>4;
				if (pwrInfo24G->BW20_Diff[rfPath][TxCount] & BIT3)		/*4bit sign number to 8 bit sign number*/
					pwrInfo24G->BW20_Diff[rfPath][TxCount] |= 0xF0;

				pwrInfo24G->OFDM_Diff[rfPath][TxCount] = (PROMContent[eeAddr]&0x0f);
				if (pwrInfo24G->OFDM_Diff[rfPath][TxCount] & BIT3)		/*4bit sign number to 8 bit sign number*/
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] |= 0xF0;

				pwrInfo24G->CCK_Diff[rfPath][TxCount] = 0;
				eeAddr++;
			} else{
				pwrInfo24G->BW40_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0xf0)>>4;
				if (pwrInfo24G->BW40_Diff[rfPath][TxCount] & BIT3)		/*4bit sign number to 8 bit sign number*/
					pwrInfo24G->BW40_Diff[rfPath][TxCount] |= 0xF0;

				pwrInfo24G->BW20_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0x0f);
				if (pwrInfo24G->BW20_Diff[rfPath][TxCount] & BIT3)		/*4bit sign number to 8 bit sign number*/
					pwrInfo24G->BW20_Diff[rfPath][TxCount] |= 0xF0;

				eeAddr++;

				pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0xf0)>>4;
				if (pwrInfo24G->OFDM_Diff[rfPath][TxCount] & BIT3)		/*4bit sign number to 8 bit sign number*/
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] |= 0xF0;


				pwrInfo24G->CCK_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0x0f);
				if (pwrInfo24G->CCK_Diff[rfPath][TxCount] & BIT3)		/*4bit sign number to 8 bit sign number*/
					pwrInfo24G->CCK_Diff[rfPath][TxCount] |= 0xF0;

				eeAddr++;
			}
		}

		/* Ignore the unnecessary 5G parameters parsing, but still consider the efuse address offset */
		#define	TX_PWR_DIFF_OFFSET_5G	10
		eeAddr += (MAX_CHNL_GROUP_5G + TX_PWR_DIFF_OFFSET_5G);
	}
}


void
Hal_EfuseParseTxPowerInfo_8723B(
	IN	PADAPTER 		padapter,
	IN	u8*			PROMContent,
	IN	BOOLEAN			AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	TxPowerInfo24G	pwrInfo24G;
	u8			rfPath, ch, group, TxCount=1;

//	RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("%s(): AutoLoadFail = %d\n", __func__, AutoLoadFail));
	Hal_ReadPowerValueFromPROM_8723B(padapter, &pwrInfo24G, PROMContent, AutoLoadFail);
	for(rfPath = 0 ; rfPath < MAX_RF_PATH ; rfPath++)
	{
		for(ch = 0 ; ch < CHANNEL_MAX_NUMBER; ch++)
		{
			Hal_GetChnlGroup8723B(ch+1, &group);
			
			if(ch == 14-1) 
			{
				pHalData->Index24G_CCK_Base[rfPath][ch] = pwrInfo24G.IndexCCK_Base[rfPath][5];
				pHalData->Index24G_BW40_Base[rfPath][ch] = pwrInfo24G.IndexBW40_Base[rfPath][group];
			}
			else
			{
				pHalData->Index24G_CCK_Base[rfPath][ch] = pwrInfo24G.IndexCCK_Base[rfPath][group];
				pHalData->Index24G_BW40_Base[rfPath][ch] = pwrInfo24G.IndexBW40_Base[rfPath][group];
			}
#ifdef CONFIG_DEBUG
			RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("======= Path %d, ChannelIndex %d, Group %d=======\n",rfPath,ch, group));			
			RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Index24G_CCK_Base[%d][%d] = 0x%x\n",rfPath,ch ,pHalData->Index24G_CCK_Base[rfPath][ch]));
			RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Index24G_BW40_Base[%d][%d] = 0x%x\n",rfPath,ch,pHalData->Index24G_BW40_Base[rfPath][ch]));			
#endif							
		}
		
		for(TxCount=0;TxCount<MAX_TX_COUNT;TxCount++)
		{
			pHalData->CCK_24G_Diff[rfPath][TxCount]=pwrInfo24G.CCK_Diff[rfPath][TxCount];
			pHalData->OFDM_24G_Diff[rfPath][TxCount]=pwrInfo24G.OFDM_Diff[rfPath][TxCount];
			pHalData->BW20_24G_Diff[rfPath][TxCount]=pwrInfo24G.BW20_Diff[rfPath][TxCount];
			pHalData->BW40_24G_Diff[rfPath][TxCount]=pwrInfo24G.BW40_Diff[rfPath][TxCount];
			
#ifdef CONFIG_DEBUG
			RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("--------------------------------------- 2.4G ---------------------------------------\n"));
			RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("CCK_24G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->CCK_24G_Diff[rfPath][TxCount]));
			RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("OFDM_24G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->OFDM_24G_Diff[rfPath][TxCount]));
			RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("BW20_24G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->BW20_24G_Diff[rfPath][TxCount]));
			RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("BW40_24G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->BW40_24G_Diff[rfPath][TxCount]));
#endif							
		}
	}

	// 2010/10/19 MH Add Regulator recognize for CU.
	if(!AutoLoadFail)
	{
		pHalData->EEPROMRegulatory = (PROMContent[EEPROM_RF_BOARD_OPTION_8723B]&0x7);	//bit0~2
		if(PROMContent[EEPROM_RF_BOARD_OPTION_8723B] == 0xFF)
			pHalData->EEPROMRegulatory = (EEPROM_DEFAULT_BOARD_OPTION&0x7);	//bit0~2
	}
	else
	{
		pHalData->EEPROMRegulatory = 0;
	}
	RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("EEPROMRegulatory = 0x%x\n", pHalData->EEPROMRegulatory));
}

VOID
Hal_EfuseParseBoardType_8723B(
	IN	PADAPTER	Adapter,	
	IN	u8*			PROMContent,
	IN	BOOLEAN		AutoloadFail
	)
{


	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	if(!AutoloadFail)
	{
		pHalData->InterfaceSel = (PROMContent[EEPROM_RF_BOARD_OPTION_8723B]&0xE0)>>5;
		if(PROMContent[EEPROM_RF_BOARD_OPTION_8723B] == 0xFF)
			pHalData->InterfaceSel = (EEPROM_DEFAULT_BOARD_OPTION&0xE0)>>5;
	}
	else
	{
		pHalData->InterfaceSel = 0;
	}
	RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("Board Type: 0x%2x\n", pHalData->InterfaceSel));

}

VOID
Hal_EfuseParseBTCoexistInfo_8723B(
	IN PADAPTER			padapter,
	IN u8*			hwinfo,
	IN BOOLEAN			AutoLoadFail
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8			tempval;
	u32			tmpu4;

//	RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("%s(): AutoLoadFail = %d\n", __func__, AutoLoadFail));
	if (!AutoLoadFail)
	{
		tmpu4 = rtw_read32(padapter, REG_MULTI_FUNC_CTRL);
		if (tmpu4 & BT_FUNC_EN)
			pHalData->EEPROMBluetoothCoexist = _TRUE;
		else
			pHalData->EEPROMBluetoothCoexist = _FALSE;

		pHalData->EEPROMBluetoothType = BT_RTL8723B;

		tempval = hwinfo[EEPROM_RF_BT_SETTING_8723B];
		if(tempval !=0xFF){
			pHalData->EEPROMBluetoothAntNum = tempval & BIT(0);
			#ifdef CONFIG_USB_HCI
			//if(padapter->interface_type == RTW_USB)
			pHalData->ant_path =ODM_RF_PATH_B;//s0
			#else //SDIO or PCIE
			// EFUSE_0xC3[6] == 0, S1(Main)-ODM_RF_PATH_A;
			// EFUSE_0xC3[6] == 1, S0(Aux)-ODM_RF_PATH_B
			pHalData->ant_path = (tempval & BIT(6))?ODM_RF_PATH_B:ODM_RF_PATH_A;
			#endif
		}
		else{
			pHalData->EEPROMBluetoothAntNum = Ant_x1;
			#ifdef CONFIG_USB_HCI
			pHalData->ant_path = ODM_RF_PATH_B;//s0
			#else
			pHalData->ant_path = ODM_RF_PATH_A;
			#endif			
		}
	}
	else
	{
		pHalData->EEPROMBluetoothCoexist = _FALSE;
		pHalData->EEPROMBluetoothType = BT_RTL8723B;
		pHalData->EEPROMBluetoothAntNum = Ant_x1;
		#ifdef CONFIG_USB_HCI
		pHalData->ant_path = ODM_RF_PATH_B;//s0
		#else
		pHalData->ant_path = ODM_RF_PATH_A;
		#endif
	}

#ifdef CONFIG_FOR_RTL8723BS_VQ0
	pHalData->ant_path = ODM_RF_PATH_B;//s0
#endif

#ifdef CONFIG_BT_COEXIST
	if (padapter->registrypriv.ant_num > 0) {
		DBG_8192C("%s: Apply driver defined antenna number(%d) to replace origin(%d)\n",
			__FUNCTION__,
			padapter->registrypriv.ant_num,
			pHalData->EEPROMBluetoothAntNum==Ant_x2?2:1);

		switch (padapter->registrypriv.ant_num) {
		case 1:
			pHalData->EEPROMBluetoothAntNum = Ant_x1;
			break;
		case 2:
			pHalData->EEPROMBluetoothAntNum = Ant_x2;
			break;
		default:
			DBG_8192C("%s: Discard invalid driver defined antenna number(%d)!\n",
				__FUNCTION__, padapter->registrypriv.ant_num);
			break;
		}
	}

	rtw_btcoex_SetBTCoexist(padapter, pHalData->EEPROMBluetoothCoexist);
	rtw_btcoex_SetChipType(padapter, pHalData->EEPROMBluetoothType);
	rtw_btcoex_SetPGAntNum(padapter, pHalData->EEPROMBluetoothAntNum==Ant_x2?2:1); 
	if (pHalData->EEPROMBluetoothAntNum == Ant_x1)
	{
		rtw_btcoex_SetSingleAntPath(padapter, pHalData->ant_path);
	}
#endif // CONFIG_BT_COEXIST

	DBG_8192C("%s: %s BT-coex, ant_num=%d\n",
		__FUNCTION__,
		pHalData->EEPROMBluetoothCoexist==_TRUE?"Enable":"Disable",
		pHalData->EEPROMBluetoothAntNum==Ant_x2?2:1);
}

VOID
Hal_EfuseParseEEPROMVer_8723B(
	IN	PADAPTER		padapter,
	IN	u8*			hwinfo,
	IN	BOOLEAN			AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

//	RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("%s(): AutoLoadFail = %d\n", __func__, AutoLoadFail));
	if(!AutoLoadFail)
		pHalData->EEPROMVersion = hwinfo[EEPROM_VERSION_8723B];
	else
		pHalData->EEPROMVersion = 1;
	RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("Hal_EfuseParseEEPROMVer(), EEVer = %d\n",
		pHalData->EEPROMVersion));
}



VOID
Hal_EfuseParsePackageType_8723B(
	IN	PADAPTER		pAdapter,
	IN	u8*				hwinfo,
	IN	BOOLEAN 	AutoLoadFail
	) 
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u1Byte			package;
	u8 efuseContent;
	
	Efuse_PowerSwitch(pAdapter, _FALSE, _TRUE);
	efuse_OneByteRead(pAdapter, 0x1FB, &efuseContent, FALSE);
	DBG_871X("%s phy efuse read 0x1FB =%x \n",__func__,efuseContent);
	Efuse_PowerSwitch(pAdapter, _FALSE, _FALSE);
	
	package = efuseContent & 0x7;
	switch (package) 
	{
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

	DBG_871X("PackageType = 0x%X\n", pHalData->PackageType);

#ifdef CONFIG_SDIO_HCI
	/* RTL8703AS: 0x1FB[5:4] 2b'10 */
	/* RTL8723BS: 0x1FB[5:4] 2b'11 */
	if ((efuseContent & 0x30) == 0x20) {
		pAdapter->registrypriv.bw_mode &= 0xF0;
		DBG_871X("This is the case of 8703AS\n");
	}
#endif	
}


VOID
Hal_EfuseParseVoltage_8723B(
	IN	PADAPTER		pAdapter,
	IN	u8* 			hwinfo,
	IN	BOOLEAN 	AutoLoadFail
	) 
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	
	//_rtw_memcpy(pHalData->adjuseVoltageVal, &hwinfo[EEPROM_Voltage_ADDR_8723B], 1);
	DBG_871X("%s hwinfo[EEPROM_Voltage_ADDR_8723B] =%02x \n",__func__, hwinfo[EEPROM_Voltage_ADDR_8723B]);
	pHalData->adjuseVoltageVal = (hwinfo[EEPROM_Voltage_ADDR_8723B] & 0xf0) >> 4 ;
	DBG_871X("%s pHalData->adjuseVoltageVal =%x \n",__func__,pHalData->adjuseVoltageVal);
}

VOID
Hal_EfuseParseChnlPlan_8723B(
	IN	PADAPTER		padapter,
	IN	u8*			hwinfo,
	IN	BOOLEAN			AutoLoadFail
	)
{
	padapter->mlmepriv.ChannelPlan = hal_com_config_channel_plan(
		padapter
		, hwinfo?hwinfo[EEPROM_ChannelPlan_8723B]:0xFF
		, padapter->registrypriv.channel_plan
		, RT_CHANNEL_DOMAIN_WORLD_NULL
		, AutoLoadFail
	);

	RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("EEPROM ChannelPlan=0x%02x\n", padapter->mlmepriv.ChannelPlan));
}

VOID
Hal_EfuseParseCustomerID_8723B(
	IN	PADAPTER		padapter,
	IN	u8*			hwinfo,
	IN	BOOLEAN			AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

//	RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("%s(): AutoLoadFail = %d\n", __func__, AutoLoadFail));
	if (!AutoLoadFail)
	{
		pHalData->EEPROMCustomerID = hwinfo[EEPROM_CustomID_8723B];
	}
	else
	{
		pHalData->EEPROMCustomerID = 0;
	}
	RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("EEPROM Customer ID: 0x%2x\n", pHalData->EEPROMCustomerID));
}

VOID
Hal_EfuseParseAntennaDiversity_8723B(
	IN	PADAPTER		pAdapter,
	IN	u8				* hwinfo,
	IN	BOOLEAN			AutoLoadFail
	)
{
#ifdef CONFIG_ANTENNA_DIVERSITY
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(pAdapter);
	struct registry_priv	*registry_par = &pAdapter->registrypriv;

	if (pHalData->EEPROMBluetoothAntNum == Ant_x1){
		pHalData->AntDivCfg = 0;
	}
	else{
		if(registry_par->antdiv_cfg == 2)// 0:OFF , 1:ON, 2:By EFUSE
			pHalData->AntDivCfg = 1;
		else
			pHalData->AntDivCfg = registry_par->antdiv_cfg;
	}

	// If TRxAntDivType is AUTO in advanced setting, use EFUSE value instead.
	if(registry_par->antdiv_type == 0) {
		pHalData->TRxAntDivType = hwinfo[EEPROM_RFE_OPTION_8723B];
		if (pHalData->TRxAntDivType == 0xFF)
			pHalData->TRxAntDivType = S0S1_SW_ANTDIV;//GetRegAntDivType(pAdapter);
		else if (pHalData->TRxAntDivType == 0x10)
			pHalData->TRxAntDivType = S0S1_SW_ANTDIV; //intrnal switch S0S1
		else if (pHalData->TRxAntDivType == 0x11)
			pHalData->TRxAntDivType = S0S1_SW_ANTDIV; //intrnal switch S0S1
		else
			DBG_8192C("%s: efuse[0x%x]=0x%02x is unknown type\n",
				__FUNCTION__, EEPROM_RFE_OPTION_8723B, pHalData->TRxAntDivType);
	}
	else{
		pHalData->TRxAntDivType = registry_par->antdiv_type ;//GetRegAntDivType(pAdapter);
	}

	DBG_8192C("%s: AntDivCfg=%d, AntDivType=%d\n",
		__FUNCTION__, pHalData->AntDivCfg, pHalData->TRxAntDivType);
#endif
}

VOID
Hal_EfuseParseXtal_8723B(
	IN	PADAPTER		pAdapter,
	IN	u8			* hwinfo,
	IN	BOOLEAN		AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

//	RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("%s(): AutoLoadFail = %d\n", __func__, AutoLoadFail));
	if(!AutoLoadFail)
	{
		pHalData->CrystalCap = hwinfo[EEPROM_XTAL_8723B];
		if(pHalData->CrystalCap == 0xFF)
			pHalData->CrystalCap = EEPROM_Default_CrystalCap_8723B;	   //what value should 8812 set?
	}
	else
	{
		pHalData->CrystalCap = EEPROM_Default_CrystalCap_8723B;
	}
	RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("EEPROM CrystalCap: 0x%2x\n", pHalData->CrystalCap));
}


void
Hal_EfuseParseThermalMeter_8723B(
	PADAPTER	padapter,
	u8			*PROMContent,
	u8			AutoLoadFail
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

//	RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("%s(): AutoLoadFail = %d\n", __func__, AutoLoadFail));
	//
	// ThermalMeter from EEPROM
	//
	if (_FALSE == AutoLoadFail)
		pHalData->EEPROMThermalMeter = PROMContent[EEPROM_THERMAL_METER_8723B];
	else
		pHalData->EEPROMThermalMeter = EEPROM_Default_ThermalMeter_8723B;

	if ((pHalData->EEPROMThermalMeter == 0xff) || (_TRUE == AutoLoadFail))
	{
		pHalData->odmpriv.RFCalibrateInfo.bAPKThermalMeterIgnore = _TRUE;
		pHalData->EEPROMThermalMeter = EEPROM_Default_ThermalMeter_8723B;
	}

	RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("EEPROM ThermalMeter=0x%x\n", pHalData->EEPROMThermalMeter));
}


#ifdef CONFIG_RF_GAIN_OFFSET
void Hal_ReadRFGainOffset(
	IN		PADAPTER		Adapter,
	IN		u8* 			PROMContent,
	IN		BOOLEAN 		AutoloadFail)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	//
	// BB_RF Gain Offset from EEPROM
	//

	if(!AutoloadFail ){
		pHalData->EEPROMRFGainOffset =PROMContent[EEPROM_RF_GAIN_OFFSET];
		DBG_871X("AutoloadFail =%x,\n", AutoloadFail);
		pHalData->EEPROMRFGainVal=EFUSE_Read1Byte(Adapter, EEPROM_RF_GAIN_VAL);
		DBG_871X("pHalData->EEPROMRFGainVal=%x\n", pHalData->EEPROMRFGainVal);
	}
	else{
		pHalData->EEPROMRFGainOffset = 0;
		pHalData->EEPROMRFGainVal=0xFF;
		DBG_871X("else AutoloadFail =%x,\n", AutoloadFail);
	}
	DBG_871X("EEPRORFGainOffset = 0x%02x\n", pHalData->EEPROMRFGainOffset);
}
#endif //CONFIG_RF_GAIN_OFFSET

u8 
BWMapping_8723B(
	IN	PADAPTER		Adapter,
	IN	struct pkt_attrib	*pattrib
)
{
	u8	BWSettingOfDesc = 0;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);

	//DBG_871X("BWMapping pHalData->CurrentChannelBW %d, pattrib->bwmode %d \n",pHalData->CurrentChannelBW,pattrib->bwmode);

	if(pHalData->CurrentChannelBW== CHANNEL_WIDTH_80)
	{
		if(pattrib->bwmode == CHANNEL_WIDTH_80)
			BWSettingOfDesc= 2;
		else if(pattrib->bwmode == CHANNEL_WIDTH_40)
			BWSettingOfDesc = 1;
		else
			BWSettingOfDesc = 0;
	}
	else if(pHalData->CurrentChannelBW== CHANNEL_WIDTH_40)
	{
		if((pattrib->bwmode == CHANNEL_WIDTH_40) || (pattrib->bwmode == CHANNEL_WIDTH_80))
			BWSettingOfDesc = 1;
		else
			BWSettingOfDesc = 0;
	}
	else
		BWSettingOfDesc = 0;

	//if(pTcb->bBTTxPacket)
	//	BWSettingOfDesc = 0;

	return BWSettingOfDesc;
}

u8	SCMapping_8723B(PADAPTER Adapter, struct pkt_attrib *pattrib)
{
	u8	SCSettingOfDesc = 0;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);

	//DBG_871X("SCMapping: pHalData->CurrentChannelBW %d, pHalData->nCur80MhzPrimeSC %d, pHalData->nCur40MhzPrimeSC %d \n",pHalData->CurrentChannelBW,pHalData->nCur80MhzPrimeSC,pHalData->nCur40MhzPrimeSC);
	
	if(pHalData->CurrentChannelBW == CHANNEL_WIDTH_80)
	{
		if(pattrib->bwmode == CHANNEL_WIDTH_80)
		{
			SCSettingOfDesc = VHT_DATA_SC_DONOT_CARE;
		}
		else if(pattrib->bwmode == CHANNEL_WIDTH_40)
		{
			if(pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
				SCSettingOfDesc = VHT_DATA_SC_40_LOWER_OF_80MHZ;
			else if(pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
				SCSettingOfDesc = VHT_DATA_SC_40_UPPER_OF_80MHZ;
			else
				DBG_871X("SCMapping: Not Correct Primary40MHz Setting \n");
		}
		else
		{
			if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER))
				SCSettingOfDesc = VHT_DATA_SC_20_LOWEST_OF_80MHZ;
			else if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER))
				SCSettingOfDesc = VHT_DATA_SC_20_LOWER_OF_80MHZ;
			else if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER))
				SCSettingOfDesc = VHT_DATA_SC_20_UPPER_OF_80MHZ;
			else if((pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER) && (pHalData->nCur80MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER))
				SCSettingOfDesc = VHT_DATA_SC_20_UPPERST_OF_80MHZ;
			else
				DBG_871X("SCMapping: Not Correct Primary40MHz Setting \n");
		}
	}
	else if(pHalData->CurrentChannelBW== CHANNEL_WIDTH_40)
	{
		//DBG_871X("SCMapping: HT Case: pHalData->CurrentChannelBW %d, pHalData->nCur40MhzPrimeSC %d \n",pHalData->CurrentChannelBW,pHalData->nCur40MhzPrimeSC);

		if(pattrib->bwmode == CHANNEL_WIDTH_40)
		{
			SCSettingOfDesc = VHT_DATA_SC_DONOT_CARE;
		}
		else if(pattrib->bwmode == CHANNEL_WIDTH_20)
		{
			if(pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_UPPER)
			{
				SCSettingOfDesc = VHT_DATA_SC_20_UPPER_OF_80MHZ;
			}
			else if(pHalData->nCur40MhzPrimeSC == HAL_PRIME_CHNL_OFFSET_LOWER)
			{
				SCSettingOfDesc = VHT_DATA_SC_20_LOWER_OF_80MHZ;
			}
			else
			{
				SCSettingOfDesc = VHT_DATA_SC_DONOT_CARE;
			}
		}
	}
	else
	{
		SCSettingOfDesc = VHT_DATA_SC_DONOT_CARE;
	}

	return SCSettingOfDesc;
}


static u8 fill_txdesc_sectype(struct pkt_attrib *pattrib)
{
	u8 sectype = 0;
	if ((pattrib->encrypt > 0) && !pattrib->bswenc)
	{
		switch (pattrib->encrypt)
		{
			// SEC_TYPE
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

static void fill_txdesc_vcs_8723b(PADAPTER padapter, struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	//DBG_8192C("cvs_mode=%d\n", pattrib->vcs_mode);

	if (pattrib->vcs_mode) {
		switch (pattrib->vcs_mode) {
		case RTS_CTS:
			SET_TX_DESC_RTS_ENABLE_8723B(ptxdesc, 1);
			SET_TX_DESC_HW_RTS_ENABLE_8723B(ptxdesc, 1);
			break;

		case CTS_TO_SELF:
			SET_TX_DESC_CTS2SELF_8723B(ptxdesc, 1);
			break;

		case NONE_VCS:
		default:
			break;
		}

		SET_TX_DESC_RTS_RATE_8723B(ptxdesc, 8); // RTS Rate=24M
		SET_TX_DESC_RTS_RATE_FB_LIMIT_8723B(ptxdesc, 0xF);

		if (padapter->mlmeextpriv.mlmext_info.preamble_mode == PREAMBLE_SHORT) {
			SET_TX_DESC_RTS_SHORT_8723B(ptxdesc, 1);
		}

		// Set RTS BW
		if (pattrib->ht_en) {
			SET_TX_DESC_RTS_SC_8723B(ptxdesc, SCMapping_8723B(padapter, pattrib));
		}
	}
}

static void fill_txdesc_phy_8723b(PADAPTER padapter, struct pkt_attrib *pattrib, u8 *ptxdesc)
{
	//DBG_8192C("bwmode=%d, ch_off=%d\n", pattrib->bwmode, pattrib->ch_offset);

	if (pattrib->ht_en) {
		SET_TX_DESC_DATA_BW_8723B(ptxdesc, BWMapping_8723B(padapter, pattrib));
		SET_TX_DESC_DATA_SC_8723B(ptxdesc, SCMapping_8723B(padapter, pattrib));
	}
}

static void rtl8723b_fill_default_txdesc(
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

	if (pxmitframe->frame_tag == DATA_FRAMETAG)
	{
		u8 drv_userate = 0;

		SET_TX_DESC_MACID_8723B(pbuf, pattrib->mac_id);
		SET_TX_DESC_RATE_ID_8723B(pbuf, pattrib->raid);
		SET_TX_DESC_QUEUE_SEL_8723B(pbuf, pattrib->qsel);
		SET_TX_DESC_SEQ_8723B(pbuf, pattrib->seqnum);

		SET_TX_DESC_SEC_TYPE_8723B(pbuf, fill_txdesc_sectype(pattrib));
		fill_txdesc_vcs_8723b(padapter, pattrib, pbuf);

		if(pattrib->icmp_pkt ==1 && padapter->registrypriv.wifi_spec==1)
			drv_userate = 1;

		if ((pattrib->ether_type != 0x888e) &&
			(pattrib->ether_type != 0x0806) &&
			(pattrib->ether_type != 0x88B4) &&
			(pattrib->dhcp_pkt != 1) &&
			(drv_userate != 1)
#ifdef CONFIG_AUTO_AP_MODE
		    && (pattrib->pctrl != _TRUE)
#endif
			)
		{
			// Non EAP & ARP & DHCP type data packet

			if (pattrib->ampdu_en == _TRUE) {
				SET_TX_DESC_AGG_ENABLE_8723B(pbuf, 1);
				SET_TX_DESC_MAX_AGG_NUM_8723B(pbuf, 0x1F);
				SET_TX_DESC_AMPDU_DENSITY_8723B(pbuf, pattrib->ampdu_spacing);
			}
			else {
				SET_TX_DESC_AGG_BREAK_8723B(pbuf, 1);
			}

			fill_txdesc_phy_8723b(padapter, pattrib, pbuf);

			SET_TX_DESC_DATA_RATE_FB_LIMIT_8723B(pbuf, 0x1F);

			if (pHalData->fw_ractrl == _FALSE) {
				SET_TX_DESC_USE_RATE_8723B(pbuf, 1);

				if (pHalData->INIDATA_RATE[pattrib->mac_id] & BIT(7)) {
					SET_TX_DESC_DATA_SHORT_8723B(pbuf, 1);
				}

				SET_TX_DESC_TX_RATE_8723B(pbuf, pHalData->INIDATA_RATE[pattrib->mac_id] & 0x7F);
			}

			// modify data rate by iwpriv
			if (padapter->fix_rate != 0xFF) {
				SET_TX_DESC_USE_RATE_8723B(pbuf, 1);
				if (padapter->fix_rate & BIT(7)) {
					SET_TX_DESC_DATA_SHORT_8723B(pbuf, 1);
				}
				SET_TX_DESC_TX_RATE_8723B(pbuf, padapter->fix_rate & 0x7F);
				if (!padapter->data_fb) {
					SET_TX_DESC_DISABLE_FB_8723B(pbuf, 1);
				}
			}

			if (pattrib->ldpc) {
				SET_TX_DESC_DATA_LDPC_8723B(pbuf, 1);
			}

			if (pattrib->stbc) {
				SET_TX_DESC_DATA_STBC_8723B(pbuf, 1);
			}

#ifdef CONFIG_CMCC_TEST
			SET_TX_DESC_DATA_SHORT_8723B(pbuf, 1); /* use cck short premble */
#endif
		}
		else
		{
			// EAP data packet and ARP packet.
			// Use the 1M data rate to send the EAP/ARP packet.
			// This will maybe make the handshake smooth.

			SET_TX_DESC_AGG_BREAK_8723B(pbuf, 1);
			SET_TX_DESC_USE_RATE_8723B(pbuf, 1);
			if (pmlmeinfo->preamble_mode == PREAMBLE_SHORT) {
				SET_TX_DESC_DATA_SHORT_8723B(pbuf, 1);
			}
			SET_TX_DESC_TX_RATE_8723B(pbuf, MRateToHwRate(pmlmeext->tx_rate));

			DBG_8192C(FUNC_ADPT_FMT ": SP Packet(0x%04X) rate=0x%x\n",
				FUNC_ADPT_ARG(padapter), pattrib->ether_type, MRateToHwRate(pmlmeext->tx_rate));
		}

#if defined(CONFIG_USB_TX_AGGREGATION) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		SET_TX_DESC_USB_TXAGG_NUM_8723B(pbuf, pxmitframe->agg_num);
#endif

#ifdef CONFIG_TDLS
#ifdef CONFIG_XMIT_ACK
		/* CCX-TXRPT ack for xmit mgmt frames. */
		if (pxmitframe->ack_report) {
			#ifdef DBG_CCX
			DBG_8192C("%s set spe_rpt\n", __func__);
			#endif
			SET_TX_DESC_SPE_RPT_8723B(pbuf, 1);
			SET_TX_DESC_SW_DEFINE_8723B(pbuf, (u8)(GET_PRIMARY_ADAPTER(padapter)->xmitpriv.seq_no));
		}
#endif /* CONFIG_XMIT_ACK */
#endif
	}
	else if (pxmitframe->frame_tag == MGNT_FRAMETAG)
	{
//		RT_TRACE(_module_hal_xmit_c_, _drv_notice_, ("%s: MGNT_FRAMETAG\n", __FUNCTION__));

		SET_TX_DESC_MACID_8723B(pbuf, pattrib->mac_id);
		SET_TX_DESC_QUEUE_SEL_8723B(pbuf, pattrib->qsel);
		SET_TX_DESC_RATE_ID_8723B(pbuf, pattrib->raid);
		SET_TX_DESC_SEQ_8723B(pbuf, pattrib->seqnum);
		SET_TX_DESC_USE_RATE_8723B(pbuf, 1);

		SET_TX_DESC_MBSSID_8723B(pbuf, pattrib->mbssid & 0xF);

		SET_TX_DESC_RETRY_LIMIT_ENABLE_8723B(pbuf, 1);
		if (pattrib->retry_ctrl == _TRUE) {
			SET_TX_DESC_DATA_RETRY_LIMIT_8723B(pbuf, 6);
		} else {
			SET_TX_DESC_DATA_RETRY_LIMIT_8723B(pbuf, 12);
		}

#ifdef CONFIG_INTEL_PROXIM
		if((padapter->proximity.proxim_on==_TRUE)&&(pattrib->intel_proxim==_TRUE)){
			DBG_871X("\n %s pattrib->rate=%d\n",__FUNCTION__,pattrib->rate);
			SET_TX_DESC_TX_RATE_8723B(pbuf, pattrib->rate);
		}
		else
#endif
		{
			SET_TX_DESC_TX_RATE_8723B(pbuf, MRateToHwRate(pmlmeext->tx_rate));
		}

#ifdef CONFIG_XMIT_ACK
		// CCX-TXRPT ack for xmit mgmt frames.
		if (pxmitframe->ack_report) {
			#ifdef DBG_CCX
			DBG_8192C("%s set spe_rpt\n", __FUNCTION__);
			#endif
			SET_TX_DESC_SPE_RPT_8723B(pbuf, 1);
			SET_TX_DESC_SW_DEFINE_8723B(pbuf, (u8)(GET_PRIMARY_ADAPTER(padapter)->xmitpriv.seq_no));
		}
#endif // CONFIG_XMIT_ACK
	}
	else if (pxmitframe->frame_tag == TXAGG_FRAMETAG)
	{
		RT_TRACE(_module_hal_xmit_c_, _drv_warning_, ("%s: TXAGG_FRAMETAG\n", __FUNCTION__));
	}
#ifdef CONFIG_MP_INCLUDED
	else if (pxmitframe->frame_tag == MP_FRAMETAG)
	{
		RT_TRACE(_module_hal_xmit_c_, _drv_notice_, ("%s: MP_FRAMETAG\n", __FUNCTION__));
		fill_txdesc_for_mp(padapter, pbuf);
	}
#endif
	else
	{
		RT_TRACE(_module_hal_xmit_c_, _drv_warning_, ("%s: frame_tag=0x%x\n", __FUNCTION__, pxmitframe->frame_tag));

		SET_TX_DESC_MACID_8723B(pbuf, pattrib->mac_id);
		SET_TX_DESC_RATE_ID_8723B(pbuf, pattrib->raid);
		SET_TX_DESC_QUEUE_SEL_8723B(pbuf, pattrib->qsel);
		SET_TX_DESC_SEQ_8723B(pbuf, pattrib->seqnum);
		SET_TX_DESC_USE_RATE_8723B(pbuf, 1);
		SET_TX_DESC_TX_RATE_8723B(pbuf, MRateToHwRate(pmlmeext->tx_rate));
	}

	SET_TX_DESC_PKT_SIZE_8723B(pbuf, pattrib->last_txcmdsz);

	{
		u8 pkt_offset, offset;

		pkt_offset = 0;
		offset = TXDESC_SIZE;
#ifdef CONFIG_USB_HCI
		pkt_offset = pxmitframe->pkt_offset;
		offset += (pxmitframe->pkt_offset >> 3);
#endif // CONFIG_USB_HCI

#ifdef CONFIG_TX_EARLY_MODE
		if (pxmitframe->frame_tag == DATA_FRAMETAG) {
			pkt_offset = 1;
			offset += EARLY_MODE_INFO_SIZE;
		}
#endif // CONFIG_TX_EARLY_MODE

		SET_TX_DESC_PKT_OFFSET_8723B(pbuf, pkt_offset);
		SET_TX_DESC_OFFSET_8723B(pbuf, offset);
	}

	if (bmcst) {
		SET_TX_DESC_BMC_8723B(pbuf, 1);
	}

	// 2009.11.05. tynli_test. Suggested by SD4 Filen for FW LPS.
	// (1) The sequence number of each non-Qos frame / broadcast / multicast /
	// mgnt frame should be controled by Hw because Fw will also send null data
	// which we cannot control when Fw LPS enable.
	// --> default enable non-Qos data sequense number. 2010.06.23. by tynli.
	// (2) Enable HW SEQ control for beacon packet, because we use Hw beacon.
	// (3) Use HW Qos SEQ to control the seq num of Ext port non-Qos packets.
	// 2010.06.23. Added by tynli.
	if (!pattrib->qos_en) {
		SET_TX_DESC_HWSEQ_EN_8723B(pbuf, 1);
	}
}

/*
 *	Description:
 *
 *	Parameters:
 *		pxmitframe	xmitframe
 *		pbuf		where to fill tx desc
 */
void rtl8723b_update_txdesc(struct xmit_frame *pxmitframe, u8 *pbuf)
{
	PADAPTER padapter = pxmitframe->padapter;
	rtl8723b_fill_default_txdesc(pxmitframe, pbuf);

#ifdef CONFIG_ANTENNA_DIVERSITY
	ODM_SetTxAntByTxInfo(&GET_HAL_DATA(padapter)->odmpriv, pbuf, pxmitframe->attrib.mac_id);
#endif // CONFIG_ANTENNA_DIVERSITY

#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	rtl8723b_cal_txdesc_chksum((struct tx_desc*)pbuf);
#endif
}

#ifdef CONFIG_TSF_RESET_OFFLOAD
int reset_tsf(PADAPTER Adapter, u8 reset_port )
{
	u8 reset_cnt_before = 0, reset_cnt_after = 0, loop_cnt = 0;
	u32 reg_reset_tsf_cnt = (IFACE_PORT0==reset_port) ?
				REG_FW_RESET_TSF_CNT_0:REG_FW_RESET_TSF_CNT_1;

	rtw_scan_abort(Adapter->pbuddy_adapter);	/*	site survey will cause reset_tsf fail	*/
	reset_cnt_after = reset_cnt_before = rtw_read8(Adapter,reg_reset_tsf_cnt);
	rtl8723b_reset_tsf(Adapter, reset_port);

	while ((reset_cnt_after == reset_cnt_before ) && (loop_cnt < 10)) {
		rtw_msleep_os(100);
		loop_cnt++;
		reset_cnt_after = rtw_read8(Adapter, reg_reset_tsf_cnt);
	}

	return(loop_cnt >= 10) ? _FAIL : _TRUE;
}
#endif // CONFIG_TSF_RESET_OFFLOAD

static void hw_var_set_monitor(PADAPTER Adapter, u8 variable, u8 *val)
{
	u32	value_rcr, rcr_bits;
	u16	value_rxfltmap2;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv *pmlmepriv = &(Adapter->mlmepriv);

	if (*((u8 *)val) == _HW_STATE_MONITOR_) {

		/* Leave IPS */
		rtw_pm_set_ips(Adapter, IPS_NONE);
		LeaveAllPowerSaveMode(Adapter);

		/* Receive all type */
		rcr_bits = RCR_AAP | RCR_APM | RCR_AM | RCR_AB | RCR_APWRMGT | RCR_ADF | RCR_ACF | RCR_AMF | RCR_APP_PHYST_RXFF;

		/* Append FCS */
		rcr_bits |= RCR_APPFCS;

		#if 0
		/* 
		   CRC and ICV packet will drop in recvbuf2recvframe()
		   We no turn on it.
		 */
		rcr_bits |= (RCR_ACRC32 | RCR_AICV);
		#endif

		/* Receive all data frames */
		value_rxfltmap2 = 0xFFFF;

		value_rcr = rcr_bits;
		rtw_write32(Adapter, REG_RCR, value_rcr);

		rtw_write16(Adapter, REG_RXFLTMAP2, value_rxfltmap2);

		#if 0
		/* tx pause */
		rtw_write8(padapter, REG_TXPAUSE, 0xFF);
		#endif
	} else {
		/* do nothing */
	}

}

static void hw_var_set_opmode(PADAPTER padapter, u8 variable, u8* val)
{
	u8 val8;
	u8 mode = *((u8 *)val);

	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(padapter);

	/* reset RCR */
	rtw_write32(padapter, REG_RCR, pHalData->ReceiveConfig);

	if (mode == _HW_STATE_MONITOR_) {
		/* set net_type */
		Set_MSR(padapter, _HW_STATE_NOLINK_);

		hw_var_set_monitor(padapter, variable, val);
		return;
	}

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->iface_type == IFACE_PORT1)
	{
		// disable Port1 TSF update
		val8 = rtw_read8(padapter, REG_BCN_CTRL_1);
		val8 |= DIS_TSF_UDT;
		rtw_write8(padapter, REG_BCN_CTRL_1, val8);
		
		Set_MSR(padapter, mode);
		
		DBG_871X("#### %s()-%d iface_type(%d) mode=%d ####\n",
			__FUNCTION__, __LINE__, padapter->iface_type, mode);

		if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_))
		{
			if (!check_buddy_mlmeinfo_state(padapter, WIFI_FW_AP_STATE))
			{
				StopTxBeacon(padapter);
#ifdef CONFIG_PCI_HCI
				UpdateInterruptMask8723BE(padapter, 0, 0, RT_BCN_INT_MASKS, 0);
#else // !CONFIG_PCI_HCI
#ifdef CONFIG_INTERRUPT_BASED_TXBCN	

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT	
				rtw_write8(padapter, REG_DRVERLYINT, 0x05);//restore early int time to 5ms
				UpdateInterruptMask8723BU(padapter, _TRUE, 0, IMR_BCNDMAINT0_8723B);
#endif // CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
				UpdateInterruptMask8723BU(padapter, _TRUE ,0, (IMR_TXBCN0ERR_8723B|IMR_TXBCN0OK_8723B));
#endif // CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR

#endif // CONFIG_INTERRUPT_BASED_TXBCN
#endif // !CONFIG_PCI_HCI
			}

			// disable atim wnd and disable beacon function
			rtw_write8(padapter, REG_BCN_CTRL_1, DIS_TSF_UDT|DIS_ATIM);
		}
		else if ((mode == _HW_STATE_ADHOC_) /*|| (mode == _HW_STATE_AP_)*/)
		{
			ResumeTxBeacon(padapter);
			rtw_write8(padapter, REG_BCN_CTRL_1, DIS_TSF_UDT|EN_BCN_FUNCTION|DIS_BCNQ_SUB);
		}
		else if (mode == _HW_STATE_AP_)
		{
#ifdef CONFIG_PCI_HCI
			UpdateInterruptMask8723BE(padapter, RT_BCN_INT_MASKS, 0, 0, 0);
#else // !CONFIG_PCI_HCI
#ifdef CONFIG_INTERRUPT_BASED_TXBCN

#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			UpdateInterruptMask8723BU(padapter, _TRUE, IMR_BCNDMAINT0_8723B, 0);
#endif // CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR	
			UpdateInterruptMask8723BU(padapter, _TRUE, (IMR_TXBCN0ERR_8723B|IMR_TXBCN0OK_8723B), 0);
#endif // CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR

#endif // CONFIG_INTERRUPT_BASED_TXBCN
#endif // !CONFIG_PCI_HCI

			ResumeTxBeacon(padapter);

			rtw_write8(padapter, REG_BCN_CTRL_1, DIS_TSF_UDT|DIS_BCNQ_SUB);

			// Set RCR
			//rtw_write32(padapter, REG_RCR, 0x70002a8e);//CBSSID_DATA must set to 0
			//rtw_write32(padapter, REG_RCR, 0x7000228e);//CBSSID_DATA must set to 0
			rtw_write32(padapter, REG_RCR, 0x7000208e);//CBSSID_DATA must set to 0,reject ICV_ERR packet
			// enable to rx data frame				
			rtw_write16(padapter, REG_RXFLTMAP2, 0xFFFF);
			// enable to rx ps-poll
			rtw_write16(padapter, REG_RXFLTMAP1, 0x0400);

			// Beacon Control related register for first time 
			rtw_write8(padapter, REG_BCNDMATIM, 0x02); // 2ms		

			//rtw_write8(padapter, REG_BCN_MAX_ERR, 0xFF);
			rtw_write8(padapter, REG_ATIMWND_1, 0x0a); // 10ms for port1
			rtw_write16(padapter, REG_BCNTCFG, 0x00);
			rtw_write16(padapter, REG_TBTT_PROHIBIT, 0xff04);
			rtw_write16(padapter, REG_TSFTR_SYN_OFFSET, 0x7fff);// +32767 (~32ms)
	
			// reset TSF2	
			rtw_write8(padapter, REG_DUAL_TSF_RST, BIT(1));

			// enable BCN1 Function for if2
			// don't enable update TSF1 for if2 (due to TSF update when beacon/probe rsp are received)
			rtw_write8(padapter, REG_BCN_CTRL_1, (DIS_TSF_UDT|EN_BCN_FUNCTION | EN_TXBCN_RPT|DIS_BCNQ_SUB));

			//SW_BCN_SEL - Port1
			//rtw_write8(Adapter, REG_DWBCN1_CTRL_8192E+2, rtw_read8(Adapter, REG_DWBCN1_CTRL_8192E+2)|BIT4);
			rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);
			
			// select BCN on port 1
			rtw_write8(padapter, REG_CCK_CHECK_8723B,
				(rtw_read8(padapter, REG_CCK_CHECK_8723B)|BIT_BCN_PORT_SEL));
			
			if (check_buddy_fwstate(padapter, WIFI_FW_NULL_STATE))
			{
				val8 = rtw_read8(padapter, REG_BCN_CTRL);
				val8 &= ~EN_BCN_FUNCTION;
				rtw_write8(padapter, REG_BCN_CTRL, val8);
			}

			//BCN1 TSF will sync to BCN0 TSF with offset(0x518) if if1_sta linked
			//rtw_write8(padapter, REG_BCN_CTRL_1, rtw_read8(padapter, REG_BCN_CTRL_1)|BIT(5));
			//rtw_write8(padapter, REG_DUAL_TSF_RST, BIT(3));
					
			//dis BCN0 ATIM  WND if if1 is station
			rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)|DIS_ATIM);

#ifdef CONFIG_TSF_RESET_OFFLOAD
			// Reset TSF for STA+AP concurrent mode
			if (check_buddy_fwstate(padapter, (WIFI_STATION_STATE|WIFI_ASOC_STATE)))
			{
				if (reset_tsf(padapter, IFACE_PORT1) == _FALSE)
					DBG_871X("ERROR! %s()-%d: Reset port1 TSF fail\n",
						__FUNCTION__, __LINE__);
			}
#endif // CONFIG_TSF_RESET_OFFLOAD
		}
	}
	else //else for port0
#endif // CONFIG_CONCURRENT_MODE
	{
		// disable Port0 TSF update
		val8 = rtw_read8(padapter, REG_BCN_CTRL);
		val8 |= DIS_TSF_UDT;
		rtw_write8(padapter, REG_BCN_CTRL, val8);

		// set net_type
		Set_MSR(padapter, mode);
		DBG_871X("#### %s() -%d iface_type(0) mode = %d ####\n", __FUNCTION__, __LINE__, mode);

		if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_))
		{
#ifdef CONFIG_CONCURRENT_MODE
			if (!check_buddy_mlmeinfo_state(padapter, WIFI_FW_AP_STATE))		
#endif // CONFIG_CONCURRENT_MODE
			{
				StopTxBeacon(padapter);
#ifdef CONFIG_PCI_HCI
				UpdateInterruptMask8723BE(padapter, 0, 0, RT_BCN_INT_MASKS, 0);
#else // !CONFIG_PCI_HCI
#ifdef CONFIG_INTERRUPT_BASED_TXBCN
#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
				rtw_write8(padapter, REG_DRVERLYINT, 0x05); // restore early int time to 5ms
				UpdateInterruptMask8812AU(padapter, _TRUE, 0, IMR_BCNDMAINT0_8723B);
#endif // CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
				UpdateInterruptMask8812AU(padapter,_TRUE ,0, (IMR_TXBCN0ERR_8723B|IMR_TXBCN0OK_8723B));
#endif // CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR

#endif // CONFIG_INTERRUPT_BASED_TXBCN
#endif // !CONFIG_PCI_HCI
			}

			// disable atim wnd
			rtw_write8(padapter, REG_BCN_CTRL, DIS_TSF_UDT|EN_BCN_FUNCTION|DIS_ATIM);
			//rtw_write8(padapter,REG_BCN_CTRL, 0x18);
		}
		else if ((mode == _HW_STATE_ADHOC_) /*|| (mode == _HW_STATE_AP_)*/)
		{
			ResumeTxBeacon(padapter);
			rtw_write8(padapter, REG_BCN_CTRL, DIS_TSF_UDT|EN_BCN_FUNCTION|DIS_BCNQ_SUB);
		}
		else if (mode == _HW_STATE_AP_)
		{
#ifdef CONFIG_PCI_HCI
			UpdateInterruptMask8723BE( padapter, RT_BCN_INT_MASKS, 0, 0, 0);
#else // !CONFIG_PCI_HCI
#ifdef CONFIG_INTERRUPT_BASED_TXBCN
#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			UpdateInterruptMask8723BU(padapter, _TRUE ,IMR_BCNDMAINT0_8723B, 0);
#endif // CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
			UpdateInterruptMask8723BU(padapter,_TRUE ,(IMR_TXBCN0ERR_8723B|IMR_TXBCN0OK_8723B), 0);
#endif // CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR

#endif // CONFIG_INTERRUPT_BASED_TXBCN
#endif

			ResumeTxBeacon(padapter);

			rtw_write8(padapter, REG_BCN_CTRL, DIS_TSF_UDT|DIS_BCNQ_SUB);

			//Set RCR
			//rtw_write32(padapter, REG_RCR, 0x70002a8e);//CBSSID_DATA must set to 0
			//rtw_write32(padapter, REG_RCR, 0x7000228e);//CBSSID_DATA must set to 0
			rtw_write32(padapter, REG_RCR, 0x7000208e);//CBSSID_DATA must set to 0,reject ICV_ERR packet
			//enable to rx data frame
			rtw_write16(padapter, REG_RXFLTMAP2, 0xFFFF);
			//enable to rx ps-poll
			rtw_write16(padapter, REG_RXFLTMAP1, 0x0400);

			//Beacon Control related register for first time
			rtw_write8(padapter, REG_BCNDMATIM, 0x02); // 2ms			
			
			//rtw_write8(padapter, REG_BCN_MAX_ERR, 0xFF);
			rtw_write8(padapter, REG_ATIMWND, 0x0a); // 10ms
			rtw_write16(padapter, REG_BCNTCFG, 0x00);
			rtw_write16(padapter, REG_TBTT_PROHIBIT, 0xff04);
			rtw_write16(padapter, REG_TSFTR_SYN_OFFSET, 0x7fff);// +32767 (~32ms)

			//reset TSF
			rtw_write8(padapter, REG_DUAL_TSF_RST, BIT(0));
	
			//enable BCN0 Function for if1
			//don't enable update TSF0 for if1 (due to TSF update when beacon/probe rsp are received)
			rtw_write8(padapter, REG_BCN_CTRL, (DIS_TSF_UDT|EN_BCN_FUNCTION|EN_TXBCN_RPT|DIS_BCNQ_SUB));
		
			//SW_BCN_SEL - Port0
			//rtw_write8(Adapter, REG_DWBCN1_CTRL_8192E+2, rtw_read8(Adapter, REG_DWBCN1_CTRL_8192E+2) & ~BIT4);
			rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);
			
			// select BCN on port 0
			rtw_write8(padapter, REG_CCK_CHECK_8723B,
				(rtw_read8(padapter, REG_CCK_CHECK_8723B)& ~BIT_BCN_PORT_SEL));				

#ifdef CONFIG_CONCURRENT_MODE
			if (check_buddy_fwstate(padapter, WIFI_FW_NULL_STATE))
			{
				val8 = rtw_read8(padapter, REG_BCN_CTRL_1);
				val8 &= ~EN_BCN_FUNCTION;
				rtw_write8(padapter, REG_BCN_CTRL_1, val8);
			}
#endif // CONFIG_CONCURRENT_MODE

			// dis BCN1 ATIM  WND if if2 is station
			val8 = rtw_read8(padapter, REG_BCN_CTRL_1);
			val8 |= DIS_ATIM;
			rtw_write8(padapter, REG_BCN_CTRL_1, val8);
#ifdef CONFIG_TSF_RESET_OFFLOAD
			// Reset TSF for STA+AP concurrent mode
			if (check_buddy_fwstate(padapter, (WIFI_STATION_STATE|WIFI_ASOC_STATE)))
			{
				if (reset_tsf(padapter, IFACE_PORT0) == _FALSE)
					DBG_871X("ERROR! %s()-%d: Reset port0 TSF fail\n",
						__FUNCTION__, __LINE__);
			}
#endif	// CONFIG_TSF_RESET_OFFLOAD
		}
	}
}

static void hw_var_set_macaddr(PADAPTER padapter, u8 variable, u8 *val)
{
	u8 idx = 0;
	u32 reg_macid;

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->iface_type == IFACE_PORT1)
	{
		reg_macid = REG_MACID1;
	}
	else
#endif
	{
		reg_macid = REG_MACID;
	}

	for (idx = 0 ; idx < 6; idx++)
	{
		rtw_write8(GET_PRIMARY_ADAPTER(padapter), (reg_macid+idx), val[idx]);
	}
}

static void hw_var_set_bssid(PADAPTER padapter, u8 variable, u8 *val)
{
	u8	idx = 0;
	u32 reg_bssid;

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->iface_type == IFACE_PORT1)
	{
		reg_bssid = REG_BSSID1;
	}
	else
#endif
	{
		reg_bssid = REG_BSSID;
	}

	for (idx = 0 ; idx < 6; idx++)
	{
		rtw_write8(padapter, (reg_bssid+idx), val[idx]);
	}
}

static void hw_var_set_bcn_func(PADAPTER padapter, u8 variable, u8 *val)
{
	u32 bcn_ctrl_reg;

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->iface_type == IFACE_PORT1)
	{
		bcn_ctrl_reg = REG_BCN_CTRL_1;
	}
	else
#endif
	{
		bcn_ctrl_reg = REG_BCN_CTRL;
	}

	if (*(u8*)val)
	{
		rtw_write8(padapter, bcn_ctrl_reg, (EN_BCN_FUNCTION | EN_TXBCN_RPT));
	}
	else
	{
		u8 val8;
		val8 = rtw_read8(padapter, bcn_ctrl_reg);
		val8 &= ~(EN_BCN_FUNCTION | EN_TXBCN_RPT);
#ifdef CONFIG_BT_COEXIST
		// Always enable port0 beacon function for PSTDMA
		if (REG_BCN_CTRL == bcn_ctrl_reg)
			val8 |= EN_BCN_FUNCTION;
#endif
		rtw_write8(padapter, bcn_ctrl_reg, val8);
	}
}

static void hw_var_set_correct_tsf(PADAPTER padapter, u8 variable, u8* val)
{
	u8 val8;
	u64	tsf;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;


	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;
	
	tsf = pmlmeext->TSFValue - rtw_modular64(pmlmeext->TSFValue, (pmlmeinfo->bcn_interval*1024)) -1024; //us

	if (((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) ||
		((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
	{
		StopTxBeacon(padapter);
	}

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->iface_type == IFACE_PORT1)
	{
		// disable related TSF function
		val8 = rtw_read8(padapter, REG_BCN_CTRL_1);
		val8 &= ~EN_BCN_FUNCTION;
		rtw_write8(padapter, REG_BCN_CTRL_1, val8);

		rtw_write32(padapter, REG_TSFTR1, tsf);
		rtw_write32(padapter, REG_TSFTR1+4, tsf>>32);


		// enable related TSF function
		val8 = rtw_read8(padapter, REG_BCN_CTRL_1);
		val8 |= EN_BCN_FUNCTION;
		rtw_write8(padapter, REG_BCN_CTRL_1, val8);

		// Update buddy port's TSF if it is SoftAP for beacon TX issue!
		if ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE
			&& check_buddy_fwstate(padapter, WIFI_AP_STATE)
			)
		{
			// disable related TSF function
			val8 = rtw_read8(padapter, REG_BCN_CTRL);
			val8 &= ~EN_BCN_FUNCTION;
			rtw_write8(padapter, REG_BCN_CTRL, val8);

			rtw_write32(padapter, REG_TSFTR, tsf);
			rtw_write32(padapter, REG_TSFTR+4, tsf>>32);

			// enable related TSF function
			val8 = rtw_read8(padapter, REG_BCN_CTRL);
			val8 |= EN_BCN_FUNCTION;
			rtw_write8(padapter, REG_BCN_CTRL, val8);
#ifdef CONFIG_TSF_RESET_OFFLOAD
			// Update buddy port's TSF(TBTT) if it is SoftAP for beacon TX issue!
			if (reset_tsf(padapter, IFACE_PORT0) == _FALSE)
				DBG_871X("ERROR! %s()-%d: Reset port0 TSF fail\n",
					__FUNCTION__, __LINE__);

#endif // CONFIG_TSF_RESET_OFFLOAD
		}
	}
	else
#endif // CONFIG_CONCURRENT_MODE
	{
		// disable related TSF function
		val8 = rtw_read8(padapter, REG_BCN_CTRL);
		val8 &= ~EN_BCN_FUNCTION;
		rtw_write8(padapter, REG_BCN_CTRL, val8);

		rtw_write32(padapter, REG_TSFTR, tsf);
		rtw_write32(padapter, REG_TSFTR+4, tsf>>32);

		// enable related TSF function
		val8 = rtw_read8(padapter, REG_BCN_CTRL);
		val8 |= EN_BCN_FUNCTION;
		rtw_write8(padapter, REG_BCN_CTRL, val8);

#ifdef CONFIG_CONCURRENT_MODE
		// Update buddy port's TSF if it is SoftAP for beacon TX issue!
		if ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE
			&& check_buddy_fwstate(padapter, WIFI_AP_STATE))
		{
			// disable related TSF function
			val8 = rtw_read8(padapter, REG_BCN_CTRL_1);
			val8 &= ~EN_BCN_FUNCTION;
			rtw_write8(padapter, REG_BCN_CTRL_1, val8);

			rtw_write32(padapter, REG_TSFTR1, tsf);
			rtw_write32(padapter, REG_TSFTR1+4, tsf>>32);

			// enable related TSF function
			val8 = rtw_read8(padapter, REG_BCN_CTRL_1);
			val8 |= EN_BCN_FUNCTION;
			rtw_write8(padapter, REG_BCN_CTRL_1, val8);

#ifdef CONFIG_TSF_RESET_OFFLOAD
			// Update buddy port's TSF if it is SoftAP for beacon TX issue!
			if (reset_tsf(padapter, IFACE_PORT1) == _FALSE)
			{
				DBG_871X("ERROR! %s()-%d: Reset port1 TSF fail\n",
					__FUNCTION__, __LINE__);
			}
#endif // CONFIG_TSF_RESET_OFFLOAD
		}
#endif // CONFIG_CONCURRENT_MODE
	}

	if (((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
		|| ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
	{
		ResumeTxBeacon(padapter);
	}
}

static void hw_var_set_mlme_disconnect(PADAPTER padapter, u8 variable, u8 *val)
{
	u8 val8;

#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_mlmeinfo_state(padapter, _HW_STATE_NOLINK_))
#endif
	{
		// Set RCR to not to receive data frame when NO LINK state
		//rtw_write32(padapter, REG_RCR, rtw_read32(padapter, REG_RCR) & ~RCR_ADF);
		// reject all data frames
		rtw_write16(padapter, REG_RXFLTMAP2, 0);
	}

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->iface_type == IFACE_PORT1)
	{
		// reset TSF1
		rtw_write8(padapter, REG_DUAL_TSF_RST, BIT(1));

		// disable update TSF1
		val8 = rtw_read8(padapter, REG_BCN_CTRL_1);
		val8 |= DIS_TSF_UDT;
		rtw_write8(padapter, REG_BCN_CTRL_1, val8);
		
		// disable Port1's beacon function
		val8 = rtw_read8(padapter, REG_BCN_CTRL_1);
		val8 &= ~EN_BCN_FUNCTION;
		rtw_write8(padapter, REG_BCN_CTRL_1, val8);
	}
	else
#endif
	{
		// reset TSF
		rtw_write8(padapter, REG_DUAL_TSF_RST, BIT(0));

		// disable update TSF
		val8 = rtw_read8(padapter, REG_BCN_CTRL);
		val8 |= DIS_TSF_UDT;
		rtw_write8(padapter, REG_BCN_CTRL, val8);
	}
}

static void hw_var_set_mlme_sitesurvey(PADAPTER padapter, u8 variable, u8* val)
{
	u32	value_rcr, rcr_clear_bit, reg_bcn_ctl;
	u16	value_rxfltmap2;
	u8 val8;
	PHAL_DATA_TYPE pHalData;
	struct mlme_priv *pmlmepriv;


	pHalData = GET_HAL_DATA(padapter);
	pmlmepriv = &padapter->mlmepriv;

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->iface_type == IFACE_PORT1)
		reg_bcn_ctl = REG_BCN_CTRL_1;
	else
#endif
		reg_bcn_ctl = REG_BCN_CTRL;

#ifdef CONFIG_FIND_BEST_CHANNEL
	rcr_clear_bit = (RCR_CBSSID_BCN | RCR_CBSSID_DATA);

	/* Receive all data frames */
	value_rxfltmap2 = 0xFFFF;
#else // CONFIG_FIND_BEST_CHANNEL

	rcr_clear_bit = RCR_CBSSID_BCN;

	// config RCR to receive different BSSID & not to receive data frame
	value_rxfltmap2 = 0;

#endif // CONFIG_FIND_BEST_CHANNEL

	if ((check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
#ifdef CONFIG_CONCURRENT_MODE
		|| (check_buddy_fwstate(padapter, WIFI_AP_STATE) == _TRUE)
#endif
		)
	{
		rcr_clear_bit = RCR_CBSSID_BCN;
	}
#ifdef CONFIG_TDLS
	// TDLS will clear RCR_CBSSID_DATA bit for connection.
	else if (padapter->tdlsinfo.link_established == _TRUE)
	{
		rcr_clear_bit = RCR_CBSSID_BCN;
	}
#endif // CONFIG_TDLS

	value_rcr = rtw_read32(padapter, REG_RCR);

	if (*((u8*)val))
	{
		// under sitesurvey
		value_rcr &= ~(rcr_clear_bit);
		rtw_write32(padapter, REG_RCR, value_rcr);

		rtw_write16(padapter, REG_RXFLTMAP2, value_rxfltmap2);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE))
		{
			// disable update TSF
			val8 = rtw_read8(padapter, reg_bcn_ctl);
			val8 |= DIS_TSF_UDT;
			rtw_write8(padapter, reg_bcn_ctl, val8);
		}

		// Save orignal RRSR setting.
		pHalData->RegRRSR = rtw_read16(padapter, REG_RRSR);

#ifdef CONFIG_CONCURRENT_MODE
		if (check_buddy_mlmeinfo_state(padapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(padapter, _FW_LINKED))
		{
			StopTxBeacon(padapter);
		}
#endif
	}
	else
	{
		// sitesurvey done
		if (check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE))
#ifdef CONFIG_CONCURRENT_MODE
			|| check_buddy_fwstate(padapter, (_FW_LINKED|WIFI_AP_STATE))
#endif
			)
		{
			// enable to rx data frame
			rtw_write16(padapter, REG_RXFLTMAP2, 0xFFFF);
		}

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE))
		{
			// enable update TSF
			val8 = rtw_read8(padapter, reg_bcn_ctl);
			val8 &= ~DIS_TSF_UDT;
			rtw_write8(padapter, reg_bcn_ctl, val8);
		}

		value_rcr |= rcr_clear_bit;
		rtw_write32(padapter, REG_RCR, value_rcr);

		// Restore orignal RRSR setting.
		rtw_write16(padapter, REG_RRSR, pHalData->RegRRSR);

#ifdef CONFIG_CONCURRENT_MODE
		if (check_buddy_mlmeinfo_state(padapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(padapter, _FW_LINKED))
		{
			ResumeTxBeacon(padapter);
		}
#endif
	}
}

static void hw_var_set_mlme_join(PADAPTER padapter, u8 variable, u8 *val)
{
	u8 val8;
	u16 val16;
	u32 val32;
	u8 RetryLimit;
	u8 type;
	PHAL_DATA_TYPE pHalData;
	struct mlme_priv *pmlmepriv;

	RetryLimit = 0x30;
	type = *(u8*)val;
	pHalData = GET_HAL_DATA(padapter);
	pmlmepriv = &padapter->mlmepriv;

#ifdef CONFIG_CONCURRENT_MODE
	if (type == 0)
	{
		// prepare to join
		if (check_buddy_mlmeinfo_state(padapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(padapter, _FW_LINKED))
		{
			StopTxBeacon(padapter);
		}

		// enable to rx data frame.Accept all data frame
		rtw_write16(padapter, REG_RXFLTMAP2, 0xFFFF);

		if (check_buddy_mlmeinfo_state(padapter, WIFI_FW_AP_STATE))
		{
			val32 = rtw_read32(padapter, REG_RCR);
			val32 |= RCR_CBSSID_BCN;
			rtw_write32(padapter, REG_RCR, val32);
		}
		else
		{
			val32 = rtw_read32(padapter, REG_RCR);
			val32 |= RCR_CBSSID_DATA | RCR_CBSSID_BCN;
			rtw_write32(padapter, REG_RCR, val32);
		}

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
		{
			RetryLimit = (pHalData->CustomerID == RT_CID_CCX) ? 7 : 48;
		}
		else // Ad-hoc Mode
		{
			RetryLimit = 0x7;
		}
	}
	else if (type == 1)
	{
		// joinbss_event call back when join res < 0
		if (check_buddy_mlmeinfo_state(padapter, _HW_STATE_NOLINK_))
			rtw_write16(padapter, REG_RXFLTMAP2, 0x00);

		if (check_buddy_mlmeinfo_state(padapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(padapter, _FW_LINKED))
		{
			ResumeTxBeacon(padapter);

			// reset TSF 1/2 after ResumeTxBeacon
			rtw_write8(padapter, REG_DUAL_TSF_RST, BIT(1)|BIT(0));
		}
	}
	else if (type == 2)
	{
		// sta add event call back

		// enable update TSF
		if (padapter->iface_type == IFACE_PORT1)
		{
			val8 = rtw_read8(padapter, REG_BCN_CTRL_1);
			val8 &= ~DIS_TSF_UDT;
			rtw_write8(padapter, REG_BCN_CTRL_1, val8);
		}
		else
		{
			val8 = rtw_read8(padapter, REG_BCN_CTRL);
			val8 &= ~DIS_TSF_UDT;
			rtw_write8(padapter, REG_BCN_CTRL, val8);
		}

		if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE))
		{
			rtw_write8(padapter, 0x542 ,0x02);
			RetryLimit = 0x7;
		}

		if (check_buddy_mlmeinfo_state(padapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(padapter, _FW_LINKED))
		{
			ResumeTxBeacon(padapter);

			// reset TSF 1/2 after ResumeTxBeacon
			rtw_write8(padapter, REG_DUAL_TSF_RST, BIT(1)|BIT(0));
		}
	}

	val16 = (RetryLimit << RETRY_LIMIT_SHORT_SHIFT) | (RetryLimit << RETRY_LIMIT_LONG_SHIFT);
	rtw_write16(padapter, REG_RL, val16);
#else // !CONFIG_CONCURRENT_MODE
	if (type == 0) // prepare to join
	{
		//enable to rx data frame.Accept all data frame
		//rtw_write32(padapter, REG_RCR, rtw_read32(padapter, REG_RCR)|RCR_ADF);
		rtw_write16(padapter, REG_RXFLTMAP2, 0xFFFF);

		val32 = rtw_read32(padapter, REG_RCR);
		if (padapter->in_cta_test)
			val32 &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN);//| RCR_ADF
		else
			val32 |= RCR_CBSSID_DATA|RCR_CBSSID_BCN;
		rtw_write32(padapter, REG_RCR, val32);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
		{
			RetryLimit = (pHalData->CustomerID == RT_CID_CCX) ? 7 : 48;
		}
		else // Ad-hoc Mode
		{
			RetryLimit = 0x7;
		}
	}
	else if (type == 1) //joinbss_event call back when join res < 0
	{
		rtw_write16(padapter, REG_RXFLTMAP2, 0x00);
	}
	else if (type == 2) //sta add event call back
	{
		//enable update TSF
		val8 = rtw_read8(padapter, REG_BCN_CTRL);
		val8 &= ~DIS_TSF_UDT;
		rtw_write8(padapter, REG_BCN_CTRL, val8);

		if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE))
		{
			RetryLimit = 0x7;
		}
	}

	val16 = (RetryLimit << RETRY_LIMIT_SHORT_SHIFT) | (RetryLimit << RETRY_LIMIT_LONG_SHIFT);
	rtw_write16(padapter, REG_RL, val16);
#endif // !CONFIG_CONCURRENT_MODE
}

void CCX_FwC2HTxRpt_8723b(PADAPTER padapter, u8 *pdata, u8 len)
{
	u8 seq_no;
	
#define	GET_8723B_C2H_TX_RPT_LIFE_TIME_OVER(_Header)	LE_BITS_TO_1BYTE((_Header + 0), 6, 1)
#define	GET_8723B_C2H_TX_RPT_RETRY_OVER(_Header)	LE_BITS_TO_1BYTE((_Header + 0), 7, 1)

	//DBG_871X("%s, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n", __func__, 
	//		*pdata, *(pdata+1), *(pdata+2), *(pdata+3), *(pdata+4), *(pdata+5), *(pdata+6), *(pdata+7));

	seq_no = *(pdata+6);

#ifdef CONFIG_XMIT_ACK
	if (GET_8723B_C2H_TX_RPT_RETRY_OVER(pdata) | GET_8723B_C2H_TX_RPT_LIFE_TIME_OVER(pdata)) {
		rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_CCX_PKT_FAIL);
	}
/*	
	else if(seq_no != padapter->xmitpriv.seq_no) {
		DBG_871X("tx_seq_no=%d, rpt_seq_no=%d\n", padapter->xmitpriv.seq_no, seq_no);
		rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_CCX_PKT_FAIL);
	} 
*/	
	else {
		rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_SUCCESS);
	}
#endif
}

s32 c2h_id_filter_ccx_8723b(u8 *buf)
{
	struct c2h_evt_hdr_88xx *c2h_evt = (struct c2h_evt_hdr_88xx *)buf;
	s32 ret = _FALSE;
	if (c2h_evt->id == C2H_CCX_TX_RPT)
		ret = _TRUE;
	
	return ret;
}


s32 c2h_handler_8723b(PADAPTER padapter, u8 *buf)
{
	struct c2h_evt_hdr_88xx *pC2hEvent = (struct c2h_evt_hdr_88xx *)buf;
	PHAL_DATA_TYPE	pHalData=GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	s32 ret = _SUCCESS;
	u8 index = 0;

	if (pC2hEvent == NULL) {
		DBG_8192C("%s(): pC2hEventis NULL\n",__FUNCTION__);
		ret = _FAIL;
		goto exit;
	}

	switch (pC2hEvent->id)
	{
		case C2H_AP_RPT_RSP:
			{
//YJ,TODO,130407
#if 0
				u4Byte c2h_ap_keeplink = _TRUE;
				if (c2hBuf[2] == 0 && c2hBuf[3] == 0)
					c2h_ap_keeplink = _FALSE;
				else
					c2h_ap_keeplink = _TRUE;

				if (_TRUE == pmlmeext->try_ap_c2h_wait) {
					if (_FALSE == c2h_ap_keeplink) {
						pmlmeext->try_ap_c2h_wait = _FALSE;
						RT_TRACE(_module_hal_init_c_, _drv_err_,("fw tell us link is off\n"));
						receive_disconnect(padapter, pmlmeinfo->network.MacAddress , 65535);
					} else  {
						RT_TRACE(_module_hal_init_c_, _drv_err_,("fw tell us link is on\n"));
					}
				} else {
					RT_TRACE(_module_hal_init_c_, _drv_err_,("we don't need this C2H\n"));
				}
				pmlmeext->check_ap_processing = _FALSE;
#endif				
			}
			break;
		case C2H_DBG:
			{
				RT_TRACE(_module_hal_init_c_, _drv_info_, ("c2h_handler_8723b: %s\n", pC2hEvent->payload));
			}
			break;

		case C2H_CCX_TX_RPT:
//			CCX_FwC2HTxRpt(padapter, QueueID, pC2hEvent->payload);
			break;

#ifdef CONFIG_BT_COEXIST
#ifdef CONFIG_PCI_HCI
		case C2H_BT_RSSI:
//			fwc2h_ODM(padapter, tmpBuf, &C2hEvent);
			//BT_FwC2hBtRssi(padapter, pC2hEvent->payload);
			break;
#endif
#endif

		case C2H_EXT_RA_RPT:
//			C2HExtRaRptHandler(padapter, pC2hEvent->payload, C2hEvent.CmdLen);
			break;

		case C2H_HW_INFO_EXCH:
			RT_TRACE(_module_hal_init_c_, _drv_info_, ("[BT], C2H_HW_INFO_EXCH\n"));
			for (index = 0; index < pC2hEvent->plen; index++)
			{
				RT_TRACE(_module_hal_init_c_, _drv_info_, ("[BT], tmpBuf[%d]=0x%x\n", index, pC2hEvent->payload[index]));
			}
			break;

#ifdef CONFIG_BT_COEXIST
		case C2H_8723B_BT_INFO:
			rtw_btcoex_BtInfoNotify(padapter, pC2hEvent->plen, pC2hEvent->payload);
			break;
#endif

#ifdef CONFIG_MP_INCLUDED
		case C2H_8723B_BT_MP_INFO:
			DBG_8192C(" %s, C2H_8723B_BT_MP_INFO pC2hEvent->plen=%d\n",__func__,pC2hEvent->plen);
			MPTBT_FwC2hBtMpCtrl(padapter, pC2hEvent->payload, pC2hEvent->plen);
			break;
#endif
		default:
			break;
	}

	// Clear event to notify FW we have read the command.
	// Note:
	//	If this field isn't clear, the FW won't update the next command message.
//	rtw_write8(padapter, REG_C2HEVT_CLEAR, C2H_EVT_HOST_CLOSE);
exit:
	return ret;
}

static void process_c2h_event(PADAPTER padapter, PC2H_EVT_HDR pC2hEvent, u8 *c2hBuf)
{
	u8				index = 0;
	PHAL_DATA_TYPE	pHalData=GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (c2hBuf == NULL) {
		DBG_8192C("%s c2hbuff is NULL\n",__FUNCTION__);
		return;
	}

	switch (pC2hEvent->CmdID)
	{
		case C2H_AP_RPT_RSP:
			#if 0
			{
			
				u4Byte c2h_ap_keeplink = _TRUE;
				if (c2hBuf[2] == 0 && c2hBuf[3] == 0)
					c2h_ap_keeplink = _FALSE;
				else
					c2h_ap_keeplink = _TRUE;

				if (_TRUE == pmlmeext->try_ap_c2h_wait) {
					if (_FALSE == c2h_ap_keeplink) {
						pmlmeext->try_ap_c2h_wait = _FALSE;
						RT_TRACE(_module_hal_init_c_, _drv_err_,("fw tell us link is off\n"));
						receive_disconnect(padapter, pmlmeinfo->network.MacAddress , 65535);
					} else	{
						RT_TRACE(_module_hal_init_c_, _drv_err_,("fw tell us link is on\n"));
					}
				} else {
					RT_TRACE(_module_hal_init_c_, _drv_err_,("we don't need this C2H\n"));
				}
				pmlmeext->check_ap_processing = _FALSE;
			}
			#endif
			break;
		case C2H_DBG:
			{
				RT_TRACE(_module_hal_init_c_, _drv_info_, ("C2HCommandHandler: %s\n", c2hBuf));
			}
			break;

		case C2H_CCX_TX_RPT:
			CCX_FwC2HTxRpt_8723b(padapter, c2hBuf, pC2hEvent->CmdLen);
			break;

#ifdef CONFIG_BT_COEXIST
#ifdef CONFIG_PCI_HCI
		case C2H_BT_RSSI:
//			fwc2h_ODM(padapter, tmpBuf, &C2hEvent);
			//BT_FwC2hBtRssi(padapter, c2hBuf);
			break;
#endif
#endif

		case C2H_EXT_RA_RPT:
//			C2HExtRaRptHandler(padapter, tmpBuf, C2hEvent.CmdLen);
			break;

		case C2H_HW_INFO_EXCH:
			RT_TRACE(_module_hal_init_c_, _drv_info_, ("[BT], C2H_HW_INFO_EXCH\n"));
			for (index = 0; index < pC2hEvent->CmdLen; index++)
			{
				RT_TRACE(_module_hal_init_c_, _drv_info_, ("[BT], tmpBuf[%d]=0x%x\n", index, c2hBuf[index]));
			}
			break;

#ifdef CONFIG_BT_COEXIST
		case C2H_8723B_BT_INFO:
			rtw_btcoex_BtInfoNotify(padapter, pC2hEvent->CmdLen, c2hBuf);
			break;
#endif

#ifdef CONFIG_MP_INCLUDED
		case C2H_8723B_BT_MP_INFO:
			MPTBT_FwC2hBtMpCtrl(padapter, c2hBuf, pC2hEvent->CmdLen);
			break;
#endif

#ifdef CONFIG_FW_C2H_DEBUG
		case C2H_8723B_FW_DEBUG:
			Debug_FwC2H(padapter, c2hBuf, pC2hEvent->CmdLen);
			break;
#endif // CONFIG_FW_C2H_DEBUG

		default:
			break;
	}

#ifndef CONFIG_C2H_PACKET_EN
	// Clear event to notify FW we have read the command.
	// Note:
	//	If this field isn't clear, the FW won't update the next command message.
	rtw_write8(padapter, REG_C2HEVT_CLEAR, C2H_EVT_HOST_CLOSE);
#endif
}

#ifdef CONFIG_C2H_PACKET_EN

static void C2HPacketHandler_8723B(PADAPTER padapter, u8 *pbuffer, u16 length)
{
	C2H_EVT_HDR 	C2hEvent;
	u8 *tmpBuf=NULL;
#ifdef CONFIG_WOWLAN
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	
	if(pwrpriv->wowlan_mode == _TRUE)
	{
		DBG_871X("%s(): return because wowolan_mode==TRUE! CMDID=%d\n", __func__, pbuffer[0]);
		return;
	}
#endif
	C2hEvent.CmdID = pbuffer[0];
	C2hEvent.CmdSeq = pbuffer[1];
	C2hEvent.CmdLen = length - 2;
	tmpBuf = pbuffer + 2;

	//DBG_871X("%s C2hEvent.CmdID:%x C2hEvent.CmdLen:%x C2hEvent.CmdSeq:%x\n",
	//		__func__, C2hEvent.CmdID, C2hEvent.CmdLen, C2hEvent.CmdSeq);
	RT_PRINT_DATA(_module_hal_init_c_, _drv_notice_, "C2HPacketHandler_8723B(): Command Content:\n", tmpBuf, C2hEvent.CmdLen);
	
	process_c2h_event(padapter, &C2hEvent, tmpBuf);
	//c2h_handler_8723b(padapter,&C2hEvent);
	return;
}

void rtl8723b_c2h_packet_handler(PADAPTER padapter, u8 *pbuf, u16 length)
{
	C2H_EVT_HDR C2hEvent;
	u8 *pdata;


	if (length == 0)
		return;

	C2hEvent.CmdID = pbuf[0];
	C2hEvent.CmdSeq = pbuf[1];
	C2hEvent.CmdLen = length - 2;
	pdata = pbuf + 2;

	DBG_8192C("%s: C2H, ID=%d seq=%d len=%d\n",
		__FUNCTION__, C2hEvent.CmdID, C2hEvent.CmdSeq, C2hEvent.CmdLen);

	switch (C2hEvent.CmdID) {
	case C2H_CCX_TX_RPT:
#ifdef CONFIG_FW_C2H_DEBUG
	case C2H_8723B_FW_DEBUG:
#endif // CONFIG_FW_C2H_DEBUG
		process_c2h_event(padapter, &C2hEvent, pdata);
		break;

	default:
		pdata = rtw_zmalloc(length);
		if (pdata == NULL)
			break;
		_rtw_memcpy(pdata, pbuf, length);
		if (rtw_c2h_packet_wk_cmd(padapter, pdata, length) == _FAIL)
			rtw_mfree(pdata, length);
		break;
	}
}

#else // !CONFIG_C2H_PACKET_EN
//
//C2H event format:
// Field	 TRIGGER		CONTENT    CMD_SEQ	CMD_LEN 	 CMD_ID
// BITS  [127:120]	[119:16]	  [15:8]		  [7:4] 	   [3:0]
//2009.10.08. by tynli.
static void C2HCommandHandler(PADAPTER padapter)
{
	C2H_EVT_HDR 	C2hEvent;
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)

	u8				*tmpBuf = NULL;
	u8				index = 0;
	u8				bCmdMsgReady = _FALSE;
	u8				U1bTmp = 0;
//	u8				QueueID = 0;

	_rtw_memset(&C2hEvent, 0, sizeof(C2H_EVT_HDR));

	C2hEvent.CmdID = rtw_read8(padapter, REG_C2HEVT_CMD_ID_8723B);
	C2hEvent.CmdLen = rtw_read8(padapter, REG_C2HEVT_CMD_LEN_8723B);
	C2hEvent.CmdSeq = rtw_read8(padapter, REG_C2HEVT_CMD_ID_8723B + 1);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_info_, "C2HCommandHandler(): ",
		&C2hEvent , sizeof(C2hEvent));

	U1bTmp = rtw_read8(padapter, REG_C2HEVT_CLEAR);
	DBG_871X("%s C2hEvent.CmdID:%x C2hEvent.CmdLen:%x C2hEvent.CmdSeq:%x\n",
			__func__, C2hEvent.CmdID, C2hEvent.CmdLen, C2hEvent.CmdSeq);

	if (U1bTmp == C2H_EVT_HOST_CLOSE)
	{
		// Not ready.
		return;
	}
	else if (U1bTmp == C2H_EVT_FW_CLOSE)
	{
		bCmdMsgReady = _TRUE;
	}
	else
	{
		// Not a valid value, reset the clear event.
		goto exit;
	}

	if(C2hEvent.CmdLen == 0)
		goto exit;
	tmpBuf = rtw_zmalloc(C2hEvent.CmdLen);
	if (tmpBuf == NULL)
		goto exit;

	// Read the content
	for (index = 0; index < C2hEvent.CmdLen; index++)
	{
		tmpBuf[index] = rtw_read8(padapter, REG_C2HEVT_CMD_ID_8723B + 2 + index);
	}

	RT_PRINT_DATA(_module_hal_init_c_, _drv_notice_, "C2HCommandHandler(): Command Content:\n", tmpBuf, C2hEvent.CmdLen);

	//process_c2h_event(padapter,&C2hEvent, tmpBuf);
	c2h_handler_8723b(padapter,&C2hEvent);
	if (tmpBuf)
		rtw_mfree(tmpBuf, C2hEvent.CmdLen);
#endif // CONFIG_SDIO_HCI || CONFIG_GSPI_HCI

#ifdef CONFIG_USB_HCI
	HAL_DATA_TYPE	*pHalData=GET_HAL_DATA(padapter);

	_rtw_memset(&C2hEvent, 0, sizeof(C2H_EVT_HDR));
	C2hEvent.CmdID = pHalData->C2hArray[USB_C2H_CMDID_OFFSET] & 0xF;
	C2hEvent.CmdLen = (pHalData->C2hArray[USB_C2H_CMDID_OFFSET] & 0xF0) >> 4;
	C2hEvent.CmdSeq =pHalData->C2hArray[USB_C2H_SEQ_OFFSET];
	c2h_handler_8723b(padapter,(u8 *)&C2hEvent);
	//process_c2h_event(padapter,&C2hEvent,&pHalData->C2hArray[USB_C2H_EVENT_OFFSET]);
#endif // CONFIG_USB_HCI

	//REG_C2HEVT_CLEAR have done in process_c2h_event
	return;
exit:
	rtw_write8(padapter, REG_C2HEVT_CLEAR, C2H_EVT_HOST_CLOSE);
	return;
}

#endif // !CONFIG_C2H_PACKET_EN

void SetHwReg8723B(PADAPTER padapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	u8 val8;
	u16 val16;
	u32 val32;

_func_enter_;

	switch (variable)
	{
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
			u16 input_b = 0, masked = 0, ioted = 0, BrateCfg = 0;
			u16 rrsr_2g_force_mask = (RRSR_11M|RRSR_5_5M|RRSR_1M);
			u16 rrsr_2g_allow_mask = (RRSR_24M|RRSR_12M|RRSR_6M|RRSR_CCK_RATES);

			HalSetBrateCfg(padapter, val, &BrateCfg);
			input_b = BrateCfg;

			/* apply force and allow mask */
			BrateCfg |= rrsr_2g_force_mask;
			BrateCfg &= rrsr_2g_allow_mask;
			masked = BrateCfg;

			#ifdef CONFIG_CMCC_TEST
			BrateCfg |= (RRSR_11M|RRSR_5_5M|RRSR_1M); /* use 11M to send ACK */
			BrateCfg |= (RRSR_24M|RRSR_18M|RRSR_12M); //CMCC_OFDM_ACK 12/18/24M
			#endif

			/* IOT consideration */
			if (mlmext_info->assoc_AP_vendor == HT_IOT_PEER_CISCO) {
				/* if peer is cisco and didn't use ofdm rate, we enable 6M ack */
				if((BrateCfg & (RRSR_24M|RRSR_12M|RRSR_6M)) == 0)
					BrateCfg |= RRSR_6M;
			}
			ioted = BrateCfg;

			pHalData->BasicRateSet = BrateCfg;

			DBG_8192C("HW_VAR_BASIC_RATE: %#x -> %#x -> %#x\n", input_b, masked, ioted);

			// Set RRSR rate table.
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

#ifdef CONFIG_BT_COEXIST
			rtw_btcoex_ScanNotify(padapter, *val?_TRUE:_FALSE);
#endif // CONFIG_BT_COEXIST
			break;

		case HW_VAR_MLME_JOIN:
			hw_var_set_mlme_join(padapter, variable, val);

#ifdef CONFIG_BT_COEXIST
			switch (*val)
			{
				case 0:
					// prepare to join
					rtw_btcoex_ConnectNotify(padapter, _TRUE);
					break;
				case 1:
					// joinbss_event callback when join res < 0
					rtw_btcoex_ConnectNotify(padapter, _FALSE);
					break;
				case 2:
					// sta add event callback
//					rtw_btcoex_MediaStatusNotify(padapter, RT_MEDIA_CONNECT);
					break;
			}
#endif // CONFIG_BT_COEXIST
			break;

		case HW_VAR_ON_RCR_AM:
			val32 = rtw_read32(padapter, REG_RCR);
			val32 |= RCR_AM;
			rtw_write32(padapter, REG_RCR, val32);
			DBG_8192C("%s, %d, RCR= %x\n", __FUNCTION__, __LINE__, rtw_read32(padapter, REG_RCR));
			break;

		case HW_VAR_OFF_RCR_AM:
			val32 = rtw_read32(padapter, REG_RCR);
			val32 &= ~RCR_AM;
			rtw_write32(padapter, REG_RCR, val32);
			DBG_8192C("%s, %d, RCR= %x\n", __FUNCTION__, __LINE__, rtw_read32(padapter, REG_RCR));
			break;

		case HW_VAR_BEACON_INTERVAL:
			rtw_write16(padapter, REG_BCN_INTERVAL, *((u16*)val));
			break;

		case HW_VAR_SLOT_TIME:
			rtw_write8(padapter, REG_SLOT, *val);
			break;

		case HW_VAR_RESP_SIFS:
#if 0
			// SIFS for OFDM Data ACK
			rtw_write8(padapter, REG_SIFS_CTX+1, val[0]);
			// SIFS for OFDM consecutive tx like CTS data!
			rtw_write8(padapter, REG_SIFS_TRX+1, val[1]);

			rtw_write8(padapter, REG_SPEC_SIFS+1, val[0]);
			rtw_write8(padapter, REG_MAC_SPEC_SIFS+1, val[0]);

			// 20100719 Joseph: Revise SIFS setting due to Hardware register definition change.
			rtw_write8(padapter, REG_R2T_SIFS+1, val[0]);
			rtw_write8(padapter, REG_T2T_SIFS+1, val[0]);

#else
			//SIFS_Timer = 0x0a0a0808;
			//RESP_SIFS for CCK
			rtw_write8(padapter, REG_RESP_SIFS_CCK, val[0]); // SIFS_T2T_CCK (0x08)
			rtw_write8(padapter, REG_RESP_SIFS_CCK+1, val[1]); //SIFS_R2T_CCK(0x08)
			//RESP_SIFS for OFDM
			rtw_write8(padapter, REG_RESP_SIFS_OFDM, val[2]); //SIFS_T2T_OFDM (0x0a)
			rtw_write8(padapter, REG_RESP_SIFS_OFDM+1, val[3]); //SIFS_R2T_OFDM(0x0a)
#endif
			break;

		case HW_VAR_ACK_PREAMBLE:
			{
				u8 regTmp;
				u8 bShortPreamble = *val;

				// Joseph marked out for Netgear 3500 TKIP channel 7 issue.(Temporarily)
				//regTmp = (pHalData->nCur40MhzPrimeSC)<<5;
				regTmp = 0;
				if (bShortPreamble) regTmp |= 0x80;
				rtw_write8(padapter, REG_RRSR+2, regTmp);
			}
			break;

		case HW_VAR_CAM_EMPTY_ENTRY:
			{
				u8	ucIndex = *val;
				u8	i;
				u32	ulCommand = 0;
				u32	ulContent = 0;
				u32	ulEncAlgo = CAM_AES;

				for (i=0; i<CAM_CONTENT_COUNT; i++)
				{
					// filled id in CAM config 2 byte
					if (i == 0)
					{
						ulContent |= (ucIndex & 0x03) | ((u16)(ulEncAlgo)<<2);
						//ulContent |= CAM_VALID;
					}
					else
					{
						ulContent = 0;
					}
					// polling bit, and No Write enable, and address
					ulCommand = CAM_CONTENT_COUNT*ucIndex+i;
					ulCommand = ulCommand | CAM_POLLINIG | CAM_WRITE;
					// write content 0 is equall to mark invalid
					rtw_write32(padapter, WCAMI, ulContent);  //delay_ms(40);
					//RT_TRACE(COMP_SEC, DBG_LOUD, ("CAM_empty_entry(): WRITE A4: %lx \n",ulContent));
					rtw_write32(padapter, RWCAM, ulCommand);  //delay_ms(40);
					//RT_TRACE(COMP_SEC, DBG_LOUD, ("CAM_empty_entry(): WRITE A0: %lx \n",ulCommand));
				}
			}
			break;

		case HW_VAR_CAM_INVALID_ALL:
			rtw_write32(padapter, RWCAM, BIT(31)|BIT(30));
			break;

		case HW_VAR_CAM_WRITE:
			{
				u32 cmd;
				u32 *cam_val = (u32*)val;

				rtw_write32(padapter, WCAMI, cam_val[0]);

				cmd = CAM_POLLINIG | CAM_WRITE | cam_val[1];
				rtw_write32(padapter, RWCAM, cmd);
			}
			break;

		case HW_VAR_AC_PARAM_VO:
			rtw_write32(padapter, REG_EDCA_VO_PARAM, *((u32*)val));
			break;

		case HW_VAR_AC_PARAM_VI:
			rtw_write32(padapter, REG_EDCA_VI_PARAM, *((u32*)val));
			break;

		case HW_VAR_AC_PARAM_BE:
			pHalData->AcParam_BE = ((u32*)(val))[0];
			rtw_write32(padapter, REG_EDCA_BE_PARAM, *((u32*)val));
			break;

		case HW_VAR_AC_PARAM_BK:
			rtw_write32(padapter, REG_EDCA_BK_PARAM, *((u32*)val));
			break;

		case HW_VAR_ACM_CTRL:
			{
				u8 ctrl = *((u8*)val);
				u8 hwctrl = 0;

				if (ctrl != 0)
				{
					hwctrl |= AcmHw_HwEn;

					if (ctrl & BIT(1)) // BE
						hwctrl |= AcmHw_BeqEn;

					if (ctrl & BIT(2)) // VI
						hwctrl |= AcmHw_ViqEn;

					if (ctrl & BIT(3)) // VO
						hwctrl |= AcmHw_VoqEn;
				}

				DBG_8192C("[HW_VAR_ACM_CTRL] Write 0x%02X\n", hwctrl);
				rtw_write8(padapter, REG_ACMHWCTRL, hwctrl);
			}
			break;

		case HW_VAR_AMPDU_FACTOR:
			{
				u32	AMPDULen =  (*((u8 *)val));

				if(AMPDULen < HT_AGG_SIZE_32K)
					AMPDULen = (0x2000 << (*((u8 *)val))) -1;
				else
					AMPDULen = 0x7fff;

				rtw_write32(padapter, REG_AMPDU_MAX_LENGTH_8723B, AMPDULen);
			}
			break;

#if 0
		case HW_VAR_RXDMA_AGG_PG_TH:
			rtw_write8(padapter, REG_RXDMA_AGG_PG_TH, *val);
			break;
#endif

		case HW_VAR_H2C_FW_PWRMODE:
			{
				u8 psmode = *val;

				// Forece leave RF low power mode for 1T1R to prevent conficting setting in Fw power
				// saving sequence. 2010.06.07. Added by tynli. Suggested by SD3 yschang.
				if (psmode != PS_MODE_ACTIVE)
				{
					ODM_RF_Saving(&pHalData->odmpriv, _TRUE);
				}

				//if (psmode != PS_MODE_ACTIVE)	{
				//	rtl8723b_set_lowpwr_lps_cmd(padapter, _TRUE);
				//} else {
				//	rtl8723b_set_lowpwr_lps_cmd(padapter, _FALSE);
				//}
				rtl8723b_set_FwPwrMode_cmd(padapter, psmode);
			}
			break;
		case HW_VAR_H2C_PS_TUNE_PARAM:
			rtl8723b_set_FwPsTuneParam_cmd(padapter);
			break;
			
		case HW_VAR_H2C_FW_JOINBSSRPT:
			rtl8723b_set_FwJoinBssRpt_cmd(padapter, *val);
			break;

#ifdef CONFIG_P2P
		case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
			rtl8723b_set_p2p_ps_offload_cmd(padapter, *val);
			break;
#endif //CONFIG_P2P
#ifdef CONFIG_TDLS
		case HW_VAR_TDLS_WRCR:
			rtw_write32(padapter, REG_RCR, rtw_read32(padapter, REG_RCR)&(~RCR_CBSSID_DATA ));
			break;
		case HW_VAR_TDLS_RS_RCR:
			rtw_write32(padapter, REG_RCR, rtw_read32(padapter, REG_RCR)|(RCR_CBSSID_DATA));
			break;
#endif //CONFIG_TDLS
#ifdef CONFIG_SW_ANTENNA_DIVERSITY
		case HW_VAR_ANTENNA_DIVERSITY_LINK:
			//SwAntDivRestAfterLink8192C(padapter);
			ODM_SwAntDivRestAfterLink(&pHalData->odmpriv);
			break;

		case HW_VAR_ANTENNA_DIVERSITY_SELECT:
			{
				u8 Optimum_antenna = *val;

				//DBG_8192C("==> HW_VAR_ANTENNA_DIVERSITY_SELECT , Ant_(%s)\n",(Optimum_antenna==2)?"A":"B");

				//PHY_SetBBReg(padapter, rFPGA0_XA_RFInterfaceOE, 0x300, Optimum_antenna);
				ODM_SetAntenna(&pHalData->odmpriv, Optimum_antenna);
			}
			break;
#endif

		case HW_VAR_EFUSE_USAGE:
			pHalData->EfuseUsedPercentage = *val;
			break;

		case HW_VAR_EFUSE_BYTES:
			pHalData->EfuseUsedBytes = *((u16*)val);
			break;

		case HW_VAR_EFUSE_BT_USAGE:
#ifdef HAL_EFUSE_MEMORY
			pHalData->EfuseHal.BTEfuseUsedPercentage = *val;
#endif
			break;

		case HW_VAR_EFUSE_BT_BYTES:
#ifdef HAL_EFUSE_MEMORY
			pHalData->EfuseHal.BTEfuseUsedBytes = *((u16*)val);
#else
			BTEfuseUsedBytes = *((u16*)val);
#endif
			break;

		case HW_VAR_FIFO_CLEARN_UP:
			{
				#define RW_RELEASE_EN		BIT(18)
				#define RXDMA_IDLE			BIT(17)

				struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
				u8 trycnt = 100;

				// pause tx
				rtw_write8(padapter, REG_TXPAUSE, 0xff);

				// keep sn
				padapter->xmitpriv.nqos_ssn = rtw_read16(padapter, REG_NQOS_SEQ);

				if (pwrpriv->bkeepfwalive != _TRUE)
				{
					/* RX DMA stop */
					val32 = rtw_read32(padapter, REG_RXPKT_NUM);
					val32 |= RW_RELEASE_EN;
					rtw_write32(padapter, REG_RXPKT_NUM, val32);
					do {
						val32 = rtw_read32(padapter, REG_RXPKT_NUM);
						val32 &= RXDMA_IDLE;
						if (val32)
							break;

						DBG_871X("%s: [HW_VAR_FIFO_CLEARN_UP] val=%x times:%d\n", __FUNCTION__, val32, trycnt);
					} while (--trycnt);
					if (trycnt == 0) {
						DBG_8192C("[HW_VAR_FIFO_CLEARN_UP] Stop RX DMA failed......\n");
					}

					// RQPN Load 0
					rtw_write16(padapter, REG_RQPN_NPQ, 0);
					rtw_write32(padapter, REG_RQPN, 0x80000000);
					rtw_mdelay_os(2);
				}
			}
			break;

#ifdef CONFIG_CONCURRENT_MODE
		case HW_VAR_CHECK_TXBUF:
			{
				u32 i;
				u8 RetryLimit = 0x01;
				u32 reg_200, reg_204;

				val16 = RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT;
				rtw_write16(padapter, REG_RL, val16);

				for (i=0; i<200; i++) // polling 200x10=2000 msec  
				{
					reg_200 = rtw_read32(padapter, 0x200);
					reg_204 = rtw_read32(padapter, 0x204);
					if (reg_200 != reg_204)
					{
						//DBG_871X("packet in tx packet buffer - 0x204=%x, 0x200=%x (%d)\n", rtw_read32(padapter, 0x204), rtw_read32(padapter, 0x200), i);
						rtw_msleep_os(10);
					}
					else
					{
						DBG_871X("[HW_VAR_CHECK_TXBUF] no packet in tx packet buffer (%d)\n", i);
						break;
					}
				}

				if (reg_200 != reg_204)
					DBG_871X("packets in tx buffer - 0x204=%x, 0x200=%x\n", reg_204, reg_200);
				
				RetryLimit = 0x30;
				val16 = RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT;
				rtw_write16(padapter, REG_RL, val16);
			}
			break;
#endif // CONFIG_CONCURRENT_MODE

		case HW_VAR_APFM_ON_MAC:
			pHalData->bMacPwrCtrlOn = *val;
#ifdef PLATFORM_LINUX
			DBG_8192C("%s: bMacPwrCtrlOn=%d\n", __func__, pHalData->bMacPwrCtrlOn);
#endif
			break;

		case HW_VAR_NAV_UPPER:
			{
				u32 usNavUpper = *((u32*)val);

				if (usNavUpper > HAL_NAV_UPPER_UNIT_8723B * 0xFF)
				{
					RT_TRACE(_module_hal_init_c_, _drv_notice_, ("The setting value (0x%08X us) of NAV_UPPER is larger than (%d * 0xFF)!!!\n", usNavUpper, HAL_NAV_UPPER_UNIT_8723B));
					break;
				}

				// The value of ((usNavUpper + HAL_NAV_UPPER_UNIT_8723B - 1) / HAL_NAV_UPPER_UNIT_8723B)
				// is getting the upper integer.
				usNavUpper = (usNavUpper + HAL_NAV_UPPER_UNIT_8723B - 1) / HAL_NAV_UPPER_UNIT_8723B;
				rtw_write8(padapter, REG_NAV_UPPER, (u8)usNavUpper);
			}
			break;

#ifndef CONFIG_C2H_PACKET_EN
		case HW_VAR_C2H_HANDLE:
			C2HCommandHandler(padapter);
			break;
#endif

		case HW_VAR_H2C_MEDIA_STATUS_RPT:
			{
				u16	mstatus_rpt = (*(u16 *)val);
				u8	mstatus, macId;

				mstatus = (u8) (mstatus_rpt & 0xFF);
				macId = (u8)(mstatus_rpt >> 8)  ;
				rtl8723b_set_FwMediaStatusRpt_cmd(padapter , mstatus, macId);
			}
			break;
		case HW_VAR_BCN_VALID:
#ifdef CONFIG_CONCURRENT_MODE
			if (padapter->iface_type == IFACE_PORT1)
			{
				val8 = rtw_read8(padapter,  REG_DWBCN1_CTRL_8723B+2);
				val8 |= BIT(0);
				rtw_write8(padapter, REG_DWBCN1_CTRL_8723B+2, val8); 
			}
			else
#endif // CONFIG_CONCURRENT_MODE
			{
				// BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2, write 1 to clear, Clear by sw
				val8 = rtw_read8(padapter, REG_TDECTRL+2);
				val8 |= BIT(0);
				rtw_write8(padapter, REG_TDECTRL+2, val8);
			}
			break;

		case HW_VAR_DL_BCN_SEL:
#ifdef CONFIG_CONCURRENT_MODE
			if (padapter->iface_type == IFACE_PORT1)
			{
				// SW_BCN_SEL - Port1
				val8 = rtw_read8(padapter, REG_DWBCN1_CTRL_8723B+2);
				val8 |= BIT(4);
				rtw_write8(padapter, REG_DWBCN1_CTRL_8723B+2, val8);
			}
			else
#endif // CONFIG_CONCURRENT_MODE
			{
				// SW_BCN_SEL - Port0
				val8 = rtw_read8(padapter, REG_DWBCN1_CTRL_8723B+2);
				val8 &= ~BIT(4);
				rtw_write8(padapter, REG_DWBCN1_CTRL_8723B+2, val8);	
			}
			break;

		case HW_VAR_DO_IQK:
			pHalData->bNeedIQK = _TRUE;
			break;

		case HW_VAR_DL_RSVD_PAGE:
#ifdef CONFIG_BT_COEXIST
			if (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == _TRUE)
			{
				rtl8723b_download_BTCoex_AP_mode_rsvd_page(padapter);
			}
			else
#endif // CONFIG_BT_COEXIST
			{
				rtl8723b_download_rsvd_page(padapter, RT_MEDIA_CONNECT);
			}
			break;

		case HW_VAR_MACID_SLEEP:
		{
			u32 reg_macid_sleep;
			u8 bit_shift;
			u8 id = *(u8*)val;

			if (id < 32) {
				reg_macid_sleep = REG_MACID_SLEEP;
				bit_shift = id;
			} else if (id < 64) {
				reg_macid_sleep = REG_MACID_SLEEP_1;
				bit_shift = id-32;
			} else if (id < 96) {
				reg_macid_sleep = REG_MACID_SLEEP_2;
				bit_shift = id-64;
			} else if (id < 128) {
				reg_macid_sleep = REG_MACID_SLEEP_3;
				bit_shift = id-96;
			} else {
				rtw_warn_on(1);
				break;
			}

			val32 = rtw_read32(padapter, reg_macid_sleep);
			DBG_8192C(FUNC_ADPT_FMT ": [HW_VAR_MACID_SLEEP] macid=%d, org reg_0x%03x=0x%08X\n",
				FUNC_ADPT_ARG(padapter), id, reg_macid_sleep, val32);

			if (val32 & BIT(bit_shift))
				break;

			val32 |= BIT(bit_shift);
			rtw_write32(padapter, reg_macid_sleep, val32);
		}
			break;

		case HW_VAR_MACID_WAKEUP:
		{
			u32 reg_macid_sleep;
			u8 bit_shift;
			u8 id = *(u8*)val;

			if (id < 32) {
				reg_macid_sleep = REG_MACID_SLEEP;
				bit_shift = id;
			} else if (id < 64) {
				reg_macid_sleep = REG_MACID_SLEEP_1;
				bit_shift = id-32;
			} else if (id < 96) {
				reg_macid_sleep = REG_MACID_SLEEP_2;
				bit_shift = id-64;
			} else if (id < 128) {
				reg_macid_sleep = REG_MACID_SLEEP_3;
				bit_shift = id-96;
			} else {
				rtw_warn_on(1);
				break;
			}

			val32 = rtw_read32(padapter, reg_macid_sleep);
			DBG_8192C(FUNC_ADPT_FMT ": [HW_VAR_MACID_WAKEUP] macid=%d, org reg_0x%03x=0x%08X\n",
				FUNC_ADPT_ARG(padapter), id, reg_macid_sleep, val32);

			if (!(val32 & BIT(bit_shift)))
				break;

			val32 &= ~BIT(bit_shift);
			rtw_write32(padapter, reg_macid_sleep, val32);
		}
			break;
#ifdef CONFIG_GPIO_WAKEUP
		case HW_SET_GPIO_WL_CTRL:
		{
			u8 enable = *val;
			u8 value = rtw_read8(padapter, 0x4e);
			if (enable && (value & BIT(6))) {
				value &= ~BIT(6);
				rtw_write8(padapter, 0x4e, value);
			} else if (enable == _FALSE){
				value |= BIT(6);
				rtw_write8(padapter, 0x4e, value);
			}
			DBG_871X("%s: set WL control, 0x4E=0x%02X\n",
					__func__, rtw_read8(padapter, 0x4e));
		}
			break;
#endif
		default:
			SetHwReg(padapter, variable, val);
			break;
	}

_func_exit_;
}

struct qinfo_8723b {
	u32 head:8;
	u32 pkt_num:7;
	u32 tail:8;
	u32 ac:2;
	u32 macid:7;
};

struct bcn_qinfo_8723b {
	u16 head:8;
	u16 pkt_num:8;
};

void dump_qinfo_8723b(void *sel, struct qinfo_8723b *info, const char *tag)
{
	//if (info->pkt_num)
	DBG_871X_SEL_NL(sel, "%shead:0x%02x, tail:0x%02x, pkt_num:%u, macid:%u, ac:%u\n"
		, tag ? tag : "", info->head, info->tail, info->pkt_num, info->macid, info->ac
	);
}

void dump_bcn_qinfo_8723b(void *sel, struct bcn_qinfo_8723b *info, const char *tag)
{
	//if (info->pkt_num)
	DBG_871X_SEL_NL(sel, "%shead:0x%02x, pkt_num:%u\n"
		, tag ? tag : "", info->head, info->pkt_num
	);
}

void dump_mac_qinfo_8723b(void *sel, _adapter *adapter)
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

	dump_qinfo_8723b(sel, (struct qinfo_8723b *)&q0_info, "Q0 ");
	dump_qinfo_8723b(sel, (struct qinfo_8723b *)&q1_info, "Q1 ");
	dump_qinfo_8723b(sel, (struct qinfo_8723b *)&q2_info, "Q2 ");
	dump_qinfo_8723b(sel, (struct qinfo_8723b *)&q3_info, "Q3 ");
	dump_qinfo_8723b(sel, (struct qinfo_8723b *)&q4_info, "Q4 ");
	dump_qinfo_8723b(sel, (struct qinfo_8723b *)&q5_info, "Q5 ");
	dump_qinfo_8723b(sel, (struct qinfo_8723b *)&q6_info, "Q6 ");
	dump_qinfo_8723b(sel, (struct qinfo_8723b *)&q7_info, "Q7 ");
	dump_qinfo_8723b(sel, (struct qinfo_8723b *)&mg_q_info, "MG ");
	dump_qinfo_8723b(sel, (struct qinfo_8723b *)&hi_q_info, "HI ");
	dump_bcn_qinfo_8723b(sel, (struct bcn_qinfo_8723b *)&bcn_q_info, "BCN ");
}

void GetHwReg8723B(PADAPTER padapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u8 val8;
	u16 val16;
	u32 val32;


	switch (variable)
	{
		case HW_VAR_TXPAUSE:
			*val = rtw_read8(padapter, REG_TXPAUSE);
			break;

		case HW_VAR_BCN_VALID:
#ifdef CONFIG_CONCURRENT_MODE
			if (padapter->iface_type == IFACE_PORT1)
			{
				val8 = rtw_read8(padapter, REG_DWBCN1_CTRL_8723B+2);
				*val = (BIT(0) & val8) ? _TRUE:_FALSE;
			}
			else
#endif
			{
				// BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2
				val8 = rtw_read8(padapter, REG_TDECTRL+2);
				*val = (BIT(0) & val8) ? _TRUE:_FALSE;
			}
			break;

		case HW_VAR_FWLPS_RF_ON:
			{
				// When we halt NIC, we should check if FW LPS is leave.
				u32 valRCR;

				if ((padapter->bSurpriseRemoved == _TRUE) ||
					(adapter_to_pwrctl(padapter)->rf_pwrstate == rf_off))
				{
					// If it is in HW/SW Radio OFF or IPS state, we do not check Fw LPS Leave,
					// because Fw is unload.
					*val = _TRUE;
				}
				else
				{
					valRCR = rtw_read32(padapter, REG_RCR);
					valRCR &= 0x00070000;
					if(valRCR)
						*val = _FALSE;
					else
						*val = _TRUE;
				}
			}
			break;

#ifdef CONFIG_ANTENNA_DIVERSITY
		case HW_VAR_CURRENT_ANTENNA:
			*val = pHalData->CurAntenna;
			break;
#endif

		case HW_VAR_EFUSE_USAGE:
			*val = pHalData->EfuseUsedPercentage;
			break;

		case HW_VAR_EFUSE_BYTES:
			*((u16*)val) = pHalData->EfuseUsedBytes;
			break;

		case HW_VAR_EFUSE_BT_USAGE:
#ifdef HAL_EFUSE_MEMORY
			*val = pHalData->EfuseHal.BTEfuseUsedPercentage;
#endif
			break;

		case HW_VAR_EFUSE_BT_BYTES:
#ifdef HAL_EFUSE_MEMORY
			*((u16*)val) = pHalData->EfuseHal.BTEfuseUsedBytes;
#else
			*((u16*)val) = BTEfuseUsedBytes;
#endif
			break;

		case HW_VAR_APFM_ON_MAC:
			*val = pHalData->bMacPwrCtrlOn;
			break;
		case HW_VAR_CHK_HI_QUEUE_EMPTY:
			val16 = rtw_read16(padapter, REG_TXPKT_EMPTY);
			*val = (val16 & BIT(10)) ? _TRUE:_FALSE;
			break;
#ifdef CONFIG_WOWLAN
		case HW_VAR_RPWM_TOG:
			*val = rtw_read8(padapter, SDIO_LOCAL_BASE|SDIO_REG_HRPWM1) & BIT7;
			break;
		case HW_VAR_WAKEUP_REASON:
			*val = rtw_read8(padapter, REG_WOWLAN_WAKE_REASON);
			if(*val == 0xEA)
				*val = 0;
			break;
		case HW_VAR_SYS_CLKR:
			*val = rtw_read8(padapter, REG_SYS_CLKR);
			break;
#endif
		case HW_VAR_DUMP_MAC_QUEUE_INFO:
			dump_mac_qinfo_8723b(val, padapter);
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
u8 SetHalDefVar8723B(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval)
{
	PHAL_DATA_TYPE pHalData;
	u8 bResult;


	pHalData = GET_HAL_DATA(padapter);
	bResult = _SUCCESS;

	switch (variable)
	{
		default:
			bResult = SetHalDefVar(padapter, variable, pval);
			break;
	}

	return bResult;
}

#ifdef CONFIG_C2H_PACKET_EN
void SetHwRegWithBuf8723B(PADAPTER padapter, u8 variable, u8 *pbuf, int len)
{
	PHAL_DATA_TYPE pHalData;

_func_enter_;

	pHalData = GET_HAL_DATA(padapter);

	switch (variable) {
	case HW_VAR_C2H_HANDLE:
		C2HPacketHandler_8723B(padapter, pbuf, len);
		break;

	default:
		break;
	}
_func_exit_;
}
#endif // CONFIG_C2H_PACKET_EN

/*
 *	Description: 
 *		Query setting of specified variable.
 */
u8 GetHalDefVar8723B(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval)
{
	PHAL_DATA_TYPE pHalData;
	u8 bResult;


	pHalData = GET_HAL_DATA(padapter);
	bResult = _SUCCESS;

	switch (variable)
	{
		case HAL_DEF_MAX_RECVBUF_SZ:
			*((u32*)pval) = MAX_RECVBUF_SZ;
			break;

		case HAL_DEF_RX_PACKET_OFFSET:
			*((u32*)pval) = RXDESC_SIZE + DRVINFO_SZ*8;
			break;

		case HW_VAR_MAX_RX_AMPDU_FACTOR:
			// Stanley@BB.SD3 suggests 16K can get stable performance
			// The experiment was done on SDIO interface
			// coding by Lucas@20130730
			*(HT_CAP_AMPDU_FACTOR*)pval = MAX_AMPDU_FACTOR_16K;
			break;
		case HAL_DEF_TX_LDPC:
		case HAL_DEF_RX_LDPC:
			*((u8 *)pval) = _FALSE;
			break;
		case HAL_DEF_TX_STBC:
			*((u8 *)pval) = 0;
			break;
		case HAL_DEF_RX_STBC:
			*((u8 *)pval) = 1;
			break;
		case HAL_DEF_EXPLICIT_BEAMFORMER:
		case HAL_DEF_EXPLICIT_BEAMFORMEE:
			*((u8 *)pval) = _FALSE;
			break;

		case HW_DEF_RA_INFO_DUMP:
			{
				u8 mac_id = *(u8*)pval;
				u32 cmd;
				u32 ra_info1, ra_info2;
				u32 rate_mask1, rate_mask2;
				u8 curr_tx_rate,curr_tx_sgi,hight_rate,lowest_rate;			
				
				DBG_8192C("============ RA status check  Mac_id:%d ===================\n", mac_id);

				cmd = 0x40000100 | mac_id;
				rtw_write32(padapter, REG_HMEBOX_DBG_2_8723B, cmd);
				rtw_msleep_os(10);
				ra_info1 = rtw_read32(padapter, 0x2F0);
				curr_tx_rate = ra_info1&0x7F;
				curr_tx_sgi = (ra_info1>>7)&0x01;
				DBG_8192C("[ ra_info1:0x%08x ] =>cur_tx_rate= %s,cur_sgi:%d, PWRSTS = 0x%02x  \n",
					ra_info1,						
					HDATA_RATE(curr_tx_rate),
					curr_tx_sgi,
					(ra_info1>>8)  & 0x07);

				cmd = 0x40000400 | mac_id;
				rtw_write32(padapter, REG_HMEBOX_DBG_2_8723B,cmd);
				rtw_msleep_os(10);
				ra_info1 = rtw_read32(padapter, 0x2F0);
				ra_info2 = rtw_read32(padapter, 0x2F4);
				rate_mask1 = rtw_read32(padapter, 0x2F8);
				rate_mask2 = rtw_read32(padapter, 0x2FC);
				hight_rate = ra_info2&0xFF;
				lowest_rate = (ra_info2>>8)  & 0xFF;
					
				DBG_8192C("[ ra_info1:0x%08x ] =>RSSI=%d, BW_setting=0x%02x, DISRA=0x%02x, VHT_EN=0x%02x\n",
					ra_info1,
					ra_info1&0xFF,
					(ra_info1>>8)  & 0xFF,
					(ra_info1>>16) & 0xFF,
					(ra_info1>>24) & 0xFF);
					
				DBG_8192C("[ ra_info2:0x%08x ] =>hight_rate=%s, lowest_rate=%s, SGI=0x%02x, RateID=%d\n",
					ra_info2,
					HDATA_RATE(hight_rate),
					HDATA_RATE(lowest_rate),
					(ra_info2>>16) & 0xFF,
					(ra_info2>>24) & 0xFF);

				DBG_8192C("rate_mask2=0x%08x, rate_mask1=0x%08x\n", rate_mask2, rate_mask1);
				
			}
			break;

		case HAL_DEF_TX_PAGE_BOUNDARY:
			if (!padapter->registrypriv.wifi_spec)
			{
				*(u8*)pval = TX_PAGE_BOUNDARY_8723B;
			}
			else
			{
				*(u8*)pval = WMM_NORMAL_TX_PAGE_BOUNDARY_8723B;
			}
			break;

		case HAL_DEF_MACID_SLEEP:
			*(u8*)pval = _TRUE; // support macid sleep
			break;
		case HAL_DEF_TX_PAGE_SIZE:
			 *(( u32*)pval) = PAGE_SIZE_128;
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
	DBG_871X("%s\n", __func__);
}
#endif //CONFIG_WOWLAN

void rtl8723b_start_thread(_adapter *padapter)
{
#if (defined CONFIG_SDIO_HCI) || (defined CONFIG_GSPI_HCI)
#ifndef CONFIG_SDIO_TX_TASKLET
	struct xmit_priv *xmitpriv = &padapter->xmitpriv;

	xmitpriv->SdioXmitThread = kthread_run(rtl8723bs_xmit_thread, padapter, "RTWHALXT");
	if (IS_ERR(xmitpriv->SdioXmitThread))
	{
		RT_TRACE(_module_hal_xmit_c_, _drv_err_, ("%s: start rtl8723bs_xmit_thread FAIL!!\n", __FUNCTION__));
	}
#endif
#endif
}

void rtl8723b_stop_thread(_adapter *padapter)
{
#if (defined CONFIG_SDIO_HCI) || (defined CONFIG_GSPI_HCI)
#ifndef CONFIG_SDIO_TX_TASKLET
	struct xmit_priv *xmitpriv = &padapter->xmitpriv;

	// stop xmit_buf_thread
	if (xmitpriv->SdioXmitThread ) {
		_rtw_up_sema(&xmitpriv->SdioXmitSema);
		_rtw_down_sema(&xmitpriv->SdioXmitTerminateSema);
		xmitpriv->SdioXmitThread = 0;
	}
#endif
#endif
}

#if defined(CONFIG_CHECK_BT_HANG) && defined(CONFIG_BT_COEXIST)
extern void check_bt_status_work(void *data);
void rtl8723bs_init_checkbthang_workqueue(_adapter * adapter)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	adapter->priv_checkbt_wq = alloc_workqueue("sdio_wq", 0, 0);
#else
	adapter->priv_checkbt_wq = create_workqueue("sdio_wq");
#endif
	INIT_DELAYED_WORK(&adapter->checkbt_work, (void*)check_bt_status_work);
}

void rtl8723bs_free_checkbthang_workqueue(_adapter * adapter)
{
	if (adapter->priv_checkbt_wq) {
		cancel_delayed_work_sync(&adapter->checkbt_work);
		flush_workqueue(adapter->priv_checkbt_wq);
		destroy_workqueue(adapter->priv_checkbt_wq);
		adapter->priv_checkbt_wq = NULL;
	} 
}

void rtl8723bs_cancle_checkbthang_workqueue(_adapter * adapter)
{
	if (adapter->priv_checkbt_wq) {
		cancel_delayed_work_sync(&adapter->checkbt_work);
	}
}

void rtl8723bs_hal_check_bt_hang(_adapter * adapter)
{
	if (adapter->priv_checkbt_wq)
		queue_delayed_work(adapter->priv_checkbt_wq, &(adapter->checkbt_work), 0);
}
#endif



