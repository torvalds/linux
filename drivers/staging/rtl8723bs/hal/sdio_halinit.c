// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _SDIO_HALINIT_C_

#include <drv_types.h>
#include <rtw_debug.h>
#include <rtl8723b_hal.h>

#include "hal_com_h2c.h"
/*
 * Description:
 *Call power on sequence to enable card
 *
 * Return:
 *_SUCCESS	enable success
 *_FAIL		enable fail
 */
static u8 CardEnable(struct adapter *padapter)
{
	u8 bMacPwrCtrlOn;
	u8 ret = _FAIL;


	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (!bMacPwrCtrlOn) {
		/*  RSV_CTRL 0x1C[7:0] = 0x00 */
		/*  unlock ISO/CLK/Power control register */
		rtw_write8(padapter, REG_RSV_CTRL, 0x0);

		ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, rtl8723B_card_enable_flow);
		if (ret == _SUCCESS) {
			u8 bMacPwrCtrlOn = true;
			rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
		}
	} else
		ret = _SUCCESS;

	return ret;
}

#ifdef CONFIG_GPIO_WAKEUP
/* we set it high under init and fw will */
/* give us Low Pulse when host wake up */
void HostWakeUpGpioClear(struct adapter *Adapter)
{
	u32 value32;

	value32 = rtw_read32(Adapter, REG_GPIO_PIN_CTRL_2);

	/* set GPIO 12 1 */
	value32 |= BIT(12);/* 4+8 */
	/* GPIO 12 out put */
	value32 |= BIT(20);/* 4+16 */

	rtw_write32(Adapter, REG_GPIO_PIN_CTRL_2, value32);
} /* HostWakeUpGpioClear */

void HalSetOutPutGPIO(struct adapter *padapter, u8 index, u8 OutPutValue)
{
	if (index <= 7) {
		/* config GPIO mode */
		rtw_write8(padapter, REG_GPIO_PIN_CTRL + 3, rtw_read8(padapter, REG_GPIO_PIN_CTRL + 3) & ~BIT(index));

		/* config GPIO Sel */
		/* 0: input */
		/* 1: output */
		rtw_write8(padapter, REG_GPIO_PIN_CTRL + 2, rtw_read8(padapter, REG_GPIO_PIN_CTRL + 2) | BIT(index));

		/* set output value */
		if (OutPutValue)
			rtw_write8(padapter, REG_GPIO_PIN_CTRL + 1, rtw_read8(padapter, REG_GPIO_PIN_CTRL + 1) | BIT(index));
		else
			rtw_write8(padapter, REG_GPIO_PIN_CTRL + 1, rtw_read8(padapter, REG_GPIO_PIN_CTRL + 1) & ~BIT(index));
	} else {
		/* 88C Series: */
		/* index: 11~8 transform to 3~0 */
		/* 8723 Series: */
		/* index: 12~8 transform to 4~0 */
		index -= 8;

		/* config GPIO mode */
		rtw_write8(padapter, REG_GPIO_PIN_CTRL_2 + 3, rtw_read8(padapter, REG_GPIO_PIN_CTRL_2 + 3) & ~BIT(index));

		/* config GPIO Sel */
		/* 0: input */
		/* 1: output */
		rtw_write8(padapter, REG_GPIO_PIN_CTRL_2 + 2, rtw_read8(padapter, REG_GPIO_PIN_CTRL_2 + 2) | BIT(index));

		/* set output value */
		if (OutPutValue)
			rtw_write8(padapter, REG_GPIO_PIN_CTRL_2 + 1, rtw_read8(padapter, REG_GPIO_PIN_CTRL_2 + 1) | BIT(index));
		else
			rtw_write8(padapter, REG_GPIO_PIN_CTRL_2 + 1, rtw_read8(padapter, REG_GPIO_PIN_CTRL_2 + 1) & ~BIT(index));
	}
}
#endif

static
u8 _InitPowerOn_8723BS(struct adapter *padapter)
{
	u8 value8;
	u16 value16;
	u32 value32;
	u8 ret;
/* 	u8 bMacPwrCtrlOn; */


	/*  all of these MUST be configured before power on */
#ifdef CONFIG_EXT_CLK
	/*  Use external crystal(XTAL) */
	value8 = rtw_read8(padapter, REG_PAD_CTRL1_8723B + 2);
	value8 |=  BIT(7);
	rtw_write8(padapter, REG_PAD_CTRL1_8723B + 2, value8);

	/*  CLK_REQ High active or Low Active */
	/*  Request GPIO polarity: */
	/*  0: low active */
	/*  1: high active */
	value8 = rtw_read8(padapter, REG_MULTI_FUNC_CTRL + 1);
	value8 |= BIT(5);
	rtw_write8(padapter, REG_MULTI_FUNC_CTRL + 1, value8);
#endif /*  CONFIG_EXT_CLK */

	/*  only cmd52 can be used before power on(card enable) */
	ret = CardEnable(padapter);
	if (!ret) {
		RT_TRACE(
			_module_hci_hal_init_c_,
			_drv_emerg_,
			("%s: run power on flow fail\n", __func__)
		);
		return _FAIL;
	}

	/*  Radio-Off Pin Trigger */
	value8 = rtw_read8(padapter, REG_GPIO_INTM + 1);
	value8 |= BIT(1); /*  Enable falling edge triggering interrupt */
	rtw_write8(padapter, REG_GPIO_INTM + 1, value8);
	value8 = rtw_read8(padapter, REG_GPIO_IO_SEL_2 + 1);
	value8 |= BIT(1);
	rtw_write8(padapter, REG_GPIO_IO_SEL_2 + 1, value8);

	/*  Enable power down and GPIO interrupt */
	value16 = rtw_read16(padapter, REG_APS_FSMCO);
	value16 |= EnPDN; /*  Enable HW power down and RF on */
	rtw_write16(padapter, REG_APS_FSMCO, value16);

	/*  Enable CMD53 R/W Operation */
/* 	bMacPwrCtrlOn = true; */
/* 	rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn); */

	rtw_write8(padapter, REG_CR, 0x00);
	/*  Enable MAC DMA/WMAC/SCHEDULE/SEC block */
	value16 = rtw_read16(padapter, REG_CR);
	value16 |= (
		HCI_TXDMA_EN |
		HCI_RXDMA_EN |
		TXDMA_EN |
		RXDMA_EN |
		PROTOCOL_EN |
		SCHEDULE_EN |
		ENSEC |
		CALTMR_EN
	);
	rtw_write16(padapter, REG_CR, value16);

	hal_btcoex_PowerOnSetting(padapter);

	/*  external switch to S1 */
	/*  0x38[11] = 0x1 */
	/*  0x4c[23] = 0x1 */
	/*  0x64[0] = 0 */
	value16 = rtw_read16(padapter, REG_PWR_DATA);
	/*  Switch the control of EESK, EECS to RFC for DPDT or Antenna switch */
	value16 |= BIT(11); /*  BIT_EEPRPAD_RFE_CTRL_EN */
	rtw_write16(padapter, REG_PWR_DATA, value16);
/* 	DBG_8192C("%s: REG_PWR_DATA(0x%x) = 0x%04X\n", __func__, REG_PWR_DATA, rtw_read16(padapter, REG_PWR_DATA)); */

	value32 = rtw_read32(padapter, REG_LEDCFG0);
	value32 |= BIT(23); /*  DPDT_SEL_EN, 1 for SW control */
	rtw_write32(padapter, REG_LEDCFG0, value32);
/* 	DBG_8192C("%s: REG_LEDCFG0(0x%x) = 0x%08X\n", __func__, REG_LEDCFG0, rtw_read32(padapter, REG_LEDCFG0)); */

	value8 = rtw_read8(padapter, REG_PAD_CTRL1_8723B);
	value8 &= ~BIT(0); /*  BIT_SW_DPDT_SEL_DATA, DPDT_SEL default configuration */
	rtw_write8(padapter, REG_PAD_CTRL1_8723B, value8);
/* 	DBG_8192C("%s: REG_PAD_CTRL1(0x%x) = 0x%02X\n", __func__, REG_PAD_CTRL1_8723B, rtw_read8(padapter, REG_PAD_CTRL1_8723B)); */

#ifdef CONFIG_GPIO_WAKEUP
	HostWakeUpGpioClear(padapter);
#endif

	return _SUCCESS;
}

/* Tx Page FIFO threshold */
static void _init_available_page_threshold(struct adapter *padapter, u8 numHQ, u8 numNQ, u8 numLQ, u8 numPubQ)
{
	u16 HQ_threshold, NQ_threshold, LQ_threshold;

	HQ_threshold = (numPubQ + numHQ + 1) >> 1;
	HQ_threshold |= (HQ_threshold << 8);

	NQ_threshold = (numPubQ + numNQ + 1) >> 1;
	NQ_threshold |= (NQ_threshold << 8);

	LQ_threshold = (numPubQ + numLQ + 1) >> 1;
	LQ_threshold |= (LQ_threshold << 8);

	rtw_write16(padapter, 0x218, HQ_threshold);
	rtw_write16(padapter, 0x21A, NQ_threshold);
	rtw_write16(padapter, 0x21C, LQ_threshold);
	DBG_8192C("%s(): Enable Tx FIFO Page Threshold H:0x%x, N:0x%x, L:0x%x\n", __func__, HQ_threshold, NQ_threshold, LQ_threshold);
}

static void _InitQueueReservedPage(struct adapter *padapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	u32 numHQ = 0;
	u32 numLQ = 0;
	u32 numNQ = 0;
	u32 numPubQ;
	u32 value32;
	u8 value8;
	bool bWiFiConfig	= pregistrypriv->wifi_spec;

	if (pHalData->OutEpQueueSel & TX_SELE_HQ)
		numHQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_HPQ_8723B : NORMAL_PAGE_NUM_HPQ_8723B;

	if (pHalData->OutEpQueueSel & TX_SELE_LQ)
		numLQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_LPQ_8723B : NORMAL_PAGE_NUM_LPQ_8723B;

	/*  NOTE: This step shall be proceed before writing REG_RQPN. */
	if (pHalData->OutEpQueueSel & TX_SELE_NQ)
		numNQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_NPQ_8723B : NORMAL_PAGE_NUM_NPQ_8723B;

	numPubQ = TX_TOTAL_PAGE_NUMBER_8723B - numHQ - numLQ - numNQ;

	value8 = (u8)_NPQ(numNQ);
	rtw_write8(padapter, REG_RQPN_NPQ, value8);

	/*  TX DMA */
	value32 = _HPQ(numHQ) | _LPQ(numLQ) | _PUBQ(numPubQ) | LD_RQPN;
	rtw_write32(padapter, REG_RQPN, value32);

	rtw_hal_set_sdio_tx_max_length(padapter, numHQ, numNQ, numLQ, numPubQ);

	_init_available_page_threshold(padapter, numHQ, numNQ, numLQ, numPubQ);
}

static void _InitTxBufferBoundary(struct adapter *padapter)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;

	/* u16 txdmactrl; */
	u8 txpktbuf_bndy;

	if (!pregistrypriv->wifi_spec) {
		txpktbuf_bndy = TX_PAGE_BOUNDARY_8723B;
	} else {
		/* for WMM */
		txpktbuf_bndy = WMM_NORMAL_TX_PAGE_BOUNDARY_8723B;
	}

	rtw_write8(padapter, REG_TXPKTBUF_BCNQ_BDNY_8723B, txpktbuf_bndy);
	rtw_write8(padapter, REG_TXPKTBUF_MGQ_BDNY_8723B, txpktbuf_bndy);
	rtw_write8(padapter, REG_TXPKTBUF_WMAC_LBK_BF_HD_8723B, txpktbuf_bndy);
	rtw_write8(padapter, REG_TRXFF_BNDY, txpktbuf_bndy);
	rtw_write8(padapter, REG_TDECTRL + 1, txpktbuf_bndy);
}

static void _InitNormalChipRegPriority(
	struct adapter *Adapter,
	u16 beQ,
	u16 bkQ,
	u16 viQ,
	u16 voQ,
	u16 mgtQ,
	u16 hiQ
)
{
	u16 value16 = (rtw_read16(Adapter, REG_TRXDMA_CTRL) & 0x7);

	value16 |=
		_TXDMA_BEQ_MAP(beQ)  |
		_TXDMA_BKQ_MAP(bkQ)  |
		_TXDMA_VIQ_MAP(viQ)  |
		_TXDMA_VOQ_MAP(voQ)  |
		_TXDMA_MGQ_MAP(mgtQ) |
		_TXDMA_HIQ_MAP(hiQ);

	rtw_write16(Adapter, REG_TRXDMA_CTRL, value16);
}

static void _InitNormalChipOneOutEpPriority(struct adapter *Adapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);

	u16 value = 0;
	switch (pHalData->OutEpQueueSel) {
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
		/* RT_ASSERT(false, ("Shall not reach here!\n")); */
		break;
	}

	_InitNormalChipRegPriority(
		Adapter, value, value, value, value, value, value
	);

}

static void _InitNormalChipTwoOutEpPriority(struct adapter *Adapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	u16 beQ, bkQ, viQ, voQ, mgtQ, hiQ;


	u16 valueHi = 0;
	u16 valueLow = 0;

	switch (pHalData->OutEpQueueSel) {
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
		/* RT_ASSERT(false, ("Shall not reach here!\n")); */
		break;
	}

	if (!pregistrypriv->wifi_spec) {
		beQ = valueLow;
		bkQ = valueLow;
		viQ = valueHi;
		voQ = valueHi;
		mgtQ = valueHi;
		hiQ = valueHi;
	} else {
		/* for WMM , CONFIG_OUT_EP_WIFI_MODE */
		beQ = valueLow;
		bkQ = valueHi;
		viQ = valueHi;
		voQ = valueLow;
		mgtQ = valueHi;
		hiQ = valueHi;
	}

	_InitNormalChipRegPriority(Adapter, beQ, bkQ, viQ, voQ, mgtQ, hiQ);

}

static void _InitNormalChipThreeOutEpPriority(struct adapter *padapter)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	u16 beQ, bkQ, viQ, voQ, mgtQ, hiQ;

	if (!pregistrypriv->wifi_spec) {
		/*  typical setting */
		beQ = QUEUE_LOW;
		bkQ = QUEUE_LOW;
		viQ = QUEUE_NORMAL;
		voQ = QUEUE_HIGH;
		mgtQ = QUEUE_HIGH;
		hiQ = QUEUE_HIGH;
	} else {
		/*  for WMM */
		beQ = QUEUE_LOW;
		bkQ = QUEUE_NORMAL;
		viQ = QUEUE_NORMAL;
		voQ = QUEUE_HIGH;
		mgtQ = QUEUE_HIGH;
		hiQ = QUEUE_HIGH;
	}
	_InitNormalChipRegPriority(padapter, beQ, bkQ, viQ, voQ, mgtQ, hiQ);
}

static void _InitQueuePriority(struct adapter *Adapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);

	switch (pHalData->OutEpNumber) {
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
		/* RT_ASSERT(false, ("Shall not reach here!\n")); */
		break;
	}


}

static void _InitPageBoundary(struct adapter *padapter)
{
	/*  RX Page Boundary */
	u16 rxff_bndy = RX_DMA_BOUNDARY_8723B;

	rtw_write16(padapter, (REG_TRXFF_BNDY + 2), rxff_bndy);
}

static void _InitTransferPageSize(struct adapter *padapter)
{
	/*  Tx page size is always 128. */

	u8 value8;
	value8 = _PSRX(PBP_128) | _PSTX(PBP_128);
	rtw_write8(padapter, REG_PBP, value8);
}

static void _InitDriverInfoSize(struct adapter *padapter, u8 drvInfoSize)
{
	rtw_write8(padapter, REG_RX_DRVINFO_SZ, drvInfoSize);
}

static void _InitNetworkType(struct adapter *padapter)
{
	u32 value32;

	value32 = rtw_read32(padapter, REG_CR);

	/*  TODO: use the other function to set network type */
/* 	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AD_HOC); */
	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AP);

	rtw_write32(padapter, REG_CR, value32);
}

static void _InitWMACSetting(struct adapter *padapter)
{
	struct hal_com_data *pHalData;
	u16 value16;


	pHalData = GET_HAL_DATA(padapter);

	pHalData->ReceiveConfig = 0;
	pHalData->ReceiveConfig |= RCR_APM | RCR_AM | RCR_AB;
	pHalData->ReceiveConfig |= RCR_CBSSID_DATA | RCR_CBSSID_BCN | RCR_AMF;
	pHalData->ReceiveConfig |= RCR_HTC_LOC_CTRL;
	pHalData->ReceiveConfig |= RCR_APP_PHYST_RXFF | RCR_APP_ICV | RCR_APP_MIC;
	rtw_write32(padapter, REG_RCR, pHalData->ReceiveConfig);

	/*  Accept all multicast address */
	rtw_write32(padapter, REG_MAR, 0xFFFFFFFF);
	rtw_write32(padapter, REG_MAR + 4, 0xFFFFFFFF);

	/*  Accept all data frames */
	value16 = 0xFFFF;
	rtw_write16(padapter, REG_RXFLTMAP2, value16);

	/*  2010.09.08 hpfan */
	/*  Since ADF is removed from RCR, ps-poll will not be indicate to driver, */
	/*  RxFilterMap should mask ps-poll to gurantee AP mode can rx ps-poll. */
	value16 = 0x400;
	rtw_write16(padapter, REG_RXFLTMAP1, value16);

	/*  Accept all management frames */
	value16 = 0xFFFF;
	rtw_write16(padapter, REG_RXFLTMAP0, value16);
}

static void _InitAdaptiveCtrl(struct adapter *padapter)
{
	u16 value16;
	u32 value32;

	/*  Response Rate Set */
	value32 = rtw_read32(padapter, REG_RRSR);
	value32 &= ~RATE_BITMAP_ALL;
	value32 |= RATE_RRSR_CCK_ONLY_1M;
	rtw_write32(padapter, REG_RRSR, value32);

	/*  CF-END Threshold */
	/* m_spIoBase->rtw_write8(REG_CFEND_TH, 0x1); */

	/*  SIFS (used in NAV) */
	value16 = _SPEC_SIFS_CCK(0x10) | _SPEC_SIFS_OFDM(0x10);
	rtw_write16(padapter, REG_SPEC_SIFS, value16);

	/*  Retry Limit */
	value16 = _LRL(0x30) | _SRL(0x30);
	rtw_write16(padapter, REG_RL, value16);
}

static void _InitEDCA(struct adapter *padapter)
{
	/*  Set Spec SIFS (used in NAV) */
	rtw_write16(padapter, REG_SPEC_SIFS, 0x100a);
	rtw_write16(padapter, REG_MAC_SPEC_SIFS, 0x100a);

	/*  Set SIFS for CCK */
	rtw_write16(padapter, REG_SIFS_CTX, 0x100a);

	/*  Set SIFS for OFDM */
	rtw_write16(padapter, REG_SIFS_TRX, 0x100a);

	/*  TXOP */
	rtw_write32(padapter, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtw_write32(padapter, REG_EDCA_BK_PARAM, 0x0000A44F);
	rtw_write32(padapter, REG_EDCA_VI_PARAM, 0x005EA324);
	rtw_write32(padapter, REG_EDCA_VO_PARAM, 0x002FA226);
}

static void _InitRetryFunction(struct adapter *padapter)
{
	u8 value8;

	value8 = rtw_read8(padapter, REG_FWHW_TXQ_CTRL);
	value8 |= EN_AMPDU_RTY_NEW;
	rtw_write8(padapter, REG_FWHW_TXQ_CTRL, value8);

	/*  Set ACK timeout */
	rtw_write8(padapter, REG_ACKTO, 0x40);
}

static void HalRxAggr8723BSdio(struct adapter *padapter)
{
	u8 valueDMATimeout;
	u8 valueDMAPageCount;

	valueDMATimeout = 0x06;
	valueDMAPageCount = 0x06;

	rtw_write8(padapter, REG_RXDMA_AGG_PG_TH + 1, valueDMATimeout);
	rtw_write8(padapter, REG_RXDMA_AGG_PG_TH, valueDMAPageCount);
}

static void sdio_AggSettingRxUpdate(struct adapter *padapter)
{
	u8 valueDMA;
	u8 valueRxAggCtrl = 0;
	u8 aggBurstNum = 3;  /* 0:1, 1:2, 2:3, 3:4 */
	u8 aggBurstSize = 0;  /* 0:1K, 1:512Byte, 2:256Byte... */

	valueDMA = rtw_read8(padapter, REG_TRXDMA_CTRL);
	valueDMA |= RXDMA_AGG_EN;
	rtw_write8(padapter, REG_TRXDMA_CTRL, valueDMA);

	valueRxAggCtrl |= RXDMA_AGG_MODE_EN;
	valueRxAggCtrl |= ((aggBurstNum << 2) & 0x0C);
	valueRxAggCtrl |= ((aggBurstSize << 4) & 0x30);
	rtw_write8(padapter, REG_RXDMA_MODE_CTRL_8723B, valueRxAggCtrl);/* RxAggLowThresh = 4*1K */
}

static void _initSdioAggregationSetting(struct adapter *padapter)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);

	/*  Tx aggregation setting */
/* 	sdio_AggSettingTxUpdate(padapter); */

	/*  Rx aggregation setting */
	HalRxAggr8723BSdio(padapter);

	sdio_AggSettingRxUpdate(padapter);

	/*  201/12/10 MH Add for USB agg mode dynamic switch. */
	pHalData->UsbRxHighSpeedMode = false;
}

static void _InitOperationMode(struct adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext;
	u8 regBwOpMode = 0;

	pmlmeext = &padapter->mlmeextpriv;

	/* 1 This part need to modified according to the rate set we filtered!! */
	/*  */
	/*  Set RRSR, RATR, and REG_BWOPMODE registers */
	/*  */
	switch (pmlmeext->cur_wireless_mode) {
	case WIRELESS_MODE_B:
		regBwOpMode = BW_OPMODE_20MHZ;
		break;
	case WIRELESS_MODE_A:
/* 			RT_ASSERT(false, ("Error wireless a mode\n")); */
		break;
	case WIRELESS_MODE_G:
		regBwOpMode = BW_OPMODE_20MHZ;
		break;
	case WIRELESS_MODE_AUTO:
		regBwOpMode = BW_OPMODE_20MHZ;
		break;
	case WIRELESS_MODE_N_24G:
		/*  It support CCK rate by default. */
		/*  CCK rate will be filtered out only when associated AP does not support it. */
		regBwOpMode = BW_OPMODE_20MHZ;
		break;
	case WIRELESS_MODE_N_5G:
/* 			RT_ASSERT(false, ("Error wireless mode")); */
		regBwOpMode = BW_OPMODE_5G;
		break;

	default: /* for MacOSX compiler warning. */
		break;
	}

	rtw_write8(padapter, REG_BWOPMODE, regBwOpMode);

}

static void _InitInterrupt(struct adapter *padapter)
{
	/*  HISR - turn all off */
	rtw_write32(padapter, REG_HISR, 0);

	/*  HIMR - turn all off */
	rtw_write32(padapter, REG_HIMR, 0);

	/*  */
	/*  Initialize and enable SDIO Host Interrupt. */
	/*  */
	InitInterrupt8723BSdio(padapter);

	/*  */
	/*  Initialize system Host Interrupt. */
	/*  */
	InitSysInterrupt8723BSdio(padapter);
}

static void _InitRFType(struct adapter *padapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);

#if	DISABLE_BB_RF
	pHalData->rf_chip	= RF_PSEUDO_11N;
	return;
#endif

	pHalData->rf_chip	= RF_6052;

	pHalData->rf_type = RF_1T1R;
	DBG_8192C("Set RF Chip ID to RF_6052 and RF type to 1T1R.\n");
}

static void _RfPowerSave(struct adapter *padapter)
{
/* YJ, TODO */
}

/*  */
/*  2010/08/09 MH Add for power down check. */
/*  */
static bool HalDetectPwrDownMode(struct adapter *Adapter)
{
	u8 tmpvalue;
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(Adapter);


	EFUSE_ShadowRead(Adapter, 1, 0x7B/*EEPROM_RF_OPT3_92C*/, (u32 *)&tmpvalue);

	/*  2010/08/25 MH INF priority > PDN Efuse value. */
	if (tmpvalue & BIT4 && pwrctrlpriv->reg_pdnmode)
		pHalData->pwrdown = true;
	else
		pHalData->pwrdown = false;

	DBG_8192C("HalDetectPwrDownMode(): PDN =%d\n", pHalData->pwrdown);

	return pHalData->pwrdown;
}	/*  HalDetectPwrDownMode */

static u32 rtl8723bs_hal_init(struct adapter *padapter)
{
	s32 ret;
	struct hal_com_data *pHalData;
	struct pwrctrl_priv *pwrctrlpriv;
	u32 NavUpper = WiFiNavUpperUs;
	u8 u1bTmp;

	pHalData = GET_HAL_DATA(padapter);
	pwrctrlpriv = adapter_to_pwrctl(padapter);

	if (
		adapter_to_pwrctl(padapter)->bips_processing == true &&
		adapter_to_pwrctl(padapter)->pre_ips_type == 0
	) {
		unsigned long start_time;
		u8 cpwm_orig, cpwm_now;
		u8 val8, bMacPwrCtrlOn = true;

		DBG_871X("%s: Leaving IPS in FWLPS state\n", __func__);

		/* for polling cpwm */
		cpwm_orig = 0;
		rtw_hal_get_hwreg(padapter, HW_VAR_CPWM, &cpwm_orig);

		/* ser rpwm */
		val8 = rtw_read8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HRPWM1);
		val8 &= 0x80;
		val8 += 0x80;
		val8 |= BIT(6);
		rtw_write8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HRPWM1, val8);
		DBG_871X("%s: write rpwm =%02x\n", __func__, val8);
		adapter_to_pwrctl(padapter)->tog = (val8 + 0x80) & 0x80;

		/* do polling cpwm */
		start_time = jiffies;
		do {

			mdelay(1);

			rtw_hal_get_hwreg(padapter, HW_VAR_CPWM, &cpwm_now);
			if ((cpwm_orig ^ cpwm_now) & 0x80)
				break;

			if (jiffies_to_msecs(jiffies - start_time) > 100) {
				DBG_871X("%s: polling cpwm timeout when leaving IPS in FWLPS state\n", __func__);
				break;
			}
		} while (1);

		rtl8723b_set_FwPwrModeInIPS_cmd(padapter, 0);

		rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);

		hal_btcoex_InitHwConfig(padapter, false);

		return _SUCCESS;
	}

#ifdef CONFIG_WOWLAN
	if (rtw_read8(padapter, REG_MCUFWDL) & BIT7) {
		u8 reg_val = 0;
		DBG_871X("+Reset Entry+\n");
		rtw_write8(padapter, REG_MCUFWDL, 0x00);
		_8051Reset8723(padapter);
		/* reset BB */
		reg_val = rtw_read8(padapter, REG_SYS_FUNC_EN);
		reg_val &= ~(BIT(0) | BIT(1));
		rtw_write8(padapter, REG_SYS_FUNC_EN, reg_val);
		/* reset RF */
		rtw_write8(padapter, REG_RF_CTRL, 0);
		/* reset TRX path */
		rtw_write16(padapter, REG_CR, 0);
		/* reset MAC, Digital Core */
		reg_val = rtw_read8(padapter, REG_SYS_FUNC_EN + 1);
		reg_val &= ~(BIT(4) | BIT(7));
		rtw_write8(padapter, REG_SYS_FUNC_EN + 1, reg_val);
		reg_val = rtw_read8(padapter, REG_SYS_FUNC_EN + 1);
		reg_val |= BIT(4) | BIT(7);
		rtw_write8(padapter, REG_SYS_FUNC_EN + 1, reg_val);
		DBG_871X("-Reset Entry-\n");
	}
#endif /* CONFIG_WOWLAN */
	/*  Disable Interrupt first. */
/* 	rtw_hal_disable_interrupt(padapter); */

	ret = _InitPowerOn_8723BS(padapter);
	if (_FAIL == ret) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init Power On!\n"));
		return _FAIL;
	}

	rtw_write8(padapter, REG_EARLY_MODE_CONTROL, 0);

	ret = rtl8723b_FirmwareDownload(padapter, false);
	if (ret != _SUCCESS) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("%s: Download Firmware failed!!\n", __func__));
		padapter->bFWReady = false;
		pHalData->fw_ractrl = false;
		return ret;
	} else {
		RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("rtl8723bs_hal_init(): Download Firmware Success!!\n"));
		padapter->bFWReady = true;
		pHalData->fw_ractrl = true;
	}

	rtl8723b_InitializeFirmwareVars(padapter);

/* 	SIC_Init(padapter); */

	if (pwrctrlpriv->reg_rfoff)
		pwrctrlpriv->rf_pwrstate = rf_off;

	/*  2010/08/09 MH We need to check if we need to turnon or off RF after detecting */
	/*  HW GPIO pin. Before PHY_RFConfig8192C. */
	HalDetectPwrDownMode(padapter);

	/*  Set RF type for BB/RF configuration */
	_InitRFType(padapter);

	/*  Save target channel */
	/*  <Roger_Notes> Current Channel will be updated again later. */
	pHalData->CurrentChannel = 6;

#if (HAL_MAC_ENABLE == 1)
	ret = PHY_MACConfig8723B(padapter);
	if (ret != _SUCCESS) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Initializepadapter8192CSdio(): Fail to configure MAC!!\n"));
		return ret;
	}
#endif
	/*  */
	/* d. Initialize BB related configurations. */
	/*  */
#if (HAL_BB_ENABLE == 1)
	ret = PHY_BBConfig8723B(padapter);
	if (ret != _SUCCESS) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Initializepadapter8192CSdio(): Fail to configure BB!!\n"));
		return ret;
	}
#endif

	/*  If RF is on, we need to init RF. Otherwise, skip the procedure. */
	/*  We need to follow SU method to change the RF cfg.txt. Default disable RF TX/RX mode. */
	/* if (pHalData->eRFPowerState == eRfOn) */
	{
#if (HAL_RF_ENABLE == 1)
		ret = PHY_RFConfig8723B(padapter);
		if (ret != _SUCCESS) {
			RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Initializepadapter8192CSdio(): Fail to configure RF!!\n"));
			return ret;
		}
#endif
	}

	/*  */
	/*  Joseph Note: Keep RfRegChnlVal for later use. */
	/*  */
	pHalData->RfRegChnlVal[0] =
		PHY_QueryRFReg(padapter, (enum RF_PATH)0, RF_CHNLBW, bRFRegOffsetMask);
	pHalData->RfRegChnlVal[1] =
		PHY_QueryRFReg(padapter, (enum RF_PATH)1, RF_CHNLBW, bRFRegOffsetMask);


	/* if (!pHalData->bMACFuncEnable) { */
	_InitQueueReservedPage(padapter);
	_InitTxBufferBoundary(padapter);

	/*  init LLT after tx buffer boundary is defined */
	ret = rtl8723b_InitLLTTable(padapter);
	if (_SUCCESS != ret) {
		DBG_8192C("%s: Failed to init LLT Table!\n", __func__);
		return _FAIL;
	}
	/*  */
	_InitQueuePriority(padapter);
	_InitPageBoundary(padapter);
	_InitTransferPageSize(padapter);

	/*  Get Rx PHY status in order to report RSSI and others. */
	_InitDriverInfoSize(padapter, DRVINFO_SZ);
	hal_init_macaddr(padapter);
	_InitNetworkType(padapter);
	_InitWMACSetting(padapter);
	_InitAdaptiveCtrl(padapter);
	_InitEDCA(padapter);
	_InitRetryFunction(padapter);
	_initSdioAggregationSetting(padapter);
	_InitOperationMode(padapter);
	rtl8723b_InitBeaconParameters(padapter);
	_InitInterrupt(padapter);
	_InitBurstPktLen_8723BS(padapter);

	/* YJ, TODO */
	rtw_write8(padapter, REG_SECONDARY_CCA_CTRL_8723B, 0x3);	/*  CCA */
	rtw_write8(padapter, 0x976, 0);	/*  hpfan_todo: 2nd CCA related */

	rtw_write16(padapter, REG_PKT_VO_VI_LIFE_TIME, 0x0400);	/*  unit: 256us. 256ms */
	rtw_write16(padapter, REG_PKT_BE_BK_LIFE_TIME, 0x0400);	/*  unit: 256us. 256ms */

	invalidate_cam_all(padapter);

	rtw_hal_set_chnl_bw(padapter, padapter->registrypriv.channel,
		CHANNEL_WIDTH_20, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HAL_PRIME_CHNL_OFFSET_DONT_CARE);

	/*  Record original value for template. This is arough data, we can only use the data */
	/*  for power adjust. The value can not be adjustde according to different power!!! */
/* 	pHalData->OriginalCckTxPwrIdx = pHalData->CurrentCckTxPwrIdx; */
/* 	pHalData->OriginalOfdm24GTxPwrIdx = pHalData->CurrentOfdm24GTxPwrIdx; */

	rtl8723b_InitAntenna_Selection(padapter);

	/*  */
	/*  Disable BAR, suggested by Scott */
	/*  2010.04.09 add by hpfan */
	/*  */
	rtw_write32(padapter, REG_BAR_MODE_CTRL, 0x0201ffff);

	/*  HW SEQ CTRL */
	/*  set 0x0 to 0xFF by tynli. Default enable HW SEQ NUM. */
	rtw_write8(padapter, REG_HWSEQ_CTRL, 0xFF);


	/*  */
	/*  Configure SDIO TxRx Control to enable Rx DMA timer masking. */
	/*  2010.02.24. */
	/*  */
	rtw_write32(padapter, SDIO_LOCAL_BASE | SDIO_REG_TX_CTRL, 0);

	_RfPowerSave(padapter);


	rtl8723b_InitHalDm(padapter);

	/* DbgPrint("pHalData->DefaultTxPwrDbm = %d\n", pHalData->DefaultTxPwrDbm); */

	/*  */
	/*  Update current Tx FIFO page status. */
	/*  */
	HalQueryTxBufferStatus8723BSdio(padapter);
	HalQueryTxOQTBufferStatus8723BSdio(padapter);
	pHalData->SdioTxOQTMaxFreeSpace = pHalData->SdioTxOQTFreeSpace;

	/*  Enable MACTXEN/MACRXEN block */
	u1bTmp = rtw_read8(padapter, REG_CR);
	u1bTmp |= (MACTXEN | MACRXEN);
	rtw_write8(padapter, REG_CR, u1bTmp);

	rtw_hal_set_hwreg(padapter, HW_VAR_NAV_UPPER, (u8 *)&NavUpper);

	/* ack for xmit mgmt frames. */
	rtw_write32(padapter, REG_FWHW_TXQ_CTRL, rtw_read32(padapter, REG_FWHW_TXQ_CTRL) | BIT(12));

/* 	pHalData->PreRpwmVal = SdioLocalCmd52Read1Byte(padapter, SDIO_REG_HRPWM1) & 0x80; */

	{
		pwrctrlpriv->rf_pwrstate = rf_on;

		if (pwrctrlpriv->rf_pwrstate == rf_on) {
			struct pwrctrl_priv *pwrpriv;
			unsigned long start_time;
			u8 restore_iqk_rst;
			u8 b2Ant;
			u8 h2cCmdBuf;

			pwrpriv = adapter_to_pwrctl(padapter);

			PHY_LCCalibrate_8723B(&pHalData->odmpriv);

			/* Inform WiFi FW that it is the beginning of IQK */
			h2cCmdBuf = 1;
			FillH2CCmd8723B(padapter, H2C_8723B_BT_WLAN_CALIBRATION, 1, &h2cCmdBuf);

			start_time = jiffies;
			do {
				if (rtw_read8(padapter, 0x1e7) & 0x01)
					break;

				msleep(50);
			} while (jiffies_to_msecs(jiffies - start_time) <= 400);

			hal_btcoex_IQKNotify(padapter, true);

			restore_iqk_rst = pwrpriv->bips_processing;
			b2Ant = pHalData->EEPROMBluetoothAntNum == Ant_x2;
			PHY_IQCalibrate_8723B(padapter, false, restore_iqk_rst, b2Ant, pHalData->ant_path);
			pHalData->odmpriv.RFCalibrateInfo.bIQKInitialized = true;

			hal_btcoex_IQKNotify(padapter, false);

			/* Inform WiFi FW that it is the finish of IQK */
			h2cCmdBuf = 0;
			FillH2CCmd8723B(padapter, H2C_8723B_BT_WLAN_CALIBRATION, 1, &h2cCmdBuf);

			ODM_TXPowerTrackingCheck(&pHalData->odmpriv);
		}
	}

	/*  Init BT hw config. */
	hal_btcoex_InitHwConfig(padapter, false);

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("-%s\n", __func__));

	return _SUCCESS;
}

/*  */
/*  Description: */
/* 	RTL8723e card disable power sequence v003 which suggested by Scott. */
/*  */
/*  First created by tynli. 2011.01.28. */
/*  */
static void CardDisableRTL8723BSdio(struct adapter *padapter)
{
	u8 u1bTmp;
	u8 bMacPwrCtrlOn;
	u8 ret = _FAIL;

	/*  Run LPS WL RFOFF flow */
	ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, rtl8723B_enter_lps_flow);
	if (ret == _FAIL) {
		DBG_8192C(KERN_ERR "%s: run RF OFF flow fail!\n", __func__);
	}

	/* 	==== Reset digital sequence   ====== */

	u1bTmp = rtw_read8(padapter, REG_MCUFWDL);
	if ((u1bTmp & RAM_DL_SEL) && padapter->bFWReady) /* 8051 RAM code */
		rtl8723b_FirmwareSelfReset(padapter);

	/*  Reset MCU 0x2[10]= 0. Suggested by Filen. 2011.01.26. by tynli. */
	u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN + 1);
	u1bTmp &= ~BIT(2);	/*  0x2[10], FEN_CPUEN */
	rtw_write8(padapter, REG_SYS_FUNC_EN + 1, u1bTmp);

	/*  MCUFWDL 0x80[1:0]= 0 */
	/*  reset MCU ready status */
	rtw_write8(padapter, REG_MCUFWDL, 0);

	/*  Reset MCU IO Wrapper, added by Roger, 2011.08.30 */
	u1bTmp = rtw_read8(padapter, REG_RSV_CTRL + 1);
	u1bTmp &= ~BIT(0);
	rtw_write8(padapter, REG_RSV_CTRL + 1, u1bTmp);
	u1bTmp = rtw_read8(padapter, REG_RSV_CTRL + 1);
	u1bTmp |= BIT(0);
	rtw_write8(padapter, REG_RSV_CTRL+1, u1bTmp);

	/* 	==== Reset digital sequence end ====== */

	bMacPwrCtrlOn = false;	/*  Disable CMD53 R/W */
	ret = false;
	rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, rtl8723B_card_disable_flow);
	if (!ret) {
		DBG_8192C(KERN_ERR "%s: run CARD DISABLE flow fail!\n", __func__);
	}
}

static u32 rtl8723bs_hal_deinit(struct adapter *padapter)
{
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	if (padapter->hw_init_completed) {
		if (adapter_to_pwrctl(padapter)->bips_processing) {
			if (padapter->netif_up) {
				int cnt = 0;
				u8 val8 = 0;

				DBG_871X("%s: issue H2C to FW when entering IPS\n", __func__);

				rtl8723b_set_FwPwrModeInIPS_cmd(padapter, 0x3);
				/* poll 0x1cc to make sure H2C command already finished by FW; MAC_0x1cc = 0 means H2C done by FW. */
				do {
					val8 = rtw_read8(padapter, REG_HMETFR);
					cnt++;
					DBG_871X("%s  polling REG_HMETFR = 0x%x, cnt =%d\n", __func__, val8, cnt);
					mdelay(10);
				} while (cnt < 100 && (val8 != 0));
				/* H2C done, enter 32k */
				if (val8 == 0) {
					/* ser rpwm to enter 32k */
					val8 = rtw_read8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HRPWM1);
					val8 += 0x80;
					val8 |= BIT(0);
					rtw_write8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HRPWM1, val8);
					DBG_871X("%s: write rpwm =%02x\n", __func__, val8);
					adapter_to_pwrctl(padapter)->tog = (val8 + 0x80) & 0x80;
					cnt = val8 = 0;
					do {
						val8 = rtw_read8(padapter, REG_CR);
						cnt++;
						DBG_871X("%s  polling 0x100 = 0x%x, cnt =%d\n", __func__, val8, cnt);
						mdelay(10);
					} while (cnt < 100 && (val8 != 0xEA));
				} else {
					DBG_871X(
						"MAC_1C0 =%08x, MAC_1C4 =%08x, MAC_1C8 =%08x, MAC_1CC =%08x\n",
						rtw_read32(padapter, 0x1c0),
						rtw_read32(padapter, 0x1c4),
						rtw_read32(padapter, 0x1c8),
						rtw_read32(padapter, 0x1cc)
					);
				}

				DBG_871X(
					"polling done when entering IPS, check result : 0x100 = 0x%x, cnt =%d, MAC_1cc = 0x%02x\n",
					rtw_read8(padapter, REG_CR),
					cnt,
					rtw_read8(padapter, REG_HMETFR)
				);

				adapter_to_pwrctl(padapter)->pre_ips_type = 0;

			} else {
				pdbgpriv->dbg_carddisable_cnt++;
				CardDisableRTL8723BSdio(padapter);

				adapter_to_pwrctl(padapter)->pre_ips_type = 1;
			}

		} else {
			pdbgpriv->dbg_carddisable_cnt++;
			CardDisableRTL8723BSdio(padapter);
		}
	} else
		pdbgpriv->dbg_deinit_fail_cnt++;

	return _SUCCESS;
}

static u32 rtl8723bs_inirp_init(struct adapter *padapter)
{
	return _SUCCESS;
}

static u32 rtl8723bs_inirp_deinit(struct adapter *padapter)
{
	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("+rtl8723bs_inirp_deinit\n"));

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("-rtl8723bs_inirp_deinit\n"));

	return _SUCCESS;
}

static void rtl8723bs_init_default_value(struct adapter *padapter)
{
	struct hal_com_data *pHalData;


	pHalData = GET_HAL_DATA(padapter);

	rtl8723b_init_default_value(padapter);

	/*  interface related variable */
	pHalData->SdioRxFIFOCnt = 0;
}

static void rtl8723bs_interface_configure(struct adapter *padapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	bool bWiFiConfig = pregistrypriv->wifi_spec;


	pdvobjpriv->RtOutPipe[0] = WLAN_TX_HIQ_DEVICE_ID;
	pdvobjpriv->RtOutPipe[1] = WLAN_TX_MIQ_DEVICE_ID;
	pdvobjpriv->RtOutPipe[2] = WLAN_TX_LOQ_DEVICE_ID;

	if (bWiFiConfig)
		pHalData->OutEpNumber = 2;
	else
		pHalData->OutEpNumber = SDIO_MAX_TX_QUEUE;

	switch (pHalData->OutEpNumber) {
	case 3:
		pHalData->OutEpQueueSel = TX_SELE_HQ | TX_SELE_LQ | TX_SELE_NQ;
		break;
	case 2:
		pHalData->OutEpQueueSel = TX_SELE_HQ | TX_SELE_NQ;
		break;
	case 1:
		pHalData->OutEpQueueSel = TX_SELE_HQ;
		break;
	default:
		break;
	}

	Hal_MappingOutPipe(padapter, pHalData->OutEpNumber);
}

/*  */
/* 	Description: */
/* 		We should set Efuse cell selection to WiFi cell in default. */
/*  */
/* 	Assumption: */
/* 		PASSIVE_LEVEL */
/*  */
/* 	Added by Roger, 2010.11.23. */
/*  */
static void _EfuseCellSel(struct adapter *padapter)
{
	u32 value32;

	value32 = rtw_read32(padapter, EFUSE_TEST);
	value32 = (value32 & ~EFUSE_SEL_MASK) | EFUSE_SEL(EFUSE_WIFI_SEL_0);
	rtw_write32(padapter, EFUSE_TEST, value32);
}

static void _ReadRFType(struct adapter *Adapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);

#if DISABLE_BB_RF
	pHalData->rf_chip = RF_PSEUDO_11N;
#else
	pHalData->rf_chip = RF_6052;
#endif
}


static void Hal_EfuseParseMACAddr_8723BS(
	struct adapter *padapter, u8 *hwinfo, bool AutoLoadFail
)
{
	u16 i;
	u8 sMacAddr[6] = {0x00, 0xE0, 0x4C, 0xb7, 0x23, 0x00};
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);

	if (AutoLoadFail) {
/* 		sMacAddr[5] = (u8)GetRandomNumber(1, 254); */
		for (i = 0; i < 6; i++)
			pEEPROM->mac_addr[i] = sMacAddr[i];
	} else {
		/* Read Permanent MAC address */
		memcpy(pEEPROM->mac_addr, &hwinfo[EEPROM_MAC_ADDR_8723BS], ETH_ALEN);
	}
/* 	NicIFSetMacAddress(padapter, padapter->PermanentAddress); */

	RT_TRACE(_module_hci_hal_init_c_, _drv_notice_,
		 ("Hal_EfuseParseMACAddr_8723BS: Permanent Address = %pM\n", pEEPROM->mac_addr));
}

static void Hal_EfuseParseBoardType_8723BS(
	struct adapter *padapter, u8 *hwinfo, bool AutoLoadFail
)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);

	if (!AutoLoadFail) {
		pHalData->BoardType = (hwinfo[EEPROM_RF_BOARD_OPTION_8723B] & 0xE0) >> 5;
		if (pHalData->BoardType == 0xFF)
			pHalData->BoardType = (EEPROM_DEFAULT_BOARD_OPTION & 0xE0) >> 5;
	} else
		pHalData->BoardType = 0;
	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Board Type: 0x%2x\n", pHalData->BoardType));
}

static void _ReadEfuseInfo8723BS(struct adapter *padapter)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	u8 *hwinfo = NULL;

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("====>_ReadEfuseInfo8723BS()\n"));

	/*  */
	/*  This part read and parse the eeprom/efuse content */
	/*  */

	if (sizeof(pEEPROM->efuse_eeprom_data) < HWSET_MAX_SIZE_8723B)
		DBG_871X("[WARNING] size of efuse_eeprom_data is less than HWSET_MAX_SIZE_8723B!\n");

	hwinfo = pEEPROM->efuse_eeprom_data;

	Hal_InitPGData(padapter, hwinfo);

	Hal_EfuseParseIDCode(padapter, hwinfo);
	Hal_EfuseParseEEPROMVer_8723B(padapter, hwinfo, pEEPROM->bautoload_fail_flag);

	Hal_EfuseParseMACAddr_8723BS(padapter, hwinfo, pEEPROM->bautoload_fail_flag);

	Hal_EfuseParseTxPowerInfo_8723B(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseBoardType_8723BS(padapter, hwinfo, pEEPROM->bautoload_fail_flag);

	/*  */
	/*  Read Bluetooth co-exist and initialize */
	/*  */
	Hal_EfuseParsePackageType_8723B(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseBTCoexistInfo_8723B(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseChnlPlan_8723B(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseXtal_8723B(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseThermalMeter_8723B(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseAntennaDiversity_8723B(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseCustomerID_8723B(padapter, hwinfo, pEEPROM->bautoload_fail_flag);

	Hal_EfuseParseVoltage_8723B(padapter, hwinfo, pEEPROM->bautoload_fail_flag);

#ifdef CONFIG_WOWLAN
	Hal_DetectWoWMode(padapter);
#endif

	Hal_ReadRFGainOffset(padapter, hwinfo, pEEPROM->bautoload_fail_flag);

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("<==== _ReadEfuseInfo8723BS()\n"));
}

static void _ReadPROMContent(struct adapter *padapter)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	u8 	eeValue;

	eeValue = rtw_read8(padapter, REG_9346CR);
	/*  To check system boot selection. */
	pEEPROM->EepromOrEfuse = (eeValue & BOOT_FROM_EEPROM) ? true : false;
	pEEPROM->bautoload_fail_flag = (eeValue & EEPROM_EN) ? false : true;

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_,
		 ("%s: 9346CR = 0x%02X, Boot from %s, Autoload %s\n",
		  __func__, eeValue,
		  (pEEPROM->EepromOrEfuse ? "EEPROM" : "EFUSE"),
		  (pEEPROM->bautoload_fail_flag ? "Fail" : "OK")));

/* 	pHalData->EEType = IS_BOOT_FROM_EEPROM(Adapter) ? EEPROM_93C46 : EEPROM_BOOT_EFUSE; */

	_ReadEfuseInfo8723BS(padapter);
}

static void _InitOtherVariable(struct adapter *Adapter)
{
}

/*  */
/* 	Description: */
/* 		Read HW adapter information by E-Fuse or EEPROM according CR9346 reported. */
/*  */
/* 	Assumption: */
/* 		PASSIVE_LEVEL (SDIO interface) */
/*  */
/*  */
static s32 _ReadAdapterInfo8723BS(struct adapter *padapter)
{
	u8 val8;
	unsigned long start;

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("+_ReadAdapterInfo8723BS\n"));

	/*  before access eFuse, make sure card enable has been called */
	if (!padapter->hw_init_completed)
		_InitPowerOn_8723BS(padapter);


	val8 = rtw_read8(padapter, 0x4e);
	MSG_8192C("%s, 0x4e = 0x%x\n", __func__, val8);
	val8 |= BIT(6);
	rtw_write8(padapter, 0x4e, val8);


	start = jiffies;

	_EfuseCellSel(padapter);
	_ReadRFType(padapter);
	_ReadPROMContent(padapter);
	_InitOtherVariable(padapter);

	if (!padapter->hw_init_completed) {
		rtw_write8(padapter, 0x67, 0x00); /*  for BT, Switch Ant control to BT */
		CardDisableRTL8723BSdio(padapter);/* for the power consumption issue,  wifi ko module is loaded during booting, but wifi GUI is off */
	}


	MSG_8192C("<==== _ReadAdapterInfo8723BS in %d ms\n", jiffies_to_msecs(jiffies - start));

	return _SUCCESS;
}

static void ReadAdapterInfo8723BS(struct adapter *padapter)
{
	/*  Read EEPROM size before call any EEPROM function */
	padapter->EepromAddressSize = GetEEPROMSize8723B(padapter);

	_ReadAdapterInfo8723BS(padapter);
}

/*
 * If variable not handled here,
 * some variables will be processed in SetHwReg8723B()
 */
static void SetHwReg8723BS(struct adapter *padapter, u8 variable, u8 *val)
{
	u8 val8;

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	struct wowlan_ioctl_param *poidparam;
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	int res;
	u32 tmp;
	u16 len = 0;
	u8 trycnt = 100;
	u32 himr = 0;
#if defined(CONFIG_WOWLAN)
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct sta_info *psta = NULL;
	u64 iv_low = 0, iv_high = 0;
	u8 mstatus = (*(u8 *)val);
#endif
#endif

	switch (variable) {
	case HW_VAR_SET_RPWM:
		/*  rpwm value only use BIT0(clock bit) , BIT6(Ack bit), and BIT7(Toggle bit) */
		/*  BIT0 value - 1: 32k, 0:40MHz. */
		/*  BIT6 value - 1: report cpwm value after success set, 0:do not report. */
		/*  BIT7 value - Toggle bit change. */
		{
			val8 = *val;
			val8 &= 0xC1;
			rtw_write8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HRPWM1, val8);
		}
		break;
	case HW_VAR_SET_REQ_FW_PS:
		{
			u8 req_fw_ps = 0;
			req_fw_ps = rtw_read8(padapter, 0x8f);
			req_fw_ps |= 0x10;
			rtw_write8(padapter, 0x8f, req_fw_ps);
		}
		break;
	case HW_VAR_RXDMA_AGG_PG_TH:
		val8 = *val;
		break;

#ifdef CONFIG_WOWLAN
	case HW_VAR_WOWLAN:
	{
		poidparam = (struct wowlan_ioctl_param *)val;
		switch (poidparam->subcode) {
		case WOWLAN_ENABLE:
			DBG_871X_LEVEL(_drv_always_, "WOWLAN_ENABLE\n");

			/* backup data rate to register 0x8b for wowlan FW */
			rtw_write8(padapter, 0x8d, 1);
			rtw_write8(padapter, 0x8c, 0);
			rtw_write8(padapter, 0x8f, 0x40);
			rtw_write8(padapter, 0x8b,
			rtw_read8(padapter, 0x2f0));

			/*  1. Download WOWLAN FW */
			DBG_871X_LEVEL(_drv_always_, "Re-download WoWlan FW!\n");
			SetFwRelatedForWoWLAN8723b(padapter, true);

			/*  2. RX DMA stop */
			DBG_871X_LEVEL(_drv_always_, "Pause DMA\n");
			rtw_write32(padapter, REG_RXPKT_NUM, (rtw_read32(padapter, REG_RXPKT_NUM) | RW_RELEASE_EN));
			do {
				if ((rtw_read32(padapter, REG_RXPKT_NUM) & RXDMA_IDLE)) {
					DBG_871X_LEVEL(_drv_always_, "RX_DMA_IDLE is true\n");
					break;
				} else {
					/*  If RX_DMA is not idle, receive one pkt from DMA */
					res = sdio_local_read(padapter, SDIO_REG_RX0_REQ_LEN, 4, (u8 *)&tmp);
					len = le16_to_cpu(tmp);
					DBG_871X_LEVEL(_drv_always_, "RX len:%d\n", len);
					if (len > 0)
						res = RecvOnePkt(padapter, len);
					else
						DBG_871X_LEVEL(_drv_always_, "read length fail %d\n", len);

					DBG_871X_LEVEL(_drv_always_, "RecvOnePkt Result: %d\n", res);
				}
			} while (trycnt--);
			if (trycnt == 0)
				DBG_871X_LEVEL(_drv_always_, "Stop RX DMA failed......\n");

			/*  3. Clear IMR and ISR */
			DBG_871X_LEVEL(_drv_always_, "Clear IMR and ISR\n");
			tmp = 0;
			sdio_local_write(padapter, SDIO_REG_HIMR_ON, 4, (u8 *)&tmp);
			sdio_local_write(padapter, SDIO_REG_HIMR, 4, (u8 *)&tmp);
			sdio_local_read(padapter, SDIO_REG_HISR, 4, (u8 *)&tmp);
			sdio_local_write(padapter, SDIO_REG_HISR, 4, (u8 *)&tmp);

			/*  4. Enable CPWM2 only */
			DBG_871X_LEVEL(_drv_always_, "Enable only CPWM2\n");
			sdio_local_read(padapter, SDIO_REG_HIMR, 4, (u8 *)&tmp);
			DBG_871X("DisableInterruptButCpwm28723BSdio(): Read SDIO_REG_HIMR: 0x%08x\n", tmp);

			himr = cpu_to_le32(SDIO_HIMR_DISABLED) | SDIO_HIMR_CPWM2_MSK;
			sdio_local_write(padapter, SDIO_REG_HIMR, 4, (u8 *)&himr);

			sdio_local_read(padapter, SDIO_REG_HIMR, 4, (u8 *)&tmp);
			DBG_871X("DisableInterruptButCpwm28723BSdio(): Read again SDIO_REG_HIMR: 0x%08x\n", tmp);

			/*  5. Set Enable WOWLAN H2C command. */
			DBG_871X_LEVEL(_drv_always_, "Set Enable WOWLan cmd\n");
			rtl8723b_set_wowlan_cmd(padapter, 1);

			/*  6. Check EnableWoWlan CMD is ready */
			if (!pwrctl->wowlan_pno_enable) {
				DBG_871X_LEVEL(_drv_always_, "Check EnableWoWlan CMD is ready\n");
				mstatus = rtw_read8(padapter, REG_WOW_CTRL);
				trycnt = 10;
				while (!(mstatus & BIT1) && trycnt > 1) {
					mstatus = rtw_read8(padapter, REG_WOW_CTRL);
					DBG_871X("Loop index: %d :0x%02x\n", trycnt, mstatus);
					trycnt--;
					msleep(2);
				}
			}
			break;

		case WOWLAN_DISABLE:
			DBG_871X_LEVEL(_drv_always_, "WOWLAN_DISABLE\n");

			psta = rtw_get_stainfo(&padapter->stapriv, get_bssid(pmlmepriv));
			if (psta)
				rtl8723b_set_FwMediaStatusRpt_cmd(padapter, RT_MEDIA_DISCONNECT, psta->mac_id);
			else
				DBG_871X("psta is null\n");

			/*  1. Read wakeup reason */
			pwrctl->wowlan_wake_reason = rtw_read8(padapter, REG_WOWLAN_WAKE_REASON);
			DBG_871X_LEVEL(
				_drv_always_,
				"wakeup_reason: 0x%02x, mac_630 = 0x%08x, mac_634 = 0x%08x, mac_1c0 = 0x%08x, mac_1c4 = 0x%08x"
				", mac_494 = 0x%08x, , mac_498 = 0x%08x, mac_49c = 0x%08x, mac_608 = 0x%08x, mac_4a0 = 0x%08x, mac_4a4 = 0x%08x\n"
				", mac_1cc = 0x%08x, mac_2f0 = 0x%08x, mac_2f4 = 0x%08x, mac_2f8 = 0x%08x, mac_2fc = 0x%08x, mac_8c = 0x%08x",
				pwrctl->wowlan_wake_reason,
				rtw_read32(padapter, REG_WOWLAN_GTK_DBG1),
				rtw_read32(padapter, REG_WOWLAN_GTK_DBG2),
				rtw_read32(padapter, 0x1c0),
				rtw_read32(padapter, 0x1c4),
				rtw_read32(padapter, 0x494),
				rtw_read32(padapter, 0x498),
				rtw_read32(padapter, 0x49c),
				rtw_read32(padapter, 0x608),
				rtw_read32(padapter, 0x4a0),
				rtw_read32(padapter, 0x4a4),
				rtw_read32(padapter, 0x1cc),
				rtw_read32(padapter, 0x2f0),
				rtw_read32(padapter, 0x2f4),
				rtw_read32(padapter, 0x2f8),
				rtw_read32(padapter, 0x2fc),
				rtw_read32(padapter, 0x8c)
			);
#ifdef CONFIG_PNO_SET_DEBUG
			DBG_871X("0x1b9: 0x%02x, 0x632: 0x%02x\n", rtw_read8(padapter, 0x1b9), rtw_read8(padapter, 0x632));
			DBG_871X("0x4fc: 0x%02x, 0x4fd: 0x%02x\n", rtw_read8(padapter, 0x4fc), rtw_read8(padapter, 0x4fd));
			DBG_871X("TXDMA STATUS: 0x%08x\n", rtw_read32(padapter, REG_TXDMA_STATUS));
#endif

			{
				/*  2.  Set Disable WOWLAN H2C command. */
				DBG_871X_LEVEL(_drv_always_, "Set Disable WOWLan cmd\n");
				rtl8723b_set_wowlan_cmd(padapter, 0);

				/*  3. Check Disable WoWlan CMD ready. */
				DBG_871X_LEVEL(_drv_always_, "Check DisableWoWlan CMD is ready\n");
				mstatus = rtw_read8(padapter, REG_WOW_CTRL);
				trycnt = 50;
				while (mstatus & BIT1 && trycnt > 1) {
					mstatus = rtw_read8(padapter, REG_WOW_CTRL);
					DBG_871X_LEVEL(_drv_always_, "Loop index: %d :0x%02x\n", trycnt, mstatus);
					trycnt--;
					msleep(10);
				}

				if (mstatus & BIT1) {
					DBG_871X_LEVEL(_drv_always_, "Disable WOW mode fail!!\n");
					DBG_871X("Set 0x690 = 0x00\n");
					rtw_write8(padapter, REG_WOW_CTRL, (rtw_read8(padapter, REG_WOW_CTRL) & 0xf0));
					DBG_871X_LEVEL(_drv_always_, "Release RXDMA\n");
					rtw_write32(padapter, REG_RXPKT_NUM, (rtw_read32(padapter, REG_RXPKT_NUM) & (~RW_RELEASE_EN)));
				}

				/*  3.1 read fw iv */
				iv_low = rtw_read32(padapter, REG_TXPKTBUF_IV_LOW);
					/* only low two bytes is PN, check AES_IV macro for detail */
				iv_low &= 0xffff;
				iv_high = rtw_read32(padapter, REG_TXPKTBUF_IV_HIGH);
					/* get the real packet number */
				pwrctl->wowlan_fw_iv = iv_high << 16 | iv_low;
				DBG_871X_LEVEL(_drv_always_, "fw_iv: 0x%016llx\n", pwrctl->wowlan_fw_iv);
				/* Update TX iv data. */
				rtw_set_sec_pn(padapter);

				/*  3.2 read GTK index and key */
				if (
					psecuritypriv->binstallKCK_KEK == true &&
					psecuritypriv->dot11PrivacyAlgrthm == _AES_
				) {
					u8 gtk_keyindex = 0;
					u8 get_key[16];
					/* read gtk key index */
					gtk_keyindex = rtw_read8(padapter, 0x48c);

					if (gtk_keyindex < 4) {
						psecuritypriv->dot118021XGrpKeyid = gtk_keyindex;
						read_cam(padapter, gtk_keyindex, get_key);
						memcpy(psecuritypriv->dot118021XGrpKey[psecuritypriv->dot118021XGrpKeyid].skey, get_key, 16);
						DBG_871X_LEVEL(
							_drv_always_,
							"GTK (%d) = 0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
							gtk_keyindex,
							psecuritypriv->dot118021XGrpKey[psecuritypriv->dot118021XGrpKeyid].lkey[0],
							psecuritypriv->dot118021XGrpKey[psecuritypriv->dot118021XGrpKeyid].lkey[1],
							psecuritypriv->dot118021XGrpKey[psecuritypriv->dot118021XGrpKeyid].lkey[2],
							psecuritypriv->dot118021XGrpKey[psecuritypriv->dot118021XGrpKeyid].lkey[3]
						);
					} else
						DBG_871X_LEVEL(_drv_always_, "GTK index =%d\n", gtk_keyindex);
				}

				/*  4. Re-download Normal FW. */
				DBG_871X_LEVEL(_drv_always_, "Re-download Normal FW!\n");
				SetFwRelatedForWoWLAN8723b(padapter, false);
			}
#ifdef CONFIG_GPIO_WAKEUP
			DBG_871X_LEVEL(_drv_always_, "Set Wake GPIO to high for default.\n");
			HalSetOutPutGPIO(padapter, WAKEUP_GPIO_IDX, 1);
#endif

			/*  5. Download reserved pages and report media status if needed. */
			if (
				(pwrctl->wowlan_wake_reason != FWDecisionDisconnect) &&
				(pwrctl->wowlan_wake_reason != Rx_Pairwisekey) &&
				(pwrctl->wowlan_wake_reason != Rx_DisAssoc) &&
				(pwrctl->wowlan_wake_reason != Rx_DeAuth)
			) {
				rtl8723b_set_FwJoinBssRpt_cmd(padapter, RT_MEDIA_CONNECT);
				if (psta)
					rtl8723b_set_FwMediaStatusRpt_cmd(padapter, RT_MEDIA_CONNECT, psta->mac_id);
			}
#ifdef CONFIG_PNO_SUPPORT
			rtw_write8(padapter, 0x1b8, 0);
			DBG_871X("reset 0x1b8: %d\n", rtw_read8(padapter, 0x1b8));
			rtw_write8(padapter, 0x1b9, 0);
			DBG_871X("reset 0x1b9: %d\n", rtw_read8(padapter, 0x1b9));
			rtw_write8(padapter, REG_PNO_STATUS, 0);
			DBG_871X("reset REG_PNO_STATUS: %d\n", rtw_read8(padapter, REG_PNO_STATUS));
#endif
			break;

		default:
			break;
		}
	}
	break;
#endif /* CONFIG_WOWLAN */
#ifdef CONFIG_AP_WOWLAN
	case HW_VAR_AP_WOWLAN:
	{
		poidparam = (struct wowlan_ioctl_param *)val;
		switch (poidparam->subcode) {
		case WOWLAN_AP_ENABLE:
			DBG_871X("%s, WOWLAN_AP_ENABLE\n", __func__);
			/*  1. Download WOWLAN FW */
			DBG_871X_LEVEL(_drv_always_, "Re-download WoWlan FW!\n");
			SetFwRelatedForWoWLAN8723b(padapter, true);

			/*  2. RX DMA stop */
			DBG_871X_LEVEL(_drv_always_, "Pause DMA\n");
			rtw_write32(padapter, REG_RXPKT_NUM,
				(rtw_read32(padapter, REG_RXPKT_NUM) | RW_RELEASE_EN));
			do {
				if ((rtw_read32(padapter, REG_RXPKT_NUM) & RXDMA_IDLE)) {
					DBG_871X_LEVEL(_drv_always_, "RX_DMA_IDLE is true\n");
					break;
				} else {
					/*  If RX_DMA is not idle, receive one pkt from DMA */
					res = sdio_local_read(padapter, SDIO_REG_RX0_REQ_LEN, 4, (u8 *)&tmp);
					len = le16_to_cpu(tmp);

					DBG_871X_LEVEL(_drv_always_, "RX len:%d\n", len);
					if (len > 0)
						res = RecvOnePkt(padapter, len);
					else
						DBG_871X_LEVEL(_drv_always_, "read length fail %d\n", len);

					DBG_871X_LEVEL(_drv_always_, "RecvOnePkt Result: %d\n", res);
				}
			} while (trycnt--);

			if (trycnt == 0)
				DBG_871X_LEVEL(_drv_always_, "Stop RX DMA failed......\n");

			/*  3. Clear IMR and ISR */
			DBG_871X_LEVEL(_drv_always_, "Clear IMR and ISR\n");
			tmp = 0;
			sdio_local_write(padapter, SDIO_REG_HIMR_ON, 4, (u8 *)&tmp);
			sdio_local_write(padapter, SDIO_REG_HIMR, 4, (u8 *)&tmp);
			sdio_local_read(padapter, SDIO_REG_HISR, 4, (u8 *)&tmp);
			sdio_local_write(padapter, SDIO_REG_HISR, 4, (u8 *)&tmp);

			/*  4. Enable CPWM2 only */
			DBG_871X_LEVEL(_drv_always_, "Enable only CPWM2\n");
			sdio_local_read(padapter, SDIO_REG_HIMR, 4, (u8 *)&tmp);
			DBG_871X("DisableInterruptButCpwm28723BSdio(): Read SDIO_REG_HIMR: 0x%08x\n", tmp);

			himr = cpu_to_le32(SDIO_HIMR_DISABLED) | SDIO_HIMR_CPWM2_MSK;
			sdio_local_write(padapter, SDIO_REG_HIMR, 4, (u8 *)&himr);

			sdio_local_read(padapter, SDIO_REG_HIMR, 4, (u8 *)&tmp);
			DBG_871X("DisableInterruptButCpwm28723BSdio(): Read again SDIO_REG_HIMR: 0x%08x\n", tmp);

			/*  5. Set Enable WOWLAN H2C command. */
			DBG_871X_LEVEL(_drv_always_, "Set Enable AP WOWLan cmd\n");
			rtl8723b_set_ap_wowlan_cmd(padapter, 1);
			/*  6. add some delay for H2C cmd ready */
			msleep(10);

			rtw_write8(padapter, REG_WOWLAN_WAKE_REASON, 0);
			break;
		case WOWLAN_AP_DISABLE:
			DBG_871X("%s, WOWLAN_AP_DISABLE\n", __func__);
			/*  1. Read wakeup reason */
			pwrctl->wowlan_wake_reason =
				rtw_read8(padapter, REG_WOWLAN_WAKE_REASON);

			DBG_871X_LEVEL(_drv_always_, "wakeup_reason: 0x%02x\n",
					pwrctl->wowlan_wake_reason);

			/*  2.  Set Disable WOWLAN H2C command. */
			DBG_871X_LEVEL(_drv_always_, "Set Disable WOWLan cmd\n");
			rtl8723b_set_ap_wowlan_cmd(padapter, 0);
			/*  6. add some delay for H2C cmd ready */
			msleep(2);

			DBG_871X_LEVEL(_drv_always_, "Release RXDMA\n");

			rtw_write32(padapter, REG_RXPKT_NUM,
				(rtw_read32(padapter, REG_RXPKT_NUM) & (~RW_RELEASE_EN)));

			SetFwRelatedForWoWLAN8723b(padapter, false);

#ifdef CONFIG_GPIO_WAKEUP
		DBG_871X_LEVEL(_drv_always_, "Set Wake GPIO to high for default.\n");
		HalSetOutPutGPIO(padapter, WAKEUP_GPIO_IDX, 1);
#endif /* CONFIG_GPIO_WAKEUP */
		rtl8723b_set_FwJoinBssRpt_cmd(padapter, RT_MEDIA_CONNECT);
		issue_beacon(padapter, 0);
		break;
		default:
			break;
	}
}
	break;
#endif /* CONFIG_AP_WOWLAN */
	case HW_VAR_DM_IN_LPS:
		rtl8723b_hal_dm_in_lps(padapter);
		break;
	default:
		SetHwReg8723B(padapter, variable, val);
		break;
	}
}

/*
 * If variable not handled here,
 * some variables will be processed in GetHwReg8723B()
 */
static void GetHwReg8723BS(struct adapter *padapter, u8 variable, u8 *val)
{
	switch (variable) {
	case HW_VAR_CPWM:
		*val = rtw_read8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HCPWM1_8723B);
		break;

	case HW_VAR_FW_PS_STATE:
		{
			/* 3. read dword 0x88               driver read fw ps state */
			*((u16 *)val) = rtw_read16(padapter, 0x88);
		}
		break;
	default:
		GetHwReg8723B(padapter, variable, val);
		break;
	}
}

static void SetHwRegWithBuf8723B(struct adapter *padapter, u8 variable, u8 *pbuf, int len)
{
	switch (variable) {
	case HW_VAR_C2H_HANDLE:
		/* DBG_8192C("%s len =%d\n", __func__, len); */
		C2HPacketHandler_8723B(padapter, pbuf, len);
		break;
	default:
		break;
	}
}

/*  */
/* 	Description: */
/* 		Query setting of specified variable. */
/*  */
static u8 GetHalDefVar8723BSDIO(
	struct adapter *Adapter, enum HAL_DEF_VARIABLE eVariable, void *pValue
)
{
	u8 	bResult = _SUCCESS;

	switch (eVariable) {
	case HAL_DEF_IS_SUPPORT_ANT_DIV:
		break;
	case HAL_DEF_CURRENT_ANTENNA:
		break;
	case HW_VAR_MAX_RX_AMPDU_FACTOR:
		/*  Stanley@BB.SD3 suggests 16K can get stable performance */
		/*  coding by Lucas@20130730 */
		*(u32 *)pValue = IEEE80211_HT_MAX_AMPDU_16K;
		break;
	default:
		bResult = GetHalDefVar8723B(Adapter, eVariable, pValue);
		break;
	}

	return bResult;
}

/*  */
/* 	Description: */
/* 		Change default setting of specified variable. */
/*  */
static u8 SetHalDefVar8723BSDIO(struct adapter *Adapter,
				enum HAL_DEF_VARIABLE eVariable, void *pValue)
{
	return SetHalDefVar8723B(Adapter, eVariable, pValue);
}

void rtl8723bs_set_hal_ops(struct adapter *padapter)
{
	struct hal_ops *pHalFunc = &padapter->HalFunc;

	rtl8723b_set_hal_ops(pHalFunc);

	pHalFunc->hal_init = &rtl8723bs_hal_init;
	pHalFunc->hal_deinit = &rtl8723bs_hal_deinit;

	pHalFunc->inirp_init = &rtl8723bs_inirp_init;
	pHalFunc->inirp_deinit = &rtl8723bs_inirp_deinit;

	pHalFunc->init_xmit_priv = &rtl8723bs_init_xmit_priv;
	pHalFunc->free_xmit_priv = &rtl8723bs_free_xmit_priv;

	pHalFunc->init_recv_priv = &rtl8723bs_init_recv_priv;
	pHalFunc->free_recv_priv = &rtl8723bs_free_recv_priv;

	pHalFunc->init_default_value = &rtl8723bs_init_default_value;
	pHalFunc->intf_chip_configure = &rtl8723bs_interface_configure;
	pHalFunc->read_adapter_info = &ReadAdapterInfo8723BS;

	pHalFunc->enable_interrupt = &EnableInterrupt8723BSdio;
	pHalFunc->disable_interrupt = &DisableInterrupt8723BSdio;
	pHalFunc->check_ips_status = &CheckIPSStatus;
#ifdef CONFIG_WOWLAN
	pHalFunc->clear_interrupt = &ClearInterrupt8723BSdio;
#endif
	pHalFunc->SetHwRegHandler = &SetHwReg8723BS;
	pHalFunc->GetHwRegHandler = &GetHwReg8723BS;
	pHalFunc->SetHwRegHandlerWithBuf = &SetHwRegWithBuf8723B;
	pHalFunc->GetHalDefVarHandler = &GetHalDefVar8723BSDIO;
	pHalFunc->SetHalDefVarHandler = &SetHalDefVar8723BSDIO;

	pHalFunc->hal_xmit = &rtl8723bs_hal_xmit;
	pHalFunc->mgnt_xmit = &rtl8723bs_mgnt_xmit;
	pHalFunc->hal_xmitframe_enqueue = &rtl8723bs_hal_xmitframe_enqueue;

#if defined(CONFIG_CHECK_BT_HANG)
	pHalFunc->hal_init_checkbthang_workqueue = &rtl8723bs_init_checkbthang_workqueue;
	pHalFunc->hal_free_checkbthang_workqueue = &rtl8723bs_free_checkbthang_workqueue;
	pHalFunc->hal_cancle_checkbthang_workqueue = &rtl8723bs_cancle_checkbthang_workqueue;
	pHalFunc->hal_checke_bt_hang = &rtl8723bs_hal_check_bt_hang;
#endif
}
