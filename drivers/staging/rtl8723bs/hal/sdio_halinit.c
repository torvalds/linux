// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
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

static
u8 _InitPowerOn_8723BS(struct adapter *padapter)
{
	u8 value8;
	u16 value16;
	u32 value32;
	u8 ret;
/* 	u8 bMacPwrCtrlOn; */


	/*  all of these MUST be configured before power on */

	/*  only cmd52 can be used before power on(card enable) */
	ret = CardEnable(padapter);
	if (!ret)
		return _FAIL;

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

	value32 = rtw_read32(padapter, REG_LEDCFG0);
	value32 |= BIT(23); /*  DPDT_SEL_EN, 1 for SW control */
	rtw_write32(padapter, REG_LEDCFG0, value32);

	value8 = rtw_read8(padapter, REG_PAD_CTRL1_8723B);
	value8 &= ~BIT(0); /*  BIT_SW_DPDT_SEL_DATA, DPDT_SEL default configuration */
	rtw_write8(padapter, REG_PAD_CTRL1_8723B, value8);

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

	pHalData->rf_chip	= RF_6052;

	pHalData->rf_type = RF_1T1R;
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

		/* for polling cpwm */
		cpwm_orig = 0;
		rtw_hal_get_hwreg(padapter, HW_VAR_CPWM, &cpwm_orig);

		/* ser rpwm */
		val8 = rtw_read8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HRPWM1);
		val8 &= 0x80;
		val8 += 0x80;
		val8 |= BIT(6);
		rtw_write8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HRPWM1, val8);
		adapter_to_pwrctl(padapter)->tog = (val8 + 0x80) & 0x80;

		/* do polling cpwm */
		start_time = jiffies;
		do {

			mdelay(1);

			rtw_hal_get_hwreg(padapter, HW_VAR_CPWM, &cpwm_now);
			if ((cpwm_orig ^ cpwm_now) & 0x80)
				break;

			if (jiffies_to_msecs(jiffies - start_time) > 100)
				break;

		} while (1);

		rtl8723b_set_FwPwrModeInIPS_cmd(padapter, 0);

		rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);

		hal_btcoex_InitHwConfig(padapter, false);

		return _SUCCESS;
	}

	/*  Disable Interrupt first. */
/* 	rtw_hal_disable_interrupt(padapter); */

	ret = _InitPowerOn_8723BS(padapter);
	if (ret == _FAIL)
		return _FAIL;

	rtw_write8(padapter, REG_EARLY_MODE_CONTROL, 0);

	ret = rtl8723b_FirmwareDownload(padapter, false);
	if (ret != _SUCCESS) {
		padapter->bFWReady = false;
		pHalData->fw_ractrl = false;
		return ret;
	} else {
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

	ret = PHY_MACConfig8723B(padapter);
	if (ret != _SUCCESS)
		return ret;
	/*  */
	/* d. Initialize BB related configurations. */
	/*  */
	ret = PHY_BBConfig8723B(padapter);
	if (ret != _SUCCESS)
		return ret;

	/*  If RF is on, we need to init RF. Otherwise, skip the procedure. */
	/*  We need to follow SU method to change the RF cfg.txt. Default disable RF TX/RX mode. */
	/* if (pHalData->eRFPowerState == eRfOn) */
	{
		ret = PHY_RFConfig8723B(padapter);
		if (ret != _SUCCESS)
			return ret;
	}

	/*  */
	/*  Joseph Note: Keep RfRegChnlVal for later use. */
	/*  */
	pHalData->RfRegChnlVal[0] =
		PHY_QueryRFReg(padapter, (enum rf_path)0, RF_CHNLBW, bRFRegOffsetMask);
	pHalData->RfRegChnlVal[1] =
		PHY_QueryRFReg(padapter, (enum rf_path)1, RF_CHNLBW, bRFRegOffsetMask);


	/* if (!pHalData->bMACFuncEnable) { */
	_InitQueueReservedPage(padapter);
	_InitTxBufferBoundary(padapter);

	/*  init LLT after tx buffer boundary is defined */
	ret = rtl8723b_InitLLTTable(padapter);
	if (ret != _SUCCESS)
		return _FAIL;

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

	/*  Run LPS WL RFOFF flow */
	HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, rtl8723B_enter_lps_flow);

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
	rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_SDIO_MSK, rtl8723B_card_disable_flow);
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

				rtl8723b_set_FwPwrModeInIPS_cmd(padapter, 0x3);
				/* poll 0x1cc to make sure H2C command already finished by FW; MAC_0x1cc = 0 means H2C done by FW. */
				do {
					val8 = rtw_read8(padapter, REG_HMETFR);
					cnt++;
					mdelay(10);
				} while (cnt < 100 && (val8 != 0));
				/* H2C done, enter 32k */
				if (val8 == 0) {
					/* ser rpwm to enter 32k */
					val8 = rtw_read8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HRPWM1);
					val8 += 0x80;
					val8 |= BIT(0);
					rtw_write8(padapter, SDIO_LOCAL_BASE | SDIO_REG_HRPWM1, val8);
					adapter_to_pwrctl(padapter)->tog = (val8 + 0x80) & 0x80;
					cnt = val8 = 0;
					do {
						val8 = rtw_read8(padapter, REG_CR);
						cnt++;
						mdelay(10);
					} while (cnt < 100 && (val8 != 0xEA));
				}

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

	pHalData->rf_chip = RF_6052;
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
}

static void _ReadEfuseInfo8723BS(struct adapter *padapter)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	u8 *hwinfo = NULL;

	/*  */
	/*  This part read and parse the eeprom/efuse content */
	/*  */

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

	Hal_ReadRFGainOffset(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
}

static void _ReadPROMContent(struct adapter *padapter)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	u8 	eeValue;

	eeValue = rtw_read8(padapter, REG_9346CR);
	/*  To check system boot selection. */
	pEEPROM->EepromOrEfuse = (eeValue & BOOT_FROM_EEPROM) ? true : false;
	pEEPROM->bautoload_fail_flag = (eeValue & EEPROM_EN) ? false : true;

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

	/*  before access eFuse, make sure card enable has been called */
	if (!padapter->hw_init_completed)
		_InitPowerOn_8723BS(padapter);


	val8 = rtw_read8(padapter, 0x4e);
	val8 |= BIT(6);
	rtw_write8(padapter, 0x4e, val8);

	_EfuseCellSel(padapter);
	_ReadRFType(padapter);
	_ReadPROMContent(padapter);
	_InitOtherVariable(padapter);

	if (!padapter->hw_init_completed) {
		rtw_write8(padapter, 0x67, 0x00); /*  for BT, Switch Ant control to BT */
		CardDisableRTL8723BSdio(padapter);/* for the power consumption issue,  wifi ko module is loaded during booting, but wifi GUI is off */
	}

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
	struct adapter *Adapter, enum hal_def_variable eVariable, void *pValue
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
				enum hal_def_variable eVariable, void *pValue)
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
	pHalFunc->SetHwRegHandler = &SetHwReg8723BS;
	pHalFunc->GetHwRegHandler = &GetHwReg8723BS;
	pHalFunc->SetHwRegHandlerWithBuf = &SetHwRegWithBuf8723B;
	pHalFunc->GetHalDefVarHandler = &GetHalDefVar8723BSDIO;
	pHalFunc->SetHalDefVarHandler = &SetHalDefVar8723BSDIO;

	pHalFunc->hal_xmit = &rtl8723bs_hal_xmit;
	pHalFunc->mgnt_xmit = &rtl8723bs_mgnt_xmit;
	pHalFunc->hal_xmitframe_enqueue = &rtl8723bs_hal_xmitframe_enqueue;
}
