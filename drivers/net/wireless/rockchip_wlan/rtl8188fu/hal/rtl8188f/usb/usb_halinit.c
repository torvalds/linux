/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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

#define _USB_HALINIT_C_

#include <rtl8188f_hal.h>
#ifdef CONFIG_WOWLAN
#include "hal_com_h2c.h"
#endif



static void _dbg_dump_macreg(_adapter *padapter)
{
	u32 offset = 0;
	u32 val32 = 0;
	u32 index = 0;

	for (index = 0; index < 64; index++) {
		offset = index * 4;
		val32 = rtw_read32(padapter, offset);
		DBG_8192C("offset : 0x%02x ,val:0x%08x\n", offset, val32);
	}
}

static VOID
_ConfigChipOutEP_8188F(
	IN	PADAPTER	pAdapter,
	IN	u8		NumOutPipe
)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);


	pHalData->OutEpQueueSel = 0;
	pHalData->OutEpNumber	= 0;

	switch (NumOutPipe) {
	case 4:
		pHalData->OutEpQueueSel = TX_SELE_HQ | TX_SELE_LQ | TX_SELE_NQ;
		pHalData->OutEpNumber = 4;
		break;
	case 3:
		pHalData->OutEpQueueSel = TX_SELE_HQ | TX_SELE_LQ | TX_SELE_NQ;
		pHalData->OutEpNumber = 3;
		break;
	case 2:
		pHalData->OutEpQueueSel = TX_SELE_HQ | TX_SELE_NQ;
		pHalData->OutEpNumber = 2;
		break;
	case 1:
		pHalData->OutEpQueueSel = TX_SELE_HQ;
		pHalData->OutEpNumber = 1;
		break;
	default:
		break;

	}
	DBG_871X("%s OutEpQueueSel(0x%02x), OutEpNumber(%d)\n", __func__, pHalData->OutEpQueueSel, pHalData->OutEpNumber);

}

static BOOLEAN HalUsbSetQueuePipeMapping8188FUsb(
	IN	PADAPTER	pAdapter,
	IN	u8		NumInPipe,
	IN	u8		NumOutPipe
)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);
	BOOLEAN			result		= _FALSE;

	_ConfigChipOutEP_8188F(pAdapter, NumOutPipe);

	/* Normal chip with one IN and one OUT doesn't have interrupt IN EP. */
	if (1 == pHalData->OutEpNumber) {
		if (1 != NumInPipe)
			return result;
	}

	/* All config other than above support one Bulk IN and one Interrupt IN. */
	/*if(2 != NumInPipe){ */
	/*	return result; */
	/*} */

	result = Hal_MappingOutPipe(pAdapter, NumOutPipe);

	return result;

}

void rtl8188fu_interface_configure(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);

	if (IS_HIGH_SPEED_USB(padapter)) {
		/* HIGH SPEED */
		pHalData->UsbBulkOutSize = USB_HIGH_SPEED_BULK_SIZE;/*512 bytes */
	} else {
		/* FULL SPEED */
		pHalData->UsbBulkOutSize = USB_FULL_SPEED_BULK_SIZE;/*64 bytes */
	}

	pHalData->interfaceIndex = pdvobjpriv->InterfaceNumber;

#ifdef CONFIG_USB_TX_AGGREGATION
	pHalData->UsbTxAggMode		= 1;
	pHalData->UsbTxAggDescNum	= 0x6;	/* only 4 bits */
#endif

#ifdef CONFIG_USB_RX_AGGREGATION
	pHalData->UsbRxAggMode		= USB_RX_AGG_USB;
	pHalData->UsbRxAggBlockCount	= 0x5; /* unit: 4KB */
	pHalData->UsbRxAggBlockTimeout	= 0x20; /* unit: 32us */
	pHalData->UsbRxAggPageCount	= 0xF; /* uint: 1KB */
	pHalData->UsbRxAggPageTimeout	= 0x20; /* unit: 32us */
#endif

	HalUsbSetQueuePipeMapping8188FUsb(padapter,
									  pdvobjpriv->RtNumInPipes, pdvobjpriv->RtNumOutPipes);

}

static u32 _InitPowerOn_8188FU(PADAPTER padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct registry_priv *regsty = dvobj_to_regsty(dvobj);
	u32 		status = _SUCCESS;
	u16			value16 = 0;
	u8			value8 = 0;
	u32 value32;

	/* check to apply user defined pll_ref_clk_sel */
	if ((regsty->pll_ref_clk_sel & 0x0F) != 0x0F)
		rtl8188f_set_pll_ref_clk_sel(padapter, regsty->pll_ref_clk_sel);

	/* HW Power on sequence */
	if (!HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, rtl8188F_card_enable_flow))
		return _FAIL;

	/* Enable MAC DMA/WMAC/SCHEDULE/SEC block */
	/* Set CR bit10 to enable 32k calibration. Suggested by SD1 Gimmy. Added by tynli. 2011.08.31. */
	rtw_write8(padapter, REG_CR_8188F, 0x00);  /*suggseted by zhouzhou, by page, 20111230 */
	value16 = rtw_read16(padapter, REG_CR_8188F);
	value16 |= (HCI_TXDMA_EN | HCI_RXDMA_EN | TXDMA_EN | RXDMA_EN
				| PROTOCOL_EN | SCHEDULE_EN | ENSEC | CALTMR_EN);
	rtw_write16(padapter, REG_CR_8188F, value16);

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_PowerOnSetting(padapter);

	/* external switch to S1 */
	/* 0x38[11] = 0x1 */
	/* 0x4c[23] = 0x1 */
	/* 0x64[0] = 0 */
	value16 = rtw_read16(padapter, REG_PWR_DATA);
	/* Switch the control of EESK, EECS to RFC for DPDT or Antenna switch */
	value16 |= BIT(11); /* BIT_EEPRPAD_RFE_CTRL_EN */
	rtw_write16(padapter, REG_PWR_DATA, value16);
	/*DBG_8192C("%s: REG_PWR_DATA(0x%x)=0x%04X\n", __func__, REG_PWR_DATA, rtw_read16(padapter, REG_PWR_DATA)); */

	value32 = rtw_read32(padapter, REG_LEDCFG0);
	value32 |= BIT(23); /* DPDT_SEL_EN, 1 for SW control */
	rtw_write32(padapter, REG_LEDCFG0, value32);
	/*DBG_8192C("%s: REG_LEDCFG0(0x%x)=0x%08X\n", __func__, REG_LEDCFG0, rtw_read32(padapter, REG_LEDCFG0)); */

	value8 = rtw_read8(padapter, REG_PAD_CTRL1_8188F);
	value8 &= ~BIT(0); /* BIT_SW_DPDT_SEL_DATA, DPDT_SEL default configuration */
	rtw_write8(padapter, REG_PAD_CTRL1_8188F, value8);
	/*DBG_8192C("%s: REG_PAD_CTRL1(0x%x)=0x%02X\n", __func__, REG_PAD_CTRL1_8188F, rtw_read8(padapter, REG_PAD_CTRL1_8188F)); */
#endif /* CONFIG_BT_COEXIST */

	return status;
}



/*------------------------------------------------------------------------- */
/* */
/* LLT R/W/Init function */
/* */
/*------------------------------------------------------------------------- */
static u8 _LLTWrite(
	IN  PADAPTER	Adapter,
	IN	u32		address,
	IN	u32		data
)
{
	u8	status = _SUCCESS;
	s8 	count = POLLING_LLT_THRESHOLD;
	u32 	value = _LLT_INIT_ADDR(address) | _LLT_INIT_DATA(data) | _LLT_OP(_LLT_WRITE_ACCESS);

	rtw_write32(Adapter, REG_LLT_INIT, value);

	/*polling */
	do {
		value = rtw_read32(Adapter, REG_LLT_INIT);
		if (_LLT_NO_ACTIVE == _LLT_OP_VALUE(value))
			break;
	} while (--count);

	if (count <= 0) {
		DBG_871X("Failed to polling write LLT done at address %d!\n", address);
		status = _FAIL;
	}
	return status;

}



/*--------------------------------------------------------------- */
/* */
/*	MAC init functions */
/* */
/*--------------------------------------------------------------- */
/*
 * USB has no hardware interrupt,
 * so no need to initialize HIMR.
 */
static void _InitInterrupt(PADAPTER padapter)
{
#ifdef CONFIG_SUPPORT_USB_INT
	/* clear interrupt, write 1 clear */
	rtw_write32(padapter, REG_HISR0_8188F, 0xFFFFFFFF);
	rtw_write32(padapter, REG_HISR1_8188F, 0xFFFFFFFF);
#endif /* CONFIG_SUPPORT_USB_INT */
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
	BOOLEAN		bWiFiConfig	= pregistrypriv->wifi_spec;

	if (pHalData->OutEpQueueSel & TX_SELE_HQ)
		numHQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_HPQ_8188F : NORMAL_PAGE_NUM_HPQ_8188F;

	if (pHalData->OutEpQueueSel & TX_SELE_LQ)
		numLQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_LPQ_8188F : NORMAL_PAGE_NUM_LPQ_8188F;

	/* NOTE: This step shall be proceed before writing REG_RQPN. */
	if (pHalData->OutEpQueueSel & TX_SELE_NQ)
		numNQ = bWiFiConfig ? WMM_NORMAL_PAGE_NUM_NPQ_8188F : NORMAL_PAGE_NUM_NPQ_8188F;
	value8 = (u8)_NPQ(numNQ);
	rtw_write8(padapter, REG_RQPN_NPQ, value8);

	numPubQ = TX_TOTAL_PAGE_NUMBER_8188F - numHQ - numLQ - numNQ;

	/* TX DMA */
	value32 = _HPQ(numHQ) | _LPQ(numLQ) | _PUBQ(numPubQ) | LD_RQPN;
	rtw_write32(padapter, REG_RQPN, value32);

}

static void _InitTxBufferBoundary(PADAPTER padapter)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
#ifdef CONFIG_CONCURRENT_MODE
	u8 val8;
#endif /* CONFIG_CONCURRENT_MODE */

	/*u16	txdmactrl; */
	u8	txpktbuf_bndy;

	if (!pregistrypriv->wifi_spec)
		txpktbuf_bndy = TX_PAGE_BOUNDARY_8188F;
	else {
		/*for WMM */
		txpktbuf_bndy = WMM_NORMAL_TX_PAGE_BOUNDARY_8188F;
	}

	rtw_write8(padapter, REG_TXPKTBUF_BCNQ_BDNY_8188F, txpktbuf_bndy);
	rtw_write8(padapter, REG_TXPKTBUF_MGQ_BDNY_8188F, txpktbuf_bndy);
	rtw_write8(padapter, REG_TXPKTBUF_WMAC_LBK_BF_HD_8188F, txpktbuf_bndy);
	rtw_write8(padapter, REG_TRXFF_BNDY, txpktbuf_bndy);
	rtw_write8(padapter, REG_TDECTRL + 1, txpktbuf_bndy);

#ifdef CONFIG_CONCURRENT_MODE
	val8 = txpktbuf_bndy + 8;
	rtw_write8(padapter, REG_BCNQ1_BDNY, val8);
	rtw_write8(padapter, REG_DWBCN1_CTRL_8188F + 1, val8); /* BCN1_HEAD */

	val8 = rtw_read8(padapter, REG_DWBCN1_CTRL_8188F + 2);
	val8 |= BIT(1); /* BIT1- BIT_SW_BCN_SEL_EN */
	rtw_write8(padapter, REG_DWBCN1_CTRL_8188F + 2, val8);
#endif /* CONFIG_CONCURRENT_MODE */
}


VOID
_InitTransferPageSize_8188fu(
	PADAPTER Adapter
)
{

	u1Byte	value8;
	value8 = _PSRX(PBP_256) | _PSTX(PBP_256);

	rtw_write8(Adapter, REG_PBP_8188F, value8);
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
	u16 value16		= (rtw_read16(Adapter, REG_TRXDMA_CTRL_8188F) & 0x7);

	value16 |=	_TXDMA_BEQ_MAP(beQ) 	| _TXDMA_BKQ_MAP(bkQ) |
				_TXDMA_VIQ_MAP(viQ) 	| _TXDMA_VOQ_MAP(voQ) |
				_TXDMA_MGQ_MAP(mgtQ) | _TXDMA_HIQ_MAP(hiQ);

	rtw_write16(Adapter, REG_TRXDMA_CTRL_8188F, value16);
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
		/*RT_ASSERT(FALSE,("Shall not reach here!\n")); */
		break;
	}

	if (!pregistrypriv->wifi_spec) {
		beQ		= valueLow;
		bkQ		= valueLow;
		viQ		= valueHi;
		voQ		= valueHi;
		mgtQ	= valueHi;
		hiQ		= valueHi;
	} else { /*for WMM ,CONFIG_OUT_EP_WIFI_MODE */
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
		hiQ			= QUEUE_HIGH;
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

static void _InitQueuePriority(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	switch (pHalData->OutEpNumber) {
	case 2:
		_InitNormalChipTwoOutEpPriority(padapter);
		break;
	case 3:
	case 4:
		_InitNormalChipThreeOutEpPriority(padapter);
		break;
	default:
		/*RT_ASSERT(FALSE,("Shall not reach here!\n")); */
		break;
	}

}


static void _InitPageBoundary(PADAPTER padapter)
{
	/* RX Page Boundary */
	u16 rxff_bndy = RX_DMA_BOUNDARY_8188F;

	rtw_write16(padapter, (REG_TRXFF_BNDY + 2), rxff_bndy);

	/* TODO: ?? shall we set tx boundary? */
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

	/* TODO: use the other function to set network type */
#if 0/*RTL8191C_FPGA_NETWORKTYPE_ADHOC */
	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AD_HOC);
#else
	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AP);
#endif
	rtw_write32(Adapter, REG_CR, value32);
/*	RASSERT(pIoBase->rtw_read8(REG_CR + 2) == 0x2); */
}


static VOID
_InitDriverInfoSize(
	IN  PADAPTER	Adapter,
	IN	u8		drvInfoSize
)
{
	rtw_write8(Adapter, REG_RX_DRVINFO_SZ, drvInfoSize);
}

static VOID
_InitWMACSetting(
	IN  PADAPTER Adapter
)
{
	/*u4Byte			value32; */
	u16			value16;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

/*	pHalData->ReceiveConfig = AAP | APM | AM |AB  |ADD3|APWRMGT| APP_ICV | APP_MIC |APP_FCS|ADF |ACF|AMF|HTC_LOC_CTRL|APP_PHYSTS; */
/*	pHalData->ReceiveConfig = AAP | APM | AM | AB | ADD3 | APWRMGT | APP_ICV | APP_MIC | ADF | ACF | AMF | HTC_LOC_CTRL | APP_PHYSTS; */
	pHalData->ReceiveConfig = RCR_APM | RCR_AM | RCR_AB | RCR_CBSSID_DATA | RCR_CBSSID_BCN | RCR_APP_ICV | RCR_AMF | RCR_HTC_LOC_CTRL | RCR_APP_MIC | RCR_APP_PHYST_RXFF;

	rtw_write32(Adapter, REG_RCR, pHalData->ReceiveConfig);

	/* Accept all data frames */
	value16 = 0xFFFF;
	rtw_write16(Adapter, REG_RXFLTMAP2_8188F, value16);

	/* 2010.09.08 hpfan */
	/* Since ADF is removed from RCR, ps-poll will not be indicate to driver, */
	/* RxFilterMap should mask ps-poll to gurantee AP mode can rx ps-poll. */

	value16 = 0x400;
	rtw_write16(Adapter, REG_RXFLTMAP1_8188F, value16);

	/* Accept all management frames */
	value16 = 0xFFFF;
	rtw_write16(Adapter, REG_RXFLTMAP0_8188F, value16);

}

static VOID
_InitAdaptiveCtrl(
	IN  PADAPTER Adapter
)
{
	u16	value16;
	u32	value32;

	/* Response Rate Set */
	value32 = rtw_read32(Adapter, REG_RRSR);
	value32 &= ~RATE_BITMAP_ALL;
	value32 |= RATE_RRSR_CCK_ONLY_1M;
	rtw_write32(Adapter, REG_RRSR, value32);

	/* CF-END Threshold */
	/*m_spIoBase->rtw_write8(REG_CFEND_TH, 0x1); */

	/* SIFS (used in NAV) */
	value16 = _SPEC_SIFS_CCK(0x10) | _SPEC_SIFS_OFDM(0x10);
	rtw_write16(Adapter, REG_SPEC_SIFS, value16);

	/* Retry Limit */
	value16 = _LRL(0x30) | _SRL(0x30);
	rtw_write16(Adapter, REG_RL, value16);

}

static VOID
_InitRateFallback(
	IN  PADAPTER Adapter
)
{
	/* Set Data Auto Rate Fallback Retry Count register. */
	rtw_write32(Adapter, REG_DARFRC, 0x00000000);
	rtw_write32(Adapter, REG_DARFRC + 4, 0x10080404);
	rtw_write32(Adapter, REG_RARFRC, 0x04030201);
	rtw_write32(Adapter, REG_RARFRC + 4, 0x08070605);

}


static VOID
_InitEDCA(
	IN  PADAPTER Adapter
)
{
	/* Set Spec SIFS (used in NAV) */
	rtw_write16(Adapter, REG_SPEC_SIFS, 0x100a);
	rtw_write16(Adapter, REG_MAC_SPEC_SIFS, 0x100a);

	/* Set SIFS for CCK */
	rtw_write16(Adapter, REG_SIFS_CTX, 0x100a);

	/* Set SIFS for OFDM */
	rtw_write16(Adapter, REG_SIFS_TRX, 0x100a);

	/* TXOP */
	rtw_write32(Adapter, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtw_write32(Adapter, REG_EDCA_BK_PARAM, 0x0000A44F);
	rtw_write32(Adapter, REG_EDCA_VI_PARAM, 0x005EA324);
	rtw_write32(Adapter, REG_EDCA_VO_PARAM, 0x002FA226);
}

#ifdef CONFIG_LED
static void _InitHWLed(PADAPTER Adapter)
{
	struct led_priv *pledpriv = &(Adapter->ledpriv);

	if (pledpriv->LedStrategy != HW_LED)
		return;

/* HW led control */
/* to do .... */
/*must consider cases of antenna diversity/ commbo card/solo card/mini card */

}
#endif /*CONFIG_LED */

static VOID
_InitRDGSetting_8188fu(
	IN	PADAPTER Adapter
)
{
	rtw_write8(Adapter, REG_RD_CTRL_8188F, 0xFF);
	rtw_write16(Adapter, REG_RD_NAV_NXT_8188F, 0x200);
	rtw_write8(Adapter, REG_RD_RESP_PKT_TH_8188F, 0x05);
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

	/* Set ACK timeout */
	rtw_write8(Adapter, REG_ACKTO, 0x40);
}

static void _InitBurstPktLen(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u8 tmp8;


	tmp8 = rtw_read8(padapter, REG_RXDMA_PRO_8188F);
	tmp8 &= ~(BIT(4) | BIT(5));
	switch (pHalData->UsbBulkOutSize) {
	case USB_HIGH_SPEED_BULK_SIZE:
		tmp8 |= BIT(4); /* set burst pkt len=512B */
		break;
	case USB_FULL_SPEED_BULK_SIZE:
	default:
		tmp8 |= BIT(5); /* set burst pkt len=64B */
		break;
	}
	tmp8 |= BIT(1) | BIT(2) | BIT(3);
	rtw_write8(padapter, REG_RXDMA_PRO_8188F, tmp8);

	pHalData->bSupportUSB3 = _FALSE;

	tmp8 = rtw_read8(padapter, REG_HT_SINGLE_AMPDU_8188F);
	tmp8 |= BIT(7); /* enable single pkt ampdu */
	rtw_write8(padapter, REG_HT_SINGLE_AMPDU_8188F, tmp8);
	rtw_write16(padapter, REG_MAX_AGGR_NUM_8188F, 0x0C14);
	rtw_write8(padapter, REG_AMPDU_MAX_TIME_8188F, 0x70);
	rtw_write32(padapter, REG_AMPDU_MAX_LENGTH_8188F, 0xffffffff);
	if (pHalData->AMPDUBurstMode)
		rtw_write8(padapter, REG_AMPDU_BURST_MODE_8188F, 0x5F);

	/* for VHT packet length 11K */
	rtw_write8(padapter, REG_RX_PKT_LIMIT_8188F, 0x18);

	rtw_write8(padapter, REG_PIFS_8188F, 0x00);
	rtw_write8(padapter, REG_FWHW_TXQ_CTRL_8188F, 0x80);
	rtw_write32(padapter, REG_FAST_EDCA_CTRL_8188F, 0x03086666);
	rtw_write8(padapter, REG_USTIME_TSF_8188F, 0x28);
	rtw_write8(padapter, REG_USTIME_EDCA_8188F, 0x28);

	/* to prevent mac is reseted by bus. 20111208, by Page */
	tmp8 = rtw_read8(padapter, REG_RSV_CTRL_8188F);
	tmp8 |= BIT(5) | BIT(6);
	rtw_write8(padapter, REG_RSV_CTRL_8188F, tmp8);
}

/*-----------------------------------------------------------------------------
 * Function:	usb_AggSettingTxUpdate()
 *
 * Overview:	Separate TX/RX parameters update independent for TP detection and
 *			dynamic TX/RX aggreagtion parameters update.
 *
 * Input:			PADAPTER
 *
 * Output/Return:	NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	12/10/2010	MHC		Separate to smaller function.
 *
 *---------------------------------------------------------------------------*/
static VOID
usb_AggSettingTxUpdate(
	IN	PADAPTER			Adapter
)
{
#ifdef CONFIG_USB_TX_AGGREGATION
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	/*PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo); */
	u32			value32;

	if (Adapter->registrypriv.wifi_spec)
		pHalData->UsbTxAggMode = _FALSE;

	if (pHalData->UsbTxAggMode) {
		value32 = rtw_read32(Adapter, REG_DWBCN0_CTRL_8188F);
		value32 = value32 & ~(BLK_DESC_NUM_MASK << BLK_DESC_NUM_SHIFT);
		value32 |= ((pHalData->UsbTxAggDescNum & BLK_DESC_NUM_MASK) << BLK_DESC_NUM_SHIFT);

		rtw_write32(Adapter, REG_DWBCN0_CTRL_8188F, value32);
		rtw_write8(Adapter, REG_DWBCN1_CTRL_8188F, pHalData->UsbTxAggDescNum << 1);
	}

#endif
}	/* usb_AggSettingTxUpdate */


/*-----------------------------------------------------------------------------
 * Function:	usb_AggSettingRxUpdate()
 *
 * Overview:	Separate TX/RX parameters update independent for TP detection and
 *			dynamic TX/RX aggreagtion parameters update.
 *
 * Input:			PADAPTER
 *
 * Output/Return:	NONE
 *
 *---------------------------------------------------------------------------*/
static void usb_AggSettingRxUpdate(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	u8 aggctrl;
	u32 aggrx, rxdmamode;


	pHalData = GET_HAL_DATA(padapter);

	aggctrl = rtw_read8(padapter, REG_TRXDMA_CTRL);
	aggctrl &= ~RXDMA_AGG_EN;

	aggrx = rtw_read32(padapter, REG_RXDMA_AGG_PG_TH);
	aggrx &= ~BIT_USB_RXDMA_AGG_EN;
	aggrx &= ~0xFF0F; /* reset agg size and timeout */

	rxdmamode = rtw_read8(padapter, REG_RXDMA_MODE);

#ifdef CONFIG_USB_RX_AGGREGATION
	switch (pHalData->UsbRxAggMode) {
	case USB_RX_AGG_DMA:
		aggctrl |= RXDMA_AGG_EN;
		aggrx |= BIT_USB_RXDMA_AGG_EN;
		aggrx |= (pHalData->UsbRxAggPageCount & 0xF);
		aggrx |= (pHalData->UsbRxAggPageTimeout << 8);
		rxdmamode |= BIT_DMA_MODE;
		DBG_8192C("%s: RX Aggregation DMA mode, size=%dKB, timeout=%dus\n",
				  __func__, pHalData->UsbRxAggPageCount & 0xF, pHalData->UsbRxAggPageTimeout * 32);
		break;

	case USB_RX_AGG_USB:
	case USB_RX_AGG_MIX:
		aggctrl |= RXDMA_AGG_EN;
		aggrx &= ~BIT_USB_RXDMA_AGG_EN;
		aggrx |= (pHalData->UsbRxAggBlockCount & 0xF);
		aggrx |= (pHalData->UsbRxAggBlockTimeout << 8);
		rxdmamode |= BIT_DMA_MODE;
		DBG_8192C("%s: RX Aggregation USB mode, size=%dKB, timeout=%dus\n",
				  __func__, (pHalData->UsbRxAggBlockCount & 0xF) * 4, pHalData->UsbRxAggBlockTimeout * 32);
		break;

	case USB_RX_AGG_DISABLE:
		aggctrl &= ~RXDMA_AGG_EN;
		aggrx &= ~BIT_USB_RXDMA_AGG_EN;
		rxdmamode &= ~BIT_DMA_MODE;
	default:
		DBG_8192C("%s: RX Aggregation Disable!\n", __func__);
		break;
	}
#endif /* CONFIG_USB_RX_AGGREGATION */

	rtw_write8(padapter, REG_TRXDMA_CTRL, aggctrl);
	rtw_write32(padapter, REG_RXDMA_AGG_PG_TH, aggrx);
	rtw_write8(padapter, REG_RXDMA_MODE, rxdmamode);
}

static VOID
_initUsbAggregationSetting(
	IN  PADAPTER Adapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	/* Tx aggregation setting */
	usb_AggSettingTxUpdate(Adapter);

	/* Rx aggregation setting */
	usb_AggSettingRxUpdate(Adapter);

	/* 201/12/10 MH Add for USB agg mode dynamic switch. */
	pHalData->UsbRxHighSpeedMode = _FALSE;
}

static VOID
PHY_InitAntennaSelection8188F(
	PADAPTER Adapter
)
{
	/* TODO: <20130114, Kordan> The following setting is only for DPDT and Fixed board type. */
	/* TODO:  A better solution is configure it according EFUSE during the run-time. */
	PHY_SetMacReg(Adapter, 0x64, BIT20, 0x0);			/*0x66[4]=0 */
	PHY_SetMacReg(Adapter, 0x64, BIT24, 0x0);			/*0x66[8]=0 */
	PHY_SetMacReg(Adapter, 0x40, BIT4, 0x0);			/*0x40[4]=0 */
	PHY_SetMacReg(Adapter, 0x40, BIT3, 0x1);			/*0x40[3]=1 */
	PHY_SetMacReg(Adapter, 0x4C, BIT24, 0x1);			/*0x4C[24:23]=10 */
	PHY_SetMacReg(Adapter, 0x4C, BIT23, 0x0);			/*0x4C[24:23]=10 */
	PHY_SetBBReg(Adapter, 0x944, BIT1 | BIT0, 0x3);		/*0x944[1:0]=11 */
	PHY_SetBBReg(Adapter, 0x930, bMaskByte0, 0x77);		/*0x930[7:0]=77 */
	PHY_SetMacReg(Adapter, 0x38, BIT11, 0x1);			/*0x38[11]=1 */
}

static VOID _InitAdhocWorkaroundParams(IN PADAPTER Adapter)
{
#ifdef RTL8192CU_ADHOC_WORKAROUND_SETTING
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	pHalData->RegBcnCtrlVal = rtw_read8(Adapter, REG_BCN_CTRL);
	pHalData->RegTxPause = rtw_read8(Adapter, REG_TXPAUSE);
	pHalData->RegFwHwTxQCtrl = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL + 2);
	pHalData->RegReg542 = rtw_read8(Adapter, REG_TBTT_PROHIBIT + 2);
#endif
}

/* Set CCK and OFDM Block "ON" */
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

#define MgntActSet_RF_State(...)

enum {
	Antenna_Lfet = 1,
	Antenna_Right = 2,
};

/* */
/* 2010/08/09 MH Add for power down check. */
/* */
static BOOLEAN
HalDetectPwrDownMode(
	IN PADAPTER				Adapter
)
{
	u8	tmpvalue;
	HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(Adapter);
	struct pwrctrl_priv		*pwrctrlpriv = adapter_to_pwrctl(Adapter);

	EFUSE_ShadowRead(Adapter, 1, EEPROM_FEATURE_OPTION_8188F, (u32 *)&tmpvalue);

	/* 2010/08/25 MH INF priority > PDN Efuse value. */
	if (tmpvalue & BIT4 && pwrctrlpriv->reg_pdnmode)
		pHalData->pwrdown = _TRUE;
	else
		pHalData->pwrdown = _FALSE;

	DBG_8192C("HalDetectPwrDownMode(): PDN=%d\n", pHalData->pwrdown);
	return pHalData->pwrdown;

}	/* HalDetectPwrDownMode */


/* */
/* 2010/08/26 MH Add for selective suspend mode check. */
/* If Efuse 0x0e bit1 is not enabled, we can not support selective suspend for Minicard and */
/* slim card. */
/* */
static VOID
HalDetectSelectiveSuspendMode(
	IN PADAPTER				Adapter
)
{
#if 0   /*amyma */
	u8	tmpvalue;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(Adapter);

	/* If support HW radio detect, we need to enable WOL ability, otherwise, we */
	/* can not use FW to notify host the power state switch. */

	EFUSE_ShadowRead(Adapter, 1, EEPROM_USB_OPTIONAL1, (u32 *)&tmpvalue);

	DBG_8192C("HalDetectSelectiveSuspendMode(): SS ");
	if (tmpvalue & BIT1)
		DBG_8192C("Enable\n");
	else {
		DBG_8192C("Disable\n");
		pdvobjpriv->RegUsbSS = _FALSE;
	}

	/* 2010/09/01 MH According to Dongle Selective Suspend INF. We can switch SS mode. */
	if (pdvobjpriv->RegUsbSS && !SUPPORT_HW_RADIO_DETECT(pHalData)) {
		/*PMGNT_INFO				pMgntInfo = &(Adapter->MgntInfo); */

		/*if (!pMgntInfo->bRegDongleSS) */
		/*{ */
		/*	RT_TRACE(COMP_INIT, DBG_LOUD, ("Dongle disable SS\n")); */
		pdvobjpriv->RegUsbSS = _FALSE;
		/*} */
	}
#endif
}	/* HalDetectSelectiveSuspendMode */
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
	/*PRT_USB_DEVICE	pDevice = GET_RT_USB_DEVICE(pAdapter); */
	u16	reg = rtw_read16(pAdapter, REG_GPIO_MUXCFG);

	/*if (!pDevice->RegUsbSS) */
	{
		return;
	}

	/* */
	/* 2010/08/23 MH According to Alfred's suggestion, we need to to prevent HW */
	/* to enter suspend mode automatically. Otherwise, it will shut down major power */
	/* domain and 8051 will stop. When we try to enter selective suspend mode, we */
	/* need to prevent HW to enter D2 mode aumotmatically. Another way, Host will */
	/* issue a S10 signal to power domain. Then it will cleat SIC setting(from Yngli). */
	/* We need to enable HW suspend mode when enter S3/S4 or disable. We need */
	/* to disable HW suspend mode for IPS/radio_off. */
	/* */
	/*RT_TRACE(COMP_RF, DBG_LOUD, ("HwSuspendModeEnable92Cu = %d\n", Type)); */
	if (Type == _FALSE) {
		reg |= BIT14;
		/*RT_TRACE(COMP_RF, DBG_LOUD, ("REG_GPIO_MUXCFG = %x\n", reg)); */
		rtw_write16(pAdapter, REG_GPIO_MUXCFG, reg);
		reg |= BIT12;
		/*RT_TRACE(COMP_RF, DBG_LOUD, ("REG_GPIO_MUXCFG = %x\n", reg)); */
		rtw_write16(pAdapter, REG_GPIO_MUXCFG, reg);
	} else {
		reg &= (~BIT12);
		rtw_write16(pAdapter, REG_GPIO_MUXCFG, reg);
		reg &= (~BIT14);
		rtw_write16(pAdapter, REG_GPIO_MUXCFG, reg);
	}

}	/* HwSuspendModeEnable92Cu */
rt_rf_power_state RfOnOffDetect(IN	PADAPTER pAdapter)
{
	/*HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(pAdapter); */
	u8	val8;
	rt_rf_power_state rfpowerstate = rf_off;

	if (adapter_to_pwrctl(pAdapter)->bHWPowerdown) {
		val8 = rtw_read8(pAdapter, REG_HSISR);
		DBG_8192C("pwrdown, 0x5c(BIT7)=%02x\n", val8);
		rfpowerstate = (val8 & BIT7) ? rf_off : rf_on;
	} else { /* rf on/off */
		rtw_write8(pAdapter, REG_MAC_PINMUX_CFG, rtw_read8(pAdapter, REG_MAC_PINMUX_CFG) & ~(BIT3));
		val8 = rtw_read8(pAdapter, REG_GPIO_IO_SEL);
		DBG_8192C("GPIO_IN=%02x\n", val8);
		rfpowerstate = (val8 & BIT3) ? rf_on : rf_off;
	}
	return rfpowerstate;
}	/* HalDetectPwrDownMode */

void _ps_open_RF(_adapter *padapter);

#ifdef CONFIG_8188FTV_SOLUTION_D
/* 
Write corresponding register of efuse. Indirect Write.
Offset:	reg offset.
Value:	u8 value
*/
VOID WriteUSB2PHYReg(PADAPTER Adapter, u8 Offset, u8 Value)
{
	rtw_write8(Adapter, 0xFE41, Value);
	rtw_write8(Adapter, 0xFE40, Offset);
	rtw_write8(Adapter, 0xFE42, 0x81);
}

/* 
Read corresponding register of efuse. Indirect Read.
Offset: reg offset.
*/
u1Byte ReadUSB2PHYReg(PADAPTER Adapter, u8 Offset)
{
	u8 value = 0;
	rtw_write8(Adapter, 0xFE40, Offset);
	rtw_write8(Adapter, 0xFE42, 0x81);
	value = rtw_read8(Adapter, 0xFE43);

	return value;
}

void rtl8188fu_solution_d(PADAPTER Adapter)
{
	u8 reg_val[6] = {0};
	u16 reg0mask = BIT(10) | BIT(11) | BIT(12) | BIT(13);
	u16 reg1mask = BIT(0) | BIT(1) | BIT(2) | BIT(3);

	reg_val[0] = PHY_QueryMacReg(Adapter, 0x10, reg0mask);	/* efuse 0x3   */
	reg_val[1] = PHY_QueryMacReg(Adapter, 0xC4, reg1mask);	/* efuse 0xd   */
	reg_val[2] = ReadUSB2PHYReg(Adapter, 0xC1);				/* efuse 0x131 */
	reg_val[3] = PHY_QueryMacReg(Adapter, 0x4, BIT(11)); 	/* efuse 0xa   */
	reg_val[4] = ReadUSB2PHYReg(Adapter, 0xD2);				/* efuse 0x13a */
	reg_val[5] = ReadUSB2PHYReg(Adapter, 0xD3);				/* efuse 0x13b */

	if(!(reg_val[3] == 0 && reg_val[4] == 0 && reg_val[5] == 0x31)) /* solution "EP" not applied */
	{
		if((reg_val[0] == 0xC && reg_val[1] == 0xC && reg_val[2] == 0xAE)		/* solution O */
			||(reg_val[0] == 0xF && reg_val[1] == 0xC && reg_val[2] == 0xAE)	/* solution A */
			||(reg_val[0] == 0xC && reg_val[1] == 0xB && reg_val[2] == 0xAE)	/* solution B */
			||(reg_val[0] == 0xC && reg_val[1] == 0xC && reg_val[2] == 0xBE))	/* solution C */
		{
			/* apply solution D */
			PHY_SetMacReg(Adapter, 0x10, reg0mask, 0xF);
			PHY_SetMacReg(Adapter, 0xC4, reg1mask, 0x7);
			WriteUSB2PHYReg(Adapter, 0xE1, 0xB6);
			DBG_871X("%s, Aplly Solution D\n", __func__);
		}
		else if(reg_val[0] == 0xF && reg_val[1] == 0x7 && reg_val[2] == 0xB6)
			DBG_871X("%s, Solution D already apllied\n", __func__);
		else
			DBG_871X("%s, Unexpected efuse content\n", __func__);
	}
	else
		DBG_871X("%s, Solution EP already applied\n", __func__);
}
#endif

u32 rtl8188fu_hal_init(PADAPTER padapter)
{
	u8	value8 = 0, u1bRegCR;
	u32	boundary, status = _SUCCESS;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv		*pwrctrlpriv = adapter_to_pwrctl(padapter);
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
	rt_rf_power_state		eRfPowerStateToSet;
	u32 NavUpper = WiFiNavUpperUs;
	u32 value32;
	u32 init_start_time = rtw_get_current_time();


#ifdef DBG_HAL_INIT_PROFILING

	enum HAL_INIT_STAGES {
		HAL_INIT_STAGES_BEGIN = 0,
		HAL_INIT_STAGES_INIT_PW_ON,
		HAL_INIT_STAGES_INIT_LLTT,
		HAL_INIT_STAGES_MISC01,
		HAL_INIT_STAGES_DOWNLOAD_FW,
		HAL_INIT_STAGES_MAC,
		HAL_INIT_STAGES_BB,
		HAL_INIT_STAGES_RF,
		HAL_INIT_STAGES_MISC02,
		HAL_INIT_STAGES_TURN_ON_BLOCK,
		HAL_INIT_STAGES_INIT_SECURITY,
		HAL_INIT_STAGES_MISC11,
		/*HAL_INIT_STAGES_RF_PS, */
		HAL_INIT_STAGES_INIT_HAL_DM,
/*		HAL_INIT_STAGES_IQK, */
/*		HAL_INIT_STAGES_PW_TRACK, */
/*		HAL_INIT_STAGES_LCK, */
		HAL_INIT_STAGES_MISC21,
		/*HAL_INIT_STAGES_INIT_PABIAS, */
		HAL_INIT_STAGES_BT_COEXIST,
		/*HAL_INIT_STAGES_ANTENNA_SEL, */
		HAL_INIT_STAGES_MISC31,
		HAL_INIT_STAGES_END,
		HAL_INIT_STAGES_NUM
	};

	static char * const hal_init_stages_str[] = {
		"HAL_INIT_STAGES_BEGIN",
		"HAL_INIT_STAGES_INIT_PW_ON",
		"HAL_INIT_STAGES_INIT_LLTT",
		"HAL_INIT_STAGES_MISC01",
		"HAL_INIT_STAGES_DOWNLOAD_FW",
		"HAL_INIT_STAGES_MAC",
		"HAL_INIT_STAGES_BB",
		"HAL_INIT_STAGES_RF",
		"HAL_INIT_STAGES_MISC02",
		"HAL_INIT_STAGES_TURN_ON_BLOCK",
		"HAL_INIT_STAGES_INIT_SECURITY",
		"HAL_INIT_STAGES_MISC11",
		/*"HAL_INIT_STAGES_RF_PS", */
		"HAL_INIT_STAGES_INIT_HAL_DM",
/*		"HAL_INIT_STAGES_IQK", */
/*		"HAL_INIT_STAGES_PW_TRACK", */
/*		"HAL_INIT_STAGES_LCK", */
		"HAL_INIT_STAGES_MISC21",
		/*"HAL_INIT_STAGES_INIT_PABIAS", */
		"HAL_INIT_STAGES_BT_COEXIST",
		/*"HAL_INIT_STAGES_ANTENNA_SEL", */
		"HAL_INIT_STAGES_MISC31",
		"HAL_INIT_STAGES_END",
	};

	int hal_init_profiling_i;
	u32 hal_init_stages_timestamp[HAL_INIT_STAGES_NUM]; /*used to record the time of each stage's starting point */

	for (hal_init_profiling_i = 0; hal_init_profiling_i < HAL_INIT_STAGES_NUM; hal_init_profiling_i++)
		hal_init_stages_timestamp[hal_init_profiling_i] = 0;

#define HAL_INIT_PROFILE_TAG(stage) { hal_init_stages_timestamp[(stage)] = rtw_get_current_time(); }
#else
#define HAL_INIT_PROFILE_TAG(stage) do {} while (0)
#endif /*DBG_HAL_INIT_PROFILING */



	_func_enter_;

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_BEGIN);

/*	if (rtw_is_surprise_removed(Adapter)) */
/*		return RT_STATUS_FAILURE; */

#if 0  /* To prevent Pomelo hanging. Added by tianzeyum. 2014.11.02 */
#define REG_USB_ACCESS_TIMEOUT 0xFE4C
	rtw_write8(padapter, REG_USB_ACCESS_TIMEOUT, 0x80);
#undef REG_USB_ACCESS_TIMEOUT
#endif


	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_PW_ON);
	status = rtw_hal_power_on(padapter);
	if (status == _FAIL) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init power on!\n"));
		goto exit;
	}

	/* Check if MAC has already power on. */
	value8 = rtw_read8(padapter, REG_SYS_CLKR_8188F + 1);
	u1bRegCR = rtw_read8(padapter, REG_CR_8188F);
	DBG_871X(" power-on :REG_SYS_CLKR 0x09=0x%02x. REG_CR 0x100=0x%02x.\n", value8, u1bRegCR);
	if ((value8 & BIT3) && (u1bRegCR != 0 && u1bRegCR != 0xEA))
		DBG_871X(" MAC has already power on.\n");
	else {
		/* Set FwPSState to ALL_ON mode to prevent from the I/O be return because of 32k */
		/* state which is set before sleep under wowlan mode. 2012.01.04. by tynli. */
		/*pHalData->FwPSState = FW_PS_STATE_ALL_ON_88E; */
		DBG_871X(" MAC has not been powered on yet.\n");
	}

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_LLTT);
	if (!pregistrypriv->wifi_spec)
		boundary = TX_PAGE_BOUNDARY_8188F;
	else {
		/* for WMM */
		boundary = WMM_NORMAL_TX_PAGE_BOUNDARY_8188F;
	}
	status =  rtl8188f_InitLLTTable(padapter);
	if (status == _FAIL) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init LLT table\n"));
		goto exit;
	}

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC01);
	if (pHalData->bRDGEnable)
		_InitRDGSetting_8188fu(padapter);


	/*Enable TX Report */
	/*Enable Tx Report Timer */
	value8 = rtw_read8(padapter, REG_TX_RPT_CTRL);
	rtw_write8(padapter, REG_TX_RPT_CTRL, value8 | BIT1);
	/*Set MAX RPT MACID */
	rtw_write8(padapter, REG_TX_RPT_CTRL + 1, 2);
	/*Tx RPT Timer. Unit: 32us */
	rtw_write16(padapter, REG_TX_RPT_TIME, 0xCdf0);

#ifdef CONFIG_TX_EARLY_MODE
	if (pHalData->AMPDUBurstMode) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("EarlyMode Enabled!!!\n"));

		value8 = rtw_read8(padapter, REG_EARLY_MODE_CONTROL_8188F);
#if RTL8188F_EARLY_MODE_PKT_NUM_10 == 1
		value8 = value8 | 0x1f;
#else
		value8 = value8 | 0xf;
#endif
		rtw_write8(padapter, REG_EARLY_MODE_CONTROL_8188F, value8);

		rtw_write8(padapter, REG_EARLY_MODE_CONTROL_8188F + 3, 0x80);

		value8 = rtw_read8(padapter, REG_TCR_8188F + 1);
		value8 = value8 | 0x40;
		rtw_write8(padapter, REG_TCR_8188F + 1, value8);
	} else
		rtw_write8(padapter, REG_EARLY_MODE_CONTROL_8188F, 0);
#endif

	/* <Kordan> InitHalDm should be put ahead of FirmwareDownload. (HWConfig flow: FW->MAC->-BB->RF) */
	/*InitHalDm(Adapter); */

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_DOWNLOAD_FW);
	if (padapter->registrypriv.mp_mode == 0
		#if defined(CONFIG_MP_INCLUDED) && defined(CONFIG_RTW_CUSTOMER_STR)
		|| padapter->registrypriv.mp_customer_str
		#endif
	) {
		status = rtl8188f_FirmwareDownload(padapter, _FALSE);
		if (status != _SUCCESS) {
			padapter->bFWReady = _FALSE;
			pHalData->fw_ractrl = _FALSE;
			DBG_871X("fw download fail!\n");
			goto exit;
		} else {
			padapter->bFWReady = _TRUE;
			pHalData->fw_ractrl = _TRUE;
			DBG_871X("fw download ok!\n");
		}
	}

	if (pwrctrlpriv->reg_rfoff == _TRUE)
		pwrctrlpriv->rf_pwrstate = rf_off;

	/* Set RF type for BB/RF configuration */
	/*_InitRFType(Adapter); */

	/* We should call the function before MAC/BB configuration. */
	PHY_InitAntennaSelection8188F(padapter);



	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MAC);
#if (HAL_MAC_ENABLE == 1)
	status = PHY_MACConfig8188F(padapter);
	if (status == _FAIL) {
		DBG_871X("PHY_MACConfig8188F fault !!\n");
		goto exit;
	}
#endif
	DBG_871X("PHY_MACConfig8188F OK!\n");


	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_BB);
	/* */
	/*d. Initialize BB related configurations. */
	/* */
#if (HAL_BB_ENABLE == 1)
	status = PHY_BBConfig8188F(padapter);
	if (status == _FAIL) {
		DBG_871X("PHY_BBConfig8188F fault !!\n");
		goto exit;
	}
#endif

	DBG_871X("PHY_BBConfig8188F OK!\n");



	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_RF);
#if (HAL_RF_ENABLE == 1)
	status = PHY_RFConfig8188F(padapter);

	if (status == _FAIL) {
		DBG_871X("PHY_RFConfig8188F fault !!\n");
		goto exit;
	}

	DBG_871X("PHY_RFConfig8188F OK!\n");


#endif



	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC02);
	_InitQueueReservedPage(padapter);
	_InitTxBufferBoundary(padapter);
	_InitQueuePriority(padapter);
	_InitPageBoundary(padapter);
	_InitTransferPageSize_8188fu(padapter);


	/* Get Rx PHY status in order to report RSSI and others. */
	_InitDriverInfoSize(padapter, DRVINFO_SZ);

	_InitInterrupt(padapter);
	hal_init_macaddr(padapter);/*set mac_address */
	_InitNetworkType(padapter);/*set msr */
	_InitWMACSetting(padapter);
	_InitAdaptiveCtrl(padapter);
	_InitEDCA(padapter);
	_InitRateFallback(padapter);
	_InitRetryFunction(padapter);
/*	_InitOperationMode(Adapter);//todo */
	rtl8188f_InitBeaconParameters(padapter);
	rtl8188f_InitBeaconMaxError(padapter, _TRUE);

	_InitBurstPktLen(padapter);
	_initUsbAggregationSetting(padapter);

#ifdef ENABLE_USB_DROP_INCORRECT_OUT
	_InitHardwareDropIncorrectBulkOut(padapter);
#endif

#if defined(CONFIG_CONCURRENT_MODE) || defined(CONFIG_TX_MCAST2UNI)

#ifdef CONFIG_CHECK_AC_LIFETIME
	/* Enable lifetime check for the four ACs */
	rtw_write8(padapter, REG_LIFETIME_CTRL, 0x0F);
#endif	/* CONFIG_CHECK_AC_LIFETIME	 */

#ifdef CONFIG_TX_MCAST2UNI
	rtw_write16(padapter, REG_PKT_VO_VI_LIFE_TIME, 0x0400);	/* unit: 256us. 256ms */
	rtw_write16(padapter, REG_PKT_BE_BK_LIFE_TIME, 0x0400);	/* unit: 256us. 256ms */
#else	/* CONFIG_TX_MCAST2UNI */
	rtw_write16(padapter, REG_PKT_VO_VI_LIFE_TIME, 0x3000);	/* unit: 256us. 3s */
	rtw_write16(padapter, REG_PKT_BE_BK_LIFE_TIME, 0x3000);	/* unit: 256us. 3s */
#endif	/* CONFIG_TX_MCAST2UNI */
#endif	/* CONFIG_CONCURRENT_MODE || CONFIG_TX_MCAST2UNI */


#ifdef CONFIG_LED
	_InitHWLed(padapter);
#endif /*CONFIG_LED */

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_TURN_ON_BLOCK);
	_BBTurnOnBlock(padapter);
	/*NicIFSetMacAddress(padapter, padapter->PermanentAddress); */


	rtw_hal_set_chnl_bw(padapter, padapter->registrypriv.channel,
						CHANNEL_WIDTH_20, HAL_PRIME_CHNL_OFFSET_DONT_CARE, HAL_PRIME_CHNL_OFFSET_DONT_CARE);

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_SECURITY);
	invalidate_cam_all(padapter);

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC11);
	/* 2010/12/17 MH We need to set TX power according to EFUSE content at first. */
	/*PHY_SetTxPowerLevel8188F(padapter, pHalData->CurrentChannel); */
	rtl8188f_InitAntenna_Selection(padapter);

	/* HW SEQ CTRL */
	/*set 0x0 to 0xFF by tynli. Default enable HW SEQ NUM. */
	rtw_write8(padapter, REG_HWSEQ_CTRL, 0xFF);

	/* */
	/* Disable BAR, suggested by Scott */
	/* 2010.04.09 add by hpfan */
	/* */
	rtw_write32(padapter, REG_BAR_MODE_CTRL, 0x0201ffff);

	if (pregistrypriv->wifi_spec)
		rtw_write16(padapter, REG_FAST_EDCA_CTRL , 0);

	/* Move by Neo for USB SS from above setp */

/*	_RfPowerSave(Adapter); */

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_HAL_DM);
	rtl8188f_InitHalDm(padapter);

#if (MP_DRIVER == 1)
	if (padapter->registrypriv.mp_mode == 1) {
		padapter->mppriv.channel = pHalData->CurrentChannel;
		MPT_InitializeAdapter(padapter, padapter->mppriv.channel);
	} else
#endif
	{
		pwrctrlpriv->rf_pwrstate = rf_on;

		if (pwrctrlpriv->rf_pwrstate == rf_on) {
			struct pwrctrl_priv *pwrpriv;
			u32 start_time;
			u8 restore_iqk_rst;
			u8 h2cCmdBuf;

			pwrpriv = adapter_to_pwrctl(padapter);

			PHY_LCCalibrate_8188F(&pHalData->odmpriv);

#ifdef CONFIG_BT_COEXIST
			/* Inform WiFi FW that it is the beginning of IQK */
			h2cCmdBuf = 1;
			FillH2CCmd8188F(padapter, H2C_8188F_BT_WLAN_CALIBRATION, 1, &h2cCmdBuf);

			start_time = rtw_get_current_time();
			do {
				if (rtw_read8(padapter, 0x1e7) & 0x01)
					break;

				rtw_msleep_os(50);
			} while (rtw_get_passing_time_ms(start_time) <= 400);


			rtw_btcoex_IQKNotify(padapter, _TRUE);
#endif

			restore_iqk_rst = (pwrpriv->bips_processing == _TRUE) ? _TRUE : _FALSE;
			PHY_IQCalibrate_8188F(padapter, _FALSE, restore_iqk_rst);
			pHalData->odmpriv.RFCalibrateInfo.bIQKInitialized = _TRUE;

#ifdef CONFIG_BT_COEXIST
			rtw_btcoex_IQKNotify(padapter, _FALSE);

			/* Inform WiFi FW that it is the finish of IQK */
			h2cCmdBuf = 0;
			FillH2CCmd8188F(padapter, H2C_8188F_BT_WLAN_CALIBRATION, 1, &h2cCmdBuf);
#endif

			ODM_TXPowerTrackingCheck(&pHalData->odmpriv);
		}
	}


	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC21);

/*HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_PABIAS); */
/*	_InitPABias(Adapter); */

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_BT_COEXIST);
#ifdef CONFIG_BT_COEXIST
	/* Init BT hw config. */
	rtw_btcoex_HAL_Initialize(padapter, _FALSE);
#else
	/* rtw_btcoex_HAL_Initialize(padapter, _TRUE);	// For Test. */
#endif

#if 0
	/* 2010/05/20 MH We need to init timer after update setting. Otherwise, we can not get correct inf setting. */
	/* 2010/05/18 MH For SE series only now. Init GPIO detect time */
	if (pDevice->RegUsbSS) {
		RT_TRACE(COMP_INIT, DBG_LOUD, (" call GpioDetectTimerStart8192CU\n"));
		GpioDetectTimerStart8192CU(Adapter);	/* Disable temporarily */
	}

	/* 2010/08/23 MH According to Alfred's suggestion, we need to to prevent HW enter */
	/* suspend mode automatically. */
	HwSuspendModeEnable92Cu(Adapter, _FALSE);
#endif

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC31);
	rtw_hal_set_hwreg(padapter, HW_VAR_NAV_UPPER, (u8 *)&NavUpper);

#ifdef CONFIG_XMIT_ACK
	/*ack for xmit mgmt frames. */
	rtw_write32(padapter, REG_FWHW_TXQ_CTRL, rtw_read32(padapter, REG_FWHW_TXQ_CTRL) | BIT(12));
#endif /*CONFIG_XMIT_ACK */

	/* Enable MACTXEN/MACRXEN block */
	u1bRegCR = rtw_read8(padapter, REG_CR);
	u1bRegCR |= (MACTXEN | MACRXEN);
	rtw_write8(padapter, REG_CR, u1bRegCR);

/* #if RTL8188F_USB_MAC_LOOPBACK */
#if 0
	rtw_write8(padapter, REG_CR_8188F + 3, 0x0B);
	DBG_871X("MAC loopback: REG_CR_8188F=%#X.\n", rtw_read32(padapter, REG_CR_8188F));
#endif

#if FPGA_TWO_MAC_VERIFICATION
/* #if 1 */
	/* Enable BB */
	value8 = rtw_read8(padapter, REG_SYS_FUNC_EN_8188F);
	DBG_871X("RJZ: open 2-MAC mode: REG_SYS_FUNC_EN_8188F = %#x.\n", value8);
	rtw_write8(padapter, REG_SYS_FUNC_EN_8188F, value8 | BIT0 | BIT1 | BIT2);
	value8 = rtw_read8(padapter, REG_SYS_FUNC_EN_8188F);
	DBG_871X("RJZ: open 2-MAC mode: REG_SYS_FUNC_EN_8188F = %#x.\n", value8);

	/* Use 40MHz */
	value8 = rtw_read8(padapter, REG_SYS_CLKR_8188F);
	rtw_write8(padapter, REG_SYS_CLKR_8188F, value8 & ~BIT4);
	value8 = rtw_read8(padapter, REG_SYS_CLKR_8188F);

	/* Clear 970~976. */
	rtw_write32(padapter, 0x0970, 0);
	rtw_write16(padapter, 0x0974, 0);
	rtw_write8(padapter, 0x0976, 0);

	/* Set the microsecond time unit used by MAC TSF clock. */
	rtw_write8(padapter, REG_USTIME_TSF_8188F, 0x28);
#endif

	/*_dbg_dump_macreg(Adapter); */

exit:
	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_END);

	DBG_871X("%s in %dms\n", __func__, rtw_get_passing_time_ms(init_start_time));

#ifdef DBG_HAL_INIT_PROFILING
	hal_init_stages_timestamp[HAL_INIT_STAGES_END] = rtw_get_current_time();

	for (hal_init_profiling_i = 0; hal_init_profiling_i < HAL_INIT_STAGES_NUM - 1; hal_init_profiling_i++) {
		DBG_871X("DBG_HAL_INIT_PROFILING: %35s, %u, %5u, %5u\n"
				 , hal_init_stages_str[hal_init_profiling_i]
				 , hal_init_stages_timestamp[hal_init_profiling_i]
				 , (hal_init_stages_timestamp[hal_init_profiling_i + 1] - hal_init_stages_timestamp[hal_init_profiling_i])
				 , rtw_get_time_interval_ms(hal_init_stages_timestamp[hal_init_profiling_i], hal_init_stages_timestamp[hal_init_profiling_i + 1])
				);
	}
#endif

	_func_exit_;

	return status;
}


static VOID
_DisableGPIO(
	IN	PADAPTER	Adapter
)
{
	/*
	 * j. GPIO_PIN_CTRL 0x44[31:0]=0x000
	 * k. Value = GPIO_PIN_CTRL[7:0]
	 * l. GPIO_PIN_CTRL 0x44[31:0] = 0x00FF0000 | (value <<8);		write external PIN level
	 * m. GPIO_MUXCFG 0x42 [15:0] = 0x0780
	 * n. LEDCFG 0x4C[15:0] = 0x8080
	 */
	u8	value8;
	u16	value16;
	u32	value32;

	/*1. Disable GPIO[7:0] */
	rtw_write16(Adapter, REG_GPIO_PIN_CTRL + 2, 0x0000);
	value32 = rtw_read32(Adapter, REG_GPIO_PIN_CTRL) & 0xFFFF00FF;
	value8 = (u8)(value32 & 0x000000FF);
	value32 |= ((value8 << 8) | 0x00FF0000);
	rtw_write32(Adapter, REG_GPIO_PIN_CTRL, value32);

	/*2. Disable GPIO[10:8] */
	rtw_write8(Adapter, REG_GPIO_MUXCFG + 3, 0x00);
	value16 = rtw_read16(Adapter, REG_GPIO_MUXCFG + 2) & 0xFF0F;
	value8 = (u8)(value16 & 0x000F);
	value16 |= ((value8 << 4) | 0x0780);
	rtw_write16(Adapter, REG_GPIO_MUXCFG + 2, value16);

	/*3. Disable LED0 & 1 */
	rtw_write16(Adapter, REG_LEDCFG0, 0x8080);

	/*RT_TRACE(COMP_INIT, DBG_LOUD, ("======> Disable GPIO and LED.\n")); */

} /*end of _DisableGPIO() */

static VOID
_ResetFWDownloadRegister(
	IN PADAPTER			Adapter
)
{
	u32	value32;

	value32 = rtw_read32(Adapter, REG_MCUFWDL);
	value32 &= ~(MCUFWDL_EN | MCUFWDL_RDY);
	rtw_write32(Adapter, REG_MCUFWDL, value32);
	/*RT_TRACE(COMP_INIT, DBG_LOUD, ("Reset FW download register.\n")); */
}

static VOID
_ResetBB(
	IN PADAPTER			Adapter
)
{
	u16	value16;

	/*reset BB */
	value16 = rtw_read16(Adapter, REG_SYS_FUNC_EN);
	value16 &= ~(FEN_BBRSTB | FEN_BB_GLB_RSTn);
	rtw_write16(Adapter, REG_SYS_FUNC_EN, value16);
	/*RT_TRACE(COMP_INIT, DBG_LOUD, ("Reset BB.\n")); */
}

static VOID
_ResetMCU(
	IN PADAPTER			Adapter
)
{
	u16	value16;

	/* reset MCU */
	value16 = rtw_read16(Adapter, REG_SYS_FUNC_EN);
	value16 &= ~FEN_CPUEN;
	rtw_write16(Adapter, REG_SYS_FUNC_EN, value16);
	/*RT_TRACE(COMP_INIT, DBG_LOUD, ("Reset MCU.\n")); */
}

static VOID
_DisableMAC_AFE_PLL(
	IN PADAPTER			Adapter
)
{
	u32	value32;

	/*disable MAC/ AFE PLL */
	value32 = rtw_read32(Adapter, REG_APS_FSMCO);
	value32 |= APDM_MAC;
	rtw_write32(Adapter, REG_APS_FSMCO, value32);

	value32 |= APFM_OFF;
	rtw_write32(Adapter, REG_APS_FSMCO, value32);
	/*RT_TRACE(COMP_INIT, DBG_LOUD, ("Disable MAC, AFE PLL.\n")); */
}

static VOID
_AutoPowerDownToHostOff(
	IN	PADAPTER		Adapter
)
{
	u32			value32;
	rtw_write8(Adapter, REG_SPS0_CTRL, 0x22);

	value32 = rtw_read32(Adapter, REG_APS_FSMCO);

	value32 |= APDM_HOST;/*card disable */
	rtw_write32(Adapter, REG_APS_FSMCO, value32);
	/*RT_TRACE(COMP_INIT, DBG_LOUD, ("Auto Power Down to Host-off state.\n")); */

	/* set USB suspend */
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

	/* set USB suspend */
	value32 |= AFSM_HSUS;
	rtw_write32(Adapter, REG_APS_FSMCO, value32);

	/*RT_ASSERT(0 == (rtw_read32(Adapter, REG_APS_FSMCO) & BIT(12)),("")); */
	/*RT_TRACE(COMP_INIT, DBG_LOUD, ("Set USB suspend.\n")); */

}

static VOID
_DisableRFAFEAndResetBB(
	IN PADAPTER			Adapter
)
{
	/*
	 * a.	TXPAUSE 0x522[7:0] = 0xFF			Pause MAC TX queue
	 * b.	RF path 0 offset 0x00 = 0x00		disable RF
	 * c.	APSD_CTRL 0x600[7:0] = 0x40
	 * d.	SYS_FUNC_EN 0x02[7:0] = 0x16		reset BB state machine
	 * e.	SYS_FUNC_EN 0x02[7:0] = 0x14		reset BB state machine
	 */
	u8 eRFPath = 0, value8 = 0;
	rtw_write8(Adapter, REG_TXPAUSE, 0xFF);
	PHY_SetRFReg(Adapter, eRFPath, 0x0, bMaskByte0, 0x0);

	value8 |= APSDOFF;
	rtw_write8(Adapter, REG_APSD_CTRL, value8);/*0x40 */

	value8 = 0;
	value8 |= (FEN_USBD | FEN_USBA | FEN_BB_GLB_RSTn);
	rtw_write8(Adapter, REG_SYS_FUNC_EN, value8);/*0x16 */

	value8 &= (~FEN_BB_GLB_RSTn);
	rtw_write8(Adapter, REG_SYS_FUNC_EN, value8); /*0x14 */

	/*RT_TRACE(COMP_INIT, DBG_LOUD, ("======> RF off and reset BB.\n")); */
}

static VOID
_ResetDigitalProcedure1(
	IN 	PADAPTER			Adapter,
	IN	BOOLEAN				bWithoutHWSM
)
{

	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);

	if (pHalData->FirmwareVersion <=  0x20) {
#if 0
		/*
		 * f.	SYS_FUNC_EN 0x03[7:0]=0x54		reset MAC register, DCORE
		 * g.	MCUFWDL 0x80[7:0]=0				reset MCU ready status
		 */
		u4Byte	value32 = 0;
		PlatformIOWrite1Byte(Adapter, REG_SYS_FUNC_EN + 1, 0x54);
		PlatformIOWrite1Byte(Adapter, REG_MCUFWDL, 0);
#else
		/*
		 * i.	MCUFWDL 0x80[7:0]=0				reset MCU ready status
		 * g.	SYS_FUNC_EN 0x02[10]= 0			reset MCU register, (8051 reset)
		 * h.	SYS_FUNC_EN 0x02[15-12]= 5		reset MAC register, DCORE
		 * i.	SYS_FUNC_EN 0x02[10]= 1			enable MCU register, (8051 enable)
		 */
		u16 valu16 = 0;

		rtw_write8(Adapter, REG_MCUFWDL, 0);

		valu16 = rtw_read16(Adapter, REG_SYS_FUNC_EN);
		rtw_write16(Adapter, REG_SYS_FUNC_EN, (valu16 & (~FEN_CPUEN)));/*reset MCU ,8051 */

		valu16 = rtw_read16(Adapter, REG_SYS_FUNC_EN) & 0x0FFF;
		rtw_write16(Adapter, REG_SYS_FUNC_EN, (valu16 | (FEN_HWPDN | FEN_ELDR))); /*reset MAC */

#ifdef DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE
		{
			u8 val;

			val = rtw_read8(Adapter, REG_MCUFWDL)

			if (val) {
				DBG_871X("DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE %s:%d REG_MCUFWDL:0x%02x\n",
						__func__, __LINE__, val);
			}
		}
#endif


		valu16 = rtw_read16(Adapter, REG_SYS_FUNC_EN);
		rtw_write16(Adapter, REG_SYS_FUNC_EN, (valu16 | FEN_CPUEN));/*enable MCU ,8051 */


#endif
	} else {
		u8 retry_cnts = 0;

		if (rtw_read8(Adapter, REG_MCUFWDL) & BIT1) {
			/*IF fw in RAM code, do reset */

			rtw_write8(Adapter, REG_MCUFWDL, 0);
			if (Adapter->bFWReady) {
				/* 2010/08/25 MH According to RD alfred's suggestion, we need to disable other */
				/* HRCV INT to influence 8051 reset. */
				rtw_write8(Adapter, REG_FWIMR, 0x20);

				rtw_write8(Adapter, REG_HMETFR + 3, 0x20); /*8051 reset by self */

				while ((retry_cnts++ < 100) && (FEN_CPUEN & rtw_read16(Adapter, REG_SYS_FUNC_EN))) {
					/*PlatformStallExecution(50); //us */
					rtw_udelay_os(50);
				}

				if (retry_cnts >= 100) {
					DBG_8192C("%s #####=> 8051 reset failed!.........................\n", __func__);
					/* if 8051 reset fail we trigger GPIO 0 for LA */
					/*PlatformEFIOWrite4Byte(	Adapter, */
					/*						REG_GPIO_PIN_CTRL, */
					/*						0x00010100); */
					/* 2010/08/31 MH According to Filen's info, if 8051 reset fail, reset MAC directly. */
					rtw_write8(Adapter, REG_SYS_FUNC_EN + 1, 0x50);	/*Reset MAC and Enable 8051 */
					rtw_mdelay_os(10);
				} else {
					/*DBG_871X("%s =====> 8051 reset success (%d) .\n", __func__, retry_cnts); */
				}
			} else
				DBG_871X("%s =====> 8051 in RAM but !Adapter->bFWReady\n", __func__);
		} else {
			/*DBG_871X("%s =====> 8051 in ROM.\n", __func__); */
		}

#ifdef DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE
		{
			u8 val;
			val = rtw_read8(Adapter, REG_MCUFWDL)

			if (val) {
				DBG_871X("DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE %s:%d REG_MCUFWDL:0x%02x\n",
						__func__, __LINE__, val);
			}
		}
#endif

		rtw_write8(Adapter, REG_SYS_FUNC_EN + 1, 0x54);	/*Reset MAC and Enable 8051 */
	}

	/* Clear rpwm value for initial toggle bit trigger. */
	rtw_write8(Adapter, REG_USB_HRPWM, 0x00);

	if (bWithoutHWSM) {
		/*
		 * Without HW auto state machine
		 * g.	SYS_CLKR 0x08[15:0] = 0x30A3			disable MAC clock
		 * h.	AFE_PLL_CTRL 0x28[7:0] = 0x80			disable AFE PLL
		 * i.	AFE_XTAL_CTRL 0x24[15:0] = 0x880F		gated AFE DIG_CLOCK
		 * j.	SYS_ISO_CTRL 0x00[7:0] = 0xF9			isolated digital to PON
		 */
		/*rtw_write16(Adapter, REG_SYS_CLKR, 0x30A3); */
		rtw_write16(Adapter, REG_SYS_CLKR, 0x70A3);/*modify to 0x70A3 by Scott. */
		rtw_write8(Adapter, REG_AFE_PLL_CTRL, 0x80);
		rtw_write16(Adapter, REG_AFE_XTAL_CTRL, 0x880F);
		rtw_write8(Adapter, REG_SYS_ISO_CTRL, 0xF9);
	} else {
		/* Disable all RF/BB power */
		rtw_write8(Adapter, REG_RF_CTRL, 0x00);
	}
	/*RT_TRACE(COMP_INIT, DBG_LOUD, ("======> Reset Digital.\n")); */

}

static VOID
_ResetDigitalProcedure2(
	IN 	PADAPTER			Adapter
)
{
	/*
	 * k.	SYS_FUNC_EN 0x03[7:0] = 0x44			disable ELDR runction
	 * l.	SYS_CLKR 0x08[15:0] = 0x3083			disable ELDR clock
	 * m.	SYS_ISO_CTRL 0x01[7:0] = 0x83			isolated ELDR to PON
	 */
	/*rtw_write8(Adapter, REG_SYS_FUNC_EN+1, 0x44);//marked by Scott. */
	/*rtw_write16(Adapter, REG_SYS_CLKR, 0x3083); */
	/*rtw_write8(Adapter, REG_SYS_ISO_CTRL+1, 0x83); */

	rtw_write16(Adapter, REG_SYS_CLKR, 0x70a3); /*modify to 0x70a3 by Scott. */
	rtw_write8(Adapter, REG_SYS_ISO_CTRL + 1, 0x82); /*modify to 0x82 by Scott. */
}

static VOID
_DisableAnalog(
	IN PADAPTER			Adapter,
	IN BOOLEAN			bWithoutHWSM
)
{
	u16 value16 = 0;
	u8 value8 = 0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	if (bWithoutHWSM) {
		/*
		 * n.	LDOA15_CTRL 0x20[7:0] = 0x04		disable A15 power
		 * o.	LDOV12D_CTRL 0x21[7:0] = 0x54		disable digital core power
		 * r.	When driver call disable, the ASIC will turn off remaining clock automatically
		 */

		rtw_write8(Adapter, REG_LDOA15_CTRL, 0x04);
		/*PlatformIOWrite1Byte(Adapter, REG_LDOV12D_CTRL, 0x54); */

		value8 = rtw_read8(Adapter, REG_LDOV12D_CTRL);
		value8 &= (~LDV12_EN);
		rtw_write8(Adapter, REG_LDOV12D_CTRL, value8);
		/*RT_TRACE(COMP_INIT, DBG_LOUD, (" REG_LDOV12D_CTRL Reg0x21:0x%02x.\n",value8)); */
	}

	/*
	 * h.	SPS0_CTRL 0x11[7:0] = 0x23			enter PFM mode
	 * i.	APS_FSMCO 0x04[15:0] = 0x4802		set USB suspend
	 */


	value8 = 0x23;

	rtw_write8(Adapter, REG_SPS0_CTRL, value8);


	if (bWithoutHWSM) {
		/* 2010/08/31 According to Filen description, we need to use HW to shut down 8051 automatically. */
		/* Because suspend operation need the asistance of 8051 to wait for 3ms. */
		value16 |= (APDM_HOST | AFSM_HSUS | PFM_ALDN);
	} else
		value16 |= (APDM_HOST | AFSM_HSUS | PFM_ALDN);

	rtw_write16(Adapter, REG_APS_FSMCO, value16);/*0x4802 */

	rtw_write8(Adapter, REG_RSV_CTRL, 0x0e);

#if 0
	/*tynli_test for suspend mode. */
	if (!bWithoutHWSM)
		rtw_write8(Adapter, 0xfe10, 0x19);
#endif

	/*RT_TRACE(COMP_INIT, DBG_LOUD, ("======> Disable Analog Reg0x04:0x%04x.\n",value16)); */
}

static void rtl8188fu_hw_power_down(_adapter *padapter)
{
	u8	u1bTmp;

	DBG_8192C("PowerDownRTL8188FU\n");


	/* 1. Run Card Disable Flow */
	/* Done before this function call. */

	/* 2. 0x04[16] = 0			// reset WLON */
	u1bTmp = rtw_read8(padapter, REG_APS_FSMCO + 2);
	rtw_write8(padapter, REG_APS_FSMCO + 2, (u1bTmp & (~BIT0)));

	/* 3. 0x04[12:11] = 2b'11 // enable suspend */
	/* Done before this function call. */

	/* 4. 0x04[15] = 1			// enable PDN */
	u1bTmp = rtw_read8(padapter, REG_APS_FSMCO + 1);
	rtw_write8(padapter, REG_APS_FSMCO + 1, (u1bTmp | BIT7));
}

/* */
/* Description: RTL8188F card disable power sequence v003 which suggested by Scott. */
/* First created by tynli. 2011.01.28. */
/* */
VOID
CardDisableRTL8188FU(
	PADAPTER			Adapter
)
{
	u8		u1bTmp;
/*	PMGNT_INFO	pMgntInfo	= &(Adapter->MgntInfo); */

	DBG_8192C("CardDisableRTL8188FU\n");

	/*Stop Tx Report Timer. 0x4EC[Bit1]=b'0 */
	u1bTmp = rtw_read8(Adapter, REG_TX_RPT_CTRL);
	rtw_write8(Adapter, REG_TX_RPT_CTRL, u1bTmp & (~BIT1));

	/* stop rx */
	rtw_write8(Adapter, REG_CR_8188F, 0x0);
	if ((rtw_read8(Adapter, REG_MCUFWDL_8188F)&BIT7) &&
		Adapter->bFWReady)   /*8051 RAM code */
		rtl8188f_FirmwareSelfReset(Adapter);

	/* 1. Run LPS WL RFOFF flow */
	HalPwrSeqCmdParsing(Adapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, rtl8188F_enter_lps_flow);

	/* Reset MCU. Suggested by Filen. 2011.01.26. by tynli. */
	u1bTmp = rtw_read8(Adapter, REG_SYS_FUNC_EN_8188F + 1);
	rtw_write8(Adapter, REG_SYS_FUNC_EN_8188F + 1, (u1bTmp & (~BIT2)));

	/* MCUFWDL 0x80[1:0]=0				// reset MCU ready status */
	rtw_write8(Adapter, REG_MCUFWDL_8188F, 0x00);

	/* Card disable power action flow */
	HalPwrSeqCmdParsing(Adapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, rtl8188F_card_disable_flow);

	Adapter->bFWReady = _FALSE;
}

u32 rtl8188fu_hal_deinit(PADAPTER Adapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(Adapter);
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(Adapter);

	DBG_871X("==> %s\n", __func__);

	rtw_write16(Adapter, REG_GPIO_MUXCFG, rtw_read16(Adapter, REG_GPIO_MUXCFG) & (~BIT12));

	rtw_write32(Adapter, REG_HISR0_8188F, 0xFFFFFFFF);
	rtw_write32(Adapter, REG_HISR1_8188F, 0xFFFFFFFF);

#if 0
	/* USB only need to clear HISR, no need to set HIMR, because there's no hardware interrupt for USB. */
	rtw_write32(Adapter, REG_HIMR0_8188F, IMR_DISABLED_8188F);
	rtw_write32(Adapter, REG_HIMR1_8188F, IMR_DISABLED_8188F);
#endif

#ifdef CONFIG_MP_INCLUDED
	if (Adapter->registrypriv.mp_mode == 1)
		MPT_DeInitAdapter(Adapter);
#endif

#ifdef SUPPORT_HW_RFOFF_DETECTED
	DBG_871X("%s: bkeepfwalive(%x)\n", __func__, pwrctl->bkeepfwalive);

	if (pwrctl->bkeepfwalive) {
		_ps_close_RF(Adapter);
		if ((pwrctl->bHWPwrPindetect) && (pwrctl->bHWPowerdown))
			rtl8188fu_hw_power_down(Adapter);
	} else
#endif
	{
		if (rtw_is_hw_init_completed(Adapter)) {
			rtw_hal_power_off(Adapter);

			if ((pwrctl->bHWPwrPindetect) && (pwrctl->bHWPowerdown))
				rtl8188fu_hw_power_down(Adapter);
		}
		pHalData->bMacPwrCtrlOn = _FALSE;
	}
	return _SUCCESS;
}

unsigned int rtl8188fu_inirp_init(PADAPTER Adapter)
{
	u8 i;
	struct recv_buf *precvbuf;
	uint	status;
	struct dvobj_priv *pdev = adapter_to_dvobj(Adapter);
	struct intf_hdl *pintfhdl = &Adapter->iopriv.intf;
	struct recv_priv *precvpriv = &(Adapter->recvpriv);

	u32 (*_read_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	u32 (*_read_interrupt)(struct intf_hdl *pintfhdl, u32 addr);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
#endif /*CONFIG_USB_INTERRUPT_IN_PIPE */

	_func_enter_;

	_read_port = pintfhdl->io_ops._read_port;

	status = _SUCCESS;

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("===> usb_inirp_init\n"));

	precvpriv->ff_hwaddr = RECV_BULK_IN_ADDR;

	/*issue Rx irp to receive data */
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;
	for (i = 0; i < NR_RECVBUFF; i++) {
		if (_read_port(pintfhdl, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf) == _FALSE) {
			RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("usb_rx_init: usb_read_port error\n"));
			status = _FAIL;
			goto exit;
		}

		precvbuf++;
		precvpriv->free_recv_buf_queue_cnt--;
	}

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	_read_interrupt = pintfhdl->io_ops._read_interrupt;
	if (_read_interrupt(pintfhdl, RECV_INT_IN_ADDR) == _FALSE) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("usb_rx_init: usb_read_interrupt error\n"));
		status = _FAIL;
	}
	pHalData->IntrMask[0] = rtw_read32(Adapter, REG_USB_HIMR);
	MSG_8192C("pHalData->IntrMask = 0x%04x\n", pHalData->IntrMask[0]);
	pHalData->IntrMask[0] |= UHIMR_C2HCMD | UHIMR_CPWM;
	rtw_write32(Adapter, REG_USB_HIMR, pHalData->IntrMask[0]);
#endif /*CONFIG_USB_INTERRUPT_IN_PIPE */

exit:

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("<=== usb_inirp_init\n"));

	_func_exit_;

	return status;

}

unsigned int rtl8188fu_inirp_deinit(PADAPTER Adapter)
{
#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	u32 (*_read_interrupt)(struct intf_hdl *pintfhdl, u32 addr);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
#endif /*CONFIG_USB_INTERRUPT_IN_PIPE */
	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("\n ===> usb_rx_deinit\n"));

	rtw_read_port_cancel(Adapter);
#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	pHalData->IntrMask[0] = rtw_read32(Adapter, REG_USB_HIMR);
	MSG_8192C("%s pHalData->IntrMask = 0x%04x\n", __func__, pHalData->IntrMask[0]);
	pHalData->IntrMask[0] = 0x0;
	rtw_write32(Adapter, REG_USB_HIMR, pHalData->IntrMask[0]);
	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("\n <=== usb_rx_deinit\n"));
#endif /*CONFIG_USB_INTERRUPT_IN_PIPE */
	return _SUCCESS;
}


static u32
_GetChannelGroup(
	IN	u32	channel
)
{
	/*RT_ASSERT((channel < 14), ("Channel %d no is supported!\n")); */

	if (channel < 3)	/* Channel 1~3 */
		return 0;
	else if (channel < 9)	/* Channel 4~9 */
		return 1;

	return 2;				/* Channel 10~14 */
}


/*------------------------------------------------------------------- */
/* */
/*	EEPROM/EFUSE Content Parsing */
/* */
/*------------------------------------------------------------------- */
static VOID
_ReadLEDSetting(
	IN	PADAPTER	Adapter,
	IN	u8		*PROMContent,
	IN	BOOLEAN		AutoloadFail
)
{
	struct led_priv *pledpriv = &(Adapter->ledpriv);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

#ifdef CONFIG_SW_LED
	pledpriv->bRegUseLed = _TRUE;

	/* */
	/* Led mode */
	/* */
	switch (pHalData->CustomerID) {
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

/*	if( BOARD_MINICARD == pHalData->BoardType ) */
/*	{ */
/*		pledpriv->LedStrategy = SW_LED_MODE6; */
/*	} */
	pHalData->bLedOpenDrain = _TRUE;/* Support Open-drain arrangement for controlling the LED. Added by Roger, 2009.10.16. */
#else /* HW LED */
	pledpriv->LedStrategy = HW_LED;
#endif /*CONFIG_SW_LED */
}

static VOID
_ReadThermalMeter(
	IN	PADAPTER	Adapter,
	IN	u8	*PROMContent,
	IN	BOOLEAN 	AutoloadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	tempval;

	/* */
	/* ThermalMeter from EEPROM */
	/* */
	if (!AutoloadFail)
		tempval = PROMContent[EEPROM_THERMAL_METER_8188F];
	else
		tempval = EEPROM_Default_ThermalMeter;

	pHalData->EEPROMThermalMeter = (tempval & 0x1f);	/*[4:0] */

	if (pHalData->EEPROMThermalMeter == 0x1f || AutoloadFail)
		pHalData->odmpriv.RFCalibrateInfo.bAPKThermalMeterIgnore = _TRUE;

#if 0
	if (pHalData->EEPROMThermalMeter < 0x06 || pHalData->EEPROMThermalMeter > 0x1c)
		pHalData->EEPROMThermalMeter = 0x12;
#endif

	/*pHalData->ThermalMeter[0] = pHalData->EEPROMThermalMeter; */


	/*RTPRINT(FINIT, INIT_TxPower, ("ThermalMeter = 0x%x\n", pHalData->EEPROMThermalMeter)); */

}

VOID
Hal_EfuseParsePIDVID_8188FU(
	IN	PADAPTER		pAdapter,
	IN	u8			*hwinfo,
	IN	BOOLEAN			AutoLoadFail
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if (AutoLoadFail) {
		pHalData->EEPROMVID = 0;
		pHalData->EEPROMPID = 0;
	} else {
		/* VID, PID */
		pHalData->EEPROMVID = le16_to_cpu(*(u16 *)&hwinfo[EEPROM_VID_8188FU]);
		pHalData->EEPROMPID = le16_to_cpu(*(u16 *)&hwinfo[EEPROM_PID_8188FU]);

	}

	MSG_8192C("EEPROM VID = 0x%4x\n", pHalData->EEPROMVID);
	MSG_8192C("EEPROM PID = 0x%4x\n", pHalData->EEPROMPID);
}

static VOID
InitAdapterVariablesByPROM_8188FU(
	IN	PADAPTER	padapter
)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u8			*hwinfo = NULL;

	if (sizeof(pHalData->efuse_eeprom_data) < HWSET_MAX_SIZE_8188F)
		DBG_871X("[WARNING] size of efuse_eeprom_data is less than HWSET_MAX_SIZE_8188F!\n");

	hwinfo = pHalData->efuse_eeprom_data;

	Hal_InitPGData(padapter, hwinfo);

	Hal_EfuseParseIDCode(padapter, hwinfo);
	Hal_EfuseParsePIDVID_8188FU(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseEEPROMVer_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
	hal_config_macaddr(padapter, pHalData->bautoload_fail_flag);
	Hal_EfuseParseTxPowerInfo_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
/* Hal_EfuseParseBTCoexistInfo_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag); */

	Hal_EfuseParseChnlPlan_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseThermalMeter_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
/* _ReadLEDSetting(Adapter, PROMContent, pHalData->bautoload_fail_flag); */
	Hal_EfuseParsePowerSavingMode_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseAntennaDiversity_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);

	Hal_EfuseParseEEPROMVer_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
	Hal_EfuseParseCustomerID_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
/* Hal_EfuseParseRateIndicationOption(padapter, hwinfo, pHalData->bautoload_fail_flag); */
	Hal_EfuseParseXtal_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
	/* */
	/* The following part initialize some vars by PG info. */
	/* */
/* Hal_InitChannelPlan(padapter); */



	/*hal_CustomizedBehavior_8188FU(Adapter); */

	Hal_EfuseParseKFreeData_8188F(padapter, hwinfo, pHalData->bautoload_fail_flag);
	hal_read_mac_hidden_rpt(padapter);

/*	Adapter->bDongle = (PROMContent[EEPROM_EASY_REPLACEMENT] == 1)? 0: 1; */
	DBG_8192C("%s(): REPLACEMENT = %x\n", __func__, padapter->bDongle);
}

static void _ReadPROMContent(
	IN PADAPTER 		Adapter
)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(Adapter);
	u8			eeValue;

	/* To check system boot selection. */
	eeValue = rtw_read8(Adapter, REG_SYS_EEPROM_CTRL);
	pHalData->EepromOrEfuse = (eeValue & EEPROMSEL) ? _TRUE : _FALSE;
	pHalData->bautoload_fail_flag = (eeValue & EEPROM_EN) ? _FALSE : _TRUE;

	DBG_8192C("Boot from %s, Autoload %s !\n", (pHalData->EepromOrEfuse ? "EEPROM" : "EFUSE"),
			  (pHalData->bautoload_fail_flag ? "Fail" : "OK"));

#ifdef CONFIG_8188FTV_SOLUTION_D
	rtl8188fu_solution_d(Adapter);
#endif

	InitAdapterVariablesByPROM_8188FU(Adapter);
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



/* */
/*	Description: */
/*		We should set Efuse cell selection to WiFi cell in default. */
/* */
/*	Assumption: */
/*		PASSIVE_LEVEL */
/* */
/*	Added by Roger, 2010.11.23. */
/* */
void
hal_EfuseCellSel(
	IN	PADAPTER	Adapter
)
{
	u32			value32;

	value32 = rtw_read32(Adapter, EFUSE_TEST);
	value32 = (value32 & ~EFUSE_SEL_MASK) | EFUSE_SEL(EFUSE_WIFI_SEL_0);
	rtw_write32(Adapter, EFUSE_TEST, value32);
}

static void ReadAdapterInfo8188FU(PADAPTER Adapter)
{
	/* Read EEPROM size before call any EEPROM function */
	Adapter->EepromAddressSize = GetEEPROMSize8188F(Adapter);

	/*Efuse_InitSomeVar(Adapter); */

	hal_EfuseCellSel(Adapter);

	_ReadRFType(Adapter);/*rf_chip -> _InitRFType() */
	_ReadPROMContent(Adapter);
}

#define GPIO_DEBUG_PORT_NUM 0
static void rtl8188fu_trigger_gpio_0(_adapter *padapter)
{

	u32 gpioctrl;
	DBG_871X("==> trigger_gpio_0...\n");
	rtw_write16_async(padapter, REG_GPIO_PIN_CTRL, 0);
	rtw_write8_async(padapter, REG_GPIO_PIN_CTRL + 2, 0xFF);
	gpioctrl = (BIT(GPIO_DEBUG_PORT_NUM) << 24) | (BIT(GPIO_DEBUG_PORT_NUM) << 16);
	rtw_write32_async(padapter, REG_GPIO_PIN_CTRL, gpioctrl);
	gpioctrl |= (BIT(GPIO_DEBUG_PORT_NUM) << 8);
	rtw_write32_async(padapter, REG_GPIO_PIN_CTRL, gpioctrl);
	DBG_871X("<=== trigger_gpio_0...\n");

}

/*
 * If variable not handled here,
 * some variables will be processed in SetHwReg8188FU()
 */
void SetHwReg8188FU(PADAPTER Adapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);

	_func_enter_;

	switch (variable) {
	case HW_VAR_RXDMA_AGG_PG_TH:
#ifdef CONFIG_USB_RX_AGGREGATION
	{
		u8 threshold = *val;

		if (threshold == 0)
			threshold = pHalData->UsbRxAggPageCount;
		SetHwReg8188F(Adapter, HW_VAR_RXDMA_AGG_PG_TH, &threshold);
	}
#endif
	break;

	case HW_VAR_SET_RPWM:
		rtw_write8(Adapter, REG_USB_HRPWM, *val);
		break;

	case HW_VAR_TRIGGER_GPIO_0:
		rtl8188fu_trigger_gpio_0(Adapter);
		break;

	default:
		SetHwReg8188F(Adapter, variable, val);
		break;
	}

	_func_exit_;
}

/*
 * If variable not handled here,
 * some variables will be processed in GetHwReg8188FU()
 */
void GetHwReg8188FU(PADAPTER Adapter, u8 variable, u8 *val)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);

	_func_enter_;

	switch (variable) {
	default:
		GetHwReg8188F(Adapter, variable, val);
		break;
	}

	_func_exit_;
}

/* */
/*	Description: */
/*		Query setting of specified variable. */
/* */
u8
GetHalDefVar8188FUsb(
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
		*((u8 *)pValue) = (pHalData->AntDivCfg == 0) ? _FALSE : _TRUE;
#endif
		break;

	case HAL_DEF_DRVINFO_SZ:
		*((u32 *)pValue) = DRVINFO_SZ;
		break;
	case HAL_DEF_MAX_RECVBUF_SZ:
		*((u32 *)pValue) = MAX_RECVBUF_SZ;
		break;
	case HAL_DEF_RX_PACKET_OFFSET:
		*((u32 *)pValue) = RXDESC_SIZE + DRVINFO_SZ*8;
		break;
	case HW_VAR_MAX_RX_AMPDU_FACTOR:
		*((HT_CAP_AMPDU_FACTOR *)pValue) = MAX_AMPDU_FACTOR_64K;
		break;
	default:
		bResult = GetHalDefVar8188F(Adapter, eVariable, pValue);
		break;
	}

	return bResult;
}




/* */
/*	Description: */
/*		Change default setting of specified variable. */
/* */
u8
SetHalDefVar8188FUsb(
	IN	PADAPTER				Adapter,
	IN	HAL_DEF_VARIABLE		eVariable,
	IN	PVOID					pValue
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _SUCCESS;

	switch (eVariable) {
	default:
		bResult = SetHalDefVar8188F(Adapter, eVariable, pValue);
		break;
	}

	return bResult;
}

void _update_response_rate(_adapter *padapter, unsigned int mask)
{
	u8	RateIndex = 0;
	/* Set RRSR rate table. */
	rtw_write8(padapter, REG_RRSR, mask & 0xff);
	rtw_write8(padapter, REG_RRSR + 1, (mask >> 8) & 0xff);

	/* Set RTS initial rate */
	while (mask > 0x1) {
		mask = (mask >> 1);
		RateIndex++;
	}
	rtw_write8(padapter, REG_INIRTS_RATE_SEL, RateIndex);
}

static void rtl8188fu_init_default_value(PADAPTER padapter)
{
	rtl8188f_init_default_value(padapter);
}

static u8 rtl8188fu_ps_func(PADAPTER Adapter, HAL_INTF_PS_FUNC efunc_id, u8 *val)
{
	u8 bResult = _TRUE;
	switch (efunc_id) {

#if defined(CONFIG_AUTOSUSPEND) && defined(SUPPORT_HW_RFOFF_DETECTED)
	case HAL_USB_SELECT_SUSPEND: {
		u8 bfwpoll = *((u8 *)val);

		rtl8188f_set_FwSelectSuspend_cmd(Adapter, bfwpoll , 500); /*note fw to support hw power down ping detect */
	}
	break;
#endif /*CONFIG_AUTOSUSPEND && SUPPORT_HW_RFOFF_DETECTED */

	default:
		break;
	}
	return bResult;
}

void rtl8188fu_set_hal_ops(_adapter *padapter)
{
	struct hal_ops	*pHalFunc = &padapter->HalFunc;

	_func_enter_;

	rtl8188f_set_hal_ops(pHalFunc);

	pHalFunc->hal_power_on = &_InitPowerOn_8188FU;
	pHalFunc->hal_power_off = &CardDisableRTL8188FU;

	pHalFunc->hal_init = &rtl8188fu_hal_init;
	pHalFunc->hal_deinit = &rtl8188fu_hal_deinit;

	pHalFunc->inirp_init = &rtl8188fu_inirp_init;
	pHalFunc->inirp_deinit = &rtl8188fu_inirp_deinit;

	pHalFunc->init_xmit_priv = &rtl8188fu_init_xmit_priv;
	pHalFunc->free_xmit_priv = &rtl8188fu_free_xmit_priv;

	pHalFunc->init_recv_priv = &rtl8188fu_init_recv_priv;
	pHalFunc->free_recv_priv = &rtl8188fu_free_recv_priv;
#ifdef CONFIG_SW_LED
	pHalFunc->InitSwLeds = &rtl8188fu_InitSwLeds;
	pHalFunc->DeInitSwLeds = &rtl8188fu_DeInitSwLeds;
#else /*case of hw led or no led */
	pHalFunc->InitSwLeds = NULL;
	pHalFunc->DeInitSwLeds = NULL;
#endif/*CONFIG_SW_LED */

	pHalFunc->init_default_value = &rtl8188f_init_default_value;
	pHalFunc->intf_chip_configure = &rtl8188fu_interface_configure;
	pHalFunc->read_adapter_info = &ReadAdapterInfo8188FU;

	pHalFunc->SetHwRegHandler = &SetHwReg8188FU;
	pHalFunc->GetHwRegHandler = &GetHwReg8188FU;
	pHalFunc->GetHalDefVarHandler = &GetHalDefVar8188FUsb;
	pHalFunc->SetHalDefVarHandler = &SetHalDefVar8188FUsb;

	pHalFunc->hal_xmit = &rtl8188fu_hal_xmit;
	pHalFunc->mgnt_xmit = &rtl8188fu_mgnt_xmit;
	pHalFunc->hal_xmitframe_enqueue = &rtl8188fu_hal_xmitframe_enqueue;

#ifdef CONFIG_HOSTAPD_MLME
	pHalFunc->hostap_mgnt_xmit_entry = &rtl8188fu_hostap_mgnt_xmit_entry;
#endif
	pHalFunc->interface_ps_func = &rtl8188fu_ps_func;

#ifdef CONFIG_XMIT_THREAD_MODE
	pHalFunc->xmit_thread_handler = &rtl8188fu_xmit_buf_handler;
#endif
#ifdef CONFIG_SUPPORT_USB_INT
	pHalFunc->interrupt_handler = interrupt_handler_8188fu;
#endif

	_func_exit_;
}

