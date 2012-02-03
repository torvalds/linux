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

#define _RTL8192D_HAL_INIT_C_
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_byteorder.h>
#include <rtw_efuse.h>

#include <hal_init.h>

#ifdef CONFIG_USB_HCI
#include <usb_hal.h>
#endif

#include <rtl8192d_hal.h>

atomic_t GlobalMutexForGlobalAdapterList = ATOMIC_INIT(0);
atomic_t GlobalMutexForMac0_2G_Mac1_5G = ATOMIC_INIT(0);
atomic_t GlobalMutexForPowerAndEfuse = ATOMIC_INIT(0);
atomic_t GlobalMutexForPowerOnAndPowerOff = ATOMIC_INIT(0);
atomic_t GlobalMutexForFwDownload = ATOMIC_INIT(0);
#ifdef CONFIG_DUALMAC_CONCURRENT
atomic_t GlobalCounterForMutex = ATOMIC_INIT(0);
BOOLEAN GlobalFirstConfigurationForNormalChip = _TRUE;
#endif


static BOOLEAN
_IsFWDownloaded(
	IN	PADAPTER			Adapter
	)
{
	return ((rtw_read32(Adapter, REG_MCUFWDL) & MCUFWDL_RDY) ? _TRUE : _FALSE);
}

static VOID
_FWDownloadEnable(
	IN	PADAPTER		Adapter,
	IN	BOOLEAN			enable
	)
{
#ifdef CONFIG_USB_HCI
	u32	value32 = rtw_read32(Adapter, REG_MCUFWDL);

	if(enable){
		value32 |= MCUFWDL_EN;
	}
	else{
		value32 &= ~MCUFWDL_EN;
	}

	rtw_write32(Adapter, REG_MCUFWDL, value32);

#else
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

		// Reserved for fw extension.   0x81[7] is used for mac0 status ,so don't write this reg here		 
		//rtw_write8(Adapter, REG_MCUFWDL+1, 0x00);
	}
#endif
}

#ifdef CONFIG_USB_HCI
static VOID
_BlockWrite_92d(
	IN		PADAPTER		Adapter,
	IN		PVOID			buffer,
	IN		u32				size
	)
{
	u32			blockSize8 = sizeof(u64);	
	u32			blocksize4 = sizeof(u32);
	u32			blockSize = 64;
	u8*			bufferPtr = (u8*)buffer;
	u32*		pu4BytePtr = (u32*)buffer;
	u32			i, offset, blockCount, remainSize,remain8,remain4,blockCount8,blockCount4;

	blockCount = size / blockSize;
	remain8 = size % blockSize;
	for(i = 0 ; i < blockCount ; i++){
		offset = i * blockSize;
		rtw_writeN(Adapter, (FW_8192D_START_ADDRESS + offset), 64,(bufferPtr + offset));
	}


	if(remain8){
		offset = blockCount * blockSize;

		blockCount8=remain8/blockSize8;
		remain4=remain8%blockSize8;
		//RT_TRACE(COMP_INIT,DBG_LOUD,("remain4 size %x blockcount %x blockCount8 %x\n",remain4,blockCount,blockCount8));		 
		for(i = 0 ; i < blockCount8 ; i++){	
			rtw_writeN(Adapter, (FW_8192D_START_ADDRESS + offset+i*blockSize8), 8,(bufferPtr + offset+i*blockSize8));
		}

		if(remain4){
			offset=blockCount * blockSize+blockCount8*blockSize8;		
			blockCount4=remain4/blocksize4;
			remainSize=remain8%blocksize4;
			 
			for(i = 0 ; i < blockCount4 ; i++){				
				rtw_write32(Adapter, (FW_8192D_START_ADDRESS + offset+i*blocksize4), cpu_to_le32(*(pu4BytePtr+ offset/4+i)));		
			}	
			
			if(remainSize){
				offset=blockCount * blockSize+blockCount8*blockSize8+blockCount4*blocksize4;
				for(i = 0 ; i < remainSize ; i++){
					rtw_write8(Adapter, (FW_8192D_START_ADDRESS + offset + i), *(bufferPtr +offset+ i));
				}	
			}
			
		}
		
	}
	
}
#endif
#ifndef CONFIG_USB_HCI
static VOID
_BlockWrite(
	IN		PADAPTER		Adapter,
	IN		PVOID			buffer,
	IN		u32			size
	)
{
	u32			blockSize	= sizeof(u32);	// Use 4-byte write to download FW
	u8*			bufferPtr	= (u8*)buffer;
	u32*			pu4BytePtr	= (u32*)buffer;
	u32			i, offset, blockCount, remainSize;

	blockCount = size / blockSize;
	remainSize = size % blockSize;

	for(i = 0 ; i < blockCount ; i++){
		offset = i * blockSize;
		rtw_write32(Adapter, (FW_8192D_START_ADDRESS + offset), cpu_to_le32(*(pu4BytePtr + i)));		
	}

	if(remainSize){
		offset = blockCount * blockSize;
		bufferPtr += offset;
		
		for(i = 0 ; i < remainSize ; i++){
			rtw_write8(Adapter, (FW_8192D_START_ADDRESS + offset + i), *(bufferPtr + i));
		}
	}

}
#endif //CONFIG_USB_HCI
static VOID
_PageWrite(
	IN		PADAPTER		Adapter,
	IN		u32			page,
	IN		PVOID			buffer,
	IN		u32			size
	)
{
	u8 value8;
	u8 u8Page = (u8) (page & 0x07) ;

	value8 = (rtw_read8(Adapter, REG_MCUFWDL+2)& 0xF8 ) | u8Page ;
	rtw_write8(Adapter, REG_MCUFWDL+2,value8);
#ifdef CONFIG_USB_HCI
	_BlockWrite_92d(Adapter,buffer,size);
#else
	_BlockWrite(Adapter,buffer,size);
#endif
}
#ifdef CONFIG_PCI_HCI
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
#endif //CONFIG_PCI_HCI
static VOID
_WriteFW(
	IN		PADAPTER		Adapter,
	IN		PVOID			buffer,
	IN		u32			size
	)
{
	// Since we need dynamic decide method of dwonload fw, so we call this function to get chip version.
	// We can remove _ReadChipVersion from ReadAdapterInfo8192C later.
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
		_PageWrite(Adapter,page, (bufferPtr+offset),MAX_PAGE_SIZE);			
	}
	if(remainSize){
		offset = pageNums *MAX_PAGE_SIZE;
		page = pageNums;
		_PageWrite(Adapter,page, (bufferPtr+offset),remainSize);
	}	
	DBG_8192C("_WriteFW Done- for Normal chip.\n");

}
int _FWFreeToGo_92D(
	IN		PADAPTER		Adapter
	);
int _FWFreeToGo_92D(
	IN		PADAPTER		Adapter
	)
{
	u32			counter = 0;
	u32			value32;
	// polling CheckSum report
	do{
		value32 = rtw_read32(Adapter, REG_MCUFWDL);
	}while((counter ++ < POLLING_READY_TIMEOUT_COUNT) && (!(value32 & FWDL_ChkSum_rpt  )));	

	if(counter >= POLLING_READY_TIMEOUT_COUNT){	
		DBG_8192C("chksum report faill ! REG_MCUFWDL:0x%08x .\n",value32);
		return _FAIL;
	}
	DBG_8192C("Checksum report OK ! REG_MCUFWDL:0x%08x .\n",value32);

	value32 = rtw_read32(Adapter, REG_MCUFWDL);
	value32 |= MCUFWDL_RDY;
	rtw_write32(Adapter, REG_MCUFWDL, value32);
	return _SUCCESS;
	
}

VOID
rtl8192d_FirmwareSelfReset(
	IN	PADAPTER		Adapter
)
{
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	u1bTmp;
	u8	Delay = 100;
		
	//if((pHalData->FirmwareVersion > 0x21) ||
	//	(pHalData->FirmwareVersion == 0x21 &&
	//	pHalData->FirmwareSubVersion >= 0x01))
	{
		rtw_write8(Adapter, REG_FSIMR, 0x00);
		// 2010/08/25 MH Accordign to RD alfred's suggestion, we need to disable other
		// HRCV INT to influence 8051 reset.
		rtw_write8(Adapter, REG_FWIMR, 0x20);
		// 2011/02/15 MH According to Alex's suggestion, close mask to prevent incorrect FW write operation.
		rtw_write8(Adapter, REG_FTIMR, 0x00);
	
		//0x1cf=0x20. Inform 8051 to reset. 2009.12.25. tynli_test
		rtw_write8(Adapter, REG_HMETFR+3, 0x20);
	
		u1bTmp = rtw_read8(Adapter, REG_SYS_FUNC_EN+1);
		while(u1bTmp&BIT2)
		{
			Delay--;
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("PowerOffAdapter8192CE(): polling 0x03[2] Delay = %d \n", Delay));
			if(Delay == 0)
				break;
			rtw_udelay_os(50);
			u1bTmp = rtw_read8(Adapter, REG_SYS_FUNC_EN+1);
		}
	
		if((u1bTmp&BIT2) && (Delay == 0))
		{
			//DbgPrint("FirmwareDownload92C(): Fail!!!!!! 0x03 = %x\n", u1bTmp);
			rtw_write8(Adapter, REG_FWIMR, 0x00);
		}
	}
}

//
// description :polling fw ready
//
int _FWInit(
	IN PADAPTER			  Adapter     
	);
int _FWInit(
	IN PADAPTER			  Adapter     
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32			counter = 0;


	DBG_8192C("FW already have download ; \n");

	// polling for FW ready
	counter = 0;
	do
	{
		if(pHalData->interfaceIndex==0){
			if(rtw_read8(Adapter, FW_MAC0_ready) & mac0_ready){
				DBG_8192C("Polling FW ready success!! REG_MCUFWDL:0x%x .\n",rtw_read8(Adapter, FW_MAC0_ready));
				return _SUCCESS;
			}
			rtw_udelay_os(5);
		}
		else{
			if(rtw_read8(Adapter, FW_MAC1_ready) &mac1_ready){
				DBG_8192C("Polling FW ready success!! REG_MCUFWDL:0x%x .\n",rtw_read8(Adapter, FW_MAC1_ready));
				return _SUCCESS;
			}
			rtw_udelay_os(5);					
		}

	}while(counter++ < POLLING_READY_TIMEOUT_COUNT);

	if(pHalData->interfaceIndex==0){
		DBG_8192C("Polling FW ready fail!! MAC0 FW init not ready:0x%x .\n",rtw_read8(Adapter, FW_MAC0_ready) );
	}
	else{
		DBG_8192C("Polling FW ready fail!! MAC1 FW init not ready:0x%x .\n",rtw_read8(Adapter, FW_MAC1_ready) );
	}
	
	DBG_8192C("Polling FW ready fail!! REG_MCUFWDL:0x%x .\n",rtw_read32(Adapter, REG_MCUFWDL));
	return _FAIL;

}

#ifdef CONFIG_FILE_FWIMG
extern char *rtw_fw_file_path;
u8	FwBuffer8192D[FW_8192D_SIZE];
#endif //CONFIG_FILE_FWIMG
//
//	Description:
//		Download 8192C firmware code.
//
//
int FirmwareDownload92D(
	IN	PADAPTER			Adapter
)
{	
	int	rtStatus = _SUCCESS;	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	s8 				R92DFwImageFileName[] ={RTL8192D_FW_IMG};
	u8*				FwImage;
	u32				FwImageLen;
	char*			pFwImageFileName;
	PRT_FIRMWARE_92D	pFirmware = NULL;
	PRT_8192D_FIRMWARE_HDR		pFwHdr = NULL;
	u8		*pFirmwareBuf;
	u32		FirmwareLen;
	u8		value;
	u32		count;
	BOOLEAN	 bFwDownloaded = _FALSE,bFwDownloadInProcess = _FALSE;

	if(Adapter->bSurpriseRemoved){
		return _FAIL;
	}

	pFirmware = (PRT_FIRMWARE_92D)rtw_zvmalloc(sizeof(RT_FIRMWARE_92D));
	if(!pFirmware)
	{
		rtStatus = _FAIL;
		goto Exit;
	}

	pFwImageFileName = R92DFwImageFileName;
	FwImage = (u8 *)Rtl8192D_FwImageArray;
	FwImageLen = Rtl8192D_FwImageArrayLength;

	DBG_8192C(" ===> FirmwareDownload92D() fw:Rtl8192D_FwImageArray\n");

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
			rtStatus = rtw_retrive_from_file(rtw_fw_file_path, FwBuffer8192D, FW_8192D_SIZE);
			
			pFirmware->ulFwLength = rtStatus>=0?rtStatus:0;
			pFirmware->szFwBuffer = FwBuffer8192D;
			#endif //CONFIG_FILE_FWIMG
			
			if(pFirmware->ulFwLength <= 0)
			{
				rtStatus = _FAIL;
				goto Exit;
			}
			break;
		case FW_SOURCE_HEADER_FILE:
#if 0
			if(ImgArrayLength > FW_8192C_SIZE){
				rtStatus = _FAIL;
				//RT_TRACE(COMP_INIT, DBG_SERIOUS, ("Firmware size exceed 0x%X. Check it.\n", FW_8192C_SIZE) );
				goto Exit;
			}
#endif
			pFirmware->szFwBuffer = FwImage;
			pFirmware->ulFwLength = FwImageLen;
			break;
	}

	#ifdef DBG_FW_STORE_FILE_PATH //used to store firmware to file...
	if(pFirmware->ulFwLength > 0)
	{
		rtw_store_to_file(DBG_FW_STORE_FILE_PATH, pFirmware->szFwBuffer, pFirmware->ulFwLength);
	}
	#endif

	pFirmwareBuf = pFirmware->szFwBuffer;
	FirmwareLen = pFirmware->ulFwLength;

	// To Check Fw header. Added by tynli. 2009.12.04.
	pFwHdr = (PRT_8192D_FIRMWARE_HDR)pFirmware->szFwBuffer;

	pHalData->FirmwareVersion =  le16_to_cpu(pFwHdr->Version); 
	pHalData->FirmwareSubVersion = le16_to_cpu(pFwHdr->Subversion); 

	DBG_8192C(" FirmwareVersion(%#x), Signature(%#x)\n", pHalData->FirmwareVersion, le16_to_cpu(pFwHdr->Signature));

	if(IS_FW_HEADER_EXIST(pFwHdr))
	{
		//DBG_8192C("Shift 32 bytes for FW header!!\n");
		pFirmwareBuf = pFirmwareBuf + 32;
		FirmwareLen = FirmwareLen -32;
	}

  	ACQUIRE_GLOBAL_MUTEX(GlobalMutexForFwDownload);
	bFwDownloaded = _IsFWDownloaded(Adapter);
	if((rtw_read8(Adapter, 0x1f)&BIT5) == BIT5)
		bFwDownloadInProcess = _TRUE;
	else
		bFwDownloadInProcess = _FALSE;

	if(bFwDownloaded)
	{
		RELEASE_GLOBAL_MUTEX(GlobalMutexForFwDownload);
		goto Exit;
	}
	else if(bFwDownloadInProcess)
	{
		RELEASE_GLOBAL_MUTEX(GlobalMutexForFwDownload);
		for(count=0;count<5000;count++)
		{
			rtw_udelay_os(500);
			ACQUIRE_GLOBAL_MUTEX(GlobalMutexForFwDownload);
			bFwDownloaded = _IsFWDownloaded(Adapter);
			if((rtw_read8(Adapter, 0x1f)&BIT5) == BIT5)
				bFwDownloadInProcess = _TRUE;
			else
				bFwDownloadInProcess = _FALSE;
			RELEASE_GLOBAL_MUTEX(GlobalMutexForFwDownload);
			if(bFwDownloaded)
				goto Exit;
			else if(!bFwDownloadInProcess)
				break;
			else
				DBG_8192C("Wait for another mac download fw \n");
		}
		ACQUIRE_GLOBAL_MUTEX(GlobalMutexForFwDownload);
		value=rtw_read8(Adapter, 0x1f);
		value|=BIT5;
		rtw_write8(Adapter, 0x1f,value);
		RELEASE_GLOBAL_MUTEX(GlobalMutexForFwDownload);
	}
	else
	{
		value=rtw_read8(Adapter, 0x1f);
		value|=BIT5;
		rtw_write8(Adapter, 0x1f,value);
		RELEASE_GLOBAL_MUTEX(GlobalMutexForFwDownload);
	}

	// Suggested by Filen. If 8051 is running in RAM code, driver should inform Fw to reset by itself,
	// or it will cause download Fw fail. 2010.02.01. by tynli.
	if(rtw_read8(Adapter, REG_MCUFWDL)&BIT7) //8051 RAM code
	{	
		rtl8192d_FirmwareSelfReset(Adapter);
		rtw_write8(Adapter, REG_MCUFWDL, 0x00);		
	}

	_FWDownloadEnable(Adapter, _TRUE);
	_WriteFW(Adapter, pFirmwareBuf, FirmwareLen);
	_FWDownloadEnable(Adapter, _FALSE);

	ACQUIRE_GLOBAL_MUTEX(GlobalMutexForFwDownload);
	rtStatus=_FWFreeToGo_92D(Adapter);	
	// download fw over,clear 0x1f[5]
	value=rtw_read8(Adapter, 0x1f);
	value&=(~BIT5);
	rtw_write8(Adapter, 0x1f,value);		
	RELEASE_GLOBAL_MUTEX(GlobalMutexForFwDownload);

	if(_SUCCESS != rtStatus){
		DBG_8192C("Firmware is not ready to run!\n");
		goto Exit;
	}

Exit:

	rtStatus =_FWInit(Adapter);

	if(pFirmware) {
		rtw_vmfree((u8*)pFirmware, sizeof(RT_FIRMWARE_92D));
	}

	//RT_TRACE(COMP_INIT, DBG_LOUD, (" <=== FirmwareDownload91C()\n"));
	return rtStatus;

}

//"chnl" begins from 0. It's not a real channel.
//"channel_info[chnl]" is a real channel.
static u8 Hal_GetChnlGroupfromArray(u8 chnl)
{
	u8	group=0;
	u8	channel_info[59] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,100,102,104,106,108,110,112,114,116,118,120,122,124,126,128,130,132,134,136,138,140,149,151,153,155,157,159,161,163,165};

	if (channel_info[chnl] <= 3)			// Chanel 1-3
		group = 0;
	else if (channel_info[chnl] <= 9)		// Channel 4-9
		group = 1;
	else	if(channel_info[chnl] <=14)				// Channel 10-14
		group = 2;
	// For TX_POWER_FOR_5G_BAND 
	else if(channel_info[chnl] <= 44)
		group = 3;
	else if(channel_info[chnl] <= 54)
		group = 4;
	else if(channel_info[chnl] <= 64)
		group = 5;
	else if(channel_info[chnl] <= 112)
		group = 6;
	else if(channel_info[chnl] <= 126)
		group = 7;
	else if(channel_info[chnl] <= 140)
		group = 8;
	else if(channel_info[chnl] <= 153)
		group = 9;
	else if(channel_info[chnl] <= 159)
		group = 10;
	else 
		group = 11;
	
	return group;
}

VOID
rtl8192d_ReadChipVersion(
	IN PADAPTER			Adapter
	)
{
	u32	value32;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	VERSION_8192D	ChipVersion = VERSION_TEST_CHIP_88C;

	value32 = rtw_read32(Adapter, REG_SYS_CFG);
	DBG_871X("ReadChipVersion8192D 0xF0 = 0x%x \n", value32);

	ChipVersion = (VERSION_8192D)(VERSION_NORMAL_CHIP_92D_SINGLEPHY | CHIP_92D);

	//Decide TestChip or NormalChip here.
	//92D's RF_type will be decided when the reg0x2c is filled.
	if (!(value32 & 0x000f0000))
	{ //Test or Normal Chip:  hardward id 0xf0[19:16] =0 test chip
		ChipVersion = VERSION_TEST_CHIP_92D_SINGLEPHY;
		DBG_871X("TEST CHIP!!!\n");
	}
	else
	{
		ChipVersion = (VERSION_8192D)( ChipVersion | NORMAL_CHIP);
		DBG_871X("Normal CHIP!!!\n");
	}

	pHalData->VersionID = ChipVersion;

}

//-------------------------------------------------------------------------
//
//	Channel Plan
//
//-------------------------------------------------------------------------

static RT_CHANNEL_DOMAIN
_HalMapChannelPlan8192D(
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
		case 0x7F: //Realtek Reserve
			rtChannelDomain = RT_CHANNEL_DOMAIN_FCC_NO_DFS;
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("_HalMapChannelPlan8192D():Force ChannelPlan =0x%X \n" ,0));
			break;
		default:
			if(HalChannelPlan == 0xFF)
				rtChannelDomain = RT_CHANNEL_DOMAIN_FCC_NO_DFS;
			else
				rtChannelDomain = (RT_CHANNEL_DOMAIN)HalChannelPlan;
			break;
	}
	
	return 	rtChannelDomain;

}

VOID
rtl8192d_ReadChannelPlan(
	IN	PADAPTER 		Adapter,
	IN	u8*			PROMContent,
	IN	BOOLEAN			AutoLoadFail
	)
{
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	u8			channelPlan;

	if(AutoLoadFail){
		channelPlan = CHPL_FCC;
	}
	else{
		channelPlan = PROMContent[EEPROM_CHANNEL_PLAN];
	}

	if((pregistrypriv->channel_plan>= RT_CHANNEL_DOMAIN_MAX) || (channelPlan & EEPROM_CHANNEL_PLAN_BY_HW_MASK))
	{
		pmlmepriv->ChannelPlan = _HalMapChannelPlan8192D(Adapter, (channelPlan & (~(EEPROM_CHANNEL_PLAN_BY_HW_MASK))));
		//pMgntInfo->bChnlPlanFromHW = (channelPlan & EEPROM_CHANNEL_PLAN_BY_HW_MASK) ? _TRUE : _FALSE; // User cannot change  channel plan.
	}
	else
	{
		pmlmepriv->ChannelPlan = (RT_CHANNEL_DOMAIN)pregistrypriv->channel_plan;
	}

#if 0 //todo:
	switch(pMgntInfo->ChannelPlan)
	{
		case RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN:
		{
			PRT_DOT11D_INFO	pDot11dInfo = GET_DOT11D_INFO(pMgntInfo);

			pDot11dInfo->bEnabled = TRUE;

			RT_TRACE(COMP_INIT, DBG_LOUD, ("Enable dot11d when RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN!\n"));
			break;
		}
		
		default: //for MacOSX warning message
			break;
	}
#endif

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("RegChannelPlan(%d) EEPROMChannelPlan(%ld)", pMgntInfo->RegChannelPlan, (u4Byte)channelPlan));
	MSG_8192C("ChannelPlan = %d\n" , pmlmepriv->ChannelPlan);

}

//-------------------------------------------------------------------------
//
//	EEPROM Power index mapping
//
//-------------------------------------------------------------------------

static VOID
hal_ReadPowerValueFromPROM92D(
	IN	PADAPTER 		Adapter,
	IN	PTxPowerInfo	pwrInfo,
	IN	u8*			PROMContent,
	IN	BOOLEAN			AutoLoadFail
	)
{
	u32	rfPath, eeAddr, group, offset1,offset2=0;
	u8	i = 0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	_rtw_memset(pwrInfo, 0, sizeof(TxPowerInfo));

	if(AutoLoadFail){		
		for(group = 0 ; group < CHANNEL_GROUP_MAX ; group++){
			for(rfPath = 0 ; rfPath < RF_PATH_MAX ; rfPath++){
				if(group< CHANNEL_GROUP_MAX_2G)
				{
					pwrInfo->CCKIndex[rfPath][group]		= EEPROM_Default_TxPowerLevel_2G;	
					pwrInfo->HT40_1SIndex[rfPath][group]		= EEPROM_Default_TxPowerLevel_2G;
				}	
				else
				{
					pwrInfo->HT40_1SIndex[rfPath][group]		= EEPROM_Default_TxPowerLevel_5G;
				}
				pwrInfo->HT40_2SIndexDiff[rfPath][group]	= EEPROM_Default_HT40_2SDiff;
				pwrInfo->HT20IndexDiff[rfPath][group]		= EEPROM_Default_HT20_Diff;
				pwrInfo->OFDMIndexDiff[rfPath][group]	= EEPROM_Default_LegacyHTTxPowerDiff;
				pwrInfo->HT40MaxOffset[rfPath][group]	= EEPROM_Default_HT40_PwrMaxOffset;		
				pwrInfo->HT20MaxOffset[rfPath][group]	= EEPROM_Default_HT20_PwrMaxOffset;
			}
		}

		for(i = 0; i < 3; i++)
		{
			pwrInfo->TSSI_A_5G[i] = EEPROM_Default_TSSI;
			pwrInfo->TSSI_B_5G[i] = EEPROM_Default_TSSI;
		}
		pHalData->bNOPG = _TRUE;
		return;
	}

	//Maybe autoload OK,buf the tx power index vlaue is not filled.
	//If we find it,we set it default value.
	for(rfPath = 0 ; rfPath < RF_PATH_MAX ; rfPath++){
		for(group = 0 ; group < CHANNEL_GROUP_MAX_2G; group++){
			eeAddr = EEPROM_CCK_TX_PWR_INX_2G + (rfPath * 3) + group;
			pwrInfo->CCKIndex[rfPath][group] = 
				(PROMContent[eeAddr] == 0xFF)?(eeAddr>0x7B?EEPROM_Default_TxPowerLevel_5G:EEPROM_Default_TxPowerLevel_2G):PROMContent[eeAddr];
			if(PROMContent[eeAddr] == 0xFF)
				pHalData->bNOPG = _TRUE;
		}
	}
	for(rfPath = 0 ; rfPath < RF_PATH_MAX ; rfPath++){
		for(group = 0 ; group < CHANNEL_GROUP_MAX ; group++){
			offset1 = group / 3;
			offset2 = group % 3;
			eeAddr = EEPROM_HT40_1S_TX_PWR_INX_2G+ (rfPath * 3) + offset2 + offset1*21;
			pwrInfo->HT40_1SIndex[rfPath][group] = 
				(PROMContent[eeAddr] == 0xFF)?(eeAddr>0x7B?EEPROM_Default_TxPowerLevel_5G:EEPROM_Default_TxPowerLevel_2G):PROMContent[eeAddr];
		}
	}

	//These just for 92D efuse offset.
	for(group = 0 ; group < CHANNEL_GROUP_MAX ; group++){
		for(rfPath = 0 ; rfPath < RF_PATH_MAX ; rfPath++){
			offset1 = group / 3;
			offset2 = group % 3;

			if(PROMContent[EEPROM_HT40_2S_TX_PWR_INX_DIFF_2G+ offset2 + offset1*21] != 0xFF)
				pwrInfo->HT40_2SIndexDiff[rfPath][group] = 
					(PROMContent[EEPROM_HT40_2S_TX_PWR_INX_DIFF_2G+ offset2 + offset1*21] >> (rfPath * 4)) & 0xF;
					//RT_TRACE(COMP_INIT,DBG_LOUD,
					//	("ht40_2sdiff:group:%d,%x:0x%x.\n",group,EEPROM_HT40_2S_TX_PWR_INX_DIFF_2G+ offset2 + offset1*21,PROMContent[EEPROM_HT40_2S_TX_PWR_INX_DIFF_2G+ offset2 + offset1*21]));
			else
				pwrInfo->HT40_2SIndexDiff[rfPath][group]	= EEPROM_Default_HT40_2SDiff;

			if(PROMContent[EEPROM_HT20_TX_PWR_INX_DIFF_2G + offset2 + offset1*21] != 0xFF)
			{
				pwrInfo->HT20IndexDiff[rfPath][group] =
					(PROMContent[EEPROM_HT20_TX_PWR_INX_DIFF_2G+ offset2 + offset1*21] >> (rfPath * 4)) & 0xF;
				if(pwrInfo->HT20IndexDiff[rfPath][group] & BIT3)	//4bit sign number to 8 bit sign number
					pwrInfo->HT20IndexDiff[rfPath][group] |= 0xF0;				
			}
			else
			{
				pwrInfo->HT20IndexDiff[rfPath][group]		= EEPROM_Default_HT20_Diff;
			}

			if(PROMContent[EEPROM_OFDM_TX_PWR_INX_DIFF_2G + offset2 + offset1*21] != 0xFF)
				pwrInfo->OFDMIndexDiff[rfPath][group] =
					(PROMContent[EEPROM_OFDM_TX_PWR_INX_DIFF_2G + offset2 + offset1*21] >> (rfPath * 4)) & 0xF;
			else
				pwrInfo->OFDMIndexDiff[rfPath][group]	= EEPROM_Default_LegacyHTTxPowerDiff;

			if(PROMContent[EEPROM_HT40_MAX_PWR_OFFSET_2G + offset2 + offset1*21] != 0xFF)
				pwrInfo->HT40MaxOffset[rfPath][group] =
					(PROMContent[EEPROM_HT40_MAX_PWR_OFFSET_2G + offset2 + offset1*21] >> (rfPath * 4)) & 0xF;
			else
				pwrInfo->HT40MaxOffset[rfPath][group]	= EEPROM_Default_HT40_PwrMaxOffset;

			if(PROMContent[EEPROM_HT20_MAX_PWR_OFFSET_2G + offset2 + offset1*21] != 0xFF)
				pwrInfo->HT20MaxOffset[rfPath][group] =
					(PROMContent[EEPROM_HT20_MAX_PWR_OFFSET_2G + offset2 + offset1*21] >> (rfPath * 4)) & 0xF;
			else	
				pwrInfo->HT20MaxOffset[rfPath][group]	= EEPROM_Default_HT20_PwrMaxOffset;			
			
		}
	}

	if(PROMContent[EEPROM_TSSI_A_5G] != 0xFF){
		//5GL
		pwrInfo->TSSI_A_5G[0] = PROMContent[EEPROM_TSSI_A_5G] & 0x3F;	//[0:5]
		pwrInfo->TSSI_B_5G[0] = PROMContent[EEPROM_TSSI_B_5G] & 0x3F;

		//5GM
		pwrInfo->TSSI_A_5G[1] = PROMContent[EEPROM_TSSI_AB_5G] & 0x3F;
		pwrInfo->TSSI_B_5G[1] = (PROMContent[EEPROM_TSSI_AB_5G] & 0xC0) >> 6 | 
							(PROMContent[EEPROM_TSSI_AB_5G+1] & 0x0F) << 2;

		//5GH
		pwrInfo->TSSI_A_5G[2] = (PROMContent[EEPROM_TSSI_AB_5G+1] & 0xF0) >> 4 | 
							(PROMContent[EEPROM_TSSI_AB_5G+2] & 0x03) << 4;
		pwrInfo->TSSI_B_5G[2] = (PROMContent[EEPROM_TSSI_AB_5G+2] & 0xFC) >> 2 ;
	}
	else
	{
		for(i = 0; i < 3; i++)
		{
			pwrInfo->TSSI_A_5G[i] = EEPROM_Default_TSSI;
			pwrInfo->TSSI_B_5G[i] = EEPROM_Default_TSSI;
		}
	}
}

VOID
rtl8192d_ReadTxPowerInfo(
	IN	PADAPTER 		Adapter,
	IN	u8*			PROMContent,
	IN	BOOLEAN			AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	TxPowerInfo	pwrInfo;
	u32			rfPath, ch, group;
	u8			pwr, diff,tempval[2], i;

	hal_ReadPowerValueFromPROM92D(Adapter, &pwrInfo, PROMContent, AutoLoadFail);

	if(!AutoLoadFail)
	{
		pHalData->EEPROMRegulatory = (PROMContent[EEPROM_RF_OPT1]&0x7);	//bit0~2
		pHalData->EEPROMThermalMeter = PROMContent[EEPROM_THERMAL_METER]&0x1f;
		pHalData->CrystalCap = PROMContent[EEPROM_XTAL_K];
		tempval[0] = PROMContent[EEPROM_IQK_DELTA]&0x03;
		tempval[1] = (PROMContent[EEPROM_LCK_DELTA]&0x0C) >> 2;
		pHalData->bTXPowerDataReadFromEEPORM = _TRUE;
		if(IS_92D_D_CUT(pHalData->VersionID)||IS_92D_E_CUT(pHalData->VersionID))
		{
			pHalData->InternalPA5G[0] = !((PROMContent[EEPROM_TSSI_A_5G] & BIT6) >> 6);
			pHalData->InternalPA5G[1] = !((PROMContent[EEPROM_TSSI_B_5G] & BIT6) >> 6);
			DBG_8192C("Is D/E cut,Internal PA0 %d Internal PA1 %d\n",pHalData->InternalPA5G[0],pHalData->InternalPA5G[1]);
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

	if(pHalData->EEPROMC9 == 0xFF || AutoLoadFail)
	{
		switch(pHalData->PAMode)
		{
			//external pa
			case 0:	
				pHalData->EEPROMC9 = EEPROM_Default_externalPA_C9;
				pHalData->EEPROMCC = EEPROM_Default_externalPA_CC;	
				pHalData->InternalPA5G[0] = _FALSE;			
				pHalData->InternalPA5G[1] = _FALSE;			
				break;

			// internal pa - SP3T
			case 1:
				pHalData->EEPROMC9 = EEPROM_Default_internalPA_SP3T_C9;
				pHalData->EEPROMCC = EEPROM_Default_internalPA_SP3T_CC;			
				pHalData->InternalPA5G[0] = _TRUE;			
				pHalData->InternalPA5G[1] = _TRUE;						
				break;

			//intermal pa = SPDT
			case 2:
				pHalData->EEPROMC9 = EEPROM_Default_internalPA_SPDT_C9;
				pHalData->EEPROMCC = EEPROM_Default_internalPA_SPDT_CC;			
				pHalData->InternalPA5G[0] = _TRUE;			
				pHalData->InternalPA5G[1] = _TRUE;									
				break;

			default:
				break;
		}	
	}
	DBG_871X("PHY_SetPAMode mode %d, c9 = 0x%x cc = 0x%x interface index %d\n", pHalData->PAMode, pHalData->EEPROMC9, pHalData->EEPROMCC, pHalData->interfaceIndex);

	//Use default value to fill parameters if efuse is not filled on some place.	

	// ThermalMeter from EEPROM
	if(pHalData->EEPROMThermalMeter < 0x06 || pHalData->EEPROMThermalMeter > 0x1c)
		pHalData->EEPROMThermalMeter = 0x12;
	
	pdmpriv->ThermalMeter[0] = pHalData->EEPROMThermalMeter;	

	//check XTAL_K
	if(pHalData->CrystalCap == 0xFF)
		pHalData->CrystalCap = 0;

	if(pHalData->EEPROMRegulatory >3)
		pHalData->EEPROMRegulatory = 0;

	for(i = 0; i < 2; i++)
	{
		switch(tempval[i])
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

#if 1
	pdmpriv->Delta_IQK = tempval[0];
	pdmpriv->Delta_LCK = 5;
#else
	//temporarily close 92D re-IQK&LCK by thermal meter,advised by allen.2010-11-03
	pHalData->Delta_IQK = 0;
	pHalData->Delta_LCK = 0;
#endif

	if(pHalData->EEPROMC9 == 0xFF)
		pHalData->EEPROMC9 = 0x00;

	//RTPRINT(FINIT, INIT_TxPower, ("EEPROMRegulatory = 0x%x\n", pHalData->EEPROMRegulatory));		
	//RTPRINT(FINIT, INIT_TxPower, ("ThermalMeter = 0x%x\n", pHalData->EEPROMThermalMeter));	
	//RTPRINT(FINIT, INIT_TxPower, ("CrystalCap = 0x%x\n", pHalData->CrystalCap));	
	//RTPRINT(FINIT, INIT_TxPower, ("Delta_IQK = 0x%x Delta_LCK = 0x%x\n", pHalData->Delta_IQK, pHalData->Delta_LCK));	
	
	for(rfPath = 0 ; rfPath < RF_PATH_MAX ; rfPath++){
		for(ch = 0 ; ch < CHANNEL_MAX_NUMBER ; ch++){
			group = Hal_GetChnlGroupfromArray((u8)ch);

			if(ch < CHANNEL_MAX_NUMBER_2G)
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

	for(rfPath = 0 ; rfPath < RF_PATH_MAX ; rfPath++){
		for(ch = 0 ; ch < CHANNEL_MAX_NUMBER ; ch++){
			if(ch < CHANNEL_MAX_NUMBER_2G)
			{
				DBG_8192C("RF(%d)-Ch(%d) [CCK / HT40_1S / HT40_2S] = [0x%x / 0x%x / 0x%x]\n", 
					rfPath, ch, 
					pHalData->TxPwrLevelCck[rfPath][ch], 
					pHalData->TxPwrLevelHT40_1S[rfPath][ch], 
					pHalData->TxPwrLevelHT40_2S[rfPath][ch]);
			}
			else
			{
				DBG_8192C("RF(%d)-Ch(%d) [HT40_1S / HT40_2S] = [0x%x / 0x%x]\n", 
					rfPath, ch, 
					pHalData->TxPwrLevelHT40_1S[rfPath][ch], 
					pHalData->TxPwrLevelHT40_2S[rfPath][ch]);
			}
		}
	}

	for(ch = 0 ; ch < CHANNEL_MAX_NUMBER ; ch++){
		DBG_8192C("RF-A Ht20 to HT40 Diff[%d] = 0x%x\n", ch, pHalData->TxPwrHt20Diff[RF_PATH_A][ch]);
	}

	for(ch = 0 ; ch < CHANNEL_MAX_NUMBER ; ch++){
		DBG_8192C("RF-A Legacy to Ht40 Diff[%d] = 0x%x\n", ch, pHalData->TxPwrLegacyHtDiff[RF_PATH_A][ch]);
	}
	
	for(ch = 0 ; ch < CHANNEL_MAX_NUMBER ; ch++){
		DBG_8192C("RF-B Ht20 to HT40 Diff[%d] = 0x%x\n", ch, pHalData->TxPwrHt20Diff[RF_PATH_B][ch]);
	}
	
	for(ch = 0 ; ch < CHANNEL_MAX_NUMBER ; ch++){
		DBG_8192C("RF-B Legacy to HT40 Diff[%d] = 0x%x\n", ch, pHalData->TxPwrLegacyHtDiff[RF_PATH_B][ch]);
	}
	
#endif

}

//
//	Description:
//		Reset Dual Mac Mode Switch related settings
//
//	Assumption:
//
VOID rtl8192d_ResetDualMacSwitchVariables(
		IN PADAPTER			Adapter
)
{
#ifdef CONFIG_DUALMAC_CONCURRENT
/*	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PADAPTER		BuddyAdapter = Adapter->BuddyAdapter;

	Adapter->bNeedReConfigMac = _FALSE;
	Adapter->bNeedReConfigPhyRf = _FALSE;
	Adapter->bNeedTurnOffAdapterInModeSwitchProcess = _FALSE;
	Adapter->bNeedRecoveryAfterModeSwitch = _FALSE;
	Adapter->bInModeSwitchProcess = _FALSE;
	Adapter->bDoTurnOffPhyRf  = _FALSE;

	if(BuddyAdapter != NULL)
	{
		Adapter->PreChangeAction = BuddyAdapter->PreChangeAction;
	}
	else
	{
		Adapter->PreChangeAction = MAXACTION;
	}

	//set dual mac role to set
	Adapter->DualMacRoleToSet.BandType = pHalData->CurrentBandType92D;
	Adapter->DualMacRoleToSet.BandSet = pHalData->BandSet92D;
	Adapter->DualMacRoleToSet.RFType = pHalData->RF_Type;
	Adapter->DualMacRoleToSet.macPhyMode = pHalData->MacPhyMode92D ;
	Adapter->DualMacRoleToSet.bMasterOfDMSP = Adapter->bMasterOfDMSP;
	Adapter->DualMacRoleToSet.bSlaveOfDMSP = Adapter->bSlaveOfDMSP;*/
#endif //CONFIG_DUALMAC_CONCURRENT

}

u8 GetEEPROMSize8192D(PADAPTER Adapter)
{
	u8	size = 0;
	u32	curRCR;

	curRCR = rtw_read16(Adapter, REG_9346CR);
	size = (curRCR & BOOT_FROM_EEPROM) ? 6 : 4; // 6: EEPROM used is 93C46, 4: boot from E-Fuse.
	
	MSG_8192C("EEPROM type is %s\n", size==4 ? "E-FUSE" : "93C46");
	
	return size;
}

void rtl8192d_HalSetBrateCfg(
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

/************************************************************
Function: Synchrosize for power off with dual mac
*************************************************************/
BOOLEAN
PHY_CheckPowerOffFor8192D(
	PADAPTER   Adapter
)
{
	u8 u1bTmp;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);

	if(pHalData->MacPhyMode92D==SINGLEMAC_SINGLEPHY)
	{
		u1bTmp = rtw_read8(Adapter, REG_MAC0);
		rtw_write8(Adapter, REG_MAC0, u1bTmp&(~MAC0_ON));	
		return _TRUE;	  
	}

	ACQUIRE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);
	if(pHalData->interfaceIndex == 0){
		u1bTmp = rtw_read8(Adapter, REG_MAC0);
		rtw_write8(Adapter, REG_MAC0, u1bTmp&(~MAC0_ON));	
		u1bTmp = rtw_read8(Adapter, REG_MAC1);
		u1bTmp &=MAC1_ON;

	}
	else{
		u1bTmp = rtw_read8(Adapter, REG_MAC1);
		rtw_write8(Adapter, REG_MAC1, u1bTmp&(~MAC1_ON));
		u1bTmp = rtw_read8(Adapter, REG_MAC0);
		u1bTmp &=MAC0_ON;
	}

	if(u1bTmp)
	{
		RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);
		return _FALSE;	
	}

	u1bTmp=rtw_read8(Adapter, REG_POWER_OFF_IN_PROCESS);
	u1bTmp|=BIT7;
	rtw_write8(Adapter, REG_POWER_OFF_IN_PROCESS, u1bTmp);

	RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);
	return _TRUE;	
}


/************************************************************
Function: Synchrosize for power off/on with dual mac
*************************************************************/
VOID
PHY_SetPowerOnFor8192D(
	PADAPTER	Adapter
)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(Adapter);
	u8	value8;
	u16	i;

	// notice fw know band status  0x81[1]/0x53[1] = 0: 5G, 1: 2G
	if(pHalData->CurrentBandType92D==BAND_ON_2_4G)
	{
		value8 = rtw_read8(Adapter, (pHalData->interfaceIndex==0?REG_MAC0:REG_MAC1));
		value8 |= BIT1;
		rtw_write8(Adapter, (pHalData->interfaceIndex==0?REG_MAC0:REG_MAC1),value8);
	}
	else
	{
		value8 = rtw_read8(Adapter, (pHalData->interfaceIndex==0?REG_MAC0:REG_MAC1));
		value8 &= (~BIT1);
		rtw_write8(Adapter, (pHalData->interfaceIndex==0?REG_MAC0:REG_MAC1),value8);	
	}

	if(pHalData->MacPhyMode92D ==SINGLEMAC_SINGLEPHY)
	{
		value8 = rtw_read8(Adapter, REG_MAC0);		
		rtw_write8(Adapter, REG_MAC0, value8|MAC0_ON);
	}
	else
	{
		ACQUIRE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);
		if(pHalData->interfaceIndex == 0)
		{
			value8 = rtw_read8(Adapter, REG_MAC0);		
			rtw_write8(Adapter, REG_MAC0, value8|MAC0_ON);
		}
		else	
		{
			value8 = rtw_read8(Adapter, REG_MAC1);
			rtw_write8(Adapter, REG_MAC1, value8|MAC1_ON);
		}
		value8 = rtw_read8(Adapter, REG_POWER_OFF_IN_PROCESS);
		RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);

		for(i=0;i<200;i++)
		{
			if((value8&BIT7) == 0)
			{
				break;
			}
			else
			{
				rtw_udelay_os(500);
				ACQUIRE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);
				value8 = rtw_read8(Adapter, REG_POWER_OFF_IN_PROCESS);
				RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);
			}
		}

		if(i==200)
			DBG_8192C("Another mac power off over time \n");
	}
}

void rtl8192d_free_hal_data(_adapter * padapter)
{
_func_enter_;

	DBG_8192C("===== rtl8192du_free_hal_data =====\n");

	if(padapter->HalData)
		rtw_mfree(padapter->HalData, sizeof(HAL_DATA_TYPE));
#ifdef CONFIG_DUALMAC_CONCURRENT
	GlobalFirstConfigurationForNormalChip = _TRUE;
#endif
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
rtl8192d_EfusePowerSwitch(
	IN	PADAPTER	pAdapter,
	IN	u8		bWrite,
	IN	u8		PwrState)
{
	u8	tempval;
	u16	tmpV16;

	if (PwrState == _TRUE)
	{
		// 1.2V Power: From VDDON with Power Cut(0x0000h[15]), defualt valid
		tmpV16 = rtw_read16(pAdapter, REG_SYS_ISO_CTRL);
		if (!(tmpV16 & PWC_EV12V)) {
			tmpV16 |= PWC_EV12V ;
			 rtw_write16(pAdapter, REG_SYS_ISO_CTRL, tmpV16);
		}
		// Reset: 0x0000h[28], default valid
		tmpV16 = rtw_read16(pAdapter, REG_SYS_FUNC_EN);
		if (!(tmpV16 & FEN_ELDR)) {
			tmpV16 |= FEN_ELDR ;
			rtw_write16(pAdapter,REG_SYS_FUNC_EN,tmpV16);
		}

		// Clock: Gated(0x0008h[5]) 8M(0x0008h[1]) clock from ANA, default valid
		tmpV16 = rtw_read16(pAdapter, REG_SYS_CLKR);
		if ( (!(tmpV16 & LOADER_CLK_EN)) || (!(tmpV16 & ANA8M)) ){
			tmpV16 |= (LOADER_CLK_EN | ANA8M);
			rtw_write16(pAdapter, REG_SYS_CLKR, tmpV16);
		}

		if(bWrite == _TRUE){
			// Enable LDO 2.5V before read/write action
			tempval = rtw_read8(pAdapter, EFUSE_TEST+3);
			tempval &= 0x0F;
			tempval |= (VOLTAGE_V25 << 4);
			rtw_write8(pAdapter, EFUSE_TEST+3, (tempval | 0x80));
		}
	}
	else
	{
		if (bWrite == _TRUE) {
			// Disable LDO 2.5V after read/write action
			tempval = rtw_read8(pAdapter, EFUSE_TEST+3);
			rtw_write8(pAdapter, EFUSE_TEST+3, (tempval & 0x7F));
		}
	}
}

static VOID
ReadEFuse_RTL8192D(
	PADAPTER	Adapter,
	u8			efuseType,
	u16			_offset,
	u16 			_size_byte,
	u8      		*pbuf,
	IN BOOLEAN	bPseudoTest
	)
{
	u8  	efuseTbl[EFUSE_MAP_LEN];
	u8  	rtemp8[1];
	u16 	eFuse_Addr = 0;
	u8  	offset, wren;
	u16  i, j;
	u16 	eFuseWord[EFUSE_MAX_SECTION][EFUSE_MAX_WORD_UNIT];
	u16	efuse_utilized = 0;
	u8	efuse_usage = 0;
	u8	u1temp = 0;

	//
	// Do NOT excess total size of EFuse table. Added by Roger, 2008.11.10.
	//
	if((_offset + _size_byte)>EFUSE_MAP_LEN)
	{// total E-Fuse table is 128bytes
		DBG_8192C("ReadEFuse(): Invalid offset(%#x) with read bytes(%#x)!!\n",_offset, _size_byte);
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
	else
	{
		//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("EFUSE is empty efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, *rtemp8));
		return;
	}

	//
	// 2. Read real efuse content. Filter PG header and every section data.
	//
	while((*rtemp8 != 0xFF) && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN))
	{
		// Check PG header for section num.
		if((*rtemp8 & 0x1F ) == 0x0F)		//extended header
		{
		
			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header u1temp=%x *rtemp&0xE0 0x%x\n", u1temp, *rtemp8 & 0xE0));
			
			u1temp =( (*rtemp8 & 0xE0) >> 5);

			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header u1temp=%x \n", u1temp));
			
			ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);

			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, *rtemp8));	
			
			if((*rtemp8 & 0x0F) == 0x0F)
			{
				eFuse_Addr++;			
				ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest); 
				
				if(*rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN))
				{
					eFuse_Addr++;				
				}				
				continue;
			}
			else
			{
				offset = ((*rtemp8 & 0xF0) >> 1) | u1temp;
				wren = (*rtemp8 & 0x0F);
				eFuse_Addr++;				
			}
		}
		else
		{
			offset = ((*rtemp8 >> 4) & 0x0f);
			wren = (*rtemp8 & 0x0f);			
		}

		if(offset < EFUSE_MAX_SECTION)
		{
			// Get word enable value from PG header
			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Offset-%d Worden=%x\n", offset, wren));

			for(i=0; i<EFUSE_MAX_WORD_UNIT; i++)
			{
				// Check word enable condition in the section				
				if(!(wren & 0x01))
				{
					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d\n", eFuse_Addr));
					ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);
					eFuse_Addr++;
					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Data=0x%x\n", *rtemp8));
					efuse_utilized++;
					eFuseWord[offset][i] = (*rtemp8 & 0xff);
					

					if(eFuse_Addr >= EFUSE_REAL_CONTENT_LEN) 
						break;

					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d\n", eFuse_Addr));
					ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);
					eFuse_Addr++;
					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Data=0x%x\n", *rtemp8));

					efuse_utilized++;
					eFuseWord[offset][i] |= (((u16)*rtemp8 << 8) & 0xff00);

					if(eFuse_Addr >= EFUSE_REAL_CONTENT_LEN) 
						break;
				}
				
				wren >>= 1;
				
			}
		}

		// Read next PG header
		ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);
		//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d rtemp 0x%x\n", eFuse_Addr, *rtemp8));
		
		if(*rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN))
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
	//priv->EfuseUsedBytes = efuse_utilized;
	efuse_usage = (u8)((efuse_utilized*100)/EFUSE_REAL_CONTENT_LEN);
	//priv->EfuseUsedPercentage = efuse_usage;	
	Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_EFUSE_BYTES, (u8*)&efuse_utilized);
	//Adapter->HalFunc.SetHwRegHandler(dev, HW_VAR_EFUSE_USAGE, (u8*)&efuse_usage);
}

static VOID
hal_EfuseUpdateNormalChipVersion_92D(
	IN	PADAPTER	Adapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	VERSION_8192D	ChipVer = pHalData->VersionID;
	u8	CutValue[2];
	u16	ChipValue=0;
		
	ReadEFuseByte(Adapter,EEPROME_CHIP_VERSION_H,&CutValue[1], _FALSE);
	ReadEFuseByte(Adapter,EEPROME_CHIP_VERSION_L,&CutValue[0], _FALSE);

	ChipValue= (CutValue[1]<<8)|CutValue[0];
	switch(ChipValue){
		case 0xAA55:
			//ChipVer |= CHIP_92D_C_CUT;
			ChipVer = (VERSION_8192D)( ChipVer | C_CUT_VERSION);
			MSG_8192C("C-CUT!!!\n");
			break;
		case 0x9966:
			//ChipVer |= CHIP_92D_D_CUT;
			ChipVer = (VERSION_8192D)( ChipVer | D_CUT_VERSION);
			MSG_8192C("D-CUT!!!\n");
			break;
		case 0xCC33:
			ChipVer = (VERSION_8192D)( ChipVer | E_CUT_VERSION);
			MSG_8192C("E-CUT!!!\n");
			break;
		default:
			//ChipVer |= CHIP_92D_D_CUT;
			ChipVer = (VERSION_8192D)( ChipVer | D_CUT_VERSION);
			MSG_8192C("Unkown CUT!\n");
			break; 
	}

	pHalData->VersionID = ChipVer;
}

static BOOLEAN 
hal_EfuseMacMode_ISVS_92D(
     IN     PADAPTER     Adapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	PartNo;
	BOOLEAN bResult = _FALSE;
	// 92D VS not support dual mac mode  
	if(IS_NORMAL_CHIP92D(pHalData->VersionID))
	{
		ReadEFuseByte(Adapter,EEPROM_DEF_PART_NO,&PartNo, _FALSE);	
		//RT_TRACE(COMP_INIT, DBG_LOUD, ("92D efuse byte 1021 content :%d \n",PartNo));
		
		if((((PartNo & 0xc0) ==  PARTNO_92D_NIC)&&((PartNo & 0x0c) == PARTNO_SINGLE_BAND_VS))||
			(((PartNo & 0xF0) == PARTNO_92D_NIC_REMARK) &&((PartNo & 0x0F) == PARTNO_SINGLE_BAND_VS_REMARK)))
		{
			//RT_TRACE(COMP_INIT, DBG_SERIOUS, ("92D VS !\n"));
			bResult = _TRUE;
		}
		else if(PartNo == 0x00)
		{
			ReadEFuseByte(Adapter,EEPROM_DEF_PART_NO+1,&PartNo, _FALSE);			
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("92D efuse byte 1022 content :%d \n",PartNo));
			if((((PartNo & 0xc0) ==  PARTNO_92D_NIC)&&((PartNo & 0x0c) == PARTNO_SINGLE_BAND_VS))||
				(((PartNo & 0xF0) == PARTNO_92D_NIC_REMARK) &&((PartNo & 0x0F) == PARTNO_SINGLE_BAND_VS_REMARK)))
			{
				//RT_TRACE(COMP_INIT, DBG_SERIOUS, ("92D VS !\n"));
				bResult = _TRUE;
			}			
		}
	}
	
	return bResult;
}

static VOID
rtl8192d_ReadEFuse(
	PADAPTER	Adapter,
	u8			efuseType,
	u16			_offset,
	u16 			_size_byte,
	u8      		*pbuf,
	IN BOOLEAN	bPseudoTest
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);

	ReadEFuse_RTL8192D(Adapter, efuseType, _offset, _size_byte, pbuf, bPseudoTest);

	hal_EfuseUpdateNormalChipVersion_92D(Adapter);
	pHalData->bIsVS = hal_EfuseMacMode_ISVS_92D(Adapter);
}

static VOID
rtl8192d_EFUSE_GetEfuseDefinition(
	IN		PADAPTER	pAdapter,
	IN		u8		efuseType,
	IN		u8		type,
	OUT		PVOID		*pOut,
	IN		BOOLEAN		bPseudoTest
	)
{
	switch(type)
	{
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
		default:
			{
				u8 *pu1Tmp;
				pu1Tmp = (u8 *)pOut;
				*pu1Tmp = 0;
			}
			break;
	}
}

static u16
rtl8192d_EfuseGetCurrentSize(
	IN	PADAPTER	pAdapter,
	IN	u8			efuseType,
	IN	BOOLEAN		bPseudoTest)
{
	int	bContinual = _TRUE;

	u16	efuse_addr = 0;
	u8	hoffset=0,hworden=0;	
	u8	efuse_data,word_cnts=0;
		
	while (	bContinual && 
			efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest) && 
			(efuse_addr  < EFUSE_REAL_CONTENT_LEN) )
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

	return efuse_addr;
	
}

static int
rtl8192d_Efuse_PgPacketRead(	IN	PADAPTER	pAdapter,
					IN	u8			offset,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{	
	u8	ReadState = PG_STATE_HEADER;	
	
	int	bContinual = _TRUE;
	int	bDataEmpty = _TRUE ;	

	u8	efuse_data,word_cnts=0;
	u16	efuse_addr = 0;
	u8	hoffset=0,hworden=0;	
	u8	tmpidx=0;
	u8	tmpdata[8];
	u8	tmp_header = 0;
	
	if(data==NULL)	return _FALSE;
	if(offset>=EFUSE_MAX_SECTION)		return _FALSE;	
	

	_rtw_memset((PVOID)data, 0xff, sizeof(u8)*PGPKT_DATA_SIZE);
	_rtw_memset((PVOID)tmpdata, 0xff, sizeof(u8)*PGPKT_DATA_SIZE);
	
	//RT_PRINT_DATA(COMP_EFUSE, DBG_LOUD, ("efuse_PgPacketRead-1\n"), data, 8);
	
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
			if(efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest)&&(efuse_data!=0xFF))
			{
				if((efuse_data & 0x1F) == 0x0F)
				{
					tmp_header = efuse_data;
					efuse_addr++;
					efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest);
					if((efuse_data & 0x0F) != 0x0F)
					{
						hoffset = ((tmp_header & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
						hworden = efuse_data & 0x0F;						
					}
					else
					{
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
	//efuse_reg_ctrl(pAdapter,_FALSE);//power off
	
	//RT_PRINT_DATA(COMP_EFUSE, DBG_LOUD, ("efuse_PgPacketRead-2\n"), data, 8);
	
	if(	(data[0]==0xff) &&(data[1]==0xff) && (data[2]==0xff)  && (data[3]==0xff) &&
		(data[4]==0xff) &&(data[5]==0xff) && (data[6]==0xff)  && (data[7]==0xff))
		return _FALSE;
	else
		return _TRUE;

}

static int 
rtl8192d_Efuse_PgPacketWrite(IN	PADAPTER	pAdapter, 
					IN	u8 			offset,
					IN	u8			word_en,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	u8	WriteState = PG_STATE_HEADER;		

	int	bContinual = _TRUE,bDataEmpty=_TRUE, bResult = _TRUE;
	u16	efuse_addr = 0;
	u8	efuse_data;

	u8	pg_header = 0, pg_header_temp = 0;

	u8	tmp_word_cnts=0,target_word_cnts=0;
	u8	tmp_header,match_word_en,tmp_word_en;

	PGPKT_STRUCT target_pkt;	
	PGPKT_STRUCT tmp_pkt=
	{
		.offset=0,
		.word_en=0,
		.data={0,0,0,0,0,0,0,0},
		.word_cnts=0
	};
	
	u8	originaldata[sizeof(u8)*8];
	u8	tmpindex = 0,badworden = 0x0F;

	static int	repeat_times = 0;

	BOOLEAN		bExtendedHeader = _FALSE;
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
		//RTPRINT(FEEPROM, EFUSE_PG, ("efuse_PgPacketWrite error \n"));
		return _FALSE;
	}

	// Init the 8 bytes content as 0xff
	target_pkt.offset = offset;
	target_pkt.word_en= word_en;

	//RTPRINT(FEEPROM, EFUSE_PG, ("efuse_PgPacketWrite target offset 0x%x word_en 0x%x \n", target_pkt.offset, target_pkt.word_en));
	

	_rtw_memset((PVOID)target_pkt.data, 0xFF, sizeof(u8)*8);
	
	efuse_WordEnableDataRead(word_en, data, target_pkt.data);
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
				if((efuse_data&0x1F) == 0x0F)		//extended header
				{
					tmp_header = efuse_data;
					efuse_addr++;
					efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest);
					if((efuse_data & 0x0F) == 0x0F)	//wren fail
					{
						efuse_addr++;
						continue;
					}
					else
					{
						tmp_pkt.offset = ((tmp_header & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
						tmp_pkt.word_en = efuse_data & 0x0F;
					}
				}
				else
				{
					tmp_header  =  efuse_data;					
					tmp_pkt.offset 	= (tmp_header>>4) & 0x0F;
					tmp_pkt.word_en = tmp_header & 0x0F;					
				}				
				tmp_word_cnts =  Efuse_CalculateWordCnts(tmp_pkt.word_en);

				//RTPRINT(FEEPROM, EFUSE_PG, ("section offset 0x%x worden 0x%x\n", tmp_pkt.offset, tmp_pkt.word_en));

				//************  so-1 *******************
				if(tmp_pkt.offset  != target_pkt.offset)
				{
					efuse_addr = efuse_addr + (tmp_word_cnts*2) +1; //Next pg_packet
#if (EFUSE_ERROE_HANDLE == 1)
					WriteState = PG_STATE_HEADER;
#endif
				}
				else		//write the same offset
				{	
					//RTPRINT(FEEPROM, EFUSE_PG, ("efuse_PgPacketWrite section offset the same\n"));
				
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
						//RTPRINT(FEEPROM, EFUSE_PG, ("efuse_PgPacketWrite section offset the same and data is NOT empty\n"));
					
						efuse_addr = efuse_addr + (tmp_word_cnts*2) +1; //Next pg_packet										
#if (EFUSE_ERROE_HANDLE == 1)
						WriteState=PG_STATE_HEADER;
#endif
					}
					else
					{//************  so-2-2 *******************

						//RTPRINT(FEEPROM, EFUSE_PG, ("efuse_PgPacketWrite section data empty\n"));
					
						match_word_en = 0x0F;			//same bit as original wren
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
								Efuse_PgPacketWrite(pAdapter, reorg_offset, reorg_worden, target_pkt.data, bPseudoTest);	
							}	
							//############################						

							tmp_word_en = 0x0F;		//not the same bit as original wren
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
//								efuse_addr = efuse_addr + (2*tmp_word_cnts) +1;//next pg packet addr							
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
				bExtendedHeader = _FALSE;
			
				if(target_pkt.offset >= EFUSE_MAX_SECTION_BASE)
				{
					pg_header = ((target_pkt.offset &0x07) << 5) | 0x0F;

					//RTPRINT(FEEPROM, EFUSE_PG, ("efuse_PgPacketWrite extended pg_header[2:0] |0x0F 0x%x \n", pg_header));
					
					efuse_OneByteWrite(pAdapter,efuse_addr, pg_header, bPseudoTest);
					efuse_OneByteRead(pAdapter,efuse_addr, &tmp_header, bPseudoTest);							

					while(tmp_header == 0xFF)
					{		
						//RTPRINT(FEEPROM, EFUSE_PG, ("efuse_PgPacketWrite extended pg_header[2:0] wirte fail \n"));
					
						repeat_times++; 	
					
						if(repeat_times>EFUSE_REPEAT_THRESHOLD_){
							bContinual = _FALSE;
							bResult = _FALSE;
							efuse_addr++;
							break;
						}		
						efuse_OneByteWrite(pAdapter,efuse_addr, pg_header, bPseudoTest);
						efuse_OneByteRead(pAdapter,efuse_addr, &tmp_header, bPseudoTest);																									
					}
					
					if(!bContinual)
						break;

					if(tmp_header == pg_header)
					{
						efuse_addr++;
						pg_header_temp = pg_header;
						pg_header = ((target_pkt.offset & 0x78) << 1 ) | target_pkt.word_en;

						//RTPRINT(FEEPROM, EFUSE_PG, ("efuse_PgPacketWrite extended pg_header[6:3] | worden 0x%x word_en 0x%x \n", pg_header));
						
						efuse_OneByteWrite(pAdapter,efuse_addr, pg_header, bPseudoTest);
						efuse_OneByteRead(pAdapter,efuse_addr, &tmp_header, bPseudoTest);													

						while(tmp_header == 0xFF)
						{											
							repeat_times++;		

							if(repeat_times>EFUSE_REPEAT_THRESHOLD_){
								bContinual = _FALSE;
								bResult = _FALSE;
								break;
							}							
							efuse_OneByteWrite(pAdapter,efuse_addr, pg_header, bPseudoTest);
							efuse_OneByteRead(pAdapter,efuse_addr, &tmp_header, bPseudoTest);																										
						}

						if(!bContinual)
							break;

						if((tmp_header & 0x0F) == 0x0F)	//wren PG fail
						{
							repeat_times++;		

							if(repeat_times>EFUSE_REPEAT_THRESHOLD_){
								bContinual = _FALSE;
								bResult = _FALSE;
								break;
							}							
							else
							{
								efuse_addr++;
								continue;
							}
						}
						else if(pg_header != tmp_header)	//offset PG fail						
						{
							bExtendedHeader = _TRUE;
							tmp_pkt.offset = ((pg_header_temp & 0xE0) >> 5) | ((tmp_header & 0xF0) >> 1);
							tmp_pkt.word_en=  tmp_header & 0x0F;					
							tmp_word_cnts =  Efuse_CalculateWordCnts(tmp_pkt.word_en);							
						}
					}
					else if ((tmp_header & 0x1F) == 0x0F)		//wrong extended header
					{
						efuse_addr+=2;
						continue;						
					}
				}
				else
				{
					pg_header = ((target_pkt.offset << 4)&0xf0) |target_pkt.word_en;
					efuse_OneByteWrite(pAdapter,efuse_addr, pg_header, bPseudoTest);
					efuse_OneByteRead(pAdapter,efuse_addr, &tmp_header, bPseudoTest);					
				}
		
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
					if(!bExtendedHeader)
					{
						tmp_pkt.offset = (tmp_header>>4) & 0x0F;
						tmp_pkt.word_en=  tmp_header & 0x0F;					
						tmp_word_cnts =  Efuse_CalculateWordCnts(tmp_pkt.word_en);
					}
																											
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
			badworden = Efuse_WordEnableDataWrite(pAdapter,efuse_addr+1,target_pkt.word_en,target_pkt.data , bPseudoTest);	
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
		//RT_TRACE(COMP_EFUSE, DBG_LOUD, ("efuse_PgPacketWrite(): efuse_addr(%#x) Out of size!!\n", efuse_addr));
	}
	//efuse_reg_ctrl(pAdapter,_FALSE);//power off
	
	return _TRUE;
}

static u8
rtl8192d_Efuse_WordEnableDataWrite(	IN	PADAPTER	pAdapter,
							IN	u16		efuse_addr,
							IN	u8		word_en, 
							IN	u8		*data,
							IN	BOOLEAN		bPseudoTest)
{		
	u16	tmpaddr = 0;
	u16	start_addr = efuse_addr;
	u8	badworden = 0x0F;
	u8	tmpdata[8]; 
	
	//memset(tmpdata,0xff,PGPKT_DATA_SIZE);
	_rtw_memset((PVOID)tmpdata, 0xff, PGPKT_DATA_SIZE);
	//RT_TRACE(COMP_EFUSE, DBG_LOUD, ("word_en = %x efuse_addr=%x\n", word_en, efuse_addr));

	//RT_PRINT_DATA(COMP_EFUSE, DBG_LOUD, ("U-EFUSE\n"), data, 8);

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

	//Efuse related function
	pHalFunc->EfusePowerSwitch = &rtl8192d_EfusePowerSwitch;
	pHalFunc->ReadEFuse = &rtl8192d_ReadEFuse;
	pHalFunc->EFUSEGetEfuseDefinition = &rtl8192d_EFUSE_GetEfuseDefinition;
	pHalFunc->EfuseGetCurrentSize = &rtl8192d_EfuseGetCurrentSize;
	pHalFunc->Efuse_PgPacketRead = &rtl8192d_Efuse_PgPacketRead;
	pHalFunc->Efuse_PgPacketWrite = &rtl8192d_Efuse_PgPacketWrite;
	pHalFunc->Efuse_WordEnableDataWrite = &rtl8192d_Efuse_WordEnableDataWrite;

#ifdef CONFIG_IOL
	pHalFunc->IOL_exec_cmds_sync = NULL;
#endif
}

