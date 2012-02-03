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
#include <hal_init.h>
#include <rtw_efuse.h>

#include <rtl8192d_hal.h>
#include <rtl8192d_led.h>

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

//add mutex to solve the problem that reading efuse and power on/fw download do 
//on the same time 
extern atomic_t GlobalMutexForMac0_2G_Mac1_5G;
extern atomic_t GlobalMutexForPowerAndEfuse;
extern atomic_t GlobalMutexForPowerOnAndPowerOff;
#ifdef CONFIG_DUALMAC_CONCURRENT
extern atomic_t GlobalCounterForMutex;
extern BOOLEAN GlobalFirstConfigurationForNormalChip;
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
	
	pHalData->Queue2EPNum[4] = pHalData->RtBulkOutPipe[0];//TS
	pHalData->Queue2EPNum[5] = pHalData->RtBulkOutPipe[0];//MGT
	pHalData->Queue2EPNum[6] = pHalData->RtBulkOutPipe[0];//BMC
	pHalData->Queue2EPNum[7] = pHalData->RtBulkOutPipe[0];//BCN
}


static VOID
_TwoOutEpMapping(
	IN	HAL_DATA_TYPE	*pHalData,
	IN	BOOLEAN	 		bWIFICfg
	)
{

	if(bWIFICfg){ // Normal chip && wmm
		
		//	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA 
		//{  0, 	1, 	0, 	1, 	0, 	0, 	0, 	0, 		0	};
		//0:H(end_number=0x02), 1:L (end_number=0x03)
		
		pHalData->Queue2EPNum[0] = pHalData->RtBulkOutPipe[1];//VO
		pHalData->Queue2EPNum[1] = pHalData->RtBulkOutPipe[0];//VI
		pHalData->Queue2EPNum[2] = pHalData->RtBulkOutPipe[1];//BE
		pHalData->Queue2EPNum[3] = pHalData->RtBulkOutPipe[0];//BK
		
		pHalData->Queue2EPNum[4] = pHalData->RtBulkOutPipe[0];//TS
		pHalData->Queue2EPNum[5] = pHalData->RtBulkOutPipe[0];//MGT
		pHalData->Queue2EPNum[6] = pHalData->RtBulkOutPipe[0];//BMC
		pHalData->Queue2EPNum[7] = pHalData->RtBulkOutPipe[0];//BCN
		
	}
	else{//typical setting

		
		//BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA 
		//{  1, 	1, 	0, 	0, 	0, 	0, 	0, 	0, 		0	};			
		//0:H(end_number=0x02), 1:L (end_number=0x03)
		
		pHalData->Queue2EPNum[0] = pHalData->RtBulkOutPipe[0];//VO
		pHalData->Queue2EPNum[1] = pHalData->RtBulkOutPipe[0];//VI
		pHalData->Queue2EPNum[2] = pHalData->RtBulkOutPipe[1];//BE
		pHalData->Queue2EPNum[3] = pHalData->RtBulkOutPipe[1];//BK
		
		pHalData->Queue2EPNum[4] = pHalData->RtBulkOutPipe[0];//TS
		pHalData->Queue2EPNum[5] = pHalData->RtBulkOutPipe[0];//MGT
		pHalData->Queue2EPNum[6] = pHalData->RtBulkOutPipe[0];//BMC
		pHalData->Queue2EPNum[7] = pHalData->RtBulkOutPipe[0];//BCN	
		
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
		
		pHalData->Queue2EPNum[4] = pHalData->RtBulkOutPipe[0];//TS
		pHalData->Queue2EPNum[5] = pHalData->RtBulkOutPipe[0];//MGT
		pHalData->Queue2EPNum[6] = pHalData->RtBulkOutPipe[0];//BMC
		pHalData->Queue2EPNum[7] = pHalData->RtBulkOutPipe[0];//BCN
		
	}
	else{//typical setting

		
		//	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA 
		//{  2, 	2, 	1, 	0, 	0, 	0, 	0, 	0, 		0	};			
		//0:H(end_number=0x02), 1:N(end_number=0x03), 2:L (end_number=0x05)
		
		pHalData->Queue2EPNum[0] = pHalData->RtBulkOutPipe[0];//VO
		pHalData->Queue2EPNum[1] = pHalData->RtBulkOutPipe[1];//VI
		pHalData->Queue2EPNum[2] = pHalData->RtBulkOutPipe[2];//BE
		pHalData->Queue2EPNum[3] = pHalData->RtBulkOutPipe[2];//BK
		
		pHalData->Queue2EPNum[4] = pHalData->RtBulkOutPipe[0];//TS
		pHalData->Queue2EPNum[5] = pHalData->RtBulkOutPipe[0];//MGT
		pHalData->Queue2EPNum[6] = pHalData->RtBulkOutPipe[0];//BMC
		pHalData->Queue2EPNum[7] = pHalData->RtBulkOutPipe[0];//BCN	
	}

}

static BOOLEAN
_MappingOutEP(
	IN	PADAPTER	pAdapter,
	IN	u8		NumOutPipe
	)
{		
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct registry_priv *pregistrypriv = &pAdapter->registrypriv;

	BOOLEAN	 bWIFICfg = (pregistrypriv->wifi_spec) ?_TRUE:_FALSE;
	
	BOOLEAN result = _TRUE;

	switch(NumOutPipe)
	{
		case 2:
			_TwoOutEpMapping(pHalData, bWIFICfg);
			break;
		case 3:
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
_ConfigChipOutEP(
	IN	PADAPTER	pAdapter,
	IN	u8		NumOutPipe
	)
{
	u8			value8;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);

	pHalData->OutEpQueueSel = 0;
	pHalData->OutEpNumber = 0;
		
	// Normal and High queue
	if(pHalData->interfaceIndex==0)
		value8=rtw_read8(pAdapter, REG_USB_High_NORMAL_Queue_Select_MAC0);
	else
		value8=rtw_read8(pAdapter, REG_USB_High_NORMAL_Queue_Select_MAC1);

	if(value8 & USB_NORMAL_SIE_EP_MASK){
		pHalData->OutEpQueueSel |= TX_SELE_HQ;
		pHalData->OutEpNumber++;
	}
	
	if((value8 >> USB_NORMAL_SIE_EP_SHIFT) & USB_NORMAL_SIE_EP_MASK){
		pHalData->OutEpQueueSel |= TX_SELE_NQ;
		pHalData->OutEpNumber++;
	}
	
	// Low queue
	if(pHalData->interfaceIndex==0)
		value8=rtw_read8(pAdapter, (REG_USB_High_NORMAL_Queue_Select_MAC0+1));
	else
		value8=rtw_read8(pAdapter, (REG_USB_High_NORMAL_Queue_Select_MAC1+1));
	
	if(value8 & USB_NORMAL_SIE_EP_MASK){
		pHalData->OutEpQueueSel |= TX_SELE_LQ;
		pHalData->OutEpNumber++;
	}

	//add for 0xfe44 0xfe45 0xfe47 0xfe48 not validly 
	switch(NumOutPipe){
		case 3:
			pHalData->OutEpQueueSel=TX_SELE_HQ| TX_SELE_LQ|TX_SELE_NQ;
			pHalData->OutEpNumber=3;
			break;
		case 2:
			pHalData->OutEpQueueSel=TX_SELE_HQ| TX_SELE_NQ;
			pHalData->OutEpNumber=2;
			break;
		case 1:
			pHalData->OutEpQueueSel=TX_SELE_HQ;
			pHalData->OutEpNumber=1;
			break;
		default:				
			break;
	}

	// TODO: Error recovery for this case
	//RT_ASSERT((NumOutPipe == pHalData->OutEpNumber), ("Out EP number isn't match! %d(Descriptor) != %d (SIE reg)\n", (u4Byte)NumOutPipe, (u4Byte)pHalData->OutEpNumber));

}

static BOOLEAN HalUsbSetQueuePipeMapping8192DUsb(
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

	result = _MappingOutEP(pAdapter, NumOutPipe);
	
	return result;

}

void rtl8192du_interface_configure(_adapter *padapter);
void rtl8192du_interface_configure(_adapter *padapter)
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
	//DBG_8192C("Bulk In = %x, Bulk Out = %x %x %x\n",pHalData->RtBulkInPipe, pHalData->RtBulkOutPipe[0],pHalData->RtBulkOutPipe[1],pHalData->RtBulkOutPipe[2]);
#ifdef CONFIG_USB_TX_AGGREGATION
	pHalData->UsbTxAggMode		= 1;
	pHalData->UsbTxAggDescNum	= 0x4;	// only 4 bits
#endif

#ifdef CONFIG_USB_RX_AGGREGATION
	//pHalData->UsbRxAggMode = USB_RX_AGG_DMA_USB;// USB_RX_AGG_DMA;
	pHalData->UsbRxAggMode = USB_RX_AGG_DMA;// USB_RX_AGG_DMA;
	pHalData->UsbRxAggBlockCount	= 8; //unit : 512b
	pHalData->UsbRxAggBlockTimeout = 0x6;
	pHalData->UsbRxAggPageCount	= 48; //uint :128 b //0x0A;	// 10 = MAX_RX_DMA_BUFFER_SIZE/2/pHalData->UsbBulkOutSize
	pHalData->UsbRxAggPageTimeout = 0x6; //6, absolute time = 34ms/(2^6)
#endif

	HalUsbSetQueuePipeMapping8192DUsb(padapter,
				pdvobjpriv->RtNumInPipes, pdvobjpriv->RtNumOutPipes);

}

static u8 _InitPowerOn(_adapter *padapter)
{
	u8	ret = _SUCCESS;
	u16	value16=0;
	u8	value8 = 0;
//	HAL_DATA_TYPE    *pHalData = GET_HAL_DATA(padapter);

	// polling autoload done.
	u32	pollingCount = 0;

	if(padapter->bSurpriseRemoved){
		return _FAIL;
	}

#if 0
	if(pHalData->MacPhyMode92D != SINGLEMAC_SINGLEPHY)
	{
		// in DMDP mode,polling power off in process bit 0x17[7]
		value8=rtw_read8(padapter, 0x17);
		while((value8&BIT7) && (pollingCount < 200))
		{
			rtw_udelay_os(200);
			value8=rtw_read8(padapter, 0x17);		
			pollingCount++;
		}
	}
#endif
	pollingCount = 0;
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


	//For hardware power on sequence.

	//0.	RSV_CTRL 0x1C[7:0] = 0x00			// unlock ISO/CLK/Power control register
	rtw_write8(padapter, REG_RSV_CTRL, 0x0);	
	// Power on when re-enter from IPS/Radio off/card disable
	rtw_write8(padapter, REG_SPS0_CTRL, 0x2b);//enable SPS into PWM mode
	rtw_usleep_os(100);//PlatformSleepUs(150);//this is not necessary when initially power on

	value8 = rtw_read8(padapter, REG_LDOV12D_CTRL);	
	if(0== (value8 & LDV12_EN) ){
		value8 |= LDV12_EN;
		rtw_write8(padapter, REG_LDOV12D_CTRL, value8);	
		//RT_TRACE(COMP_INIT, DBG_LOUD, (" power-on :REG_LDOV12D_CTRL Reg0x21:0x%02x.\n",value8));
		rtw_usleep_os(100);//PlatformSleepUs(100);//this is not necessary when initially power on
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

	// release RF digital isolation
	value16 = rtw_read16(padapter, REG_SYS_ISO_CTRL);
	value16 &= ~ISO_DIOR;
	rtw_write16(padapter, REG_SYS_ISO_CTRL, value16);

	// Enable MAC DMA/WMAC/SCHEDULE/SEC block
	value16 = rtw_read16(padapter, REG_CR);
	value16 |= (HCI_TXDMA_EN | HCI_RXDMA_EN | TXDMA_EN | RXDMA_EN
				| PROTOCOL_EN | SCHEDULE_EN | MACTXEN | MACRXEN | ENSEC);
	rtw_write16(padapter, REG_CR, value16);

	return ret;

}


static VOID 
PHY_InitPABias92D(IN	PADAPTER Adapter)
{
	u8	tmpU1b;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	BOOLEAN		is92 = IS_92D_SINGLEPHY(pHalData->VersionID);
	RF_RADIO_PATH_E eRFPath = RF_PATH_A;

	tmpU1b = EFUSE_Read1Byte(Adapter, 0x3FA); 

	DBG_871X("PHY_InitPABias92D 0x3FA 0x%x \n",tmpU1b);

	if(!(tmpU1b & BIT0) && (is92 || pHalData->interfaceIndex == 0))
	{
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, 0x07401);	
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x70000);		
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x0F425);
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x4F425);		
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x8F425);		
		
		//Back to RX Mode
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x30000);				
		DBG_871X("2G PA BIAS path A\n");
	}	

	if(!(tmpU1b & BIT1) && (is92 || pHalData->interfaceIndex == 1))
	{
		eRFPath = pHalData->interfaceIndex == 1?RF_PATH_A:RF_PATH_B;
		PHY_SetRFReg(Adapter, eRFPath, RF_CHNLBW, bRFRegOffsetMask, 0x07401);	
		PHY_SetRFReg(Adapter, eRFPath, RF_AC, bRFRegOffsetMask, 0x70000);				
		PHY_SetRFReg(Adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x0F425);
		PHY_SetRFReg(Adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x4F425);		
		PHY_SetRFReg(Adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x8F425);

		//Back to RX Mode
		PHY_SetRFReg(Adapter, eRFPath, RF_AC, bRFRegOffsetMask, 0x30000);			
		DBG_871X("2G PA BIAS path B\n");
	}

	if(!(tmpU1b & BIT2) && (is92 || pHalData->interfaceIndex == 0))
	{
		//5GL_channel	
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, 0x17524);	
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x70000);			
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x0F496);
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x4F496);		
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x8F496);		

		//5GM_channel	
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, 0x37564);	
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x70000);			
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x0F496);
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x4F496);		
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x8F496);		

		//5GH_channel	
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, 0x57595);	
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x70000);			
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x0F496);
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x4F496);		
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_IPA, bRFRegOffsetMask, 0x8F496);			

		//Back to RX Mode
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x30000);				
		
		DBG_871X("5G PA BIAS path A\n");
	}	

	if(!(tmpU1b & BIT3) && (is92 || pHalData->interfaceIndex == 1))
	{
		eRFPath = (pHalData->interfaceIndex == 1)?RF_PATH_A:RF_PATH_B;	
		//5GL_channel	
		PHY_SetRFReg(Adapter, eRFPath, RF_CHNLBW, bRFRegOffsetMask, 0x17524);	
		PHY_SetRFReg(Adapter, eRFPath, RF_AC, bRFRegOffsetMask, 0x70000);		
		PHY_SetRFReg(Adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x0F496);
		PHY_SetRFReg(Adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x4F496);		
		PHY_SetRFReg(Adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x8F496);

		//5GM_channel	
		PHY_SetRFReg(Adapter, eRFPath, RF_CHNLBW, bRFRegOffsetMask, 0x37564);	
		PHY_SetRFReg(Adapter, eRFPath, RF_AC, bRFRegOffsetMask, 0x70000);	
		PHY_SetRFReg(Adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x0F496);
		PHY_SetRFReg(Adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x4F496);		
		PHY_SetRFReg(Adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x8F496);

		//5GH_channel	
		PHY_SetRFReg(Adapter, eRFPath, RF_CHNLBW, bRFRegOffsetMask, 0x57595);	
		PHY_SetRFReg(Adapter, eRFPath, RF_AC, bRFRegOffsetMask, 0x70000);			
		PHY_SetRFReg(Adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x0F496);
		PHY_SetRFReg(Adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x4F496);		
		PHY_SetRFReg(Adapter, eRFPath, RF_IPA, bRFRegOffsetMask, 0x8F496);

		//Back to RX Mode
		PHY_SetRFReg(Adapter, eRFPath, RF_AC, bRFRegOffsetMask, 0x30000);			
		DBG_871X("5G PA BIAS path B\n");
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
	int	count = 0;
	u32	value = _LLT_INIT_ADDR(address) | _LLT_INIT_DATA(data) | _LLT_OP(_LLT_WRITE_ACCESS);

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

#ifndef PLATFORM_FREEBSD //amy,temp remove
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
#endif //amy,temp remove

static u8 InitLLTTable(
	IN  PADAPTER	Adapter,
	IN	u32		boundary
	)
{
	u8		status = _SUCCESS;
	u32		i;
	u32		txpktbuf_bndy = boundary;
	u32		Last_Entry_Of_TxPktBuf = LAST_ENTRY_OF_TX_PKT_BUFFER;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);


	if(pHalData->MacPhyMode92D !=SINGLEMAC_SINGLEPHY){
		//for 92du two mac: The page size is different from 92c and 92s
		txpktbuf_bndy =TX_PAGE_BOUNDARY_DUAL_MAC;
		Last_Entry_Of_TxPktBuf=LAST_ENTRY_OF_TX_PKT_BUFFER_DUAL_MAC;
	}
	else{
		txpktbuf_bndy = boundary;
		Last_Entry_Of_TxPktBuf=LAST_ENTRY_OF_TX_PKT_BUFFER;
		//txpktbuf_bndy =253;
		//Last_Entry_Of_TxPktBuf=255;
	} 

	for(i = 0 ; i < (txpktbuf_bndy - 1) ; i++){
		status = _LLTWrite(Adapter, i , i + 1);
		if(_SUCCESS != status){
			return status;
		}
	}

	// end of list
	status = _LLTWrite(Adapter, (txpktbuf_bndy - 1), 0xFF);
	if(_SUCCESS != status){
		return status;
	}

	// Make the other pages as ring buffer
	// This ring buffer is used as beacon buffer if we config this MAC as two MAC transfer.
	// Otherwise used as local loopback buffer. 
	for(i = txpktbuf_bndy ; i < Last_Entry_Of_TxPktBuf ; i++){
		status = _LLTWrite(Adapter, i, (i + 1)); 
		if(_SUCCESS != status){
			return status;
		}
	}
	
	// Let last entry point to the start entry of ring buffer
	status = _LLTWrite(Adapter, Last_Entry_Of_TxPktBuf, txpktbuf_bndy);
	if(_SUCCESS != status){
		return status;
	}

	return status;
	
}

#ifndef PLATFORM_FREEBSD //amy,temp remove
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
#endif //amy,temp remove

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
	u32			txQPageNum, txQPageUnit,txQRemainPage;

	if(!pregistrypriv->wifi_spec)
	{
		if(pHalData->MacPhyMode92D !=SINGLEMAC_SINGLEPHY)	
		{
			numPubQ = NORMAL_PAGE_NUM_PUBQ_92D_DUAL_MAC;
			txQPageNum = TX_TOTAL_PAGE_NUMBER_92D_DUAL_MAC- numPubQ;	
		}
		else
		{
			numPubQ =TEST_PAGE_NUM_PUBQ;
			//RT_ASSERT((numPubQ < TX_TOTAL_PAGE_NUMBER), ("Public queue page number is great than total tx page number.\n"));
			txQPageNum = TX_TOTAL_PAGE_NUMBER - numPubQ;
		}

		if((pHalData->MacPhyMode92D !=SINGLEMAC_SINGLEPHY)&&(outEPNum==3))
		{// temply for DMDP/DMSP Page allocate
			numHQ=NORMAL_PAGE_NUM_HPQ_92D_DUAL_MAC;
			numLQ=NORMAL_PAGE_NUM_LPQ_92D_DUAL_MAC;
			numNQ=NORMAL_PAGE_NUM_NORMALQ_92D_DUAL_MAC;
		}
		else
		{
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
			if(pHalData->OutEpQueueSel & TX_SELE_NQ)
				numNQ = txQPageUnit;

			value8 = (u8)_NPQ(numNQ);
			rtw_write8(Adapter, REG_RQPN_NPQ, value8);
		}
	}
	else{ //for WMM 
		//RT_ASSERT((outEPNum>=2), ("for WMM ,number of out-ep must more than or equal to 2!\n"));
		// 92du wifi config only for SMSP
		//RT_ASSERT((pHalData->MacPhyMode92D==SINGLEMAC_SINGLEPHY), ("for WMM ,only SMSP come here!\n"));

		numPubQ = (outEPNum==2)?WMM_NORMAL_PAGE_NUM_PUBQ:WMM_NORMAL_PAGE_NUM_PUBQ_92D;

		if(pHalData->OutEpQueueSel & TX_SELE_HQ){
			numHQ = (outEPNum==2)?WMM_NORMAL_PAGE_NUM_HPQ:WMM_NORMAL_PAGE_NUM_HPQ_92D;							
		}

		if(pHalData->OutEpQueueSel & TX_SELE_LQ){
			numLQ = (outEPNum==2)?WMM_NORMAL_PAGE_NUM_LPQ:WMM_NORMAL_PAGE_NUM_LPQ_92D;
		}

		if(pHalData->OutEpQueueSel & TX_SELE_NQ){
			numNQ = (outEPNum==2)?WMM_NORMAL_PAGE_NUM_NPQ:WMM_NORMAL_PAGE_NUM_NPQ_92D;
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
#ifdef  CONFIG_CONCURRENT_MODE
		if(Adapter->iface_type == IFACE_PORT1)			
			rtw_write8(Adapter, (REG_MACID1+i), pEEPROM->mac_addr[i]);			
		else
#endif			
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
*/
}


static VOID
_InitTxBufferBoundary(
	IN  PADAPTER Adapter
	)
{	
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

	//u16	txdmactrl;	
	u8	txpktbuf_bndy; 

	if(!pregistrypriv->wifi_spec){
		txpktbuf_bndy = TX_PAGE_BOUNDARY;
	}
	else{//for WMM
		txpktbuf_bndy = WMM_NORMAL_TX_PAGE_BOUNDARY;
	}

	if(pHalData->MacPhyMode92D !=SINGLEMAC_SINGLEPHY)
		txpktbuf_bndy = TX_PAGE_BOUNDARY_DUAL_MAC;

	rtw_write8(Adapter, REG_TXPKTBUF_BCNQ_BDNY, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TXPKTBUF_MGQ_BDNY, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TXPKTBUF_WMAC_LBK_BF_HD, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TRXFF_BNDY, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TDECTRL+1, txpktbuf_bndy);
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
		beQ 		= valueLow;
		bkQ 		= valueLow;
		viQ		= valueHi;
		voQ 		= valueHi;
		mgtQ 	= valueHi; 
		hiQ 		= valueHi;								
	}
	else{//for WMM ,CONFIG_OUT_EP_WIFI_MODE
		beQ		= valueLow;
		bkQ 		= valueHi;		
		viQ 		= valueHi;
		voQ 		= valueLow;
		mgtQ 	= valueHi;
		hiQ 		= valueHi;							
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
	IN  PADAPTER Adapter
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
#if ENABLE_USB_DROP_INCORRECT_OUT //amy,temp remove
static VOID
_InitHardwareDropIncorrectBulkOut(
	IN  PADAPTER Adapter
	)
{
	u32	value32 = rtw_read32(Adapter, REG_TXDMA_OFFSET_CHK);
	value32 |= DROP_DATA_EN;
	rtw_write32(Adapter, REG_TXDMA_OFFSET_CHK, value32);
}
#endif //ENABLE_USB_DROP_INCORRECT_OUT, amy,temp remove
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
	pHalData->ReceiveConfig = AAP | APM | AM | AB | CBSSID |CBSSID_BCN | APP_ICV | AMF | HTC_LOC_CTRL | APP_MIC | APP_PHYSTS;

#if (0 == RTL8192C_RX_PACKET_NO_INCLUDE_CRC)
	pHalData->ReceiveConfig |= ACRC32;
#endif

	rtw_write32(Adapter, REG_RCR, pHalData->ReceiveConfig);

	// Accept all multicast address
	rtw_write32(Adapter, REG_MAR, 0xFFFFFFFF);
	rtw_write32(Adapter, REG_MAR+4, 0xFFFFFFFF);

	// Accept all data frames
	//value16 = 0xFFFF;
	//rtw_write16(Adapter, REG_RXFLTMAP2, value16);

	// Accept all management frames
	//value16 = 0xFFFF;
	//rtw_write16(Adapter, REG_RXFLTMAP0, value16);

	//Reject all control frame - default value is 0
	//rtw_write16(Adapter,REG_RXFLTMAP1,0x0);
}

static VOID
_InitAdaptiveCtrl(
	IN  PADAPTER Adapter
	)
{
	u16	value16;
	u32	value32;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);

	// Response Rate Set
	value32 = rtw_read32(Adapter, REG_RRSR);
	value32 &= ~RATE_BITMAP_ALL;
	if(pHalData->CurrentBandType92D == BAND_ON_5G)
	{
		value32 |= RATE_RRSR_WITHOUT_CCK;
	}
	else
	{
		value32 |= RATE_RRSR_CCK_ONLY_1M;
	}
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
	//PHAL_DATA_8192CUSB	pHalData = GetHalData8192CUsb(Adapter);
	u16				value16;

	//disable EDCCA count down, to reduce collison and retry
	value16 = rtw_read16(Adapter, REG_RD_CTRL);
	value16 |= DIS_EDCA_CNT_DWN;
	rtw_write16(Adapter, REG_RD_CTRL, value16);	


	// Update SIFS timing.  ??????????
	//pHalData->SifsTime = 0x0e0e0a0a;
	//Adapter->HalFunc.SetHwRegHandler( Adapter, HW_VAR_SIFS,  (pu1Byte)&pHalData->SifsTime);
	// SIFS for CCK Data ACK
	rtw_write8(Adapter, REG_SIFS_CTX, 0xa);
	// SIFS for CCK consecutive tx like CTS data!
	rtw_write8(Adapter, REG_SIFS_CTX+1, 0xa);
	
	// SIFS for OFDM Data ACK
	rtw_write8(Adapter, REG_SIFS_TRX, 0xe);
	// SIFS for OFDM consecutive tx like CTS data!
	rtw_write8(Adapter, REG_SIFS_TRX+1, 0xe);

	// Set CCK/OFDM SIFS
	rtw_write16(Adapter, REG_SIFS_CTX, 0x0a0a); // CCK SIFS shall always be 10us.
	rtw_write16(Adapter, REG_SIFS_TRX, 0x1010);

	rtw_write16(Adapter, REG_PROT_MODE_CTRL, 0x0204);

	rtw_write32(Adapter, REG_BAR_MODE_CTRL, 0x014004);


	// TXOP
	rtw_write32(Adapter, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtw_write32(Adapter, REG_EDCA_BK_PARAM, 0x0000A44F);
	rtw_write32(Adapter, REG_EDCA_VI_PARAM, 0x005EA324);
	rtw_write32(Adapter, REG_EDCA_VO_PARAM, 0x002FA226);

	// PIFS
	rtw_write8(Adapter, REG_PIFS, 0x1C);
		
	//AGGR BREAK TIME Register
	rtw_write8(Adapter, REG_AGGR_BREAK_TIME, 0x16);

	rtw_write16(Adapter, REG_NAV_PROT_LEN, 0x0040);
	
	rtw_write8(Adapter, REG_BCNDMATIM, 0x02);

	rtw_write8(Adapter, REG_ATIMWND, 0x02);
	
}


static VOID
_InitAMPDUAggregation(
	IN  PADAPTER Adapter
	)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);

	//rtw_write32(Adapter, REG_AGGLEN_LMT, 0x99997631);

	if(pHalData->MacPhyMode92D ==SINGLEMAC_SINGLEPHY)
		rtw_write32(Adapter, REG_AGGLEN_LMT, 0x88728841);
	else if(pHalData->MacPhyMode92D ==DUALMAC_SINGLEPHY)
		rtw_write32(Adapter, REG_AGGLEN_LMT, 0x44444441);
	else if(pHalData->MacPhyMode92D ==DUALMAC_DUALPHY)
		rtw_write32(Adapter, REG_AGGLEN_LMT, 0x66525541);

	rtw_write8(Adapter, REG_AGGR_BREAK_TIME, 0x16);
}

static VOID
_InitBeaconMaxError(
	IN  PADAPTER	Adapter,
	IN	BOOLEAN		InfraMode
	)
{
#ifdef RTL8192CU_ADHOC_WORKAROUND_SETTING
	rtw_write8(Adapter, REG_BCN_MAX_ERR,  0xFF );
#else
	//rtw_write8(Adapter, REG_BCN_MAX_ERR, (InfraMode ? 0xFF : 0x10));
#endif
}

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


static VOID
_InitUsbAggregationSetting(
	IN  PADAPTER Adapter
	)
{
#ifdef CONFIG_USB_TX_AGGREGATION
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32			value32;

	if(Adapter->registrypriv.wifi_spec)
		pHalData->UsbTxAggMode = _FALSE;

	if(pHalData->MacPhyMode92D!=SINGLEMAC_SINGLEPHY)
		pHalData->UsbTxAggDescNum = 2;

	if(pHalData->UsbTxAggMode){
		value32 = rtw_read32(Adapter, REG_TDECTRL);
		value32 = value32 & ~(BLK_DESC_NUM_MASK << BLK_DESC_NUM_SHIFT);
		value32 |= ((pHalData->UsbTxAggDescNum & BLK_DESC_NUM_MASK) << BLK_DESC_NUM_SHIFT);
		
		rtw_write32(Adapter, REG_TDECTRL, value32);
	}
}
#endif

	// Rx aggregation setting
#ifdef CONFIG_USB_RX_AGGREGATION
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8		valueDMA;
	u8		valueUSB;

	if(pHalData->MacPhyMode92D!=SINGLEMAC_SINGLEPHY)
	{
		pHalData->UsbRxAggPageCount	= 24;
		pHalData->UsbRxAggPageTimeout = 0x6;
	}

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
		case USB_RX_AGG_DMA_USB:
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
#if 1
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
		case USB_RX_AGG_DMA_USB:
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
#endif
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

}
#endif

}


static VOID
_InitOperationMode(
	IN	PADAPTER			Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8				regBwOpMode = 0, MinSpaceCfg=0;
	u32				regRATR = 0, regRRSR = 0;

	//1 This part need to modified according to the rate set we filtered!!
	//
	// Set RRSR, RATR, and REG_BWOPMODE registers
	//
	switch(pHalData->CurrentWirelessMode)
	{
		case WIRELESS_MODE_B:
			regBwOpMode = BW_OPMODE_20MHZ;
			regRATR = RATE_ALL_CCK;
			regRRSR = RATE_ALL_CCK;
			break;
		case WIRELESS_MODE_A:
			//RT_ASSERT(FALSE,("Error wireless a mode\n"));
#if 1
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
		case WIRELESS_MODE_UNKNOWN:
		case WIRELESS_MODE_AUTO:
			//if (pHalData->bInHctTest)
			if (0)
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
			//RT_ASSERT(FALSE,("Error wireless mode"));
#if 1
			regBwOpMode = BW_OPMODE_5G;
			regRATR = RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
			regRRSR = RATE_ALL_OFDM_AG;
#endif
			break;
	}

	// Ziv ????????
	//rtw_write32(Adapter, REG_INIRTS_RATE_SEL, regRRSR);
	rtw_write8(Adapter, REG_BWOPMODE, regBwOpMode);

	// For Min Spacing configuration.
	switch(pHalData->rf_type)
	{
		case RF_1T2R:
		case RF_1T1R:
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("Initializeadapter: RF_Type%s\n", (pHalData->RF_Type==RF_1T1R? "(1T1R)":"(1T2R)")));
			MinSpaceCfg = (MAX_MSS_DENSITY_1T<<3);						
			break;
		case RF_2T2R:
		case RF_2T2R_GREEN:
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("Initializeadapter:RF_Type(2T2R)\n"));
			MinSpaceCfg = (MAX_MSS_DENSITY_2T<<3);			
			break;
	}
	
	rtw_write8(Adapter, REG_AMPDU_MIN_SPACE, MinSpaceCfg);

}


static VOID
_InitSecuritySetting(
	IN  PADAPTER Adapter
	)
{
	invalidate_cam_all(Adapter);
}

 static VOID
_InitBeaconParameters(
	IN  PADAPTER Adapter
	)
{
//	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	 //default value  for register 0x558 and 0x559 is  0x05 0x03 (92DU before bitfile0821) zhiyuan 2009/08/26
	rtw_write16(Adapter, REG_TBTT_PROHIBIT,0x3c02);// ms
	rtw_write8(Adapter, REG_DRVERLYINT, 0x05);//ms
	rtw_write8(Adapter, REG_BCNDMATIM, 0x03);

	// TODO: Remove these magic number
	//rtw_write16(Adapter, REG_TBTT_PROHIBIT,0x6404);// ms
	//rtw_write8(Adapter, REG_DRVERLYINT, DRIVER_EARLY_INT_TIME);//ms
	//rtw_write8(Adapter, REG_BCNDMATIM, BCN_DMA_ATIME_INT_TIME);

	// Suggested by designer timchen. Change beacon AIFS to the largest number
	// beacause test chip does not contension before sending beacon. by tynli. 2009.11.03
	//if(IS_NORMAL_CHIP( pHalData->VersionID)){
		rtw_write16(Adapter, REG_BCNTCFG, 0x660F);
	//}
	//else{		
	//	rtw_write16(Adapter, REG_BCNTCFG, 0x66FF);
	//}

}

static VOID
_InitRFType(
	IN	PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

#if (DISABLE_BB_RF==1)
	pHalData->rf_chip	= RF_PSEUDO_11N;
	pHalData->rf_type	= RF_1T1R;// RF_2T2R;
#else

	pHalData->rf_chip	= RF_6052;

	if(pHalData->MacPhyMode92D==DUALMAC_DUALPHY)
	{
		pHalData->rf_type = RF_1T1R;
	}
	else{// SMSP OR DMSP
		pHalData->rf_type = RF_2T2R;
	}
#endif
}

#if RTL8192CU_ADHOC_WORKAROUND_SETTING
static VOID _InitAdhocWorkaroundParams(IN PADAPTER Adapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);	
	pHalData->RegBcnCtrlVal = rtw_read8(Adapter, REG_BCN_CTRL);
	pHalData->RegTxPause = rtw_read8(Adapter, REG_TXPAUSE); 
	pHalData->RegFwHwTxQCtrl = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL+2);
	pHalData->RegReg542 = rtw_read8(Adapter, REG_TBTT_PROHIBIT+2);
	pHalData->RegCR_1 = rtw_read8(Adapter, REG_CR+1);
}
#endif

static VOID
_BeaconFunctionEnable(
	IN	PADAPTER		Adapter,
	IN	BOOLEAN			Enable,
	IN	BOOLEAN			Linked
	)
{
//	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
//	u8			value8 = 0;
#if 0
	value8 = Enable ? (EN_BCN_FUNCTION | EN_TXBCN_RPT) : EN_BCN_FUNCTION;

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
	// 20100901 zhiyuan: Change original setting of BCN_CTRL(0x550) from 
	// 0x1a to 0x1b. Set BIT0 of this register disable ATIM  function. 
	//  enable ATIM function may invoke HW Tx stop operation. This may cause ping failed 
	// sometimes in long run test. So just disable it now.
	// When ATIM function is disabled, High Queue should not use anymore.
	rtw_write8(Adapter, REG_BCN_CTRL, 0x1b);
	rtw_write8(Adapter, REG_RD_CTRL+1, 0x6F);
#endif
}


// Set CCK and OFDM Block "ON"
static VOID _BBTurnOnBlock(
	IN	PADAPTER		Adapter
	)
{
	HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(Adapter);
#if (DISABLE_BB_RF)
	return;
#endif

	if(pHalData->CurrentBandType92D == BAND_ON_5G)
		PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bCCKEn|bOFDMEn, 0x2);
	else
		PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bCCKEn|bOFDMEn, 0x3);
}

static VOID _RfPowerSave(
	IN	PADAPTER		Adapter
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);
	struct pwrctrl_priv		*pwrctrlpriv = &Adapter->pwrctrlpriv;
	u8			eRFPath;

#if (DISABLE_BB_RF)
	return;
#endif

	if(pwrctrlpriv->reg_rfoff == _TRUE){ // User disable RF via registry.
		//RT_TRACE((COMP_INIT|COMP_RF), DBG_LOUD, ("InitializeAdapter8192CUsb(): Turn off RF for RegRfOff.\n"));
		//MgntActSet_RF_State(Adapter, rf_off, RF_CHANGE_BY_SW, _TRUE);
		// Those action will be discard in MgntActSet_RF_State because off the same state
#ifdef CONFIG_DUALMAC_CONCURRENT
		if(pHalData->bSlaveOfDMSP)
			return;
#endif
		for(eRFPath = 0; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
			PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, 0x4, 0xC00, 0x0);
	}
	else if(pwrctrlpriv->rfoff_reason > RF_CHANGE_BY_PS){ // H/W or S/W RF OFF before sleep.
		//RT_TRACE((COMP_INIT|COMP_RF), DBG_LOUD, ("InitializeAdapter8192CUsb(): Turn off RF for RfOffReason(%ld).\n", pMgntInfo->RfOffReason));
		//MgntActSet_RF_State(Adapter, rf_off, pMgntInfo->RfOffReason, _TRUE);
	}
	else{
		pwrctrlpriv->rf_pwrstate = rf_on;
		pwrctrlpriv->rfoff_reason = 0; 
		//if(Adapter->bInSetPower || Adapter->bResetInProgress)
		//	PlatformUsbEnableInPipes(Adapter);
		//RT_TRACE((COMP_INIT|COMP_RF), DBG_LOUD, ("InitializeAdapter8192CUsb(): RF is on.\n"));
	}
}

#ifdef CONFIG_LED
static void _InitHWLed(PADAPTER Adapter)
{
	struct led_priv *pledpriv = &(Adapter->ledpriv);
	
	if( pledpriv->LedStrategy != HW_LED)
			return;
	
// HW led control
// to do .... 
//must consider the cases of antenna diversity/ commbo card/solo card/mini card

}
#endif //CONFIG_LED


u32 rtl8192du_hal_init(_adapter *padapter);
u32 rtl8192du_hal_init(_adapter *padapter)
{
	u8	val8 = 0, tmpU1b;
	u16	val16;
	u32	boundary, i = 0,  status = _SUCCESS;
#if SWLCK == 0
	u32	j;
#endif //SWLCK == 0
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv		*pwrctrlpriv = &padapter->pwrctrlpriv;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
#ifdef CONFIG_DUALMAC_CONCURRENT
	PADAPTER	BuddyAdapter = padapter->pbuddy_adapter;
#endif

_func_enter_;

	padapter->init_adpt_in_progress = _TRUE;

#ifdef CONFIG_DUALMAC_CONCURRENT
	if(BuddyAdapter != NULL)
	{
		if(BuddyAdapter->bHaltInProgress)
		{
			for(i=0;i<100;i++)
			{
				rtw_usleep_os(1000);
				if(!BuddyAdapter->bHaltInProgress)
					break;
			}

			if(i==100)
			{
				DBG_871X("fail to initialization due to another adapter is in halt \n");
				return _FAIL;
			}
		}
	}
#endif
	//RT_TRACE(COMP_INIT, DBG_LOUD, ("--->InitializeAdapter8192CUsb()\n"));

	if(padapter->bSurpriseRemoved)
		return _FAIL;

	//Let the first starting mac load RF parameters and do LCK in this case,by wl
	if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY 
		&& ((pHalData->interfaceIndex == 0 && pHalData->BandSet92D == BAND_ON_2_4G)
			|| (pHalData->interfaceIndex == 1 && pHalData->BandSet92D == BAND_ON_5G)))
		ACQUIRE_GLOBAL_MUTEX(GlobalMutexForMac0_2G_Mac1_5G);

	ACQUIRE_GLOBAL_MUTEX(GlobalMutexForPowerAndEfuse);

	rtw_write8(padapter, REG_RSV_CTRL, 0x0);
	val8 = rtw_read8(padapter, 0x0003);
	val8 &= (~BIT7);
	rtw_write8(padapter, 0x0003, val8);

	//mac status:
	//0x81[4]:0 mac0 off, 1:mac0 on
	//0x82[4]:0 mac1 off, 1: mac1 on.

	//For s3/s4 may reset mac,Reg0xf8 may be set to 0, so reset macphy control reg here.
	PHY_ConfigMacPhyMode92D(padapter);

	PHY_SetPowerOnFor8192D(padapter);

	status = _InitPowerOn(padapter);
	if(status == _FAIL){
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init power on!\n"));
		RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerAndEfuse);
		if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY 
		&& ((pHalData->interfaceIndex == 0 && pHalData->BandSet92D == BAND_ON_2_4G)
			|| (pHalData->interfaceIndex == 1 && pHalData->BandSet92D == BAND_ON_5G)))
			RELEASE_GLOBAL_MUTEX(GlobalMutexForMac0_2G_Mac1_5G);
		goto exit;
	}

	if(!pregistrypriv->wifi_spec){
		boundary = TX_PAGE_BOUNDARY;
	}
	else{// for WMM
		boundary = WMM_NORMAL_TX_PAGE_BOUNDARY;
	}

	PHY_ConfigMacCoexist_RFPage92D(padapter);

	status =  InitLLTTable(padapter, boundary);
	if(status == _FAIL){
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init power on!\n"));
		RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerAndEfuse);
		if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY 
		&& ((pHalData->interfaceIndex == 0 && pHalData->BandSet92D == BAND_ON_2_4G)
			|| (pHalData->interfaceIndex == 1 && pHalData->BandSet92D == BAND_ON_5G)))
			RELEASE_GLOBAL_MUTEX(GlobalMutexForMac0_2G_Mac1_5G);
		return status;
	}

#if ((1 == MP_DRIVER) ||  (0 == FW_PROCESS_VENDOR_CMD))

	rtl8192d_PHY_InitRxSetting(padapter);
	RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerAndEfuse);
	DBG_8192C("%s(): Don't Download Firmware !!\n",__FUNCTION__);
	padapter->bFWReady = _FALSE;
	pHalData->fw_ractrl = _FALSE;

#else

	status = FirmwareDownload92D(padapter);
	RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerAndEfuse);
	if(status == _FAIL){
		padapter->bFWReady = _FALSE;
		pHalData->fw_ractrl = _FALSE;
		DBG_8192C("fw download fail!\n");

		//return fail only when part number check fail,suggested by alex
		if(0xE0 == rtw_read8(padapter, 0x1c5))
		{
			if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY 
			&& ((pHalData->interfaceIndex == 0 && pHalData->BandSet92D == BAND_ON_2_4G)
				|| (pHalData->interfaceIndex == 1 && pHalData->BandSet92D == BAND_ON_5G)))
				RELEASE_GLOBAL_MUTEX(GlobalMutexForMac0_2G_Mac1_5G);	

			goto exit;
		}
	}
	else	{
		padapter->bFWReady = _TRUE;
		pHalData->fw_ractrl = _TRUE;
		DBG_8192C("fw download ok!\n");
	}

#endif

	pHalData->LastHMEBoxNum = 0;

	if(pwrctrlpriv->reg_rfoff == _TRUE){
		pwrctrlpriv->rf_pwrstate = rf_off;
	}

	// Set RF type for BB/RF configuration	
	_InitRFType(padapter);//->_ReadRFType()

	// Save target channel
	// <Roger_Notes> Current Channel will be updated again later.
	//pHalData->CurrentChannel = 6;//default set to 6

#if (HAL_MAC_ENABLE == 1)
	status = PHY_MACConfig8192D(padapter);
	if(status == _FAIL)
	{
		if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY 
		&& ((pHalData->interfaceIndex == 0 && pHalData->BandSet92D == BAND_ON_2_4G)
			|| (pHalData->interfaceIndex == 1 && pHalData->BandSet92D == BAND_ON_5G)))
			RELEASE_GLOBAL_MUTEX(GlobalMutexForMac0_2G_Mac1_5G);
		goto exit;
	}
#endif

	_InitQueueReservedPage(padapter);
	_InitTxBufferBoundary(padapter);		
	_InitQueuePriority(padapter);
	_InitTransferPageSize(padapter);

	// Get Rx PHY status in order to report RSSI and others.
	_InitDriverInfoSize(padapter, DRVINFO_SZ);

	_InitInterrupt(padapter);	
	_InitID(padapter);//set mac_address
	_InitNetworkType(padapter);//set msr	
	_InitWMACSetting(padapter);
	_InitAdaptiveCtrl(padapter);
	_InitEDCA(padapter);
	_InitRateFallback(padapter);
	_InitRetryFunction(padapter);
	_InitUsbAggregationSetting(padapter);
	_InitOperationMode(padapter);//todo
	_InitBeaconParameters(padapter);
	_InitAMPDUAggregation(padapter);
	_InitBeaconMaxError(padapter, _TRUE);
	
#if defined(CONFIG_CONCURRENT_MODE) || defined(CONFIG_TX_MCAST2UNI)

#ifdef CONFIG_CHECK_AC_LIFETIME
	// Enable lifetime check for the four ACs
	//rtw_write8(padapter, REG_LIFETIME_EN, 0x0F);
#endif	// CONFIG_CHECK_AC_LIFETIME

#ifdef CONFIG_TX_MCAST2UNI
	rtw_write16(padapter, REG_PKT_VO_VI_LIFE_TIME, 0x0400);	// unit: 256us. 256ms
	rtw_write16(padapter, REG_PKT_BE_BK_LIFE_TIME, 0x0400);	// unit: 256us. 256ms
#else	// CONFIG_TX_MCAST2UNI
	rtw_write16(padapter, REG_PKT_VO_VI_LIFE_TIME, 0x3000);	// unit: 256us. 3s
	rtw_write16(padapter, REG_PKT_BE_BK_LIFE_TIME, 0x3000);	// unit: 256us. 3s
#endif	// CONFIG_TX_MCAST2UNI
#endif	// CONFIG_CONCURRENT_MODE || CONFIG_TX_MCAST2UNI
	
	#ifdef CONFIG_LED
	_InitHWLed(padapter);
	#endif

#if ENABLE_USB_DROP_INCORRECT_OUT
	_InitHardwareDropIncorrectBulkOut(padapter);
#endif
	if(pHalData->bRDGEnable){
		_InitRDGSetting(padapter);
	}

	// Set Data Auto Rate Fallback Reg.
	for(i = 0 ; i < 4 ; i++){
		rtw_write32(padapter, REG_ARFR0+i*4, 0x1f8ffff0);
	}

	if(pregistrypriv->wifi_spec)
	{
		rtw_write16(padapter, REG_FAST_EDCA_CTRL, 0);
	}
	else{
		if(pHalData->MacPhyMode92D==SINGLEMAC_SINGLEPHY){
			if(pHalData->OutEpNumber == 2)  // suggested by chunchu
				rtw_write32(padapter, REG_FAST_EDCA_CTRL, 0x03066666);
		       else
				rtw_write16(padapter, REG_FAST_EDCA_CTRL, 0x8888);
		}
		else{
			rtw_write16(padapter, REG_FAST_EDCA_CTRL, 0x5555);
		}
	}

	tmpU1b=rtw_read8(padapter, 0x605);
	tmpU1b|=0xf0;
	rtw_write8(padapter, 0x605,tmpU1b);	
	rtw_write8(padapter, 0x55e,0x30);
	rtw_write8(padapter, 0x55f,0x30);
	rtw_write8(padapter, 0x606,0x30);
	//PlatformEFIOWrite1Byte(Adapter, 0x5e4,0x38);	  // masked for new bitfile

	//for bitfile 0912/0923 zhiyuan 2009/09/23
	// temp for high queue and mgnt Queue corrupt in time;
	//it may cause hang when sw beacon use high_Q,other frame use mgnt_Q; or ,sw beacon use mgnt_Q ,other frame use high_Q;
	rtw_write8(padapter, 0x523, 0x10);
	val16 = rtw_read16(padapter, 0x524);
	val16|=BIT12;
	rtw_write16(padapter,0x524 , val16);

	rtw_write8(padapter,REG_TXPAUSE, 0);

	// suggested by zhouzhou   usb suspend  idle time count for bitfile0927  2009/10/09 zhiyuan
	val8=rtw_read8(padapter, 0xfe56);
	val8 |=(BIT0|BIT1);
	rtw_write8(padapter, 0xfe56, val8);

	if(pHalData->bEarlyModeEnable)
	{
		DBG_8192C("EarlyMode Enabled!!!\n");

		tmpU1b = rtw_read8(padapter,0x4d0);
		tmpU1b = tmpU1b|0x1f;
		rtw_write8(padapter,0x4d0,tmpU1b);

		rtw_write8(padapter,0x4d3,0x80);

		tmpU1b = rtw_read8(padapter,0x605);
		tmpU1b = tmpU1b|0x40;
		rtw_write8(padapter,0x605,tmpU1b);
	}
	else
	{
		rtw_write8(padapter,0x4d0,0);
	}

	//
	//d. Initialize BB related configurations.
	//
#if (HAL_BB_ENABLE == 1)
	status = PHY_BBConfig8192D(padapter);
	if(status == _FAIL)
	{
		if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY 
		&& ((pHalData->interfaceIndex == 0 && pHalData->BandSet92D == BAND_ON_2_4G)
			|| (pHalData->interfaceIndex == 1 && pHalData->BandSet92D == BAND_ON_5G)))
			RELEASE_GLOBAL_MUTEX(GlobalMutexForMac0_2G_Mac1_5G);
		goto exit;
	}
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	if(pHalData->bSlaveOfDMSP)
	{
		DBG_871X("slave of dmsp close phy1 \n");
		PHY_StopTRXBeforeChangeBand8192D(padapter);
	}
#endif

	if(padapter->bFWReady && pHalData->FirmwareVersion >= 0x13)
	{
		pHalData->bReadRFbyFW = _TRUE;
		DBG_871X("Enable 92du query RF by FW.\n");
	}
	else
	{
		pHalData->bReadRFbyFW = _FALSE;
	}

	// 92CU use 3-wire to r/w RF
	//
	// e. Initialize RF related configurations.
	//
	// 2007/11/02 MH Before initalizing RF. We can not use FW to do RF-R/W.
	//pHalData->Rf_Mode = RF_OP_By_SW_3wire;
#if (HAL_RF_ENABLE == 1)
	// set before initialize RF, 
	PHY_SetBBReg(padapter, rFPGA0_AnalogParameter4, 0x00f00000,  0xf);

	status = PHY_RFConfig8192D(padapter);	
	if(status == _FAIL)
	{
		if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY 
		&& ((pHalData->interfaceIndex == 0 && pHalData->BandSet92D == BAND_ON_2_4G)
			|| (pHalData->interfaceIndex == 1 && pHalData->BandSet92D == BAND_ON_5G)))
			RELEASE_GLOBAL_MUTEX(GlobalMutexForMac0_2G_Mac1_5G);
		goto exit;
	}

	// set default value after initialize RF, 
	PHY_SetBBReg(padapter, rFPGA0_AnalogParameter4, 0x00f00000,  0);

#ifdef CONFIG_DUALMAC_CONCURRENT
	if(!pHalData->bSlaveOfDMSP)
#endif
		PHY_UpdateBBRFConfiguration8192D(padapter, _FALSE);

#endif

#if RTL8192CU_ADHOC_WORKAROUND_SETTING
	_InitAdhocWorkaroundParams(padapter);
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	if(!pHalData->bSlaveOfDMSP)
#endif
		_BBTurnOnBlock(padapter);

	//_InitID(padapter);
	//NicIFSetMacAddress(padapter, padapter->PermanentAddress);

	if(pHalData->CurrentBandType92D == BAND_ON_5G)
	{
		pHalData->CurrentWirelessMode = WIRELESS_MODE_N_5G;
	}
	else
	{
		pHalData->CurrentWirelessMode = WIRELESS_MODE_N_24G;
	}

	_InitSecuritySetting(padapter);
	_RfPowerSave(padapter);

	// HW SEQ CTRL
	//set 0x0 to 0xFF by tynli. Default enable HW SEQ NUM.
	rtw_write8(padapter,REG_HWSEQ_CTRL, 0xFF); 

	//schmitt trigger ,improve tx evm for 92du,suggested by ynlin  12/03/2010
	tmpU1b = rtw_read8(padapter, REG_AFE_XTAL_CTRL);
	tmpU1b |=BIT1;
	rtw_write8(padapter, REG_AFE_XTAL_CTRL, tmpU1b);

	//disable bar
	rtw_write32(padapter, REG_BAR_MODE_CTRL, 0xffff);

	//Nav limit , suggest by scott
	rtw_write8(padapter, 0x652, 0x0);
	rtw_write8(padapter, 0xc87, 0x50);//suggest by Jackson for CCA

#if (MP_DRIVER == 1)
	padapter->mppriv.channel = pHalData->CurrentChannel;
	MPT_InitializeAdapter(padapter, padapter->mppriv.channel);
	//MPT_InitializeAdapter(padapter, Channel);
#else // temply marked this for RF
#ifdef CONFIG_DUALMAC_CONCURRENT
	if(!pHalData->bSlaveOfDMSP)
#endif
	{
		// do IQK for 2.4G for better scan result, if current bandtype is 2.4G.
		if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
			rtl8192d_PHY_IQCalibrate(padapter);
	
		rtl8192d_dm_CheckTXPowerTracking(padapter);
		rtl8192d_PHY_LCCalibrate(padapter);
		if(pHalData->MacPhyMode92D == DUALMAC_DUALPHY 
		&& ((pHalData->interfaceIndex == 0 && pHalData->BandSet92D == BAND_ON_2_4G)
			|| (pHalData->interfaceIndex == 1 && pHalData->BandSet92D == BAND_ON_5G)))
			RELEASE_GLOBAL_MUTEX(GlobalMutexForMac0_2G_Mac1_5G);
		//5G and 2.4G must wait sometime to let RF LO ready
		//by sherry 2010.06.28
#if SWLCK == 0	
		{
			u32 tmpRega, tmpRegb;
			for(j=0;j<10000;j++)
			{
				rtw_udelay_os(MAX_STALL_TIME);
                           	if(pHalData->rf_type == RF_1T1R)
				{
					tmpRega = PHY_QueryRFReg(padapter, (RF_RADIO_PATH_E)RF_PATH_A, 0x2a, bMaskDWord);
					if((tmpRega&BIT11)==BIT11)
						break;	
				}
				else
				{
					tmpRega = PHY_QueryRFReg(padapter, (RF_RADIO_PATH_E)RF_PATH_A, 0x2a, bMaskDWord);
					tmpRegb = PHY_QueryRFReg(padapter, (RF_RADIO_PATH_E)RF_PATH_B, 0x2a, bMaskDWord);				
					if(((tmpRega&BIT11)==BIT11)&&((tmpRegb&BIT11)==BIT11))
						break;
					// temply add for DMSP
					if(pHalData->MacPhyMode92D==DUALMAC_SINGLEPHY&&(pHalData->interfaceIndex!=0))
						break;
				}
			}
		}
#endif
	}
#endif

	PHY_InitPABias92D(padapter);

	rtl8192d_InitHalDm(padapter);

	padapter->init_adpt_in_progress = _FALSE;

	//misc
	{
		int i;		
		u8 mac_addr[6];
		for(i=0; i<6; i++)
		{			
#ifdef CONFIG_CONCURRENT_MODE
			if(padapter->iface_type == IFACE_PORT1)		
				mac_addr[i] = rtw_read8(padapter, REG_MACID1+i);
			else
#endif			
			mac_addr[i] = rtw_read8(padapter, REG_MACID+i);		
		}
		
		DBG_8192C("MAC Address from REG_MACID = "MAC_FMT"\n", MAC_ARG(mac_addr));
	}

	{	
		//pHalData->LoopbackMode=LOOPBACK_MODE;
		//if(padapter->ResetProgress == RESET_TYPE_NORESET)
		//{
		       u32					ulRegRead;
			//3//
			//3//Set Loopback mode or Normal mode
			//3//
			//2006.12.13 by emily. Note!We should not merge these two CPU_GEN register writings 
			//	because setting of System_Reset bit reset MAC to default transmission mode.
			ulRegRead = rtw_read32(padapter, 0x100);	//CPU_GEN  0x100

			//if(pHalData->LoopbackMode == RTL8192SU_NO_LOOPBACK)
			{
				ulRegRead |= ulRegRead ;
			}
			//else if (pHalData->LoopbackMode == RTL8192SU_MAC_LOOPBACK )
			//{
				//RT_TRACE(COMP_INIT, DBG_LOUD, ("==>start loop back mode %x\n",ulRegRead));
			//	ulRegRead |= 0x0b000000; //0x0b000000 CPU_CCK_LOOPBACK;
			//}
			//else if(pHalData->LoopbackMode == RTL8192SU_DMA_LOOPBACK)
			//{
				//RT_TRACE(COMP_INIT, DBG_LOUD, ("==>start dule mac loop back mode %x\n",ulRegRead));
			//	ulRegRead |= 0x07000000; //0x07000000 CPU_CCK_LOOPBACK;
			//}
			//else
			//{
				//RT_ASSERT(FALSE, ("Serious error: wrong loopback mode setting\n"));	
			//}

			rtw_write32(padapter, 0x100, ulRegRead);
	              //RT_TRACE(COMP_INIT,DBG_LOUD,("==>loop back mode CPU GEN value:%x\n",ulRegRead));

			// 2006.11.29. After reset cpu, we sholud wait for a second, otherwise, it may fail to write registers. Emily
			rtw_udelay_os(500);
		//}
	}

	RT_CLEAR_PS_LEVEL(pwrctrlpriv, RT_RF_OFF_LEVL_HALT_NIC);


	if((pregistrypriv->lowrate_two_xmit) && (pHalData->MacPhyMode92D != DUALMAC_DUALPHY))
	{		
		//for Use 2 path Tx to transmit MCS0~7 and legacy mode
		//Reg90C[30]=1'b0 (OFDM TX by Reg, default PHY parameter)
		//Reg80C[31]=1'b0 (CCK TX by Reg, default PHYparameter)
		//RegC8C=0xa0e40000 (OFDM RX weighting)
		rtw_write32(padapter, 0x90C, rtw_read32(padapter, 0x90C)&(~BIT(30)));
		rtw_write32(padapter, 0x80C, rtw_read32(padapter, 0x80C)&(~BIT(31)));	
		rtw_write32(padapter, 0xC8C, 0xa0e40000);
	}

#if (RTL8192D_EASY_SMART_CONCURRENT == 1)
	if((Adapter->bMasterOfDMSP) && (!GET_POWER_SAVE_CONTROL(pMgntInfo)->bSwRfProcessing))
		PlatformSetTimer(Adapter, &pHalData->DualMacSetOperationForAnotherMacTimer, 2000);
#if 0
	if(IS_HARDWARE_TYPE_8192D(Adapter))
	{
		DualMacEasyConcurrentInit(Adapter);
	}
#endif
#endif

exit:

_func_exit_;

	return status;
}


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
	rtw_write16(Adapter, REG_LEDCFG0, 0x8888);

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("======> Disable GPIO and LED.\n"));
 
} //end of _DisableGPIO()
#ifndef PLATFORM_FREEBSD //amy, temp remove
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
#endif //amy, temp remove
static VOID
_DisableRFAFEAndResetBB8192D(
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
       HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	eRFPath = 0,value8 = 0;

	PHY_SetBBReg(Adapter, rFPGA0_AnalogParameter4, 0x00f00000,  0xf);
	PHY_SetRFReg(Adapter, (RF_RADIO_PATH_E)eRFPath, 0x0,bRFRegOffsetMask, 0x0);

	value8 |= APSDOFF;
	rtw_write8(Adapter, REG_APSD_CTRL, value8);//0x40


	//testchip  should not do BB reset if another mac is alive;
	value8 = 0 ; 
	value8 |=( FEN_USBD | FEN_USBA | FEN_BB_GLB_RSTn);
	rtw_write8(Adapter, REG_SYS_FUNC_EN,value8 );//0x16

	if(pHalData->MacPhyMode92D!=SINGLEMAC_SINGLEPHY)
	{
		if(pHalData->interfaceIndex!=0){
			value8 &=( ~FEN_BB_GLB_RSTn );
			rtw_write8(Adapter, REG_SYS_FUNC_EN, value8); //0x14	
		}
	}
	else{
		value8 &=( ~FEN_BB_GLB_RSTn );
		rtw_write8(Adapter, REG_SYS_FUNC_EN, value8); //0x14	
	}

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("======> RF off and reset BB.\n"));
}

static VOID
_DisableRFAFEAndResetBB(
	IN PADAPTER			Adapter
	)
{
	_DisableRFAFEAndResetBB8192D(Adapter);
}

static VOID
_ResetDigitalProcedure1(
	IN 	PADAPTER			Adapter,
	IN	BOOLEAN				bWithoutHWSM	
	)
{

	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);

	if((pHalData->FirmwareVersion <=  0x20)&&(_FALSE)){
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
		// 2010/08/12 MH For USB SS, we can not stop 8051 when we are trying to
		// enter IPS/HW&SW radio off. For S3/S4/S5/Disable, we can stop 8051 because
		// we will init FW when power on again.
		//if(!pDevice->RegUsbSS)
		{	// If we want to SS mode, we can not reset 8051.
			if(rtw_read8(Adapter, REG_MCUFWDL) & BIT1)
			{ //IF fw in RAM code, do reset 
				
				if(Adapter->bFWReady)
				{
					rtw_write8(Adapter, REG_FSIMR, 0x00);
					// 2010/08/25 MH Accordign to RD alfred's suggestion, we need to disable other
					// HRCV INT to influence 8051 reset.
					rtw_write8(Adapter, REG_FWIMR, 0x20);
					// 2011/02/15 MH According to Alex's suggestion, close mask to prevent incorrect FW write operation.
					rtw_write8(Adapter, REG_FTIMR, 0x00);
					
					rtw_write8(Adapter, REG_MCUFWDL, 0);
					rtw_write8(Adapter, REG_HMETFR+3, 0x20);//8051 reset by self
					
					while( (retry_cnts++ <100) && (FEN_CPUEN &rtw_read16(Adapter, REG_SYS_FUNC_EN)))
					{
						rtw_udelay_os(50);//us
					}
					//RT_ASSERT((retry_cnts < 100), ("8051 reset failed!\n"));			

					if (retry_cnts>= 100)
					{
						rtw_write8(Adapter, REG_FWIMR, 0x00);
						// 2010/08/31 MH According to Filen's info, if 8051 reset fail, reset MAC directly.
						rtw_write8(Adapter, REG_SYS_FUNC_EN+1, 0x50);	//Reset MAC and Enable 8051
						rtw_mdelay_os(10);
					}
					else	
					{
						DBG_8192C("=====> 8051 reset success (%d) .\n",retry_cnts);
					}
				}
			}
			else
			{
				DBG_8192C("=====> 8051 in ROM.\n");
			}			

			#ifdef DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE
			{
				u8 val;
				if( (val=rtw_read8(Adapter, REG_MCUFWDL)))
					DBG_871X("DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE %s:%d REG_MCUFWDL:0x%02x\n", __FUNCTION__, __LINE__, val);
			}
			#endif
			
			rtw_write8(Adapter, REG_SYS_FUNC_EN+1, 0x54);	//Reset MAC and Enable 8051
			rtw_write8(Adapter, REG_MCUFWDL, 0);

		}
	}

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
	u32 value16 = 0;
	u8 value8=0;
	
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
	rtw_write8(Adapter, REG_SPS0_CTRL, 0x23);
	
	value16 |= (APDM_HOST | AFSM_HSUS |PFM_ALDN);
	rtw_write16(Adapter, REG_APS_FSMCO,value16 );//0x4802 

	rtw_write8(Adapter, REG_RSV_CTRL, 0x0e);

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("======> Disable Analog Reg0x04:0x%04x.\n",value16));
}

#ifndef PLATFORM_FREEBSD //amy, temp remove
static BOOLEAN
CanGotoPowerOff92D(
	IN	PADAPTER			Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8 u1bTmp;
#ifdef CONFIG_DUALMAC_CONCURRENT
	PADAPTER	BuddyAdapter = Adapter->pbuddy_adapter;
#endif

	if(pHalData->MacPhyMode92D==SINGLEMAC_SINGLEPHY)
		return _TRUE;	  
	
#ifdef CONFIG_DUALMAC_CONCURRENT
	if(BuddyAdapter != NULL)
	{
		if(BuddyAdapter->init_adpt_in_progress)
		{
			DBG_871X("do not power off during another adapter is initialization \n");
			return _FALSE;
		}
	}
#endif

	if(pHalData->interfaceIndex==0)
	{	// query another mac status;
		u1bTmp = rtw_read8(Adapter, REG_MAC1);
		u1bTmp&=MAC1_ON;
	}
	else
	{
		u1bTmp = rtw_read8(Adapter, REG_MAC0);
		u1bTmp&=MAC0_ON;
	}

	//0x17[7]:1b' power off in process
	u1bTmp=rtw_read8(Adapter, 0x17);
	u1bTmp|=BIT7;
	rtw_write8(Adapter, 0x17, u1bTmp);

	rtw_udelay_os(500);
	// query another mac status;
	if(pHalData->interfaceIndex==0)
	{	// query another mac status;
		u1bTmp = rtw_read8(Adapter, REG_MAC1);
		u1bTmp&=MAC1_ON;
	}
	else
	{
		u1bTmp = rtw_read8(Adapter, REG_MAC0);
		u1bTmp&=MAC0_ON;
	}
	//if another mac is alive,do not do power off 
	if(u1bTmp)
	{
		u1bTmp=rtw_read8(Adapter, 0x17);
		u1bTmp&=(~BIT7);
		rtw_write8(Adapter, 0x17, u1bTmp);
		return _FALSE;
	}
	return _TRUE;
}
#endif //amy, temp remove

static int	
CardDisableHWSM( // HW Auto state machine
	IN	PADAPTER		Adapter,
	IN	BOOLEAN			resetMCU
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	int		rtStatus = _SUCCESS;
	u8		value;
	
	if(Adapter->bSurpriseRemoved){
		return rtStatus;
	}

	rtw_write8(Adapter, REG_TXPAUSE, 0xFF);
	rtw_udelay_os(500); 
	rtw_write8(Adapter,	REG_CR, 0x0);

	//==== RF Off Sequence ====
#ifdef CONFIG_DUALMAC_CONCURRENT
	if(!pHalData->bSlaveOfDMSP || Adapter->DualMacConcurrent == _FALSE)
#endif
		_DisableRFAFEAndResetBB(Adapter);

	if(!PHY_CheckPowerOffFor8192D(Adapter))
		return rtStatus;

	//0x20:value 05-->04
	rtw_write8(Adapter, REG_LDOA15_CTRL,0x04);
      	//RF Control 
	rtw_write8(Adapter, REG_RF_CTRL,0);

	//  ==== Reset digital sequence   ======
	_ResetDigitalProcedure1(Adapter, _FALSE);
	
	//  ==== Pull GPIO PIN to balance level and LED control ======
	_DisableGPIO(Adapter);

	//  ==== Disable analog sequence ===
	_DisableAnalog(Adapter, _FALSE);

	ACQUIRE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);
	value=rtw_read8(Adapter, REG_POWER_OFF_IN_PROCESS);
	value&=(~BIT7);
	rtw_write8(Adapter, REG_POWER_OFF_IN_PROCESS, value);
	RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("======> Card disable finished.\n"));

	return rtStatus;
}

static int	
CardDisableWithoutHWSM( // without HW Auto state machine
	IN	PADAPTER		Adapter	
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	int		rtStatus = _SUCCESS;
	u8		value;

	if(Adapter->bSurpriseRemoved){
		return rtStatus;
	}

	rtw_write8(Adapter, REG_TXPAUSE, 0xFF);
	rtw_udelay_os(500); 
	rtw_write8(Adapter,	REG_CR, 0x0);

	//==== RF Off Sequence ====
#ifdef CONFIG_DUALMAC_CONCURRENT
	if(!pHalData->bSlaveOfDMSP || Adapter->DualMacConcurrent == _FALSE)
#endif	
		_DisableRFAFEAndResetBB(Adapter);

	// stop tx/rx 
	rtw_write8(Adapter, REG_TXPAUSE, 0xFF);
	rtw_udelay_os(500); 
	rtw_write8(Adapter,	REG_CR, 0x0);

	if(!PHY_CheckPowerOffFor8192D(Adapter))
	{
		return rtStatus;
	}

	//0x20:value 05-->04
	rtw_write8(Adapter, REG_LDOA15_CTRL,0x04);
      	//RF Control 
	rtw_write8(Adapter, REG_RF_CTRL,0);

	//  ==== Reset digital sequence   ======
#ifdef CONFIG_DUALMAC_CONCURRENT
	_ResetDigitalProcedure1(Adapter, _FALSE);
#else
	_ResetDigitalProcedure1(Adapter,_TRUE);
#endif

	//  ==== Pull GPIO PIN to balance level and LED control ======
	_DisableGPIO(Adapter);

	//  ==== Reset digital sequence   ======
	_ResetDigitalProcedure2(Adapter);

	//  ==== Disable analog sequence ===
#ifdef CONFIG_DUALMAC_CONCURRENT
	_DisableAnalog(Adapter,_FALSE);
#else
	_DisableAnalog(Adapter,_TRUE);
#endif

	ACQUIRE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);
	value=rtw_read8(Adapter, REG_POWER_OFF_IN_PROCESS);
	value&=(~BIT7);
	rtw_write8(Adapter, REG_POWER_OFF_IN_PROCESS, value);
	RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("<====== Card Disable Without HWSM .\n"));
	return rtStatus;
}

u32 rtl8192du_hal_deinit(_adapter *padapter);
u32 rtl8192du_hal_deinit(_adapter *padapter)
 {
	u8	u1bTmp;
	u8	OpMode;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv	*pwrpriv = &padapter->pwrctrlpriv;

_func_enter_;

	if(RT_IN_PS_LEVEL(pwrpriv, RT_RF_OFF_LEVL_HALT_NIC))
	{
		DBG_8192C("HaltAdapter8192DUsb(): Not to haltadapter if HW already halt\n");
		return _FAIL;
	}

	padapter->bHaltInProgress = _TRUE;

	OpMode = 0;
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_MEDIA_STATUS, (u8 *)(&OpMode));

#if 0
	if(pHalData->interfaceIndex == 0){
		u1bTmp = rtw_read8(padapter, REG_MAC0);
		rtw_write8(padapter, REG_MAC0, u1bTmp&(~MAC0_ON));
	}
	else{
		u1bTmp = rtw_read8(padapter, REG_MAC1);
		rtw_write8(padapter, REG_MAC1, u1bTmp&(~MAC1_ON));
	}
#endif

	if(/*Adapter->bInUsbIfTest ||*/ !pHalData->bSupportRemoteWakeUp){
		if( padapter->bCardDisableWOHSM == _FALSE)
			CardDisableHWSM(padapter, _FALSE);
		else
			CardDisableWithoutHWSM(padapter);
	}
	else{

		// Wake on WLAN
	}

	if(pHalData->bInSetPower)
	{
		//0xFE10[4] clear before suspend	 suggested by zhouzhou
		u1bTmp=rtw_read8(padapter,0xfe10);
		u1bTmp&=(~BIT4);
		rtw_write8(padapter,0xfe10,u1bTmp);
	}

	RT_SET_PS_LEVEL(pwrpriv, RT_RF_OFF_LEVL_HALT_NIC);

	rtw_led_control(padapter, LED_CTL_POWER_OFF);

	padapter->bHaltInProgress = _FALSE;

_func_exit_;
	
	return _SUCCESS;
 }

unsigned int rtl8192du_inirp_init(_adapter * padapter);
unsigned int rtl8192du_inirp_init(_adapter * padapter)
{	
	u8 i;	
	struct recv_buf *precvbuf;
	uint	status;
	struct intf_hdl * pintfhdl=&padapter->iopriv.intf;
	struct recv_priv *precvpriv = &(padapter->recvpriv);
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

unsigned int rtl8192du_inirp_deinit(_adapter * padapter);
unsigned int rtl8192du_inirp_deinit(_adapter * padapter)
{	
	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("\n ===> usb_rx_deinit \n"));
	
	rtw_read_port_cancel(padapter);


	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("\n <=== usb_rx_deinit \n"));

	return _SUCCESS;
}

//-------------------------------------------------------------------
//
//	EEPROM/EFUSE Content Parsing
//
//-------------------------------------------------------------------

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

u32
_GetChannelGroup(
	IN	u32	channel
	);
u32
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

	//	Decide CustomerID according to VID/DID or EEPROM
	switch(pHalData->EEPROMCustomerID)
	{
		case EEPROM_CID_WHQL:
			//Adapter->bInHctTest = _TRUE;

			//pMgntInfo->bSupportTurboMode = _FALSE;
			//pMgntInfo->bAutoTurboBy8186 = _FALSE;

			//pMgntInfo->PowerSaveControl.bInactivePs = _FALSE;
			//pMgntInfo->PowerSaveControl.bIPSModeBackup = _FALSE;
			//pMgntInfo->PowerSaveControl.bLeisurePs = _FALSE;
			//pMgntInfo->keepAliveLevel = 0;

			//Adapter->bUnloadDriverwhenS3S4 = _FALSE;
			break;			
		default:
			pHalData->CustomerID = RT_CID_DEFAULT;
			break;
			
	}

	MSG_8192C("EEPROMVID = 0x%04x\n", pHalData->EEPROMVID);
	MSG_8192C("EEPROMPID = 0x%04x\n", pHalData->EEPROMPID);
	MSG_8192C("EEPROMCustomerID : 0x%02x\n", pHalData->EEPROMCustomerID);
	MSG_8192C("EEPROMSubCustomerID: 0x%02x\n", pHalData->EEPROMSubCustomerID);
}

static VOID
_ReadMACAddress(
	IN	PADAPTER	Adapter,	
	IN	u8*		PROMContent,
	IN	BOOLEAN		AutoloadFail
	)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	EEPROM_EFUSE_PRIV	*pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);

	// Dual MAC should assign diffrent MAC address ,or, it is wil cause hang in single phy mode  zhiyuan 04/07/2010
	//Temply random assigh mac address for  efuse mac address not ready now
	if(AutoloadFail == _FALSE  ){
		if(pHalData->interfaceIndex == 0){
			//change to use memcpy, in order to avoid alignment issue. Baron 2011/6/20
			_rtw_memcpy(&pEEPROM->mac_addr, &PROMContent[EEPROM_MAC_ADDR_MAC0_92D], ETH_ALEN);
		}
		else{
			//change to use memcpy, in order to avoid alignment issue. Baron 2011/6/20
			_rtw_memcpy(&pEEPROM->mac_addr, &PROMContent[EEPROM_MAC_ADDR_MAC1_92D], ETH_ALEN);
		}

		if(is_broadcast_mac_addr(pEEPROM->mac_addr) || is_multicast_mac_addr(pEEPROM->mac_addr))
		{
			//Random assigh MAC address
			u8 sMacAddr[MAC_ADDR_LEN] = {0x00, 0xE0, 0x4C, 0x81, 0x92, 0x00};
			//u32	curtime = rtw_get_current_time();
			if(pHalData->interfaceIndex == 1){
				sMacAddr[5] = 0x01;
				//sMacAddr[5] = (u8)(curtime & 0xff);
				//sMacAddr[5] = (u8)GetRandomNumber(1, 254);
			}
			_rtw_memcpy(pEEPROM->mac_addr, sMacAddr, ETH_ALEN);
		}
	}
	else
	{
		//Random assigh MAC address
		u8 sMacAddr[MAC_ADDR_LEN] = {0x00, 0xE0, 0x4C, 0x81, 0x92, 0x00};
		//u32	curtime = rtw_get_current_time();
		if(pHalData->interfaceIndex == 1){
			sMacAddr[5] = 0x01;
			//sMacAddr[5] = (u8)(curtime & 0xff);
			//sMacAddr[5] = (u8)GetRandomNumber(1, 254);
		}
		_rtw_memcpy(pEEPROM->mac_addr, sMacAddr, ETH_ALEN);
	}
	
	//NicIFSetMacAddress(Adapter, Adapter->PermanentAddress);
	//RT_PRINT_ADDR(COMP_INIT|COMP_EFUSE, DBG_LOUD, "MAC Addr: %s", Adapter->PermanentAddress);
	DBG_8192C("MAC Address from EFUSE = "MAC_FMT"\n", MAC_ARG(pEEPROM->mac_addr));
}


static VOID
hal_ReadMacPhyModeFromPROM92DU(
	IN	PADAPTER	Adapter,	
	IN	u8*		PROMContent
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	MacPhyCrValue = 0;
#ifdef CONFIG_DUALMAC_CONCURRENT
	//PADAPTER	BuddyAdapter = Adapter->BuddyAdapter;
#endif
	

	MacPhyCrValue=PROMContent[EEPROM_ENDPOINT_SETTING];
	if(MacPhyCrValue & BIT0)
	{
#ifdef CONFIG_DUALMAC_CONCURRENT
		pHalData->MacPhyMode92D = DUALMAC_SINGLEPHY;
		Adapter->DualMacConcurrent = _TRUE;
#else
		pHalData->MacPhyMode92D = DUALMAC_DUALPHY;
		DBG_8192C("hal_ReadMacPhyModeFromPROM92DU:: MacPhyMode DUALMAC_DUALPHY \n");
#endif
	}
	else
	{
		pHalData->MacPhyMode92D = SINGLEMAC_SINGLEPHY;
	}

	DBG_8192C("_ReadMacPhyModeFromPROM92DU(): MacPhyCrValue %d \n", MacPhyCrValue);
	
}

static VOID
hal_ReadMacPhyMode_92D(
	IN	PADAPTER	Adapter,	
	IN	u8			*PROMContent,
	IN	BOOLEAN		AutoloadFail
)
{
#ifdef CONFIG_DUALMAC_CONCURRENT
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
#endif //CONFIG_DUALMAC_CONCURRENT
	u8	Mac1EnableValue = 0;


	
	if(AutoloadFail==_TRUE){
		Mac1EnableValue = rtw_read8(Adapter,0xFE64);
		PHY_ReadMacPhyMode92D(Adapter, AutoloadFail);

		DBG_8192C("_ReadMacPhyMode(): AutoloadFail %d 0xFE64 = 0x%x \n",AutoloadFail, Mac1EnableValue);
	}
	else{
		hal_ReadMacPhyModeFromPROM92DU(Adapter, PROMContent);
	}

#ifdef CONFIG_DUALMAC_CONCURRENT
//SMSP-->DMDP/DMSP wait for another adapter compeletes mode switc
	//CheckInModeSwitchProcess(Adapter);

//get Dual Mac Mode from 0x2C for test chip and 0xF8 for normal chip
	ACQUIRE_GLOBAL_MUTEX(GlobalCounterForMutex);
	if(GlobalFirstConfigurationForNormalChip)
	{
		RELEASE_GLOBAL_MUTEX(GlobalCounterForMutex);			
		PHY_ConfigMacPhyMode92D(Adapter);
		ACQUIRE_GLOBAL_MUTEX(GlobalCounterForMutex);
		GlobalFirstConfigurationForNormalChip = _FALSE;
		RELEASE_GLOBAL_MUTEX(GlobalCounterForMutex);				
	}
	else
	{
		RELEASE_GLOBAL_MUTEX(GlobalCounterForMutex);			
		PHY_ReadMacPhyMode92D(Adapter, AutoloadFail);
	}
#else
	PHY_ConfigMacPhyMode92D(Adapter);
#endif

	PHY_ConfigMacPhyModeInfo92D(Adapter);
	rtl8192d_ResetDualMacSwitchVariables(Adapter);

}

static VOID
_ReadBoardType(
	IN	PADAPTER	Adapter,	
	IN	u8*		PROMContent,
	IN	BOOLEAN		AutoloadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			boardType;

	if(AutoloadFail){
		pHalData->rf_type = RF_2T2R;
		pHalData->BluetoothCoexist = _FALSE;
		return;
	}

	boardType = PROMContent[EEPROM_NORMAL_BoardType];
	boardType &= BOARD_TYPE_NORMAL_MASK;
	boardType >>= 5;

#if 0
	switch(boardType & 0xF)
	{
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
			pHalData->rf_type = RF_2T2R;
			break;
		case 5:
			pHalData->rf_type = RF_1T2R;
			break;
		default:
			pHalData->rf_type = RF_1T1R;
			break;
	}

	pHalData->BluetoothCoexist = (boardType >> 4) ? _TRUE : _FALSE;
#endif
}


static VOID
_ReadLEDSetting(
	IN	PADAPTER	Adapter,	
	IN	u8*		PROMContent,
	IN	BOOLEAN		AutoloadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct led_priv 	*pledpriv = &(Adapter->ledpriv);

	// Led mode
	switch(pHalData->CustomerID)
	{
		case RT_CID_DEFAULT:
			pledpriv->LedStrategy = SW_LED_MODE1;
			pledpriv->bRegUseLed = _TRUE;
			break;
		default:
			pledpriv->LedStrategy = SW_LED_MODE0;
			break;			
	}

	#ifdef CONFIG_FORCE_HW_LED
	pledpriv->LedStrategy = HW_LED;
	#endif
}

static void _InitAdapterVariablesByPROM(
	IN	PADAPTER	Adapter,	
	IN	u8*		PROMContent,
	IN	unsigned char AutoloadFail
	)
{
	_ReadPROMVersion(Adapter, PROMContent, AutoloadFail);
	_ReadIDs(Adapter, PROMContent, AutoloadFail);
	_ReadMACAddress(Adapter, PROMContent, AutoloadFail);
	rtl8192d_ReadTxPowerInfo(Adapter, PROMContent, AutoloadFail);
	hal_ReadMacPhyMode_92D(Adapter, PROMContent, AutoloadFail);
	rtl8192d_ReadChannelPlan(Adapter, PROMContent, AutoloadFail);
	_ReadBoardType(Adapter, PROMContent, AutoloadFail);
	_ReadLEDSetting(Adapter, PROMContent, AutoloadFail);	
}

static void _ReadPROMContent(
	IN PADAPTER 		Adapter
	)
{	
	EEPROM_EFUSE_PRIV	*pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
//	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	u8			PROMContent[HWSET_MAX_SIZE]={0};
	u8			eeValue;
	u32			i;
//	u16			value16;

	eeValue = rtw_read8(Adapter, REG_9346CR);
	// To check system boot selection.
	pEEPROM->EepromOrEfuse		= (eeValue & BOOT_FROM_EEPROM) ? _TRUE : _FALSE;
	pEEPROM->bautoload_fail_flag	= (eeValue & EEPROM_EN) ? _FALSE : _TRUE;


	DBG_8192C("Boot from %s, Autoload %s !\n", (pEEPROM->EepromOrEfuse ? "EEPROM" : "EFUSE"),
				(pEEPROM->bautoload_fail_flag ? "Fail" : "OK") );

	//pHalData->EEType = (pEEPROM->EepromOrEfuse == _TRUE) ? EEPROM_93C46 : EEPROM_BOOT_EFUSE;

	if (pEEPROM->bautoload_fail_flag == _FALSE)
	{
		if ( pEEPROM->EepromOrEfuse == _TRUE)
		{
			// Read all Content from EEPROM or EFUSE.
			for(i = 0; i < HWSET_MAX_SIZE; i += 2)
			{
				//todo:
				//value16 = EF2Byte(ReadEEprom(Adapter, (u16) (i>>1)));
				//*((u16*)(&PROMContent[i])) = value16; 				
			}
		}
		else
		{
			// Read EFUSE real map to shadow.
			ACQUIRE_GLOBAL_MUTEX(GlobalMutexForPowerAndEfuse);
			EFUSE_ShadowMapUpdate(Adapter, EFUSE_WIFI, _FALSE);
			RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerAndEfuse);
			_rtw_memcpy((void*)PROMContent, (void*)pEEPROM->efuse_eeprom_data, HWSET_MAX_SIZE);		
		}

		//Double check 0x8192 autoload status again
		if(RTL8192_EEPROM_ID != le16_to_cpu(*((u16 *)PROMContent)))
		{
			pEEPROM->bautoload_fail_flag = _TRUE;
			DBG_8192C("Autoload OK but EEPROM ID content is incorrect!!\n");
		}
		
	}
	else if ( pEEPROM->EepromOrEfuse == _FALSE)//auto load fail
	{
		_rtw_memset(pEEPROM->efuse_eeprom_data, 0xff, HWSET_MAX_SIZE);
		_rtw_memcpy((void*)PROMContent, (void*)pEEPROM->efuse_eeprom_data, HWSET_MAX_SIZE);		
	}

	
	_InitAdapterVariablesByPROM(Adapter, PROMContent, pEEPROM->bautoload_fail_flag);
	
}


static VOID
_InitOtherVariable(
	IN PADAPTER 		Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);	

	//if(Adapter->bInHctTest){
	//	pMgntInfo->PowerSaveControl.bInactivePs = _FALSE;
	//	pMgntInfo->PowerSaveControl.bIPSModeBackup = _FALSE;
	//	pMgntInfo->PowerSaveControl.bLeisurePs = _FALSE;
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

static int _ReadAdapterInfo8192DU(PADAPTER	Adapter)
{
	MSG_8192C("====> ReadAdapterInfo8192D\n");

	//rtl8192d_ReadChipVersion(Adapter);
	_ReadRFType(Adapter);
	_ReadPROMContent(Adapter);

	_InitOtherVariable(Adapter);

	//For 92DU Phy and Mac mode set ,will initialize by EFUSE/EPPROM     zhiyuan 2010/03/25
	MSG_8192C("<==== ReadAdapterInfo8192D\n");

	return _SUCCESS;
}

static void ReadAdapterInfo8192DU(PADAPTER Adapter)
{
	// Read EEPROM size before call any EEPROM function
	//Adapter->EepromAddressSize=Adapter->HalFunc.GetEEPROMSizeHandler(Adapter);
	Adapter->EepromAddressSize = GetEEPROMSize8192D(Adapter);
	
	_ReadAdapterInfo8192DU(Adapter);
}

#define GPIO_DEBUG_PORT_NUM 0
static void rtl8192du_trigger_gpio_0(_adapter *padapter)
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

static VOID 
StopTxBeacon(	
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE*	pHalData = GET_HAL_DATA(Adapter);

	//PlatformScheduleWorkItem(&pHalData->StopTxBeaconWorkItem);

	DBG_8192C("StopTxBeacon\n");

	rtw_write8(Adapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) & (~BIT6));
	pHalData->RegFwHwTxQCtrl &= (~BIT6);
	rtw_write8(Adapter, REG_BCN_MAX_ERR, 0xff);
	rtw_write8(Adapter, REG_TBTT_PROHIBIT+1, 0x64);

	//CheckFwRsvdPageContent(Adapter);  // 2010.06.23. Added by tynli.

}

static VOID
ResumeTxBeacon(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE*	pHalData = GET_HAL_DATA(Adapter);

	//PlatformScheduleWorkItem(&pHalData->ResumeTxBeaconWorkItem);

	DBG_8192C("ResumeTxBeacon\n");

	rtw_write8(Adapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) | BIT6);
	pHalData->RegFwHwTxQCtrl |= BIT6;
	rtw_write8(Adapter, REG_BCN_MAX_ERR, 0x0a);
	rtw_write8(Adapter, REG_TBTT_PROHIBIT+1, 0x64);

}

//
// 2010.11.17. Added by tynli.
//
u8
SelectRTSInitialRate(
	IN	PADAPTER	Adapter
);
u8
SelectRTSInitialRate(
	IN	PADAPTER	Adapter
)
{
	struct sta_info		*psta;
	struct mlme_priv		*pmlmepriv = &Adapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	struct sta_priv		*pstapriv = &Adapter->stapriv;
	u8	bUseProtection;
	u16	BasicRateCfg=0;
	u8	SupportRateSet[NDIS_802_11_LENGTH_RATES_EX];
	u8	RTSRateIndex=0; // 1M
	u8	LowestRateIdx=0;
	//u8	TempRateIdx=0;
	//u8	i;


	psta = rtw_get_stainfo(pstapriv, cur_network->MacAddress);
	if(psta == NULL)
	{
		return RTSRateIndex;
	}

	if(psta->rtsen || psta->cts2self)
		bUseProtection = _TRUE;

	_rtw_memcpy(SupportRateSet, cur_network->SupportedRates, NDIS_802_11_LENGTH_RATES_EX);

	rtl8192d_HalSetBrateCfg( Adapter, SupportRateSet, &BasicRateCfg );

	if( bUseProtection && 
		(!(pmlmeext->cur_wireless_mode == WIRELESS_11A|| pmlmeext->cur_wireless_mode == WIRELESS_11A_5N)))// 5G not support cck rate
	{
		// Use CCK rate
		BasicRateCfg &= 0xf; //CCK rate
		while(BasicRateCfg > 0x1)
		{
			BasicRateCfg = (BasicRateCfg>> 1);
			RTSRateIndex++;
		}
	}
	else //if(pMgntInfo->pHTInfo->CurrentOpMode)
	{
		//MacId 0: INFRA mode.
		if((check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE)&&(check_fwstate(pmlmepriv, WIFI_STATION_STATE)==_TRUE))
		{
			LowestRateIdx = rtw_read8(Adapter, REG_INIDATA_RATE_SEL)&0x3f;
		}		

		//Todo: for AP mode and IBSS mode.
		/*for(i = 0; i < ASSOCIATE_ENTRY_NUM; i++)
		{
			if(AsocEntry[i].bUsed && AsocEntry[i].bAssociated)
 			{
				//Get the lowest data rate.
				if(AsocEntry[i].AID != 0)
				{
					TempRateIdx = rtw_read8(Adapter, REG_INIDATA_RATE_SEL+4*(AsocEntry[i].AID+1));
					TempRateIdx &= 0x3f; // bit 0-5: rate index
					
					if(TempRateIdx < LowestRateIdx)
						LowestRateIdx = TempRateIdx;
				}
			}
		}*/
		
		// Adjust RTS Init rate when the data rate is MCS0~2, 8~10 which is lower than 24M.
		if(LowestRateIdx == 12 || LowestRateIdx == 20) //MCS0, MCS8
		{
			RTSRateIndex = 4; // 6M
		}
		else if(LowestRateIdx == 13 || LowestRateIdx == 14 ||
			LowestRateIdx == 21 || LowestRateIdx == 22) //MCS1, MCS2, MCS9, MCS10
		{
			RTSRateIndex = 6; // 12M
		}
		else 
		{
			//if((pmlmeext->cur_wireless_mode == WIRELESS_11BG_24N) && 
				//(!pMgntInfo->pHTInfo->bCurSuppCCK) && 
			//	(pmlmeext->cur_bwmode == HT_CHANNEL_WIDTH_40))
			//{
			//	BasicRateCfg &= 0xfff0; //disable CCK
			//}
			
			if(BasicRateCfg != 0)
			{
				// Select RTS Init rate
				while(BasicRateCfg > 0x1)
				{
					BasicRateCfg = (BasicRateCfg>> 1);
					RTSRateIndex++;
				}
			}
			else
			{
				RTSRateIndex = 4; // 6M
			}
		}
		
	}

	//Set RTS init rate to Hw.
	return RTSRateIndex;
}

//
// Description: Selcet the RTS init rate and set the rate to HW.
// 2010.11.25. Created by tynli.
//
VOID
SetRTSRateWorkItemCallback(
	IN PVOID			pContext
);
VOID
SetRTSRateWorkItemCallback(
	IN PVOID			pContext
)
{
	PADAPTER		Adapter =  (PADAPTER)pContext;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	NewRTSInitRate = 0;
	
	NewRTSInitRate = SelectRTSInitialRate(Adapter);
	if(NewRTSInitRate != pHalData->RTSInitRate)
	{
		rtw_write8(Adapter, REG_INIRTS_RATE_SEL, NewRTSInitRate);
		pHalData->RTSInitRate = NewRTSInitRate;
	}
	
	DBG_8192C("HW_VAR_INIT_RTS_RATE: RateIndex(%d)\n", NewRTSInitRate);
}

static void hw_var_set_opmode(PADAPTER Adapter, u8 variable, u8* val)
{
	u8	val8;
	u8	mode = *((u8 *)val);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

#ifdef CONFIG_CONCURRENT_MODE
	if(Adapter->iface_type == IFACE_PORT1)
	{
		if((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_))
		{
			PADAPTER pbuddy_adapter = Adapter->pbuddy_adapter;
			struct mlme_ext_priv *pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;
			struct mlme_ext_info *pbuddy_mlmeinfo = &(pbuddy_mlmeext->mlmext_info);
			
			if((pbuddy_mlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
			{
				StopTxBeacon(Adapter);
			}
			rtw_write8(Adapter,REG_BCN_CTRL_1, 0x19);//disable atim wnd
			//rtw_write8(Adapter,REG_BCN_CTRL_1, 0x18);
		}
		else if((mode == _HW_STATE_ADHOC_) /*|| (mode == _HW_STATE_AP_)*/)
		{
			ResumeTxBeacon(Adapter);
			rtw_write8(Adapter,REG_BCN_CTRL_1, 0x1a);
		}
		else if(mode == _HW_STATE_AP_)
		{
			ResumeTxBeacon(Adapter);
					
			rtw_write8(Adapter, REG_BCN_CTRL_1, 0x12);

					
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
			rtw_write8(Adapter, REG_ATIMWND_1, 0x0a); // 10ms for port1
			rtw_write16(Adapter, REG_BCNTCFG, 0x00);
			rtw_write16(Adapter, REG_TBTT_PROHIBIT, 0xff04);
			rtw_write16(Adapter, REG_TSFTR_SYN_OFFSET, 0x7fff);// +32767 (~32ms)		
	
			//reset TSF2	
			rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(1));

			//enable TSF1 Function for if2
			rtw_write8(Adapter, REG_BCN_CTRL_1, (EN_BCN_FUNCTION | EN_TXBCN_RPT|BIT(1)));
			
			//enable update TSF1 for if2
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)&(~BIT(4)));

			//dis BCN0 ATIM  WND if if1 is station
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(0));
					
		}

		val8 = rtw_read8(Adapter, MSR)&0x03;
		val8 |= (mode<<2);
		rtw_write8(Adapter, MSR, val8);

	}
	else
#endif //CONFIG_CONCURRENT_MODE
	{
		if((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_))
		{
#ifdef CONFIG_CONCURRENT_MODE
			PADAPTER pbuddy_adapter = Adapter->pbuddy_adapter;
			struct mlme_ext_priv *pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;
			struct mlme_ext_info *pbuddy_mlmeinfo = &(pbuddy_mlmeext->mlmext_info);
			
			if((pbuddy_mlmeinfo->state&0x03) != WIFI_FW_AP_STATE)
#endif //CONFIG_CONCURRENT_MODE
			{
				StopTxBeacon(Adapter);
			}
			rtw_write8(Adapter,REG_BCN_CTRL, 0x19);//disable atim wnd
			//rtw_write8(Adapter,REG_BCN_CTRL, 0x18);
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
			//write32(padapter, REG_RCR, 0x70002a8e);//CBSSID_DATA must set to 0
			rtw_write32(Adapter, REG_RCR, 0x7000228e);//CBSSID_DATA must set to 0
			//enable to rx data frame				
			rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);
			//enable to rx ps-poll
			rtw_write16(Adapter, REG_RXFLTMAP1, 0x0400);

			//Beacon Control related register for first time 
			rtw_write8(Adapter, REG_BCNDMATIM, 0x02); // 2ms		
			rtw_write8(Adapter, REG_DRVERLYINT, 0x05);// 5ms
			//write8(Adapter, REG_BCN_MAX_ERR, 0xFF);
			rtw_write8(Adapter, REG_ATIMWND, 0x0a); // 10ms for port0
			rtw_write16(Adapter, REG_BCNTCFG, 0x00);
			rtw_write16(Adapter, REG_TBTT_PROHIBIT, 0xff04);
			rtw_write16(Adapter, REG_TSFTR_SYN_OFFSET, 0x7fff);// +32767 (~32ms)	
	
			//reset TSF		
			rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(0));

			//enable TSF Function for if1
			rtw_write8(Adapter, REG_BCN_CTRL, (EN_BCN_FUNCTION | EN_TXBCN_RPT));
			
			//enable update TSF for if1
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(4)));			

			//dis BCN1 ATIM  WND if if2 is station
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|BIT(0));		
		}

		val8 = rtw_read8(Adapter, MSR)&0x0c;
		val8 |= mode;
		rtw_write8(Adapter, MSR, val8);

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
#endif //CONFIG_CONCURRENT_MODE
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
#ifdef CONFIG_CONCURRENT_MODE
	if(Adapter->iface_type == IFACE_PORT1)
	{
		//disable related TSF function
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)&(~BIT(3)));
							
		rtw_write32(Adapter, REG_TSFTR1, tsf);
		rtw_write32(Adapter, REG_TSFTR1+4, tsf>>32);

		//enable related TSF function
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|BIT(3));
	}
	else
#endif //CONFIG_CONCURRENT_MODE
	{
		//disable related TSF function
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(3)));
							
		rtw_write32(Adapter, REG_TSFTR, tsf);
		rtw_write32(Adapter, REG_TSFTR+4, tsf>>32);

		//enable related TSF function
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(3));
	}
				
							
	if(((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
	{
		//pHalData->RegTxPause  &= (~STOP_BCNQ);
		//rtw_write8(Adapter, REG_TXPAUSE, (rtw_read8(Adapter, REG_TXPAUSE)&(~BIT(6))));
		ResumeTxBeacon(Adapter);
	}
}

static void hw_var_set_mlme_disconnect(PADAPTER Adapter, u8 variable, u8* val)
{
#ifdef CONFIG_CONCURRENT_MODE
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PADAPTER pbuddy_adapter = Adapter->pbuddy_adapter;
	struct mlme_ext_priv *pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;
	struct mlme_ext_info *pbuddy_mlmeinfo = &(pbuddy_mlmeext->mlmext_info);
			
	if((pbuddy_mlmeinfo->state&0x03) == _HW_STATE_NOLINK_)
		rtw_write16(Adapter, REG_RXFLTMAP2, 0x00);
	

	if(Adapter->iface_type == IFACE_PORT1)
	{
		//reset TSF
		rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(1));

		//disable update TSF
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|BIT(4));	
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
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);

	struct mlme_ext_priv *pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	
	PADAPTER pbuddy_adapter = Adapter->pbuddy_adapter;
	struct mlme_priv *pbuddy_mlmepriv = &(pbuddy_adapter->mlmepriv);
	struct mlme_ext_priv *pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;
	struct mlme_ext_info *pbuddy_mlmeinfo = &(pbuddy_mlmeext->mlmext_info);
	
	if(*((u8 *)val))//under sitesurvey
	{
		//config RCR to receive different BSSID & not to receive data frame
		//pHalData->ReceiveConfig &= (~(RCR_CBSSID_DATA | RCR_CBSSID_BCN));			
		u32 v = rtw_read32(Adapter, REG_RCR);
		v &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN );//| RCR_ADF
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

		if(((pbuddy_mlmeinfo->state&0x03) == WIFI_FW_AP_STATE) &&
			check_fwstate(pbuddy_mlmepriv, _FW_LINKED))
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

		if((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE ||
			(pbuddy_mlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
			rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_BCN);
		else			
			rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);
					
		if(((pbuddy_mlmeinfo->state&0x03) == WIFI_FW_AP_STATE) &&
			check_fwstate(pbuddy_mlmepriv, _FW_LINKED))
		{
			ResumeTxBeacon(Adapter);			

			//reset TSF 1/2 after ResumeTxBeacon
			if(pbuddy_adapter->iface_type == IFACE_PORT1)				
				rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(1));	
			else
				rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(0));
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
	PADAPTER pbuddy_adapter = Adapter->pbuddy_adapter;
	struct mlme_priv *pbuddy_mlmepriv = &(pbuddy_adapter->mlmepriv);
	struct mlme_ext_priv *pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;
	struct mlme_ext_info *pbuddy_mlmeinfo = &(pbuddy_mlmeext->mlmext_info);

	if(type == 0) // prepare to join
	{		
		if(((pbuddy_mlmeinfo->state&0x03) == WIFI_FW_AP_STATE) &&
			check_fwstate(pbuddy_mlmepriv, _FW_LINKED))
		{
			StopTxBeacon(Adapter);
		}
	
		//enable to rx data frame.Accept all data frame
		//rtw_write32(padapter, REG_RCR, rtw_read32(padapter, REG_RCR)|RCR_ADF);
		rtw_write16(Adapter, REG_RXFLTMAP2,0xFFFF);

		if((pbuddy_mlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
			rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_BCN);
		else
			rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);

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
		if((pbuddy_mlmeinfo->state&0x03) == _HW_STATE_NOLINK_)
			rtw_write16(Adapter, REG_RXFLTMAP2,0x00);

		if(((pbuddy_mlmeinfo->state&0x03) == WIFI_FW_AP_STATE) &&
			check_fwstate(pbuddy_mlmepriv, _FW_LINKED))
		{
			ResumeTxBeacon(Adapter);			
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


		if(((pbuddy_mlmeinfo->state&0x03) == WIFI_FW_AP_STATE) &&
			check_fwstate(pbuddy_mlmepriv, _FW_LINKED))
		{
			ResumeTxBeacon(Adapter);			
		}
		
	}

	rtw_write16(Adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);
	
#endif
}

#ifdef CONFIG_DUALMAC_CONCURRENT
static void dc_hw_var_mlme_sitesurvey(PADAPTER Adapter, u8 sitesurvey_state)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);	
	PADAPTER BuddyAdapter = Adapter->pbuddy_adapter;
	struct mlme_priv *pbuddy_mlmepriv;
	struct mlme_ext_priv *pbuddy_mlmeext;
	struct mlme_ext_info *pbuddy_mlmeinfo;


	if((BuddyAdapter !=NULL) && 
		Adapter->DualMacConcurrent == _TRUE)
	{
		pbuddy_mlmepriv = &(BuddyAdapter->mlmepriv);
		pbuddy_mlmeext = &BuddyAdapter->mlmeextpriv;
		pbuddy_mlmeinfo = &(pbuddy_mlmeext->mlmext_info);

		if(sitesurvey_state)//under sitesurvey
		{
			if(((pbuddy_mlmeinfo->state&0x03) == WIFI_FW_AP_STATE) &&
				check_fwstate(pbuddy_mlmepriv, _FW_LINKED))
			{
				StopTxBeacon(BuddyAdapter);
			}

			rtw_write16(Adapter, REG_RRSR, 0x150);
		}
		else//sitesurvey done
		{
			if(((pbuddy_mlmeinfo->state&0x03) == WIFI_FW_AP_STATE) &&
				check_fwstate(pbuddy_mlmepriv, _FW_LINKED))
			{
				ResumeTxBeacon(BuddyAdapter);
			}	
		}
	}
}

static void dc_hw_var_mlme_join(PADAPTER Adapter, u8 join_state)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	PADAPTER BuddyAdapter = Adapter->pbuddy_adapter;
	struct mlme_priv *pbuddy_mlmepriv;
	struct mlme_ext_priv *pbuddy_mlmeext;
	struct mlme_ext_info *pbuddy_mlmeinfo;


	if((BuddyAdapter !=NULL) && 
		Adapter->DualMacConcurrent == _TRUE)
	{
		pbuddy_mlmepriv = &(BuddyAdapter->mlmepriv);
		pbuddy_mlmeext = &BuddyAdapter->mlmeextpriv;
		pbuddy_mlmeinfo = &(pbuddy_mlmeext->mlmext_info);

		if(pmlmeext->cur_channel != pbuddy_mlmeext->cur_channel ||
			pmlmeext->cur_bwmode != pbuddy_mlmeext->cur_bwmode ||
			pmlmeext->cur_ch_offset != pbuddy_mlmeext->cur_ch_offset)
		{
			if(join_state == 0)// prepare to join
			{
				if(((pbuddy_mlmeinfo->state&0x03) == WIFI_FW_AP_STATE) &&
					check_fwstate(pbuddy_mlmepriv, _FW_LINKED))
				{
					StopTxBeacon(BuddyAdapter);
				}
			}
			else//join success or fail
			{
				if(((pbuddy_mlmeinfo->state&0x03) == WIFI_FW_AP_STATE) &&
					check_fwstate(pbuddy_mlmepriv, _FW_LINKED))
				{
					ResumeTxBeacon(BuddyAdapter);
				}
			}
		}
	}
}
#endif

void SetHwReg8192DU(PADAPTER Adapter, u8 variable, u8* val);
void SetHwReg8192DU(PADAPTER Adapter, u8 variable, u8* val)
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
#if defined(CONFIG_CONCURRENT_MODE)
			hw_var_set_opmode(Adapter, variable, val);
#else //CONFIG_CONCURRENT_MODE
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
					//if(IS_NORMAL_CHIP(pHalData->VersionID))
					//{			
						rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(4)));			
					//}
					//else
					//{
					//	rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~(BIT(4)|BIT(5))));
					//}
					
				}

				val8 = rtw_read8(Adapter, MSR)&0x0c;
				val8 |= mode;
				rtw_write8(Adapter, MSR, val8);
			}
#endif //CONFIG_CONCURRENT_MODE
			break;
		case HW_VAR_MAC_ADDR:
			hw_var_set_macaddr(Adapter, variable, val);			
			break;
		case HW_VAR_BSSID:
#if defined(CONFIG_CONCURRENT_MODE)
			hw_var_set_bssid(Adapter, variable, val);
#else //CONFIG_CONCURRENT_MODE
			{
				u8	idx = 0;
				for(idx = 0 ; idx < 6; idx++)
				{
					rtw_write8(Adapter, (REG_BSSID+idx), val[idx]);
				}
			}
#endif //CONFIG_CONCURRENT_MODE
			break;
		case HW_VAR_BASIC_RATE:
			{
				u16	BrateCfg = 0;
				u8	RateIndex = 0;
#ifdef CONFIG_DUALMAC_CONCURRENT
				PADAPTER	BuddyAdapter = Adapter->pbuddy_adapter;
#endif
				
				// 2007.01.16, by Emily
				// Select RRSR (in Legacy-OFDM and CCK)
				// For 8190, we select only 24M, 12M, 6M, 11M, 5.5M, 2M, and 1M from the Basic rate.
				// We do not use other rates.
				rtl8192d_HalSetBrateCfg( Adapter, val, &BrateCfg );

				if(pHalData->CurrentBandType92D == BAND_ON_2_4G)
				{
					//CCK 2M ACK should be disabled for some BCM and Atheros AP IOT
					//because CCK 2M has poor TXEVM
					//CCK 5.5M & 11M ACK should be enabled for better performance
					pHalData->BasicRateSet = BrateCfg = (BrateCfg |0xd )& 0x15d;
					BrateCfg |= 0x1; // default enable 1M ACK rate
				}
				else // 5G
				{
					pHalData->BasicRateSet &= 0xFF0;
					BrateCfg |= 0x10; // default enable 6M ACK rate
				}

				DBG_8192C("HW_VAR_BASIC_RATE: BrateCfg(%#x)\n", BrateCfg);

				// Set RRSR rate table.
				rtw_write8(Adapter, REG_RRSR, BrateCfg&0xff);
				rtw_write8(Adapter, REG_RRSR+1, (BrateCfg>>8)&0xff);

				//if(pHalData->FirmwareVersion > 0xe)
				//{
				//	SetRTSRateWorkItemCallback(Adapter);
				//}
				//else
				//{
					// Set RTS initial rate
					while(BrateCfg > 0x1)
					{
						BrateCfg = (BrateCfg>> 1);
						RateIndex++;
					}
					rtw_write8(Adapter, REG_INIRTS_RATE_SEL, RateIndex);
				//}


				if(check_fwstate(&Adapter->mlmepriv, WIFI_AP_STATE) == _TRUE)
				{
					DBG_8192C("%s(): pHalData->bNeedIQK = _TRUE\n",__FUNCTION__);
					pHalData->bNeedIQK = _TRUE; //for 92D IQK
				}
#ifdef CONFIG_DUALMAC_CONCURRENT
				if((BuddyAdapter !=NULL) && (pHalData->bSlaveOfDMSP))
				{
					if(check_fwstate(&BuddyAdapter->mlmepriv, WIFI_AP_STATE) == _TRUE)
						GET_HAL_DATA(BuddyAdapter)->bNeedIQK = _TRUE; //for 92D IQK
				}
#endif
			}
			break;
		case HW_VAR_TXPAUSE:
			rtw_write8(Adapter, REG_TXPAUSE, *((u8 *)val));	
			break;
		case HW_VAR_BCN_FUNC:
#ifdef CONFIG_CONCURRENT_MODE
			hw_var_set_bcn_func(Adapter, variable, val);
#else //CONFIG_CONCURRENT_MODE
			if(*((u8 *)val))
			{
				rtw_write8(Adapter, REG_BCN_CTRL, (EN_BCN_FUNCTION | EN_TXBCN_RPT));
			}
			else
			{
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~(EN_BCN_FUNCTION | EN_TXBCN_RPT)));
			}
#endif //CONFIG_CONCURRENT_MODE
			break;
		case HW_VAR_CORRECT_TSF:
#ifdef CONFIG_CONCURRENT_MODE
			hw_var_set_correct_tsf(Adapter, variable, val);
#else //CONFIG_CONCURRENT_MODE
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
#endif //CONFIG_CONCURRENT_MODE
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
#else //CONFIG_CONCURRENT_MODE
			{
				//Set RCR to not to receive data frame when NO LINK state
				//rtw_write32(Adapter, REG_RCR, rtw_read32(padapter, REG_RCR) & ~RCR_ADF);
				rtw_write16(Adapter, REG_RXFLTMAP2,0x00);

				//reset TSF
				rtw_write8(Adapter, REG_DUAL_TSF_RST, (BIT(0)|BIT(1)));

				//disable update TSF
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(4));
			}
#endif //CONFIG_CONCURRENT_MODE
			break;
		case HW_VAR_MLME_SITESURVEY:
#ifdef CONFIG_CONCURRENT_MODE
			hw_var_set_mlme_sitesurvey(Adapter, variable,  val);
#else //CONFIG_CONCURRENT_MODE
			if(*((u8 *)val))//under sitesurvey
			{
				pHalData->bLoadIMRandIQKSettingFor2G = _FALSE;			
				{
					u32 v = rtw_read32(Adapter, REG_RCR);
					v &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN );//| RCR_ADF
					rtw_write32(Adapter, REG_RCR, v);
#ifdef CONFIG_FIND_BEST_CHANNEL
					// Recieve all data frames
					rtw_write16(Adapter, REG_RXFLTMAP2,0xFFFF);
#else /* CONFIG_FIND_BEST_CHANNEL */
					//config RCR to receive different BSSID & not to receive data frame
					rtw_write16(Adapter, REG_RXFLTMAP2,0x00);
#endif /* CONFIG_FIND_BEST_CHANNEL */
					//disable update TSF
					rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(4));
				}

				// Save orignal RRSR setting.
				pHalData->RegRRSR = rtw_read16(Adapter, REG_RRSR);
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
					rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);

				// Restore orignal RRSR setting.
				rtw_write16(Adapter, REG_RRSR, pHalData->RegRRSR);
			}
#ifdef CONFIG_DUALMAC_CONCURRENT
			dc_hw_var_mlme_sitesurvey(Adapter, *((u8 *)val));
#endif //CONFIG_DUALMAC_CONCURRENT

#endif //CONFIG_CONCURRENT_MODE
			break;
		case HW_VAR_MLME_JOIN:
#ifdef CONFIG_CONCURRENT_MODE
			hw_var_set_mlme_join(Adapter, variable,  val);
#else //CONFIG_CONCURRENT_MODE
			{
				u8	RetryLimit = 0x30;
				u8	type = *((u8 *)val);
				struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
#ifdef CONFIG_DUALMAC_CONCURRENT
				PADAPTER	BuddyAdapter = Adapter->pbuddy_adapter;
#endif

				if(type == 0) // prepare to join
				{
					//enable to rx data frame.Accept all data frame
					//rtw_write32(padapter, REG_RCR, rtw_read32(padapter, REG_RCR)|RCR_ADF);
					rtw_write16(Adapter, REG_RXFLTMAP2,0xFFFF);

					rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);

					if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
					{
						RetryLimit = (pHalData->CustomerID == RT_CID_CCX) ? 7 : 48;
					}
					else // Ad-hoc Mode
					{
						RetryLimit = 0x7;
					}

					DBG_8192C("%s(): pHalData->bNeedIQK = _TRUE, pHalData->IQKRdyForXmit = 0\n",__FUNCTION__);
					pHalData->bNeedIQK = _TRUE; //for 92D IQK
					ATOMIC_SET(&pHalData->IQKRdyForXmit, 0);
#ifdef CONFIG_DUALMAC_CONCURRENT
					if((BuddyAdapter !=NULL) && (pHalData->bSlaveOfDMSP))
					{
						GET_HAL_DATA(BuddyAdapter)->bNeedIQK = _TRUE; //for 92D IQK
					}
#endif
				}
				else if(type == 1) //joinbss_event call back when join res < 0
				{
					//config RCR to receive different BSSID & not to receive data frame during linking				
					//u32 v = rtw_read32(Adapter, REG_RCR);
					//v &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN );//| RCR_ADF
					//rtw_write32(Adapter, REG_RCR, v);
				
					rtw_write16(Adapter, REG_RXFLTMAP2,0x00);
				}
				else if(type == 2) //sta add event call back
				{
					//enable update TSF
					rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(4)));

					if(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE))
					{
						//fixed beacon issue for 8191su...........
						//rtw_write8(Adapter,0x542 ,0x02);
						RetryLimit = 0x7;
					}
				}

				rtw_write16(Adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);
#ifdef CONFIG_DUALMAC_CONCURRENT
				dc_hw_var_mlme_join(Adapter, *((u8 *)val));
#endif //CONFIG_DUALMAC_CONCURRENT
			}
#endif //CONFIG_CONCURRENT_MODE
			break;
		case HW_VAR_BEACON_INTERVAL:
			rtw_write16(Adapter, REG_BCN_INTERVAL, *((u16 *)val));
			break;
		case HW_VAR_SLOT_TIME:
			{
				u8	u1bAIFS, aSifsTime;
				struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
				struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

				DBG_8192C("Set HW_VAR_SLOT_TIME: SlotTime(%#x)\n", val[0]);
				rtw_write8(Adapter, REG_SLOT, val[0]);

				if(pmlmeinfo->WMM_enable == 0)
				{
					if(pmlmeext->cur_wireless_mode == WIRELESS_11B)
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
		case HW_VAR_ACK_PREAMBLE:
			{
				u8	regTmp;
				u8	bShortPreamble = *( (PBOOLEAN)val );
				// Joseph marked out for Netgear 3500 TKIP channel 7 issue.(Temporarily)
				regTmp = (pHalData->nCur40MhzPrimeSC)<<5;
				//regTmp = 0;
				if(bShortPreamble)
					regTmp |= 0x80;

				rtw_write8(Adapter, REG_RRSR+2, regTmp);
			}
			break;
		case HW_VAR_SEC_CFG:
#ifdef CONFIG_CONCURRENT_MODE			
			rtw_write8(Adapter, REG_SECCFG, 0x0c |BIT(5));//only enable tx enc and rx dec engine.
#else //CONFIG_CONCURRENT_MODE
			rtw_write8(Adapter, REG_SECCFG, *((u8 *)val));
#endif //CONFIG_CONCURRENT_MODE
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
				u8	FactorToSet;
				u32	RegToSet;
				u8	*pTmpByte = NULL;
				u8	index = 0;

				RegToSet = 0xb972a841;
				if(pHalData->MacPhyMode92D==SINGLEMAC_SINGLEPHY){
					RegToSet = 0x88728841;
				}
				else if(pHalData->MacPhyMode92D==DUALMAC_DUALPHY){
					RegToSet = 0x66525541;
				}
				else if(pHalData->MacPhyMode92D==DUALMAC_SINGLEPHY){
					RegToSet = 0x44444441;
				}

				FactorToSet = *((u8 *)val);
				if(FactorToSet <= 3)
				{
					FactorToSet = (1<<(FactorToSet + 2));
					if(FactorToSet>0xf)
						FactorToSet = 0xf;

					for(index=0; index<4; index++)
					{
						pTmpByte = (u8 *)(&RegToSet) + index;

						if((*pTmpByte & 0xf0) > (FactorToSet<<4))
							*pTmpByte = (*pTmpByte & 0x0f) | (FactorToSet<<4);
					
						if((*pTmpByte & 0x0f) > FactorToSet)
							*pTmpByte = (*pTmpByte & 0xf0) | (FactorToSet);
					}

					rtw_write32(Adapter, REG_AGGLEN_LMT, RegToSet);
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
			{
				u8	RpwmVal = (*(u8 *)val);
				RpwmVal = RpwmVal & 0xf;

				/*if(pHalData->PreRpwmVal & BIT7) //bit7: 1
				{
					PlatformEFIOWrite1Byte(Adapter, REG_USB_HRPWM, (*(pu1Byte)val));
					pHalData->PreRpwmVal = (*(pu1Byte)val);
				}
				else //bit7: 0
				{
					PlatformEFIOWrite1Byte(Adapter, REG_USB_HRPWM, ((*(pu1Byte)val)|BIT7));
					pHalData->PreRpwmVal = ((*(pu1Byte)val)|BIT7);
				}*/
				FillH2CCmd92D(Adapter, H2C_PWRM, 1, (u8 *)(&RpwmVal));
			}
			break;
		case HW_VAR_H2C_FW_PWRMODE:
			rtl8192d_set_FwPwrMode_cmd(Adapter, (*(u8 *)val));
			break;
		case HW_VAR_H2C_FW_JOINBSSRPT:
			rtl8192d_set_FwJoinBssReport_cmd(Adapter, (*(u8 *)val));
			break;
#ifdef CONFIG_P2P
		case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
			{
				u8	p2p_ps_state = (*(u8 *)val);
				rtl8192d_set_p2p_ps_offload_cmd(Adapter, p2p_ps_state);
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
			rtl8192du_trigger_gpio_0(Adapter);
			break;
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
		case HW_VAR_CHECK_TXBUF:
#if defined(CONFIG_CONCURRENT_MODE) || defined(CONFIG_DUALMAC_CONCURRENT)
			{
				int i;
				u8	RetryLimit = 0x01;
				
				//rtw_write16(Adapter, REG_RL,0x0101);
				rtw_write16(Adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);
		
				for(i=0;i<1000;i++)
				{
					if(rtw_read32(Adapter, 0x200) != rtw_read32(Adapter, 0x204))
					{
						//DBG_871X("packet in tx packet buffer - 0x204=%x, 0x200=%x (%d)\n", rtw_read32(Adapter, 0x204), rtw_read32(Adapter, 0x200), i);
						rtw_msleep_os(10);
					}
					else
					{
						DBG_871X("no packet in tx packet buffer (%d)\n", i);
						break;
					}
				}

				RetryLimit = 0x30;	
				rtw_write16(Adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);
		
			}
#endif
			break;
		default:
			break;
	}

_func_exit_;
}

void GetHwReg8192DU(PADAPTER Adapter, u8 variable, u8* val);
void GetHwReg8192DU(PADAPTER Adapter, u8 variable, u8* val)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

_func_enter_;

	switch(variable)
	{
		case HW_VAR_BASIC_RATE:
			*((u16 *)(val)) = pHalData->BasicRateSet;
			break;
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
GetHalDefVar8192DUsb(
	IN	PADAPTER				Adapter,
	IN	HAL_DEF_VARIABLE		eVariable,
	IN	PVOID					pValue
	);
u8
GetHalDefVar8192DUsb(
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
		case HAL_DEF_DRVINFO_SZ:
			*(( u32*)pValue) = DRVINFO_SZ;
			break;
		case HAL_DEF_MAX_RECVBUF_SZ:
			*(( u32*)pValue) = MAX_RECVBUF_SZ;
			break;
		case HAL_DEF_RX_PACKET_OFFSET:
			*(( u32*)pValue) = RXDESC_SIZE + DRVINFO_SZ;
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
SetHalDefVar8192DUsb(
	IN	PADAPTER				Adapter,
	IN	HAL_DEF_VARIABLE		eVariable,
	IN	PVOID					pValue
	);
u8
SetHalDefVar8192DUsb(
	IN	PADAPTER				Adapter,
	IN	HAL_DEF_VARIABLE		eVariable,
	IN	PVOID					pValue
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _TRUE;

	switch(eVariable)
	{
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

u32  _update_92cu_basic_rate(_adapter *padapter, unsigned int mask);
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

void _update_response_rate(_adapter *padapter,unsigned int mask);
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

void UpdateHalRAMask8192DUsb(PADAPTER padapter, u32 mac_id);
void UpdateHalRAMask8192DUsb(PADAPTER padapter, u32 mac_id)
{
	u32	value[2];
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

	if (mac_id >= NUM_STA)
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
			networkType = judge_network_type(padapter, cur_network->SupportedRates, supportRateNum);
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
			networkType = judge_network_type(padapter, pmlmeinfo->FW_sta_info[mac_id].SupportedRates, supportRateNum);
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
		value[0] = mask;
		value[1] = mac_id | (shortGIrate?0x20:0x00) |BIT(7);

		DBG_871X("update raid entry, mask=0x%x, arg=0x%x\n", value[0], value[1]);

		FillH2CCmd92D(padapter, H2C_RA_MASK, 5, (u8 *)(&value));
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

void SetBeaconRelatedRegisters8192DUsb(PADAPTER padapter);
void SetBeaconRelatedRegisters8192DUsb(PADAPTER padapter)
{
	u32	value32;
//	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);


	rtw_write8(padapter, REG_ATIMWND, 0x02);

	rtw_write16(padapter, REG_BCN_INTERVAL, pmlmeinfo->bcn_interval);

	_InitBeaconParameters(padapter);

	rtw_write8(padapter, REG_SLOT, 0x09);

	//
	// Reset TSF Timer to zero, added by Roger. 2008.06.24
	//
	//pHalData->TransmitConfig &= (~TSFRST);
	value32 = rtw_read32(padapter, REG_TCR); 
	value32 &= ~TSFRST;
	rtw_write32(padapter, REG_TCR, value32); 

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

static void rtl8192du_init_default_value(_adapter * padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	pHalData->CurrentWirelessMode = WIRELESS_MODE_AUTO;

	//init default value
	pHalData->fw_ractrl = _FALSE;
	if(!pwrctrlpriv->bkeepfwalive)
		pHalData->LastHMEBoxNum = 0;

	pHalData->bEarlyModeEnable = 0;
	pHalData->pwrGroupCnt = 0;

	//init dm default value
	pdmpriv->TM_Trigger = 0;
	//pdmpriv->binitialized = _FALSE;
	pdmpriv->prv_traffic_idx = 3;

	rtl8192d_PHY_ResetIQKResult(padapter);

}


void rtl8192du_set_hal_ops(_adapter * padapter)
{
	struct hal_ops	*pHalFunc = &padapter->HalFunc;

_func_enter_;

	padapter->HalData = rtw_zmalloc(sizeof(HAL_DATA_TYPE));
	if(padapter->HalData == NULL){
		DBG_8192C("cant not alloc memory for HAL DATA \n");
	}
	//_rtw_memset(padapter->HalData, 0, sizeof(HAL_DATA_TYPE));
	padapter->hal_data_sz = sizeof(HAL_DATA_TYPE);

	ATOMIC_SET(&(GET_HAL_DATA(padapter)->IQKRdyForXmit), 1);

	pHalFunc->hal_init = &rtl8192du_hal_init;
	pHalFunc->hal_deinit = &rtl8192du_hal_deinit;

	//pHalFunc->free_hal_data = &rtl8192d_free_hal_data;

	pHalFunc->inirp_init = &rtl8192du_inirp_init;
	pHalFunc->inirp_deinit = &rtl8192du_inirp_deinit;

	pHalFunc->init_xmit_priv = &rtl8192du_init_xmit_priv;
	pHalFunc->free_xmit_priv = &rtl8192du_free_xmit_priv;

	pHalFunc->init_recv_priv = &rtl8192du_init_recv_priv;
	pHalFunc->free_recv_priv = &rtl8192du_free_recv_priv;
#ifdef CONFIG_SW_LED
	pHalFunc->InitSwLeds = &rtl8192du_InitSwLeds;
	pHalFunc->DeInitSwLeds = &rtl8192du_DeInitSwLeds;
#else // case of hw led or no led
	pHalFunc->InitSwLeds = NULL;
	pHalFunc->DeInitSwLeds = NULL;	
#endif //CONFIG_SW_LED
	//pHalFunc->dm_init = &rtl8192d_init_dm_priv;
	//pHalFunc->dm_deinit = &rtl8192d_deinit_dm_priv;

	pHalFunc->init_default_value = &rtl8192du_init_default_value;
	pHalFunc->intf_chip_configure = &rtl8192du_interface_configure;
	pHalFunc->read_adapter_info = &ReadAdapterInfo8192DU;

	//pHalFunc->set_bwmode_handler = &PHY_SetBWMode8192D;
	//pHalFunc->set_channel_handler = &PHY_SwChnl8192D;

	pHalFunc->hal_dm_watchdog = &rtl8192d_HalDmWatchDog;

	pHalFunc->SetHwRegHandler = &SetHwReg8192DU;
	pHalFunc->GetHwRegHandler = &GetHwReg8192DU;
  	pHalFunc->GetHalDefVarHandler = &GetHalDefVar8192DUsb;
 	pHalFunc->SetHalDefVarHandler = &SetHalDefVar8192DUsb;

	pHalFunc->UpdateRAMaskHandler = &UpdateHalRAMask8192DUsb;
	pHalFunc->SetBeaconRelatedRegistersHandler = &SetBeaconRelatedRegisters8192DUsb;

	pHalFunc->hal_xmit = &rtl8192du_hal_xmit;
	pHalFunc->mgnt_xmit = &rtl8192du_mgnt_xmit;

	//pHalFunc->read_bbreg = &rtl8192d_PHY_QueryBBReg;
	//pHalFunc->write_bbreg = &rtl8192d_PHY_SetBBReg;
	//pHalFunc->read_rfreg = &rtl8192d_PHY_QueryRFReg;
	//pHalFunc->write_rfreg = &rtl8192d_PHY_SetRFReg;

#ifdef CONFIG_HOSTAPD_MLME
	pHalFunc->hostap_mgnt_xmit_entry = &rtl8192du_hostap_mgnt_xmit_entry;
#endif

	rtl8192d_set_hal_ops(pHalFunc);

_func_exit_;
}
