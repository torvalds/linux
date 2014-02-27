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
#define _HCI_HAL_INIT_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_efuse.h>


#include <HalPwrSeqCmd.h>
#include <Hal8723PwrSeq.h>
#include <rtl8723a_hal.h>
#include <rtl8723a_led.h>

#ifdef CONFIG_IOL
#include <rtw_iol.h>
#endif

#ifdef CONFIG_EFUSE_CONFIG_FILE
#include <linux/fs.h>
#include <asm/uaccess.h>
#endif //CONFIG_EFUSE_CONFIG_FILE

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)

#error "Shall be Linux or Windows, but not both!\n"

#endif

#ifndef CONFIG_USB_HCI

#error "CONFIG_USB_HCI shall be on!\n"

#endif

#include <usb_ops.h>
#include <usb_hal.h>
#include <usb_osintf.h>

#if DISABLE_BB_RF
	#define		HAL_MAC_ENABLE	0
	#define		HAL_BB_ENABLE		0
	#define		HAL_RF_ENABLE		0
#else
	#define		HAL_MAC_ENABLE	1
	#define		HAL_BB_ENABLE		1
	#define		HAL_RF_ENABLE		1
#endif


static VOID
_ConfigChipOutEP(
	IN	PADAPTER	pAdapter,
	IN	u8		NumOutPipe
	)
{
	u8			value8;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);

	pHalData->OutEpQueueSel = 0;
	pHalData->OutEpNumber	= 0;
		
	// Normal and High queue
	value8 = rtw_read8(pAdapter, (REG_NORMAL_SIE_EP + 1));
	
	if(value8 & USB_NORMAL_SIE_EP_MASK){
		pHalData->OutEpQueueSel |= TX_SELE_HQ;
		pHalData->OutEpNumber++;
	}
	
	if((value8 >> USB_NORMAL_SIE_EP_SHIFT) & USB_NORMAL_SIE_EP_MASK){
		pHalData->OutEpQueueSel |= TX_SELE_NQ;
		pHalData->OutEpNumber++;
	}
	
	// Low queue
	value8 = rtw_read8(pAdapter, (REG_NORMAL_SIE_EP + 2));
	if(value8 & USB_NORMAL_SIE_EP_MASK){
		pHalData->OutEpQueueSel |= TX_SELE_LQ;
		pHalData->OutEpNumber++;
	}

	// TODO: Error recovery for this case
	//RT_ASSERT((NumOutPipe == pHalData->OutEpNumber), ("Out EP number isn't match! %d(Descriptor) != %d (SIE reg)\n", (u4Byte)NumOutPipe, (u4Byte)pHalData->OutEpNumber));

}

static BOOLEAN HalUsbSetQueuePipeMapping8192CUsb(
	IN	PADAPTER	pAdapter,
	IN	u8		NumInPipe,
	IN	u8		NumOutPipe
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);
	BOOLEAN			result		= _FALSE;
	
	_ConfigChipOutEP(pAdapter, NumOutPipe);

	// Normal chip with one IN and one OUT doesn't have interrupt IN EP.
	if(1 == pHalData->OutEpNumber){
		if(1 != NumInPipe){
			return result;
		}
	}

	result = Hal_MappingOutPipe(pAdapter, NumOutPipe);
	
	return result;

}

void rtl8192cu_interface_configure(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);

	if (pdvobjpriv->ishighspeed == _TRUE)
	{
		pHalData->UsbBulkOutSize = USB_HIGH_SPEED_BULK_SIZE;//512 bytes
	}
	else
	{
		pHalData->UsbBulkOutSize = USB_FULL_SPEED_BULK_SIZE;//64 bytes
	}

	pHalData->interfaceIndex = pdvobjpriv->InterfaceNumber;

#ifdef CONFIG_USB_TX_AGGREGATION
	pHalData->UsbTxAggMode		= 1;
	pHalData->UsbTxAggDescNum	= 0x6;	// only 4 bits
#endif

#ifdef CONFIG_USB_RX_AGGREGATION
	pHalData->UsbRxAggMode		= USB_RX_AGG_DMA;// USB_RX_AGG_DMA;
	pHalData->UsbRxAggBlockCount	= 8; //unit : 512b
	pHalData->UsbRxAggBlockTimeout	= 0x6;
	pHalData->UsbRxAggPageCount	= 48; //uint :128 b //0x0A;	// 10 = MAX_RX_DMA_BUFFER_SIZE/2/pHalData->UsbBulkOutSize
	pHalData->UsbRxAggPageTimeout	= 0x4; //6, absolute time = 34ms/(2^6)
#endif

	HalUsbSetQueuePipeMapping8192CUsb(padapter,
				pdvobjpriv->RtNumInPipes, pdvobjpriv->RtNumOutPipes);

}

static u8 _InitPowerOn(PADAPTER padapter)
{
	u8 		status = _SUCCESS;
	u16			value16=0;
	u8			value8 = 0;

	// RSV_CTRL 0x1C[7:0] = 0x00			// unlock ISO/CLK/Power control register
	rtw_write8(padapter, REG_RSV_CTRL, 0x0);

	// HW Power on sequence
	if(!HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, rtl8723A_card_enable_flow ))
				return _FAIL;

	// 0x04[19] = 1, suggest by Jackie 2011.05.09, reset 8051
			value8 = rtw_read8(padapter, REG_APS_FSMCO+2);
			rtw_write8(padapter,REG_APS_FSMCO+2,(value8|BIT3));

			// Enable MAC DMA/WMAC/SCHEDULE/SEC block			
			// Set CR bit10 to enable 32k calibration. Suggested by SD1 Gimmy. Added by tynli. 2011.08.31.
			value16 = rtw_read16(padapter, REG_CR);
			value16 |= (HCI_TXDMA_EN | HCI_RXDMA_EN | TXDMA_EN | RXDMA_EN
						| PROTOCOL_EN | SCHEDULE_EN | MACTXEN | MACRXEN | ENSEC | CALTMR_EN);
			rtw_write16(padapter, REG_CR, value16);

	//for Efuse PG, suggest by Jackie 2011.11.23
	PHY_SetBBReg(padapter, REG_EFUSE_CTRL, BIT28|BIT29|BIT30, 0x06);

	return status;
}


static void _dbg_dump_macreg(_adapter *padapter)
{
	u32 offset = 0;
	u32 val32 = 0;
	u32 index =0 ;
	for(index=0;index<64;index++)
	{
		offset = index*4;
		val32 = rtw_read32(padapter,offset);
		DBG_8192C("offset : 0x%02x ,val:0x%08x\n",offset,val32);
	}
}

//-------------------------------------------------------------------------
//
// LLT R/W/Init function
//
//-------------------------------------------------------------------------
static u8 _LLTWrite(
	IN  PADAPTER	Adapter,
	IN	u32		address,
	IN	u32		data
	)
{
	u8	status = _SUCCESS;
	int 		count = 0;
	u32 		value = _LLT_INIT_ADDR(address) | _LLT_INIT_DATA(data) | _LLT_OP(_LLT_WRITE_ACCESS);

	rtw_write32(Adapter, REG_LLT_INIT, value);
	
	//polling
	do{
		
		value = rtw_read32(Adapter, REG_LLT_INIT);
		if(_LLT_NO_ACTIVE == _LLT_OP_VALUE(value)){
			break;
		}
		
		if(count > POLLING_LLT_THRESHOLD){
			//RT_TRACE(COMP_INIT,DBG_SERIOUS,("Failed to polling write LLT done at address %d!\n", address));
			status = _FAIL;
			break;
		}
	}while(count++);

	return status;
	
}


static u8 _LLTRead(
	IN  PADAPTER	Adapter,
	IN	u32		address
	)
{
	int		count = 0;
	u32		value = _LLT_INIT_ADDR(address) | _LLT_OP(_LLT_READ_ACCESS);

	rtw_write32(Adapter, REG_LLT_INIT, value);

	//polling and get value
	do{
		
		value = rtw_read32(Adapter, REG_LLT_INIT);
		if(_LLT_NO_ACTIVE == _LLT_OP_VALUE(value)){
			return (u8)value;
		}
		
		if(count > POLLING_LLT_THRESHOLD){
			//RT_TRACE(COMP_INIT,DBG_SERIOUS,("Failed to polling read LLT done at address %d!\n", address));
			break;
		}
	}while(count++);

	return 0xFF;

}


//---------------------------------------------------------------
//
//	MAC init functions
//
//---------------------------------------------------------------
static VOID
_SetMacID(
	IN  PADAPTER Adapter, u8* MacID
	)
{
	u32 i;
	for(i=0 ; i< MAC_ADDR_LEN ; i++){
#ifdef  CONFIG_CONCURRENT_MODE		
		if(Adapter->iface_type == IFACE_PORT1)
			rtw_write32(Adapter, REG_MACID1+i, MacID[i]);
		else
#endif			
		rtw_write32(Adapter, REG_MACID+i, MacID[i]);
	}
}

static VOID
_SetBSSID(
	IN  PADAPTER Adapter, u8* BSSID
	)
{
	u32 i;
	for(i=0 ; i< MAC_ADDR_LEN ; i++){
#ifdef  CONFIG_CONCURRENT_MODE		
		if(Adapter->iface_type == IFACE_PORT1)
			rtw_write32(Adapter, REG_BSSID1+i, BSSID[i]);
		else
#endif			
		rtw_write32(Adapter, REG_BSSID+i, BSSID[i]);
	}
}


// Shall USB interface init this?
static VOID
_InitInterrupt(
	IN  PADAPTER Adapter
	)
{
	u32	value32;

	// HISR - turn all on
	value32 = 0xFFFFFFFF;
	rtw_write32(Adapter, REG_HISR, value32);

	// HIMR - turn all on
	rtw_write32(Adapter, REG_HIMR, value32);
}


static VOID
_InitQueueReservedPage(
	IN  PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);
	struct registry_priv *pregistrypriv = &Adapter->registrypriv; 
	
	u32			outEPNum	= (u32)pHalData->OutEpNumber;
	u32			numHQ		= 0;
	u32			numLQ		= 0;
	u32			numNQ		= 0;
	u32			numPubQ;
	u32			value32;
	u8			value8;
	BOOLEAN		bWiFiConfig	= pregistrypriv->wifi_spec;
	//u32			txQPageNum, txQPageUnit,txQRemainPage;
	
	if( (pregistrypriv->wifi_spec==_TRUE)	 ||(pregistrypriv->qos_opt_enable==_TRUE))
		bWiFiConfig = _TRUE;

	{ //for WMM 
		//RT_ASSERT((outEPNum>=2), ("for WMM ,number of out-ep must more than or equal to 2!\n"));

		numPubQ = bWiFiConfig?WMM_NORMAL_PAGE_NUM_PUBQ:NORMAL_PAGE_NUM_PUBQ;

		if (pHalData->OutEpQueueSel & TX_SELE_HQ)
		{
			numHQ = bWiFiConfig?WMM_NORMAL_PAGE_NUM_HPQ:NORMAL_PAGE_NUM_HPQ;
		}

		if (pHalData->OutEpQueueSel & TX_SELE_LQ)
		{
			numLQ = bWiFiConfig?WMM_NORMAL_PAGE_NUM_LPQ:NORMAL_PAGE_NUM_LPQ;
		}
		// NOTE: This step shall be proceed before writting REG_RQPN.
		if(pHalData->OutEpQueueSel & TX_SELE_NQ){
			numNQ = bWiFiConfig?WMM_NORMAL_PAGE_NUM_NPQ:NORMAL_PAGE_NUM_NPQ;
		}
		value8 = (u8)_NPQ(numNQ);
		rtw_write8(Adapter, REG_RQPN_NPQ, value8);
	}

	// TX DMA
	value32 = _HPQ(numHQ) | _LPQ(numLQ) | _PUBQ(numPubQ) | LD_RQPN;	
	rtw_write32(Adapter, REG_RQPN, value32);	
}

static VOID
_InitTxBufferBoundary(
	IN  PADAPTER Adapter
	)
{	
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

	u8	txpktbuf_bndy; 

	if(!pregistrypriv->wifi_spec){
		txpktbuf_bndy = TX_PAGE_BOUNDARY;
	}
	else{//for WMM
		txpktbuf_bndy = WMM_NORMAL_TX_PAGE_BOUNDARY;
	}

	rtw_write8(Adapter, REG_TXPKTBUF_BCNQ_BDNY, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TXPKTBUF_MGQ_BDNY, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TXPKTBUF_WMAC_LBK_BF_HD, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TRXFF_BNDY, txpktbuf_bndy);	
#if 1
	rtw_write8(Adapter, REG_TDECTRL+1, txpktbuf_bndy);
#else
	txdmactrl = PlatformIORead2Byte(Adapter, REG_TDECTRL);
	txdmactrl &= ~BCN_HEAD_MASK;
	txdmactrl |= BCN_HEAD(txpktbuf_bndy);
	PlatformIOWrite2Byte(Adapter, REG_TDECTRL, txdmactrl);
#endif
}

static VOID
_InitPageBoundary(
	IN  PADAPTER Adapter
	)
{
	// RX Page Boundary
	//srand(static_cast<unsigned int>(time(NULL)) );
	u16 rxff_bndy = 0x27FF;//(rand() % 1) ? 0x27FF : 0x23FF;

	rtw_write16(Adapter, (REG_TRXFF_BNDY + 2), rxff_bndy);

	// TODO: ?? shall we set tx boundary?
}


static VOID
_InitNormalChipRegPriority(
	IN	PADAPTER	Adapter,
	IN	u16		beQ,
	IN	u16		bkQ,
	IN	u16		viQ,
	IN	u16		voQ,
	IN	u16		mgtQ,
	IN	u16		hiQ
	)
{
	u16 value16		= (rtw_read16(Adapter, REG_TRXDMA_CTRL) & 0x7);

	value16 |=	_TXDMA_BEQ_MAP(beQ) 	| _TXDMA_BKQ_MAP(bkQ) |
				_TXDMA_VIQ_MAP(viQ) 	| _TXDMA_VOQ_MAP(voQ) |
				_TXDMA_MGQ_MAP(mgtQ)| _TXDMA_HIQ_MAP(hiQ);
	
	rtw_write16(Adapter, REG_TRXDMA_CTRL, value16);
}

static VOID
_InitNormalChipOneOutEpPriority(
	IN	PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

	u16	value = 0;
	switch(pHalData->OutEpQueueSel)
	{
		case TX_SELE_HQ:
			value = QUEUE_HIGH;
			break;
		case TX_SELE_LQ:
			value = QUEUE_LOW;
			break;
		case TX_SELE_NQ:
			value = QUEUE_NORMAL;
			break;
		default:
			//RT_ASSERT(FALSE,("Shall not reach here!\n"));
			break;
	}
	
	_InitNormalChipRegPriority(Adapter,
								value,
								value,
								value,
								value,
								value,
								value
								);

}

static VOID
_InitNormalChipTwoOutEpPriority(
	IN	PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	u16			beQ,bkQ,viQ,voQ,mgtQ,hiQ;
	

	u16	valueHi = 0;
	u16	valueLow = 0;
	
	switch(pHalData->OutEpQueueSel)
	{
		case (TX_SELE_HQ | TX_SELE_LQ):
			valueHi = QUEUE_HIGH;
			valueLow = QUEUE_LOW;
			break;
		case (TX_SELE_NQ | TX_SELE_LQ):
			valueHi = QUEUE_NORMAL;
			valueLow = QUEUE_LOW;
			break;
		case (TX_SELE_HQ | TX_SELE_NQ):
			valueHi = QUEUE_HIGH;
			valueLow = QUEUE_NORMAL;
			break;
		default:
			//RT_ASSERT(FALSE,("Shall not reach here!\n"));
			break;
	}

	if(!pregistrypriv->wifi_spec ){
		beQ		= valueLow;
		bkQ		= valueLow;
		viQ		= valueHi;
		voQ		= valueHi;
		mgtQ	= valueHi; 
		hiQ		= valueHi;								
	}
	else{//for WMM ,CONFIG_OUT_EP_WIFI_MODE
		beQ		= valueLow;
		bkQ		= valueHi;		
		viQ		= valueHi;
		voQ		= valueLow;
		mgtQ	= valueHi;
		hiQ		= valueHi;							
	}
	
	_InitNormalChipRegPriority(Adapter,beQ,bkQ,viQ,voQ,mgtQ,hiQ);

}

static VOID
_InitNormalChipThreeOutEpPriority(
	IN	PADAPTER Adapter
	)
{
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	u16			beQ,bkQ,viQ,voQ,mgtQ,hiQ;

	if(!pregistrypriv->wifi_spec ){// typical setting
		beQ		= QUEUE_LOW;
		bkQ 		= QUEUE_LOW;
		viQ 		= QUEUE_NORMAL;
		voQ 		= QUEUE_HIGH;
		mgtQ 	= QUEUE_HIGH;
		hiQ 		= QUEUE_HIGH;			
	}
	else{// for WMM
		beQ		= QUEUE_LOW;
		bkQ 		= QUEUE_NORMAL;
		viQ 		= QUEUE_NORMAL;
		voQ 		= QUEUE_HIGH;
		mgtQ 	= QUEUE_HIGH;
		hiQ 		= QUEUE_HIGH;			
	}
	_InitNormalChipRegPriority(Adapter,beQ,bkQ,viQ,voQ,mgtQ,hiQ);
}

static VOID
_InitNormalChipQueuePriority(
	IN	PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

	switch(pHalData->OutEpNumber)
	{
		case 1:
			_InitNormalChipOneOutEpPriority(Adapter);
			break;
		case 2:
			_InitNormalChipTwoOutEpPriority(Adapter);
			break;
		case 3:
			_InitNormalChipThreeOutEpPriority(Adapter);
			break;
		default:
			//RT_ASSERT(FALSE,("Shall not reach here!\n"));
			break;
	}


}

static VOID
_InitQueuePriority(
	IN  PADAPTER Adapter
	)
{
	_InitNormalChipQueuePriority(Adapter);
}

static VOID
_InitHardwareDropIncorrectBulkOut(
	IN  PADAPTER Adapter
	)
{
#ifdef ENABLE_USB_DROP_INCORRECT_OUT
	u32	value32 = rtw_read32(Adapter, REG_TXDMA_OFFSET_CHK);
	value32 |= DROP_DATA_EN;
	rtw_write32(Adapter, REG_TXDMA_OFFSET_CHK, value32);
#endif
}

static VOID
_InitNetworkType(
	IN  PADAPTER Adapter
	)
{
	u32	value32;

	value32 = rtw_read32(Adapter, REG_CR);

	// TODO: use the other function to set network type
#if RTL8191C_FPGA_NETWORKTYPE_ADHOC
	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AD_HOC);
#else
	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AP);
#endif
	rtw_write32(Adapter, REG_CR, value32);
//	RASSERT(pIoBase->rtw_read8(REG_CR + 2) == 0x2);
}

static VOID
_InitTransferPageSize(
	IN  PADAPTER Adapter
	)
{
	// Tx page size is always 128.
	
	u8	value8;
	value8 = _PSRX(PBP_128) | _PSTX(PBP_128);
	rtw_write8(Adapter, REG_PBP, value8);
}

static VOID
_InitDriverInfoSize(
	IN  PADAPTER	Adapter,
	IN	u8		drvInfoSize
	)
{
	rtw_write8(Adapter,REG_RX_DRVINFO_SZ, drvInfoSize);
}

static VOID
_InitWMACSetting(
	IN  PADAPTER Adapter
	)
{
	//u4Byte			value32;
	//u16			value16;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	//pHalData->ReceiveConfig = AAP | APM | AM | AB | APP_ICV | ADF | AMF | APP_FCS | HTC_LOC_CTRL | APP_MIC | APP_PHYSTS;
	//pHalData->ReceiveConfig = RCR_AAP | RCR_APM | RCR_AM | RCR_AB |RCR_CBSSID_DATA| RCR_CBSSID_BCN| RCR_APP_ICV | RCR_AMF | RCR_HTC_LOC_CTRL | RCR_APP_MIC | RCR_APP_PHYSTS;
	 // don't turn on AAP, it will allow all packets to driver
        pHalData->ReceiveConfig = RCR_APM | RCR_AM | RCR_AB |RCR_CBSSID_DATA| RCR_CBSSID_BCN| RCR_APP_ICV | RCR_AMF | RCR_HTC_LOC_CTRL | RCR_APP_MIC | RCR_APP_PHYSTS;	 
#if (1 == RTL8192C_RX_PACKET_INCLUDE_CRC)
	pHalData->ReceiveConfig |= ACRC32;
#endif

	// some REG_RCR will be modified later by phy_ConfigMACWithHeaderFile()
	rtw_write32(Adapter, REG_RCR, pHalData->ReceiveConfig);

	// Accept all multicast address
	rtw_write32(Adapter, REG_MAR, 0xFFFFFFFF);
	rtw_write32(Adapter, REG_MAR + 4, 0xFFFFFFFF);


	// Accept all data frames
	//value16 = 0xFFFF;
	//rtw_write16(Adapter, REG_RXFLTMAP2, value16);

	// 2010.09.08 hpfan
	// Since ADF is removed from RCR, ps-poll will not be indicate to driver,
	// RxFilterMap should mask ps-poll to gurantee AP mode can rx ps-poll.
	//value16 = 0x400;
	//rtw_write16(Adapter, REG_RXFLTMAP1, value16);

	// Accept all management frames
	//value16 = 0xFFFF;
	//rtw_write16(Adapter, REG_RXFLTMAP0, value16);

	//enable RX_SHIFT bits
	//rtw_write8(Adapter, REG_TRXDMA_CTRL, rtw_read8(Adapter, REG_TRXDMA_CTRL)|BIT(1));	

}

static VOID
_InitAdaptiveCtrl(
	IN  PADAPTER Adapter
	)
{
	u16	value16;
	u32	value32;

	// Response Rate Set
	value32 = rtw_read32(Adapter, REG_RRSR);
	value32 &= ~RATE_BITMAP_ALL;
	value32 |= RATE_RRSR_CCK_ONLY_1M;
	rtw_write32(Adapter, REG_RRSR, value32);

	// CF-END Threshold
	//m_spIoBase->rtw_write8(REG_CFEND_TH, 0x1);

	// SIFS (used in NAV)
	value16 = _SPEC_SIFS_CCK(0x10) | _SPEC_SIFS_OFDM(0x10);
	rtw_write16(Adapter, REG_SPEC_SIFS, value16);

	// Retry Limit
	value16 = _LRL(0x30) | _SRL(0x30);
	rtw_write16(Adapter, REG_RL, value16);
	
}

static VOID
_InitRateFallback(
	IN  PADAPTER Adapter
	)
{
	// Set Data Auto Rate Fallback Retry Count register.
	rtw_write32(Adapter, REG_DARFRC, 0x00000000);
	rtw_write32(Adapter, REG_DARFRC+4, 0x10080404);
	rtw_write32(Adapter, REG_RARFRC, 0x04030201);
	rtw_write32(Adapter, REG_RARFRC+4, 0x08070605);

}


static VOID
_InitEDCA(
	IN  PADAPTER Adapter
	)
{
	// Set Spec SIFS (used in NAV)
	rtw_write16(Adapter,REG_SPEC_SIFS, 0x100a);
	rtw_write16(Adapter,REG_MAC_SPEC_SIFS, 0x100a);

	// Set SIFS for CCK
	rtw_write16(Adapter,REG_SIFS_CTX, 0x100a);	

	// Set SIFS for OFDM
	rtw_write16(Adapter,REG_SIFS_TRX, 0x100a);

	// TXOP
	rtw_write32(Adapter, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtw_write32(Adapter, REG_EDCA_BK_PARAM, 0x0000A44F);
	rtw_write32(Adapter, REG_EDCA_VI_PARAM, 0x005EA324);
	rtw_write32(Adapter, REG_EDCA_VO_PARAM, 0x002FA226);
}

#ifdef CONFIG_LED
static void _InitHWLed(PADAPTER Adapter)
{
	struct led_priv *pledpriv = &(Adapter->ledpriv);
	
	if( pledpriv->LedStrategy != HW_LED)
		return;
	
// HW led control
// to do .... 
//must consider cases of antenna diversity/ commbo card/solo card/mini card

}
#endif //CONFIG_LED

static VOID
_InitRDGSetting(
	IN	PADAPTER Adapter
	)
{
	rtw_write8(Adapter,REG_RD_CTRL,0xFF);
	rtw_write16(Adapter, REG_RD_NAV_NXT, 0x200);
	rtw_write8(Adapter,REG_RD_RESP_PKT_TH,0x05);
}

static VOID
_InitRxSetting(
	IN	PADAPTER Adapter
	)
{
	rtw_write32(Adapter, REG_MACID, 0x87654321);
	rtw_write32(Adapter, 0x0700, 0x87654321);
}

static VOID
_InitRetryFunction(
	IN  PADAPTER Adapter
	)
{
	u8	value8;
	
	value8 = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL);
	value8 |= EN_AMPDU_RTY_NEW;
	rtw_write8(Adapter, REG_FWHW_TXQ_CTRL, value8);

	// Set ACK timeout
	rtw_write8(Adapter, REG_ACKTO, 0x40);
}

/*-----------------------------------------------------------------------------
 * Function:	usb_AggSettingTxUpdate()
 *
 * Overview:	Seperate TX/RX parameters update independent for TP detection and 
 *			dynamic TX/RX aggreagtion parameters update.
 *
 * Input:			PADAPTER
 *
 * Output/Return:	NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	12/10/2010	MHC		Seperate to smaller function.
 *
 *---------------------------------------------------------------------------*/
static VOID
usb_AggSettingTxUpdate(
	IN	PADAPTER			Adapter
	)
{
#ifdef CONFIG_USB_TX_AGGREGATION
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	//PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);	
	u32			value32;

	if(Adapter->registrypriv.wifi_spec)
		pHalData->UsbTxAggMode = _FALSE;

	if(pHalData->UsbTxAggMode){
		value32 = rtw_read32(Adapter, REG_TDECTRL);
		value32 = value32 & ~(BLK_DESC_NUM_MASK << BLK_DESC_NUM_SHIFT);
		value32 |= ((pHalData->UsbTxAggDescNum & BLK_DESC_NUM_MASK) << BLK_DESC_NUM_SHIFT);
		
		rtw_write32(Adapter, REG_TDECTRL, value32);
	}
	
#endif
}	// usb_AggSettingTxUpdate


/*-----------------------------------------------------------------------------
 * Function:	usb_AggSettingRxUpdate()
 *
 * Overview:	Seperate TX/RX parameters update independent for TP detection and 
 *			dynamic TX/RX aggreagtion parameters update.
 *
 * Input:			PADAPTER
 *
 * Output/Return:	NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	12/10/2010	MHC		Seperate to smaller function.
 *
 *---------------------------------------------------------------------------*/
static VOID
usb_AggSettingRxUpdate(
	IN	PADAPTER			Adapter
	)
{
#ifdef CONFIG_USB_RX_AGGREGATION
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	//PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);
	u8			valueDMA;
	u8			valueUSB;

	valueDMA = rtw_read8(Adapter, REG_TRXDMA_CTRL);
	valueUSB = rtw_read8(Adapter, REG_USB_SPECIAL_OPTION);

	switch(pHalData->UsbRxAggMode)
	{
		case USB_RX_AGG_DMA:
			valueDMA |= RXDMA_AGG_EN;
			valueUSB &= ~USB_AGG_EN;
			break;
		case USB_RX_AGG_USB:
			valueDMA &= ~RXDMA_AGG_EN;
			valueUSB |= USB_AGG_EN;
			break;
		case USB_RX_AGG_MIX:
			valueDMA |= RXDMA_AGG_EN;
			valueUSB |= USB_AGG_EN;
			break;
		case USB_RX_AGG_DISABLE:
		default:
			valueDMA &= ~RXDMA_AGG_EN;
			valueUSB &= ~USB_AGG_EN;
			break;
	}

	rtw_write8(Adapter, REG_TRXDMA_CTRL, valueDMA);
	rtw_write8(Adapter, REG_USB_SPECIAL_OPTION, valueUSB);

	switch(pHalData->UsbRxAggMode)
	{
		case USB_RX_AGG_DMA:
			rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH, pHalData->UsbRxAggPageCount);
			rtw_write8(Adapter, REG_USB_DMA_AGG_TO, pHalData->UsbRxAggPageTimeout);
			break;
		case USB_RX_AGG_USB:
			rtw_write8(Adapter, REG_USB_AGG_TH, pHalData->UsbRxAggBlockCount);
			rtw_write8(Adapter, REG_USB_AGG_TO, pHalData->UsbRxAggBlockTimeout);
			break;
		case USB_RX_AGG_MIX:
			rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH, pHalData->UsbRxAggPageCount);
			rtw_write8(Adapter, REG_USB_DMA_AGG_TO, pHalData->UsbRxAggPageTimeout);
			rtw_write8(Adapter, REG_USB_AGG_TH, pHalData->UsbRxAggBlockCount);
			rtw_write8(Adapter, REG_USB_AGG_TO, pHalData->UsbRxAggBlockTimeout);
			break;
		case USB_RX_AGG_DISABLE:
		default:
			// TODO: 
			break;
	}

	switch(PBP_128)
	{
		case PBP_128:
			pHalData->HwRxPageSize = 128;
			break;
		case PBP_64:
			pHalData->HwRxPageSize = 64;
			break;
		case PBP_256:
			pHalData->HwRxPageSize = 256;
			break;
		case PBP_512:
			pHalData->HwRxPageSize = 512;
			break;
		case PBP_1024:
			pHalData->HwRxPageSize = 1024;
			break;
		default:
			//RT_ASSERT(FALSE, ("RX_PAGE_SIZE_REG_VALUE definition is incorrect!\n"));
			break;
	}
#endif
}	// usb_AggSettingRxUpdate

static VOID
InitUsbAggregationSetting(
	IN  PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	// Tx aggregation setting
	usb_AggSettingTxUpdate(Adapter);

	// Rx aggregation setting
	usb_AggSettingRxUpdate(Adapter);

	// 201/12/10 MH Add for USB agg mode dynamic switch.
	pHalData->UsbRxHighSpeedMode = _FALSE;
}

/*-----------------------------------------------------------------------------
 * Function:	USB_AggModeSwitch()
 *
 * Overview:	When RX traffic is more than 40M, we need to adjust some parameters to increase
 *			RX speed by increasing batch indication size. This will decrease TCP ACK speed, we
 *			need to monitor the influence of FTP/network share.
 *			For TX mode, we are still ubder investigation.
 *
 * Input:		PADAPTER
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	12/10/2010	MHC		Create Version 0.  
 *
 *---------------------------------------------------------------------------*/
VOID
USB_AggModeSwitch(
	IN	PADAPTER			Adapter
	)
{
#if 0
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);

	//pHalData->UsbRxHighSpeedMode = FALSE;
	// How to measure the RX speed? We assume that when traffic is more than 
	if (pMgntInfo->bRegAggDMEnable == _FALSE)
	{
		return;	// Inf not support.
	}
	
	
	if (pmlmepriv->LinkDetectInfo.bHigherBusyTraffic == _TRUE && 
		pHalData->UsbRxHighSpeedMode == _FALSE)
	{
		pHalData->UsbRxHighSpeedMode = _TRUE;
		DBG_8192C("UsbAggModeSwitchCheck to HIGH\n");
	}
	else if (pmlmepriv->LinkDetectInfo.bHigherBusyTraffic == _FALSE && 
		pHalData->UsbRxHighSpeedMode == _TRUE)
	{
		pHalData->UsbRxHighSpeedMode = _FALSE;
		DBG_8192C("UsbAggModeSwitchCheck to LOW\n");
	}
	else
	{
		return; 
	}
	
	// 2010/12/10 MH Add for USB Aggregation judgement we need to 
	//if( pMgntInfo->LinkDetectInfo.NumRxOkInPeriod > 4000 ||
		//			pMgntInfo->LinkDetectInfo.NumTxOkInPeriod > 4000 )

#ifdef CONFIG_USB_TX_AGGREGATION
	//usb_AggSettingTxUpdate(Adapter);
#endif
		
#ifdef CONFIG_USB_RX_AGGREGATION
	if (pHalData->UsbRxHighSpeedMode == _TRUE)	
	{
		// 2010/12/10 MH The parameter is tested by SD1 engineer and SD3 channel emulator.
		// USB mode
		pHalData->UsbRxAggBlockCount		= 40;
		pHalData->UsbRxAggBlockTimeout	= 5;
		// Mix mode
		pHalData->UsbRxAggPageCount		= 72;
		pHalData->UsbRxAggPageTimeout	= 6;		
	}
	else
	{
		// USB mode
		pHalData->UsbRxAggBlockCount		= pMgntInfo->RegUsbRxAggBlockCount;
		pHalData->UsbRxAggBlockTimeout	= pMgntInfo->RegUsbRxAggBlockTimeout;	
		// Mix mode
		pHalData->UsbRxAggPageCount		= pMgntInfo->RegUsbRxAggPageCount;
		pHalData->UsbRxAggPageTimeout	= pMgntInfo->RegUsbRxAggPageTimeout;	
	}	
#endif
#endif
}	// USB_AggModeSwitch

static VOID
_InitOperationMode(
	IN	PADAPTER			Adapter
	)
{
#if 0//gtest
	PHAL_DATA_8192CUSB	pHalData = GetHalData8192CUsb(Adapter);
	u1Byte				regBwOpMode = 0;
	u4Byte				regRATR = 0, regRRSR = 0;


	//1 This part need to modified according to the rate set we filtered!!
	//
	// Set RRSR, RATR, and REG_BWOPMODE registers
	//
	switch(Adapter->RegWirelessMode)
	{
		case WIRELESS_MODE_B:
			regBwOpMode = BW_OPMODE_20MHZ;
			regRATR = RATE_ALL_CCK;
			regRRSR = RATE_ALL_CCK;
			break;
		case WIRELESS_MODE_A:
			ASSERT(FALSE);
#if 0
			regBwOpMode = BW_OPMODE_5G |BW_OPMODE_20MHZ;
			regRATR = RATE_ALL_OFDM_AG;
			regRRSR = RATE_ALL_OFDM_AG;
#endif
			break;
		case WIRELESS_MODE_G:
			regBwOpMode = BW_OPMODE_20MHZ;
			regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
			regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
			break;
		case WIRELESS_MODE_AUTO:
			if (Adapter->bInHctTest)
			{
			    regBwOpMode = BW_OPMODE_20MHZ;
			    regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
			    regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
			}
			else
			{
			    regBwOpMode = BW_OPMODE_20MHZ;
			    regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
			    regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
			}
			break;
		case WIRELESS_MODE_N_24G:
			// It support CCK rate by default.
			// CCK rate will be filtered out only when associated AP does not support it.
			regBwOpMode = BW_OPMODE_20MHZ;
				regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
				regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
			break;
		case WIRELESS_MODE_N_5G:
			ASSERT(FALSE);
#if 0
			regBwOpMode = BW_OPMODE_5G;
			regRATR = RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
			regRRSR = RATE_ALL_OFDM_AG;
#endif
			break;
	}

	// Ziv ????????
	//PlatformEFIOWrite4Byte(Adapter, REG_INIRTS_RATE_SEL, regRRSR);
	rtw_write8(Adapter, REG_BWOPMODE, regBwOpMode);

	// For Min Spacing configuration.
	switch(pHalData->RF_Type)
	{
		case RF_1T2R:
		case RF_1T1R:
			RT_TRACE(COMP_INIT, DBG_LOUD, ("Initializeadapter: RF_Type%s\n", (pHalData->RF_Type==RF_1T1R? "(1T1R)":"(1T2R)")));
			Adapter->MgntInfo.MinSpaceCfg = (MAX_MSS_DENSITY_1T<<3);						
			break;
		case RF_2T2R:
		case RF_2T2R_GREEN:
			RT_TRACE(COMP_INIT, DBG_LOUD, ("Initializeadapter:RF_Type(2T2R)\n"));
			Adapter->MgntInfo.MinSpaceCfg = (MAX_MSS_DENSITY_2T<<3);			
			break;
	}
	
	rtw_write8(Adapter, REG_AMPDU_MIN_SPACE, Adapter->MgntInfo.MinSpaceCfg);
#endif
}

static VOID
_InitRFType(
	IN	PADAPTER Adapter
	)
{
	struct registry_priv	 *pregpriv = &Adapter->registrypriv;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);
	BOOLEAN			is92CU		= IS_92C_SERIAL(pHalData->VersionID);

#if	DISABLE_BB_RF
	pHalData->rf_chip	= RF_PSEUDO_11N;
	return;
#endif

	pHalData->rf_chip	= RF_6052;

	if(_FALSE == is92CU){
		pHalData->rf_type = RF_1T1R;
		DBG_8192C("Set RF Chip ID to RF_6052 and RF type to 1T1R.\n");
		return;
	}

	// TODO: Consider that EEPROM set 92CU to 1T1R later.
	// Force to overwrite setting according to chip version. Ignore EEPROM setting.
	//pHalData->RF_Type = is92CU ? RF_2T2R : RF_1T1R;
	MSG_8192C("Set RF Chip ID to RF_6052 and RF type to %d.\n", pHalData->rf_type);

}

static VOID _InitAdhocWorkaroundParams(IN PADAPTER Adapter)
{
#ifdef RTL8192CU_ADHOC_WORKAROUND_SETTING
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);	
	pHalData->RegBcnCtrlVal = rtw_read8(Adapter, REG_BCN_CTRL);
	pHalData->RegTxPause = rtw_read8(Adapter, REG_TXPAUSE); 
	pHalData->RegFwHwTxQCtrl = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL+2);
	pHalData->RegReg542 = rtw_read8(Adapter, REG_TBTT_PROHIBIT+2);
#endif	
}

// Set CCK and OFDM Block "ON"
static VOID _BBTurnOnBlock(
	IN	PADAPTER		Adapter
	)
{
#if (DISABLE_BB_RF)
	return;
#endif

	PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bCCKEn, 0x1);
	PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bOFDMEn, 0x1);
}

#define MgntActSet_RF_State(...)
static void _RfPowerSave(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	struct pwrctrl_priv *pwrctrl;
	rt_rf_power_state eRfPowerStateToSet;
	u8 u1bTmp;


#if (DISABLE_BB_RF)
	return;
#endif

	pHalData = GET_HAL_DATA(padapter);
	pwrctrl = adapter_to_pwrctl(padapter);

	//
	// 2010/08/11 MH Merge from 8192SE for Minicard init. We need to confirm current radio status
	// and then decide to enable RF or not.!!!??? For Selective suspend mode. We may not 
	// call init_adapter. May cause some problem??
	//
	// Fix the bug that Hw/Sw radio off before S3/S4, the RF off action will not be executed 
	// in MgntActSet_RF_State() after wake up, because the value of pHalData->eRFPowerState 
	// is the same as eRfOff, we should change it to eRfOn after we config RF parameters.
	// Added by tynli. 2010.03.30.
	pwrctrl->rf_pwrstate = rf_on;
	RT_CLEAR_PS_LEVEL(pwrctrl, RT_RF_OFF_LEVL_HALT_NIC);
	//Added by chiyokolin, 2011.10.12 for Tx
	rtw_write8(padapter, REG_TXPAUSE, 0x00);

	// 20100326 Joseph: Copy from GPIOChangeRFWorkItemCallBack() function to check HW radio on/off.
	// 20100329 Joseph: Revise and integrate the HW/SW radio off code in initialization.
#if 1
	pwrctrl->b_hw_radio_off = _FALSE;
	eRfPowerStateToSet = rf_on;
#else
	eRfPowerStateToSet = (rt_rf_power_state) RfOnOffDetect(padapter);
	pwrctrl->rfoff_reason |= eRfPowerStateToSet==rf_on ? RF_CHANGE_BY_INIT : RF_CHANGE_BY_HW;
	pwrctrl->rfoff_reason |= (pwrctrl->reg_rfoff) ? RF_CHANGE_BY_SW : 0;

	if (pwrctrl->rfoff_reason & RF_CHANGE_BY_HW)
		pwrctrl->b_hw_radio_off = _TRUE;

	if (pwrctrl->reg_rfoff == _TRUE)
	{
		// User disable RF via registry.
		RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("%s: Turn off RF for RegRfOff\n", __FUNCTION__));
		MgntActSet_RF_State(padapter, rf_off, RF_CHANGE_BY_SW, _TRUE);

//		if (padapter->bSlaveOfDMSP)
//			return;
	}
	else if (pwrctrl->rfoff_reason > RF_CHANGE_BY_PS)
	{
		// H/W or S/W RF OFF before sleep.
		RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("InitializeAdapter8192CUsb(): Turn off RF for RfOffReason(%ld).\n", pMgntInfo->RfOffReason));
		MgntActSet_RF_State(padapter, rf_off, pwrctrl->rfoff_reason, _TRUE);
	}
	else
	{
		// Perform GPIO polling to find out current RF state. added by Roger, 2010.04.09.
#if 0
//		if (RT_GetInterfaceSelection(padapter)==INTF_SEL2_MINICARD &&
		if ((pHalData->BoardType == BOARD_MINICARD) &&
			(padapter->MgntInfo.PowerSaveControl.bGpioRfSw))
		{
			RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("InitializeAdapter8192CU(): RF=%d \n", eRfPowerStateToSet));
			if (eRfPowerStateToSet == rf_off)
			{
				MgntActSet_RF_State(padapter, rf_off, RF_CHANGE_BY_HW, _TRUE);
				ppwrctrl->b_hw_radio_off = _TRUE;
			}
			else
			{
				ppwrctrl->rf_pwrstate = rf_off;
				ppwrctrl->rfoff_reason = RF_CHANGE_BY_INIT; 
				ppwrctrl->b_hw_radio_off = _FALSE;
				MgntActSet_RF_State(padapter, rf_on, ppwrctrl->rfoff_reason, _TRUE);
			}
		}
		else
#endif
		{
			pwrctrl->rf_pwrstate = rf_off;
			pwrctrl->rfoff_reason = RF_CHANGE_BY_INIT;
			MgntActSet_RF_State(padapter, rf_on, pwrctrl->rfoff_reason, _TRUE);
		}

		pwrctrl->rfoff_reason = 0; 
		pwrctrl->b_hw_radio_off = _FALSE;
		pwrctrl->rf_pwrstate = rf_on;
		if (padapter->ledpriv.LedControlHandler)
			padapter->ledpriv.LedControlHandler(padapter, LED_CTL_POWER_ON);
	}
#endif
	// 2010/-8/09 MH For power down module, we need to enable register block contrl reg at 0x1c.
	// Then enable power down control bit of register 0x04 BIT4 and BIT15 as 1.
	if (pHalData->pwrdown && eRfPowerStateToSet == rf_off)
	{
		DBG_871X("%s pwrdown\n", __FUNCTION__);
		// Enable register area 0x0-0xc.
		rtw_write8(padapter, REG_RSV_CTRL, 0x0);

		//
		// <Roger_Notes> We should configure HW PDn source for WiFi ONLY, and then
		// our HW will be set in power-down mode if PDn source from all  functions are configured.
		// 2010.10.06.
		//
		u1bTmp = rtw_read8(padapter, REG_MULTI_FUNC_CTRL);
		u1bTmp |= WL_HWPDN_EN;
		rtw_write8(padapter, REG_MULTI_FUNC_CTRL, u1bTmp);
	}
}

enum {
	Antenna_Lfet = 1,
	Antenna_Right = 2,	
};
			
//
// 2010/08/09 MH Add for power down check.
//
static BOOLEAN
HalDetectPwrDownMode(
	IN PADAPTER				Adapter
	)
{
	u8	tmpvalue;
	HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(Adapter);
	struct pwrctrl_priv		*pwrctrlpriv = adapter_to_pwrctl(Adapter);
	
	EFUSE_ShadowRead(Adapter, 1, EEPROM_RF_OPT3, (u32 *)&tmpvalue);

	// 2010/08/25 MH INF priority > PDN Efuse value.
	if(tmpvalue & BIT4 && pwrctrlpriv->reg_pdnmode)
	{
		pHalData->pwrdown = _TRUE;
	}
	else
	{
		pHalData->pwrdown = _FALSE;
	}

	DBG_8192C("HalDetectPwrDownMode(): PDN=%d\n", pHalData->pwrdown);
	return pHalData->pwrdown;
		
}	// HalDetectPwrDownMode


//
// 2010/08/26 MH Add for selective suspend mode check.
// If Efuse 0x0e bit1 is not enabled, we can not support selective suspend for Minicard and
// slim card.
//
static VOID
HalDetectSelectiveSuspendMode(
	IN PADAPTER				Adapter
	)
{
	u8	tmpvalue;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(Adapter);

	// If support HW radio detect, we need to enable WOL ability, otherwise, we 
	// can not use FW to notify host the power state switch.
	
	EFUSE_ShadowRead(Adapter, 1, EEPROM_USB_OPTIONAL1, (u32 *)&tmpvalue);

	DBG_8192C("HalDetectSelectiveSuspendMode(): SS ");
	if(tmpvalue & BIT1)
	{
		DBG_8192C("Enable\n");
	}
	else
	{
		DBG_8192C("Disable\n");
		pdvobjpriv->RegUsbSS = _FALSE;
	}

	// 2010/09/01 MH According to Dongle Selective Suspend INF. We can switch SS mode.
	if (pdvobjpriv->RegUsbSS && !SUPPORT_HW_RADIO_DETECT(pHalData))
	{
		//PMGNT_INFO				pMgntInfo = &(Adapter->MgntInfo);

		//if (!pMgntInfo->bRegDongleSS)	
		//{
		//	RT_TRACE(COMP_INIT, DBG_LOUD, ("Dongle disable SS\n"));
			pdvobjpriv->RegUsbSS = _FALSE;
		//}
	}
}	// HalDetectSelectiveSuspendMode
/*-----------------------------------------------------------------------------
 * Function:	HwSuspendModeEnable92Cu()
 *
 * Overview:	HW suspend mode switch.
 *
 * Input:		NONE
 *
 * Output:	NONE
 *
 * Return:	NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	08/23/2010	MHC		HW suspend mode switch test..
 *---------------------------------------------------------------------------*/
static VOID 
HwSuspendModeEnable92Cu(
	IN	PADAPTER	pAdapter,
	IN	u8			Type
	)
{
	//PRT_USB_DEVICE 		pDevice = GET_RT_USB_DEVICE(pAdapter);
	u16	reg = rtw_read16(pAdapter, REG_GPIO_MUXCFG);	

	//if (!pDevice->RegUsbSS)
	{
		return;
	}

	//
	// 2010/08/23 MH According to Alfred's suggestion, we need to to prevent HW
	// to enter suspend mode automatically. Otherwise, it will shut down major power 
	// domain and 8051 will stop. When we try to enter selective suspend mode, we
	// need to prevent HW to enter D2 mode aumotmatically. Another way, Host will
	// issue a S10 signal to power domain. Then it will cleat SIC setting(from Yngli).
	// We need to enable HW suspend mode when enter S3/S4 or disable. We need 
	// to disable HW suspend mode for IPS/radio_off.
	//
	//RT_TRACE(COMP_RF, DBG_LOUD, ("HwSuspendModeEnable92Cu = %d\n", Type));
	if (Type == _FALSE)
	{
		reg |= BIT14;
		//RT_TRACE(COMP_RF, DBG_LOUD, ("REG_GPIO_MUXCFG = %x\n", reg));
		rtw_write16(pAdapter, REG_GPIO_MUXCFG, reg);
		reg |= BIT12;
		//RT_TRACE(COMP_RF, DBG_LOUD, ("REG_GPIO_MUXCFG = %x\n", reg));
		rtw_write16(pAdapter, REG_GPIO_MUXCFG, reg);
	}
	else
	{
		reg &= (~BIT12);
		rtw_write16(pAdapter, REG_GPIO_MUXCFG, reg);
		reg &= (~BIT14);
		rtw_write16(pAdapter, REG_GPIO_MUXCFG, reg);
	}
	
}	// HwSuspendModeEnable92Cu
rt_rf_power_state RfOnOffDetect(IN	PADAPTER pAdapter )
{
	//HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(pAdapter);
	u8	val8;
	rt_rf_power_state rfpowerstate = rf_off;

	if(adapter_to_pwrctl(pAdapter)->bHWPowerdown)
	{
		val8 = rtw_read8(pAdapter, REG_HSISR);
		DBG_8192C("pwrdown, 0x5c(BIT7)=%02x\n", val8);
		rfpowerstate = (val8 & BIT7) ? rf_off: rf_on;				
	}
	else // rf on/off
	{
		rtw_write8(	pAdapter, REG_MAC_PINMUX_CFG,rtw_read8(pAdapter, REG_MAC_PINMUX_CFG)&~(BIT3));
		val8 = rtw_read8(pAdapter, REG_GPIO_IO_SEL);
		DBG_8192C("GPIO_IN=%02x\n", val8);
		rfpowerstate = (val8 & BIT3) ? rf_on : rf_off;	
	}
	return rfpowerstate;
}	// HalDetectPwrDownMode

void _ps_open_RF(_adapter *padapter);

u32 rtl8723au_hal_init(PADAPTER Adapter)
{
	u8	val8 = 0;
	u32	boundary, status = _SUCCESS;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	struct pwrctrl_priv		*pwrctrlpriv = adapter_to_pwrctl(Adapter);
	struct registry_priv	*pregistrypriv = &Adapter->registrypriv; 
	u8	is92C = IS_92C_SERIAL(pHalData->VersionID);
	rt_rf_power_state		eRfPowerStateToSet;
	u32 NavUpper = WiFiNavUpperUs;

	u32 init_start_time = rtw_get_current_time();


#ifdef DBG_HAL_INIT_PROFILING

	enum HAL_INIT_STAGES {
		HAL_INIT_STAGES_BEGIN = 0,
		HAL_INIT_STAGES_INIT_PW_ON,
		HAL_INIT_STAGES_INIT_LLTT,
		HAL_INIT_STAGES_MISC01,
		HAL_INIT_STAGES_DOWNLOAD_FW,
		HAL_INIT_STAGES_MAC,
		HAL_INIT_STAGES_BB,
		HAL_INIT_STAGES_RF,
		HAL_INIT_STAGES_MISC02,
		HAL_INIT_STAGES_TURN_ON_BLOCK,
		HAL_INIT_STAGES_INIT_SECURITY,
		HAL_INIT_STAGES_MISC11,
		//HAL_INIT_STAGES_RF_PS,
		HAL_INIT_STAGES_IQK,
		HAL_INIT_STAGES_PW_TRACK,
		HAL_INIT_STAGES_LCK,
		HAL_INIT_STAGES_MISC21,
		//HAL_INIT_STAGES_INIT_PABIAS,
		#ifdef CONFIG_BT_COEXIST
		HAL_INIT_STAGES_BT_COEXIST,
		#endif
		//HAL_INIT_STAGES_ANTENNA_SEL,
		HAL_INIT_STAGES_INIT_HAL_DM,
		HAL_INIT_STAGES_MISC31,
		HAL_INIT_STAGES_END,
		HAL_INIT_STAGES_NUM
	};

	char * hal_init_stages_str[] = {
		"HAL_INIT_STAGES_BEGIN",
		"HAL_INIT_STAGES_INIT_PW_ON",
		"HAL_INIT_STAGES_INIT_LLTT",
		"HAL_INIT_STAGES_MISC01",
		"HAL_INIT_STAGES_DOWNLOAD_FW",
		"HAL_INIT_STAGES_MAC",
		"HAL_INIT_STAGES_BB",
		"HAL_INIT_STAGES_RF",
		"HAL_INIT_STAGES_MISC02",
		"HAL_INIT_STAGES_TURN_ON_BLOCK",
		"HAL_INIT_STAGES_INIT_SECURITY",
		"HAL_INIT_STAGES_MISC11",
		//"HAL_INIT_STAGES_RF_PS",
		"HAL_INIT_STAGES_IQK",
		"HAL_INIT_STAGES_PW_TRACK",
		"HAL_INIT_STAGES_LCK",
		"HAL_INIT_STAGES_MISC21",
		//"HAL_INIT_STAGES_INIT_PABIAS",
		#ifdef CONFIG_BT_COEXIST
		"HAL_INIT_STAGES_BT_COEXIST",
		#endif
		//"HAL_INIT_STAGES_ANTENNA_SEL",
		"HAL_INIT_STAGES_INIT_HAL_DM",
		"HAL_INIT_STAGES_MISC31",
		"HAL_INIT_STAGES_END",
	};

	int hal_init_profiling_i;
	u32 hal_init_stages_timestamp[HAL_INIT_STAGES_NUM]; //used to record the time of each stage's starting point

	for(hal_init_profiling_i=0;hal_init_profiling_i<HAL_INIT_STAGES_NUM;hal_init_profiling_i++)
		hal_init_stages_timestamp[hal_init_profiling_i]=0;

	#define HAL_INIT_PROFILE_TAG(stage) hal_init_stages_timestamp[(stage)]=rtw_get_current_time();
#else
	#define HAL_INIT_PROFILE_TAG(stage) do {} while(0)
#endif //DBG_HAL_INIT_PROFILING



_func_enter_;

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_BEGIN);
	if(pwrctrlpriv->bkeepfwalive)
	{
		_ps_open_RF(Adapter);

		if(pHalData->bIQKInitialized ){
			rtl8192c_PHY_IQCalibrate(Adapter,_TRUE);
		}
		else{
			rtl8192c_PHY_IQCalibrate(Adapter,_FALSE);
			pHalData->bIQKInitialized = _TRUE;
		}
		rtl8192c_odm_CheckTXPowerTracking(Adapter);
		rtl8192c_PHY_LCCalibrate(Adapter);

		goto exit;
	}

//	pHalData->bMACFuncEnable = _FALSE;
	// Check if MAC has already power on. by tynli. 2011.05.27.
	val8 = rtw_read8(Adapter, REG_CR);
	RT_TRACE(_module_hci_hal_init_c_, _drv_info_,
			 ("%s: REG_CR 0x100=0x%02x\n", __FUNCTION__, val8));
	//Fix 92DU-VC S3 hang with the reason is that secondary mac is not initialized.
	//0x100 value of first mac is 0xEA while 0x100 value of secondary is 0x00
	//by sherry 20111102
	if (val8 == 0xEA) {
		pHalData->bMACFuncEnable = _FALSE;
	} else {
		pHalData->bMACFuncEnable = _TRUE;
		RT_TRACE(_module_hci_hal_init_c_, _drv_info_,
				 ("%s: MAC has already power on\n", __FUNCTION__));
	}

#if 0
	if(bSupportRemoteWakeUp && Adapter->bWakeFromPnpSleep && pHalData->bMACFuncEnable)
	{
		if(IS_HARDWARE_TYPE_8723AU(Adapter))
		{
			FW_CTRL_PS_MODE FwPwrMode = FW_PS_ACTIVE_MODE;
			
			pHalData->H2CStopInsertQueue = FALSE;
			
			if(pMgntInfo->mAssoc && 
				pMgntInfo->OpMode == RT_OP_MODE_INFRASTRUCTURE &&
				(pMgntInfo->PowerSaveControl.WoWLANLPSLevel > 0))
			{
				RT_TRACE(COMP_POWER, DBG_LOUD, ("FwLPS: Active!!\n"));	
				rtw_hal_set_hwreg(Adapter, HW_VAR_H2C_FW_PWRMODE, (pu1Byte)(&FwPwrMode));
			}
			
			HalSetFWWoWlanMode92C(Adapter, FALSE);

			// Clear WoWLAN event FTISR[WWLAN_INT_EN].
			if(IS_FW_8723A(Adapter) && Adapter->MgntInfo.FirmwareVersion < 10)
			{
				// Fw revises the bug after version 10.
				value8 = rtw_read8(Adapter, REG_FTISR+3);
				rtw_write8(Adapter, REG_FTISR+3, value8|BIT2);
			}

			SimpleInitializeAdapter8192CUsb(Adapter);

			pMgntInfo->init_adpt_in_progress = FALSE;
			return RT_STATUS_SUCCESS;
		}
	}
#endif


HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_PW_ON);
	status = _InitPowerOn(Adapter);
	if(status == _FAIL){
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init power on!\n"));
		goto exit;
	}

#ifdef CONFIG_BT_COEXIST
	//
	// 2010/09/23 MH Accordgin to Alfred's siggestion. we need to enable SIC to prevent HW
	// to enter suspend mode automatically. If host does not send SOF every 3ms. Or under DTM
	// test with rtl8188cu selective suspend enabler filter driver, WIN host will trigger the device to
	// enter suspend mode after some test (unknow reason now). We need to prevent the case otherwise
	// the register will be 0xea and all TX/RX path stop accidently.
	//
	//
	// 2010/10/01 MH If the OS is XP, host will trigger USB device to enter D3 mode. In CU HW design
	// it will enter suspend mode automatically. In slim combo card, the BT clock will be cut off if HW
	// enter suspend mode. We need to seperate differet case.
	//
	if (BT_IsBtExist(Adapter))
	{
#if 0
#if OS_WIN_FROM_VISTA(OS_VERSION)
		RT_TRACE(COMP_INIT, DBG_LOUD, ("Slim_combo win7/vista need not enable SIC\n"));
#else
		RT_TRACE(COMP_INIT, DBG_LOUD, ("Slim_combo XP enable SIC\n"));
		// 2010/10/15 MH According to Alfre's description, e need to enable bit14 at first an then enable bit12.
		// Otherwise, HW will enter debug mode and 8051 can not work. We need to stay at test mode to enable SIC.
		rtw_write16(Adapter, REG_GPIO_MUXCFG, rtw_read16(Adapter, REG_GPIO_MUXCFG)|BIT14);
		rtw_write16(Adapter, REG_GPIO_MUXCFG, rtw_read16(Adapter, REG_GPIO_MUXCFG)|BIT12);
#endif
#endif
	}
#endif // CONFIG_BT_COEXIST

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_LLTT);
	if (!pregistrypriv->wifi_spec) {
		boundary = TX_PAGE_BOUNDARY;
	} else {
		// for WMM
		boundary = WMM_NORMAL_TX_PAGE_BOUNDARY;
	}

	if (!pHalData->bMACFuncEnable)
	{
		status =  InitLLTTable(Adapter, boundary);
		if(status == _FAIL){
			RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init LLT table\n"));
			goto exit;
		}
	}


HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC01);
	if(pHalData->bRDGEnable){
		_InitRDGSetting(Adapter);
	}


HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_DOWNLOAD_FW);
#if (1 == MP_DRIVER)
if (Adapter->registrypriv.mp_mode == 1)
{
	_InitRxSetting(Adapter);
	// Don't Download Firmware
	Adapter->bFWReady = _FALSE;
}
//else
#endif
{
	status = rtl8723a_FirmwareDownload(Adapter);
	if(status != _SUCCESS)
	{
		Adapter->bFWReady = _FALSE;
		pHalData->fw_ractrl = _FALSE;
		DBG_8192C("fw download fail!\n");
		goto exit;
	}	
	else
	{
		Adapter->bFWReady = _TRUE;
		pHalData->fw_ractrl = _TRUE;
		DBG_8192C("fw download ok!\n");	
	}
}

	rtl8723a_InitializeFirmwareVars(Adapter);

	if(pwrctrlpriv->reg_rfoff == _TRUE){
		pwrctrlpriv->rf_pwrstate = rf_off;
	}

	// 2010/08/09 MH We need to check if we need to turnon or off RF after detecting
	// HW GPIO pin. Before PHY_RFConfig8192C.
	//HalDetectPwrDownMode(Adapter);
	// 2010/08/26 MH If Efuse does not support sective suspend then disable the function.
	//HalDetectSelectiveSuspendMode(Adapter);

	// Set RF type for BB/RF configuration	
	_InitRFType(Adapter);//->_ReadRFType()

	// Save target channel
	// <Roger_Notes> Current Channel will be updated again later.
	pHalData->CurrentChannel = 6;//default set to 6


HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MAC);
#if (HAL_MAC_ENABLE == 1)
	status = PHY_MACConfig8723A(Adapter);
	if(status == _FAIL)
	{
		DBG_8192C("PHY_MACConfig8723A fault !!\n");	
		goto exit;
	}
#endif


HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_BB);
	//
	//d. Initialize BB related configurations.
	//
#if (HAL_BB_ENABLE == 1)
	status = PHY_BBConfig8723A(Adapter);
	if(status == _FAIL)
	{
		DBG_8192C("PHY_BBConfig8723A fault !!\n");	
		goto exit;
	}
#endif

	//
	// 2011/11/15 MH Add for tx power by rate fine tune. We need to call the function after BB config.
	// Because the tx power by rate table is inited in BB config.
	//
//	HAL_AdjustPwrIndexDiffRateOffset(Adapter);
//	HAL_AdjustPwrIndexbyRegistry(Adapter);

#if 0
	// The FW command register update must after MAC and FW init ready.
	if (Adapter->bFWReady == TRUE)
	{		
		if(pDevice->RegUsbSS)
		{
			u1Byte		u1H2CSSRf[3]={0};

			SET_H2CCMD_SELECTIVE_SUSPEND_ROF_CMD_ON(u1H2CSSRf,  1);
			SET_H2CCMD_SELECTIVE_SUSPEND_ROF_CMD_GPIO_PERIOD(u1H2CSSRf, 500);
			
			FillH2CCmd92C(Adapter, H2C_SELECTIVE_SUSPEND_ROF_CMD, 3, u1H2CSSRf);			
			RT_TRACE(COMP_INIT, DBG_LOUD, 
			("SS Set H2C_CMD for FW detect GPIO time=%d\n", GET_H2CCMD_SELECTIVE_SUSPEND_ROF_CMD_GPIO_PERIOD(u1H2CSSRf)));
		}
		else
			RT_TRACE(COMP_INIT, DBG_LOUD, ("Non-SS Driver detect GPIO by itself\n"));
	}
	else
	{
		RT_TRACE(COMP_INIT, DBG_LOUD, ("Adapter->bFWReady == FALSE\n"));
		// 2011/02/11 MH If FW is not ready, we can not enter seecitve suspend mode, otherwise,
		// We can not support GPIO/PBC detection by FW with selectiev suspend support.
		pDevice->RegUsbSS = FALSE;	
	}
#endif


HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_RF);
#if (HAL_RF_ENABLE == 1)
	status = PHY_RFConfig8723A(Adapter);
	if(status == _FAIL)
	{
		DBG_8192C("PHY_RFConfig8723A fault !!\n");	
		goto exit;
	}

	//reducing 80M spur
	PHY_SetBBReg(Adapter, RF_T_METER, bMaskDWord, 0x0381808d);
	PHY_SetBBReg(Adapter, RF_SYN_G4, bMaskDWord, 0xf2ffff83);
	PHY_SetBBReg(Adapter, RF_SYN_G4, bMaskDWord, 0xf2ffff82);
	PHY_SetBBReg(Adapter, RF_SYN_G4, bMaskDWord, 0xf2ffff83);

	//RFSW Control
	PHY_SetBBReg(Adapter, rFPGA0_TxInfo, bMaskDWord, 0x00000003);	//0x804[14]=0
	PHY_SetBBReg(Adapter, rFPGA0_XAB_RFInterfaceSW, bMaskDWord, 0x07000760);	//0x870[6:5]=b'11
	PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, bMaskDWord, 0x66F60210); //0x860[6:5]=b'00

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("%s: 0x870 = value 0x%x\n", __FUNCTION__, PHY_QueryBBReg(Adapter, 0x870, bMaskDWord)));

#endif

	//
	// Joseph Note: Keep RfRegChnlVal for later use.
	//
	pHalData->RfRegChnlVal[0] = PHY_QueryRFReg(Adapter, (RF_RADIO_PATH_E)0, RF_CHNLBW, bRFRegOffsetMask);
	pHalData->RfRegChnlVal[1] = PHY_QueryRFReg(Adapter, (RF_RADIO_PATH_E)1, RF_CHNLBW, bRFRegOffsetMask);


HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC02);
	if (!pHalData->bMACFuncEnable) {
		_InitQueueReservedPage(Adapter);
		_InitTxBufferBoundary(Adapter);
	}
	_InitQueuePriority(Adapter);
	_InitPageBoundary(Adapter);
	_InitTransferPageSize(Adapter);

	// Get Rx PHY status in order to report RSSI and others.
	_InitDriverInfoSize(Adapter, DRVINFO_SZ);

	_InitInterrupt(Adapter);
	hal_init_macaddr(Adapter);//set mac_address
	_InitNetworkType(Adapter);//set msr
	_InitWMACSetting(Adapter);
	_InitAdaptiveCtrl(Adapter);
	_InitEDCA(Adapter);
	_InitRateFallback(Adapter);
	_InitRetryFunction(Adapter);
	InitUsbAggregationSetting(Adapter);
	_InitOperationMode(Adapter);//todo
	rtl8723a_InitBeaconParameters(Adapter);
	rtl8723a_InitBeaconMaxError(Adapter, _TRUE);

#ifdef RTL8192CU_ADHOC_WORKAROUND_SETTING
	_InitAdhocWorkaroundParams(Adapter);
#endif

	_InitHardwareDropIncorrectBulkOut(Adapter);

#if defined(CONFIG_CONCURRENT_MODE) || defined(CONFIG_TX_MCAST2UNI)

#ifdef CONFIG_CHECK_AC_LIFETIME
	// Enable lifetime check for the four ACs
	rtw_write8(Adapter, REG_LIFETIME_EN, 0x0F);
#endif	// CONFIG_CHECK_AC_LIFETIME

#ifdef CONFIG_TX_MCAST2UNI
	rtw_write16(Adapter, REG_PKT_VO_VI_LIFE_TIME, 0x0400);	// unit: 256us. 256ms
	rtw_write16(Adapter, REG_PKT_BE_BK_LIFE_TIME, 0x0400);	// unit: 256us. 256ms
#else	// CONFIG_TX_MCAST2UNI
	rtw_write16(Adapter, REG_PKT_VO_VI_LIFE_TIME, 0x3000);	// unit: 256us. 3s
	rtw_write16(Adapter, REG_PKT_BE_BK_LIFE_TIME, 0x3000);	// unit: 256us. 3s
#endif	// CONFIG_TX_MCAST2UNI
#endif	// CONFIG_CONCURRENT_MODE || CONFIG_TX_MCAST2UNI
	

#ifdef CONFIG_LED
	_InitHWLed(Adapter);
#endif //CONFIG_LED

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_TURN_ON_BLOCK);
	_BBTurnOnBlock(Adapter);
	//NicIFSetMacAddress(padapter, padapter->PermanentAddress);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_SECURITY);
	invalidate_cam_all(Adapter);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC11);
	// 2010/12/17 MH We need to set TX power according to EFUSE content at first.
	PHY_SetTxPowerLevel8192C(Adapter, pHalData->CurrentChannel);

	rtl8723a_InitAntenna_Selection(Adapter);

	// HW SEQ CTRL
	//set 0x0 to 0xFF by tynli. Default enable HW SEQ NUM.
	rtw_write8(Adapter,REG_HWSEQ_CTRL, 0xFF); 

	// 
	// Disable BAR, suggested by Scott
	// 2010.04.09 add by hpfan
	//
	rtw_write32(Adapter, REG_BAR_MODE_CTRL, 0x0201ffff);

	if(pregistrypriv->wifi_spec)
		rtw_write16(Adapter,REG_FAST_EDCA_CTRL ,0);

	// Move by Neo for USB SS from above setp
	_RfPowerSave(Adapter);

#if (MP_DRIVER == 1)
	if (Adapter->registrypriv.mp_mode == 1)
	{
	Adapter->mppriv.channel = pHalData->CurrentChannel;
	MPT_InitializeAdapter(Adapter, Adapter->mppriv.channel);
	}
	else
#endif
	{
	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_IQK);
		// 2010/08/26 MH Merge from 8192CE.
		//sherry masked that it has been done in _RfPowerSave
		//20110927
		//recovery for 8192cu and 9723Au 20111017
		if(pwrctrlpriv->rf_pwrstate == rf_on)
		{
			if(pHalData->bIQKInitialized ){
				rtl8192c_PHY_IQCalibrate(Adapter,_TRUE);
			} else {
				rtl8192c_PHY_IQCalibrate(Adapter,_FALSE);
				pHalData->bIQKInitialized = _TRUE;
			}
			
	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_PW_TRACK);
			rtl8192c_odm_CheckTXPowerTracking(Adapter);

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_LCK);
			rtl8192c_PHY_LCCalibrate(Adapter);

#ifdef CONFIG_BT_COEXIST
			rtl8723a_SingleDualAntennaDetection(Adapter);
#endif
		}
	}


HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC21);
#ifdef USB_INTERFERENCE_ISSUE
 //fixed USB interface interference issue
	rtw_write8(Adapter, 0xfe40, 0xe0);
	rtw_write8(Adapter, 0xfe41, 0x8d);
	rtw_write8(Adapter, 0xfe42, 0x80);
	rtw_write32(Adapter,0x20c,0xfd0320);
#if 1
	//2011/01/07 ,suggest by Johnny,for solved the problem that too many protocol error on USB bus
	if(!IS_81xxC_VENDOR_UMC_A_CUT(pHalData->VersionID) )//&& !IS_92C_SERIAL(pHalData->VersionID))// TSMC , 8188
	{		
		// 0xE6=0x94
		rtw_write8(Adapter, 0xFE40, 0xE6);
		rtw_write8(Adapter, 0xFE41, 0x94);
		rtw_write8(Adapter, 0xFE42, 0x80); 

		// 0xE0=0x19
		rtw_write8(Adapter, 0xFE40, 0xE0);
		rtw_write8(Adapter, 0xFE41, 0x19);
		rtw_write8(Adapter, 0xFE42, 0x80);

		// 0xE5=0x91
		rtw_write8(Adapter, 0xFE40, 0xE5);
		rtw_write8(Adapter, 0xFE41, 0x91);
		rtw_write8(Adapter, 0xFE42, 0x80); 

		// 0xE2=0x81
		rtw_write8(Adapter, 0xFE40, 0xE2);
		rtw_write8(Adapter, 0xFE41, 0x81);
		rtw_write8(Adapter, 0xFE42, 0x80);    
	
	}

#endif
#endif //USB_INTERFERENCE_ISSUE

//HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_PABIAS);
//	_InitPABias(Adapter);

#ifdef CONFIG_BT_COEXIST
HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_BT_COEXIST);
	// Init BT hw config.
	BT_InitHwConfig(Adapter);
#endif

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_HAL_DM);
	rtl8723a_InitHalDm(Adapter);

#if 0
	// 2010/05/20 MH We need to init timer after update setting. Otherwise, we can not get correct inf setting.
	// 2010/05/18 MH For SE series only now. Init GPIO detect time
	if(pDevice->RegUsbSS)
	{
		RT_TRACE(COMP_INIT, DBG_LOUD, (" call GpioDetectTimerStart8192CU\n"));							
		GpioDetectTimerStart8192CU(Adapter);	// Disable temporarily
	}

	// 2010/08/23 MH According to Alfred's suggestion, we need to to prevent HW enter
	// suspend mode automatically.
	HwSuspendModeEnable92Cu(Adapter, _FALSE);
#endif

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC31);
	rtw_hal_set_hwreg(Adapter, HW_VAR_NAV_UPPER, (u8*)&NavUpper);

	// 2011/03/09 MH debug only, UMC-B cut pass 2500 S5 test, but we need to fin root cause. 
	if (!IS_HARDWARE_TYPE_8192DU(Adapter) && ((rtw_read32(Adapter, rFPGA0_RFMOD) & 0xFF000000) != 0x83000000)) 
	{
		PHY_SetBBReg(Adapter, rFPGA0_RFMOD, BIT(24), 1);
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("%s: IQK fail recorver\n", __FUNCTION__));
	}
	else
	{
		RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("%s: IQK OK\n", __FUNCTION__));
	}

#ifdef CONFIG_XMIT_ACK
	//ack for xmit mgmt frames.
	rtw_write32(Adapter, REG_FWHW_TXQ_CTRL, rtw_read32(Adapter, REG_FWHW_TXQ_CTRL)|BIT(12));
#endif //CONFIG_XMIT_ACK

	//_dbg_dump_macreg(padapter);

exit:
HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_END);

	DBG_871X("%s in %dms\n", __FUNCTION__, rtw_get_passing_time_ms(init_start_time));

	#ifdef DBG_HAL_INIT_PROFILING
	hal_init_stages_timestamp[HAL_INIT_STAGES_END]=rtw_get_current_time();

	for(hal_init_profiling_i=0;hal_init_profiling_i<HAL_INIT_STAGES_NUM-1;hal_init_profiling_i++) {
		DBG_871X("DBG_HAL_INIT_PROFILING: %35s, %u, %5u, %5u\n"
			, hal_init_stages_str[hal_init_profiling_i]
			, hal_init_stages_timestamp[hal_init_profiling_i]
			, (hal_init_stages_timestamp[hal_init_profiling_i+1]-hal_init_stages_timestamp[hal_init_profiling_i])
			, rtw_get_time_interval_ms(hal_init_stages_timestamp[hal_init_profiling_i], hal_init_stages_timestamp[hal_init_profiling_i+1])
		);
	}	
	#endif

_func_exit_;

	return status;
}


#define SYNC_SD7_20110802_phy_SsPwrSwitch92CU
#ifdef SYNC_SD7_20110802_phy_SsPwrSwitch92CU
VOID
phy_SsPwrSwitch92CU(
	IN	PADAPTER			Adapter,
	IN	rt_rf_power_state	eRFPowerState,
	IN	int bRegSSPwrLvl
	)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(Adapter);
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	u8				value8;
	
	switch( eRFPowerState )
	{
		case rf_on:								
			if (bRegSSPwrLvl == 1)
			{
				// 1. Enable MAC Clock. Can not be enabled now.
				//WriteXBYTE(REG_SYS_CLKR+1, ReadXBYTE(REG_SYS_CLKR+1) | BIT(3));
				
				// 2. Force PWM, Enable SPS18_LDO_Marco_Block
				rtw_write8(Adapter, REG_SPS0_CTRL, 
				rtw_read8(Adapter, REG_SPS0_CTRL) | (BIT0|BIT3));

				// 3. restore BB, AFE control register.
				//RF
				if (pHalData->rf_type ==  RF_2T2R)
					PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, 0x380038, 1);							
				else								
					PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, 0x38, 1);							
				PHY_SetBBReg(Adapter, rOFDM0_TRxPathEnable, 0xf0, 1);
				PHY_SetBBReg(Adapter, rFPGA0_RFMOD, BIT1, 0);

				//AFE
				//DbgPrint("0x0e70 = %x\n", Adapter->PS_BBRegBackup[PSBBREG_AFE0]);
				//PHY_SetBBReg(Adapter, 0x0e70, bMaskDWord ,Adapter->PS_BBRegBackup[PSBBREG_AFE0] );
				//PHY_SetBBReg(Adapter, 0x0e70, bMaskDWord ,0x631B25A0 );				
				if (pHalData->rf_type ==  RF_2T2R)
					PHY_SetBBReg(Adapter, rRx_Wait_CCA, bMaskDWord ,0x63DB25A0 );
				else if (pHalData->rf_type ==  RF_1T1R)
					PHY_SetBBReg(Adapter, rRx_Wait_CCA, bMaskDWord ,0x631B25A0 );

				// 4. issue 3-wire command that RF set to Rx idle mode. This is used to re-write the RX idle mode.
				// We can only prvide a usual value instead and then HW will modify the value by itself.
				PHY_SetRFReg(Adapter,RF_PATH_A, 0, bRFRegOffsetMask,0x32D95);
				if (pHalData->rf_type ==  RF_2T2R)
				{
					PHY_SetRFReg(Adapter,RF_PATH_B, 0, bRFRegOffsetMask,0x32D95);
				}
			}	
			else		// Level 2 or others.
			{
				//h.	AFE_PLL_CTRL 0x28[7:0] = 0x80			//disable AFE PLL
				rtw_write8(Adapter, REG_AFE_PLL_CTRL, 0x81);
				
				// i.	AFE_XTAL_CTRL 0x24[15:0] = 0x880F		//gated AFE DIG_CLOCK
				rtw_write16(Adapter, REG_AFE_XTAL_CTRL, 0x800F);
				rtw_mdelay_os(1);
				
				// 1. Enable MAC Clock. Can not be enabled now.
				//WriteXBYTE(REG_SYS_CLKR+1, ReadXBYTE(REG_SYS_CLKR+1) | BIT(3));
				
				// 2. Force PWM, Enable SPS18_LDO_Marco_Block
				rtw_write8(Adapter, REG_SPS0_CTRL, 
				rtw_read8(Adapter, REG_SPS0_CTRL) | (BIT0|BIT3));

				// 3. restore BB, AFE control register.
				//RF
				if (pHalData->rf_type ==  RF_2T2R)
					PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, 0x380038, 1);							
				else								
					PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, 0x38, 1);							
				PHY_SetBBReg(Adapter, rOFDM0_TRxPathEnable, 0xf0, 1);
				PHY_SetBBReg(Adapter, rFPGA0_RFMOD, BIT1, 0);

				//AFE
				if (pHalData->rf_type ==  RF_2T2R)
					PHY_SetBBReg(Adapter, rRx_Wait_CCA, bMaskDWord ,0x63DB25A0 );
				else if (pHalData->rf_type ==  RF_1T1R)
					PHY_SetBBReg(Adapter, rRx_Wait_CCA, bMaskDWord ,0x631B25A0 );

				// 4. issue 3-wire command that RF set to Rx idle mode. This is used to re-write the RX idle mode.
				// We can only prvide a usual value instead and then HW will modify the value by itself.
				PHY_SetRFReg(Adapter,RF_PATH_A, 0, bRFRegOffsetMask,0x32D95);
				if (pHalData->rf_type ==  RF_2T2R)
				{
					PHY_SetRFReg(Adapter,RF_PATH_B, 0, bRFRegOffsetMask,0x32D95);
				}

				// 5. gated MAC Clock
				//WriteXBYTE(REG_SYS_CLKR+1, ReadXBYTE(REG_SYS_CLKR+1) & ~(BIT(3)));
				//rtw_write8(Adapter, REG_SYS_CLKR+1, rtw_read8(Adapter, REG_SYS_CLKR+1)|(BIT3));

				{
					//u8 			eRFPath = RF_PATH_A,value8 = 0, retry = 0;
					u8		bytetmp;
					//PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, 0x0, bMaskByte0, 0x0);
					// 2010/08/12 MH Add for B path under SS test. 
					//if (pHalData->RF_Type ==  RF_2T2R)
						//PHY_SetRFReg(Adapter, RF_PATH_B, 0x0, bMaskByte0, 0x0);

					bytetmp = rtw_read8(Adapter, REG_APSD_CTRL);
					rtw_write8(Adapter, REG_APSD_CTRL, bytetmp & ~BIT6);
				
					rtw_mdelay_os(10);

					// Set BB reset at first
					rtw_write8(Adapter, REG_SYS_FUNC_EN, 0x17 );//0x16		

					// Enable TX
					rtw_write8(Adapter, REG_TXPAUSE, 0x0);
				}				
				//CardSelectiveSuspendLeave(Adapter);
			}

			break;
	   
		case rf_sleep:
		case rf_off:
				value8 = rtw_read8(Adapter, REG_SPS0_CTRL) ;
				if (IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID))
					value8 &= ~(BIT0);
				else
					value8 &= ~(BIT0|BIT3);
				if (bRegSSPwrLvl == 1)
				{
					RT_TRACE(_module_hal_init_c_, _drv_err_, ("SS LVL1\n"));
					// Disable RF and BB only for SelectSuspend.
					
					// 1. Set BB/RF to shutdown.
					//	(1) Reg878[5:3]= 0 	// RF rx_code for preamble power saving
					//	(2)Reg878[21:19]= 0	//Turn off RF-B
					//	(3) RegC04[7:4]= 0 	// turn off all paths for packet detection
					//	(4) Reg800[1] = 1 		// enable preamble power saving
					pwrctl->PS_BBRegBackup[PSBBREG_RF0] = PHY_QueryBBReg(Adapter, rFPGA0_XAB_RFParameter, bMaskDWord);
					pwrctl->PS_BBRegBackup[PSBBREG_RF1] = PHY_QueryBBReg(Adapter, rOFDM0_TRxPathEnable, bMaskDWord);
					pwrctl->PS_BBRegBackup[PSBBREG_RF2] = PHY_QueryBBReg(Adapter, rFPGA0_RFMOD, bMaskDWord);
					if (pHalData->rf_type ==  RF_2T2R)
					{
						PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, 0x380038, 0);							
					}
					else if (pHalData->rf_type ==  RF_1T1R)
					{
						PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, 0x38, 0);							
					}
					PHY_SetBBReg(Adapter, rOFDM0_TRxPathEnable, 0xf0, 0);						
					PHY_SetBBReg(Adapter, rFPGA0_RFMOD, BIT1,1);
					
					// 2 .AFE control register to power down. bit[30:22]
					pwrctl->PS_BBRegBackup[PSBBREG_AFE0] = PHY_QueryBBReg(Adapter, rRx_Wait_CCA, bMaskDWord);	
					if (pHalData->rf_type ==  RF_2T2R)
						PHY_SetBBReg(Adapter, rRx_Wait_CCA, bMaskDWord ,0x00DB25A0);
					else if (pHalData->rf_type ==  RF_1T1R)
						PHY_SetBBReg(Adapter, rRx_Wait_CCA, bMaskDWord ,0x001B25A0);
					
					// 3. issue 3-wire command that RF set to power down.
					PHY_SetRFReg(Adapter,RF_PATH_A, 0, bRFRegOffsetMask,0);
					if (pHalData->rf_type ==  RF_2T2R)
					{
						PHY_SetRFReg(Adapter,RF_PATH_B, 0, bRFRegOffsetMask,0);
					}

					// 4. Force PFM , disable SPS18_LDO_Marco_Block					
					rtw_write8(Adapter, REG_SPS0_CTRL, value8);

					// 5. gated MAC Clock
					//WriteXBYTE(REG_SYS_CLKR+1, ReadXBYTE(REG_SYS_CLKR+1) & ~(BIT(3)));
				}
				else	// Level 2 or others.
				{
					RT_TRACE(COMP_POWER, DBG_LOUD, ("SS LVL2\n"));
					{
						u8 			eRFPath = RF_PATH_A,value8 = 0;
						rtw_write8(Adapter, REG_TXPAUSE, 0xFF);
						PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, 0x0, bMaskByte0, 0x0);
						// 2010/08/12 MH Add for B path under SS test. 
						//if (pHalData->RF_Type ==  RF_2T2R)
							//PHY_SetRFReg(Adapter, RF_PATH_B, 0x0, bMaskByte0, 0x0);

						value8 |= APSDOFF;
						rtw_write8(Adapter, REG_APSD_CTRL, value8);//0x40

						// After switch APSD, we need to delay for stability
						rtw_mdelay_os(10);

						// Set BB reset at first
						value8 = 0 ; 
						value8 |=( FEN_USBD | FEN_USBA | FEN_BB_GLB_RSTn);
						rtw_write8(Adapter, REG_SYS_FUNC_EN,value8 );//0x16			
					}

					// Disable RF and BB only for SelectSuspend.

					// 1. Set BB/RF to shutdown.
					//	(1) Reg878[5:3]= 0 	// RF rx_code for preamble power saving
					//	(2)Reg878[21:19]= 0	//Turn off RF-B
					//	(3) RegC04[7:4]= 0 	// turn off all paths for packet detection
					//	(4) Reg800[1] = 1 		// enable preamble power saving
					pwrctl->PS_BBRegBackup[PSBBREG_RF0] = PHY_QueryBBReg(Adapter, rFPGA0_XAB_RFParameter, bMaskDWord);
					pwrctl->PS_BBRegBackup[PSBBREG_RF1] = PHY_QueryBBReg(Adapter, rOFDM0_TRxPathEnable, bMaskDWord);
					pwrctl->PS_BBRegBackup[PSBBREG_RF2] = PHY_QueryBBReg(Adapter, rFPGA0_RFMOD, bMaskDWord);
					if (pHalData->rf_type ==  RF_2T2R)
					{
						PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, 0x380038, 0);							
					}
					else if (pHalData->rf_type ==  RF_1T1R)
					{
						PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, 0x38, 0);							
					}
					PHY_SetBBReg(Adapter, rOFDM0_TRxPathEnable, 0xf0, 0);						
					PHY_SetBBReg(Adapter, rFPGA0_RFMOD, BIT1,1);
					
					// 2 .AFE control register to power down. bit[30:22]
					pwrctl->PS_BBRegBackup[PSBBREG_AFE0] = PHY_QueryBBReg(Adapter, rRx_Wait_CCA, bMaskDWord);	
					if (pHalData->rf_type ==  RF_2T2R)
						PHY_SetBBReg(Adapter, rRx_Wait_CCA, bMaskDWord ,0x00DB25A0);
					else if (pHalData->rf_type ==  RF_1T1R)
						PHY_SetBBReg(Adapter, rRx_Wait_CCA, bMaskDWord ,0x001B25A0);
					
					// 3. issue 3-wire command that RF set to power down.
					PHY_SetRFReg(Adapter,RF_PATH_A, 0, bRFRegOffsetMask,0);
					if (pHalData->rf_type ==  RF_2T2R)
					{
						PHY_SetRFReg(Adapter,RF_PATH_B, 0, bRFRegOffsetMask,0);
					}

					// 4. Force PFM , disable SPS18_LDO_Marco_Block
					rtw_write8(Adapter, REG_SPS0_CTRL, value8);

					// 2010/10/13 MH/Isaachsu exchange sequence.
					//h.	AFE_PLL_CTRL 0x28[7:0] = 0x80			//disable AFE PLL
					rtw_write8(Adapter, REG_AFE_PLL_CTRL, 0x80);
					rtw_mdelay_os(1);

					// i.	AFE_XTAL_CTRL 0x24[15:0] = 0x880F		//gated AFE DIG_CLOCK
					rtw_write16(Adapter, REG_AFE_XTAL_CTRL, 0xA80F);

					// 5. gated MAC Clock
					//WriteXBYTE(REG_SYS_CLKR+1, ReadXBYTE(REG_SYS_CLKR+1) & ~(BIT(3)));
					//rtw_write8(Adapter, REG_SYS_CLKR+1, rtw_read8(Adapter, REG_SYS_CLKR+1)& ~(BIT3))
					
					//CardSelectiveSuspendEnter(Adapter);
				}

			break;

		default:
			break;
	} 
	
}	// phy_PowerSwitch92CU

void _ps_open_RF(_adapter *padapter) {
	//here call with bRegSSPwrLvl 1, bRegSSPwrLvl 2 needs to be verified
	phy_SsPwrSwitch92CU(padapter, rf_on, 1);
}

void _ps_close_RF(_adapter *padapter){
	//here call with bRegSSPwrLvl 1, bRegSSPwrLvl 2 needs to be verified
	phy_SsPwrSwitch92CU(padapter, rf_off, 1);
}
#endif //SYNC_SD7_20110802_phy_SsPwrSwitch92CU



static VOID 
_DisableGPIO(
	IN	PADAPTER	Adapter
	)
{
/***************************************
j. GPIO_PIN_CTRL 0x44[31:0]=0x000		// 
k. Value = GPIO_PIN_CTRL[7:0]
l.  GPIO_PIN_CTRL 0x44[31:0] = 0x00FF0000 | (value <<8); //write external PIN level
m. GPIO_MUXCFG 0x42 [15:0] = 0x0780
n. LEDCFG 0x4C[15:0] = 0x8080
***************************************/
	u8	value8;
	u16	value16;
	u32	value32;

	//1. Disable GPIO[7:0]
	rtw_write16(Adapter, REG_GPIO_PIN_CTRL+2, 0x0000);
    	value32 = rtw_read32(Adapter, REG_GPIO_PIN_CTRL) & 0xFFFF00FF;  
	value8 = (u8) (value32&0x000000FF);
	value32 |= ((value8<<8) | 0x00FF0000);
	rtw_write32(Adapter, REG_GPIO_PIN_CTRL, value32);
	      
	//2. Disable GPIO[10:8]          
	rtw_write8(Adapter, REG_GPIO_MUXCFG+3, 0x00);
	    value16 = rtw_read16(Adapter, REG_GPIO_MUXCFG+2) & 0xFF0F;  
	value8 = (u8) (value16&0x000F);
	value16 |= ((value8<<4) | 0x0780);
	rtw_write16(Adapter, REG_GPIO_MUXCFG+2, value16);

	//3. Disable LED0 & 1
	rtw_write16(Adapter, REG_LEDCFG0, 0x8080);

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("======> Disable GPIO and LED.\n"));
 
} //end of _DisableGPIO()

static VOID
_ResetFWDownloadRegister(
	IN PADAPTER			Adapter
	)
{
	u32	value32;

	value32 = rtw_read32(Adapter, REG_MCUFWDL);
	value32 &= ~(MCUFWDL_EN | MCUFWDL_RDY);
	rtw_write32(Adapter, REG_MCUFWDL, value32);
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Reset FW download register.\n"));
}


static int
_DisableRF_AFE(
	IN PADAPTER			Adapter
	)
{
	int		rtStatus = _SUCCESS;
	u32			pollingCount = 0;
	u8			value8;
	
	//disable RF/ AFE AD/DA
	value8 = APSDOFF;
	rtw_write8(Adapter, REG_APSD_CTRL, value8);


#if (RTL8192CU_ASIC_VERIFICATION)

	do
	{
		if(rtw_read8(Adapter, REG_APSD_CTRL) & APSDOFF_STATUS){
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("Disable RF, AFE, AD, DA Done!\n"));
			break;
		}

		if(pollingCount++ > POLLING_READY_TIMEOUT_COUNT){
			//RT_TRACE(COMP_INIT, DBG_SERIOUS, ("Failed to polling APSDOFF_STATUS done!\n"));
			return _FAIL;
		}
				
	}while(_TRUE);
	
#endif

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Disable RF, AFE,AD, DA.\n"));
	return rtStatus;

}

static VOID
_ResetBB(
	IN PADAPTER			Adapter
	)
{
	u16	value16;

	//reset BB
	value16 = rtw_read16(Adapter, REG_SYS_FUNC_EN);
	value16 &= ~(FEN_BBRSTB | FEN_BB_GLB_RSTn);
	rtw_write16(Adapter, REG_SYS_FUNC_EN, value16);
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Reset BB.\n"));
}

static VOID
_ResetMCU(
	IN PADAPTER			Adapter
	)
{
	u16	value16;
	
	// reset MCU
	value16 = rtw_read16(Adapter, REG_SYS_FUNC_EN);
	value16 &= ~FEN_CPUEN;
	rtw_write16(Adapter, REG_SYS_FUNC_EN, value16);
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Reset MCU.\n"));
}

static VOID
_DisableMAC_AFE_PLL(
	IN PADAPTER			Adapter
	)
{
	u32	value32;
	
	//disable MAC/ AFE PLL
	value32 = rtw_read32(Adapter, REG_APS_FSMCO);
	value32 |= APDM_MAC;
	rtw_write32(Adapter, REG_APS_FSMCO, value32);
	
	value32 |= APFM_OFF;
	rtw_write32(Adapter, REG_APS_FSMCO, value32);
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Disable MAC, AFE PLL.\n"));
}

static VOID
_AutoPowerDownToHostOff(
	IN	PADAPTER		Adapter
	)
{
	u32			value32;
	rtw_write8(Adapter, REG_SPS0_CTRL, 0x22);

	value32 = rtw_read32(Adapter, REG_APS_FSMCO);	
	
	value32 |= APDM_HOST;//card disable
	rtw_write32(Adapter, REG_APS_FSMCO, value32);
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Auto Power Down to Host-off state.\n"));

	// set USB suspend
	value32 = rtw_read32(Adapter, REG_APS_FSMCO);
	value32 &= ~AFSM_PCIE;
	rtw_write32(Adapter, REG_APS_FSMCO, value32);

}

static VOID
_SetUsbSuspend(
	IN PADAPTER			Adapter
	)
{
	u32			value32;

	value32 = rtw_read32(Adapter, REG_APS_FSMCO);
	
	// set USB suspend
	value32 |= AFSM_HSUS;
	rtw_write32(Adapter, REG_APS_FSMCO, value32);

	//RT_ASSERT(0 == (rtw_read32(Adapter, REG_APS_FSMCO) & BIT(12)),(""));
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("Set USB suspend.\n"));
	
}

static VOID
_DisableRFAFEAndResetBB(
	IN PADAPTER			Adapter
	)
{
/**************************************
a.	TXPAUSE 0x522[7:0] = 0xFF             //Pause MAC TX queue
b.	RF path 0 offset 0x00 = 0x00            // disable RF
c. 	APSD_CTRL 0x600[7:0] = 0x40
d.	SYS_FUNC_EN 0x02[7:0] = 0x16		//reset BB state machine
e.	SYS_FUNC_EN 0x02[7:0] = 0x14		//reset BB state machine
***************************************/
	u8 eRFPath = 0,value8 = 0;
	rtw_write8(Adapter, REG_TXPAUSE, 0xFF);
	PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, 0x0, bMaskByte0, 0x0);

	value8 |= APSDOFF;
	rtw_write8(Adapter, REG_APSD_CTRL, value8);//0x40
	
	value8 = 0 ; 
	value8 |=( FEN_USBD | FEN_USBA | FEN_BB_GLB_RSTn);
	rtw_write8(Adapter, REG_SYS_FUNC_EN,value8 );//0x16		
	
	value8 &=( ~FEN_BB_GLB_RSTn );
	rtw_write8(Adapter, REG_SYS_FUNC_EN, value8); //0x14		
	
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("======> RF off and reset BB.\n"));
}

static VOID
_ResetDigitalProcedure1(
	IN 	PADAPTER			Adapter,
	IN	BOOLEAN				bWithoutHWSM	
	)
{

	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);

	if(pHalData->FirmwareVersion <=  0x20){
		#if 0
		/*****************************
		f.	SYS_FUNC_EN 0x03[7:0]=0x54		// reset MAC register, DCORE
		g.	MCUFWDL 0x80[7:0]=0				// reset MCU ready status
		******************************/
		u4Byte	value32 = 0;
		PlatformIOWrite1Byte(Adapter, REG_SYS_FUNC_EN+1, 0x54);
		PlatformIOWrite1Byte(Adapter, REG_MCUFWDL, 0);	
		#else
		/*****************************
		f.	MCUFWDL 0x80[7:0]=0				// reset MCU ready status
		g.	SYS_FUNC_EN 0x02[10]= 0			// reset MCU register, (8051 reset)
		h.	SYS_FUNC_EN 0x02[15-12]= 5		// reset MAC register, DCORE
		i.     SYS_FUNC_EN 0x02[10]= 1			// enable MCU register, (8051 enable)
		******************************/
			u16 valu16 = 0;
			rtw_write8(Adapter, REG_MCUFWDL, 0);

			valu16 = rtw_read16(Adapter, REG_SYS_FUNC_EN);	
			rtw_write16(Adapter, REG_SYS_FUNC_EN, (valu16 & (~FEN_CPUEN)));//reset MCU ,8051

			valu16 = rtw_read16(Adapter, REG_SYS_FUNC_EN)&0x0FFF;	
			rtw_write16(Adapter, REG_SYS_FUNC_EN, (valu16 |(FEN_HWPDN|FEN_ELDR)));//reset MAC
			
			#ifdef DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE
			{
				u8 val;
				if( (val=rtw_read8(Adapter, REG_MCUFWDL)))
					DBG_871X("DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE %s:%d REG_MCUFWDL:0x%02x\n", __FUNCTION__, __LINE__, val);
			}
			#endif

			
			valu16 = rtw_read16(Adapter, REG_SYS_FUNC_EN);	
			rtw_write16(Adapter, REG_SYS_FUNC_EN, (valu16 | FEN_CPUEN));//enable MCU ,8051	

		
		#endif
	}
	else{
		u8 retry_cnts = 0;	
		
		if(rtw_read8(Adapter, REG_MCUFWDL) & BIT1)
		{ //IF fw in RAM code, do reset 

			rtw_write8(Adapter, REG_MCUFWDL, 0);
			if(Adapter->bFWReady){
				// 2010/08/25 MH Accordign to RD alfred's suggestion, we need to disable other
				// HRCV INT to influence 8051 reset.
				rtw_write8(Adapter, REG_FWIMR, 0x20);
				
				rtw_write8(Adapter, REG_HMETFR+3, 0x20);//8051 reset by self

				while( (retry_cnts++ <100) && (FEN_CPUEN &rtw_read16(Adapter, REG_SYS_FUNC_EN)))
				{					
					rtw_udelay_os(50);//PlatformStallExecution(50);//us
				}

				if(retry_cnts >= 100){
					DBG_8192C("%s #####=> 8051 reset failed!.........................\n", __FUNCTION__);
					// if 8051 reset fail we trigger GPIO 0 for LA
					//PlatformEFIOWrite4Byte(	Adapter, 
					//						REG_GPIO_PIN_CTRL, 
					//						0x00010100);
					// 2010/08/31 MH According to Filen's info, if 8051 reset fail, reset MAC directly.
					rtw_write8(Adapter, REG_SYS_FUNC_EN+1, 0x50);	//Reset MAC and Enable 8051
					rtw_mdelay_os(10);
				}
				else {
					//DBG_871X("%s =====> 8051 reset success (%d) .\n", __FUNCTION__, retry_cnts);
				}
			}
			else {
				DBG_871X("%s =====> 8051 in RAM but !Adapter->bFWReady\n", __FUNCTION__);	
			}
		}
		else{
			//DBG_871X("%s =====> 8051 in ROM.\n", __FUNCTION__);
		}	
		
		#ifdef DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE
		{
			u8 val;
			if( (val=rtw_read8(Adapter, REG_MCUFWDL)))
				DBG_871X("DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE %s:%d REG_MCUFWDL:0x%02x\n", __FUNCTION__, __LINE__, val);
		}
		#endif
		
		rtw_write8(Adapter, REG_SYS_FUNC_EN+1, 0x54);	//Reset MAC and Enable 8051
	}

	// Clear rpwm value for initial toggle bit trigger.
	rtw_write8(Adapter, REG_USB_HRPWM, 0x00);

	if(bWithoutHWSM){
	/*****************************
		Without HW auto state machine
	g.	SYS_CLKR 0x08[15:0] = 0x30A3			//disable MAC clock
	h.	AFE_PLL_CTRL 0x28[7:0] = 0x80			//disable AFE PLL
	i.	AFE_XTAL_CTRL 0x24[15:0] = 0x880F		//gated AFE DIG_CLOCK
	j.	SYS_ISO_CTRL 0x00[7:0] = 0xF9			// isolated digital to PON
	******************************/	
		//rtw_write16(Adapter, REG_SYS_CLKR, 0x30A3);
		rtw_write16(Adapter, REG_SYS_CLKR, 0x70A3);//modify to 0x70A3 by Scott.
		rtw_write8(Adapter, REG_AFE_PLL_CTRL, 0x80);		
		rtw_write16(Adapter, REG_AFE_XTAL_CTRL, 0x880F);
		rtw_write8(Adapter, REG_SYS_ISO_CTRL, 0xF9);		
	}
	else
	{		
		// Disable all RF/BB power 
		rtw_write8(Adapter, REG_RF_CTRL, 0x00);
	}
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("======> Reset Digital.\n"));

}

static VOID
_ResetDigitalProcedure2(
	IN 	PADAPTER			Adapter
)
{
/*****************************
k.	SYS_FUNC_EN 0x03[7:0] = 0x44			// disable ELDR runction
l.	SYS_CLKR 0x08[15:0] = 0x3083			// disable ELDR clock
m.	SYS_ISO_CTRL 0x01[7:0] = 0x83			// isolated ELDR to PON
******************************/
	//rtw_write8(Adapter, REG_SYS_FUNC_EN+1, 0x44);//marked by Scott.
	//rtw_write16(Adapter, REG_SYS_CLKR, 0x3083);
	//rtw_write8(Adapter, REG_SYS_ISO_CTRL+1, 0x83);

 	rtw_write16(Adapter, REG_SYS_CLKR, 0x70a3); //modify to 0x70a3 by Scott.
 	rtw_write8(Adapter, REG_SYS_ISO_CTRL+1, 0x82); //modify to 0x82 by Scott.
}

static VOID
_DisableAnalog(
	IN PADAPTER			Adapter,
	IN BOOLEAN			bWithoutHWSM	
	)
{
	u16 value16 = 0;
	u8 value8=0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	
	if(bWithoutHWSM){
	/*****************************
	n.	LDOA15_CTRL 0x20[7:0] = 0x04		// disable A15 power
	o.	LDOV12D_CTRL 0x21[7:0] = 0x54		// disable digital core power
	r.	When driver call disable, the ASIC will turn off remaining clock automatically 
	******************************/
	
		rtw_write8(Adapter, REG_LDOA15_CTRL, 0x04);
		//PlatformIOWrite1Byte(Adapter, REG_LDOV12D_CTRL, 0x54);		
		
		value8 = rtw_read8(Adapter, REG_LDOV12D_CTRL);		
		value8 &= (~LDV12_EN);
		rtw_write8(Adapter, REG_LDOV12D_CTRL, value8);	
		//RT_TRACE(COMP_INIT, DBG_LOUD, (" REG_LDOV12D_CTRL Reg0x21:0x%02x.\n",value8));
	}
	
/*****************************
h.	SPS0_CTRL 0x11[7:0] = 0x23			//enter PFM mode
i.	APS_FSMCO 0x04[15:0] = 0x4802		// set USB suspend 
******************************/	


	value8 = 0x23;
	if (IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID))
		value8 |= BIT3;

	rtw_write8(Adapter, REG_SPS0_CTRL, value8);


	if(bWithoutHWSM)
	{			
		//value16 |= (APDM_HOST | /*AFSM_HSUS |*/PFM_ALDN);
		// 2010/08/31 According to Filen description, we need to use HW to shut down 8051 automatically.
		// Becasue suspend operatione need the asistance of 8051 to wait for 3ms.
		value16 |= (APDM_HOST | AFSM_HSUS |PFM_ALDN);
	}
	else
	{			
		value16 |= (APDM_HOST | AFSM_HSUS |PFM_ALDN);
	}

	rtw_write16(Adapter, REG_APS_FSMCO,value16 );//0x4802 

	rtw_write8(Adapter, REG_RSV_CTRL, 0x0e);

 #if 0
 	//tynli_test for suspend mode.
	if(!bWithoutHWSM){
 		rtw_write8(Adapter, 0xfe10, 0x19);
	} 
#endif

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("======> Disable Analog Reg0x04:0x%04x.\n",value16));
}

static void rtl8723au_hw_power_down(_adapter *padapter)
{
	u8	u1bTmp;

	DBG_8192C("PowerDownRTL8723U\n");

	
	// 1. Run Card Disable Flow
	// Done before this function call.
	
	// 2. 0x04[16] = 0			// reset WLON
	u1bTmp = rtw_read8(padapter, REG_APS_FSMCO+2);
	rtw_write8(padapter, REG_APS_FSMCO+2, (u1bTmp&(~BIT0)));
	
	// 3. 0x04[12:11] = 2b'11 // enable suspend 
	// Done before this function call.

	// 4. 0x04[15] = 1			// enable PDN
	u1bTmp = rtw_read8(padapter, REG_APS_FSMCO+1);
	rtw_write8(padapter, REG_APS_FSMCO+1, (u1bTmp|BIT7));
}

//
// Description: RTL8723e card disable power sequence v003 which suggested by Scott.
// First created by tynli. 2011.01.28.
//
VOID
CardDisableRTL8723U(
	PADAPTER			Adapter 
)
{
	u8		u1bTmp;
//	PMGNT_INFO	pMgntInfo	= &(Adapter->MgntInfo);

	DBG_8192C("CardDisableRTL8723U\n");

	// USB-MF Card Disable Flow
	// 1. Run LPS WL RFOFF flow
	HalPwrSeqCmdParsing(Adapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, rtl8723A_enter_lps_flow);		

	// 2. 0x1F[7:0] = 0		// turn off RF
	rtw_write8(Adapter, REG_RF_CTRL, 0x00);

	//	==== Reset digital sequence   ======
	if((rtw_read8(Adapter, REG_MCUFWDL)&BIT7) && 
		Adapter->bFWReady) //8051 RAM code
	{	
		rtl8723a_FirmwareSelfReset(Adapter);
	}

	// Reset MCU. Suggested by Filen. 2011.01.26. by tynli.
	u1bTmp = rtw_read8(Adapter, REG_SYS_FUNC_EN+1);
	rtw_write8(Adapter, REG_SYS_FUNC_EN+1, (u1bTmp&(~BIT2)));

	// g.	MCUFWDL 0x80[1:0]=0				// reset MCU ready status
	rtw_write8(Adapter, REG_MCUFWDL, 0x00);

	//	==== Reset digital sequence end ======	
//	if((pMgntInfo->RfOffReason & RF_CHANGE_BY_HW) )
	{
		// Card disable power action flow
		HalPwrSeqCmdParsing(Adapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, rtl8723A_card_disable_flow);	
	}

	// Reset MCU IO Wrapper, added by Roger, 2011.08.30.
	u1bTmp = rtw_read8(Adapter, REG_RSV_CTRL+1);
	rtw_write8(Adapter, REG_RSV_CTRL+1, (u1bTmp&(~BIT0)));	
	u1bTmp = rtw_read8(Adapter, REG_RSV_CTRL+1);
	rtw_write8(Adapter, REG_RSV_CTRL+1, u1bTmp|BIT0);	

	// 7. RSV_CTRL 0x1C[7:0] = 0x0E			// lock ISO/CLK/Power control register
	rtw_write8(Adapter, REG_RSV_CTRL, 0x0e);

}


u32 rtl8723au_hal_deinit(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);


	DBG_8192C("==> %s\n", __FUNCTION__);

#ifdef CONFIG_BT_COEXIST
	BT_HaltProcess(padapter);
#endif
#ifdef CONFIG_MP_INCLUDED
	if (padapter->registrypriv.mp_mode == 1)
		MPT_DeInitAdapter(padapter);
#endif
	// 2011/02/18 To Fix RU LNA  power leakage problem. We need to execute below below in
	// Adapter init and halt sequence. Accordingto EEchou's opinion, we can enable the ability for all
	// IC. Accord to johnny's opinion, only RU need the support.
	CardDisableRTL8723U(padapter);

	return _SUCCESS;
}


unsigned int rtl8723au_inirp_init(PADAPTER Adapter)
{	
	u8 i;	
	struct recv_buf *precvbuf;
	uint	status;
	struct dvobj_priv *pdev= adapter_to_dvobj(Adapter);
	struct intf_hdl * pintfhdl=&Adapter->iopriv.intf;
	struct recv_priv *precvpriv = &(Adapter->recvpriv);
	u32 (*_read_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	u32 (*_read_interrupt)(struct intf_hdl *pintfhdl, u32 addr);
	HAL_DATA_TYPE	*pHalData=GET_HAL_DATA(Adapter);
#endif //CONFIG_USB_INTERRUPT_IN_PIPE

_func_enter_;

	_read_port = pintfhdl->io_ops._read_port;

	status = _SUCCESS;

	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("===> usb_inirp_init \n"));	
		
	precvpriv->ff_hwaddr = RECV_BULK_IN_ADDR;

	//issue Rx irp to receive data	
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;	
	for(i=0; i<NR_RECVBUFF; i++)
	{
		if(_read_port(pintfhdl, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf) == _FALSE )
		{
			RT_TRACE(_module_hci_hal_init_c_,_drv_err_,("usb_rx_init: usb_read_port error \n"));
			status = _FAIL;
			goto exit;
		}
		
		precvbuf++;		
		precvpriv->free_recv_buf_queue_cnt--;
	}

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	_read_interrupt = pintfhdl->io_ops._read_interrupt;
	if(_read_interrupt(pintfhdl, RECV_INT_IN_ADDR) == _FALSE )
	{
		RT_TRACE(_module_hci_hal_init_c_,_drv_err_,("usb_rx_init: usb_read_interrupt error \n"));
		status = _FAIL;
	}
	pHalData->IntrMask[0]=rtw_read32(Adapter, REG_USB_HIMR);
	MSG_8192C("pHalData->IntrMask = 0x%04x\n", pHalData->IntrMask[0]);
	pHalData->IntrMask[0]|=UHIMR_C2HCMD|UHIMR_CPWM;
	rtw_write32(Adapter, REG_USB_HIMR,pHalData->IntrMask[0]);
#endif //CONFIG_USB_INTERRUPT_IN_PIPE

exit:
	
	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("<=== usb_inirp_init \n"));

_func_exit_;

	return status;

}

unsigned int rtl8723au_inirp_deinit(PADAPTER Adapter)
{	
#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	u32 (*_read_interrupt)(struct intf_hdl *pintfhdl, u32 addr);
	HAL_DATA_TYPE	*pHalData=GET_HAL_DATA(Adapter);
#endif //CONFIG_USB_INTERRUPT_IN_PIPE
	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("\n ===> usb_rx_deinit \n"));
	
	rtw_read_port_cancel(Adapter);
#ifdef CONFIG_USB_INTERRUPT_IN_PIPE	
	pHalData->IntrMask[0]=rtw_read32(Adapter, REG_USB_HIMR);
	MSG_8192C("%s pHalData->IntrMask = 0x%04x\n",__FUNCTION__, pHalData->IntrMask[0]);
	pHalData->IntrMask[0]=0x0;
	rtw_write32(Adapter, REG_USB_HIMR,pHalData->IntrMask[0]);
	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("\n <=== usb_rx_deinit \n"));
#endif //CONFIG_USB_INTERRUPT_IN_PIPE
	return _SUCCESS;
}


static u32
_GetChannelGroup(
	IN	u32	channel
	)
{
	//RT_ASSERT((channel < 14), ("Channel %d no is supported!\n"));

	if(channel < 3){ 	// Channel 1~3
		return 0;
	}
	else if(channel < 9){ // Channel 4~9
		return 1;
	}

	return 2;				// Channel 10~14	
}


//-------------------------------------------------------------------
//
//	EEPROM/EFUSE Content Parsing
//
//-------------------------------------------------------------------
static void
_ReadIDs(
	IN	PADAPTER	Adapter,
	IN	u8*		PROMContent,
	IN	BOOLEAN		AutoloadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	if(_FALSE == AutoloadFail){
		// VID, PID 
		pHalData->EEPROMVID = le16_to_cpu( *(u16 *)&PROMContent[EEPROM_VID]);
		pHalData->EEPROMPID = le16_to_cpu( *(u16 *)&PROMContent[EEPROM_PID]);
		
		// Customer ID, 0x00 and 0xff are reserved for Realtek. 		
		pHalData->EEPROMCustomerID = *(u8 *)&PROMContent[EEPROM_CUSTOMER_ID];
		pHalData->EEPROMSubCustomerID = *(u8 *)&PROMContent[EEPROM_SUBCUSTOMER_ID];

	}
	else{
		pHalData->EEPROMVID	 = EEPROM_Default_VID;
		pHalData->EEPROMPID	 = EEPROM_Default_PID;

		// Customer ID, 0x00 and 0xff are reserved for Realtek. 		
		pHalData->EEPROMCustomerID	= EEPROM_Default_CustomerID;
		pHalData->EEPROMSubCustomerID = EEPROM_Default_SubCustomerID;

	}

	// For customized behavior.
	if((pHalData->EEPROMVID == 0x103C) && (pHalData->EEPROMVID == 0x1629))// HP Lite-On for RTL8188CUS Slim Combo.
		pHalData->CustomerID = RT_CID_819x_HP;

	//	Decide CustomerID according to VID/DID or EEPROM
	switch(pHalData->EEPROMCustomerID)
	{
		case EEPROM_CID_DEFAULT:
			if((pHalData->EEPROMVID == 0x2001) && (pHalData->EEPROMPID == 0x3308))
				pHalData->CustomerID = RT_CID_DLINK;
			else if((pHalData->EEPROMVID == 0x2001) && (pHalData->EEPROMPID == 0x3309))
				pHalData->CustomerID = RT_CID_DLINK;
			else if((pHalData->EEPROMVID == 0x2001) && (pHalData->EEPROMPID == 0x330a))
				pHalData->CustomerID = RT_CID_DLINK;
			break;
		case EEPROM_CID_WHQL:
/*			
			Adapter->bInHctTest = TRUE;

			pMgntInfo->bSupportTurboMode = FALSE;
			pMgntInfo->bAutoTurboBy8186 = FALSE;

			pMgntInfo->PowerSaveControl.bInactivePs = FALSE;
			pMgntInfo->PowerSaveControl.bIPSModeBackup = FALSE;
			pMgntInfo->PowerSaveControl.bLeisurePs = FALSE;
				
			pMgntInfo->keepAliveLevel = 0;

			Adapter->bUnloadDriverwhenS3S4 = FALSE;
*/				
			break;
		default:
			pHalData->CustomerID = RT_CID_DEFAULT;
			break;
			
	}

	MSG_8192C("EEPROMVID = 0x%04x\n", pHalData->EEPROMVID);
	MSG_8192C("EEPROMPID = 0x%04x\n", pHalData->EEPROMPID);
	MSG_8192C("EEPROMCustomerID : 0x%02x\n", pHalData->EEPROMCustomerID);
	MSG_8192C("EEPROMSubCustomerID: 0x%02x\n", pHalData->EEPROMSubCustomerID);

	MSG_8192C("RT_CustomerID: 0x%02x\n", pHalData->CustomerID);

}


static VOID
_ReadMACAddress(
	IN	PADAPTER	Adapter,	
	IN	u8*		PROMContent,
	IN	BOOLEAN		AutoloadFail
	)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);

	if(_FALSE == AutoloadFail){
		//Read Permanent MAC address and set value to hardware
		_rtw_memcpy(pEEPROM->mac_addr, &PROMContent[EEPROM_MAC_ADDR], ETH_ALEN);		
	}
	else{
		//Random assigh MAC address
		u8 sMacAddr[MAC_ADDR_LEN] = {0x00, 0xE0, 0x4C, 0x81, 0x92, 0x00};
		//sMacAddr[5] = (u8)GetRandomNumber(1, 254);		
		_rtw_memcpy(pEEPROM->mac_addr, sMacAddr, ETH_ALEN);	
	}
	DBG_8192C("%s MAC Address from EFUSE = "MAC_FMT"\n",__FUNCTION__, MAC_ARG(pEEPROM->mac_addr));
	//NicIFSetMacAddress(Adapter, Adapter->PermanentAddress);
	//RT_PRINT_ADDR(COMP_INIT|COMP_EFUSE, DBG_LOUD, "MAC Addr: %s", Adapter->PermanentAddress);

}

static VOID
_ReadBoardType(
	IN	PADAPTER	Adapter,	
	IN	u8*		PROMContent,
	IN	BOOLEAN		AutoloadFail
	)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter); 
	u32			value32;
	u8			boardType = BOARD_USB_DONGLE; 

	if(AutoloadFail){
		if(IS_8723_SERIES(pHalData->VersionID))
			pHalData->rf_type = RF_1T1R;
		else
		        pHalData->rf_type = RF_2T2R;
	
		pHalData->BoardType = boardType;
		return;
	}

	boardType = PROMContent[EEPROM_NORMAL_BoardType];
	boardType &= BOARD_TYPE_NORMAL_MASK;//bit[7:5]
	boardType >>= 5;

	pHalData->BoardType = boardType;
	MSG_8192C("_ReadBoardType(%x)\n",pHalData->BoardType);

	if (boardType == BOARD_USB_High_PA)
		pHalData->ExternalPA = 1;
}


static VOID
_ReadLEDSetting(
	IN	PADAPTER	Adapter,	
	IN	u8*		PROMContent,
	IN	BOOLEAN		AutoloadFail
	)
{
	struct led_priv *pledpriv = &(Adapter->ledpriv);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
#ifdef CONFIG_SW_LED
	pledpriv->bRegUseLed = _TRUE;

	//
	// Led mode
	//
	switch(pHalData->CustomerID)
	{
		case RT_CID_DEFAULT:
			pledpriv->LedStrategy = SW_LED_MODE1;
			pledpriv->bRegUseLed = _TRUE;
			break;

		case RT_CID_819x_HP:
			pledpriv->LedStrategy = SW_LED_MODE6;
			break;

		default:
			pledpriv->LedStrategy = SW_LED_MODE1;
			break;
	}

	if( BOARD_MINICARD == pHalData->BoardType )
	{
		pledpriv->LedStrategy = SW_LED_MODE6;
	}
	pHalData->bLedOpenDrain = _TRUE;// Support Open-drain arrangement for controlling the LED. Added by Roger, 2009.10.16.
#else // HW LED
	pledpriv->LedStrategy = HW_LED;
#endif //CONFIG_SW_LED
}

static VOID
_ReadThermalMeter(
	IN	PADAPTER	Adapter,	
	IN	u8* 	PROMContent,
	IN	BOOLEAN 	AutoloadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u8	tempval;

	//
	// ThermalMeter from EEPROM
	//
	if(!AutoloadFail)	
		tempval = PROMContent[EEPROM_THERMAL_METER];
	else
		tempval = EEPROM_Default_ThermalMeter;
	
	pHalData->EEPROMThermalMeter = (tempval&0x1f);	//[4:0]

	if(pHalData->EEPROMThermalMeter == 0x1f || AutoloadFail)
		pdmpriv->bAPKThermalMeterIgnore = _TRUE;

#if 0
	if(pHalData->EEPROMThermalMeter < 0x06 || pHalData->EEPROMThermalMeter > 0x1c)
		pHalData->EEPROMThermalMeter = 0x12;
#endif

	pdmpriv->ThermalMeter[0] = pHalData->EEPROMThermalMeter;
	
	//RTPRINT(FINIT, INIT_TxPower, ("ThermalMeter = 0x%x\n", pHalData->EEPROMThermalMeter));
	
}

static VOID
_ReadRFSetting(
	IN	PADAPTER	Adapter,	
	IN	u8* 	PROMContent,
	IN	BOOLEAN 	AutoloadFail
	)
{
}


#if 0
static VOID
readAntennaDiversity(
	IN	PADAPTER	pAdapter,
	IN	u8			*hwinfo,
	IN	BOOLEAN		AutoLoadFail
	)
{

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct registry_priv	*registry_par = &pAdapter->registrypriv;

	if(!AutoLoadFail)
	{
		// Antenna Diversity setting. 
		if(registry_par->antdiv_cfg == 2) // 2: From Efuse
			pHalData->AntDivCfg = (hwinfo[EEPROM_RF_OPT1]&0x18)>>3;
		else
			pHalData->AntDivCfg = registry_par->antdiv_cfg ;  // 0:OFF , 1:ON,

		DBG_8192C("### AntDivCfg(%x)\n",pHalData->AntDivCfg);	

		//if(pHalData->EEPROMBluetoothCoexist!=0 && pHalData->EEPROMBluetoothAntNum==Ant_x1)
		//	pHalData->AntDivCfg = 0;
	}
	else
	{
		pHalData->AntDivCfg = 0;
	}

}
#endif
// Read HW power down mode selection 
static void _ReadPSSetting(IN PADAPTER Adapter,IN u8*PROMContent,IN u8	AutoloadFail)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(Adapter);

	if(AutoloadFail){
		pwrctl->bHWPowerdown = _FALSE;
		pwrctl->bSupportRemoteWakeup = _FALSE;
	}
	else	{
		//if(SUPPORT_HW_RADIO_DETECT(Adapter))
			pwrctl->bHWPwrPindetect = Adapter->registrypriv.hwpwrp_detect;
		//else
			//pwrctl->bHWPwrPindetect = _FALSE;//dongle not support new
			
			
		//hw power down mode selection , 0:rf-off / 1:power down

		if(Adapter->registrypriv.hwpdn_mode==2)
			pwrctl->bHWPowerdown = (PROMContent[EEPROM_RF_OPT3] & BIT4);
		else
			pwrctl->bHWPowerdown = Adapter->registrypriv.hwpdn_mode;
				
		// decide hw if support remote wakeup function
		// if hw supported, 8051 (SIE) will generate WeakUP signal( D+/D- toggle) when autoresume
		pwrctl->bSupportRemoteWakeup = (PROMContent[EEPROM_TEST_USB_OPT] & BIT1)?_TRUE :_FALSE;

		//if(SUPPORT_HW_RADIO_DETECT(Adapter))	
			//Adapter->registrypriv.usbss_enable = pwrctl->bSupportRemoteWakeup ;
		
		DBG_8192C("%s...bHWPwrPindetect(%x)-bHWPowerdown(%x) ,bSupportRemoteWakeup(%x)\n",__FUNCTION__,
			pwrctl->bHWPwrPindetect, pwrctl->bHWPowerdown, pwrctl->bSupportRemoteWakeup);

		DBG_8192C("### PS params=>  power_mgnt(%x),usbss_enable(%x) ###\n",Adapter->registrypriv.power_mgnt,Adapter->registrypriv.usbss_enable);
		
	}
	
}






VOID
Hal_EfuseParsePIDVID_8723AU(
	IN	PADAPTER		pAdapter,
	IN	u8*			hwinfo,
	IN	BOOLEAN			AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if(AutoLoadFail)
	{
		pHalData->EEPROMVID = 0;
		pHalData->EEPROMPID = 0;
	}
	else
	{
			// VID, PID 
		pHalData->EEPROMVID = le16_to_cpu(*(u16*)&hwinfo[EEPROM_VID_8723AU]);
		pHalData->EEPROMPID = le16_to_cpu(*(u16*)&hwinfo[EEPROM_PID_8723AU]);

	}

	MSG_8192C("EEPROM VID = 0x%4x\n", pHalData->EEPROMVID);
	MSG_8192C("EEPROM PID = 0x%4x\n", pHalData->EEPROMPID);
}


static void
Hal_EfuseParseMACAddr_8723AU(
	IN	PADAPTER		padapter,
	IN	u8*			hwinfo,
	IN	BOOLEAN			AutoLoadFail
	)
{
	u16			i, usValue;
	u8			sMacAddr[6] = {0x00, 0xE0, 0x4C, 0x87, 0x23, 0x00};
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);

	if (AutoLoadFail)
	{
//		sMacAddr[5] = (u1Byte)GetRandomNumber(1, 254);
		for (i=0; i<6; i++)
			pEEPROM->mac_addr[i] = sMacAddr[i];
	}
	else
	{
		//Read Permanent MAC address
#if 1
		_rtw_memcpy(pEEPROM->mac_addr, &hwinfo[EEPROM_MAC_ADDR_8723AU], ETH_ALEN);
#else
		for(i=0; i<6; i+=2)
		{
			usValue = *(u16*)&hwinfo[EEPROM_MAC_ADDR_8723S+i];
			*((u16*)(&pEEPROM->mac_addr[i])) = usValue;
		}
#endif
	}
//	NicIFSetMacAddress(pAdapter, pAdapter->PermanentAddress);

	RT_TRACE(_module_hci_hal_init_c_, _drv_notice_,
		 ("Hal_EfuseParseMACAddr_8723AU: Permanent Address=%02x:%02x:%02x:%02x:%02x:%02x\n",
		  pEEPROM->mac_addr[0], pEEPROM->mac_addr[1],
		  pEEPROM->mac_addr[2], pEEPROM->mac_addr[3],
		  pEEPROM->mac_addr[4], pEEPROM->mac_addr[5]));
}


#ifdef CONFIG_EFUSE_CONFIG_FILE
static u32 Hal_readPGDataFromConfigFile(
	PADAPTER	padapter)
{
	u32 i;
	struct file *fp;
	mm_segment_t fs;
	u8 temp[3];
	loff_t pos = 0;
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	u8	*PROMContent = pEEPROM->efuse_eeprom_data;


	temp[2] = 0; // add end of string '\0'

	fp = filp_open("/system/etc/wifi/wifi_efuse.map", O_RDWR,  0644);
	if (IS_ERR(fp)) {
		pEEPROM->bloadfile_fail_flag= _TRUE;
		DBG_871X("Error, Efuse configure file doesn't exist.\n");
		return _FAIL;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);

	DBG_871X("Efuse configure file:\n");
	for (i=0; i<HWSET_MAX_SIZE_88E; i++) {
		vfs_read(fp, temp, 2, &pos);
		PROMContent[i] = simple_strtoul(temp, NULL, 16 );
		pos += 1; // Filter the space character
		DBG_871X("%02X \n", PROMContent[i]);
	}
	DBG_871X("\n");
	set_fs(fs);

	filp_close(fp, NULL);
	
	pEEPROM->bloadfile_fail_flag= _FALSE;
	return _SUCCESS;
}


static void
Hal_ReadMACAddrFromFile_8723AU(
	PADAPTER		padapter
	)
{
	u32 i;
	struct file *fp;
	mm_segment_t fs;
	u8 source_addr[18];
	loff_t pos = 0;
	u32 curtime = rtw_get_current_time();
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	u8 *head, *end;

	u8 null_mac_addr[ETH_ALEN] = {0, 0, 0,0, 0, 0};
	u8 multi_mac_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	_rtw_memset(source_addr, 0, 18);
	_rtw_memset(pEEPROM->mac_addr, 0, ETH_ALEN);

	fp = filp_open("/data/wifimac.txt", O_RDWR,  0644);
	if (IS_ERR(fp)) {
		pEEPROM->bloadmac_fail_flag = _TRUE;
		DBG_871X("Error, wifi mac address file doesn't exist.\n");
	} else {
		fs = get_fs();
		set_fs(KERNEL_DS);

		DBG_871X("wifi mac address:\n");
		vfs_read(fp, source_addr, 18, &pos);
		source_addr[17] = ':';

		head = end = source_addr;
		for (i=0; i<ETH_ALEN; i++) {
			while (end && (*end != ':') )
				end++;

			if (end && (*end == ':') )
				*end = '\0';

			pEEPROM->mac_addr[i] = simple_strtoul(head, NULL, 16 );

			if (end) {
				end++;
				head = end;
			}
			DBG_871X("%02x \n", pEEPROM->mac_addr[i]);
		}
		DBG_871X("\n");
		set_fs(fs);

		filp_close(fp, NULL);
	}

	if ( (_rtw_memcmp(pEEPROM->mac_addr, null_mac_addr, ETH_ALEN)) ||
		(_rtw_memcmp(pEEPROM->mac_addr, multi_mac_addr, ETH_ALEN)) ) {
		pEEPROM->mac_addr[0] = 0x00;
		pEEPROM->mac_addr[1] = 0xe0;
		pEEPROM->mac_addr[2] = 0x4c;
		pEEPROM->mac_addr[3] = (u8)(curtime & 0xff) ;
		pEEPROM->mac_addr[4] = (u8)((curtime>>8) & 0xff) ;
		pEEPROM->mac_addr[5] = (u8)((curtime>>16) & 0xff) ;
	}
	
	pEEPROM->bloadmac_fail_flag = _FALSE;
	
	 DBG_871X("Hal_ReadMACAddrFromFile_8188ES: Permanent Address = %02x-%02x-%02x-%02x-%02x-%02x\n",
		  pEEPROM->mac_addr[0], pEEPROM->mac_addr[1],
		  pEEPROM->mac_addr[2], pEEPROM->mac_addr[3],
		  pEEPROM->mac_addr[4], pEEPROM->mac_addr[5]);
}
#endif //CONFIG_EFUSE_CONFIG_FILE


static VOID
readAdapterInfo(
	IN	PADAPTER	padapter
	)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	//PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u8			hwinfo[HWSET_MAX_SIZE];
	
#ifdef CONFIG_EFUSE_CONFIG_FILE
	Hal_readPGDataFromConfigFile(padapter);
#else //CONFIG_EFUSE_CONFIG_FILE	
	Hal_InitPGData(padapter, hwinfo);
#endif	//CONFIG_EFUSE_CONFIG_FILE	
	Hal_EfuseParseIDCode(padapter, hwinfo);
	Hal_EfuseParsePIDVID_8723AU(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseEEPROMVer(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
#ifdef CONFIG_EFUSE_CONFIG_FILE
	Hal_ReadMACAddrFromFile_8723AU(padapter);
#else //CONFIG_EFUSE_CONFIG_FILE
	Hal_EfuseParseMACAddr_8723AU(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
#endif
	Hal_EfuseParseTxPowerInfo_8723A(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	_ReadBoardType(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseBTCoexistInfo_8723A(padapter, hwinfo, pEEPROM->bautoload_fail_flag);

	rtl8723a_EfuseParseChnlPlan(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseThermalMeter_8723A(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	_ReadLEDSetting(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
//	_ReadRFSetting(Adapter, PROMContent, pEEPROM->bautoload_fail_flag);
//	_ReadPSSetting(Adapter, PROMContent, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseAntennaDiversity(padapter, hwinfo, pEEPROM->bautoload_fail_flag);

	Hal_EfuseParseEEPROMVer(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseCustomerID(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseRateIndicationOption(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseXtal_8723A(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	//
	// The following part initialize some vars by PG info.
	//
	Hal_InitChannelPlan(padapter);

	

	//hal_CustomizedBehavior_8723U(Adapter);

//	Adapter->bDongle = (PROMContent[EEPROM_EASY_REPLACEMENT] == 1)? 0: 1;
	DBG_8192C("%s(): REPLACEMENT = %x\n",__FUNCTION__,padapter->bDongle);
}

static void _ReadPROMContent(
	IN PADAPTER 		Adapter
	)
{	
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			PROMContent[HWSET_MAX_SIZE]={0};
	u8			eeValue;
	u32			i;
	u16			value16;

	eeValue = rtw_read8(Adapter, REG_9346CR);
	// To check system boot selection.
	pEEPROM->EepromOrEfuse		= (eeValue & BOOT_FROM_EEPROM) ? _TRUE : _FALSE;
	pEEPROM->bautoload_fail_flag	= (eeValue & EEPROM_EN) ? _FALSE : _TRUE;


	DBG_8192C("Boot from %s, Autoload %s !\n", (pEEPROM->EepromOrEfuse ? "EEPROM" : "EFUSE"),
				(pEEPROM->bautoload_fail_flag ? "Fail" : "OK") );

	//pHalData->EEType = IS_BOOT_FROM_EEPROM(Adapter) ? EEPROM_93C46 : EEPROM_BOOT_EFUSE;

	//if(IS_HARDWARE_TYPE_8723A(Adapter))
	//	readAdapterInfo_8723U(Adapter);
	//else
		readAdapterInfo(Adapter);
}


static VOID
_InitOtherVariable(
	IN PADAPTER 		Adapter
	)
{
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);	


	//if(Adapter->bInHctTest){
	//	pMgntInfo->PowerSaveControl.bInactivePs = FALSE;
	//	pMgntInfo->PowerSaveControl.bIPSModeBackup = FALSE;
	//	pMgntInfo->PowerSaveControl.bLeisurePs = FALSE;
	//	pMgntInfo->keepAliveLevel = 0;
	//}


}

static VOID
_ReadRFType(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

#if DISABLE_BB_RF
	pHalData->rf_chip = RF_PSEUDO_11N;
#else
	pHalData->rf_chip = RF_6052;
#endif
}

void _ReadSilmComboMode(PADAPTER Adapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	pHalData->SlimComboDbg = _FALSE;	// Default is not debug mode.

	// 2010/11/22 MH We need to enter debug mode for TSMA and UMC A cut
	//if ((Adapter->chip_type == RTL8188C_8192C) && 
/*
	if (IS_HARDWARE_TYPE_8192CU(Adapter) && 
		(pHalData->BoardType == BOARD_USB_COMBO))
	{			
		switch (pHalData->VersionID)
		{
			case	VERSION_NORMAL_TSMC_CHIP_88C:
			case	VERSION_NORMAL_TSMC_CHIP_92C:
			case	VERSION_NORMAL_TSMC_CHIP_92C_1T2R:
			case	VERSION_NORMAL_UMC_CHIP_88C_A_CUT:
			case	VERSION_NORMAL_UMC_CHIP_92C_A_CUT:
			case	VERSION_NORMAL_UMC_CHIP_92C_1T2R_A_CUT:
				if ((rtw_read8(Adapter, REG_SYS_CFG+3) &0xF0) == 0x20)
					pHalData->SlimComboDbg = _TRUE;
				
				break;

			case	VERSION_NORMAL_UMC_CHIP_88C_B_CUT:
			case	VERSION_NORMAL_UMC_CHIP_92C_B_CUT:
			case	VERSION_NORMAL_UMC_CHIP_92C_1T2R_B_CUT:
				// 2011/02/15 MH UNC-B cut ECO fail, we need to support slim combo debug mode.
				if ((rtw_read8(Adapter, REG_SYS_CFG+3) &0xF0) == 0x20)
					pHalData->SlimComboDbg = _TRUE;
				break;
				
			default:
				break;
		}		
		
	}
*/
}

//
//	Description:
//		We should set Efuse cell selection to WiFi cell in default.
//
//	Assumption:
//		PASSIVE_LEVEL
//
//	Added by Roger, 2010.11.23.
//
void
hal_EfuseCellSel(
	IN	PADAPTER	Adapter
	)
{	
	u32			value32;	

	value32 = rtw_read32(Adapter, EFUSE_TEST);
	value32 = (value32 & ~EFUSE_SEL_MASK) | EFUSE_SEL(EFUSE_WIFI_SEL_0);
	rtw_write32(Adapter, EFUSE_TEST, value32);
}

static int _ReadAdapterInfo8723AU(PADAPTER	Adapter)
{
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32 start=rtw_get_current_time();
	
	MSG_8192C("====> _ReadAdapterInfo8723AU\n");

	//Efuse_InitSomeVar(Adapter);

	hal_EfuseCellSel(Adapter);

	_ReadRFType(Adapter);//rf_chip -> _InitRFType()
	_ReadPROMContent(Adapter);

	// 2010/10/25 MH THe function must be called after borad_type & IC-Version recognize.
	_ReadSilmComboMode(Adapter);

	_InitOtherVariable(Adapter);

	//MSG_8192C("%s()(done), rf_chip=0x%x, rf_type=0x%x\n",  __FUNCTION__, pHalData->rf_chip, pHalData->rf_type);

	MSG_8192C("<==== _ReadAdapterInfo8723AU in %d ms\n", rtw_get_passing_time_ms(start));

	return _SUCCESS;
}


static void ReadAdapterInfo8723AU(PADAPTER Adapter)
{
	// Read EEPROM size before call any EEPROM function
	Adapter->EepromAddressSize = GetEEPROMSize8723A(Adapter);
	
	_ReadAdapterInfo8723AU(Adapter);
}


#define GPIO_DEBUG_PORT_NUM 0
static void rtl8192cu_trigger_gpio_0(_adapter *padapter)
{

	u32 gpioctrl;
	DBG_8192C("==> trigger_gpio_0...\n");
	rtw_write16_async(padapter,REG_GPIO_PIN_CTRL,0);
	rtw_write8_async(padapter,REG_GPIO_PIN_CTRL+2,0xFF);
	gpioctrl = (BIT(GPIO_DEBUG_PORT_NUM)<<24 )|(BIT(GPIO_DEBUG_PORT_NUM)<<16);
	rtw_write32_async(padapter,REG_GPIO_PIN_CTRL,gpioctrl);
	gpioctrl |= (BIT(GPIO_DEBUG_PORT_NUM)<<8);
	rtw_write32_async(padapter,REG_GPIO_PIN_CTRL,gpioctrl);
	DBG_8192C("<=== trigger_gpio_0...\n");

}

/*
 * If variable not handled here,
 * some variables will be processed in SetHwReg8723A()
 */
void SetHwReg8723AU(PADAPTER Adapter, u8 variable, u8* val)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);

_func_enter_;

	switch(variable)
	{
		case HW_VAR_RXDMA_AGG_PG_TH:
#ifdef CONFIG_USB_RX_AGGREGATION
			{
				u8 threshold = *val;
				if (threshold == 0)
					threshold = pHalData->UsbRxAggPageCount;
				SetHwReg8723A(Adapter, HW_VAR_RXDMA_AGG_PG_TH, &threshold);
			}
#endif
			break;

		case HW_VAR_SET_RPWM:
			rtw_write8(Adapter, REG_USB_HRPWM, *val);
			break;

		case HW_VAR_TRIGGER_GPIO_0:
			rtl8192cu_trigger_gpio_0(Adapter);
			break;

		default:
			SetHwReg8723A(Adapter, variable, val);
			break;
	}

_func_exit_;
}

/*
 * If variable not handled here,
 * some variables will be processed in GetHwReg8723A()
 */
void GetHwReg8723AU(PADAPTER Adapter, u8 variable, u8* val)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);

_func_enter_;

	switch (variable)
	{
		case HW_VAR_GET_CPWM:
			*val =  rtw_read8(Adapter, REG_USB_HCPWM);			
			break;
		default:
			GetHwReg8723A(Adapter, variable, val);
			break;
	}

_func_exit_;
}

//
//	Description: 
//		Query setting of specified variable.
//
u8
GetHalDefVar8192CUsb(
	IN	PADAPTER				Adapter,
	IN	HAL_DEF_VARIABLE		eVariable,
	IN	PVOID					pValue
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _SUCCESS;

	switch(eVariable)
	{
		case HAL_DEF_UNDERCORATEDSMOOTHEDPWDB:
			*((int *)pValue) = pHalData->dmpriv.UndecoratedSmoothedPWDB;
			break;
		case HAL_DEF_IS_SUPPORT_ANT_DIV:
			#ifdef CONFIG_ANTENNA_DIVERSITY
			*((u8 *)pValue) = (IS_92C_SERIAL(pHalData->VersionID) ||(pHalData->AntDivCfg==0))?_FALSE:_TRUE;
			#endif
			break;			
		case HAL_DEF_CURRENT_ANTENNA:
			#ifdef CONFIG_ANTENNA_DIVERSITY
			*(( u8*)pValue) = pHalData->CurAntenna;			
			#endif
			break;
		case HAL_DEF_DRVINFO_SZ:
			*(( u32*)pValue) = DRVINFO_SZ;
			break;
		case HAL_DEF_MAX_RECVBUF_SZ:
			*(( u32*)pValue) = MAX_RECVBUF_SZ;
			break;
		case HAL_DEF_RX_PACKET_OFFSET:
			*(( u32*)pValue) = RXDESC_SIZE + DRVINFO_SZ;
			break;
		case HAL_DEF_DBG_DUMP_RXPKT:
			*(( u8*)pValue) = pHalData->bDumpRxPkt;
			break;
		case HW_VAR_MAX_RX_AMPDU_FACTOR:
			*(( u32*)pValue) = MAX_AMPDU_FACTOR_64K;
			break;
		default:
			bResult = GetHalDefVar(Adapter, eVariable, pValue);
			break;
	}

	return bResult;
}




//
//	Description:
//		Change default setting of specified variable.
//
u8
SetHalDefVar8192CUsb(
	IN	PADAPTER				Adapter,
	IN	HAL_DEF_VARIABLE		eVariable,
	IN	PVOID					pValue
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _SUCCESS;

	switch(eVariable)
	{
		case HAL_DEF_DBG_DUMP_RXPKT:
			pHalData->bDumpRxPkt = *(( u8*)pValue);
			break;
		case HAL_DEF_DBG_DM_FUNC:
			{
				u8 dm_func = *(( u8*)pValue);
				struct dm_priv	*pdmpriv = &pHalData->dmpriv;
				DM_ODM_T 		*podmpriv = &pHalData->odmpriv;
				
				if(dm_func == 0){ //disable all dynamic func
					podmpriv->SupportAbility = DYNAMIC_FUNC_DISABLE;
					DBG_8192C("==> Disable all dynamic function...\n");
				}
				else if(dm_func == 1){//disable DIG
					podmpriv->SupportAbility  &= (~DYNAMIC_BB_DIG);
					DBG_8192C("==> Disable DIG...\n");
				}
				else if(dm_func == 2){//disable High power
					podmpriv->SupportAbility  &= (~DYNAMIC_BB_DYNAMIC_TXPWR);
				}
				else if(dm_func == 3){//disable tx power tracking
					podmpriv->SupportAbility  &= (~DYNAMIC_RF_CALIBRATION);
					DBG_8192C("==> Disable tx power tracking...\n");
				}
				else if(dm_func == 4){//disable BT coexistence
					pdmpriv->DMFlag &= (~DYNAMIC_FUNC_BT);
				}
				else if(dm_func == 5){//disable antenna diversity
					podmpriv->SupportAbility  &= (~DYNAMIC_BB_ANT_DIV);
				}				
				else if(dm_func == 6){//turn on all dynamic func
					if(!(podmpriv->SupportAbility  & DYNAMIC_BB_DIG))
					{						
						DIG_T	*pDigTable = &podmpriv->DM_DigTable;
						pDigTable->CurIGValue= rtw_read8(Adapter,0xc50);	
					}
					pdmpriv->DMFlag |= DYNAMIC_FUNC_BT;
					podmpriv->SupportAbility = DYNAMIC_ALL_FUNC_ENABLE;
					DBG_8192C("==> Turn on all dynamic function...\n");
				}			
			}
			break;
		default:
			bResult = SetHalDefVar(Adapter, eVariable, pValue);
			break;
	}

	return bResult;
}

/*
u32  _update_92cu_basic_rate(_adapter *padapter, unsigned int mask)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(padapter);
	unsigned int BrateCfg = 0;


	if(pHalData->VersionID != VERSION_TEST_CHIP_88C)
		BrateCfg = mask  & 0x15F;
	else	//for 88CU 46PING setting, Disable CCK 2M, 5.5M, Others must tuning
		BrateCfg = mask  & 0x159;

	BrateCfg |= 0x01; // default enable 1M ACK rate					

	return BrateCfg;
}
*/
void _update_response_rate(_adapter *padapter,unsigned int mask)
{
	u8	RateIndex = 0;
	// Set RRSR rate table.
	rtw_write8(padapter, REG_RRSR, mask&0xff);
	rtw_write8(padapter,REG_RRSR+1, (mask>>8)&0xff);

	// Set RTS initial rate
	while(mask > 0x1)
	{
		mask = (mask>> 1);
		RateIndex++;
	}
	rtw_write8(padapter, REG_INIRTS_RATE_SEL, RateIndex);
}

void UpdateHalRAMask8192CUsb(PADAPTER padapter, u32 mac_id,u8 rssi_level )
{
	//volatile unsigned int result;
	u8	init_rate=0;
	u8	networkType, raid;	
	u32	mask,rate_bitmap;
	u8	shortGIrate = _FALSE;
	int	supportRateNum = 0;
	struct sta_info	*psta;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);

	if (mac_id >= NUM_STA) //CAM_SIZE
	{
		return;
	}

	psta = pmlmeinfo->FW_sta_info[mac_id].psta;
	if(psta == NULL)
	{
		return;
	}

	switch (mac_id)
	{
		case 0:// for infra mode
#ifdef CONFIG_CONCURRENT_MODE
		case 2:// first station uses macid=0, second station uses macid=2
#endif
			supportRateNum = rtw_get_rateset_len(cur_network->SupportedRates);
			networkType = judge_network_type(padapter, cur_network->SupportedRates, supportRateNum) & 0xf;
			//pmlmeext->cur_wireless_mode = networkType;
			raid = networktype_to_raid(networkType);
						
			mask = update_supported_rate(cur_network->SupportedRates, supportRateNum);
			mask |= (pmlmeinfo->HT_enable)? update_MSC_rate(&(pmlmeinfo->HT_caps)): 0;
			
			
			if (support_short_GI(padapter, &(pmlmeinfo->HT_caps)))
			{
				shortGIrate = _TRUE;
			}
			
			break;

		case 1://for broadcast/multicast
			supportRateNum = rtw_get_rateset_len(pmlmeinfo->FW_sta_info[mac_id].SupportedRates);
			if(pmlmeext->cur_wireless_mode & WIRELESS_11B)
				networkType = WIRELESS_11B;
			else
				networkType = WIRELESS_11G;
			raid = networktype_to_raid(networkType);

			mask = update_basic_rate(cur_network->SupportedRates, supportRateNum);
			
			break;

		default: //for each sta in IBSS
			supportRateNum = rtw_get_rateset_len(pmlmeinfo->FW_sta_info[mac_id].SupportedRates);
			networkType = judge_network_type(padapter, pmlmeinfo->FW_sta_info[mac_id].SupportedRates, supportRateNum) & 0xf;
			//pmlmeext->cur_wireless_mode = networkType;
			raid = networktype_to_raid(networkType);
			
			mask = update_supported_rate(cur_network->SupportedRates, supportRateNum);
			

			//todo: support HT in IBSS
			
			break;
	}
	
	//mask &=0x0fffffff;
	rate_bitmap = 0x0fffffff;	
#ifdef	CONFIG_ODM_REFRESH_RAMASK
	{				
		rate_bitmap = ODM_Get_Rate_Bitmap(&pHalData->odmpriv,mac_id,mask,rssi_level);
		printk("%s => mac_id:%d, networkType:0x%02x, mask:0x%08x\n\t ==> rssi_level:%d, rate_bitmap:0x%08x\n",
			__FUNCTION__,mac_id,networkType,mask,rssi_level,rate_bitmap);
	}
#endif
	
	mask &= rate_bitmap;
	mask |= ((raid<<28)&0xf0000000);

	
	init_rate = get_highest_rate_idx(mask)&0x3f;
	
	if(pHalData->fw_ractrl == _TRUE)
	{
		u8 arg = 0;

		//arg = (cam_idx-4)&0x1f;//MACID
		arg = mac_id&0x1f;//MACID
		
		arg |= BIT(7);
		
		if (shortGIrate==_TRUE)
			arg |= BIT(5);

		DBG_871X("update raid entry, mask=0x%x, arg=0x%x\n", mask, arg);

		rtl8192c_set_raid_cmd(padapter, mask, arg);	
		
	}
	else
	{
		if (shortGIrate==_TRUE)
			init_rate |= BIT(6);

		rtw_write8(padapter, (REG_INIDATA_RATE_SEL+mac_id), init_rate);		
	}


	//set ra_id
	psta->raid = raid;
	psta->init_rate = init_rate;

	//set correct initial date rate for each mac_id
	pdmpriv->INIDATA_RATE[mac_id] = init_rate;
}

static void rtl8723au_init_default_value(PADAPTER padapter)
{
	rtl8723a_init_default_value(padapter);
}

static u8 rtl8192cu_ps_func(PADAPTER Adapter,HAL_INTF_PS_FUNC efunc_id, u8 *val)
{	
	u8 bResult = _TRUE;
	switch(efunc_id){

		#if defined(CONFIG_AUTOSUSPEND) && defined(SUPPORT_HW_RFOFF_DETECTED)
		case HAL_USB_SELECT_SUSPEND:
			{
				u8 bfwpoll = *(( u8*)val);
				rtl8192c_set_FwSelectSuspend_cmd(Adapter,bfwpoll ,500);//note fw to support hw power down ping detect
			}
			break;
		#endif //CONFIG_AUTOSUSPEND && SUPPORT_HW_RFOFF_DETECTED

		default:
			break;
	}
	return bResult;
}

void rtl8723au_set_hal_ops(_adapter * padapter)
{
	struct hal_ops	*pHalFunc = &padapter->HalFunc;

_func_enter_;

#ifdef CONFIG_CONCURRENT_MODE
	if(padapter->isprimary)
#endif //CONFIG_CONCURRENT_MODE
	{
		//set hardware operation functions
		padapter->HalData = rtw_zmalloc(sizeof(HAL_DATA_TYPE));
		if(padapter->HalData == NULL){
			DBG_8192C("cant not alloc memory for HAL DATA \n");
		}
	}

	//_rtw_memset(padapter->HalData, 0, sizeof(HAL_DATA_TYPE));
	padapter->hal_data_sz = sizeof(HAL_DATA_TYPE);

	pHalFunc->hal_init = &rtl8723au_hal_init;
	pHalFunc->hal_deinit = &rtl8723au_hal_deinit;

	//pHalFunc->free_hal_data = &rtl8192c_free_hal_data;

	pHalFunc->inirp_init = &rtl8723au_inirp_init;
	pHalFunc->inirp_deinit = &rtl8723au_inirp_deinit;

	pHalFunc->init_xmit_priv = &rtl8192cu_init_xmit_priv;
	pHalFunc->free_xmit_priv = &rtl8192cu_free_xmit_priv;

	pHalFunc->init_recv_priv = &rtl8192cu_init_recv_priv;
	pHalFunc->free_recv_priv = &rtl8192cu_free_recv_priv;
#ifdef CONFIG_SW_LED
	pHalFunc->InitSwLeds = &rtl8723au_InitSwLeds;
	pHalFunc->DeInitSwLeds = &rtl8723au_DeInitSwLeds;
#else //case of hw led or no led
	pHalFunc->InitSwLeds = NULL;
	pHalFunc->DeInitSwLeds = NULL;	
#endif//CONFIG_SW_LED

	pHalFunc->init_default_value = &rtl8723au_init_default_value;
	pHalFunc->intf_chip_configure = &rtl8192cu_interface_configure;
	pHalFunc->read_adapter_info = &ReadAdapterInfo8723AU;

	//pHalFunc->set_bwmode_handler = &PHY_SetBWMode8192C;
	//pHalFunc->set_channel_handler = &PHY_SwChnl8192C;

	//pHalFunc->hal_dm_watchdog = &rtl8192c_HalDmWatchDog;

	pHalFunc->SetHwRegHandler = &SetHwReg8723AU;
	pHalFunc->GetHwRegHandler = &GetHwReg8723AU;
	pHalFunc->GetHalDefVarHandler = &GetHalDefVar8192CUsb;
	pHalFunc->SetHalDefVarHandler = &SetHalDefVar8192CUsb;

	pHalFunc->UpdateRAMaskHandler = &UpdateHalRAMask8192CUsb;

	pHalFunc->hal_xmit = &rtl8192cu_hal_xmit;
	pHalFunc->mgnt_xmit = &rtl8192cu_mgnt_xmit;
	pHalFunc->hal_xmitframe_enqueue = &rtl8723au_hal_xmitframe_enqueue;

#ifdef CONFIG_HOSTAPD_MLME
	pHalFunc->hostap_mgnt_xmit_entry = &rtl8192cu_hostap_mgnt_xmit_entry;
#endif
	pHalFunc->interface_ps_func = &rtl8192cu_ps_func;

	rtl8723a_set_hal_ops(pHalFunc);

_func_exit_;

}

