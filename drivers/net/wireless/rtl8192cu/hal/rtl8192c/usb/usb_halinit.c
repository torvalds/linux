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

#include <rtl8192c_hal.h>
#include <rtl8192c_led.h>

#ifdef DBG_CONFIG_ERROR_DETECT
#include "rtl8192c_sreset.h"
#endif

#ifdef CONFIG_IOL
#include <rtw_iol.h>
#endif

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

//endpoint number 1,2,3,4,5
// bult in : 1
// bult out: 2 (High)
// bult out: 3 (Normal) for 3 out_ep, (Low) for 2 out_ep
// interrupt in: 4
// bult out: 5 (Low) for 3 out_ep


static VOID
_OneOutEpMapping(
	IN	HAL_DATA_TYPE	*pHalData
	)
{
	//only endpoint number 0x02

	pHalData->Queue2EPNum[0] = pHalData->RtBulkOutPipe[0];//VO
	pHalData->Queue2EPNum[1] = pHalData->RtBulkOutPipe[0];//VI
	pHalData->Queue2EPNum[2] = pHalData->RtBulkOutPipe[0];//BE
	pHalData->Queue2EPNum[3] = pHalData->RtBulkOutPipe[0];//BK
	
	pHalData->Queue2EPNum[4] = pHalData->RtBulkOutPipe[0];//BCN
	pHalData->Queue2EPNum[5] = pHalData->RtBulkOutPipe[0];//MGT
	pHalData->Queue2EPNum[6] = pHalData->RtBulkOutPipe[0];//HIGH
	pHalData->Queue2EPNum[7] = pHalData->RtBulkOutPipe[0];//TXCMD
}


static VOID
_TwoOutEpMapping(
	IN	BOOLEAN			IsTestChip,
	IN	HAL_DATA_TYPE	*pHalData,
	IN	BOOLEAN	 		bWIFICfg
	)
{

/*
#define VO_QUEUE_INX		0
#define VI_QUEUE_INX		1
#define BE_QUEUE_INX		2
#define BK_QUEUE_INX		3
#define BCN_QUEUE_INX	4
#define MGT_QUEUE_INX	5
#define HIGH_QUEUE_INX	6
#define TXCMD_QUEUE_INX	7
*/

	if(IsTestChip && bWIFICfg){ // test chip && wmm
	
		
		//	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA 
		//{  1, 	0, 	1, 	0, 	0, 	0, 	0, 	0, 		0	};			
		//0:H(end_number=0x02), 1:L (end_number=0x03)

		pHalData->Queue2EPNum[0] = pHalData->RtBulkOutPipe[0];//VO
		pHalData->Queue2EPNum[1] = pHalData->RtBulkOutPipe[1];//VI
		pHalData->Queue2EPNum[2] = pHalData->RtBulkOutPipe[0];//BE
		pHalData->Queue2EPNum[3] = pHalData->RtBulkOutPipe[1];//BK
		
		pHalData->Queue2EPNum[4] = pHalData->RtBulkOutPipe[0];//BCN
		pHalData->Queue2EPNum[5] = pHalData->RtBulkOutPipe[0];//MGT
		pHalData->Queue2EPNum[6] = pHalData->RtBulkOutPipe[0];//HIGH
		pHalData->Queue2EPNum[7] = pHalData->RtBulkOutPipe[0];//TXCMD
	}
	else if(!IsTestChip && bWIFICfg){ // Normal chip && wmm
		
		//	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA 
		//{  0, 	1, 	0, 	1, 	0, 	0, 	0, 	0, 		0	};
		//0:H(end_number=0x02), 1:L (end_number=0x03)

		pHalData->Queue2EPNum[0] = pHalData->RtBulkOutPipe[1];//VO
		pHalData->Queue2EPNum[1] = pHalData->RtBulkOutPipe[0];//VI
		pHalData->Queue2EPNum[2] = pHalData->RtBulkOutPipe[1];//BE
		pHalData->Queue2EPNum[3] = pHalData->RtBulkOutPipe[0];//BK
		
		pHalData->Queue2EPNum[4] = pHalData->RtBulkOutPipe[0];//BCN
		pHalData->Queue2EPNum[5] = pHalData->RtBulkOutPipe[0];//MGT
		pHalData->Queue2EPNum[6] = pHalData->RtBulkOutPipe[0];//HIGH
		pHalData->Queue2EPNum[7] = pHalData->RtBulkOutPipe[0];//TXCMD
	}
	else{//typical setting
		
		//BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA 
		//{  1, 	1, 	0, 	0, 	0, 	0, 	0, 	0, 		0	};			
		//0:H(end_number=0x02), 1:L (end_number=0x03)

		pHalData->Queue2EPNum[0] = pHalData->RtBulkOutPipe[0];//VO
		pHalData->Queue2EPNum[1] = pHalData->RtBulkOutPipe[0];//VI
		pHalData->Queue2EPNum[2] = pHalData->RtBulkOutPipe[1];//BE
		pHalData->Queue2EPNum[3] = pHalData->RtBulkOutPipe[1];//BK
		
		pHalData->Queue2EPNum[4] = pHalData->RtBulkOutPipe[0];//BCN
		pHalData->Queue2EPNum[5] = pHalData->RtBulkOutPipe[0];//MGT
		pHalData->Queue2EPNum[6] = pHalData->RtBulkOutPipe[0];//HIGH
		pHalData->Queue2EPNum[7] = pHalData->RtBulkOutPipe[0];//TXCMD
	}
	
}


static VOID _ThreeOutEpMapping(
	IN	HAL_DATA_TYPE	*pHalData,
	IN	BOOLEAN	 		bWIFICfg
	)
{
	if(bWIFICfg){//for WMM

		//	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA 
		//{  1, 	2, 	1, 	0, 	0, 	0, 	0, 	0, 		0	};
		//0:H(end_number=0x02), 1:N(end_number=0x03), 2:L (end_number=0x05)

		pHalData->Queue2EPNum[0] = pHalData->RtBulkOutPipe[0];//VO
		pHalData->Queue2EPNum[1] = pHalData->RtBulkOutPipe[1];//VI
		pHalData->Queue2EPNum[2] = pHalData->RtBulkOutPipe[2];//BE
		pHalData->Queue2EPNum[3] = pHalData->RtBulkOutPipe[1];//BK
		
		pHalData->Queue2EPNum[4] = pHalData->RtBulkOutPipe[0];//BCN
		pHalData->Queue2EPNum[5] = pHalData->RtBulkOutPipe[0];//MGT
		pHalData->Queue2EPNum[6] = pHalData->RtBulkOutPipe[0];//HIGH
		pHalData->Queue2EPNum[7] = pHalData->RtBulkOutPipe[0];//TXCMD
	}
	else{//typical setting

		//	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA 
		//{  2, 	2, 	1, 	0, 	0, 	0, 	0, 	0, 		0	};			
		//0:H(end_number=0x02), 1:N(end_number=0x03), 2:L (end_number=0x05)
		pHalData->Queue2EPNum[0] = pHalData->RtBulkOutPipe[0];//VO
		pHalData->Queue2EPNum[1] = pHalData->RtBulkOutPipe[1];//VI
		pHalData->Queue2EPNum[2] = pHalData->RtBulkOutPipe[2];//BE
		pHalData->Queue2EPNum[3] = pHalData->RtBulkOutPipe[2];//BK
		
		pHalData->Queue2EPNum[4] = pHalData->RtBulkOutPipe[0];//BCN
		pHalData->Queue2EPNum[5] = pHalData->RtBulkOutPipe[0];//MGT
		pHalData->Queue2EPNum[6] = pHalData->RtBulkOutPipe[0];//HIGH
		pHalData->Queue2EPNum[7] = pHalData->RtBulkOutPipe[0];//TXCMD
	}

}

static BOOLEAN
_MappingOutEP(
	IN	PADAPTER	pAdapter,
	IN	u8		NumOutPipe,
	IN	BOOLEAN		IsTestChip
	)
{		
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct registry_priv *pregistrypriv = &pAdapter->registrypriv;

	BOOLEAN	 bWIFICfg = (pregistrypriv->wifi_spec) ?_TRUE:_FALSE;
	
	BOOLEAN result = _TRUE;

	switch(NumOutPipe)
	{
		case 2:
			_TwoOutEpMapping(IsTestChip, pHalData, bWIFICfg);
			break;
		case 3:
			// Test chip doesn't support three out EPs.
			if(IsTestChip){
				return _FALSE;
			}			
			_ThreeOutEpMapping(pHalData, bWIFICfg);
			break;
		case 1:
			_OneOutEpMapping(pHalData);
			break;
		default:
			result = _FALSE;
			break;
	}

	return result;
	
}

static VOID
_ConfigTestChipOutEP(
	IN	PADAPTER	pAdapter,
	IN	u8		NumOutPipe
	)
{
	u8			value8,txqsele;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);

	pHalData->OutEpQueueSel = 0;
	pHalData->OutEpNumber	= 0;

	value8 = rtw_read8(pAdapter, REG_TEST_SIE_OPTIONAL);
	value8 = (value8 & USB_TEST_EP_MASK) >> USB_TEST_EP_SHIFT;
	
	switch(value8)
	{
		case 0:		// 2 bulk OUT, 1 bulk IN
		case 3:		
			pHalData->OutEpQueueSel = TX_SELE_HQ | TX_SELE_LQ;
			pHalData->OutEpNumber	= 2;
			//RT_TRACE(COMP_INIT,  DBG_LOUD, ("EP Config: 2 bulk OUT, 1 bulk IN\n"));
			break;
		case 1:		// 1 bulk IN/OUT => map all endpoint to Low queue
		case 2:		// 1 bulk IN, 1 bulk OUT => map all endpoint to High queue
			txqsele = rtw_read8(pAdapter, REG_TEST_USB_TXQS);
			if(txqsele & 0x0F){//map all endpoint to High queue
				pHalData->OutEpQueueSel  = TX_SELE_HQ;
			}
			else if(txqsele&0xF0){//map all endpoint to Low queue
				pHalData->OutEpQueueSel  =  TX_SELE_LQ;
			}
			pHalData->OutEpNumber	= 1;
			//RT_TRACE(COMP_INIT,  DBG_LOUD, ("%s\n", ((1 == value8) ? "1 bulk IN/OUT" : "1 bulk IN, 1 bulk OUT")));
			break;
		default:
			break;
	}

	// TODO: Error recovery for this case
	//RT_ASSERT((NumOutPipe == pHalData->OutEpNumber), ("Out EP number isn't match! %d(Descriptor) != %d (SIE reg)\n", (u4Byte)NumOutPipe, (u4Byte)pHalData->OutEpNumber));

}



static VOID
_ConfigNormalChipOutEP(
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
	BOOLEAN			isNormalChip;

	//may be update UPHY Parameter == georgia 
	

	// ReadAdapterInfo8192C also call _ReadChipVersion too.
	// Since we need dynamic config EP mapping, so we call this function to get chip version.
	// We can remove _ReadChipVersion from ReadAdapterInfo8192C later.
	//pHalData->VersionID = rtl8192c_ReadChipVersion(pAdapter);

	isNormalChip = IS_NORMAL_CHIP(pHalData->VersionID);

	if(isNormalChip){
		_ConfigNormalChipOutEP(pAdapter, NumOutPipe);
	}
	else{
		_ConfigTestChipOutEP(pAdapter, NumOutPipe);
	}

	// Normal chip with one IN and one OUT doesn't have interrupt IN EP.
	if(isNormalChip && (1 == pHalData->OutEpNumber)){
		if(1 != NumInPipe){
			return result;
		}
	}

	// All config other than above support one Bulk IN and one Interrupt IN.
	//if(2 != NumInPipe){
	//	return result;
	//}

	result = _MappingOutEP(pAdapter, NumOutPipe, !isNormalChip);
	
	return result;

}

void rtl8192cu_interface_configure(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	struct dvobj_priv	*pdvobjpriv = &padapter->dvobjpriv;

	if (pdvobjpriv->ishighspeed == _TRUE)
	{
		pHalData->UsbBulkOutSize = USB_HIGH_SPEED_BULK_SIZE;//512 bytes
	}
	else
	{
		pHalData->UsbBulkOutSize = USB_FULL_SPEED_BULK_SIZE;//64 bytes
	}

	pHalData->interfaceIndex = pdvobjpriv->InterfaceNumber;
	pHalData->RtBulkInPipe = pdvobjpriv->ep_num[0];
	pHalData->RtBulkOutPipe[0] = pdvobjpriv->ep_num[1];
	pHalData->RtBulkOutPipe[1] = pdvobjpriv->ep_num[2];
	pHalData->RtIntInPipe = pdvobjpriv->ep_num[3];
	pHalData->RtBulkOutPipe[2] = pdvobjpriv->ep_num[4];

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

static u8 _InitPowerOn(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8	ret = _SUCCESS;
	u16	value16=0;
	u8	value8 = 0;
	u32  value32 = 0;

	// polling autoload done.
	u32	pollingCount = 0;

	do
	{
		if(rtw_read8(padapter, REG_APS_FSMCO) & PFM_ALDN){
			//RT_TRACE(COMP_INIT,DBG_LOUD,("Autoload Done!\n"));
			break;
		}

		if(pollingCount++ > POLLING_READY_TIMEOUT_COUNT){
			//RT_TRACE(COMP_INIT,DBG_SERIOUS,("Failed to polling REG_APS_FSMCO[PFM_ALDN] done!\n"));
			return _FAIL;
		}
				
	}while(_TRUE);


//	For hardware power on sequence.

	//0.	RSV_CTRL 0x1C[7:0] = 0x00			// unlock ISO/CLK/Power control register
	rtw_write8(padapter, REG_RSV_CTRL, 0x0);	
	// Power on when re-enter from IPS/Radio off/card disable
	rtw_write8(padapter, REG_SPS0_CTRL, 0x2b);//enable SPS into PWM mode
/*
	value16 = PlatformIORead2Byte(Adapter, REG_AFE_XTAL_CTRL);//enable AFE clock
	value16 &=  (~XTAL_GATE_AFE);
	PlatformIOWrite2Byte(Adapter,REG_AFE_XTAL_CTRL, value16 );		
*/
	
	rtw_udelay_os(100);//PlatformSleepUs(150);//this is not necessary when initially power on

	value8 = rtw_read8(padapter, REG_LDOV12D_CTRL);	
	if(0== (value8 & LDV12_EN) ){
		value8 |= LDV12_EN;
		rtw_write8(padapter, REG_LDOV12D_CTRL, value8);	
		//RT_TRACE(COMP_INIT, DBG_LOUD, (" power-on :REG_LDOV12D_CTRL Reg0x21:0x%02x.\n",value8));
		rtw_udelay_os(100);//PlatformSleepUs(100);//this is not necessary when initially power on
		value8 = rtw_read8(padapter, REG_SYS_ISO_CTRL);
		value8 &= ~ISO_MD2PP;
		rtw_write8(padapter, REG_SYS_ISO_CTRL, value8);			
	}	
	
	// auto enable WLAN
	pollingCount = 0;
	value16 = rtw_read16(padapter, REG_APS_FSMCO);
	value16 |= APFM_ONMAC;
	rtw_write16(padapter, REG_APS_FSMCO, value16);

	do
	{
		if(0 == (rtw_read16(padapter, REG_APS_FSMCO) & APFM_ONMAC)){
			//RT_TRACE(COMP_INIT,DBG_LOUD,("MAC auto ON okay!\n"));
			break;
		}

		if(pollingCount++ > POLLING_READY_TIMEOUT_COUNT){
			//RT_TRACE(COMP_INIT,DBG_SERIOUS,("Failed to polling REG_APS_FSMCO[APFM_ONMAC] done!\n"));
			return _FAIL;
		}
				
	}while(_TRUE);

	//Enable Radio ,GPIO ,and LED function
	rtw_write16(padapter,REG_APS_FSMCO,0x0812);

#ifdef CONFIG_AUTOSUSPEND
	//for usb Combo card ,BT
	if((BOARD_USB_COMBO == pHalData->BoardType)&&(padapter->registrypriv.usbss_enable))
	{
		value32 =  rtw_read32(padapter, REG_APS_FSMCO);
		value32 |= (SOP_ABG|SOP_AMB|XOP_BTCK);
		rtw_write32(padapter, REG_APS_FSMCO, value32);		
	}
#endif	

	// release RF digital isolation
	value16 = rtw_read16(padapter, REG_SYS_ISO_CTRL);
	value16 &= ~ISO_DIOR;
	rtw_write16(padapter, REG_SYS_ISO_CTRL, value16);

	// Enable MAC DMA/WMAC/SCHEDULE/SEC block
	value16 = rtw_read16(padapter, REG_CR);
	value16 |= (HCI_TXDMA_EN | HCI_RXDMA_EN | TXDMA_EN | RXDMA_EN
				| PROTOCOL_EN | SCHEDULE_EN | MACTXEN | MACRXEN | ENSEC);
	rtw_write16(padapter, REG_CR, value16);
	
	//tynli_test for suspend mode.
	{
		rtw_write8(padapter,  0xfe10, 0x19);
	}

	// 2010/11/22 MH For slim combo debug mode check.
	if (pHalData->BoardType == BOARD_USB_COMBO)
	{
		if (pHalData->SlimComboDbg == _TRUE)
		{
			DBG_8192C("SlimComboDbg == TRUE\n");

			// 1. SIC?Test Mode 中, Debug Ports 會自動 Enable, 所以 Driver 上來後, 
			//	要關掉請設定 0x 00[7] -> "1", 將它 Disable.   effect if not: power consumption increase
			rtw_write8(padapter, REG_SYS_ISO_CTRL, rtw_read8(padapter, REG_SYS_ISO_CTRL)|BIT7);

			// 2. SIC?Test Mode 中, GPIO-8?會 report Power State 所以 Driver 上來後, 請設定? 0x04[6] -> "1" 將它 Disable
			// effect if not: GPIO-8 could not be GPIO or LED function
			rtw_write8(padapter, REG_APS_FSMCO, rtw_read8(padapter, REG_APS_FSMCO)|BIT6);

			// 3. SIC Test Mode 中, EESK, EECS 會 report?Host Clock status, 所以 Driver 上來後, 請設定? 0x40[4] -> "1" 將它切成 EEPROM 使用 Pin (autoload still from Efuse)
			//  effect if not:power consumption increase
			value8 = rtw_read8(padapter, REG_GPIO_MUXCFG)|BIT4 ;
		#ifdef CONFIG_BT_COEXIST	
			// 2011/01/26 MH UMB-B cut bug. We need to support the modification.
			if (IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID) &&
				pHalData->bt_coexist.BT_Coexist)
			{
				value8 |= (BIT5);	
			}
		#endif
			rtw_write8(padapter, REG_GPIO_MUXCFG,value8 );
			
			
			// 4. SIC Test Mode 中,?SIC Debug ports 會自動 Enable , 所以 Driver 上來後馬上, 請設定? 0x40[15:11] -> “0x00”, 將它Disable
			//  4.1Two Steps setting for safety: 0x40[15,13,12, 11] -> "0", then ?0x40[14] -> "0"
			// effect if not: Host could not transfer packets, and GPIO-3,2 will occupied by SIC then Co-exist could not work.
			rtw_write16(padapter, REG_GPIO_MUXCFG, (rtw_read16(padapter, REG_GPIO_MUXCFG)&0x07FF)|BIT14);
			rtw_write16(padapter, REG_GPIO_MUXCFG, rtw_read16(padapter, REG_GPIO_MUXCFG)&0x07FF);
		}
	}


	// 2011/02/18 To Fix RU LNA  power leakage problem. We need to execute below below in
	// Adapter init and halt sequence. Accordingto EEchou's opinion, we can enable the ability for all
	// IC. According to Johnny's opinion, only RU will meet the condition.
	if (IS_HARDWARE_TYPE_8192C(padapter) && (pHalData->BoardType == BOARD_USB_High_PA))
		rtw_write32(padapter, rFPGA0_XCD_RFParameter, rtw_read32(padapter, rFPGA0_XCD_RFParameter)&(~BIT1));
	return ret;

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
	BOOLEAN		isNormal = IS_NORMAL_CHIP(pHalData->VersionID);
	BOOLEAN		is92C = IS_92C_SERIAL(pHalData->VersionID);
	
	//FIXED PA current issue
	//efuse_one_byte_read(padapter, 0x1FA, &pa_setting);
	pa_setting = EFUSE_Read1Byte(padapter, 0x1FA);

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("_InitPABias 0x1FA 0x%x \n",pa_setting));

	if(!(pa_setting & BIT0))
	{
		PHY_SetRFReg(padapter, RF90_PATH_A, 0x15, 0x0FFFFF, 0x0F406);
		PHY_SetRFReg(padapter, RF90_PATH_A, 0x15, 0x0FFFFF, 0x4F406);		
		PHY_SetRFReg(padapter, RF90_PATH_A, 0x15, 0x0FFFFF, 0x8F406);		
		PHY_SetRFReg(padapter, RF90_PATH_A, 0x15, 0x0FFFFF, 0xCF406);		
		//RT_TRACE(COMP_INIT, DBG_LOUD, ("PA BIAS path A\n"));
	}	

	if(!(pa_setting & BIT1) && isNormal && is92C)
	{
		PHY_SetRFReg(padapter,RF90_PATH_B, 0x15, 0x0FFFFF, 0x0F406);
		PHY_SetRFReg(padapter,RF90_PATH_B, 0x15, 0x0FFFFF, 0x4F406);		
		PHY_SetRFReg(padapter,RF90_PATH_B, 0x15, 0x0FFFFF, 0x8F406);		
		PHY_SetRFReg(padapter,RF90_PATH_B, 0x15, 0x0FFFFF, 0xCF406);
		//RT_TRACE(COMP_INIT, DBG_LOUD, ("PA BIAS path B\n"));	
	}

	if(!(pa_setting & BIT4))
	{
		pa_setting = rtw_read8(padapter, 0x16);
		pa_setting &= 0x0F;
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

#if MP_DRIVER != 1
		if(pbtpriv->BT_Ant_isolation)
		{
			rtw_write8( padapter,REG_GPIO_MUXCFG, 0xa0);
			DBG_8192C("BT write 0x%x = 0x%x\n", REG_GPIO_MUXCFG, 0xa0);
		}
#endif		

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


static u8 InitLLTTable(
	IN  PADAPTER	Adapter,
	IN	u32		boundary
	)
{
	u8	status = _SUCCESS;
	u32		i;

#ifdef CONFIG_IOL_LLT
	{
		struct xmit_frame	*xmit_frame;
		if((xmit_frame=rtw_IOL_accquire_xmit_frame(Adapter)) == NULL)
			return _FAIL;

		rtw_IOL_append_LLT_cmd(xmit_frame, boundary);
		status = rtw_IOL_exec_cmds_sync(Adapter, xmit_frame, 1000);
	}
#else
	for(i = 0 ; i < (boundary - 1) ; i++){
		status = _LLTWrite(Adapter, i , i + 1);
		if(_SUCCESS != status){
			return status;
		}
	}

	// end of list
	status = _LLTWrite(Adapter, (boundary - 1), 0xFF); 
	if(_SUCCESS != status){
		return status;
	}

	// Make the other pages as ring buffer
	// This ring buffer is used as beacon buffer if we config this MAC as two MAC transfer.
	// Otherwise used as local loopback buffer. 
	for(i = boundary ; i < LAST_ENTRY_OF_TX_PKT_BUFFER ; i++){
		status = _LLTWrite(Adapter, i, (i + 1)); 
		if(_SUCCESS != status){
			return status;
		}
	}
	
	// Let last entry point to the start entry of ring buffer
	status = _LLTWrite(Adapter, LAST_ENTRY_OF_TX_PKT_BUFFER, boundary);
	if(_SUCCESS != status){
		return status;
	}
#endif

	return status;
	
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
	BOOLEAN			isNormalChip = IS_NORMAL_CHIP(pHalData->VersionID);
	
	u32			outEPNum	= (u32)pHalData->OutEpNumber;
	u32			numHQ		= 0;
	u32			numLQ		= 0;
	u32			numNQ		= 0;
	u32			numPubQ;
	u32			value32;
	u8			value8;
	BOOLEAN		bWiFiConfig	= pregistrypriv->wifi_spec;
	//u32			txQPageNum, txQPageUnit,txQRemainPage;

#if 0
	if(!pregistrypriv->wifi_spec){		
		numPubQ = (isNormalChip) ? NORMAL_PAGE_NUM_PUBQ : TEST_PAGE_NUM_PUBQ;
		//RT_ASSERT((numPubQ < TX_TOTAL_PAGE_NUMBER), ("Public queue page number is great than total tx page number.\n"));
		txQPageNum = TX_TOTAL_PAGE_NUMBER - numPubQ;

		//RT_ASSERT((0 == txQPageNum%txQPageNum), ("Total tx page number is not dividable!\n"));
		
		txQPageUnit = txQPageNum/outEPNum;
		txQRemainPage = txQPageNum % outEPNum;

		if(pHalData->OutEpQueueSel & TX_SELE_HQ){
			numHQ = txQPageUnit;
		}
		if(pHalData->OutEpQueueSel & TX_SELE_LQ){
			numLQ = txQPageUnit;
		}
		// HIGH priority queue always present in the configuration of 2 or 3 out-ep 
		// so ,remainder pages have assigned to High queue
		if((outEPNum>1) && (txQRemainPage)){			
			numHQ += txQRemainPage;
		}

		// NOTE: This step shall be proceed before writting REG_RQPN.
		if(isNormalChip){
			if(pHalData->OutEpQueueSel & TX_SELE_NQ){
				numNQ = txQPageUnit;
			}
			value8 = (u8)_NPQ(numNQ);
			rtw_write8(Adapter, REG_RQPN_NPQ, value8);
		}
		//RT_ASSERT(((numHQ + numLQ + numNQ + numPubQ) < TX_PAGE_BOUNDARY), ("Total tx page number is greater than tx boundary!\n"));
	}
	else
#endif
	{ //for WMM 
		//RT_ASSERT((outEPNum>=2), ("for WMM ,number of out-ep must more than or equal to 2!\n"));

		numPubQ = (isNormalChip) 	? ((bWiFiConfig)?WMM_NORMAL_PAGE_NUM_PUBQ:NORMAL_PAGE_NUM_PUBQ)
								:WMM_TEST_PAGE_NUM_PUBQ;		

		if(pHalData->OutEpQueueSel & TX_SELE_HQ){
			numHQ = (isNormalChip)?((bWiFiConfig)?WMM_NORMAL_PAGE_NUM_HPQ:NORMAL_PAGE_NUM_HPQ)
								:WMM_TEST_PAGE_NUM_HPQ;
		}

		if(pHalData->OutEpQueueSel & TX_SELE_LQ){
			numLQ = (isNormalChip)?((bWiFiConfig)?WMM_NORMAL_PAGE_NUM_LPQ:NORMAL_PAGE_NUM_LPQ)
								:WMM_TEST_PAGE_NUM_LPQ;
		}
		// NOTE: This step shall be proceed before writting REG_RQPN.
		if(isNormalChip){			
			if(pHalData->OutEpQueueSel & TX_SELE_NQ){
				numNQ = (bWiFiConfig)?WMM_NORMAL_PAGE_NUM_NPQ:NORMAL_PAGE_NUM_NPQ;
			}
			value8 = (u8)_NPQ(numNQ);
			rtw_write8(Adapter, REG_RQPN_NPQ, value8);
		}
	}

	// TX DMA
	value32 = _HPQ(numHQ) | _LPQ(numLQ) | _PUBQ(numPubQ) | LD_RQPN;	
	rtw_write32(Adapter, REG_RQPN, value32);	
}

static void _InitID(IN  PADAPTER Adapter)
{
	int i;	 
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
	
	for(i=0; i<6; i++)
	{
		rtw_write8(Adapter, (REG_MACID+i), pEEPROM->mac_addr[i]);		 	
	}

/*
	NicIFSetMacAddress(Adapter, Adapter->PermanentAddress);
	//Ziv test
#if 1
	{
		u1Byte sMacAddr[6] = {0};
		u4Byte i;
		
		for(i = 0 ; i < MAC_ADDR_LEN ; i++){
			sMacAddr[i] = PlatformIORead1Byte(Adapter, (REG_MACID + i));
		}
		RT_PRINT_ADDR(COMP_INIT|COMP_EFUSE, DBG_LOUD, "Read back MAC Addr: ", sMacAddr);
	}
#endif

#if 0
	u4Byte nMAR 	= 0xFFFFFFFF;
	u8 m_MacID[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
	u8 m_BSSID[] = {0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
	int i;
	
	_SetMacID(Adapter, Adapter->PermanentAddress);
	_SetBSSID(Adapter, m_BSSID);

	//set MAR
	PlatformIOWrite4Byte(Adapter, REG_MAR, nMAR);
	PlatformIOWrite4Byte(Adapter, REG_MAR+4, nMAR);
#endif
*/
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
		txpktbuf_bndy = ( IS_NORMAL_CHIP( pHalData->VersionID))?WMM_NORMAL_TX_PAGE_BOUNDARY
															:WMM_TEST_TX_PAGE_BOUNDARY;
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
_InitTestChipQueuePriority(
	IN	PADAPTER Adapter
	)
{
	u8	hq_sele ;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	
	switch(pHalData->OutEpNumber)
	{
		case 2:	// (TX_SELE_HQ|TX_SELE_LQ)
			if(!pregistrypriv->wifi_spec)//typical setting			
				hq_sele =  HQSEL_VOQ | HQSEL_VIQ | HQSEL_MGTQ | HQSEL_HIQ ;
			else	//for WMM
				hq_sele = HQSEL_VOQ | HQSEL_BEQ | HQSEL_MGTQ | HQSEL_HIQ ;
			break;
		case 1:
			if(TX_SELE_LQ == pHalData->OutEpQueueSel ){//map all endpoint to Low queue
				 hq_sele = 0;
			}
			else if(TX_SELE_HQ == pHalData->OutEpQueueSel){//map all endpoint to High queue
				hq_sele =  HQSEL_VOQ | HQSEL_VIQ | HQSEL_BEQ | HQSEL_BKQ | HQSEL_MGTQ | HQSEL_HIQ ;
			}		
			break;
		default:
			//RT_ASSERT(FALSE,("Shall not reach here!\n"));
			break;
	}
	rtw_write8(Adapter, (REG_TRXDMA_CTRL+1), hq_sele);
}


static VOID
_InitQueuePriority(
	IN  PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

	if(IS_NORMAL_CHIP( pHalData->VersionID)){
		_InitNormalChipQueuePriority(Adapter);
	}
	else{
		_InitTestChipQueuePriority(Adapter);
	}
}

static VOID
_InitHardwareDropIncorrectBulkOut(
	IN  PADAPTER Adapter
	)
{
	u32	value32 = rtw_read32(Adapter, REG_TXDMA_OFFSET_CHK);
	value32 |= DROP_DATA_EN;
	rtw_write32(Adapter, REG_TXDMA_OFFSET_CHK, value32);
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
	pHalData->ReceiveConfig = RCR_AAP | RCR_APM | RCR_AM | RCR_AB |RCR_CBSSID_DATA| RCR_CBSSID_BCN| RCR_APP_ICV | RCR_AMF | RCR_HTC_LOC_CTRL | RCR_APP_MIC | RCR_APP_PHYSTS;
#if (0 == RTL8192C_RX_PACKET_NO_INCLUDE_CRC)
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
#ifdef RTL8192CU_ADHOC_WORKAROUND_SETTING
	rtw_write8(Adapter, REG_BCN_MAX_ERR, 0xFF);	
#else
	//rtw_write8(Adapter, REG_BCN_MAX_ERR, (InfraMode ? 0xFF : 0x10));	
#endif
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
	PlatformEFIOWrite1Byte(Adapter, REG_BWOPMODE, regBwOpMode);

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
	
	PlatformEFIOWrite1Byte(Adapter, REG_AMPDU_MIN_SPACE, Adapter->MgntInfo.MinSpaceCfg);
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
	rtw_write8(Adapter, REG_DRVERLYINT, DRIVER_EARLY_INT_TIME);// 5ms
	rtw_write8(Adapter, REG_BCNDMATIM, BCN_DMA_ATIME_INT_TIME); // 2ms

	// Suggested by designer timchen. Change beacon AIFS to the largest number
	// beacause test chip does not contension before sending beacon. by tynli. 2009.11.03
	if(IS_NORMAL_CHIP( pHalData->VersionID)){
		rtw_write16(Adapter, REG_BCNTCFG, 0x660F);
	}
	else{		
		rtw_write16(Adapter, REG_BCNTCFG, 0x66FF);
	}

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
#if RTL8192CU_ADHOC_WORKAROUND_SETTING
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);	
	pHalData->RegBcnCtrlVal = rtw_read8(Adapter, REG_BCN_CTRL);
	pHalData->RegTxPause = rtw_read8(Adapter, REG_TXPAUSE); 
	pHalData->RegFwHwTxQCtrl = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL+2);
	pHalData->RegReg542 = rtw_read8(Adapter, REG_TBTT_PROHIBIT+2);
#endif	
}

static VOID
_BeaconFunctionEnable(
	IN	PADAPTER		Adapter,
	IN	BOOLEAN			Enable,
	IN	BOOLEAN			Linked
	)
{
#if 0
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			value8 = 0;

	//value8 = Enable ? (EN_BCN_FUNCTION | EN_TXBCN_RPT) : EN_BCN_FUNCTION;

	if(_FALSE == Linked){		
		if(IS_NORMAL_CHIP( pHalData->VersionID)){
			value8 |= DIS_TSF_UDT0_NORMAL_CHIP;
		}
		else{
			value8 |= DIS_TSF_UDT0_TEST_CHIP;
		}
	}

	rtw_write8(Adapter, REG_BCN_CTRL, value8);
#else
	rtw_write8(Adapter, REG_BCN_CTRL, (BIT4 | BIT3 | BIT1));
	//SetBcnCtrlReg(Adapter, (BIT4 | BIT3 | BIT1), 0x00);
	//RT_TRACE(COMP_BEACON, DBG_LOUD, ("_BeaconFunctionEnable 0x550 0x%x\n", PlatformEFIORead1Byte(Adapter, 0x550)));			

	rtw_write8(Adapter, REG_RD_CTRL+1, 0x6F);	
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
			PHY_SetRFReg(Adapter, (RF90_RADIO_PATH_E)eRFPath, 0x4, 0xC00, 0x0);
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
	
	if((RF_1T1R == pHalData->rf_type))
	{	
		rtw_write32(Adapter, REG_LEDCFG0, rtw_read32(Adapter, REG_LEDCFG0)|BIT23);	
		PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, BIT13, 0x01);
		
		if(PHY_QueryBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, 0x300) == Antenna_A)
			pHalData->CurAntenna = Antenna_A;
		else
			pHalData->CurAntenna = Antenna_B;
		DBG_8192C("%s,Cur_ant:(%x)%s\n",__FUNCTION__,pHalData->CurAntenna,(pHalData->CurAntenna == Antenna_A)?"Antenna_A":"Antenna_B");
			
}


}
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
	struct pwrctrl_priv		*pwrctrlpriv = &Adapter->pwrctrlpriv;
	
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
	struct dvobj_priv	*pdvobjpriv = &Adapter->dvobjpriv;

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
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(pAdapter);
	u8	val8;
	rt_rf_power_state rfpowerstate = rf_off;

	if(pAdapter->pwrctrlpriv.bHWPowerdown)
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


u32 rtl8192cu_hal_init(PADAPTER Adapter)
{
	u8	val8 = 0;
	u32	boundary, status = _SUCCESS;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	struct pwrctrl_priv		*pwrctrlpriv = &Adapter->pwrctrlpriv;
	struct registry_priv	*pregistrypriv = &Adapter->registrypriv;
	u8	isNormal = IS_NORMAL_CHIP(pHalData->VersionID);
	u8	is92C = IS_92C_SERIAL(pHalData->VersionID);
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
		HAL_INIT_STAGES_INIT_LLTT,
		HAL_INIT_STAGES_MAC,
		HAL_INIT_STAGES_MISC02,
		HAL_INIT_STAGES_BB,
		HAL_INIT_STAGES_RF,
		HAL_INIT_STAGES_TURN_ON_BLOCK,
		HAL_INIT_STAGES_INIT_SECURITY,
		HAL_INIT_STAGES_MISC11,
		//HAL_INIT_STAGES_RF_PS,
		HAL_INIT_STAGES_IQK,
		HAL_INIT_STAGES_PW_TRACK,
		HAL_INIT_STAGES_LCK,
		HAL_INIT_STAGES_MISC21,
		HAL_INIT_STAGES_INIT_PABIAS,
		HAL_INIT_STAGES_BT_COEXIST,
		//HAL_INIT_STAGES_ANTENNA_SEL,
		HAL_INIT_STAGES_INIT_HAL_DM,
		HAL_INIT_STAGES_MISC31,
		HAL_INIT_STAGES_END,
		HAL_INIT_STAGES_NUM
	};

	char * hal_init_stages_str[] = {
		"HAL_INIT_STAGES_BEGIN",
		"HAL_INIT_STAGES_INIT_PW_ON",
		"HAL_INIT_STAGES_MISC01",
		"HAL_INIT_STAGES_DOWNLOAD_FW",
		"HAL_INIT_STAGES_INIT_LLTT",
		"HAL_INIT_STAGES_MAC",
		"HAL_INIT_STAGES_MISC02",
		"HAL_INIT_STAGES_BB",
		"HAL_INIT_STAGES_RF",
		"HAL_INIT_STAGES_TURN_ON_BLOCK",
		"HAL_INIT_STAGES_INIT_SECURITY",
		"HAL_INIT_STAGES_MISC11",
		//"HAL_INIT_STAGES_RF_PS",
		"HAL_INIT_STAGES_IQK",
		"HAL_INIT_STAGES_PW_TRACK",
		"HAL_INIT_STAGES_LCK",
		"HAL_INIT_STAGES_MISC21",
		"HAL_INIT_STAGES_INIT_PABIAS",
		"HAL_INIT_STAGES_BT_COEXIST",
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
	if(Adapter->pwrctrlpriv.bkeepfwalive)
	{
		_ps_open_RF(Adapter);

		if(pHalData->bIQKInitialized ){
			rtl8192c_PHY_IQCalibrate(Adapter,_TRUE);
		}
		else{
			rtl8192c_PHY_IQCalibrate(Adapter,_FALSE);
			pHalData->bIQKInitialized = _TRUE;
		}
		rtl8192c_dm_CheckTXPowerTracking(Adapter);
		rtl8192c_PHY_LCCalibrate(Adapter);

		goto exit;
	}

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_PW_ON);
	status = _InitPowerOn(Adapter);
	if(status == _FAIL){
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init power on!\n"));
		goto exit;
	}


HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC01);
	_InitQueueReservedPage(Adapter);
	_InitTxBufferBoundary(Adapter);		
	_InitQueuePriority(Adapter);
	_InitPageBoundary(Adapter);	
	_InitTransferPageSize(Adapter);


#if ENABLE_USB_DROP_INCORRECT_OUT
	_InitHardwareDropIncorrectBulkOut(Adapter);
#endif

	if(pHalData->bRDGEnable){
		_InitRDGSetting(Adapter);
	}

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_DOWNLOAD_FW);
#if (1 == MP_DRIVER)
	_InitRxSetting(Adapter);
	// Don't Download Firmware
	Adapter->bFWReady = _FALSE;
#elif RTL8192CU_FW_DOWNLOAD_ENABLE
	status = FirmwareDownload92C(Adapter);
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
#endif

	InitializeFirmwareVars92C(Adapter);

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

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_LLTT);
	if(!pregistrypriv->wifi_spec){
		boundary = TX_PAGE_BOUNDARY;
	}
	else{// for WMM
		boundary = (IS_NORMAL_CHIP(pHalData->VersionID))	?WMM_NORMAL_TX_PAGE_BOUNDARY
													:WMM_TEST_TX_PAGE_BOUNDARY;
	}															
	status =  InitLLTTable(Adapter, boundary);
	if(status == _FAIL){
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init LLT table\n"));
		goto exit;
	}

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MAC);
#if (HAL_MAC_ENABLE == 1)
	status = PHY_MACConfig8192C(Adapter);
	if(status == _FAIL)
	{
		goto exit;
	}
#endif

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC02);
	// Get Rx PHY status in order to report RSSI and others.
	_InitDriverInfoSize(Adapter, DRVINFO_SZ);

	_InitInterrupt(Adapter);
	_InitID(Adapter);//set mac_address
	_InitNetworkType(Adapter);//set msr	
	_InitWMACSetting(Adapter);
	_InitAdaptiveCtrl(Adapter);
	_InitEDCA(Adapter);
	_InitRateFallback(Adapter);
	_InitRetryFunction(Adapter);
	InitUsbAggregationSetting(Adapter);
	_InitOperationMode(Adapter);//todo
	_InitBeaconParameters(Adapter);
	_InitBeaconMaxError(Adapter, _TRUE);

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

	//
	//d. Initialize BB related configurations.
	//

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_BB);
#if (HAL_BB_ENABLE == 1)
	status = PHY_BBConfig8192C(Adapter);
	if(status == _FAIL)
	{
		goto exit;
	}
#endif

	// 92CU use 3-wire to r/w RF
	//pHalData->Rf_Mode = RF_OP_By_SW_3wire;

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_RF);
#if (HAL_RF_ENABLE == 1)
	status = PHY_RFConfig8192C(Adapter);	
	if(status == _FAIL)
	{
		goto exit;
	}

	if(IS_VENDOR_UMC_A_CUT(pHalData->VersionID) && !IS_92C_SERIAL(pHalData->VersionID))
	{
		PHY_SetRFReg(Adapter, RF90_PATH_A, RF_RX_G1, bMaskDWord, 0x30255);
		PHY_SetRFReg(Adapter, RF90_PATH_A, RF_RX_G2, bMaskDWord, 0x50a00);		
	}
#endif

	//
	// Joseph Note: Keep RfRegChnlVal for later use.
	//
	pHalData->RfRegChnlVal[0] = PHY_QueryRFReg(Adapter, (RF90_RADIO_PATH_E)0, RF_CHNLBW, bRFRegOffsetMask);
	pHalData->RfRegChnlVal[1] = PHY_QueryRFReg(Adapter, (RF90_RADIO_PATH_E)1, RF_CHNLBW, bRFRegOffsetMask);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_TURN_ON_BLOCK);
	_BBTurnOnBlock(Adapter);
	//NicIFSetMacAddress(padapter, padapter->PermanentAddress);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_SECURITY);
	invalidate_cam_all(Adapter);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC11);
	// 2010/12/17 MH We need to set TX power according to EFUSE content at first.
	PHY_SetTxPowerLevel8192C(Adapter, pHalData->CurrentChannel);

// Move by Neo for USB SS to below setp	
//_RfPowerSave(Adapter);

	if (!IS_92C_SERIAL( pHalData->VersionID) && (pHalData->AntDivCfg!=0))
	{ //for 88CU ,1T1R
		_InitAntenna_Selection(Adapter);
	}


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

#if (MP_DRIVER == 1)
	Adapter->mppriv.channel = pHalData->CurrentChannel;
	MPT_InitializeAdapter(Adapter, Adapter->mppriv.channel);
#else
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
			//PHY_SetRFReg(Adapter, (RF90_RADIO_PATH_E)eRFPath, 0x4, 0xC00, 0x0);
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
		//if(IS_HARDWARE_TYPE_8723U(Adapter))
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

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_IQK);
	// 2010/08/26 MH Merge from 8192CE.
	if(pwrctrlpriv->rf_pwrstate == rf_on)
	{
		if(pHalData->bIQKInitialized ){
			rtl8192c_PHY_IQCalibrate(Adapter,_TRUE);
		}
		else
		{
			rtl8192c_PHY_IQCalibrate(Adapter,_FALSE);
			pHalData->bIQKInitialized = _TRUE;
		}
		
HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_PW_TRACK);
		rtl8192c_dm_CheckTXPowerTracking(Adapter);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_LCK);
		rtl8192c_PHY_LCCalibrate(Adapter);
	}
#endif /* #if (MP_DRIVER == 1) */


HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC21);
#if RTL8192CU_ADHOC_WORKAROUND_SETTING
	_InitAdhocWorkaroundParams(Adapter);
#endif


#ifdef USB_INTERFERENCE_ISSUE
 //fixed USB interface interference issue
	rtw_write8(Adapter, 0xfe40, 0xe0);
	rtw_write8(Adapter, 0xfe41, 0x8d);
	rtw_write8(Adapter, 0xfe42, 0x80);
	rtw_write32(Adapter,0x20c,0xfd0320);
#if 1
	//2011/01/07 ,suggest by Johnny,for solved the problem that too many protocol error on USB bus
	if(!IS_VENDOR_UMC_A_CUT(pHalData->VersionID) )//&& !IS_92C_SERIAL(pHalData->VersionID))// TSMC , 8188
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

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_PABIAS);
	_InitPABias(Adapter);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_BT_COEXIST);
#ifdef CONFIG_BT_COEXIST
	_InitBTCoexist(Adapter);
#endif

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_HAL_DM);
	rtl8192c_InitHalDm(Adapter);

	// 2010/08/23 MH According to Alfred's suggestion, we need to to prevent HW enter
	// suspend mode automatically.
	//HwSuspendModeEnable92Cu(Adapter, _FALSE);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC31);
	rtw_write8(Adapter, 0x15, 0xe9);//suggest by Johnny for lower temperature
	//_dbg_dump_macreg(padapter);

	//misc
	{
		int i;		
		u8 mac_addr[6];
		for(i=0; i<6; i++)
		{			
			mac_addr[i] = rtw_read8(Adapter, REG_MACID+i);		
		}
		
		DBG_8192C("MAC Address from REG_MACID = "MAC_FMT"\n", MAC_ARG(mac_addr));
	}

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
#define PlatformEFIOWrite1Byte	rtw_write8
#define PlatformEFIOWrite2Byte	rtw_write16
#define PlatformEFIORead1Byte	rtw_read8
#define delay_ms		rtw_mdelay_os
#define u1Byte u8

VOID
phy_SsPwrSwitch92CU(
	IN	PADAPTER			Adapter,
	IN	rt_rf_power_state	eRFPowerState,
	IN	int bRegSSPwrLvl
	)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	u1Byte				value8;
	
	switch( eRFPowerState )
	{
		case rf_on:								
			if (bRegSSPwrLvl == 1)
			{
				// 1. Enable MAC Clock. Can not be enabled now.
				//WriteXBYTE(REG_SYS_CLKR+1, ReadXBYTE(REG_SYS_CLKR+1) | BIT(3));
				
				// 2. Force PWM, Enable SPS18_LDO_Marco_Block
				PlatformEFIOWrite1Byte(Adapter, REG_SPS0_CTRL, 
				PlatformEFIORead1Byte(Adapter, REG_SPS0_CTRL) | (BIT0|BIT3));

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
				PHY_SetRFReg(Adapter,RF90_PATH_A, 0, bRFRegOffsetMask,0x32D95);
				if (pHalData->rf_type ==  RF_2T2R)
				{
					PHY_SetRFReg(Adapter,RF90_PATH_B, 0, bRFRegOffsetMask,0x32D95);
				}
			}	
			else		// Level 2 or others.
			{
				//h.	AFE_PLL_CTRL 0x28[7:0] = 0x80			//disable AFE PLL
				PlatformEFIOWrite1Byte(Adapter, REG_AFE_PLL_CTRL, 0x81);
				
				// i.	AFE_XTAL_CTRL 0x24[15:0] = 0x880F		//gated AFE DIG_CLOCK
				PlatformEFIOWrite2Byte(Adapter, REG_AFE_XTAL_CTRL, 0x800F);
				delay_ms(1);
				
				// 1. Enable MAC Clock. Can not be enabled now.
				//WriteXBYTE(REG_SYS_CLKR+1, ReadXBYTE(REG_SYS_CLKR+1) | BIT(3));
				
				// 2. Force PWM, Enable SPS18_LDO_Marco_Block
				PlatformEFIOWrite1Byte(Adapter, REG_SPS0_CTRL, 
				PlatformEFIORead1Byte(Adapter, REG_SPS0_CTRL) | (BIT0|BIT3));

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
				PHY_SetRFReg(Adapter,RF90_PATH_A, 0, bRFRegOffsetMask,0x32D95);
				if (pHalData->rf_type ==  RF_2T2R)
				{
					PHY_SetRFReg(Adapter,RF90_PATH_B, 0, bRFRegOffsetMask,0x32D95);
				}

				// 5. gated MAC Clock
				//WriteXBYTE(REG_SYS_CLKR+1, ReadXBYTE(REG_SYS_CLKR+1) & ~(BIT(3)));
				//PlatformEFIOWrite1Byte(Adapter, REG_SYS_CLKR+1, PlatformEFIORead1Byte(Adapter, REG_SYS_CLKR+1)|(BIT3));

				{
					//u1Byte 			eRFPath = RF90_PATH_A,value8 = 0, retry = 0;
					u1Byte		bytetmp;
					//PHY_SetRFReg(Adapter, (RF90_RADIO_PATH_E)eRFPath, 0x0, bMaskByte0, 0x0);
					// 2010/08/12 MH Add for B path under SS test. 
					//if (pHalData->RF_Type ==  RF_2T2R)
						//PHY_SetRFReg(Adapter, RF90_PATH_B, 0x0, bMaskByte0, 0x0);

					bytetmp = PlatformEFIORead1Byte(Adapter, REG_APSD_CTRL);
					PlatformEFIOWrite1Byte(Adapter, REG_APSD_CTRL, bytetmp & ~BIT6);
				
					delay_ms(10);

					// Set BB reset at first
					PlatformEFIOWrite1Byte(Adapter, REG_SYS_FUNC_EN, 0x17 );//0x16		

					// Enable TX
					PlatformEFIOWrite1Byte(Adapter, REG_TXPAUSE, 0x0);
				}
				//Adapter->HalFunc.InitializeAdapterHandler(Adapter, Adapter->MgntInfo.dot11CurrentChannelNumber);
				//CardSelectiveSuspendLeave(Adapter);
			}

			break;
	   
		case rf_sleep:
		case rf_off:
				value8 = PlatformEFIORead1Byte(Adapter, REG_SPS0_CTRL) ;
				if (IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID))
					value8 &= ~(BIT0);
				else
					value8 &= ~(BIT0|BIT3);
				if (bRegSSPwrLvl == 1)
				{
					RT_TRACE(COMP_POWER, DBG_LOUD, ("SS LVL1\n"));
					// Disable RF and BB only for SelectSuspend.
					
					// 1. Set BB/RF to shutdown.
					//	(1) Reg878[5:3]= 0 	// RF rx_code for preamble power saving
					//	(2)Reg878[21:19]= 0	//Turn off RF-B
					//	(3) RegC04[7:4]= 0 	// turn off all paths for packet detection
					//	(4) Reg800[1] = 1 		// enable preamble power saving
					Adapter->pwrctrlpriv.PS_BBRegBackup[PSBBREG_RF0] = PHY_QueryBBReg(Adapter, rFPGA0_XAB_RFParameter, bMaskDWord);
					Adapter->pwrctrlpriv.PS_BBRegBackup[PSBBREG_RF1] = PHY_QueryBBReg(Adapter, rOFDM0_TRxPathEnable, bMaskDWord);
					Adapter->pwrctrlpriv.PS_BBRegBackup[PSBBREG_RF2] = PHY_QueryBBReg(Adapter, rFPGA0_RFMOD, bMaskDWord);
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
					Adapter->pwrctrlpriv.PS_BBRegBackup[PSBBREG_AFE0] = PHY_QueryBBReg(Adapter, rRx_Wait_CCA, bMaskDWord);	
					if (pHalData->rf_type ==  RF_2T2R)
						PHY_SetBBReg(Adapter, rRx_Wait_CCA, bMaskDWord ,0x00DB25A0);
					else if (pHalData->rf_type ==  RF_1T1R)
						PHY_SetBBReg(Adapter, rRx_Wait_CCA, bMaskDWord ,0x001B25A0);
					
					// 3. issue 3-wire command that RF set to power down.
					PHY_SetRFReg(Adapter,RF90_PATH_A, 0, bRFRegOffsetMask,0);
					if (pHalData->rf_type ==  RF_2T2R)
					{
						PHY_SetRFReg(Adapter,RF90_PATH_B, 0, bRFRegOffsetMask,0);
					}

					// 4. Force PFM , disable SPS18_LDO_Marco_Block					
					PlatformEFIOWrite1Byte(Adapter, REG_SPS0_CTRL, value8);

					// 5. gated MAC Clock
					//WriteXBYTE(REG_SYS_CLKR+1, ReadXBYTE(REG_SYS_CLKR+1) & ~(BIT(3)));
				}
				else	// Level 2 or others.
				{
					RT_TRACE(COMP_POWER, DBG_LOUD, ("SS LVL2\n"));
					{
						u1Byte 			eRFPath = RF90_PATH_A,value8 = 0;
						PlatformEFIOWrite1Byte(Adapter, REG_TXPAUSE, 0xFF);
						PHY_SetRFReg(Adapter, (RF90_RADIO_PATH_E)eRFPath, 0x0, bMaskByte0, 0x0);
						// 2010/08/12 MH Add for B path under SS test. 
						//if (pHalData->RF_Type ==  RF_2T2R)
							//PHY_SetRFReg(Adapter, RF90_PATH_B, 0x0, bMaskByte0, 0x0);

						value8 |= APSDOFF;
						PlatformEFIOWrite1Byte(Adapter, REG_APSD_CTRL, value8);//0x40

						// After switch APSD, we need to delay for stability
						delay_ms(10);

						// Set BB reset at first
						value8 = 0 ; 
						value8 |=( FEN_USBD | FEN_USBA | FEN_BB_GLB_RSTn);
						PlatformEFIOWrite1Byte(Adapter, REG_SYS_FUNC_EN,value8 );//0x16			
					}

					// Disable RF and BB only for SelectSuspend.

					// 1. Set BB/RF to shutdown.
					//	(1) Reg878[5:3]= 0 	// RF rx_code for preamble power saving
					//	(2)Reg878[21:19]= 0	//Turn off RF-B
					//	(3) RegC04[7:4]= 0 	// turn off all paths for packet detection
					//	(4) Reg800[1] = 1 		// enable preamble power saving
					Adapter->pwrctrlpriv.PS_BBRegBackup[PSBBREG_RF0] = PHY_QueryBBReg(Adapter, rFPGA0_XAB_RFParameter, bMaskDWord);
					Adapter->pwrctrlpriv.PS_BBRegBackup[PSBBREG_RF1] = PHY_QueryBBReg(Adapter, rOFDM0_TRxPathEnable, bMaskDWord);
					Adapter->pwrctrlpriv.PS_BBRegBackup[PSBBREG_RF2] = PHY_QueryBBReg(Adapter, rFPGA0_RFMOD, bMaskDWord);
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
					Adapter->pwrctrlpriv.PS_BBRegBackup[PSBBREG_AFE0] = PHY_QueryBBReg(Adapter, rRx_Wait_CCA, bMaskDWord);	
					if (pHalData->rf_type ==  RF_2T2R)
						PHY_SetBBReg(Adapter, rRx_Wait_CCA, bMaskDWord ,0x00DB25A0);
					else if (pHalData->rf_type ==  RF_1T1R)
						PHY_SetBBReg(Adapter, rRx_Wait_CCA, bMaskDWord ,0x001B25A0);
					
					// 3. issue 3-wire command that RF set to power down.
					PHY_SetRFReg(Adapter,RF90_PATH_A, 0, bRFRegOffsetMask,0);
					if (pHalData->rf_type ==  RF_2T2R)
					{
						PHY_SetRFReg(Adapter,RF90_PATH_B, 0, bRFRegOffsetMask,0);
					}

					// 4. Force PFM , disable SPS18_LDO_Marco_Block
					PlatformEFIOWrite1Byte(Adapter, REG_SPS0_CTRL, value8);

					// 2010/10/13 MH/Isaachsu exchange sequence.
					//h.	AFE_PLL_CTRL 0x28[7:0] = 0x80			//disable AFE PLL
					PlatformEFIOWrite1Byte(Adapter, REG_AFE_PLL_CTRL, 0x80);
					delay_ms(1);

					// i.	AFE_XTAL_CTRL 0x24[15:0] = 0x880F		//gated AFE DIG_CLOCK
					PlatformEFIOWrite2Byte(Adapter, REG_AFE_XTAL_CTRL, 0xA80F);

					// 5. gated MAC Clock
					//WriteXBYTE(REG_SYS_CLKR+1, ReadXBYTE(REG_SYS_CLKR+1) & ~(BIT(3)));
					//PlatformEFIOWrite1Byte(Adapter, REG_SYS_CLKR+1, PlatformEFIORead1Byte(Adapter, REG_SYS_CLKR+1)& ~(BIT3))
					
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
	PHY_SetRFReg(Adapter, (RF90_RADIO_PATH_E)eRFPath, 0x0, bMaskByte0, 0x0);

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

static int	
CardDisableHWSM( // HW Auto state machine
	IN	PADAPTER		Adapter,
	IN	BOOLEAN			resetMCU
	)
{
	int		rtStatus = _SUCCESS;
	if(Adapter->bSurpriseRemoved){
		return rtStatus;
	}
#if 1
	//==== RF Off Sequence ====
	_DisableRFAFEAndResetBB(Adapter);

	//  ==== Reset digital sequence   ======
	_ResetDigitalProcedure1(Adapter, _FALSE);
	
	//  ==== Pull GPIO PIN to balance level and LED control ======
	_DisableGPIO(Adapter);

	//  ==== Disable analog sequence ===
	_DisableAnalog(Adapter, _FALSE);

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("======> Card disable finished.\n"));
#else
	_DisableGPIO(Adapter);
	
	//reset FW download register
	_ResetFWDownloadRegister(Adapter);


	//disable RF/ AFE AD/DA
	rtStatus = _DisableRF_AFE(Adapter);
	if(RT_STATUS_SUCCESS != rtStatus){
		RT_TRACE(COMP_INIT, DBG_SERIOUS, ("_DisableRF_AFE failed!\n"));
		goto Exit;
	}
	_ResetBB(Adapter);

	if(resetMCU){
		_ResetMCU(Adapter);
	}

	_AutoPowerDownToHostOff(Adapter);
	//_DisableMAC_AFE_PLL(Adapter);
	
	_SetUsbSuspend(Adapter);
Exit:
#endif
	return rtStatus;
	
}

static int	
CardDisableWithoutHWSM( // without HW Auto state machine
	IN	PADAPTER		Adapter	
	)
{
	int		rtStatus = _SUCCESS;

	if(Adapter->bSurpriseRemoved){
		return rtStatus;
	}
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("======> Card Disable Without HWSM .\n"));
	//==== RF Off Sequence ====
	_DisableRFAFEAndResetBB(Adapter);

	//  ==== Reset digital sequence   ======
	_ResetDigitalProcedure1(Adapter, _TRUE);

	//  ==== Pull GPIO PIN to balance level and LED control ======
	_DisableGPIO(Adapter);

	//  ==== Reset digital sequence   ======
	_ResetDigitalProcedure2(Adapter);

	//  ==== Disable analog sequence ===
	_DisableAnalog(Adapter, _TRUE);
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("<====== Card Disable Without HWSM .\n"));
	return rtStatus;
}

static void rtl8192cu_hw_power_down(_adapter *padapter)
{
	// 2010/-8/09 MH For power down module, we need to enable register block contrl reg at 0x1c.
	// Then enable power down control bit of register 0x04 BIT4 and BIT15 as 1.
		
	// Enable register area 0x0-0xc.
	rtw_write8(padapter,REG_RSV_CTRL, 0x0);			
	rtw_write16(padapter, REG_APS_FSMCO, 0x8812);
}

u32 rtl8192cu_hal_deinit(PADAPTER Adapter)
 {

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
   	DBG_8192C("==> %s \n",__FUNCTION__);
	// 2011/02/18 To Fix RU LNA  power leakage problem. We need to execute below below in
	// Adapter init and halt sequence. Accordingto EEchou's opinion, we can enable the ability for all
	// IC. Accord to johnny's opinion, only RU need the support.
	if (IS_HARDWARE_TYPE_8192C(Adapter)  && (pHalData->BoardType == BOARD_USB_High_PA))
		rtw_write32(Adapter, rFPGA0_XCD_RFParameter, rtw_read32(Adapter, rFPGA0_XCD_RFParameter)|BIT1);

 #ifdef SUPPORT_HW_RFOFF_DETECTED
 	DBG_8192C("bkeepfwalive(%x)\n",Adapter->pwrctrlpriv.bkeepfwalive);
 	if(Adapter->pwrctrlpriv.bkeepfwalive)
 	{
		_ps_close_RF(Adapter);		
		if((Adapter->pwrctrlpriv.bHWPwrPindetect) && (Adapter->pwrctrlpriv.bHWPowerdown))		
			rtl8192cu_hw_power_down(Adapter);
 	}
	else
#endif
	{
		if( Adapter->bCardDisableWOHSM == _FALSE)
		{
			DBG_8192C("card disble HWSM...........\n");
			CardDisableHWSM(Adapter, _FALSE);
		}
		else
		{
			DBG_8192C("card disble without HWSM...........\n");
			CardDisableWithoutHWSM(Adapter); // without HW Auto state machine		

			if((Adapter->pwrctrlpriv.bHWPwrPindetect ) && (Adapter->pwrctrlpriv.bHWPowerdown))		
				rtl8192cu_hw_power_down(Adapter);
		}
	}
	
	return _SUCCESS;
 }


unsigned int rtl8192cu_inirp_init(PADAPTER Adapter)
{	
	u8 i;	
	struct recv_buf *precvbuf;
	uint	status;
	struct dvobj_priv *pdev=&Adapter->dvobjpriv;
	struct intf_hdl * pintfhdl=&Adapter->iopriv.intf;
	struct recv_priv *precvpriv = &(Adapter->recvpriv);
	u32 (*_read_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
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

unsigned int rtl8192cu_inirp_deinit(PADAPTER Adapter)
{	
	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("\n ===> usb_rx_deinit \n"));
	
	rtw_read_port_cancel(Adapter);

	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("\n <=== usb_rx_deinit \n"));

	return _SUCCESS;
}

//-------------------------------------------------------------------------
//
//	Channel Plan
//
//-------------------------------------------------------------------------

static VOID
ReadChannelPlan(
	IN	PADAPTER 		Adapter,
	IN	u8*			PROMContent,
	IN	BOOLEAN			AutoLoadFail
	)
{

#define EEPROM_TEST_CHANNEL_PLAN	 (0x7D)
#define EEPROM_NORMAL_CHANNEL_PLAN (0x75)

	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	u8			channelPlan;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	if(AutoLoadFail){
		channelPlan = CHPL_FCC;
	}
	else{
		if(IS_NORMAL_CHIP(pHalData->VersionID))
		 	channelPlan = PROMContent[EEPROM_NORMAL_CHANNEL_PLAN];
		else
			channelPlan = PROMContent[EEPROM_TEST_CHANNEL_PLAN];
	}

	if((pregistrypriv->channel_plan>= RT_CHANNEL_DOMAIN_MAX) || (channelPlan & EEPROM_CHANNEL_PLAN_BY_HW_MASK))
	{
		pmlmepriv->ChannelPlan = _HalMapChannelPlan8192C(Adapter, (channelPlan & (~(EEPROM_CHANNEL_PLAN_BY_HW_MASK))));
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
	
			pDot11dInfo->bEnabled = _TRUE;
		}
		//RT_TRACE(COMP_INIT, DBG_LOUD, ("Enable dot11d when RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN!\n"));
		break;
	}
#endif

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("RegChannelPlan(%d) EEPROMChannelPlan(%ld)", pMgntInfo->RegChannelPlan, (u4Byte)channelPlan));
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("ChannelPlan = %d\n" , pMgntInfo->ChannelPlan));

	MSG_8192C("RT_ChannelPlan: 0x%02x\n", pmlmepriv->ChannelPlan);

}


//-------------------------------------------------------------------------
//
//	EEPROM Power index mapping
//
//-------------------------------------------------------------------------

 static VOID
_ReadPowerValueFromPROM(
	IN	PTxPowerInfo	pwrInfo,
	IN	u8*			PROMContent,
	IN	BOOLEAN			AutoLoadFail
	)
{
	u32 rfPath, eeAddr, group;

	_rtw_memset(pwrInfo, 0, sizeof(TxPowerInfo));

	if(AutoLoadFail){		
		for(group = 0 ; group < CHANNEL_GROUP_MAX ; group++){
			for(rfPath = 0 ; rfPath < RF90_PATH_MAX ; rfPath++){
				pwrInfo->CCKIndex[rfPath][group]		= EEPROM_Default_TxPowerLevel;	
				pwrInfo->HT40_1SIndex[rfPath][group]	= EEPROM_Default_TxPowerLevel;
				pwrInfo->HT40_2SIndexDiff[rfPath][group]= EEPROM_Default_HT40_2SDiff;
				pwrInfo->HT20IndexDiff[rfPath][group]	= EEPROM_Default_HT20_Diff;
				pwrInfo->OFDMIndexDiff[rfPath][group]	= EEPROM_Default_LegacyHTTxPowerDiff;
				pwrInfo->HT40MaxOffset[rfPath][group]	= EEPROM_Default_HT40_PwrMaxOffset;		
				pwrInfo->HT20MaxOffset[rfPath][group]	= EEPROM_Default_HT20_PwrMaxOffset;
			}
		}

		pwrInfo->TSSI_A = EEPROM_Default_TSSI;
		pwrInfo->TSSI_B = EEPROM_Default_TSSI;
		
		return;
	}
	
	for(rfPath = 0 ; rfPath < RF90_PATH_MAX ; rfPath++){
		for(group = 0 ; group < CHANNEL_GROUP_MAX ; group++){
			eeAddr = EEPROM_CCK_TX_PWR_INX + (rfPath * 3) + group;
			pwrInfo->CCKIndex[rfPath][group] = PROMContent[eeAddr];

			eeAddr = EEPROM_HT40_1S_TX_PWR_INX + (rfPath * 3) + group;
			pwrInfo->HT40_1SIndex[rfPath][group] = PROMContent[eeAddr];
		}
	}

	for(group = 0 ; group < CHANNEL_GROUP_MAX ; group++){
		for(rfPath = 0 ; rfPath < RF90_PATH_MAX ; rfPath++){
			pwrInfo->HT40_2SIndexDiff[rfPath][group] = 
			(PROMContent[EEPROM_HT40_2S_TX_PWR_INX_DIFF + group] >> (rfPath * 4)) & 0xF;

#if 1
			pwrInfo->HT20IndexDiff[rfPath][group] =
			(PROMContent[EEPROM_HT20_TX_PWR_INX_DIFF + group] >> (rfPath * 4)) & 0xF;
			if(pwrInfo->HT20IndexDiff[rfPath][group] & BIT3)	//4bit sign number to 8 bit sign number
				pwrInfo->HT20IndexDiff[rfPath][group] |= 0xF0;
#else
			pwrInfo->HT20IndexDiff[rfPath][group] =
			(PROMContent[EEPROM_HT20_TX_PWR_INX_DIFF + group] >> (rfPath * 4)) & 0xF;
#endif

			pwrInfo->OFDMIndexDiff[rfPath][group] =
			(PROMContent[EEPROM_OFDM_TX_PWR_INX_DIFF+ group] >> (rfPath * 4)) & 0xF;

			pwrInfo->HT40MaxOffset[rfPath][group] =
			(PROMContent[EEPROM_HT40_MAX_PWR_OFFSET+ group] >> (rfPath * 4)) & 0xF;

			pwrInfo->HT20MaxOffset[rfPath][group] =
			(PROMContent[EEPROM_HT20_MAX_PWR_OFFSET+ group] >> (rfPath * 4)) & 0xF;
		}
	}

	pwrInfo->TSSI_A = PROMContent[EEPROM_TSSI_A];
	pwrInfo->TSSI_B = PROMContent[EEPROM_TSSI_B];

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


static VOID
ReadTxPowerInfo(
	IN	PADAPTER 		Adapter,
	IN	u8*			PROMContent,
	IN	BOOLEAN			AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	TxPowerInfo		pwrInfo;
	u32			rfPath, ch, group;
	u8			pwr, diff;

	_ReadPowerValueFromPROM(&pwrInfo, PROMContent, AutoLoadFail);

	if(!AutoLoadFail)
		pHalData->bTXPowerDataReadFromEEPORM = _TRUE;

	for(rfPath = 0 ; rfPath < RF90_PATH_MAX ; rfPath++){
		for(ch = 0 ; ch < CHANNEL_MAX_NUMBER ; ch++){
			group = _GetChannelGroup(ch);

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

	for(rfPath = 0 ; rfPath < RF90_PATH_MAX ; rfPath++){
		for(ch = 0 ; ch < CHANNEL_MAX_NUMBER ; ch++){
			RTPRINT(FINIT, INIT_TxPower, 
				("RF(%d)-Ch(%d) [CCK / HT40_1S / HT40_2S] = [0x%x / 0x%x / 0x%x]\n", 
				rfPath, ch, pHalData->TxPwrLevelCck[rfPath][ch], 
				pHalData->TxPwrLevelHT40_1S[rfPath][ch], 
				pHalData->TxPwrLevelHT40_2S[rfPath][ch]));

		}
	}

	for(ch = 0 ; ch < CHANNEL_MAX_NUMBER ; ch++){
		RTPRINT(FINIT, INIT_TxPower, ("RF-A Ht20 to HT40 Diff[%d] = 0x%x\n", ch, pHalData->TxPwrHt20Diff[RF90_PATH_A][ch]));
	}

	for(ch = 0 ; ch < CHANNEL_MAX_NUMBER ; ch++){
		RTPRINT(FINIT, INIT_TxPower, ("RF-A Legacy to Ht40 Diff[%d] = 0x%x\n", ch, pHalData->TxPwrLegacyHtDiff[RF90_PATH_A][ch]));
	}
	
	for(ch = 0 ; ch < CHANNEL_MAX_NUMBER ; ch++){
		RTPRINT(FINIT, INIT_TxPower, ("RF-B Ht20 to HT40 Diff[%d] = 0x%x\n", ch, pHalData->TxPwrHt20Diff[RF90_PATH_B][ch]));
	}
	
	for(ch = 0 ; ch < CHANNEL_MAX_NUMBER ; ch++){
		RTPRINT(FINIT, INIT_TxPower, ("RF-B Legacy to HT40 Diff[%d] = 0x%x\n", ch, pHalData->TxPwrLegacyHtDiff[RF90_PATH_B][ch]));
	}
	
#endif
	// 2010/10/19 MH Add Regulator recognize for CU.
	if(!AutoLoadFail)
	{
		pHalData->EEPROMRegulatory = (PROMContent[RF_OPTION1]&0x7);	//bit0~2
	}
	else
	{
		pHalData->EEPROMRegulatory = 0;
	}
	DBG_8192C("EEPROMRegulatory = 0x%x\n", pHalData->EEPROMRegulatory);

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
	BOOLEAN		isNormal = IS_NORMAL_CHIP(pHalData->VersionID);
	u32			value32;
	u8			boardType = BOARD_USB_DONGLE;
#if 0
	if(isNormal)
	{
		value32 = rtw_read32(Adapter, REG_HPON_FSM);

		DBG_8192C("first value 0x%x BoardType after 0x%x \n", CHIP_BONDING_IDENTIFIER(value32), pHalData->BoardType);
		
		if(!IS_92C_SERIAL(pHalData->VersionID))
		{
			if(CHIP_BONDING_IDENTIFIER(value32) == CHIP_BONDING_88C_USB_MCARD)
			{
				pHalData->BoardType = BOARD_MINICARD;
				DBG_8192C("value 0x%x BoardType after 0x%x \n", CHIP_BONDING_IDENTIFIER(value32), pHalData->BoardType);
			}
			else if(CHIP_BONDING_IDENTIFIER(value32) == CHIP_BONDING_88C_USB_HP)
			{
				pHalData->BoardType = BOARD_USB_High_PA;
				DBG_8192C("value 0x%x BoardType after 0x%x \n", CHIP_BONDING_IDENTIFIER(value32), pHalData->BoardType);
			}
		}
	}
#endif

	if(AutoloadFail){
		if(IS_8723_SERIES(pHalData->VersionID))
			pHalData->rf_type = RF_1T1R;
		else
		        pHalData->rf_type = RF_2T2R;

		pHalData->BluetoothCoexist = _FALSE;
		pHalData->BoardType = boardType;
		return;
	}

	if(isNormal) 
	{
		boardType = PROMContent[EEPROM_NORMAL_BoardType];
		boardType &= BOARD_TYPE_NORMAL_MASK;//bit[7:5]
		boardType >>= 5;
	}
	else
	{
		boardType = PROMContent[EEPROM_RF_OPT4];
		boardType &= BOARD_TYPE_TEST_MASK;		
	}

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

static void
_ReadPROMVersion(
	IN	PADAPTER	Adapter,	
	IN	u8* 	PROMContent,
	IN	BOOLEAN 	AutoloadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	if(AutoloadFail){
		pHalData->EEPROMVersion = EEPROM_Default_Version;		
	}
	else{
		pHalData->EEPROMVersion = *(u8 *)&PROMContent[EEPROM_VERSION];
	}
}

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

static VOID
hal_InitPGData(
	IN	PADAPTER	pAdapter,
	IN	OUT	u8		*PROMContent
	)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(pAdapter);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u32	i;
	u16	value16;

	if(_FALSE == pEEPROM->bautoload_fail_flag)
	{ // autoload OK.
		if (_TRUE == pEEPROM->EepromOrEfuse)
		{
			// Read all Content from EEPROM or EFUSE.
			for(i = 0; i < HWSET_MAX_SIZE; i += 2)
			{
				//value16 = EF2Byte(ReadEEprom(pAdapter, (u2Byte) (i>>1)));
				//*((u16 *)(&PROMContent[i])) = value16; 				
			}
		}
		else
		{
			// Read EFUSE real map to shadow.
			EFUSE_ShadowMapUpdate(pAdapter, EFUSE_WIFI, _FALSE);
			_rtw_memcpy((void*)PROMContent, (void*)pEEPROM->efuse_eeprom_data, HWSET_MAX_SIZE);
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
}
// Read HW power down mode selection 
static void _ReadPSSetting(IN PADAPTER Adapter,IN u8*PROMContent,IN u8	AutoloadFail)
{
	if(AutoloadFail){
		Adapter->pwrctrlpriv.bHWPowerdown = _FALSE;
		Adapter->pwrctrlpriv.bSupportRemoteWakeup = _FALSE;
	}
	else	{
		//if(SUPPORT_HW_RADIO_DETECT(Adapter))
			Adapter->pwrctrlpriv.bHWPwrPindetect = Adapter->registrypriv.hwpwrp_detect;
		//else
			//Adapter->pwrctrlpriv.bHWPwrPindetect = _FALSE;//dongle not support new
			
			
		//hw power down mode selection , 0:rf-off / 1:power down

		if(Adapter->registrypriv.hwpdn_mode==2)
			Adapter->pwrctrlpriv.bHWPowerdown = (PROMContent[EEPROM_RF_OPT3] & BIT4);
		else
			Adapter->pwrctrlpriv.bHWPowerdown = Adapter->registrypriv.hwpdn_mode;
				
		// decide hw if support remote wakeup function
		// if hw supported, 8051 (SIE) will generate WeakUP signal( D+/D- toggle) when autoresume
		Adapter->pwrctrlpriv.bSupportRemoteWakeup = (PROMContent[EEPROM_TEST_USB_OPT] & BIT1)?_TRUE :_FALSE;

		//if(SUPPORT_HW_RADIO_DETECT(Adapter))	
			//Adapter->registrypriv.usbss_enable = Adapter->pwrctrlpriv.bSupportRemoteWakeup ;
		
		DBG_8192C("%s...bHWPwrPindetect(%x)-bHWPowerdown(%x) ,bSupportRemoteWakeup(%x)\n",__FUNCTION__,
		Adapter->pwrctrlpriv.bHWPwrPindetect,Adapter->pwrctrlpriv.bHWPowerdown ,Adapter->pwrctrlpriv.bSupportRemoteWakeup);

		DBG_8192C("### PS params=>  power_mgnt(%x),usbss_enable(%x) ###\n",Adapter->registrypriv.power_mgnt,Adapter->registrypriv.usbss_enable);
		
	}
	
}

static VOID
readAdapterInfo_8192CU(
	IN	PADAPTER	Adapter
	)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(Adapter);
	u8	PROMContent[HWSET_MAX_SIZE]={0};

	hal_InitPGData(Adapter, PROMContent);
	rtl8192c_EfuseParseIDCode(Adapter, PROMContent);
	
	_ReadPROMVersion(Adapter, PROMContent, pEEPROM->bautoload_fail_flag);
	_ReadIDs(Adapter, PROMContent, pEEPROM->bautoload_fail_flag);
	_ReadMACAddress(Adapter, PROMContent, pEEPROM->bautoload_fail_flag);	
	ReadTxPowerInfo(Adapter, PROMContent, pEEPROM->bautoload_fail_flag);
	_ReadBoardType(Adapter, PROMContent, pEEPROM->bautoload_fail_flag);

#ifdef CONFIG_BT_COEXIST
	//
	// Read Bluetooth co-exist and initialize
	//
	rtl8192c_ReadBluetoothCoexistInfo(Adapter, PROMContent, pEEPROM->bautoload_fail_flag);
#endif
	
	ReadChannelPlan(Adapter, PROMContent, pEEPROM->bautoload_fail_flag);
	_ReadThermalMeter(Adapter, PROMContent, pEEPROM->bautoload_fail_flag);
	_ReadLEDSetting(Adapter, PROMContent, pEEPROM->bautoload_fail_flag);	
	_ReadRFSetting(Adapter, PROMContent, pEEPROM->bautoload_fail_flag);
	_ReadPSSetting(Adapter, PROMContent, pEEPROM->bautoload_fail_flag);
	readAntennaDiversity(Adapter, PROMContent, pEEPROM->bautoload_fail_flag);

	//hal_CustomizedBehavior_8723U(Adapter);

	Adapter->bDongle = (PROMContent[EEPROM_EASY_REPLACEMENT] == 1)? 0: 1;
	DBG_8192C("%s(): REPLACEMENT = %x\n",__FUNCTION__,Adapter->bDongle);
}

static void _ReadPROMContent(
	IN PADAPTER 		Adapter
	)
{	
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
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

	//if(IS_HARDWARE_TYPE_8723(Adapter))
	//	readAdapterInfo_8723U(Adapter);
	//else
		readAdapterInfo_8192CU(Adapter);
}


static VOID
_InitOtherVariable(
	IN PADAPTER 		Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);	


	//if(Adapter->bInHctTest){
	//	pMgntInfo->PowerSaveControl.bInactivePs = FALSE;
	//	pMgntInfo->PowerSaveControl.bIPSModeBackup = FALSE;
	//	pMgntInfo->PowerSaveControl.bLeisurePs = FALSE;
	//	pMgntInfo->keepAliveLevel = 0;
	//}

	// 2009/06/10 MH For 92S 1*1=1R/ 1*2&2*2 use 2R. We default set 1*1 use radio A
	// So if you want to use radio B. Please modify RF path enable bit for correct signal
	// strength calculate.
	if (pHalData->rf_type == RF_1T1R){
		pHalData->bRFPathRxEnable[0] = _TRUE;
	}
	else{
		pHalData->bRFPathRxEnable[0] = pHalData->bRFPathRxEnable[1] = _TRUE;
	}

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
	if ((Adapter->chip_type == RTL8188C_8192C) && 
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
	
}
static int _ReadAdapterInfo8192CU(PADAPTER	Adapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32 start=rtw_get_current_time();
	
	MSG_8192C("====> ReadAdapterInfo8192C\n");

	//Efuse_InitSomeVar(Adapter);

	//if(IS_HARDWARE_TYPE_8723(Adapter))
	//	_EfuseCellSel(Adapter);

	_ReadRFType(Adapter);//rf_chip -> _InitRFType()
	_ReadPROMContent(Adapter);

	// 2010/10/25 MH THe function must be called after borad_type & IC-Version recognize.
	_ReadSilmComboMode(Adapter);

	_InitOtherVariable(Adapter);

	//MSG_8192C("%s()(done), rf_chip=0x%x, rf_type=0x%x\n",  __FUNCTION__, pHalData->rf_chip, pHalData->rf_type);

	MSG_8192C("<==== ReadAdapterInfo8192C in %d ms\n", rtw_get_passing_time_ms(start));

	return _SUCCESS;
}


static void ReadAdapterInfo8192CU(PADAPTER Adapter)
{
	// Read EEPROM size before call any EEPROM function
	//Adapter->EepromAddressSize=Adapter->HalFunc.GetEEPROMSizeHandler(Adapter);
	Adapter->EepromAddressSize = GetEEPROMSize8192C(Adapter);
	
	_ReadAdapterInfo8192CU(Adapter);
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

static void ResumeTxBeacon(_adapter *padapter)
{
	HAL_DATA_TYPE*	pHalData = GET_HAL_DATA(padapter);	

	// 2010.03.01. Marked by tynli. No need to call workitem beacause we record the value
	// which should be read from register to a global variable.

	 if(IS_NORMAL_CHIP(pHalData->VersionID))
	 {
		rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) | BIT6);
		pHalData->RegFwHwTxQCtrl |= BIT6;
		rtw_write8(padapter, REG_TBTT_PROHIBIT+1, 0xff);
		pHalData->RegReg542 |= BIT0;
		rtw_write8(padapter, REG_TBTT_PROHIBIT+2, pHalData->RegReg542);
	 }
	 else
	 {
		pHalData->RegTxPause = rtw_read8(padapter, REG_TXPAUSE);
		rtw_write8(padapter, REG_TXPAUSE, pHalData->RegTxPause & (~BIT6));
	 }
	 
}

static void StopTxBeacon(_adapter *padapter)
{
	HAL_DATA_TYPE*	pHalData = GET_HAL_DATA(padapter);

	// 2010.03.01. Marked by tynli. No need to call workitem beacause we record the value
	// which should be read from register to a global variable.

 	if(IS_NORMAL_CHIP(pHalData->VersionID))
	 {
		rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) & (~BIT6));
		pHalData->RegFwHwTxQCtrl &= (~BIT6);
		rtw_write8(padapter, REG_TBTT_PROHIBIT+1, 0x64);
		pHalData->RegReg542 &= ~(BIT0);
		rtw_write8(padapter, REG_TBTT_PROHIBIT+2, pHalData->RegReg542);
	 }
	 else
	 {
		pHalData->RegTxPause = rtw_read8(padapter, REG_TXPAUSE);
		rtw_write8(padapter, REG_TXPAUSE, pHalData->RegTxPause | BIT6);
	 }

	 //todo: CheckFwRsvdPageContent(Adapter);  // 2010.06.23. Added by tynli.

}


void SetHwReg8192CU(PADAPTER Adapter, u8 variable, u8* val)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

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
			{
				u8	val8;
				u8	mode = *((u8 *)val);

				if((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_))
				{
					StopTxBeacon(Adapter);
					rtw_write8(Adapter,REG_BCN_CTRL, 0x18);
				}
				else if((mode == _HW_STATE_ADHOC_) /*|| (mode == _HW_STATE_AP_)*/)
				{					
					ResumeTxBeacon(Adapter);
					rtw_write8(Adapter,REG_BCN_CTRL, 0x1a);					
				}
				else if(mode == _HW_STATE_AP_)
				{
					ResumeTxBeacon(Adapter);
					
					rtw_write8(Adapter, REG_BCN_CTRL, 0x12);

					
					//Set RCR
					//rtw_write32(padapter, REG_RCR, 0x70002a8e);//CBSSID_DATA must set to 0
					rtw_write32(Adapter, REG_RCR, 0x7000228e);//CBSSID_DATA must set to 0
					//enable to rx data frame				
					rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);
					//enable to rx ps-poll
					rtw_write16(Adapter, REG_RXFLTMAP1, 0x0400);

					//Beacon Control related register for first time 
					rtw_write8(Adapter, REG_BCNDMATIM, 0x02); // 2ms		
					rtw_write8(Adapter, REG_DRVERLYINT, 0x05);// 5ms
					//rtw_write8(Adapter, REG_BCN_MAX_ERR, 0xFF);
					rtw_write8(Adapter, REG_ATIMWND, 0x0a); // 10ms
					rtw_write16(Adapter, REG_BCNTCFG, 0x00);
					rtw_write16(Adapter, REG_TBTT_PROHIBIT, 0x6404);		
	
					//reset TSF		
					rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(0));

					//enable TSF Function for if1
					rtw_write8(Adapter, REG_BCN_CTRL, (EN_BCN_FUNCTION | EN_TXBCN_RPT));
			
					//enable update TSF for if1
					if(IS_NORMAL_CHIP(pHalData->VersionID))
					{			
						rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(4)));			
					}
					else
					{
						rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~(BIT(4)|BIT(5))));
					}
					
				}

				val8 = rtw_read8(Adapter, MSR)&0x0c;
				val8 |= mode;
				rtw_write8(Adapter, MSR, val8);
			}
			break;
		case HW_VAR_BSSID:
			{
				u8	idx = 0;
				for(idx = 0 ; idx < 6; idx++)
				{
					rtw_write8(Adapter, (REG_BSSID+idx), val[idx]);
				}
			}
			break;
		case HW_VAR_BASIC_RATE:
			{
				u16			BrateCfg = 0;
				u8			RateIndex = 0;
				
				// 2007.01.16, by Emily
				// Select RRSR (in Legacy-OFDM and CCK)
				// For 8190, we select only 24M, 12M, 6M, 11M, 5.5M, 2M, and 1M from the Basic rate.
				// We do not use other rates.
				rtl8192c_HalSetBrateCfg( Adapter, val, &BrateCfg );

				//2011.03.30 add by Luke Lee
				//CCK 2M ACK should be disabled for some BCM and Atheros AP IOT
				//because CCK 2M has poor TXEVM
				//CCK 5.5M & 11M ACK should be enabled for better performance

				pHalData->BasicRateSet = BrateCfg = (BrateCfg |0xd) & 0x15d;		

				BrateCfg |= 0x01; // default enable 1M ACK rate

				DBG_8192C("HW_VAR_BASIC_RATE: BrateCfg(%#x)\n", BrateCfg);
				
				// Set RRSR rate table.
				rtw_write8(Adapter, REG_RRSR, BrateCfg&0xff);
				rtw_write8(Adapter, REG_RRSR+1, (BrateCfg>>8)&0xff);

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
			if(*((u8 *)val))
			{
				rtw_write8(Adapter, REG_BCN_CTRL, (EN_BCN_FUNCTION | EN_TXBCN_RPT));
			}
			else
			{
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~(EN_BCN_FUNCTION | EN_TXBCN_RPT)));
			}
			break;
		case HW_VAR_CORRECT_TSF:
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
			break;
		case HW_VAR_CHECK_BSSID:
			if(*((u8 *)val))
			{
				if(IS_NORMAL_CHIP(pHalData->VersionID))
				{
					rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);
				}
				else
				{
					rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA);
				}
			}
			else
			{
				u32	val32;

				val32 = rtw_read32(Adapter, REG_RCR);

				if(IS_NORMAL_CHIP(pHalData->VersionID))
				{
					val32 &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN);
				}
				else
				{
					val32 &= 0xfffff7bf;
				}

				rtw_write32(Adapter, REG_RCR, val32);
			}
			break;
		case HW_VAR_MLME_DISCONNECT:
			{
				//Set RCR to not to receive data frame when NO LINK state
				//rtw_write32(Adapter, REG_RCR, rtw_read32(padapter, REG_RCR) & ~RCR_ADF);
				//reject all data frames
				rtw_write16(Adapter, REG_RXFLTMAP2,0x00);

				//reset TSF
				rtw_write8(Adapter, REG_DUAL_TSF_RST, (BIT(0)|BIT(1)));

				//disable update TSF
				if(IS_NORMAL_CHIP(pHalData->VersionID))
				{
					rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(4));	
				}
				else
				{
					rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(4)|BIT(5));
				}
			}
			break;
		case HW_VAR_MLME_SITESURVEY:
			if(*((u8 *)val))//under sitesurvey
			{
				if(IS_NORMAL_CHIP(pHalData->VersionID))
				{
					//config RCR to receive different BSSID & not to receive data frame
					//pHalData->ReceiveConfig &= (~(RCR_CBSSID_DATA | RCR_CBSSID_BCN));			
					u32 v = rtw_read32(Adapter, REG_RCR);
					v &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN );//| RCR_ADF
					rtw_write32(Adapter, REG_RCR, v);
					//reject all data frame
					rtw_write16(Adapter, REG_RXFLTMAP2,0x00);

					//disable update TSF
					rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(4));
				}	
				else
				{
					//config RCR to receive different BSSID & not to receive data frame			
					rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR) & 0xfffff7bf);


					//disable update TSF
					rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(4)|BIT(5));
				}
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
					if(IS_NORMAL_CHIP(pHalData->VersionID))
					{
						rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(4)));
					}
					else
					{
						rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~(BIT(4)|BIT(5))));
					}
				}
				else if((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
				{
					rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_ADF);
					
					//enable update TSF
					if(IS_NORMAL_CHIP(pHalData->VersionID))			
						rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(4)));			
					else			
						rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~(BIT(4)|BIT(5))));	
				}


				if(IS_NORMAL_CHIP(pHalData->VersionID))
				{
					if((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
						rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_BCN);
					else			
						rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);
				}
				else
				{
					rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA);
				}
			}
			break;
		case HW_VAR_MLME_JOIN:
			{
				u8	RetryLimit = 0x30;
				u8	type = *((u8 *)val);
				struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;

				if(type == 0) // prepare to join
				{
					//enable to rx data frame.Accept all data frame
					//rtw_write32(padapter, REG_RCR, rtw_read32(padapter, REG_RCR)|RCR_ADF);
					rtw_write16(Adapter, REG_RXFLTMAP2,0xFFFF);

					if(IS_NORMAL_CHIP(pHalData->VersionID))
					{
						rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);
					}
					else
					{
						rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA);
					}

					if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
					{
						RetryLimit = (pHalData->CustomerID == RT_CID_CCX) ? 7 : 48;
					}
					else // Ad-hoc Mode
					{
						RetryLimit = 0x7;
					}
				}
				else if(type == 1) //joinbss_event call back when join res < 0
				{
					//if(IS_NORMAL_CHIP(pHalData->VersionID))
					//{
						//config RCR to receive different BSSID & not to receive data frame during linking				
					//	u32 v = rtw_read32(Adapter, REG_RCR);
					//	v &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN );//| RCR_ADF
					//	rtw_write32(Adapter, REG_RCR, v);
					//}	
					//else
					//{
						//config RCR to receive different BSSID & not to receive data frame during linking	
					//	rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR) & 0xfffff7bf);
					//}

					rtw_write16(Adapter, REG_RXFLTMAP2,0x00);
				}
				else if(type == 2) //sta add event call back
				{
					if(IS_NORMAL_CHIP(pHalData->VersionID))
					{
						//enable update TSF
						rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(4)));
					}
					else
					{
						//enable update TSF
						rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~(BIT(4)|BIT(5))));
					}

					if(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE))
					{
						//fixed beacon issue for 8191su...........
						rtw_write8(Adapter,0x542 ,0x02);
						RetryLimit = 0x7;
					}
				}

				rtw_write16(Adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);
			}
			break;
		case HW_VAR_BEACON_INTERVAL:
			rtw_write16(Adapter, REG_BCN_INTERVAL, *((u16 *)val));
			break;
		case HW_VAR_SLOT_TIME:
			{
				u8	u1bAIFS, aSifsTime;
				struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
				struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
				
				rtw_write8(Adapter, REG_SLOT, val[0]);

				if(pmlmeinfo->WMM_enable == 0)
				{
					if( pmlmeext->cur_wireless_mode == WIRELESS_11B)
						aSifsTime = 10;
					else
						aSifsTime = 16;
					
					u1bAIFS = aSifsTime + (2 * pmlmeinfo->slotTime);
					
					// <Roger_EXP> Temporary removed, 2008.06.20.
					rtw_write8(Adapter, REG_EDCA_VO_PARAM, u1bAIFS);
					rtw_write8(Adapter, REG_EDCA_VI_PARAM, u1bAIFS);
					rtw_write8(Adapter, REG_EDCA_BE_PARAM, u1bAIFS);
					rtw_write8(Adapter, REG_EDCA_BK_PARAM, u1bAIFS);
				}
			}
			break;
		case HW_VAR_SIFS:
			{
				// SIFS for OFDM Data ACK
				rtw_write8(Adapter, REG_SIFS_CTX+1, val[0]);
				// SIFS for OFDM consecutive tx like CTS data!
				rtw_write8(Adapter, REG_SIFS_TRX+1, val[1]);
				
				rtw_write8(Adapter,REG_SPEC_SIFS+1, val[0]);
				rtw_write8(Adapter,REG_MAC_SPEC_SIFS+1, val[0]);

				// 20100719 Joseph: Revise SIFS setting due to Hardware register definition change.
				rtw_write8(Adapter, REG_R2T_SIFS+1, val[0]);
				rtw_write8(Adapter, REG_T2T_SIFS+1, val[0]);
			}
			break;
		case HW_VAR_ACK_PREAMBLE:
			{
				u8	regTmp;
				u8	bShortPreamble = *( (PBOOLEAN)val );
				// Joseph marked out for Netgear 3500 TKIP channel 7 issue.(Temporarily)
				//regTmp = (pHalData->nCur40MhzPrimeSC)<<5;
				regTmp = 0;
				if(bShortPreamble)
					regTmp |= 0x80;

				rtw_write8(Adapter, REG_RRSR+2, regTmp);
			}
			break;
		case HW_VAR_SEC_CFG:
			rtw_write8(Adapter, REG_SECCFG, *((u8 *)val));
			break;
		case HW_VAR_DM_FLAG:
			pdmpriv->DMFlag = *((u8 *)val);
			break;
		case HW_VAR_DM_FUNC_OP:
			if(val[0])
			{// save dm flag
				pdmpriv->DMFlag_tmp = pdmpriv->DMFlag;
			}
			else
			{// restore dm flag
				pdmpriv->DMFlag = pdmpriv->DMFlag_tmp;
			}
			break;
		case HW_VAR_DM_FUNC_SET:
			pdmpriv->DMFlag |= *((u8 *)val);
			break;
		case HW_VAR_DM_FUNC_CLR:
			pdmpriv->DMFlag &= *((u8 *)val);
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
			rtw_write8(Adapter, REG_USB_HRPWM, *((u8 *)val));
			break;
		case HW_VAR_H2C_FW_PWRMODE:
			{
				u8	psmode = (*(u8 *)val);
			
				// Forece leave RF low power mode for 1T1R to prevent conficting setting in Fw power
				// saving sequence. 2010.06.07. Added by tynli. Suggested by SD3 yschang.
				if( (psmode != PS_MODE_ACTIVE) && (!IS_92C_SERIAL(pHalData->VersionID)))
				{
					rtl8192c_dm_RF_Saving(Adapter, _TRUE);
				}
				rtl8192c_set_FwPwrMode_cmd(Adapter, psmode);
			}
			break;
		case HW_VAR_H2C_FW_JOINBSSRPT:
			{
				u8	mstatus = (*(u8 *)val);
				rtl8192c_set_FwJoinBssReport_cmd(Adapter, mstatus);
			}
			break;
#ifdef CONFIG_P2P
		case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
			{
				u8	p2p_ps_state = (*(u8 *)val);
				rtl8192c_set_p2p_ps_offload_cmd(Adapter, p2p_ps_state);
			}
			break;
#endif //CONFIG_P2P
		case HW_VAR_INITIAL_GAIN:
			{
				DIG_T	*pDigTable = &pdmpriv->DM_DigTable;					
				u32 		rx_gain = ((u32 *)(val))[0];
				
				if(rx_gain == 0xff){//restore rx gain
					pDigTable->CurIGValue = pDigTable->BackupIGValue;
					rtw_write8(Adapter,rOFDM0_XAAGCCore1, pDigTable->CurIGValue);
					rtw_write8(Adapter,rOFDM0_XBAGCCore1, pDigTable->CurIGValue);
				}
				else{
					pDigTable->BackupIGValue = pDigTable->CurIGValue;					
					PHY_SetBBReg(Adapter, rOFDM0_XAAGCCore1, 0x7f,rx_gain );
					PHY_SetBBReg(Adapter, rOFDM0_XBAGCCore1, 0x7f,rx_gain);
					pDigTable->CurIGValue = rx_gain;
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
#ifdef CONFIG_SW_ANTENNA_DIVERSITY

		case HW_VAR_ANTENNA_DIVERSITY_LINK:
			SwAntDivRestAfterLink8192C(Adapter);
			break;
		case HW_VAR_ANTENNA_DIVERSITY_SELECT:
			{
				u8	Optimum_antenna = (*(u8 *)val);
				//switch antenna to Optimum_antenna
		//		DBG_8192C("==> HW_VAR_ANTENNA_DIVERSITY_SELECT , Ant_(%s)\n",(Optimum_antenna==2)?"A":"B");
				if(pHalData->CurAntenna !=  Optimum_antenna)		
				{
					PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, 0x300, Optimum_antenna);
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
				#define RW_RELEASE_EN		BIT18
				#define RXDMA_IDLE			BIT17
				
				struct pwrctrl_priv *pwrpriv = &Adapter->pwrctrlpriv;
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
		default:
			break;
	}

_func_exit_;
}

void GetHwReg8192CU(PADAPTER Adapter, u8 variable, u8* val)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

_func_enter_;

	switch(variable)
	{
		case HW_VAR_BASIC_RATE:
			*((u16 *)(val)) = pHalData->BasicRateSet;
		case HW_VAR_TXPAUSE:
			val[0] = rtw_read8(Adapter, REG_TXPAUSE);
			break;
		case HW_VAR_TX_BCN_DONE:
			{
				u32 xmitbcnDown;
				xmitbcnDown= rtw_read32(Adapter, REG_TDECTRL);
				if(xmitbcnDown & BCN_VALID  ){
					rtw_write32(Adapter,REG_TDECTRL, xmitbcnDown | BCN_VALID  ); // write 1 to clear, Clear by sw
					val[0] = _TRUE;
				}
			}
			break;
		case HW_VAR_DM_FLAG:
			val[0] = pHalData->dmpriv.DMFlag;
			break;
		case HW_VAR_RF_TYPE:
			val[0] = pHalData->rf_type;
			break;
		case HW_VAR_FWLPS_RF_ON:
			{
				//When we halt NIC, we should check if FW LPS is leave.
				u32	valRCR;
				
				if(Adapter->pwrctrlpriv.rf_pwrstate == rf_off)
				{
					// If it is in HW/SW Radio OFF or IPS state, we do not check Fw LPS Leave,
					// because Fw is unload.
					val[0] = _TRUE;
				}
				else
				{
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
		default:
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
	u8			bResult = _TRUE;

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
		case HAL_DEF_DBG_DM_FUNC:
			*(( u8*)pValue) = pHalData->dmpriv.DMFlag;
			break;	
		default:
			//RT_TRACE(COMP_INIT, DBG_WARNING, ("GetHalDefVar8192CUsb(): Unkown variable: %d!\n", eVariable));
			bResult = _FALSE;
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
	u8			bResult = _TRUE;

	switch(eVariable)
	{
		case HAL_DEF_DBG_DUMP_RXPKT:
			pHalData->bDumpRxPkt = *(( u8*)pValue);
			break;
		case HAL_DEF_DBG_DM_FUNC:
			{
				u8 dm_func = *(( u8*)pValue);
				struct dm_priv	*pdmpriv = &pHalData->dmpriv;	
				
				if(dm_func == 0){ //disable all dynamic func
					pdmpriv->DMFlag = DYNAMIC_FUNC_DISABLE;
					DBG_8192C("==> Disable all dynamic function...\n");
				}
				else if(dm_func == 1){//disable DIG
					pdmpriv->DMFlag &= (~DYNAMIC_FUNC_DIG);
					DBG_8192C("==> Disable DIG...\n");
				}
				else if(dm_func == 2){//disable High power
					pdmpriv->DMFlag &= (~DYNAMIC_FUNC_HP);
				}
				else if(dm_func == 3){//disable tx power tracking
					pdmpriv->DMFlag &= (~DYNAMIC_FUNC_SS);
					DBG_8192C("==> Disable tx power tracking...\n");
				}
				else if(dm_func == 4){//disable BT coexistence
					pdmpriv->DMFlag &= (~DYNAMIC_FUNC_BT);
				}
				else if(dm_func == 5){//disable antenna diversity
					pdmpriv->DMFlag &= (~DYNAMIC_FUNC_ANT_DIV);
				}				
				else if(dm_func == 6){//turn on all dynamic func
					if(!(pdmpriv->DMFlag & DYNAMIC_FUNC_DIG))
					{
						struct dm_priv	*pdmpriv = &pHalData->dmpriv;
						DIG_T	*pDigTable = &pdmpriv->DM_DigTable;
						pDigTable->PreIGValue = rtw_read8(Adapter,0xc50);	
					}
						
					pdmpriv->DMFlag |= (DYNAMIC_FUNC_DIG|DYNAMIC_FUNC_HP|DYNAMIC_FUNC_SS|
						DYNAMIC_FUNC_BT|DYNAMIC_FUNC_ANT_DIV) ;
					DBG_8192C("==> Turn on all dynamic function...\n");
				}			
			}
			break;
		default:
			//RT_TRACE(COMP_INIT, DBG_TRACE, ("SetHalDefVar819xUsb(): Unkown variable: %d!\n", eVariable));
			bResult = _FALSE;
			break;
	}

	return bResult;
}

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
		if(pHalData->VersionID != VERSION_TEST_CHIP_88C)
			BrateCfg = mask  & 0x15F;
		else	//for 88CU 46PING setting, Disable CCK 2M, 5.5M, Others must tuning
			BrateCfg = mask  & 0x159;
	}

	BrateCfg |= 0x01; // default enable 1M ACK rate					

	return BrateCfg;
}

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

void UpdateHalRAMask8192CUsb(PADAPTER padapter, u32 mac_id)
{
	//volatile unsigned int result;
	u8	init_rate=0;
	u8	networkType, raid;	
	u32	mask;
	u8	shortGIrate = _FALSE;
	int	supportRateNum = 0;
	struct sta_info	*psta;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
#ifdef CONFIG_BT_COEXIST
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);
#endif

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
			supportRateNum = rtw_get_rateset_len(cur_network->SupportedRates);
			networkType = judge_network_type(padapter, cur_network->SupportedRates, supportRateNum) & 0xf;
			//pmlmeext->cur_wireless_mode = networkType;
			raid = networktype_to_raid(networkType);
						
			mask = update_supported_rate(cur_network->SupportedRates, supportRateNum);
			mask |= (pmlmeinfo->HT_enable)? update_MSC_rate(&(pmlmeinfo->HT_caps)): 0;
			mask |= ((raid<<28)&0xf0000000);
			
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
			mask |= ((raid<<28)&0xf0000000);

			break;

		default: //for each sta in IBSS
			supportRateNum = rtw_get_rateset_len(pmlmeinfo->FW_sta_info[mac_id].SupportedRates);
			networkType = judge_network_type(padapter, pmlmeinfo->FW_sta_info[mac_id].SupportedRates, supportRateNum) & 0xf;
			//pmlmeext->cur_wireless_mode = networkType;
			raid = networktype_to_raid(networkType);
			
			mask = update_supported_rate(cur_network->SupportedRates, supportRateNum);
			mask |= ((raid<<28)&0xf0000000);

			//todo: support HT in IBSS
			
			break;
	}
	
#ifdef CONFIG_BT_COEXIST
	if( (pbtpriv->BT_Coexist) &&
		(pbtpriv->BT_CoexistType == BT_CSR_BC4) &&
		(pbtpriv->BT_CUR_State) &&
		(pbtpriv->BT_Ant_isolation) &&
		((pbtpriv->BT_Service==BT_SCO)||
		(pbtpriv->BT_Service==BT_Busy)) )
		mask &= 0xffffcfc0;
	else		
#endif
		mask &=0xffffffff;
	
	
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

void SetBeaconRelatedRegisters8192CUsb(PADAPTER padapter)
{
	u32	value32;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

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

	rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)|BIT(1));

}

static void rtl8192cu_init_default_value(_adapter * padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u8	i;

	//init default value
	pHalData->fw_ractrl = _FALSE;	
	pHalData->bIQKInitialized = _FALSE;
	if(!pwrctrlpriv->bkeepfwalive)
		pHalData->LastHMEBoxNum = 0;
	
	pHalData->bIQKInitialized = _FALSE;
	//init dm default value
	pdmpriv->TM_Trigger = 0;
	pdmpriv->binitialized = _FALSE;
	pdmpriv->prv_traffic_idx = 3;
	pdmpriv->initialize = 0;

	pdmpriv->ThermalValue_HP_index = 0;
	for(i = 0; i < HP_THERMAL_NUM; i++)
		pdmpriv->ThermalValue_HP[i] = 0;
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
void rtl8192cu_set_hal_ops(_adapter * padapter)
{
	struct hal_ops	*pHalFunc = &padapter->HalFunc;

_func_enter_;

	padapter->HalData = rtw_zmalloc(sizeof(HAL_DATA_TYPE));
	if(padapter->HalData == NULL){
		DBG_8192C("cant not alloc memory for HAL DATA \n");
	}
	//_rtw_memset(padapter->HalData, 0, sizeof(HAL_DATA_TYPE));

	pHalFunc->hal_init = &rtl8192cu_hal_init;
	pHalFunc->hal_deinit = &rtl8192cu_hal_deinit;

	//pHalFunc->free_hal_data = &rtl8192c_free_hal_data;

	pHalFunc->inirp_init = &rtl8192cu_inirp_init;
	pHalFunc->inirp_deinit = &rtl8192cu_inirp_deinit;

	pHalFunc->init_xmit_priv = &rtl8192cu_init_xmit_priv;
	pHalFunc->free_xmit_priv = &rtl8192cu_free_xmit_priv;

	pHalFunc->init_recv_priv = &rtl8192cu_init_recv_priv;
	pHalFunc->free_recv_priv = &rtl8192cu_free_recv_priv;
#ifdef CONFIG_SW_LED
	pHalFunc->InitSwLeds = &rtl8192cu_InitSwLeds;
	pHalFunc->DeInitSwLeds = &rtl8192cu_DeInitSwLeds;
#else //case of hw led or no led
	pHalFunc->InitSwLeds = NULL;
	pHalFunc->DeInitSwLeds = NULL;
#endif//CONFIG_SW_LED

	//pHalFunc->dm_init = &rtl8192c_init_dm_priv;
	//pHalFunc->dm_deinit = &rtl8192c_deinit_dm_priv;

	pHalFunc->init_default_value = &rtl8192cu_init_default_value;
	pHalFunc->intf_chip_configure = &rtl8192cu_interface_configure;
	pHalFunc->read_adapter_info = &ReadAdapterInfo8192CU;

	//pHalFunc->set_bwmode_handler = &PHY_SetBWMode8192C;
	//pHalFunc->set_channel_handler = &PHY_SwChnl8192C;

	//pHalFunc->hal_dm_watchdog = &rtl8192c_HalDmWatchDog;

	pHalFunc->SetHwRegHandler = &SetHwReg8192CU;
	pHalFunc->GetHwRegHandler = &GetHwReg8192CU;
  	pHalFunc->GetHalDefVarHandler = &GetHalDefVar8192CUsb;
 	pHalFunc->SetHalDefVarHandler = &SetHalDefVar8192CUsb;

	pHalFunc->UpdateRAMaskHandler = &UpdateHalRAMask8192CUsb;
	pHalFunc->SetBeaconRelatedRegistersHandler = &SetBeaconRelatedRegisters8192CUsb;

	//pHalFunc->Add_RateATid = &rtl8192c_Add_RateATid;

//#ifdef CONFIG_SW_ANTENNA_DIVERSITY
	//pHalFunc->SwAntDivBeforeLinkHandler = &SwAntDivBeforeLink8192C;
	//pHalFunc->SwAntDivCompareHandler = &SwAntDivCompare8192C;
//#endif

	pHalFunc->hal_xmit = &rtl8192cu_hal_xmit;
	pHalFunc->mgnt_xmit = &rtl8192cu_mgnt_xmit;
	
	//pHalFunc->read_bbreg = &rtl8192c_PHY_QueryBBReg;
	//pHalFunc->write_bbreg = &rtl8192c_PHY_SetBBReg;
	//pHalFunc->read_rfreg = &rtl8192c_PHY_QueryRFReg;
	//pHalFunc->write_rfreg = &rtl8192c_PHY_SetRFReg;

#ifdef CONFIG_HOSTAPD_MLME
	pHalFunc->hostap_mgnt_xmit_entry = &rtl8192cu_hostap_mgnt_xmit_entry;
#endif
	pHalFunc->interface_ps_func = &rtl8192cu_ps_func;
	
	rtl8192c_set_hal_ops(pHalFunc);
_func_exit_;

}

