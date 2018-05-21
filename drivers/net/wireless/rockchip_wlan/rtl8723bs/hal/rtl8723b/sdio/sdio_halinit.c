/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#define _SDIO_HALINIT_C_

#include <rtl8723b_hal.h>
#include "hal_com_h2c.h"

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
	u8 ret = _FAIL;


	bMacPwrCtrlOn = _FALSE;
	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (bMacPwrCtrlOn == _FALSE) {
		/* RSV_CTRL 0x1C[7:0] = 0x00 */
		/* unlock ISO/CLK/Power control register */
		rtw_write8(padapter, REG_RSV_CTRL, 0x0);

		ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, rtl8723B_card_enable_flow);
		if (ret == _SUCCESS) {
			u8 bMacPwrCtrlOn = _TRUE;
			rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
		}
	} else
		ret = _SUCCESS;

	return ret;
}

/* static */
u32 _InitPowerOn_8723BS(PADAPTER padapter)
{
	u8 value8;
	u16 value16;
	u32 value32;
	u8 ret;
	u8 pwron_chk_cnt = 0;
	/*	u8 bMacPwrCtrlOn; */

_init_power_on:

#if 1
	/* all of these MUST be configured before power on */
#ifdef CONFIG_XTAL_26M
	/* Config PLL Reference CLK, */
	/* Change crystal to 26M, APLL_FREF_SEL = 4b'0101 */
	/* APLL_FREF_SEL[0]=1b'1 */
	value8 = rtw_read8(padapter, REG_AFE_PLL_CTRL);
	value8 |= BIT(2);
	rtw_write8(padapter, REG_AFE_PLL_CTRL, value8);
	/* APLL_FREF_SEL[2:1]=2b'10 */
	value8 = rtw_read8(padapter, REG_AFE_CTRL_4_8723B + 1);
	value8 &= ~(BIT(1) | BIT(0));
	value8 |= BIT(1);
	rtw_write16(padapter, REG_AFE_CTRL_4_8723B + 1, value8);
	/* APLL_FREF_SEL[3]=1b'0 */
	value8 = rtw_read8(padapter, REG_AFE_CTRL_4_8723B);
	value8 &= ~BIT(7);
	rtw_write16(padapter, REG_AFE_CTRL_4_8723B, value8);
#endif /* CONFIG_XTAL_26M */

#ifdef CONFIG_EXT_CLK
	/* Use external crystal(XTAL) */
	value8 = rtw_read8(padapter, REG_PAD_CTRL1_8723B + 2);
	value8 |=  BIT(7);
	rtw_write8(padapter, REG_PAD_CTRL1_8723B + 2, value8);

	/* CLK_REQ High active or Low Active */
	/* Request GPIO polarity: */
	/* 0: low active */
	/* 1: high active */
	value8 = rtw_read8(padapter, REG_MULTI_FUNC_CTRL + 1);
	value8 |= BIT(5);
	rtw_write8(padapter, REG_MULTI_FUNC_CTRL + 1, value8);
#endif /* CONFIG_EXT_CLK */
#endif /* all of these MUST be configured before power on */

	/* only cmd52 can be used before power on(card enable) */
	ret = CardEnable(padapter);
	if (ret == _FALSE) {
		return _FAIL;
	}

	/* Radio-Off Pin Trigger */
	value8 = rtw_read8(padapter, REG_GPIO_INTM + 1);
	value8 |= BIT(1); /* Enable falling edge triggering interrupt */
	rtw_write8(padapter, REG_GPIO_INTM + 1, value8);
	value8 = rtw_read8(padapter, REG_GPIO_IO_SEL_2 + 1);
	value8 |= BIT(1);
	rtw_write8(padapter, REG_GPIO_IO_SEL_2 + 1, value8);

	/* Enable power down and GPIO interrupt */
	value16 = rtw_read16(padapter, REG_APS_FSMCO);
	value16 |= EnPDN; /* Enable HW power down and RF on */
	rtw_write16(padapter, REG_APS_FSMCO, value16);

	/* Enable CMD53 R/W Operation
	*	bMacPwrCtrlOn = _TRUE;
	*	rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn); */

	rtw_write8(padapter, REG_CR, 0x00);
	/* Enable MAC DMA/WMAC/SCHEDULE/SEC block */
	value16 = rtw_read16(padapter, REG_CR);
	value16 |= (HCI_TXDMA_EN | HCI_RXDMA_EN | TXDMA_EN | RXDMA_EN
		    | PROTOCOL_EN | SCHEDULE_EN | ENSEC | CALTMR_EN);
	rtw_write16(padapter, REG_CR, value16);


	/* PowerOnCheck() */
	ret = sdio_power_on_check(padapter);
	pwron_chk_cnt++;
	if (_FAIL == ret) {
		if (pwron_chk_cnt > 1) {
			RTW_INFO("Failed to init Power On!\n");
			return _FAIL;
		}
		RTW_INFO("Power on Fail! do it again\n");
		goto _init_power_on;
	}

	return _SUCCESS;
}

/* Tx Page FIFO threshold */
static void _init_available_page_threshold(PADAPTER padapter, u8 numHQ, u8 numNQ, u8 numLQ, u8 numPubQ)
{
	u16	HQ_threshold, NQ_threshold, LQ_threshold;

	HQ_threshold = (numPubQ + numHQ + 1) >> 1;
	HQ_threshold |= (HQ_threshold << 8);

	NQ_threshold = (numPubQ + numNQ + 1) >> 1;
	NQ_threshold |= (NQ_threshold << 8);

	LQ_threshold = (numPubQ + numLQ + 1) >> 1;
	LQ_threshold |= (LQ_threshold << 8);

	rtw_write16(padapter, 0x218, HQ_threshold);
	rtw_write16(padapter, 0x21A, NQ_threshold);
	rtw_write16(padapter, 0x21C, LQ_threshold);
	RTW_INFO("%s(): Enable Tx FIFO Page Threshold H:0x%x,N:0x%x,L:0x%x\n", __FUNCTION__, HQ_threshold, NQ_threshold, LQ_threshold);
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

	if (pHalData->OutEpQueueSel & TX_SELE_HQ)
		numHQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_HPQ_8723B : NORMAL_PAGE_NUM_HPQ_8723B;

	if (pHalData->OutEpQueueSel & TX_SELE_LQ)
		numLQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_LPQ_8723B : NORMAL_PAGE_NUM_LPQ_8723B;

	/* NOTE: This step shall be proceed before writting REG_RQPN. */
	if (pHalData->OutEpQueueSel & TX_SELE_NQ)
		numNQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_NPQ_8723B : NORMAL_PAGE_NUM_NPQ_8723B;

	numPubQ = TX_TOTAL_PAGE_NUMBER_8723B - numHQ - numLQ - numNQ;

	value8 = (u8)_NPQ(numNQ);
	rtw_write8(padapter, REG_RQPN_NPQ, value8);

	/* TX DMA */
	value32 = _HPQ(numHQ) | _LPQ(numLQ) | _PUBQ(numPubQ) | LD_RQPN;
	rtw_write32(padapter, REG_RQPN, value32);

	rtw_hal_set_sdio_tx_max_length(padapter, numHQ, numNQ, numLQ, numPubQ);

#ifdef CONFIG_SDIO_TX_ENABLE_AVAL_INT
	_init_available_page_threshold(padapter, numHQ, numNQ, numLQ, numPubQ);
#endif
}

static void _InitTxBufferBoundary(PADAPTER padapter)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
#ifdef CONFIG_CONCURRENT_MODE
	u8 val8;
#endif /* CONFIG_CONCURRENT_MODE */

	/* u16	txdmactrl; */
	u8	txpktbuf_bndy;

	if (!pregistrypriv->wifi_spec)
		txpktbuf_bndy = TX_PAGE_BOUNDARY_8723B;
	else {
		/* for WMM */
		txpktbuf_bndy = WMM_NORMAL_TX_PAGE_BOUNDARY_8723B;
	}

	rtw_write8(padapter, REG_TXPKTBUF_BCNQ_BDNY_8723B, txpktbuf_bndy);
	rtw_write8(padapter, REG_TXPKTBUF_MGQ_BDNY_8723B, txpktbuf_bndy);
	rtw_write8(padapter, REG_TXPKTBUF_WMAC_LBK_BF_HD_8723B, txpktbuf_bndy);
	rtw_write8(padapter, REG_TRXFF_BNDY, txpktbuf_bndy);
	rtw_write8(padapter, REG_TDECTRL + 1, txpktbuf_bndy);

#ifdef CONFIG_CONCURRENT_MODE
	val8 = txpktbuf_bndy + BCNQ_PAGE_NUM_8723B + WOWLAN_PAGE_NUM_8723B;
	rtw_write8(padapter, REG_BCNQ1_BDNY, val8);
	rtw_write8(padapter, REG_DWBCN1_CTRL_8723B + 1, val8); /* BCN1_HEAD */

	val8 = rtw_read8(padapter, REG_DWBCN1_CTRL_8723B + 2);
	val8 |= BIT(1); /* BIT1- BIT_SW_BCN_SEL_EN */
	rtw_write8(padapter, REG_DWBCN1_CTRL_8723B + 2, val8);
#endif /* CONFIG_CONCURRENT_MODE */
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

	value16 |=	_TXDMA_BEQ_MAP(beQ)	| _TXDMA_BKQ_MAP(bkQ) |
			_TXDMA_VIQ_MAP(viQ)	| _TXDMA_VOQ_MAP(voQ) |
			_TXDMA_MGQ_MAP(mgtQ) | _TXDMA_HIQ_MAP(hiQ);

	rtw_write16(Adapter, REG_TRXDMA_CTRL, value16);
}

static VOID
_InitNormalChipOneOutEpPriority(
	IN	PADAPTER Adapter
)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

	u16	value = 0;
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
		/* RT_ASSERT(FALSE,("Shall not reach here!\n")); */
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
	u16			beQ, bkQ, viQ, voQ, mgtQ, hiQ;


	u16	valueHi = 0;
	u16	valueLow = 0;

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
		/* RT_ASSERT(FALSE,("Shall not reach here!\n")); */
		break;
	}

	if (!pregistrypriv->wifi_spec) {
		beQ		= valueLow;
		bkQ		= valueLow;
		viQ		= valueHi;
		voQ		= valueHi;
		mgtQ	= valueHi;
		hiQ		= valueHi;
	} else { /* for WMM ,CONFIG_OUT_EP_WIFI_MODE */
		beQ		= valueLow;
		bkQ		= valueHi;
		viQ		= valueHi;
		voQ		= valueLow;
		mgtQ	= valueHi;
		hiQ		= valueHi;
	}

	_InitNormalChipRegPriority(Adapter, beQ, bkQ, viQ, voQ, mgtQ, hiQ);

}

static VOID
_InitNormalChipThreeOutEpPriority(
	IN	PADAPTER padapter
)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	u16			beQ, bkQ, viQ, voQ, mgtQ, hiQ;

	if (!pregistrypriv->wifi_spec) { /* typical setting */
		beQ		= QUEUE_LOW;
		bkQ		= QUEUE_LOW;
		viQ		= QUEUE_NORMAL;
		voQ		= QUEUE_HIGH;
		mgtQ	= QUEUE_HIGH;
		hiQ		= QUEUE_HIGH;
	} else { /* for WMM */
		beQ		= QUEUE_LOW;
		bkQ		= QUEUE_NORMAL;
		viQ		= QUEUE_NORMAL;
		voQ		= QUEUE_HIGH;
		mgtQ	= QUEUE_HIGH;
		hiQ		= QUEUE_HIGH;
	}
	_InitNormalChipRegPriority(padapter, beQ, bkQ, viQ, voQ, mgtQ, hiQ);
}

static VOID
_InitNormalChipQueuePriority(
	IN	PADAPTER Adapter
)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

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
		/* RT_ASSERT(FALSE,("Shall not reach here!\n")); */
		break;
	}


}

static void _InitQueuePriority(PADAPTER padapter)
{
	_InitNormalChipQueuePriority(padapter);
}

static void _InitPageBoundary(PADAPTER padapter)
{
	/* RX Page Boundary */
	u16 rxff_bndy = RX_DMA_BOUNDARY_8723B;

	rtw_write16(padapter, (REG_TRXFF_BNDY + 2), rxff_bndy);
}

static void _InitTransferPageSize(PADAPTER padapter)
{
	/* Tx page size is always 128. */

	u8 value8;
	value8 = _PSRX(PBP_128) | _PSTX(PBP_128);
	rtw_write8(padapter, REG_PBP, value8);
}

void _InitDriverInfoSize(PADAPTER padapter, u8 drvInfoSize)
{
	rtw_write8(padapter, REG_RX_DRVINFO_SZ, drvInfoSize);
}

void _InitNetworkType(PADAPTER padapter)
{
	u32 value32;

	value32 = rtw_read32(padapter, REG_CR);

	/* TODO: use the other function to set network type
	*	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AD_HOC); */
	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AP);

	rtw_write32(padapter, REG_CR, value32);
}

void _InitWMACSetting(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	u16 value16;
	u32 rcr;

	pHalData = GET_HAL_DATA(padapter);

	rcr = 0
		| RCR_APM | RCR_AM | RCR_AB
		| RCR_CBSSID_DATA | RCR_CBSSID_BCN | RCR_AMF
		| RCR_HTC_LOC_CTRL
		| RCR_APP_PHYST_RXFF | RCR_APP_ICV | RCR_APP_MIC
		#ifdef CONFIG_MAC_LOOPBACK_DRIVER
		| RCR_AAP
		| RCR_ADD3 | RCR_APWRMGT | RCR_ACRC32 | RCR_ADF
		#endif
		;
	rtw_hal_set_hwreg(padapter, HW_VAR_RCR, (u8 *)&rcr);

	/* Accept all multicast address */
	rtw_write32(padapter, REG_MAR, 0xFFFFFFFF);
	rtw_write32(padapter, REG_MAR + 4, 0xFFFFFFFF);

	/* Accept all data frames */
	value16 = 0xFFFF;
	rtw_write16(padapter, REG_RXFLTMAP2, value16);

	/* 2010.09.08 hpfan */
	/* Since ADF is removed from RCR, ps-poll will not be indicate to driver, */
	/* RxFilterMap should mask ps-poll to gurantee AP mode can rx ps-poll. */
	value16 = 0x400;
	rtw_write16(padapter, REG_RXFLTMAP1, value16);

	/* Accept all management frames */
	value16 = 0xFFFF;
	rtw_write16(padapter, REG_RXFLTMAP0, value16);
}

void _InitAdaptiveCtrl(PADAPTER padapter)
{
	u16	value16;
	u32	value32;

	/* Response Rate Set */
	value32 = rtw_read32(padapter, REG_RRSR);
	value32 &= ~RATE_BITMAP_ALL;
	value32 |= RATE_RRSR_CCK_ONLY_1M;
	rtw_write32(padapter, REG_RRSR, value32);

	/* CF-END Threshold */
	/* m_spIoBase->rtw_write8(REG_CFEND_TH, 0x1); */

	/* SIFS (used in NAV) */
	value16 = _SPEC_SIFS_CCK(0x10) | _SPEC_SIFS_OFDM(0x10);
	rtw_write16(padapter, REG_SPEC_SIFS, value16);

	/* Retry Limit */
	value16 = _LRL(RL_VAL_STA) | _SRL(RL_VAL_STA);
	rtw_write16(padapter, REG_RL, value16);
}

void _InitEDCA(PADAPTER padapter)
{
	/* Set Spec SIFS (used in NAV) */
	rtw_write16(padapter, REG_SPEC_SIFS, 0x100a);
	rtw_write16(padapter, REG_MAC_SPEC_SIFS, 0x100a);

	/* Set SIFS for CCK */
	rtw_write16(padapter, REG_SIFS_CTX, 0x100a);

	/* Set SIFS for OFDM */
	rtw_write16(padapter, REG_SIFS_TRX, 0x100a);

	/* TXOP */
	rtw_write32(padapter, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtw_write32(padapter, REG_EDCA_BK_PARAM, 0x0000A44F);
	rtw_write32(padapter, REG_EDCA_VI_PARAM, 0x005EA324);
	rtw_write32(padapter, REG_EDCA_VO_PARAM, 0x002FA226);
}

void _InitRetryFunction(PADAPTER padapter)
{
	u8	value8;

	value8 = rtw_read8(padapter, REG_FWHW_TXQ_CTRL);
	value8 |= EN_AMPDU_RTY_NEW;
	rtw_write8(padapter, REG_FWHW_TXQ_CTRL, value8);

	/* Set ACK timeout */
	rtw_write8(padapter, REG_ACKTO, 0x40);
}

static void HalRxAggr8723BSdio(PADAPTER padapter)
{
	struct registry_priv *pregistrypriv;
	u8	valueDMATimeout;
	u8	valueDMAPageCount;


	pregistrypriv = &padapter->registrypriv;

	if (pregistrypriv->wifi_spec) {
		/* 2010.04.27 hpfan */
		/* Adjust RxAggrTimeout to close to zero disable RxAggr, suggested by designer */
		/* Timeout value is calculated by 34 / (2^n) */
		valueDMATimeout = 0x06;
		valueDMAPageCount = 0x06;
	} else {
		/* 20130530, Isaac@SD1 suggest 3 kinds of parameter */
#if 1
		/* TX/RX Balance */
		valueDMATimeout = 0x06;
		valueDMAPageCount = 0x06;
#endif
#if 0
		/* TX/RX Balance, but TCP ack may be late */
		valueDMATimeout = 0x16;
		valueDMAPageCount = 0x06;
#endif
#if 0
		/* RX Best */
		valueDMATimeout = 0x16;
		valueDMAPageCount = 0x08;
#endif
	}

#ifdef CONFIG_DONT_CARE_TP
	valueDMATimeout = 0x0f;
	valueDMAPageCount = 0x04;  /* RxAggUpthreshold = [4]*1K bytes+1.5k.  since RxAggUpthreshold+SzAmsdu(3839)<MaxRxBuffSize(8k), MaxvalueDMAPageCount=4. */
#endif
	rtw_write8(padapter, REG_RXDMA_AGG_PG_TH + 1, valueDMATimeout);
	rtw_write8(padapter, REG_RXDMA_AGG_PG_TH, valueDMAPageCount);
}

void sdio_AggSettingRxUpdate(PADAPTER padapter)
{
	HAL_DATA_TYPE *pHalData;
	u8 valueDMA;
	u8 valueRxAggCtrl = 0;
	u8 aggBurstNum = 3;  /* 0:1, 1:2, 2:3, 3:4 */
	u8 aggBurstSize = 0;  /* 0:1K, 1:512Byte, 2:256Byte... */

	pHalData = GET_HAL_DATA(padapter);

	valueDMA = rtw_read8(padapter, REG_TRXDMA_CTRL);
	valueDMA |= RXDMA_AGG_EN;
	rtw_write8(padapter, REG_TRXDMA_CTRL, valueDMA);

	valueRxAggCtrl |= RXDMA_AGG_MODE_EN;
	valueRxAggCtrl |= ((aggBurstNum << 2) & 0x0C);
	valueRxAggCtrl |= ((aggBurstSize << 4) & 0x30);
	rtw_write8(padapter, REG_RXDMA_MODE_CTRL_8723B, valueRxAggCtrl);/* RxAggLowThresh = 4*1K */
}

void _initSdioAggregationSetting(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	/* Tx aggregation setting
	*	sdio_AggSettingTxUpdate(padapter); */

	/* Rx aggregation setting */
	HalRxAggr8723BSdio(padapter);

	sdio_AggSettingRxUpdate(padapter);
}

static void _RXAggrSwitch(PADAPTER padapter, u8 enable)
{
	PHAL_DATA_TYPE pHalData;
	u8 valueDMA;
	u8 valueRxAggCtrl;


	pHalData = GET_HAL_DATA(padapter);

	valueDMA = rtw_read8(padapter, REG_TRXDMA_CTRL);
	valueRxAggCtrl = rtw_read8(padapter, REG_RXDMA_MODE_CTRL_8723B);

	if (_TRUE == enable) {
		valueDMA |= RXDMA_AGG_EN;
		valueRxAggCtrl |= RXDMA_AGG_MODE_EN;
	} else {
		valueDMA &= ~RXDMA_AGG_EN;
		valueRxAggCtrl &= ~RXDMA_AGG_MODE_EN;
	}

	rtw_write8(padapter, REG_TRXDMA_CTRL, valueDMA);
	rtw_write8(padapter, REG_RXDMA_MODE_CTRL_8723B, valueRxAggCtrl);
}

void _InitInterrupt(PADAPTER padapter)
{
	/*  */
	/* Initialize and enable SDIO Host Interrupt. */
	/*  */
	InitInterrupt8723BSdio(padapter);

	/*  */
	/* Initialize system Host Interrupt. */
	/*  */
	InitSysInterrupt8723BSdio(padapter);
}

void _InitRDGSetting(PADAPTER padapter)
{
	rtw_write8(padapter, REG_RD_CTRL, 0xFF);
	rtw_write16(padapter, REG_RD_NAV_NXT, 0x200);
	rtw_write8(padapter, REG_RD_RESP_PKT_TH, 0x05);
}

static void _InitRFType(PADAPTER padapter)
{
	struct registry_priv *pregpriv = &padapter->registrypriv;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);

#if	DISABLE_BB_RF
	pHalData->rf_chip	= RF_PSEUDO_11N;
	return;
#endif
	pHalData->rf_chip	= RF_6052;

	RTW_INFO("Set RF Chip ID to RF_6052 and RF type to %d.\n", pHalData->rf_type);
}

/* Set CCK and OFDM Block "ON" */
static void _BBTurnOnBlock(PADAPTER padapter)
{
#if (DISABLE_BB_RF)
	return;
#endif

	phy_set_bb_reg(padapter, rFPGA0_RFMOD, bCCKEn, 0x1);
	phy_set_bb_reg(padapter, rFPGA0_RFMOD, bOFDMEn, 0x1);
}

static void _RfPowerSave(PADAPTER padapter)
{
	/* YJ,TODO */
}

static void _InitAntenna_Selection(PADAPTER padapter)
{
	rtw_write8(padapter, REG_LEDCFG2, 0x82);
}

static void _InitPABias(PADAPTER padapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
	u8			pa_setting;

	/* FIXED PA current issue */
	/* efuse_one_byte_read(padapter, 0x1FA, &pa_setting); */
	efuse_OneByteRead(padapter, 0x1FA, &pa_setting, _FALSE);


	if (!(pa_setting & BIT0)) {
		phy_set_rf_reg(padapter, RF_PATH_A, 0x15, 0x0FFFFF, 0x0F406);
		phy_set_rf_reg(padapter, RF_PATH_A, 0x15, 0x0FFFFF, 0x4F406);
		phy_set_rf_reg(padapter, RF_PATH_A, 0x15, 0x0FFFFF, 0x8F406);
		phy_set_rf_reg(padapter, RF_PATH_A, 0x15, 0x0FFFFF, 0xCF406);
	}

	if (!(pa_setting & BIT4)) {
		pa_setting = rtw_read8(padapter, 0x16);
		pa_setting &= 0x0F;
		rtw_write8(padapter, 0x16, pa_setting | 0x80);
		rtw_write8(padapter, 0x16, pa_setting | 0x90);
	}
}

/*
 * 2010/08/09 MH Add for power down check.
 *   */
static BOOLEAN HalDetectPwrDownMode(PADAPTER Adapter)
{
	u8 tmpvalue;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(Adapter);


	EFUSE_ShadowRead(Adapter, 1, EEPROM_FEATURE_OPTION_8723B, (u32 *)&tmpvalue);

	/* 2010/08/25 MH INF priority > PDN Efuse value. */
	if (tmpvalue & BIT4 && pwrctrlpriv->reg_pdnmode)
		pHalData->pwrdown = _TRUE;
	else
		pHalData->pwrdown = _FALSE;

	RTW_INFO("HalDetectPwrDownMode(): PDN=%d\n", pHalData->pwrdown);

	return pHalData->pwrdown;
}	/* HalDetectPwrDownMode */

static u32 rtl8723bs_hal_init(PADAPTER padapter)
{
	s32 ret;
	PHAL_DATA_TYPE pHalData;
	struct pwrctrl_priv *pwrctrlpriv;
	struct registry_priv *pregistrypriv;
	struct sreset_priv *psrtpriv;
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	rt_rf_power_state eRfPowerStateToSet;
	u32 NavUpper = WiFiNavUpperUs;
	u8 u1bTmp;
	u16 value16;
	u8 typeid;
	u32 u4Tmp;

	pHalData = GET_HAL_DATA(padapter);
	psrtpriv = &pHalData->srestpriv;
	pwrctrlpriv = adapter_to_pwrctl(padapter);
	pregistrypriv = &padapter->registrypriv;

/*For AW H64 platform rf interference issue only!*/
#ifdef CONFIG_SDIO_PAD_DRIVING_CTRL
	phy_set_bb_reg(padapter, 0x74, BIT7|BIT6|BIT5|BIT4, 0x2);
#endif
#ifdef CONFIG_SWLPS_IN_IPS
	if (adapter_to_pwrctl(padapter)->bips_processing == _TRUE) {
		u8 val8, bMacPwrCtrlOn = _TRUE;

		RTW_INFO("%s: run LPS flow in IPS\n", __FUNCTION__);

		/* ser rpwm */
		val8 = rtw_read8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HRPWM1);
		val8 &= 0x80;
		val8 += 0x80;
		val8 |= BIT(6);
		rtw_write8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HRPWM1, val8);

		adapter_to_pwrctl(padapter)->tog = (val8 + 0x80) & 0x80;

		rtw_mdelay_os(5); /* wait set rpwm already */

		ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, rtl8723B_leave_swlps_flow);
		if (ret == _FALSE) {
			RTW_INFO("%s: run LPS flow in IPS fail!\n", __FUNCTION__);
			return _FAIL;
		}

		rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);

		pHalData->LastHMEBoxNum = 0;

#ifdef CONFIG_BT_COEXIST
		rtw_btcoex_HAL_Initialize(padapter, _FALSE);
#else
		rtw_btcoex_HAL_Initialize(padapter, _TRUE);
#endif /* CONFIG_BT_COEXIST */

		return _SUCCESS;
	}
#elif defined(CONFIG_FWLPS_IN_IPS)
	if (adapter_to_pwrctl(padapter)->bips_processing == _TRUE && psrtpriv->silent_reset_inprogress == _FALSE
	    && adapter_to_pwrctl(padapter)->pre_ips_type == 0) {
		systime start_time;
		u8 cpwm_orig, cpwm_now;
		u8 val8, bMacPwrCtrlOn = _TRUE;

		RTW_INFO("%s: Leaving IPS in FWLPS state\n", __FUNCTION__);

		/* for polling cpwm */
		cpwm_orig = 0;
		rtw_hal_get_hwreg(padapter, HW_VAR_CPWM, &cpwm_orig);

		/* ser rpwm */
		val8 = rtw_read8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HRPWM1);
		val8 &= 0x80;
		val8 += 0x80;
		val8 |= BIT(6);
		rtw_write8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HRPWM1, val8);
		RTW_INFO("%s: write rpwm=%02x\n", __FUNCTION__, val8);
		adapter_to_pwrctl(padapter)->tog = (val8 + 0x80) & 0x80;

		/* do polling cpwm */
		start_time = rtw_get_current_time();
		do {

			rtw_mdelay_os(1);

			rtw_hal_get_hwreg(padapter, HW_VAR_CPWM, &cpwm_now);
			if ((cpwm_orig ^ cpwm_now) & 0x80) {
#ifdef DBG_CHECK_FW_PS_STATE
				RTW_INFO("%s: polling cpwm ok when leaving IPS in FWLPS state, cpwm_orig=%02x, cpwm_now=%02x, 0x100=0x%x\n"
					, __FUNCTION__, cpwm_orig, cpwm_now, rtw_read8(padapter, REG_CR));
#endif /* DBG_CHECK_FW_PS_STATE */
				break;
			}

			if (rtw_get_passing_time_ms(start_time) > 100) {
				RTW_INFO("%s: polling cpwm timeout when leaving IPS in FWLPS state\n", __FUNCTION__);
				break;
			}
		} while (1);

		rtl8723b_set_FwPwrModeInIPS_cmd(padapter, 0);

		rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);

#ifdef CONFIG_BT_COEXIST
		rtw_btcoex_HAL_Initialize(padapter, _FALSE);
#else
		rtw_btcoex_HAL_Initialize(padapter, _TRUE);
#endif /* CONFIG_BT_COEXIST */

#ifdef DBG_CHECK_FW_PS_STATE
		if (rtw_fw_ps_state(padapter) == _FAIL) {
			RTW_INFO("after hal init, fw ps state in 32k\n");
			pdbgpriv->dbg_ips_drvopen_fail_cnt++;
		}
#endif /* DBG_CHECK_FW_PS_STATE */
		return _SUCCESS;
	}
#endif /* CONFIG_SWLPS_IN_IPS */

	/* Disable Interrupt first.
	*	rtw_hal_disable_interrupt(padapter); */

	if (rtw_read8(padapter, REG_MCUFWDL) == 0xc6)
		RTW_INFO("FW exist before power on!!\n");
	else
		RTW_INFO("FW does not exist before power on!!\n");

	if (rtw_fw_ps_state(padapter) == _FAIL) {
		RTW_INFO("check fw_ps_state fail before PowerOn!\n");
		pdbgpriv->dbg_ips_drvopen_fail_cnt++;
	}

	ret = rtw_hal_power_on(padapter);
	if (_FAIL == ret) {
		return _FAIL;
	}
	RTW_INFO("Power on ok!\n");

	if (rtw_fw_ps_state(padapter) == _FAIL) {
		RTW_INFO("check fw_ps_state fail after PowerOn!\n");
		pdbgpriv->dbg_ips_drvopen_fail_cnt++;
	}


	rtw_write8(padapter, REG_EARLY_MODE_CONTROL, 0);

	/* if (padapter->registrypriv.mp_mode == 0) */
	{
		ret = rtl8723b_FirmwareDownload(padapter, _FALSE);
		if (ret != _SUCCESS) {
			pHalData->bFWReady = _FALSE;
			pHalData->fw_ractrl = _FALSE;
			return ret;
		} else {
			pHalData->bFWReady = _TRUE;
			pHalData->fw_ractrl = _TRUE;
		}
	}

	/*	SIC_Init(padapter); */

	if (pwrctrlpriv->reg_rfoff == _TRUE)
		pwrctrlpriv->rf_pwrstate = rf_off;

	/* 2010/08/09 MH We need to check if we need to turnon or off RF after detecting */
	/* HW GPIO pin. Before PHY_RFConfig8192C. */
	HalDetectPwrDownMode(padapter);

	/* Set RF type for BB/RF configuration */
	_InitRFType(padapter);

	/* Save target channel */
	/* <Roger_Notes> Current Channel will be updated again later. */
	pHalData->current_channel = 6;

#if (HAL_MAC_ENABLE == 1)
	ret = PHY_MACConfig8723B(padapter);
	if (ret != _SUCCESS) {
		return ret;
	}
#endif
	/*  */
	/* d. Initialize BB related configurations. */
	/*  */
#if (HAL_BB_ENABLE == 1)
	ret = PHY_BBConfig8723B(padapter);
	if (ret != _SUCCESS) {
		return ret;
	}
#endif

	/* If RF is on, we need to init RF. Otherwise, skip the procedure. */
	/* We need to follow SU method to change the RF cfg.txt. Default disable RF TX/RX mode. */
	/* if(pHalData->eRFPowerState == eRfOn) */
	{
#if (HAL_RF_ENABLE == 1)
		ret = PHY_RFConfig8723B(padapter);
		if (ret != _SUCCESS) {
			return ret;
		}
#endif
	}

	/*  */
	/* Joseph Note: Keep RfRegChnlVal for later use. */
	/*  */
	pHalData->RfRegChnlVal[0] = phy_query_rf_reg(padapter, RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask);
	pHalData->RfRegChnlVal[1] = phy_query_rf_reg(padapter, RF_PATH_B, RF_CHNLBW, bRFRegOffsetMask);


	/* if (!pHalData->bMACFuncEnable) { */
	_InitQueueReservedPage(padapter);
	_InitTxBufferBoundary(padapter);

	/* init LLT after tx buffer boundary is defined */
	ret = rtl8723b_InitLLTTable(padapter);
	if (_SUCCESS != ret) {
		RTW_INFO("%s: Failed to init LLT Table!\n", __FUNCTION__);
		return _FAIL;
	}
	/* } */
	_InitQueuePriority(padapter);
	_InitPageBoundary(padapter);
	_InitTransferPageSize(padapter);

	/* Get Rx PHY status in order to report RSSI and others. */
	_InitDriverInfoSize(padapter, DRVINFO_SZ);
	_InitNetworkType(padapter);
	_InitWMACSetting(padapter);
	_InitAdaptiveCtrl(padapter);
	_InitEDCA(padapter);
	_InitRetryFunction(padapter);
	_initSdioAggregationSetting(padapter);

	rtl8723b_InitBeaconParameters(padapter);
	rtl8723b_InitBeaconMaxError(padapter, _TRUE);
	_InitInterrupt(padapter);
	_InitBurstPktLen_8723BS(padapter);

	/* YJ,TODO */
	rtw_write8(padapter, REG_SECONDARY_CCA_CTRL_8723B, 0x3);	/* CCA */
	rtw_write8(padapter, 0x976, 0);	/* hpfan_todo: 2nd CCA related */

#if defined(CONFIG_CONCURRENT_MODE) || defined(CONFIG_TX_MCAST2UNI)

#ifdef CONFIG_CHECK_AC_LIFETIME
	/* Enable lifetime check for the four ACs */
	rtw_write8(padapter, REG_LIFETIME_CTRL, rtw_read8(padapter, REG_LIFETIME_CTRL) | 0x0F);
#endif /* CONFIG_CHECK_AC_LIFETIME */

#ifdef CONFIG_TX_MCAST2UNI
	rtw_write16(padapter, REG_PKT_VO_VI_LIFE_TIME, 0x0400);	/* unit: 256us. 256ms */
	rtw_write16(padapter, REG_PKT_BE_BK_LIFE_TIME, 0x0400);	/* unit: 256us. 256ms */
#else	/* CONFIG_TX_MCAST2UNI */
	rtw_write16(padapter, REG_PKT_VO_VI_LIFE_TIME, 0x3000);	/* unit: 256us. 3s */
	rtw_write16(padapter, REG_PKT_BE_BK_LIFE_TIME, 0x3000);	/* unit: 256us. 3s */
#endif /* CONFIG_TX_MCAST2UNI */
#endif /* CONFIG_CONCURRENT_MODE || CONFIG_TX_MCAST2UNI */


	invalidate_cam_all(padapter);

	rtw_hal_set_chnl_bw(padapter, padapter->registrypriv.channel,
		CHANNEL_WIDTH_20, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HAL_PRIME_CHNL_OFFSET_DONT_CARE);

	rtl8723b_InitAntenna_Selection(padapter);

	/*  */
	/* Disable BAR, suggested by Scott */
	/* 2010.04.09 add by hpfan */
	/*  */
	rtw_write32(padapter, REG_BAR_MODE_CTRL, 0x0201ffff);

	/* HW SEQ CTRL */
	/* set 0x0 to 0xFF by tynli. Default enable HW SEQ NUM. */
	rtw_write8(padapter, REG_HWSEQ_CTRL, 0xFF);


#ifdef CONFIG_MAC_LOOPBACK_DRIVER
	u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN);
	u1bTmp &= ~(FEN_BBRSTB | FEN_BB_GLB_RSTn);
	rtw_write8(padapter, REG_SYS_FUNC_EN, u1bTmp);

	rtw_write8(padapter, REG_RD_CTRL, 0x0F);
	rtw_write8(padapter, REG_RD_CTRL + 1, 0xCF);
	rtw_write8(padapter, REG_TXPKTBUF_WMAC_LBK_BF_HD, 0x80);
	rtw_write32(padapter, REG_CR, 0x0b0202ff);
#endif

	/*
	 * Configure SDIO TxRx Control to enable Rx DMA timer masking.
	 * 2010.02.24.
	 * Only clear necessary bits 0x0[2:0] and 0x2[15:0] and keep 0x0[15:3]
	 * 2015.03.19.
	 */
	u4Tmp = rtw_read32(padapter, SDIO_LOCAL_BASE | SDIO_REG_TX_CTRL);
	u4Tmp &= 0x0000FFF8;
	rtw_write32(padapter, SDIO_LOCAL_BASE | SDIO_REG_TX_CTRL, u4Tmp);

	_RfPowerSave(padapter);

	rtl8723b_InitHalDm(padapter);

	/* dbg_print("pHalData->DefaultTxPwrDbm = %d\n", pHalData->DefaultTxPwrDbm); */

	/* if(pHalData->SwBeaconType < HAL92CSDIO_DEFAULT_BEACON_TYPE) */ /* The lowest Beacon Type that HW can support */
	/*		pHalData->SwBeaconType = HAL92CSDIO_DEFAULT_BEACON_TYPE; */

	/*  */
	/* Update current Tx FIFO page status. */
	/*  */
	HalQueryTxBufferStatus8723BSdio(padapter);
	HalQueryTxOQTBufferStatus8723BSdio(padapter);
	pHalData->SdioTxOQTMaxFreeSpace = pHalData->SdioTxOQTFreeSpace;

	/* Enable MACTXEN/MACRXEN block */
	u1bTmp = rtw_read8(padapter, REG_CR);
	u1bTmp |= (MACTXEN | MACRXEN);
	rtw_write8(padapter, REG_CR, u1bTmp);

	rtw_hal_set_hwreg(padapter, HW_VAR_NAV_UPPER, (u8 *)&NavUpper);

#ifdef CONFIG_XMIT_ACK
	/* ack for xmit mgmt frames. */
	rtw_write32(padapter, REG_FWHW_TXQ_CTRL, rtw_read32(padapter, REG_FWHW_TXQ_CTRL) | BIT(12));
#endif /* CONFIG_XMIT_ACK	 */

	/*	pHalData->PreRpwmVal = SdioLocalCmd52Read1Byte(padapter, SDIO_REG_HRPWM1) & 0x80; */

#if (MP_DRIVER == 1)
	if (padapter->registrypriv.mp_mode == 1) {
		padapter->mppriv.channel = pHalData->current_channel;
		MPT_InitializeAdapter(padapter, padapter->mppriv.channel);
	} else
#endif /* #if (MP_DRIVER == 1) */
	{
		pwrctrlpriv->rf_pwrstate = rf_on;

		if (pwrctrlpriv->rf_pwrstate == rf_on) {
			struct pwrctrl_priv *pwrpriv;
			systime start_time;
			u8 restore_iqk_rst;
			u8 b2Ant;
			u8 h2cCmdBuf;

			pwrpriv = adapter_to_pwrctl(padapter);

			/*phy_lc_calibrate_8723b(&pHalData->odmpriv);*/
			halrf_lck_trigger(&pHalData->odmpriv);

			/* Inform WiFi FW that it is the beginning of IQK */
			h2cCmdBuf = 1;
			FillH2CCmd8723B(padapter, H2C_8723B_BT_WLAN_CALIBRATION, 1, &h2cCmdBuf);

			start_time = rtw_get_current_time();
			do {
				if (rtw_read8(padapter, 0x1e7) & 0x01)
					break;

				rtw_msleep_os(50);
			} while (rtw_get_passing_time_ms(start_time) <= 400);

#ifdef CONFIG_BT_COEXIST
			rtw_btcoex_IQKNotify(padapter, _TRUE);
#endif
			restore_iqk_rst = (pwrpriv->bips_processing == _TRUE) ? _TRUE : _FALSE;
			b2Ant = pHalData->EEPROMBluetoothAntNum == Ant_x2 ? _TRUE : _FALSE;
			/*phy_iq_calibrate_8723b(padapter, _FALSE, restore_iqk_rst, b2Ant, pHalData->ant_path);*/
			halrf_iqk_trigger(&pHalData->odmpriv, restore_iqk_rst);
			pHalData->bIQKInitialized = _TRUE;
#ifdef CONFIG_BT_COEXIST
			rtw_btcoex_IQKNotify(padapter, _FALSE);
#endif

			/* Inform WiFi FW that it is the finish of IQK */
			h2cCmdBuf = 0;
			FillH2CCmd8723B(padapter, H2C_8723B_BT_WLAN_CALIBRATION, 1, &h2cCmdBuf);

			odm_txpowertracking_check(&pHalData->odmpriv);
		}
	}

#ifdef CONFIG_BT_COEXIST
	/* Init BT hw config. */
	rtw_btcoex_HAL_Initialize(padapter, _FALSE);
#else
	rtw_btcoex_HAL_Initialize(padapter, _TRUE);
#endif


	return _SUCCESS;
}

/*
 * Description:
 *	RTL8723e card disable power sequence v003 which suggested by Scott.
 *
 * First created by tynli. 2011.01.28.
 *   */
static void CardDisableRTL8723BSdio(PADAPTER padapter)
{
	u8		u1bTmp;
	u16		u2bTmp;
	u32		u4bTmp;
	u8		bMacPwrCtrlOn;
	u8		ret = _FAIL;

	/* Run LPS WL RFOFF flow */
	ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, rtl8723B_enter_lps_flow);
	if (ret == _FAIL)
		RTW_ERR("%s: run RF OFF flow fail!\n", __func__);

	/*	==== Reset digital sequence   ====== */

	u1bTmp = rtw_read8(padapter, REG_MCUFWDL);
	if ((u1bTmp & RAM_DL_SEL) && GET_HAL_DATA(padapter)->bFWReady) /* 8051 RAM code */
		rtl8723b_FirmwareSelfReset(padapter);

	/* Reset MCU 0x2[10]=0. Suggested by Filen. 2011.01.26. by tynli. */
	u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN + 1);
	u1bTmp &= ~BIT(2);	/* 0x2[10], FEN_CPUEN */
	rtw_write8(padapter, REG_SYS_FUNC_EN + 1, u1bTmp);

	/* MCUFWDL 0x80[1:0]=0 */
	/* reset MCU ready status */
	rtw_write8(padapter, REG_MCUFWDL, 0);

	/* Reset MCU IO Wrapper, added by Roger, 2011.08.30 */
	u1bTmp = rtw_read8(padapter, REG_RSV_CTRL + 1);
	u1bTmp &= ~BIT(0);
	rtw_write8(padapter, REG_RSV_CTRL + 1, u1bTmp);
	u1bTmp = rtw_read8(padapter, REG_RSV_CTRL + 1);
	u1bTmp |= BIT(0);
	rtw_write8(padapter, REG_RSV_CTRL + 1, u1bTmp);

	/*	==== Reset digital sequence end ====== */

	bMacPwrCtrlOn = _FALSE;	/* Disable CMD53 R/W */
	ret = _FALSE;
	rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, rtl8723B_card_disable_flow);
	if (ret == _FALSE)
		RTW_ERR("%s: run CARD DISABLE flow fail!\n", __func__);

	GET_HAL_DATA(padapter)->bFWReady = _FALSE;
}

static u32 rtl8723bs_hal_deinit(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	struct sreset_priv *psrtpriv = &pHalData->srestpriv;
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

#ifdef CONFIG_MP_INCLUDED
	if (padapter->registrypriv.mp_mode == 1)
		MPT_DeInitAdapter(padapter);
#endif

	if (rtw_is_hw_init_completed(padapter)) {
#ifdef CONFIG_SWLPS_IN_IPS
		if (adapter_to_pwrctl(padapter)->bips_processing == _TRUE) {
			u8	bMacPwrCtrlOn;
			u8 ret =  _TRUE;

			RTW_INFO("%s: run LPS flow in IPS\n", __FUNCTION__);

			rtw_write32(padapter, 0x130, 0x0);
			rtw_write32(padapter, 0x138, 0x100);
			rtw_write8(padapter, 0x13d, 0x1);


			bMacPwrCtrlOn = _FALSE;	/* Disable CMD53 R/W	 */
			rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);

			ret = HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, rtl8723B_enter_swlps_flow);
			if (ret == _FALSE) {
				RTW_INFO("%s: run LPS flow in IPS fail!\n", __FUNCTION__);
				return _FAIL;
			}
		} else
#elif defined(CONFIG_FWLPS_IN_IPS)
		if (adapter_to_pwrctl(padapter)->bips_processing == _TRUE && psrtpriv->silent_reset_inprogress == _FALSE) {
			if (padapter->netif_up == _TRUE) {
				int cnt = 0;
				u8 val8 = 0;

				RTW_INFO("%s: issue H2C to FW when entering IPS\n", __FUNCTION__);

				rtl8723b_set_FwPwrModeInIPS_cmd(padapter, 0x1);
				/* poll 0x1cc to make sure H2C command already finished by FW; MAC_0x1cc=0 means H2C done by FW. */
				do {
					val8 = rtw_read8(padapter, REG_HMETFR);
					cnt++;
					RTW_INFO("%s  polling REG_HMETFR=0x%x, cnt=%d\n", __FUNCTION__, val8, cnt);
					rtw_mdelay_os(10);
				} while (cnt < 100 && (val8 != 0));
				/* H2C done, enter 32k */
				if (val8 == 0) {
					/* ser rpwm to enter 32k */
					val8 = rtw_read8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HRPWM1);
					val8 += 0x80;
					val8 |= BIT(0);
					rtw_write8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HRPWM1, val8);
					RTW_INFO("%s: write rpwm=%02x\n", __FUNCTION__, val8);
					adapter_to_pwrctl(padapter)->tog = (val8 + 0x80) & 0x80;
					cnt = val8 = 0;
					do {
						val8 = rtw_read8(padapter, REG_CR);
						cnt++;
						RTW_INFO("%s  polling 0x100=0x%x, cnt=%d\n", __FUNCTION__, val8, cnt);
						rtw_mdelay_os(10);
					} while (cnt < 100 && (val8 != 0xEA));
#ifdef DBG_CHECK_FW_PS_STATE
					if (val8 != 0xEA)
						RTW_INFO("MAC_1C0=%08x, MAC_1C4=%08x, MAC_1C8=%08x, MAC_1CC=%08x\n", rtw_read32(padapter, 0x1c0), rtw_read32(padapter, 0x1c4)
							, rtw_read32(padapter, 0x1c8), rtw_read32(padapter, 0x1cc));
#endif /* DBG_CHECK_FW_PS_STATE */
				} else {
					RTW_INFO("MAC_1C0=%08x, MAC_1C4=%08x, MAC_1C8=%08x, MAC_1CC=%08x\n", rtw_read32(padapter, 0x1c0), rtw_read32(padapter, 0x1c4)
						, rtw_read32(padapter, 0x1c8), rtw_read32(padapter, 0x1cc));
				}

				RTW_INFO("polling done when entering IPS, check result : 0x100=0x%x, cnt=%d, MAC_1cc=0x%02x\n"
					, rtw_read8(padapter, REG_CR), cnt, rtw_read8(padapter, REG_HMETFR));

				adapter_to_pwrctl(padapter)->pre_ips_type = 0;

			} else {
				pdbgpriv->dbg_carddisable_cnt++;
#ifdef DBG_CHECK_FW_PS_STATE
				if (rtw_fw_ps_state(padapter) == _FAIL) {
					RTW_INFO("card disable should leave 32k\n");
					pdbgpriv->dbg_carddisable_error_cnt++;
				}
#endif /* DBG_CHECK_FW_PS_STATE */
				rtw_hal_power_off(padapter);

				adapter_to_pwrctl(padapter)->pre_ips_type = 1;
			}

		} else
#endif /* CONFIG_SWLPS_IN_IPS */
		{
			pdbgpriv->dbg_carddisable_cnt++;
#ifdef DBG_CHECK_FW_PS_STATE
			if (rtw_fw_ps_state(padapter) == _FAIL) {
				RTW_INFO("card disable should leave 32k\n");
				pdbgpriv->dbg_carddisable_error_cnt++;
			}
#endif /* DBG_CHECK_FW_PS_STATE */
			rtw_hal_power_off(padapter);
		}
	} else
		pdbgpriv->dbg_deinit_fail_cnt++;

	return _SUCCESS;
}
static void rtl8723bs_init_default_value(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;


	pHalData = GET_HAL_DATA(padapter);

	rtl8723b_init_default_value(padapter);

	/* interface related variable */
	pHalData->SdioRxFIFOCnt = 0;
}

static void rtl8723bs_interface_configure(PADAPTER padapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
	struct dvobj_priv		*pdvobjpriv = adapter_to_dvobj(padapter);
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	BOOLEAN		bWiFiConfig	= pregistrypriv->wifi_spec;


	/* F-Cut patch for IC always can response SDIO recognition commands */
	if (IS_F_CUT(pHalData->version_id)) {
		u32 addr;
		u8 val8;

		/*
		 * Always response recognition command
		 * SDIO Local Register 0x00[4] = 1
		 */
		addr = SDIO_LOCAL_BASE | SDIO_REG_TX_CTRL;
		val8 = rtw_read8(padapter, addr);
		if (!(val8 & BIT(4))) {
			val8 |= BIT(4);
			rtw_write8(padapter, addr, val8);
		}
	}

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

/*
 *	Description:
 *		We should set Efuse cell selection to WiFi cell in default.
 *
 *	Assumption:
 *		PASSIVE_LEVEL
 *
 *	Added by Roger, 2010.11.23.
 *   */
static void
_EfuseCellSel(
	IN	PADAPTER	padapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	u32			value32;

	/* if(INCLUDE_MULTI_FUNC_BT(padapter)) */
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

static VOID
_ReadEfuseInfo8723BS(
	IN PADAPTER			padapter
)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u8			*hwinfo = NULL;


	/*  */
	/* This part read and parse the eeprom/efuse content */
	/*  */

	if (sizeof(pHalData->efuse_eeprom_data) < HWSET_MAX_SIZE_8723B)
		RTW_INFO("[WARNING] size of efuse_eeprom_data is less than HWSET_MAX_SIZE_8723B!\n");

	hwinfo = pHalData->efuse_eeprom_data;

	Hal_InitPGData(padapter, hwinfo);

	Hal_EfuseParseIDCode(padapter, hwinfo);
	Hal_EfuseParseEEPROMVer_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);
	hal_config_macaddr(padapter, pHalData->bautoload_fail_flag);
	Hal_EfuseParseTxPowerInfo_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseBoardType_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);

	/*  */
	/* Read Bluetooth co-exist and initialize */
	/*  */
	Hal_EfuseParseBTCoexistInfo_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseChnlPlan_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseXtal_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParsePackageType_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseThermalMeter_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseAntennaDiversity_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseCustomerID_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);

	Hal_EfuseParseVoltage_8723B(padapter, hwinfo, pHalData->bautoload_fail_flag);

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	Hal_DetectWoWMode(padapter);
#endif

#ifdef CONFIG_RF_POWER_TRIM
	Hal_ReadRFGainOffset(padapter, hwinfo, pHalData->bautoload_fail_flag);
#endif	/*CONFIG_RF_POWER_TRIM*/

}

static void _ReadPROMContent(
	IN PADAPTER		padapter
)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u8			eeValue;

	eeValue = rtw_read8(padapter, REG_9346CR);
	/* To check system boot selection. */
	pHalData->EepromOrEfuse = (eeValue & BOOT_FROM_EEPROM) ? _TRUE : _FALSE;
	pHalData->bautoload_fail_flag = (eeValue & EEPROM_EN) ? _FALSE : _TRUE;


	/*	pHalData->EEType = IS_BOOT_FROM_EEPROM(Adapter) ? EEPROM_93C46 : EEPROM_BOOT_EFUSE; */

	_ReadEfuseInfo8723BS(padapter);
}

static VOID
_InitOtherVariable(
	IN PADAPTER		Adapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);


	/* if(Adapter->bInHctTest){ */
	/*	pMgntInfo->PowerSaveControl.bInactivePs = FALSE; */
	/*	pMgntInfo->PowerSaveControl.bIPSModeBackup = FALSE; */
	/*	pMgntInfo->PowerSaveControl.bLeisurePs = FALSE; */
	/*	pMgntInfo->keepAliveLevel = 0; */
	/* } */



}

/*
 *	Description:
 *		Read HW adapter information by E-Fuse or EEPROM according CR9346 reported.
 *
 *	Assumption:
 *		PASSIVE_LEVEL (SDIO interface)
 *
 *   */
static u8 ReadAdapterInfo8723BS(PADAPTER padapter)
{
	u8 val8;

	val8 = rtw_read8(padapter, 0x4e);
	RTW_INFO("%s, 0x4e=0x%x\n", __func__, val8);
	val8 |= BIT(6);
	rtw_write8(padapter, 0x4e, val8);

	/* Read EEPROM size before call any EEPROM function */
	padapter->EepromAddressSize = GetEEPROMSize8723B(padapter);

	_EfuseCellSel(padapter);
	_ReadRFType(padapter);
	_ReadPROMContent(padapter);
	_InitOtherVariable(padapter);

#ifdef CONFIG_PLATFORM_INTEL_BYT
	{ /* for BT, let BT can control ANT when wifi disable */
		u32 val32;
		RTW_INFO("%s, 0x4c=0x%x\n", __func__, rtw_read32(padapter, 0x4c));
		val32 = rtw_read32(padapter, 0x64);
		RTW_INFO("%s, 0x64=0x%x\n", __func__, val32);
		val32 |= BIT(13);
		rtw_write32(padapter, 0x64, val32);
		RTW_INFO("%s, 0x64=0x%x\n", __func__, rtw_read32(padapter, 0x64));
	}
#endif /* CONFIG_PLATFORM_INTEL_BYT */

	return _SUCCESS;
}

/*
 * If variable not handled here,
 * some variables will be processed in SetHwReg8723B()
 */
u8 SetHwReg8723BS(PADAPTER padapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE pHalData;
	u8 ret = _SUCCESS;
	u8 val8;


	pHalData = GET_HAL_DATA(padapter);

	switch (variable) {
	case HW_VAR_SET_RPWM:
		/* rpwm value only use BIT0(clock bit) ,BIT6(Ack bit), and BIT7(Toggle bit) */
		/* BIT0 value - 1: 32k, 0:40MHz. */
		/* BIT6 value - 1: report cpwm value after success set, 0:do not report. */
		/* BIT7 value - Toggle bit change. */
	{
		val8 = *val;
		val8 &= 0xC1;
		rtw_write8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HRPWM1, val8);
	}
	break;
	case HW_VAR_SET_REQ_FW_PS:
	/* 1. driver write 0x8f[4]=1  */ { /* request fw ps state (only can write bit4) */
		u8 req_fw_ps = 0;
		req_fw_ps = rtw_read8(padapter, 0x8f);
		req_fw_ps |= 0x10;
		rtw_write8(padapter, 0x8f, req_fw_ps);
	}
	break;
	case HW_VAR_RXDMA_AGG_PG_TH:
		val8 = *val;

		/* TH=1 => invalidate RX DMA aggregation */
		/* TH=0 => validate RX DMA aggregation, use init value. */
		if (val8 == 0) {
			/* enable RXDMA aggregation */
			/* _RXAggrSwitch(padapter, _TRUE); */
		} else {
			/* disable RXDMA aggregation */
			/* _RXAggrSwitch(padapter, _FALSE); */
		}
		break;
	default:
		ret = SetHwReg8723B(padapter, variable, val);
		break;
	}

	return ret;
}

/*
 * If variable not handled here,
 * some variables will be processed in GetHwReg8723B()
 */
void GetHwReg8723BS(PADAPTER padapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);


	switch (variable) {
	case HW_VAR_CPWM:
		*val = rtw_read8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HCPWM1_8723B);
		break;

	case HW_VAR_FW_PS_STATE: {
		/* 3. read dword 0x88               */ /* driver read fw ps state */
		*((u16 *)val) = rtw_read16(padapter, 0x88);
	}
	break;
	default:
		GetHwReg8723B(padapter, variable, val);
		break;
	}

}

/*
 *	Description:
 *		Query setting of specified variable.
 *   */
u8
GetHalDefVar8723BSDIO(
	IN	PADAPTER				Adapter,
	IN	HAL_DEF_VARIABLE		eVariable,
	IN	PVOID					pValue
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _SUCCESS;

	switch (eVariable) {
	case HAL_DEF_IS_SUPPORT_ANT_DIV:
#ifdef CONFIG_ANTENNA_DIVERSITY
		*((u8 *)pValue) = _FALSE;
#endif
		break;

	case HW_VAR_MAX_RX_AMPDU_FACTOR:
		/* Stanley@BB.SD3 suggests 16K can get stable performance */
		/* coding by Lucas@20130730 */
		*(HT_CAP_AMPDU_FACTOR *)pValue = MAX_AMPDU_FACTOR_16K;
		break;
	default:
		bResult = GetHalDefVar8723B(Adapter, eVariable, pValue);
		break;
	}

	return bResult;
}

/*
 *	Description:
 *		Change default setting of specified variable.
 *   */
u8
SetHalDefVar8723BSDIO(
	IN	PADAPTER				Adapter,
	IN	HAL_DEF_VARIABLE		eVariable,
	IN	PVOID					pValue
)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _SUCCESS;

	switch (eVariable) {
	default:
		bResult = SetHalDefVar8723B(Adapter, eVariable, pValue);
		break;
	}

	return bResult;
}

void rtl8723bs_set_hal_ops(PADAPTER padapter)
{
	struct hal_ops *pHalFunc = &padapter->hal_func;


	rtl8723b_set_hal_ops(pHalFunc);

	pHalFunc->hal_power_on = &_InitPowerOn_8723BS;
	pHalFunc->hal_power_off = &CardDisableRTL8723BSdio;

	pHalFunc->hal_init = &rtl8723bs_hal_init;
	pHalFunc->hal_deinit = &rtl8723bs_hal_deinit;

	pHalFunc->init_xmit_priv = &rtl8723bs_init_xmit_priv;
	pHalFunc->free_xmit_priv = &rtl8723bs_free_xmit_priv;

	pHalFunc->init_recv_priv = &rtl8723bs_init_recv_priv;
	pHalFunc->free_recv_priv = &rtl8723bs_free_recv_priv;
#ifdef CONFIG_RTW_SW_LED
	pHalFunc->InitSwLeds = &rtl8723bs_InitSwLeds;
	pHalFunc->DeInitSwLeds = &rtl8723bs_DeInitSwLeds;
#endif
	pHalFunc->init_default_value = &rtl8723bs_init_default_value;
	pHalFunc->intf_chip_configure = &rtl8723bs_interface_configure;
	pHalFunc->read_adapter_info = &ReadAdapterInfo8723BS;

	pHalFunc->enable_interrupt = &EnableInterrupt8723BSdio;
	pHalFunc->disable_interrupt = &DisableInterrupt8723BSdio;
	pHalFunc->check_ips_status = &CheckIPSStatus;

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	pHalFunc->clear_interrupt = &ClearInterrupt8723BSdio;
#endif
	pHalFunc->set_hw_reg_handler = &SetHwReg8723BS;
	pHalFunc->GetHwRegHandler = &GetHwReg8723BS;
	pHalFunc->get_hal_def_var_handler = &GetHalDefVar8723BSDIO;
	pHalFunc->SetHalDefVarHandler = &SetHalDefVar8723BSDIO;

	pHalFunc->hal_xmit = &rtl8723bs_hal_xmit;
	pHalFunc->mgnt_xmit = &rtl8723bs_mgnt_xmit;
	pHalFunc->hal_xmitframe_enqueue = &rtl8723bs_hal_xmitframe_enqueue;

#ifdef CONFIG_HOSTAPD_MLME
	pHalFunc->hostap_mgnt_xmit_entry = NULL;
#endif

#if defined(CONFIG_CHECK_BT_HANG) && defined(CONFIG_BT_COEXIST)
	pHalFunc->hal_init_checkbthang_workqueue = &rtl8723bs_init_checkbthang_workqueue;
	pHalFunc->hal_free_checkbthang_workqueue = &rtl8723bs_free_checkbthang_workqueue;
	pHalFunc->hal_cancle_checkbthang_workqueue = &rtl8723bs_cancle_checkbthang_workqueue;
	pHalFunc->hal_checke_bt_hang = &rtl8723bs_hal_check_bt_hang;
#endif

}
