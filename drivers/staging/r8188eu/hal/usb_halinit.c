// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _HCI_HAL_INIT_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/rtw_efuse.h"
#include "../include/rtw_fw.h"
#include "../include/rtl8188e_hal.h"
#include "../include/rtw_iol.h"
#include "../include/usb_ops.h"
#include "../include/usb_osintf.h"
#include "../include/HalPwrSeqCmd.h"

static void one_out_pipe(struct adapter *adapter)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(adapter);

	pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];/* VO */
	pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[0];/* VI */
	pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[0];/* BE */
	pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[0];/* BK */
}

static void two_out_pipe(struct adapter *adapter, bool wifi_cfg)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(adapter);

	/* 0:H, 1:L */

	pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[0];/* VI */
	pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[1];/* BE */

	if (wifi_cfg) {
		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[1];/* VO */
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[0];/* BK */
	} else {
		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];/* VO */
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[1];/* BK */
	}
}

static void three_out_pipe(struct adapter *adapter, bool wifi_cfg)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(adapter);

	/* 0:H, 1:N, 2:L */

	pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];/* VO */
	pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[1];/* VI */
	pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[2];/* BE */

	pdvobjpriv->Queue2Pipe[3] = wifi_cfg ?
		pdvobjpriv->RtOutPipe[1] : pdvobjpriv->RtOutPipe[2];/* BK */
}

int rtl8188eu_interface_configure(struct adapter *adapt)
{
	struct registry_priv *pregistrypriv = &adapt->registrypriv;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(adapt);
	struct hal_data_8188e *haldata = &adapt->haldata;
	bool wifi_cfg = pregistrypriv->wifi_spec;

	pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];/* BCN */
	pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];/* MGT */
	pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];/* HIGH */
	pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];/* TXCMD */

	switch (pdvobjpriv->RtNumOutPipes) {
	case 3:
		haldata->out_ep_extra_queues = TX_SELE_LQ | TX_SELE_NQ;
		three_out_pipe(adapt, wifi_cfg);
		break;
	case 2:
		haldata->out_ep_extra_queues = TX_SELE_NQ;
		two_out_pipe(adapt, wifi_cfg);
		break;
	case 1:
		one_out_pipe(adapt);
		break;
	default:
		return -ENXIO;
	}

	return 0;
}

u32 rtl8188eu_InitPowerOn(struct adapter *adapt)
{
	u16 value16;
	int res;

	/*  HW Power on sequence */
	struct hal_data_8188e *haldata = &adapt->haldata;
	if (haldata->bMacPwrCtrlOn)
		return _SUCCESS;

	if (!HalPwrSeqCmdParsing(adapt, PWR_ON_FLOW))
		return _FAIL;

	/*  Enable MAC DMA/WMAC/SCHEDULE/SEC block */
	/*  Set CR bit10 to enable 32k calibration. Suggested by SD1 Gimmy. Added by tynli. 2011.08.31. */
	rtw_write16(adapt, REG_CR, 0x00);  /* suggseted by zhouzhou, by page, 20111230 */

		/*  Enable MAC DMA/WMAC/SCHEDULE/SEC block */
	res = rtw_read16(adapt, REG_CR, &value16);
	if (res)
		return _FAIL;

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
	int res;

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
	res = rtw_read8(Adapter, REG_USB_SPECIAL_OPTION, &usb_opt);
	if (res)
		return;

	if (adapter_to_dvobj(Adapter)->pusbdev->speed == USB_SPEED_HIGH)
		usb_opt = usb_opt | (INT_BULK_SEL);
	else
		usb_opt = usb_opt & (~INT_BULK_SEL);

	rtw_write8(Adapter, REG_USB_SPECIAL_OPTION, usb_opt);
}

static void _InitQueueReservedPage(struct adapter *Adapter)
{
	struct hal_data_8188e *haldata = &Adapter->haldata;
	struct registry_priv	*pregistrypriv = &Adapter->registrypriv;
	u8 numLQ = 0;
	u8 numNQ = 0;
	u8 numPubQ;

	if (pregistrypriv->wifi_spec) {
		if (haldata->out_ep_extra_queues & TX_SELE_LQ)
			numLQ = 0x1C;

		/*  NOTE: This step shall be proceed before writing REG_RQPN. */
		if (haldata->out_ep_extra_queues & TX_SELE_NQ)
			numNQ = 0x1C;

		rtw_write8(Adapter, REG_RQPN_NPQ, numNQ);

		numPubQ = 0xA8 - NUM_HQ - numLQ - numNQ;

		/*  TX DMA */
		rtw_write32(Adapter, REG_RQPN, LD_RQPN | numPubQ << 16 | numLQ << 8 | NUM_HQ);
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
	u16 value16;
	int res;

	res = rtw_read16(Adapter, REG_TRXDMA_CTRL, &value16);
	if (res)
		return;

	value16 &= 0x7;

	value16 |= _TXDMA_BEQ_MAP(beQ)	| _TXDMA_BKQ_MAP(bkQ) |
		   _TXDMA_VIQ_MAP(viQ)	| _TXDMA_VOQ_MAP(voQ) |
		   _TXDMA_MGQ_MAP(mgtQ) | _TXDMA_HIQ_MAP(hiQ);

	rtw_write16(Adapter, REG_TRXDMA_CTRL, value16);
}

static void _InitNormalChipTwoOutEpPriority(struct adapter *Adapter)
{
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	u16 bkQ, voQ;

	if (!pregistrypriv->wifi_spec) {
		bkQ	= QUEUE_NORMAL;
		voQ	= QUEUE_HIGH;
	} else {/* for WMM ,CONFIG_OUT_EP_WIFI_MODE */
		bkQ	= QUEUE_HIGH;
		voQ	= QUEUE_NORMAL;
	}
	_InitNormalChipRegPriority(Adapter, QUEUE_NORMAL, bkQ, QUEUE_HIGH,
				   voQ, QUEUE_HIGH, QUEUE_HIGH);
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
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(Adapter);

	switch (pdvobjpriv->RtNumOutPipes) {
	case 1:
		_InitNormalChipRegPriority(Adapter, QUEUE_HIGH, QUEUE_HIGH, QUEUE_HIGH,
					   QUEUE_HIGH, QUEUE_HIGH, QUEUE_HIGH);
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
	int res;

	res = rtw_read32(Adapter, REG_CR, &value32);
	if (res)
		return;

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
	u32 receive_config = RCR_AAP | RCR_APM | RCR_AM | RCR_AB |
			     RCR_CBSSID_DATA | RCR_CBSSID_BCN |
			     RCR_APP_ICV | RCR_AMF | RCR_HTC_LOC_CTRL |
			     RCR_APP_MIC | RCR_APP_PHYSTS;

	/*  some REG_RCR will be modified later by phy_ConfigMACWithHeaderFile() */
	rtw_write32(Adapter, REG_RCR, receive_config);

	/*  Accept all multicast address */
	rtw_write32(Adapter, REG_MAR, 0xFFFFFFFF);
	rtw_write32(Adapter, REG_MAR + 4, 0xFFFFFFFF);
}

static void _InitAdaptiveCtrl(struct adapter *Adapter)
{
	u16 value16;
	u32 value32;
	int res;

	/*  Response Rate Set */
	res = rtw_read32(Adapter, REG_RRSR, &value32);
	if (res)
		return;

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
	int res;

	res = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL, &value8);
	if (res)
		return;

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
	u32 value32;
	int res;

	if (Adapter->registrypriv.wifi_spec)
		return;

	res = rtw_read32(Adapter, REG_TDECTRL, &value32);
	if (res)
		return;

	value32 = value32 & ~(BLK_DESC_NUM_MASK << BLK_DESC_NUM_SHIFT);
	value32 |= ((USB_TXAGG_DESC_NUM & BLK_DESC_NUM_MASK) << BLK_DESC_NUM_SHIFT);

	rtw_write32(Adapter, REG_TDECTRL, value32);
}

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
	u8 valueDMA;
	u8 valueUSB;
	int res;

	res = rtw_read8(Adapter, REG_TRXDMA_CTRL, &valueDMA);
	if (res)
		return;

	res = rtw_read8(Adapter, REG_USB_SPECIAL_OPTION, &valueUSB);
	if (res)
		return;

	valueDMA |= RXDMA_AGG_EN;
	valueUSB &= ~USB_AGG_EN;

	rtw_write8(Adapter, REG_TRXDMA_CTRL, valueDMA);
	rtw_write8(Adapter, REG_USB_SPECIAL_OPTION, valueUSB);

	rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH, USB_RXAGG_PAGE_COUNT);
	rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH + 1, USB_RXAGG_PAGE_TIMEOUT);
}

static void InitUsbAggregationSetting(struct adapter *Adapter)
{
	/*  Tx aggregation setting */
	usb_AggSettingTxUpdate(Adapter);

	/*  Rx aggregation setting */
	usb_AggSettingRxUpdate(Adapter);
}

/* FIXME: add error handling in callers */
static int _InitBeaconParameters(struct adapter *Adapter)
{
	struct hal_data_8188e *haldata = &Adapter->haldata;
	int res;

	rtw_write16(Adapter, REG_BCN_CTRL, 0x1010);

	/*  TODO: Remove these magic number */
	rtw_write16(Adapter, REG_TBTT_PROHIBIT, 0x6404);/*  ms */
	rtw_write8(Adapter, REG_DRVERLYINT, DRIVER_EARLY_INT_TIME);/*  5ms */
	rtw_write8(Adapter, REG_BCNDMATIM, BCN_DMA_ATIME_INT_TIME); /*  2ms */

	/*  Suggested by designer timchen. Change beacon AIFS to the largest number */
	/*  beacause test chip does not contension before sending beacon. by tynli. 2009.11.03 */
	rtw_write16(Adapter, REG_BCNTCFG, 0x660F);

	res = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL + 2, &haldata->RegFwHwTxQCtrl);
	if (res)
		return res;

	res = rtw_read8(Adapter, REG_TBTT_PROHIBIT + 2, &haldata->RegReg542);
	if (res)
		return res;

	res = rtw_read8(Adapter, REG_CR + 1, &haldata->RegCR_1);
	if (res)
		return res;

	return 0;
}

static void _BeaconFunctionEnable(struct adapter *Adapter)
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

static void _InitAntenna_Selection(struct adapter *Adapter)
{
	struct hal_data_8188e *haldata = &Adapter->haldata;
	int res;
	u32 reg;

	if (haldata->AntDivCfg == 0)
		return;

	res = rtw_read32(Adapter, REG_LEDCFG0, &reg);
	if (res)
		return;

	rtw_write32(Adapter, REG_LEDCFG0, reg | BIT(23));
	rtl8188e_PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, BIT(13), 0x01);

	if (rtl8188e_PHY_QueryBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, 0x300) == Antenna_A)
		haldata->CurAntenna = Antenna_A;
	else
		haldata->CurAntenna = Antenna_B;
}

static void hw_var_set_macaddr(struct adapter *Adapter, u8 *val)
{
	u8 idx = 0;
	u32 reg_macid;

	reg_macid = REG_MACID;

	for (idx = 0; idx < 6; idx++)
		rtw_write8(Adapter, (reg_macid + idx), val[idx]);
}

u32 rtl8188eu_hal_init(struct adapter *Adapter)
{
	u8 value8 = 0;
	u16  value16;
	u32 status = _SUCCESS;
	int res;
	struct hal_data_8188e *haldata = &Adapter->haldata;
	struct pwrctrl_priv		*pwrctrlpriv = &Adapter->pwrctrlpriv;
	struct registry_priv	*pregistrypriv = &Adapter->registrypriv;
	u32 reg;

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

	/*  2010/08/09 MH We need to check if we need to turnon or off RF after detecting */
	/*  HW GPIO pin. Before PHY_RFConfig8192C. */
	/*  2010/08/26 MH If Efuse does not support sective suspend then disable the function. */

	_InitQueueReservedPage(Adapter);
	_InitQueuePriority(Adapter);
	_InitPageBoundary(Adapter);
	_InitTransferPageSize(Adapter);

	_InitTxBufferBoundary(Adapter, 0);

	status = rtl8188e_firmware_download(Adapter);

	if (status != _SUCCESS) {
		Adapter->bFWReady = false;
		haldata->fw_ractrl = false;
		return status;
	} else {
		Adapter->bFWReady = true;
		haldata->fw_ractrl = false;
	}
	/* Initialize firmware vars */
	Adapter->pwrctrlpriv.bFwCurrentInPSMode = false;
	haldata->LastHMEBoxNum = 0;

	status = PHY_MACConfig8188E(Adapter);
	if (status == _FAIL)
		goto exit;

	/*  */
	/* d. Initialize BB related configurations. */
	/*  */
	status = PHY_BBConfig8188E(Adapter);
	if (status == _FAIL)
		goto exit;

	status = phy_RF6052_Config_ParaFile(Adapter);
	if (status == _FAIL)
		goto exit;

	status = rtl8188e_iol_efuse_patch(Adapter);
	if (status == _FAIL)
		goto exit;

	_InitTxBufferBoundary(Adapter, TX_PAGE_BOUNDARY_88E);

	status =  InitLLTTable(Adapter, TX_PAGE_BOUNDARY_88E);
	if (status == _FAIL)
		goto exit;

	/*  Get Rx PHY status in order to report RSSI and others. */
	_InitDriverInfoSize(Adapter, DRVINFO_SZ);

	_InitInterrupt(Adapter);
	hw_var_set_macaddr(Adapter, Adapter->eeprompriv.mac_addr);
	_InitNetworkType(Adapter);/* set msr */
	_InitWMACSetting(Adapter);
	_InitAdaptiveCtrl(Adapter);
	_InitEDCA(Adapter);
	_InitRetryFunction(Adapter);
	InitUsbAggregationSetting(Adapter);
	_InitBeaconParameters(Adapter);

	/*  */
	/*  Init CR MACTXEN, MACRXEN after setting RxFF boundary REG_TRXFF_BNDY to patch */
	/*  Hw bug which Hw initials RxFF boundary size to a value which is larger than the real Rx buffer size in 88E. */
	/*  */
	/*  Enable MACTXEN/MACRXEN block */
	res = rtw_read16(Adapter, REG_CR, &value16);
	if (res)
		return _FAIL;

	value16 |= (MACTXEN | MACRXEN);
	rtw_write8(Adapter, REG_CR, value16);

	/* Enable TX Report */
	/* Enable Tx Report Timer */
	res = rtw_read8(Adapter, REG_TX_RPT_CTRL, &value8);
	if (res)
		return _FAIL;

	rtw_write8(Adapter,  REG_TX_RPT_CTRL, (value8 | BIT(1) | BIT(0)));
	/* Set MAX RPT MACID */
	rtw_write8(Adapter,  REG_TX_RPT_CTRL + 1, 2);/* FOR sta mode ,0: bc/mc ,1:AP */
	/* Tx RPT Timer. Unit: 32us */
	rtw_write16(Adapter, REG_TX_RPT_TIME, 0xCdf0);

	rtw_write8(Adapter, REG_EARLY_MODE_CONTROL, 0);

	rtw_write16(Adapter, REG_PKT_VO_VI_LIFE_TIME, 0x0400);	/*  unit: 256us. 256ms */
	rtw_write16(Adapter, REG_PKT_BE_BK_LIFE_TIME, 0x0400);	/*  unit: 256us. 256ms */

	/* Keep RfRegChnlVal for later use. */
	haldata->RfRegChnlVal = rtl8188e_PHY_QueryRFReg(Adapter, RF_CHNLBW, bRFRegOffsetMask);

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
	res = rtw_read16(Adapter, REG_TXDMA_OFFSET_CHK, &value16);
	if (res)
		return _FAIL;

	rtw_write16(Adapter, REG_TXDMA_OFFSET_CHK, (value16 | DROP_DATA_EN));

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
	res = rtw_read32(Adapter, REG_FWHW_TXQ_CTRL, &reg);
	if (res)
		return _FAIL;

	rtw_write32(Adapter, REG_FWHW_TXQ_CTRL, reg | BIT(12));

exit:
	return status;
}

static void CardDisableRTL8188EU(struct adapter *Adapter)
{
	u8 val8;
	struct hal_data_8188e *haldata = &Adapter->haldata;
	int res;

	/* Stop Tx Report Timer. 0x4EC[Bit1]=b'0 */
	res = rtw_read8(Adapter, REG_TX_RPT_CTRL, &val8);
	if (res)
		return;

	rtw_write8(Adapter, REG_TX_RPT_CTRL, val8 & (~BIT(1)));

	/*  stop rx */
	rtw_write8(Adapter, REG_CR, 0x0);

	/*  Run LPS WL RFOFF flow */
	HalPwrSeqCmdParsing(Adapter, LPS_ENTER_FLOW);

	/*  2. 0x1F[7:0] = 0		turn off RF */

	res = rtw_read8(Adapter, REG_MCUFWDL, &val8);
	if (res)
		return;

	if ((val8 & RAM_DL_SEL) && Adapter->bFWReady) { /* 8051 RAM code */
		/*  Reset MCU 0x2[10]=0. */
		res = rtw_read8(Adapter, REG_SYS_FUNC_EN + 1, &val8);
		if (res)
			return;

		val8 &= ~BIT(2);	/*  0x2[10], FEN_CPUEN */
		rtw_write8(Adapter, REG_SYS_FUNC_EN + 1, val8);
	}

	/*  reset MCU ready status */
	rtw_write8(Adapter, REG_MCUFWDL, 0);

	/* YJ,add,111212 */
	/* Disable 32k */
	res = rtw_read8(Adapter, REG_32K_CTRL, &val8);
	if (res)
		return;

	rtw_write8(Adapter, REG_32K_CTRL, val8 & (~BIT(0)));

	/*  Card disable power action flow */
	HalPwrSeqCmdParsing(Adapter, DISABLE_FLOW);

	/*  Reset MCU IO Wrapper */
	res = rtw_read8(Adapter, REG_RSV_CTRL + 1, &val8);
	if (res)
		return;

	rtw_write8(Adapter, REG_RSV_CTRL + 1, (val8 & (~BIT(3))));

	res = rtw_read8(Adapter, REG_RSV_CTRL + 1, &val8);
	if (res)
		return;

	rtw_write8(Adapter, REG_RSV_CTRL + 1, val8 | BIT(3));

	/* YJ,test add, 111207. For Power Consumption. */
	res = rtw_read8(Adapter, GPIO_IN, &val8);
	if (res)
		return;

	rtw_write8(Adapter, GPIO_OUT, val8);
	rtw_write8(Adapter, GPIO_IO_SEL, 0xFF);/* Reg0x46 */

	res = rtw_read8(Adapter, REG_GPIO_IO_SEL, &val8);
	if (res)
		return;

	rtw_write8(Adapter, REG_GPIO_IO_SEL, (val8 << 4));
	res = rtw_read8(Adapter, REG_GPIO_IO_SEL + 1, &val8);
	if (res)
		return;

	rtw_write8(Adapter, REG_GPIO_IO_SEL + 1, val8 | 0x0F);/* Reg0x43 */
	rtw_write32(Adapter, REG_BB_PAD_CTRL, 0x00080808);/* set LNA ,TRSW,EX_PA Pin to output mode */
	haldata->bMacPwrCtrlOn = false;
	Adapter->bFWReady = false;
}

u32 rtl8188eu_hal_deinit(struct adapter *Adapter)
{
	rtw_write32(Adapter, REG_HIMR_88E, IMR_DISABLED_88E);
	rtw_write32(Adapter, REG_HIMRE_88E, IMR_DISABLED_88E);

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
	struct eeprom_priv *eeprom = &adapt->eeprompriv;

	if (AutoLoadFail) {
		eth_random_addr(eeprom->mac_addr);
	} else {
		/* Read Permanent MAC address */
		memcpy(eeprom->mac_addr, &hwinfo[EEPROM_MAC_ADDR_88EU], ETH_ALEN);
	}
}

int ReadAdapterInfo8188EU(struct adapter *Adapter)
{
	struct eeprom_priv *eeprom = &Adapter->eeprompriv;
	struct led_priv *ledpriv = &Adapter->ledpriv;
	u8 *efuse_buf;
	u8 eeValue;
	int res;

	/* check system boot selection */
	res = rtw_read8(Adapter, REG_9346CR, &eeValue);
	if (res)
		return res;

	eeprom->bautoload_fail_flag	= !(eeValue & EEPROM_EN);

	efuse_buf = kmalloc(EFUSE_MAP_LEN_88E, GFP_KERNEL);
	if (!efuse_buf)
		return -ENOMEM;
	memset(efuse_buf, 0xFF, EFUSE_MAP_LEN_88E);

	if (!(eeValue & BOOT_FROM_EEPROM) && !eeprom->bautoload_fail_flag) {
		rtl8188e_EfusePowerSwitch(Adapter, true);
		rtl8188e_ReadEFuse(Adapter, EFUSE_MAP_LEN_88E, efuse_buf);
		rtl8188e_EfusePowerSwitch(Adapter, false);
	}

	/* parse the eeprom/efuse content */
	Hal_EfuseParseIDCode88E(Adapter, efuse_buf);
	Hal_EfuseParseMACAddr_8188EU(Adapter, efuse_buf, eeprom->bautoload_fail_flag);

	Hal_ReadPowerSavingMode88E(Adapter, efuse_buf, eeprom->bautoload_fail_flag);
	Hal_ReadTxPowerInfo88E(Adapter, efuse_buf, eeprom->bautoload_fail_flag);
	rtl8188e_EfuseParseChnlPlan(Adapter, efuse_buf, eeprom->bautoload_fail_flag);
	Hal_EfuseParseXtal_8188E(Adapter, efuse_buf, eeprom->bautoload_fail_flag);
	Hal_ReadAntennaDiversity88E(Adapter, efuse_buf, eeprom->bautoload_fail_flag);
	Hal_ReadThermalMeter_88E(Adapter, efuse_buf, eeprom->bautoload_fail_flag);

	ledpriv->bRegUseLed = true;
	kfree(efuse_buf);
	return 0;
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

	mask &= rate_bitmap;

	init_rate = get_highest_rate_idx(mask) & 0x3f;

	if (haldata->fw_ractrl) {
		mask |= ((raid << 28) & 0xf0000000);
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
	int res;
	u8 reg;
	/* reset TSF, enable update TSF, correcting TSF On Beacon */

	/* BCN interval */
	rtw_write16(adapt, REG_BCN_INTERVAL, pmlmeinfo->bcn_interval);
	rtw_write8(adapt, REG_ATIMWND, 0x02);/*  2ms */

	_InitBeaconParameters(adapt);

	rtw_write8(adapt, REG_SLOT, 0x09);

	res = rtw_read32(adapt, REG_TCR, &value32);
	if (res)
		return;

	value32 &= ~TSFRST;
	rtw_write32(adapt,  REG_TCR, value32);

	value32 |= TSFRST;
	rtw_write32(adapt, REG_TCR, value32);

	/*  NOTE: Fix test chip's bug (about contention windows's randomness) */
	rtw_write8(adapt,  REG_RXTSF_OFFSET_CCK, 0x50);
	rtw_write8(adapt, REG_RXTSF_OFFSET_OFDM, 0x50);

	_BeaconFunctionEnable(adapt);

	rtw_resume_tx_beacon(adapt);

	res = rtw_read8(adapt, bcn_ctrl_reg, &reg);
	if (res)
		return;

	rtw_write8(adapt, bcn_ctrl_reg, reg | BIT(1));
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
	haldata->odmpriv.RFCalibrateInfo.ThermalValue_HP_index = 0;
	for (i = 0; i < HP_THERMAL_NUM; i++)
		haldata->odmpriv.RFCalibrateInfo.ThermalValue_HP[i] = 0;
}
