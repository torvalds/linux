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
#include <HalPwrSeqCmd.h>
#include <Hal8723PwrSeq.h>
#include <rtl8723a_hal.h>
#include <rtl8723a_led.h>
#include <sdio_ops.h>


/*
 * Description:
 *	Call power on sequence to enable card
 *
 * Return:
 *	_SUCCESS	enable success
 *	_FAIL		enable fail
 */
static u8 CardEnable(PADAPTER padapter)
{
	u8 bMacPwrCtrlOn;
	u8 ret;


	padapter->HalFunc.GetHwRegHandler(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (bMacPwrCtrlOn == _FALSE)
	{
		// RSV_CTRL 0x1C[7:0] = 0x00
		// unlock ISO/CLK/Power control register
		rtw_write8(padapter, REG_RSV_CTRL, 0x0);

		ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, rtl8723A_card_enable_flow);
		if (ret == _SUCCESS) {
			u8 bMacPwrCtrlOn = _TRUE;
			padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
		}
	} else
		ret = _SUCCESS;

	return ret;
}

//static
u8 _InitPowerOn(PADAPTER padapter)
{
	u8 value8;
	u16 value16;
	u32 value32;
	u8 ret;
//	u8 bMacPwrCtrlOn;


	ret = CardEnable(padapter);
	if (ret == _FALSE) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_emerg_,
				("%s: run power on flow fail\n", __FUNCTION__));
		return _FAIL;
	}

	// Radio-Off Pin Trigger
	value8 = rtw_read8(padapter, REG_GPIO_INTM+1);
	value8 |= BIT(1); // Enable falling edge triggering interrupt
	rtw_write8(padapter, REG_GPIO_INTM+1, value8);
	value8 = rtw_read8(padapter, REG_GPIO_IO_SEL_2+1);
	value8 |= BIT(1);
	rtw_write8(padapter, REG_GPIO_IO_SEL_2+1, value8);

	// Enable power down and GPIO interrupt
	value16 = rtw_read16(padapter, REG_APS_FSMCO);
	value16 |= EnPDN; // Enable HW power down and RF on
	rtw_write16(padapter, REG_APS_FSMCO, value16);

	// Enable CMD53 R/W Operation
//	bMacPwrCtrlOn = _TRUE;
//	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);

	// Enable MAC DMA/WMAC/SCHEDULE/SEC block
	value16 = rtw_read16(padapter, REG_CR);
	value16 |= (HCI_TXDMA_EN | HCI_RXDMA_EN | TXDMA_EN | RXDMA_EN
				| PROTOCOL_EN | SCHEDULE_EN | ENSEC | CALTMR_EN);
	rtw_write16(padapter, REG_CR, value16);

	return _SUCCESS;
}

static void _InitQueueReservedPage(PADAPTER padapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	u32			outEPNum	= (u32)pHalData->OutEpNumber;
	u32			numHQ		= 0;
	u32			numLQ		= 0;
	u32			numNQ		= 0;
	u32			numPubQ;
	u32			value32;
	u8			value8;
	BOOLEAN			bWiFiConfig	= pregistrypriv->wifi_spec;
	//u32			txQPageNum, txQPageUnit,txQRemainPage;


	{ //for WMM

		numPubQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_PUBQ : NORMAL_PAGE_NUM_PUBQ;

		if (pHalData->OutEpQueueSel & TX_SELE_HQ)
		{
			numHQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_HPQ : NORMAL_PAGE_NUM_HPQ;
		}

		if (pHalData->OutEpQueueSel & TX_SELE_LQ)
		{
			numLQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_LPQ : NORMAL_PAGE_NUM_LPQ;
		}

		// NOTE: This step shall be proceed before writting REG_RQPN.
		if (pHalData->OutEpQueueSel & TX_SELE_NQ) {
				numNQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_NPQ : NORMAL_PAGE_NUM_NPQ;
		}
		value8 = (u8)_NPQ(numNQ);
		rtw_write8(padapter, REG_RQPN_NPQ, value8);
	}

	// TX DMA
	value32 = _HPQ(numHQ) | _LPQ(numLQ) | _PUBQ(numPubQ) | LD_RQPN;
	rtw_write32(padapter, REG_RQPN, value32);
}

static void _InitTxBufferBoundary(PADAPTER padapter)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	//u16	txdmactrl;
	u8	txpktbuf_bndy;

	if (!pregistrypriv->wifi_spec) {
		txpktbuf_bndy = TX_PAGE_BOUNDARY;
	} else {
		//for WMM
		txpktbuf_bndy = ( IS_81XXC_TEST_CHIP( pHalData->VersionID)) ? \
						WMM_TEST_TX_PAGE_BOUNDARY : WMM_NORMAL_TX_PAGE_BOUNDARY;
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

static VOID
_InitTestChipQueuePriority(
	IN	PADAPTER Adapter
	)
{
	u8	hq_sele ;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;


	if(IS_HARDWARE_TYPE_8723AS(Adapter))
		return;

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

static void _InitQueuePriority(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);

	if(IS_81XXC_TEST_CHIP(pHalData->VersionID))
	{
		_InitTestChipQueuePriority(padapter);
	}
	else
	{
		_InitNormalChipQueuePriority(padapter);
	}
}

static void _InitPageBoundary(PADAPTER padapter)
{
	// RX Page Boundary
	//srand(static_cast<unsigned int>(time(NULL)) );
	u16 rxff_bndy = 0x27FF;//(rand() % 1) ? 0x27FF : 0x23FF;

	rtw_write16(padapter, (REG_TRXFF_BNDY + 2), rxff_bndy);

	// TODO: ?? shall we set tx boundary?
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
#ifdef CONFIG_MAC_LOOPBACK_DRIVER
	pHalData->ReceiveConfig |= RCR_ADD3 | RCR_APWRMGT | RCR_ACRC32 | RCR_ADF;
#endif

	// some REG_RCR will be modified later by phy_ConfigMACWithHeaderFile()
	rtw_write32(padapter, REG_RCR, pHalData->ReceiveConfig);

	// Accept all multicast address
	rtw_write32(padapter, REG_MAR, 0xFFFFFFFF);
	rtw_write32(padapter, REG_MAR + 4, 0xFFFFFFFF);

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

static void HalRxAggr8723ASdio(PADAPTER padapter)
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
		valueDMAPageCount = 0x0F;
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
//	sdio_AggSettingTxUpdate(padapter);

	// Rx aggregation setting
	HalRxAggr8723ASdio(padapter);
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

void _InitInterrupt(PADAPTER padapter)
{
	// HISR - turn all off
	rtw_write32(padapter, REG_HISR, 0);

	// HIMR - turn all off
	rtw_write32(padapter, REG_HIMR, 0);

	//
	// Initialize and enable SDIO Host Interrupt.
	//
	InitInterrupt8723ASdio(padapter);

	//
	// Initialize and enable system Host Interrupt.
	//
	InitSysInterrupt8723ASdio(padapter);

	EnableInterrupt8723ASdio(padapter);
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
	BOOLEAN is92CU = IS_92C_SERIAL(pHalData->VersionID);


#if	DISABLE_BB_RF
	pHalData->rf_chip	= RF_PSEUDO_11N;
	return;
#endif

	pHalData->rf_chip	= RF_6052;

	if (_FALSE == is92CU) {
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

static void _RfPowerSave(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData;
//	PMGNT_INFO		pMgntInfo;
	struct pwrctrl_priv *pwrctrlpriv;
	u8				u1bTmp;
	rt_rf_power_state	eRfPowerStateToSet; 


#if (DISABLE_BB_RF)
	return;
#endif

	pHalData = GET_HAL_DATA(padapter);
//	pMgntInfo = &padapter->MgntInfo;
	pwrctrlpriv = &padapter->pwrctrlpriv;

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
	//Added by chiyokolin, 2011.10.12 for Tx
	rtw_write8(padapter, REG_TXPAUSE, 0x00);

	// 20100326 Joseph: Copy from GPIOChangeRFWorkItemCallBack() function to check HW radio on/off.
	// 20100329 Joseph: Revise and integrate the HW/SW radio off code in initialization.
#if 1
	pwrctrlpriv->b_hw_radio_off = _FALSE;
	eRfPowerStateToSet = rf_on;
#else
	eRfPowerStateToSet = (rt_rf_power_state)RfOnOffDetect(padapter);
	pMgntInfo->RfOffReason |= eRfPowerStateToSet==rf_on ? RF_CHANGE_BY_INIT : RF_CHANGE_BY_HW;
	pMgntInfo->RfOffReason |= (pMgntInfo->RegRfOff) ? RF_CHANGE_BY_SW : 0;

	if (pMgntInfo->RfOffReason & RF_CHANGE_BY_HW)
		pHalData->bHwRadioOff = _TRUE;

	if (pMgntInfo->RegRfOff == _TRUE)
	{ // User disable RF via registry.
		RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("%s: Turn off RF for RegRfOff\n", __FUNCTION__));
		MgntActSet_RF_State(padapter, rf_off, RF_CHANGE_BY_SW, _TRUE);

		if (padapter->bSlaveOfDMSP)
			return;

		// Those action will be discard in MgntActSet_RF_State because off the same state
//		for (eRFPath = 0; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
//			PHY_SetRFReg(padapter, (RF_RADIO_PATH_E)eRFPath, RF_BS_PA_APSET_G5_G8, 0xC00, 0x0);
	}
	else if (pMgntInfo->RfOffReason > RF_CHANGE_BY_PS)
	{ // H/W or S/W RF OFF before sleep.
		RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("%s: Turn off RF for RfOffReason(%x)\n", __FUNCTION__, pMgntInfo->RfOffReason));

		// Selective suspend mode Resume from S3/S4 CU need to enable RF and turn off again.		
		//MgntActSet_RF_State(padapter, rf_on, pMgntInfo->RfOffReason, _TRUE);
		pHalData->eRFPowerState = rf_on;
		MgntActSet_RF_State(padapter, rf_off, pMgntInfo->RfOffReason, _TRUE);

		// Those action will be discard in MgntActSet_RF_State because off the same state
//		for (eRFPath = 0; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
//			PHY_SetRFReg(padapter, (RF_RADIO_PATH_E)eRFPath, RF_BS_PA_APSET_G5_G8, 0xC00, 0x0);
	}
	else
	{
		// Perform GPIO polling to find out current RF state. added by Roger, 2010.04.09.
		if( RT_GetInterfaceSelection(padapter)==INTF_SEL2_MINICARD && 
			(padapter->MgntInfo.PowerSaveControl.bGpioRfSw)) 
		{
			RT_TRACE(_module_hci_hal_init_c_, _drv_notice_ ("%s: RF=%d \n", __FUNCTION__, eRfPowerStateToSet));
			if (eRfPowerStateToSet == rf_off)
			{				
				MgntActSet_RF_State(padapter, rf_off, RF_CHANGE_BY_HW, _TRUE);
				pHalData->bHwRadioOff = _TRUE;
			}
			else
			{
				pHalData->eRFPowerState = rf_off;
				pMgntInfo->RfOffReason = RF_CHANGE_BY_INIT;
				pHalData->bHwRadioOff = _FALSE;
				MgntActSet_RF_State(padapter, rf_on, pMgntInfo->RfOffReason, _TRUE);
				//DrvIFIndicateCurrentPhyStatus(padapter);
			}
		}
		else
		{
			pHalData->eRFPowerState = rf_off;
			pMgntInfo->RfOffReason = RF_CHANGE_BY_INIT;
			MgntActSet_RF_State(padapter, rf_on, pMgntInfo->RfOffReason, _TRUE);
			//DrvIFIndicateCurrentPhyStatus(padapter);
		}

		pMgntInfo->RfOffReason = 0; 
		pHalData->bHwRadioOff = _FALSE;
		pHalData->eRFPowerState = rf_on;
		padapter->HalFunc.LedControlHandler(padapter, LED_CTL_POWER_ON);
	}
#endif
	// 2010/-8/09 MH For power down module, we need to enable register block contrl reg at 0x1c.
	// Then enable power down control bit of register 0x04 BIT4 and BIT15 as 1.
	if (pHalData->pwrdown && eRfPowerStateToSet == rf_off)
	{
		// Enable register area 0x0-0xc.
		rtw_write8(padapter, REG_RSV_CTRL, 0x0);

		//
		// <Roger_Notes> We should configure HW PDn source for WiFi ONLY, and then
		// our HW will be set in power-down mode if PDn source from all  functions are configured.
		// 2010.10.06.
		//
		if (IS_HARDWARE_TYPE_8723AS(padapter))
		{
			u1bTmp = rtw_read8(padapter, REG_MULTI_FUNC_CTRL);
			u1bTmp |= WL_HWPDN_EN;
			rtw_write8(padapter, REG_MULTI_FUNC_CTRL, u1bTmp);
		}
		else
		{
			rtw_write16(padapter, REG_APS_FSMCO, 0x8812);
		}
	}
	//DrvIFIndicateCurrentPhyStatus(padapter);	
}

static void _InitAntenna_Selection(PADAPTER padapter)
{
	rtw_write8(padapter, REG_LEDCFG2, 0x82);
}

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
		PHY_SetRFReg(padapter, RF_PATH_B, 0x15, 0x0FFFFF, 0x0F406);
		PHY_SetRFReg(padapter, RF_PATH_B, 0x15, 0x0FFFFF, 0x4F406);
		PHY_SetRFReg(padapter, RF_PATH_B, 0x15, 0x0FFFFF, 0x8F406);
		PHY_SetRFReg(padapter, RF_PATH_B, 0x15, 0x0FFFFF, 0xCF406);
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

//
// 2010/08/09 MH Add for power down check.
//
static BOOLEAN HalDetectPwrDownMode(PADAPTER Adapter)
{
	u8 tmpvalue;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct pwrctrl_priv *pwrctrlpriv = &Adapter->pwrctrlpriv;


	EFUSE_ShadowRead(Adapter, 1, 0x7B/*EEPROM_RF_OPT3_92C*/, (u32 *)&tmpvalue);

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

static u32 rtl8723as_hal_init(PADAPTER padapter)
{
	s32 ret;
	u32 boundary;
	PHAL_DATA_TYPE pHalData;
	struct pwrctrl_priv *pwrctrlpriv;
	struct registry_priv *pregistrypriv;
	u8 is92C;
	rt_rf_power_state eRfPowerStateToSet;
	u32 NavUpper = WiFiNavUpperUs;
	u8 u1bTmp;
	u16 value16;


	pHalData = GET_HAL_DATA(padapter);
	pwrctrlpriv = &padapter->pwrctrlpriv;
	pregistrypriv = &padapter->registrypriv;
	is92C = IS_92C_SERIAL(pHalData->VersionID);

	// Disable Interrupt first.
//	padapter->HalFunc.disable_interrupt(padapter);

	ret = _InitPowerOn(padapter);
	if (_FAIL == ret) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init Power On!\n"));
		return _FAIL;
	}

//	padapter->HalFunc.HalRxAggrHandler(padapter, _TRUE);

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
	if (HALBT_IsBTExist(padapter))
	{
#if 0
#if OS_WIN_FROM_VISTA(OS_VERSION)
		RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("Slim_combo win7/vista need not enable SIC\n"));
#else
		RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("Slim_combo XP enable SIC\n"));
		// 2010/10/15 MH According to Alfre's description, e need to enable bit14 at first an then enable bit12.
		// Otherwise, HW will enter debug mode and 8051 can not work. We need to stay at test mode to enable SIC.
		rtw_write16(padapter, REG_GPIO_MUXCFG, rtw_read16(padapter, REG_GPIO_MUXCFG)|BIT14);
		rtw_write16(padapter, REG_GPIO_MUXCFG, rtw_read16(padapter, REG_GPIO_MUXCFG)|BIT12);
#endif
#endif
	}
#endif

	if (!pregistrypriv->wifi_spec) {
		boundary = TX_PAGE_BOUNDARY;
	} else {
		boundary = WMM_NORMAL_TX_PAGE_BOUNDARY; // for WMM
	}
	ret = InitLLTTable(padapter, boundary);
	if (_SUCCESS != ret) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init LLT Table!\n"));
		return _FAIL;
	}

	_InitQueueReservedPage(padapter);
	_InitTxBufferBoundary(padapter);
	_InitQueuePriority(padapter);
	_InitPageBoundary(padapter);
	_InitTransferPageSize(padapter);

	// Get Rx PHY status in order to report RSSI and others.
	_InitDriverInfoSize(padapter, DRVINFO_SZ);
	_InitID(padapter);
	_InitNetworkType(padapter);
	_InitWMACSetting(padapter);
	_InitAdaptiveCtrl(padapter);
	_InitEDCA(padapter);
	_InitRateFallback(padapter);
	_InitRetryFunction(padapter);
	_initSdioAggregationSetting(padapter);
	_InitOperationMode(padapter);
	rtl8723a_InitBeaconParameters(padapter);
	rtl8723a_InitBeaconMaxError(padapter, _TRUE);
	_InitInterrupt(padapter);

#if 0
	if(pHTInfo->bRDGEnable){
		_InitRDGSetting(Adapter);
	}

	if (pHalData->bEarlyModeEnable)
	{
		RT_TRACE(_module_hci_hal_init_c_, _drv_info_,("EarlyMode Enabled!!!\n"));

		u1bTmp = rtw_read8(padapter, REG_EARLY_MODE_CONTROL);
		u1bTmp |= 0xf;
		rtw_write8(padapter, REG_EARLY_MODE_CONTROL, u1bTmp);

		rtw_write8(padapter, REG_EARLY_MODE_CONTROL+3, 0x80);

		u1bTmp = rtw_read8(padapter, REG_TCR+1);
		u1bTmp |= 0x40;
		rtw_write8(padapter,REG_TCR+1, u1bTmp);
	}
	else
#endif
		rtw_write8(padapter, REG_EARLY_MODE_CONTROL, 0);

#if (MP_DRIVER == 1)
	_InitRxSetting(padapter);
	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("%s: Don't Download Firmware!!\n", __FUNCTION__));
	padapter->bFWReady = _FALSE;
#else
	ret = rtl8723a_FirmwareDownload(padapter);
	if (ret != _SUCCESS) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("%s: Download Firmware failed!!\n", __FUNCTION__));
		padapter->bFWReady = _FALSE;
		pHalData->fw_ractrl = _FALSE;
		return ret;
	} else {
		RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Initializepadapter8192CSdio(): Download Firmware Success!!\n"));
		padapter->bFWReady = _TRUE;
		pHalData->fw_ractrl = _TRUE;
	}
#endif

	rtl8723a_InitializeFirmwareVars(padapter);

//	SIC_Init(padapter);

	if (pwrctrlpriv->reg_rfoff == _TRUE) {
		pwrctrlpriv->rf_pwrstate = rf_off;
	}

	// 2010/08/09 MH We need to check if we need to turnon or off RF after detecting
	// HW GPIO pin. Before PHY_RFConfig8192C.
	HalDetectPwrDownMode(padapter);


	// Set RF type for BB/RF configuration
	_InitRFType(padapter);

	// Save target channel
	// <Roger_Notes> Current Channel will be updated again later.
	pHalData->CurrentChannel = 6;

#if (HAL_MAC_ENABLE == 1)
	ret = PHY_MACConfig8723A(padapter);
	if(ret != _SUCCESS){
//		RT_TRACE(COMP_INIT, DBG_LOUD, ("Initializepadapter8192CSdio(): Fail to configure MAC!!\n"));
		return ret;
	}
#endif

	//
	//d. Initialize BB related configurations.
	//
#if (HAL_BB_ENABLE == 1)
	ret = PHY_BBConfig8723A(padapter);
	if(ret != _SUCCESS){
//		RT_TRACE(COMP_INIT, DBG_SERIOUS, ("Initializepadapter8192CSdio(): Fail to configure BB!!\n"));
		return ret;
	}
#endif

	// The FW command register update must after MAC and FW init ready.
#if 0
	if (padapter->bFWReady == TRUE)
	{
		if(pDevice->RegUsbSS)
		{
			H2C_SS_RFOFF_PARAM param;
			param.gpio_period = 500;
			param.ROFOn = 1;
			FillH2CCmd92C(padapter, H2C_SELECTIVE_SUSPEND_ROF_CMD, sizeof(param), (pu8)(&param));
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

	// If RF is on, we need to init RF. Otherwise, skip the procedure.
	// We need to follow SU method to change the RF cfg.txt. Default disable RF TX/RX mode.
	//if(pHalData->eRFPowerState == eRfOn)
	{
#if (HAL_RF_ENABLE == 1)
		ret = PHY_RFConfig8723A(padapter);

		if(ret != _SUCCESS){
//			RT_TRACE(COMP_INIT, DBG_LOUD, ("Initializepadapter8192CSdio(): Fail to configure RF!!\n"));
			return ret;
		}

	if (IS_81xxC_VENDOR_UMC_A_CUT(pHalData->VersionID) && !IS_92C_SERIAL(pHalData->VersionID))
	{
		PHY_SetRFReg(padapter, RF_PATH_A, RF_RX_G1, bMaskDWord, 0x30255);
		PHY_SetRFReg(padapter, RF_PATH_A, RF_RX_G2, bMaskDWord, 0x50a00);
	}

#endif
	}

	_BBTurnOnBlock(padapter);
#if 0
#if RT_PLATFORM == PLATFORM_WINDOWS
	if(PlatformIsOverrideAddress(padapter))
		NicIFSetMacAddress(padapter, PlatformGetOverrideAddress(padapter));
	else
#endif
		NicIFSetMacAddress(padapter, padapter->PermanentAddress);

	if(padapter->ResetProgress == RESET_TYPE_NORESET){
		RT_TRACE(COMP_MLME, DBG_LOUD, ("Initializepadapter8192CSdio():RegWirelessMode(%#x) \n", padapter->RegWirelessMode));
		padapter->HalFunc.SetWirelessModeHandler(padapter, padapter->RegWirelessMode);
	}
#endif

#if 1
	invalidate_cam_all(padapter);
#else
	CamResetAllEntry(padapter);
	padapter->HalFunc.EnableHWSecCfgHandler(padapter);
#endif

	// 2010/12/17 MH We need to set TX power according to EFUSE content at first.
	PHY_SetTxPowerLevel8192C(padapter, pHalData->CurrentChannel);
	// Record original value for template. This is arough data, we can only use the data
	// for power adjust. The value can not be adjustde according to different power!!!
//	pHalData->OriginalCckTxPwrIdx = pHalData->CurrentCckTxPwrIdx;
//	pHalData->OriginalOfdm24GTxPwrIdx = pHalData->CurrentOfdm24GTxPwrIdx;

// Move by Neo for USB SS to below setp
//_RfPowerSave(padapter);

	rtl8723a_InitAntenna_Selection(padapter);

	//
	// Disable BAR, suggested by Scott
	// 2010.04.09 add by hpfan
	//
	rtw_write32(padapter, REG_BAR_MODE_CTRL, 0x0201ffff);

	// HW SEQ CTRL
	// set 0x0 to 0xFF by tynli. Default enable HW SEQ NUM.
	rtw_write8(padapter, REG_HWSEQ_CTRL, 0xFF);


#ifdef CONFIG_MAC_LOOPBACK_DRIVER
	u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN);
	u1bTmp &= ~(FEN_BBRSTB|FEN_BB_GLB_RSTn);
	rtw_write8(padapter, REG_SYS_FUNC_EN,u1bTmp);

	rtw_write8(padapter, REG_RD_CTRL, 0x0F);
	rtw_write8(padapter, REG_RD_CTRL+1, 0xCF);
	rtw_write8(padapter, REG_TXPKTBUF_WMAC_LBK_BF_HD, 0x80);
	rtw_write32(padapter, REG_CR, 0x0b0202ff);
#endif

	//
	// Configure SDIO TxRx Control to enable Rx DMA timer masking.
	// 2010.02.24.
	//
	SdioLocalCmd52Write1Byte(padapter, SDIO_REG_TX_CTRL, 0);

	_RfPowerSave(padapter);

//	RT_TRACE(COMP_INIT|COMP_MLME, DBG_LOUD, ("HighestOperaRate = %x\n", padapter->MgntInfo.HighestOperaRate));
#if 0
#if (0 == RTL8192SU_FPGA_UNSPECIFIED_NETWORK)
	PlatformStartWorkItem( &(pHalData->RtUsbCheckForHangWorkItem) );
#endif

#if SILENT_RESET
	PlatformStartWorkItem( &(pHalData->RtUsbCheckResetWorkItem) );
#endif
#endif
#if (MP_DRIVER == 1)
	padapter->mppriv.channel = pHalData->CurrentChannel;
	MPT_InitializeAdapter(padapter, padapter->mppriv.channel);
#else // MP_DRIVER != 1
	// 2010/08/26 MH Merge from 8192CE.
	if (pwrctrlpriv->rf_pwrstate == rf_on)
	{
		if (pHalData->bIQKInitialized)
//			PHY_IQCalibrate(padapter, _TRUE);
			rtl8192c_PHY_IQCalibrate(padapter,_TRUE);
		else
		{
//			PHY_IQCalibrate(padapter, _FALSE);
			rtl8192c_PHY_IQCalibrate(padapter,_FALSE);
			pHalData->bIQKInitialized = _TRUE;
		}

//		dm_CheckTXPowerTracking(padapter);
//		PHY_LCCalibrate(padapter);
		rtl8192c_odm_CheckTXPowerTracking(padapter);
		rtl8192c_PHY_LCCalibrate(padapter);

#ifdef CONFIG_BT_COEXIST
		rtl8723a_SingleDualAntennaDetection(padapter);
#endif
	}
#endif // MP_DRIVER != 1
#if 0
	//if(pHalData->eRFPowerState == eRfOn)
	{
		_InitPABias(padapter);
	}
#endif

#ifdef CONFIG_BT_COEXIST
	// Init BT hw config.
	HALBT_InitHwConfig(padapter);
#endif

//	InitHalDm(padapter);
	rtl8723a_InitHalDm(padapter);

	// 2010/05/20 MH We need to init timer after update setting. Otherwise, we can not get correct inf setting.
	// 2010/05/18 MH For SE series only now. Init GPIO detect time
#if 0
	if (pDevice->RegUsbSS)
	{
		RT_TRACE(COMP_INIT, DBG_LOUD, (" call GpioDetectTimerStart\n"));
		GpioDetectTimerStart(padapter);	// Disable temporarily
	}
#endif

	// 2010/12/17 MH For TX power level OID modification from UI.
//	padapter->HalFunc.GetTxPowerLevelHandler( padapter, &pHalData->DefaultTxPwrDbm );
	//DbgPrint("pHalData->DefaultTxPwrDbm = %d\n", pHalData->DefaultTxPwrDbm);

//	if(pHalData->SwBeaconType < HAL92CSDIO_DEFAULT_BEACON_TYPE) // The lowest Beacon Type that HW can support
//		pHalData->SwBeaconType = HAL92CSDIO_DEFAULT_BEACON_TYPE;

	//
	// Update current Tx FIFO page status.
	//
	HalQueryTxBufferStatus8723ASdio(padapter);

	// Enable MACTXEN/MACRXEN block
	u1bTmp = rtw_read8(padapter, REG_CR);
	u1bTmp |= (MACTXEN | MACRXEN);
	rtw_write8(padapter, REG_CR, u1bTmp);

	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_NAV_UPPER, (u8*)&NavUpper);

//	pHalData->PreRpwmVal = SdioLocalCmd52Read1Byte(padapter, SDIO_REG_HRPWM1) & 0x80;

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("-%s\n", __FUNCTION__));

	return _SUCCESS;
}
#if 0
static void rtl8723as_hw_power_down(PADAPTER padapter)
{
	// 2010/-8/09 MH For power down module, we need to enable register block contrl reg at 0x1c.
	// Then enable power down control bit of register 0x04 BIT4 and BIT15 as 1.

	// Enable register area 0x0-0xc.
	rtw_write8(padapter,REG_RSV_CTRL, 0x0);
	rtw_write16(padapter, REG_APS_FSMCO, 0x8812);
}

//
// Description: RTL8723e card disable power sequence v003 which suggested by Scott.
// First created by tynli. 2011.01.28.
//
static void PowerDownRTL8723ASdio(PADAPTER padapter)
{
	u8 v8;
	u32 retry;

	rtw_write8(padapter, REG_RF_CTRL, 0);

	v8 = rtw_read8(padapter, REG_LEDCFG2);
	v8 &= ~BIT(7);
	rtw_write8(padapter, REG_LEDCFG2, v8);

	v8 = rtw_read8(padapter, REG_APS_FSMCO+1);
	v8 |= BIT(1);
	rtw_write8(padapter, REG_APS_FSMCO+1, v8);

	retry = 0;
	do {
		v8 = rtw_read8(padapter, REG_APS_FSMCO+1);
		if (!(v8 & BIT(1))) break;
		retry++;
		if (retry == 1000) break;
	} while (1);
	if (retry == 1000)
		printk(KERN_ERR "%s: can't wait REG_APS_FSMCO BIT9 to 0! (0x%02x)\n", __func__, v8);
	
	v8 = rtw_read8(padapter, REG_APS_FSMCO+2);
	v8 &= ~BIT(0);
	rtw_write8(padapter, REG_APS_FSMCO+2, v8);

	v8 = rtw_read8(padapter, REG_APS_FSMCO+1);
	v8 |= BIT(7);
	rtw_write8(padapter, REG_APS_FSMCO+1, v8);
}

//
// Description: RTL8723e card disable power sequence v003 which suggested by Scott.
// First created by tynli. 2011.01.28.
//
static void LPSRadioOffRTL8723ASdio(PADAPTER padapter)
{
	u8	u1bTmp;
	u32 v32;
	u32 retry;

	// 1. 0x522[7:0] = 0xFF	// TX pause
	rtw_write8(padapter, REG_TXPAUSE, 0x7F);

	retry = 0;
	v32 = 0;
	do {
		v32 = rtw_read32(padapter, 0x5F8);
		if (v32 == 0) break;
		retry++;
		if (retry == 1000) break;
	} while (1);
	if (retry == 1000)
		printk(KERN_ERR "%s: polling 0x5F8 to 0 fail! (0x%08x)\n", __func__, v32);

	// 2. 0x02[1:0] = 2b'10	// Reset BB TRX
	u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN);
	u1bTmp &= ~BIT0;
	rtw_write8(padapter, REG_SYS_FUNC_EN, u1bTmp);

	rtw_udelay_os(2);

	u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN);
	u1bTmp &= ~BIT1;
	rtw_write8(padapter, REG_SYS_FUNC_EN, u1bTmp);

	// 3. 0x100[7:0] = 0x3F	// Reset MAC TRX
	rtw_write8(padapter, REG_CR, 0x3F);

	// 4. 0x101[1] = 0		// check if removed later
	u1bTmp = rtw_read8(padapter, REG_CR+1);
	rtw_write8(padapter, REG_CR+1, u1bTmp&(~BIT1));

	// 5. 0x553[5] = 1		// respond TX ok to scheduler
	u1bTmp = rtw_read8(padapter,  REG_DUAL_TSF_RST);
	rtw_write8(padapter, REG_DUAL_TSF_RST, (u1bTmp|BIT5));
}
#endif
//
// Description:
//	RTL8723e card disable power sequence v003 which suggested by Scott.
//
// First created by tynli. 2011.01.28.
//
static void CardDisableRTL8723ASdio(PADAPTER padapter)
{
	u8		u1bTmp;
	u16		u2bTmp;
	u32		u4bTmp;
	u8		bMacPwrCtrlOn;
	u8		ret;


	// Run LPS WL RFOFF flow
	ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, rtl8723A_enter_lps_flow);
	if (ret == _FALSE) {
		printk(KERN_ERR "%s: run RF OFF flow fail!\n", __func__);
	}

	//	==== Reset digital sequence   ======

	u1bTmp = rtw_read8(padapter, REG_MCUFWDL);
	if ((u1bTmp & RAM_DL_SEL) && padapter->bFWReady) //8051 RAM code
		rtl8723a_FirmwareSelfReset(padapter);

	// Reset MCU 0x2[10]=0. Suggested by Filen. 2011.01.26. by tynli.
	u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
	u1bTmp &= ~BIT(2);	// 0x2[10], FEN_CPUEN
	rtw_write8(padapter, REG_SYS_FUNC_EN+1, u1bTmp);

	// MCUFWDL 0x80[1:0]=0
	// reset MCU ready status
	rtw_write8(padapter, REG_MCUFWDL, 0);

	//	==== Reset digital sequence end ======

	// Power down.
	bMacPwrCtrlOn = _FALSE;	// Disable CMD53 R/W
	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, rtl8723A_card_disable_flow);
	if (ret == _FALSE) {
		printk(KERN_ERR "%s: run CARD DISABLE flow fail!\n", __func__);
	}

	// Reset MCU IO Wrapper, added by Roger, 2011.08.30
	u1bTmp = rtw_read8(padapter, REG_RSV_CTRL+1);
	u1bTmp &= ~BIT(0);
	rtw_write8(padapter, REG_RSV_CTRL+1, u1bTmp);
	u1bTmp = rtw_read8(padapter, REG_RSV_CTRL+1);
	u1bTmp |= BIT(0);
	rtw_write8(padapter, REG_RSV_CTRL+1, u1bTmp);

	// RSV_CTRL 0x1C[7:0]=0x0E
	// lock ISO/CLK/Power control register
	rtw_write8(padapter, REG_RSV_CTRL, 0x0E);
}

static u32 rtl8723as_hal_deinit(PADAPTER padapter)
{
#ifdef CONFIG_BT_COEXIST
	BT_HaltProcess(padapter);
#endif

	if (padapter->hw_init_completed == _TRUE)
		CardDisableRTL8723ASdio(padapter);
	
	return _SUCCESS;
}

static u32 rtl8723as_inirp_init(PADAPTER padapter)
{
	u32 status;

_func_enter_;

	status = _SUCCESS;

_func_exit_;

	return status;
}

static u32 rtl8723as_inirp_deinit(PADAPTER padapter)
{
	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("+rtl8723as_inirp_deinit\n"));

	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("-rtl8723as_inirp_deinit\n"));

	return _SUCCESS;
}

static void rtl8723as_init_default_value(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;


	pHalData = GET_HAL_DATA(padapter);

	rtl8723a_init_default_value(padapter);

	// interface related variable
	pHalData->SdioRxFIFOCnt = 0;
}
#if 0
static VOID
_ConfigTestChipOutEP(
	IN	PADAPTER	pAdapter,
	IN	u1Byte		NumOutPipe
	)
{
	u1Byte			value8,txqsele;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);

	pHalData->OutEpQueueSel = 0;
	pHalData->OutEpNumber	= 0;

	value8 = PlatformEFIORead1Byte(pAdapter, REG_TEST_SIE_OPTIONAL);
	value8 = (value8 & USB_TEST_EP_MASK) >> USB_TEST_EP_SHIFT;

	switch(value8)
	{
		case 0:		// 2 bulk OUT, 1 bulk IN
		case 3:
			pHalData->OutEpQueueSel = TX_SELE_HQ | TX_SELE_LQ;
			pHalData->OutEpNumber	= 2;
			RT_TRACE(COMP_INIT,  DBG_LOUD, ("EP Config: 2 bulk OUT, 1 bulk IN\n"));
			break;
		case 1:		// 1 bulk IN/OUT => map all endpoint to Low queue
		case 2:		// 1 bulk IN, 1 bulk OUT => map all endpoint to High queue
			txqsele = PlatformEFIORead1Byte(pAdapter, REG_TEST_USB_TXQS);
			if(txqsele & 0x0F){//map all endpoint to High queue
				pHalData->OutEpQueueSel  = TX_SELE_HQ;
			}
			else if(txqsele&0xF0){//map all endpoint to Low queue
				pHalData->OutEpQueueSel  =  TX_SELE_LQ;
			}
			pHalData->OutEpNumber	= 1;
			RT_TRACE(COMP_INIT,  DBG_LOUD, ("%s\n", ((1 == value8) ? "1 bulk IN/OUT" : "1 bulk IN, 1 bulk OUT")));
			break;
		default:
			break;
	}

	// TODO: Error recovery for this case
	RT_ASSERT((NumOutPipe == pHalData->OutEpNumber), ("Out EP number isn't match! %ld(Descriptor) != %ld (SIE reg)\n", (u4Byte)NumOutPipe, (u4Byte)pHalData->OutEpNumber));
}

static VOID
_ConfigNormalChipOutEP(
	IN	PADAPTER	pAdapter,
	IN	u1Byte		NumOutPipe
	)
{
	u1Byte			value8;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);

	pHalData->OutEpQueueSel = 0;
	pHalData->OutEpNumber	= 0;

	pHalData->OutEpQueueSel = TX_SELE_HQ|TX_SELE_NQ|TX_SELE_LQ;
	pHalData->OutEpNumber = NumOutPipe;

	// TODO: Error recovery for this case
	RT_ASSERT((NumOutPipe == pHalData->OutEpNumber), ("Out EP number isn't match! %ld(Descriptor) != %ld (SIE reg)\n", (u4Byte)NumOutPipe, (u4Byte)pHalData->OutEpNumber));

}
static VOID _ThreeOutEpMapping(
	IN	HAL_DATA_TYPE	*pHalData,
	IN	BOOLEAN	 		bWIFICfg
	)
{
	if (bWIFICfg)
	{
		// for WMM
		u8	Queue2Pipe[] =
		//	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA
		{  	1, 	2, 	1, 	0, 	0, 	0, 	0, 	0, 		0	};

		QUEUE_INDEX_LIST	Pipe2Queue[] = {
						{{VO_QUEUE, HCCA_QUEUE, BEACON_QUEUE, HIGH_QUEUE, MGNT_QUEUE}, 5},	// 0 :HIQ
						{{BK_QUEUE, VI_QUEUE}, 2},											// 1 :MIQ
						{{BE_QUEUE}, 1} 														// 2 :LOQ
						};
		_UpdateMappingStruct(
						pHalData,
						Pipe2Queue,
						sizeof(Pipe2Queue),
						Queue2Pipe,
						sizeof(Queue2Pipe)
						);
	}
	else
	{
		//typical setting
		u8	Queue2Pipe[] =
		//	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA
		{  	2, 	2, 	1, 	0, 	0, 		0, 	 0, 	   0, 		0	};

		QUEUE_INDEX_LIST	Pipe2Queue[] = {
						{{VO_QUEUE, HCCA_QUEUE, BEACON_QUEUE, HIGH_QUEUE, MGNT_QUEUE}, 5},	// 0 :HIQ
						{{VI_QUEUE}, 1} ,										// 1 :MIQ
						{{BE_QUEUE, BK_QUEUE}, 2}							// 2 :LOQ
						};
		_UpdateMappingStruct(
						pHalData,
						Pipe2Queue,
						sizeof(Pipe2Queue),
						Queue2Pipe,
						sizeof(Queue2Pipe)
						);
	}
}

static BOOLEAN
_MappingOutEP(
	IN	PADAPTER	padapter,
	IN	u1Byte		NumOutPipe,
	IN	BOOLEAN		IsTestChip
	)
{
	HAL_DATA_TYPE *pHalData;
	struct registry_priv *pregistrypriv;
	u8 bWIFICfg;
	BOOLEAN result = _TRUE;


	pHalData = GET_HAL_DATA(padapter);
	pregistrypriv = &padapter->registrypriv;
	bWIFICfg = pregistrypriv->wifi_spec;

	switch (NumOutPipe)
	{
		case 3:
			_ThreeOutEpMapping(pHalData, bWIFICfg);
			break;

		default:
			RT_TRACE(_module_hci_hal_init_c_, _drv_err_,
					("Incorrect the number of SDIO TxQueue!!\n"));
			result = _FALSE;
			break;
	}

	return result;
}

static BOOLEAN
HalSdioSetQueueMapping8192CSdio(
	IN	PADAPTER	padapter,
	IN	u1Byte		NumIn,
	IN	u1Byte		NumOut
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	BOOLEAN			result		= FALSE;
	BOOLEAN			is81xxCTest = FALSE;


//	pHalData->VersionID	= ReadChipVersion(padapter);
	is81xxCTest = IS_81XXC_TEST_CHIP(pHalData->VersionID);

	if(is81xxCTest)
		_ConfigTestChipOutEP(padapter, NumOut);
	else
		_ConfigNormalChipOutEP(padapter, NumOut);

	result = _MappingOutEP(padapter, NumOut, is81xxCTest);

	return TRUE;
}
#endif

static void rtl8723as_interface_configure(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	struct registry_priv *pregistrypriv;
	u8 bWIFICfg;


	pHalData = GET_HAL_DATA(padapter);
	pregistrypriv = &padapter->registrypriv;
	bWIFICfg = pregistrypriv->wifi_spec;

	pHalData->OutEpQueueSel = TX_SELE_HQ|TX_SELE_NQ|TX_SELE_LQ;
	pHalData->OutEpNumber = SDIO_MAX_TX_QUEUE;

	if (bWIFICfg)
	{
		// for WMM
		u8	Queue2Pipe[] =
		//	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA
		{  	1, 	2, 	1, 	0, 	0, 	0, 	0, 	0, 		0	};

		_rtw_memcpy(pHalData->Queue2EPNum, Queue2Pipe, 9);
	}
	else
	{
		//typical setting
		u8	Queue2Pipe[] =
		//	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA
		{  	2, 	2, 	1, 	0, 	0, 		0, 	 0, 	   0, 		0	};

		_rtw_memcpy(pHalData->Queue2EPNum, Queue2Pipe, 9);
	}
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
static void
_EfuseCellSel(
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
Hal_EfuseParsePIDVID_8723AS(
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
Hal_EfuseParseMACAddr_8723AS(
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
		_rtw_memcpy(pEEPROM->mac_addr, &hwinfo[EEPROM_MAC_ADDR_8723AS], ETH_ALEN);
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
		 ("Hal_EfuseParseMACAddr_8723AS: Permanent Address = %02x-%02x-%02x-%02x-%02x-%02x\n",
		  pEEPROM->mac_addr[0], pEEPROM->mac_addr[1],
		  pEEPROM->mac_addr[2], pEEPROM->mac_addr[3],
		  pEEPROM->mac_addr[4], pEEPROM->mac_addr[5]));
}

static void
Hal_EfuseParseBoardType_8723AS(
	IN	PADAPTER		pAdapter,
	IN	u8*			hwinfo,
	IN	BOOLEAN			AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if (!AutoLoadFail)
		pHalData->BoardType = (hwinfo[RF_OPTION1_8723A] & 0xE0) >> 5;
	else
		pHalData->BoardType = 0;
	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Board Type: 0x%2x\n", pHalData->BoardType));
}

static void
Hal_CustomizeByCustomerID_8723AS(
	IN	PADAPTER		padapter
	)
{
#if 0
	PMGNT_INFO		pMgntInfo = &(padapter->MgntInfo);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	// For customized behavior.
	if((pHalData->EEPROMVID == 0x103C) && (pHalData->EEPROMVID == 0x1629))// HP Lite-On for RTL8188CUS Slim Combo.
		pMgntInfo->CustomerID = RT_CID_819x_HP;

	// Decide CustomerID according to VID/DID or EEPROM
	switch(pHalData->EEPROMCustomerID)
	{
		case EEPROM_CID_DEFAULT:
			if((pHalData->EEPROMVID == 0x2001) && (pHalData->EEPROMPID == 0x3308))
				pMgntInfo->CustomerID = RT_CID_DLINK;
			else if((pHalData->EEPROMVID == 0x2001) && (pHalData->EEPROMPID == 0x3309))
				pMgntInfo->CustomerID = RT_CID_DLINK;
			else if((pHalData->EEPROMVID == 0x2001) && (pHalData->EEPROMPID == 0x330a))
				pMgntInfo->CustomerID = RT_CID_DLINK;
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
			pMgntInfo->CustomerID = RT_CID_DEFAULT;
			break;

	}

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Mgnt Customer ID: 0x%02x\n", pMgntInfo->CustomerID));

	hal_CustomizedBehavior_8723U(padapter);
#endif
}

static VOID
readAdapterInfo(
	IN PADAPTER			padapter
	)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
//	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8			hwinfo[HWSET_MAX_SIZE];

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("====> readpadapterInfo_8723S()\n"));

	//
	// This part read and parse the eeprom/efuse content
	//
	Hal_InitPGData(padapter, hwinfo);
	Hal_EfuseParseIDCode(padapter, hwinfo);
	Hal_EfuseParsePIDVID_8723AS(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseMACAddr_8723AS(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseTxPowerInfo_8723A(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseBTCoexistInfo_8723A(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseEEPROMVer(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	rtl8723a_EfuseParseChnlPlan(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseCustomerID(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseAntennaDiversity(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseRateIndicationOption(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseBoardType_8723AS(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseXtal_8723A(padapter, hwinfo, pEEPROM->bautoload_fail_flag);

	//
	// The following part initialize some vars by PG info.
	//
	Hal_InitChannelPlan(padapter);
	Hal_CustomizeByCustomerID_8723AS(padapter);

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

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_,
		 ("%s: 9346CR=0x%02X, Boot from %s, Autoload %s\n",
		  __FUNCTION__, eeValue,
		  (pEEPROM->EepromOrEfuse ? "EEPROM" : "EFUSE"),
		  (pEEPROM->bautoload_fail_flag ? "Fail" : "OK")));

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
static s32 _ReadAdapterInfo8723AS(PADAPTER padapter)
{
	u32 start;


	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("+ReadAdapterInfo8723AS\n"));

	// before access eFuse, make sure card enable has been called
	CardEnable(padapter);

	start = rtw_get_current_time();

	_EfuseCellSel(padapter);
	_ReadRFType(padapter);
	_ReadPROMContent(padapter);
	_InitOtherVariable(padapter);

	MSG_8192C("<==== ReadAdapterInfo8723AS in %d ms\n", rtw_get_passing_time_ms(start));

	return _SUCCESS;
}

static void ReadAdapterInfo8723AS(PADAPTER padapter)
{
	// Read EEPROM size before call any EEPROM function
	padapter->EepromAddressSize = GetEEPROMSize8723A(padapter);

	_ReadAdapterInfo8723AS(padapter);
}

/*
 * If variable not handled here,
 * some variables will be processed in SetHwReg8723A()
 */
void SetHwReg8723AS(PADAPTER padapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);

_func_enter_;

	switch (variable)
	{
		case HW_VAR_SET_RPWM:
			rtw_write8(padapter, SDIO_LOCAL_BASE|SDIO_REG_HRPWM1, *val);
			break;

		default:
			SetHwReg8723A(padapter, variable, val);
			break;
	}

_func_exit_;
}

/*
 * If variable not handled here,
 * some variables will be processed in GetHwReg8723A()
 */
void GetHwReg8723AS(PADAPTER padapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);

_func_enter_;

	switch (variable)
	{
		default:
			GetHwReg8723A(padapter, variable, val);
			break;
	}

_func_exit_;
}

//
//	Description:
//		Query setting of specified variable.
//
u8
GetHalDefVar8723ASDIO(
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
		case HAL_DEF_DBG_DUMP_RXPKT:
			*(( u8*)pValue) = pHalData->bDumpRxPkt;
			break;
		case HAL_DEF_DBG_DM_FUNC:
			*(( u32*)pValue) =pHalData->odmpriv.SupportAbility;
			break;
		case HW_VAR_MAX_RX_AMPDU_FACTOR:
			*(( u32*)pValue) = MAX_AMPDU_FACTOR_64K;
			break;	
		default:
			//RT_TRACE(COMP_INIT, DBG_WARNING, ("GetHalDefVar8723ASDIO(): Unkown variable: %d!\n", eVariable));
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
SetHalDefVar8723ASDIO(
	IN	PADAPTER				Adapter,
	IN	HAL_DEF_VARIABLE		eVariable,
	IN	PVOID					pValue
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
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
			//RT_TRACE(COMP_INIT, DBG_TRACE, ("SetHalDefVar819xUsb(): Unkown variable: %d!\n", eVariable));
			bResult = _FALSE;
			break;
	}

	return bResult;
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

void rtl8723as_set_hal_ops(PADAPTER padapter)
{
	struct hal_ops *pHalFunc = &padapter->HalFunc;

_func_enter_;

	rtl8723a_set_hal_ops(pHalFunc);

	pHalFunc->hal_init = &rtl8723as_hal_init;
	pHalFunc->hal_deinit = &rtl8723as_hal_deinit;

	pHalFunc->inirp_init = &rtl8723as_inirp_init;
	pHalFunc->inirp_deinit = &rtl8723as_inirp_deinit;

	pHalFunc->init_xmit_priv = &rtl8723as_init_xmit_priv;
	pHalFunc->free_xmit_priv = &rtl8723as_free_xmit_priv;

	pHalFunc->init_recv_priv = &rtl8723as_init_recv_priv;
	pHalFunc->free_recv_priv = &rtl8723as_free_recv_priv;

	pHalFunc->InitSwLeds = &rtl8723as_InitSwLeds;
	pHalFunc->DeInitSwLeds = &rtl8723as_DeInitSwLeds;

	pHalFunc->init_default_value = &rtl8723as_init_default_value;
	pHalFunc->intf_chip_configure = &rtl8723as_interface_configure;
	pHalFunc->read_adapter_info = &ReadAdapterInfo8723AS;

	pHalFunc->enable_interrupt = &EnableInterrupt8723ASdio;
	pHalFunc->disable_interrupt = &DisableInterrupt8723ASdio;

	pHalFunc->SetHwRegHandler = &SetHwReg8723AS;
	pHalFunc->GetHwRegHandler = &GetHwReg8723AS;
	pHalFunc->GetHalDefVarHandler = &GetHalDefVar8723ASDIO;
 	pHalFunc->SetHalDefVarHandler = &SetHalDefVar8723ASDIO;

//	pHalFunc->UpdateRAMaskHandler = &UpdateHalRAMask8723ASdio;
	pHalFunc->UpdateRAMaskHandler = &UpdateHalRAMask8192CUsb;

	pHalFunc->hal_xmit = &rtl8723as_hal_xmit;
	pHalFunc->mgnt_xmit = &rtl8723as_mgnt_xmit;

#ifdef CONFIG_HOSTAPD_MLME
	pHalFunc->hostap_mgnt_xmit_entry = NULL;
//	pHalFunc->hostap_mgnt_xmit_entry = &rtl8192cu_hostap_mgnt_xmit_entry;
#endif


_func_exit_;
}


