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

#include <drv_types.h>
#include <rtl8188e_hal.h>
#include "hal_com_h2c.h"

#ifndef CONFIG_USB_HCI

	#error "CONFIG_USB_HCI shall be on!\n"

#endif


static VOID
_ConfigNormalChipOutEP_8188E(
	IN	PADAPTER	pAdapter,
	IN	u8		NumOutPipe
)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);

	switch (NumOutPipe) {
	case	3:
		pHalData->OutEpQueueSel = TX_SELE_HQ | TX_SELE_LQ | TX_SELE_NQ;
		pHalData->OutEpNumber = 3;
		break;
	case	2:
		pHalData->OutEpQueueSel = TX_SELE_HQ | TX_SELE_NQ;
		pHalData->OutEpNumber = 2;
		break;
	case	1:
		pHalData->OutEpQueueSel = TX_SELE_HQ;
		pHalData->OutEpNumber = 1;
		break;
	default:
		break;

	}
	RTW_INFO("%s OutEpQueueSel(0x%02x), OutEpNumber(%d)\n", __FUNCTION__, pHalData->OutEpQueueSel, pHalData->OutEpNumber);

}

static BOOLEAN HalUsbSetQueuePipeMapping8188EUsb(
	IN	PADAPTER	pAdapter,
	IN	u8		NumInPipe,
	IN	u8		NumOutPipe
)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);
	BOOLEAN			result		= _FALSE;

	_ConfigNormalChipOutEP_8188E(pAdapter, NumOutPipe);

	/* Normal chip with one IN and one OUT doesn't have interrupt IN EP. */
	if (1 == pHalData->OutEpNumber) {
		if (1 != NumInPipe)
			return result;
	}

	/* All config other than above support one Bulk IN and one Interrupt IN. */
	/* if(2 != NumInPipe){ */
	/*	return result; */
	/* } */

	result = Hal_MappingOutPipe(pAdapter, NumOutPipe);

	return result;

}

void rtl8188eu_interface_configure(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct registry_priv  *registry_par = &padapter->registrypriv;

	if (IS_HIGH_SPEED_USB(padapter)) {
		pHalData->UsbBulkOutSize = USB_HIGH_SPEED_BULK_SIZE;/* 512 bytes */
	} else {
		pHalData->UsbBulkOutSize = USB_FULL_SPEED_BULK_SIZE;/* 64 bytes */
	}

	pHalData->interfaceIndex = pdvobjpriv->InterfaceNumber;

#ifdef CONFIG_USB_TX_AGGREGATION
	pHalData->UsbTxAggMode		= 1;
	pHalData->UsbTxAggDescNum	= 0x1;	/* only 4 bits */
#endif

#ifdef CONFIG_USB_RX_AGGREGATION
	pHalData->rxagg_mode = registry_par->usb_rxagg_mode;

	if ((pHalData->rxagg_mode != RX_AGG_DMA) && (pHalData->rxagg_mode != RX_AGG_USB))
		pHalData->rxagg_mode = RX_AGG_DMA;

	if (pHalData->rxagg_mode	== RX_AGG_DMA) {
		pHalData->rxagg_dma_size	= 48; /* uint: 128b, 0x0A = 10 = MAX_RX_DMA_BUFFER_SIZE/2/pHalData->UsbBulkOutSize */
		pHalData->rxagg_dma_timeout	= 0x4; /* 6, absolute time = 34ms/(2^6) */
	} else if (pHalData->rxagg_mode == RX_AGG_USB) {
		pHalData->rxagg_usb_size	= 16; /* unit: 512b */
		pHalData->rxagg_usb_timeout	= 0x6;
	}
#endif

	HalUsbSetQueuePipeMapping8188EUsb(padapter,
			  pdvobjpriv->RtNumInPipes, pdvobjpriv->RtNumOutPipes);

}

static u32 _InitPowerOn_8188EU(_adapter *padapter)
{
	u16 value16;
	/* HW Power on sequence */
	u8 bMacPwrCtrlOn = _FALSE;


	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (bMacPwrCtrlOn == _TRUE)
		return _SUCCESS;

	if (!HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, Rtl8188E_NIC_PWR_ON_FLOW)) {
		RTW_ERR("%s: run power on flow fail\n", __func__);
		return _FAIL;
	}

	/* Enable MAC DMA/WMAC/SCHEDULE/SEC block */
	/* Set CR bit10 to enable 32k calibration. Suggested by SD1 Gimmy. Added by tynli. 2011.08.31. */
	rtw_write16(padapter, REG_CR, 0x00);  /* suggseted by zhouzhou, by page, 20111230 */


	/* Enable MAC DMA/WMAC/SCHEDULE/SEC block */
	value16 = rtw_read16(padapter, REG_CR);
	value16 |= (HCI_TXDMA_EN | HCI_RXDMA_EN | TXDMA_EN | RXDMA_EN
		    | PROTOCOL_EN | SCHEDULE_EN | ENSEC | CALTMR_EN);
	/* for SDIO - Set CR bit10 to enable 32k calibration. Suggested by SD1 Gimmy. Added by tynli. 2011.08.31. */

	rtw_write16(padapter, REG_CR, value16);

	bMacPwrCtrlOn = _TRUE;
	rtw_hal_set_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);

	return _SUCCESS;

}


static void _dbg_dump_macreg(_adapter *padapter)
{
	u32 offset = 0;
	u32 val32 = 0;
	u32 index = 0 ;
	for (index = 0; index < 64; index++) {
		offset = index * 4;
		val32 = rtw_read32(padapter, offset);
		RTW_INFO("offset : 0x%02x ,val:0x%08x\n", offset, val32);
	}
}


static void _InitPABias(_adapter *padapter)
{
	HAL_DATA_TYPE		*pHalData	= GET_HAL_DATA(padapter);
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
#ifdef CONFIG_BT_COEXIST
static void _InitBTCoexist(_adapter *padapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);
	u8 u1Tmp;

	if (pbtpriv->BT_Coexist && pbtpriv->BT_CoexistType == BT_CSR_BC4) {

		/* #if MP_DRIVER != 1 */
		if (padapter->registrypriv.mp_mode == 0) {
			if (pbtpriv->BT_Ant_isolation) {
				rtw_write8(padapter, REG_GPIO_MUXCFG, 0xa0);
				RTW_INFO("BT write 0x%x = 0x%x\n", REG_GPIO_MUXCFG, 0xa0);
			}
		}
		/* #endif */

		u1Tmp = rtw_read8(padapter, 0x4fd) & BIT0;
		u1Tmp = u1Tmp |
			((pbtpriv->BT_Ant_isolation == 1) ? 0 : BIT1) |
			((pbtpriv->BT_Service == BT_SCO) ? 0 : BIT2);
		rtw_write8(padapter, 0x4fd, u1Tmp);
		RTW_INFO("BT write 0x%x = 0x%x for non-isolation\n", 0x4fd, u1Tmp);


		rtw_write32(padapter, REG_BT_COEX_TABLE + 4, 0xaaaa9aaa);
		RTW_INFO("BT write 0x%x = 0x%x\n", REG_BT_COEX_TABLE + 4, 0xaaaa9aaa);

		rtw_write32(padapter, REG_BT_COEX_TABLE + 8, 0xffbd0040);
		RTW_INFO("BT write 0x%x = 0x%x\n", REG_BT_COEX_TABLE + 8, 0xffbd0040);

		rtw_write32(padapter,  REG_BT_COEX_TABLE + 0xc, 0x40000010);
		RTW_INFO("BT write 0x%x = 0x%x\n", REG_BT_COEX_TABLE + 0xc, 0x40000010);

		/* Config to 1T1R */
		u1Tmp =  rtw_read8(padapter, rOFDM0_TRxPathEnable);
		u1Tmp &= ~(BIT1);
		rtw_write8(padapter, rOFDM0_TRxPathEnable, u1Tmp);
		RTW_INFO("BT write 0xC04 = 0x%x\n", u1Tmp);

		u1Tmp = rtw_read8(padapter, rOFDM1_TRxPathEnable);
		u1Tmp &= ~(BIT1);
		rtw_write8(padapter, rOFDM1_TRxPathEnable, u1Tmp);
		RTW_INFO("BT write 0xD04 = 0x%x\n", u1Tmp);

	}
}
#endif



/* ---------------------------------------------------------------
 *
 *	MAC init functions
 *
 * --------------------------------------------------------------- */

/* Shall USB interface init this? */
static VOID
_InitInterrupt(
	IN  PADAPTER Adapter
)
{
	u32	imr, imr_ex;
	u8  usb_opt;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	struct dvobj_priv *pdev = adapter_to_dvobj(Adapter);
#endif

	/* HISR write one to clear */
	rtw_write32(Adapter, REG_HISR_88E, 0xFFFFFFFF);
	/* HIMR -	 */
	imr = IMR_PSTIMEOUT_88E | IMR_TBDER_88E | IMR_CPWM_88E | IMR_CPWM2_88E ;
	rtw_write32(Adapter, REG_HIMR_88E, imr);
	pHalData->IntrMask[0] = imr;

	imr_ex = IMR_TXERR_88E | IMR_RXERR_88E | IMR_TXFOVW_88E | IMR_RXFOVW_88E;
	rtw_write32(Adapter, REG_HIMRE_88E, imr_ex);
	pHalData->IntrMask[1] = imr_ex;

#ifdef CONFIG_SUPPORT_USB_INT
	/* REG_USB_SPECIAL_OPTION - BIT(4) */
	/* 0; Use interrupt endpoint to upload interrupt pkt */
	/* 1; Use bulk endpoint to upload interrupt pkt,	 */
	usb_opt = rtw_read8(Adapter, REG_USB_SPECIAL_OPTION);

	if ((IS_FULL_SPEED_USB(Adapter))
#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	    || (pdev->RtInPipe[REALTEK_USB_IN_INT_EP_IDX] == 0x05)
#endif
	   )
		usb_opt = usb_opt & (~INT_BULK_SEL);
	else
		usb_opt = usb_opt | (INT_BULK_SEL);

	rtw_write8(Adapter, REG_USB_SPECIAL_OPTION, usb_opt);

#endif/* CONFIG_SUPPORT_USB_INT */

}


static VOID
_InitQueueReservedPage(
	IN  PADAPTER Adapter
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	struct registry_priv	*pregistrypriv = &Adapter->registrypriv;
	u32			outEPNum	= (u32)pHalData->OutEpNumber;
	u32			numHQ		= 0;
	u32			numLQ		= 0;
	u32			numNQ		= 0;
	u32			numPubQ	= 0x00;
	u32			value32;
	u8			value8;
	BOOLEAN			bWiFiConfig	= pregistrypriv->wifi_spec;

	if (bWiFiConfig || pregistrypriv->qos_opt_enable) {
		if (pHalData->OutEpQueueSel & TX_SELE_HQ)
			numHQ =  WMM_NORMAL_PAGE_NUM_HPQ_88E;

		if (pHalData->OutEpQueueSel & TX_SELE_LQ)
			numLQ = WMM_NORMAL_PAGE_NUM_LPQ_88E;

		/* NOTE: This step shall be proceed before writting REG_RQPN. */
		if (pHalData->OutEpQueueSel & TX_SELE_NQ)
			numNQ = WMM_NORMAL_PAGE_NUM_NPQ_88E;
	} else {
		if (pHalData->OutEpQueueSel & TX_SELE_HQ)
			numHQ = NORMAL_PAGE_NUM_HPQ_88E;

		if (pHalData->OutEpQueueSel & TX_SELE_LQ)
			numLQ = NORMAL_PAGE_NUM_LPQ_88E;

		/* NOTE: This step shall be proceed before writting REG_RQPN.		 */
		if (pHalData->OutEpQueueSel & TX_SELE_NQ)
			numNQ = NORMAL_PAGE_NUM_NPQ_88E;
	}

	value8 = (u8)_NPQ(numNQ);
	rtw_write8(Adapter, REG_RQPN_NPQ, value8);

	numPubQ = TX_TOTAL_PAGE_NUMBER_88E(Adapter) - numHQ - numLQ - numNQ;

	/* TX DMA */
	value32 = _HPQ(numHQ) | _LPQ(numLQ) | _PUBQ(numPubQ) | LD_RQPN;
	rtw_write32(Adapter, REG_RQPN, value32);
}

static VOID
_InitTxBufferBoundary(
	IN PADAPTER Adapter,
	IN u8 txpktbuf_bndy
)
{
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	/* HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter); */

	/* u16	txdmactrl; */

	rtw_write8(Adapter, REG_BCNQ_BDNY, txpktbuf_bndy);
	rtw_write8(Adapter, REG_MGQ_BDNY, txpktbuf_bndy);
	rtw_write8(Adapter, REG_WMAC_LBK_BF_HD, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TRXFF_BNDY, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TDECTRL + 1, txpktbuf_bndy);

}

static VOID
_InitPageBoundary(
	IN  PADAPTER Adapter
)
{
	/* RX Page Boundary	 */
	u16 rxff_bndy = 0;

	rxff_bndy = MAX_RX_DMA_BUFFER_SIZE_88E(Adapter) - 1;

#if 0

	/* RX Page Boundary */
	/* srand(static_cast<unsigned int>(time(NULL)) ); */
	if (bSupportRemoteWakeUp) {
		Offset = MAX_RX_DMA_BUFFER_SIZE_88E(Adapter) + MAX_TX_REPORT_BUFFER_SIZE - MAX_SUPPORT_WOL_PATTERN_NUM(Adapter) * WKFMCAM_SIZE;
		Offset = Offset / 128; /* RX page size = 128 byte */
		rxff_bndy = (Offset * 128) - 1;
	} else

#endif
		rtw_write16(Adapter, (REG_TRXFF_BNDY + 2), rxff_bndy);
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
	IN	PADAPTER Adapter
)
{
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
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
	_InitNormalChipRegPriority(Adapter, beQ, bkQ, viQ, voQ, mgtQ, hiQ);
}

static VOID
_InitQueuePriority(
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



static VOID
_InitHardwareDropIncorrectBulkOut(
	IN  PADAPTER Adapter
)
{
#ifdef ENABLE_USB_DROP_INCORRECT_OUT
	u32	value32 = rtw_read32(Adapter, REG_TXDMA_OFFSET_CHK);
	value32 |= DROP_DATA_EN;
	rtw_write32(Adapter, REG_TXDMA_OFFSET_CHK, value32);
#endif
}

static VOID
_InitNetworkType(
	IN  PADAPTER Adapter
)
{
	u32	value32;

	value32 = rtw_read32(Adapter, REG_CR);
	/* TODO: use the other function to set network type */
	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AP);

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
	/* u4Byte			value32; */
	/* u16			value16; */
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	/* pHalData->ReceiveConfig = AAP | APM | AM | AB | APP_ICV | ADF | AMF | APP_FCS | HTC_LOC_CTRL | APP_MIC | APP_PHYSTS; */
	/* pHalData->ReceiveConfig = */
	/* RCR_AAP | RCR_APM | RCR_AM | RCR_AB |RCR_CBSSID_DATA| RCR_CBSSID_BCN| RCR_APP_ICV | RCR_AMF | RCR_HTC_LOC_CTRL | RCR_APP_MIC | RCR_APP_PHYSTS;	  */
	/* don't turn on AAP, it will allow all packets to driver */
	pHalData->ReceiveConfig = RCR_APM | RCR_AM | RCR_AB | RCR_CBSSID_DATA | RCR_CBSSID_BCN | RCR_APP_ICV | RCR_AMF | RCR_HTC_LOC_CTRL | RCR_APP_MIC | RCR_APP_PHYST_RXFF;

#if (1 == RTL8188E_RX_PACKET_INCLUDE_CRC)
	pHalData->ReceiveConfig |= ACRC32;
#endif

	/* some REG_RCR will be modified later by phy_ConfigMACWithHeaderFile() */
	rtw_write32(Adapter, REG_RCR, pHalData->ReceiveConfig);

	/* Accept all multicast address */
	rtw_write32(Adapter, REG_MAR, 0xFFFFFFFF);
	rtw_write32(Adapter, REG_MAR + 4, 0xFFFFFFFF);


	/* Accept all data frames */
	/* value16 = 0xFFFF; */
	/* rtw_write16(Adapter, REG_RXFLTMAP2, value16); */

	/* 2010.09.08 hpfan */
	/* Since ADF is removed from RCR, ps-poll will not be indicate to driver, */
	/* RxFilterMap should mask ps-poll to gurantee AP mode can rx ps-poll. */
	/* value16 = 0x400; */
	/* rtw_write16(Adapter, REG_RXFLTMAP1, value16); */

	/* Accept all management frames */
	/* value16 = 0xFFFF; */
	/* rtw_write16(Adapter, REG_RXFLTMAP0, value16); */

	/* enable RX_SHIFT bits */
	/* rtw_write8(Adapter, REG_TRXDMA_CTRL, rtw_read8(Adapter, REG_TRXDMA_CTRL)|BIT(1));	 */

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
	/* m_spIoBase->rtw_write8(REG_CFEND_TH, 0x1); */

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


static VOID
_InitBeaconMaxError(
	IN  PADAPTER	Adapter,
	IN	BOOLEAN		InfraMode
)
{

}


#ifdef CONFIG_LED
static void _InitHWLed(PADAPTER Adapter)
{
	struct led_priv *pledpriv = &(Adapter->ledpriv);

	if (pledpriv->LedStrategy != HW_LED)
		return;

	/* HW led control
	 * to do ....
	 * must consider cases of antenna diversity/ commbo card/solo card/mini card */

}
#endif /* CONFIG_LED */

static VOID
_InitRDGSetting(
	IN	PADAPTER Adapter
)
{
	rtw_write8(Adapter, REG_RD_CTRL, 0xFF);
	rtw_write16(Adapter, REG_RD_NAV_NXT, 0x200);
	rtw_write8(Adapter, REG_RD_RESP_PKT_TH, 0x05);
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

/*-----------------------------------------------------------------------------
 * Function:	usb_AggSettingTxUpdate()
 *
 * Overview:	Seperate TX/RX parameters update independent for TP detection and
 *			dynamic TX/RX aggreagtion parameters update.
 *
 * Input:			PADAPTER
 *
 * Output/Return:	NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	12/10/2010	MHC		Seperate to smaller function.
 *
 *---------------------------------------------------------------------------*/
static VOID
usb_AggSettingTxUpdate(
	IN	PADAPTER			Adapter
)
{
#ifdef CONFIG_USB_TX_AGGREGATION
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	/* PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);	 */
	u32			value32;

	if (Adapter->registrypriv.wifi_spec)
		pHalData->UsbTxAggMode = _FALSE;

	if (pHalData->UsbTxAggMode) {
		value32 = rtw_read32(Adapter, REG_TDECTRL);
		value32 = value32 & ~(BLK_DESC_NUM_MASK << BLK_DESC_NUM_SHIFT);
		value32 |= ((pHalData->UsbTxAggDescNum & BLK_DESC_NUM_MASK) << BLK_DESC_NUM_SHIFT);

		rtw_write32(Adapter, REG_TDECTRL, value32);
	}

#endif
}	/* usb_AggSettingTxUpdate */


/*-----------------------------------------------------------------------------
 * Function:	usb_AggSettingRxUpdate()
 *
 * Overview:	Seperate TX/RX parameters update independent for TP detection and
 *			dynamic TX/RX aggreagtion parameters update.
 *
 * Input:			PADAPTER
 *
 * Output/Return:	NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	12/10/2010	MHC		Seperate to smaller function.
 *
 *---------------------------------------------------------------------------*/
static VOID
usb_AggSettingRxUpdate(
	IN	PADAPTER			Adapter
)
{
#ifdef CONFIG_USB_RX_AGGREGATION
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	/* PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo); */
	u8			valueDMA;
	u8			valueUSB;

	valueDMA = rtw_read8(Adapter, REG_TRXDMA_CTRL);
	valueUSB = rtw_read8(Adapter, REG_USB_SPECIAL_OPTION);

	switch (pHalData->rxagg_mode) {
	case RX_AGG_DMA:
		valueDMA |= RXDMA_AGG_EN;
		valueUSB &= ~USB_AGG_EN;
		break;
	case RX_AGG_USB:
		valueDMA &= ~RXDMA_AGG_EN;
		valueUSB |= USB_AGG_EN;
		break;
	case RX_AGG_MIX:
		valueDMA |= RXDMA_AGG_EN;
		valueUSB |= USB_AGG_EN;
		break;
	case RX_AGG_DISABLE:
	default:
		valueDMA &= ~RXDMA_AGG_EN;
		valueUSB &= ~USB_AGG_EN;
		break;
	}

	rtw_write8(Adapter, REG_TRXDMA_CTRL, valueDMA);
	rtw_write8(Adapter, REG_USB_SPECIAL_OPTION, valueUSB);

	switch (pHalData->rxagg_mode) {
	case RX_AGG_DMA:
		rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH, pHalData->rxagg_dma_size);
		rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH + 1, pHalData->rxagg_dma_timeout);
		break;
	case RX_AGG_USB:
		rtw_write8(Adapter, REG_USB_AGG_TH, pHalData->rxagg_usb_size);
		rtw_write8(Adapter, REG_USB_AGG_TO, pHalData->rxagg_usb_timeout);
		break;
	case RX_AGG_MIX:
		rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH, pHalData->rxagg_dma_size);
		rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH + 1, pHalData->rxagg_dma_timeout & 0x1F); /* 0x280[12:8] */

		rtw_write8(Adapter, REG_USB_AGG_TH, pHalData->rxagg_usb_size);
		rtw_write8(Adapter, REG_USB_AGG_TO, pHalData->rxagg_usb_timeout);

		break;
	case RX_AGG_DISABLE:
	default:
		/* TODO: */
		break;
	}

	switch (PBP_128) {
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
		/* RT_ASSERT(FALSE, ("RX_PAGE_SIZE_REG_VALUE definition is incorrect!\n")); */
		break;
	}
#endif
}	/* usb_AggSettingRxUpdate */

static VOID
InitUsbAggregationSetting(
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
VOID
HalRxAggr8188EUsb(
	IN  PADAPTER Adapter,
	IN BOOLEAN	Value
)
{
#if 0/* USB_RX_AGGREGATION_92C */

	PMGNT_INFO		pMgntInfo = &Adapter->MgntInfo;
	u1Byte			valueDMATimeout;
	u1Byte			valueDMAPageCount;
	u1Byte			valueUSBTimeout;
	u1Byte			valueUSBBlockCount;

	/* selection to prevent bad TP. */
	if (IS_WIRELESS_MODE_B(Adapter) || IS_WIRELESS_MODE_G(Adapter) || IS_WIRELESS_MODE_A(Adapter) || pMgntInfo->bWiFiConfg) {
		/* 2010.04.27 hpfan */
		/* Adjust RxAggrTimeout to close to zero disable RxAggr, suggested by designer */
		/* Timeout value is calculated by 34 / (2^n) */
		valueDMATimeout = 0x0f;
		valueDMAPageCount = 0x01;
		valueUSBTimeout = 0x0f;
		valueUSBBlockCount = 0x01;
		rtw_hal_set_hwreg(Adapter, HW_VAR_RX_AGGR_PGTO, (pu1Byte)&valueDMATimeout);
		rtw_hal_set_hwreg(Adapter, HW_VAR_RX_AGGR_PGTH, (pu1Byte)&valueDMAPageCount);
		rtw_hal_set_hwreg(Adapter, HW_VAR_RX_AGGR_USBTO, (pu1Byte)&valueUSBTimeout);
		rtw_hal_set_hwreg(Adapter, HW_VAR_RX_AGGR_USBTH, (pu1Byte)&valueUSBBlockCount);
	} else {
		rtw_hal_set_hwreg(Adapter, HW_VAR_RX_AGGR_USBTO, (pu1Byte)&pMgntInfo->RegRxAggBlockTimeout);
		rtw_hal_set_hwreg(Adapter, HW_VAR_RX_AGGR_USBTH, (pu1Byte)&pMgntInfo->RegRxAggBlockCount);
	}

#endif
}

/*-----------------------------------------------------------------------------
 * Function:	USB_AggModeSwitch()
 *
 * Overview:	When RX traffic is more than 40M, we need to adjust some parameters to increase
 *			RX speed by increasing batch indication size. This will decrease TCP ACK speed, we
 *			need to monitor the influence of FTP/network share.
 *			For TX mode, we are still ubder investigation.
 *
 * Input:		PADAPTER
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	12/10/2010	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
VOID
USB_AggModeSwitch(
	IN	PADAPTER			Adapter
)
{
#if 0
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);

	/* pHalData->UsbRxHighSpeedMode = FALSE; */
	/* How to measure the RX speed? We assume that when traffic is more than */
	if (pMgntInfo->bRegAggDMEnable == FALSE) {
		return;	/* Inf not support. */
	}


	if (pMgntInfo->LinkDetectInfo.bHigherBusyRxTraffic == TRUE &&
	    pHalData->UsbRxHighSpeedMode == FALSE) {
		pHalData->UsbRxHighSpeedMode = TRUE;
	} else if (pMgntInfo->LinkDetectInfo.bHigherBusyRxTraffic == FALSE &&
		   pHalData->UsbRxHighSpeedMode == TRUE) {
		pHalData->UsbRxHighSpeedMode = FALSE;
	} else
		return;


#if USB_RX_AGGREGATION_92C
	if (pHalData->UsbRxHighSpeedMode == TRUE) {
		/* 2010/12/10 MH The parameter is tested by SD1 engineer and SD3 channel emulator. */
		/* USB mode */
#if (RT_PLATFORM == PLATFORM_LINUX)
		if (pMgntInfo->LinkDetectInfo.bTxBusyTraffic) {
			pHalData->RxAggBlockCount	= 16;
			pHalData->RxAggBlockTimeout	= 7;
		} else
#endif
		{
			pHalData->RxAggBlockCount	= 40;
			pHalData->RxAggBlockTimeout	= 5;
		}
		/* Mix mode */
		pHalData->RxAggPageCount	= 72;
		pHalData->RxAggPageTimeout	= 6;
	} else {
		/* USB mode */
		pHalData->RxAggBlockCount	= pMgntInfo->RegRxAggBlockCount;
		pHalData->RxAggBlockTimeout	= pMgntInfo->RegRxAggBlockTimeout;
		/* Mix mode */
		pHalData->RxAggPageCount		= pMgntInfo->RegRxAggPageCount;
		pHalData->RxAggPageTimeout	= pMgntInfo->RegRxAggPageTimeout;
	}

	if (pHalData->RxAggBlockCount > MAX_RX_AGG_BLKCNT)
		pHalData->RxAggBlockCount = MAX_RX_AGG_BLKCNT;
#if (OS_WIN_FROM_VISTA(OS_VERSION)) || (RT_PLATFORM == PLATFORM_LINUX)	/* do not support WINXP to prevent usbehci.sys BSOD */
	if (IS_WIRELESS_MODE_N_24G(Adapter) || IS_WIRELESS_MODE_N_5G(Adapter)) {
		/*  */
		/* 2010/12/24 MH According to V1012 QC IOT test, XP BSOD happen when running chariot test */
		/* with the aggregation dynamic change!! We need to disable the function to prevent it is broken */
		/* in usbehci.sys. */
		/*  */
		usb_AggSettingRxUpdate_8188E(Adapter);

		/* 2010/12/27 MH According to designer's suggstion, we can only modify Timeout value. Otheriwse */
		/* there might many HW incorrect behavior, the XP BSOD at usbehci.sys may be relative to the */
		/* issue. Base on the newest test, we can not enable block cnt > 30, otherwise XP usbehci.sys may */
		/* BSOD. */
	}
#endif

#endif
#endif
}	/* USB_AggModeSwitch */

static VOID
_InitOperationMode(
	IN	PADAPTER			Adapter
)
{
#if 0/* gtest */
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	u1Byte				regBwOpMode = 0;
	u4Byte				regRATR = 0, regRRSR = 0;


	/* 1 This part need to modified according to the rate set we filtered!! */
	/*  */
	/* Set RRSR, RATR, and REG_BWOPMODE registers */
	/*  */
	switch (Adapter->RegWirelessMode) {
	case WIRELESS_MODE_B:
		regBwOpMode = BW_OPMODE_20MHZ;
		regRATR = RATE_ALL_CCK;
		regRRSR = RATE_ALL_CCK;
		break;
	case WIRELESS_MODE_A:
		regBwOpMode = BW_OPMODE_5G | BW_OPMODE_20MHZ;
		regRATR = RATE_ALL_OFDM_AG;
		regRRSR = RATE_ALL_OFDM_AG;
		break;
	case WIRELESS_MODE_G:
		regBwOpMode = BW_OPMODE_20MHZ;
		regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		break;
	case WIRELESS_MODE_AUTO:
		if (Adapter->bInHctTest) {
			regBwOpMode = BW_OPMODE_20MHZ;
			regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
			regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		} else {
			regBwOpMode = BW_OPMODE_20MHZ;
			regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
			regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		}
		break;
	case WIRELESS_MODE_N_24G:
		/* It support CCK rate by default. */
		/* CCK rate will be filtered out only when associated AP does not support it. */
		regBwOpMode = BW_OPMODE_20MHZ;
		regRATR = RATE_ALL_CCK | RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
		regRRSR = RATE_ALL_CCK | RATE_ALL_OFDM_AG;
		break;
	case WIRELESS_MODE_N_5G:
		regBwOpMode = BW_OPMODE_5G;
		regRATR = RATE_ALL_OFDM_AG | RATE_ALL_OFDM_1SS | RATE_ALL_OFDM_2SS;
		regRRSR = RATE_ALL_OFDM_AG;
		break;

	default: /* for MacOSX compiler warning. */
		break;
	}

	/* Ziv ???????? */
	/* PlatformEFIOWrite4Byte(Adapter, REG_INIRTS_RATE_SEL, regRRSR); */
	PlatformEFIOWrite1Byte(Adapter, REG_BWOPMODE, regBwOpMode);
#endif
}


static VOID
_InitBeaconParameters(
	IN  PADAPTER Adapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	rtw_write16(Adapter, REG_BCN_CTRL, 0x1010);

	/* TODO: Remove these magic number */
	/* TBTT setup time:128 us */
	rtw_write8(Adapter, REG_TBTT_PROHIBIT, 0x04);

	/*TBTT hold time :4ms 0x540[19:8]*/
	rtw_write8(Adapter, REG_TBTT_PROHIBIT + 1,
		TBTT_PROBIHIT_HOLD_TIME & 0xFF);
	rtw_write8(Adapter, REG_TBTT_PROHIBIT + 2,
		(rtw_read8(Adapter, REG_TBTT_PROHIBIT + 2) & 0xF0) | (TBTT_PROBIHIT_HOLD_TIME >> 8));
	rtw_write8(Adapter, REG_DRVERLYINT, DRIVER_EARLY_INT_TIME_8188E);/* 5ms */
	rtw_write8(Adapter, REG_BCNDMATIM, BCN_DMA_ATIME_INT_TIME_8188E); /* 2ms */

	/* Suggested by designer timchen. Change beacon AIFS to the largest number */
	/* beacause test chip does not contension before sending beacon. by tynli. 2009.11.03 */
	rtw_write16(Adapter, REG_BCNTCFG, 0x660F);

	pHalData->RegBcnCtrlVal = rtw_read8(Adapter, REG_BCN_CTRL);
	pHalData->RegTxPause = rtw_read8(Adapter, REG_TXPAUSE);
	pHalData->RegFwHwTxQCtrl = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL + 2);
	pHalData->RegReg542 = rtw_read8(Adapter, REG_TBTT_PROHIBIT + 2);
	pHalData->RegCR_1 = rtw_read8(Adapter, REG_CR + 1);
}

static VOID
_InitRFType(
	IN	PADAPTER Adapter
)
{
	struct registry_priv	*pregpriv = &Adapter->registrypriv;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

#if	DISABLE_BB_RF
	pHalData->rf_chip	= RF_PSEUDO_11N;
	return;
#endif
	pHalData->rf_chip	= RF_6052;

	/* TODO: Consider that EEPROM set 92CU to 1T1R later. */
	/* Force to overwrite setting according to chip version. Ignore EEPROM setting. */
	/* pHalData->RF_Type = is92CU ? RF_2T2R : RF_1T1R; */
	RTW_INFO("Set RF Chip ID to RF_6052 and RF type to %d.\n", pHalData->rf_type);

}


static VOID
_BeaconFunctionEnable(
	IN	PADAPTER		Adapter,
	IN	BOOLEAN			Enable,
	IN	BOOLEAN			Linked
)
{
	rtw_write8(Adapter, REG_BCN_CTRL, (BIT4 | BIT3 | BIT1));
	/* SetBcnCtrlReg(Adapter, (BIT4 | BIT3 | BIT1), 0x00); */

	rtw_write8(Adapter, REG_RD_CTRL + 1, 0x6F);
}


/* Set CCK and OFDM Block "ON" */
static VOID _BBTurnOnBlock(
	IN	PADAPTER		Adapter
)
{
#if (DISABLE_BB_RF)
	return;
#endif

	phy_set_bb_reg(Adapter, rFPGA0_RFMOD, bCCKEn, 0x1);
	phy_set_bb_reg(Adapter, rFPGA0_RFMOD, bOFDMEn, 0x1);
}

static VOID _RfPowerSave(
	IN	PADAPTER		Adapter
)
{
#if 0
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);
	PMGNT_INFO		pMgntInfo	= &(Adapter->MgntInfo);
	u1Byte			eRFPath;

#if (DISABLE_BB_RF)
	return;
#endif

	if (pMgntInfo->RegRfOff == TRUE) { /* User disable RF via registry. */
		MgntActSet_RF_State(Adapter, eRfOff, RF_CHANGE_BY_SW);
		/* Those action will be discard in MgntActSet_RF_State because off the same state */
		for (eRFPath = 0; eRFPath < pHalData->NumTotalRFPath; eRFPath++)
			phy_set_rf_reg(Adapter, eRFPath, 0x4, 0xC00, 0x0);
	} else if (pMgntInfo->RfOffReason > RF_CHANGE_BY_PS) { /* H/W or S/W RF OFF before sleep. */
		MgntActSet_RF_State(Adapter, eRfOff, pMgntInfo->RfOffReason);
	} else {
		pHalData->eRFPowerState = eRfOn;
		pMgntInfo->RfOffReason = 0;
		if (Adapter->bInSetPower || Adapter->bResetInProgress)
			PlatformUsbEnableInPipes(Adapter);
	}
#endif
}

enum {
	Antenna_Lfet = 1,
	Antenna_Right = 2,
};

static VOID
_InitAntenna_Selection(IN	PADAPTER Adapter)
{
#ifdef CONFIG_ANTENNA_DIVERSITY
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

	if (pHalData->AntDivCfg == 0)
		return;
	RTW_INFO("==>  %s ....\n", __FUNCTION__);

	rtw_write32(Adapter, REG_LEDCFG0, rtw_read32(Adapter, REG_LEDCFG0) | BIT23);
	phy_set_bb_reg(Adapter, rFPGA0_XAB_RFParameter, BIT13, 0x01);
#endif
}

/*
 * 2010/08/26 MH Add for selective suspend mode check.
 * If Efuse 0x0e bit1 is not enabled, we can not support selective suspend for Minicard and
 * slim card.
 *   */
static VOID
HalDetectSelectiveSuspendMode(
	IN PADAPTER				Adapter
)
{
#if 0
	u8	tmpvalue;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(Adapter);

	/* If support HW radio detect, we need to enable WOL ability, otherwise, we */
	/* can not use FW to notify host the power state switch. */

	EFUSE_ShadowRead(Adapter, 1, EEPROM_USB_OPTIONAL1, (u32 *)&tmpvalue);

	RTW_INFO("HalDetectSelectiveSuspendMode(): SS ");
	if (tmpvalue & BIT1)
		RTW_INFO("Enable\n");
	else {
		RTW_INFO("Disable\n");
		pdvobjpriv->RegUsbSS = _FALSE;
	}

	/* 2010/09/01 MH According to Dongle Selective Suspend INF. We can switch SS mode. */
	if (pdvobjpriv->RegUsbSS && !SUPPORT_HW_RADIO_DETECT(pHalData)) {
		/* PMGNT_INFO				pMgntInfo = &(Adapter->MgntInfo); */

		/* if (!pMgntInfo->bRegDongleSS)	 */
		/* { */
		pdvobjpriv->RegUsbSS = _FALSE;
		/* } */
	}
#endif
}	/* HalDetectSelectiveSuspendMode */
#if 0
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
HwSuspendModeEnable_88eu(
	IN	PADAPTER	pAdapter,
	IN	u8			Type
)
{
	/* PRT_USB_DEVICE 		pDevice = GET_RT_USB_DEVICE(pAdapter); */
	u16	reg = rtw_read16(pAdapter, REG_GPIO_MUXCFG);

	/* if (!pDevice->RegUsbSS) */
	{
		return;
	}

	/*  */
	/* 2010/08/23 MH According to Alfred's suggestion, we need to to prevent HW */
	/* to enter suspend mode automatically. Otherwise, it will shut down major power */
	/* domain and 8051 will stop. When we try to enter selective suspend mode, we */
	/* need to prevent HW to enter D2 mode aumotmatically. Another way, Host will */
	/* issue a S10 signal to power domain. Then it will cleat SIC setting(from Yngli). */
	/* We need to enable HW suspend mode when enter S3/S4 or disable. We need */
	/* to disable HW suspend mode for IPS/radio_off. */
	/*  */
	if (Type == _FALSE) {
		reg |= BIT14;
		rtw_write16(pAdapter, REG_GPIO_MUXCFG, reg);
		reg |= BIT12;
		rtw_write16(pAdapter, REG_GPIO_MUXCFG, reg);
	} else {
		reg &= (~BIT12);
		rtw_write16(pAdapter, REG_GPIO_MUXCFG, reg);
		reg &= (~BIT14);
		rtw_write16(pAdapter, REG_GPIO_MUXCFG, reg);
	}

}	/* HwSuspendModeEnable92Cu */
#endif

rt_rf_power_state RfOnOffDetect(IN	PADAPTER pAdapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(pAdapter);
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(pAdapter);
	u8	val8;
	rt_rf_power_state rfpowerstate = rf_off;

	if (pwrctl->bHWPowerdown) {
		val8 = rtw_read8(pAdapter, REG_HSISR);
		RTW_INFO("pwrdown, 0x5c(BIT7)=%02x\n", val8);
		rfpowerstate = (val8 & BIT7) ? rf_off : rf_on;
	} else { /* rf on/off */
		rtw_write8(pAdapter, REG_MAC_PINMUX_CFG, rtw_read8(pAdapter, REG_MAC_PINMUX_CFG) & ~(BIT3));
		val8 = rtw_read8(pAdapter, REG_GPIO_IO_SEL);
		RTW_INFO("GPIO_IN=%02x\n", val8);
		rfpowerstate = (val8 & BIT3) ? rf_on : rf_off;
	}
	return rfpowerstate;
}	/* HalDetectPwrDownMode */

void _ps_open_RF(_adapter *padapter);

u32 rtl8188eu_hal_init(PADAPTER Adapter)
{
	u8	value8 = 0;
	u16  value16;
	u8	txpktbuf_bndy;
	u32	status = _SUCCESS;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	struct pwrctrl_priv		*pwrctrlpriv = adapter_to_pwrctl(Adapter);
	struct registry_priv	*pregistrypriv = &Adapter->registrypriv;

	rt_rf_power_state		eRfPowerStateToSet;
#ifdef CONFIG_BT_COEXIST
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);
#endif

	u32 init_start_time = rtw_get_current_time();


#ifdef DBG_HAL_INIT_PROFILING

	enum HAL_INIT_STAGES {
		HAL_INIT_STAGES_BEGIN = 0,
		HAL_INIT_STAGES_INIT_PW_ON,
		HAL_INIT_STAGES_MISC01,
		HAL_INIT_STAGES_DOWNLOAD_FW,
		HAL_INIT_STAGES_MAC,
		HAL_INIT_STAGES_BB,
		HAL_INIT_STAGES_RF,
		HAL_INIT_STAGES_EFUSE_PATCH,
		HAL_INIT_STAGES_INIT_LLTT,

		HAL_INIT_STAGES_MISC02,
		HAL_INIT_STAGES_TURN_ON_BLOCK,
		HAL_INIT_STAGES_INIT_SECURITY,
		HAL_INIT_STAGES_MISC11,
		HAL_INIT_STAGES_INIT_HAL_DM,
		/* HAL_INIT_STAGES_RF_PS, */
		HAL_INIT_STAGES_IQK,
		HAL_INIT_STAGES_PW_TRACK,
		HAL_INIT_STAGES_LCK,
		/* HAL_INIT_STAGES_MISC21, */
		/* HAL_INIT_STAGES_INIT_PABIAS, */
#ifdef CONFIG_BT_COEXIST
		HAL_INIT_STAGES_BT_COEXIST,
#endif
		/* HAL_INIT_STAGES_ANTENNA_SEL, */
		/* HAL_INIT_STAGES_MISC31, */
		HAL_INIT_STAGES_END,
		HAL_INIT_STAGES_NUM
	};

	char *hal_init_stages_str[] = {
		"HAL_INIT_STAGES_BEGIN",
		"HAL_INIT_STAGES_INIT_PW_ON",
		"HAL_INIT_STAGES_MISC01",
		"HAL_INIT_STAGES_DOWNLOAD_FW",
		"HAL_INIT_STAGES_MAC",
		"HAL_INIT_STAGES_BB",
		"HAL_INIT_STAGES_RF",
		"HAL_INIT_STAGES_EFUSE_PATCH",
		"HAL_INIT_STAGES_INIT_LLTT",
		"HAL_INIT_STAGES_MISC02",
		"HAL_INIT_STAGES_TURN_ON_BLOCK",
		"HAL_INIT_STAGES_INIT_SECURITY",
		"HAL_INIT_STAGES_MISC11",
		"HAL_INIT_STAGES_INIT_HAL_DM",
		/* "HAL_INIT_STAGES_RF_PS", */
		"HAL_INIT_STAGES_IQK",
		"HAL_INIT_STAGES_PW_TRACK",
		"HAL_INIT_STAGES_LCK",
		/* "HAL_INIT_STAGES_MISC21", */
#ifdef CONFIG_BT_COEXIST
		"HAL_INIT_STAGES_BT_COEXIST",
#endif
		/* "HAL_INIT_STAGES_ANTENNA_SEL", */
		/* "HAL_INIT_STAGES_MISC31", */
		"HAL_INIT_STAGES_END",
	};

	int hal_init_profiling_i;
	u32 hal_init_stages_timestamp[HAL_INIT_STAGES_NUM]; /* used to record the time of each stage's starting point */

	for (hal_init_profiling_i = 0; hal_init_profiling_i < HAL_INIT_STAGES_NUM; hal_init_profiling_i++)
		hal_init_stages_timestamp[hal_init_profiling_i] = 0;

#define HAL_INIT_PROFILE_TAG(stage) do { hal_init_stages_timestamp[(stage)] = rtw_get_current_time(); } while (0)
#else
#define HAL_INIT_PROFILE_TAG(stage) do {} while (0)
#endif /* DBG_HAL_INIT_PROFILING */




	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_BEGIN);

	if (pwrctrlpriv->bkeepfwalive) {
		_ps_open_RF(Adapter);

		if (pHalData->bIQKInitialized) {
			/*			PHY_IQCalibrate(padapter, _TRUE); */
			phy_iq_calibrate_8188e(Adapter, _TRUE);
		} else {
			/*			PHY_IQCalibrate(padapter, _FALSE); */
			phy_iq_calibrate_8188e(Adapter, _FALSE);
			pHalData->bIQKInitialized = _TRUE;
		}

		/*		dm_check_txpowertracking(padapter);
		 *		phy_lc_calibrate(padapter); */
		odm_txpowertracking_check(&pHalData->odmpriv);
		phy_lc_calibrate_8188e(&pHalData->odmpriv);

		goto exit;
	}


	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_PW_ON);
	status = rtw_hal_power_on(Adapter);
	if (status == _FAIL) {
		goto exit;
	}

	/* Set RF type for BB/RF configuration	 */
	_InitRFType(Adapter);/* ->_ReadRFType() */

	/* Save target channel */
	/* <Roger_Notes> Current Channel will be updated again later. */
	pHalData->current_channel = 6;/* default set to 6 */
	if (pwrctrlpriv->reg_rfoff == _TRUE)
		pwrctrlpriv->rf_pwrstate = rf_off;

	/* 2010/08/09 MH We need to check if we need to turnon or off RF after detecting */
	/* HW GPIO pin. Before PHY_RFConfig8192C. */
	/* HalDetectPwrDownMode(Adapter); */
	/* 2010/08/26 MH If Efuse does not support sective suspend then disable the function. */
	/*HalDetectSelectiveSuspendMode(Adapter);*/

	if (!pregistrypriv->wifi_spec)
		txpktbuf_bndy = TX_PAGE_BOUNDARY_88E(Adapter);
	else {
		/* for WMM */
		txpktbuf_bndy = WMM_NORMAL_TX_PAGE_BOUNDARY_88E(Adapter);
	}

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC01);
	_InitQueueReservedPage(Adapter);
	_InitQueuePriority(Adapter);
	_InitPageBoundary(Adapter);
	_InitTransferPageSize(Adapter);

#ifdef CONFIG_IOL_IOREG_CFG
	_InitTxBufferBoundary(Adapter, 0);
#endif



	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_DOWNLOAD_FW);
	if (Adapter->registrypriv.mp_mode == 0) {
		status = rtl8188e_FirmwareDownload(Adapter, _FALSE);
		if (status != _SUCCESS) {
			RTW_INFO("%s: Download Firmware failed!!\n", __FUNCTION__);
			Adapter->bFWReady = _FALSE;
			pHalData->fw_ractrl = _FALSE;
			return status;
		} else {
			Adapter->bFWReady = _TRUE;
#ifdef CONFIG_SFW_SUPPORTED
			pHalData->fw_ractrl = IS_VENDOR_8188E_I_CUT_SERIES(Adapter) ? _TRUE : _FALSE;
#else
			pHalData->fw_ractrl = _FALSE;
#endif
		}
	}

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MAC);
#if (HAL_MAC_ENABLE == 1)
	status = PHY_MACConfig8188E(Adapter);
	if (status == _FAIL) {
		RTW_INFO(" ### Failed to init MAC ......\n ");
		goto exit;
	}
#endif

	/*  */
	/* d. Initialize BB related configurations. */
	/*  */
	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_BB);
#if (HAL_BB_ENABLE == 1)
	status = PHY_BBConfig8188E(Adapter);
	if (status == _FAIL) {
		RTW_INFO(" ### Failed to init BB ......\n ");
		goto exit;
	}
#endif


	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_RF);
#if (HAL_RF_ENABLE == 1)
	status = PHY_RFConfig8188E(Adapter);
	if (status == _FAIL) {
		RTW_INFO(" ### Failed to init RF ......\n ");
		goto exit;
	}
#endif

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_EFUSE_PATCH);
#if defined(CONFIG_IOL_EFUSE_PATCH)
	status = rtl8188e_iol_efuse_patch(Adapter);
	if (status == _FAIL) {
		RTW_INFO("%s  rtl8188e_iol_efuse_patch failed\n", __FUNCTION__);
		goto exit;
	}
#endif

	_InitTxBufferBoundary(Adapter, txpktbuf_bndy);

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_LLTT);
	status =  InitLLTTable(Adapter, txpktbuf_bndy);
	if (status == _FAIL) {
		goto exit;
	}

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC02);
	/* Get Rx PHY status in order to report RSSI and others. */
	_InitDriverInfoSize(Adapter, DRVINFO_SZ);

	_InitInterrupt(Adapter);
	_InitNetworkType(Adapter);/* set msr	 */
	_InitWMACSetting(Adapter);
	_InitAdaptiveCtrl(Adapter);
	_InitEDCA(Adapter);
	/* _InitRateFallback(Adapter); */ /* just follow MP Team ???Georgia */
	_InitRetryFunction(Adapter);
	InitUsbAggregationSetting(Adapter);
	_InitOperationMode(Adapter);/* todo */
	_InitBeaconParameters(Adapter);
	_InitBeaconMaxError(Adapter, _TRUE);

	/*  */
	/* Init CR MACTXEN, MACRXEN after setting RxFF boundary REG_TRXFF_BNDY to patch */
	/* Hw bug which Hw initials RxFF boundry size to a value which is larger than the real Rx buffer size in 88E. */
	/*  */
	/* Enable MACTXEN/MACRXEN block */
	value16 = rtw_read16(Adapter, REG_CR);
	value16 |= (MACTXEN | MACRXEN);
	rtw_write8(Adapter, REG_CR, value16);

	_InitHardwareDropIncorrectBulkOut(Adapter);


	if (pHalData->bRDGEnable)
		_InitRDGSetting(Adapter);

	/* Enable TX Report & Tx Report Timer   */
	value8 = rtw_read8(Adapter, REG_TX_RPT_CTRL);
	rtw_write8(Adapter,  REG_TX_RPT_CTRL, (value8 | BIT1 | BIT0));

#if (RATE_ADAPTIVE_SUPPORT == 1)
	if (!pHalData->fw_ractrl) {
		/* Set MAX RPT MACID */
		rtw_write8(Adapter,  REG_TX_RPT_CTRL + 1, 2); /* FOR sta mode ,0: bc/mc ,1:AP */
		/* Tx RPT Timer. Unit: 32us */
		rtw_write16(Adapter, REG_TX_RPT_TIME, 0xCdf0);
	} else
#endif
	{
		/* disable tx rpt */
		rtw_write8(Adapter,  REG_TX_RPT_CTRL + 1, 0); /* FOR sta mode ,0: bc/mc ,1:AP */
	}
#if 0
	if (pHTInfo->bRDGEnable)
		_InitRDGSetting_8188E(Adapter);
#endif

#ifdef CONFIG_TX_EARLY_MODE
	if (pHalData->bEarlyModeEnable) {

		value8 = rtw_read8(Adapter, REG_EARLY_MODE_CONTROL);
#if RTL8188E_EARLY_MODE_PKT_NUM_10 == 1
		value8 = value8 | 0x1f;
#else
		value8 = value8 | 0xf;
#endif
		rtw_write8(Adapter, REG_EARLY_MODE_CONTROL, value8);

		rtw_write8(Adapter, REG_EARLY_MODE_CONTROL + 3, 0x80);

		value8 = rtw_read8(Adapter, REG_TCR + 1);
		value8 = value8 | 0x40;
		rtw_write8(Adapter, REG_TCR + 1, value8);
	} else
#endif
	{
		rtw_write8(Adapter, REG_EARLY_MODE_CONTROL, 0);
	}

	rtw_write32(Adapter, REG_MACID_NO_LINK_0, 0xFFFFFFFF);
	rtw_write32(Adapter, REG_MACID_NO_LINK_1, 0xFFFFFFFF);

#if defined(CONFIG_CONCURRENT_MODE) || defined(CONFIG_TX_MCAST2UNI)

#ifdef CONFIG_CHECK_AC_LIFETIME
	/* Enable lifetime check for the four ACs */
	rtw_write8(Adapter, REG_LIFETIME_CTRL, rtw_read8(Adapter, REG_LIFETIME_CTRL) | 0x0f);
#endif /* CONFIG_CHECK_AC_LIFETIME */

#ifdef CONFIG_TX_MCAST2UNI
	rtw_write16(Adapter, REG_PKT_VO_VI_LIFE_TIME, 0x0400);	/* unit: 256us. 256ms */
	rtw_write16(Adapter, REG_PKT_BE_BK_LIFE_TIME, 0x0400);	/* unit: 256us. 256ms */
#else	/* CONFIG_TX_MCAST2UNI */
	rtw_write16(Adapter, REG_PKT_VO_VI_LIFE_TIME, 0x3000);	/* unit: 256us. 3s */
	rtw_write16(Adapter, REG_PKT_BE_BK_LIFE_TIME, 0x3000);	/* unit: 256us. 3s */
#endif /* CONFIG_TX_MCAST2UNI */
#endif /* CONFIG_CONCURRENT_MODE || CONFIG_TX_MCAST2UNI */


#ifdef CONFIG_LED
	_InitHWLed(Adapter);
#endif /* CONFIG_LED */


	/*  */
	/* Joseph Note: Keep RfRegChnlVal for later use. */
	/*  */
	pHalData->RfRegChnlVal[0] = phy_query_rf_reg(Adapter, 0, RF_CHNLBW, bRFRegOffsetMask);
	pHalData->RfRegChnlVal[1] = phy_query_rf_reg(Adapter, 1, RF_CHNLBW, bRFRegOffsetMask);

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_TURN_ON_BLOCK);
	_BBTurnOnBlock(Adapter);
	/* NicIFSetMacAddress(padapter, padapter->PermanentAddress); */

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_SECURITY);
	invalidate_cam_all(Adapter);

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC11);
	/* 2010/12/17 MH We need to set TX power according to EFUSE content at first. */
	PHY_SetTxPowerLevel8188E(Adapter, pHalData->current_channel);

	/* Move by Neo for USB SS to below setp
	 * _RfPowerSave(Adapter); */

	_InitAntenna_Selection(Adapter);

	/*  */
	/* Disable BAR, suggested by Scott */
	/* 2010.04.09 add by hpfan */
	/*  */
	rtw_write32(Adapter, REG_BAR_MODE_CTRL, 0x0201ffff);

	/* HW SEQ CTRL */
	/* set 0x0 to 0xFF by tynli. Default enable HW SEQ NUM. */
	rtw_write8(Adapter, REG_HWSEQ_CTRL, 0xFF);

	PHY_SetRFEReg_8188E(Adapter);

	if (pregistrypriv->wifi_spec) {
		rtw_write16(Adapter, REG_FAST_EDCA_CTRL , 0);
		/* Nav limit , suggest by scott */
		rtw_write8(Adapter, REG_NAV_UPPER, 0x0);
	}

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_HAL_DM);
	rtl8188e_InitHalDm(Adapter);

#if (MP_DRIVER == 1)
	if (Adapter->registrypriv.mp_mode == 1) {
		Adapter->mppriv.channel = pHalData->current_channel;
		MPT_InitializeAdapter(Adapter, Adapter->mppriv.channel);
	} else
#endif  /* #if (MP_DRIVER == 1) */
	{
		/*  */
		/* 2010/08/11 MH Merge from 8192SE for Minicard init. We need to confirm current radio status */
		/* and then decide to enable RF or not.!!!??? For Selective suspend mode. We may not */
		/* call init_adapter. May cause some problem?? */
		/*  */
		/* Fix the bug that Hw/Sw radio off before S3/S4, the RF off action will not be executed */
		/* in MgntActSet_RF_State() after wake up, because the value of pHalData->eRFPowerState */
		/* is the same as eRfOff, we should change it to eRfOn after we config RF parameters. */
		/* Added by tynli. 2010.03.30. */
		pwrctrlpriv->rf_pwrstate = rf_on;

		if (!pHalData->fw_ractrl) {
			/* enable Tx report. */
			rtw_write8(Adapter,  REG_FWHW_TXQ_CTRL + 1, 0x0F);
			/* tynli_test_tx_report. */
			rtw_write16(Adapter, REG_TX_RPT_TIME, 0x3DF0);
		}

		/* Suggested by SD1 pisa. Added by tynli. 2011.10.21. */
		rtw_write8(Adapter, REG_EARLY_MODE_CONTROL + 3, 0x01); /* Pretx_en, for WEP/TKIP SEC */


		/* enable tx DMA to drop the redundate data of packet */
		rtw_write16(Adapter, REG_TXDMA_OFFSET_CHK, (rtw_read16(Adapter, REG_TXDMA_OFFSET_CHK) | DROP_DATA_EN));

		HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_IQK);
		/* 2010/08/26 MH Merge from 8192CE. */
		if (pwrctrlpriv->rf_pwrstate == rf_on) {
			if (pHalData->bIQKInitialized)
				phy_iq_calibrate_8188e(Adapter, _TRUE);
			else {
				phy_iq_calibrate_8188e(Adapter, _FALSE);
				pHalData->bIQKInitialized = _TRUE;
			}

			HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_PW_TRACK);

			odm_txpowertracking_check(&pHalData->odmpriv);


			HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_LCK);
			phy_lc_calibrate_8188e(&pHalData->odmpriv);
		}
	}

	/* HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_PABIAS);
	 *	_InitPABias(Adapter); */
	rtw_write8(Adapter, REG_USB_HRPWM, 0);

#ifdef CONFIG_XMIT_ACK
	/* ack for xmit mgmt frames. */
	rtw_write32(Adapter, REG_FWHW_TXQ_CTRL, rtw_read32(Adapter, REG_FWHW_TXQ_CTRL) | BIT(12));
#endif /* CONFIG_XMIT_ACK */

exit:
	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_END);

	RTW_INFO("%s in %dms\n", __FUNCTION__, rtw_get_passing_time_ms(init_start_time));

#ifdef DBG_HAL_INIT_PROFILING
	hal_init_stages_timestamp[HAL_INIT_STAGES_END] = rtw_get_current_time();

	for (hal_init_profiling_i = 0; hal_init_profiling_i < HAL_INIT_STAGES_NUM - 1; hal_init_profiling_i++) {
		RTW_INFO("DBG_HAL_INIT_PROFILING: %35s, %u, %5u, %5u\n"
			 , hal_init_stages_str[hal_init_profiling_i]
			 , hal_init_stages_timestamp[hal_init_profiling_i]
			, (hal_init_stages_timestamp[hal_init_profiling_i + 1] - hal_init_stages_timestamp[hal_init_profiling_i])
			, rtw_get_time_interval_ms(hal_init_stages_timestamp[hal_init_profiling_i], hal_init_stages_timestamp[hal_init_profiling_i + 1])
			);
	}
#endif



	return status;
}

void _ps_open_RF(_adapter *padapter)
{
	/* here call with bRegSSPwrLvl 1, bRegSSPwrLvl 2 needs to be verified */
	/* phy_SsPwrSwitch92CU(padapter, rf_on, 1); */
}

void _ps_close_RF(_adapter *padapter)
{
	/* here call with bRegSSPwrLvl 1, bRegSSPwrLvl 2 needs to be verified */
	/* phy_SsPwrSwitch92CU(padapter, rf_off, 1); */
}


VOID
hal_poweroff_8188eu(
	IN	PADAPTER			Adapter
)
{

	u8	val8;
	u16	val16;
	u32	val32;
	u8 bMacPwrCtrlOn = _FALSE;

	rtw_hal_get_hwreg(Adapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (bMacPwrCtrlOn == _FALSE)
		return ;


	/* Stop Tx Report Timer. 0x4EC[Bit1]=b'0 */
	val8 = rtw_read8(Adapter, REG_TX_RPT_CTRL);
	rtw_write8(Adapter, REG_TX_RPT_CTRL, val8 & (~BIT1));

	/* stop rx */
	rtw_write8(Adapter, REG_CR, 0x0);

	/* Run LPS WL RFOFF flow */
	HalPwrSeqCmdParsing(Adapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, Rtl8188E_NIC_LPS_ENTER_FLOW);


	/* 2. 0x1F[7:0] = 0		 */ /* turn off RF */
	/* rtw_write8(Adapter, REG_RF_CTRL, 0x00); */

	val8 = rtw_read8(Adapter, REG_MCUFWDL);
	if ((val8 & RAM_DL_SEL) && Adapter->bFWReady) { /* 8051 RAM code */
		/* _8051Reset88E(padapter);		 */

		/* Reset MCU 0x2[10]=0. */
		val8 = rtw_read8(Adapter, REG_SYS_FUNC_EN + 1);
		val8 &= ~BIT(2);	/* 0x2[10], FEN_CPUEN */
		rtw_write8(Adapter, REG_SYS_FUNC_EN + 1, val8);
	}

	/* val8 = rtw_read8(Adapter, REG_SYS_FUNC_EN+1); */
	/* val8 &= ~BIT(2);	 */ /* 0x2[10], FEN_CPUEN */
	/* rtw_write8(Adapter, REG_SYS_FUNC_EN+1, val8); */

	/* MCUFWDL 0x80[1:0]=0 */
	/* reset MCU ready status */
	rtw_write8(Adapter, REG_MCUFWDL, 0);

	/* YJ,add,111212 */
	/* Disable 32k */
	val8 = rtw_read8(Adapter, REG_32K_CTRL);
	rtw_write8(Adapter, REG_32K_CTRL, val8 & (~BIT0));

	/* Card disable power action flow */
	HalPwrSeqCmdParsing(Adapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, Rtl8188E_NIC_DISABLE_FLOW);

	/* Reset MCU IO Wrapper */
	val8 = rtw_read8(Adapter, REG_RSV_CTRL + 1);
	rtw_write8(Adapter, REG_RSV_CTRL + 1, (val8 & (~BIT3)));
	val8 = rtw_read8(Adapter, REG_RSV_CTRL + 1);
	rtw_write8(Adapter, REG_RSV_CTRL + 1, val8 | BIT3);

#if 0
	/* 7. RSV_CTRL 0x1C[7:0] = 0x0E			 */ /* lock ISO/CLK/Power control register */
	rtw_write8(Adapter, REG_RSV_CTRL, 0x0e);
#endif
#if 1
	/* YJ,test add, 111207. For Power Consumption. */
	val8 = rtw_read8(Adapter, GPIO_IN);
	rtw_write8(Adapter, GPIO_OUT, val8);
	rtw_write8(Adapter, GPIO_IO_SEL, 0xFF);/* Reg0x46 */

	val8 = rtw_read8(Adapter, REG_GPIO_IO_SEL);
	/* rtw_write8(Adapter, REG_GPIO_IO_SEL, (val8<<4)|val8); */
	rtw_write8(Adapter, REG_GPIO_IO_SEL, (val8 << 4));
	val8 = rtw_read8(Adapter, REG_GPIO_IO_SEL + 1);
	rtw_write8(Adapter, REG_GPIO_IO_SEL + 1, val8 | 0x0F); /* Reg0x43 */
	rtw_write32(Adapter, REG_BB_PAD_CTRL, 0x00080808);/* set LNA ,TRSW,EX_PA Pin to output mode */
#endif

	Adapter->bFWReady = _FALSE;

	bMacPwrCtrlOn = _FALSE;
	rtw_hal_set_hwreg(Adapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
}
static void rtl8188eu_hw_power_down(_adapter *padapter)
{
	/* 2010/-8/09 MH For power down module, we need to enable register block contrl reg at 0x1c. */
	/* Then enable power down control bit of register 0x04 BIT4 and BIT15 as 1. */

	/* Enable register area 0x0-0xc. */
	rtw_write8(padapter, REG_RSV_CTRL, 0x0);
	rtw_write16(padapter, REG_APS_FSMCO, 0x8812);
}

u32 rtl8188eu_hal_deinit(PADAPTER Adapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(Adapter);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	RTW_INFO("==> %s\n", __FUNCTION__);

#ifdef CONFIG_SUPPORT_USB_INT
	rtw_write32(Adapter, REG_HIMR_88E, IMR_DISABLED_88E);
	rtw_write32(Adapter, REG_HIMRE_88E, IMR_DISABLED_88E);
#endif

#ifdef SUPPORT_HW_RFOFF_DETECTED
	RTW_INFO("bkeepfwalive(%x)\n", pwrctl->bkeepfwalive);
	if (pwrctl->bkeepfwalive) {
		_ps_close_RF(Adapter);
		if ((pwrctl->bHWPwrPindetect) && (pwrctl->bHWPowerdown))
			rtl8188eu_hw_power_down(Adapter);
	} else
#endif
	{
		if (rtw_is_hw_init_completed(Adapter)) {
			rtw_hal_power_off(Adapter);

			if ((pwrctl->bHWPwrPindetect) && (pwrctl->bHWPowerdown))
				rtl8188eu_hw_power_down(Adapter);

		}
	}
	return _SUCCESS;
}


unsigned int rtl8188eu_inirp_init(PADAPTER Adapter)
{
	u8 i;
	struct recv_buf *precvbuf;
	uint	status;
	struct dvobj_priv *pdev = adapter_to_dvobj(Adapter);
	struct intf_hdl *pintfhdl = &Adapter->iopriv.intf;
	struct recv_priv *precvpriv = &(Adapter->recvpriv);
	u32(*_read_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);
#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	u32(*_read_interrupt)(struct intf_hdl *pintfhdl, u32 addr);
#endif


	_read_port = pintfhdl->io_ops._read_port;

	status = _SUCCESS;


	precvpriv->ff_hwaddr = RECV_BULK_IN_ADDR;

	/* issue Rx irp to receive data	 */
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;
	for (i = 0; i < NR_RECVBUFF; i++) {
		if (_read_port(pintfhdl, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf) == _FALSE) {
			status = _FAIL;
			goto exit;
		}

		precvbuf++;
		precvpriv->free_recv_buf_queue_cnt--;
	}

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
	if (pdev->RtInPipe[REALTEK_USB_IN_INT_EP_IDX] != 0x05) {
		status = _FAIL;
		RTW_INFO("%s =>Warning !! Have not USB Int-IN pipe, RtIntInPipe(%d)!!!\n", __func__, pdev->RtInPipe[REALTEK_USB_IN_INT_EP_IDX]);
		goto exit;
	}
	_read_interrupt = pintfhdl->io_ops._read_interrupt;
	if (_read_interrupt(pintfhdl, RECV_INT_IN_ADDR) == _FALSE) {
		status = _FAIL;
	}
#endif

exit:



	return status;

}

unsigned int rtl8188eu_inirp_deinit(PADAPTER Adapter)
{

	rtw_read_port_cancel(Adapter);


	return _SUCCESS;
}



/* -------------------------------------------------------------------------
 *
 *	EEPROM Power index mapping
 *
 * ------------------------------------------------------------------------- */


/* -------------------------------------------------------------------
 *
 *	EEPROM/EFUSE Content Parsing
 *
 * ------------------------------------------------------------------- */

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

	switch (pHalData->CustomerID) {
	default:
		pledpriv->LedStrategy = SW_LED_MODE1;
		break;
	}
	pHalData->bLedOpenDrain = _TRUE;/* Support Open-drain arrangement for controlling the LED. Added by Roger, 2009.10.16. */
#else /* HW LED */
	pledpriv->LedStrategy = HW_LED;
#endif /* CONFIG_SW_LED */
}

static VOID
_ReadRFSetting(
	IN	PADAPTER	Adapter,
	IN	u8	*PROMContent,
	IN	BOOLEAN	AutoloadFail
)
{
}

static VOID
hal_InitPGData(
	IN	PADAPTER	pAdapter,
	IN	OUT	u8		*PROMContent
)
{
#if 0

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	u32	i;
	u16	value16;

	if (_FALSE == pHalData->bautoload_fail_flag) {
		/* autoload OK. */
		if (_TRUE == pHalData->EepromOrEfuse) {
			/* Read all Content from EEPROM or EFUSE. */
			for (i = 0; i < HWSET_MAX_SIZE_88E; i += 2) {
				/* value16 = EF2Byte(ReadEEprom(pAdapter, (u2Byte) (i>>1))); */
				/* *((u16 *)(&PROMContent[i])) = value16;				 */
			}
		} else {
			/* Read EFUSE real map to shadow. */
			EFUSE_ShadowMapUpdate(pAdapter, EFUSE_WIFI, _FALSE);
			_rtw_memcpy((void *)PROMContent, (void *)pHalData->efuse_eeprom_data, HWSET_MAX_SIZE_88E);
		}
	} else {
		/* autoload fail */
		pHalData->bautoload_fail_flag = _TRUE;
		/* update to default value 0xFF */
		if (_FALSE == pHalData->EepromOrEfuse)
			EFUSE_ShadowMapUpdate(pAdapter, EFUSE_WIFI, _FALSE);
	}
#endif
}
static void
Hal_EfuseParsePIDVID_8188EU(
	IN	PADAPTER		pAdapter,
	IN	u8				*hwinfo,
	IN	BOOLEAN			AutoLoadFail
)
{

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if (!AutoLoadFail) {
		/* VID, PID */
		pHalData->EEPROMVID = ReadLE2Byte(&hwinfo[EEPROM_VID_88EU]);
		pHalData->EEPROMPID = ReadLE2Byte(&hwinfo[EEPROM_PID_88EU]);

		/* Customer ID, 0x00 and 0xff are reserved for Realtek.		 */
		pHalData->EEPROMCustomerID = *(u8 *)&hwinfo[EEPROM_CustomID_88E];
		pHalData->EEPROMSubCustomerID = EEPROM_Default_SubCustomerID;

	} else {
		pHalData->EEPROMVID			= EEPROM_Default_VID;
		pHalData->EEPROMPID			= EEPROM_Default_PID;

		/* Customer ID, 0x00 and 0xff are reserved for Realtek.		 */
		pHalData->EEPROMCustomerID		= EEPROM_Default_CustomerID;
		pHalData->EEPROMSubCustomerID	= EEPROM_Default_SubCustomerID;

	}

	RTW_INFO("VID = 0x%04X, PID = 0x%04X\n", pHalData->EEPROMVID, pHalData->EEPROMPID);
	RTW_INFO("Customer ID: 0x%02X, SubCustomer ID: 0x%02X\n", pHalData->EEPROMCustomerID, pHalData->EEPROMSubCustomerID);
}

static void
Hal_CustomizeByCustomerID_8188EU(
	IN	PADAPTER		padapter
)
{
#if 0
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	/* For customized behavior. */
	if ((pHalData->EEPROMVID == 0x103C) && (pHalData->EEPROMVID == 0x1629)) /* HP Lite-On for RTL8188CUS Slim Combo. */
		pHalData->CustomerID = RT_CID_819x_HP;

	/* Decide CustomerID according to VID/DID or EEPROM */
	switch (pHalData->EEPROMCustomerID) {
	case EEPROM_CID_DEFAULT:
		if ((pHalData->EEPROMVID == 0x2001) && (pHalData->EEPROMPID == 0x3308))
			pHalData->CustomerID = RT_CID_DLINK;
		else if ((pHalData->EEPROMVID == 0x2001) && (pHalData->EEPROMPID == 0x3309))
			pHalData->CustomerID = RT_CID_DLINK;
		else if ((pHalData->EEPROMVID == 0x2001) && (pHalData->EEPROMPID == 0x330a))
			pHalData->CustomerID = RT_CID_DLINK;
		break;
	case EEPROM_CID_WHQL:
		padapter->bInHctTest = TRUE;

		pMgntInfo->bSupportTurboMode = FALSE;
		pMgntInfo->bAutoTurboBy8186 = FALSE;

		pMgntInfo->PowerSaveControl.bInactivePs = FALSE;
		pMgntInfo->PowerSaveControl.bIPSModeBackup = FALSE;
		pMgntInfo->PowerSaveControl.bLeisurePs = FALSE;
		pMgntInfo->PowerSaveControl.bLeisurePsModeBackup = FALSE;
		pMgntInfo->keepAliveLevel = 0;

		padapter->bUnloadDriverwhenS3S4 = FALSE;
		break;
	default:
		pHalData->CustomerID = RT_CID_DEFAULT;
		break;

	}


	hal_CustomizedBehavior_8723U(padapter);
#endif
}

static VOID
readAdapterInfo_8188EU(
	IN	PADAPTER	padapter
)
{
#if 1
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);

	/* parse the eeprom/efuse content */
	Hal_EfuseParseIDCode88E(padapter, pHalData->efuse_eeprom_data);
	Hal_EfuseParsePIDVID_8188EU(padapter, pHalData->efuse_eeprom_data, pHalData->bautoload_fail_flag);
	hal_config_macaddr(padapter, pHalData->bautoload_fail_flag);
	Hal_ReadPowerSavingMode88E(padapter, pHalData->efuse_eeprom_data, pHalData->bautoload_fail_flag);
	Hal_ReadTxPowerInfo88E(padapter, pHalData->efuse_eeprom_data, pHalData->bautoload_fail_flag);
	Hal_EfuseParseEEPROMVer88E(padapter, pHalData->efuse_eeprom_data, pHalData->bautoload_fail_flag);
	rtl8188e_EfuseParseChnlPlan(padapter, pHalData->efuse_eeprom_data, pHalData->bautoload_fail_flag);
	Hal_EfuseParseXtal_8188E(padapter, pHalData->efuse_eeprom_data, pHalData->bautoload_fail_flag);
	Hal_EfuseParseCustomerID88E(padapter, pHalData->efuse_eeprom_data, pHalData->bautoload_fail_flag);
	Hal_ReadAntennaDiversity88E(padapter, pHalData->efuse_eeprom_data, pHalData->bautoload_fail_flag);
	Hal_EfuseParseBoardType88E(padapter, pHalData->efuse_eeprom_data, pHalData->bautoload_fail_flag);
	Hal_ReadThermalMeter_88E(padapter, pHalData->efuse_eeprom_data, pHalData->bautoload_fail_flag);
	Hal_ReadPAType_8188E(padapter, pHalData->efuse_eeprom_data, pHalData->bautoload_fail_flag);
	Hal_ReadAmplifierType_8188E(padapter, pHalData->efuse_eeprom_data, pHalData->bautoload_fail_flag);
	Hal_ReadRFEType_8188E(padapter, pHalData->efuse_eeprom_data, pHalData->bautoload_fail_flag);
#ifdef CONFIG_RF_POWER_TRIM
	Hal_ReadRFGainOffset(padapter, pHalData->efuse_eeprom_data, pHalData->bautoload_fail_flag);
#endif	/*CONFIG_RF_POWER_TRIM*/

	/*  */
	/* The following part initialize some vars by PG info. */
	/*  */

#if defined(CONFIG_WOWLAN) && defined(CONFIG_SDIO_HCI)
	Hal_DetectWoWMode(padapter);
#endif /* CONFIG_WOWLAN && CONFIG_SDIO_HCI */

	Hal_CustomizeByCustomerID_8188EU(padapter);

	_ReadLEDSetting(padapter, pHalData->efuse_eeprom_data, pHalData->bautoload_fail_flag);

#else

#ifdef CONFIG_INTEL_PROXIM
	/* for intel proximity */
	if (pHalData->rf_type == RF_1T1R)
		Adapter->proximity.proxim_support = _TRUE;
	else if (pHalData->rf_type == RF_2T2R) {
		if ((pHalData->EEPROMPID == 0x8186) &&
		    (pHalData->EEPROMVID == 0x0bda))
			Adapter->proximity.proxim_support = _TRUE;
	} else
		Adapter->proximity.proxim_support = _FALSE;
#endif /* CONFIG_INTEL_PROXIM		 */
#endif
}

static void _ReadPROMContent(
	IN PADAPTER		Adapter
)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(Adapter);
	u8			eeValue;

	/* check system boot selection */
	eeValue = rtw_read8(Adapter, REG_9346CR);
	pHalData->EepromOrEfuse		= (eeValue & BOOT_FROM_EEPROM) ? _TRUE : _FALSE;
	pHalData->bautoload_fail_flag	= (eeValue & EEPROM_EN) ? _FALSE : _TRUE;


	RTW_INFO("Boot from %s, Autoload %s !\n", (pHalData->EepromOrEfuse ? "EEPROM" : "EFUSE"),
		 (pHalData->bautoload_fail_flag ? "Fail" : "OK"));

	/* pHalData->EEType = IS_BOOT_FROM_EEPROM(Adapter) ? EEPROM_93C46 : EEPROM_BOOT_EFUSE; */

	Hal_InitPGData88E(Adapter);
	readAdapterInfo_8188EU(Adapter);
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

static u8 ReadAdapterInfo8188EU(PADAPTER Adapter)
{
	/* Read EEPROM size before call any EEPROM function */
	Adapter->EepromAddressSize = GetEEPROMSize8188E(Adapter);

	/* Efuse_InitSomeVar(Adapter); */

	_ReadRFType(Adapter);/* rf_chip->_InitRFType() */
	_ReadPROMContent(Adapter);

	return _SUCCESS;
}

void UpdateInterruptMask8188EU(PADAPTER padapter, u8 bHIMR0 , u32 AddMSR, u32 RemoveMSR)
{
	HAL_DATA_TYPE *pHalData;

	u32 *himr;
	pHalData = GET_HAL_DATA(padapter);

	if (bHIMR0)
		himr = &(pHalData->IntrMask[0]);
	else
		himr = &(pHalData->IntrMask[1]);

	if (AddMSR)
		*himr |= AddMSR;

	if (RemoveMSR)
		*himr &= (~RemoveMSR);

	if (bHIMR0)
		rtw_write32(padapter, REG_HIMR_88E, *himr);
	else
		rtw_write32(padapter, REG_HIMRE_88E, *himr);

}

void SetHwReg8188EU(PADAPTER Adapter, u8 variable, u8 *val)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);


	switch (variable) {

	case HW_VAR_RXDMA_AGG_PG_TH:
#ifdef CONFIG_USB_RX_AGGREGATION
		{
			u8	threshold = *((u8 *)val);
			if (threshold == 0)
				threshold = pHalData->rxagg_dma_size;
			rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH, threshold);
		}
#endif
		break;
	case HW_VAR_SET_RPWM:
#ifdef CONFIG_LPS_LCLK
		{
			u8	ps_state = *((u8 *)val);
			/* rpwm value only use BIT0(clock bit) ,BIT6(Ack bit), and BIT7(Toggle bit) for 88e. */
			/* BIT0 value - 1: 32k, 0:40MHz. */
			/* BIT6 value - 1: report cpwm value after success set, 0:do not report. */
			/* BIT7 value - Toggle bit change. */
			/* modify by Thomas. 2012/4/2. */
			ps_state = ps_state & 0xC1;
			/* RTW_INFO("##### Change RPWM value to = %x for switch clk #####\n",ps_state); */
			rtw_write8(Adapter, REG_USB_HRPWM, ps_state);
		}
#endif
		break;
	default:
		SetHwReg8188E(Adapter, variable, val);
		break;
	}

}

void GetHwReg8188EU(PADAPTER Adapter, u8 variable, u8 *val)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	switch (variable) {
	default:
		GetHwReg8188E(Adapter, variable, val);
		break;
	}

}

/*
 *	Description:
 *		Query setting of specified variable.
 *   */
u8
GetHalDefVar8188EUsb(
	IN	PADAPTER				Adapter,
	IN	HAL_DEF_VARIABLE		eVariable,
	IN	PVOID					pValue
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _SUCCESS;

	switch (eVariable) {
	case HW_VAR_MAX_RX_AMPDU_FACTOR:
		*((u32 *)pValue) = MAX_AMPDU_FACTOR_64K;
		break;

	case HAL_DEF_TX_LDPC:
	case HAL_DEF_RX_LDPC:
		*((u8 *)pValue) = _FALSE;
		break;
	case HAL_DEF_TX_STBC:
		*((u8 *)pValue) = 0;
		break;
	case HAL_DEF_RX_STBC:
		*((u8 *)pValue) = 1;
		break;
	default:
		bResult = GetHalDefVar8188E(Adapter, eVariable, pValue);
		break;
	}

	return bResult;
}




/*
 *	Description:
 *		Change default setting of specified variable.
 *   */
u8
SetHalDefVar8188EUsb(
	IN	PADAPTER				Adapter,
	IN	HAL_DEF_VARIABLE		eVariable,
	IN	PVOID					pValue
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _SUCCESS;

	switch (eVariable) {
	default:
		bResult = SetHalDefVar(Adapter, eVariable, pValue);
		break;
	}

	return bResult;
}
#if 0
u32  _update_92cu_basic_rate(_adapter *padapter, unsigned int mask)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
#ifdef CONFIG_BT_COEXIST
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);
#endif
	unsigned int BrateCfg = 0;

#ifdef CONFIG_BT_COEXIST
	if ((pbtpriv->BT_Coexist) &&	(pbtpriv->BT_CoexistType == BT_CSR_BC4))
		BrateCfg = mask  & 0x151;

	else
#endif
	{
		/* if(pHalData->version_id != VERSION_TEST_CHIP_88C) */
		BrateCfg = mask  & 0x15F;
		/* else	//for 88CU 46PING setting, Disable CCK 2M, 5.5M, Others must tuning */
		/*	BrateCfg = mask  & 0x159; */
	}

	BrateCfg |= 0x01; /*  default enable 1M ACK rate					 */

	return BrateCfg;
}
#endif
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

void SetBeaconRelatedRegisters8188EUsb(PADAPTER padapter)
{
	u32	value32;
	/* HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter); */
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u32 bcn_ctrl_reg			= REG_BCN_CTRL;
	/* reset TSF, enable update TSF, correcting TSF On Beacon */

	/* REG_BCN_INTERVAL */
	/* REG_BCNDMATIM */
	/* REG_ATIMWND */
	/* REG_TBTT_PROHIBIT */
	/* REG_DRVERLYINT */
	/* REG_BCN_MAX_ERR	 */
	/* REG_BCNTCFG */ /* (0x510) */
	/* REG_DUAL_TSF_RST */
	/* REG_BCN_CTRL  */ /* (0x550) */

	/* BCN interval */
#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->hw_port == HW_PORT1)
		bcn_ctrl_reg = REG_BCN_CTRL_1;
#endif
	rtw_write16(padapter, REG_BCN_INTERVAL, pmlmeinfo->bcn_interval);
	rtw_write8(padapter, REG_ATIMWND, 0x02);/* 2ms */

	_InitBeaconParameters(padapter);

	rtw_write8(padapter, REG_SLOT, 0x09);

	value32 = rtw_read32(padapter, REG_TCR);
	value32 &= ~TSFRST;
	rtw_write32(padapter,  REG_TCR, value32);

	value32 |= TSFRST;
	rtw_write32(padapter, REG_TCR, value32);

	/* NOTE: Fix test chip's bug (about contention windows's randomness) */
	rtw_write8(padapter,  REG_RXTSF_OFFSET_CCK, 0x50);
	rtw_write8(padapter, REG_RXTSF_OFFSET_OFDM, 0x50);

	_BeaconFunctionEnable(padapter, _TRUE, _TRUE);

	ResumeTxBeacon(padapter);

	/* rtw_write8(padapter, 0x422, rtw_read8(padapter, 0x422)|BIT(6)); */

	/* rtw_write8(padapter, 0x541, 0xff); */

	/* rtw_write8(padapter, 0x542, rtw_read8(padapter, 0x541)|BIT(0)); */

	rtw_write8(padapter, bcn_ctrl_reg, rtw_read8(padapter, bcn_ctrl_reg) | BIT(1));

}

static void rtl8188eu_init_default_value(_adapter *padapter)
{
	PHAL_DATA_TYPE pHalData;
	struct pwrctrl_priv *pwrctrlpriv;
	u8 i;

	pHalData = GET_HAL_DATA(padapter);
	pwrctrlpriv = adapter_to_pwrctl(padapter);

	rtl8188e_init_default_value(padapter);

	/* init default value */
	pHalData->fw_ractrl = _FALSE;
	if (!pwrctrlpriv->bkeepfwalive)
		pHalData->LastHMEBoxNum = 0;

	/* init dm default value */
	pHalData->bIQKInitialized = _FALSE;
	pHalData->odmpriv.rf_calibrate_info.tm_trigger = 0;/* for IQK */
	/* pdmpriv->binitialized = _FALSE;
	*	pdmpriv->prv_traffic_idx = 3;
	*	pdmpriv->initialize = 0; */
	pHalData->odmpriv.rf_calibrate_info.thermal_value_hp_index = 0;
	for (i = 0; i < HP_THERMAL_NUM; i++)
		pHalData->odmpriv.rf_calibrate_info.thermal_value_hp[i] = 0;

	pHalData->EfuseHal.fakeEfuseBank = 0;
	pHalData->EfuseHal.fakeEfuseUsedBytes = 0;
	_rtw_memset(pHalData->EfuseHal.fakeEfuseContent, 0xFF, EFUSE_MAX_HW_SIZE);
	_rtw_memset(pHalData->EfuseHal.fakeEfuseInitMap, 0xFF, EFUSE_MAX_MAP_LEN);
	_rtw_memset(pHalData->EfuseHal.fakeEfuseModifiedMap, 0xFF, EFUSE_MAX_MAP_LEN);
}

static u8 rtl8188eu_ps_func(PADAPTER Adapter, HAL_INTF_PS_FUNC efunc_id, u8 *val)
{
	u8 bResult = _TRUE;
	switch (efunc_id) {

#if defined(CONFIG_AUTOSUSPEND) && defined(SUPPORT_HW_RFOFF_DETECTED)
	case HAL_USB_SELECT_SUSPEND: {
		u8 bfwpoll = *((u8 *)val);
		/* rtl8188e_set_FwSelectSuspend_cmd(Adapter,bfwpoll ,500); */ /* note fw to support hw power down ping detect */
	}
	break;
#endif /* CONFIG_AUTOSUSPEND && SUPPORT_HW_RFOFF_DETECTED */

	default:
		break;
	}
	return bResult;
}

void rtl8188eu_set_hal_ops(_adapter *padapter)
{
	struct hal_ops	*pHalFunc = &padapter->hal_func;


	pHalFunc->hal_power_on = _InitPowerOn_8188EU;
	pHalFunc->hal_power_off = hal_poweroff_8188eu;

	pHalFunc->hal_init = &rtl8188eu_hal_init;
	pHalFunc->hal_deinit = &rtl8188eu_hal_deinit;

	pHalFunc->inirp_init = &rtl8188eu_inirp_init;
	pHalFunc->inirp_deinit = &rtl8188eu_inirp_deinit;

	pHalFunc->init_xmit_priv = &rtl8188eu_init_xmit_priv;
	pHalFunc->free_xmit_priv = &rtl8188eu_free_xmit_priv;

	pHalFunc->init_recv_priv = &rtl8188eu_init_recv_priv;
	pHalFunc->free_recv_priv = &rtl8188eu_free_recv_priv;
#ifdef CONFIG_SW_LED
	pHalFunc->InitSwLeds = &rtl8188eu_InitSwLeds;
	pHalFunc->DeInitSwLeds = &rtl8188eu_DeInitSwLeds;
#else /* case of hw led or no led */
	pHalFunc->InitSwLeds = NULL;
	pHalFunc->DeInitSwLeds = NULL;
#endif/* CONFIG_SW_LED */

	pHalFunc->init_default_value = &rtl8188eu_init_default_value;
	pHalFunc->intf_chip_configure = &rtl8188eu_interface_configure;
	pHalFunc->read_adapter_info = &ReadAdapterInfo8188EU;
	pHalFunc->set_hw_reg_handler = &SetHwReg8188EU;
	pHalFunc->GetHwRegHandler = &GetHwReg8188EU;
	pHalFunc->get_hal_def_var_handler = &GetHalDefVar8188EUsb;
	pHalFunc->SetHalDefVarHandler = &SetHalDefVar8188EUsb;

	pHalFunc->SetBeaconRelatedRegistersHandler = &SetBeaconRelatedRegisters8188EUsb;

	pHalFunc->hal_xmit = &rtl8188eu_hal_xmit;
	pHalFunc->mgnt_xmit = &rtl8188eu_mgnt_xmit;
	pHalFunc->hal_xmitframe_enqueue = &rtl8188eu_hal_xmitframe_enqueue;


#ifdef CONFIG_HOSTAPD_MLME
	pHalFunc->hostap_mgnt_xmit_entry = &rtl8188eu_hostap_mgnt_xmit_entry;
#endif
	pHalFunc->interface_ps_func = &rtl8188eu_ps_func;

#ifdef CONFIG_XMIT_THREAD_MODE
	pHalFunc->xmit_thread_handler = &rtl8188eu_xmit_buf_handler;
#endif
#ifdef CONFIG_SUPPORT_USB_INT
	pHalFunc->interrupt_handler = interrupt_handler_8188eu;
#endif
	rtl8188e_set_hal_ops(pHalFunc);

}
