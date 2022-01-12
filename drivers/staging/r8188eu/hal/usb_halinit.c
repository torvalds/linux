// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _HCI_HAL_INIT_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/rtw_efuse.h"

#include "../include/rtl8188e_hal.h"
#include "../include/rtw_iol.h"
#include "../include/usb_ops.h"
#include "../include/usb_osintf.h"
#include "../include/Hal8188EPwrSeq.h"

#define		HAL_MAC_ENABLE	1
#define		HAL_BB_ENABLE		1
#define		HAL_RF_ENABLE		1

static void _ConfigNormalChipOutEP_8188E(struct adapter *adapt, u8 NumOutPipe)
{
	struct hal_data_8188e *haldata = &adapt->haldata;

	switch (NumOutPipe) {
	case	3:
		haldata->OutEpQueueSel = TX_SELE_HQ | TX_SELE_LQ | TX_SELE_NQ;
		haldata->OutEpNumber = 3;
		break;
	case	2:
		haldata->OutEpQueueSel = TX_SELE_HQ | TX_SELE_NQ;
		haldata->OutEpNumber = 2;
		break;
	case	1:
		haldata->OutEpQueueSel = TX_SELE_HQ;
		haldata->OutEpNumber = 1;
		break;
	default:
		break;
	}
	DBG_88E("%s OutEpQueueSel(0x%02x), OutEpNumber(%d)\n", __func__, haldata->OutEpQueueSel, haldata->OutEpNumber);
}

static bool HalUsbSetQueuePipeMapping8188EUsb(struct adapter *adapt, u8 NumOutPipe)
{

	_ConfigNormalChipOutEP_8188E(adapt, NumOutPipe);
	return Hal_MappingOutPipe(adapt, NumOutPipe);
}

void rtl8188eu_interface_configure(struct adapter *adapt)
{
	struct hal_data_8188e *haldata = &adapt->haldata;
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(adapt);

	if (pdvobjpriv->ishighspeed)
		haldata->UsbBulkOutSize = USB_HIGH_SPEED_BULK_SIZE;/* 512 bytes */
	else
		haldata->UsbBulkOutSize = USB_FULL_SPEED_BULK_SIZE;/* 64 bytes */

	haldata->UsbTxAggMode		= 1;
	haldata->UsbTxAggDescNum	= 0x6;	/*  only 4 bits */

	haldata->UsbRxAggMode		= USB_RX_AGG_DMA;/*  USB_RX_AGG_DMA; */
	haldata->UsbRxAggBlockCount	= 8; /* unit : 512b */
	haldata->UsbRxAggBlockTimeout	= 0x6;
	haldata->UsbRxAggPageCount	= 48; /* uint :128 b 0x0A;	10 = MAX_RX_DMA_BUFFER_SIZE/2/haldata->UsbBulkOutSize */
	haldata->UsbRxAggPageTimeout	= 0x4; /* 6, absolute time = 34ms/(2^6) */

	HalUsbSetQueuePipeMapping8188EUsb(adapt, pdvobjpriv->RtNumOutPipes);
}

u32 rtl8188eu_InitPowerOn(struct adapter *adapt)
{
	u16 value16;
	/*  HW Power on sequence */
	struct hal_data_8188e *haldata = &adapt->haldata;
	if (haldata->bMacPwrCtrlOn)
		return _SUCCESS;

	if (!HalPwrSeqCmdParsing(adapt, Rtl8188E_NIC_PWR_ON_FLOW)) {
		DBG_88E(KERN_ERR "%s: run power on flow fail\n", __func__);
		return _FAIL;
	}

	/*  Enable MAC DMA/WMAC/SCHEDULE/SEC block */
	/*  Set CR bit10 to enable 32k calibration. Suggested by SD1 Gimmy. Added by tynli. 2011.08.31. */
	rtw_write16(adapt, REG_CR, 0x00);  /* suggseted by zhouzhou, by page, 20111230 */

		/*  Enable MAC DMA/WMAC/SCHEDULE/SEC block */
	value16 = rtw_read16(adapt, REG_CR);
	value16 |= (HCI_TXDMA_EN | HCI_RXDMA_EN | TXDMA_EN | RXDMA_EN
				| PROTOCOL_EN | SCHEDULE_EN | ENSEC | CALTMR_EN);
	/*  for SDIO - Set CR bit10 to enable 32k calibration. Suggested by SD1 Gimmy. Added by tynli. 2011.08.31. */

	rtw_write16(adapt, REG_CR, value16);
	haldata->bMacPwrCtrlOn = true;

	return _SUCCESS;
}

/*  Shall USB interface init this? */
static void _InitInterrupt(struct adapter *Adapter)
{
	u32 imr, imr_ex;
	u8  usb_opt;

	/* HISR write one to clear */
	rtw_write32(Adapter, REG_HISR_88E, 0xFFFFFFFF);
	/*  HIMR - */
	imr = IMR_PSTIMEOUT_88E | IMR_TBDER_88E | IMR_CPWM_88E | IMR_CPWM2_88E;
	rtw_write32(Adapter, REG_HIMR_88E, imr);

	imr_ex = IMR_TXERR_88E | IMR_RXERR_88E | IMR_TXFOVW_88E | IMR_RXFOVW_88E;
	rtw_write32(Adapter, REG_HIMRE_88E, imr_ex);

	/*  REG_USB_SPECIAL_OPTION - BIT(4) */
	/*  0; Use interrupt endpoint to upload interrupt pkt */
	/*  1; Use bulk endpoint to upload interrupt pkt, */
	usb_opt = rtw_read8(Adapter, REG_USB_SPECIAL_OPTION);

	if (!adapter_to_dvobj(Adapter)->ishighspeed)
		usb_opt = usb_opt & (~INT_BULK_SEL);
	else
		usb_opt = usb_opt | (INT_BULK_SEL);

	rtw_write8(Adapter, REG_USB_SPECIAL_OPTION, usb_opt);
}

static void _InitQueueReservedPage(struct adapter *Adapter)
{
	struct hal_data_8188e *haldata = &Adapter->haldata;
	struct registry_priv	*pregistrypriv = &Adapter->registrypriv;
	u32 numHQ	= 0;
	u32 numLQ	= 0;
	u32 numNQ	= 0;
	u32 numPubQ;
	u32 value32;
	u8 value8;
	bool bWiFiConfig = pregistrypriv->wifi_spec;

	if (bWiFiConfig) {
		if (haldata->OutEpQueueSel & TX_SELE_HQ)
			numHQ =  0x29;

		if (haldata->OutEpQueueSel & TX_SELE_LQ)
			numLQ = 0x1C;

		/*  NOTE: This step shall be proceed before writting REG_RQPN. */
		if (haldata->OutEpQueueSel & TX_SELE_NQ)
			numNQ = 0x1C;
		value8 = (u8)_NPQ(numNQ);
		rtw_write8(Adapter, REG_RQPN_NPQ, value8);

		numPubQ = 0xA8 - numHQ - numLQ - numNQ;

		/*  TX DMA */
		value32 = _HPQ(numHQ) | _LPQ(numLQ) | _PUBQ(numPubQ) | LD_RQPN;
		rtw_write32(Adapter, REG_RQPN, value32);
	} else {
		rtw_write16(Adapter, REG_RQPN_NPQ, 0x0000);/* Just follow MP Team,??? Georgia 03/28 */
		rtw_write16(Adapter, REG_RQPN_NPQ, 0x0d);
		rtw_write32(Adapter, REG_RQPN, 0x808E000d);/* reserve 7 page for LPS */
	}
}

static void _InitTxBufferBoundary(struct adapter *Adapter, u8 txpktbuf_bndy)
{
	rtw_write8(Adapter, REG_TXPKTBUF_BCNQ_BDNY, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TXPKTBUF_MGQ_BDNY, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TXPKTBUF_WMAC_LBK_BF_HD, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TRXFF_BNDY, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TDECTRL + 1, txpktbuf_bndy);
}

static void _InitPageBoundary(struct adapter *Adapter)
{
	/*  RX Page Boundary */
	/*  */
	u16 rxff_bndy = MAX_RX_DMA_BUFFER_SIZE_88E - 1;

	rtw_write16(Adapter, (REG_TRXFF_BNDY + 2), rxff_bndy);
}

static void _InitNormalChipRegPriority(struct adapter *Adapter, u16 beQ,
				       u16 bkQ, u16 viQ, u16 voQ, u16 mgtQ,
				       u16 hiQ)
{
	u16 value16	= (rtw_read16(Adapter, REG_TRXDMA_CTRL) & 0x7);

	value16 |= _TXDMA_BEQ_MAP(beQ)	| _TXDMA_BKQ_MAP(bkQ) |
		   _TXDMA_VIQ_MAP(viQ)	| _TXDMA_VOQ_MAP(voQ) |
		   _TXDMA_MGQ_MAP(mgtQ) | _TXDMA_HIQ_MAP(hiQ);

	rtw_write16(Adapter, REG_TRXDMA_CTRL, value16);
}

static void _InitNormalChipOneOutEpPriority(struct adapter *Adapter)
{
	struct hal_data_8188e *haldata = &Adapter->haldata;

	u16 value = 0;
	switch (haldata->OutEpQueueSel) {
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
	_InitNormalChipRegPriority(Adapter, value, value, value, value,
				   value, value);
}

static void _InitNormalChipTwoOutEpPriority(struct adapter *Adapter)
{
	struct hal_data_8188e *haldata = &Adapter->haldata;
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	u16 beQ, bkQ, viQ, voQ, mgtQ, hiQ;
	u16 valueHi = 0;
	u16 valueLow = 0;

	switch (haldata->OutEpQueueSel) {
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
		beQ	= valueLow;
		bkQ	= valueLow;
		viQ	= valueHi;
		voQ	= valueHi;
		mgtQ	= valueHi;
		hiQ	= valueHi;
	} else {/* for WMM ,CONFIG_OUT_EP_WIFI_MODE */
		beQ	= valueLow;
		bkQ	= valueHi;
		viQ	= valueHi;
		voQ	= valueLow;
		mgtQ	= valueHi;
		hiQ	= valueHi;
	}
	_InitNormalChipRegPriority(Adapter, beQ, bkQ, viQ, voQ, mgtQ, hiQ);
}

static void _InitNormalChipThreeOutEpPriority(struct adapter *Adapter)
{
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	u16 beQ, bkQ, viQ, voQ, mgtQ, hiQ;

	if (!pregistrypriv->wifi_spec) {/*  typical setting */
		beQ	= QUEUE_LOW;
		bkQ	= QUEUE_LOW;
		viQ	= QUEUE_NORMAL;
		voQ	= QUEUE_HIGH;
		mgtQ	= QUEUE_HIGH;
		hiQ	= QUEUE_HIGH;
	} else {/*  for WMM */
		beQ	= QUEUE_LOW;
		bkQ	= QUEUE_NORMAL;
		viQ	= QUEUE_NORMAL;
		voQ	= QUEUE_HIGH;
		mgtQ	= QUEUE_HIGH;
		hiQ	= QUEUE_HIGH;
	}
	_InitNormalChipRegPriority(Adapter, beQ, bkQ, viQ, voQ, mgtQ, hiQ);
}

static void _InitQueuePriority(struct adapter *Adapter)
{
	struct hal_data_8188e *haldata = &Adapter->haldata;

	switch (haldata->OutEpNumber) {
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

static void _InitNetworkType(struct adapter *Adapter)
{
	u32 value32;

	value32 = rtw_read32(Adapter, REG_CR);
	/*  TODO: use the other function to set network type */
	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AP);

	rtw_write32(Adapter, REG_CR, value32);
}

static void _InitTransferPageSize(struct adapter *Adapter)
{
	/*  Tx page size is always 128. */

	u8 value8;
	value8 = _PSRX(PBP_128) | _PSTX(PBP_128);
	rtw_write8(Adapter, REG_PBP, value8);
}

static void _InitDriverInfoSize(struct adapter *Adapter, u8 drvInfoSize)
{
	rtw_write8(Adapter, REG_RX_DRVINFO_SZ, drvInfoSize);
}

static void _InitWMACSetting(struct adapter *Adapter)
{
	struct hal_data_8188e *haldata = &Adapter->haldata;

	haldata->ReceiveConfig = RCR_AAP | RCR_APM | RCR_AM | RCR_AB |
				  RCR_CBSSID_DATA | RCR_CBSSID_BCN |
				  RCR_APP_ICV | RCR_AMF | RCR_HTC_LOC_CTRL |
				  RCR_APP_MIC | RCR_APP_PHYSTS;

	/*  some REG_RCR will be modified later by phy_ConfigMACWithHeaderFile() */
	rtw_write32(Adapter, REG_RCR, haldata->ReceiveConfig);

	/*  Accept all multicast address */
	rtw_write32(Adapter, REG_MAR, 0xFFFFFFFF);
	rtw_write32(Adapter, REG_MAR + 4, 0xFFFFFFFF);
}

static void _InitAdaptiveCtrl(struct adapter *Adapter)
{
	u16 value16;
	u32 value32;

	/*  Response Rate Set */
	value32 = rtw_read32(Adapter, REG_RRSR);
	value32 &= ~RATE_BITMAP_ALL;
	value32 |= RATE_RRSR_CCK_ONLY_1M;
	rtw_write32(Adapter, REG_RRSR, value32);

	/*  CF-END Threshold */

	/*  SIFS (used in NAV) */
	value16 = _SPEC_SIFS_CCK(0x10) | _SPEC_SIFS_OFDM(0x10);
	rtw_write16(Adapter, REG_SPEC_SIFS, value16);

	/*  Retry Limit */
	value16 = _LRL(0x30) | _SRL(0x30);
	rtw_write16(Adapter, REG_RL, value16);
}

static void _InitEDCA(struct adapter *Adapter)
{
	/*  Set Spec SIFS (used in NAV) */
	rtw_write16(Adapter, REG_SPEC_SIFS, 0x100a);
	rtw_write16(Adapter, REG_MAC_SPEC_SIFS, 0x100a);

	/*  Set SIFS for CCK */
	rtw_write16(Adapter, REG_SIFS_CTX, 0x100a);

	/*  Set SIFS for OFDM */
	rtw_write16(Adapter, REG_SIFS_TRX, 0x100a);

	/*  TXOP */
	rtw_write32(Adapter, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtw_write32(Adapter, REG_EDCA_BK_PARAM, 0x0000A44F);
	rtw_write32(Adapter, REG_EDCA_VI_PARAM, 0x005EA324);
	rtw_write32(Adapter, REG_EDCA_VO_PARAM, 0x002FA226);
}

static void _InitRetryFunction(struct adapter *Adapter)
{
	u8 value8;

	value8 = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL);
	value8 |= EN_AMPDU_RTY_NEW;
	rtw_write8(Adapter, REG_FWHW_TXQ_CTRL, value8);

	/*  Set ACK timeout */
	rtw_write8(Adapter, REG_ACKTO, 0x40);
}

/*-----------------------------------------------------------------------------
 * Function:	usb_AggSettingTxUpdate()
 *
 * Overview:	Separate TX/RX parameters update independent for TP detection and
 *			dynamic TX/RX aggreagtion parameters update.
 *
 * Input:			struct adapter *
 *
 * Output/Return:	NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	12/10/2010	MHC		Separate to smaller function.
 *
 *---------------------------------------------------------------------------*/
static void usb_AggSettingTxUpdate(struct adapter *Adapter)
{
	struct hal_data_8188e *haldata = &Adapter->haldata;
	u32 value32;

	if (Adapter->registrypriv.wifi_spec)
		haldata->UsbTxAggMode = false;

	if (haldata->UsbTxAggMode) {
		value32 = rtw_read32(Adapter, REG_TDECTRL);
		value32 = value32 & ~(BLK_DESC_NUM_MASK << BLK_DESC_NUM_SHIFT);
		value32 |= ((haldata->UsbTxAggDescNum & BLK_DESC_NUM_MASK) << BLK_DESC_NUM_SHIFT);

		rtw_write32(Adapter, REG_TDECTRL, value32);
	}
}	/*  usb_AggSettingTxUpdate */

/*-----------------------------------------------------------------------------
 * Function:	usb_AggSettingRxUpdate()
 *
 * Overview:	Separate TX/RX parameters update independent for TP detection and
 *			dynamic TX/RX aggreagtion parameters update.
 *
 * Input:			struct adapter *
 *
 * Output/Return:	NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	12/10/2010	MHC		Separate to smaller function.
 *
 *---------------------------------------------------------------------------*/
static void
usb_AggSettingRxUpdate(
		struct adapter *Adapter
	)
{
	struct hal_data_8188e *haldata = &Adapter->haldata;
	u8 valueDMA;
	u8 valueUSB;

	valueDMA = rtw_read8(Adapter, REG_TRXDMA_CTRL);
	valueUSB = rtw_read8(Adapter, REG_USB_SPECIAL_OPTION);

	switch (haldata->UsbRxAggMode) {
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

	switch (haldata->UsbRxAggMode) {
	case USB_RX_AGG_DMA:
		rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH, haldata->UsbRxAggPageCount);
		rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH + 1, haldata->UsbRxAggPageTimeout);
		break;
	case USB_RX_AGG_USB:
		rtw_write8(Adapter, REG_USB_AGG_TH, haldata->UsbRxAggBlockCount);
		rtw_write8(Adapter, REG_USB_AGG_TO, haldata->UsbRxAggBlockTimeout);
		break;
	case USB_RX_AGG_MIX:
		rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH, haldata->UsbRxAggPageCount);
		rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH + 1, (haldata->UsbRxAggPageTimeout & 0x1F));/* 0x280[12:8] */
		rtw_write8(Adapter, REG_USB_AGG_TH, haldata->UsbRxAggBlockCount);
		rtw_write8(Adapter, REG_USB_AGG_TO, haldata->UsbRxAggBlockTimeout);
		break;
	case USB_RX_AGG_DISABLE:
	default:
		/*  TODO: */
		break;
	}
}	/*  usb_AggSettingRxUpdate */

static void InitUsbAggregationSetting(struct adapter *Adapter)
{
	/*  Tx aggregation setting */
	usb_AggSettingTxUpdate(Adapter);

	/*  Rx aggregation setting */
	usb_AggSettingRxUpdate(Adapter);
}

static void _InitOperationMode(struct adapter *Adapter)
{
}

static void _InitBeaconParameters(struct adapter *Adapter)
{
	struct hal_data_8188e *haldata = &Adapter->haldata;

	rtw_write16(Adapter, REG_BCN_CTRL, 0x1010);

	/*  TODO: Remove these magic number */
	rtw_write16(Adapter, REG_TBTT_PROHIBIT, 0x6404);/*  ms */
	rtw_write8(Adapter, REG_DRVERLYINT, DRIVER_EARLY_INT_TIME);/*  5ms */
	rtw_write8(Adapter, REG_BCNDMATIM, BCN_DMA_ATIME_INT_TIME); /*  2ms */

	/*  Suggested by designer timchen. Change beacon AIFS to the largest number */
	/*  beacause test chip does not contension before sending beacon. by tynli. 2009.11.03 */
	rtw_write16(Adapter, REG_BCNTCFG, 0x660F);

	haldata->RegFwHwTxQCtrl = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL + 2);
	haldata->RegReg542 = rtw_read8(Adapter, REG_TBTT_PROHIBIT + 2);
	haldata->RegCR_1 = rtw_read8(Adapter, REG_CR + 1);
}

static void _BeaconFunctionEnable(struct adapter *Adapter,
				  bool Enable, bool Linked)
{
	rtw_write8(Adapter, REG_BCN_CTRL, (BIT(4) | BIT(3) | BIT(1)));

	rtw_write8(Adapter, REG_RD_CTRL + 1, 0x6F);
}

/*  Set CCK and OFDM Block "ON" */
static void _BBTurnOnBlock(struct adapter *Adapter)
{
	rtl8188e_PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bCCKEn, 0x1);
	rtl8188e_PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bOFDMEn, 0x1);
}

enum {
	Antenna_Lfet = 1,
	Antenna_Right = 2,
};

static void _InitAntenna_Selection(struct adapter *Adapter)
{
	struct hal_data_8188e *haldata = &Adapter->haldata;

	if (haldata->AntDivCfg == 0)
		return;
	DBG_88E("==>  %s ....\n", __func__);

	rtw_write32(Adapter, REG_LEDCFG0, rtw_read32(Adapter, REG_LEDCFG0) | BIT(23));
	rtl8188e_PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, BIT(13), 0x01);

	if (rtl8188e_PHY_QueryBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, 0x300) == Antenna_A)
		haldata->CurAntenna = Antenna_A;
	else
		haldata->CurAntenna = Antenna_B;
	DBG_88E("%s,Cur_ant:(%x)%s\n", __func__, haldata->CurAntenna, (haldata->CurAntenna == Antenna_A) ? "Antenna_A" : "Antenna_B");
}

u32 rtl8188eu_hal_init(struct adapter *Adapter)
{
	u8 value8 = 0;
	u16  value16;
	u8 txpktbuf_bndy;
	u32 status = _SUCCESS;
	struct hal_data_8188e *haldata = &Adapter->haldata;
	struct pwrctrl_priv		*pwrctrlpriv = &Adapter->pwrctrlpriv;
	struct registry_priv	*pregistrypriv = &Adapter->registrypriv;
	u32 init_start_time = jiffies;

	if (Adapter->pwrctrlpriv.bkeepfwalive) {
		if (haldata->odmpriv.RFCalibrateInfo.bIQKInitialized) {
			PHY_IQCalibrate_8188E(Adapter, true);
		} else {
			PHY_IQCalibrate_8188E(Adapter, false);
			haldata->odmpriv.RFCalibrateInfo.bIQKInitialized = true;
		}

		ODM_TXPowerTrackingCheck(&haldata->odmpriv);
		PHY_LCCalibrate_8188E(Adapter);

		goto exit;
	}

	status = rtl8188eu_InitPowerOn(Adapter);
	if (status == _FAIL)
		goto exit;

	/*  Save target channel */
	haldata->CurrentChannel = 6;/* default set to 6 */

	if (pwrctrlpriv->reg_rfoff) {
		pwrctrlpriv->rf_pwrstate = rf_off;
	}

	/*  2010/08/09 MH We need to check if we need to turnon or off RF after detecting */
	/*  HW GPIO pin. Before PHY_RFConfig8192C. */
	/*  2010/08/26 MH If Efuse does not support sective suspend then disable the function. */

	if (!pregistrypriv->wifi_spec) {
		txpktbuf_bndy = TX_PAGE_BOUNDARY_88E;
	} else {
		/*  for WMM */
		txpktbuf_bndy = WMM_NORMAL_TX_PAGE_BOUNDARY_88E;
	}

	_InitQueueReservedPage(Adapter);
	_InitQueuePriority(Adapter);
	_InitPageBoundary(Adapter);
	_InitTransferPageSize(Adapter);

	_InitTxBufferBoundary(Adapter, 0);

	status = rtl8188e_FirmwareDownload(Adapter);

	if (status != _SUCCESS) {
		DBG_88E("%s: Download Firmware failed!!\n", __func__);
		Adapter->bFWReady = false;
		haldata->fw_ractrl = false;
		return status;
	} else {
		Adapter->bFWReady = true;
		haldata->fw_ractrl = false;
	}
	rtl8188e_InitializeFirmwareVars(Adapter);

#if (HAL_MAC_ENABLE == 1)
	status = PHY_MACConfig8188E(Adapter);
	if (status == _FAIL) {
		DBG_88E(" ### Failed to init MAC ......\n ");
		goto exit;
	}
#endif

	/*  */
	/* d. Initialize BB related configurations. */
	/*  */
#if (HAL_BB_ENABLE == 1)
	status = PHY_BBConfig8188E(Adapter);
	if (status == _FAIL) {
		DBG_88E(" ### Failed to init BB ......\n ");
		goto exit;
	}
#endif

#if (HAL_RF_ENABLE == 1)
	status = PHY_RFConfig8188E(Adapter);
	if (status == _FAIL) {
		DBG_88E(" ### Failed to init RF ......\n ");
		goto exit;
	}
#endif

	status = rtl8188e_iol_efuse_patch(Adapter);
	if (status == _FAIL) {
		DBG_88E("%s  rtl8188e_iol_efuse_patch failed\n", __func__);
		goto exit;
	}

	_InitTxBufferBoundary(Adapter, txpktbuf_bndy);

	status =  InitLLTTable(Adapter, txpktbuf_bndy);
	if (status == _FAIL)
		goto exit;

	/*  Get Rx PHY status in order to report RSSI and others. */
	_InitDriverInfoSize(Adapter, DRVINFO_SZ);

	_InitInterrupt(Adapter);
	hal_init_macaddr(Adapter);/* set mac_address */
	_InitNetworkType(Adapter);/* set msr */
	_InitWMACSetting(Adapter);
	_InitAdaptiveCtrl(Adapter);
	_InitEDCA(Adapter);
	_InitRetryFunction(Adapter);
	InitUsbAggregationSetting(Adapter);
	_InitOperationMode(Adapter);/* todo */
	_InitBeaconParameters(Adapter);

	/*  */
	/*  Init CR MACTXEN, MACRXEN after setting RxFF boundary REG_TRXFF_BNDY to patch */
	/*  Hw bug which Hw initials RxFF boundary size to a value which is larger than the real Rx buffer size in 88E. */
	/*  */
	/*  Enable MACTXEN/MACRXEN block */
	value16 = rtw_read16(Adapter, REG_CR);
	value16 |= (MACTXEN | MACRXEN);
	rtw_write8(Adapter, REG_CR, value16);

	/* Enable TX Report */
	/* Enable Tx Report Timer */
	value8 = rtw_read8(Adapter, REG_TX_RPT_CTRL);
	rtw_write8(Adapter,  REG_TX_RPT_CTRL, (value8 | BIT(1) | BIT(0)));
	/* Set MAX RPT MACID */
	rtw_write8(Adapter,  REG_TX_RPT_CTRL + 1, 2);/* FOR sta mode ,0: bc/mc ,1:AP */
	/* Tx RPT Timer. Unit: 32us */
	rtw_write16(Adapter, REG_TX_RPT_TIME, 0xCdf0);

	rtw_write8(Adapter, REG_EARLY_MODE_CONTROL, 0);

	rtw_write16(Adapter, REG_PKT_VO_VI_LIFE_TIME, 0x0400);	/*  unit: 256us. 256ms */
	rtw_write16(Adapter, REG_PKT_BE_BK_LIFE_TIME, 0x0400);	/*  unit: 256us. 256ms */

	/* Keep RfRegChnlVal for later use. */
	haldata->RfRegChnlVal[0] = rtl8188e_PHY_QueryRFReg(Adapter, (enum rf_radio_path)0, RF_CHNLBW, bRFRegOffsetMask);
	haldata->RfRegChnlVal[1] = rtl8188e_PHY_QueryRFReg(Adapter, (enum rf_radio_path)1, RF_CHNLBW, bRFRegOffsetMask);

	_BBTurnOnBlock(Adapter);

	invalidate_cam_all(Adapter);

	/*  2010/12/17 MH We need to set TX power according to EFUSE content at first. */
	PHY_SetTxPowerLevel8188E(Adapter, haldata->CurrentChannel);

/*  Move by Neo for USB SS to below setp */
/* _RfPowerSave(Adapter); */

	_InitAntenna_Selection(Adapter);

	/*  */
	/*  Disable BAR, suggested by Scott */
	/*  2010.04.09 add by hpfan */
	/*  */
	rtw_write32(Adapter, REG_BAR_MODE_CTRL, 0x0201ffff);

	/*  HW SEQ CTRL */
	/* set 0x0 to 0xFF by tynli. Default enable HW SEQ NUM. */
	rtw_write8(Adapter, REG_HWSEQ_CTRL, 0xFF);

	if (pregistrypriv->wifi_spec)
		rtw_write16(Adapter, REG_FAST_EDCA_CTRL, 0);

	/* Nav limit , suggest by scott */
	rtw_write8(Adapter, 0x652, 0x0);

	rtl8188e_InitHalDm(Adapter);

	/*  2010/08/11 MH Merge from 8192SE for Minicard init. We need to confirm current radio status */
	/*  and then decide to enable RF or not.!!!??? For Selective suspend mode. We may not */
	/*  call initstruct adapter. May cause some problem?? */
	/*  Fix the bug that Hw/Sw radio off before S3/S4, the RF off action will not be executed */
	/*  in MgntActSet_RF_State() after wake up, because the value of haldata->eRFPowerState */
	/*  is the same as eRfOff, we should change it to eRfOn after we config RF parameters. */
	/*  Added by tynli. 2010.03.30. */
	pwrctrlpriv->rf_pwrstate = rf_on;

	/*  enable Tx report. */
	rtw_write8(Adapter,  REG_FWHW_TXQ_CTRL + 1, 0x0F);

	/*  Suggested by SD1 pisa. Added by tynli. 2011.10.21. */
	rtw_write8(Adapter, REG_EARLY_MODE_CONTROL + 3, 0x01);/* Pretx_en, for WEP/TKIP SEC */

	/* tynli_test_tx_report. */
	rtw_write16(Adapter, REG_TX_RPT_TIME, 0x3DF0);

	/* enable tx DMA to drop the redundate data of packet */
	rtw_write16(Adapter, REG_TXDMA_OFFSET_CHK, (rtw_read16(Adapter, REG_TXDMA_OFFSET_CHK) | DROP_DATA_EN));

	/*  2010/08/26 MH Merge from 8192CE. */
	if (pwrctrlpriv->rf_pwrstate == rf_on) {
		if (haldata->odmpriv.RFCalibrateInfo.bIQKInitialized) {
			PHY_IQCalibrate_8188E(Adapter, true);
		} else {
			PHY_IQCalibrate_8188E(Adapter, false);
			haldata->odmpriv.RFCalibrateInfo.bIQKInitialized = true;
		}

		ODM_TXPowerTrackingCheck(&haldata->odmpriv);

		PHY_LCCalibrate_8188E(Adapter);
	}

/*	_InitPABias(Adapter); */
	rtw_write8(Adapter, REG_USB_HRPWM, 0);

	/* ack for xmit mgmt frames. */
	rtw_write32(Adapter, REG_FWHW_TXQ_CTRL, rtw_read32(Adapter, REG_FWHW_TXQ_CTRL) | BIT(12));

exit:
	DBG_88E("%s in %dms\n", __func__, rtw_get_passing_time_ms(init_start_time));

	return status;
}

static void CardDisableRTL8188EU(struct adapter *Adapter)
{
	u8 val8;
	struct hal_data_8188e *haldata = &Adapter->haldata;

	/* Stop Tx Report Timer. 0x4EC[Bit1]=b'0 */
	val8 = rtw_read8(Adapter, REG_TX_RPT_CTRL);
	rtw_write8(Adapter, REG_TX_RPT_CTRL, val8 & (~BIT(1)));

	/*  stop rx */
	rtw_write8(Adapter, REG_CR, 0x0);

	/*  Run LPS WL RFOFF flow */
	HalPwrSeqCmdParsing(Adapter, Rtl8188E_NIC_LPS_ENTER_FLOW);

	/*  2. 0x1F[7:0] = 0		turn off RF */

	val8 = rtw_read8(Adapter, REG_MCUFWDL);
	if ((val8 & RAM_DL_SEL) && Adapter->bFWReady) { /* 8051 RAM code */
		/*  Reset MCU 0x2[10]=0. */
		val8 = rtw_read8(Adapter, REG_SYS_FUNC_EN + 1);
		val8 &= ~BIT(2);	/*  0x2[10], FEN_CPUEN */
		rtw_write8(Adapter, REG_SYS_FUNC_EN + 1, val8);
	}

	/*  reset MCU ready status */
	rtw_write8(Adapter, REG_MCUFWDL, 0);

	/* YJ,add,111212 */
	/* Disable 32k */
	val8 = rtw_read8(Adapter, REG_32K_CTRL);
	rtw_write8(Adapter, REG_32K_CTRL, val8 & (~BIT(0)));

	/*  Card disable power action flow */
	HalPwrSeqCmdParsing(Adapter, Rtl8188E_NIC_DISABLE_FLOW);

	/*  Reset MCU IO Wrapper */
	val8 = rtw_read8(Adapter, REG_RSV_CTRL + 1);
	rtw_write8(Adapter, REG_RSV_CTRL + 1, (val8 & (~BIT(3))));
	val8 = rtw_read8(Adapter, REG_RSV_CTRL + 1);
	rtw_write8(Adapter, REG_RSV_CTRL + 1, val8 | BIT(3));

	/* YJ,test add, 111207. For Power Consumption. */
	val8 = rtw_read8(Adapter, GPIO_IN);
	rtw_write8(Adapter, GPIO_OUT, val8);
	rtw_write8(Adapter, GPIO_IO_SEL, 0xFF);/* Reg0x46 */

	val8 = rtw_read8(Adapter, REG_GPIO_IO_SEL);
	rtw_write8(Adapter, REG_GPIO_IO_SEL, (val8 << 4));
	val8 = rtw_read8(Adapter, REG_GPIO_IO_SEL + 1);
	rtw_write8(Adapter, REG_GPIO_IO_SEL + 1, val8 | 0x0F);/* Reg0x43 */
	rtw_write32(Adapter, REG_BB_PAD_CTRL, 0x00080808);/* set LNA ,TRSW,EX_PA Pin to output mode */
	haldata->bMacPwrCtrlOn = false;
	Adapter->bFWReady = false;
}

u32 rtl8188eu_hal_deinit(struct adapter *Adapter)
{

	DBG_88E("==> %s\n", __func__);

	rtw_write32(Adapter, REG_HIMR_88E, IMR_DISABLED_88E);
	rtw_write32(Adapter, REG_HIMRE_88E, IMR_DISABLED_88E);

	DBG_88E("bkeepfwalive(%x)\n", Adapter->pwrctrlpriv.bkeepfwalive);
	if (!Adapter->pwrctrlpriv.bkeepfwalive) {
		if (Adapter->hw_init_completed) {
			CardDisableRTL8188EU(Adapter);
		}
	}
	return _SUCCESS;
 }

unsigned int rtl8188eu_inirp_init(struct adapter *Adapter)
{
	u8 i;
	struct recv_buf *precvbuf;
	uint	status;
	struct recv_priv *precvpriv = &Adapter->recvpriv;

	status = _SUCCESS;

	/* issue Rx irp to receive data */
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;
	for (i = 0; i < NR_RECVBUFF; i++) {
		if (!rtw_read_port(Adapter, (unsigned char *)precvbuf)) {
			status = _FAIL;
			goto exit;
		}

		precvbuf++;
		precvpriv->free_recv_buf_queue_cnt--;
	}

exit:
	return status;
}

/*  */
/*  */
/*	EEPROM/EFUSE Content Parsing */
/*  */
/*  */

static void Hal_EfuseParseMACAddr_8188EU(struct adapter *adapt, u8 *hwinfo, bool AutoLoadFail)
{
	u16 i;
	u8 sMacAddr[6] = {0x00, 0xE0, 0x4C, 0x81, 0x88, 0x02};
	struct eeprom_priv *eeprom = &adapt->eeprompriv;

	if (AutoLoadFail) {
		for (i = 0; i < 6; i++)
			eeprom->mac_addr[i] = sMacAddr[i];
	} else {
		/* Read Permanent MAC address */
		memcpy(eeprom->mac_addr, &hwinfo[EEPROM_MAC_ADDR_88EU], ETH_ALEN);
	}
}

void ReadAdapterInfo8188EU(struct adapter *Adapter)
{
	struct eeprom_priv *eeprom = &Adapter->eeprompriv;
	struct led_priv *ledpriv = &Adapter->ledpriv;
	u8 eeValue;

	/*  Read EEPROM size before call any EEPROM function */
	Adapter->EepromAddressSize = GetEEPROMSize8188E(Adapter);

	/* check system boot selection */
	eeValue = rtw_read8(Adapter, REG_9346CR);
	eeprom->EepromOrEfuse		= (eeValue & BOOT_FROM_EEPROM);
	eeprom->bautoload_fail_flag	= !(eeValue & EEPROM_EN);

	DBG_88E("Boot from %s, Autoload %s !\n", (eeprom->EepromOrEfuse ? "EEPROM" : "EFUSE"),
		(eeprom->bautoload_fail_flag ? "Fail" : "OK"));

	if (!is_boot_from_eeprom(Adapter))
		EFUSE_ShadowMapUpdate(Adapter);

	/* parse the eeprom/efuse content */
	Hal_EfuseParseIDCode88E(Adapter, eeprom->efuse_eeprom_data);
	Hal_EfuseParseMACAddr_8188EU(Adapter, eeprom->efuse_eeprom_data, eeprom->bautoload_fail_flag);

	Hal_ReadPowerSavingMode88E(Adapter, eeprom->efuse_eeprom_data, eeprom->bautoload_fail_flag);
	Hal_ReadTxPowerInfo88E(Adapter, eeprom->efuse_eeprom_data, eeprom->bautoload_fail_flag);
	rtl8188e_EfuseParseChnlPlan(Adapter, eeprom->efuse_eeprom_data, eeprom->bautoload_fail_flag);
	Hal_EfuseParseXtal_8188E(Adapter, eeprom->efuse_eeprom_data, eeprom->bautoload_fail_flag);
	Hal_ReadAntennaDiversity88E(Adapter, eeprom->efuse_eeprom_data, eeprom->bautoload_fail_flag);
	Hal_ReadThermalMeter_88E(Adapter, eeprom->efuse_eeprom_data, eeprom->bautoload_fail_flag);

	ledpriv->bRegUseLed = true;
}

static void ResumeTxBeacon(struct adapter *adapt)
{
	struct hal_data_8188e *haldata = &adapt->haldata;

	/*  2010.03.01. Marked by tynli. No need to call workitem beacause we record the value */
	/*  which should be read from register to a global variable. */

	rtw_write8(adapt, REG_FWHW_TXQ_CTRL + 2, (haldata->RegFwHwTxQCtrl) | BIT(6));
	haldata->RegFwHwTxQCtrl |= BIT(6);
	rtw_write8(adapt, REG_TBTT_PROHIBIT + 1, 0xff);
	haldata->RegReg542 |= BIT(0);
	rtw_write8(adapt, REG_TBTT_PROHIBIT + 2, haldata->RegReg542);
}

static void StopTxBeacon(struct adapter *adapt)
{
	struct hal_data_8188e *haldata = &adapt->haldata;

	/*  2010.03.01. Marked by tynli. No need to call workitem beacause we record the value */
	/*  which should be read from register to a global variable. */

	rtw_write8(adapt, REG_FWHW_TXQ_CTRL + 2, (haldata->RegFwHwTxQCtrl) & (~BIT(6)));
	haldata->RegFwHwTxQCtrl &= (~BIT(6));
	rtw_write8(adapt, REG_TBTT_PROHIBIT + 1, 0x64);
	haldata->RegReg542 &= ~(BIT(0));
	rtw_write8(adapt, REG_TBTT_PROHIBIT + 2, haldata->RegReg542);

	 /* todo: CheckFwRsvdPageContent(Adapter);  2010.06.23. Added by tynli. */
}

static void hw_var_set_opmode(struct adapter *Adapter, u8 variable, u8 *val)
{
	u8 val8;
	u8 mode = *((u8 *)val);

	/*  disable Port0 TSF update */
	rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL) | BIT(4));

	/*  set net_type */
	val8 = rtw_read8(Adapter, MSR) & 0x0c;
	val8 |= mode;
	rtw_write8(Adapter, MSR, val8);

	DBG_88E("%s()-%d mode = %d\n", __func__, __LINE__, mode);

	if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_)) {
		StopTxBeacon(Adapter);

		rtw_write8(Adapter, REG_BCN_CTRL, 0x19);/* disable atim wnd */
	} else if (mode == _HW_STATE_ADHOC_) {
		ResumeTxBeacon(Adapter);
		rtw_write8(Adapter, REG_BCN_CTRL, 0x1a);
	} else if (mode == _HW_STATE_AP_) {
		ResumeTxBeacon(Adapter);

		rtw_write8(Adapter, REG_BCN_CTRL, 0x12);

		/* Set RCR */
		rtw_write32(Adapter, REG_RCR, 0x7000208e);/* CBSSID_DATA must set to 0,reject ICV_ERR packet */
		/* enable to rx data frame */
		rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);
		/* enable to rx ps-poll */
		rtw_write16(Adapter, REG_RXFLTMAP1, 0x0400);

		/* Beacon Control related register for first time */
		rtw_write8(Adapter, REG_BCNDMATIM, 0x02); /*  2ms */

		rtw_write8(Adapter, REG_ATIMWND, 0x0a); /*  10ms */
		rtw_write16(Adapter, REG_BCNTCFG, 0x00);
		rtw_write16(Adapter, REG_TBTT_PROHIBIT, 0xff04);
		rtw_write16(Adapter, REG_TSFTR_SYN_OFFSET, 0x7fff);/*  +32767 (~32ms) */

		/* reset TSF */
		rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(0));

		/* BIT(3) - If set 0, hw will clr bcnq when tx becon ok/fail or port 0 */
		rtw_write8(Adapter, REG_MBID_NUM, rtw_read8(Adapter, REG_MBID_NUM) | BIT(3) | BIT(4));

		/* enable BCN0 Function for if1 */
		/* don't enable update TSF0 for if1 (due to TSF update when beacon/probe rsp are received) */
		rtw_write8(Adapter, REG_BCN_CTRL, (DIS_TSF_UDT0_NORMAL_CHIP | EN_BCN_FUNCTION | BIT(1)));

		/* dis BCN1 ATIM  WND if if2 is station */
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1) | BIT(0));
	}
}

static void hw_var_set_macaddr(struct adapter *Adapter, u8 variable, u8 *val)
{
	u8 idx = 0;
	u32 reg_macid;

	reg_macid = REG_MACID;

	for (idx = 0; idx < 6; idx++)
		rtw_write8(Adapter, (reg_macid + idx), val[idx]);
}

static void hw_var_set_bssid(struct adapter *Adapter, u8 variable, u8 *val)
{
	u8 idx = 0;
	u32 reg_bssid;

	reg_bssid = REG_BSSID;

	for (idx = 0; idx < 6; idx++)
		rtw_write8(Adapter, (reg_bssid + idx), val[idx]);
}

static void hw_var_set_bcn_func(struct adapter *Adapter, u8 variable, u8 *val)
{
	u32 bcn_ctrl_reg;

	bcn_ctrl_reg = REG_BCN_CTRL;

	if (*((u8 *)val))
		rtw_write8(Adapter, bcn_ctrl_reg, (EN_BCN_FUNCTION | EN_TXBCN_RPT));
	else
		rtw_write8(Adapter, bcn_ctrl_reg, rtw_read8(Adapter, bcn_ctrl_reg) & (~(EN_BCN_FUNCTION | EN_TXBCN_RPT)));
}

void SetHwReg8188EU(struct adapter *Adapter, u8 variable, u8 *val)
{
	struct hal_data_8188e *haldata = &Adapter->haldata;
	struct dm_priv	*pdmpriv = &haldata->dmpriv;
	struct odm_dm_struct *podmpriv = &haldata->odmpriv;

	switch (variable) {
	case HW_VAR_MEDIA_STATUS:
		{
			u8 val8;

			val8 = rtw_read8(Adapter, MSR) & 0x0c;
			val8 |= *((u8 *)val);
			rtw_write8(Adapter, MSR, val8);
		}
		break;
	case HW_VAR_MEDIA_STATUS1:
		{
			u8 val8;

			val8 = rtw_read8(Adapter, MSR) & 0x03;
			val8 |= *((u8 *)val) << 2;
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
			u16 BrateCfg = 0;
			u8 RateIndex = 0;

			/*  2007.01.16, by Emily */
			/*  Select RRSR (in Legacy-OFDM and CCK) */
			/*  For 8190, we select only 24M, 12M, 6M, 11M, 5.5M, 2M, and 1M from the Basic rate. */
			/*  We do not use other rates. */
			HalSetBrateCfg(Adapter, val, &BrateCfg);
			DBG_88E("HW_VAR_BASIC_RATE: BrateCfg(%#x)\n", BrateCfg);

			/* 2011.03.30 add by Luke Lee */
			/* CCK 2M ACK should be disabled for some BCM and Atheros AP IOT */
			/* because CCK 2M has poor TXEVM */
			/* CCK 5.5M & 11M ACK should be enabled for better performance */

			BrateCfg = (BrateCfg | 0xd) & 0x15d;
			haldata->BasicRateSet = BrateCfg;

			BrateCfg |= 0x01; /*  default enable 1M ACK rate */
			/*  Set RRSR rate table. */
			rtw_write8(Adapter, REG_RRSR, BrateCfg & 0xff);
			rtw_write8(Adapter, REG_RRSR + 1, (BrateCfg >> 8) & 0xff);
			rtw_write8(Adapter, REG_RRSR + 2, rtw_read8(Adapter, REG_RRSR + 2) & 0xf0);

			/*  Set RTS initial rate */
			while (BrateCfg > 0x1) {
				BrateCfg = (BrateCfg >> 1);
				RateIndex++;
			}
			/*  Ziv - Check */
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
		{
			u64	tsf;
			struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
			struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;

			tsf = pmlmeext->TSFValue - do_div(pmlmeext->TSFValue,
							  pmlmeinfo->bcn_interval * 1024) - 1024; /* us */

			if (((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE))
				StopTxBeacon(Adapter);

			/* disable related TSF function */
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL) & (~BIT(3)));

			rtw_write32(Adapter, REG_TSFTR, tsf);
			rtw_write32(Adapter, REG_TSFTR + 4, tsf >> 32);

			/* enable related TSF function */
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL) | BIT(3));

			if (((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE))
				ResumeTxBeacon(Adapter);
		}
		break;
	case HW_VAR_CHECK_BSSID:
		if (*((u8 *)val)) {
			rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR) | RCR_CBSSID_DATA | RCR_CBSSID_BCN);
		} else {
			u32 val32;

			val32 = rtw_read32(Adapter, REG_RCR);

			val32 &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN);

			rtw_write32(Adapter, REG_RCR, val32);
		}
		break;
	case HW_VAR_MLME_DISCONNECT:
		/* Set RCR to not to receive data frame when NO LINK state */
		/* reject all data frames */
		rtw_write16(Adapter, REG_RXFLTMAP2, 0x00);

		/* reset TSF */
		rtw_write8(Adapter, REG_DUAL_TSF_RST, (BIT(0) | BIT(1)));

		/* disable update TSF */
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL) | BIT(4));
		break;
	case HW_VAR_MLME_SITESURVEY:
		if (*((u8 *)val)) { /* under sitesurvey */
			/* config RCR to receive different BSSID & not to receive data frame */
			u32 v = rtw_read32(Adapter, REG_RCR);
			v &= ~(RCR_CBSSID_BCN);
			rtw_write32(Adapter, REG_RCR, v);
			/* reject all data frame */
			rtw_write16(Adapter, REG_RXFLTMAP2, 0x00);

			/* disable update TSF */
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL) | BIT(4));
		} else { /* sitesurvey done */
			struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
			struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;

			if ((is_client_associated_to_ap(Adapter)) ||
			    ((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE)) {
				/* enable to rx data frame */
				rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);

				/* enable update TSF */
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL) & (~BIT(4)));
			} else if ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) {
				rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);
				/* enable update TSF */
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL) & (~BIT(4)));
			}
			if ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) {
				rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR) | RCR_CBSSID_BCN);
			} else {
				if (Adapter->in_cta_test) {
					u32 v = rtw_read32(Adapter, REG_RCR);
					v &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN);/*  RCR_ADF */
					rtw_write32(Adapter, REG_RCR, v);
				} else {
					rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR) | RCR_CBSSID_BCN);
				}
			}
		}
		break;
	case HW_VAR_MLME_JOIN:
		{
			u8 RetryLimit = 0x30;
			u8 type = *((u8 *)val);
			struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;

			if (type == 0) { /*  prepare to join */
				/* enable to rx data frame.Accept all data frame */
				rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);

				if (Adapter->in_cta_test) {
					u32 v = rtw_read32(Adapter, REG_RCR);
					v &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN);/*  RCR_ADF */
					rtw_write32(Adapter, REG_RCR, v);
				} else {
					rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR) | RCR_CBSSID_DATA | RCR_CBSSID_BCN);
				}

				if (check_fwstate(pmlmepriv, WIFI_STATION_STATE))
					RetryLimit = 48;
				else /*  Ad-hoc Mode */
					RetryLimit = 0x7;
			} else if (type == 1) {
				/* joinbss_event call back when join res < 0 */
				rtw_write16(Adapter, REG_RXFLTMAP2, 0x00);
			} else if (type == 2) {
				/* sta add event call back */
				/* enable update TSF */
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL) & (~BIT(4)));

				if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE))
					RetryLimit = 0x7;
			}
			rtw_write16(Adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);
		}
		break;
	case HW_VAR_BEACON_INTERVAL:
		rtw_write16(Adapter, REG_BCN_INTERVAL, *((u16 *)val));
		break;
	case HW_VAR_SLOT_TIME:
		{
			u8 u1bAIFS, aSifsTime;
			struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
			struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;

			rtw_write8(Adapter, REG_SLOT, val[0]);

			if (pmlmeinfo->WMM_enable == 0) {
				if (pmlmeext->cur_wireless_mode == WIRELESS_11B)
					aSifsTime = 10;
				else
					aSifsTime = 16;

				u1bAIFS = aSifsTime + (2 * pmlmeinfo->slotTime);

				/*  <Roger_EXP> Temporary removed, 2008.06.20. */
				rtw_write8(Adapter, REG_EDCA_VO_PARAM, u1bAIFS);
				rtw_write8(Adapter, REG_EDCA_VI_PARAM, u1bAIFS);
				rtw_write8(Adapter, REG_EDCA_BE_PARAM, u1bAIFS);
				rtw_write8(Adapter, REG_EDCA_BK_PARAM, u1bAIFS);
			}
		}
		break;
	case HW_VAR_RESP_SIFS:
		/* RESP_SIFS for CCK */
		rtw_write8(Adapter, REG_R2T_SIFS, val[0]); /*  SIFS_T2T_CCK (0x08) */
		rtw_write8(Adapter, REG_R2T_SIFS + 1, val[1]); /* SIFS_R2T_CCK(0x08) */
		/* RESP_SIFS for OFDM */
		rtw_write8(Adapter, REG_T2T_SIFS, val[2]); /* SIFS_T2T_OFDM (0x0a) */
		rtw_write8(Adapter, REG_T2T_SIFS + 1, val[3]); /* SIFS_R2T_OFDM(0x0a) */
		break;
	case HW_VAR_ACK_PREAMBLE:
		{
			u8 regTmp;
			u8 bShortPreamble = *((bool *)val);
			/*  Joseph marked out for Netgear 3500 TKIP channel 7 issue.(Temporarily) */
			regTmp = (haldata->nCur40MhzPrimeSC) << 5;
			if (bShortPreamble)
				regTmp |= 0x80;

			rtw_write8(Adapter, REG_RRSR + 2, regTmp);
		}
		break;
	case HW_VAR_SEC_CFG:
		rtw_write8(Adapter, REG_SECCFG, *((u8 *)val));
		break;
	case HW_VAR_DM_FLAG:
		podmpriv->SupportAbility = *((u8 *)val);
		break;
	case HW_VAR_DM_FUNC_OP:
		if (val[0])
			podmpriv->BK_SupportAbility = podmpriv->SupportAbility;
		else
			podmpriv->SupportAbility = podmpriv->BK_SupportAbility;
		break;
	case HW_VAR_DM_FUNC_SET:
		if (*((u32 *)val) == DYNAMIC_ALL_FUNC_ENABLE) {
			podmpriv->SupportAbility =	pdmpriv->InitODMFlag;
		} else {
			podmpriv->SupportAbility |= *((u32 *)val);
		}
		break;
	case HW_VAR_DM_FUNC_CLR:
		podmpriv->SupportAbility &= *((u32 *)val);
		break;
	case HW_VAR_CAM_EMPTY_ENTRY:
		{
			u8 ucIndex = *((u8 *)val);
			u8 i;
			u32 ulCommand = 0;
			u32 ulContent = 0;
			u32 ulEncAlgo = CAM_AES;

			for (i = 0; i < CAM_CONTENT_COUNT; i++) {
				/*  filled id in CAM config 2 byte */
				if (i == 0)
					ulContent |= (ucIndex & 0x03) | ((u16)(ulEncAlgo) << 2);
				else
					ulContent = 0;
				/*  polling bit, and No Write enable, and address */
				ulCommand = CAM_CONTENT_COUNT * ucIndex + i;
				ulCommand = ulCommand | CAM_POLLINIG | CAM_WRITE;
				/*  write content 0 is equall to mark invalid */
				rtw_write32(Adapter, WCAMI, ulContent);  /* delay_ms(40); */
				rtw_write32(Adapter, RWCAM, ulCommand);  /* delay_ms(40); */
			}
		}
		break;
	case HW_VAR_CAM_INVALID_ALL:
		rtw_write32(Adapter, RWCAM, BIT(31) | BIT(30));
		break;
	case HW_VAR_CAM_WRITE:
		{
			u32 cmd;
			u32 *cam_val = (u32 *)val;
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
		haldata->AcParam_BE = ((u32 *)(val))[0];
		rtw_write32(Adapter, REG_EDCA_BE_PARAM, ((u32 *)(val))[0]);
		break;
	case HW_VAR_AC_PARAM_BK:
		rtw_write32(Adapter, REG_EDCA_BK_PARAM, ((u32 *)(val))[0]);
		break;
	case HW_VAR_ACM_CTRL:
		{
			u8 acm_ctrl = *((u8 *)val);
			u8 AcmCtrl = rtw_read8(Adapter, REG_ACMHWCTRL);

			if (acm_ctrl > 1)
				AcmCtrl = AcmCtrl | 0x1;

			if (acm_ctrl & BIT(3))
				AcmCtrl |= AcmHw_VoqEn;
			else
				AcmCtrl &= (~AcmHw_VoqEn);

			if (acm_ctrl & BIT(2))
				AcmCtrl |= AcmHw_ViqEn;
			else
				AcmCtrl &= (~AcmHw_ViqEn);

			if (acm_ctrl & BIT(1))
				AcmCtrl |= AcmHw_BeqEn;
			else
				AcmCtrl &= (~AcmHw_BeqEn);

			DBG_88E("[HW_VAR_ACM_CTRL] Write 0x%X\n", AcmCtrl);
			rtw_write8(Adapter, REG_ACMHWCTRL, AcmCtrl);
		}
		break;
	case HW_VAR_AMPDU_MIN_SPACE:
		{
			u8 MinSpacingToSet;
			u8 SecMinSpace;

			MinSpacingToSet = *((u8 *)val);
			if (MinSpacingToSet <= 7) {
				switch (Adapter->securitypriv.dot11PrivacyAlgrthm) {
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
				if (MinSpacingToSet < SecMinSpace)
					MinSpacingToSet = SecMinSpace;
				rtw_write8(Adapter, REG_AMPDU_MIN_SPACE, (rtw_read8(Adapter, REG_AMPDU_MIN_SPACE) & 0xf8) | MinSpacingToSet);
			}
		}
		break;
	case HW_VAR_AMPDU_FACTOR:
		{
			u8 RegToSet_Normal[4] = {0x41, 0xa8, 0x72, 0xb9};
			u8 FactorToSet;
			u8 *pRegToSet;
			u8 index = 0;

			pRegToSet = RegToSet_Normal; /*  0xb972a841; */
			FactorToSet = *((u8 *)val);
			if (FactorToSet <= 3) {
				FactorToSet = (1 << (FactorToSet + 2));
				if (FactorToSet > 0xf)
					FactorToSet = 0xf;

				for (index = 0; index < 4; index++) {
					if ((pRegToSet[index] & 0xf0) > (FactorToSet << 4))
						pRegToSet[index] = (pRegToSet[index] & 0x0f) | (FactorToSet << 4);

					if ((pRegToSet[index] & 0x0f) > FactorToSet)
						pRegToSet[index] = (pRegToSet[index] & 0xf0) | (FactorToSet);

					rtw_write8(Adapter, (REG_AGGLEN_LMT + index), pRegToSet[index]);
				}
			}
		}
		break;
	case HW_VAR_RXDMA_AGG_PG_TH:
		{
			u8 threshold = *((u8 *)val);
			if (threshold == 0)
				threshold = haldata->UsbRxAggPageCount;
			rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH, threshold);
		}
		break;
	case HW_VAR_H2C_FW_PWRMODE:
		{
			u8 psmode = (*(u8 *)val);

			/*  Forece leave RF low power mode for 1T1R to prevent conficting setting in Fw power */
			/*  saving sequence. 2010.06.07. Added by tynli. Suggested by SD3 yschang. */
			if (psmode != PS_MODE_ACTIVE)
				ODM_RF_Saving(podmpriv, true);
			rtl8188e_set_FwPwrMode_cmd(Adapter, psmode);
		}
		break;
	case HW_VAR_H2C_FW_JOINBSSRPT:
		{
			u8 mstatus = (*(u8 *)val);
			rtl8188e_set_FwJoinBssReport_cmd(Adapter, mstatus);
		}
		break;
	case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
		{
			u8 p2p_ps_state = (*(u8 *)val);
			rtl8188e_set_p2p_ps_offload_cmd(Adapter, p2p_ps_state);
		}
		break;
	case HW_VAR_INITIAL_GAIN:
		{
			struct rtw_dig *pDigTable = &podmpriv->DM_DigTable;
			u32 rx_gain = ((u32 *)(val))[0];

			if (rx_gain == 0xff) {/* restore rx gain */
				ODM_Write_DIG(podmpriv, pDigTable->BackupIGValue);
			} else {
				pDigTable->BackupIGValue = pDigTable->CurIGValue;
				ODM_Write_DIG(podmpriv, rx_gain);
			}
		}
		break;
	case HW_VAR_RPT_TIMER_SETTING:
		{
			u16 min_rpt_time = (*(u16 *)val);
			ODM_RA_Set_TxRPT_Time(podmpriv, min_rpt_time);
		}
		break;
	case HW_VAR_ANTENNA_DIVERSITY_SELECT:
		{
			u8 Optimum_antenna = (*(u8 *)val);
			u8 Ant;
			/* switch antenna to Optimum_antenna */
			if (haldata->CurAntenna !=  Optimum_antenna) {
				Ant = (Optimum_antenna == 2) ? MAIN_ANT : AUX_ANT;
				ODM_UpdateRxIdleAnt_88E(&haldata->odmpriv, Ant);

				haldata->CurAntenna = Optimum_antenna;
			}
		}
		break;
	case HW_VAR_EFUSE_BYTES: /*  To set EFUE total used bytes, added by Roger, 2008.12.22. */
		haldata->EfuseUsedBytes = *((u16 *)val);
		break;
	case HW_VAR_FIFO_CLEARN_UP:
		{
			struct pwrctrl_priv *pwrpriv = &Adapter->pwrctrlpriv;
			u8 trycnt = 100;

			/* pause tx */
			rtw_write8(Adapter, REG_TXPAUSE, 0xff);

			/* keep sn */
			Adapter->xmitpriv.nqos_ssn = rtw_read16(Adapter, REG_NQOS_SEQ);

			if (!pwrpriv->bkeepfwalive) {
				/* RX DMA stop */
				rtw_write32(Adapter, REG_RXPKT_NUM, (rtw_read32(Adapter, REG_RXPKT_NUM) | RW_RELEASE_EN));
				do {
					if (!(rtw_read32(Adapter, REG_RXPKT_NUM) & RXDMA_IDLE))
						break;
				} while (trycnt--);
				if (trycnt == 0)
					DBG_88E("Stop RX DMA failed......\n");

				/* RQPN Load 0 */
				rtw_write16(Adapter, REG_RQPN_NPQ, 0x0);
				rtw_write32(Adapter, REG_RQPN, 0x80000000);
				mdelay(10);
			}
		}
		break;
	case HW_VAR_APFM_ON_MAC:
		haldata->bMacPwrCtrlOn = *val;
		DBG_88E("%s: bMacPwrCtrlOn=%d\n", __func__, haldata->bMacPwrCtrlOn);
		break;
	case HW_VAR_TX_RPT_MAX_MACID:
		{
			u8 maxMacid = *val;
			DBG_88E("### MacID(%d),Set Max Tx RPT MID(%d)\n", maxMacid, maxMacid + 1);
			rtw_write8(Adapter, REG_TX_RPT_CTRL + 1, maxMacid + 1);
		}
		break;
	case HW_VAR_H2C_MEDIA_STATUS_RPT:
		rtl8188e_set_FwMediaStatus_cmd(Adapter, (*(__le16 *)val));
		break;
	case HW_VAR_BCN_VALID:
		/* BCN_VALID, BIT(16) of REG_TDECTRL = BIT(0) of REG_TDECTRL+2, write 1 to clear, Clear by sw */
		rtw_write8(Adapter, REG_TDECTRL + 2, rtw_read8(Adapter, REG_TDECTRL + 2) | BIT(0));
		break;
	default:
		break;
	}

}

void GetHwReg8188EU(struct adapter *Adapter, u8 variable, u8 *val)
{
	struct hal_data_8188e *haldata = &Adapter->haldata;
	struct odm_dm_struct *podmpriv = &haldata->odmpriv;

	switch (variable) {
	case HW_VAR_BASIC_RATE:
		*((u16 *)(val)) = haldata->BasicRateSet;
		fallthrough;
	case HW_VAR_TXPAUSE:
		val[0] = rtw_read8(Adapter, REG_TXPAUSE);
		break;
	case HW_VAR_BCN_VALID:
		/* BCN_VALID, BIT(16) of REG_TDECTRL = BIT(0) of REG_TDECTRL+2 */
		val[0] = (BIT(0) & rtw_read8(Adapter, REG_TDECTRL + 2)) ? true : false;
		break;
	case HW_VAR_DM_FLAG:
		val[0] = podmpriv->SupportAbility;
		break;
	case HW_VAR_FWLPS_RF_ON:
		{
			/* When we halt NIC, we should check if FW LPS is leave. */
			if (Adapter->pwrctrlpriv.rf_pwrstate == rf_off) {
				/*  If it is in HW/SW Radio OFF or IPS state, we do not check Fw LPS Leave, */
				/*  because Fw is unload. */
				val[0] = true;
			} else {
				u32 valRCR;
				valRCR = rtw_read32(Adapter, REG_RCR);
				valRCR &= 0x00070000;
				if (valRCR)
					val[0] = false;
				else
					val[0] = true;
			}
		}
		break;
	case HW_VAR_CURRENT_ANTENNA:
		val[0] = haldata->CurAntenna;
		break;
	case HW_VAR_EFUSE_BYTES: /*  To get EFUE total used bytes, added by Roger, 2008.12.22. */
		*((u16 *)(val)) = haldata->EfuseUsedBytes;
		break;
	case HW_VAR_APFM_ON_MAC:
		*val = haldata->bMacPwrCtrlOn;
		break;
	case HW_VAR_CHK_HI_QUEUE_EMPTY:
		*val = ((rtw_read32(Adapter, REG_HGQ_INFORMATION) & 0x0000ff00) == 0) ? true : false;
		break;
	default:
		break;
	}

}

/* Query setting of specified variable. */
u8 GetHalDefVar8188EUsb(struct adapter *Adapter, enum hal_def_variable eVariable, void *pValue)
{
	struct hal_data_8188e *haldata = &Adapter->haldata;
	u8 bResult = _SUCCESS;

	switch (eVariable) {
	case HAL_DEF_UNDERCORATEDSMOOTHEDPWDB:
		{
			struct mlme_priv *pmlmepriv = &Adapter->mlmepriv;
			struct sta_priv *pstapriv = &Adapter->stapriv;
			struct sta_info *psta;
			psta = rtw_get_stainfo(pstapriv, pmlmepriv->cur_network.network.MacAddress);
			if (psta)
				*((int *)pValue) = psta->rssi_stat.UndecoratedSmoothedPWDB;
		}
		break;
	case HAL_DEF_IS_SUPPORT_ANT_DIV:
		*((u8 *)pValue) = (haldata->AntDivCfg == 0) ? false : true;
		break;
	case HAL_DEF_CURRENT_ANTENNA:
		*((u8 *)pValue) = haldata->CurAntenna;
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
	case HAL_DEF_DBG_DM_FUNC:
		*((u32 *)pValue) = haldata->odmpriv.SupportAbility;
		break;
	case HAL_DEF_RA_DECISION_RATE:
		{
			u8 MacID = *((u8 *)pValue);
			*((u8 *)pValue) = ODM_RA_GetDecisionRate_8188E(&haldata->odmpriv, MacID);
		}
		break;
	case HAL_DEF_RA_SGI:
		{
			u8 MacID = *((u8 *)pValue);
			*((u8 *)pValue) = ODM_RA_GetShortGI_8188E(&haldata->odmpriv, MacID);
		}
		break;
	case HAL_DEF_PT_PWR_STATUS:
		{
			u8 MacID = *((u8 *)pValue);
			*((u8 *)pValue) = ODM_RA_GetHwPwrStatus_8188E(&haldata->odmpriv, MacID);
		}
		break;
	case HW_VAR_MAX_RX_AMPDU_FACTOR:
		*((u32 *)pValue) = MAX_AMPDU_FACTOR_64K;
		break;
	case HW_DEF_RA_INFO_DUMP:
		{
			u8 entry_id = *((u8 *)pValue);
			if (check_fwstate(&Adapter->mlmepriv, _FW_LINKED)) {
				DBG_88E("============ RA status check ===================\n");
				DBG_88E("Mac_id:%d , RateID = %d, RAUseRate = 0x%08x, RateSGI = %d, DecisionRate = 0x%02x ,PTStage = %d\n",
					entry_id,
					haldata->odmpriv.RAInfo[entry_id].RateID,
					haldata->odmpriv.RAInfo[entry_id].RAUseRate,
					haldata->odmpriv.RAInfo[entry_id].RateSGI,
					haldata->odmpriv.RAInfo[entry_id].DecisionRate,
					haldata->odmpriv.RAInfo[entry_id].PTStage);
			}
		}
		break;
	case HAL_DEF_DBG_DUMP_RXPKT:
		*((u8 *)pValue) = haldata->bDumpRxPkt;
		break;
	case HAL_DEF_DBG_DUMP_TXPKT:
		*((u8 *)pValue) = haldata->bDumpTxPkt;
		break;
	default:
		bResult = _FAIL;
		break;
	}

	return bResult;
}

/* Change default setting of specified variable. */
u8 SetHalDefVar8188EUsb(struct adapter *Adapter, enum hal_def_variable eVariable, void *pValue)
{
	struct hal_data_8188e *haldata = &Adapter->haldata;
	u8 bResult = _SUCCESS;

	switch (eVariable) {
	case HAL_DEF_DBG_DM_FUNC:
		{
			u8 dm_func = *((u8 *)pValue);
			struct odm_dm_struct *podmpriv = &haldata->odmpriv;

			if (dm_func == 0) { /* disable all dynamic func */
				podmpriv->SupportAbility = DYNAMIC_FUNC_DISABLE;
				DBG_88E("==> Disable all dynamic function...\n");
			} else if (dm_func == 1) {/* disable DIG */
				podmpriv->SupportAbility  &= (~DYNAMIC_BB_DIG);
				DBG_88E("==> Disable DIG...\n");
			} else if (dm_func == 2) {/* disable High power */
				podmpriv->SupportAbility  &= (~DYNAMIC_BB_DYNAMIC_TXPWR);
			} else if (dm_func == 3) {/* disable tx power tracking */
				podmpriv->SupportAbility  &= (~DYNAMIC_RF_CALIBRATION);
				DBG_88E("==> Disable tx power tracking...\n");
			} else if (dm_func == 5) {/* disable antenna diversity */
				podmpriv->SupportAbility  &= (~DYNAMIC_BB_ANT_DIV);
			} else if (dm_func == 6) {/* turn on all dynamic func */
				if (!(podmpriv->SupportAbility  & DYNAMIC_BB_DIG)) {
					struct rtw_dig *pDigTable = &podmpriv->DM_DigTable;
					pDigTable->CurIGValue = rtw_read8(Adapter, 0xc50);
				}
				podmpriv->SupportAbility = DYNAMIC_ALL_FUNC_ENABLE;
				DBG_88E("==> Turn on all dynamic function...\n");
			}
		}
		break;
	case HAL_DEF_DBG_DUMP_RXPKT:
		haldata->bDumpRxPkt = *((u8 *)pValue);
		break;
	case HAL_DEF_DBG_DUMP_TXPKT:
		haldata->bDumpTxPkt = *((u8 *)pValue);
		break;
	default:
		bResult = _FAIL;
		break;
	}

	return bResult;
}

void UpdateHalRAMask8188EUsb(struct adapter *adapt, u32 mac_id, u8 rssi_level)
{
	u8 init_rate = 0;
	u8 networkType, raid;
	u32 mask, rate_bitmap;
	u8 shortGIrate = false;
	int	supportRateNum = 0;
	struct sta_info	*psta;
	struct hal_data_8188e *haldata = &adapt->haldata;
	struct mlme_ext_priv	*pmlmeext = &adapt->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex	*cur_network = &pmlmeinfo->network;

	if (mac_id >= NUM_STA) /* CAM_SIZE */
		return;
	psta = pmlmeinfo->FW_sta_info[mac_id].psta;
	if (!psta)
		return;
	switch (mac_id) {
	case 0:/*  for infra mode */
		supportRateNum = rtw_get_rateset_len(cur_network->SupportedRates);
		networkType = judge_network_type(adapt, cur_network->SupportedRates, supportRateNum) & 0xf;
		raid = networktype_to_raid(networkType);
		mask = update_supported_rate(cur_network->SupportedRates, supportRateNum);
		mask |= (pmlmeinfo->HT_enable) ? update_MSC_rate(&pmlmeinfo->HT_caps) : 0;
		if (support_short_GI(adapt, &pmlmeinfo->HT_caps))
			shortGIrate = true;
		break;
	case 1:/* for broadcast/multicast */
		supportRateNum = rtw_get_rateset_len(pmlmeinfo->FW_sta_info[mac_id].SupportedRates);
		if (pmlmeext->cur_wireless_mode & WIRELESS_11B)
			networkType = WIRELESS_11B;
		else
			networkType = WIRELESS_11G;
		raid = networktype_to_raid(networkType);
		mask = update_basic_rate(cur_network->SupportedRates, supportRateNum);
		break;
	default: /* for each sta in IBSS */
		supportRateNum = rtw_get_rateset_len(pmlmeinfo->FW_sta_info[mac_id].SupportedRates);
		networkType = judge_network_type(adapt, pmlmeinfo->FW_sta_info[mac_id].SupportedRates, supportRateNum) & 0xf;
		raid = networktype_to_raid(networkType);
		mask = update_supported_rate(cur_network->SupportedRates, supportRateNum);

		/* todo: support HT in IBSS */
		break;
	}

	rate_bitmap = 0x0fffffff;
	rate_bitmap = ODM_Get_Rate_Bitmap(&haldata->odmpriv, mac_id, mask, rssi_level);
	DBG_88E("%s => mac_id:%d, networkType:0x%02x, mask:0x%08x\n\t ==> rssi_level:%d, rate_bitmap:0x%08x\n",
		__func__, mac_id, networkType, mask, rssi_level, rate_bitmap);

	mask &= rate_bitmap;

	init_rate = get_highest_rate_idx(mask) & 0x3f;

	if (haldata->fw_ractrl) {
		u8 arg;

		arg = mac_id & 0x1f;/* MACID */
		arg |= BIT(7);
		if (shortGIrate)
			arg |= BIT(5);
		mask |= ((raid << 28) & 0xf0000000);
		DBG_88E("update raid entry, mask=0x%x, arg=0x%x\n", mask, arg);
		psta->ra_mask = mask;
		mask |= ((raid << 28) & 0xf0000000);

		/* to do ,for 8188E-SMIC */
		rtl8188e_set_raid_cmd(adapt, mask);
	} else {
		ODM_RA_UpdateRateInfo_8188E(&haldata->odmpriv,
				mac_id,
				raid,
				mask,
				shortGIrate
				);
	}
	/* set ra_id */
	psta->raid = raid;
	psta->init_rate = init_rate;
}

void SetBeaconRelatedRegisters8188EUsb(struct adapter *adapt)
{
	u32 value32;
	struct mlme_ext_priv	*pmlmeext = &adapt->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;
	u32 bcn_ctrl_reg			= REG_BCN_CTRL;
	/* reset TSF, enable update TSF, correcting TSF On Beacon */

	/* BCN interval */
	rtw_write16(adapt, REG_BCN_INTERVAL, pmlmeinfo->bcn_interval);
	rtw_write8(adapt, REG_ATIMWND, 0x02);/*  2ms */

	_InitBeaconParameters(adapt);

	rtw_write8(adapt, REG_SLOT, 0x09);

	value32 = rtw_read32(adapt, REG_TCR);
	value32 &= ~TSFRST;
	rtw_write32(adapt,  REG_TCR, value32);

	value32 |= TSFRST;
	rtw_write32(adapt, REG_TCR, value32);

	/*  NOTE: Fix test chip's bug (about contention windows's randomness) */
	rtw_write8(adapt,  REG_RXTSF_OFFSET_CCK, 0x50);
	rtw_write8(adapt, REG_RXTSF_OFFSET_OFDM, 0x50);

	_BeaconFunctionEnable(adapt, true, true);

	ResumeTxBeacon(adapt);

	rtw_write8(adapt, bcn_ctrl_reg, rtw_read8(adapt, bcn_ctrl_reg) | BIT(1));
}

void rtl8188eu_init_default_value(struct adapter *adapt)
{
	struct hal_data_8188e *haldata = &adapt->haldata;
	struct pwrctrl_priv *pwrctrlpriv;
	u8 i;

	pwrctrlpriv = &adapt->pwrctrlpriv;

	/* init default value */
	haldata->fw_ractrl = false;
	if (!pwrctrlpriv->bkeepfwalive)
		haldata->LastHMEBoxNum = 0;

	/* init dm default value */
	haldata->odmpriv.RFCalibrateInfo.bIQKInitialized = false;
	haldata->odmpriv.RFCalibrateInfo.TM_Trigger = 0;/* for IQK */
	haldata->pwrGroupCnt = 0;
	haldata->PGMaxGroup = 13;
	haldata->odmpriv.RFCalibrateInfo.ThermalValue_HP_index = 0;
	for (i = 0; i < HP_THERMAL_NUM; i++)
		haldata->odmpriv.RFCalibrateInfo.ThermalValue_HP[i] = 0;
}
