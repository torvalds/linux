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
 ******************************************************************************/
#define _HCI_HAL_INIT_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_efuse.h>

#include <HalPwrSeqCmd.h>
#include <Hal8723PwrSeq.h>
#include <rtl8723a_hal.h>
#include <linux/ieee80211.h>

#include <usb_ops.h>

static void phy_SsPwrSwitch92CU(struct rtw_adapter *Adapter,
				enum rt_rf_power_state eRFPowerState);

static void
_ConfigChipOutEP(struct rtw_adapter *pAdapter, u8 NumOutPipe)
{
	u8 value8;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(pAdapter);

	pHalData->OutEpQueueSel = 0;
	pHalData->OutEpNumber = 0;

	/*  Normal and High queue */
	value8 = rtl8723au_read8(pAdapter, (REG_NORMAL_SIE_EP + 1));

	if (value8 & USB_NORMAL_SIE_EP_MASK) {
		pHalData->OutEpQueueSel |= TX_SELE_HQ;
		pHalData->OutEpNumber++;
	}

	if ((value8 >> USB_NORMAL_SIE_EP_SHIFT) & USB_NORMAL_SIE_EP_MASK) {
		pHalData->OutEpQueueSel |= TX_SELE_NQ;
		pHalData->OutEpNumber++;
	}

	/*  Low queue */
	value8 = rtl8723au_read8(pAdapter, (REG_NORMAL_SIE_EP + 2));
	if (value8 & USB_NORMAL_SIE_EP_MASK) {
		pHalData->OutEpQueueSel |= TX_SELE_LQ;
		pHalData->OutEpNumber++;
	}

	/*  TODO: Error recovery for this case */
	/* RT_ASSERT((NumOutPipe == pHalData->OutEpNumber),
	   ("Out EP number isn't match! %d(Descriptor) != %d (SIE reg)\n",
	   (u32)NumOutPipe, (u32)pHalData->OutEpNumber)); */
}

bool rtl8723au_chip_configure(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);
	u8 NumInPipe = pdvobjpriv->RtNumInPipes;
	u8 NumOutPipe = pdvobjpriv->RtNumOutPipes;

	_ConfigChipOutEP(padapter, NumOutPipe);

	/*  Normal chip with one IN and one OUT doesn't have interrupt IN EP. */
	if (pHalData->OutEpNumber == 1) {
		if (NumInPipe != 1)
			return false;
	}

	return Hal_MappingOutPipe23a(padapter, NumOutPipe);
}

static int _InitPowerOn(struct rtw_adapter *padapter)
{
	u16 value16;
	u8 value8;

	/*  RSV_CTRL 0x1C[7:0] = 0x00
	    unlock ISO/CLK/Power control register */
	rtl8723au_write8(padapter, REG_RSV_CTRL, 0x0);

	/*  HW Power on sequence */
	if (!HalPwrSeqCmdParsing23a(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
				 PWR_INTF_USB_MSK, rtl8723AU_card_enable_flow))
		return _FAIL;

	/*  0x04[19] = 1, suggest by Jackie 2011.05.09, reset 8051 */
	value8 = rtl8723au_read8(padapter, REG_APS_FSMCO+2);
	rtl8723au_write8(padapter, REG_APS_FSMCO + 2, value8 | BIT(3));

	/*  Enable MAC DMA/WMAC/SCHEDULE/SEC block */
	/*  Set CR bit10 to enable 32k calibration. Suggested by SD1 Gimmy.
	    Added by tynli. 2011.08.31. */
	value16 = rtl8723au_read16(padapter, REG_CR);
	value16 |= (HCI_TXDMA_EN | HCI_RXDMA_EN | TXDMA_EN | RXDMA_EN |
		    PROTOCOL_EN | SCHEDULE_EN | MACTXEN | MACRXEN |
		    ENSEC | CALTMR_EN);
	rtl8723au_write16(padapter, REG_CR, value16);

	/* for Efuse PG, suggest by Jackie 2011.11.23 */
	PHY_SetBBReg(padapter, REG_EFUSE_CTRL, BIT(28)|BIT(29)|BIT(30), 0x06);

	return _SUCCESS;
}

/*  Shall USB interface init this? */
static void _InitInterrupt(struct rtw_adapter *Adapter)
{
	u32 value32;

	/*  HISR - turn all on */
	value32 = 0xFFFFFFFF;
	rtl8723au_write32(Adapter, REG_HISR, value32);

	/*  HIMR - turn all on */
	rtl8723au_write32(Adapter, REG_HIMR, value32);
}

static void _InitQueueReservedPage(struct rtw_adapter *Adapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	u32 numHQ = 0;
	u32 numLQ = 0;
	u32 numNQ = 0;
	u32 numPubQ;
	u32 value32;
	u8 value8;
	bool bWiFiConfig = pregistrypriv->wifi_spec;

	/* RT_ASSERT((outEPNum>= 2), ("for WMM , number of out-ep "
	   "must more than or equal to 2!\n")); */

	numPubQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_PUBQ : NORMAL_PAGE_NUM_PUBQ;

	if (pHalData->OutEpQueueSel & TX_SELE_HQ) {
		numHQ = bWiFiConfig ?
			WMM_NORMAL_PAGE_NUM_HPQ : NORMAL_PAGE_NUM_HPQ;
	}

	if (pHalData->OutEpQueueSel & TX_SELE_LQ) {
		numLQ = bWiFiConfig ?
			WMM_NORMAL_PAGE_NUM_LPQ : NORMAL_PAGE_NUM_LPQ;
	}
	/*  NOTE: This step shall be proceed before
	    writting REG_RQPN. */
	if (pHalData->OutEpQueueSel & TX_SELE_NQ) {
		numNQ = bWiFiConfig ?
			WMM_NORMAL_PAGE_NUM_NPQ : NORMAL_PAGE_NUM_NPQ;
	}
	value8 = (u8)_NPQ(numNQ);
	rtl8723au_write8(Adapter, REG_RQPN_NPQ, value8);

	/*  TX DMA */
	value32 = _HPQ(numHQ) | _LPQ(numLQ) | _PUBQ(numPubQ) | LD_RQPN;
	rtl8723au_write32(Adapter, REG_RQPN, value32);
}

static void _InitTxBufferBoundary(struct rtw_adapter *Adapter)
{
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;

	u8 txpktbuf_bndy;

	if (!pregistrypriv->wifi_spec)
		txpktbuf_bndy = TX_PAGE_BOUNDARY;
	else /* for WMM */
		txpktbuf_bndy = WMM_NORMAL_TX_PAGE_BOUNDARY;

	rtl8723au_write8(Adapter, REG_TXPKTBUF_BCNQ_BDNY, txpktbuf_bndy);
	rtl8723au_write8(Adapter, REG_TXPKTBUF_MGQ_BDNY, txpktbuf_bndy);
	rtl8723au_write8(Adapter, REG_TXPKTBUF_WMAC_LBK_BF_HD, txpktbuf_bndy);
	rtl8723au_write8(Adapter, REG_TRXFF_BNDY, txpktbuf_bndy);
	rtl8723au_write8(Adapter, REG_TDECTRL+1, txpktbuf_bndy);
}

static void _InitPageBoundary(struct rtw_adapter *Adapter)
{
	/*  RX Page Boundary */
	/* srand(static_cast<unsigned int>(time(NULL))); */
	u16 rxff_bndy = 0x27FF;/* rand() % 1) ? 0x27FF : 0x23FF; */

	rtl8723au_write16(Adapter, (REG_TRXFF_BNDY + 2), rxff_bndy);

	/*  TODO: ?? shall we set tx boundary? */
}

static void
_InitNormalChipRegPriority(struct rtw_adapter *Adapter, u16 beQ, u16 bkQ,
			   u16 viQ, u16 voQ, u16 mgtQ, u16 hiQ)
{
	u16 value16 = rtl8723au_read16(Adapter, REG_TRXDMA_CTRL) & 0x7;

	value16 |= _TXDMA_BEQ_MAP(beQ) | _TXDMA_BKQ_MAP(bkQ) |
		_TXDMA_VIQ_MAP(viQ) | _TXDMA_VOQ_MAP(voQ) |
		_TXDMA_MGQ_MAP(mgtQ) | _TXDMA_HIQ_MAP(hiQ);

	rtl8723au_write16(Adapter, REG_TRXDMA_CTRL, value16);
}

static void _InitNormalChipOneOutEpPriority(struct rtw_adapter *Adapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
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

	_InitNormalChipRegPriority(Adapter, value, value, value,
				   value, value, value);
}

static void _InitNormalChipTwoOutEpPriority(struct rtw_adapter *Adapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
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
	} else {/* for WMM , CONFIG_OUT_EP_WIFI_MODE */
		beQ = valueLow;
		bkQ = valueHi;
		viQ = valueHi;
		voQ = valueLow;
		mgtQ = valueHi;
		hiQ = valueHi;
	}

	_InitNormalChipRegPriority(Adapter, beQ, bkQ, viQ, voQ, mgtQ, hiQ);
}

static void _InitNormalChipThreeOutEpPriority(struct rtw_adapter *Adapter)
{
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	u16 beQ, bkQ, viQ, voQ, mgtQ, hiQ;

	if (!pregistrypriv->wifi_spec) {/*  typical setting */
		beQ = QUEUE_LOW;
		bkQ = QUEUE_LOW;
		viQ = QUEUE_NORMAL;
		voQ = QUEUE_HIGH;
		mgtQ = QUEUE_HIGH;
		hiQ = QUEUE_HIGH;
	} else {/*  for WMM */
		beQ = QUEUE_LOW;
		bkQ = QUEUE_NORMAL;
		viQ = QUEUE_NORMAL;
		voQ = QUEUE_HIGH;
		mgtQ = QUEUE_HIGH;
		hiQ = QUEUE_HIGH;
	}
	_InitNormalChipRegPriority(Adapter, beQ, bkQ, viQ, voQ, mgtQ, hiQ);
}

static void _InitQueuePriority(struct rtw_adapter *Adapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);

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

static void _InitTransferPageSize(struct rtw_adapter *Adapter)
{
	/*  Tx page size is always 128. */

	u8 value8;
	value8 = _PSRX(PBP_128) | _PSTX(PBP_128);
	rtl8723au_write8(Adapter, REG_PBP, value8);
}

static void _InitDriverInfoSize(struct rtw_adapter *Adapter, u8 drvInfoSize)
{
	rtl8723au_write8(Adapter, REG_RX_DRVINFO_SZ, drvInfoSize);
}

static void _InitWMACSetting(struct rtw_adapter *Adapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);

	/*  don't turn on AAP, it will allow all packets to driver */
	pHalData->ReceiveConfig = RCR_APM | RCR_AM | RCR_AB | RCR_CBSSID_DATA |
				  RCR_CBSSID_BCN | RCR_APP_ICV | RCR_AMF |
				  RCR_HTC_LOC_CTRL | RCR_APP_MIC |
				  RCR_APP_PHYSTS;

	/*  some REG_RCR will be modified later by
	    phy_ConfigMACWithHeaderFile() */
	rtl8723au_write32(Adapter, REG_RCR, pHalData->ReceiveConfig);

	/*  Accept all multicast address */
	rtl8723au_write32(Adapter, REG_MAR, 0xFFFFFFFF);
	rtl8723au_write32(Adapter, REG_MAR + 4, 0xFFFFFFFF);

	/*  Accept all data frames */
	/* value16 = 0xFFFF; */
	/* rtl8723au_write16(Adapter, REG_RXFLTMAP2, value16); */

	/*  2010.09.08 hpfan */
	/*  Since ADF is removed from RCR, ps-poll will not be indicate
	    to driver, */
	/*  RxFilterMap should mask ps-poll to gurantee AP mode can
	    rx ps-poll. */
	/* value16 = 0x400; */
	/* rtl8723au_write16(Adapter, REG_RXFLTMAP1, value16); */

	/*  Accept all management frames */
	/* value16 = 0xFFFF; */
	/* rtl8723au_write16(Adapter, REG_RXFLTMAP0, value16); */

	/* enable RX_SHIFT bits */
	/* rtl8723au_write8(Adapter, REG_TRXDMA_CTRL, rtl8723au_read8(Adapter,
	   REG_TRXDMA_CTRL)|BIT(1)); */
}

static void _InitAdaptiveCtrl(struct rtw_adapter *Adapter)
{
	u16 value16;
	u32 value32;

	/*  Response Rate Set */
	value32 = rtl8723au_read32(Adapter, REG_RRSR);
	value32 &= ~RATE_BITMAP_ALL;
	value32 |= RATE_RRSR_CCK_ONLY_1M;
	rtl8723au_write32(Adapter, REG_RRSR, value32);

	/*  CF-END Threshold */
	/* m_spIoBase->rtl8723au_write8(REG_CFEND_TH, 0x1); */

	/*  SIFS (used in NAV) */
	value16 = _SPEC_SIFS_CCK(0x10) | _SPEC_SIFS_OFDM(0x10);
	rtl8723au_write16(Adapter, REG_SPEC_SIFS, value16);

	/*  Retry Limit */
	value16 = _LRL(0x30) | _SRL(0x30);
	rtl8723au_write16(Adapter, REG_RL, value16);
}

static void _InitRateFallback(struct rtw_adapter *Adapter)
{
	/*  Set Data Auto Rate Fallback Retry Count register. */
	rtl8723au_write32(Adapter, REG_DARFRC, 0x00000000);
	rtl8723au_write32(Adapter, REG_DARFRC+4, 0x10080404);
	rtl8723au_write32(Adapter, REG_RARFRC, 0x04030201);
	rtl8723au_write32(Adapter, REG_RARFRC+4, 0x08070605);
}

static void _InitEDCA(struct rtw_adapter *Adapter)
{
	/*  Set Spec SIFS (used in NAV) */
	rtl8723au_write16(Adapter, REG_SPEC_SIFS, 0x100a);
	rtl8723au_write16(Adapter, REG_MAC_SPEC_SIFS, 0x100a);

	/*  Set SIFS for CCK */
	rtl8723au_write16(Adapter, REG_SIFS_CTX, 0x100a);

	/*  Set SIFS for OFDM */
	rtl8723au_write16(Adapter, REG_SIFS_TRX, 0x100a);

	/*  TXOP */
	rtl8723au_write32(Adapter, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtl8723au_write32(Adapter, REG_EDCA_BK_PARAM, 0x0000A44F);
	rtl8723au_write32(Adapter, REG_EDCA_VI_PARAM, 0x005EA324);
	rtl8723au_write32(Adapter, REG_EDCA_VO_PARAM, 0x002FA226);
}

static void _InitRDGSetting(struct rtw_adapter *Adapter)
{
	rtl8723au_write8(Adapter, REG_RD_CTRL, 0xFF);
	rtl8723au_write16(Adapter, REG_RD_NAV_NXT, 0x200);
	rtl8723au_write8(Adapter, REG_RD_RESP_PKT_TH, 0x05);
}

static void _InitRetryFunction(struct rtw_adapter *Adapter)
{
	u8 value8;

	value8 = rtl8723au_read8(Adapter, REG_FWHW_TXQ_CTRL);
	value8 |= EN_AMPDU_RTY_NEW;
	rtl8723au_write8(Adapter, REG_FWHW_TXQ_CTRL, value8);

	/*  Set ACK timeout */
	rtl8723au_write8(Adapter, REG_ACKTO, 0x40);
}

static void _InitRFType(struct rtw_adapter *Adapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	bool is92CU = IS_92C_SERIAL(pHalData->VersionID);

	pHalData->rf_chip = RF_6052;

	if (!is92CU) {
		pHalData->rf_type = RF_1T1R;
		DBG_8723A("Set RF Chip ID to RF_6052 and RF type to 1T1R.\n");
		return;
	}

	/*  TODO: Consider that EEPROM set 92CU to 1T1R later. */
	/*  Force to overwrite setting according to chip version. Ignore
	    EEPROM setting. */
	/* pHalData->RF_Type = is92CU ? RF_2T2R : RF_1T1R; */
	MSG_8723A("Set RF Chip ID to RF_6052 and RF type to %d.\n",
		  pHalData->rf_type);
}

/*  Set CCK and OFDM Block "ON" */
static void _BBTurnOnBlock(struct rtw_adapter *Adapter)
{
	PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bCCKEn, 0x1);
	PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bOFDMEn, 0x1);
}

#define MgntActSet_RF_State(...)
static void _RfPowerSave(struct rtw_adapter *padapter)
{
}

enum {
	Antenna_Lfet = 1,
	Antenna_Right = 2,
};

enum rt_rf_power_state RfOnOffDetect23a(struct rtw_adapter *pAdapter)
{
	/* struct hal_data_8723a *pHalData = GET_HAL_DATA(pAdapter); */
	u8 val8;
	enum rt_rf_power_state rfpowerstate = rf_off;

	rtl8723au_write8(pAdapter, REG_MAC_PINMUX_CFG,
			 rtl8723au_read8(pAdapter,
					 REG_MAC_PINMUX_CFG) & ~BIT(3));
	val8 = rtl8723au_read8(pAdapter, REG_GPIO_IO_SEL);
	DBG_8723A("GPIO_IN =%02x\n", val8);
	rfpowerstate = (val8 & BIT(3)) ? rf_on : rf_off;

	return rfpowerstate;
}

int rtl8723au_hal_init(struct rtw_adapter *Adapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	struct pwrctrl_priv *pwrctrlpriv = &Adapter->pwrctrlpriv;
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	u8 val8 = 0;
	u32 boundary;
	int status = _SUCCESS;
	bool mac_on;

	unsigned long init_start_time = jiffies;

	Adapter->hw_init_completed = false;

	if (Adapter->pwrctrlpriv.bkeepfwalive) {
		phy_SsPwrSwitch92CU(Adapter, rf_on);

		if (pHalData->bIQKInitialized) {
			rtl8723a_phy_iq_calibrate(Adapter, true);
		} else {
			rtl8723a_phy_iq_calibrate(Adapter, false);
			pHalData->bIQKInitialized = true;
		}
		rtl8723a_odm_check_tx_power_tracking(Adapter);
		rtl8723a_phy_lc_calibrate(Adapter);

		goto exit;
	}

	/*  Check if MAC has already power on. by tynli. 2011.05.27. */
	val8 = rtl8723au_read8(Adapter, REG_CR);
	RT_TRACE(_module_hci_hal_init_c_, _drv_info_,
		 ("%s: REG_CR 0x100 = 0x%02x\n", __func__, val8));
	/* Fix 92DU-VC S3 hang with the reason is that secondary mac is not
	   initialized. */
	/* 0x100 value of first mac is 0xEA while 0x100 value of secondary
	   is 0x00 */
	if (val8 == 0xEA) {
		mac_on = false;
	} else {
		mac_on = true;
		RT_TRACE(_module_hci_hal_init_c_, _drv_info_,
			 ("%s: MAC has already power on\n", __func__));
	}

	status = _InitPowerOn(Adapter);
	if (status == _FAIL) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_,
			 ("Failed to init power on!\n"));
		goto exit;
	}

	if (!pregistrypriv->wifi_spec) {
		boundary = TX_PAGE_BOUNDARY;
	} else {
		/*  for WMM */
		boundary = WMM_NORMAL_TX_PAGE_BOUNDARY;
	}

	if (!mac_on) {
		status =  InitLLTTable23a(Adapter, boundary);
		if (status == _FAIL) {
			RT_TRACE(_module_hci_hal_init_c_, _drv_err_,
				 ("Failed to init LLT table\n"));
			goto exit;
		}
	}

	if (pHalData->bRDGEnable)
		_InitRDGSetting(Adapter);

	status = rtl8723a_FirmwareDownload(Adapter);
	if (status != _SUCCESS) {
		Adapter->bFWReady = false;
		pHalData->fw_ractrl = false;
		DBG_8723A("fw download fail!\n");
		goto exit;
	} else {
		Adapter->bFWReady = true;
		pHalData->fw_ractrl = true;
		DBG_8723A("fw download ok!\n");
	}

	rtl8723a_InitializeFirmwareVars(Adapter);

	if (pwrctrlpriv->reg_rfoff == true) {
		pwrctrlpriv->rf_pwrstate = rf_off;
	}

	/*  2010/08/09 MH We need to check if we need to turnon or off RF after detecting */
	/*  HW GPIO pin. Before PHY_RFConfig8192C. */
	/* HalDetectPwrDownMode(Adapter); */
	/*  2010/08/26 MH If Efuse does not support sective suspend then disable the function. */
	/* HalDetectSelectiveSuspendMode(Adapter); */

	/*  Set RF type for BB/RF configuration */
	_InitRFType(Adapter);/* _ReadRFType() */

	/*  Save target channel */
	/*  <Roger_Notes> Current Channel will be updated again later. */
	pHalData->CurrentChannel = 6;/* default set to 6 */

	status = PHY_MACConfig8723A(Adapter);
	if (status == _FAIL) {
		DBG_8723A("PHY_MACConfig8723A fault !!\n");
		goto exit;
	}

	/*  */
	/* d. Initialize BB related configurations. */
	/*  */
	status = PHY_BBConfig8723A(Adapter);
	if (status == _FAIL) {
		DBG_8723A("PHY_BBConfig8723A fault !!\n");
		goto exit;
	}

	/*  Add for tx power by rate fine tune. We need to call the function after BB config. */
	/*  Because the tx power by rate table is inited in BB config. */

	status = PHY_RF6052_Config8723A(Adapter);
	if (status == _FAIL) {
		DBG_8723A("PHY_RF6052_Config8723A failed!!\n");
		goto exit;
	}

	/* reducing 80M spur */
	PHY_SetBBReg(Adapter, REG_AFE_XTAL_CTRL, bMaskDWord, 0x0381808d);
	PHY_SetBBReg(Adapter, REG_AFE_PLL_CTRL, bMaskDWord, 0xf0ffff83);
	PHY_SetBBReg(Adapter, REG_AFE_PLL_CTRL, bMaskDWord, 0xf0ffff82);
	PHY_SetBBReg(Adapter, REG_AFE_PLL_CTRL, bMaskDWord, 0xf0ffff83);

	/* RFSW Control */
	PHY_SetBBReg(Adapter, rFPGA0_TxInfo, bMaskDWord, 0x00000003);	/* 0x804[14]= 0 */
	PHY_SetBBReg(Adapter, rFPGA0_XAB_RFInterfaceSW, bMaskDWord, 0x07000760);	/* 0x870[6:5]= b'11 */
	PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, bMaskDWord, 0x66F60210); /* 0x860[6:5]= b'00 */

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("%s: 0x870 = value 0x%x\n", __func__, PHY_QueryBBReg(Adapter, 0x870, bMaskDWord)));

	/*  */
	/*  Joseph Note: Keep RfRegChnlVal for later use. */
	/*  */
	pHalData->RfRegChnlVal[0] = PHY_QueryRFReg(Adapter, RF_PATH_A,
						   RF_CHNLBW, bRFRegOffsetMask);
	pHalData->RfRegChnlVal[1] = PHY_QueryRFReg(Adapter, RF_PATH_B,
						   RF_CHNLBW, bRFRegOffsetMask);

	if (!mac_on) {
		_InitQueueReservedPage(Adapter);
		_InitTxBufferBoundary(Adapter);
	}
	_InitQueuePriority(Adapter);
	_InitPageBoundary(Adapter);
	_InitTransferPageSize(Adapter);

	/*  Get Rx PHY status in order to report RSSI and others. */
	_InitDriverInfoSize(Adapter, DRVINFO_SZ);

	_InitInterrupt(Adapter);
	hw_var_set_macaddr(Adapter, Adapter->eeprompriv.mac_addr);
	rtl8723a_set_media_status(Adapter, MSR_INFRA);
	_InitWMACSetting(Adapter);
	_InitAdaptiveCtrl(Adapter);
	_InitEDCA(Adapter);
	_InitRateFallback(Adapter);
	_InitRetryFunction(Adapter);
	rtl8723a_InitBeaconParameters(Adapter);

	_BBTurnOnBlock(Adapter);
	/* NicIFSetMacAddress(padapter, padapter->PermanentAddress); */

	rtl8723a_cam_invalidate_all(Adapter);

	/*  2010/12/17 MH We need to set TX power according to EFUSE content at first. */
	PHY_SetTxPowerLevel8723A(Adapter, pHalData->CurrentChannel);

	rtl8723a_InitAntenna_Selection(Adapter);

	/*  HW SEQ CTRL */
	/* set 0x0 to 0xFF by tynli. Default enable HW SEQ NUM. */
	rtl8723au_write8(Adapter, REG_HWSEQ_CTRL, 0xFF);

	/*  */
	/*  Disable BAR, suggested by Scott */
	/*  2010.04.09 add by hpfan */
	/*  */
	rtl8723au_write32(Adapter, REG_BAR_MODE_CTRL, 0x0201ffff);

	if (pregistrypriv->wifi_spec)
		rtl8723au_write16(Adapter, REG_FAST_EDCA_CTRL, 0);

	/*  Move by Neo for USB SS from above setp */
	_RfPowerSave(Adapter);

	/*  2010/08/26 MH Merge from 8192CE. */
	/* sherry masked that it has been done in _RfPowerSave */
	/* 20110927 */
	/* recovery for 8192cu and 9723Au 20111017 */
	if (pwrctrlpriv->rf_pwrstate == rf_on) {
		if (pHalData->bIQKInitialized) {
			rtl8723a_phy_iq_calibrate(Adapter, true);
		} else {
			rtl8723a_phy_iq_calibrate(Adapter, false);
			pHalData->bIQKInitialized = true;
		}

		rtl8723a_odm_check_tx_power_tracking(Adapter);

		rtl8723a_phy_lc_calibrate(Adapter);

		rtl8723a_dual_antenna_detection(Adapter);
	}

	/* fixed USB interface interference issue */
	rtl8723au_write8(Adapter, 0xfe40, 0xe0);
	rtl8723au_write8(Adapter, 0xfe41, 0x8d);
	rtl8723au_write8(Adapter, 0xfe42, 0x80);
	rtl8723au_write32(Adapter, 0x20c, 0xfd0320);
	/* Solve too many protocol error on USB bus */
	if (!IS_81xxC_VENDOR_UMC_A_CUT(pHalData->VersionID)) {
		/*  0xE6 = 0x94 */
		rtl8723au_write8(Adapter, 0xFE40, 0xE6);
		rtl8723au_write8(Adapter, 0xFE41, 0x94);
		rtl8723au_write8(Adapter, 0xFE42, 0x80);

		/*  0xE0 = 0x19 */
		rtl8723au_write8(Adapter, 0xFE40, 0xE0);
		rtl8723au_write8(Adapter, 0xFE41, 0x19);
		rtl8723au_write8(Adapter, 0xFE42, 0x80);

		/*  0xE5 = 0x91 */
		rtl8723au_write8(Adapter, 0xFE40, 0xE5);
		rtl8723au_write8(Adapter, 0xFE41, 0x91);
		rtl8723au_write8(Adapter, 0xFE42, 0x80);

		/*  0xE2 = 0x81 */
		rtl8723au_write8(Adapter, 0xFE40, 0xE2);
		rtl8723au_write8(Adapter, 0xFE41, 0x81);
		rtl8723au_write8(Adapter, 0xFE42, 0x80);

	}

/*	_InitPABias(Adapter); */

	/*  Init BT hw config. */
	rtl8723a_BT_init_hwconfig(Adapter);

	rtl8723a_InitHalDm(Adapter);

	val8 = ((WiFiNavUpperUs + HAL_8723A_NAV_UPPER_UNIT - 1) /
		HAL_8723A_NAV_UPPER_UNIT);
	rtl8723au_write8(Adapter, REG_NAV_UPPER, val8);

	/*  2011/03/09 MH debug only, UMC-B cut pass 2500 S5 test, but we need to fin root cause. */
	if (((rtl8723au_read32(Adapter, rFPGA0_RFMOD) & 0xFF000000) !=
	     0x83000000)) {
		PHY_SetBBReg(Adapter, rFPGA0_RFMOD, BIT(24), 1);
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("%s: IQK fail recorver\n", __func__));
	}

	/* ack for xmit mgmt frames. */
	rtl8723au_write32(Adapter, REG_FWHW_TXQ_CTRL,
			  rtl8723au_read32(Adapter, REG_FWHW_TXQ_CTRL)|BIT(12));

exit:
	if (status == _SUCCESS) {
		Adapter->hw_init_completed = true;

		if (Adapter->registrypriv.notch_filter == 1)
			rtl8723a_notch_filter(Adapter, 1);
	}

	DBG_8723A("%s in %dms\n", __func__,
		  jiffies_to_msecs(jiffies - init_start_time));
	return status;
}

static void phy_SsPwrSwitch92CU(struct rtw_adapter *Adapter,
				enum rt_rf_power_state eRFPowerState)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	u8 sps0;

	sps0 = rtl8723au_read8(Adapter, REG_SPS0_CTRL);

	switch (eRFPowerState) {
	case rf_on:
		/*  1. Enable MAC Clock. Can not be enabled now. */
		/* WriteXBYTE(REG_SYS_CLKR+1,
		   ReadXBYTE(REG_SYS_CLKR+1) | BIT(3)); */

		/*  2. Force PWM, Enable SPS18_LDO_Marco_Block */
		rtl8723au_write8(Adapter, REG_SPS0_CTRL,
				 sps0 | BIT(0) | BIT(3));

		/*  3. restore BB, AFE control register. */
		/* RF */
		if (pHalData->rf_type ==  RF_2T2R)
			PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter,
				     0x380038, 1);
		else
			PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter,
				     0x38, 1);
		PHY_SetBBReg(Adapter, rOFDM0_TRxPathEnable, 0xf0, 1);
		PHY_SetBBReg(Adapter, rFPGA0_RFMOD, BIT(1), 0);

		/* AFE */
		if (pHalData->rf_type ==  RF_2T2R)
			PHY_SetBBReg(Adapter, rRx_Wait_CCA, bMaskDWord,
				     0x63DB25A0);
		else if (pHalData->rf_type ==  RF_1T1R)
			PHY_SetBBReg(Adapter, rRx_Wait_CCA, bMaskDWord,
				     0x631B25A0);

		/*  4. issue 3-wire command that RF set to Rx idle
		    mode. This is used to re-write the RX idle mode. */
		/*  We can only prvide a usual value instead and then
		    HW will modify the value by itself. */
		PHY_SetRFReg(Adapter, RF_PATH_A, 0, bRFRegOffsetMask, 0x32D95);
		if (pHalData->rf_type ==  RF_2T2R) {
			PHY_SetRFReg(Adapter, RF_PATH_B, 0,
				     bRFRegOffsetMask, 0x32D95);
		}
		break;
	case rf_sleep:
	case rf_off:
		if (IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID))
			sps0 &= ~BIT(0);
		else
			sps0 &= ~(BIT(0) | BIT(3));

		RT_TRACE(_module_hal_init_c_, _drv_err_, ("SS LVL1\n"));
		/*  Disable RF and BB only for SelectSuspend. */

		/*  1. Set BB/RF to shutdown. */
		/*	(1) Reg878[5:3]= 0	RF rx_code for
						preamble power saving */
		/*	(2)Reg878[21:19]= 0	Turn off RF-B */
		/*	(3) RegC04[7:4]= 0	Turn off all paths
						for packet detection */
		/*	(4) Reg800[1] = 1	enable preamble power saving */
		Adapter->pwrctrlpriv.PS_BBRegBackup[PSBBREG_RF0] =
			PHY_QueryBBReg(Adapter, rFPGA0_XAB_RFParameter,
				       bMaskDWord);
		Adapter->pwrctrlpriv.PS_BBRegBackup[PSBBREG_RF1] =
			PHY_QueryBBReg(Adapter, rOFDM0_TRxPathEnable,
				       bMaskDWord);
		Adapter->pwrctrlpriv.PS_BBRegBackup[PSBBREG_RF2] =
			PHY_QueryBBReg(Adapter, rFPGA0_RFMOD, bMaskDWord);
		if (pHalData->rf_type ==  RF_2T2R) {
			PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter,
				     0x380038, 0);
		} else if (pHalData->rf_type ==  RF_1T1R) {
			PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, 0x38, 0);
		}
		PHY_SetBBReg(Adapter, rOFDM0_TRxPathEnable, 0xf0, 0);
		PHY_SetBBReg(Adapter, rFPGA0_RFMOD, BIT(1), 1);

		/*  2 .AFE control register to power down. bit[30:22] */
		Adapter->pwrctrlpriv.PS_BBRegBackup[PSBBREG_AFE0] =
			PHY_QueryBBReg(Adapter, rRx_Wait_CCA, bMaskDWord);
		if (pHalData->rf_type ==  RF_2T2R)
			PHY_SetBBReg(Adapter, rRx_Wait_CCA, bMaskDWord,
				     0x00DB25A0);
		else if (pHalData->rf_type ==  RF_1T1R)
			PHY_SetBBReg(Adapter, rRx_Wait_CCA, bMaskDWord,
				     0x001B25A0);

		/*  3. issue 3-wire command that RF set to power down.*/
		PHY_SetRFReg(Adapter, RF_PATH_A, 0, bRFRegOffsetMask, 0);
		if (pHalData->rf_type ==  RF_2T2R)
			PHY_SetRFReg(Adapter, RF_PATH_B, 0,
				     bRFRegOffsetMask, 0);

		/*  4. Force PFM , disable SPS18_LDO_Marco_Block */
		rtl8723au_write8(Adapter, REG_SPS0_CTRL, sps0);
		break;
	default:
		break;
	}
}

static void CardDisableRTL8723U(struct rtw_adapter *Adapter)
{
	u8		u1bTmp;

	DBG_8723A("CardDisableRTL8723U\n");
	/*  USB-MF Card Disable Flow */
	/*  1. Run LPS WL RFOFF flow */
	HalPwrSeqCmdParsing23a(Adapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
			    PWR_INTF_USB_MSK, rtl8723AU_enter_lps_flow);

	/*  2. 0x1F[7:0] = 0		turn off RF */
	rtl8723au_write8(Adapter, REG_RF_CTRL, 0x00);

	/*	==== Reset digital sequence   ====== */
	if ((rtl8723au_read8(Adapter, REG_MCUFWDL) & BIT(7)) &&
	    Adapter->bFWReady) /* 8051 RAM code */
		rtl8723a_FirmwareSelfReset(Adapter);

	/*  Reset MCU. Suggested by Filen. 2011.01.26. by tynli. */
	u1bTmp = rtl8723au_read8(Adapter, REG_SYS_FUNC_EN+1);
	rtl8723au_write8(Adapter, REG_SYS_FUNC_EN+1, u1bTmp & ~BIT(2));

	/*  g.	MCUFWDL 0x80[1:0]= 0		reset MCU ready status */
	rtl8723au_write8(Adapter, REG_MCUFWDL, 0x00);

	/*	==== Reset digital sequence end ====== */
	/*  Card disable power action flow */
	HalPwrSeqCmdParsing23a(Adapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK,
			       PWR_INTF_USB_MSK,
			       rtl8723AU_card_disable_flow);

	/*  Reset MCU IO Wrapper, added by Roger, 2011.08.30. */
	u1bTmp = rtl8723au_read8(Adapter, REG_RSV_CTRL + 1);
	rtl8723au_write8(Adapter, REG_RSV_CTRL+1, u1bTmp & ~BIT(0));
	u1bTmp = rtl8723au_read8(Adapter, REG_RSV_CTRL + 1);
	rtl8723au_write8(Adapter, REG_RSV_CTRL+1, u1bTmp | BIT(0));

	/*  7. RSV_CTRL 0x1C[7:0] = 0x0E  lock ISO/CLK/Power control register */
	rtl8723au_write8(Adapter, REG_RSV_CTRL, 0x0e);
}

int rtl8723au_hal_deinit(struct rtw_adapter *padapter)
{
	DBG_8723A("==> %s\n", __func__);

#ifdef CONFIG_8723AU_BT_COEXIST
	BT_HaltProcess(padapter);
#endif
	/*  2011/02/18 To Fix RU LNA  power leakage problem. We need to
	    execute below below in Adapter init and halt sequence.
	    According to EEchou's opinion, we can enable the ability for all */
	/*  IC. Accord to johnny's opinion, only RU need the support. */
	CardDisableRTL8723U(padapter);

	padapter->hw_init_completed = false;

	return _SUCCESS;
}

int rtl8723au_inirp_init(struct rtw_adapter *Adapter)
{
	u8 i;
	struct recv_buf *precvbuf;
	int status;
	struct recv_priv *precvpriv = &Adapter->recvpriv;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);

	status = _SUCCESS;

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("===> usb_inirp_init\n"));

	/* issue Rx irp to receive data */
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;
	for (i = 0; i < NR_RECVBUFF; i++) {
		if (rtl8723au_read_port(Adapter, 0, precvbuf) == _FAIL) {
			RT_TRACE(_module_hci_hal_init_c_, _drv_err_,
				 ("usb_rx_init: usb_read_port error\n"));
			status = _FAIL;
			goto exit;
		}
		precvbuf++;
	}
	if (rtl8723au_read_interrupt(Adapter) == _FAIL) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_,
			 ("%s: usb_read_interrupt error\n", __func__));
		status = _FAIL;
	}
	pHalData->IntrMask[0] = rtl8723au_read32(Adapter, REG_USB_HIMR);
	MSG_8723A("pHalData->IntrMask = 0x%04x\n", pHalData->IntrMask[0]);
	pHalData->IntrMask[0] |= UHIMR_C2HCMD|UHIMR_CPWM;
	rtl8723au_write32(Adapter, REG_USB_HIMR, pHalData->IntrMask[0]);
exit:
	RT_TRACE(_module_hci_hal_init_c_, _drv_info_,
		 ("<=== usb_inirp_init\n"));
	return status;
}

int rtl8723au_inirp_deinit(struct rtw_adapter *Adapter)
{
	struct hal_data_8723a	*pHalData = GET_HAL_DATA(Adapter);

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_,
		 ("\n ===> usb_rx_deinit\n"));
	rtl8723au_read_port_cancel(Adapter);
	pHalData->IntrMask[0] = rtl8723au_read32(Adapter, REG_USB_HIMR);
	MSG_8723A("%s pHalData->IntrMask = 0x%04x\n", __func__,
		  pHalData->IntrMask[0]);
	pHalData->IntrMask[0] = 0x0;
	rtl8723au_write32(Adapter, REG_USB_HIMR, pHalData->IntrMask[0]);
	RT_TRACE(_module_hci_hal_init_c_, _drv_info_,
		 ("\n <=== usb_rx_deinit\n"));
	return _SUCCESS;
}

static void _ReadBoardType(struct rtw_adapter *Adapter, u8 *PROMContent,
			   bool AutoloadFail)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	u8 boardType = BOARD_USB_DONGLE;

	if (AutoloadFail) {
		if (IS_8723_SERIES(pHalData->VersionID))
			pHalData->rf_type = RF_1T1R;
		else
			pHalData->rf_type = RF_2T2R;
		pHalData->BoardType = boardType;
		return;
	}

	boardType = PROMContent[EEPROM_NORMAL_BoardType];
	boardType &= BOARD_TYPE_NORMAL_MASK;/* bit[7:5] */
	boardType >>= 5;

	pHalData->BoardType = boardType;
	MSG_8723A("_ReadBoardType(%x)\n", pHalData->BoardType);

	if (boardType == BOARD_USB_High_PA)
		pHalData->ExternalPA = 1;
}

static void Hal_EfuseParseMACAddr_8723AU(struct rtw_adapter *padapter,
					 u8 *hwinfo, bool AutoLoadFail)
{
	u16 i;
	u8 sMacAddr[ETH_ALEN] = {0x00, 0xE0, 0x4C, 0x87, 0x23, 0x00};
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);

	if (AutoLoadFail) {
		for (i = 0; i < 6; i++)
			pEEPROM->mac_addr[i] = sMacAddr[i];
	} else {
		/* Read Permanent MAC address */
		memcpy(pEEPROM->mac_addr, &hwinfo[EEPROM_MAC_ADDR_8723AU],
		       ETH_ALEN);
	}

	RT_TRACE(_module_hci_hal_init_c_, _drv_notice_,
		 ("Hal_EfuseParseMACAddr_8723AU: Permanent Address =%02x:%02x:"
		  "%02x:%02x:%02x:%02x\n",
		  pEEPROM->mac_addr[0], pEEPROM->mac_addr[1],
		  pEEPROM->mac_addr[2], pEEPROM->mac_addr[3],
		  pEEPROM->mac_addr[4], pEEPROM->mac_addr[5]));
}

static void readAdapterInfo(struct rtw_adapter *padapter)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	/* struct hal_data_8723a * pHalData = GET_HAL_DATA(padapter); */
	u8 hwinfo[HWSET_MAX_SIZE];

	Hal_InitPGData(padapter, hwinfo);
	Hal_EfuseParseIDCode(padapter, hwinfo);
	Hal_EfuseParseEEPROMVer(padapter, hwinfo,
				pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseMACAddr_8723AU(padapter, hwinfo,
				     pEEPROM->bautoload_fail_flag);
	Hal_EfuseParsetxpowerinfo_8723A(padapter, hwinfo,
					pEEPROM->bautoload_fail_flag);
	_ReadBoardType(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseBTCoexistInfo_8723A(padapter, hwinfo,
					  pEEPROM->bautoload_fail_flag);

	rtl8723a_EfuseParseChnlPlan(padapter, hwinfo,
				    pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseThermalMeter_8723A(padapter, hwinfo,
					 pEEPROM->bautoload_fail_flag);
/*	_ReadRFSetting(Adapter, PROMContent, pEEPROM->bautoload_fail_flag); */
/*	_ReadPSSetting(Adapter, PROMContent, pEEPROM->bautoload_fail_flag); */
	Hal_EfuseParseAntennaDiversity(padapter, hwinfo,
				       pEEPROM->bautoload_fail_flag);

	Hal_EfuseParseEEPROMVer(padapter, hwinfo, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseCustomerID(padapter, hwinfo,
				 pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseRateIndicationOption(padapter, hwinfo,
					   pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseXtal_8723A(padapter, hwinfo,
				 pEEPROM->bautoload_fail_flag);

	/* hal_CustomizedBehavior_8723U(Adapter); */

/*	Adapter->bDongle = (PROMContent[EEPROM_EASY_REPLACEMENT] == 1)? 0: 1; */
	DBG_8723A("%s(): REPLACEMENT = %x\n", __func__, padapter->bDongle);
}

static void _ReadPROMContent(struct rtw_adapter *Adapter)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
	u8 eeValue;

	eeValue = rtl8723au_read8(Adapter, REG_9346CR);
	/*  To check system boot selection. */
	pEEPROM->EepromOrEfuse = (eeValue & BOOT_FROM_EEPROM) ? true : false;
	pEEPROM->bautoload_fail_flag = (eeValue & EEPROM_EN) ? false : true;

	DBG_8723A("Boot from %s, Autoload %s !\n",
		  (pEEPROM->EepromOrEfuse ? "EEPROM" : "EFUSE"),
		  (pEEPROM->bautoload_fail_flag ? "Fail" : "OK"));

	readAdapterInfo(Adapter);
}

static void _ReadRFType(struct rtw_adapter *Adapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);

	pHalData->rf_chip = RF_6052;
}

/*  */
/*	Description: */
/*		We should set Efuse cell selection to WiFi cell in default. */
/*  */
/*	Assumption: */
/*		PASSIVE_LEVEL */
/*  */
/*	Added by Roger, 2010.11.23. */
/*  */
static void hal_EfuseCellSel(struct rtw_adapter *Adapter)
{
	u32 value32;

	value32 = rtl8723au_read32(Adapter, EFUSE_TEST);
	value32 = (value32 & ~EFUSE_SEL_MASK) | EFUSE_SEL(EFUSE_WIFI_SEL_0);
	rtl8723au_write32(Adapter, EFUSE_TEST, value32);
}

void rtl8723a_read_adapter_info(struct rtw_adapter *Adapter)
{
	unsigned long start = jiffies;

	/*  Read EEPROM size before call any EEPROM function */
	Adapter->EepromAddressSize = GetEEPROMSize8723A(Adapter);

	MSG_8723A("====> _ReadAdapterInfo8723AU\n");

	hal_EfuseCellSel(Adapter);

	_ReadRFType(Adapter);/* rf_chip -> _InitRFType() */
	_ReadPROMContent(Adapter);

	/* MSG_8723A("%s()(done), rf_chip = 0x%x, rf_type = 0x%x\n",
	   __func__, pHalData->rf_chip, pHalData->rf_type); */

	MSG_8723A("<==== _ReadAdapterInfo8723AU in %d ms\n",
		  jiffies_to_msecs(jiffies - start));
}

/*  */
/*	Description: */
/*		Query setting of specified variable. */
/*  */
int GetHalDefVar8192CUsb(struct rtw_adapter *Adapter,
			 enum hal_def_variable eVariable, void *pValue)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	int bResult = _SUCCESS;

	switch (eVariable) {
	case HAL_DEF_UNDERCORATEDSMOOTHEDPWDB:
		*((int *)pValue) = pHalData->dmpriv.UndecoratedSmoothedPWDB;
		break;
	case HAL_DEF_IS_SUPPORT_ANT_DIV:
		break;
	case HAL_DEF_CURRENT_ANTENNA:
		break;
	case HAL_DEF_DRVINFO_SZ:
		*((u32 *)pValue) = DRVINFO_SZ;
		break;
	case HAL_DEF_MAX_RECVBUF_SZ:
		*((u32 *)pValue) = MAX_RECVBUF_SZ;
		break;
	case HAL_DEF_RX_PACKET_OFFSET:
		*((u32 *)pValue) = RXDESC_SIZE + DRVINFO_SZ;
		break;
	case HAL_DEF_DBG_DUMP_RXPKT:
		*((u8 *)pValue) = pHalData->bDumpRxPkt;
		break;
	case HAL_DEF_DBG_DM_FUNC:
		*((u32 *)pValue) = pHalData->odmpriv.SupportAbility;
		break;
	case HW_VAR_MAX_RX_AMPDU_FACTOR:
		*((u32 *)pValue) = IEEE80211_HT_MAX_AMPDU_64K;
		break;
	case HW_DEF_ODM_DBG_FLAG:
	{
		struct dm_odm_t	*pDM_Odm = &pHalData->odmpriv;
		printk("pDM_Odm->DebugComponents = 0x%llx\n",
		       pDM_Odm->DebugComponents);
	}
		break;
	default:
		/* RT_TRACE(COMP_INIT, DBG_WARNING, ("GetHalDefVar8192CUsb(): "
		   "Unkown variable: %d!\n", eVariable)); */
		bResult = _FAIL;
		break;
	}

	return bResult;
}

void rtl8723a_update_ramask(struct rtw_adapter *padapter,
			    u32 mac_id, u8 rssi_level)
{
	struct sta_info	*psta;
	struct FW_Sta_Info *fw_sta;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct dm_priv *pdmpriv = &pHalData->dmpriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *cur_network = &pmlmeinfo->network;
	u8 init_rate, networkType, raid;
	u32 mask, rate_bitmap;
	u8 shortGIrate = false;
	int supportRateNum;

	if (mac_id >= NUM_STA) /* CAM_SIZE */
		return;

	psta = pmlmeinfo->FW_sta_info[mac_id].psta;
	if (psta == NULL)
		return;

	switch (mac_id) {
	case 0:/*  for infra mode */
		supportRateNum =
			rtw_get_rateset_len23a(cur_network->SupportedRates);
		networkType = judge_network_type23a(padapter,
						 cur_network->SupportedRates,
						 supportRateNum) & 0xf;
		/* pmlmeext->cur_wireless_mode = networkType; */
		raid = networktype_to_raid23a(networkType);

		mask = update_supported_rate23a(cur_network->SupportedRates,
					     supportRateNum);
		mask |= (pmlmeinfo->HT_enable) ?
			update_MSC_rate23a(&pmlmeinfo->ht_cap) : 0;

		if (support_short_GI23a(padapter, &pmlmeinfo->ht_cap))
			shortGIrate = true;
		break;

	case 1:/* for broadcast/multicast */
		fw_sta = &pmlmeinfo->FW_sta_info[mac_id]; 
		supportRateNum = rtw_get_rateset_len23a(fw_sta->SupportedRates);
		if (pmlmeext->cur_wireless_mode & WIRELESS_11B)
			networkType = WIRELESS_11B;
		else
			networkType = WIRELESS_11G;
		raid = networktype_to_raid23a(networkType);

		mask = update_basic_rate23a(cur_network->SupportedRates,
					 supportRateNum);
		break;

	default: /* for each sta in IBSS */
		fw_sta = &pmlmeinfo->FW_sta_info[mac_id]; 
		supportRateNum = rtw_get_rateset_len23a(fw_sta->SupportedRates);
		networkType = judge_network_type23a(padapter,
						    fw_sta->SupportedRates,
						    supportRateNum) & 0xf;
		/* pmlmeext->cur_wireless_mode = networkType; */
		raid = networktype_to_raid23a(networkType);

		mask = update_supported_rate23a(cur_network->SupportedRates,
						supportRateNum);

		/* todo: support HT in IBSS */
		break;
	}

	/* mask &= 0x0fffffff; */
	rate_bitmap = ODM_Get_Rate_Bitmap23a(pHalData, mac_id, mask,
					     rssi_level);
	DBG_8723A("%s => mac_id:%d, networkType:0x%02x, "
		  "mask:0x%08x\n\t ==> rssi_level:%d, rate_bitmap:0x%08x\n",
		  __func__, mac_id, networkType, mask, rssi_level, rate_bitmap);

	mask &= rate_bitmap;
	mask |= ((raid << 28) & 0xf0000000);

	init_rate = get_highest_rate_idx23a(mask) & 0x3f;

	if (pHalData->fw_ractrl == true) {
		u8 arg = 0;

		arg = mac_id & 0x1f;/* MACID */

		arg |= BIT(7);

		if (shortGIrate == true)
			arg |= BIT(5);

		DBG_8723A("update raid entry, mask = 0x%x, arg = 0x%x\n",
			  mask, arg);

		rtl8723a_set_raid_cmd(padapter, mask, arg);
	} else {
		if (shortGIrate == true)
			init_rate |= BIT(6);

		rtl8723au_write8(padapter, (REG_INIDATA_RATE_SEL + mac_id),
				 init_rate);
	}

	/* set ra_id */
	psta->raid = raid;
	psta->init_rate = init_rate;

	/* set correct initial date rate for each mac_id */
	pdmpriv->INIDATA_RATE[mac_id] = init_rate;
}
