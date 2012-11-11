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
#define _SDIO_HALINIT_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

#ifndef CONFIG_SDIO_HCI
#error "CONFIG_SDIO_HCI shall be on!\n"
#endif

#include <rtw_efuse.h>
#include <rtl8188e_hal.h>
#include <rtl8188e_led.h>
#include <HalPwrSeqCmd.h>
#include <Hal8188EPwrSeq.h>
#include <sdio_ops.h>

/*
 * Description:
 *	Call power on sequence to enable card
 *
 * Return:
 *	_SUCCESS	enable success
 *	_FAIL		enable fail
 */
static u8 _CardEnable(PADAPTER padapter)
{
	u8 bMacPwrCtrlOn;
	u8 ret;

	DBG_871X("=>%s\n", __FUNCTION__);

	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (bMacPwrCtrlOn == _FALSE)
	{
		// RSV_CTRL 0x1C[7:0] = 0x00
		// unlock ISO/CLK/Power control register
		rtw_write8(padapter, REG_RSV_CTRL, 0x0);

		ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, Rtl8188E_NIC_ENABLE_FLOW);
		if (ret == _SUCCESS) {
			u8 bMacPwrCtrlOn = _TRUE;
			rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
		}
		else
		{
			DBG_871X(KERN_ERR "%s: run power on flow fail\n", __func__);
			return _FAIL;	
		}
		
	} 
	else
	{
		ret = _SUCCESS;
	}

	DBG_871X("<=%s\n", __FUNCTION__);

	return ret;
	
}

static u8 _InitPowerOn(PADAPTER padapter)
{
	u8 value8;
	u16 value16;
	u32 value32;
	u8 ret;

	DBG_871X("=>%s\n", __FUNCTION__);

	ret = _CardEnable(padapter);
	if (ret == _FAIL) {
		return ret;
	}

/*
	// Radio-Off Pin Trigger
	value8 = rtw_read8(padapter, REG_GPIO_INTM+1);
	value8 |= BIT(1); // Enable falling edge triggering interrupt
	rtw_write8(padapter, REG_GPIO_INTM+1, value8);
	value8 = rtw_read8(padapter, REG_GPIO_IO_SEL_2+1);
	value8 |= BIT(1);
	rtw_write8(padapter, REG_GPIO_IO_SEL_2+1, value8);
*/

	// Enable power down and GPIO interrupt
	value16 = rtw_read16(padapter, REG_APS_FSMCO);
	value16 |= EnPDN; // Enable HW power down and RF on
	rtw_write16(padapter, REG_APS_FSMCO, value16);


	// Enable MAC DMA/WMAC/SCHEDULE/SEC block
	value16 = rtw_read16(padapter, REG_CR);
	value16 |= (HCI_TXDMA_EN | HCI_RXDMA_EN | TXDMA_EN | RXDMA_EN
				| PROTOCOL_EN | SCHEDULE_EN | ENSEC | CALTMR_EN);
	// for SDIO - Set CR bit10 to enable 32k calibration. Suggested by SD1 Gimmy. Added by tynli. 2011.08.31.
	
	rtw_write16(padapter, REG_CR, value16);



	// Enable CMD53 R/W Operation
//	bMacPwrCtrlOn = TRUE;
//	rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, (pu8)(&bMacPwrCtrlOn));

	DBG_871X("<=%s\n", __FUNCTION__);
	
	return _SUCCESS;
	
}

static void _InitQueueReservedPage(PADAPTER padapter)
{
#ifdef RTL8188ES_MAC_LOOPBACK	

//#define MAC_LOOPBACK_PAGE_NUM_PUBQ		0x26
//#define MAC_LOOPBACK_PAGE_NUM_HPQ		0x0b
//#define MAC_LOOPBACK_PAGE_NUM_LPQ		0x0b
//#define MAC_LOOPBACK_PAGE_NUM_NPQ		0x0b // 71 pages=>9088 bytes, 8.875k

	rtw_write16(padapter, REG_RQPN_NPQ, 0x0b0b);
	rtw_write32(padapter, REG_RQPN, 0x80260b0b);
	
#else //TX_PAGE_BOUNDARY_LOOPBACK_MODE	
	rtw_write16(padapter, REG_RQPN_NPQ, 0x0000);
	rtw_write32(padapter,REG_RQPN, 0x80a00900);
#endif
	return;	
}

static void _InitTxBufferBoundary(PADAPTER padapter)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	//u16	txdmactrl;
	u8	txpktbuf_bndy;

	if (!pregistrypriv->wifi_spec) {
		txpktbuf_bndy = TX_PAGE_BOUNDARY_88E;// 21k
	} else {
		//for WMM
		txpktbuf_bndy = WMM_NORMAL_TX_PAGE_BOUNDARY_88E;
	}

	rtw_write8(padapter, REG_TXPKTBUF_BCNQ_BDNY, txpktbuf_bndy);
	rtw_write8(padapter, REG_TXPKTBUF_MGQ_BDNY, txpktbuf_bndy);
	rtw_write8(padapter, REG_TXPKTBUF_WMAC_LBK_BF_HD, txpktbuf_bndy);
	rtw_write8(padapter, REG_TRXFF_BNDY, txpktbuf_bndy);
	rtw_write8(padapter, REG_TDECTRL+1, txpktbuf_bndy);

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
	value16 |= 0xF5B1;	
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
	IN	PADAPTER padapter
	)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	u16			beQ, bkQ, viQ, voQ, mgtQ, hiQ;

	if (!pregistrypriv->wifi_spec){// typical setting
		beQ		= QUEUE_LOW;
		bkQ 		= QUEUE_LOW;
		viQ 		= QUEUE_NORMAL;
		voQ 		= QUEUE_HIGH;
		mgtQ 	= QUEUE_HIGH;
		hiQ 		= QUEUE_HIGH;
	}
	else {// for WMM
		beQ		= QUEUE_LOW;
		bkQ 		= QUEUE_NORMAL;
		viQ 		= QUEUE_NORMAL;
		voQ 		= QUEUE_HIGH;
		mgtQ 	= QUEUE_HIGH;
		hiQ 		= QUEUE_HIGH;
	}
	_InitNormalChipRegPriority(padapter,beQ,bkQ,viQ,voQ,mgtQ,hiQ);
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


static void _InitQueuePriority(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	_InitNormalChipQueuePriority(padapter);
}

static void _InitPageBoundary(PADAPTER padapter)
{
	// RX Page Boundary	
	u16 rxff_bndy = MAX_RX_DMA_BUFFER_SIZE_88E-1;

	rtw_write16(padapter, (REG_TRXFF_BNDY + 2), rxff_bndy);

}

static void _InitTransferPageSize(PADAPTER padapter)
{
	// Tx page size is always 128.

	u8 value8;
	value8 = _PSRX(PBP_128) | _PSTX(PBP_128);
	rtw_write8(padapter, REG_PBP, value8);	
}

void _InitDriverInfoSize(PADAPTER padapter, u8 drvInfoSize)
{
	rtw_write8(padapter, REG_RX_DRVINFO_SZ, drvInfoSize);
}

void _InitID(PADAPTER padapter)
{
	u32 i;
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);

	for (i = 0; i < 6; i++)
		rtw_write8(padapter, REG_MACID + i, pEEPROM->mac_addr[i]);
}

void _InitNetworkType(PADAPTER padapter)
{
	u32 value32;

	value32 = rtw_read32(padapter, REG_CR);

	// TODO: use the other function to set network type
//	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AD_HOC);
	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AP);

	rtw_write32(padapter, REG_CR, value32);
}

void _InitWMACSetting(PADAPTER padapter)
{
	u16 value16;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);


	pHalData->ReceiveConfig = RCR_AAP | RCR_APM | RCR_AM | RCR_AB | RCR_CBSSID_DATA | RCR_CBSSID_BCN | RCR_AMF | RCR_HTC_LOC_CTRL | RCR_APP_PHYSTS | RCR_APP_ICV | RCR_APP_MIC;

	rtw_write32(padapter, REG_RCR, pHalData->ReceiveConfig);

	// Accept all data frames
	value16 = 0xFFFF;
	rtw_write16(padapter, REG_RXFLTMAP2, value16);

	// 2010.09.08 hpfan
	// Since ADF is removed from RCR, ps-poll will not be indicate to driver,
	// RxFilterMap should mask ps-poll to gurantee AP mode can rx ps-poll.
	value16 = 0x400;
	rtw_write16(padapter, REG_RXFLTMAP1, value16);

	// Accept all management frames
	value16 = 0xFFFF;
	rtw_write16(padapter, REG_RXFLTMAP0, value16);
	
}

void _InitAdaptiveCtrl(PADAPTER padapter)
{
	u16	value16;
	u32	value32;

	// Response Rate Set
	value32 = rtw_read32(padapter, REG_RRSR);
	value32 &= ~RATE_BITMAP_ALL;
	value32 |= RATE_RRSR_CCK_ONLY_1M;
	rtw_write32(padapter, REG_RRSR, value32);

	// CF-END Threshold
	//m_spIoBase->rtw_write8(REG_CFEND_TH, 0x1);

	// SIFS (used in NAV)
	value16 = _SPEC_SIFS_CCK(0x10) | _SPEC_SIFS_OFDM(0x10);
	rtw_write16(padapter, REG_SPEC_SIFS, value16);

	// Retry Limit
	value16 = _LRL(0x30) | _SRL(0x30);
	rtw_write16(padapter, REG_RL, value16);
}	

void _InitEDCA(PADAPTER padapter)
{
	// Set Spec SIFS (used in NAV)
	rtw_write16(padapter, REG_SPEC_SIFS, 0x100a);
	rtw_write16(padapter, REG_MAC_SPEC_SIFS, 0x100a);

	// Set SIFS for CCK
	rtw_write16(padapter, REG_SIFS_CTX, 0x100a);

	// Set SIFS for OFDM
	rtw_write16(padapter, REG_SIFS_TRX, 0x100a);

	// TXOP
	rtw_write32(padapter, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtw_write32(padapter, REG_EDCA_BK_PARAM, 0x0000A44F);
	rtw_write32(padapter, REG_EDCA_VI_PARAM, 0x005EA324);
	rtw_write32(padapter, REG_EDCA_VO_PARAM, 0x002FA226);
}

void _InitRateFallback(PADAPTER padapter)
{
	// Set Data Auto Rate Fallback Retry Count register.
	rtw_write32(padapter, REG_DARFRC, 0x00000000);
	rtw_write32(padapter, REG_DARFRC+4, 0x10080404);
	rtw_write32(padapter, REG_RARFRC, 0x04030201);
	rtw_write32(padapter, REG_RARFRC+4, 0x08070605);

}

void _InitRetryFunction(PADAPTER padapter)
{
	u8	value8;

	value8 = rtw_read8(padapter, REG_FWHW_TXQ_CTRL);
	value8 |= EN_AMPDU_RTY_NEW;
	rtw_write8(padapter, REG_FWHW_TXQ_CTRL, value8);

	// Set ACK timeout
	rtw_write8(padapter, REG_ACKTO, 0x40);
}

static void HalRxAggr8188ESdio(PADAPTER padapter)
{
#if 1
	struct registry_priv *pregistrypriv;
	u8	valueDMATimeout;
	u8	valueDMAPageCount;


	pregistrypriv = &padapter->registrypriv;

	if (pregistrypriv->wifi_spec)
	{
		// 2010.04.27 hpfan
		// Adjust RxAggrTimeout to close to zero disable RxAggr, suggested by designer
		// Timeout value is calculated by 34 / (2^n)
		valueDMATimeout = 0x0f;
		valueDMAPageCount = 0x01;
	}
	else
	{
		valueDMATimeout = 0x06;
		//valueDMAPageCount = 0x0F;
		//valueDMATimeout = 0x0a;  
		valueDMAPageCount = 0x24;
	}

	rtw_write8(padapter, REG_RXDMA_AGG_PG_TH+1, valueDMATimeout);
	rtw_write8(padapter, REG_RXDMA_AGG_PG_TH, valueDMAPageCount);
#endif
}

void sdio_AggSettingRxUpdate(PADAPTER padapter)
{
#if 1
	HAL_DATA_TYPE *pHalData;
	u8 valueDMA;


	pHalData = GET_HAL_DATA(padapter);

	valueDMA = rtw_read8(padapter, REG_TRXDMA_CTRL);
	valueDMA |= RXDMA_AGG_EN;
	rtw_write8(padapter, REG_TRXDMA_CTRL, valueDMA);

#if 0
	switch (RX_PAGE_SIZE_REG_VALUE)
	{
		case PBP_64:
			pHalData->HwRxPageSize = 64;
			break;
		case PBP_128:
			pHalData->HwRxPageSize = 128;
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
			RT_TRACE(_module_hci_hal_init_c_, _drv_err_,
				("%s: RX_PAGE_SIZE_REG_VALUE definition is incorrect!\n", __FUNCTION__));
			break;
	}
#endif
#endif
}

void _initSdioAggregationSetting(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	// Tx aggregation setting
	//sdio_AggSettingTxUpdate(padapter);

	// Rx aggregation setting
	HalRxAggr8188ESdio(padapter);
	sdio_AggSettingRxUpdate(padapter);

	// 201/12/10 MH Add for USB agg mode dynamic switch.
	pHalData->UsbRxHighSpeedMode = _FALSE;
}


void _InitOperationMode(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	struct mlme_ext_priv *pmlmeext;
	u8				regBwOpMode = 0;
	u32				regRATR = 0, regRRSR = 0;
	u8				MinSpaceCfg;


	pHalData = GET_HAL_DATA(padapter);
	pmlmeext = &padapter->mlmeextpriv;

	//1 This part need to modified according to the rate set we filtered!!
	//
	// Set RRSR, RATR, and REG_BWOPMODE registers
	//
	switch(pmlmeext->cur_wireless_mode)
	{
		case WIRELESS_MODE_B:
			regBwOpMode = BW_OPMODE_20MHZ;
			regRATR = RATE_ALL_CCK;
			regRRSR = RATE_ALL_CCK;
			break;
		case WIRELESS_MODE_A:
//			RT_ASSERT(FALSE,("Error wireless a mode\n"));
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
#if 0
			if (padapter->bInHctTest)
			{
				regBwOpMode = BW_OPMODE_20MHZ;
				regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
				regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
			}
			else
#endif
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
//			RT_ASSERT(FALSE,("Error wireless mode"));
#if 0
			regBwOpMode = BW_OPMODE_5G;
			regRATR = RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
			regRRSR = RATE_ALL_OFDM_AG;
#endif
			break;

		default: //for MacOSX compiler warning.
			break;
	}

	rtw_write8(padapter, REG_BWOPMODE, regBwOpMode);

	// For Min Spacing configuration.
	switch(pHalData->rf_type)
	{
		case RF_1T2R:
		case RF_1T1R:
			RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Initializepadapter: RF_Type%s\n", (pHalData->rf_type==RF_1T1R? "(1T1R)":"(1T2R)")));
//			padapter->MgntInfo.MinSpaceCfg = (MAX_MSS_DENSITY_1T<<3);
			MinSpaceCfg = (MAX_MSS_DENSITY_1T << 3);
			break;
		case RF_2T2R:
		case RF_2T2R_GREEN:
			RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Initializepadapter:RF_Type(2T2R)\n"));
//			padapter->MgntInfo.MinSpaceCfg = (MAX_MSS_DENSITY_2T<<3);
			MinSpaceCfg = (MAX_MSS_DENSITY_2T << 3);
			break;
	}

//	rtw_write8(padapter, REG_AMPDU_MIN_SPACE, padapter->MgntInfo.MinSpaceCfg);
	rtw_write8(padapter, REG_AMPDU_MIN_SPACE, MinSpaceCfg);
}


void _InitBeaconParameters(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;


	pHalData = GET_HAL_DATA(padapter);

	rtw_write16(padapter, REG_BCN_CTRL, 0x1010);

	// TODO: Remove these magic number
	rtw_write16(padapter, REG_TBTT_PROHIBIT, 0x6404);// ms
	rtw_write8(padapter, REG_DRVERLYINT, DRIVER_EARLY_INT_TIME);//ms
	rtw_write8(padapter, REG_BCNDMATIM, BCN_DMA_ATIME_INT_TIME);

	// Suggested by designer timchen. Change beacon AIFS to the largest number
	// beacause test chip does not contension before sending beacon. by tynli. 2009.11.03
	rtw_write16(padapter, REG_BCNTCFG, 0x660F);

	
	pHalData->RegBcnCtrlVal = rtw_read8(padapter, REG_BCN_CTRL);
	pHalData->RegTxPause = rtw_read8(padapter, REG_TXPAUSE); 
	pHalData->RegFwHwTxQCtrl = rtw_read8(padapter, REG_FWHW_TXQ_CTRL+2);
	pHalData->RegReg542 = rtw_read8(padapter, REG_TBTT_PROHIBIT+2);
	pHalData->RegCR_1 = rtw_read8(padapter, REG_CR+1);
	
}

void _InitBeaconMaxError(PADAPTER padapter, BOOLEAN InfraMode)
{
#ifdef RTL8192CU_ADHOC_WORKAROUND_SETTING
	rtw_write8(padapter, REG_BCN_MAX_ERR, 0xFF);
#endif
}

void _InitInterrupt(PADAPTER padapter)
{

	//HISR write one to clear
	rtw_write32(padapter, REG_HISR_88E, 0xFFFFFFFF);
	
	// HIMR - turn all off
	rtw_write32(padapter, REG_HIMR_88E, 0);

	//
	// Initialize and enable SDIO Host Interrupt.
	//
	InitInterrupt8188ESdio(padapter);


	//
	// Initialize and enable system Host Interrupt.
	//
	//InitSysInterrupt8188ESdio(Adapter);//TODO:
	
	//
	// Enable SDIO Host Interrupt.
	//
	//EnableInterrupt8188ESdio(padapter);//Move to sd_intf_start()/stop
	
}

void _InitRDGSetting(PADAPTER padapter)
{
	rtw_write8(padapter, REG_RD_CTRL, 0xFF);
	rtw_write16(padapter, REG_RD_NAV_NXT, 0x200);
	rtw_write8(padapter, REG_RD_RESP_PKT_TH, 0x05);
}

#if (MP_DRIVER == 1  )
static void _InitRxSetting(PADAPTER padapter)
{
	rtw_write32(padapter, REG_MACID, 0x87654321);
	rtw_write32(padapter, 0x0700, 0x87654321);
}
#endif

static void _InitRFType(PADAPTER padapter)
{
	struct registry_priv *pregpriv = &padapter->registrypriv;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	//BOOLEAN is92CU = IS_92C_SERIAL(pHalData->VersionID);
	BOOLEAN	 is2T2R = IS_2T2R(pHalData->VersionID);

#if	DISABLE_BB_RF
	pHalData->rf_chip	= RF_PSEUDO_11N;
	return;
#endif

	pHalData->rf_chip	= RF_6052;

	//if (_FALSE == is92CU) {
	if(_FALSE == is2T2R){
		pHalData->rf_type = RF_1T1R;
		DBG_8192C("Set RF Chip ID to RF_6052 and RF type to 1T1R.\n");
		return;
	}

	// TODO: Consider that EEPROM set 92CU to 1T1R later.
	// Force to overwrite setting according to chip version. Ignore EEPROM setting.
	//pHalData->RF_Type = is92CU ? RF_2T2R : RF_1T1R;
	MSG_8192C("Set RF Chip ID to RF_6052 and RF type to %d.\n", pHalData->rf_type);
}

// Set CCK and OFDM Block "ON"
static void _BBTurnOnBlock(PADAPTER padapter)
{
#if (DISABLE_BB_RF)
	return;
#endif

	PHY_SetBBReg(padapter, rFPGA0_RFMOD, bCCKEn, 0x1);
	PHY_SetBBReg(padapter, rFPGA0_RFMOD, bOFDMEn, 0x1);
}

#if 0
static void _InitAntenna_Selection(PADAPTER padapter)
{
	rtw_write8(padapter, REG_LEDCFG2, 0x82);
}
#endif

static void _InitPABias(PADAPTER padapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
	u8			pa_setting;
	BOOLEAN		isNormal = IS_NORMAL_CHIP(pHalData->VersionID);
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

	if(!(pa_setting & BIT1) && isNormal && is92C)
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

#if 0
VOID
_InitRDGSetting_8188E(
	IN	PADAPTER Adapter
	)
{
	PlatformEFIOWrite1Byte(Adapter,REG_RD_CTRL,0xFF);
	PlatformEFIOWrite2Byte(Adapter, REG_RD_NAV_NXT, 0x200);
	PlatformEFIOWrite1Byte(Adapter,REG_RD_RESP_PKT_TH,0x05);
}
#endif

static u32 rtl8188es_hal_init(PADAPTER padapter)
{
	s32 ret;
	u32 boundary;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv		*pwrctrlpriv = &padapter->pwrctrlpriv;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	u8 isNormal = IS_NORMAL_CHIP(pHalData->VersionID);
	u8 is92C = IS_92C_SERIAL(pHalData->VersionID);
	rt_rf_power_state	eRfPowerStateToSet;
	u8 value8;
	u16 value16;

	u32 init_start_time = rtw_get_current_time();

#ifdef DBG_HAL_INIT_PROFILING

	enum HAL_INIT_STAGES {
		HAL_INIT_STAGES_BEGIN = 0,
		HAL_INIT_STAGES_INIT_PW_ON,
		HAL_INIT_STAGES_INIT_LLTT,
		HAL_INIT_STAGES_MISC01,
		HAL_INIT_STAGES_MISC02,
		HAL_INIT_STAGES_DOWNLOAD_FW,
		HAL_INIT_STAGES_MAC,
		HAL_INIT_STAGES_BB,
		HAL_INIT_STAGES_RF,
		HAL_INIT_STAGES_TURN_ON_BLOCK,
		HAL_INIT_STAGES_INIT_SECURITY,
		HAL_INIT_STAGES_MISC11,
		HAL_INIT_STAGES_INIT_HAL_DM,
		HAL_INIT_STAGES_IQK,
		HAL_INIT_STAGES_PW_TRACK,
		HAL_INIT_STAGES_LCK,
		HAL_INIT_STAGES_INIT_PABIAS,
		HAL_INIT_STAGES_MISC31,
		HAL_INIT_STAGES_END,
		HAL_INIT_STAGES_NUM
	};

	char * hal_init_stages_str[] = {
		"HAL_INIT_STAGES_BEGIN",
		"HAL_INIT_STAGES_INIT_PW_ON",
		"HAL_INIT_STAGES_INIT_LLTT",
		"HAL_INIT_STAGES_MISC01",
		"HAL_INIT_STAGES_MISC02",
		"HAL_INIT_STAGES_DOWNLOAD_FW",
		"HAL_INIT_STAGES_MAC",
		"HAL_INIT_STAGES_BB",
		"HAL_INIT_STAGES_RF",
		"HAL_INIT_STAGES_TURN_ON_BLOCK",
		"HAL_INIT_STAGES_INIT_SECURITY",
		"HAL_INIT_STAGES_MISC11",
		"HAL_INIT_STAGES_INIT_HAL_DM",
		"HAL_INIT_STAGES_IQK",
		"HAL_INIT_STAGES_PW_TRACK",
		"HAL_INIT_STAGES_LCK",
		"HAL_INIT_STAGES_INIT_PABIAS",
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

	DBG_8192C("+rtl8188es_hal_init\n");

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_BEGIN);
	// Disable Interrupt first.
//	rtw_hal_disable_interrupt(padapter);
//	DisableInterrupt8188ESdio(padapter);
	
HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_PW_ON);
	ret = _InitPowerOn(padapter);
	if (_FAIL == ret) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init Power On!\n"));
		goto exit;
	}

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_LLTT);
	if (!pregistrypriv->wifi_spec) {
		boundary = TX_PAGE_BOUNDARY_88E;
	} else {
		// for WMM
		boundary = WMM_NORMAL_TX_PAGE_BOUNDARY_88E;
	}
	ret = InitLLTTable(padapter, boundary);
	if (_SUCCESS != ret) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init LLT Table!\n"));
		goto exit;
	}

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC01);
	_InitQueueReservedPage(padapter);
	_InitTxBufferBoundary(padapter);
	_InitQueuePriority(padapter);
	_InitPageBoundary(padapter);
	_InitTransferPageSize(padapter);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC02);
	// Get Rx PHY status in order to report RSSI and others.
	_InitDriverInfoSize(padapter, 4);
	_InitID(padapter);
	_InitNetworkType(padapter);
	_InitWMACSetting(padapter);
	_InitAdaptiveCtrl(padapter);
	_InitEDCA(padapter);
	_InitRateFallback(padapter);
	_InitRetryFunction(padapter);
	_initSdioAggregationSetting(padapter);
	_InitOperationMode(padapter);
	_InitBeaconParameters(padapter);
	_InitBeaconMaxError(padapter, _TRUE);
	_InitInterrupt(padapter);
	
#if (RATE_ADAPTIVE_SUPPORT==1)
	{//Enable TX Report
		//Enable Tx Report Timer   
		value8 = rtw_read8(padapter, REG_TX_RPT_CTRL);
		rtw_write8(padapter,  REG_TX_RPT_CTRL, (value8|BIT1|BIT0));
		//Set MAX RPT MACID
		rtw_write8(padapter,  REG_TX_RPT_CTRL+1, 2);//FOR sta mode ,0: bc/mc ,1:AP
		//Tx RPT Timer. Unit: 32us
		rtw_write16(padapter, REG_TX_RPT_TIME, 0xCdf0);
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

		value8 = rtw_read8(padapter, REG_EARLY_MODE_CONTROL);
#if RTL8188E_EARLY_MODE_PKT_NUM_10 == 1
		value8 = value8|0x1f;
#else
		value8 = value8|0xf;
#endif
		rtw_write8(padapter, REG_EARLY_MODE_CONTROL, value8);

		rtw_write8(padapter, REG_EARLY_MODE_CONTROL+3, 0x80);

		value8 = rtw_read8(padapter, REG_TCR+1);
		value8 = value8|0x40;
		rtw_write8(padapter,REG_TCR+1, value8);
	}
	else
#endif
	{
		rtw_write8(padapter, REG_EARLY_MODE_CONTROL, 0);
	}

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_DOWNLOAD_FW);
#if (MP_DRIVER == 1)

	_InitRxSetting(padapter);
	RT_TRACE(COMP_INIT, DBG_LOUD, ("InitializeAdapter8192CSdio(): Don't Download Firmware !!\n"));
	padapter->bFWReady = _FALSE;
	pHalData->fw_ractrl = _FALSE;
	
#else //MP_DRIVER != 1
#if 0
	padapter->bFWReady = _FALSE; //because no fw for test chip	
	pHalData->fw_ractrl = _FALSE;
#else
	ret = rtl8188e_FirmwareDownload(padapter);
	if (ret != _SUCCESS) {
		DBG_871X("%s: Download Firmware failed!!\n", __FUNCTION__);
		padapter->bFWReady = _FALSE;
		pHalData->fw_ractrl = _FALSE;
		goto exit;
	} else {
		RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Initializepadapter8192CSdio(): Download Firmware Success!!\n"));
		padapter->bFWReady = _TRUE;
		pHalData->fw_ractrl = _FALSE;
	}
#endif
#endif //MP_DRIVER == 1


	rtl8188e_InitializeFirmwareVars(padapter);

#if(SIC_ENABLE == 1)
	SIC_Init(padapter);
#endif


	if (pwrctrlpriv->reg_rfoff == _TRUE) {
		pwrctrlpriv->rf_pwrstate = rf_off;
	}

	// 2010/08/09 MH We need to check if we need to turnon or off RF after detecting
	// HW GPIO pin. Before PHY_RFConfig8192C.
	HalDetectPwrDownMode88E(padapter);


	// Set RF type for BB/RF configuration
	_InitRFType(padapter);

	// Save target channel
	// <Roger_Notes> Current Channel will be updated again later.
	pHalData->CurrentChannel = 1;

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MAC);
#if (HAL_MAC_ENABLE == 1)
	ret = PHY_MACConfig8188E(padapter);
	if(ret != _SUCCESS){
//		RT_TRACE(COMP_INIT, DBG_LOUD, ("Initializepadapter8192CSdio(): Fail to configure MAC!!\n"));
		goto exit;
	}
#endif

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_BB);
	//
	//d. Initialize BB related configurations.
	//
#if (HAL_BB_ENABLE == 1)
	ret = PHY_BBConfig8188E(padapter);
	if(ret != _SUCCESS){
//		RT_TRACE(COMP_INIT, DBG_SERIOUS, ("Initializepadapter8192CSdio(): Fail to configure BB!!\n"));
		goto exit;
	}
#endif


HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_RF);
	// 92CU use 3-wire to r/w RF
//	pHalData->Rf_Mode = RF_OP_By_SW_3wire;

	// The FW command register update must after MAC and FW init ready.
#if 0
	if (padapter->bFWReady == _TRUE)
	{
		if(pDevice->RegUsbSS)
		{
			H2C_SS_RFOFF_PARAM param;
			param.gpio_period = 500;
			param.ROFOn = 1;
			FillH2CCmd_88E(padapter, H2C_SELECTIVE_SUSPEND_ROF_CMD, sizeof(param), (pu8)(&param));
			RT_TRACE(COMP_INIT, DBG_LOUD,
			("SS Set H2C_CMD for FW detect GPIO time=%d\n", param.gpio_period));
		}
		else
			RT_TRACE(COMP_INIT, DBG_LOUD, ("Non-SS Driver detect GPIO by itself\n"));
	}
	else
	{
		RT_TRACE(COMP_INIT, DBG_LOUD, ("padapter->bFWReady == FALSE\n"));
	}
#endif
	// 2010/08/09 MH We need to check if we need to turnon or off RF after detecting
	// HW GPIO pin. Before PHY_RFConfig8192C.
	//HalDetectPwrDownMode(padapter);

	// If RF is on, we need to init RF. Otherwise, skip the procedure.
	// We need to follow SU method to change the RF cfg.txt. Default disable RF TX/RX mode.
	//if(pHalData->eRFPowerState == eRfOn)
	{
#if (HAL_RF_ENABLE == 1)
	ret = PHY_RFConfig8188E(padapter);

	if(ret != _SUCCESS){
//		RT_TRACE(COMP_INIT, DBG_LOUD, ("Initializepadapter8192CSdio(): Fail to configure RF!!\n"));
		goto exit;
	}

	//if(IS_VENDOR_UMC_A_CUT(pHalData->VersionID) && !IS_92C_SERIAL(pHalData->VersionID))
	//{
	//	PHY_SetRFReg(padapter, RF_PATH_A, RF_RX_G1, bMaskDWord, 0x30255);
	//	PHY_SetRFReg(padapter, RF_PATH_A, RF_RX_G2, bMaskDWord, 0x50a00);
	//}

#endif //HAL_RF_ENABLE == 1
	}

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_TURN_ON_BLOCK);
	_BBTurnOnBlock(padapter);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_SECURITY);
#if 1
	invalidate_cam_all(padapter);
#else
	CamResetAllEntry(padapter);
	padapter->HalFunc.EnableHWSecCfgHandler(padapter);
#endif

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC11);
	// 2010/12/17 MH We need to set TX power according to EFUSE content at first.
	PHY_SetTxPowerLevel8188E(padapter, pHalData->CurrentChannel);
	// Record original value for template. This is arough data, we can only use the data
	// for power adjust. The value can not be adjustde according to different power!!!
//	pHalData->OriginalCckTxPwrIdx = pHalData->CurrentCckTxPwrIdx;
//	pHalData->OriginalOfdm24GTxPwrIdx = pHalData->CurrentOfdm24GTxPwrIdx;

// Move by Neo for USB SS to below setp
//_RfPowerSave(padapter);
#if 0 //ANTENNA_SELECTION_STATIC_SETTING
#if 0
	if (!IS_92C_SERIAL( pHalData->VersionID) && (pHalData->AntDivCfg!=0))
#else
	if (IS_1T1R( pHalData->VersionID) && (pHalData->AntDivCfg!=0))
#endif
	{ //for 88CU ,1T1R
		_InitAntenna_Selection(padapter);
	}
#endif

	//
	// Disable BAR, suggested by Scott
	// 2010.04.09 add by hpfan
	//
	rtw_write32(padapter, REG_BAR_MODE_CTRL, 0x0201ffff);

	// HW SEQ CTRL
	// set 0x0 to 0xFF by tynli. Default enable HW SEQ NUM.
	rtw_write8(padapter, REG_HWSEQ_CTRL, 0xFF);


#ifdef RTL8188ES_MAC_LOOPBACK
	value8 = rtw_read8(padapter, REG_SYS_FUNC_EN);
	value8 &= ~(FEN_BBRSTB|FEN_BB_GLB_RSTn);
	rtw_write8(padapter, REG_SYS_FUNC_EN, value8);//disable BB, CCK/OFDM

	rtw_write8(padapter, REG_RD_CTRL, 0x0F);
	rtw_write8(padapter, REG_RD_CTRL+1, 0xCF);
	//rtw_write8(padapter, REG_TXPKTBUF_WMAC_LBK_BF_HD, 0x80);//to check _InitPageBoundary()
	rtw_write32(padapter, REG_CR, 0x0b0202ff);//0x100[28:24]=0x01011, enable mac loopback, no HW Security Eng.
#endif

	//
	// Configure SDIO TxRx Control to enable Rx DMA timer masking.
	// 2010.02.24.
	//
	value8 = SdioLocalCmd52Read1Byte(padapter, SDIO_REG_TX_CTRL);
	SdioLocalCmd52Write1Byte(padapter, SDIO_REG_TX_CTRL, 0x02);


#if (MP_DRIVER == 1)
	padapter->mppriv.channel = pHalData->CurrentChannel;
	MPT_InitializeAdapter(padapter, padapter->mppriv.channel);
#else //(MP_DRIVER == 1)

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
	RT_CLEAR_PS_LEVEL(pwrctrlpriv, RT_RF_OFF_LEVL_HALT_NIC);

	// 20100326 Joseph: Copy from GPIOChangeRFWorkItemCallBack() function to check HW radio on/off.
	// 20100329 Joseph: Revise and integrate the HW/SW radio off code in initialization.
//	pHalData->bHwRadioOff = _FALSE;
	pwrctrlpriv->b_hw_radio_off = _FALSE;
	eRfPowerStateToSet = rf_on;

	// 2010/-8/09 MH For power down module, we need to enable register block contrl reg at 0x1c.
	// Then enable power down control bit of register 0x04 BIT4 and BIT15 as 1.
	if(pHalData->pwrdown && eRfPowerStateToSet == rf_off)
	{
		// Enable register area 0x0-0xc.
		rtw_write8(padapter, REG_RSV_CTRL, 0x0);

		//
		// <Roger_Notes> We should configure HW PDn source for WiFi ONLY, and then
		// our HW will be set in power-down mode if PDn source from all  functions are configured.
		// 2010.10.06.
		//
		if(IS_HARDWARE_TYPE_8723AS(padapter))
		{
			value8 = rtw_read8(padapter, REG_MULTI_FUNC_CTRL);
			rtw_write8(padapter, REG_MULTI_FUNC_CTRL, (value8|WL_HWPDN_EN));
		}
		else
		{
			rtw_write16(padapter, REG_APS_FSMCO, 0x8812);
		}
	}
	//DrvIFIndicateCurrentPhyStatus(padapter); // 2010/08/17 MH Disable to prevent BSOD.

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_HAL_DM);
//	InitHalDm(padapter);
	rtl8188e_InitHalDm(padapter);
	

	// 2010/08/26 MH Merge from 8192CE.
	if(pwrctrlpriv->rf_pwrstate == rf_on)
	{
	
HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_IQK);
		if(pHalData->odmpriv.RFCalibrateInfo.bIQKInitialized){
//			PHY_IQCalibrate(padapter, _TRUE);
			PHY_IQCalibrate_8188E(padapter,_TRUE);
		}
		else
		{
//			PHY_IQCalibrate(padapter, _FALSE);
			PHY_IQCalibrate_8188E(padapter,_FALSE);
			pHalData->odmpriv.RFCalibrateInfo.bIQKInitialized = _TRUE;
		}

//		dm_CheckTXPowerTracking(padapter);
//		PHY_LCCalibrate(padapter);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_PW_TRACK);
		ODM_TXPowerTrackingCheck(&pHalData->odmpriv );

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_LCK);
		PHY_LCCalibrate_8188E(padapter);

		
	}

#endif //(MP_DRIVER == 1)


HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_PABIAS);
	//if(pHalData->eRFPowerState == eRfOn)
	{
		_InitPABias(padapter);
	}

	// Init BT hw config.
//	HALBT_InitHwConfig(padapter);


HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC31);
	// 2010/05/20 MH We need to init timer after update setting. Otherwise, we can not get correct inf setting.
	// 2010/05/18 MH For SE series only now. Init GPIO detect time
#if 0
	if(pDevice->RegUsbSS)
	{
		RT_TRACE(COMP_INIT, DBG_LOUD, (" call GpioDetectTimerStart\n"));
		GpioDetectTimerStart(padapter);	// Disable temporarily
	}
#endif

	// 2010/08/23 MH According to Alfred's suggestion, we need to to prevent HW enter
	// suspend mode automatically.
	//HwSuspendModeEnable92Cu(padapter, FALSE);

	// 2010/12/17 MH For TX power level OID modification from UI.
//	padapter->HalFunc.GetTxPowerLevelHandler( padapter, &pHalData->DefaultTxPwrDbm );
	//DbgPrint("pHalData->DefaultTxPwrDbm = %d\n", pHalData->DefaultTxPwrDbm);

//	if(pHalData->SwBeaconType < HAL92CSDIO_DEFAULT_BEACON_TYPE) // The lowest Beacon Type that HW can support
//		pHalData->SwBeaconType = HAL92CSDIO_DEFAULT_BEACON_TYPE;

	//
	// Update current Tx FIFO page status.
	//
	HalQueryTxBufferStatus8189ESdio(padapter);

	// Enable MACTXEN/MACRXEN block
	value16 = rtw_read16(padapter, REG_CR);
	value16 |= (MACTXEN | MACRXEN);
	rtw_write8(padapter, REG_CR, value16);

	if(pregistrypriv->wifi_spec)
		rtw_write16(padapter,REG_FAST_EDCA_CTRL ,0);


	//TODO:Setting HW_VAR_NAV_UPPER !!!!!!!!!!!!!!!!!!!!
	//rtw_hal_set_hwreg(Adapter, HW_VAR_NAV_UPPER, ((pu1Byte)&NavUpper));

	if(IS_HARDWARE_TYPE_8188ES(padapter))
	{
		value8= rtw_read8(padapter, 0x4d3);
		rtw_write8(padapter, 0x4d3, (value8|0x1));		
	}

	//pHalData->PreRpwmVal = PlatformEFSdioLocalCmd52Read1Byte(Adapter, SDIO_REG_HRPWM1)&0x80;


	// enable Tx report.
	rtw_write8(padapter,  REG_FWHW_TXQ_CTRL+1, 0x0F);
/*
	// Suggested by SD1 pisa. Added by tynli. 2011.10.21.
	PlatformEFIOWrite1Byte(Adapter, REG_EARLY_MODE_CONTROL+3, 0x01);

*/	//tynli_test_tx_report.
	rtw_write16(padapter, REG_TX_RPT_TIME, 0x3DF0);
	//RT_TRACE(COMP_INIT, DBG_TRACE, ("InitializeAdapter8188EUsb() <====\n"));

	rtw_write8(padapter, SDIO_LOCAL_BASE|SDIO_REG_HRPWM1, 0);

//#debug print for checking compile flags
	//DBG_8192C("RTL8188E_FPGA_TRUE_PHY_VERIFICATION=%d\n", RTL8188E_FPGA_TRUE_PHY_VERIFICATION);
	DBG_8192C("DISABLE_BB_RF=%d\n", DISABLE_BB_RF);	
	DBG_8192C("IS_HARDWARE_TYPE_8188ES=%d\n", IS_HARDWARE_TYPE_8188ES(padapter));
//#

	//RT_TRACE(COMP_INIT, DBG_LOUD, ("<---Initializepadapter8192CSdio()\n"));
	DBG_8192C("-rtl8188es_hal_init\n");

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

	return ret;
	
}

static void CardDisableRTL8188ESdio(PADAPTER padapter)
{
	u8		u1bTmp;
	u16		u2bTmp;
	u32		u4bTmp;
	u8		bMacPwrCtrlOn;
	u8		ret;

	DBG_871X("=>%s\n", __FUNCTION__);



	//Stop Tx Report Timer. 0x4EC[Bit1]=b'0
	u1bTmp = rtw_read8(padapter, REG_TX_RPT_CTRL);
	rtw_write8(padapter, REG_TX_RPT_CTRL, u1bTmp&(~BIT1));
	
	// stop rx 
	rtw_write8(padapter,REG_CR, 0x0);


	// Run LPS WL RFOFF flow	
	ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, Rtl8188E_NIC_LPS_ENTER_FLOW);
	if (ret == _FALSE) {
		DBG_871X("%s: run RF OFF flow fail!\n", __func__);
	}

	//	==== Reset digital sequence   ======

	u1bTmp = rtw_read8(padapter, REG_MCUFWDL);
	if ((u1bTmp & RAM_DL_SEL) && padapter->bFWReady) //8051 RAM code
	{
		//rtl8723a_FirmwareSelfReset(padapter);
		//_8051Reset88E(padapter);		
		
		// Reset MCU 0x2[10]=0.
		u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
		u1bTmp &= ~BIT(2);	// 0x2[10], FEN_CPUEN
		rtw_write8(padapter, REG_SYS_FUNC_EN+1, u1bTmp);
	}	

	//u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
	//u1bTmp &= ~BIT(2);	// 0x2[10], FEN_CPUEN
	//rtw_write8(padapter, REG_SYS_FUNC_EN+1, u1bTmp);

	// MCUFWDL 0x80[1:0]=0
	// reset MCU ready status
	rtw_write8(padapter, REG_MCUFWDL, 0);

	//==== Reset digital sequence end ======
	

	bMacPwrCtrlOn = _FALSE;	// Disable CMD53 R/W
	rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);


/*
	if((pMgntInfo->RfOffReason & RF_CHANGE_BY_HW) && pHalData->pwrdown)
	{// Power Down
		
		// Card disable power action flow
		ret = HalPwrSeqCmdParsing(Adapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, Rtl8188E_NIC_PDN_FLOW);	
	}	
	else
*/	
	{ // Non-Power Down

		// Card disable power action flow
		ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, Rtl8188E_NIC_DISABLE_FLOW);
	}
		 
	if (ret == _FALSE) {
		DBG_871X("%s: run CARD DISABLE flow fail!\n", __func__);
	}

/*
	// Reset MCU IO Wrapper, added by Roger, 2011.08.30
	u1bTmp = rtw_read8(padapter, REG_RSV_CTRL+1);
	u1bTmp &= ~BIT(0);
	rtw_write8(padapter, REG_RSV_CTRL+1, u1bTmp);
	u1bTmp = rtw_read8(padapter, REG_RSV_CTRL+1);
	u1bTmp |= BIT(0);
	rtw_write8(padapter, REG_RSV_CTRL+1, u1bTmp);
*/

#if 1
	// For Power Consumption.
	u1bTmp = rtw_read8(padapter, GPIO_IN);
	rtw_write8(padapter, GPIO_OUT, u1bTmp);
	rtw_write8(padapter, GPIO_IO_SEL, 0xFF);//Reg0x46

	u1bTmp = rtw_read8(padapter, REG_GPIO_IO_SEL);
	//rtw_write8(padapter, REG_GPIO_IO_SEL, (u1bTmp<<4)|u1bTmp);
	rtw_write8(padapter, REG_GPIO_IO_SEL, (u1bTmp<<4));
	u1bTmp = rtw_read8(padapter, REG_GPIO_IO_SEL+1);
	rtw_write8(padapter, REG_GPIO_IO_SEL+1, u1bTmp|0x0F);//Reg0x43
#endif

	// RSV_CTRL 0x1C[7:0]=0x0E
	// lock ISO/CLK/Power control register
	rtw_write8(padapter, REG_RSV_CTRL, 0x0E);


	DBG_871X("<=%s\n", __FUNCTION__);

}

static u32 rtl8188es_hal_deinit(PADAPTER padapter)
{
	DBG_871X("=>%s\n", __FUNCTION__);

	if (padapter->hw_init_completed == _TRUE)
		CardDisableRTL8188ESdio(padapter);


	DBG_871X("<=%s\n", __FUNCTION__);

	return _SUCCESS;
}

static u32 rtl8188es_inirp_init(PADAPTER padapter)
{
	u32 status;

_func_enter_;

	status = _SUCCESS;

_func_exit_;

	return status;
}

static u32 rtl8188es_inirp_deinit(PADAPTER padapter)
{
	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("+rtl8188es_inirp_deinit\n"));

	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("-rtl8188es_inirp_deinit\n"));

	return _SUCCESS;
}

static void rtl8188es_init_default_value(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	struct pwrctrl_priv *pwrctrlpriv;
	struct dm_priv *pdmpriv;
	u8 i;

	pHalData = GET_HAL_DATA(padapter);
	pwrctrlpriv = &padapter->pwrctrlpriv;
	pdmpriv = &pHalData->dmpriv;


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

	// interface related variable
	pHalData->SdioRxFIFOCnt = 0;
}


static void rtl8188es_interface_configure(PADAPTER padapter)
{
	HAL_DATA_TYPE *pHalData;
	struct registry_priv *pregistrypriv;
	u8 bWIFICfg;


	pHalData = GET_HAL_DATA(padapter);
	pregistrypriv = &padapter->registrypriv;
	bWIFICfg = pregistrypriv->wifi_spec;

	pHalData->OutEpQueueSel = TX_SELE_HQ|TX_SELE_NQ|TX_SELE_LQ;
	pHalData->OutEpNumber = SDIO_MAX_TX_QUEUE;
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
static void _EfuseCellSel(
	IN	PADAPTER	padapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	u32			value32;

	//if(INCLUDE_MULTI_FUNC_BT(padapter))
	{
		value32 = rtw_read32(padapter, EFUSE_TEST);
		value32 = (value32 & ~EFUSE_SEL_MASK) | EFUSE_SEL(EFUSE_WIFI_SEL_0);
		rtw_write32(padapter, EFUSE_TEST, value32);
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

static void
Hal_EfuseParsePIDVID_8188ES(
	IN	PADAPTER		pAdapter,
	IN	u8*			hwinfo,
	IN	BOOLEAN			AutoLoadFail
	)
{
//	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	//
	// <Roger_Notes> The PID/VID info was parsed from CISTPL_MANFID Tuple in CIS area before.
	// VID is parsed from Manufacture code field and PID is parsed from Manufacture information field.
	// 2011.04.01.
	//

//	RT_TRACE(COMP_INIT, DBG_LOUD, ("EEPROM VID = 0x%4x\n", pHalData->EEPROMVID));
//	RT_TRACE(COMP_INIT, DBG_LOUD, ("EEPROM PID = 0x%4x\n", pHalData->EEPROMPID));
}

static void
Hal_EfuseParseMACAddr_8188ES(
	IN	PADAPTER		padapter,
	IN	u8*			hwinfo,
	IN	BOOLEAN			AutoLoadFail
	)
{
	u16			i, usValue;
	u8			sMacAddr[6] = {0x00, 0xE0, 0x4C, 0x81, 0x88, 0x77};
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
		_rtw_memcpy(pEEPROM->mac_addr, &hwinfo[EEPROM_MAC_ADDR_88ES], ETH_ALEN);

	}
//	NicIFSetMacAddress(pAdapter, pAdapter->PermanentAddress);

	DBG_871X("Hal_EfuseParseMACAddr_8188ES: Permanent Address = %02x-%02x-%02x-%02x-%02x-%02x\n",
		  pEEPROM->mac_addr[0], pEEPROM->mac_addr[1],
		  pEEPROM->mac_addr[2], pEEPROM->mac_addr[3],
		  pEEPROM->mac_addr[4], pEEPROM->mac_addr[5]);
}



static VOID
readAdapterInfo(
	IN PADAPTER			padapter
	)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
//	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8			hwinfo[HWSET_MAX_SIZE_88E];

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("====> readpadapterInfo_8723S()\n"));

	//
	// This part read and parse the eeprom/efuse content
	//	
	Hal_InitPGData88E(padapter, hwinfo);
	Hal_EfuseParseIDCode88E(padapter, hwinfo);
	
	Hal_EfuseParsePIDVID_8188ES(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseMACAddr_8188ES(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	
	Hal_ReadPowerSavingMode88E(padapter, hwinfo, pEEPROM->bautoload_fail_flag);	
	Hal_ReadTxPowerInfo88E(padapter, hwinfo, pEEPROM->bautoload_fail_flag);	
	Hal_EfuseParseEEPROMVer88E(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	rtl8188e_EfuseParseChnlPlan(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseXtal_8188E(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseCustomerID88E(padapter, hwinfo, pEEPROM->bautoload_fail_flag);	
	//Hal_ReadAntennaDiversity88E(padapter, hwinfo, pEEPROM->bautoload_fail_flag);	
	Hal_EfuseParseBoardType88E(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_ReadThermalMeter_88E(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	//
	// The following part initialize some vars by PG info.
	//
	Hal_InitChannelPlan(padapter);	

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("<==== readpadapterInfo_8723S()\n"));
}

static void _ReadPROMContent(
	IN PADAPTER 		padapter
	)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
//	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8			eeValue;


	eeValue = rtw_read8(padapter, REG_9346CR);
	// To check system boot selection.
	pEEPROM->EepromOrEfuse = (eeValue & BOOT_FROM_EEPROM) ? _TRUE : _FALSE;
	pEEPROM->bautoload_fail_flag = (eeValue & EEPROM_EN) ? _FALSE : _TRUE;

	DBG_871X("%s: 9346CR=0x%02X, Boot from %s, Autoload %s\n",
		  __FUNCTION__, eeValue,
		  (pEEPROM->EepromOrEfuse ? "EEPROM" : "EFUSE"),
		  (pEEPROM->bautoload_fail_flag ? "Fail" : "OK"));

//	pHalData->EEType = IS_BOOT_FROM_EEPROM(Adapter) ? EEPROM_93C46 : EEPROM_BOOT_EFUSE;

	readAdapterInfo(padapter);
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


}

//
//	Description:
//		Read HW adapter information by E-Fuse or EEPROM according CR9346 reported.
//
//	Assumption:
//		PASSIVE_LEVEL (SDIO interface)
//
//
static s32 _ReadAdapterInfo8188ES(PADAPTER padapter)
{
	u32 start;

		
	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("+_ReadAdapterInfo8188ES\n"));

	// before access eFuse, make sure card enable has been called
	if(_CardEnable(padapter) == _FAIL)
	{
		DBG_871X(KERN_ERR "%s: run power on flow fail\n", __func__);
		return _FAIL;	
	}

	start = rtw_get_current_time();
	
//	Efuse_InitSomeVar(Adapter);
//	pHalData->VersionID = ReadChipVersion(Adapter);
//	_EfuseCellSel(padapter);

	_ReadRFType(padapter);//rf_chip -> _InitRFType()
	_ReadPROMContent(padapter);
	
	// 2010/10/25 MH THe function must be called after borad_type & IC-Version recognize.
	//ReadSilmComboMode(Adapter);
	_InitOtherVariable(padapter);


	//MSG_8192C("%s()(done), rf_chip=0x%x, rf_type=0x%x\n",  __FUNCTION__, pHalData->rf_chip, pHalData->rf_type);
	MSG_8192C("<==== ReadAdapterInfo8188ES in %d ms\n", rtw_get_passing_time_ms(start));

	return _SUCCESS;
}

static void ReadAdapterInfo8188ES(PADAPTER padapter)
{
	// Read EEPROM size before call any EEPROM function
	padapter->EepromAddressSize = GetEEPROMSize8188E(padapter);

	_ReadAdapterInfo8188ES(padapter);
}

static void ResumeTxBeacon(PADAPTER padapter)
{
	HAL_DATA_TYPE*	pHalData = GET_HAL_DATA(padapter);

	// 2010.03.01. Marked by tynli. No need to call workitem beacause we record the value
	// which should be read from register to a global variable.

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("+ResumeTxBeacon\n"));

	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) | BIT6);
	pHalData->RegFwHwTxQCtrl |= BIT6;
	rtw_write8(padapter, REG_TBTT_PROHIBIT+1, 0xff);
	pHalData->RegReg542 |= BIT0;
	rtw_write8(padapter, REG_TBTT_PROHIBIT+2, pHalData->RegReg542);
}

static void StopTxBeacon(PADAPTER padapter)
{
	HAL_DATA_TYPE*	pHalData = GET_HAL_DATA(padapter);

	// 2010.03.01. Marked by tynli. No need to call workitem beacause we record the value
	// which should be read from register to a global variable.

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("+StopTxBeacon\n"));

	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) & (~BIT6));
	pHalData->RegFwHwTxQCtrl &= (~BIT6);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+1, 0x64);
	pHalData->RegReg542 &= ~(BIT0);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+2, pHalData->RegReg542);

	CheckFwRsvdPageContent(padapter);  // 2010.06.23. Added by tynli.
}

void EnableBcnSubFunc(PADAPTER padapter)
{
	SetBcnCtrlReg(padapter, 0, BIT1);
}

void DisableBcnSubFunc(PADAPTER padapter)
{
	SetBcnCtrlReg(padapter, BIT1, 0);
}

static void SetHwReg8188ES(PADAPTER Adapter, u8 variable, u8* val)
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
			{
				u8 mode = *((u8 *)val);
				u8 val8;

				val8 = rtw_read8(Adapter, MSR) & 0x0c; // NETYPE0
				val8 |= mode;
				rtw_write8(Adapter, MSR, val8);

				_InitBeaconMaxError(Adapter, ((_HW_STATE_STATION_ == mode) ? _TRUE : _FALSE));

				if((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_))
				{					
#ifdef CONFIG_INTERRUPT_BASED_TXBCN	
				#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
				rtw_write8(Adapter, REG_DRVERLYINT, 0x05);//restore early int time to 5ms
				UpdateInterruptMask8188ESdio(Adapter, 0, SDIO_HIMR_BCNERLY_INT_MSK);	
				#endif
				
				#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR		
				UpdateInterruptMask8188ESdio(Adapter, 0, (SDIO_HIMR_TXBCNOK_MSK|SDIO_HIMR_TXBCNERR_MSK));	
				#endif
					
#endif //CONFIG_INTERRUPT_BASED_TXBCN		
					StopTxBeacon(Adapter);
					EnableBcnSubFunc(Adapter);
				}
				else if((mode == _HW_STATE_ADHOC_) /*|| (mode == _HW_STATE_AP_)*/)
				{
					ResumeTxBeacon(Adapter);
					DisableBcnSubFunc(Adapter);
				}
				else if(mode == _HW_STATE_AP_)
				{
#ifdef CONFIG_INTERRUPT_BASED_TXBCN			
					#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
					UpdateInterruptMask8188ESdio(Adapter, SDIO_HIMR_BCNERLY_INT_MSK, 0);
					#endif

					#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR	
					UpdateInterruptMask8188ESdio(Adapter, (SDIO_HIMR_TXBCNOK_MSK|SDIO_HIMR_TXBCNERR_MSK), 0);
					#endif
					
#endif //CONFIG_INTERRUPT_BASED_TXBCN
					ResumeTxBeacon(Adapter);

//					rtw_write8(Adapter, REG_BCN_CTRL, 0x12);
//					pHalData->RegBcnCtrlVal = 0x12;
					SetBcnCtrlReg(Adapter, 0x12, ~0x12);

					//enable SW Beacon
					rtw_write32(Adapter, REG_CR, rtw_read32(Adapter, REG_CR)|BIT(8));

					//Set RCR
					//rtw_write32(padapter, REG_RCR, 0x70002a8e);//CBSSID_DATA must set to 0
					//rtw_write32(Adapter, REG_RCR, 0x7000228e);//CBSSID_DATA must set to 0
					rtw_write32(Adapter, REG_RCR, 0x7000208e);//CBSSID_DATA must set to 0, Reject ICV_ERROR packets
					//enable to rx data frame
					rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);
					//enable to rx ps-poll
					rtw_write16(Adapter, REG_RXFLTMAP1, 0x0400);

					//Beacon Control related register for first time
					rtw_write8(Adapter, REG_BCNDMATIM, 0x02); // 2ms //BCN DMA only for PCIE HW Beacon
					
					//rtw_write8(Adapter, REG_BCN_MAX_ERR, 0xFF);
					rtw_write8(Adapter, REG_ATIMWND, 0x0a); // 10ms
					rtw_write16(Adapter, REG_BCNTCFG, 0x00);
					rtw_write16(Adapter, REG_TBTT_PROHIBIT, 0x6404);

					//reset TSF
					rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(0));

					// enable TSF Function and disanle update TSF for if1
					SetBcnCtrlReg(Adapter, EN_BCN_FUNCTION|EN_TXBCN_RPT|BIT(4), 0);

					//BIT3 - If set 0, hw will clr bcnq when tx becon ok/fail or port 0
					rtw_write8(Adapter, REG_MBID_NUM, rtw_read8(Adapter, REG_MBID_NUM)|BIT(3));
					
				}
				else
				{
					RT_TRACE(_module_hci_hal_init_c_, _drv_warning_, ("Set HW_VAR_SET_OPMODE: No such media status. opmode 0x%x\n", mode));
				}
				
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
				rtl8188e_HalSetBrateCfg( Adapter, val, &BrateCfg );
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
				SetBcnCtrlReg(Adapter, EN_BCN_FUNCTION | EN_TXBCN_RPT, 0);
			}
			else
			{
				SetBcnCtrlReg(Adapter, 0, EN_BCN_FUNCTION | EN_TXBCN_RPT);
			}
			break;
		case HW_VAR_CORRECT_TSF:
			{
				u64	tsf;
				struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
				struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

				//f = pmlmeext->TSFValue - ((u32)pmlmeext->TSFValue % (pmlmeinfo->bcn_interval*1024)) -1024; //us
				tsf = pmlmeext->TSFValue - rtw_modular64(pmlmeext->TSFValue, (pmlmeinfo->bcn_interval*1024)) - 1024; //us

				if (((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) ||
					((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE))
				{
					//pHalData->RegTxPause |= STOP_BCNQ;BIT(6)
					//rtw_write8(Adapter, REG_TXPAUSE, (rtw_read8(Adapter, REG_TXPAUSE)|BIT(6)));
					StopTxBeacon(Adapter);
				}

				// disable related TSF function
				SetBcnCtrlReg(Adapter, 0, EN_BCN_FUNCTION);

				rtw_write32(Adapter, REG_TSFTR, tsf);
				rtw_write32(Adapter, REG_TSFTR+4, tsf>>32);

				// enable related TSF function
				SetBcnCtrlReg(Adapter, EN_BCN_FUNCTION, 0);


				if (((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) ||
					((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE))
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
			{
				//Set RCR to not to receive data frame when NO LINK state
				//rtw_write32(Adapter, REG_RCR, rtw_read32(padapter, REG_RCR) & ~RCR_ADF);
				rtw_write16(Adapter, REG_RXFLTMAP2,0x00);

				//reset TSF
				rtw_write8(Adapter, REG_DUAL_TSF_RST, (BIT(0)|BIT(1)));

				// disable update TSF
				SetBcnCtrlReg(Adapter, BIT(4), 0);
			}
			break;
		case HW_VAR_MLME_SITESURVEY:
			if(*((u8 *)val))//under sitesurvey
			{
				//config RCR to receive different BSSID & not to receive data frame
				//pHalData->ReceiveConfig &= (~(RCR_CBSSID_DATA | RCR_CBSSID_BCN));			
				u32 v = rtw_read32(Adapter, REG_RCR);
				v &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN );//| RCR_ADF
				rtw_write32(Adapter, REG_RCR, v);
				//reject all data frame
				rtw_write16(Adapter, REG_RXFLTMAP2,0x00);

				//disable update TSF
				SetBcnCtrlReg(Adapter, BIT(4), 0);
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
					SetBcnCtrlReg(Adapter, 0, BIT(4));
				}
				else if((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
				{
					rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_ADF);

					//enable update TSF
					SetBcnCtrlReg(Adapter, 0, BIT(4));
				}

				if((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
					rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_BCN);
				else
					rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);
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
					rtw_write16(Adapter, REG_RXFLTMAP2,0x00);
				}
				else if(type == 2) //sta add event call back
				{
					// enable update TSF
					SetBcnCtrlReg(Adapter, 0, BIT(4));

					if(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE))
					{
						RetryLimit = 0x7;
					}
				}

				rtw_write16(Adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);
			}
			break;
		case HW_VAR_BEACON_INTERVAL:
			rtw_write16(Adapter, REG_BCN_INTERVAL, *((u16 *)val));

#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			{
				struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
				struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
				u16 bcn_interval = 	*((u16 *)val);
				if((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE){
					printk("%s==> bcn_interval:%d, eraly_int:%d \n",__FUNCTION__,bcn_interval,bcn_interval>>1);				
					rtw_write8(Adapter, REG_DRVERLYINT, bcn_interval>>1);// 50ms for sdio 
				}
				else{
					
				}
			}
#endif//CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT

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
			podmpriv->SupportAbility = *((u8 *)val);
			break;
		case HW_VAR_DM_FUNC_OP:
			if(val[0])
			{// save dm flag
				podmpriv->BK_SupportAbility = podmpriv->SupportAbility;				
			}
			else
			{// restore dm flag
				podmpriv->SupportAbility = podmpriv->BK_SupportAbility;
			}
			break;
		case HW_VAR_DM_FUNC_SET:
			if(*((u32 *)val) == DYNAMIC_ALL_FUNC_ENABLE){
				pdmpriv->DMFlag = pdmpriv->InitDMFlag;
				podmpriv->SupportAbility = 	pdmpriv->InitODMFlag;
			}
			else{
				podmpriv->SupportAbility |= *((u32 *)val);
			}
			break;
		case HW_VAR_DM_FUNC_CLR:
			podmpriv->SupportAbility &= *((u32 *)val);
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
			rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH, *((u8 *)val));
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
				rtw_write8(Adapter, SDIO_LOCAL_BASE|SDIO_REG_HRPWM1, ps_state);
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
#ifdef CONFIG_P2P
		case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
			{
				u8	p2p_ps_state = (*(u8 *)val);
				//rtl8192c_set_p2p_ps_offload_cmd(Adapter, p2p_ps_state);
			}
			break;
#endif
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
//			rtl8192cu_trigger_gpio_0(Adapter);
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

				//DBG_8192C("==> HW_VAR_ANTENNA_DIVERSITY_SELECT , Ant_(%s)\n",(Optimum_antenna==2)?"A":"B");

				//PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, 0x300, Optimum_antenna);
				ODM_RA_Set_TxRPT_Time(podmpriv,min_rpt_time);	
			}
			break;
#endif

#ifdef CONFIG_SW_ANTENNA_DIVERSITY
		case HW_VAR_ANTENNA_DIVERSITY_LINK:
			//SwAntDivRestAfterLink8192C(Adapter);
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
					ODM_UpdateRxIdleAnt_88E(&pHalData->odmpriv, Ant);
					
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

				//RX DMA stop
				rtw_write32(Adapter,REG_RXPKT_NUM,(rtw_read32(Adapter,REG_RXPKT_NUM)|RW_RELEASE_EN));
				do{
					if(!(rtw_read32(Adapter,REG_RXPKT_NUM)&RXDMA_IDLE))
						break;
				}while(trycnt--);
				if(trycnt ==0)
					printk("Stop RX DMA failed...... \n");

				//RQPN Load 0
				rtw_write16(Adapter,REG_RQPN_NPQ,0x0);
				rtw_write32(Adapter,REG_RQPN,0x80000000);
				rtw_mdelay_os(10);

			}
			break;
			
		case HW_VAR_APFM_ON_MAC:
			pHalData->bMacPwrCtrlOn = *val;
			DBG_871X("%s: bMacPwrCtrlOn=%d\n", __func__, pHalData->bMacPwrCtrlOn);
			break;
#if (RATE_ADAPTIVE_SUPPORT == 1)
		case HW_VAR_TX_RPT_MAX_MACID:
			{
				u8 maxMacid = *val;
				printk("### MacID(%d),Set Max Tx RPT MID(%d)\n",maxMacid,maxMacid+1);
				rtw_write8(Adapter, REG_TX_RPT_CTRL+1, maxMacid+1);
			}
			break;
#endif	//  (RATE_ADAPTIVE_SUPPORT == 1)		
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
			
			break;
	}

_func_exit_;
}

static void GetHwReg8188ES(PADAPTER padapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE 	pHalData= GET_HAL_DATA(padapter);
	DM_ODM_T 		*podmpriv = &pHalData->odmpriv;
_func_enter_;

	switch (variable)
	{
		case HW_VAR_BASIC_RATE:
			*((u16*)val) = pHalData->BasicRateSet;
			break;

		case HW_VAR_TXPAUSE:
			val[0] = rtw_read8(padapter, REG_TXPAUSE);
			break;

		case HW_VAR_BCN_VALID:
			//BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2
			val[0] = (BIT0 & rtw_read8(padapter, REG_TDECTRL+2))?_TRUE:_FALSE;
			break;

		case HW_VAR_DM_FLAG:
			val[0] = podmpriv->SupportAbility;
			break;

		case HW_VAR_RF_TYPE:
			val[0] = pHalData->rf_type;
			break;

		case HW_VAR_FWLPS_RF_ON:
			{
				//When we halt NIC, we should check if FW LPS is leave.				
				if ((padapter->bSurpriseRemoved == _TRUE) ||
					(padapter->pwrctrlpriv.rf_pwrstate == rf_off))
				{
					// If it is in HW/SW Radio OFF or IPS state, we do not check Fw LPS Leave,
					// because Fw is unload.
					val[0] = _TRUE;
				}
				else
				{
					#if 1   // refine by scott
					u32	valCR;
                    		valCR = rtw_read8(padapter, REG_CR);
                   			valCR &= 0xC0;

                    		if(valCR)
                        			val[0] = _TRUE;
                    		else
						val[0] = _FALSE;
					#else   // refine by scott
					u32	valRCR;
					valRCR = rtw_read32(Adapter, REG_RCR);
					valRCR &= 0x00070000;
					if(valRCR)
						val[0] = _FALSE;
					else
						val[0] = _TRUE;
					#endif
				}
			}
			break;
#ifdef CONFIG_ANTENNA_DIVERSITY
		case HW_VAR_CURRENT_ANTENNA:
			val[0] = pHalData->CurAntenna;
			break;
#endif
		case HW_VAR_EFUSE_BYTES: // To get EFUE total used bytes, added by Roger, 2008.12.22.
			*((u16*)val) = pHalData->EfuseUsedBytes;
			break;

		case HW_VAR_APFM_ON_MAC:
			*val = pHalData->bMacPwrCtrlOn;
			break;
		case HW_VAR_CHK_HI_QUEUE_EMPTY:
			*val = ((rtw_read32(padapter, REG_HGQ_INFORMATION)&0x0000ff00)==0) ? _TRUE:_FALSE;
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
GetHalDefVar8188ESDIO(
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
			*((u8 *)pValue) = (pHalData->AntDivCfg==0)?_FALSE:_TRUE;
			#endif
			break;
		case HAL_DEF_CURRENT_ANTENNA:
#ifdef CONFIG_ANTENNA_DIVERSITY
			*(( u8*)pValue) = pHalData->CurAntenna;
#endif
			break;
		case HAL_DEF_DBG_DM_FUNC:
			*(( u32*)pValue) =pHalData->odmpriv.SupportAbility;
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
#endif //(POWER_TRAINING_ACTIVE==1)
			break;		

		case HW_VAR_MAX_RX_AMPDU_FACTOR:
			*(( u32*)pValue) = MAX_AMPDU_FACTOR_16K;
			break;

		case HW_DEF_RA_INFO_DUMP:
#if (RATE_ADAPTIVE_SUPPORT == 1)		
			{
				u8 entry_id = *((u8*)pValue);	
				if(check_fwstate(&Adapter->mlmepriv, _FW_LINKED)== _TRUE)
				{
					DBG_871X("============ RA status check ===================\n");
					printk("Mac_id:%d ,RateID = %d,RAUseRate = 0x%08x,RateSGI = %d, DecisionRate = 0x%02x ,PTStage = %d\n",
						entry_id,
						pHalData->odmpriv.RAInfo[entry_id].RateID,
						pHalData->odmpriv.RAInfo[entry_id].RAUseRate,
						pHalData->odmpriv.RAInfo[entry_id].RateSGI,
						pHalData->odmpriv.RAInfo[entry_id].DecisionRate,
						pHalData->odmpriv.RAInfo[entry_id].PTStage);
				}
			}
#endif // (RATE_ADAPTIVE_SUPPORT == 1)
			break;

		case HAL_DEF_DBG_DUMP_RXPKT:
			*(( u8*)pValue) = pHalData->bDumpRxPkt;
			break;
		case HAL_DEF_DBG_DUMP_TXPKT:
			*(( u8*)pValue) = pHalData->bDumpTxPkt;
			break;		
			
		default:
			//RT_TRACE(COMP_INIT, DBG_WARNING, ("GetHalDefVar8188ESDIO(): Unkown variable: %d!\n", eVariable));
			bResult = _FAIL;
			break;
	}

	return bResult;
}




//
//	Description:
//		Change default setting of specified variable.
//
u8
SetHalDefVar8188ESDIO(
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
				//else if(dm_func == 4){//disable BT coexistence
				//	pdmpriv->DMFlag &= (~DYNAMIC_FUNC_BT);
				//}
				else if(dm_func == 5){//disable antenna diversity
					podmpriv->SupportAbility  &= (~DYNAMIC_BB_ANT_DIV);
				}				
				else if(dm_func == 6){//turn on all dynamic func
					if(!(podmpriv->SupportAbility  & DYNAMIC_BB_DIG))
					{						
						DIG_T	*pDigTable = &podmpriv->DM_DigTable;
						pDigTable->CurIGValue= rtw_read8(Adapter,0xc50);	
					}
					//pdmpriv->DMFlag |= DYNAMIC_FUNC_BT;
					podmpriv->SupportAbility = DYNAMIC_ALL_FUNC_ENABLE;
					DBG_8192C("==> Turn on all dynamic function...\n");
				}			
			}
			break;			
		case HAL_DEF_DBG_DUMP_RXPKT:
			pHalData->bDumpRxPkt = *(( u8*)pValue);
			break;
		case HAL_DEF_DBG_DUMP_TXPKT:
			pHalData->bDumpTxPkt = *(( u8*)pValue);
			break;				
		default:
			//RT_TRACE(COMP_INIT, DBG_TRACE, ("SetHalDefVar819xUsb(): Unkown variable: %d!\n", eVariable));
			bResult = _FALSE;
			break;
	}

	return bResult;
}

void UpdateHalRAMask8188ESdio(PADAPTER padapter, u32 mac_id, u8 rssi_level)
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
	
	//mask &=0xffffffff;
	rate_bitmap = 0x0fffffff;	
#ifdef	CONFIG_ODM_REFRESH_RAMASK
	{				
		rate_bitmap = ODM_Get_Rate_Bitmap(&pHalData->odmpriv,mask,rssi_level);
		printk("%s => mac_id:%d, networkType:0x%02x, mask:0x%08x\n\t ==> rssi_level:%d, rate_bitmap:0x%08x\n",
			__FUNCTION__,mac_id,networkType,mask,rssi_level,rate_bitmap);
	}
#endif
	mask &= rate_bitmap; 
	
	
	init_rate = get_highest_rate_idx(mask)&0x3f;
	
	if(pHalData->fw_ractrl == _TRUE)
	{
		u8 arg = 0;

		//arg = (cam_idx-4)&0x1f;//MACID
		arg = mac_id&0x1f;//MACID
		
		arg |= BIT(7);
		
		if (shortGIrate==_TRUE)
			arg |= BIT(5);
		mask |= ((raid<<28)&0xf0000000);	
		DBG_871X("update raid entry, mask=0x%x, arg=0x%x\n", mask, arg);
		psta->ra_mask=mask;
#ifdef CONFIG_INTEL_PROXIM
		if(padapter->proximity.proxim_on ==_TRUE){
			arg &= ~BIT(6);
		}
		else {
			arg |= BIT(6);
		}
#endif //CONFIG_INTEL_PROXIM
		rtl8188e_set_raid_cmd(padapter, mask);	
		
	}
	else
	{	

#if(RATE_ADAPTIVE_SUPPORT == 1)	

		ODM_RA_UpdateRateInfo_8188E(
				&(pHalData->odmpriv),
				mac_id,
				raid, 
				mask,
				shortGIrate
				);

#endif		
	}


	//set ra_id
	psta->raid = raid;
	psta->init_rate = init_rate;


}


static VOID
_BeaconFunctionEnable(
	IN	PADAPTER		padapter,
	IN	BOOLEAN			Enable,
	IN	BOOLEAN			Linked
	)
{
	SetBcnCtrlReg(padapter, (BIT4 | BIT3 | BIT1), 0x00);
//	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("_BeaconFunctionEnable 0x550 0x%x\n", rtw_read8(padapter, 0x550)));

	rtw_write8(padapter, REG_RD_CTRL+1, 0x6F);
}

void SetBeaconRelatedRegisters8188ESdio(PADAPTER padapter)
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

	//
	// ATIM window
	//
	rtw_write16(padapter, REG_ATIMWND, 2);

	//
	// Beacon interval (in unit of TU).
	//
	rtw_write16(padapter, REG_BCN_INTERVAL, pmlmeinfo->bcn_interval);

	_InitBeaconParameters(padapter);

	rtw_write8(padapter, REG_SLOT, 0x09);

	//
	// Force beacon frame transmission even after receiving beacon frame from other ad hoc STA
	//
	//PlatformEFIOWrite1Byte(Adapter, BCN_ERR_THRESH, 0x0a); // We force beacon sent to prevent unexpect disconnect status in Ad hoc mode

	//
	// Reset TSF Timer to zero, added by Roger. 2008.06.24
	//
	value32 = rtw_read32(padapter, REG_TCR);
	value32 &= ~TSFRST;
	rtw_write32(padapter,  REG_TCR, value32);

	value32 |= TSFRST;
	rtw_write32(padapter, REG_TCR, value32);

	// TODO: Modify later (Find the right parameters)
	// NOTE: Fix test chip's bug (about contention windows's randomness)
//	if (OpMode == RT_OP_MODE_IBSS || OpMode == RT_OP_MODE_AP)
	if (check_fwstate(&padapter->mlmepriv, WIFI_ADHOC_STATE|WIFI_AP_STATE) == _TRUE)
	{
		rtw_write8(padapter, REG_RXTSF_OFFSET_CCK, 0x50);
		rtw_write8(padapter, REG_RXTSF_OFFSET_OFDM, 0x50);
	}

	_BeaconFunctionEnable(padapter, _TRUE, _TRUE);

	ResumeTxBeacon(padapter);
	SetBcnCtrlReg(padapter, BIT(1), 0);
}

void rtl8188es_set_hal_ops(PADAPTER padapter)
{
	struct hal_ops *pHalFunc = &padapter->HalFunc;

_func_enter_;

/*
	//set hardware operation functions
	padapter->HalData = rtw_zmalloc(sizeof(HAL_DATA_TYPE));
	if (padapter->HalData == NULL) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_,
			 ("can't alloc memory for HAL DATA\n"));		
	}
	padapter->hal_data_sz = sizeof(HAL_DATA_TYPE);
*/

	rtl8188e_set_hal_ops(pHalFunc);
	
	pHalFunc->hal_init = &rtl8188es_hal_init;
	pHalFunc->hal_deinit = &rtl8188es_hal_deinit;

	pHalFunc->inirp_init = &rtl8188es_inirp_init;
	pHalFunc->inirp_deinit = &rtl8188es_inirp_deinit;

	pHalFunc->init_xmit_priv = &rtl8188es_init_xmit_priv;
	pHalFunc->free_xmit_priv = &rtl8188es_free_xmit_priv;

	pHalFunc->init_recv_priv = &rtl8188es_init_recv_priv;
	pHalFunc->free_recv_priv = &rtl8188es_free_recv_priv;

	pHalFunc->InitSwLeds = &rtl8188es_InitSwLeds;
	pHalFunc->DeInitSwLeds = &rtl8188es_DeInitSwLeds;

	pHalFunc->init_default_value = &rtl8188es_init_default_value;
	pHalFunc->intf_chip_configure = &rtl8188es_interface_configure;
	pHalFunc->read_adapter_info = &ReadAdapterInfo8188ES;

	pHalFunc->enable_interrupt = &EnableInterrupt8188ESdio;
	pHalFunc->disable_interrupt = &DisableInterrupt8188ESdio;

	pHalFunc->SetHwRegHandler = &SetHwReg8188ES;
	pHalFunc->GetHwRegHandler = &GetHwReg8188ES;

	pHalFunc->GetHalDefVarHandler = &GetHalDefVar8188ESDIO;
 	pHalFunc->SetHalDefVarHandler = &SetHalDefVar8188ESDIO;

	pHalFunc->UpdateRAMaskHandler = &UpdateHalRAMask8188ESdio;
	pHalFunc->SetBeaconRelatedRegistersHandler = &SetBeaconRelatedRegisters8188ESdio;

	pHalFunc->hal_xmit = &rtl8188es_hal_xmit;
	pHalFunc->mgnt_xmit = &rtl8188es_mgnt_xmit;

#ifdef CONFIG_HOSTAPD_MLME
	pHalFunc->hostap_mgnt_xmit_entry = NULL;
//	pHalFunc->hostap_mgnt_xmit_entry = &rtl8192cu_hostap_mgnt_xmit_entry;
#endif

_func_exit_;

}

