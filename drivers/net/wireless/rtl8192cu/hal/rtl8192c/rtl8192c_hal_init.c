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
 *******************************************************************************/

#define _RTL8192C_HAL_INIT_C_
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_byteorder.h>
#include <rtw_efuse.h>

#include <rtl8192c_hal.h>

#ifdef CONFIG_USB_HCI
#include <usb_hal.h>
#endif

#ifdef CONFIG_PCI_HCI
#include <pci_hal.h>
#endif

static BOOLEAN
hal_EfusePgPacketWrite2ByteHeader(
	IN	PADAPTER		pAdapter,
	IN	u8				efuseType,
	IN	u16				*pAddr,
	IN	PPGPKT_STRUCT	pTargetPkt,
	IN	BOOLEAN			bPseudoTest);
static BOOLEAN
hal_EfusePgPacketWrite1ByteHeader(
	IN	PADAPTER		pAdapter, 
	IN	u8				efuseType,
	IN	u16				*pAddr,
	IN	PPGPKT_STRUCT	pTargetPkt,
	IN	BOOLEAN			bPseudoTest);
static BOOLEAN
hal_EfusePgPacketWriteData(
	IN	PADAPTER		pAdapter, 
	IN	u8				efuseType,
	IN	u16				*pAddr,
	IN	PPGPKT_STRUCT	pTargetPkt,
	IN	BOOLEAN			bPseudoTest);
static BOOLEAN 
hal_EfusePgPacketWrite_BT(
	IN	PADAPTER		pAdapter, 
	IN	u8				offset,
	IN	u8				word_en,
	IN	u8				*pData,
	IN	BOOLEAN			bPseudoTest);

static VOID
_FWDownloadEnable(
	IN	PADAPTER		Adapter,
	IN	BOOLEAN			enable
	)
{
	u8	tmp;

	if(enable)
	{
		#ifdef DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE
		{
			u8 val;
			if( (val=rtw_read8(Adapter, REG_MCUFWDL)))
				DBG_871X("DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE %s:%d REG_MCUFWDL:0x%02x\n", __FUNCTION__, __LINE__, val);
		}
		#endif
	
		// 8051 enable
		tmp = rtw_read8(Adapter, REG_SYS_FUNC_EN+1);
		rtw_write8(Adapter, REG_SYS_FUNC_EN+1, tmp|0x04);

		// MCU firmware download enable.
		tmp = rtw_read8(Adapter, REG_MCUFWDL);
		rtw_write8(Adapter, REG_MCUFWDL, tmp|0x01);

		// 8051 reset
		tmp = rtw_read8(Adapter, REG_MCUFWDL+2);
		rtw_write8(Adapter, REG_MCUFWDL+2, tmp&0xf7);
	}
	else
	{
		// MCU firmware download enable.
		tmp = rtw_read8(Adapter, REG_MCUFWDL);
		rtw_write8(Adapter, REG_MCUFWDL, tmp&0xfe);

		// Reserved for fw extension.
		//rtw_write8(Adapter, REG_MCUFWDL+1, 0x00);
	}
}


#define MAX_REG_BOLCK_SIZE	196
#define MIN_REG_BOLCK_SIZE	8

static int
_BlockWrite(
	IN		PADAPTER		Adapter,
	IN		PVOID		buffer,
	IN		u32			size
	)
{
	int ret = _SUCCESS;

#ifdef CONFIG_PCI_HCI
	u32			blockSize	= sizeof(u32);	// Use 4-byte write to download FW
	u8			*bufferPtr	= (u8 *)buffer;
	u32			*pu4BytePtr	= (u32 *)buffer;
	u32			i, offset, blockCount, remainSize;

	blockCount = size / blockSize;
	remainSize = size % blockSize;

	for(i = 0 ; i < blockCount ; i++){
		offset = i * blockSize;
		rtw_write32(Adapter, (FW_8192C_START_ADDRESS + offset), *(pu4BytePtr + i));
	}

	if(remainSize){
		offset = blockCount * blockSize;
		bufferPtr += offset;
		
		for(i = 0 ; i < remainSize ; i++){
			rtw_write8(Adapter, (FW_8192C_START_ADDRESS + offset + i), *(bufferPtr + i));
		}
	}
#else
  
#ifdef SUPPORTED_BLOCK_IO
	u32			blockSize	= MAX_REG_BOLCK_SIZE;	// Use 196-byte write to download FW	
	u32			blockSize2  	= MIN_REG_BOLCK_SIZE;	
#else
	u32			blockSize	= sizeof(u32);	// Use 4-byte write to download FW
	u32*		pu4BytePtr	= (u32*)buffer;
	u32			blockSize2  	= sizeof(u8);
#endif
	u8*			bufferPtr	= (u8*)buffer;
	u32			i, offset = 0, offset2, blockCount, remainSize, remainSize2;

	blockCount = size / blockSize;
	remainSize = size % blockSize;

	for(i = 0 ; i < blockCount ; i++){
		offset = i * blockSize;
		#ifdef SUPPORTED_BLOCK_IO
		ret = rtw_writeN(Adapter, (FW_8192C_START_ADDRESS + offset), blockSize, (bufferPtr + offset));
		#else
		ret = rtw_write32(Adapter, (FW_8192C_START_ADDRESS + offset), le32_to_cpu(*(pu4BytePtr + i)));
		#endif

		if(ret == _FAIL)
			goto exit;
	}

	if(remainSize){
		#if defined(SUPPORTED_BLOCK_IO) && defined(DBG_BLOCK_WRITE_ISSUE) //Can this be enabled?
		offset = blockCount * blockSize;
		ret = rtw_writeN(Adapter, (FW_8192C_START_ADDRESS + offset), remainSize, (bufferPtr + offset));
		goto exit;
		#endif
		offset2 = blockCount * blockSize;		
		blockCount = remainSize / blockSize2;
		remainSize2 = remainSize % blockSize2;

		for(i = 0 ; i < blockCount ; i++){
			offset = offset2 + i * blockSize2;
			#ifdef SUPPORTED_BLOCK_IO
			ret = rtw_writeN(Adapter, (FW_8192C_START_ADDRESS + offset), blockSize2, (bufferPtr + offset));
			#else
			ret = rtw_write8(Adapter, (FW_8192C_START_ADDRESS + offset ), *(bufferPtr + offset));
			#endif

			if(ret == _FAIL)
				goto exit;
		}		

		if(remainSize2)
		{
			offset += blockSize2;
			bufferPtr += offset;
			
			for(i = 0 ; i < remainSize2 ; i++){
				ret = rtw_write8(Adapter, (FW_8192C_START_ADDRESS + offset + i), *(bufferPtr + i));

				if(ret == _FAIL)
					goto exit;
			}
		}
	}
#endif

exit:
	return ret;
}

static int
_PageWrite(
	IN		PADAPTER	Adapter,
	IN		u32			page,
	IN		PVOID		buffer,
	IN		u32			size
	)
{
	u8 value8;
	u8 u8Page = (u8) (page & 0x07) ;

	value8 = (rtw_read8(Adapter, REG_MCUFWDL+2)& 0xF8 ) | u8Page ;
	rtw_write8(Adapter, REG_MCUFWDL+2,value8);
	return _BlockWrite(Adapter,buffer,size);
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
	IN		PADAPTER		Adapter,
	IN		PVOID			buffer,
	IN		u32			size
	)
{
	// Since we need dynamic decide method of dwonload fw, so we call this function to get chip version.
	// We can remove _ReadChipVersion from ReadAdapterInfo8192C later.

	int ret = _SUCCESS;
	BOOLEAN			isNormalChip;	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);	
	
	isNormalChip = IS_NORMAL_CHIP(pHalData->VersionID);

	if(isNormalChip){
		u32 	pageNums,remainSize ;
		u32 	page,offset;
		u8*	bufferPtr = (u8*)buffer;

#ifdef CONFIG_PCI_HCI
		// 20100120 Joseph: Add for 88CE normal chip. 
		// Fill in zero to make firmware image to dword alignment.
		_FillDummy(bufferPtr, &size);
#endif

		pageNums = size / MAX_PAGE_SIZE ;		
		//RT_ASSERT((pageNums <= 4), ("Page numbers should not greater then 4 \n"));			
		remainSize = size % MAX_PAGE_SIZE;		
		
		for(page = 0; page < pageNums;  page++){
			offset = page *MAX_PAGE_SIZE;
			ret = _PageWrite(Adapter,page, (bufferPtr+offset),MAX_PAGE_SIZE);			
			
			if(ret == _FAIL)
				goto exit;
		}
		if(remainSize){
			offset = pageNums *MAX_PAGE_SIZE;
			page = pageNums;
			ret = _PageWrite(Adapter,page, (bufferPtr+offset),remainSize);

			if(ret == _FAIL)
				goto exit;
		}	
		//RT_TRACE(COMP_INIT, DBG_LOUD, ("_WriteFW Done- for Normal chip.\n"));
	}
	else	{
		ret = _BlockWrite(Adapter,buffer,size);

		if(ret == _FAIL)
			goto exit;
		//RT_TRACE(COMP_INIT, DBG_LOUD, ("_WriteFW Done- for Test chip.\n"));
	}

exit:
	return ret;
}

static int _FWFreeToGo(
	IN		PADAPTER		Adapter
	)
{
	u32			counter = 0;
	u32			value32;
	u8			value8;
	u32	restarted = _FALSE;
	
	// polling CheckSum report
	do{
		value32 = rtw_read32(Adapter, REG_MCUFWDL);
	}while((counter ++ < POLLING_READY_TIMEOUT_COUNT) && (!(value32 & FWDL_ChkSum_rpt)));	

	if(counter >= POLLING_READY_TIMEOUT_COUNT){	
		DBG_8192C("chksum report faill ! REG_MCUFWDL:0x%08x\n",value32);
		return _FAIL;
	} else {
		//DBG_8192C("chksum report success ! REG_MCUFWDL:0x%08x, counter:%u\n",value32, counter);
	}
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Checksum report OK ! REG_MCUFWDL:0x%08x .\n",value32));


	value8 = rtw_read8(Adapter, REG_MCUFWDL);
	value8 |= MCUFWDL_RDY;
	value8 &= ~WINTINI_RDY;
	rtw_write8(Adapter, REG_MCUFWDL, value8);
	

POLLING_FW_READY:	
	// polling for FW ready
	counter = 0;
	do
	{
		if(rtw_read32(Adapter, REG_MCUFWDL) & WINTINI_RDY){
			//RT_TRACE(COMP_INIT, DBG_SERIOUS, ("Polling FW ready success!! REG_MCUFWDL:0x%08x .\n",PlatformIORead4Byte(Adapter, REG_MCUFWDL)) );
			return _SUCCESS;
		}
		rtw_udelay_os(5);
	}while(counter++ < POLLING_READY_TIMEOUT_COUNT);

	DBG_8192C("Polling FW ready fail!! REG_MCUFWDL:0x%08x .\n", rtw_read32(Adapter, REG_MCUFWDL));

	if(restarted == _FALSE) {
		u8 tmp = rtw_read8(Adapter, REG_SYS_FUNC_EN+1);
		DBG_8192C("Reset 51 write8 REG_SYS_FUNC_EN:0x%04x\n", tmp & ~BIT2);
		rtw_write8(Adapter, REG_SYS_FUNC_EN+1, tmp & ~BIT2);
		DBG_8192C("Reset 51 write8 REG_SYS_FUNC_EN:0x%04x\n", tmp|BIT2);
		rtw_write8(Adapter, REG_SYS_FUNC_EN+1, tmp|BIT2);
		restarted = _TRUE;
		goto POLLING_FW_READY;
	}
		
	
	return _FAIL;
	
}


VOID
rtl8192c_FirmwareSelfReset(
	IN		PADAPTER		Adapter
)
{
	u8	u1bTmp;
	u8	Delay = 100;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
		
	if((pHalData->FirmwareVersion > 0x21) ||
		(pHalData->FirmwareVersion == 0x21 &&
		pHalData->FirmwareSubVersion >= 0x01)) // after 88C Fw v33.1
	{
		//0x1cf=0x20. Inform 8051 to reset. 2009.12.25. tynli_test
		rtw_write8(Adapter, REG_HMETFR+3, 0x20);
	
		u1bTmp = rtw_read8(Adapter, REG_SYS_FUNC_EN+1);
		while(u1bTmp&BIT2)
		{
			Delay--;
			if(Delay == 0)
				break;
			rtw_udelay_os(50);
			u1bTmp = rtw_read8(Adapter, REG_SYS_FUNC_EN+1);
		}

		if((u1bTmp&BIT2) && (Delay == 0))
		{
			DBG_8192C("FirmwareDownload92C():fw reset by itself Fail!!!!!! 0x03 = %x\n", u1bTmp);
			//RT_ASSERT(FALSE, ("PowerOffAdapter8192CE(): 0x03 = %x\n", u1bTmp));
			#ifdef DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE
			{
				u8 val;
				if( (val=rtw_read8(Adapter, REG_MCUFWDL)))
					DBG_871X("DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE %s:%d REG_MCUFWDL:0x%02x\n", __FUNCTION__, __LINE__, val);
			}
			#endif
			rtw_write8(Adapter,REG_SYS_FUNC_EN+1,(rtw_read8(Adapter, REG_SYS_FUNC_EN+1)&~BIT2));
		}

		DBG_8192C("%s =====> 8051 reset success (%d) .\n", __FUNCTION__ ,Delay);
	}
}

#ifdef CONFIG_FILE_FWIMG
extern char *rtw_fw_file_path;
u8	FwBuffer8192C[FW_8192C_SIZE];
#endif //CONFIG_FILE_FWIMG
//
//	Description:
//		Download 8192C firmware code.
//
//
int FirmwareDownload92C(
	IN	PADAPTER			Adapter,
	IN	BOOLEAN			bUsedWoWLANFw
)
{	
	int	rtStatus = _SUCCESS;	
	u8 writeFW_retry = 0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	s8 			R92CFwImageFileName_TSMC[] ={RTL8192C_FW_TSMC_IMG};
	s8 			R92CFwImageFileName_UMC[] ={RTL8192C_FW_UMC_IMG};
	s8 			R92CFwImageFileName_UMC_B[] ={RTL8192C_FW_UMC_B_IMG};
#ifdef CONFIG_WOWLAN
	s8 			R92CFwImageFileName_TSMC_WW[] ={RTL8192C_FW_TSMC_WW_IMG};
	s8 			R92CFwImageFileName_UMC_WW[] ={RTL8192C_FW_UMC_WW_IMG};
	s8 			R92CFwImageFileName_UMC_B_WW[] ={RTL8192C_FW_UMC_B_WW_IMG};
#endif
	
	//s8 			R8723FwImageFileName_UMC[] ={RTL8723_FW_UMC_IMG};
	u8*			FwImage = NULL;
	u32			FwImageLen = 0;
	char*		pFwImageFileName;	
#ifdef CONFIG_WOWLAN
	u8*			FwImageWoWLAN;
	u32			FwImageWoWLANLen;
	char*		pFwImageFileName_WoWLAN;
#endif	
	u8*			pucMappedFile = NULL;
	//vivi, merge 92c and 92s into one driver, 20090817
	//vivi modify this temply, consider it later!!!!!!!!
	//PRT_FIRMWARE	pFirmware = GET_FIRMWARE_819X(Adapter);	
	//PRT_FIRMWARE_92C	pFirmware = GET_FIRMWARE_8192C(Adapter);
	PRT_FIRMWARE_92C	pFirmware = NULL;
	PRT_8192C_FIRMWARE_HDR		pFwHdr = NULL;
	u8		*pFirmwareBuf;
	u32		FirmwareLen;

	pFirmware = (PRT_FIRMWARE_92C)rtw_zvmalloc(sizeof(RT_FIRMWARE_92C));
	
	if(!pFirmware)
	{
		rtStatus = _FAIL;
		goto Exit;
	}

	if(IS_NORMAL_CHIP(pHalData->VersionID))
	{
		if(IS_VENDOR_UMC_A_CUT(pHalData->VersionID) && !IS_92C_SERIAL(pHalData->VersionID))
		{
			pFwImageFileName = R92CFwImageFileName_UMC;
			FwImage = Rtl819XFwUMCACutImageArray;
			FwImageLen = UMCACutImgArrayLength;
#ifdef CONFIG_WOWLAN
			pFwImageFileName_WoWLAN = R92CFwImageFileName_UMC_WW;
			FwImageWoWLAN= Rtl8192C_FwUMCWWImageArray;
			FwImageWoWLANLen =UMCACutWWImgArrayLength ;
#endif			
			DBG_8192C(" ===> FirmwareDownload91C() fw:Rtl819XFwImageArray_UMC\n");
		}
		else if(IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID))
		{
			// The ROM code of UMC B-cut Fw is the same as TSMC. by tynli. 2011.01.14.
			pFwImageFileName = R92CFwImageFileName_UMC_B;
			FwImage = Rtl819XFwUMCBCutImageArray;
			FwImageLen = UMCBCutImgArrayLength;
#ifdef CONFIG_WOWLAN
			pFwImageFileName_WoWLAN = R92CFwImageFileName_UMC_B_WW;
			FwImageWoWLAN= Rtl8192C_FwUMCBCutWWImageArray;
			FwImageWoWLANLen =UMCBCutWWImgArrayLength ;
#endif			
			
			DBG_8192C(" ===> FirmwareDownload91C() fw:Rtl819XFwImageArray_UMC_B\n");
		}
		else
		{
			pFwImageFileName = R92CFwImageFileName_TSMC;
			FwImage = Rtl819XFwTSMCImageArray;
			FwImageLen = TSMCImgArrayLength;
#ifdef CONFIG_WOWLAN
			pFwImageFileName_WoWLAN = R92CFwImageFileName_TSMC_WW;
			FwImageWoWLAN= Rtl8192C_FwTSMCWWImageArray;
			FwImageWoWLANLen =TSMCWWImgArrayLength ;
#endif			
			DBG_8192C(" ===> FirmwareDownload91C() fw:Rtl819XFwImageArray_TSMC\n");
		}
	}
	else
	{
	#if 0
		pFwImageFileName = TestChipFwFile;
		FwImage = Rtl8192CTestFwImg;
		FwImageLen = Rtl8192CTestFwImgLen;
		RT_TRACE(COMP_INIT, DBG_LOUD, (" ===> FirmwareDownload91C() fw:Rtl8192CTestFwImg\n"));
	#endif
	}

	//RT_TRACE(COMP_INIT, DBG_LOUD, (" ===> FirmwareDownload91C() fw:%s\n", pFwImageFileName));

	#ifdef CONFIG_FILE_FWIMG
	if(rtw_is_file_readable(rtw_fw_file_path) == _TRUE)
	{
		DBG_871X("%s accquire FW from file:%s\n", __FUNCTION__, rtw_fw_file_path);
		pFirmware->eFWSource = FW_SOURCE_IMG_FILE; // We should decided by Reg.
	}
	else
	#endif //CONFIG_FILE_FWIMG
	{
		DBG_871X("%s accquire FW from embedded image\n", __FUNCTION__);
		pFirmware->eFWSource = FW_SOURCE_HEADER_FILE;
	}


	switch(pFirmware->eFWSource)
	{
		case FW_SOURCE_IMG_FILE:
			
			#ifdef CONFIG_FILE_FWIMG
			rtStatus = rtw_retrive_from_file(rtw_fw_file_path, FwBuffer8192C, FW_8192C_SIZE);
			pFirmware->ulFwLength = rtStatus>=0?rtStatus:0;
			pFirmware->szFwBuffer = FwBuffer8192C;
			#endif //CONFIG_FILE_FWIMG

			if(pFirmware->ulFwLength <= 0)
			{
				rtStatus = _FAIL;
				goto Exit;
			}
			break;
		case FW_SOURCE_HEADER_FILE:
			if(FwImageLen > FW_8192C_SIZE){
				rtStatus = _FAIL;
				//RT_TRACE(COMP_INIT, DBG_SERIOUS, ("Firmware size exceed 0x%X. Check it.\n", FW_8192C_SIZE) );
				DBG_871X("Firmware size exceed 0x%X. Check it.\n", FW_8192C_SIZE);
				goto Exit;
			}

			pFirmware->szFwBuffer = FwImage;
			pFirmware->ulFwLength = FwImageLen;
#ifdef CONFIG_WOWLAN
			{
				pFirmware->szWoWLANFwBuffer=FwImageWoWLAN;
				pFirmware->ulWoWLANFwLength = FwImageWoWLANLen;
			}
#endif
			
			break;
	}

#ifdef CONFIG_WOWLAN	
	if(bUsedWoWLANFw)	{		
		pFirmwareBuf = pFirmware->szWoWLANFwBuffer;		
		FirmwareLen = pFirmware->ulWoWLANFwLength;		
		pFwHdr = (PRT_8192C_FIRMWARE_HDR)pFirmware->szWoWLANFwBuffer;	
	}	
	else
#endif	
	{	
		#ifdef DBG_FW_STORE_FILE_PATH //used to store firmware to file...
		if(pFirmware->ulFwLength > 0)
		{
			rtw_store_to_file(DBG_FW_STORE_FILE_PATH, pFirmware->szFwBuffer, pFirmware->ulFwLength);
		}
		#endif




		pFirmwareBuf = pFirmware->szFwBuffer;
		FirmwareLen = pFirmware->ulFwLength;

		// To Check Fw header. Added by tynli. 2009.12.04.
		pFwHdr = (PRT_8192C_FIRMWARE_HDR)pFirmware->szFwBuffer;
	}
	pHalData->FirmwareVersion =  le16_to_cpu(pFwHdr->Version); 
	pHalData->FirmwareSubVersion = le16_to_cpu(pFwHdr->Subversion); 

	//RT_TRACE(COMP_INIT, DBG_LOUD, (" FirmwareVersion(%#x), Signature(%#x)\n", 
	//	Adapter->MgntInfo.FirmwareVersion, pFwHdr->Signature));

	DBG_8192C("fw_ver=v%d, fw_subver=%d, sig=0x%x\n", 
		pHalData->FirmwareVersion, pHalData->FirmwareSubVersion, le16_to_cpu(pFwHdr->Signature)&0xFFF0);

	if(IS_FW_HEADER_EXIST(pFwHdr))
	{
		//RT_TRACE(COMP_INIT, DBG_LOUD,("Shift 32 bytes for FW header!!\n"));
		pFirmwareBuf = pFirmwareBuf + 32;
		FirmwareLen = FirmwareLen -32;
	}
		
	// Suggested by Filen. If 8051 is running in RAM code, driver should inform Fw to reset by itself,
	// or it will cause download Fw fail. 2010.02.01. by tynli.
	if(rtw_read8(Adapter, REG_MCUFWDL)&BIT7) //8051 RAM code
	{	
		rtl8192c_FirmwareSelfReset(Adapter);
		rtw_write8(Adapter, REG_MCUFWDL, 0x00);		
	}

		
	_FWDownloadEnable(Adapter, _TRUE);
	while(1) {
		u8 tmp8;
		tmp8 = rtw_read8(Adapter, REG_MCUFWDL);
		
		//reset the FWDL chksum
		rtw_write8(Adapter, REG_MCUFWDL, tmp8|FWDL_ChkSum_rpt);
		
		//tmp8 = rtw_read8(Adapter, REG_MCUFWDL);
		//DBG_8192C("Before _WriteFW, REG_MCUFWDL:0x%02x, writeFW_retry:%u\n", tmp8, writeFW_retry);
		
		rtStatus = _WriteFW(Adapter, pFirmwareBuf, FirmwareLen);
		
		//tmp8 = rtw_read8(Adapter, REG_MCUFWDL);
		//DBG_8192C("After _WriteFW, REG_MCUFWDL:0x%02x, rtStatus:%d\n", tmp8, rtStatus);

		if(rtStatus == _SUCCESS || ++writeFW_retry>3)
			break;
	} 
	_FWDownloadEnable(Adapter, _FALSE);
	if(_SUCCESS != rtStatus){
		DBG_8192C("DL Firmware failed!\n");
		goto Exit;
	}

	rtStatus = _FWFreeToGo(Adapter);
	if(_SUCCESS != rtStatus){
		DBG_8192C("DL Firmware failed!\n");
		goto Exit;
	}	
	//RT_TRACE(COMP_INIT, DBG_LOUD, (" Firmware is ready to run!\n"));

Exit:

	if(pFirmware) {
		rtw_vmfree((u8*)pFirmware, sizeof(RT_FIRMWARE_92C));
	}

	//RT_TRACE(COMP_INIT, DBG_LOUD, (" <=== FirmwareDownload91C()\n"));
	return rtStatus;

}

VOID
InitializeFirmwareVars92C(
	IN	PADAPTER		Adapter
)
{
	HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(Adapter);

	// Init Fw LPS related.
	Adapter->pwrctrlpriv.bFwCurrentInPSMode = _FALSE;

	//Init H2C counter. by tynli. 2009.12.09.
	pHalData->LastHMEBoxNum = 0;
}


//===========================================

//
// Description: Prepare some information to Fw for WoWLAN.
//			(1) Download wowlan Fw.
//			(2) Download RSVD page packets.
//			(3) Enable AP offload if needed.
//
// 2011.04.12 by tynli.
//
VOID
SetFwRelatedForWoWLAN8192CU(
	IN	PADAPTER			padapter,
	IN	u8			bHostIsGoingtoSleep
)
{	
	int	status=_FAIL;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8	 bRecover = _FALSE;
	
	if(bHostIsGoingtoSleep)
	{
		//
		// 1. Before WoWLAN we need to re-download WoWLAN Fw.
		//
		status = FirmwareDownload92C(padapter, bHostIsGoingtoSleep);
		if(status != _SUCCESS)
		{
			DBG_8192C("ConfigFwRelatedForWoWLAN8192CU(): Re-Download Firmware failed!!\n");
			return;
		}
		else
		{
			DBG_8192C("ConfigFwRelatedForWoWLAN8192CU(): Re-Download Firmware Success !!\n");
		}

		//
		// 2. Re-Init the variables about Fw related setting.
		//
		InitializeFirmwareVars92C(padapter);

		
	}
}


#ifdef CONFIG_BT_COEXIST
static void _update_bt_param(_adapter *padapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);
	struct registry_priv	*registry_par = &padapter->registrypriv;

	if(2 != registry_par->bt_iso)
		pbtpriv->BT_Ant_isolation = registry_par->bt_iso;// 0:Low, 1:High, 2:From Efuse

	if(registry_par->bt_sco == 1) // 0:Idle, 1:None-SCO, 2:SCO, 3:From Counter, 4.Busy, 5.OtherBusy
		pbtpriv->BT_Service = BT_OtherAction;
	else if(registry_par->bt_sco==2)
		pbtpriv->BT_Service = BT_SCO;
	else if(registry_par->bt_sco==4)
		pbtpriv->BT_Service = BT_Busy;
	else if(registry_par->bt_sco==5)
		pbtpriv->BT_Service = BT_OtherBusy;		
	else
		pbtpriv->BT_Service = BT_Idle;

	pbtpriv->BT_Ampdu = registry_par->bt_ampdu;
	pbtpriv->bCOBT = _TRUE;
#if 1
	DBG_8192C("BT Coexistance = %s\n", (pbtpriv->BT_Coexist==_TRUE)?"enable":"disable");
	if(pbtpriv->BT_Coexist)
	{
		if(pbtpriv->BT_Ant_Num == Ant_x2)
		{
			DBG_8192C("BlueTooth BT_Ant_Num = Antx2\n");
		}
		else if(pbtpriv->BT_Ant_Num == Ant_x1)
		{
			DBG_8192C("BlueTooth BT_Ant_Num = Antx1\n");
		}
		switch(pbtpriv->BT_CoexistType)
		{
			case BT_2Wire:
				DBG_8192C("BlueTooth BT_CoexistType = BT_2Wire\n");
				break;
			case BT_ISSC_3Wire:
				DBG_8192C("BlueTooth BT_CoexistType = BT_ISSC_3Wire\n");
				break;
			case BT_Accel:
				DBG_8192C("BlueTooth BT_CoexistType = BT_Accel\n");
				break;
			case BT_CSR_BC4:
				DBG_8192C("BlueTooth BT_CoexistType = BT_CSR_BC4\n");
				break;
			case BT_RTL8756:
				DBG_8192C("BlueTooth BT_CoexistType = BT_RTL8756\n");
				break;
			default:
				DBG_8192C("BlueTooth BT_CoexistType = Unknown\n");
				break;
		}
		DBG_8192C("BlueTooth BT_Ant_isolation = %d\n", pbtpriv->BT_Ant_isolation);


		switch(pbtpriv->BT_Service)
		{
			case BT_OtherAction:
				DBG_8192C("BlueTooth BT_Service = BT_OtherAction\n");
				break;
			case BT_SCO:
				DBG_8192C("BlueTooth BT_Service = BT_SCO\n");
				break;
			case BT_Busy:
				DBG_8192C("BlueTooth BT_Service = BT_Busy\n");
				break;
			case BT_OtherBusy:
				DBG_8192C("BlueTooth BT_Service = BT_OtherBusy\n");
				break;			
			default:
				DBG_8192C("BlueTooth BT_Service = BT_Idle\n");
				break;
		}

		DBG_8192C("BT_RadioSharedType = 0x%x\n", pbtpriv->BT_RadioSharedType);
	}
#endif

}


#define GET_BT_COEXIST(priv) (&priv->bt_coexist)

void rtl8192c_ReadBluetoothCoexistInfo(
	IN	PADAPTER	Adapter,	
	IN	u8*		PROMContent,
	IN	BOOLEAN		AutoloadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	BOOLEAN			isNormal = IS_NORMAL_CHIP(pHalData->VersionID);
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);
	u8	rf_opt4;

	if(AutoloadFail){
		pbtpriv->BT_Coexist = _FALSE;
		pbtpriv->BT_CoexistType= BT_2Wire;
		pbtpriv->BT_Ant_Num = Ant_x2;
		pbtpriv->BT_Ant_isolation= 0;
		pbtpriv->BT_RadioSharedType = BT_Radio_Shared;		
		return;
	}

	if(isNormal)
	{
		pbtpriv->BT_Coexist = (((PROMContent[EEPROM_RF_OPT1]&BOARD_TYPE_NORMAL_MASK)>>5) == BOARD_USB_COMBO)?_TRUE:_FALSE;	// bit [7:5]
		rf_opt4 = PROMContent[EEPROM_RF_OPT4];
		pbtpriv->BT_CoexistType 		= ((rf_opt4&0xe)>>1);			// bit [3:1]
		pbtpriv->BT_Ant_Num 		= (rf_opt4&0x1);				// bit [0]
		pbtpriv->BT_Ant_isolation 	= ((rf_opt4&0x10)>>4);			// bit [4]
		pbtpriv->BT_RadioSharedType 	= ((rf_opt4&0x20)>>5);			// bit [5]
	}
	else
	{
		pbtpriv->BT_Coexist = (PROMContent[EEPROM_RF_OPT4] >> 4) ? _TRUE : _FALSE;	
	}
	_update_bt_param(Adapter);

}
#endif

VERSION_8192C
rtl8192c_ReadChipVersion(
	IN	PADAPTER	Adapter
	)
{
	u32			value32;
	//VERSION_8192C	version;
	u32			ChipVersion=0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	value32 = rtw_read32(Adapter, REG_SYS_CFG);

	if (value32 & TRP_VAUX_EN)
	{
#if 0
		// Test chip.
		if(IS_HARDWARE_TYPE_8723(Adapter)) {
			ChipVersion |= ((value32 & VENDOR_ID) ? CHIP_VENDOR_UMC : 0);
			ChipVersion |= ((value32 & BT_FUNC) ? CHIP_8723: 0); // RTL8723 with BT function.			
		}
		else	 {
		version = (value32 & TYPE_ID) ?VERSION_TEST_CHIP_92C :VERSION_TEST_CHIP_88C;		
	}
#else
		// tynli_test. 2011.01.10.
		if(IS_HARDWARE_TYPE_8192C(Adapter))
		{
			ChipVersion = (value32 & TYPE_ID) ? VERSION_TEST_CHIP_92C : VERSION_TEST_CHIP_88C;						
		}
		else
		{
			ChipVersion |= ((value32 & VENDOR_ID) ? CHIP_VENDOR_UMC : 0);
			ChipVersion |= ((value32 & BT_FUNC) ? CHIP_8723: 0); // RTL8723 with BT function.			
		}
#endif
	}
	else
	{
#if 0
		// Normal mass production chip.
		ChipVersion = NORMAL_CHIP;
#if !RTL8723_FPGA_TRUE_PHY_VERIFICATION
		ChipVersion |= ((value32 & TYPE_ID) ? CHIP_92C : 0);
#endif
		ChipVersion |= ((value32 & VENDOR_ID) ? CHIP_VENDOR_UMC : 0);
		ChipVersion |= ((value32 & BT_FUNC) ? CHIP_8723: 0); // RTL8723 with BT function.
		if(IS_8723_SERIES(ChipVersion))
		{
			if(IS_VENDOR_UMC(ChipVersion))
				ChipVersion |= ((value32 & CHIP_VER_RTL_MASK) ? CHIP_VENDOR_UMC_B_CUT : 0);
		}
		else
		{
			// Mark out by tynli. UMC B-cut IC will not set the SYS_CFG[19] to UMC
			// because we do not want the custmor to know. 2011.01.11.
			//if(IS_VENDOR_UMC(ChipVersion))
			{
				// To check the value of B-cut. by tynli. 2011.01.11.
				u1bTmp = (u1Byte)((value32 & CHIP_VER_RTL_MASK)>>12);
				if(u1bTmp == 1)
				{	// B-cut
					ChipVersion |= CHIP_VENDOR_UMC_B_CUT;
				}
			}
		}
#else
		// Normal mass production chip.
		ChipVersion = NORMAL_CHIP;
//#if !RTL8723_FPGA_TRUE_PHY_VERIFICATION
		ChipVersion |= ((value32 & TYPE_ID) ? RF_TYPE_2T2R : 0); //92c
//#endif
		ChipVersion |= ((value32 & VENDOR_ID) ? CHIP_VENDOR_UMC : 0);
		ChipVersion |= ((value32 & BT_FUNC) ? CHIP_8723: 0); // RTL8723 with BT function.
		if(IS_HARDWARE_TYPE_8192C(Adapter))
		{
			// 88/92C UMC B-cut IC will not set the SYS_CFG[19] to UMC
			// because we do not want the custmor to know. by tynli. 2011.01.17.
			//MSG_8192C("mask result = 0x%x is_UMC %d chipversion 0x%x\n", (value32 & CHIP_VER_RTL_MASK), IS_CHIP_VENDOR_UMC(ChipVersion), ChipVersion);
			if((!IS_CHIP_VENDOR_UMC(ChipVersion) )&& (value32 & CHIP_VER_RTL_MASK))
			{
				//MSG_8192C("chip mask result = 0x%x\n", ((value32 & CHIP_VER_RTL_MASK) | CHIP_VENDOR_UMC));
				ChipVersion |= ((value32 & CHIP_VER_RTL_MASK) | CHIP_VENDOR_UMC); // IC version (CUT)
				//MSG_8192C("chip version = 0x%x\n", ChipVersion);
			}
		}
		else
		{
			if(IS_CHIP_VENDOR_UMC(ChipVersion))
				ChipVersion |= ((value32 & CHIP_VER_RTL_MASK)); // IC version (CUT)
		}

		if(IS_92C_SERIAL(ChipVersion))
		{
			value32 = rtw_read32(Adapter, REG_HPON_FSM);
			ChipVersion |= ((CHIP_BONDING_IDENTIFIER(value32) == CHIP_BONDING_92C_1T2R) ? RF_TYPE_1T2R : 0);			
		}
		else if(IS_8723_SERIES(ChipVersion))
		{
			//RT_ASSERT(IS_HARDWARE_TYPE_8723(Adapter), ("Incorrect chip version!!\n"));
			value32 = rtw_read32(Adapter, REG_GPIO_OUTSTS);
			ChipVersion |= ((value32 & RF_RL_ID)>>20);	 //ROM code version.	
		}
#endif

	}

	//version = (VERSION_8192C)ChipVersion;

	// For multi-function consideration. Added by Roger, 2010.10.06.
	if(IS_8723_SERIES(ChipVersion))
	{
		pHalData->MultiFunc = RT_MULTI_FUNC_NONE;
		value32 = rtw_read32(Adapter, REG_MULTI_FUNC_CTRL);
		pHalData->MultiFunc =(RT_MULTI_FUNC) (pHalData->MultiFunc| ((value32 & WL_FUNC_EN) ?  RT_MULTI_FUNC_WIFI : 0) );
		pHalData->MultiFunc =(RT_MULTI_FUNC) (pHalData->MultiFunc| ((value32 & BT_FUNC_EN) ?  RT_MULTI_FUNC_BT : 0) );
		pHalData->MultiFunc =(RT_MULTI_FUNC) (pHalData->MultiFunc| ((value32 & GPS_FUNC_EN) ?  RT_MULTI_FUNC_GPS : 0) );
		pHalData->PolarityCtl = ((value32 & WL_HWPDN_SL) ?  RT_POLARITY_HIGH_ACT : RT_POLARITY_LOW_ACT);
		//MSG_8192C("ReadChipVersion(): MultiFunc(%x), PolarityCtl(%x) \n", pHalData->MultiFunc, pHalData->PolarityCtl);

		//For regulator mode. by tynli. 2011.01.14
		pHalData->RegulatorMode = ((value32 & TRP_BT_EN) ? RT_LDO_REGULATOR : RT_SWITCHING_REGULATOR);
		//MSG_8192C("ReadChipVersion(): RegulatorMode(%x) \n", pHalData->RegulatorMode);
	}

//#if DBG
#if 1
	switch(ChipVersion)
	{
		case VERSION_NORMAL_TSMC_CHIP_92C_1T2R:
			MSG_8192C("Chip Version ID: VERSION_NORMAL_TSMC_CHIP_92C_1T2R.\n");
			break;
		case VERSION_NORMAL_TSMC_CHIP_92C:
			MSG_8192C("Chip Version ID: VERSION_NORMAL_TSMC_CHIP_92C.\n");
			break;
		case VERSION_NORMAL_TSMC_CHIP_88C:
			MSG_8192C("Chip Version ID: VERSION_NORMAL_TSMC_CHIP_88C.\n");
			break;
		case VERSION_NORMAL_UMC_CHIP_92C_1T2R_A_CUT:
			MSG_8192C("Chip Version ID: VERSION_NORMAL_UMC_CHIP_92C_1T2R_A_CUT.\n");
			break;
		case VERSION_NORMAL_UMC_CHIP_92C_A_CUT:
			MSG_8192C("Chip Version ID: VERSION_NORMAL_UMC_CHIP_92C_A_CUT.\n");
			break;
		case VERSION_NORMAL_UMC_CHIP_88C_A_CUT:
			MSG_8192C("Chip Version ID: VERSION_NORMAL_UMC_CHIP_88C_A_CUT.\n");
			break;			
		case VERSION_NORMAL_UMC_CHIP_92C_1T2R_B_CUT:
			MSG_8192C("Chip Version ID: VERSION_NORMAL_UMC_CHIP_92C_1T2R_B_CUT.\n");
			break;
		case VERSION_NORMAL_UMC_CHIP_92C_B_CUT:
			MSG_8192C("Chip Version ID: VERSION_NORMAL_UMC_CHIP_92C_B_CUT.\n");
			break;
		case VERSION_NORMAL_UMC_CHIP_88C_B_CUT:
			MSG_8192C("Chip Version ID: VERSION_NORMAL_UMC_CHIP_88C_B_CUT.\n");
			break;
		case VERSION_TEST_CHIP_92C:
			MSG_8192C("Chip Version ID: VERSION_TEST_CHIP_92C.\n");
			break;
		case VERSION_TEST_CHIP_88C:
			MSG_8192C("Chip Version ID: VERSION_TEST_CHIP_88C.\n");
			break;
		case VERSION_TEST_UMC_CHIP_8723:
			MSG_8192C("Chip Version ID: VERSION_TEST_UMC_CHIP_8723.\n");
			break;
		case VERSION_NORMAL_UMC_CHIP_8723_1T1R_A_CUT:
			MSG_8192C("Chip Version ID: VERSION_NORMA_UMC_CHIP_8723_1T1R_A_CUT.\n");
			break;
		case VERSION_NORMAL_UMC_CHIP_8723_1T1R_B_CUT:
			MSG_8192C("Chip Version ID: VERSION_NORMA_UMC_CHIP_8723_1T1R_B_CUT.\n");
			break;			
		default:
			MSG_8192C("Chip Version ID: ???????????????.\n");
			break;
	}
#endif

	pHalData->VersionID = ChipVersion;

	if(IS_1T2R(ChipVersion))
		pHalData->rf_type = RF_1T2R;
	else if(IS_2T2R(ChipVersion))
		pHalData->rf_type = RF_2T2R;
	else if(IS_8723_SERIES(ChipVersion))
		pHalData->rf_type = RF_1T1R;
	else
		pHalData->rf_type = RF_1T1R;

	MSG_8192C("RF_Type is %x!!\n", pHalData->rf_type);

	return ChipVersion;
}


RT_CHANNEL_DOMAIN
_HalMapChannelPlan8192C(
	IN	PADAPTER	Adapter,
	IN	u8		HalChannelPlan
	)
{
	RT_CHANNEL_DOMAIN	rtChannelDomain;

	switch(HalChannelPlan)
	{
#if 0 /* Not using EEPROM_CHANNEL_PLAN directly */
		case EEPROM_CHANNEL_PLAN_GLOBAL_DOMAIN:
			rtChannelDomain = RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN;
			break;
		case EEPROM_CHANNEL_PLAN_WORLD_WIDE_13:
			rtChannelDomain = RT_CHANNEL_DOMAIN_WORLD_WIDE_13;
			break;			
#endif /* Not using EEPROM_CHANNEL_PLAN directly */
		default:
			if(HalChannelPlan == 0xFF)
				rtChannelDomain = RT_CHANNEL_DOMAIN_WORLD_WIDE_13;
			else
				rtChannelDomain = (RT_CHANNEL_DOMAIN)HalChannelPlan;
			break;
	}
	
	return 	rtChannelDomain;

}

u8 GetEEPROMSize8192C(PADAPTER Adapter)
{
	u8	size = 0;
	u32	curRCR;

	curRCR = rtw_read16(Adapter, REG_9346CR);
	size = (curRCR & BOOT_FROM_EEPROM) ? 6 : 4; // 6: EEPROM used is 93C46, 4: boot from E-Fuse.
	
	MSG_8192C("EEPROM type is %s\n", size==4 ? "E-FUSE" : "93C46");
	
	return size;
}

void rtl8192c_HalSetBrateCfg(
	IN PADAPTER		Adapter,
	IN u8			*mBratesOS,
	OUT u16			*pBrateCfg
)
{
	u8	is_brate;
	u8	i;
	u8	brate;

	for(i=0;i<NDIS_802_11_LENGTH_RATES_EX;i++)
	{
		is_brate = mBratesOS[i] & IEEE80211_BASIC_RATE_MASK;
		brate = mBratesOS[i] & 0x7f;
		if( is_brate )
		{
			switch(brate)
			{
				case IEEE80211_CCK_RATE_1MB:	*pBrateCfg |= RATE_1M;	break;
				case IEEE80211_CCK_RATE_2MB:	*pBrateCfg |= RATE_2M;	break;
				case IEEE80211_CCK_RATE_5MB:	*pBrateCfg |= RATE_5_5M;break;
				case IEEE80211_CCK_RATE_11MB:	*pBrateCfg |= RATE_11M;	break;
				case IEEE80211_OFDM_RATE_6MB:	*pBrateCfg |= RATE_6M;	break;
				case IEEE80211_OFDM_RATE_9MB:	*pBrateCfg |= RATE_9M;	break;
				case IEEE80211_OFDM_RATE_12MB:	*pBrateCfg |= RATE_12M;	break;
				case IEEE80211_OFDM_RATE_18MB:	*pBrateCfg |= RATE_18M;	break;
				case IEEE80211_OFDM_RATE_24MB:	*pBrateCfg |= RATE_24M;	break;
				case IEEE80211_OFDM_RATE_36MB:	*pBrateCfg |= RATE_36M;	break;
				case IEEE80211_OFDM_RATE_48MB:	*pBrateCfg |= RATE_48M;	break;
				case IEEE80211_OFDM_RATE_54MB:	*pBrateCfg |= RATE_54M;	break;			
			}
		}

	}
}

void rtl8192c_free_hal_data(_adapter * padapter)
{
_func_enter_;

	DBG_8192C("=====> rtl8192c_free_hal_data =====\n");

	if(padapter->HalData)
		rtw_mfree(padapter->HalData, sizeof(HAL_DATA_TYPE));
	DBG_8192C("<===== rtl8192c_free_hal_data =====\n");

_func_exit_;
}

//===========================================================
//				Efuse related code
//===========================================================
enum{
		VOLTAGE_V25						= 0x03,
		LDOE25_SHIFT						= 28 ,
	};

static VOID
hal_EfusePowerSwitch_RTL8192C(
	IN	PADAPTER	pAdapter,
	IN	u8		bWrite,
	IN	u8		PwrState)
{
	u8	tempval;
	u16	tmpV16;

	if (PwrState == _TRUE)
	{
		// 1.2V Power: From VDDON with Power Cut(0x0000h[15]), defualt valid
		tmpV16 = rtw_read16(pAdapter,REG_SYS_ISO_CTRL);
		if( ! (tmpV16 & PWC_EV12V ) ){
			tmpV16 |= PWC_EV12V ;
			rtw_write16(pAdapter,REG_SYS_ISO_CTRL,tmpV16);
		}
		// Reset: 0x0000h[28], default valid
		tmpV16 =  rtw_read16(pAdapter,REG_SYS_FUNC_EN);
		if( !(tmpV16 & FEN_ELDR) ){
			tmpV16 |= FEN_ELDR ;
			rtw_write16(pAdapter,REG_SYS_FUNC_EN,tmpV16);
		}

		// Clock: Gated(0x0008h[5]) 8M(0x0008h[1]) clock from ANA, default valid
		tmpV16 = rtw_read16(pAdapter,REG_SYS_CLKR);
		if( (!(tmpV16 & LOADER_CLK_EN) )  ||(!(tmpV16 & ANA8M) ) ){
			tmpV16 |= (LOADER_CLK_EN |ANA8M ) ;
			rtw_write16(pAdapter,REG_SYS_CLKR,tmpV16);
		}

		if(bWrite == _TRUE)
		{
			// Enable LDO 2.5V before read/write action
			tempval = rtw_read8(pAdapter, EFUSE_TEST+3);
			tempval &= 0x0F;
			tempval |= (VOLTAGE_V25 << 4);
			rtw_write8(pAdapter, EFUSE_TEST+3, (tempval | 0x80));
		}
	}
	else
	{
		if(bWrite == _TRUE){
			// Disable LDO 2.5V after read/write action
			tempval = rtw_read8(pAdapter, EFUSE_TEST+3);
			rtw_write8(pAdapter, EFUSE_TEST+3, (tempval & 0x7F));
		}
	}
}

static VOID
hal_EfusePowerSwitch_RTL8723(
	IN	PADAPTER	pAdapter,
	IN	u8		bWrite,
	IN	u8		PwrState)
{
	u8	tempval;
	u16	tmpV16;

	if (PwrState == _TRUE)
	{
		rtw_write8(pAdapter, REG_EFUSE_ACCESS, EFUSE_ACCESS_ON);	

		// 1.2V Power: From VDDON with Power Cut(0x0000h[15]), defualt valid
		tmpV16 = rtw_read16(pAdapter,REG_SYS_ISO_CTRL);
		if( ! (tmpV16 & PWC_EV12V ) ){
			tmpV16 |= PWC_EV12V ;
			 rtw_write16(pAdapter,REG_SYS_ISO_CTRL,tmpV16);
		}
		// Reset: 0x0000h[28], default valid
		tmpV16 =  rtw_read16(pAdapter,REG_SYS_FUNC_EN);
		if( !(tmpV16 & FEN_ELDR) ){
			tmpV16 |= FEN_ELDR ;
			rtw_write16(pAdapter,REG_SYS_FUNC_EN,tmpV16);
		}

		// Clock: Gated(0x0008h[5]) 8M(0x0008h[1]) clock from ANA, default valid
		tmpV16 = rtw_read16(pAdapter,REG_SYS_CLKR);
		if( (!(tmpV16 & LOADER_CLK_EN) )  ||(!(tmpV16 & ANA8M) ) ){
			tmpV16 |= (LOADER_CLK_EN |ANA8M ) ;
			rtw_write16(pAdapter,REG_SYS_CLKR,tmpV16);
		}

		if(bWrite == _TRUE)
		{
			// Enable LDO 2.5V before read/write action
			tempval = rtw_read8(pAdapter, EFUSE_TEST+3);
			tempval &= 0x0F;
			tempval |= (VOLTAGE_V25 << 4);
			rtw_write8(pAdapter, EFUSE_TEST+3, (tempval | 0x80));
		}
	}
	else
	{
		rtw_write8(pAdapter, REG_EFUSE_ACCESS, EFUSE_ACCESS_OFF);

		if(bWrite == _TRUE){
			// Disable LDO 2.5V after read/write action
			tempval = rtw_read8(pAdapter, EFUSE_TEST+3);
			rtw_write8(pAdapter, EFUSE_TEST+3, (tempval & 0x7F));
		}
	}	
}

static VOID
rtl8192c_EfusePowerSwitch(
	IN	PADAPTER	pAdapter,
	IN	u8		bWrite,
	IN	u8		PwrState)
{
	if(IS_HARDWARE_TYPE_8192C(pAdapter))
	{
		hal_EfusePowerSwitch_RTL8192C(pAdapter, bWrite, PwrState);
	}
	else if(IS_HARDWARE_TYPE_8723(pAdapter))
	{
		hal_EfusePowerSwitch_RTL8723(pAdapter, bWrite, PwrState);
	}	
}

static VOID
ReadEFuse_RTL8192C(
	PADAPTER	Adapter,
	u16		 _offset,
	u16 		_size_byte,
	u8      	*pbuf,
	IN	BOOLEAN		bPseudoTest
	)
{
	u8	efuseTbl[EFUSE_MAP_LEN];
	u8	rtemp8[1];
	u16 	eFuse_Addr = 0;
	u8  	offset, wren;
	u16	i, j;
	u16	eFuseWord[EFUSE_MAX_SECTION][EFUSE_MAX_WORD_UNIT];
	u16	efuse_utilized = 0;
	u8	efuse_usage = 0;

	//
	// Do NOT excess total size of EFuse table. Added by Roger, 2008.11.10.
	//
	if((_offset + _size_byte)>EFUSE_MAP_LEN)
	{// total E-Fuse table is 128bytes
		//DBG_8192C("ReadEFuse_RTL8192C(): Invalid offset(%#x) with read bytes(%#x)!!\n",_offset, _size_byte);
		return;
	}

	// 0. Refresh efuse init map as all oxFF.
	for (i = 0; i < EFUSE_MAX_SECTION; i++)
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++)
			eFuseWord[i][j] = 0xFFFF;


	//
	// 1. Read the first byte to check if efuse is empty!!!
	// 
	//
	ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);	
	if(*rtemp8 != 0xFF)
	{
		efuse_utilized++;
		//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d\n", eFuse_Addr));
		eFuse_Addr++;
	}

	//
	// 2. Read real efuse content. Filter PG header and every section data.
	//
	while((*rtemp8 != 0xFF) && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN))
	{
		// Check PG header for section num.
		offset = ((*rtemp8 >> 4) & 0x0f);

		if(offset < EFUSE_MAX_SECTION)
		{
			// Get word enable value from PG header
			wren = (*rtemp8 & 0x0f);
			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Offset-%d Worden=%x\n", offset, wren));

			for(i=0; i<EFUSE_MAX_WORD_UNIT; i++)
			{
				// Check word enable condition in the section				
				if(!(wren & 0x01))
				{
					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d\n", eFuse_Addr));
					ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);	eFuse_Addr++;
					efuse_utilized++;
					eFuseWord[offset][i] = (*rtemp8 & 0xff);
					

					if(eFuse_Addr >= EFUSE_REAL_CONTENT_LEN) 
						break;

					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d\n", eFuse_Addr));
					ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);	eFuse_Addr++;
					efuse_utilized++;
					eFuseWord[offset][i] |= (((u16)*rtemp8 << 8) & 0xff00);

					if(eFuse_Addr >= EFUSE_REAL_CONTENT_LEN) 
						break;
				}
				
				wren >>= 1;
				
			}
		}

		//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d\n", eFuse_Addr));
		// Read next PG header
		ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);	
		if(*rtemp8 != 0xFF && (eFuse_Addr < 512))
		{
			efuse_utilized++;
			eFuse_Addr++;
		}
	}

	//
	// 3. Collect 16 sections and 4 word unit into Efuse map.
	//
	for(i=0; i<EFUSE_MAX_SECTION; i++)
	{
		for(j=0; j<EFUSE_MAX_WORD_UNIT; j++)
		{
			efuseTbl[(i*8)+(j*2)]=(eFuseWord[i][j] & 0xff);
			efuseTbl[(i*8)+((j*2)+1)]=((eFuseWord[i][j] >> 8) & 0xff);
		}
	}

	//
	// 4. Copy from Efuse map to output pointer memory!!!
	//
	for(i=0; i<_size_byte; i++)
	{		
		pbuf[i] = efuseTbl[_offset+i];
	}

	//
	// 5. Calculate Efuse utilization.
	//
	efuse_usage = (u8)((efuse_utilized*100)/EFUSE_REAL_CONTENT_LEN);
	Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_EFUSE_BYTES, (u8 *)&efuse_utilized);
	//Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_EFUSE_USAGE, (pu1Byte)&efuse_usage);
}

static VOID
ReadEFuse_RTL8723(
	PADAPTER	Adapter,
	u16		 _offset,
	u16 		_size_byte,
	u8      	*pbuf,
	IN	BOOLEAN		bPseudoTest
	)
{
	u8	efuseTbl[EFUSE_MAP_LEN_8723];
	u16	eFuse_Addr = 0;
	u8	offset = 0, wden = 0;
	u16	i, j;
	u16	eFuseWord[EFUSE_MAX_SECTION_8723][EFUSE_MAX_WORD_UNIT];
	u16	efuse_utilized = 0;
	u8	efuse_usage = 0;
	u8	offset_2_0=0;
	u8	efuseHeader=0, efuseExtHdr=0, efuseData=0;
	//
	// Do NOT excess total size of EFuse table. Added by Roger, 2008.11.10.
	//
	if((_offset + _size_byte)>EFUSE_MAP_LEN_8723)
	{
		//RT_TRACE(COMP_EFUSE, DBG_LOUD, ("ReadEFuse_RTL8723(): Invalid offset(%#x) with read bytes(%#x)!!\n",_offset, _size_byte));
		return;
	}

	// 0. Refresh efuse init map as all oxFF.
	for (i = 0; i < EFUSE_MAX_SECTION_8723; i++)
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++)
			eFuseWord[i][j] = 0xFFFF;

	//
	// 1. Read the first byte to check if efuse is empty!!!
	// 
	//
	ReadEFuseByte(Adapter, eFuse_Addr++, &efuseHeader, bPseudoTest);
	
	if(efuseHeader != 0xFF)
	{
		efuse_utilized++;
	}
	else
	{
		//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("EFUSE is empty\n"));
		return;
	}


	//
	// 2. Read real efuse content. Filter PG header and every section data.
	//
	while((efuseHeader != 0xFF) && AVAILABLE_EFUSE_ADDR(eFuse_Addr))
	{
		//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("efuse[%d]=%x\n", eFuse_Addr-1, efuseHeader));
	
		// Check PG header for section num.
		if(EXT_HEADER(efuseHeader))		//extended header
		{
			offset_2_0 = GET_HDR_OFFSET_2_0(efuseHeader);
			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header offset_2_0=%x\n", offset_2_0));

			ReadEFuseByte(Adapter, eFuse_Addr++, &efuseExtHdr, bPseudoTest);

			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("efuse[%d]=%x\n", eFuse_Addr-1, efuseExtHdr));

			if(efuseExtHdr != 0xff)
			{
				efuse_utilized++;
				if(ALL_WORDS_DISABLED(efuseExtHdr))
				{
					ReadEFuseByte(Adapter, eFuse_Addr++, &efuseHeader, bPseudoTest);
					if(efuseHeader != 0xff)
					{
						efuse_utilized++;
					}
					continue;
				}
				else
				{
					offset = ((efuseExtHdr & 0xF0) >> 1) | offset_2_0;
					wden = (efuseExtHdr & 0x0F);
				}
			}
			else
			{
				//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Error condition, extended = 0xff\n"));
				// We should handle this condition.
			}
		}
		else
		{
			offset = ((efuseHeader >> 4) & 0x0f);
			wden = (efuseHeader & 0x0f);
		}

		if(offset < EFUSE_MAX_SECTION_8723)
		{
			// Get word enable value from PG header
			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Offset-%d Worden=%x\n", offset, wden));

			for(i=0; i<EFUSE_MAX_WORD_UNIT; i++)
			{
				// Check word enable condition in the section				
				if(!(wden & (0x01<<i)))
				{
					ReadEFuseByte(Adapter, eFuse_Addr++, &efuseData, bPseudoTest);
					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("efuse[%d]=%x\n", eFuse_Addr-1, efuseData));
					efuse_utilized++;
					eFuseWord[offset][i] = (efuseData & 0xff);

					if(!AVAILABLE_EFUSE_ADDR(eFuse_Addr))
						break;

					ReadEFuseByte(Adapter, eFuse_Addr++, &efuseData, bPseudoTest);
					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("efuse[%d]=%x\n", eFuse_Addr-1, efuseData));
					efuse_utilized++;
					eFuseWord[offset][i] |= (((u16)efuseData << 8) & 0xff00);

					if(!AVAILABLE_EFUSE_ADDR(eFuse_Addr)) 
						break;
				}
			}
		}

		// Read next PG header
		ReadEFuseByte(Adapter, eFuse_Addr++, &efuseHeader, bPseudoTest);

		if(efuseHeader != 0xFF)
		{
			efuse_utilized++;
		}
	}

	//
	// 3. Collect 16 sections and 4 word unit into Efuse map.
	//
	for(i=0; i<EFUSE_MAX_SECTION_8723; i++)
	{
		for(j=0; j<EFUSE_MAX_WORD_UNIT; j++)
		{
			efuseTbl[(i*8)+(j*2)]=(eFuseWord[i][j] & 0xff);
			efuseTbl[(i*8)+((j*2)+1)]=((eFuseWord[i][j] >> 8) & 0xff);
		}
	}

	//
	// 4. Copy from Efuse map to output pointer memory!!!
	//
	for(i=0; i<_size_byte; i++)
	{		
		pbuf[i] = efuseTbl[_offset+i];
	}

	//
	// 5. Calculate Efuse utilization.
	//
	efuse_usage = (u8)((efuse_utilized*100)/EFUSE_REAL_CONTENT_LEN);
	Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_EFUSE_BYTES, (u8 *)&efuse_utilized);
	//Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_EFUSE_USAGE, (pu1Byte)&efuse_usage);
}

static BOOLEAN
Hal_EfuseSwitchToBank(
	IN		PADAPTER	pAdapter,
	IN		u8			bank,
	IN		BOOLEAN		bPseudoTest
	)
{
	BOOLEAN		bRet = _FALSE;
	u32		value32=0;

	//RTPRINT(FEEPROM, EFUSE_PG, ("Efuse switch bank to %d\n", bank));
	if(bPseudoTest)
	{
		fakeEfuseBank = bank;
		bRet = _TRUE;
	}
	else
	{
		if(IS_HARDWARE_TYPE_8723(pAdapter) && 
			INCLUDE_MULTI_FUNC_BT(pAdapter))
		{		
			value32 = rtw_read32(pAdapter, EFUSE_TEST);
			bRet = _TRUE;
			switch(bank)
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
			rtw_write32(pAdapter, EFUSE_TEST, value32);
		}
		else
			bRet = _TRUE;
	}
	return bRet;
}

static VOID
ReadEFuse_BT(
	PADAPTER	Adapter,
	u16		 _offset,
	u16 		_size_byte,
	u8      	*pbuf,
	IN	BOOLEAN	bPseudoTest
	)
{
	u8  	*efuseTbl;
	u16 	eFuse_Addr = 0;
	u8  	offset = 0, wden = 0;
	u16	i, j;
	u16 	**eFuseWord;
	u16	efuse_utilized = 0;
	u8	efuse_usage = 0;
	u8	offset_2_0=0;
	u8	efuseHeader=0, efuseExtHdr=0, efuseData=0;
	u8	bank=0;
	BOOLEAN	bCheckNextBank=_FALSE;

	efuseTbl = rtw_malloc(EFUSE_BT_MAP_LEN);
	if(efuseTbl == NULL){
		DBG_8192C("efuseTbl malloc fail !\n");
		return;
	}

	eFuseWord = (u16 **)rtw_zmalloc(sizeof(u16 *)*EFUSE_BT_MAX_SECTION);
	if(eFuseWord == NULL){
		DBG_8192C("eFuseWord malloc fail !\n");
		return;
	}
	else{
		for(i=0;i<EFUSE_BT_MAX_SECTION;i++){
			eFuseWord[i]= (u16 *)rtw_zmalloc(sizeof(u16)*EFUSE_MAX_WORD_UNIT);
			if(eFuseWord[i]==NULL){
				DBG_8192C("eFuseWord[] malloc fail !\n");
				return;
			}
		}
	}

	//
	// Do NOT excess total size of EFuse table. Added by Roger, 2008.11.10.
	//
	if((_offset + _size_byte)>EFUSE_BT_MAP_LEN)
	{
		//RT_TRACE(COMP_EFUSE, DBG_LOUD, ("ReadEFuse_BT(): Invalid offset(%#x) with read bytes(%#x)!!\n",_offset, _size_byte));
		return;
	}

	// 0. Refresh efuse init map as all oxFF.
	for (i = 0; i < EFUSE_BT_MAX_SECTION; i++)
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++)
			eFuseWord[i][j] = 0xFFFF;

	for(bank=1; bank<EFUSE_MAX_BANK; bank++)
	{
		if(!Hal_EfuseSwitchToBank(Adapter, bank, bPseudoTest))
		{
			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Hal_EfuseSwitchToBank() Fail!!\n"));
			return;
		}
		eFuse_Addr = 0;
		//
		// 1. Read the first byte to check if efuse is empty!!!
		// 
		ReadEFuseByte(Adapter, eFuse_Addr++, &efuseHeader, bPseudoTest);
		
		if(efuseHeader != 0xFF)
		{
			efuse_utilized++;
		}
		else
		{
			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("EFUSE is empty\n"));
			return;
		}
		//
		// 2. Read real efuse content. Filter PG header and every section data.
		//
		while((efuseHeader != 0xFF) && AVAILABLE_EFUSE_ADDR(eFuse_Addr))
		{
			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("efuse[%d]=0x%02x (header)\n", (((bank-1)*EFUSE_REAL_CONTENT_LEN)+eFuse_Addr-1), efuseHeader));
		
			// Check PG header for section num.
			if(EXT_HEADER(efuseHeader))		//extended header
			{
				offset_2_0 = GET_HDR_OFFSET_2_0(efuseHeader);
				//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header offset_2_0=%x\n", offset_2_0));

				ReadEFuseByte(Adapter, eFuse_Addr++, &efuseExtHdr, bPseudoTest);

				//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("efuse[%d]=0x%02x (ext header)\n", (((bank-1)*EFUSE_REAL_CONTENT_LEN)+eFuse_Addr-1), efuseExtHdr));

				if(efuseExtHdr != 0xff)
				{
					efuse_utilized++;
					if(ALL_WORDS_DISABLED(efuseExtHdr))
					{
						ReadEFuseByte(Adapter, eFuse_Addr++, &efuseHeader, bPseudoTest);
						if(efuseHeader != 0xff)
						{
							efuse_utilized++;
						}
						continue;
					}
					else
					{
						offset = ((efuseExtHdr & 0xF0) >> 1) | offset_2_0;
						wden = (efuseExtHdr & 0x0F);
					}
				}
				else
				{
					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Error condition, extended = 0xff\n"));
					// We should handle this condition.
				}
			}
			else
			{
				offset = ((efuseHeader >> 4) & 0x0f);
				wden = (efuseHeader & 0x0f);
			}

			if(offset < EFUSE_BT_MAX_SECTION)
			{
				// Get word enable value from PG header
				//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Offset-%d Worden=%x\n", offset, wden));

				for(i=0; i<EFUSE_MAX_WORD_UNIT; i++)
				{
					// Check word enable condition in the section				
					if(!(wden & (0x01<<i)))
					{
						ReadEFuseByte(Adapter, eFuse_Addr++, &efuseData, bPseudoTest);
						//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("efuse[%d]=0x%02x\n", (((bank-1)*EFUSE_REAL_CONTENT_LEN)+eFuse_Addr-1), efuseData));
						efuse_utilized++;
						eFuseWord[offset][i] = (efuseData & 0xff);

						if(!AVAILABLE_EFUSE_ADDR(eFuse_Addr))
							break;

						ReadEFuseByte(Adapter, eFuse_Addr++, &efuseData, bPseudoTest);
						//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("efuse[%d]=0x%02x\n", (((bank-1)*EFUSE_REAL_CONTENT_LEN)+eFuse_Addr-1), efuseData));
						efuse_utilized++;
						eFuseWord[offset][i] |= (((u16)efuseData << 8) & 0xff00);

						if(!AVAILABLE_EFUSE_ADDR(eFuse_Addr)) 
							break;
					}
				}
			}

			// Read next PG header
			ReadEFuseByte(Adapter, eFuse_Addr++, &efuseHeader, bPseudoTest);

			if(efuseHeader != 0xFF)
			{
				efuse_utilized++;
			}
			else
			{
				if((eFuse_Addr + EFUSE_PROTECT_BYTES_BANK) >= EFUSE_REAL_CONTENT_LEN)
					bCheckNextBank = _TRUE;
				else
					bCheckNextBank = _FALSE;
			}
		}
		if(!bCheckNextBank)
		{
			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Stop to check next bank\n"));
			break;
		}
	}

	// switch bank back to bank 0 for later BT and wifi use.
	Hal_EfuseSwitchToBank(Adapter, 0, bPseudoTest);
		
	//
	// 3. Collect 16 sections and 4 word unit into Efuse map.
	//
	for(i=0; i<EFUSE_BT_MAX_SECTION; i++)
	{
		for(j=0; j<EFUSE_MAX_WORD_UNIT; j++)
		{
			efuseTbl[(i*8)+(j*2)]=(eFuseWord[i][j] & 0xff);
			efuseTbl[(i*8)+((j*2)+1)]=((eFuseWord[i][j] >> 8) & 0xff);
		}
	}

	//
	// 4. Copy from Efuse map to output pointer memory!!!
	//
	for(i=0; i<_size_byte; i++)
	{		
		pbuf[i] = efuseTbl[_offset+i];
	}
	
	//
	// 5. Calculate Efuse utilization.
	//
	efuse_usage = (u8)((efuse_utilized*100)/EFUSE_BT_REAL_CONTENT_LEN);
	if(bPseudoTest)
	{
		fakeBTEfuseUsedBytes =  (EFUSE_REAL_CONTENT_LEN*(bank-1))+eFuse_Addr-1;
	}
	else
	{
		BTEfuseUsedBytes =  (EFUSE_REAL_CONTENT_LEN*(bank-1))+eFuse_Addr-1;
	}

	for(i=0;i<EFUSE_BT_MAX_SECTION;i++)
		rtw_mfree((u8 *)eFuseWord[i], sizeof(u16)*EFUSE_MAX_WORD_UNIT);
	rtw_mfree((u8 *)eFuseWord, sizeof(u16 *)*EFUSE_BT_MAX_SECTION);
	rtw_mfree(efuseTbl, EFUSE_BT_MAP_LEN);
}


static VOID
ReadEFuseByIC(
	PADAPTER	Adapter,
	u8		efuseType,
	u16		 _offset,
	u16 		_size_byte,
	u8      	*pbuf,
	IN BOOLEAN	bPseudoTest
	)
{
	if(efuseType == EFUSE_WIFI)
	{
		if(IS_HARDWARE_TYPE_8192C(Adapter))
		{
			ReadEFuse_RTL8192C(Adapter, _offset, _size_byte, pbuf, bPseudoTest);
		}
		else if(IS_HARDWARE_TYPE_8723(Adapter))
		{
			ReadEFuse_RTL8723(Adapter, _offset, _size_byte, pbuf, bPseudoTest);
		}
	}
	else
		ReadEFuse_BT(Adapter, _offset, _size_byte, pbuf, bPseudoTest);
}

static VOID
ReadEFuse_Pseudo(
	PADAPTER	Adapter,
	u8		efuseType,
	u16		 _offset,
	u16 		_size_byte,
	u8      	*pbuf,
	IN BOOLEAN	bPseudoTest
	)
{
	if(efuseType == EFUSE_WIFI)
		ReadEFuse_RTL8723(Adapter, _offset, _size_byte, pbuf, bPseudoTest);
	else
		ReadEFuse_BT(Adapter, _offset, _size_byte, pbuf, bPseudoTest);
}

static VOID
rtl8192c_ReadEFuse(
	PADAPTER	Adapter,
	u8		efuseType,
	u16		_offset,
	u16 		_size_byte,
	u8      	*pbuf,
	IN	BOOLEAN	bPseudoTest
	)
{
	if(bPseudoTest)
	{
		ReadEFuse_Pseudo(Adapter, efuseType, _offset, _size_byte, pbuf, bPseudoTest);
	}
	else
	{
		ReadEFuseByIC(Adapter, efuseType, _offset, _size_byte, pbuf, bPseudoTest);
	}
}

static VOID
Hal_EFUSEGetEfuseDefinition(
	IN		PADAPTER	pAdapter,
	IN		u8		efuseType,
	IN		u8		type,
	OUT		PVOID		*pOut
	)
{
	switch(type)
	{
		case TYPE_EFUSE_MAX_SECTION:
			{
				u8	*pMax_section;
				pMax_section = (u8 *)pOut;

				if(efuseType == EFUSE_WIFI)
				{
					if(IS_HARDWARE_TYPE_8192C(pAdapter))
					{
						*pMax_section = EFUSE_MAX_SECTION;
					}
					else if(IS_HARDWARE_TYPE_8723(pAdapter))
					{
						*pMax_section = EFUSE_MAX_SECTION_8723;
					}
				}
				else
					*pMax_section = EFUSE_BT_MAX_SECTION;
			}
			break;
		case TYPE_EFUSE_REAL_CONTENT_LEN:
			{
				u16 *pu2Tmp;
				pu2Tmp = (u16 *)pOut;
				if(efuseType == EFUSE_WIFI)
					*pu2Tmp = EFUSE_REAL_CONTENT_LEN;
				else
					*pu2Tmp = EFUSE_BT_REAL_CONTENT_LEN;
			}
			break;
		case TYPE_AVAILABLE_EFUSE_BYTES_BANK:
			{
				u16	*pu2Tmp;
				pu2Tmp = (u16 *)pOut;
				if(efuseType == EFUSE_WIFI)
					*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN-EFUSE_OOB_PROTECT_BYTES);
				else
					*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN-EFUSE_PROTECT_BYTES_BANK);
			}
			break;
		case TYPE_AVAILABLE_EFUSE_BYTES_TOTAL:
			{
				u16 *pu2Tmp;
				pu2Tmp = (u16 *)pOut;
				if(efuseType == EFUSE_WIFI)
					*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN-EFUSE_OOB_PROTECT_BYTES);
				else
					*pu2Tmp = (u16)(EFUSE_BT_REAL_CONTENT_LEN-(EFUSE_PROTECT_BYTES_BANK*3));
			}
			break;
		case TYPE_EFUSE_MAP_LEN:
			{
				u16 *pu2Tmp;
				pu2Tmp = (u16 *)pOut;

				if(efuseType == EFUSE_WIFI)
				{
					if(IS_HARDWARE_TYPE_8192C(pAdapter))
					{
						*pu2Tmp = (u16)EFUSE_MAP_LEN;
					}
					else if(IS_HARDWARE_TYPE_8723(pAdapter))
					{
						*pu2Tmp = (u16)EFUSE_MAP_LEN_8723;
					}
				}
				else
					*pu2Tmp = (u16)EFUSE_BT_MAP_LEN;
			}
			break;
		case TYPE_EFUSE_PROTECT_BYTES_BANK:
			{
				u8 *pu1Tmp;
				pu1Tmp = (u8 *)pOut;
				if(efuseType == EFUSE_WIFI)
					*pu1Tmp = (u8)(EFUSE_OOB_PROTECT_BYTES);
				else
					*pu1Tmp = (u8)(EFUSE_PROTECT_BYTES_BANK);
			}
			break;
		default:
			{
				u8 *pu1Tmp;
				pu1Tmp = (u8 *)pOut;
				*pu1Tmp = 0;
			}
			break;
	}
}

static VOID
Hal_EFUSEGetEfuseDefinition_Pseudo(
	IN		PADAPTER	pAdapter,
	IN		u8		efuseType,
	IN		u8		type,
	OUT		PVOID		*pOut
	)
{
	switch(type)
	{
		case TYPE_EFUSE_MAX_SECTION:
			{
				u8	*pMax_section;
				pMax_section = (u8 *)pOut;
				if(efuseType == EFUSE_WIFI)
					*pMax_section = EFUSE_MAX_SECTION_8723;
				else
					*pMax_section = EFUSE_BT_MAX_SECTION;
			}
			break;
		case TYPE_EFUSE_REAL_CONTENT_LEN:
			{
				u16 *pu2Tmp;
				pu2Tmp = (u16 *)pOut;
				if(efuseType == EFUSE_WIFI)
					*pu2Tmp = EFUSE_REAL_CONTENT_LEN;
				else
					*pu2Tmp = EFUSE_BT_REAL_CONTENT_LEN;
			}
			break;
		case TYPE_AVAILABLE_EFUSE_BYTES_BANK:
			{
				u16 *pu2Tmp;
				pu2Tmp = (u16 *)pOut;
				if(efuseType == EFUSE_WIFI)
					*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN-EFUSE_OOB_PROTECT_BYTES);
				else
					*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN-EFUSE_PROTECT_BYTES_BANK);
			}
			break;
		case TYPE_AVAILABLE_EFUSE_BYTES_TOTAL:
			{
				u16 *pu2Tmp;
				pu2Tmp = (u16 *)pOut;
				if(efuseType == EFUSE_WIFI)
					*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN-EFUSE_OOB_PROTECT_BYTES);
				else
					*pu2Tmp = (u16)(EFUSE_BT_REAL_CONTENT_LEN-(EFUSE_PROTECT_BYTES_BANK*3));
			}
			break;
		case TYPE_EFUSE_MAP_LEN:
			{
				u16 *pu2Tmp;
				pu2Tmp = (u16 *)pOut;
				if(efuseType == EFUSE_WIFI)
					*pu2Tmp = (u16)EFUSE_MAP_LEN_8723;
				else
					*pu2Tmp = (u16)EFUSE_BT_MAP_LEN;
			}
			break;
		case TYPE_EFUSE_PROTECT_BYTES_BANK:
			{
				u8 *pu1Tmp;
				pu1Tmp = (u8 *)pOut;
				if(efuseType == EFUSE_WIFI)
					*pu1Tmp = (u8)(EFUSE_OOB_PROTECT_BYTES);
				else
					*pu1Tmp = (u8)(EFUSE_PROTECT_BYTES_BANK);
			}
			break;
		default:
			{
				u8 *pu1Tmp;
				pu1Tmp = (u8 *)pOut;
				*pu1Tmp = 0;
			}
			break;
	}
}

static VOID
rtl8192c_EFUSE_GetEfuseDefinition(
	IN		PADAPTER	pAdapter,
	IN		u8		efuseType,
	IN		u8		type,
	OUT		PVOID		*pOut,
	IN		BOOLEAN		bPseudoTest
	)
{
	if(bPseudoTest)
	{
		Hal_EFUSEGetEfuseDefinition_Pseudo(pAdapter, efuseType, type, pOut);
	}
	else
	{
		Hal_EFUSEGetEfuseDefinition(pAdapter, efuseType, type, pOut);
	}
}

static u8
Hal_EfuseWordEnableDataWrite(	IN	PADAPTER	pAdapter,
							IN	u16		efuse_addr,
							IN	u8		word_en, 
							IN	u8		*data,
							IN	BOOLEAN		bPseudoTest)
{		
	u16	tmpaddr = 0;
	u16	start_addr = efuse_addr;
	u8	badworden = 0x0F;
	u8	tmpdata[8]; 
	
	_rtw_memset((PVOID)tmpdata, 0xff, PGPKT_DATA_SIZE);
	//RT_TRACE(COMP_EFUSE, DBG_LOUD, ("word_en = %x efuse_addr=%x\n", word_en, efuse_addr));

	if(!(word_en&BIT0))
	{
		tmpaddr = start_addr;
		efuse_OneByteWrite(pAdapter,start_addr++, data[0], bPseudoTest);
		efuse_OneByteWrite(pAdapter,start_addr++, data[1], bPseudoTest);

		efuse_OneByteRead(pAdapter,tmpaddr, &tmpdata[0], bPseudoTest);
		efuse_OneByteRead(pAdapter,tmpaddr+1, &tmpdata[1], bPseudoTest);
		if((data[0]!=tmpdata[0])||(data[1]!=tmpdata[1])){
			badworden &= (~BIT0);
		}
	}
	if(!(word_en&BIT1))
	{
		tmpaddr = start_addr;
		efuse_OneByteWrite(pAdapter,start_addr++, data[2], bPseudoTest);
		efuse_OneByteWrite(pAdapter,start_addr++, data[3], bPseudoTest);
				
		efuse_OneByteRead(pAdapter,tmpaddr    , &tmpdata[2], bPseudoTest);
		efuse_OneByteRead(pAdapter,tmpaddr+1, &tmpdata[3], bPseudoTest);
		if((data[2]!=tmpdata[2])||(data[3]!=tmpdata[3])){
			badworden &=( ~BIT1);
		}
	}
	if(!(word_en&BIT2))
	{
		tmpaddr = start_addr;
		efuse_OneByteWrite(pAdapter,start_addr++, data[4], bPseudoTest);
		efuse_OneByteWrite(pAdapter,start_addr++, data[5], bPseudoTest);

		efuse_OneByteRead(pAdapter,tmpaddr, &tmpdata[4], bPseudoTest);							
		efuse_OneByteRead(pAdapter,tmpaddr+1, &tmpdata[5], bPseudoTest);
		if((data[4]!=tmpdata[4])||(data[5]!=tmpdata[5])){
			badworden &=( ~BIT2);
		}
	}
	if(!(word_en&BIT3))
	{
		tmpaddr = start_addr;
		efuse_OneByteWrite(pAdapter,start_addr++, data[6], bPseudoTest);
		efuse_OneByteWrite(pAdapter,start_addr++, data[7], bPseudoTest);

		efuse_OneByteRead(pAdapter,tmpaddr, &tmpdata[6], bPseudoTest);
		efuse_OneByteRead(pAdapter,tmpaddr+1, &tmpdata[7], bPseudoTest);
		if((data[6]!=tmpdata[6])||(data[7]!=tmpdata[7])){
			badworden &=( ~BIT3);
		}
	}
	return badworden;
}

static u8
Hal_EfuseWordEnableDataWrite_Pseudo(	IN	PADAPTER	pAdapter,
							IN	u16		efuse_addr,
							IN	u8		word_en, 
							IN	u8		*data,
							IN	BOOLEAN		bPseudoTest)
{
	u8	ret=0;
	
	ret = Hal_EfuseWordEnableDataWrite(pAdapter, efuse_addr, word_en, data, bPseudoTest);

	return ret;
}

static u8
rtl8192c_Efuse_WordEnableDataWrite(	IN	PADAPTER	pAdapter,
							IN	u16		efuse_addr,
							IN	u8		word_en, 
							IN	u8		*data,
							IN	BOOLEAN		bPseudoTest)
{
	u8	ret=0;

	if(bPseudoTest)
	{
		ret = Hal_EfuseWordEnableDataWrite_Pseudo(pAdapter, efuse_addr, word_en, data, bPseudoTest);
	}
	else
	{
		ret = Hal_EfuseWordEnableDataWrite(pAdapter, efuse_addr, word_en, data, bPseudoTest);
	}
	
	return ret;
}


static u16
hal_EfuseGetCurrentSize_8192C(IN	PADAPTER	pAdapter,
	IN		BOOLEAN		bPseudoTest)
{
	int bContinual = _TRUE;

	u16	efuse_addr = 0;
	u8	hoffset=0,hworden=0;	
	u8	efuse_data,word_cnts=0;
		
	while (	bContinual && 
			efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest) && 
			(efuse_addr  < EFUSE_REAL_CONTENT_LEN) )
	{			
		if(efuse_data!=0xFF)
		{					
			hoffset = (efuse_data>>4) & 0x0F;
			hworden =  efuse_data & 0x0F;									
			word_cnts = Efuse_CalculateWordCnts(hworden);
			//read next header							
			efuse_addr = efuse_addr + (word_cnts*2)+1;						
		}
		else
		{
			bContinual = _FALSE ;			
		}
	}

	return efuse_addr;	
}

static u16
Hal_EfuseGetCurrentSize_BT(IN	PADAPTER	pAdapter,
		IN		BOOLEAN			bPseudoTest)
{
	int	bContinual = _TRUE;
	u16	efuse_addr = 0;
	u8	hoffset=0,hworden=0;	
	u8	efuse_data,word_cnts=0;
	u8	bank=0, startBank=0;
	u16	retU2=0;
	u32	total_efuse_used=0;

	if(bPseudoTest)
	{
		efuse_addr = (u16)((fakeBTEfuseUsedBytes%EFUSE_REAL_CONTENT_LEN));
		startBank = (u8)(1+(fakeBTEfuseUsedBytes/EFUSE_REAL_CONTENT_LEN));
	}
	else
	{
		efuse_addr = (u16)((BTEfuseUsedBytes%EFUSE_REAL_CONTENT_LEN));
		startBank = (u8)(1+(BTEfuseUsedBytes/EFUSE_REAL_CONTENT_LEN));
	}

	if((startBank < 1) || (startBank >= EFUSE_MAX_BANK))
		DBG_8192C("Error, bank error, bank=%d\n", bank);

	//RTPRINT(FEEPROM, EFUSE_PG, ("Hal_EfuseGetCurrentSize_BT(), start bank=%d, start_efuse_addr = %d\n", startBank, efuse_addr));
	
	for(bank=startBank; bank<EFUSE_MAX_BANK; bank++)
	{
		if(!Hal_EfuseSwitchToBank(pAdapter, bank, bPseudoTest))
			break;
		else
		{
			bContinual = _TRUE;
			if(bank != startBank)	// only when bank is switched we have to reset the efuse_addr.
				efuse_addr = 0;
		}
		
		while (	bContinual && 
				efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest) && 
				AVAILABLE_EFUSE_ADDR(efuse_addr))
		{			
			if(efuse_data!=0xFF)
			{
				if((efuse_data&0x1F) == 0x0F)		//extended header
				{
					hoffset = efuse_data;
					efuse_addr++;
					efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest);
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

		// Check if we need to check next bank efuse
		if(efuse_addr < (EFUSE_REAL_CONTENT_LEN-EFUSE_PROTECT_BYTES_BANK))
		{
			break;// don't need to check next bank.
		}
	}

	retU2 = ((bank-1)*EFUSE_REAL_CONTENT_LEN)+efuse_addr;
	if(bPseudoTest)
	{
		fakeBTEfuseUsedBytes = retU2;
		//RTPRINT(FEEPROM, EFUSE_PG, ("Hal_EfuseGetCurrentSize_BT(), return %d\n", fakeBTEfuseUsedBytes));
	}
	else
	{
		BTEfuseUsedBytes = retU2;
		//RTPRINT(FEEPROM, EFUSE_PG, ("Hal_EfuseGetCurrentSize_BT(), return %d\n", BTEfuseUsedBytes));
	}

	return retU2;
}	


static u16
hal_EfuseGetCurrentSize_8723(IN	PADAPTER	pAdapter,
		IN		BOOLEAN			bPseudoTest)
{
	int	bContinual = _TRUE;

	u16	efuse_addr = 0;
	u8	hoffset=0,hworden=0;	
	u8	efuse_data,word_cnts=0;

	if(bPseudoTest)
	{
		efuse_addr = (u16)(fakeEfuseUsedBytes);
	}
	else
	{
		pAdapter->HalFunc.GetHwRegHandler(pAdapter, HW_VAR_EFUSE_BYTES, (u8 *)&efuse_addr);
	}
	//RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfuseGetCurrentSize_8723(), start_efuse_addr = %d\n", efuse_addr));
	
	while (	bContinual && 
			efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest) && 
			AVAILABLE_EFUSE_ADDR(efuse_addr))
	{			
		if(efuse_data!=0xFF)
		{					
			if((efuse_data&0x1F) == 0x0F)		//extended header
			{
				hoffset = efuse_data;
				efuse_addr++;
				efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest);
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

	if(bPseudoTest)
	{
		fakeEfuseUsedBytes = efuse_addr;
		//RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfuseGetCurrentSize_8723(), return %d\n", fakeEfuseUsedBytes));
	}
	else
	{
		pAdapter->HalFunc.SetHwRegHandler(pAdapter, HW_VAR_EFUSE_BYTES, (u8 *)&efuse_addr);
		//RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfuseGetCurrentSize_8723(), return %d\n", efuse_addr));
	}
	
	return efuse_addr;	
}	

static u16
Hal_EfuseGetCurrentSize_Pseudo(IN	PADAPTER	pAdapter,
		IN		BOOLEAN			bPseudoTest)
{
	u16	ret=0;
	
	ret = hal_EfuseGetCurrentSize_8723(pAdapter, bPseudoTest);

	return ret;
}

static u16
rtl8192c_EfuseGetCurrentSize(
	IN	PADAPTER	pAdapter,
	IN	u8			efuseType,
	IN	BOOLEAN		bPseudoTest)
{
	u16	ret=0;

	if(efuseType == EFUSE_WIFI)
	{
		if(bPseudoTest)
		{
			ret = Hal_EfuseGetCurrentSize_Pseudo(pAdapter, bPseudoTest);
		}
		else
		{
			if(IS_HARDWARE_TYPE_8192C(pAdapter))
			{
				ret = hal_EfuseGetCurrentSize_8192C(pAdapter, bPseudoTest);
			}
			else if(IS_HARDWARE_TYPE_8723(pAdapter))
			{
				ret = hal_EfuseGetCurrentSize_8723(pAdapter, bPseudoTest);
			}
		}
	}
	else
	{
		ret = Hal_EfuseGetCurrentSize_BT(pAdapter, bPseudoTest);
	}

	return ret;
}

static int
hal_EfusePgPacketRead_8192C(	IN	PADAPTER	pAdapter,
					IN	u8			offset,
					IN	u8			*data,
					IN	BOOLEAN			bPseudoTest)
{	
	u8	ReadState = PG_STATE_HEADER;	
	
	int	bContinual = _TRUE;
	int	bDataEmpty = _TRUE ;	

	u8	efuse_data,word_cnts=0;
	u16	efuse_addr = 0;
	u8	hoffset=0,hworden=0;	
	u8	tmpidx=0;
	u8	tmpdata[8];
	
	if(data==NULL)	return _FALSE;
	if(offset>15)		return _FALSE;	
	

	_rtw_memset((PVOID)data, 0xff, sizeof(u8)*PGPKT_DATA_SIZE);
	_rtw_memset((PVOID)tmpdata, 0xff, sizeof(u8)*PGPKT_DATA_SIZE);
	
	//
	// <Roger_TODO> Efuse has been pre-programmed dummy 5Bytes at the end of Efuse by CP.
	// Skip dummy parts to prevent unexpected data read from Efuse.
	// By pass right now. 2009.02.19.
	//
	while(bContinual && (efuse_addr  < EFUSE_REAL_CONTENT_LEN) )
	{			
		//-------  Header Read -------------
		if(ReadState & PG_STATE_HEADER)
		{
			if(efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest)&&(efuse_data!=0xFF)){				
				hoffset = (efuse_data>>4) & 0x0F;
				hworden =  efuse_data & 0x0F;									
				word_cnts = Efuse_CalculateWordCnts(hworden);
				bDataEmpty = _TRUE ;

				if(hoffset==offset){
					for(tmpidx = 0;tmpidx< word_cnts*2 ;tmpidx++){
						if(efuse_OneByteRead(pAdapter, efuse_addr+1+tmpidx ,&efuse_data, bPseudoTest) ){
							tmpdata[tmpidx] = efuse_data;
							if(efuse_data!=0xff){						
								bDataEmpty = _FALSE;
							}
						}					
					}
					if(bDataEmpty==_FALSE){
						ReadState = PG_STATE_DATA;							
					}else{//read next header							
						efuse_addr = efuse_addr + (word_cnts*2)+1;
						ReadState = PG_STATE_HEADER; 
					}
				}
				else{//read next header	
					efuse_addr = efuse_addr + (word_cnts*2)+1;
					ReadState = PG_STATE_HEADER; 
				}
				
			}
			else{
				bContinual = _FALSE ;
			}
		}		
		//-------  Data section Read -------------
		else if(ReadState & PG_STATE_DATA)
		{
			efuse_WordEnableDataRead(hworden,tmpdata,data);
			efuse_addr = efuse_addr + (word_cnts*2)+1;
			ReadState = PG_STATE_HEADER; 
		}
		
	}
		
	if(	(data[0]==0xff) &&(data[1]==0xff) && (data[2]==0xff)  && (data[3]==0xff) &&
		(data[4]==0xff) &&(data[5]==0xff) && (data[6]==0xff)  && (data[7]==0xff))
		return _FALSE;
	else
		return _TRUE;

}

static int
hal_EfusePgPacketRead_8723(	IN	PADAPTER	pAdapter,
					IN	u8			offset,
					IN	u8			*data,
					IN	BOOLEAN			bPseudoTest)
{	
	u8	ReadState = PG_STATE_HEADER;	
	
	int	bContinual = _TRUE;
	int	bDataEmpty = _TRUE ;	

	u8	efuse_data,word_cnts=0;
	u16	efuse_addr = 0;
	u8	hoffset=0,hworden=0;	
	u8	tmpidx=0;
	u8	tmpdata[8];
	u8	max_section=0;
	u8	tmp_header = 0;

	EFUSE_GetEfuseDefinition(pAdapter, EFUSE_WIFI, TYPE_EFUSE_MAX_SECTION, (PVOID)&max_section, bPseudoTest);
	
	if(data==NULL)
		return _FALSE;
	if(offset>max_section)
		return _FALSE;

	_rtw_memset((PVOID)data, 0xff, sizeof(u8)*PGPKT_DATA_SIZE);
	_rtw_memset((PVOID)tmpdata, 0xff, sizeof(u8)*PGPKT_DATA_SIZE);
	
	
	//
	// <Roger_TODO> Efuse has been pre-programmed dummy 5Bytes at the end of Efuse by CP.
	// Skip dummy parts to prevent unexpected data read from Efuse.
	// By pass right now. 2009.02.19.
	//
	while(bContinual && AVAILABLE_EFUSE_ADDR(efuse_addr) )
	{			
		//-------  Header Read -------------
		if(ReadState & PG_STATE_HEADER)
		{
			if(efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest)&&(efuse_data!=0xFF))			
			{
				if(EXT_HEADER(efuse_data))
				{
					tmp_header = efuse_data;
					efuse_addr++;
					efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest);
					if(!ALL_WORDS_DISABLED(efuse_data))
					{
						hoffset = ((tmp_header & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
						hworden = efuse_data & 0x0F;						
					}
					else
					{
						DBG_8192C("Error, All words disabled\n");
						efuse_addr++;
						continue;
					}
				}
				else	
				{
					hoffset = (efuse_data>>4) & 0x0F;
					hworden =  efuse_data & 0x0F;									
				}
				word_cnts = Efuse_CalculateWordCnts(hworden);
				bDataEmpty = _TRUE ;

				if(hoffset==offset)
				{
					for(tmpidx = 0;tmpidx< word_cnts*2 ;tmpidx++)
					{
						if(efuse_OneByteRead(pAdapter, efuse_addr+1+tmpidx ,&efuse_data, bPseudoTest) )
						{
							tmpdata[tmpidx] = efuse_data;
							if(efuse_data!=0xff)
							{						
								bDataEmpty = _FALSE;
							}
						}					
					}
					if(bDataEmpty==_FALSE){
						ReadState = PG_STATE_DATA;							
					}else{//read next header							
						efuse_addr = efuse_addr + (word_cnts*2)+1;
						ReadState = PG_STATE_HEADER; 
					}
				}
				else{//read next header	
					efuse_addr = efuse_addr + (word_cnts*2)+1;
					ReadState = PG_STATE_HEADER; 
				}
				
			}
			else{
				bContinual = _FALSE ;
			}
		}		
		//-------  Data section Read -------------
		else if(ReadState & PG_STATE_DATA)
		{
			efuse_WordEnableDataRead(hworden,tmpdata,data);
			efuse_addr = efuse_addr + (word_cnts*2)+1;
			ReadState = PG_STATE_HEADER; 
		}
		
	}			
	
	if(	(data[0]==0xff) &&(data[1]==0xff) && (data[2]==0xff)  && (data[3]==0xff) &&
		(data[4]==0xff) &&(data[5]==0xff) && (data[6]==0xff)  && (data[7]==0xff))
		return _FALSE;
	else
		return _TRUE;

}

static int
Hal_EfusePgPacketRead(	IN	PADAPTER	pAdapter,
					IN	u8			offset,
					IN	u8			*data,
					IN	BOOLEAN			bPseudoTest)
{
	int	ret=0;

	if(IS_HARDWARE_TYPE_8192C(pAdapter))
	{
		ret = hal_EfusePgPacketRead_8192C(pAdapter, offset, data, bPseudoTest);
	}
	else if(IS_HARDWARE_TYPE_8723(pAdapter))
	{
		ret = hal_EfusePgPacketRead_8723(pAdapter, offset, data, bPseudoTest);
	}

	return ret;
}

static int
Hal_EfusePgPacketRead_Pseudo(	IN	PADAPTER	pAdapter,
					IN	u8			offset,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	int	ret=0;
	
	ret = hal_EfusePgPacketRead_8723(pAdapter, offset, data, bPseudoTest);

	return ret;
}

static int
rtl8192c_Efuse_PgPacketRead(	IN	PADAPTER	pAdapter,
					IN	u8			offset,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	int	ret=0;

	if(bPseudoTest)
	{
		ret = Hal_EfusePgPacketRead_Pseudo(pAdapter, offset, data, bPseudoTest);
	}
	else
	{
		ret = Hal_EfusePgPacketRead(pAdapter, offset, data, bPseudoTest);
	}

	return ret;
}

static BOOLEAN
hal_EfuseFixHeaderProcess(
	IN		PADAPTER			pAdapter,
	IN		u8					efuseType,
	IN		PPGPKT_STRUCT		pFixPkt,
	IN		u16					*pAddr,
	IN		BOOLEAN				bPseudoTest
)
{
	u8	originaldata[8], badworden=0;
	u16	efuse_addr=*pAddr;
	u32	PgWriteSuccess=0;
	
	_rtw_memset((PVOID)originaldata, 0xff, 8);
					
	if(Efuse_PgPacketRead(pAdapter, pFixPkt->offset, originaldata, bPseudoTest))
	{	//check if data exist
		badworden = Efuse_WordEnableDataWrite(pAdapter, efuse_addr+1, pFixPkt->word_en, originaldata, bPseudoTest);

		if(badworden != 0xf)	// write fail
		{
			if(efuseType == EFUSE_WIFI)
				PgWriteSuccess = Efuse_PgPacketWrite(pAdapter, pFixPkt->offset, badworden, originaldata, bPseudoTest);
			else
				PgWriteSuccess = hal_EfusePgPacketWrite_BT(pAdapter, pFixPkt->offset, badworden, originaldata, bPseudoTest);
			if(!PgWriteSuccess)
				return _FALSE;
			else
				efuse_addr = Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest);
		}
		else
		{
			efuse_addr = efuse_addr + (pFixPkt->word_cnts*2) +1;
		}
	}
	else
	{
		efuse_addr = efuse_addr + (pFixPkt->word_cnts*2) +1;
	}
	*pAddr = efuse_addr;
	return _TRUE;
}

static BOOLEAN
hal_EfusePgPacketWrite2ByteHeader(
	IN			PADAPTER		pAdapter,
	IN			u8				efuseType,
	IN			u16				*pAddr,
	IN			PPGPKT_STRUCT	pTargetPkt,
	IN			BOOLEAN			bPseudoTest)
{
	BOOLEAN		bRet=_FALSE, bContinual=_TRUE;
	u16	efuse_addr=*pAddr, efuse_max_available_len=0;
	u8	pg_header=0, tmp_header=0, pg_header_temp=0;
	u8	repeatcnt=0;

	//RTPRINT(FEEPROM, EFUSE_PG, ("Wirte 2byte header\n"));
	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_BANK, (PVOID)&efuse_max_available_len, bPseudoTest);

	while(efuse_addr < efuse_max_available_len)
	{
		pg_header = ((pTargetPkt->offset & 0x07) << 5) | 0x0F;
		//RTPRINT(FEEPROM, EFUSE_PG, ("pg_header = 0x%x\n", pg_header));
		efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
		efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);

		while(tmp_header == 0xFF)
		{
			if(repeatcnt++ > EFUSE_REPEAT_THRESHOLD_)
			{
				//RTPRINT(FEEPROM, EFUSE_PG, ("Repeat over limit for pg_header!!\n"));
				return _FALSE;
			}

			efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
			efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);
		}
		
		//to write ext_header
		if(tmp_header == pg_header)
		{
			efuse_addr++;
			pg_header_temp = pg_header;
			pg_header = ((pTargetPkt->offset & 0x78) << 1) | pTargetPkt->word_en;

			efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
			efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);

			while(tmp_header == 0xFF)
			{											
				if(repeatcnt++ > EFUSE_REPEAT_THRESHOLD_)
				{
					//RTPRINT(FEEPROM, EFUSE_PG, ("Repeat over limit for ext_header!!\n"));
					return _FALSE;
				}							

				efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
				efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);
			}

			if((tmp_header & 0x0F) == 0x0F)	//word_en PG fail
			{
				if(repeatcnt++ > EFUSE_REPEAT_THRESHOLD_)
				{
					//RTPRINT(FEEPROM, EFUSE_PG, ("Repeat over limit for word_en!!\n"));
					return _FALSE;
				}
				else
				{
					efuse_addr++;
					continue;
				}
			}
			else if(pg_header != tmp_header)	//offset PG fail						
			{
				PGPKT_STRUCT	fixPkt;
				//RTPRINT(FEEPROM, EFUSE_PG, ("Error condition for offset PG fail, need to cover the existed data\n"));
				fixPkt.offset = ((pg_header_temp & 0xE0) >> 5) | ((tmp_header & 0xF0) >> 1);
				fixPkt.word_en = tmp_header & 0x0F;					
				fixPkt.word_cnts = Efuse_CalculateWordCnts(fixPkt.word_en);
				if(!hal_EfuseFixHeaderProcess(pAdapter, efuseType, &fixPkt, &efuse_addr, bPseudoTest))
					return _FALSE;
			}
			else
			{
				bRet = _TRUE;
				break;
			}
		}
		else if ((tmp_header & 0x1F) == 0x0F)		//wrong extended header
		{
			efuse_addr+=2;
			continue;						
		}
	}

	*pAddr = efuse_addr;
	return bRet;
}

static BOOLEAN
hal_EfusePgPacketWrite1ByteHeader(
	IN			PADAPTER		pAdapter, 
	IN			u8				efuseType,
	IN			u16				*pAddr,
	IN			PPGPKT_STRUCT	pTargetPkt,
	IN			BOOLEAN			bPseudoTest)
{
	BOOLEAN		bRet=_FALSE;
	u8	pg_header=0, tmp_header=0;
	u16	efuse_addr=*pAddr;
	u8	repeatcnt=0;

	//RTPRINT(FEEPROM, EFUSE_PG, ("Wirte 1byte header\n"));
	pg_header = ((pTargetPkt->offset << 4) & 0xf0) |pTargetPkt->word_en;
	
	efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
	efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);

	while(tmp_header == 0xFF)
	{
		if(repeatcnt++ > EFUSE_REPEAT_THRESHOLD_)
		{
			return _FALSE;
		}
		efuse_OneByteWrite(pAdapter,efuse_addr, pg_header, bPseudoTest);
		efuse_OneByteRead(pAdapter,efuse_addr, &tmp_header, bPseudoTest);
	}
	
	if(pg_header == tmp_header)
	{
		bRet = _TRUE;
	}
	else
	{
		PGPKT_STRUCT	fixPkt;
		//RTPRINT(FEEPROM, EFUSE_PG, ("Error condition for fixed PG packet, need to cover the existed data\n"));
		fixPkt.offset = (tmp_header>>4) & 0x0F;
		fixPkt.word_en = tmp_header & 0x0F;					
		fixPkt.word_cnts = Efuse_CalculateWordCnts(fixPkt.word_en);
		if(!hal_EfuseFixHeaderProcess(pAdapter, efuseType, &fixPkt, &efuse_addr, bPseudoTest))
			return _FALSE;
	}
	
	*pAddr = efuse_addr;
	return bRet;
}

static BOOLEAN
hal_EfusePgPacketWriteData(
	IN			PADAPTER		pAdapter, 
	IN			u8				efuseType,
	IN			u16				*pAddr,
	IN			PPGPKT_STRUCT	pTargetPkt,
	IN			BOOLEAN			bPseudoTest)
{
	BOOLEAN	bRet=_FALSE;
	u16	efuse_addr=*pAddr;
	u8	badworden=0;
	u32	PgWriteSuccess=0;

	badworden = 0x0f;
	badworden = Efuse_WordEnableDataWrite(pAdapter, efuse_addr+1, pTargetPkt->word_en, pTargetPkt->data, bPseudoTest);
	if(badworden == 0x0F)
	{
		// write ok
		//RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfusePgPacketWriteData ok!!\n"));
		return _TRUE;
	}
	else
	{
		//RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfusePgPacketWriteData Fail!!\n"));
		//reorganize other pg packet
		if(efuseType == EFUSE_WIFI)
			PgWriteSuccess = Efuse_PgPacketWrite(pAdapter, pTargetPkt->offset, badworden, pTargetPkt->data, bPseudoTest);
		else
			PgWriteSuccess = hal_EfusePgPacketWrite_BT(pAdapter, pTargetPkt->offset, badworden, pTargetPkt->data, bPseudoTest);
		if(!PgWriteSuccess)
			return _FALSE;
		else
			return _TRUE;
	}

	return bRet;
}

static BOOLEAN
hal_EfusePgPacketWriteHeader(
	IN			PADAPTER		pAdapter, 
	IN			u8				efuseType,
	IN			u16				*pAddr,
	IN			PPGPKT_STRUCT	pTargetPkt,
	IN			BOOLEAN			bPseudoTest)
{
	BOOLEAN		bRet=_FALSE;

	if(pTargetPkt->offset >= EFUSE_MAX_SECTION_BASE)
	{
		bRet = hal_EfusePgPacketWrite2ByteHeader(pAdapter, efuseType, pAddr, pTargetPkt, bPseudoTest);
	}
	else
	{
		bRet = hal_EfusePgPacketWrite1ByteHeader(pAdapter, efuseType, pAddr, pTargetPkt, bPseudoTest);
	}

	return bRet;
}

static BOOLEAN
hal_EfusePgCheckAvailableAddr(
	IN	PADAPTER	pAdapter,
	IN	u8			efuseType,
	IN	BOOLEAN		bPseudoTest
	)
{
	u16	efuse_max_available_len=0;
	
	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (PVOID)&efuse_max_available_len, bPseudoTest);
	//RTPRINT(FEEPROM, EFUSE_PG, ("efuse_max_available_len = %d\n", efuse_max_available_len));

	if(Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest) >= efuse_max_available_len)
	{
		//RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfusePgCheckAvailableAddr error!!\n"));
		return _FALSE;
	}
	return _TRUE;
}

static VOID
hal_EfuseConstructPGPkt(
					IN	u8 				offset,
					IN	u8				word_en,
					IN	u8				*pData,
					IN	PPGPKT_STRUCT	pTargetPkt

)
{
	_rtw_memset((PVOID)pTargetPkt->data, 0xFF, sizeof(u8)*8);
	pTargetPkt->offset = offset;
	pTargetPkt->word_en= word_en;
	efuse_WordEnableDataRead(word_en, pData, pTargetPkt->data);
	pTargetPkt->word_cnts = Efuse_CalculateWordCnts(pTargetPkt->word_en);

	//RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfuseConstructPGPkt(), targetPkt, offset=%d, word_en=0x%x, word_cnts=%d\n", pTargetPkt->offset, pTargetPkt->word_en, pTargetPkt->word_cnts));
}

static BOOLEAN
hal_EfuseCheckIfDatafollowed(
	IN		PADAPTER		pAdapter,
	IN		u8				word_cnts,
	IN		u16				startAddr,
	IN		BOOLEAN			bPseudoTest
	)
{
	BOOLEAN		bRet=_FALSE;
	u8	i, efuse_data;
	
	for(i=0; i<(word_cnts*2) ; i++)
	{
		if(efuse_OneByteRead(pAdapter, (startAddr+i) ,&efuse_data, bPseudoTest)&&(efuse_data != 0xFF))
			bRet = _TRUE;
	}

	return bRet;
}

static BOOLEAN
wordEnMatched(
	IN	PPGPKT_STRUCT	pTargetPkt,
	IN	PPGPKT_STRUCT	pCurPkt,
	IN	u8				*pWden
)
{
	u8	match_word_en = 0x0F;	// default all words are disabled
	u8	i;

	// check if the same words are enabled both target and current PG packet
	if( ((pTargetPkt->word_en & BIT0) == 0) &&
		((pCurPkt->word_en & BIT0) == 0) )
	{
		match_word_en &= ~BIT0;				// enable word 0
	}
	if( ((pTargetPkt->word_en & BIT1) == 0) &&
		((pCurPkt->word_en & BIT1) == 0) )
	{
		match_word_en &= ~BIT1;				// enable word 1
	}
	if( ((pTargetPkt->word_en & BIT2) == 0) &&
		((pCurPkt->word_en & BIT2) == 0) )
	{
		match_word_en &= ~BIT2;				// enable word 2
	}
	if( ((pTargetPkt->word_en & BIT3) == 0) &&
		((pCurPkt->word_en & BIT3) == 0) )
	{
		match_word_en &= ~BIT3;				// enable word 3
	}

	*pWden = match_word_en;

	if(match_word_en != 0xf)
		return _TRUE;
	else
		return _FALSE;
}

static BOOLEAN
hal_EfusePartialWriteCheck(
					IN	PADAPTER		pAdapter,
					IN	u8				efuseType,
					IN	u16				*pAddr,
					IN	PPGPKT_STRUCT	pTargetPkt,
					IN	BOOLEAN			bPseudoTest
					)
{
	BOOLEAN		bRet=_FALSE;
	u8	i, efuse_data=0, cur_header=0;
	u8	new_wden=0, matched_wden=0, badworden=0;
	u16	startAddr=0, efuse_max_available_len=0, efuse_max=0;
	PGPKT_STRUCT	curPkt;

	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_BANK, (PVOID)&efuse_max_available_len, bPseudoTest);
	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_EFUSE_REAL_CONTENT_LEN, (PVOID)&efuse_max, bPseudoTest);

	if(efuseType == EFUSE_WIFI)
	{
		if(bPseudoTest)
		{
			startAddr = (u16)(fakeEfuseUsedBytes%EFUSE_REAL_CONTENT_LEN);
		}
		else
		{
			pAdapter->HalFunc.GetHwRegHandler(pAdapter, HW_VAR_EFUSE_BYTES, (u8 *)&startAddr);
			startAddr%=EFUSE_REAL_CONTENT_LEN;
		}
	}
	else
	{
		if(bPseudoTest)
		{
			startAddr = (u16)(fakeBTEfuseUsedBytes%EFUSE_REAL_CONTENT_LEN);
		}
		else
		{
			startAddr = (u16)(BTEfuseUsedBytes%EFUSE_REAL_CONTENT_LEN);
		}
	}
	//RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfusePartialWriteCheck(), startAddr=%d\n", startAddr));

	while(1)
	{
		if(startAddr >= efuse_max_available_len) 
		{
			bRet = _FALSE;
			break;
		}
		
		if(efuse_OneByteRead(pAdapter, startAddr, &efuse_data, bPseudoTest) && (efuse_data!=0xFF))
		{
			if(EXT_HEADER(efuse_data))
			{
				cur_header = efuse_data;
				startAddr++;
				efuse_OneByteRead(pAdapter, startAddr, &efuse_data, bPseudoTest);
				if(ALL_WORDS_DISABLED(efuse_data))
				{
					//RTPRINT(FEEPROM, EFUSE_PG, ("Error condition, all words disabled"));
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
			if( (curPkt.offset == pTargetPkt->offset) && 
				(!hal_EfuseCheckIfDatafollowed(pAdapter, curPkt.word_cnts, startAddr+1, bPseudoTest)) &&
				wordEnMatched(pTargetPkt, &curPkt, &matched_wden) )
			{
				//RTPRINT(FEEPROM, EFUSE_PG, ("Need to partial write data by the previous wrote header\n"));
				// Here to write partial data
				badworden = Efuse_WordEnableDataWrite(pAdapter, startAddr+1, matched_wden, pTargetPkt->data, bPseudoTest);
				if(badworden != 0x0F)
				{
					u32	PgWriteSuccess=0;
					// if write fail on some words, write these bad words again
					if(efuseType == EFUSE_WIFI)
						PgWriteSuccess = Efuse_PgPacketWrite(pAdapter, pTargetPkt->offset, badworden, pTargetPkt->data, bPseudoTest);
					else
						PgWriteSuccess = hal_EfusePgPacketWrite_BT(pAdapter, pTargetPkt->offset, badworden, pTargetPkt->data, bPseudoTest);

					if(!PgWriteSuccess)
					{
						bRet = _FALSE;	// write fail, return
						break;
					}					
				}
				// partial write ok, update the target packet for later use		
				for(i=0; i<4; i++)
				{
					if((matched_wden & (0x1<<i)) == 0)	// this word has been written
					{
						pTargetPkt->word_en |= (0x1<<i);	// disable the word
					}
				}
				pTargetPkt->word_cnts = Efuse_CalculateWordCnts(pTargetPkt->word_en);
			}
			// read from next header
			startAddr = startAddr + (curPkt.word_cnts*2) +1;	
		}
		else
		{
			// not used header, 0xff
			*pAddr = startAddr;
			//RTPRINT(FEEPROM, EFUSE_PG, ("Started from unused header offset=%d\n", startAddr));
			bRet = _TRUE;
			break;
		}
	}	
	return bRet;
}

static BOOLEAN 
hal_EfusePgPacketWrite_BT(
					IN	PADAPTER	pAdapter, 
					IN	u8 			offset,
					IN	u8			word_en,
					IN	u8			*pData,
					IN	BOOLEAN		bPseudoTest
					)
{
	PGPKT_STRUCT 	targetPkt;
	u16	startAddr=0;
	u8	efuseType=EFUSE_BT;
	
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

static BOOLEAN 
hal_EfusePgPacketWrite_8723(
					IN	PADAPTER		pAdapter, 
					IN	u8 			offset,
					IN	u8			word_en,
					IN	u8			*pData,
					IN	BOOLEAN		bPseudoTest
					)
{
	PGPKT_STRUCT 	targetPkt;
	u16			startAddr=0;
	u8			efuseType=EFUSE_WIFI;
	
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

static int
hal_EfusePgPacketWrite_8192C(IN	PADAPTER	pAdapter, 
					IN	u8 			offset,
					IN	u8			word_en,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	u8	WriteState = PG_STATE_HEADER;		

	int	bContinual = _TRUE,bDataEmpty=_TRUE, bResult = _TRUE;
	u16	efuse_addr = 0;
	u8	efuse_data;

	u8	pg_header = 0;

	u8	tmp_word_cnts=0,target_word_cnts=0;
	u8	tmp_header,match_word_en,tmp_word_en;

	PGPKT_STRUCT target_pkt;	
	PGPKT_STRUCT tmp_pkt;
	
	u8	originaldata[sizeof(u8)*8];
	u8	tmpindex = 0,badworden = 0x0F;

	static int	repeat_times = 0;
	u8	efuseType=EFUSE_WIFI;

	//
	// <Roger_Notes> Efuse has been pre-programmed dummy 5Bytes at the end of Efuse by CP.
	// So we have to prevent unexpected data string connection, which will cause
	// incorrect data auto-load from HW. The total size is equal or smaller than 498bytes
	// (i.e., offset 0~497, and dummy 1bytes) expected after CP test.
	// 2009.02.19.
	//
	if( Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest) >= (EFUSE_REAL_CONTENT_LEN-EFUSE_OOB_PROTECT_BYTES))
	{
		//RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfusePgPacketWrite_8192C(), over size\n"));
		return _FALSE;
	}

	// Init the 8 bytes content as 0xff
	target_pkt.offset = offset;
	target_pkt.word_en= word_en;

	_rtw_memset((PVOID)target_pkt.data, 0xFF, sizeof(u8)*8);
	
	efuse_WordEnableDataRead(word_en,data,target_pkt.data);
	target_word_cnts = Efuse_CalculateWordCnts(target_pkt.word_en);

	//efuse_reg_ctrl(pAdapter,_TRUE);//power on
	//RTPRINT(FEEPROM, EFUSE_PG, ("EFUSE Power ON\n"));

	//
	// <Roger_Notes> Efuse has been pre-programmed dummy 5Bytes at the end of Efuse by CP.
	// So we have to prevent unexpected data string connection, which will cause
	// incorrect data auto-load from HW. Dummy 1bytes is additional.
	// 2009.02.19.
	//
	while( bContinual && (efuse_addr  < (EFUSE_REAL_CONTENT_LEN-EFUSE_OOB_PROTECT_BYTES)) )
	{
		
		if(WriteState==PG_STATE_HEADER)
		{	
			bDataEmpty=_TRUE;
			badworden = 0x0F;		
			//************  so *******************
			//RTPRINT(FEEPROM, EFUSE_PG, ("EFUSE PG_STATE_HEADER\n"));
			if (	efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest) &&
				(efuse_data!=0xFF))
			{ 	
				tmp_header  =  efuse_data;
				
				tmp_pkt.offset 	= (tmp_header>>4) & 0x0F;
				tmp_pkt.word_en 	= tmp_header & 0x0F;					
				tmp_word_cnts =  Efuse_CalculateWordCnts(tmp_pkt.word_en);

				//************  so-1 *******************
				if(tmp_pkt.offset  != target_pkt.offset)
				{
					efuse_addr = efuse_addr + (tmp_word_cnts*2) +1; //Next pg_packet
					#if (EFUSE_ERROE_HANDLE == 1)
					WriteState = PG_STATE_HEADER;
					#endif
				}
				else
				{	
					//************  so-2 *******************
					for(tmpindex=0 ; tmpindex<(tmp_word_cnts*2) ; tmpindex++)
					{
						if(efuse_OneByteRead(pAdapter, (efuse_addr+1+tmpindex) ,&efuse_data, bPseudoTest)&&(efuse_data != 0xFF)){
							bDataEmpty = _FALSE;	
						}
					}	
					//************  so-2-1 *******************
					if(bDataEmpty == _FALSE)
					{						
						efuse_addr = efuse_addr + (tmp_word_cnts*2) +1; //Next pg_packet										
						#if (EFUSE_ERROE_HANDLE == 1)
						WriteState=PG_STATE_HEADER;
						#endif
					}
					else
					{//************  so-2-2 *******************
						match_word_en = 0x0F;
						if(   !( (target_pkt.word_en&BIT0)|(tmp_pkt.word_en&BIT0)  ))
						{
							 match_word_en &= (~BIT0);
						}	
						if(   !( (target_pkt.word_en&BIT1)|(tmp_pkt.word_en&BIT1)  ))
						{
							 match_word_en &= (~BIT1);
						}
						if(   !( (target_pkt.word_en&BIT2)|(tmp_pkt.word_en&BIT2)  ))
						{
							 match_word_en &= (~BIT2);
						}
						if(   !( (target_pkt.word_en&BIT3)|(tmp_pkt.word_en&BIT3)  ))
						{
							 match_word_en &= (~BIT3);
						}					
												
						//************  so-2-2-A *******************
						if((match_word_en&0x0F)!=0x0F)
						{							
							badworden = Efuse_WordEnableDataWrite(pAdapter,efuse_addr+1, tmp_pkt.word_en ,target_pkt.data, bPseudoTest);
							
							//************  so-2-2-A-1 *******************
							//############################
							if(0x0F != (badworden&0x0F))
							{														
								u8	reorg_offset = offset;
								u8	reorg_worden=badworden;								
								Efuse_PgPacketWrite(pAdapter,reorg_offset,reorg_worden,originaldata, bPseudoTest);
							}	
							//############################						

							tmp_word_en = 0x0F;	
							if(  (target_pkt.word_en&BIT0)^(match_word_en&BIT0)  )
							{
								tmp_word_en &= (~BIT0);
							}
							if(   (target_pkt.word_en&BIT1)^(match_word_en&BIT1) )
							{
								tmp_word_en &=  (~BIT1);
							}
							if(   (target_pkt.word_en&BIT2)^(match_word_en&BIT2) )
							{
								tmp_word_en &= (~BIT2);
							}						
							if(   (target_pkt.word_en&BIT3)^(match_word_en&BIT3) )
							{
								tmp_word_en &=(~BIT3);
							}							
						
							//************  so-2-2-A-2 *******************	
							if((tmp_word_en&0x0F)!=0x0F){
								//reorganize other pg packet						
								//efuse_addr = efuse_addr + (2*tmp_word_cnts) +1;//next pg packet addr							
								efuse_addr = Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest);
								//===========================
								target_pkt.offset = offset;
								target_pkt.word_en= tmp_word_en;					
								//===========================
							}else{								
								bContinual = _FALSE;
							}
							#if (EFUSE_ERROE_HANDLE == 1)
							WriteState=PG_STATE_HEADER;
							repeat_times++;
							if(repeat_times>EFUSE_REPEAT_THRESHOLD_){
								bContinual = _FALSE;
								bResult = _FALSE;
							}
							#endif
						}
						else{//************  so-2-2-B *******************
							//reorganize other pg packet						
							efuse_addr = efuse_addr + (2*tmp_word_cnts) +1;//next pg packet addr							
							//===========================
							target_pkt.offset = offset;
							target_pkt.word_en= target_pkt.word_en;					
							//===========================			
							#if (EFUSE_ERROE_HANDLE == 1)
							WriteState=PG_STATE_HEADER;
							#endif
						}		
					}				
				}				
				//RTPRINT(FEEPROM, EFUSE_PG, ("EFUSE PG_STATE_HEADER-1\n"));
			}
			else		//************  s1: header == oxff  *******************
			{
				pg_header = ((target_pkt.offset << 4)&0xf0) |target_pkt.word_en;

				efuse_OneByteWrite(pAdapter,efuse_addr, pg_header, bPseudoTest);
				efuse_OneByteRead(pAdapter,efuse_addr, &tmp_header, bPseudoTest);
		
				if(tmp_header == pg_header)
				{ //************  s1-1*******************								
					WriteState = PG_STATE_DATA;						
				}				
				#if (EFUSE_ERROE_HANDLE == 1)
				else if(tmp_header == 0xFF){//************  s1-3: if Write or read func doesn't work *******************		
					//efuse_addr doesn't change
					WriteState = PG_STATE_HEADER;					
					repeat_times++;
					if(repeat_times>EFUSE_REPEAT_THRESHOLD_){
						bContinual = _FALSE;
						bResult = _FALSE;
					}
				}
				#endif
				else
				{//************  s1-2 : fixed the header procedure *******************							
					tmp_pkt.offset = (tmp_header>>4) & 0x0F;
					tmp_pkt.word_en=  tmp_header & 0x0F;					
					tmp_word_cnts =  Efuse_CalculateWordCnts(tmp_pkt.word_en);
																											
					//************  s1-2-A :cover the exist data *******************
					//memset(originaldata,0xff,sizeof(UINT8)*8);
					_rtw_memset((PVOID)originaldata, 0xff, sizeof(u8)*8);
					
					if(Efuse_PgPacketRead( pAdapter, tmp_pkt.offset,originaldata, bPseudoTest))
					{	//check if data exist 					
						//efuse_reg_ctrl(pAdapter,_TRUE);//power on
						badworden = Efuse_WordEnableDataWrite(pAdapter,efuse_addr+1,tmp_pkt.word_en,originaldata, bPseudoTest);	
						//############################
						if(0x0F != (badworden&0x0F))
						{														
							u8	reorg_offset = tmp_pkt.offset;
							u8	reorg_worden=badworden;								
							Efuse_PgPacketWrite(pAdapter,reorg_offset,reorg_worden,originaldata, bPseudoTest);
							efuse_addr = Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest);	 
						}
						//############################
						else{
							efuse_addr = efuse_addr + (tmp_word_cnts*2) +1; //Next pg_packet							
						}
					}
					 //************  s1-2-B: wrong address*******************
					else
					{
						efuse_addr = efuse_addr + (tmp_word_cnts*2) +1; //Next pg_packet
					}

					#if (EFUSE_ERROE_HANDLE == 1)
					WriteState=PG_STATE_HEADER;	
					repeat_times++;
					if(repeat_times>EFUSE_REPEAT_THRESHOLD_){
						bContinual = _FALSE;
						bResult = _FALSE;
					}
					#endif
					
					//RTPRINT(FEEPROM, EFUSE_PG, ("EFUSE PG_STATE_HEADER-2\n"));
				}

			}

		}
		//write data state
		else if(WriteState==PG_STATE_DATA) 
		{	//************  s1-1  *******************
			//RTPRINT(FEEPROM, EFUSE_PG, ("EFUSE PG_STATE_DATA\n"));
			badworden = 0x0f;
			badworden = Efuse_WordEnableDataWrite(pAdapter,efuse_addr+1,target_pkt.word_en,target_pkt.data, bPseudoTest);	
			if((badworden&0x0F)==0x0F)
			{ //************  s1-1-A *******************
				bContinual = _FALSE;
			}
			else
			{//reorganize other pg packet //************  s1-1-B *******************
				efuse_addr = efuse_addr + (2*target_word_cnts) +1;//next pg packet addr
										
				//===========================
				target_pkt.offset = offset;
				target_pkt.word_en= badworden;		
				target_word_cnts =  Efuse_CalculateWordCnts(target_pkt.word_en); 
				//===========================			
				#if (EFUSE_ERROE_HANDLE == 1)
				WriteState=PG_STATE_HEADER;	
				repeat_times++;
				if(repeat_times>EFUSE_REPEAT_THRESHOLD_){
					bContinual = _FALSE;
					bResult = _FALSE;
				}
				#endif
				//RTPRINT(FEEPROM, EFUSE_PG, ("EFUSE PG_STATE_HEADER-3\n"));
			}
		}
	}

	if(efuse_addr  >= (EFUSE_REAL_CONTENT_LEN-EFUSE_OOB_PROTECT_BYTES))
	{
		//RT_TRACE(COMP_EFUSE, DBG_LOUD, ("hal_EfusePgPacketWrite_8192C(): efuse_addr(%#x) Out of size!!\n", efuse_addr));
	}
	//efuse_reg_ctrl(pAdapter,_FALSE);//power off
	
	return _TRUE;
}

static int 
Hal_EfusePgPacketWrite_Pseudo(IN	PADAPTER	pAdapter, 
					IN	u8 			offset,
					IN	u8			word_en,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	int ret;
	
	ret = hal_EfusePgPacketWrite_8723(pAdapter, offset, word_en, data, bPseudoTest);

	return ret;
}

static int
Hal_EfusePgPacketWrite(IN	PADAPTER	pAdapter, 
					IN	u8 			offset,
					IN	u8			word_en,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	int	ret=0;

	if(IS_HARDWARE_TYPE_8192C(pAdapter))
	{
		ret = hal_EfusePgPacketWrite_8192C(pAdapter, offset, word_en, data, bPseudoTest);
	}
	else if(IS_HARDWARE_TYPE_8723(pAdapter))
	{
		ret = hal_EfusePgPacketWrite_8723(pAdapter, offset, word_en, data, bPseudoTest);
	}

	return ret;
}

static int 
rtl8192c_Efuse_PgPacketWrite(IN	PADAPTER	pAdapter, 
					IN	u8 			offset,
					IN	u8			word_en,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	int	ret;

	if(bPseudoTest)
	{
		ret = Hal_EfusePgPacketWrite_Pseudo(pAdapter, offset, word_en, data, bPseudoTest);
	}
	else
	{
		ret = Hal_EfusePgPacketWrite(pAdapter, offset, word_en, data, bPseudoTest);
	}
	return ret;
}

VOID
rtl8192c_EfuseParseIDCode(
	IN	PADAPTER	pAdapter,
	IN	u8			*hwinfo
	)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(pAdapter);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u16			i,EEPROMId;
	
	// Checl 0x8129 again for making sure autoload status!!
	EEPROMId = *((u16 *)&hwinfo[0]);
	if( le16_to_cpu(EEPROMId) != RTL_EEPROM_ID)
	{
		DBG_8192C("EEPROM ID(%#x) is invalid!!\n", EEPROMId);
		pEEPROM->bautoload_fail_flag = _TRUE;
	}
	else
	{
		pEEPROM->bautoload_fail_flag = _FALSE;
	}

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("EEPROM ID = 0x%4x\n", EEPROMId));
}

void rtl8192c_read_chip_version(PADAPTER	pAdapter)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);
	pHalData->VersionID = rtl8192c_ReadChipVersion(pAdapter);
}
	
void rtl8192c_set_hal_ops(struct hal_ops *pHalFunc)
{
	pHalFunc->free_hal_data = &rtl8192c_free_hal_data;

	pHalFunc->dm_init = &rtl8192c_init_dm_priv;
	pHalFunc->dm_deinit = &rtl8192c_deinit_dm_priv;
	pHalFunc->read_chip_version = &rtl8192c_read_chip_version;

	pHalFunc->set_bwmode_handler = &PHY_SetBWMode8192C;
	pHalFunc->set_channel_handler = &PHY_SwChnl8192C;

	pHalFunc->hal_dm_watchdog = &rtl8192c_HalDmWatchDog;

	pHalFunc->Add_RateATid = &rtl8192c_Add_RateATid;

#ifdef CONFIG_ANTENNA_DIVERSITY
	pHalFunc->SwAntDivBeforeLinkHandler = &SwAntDivBeforeLink8192C;
	pHalFunc->SwAntDivCompareHandler = &SwAntDivCompare8192C;
#endif

	pHalFunc->read_bbreg = &rtl8192c_PHY_QueryBBReg;
	pHalFunc->write_bbreg = &rtl8192c_PHY_SetBBReg;
	pHalFunc->read_rfreg = &rtl8192c_PHY_QueryRFReg;
	pHalFunc->write_rfreg = &rtl8192c_PHY_SetRFReg;	
	
	//Efuse related function
	pHalFunc->EfusePowerSwitch = &rtl8192c_EfusePowerSwitch;
	pHalFunc->ReadEFuse = &rtl8192c_ReadEFuse;
	pHalFunc->EFUSEGetEfuseDefinition = &rtl8192c_EFUSE_GetEfuseDefinition;
	pHalFunc->EfuseGetCurrentSize = &rtl8192c_EfuseGetCurrentSize;
	pHalFunc->Efuse_PgPacketRead = &rtl8192c_Efuse_PgPacketRead;
	pHalFunc->Efuse_PgPacketWrite = &rtl8192c_Efuse_PgPacketWrite;
	pHalFunc->Efuse_WordEnableDataWrite = &rtl8192c_Efuse_WordEnableDataWrite;

#ifdef DBG_CONFIG_ERROR_DETECT
	pHalFunc->sreset_init_value = &rtl8192c_sreset_init_value;
	pHalFunc->sreset_reset_value = &rtl8192c_sreset_reset_value;	
	pHalFunc->silentreset = &rtl8192c_silentreset_for_specific_platform;
	pHalFunc->sreset_xmit_status_check = &rtl8192c_sreset_xmit_status_check;
	pHalFunc->sreset_linked_status_check  = &rtl8192c_sreset_linked_status_check;
	pHalFunc->sreset_get_wifi_status  = &rtl8192c_sreset_get_wifi_status;
#endif

#ifdef CONFIG_IOL
	pHalFunc->IOL_exec_cmds_sync = &rtl8192c_IOL_exec_cmds_sync;
#endif
}

