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

#include <drv_types.h>
#include <rtl8188e_hal.h>
#include "hal_com_h2c.h"

#ifndef CONFIG_USB_HCI

#error "CONFIG_USB_HCI shall be on!\n"

#endif


static VOID
_ConfigNormalChipOutEP_8188E(
	IN	PADAPTER	pAdapter,
	IN	u8		NumOutPipe
	)
{	
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);

	switch(NumOutPipe){
		case 	3:
				pHalData->OutEpQueueSel=TX_SELE_HQ| TX_SELE_LQ|TX_SELE_NQ;
				pHalData->OutEpNumber=3;
				break;
		case 	2:
				pHalData->OutEpQueueSel=TX_SELE_HQ| TX_SELE_NQ;
				pHalData->OutEpNumber=2;
				break;
		case 	1:
				pHalData->OutEpQueueSel=TX_SELE_HQ;
				pHalData->OutEpNumber=1;
				break;
		default:				
				break;
			
	}
	DBG_871X("%s OutEpQueueSel(0x%02x), OutEpNumber(%d) \n",__FUNCTION__,pHalData->OutEpQueueSel,pHalData->OutEpNumber );

}

static BOOLEAN HalUsbSetQueuePipeMapping8188EUsb(
	IN	PADAPTER	pAdapter,
	IN	u8		NumInPipe,
	IN	u8		NumOutPipe
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);
	BOOLEAN			result		= _FALSE;

	_ConfigNormalChipOutEP_8188E(pAdapter, NumOutPipe);
	
	// Normal chip with one IN and one OUT doesn't have interrupt IN EP.
	if(1 == pHalData->OutEpNumber){
		if(1 != NumInPipe){
			return result;
		}
	}

	// All config other than above support one Bulk IN and one Interrupt IN.
	//if(2 != NumInPipe){
	//	return result;
	//}

	result = Hal_MappingOutPipe(pAdapter, NumOutPipe);
	
	return result;

}

void rtl8188eu_interface_configure(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);

	if (IS_HIGH_SPEED_USB(padapter))
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

	HalUsbSetQueuePipeMapping8188EUsb(padapter,
				pdvobjpriv->RtNumInPipes, pdvobjpriv->RtNumOutPipes);

}

static u32 _InitPowerOn_8188EU(_adapter *padapter)
{
	u16 value16;
	// HW Power on sequence
	u8 bMacPwrCtrlOn=_FALSE;

	
	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if(bMacPwrCtrlOn == _TRUE)	
		return _SUCCESS;
	
	if(!HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, Rtl8188E_NIC_PWR_ON_FLOW))
	{
		DBG_871X(KERN_ERR "%s: run power on flow fail\n", __func__);
		return _FAIL;	
	}

	// Enable MAC DMA/WMAC/SCHEDULE/SEC block
	// Set CR bit10 to enable 32k calibration. Suggested by SD1 Gimmy. Added by tynli. 2011.08.31.
	rtw_write16(padapter, REG_CR, 0x00);  //suggseted by zhouzhou, by page, 20111230


		// Enable MAC DMA/WMAC/SCHEDULE/SEC block
	value16 = rtw_read16(padapter, REG_CR);
	value16 |= (HCI_TXDMA_EN | HCI_RXDMA_EN | TXDMA_EN | RXDMA_EN
				| PROTOCOL_EN | SCHEDULE_EN | ENSEC | CALTMR_EN);
	// for SDIO - Set CR bit10 to enable 32k calibration. Suggested by SD1 Gimmy. Added by tynli. 2011.08.31.
	
	rtw_write16(padapter, REG_CR, value16);

	bMacPwrCtrlOn = _TRUE;
	rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);


	return _SUCCESS;

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


static void _InitPABias(_adapter *padapter)
{
	HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(padapter);
	u8			pa_setting;
	BOOLEAN		is92C = IS_92C_SERIAL(pHalData->VersionID);
	
	//FIXED PA current issue
	//efuse_one_byte_read(padapter, 0x1FA, &pa_setting);
	pa_setting = EFUSE_Read1Byte(padapter, 0x1FA);

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("_InitPABias 0x1FA 0x%x \n",pa_setting));

	if(!(pa_setting & BIT0))
	{
		PHY_SetRFReg(padapter, RF_PATH_A, 0x15, 0x0FFFFF, 0x0F406);
		PHY_SetRFReg(padapter, RF_PATH_A, 0x15, 0x0FFFFF, 0x4F406);		
		PHY_SetRFReg(padapter, RF_PATH_A, 0x15, 0x0FFFFF, 0x8F406);		
		PHY_SetRFReg(padapter, RF_PATH_A, 0x15, 0x0FFFFF, 0xCF406);		
		//RT_TRACE(COMP_INIT, DBG_LOUD, ("PA BIAS path A\n"));
	}	

	if(!(pa_setting & BIT1) && is92C)
	{
		PHY_SetRFReg(padapter,RF_PATH_B, 0x15, 0x0FFFFF, 0x0F406);
		PHY_SetRFReg(padapter,RF_PATH_B, 0x15, 0x0FFFFF, 0x4F406);
		PHY_SetRFReg(padapter,RF_PATH_B, 0x15, 0x0FFFFF, 0x8F406);
		PHY_SetRFReg(padapter,RF_PATH_B, 0x15, 0x0FFFFF, 0xCF406);
		//RT_TRACE(COMP_INIT, DBG_LOUD, ("PA BIAS path B\n"));
	}

	if(!(pa_setting & BIT4))
	{
		pa_setting = rtw_read8(padapter, 0x16);
		pa_setting &= 0x0F;
		rtw_write8(padapter, 0x16, pa_setting | 0x80);
		rtw_write8(padapter, 0x16, pa_setting | 0x90);		
	}
}
#ifdef CONFIG_BT_COEXIST
static void _InitBTCoexist(_adapter *padapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);
	u8 u1Tmp;

	if(pbtpriv->BT_Coexist && pbtpriv->BT_CoexistType == BT_CSR_BC4)
	{

//#if MP_DRIVER != 1
	if (padapter->registrypriv.mp_mode == 0)
	{	
		if(pbtpriv->BT_Ant_isolation)
		{
			rtw_write8( padapter,REG_GPIO_MUXCFG, 0xa0);
			DBG_8192C("BT write 0x%x = 0x%x\n", REG_GPIO_MUXCFG, 0xa0);
		}
	}
//#endif		

		u1Tmp = rtw_read8(padapter, 0x4fd) & BIT0;
		u1Tmp = u1Tmp | 
				((pbtpriv->BT_Ant_isolation==1)?0:BIT1) | 
				((pbtpriv->BT_Service==BT_SCO)?0:BIT2);
		rtw_write8( padapter, 0x4fd, u1Tmp);
		DBG_8192C("BT write 0x%x = 0x%x for non-isolation\n", 0x4fd, u1Tmp);
		
		
		rtw_write32(padapter, REG_BT_COEX_TABLE+4, 0xaaaa9aaa);
		DBG_8192C("BT write 0x%x = 0x%x\n", REG_BT_COEX_TABLE+4, 0xaaaa9aaa);
		
		rtw_write32(padapter, REG_BT_COEX_TABLE+8, 0xffbd0040);
		DBG_8192C("BT write 0x%x = 0x%x\n", REG_BT_COEX_TABLE+8, 0xffbd0040);

		rtw_write32(padapter,  REG_BT_COEX_TABLE+0xc, 0x40000010);
		DBG_8192C("BT write 0x%x = 0x%x\n", REG_BT_COEX_TABLE+0xc, 0x40000010);

		//Config to 1T1R
		u1Tmp =  rtw_read8(padapter,rOFDM0_TRxPathEnable);
		u1Tmp &= ~(BIT1);
		rtw_write8( padapter, rOFDM0_TRxPathEnable, u1Tmp);
		DBG_8192C("BT write 0xC04 = 0x%x\n", u1Tmp);
			
		u1Tmp = rtw_read8(padapter, rOFDM1_TRxPathEnable);
		u1Tmp &= ~(BIT1);
		rtw_write8( padapter, rOFDM1_TRxPathEnable, u1Tmp);
		DBG_8192C("BT write 0xD04 = 0x%x\n", u1Tmp);

	}
}
#endif



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
	u32	imr,imr_ex;
	u8  usb_opt;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

	//HISR write one to clear
	rtw_write32(Adapter, REG_HISR_88E, 0xFFFFFFFF);
	// HIMR - 	
	imr = IMR_PSTIMEOUT_88E | IMR_TBDER_88E | IMR_CPWM_88E | IMR_CPWM2_88E ;		
	rtw_write32(Adapter, REG_HIMR_88E, imr);
	pHalData->IntrMask[0]=imr;
	
	imr_ex = IMR_TXERR_88E | IMR_RXERR_88E | IMR_TXFOVW_88E |IMR_RXFOVW_88E;	
	rtw_write32(Adapter, REG_HIMRE_88E, imr_ex);
	pHalData->IntrMask[1]=imr_ex;
	
#ifdef CONFIG_SUPPORT_USB_INT
	// REG_USB_SPECIAL_OPTION - BIT(4)
	// 0; Use interrupt endpoint to upload interrupt pkt
	// 1; Use bulk endpoint to upload interrupt pkt,	
	usb_opt = rtw_read8(Adapter, REG_USB_SPECIAL_OPTION);

	if((IS_FULL_SPEED_USB(Adapter))
		#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
		|| pHalData->RtIntInPipe == 0x05
		#endif
	)
		usb_opt = usb_opt & (~INT_BULK_SEL);
	else	
		usb_opt = usb_opt | (INT_BULK_SEL);

	rtw_write8(Adapter, REG_USB_SPECIAL_OPTION, usb_opt );			

#endif//CONFIG_SUPPORT_USB_INT
	
}


static VOID
_InitQueueReservedPage(
	IN  PADAPTER Adapter
	)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	struct registry_priv	*pregistrypriv = &Adapter->registrypriv;
	u32			outEPNum	= (u32)pHalData->OutEpNumber;
	u32			numHQ		= NORMAL_PAGE_NUM_HPQ_88E;
	u32			numLQ		= NORMAL_PAGE_NUM_LPQ_88E;
	u32			numNQ		= NORMAL_PAGE_NUM_NPQ_88E;
	u32			numPubQ	= 0x00;
	u32			value32;
	u8			value8;
	BOOLEAN			bWiFiConfig	= pregistrypriv->wifi_spec;

	if(bWiFiConfig || pregistrypriv->qos_opt_enable)
	{
		if (pHalData->OutEpQueueSel & TX_SELE_HQ)
		{
			numHQ =  WMM_NORMAL_PAGE_NUM_HPQ_88E;
		}

		if (pHalData->OutEpQueueSel & TX_SELE_LQ)
		{
			numLQ = WMM_NORMAL_PAGE_NUM_LPQ_88E;
		}

		// NOTE: This step shall be proceed before writting REG_RQPN.
		if (pHalData->OutEpQueueSel & TX_SELE_NQ) {
			numNQ = WMM_NORMAL_PAGE_NUM_NPQ_88E;
		}
	}

	value8 = (u8)_NPQ(numNQ);
	rtw_write8(Adapter, REG_RQPN_NPQ, value8);

	numPubQ = TX_TOTAL_PAGE_NUMBER_88E - numHQ - numLQ - numNQ;

	// TX DMA
	value32 = _HPQ(numHQ) | _LPQ(numLQ) | _PUBQ(numPubQ) | LD_RQPN;
	rtw_write32(Adapter, REG_RQPN, value32);
}

static VOID
_InitTxBufferBoundary(
	IN PADAPTER Adapter,
	IN u8 txpktbuf_bndy
	)
{	
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	//u16	txdmactrl;

	rtw_write8(Adapter, REG_BCNQ_BDNY, txpktbuf_bndy);
	rtw_write8(Adapter, REG_MGQ_BDNY, txpktbuf_bndy);
	rtw_write8(Adapter, REG_WMAC_LBK_BF_HD, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TRXFF_BNDY, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TDECTRL+1, txpktbuf_bndy);

}

static VOID
_InitPageBoundary(
	IN  PADAPTER Adapter
	)
{
	// RX Page Boundary	
	// 	
	u16 rxff_bndy = MAX_RX_DMA_BUFFER_SIZE_88E-1;

	#if 0

	// RX Page Boundary
	//srand(static_cast<unsigned int>(time(NULL)) );
	if(bSupportRemoteWakeUp)
	{
		Offset = MAX_RX_DMA_BUFFER_SIZE_88E+MAX_TX_REPORT_BUFFER_SIZE-MAX_SUPPORT_WOL_PATTERN_NUM(Adapter)*WKFMCAM_SIZE;
		Offset = Offset / 128; // RX page size = 128 byte
		rxff_bndy= (Offset*128) -1;	
	}
	else
		
	#endif
	rtw_write16(Adapter, (REG_TRXFF_BNDY + 2), rxff_bndy);
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
	u16 value16	= (rtw_read16(Adapter, REG_TRXDMA_CTRL) & 0x7);

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
_InitQueuePriority(
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
	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AP);

	rtw_write32(Adapter, REG_CR, value32);
//	RASSERT(pIoBase->rtw_read8(REG_CR + 2) == 0x2);
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
	//pHalData->ReceiveConfig = 
	//RCR_AAP | RCR_APM | RCR_AM | RCR_AB |RCR_CBSSID_DATA| RCR_CBSSID_BCN| RCR_APP_ICV | RCR_AMF | RCR_HTC_LOC_CTRL | RCR_APP_MIC | RCR_APP_PHYSTS;	  
	 // don't turn on AAP, it will allow all packets to driver
        pHalData->ReceiveConfig = RCR_APM | RCR_AM | RCR_AB |RCR_CBSSID_DATA| RCR_CBSSID_BCN| RCR_APP_ICV | RCR_AMF | RCR_HTC_LOC_CTRL | RCR_APP_MIC | RCR_APP_PHYST_RXFF;	 
	 
#if (1 == RTL8188E_RX_PACKET_INCLUDE_CRC)
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


static VOID
_InitBeaconMaxError(
	IN  PADAPTER	Adapter,
	IN	BOOLEAN		InfraMode
	)
{

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
			rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH+1, pHalData->UsbRxAggPageTimeout);
			break;
		case USB_RX_AGG_USB:
			rtw_write8(Adapter, REG_USB_AGG_TH, pHalData->UsbRxAggBlockCount);
			rtw_write8(Adapter, REG_USB_AGG_TO, pHalData->UsbRxAggBlockTimeout);
			break;
		case USB_RX_AGG_MIX:
			rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH, pHalData->UsbRxAggPageCount);
			rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH+1, (pHalData->UsbRxAggPageTimeout& 0x1F));//0x280[12:8]
			
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
VOID
HalRxAggr8188EUsb(
	IN  PADAPTER Adapter,
	IN BOOLEAN	Value
	)
{
#if 0//USB_RX_AGGREGATION_92C

	PMGNT_INFO		pMgntInfo = &Adapter->MgntInfo;
	u1Byte			valueDMATimeout;
	u1Byte			valueDMAPageCount;
	u1Byte			valueUSBTimeout;
	u1Byte			valueUSBBlockCount;

	// selection to prevent bad TP.
	if( IS_WIRELESS_MODE_B(Adapter) || IS_WIRELESS_MODE_G(Adapter) || IS_WIRELESS_MODE_A(Adapter)|| pMgntInfo->bWiFiConfg)
	{
		// 2010.04.27 hpfan
		// Adjust RxAggrTimeout to close to zero disable RxAggr, suggested by designer
		// Timeout value is calculated by 34 / (2^n)
		valueDMATimeout = 0x0f;
		valueDMAPageCount = 0x01;
		valueUSBTimeout = 0x0f;
		valueUSBBlockCount = 0x01;
		rtw_hal_set_hwreg(Adapter, HW_VAR_RX_AGGR_PGTO, (pu1Byte)&valueDMATimeout);
		rtw_hal_set_hwreg(Adapter, HW_VAR_RX_AGGR_PGTH, (pu1Byte)&valueDMAPageCount);
		rtw_hal_set_hwreg(Adapter, HW_VAR_RX_AGGR_USBTO, (pu1Byte)&valueUSBTimeout);
		rtw_hal_set_hwreg(Adapter, HW_VAR_RX_AGGR_USBTH, (pu1Byte)&valueUSBBlockCount);
	}
	else
	{
		rtw_hal_set_hwreg(Adapter, HW_VAR_RX_AGGR_USBTO, (pu1Byte)&pMgntInfo->RegRxAggBlockTimeout);
		rtw_hal_set_hwreg(Adapter, HW_VAR_RX_AGGR_USBTH, (pu1Byte)&pMgntInfo->RegRxAggBlockCount);
	}

#endif
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
	PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);

	//pHalData->UsbRxHighSpeedMode = FALSE;
	// How to measure the RX speed? We assume that when traffic is more than 
	if (pMgntInfo->bRegAggDMEnable == FALSE)
	{
		return;	// Inf not support.
	}
	
	
	if (pMgntInfo->LinkDetectInfo.bHigherBusyRxTraffic == TRUE && 
		pHalData->UsbRxHighSpeedMode == FALSE)
	{
		pHalData->UsbRxHighSpeedMode = TRUE;
		RT_TRACE(COMP_INIT, DBG_LOUD, ("UsbAggModeSwitchCheck to HIGH\n"));
	}
	else if (pMgntInfo->LinkDetectInfo.bHigherBusyRxTraffic == FALSE && 
		pHalData->UsbRxHighSpeedMode == TRUE)
	{
		pHalData->UsbRxHighSpeedMode = FALSE;
		RT_TRACE(COMP_INIT, DBG_LOUD, ("UsbAggModeSwitchCheck to LOW\n"));
	}
	else
	{
		return; 
	}
	

#if USB_RX_AGGREGATION_92C
	if (pHalData->UsbRxHighSpeedMode == TRUE)	
	{
		// 2010/12/10 MH The parameter is tested by SD1 engineer and SD3 channel emulator.
		// USB mode
#if (RT_PLATFORM == PLATFORM_LINUX)
		if (pMgntInfo->LinkDetectInfo.bTxBusyTraffic)
		{
			pHalData->RxAggBlockCount	= 16;
			pHalData->RxAggBlockTimeout	= 7;
		}
		else
#endif
		{
			pHalData->RxAggBlockCount	= 40;
			pHalData->RxAggBlockTimeout	= 5;
		}
		// Mix mode
		pHalData->RxAggPageCount	= 72;
		pHalData->RxAggPageTimeout	= 6;		
	}
	else
	{
		// USB mode
		pHalData->RxAggBlockCount	= pMgntInfo->RegRxAggBlockCount;
		pHalData->RxAggBlockTimeout	= pMgntInfo->RegRxAggBlockTimeout;	
		// Mix mode
		pHalData->RxAggPageCount		= pMgntInfo->RegRxAggPageCount;
		pHalData->RxAggPageTimeout	= pMgntInfo->RegRxAggPageTimeout;	
	}

	if (pHalData->RxAggBlockCount > MAX_RX_AGG_BLKCNT)
		pHalData->RxAggBlockCount = MAX_RX_AGG_BLKCNT;
#if (OS_WIN_FROM_VISTA(OS_VERSION)) || (RT_PLATFORM == PLATFORM_LINUX)	// do not support WINXP to prevent usbehci.sys BSOD
	if (IS_WIRELESS_MODE_N_24G(Adapter) || IS_WIRELESS_MODE_N_5G(Adapter))
	{
		//
		// 2010/12/24 MH According to V1012 QC IOT test, XP BSOD happen when running chariot test
		// with the aggregation dynamic change!! We need to disable the function to prevent it is broken
		// in usbehci.sys.
		//
		usb_AggSettingRxUpdate_8188E(Adapter);

		// 2010/12/27 MH According to designer's suggstion, we can only modify Timeout value. Otheriwse
		// there might many HW incorrect behavior, the XP BSOD at usbehci.sys may be relative to the 
		// issue. Base on the newest test, we can not enable block cnt > 30, otherwise XP usbehci.sys may
		// BSOD.
	}
#endif
	
#endif
#endif
}	// USB_AggModeSwitch

static VOID
_InitOperationMode(
	IN	PADAPTER			Adapter
	)
{
#if 0//gtest
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
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
			regBwOpMode = BW_OPMODE_5G |BW_OPMODE_20MHZ;
			regRATR = RATE_ALL_OFDM_AG;
			regRRSR = RATE_ALL_OFDM_AG;
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
			regBwOpMode = BW_OPMODE_5G;
			regRATR = RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
			regRRSR = RATE_ALL_OFDM_AG;
			break;
			
		default: //for MacOSX compiler warning.
			break;
	}

	// Ziv ????????
	//PlatformEFIOWrite4Byte(Adapter, REG_INIRTS_RATE_SEL, regRRSR);
	PlatformEFIOWrite1Byte(Adapter, REG_BWOPMODE, regBwOpMode);
#endif
}


 static VOID
_InitBeaconParameters(
	IN  PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	rtw_write16(Adapter, REG_BCN_CTRL, 0x1010);

	// TODO: Remove these magic number
	rtw_write16(Adapter, REG_TBTT_PROHIBIT,0x6404);// ms
	rtw_write8(Adapter, REG_DRVERLYINT, DRIVER_EARLY_INT_TIME_8188E);// 5ms
	rtw_write8(Adapter, REG_BCNDMATIM, BCN_DMA_ATIME_INT_TIME_8188E); // 2ms

	// Suggested by designer timchen. Change beacon AIFS to the largest number
	// beacause test chip does not contension before sending beacon. by tynli. 2009.11.03
	rtw_write16(Adapter, REG_BCNTCFG, 0x660F);

	pHalData->RegBcnCtrlVal = rtw_read8(Adapter, REG_BCN_CTRL);
	pHalData->RegTxPause = rtw_read8(Adapter, REG_TXPAUSE); 
	pHalData->RegFwHwTxQCtrl = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL+2);
	pHalData->RegReg542 = rtw_read8(Adapter, REG_TBTT_PROHIBIT+2);
	pHalData->RegCR_1 = rtw_read8(Adapter, REG_CR+1);
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


static VOID
_BeaconFunctionEnable(
	IN	PADAPTER		Adapter,
	IN	BOOLEAN			Enable,
	IN	BOOLEAN			Linked
	)
{
	rtw_write8(Adapter, REG_BCN_CTRL, (BIT4 | BIT3 | BIT1));
	//SetBcnCtrlReg(Adapter, (BIT4 | BIT3 | BIT1), 0x00);
	//RT_TRACE(COMP_BEACON, DBG_LOUD, ("_BeaconFunctionEnable 0x550 0x%x\n", PlatformEFIORead1Byte(Adapter, 0x550)));			

	rtw_write8(Adapter, REG_RD_CTRL+1, 0x6F);	
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

static VOID _RfPowerSave(
	IN	PADAPTER		Adapter
	)
{
#if 0
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);
	PMGNT_INFO		pMgntInfo	= &(Adapter->MgntInfo);
	u1Byte			eRFPath;

#if (DISABLE_BB_RF)
	return;
#endif

	if(pMgntInfo->RegRfOff == TRUE){ // User disable RF via registry.
		RT_TRACE((COMP_INIT|COMP_RF), DBG_LOUD, ("InitializeAdapter8192CUsb(): Turn off RF for RegRfOff.\n"));
		MgntActSet_RF_State(Adapter, eRfOff, RF_CHANGE_BY_SW);
		// Those action will be discard in MgntActSet_RF_State because off the same state
		for(eRFPath = 0; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
			PHY_SetRFReg(Adapter, eRFPath, 0x4, 0xC00, 0x0);
	}
	else if(pMgntInfo->RfOffReason > RF_CHANGE_BY_PS){ // H/W or S/W RF OFF before sleep.
		RT_TRACE((COMP_INIT|COMP_RF), DBG_LOUD, ("InitializeAdapter8192CUsb(): Turn off RF for RfOffReason(%ld).\n", pMgntInfo->RfOffReason));
		MgntActSet_RF_State(Adapter, eRfOff, pMgntInfo->RfOffReason);
	}
	else{
		pHalData->eRFPowerState = eRfOn;
		pMgntInfo->RfOffReason = 0; 
		if(Adapter->bInSetPower || Adapter->bResetInProgress)
			PlatformUsbEnableInPipes(Adapter);
		RT_TRACE((COMP_INIT|COMP_RF), DBG_LOUD, ("InitializeAdapter8192CUsb(): RF is on.\n"));
	}
#endif
}

enum {
	Antenna_Lfet = 1,
	Antenna_Right = 2,	
};

static VOID
_InitAntenna_Selection(IN	PADAPTER Adapter)
{

	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

	if(pHalData->AntDivCfg==0)
		return;
	DBG_8192C("==>  %s ....\n",__FUNCTION__);		
		
	rtw_write32(Adapter, REG_LEDCFG0, rtw_read32(Adapter, REG_LEDCFG0)|BIT23);	
	PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, BIT13, 0x01);
		
	if(PHY_QueryBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, 0x300) == MAIN_ANT)
		pHalData->CurAntenna = MAIN_ANT;
	else
		pHalData->CurAntenna = AUX_ANT;
	DBG_8192C("%s,Cur_ant:(%x)%s\n",__FUNCTION__,pHalData->CurAntenna,(pHalData->CurAntenna == MAIN_ANT)?"MAIN_ANT":"AUX_ANT");
			

}

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
#if 0
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
#endif
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
HwSuspendModeEnable_88eu(
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
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(pAdapter);
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(pAdapter);
	u8	val8;
	rt_rf_power_state rfpowerstate = rf_off;

	if(pwrctl->bHWPowerdown)
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

u32 rtl8188eu_hal_init(PADAPTER Adapter)
{
	u8	value8 = 0;
	u16  value16;
	u8	txpktbuf_bndy;
	u32	status = _SUCCESS;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	struct pwrctrl_priv		*pwrctrlpriv = adapter_to_pwrctl(Adapter);
	struct registry_priv	*pregistrypriv = &Adapter->registrypriv;
	
	rt_rf_power_state		eRfPowerStateToSet;
#ifdef CONFIG_BT_COEXIST
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);
#endif

	u32 init_start_time = rtw_get_current_time();


#ifdef DBG_HAL_INIT_PROFILING

	enum HAL_INIT_STAGES {
		HAL_INIT_STAGES_BEGIN = 0,
		HAL_INIT_STAGES_INIT_PW_ON,
		HAL_INIT_STAGES_MISC01,
		HAL_INIT_STAGES_DOWNLOAD_FW,
		HAL_INIT_STAGES_MAC,
		HAL_INIT_STAGES_BB,
		HAL_INIT_STAGES_RF,	
		HAL_INIT_STAGES_EFUSE_PATCH,
		HAL_INIT_STAGES_INIT_LLTT,	
		
		HAL_INIT_STAGES_MISC02,
		HAL_INIT_STAGES_TURN_ON_BLOCK,
		HAL_INIT_STAGES_INIT_SECURITY,
		HAL_INIT_STAGES_MISC11,
		HAL_INIT_STAGES_INIT_HAL_DM,
		//HAL_INIT_STAGES_RF_PS,
		HAL_INIT_STAGES_IQK,
		HAL_INIT_STAGES_PW_TRACK,
		HAL_INIT_STAGES_LCK,
		//HAL_INIT_STAGES_MISC21,
		//HAL_INIT_STAGES_INIT_PABIAS,
		#ifdef CONFIG_BT_COEXIST
		HAL_INIT_STAGES_BT_COEXIST,
		#endif
		//HAL_INIT_STAGES_ANTENNA_SEL,
		//HAL_INIT_STAGES_MISC31,
		HAL_INIT_STAGES_END,
		HAL_INIT_STAGES_NUM
	};

	char * hal_init_stages_str[] = {
		"HAL_INIT_STAGES_BEGIN",
		"HAL_INIT_STAGES_INIT_PW_ON",
		"HAL_INIT_STAGES_MISC01",
		"HAL_INIT_STAGES_DOWNLOAD_FW",		
		"HAL_INIT_STAGES_MAC",		
		"HAL_INIT_STAGES_BB",
		"HAL_INIT_STAGES_RF",
		"HAL_INIT_STAGES_EFUSE_PATCH",
		"HAL_INIT_STAGES_INIT_LLTT",
		"HAL_INIT_STAGES_MISC02",		
		"HAL_INIT_STAGES_TURN_ON_BLOCK",
		"HAL_INIT_STAGES_INIT_SECURITY",
		"HAL_INIT_STAGES_MISC11",
		"HAL_INIT_STAGES_INIT_HAL_DM",
		//"HAL_INIT_STAGES_RF_PS",
		"HAL_INIT_STAGES_IQK",
		"HAL_INIT_STAGES_PW_TRACK",
		"HAL_INIT_STAGES_LCK",
		//"HAL_INIT_STAGES_MISC21",
		#ifdef CONFIG_BT_COEXIST
		"HAL_INIT_STAGES_BT_COEXIST",
		#endif
		//"HAL_INIT_STAGES_ANTENNA_SEL",
		//"HAL_INIT_STAGES_MISC31",
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

#ifdef CONFIG_WOWLAN
	
	pwrctrlpriv->wowlan_wake_reason = rtw_read8(Adapter, REG_WOWLAN_WAKE_REASON);
	DBG_8192C("%s wowlan_wake_reason: 0x%02x\n", 
				__func__, pwrctrlpriv->wowlan_wake_reason);

	if(rtw_read8(Adapter, REG_MCUFWDL)&BIT7){ /*&&
		(pwrctrlpriv->wowlan_wake_reason & FWDecisionDisconnect)) {*/
		u8 reg_val=0;
		DBG_8192C("+Reset Entry+\n");
		rtw_write8(Adapter, REG_MCUFWDL, 0x00);
		_8051Reset88E(Adapter);
		//reset BB
		reg_val = rtw_read8(Adapter, REG_SYS_FUNC_EN);
		reg_val &= ~(BIT(0) | BIT(1));
		rtw_write8(Adapter, REG_SYS_FUNC_EN, reg_val);
		//reset RF
		rtw_write8(Adapter, REG_RF_CTRL, 0);
		//reset TRX path
		rtw_write16(Adapter, REG_CR, 0);
		//reset MAC, Digital Core
		reg_val = rtw_read8(Adapter, REG_SYS_FUNC_EN+1);
		reg_val &= ~(BIT(4) | BIT(7));
		rtw_write8(Adapter, REG_SYS_FUNC_EN+1, reg_val);
		reg_val = rtw_read8(Adapter, REG_SYS_FUNC_EN+1);
		reg_val |= BIT(4) | BIT(7);
		rtw_write8(Adapter, REG_SYS_FUNC_EN+1, reg_val);
		DBG_8192C("-Reset Entry-\n");
	}
#endif //CONFIG_WOWLAN

	if(pwrctrlpriv->bkeepfwalive)
	{
		_ps_open_RF(Adapter);

		if(pHalData->odmpriv.RFCalibrateInfo.bIQKInitialized){
//			PHY_IQCalibrate(padapter, _TRUE);
			PHY_IQCalibrate_8188E(Adapter,_TRUE);
		}
		else
		{
//			PHY_IQCalibrate(padapter, _FALSE);
			PHY_IQCalibrate_8188E(Adapter,_FALSE);
			pHalData->odmpriv.RFCalibrateInfo.bIQKInitialized = _TRUE;
		}

//		dm_CheckTXPowerTracking(padapter);
//		PHY_LCCalibrate(padapter);
		ODM_TXPowerTrackingCheck(&pHalData->odmpriv );
		PHY_LCCalibrate_8188E(&pHalData->odmpriv );

		goto exit;
	}
	

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_PW_ON);
	status = _InitPowerOn_8188EU(Adapter);
	if(status == _FAIL){
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init power on!\n"));
		goto exit;
	}

	// Set RF type for BB/RF configuration	
	_InitRFType(Adapter);//->_ReadRFType()

	// Save target channel
	// <Roger_Notes> Current Channel will be updated again later.
	pHalData->CurrentChannel = 6;//default set to 6
	if(pwrctrlpriv->reg_rfoff == _TRUE){
		pwrctrlpriv->rf_pwrstate = rf_off;
	}

	// 2010/08/09 MH We need to check if we need to turnon or off RF after detecting
	// HW GPIO pin. Before PHY_RFConfig8192C.
	//HalDetectPwrDownMode(Adapter);
	// 2010/08/26 MH If Efuse does not support sective suspend then disable the function.
	//HalDetectSelectiveSuspendMode(Adapter);

	if (!pregistrypriv->wifi_spec) {
		txpktbuf_bndy = TX_PAGE_BOUNDARY_88E;
	} else {
		// for WMM
		txpktbuf_bndy = WMM_NORMAL_TX_PAGE_BOUNDARY_88E;
	}
	
HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC01);
	_InitQueueReservedPage(Adapter);		
	_InitQueuePriority(Adapter);
	_InitPageBoundary(Adapter);	
	_InitTransferPageSize(Adapter);
	
#ifdef CONFIG_IOL_IOREG_CFG
	_InitTxBufferBoundary(Adapter, 0);		
#endif



HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_DOWNLOAD_FW);
#if (MP_DRIVER == 1)
	if (Adapter->registrypriv.mp_mode == 1)
	{
		_InitRxSetting(Adapter);
	}
#endif  //MP_DRIVER == 1
	{
	#if 0		
		Adapter->bFWReady = _FALSE; //because no fw for test chip	
		pHalData->fw_ractrl = _FALSE;
	#else


		status = rtl8188e_FirmwareDownload(Adapter, _FALSE);

		if (status != _SUCCESS) {
			DBG_871X("%s: Download Firmware failed!!\n", __FUNCTION__);
			Adapter->bFWReady = _FALSE;
			pHalData->fw_ractrl = _FALSE;
			return status;
		} else {
			RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Initializepadapter8192CSdio(): Download Firmware Success!!\n"));
			Adapter->bFWReady = _TRUE;
			pHalData->fw_ractrl = _FALSE;
		}
	#endif
	}


	rtl8188e_InitializeFirmwareVars(Adapter);


HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MAC);
#if (HAL_MAC_ENABLE == 1)
	status = PHY_MACConfig8188E(Adapter);
	if(status == _FAIL)
	{
		DBG_871X(" ### Failed to init MAC ...... \n ");				
		goto exit;
	}
#endif	

	//
	//d. Initialize BB related configurations.
	//
HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_BB);
#if (HAL_BB_ENABLE == 1)
	status = PHY_BBConfig8188E(Adapter);
	if(status == _FAIL)
	{
		DBG_871X(" ### Failed to init BB ...... \n ");
		goto exit;
	}
#endif


HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_RF);
#if (HAL_RF_ENABLE == 1)
	status = PHY_RFConfig8188E(Adapter);	
	if(status == _FAIL)
	{
		DBG_871X(" ### Failed to init RF ...... \n ");
		goto exit;
	}
#endif

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_EFUSE_PATCH);
#if defined(CONFIG_IOL_EFUSE_PATCH)		
	status = rtl8188e_iol_efuse_patch(Adapter);
	if(status == _FAIL){	
		DBG_871X("%s  rtl8188e_iol_efuse_patch failed \n",__FUNCTION__);
		goto exit;
	}	
#endif

	_InitTxBufferBoundary(Adapter, txpktbuf_bndy);	

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_LLTT);
	status =  InitLLTTable(Adapter, txpktbuf_bndy);
	if(status == _FAIL){
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init LLT table\n"));
		goto exit;
	}
	
HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC02);
	// Get Rx PHY status in order to report RSSI and others.
	_InitDriverInfoSize(Adapter, DRVINFO_SZ);

	_InitInterrupt(Adapter);
	hal_init_macaddr(Adapter);//set mac_address
	_InitNetworkType(Adapter);//set msr	
	_InitWMACSetting(Adapter);
	_InitAdaptiveCtrl(Adapter);
	_InitEDCA(Adapter);
	//_InitRateFallback(Adapter);//just follow MP Team ???Georgia
	_InitRetryFunction(Adapter);
	InitUsbAggregationSetting(Adapter);
	_InitOperationMode(Adapter);//todo
	_InitBeaconParameters(Adapter);
	_InitBeaconMaxError(Adapter, _TRUE);

	//
	// Init CR MACTXEN, MACRXEN after setting RxFF boundary REG_TRXFF_BNDY to patch
	// Hw bug which Hw initials RxFF boundry size to a value which is larger than the real Rx buffer size in 88E. 
	//
	// Enable MACTXEN/MACRXEN block
	value16 = rtw_read16(Adapter, REG_CR);
	value16 |= (MACTXEN | MACRXEN);
	rtw_write8(Adapter, REG_CR, value16);		

	_InitHardwareDropIncorrectBulkOut(Adapter);


	if(pHalData->bRDGEnable){
		_InitRDGSetting(Adapter);
	}

#if (RATE_ADAPTIVE_SUPPORT==1)
	{//Enable TX Report
		//Enable Tx Report Timer   
		value8 = rtw_read8(Adapter, REG_TX_RPT_CTRL);
		rtw_write8(Adapter,  REG_TX_RPT_CTRL, (value8|BIT1|BIT0));
		//Set MAX RPT MACID
		rtw_write8(Adapter,  REG_TX_RPT_CTRL+1, 2);//FOR sta mode ,0: bc/mc ,1:AP
		//Tx RPT Timer. Unit: 32us
		rtw_write16(Adapter, REG_TX_RPT_TIME, 0xCdf0);
	}
#endif	

#if 0
	if(pHTInfo->bRDGEnable){
		_InitRDGSetting_8188E(Adapter);
	}
#endif

#ifdef CONFIG_TX_EARLY_MODE	
	if( pHalData->bEarlyModeEnable)
	{
		RT_TRACE(_module_hci_hal_init_c_, _drv_info_,("EarlyMode Enabled!!!\n"));

		value8 = rtw_read8(Adapter, REG_EARLY_MODE_CONTROL);
#if RTL8188E_EARLY_MODE_PKT_NUM_10 == 1
		value8 = value8|0x1f;
#else
		value8 = value8|0xf;
#endif
		rtw_write8(Adapter, REG_EARLY_MODE_CONTROL, value8);

		rtw_write8(Adapter, REG_EARLY_MODE_CONTROL+3, 0x80);

		value8 = rtw_read8(Adapter, REG_TCR+1);
		value8 = value8|0x40;
		rtw_write8(Adapter,REG_TCR+1, value8);
	}
	else
#endif
	{
		rtw_write8(Adapter, REG_EARLY_MODE_CONTROL, 0);
	}

	rtw_write32(Adapter,REG_MACID_NO_LINK_0,0xFFFFFFFF);
	rtw_write32(Adapter,REG_MACID_NO_LINK_1,0xFFFFFFFF);
	
#if defined(CONFIG_CONCURRENT_MODE) || defined(CONFIG_TX_MCAST2UNI)

#ifdef CONFIG_CHECK_AC_LIFETIME
	// Enable lifetime check for the four ACs
	rtw_write8(Adapter, REG_LIFETIME_CTRL, 0x0F);
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

	
	//
	// Joseph Note: Keep RfRegChnlVal for later use.
	//
	pHalData->RfRegChnlVal[0] = PHY_QueryRFReg(Adapter, 0, RF_CHNLBW, bRFRegOffsetMask);
	pHalData->RfRegChnlVal[1] = PHY_QueryRFReg(Adapter, 1, RF_CHNLBW, bRFRegOffsetMask);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_TURN_ON_BLOCK);
	_BBTurnOnBlock(Adapter);
	//NicIFSetMacAddress(padapter, padapter->PermanentAddress);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_SECURITY);
	invalidate_cam_all(Adapter);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC11);
	// 2010/12/17 MH We need to set TX power according to EFUSE content at first.
	PHY_SetTxPowerLevel8188E(Adapter, pHalData->CurrentChannel);

// Move by Neo for USB SS to below setp	
//_RfPowerSave(Adapter);

	_InitAntenna_Selection(Adapter);
	
	// 
	// Disable BAR, suggested by Scott
	// 2010.04.09 add by hpfan
	//
	rtw_write32(Adapter, REG_BAR_MODE_CTRL, 0x0201ffff);

	// HW SEQ CTRL
	//set 0x0 to 0xFF by tynli. Default enable HW SEQ NUM.
	rtw_write8(Adapter,REG_HWSEQ_CTRL, 0xFF); 

	if(pregistrypriv->wifi_spec)
		rtw_write16(Adapter,REG_FAST_EDCA_CTRL ,0);

	//Nav limit , suggest by scott
	rtw_write8(Adapter, 0x652, 0x0);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_HAL_DM);
	rtl8188e_InitHalDm(Adapter);
	
#if (MP_DRIVER == 1)
	if (Adapter->registrypriv.mp_mode == 1)
	{
		Adapter->mppriv.channel = pHalData->CurrentChannel;
		MPT_InitializeAdapter(Adapter, Adapter->mppriv.channel);
	}
	else	
#endif  //#if (MP_DRIVER == 1)
	{
	//
	// 2010/08/11 MH Merge from 8192SE for Minicard init. We need to confirm current radio status
	// and then decide to enable RF or not.!!!??? For Selective suspend mode. We may not 
	// call init_adapter. May cause some problem??
	//
	// Fix the bug that Hw/Sw radio off before S3/S4, the RF off action will not be executed 
	// in MgntActSet_RF_State() after wake up, because the value of pHalData->eRFPowerState 
	// is the same as eRfOff, we should change it to eRfOn after we config RF parameters.
	// Added by tynli. 2010.03.30.
	pwrctrlpriv->rf_pwrstate = rf_on;

#if 0  //to do
	RT_CLEAR_PS_LEVEL(pwrctrlpriv, RT_RF_OFF_LEVL_HALT_NIC);
#if 1 //Todo
	// 20100326 Joseph: Copy from GPIOChangeRFWorkItemCallBack() function to check HW radio on/off.
	// 20100329 Joseph: Revise and integrate the HW/SW radio off code in initialization.

	eRfPowerStateToSet = (rt_rf_power_state) RfOnOffDetect(Adapter);
	pwrctrlpriv->rfoff_reason |= eRfPowerStateToSet==rf_on ? RF_CHANGE_BY_INIT : RF_CHANGE_BY_HW;
	pwrctrlpriv->rfoff_reason |= (pwrctrlpriv->reg_rfoff) ? RF_CHANGE_BY_SW : 0;

	if(pwrctrlpriv->rfoff_reason&RF_CHANGE_BY_HW)
		pwrctrlpriv->b_hw_radio_off = _TRUE;

	DBG_8192C("eRfPowerStateToSet=%d\n", eRfPowerStateToSet);
	
	if(pwrctrlpriv->reg_rfoff == _TRUE)
	{	// User disable RF via registry.
		DBG_8192C("InitializeAdapter8192CU(): Turn off RF for RegRfOff.\n");
		//MgntActSet_RF_State(Adapter, rf_off, RF_CHANGE_BY_SW, _TRUE);
		
		// Those action will be discard in MgntActSet_RF_State because off the same state
		//for(eRFPath = 0; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
			//PHY_SetRFReg(Adapter, eRFPath, 0x4, 0xC00, 0x0);
	}
	else if(pwrctrlpriv->rfoff_reason > RF_CHANGE_BY_PS)
	{	// H/W or S/W RF OFF before sleep.
		DBG_8192C(" Turn off RF for RfOffReason(%x) ----------\n", pwrctrlpriv->rfoff_reason);
		//pwrctrlpriv->rfoff_reason = RF_CHANGE_BY_INIT;
		pwrctrlpriv->rf_pwrstate = rf_on;
		//MgntActSet_RF_State(Adapter, rf_off, pwrctrlpriv->rfoff_reason, _TRUE);
	}
	else
	{
		// Perform GPIO polling to find out current RF state. added by Roger, 2010.04.09.
		if(pHalData->BoardType == BOARD_MINICARD /*&& (Adapter->MgntInfo.PowerSaveControl.bGpioRfSw)*/)
		{
			DBG_8192C("InitializeAdapter8192CU(): RF=%d \n", eRfPowerStateToSet);
			if (eRfPowerStateToSet == rf_off)
			{				
				//MgntActSet_RF_State(Adapter, rf_off, RF_CHANGE_BY_HW, _TRUE);
				pwrctrlpriv->b_hw_radio_off = _TRUE;	
			}
			else
			{
				pwrctrlpriv->rf_pwrstate = rf_off;
				pwrctrlpriv->rfoff_reason = RF_CHANGE_BY_INIT; 
				pwrctrlpriv->b_hw_radio_off = _FALSE;					
				//MgntActSet_RF_State(Adapter, rf_on, pwrctrlpriv->rfoff_reason, _TRUE);
			}
		}	
		else
		{
			pwrctrlpriv->rf_pwrstate = rf_off;
			pwrctrlpriv->rfoff_reason = RF_CHANGE_BY_INIT; 			
			//MgntActSet_RF_State(Adapter, rf_on, pwrctrlpriv->rfoff_reason, _TRUE);
		}
	
		pwrctrlpriv->rfoff_reason = 0; 
		pwrctrlpriv->b_hw_radio_off = _FALSE;
		pwrctrlpriv->rf_pwrstate = rf_on;
		rtw_led_control(Adapter, LED_CTL_POWER_ON);

	}

	// 2010/-8/09 MH For power down module, we need to enable register block contrl reg at 0x1c.
	// Then enable power down control bit of register 0x04 BIT4 and BIT15 as 1.
	if(pHalData->pwrdown && eRfPowerStateToSet == rf_off)
	{
		// Enable register area 0x0-0xc.
		rtw_write8(Adapter, REG_RSV_CTRL, 0x0);

		//
		// <Roger_Notes> We should configure HW PDn source for WiFi ONLY, and then
		// our HW will be set in power-down mode if PDn source from all  functions are configured.
		// 2010.10.06.
		//
		//if(IS_HARDWARE_TYPE_8723AU(Adapter))
		//{			
		//	u1bTmp = rtw_read8(Adapter, REG_MULTI_FUNC_CTRL);
		//	rtw_write8(Adapter, REG_MULTI_FUNC_CTRL, (u1bTmp|WL_HWPDN_EN));
		//}
		//else
		//{
			rtw_write16(Adapter, REG_APS_FSMCO, 0x8812);
		//}
	}
	//DrvIFIndicateCurrentPhyStatus(Adapter); // 2010/08/17 MH Disable to prevent BSOD.
#endif
#endif


	// enable Tx report.
	rtw_write8(Adapter,  REG_FWHW_TXQ_CTRL+1, 0x0F);

	// Suggested by SD1 pisa. Added by tynli. 2011.10.21.
	rtw_write8(Adapter, REG_EARLY_MODE_CONTROL+3, 0x01);//Pretx_en, for WEP/TKIP SEC

	//tynli_test_tx_report.
	rtw_write16(Adapter, REG_TX_RPT_TIME, 0x3DF0);
	//RT_TRACE(COMP_INIT, DBG_TRACE, ("InitializeAdapter8188EUsb() <====\n"));

	//enable tx DMA to drop the redundate data of packet
	rtw_write16(Adapter,REG_TXDMA_OFFSET_CHK, (rtw_read16(Adapter,REG_TXDMA_OFFSET_CHK) | DROP_DATA_EN));

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_IQK);
	// 2010/08/26 MH Merge from 8192CE.
	if(pwrctrlpriv->rf_pwrstate == rf_on)
	{
		if(pHalData->odmpriv.RFCalibrateInfo.bIQKInitialized){
			PHY_IQCalibrate_8188E(Adapter,_TRUE);
		}
		else
		{
			PHY_IQCalibrate_8188E(Adapter,_FALSE);
			pHalData->odmpriv.RFCalibrateInfo.bIQKInitialized = _TRUE;
		}
		
HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_PW_TRACK);
		
		ODM_TXPowerTrackingCheck(&pHalData->odmpriv );
		

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_LCK);
		PHY_LCCalibrate_8188E(&pHalData->odmpriv );
	}
}

//HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_PABIAS);
//	_InitPABias(Adapter);
	rtw_write8(Adapter, REG_USB_HRPWM, 0);

#ifdef CONFIG_XMIT_ACK
	//ack for xmit mgmt frames.
	rtw_write32(Adapter, REG_FWHW_TXQ_CTRL, rtw_read32(Adapter, REG_FWHW_TXQ_CTRL)|BIT(12));
#endif //CONFIG_XMIT_ACK

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

void _ps_open_RF(_adapter *padapter) {
	//here call with bRegSSPwrLvl 1, bRegSSPwrLvl 2 needs to be verified
	//phy_SsPwrSwitch92CU(padapter, rf_on, 1);
}

void _ps_close_RF(_adapter *padapter){
	//here call with bRegSSPwrLvl 1, bRegSSPwrLvl 2 needs to be verified
	//phy_SsPwrSwitch92CU(padapter, rf_off, 1);
}


VOID
hal_poweroff_8188eu(
	IN	PADAPTER			Adapter 
)
{

	u8 	val8;
	u16	val16;
	u32	val32;
	u8 bMacPwrCtrlOn = _FALSE;

	rtw_hal_get_hwreg(Adapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if(bMacPwrCtrlOn == _FALSE)	
		return ;
	
	RT_TRACE(COMP_INIT, DBG_LOUD, ("%s\n",__FUNCTION__));

	//Stop Tx Report Timer. 0x4EC[Bit1]=b'0
	val8 = rtw_read8(Adapter, REG_TX_RPT_CTRL);
	rtw_write8(Adapter, REG_TX_RPT_CTRL, val8&(~BIT1));

	// stop rx 
	rtw_write8(Adapter, REG_CR, 0x0);
	
	// Run LPS WL RFOFF flow
	HalPwrSeqCmdParsing(Adapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, Rtl8188E_NIC_LPS_ENTER_FLOW);

	
	// 2. 0x1F[7:0] = 0		// turn off RF
	//rtw_write8(Adapter, REG_RF_CTRL, 0x00);

	val8 = rtw_read8(Adapter, REG_MCUFWDL);
	if ((val8 & RAM_DL_SEL) && Adapter->bFWReady) //8051 RAM code
	{
		//rtl8723a_FirmwareSelfReset(padapter);
		//_8051Reset88E(padapter);		
		
		// Reset MCU 0x2[10]=0.
		val8 = rtw_read8(Adapter, REG_SYS_FUNC_EN+1);
		val8 &= ~BIT(2);	// 0x2[10], FEN_CPUEN
		rtw_write8(Adapter, REG_SYS_FUNC_EN+1, val8);
	}	

	//val8 = rtw_read8(Adapter, REG_SYS_FUNC_EN+1);
	//val8 &= ~BIT(2);	// 0x2[10], FEN_CPUEN
	//rtw_write8(Adapter, REG_SYS_FUNC_EN+1, val8);

	// MCUFWDL 0x80[1:0]=0
	// reset MCU ready status
	rtw_write8(Adapter, REG_MCUFWDL, 0);

	//YJ,add,111212
	//Disable 32k
	val8 = rtw_read8(Adapter, REG_32K_CTRL);
	rtw_write8(Adapter, REG_32K_CTRL, val8&(~BIT0));
	
	// Card disable power action flow
	HalPwrSeqCmdParsing(Adapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, Rtl8188E_NIC_DISABLE_FLOW);	
	
	// Reset MCU IO Wrapper
	val8 = rtw_read8(Adapter, REG_RSV_CTRL+1);
	rtw_write8(Adapter, REG_RSV_CTRL+1, (val8&(~BIT3)));	
	val8 = rtw_read8(Adapter, REG_RSV_CTRL+1);
	rtw_write8(Adapter, REG_RSV_CTRL+1, val8|BIT3);
	
#if 0
	// 7. RSV_CTRL 0x1C[7:0] = 0x0E			// lock ISO/CLK/Power control register
	rtw_write8(Adapter, REG_RSV_CTRL, 0x0e);
#endif
#if 1
	//YJ,test add, 111207. For Power Consumption.
	val8 = rtw_read8(Adapter, GPIO_IN);
	rtw_write8(Adapter, GPIO_OUT, val8);
	rtw_write8(Adapter, GPIO_IO_SEL, 0xFF);//Reg0x46

	val8 = rtw_read8(Adapter, REG_GPIO_IO_SEL);
	//rtw_write8(Adapter, REG_GPIO_IO_SEL, (val8<<4)|val8);
	rtw_write8(Adapter, REG_GPIO_IO_SEL, (val8<<4));
	val8 = rtw_read8(Adapter, REG_GPIO_IO_SEL+1);
	rtw_write8(Adapter, REG_GPIO_IO_SEL+1, val8|0x0F);//Reg0x43
	rtw_write32(Adapter, REG_BB_PAD_CTRL, 0x00080808);//set LNA ,TRSW,EX_PA Pin to output mode
#endif

	Adapter->bFWReady = _FALSE;

	bMacPwrCtrlOn = _FALSE;
	rtw_hal_set_hwreg(Adapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);	
}
static void rtl8192cu_hw_power_down(_adapter *padapter)
{
	// 2010/-8/09 MH For power down module, we need to enable register block contrl reg at 0x1c.
	// Then enable power down control bit of register 0x04 BIT4 and BIT15 as 1.
		
	// Enable register area 0x0-0xc.
	rtw_write8(padapter,REG_RSV_CTRL, 0x0);			
	rtw_write16(padapter, REG_APS_FSMCO, 0x8812);
}

u32 rtl8188eu_hal_deinit(PADAPTER Adapter)
 {
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(Adapter);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
   	DBG_8192C("==> %s \n",__FUNCTION__);

#ifdef CONFIG_SUPPORT_USB_INT
	rtw_write32(Adapter, REG_HIMR_88E, IMR_DISABLED_88E);
	rtw_write32(Adapter, REG_HIMRE_88E, IMR_DISABLED_88E);
#endif
	
 #ifdef SUPPORT_HW_RFOFF_DETECTED
 	DBG_8192C("bkeepfwalive(%x)\n", pwrctl->bkeepfwalive);
 	if(pwrctl->bkeepfwalive)
 	{
		_ps_close_RF(Adapter);		
		if((pwrctl->bHWPwrPindetect) && (pwrctl->bHWPowerdown))
			rtl8192cu_hw_power_down(Adapter);
 	}
	else
#endif
	{
		if(Adapter->hw_init_completed == _TRUE){
			hal_poweroff_8188eu(Adapter);

			if((pwrctl->bHWPwrPindetect ) && (pwrctl->bHWPowerdown))
				rtl8192cu_hw_power_down(Adapter);
			
		}
	}	
	return _SUCCESS;
 }


unsigned int rtl8188eu_inirp_init(PADAPTER Adapter)
{	
	u8 i;	
	struct recv_buf *precvbuf;
	uint	status;
	struct dvobj_priv *pdev= adapter_to_dvobj(Adapter);
	struct intf_hdl * pintfhdl=&Adapter->iopriv.intf;
	struct recv_priv *precvpriv = &(Adapter->recvpriv);	
	u32 (*_read_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	u32 (*_read_interrupt)(struct intf_hdl *pintfhdl, u32 addr);
#endif

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
	if(pHalData->RtIntInPipe != 0x05)
	{
		status = _FAIL;
		DBG_871X("%s =>Warning !! Have not USB Int-IN pipe,  pHalData->RtIntInPipe(%d)!!!\n",__FUNCTION__,pHalData->RtIntInPipe);
		goto exit;
	}	
	_read_interrupt = pintfhdl->io_ops._read_interrupt;
	if(_read_interrupt(pintfhdl, RECV_INT_IN_ADDR) == _FALSE )
	{
		RT_TRACE(_module_hci_hal_init_c_,_drv_err_,("usb_rx_init: usb_read_interrupt error \n"));
		status = _FAIL;
	}
#endif

exit:
	
	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("<=== usb_inirp_init \n"));

_func_exit_;

	return status;

}

unsigned int rtl8188eu_inirp_deinit(PADAPTER Adapter)
{	
	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("\n ===> usb_rx_deinit \n"));
	
	rtw_read_port_cancel(Adapter);

	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("\n <=== usb_rx_deinit \n"));

	return _SUCCESS;
}



//-------------------------------------------------------------------------
//
//	EEPROM Power index mapping
//
//-------------------------------------------------------------------------


//-------------------------------------------------------------------
//
//	EEPROM/EFUSE Content Parsing
//
//-------------------------------------------------------------------
static VOID
_ReadBoardType(
	IN	PADAPTER	Adapter,	
	IN	u8*		PROMContent,
	IN	BOOLEAN		AutoloadFail
	)
{
 
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
	EEPROM_EFUSE_PRIV	*pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
#ifdef CONFIG_SW_LED
	pledpriv->bRegUseLed = _TRUE;

	switch(pEEPROM->CustomerID)
	{
		default:
			pledpriv->LedStrategy = SW_LED_MODE1;
			break;
	}
	pHalData->bLedOpenDrain = _TRUE;// Support Open-drain arrangement for controlling the LED. Added by Roger, 2009.10.16.
#else // HW LED
	pledpriv->LedStrategy = HW_LED;
#endif //CONFIG_SW_LED
}

static VOID
_ReadRFSetting(
	IN	PADAPTER	Adapter,	
	IN	u8* 	PROMContent,
	IN	BOOLEAN 	AutoloadFail
	)
{
}

static VOID
hal_InitPGData(
	IN	PADAPTER	pAdapter,
	IN	OUT	u8		*PROMContent
	)
{
#if 0
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(pAdapter);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u32	i;
	u16	value16;

	if(_FALSE == pEEPROM->bautoload_fail_flag)
	{ // autoload OK.
		if (_TRUE == pEEPROM->EepromOrEfuse)
		{
			// Read all Content from EEPROM or EFUSE.
			for(i = 0; i < HWSET_MAX_SIZE_88E; i += 2)
			{
				//value16 = EF2Byte(ReadEEprom(pAdapter, (u2Byte) (i>>1)));
				//*((u16 *)(&PROMContent[i])) = value16; 				
			}
		}
		else
		{
			// Read EFUSE real map to shadow.
			EFUSE_ShadowMapUpdate(pAdapter, EFUSE_WIFI, _FALSE);
			_rtw_memcpy((void*)PROMContent, (void*)pEEPROM->efuse_eeprom_data, HWSET_MAX_SIZE_88E);
		}
	}
	else
	{//autoload fail
		//RT_TRACE(COMP_INIT, DBG_LOUD, ("AutoLoad Fail reported from CR9346!!\n")); 
		pEEPROM->bautoload_fail_flag = _TRUE;
		//update to default value 0xFF
		if (_FALSE == pEEPROM->EepromOrEfuse)		
		EFUSE_ShadowMapUpdate(pAdapter, EFUSE_WIFI, _FALSE);	
	}
#endif
}
static void
Hal_EfuseParsePIDVID_8188EU(
	IN	PADAPTER		pAdapter,
	IN	u8*				hwinfo,
	IN	BOOLEAN			AutoLoadFail
	)
{

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if( !AutoLoadFail )
	{
		// VID, PID 
		pHalData->EEPROMVID = EF2Byte( *(u16 *)&hwinfo[EEPROM_VID_88EU] );
		pHalData->EEPROMPID = EF2Byte( *(u16 *)&hwinfo[EEPROM_PID_88EU] );
		
		// Customer ID, 0x00 and 0xff are reserved for Realtek. 		
		pHalData->EEPROMCustomerID = *(u8 *)&hwinfo[EEPROM_CustomID_88E];
		pHalData->EEPROMSubCustomerID = EEPROM_Default_SubCustomerID;

	}
	else
	{
		pHalData->EEPROMVID 			= EEPROM_Default_VID;
		pHalData->EEPROMPID 			= EEPROM_Default_PID;

		// Customer ID, 0x00 and 0xff are reserved for Realtek. 		
		pHalData->EEPROMCustomerID		= EEPROM_Default_CustomerID;
		pHalData->EEPROMSubCustomerID	= EEPROM_Default_SubCustomerID;

	}

	DBG_871X("VID = 0x%04X, PID = 0x%04X\n", pHalData->EEPROMVID, pHalData->EEPROMPID);
	DBG_871X("Customer ID: 0x%02X, SubCustomer ID: 0x%02X\n", pHalData->EEPROMCustomerID, pHalData->EEPROMSubCustomerID);
}

static void
Hal_EfuseParseMACAddr_8188EU(
	IN	PADAPTER		padapter,
	IN	u8*			hwinfo,
	IN	BOOLEAN			AutoLoadFail
	)
{
	u16			i, usValue;
	u8			sMacAddr[6] = {0x00, 0xE0, 0x4C, 0x81, 0x88, 0x02};
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
		_rtw_memcpy(pEEPROM->mac_addr, &hwinfo[EEPROM_MAC_ADDR_88EU], ETH_ALEN);

	}
//	NicIFSetMacAddress(pAdapter, pAdapter->PermanentAddress);

	RT_TRACE(_module_hci_hal_init_c_, _drv_notice_,
		 ("Hal_EfuseParseMACAddr_8188ES: Permanent Address = %02x-%02x-%02x-%02x-%02x-%02x\n",
		  pEEPROM->mac_addr[0], pEEPROM->mac_addr[1],
		  pEEPROM->mac_addr[2], pEEPROM->mac_addr[3],
		  pEEPROM->mac_addr[4], pEEPROM->mac_addr[5]));
}


static void
Hal_CustomizeByCustomerID_8188EU(
	IN	PADAPTER		padapter
	)
{
#if 0
	EEPROM_EFUSE_PRIV	*pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	// For customized behavior.
	if((pHalData->EEPROMVID == 0x103C) && (pHalData->EEPROMVID == 0x1629))// HP Lite-On for RTL8188CUS Slim Combo.
		pEEPROM->CustomerID = RT_CID_819x_HP;

	// Decide CustomerID according to VID/DID or EEPROM
	switch(pHalData->EEPROMCustomerID)
	{
		case EEPROM_CID_DEFAULT:
			if((pHalData->EEPROMVID == 0x2001) && (pHalData->EEPROMPID == 0x3308))
				pEEPROM->CustomerID = RT_CID_DLINK;
			else if((pHalData->EEPROMVID == 0x2001) && (pHalData->EEPROMPID == 0x3309))
				pEEPROM->CustomerID = RT_CID_DLINK;
			else if((pHalData->EEPROMVID == 0x2001) && (pHalData->EEPROMPID == 0x330a))
				pEEPROM->CustomerID = RT_CID_DLINK;
			break;
		case EEPROM_CID_WHQL:
			padapter->bInHctTest = TRUE;

			pMgntInfo->bSupportTurboMode = FALSE;
			pMgntInfo->bAutoTurboBy8186 = FALSE;

			pMgntInfo->PowerSaveControl.bInactivePs = FALSE;
			pMgntInfo->PowerSaveControl.bIPSModeBackup = FALSE;
			pMgntInfo->PowerSaveControl.bLeisurePs = FALSE;
			pMgntInfo->PowerSaveControl.bLeisurePsModeBackup =FALSE;
			pMgntInfo->keepAliveLevel = 0;

			padapter->bUnloadDriverwhenS3S4 = FALSE;
			break;
		default:
			pEEPROM->CustomerID = RT_CID_DEFAULT;
			break;

	}

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Mgnt Customer ID: 0x%02x\n", pEEPROM->CustomerID));

	hal_CustomizedBehavior_8723U(padapter);
#endif
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
		pEEPROM->bloadfile_fail_flag = _TRUE;
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
	
	pEEPROM->bloadfile_fail_flag = _FALSE;
	
	return _SUCCESS;
}

static void
Hal_ReadMACAddrFromFile_8188EU(
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
		pEEPROM->bloadmac_fail_flag = _FALSE;
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

	DBG_871X("Hal_ReadMACAddrFromFile_8188ES: Permanent Address = %02x-%02x-%02x-%02x-%02x-%02x\n",
		  pEEPROM->mac_addr[0], pEEPROM->mac_addr[1],
		  pEEPROM->mac_addr[2], pEEPROM->mac_addr[3],
		  pEEPROM->mac_addr[4], pEEPROM->mac_addr[5]);
}
#endif //CONFIG_EFUSE_CONFIG_FILE

static VOID
readAdapterInfo_8188EU(
	IN	PADAPTER	padapter
	)
{
#if 1
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);

	/* parse the eeprom/efuse content */
	Hal_EfuseParseIDCode88E(padapter, pEEPROM->efuse_eeprom_data);
	Hal_EfuseParsePIDVID_8188EU(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
#ifdef CONFIG_EFUSE_CONFIG_FILE
	Hal_ReadMACAddrFromFile_8188EU(padapter);
#else //CONFIG_EFUSE_CONFIG_FILE	
	Hal_EfuseParseMACAddr_8188EU(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
#endif //CONFIG_EFUSE_CONFIG_FILE	

	Hal_ReadPowerSavingMode88E(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
	Hal_ReadTxPowerInfo88E(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);	
	Hal_EfuseParseEEPROMVer88E(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
	rtl8188e_EfuseParseChnlPlan(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseXtal_8188E(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseCustomerID88E(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
	Hal_ReadAntennaDiversity88E(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseBoardType88E(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
	Hal_ReadThermalMeter_88E(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);

	//
	// The following part initialize some vars by PG info.
	//
	Hal_InitChannelPlan(padapter);
#if defined(CONFIG_WOWLAN) && defined(CONFIG_SDIO_HCI)
	Hal_DetectWoWMode(padapter);
#endif //CONFIG_WOWLAN && CONFIG_SDIO_HCI
	Hal_CustomizeByCustomerID_8188EU(padapter);

	_ReadLEDSetting(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);

#else

#ifdef CONFIG_INTEL_PROXIM	
		/* for intel proximity */
	if (pHalData->rf_type== RF_1T1R) {
		Adapter->proximity.proxim_support = _TRUE;
	} else if (pHalData->rf_type== RF_2T2R) {
		if ((pHalData->EEPROMPID == 0x8186) &&
			(pHalData->EEPROMVID== 0x0bda))
		Adapter->proximity.proxim_support = _TRUE;
	} else {
		Adapter->proximity.proxim_support = _FALSE;
	}
#endif //CONFIG_INTEL_PROXIM		
#endif
}

static void _ReadPROMContent(
	IN PADAPTER 		Adapter
	)
{	
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
	u8			eeValue;

	/* check system boot selection */
	eeValue = rtw_read8(Adapter, REG_9346CR);
	pEEPROM->EepromOrEfuse		= (eeValue & BOOT_FROM_EEPROM) ? _TRUE : _FALSE;
	pEEPROM->bautoload_fail_flag	= (eeValue & EEPROM_EN) ? _FALSE : _TRUE;


	DBG_8192C("Boot from %s, Autoload %s !\n", (pEEPROM->EepromOrEfuse ? "EEPROM" : "EFUSE"),
				(pEEPROM->bautoload_fail_flag ? "Fail" : "OK") );

	//pHalData->EEType = IS_BOOT_FROM_EEPROM(Adapter) ? EEPROM_93C46 : EEPROM_BOOT_EFUSE;
#ifdef CONFIG_EFUSE_CONFIG_FILE
	Hal_readPGDataFromConfigFile(Adapter);
#else //CONFIG_EFUSE_CONFIG_FILE
	Hal_InitPGData88E(Adapter);
#endif	//CONFIG_EFUSE_CONFIG_FILE
	readAdapterInfo_8188EU(Adapter);
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

static int _ReadAdapterInfo8188EU(PADAPTER	Adapter)
{
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32 start=rtw_get_current_time();
	
	MSG_8192C("====> %s\n", __FUNCTION__);

	//Efuse_InitSomeVar(Adapter);

	//if(IS_HARDWARE_TYPE_8723A(Adapter))
	//	_EfuseCellSel(Adapter);

	_ReadRFType(Adapter);//rf_chip -> _InitRFType()
	_ReadPROMContent(Adapter);

	//MSG_8192C("%s()(done), rf_chip=0x%x, rf_type=0x%x\n",  __FUNCTION__, pHalData->rf_chip, pHalData->rf_type);

	MSG_8192C("<==== %s in %d ms\n", __FUNCTION__, rtw_get_passing_time_ms(start));

	return _SUCCESS;
}


static void ReadAdapterInfo8188EU(PADAPTER Adapter)
{
	// Read EEPROM size before call any EEPROM function	
	Adapter->EepromAddressSize = GetEEPROMSize8188E(Adapter);
	
	_ReadAdapterInfo8188EU(Adapter);
}


#define GPIO_DEBUG_PORT_NUM 0
static void rtl8192cu_trigger_gpio_0(_adapter *padapter)
{
#ifdef CONFIG_USB_SUPPORT_ASYNC_VDN_REQ	
	u32 gpioctrl;
	DBG_8192C("==> trigger_gpio_0...\n");
	rtw_write16_async(padapter,REG_GPIO_PIN_CTRL,0);
	rtw_write8_async(padapter,REG_GPIO_PIN_CTRL+2,0xFF);
	gpioctrl = (BIT(GPIO_DEBUG_PORT_NUM)<<24 )|(BIT(GPIO_DEBUG_PORT_NUM)<<16);
	rtw_write32_async(padapter,REG_GPIO_PIN_CTRL,gpioctrl);
	gpioctrl |= (BIT(GPIO_DEBUG_PORT_NUM)<<8);
	rtw_write32_async(padapter,REG_GPIO_PIN_CTRL,gpioctrl);
	DBG_8192C("<=== trigger_gpio_0...\n");
#endif
}

static void ResumeTxBeacon(_adapter *padapter)
{
	HAL_DATA_TYPE*	pHalData = GET_HAL_DATA(padapter);	

	// 2010.03.01. Marked by tynli. No need to call workitem beacause we record the value
	// which should be read from register to a global variable.

	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) | BIT6);
	pHalData->RegFwHwTxQCtrl |= BIT6;
	rtw_write8(padapter, REG_TBTT_PROHIBIT+1, 0xff);
	pHalData->RegReg542 |= BIT0;
	rtw_write8(padapter, REG_TBTT_PROHIBIT+2, pHalData->RegReg542);
}
void UpdateInterruptMask8188EU(PADAPTER padapter,u8 bHIMR0 ,u32 AddMSR, u32 RemoveMSR)
{
	HAL_DATA_TYPE *pHalData;

	u32 *himr;
	pHalData = GET_HAL_DATA(padapter);

	if(bHIMR0)
		himr = &(pHalData->IntrMask[0]);
	else
		himr = &(pHalData->IntrMask[1]);
	
	if (AddMSR)
		*himr |= AddMSR;

	if (RemoveMSR)
		*himr &= (~RemoveMSR);

	if(bHIMR0)	
		rtw_write32(padapter, REG_HIMR_88E, *himr);
	else
		rtw_write32(padapter, REG_HIMRE_88E, *himr);	

}

static void StopTxBeacon(_adapter *padapter)
{
	HAL_DATA_TYPE*	pHalData = GET_HAL_DATA(padapter);

	// 2010.03.01. Marked by tynli. No need to call workitem beacause we record the value
	// which should be read from register to a global variable.

	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) & (~BIT6));
	pHalData->RegFwHwTxQCtrl &= (~BIT6);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+1, 0x64);
	pHalData->RegReg542 &= ~(BIT0);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+2, pHalData->RegReg542);

	 //todo: CheckFwRsvdPageContent(Adapter);  // 2010.06.23. Added by tynli.

}


static void hw_var_set_opmode(PADAPTER Adapter, u8 variable, u8* val)
{
	u8	val8;
	u8	mode = *((u8 *)val);
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

#ifdef CONFIG_CONCURRENT_MODE
	if(Adapter->iface_type == IFACE_PORT1)
	{
		// disable Port1 TSF update
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|BIT(4));
		
		// set net_type
		val8 = rtw_read8(Adapter, MSR)&0x03;
		val8 |= (mode<<2);
		rtw_write8(Adapter, MSR, val8);
		
		DBG_871X("%s()-%d mode = %d\n", __FUNCTION__, __LINE__, mode);

		if((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_))
		{
			if(!check_buddy_mlmeinfo_state(Adapter, WIFI_FW_AP_STATE))			
			{
				#ifdef CONFIG_INTERRUPT_BASED_TXBCN	

				#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT	
				rtw_write8(Adapter, REG_DRVERLYINT, 0x05);//restore early int time to 5ms
				UpdateInterruptMask8188EU(Adapter,_TRUE, 0, IMR_BCNDMAINT0_88E);	
				#endif // CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
				
				#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR		
				UpdateInterruptMask8188EU(Adapter,_TRUE ,0, (IMR_TBDER_88E|IMR_TBDOK_88E));	
				#endif// CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
					
				#endif //CONFIG_INTERRUPT_BASED_TXBCN		
			

				StopTxBeacon(Adapter);
			}
			
			rtw_write8(Adapter,REG_BCN_CTRL_1, 0x11);//disable atim wnd and disable beacon function
			//rtw_write8(Adapter,REG_BCN_CTRL_1, 0x18);
		}
		else if((mode == _HW_STATE_ADHOC_) /*|| (mode == _HW_STATE_AP_)*/)
		{
			ResumeTxBeacon(Adapter);
			rtw_write8(Adapter,REG_BCN_CTRL_1, 0x1a);
			//BIT4 - If set 0, hw will clr bcnq when tx becon ok/fail or port 1
			rtw_write8(Adapter, REG_MBID_NUM, rtw_read8(Adapter, REG_MBID_NUM)|BIT(3)|BIT(4));
		}
		else if(mode == _HW_STATE_AP_)
		{
#ifdef CONFIG_INTERRUPT_BASED_TXBCN			
			#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			UpdateInterruptMask8188EU(Adapter,_TRUE ,IMR_BCNDMAINT0_88E, 0);
			#endif//CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT

			#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR	
			UpdateInterruptMask8188EU(Adapter,_TRUE ,(IMR_TBDER_88E|IMR_TBDOK_88E), 0);
			#endif//CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
					
#endif //CONFIG_INTERRUPT_BASED_TXBCN

			ResumeTxBeacon(Adapter);
					
			rtw_write8(Adapter, REG_BCN_CTRL_1, 0x12);

			//Set RCR
			//rtw_write32(padapter, REG_RCR, 0x70002a8e);//CBSSID_DATA must set to 0
			//rtw_write32(Adapter, REG_RCR, 0x7000228e);//CBSSID_DATA must set to 0
			rtw_write32(Adapter, REG_RCR, 0x7000208e);//CBSSID_DATA must set to 0,reject ICV_ERR packet
			//enable to rx data frame				
			rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);
			//enable to rx ps-poll
			rtw_write16(Adapter, REG_RXFLTMAP1, 0x0400);

			//Beacon Control related register for first time 
			rtw_write8(Adapter, REG_BCNDMATIM, 0x02); // 2ms		

			//rtw_write8(Adapter, REG_BCN_MAX_ERR, 0xFF);
			rtw_write8(Adapter, REG_ATIMWND_1, 0x0a); // 10ms for port1
			rtw_write16(Adapter, REG_BCNTCFG, 0x00);
			rtw_write16(Adapter, REG_TBTT_PROHIBIT, 0xff04);
			rtw_write16(Adapter, REG_TSFTR_SYN_OFFSET, 0x7fff);// +32767 (~32ms)
	
			//reset TSF2	
			rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(1));


			//BIT4 - If set 0, hw will clr bcnq when tx becon ok/fail or port 1
			rtw_write8(Adapter, REG_MBID_NUM, rtw_read8(Adapter, REG_MBID_NUM)|BIT(3)|BIT(4));
		       	//enable BCN1 Function for if2
			//don't enable update TSF1 for if2 (due to TSF update when beacon/probe rsp are received)
			rtw_write8(Adapter, REG_BCN_CTRL_1, (DIS_TSF_UDT0_NORMAL_CHIP|EN_BCN_FUNCTION | EN_TXBCN_RPT|BIT(1)));

#ifdef CONFIG_CONCURRENT_MODE
			if(check_buddy_fwstate(Adapter, WIFI_FW_NULL_STATE))
				rtw_write8(Adapter, REG_BCN_CTRL, 
					rtw_read8(Adapter, REG_BCN_CTRL) & ~EN_BCN_FUNCTION);
#endif
                        //BCN1 TSF will sync to BCN0 TSF with offset(0x518) if if1_sta linked
			//rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|BIT(5));
			//rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(3));
					
			//dis BCN0 ATIM  WND if if1 is station
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(0));

#ifdef CONFIG_TSF_RESET_OFFLOAD
			// Reset TSF for STA+AP concurrent mode
			if ( check_buddy_fwstate(Adapter, (WIFI_STATION_STATE|WIFI_ASOC_STATE)) ) {
				if (reset_tsf(Adapter, IFACE_PORT1) == _FALSE)
					DBG_871X("ERROR! %s()-%d: Reset port1 TSF fail\n",
						__FUNCTION__, __LINE__);
			}
#endif	// CONFIG_TSF_RESET_OFFLOAD	
		}
	}
	else
#endif //CONFIG_CONCURRENT_MODE
	{
		// disable Port0 TSF update
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(4));
		
		// set net_type
		val8 = rtw_read8(Adapter, MSR)&0x0c;
		val8 |= mode;
		rtw_write8(Adapter, MSR, val8);
		
		DBG_871X("%s()-%d mode = %d\n", __FUNCTION__, __LINE__, mode);
		
		if((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_))
		{
#ifdef CONFIG_CONCURRENT_MODE
			if(!check_buddy_mlmeinfo_state(Adapter, WIFI_FW_AP_STATE))		
#endif //CONFIG_CONCURRENT_MODE
			{
			#ifdef CONFIG_INTERRUPT_BASED_TXBCN	
				#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
				rtw_write8(Adapter, REG_DRVERLYINT, 0x05);//restore early int time to 5ms					
				UpdateInterruptMask8188EU(Adapter,_TRUE, 0, IMR_BCNDMAINT0_88E);	
				#endif//CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
				
				#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR		
				UpdateInterruptMask8188EU(Adapter,_TRUE ,0, (IMR_TBDER_88E|IMR_TBDOK_88E));	
				#endif //CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
					
			#endif //CONFIG_INTERRUPT_BASED_TXBCN		
				StopTxBeacon(Adapter);
			}
			
			rtw_write8(Adapter,REG_BCN_CTRL, 0x19);//disable atim wnd
			//rtw_write8(Adapter,REG_BCN_CTRL, 0x18);
		}
		else if((mode == _HW_STATE_ADHOC_) /*|| (mode == _HW_STATE_AP_)*/)
		{
			ResumeTxBeacon(Adapter);
			rtw_write8(Adapter,REG_BCN_CTRL, 0x1a);
			//BIT3 - If set 0, hw will clr bcnq when tx becon ok/fail or port 0
			rtw_write8(Adapter, REG_MBID_NUM, rtw_read8(Adapter, REG_MBID_NUM)|BIT(3)|BIT(4));
		}
		else if(mode == _HW_STATE_AP_)
		{

#ifdef CONFIG_INTERRUPT_BASED_TXBCN			
			#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			UpdateInterruptMask8188EU(Adapter,_TRUE ,IMR_BCNDMAINT0_88E, 0);
			#endif//CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT

			#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR	
			UpdateInterruptMask8188EU(Adapter,_TRUE ,(IMR_TBDER_88E|IMR_TBDOK_88E), 0);
			#endif//CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
					
#endif //CONFIG_INTERRUPT_BASED_TXBCN


			ResumeTxBeacon(Adapter);

			rtw_write8(Adapter, REG_BCN_CTRL, 0x12);

			//Set RCR
			//rtw_write32(padapter, REG_RCR, 0x70002a8e);//CBSSID_DATA must set to 0
			//rtw_write32(Adapter, REG_RCR, 0x7000228e);//CBSSID_DATA must set to 0
			rtw_write32(Adapter, REG_RCR, 0x7000208e);//CBSSID_DATA must set to 0,reject ICV_ERR packet
			//enable to rx data frame
			rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);
			//enable to rx ps-poll
			rtw_write16(Adapter, REG_RXFLTMAP1, 0x0400);

			//Beacon Control related register for first time
			rtw_write8(Adapter, REG_BCNDMATIM, 0x02); // 2ms			
			
			//rtw_write8(Adapter, REG_BCN_MAX_ERR, 0xFF);
			rtw_write8(Adapter, REG_ATIMWND, 0x0a); // 10ms
			rtw_write16(Adapter, REG_BCNTCFG, 0x00);
			rtw_write16(Adapter, REG_TBTT_PROHIBIT, 0xff04);
			rtw_write16(Adapter, REG_TSFTR_SYN_OFFSET, 0x7fff);// +32767 (~32ms)

			//reset TSF
			rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(0));

			//BIT3 - If set 0, hw will clr bcnq when tx becon ok/fail or port 0
			rtw_write8(Adapter, REG_MBID_NUM, rtw_read8(Adapter, REG_MBID_NUM)|BIT(3)|BIT(4));
	
		        //enable BCN0 Function for if1
			//don't enable update TSF0 for if1 (due to TSF update when beacon/probe rsp are received)
			#if defined(CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR)
			rtw_write8(Adapter, REG_BCN_CTRL, (DIS_TSF_UDT0_NORMAL_CHIP|EN_BCN_FUNCTION | EN_TXBCN_RPT|BIT(1)));
			#else
			rtw_write8(Adapter, REG_BCN_CTRL, (DIS_TSF_UDT0_NORMAL_CHIP|EN_BCN_FUNCTION |BIT(1)));
			#endif

#ifdef CONFIG_CONCURRENT_MODE
			if(check_buddy_fwstate(Adapter, WIFI_FW_NULL_STATE))
				rtw_write8(Adapter, REG_BCN_CTRL_1, 
					rtw_read8(Adapter, REG_BCN_CTRL_1) & ~EN_BCN_FUNCTION);
#endif

			//dis BCN1 ATIM  WND if if2 is station
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|BIT(0));	
#ifdef CONFIG_TSF_RESET_OFFLOAD
			// Reset TSF for STA+AP concurrent mode
			if ( check_buddy_fwstate(Adapter, (WIFI_STATION_STATE|WIFI_ASOC_STATE)) ) {
				if (reset_tsf(Adapter, IFACE_PORT0) == _FALSE)
					DBG_871X("ERROR! %s()-%d: Reset port0 TSF fail\n",
						__FUNCTION__, __LINE__);
			}
#endif	// CONFIG_TSF_RESET_OFFLOAD
		}
	}

}

static void hw_var_set_macaddr(PADAPTER Adapter, u8 variable, u8* val)
{
	u8 idx = 0;
	u32 reg_macid;

#ifdef CONFIG_CONCURRENT_MODE
	if(Adapter->iface_type == IFACE_PORT1)
	{
		reg_macid = REG_MACID1;
	}
	else
#endif
	{
		reg_macid = REG_MACID;
	}

	for(idx = 0 ; idx < 6; idx++)
	{
		rtw_write8(Adapter, (reg_macid+idx), val[idx]);
	}
	
}

static void hw_var_set_bssid(PADAPTER Adapter, u8 variable, u8* val)
{
	u8	idx = 0;
	u32 reg_bssid;

#ifdef CONFIG_CONCURRENT_MODE
	if(Adapter->iface_type == IFACE_PORT1)
	{
		reg_bssid = REG_BSSID1;
	}
	else
#endif
	{
		reg_bssid = REG_BSSID;
	}

	for(idx = 0 ; idx < 6; idx++)
	{
		rtw_write8(Adapter, (reg_bssid+idx), val[idx]);
	}

}

static void hw_var_set_bcn_func(PADAPTER Adapter, u8 variable, u8* val)
{
	u32 bcn_ctrl_reg;

#ifdef CONFIG_CONCURRENT_MODE
	if(Adapter->iface_type == IFACE_PORT1)
	{
		bcn_ctrl_reg = REG_BCN_CTRL_1;
	}	
	else
#endif		
	{		
		bcn_ctrl_reg = REG_BCN_CTRL;
	}

	if(*((u8 *)val))
	{
		rtw_write8(Adapter, bcn_ctrl_reg, (EN_BCN_FUNCTION | EN_TXBCN_RPT));
	}
	else
	{
		rtw_write8(Adapter, bcn_ctrl_reg, rtw_read8(Adapter, bcn_ctrl_reg)&(~(EN_BCN_FUNCTION | EN_TXBCN_RPT)));
	}
	

}

static void hw_var_set_correct_tsf(PADAPTER Adapter, u8 variable, u8* val)
{
#ifdef CONFIG_CONCURRENT_MODE
	u64	tsf;
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	PADAPTER pbuddy_adapter = Adapter->pbuddy_adapter;

	//tsf = pmlmeext->TSFValue - ((u32)pmlmeext->TSFValue % (pmlmeinfo->bcn_interval*1024)) -1024; //us
	tsf = pmlmeext->TSFValue - rtw_modular64(pmlmeext->TSFValue, (pmlmeinfo->bcn_interval*1024)) -1024; //us

	if(((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
	{				
		//pHalData->RegTxPause |= STOP_BCNQ;BIT(6)
		//rtw_write8(Adapter, REG_TXPAUSE, (rtw_read8(Adapter, REG_TXPAUSE)|BIT(6)));
		StopTxBeacon(Adapter);
	}

	if(Adapter->iface_type == IFACE_PORT1)
	{
		//disable related TSF function
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)&(~BIT(3)));
							
		rtw_write32(Adapter, REG_TSFTR1, tsf);
		rtw_write32(Adapter, REG_TSFTR1+4, tsf>>32);


		//enable related TSF function
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|BIT(3));	

		// Update buddy port's TSF if it is SoftAP for beacon TX issue!
		if ( (pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE
			&& check_buddy_fwstate(Adapter, WIFI_AP_STATE)
		) { 
			//disable related TSF function
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(3)));

			rtw_write32(Adapter, REG_TSFTR, tsf);
			rtw_write32(Adapter, REG_TSFTR+4, tsf>>32);

			//enable related TSF function
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(3));
#ifdef CONFIG_TSF_RESET_OFFLOAD
		// Update buddy port's TSF(TBTT) if it is SoftAP for beacon TX issue!
			if (reset_tsf(Adapter, IFACE_PORT0) == _FALSE)
				DBG_871X("ERROR! %s()-%d: Reset port0 TSF fail\n",
					__FUNCTION__, __LINE__);

#endif	// CONFIG_TSF_RESET_OFFLOAD	
		}		

		
	}
	else
	{
		//disable related TSF function
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(3)));
							
		rtw_write32(Adapter, REG_TSFTR, tsf);
		rtw_write32(Adapter, REG_TSFTR+4, tsf>>32);

		//enable related TSF function
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(3));
		
		// Update buddy port's TSF if it is SoftAP for beacon TX issue!
		if ( (pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE
			&& check_buddy_fwstate(Adapter, WIFI_AP_STATE)
		) { 
			//disable related TSF function
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)&(~BIT(3)));

			rtw_write32(Adapter, REG_TSFTR1, tsf);
			rtw_write32(Adapter, REG_TSFTR1+4, tsf>>32);

			//enable related TSF function
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|BIT(3));
#ifdef CONFIG_TSF_RESET_OFFLOAD
		// Update buddy port's TSF if it is SoftAP for beacon TX issue!
			if (reset_tsf(Adapter, IFACE_PORT1) == _FALSE)
				DBG_871X("ERROR! %s()-%d: Reset port1 TSF fail\n",
					__FUNCTION__, __LINE__);
#endif	// CONFIG_TSF_RESET_OFFLOAD
		}		

	}
				
							
	if(((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
	{
		//pHalData->RegTxPause  &= (~STOP_BCNQ);
		//rtw_write8(Adapter, REG_TXPAUSE, (rtw_read8(Adapter, REG_TXPAUSE)&(~BIT(6))));
		ResumeTxBeacon(Adapter);
	}
#endif
}

static void hw_var_set_mlme_disconnect(PADAPTER Adapter, u8 variable, u8* val)
{
#ifdef CONFIG_CONCURRENT_MODE
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PADAPTER pbuddy_adapter = Adapter->pbuddy_adapter;
			
				
	if(check_buddy_mlmeinfo_state(Adapter, _HW_STATE_NOLINK_))	
		rtw_write16(Adapter, REG_RXFLTMAP2, 0x00);
	

	if(Adapter->iface_type == IFACE_PORT1)
	{
		//reset TSF1
		rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(1));

		//disable update TSF1
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|BIT(4));

		// disable Port1's beacon function
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)&(~BIT(3)));
	}
	else
	{
		//reset TSF
		rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(0));

		//disable update TSF
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(4));
	}
#endif
}

static void hw_var_set_mlme_sitesurvey(PADAPTER Adapter, u8 variable, u8* val)
{	
#ifdef CONFIG_CONCURRENT_MODE	

	struct mlme_ext_priv *pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	if(*((u8 *)val))//under sitesurvey
	{
		//config RCR to receive different BSSID & not to receive data frame
		u32 v = rtw_read32(Adapter, REG_RCR);
		v &= ~(RCR_CBSSID_BCN);
		rtw_write32(Adapter, REG_RCR, v);

		//disable update TSF
		if((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE)
		{
			if(Adapter->iface_type == IFACE_PORT1)
			{
				rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|BIT(4));
			}
			else
			{
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(4));
			}				
		}
				
		if(check_buddy_mlmeinfo_state(Adapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(Adapter, _FW_LINKED))		
		{
			StopTxBeacon(Adapter);
		}
	}
	else//sitesurvey done
	{
		//enable to rx data frame
		//write32(Adapter, REG_RCR, read32(padapter, REG_RCR)|RCR_ADF);
		rtw_write16(Adapter, REG_RXFLTMAP2,0xFFFF);

		//enable update TSF
		if(Adapter->iface_type == IFACE_PORT1)
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)&(~BIT(4)));
		else
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(4)));

		rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_BCN);
					
		if(check_buddy_mlmeinfo_state(Adapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(Adapter, _FW_LINKED))
		{
			ResumeTxBeacon(Adapter);			
		}
	}
#endif			
}

static void hw_var_set_mlme_join(PADAPTER Adapter, u8 variable, u8* val)
{
#ifdef CONFIG_CONCURRENT_MODE
	u8	RetryLimit = 0x30;
	u8	type = *((u8 *)val);
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
	EEPROM_EFUSE_PRIV	*pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);

	if(type == 0) // prepare to join
	{		
		if(check_buddy_mlmeinfo_state(Adapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(Adapter, _FW_LINKED))		
		{
			StopTxBeacon(Adapter);
		}
	
		//enable to rx data frame.Accept all data frame
		//rtw_write32(padapter, REG_RCR, rtw_read32(padapter, REG_RCR)|RCR_ADF);
		rtw_write16(Adapter, REG_RXFLTMAP2,0xFFFF);

		if(check_buddy_mlmeinfo_state(Adapter, WIFI_FW_AP_STATE))
			rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_BCN);
		else
			rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);

		if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
		{
			RetryLimit = (pEEPROM->CustomerID == RT_CID_CCX) ? 7 : 48;
		}
		else // Ad-hoc Mode
		{
			RetryLimit = 0x7;
		}
	}
	else if(type == 1) //joinbss_event call back when join res < 0
	{		
		if(check_buddy_mlmeinfo_state(Adapter, _HW_STATE_NOLINK_))		
			rtw_write16(Adapter, REG_RXFLTMAP2,0x00);

		if(check_buddy_mlmeinfo_state(Adapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(Adapter, _FW_LINKED))
		{
			ResumeTxBeacon(Adapter);			
			
			//reset TSF 1/2 after ResumeTxBeacon
			rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(1)|BIT(0));	
			
		}
	}
	else if(type == 2) //sta add event call back
	{
	 
		//enable update TSF
		if(Adapter->iface_type == IFACE_PORT1)
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)&(~BIT(4)));
		else
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(4)));
		 
	
		if(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE))
		{
			//fixed beacon issue for 8191su...........
			rtw_write8(Adapter,0x542 ,0x02);
			RetryLimit = 0x7;
		}


		if(check_buddy_mlmeinfo_state(Adapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(Adapter, _FW_LINKED))
		{
			ResumeTxBeacon(Adapter);			
			
			//reset TSF 1/2 after ResumeTxBeacon
			rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(1)|BIT(0));
		}
		
	}

	rtw_write16(Adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);
	
#endif
}

void SetHwReg8188EU(PADAPTER Adapter, u8 variable, u8* val)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	DM_ODM_T 		*podmpriv = &pHalData->odmpriv;
_func_enter_;

	switch(variable)
	{
		case HW_VAR_MEDIA_STATUS:
			{
				u8 val8;

				val8 = rtw_read8(Adapter, MSR)&0x0c;
				val8 |= *((u8 *)val);
				rtw_write8(Adapter, MSR, val8);
			}
			break;
		case HW_VAR_MEDIA_STATUS1:
			{
				u8 val8;
				
				val8 = rtw_read8(Adapter, MSR)&0x03;
				val8 |= *((u8 *)val) <<2;
				rtw_write8(Adapter, MSR, val8);
			}
			break;
		case HW_VAR_SET_OPMODE:
			hw_var_set_opmode(Adapter, variable, val);
			break;
		case HW_VAR_MAC_ADDR:
			hw_var_set_macaddr(Adapter, variable, val);			
			break;
		case HW_VAR_BSSID:
			hw_var_set_bssid(Adapter, variable, val);
			break;
		case HW_VAR_BASIC_RATE:
			{
				u16			BrateCfg = 0;
				u8			RateIndex = 0;

				// 2007.01.16, by Emily
				// Select RRSR (in Legacy-OFDM and CCK)
				// For 8190, we select only 24M, 12M, 6M, 11M, 5.5M, 2M, and 1M from the Basic rate.
				// We do not use other rates.
				HalSetBrateCfg( Adapter, val, &BrateCfg );
				DBG_8192C("HW_VAR_BASIC_RATE: BrateCfg(%#x)\n", BrateCfg);

				//2011.03.30 add by Luke Lee
				//CCK 2M ACK should be disabled for some BCM and Atheros AP IOT
				//because CCK 2M has poor TXEVM
				//CCK 5.5M & 11M ACK should be enabled for better performance

				pHalData->BasicRateSet = BrateCfg = (BrateCfg |0xd) & 0x15d;

				BrateCfg |= 0x01; // default enable 1M ACK rate
				// Set RRSR rate table.
				rtw_write8(Adapter, REG_RRSR, BrateCfg&0xff);
				rtw_write8(Adapter, REG_RRSR+1, (BrateCfg>>8)&0xff);
				rtw_write8(Adapter, REG_RRSR+2, rtw_read8(Adapter, REG_RRSR+2)&0xf0);

				// Set RTS initial rate
				while(BrateCfg > 0x1)
				{
					BrateCfg = (BrateCfg>> 1);
					RateIndex++;
				}
				// Ziv - Check
				rtw_write8(Adapter, REG_INIRTS_RATE_SEL, RateIndex);
			}
			break;
		case HW_VAR_TXPAUSE:
			rtw_write8(Adapter, REG_TXPAUSE, *((u8 *)val));	
			break;
		case HW_VAR_BCN_FUNC:
			hw_var_set_bcn_func(Adapter, variable, val);
			break;
		case HW_VAR_CORRECT_TSF:
#ifdef CONFIG_CONCURRENT_MODE
			hw_var_set_correct_tsf(Adapter, variable, val);
#else			
			{
				u64	tsf;
				struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
				struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

				//tsf = pmlmeext->TSFValue - ((u32)pmlmeext->TSFValue % (pmlmeinfo->bcn_interval*1024)) -1024; //us
				tsf = pmlmeext->TSFValue - rtw_modular64(pmlmeext->TSFValue, (pmlmeinfo->bcn_interval*1024)) -1024; //us

				if(((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
				{				
					//pHalData->RegTxPause |= STOP_BCNQ;BIT(6)
					//rtw_write8(Adapter, REG_TXPAUSE, (rtw_read8(Adapter, REG_TXPAUSE)|BIT(6)));
					StopTxBeacon(Adapter);
				}

				//disable related TSF function
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(3)));
							
				rtw_write32(Adapter, REG_TSFTR, tsf);
				rtw_write32(Adapter, REG_TSFTR+4, tsf>>32);

				//enable related TSF function
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(3));
				
							
				if(((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
				{
					//pHalData->RegTxPause  &= (~STOP_BCNQ);
					//rtw_write8(Adapter, REG_TXPAUSE, (rtw_read8(Adapter, REG_TXPAUSE)&(~BIT(6))));
					ResumeTxBeacon(Adapter);
				}
			}
#endif
			break;
		case HW_VAR_CHECK_BSSID:
			if(*((u8 *)val))
			{ 
				rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN); 
			}
			else
			{
				u32	val32;

				val32 = rtw_read32(Adapter, REG_RCR);
 
				val32 &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN);

				rtw_write32(Adapter, REG_RCR, val32);
			}
			break;
		case HW_VAR_MLME_DISCONNECT:
#ifdef CONFIG_CONCURRENT_MODE
			hw_var_set_mlme_disconnect(Adapter, variable, val);
#else
			{
				//Set RCR to not to receive data frame when NO LINK state
				//rtw_write32(Adapter, REG_RCR, rtw_read32(padapter, REG_RCR) & ~RCR_ADF);
				//reject all data frames
				rtw_write16(Adapter, REG_RXFLTMAP2,0x00);

				//reset TSF
				rtw_write8(Adapter, REG_DUAL_TSF_RST, (BIT(0)|BIT(1)));

				//disable update TSF
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(4));	
			}
#endif
			break;
		case HW_VAR_MLME_SITESURVEY:
#ifdef CONFIG_CONCURRENT_MODE
			hw_var_set_mlme_sitesurvey(Adapter, variable,  val);
#else
			if(*((u8 *)val))//under sitesurvey
			{
				//config RCR to receive different BSSID & not to receive data frame
				u32 v = rtw_read32(Adapter, REG_RCR);
				v &= ~(RCR_CBSSID_BCN);
				rtw_write32(Adapter, REG_RCR, v);
				//reject all data frame
				rtw_write16(Adapter, REG_RXFLTMAP2,0x00);

				//disable update TSF
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(4));
			}
			else//sitesurvey done
			{
				struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
				struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

				if ((is_client_associated_to_ap(Adapter) == _TRUE) ||
					((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) )
				{
					//enable to rx data frame
					//rtw_write32(Adapter, REG_RCR, rtw_read32(padapter, REG_RCR)|RCR_ADF);
					rtw_write16(Adapter, REG_RXFLTMAP2,0xFFFF);

					//enable update TSF
					rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(4)));
				}
				else if((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
				{
					//rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_ADF);
					rtw_write16(Adapter, REG_RXFLTMAP2,0xFFFF);

					//enable update TSF
					rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(4)));
				}

				if((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
					rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_BCN);
				else
				{
					if(Adapter->in_cta_test)
					{
						u32 v = rtw_read32(Adapter, REG_RCR);
						v &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN );//| RCR_ADF
						rtw_write32(Adapter, REG_RCR, v);
					}
					else
					{
						rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_BCN);
					}
				}
			}
#endif			
			break;
		case HW_VAR_MLME_JOIN:
#ifdef CONFIG_CONCURRENT_MODE
			hw_var_set_mlme_join(Adapter, variable,  val);
#else
			{
				u8	RetryLimit = 0x30;
				u8	type = *((u8 *)val);
				struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
				EEPROM_EFUSE_PRIV	*pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
				
				if(type == 0) // prepare to join
				{
					//enable to rx data frame.Accept all data frame
					//rtw_write32(padapter, REG_RCR, rtw_read32(padapter, REG_RCR)|RCR_ADF);
					rtw_write16(Adapter, REG_RXFLTMAP2,0xFFFF);

					if(Adapter->in_cta_test)
					{
						u32 v = rtw_read32(Adapter, REG_RCR);
						v &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN );//| RCR_ADF
						rtw_write32(Adapter, REG_RCR, v);
					}
					else
					{
						rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);
					}

					if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
					{
						RetryLimit = (pEEPROM->CustomerID == RT_CID_CCX) ? 7 : 48;
					}
					else // Ad-hoc Mode
					{
						RetryLimit = 0x7;
					}
				}
				else if(type == 1) //joinbss_event call back when join res < 0
				{
					rtw_write16(Adapter, REG_RXFLTMAP2,0x00);
				}
				else if(type == 2) //sta add event call back
				{
					//enable update TSF
					rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(4)));

					if(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE))
					{
						RetryLimit = 0x7;
					}
				}

				rtw_write16(Adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);
			}
#endif
			break;
		case HW_VAR_ON_RCR_AM:
                        rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_AM);
                        DBG_871X("%s, %d, RCR= %x \n", __FUNCTION__,__LINE__, rtw_read32(Adapter, REG_RCR));
                        break;
              case HW_VAR_OFF_RCR_AM:
                        rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)& (~RCR_AM));
                        DBG_871X("%s, %d, RCR= %x \n", __FUNCTION__,__LINE__, rtw_read32(Adapter, REG_RCR));
                        break;
		case HW_VAR_BEACON_INTERVAL:
			rtw_write16(Adapter, REG_BCN_INTERVAL, *((u16 *)val));
#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			{
				struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
				struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
				u16 bcn_interval = 	*((u16 *)val);
				if((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE){
					DBG_8192C("%s==> bcn_interval:%d, eraly_int:%d \n",__FUNCTION__,bcn_interval,bcn_interval>>1);
					rtw_write8(Adapter, REG_DRVERLYINT, bcn_interval>>1);// 50ms for sdio 
				}			
			}
#endif//CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT

			break;
		case HW_VAR_SLOT_TIME:
			{
				rtw_write8(Adapter, REG_SLOT, val[0]);
			}
			break;
		case HW_VAR_ACK_PREAMBLE:
			{
				u8	regTmp;
				u8	bShortPreamble = *( (PBOOLEAN)val );
				// Joseph marked out for Netgear 3500 TKIP channel 7 issue.(Temporarily)
				regTmp = (pHalData->nCur40MhzPrimeSC)<<5;
				rtw_write8(Adapter, REG_RRSR+2, regTmp);

				regTmp = rtw_read8(Adapter,REG_WMAC_TRXPTCL_CTL+2);
				if(bShortPreamble)		
					regTmp |= BIT1;
				else
					regTmp &= (~BIT1);
				rtw_write8(Adapter,REG_WMAC_TRXPTCL_CTL+2,regTmp);				
			}
			break;
		case HW_VAR_SEC_CFG:
#ifdef CONFIG_CONCURRENT_MODE
			rtw_write8(Adapter, REG_SECCFG, 0x0c|BIT(5));// enable tx enc and rx dec engine, and no key search for MC/BC				
#else
			rtw_write8(Adapter, REG_SECCFG, *((u8 *)val));
#endif
			break;
		case HW_VAR_CAM_EMPTY_ENTRY:
			{
				u8	ucIndex = *((u8 *)val);
				u8	i;
				u32	ulCommand=0;
				u32	ulContent=0;
				u32	ulEncAlgo=CAM_AES;

				for(i=0;i<CAM_CONTENT_COUNT;i++)
				{
					// filled id in CAM config 2 byte
					if( i == 0)
					{
						ulContent |=(ucIndex & 0x03) | ((u16)(ulEncAlgo)<<2);
						//ulContent |= CAM_VALID;
					}
					else
					{
						ulContent = 0;
					}
					// polling bit, and No Write enable, and address
					ulCommand= CAM_CONTENT_COUNT*ucIndex+i;
					ulCommand= ulCommand | CAM_POLLINIG|CAM_WRITE;
					// write content 0 is equall to mark invalid
					rtw_write32(Adapter, WCAMI, ulContent);  //delay_ms(40);
					//RT_TRACE(COMP_SEC, DBG_LOUD, ("CAM_empty_entry(): WRITE A4: %lx \n",ulContent));
					rtw_write32(Adapter, RWCAM, ulCommand);  //delay_ms(40);
					//RT_TRACE(COMP_SEC, DBG_LOUD, ("CAM_empty_entry(): WRITE A0: %lx \n",ulCommand));
				}
			}
			break;
		case HW_VAR_CAM_INVALID_ALL:
			rtw_write32(Adapter, RWCAM, BIT(31)|BIT(30));
			break;
		case HW_VAR_CAM_WRITE:
			{
				u32	cmd;
				u32	*cam_val = (u32 *)val;
				rtw_write32(Adapter, WCAMI, cam_val[0]);
				
				cmd = CAM_POLLINIG | CAM_WRITE | cam_val[1];
				rtw_write32(Adapter, RWCAM, cmd);
			}
			break;
		case HW_VAR_AC_PARAM_VO:
			rtw_write32(Adapter, REG_EDCA_VO_PARAM, ((u32 *)(val))[0]);
			break;
		case HW_VAR_AC_PARAM_VI:
			rtw_write32(Adapter, REG_EDCA_VI_PARAM, ((u32 *)(val))[0]);
			break;
		case HW_VAR_AC_PARAM_BE:
			pHalData->AcParam_BE = ((u32 *)(val))[0];
			rtw_write32(Adapter, REG_EDCA_BE_PARAM, ((u32 *)(val))[0]);
			break;
		case HW_VAR_AC_PARAM_BK:
			rtw_write32(Adapter, REG_EDCA_BK_PARAM, ((u32 *)(val))[0]);
			break;
		case HW_VAR_ACM_CTRL:
			{
				u8	acm_ctrl = *((u8 *)val);
				u8	AcmCtrl = rtw_read8( Adapter, REG_ACMHWCTRL);

				if(acm_ctrl > 1)
					AcmCtrl = AcmCtrl | 0x1;

				if(acm_ctrl & BIT(3))
					AcmCtrl |= AcmHw_VoqEn;
				else
					AcmCtrl &= (~AcmHw_VoqEn);

				if(acm_ctrl & BIT(2))
					AcmCtrl |= AcmHw_ViqEn;
				else
					AcmCtrl &= (~AcmHw_ViqEn);

				if(acm_ctrl & BIT(1))
					AcmCtrl |= AcmHw_BeqEn;
				else
					AcmCtrl &= (~AcmHw_BeqEn);

				DBG_871X("[HW_VAR_ACM_CTRL] Write 0x%X\n", AcmCtrl );
				rtw_write8(Adapter, REG_ACMHWCTRL, AcmCtrl );
			}
			break;
		case HW_VAR_AMPDU_MIN_SPACE:
			{
				u8	MinSpacingToSet;
				u8	SecMinSpace;

				MinSpacingToSet = *((u8 *)val);
				if(MinSpacingToSet <= 7)
				{
					switch(Adapter->securitypriv.dot11PrivacyAlgrthm)
					{
						case _NO_PRIVACY_:
						case _AES_:
							SecMinSpace = 0;
							break;

						case _WEP40_:
						case _WEP104_:
						case _TKIP_:
						case _TKIP_WTMIC_:
							SecMinSpace = 6;
							break;
						default:
							SecMinSpace = 7;
							break;
					}

					if(MinSpacingToSet < SecMinSpace){
						MinSpacingToSet = SecMinSpace;
					}

					//RT_TRACE(COMP_MLME, DBG_LOUD, ("Set HW_VAR_AMPDU_MIN_SPACE: %#x\n", Adapter->MgntInfo.MinSpaceCfg));
					rtw_write8(Adapter, REG_AMPDU_MIN_SPACE, (rtw_read8(Adapter, REG_AMPDU_MIN_SPACE) & 0xf8) | MinSpacingToSet);
				}
			}
			break;
		case HW_VAR_AMPDU_FACTOR:
			{
				u8	RegToSet_Normal[4]={0x41,0xa8,0x72, 0xb9};
				u8	RegToSet_BT[4]={0x31,0x74,0x42, 0x97};
				u8	FactorToSet;
				u8	*pRegToSet;
				u8	index = 0;

#ifdef CONFIG_BT_COEXIST
				if(	(pHalData->bt_coexist.BT_Coexist) &&
					(pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC4) )
					pRegToSet = RegToSet_BT; // 0x97427431;
				else
#endif
					pRegToSet = RegToSet_Normal; // 0xb972a841;

				FactorToSet = *((u8 *)val);
				if(FactorToSet <= 3)
				{
					FactorToSet = (1<<(FactorToSet + 2));
					if(FactorToSet>0xf)
						FactorToSet = 0xf;

					for(index=0; index<4; index++)
					{
						if((pRegToSet[index] & 0xf0) > (FactorToSet<<4))
							pRegToSet[index] = (pRegToSet[index] & 0x0f) | (FactorToSet<<4);
					
						if((pRegToSet[index] & 0x0f) > FactorToSet)
							pRegToSet[index] = (pRegToSet[index] & 0xf0) | (FactorToSet);
						
						rtw_write8(Adapter, (REG_AGGLEN_LMT+index), pRegToSet[index]);
					}

					//RT_TRACE(COMP_MLME, DBG_LOUD, ("Set HW_VAR_AMPDU_FACTOR: %#x\n", FactorToSet));
				}
			}
			break;
		case HW_VAR_RXDMA_AGG_PG_TH:
			#ifdef CONFIG_USB_RX_AGGREGATION
			{
				u8	threshold = *((u8 *)val);
				if( threshold == 0)
				{
					threshold = pHalData->UsbRxAggPageCount;
				}
				rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH, threshold);
			}
			#endif
			break;
		case HW_VAR_SET_RPWM:
#ifdef CONFIG_LPS_LCLK
			{
				u8	ps_state = *((u8 *)val);
				//rpwm value only use BIT0(clock bit) ,BIT6(Ack bit), and BIT7(Toggle bit) for 88e.
				//BIT0 value - 1: 32k, 0:40MHz.
				//BIT6 value - 1: report cpwm value after success set, 0:do not report.
				//BIT7 value - Toggle bit change.
				//modify by Thomas. 2012/4/2.
				ps_state = ps_state & 0xC1;
				//DBG_871X("##### Change RPWM value to = %x for switch clk #####\n",ps_state);
				rtw_write8(Adapter, REG_USB_HRPWM, ps_state);
			}
#endif
			break;
		case HW_VAR_H2C_FW_PWRMODE:
			{
				u8	psmode = (*(u8 *)val);
			
				// Forece leave RF low power mode for 1T1R to prevent conficting setting in Fw power
				// saving sequence. 2010.06.07. Added by tynli. Suggested by SD3 yschang.
				if( (psmode != PS_MODE_ACTIVE) && (!IS_92C_SERIAL(pHalData->VersionID)))
				{
					ODM_RF_Saving(podmpriv, _TRUE);
				}
				rtl8188e_set_FwPwrMode_cmd(Adapter, psmode);
			}
			break;
		case HW_VAR_H2C_FW_JOINBSSRPT:
		    {
				u8	mstatus = (*(u8 *)val);
				rtl8188e_set_FwJoinBssReport_cmd(Adapter, mstatus);
			}
			break;
#ifdef CONFIG_P2P_PS
		case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
			{
				u8	p2p_ps_state = (*(u8 *)val);
				rtl8188e_set_p2p_ps_offload_cmd(Adapter, p2p_ps_state);
			}
			break;
#endif //CONFIG_P2P_PS
#ifdef CONFIG_TDLS
		case HW_VAR_TDLS_WRCR:
			rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)&(~RCR_CBSSID_DATA ));
			break;
		case HW_VAR_TDLS_INIT_CH_SEN:
			{
				rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)&(~ RCR_CBSSID_DATA )&(~RCR_CBSSID_BCN ));
				rtw_write16(Adapter, REG_RXFLTMAP2,0xffff);

				//disable update TSF
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(4));
			}
			break;
		case HW_VAR_TDLS_DONE_CH_SEN:
			{
				//enable update TSF
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~ BIT(4)));
				rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|(RCR_CBSSID_BCN ));
			}
			break;
		case HW_VAR_TDLS_RS_RCR:
			rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|(RCR_CBSSID_DATA));
			break;
#endif //CONFIG_TDLS
		case HW_VAR_INITIAL_GAIN:
			{				
				DIG_T	*pDigTable = &podmpriv->DM_DigTable;					
				u32 		rx_gain = ((u32 *)(val))[0];
		
				if(rx_gain == 0xff){//restore rx gain					
					ODM_Write_DIG(podmpriv,pDigTable->BackupIGValue);
				}
				else{
					pDigTable->BackupIGValue = pDigTable->CurIGValue;
					ODM_Write_DIG(podmpriv,rx_gain);
				}
			}
			break;
		case HW_VAR_TRIGGER_GPIO_0:
			rtl8192cu_trigger_gpio_0(Adapter);
			break;
#ifdef CONFIG_BT_COEXIST
		case HW_VAR_BT_SET_COEXIST:
			{
				u8	bStart = (*(u8 *)val);
				rtl8192c_set_dm_bt_coexist(Adapter, bStart);
			}
			break;
		case HW_VAR_BT_ISSUE_DELBA:
			{
				u8	dir = (*(u8 *)val);
				rtl8192c_issue_delete_ba(Adapter, dir);
			}
			break;
#endif
#if (RATE_ADAPTIVE_SUPPORT==1)
		case HW_VAR_RPT_TIMER_SETTING:
			{
				u16	min_rpt_time = (*(u16 *)val);
				ODM_RA_Set_TxRPT_Time(podmpriv,min_rpt_time);	
			}
			break;
#endif
#ifdef CONFIG_SW_ANTENNA_DIVERSITY

		case HW_VAR_ANTENNA_DIVERSITY_LINK:
			//odm_SwAntDivRestAfterLink8192C(Adapter);
			ODM_SwAntDivRestAfterLink(podmpriv);
			break;
#endif			
#ifdef CONFIG_ANTENNA_DIVERSITY
		case HW_VAR_ANTENNA_DIVERSITY_SELECT:
			{
				u8	Optimum_antenna = (*(u8 *)val);
				u8 	Ant ; 
				//switch antenna to Optimum_antenna
				//DBG_8192C("==> HW_VAR_ANTENNA_DIVERSITY_SELECT , Ant_(%s)\n",(Optimum_antenna==2)?"A":"B");
				if(pHalData->CurAntenna !=  Optimum_antenna)		
				{					
					Ant = (Optimum_antenna==2)?MAIN_ANT:AUX_ANT;
					ODM_UpdateRxIdleAnt(&pHalData->odmpriv, Ant);
					
					pHalData->CurAntenna = Optimum_antenna ;
					//DBG_8192C("==> HW_VAR_ANTENNA_DIVERSITY_SELECT , Ant_(%s)\n",(Optimum_antenna==2)?"A":"B");
				}
			}
			break;
#endif
		case HW_VAR_EFUSE_BYTES: // To set EFUE total used bytes, added by Roger, 2008.12.22.
			pHalData->EfuseUsedBytes = *((u16 *)val);			
			break;
		case HW_VAR_FIFO_CLEARN_UP:
			{				
				struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(Adapter);
				u8 trycnt = 100;	
				
				//pause tx
				rtw_write8(Adapter,REG_TXPAUSE,0xff);
			
				//keep sn
				Adapter->xmitpriv.nqos_ssn = rtw_read16(Adapter,REG_NQOS_SEQ);

				if(pwrpriv->bkeepfwalive != _TRUE)
				{
					//RX DMA stop
					rtw_write32(Adapter,REG_RXPKT_NUM,(rtw_read32(Adapter,REG_RXPKT_NUM)|RW_RELEASE_EN));
					do{
						if(!(rtw_read32(Adapter,REG_RXPKT_NUM)&RXDMA_IDLE))
							break;
					}while(trycnt--);
					if(trycnt ==0)
						DBG_8192C("Stop RX DMA failed...... \n");

					//RQPN Load 0
					rtw_write16(Adapter,REG_RQPN_NPQ,0x0);
					rtw_write32(Adapter,REG_RQPN,0x80000000);
					rtw_mdelay_os(10);
				}
			}
			break;
	case HW_VAR_APFM_ON_MAC:
			pHalData->bMacPwrCtrlOn = *val;
			DBG_871X("%s: bMacPwrCtrlOn=%d\n", __func__, pHalData->bMacPwrCtrlOn);
			break;

#ifdef CONFIG_WOWLAN
		case HW_VAR_WOWLAN:
		{
			struct wowlan_ioctl_param *poidparam;
			struct recv_buf *precvbuf;
			struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(Adapter);
			struct security_priv *psecuritypriv = &Adapter->securitypriv;
			int res, i;
			u32 tmp;
			u64 iv_low = 0, iv_high = 0;
			u16 len = 0;
			u8 mstatus = (*(u8 *)val);
			u8 trycnt = 100;
			u8 data[4], val8;

			poidparam = (struct wowlan_ioctl_param *)val;
			switch (poidparam->subcode){
				case WOWLAN_ENABLE:
					DBG_871X_LEVEL(_drv_always_, "WOWLAN_ENABLE\n");

					val8 = (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X)? 0xcc: 0xcf;
					rtw_write8(Adapter, REG_SECCFG, val8);
					DBG_871X_LEVEL(_drv_always_, "REG_SECCFG: %02x\n", rtw_read8(Adapter, REG_SECCFG));
					SetFwRelatedForWoWLAN8188ES(Adapter, _TRUE);

					rtl8188e_set_FwJoinBssReport_cmd(Adapter, 1);
					rtw_msleep_os(2);

					//Set Pattern
					//if(pwrctl->wowlan_pattern==_TRUE)
					//	rtw_wowlan_reload_pattern(Adapter);

					//RX DMA stop
					DBG_871X_LEVEL(_drv_always_, "Pause DMA\n");
					rtw_write32(Adapter,REG_RXPKT_NUM,(rtw_read32(Adapter,REG_RXPKT_NUM)|RW_RELEASE_EN));
					do{
						if((rtw_read32(Adapter, REG_RXPKT_NUM)&RXDMA_IDLE)) {
							DBG_871X_LEVEL(_drv_always_, "RX_DMA_IDLE is true\n");
							if (Adapter->intf_stop)
								Adapter->intf_stop(Adapter);
							break;
						} else {
							// If RX_DMA is not idle, receive one pkt from DMA
							DBG_871X_LEVEL(_drv_always_, "RX_DMA_IDLE is not true\n");
						}
					}while(trycnt--);

					if(trycnt ==0)
						DBG_871X_LEVEL(_drv_always_, "Stop RX DMA failed...... \n");

					//Set WOWLAN H2C command.
					DBG_871X_LEVEL(_drv_always_, "Set WOWLan cmd\n");
					rtl8188es_set_wowlan_cmd(Adapter, 1);

					mstatus = rtw_read8(Adapter, REG_WOW_CTRL);
					trycnt = 10;

					while(!(mstatus&BIT1) && trycnt>1) {
						mstatus = rtw_read8(Adapter, REG_WOW_CTRL);
						DBG_871X_LEVEL(_drv_always_, "Loop index: %d :0x%02x\n", trycnt, mstatus);
						trycnt --;
						rtw_msleep_os(2);
					}

					pwrctl->wowlan_wake_reason = rtw_read8(Adapter, REG_WOWLAN_WAKE_REASON);
					DBG_871X_LEVEL(_drv_always_, "wowlan_wake_reason: 0x%02x\n",
										pwrctl->wowlan_wake_reason);

					/* Invoid SE0 reset signal during suspending*/
					rtw_write8(Adapter, REG_RSV_CTRL, 0x20);
					rtw_write8(Adapter, REG_RSV_CTRL, 0x60);

					//rtw_msleep_os(10);
					break;
				case WOWLAN_DISABLE:
					trycnt = 10;

					DBG_871X_LEVEL(_drv_always_, "WOWLAN_DISABLE\n");

					rtl8188e_set_FwJoinBssReport_cmd(Adapter, 0);
					rtw_write8(Adapter, REG_SECCFG, 0x0c|BIT(5));// enable tx enc and rx dec engine, and no key search for MC/BC
					DBG_871X_LEVEL(_drv_always_, "REG_SECCFG: %02x\n", rtw_read8(Adapter, REG_SECCFG));

					pwrctl->wowlan_wake_reason =
						rtw_read8(Adapter, REG_WOWLAN_WAKE_REASON);
					DBG_871X_LEVEL(_drv_always_,
							"wakeup_reason: 0x%02x\n", pwrctl->wowlan_wake_reason);

					rtl8188es_set_wowlan_cmd(Adapter, 0);
					mstatus = rtw_read8(Adapter, REG_WOW_CTRL);
					DBG_871X_LEVEL(_drv_info_, "%s mstatus:0x%02x\n", __func__, mstatus);

					while(mstatus&BIT1 && trycnt>1) {
						mstatus = rtw_read8(Adapter, REG_WOW_CTRL);
						DBG_871X_LEVEL(_drv_always_, "Loop index: %d :0x%02x\n", trycnt, mstatus);
						trycnt --;
						rtw_msleep_os(2);
					}

					if (mstatus & BIT1) {
						DBG_871X_LEVEL(_drv_always_, "Disable WOW mode fail!!\n");
						DBG_871X("Set 0x690=0x00\n");
						rtw_write8(Adapter, REG_WOW_CTRL, (rtw_read8(Adapter, REG_WOW_CTRL)&0xf0));
						DBG_871X_LEVEL(_drv_always_, "Release RXDMA\n");
						rtw_write32(Adapter, REG_RXPKT_NUM,(rtw_read32(Adapter,REG_RXPKT_NUM)&(~RW_RELEASE_EN)));
					}

					// 3.1 read fw iv
					iv_low = rtw_read32(Adapter, REG_TXPKTBUF_IV_LOW);
					//only low two bytes is PN, check AES_IV macro for detail
					iv_low &= 0xffff;
					iv_high = rtw_read32(Adapter, REG_TXPKTBUF_IV_HIGH);
					//get the real packet number
					pwrctl->wowlan_fw_iv = iv_high << 16 | iv_low;
					DBG_871X_LEVEL(_drv_always_, "fw_iv: 0x%016llx\n", pwrctl->wowlan_fw_iv);
					//Update TX iv data.
					rtw_set_sec_pn(Adapter);

					SetFwRelatedForWoWLAN8188ES(Adapter, _FALSE);

					if((pwrctl->wowlan_wake_reason != FWDecisionDisconnect) &&
						(pwrctl->wowlan_wake_reason != Rx_Pairwisekey) &&
						(pwrctl->wowlan_wake_reason != Rx_DisAssoc) &&
						(pwrctl->wowlan_wake_reason != Rx_DeAuth))
						rtl8188e_set_FwJoinBssReport_cmd(Adapter, 1);

					rtw_msleep_os(5);
					break;
				default:
					break;
			}
		}
		break;
#endif //CONFIG_WOWLAN


	#if (RATE_ADAPTIVE_SUPPORT == 1)
		case HW_VAR_TX_RPT_MAX_MACID:
			{
				u8 maxMacid = *val;
				DBG_871X("### MacID(%d),Set Max Tx RPT MID(%d)\n",maxMacid,maxMacid+1);
				rtw_write8(Adapter, REG_TX_RPT_CTRL+1, maxMacid+1);
			}
			break;
	#endif		
		case HW_VAR_H2C_MEDIA_STATUS_RPT:
			{				
				rtl8188e_set_FwMediaStatus_cmd(Adapter , (*(u16 *)val));
			}
			break;
		case HW_VAR_BCN_VALID:
			//BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2, write 1 to clear, Clear by sw
			rtw_write8(Adapter, REG_TDECTRL+2, rtw_read8(Adapter, REG_TDECTRL+2) | BIT0); 
			break;

		default:
			SetHwReg8188E(Adapter, variable, val);
			break;
	}

_func_exit_;
}

void GetHwReg8188EU(PADAPTER Adapter, u8 variable, u8* val)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	DM_ODM_T 		*podmpriv = &pHalData->odmpriv;
_func_enter_;

	switch(variable)
	{
		case HW_VAR_TXPAUSE:
			val[0] = rtw_read8(Adapter, REG_TXPAUSE);
			break;
		case HW_VAR_BCN_VALID:
			//BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2
			val[0] = (BIT0 & rtw_read8(Adapter, REG_TDECTRL+2))?_TRUE:_FALSE;
			break;
		case HW_VAR_FWLPS_RF_ON:
			{
				//When we halt NIC, we should check if FW LPS is leave.
				if(adapter_to_pwrctl(Adapter)->rf_pwrstate == rf_off)
				{
					// If it is in HW/SW Radio OFF or IPS state, we do not check Fw LPS Leave,
					// because Fw is unload.
					val[0] = _TRUE;
				}
				else
				{
					u32 valRCR;
					valRCR = rtw_read32(Adapter, REG_RCR);
					valRCR &= 0x00070000;
					if(valRCR)
						val[0] = _FALSE;
					else
						val[0] = _TRUE;
				}
			}
			break;
#ifdef CONFIG_ANTENNA_DIVERSITY
		case HW_VAR_CURRENT_ANTENNA:
			val[0] = pHalData->CurAntenna;
			break;
#endif
		case HW_VAR_EFUSE_BYTES: // To get EFUE total used bytes, added by Roger, 2008.12.22.
			*((u16 *)(val)) = pHalData->EfuseUsedBytes;	
			break;
		case HW_VAR_APFM_ON_MAC:
			*val = pHalData->bMacPwrCtrlOn;
			break;
		case HW_VAR_CHK_HI_QUEUE_EMPTY:
			*val = ((rtw_read32(Adapter, REG_HGQ_INFORMATION)&0x0000ff00)==0) ? _TRUE:_FALSE;
			break;
		default:
			GetHwReg8188E(Adapter, variable, val);
			break;
	}

_func_exit_;
}

//
//	Description: 
//		Query setting of specified variable.
//
u8
GetHalDefVar8188EUsb(
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
#if 1 //trunk
			{
				struct mlme_priv *pmlmepriv = &Adapter->mlmepriv;
				struct sta_priv * pstapriv = &Adapter->stapriv;
				struct sta_info * psta;
				psta = rtw_get_stainfo(pstapriv, pmlmepriv->cur_network.network.MacAddress);
				if(psta)
				{
					*((int *)pValue) = psta->rssi_stat.UndecoratedSmoothedPWDB;     
				}
			}
#else //V4 branch
				if(check_fwstate(&Adapter->mlmepriv, WIFI_STATION_STATE) == _TRUE){
						*((int *)pValue) = pHalData->dmpriv.UndecoratedSmoothedPWDB;
				}
				else{
    
				}
#endif
			break;
		case HAL_DEF_IS_SUPPORT_ANT_DIV:
#ifdef CONFIG_ANTENNA_DIVERSITY
			*((u8 *)pValue) = (pHalData->AntDivCfg==0)?_FALSE:_TRUE;
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
#if (RATE_ADAPTIVE_SUPPORT == 1)
		case HAL_DEF_RA_DECISION_RATE:
			{
				u8 MacID = *((u8*)pValue);
				*((u8*)pValue) = ODM_RA_GetDecisionRate_8188E(&(pHalData->odmpriv), MacID);
			}
			break;

		case HAL_DEF_RA_SGI:
			{
				u8 MacID = *((u8*)pValue);
				*((u8*)pValue) = ODM_RA_GetShortGI_8188E(&(pHalData->odmpriv), MacID);
			}
			break;		
#endif

 
		case HAL_DEF_PT_PWR_STATUS:
#if(POWER_TRAINING_ACTIVE==1)
			{
				u8 MacID = *((u8*)pValue);
				*((u8*)pValue) = ODM_RA_GetHwPwrStatus_8188E(&(pHalData->odmpriv), MacID);
			}
#endif//(POWER_TRAINING_ACTIVE==1)
			break;		

		case HW_VAR_MAX_RX_AMPDU_FACTOR:
			*(( u32*)pValue) = MAX_AMPDU_FACTOR_64K;
			break;

		case HAL_DEF_TX_LDPC:
		case HAL_DEF_RX_LDPC:
			*((u8 *)pValue) = _FALSE;
			break;
		case HAL_DEF_TX_STBC:
			*((u8 *)pValue) = 0;
			break;
		case HAL_DEF_RX_STBC:
			*((u8 *)pValue) = 1;
			break;
		case HAL_DEF_EXPLICIT_BEAMFORMEE:
		case HAL_DEF_EXPLICIT_BEAMFORMER:
			*((u8 *)pValue) = _FALSE;
			break;

                case HW_DEF_RA_INFO_DUMP:
#if (RATE_ADAPTIVE_SUPPORT == 1)	
			{
				u8 mac_id = *((u8*)pValue);				
				u8 			bLinked = _FALSE;
#ifdef CONFIG_CONCURRENT_MODE
				PADAPTER pbuddy_adapter = Adapter->pbuddy_adapter;
#endif //CONFIG_CONCURRENT_MODE

				if(rtw_linked_check(Adapter))
					bLinked = _TRUE;
		
#ifdef CONFIG_CONCURRENT_MODE
				if(pbuddy_adapter && rtw_linked_check(pbuddy_adapter))
					bLinked = _TRUE;
#endif			
				
				if(bLinked){					
					DBG_871X("============ RA status - Mac_id:%d ===================\n",mac_id);
									
					DBG_8192C("Mac_id:%d ,RSSI:%d(%%),PTStage = %d\n",
						mac_id,pHalData->odmpriv.RAInfo[mac_id].RssiStaRA,pHalData->odmpriv.RAInfo[mac_id].PTStage);							

					DBG_8192C("RateID = %d,RAUseRate = 0x%08x,RateSGI = %d, DecisionRate = %s\n",
						pHalData->odmpriv.RAInfo[mac_id].RateID,
						pHalData->odmpriv.RAInfo[mac_id].RAUseRate,
						pHalData->odmpriv.RAInfo[mac_id].RateSGI,
						HDATA_RATE(pHalData->odmpriv.RAInfo[mac_id].DecisionRate));	
					
				}
			}
#endif	//(RATE_ADAPTIVE_SUPPORT == 1)
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
SetHalDefVar8188EUsb(
	IN	PADAPTER				Adapter,
	IN	HAL_DEF_VARIABLE		eVariable,
	IN	PVOID					pValue
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _SUCCESS;

	switch(eVariable)
	{
		default:
			bResult = SetHalDefVar(Adapter, eVariable, pValue);
			break;
	}

	return bResult;
}
/*
u32  _update_92cu_basic_rate(_adapter *padapter, unsigned int mask)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
#ifdef CONFIG_BT_COEXIST
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);
#endif
	unsigned int BrateCfg = 0;

#ifdef CONFIG_BT_COEXIST
	if(	(pbtpriv->BT_Coexist) &&	(pbtpriv->BT_CoexistType == BT_CSR_BC4)	)
	{
		BrateCfg = mask  & 0x151;
		//DBG_8192C("BT temp disable cck 2/5.5/11M, (0x%x = 0x%x)\n", REG_RRSR, BrateCfg & 0x151);
	}
	else
#endif
	{
		//if(pHalData->VersionID != VERSION_TEST_CHIP_88C)
			BrateCfg = mask  & 0x15F;
		//else	//for 88CU 46PING setting, Disable CCK 2M, 5.5M, Others must tuning
		//	BrateCfg = mask  & 0x159;
	}

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

void SetBeaconRelatedRegisters8188EUsb(PADAPTER padapter)
{
	u32	value32;
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u32 bcn_ctrl_reg 			= REG_BCN_CTRL;
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

	//BCN interval
#ifdef CONFIG_CONCURRENT_MODE
        	if (padapter->iface_type == IFACE_PORT1){
		bcn_ctrl_reg = REG_BCN_CTRL_1;
        	}
#endif	
	rtw_write16(padapter, REG_BCN_INTERVAL, pmlmeinfo->bcn_interval);
	rtw_write8(padapter, REG_ATIMWND, 0x02);// 2ms

	_InitBeaconParameters(padapter);

	rtw_write8(padapter, REG_SLOT, 0x09);

	value32 =rtw_read32(padapter, REG_TCR); 
	value32 &= ~TSFRST;
	rtw_write32(padapter,  REG_TCR, value32); 

	value32 |= TSFRST;
	rtw_write32(padapter, REG_TCR, value32); 

	// NOTE: Fix test chip's bug (about contention windows's randomness)
	rtw_write8(padapter,  REG_RXTSF_OFFSET_CCK, 0x50);
	rtw_write8(padapter, REG_RXTSF_OFFSET_OFDM, 0x50);

	_BeaconFunctionEnable(padapter, _TRUE, _TRUE);

	ResumeTxBeacon(padapter);

	//rtw_write8(padapter, 0x422, rtw_read8(padapter, 0x422)|BIT(6));
	
	//rtw_write8(padapter, 0x541, 0xff);

	//rtw_write8(padapter, 0x542, rtw_read8(padapter, 0x541)|BIT(0));

	rtw_write8(padapter, bcn_ctrl_reg, rtw_read8(padapter, bcn_ctrl_reg)|BIT(1));

}

static void rtl8188eu_init_default_value(_adapter * padapter)
{
	PHAL_DATA_TYPE pHalData;
	struct pwrctrl_priv *pwrctrlpriv;
	struct dm_priv *pdmpriv;
	u8 i;

	pHalData = GET_HAL_DATA(padapter);
	pwrctrlpriv = adapter_to_pwrctl(padapter);
	pdmpriv = &pHalData->dmpriv;
	
	padapter->registrypriv.wireless_mode = WIRELESS_11BG_24N;
	//init default value
	pHalData->fw_ractrl = _FALSE;		
	if(!pwrctrlpriv->bkeepfwalive)
		pHalData->LastHMEBoxNum = 0;	

	//init dm default value
	pHalData->odmpriv.RFCalibrateInfo.bIQKInitialized = _FALSE;
	pHalData->odmpriv.RFCalibrateInfo.TM_Trigger = 0;//for IQK
	//pdmpriv->binitialized = _FALSE;
//	pdmpriv->prv_traffic_idx = 3;
//	pdmpriv->initialize = 0;
	pHalData->pwrGroupCnt = 0;
	pHalData->PGMaxGroup= 13;
	pHalData->odmpriv.RFCalibrateInfo.ThermalValue_HP_index = 0;
	for(i = 0; i < HP_THERMAL_NUM; i++)
		pHalData->odmpriv.RFCalibrateInfo.ThermalValue_HP[i] = 0;

	pHalData->EfuseHal.fakeEfuseBank = 0;
	pHalData->EfuseHal.fakeEfuseUsedBytes = 0;
	_rtw_memset(pHalData->EfuseHal.fakeEfuseContent, 0xFF, EFUSE_MAX_HW_SIZE);
	_rtw_memset(pHalData->EfuseHal.fakeEfuseInitMap, 0xFF, EFUSE_MAX_MAP_LEN);
	_rtw_memset(pHalData->EfuseHal.fakeEfuseModifiedMap, 0xFF, EFUSE_MAX_MAP_LEN);
}

static u8 rtl8188eu_ps_func(PADAPTER Adapter,HAL_INTF_PS_FUNC efunc_id, u8 *val)
{	
	u8 bResult = _TRUE;
	switch(efunc_id){

		#if defined(CONFIG_AUTOSUSPEND) && defined(SUPPORT_HW_RFOFF_DETECTED)
		case HAL_USB_SELECT_SUSPEND:
			{
				u8 bfwpoll = *(( u8*)val);
				//rtl8188e_set_FwSelectSuspend_cmd(Adapter,bfwpoll ,500);//note fw to support hw power down ping detect
			}
			break;
		#endif //CONFIG_AUTOSUSPEND && SUPPORT_HW_RFOFF_DETECTED

		default:
			break;
	}
	return bResult;
}

void rtl8188eu_set_hal_ops(_adapter * padapter)
{
	struct hal_ops	*pHalFunc = &padapter->HalFunc;

_func_enter_;

#ifdef CONFIG_CONCURRENT_MODE
	if(padapter->isprimary)
#endif //CONFIG_CONCURRENT_MODE
	{
		padapter->HalData = rtw_zvmalloc(sizeof(HAL_DATA_TYPE));
		if(padapter->HalData == NULL){
			DBG_8192C("cant not alloc memory for HAL DATA \n");
		}
	}

	//_rtw_memset(padapter->HalData, 0, sizeof(HAL_DATA_TYPE));
	padapter->hal_data_sz = sizeof(HAL_DATA_TYPE);

	pHalFunc->hal_power_on = _InitPowerOn_8188EU;
	pHalFunc->hal_power_off = hal_poweroff_8188eu;
	
	pHalFunc->hal_init = &rtl8188eu_hal_init;
	pHalFunc->hal_deinit = &rtl8188eu_hal_deinit;

	//pHalFunc->free_hal_data = &rtl8192c_free_hal_data;

	pHalFunc->inirp_init = &rtl8188eu_inirp_init;
	pHalFunc->inirp_deinit = &rtl8188eu_inirp_deinit;

	pHalFunc->init_xmit_priv = &rtl8188eu_init_xmit_priv;
	pHalFunc->free_xmit_priv = &rtl8188eu_free_xmit_priv;

	pHalFunc->init_recv_priv = &rtl8188eu_init_recv_priv;
	pHalFunc->free_recv_priv = &rtl8188eu_free_recv_priv;
#ifdef CONFIG_SW_LED
	pHalFunc->InitSwLeds = &rtl8188eu_InitSwLeds;
	pHalFunc->DeInitSwLeds = &rtl8188eu_DeInitSwLeds;
#else //case of hw led or no led
	pHalFunc->InitSwLeds = NULL;
	pHalFunc->DeInitSwLeds = NULL;	
#endif//CONFIG_SW_LED
	
	pHalFunc->init_default_value = &rtl8188eu_init_default_value;
	pHalFunc->intf_chip_configure = &rtl8188eu_interface_configure;
	pHalFunc->read_adapter_info = &ReadAdapterInfo8188EU;

	//pHalFunc->set_bwmode_handler = &PHY_SetBWMode8192C;
	//pHalFunc->set_channel_handler = &PHY_SwChnl8192C;

	//pHalFunc->hal_dm_watchdog = &rtl8192c_HalDmWatchDog;


	pHalFunc->SetHwRegHandler = &SetHwReg8188EU;
	pHalFunc->GetHwRegHandler = &GetHwReg8188EU;
  	pHalFunc->GetHalDefVarHandler = &GetHalDefVar8188EUsb;
 	pHalFunc->SetHalDefVarHandler = &SetHalDefVar8188EUsb;

	pHalFunc->SetBeaconRelatedRegistersHandler = &SetBeaconRelatedRegisters8188EUsb;

	//pHalFunc->Add_RateATid = &rtl8192c_Add_RateATid;

	pHalFunc->hal_xmit = &rtl8188eu_hal_xmit;
	pHalFunc->mgnt_xmit = &rtl8188eu_mgnt_xmit;
	pHalFunc->hal_xmitframe_enqueue = &rtl8188eu_hal_xmitframe_enqueue;


#ifdef CONFIG_HOSTAPD_MLME
	pHalFunc->hostap_mgnt_xmit_entry = &rtl8188eu_hostap_mgnt_xmit_entry;
#endif
	pHalFunc->interface_ps_func = &rtl8188eu_ps_func;

#ifdef CONFIG_XMIT_THREAD_MODE
	pHalFunc->xmit_thread_handler = &rtl8188eu_xmit_buf_handler;
#endif
	rtl8188e_set_hal_ops(pHalFunc);
_func_exit_;

}

