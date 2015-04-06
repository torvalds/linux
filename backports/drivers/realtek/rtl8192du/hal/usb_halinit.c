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
 *
 ******************************************************************************/
#define _HCI_HAL_INIT_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <hal_intf.h>
#include <rtw_efuse.h>

#include <rtl8192d_hal.h>
#include <rtl8192d_led.h>
#include <usb_ops.h>
#include <usb_hal.h>
#include <usb_osintf.h>

#if DISABLE_BB_RF
	#define		HAL_MAC_ENABLE	0
	#define		HAL_BB_ENABLE		0
	#define		HAL_RF_ENABLE		0
#else
	#define		HAL_MAC_ENABLE	1
	#define		HAL_BB_ENABLE		1
	#define		HAL_RF_ENABLE		1
#endif

/* endpoint number 1, 2, 3, 4, 5 */
/*  bult in : 1 */
/*  bult out: 2 (High) */
/*  bult out: 3 (Normal) for 3 out_ep, (Low) for 2 out_ep */
/*  interrupt in: 4 */
/*  bult out: 5 (Low) for 3 out_ep */

static void
_OneOutEpMapping(
	struct hal_data_8192du *pHalData
	)
{
	/* only endpoint number 0x02 */

	pHalData->Queue2EPNum[0] = pHalData->RtBulkOutPipe[0];/* VO */
	pHalData->Queue2EPNum[1] = pHalData->RtBulkOutPipe[0];/* VI */
	pHalData->Queue2EPNum[2] = pHalData->RtBulkOutPipe[0];/* BE */
	pHalData->Queue2EPNum[3] = pHalData->RtBulkOutPipe[0];/* BK */

	pHalData->Queue2EPNum[4] = pHalData->RtBulkOutPipe[0];/* TS */
	pHalData->Queue2EPNum[5] = pHalData->RtBulkOutPipe[0];/* MGT */
	pHalData->Queue2EPNum[6] = pHalData->RtBulkOutPipe[0];/* BMC */
	pHalData->Queue2EPNum[7] = pHalData->RtBulkOutPipe[0];/* BCN */
}

static void
_TwoOutEpMapping(
	struct hal_data_8192du	*pHalData,
	bool			bWIFICfg
	)
{

	if (bWIFICfg) { /*  Normal chip && wmm */

		/*	BK,	BE,	VI,	VO,	BCN,	CMD, MGT, HIGH, HCCA */
		/*   0,		1,	0,	1,	0,	0,	0,	0,		0	}; */
		/* 0:H(end_number = 0x02), 1:L (end_number = 0x03) */

		pHalData->Queue2EPNum[0] = pHalData->RtBulkOutPipe[1];/* VO */
		pHalData->Queue2EPNum[1] = pHalData->RtBulkOutPipe[0];/* VI */
		pHalData->Queue2EPNum[2] = pHalData->RtBulkOutPipe[1];/* BE */
		pHalData->Queue2EPNum[3] = pHalData->RtBulkOutPipe[0];/* BK */

		pHalData->Queue2EPNum[4] = pHalData->RtBulkOutPipe[0];/* TS */
		pHalData->Queue2EPNum[5] = pHalData->RtBulkOutPipe[0];/* MGT */
		pHalData->Queue2EPNum[6] = pHalData->RtBulkOutPipe[0];/* BMC */
		pHalData->Queue2EPNum[7] = pHalData->RtBulkOutPipe[0];/* BCN */

	}
	else {/* typical setting */

		/* BK,	BE,	VI,	VO,	BCN,	CMD, MGT, HIGH, HCCA */
		/*   1,		1,	0,	0,	0,	0,	0,	0,		0	}; */
		/* 0:H(end_number = 0x02), 1:L (end_number = 0x03) */

		pHalData->Queue2EPNum[0] = pHalData->RtBulkOutPipe[0];/* VO */
		pHalData->Queue2EPNum[1] = pHalData->RtBulkOutPipe[0];/* VI */
		pHalData->Queue2EPNum[2] = pHalData->RtBulkOutPipe[1];/* BE */
		pHalData->Queue2EPNum[3] = pHalData->RtBulkOutPipe[1];/* BK */

		pHalData->Queue2EPNum[4] = pHalData->RtBulkOutPipe[0];/* TS */
		pHalData->Queue2EPNum[5] = pHalData->RtBulkOutPipe[0];/* MGT */
		pHalData->Queue2EPNum[6] = pHalData->RtBulkOutPipe[0];/* BMC */
		pHalData->Queue2EPNum[7] = pHalData->RtBulkOutPipe[0];/* BCN */

	}
}

static void _ThreeOutEpMapping(
	struct hal_data_8192du *pHalData,
	bool			bWIFICfg
	)
{
	if (bWIFICfg) {/* for WMM */

		/*	BK,	BE,	VI,	VO,	BCN,	CMD, MGT, HIGH, HCCA */
		/*   1,		2,	1,	0,	0,	0,	0,	0,		0	}; */
		/* 0:H(end_number = 0x02), 1:N(end_number = 0x03), 2:L (end_number = 0x05) */

		pHalData->Queue2EPNum[0] = pHalData->RtBulkOutPipe[0];/* VO */
		pHalData->Queue2EPNum[1] = pHalData->RtBulkOutPipe[1];/* VI */
		pHalData->Queue2EPNum[2] = pHalData->RtBulkOutPipe[2];/* BE */
		pHalData->Queue2EPNum[3] = pHalData->RtBulkOutPipe[1];/* BK */

		pHalData->Queue2EPNum[4] = pHalData->RtBulkOutPipe[0];/* TS */
		pHalData->Queue2EPNum[5] = pHalData->RtBulkOutPipe[0];/* MGT */
		pHalData->Queue2EPNum[6] = pHalData->RtBulkOutPipe[0];/* BMC */
		pHalData->Queue2EPNum[7] = pHalData->RtBulkOutPipe[0];/* BCN */

	}
	else {/* typical setting */

		/*	BK,	BE,	VI,	VO,	BCN,	CMD, MGT, HIGH, HCCA */
		/*   2,		2,	1,	0,	0,	0,	0,	0,		0	}; */
		/* 0:H(end_number = 0x02), 1:N(end_number = 0x03), 2:L (end_number = 0x05) */

		pHalData->Queue2EPNum[0] = pHalData->RtBulkOutPipe[0];/* VO */
		pHalData->Queue2EPNum[1] = pHalData->RtBulkOutPipe[1];/* VI */
		pHalData->Queue2EPNum[2] = pHalData->RtBulkOutPipe[2];/* BE */
		pHalData->Queue2EPNum[3] = pHalData->RtBulkOutPipe[2];/* BK */

		pHalData->Queue2EPNum[4] = pHalData->RtBulkOutPipe[0];/* TS */
		pHalData->Queue2EPNum[5] = pHalData->RtBulkOutPipe[0];/* MGT */
		pHalData->Queue2EPNum[6] = pHalData->RtBulkOutPipe[0];/* BMC */
		pHalData->Queue2EPNum[7] = pHalData->RtBulkOutPipe[0];/* BCN */
	}
}

static bool
_MappingOutEP(
	struct rtw_adapter *	adapter,
	u8		NumOutPipe
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct registry_priv *pregistrypriv = &adapter->registrypriv;

	bool	 bWIFICfg = (pregistrypriv->wifi_spec) ?true:false;

	bool result = true;

	switch (NumOutPipe)
	{
		case 2:
			_TwoOutEpMapping(pHalData, bWIFICfg);
			break;
		case 3:
			_ThreeOutEpMapping(pHalData, bWIFICfg);
			break;
		case 1:
			_OneOutEpMapping(pHalData);
			break;
		default:
			result = false;
			break;
	}

	return result;
}

static void
_ConfigChipOutEP(
	struct rtw_adapter *	adapter,
	u8		NumOutPipe
	)
{
	u8			value8;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	pHalData->OutEpQueueSel = 0;
	pHalData->OutEpNumber = 0;

	/*  Normal and High queue */
	if (pHalData->interfaceIndex == 0)
		value8 = rtw_read8(adapter, REG_USB_High_NORMAL_Queue_Select_MAC0);
	else
		value8 = rtw_read8(adapter, REG_USB_High_NORMAL_Queue_Select_MAC1);

	if (value8 & USB_NORMAL_SIE_EP_MASK) {
		pHalData->OutEpQueueSel |= TX_SELE_HQ;
		pHalData->OutEpNumber++;
	}

	if ((value8 >> USB_NORMAL_SIE_EP_SHIFT) & USB_NORMAL_SIE_EP_MASK) {
		pHalData->OutEpQueueSel |= TX_SELE_NQ;
		pHalData->OutEpNumber++;
	}

	/*  Low queue */
	if (pHalData->interfaceIndex == 0)
		value8 = rtw_read8(adapter, (REG_USB_High_NORMAL_Queue_Select_MAC0+1));
	else
		value8 = rtw_read8(adapter, (REG_USB_High_NORMAL_Queue_Select_MAC1+1));

	if (value8 & USB_NORMAL_SIE_EP_MASK) {
		pHalData->OutEpQueueSel |= TX_SELE_LQ;
		pHalData->OutEpNumber++;
	}

	/* add for 0xfe44 0xfe45 0xfe47 0xfe48 not validly */
	switch (NumOutPipe) {
		case 3:
			pHalData->OutEpQueueSel = TX_SELE_HQ| TX_SELE_LQ|TX_SELE_NQ;
			pHalData->OutEpNumber = 3;
			break;
		case 2:
			pHalData->OutEpQueueSel = TX_SELE_HQ| TX_SELE_NQ;
			pHalData->OutEpNumber = 2;
			break;
		case 1:
			pHalData->OutEpQueueSel = TX_SELE_HQ;
			pHalData->OutEpNumber = 1;
			break;
		default:
			break;
	}

	/*  TODO: Error recovery for this case */
	/* RT_ASSERT((NumOutPipe == pHalData->OutEpNumber), ("Out EP number isn't match! %d(Descriptor) != %d (SIE reg)\n", (u4Byte)NumOutPipe, (u4Byte)pHalData->OutEpNumber)); */
}

static bool HalUsbSetQueuePipeMapping8192DUsb(
	struct rtw_adapter *	adapter,
	u8		NumInPipe,
	u8		NumOutPipe
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	bool			result		= false;

	_ConfigChipOutEP(adapter, NumOutPipe);

	/*  Normal chip with one IN and one OUT doesn't have interrupt IN EP. */
	if (1 == pHalData->OutEpNumber) {
		if (1 != NumInPipe) {
			return result;
		}
	}

	result = _MappingOutEP(adapter, NumOutPipe);

	return result;
}

static void rtl8192du_interface_configure(struct rtw_adapter *padapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(padapter);
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);

	if (pdvobjpriv->ishighspeed == true)
	{
		pHalData->UsbBulkOutSize = USB_HIGH_SPEED_BULK_SIZE;/* 512 bytes */
	}
	else
	{
		pHalData->UsbBulkOutSize = USB_FULL_SPEED_BULK_SIZE;/* 64 bytes */
	}

	pHalData->interfaceIndex = pdvobjpriv->InterfaceNumber;
	pHalData->RtBulkInPipe = pdvobjpriv->ep_num[0];
	pHalData->RtBulkOutPipe[0] = pdvobjpriv->ep_num[1];
	pHalData->RtBulkOutPipe[1] = pdvobjpriv->ep_num[2];
	pHalData->RtIntInPipe = pdvobjpriv->ep_num[3];
	pHalData->RtBulkOutPipe[2] = pdvobjpriv->ep_num[4];
	pHalData->UsbTxAggMode = 1;
	pHalData->UsbTxAggDescNum = 0x4;	/*  only 4 bits */

	pHalData->UsbRxAggMode = USB_RX_AGG_DMA;/*  USB_RX_AGG_DMA; */
	pHalData->UsbRxAggBlockCount	= 8; /* unit : 512b */
	pHalData->UsbRxAggBlockTimeout = 0x6;
	pHalData->UsbRxAggPageCount	= 48; /* uint :128 b 0x0A;	10 = MAX_RX_DMA_BUFFER_SIZE/2/pHalData->UsbBulkOutSize */
	pHalData->UsbRxAggPageTimeout = 0x6; /* 6, absolute time = 34ms/(2^6) */

	HalUsbSetQueuePipeMapping8192DUsb(padapter,
				pdvobjpriv->RtNumInPipes, pdvobjpriv->RtNumOutPipes);
}

static u8 _InitPowerOn(struct rtw_adapter *padapter)
{
	u8	ret = _SUCCESS;
	u16	value16 = 0;
	u8	value8 = 0;

	/*  polling autoload done. */
	u32	pollingCount = 0;

	if (padapter->bSurpriseRemoved) {
		return _FAIL;
	}

	pollingCount = 0;
	do
	{
		if (rtw_read8(padapter, REG_APS_FSMCO) & PFM_ALDN) {
			/* RT_TRACE(COMP_INIT, DBG_LOUD, ("Autoload Done!\n")); */
			break;
		}

		if (pollingCount++ > POLLING_READY_TIMEOUT_COUNT) {
			/* RT_TRACE(COMP_INIT, DBG_SERIOUS, ("Failed to polling REG_APS_FSMCO[PFM_ALDN] done!\n")); */
			return _FAIL;
		}

	}while (true);

	/* For hardware power on sequence. */
	/* 0.	RSV_CTRL 0x1C[7:0] = 0x00			unlock ISO/CLK/Power control register */
	rtw_write8(padapter, REG_RSV_CTRL, 0x0);
	/*  Power on when re-enter from IPS/Radio off/card disable */
	rtw_write8(padapter, REG_SPS0_CTRL, 0x2b);/* enable SPS into PWM mode */
	rtw_usleep_os(100);/* PlatformSleepUs(150);this is not necessary when initially power on */

	value8 = rtw_read8(padapter, REG_LDOV12D_CTRL);
	if (0 == (value8 & LDV12_EN)) {
		value8 |= LDV12_EN;
		rtw_write8(padapter, REG_LDOV12D_CTRL, value8);
		/* RT_TRACE(COMP_INIT, DBG_LOUD, (" power-on :REG_LDOV12D_CTRL Reg0x21:0x%02x.\n", value8)); */
		rtw_usleep_os(100);/* PlatformSleepUs(100);this is not necessary when initially power on */
		value8 = rtw_read8(padapter, REG_SYS_ISO_CTRL);
		value8 &= ~ISO_MD2PP;
		rtw_write8(padapter, REG_SYS_ISO_CTRL, value8);
	}

	/*  auto enable WLAN */
	pollingCount = 0;
	value16 = rtw_read16(padapter, REG_APS_FSMCO);
	value16 |= APFM_ONMAC;
	rtw_write16(padapter, REG_APS_FSMCO, value16);

	do
	{
		if (0 == (rtw_read16(padapter, REG_APS_FSMCO) & APFM_ONMAC)) {
			/* RT_TRACE(COMP_INIT, DBG_LOUD, ("MAC auto ON okay!\n")); */
			break;
		}

		if (pollingCount++ > POLLING_READY_TIMEOUT_COUNT) {
			/* RT_TRACE(COMP_INIT, DBG_SERIOUS, ("Failed to polling REG_APS_FSMCO[APFM_ONMAC] done!\n")); */
			return _FAIL;
		}

	}while (true);

	/*  release RF digital isolation */
	value16 = rtw_read16(padapter, REG_SYS_ISO_CTRL);
	value16 &= ~ISO_DIOR;
	rtw_write16(padapter, REG_SYS_ISO_CTRL, value16);

	/*  Enable MAC DMA/WMAC/SCHEDULE/SEC block */
	value16 = rtw_read16(padapter, REG_CR);
	value16 |= (HCI_TXDMA_EN | HCI_RXDMA_EN | TXDMA_EN | RXDMA_EN
				| PROTOCOL_EN | SCHEDULE_EN | MACTXEN | MACRXEN | ENSEC);
	rtw_write16(padapter, REG_CR, value16);

	return ret;
}

static u16 CRC16(u8 data, u16 CRC)
{
	unsigned char shift_in, CRC_BIT15, DataBit, CRC_BIT11, CRC_BIT4 ;
	int index;
	unsigned short CRC_Result;

	for (index = 0;index<8;index++)
	{
		CRC_BIT15 = ((CRC&BIT15) ? 1:0);
		DataBit  = (data&(BIT0<<index) ? 1:0);
		shift_in = CRC_BIT15^DataBit;
		/* printf("CRC_BIT15 =%d, DataBit =%d, shift_in =%d\n", CRC_BIT15, DataBit, shift_in); */

		CRC_Result = CRC<<1;
		/* set BIT0 */
		/*	printf("CRC =%x\n", CRC_Result); */
		/* CRC bit 0 = shift_in, */
		if (shift_in == 0)
		{
			CRC_Result&= (~BIT0);
		}
		else
		{
			CRC_Result|= BIT0;
		}
		/* printf("CRC =%x\n", CRC_Result); */

		CRC_BIT11 = ((CRC&BIT11) ? 1:0)^shift_in;
		if (CRC_BIT11 == 0)
		{
			CRC_Result&= (~BIT12);
		}
		else
		{
			CRC_Result|= BIT12;
		}
		/* printf("bit12 CRC =%x\n", CRC_Result); */

		CRC_BIT4 = ((CRC&BIT4) ? 1:0)^shift_in;
		if (CRC_BIT4 == 0)
		{
			CRC_Result&= (~BIT5);
		}
		else
		{
			CRC_Result|= BIT5;
		}
		/* printf("bit5 CRC =%x\n", CRC_Result); */

		CRC = CRC_Result; /* repeat using the last result */
	}

	return CRC;
}

/*  */
/*  */
/* function name :calc_crc */
/*  */
/* input         : char* pattern , pattern size */
/*  */
/*  */
static u16 calc_crc(u8 *pdata, int length)
{
/*     unsigned char data[2]={0xC6, 0xAA}; */
	u16 CRC = 0xffff;
	int i;

	for (i = 0;i<length;i++)
	{
		CRC = CRC16(pdata[i], CRC);
	}

	CRC =~CRC;                  /* get 1' complement */
	DBG_8192D("CRC =%x\n", CRC);
	return CRC;
}

#ifdef CONFIG_WOWLAN
static int rtw_wowlan_set_pattern(struct rtw_adapter *padapter , u8 *pbuf) {
	struct pwrctrl_priv *pwrpriv =&padapter->pwrctrlpriv;
	int res = 0, crc_idx;
	u32 content = 0, cmd = 0;
	u32 *pdata;
	u8 config, crc, mc, bc, uc, idx, pattern_len, packet[200], packet_len, valid;
	u16 crc_val = 0, i;

	config = pbuf[0];
	bc = config & BIT(3)?1:0;
	mc = config & BIT(4)?1:0;
	uc = config & BIT(5)?1:0;
	idx = config & 0x7;
	crc = config & BIT(6)?1:0;
	valid = config & BIT(7)?1:0;
	pattern_len = pbuf[1];
	packet_len = pattern_len*8;
	pdata = (u32 *)pbuf;

	/*  Write to the Wakeup CAM */
	/* offset 0 */
	if (pattern_len>= 4) {
		content = pdata[1];
	}
	else {
		content = 0;
	}
	DBG_8192D("\nrtw_wowlan_set_pattern offset[0]  content  0x%x  [cpu_to_le32  0x%x]\n", content, __cpu_to_le32(content));
	/* rtw_write32(padapter, REG_WKFMCAM_RWD, __cpu_to_le32(content)); */
	pwrpriv->wowlan_pattern_context[idx][0]= __cpu_to_le32(content);
	/* cmd = BIT(31)|BIT(16)|(idx+0); */
	/* rtw_write32(padapter, REG_WKFMCAM_CMD, cmd); */
	/* offset 4 */
	if (pattern_len>= 8) {
		content = pdata[2];
	}
	else {
		content = 0;
	}
	DBG_8192D("rtw_wowlan_set_pattern offset[4]  content  0x%x  [cpu_to_le32  0x%x]\n", content, __cpu_to_le32(content));
	/* rtw_write32(padapter, REG_WKFMCAM_RWD, __cpu_to_le32(content)); */
	pwrpriv->wowlan_pattern_context[idx][1]= __cpu_to_le32(content);

	/* cmd = BIT(31)|BIT(16)|(idx+1); */
	/* rtw_write32(padapter, REG_WKFMCAM_CMD, cmd); */
	/* offset 8 */
	if (pattern_len>= 12) {
		content = pdata[3];
	}
	else {
		content = 0;
	}
	DBG_8192D("rtw_wowlan_set_pattern offset[8]  content  0x%x  [cpu_to_le32  0x%x]\n", content, __cpu_to_le32(content));
	/* rtw_write32(padapter, REG_WKFMCAM_RWD, __cpu_to_le32(content)); */
	pwrpriv->wowlan_pattern_context[idx][2]= __cpu_to_le32(content);
	/* cmd = BIT(31)|BIT(16)|(idx+2); */
	/* rtw_write32(padapter, REG_WKFMCAM_CMD, cmd); */
	/* offset 12 */
	if (pattern_len>= 16) {
		content = pdata[4];
	}
	else {
		content = 0;
	}
	DBG_8192D("rtw_wowlan_set_pattern offset[12]  content  0x%x  [cpu_to_le32  0x%x]\n", content, __cpu_to_le32(content));
	/* rtw_write32(padapter, REG_WKFMCAM_RWD, __cpu_to_le32(content)); */
	pwrpriv->wowlan_pattern_context[idx][3]= __cpu_to_le32(content);
	/* cmd = BIT(31)|BIT(16)|(idx+3); */
	/* rtw_write32(padapter, REG_WKFMCAM_CMD, cmd); */

	if (crc) {
		/*  Have the CRC value */
		crc_val =*(u16 *)(&pbuf[2]);
		DBG_8192D("rtw_wowlan_set_pattern crc_val  0x%x\n", crc_val);
		crc_val = __cpu_to_le16(crc_val);
		DBG_8192D("rtw_wowlan_set_pattern crc_val  after 0x%x\n", crc_val);
	}
	else {
		DBG_8192D("+rtw_wowlan_set_pattern   crc = 0[%x]  Should calculate the CRC\n", crc);
		/*  calculate the CRC the write to the Wakeup CAM */
		crc_idx = 0;
		for (i = 0;i<packet_len;i++) {
			if (pbuf[4+(i/8)]&(0x01<<(i%8)))
			{
				packet[crc_idx++]= pbuf[20+i];
		/*		DBG_8192D("\n i =%d packet[i]=%x pbuf[20+i(%d)]=%x\n", i, packet[i], 20+i, pbuf[20+i]); */
			}
		}
		crc_val = calc_crc(packet, crc_idx);
		DBG_8192D("+rtw_wowlan_set_pattern   crc_val = 0x%.8x\n", crc_val);

	}

	/* offset 16 */
	content = (valid<<31)| (bc<<26)|(mc<<25)|(uc<<24) |crc_val;
	printk("rtw_wowlan_set_pattern offset[16]  content  0x%x\n", content);
	rtw_write32(padapter, REG_WKFMCAM_RWD, content);
	pwrpriv->wowlan_pattern_context[idx][4]= content;
	/* cmd = BIT(31)|BIT(16)|(idx+4); */
	/* rtw_write32(padapter, REG_WKFMCAM_CMD, cmd); */
	pwrpriv->wowlan_pattern_idx|= BIT(idx);

_rtw_wowlan_set_pattern_exit:
	return res;
}

void rtw_wowlan_reload_pattern(struct rtw_adapter *padapter) {
	struct pwrctrl_priv *pwrpriv =&padapter->pwrctrlpriv;
	u32 content = 0, cmd = 0;
	u8 idx;

	for (idx = 0;idx<8;idx ++) {
		if (pwrpriv->wowlan_pattern_idx & BIT(idx)) {
			/* offset 0 */
			rtw_write32(padapter, REG_WKFMCAM_RWD, pwrpriv->wowlan_pattern_context[idx][0]);
			cmd = BIT(31)|BIT(16)|(idx+0);
			rtw_write32(padapter, REG_WKFMCAM_CMD, cmd);

			/* offset 4 */
			rtw_write32(padapter, REG_WKFMCAM_RWD, pwrpriv->wowlan_pattern_context[idx][1]);
			cmd = BIT(31)|BIT(16)|(idx+1);
			rtw_write32(padapter, REG_WKFMCAM_CMD, cmd);

			/* offset 8 */
			rtw_write32(padapter, REG_WKFMCAM_RWD, pwrpriv->wowlan_pattern_context[idx][2]);
			cmd = BIT(31)|BIT(16)|(idx+2);
			rtw_write32(padapter, REG_WKFMCAM_CMD, cmd);

			/* offset 12 */
			rtw_write32(padapter, REG_WKFMCAM_RWD, pwrpriv->wowlan_pattern_context[idx][3]);
			cmd = BIT(31)|BIT(16)|(idx+3);
			rtw_write32(padapter, REG_WKFMCAM_CMD, cmd);

			/* offset 16 */
			rtw_write32(padapter, REG_WKFMCAM_RWD, pwrpriv->wowlan_pattern_context[idx][4]);
			cmd = BIT(31)|BIT(16)|(idx+4);
			rtw_write32(padapter, REG_WKFMCAM_CMD, cmd);

		}
		DBG_8192D("print WOWCAM  idx =%d\n", idx);
		cmd = BIT(31)|(idx+0);
		rtw_write32(padapter, REG_WKFMCAM_CMD, cmd);
		DBG_8192D("print WOWCAM  offset[0]  =%x\n", rtw_read32(padapter, REG_WKFMCAM_RWD));
		cmd = BIT(31)|(idx+1);
		rtw_write32(padapter, REG_WKFMCAM_CMD, cmd);
		DBG_8192D("print WOWCAM  offset[1]  =%x\n", rtw_read32(padapter, REG_WKFMCAM_RWD));
		cmd = BIT(31)|(idx+2);
		rtw_write32(padapter, REG_WKFMCAM_CMD, cmd);
		DBG_8192D("print WOWCAM  offset[2]  =%x\n", rtw_read32(padapter, REG_WKFMCAM_RWD));
		cmd = BIT(31)|(idx+3);
		rtw_write32(padapter, REG_WKFMCAM_CMD, cmd);
		DBG_8192D("print WOWCAM  offset[3]  =%x\n", rtw_read32(padapter, REG_WKFMCAM_RWD));
		cmd = BIT(31)|(idx+4);
		rtw_write32(padapter, REG_WKFMCAM_CMD, cmd);
		DBG_8192D("print WOWCAM  offset[4]  =%x\n", rtw_read32(padapter, REG_WKFMCAM_RWD));

	}
}
#endif /* CONFIG_WOWLAN */

/*  */
/*  */
/*  LLT R/W/Init function */
/*  */
/*  */
static u8 _LLTWrite(
	struct rtw_adapter *	adapter,
	u32		address,
	u32		data
	)
{
	u8	status = _SUCCESS;
	int	count = 0;
	u32	value = _LLT_INIT_ADDR(address) | _LLT_INIT_DATA(data) | _LLT_OP(_LLT_WRITE_ACCESS);

	rtw_write32(adapter, REG_LLT_INIT, value);

	/* polling */
	do{

		value = rtw_read32(adapter, REG_LLT_INIT);
		if (_LLT_NO_ACTIVE == _LLT_OP_VALUE(value)) {
			break;
		}

		if (count > POLLING_LLT_THRESHOLD) {
			/* RT_TRACE(COMP_INIT, DBG_SERIOUS, ("Failed to polling write LLT done at address %d!\n", address)); */
			status = _FAIL;
			break;
		}
	}while (count++);

	return status;
}

static u8 _LLTRead(
	struct rtw_adapter *	adapter,
	u32		address
	)
{
	int		count = 0;
	u32		value = _LLT_INIT_ADDR(address) | _LLT_OP(_LLT_READ_ACCESS);

	rtw_write32(adapter, REG_LLT_INIT, value);

	/* polling and get value */
	do{

		value = rtw_read32(adapter, REG_LLT_INIT);
		if (_LLT_NO_ACTIVE == _LLT_OP_VALUE(value)) {
			return (u8)value;
		}

		if (count > POLLING_LLT_THRESHOLD) {
			/* RT_TRACE(COMP_INIT, DBG_SERIOUS, ("Failed to polling read LLT done at address %d!\n", address)); */
			break;
		}
	}while (count++);

	return 0xFF;
}

static u8 InitLLTTable(
	struct rtw_adapter *	adapter,
	u32		boundary
	)
{
	u8		status = _SUCCESS;
	u32		i;
	u32		txpktbuf_bndy = boundary;
	u32		Last_Entry_Of_TxPktBuf = LAST_ENTRY_OF_TX_PKT_BUFFER;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	if (pHalData->MacPhyMode92D != SINGLEMAC_SINGLEPHY) {
		/* for 92du two mac: The page size is different from 92c and 92s */
		txpktbuf_bndy = TX_PAGE_BOUNDARY_DUAL_MAC;
		Last_Entry_Of_TxPktBuf = LAST_ENTRY_OF_TX_PKT_BUFFER_DUAL_MAC;
	}
	else {
		txpktbuf_bndy = boundary;
		Last_Entry_Of_TxPktBuf = LAST_ENTRY_OF_TX_PKT_BUFFER;
		/* txpktbuf_bndy = 253; */
		/* Last_Entry_Of_TxPktBuf = 255; */
	}

	for (i = 0 ; i < (txpktbuf_bndy - 1) ; i++) {
		status = _LLTWrite(adapter, i , i + 1);
		if (_SUCCESS != status) {
			return status;
		}
	}

	/*  end of list */
	status = _LLTWrite(adapter, (txpktbuf_bndy - 1), 0xFF);
	if (_SUCCESS != status) {
		return status;
	}

	/*  Make the other pages as ring buffer */
	/*  This ring buffer is used as beacon buffer if we config this MAC as two MAC transfer. */
	/*  Otherwise used as local loopback buffer. */
	for (i = txpktbuf_bndy ; i < Last_Entry_Of_TxPktBuf ; i++) {
		status = _LLTWrite(adapter, i, (i + 1));
		if (_SUCCESS != status) {
			return status;
		}
	}

	/*  Let last entry point to the start entry of ring buffer */
	status = _LLTWrite(adapter, Last_Entry_Of_TxPktBuf, txpktbuf_bndy);
	if (_SUCCESS != status) {
		return status;
	}

	return status;
}

/*  */
/*  */
/*	MAC init functions */
/*  */
/*  */
static void _SetMacID(struct rtw_adapter *adapter, u8 *MacID)
{
	u32 i;
	for (i = 0 ; i< MAC_ADDR_LEN ; i++) {
#ifdef  CONFIG_CONCURRENT_MODE
		if (adapter->iface_type == IFACE_PORT1)
			rtw_write32(adapter, REG_MACID1+i, MacID[i]);
		else
#endif
		rtw_write32(adapter, REG_MACID+i, MacID[i]);
	}
}

static void _SetBSSID( struct rtw_adapter *adapter, u8 *BSSID)
{
	u32 i;
	for (i = 0 ; i< MAC_ADDR_LEN ; i++) {
#ifdef  CONFIG_CONCURRENT_MODE
		if (adapter->iface_type == IFACE_PORT1)
			rtw_write32(adapter, REG_BSSID1+i, BSSID[i]);
		else
#endif
		rtw_write32(adapter, REG_BSSID+i, BSSID[i]);
	}
}

/*  Shall USB interface init this? */
static void _InitInterrupt(struct rtw_adapter *adapter)
{
	u32	value32;

	/*  HISR - turn all on */
	value32 = 0xFFFFFFFF;
	rtw_write32(adapter, REG_HISR, value32);

	/*  HIMR - turn all on */
	rtw_write32(adapter, REG_HIMR, value32);
}

static void _InitQueueReservedPage(struct rtw_adapter *adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct registry_priv *pregistrypriv = &adapter->registrypriv;

	u32			outEPNum	= (u32)pHalData->OutEpNumber;
	u32			numHQ		= 0;
	u32			numLQ		= 0;
	u32			numNQ		= 0;
	u32			numPubQ;
	u32			value32;
	u8			value8;
	u32			txQPageNum, txQPageUnit, txQRemainPage;

	if (!pregistrypriv->wifi_spec)
	{
		if (pHalData->MacPhyMode92D != SINGLEMAC_SINGLEPHY)
		{
			numPubQ = NORMAL_PAGE_NUM_PUBQ_92D_DUAL_MAC;
			txQPageNum = TX_TOTAL_PAGE_NUMBER_92D_DUAL_MAC- numPubQ;
		}
		else
		{
			numPubQ = TEST_PAGE_NUM_PUBQ;
			/* RT_ASSERT((numPubQ < TX_TOTAL_PAGE_NUMBER), ("Public queue page number is great than total tx page number.\n")); */
			txQPageNum = TX_TOTAL_PAGE_NUMBER - numPubQ;
		}

		if ((pHalData->MacPhyMode92D != SINGLEMAC_SINGLEPHY)&&(outEPNum == 3))
		{/*  temply for DMDP/DMSP Page allocate */
			numHQ = NORMAL_PAGE_NUM_HPQ_92D_DUAL_MAC;
			numLQ = NORMAL_PAGE_NUM_LPQ_92D_DUAL_MAC;
			numNQ = NORMAL_PAGE_NUM_NORMALQ_92D_DUAL_MAC;
		}
		else
		{
			txQPageUnit = txQPageNum/outEPNum;
			txQRemainPage = txQPageNum % outEPNum;

			if (pHalData->OutEpQueueSel & TX_SELE_HQ) {
				numHQ = txQPageUnit;
			}
			if (pHalData->OutEpQueueSel & TX_SELE_LQ) {
				numLQ = txQPageUnit;
			}
			/*  HIGH priority queue always present in the configuration of 2 or 3 out-ep */
			/*  so , remainder pages have assigned to High queue */
			if ((outEPNum>1) && (txQRemainPage)) {
				numHQ += txQRemainPage;
			}

			/*  NOTE: This step shall be proceed before writting REG_RQPN. */
			if (pHalData->OutEpQueueSel & TX_SELE_NQ)
				numNQ = txQPageUnit;

			value8 = (u8)_NPQ(numNQ);
			rtw_write8(adapter, REG_RQPN_NPQ, value8);
		}
	}
	else { /* for WMM */
		/*  92du wifi config only for SMSP */

		numPubQ = (outEPNum == 2)?WMM_NORMAL_PAGE_NUM_PUBQ:WMM_NORMAL_PAGE_NUM_PUBQ_92D;

		if (pHalData->OutEpQueueSel & TX_SELE_HQ)
			numHQ = (outEPNum == 2)?WMM_NORMAL_PAGE_NUM_HPQ:WMM_NORMAL_PAGE_NUM_HPQ_92D;

		if (pHalData->OutEpQueueSel & TX_SELE_LQ)
			numLQ = (outEPNum == 2)?WMM_NORMAL_PAGE_NUM_LPQ:WMM_NORMAL_PAGE_NUM_LPQ_92D;

		if (pHalData->OutEpQueueSel & TX_SELE_NQ) {
			numNQ = (outEPNum == 2)?WMM_NORMAL_PAGE_NUM_NPQ:WMM_NORMAL_PAGE_NUM_NPQ_92D;
			value8 = (u8)_NPQ(numNQ);
			rtw_write8(adapter, REG_RQPN_NPQ, value8);
		}
	}

	/*  TX DMA */
	value32 = _HPQ(numHQ) | _LPQ(numLQ) | _PUBQ(numPubQ) | LD_RQPN;
	rtw_write32(adapter, REG_RQPN, value32);
}

static void _InitTxBufferBoundary(struct rtw_adapter *adapter)
{
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	struct hal_data_8192du *pHalData	= GET_HAL_DATA(adapter);

	/* u16	txdmactrl; */
	u8	txpktbuf_bndy;

	if (!pregistrypriv->wifi_spec) {
		txpktbuf_bndy = TX_PAGE_BOUNDARY;
	}
	else {/* for WMM */
		txpktbuf_bndy = WMM_NORMAL_TX_PAGE_BOUNDARY;
	}

	if (pHalData->MacPhyMode92D != SINGLEMAC_SINGLEPHY)
		txpktbuf_bndy = TX_PAGE_BOUNDARY_DUAL_MAC;

	rtw_write8(adapter, REG_TXPKTBUF_BCNQ_BDNY, txpktbuf_bndy);
	rtw_write8(adapter, REG_TXPKTBUF_MGQ_BDNY, txpktbuf_bndy);
	rtw_write8(adapter, REG_TXPKTBUF_WMAC_LBK_BF_HD, txpktbuf_bndy);
	rtw_write8(adapter, REG_TRXFF_BNDY, txpktbuf_bndy);
	rtw_write8(adapter, REG_TDECTRL+1, txpktbuf_bndy);
}

static void _InitNormalChipRegPriority(struct rtw_adapter *adapter, u16 beQ,
				       u16 bkQ, u16 viQ, u16 voQ,
				       u16 mgtQ, u16 hiQ)
{
	u16 value16 = (rtw_read16(adapter, REG_TRXDMA_CTRL) & 0x7);

	value16 |= _TXDMA_BEQ_MAP(beQ) | _TXDMA_BKQ_MAP(bkQ) |
		   _TXDMA_VIQ_MAP(viQ) | _TXDMA_VOQ_MAP(voQ) |
		   _TXDMA_MGQ_MAP(mgtQ) | _TXDMA_HIQ_MAP(hiQ);

	rtw_write16(adapter, REG_TRXDMA_CTRL, value16);
}

static void _InitNormalChipOneOutEpPriority(struct rtw_adapter *adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

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
		break;
	}

	_InitNormalChipRegPriority(adapter, value, value, value,
				   value, value, value);
}

static void _InitNormalChipTwoOutEpPriority(struct rtw_adapter *adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	u16 beQ, bkQ, viQ, voQ, mgtQ, hiQ;

	u16	valueHi = 0;
	u16	valueLow = 0;

	switch (pHalData->OutEpQueueSel)
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
			/* RT_ASSERT(FALSE, ("Shall not reach here!\n")); */
			break;
	}

	if (!pregistrypriv->wifi_spec) {
		beQ		= valueLow;
		bkQ		= valueLow;
		viQ		= valueHi;
		voQ		= valueHi;
		mgtQ	= valueHi;
		hiQ		= valueHi;
	}
	else {/* for WMM , CONFIG_OUT_EP_WIFI_MODE */
		beQ		= valueLow;
		bkQ		= valueHi;
		viQ		= valueHi;
		voQ		= valueLow;
		mgtQ	= valueHi;
		hiQ		= valueHi;
	}

	_InitNormalChipRegPriority(adapter, beQ, bkQ, viQ, voQ, mgtQ, hiQ);
}

static void _InitNormalChipThreeOutEpPriority(struct rtw_adapter *adapter)
{
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	u16			beQ, bkQ, viQ, voQ, mgtQ, hiQ;

	if (!pregistrypriv->wifi_spec) {/*  typical setting */
		beQ		= QUEUE_LOW;
		bkQ		= QUEUE_LOW;
		viQ		= QUEUE_NORMAL;
		voQ		= QUEUE_HIGH;
		mgtQ	= QUEUE_HIGH;
		hiQ		= QUEUE_HIGH;
	}
	else {/*  for WMM */
		beQ		= QUEUE_LOW;
		bkQ		= QUEUE_NORMAL;
		viQ		= QUEUE_NORMAL;
		voQ		= QUEUE_HIGH;
		mgtQ	= QUEUE_HIGH;
		hiQ		= QUEUE_HIGH;
	}
	_InitNormalChipRegPriority(adapter, beQ, bkQ, viQ, voQ, mgtQ, hiQ);
}

static void _InitQueuePriority(struct rtw_adapter *adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	switch (pHalData->OutEpNumber) {
	case 1:
		_InitNormalChipOneOutEpPriority(adapter);
		break;
	case 2:
		_InitNormalChipTwoOutEpPriority(adapter);
		break;
	case 3:
		_InitNormalChipThreeOutEpPriority(adapter);
		break;
	default:
		break;
	}
}

static void _InitNetworkType(struct rtw_adapter *adapter)
{
	u32	value32;

	value32 = rtw_read32(adapter, REG_CR);

	/*  TODO: use the other function to set network type */
#if RTL8191C_FPGA_NETWORKTYPE_ADHOC
	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AD_HOC);
#else
	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AP);
#endif
	rtw_write32(adapter, REG_CR, value32);
}

static void _InitTransferPageSize(struct rtw_adapter *adapter)
{
	/*  Tx page size is always 128. */

	u8	value8;
	value8 = _PSRX(PBP_128) | _PSTX(PBP_128);
	rtw_write8(adapter, REG_PBP, value8);
}

static void
_InitDriverInfoSize(
	struct rtw_adapter *	adapter,
	u8		drvInfoSize
	)
{
	rtw_write8(adapter, REG_RX_DRVINFO_SZ, drvInfoSize);
}

static void _InitWMACSetting(struct rtw_adapter *adapter)
{
	/* u4Byte			value32; */
	/* u16			value16; */
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	/* pHalData->ReceiveConfig = AAP | APM | AM | AB | APP_ICV | ADF | AMF | APP_FCS | HTC_LOC_CTRL | APP_MIC | APP_PHYSTS; */
	/* pHalData->ReceiveConfig = AAP | APM | AM | AB | CBSSID |CBSSID_BCN | APP_ICV | AMF | HTC_LOC_CTRL | APP_MIC | APP_PHYSTS; */

	/*  don't turn on AAP, it will allow all packets to driver */
	pHalData->ReceiveConfig = APM | AM | AB | CBSSID |CBSSID_BCN | APP_ICV | AMF | HTC_LOC_CTRL | APP_MIC | APP_PHYSTS;

#if (0 == RTL8192C_RX_PACKET_NO_INCLUDE_CRC)
	pHalData->ReceiveConfig |= ACRC32;
#endif

	rtw_write32(adapter, REG_RCR, pHalData->ReceiveConfig);

	/*  Accept all multicast address */
	rtw_write32(adapter, REG_MAR, 0xFFFFFFFF);
	rtw_write32(adapter, REG_MAR+4, 0xFFFFFFFF);
}

static void _InitAdaptiveCtrl(struct rtw_adapter *adapter)
{
	u16	value16;
	u32	value32;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	/*  Response Rate Set */
	value32 = rtw_read32(adapter, REG_RRSR);
	value32 &= ~RATE_BITMAP_ALL;
	if (pHalData->CurrentBandType92D == BAND_ON_5G)
	{
		value32 |= RATE_RRSR_WITHOUT_CCK;
	}
	else
	{
		value32 |= RATE_RRSR_CCK_ONLY_1M;
	}
	rtw_write32(adapter, REG_RRSR, value32);

	/*  CF-END Threshold */
	/* m_spIoBase->rtw_write8(REG_CFEND_TH, 0x1); */

	/*  SIFS (used in NAV) */
	value16 = _SPEC_SIFS_CCK(0x10) | _SPEC_SIFS_OFDM(0x10);
	rtw_write16(adapter, REG_SPEC_SIFS, value16);

	/*  Retry Limit */
	value16 = _LRL(0x30) | _SRL(0x30);
	rtw_write16(adapter, REG_RL, value16);
}

static void _InitRateFallback(struct rtw_adapter *adapter)
{
	/*  Set Data Auto Rate Fallback Retry Count register. */
	rtw_write32(adapter, REG_DARFRC, 0x00000000);
	rtw_write32(adapter, REG_DARFRC+4, 0x10080404);
	rtw_write32(adapter, REG_RARFRC, 0x04030201);
	rtw_write32(adapter, REG_RARFRC+4, 0x08070605);
}

static void _InitEDCA(struct rtw_adapter *adapter)
{
	u16				value16;

	/* disable EDCCA count down, to reduce collison and retry */
	value16 = rtw_read16(adapter, REG_RD_CTRL);
	value16 |= DIS_EDCA_CNT_DWN;
	rtw_write16(adapter, REG_RD_CTRL, value16);

	/*  Update SIFS timing.  ?????????? */
	/* pHalData->SifsTime = 0x0e0e0a0a; */
	/* rtw_hal_set_hwreg(adapter, HW_VAR_RESP_SIFS,  (pu1Byte)&pHalData->SifsTime); */
	/*  SIFS for CCK Data ACK */
	rtw_write8(adapter, REG_SIFS_CTX, 0xa);
	/*  SIFS for CCK consecutive tx like CTS data! */
	rtw_write8(adapter, REG_SIFS_CTX+1, 0xa);

	/*  SIFS for OFDM Data ACK */
	rtw_write8(adapter, REG_SIFS_TRX, 0xe);
	/*  SIFS for OFDM consecutive tx like CTS data! */
	rtw_write8(adapter, REG_SIFS_TRX+1, 0xe);

	/*  Set CCK/OFDM SIFS */
	rtw_write16(adapter, REG_SIFS_CTX, 0x0a0a); /*  CCK SIFS shall always be 10us. */
	rtw_write16(adapter, REG_SIFS_TRX, 0x1010);

	rtw_write16(adapter, REG_PROT_MODE_CTRL, 0x0204);

	rtw_write32(adapter, REG_BAR_MODE_CTRL, 0x014004);

	/*  TXOP */
	rtw_write32(adapter, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtw_write32(adapter, REG_EDCA_BK_PARAM, 0x0000A44F);
	rtw_write32(adapter, REG_EDCA_VI_PARAM, 0x005EA324);
	rtw_write32(adapter, REG_EDCA_VO_PARAM, 0x002FA226);

	/*  PIFS */
	rtw_write8(adapter, REG_PIFS, 0x1C);

	/* AGGR BREAK TIME Register */
	rtw_write8(adapter, REG_AGGR_BREAK_TIME, 0x16);

	rtw_write16(adapter, REG_NAV_PROT_LEN, 0x0040);

	rtw_write8(adapter, REG_BCNDMATIM, 0x02);

	rtw_write8(adapter, REG_ATIMWND, 0x02);
}

static void _InitAMPDUAggregation(struct rtw_adapter *adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	/* rtw_write32(adapter, REG_AGGLEN_LMT, 0x99997631); */

	if (pHalData->MacPhyMode92D == SINGLEMAC_SINGLEPHY)
		rtw_write32(adapter, REG_AGGLEN_LMT, 0x88728841);
	else if (pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
		rtw_write32(adapter, REG_AGGLEN_LMT, 0x44444441);
	else if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY)
		rtw_write32(adapter, REG_AGGLEN_LMT, 0x66525541);

	rtw_write8(adapter, REG_AGGR_BREAK_TIME, 0x16);
}

static void _InitBeaconMaxError(struct rtw_adapter *adapter, bool InfraMode)
{
#ifdef RTL8192CU_ADHOC_WORKAROUND_SETTING
	rtw_write8(adapter, REG_BCN_MAX_ERR,  0xFF);
#endif
}

static void _InitRDGSetting(struct rtw_adapter *adapter)
{
	rtw_write8(adapter, REG_RD_CTRL, 0xFF);
	rtw_write16(adapter, REG_RD_NAV_NXT, 0x200);
	rtw_write8(adapter, REG_RD_RESP_PKT_TH, 0x05);
}

static void _InitRetryFunction(struct rtw_adapter *adapter)
{
	u8	value8;

	value8 = rtw_read8(adapter, REG_FWHW_TXQ_CTRL);
	value8 |= EN_AMPDU_RTY_NEW;
	rtw_write8(adapter, REG_FWHW_TXQ_CTRL, value8);

	/*  Set ACK timeout */
	rtw_write8(adapter, REG_ACKTO, 0x40);
}

static void _InitUsbAggregationSetting(struct rtw_adapter *adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8 valuedma;
	u8 valueusb;
	u32 value32;

	if (adapter->registrypriv.wifi_spec)
		pHalData->UsbTxAggMode = false;

	if (pHalData->MacPhyMode92D!= SINGLEMAC_SINGLEPHY)
		pHalData->UsbTxAggDescNum = 2;

	if (pHalData->UsbTxAggMode) {
		value32 = rtw_read32(adapter, REG_TDECTRL);
		value32 = value32 & ~(BLK_DESC_NUM_MASK << BLK_DESC_NUM_SHIFT);
		value32 |= ((pHalData->UsbTxAggDescNum & BLK_DESC_NUM_MASK) << BLK_DESC_NUM_SHIFT);

		rtw_write32(adapter, REG_TDECTRL, value32);
	}

	/*  Rx aggregation setting */
	if (pHalData->MacPhyMode92D!= SINGLEMAC_SINGLEPHY) {
		pHalData->UsbRxAggPageCount	= 24;
		pHalData->UsbRxAggPageTimeout = 0x6;
	}
	valuedma = rtw_read8(adapter, REG_TRXDMA_CTRL);
	valueusb = rtw_read8(adapter, REG_USB_SPECIAL_OPTION);

	switch (pHalData->UsbRxAggMode) {
	case USB_RX_AGG_DMA:
		valuedma |= RXDMA_AGG_EN;
		valueusb &= ~USB_AGG_EN;
		break;
	case USB_RX_AGG_USB:
		valuedma &= ~RXDMA_AGG_EN;
		valueusb |= USB_AGG_EN;
		break;
	case USB_RX_AGG_DMA_USB:
		valuedma |= RXDMA_AGG_EN;
		valueusb |= USB_AGG_EN;
		break;
	case USB_RX_AGG_DISABLE:
	default:
		valuedma &= ~RXDMA_AGG_EN;
		valueusb &= ~USB_AGG_EN;
		break;
	}

	rtw_write8(adapter, REG_TRXDMA_CTRL, valuedma);
	rtw_write8(adapter, REG_USB_SPECIAL_OPTION, valueusb);
	switch (pHalData->UsbRxAggMode) {
		case USB_RX_AGG_DMA:
			rtw_write8(adapter, REG_RXDMA_AGG_PG_TH, pHalData->UsbRxAggPageCount);
			rtw_write8(adapter, REG_USB_DMA_AGG_TO, pHalData->UsbRxAggPageTimeout);
			break;
		case USB_RX_AGG_USB:
			rtw_write8(adapter, REG_USB_AGG_TH, pHalData->UsbRxAggBlockCount);
			rtw_write8(adapter, REG_USB_AGG_TO, pHalData->UsbRxAggBlockTimeout);
			break;
		case USB_RX_AGG_DMA_USB:
			rtw_write8(adapter, REG_RXDMA_AGG_PG_TH, pHalData->UsbRxAggPageCount);
			rtw_write8(adapter, REG_USB_DMA_AGG_TO, pHalData->UsbRxAggPageTimeout);
			rtw_write8(adapter, REG_USB_AGG_TH, pHalData->UsbRxAggBlockCount);
			rtw_write8(adapter, REG_USB_AGG_TO, pHalData->UsbRxAggBlockTimeout);
			break;
		case USB_RX_AGG_DISABLE:
		default:
			/*  TODO: */
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
		break;
	}
}

static void _InitOperationMode(struct rtw_adapter *adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8 regBwOpMode = 0, MinSpaceCfg = 0;

	/* 1 This part need to modified according to the rate set we filtered!! */
	/*  */
	/*  Set RRSR, RATR, and REG_BWOPMODE registers */
	/*  */
	switch (pHalData->CurrentWirelessMode) {
	case WIRELESS_MODE_B:
		regBwOpMode = BW_OPMODE_20MHZ;
		break;
	case WIRELESS_MODE_A:
		/* RT_ASSERT(FALSE, ("Error wireless a mode\n")); */
		regBwOpMode = BW_OPMODE_5G |BW_OPMODE_20MHZ;
		break;
	case WIRELESS_MODE_G:
		regBwOpMode = BW_OPMODE_20MHZ;
		break;
	case WIRELESS_MODE_UNKNOWN:
	case WIRELESS_MODE_AUTO:
		regBwOpMode = BW_OPMODE_20MHZ;
		break;
	case WIRELESS_MODE_N_24G:
		/*  It support CCK rate by default. */
		/*  CCK rate will be filtered out only when associated AP does not support it. */
		regBwOpMode = BW_OPMODE_20MHZ;
		break;
	case WIRELESS_MODE_N_5G:
		regBwOpMode = BW_OPMODE_5G;
		break;
	}

	/*  Ziv ???????? */
	rtw_write8(adapter, REG_BWOPMODE, regBwOpMode);

	/*  For Min Spacing configuration. */
	switch (pHalData->rf_type) {
	case RF_1T2R:
	case RF_1T1R:
		MinSpaceCfg = (MAX_MSS_DENSITY_1T<<3);
		break;
	case RF_2T2R:
	case RF_2T2R_GREEN:
		MinSpaceCfg = (MAX_MSS_DENSITY_2T<<3);
		break;
	}

	rtw_write8(adapter, REG_AMPDU_MIN_SPACE, MinSpaceCfg);
}

static void _InitSecuritySetting(struct rtw_adapter *adapter)
{
	invalidate_cam_all(adapter);
}

 static void _InitBeaconParameters(struct rtw_adapter *adapter)
{
	rtw_write16(adapter, REG_BCN_CTRL, 0x1010);

	/* default value  for register 0x558 and 0x559 is  0x05 0x03
	 * (92DU before bitfile0821)
	 */
	rtw_write16(adapter, REG_TBTT_PROHIBIT, 0x3c02);/*  ms */
	rtw_write8(adapter, REG_DRVERLYINT, 0x05);/* ms */
	rtw_write8(adapter, REG_BCNDMATIM, 0x03);

	/* Change beacon AIFS to the largest number
	 * beacause test chip does not detect contention
	 * before sending beacon
	 */
	rtw_write16(adapter, REG_BCNTCFG, 0x660F);
}

static void _InitRFType(struct rtw_adapter *adapter)
{
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(adapter);

#if (DISABLE_BB_RF == 1)
	pHalData->rf_chip	= RF_PSEUDO_11N;
	pHalData->rf_type	= RF_1T1R;/*  RF_2T2R; */
#else

	pHalData->rf_chip	= RF_6052;

	if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY)
	{
		pHalData->rf_type = RF_1T1R;
	}
	else {/*  SMSP OR DMSP */
		pHalData->rf_type = RF_2T2R;
	}
#endif
}

#if RTL8192CU_ADHOC_WORKAROUND_SETTING
static void _InitAdhocWorkaroundParams(struct rtw_adapter *adapter)
{
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(adapter);
	pHalData->RegBcnCtrlVal = rtw_read8(adapter, REG_BCN_CTRL);
	pHalData->RegTxPause = rtw_read8(adapter, REG_TXPAUSE);
	pHalData->RegFwHwTxQCtrl = rtw_read8(adapter, REG_FWHW_TXQ_CTRL+2);
	pHalData->RegReg542 = rtw_read8(adapter, REG_TBTT_PROHIBIT+2);
	pHalData->RegCR_1 = rtw_read8(adapter, REG_CR+1);
}
#endif

static void
_BeaconFunctionEnable(
	struct rtw_adapter *		adapter,
	bool			Enable,
	bool			Linked
	)
{
	/*  20100901 zhiyuan: Change original setting of BCN_CTRL(0x550) from */
	/*  0x1a to 0x1b. Set BIT0 of this register disable ATIM  function. */
	/*   enable ATIM function may invoke HW Tx stop operation. This may cause ping failed */
	/*  sometimes in long run test. So just disable it now. */
	/*  When ATIM function is disabled, High Queue should not use anymore. */
	rtw_write8(adapter, REG_BCN_CTRL, 0x1b);
	rtw_write8(adapter, REG_RD_CTRL+1, 0x6F);
}

/*  Set CCK and OFDM Block "ON" */
static void _BBTurnOnBlock(
	struct rtw_adapter *		adapter
	)
{
	struct hal_data_8192du		*pHalData	= GET_HAL_DATA(adapter);
#if (DISABLE_BB_RF)
	return;
#endif

	if (pHalData->CurrentBandType92D == BAND_ON_5G)
		PHY_SetBBReg(adapter, rFPGA0_RFMOD, bCCKEn|bOFDMEn, 0x2);
	else
		PHY_SetBBReg(adapter, rFPGA0_RFMOD, bCCKEn|bOFDMEn, 0x3);
}

static void _RfPowerSave(
	struct rtw_adapter *		adapter
	)
{
	struct hal_data_8192du	*pHalData	= GET_HAL_DATA(adapter);
	struct pwrctrl_priv		*pwrctrlpriv = &adapter->pwrctrlpriv;
	u8			eRFPath;

#if (DISABLE_BB_RF)
	return;
#endif

	if (pwrctrlpriv->reg_rfoff == true) { /*  User disable RF via registry. */
		/* RT_TRACE((COMP_INIT|COMP_RF), DBG_LOUD, ("Initializeadapter8192CUsb(): Turn off RF for RegRfOff.\n")); */
		/* MgntActSet_RF_State(adapter, rf_off, RF_CHANGE_BY_SW, true); */
		/*  Those action will be discard in MgntActSet_RF_State because off the same state */
#ifdef CONFIG_DUALMAC_CONCURRENT
		if (pHalData->bSlaveOfDMSP)
			return;
#endif
		for (eRFPath = 0; eRFPath <pHalData->NumTotalRFPath; eRFPath++)
			PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)eRFPath, 0x4, 0xC00, 0x0);
	}
	else if (pwrctrlpriv->rfoff_reason > RF_CHANGE_BY_PS) { /*  H/W or S/W RF OFF before sleep. */
		/* RT_TRACE((COMP_INIT|COMP_RF), DBG_LOUD, ("Initializeadapter8192CUsb(): Turn off RF for RfOffReason(%ld).\n", pMgntInfo->RfOffReason)); */
		/* MgntActSet_RF_State(adapter, rf_off, pMgntInfo->RfOffReason, true); */
	}
	else {
		pwrctrlpriv->rf_pwrstate = rf_on;
		pwrctrlpriv->rfoff_reason = 0;
		/* if (adapter->bInSetPower || adapter->bResetInProgress) */
		/*	PlatformUsbEnableInPipes(adapter); */
		/* RT_TRACE((COMP_INIT|COMP_RF), DBG_LOUD, ("Initializeadapter8192CUsb(): RF is on.\n")); */
	}
}

static void init_hwled(struct rtw_adapter *adapter)
{
	struct led_priv *pledpriv = &(adapter->ledpriv);

	if (pledpriv->LedStrategy != HW_LED)
			return;
}

#ifdef CONFIG_WOWLAN
static void dump_wakup_reason(struct rtw_adapter *padapter)
{
	switch (rtw_read8(padapter, REG_WOWLAN_REASON))
	{
		case Rx_Pairwisekey:
			DBG_8192D("Rx_Pairwisekey\n");
			break;
		case Rx_GTK:
			DBG_8192D("Rx_GTK\n");
			break;
		case Rx_DisAssoc:
			DBG_8192D("Rx_DisAssoc\n");
			break;
		case Rx_DeAuth:
			DBG_8192D("Rx_DeAuth\n");
			break;
		case FWDecisionDisconnect:
			DBG_8192D("FWDecisionDisconnect\n");
			break;
		case Rx_MagicPkt:
			DBG_8192D("Rx_MagicPkt\n");
			break;
		case FinishBtFwPatch:
			DBG_8192D("FinishBtFwPatch\n");
			break;
		default:
			DBG_8192D("UNKNOW reason\n");
			break;
	}
}
#endif /* CONFIG_WOWLAN */

static u32 rtl8192du_hal_init(struct rtw_adapter *padapter)
{
	u8	val8 = 0, tmpU1b;
	u16	val16;
	u32	boundary, i = 0,  status = _SUCCESS;
#if SWLCK == 0
	u32	j;
#endif /* SWLCK == 0 */
	struct hal_data_8192du  *pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv		*pwrctrlpriv = &padapter->pwrctrlpriv;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
#ifdef CONFIG_DUALMAC_CONCURRENT
	struct rtw_adapter *	Buddyadapter = padapter->pbuddy_adapter;
#endif
	u32 init_start_time = rtw_get_current_time();

	padapter->init_adpt_in_progress = true;

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (Buddyadapter != NULL)
	{
		if (Buddyadapter->bHaltInProgress)
		{
			for (i = 0;i<100;i++)
			{
				rtw_usleep_os(1000);
				if (!Buddyadapter->bHaltInProgress)
					break;
			}

			if (i == 100)
			{
				DBG_8192D("fail to initialization due to another adapter is in halt\n");
				return _FAIL;
			}
		}
	}
#endif
	/* RT_TRACE(COMP_INIT, DBG_LOUD, ("--->Initializeadapter8192CUsb()\n")); */

	if (padapter->bSurpriseRemoved)
		return _FAIL;

	/* Let the first starting mac load RF parameters and do LCK in this case, by wl */
	if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY
		&& ((pHalData->interfaceIndex == 0 && pHalData->BandSet92D == BAND_ON_2_4G)
			|| (pHalData->interfaceIndex == 1 && pHalData->BandSet92D == BAND_ON_5G)))
		ACQUIRE_GLOBAL_MUTEX(GlobalMutexForMac0_2G_Mac1_5G);

	ACQUIRE_GLOBAL_MUTEX(GlobalMutexForPowerAndEfuse);

	rtw_write8(padapter, REG_RSV_CTRL, 0x0);
	val8 = rtw_read8(padapter, 0x0003);
	val8 &= (~BIT7);
	rtw_write8(padapter, 0x0003, val8);

#ifdef CONFIG_WOWLAN
	if (rtw_read8(padapter, REG_MCUFWDL)&BIT7)
	{
		u8 reg_val = 0;
		rtl8192d_FirmwareSelfReset(padapter);
		rtw_write8(padapter, REG_MCUFWDL, 0x00);
		/* before BB reset should do clock gated */
		rtw_write32(padapter, rFPGA0_XCD_RFParameter, rtw_read32(padapter, rFPGA0_XCD_RFParameter)|(BIT31));
		/* reset BB */
		reg_val = rtw_read8(padapter, REG_SYS_FUNC_EN);
		reg_val &= ~(BIT(0) | BIT(1));
		rtw_write8(padapter, REG_SYS_FUNC_EN, reg_val);
		/* reset RF */
		rtw_write8(padapter, REG_RF_CTRL, 0);
		/* reset TRX path */
		rtw_write16(padapter, REG_CR, 0);
		/* reset MAC, Digital Core */
		reg_val = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
		reg_val &= ~(BIT(4) | BIT(7));
		rtw_write8(padapter, REG_SYS_FUNC_EN+1, reg_val);
		reg_val = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
		reg_val |= BIT(4) | BIT(7);
		rtw_write8(padapter, REG_SYS_FUNC_EN+1, reg_val);
	}
#endif /* CONFIG_WOWLAN */
	/* mac status: */
	/* 0x81[4]:0 mac0 off, 1:mac0 on */
	/* 0x82[4]:0 mac1 off, 1: mac1 on. */

	/* For s3/s4 may reset mac, Reg0xf8 may be set to 0, so reset macphy control reg here. */
	PHY_ConfigMacPhyMode92D(padapter);

	PHY_SetPowerOnFor8192D(padapter);

	status = _InitPowerOn(padapter);
	if (status == _FAIL) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init power on!\n"));
		RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerAndEfuse);
		if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY
		&& ((pHalData->interfaceIndex == 0 && pHalData->BandSet92D == BAND_ON_2_4G)
			|| (pHalData->interfaceIndex == 1 && pHalData->BandSet92D == BAND_ON_5G)))
			RELEASE_GLOBAL_MUTEX(GlobalMutexForMac0_2G_Mac1_5G);
		goto exit;
	}

	if (!pregistrypriv->wifi_spec) {
		boundary = TX_PAGE_BOUNDARY;
	}
	else {/*  for WMM */
		boundary = WMM_NORMAL_TX_PAGE_BOUNDARY;
	}

	PHY_ConfigMacCoexist_RFPage92D(padapter);

	status =  InitLLTTable(padapter, boundary);
	if (status == _FAIL) {
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init power on!\n"));
		RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerAndEfuse);
		if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY
		&& ((pHalData->interfaceIndex == 0 && pHalData->BandSet92D == BAND_ON_2_4G)
			|| (pHalData->interfaceIndex == 1 && pHalData->BandSet92D == BAND_ON_5G)))
			RELEASE_GLOBAL_MUTEX(GlobalMutexForMac0_2G_Mac1_5G);
		return status;
	}

#if ((1 == MP_DRIVER) ||  (0 == FW_PROCESS_VENDOR_CMD))

	rtl8192d_PHY_InitRxSetting(padapter);
	RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerAndEfuse);
	DBG_8192D("%s(): Don't Download Firmware !!\n", __func__);
	padapter->bFWReady = false;
	pHalData->fw_ractrl = false;

#else

	status = FirmwareDownload92D(padapter, false);
	RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerAndEfuse);
	if (status == _FAIL) {
		padapter->bFWReady = false;
		pHalData->fw_ractrl = false;
		DBG_8192D("fw download fail!\n");

		/* return fail only when part number check fail, suggested by alex */
		if (0xE0 == rtw_read8(padapter, 0x1c5))
		{
			if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY
			&& ((pHalData->interfaceIndex == 0 && pHalData->BandSet92D == BAND_ON_2_4G)
				|| (pHalData->interfaceIndex == 1 && pHalData->BandSet92D == BAND_ON_5G)))
				RELEASE_GLOBAL_MUTEX(GlobalMutexForMac0_2G_Mac1_5G);

			goto exit;
		}
	}
	else	{
		padapter->bFWReady = true;
		pHalData->fw_ractrl = true;
		DBG_8192D("fw download ok!\n");
	}

#endif

	pHalData->LastHMEBoxNum = 0;

	if (pwrctrlpriv->reg_rfoff == true) {
		pwrctrlpriv->rf_pwrstate = rf_off;
	}

	/*  Set RF type for BB/RF configuration */
	_InitRFType(padapter);/* _ReadRFType() */

	/*  Save target channel */
	/*  <Roger_Notes> Current Channel will be updated again later. */

#if (HAL_MAC_ENABLE == 1)
	status = PHY_MACConfig8192D(padapter);
	if (status == _FAIL)
	{
		if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY
		&& ((pHalData->interfaceIndex == 0 && pHalData->BandSet92D == BAND_ON_2_4G)
			|| (pHalData->interfaceIndex == 1 && pHalData->BandSet92D == BAND_ON_5G)))
			RELEASE_GLOBAL_MUTEX(GlobalMutexForMac0_2G_Mac1_5G);
		goto exit;
	}
#endif

	_InitQueueReservedPage(padapter);
	_InitTxBufferBoundary(padapter);
	_InitQueuePriority(padapter);
	_InitTransferPageSize(padapter);

	/*  Get Rx PHY status in order to report RSSI and others. */
	_InitDriverInfoSize(padapter, DRVINFO_SZ);

	_InitInterrupt(padapter);
	hal_init_macaddr(padapter);/* set mac_address */
	_InitNetworkType(padapter);/* set msr */
	_InitWMACSetting(padapter);
	_InitAdaptiveCtrl(padapter);
	_InitEDCA(padapter);
	_InitRateFallback(padapter);
	_InitRetryFunction(padapter);
	_InitUsbAggregationSetting(padapter);
	_InitOperationMode(padapter);/* todo */
	_InitBeaconParameters(padapter);
	_InitAMPDUAggregation(padapter);
	_InitBeaconMaxError(padapter, true);

#if defined(CONFIG_CONCURRENT_MODE)

#ifdef CONFIG_CHECK_AC_LIFETIME
	/*  Enable lifetime check for the four ACs */
	rtw_write8(padapter, REG_LIFETIME_EN, 0x0F);
#endif	/*  CONFIG_CHECK_AC_LIFETIME */

	rtw_write16(padapter, REG_PKT_VO_VI_LIFE_TIME, 0x0400);	/*  unit: 256us. 256ms */
	rtw_write16(padapter, REG_PKT_BE_BK_LIFE_TIME, 0x0400);	/*  unit: 256us. 256ms */
#endif	/*  CONFIG_CONCURRENT_MODE */

	init_hwled(padapter);

	if (pHalData->bRDGEnable)
		_InitRDGSetting(padapter);

	/*  Set Data Auto Rate Fallback Reg. */
	for (i = 0 ; i < 4 ; i++)
		rtw_write32(padapter, REG_ARFR0+i*4, 0x1f8ffff0);

	if (pregistrypriv->wifi_spec) {
		rtw_write16(padapter, REG_FAST_EDCA_CTRL, 0);
	} else {
		if (pHalData->MacPhyMode92D == SINGLEMAC_SINGLEPHY) {
			if (pHalData->OutEpNumber == 2)  /*  suggested by chunchu */
				rtw_write32(padapter, REG_FAST_EDCA_CTRL, 0x03066666);
		       else
				rtw_write16(padapter, REG_FAST_EDCA_CTRL, 0x8888);
		} else {
			rtw_write16(padapter, REG_FAST_EDCA_CTRL, 0x5555);
		}
	}

	tmpU1b = rtw_read8(padapter, 0x605);
	tmpU1b|= 0xf0;
	rtw_write8(padapter, 0x605, tmpU1b);
	rtw_write8(padapter, 0x55e, 0x30);
	rtw_write8(padapter, 0x55f, 0x30);
	rtw_write8(padapter, 0x606, 0x30);

	/* for bitfile 0912/0923 zhiyuan 2009/09/23 */
	/*  temp for high queue and mgnt Queue corrupt in time; */
	/* it may cause hang when sw beacon use high_Q, other frame use mgnt_Q; or , sw beacon use mgnt_Q , other frame use high_Q; */
	rtw_write8(padapter, 0x523, 0x10);
	val16 = rtw_read16(padapter, 0x524);
	val16|= BIT12;
	rtw_write16(padapter, 0x524 , val16);

	rtw_write8(padapter, REG_TXPAUSE, 0);

	/*  suggested by zhouzhou   usb suspend  idle time count for bitfile0927  2009/10/09 zhiyuan */
	val8 = rtw_read8(padapter, 0xfe56);
	val8 |= (BIT0|BIT1);
	rtw_write8(padapter, 0xfe56, val8);

	if (pHalData->bEarlyModeEnable)
	{
		DBG_8192D("EarlyMode Enabled!!!\n");

		tmpU1b = rtw_read8(padapter, 0x4d0);
		tmpU1b = tmpU1b|0x1f;
		rtw_write8(padapter, 0x4d0, tmpU1b);

		rtw_write8(padapter, 0x4d3, 0x80);

		tmpU1b = rtw_read8(padapter, 0x605);
		tmpU1b = tmpU1b|0x40;
		rtw_write8(padapter, 0x605, tmpU1b);
	}
	else
	{
		rtw_write8(padapter, 0x4d0, 0);
	}

	/*  */
	/* d. Initialize BB related configurations. */
	/*  */
#if (HAL_BB_ENABLE == 1)
	status = PHY_BBConfig8192D(padapter);
	if (status == _FAIL)
	{
		if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY
		&& ((pHalData->interfaceIndex == 0 && pHalData->BandSet92D == BAND_ON_2_4G)
			|| (pHalData->interfaceIndex == 1 && pHalData->BandSet92D == BAND_ON_5G)))
			RELEASE_GLOBAL_MUTEX(GlobalMutexForMac0_2G_Mac1_5G);
		goto exit;
	}
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (pHalData->bSlaveOfDMSP)
	{
		DBG_8192D("slave of dmsp close phy1\n");
		PHY_StopTRXBeforeChangeBand8192D(padapter);
	}
#endif

	if (padapter->bFWReady && pHalData->FirmwareVersion >= 0x13)
	{
		pHalData->bReadRFbyFW = true;
		DBG_8192D("Enable 92du query RF by FW.\n");
	}
	else
	{
		pHalData->bReadRFbyFW = false;
	}

	/*  92CU use 3-wire to r/w RF */
	/*  */
	/*  e. Initialize RF related configurations. */
	/*  */
	/*  2007/11/02 MH Before initalizing RF. We can not use FW to do RF-R/W. */
	/* pHalData->Rf_Mode = RF_OP_By_SW_3wire; */
#if (HAL_RF_ENABLE == 1)
	/*  set before initialize RF, */
	PHY_SetBBReg(padapter, rFPGA0_AnalogParameter4, 0x00f00000,  0xf);

	status = PHY_RFConfig8192D(padapter);
	if (status == _FAIL)
	{
		if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY
		&& ((pHalData->interfaceIndex == 0 && pHalData->BandSet92D == BAND_ON_2_4G)
			|| (pHalData->interfaceIndex == 1 && pHalData->BandSet92D == BAND_ON_5G)))
			RELEASE_GLOBAL_MUTEX(GlobalMutexForMac0_2G_Mac1_5G);
		goto exit;
	}

	/*  set default value after initialize RF, */
	PHY_SetBBReg(padapter, rFPGA0_AnalogParameter4, 0x00f00000,  0);

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (!pHalData->bSlaveOfDMSP)
#endif
		PHY_UpdateBBRFConfiguration8192D(padapter, false);

#endif

#if RTL8192CU_ADHOC_WORKAROUND_SETTING
	_InitAdhocWorkaroundParams(padapter);
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (!pHalData->bSlaveOfDMSP)
#endif
		_BBTurnOnBlock(padapter);

	/* NicIFSetMacAddress(padapter, padapter->PermanentAddress); */

	if (pHalData->CurrentBandType92D == BAND_ON_5G)
	{
		pHalData->CurrentWirelessMode = WIRELESS_MODE_N_5G;
	}
	else
	{
		pHalData->CurrentWirelessMode = WIRELESS_MODE_N_24G;
	}

	_InitSecuritySetting(padapter);

	_RfPowerSave(padapter);

	/*  HW SEQ CTRL */
	/* set 0x0 to 0xFF by tynli. Default enable HW SEQ NUM. */
	rtw_write8(padapter, REG_HWSEQ_CTRL, 0xFF);

	/* schmitt trigger , improve tx evm for 92du, suggested by ynlin  12/03/2010 */
	tmpU1b = rtw_read8(padapter, REG_AFE_XTAL_CTRL);
	tmpU1b |= BIT1;
	rtw_write8(padapter, REG_AFE_XTAL_CTRL, tmpU1b);

	/* disable bar */
	rtw_write32(padapter, REG_BAR_MODE_CTRL, 0xffff);

	/* Nav limit , suggest by scott */
	rtw_write8(padapter, 0x652, 0x0);
	rtw_write8(padapter, 0xc87, 0x50);/* suggest by Jackson for CCA */

#if (MP_DRIVER == 1)
	padapter->mppriv.channel = pHalData->CurrentChannel;
	MPT_Initializeadapter(padapter, padapter->mppriv.channel);
	/* MPT_Initializeadapter(padapter, Channel); */
#else /*  temply marked this for RF */
#ifdef CONFIG_DUALMAC_CONCURRENT
	if (!pHalData->bSlaveOfDMSP)
#endif
	{

		/*  do IQK for 2.4G for better scan result, if current bandtype is 2.4G. */
		if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
			rtl8192d_PHY_IQCalibrate(padapter);

		rtl8192d_dm_CheckTXPowerTracking(padapter);

		rtl8192d_PHY_LCCalibrate(padapter);
		if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY
		&& ((pHalData->interfaceIndex == 0 && pHalData->BandSet92D == BAND_ON_2_4G)
			|| (pHalData->interfaceIndex == 1 && pHalData->BandSet92D == BAND_ON_5G)))
			RELEASE_GLOBAL_MUTEX(GlobalMutexForMac0_2G_Mac1_5G);
		/* 5G and 2.4G must wait sometime to let RF LO ready */
		/* by sherry 2010.06.28 */
#if SWLCK == 0
		{
			u32 tmpRega, tmpRegb;
			for (j = 0;j<10000;j++)
			{
				rtw_udelay_os(MAX_STALL_TIME);
				if (pHalData->rf_type == RF_1T1R)
				{
					tmpRega = PHY_QueryRFReg(padapter, (enum RF_RADIO_PATH_E)RF_PATH_A, 0x2a, bMaskDWord);
					if ((tmpRega&BIT11) == BIT11)
						break;
				}
				else
				{
					tmpRega = PHY_QueryRFReg(padapter, (enum RF_RADIO_PATH_E)RF_PATH_A, 0x2a, bMaskDWord);
					tmpRegb = PHY_QueryRFReg(padapter, (enum RF_RADIO_PATH_E)RF_PATH_B, 0x2a, bMaskDWord);
					if (((tmpRega&BIT11) == BIT11)&&((tmpRegb&BIT11) == BIT11))
						break;
					/*  temply add for DMSP */
					if (pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY&&(pHalData->interfaceIndex!= 0))
						break;
				}
			}
		}
#endif
	}
#endif

	PHY_InitPABias92D(padapter);

	rtl8192d_InitHalDm(padapter);


	rtw_write16(padapter, REG_BCN_CTRL, 0x1818);	/*  For 2 PORT TSF SYNC */

	{
	       u32					ulRegRead;
		/* 3  */
		/* 3 Set Loopback mode or Normal mode */
		/* 3 */
		/* 2006.12.13 by emily. Note!We should not merge these two CPU_GEN register writings */
		/*	because setting of System_Reset bit reset MAC to default transmission mode. */
		ulRegRead = rtw_read32(padapter, 0x100);	/* CPU_GEN  0x100 */

		ulRegRead |= ulRegRead;

		rtw_write32(padapter, 0x100, ulRegRead);

		/*  2006.11.29. After reset cpu, we sholud wait for a second, otherwise, it may fail to write registers. Emily */
		rtw_udelay_os(500);
	}

	RT_CLEAR_PS_LEVEL(pwrctrlpriv, RT_RF_OFF_LEVL_HALT_NIC);

	if ((pregistrypriv->lowrate_two_xmit) &&
	    (pHalData->MacPhyMode92D != DUALMAC_DUALPHY)) {
		/* for Use 2 path Tx to transmit MCS0~7 and legacy mode */
		/* Reg90C[30]= 1'b0 (OFDM TX by Reg, default PHY parameter) */
		/* Reg80C[31]= 1'b0 (CCK TX by Reg, default PHYparameter) */
		/* RegC8C = 0xa0e40000 (OFDM RX weighting) */
		rtw_write32(padapter, 0x90C, rtw_read32(padapter, 0x90C)&(~BIT(30)));
		rtw_write32(padapter, 0x80C, rtw_read32(padapter, 0x80C)&(~BIT(31)));
		rtw_write32(padapter, 0xC8C, 0xa0e40000);
	}

	/* ack for xmit mgmt frames. */
	rtw_write32(padapter, REG_FWHW_TXQ_CTRL, rtw_read32(padapter, REG_FWHW_TXQ_CTRL)|BIT(12));
exit:
	padapter->init_adpt_in_progress = false;

	DBG_8192D("%s in %dms\n", __func__, rtw_get_passing_time_ms(init_start_time));

#ifdef CONFIG_WOWLAN
	if (padapter->pwrctrlpriv.wowlan_mode == true)
		dump_wakup_reason(padapter);
	{
		u16 GPIO_val;
		GPIO_val = rtw_read16(padapter, REG_GPIO_PIN_CTRL+1);
		GPIO_val |= BIT(0)|BIT(8);
		/* set GPIO 0 to high for Toshiba */
		rtw_write16(padapter, REG_GPIO_PIN_CTRL+1, GPIO_val);
	}
	/* prevent 8051 to be reset by PERST# wake on wlan by Alex & Baron */
	/* rtw_write8(padapter, REG_RSV_CTRL, 0x20); */
	/* rtw_write8(padapter, REG_RSV_CTRL, 0x60); */
#endif /*  CONFIG_WOWLAN */
	return status;
}

static void
_DisableGPIO(
	struct rtw_adapter *	adapter
	)
{
/***************************************
j. GPIO_PIN_CTRL 0x44[31:0]= 0x000
k. Value = GPIO_PIN_CTRL[7:0]
l.  GPIO_PIN_CTRL 0x44[31:0] = 0x00FF0000 | (value <<8);  write external PIN level
m. GPIO_MUXCFG 0x42 [15:0] = 0x0780
n. LEDCFG 0x4C[15:0] = 0x8080
***************************************/
	u8	value8;
	u16	value16;
	u32	value32;

	/* 1. Disable GPIO[7:0] */
	rtw_write16(adapter, REG_GPIO_PIN_CTRL+2, 0x0000);
	value32 = rtw_read32(adapter, REG_GPIO_PIN_CTRL) & 0xFFFF00FF;
	value8 = (u8) (value32&0x000000FF);
	value32 |= ((value8<<8) | 0x00FF0000);
	rtw_write32(adapter, REG_GPIO_PIN_CTRL, value32);

	/* 2. Disable GPIO[10:8] */
	rtw_write8(adapter, REG_MAC_PINMUX_CFG, 0x00);
	value16 = rtw_read16(adapter, REG_GPIO_IO_SEL) & 0xFF0F;
	value8 = (u8) (value16&0x000F);
	value16 |= ((value8<<4) | 0x0780);
	rtw_write16(adapter, REG_GPIO_IO_SEL, value16);

	/* 3. Disable LED0 & 1 */
	rtw_write16(adapter, REG_LEDCFG0, 0x8888);

	/* RT_TRACE(COMP_INIT, DBG_LOUD, ("======> Disable GPIO and LED.\n")); */
} /* end of _DisableGPIO() */
static void
_ResetFWDownloadRegister(
	struct rtw_adapter *			adapter
	)
{
	u32	value32;

	value32 = rtw_read32(adapter, REG_MCUFWDL);
	value32 &= ~(MCUFWDL_EN | MCUFWDL_RDY);
	rtw_write32(adapter, REG_MCUFWDL, value32);
}

static int
_DisableRF_AFE(
	struct rtw_adapter *			adapter
	)
{
	int		rtStatus = _SUCCESS;
	u32			pollingCount = 0;
	u8			value8;

	/* disable RF/ AFE AD/DA */
	value8 = APSDOFF;
	rtw_write8(adapter, REG_APSD_CTRL, value8);

#if (RTL8192CU_ASIC_VERIFICATION)

	do
	{
		if (rtw_read8(adapter, REG_APSD_CTRL) & APSDOFF_STATUS) {
			/* RT_TRACE(COMP_INIT, DBG_LOUD, ("Disable RF, AFE, AD, DA Done!\n")); */
			break;
		}

		if (pollingCount++ > POLLING_READY_TIMEOUT_COUNT) {
			/* RT_TRACE(COMP_INIT, DBG_SERIOUS, ("Failed to polling APSDOFF_STATUS done!\n")); */
			return _FAIL;
		}

	}while (true);

#endif

	/* RT_TRACE(COMP_INIT, DBG_LOUD, ("Disable RF, AFE, AD, DA.\n")); */
	return rtStatus;
}

static void
_ResetBB(
	struct rtw_adapter *			adapter
	)
{
	u16	value16;
	/* before BB reset should do clock gated */
	rtw_write32(adapter, rFPGA0_XCD_RFParameter, rtw_read32(adapter, rFPGA0_XCD_RFParameter)|(BIT31));
	/* reset BB */
	value16 = rtw_read16(adapter, REG_SYS_FUNC_EN);
	value16 &= ~(FEN_BBRSTB | FEN_BB_GLB_RSTn);
	rtw_write16(adapter, REG_SYS_FUNC_EN, value16);
	/* RT_TRACE(COMP_INIT, DBG_LOUD, ("Reset BB.\n")); */
}

static void
_ResetMCU(
	struct rtw_adapter *			adapter
	)
{
	u16	value16;

	/*  reset MCU */
	value16 = rtw_read16(adapter, REG_SYS_FUNC_EN);
	value16 &= ~FEN_CPUEN;
	rtw_write16(adapter, REG_SYS_FUNC_EN, value16);
	/* RT_TRACE(COMP_INIT, DBG_LOUD, ("Reset MCU.\n")); */
}

static void
_DisableMAC_AFE_PLL(
	struct rtw_adapter *			adapter
	)
{
	u32	value32;

	/* disable MAC/ AFE PLL */
	value32 = rtw_read32(adapter, REG_APS_FSMCO);
	value32 |= APDM_MAC;
	rtw_write32(adapter, REG_APS_FSMCO, value32);

	value32 |= APFM_OFF;
	rtw_write32(adapter, REG_APS_FSMCO, value32);
	/* RT_TRACE(COMP_INIT, DBG_LOUD, ("Disable MAC, AFE PLL.\n")); */
}

static void
_AutoPowerDownToHostOff(
	struct rtw_adapter *		adapter
	)
{
	u32			value32;
	rtw_write8(adapter, REG_SPS0_CTRL, 0x22);

	value32 = rtw_read32(adapter, REG_APS_FSMCO);

	value32 |= APDM_HOST;/* card disable */
	rtw_write32(adapter, REG_APS_FSMCO, value32);
	/* RT_TRACE(COMP_INIT, DBG_LOUD, ("Auto Power Down to Host-off state.\n")); */

	/*  set USB suspend */
	value32 = rtw_read32(adapter, REG_APS_FSMCO);
	value32 &= ~AFSM_PCIE;
	rtw_write32(adapter, REG_APS_FSMCO, value32);
}

static void
_SetUsbSuspend(
	struct rtw_adapter *			adapter
	)
{
	u32			value32;

	value32 = rtw_read32(adapter, REG_APS_FSMCO);

	/*  set USB suspend */
	value32 |= AFSM_HSUS;
	rtw_write32(adapter, REG_APS_FSMCO, value32);

	/* RT_ASSERT(0 == (rtw_read32(adapter, REG_APS_FSMCO) & BIT(12)), ("")); */
	/* RT_TRACE(COMP_INIT, DBG_LOUD, ("Set USB suspend.\n")); */
}

static void
_DisableRFAFEAndResetBB8192D(
	struct rtw_adapter *			adapter
	)
{
/**************************************
a.	TXPAUSE 0x522[7:0] = 0xFF             Pause MAC TX queue
b.	RF path 0 offset 0x00 = 0x00            disable RF
c.	APSD_CTRL 0x600[7:0] = 0x40
d.	SYS_FUNC_EN 0x02[7:0] = 0x16		reset BB state machine
e.	SYS_FUNC_EN 0x02[7:0] = 0x14		reset BB state machine
***************************************/
       struct hal_data_8192du	*pHalData = GET_HAL_DATA(adapter);
	u8	eRFPath = 0, value8 = 0;

	PHY_SetBBReg(adapter, rFPGA0_AnalogParameter4, 0x00f00000,  0xf);
	PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)eRFPath, 0x0, bRFRegOffsetMask, 0x0);

	value8 |= APSDOFF;
	rtw_write8(adapter, REG_APSD_CTRL, value8);/* 0x40 */

	/* testchip  should not do BB reset if another mac is alive; */
	value8 = 0 ;
	value8 |= (FEN_USBD | FEN_USBA | FEN_BB_GLB_RSTn);
	rtw_write8(adapter, REG_SYS_FUNC_EN, value8);/* 0x16 */

	if (pHalData->MacPhyMode92D!= SINGLEMAC_SINGLEPHY)
	{
		if (pHalData->interfaceIndex!= 0) {
			/* before BB reset should do clock gated */
			rtw_write32(adapter, rFPGA0_XCD_RFParameter, rtw_read32(adapter, rFPGA0_XCD_RFParameter)|(BIT31));
			value8 &= (~FEN_BB_GLB_RSTn);
			rtw_write8(adapter, REG_SYS_FUNC_EN, value8); /* 0x14 */
		}
	}
	else {
		/* before BB reset should do clock gated */
		rtw_write32(adapter, rFPGA0_XCD_RFParameter, rtw_read32(adapter, rFPGA0_XCD_RFParameter)|(BIT31));
		value8 &= (~FEN_BB_GLB_RSTn);
		rtw_write8(adapter, REG_SYS_FUNC_EN, value8); /* 0x14 */
	}

	/* RT_TRACE(COMP_INIT, DBG_LOUD, ("======> RF off and reset BB.\n")); */
}

static void
_DisableRFAFEAndResetBB(
	struct rtw_adapter *			adapter
	)
{
	_DisableRFAFEAndResetBB8192D(adapter);
}

static void
_ResetDigitalProcedure1(
	struct rtw_adapter *			adapter,
	bool				bWithoutHWSM
	)
{

	struct hal_data_8192du  *pHalData = GET_HAL_DATA(adapter);

	u8 retry_cnts = 0;
	/*  2010/08/12 MH For USB SS, we can not stop 8051 when we are trying to */
	/*  enter IPS/HW&SW radio off. For S3/S4/S5/Disable, we can stop 8051 because */
	/*  we will init FW when power on again. */
	if (rtw_read8(adapter, REG_MCUFWDL) & BIT1) { /* IF fw in RAM code, do reset */

		if (adapter->bFWReady) {
			rtw_write8(adapter, REG_FSIMR, 0x00);
			/*  2010/08/25 MH Accordign to RD alfred's suggestion, we need to disable other */
			/*  HRCV INT to influence 8051 reset. */
			rtw_write8(adapter, REG_FWIMR, 0x20);
			/*  2011/02/15 MH According to Alex's suggestion, close mask to prevent incorrect FW write operation. */
			rtw_write8(adapter, REG_FTIMR, 0x00);

			rtw_write8(adapter, REG_MCUFWDL, 0);
			rtw_write8(adapter, REG_HMETFR+3, 0x20);/* 8051 reset by self */

			while ((retry_cnts++ <100) && (FEN_CPUEN &rtw_read16(adapter, REG_SYS_FUNC_EN)))
				rtw_udelay_os(50);/* us */

			if (retry_cnts>= 100) {
				rtw_write8(adapter, REG_FWIMR, 0x00);
				/*  2010/08/31 MH According to Filen's info, if 8051 reset fail, reset MAC directly. */
				rtw_write8(adapter, REG_SYS_FUNC_EN+1, 0x50);	/* Reset MAC and Enable 8051 */
				rtw_mdelay_os(10);
			} else {
				DBG_8192D("=====> 8051 reset success (%d) .\n", retry_cnts);
			}
		}
	} else {
		DBG_8192D("=====> 8051 in ROM.\n");
	}

	#ifdef DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE
	{
		u8 val;
		if ((val = rtw_read8(adapter, REG_MCUFWDL)))
			DBG_8192D("DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE %s:%d REG_MCUFWDL:0x%02x\n", __func__, __LINE__, val);
	}
	#endif

	rtw_write8(adapter, REG_SYS_FUNC_EN+1, 0x54);	/* Reset MAC and Enable 8051 */
	rtw_write8(adapter, REG_MCUFWDL, 0);

	if (bWithoutHWSM) {
	/*****************************
		Without HW auto state machine
	g.	SYS_CLKR 0x08[15:0] = 0x30A3			disable MAC clock
	h.	AFE_PLL_CTRL 0x28[7:0] = 0x80			disable AFE PLL
	i.	AFE_XTAL_CTRL 0x24[15:0] = 0x880F		gated AFE DIG_CLOCK
	j.	SYS_ISO_CTRL 0x00[7:0] = 0xF9			isolated digital to PON
	******************************/
		/* rtw_write16(adapter, REG_SYS_CLKR, 0x30A3); */
		rtw_write16(adapter, REG_SYS_CLKR, 0x70A3);/* modify to 0x70A3 by Scott. */
		rtw_write8(adapter, REG_AFE_PLL_CTRL, 0x80);
		rtw_write16(adapter, REG_AFE_XTAL_CTRL, 0x880F);
		rtw_write8(adapter, REG_SYS_ISO_CTRL, 0xF9);
	}
	/* RT_TRACE(COMP_INIT, DBG_LOUD, ("======> Reset Digital.\n")); */
}

static void
_ResetDigitalProcedure2(
	struct rtw_adapter *			adapter
)
{
/*****************************
k.	SYS_FUNC_EN 0x03[7:0] = 0x44			disable ELDR runction
l.	SYS_CLKR 0x08[15:0] = 0x3083			disable ELDR clock
m.	SYS_ISO_CTRL 0x01[7:0] = 0x83			isolated ELDR to PON
******************************/
	rtw_write16(adapter, REG_SYS_CLKR, 0x70a3); /* modify to 0x70a3 by Scott. */
	rtw_write8(adapter, REG_SYS_ISO_CTRL+1, 0x82); /* modify to 0x82 by Scott. */
}

static void
_DisableAnalog(
	struct rtw_adapter *			adapter,
	bool			bWithoutHWSM
	)
{
	u32 value16 = 0;
	u8 value8 = 0;

	if (bWithoutHWSM) {
	/*****************************
	n.	LDOA15_CTRL 0x20[7:0] = 0x04		disable A15 power
	o.	LDOV12D_CTRL 0x21[7:0] = 0x54		disable digital core power
	r.	When driver call disable, the ASIC will turn off remaining clock automatically
	******************************/

		rtw_write8(adapter, REG_LDOA15_CTRL, 0x04);
		/* PlatformIOWrite1Byte(adapter, REG_LDOV12D_CTRL, 0x54); */

		value8 = rtw_read8(adapter, REG_LDOV12D_CTRL);
		value8 &= (~LDV12_EN);
		rtw_write8(adapter, REG_LDOV12D_CTRL, value8);
		/* RT_TRACE(COMP_INIT, DBG_LOUD, (" REG_LDOV12D_CTRL Reg0x21:0x%02x.\n", value8)); */
	}

/*****************************
h.	SPS0_CTRL 0x11[7:0] = 0x23		enter PFM mode
i.	APS_FSMCO 0x04[15:0] = 0x4802		set USB suspend
******************************/
	rtw_write8(adapter, REG_SPS0_CTRL, 0x23);

	value16 |= (APDM_HOST | AFSM_HSUS |PFM_ALDN);
	rtw_write16(adapter, REG_APS_FSMCO, value16);/* 0x4802 */

	rtw_write8(adapter, REG_RSV_CTRL, 0x0e);

	/* RT_TRACE(COMP_INIT, DBG_LOUD, ("======> Disable Analog Reg0x04:0x%04x.\n", value16)); */
}

static bool
CanGotoPowerOff92D(
	struct rtw_adapter *			adapter
	)
{
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(adapter);
	u8 u1bTmp;
#ifdef CONFIG_DUALMAC_CONCURRENT
	struct rtw_adapter *	Buddyadapter = adapter->pbuddy_adapter;
#endif

	if (pHalData->MacPhyMode92D == SINGLEMAC_SINGLEPHY)
		return true;

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (Buddyadapter != NULL)
	{
		if (Buddyadapter->init_adpt_in_progress)
		{
			DBG_8192D("do not power off during another adapter is initialization\n");
			return false;
		}
	}
#endif

	if (pHalData->interfaceIndex == 0)
	{	/*  query another mac status; */
		u1bTmp = rtw_read8(adapter, REG_MAC1);
		u1bTmp&= MAC1_ON;
	}
	else
	{
		u1bTmp = rtw_read8(adapter, REG_MAC0);
		u1bTmp&= MAC0_ON;
	}

	/* 0x17[7]:1b' power off in process */
	u1bTmp = rtw_read8(adapter, 0x17);
	u1bTmp|= BIT7;
	rtw_write8(adapter, 0x17, u1bTmp);

	rtw_udelay_os(500);
	/*  query another mac status; */
	if (pHalData->interfaceIndex == 0)
	{	/*  query another mac status; */
		u1bTmp = rtw_read8(adapter, REG_MAC1);
		u1bTmp&= MAC1_ON;
	}
	else
	{
		u1bTmp = rtw_read8(adapter, REG_MAC0);
		u1bTmp&= MAC0_ON;
	}
	/* if another mac is alive, do not do power off */
	if (u1bTmp)
	{
		u1bTmp = rtw_read8(adapter, 0x17);
		u1bTmp&= (~BIT7);
		rtw_write8(adapter, 0x17, u1bTmp);
		return false;
	}
	return true;
}

static int
CardDisableHWSM(/*  HW Auto state machine */
	struct rtw_adapter *		adapter,
	bool			resetMCU
	)
{
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(adapter);
	int		rtStatus = _SUCCESS;
	u8		value;

	if (adapter->bSurpriseRemoved) {
		return rtStatus;
	}

	rtw_write8(adapter, REG_TXPAUSE, 0xFF);
	rtw_udelay_os(500);
	rtw_write8(adapter,	REG_CR, 0x0);

	/*  RF Off Sequence ==== */
#ifdef CONFIG_DUALMAC_CONCURRENT
	if (!pHalData->bSlaveOfDMSP || adapter->DualMacConcurrent == false)
#endif
		_DisableRFAFEAndResetBB(adapter);

	if (!PHY_CheckPowerOffFor8192D(adapter))
		return rtStatus;

	/* 0x20:value 05-->04 */
	rtw_write8(adapter, REG_LDOA15_CTRL, 0x04);
	/* RF Control */
	rtw_write8(adapter, REG_RF_CTRL, 0);

	/*   ==== Reset digital sequence   ====== */
	_ResetDigitalProcedure1(adapter, false);

	/*   ==== Pull GPIO PIN to balance level and LED control ====== */
	_DisableGPIO(adapter);

	/*   ==== Disable analog sequence === */
	_DisableAnalog(adapter, false);

	ACQUIRE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);
	value = rtw_read8(adapter, REG_POWER_OFF_IN_PROCESS);
	value&= (~BIT7);
	rtw_write8(adapter, REG_POWER_OFF_IN_PROCESS, value);
	RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("======> Card disable finished.\n"));

	return rtStatus;
}

static int
CardDisableWithoutHWSM(/*  without HW Auto state machine */
	struct rtw_adapter *		adapter
	)
{
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(adapter);
	int		rtStatus = _SUCCESS;
	u8		value;

	if (adapter->bSurpriseRemoved) {
		return rtStatus;
	}

	rtw_write8(adapter, REG_TXPAUSE, 0xFF);
	rtw_udelay_os(500);
	rtw_write8(adapter,	REG_CR, 0x0);

	/*  RF Off Sequence ==== */
#ifdef CONFIG_DUALMAC_CONCURRENT
	if (!pHalData->bSlaveOfDMSP || adapter->DualMacConcurrent == false)
#endif
		_DisableRFAFEAndResetBB(adapter);

	/*  stop tx/rx */
	rtw_write8(adapter, REG_TXPAUSE, 0xFF);
	rtw_udelay_os(500);
	rtw_write8(adapter,	REG_CR, 0x0);

	if (!PHY_CheckPowerOffFor8192D(adapter))
	{
		return rtStatus;
	}

	/* 0x20:value 05-->04 */
	rtw_write8(adapter, REG_LDOA15_CTRL, 0x04);
	/* RF Control */
	rtw_write8(adapter, REG_RF_CTRL, 0);

	/*   ==== Reset digital sequence   ====== */
#ifdef CONFIG_DUALMAC_CONCURRENT
	_ResetDigitalProcedure1(adapter, false);
#else
	_ResetDigitalProcedure1(adapter, true);
#endif

	/*   ==== Pull GPIO PIN to balance level and LED control ====== */
	_DisableGPIO(adapter);

	/*   ==== Reset digital sequence   ====== */
	_ResetDigitalProcedure2(adapter);

	/*   ==== Disable analog sequence === */
#ifdef CONFIG_DUALMAC_CONCURRENT
	_DisableAnalog(adapter, false);
#else
	_DisableAnalog(adapter, true);
#endif

	ACQUIRE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);
	value = rtw_read8(adapter, REG_POWER_OFF_IN_PROCESS);
	value&= (~BIT7);
	rtw_write8(adapter, REG_POWER_OFF_IN_PROCESS, value);
	RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerOnAndPowerOff);

	/* RT_TRACE(COMP_INIT, DBG_LOUD, ("<====== Card Disable Without HWSM .\n")); */
	return rtStatus;
}

static u32 rtl8192du_hal_deinit(struct rtw_adapter *padapter)
 {
	u8	u1bTmp;
	u8	OpMode;
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv	*pwrpriv = &padapter->pwrctrlpriv;

	if (RT_IN_PS_LEVEL(pwrpriv, RT_RF_OFF_LEVL_HALT_NIC))
	{
		DBG_8192D("Haltadapter8192DUsb(): Not to haltadapter if HW already halt\n");
		return _FAIL;
	}

	padapter->bHaltInProgress = true;

	OpMode = 0;
	rtw_hal_set_hwreg(padapter, HW_VAR_MEDIA_STATUS, (u8 *)(&OpMode));

	rtw_write16(padapter, REG_GPIO_MUXCFG, rtw_read16(padapter, REG_GPIO_MUXCFG)&(~BIT12));

	if (/*adapter->bInUsbIfTest ||*/ !pHalData->bSupportRemoteWakeUp) {
		if (padapter->bCardDisableWOHSM == false)
			CardDisableHWSM(padapter, false);
		else
			CardDisableWithoutHWSM(padapter);
	} else {

		/*  Wake on WLAN */
	}

	if (pHalData->bInSetPower)
	{
		/* 0xFE10[4] clear before suspend	 suggested by zhouzhou */
		u1bTmp = rtw_read8(padapter, 0xfe10);
		u1bTmp&= (~BIT4);
		rtw_write8(padapter, 0xfe10, u1bTmp);
	}

	RT_SET_PS_LEVEL(pwrpriv, RT_RF_OFF_LEVL_HALT_NIC);

	rtw_led_control(padapter, LED_CTL_POWER_OFF);

	padapter->bHaltInProgress = false;

	return _SUCCESS;
 }

static unsigned int rtl8192du_inirp_init(struct rtw_adapter *padapter)
{
	u8 i;
	struct recv_buf *precvbuf;
	uint	status;
	struct intf_hdl *pintfhdl =&padapter->iopriv.intf;
	struct recv_priv *precvpriv = &(padapter->recvpriv);
	u32 (*_read_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);

	_read_port = pintfhdl->io_ops._read_port;

	status = _SUCCESS;

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("===> usb_inirp_init\n"));

	precvpriv->ff_hwaddr = RECV_BULK_IN_ADDR;

	/* issue Rx irp to receive data */
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;
	for (i = 0; i<NR_RECVBUFF; i++)
	{
		if (_read_port(pintfhdl, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf) == false)
		{
			RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("usb_rx_init: usb_read_port error\n"));
			status = _FAIL;
			goto exit;
		}

		precvbuf++;
		precvpriv->free_recv_buf_queue_cnt--;
	}

exit:

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("<=== usb_inirp_init\n"));
	return status;
}

static unsigned int rtl8192du_inirp_deinit(struct rtw_adapter *padapter)
{
	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("\n ===> usb_rx_deinit\n"));

	rtw_read_port_cancel(padapter);

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("\n <=== usb_rx_deinit\n"));

	return _SUCCESS;
}

/*  */
/*  */
/*	EEPROM/EFUSE Content Parsing */
/*  */
/*  */

static void
_ReadPROMVersion(
	struct rtw_adapter *	adapter,
	u8*	PROMContent,
	bool		AutoloadFail
	)
{
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(adapter);

	if (AutoloadFail) {
		pHalData->EEPROMVersion = EEPROM_Default_Version;
	}
	else {
		pHalData->EEPROMVersion = *(u8 *)&PROMContent[EEPROM_VERSION];
	}
}

static u32 _GetChannelGroup(u32 channel)
{

	if (channel < 3) {	/*  Channel 1~3 */
		return 0;
	}
	else if (channel < 9) { /*  Channel 4~9 */
		return 1;
	}

	return 2;				/*  Channel 10~14 */
}

static void
_ReadIDs(
	struct rtw_adapter *	adapter,
	u8*		PROMContent,
	bool		AutoloadFail
	)
{
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(adapter);

	if (false == AutoloadFail) {
		/*  VID, PID */
		pHalData->EEPROMVID = le16_to_cpu(*(__le16 *)&PROMContent[EEPROM_VID]);
		pHalData->EEPROMPID = le16_to_cpu(*(__le16 *)&PROMContent[EEPROM_PID]);

		/*  Customer ID, 0x00 and 0xff are reserved for Realtek. */
		pHalData->EEPROMCustomerID = *(u8 *)&PROMContent[EEPROM_CUSTOMER_ID];
		pHalData->EEPROMSubCustomerID = *(u8 *)&PROMContent[EEPROM_SUBCUSTOMER_ID];

	}
	else {
		pHalData->EEPROMVID	 = EEPROM_Default_VID;
		pHalData->EEPROMPID	 = EEPROM_Default_PID;

		/*  Customer ID, 0x00 and 0xff are reserved for Realtek. */
		pHalData->EEPROMCustomerID	= EEPROM_Default_CustomerID;
		pHalData->EEPROMSubCustomerID = EEPROM_Default_SubCustomerID;

	}

	/*	Decide CustomerID according to VID/DID or EEPROM */
	switch (pHalData->EEPROMCustomerID) {
	case EEPROM_CID_WHQL:
		break;
	default:
		pHalData->CustomerID = RT_CID_DEFAULT;
		break;
	}

	DBG_8192D("EEPROMVID = 0x%04x\n", pHalData->EEPROMVID);
	DBG_8192D("EEPROMPID = 0x%04x\n", pHalData->EEPROMPID);
	DBG_8192D("EEPROMCustomerID : 0x%02x\n", pHalData->EEPROMCustomerID);
	DBG_8192D("EEPROMSubCustomerID: 0x%02x\n", pHalData->EEPROMSubCustomerID);
}

static void
_ReadMACAddress(
	struct rtw_adapter *	adapter,
	u8*		PROMContent,
	bool		AutoloadFail
	)
{
	struct hal_data_8192du		*pHalData = GET_HAL_DATA(adapter);
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(adapter);

	/*  Dual MAC should assign diffrent MAC address , or, it is wil cause hang in single phy mode  zhiyuan 04/07/2010 */
	/* Temply random assigh mac address for  efuse mac address not ready now */
	if (AutoloadFail == false ) {
		if (pHalData->interfaceIndex == 0) {
			/* change to use memcpy, in order to avoid alignment issue. Baron 2011/6/20 */
			memcpy(&pEEPROM->mac_addr, &PROMContent[EEPROM_MAC_ADDR_MAC0_92D], ETH_ALEN);
		}
		else {
			/* change to use memcpy, in order to avoid alignment issue. Baron 2011/6/20 */
			memcpy(&pEEPROM->mac_addr, &PROMContent[EEPROM_MAC_ADDR_MAC1_92D], ETH_ALEN);
		}

		if (is_broadcast_mac_addr(pEEPROM->mac_addr) || is_multicast_mac_addr(pEEPROM->mac_addr))
		{
			/* Random assigh MAC address */
			u8 sMacAddr[MAC_ADDR_LEN] = {0x00, 0xE0, 0x4C, 0x81, 0x92, 0x00};
			if (pHalData->interfaceIndex == 1)
				sMacAddr[5] = 0x01;
			memcpy(pEEPROM->mac_addr, sMacAddr, ETH_ALEN);
		}
	}
	else
	{
		/* Random assigh MAC address */
		u8 sMacAddr[MAC_ADDR_LEN] = {0x00, 0xE0, 0x4C, 0x81, 0x92, 0x00};
		if (pHalData->interfaceIndex == 1) {
			sMacAddr[5] = 0x01;
		}
		memcpy(pEEPROM->mac_addr, sMacAddr, ETH_ALEN);
	}

	DBG_8192D("MAC Address from EFUSE = %pM\n", pEEPROM->mac_addr);
}

static void
hal_ReadMacPhyModeFromPROM92DU(
	struct rtw_adapter *	adapter,
	u8*		PROMContent
)
{
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(adapter);
	u8	MacPhyCrValue = 0;

	MacPhyCrValue = PROMContent[EEPROM_ENDPOINT_SETTING];
	if (MacPhyCrValue & BIT0)
	{
#ifdef CONFIG_DUALMAC_CONCURRENT
		if (adapter->registrypriv.mac_phy_mode == 3)
		{
			pHalData->MacPhyMode92D = DUALMAC_SINGLEPHY;
			adapter->DualMacConcurrent = true;
		}
		else
		{
			pHalData->MacPhyMode92D = DUALMAC_DUALPHY;
			adapter->DualMacConcurrent = false;
		}
#else
		pHalData->MacPhyMode92D = DUALMAC_DUALPHY;
		DBG_8192D("hal_ReadMacPhyModeFromPROM92DU:: MacPhyMode DUALMAC_DUALPHY\n");
#endif

		if (adapter->registrypriv.mac_phy_mode == 1)
			pHalData->MacPhyMode92D = SINGLEMAC_SINGLEPHY;
		else	 if (adapter->registrypriv.mac_phy_mode == 2)
			pHalData->MacPhyMode92D = DUALMAC_DUALPHY;
	}
	else
	{
		pHalData->MacPhyMode92D = SINGLEMAC_SINGLEPHY;
	}

	DBG_8192D("_ReadMacPhyModeFromPROM92DU(): MacPhyCrValue %d\n", MacPhyCrValue);
}

static void
hal_ReadMacPhyMode_92D(
	struct rtw_adapter *	adapter,
	u8			*PROMContent,
	bool		AutoloadFail
)
{
#ifdef CONFIG_DUALMAC_CONCURRENT
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(adapter);
#endif /* CONFIG_DUALMAC_CONCURRENT */
	u8	Mac1EnableValue = 0;

	if (AutoloadFail == true) {
		Mac1EnableValue = rtw_read8(adapter, 0xFE64);
		PHY_ReadMacPhyMode92D(adapter, AutoloadFail);

		DBG_8192D("_ReadMacPhyMode(): AutoloadFail %d 0xFE64 = 0x%x\n", AutoloadFail, Mac1EnableValue);
	}
	else {
		hal_ReadMacPhyModeFromPROM92DU(adapter, PROMContent);
	}

#ifdef CONFIG_DUALMAC_CONCURRENT
/* SMSP-->DMDP/DMSP wait for another adapter compeletes mode switc */

/* get Dual Mac Mode from 0x2C for test chip and 0xF8 for normal chip */
	ACQUIRE_GLOBAL_MUTEX(GlobalCounterForMutex);
	if (GlobalFirstConfigurationForNormalChip)
	{
		RELEASE_GLOBAL_MUTEX(GlobalCounterForMutex);
		PHY_ConfigMacPhyMode92D(adapter);
		ACQUIRE_GLOBAL_MUTEX(GlobalCounterForMutex);
		GlobalFirstConfigurationForNormalChip = false;
		RELEASE_GLOBAL_MUTEX(GlobalCounterForMutex);
	}
	else
	{
		RELEASE_GLOBAL_MUTEX(GlobalCounterForMutex);
		PHY_ReadMacPhyMode92D(adapter, AutoloadFail);
	}
#else
	PHY_ConfigMacPhyMode92D(adapter);
#endif

	PHY_ConfigMacPhyModeInfo92D(adapter);
	rtl8192d_ResetDualMacSwitchVariables(adapter);
}

static void
_ReadBoardType(
	struct rtw_adapter *	adapter,
	u8*		PROMContent,
	bool		AutoloadFail
	)
{
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(adapter);
	u8			boardType;

	if (AutoloadFail) {
		pHalData->rf_type = RF_2T2R;
		pHalData->BluetoothCoexist = false;
		return;
	}

	boardType = PROMContent[EEPROM_NORMAL_BoardType];
	boardType &= BOARD_TYPE_NORMAL_MASK;
	boardType >>= 5;
}

static void
_ReadLEDSetting(
	struct rtw_adapter *	adapter,
	u8*		PROMContent,
	bool		AutoloadFail
	)
{
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(adapter);
	struct led_priv		*pledpriv = &(adapter->ledpriv);

	/*  Led mode */
	switch (pHalData->CustomerID)
	{
		case RT_CID_DEFAULT:
			pledpriv->LedStrategy = SW_LED_MODE1;
			pledpriv->bRegUseLed = true;
			break;
		default:
			pledpriv->LedStrategy = SW_LED_MODE0;
			break;
	}

	#ifdef CONFIG_FORCE_HW_LED
	pledpriv->LedStrategy = HW_LED;
	#endif
}

#ifdef CONFIG_WOWLAN
static void
_ReadWOWLAN(
	struct rtw_adapter *	adapter,
	u8*		PROMContent,
	bool		AutoloadFail
	)
{
	if (AutoloadFail)
		adapter->pwrctrlpriv.bSupportRemoteWakeup = false;
	else
	{
		/*  decide hw if support remote wakeup function */
		/*  if hw supported, 8051 (SIE) will generate WeakUP signal(D+/D- toggle) when autoresume */
		adapter->pwrctrlpriv.bSupportRemoteWakeup = (PROMContent[EEPROM_Option_Setting] & BIT1)?true :false;
		DBG_8192D("efuse remote wakeup =%d\n", adapter->pwrctrlpriv.bSupportRemoteWakeup);
	}
}
#endif /* CONFIG_WOWLAN */

static void _InitadapterVariablesByPROM(
	struct rtw_adapter *	adapter,
	u8*		PROMContent,
	unsigned char AutoloadFail
	)
{
	_ReadPROMVersion(adapter, PROMContent, AutoloadFail);
	_ReadIDs(adapter, PROMContent, AutoloadFail);
	_ReadMACAddress(adapter, PROMContent, AutoloadFail);
	rtl8192d_ReadTxPowerInfo(adapter, PROMContent, AutoloadFail);
	hal_ReadMacPhyMode_92D(adapter, PROMContent, AutoloadFail);
	rtl8192d_EfuseParseChnlPlan(adapter, PROMContent, AutoloadFail);
	_ReadBoardType(adapter, PROMContent, AutoloadFail);
	_ReadLEDSetting(adapter, PROMContent, AutoloadFail);
#ifdef CONFIG_WOWLAN
	_ReadWOWLAN(adapter, PROMContent, AutoloadFail);
#endif /* CONFIG_WOWLAN */
}

static void _ReadPROMContent(
	struct rtw_adapter *		adapter
	)
{
	struct eeprom_priv *pEEPROM = GET_EEPROM_EFUSE_PRIV(adapter);
	u8			PROMContent[HWSET_MAX_SIZE]={0};
	u8			eeValue;
	u32			i;

	eeValue = rtw_read8(adapter, REG_9346CR);
	/*  To check system boot selection. */
	pEEPROM->EepromOrEfuse		= (eeValue & BOOT_FROM_EEPROM) ? true : false;
	pEEPROM->bautoload_fail_flag	= (eeValue & EEPROM_EN) ? false : true;

	DBG_8192D("Boot from %s, Autoload %s !\n", (pEEPROM->EepromOrEfuse ? "EEPROM" : "EFUSE"),
				(pEEPROM->bautoload_fail_flag ? "Fail" : "OK"));

	if (pEEPROM->bautoload_fail_flag == false) {
		if (pEEPROM->EepromOrEfuse == true) {
			/*  Read all Content from EEPROM or EFUSE. */
		} else {
			/*  Read EFUSE real map to shadow. */
			ACQUIRE_GLOBAL_MUTEX(GlobalMutexForPowerAndEfuse);
			EFUSE_ShadowMapUpdate(adapter, EFUSE_WIFI, false);
			RELEASE_GLOBAL_MUTEX(GlobalMutexForPowerAndEfuse);
			memcpy((void*)PROMContent, (void*)pEEPROM->efuse_eeprom_data, HWSET_MAX_SIZE);
		}

		/* Double check 0x8192 autoload status again */
		if (RTL8192_EEPROM_ID != le16_to_cpu(*((__le16 *)PROMContent)))
		{
			pEEPROM->bautoload_fail_flag = true;
			DBG_8192D("Autoload OK but EEPROM ID content is incorrect!!\n");
		}

	}
	else if (pEEPROM->EepromOrEfuse == false)/* auto load fail */
	{
		memset(pEEPROM->efuse_eeprom_data, 0xff, HWSET_MAX_SIZE);
		memcpy((void*)PROMContent, (void*)pEEPROM->efuse_eeprom_data, HWSET_MAX_SIZE);
	}

	_InitadapterVariablesByPROM(adapter, PROMContent, pEEPROM->bautoload_fail_flag);
}

static void
_InitOtherVariable(
	struct rtw_adapter *		adapter
	)
{
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(adapter);

	/*  2009/06/10 MH For 92S 1*1 = 1R/ 1*2&2*2 use 2R. We default set 1*1 use radio A */
	/*  So if you want to use radio B. Please modify RF path enable bit for correct signal */
	/*  strength calculate. */
	if (pHalData->rf_type == RF_1T1R) {
		pHalData->bRFPathRxEnable[0] = true;
	}
	else {
		pHalData->bRFPathRxEnable[0] = pHalData->bRFPathRxEnable[1] = true;
	}
}

static void
_ReadRFType(
	struct rtw_adapter *	adapter
	)
{
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(adapter);

#if DISABLE_BB_RF
	pHalData->rf_chip = RF_PSEUDO_11N;
#else
	pHalData->rf_chip = RF_6052;
#endif
}

static int _ReadadapterInfo8192DU(struct rtw_adapter *	adapter)
{
	u32 start = rtw_get_current_time();

	DBG_8192D("====> %s\n", __func__);

	_ReadRFType(adapter);
	_ReadPROMContent(adapter);

	_InitOtherVariable(adapter);

	/* For 92DU Phy and Mac mode set , will initialize by EFUSE/EPPROM     zhiyuan 2010/03/25 */
	DBG_8192D("<==== %s in %d ms\n", __func__, rtw_get_passing_time_ms(start));

	return _SUCCESS;
}

static void ReadadapterInfo8192DU(struct rtw_adapter *adapter)
{
	/*  Read EEPROM size before call any EEPROM function */
	adapter->EepromAddressSize = GetEEPROMSize8192D(adapter);

	_ReadadapterInfo8192DU(adapter);
}

#define GPIO_DEBUG_PORT_NUM 0
static void rtl8192du_trigger_gpio_0(struct rtw_adapter *padapter)
{

	u32 gpioctrl;
	DBG_8192D("==> trigger_gpio_0...\n");
	rtw_write16_async(padapter, REG_GPIO_PIN_CTRL, 0);
	rtw_write8_async(padapter, REG_GPIO_PIN_CTRL+2, 0xFF);
	gpioctrl = (BIT(GPIO_DEBUG_PORT_NUM)<<24)|(BIT(GPIO_DEBUG_PORT_NUM)<<16);
	rtw_write32_async(padapter, REG_GPIO_PIN_CTRL, gpioctrl);
	gpioctrl |= (BIT(GPIO_DEBUG_PORT_NUM)<<8);
	rtw_write32_async(padapter, REG_GPIO_PIN_CTRL, gpioctrl);
	DBG_8192D("<=== trigger_gpio_0...\n");
}

static void
StopTxBeacon(
	struct rtw_adapter *	adapter
	)
{
	struct hal_data_8192du *	pHalData = GET_HAL_DATA(adapter);

	DBG_8192D("StopTxBeacon\n");

	rtw_write8(adapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) & (~BIT6));
	pHalData->RegFwHwTxQCtrl &= (~BIT6);
	rtw_write8(adapter, REG_BCN_MAX_ERR, 0xff);
	rtw_write8(adapter, REG_TBTT_PROHIBIT+1, 0x64);
}

static void
ResumeTxBeacon(
	struct rtw_adapter *	adapter
	)
{
	struct hal_data_8192du *	pHalData = GET_HAL_DATA(adapter);

	DBG_8192D("ResumeTxBeacon\n");

	rtw_write8(adapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) | BIT6);
	pHalData->RegFwHwTxQCtrl |= BIT6;
	rtw_write8(adapter, REG_BCN_MAX_ERR, 0x0a);
	rtw_write8(adapter, REG_TBTT_PROHIBIT+1, 0x64);
}

/*  */
/*  2010.11.17. Added by tynli. */
/*  */
static u8 SelectRTSInitialRate(struct rtw_adapter *adapter)
{
	struct sta_info		*psta;
	struct mlme_priv		*pmlmepriv = &adapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex	*cur_network = &(pmlmeinfo->network);
	struct sta_priv		*pstapriv = &adapter->stapriv;
	u8	bUseProtection;
	u16	BasicRateCfg = 0;
	u8	SupportRateSet[NDIS_802_11_LENGTH_RATES_EX];
	u8	RTSRateIndex = 0; /*  1M */
	u8	LowestRateIdx = 0;

	psta = rtw_get_stainfo(pstapriv, cur_network->MacAddress);
	if (psta == NULL)
	{
		return RTSRateIndex;
	}

	if (psta->rtsen || psta->cts2self)
		bUseProtection = true;

	memcpy(SupportRateSet, cur_network->SupportedRates, NDIS_802_11_LENGTH_RATES_EX);

	halsetbratecfg(adapter, SupportRateSet, &BasicRateCfg);

	if (bUseProtection &&
		(!(pmlmeext->cur_wireless_mode == WIRELESS_11A|| pmlmeext->cur_wireless_mode == WIRELESS_11A_5N)))/*  5G not support cck rate */
	{
		/*  Use CCK rate */
		BasicRateCfg &= 0xf; /* CCK rate */
		while (BasicRateCfg > 0x1)
		{
			BasicRateCfg = (BasicRateCfg>> 1);
			RTSRateIndex++;
		}
	}
	else /* if (pMgntInfo->pHTInfo->CurrentOpMode) */
	{
		/* MacId 0: INFRA mode. */
		if ((check_fwstate(pmlmepriv, _FW_LINKED) == true)&&(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true))
		{
			LowestRateIdx = rtw_read8(adapter, REG_INIDATA_RATE_SEL)&0x3f;
		}

		/*  Adjust RTS Init rate when the data rate is MCS0~2, 8~10 which is lower than 24M. */
		if (LowestRateIdx == 12 || LowestRateIdx == 20) /* MCS0, MCS8 */
		{
			RTSRateIndex = 4; /*  6M */
		}
		else if (LowestRateIdx == 13 || LowestRateIdx == 14 ||
			LowestRateIdx == 21 || LowestRateIdx == 22) /* MCS1, MCS2, MCS9, MCS10 */
		{
			RTSRateIndex = 6; /*  12M */
		}
		else
		{
			if (BasicRateCfg != 0)
			{
				/*  Select RTS Init rate */
				while (BasicRateCfg > 0x1)
				{
					BasicRateCfg = (BasicRateCfg>> 1);
					RTSRateIndex++;
				}
			}
			else
			{
				RTSRateIndex = 4; /*  6M */
			}
		}

	}

	/* Set RTS init rate to Hw. */
	return RTSRateIndex;
}

/*  */
/*  Description: Selcet the RTS init rate and set the rate to HW. */
/*  2010.11.25. Created by tynli. */
/*  */
static void SetRTSRateWorkItemCallback(void *pContext)
{
	struct rtw_adapter *adapter =  (struct rtw_adapter *)pContext;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8	NewRTSInitRate = 0;

	NewRTSInitRate = SelectRTSInitialRate(adapter);
	if (NewRTSInitRate != pHalData->RTSInitRate)
	{
		rtw_write8(adapter, REG_INIRTS_RATE_SEL, NewRTSInitRate);
		pHalData->RTSInitRate = NewRTSInitRate;
	}

	DBG_8192D("HW_VAR_INIT_RTS_RATE: RateIndex(%d)\n", NewRTSInitRate);
}

static void hw_var_set_opmode(struct rtw_adapter *adapter, u8 variable, u8 *val)
{
	u8	val8;
	u8	mode = *((u8 *)val);
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

#ifdef CONFIG_CONCURRENT_MODE
	if (adapter->iface_type == IFACE_PORT1)
	{
		/*  disable Port1 TSF update */
		rtw_write8(adapter, REG_BCN_CTRL_1, rtw_read8(adapter, REG_BCN_CTRL_1)|BIT(4));

		/*  set net_type */
		val8 = rtw_read8(adapter, MSR)&0x03;
		val8 |= (mode<<2);
		rtw_write8(adapter, MSR, val8);

		/* reset TSF1 */
		rtw_write8(adapter, REG_DUAL_TSF_RST, BIT(1));

		DBG_8192D("%s()-%d mode = %d\n", __func__, __LINE__, mode);

		if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_))
		{
			if (!check_buddy_mlmeinfo_state(adapter, WIFI_FW_AP_STATE))
			{
				StopTxBeacon(adapter);
			}

			rtw_write8(adapter, REG_BCN_CTRL_1, 0x19);/* disable atim wnd */
		}
		else if ((mode == _HW_STATE_ADHOC_) /*|| (mode == _HW_STATE_AP_)*/)
		{
			ResumeTxBeacon(adapter);
			rtw_write8(adapter, REG_BCN_CTRL_1, 0x1a);
		}
		else if (mode == _HW_STATE_AP_)
		{
			ResumeTxBeacon(adapter);

			rtw_write8(adapter, REG_BCN_CTRL_1, 0x12);

			/* Set RCR */
			rtw_write32(adapter, REG_RCR, 0x7000228e);/* CBSSID_DATA must set to 0 */
			/* enable to rx data frame */
			rtw_write16(adapter, REG_RXFLTMAP2, 0xFFFF);
			/* enable to rx ps-poll */
			rtw_write16(adapter, REG_RXFLTMAP1, 0x0400);

			/* Beacon Control related register for first time */
			rtw_write8(adapter, REG_BCNDMATIM, 0x02); /*  2ms */
			rtw_write8(adapter, REG_DRVERLYINT, 0x05);/*  5ms */
			rtw_write8(adapter, REG_ATIMWND_1, 0x0a); /*  10ms for port1 */
			rtw_write16(adapter, REG_BCNTCFG, 0x00);
			rtw_write16(adapter, REG_TBTT_PROHIBIT, 0xff04);
			rtw_write16(adapter, REG_TSFTR_SYN_OFFSET, 0x7fff);/*  +32767 (~32ms) */

		       /* enable BCN1 Function for if2 */
			/* don't enable update TSF1 for if2 (due to TSF update when beacon/probe rsp are received) */
			rtw_write8(adapter, REG_BCN_CTRL_1, (DIS_TSF_UDT0_NORMAL_CHIP|EN_BCN_FUNCTION | EN_TXBCN_RPT|BIT(1)));

			DBG_8192D("%s()-%d: REG_BCN_CTRL_1 = %02x\n", __func__, __LINE__, rtw_read8(adapter, REG_BCN_CTRL_1));

			if (check_buddy_fwstate(adapter, WIFI_FW_NULL_STATE))
				rtw_write8(adapter, REG_BCN_CTRL,
					rtw_read8(adapter, REG_BCN_CTRL) & ~EN_BCN_FUNCTION);

			/* dis BCN0 ATIM  WND if if1 is station */
			rtw_write8(adapter, REG_BCN_CTRL, rtw_read8(adapter, REG_BCN_CTRL)|BIT(0));
#ifdef CONFIG_TSF_RESET_OFFLOAD
			/*  Reset TSF for STA+AP concurrent mode */
			if (check_buddy_fwstate(adapter, (WIFI_STATION_STATE|WIFI_ASOC_STATE))) {
				if (reset_tsf(adapter, IFACE_PORT1) == false)
					DBG_8192D("ERROR! %s()-%d: Reset port1 TSF fail\n",
						__func__, __LINE__);
			}
#endif	/*  CONFIG_TSF_RESET_OFFLOAD */
		}

	}
	else	/*  (adapter->iface_type == IFACE_PORT1) */
#endif /* CONFIG_CONCURRENT_MODE */
	{
		/*  disable Port0 TSF update */
		rtw_write8(adapter, REG_BCN_CTRL, rtw_read8(adapter, REG_BCN_CTRL)|BIT(4));

		/*  set net_type */
		val8 = rtw_read8(adapter, MSR)&0x0c;
		val8 |= mode;
		rtw_write8(adapter, MSR, val8);

		/* reset TSF0 */
		rtw_write8(adapter, REG_DUAL_TSF_RST, BIT(0));

		DBG_8192D("%s()-%d mode = %d\n", __func__, __LINE__, mode);

		if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_))
		{
#ifdef CONFIG_CONCURRENT_MODE
			if (!check_buddy_mlmeinfo_state(adapter, WIFI_FW_AP_STATE))
#endif /* CONFIG_CONCURRENT_MODE */
			{
				StopTxBeacon(adapter);
			}

			rtw_write8(adapter, REG_BCN_CTRL, 0x19);/* disable atim wnd */
		}
		else if ((mode == _HW_STATE_ADHOC_) /*|| (mode == _HW_STATE_AP_)*/)
		{
			ResumeTxBeacon(adapter);
			rtw_write8(adapter, REG_BCN_CTRL, 0x1a);
		}
		else if (mode == _HW_STATE_AP_)
		{
			ResumeTxBeacon(adapter);

			rtw_write8(adapter, REG_BCN_CTRL, 0x12);

			/* Set RCR */
			rtw_write32(adapter, REG_RCR, 0x7000228e);/* CBSSID_DATA must set to 0 */
			/* enable to rx data frame */
			rtw_write16(adapter, REG_RXFLTMAP2, 0xFFFF);
			/* enable to rx ps-poll */
			rtw_write16(adapter, REG_RXFLTMAP1, 0x0400);

			/* Beacon Control related register for first time */

			rtw_write8(adapter, REG_BCNDMATIM, 0x02); /*  2ms */
			rtw_write8(adapter, REG_DRVERLYINT, 0x05);/*  5ms */

			rtw_write8(adapter, REG_ATIMWND, 0x0a); /*  10ms for port0 */
			rtw_write16(adapter, REG_BCNTCFG, 0x00);
			rtw_write16(adapter, REG_TBTT_PROHIBIT, 0xff04);
			rtw_write16(adapter, REG_TSFTR_SYN_OFFSET, 0x7fff);/*  +32767 (~32ms) */

		        /* enable BCN0 Function for if1 */
			/* don't enable update TSF0 for if1 (due to TSF update when beacon/probe rsp are received) */
			rtw_write8(adapter, REG_BCN_CTRL, (DIS_TSF_UDT0_NORMAL_CHIP|EN_BCN_FUNCTION | EN_TXBCN_RPT|BIT(1)));

#ifdef CONFIG_CONCURRENT_MODE
			if (check_buddy_fwstate(adapter, WIFI_FW_NULL_STATE))
				rtw_write8(adapter, REG_BCN_CTRL_1,
					rtw_read8(adapter, REG_BCN_CTRL_1) & ~EN_BCN_FUNCTION);

			/* dis BCN1 ATIM  WND if if2 is station */
			rtw_write8(adapter, REG_BCN_CTRL_1, rtw_read8(adapter, REG_BCN_CTRL_1)|BIT(0));
#ifdef CONFIG_TSF_RESET_OFFLOAD
			/*  Reset TSF for STA+AP concurrent mode */
			if (check_buddy_fwstate(adapter, (WIFI_STATION_STATE|WIFI_ASOC_STATE))) {
				if (reset_tsf(adapter, IFACE_PORT0) == false)
					DBG_8192D("ERROR! %s()-%d: Reset port0 TSF fail\n",
						__func__, __LINE__);
			}
#endif /*  CONFIG_TSF_RESET_OFFLOAD */
#endif /*  CONFIG_CONCURRENT_MODE */
		}

	}
}

static void hw_var_set_macaddr(struct rtw_adapter *adapter, u8 variable, u8 *val)
{
	u8 idx = 0;
	u32 reg_macid;

#ifdef CONFIG_CONCURRENT_MODE
	if (adapter->iface_type == IFACE_PORT1) {
		reg_macid = REG_MACID1;
	} else
#endif
	{
		reg_macid = REG_MACID;
	}

	for (idx = 0 ; idx < 6; idx++)
		rtw_write8(adapter, (reg_macid+idx), val[idx]);
}

static void hw_var_set_bssid(struct rtw_adapter *adapter, u8 variable, u8 *val)
{
	u8	idx = 0;
	u32 reg_bssid;

#ifdef CONFIG_CONCURRENT_MODE
	if (adapter->iface_type == IFACE_PORT1)
	{
		reg_bssid = REG_BSSID1;
	}
	else
#endif
	{
		reg_bssid = REG_BSSID;
	}

	for (idx = 0 ; idx < 6; idx++)
	{
		rtw_write8(adapter, (reg_bssid+idx), val[idx]);
	}
}

static void hw_var_set_bcn_func(struct rtw_adapter *adapter, u8 variable,
				u8 *val)
{
	u32 bcn_ctrl_reg;

#ifdef CONFIG_CONCURRENT_MODE
	if (adapter->iface_type == IFACE_PORT1)
	{
		bcn_ctrl_reg = REG_BCN_CTRL_1;

		if (*((u8 *)val))
		{
			rtw_write8(adapter, bcn_ctrl_reg, (EN_BCN_FUNCTION | EN_TXBCN_RPT));
		}
		else
		{
			rtw_write8(adapter, bcn_ctrl_reg, rtw_read8(adapter, bcn_ctrl_reg)&(~(EN_BCN_FUNCTION | EN_TXBCN_RPT)));
		}
	}
	else
#endif
	{
		bcn_ctrl_reg = REG_BCN_CTRL;
		if (*((u8 *)val))
		{
			rtw_write8(adapter, bcn_ctrl_reg, (EN_BCN_FUNCTION | EN_TXBCN_RPT));
		}
		else
		{
			rtw_write8(adapter, bcn_ctrl_reg, (rtw_read8(adapter, bcn_ctrl_reg)&(~(EN_TXBCN_RPT))) | DIS_TSF_UDT0_NORMAL_CHIP);
		}
	}
}

static void hw_var_set_correct_tsf(struct rtw_adapter *adapter, u8 variable,
				   u8 *val)
{
#ifdef CONFIG_CONCURRENT_MODE
	u64	tsf;
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	tsf = pmlmeext->TSFValue - rtw_modular64(pmlmeext->TSFValue, (pmlmeinfo->bcn_interval*1024)) -1024; /* us */

	if (((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
	{
		StopTxBeacon(adapter);
	}

	if (adapter->iface_type == IFACE_PORT1)
	{
		/* disable related TSF function */
		rtw_write8(adapter, REG_BCN_CTRL_1, rtw_read8(adapter, REG_BCN_CTRL_1)&(~BIT(3)));

		rtw_write32(adapter, REG_TSFTR1, tsf);
		rtw_write32(adapter, REG_TSFTR1+4, tsf>>32);

		/* enable related TSF function */
		rtw_write8(adapter, REG_BCN_CTRL_1, rtw_read8(adapter, REG_BCN_CTRL_1)|BIT(3));

#ifdef CONFIG_TSF_RESET_OFFLOAD
		/*  Update buddy port's TSF(TBTT) if it is SoftAP for beacon TX issue! */
		if ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE
			&& check_buddy_fwstate(adapter, WIFI_AP_STATE)) {
			if (reset_tsf(adapter, IFACE_PORT0) == false)
				DBG_8192D("ERROR! %s()-%d: Reset port0 TSF fail\n",
					__func__, __LINE__);
		}
#endif	/*  CONFIG_TSF_RESET_OFFLOAD */

	}
	else	/*  adapter->iface_type == IFACE_PORT1 */
	{
		/* disable related TSF function */
		rtw_write8(adapter, REG_BCN_CTRL, rtw_read8(adapter, REG_BCN_CTRL)&(~BIT(3)));
		/*  disable TSF update instead! May induce burst beacon TX */

		rtw_write32(adapter, REG_TSFTR, tsf);
		rtw_write32(adapter, REG_TSFTR+4, tsf>>32);

		/* enable related TSF function */
		rtw_write8(adapter, REG_BCN_CTRL, rtw_read8(adapter, REG_BCN_CTRL)|BIT(3));

		/*  Update buddy port's TSF if it is SoftAP for beacon TX issue! */
		if ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE
			&& check_buddy_fwstate(adapter, WIFI_AP_STATE)
		) {
			/* disable related TSF function */
			rtw_write8(adapter, REG_BCN_CTRL_1, rtw_read8(adapter, REG_BCN_CTRL_1)&(~BIT(3)));
			/*  disable TSF update instead! */

			rtw_write32(adapter, REG_TSFTR1, tsf);
			rtw_write32(adapter, REG_TSFTR1+4, tsf>>32);

			/* enable related TSF function */
			rtw_write8(adapter, REG_BCN_CTRL_1, rtw_read8(adapter, REG_BCN_CTRL_1)|BIT(3));
		}
#ifdef CONFIG_TSF_RESET_OFFLOAD
		/*  Update buddy port's TSF if it is SoftAP for beacon TX issue! */
		if ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE
			&& check_buddy_fwstate(adapter, WIFI_AP_STATE)) {
			if (reset_tsf(adapter, IFACE_PORT1) == false)
				DBG_8192D("ERROR! %s()-%d: Reset port1 TSF fail\n",
					__func__, __LINE__);
		}
#endif	/*  CONFIG_TSF_RESET_OFFLOAD */
	}

	if (((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
	{
		ResumeTxBeacon(adapter);
	}
#endif	/*  CONFIG_CONCURRENT_MODE */
}

static void hw_var_set_mlme_disconnect(struct rtw_adapter *adapter,
				       u8 variable, u8 *val)
{
#ifdef CONFIG_CONCURRENT_MODE
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct rtw_adapter *pbuddy_adapter = adapter->pbuddy_adapter;

	if (check_buddy_mlmeinfo_state(adapter, _HW_STATE_NOLINK_))
		rtw_write16(adapter, REG_RXFLTMAP2, 0x00);

	if (adapter->iface_type == IFACE_PORT1)
	{
		int i;
		u8 reg_bcn_ctrl_1;

		/*  a.Driver set 0x422 bit 6 = 0 */
		rtw_write8(adapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) & (~BIT6));
		pHalData->RegFwHwTxQCtrl &= (~BIT6);

#ifdef CONFIG_BEACON_DISABLE_OFFLOAD
		u8 reg_bcn_disable_cnt = rtw_read8(adapter, REG_FW_BCN_DIS_CNT);
		DBG_8192D("%s()-%d: reg_bcn_disable_cnt =%02x\n", __func__, __LINE__, reg_bcn_disable_cnt);

		reg_bcn_ctrl_1 = rtw_read8(adapter, REG_BCN_CTRL_1);
		DBG_8192D("%s()-%d: reg_bcn_ctrl_1 =%02x\n", __func__, __LINE__, reg_bcn_ctrl_1);

		/*  b. driver set h2c cmd */
		rtl8192c_dis_beacon_fun_cmd(adapter);

		/*
			  FW Job for port 0

		   c. 8051 set nettype to ap
		   d. 8051 check dma_int
		   e. 8051 set nettype to no_link
		   f.8051 dis_tsf_update   0x550 bit 4
		   g.8051 reset  beacon function test count   0x553 bit0.
		   h.8051 disable beacon function   0x550  bit3
		   i. 8051 sent ready to driver

		*/

		/*  The worst case is 100 + 15 ms */
		rtw_msleep_os(120);

		for (i = 0; i< 10; i++) {
			reg_bcn_ctrl_1 = rtw_read8(adapter, REG_BCN_CTRL_1);
			if ((reg_bcn_ctrl_1 & BIT(3)) == 0) {
				break;
			}
			DBG_8192D("%s()-%d: BEACON_DISABLE_OFFLOAD not finished! REG_BCN_CTRL_1 =%02x\n", __func__, __LINE__, reg_bcn_ctrl_1);
			DBG_8192D("%s()-%d: reg_bcn_disable_cnt =%02x\n", __func__, __LINE__, rtw_read8(adapter, REG_FW_BCN_DIS_CNT));
			DBG_8192D("%s()-%d: REG_BCN_CTRL =%02x\n", __func__, __LINE__, rtw_read8(adapter, REG_BCN_CTRL));
			DBG_8192D("%s()-%d: FWISR =%08x\n", __func__, __LINE__, rtw_read32(adapter, REG_FWISR));
			rtw_msleep_os(100);
		}
		DBG_8192D("%s()-%d: reg_bcn_disable_cnt =%02x\n", __func__, __LINE__, rtw_read8(adapter, REG_FW_BCN_DIS_CNT));
		DBG_8192D("%s()-%d: reg_bcn_ctrl_1 =%02x\n", __func__, __LINE__, reg_bcn_ctrl_1);

#else   /*  CONFIG_BEACON_DISABLE_OFFLOAD */

		/* disable update TSF1 */
			rtw_write8(adapter, REG_BCN_CTRL_1, rtw_read8(adapter, REG_BCN_CTRL_1)|BIT(4));

		/* reset TSF1 */
		rtw_write8(adapter, REG_DUAL_TSF_RST, BIT(1));

		/*  disable Port1's beacon function */
		rtw_write8(adapter, REG_BCN_CTRL_1, rtw_read8(adapter, REG_BCN_CTRL_1)&(~BIT(3)));

#endif  /*  CONFIG_BEACON_DISABLE_OFFLOAD */

		/*  j, Driver set 0x422 bit 6 = 1 */
		rtw_write8(adapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) | BIT6);
		pHalData->RegFwHwTxQCtrl |= BIT6;

		/*  k. re_download beacon pkt */
		if (check_buddy_mlmeinfo_state(adapter, WIFI_FW_AP_STATE))
			set_tx_beacon_cmd(pbuddy_adapter);

	}
	else	/*  (adapter->iface_type == IFACE_PORT1) */
	{
		/* disable update TSF */
			rtw_write8(adapter, REG_BCN_CTRL, rtw_read8(adapter, REG_BCN_CTRL)|BIT(4));

		/* reset TSF */
		rtw_write8(adapter, REG_DUAL_TSF_RST, BIT(0));

		/*  Can't disable Port0's beacon function due to it is used by RA */
	}
#endif
}

static void hw_var_set_mlme_sitesurvey(struct rtw_adapter *adapter, u8 variable, u8 *val)
{
	u32	value_rcr, rcr_clear_bit, reg_bcn_ctl;
	u16	value_rxfltmap2;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct mlme_priv *pmlmepriv =&(adapter->mlmepriv);

#ifdef CONFIG_CONCURRENT_MODE
	if (adapter->iface_type == IFACE_PORT1)
		reg_bcn_ctl = REG_BCN_CTRL_1;
	else
#endif
		reg_bcn_ctl = REG_BCN_CTRL;

#ifdef CONFIG_FIND_BEST_CHANNEL

	if ((check_fwstate(pmlmepriv, WIFI_AP_STATE) == true)
#ifdef CONFIG_CONCURRENT_MODE
		|| (check_buddy_fwstate(adapter, WIFI_AP_STATE) == true)
#endif
		)
	{
		rcr_clear_bit = RCR_CBSSID_BCN;
	}
	else
	{
		rcr_clear_bit = (RCR_CBSSID_BCN | RCR_CBSSID_DATA);
	}

	/*  Recieve all data frames */
	value_rxfltmap2 = 0xFFFF;

#else /* CONFIG_FIND_BEST_CHANNEL */

	rcr_clear_bit = RCR_CBSSID_BCN;

	/* config RCR to receive different BSSID & not to receive data frame */
	value_rxfltmap2 = 0;

#endif /* CONFIG_FIND_BEST_CHANNEL */

	value_rcr = rtw_read32(adapter, REG_RCR);

	if (*((u8 *)val))/* under sitesurvey */
	{
		pHalData->bLoadIMRandIQKSettingFor2G = false;

		value_rcr &= ~(rcr_clear_bit);
		rtw_write32(adapter, REG_RCR, value_rcr);

		rtw_write16(adapter, REG_RXFLTMAP2, value_rxfltmap2);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE |WIFI_ADHOC_MASTER_STATE)) {
			/* disable update TSF */
			rtw_write8(adapter, reg_bcn_ctl, rtw_read8(adapter, reg_bcn_ctl)|BIT(4));
		}

		/*  Save orignal RRSR setting. */
		pHalData->RegRRSR = rtw_read16(adapter, REG_RRSR);

#ifdef CONFIG_CONCURRENT_MODE
		if (check_buddy_mlmeinfo_state(adapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(adapter, _FW_LINKED))
		{
			StopTxBeacon(adapter);
		}
#endif
	}
	else/* sitesurvey done */
	{
		if (check_fwstate(pmlmepriv, _FW_LINKED) || check_fwstate(pmlmepriv, WIFI_AP_STATE)
#ifdef CONFIG_CONCURRENT_MODE
			|| check_buddy_fwstate(adapter, _FW_LINKED) || check_buddy_fwstate(adapter, WIFI_AP_STATE)
#endif
			)
		{
			/* enable to rx data frame */
			rtw_write16(adapter, REG_RXFLTMAP2, 0xFFFF);
		}

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE |WIFI_ADHOC_MASTER_STATE)) {
			/* enable update TSF */
			rtw_write8(adapter, reg_bcn_ctl, rtw_read8(adapter, reg_bcn_ctl)&(~BIT(4)));
		}

		value_rcr |= rcr_clear_bit;
		rtw_write32(adapter, REG_RCR, value_rcr);

		/*  Restore orignal RRSR setting. */
		rtw_write16(adapter, REG_RRSR, pHalData->RegRRSR);

#ifdef CONFIG_CONCURRENT_MODE
		if (check_buddy_mlmeinfo_state(adapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(adapter, _FW_LINKED))
		{
			ResumeTxBeacon(adapter);
		}
#endif
	}
}

static void hw_var_set_mlme_join(struct rtw_adapter *adapter,
				 u8 variable, u8 *val)
{
#ifdef CONFIG_CONCURRENT_MODE
	u8	RetryLimit = 0x30;
	u8	type = *((u8 *)val);
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct mlme_priv	*pmlmepriv = &adapter->mlmepriv;

	if (type == 0) /*  prepare to join */
	{
		if (check_buddy_mlmeinfo_state(adapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(adapter, _FW_LINKED))
		{
			StopTxBeacon(adapter);
		}

		/* enable to rx data frame.Accept all data frame */
		rtw_write16(adapter, REG_RXFLTMAP2, 0xFFFF);

		if (check_buddy_mlmeinfo_state(adapter, WIFI_FW_AP_STATE))
			rtw_write32(adapter, REG_RCR, rtw_read32(adapter, REG_RCR)|RCR_CBSSID_BCN);
		else
			rtw_write32(adapter, REG_RCR, rtw_read32(adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true)
		{
			RetryLimit = (pHalData->CustomerID == RT_CID_CCX) ? 7 : 48;
		}
		else /*  Ad-hoc Mode */
		{
			RetryLimit = 0x7;
		}

		DBG_8192D("%s(): pHalData->bNeedIQK = true\n", __func__);
		pHalData->bNeedIQK = true; /* for 92D IQK */
	}
	else if (type == 1) /* joinbss_event call back when join res < 0 */
	{
		if (check_buddy_mlmeinfo_state(adapter, _HW_STATE_NOLINK_))
			rtw_write16(adapter, REG_RXFLTMAP2, 0x00);

		if (check_buddy_mlmeinfo_state(adapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(adapter, _FW_LINKED))
		{
			ResumeTxBeacon(adapter);
		}
	}
	else if (type == 2) /* sta add event call back */
	{

		/* enable update TSF */
		if (adapter->iface_type == IFACE_PORT1)
			rtw_write8(adapter, REG_BCN_CTRL_1, rtw_read8(adapter, REG_BCN_CTRL_1)&(~BIT(4)));
		else
			rtw_write8(adapter, REG_BCN_CTRL, rtw_read8(adapter, REG_BCN_CTRL)&(~BIT(4)));

		if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE))
		{
			/* fixed beacon issue for 8191su........... */
			rtw_write8(adapter, 0x542 , 0x02);
			RetryLimit = 0x7;
		}

		if (check_buddy_mlmeinfo_state(adapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(adapter, _FW_LINKED))
		{
			ResumeTxBeacon(adapter);
		}

	}

	rtw_write16(adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);

#endif
}

#ifdef CONFIG_DUALMAC_CONCURRENT
static void dc_hw_var_mlme_sitesurvey(struct rtw_adapter *adapter,
				      u8 sitesurvey_state)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct rtw_adapter *Buddyadapter = adapter->pbuddy_adapter;
	struct mlme_priv *pbuddy_mlmepriv;
	struct mlme_ext_priv *pbuddy_mlmeext;

	if ((Buddyadapter != NULL) &&
		adapter->DualMacConcurrent == true)
	{
		pbuddy_mlmepriv = &(Buddyadapter->mlmepriv);
		pbuddy_mlmeext = &Buddyadapter->mlmeextpriv;

		if (sitesurvey_state)/* under sitesurvey */
		{
			if (check_buddy_mlmeinfo_state(adapter, WIFI_FW_AP_STATE) &&
				check_buddy_fwstate(adapter, _FW_LINKED))
			{
				StopTxBeacon(Buddyadapter);
			}

			rtw_write16(adapter, REG_RRSR, 0x150);
		}
		else/* sitesurvey done */
		{
			if (check_buddy_mlmeinfo_state(adapter, WIFI_FW_AP_STATE) &&
				check_buddy_fwstate(adapter, _FW_LINKED))
			{
				ResumeTxBeacon(Buddyadapter);
			}
		}
	}
}

static void dc_hw_var_mlme_join(struct rtw_adapter *adapter, u8 join_state)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct rtw_adapter *Buddyadapter = adapter->pbuddy_adapter;
	struct mlme_priv *pbuddy_mlmepriv;
	struct mlme_ext_priv *pbuddy_mlmeext;

	if ((Buddyadapter != NULL) &&
		adapter->DualMacConcurrent == true)
	{
		pbuddy_mlmepriv = &(Buddyadapter->mlmepriv);
		pbuddy_mlmeext = &Buddyadapter->mlmeextpriv;

		if (pmlmeext->cur_channel != pbuddy_mlmeext->cur_channel ||
			pmlmeext->cur_bwmode != pbuddy_mlmeext->cur_bwmode ||
			pmlmeext->cur_ch_offset != pbuddy_mlmeext->cur_ch_offset)
		{
			if (join_state == 0)/*  prepare to join */
			{
				if (check_buddy_mlmeinfo_state(adapter, WIFI_FW_AP_STATE) &&
					check_buddy_fwstate(adapter, _FW_LINKED))
				{
					StopTxBeacon(Buddyadapter);
				}
			}
			else/* join success or fail */
			{
				if (check_buddy_mlmeinfo_state(adapter, WIFI_FW_AP_STATE) &&
					check_buddy_fwstate(adapter, _FW_LINKED))
				{
					ResumeTxBeacon(Buddyadapter);
				}
			}
		}
	}
}
#endif

static void SetHwReg8192DU(struct rtw_adapter *adapter, u8 variable, u8 *val)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	switch (variable)
	{
		case HW_VAR_MEDIA_STATUS:
			{
				u8 val8;

				val8 = rtw_read8(adapter, MSR)&0x0c;
				val8 |= *((u8 *)val);
				rtw_write8(adapter, MSR, val8);
			}
			break;
		case HW_VAR_MEDIA_STATUS1:
			{
				u8 val8;

				val8 = rtw_read8(adapter, MSR)&0x03;
				val8 |= *((u8 *)val) <<2;
				rtw_write8(adapter, MSR, val8);
			}
			break;
		case HW_VAR_SET_OPMODE:
#if defined(CONFIG_CONCURRENT_MODE)
			hw_var_set_opmode(adapter, variable, val);
#else /* CONFIG_CONCURRENT_MODE */
			{
				u8	val8;
				u8	mode = *((u8 *)val);

				if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_))
				{
					StopTxBeacon(adapter);
					rtw_write8(adapter, REG_BCN_CTRL, 0x18);
				}
				else if ((mode == _HW_STATE_ADHOC_) /*|| (mode == _HW_STATE_AP_)*/)
				{
					ResumeTxBeacon(adapter);
					rtw_write8(adapter, REG_BCN_CTRL, 0x1a);
				}
				else if (mode == _HW_STATE_AP_)
				{
					ResumeTxBeacon(adapter);

					rtw_write8(adapter, REG_BCN_CTRL, 0x12);

					/* Set RCR */
					rtw_write32(adapter, REG_RCR, 0x7000228e);/* CBSSID_DATA must set to 0 */
					/* enable to rx data frame */
					rtw_write16(adapter, REG_RXFLTMAP2, 0xFFFF);
					/* enable to rx ps-poll */
					rtw_write16(adapter, REG_RXFLTMAP1, 0x0400);

					/* Beacon Control related register for first time */
					rtw_write8(adapter, REG_BCNDMATIM, 0x02); /*  2ms */
					rtw_write8(adapter, REG_DRVERLYINT, 0x05);/*  5ms */
					rtw_write8(adapter, REG_ATIMWND, 0x0a); /*  10ms */
					rtw_write16(adapter, REG_BCNTCFG, 0x00);
					rtw_write16(adapter, REG_TBTT_PROHIBIT, 0x6404);

					/* reset TSF */
					rtw_write8(adapter, REG_DUAL_TSF_RST, BIT(0));

					/* enable TSF Function for if1 */
					rtw_write8(adapter, REG_BCN_CTRL, (EN_BCN_FUNCTION | EN_TXBCN_RPT));

					/* enable update TSF for if1 */
					rtw_write8(adapter, REG_BCN_CTRL, rtw_read8(adapter, REG_BCN_CTRL)&(~BIT(4)));
				}

				val8 = rtw_read8(adapter, MSR)&0x0c;
				val8 |= mode;
				rtw_write8(adapter, MSR, val8);
			}
#endif /* CONFIG_CONCURRENT_MODE */
			break;
		case HW_VAR_MAC_ADDR:
			hw_var_set_macaddr(adapter, variable, val);
			break;
		case HW_VAR_BSSID:
#if defined(CONFIG_CONCURRENT_MODE)
			hw_var_set_bssid(adapter, variable, val);
#else /* CONFIG_CONCURRENT_MODE */
			{
				u8	idx = 0;
				for (idx = 0 ; idx < 6; idx++)
				{
					rtw_write8(adapter, (REG_BSSID+idx), val[idx]);
				}
			}
#endif /* CONFIG_CONCURRENT_MODE */
			{
#ifdef CONFIG_DUALMAC_CONCURRENT
				struct rtw_adapter *	Buddyadapter = adapter->pbuddy_adapter;
#endif
				if (check_fwstate(&adapter->mlmepriv, WIFI_AP_STATE) == true)
				{
					DBG_8192D("%s(): pHalData->bNeedIQK = true\n", __func__);
					pHalData->bNeedIQK = true; /* for 92D IQK */
				}
#ifdef CONFIG_DUALMAC_CONCURRENT
				if ((Buddyadapter != NULL) && (pHalData->bSlaveOfDMSP))
				{
					if (check_fwstate(&Buddyadapter->mlmepriv, WIFI_AP_STATE) == true)
						GET_HAL_DATA(Buddyadapter)->bNeedIQK = true; /* for 92D IQK */
				}
#endif
			}
			break;
		case HW_VAR_INIT_DATA_RATE:
			{
				u8	init_data_rate = *((u8 *)val);
#ifdef CONFIG_CONCURRENT_MODE
				if (SECONDARY_ADAPTER == adapter->adapter_type) {
					rtw_write8(adapter, REG_INIDATA_RATE_SEL+2, init_data_rate);
					pdmpriv->INIDATA_RATE[2] = init_data_rate;
					DBG_8192D("HW_VAR_INIT_DATA_RATE: Set Init Data Rate(%#x) for MACID 2\n", rtw_read8(adapter, REG_INIDATA_RATE_SEL));
				}
				else
#endif
				{
					rtw_write8(adapter, REG_INIDATA_RATE_SEL, init_data_rate);
					pdmpriv->INIDATA_RATE[0] = init_data_rate;
					DBG_8192D("HW_VAR_INIT_DATA_RATE: Set Init Data Rate(%#x) for MACID 0\n", rtw_read8(adapter, REG_INIDATA_RATE_SEL));
				}
			}
			break;
		case HW_VAR_BASIC_RATE:
			{
				u16	BrateCfg = 0;
				u8	RateIndex = 0, b2GBand = false;
				struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;

				/*  2007.01.16, by Emily */
				/*  Select RRSR (in Legacy-OFDM and CCK) */
				/*  For 8190, we select only 24M, 12M, 6M, 11M, 5.5M, 2M, and 1M from the Basic rate. */
				/*  We do not use other rates. */
				halsetbratecfg(adapter, val, &BrateCfg);

				if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
					b2GBand = true;
				else
					b2GBand = false;

				if (b2GBand)
				{
					/* CCK 2M ACK should be disabled for some BCM and Atheros AP IOT */
					/* because CCK 2M has poor TXEVM */
					/* CCK 5.5M & 11M ACK should be enabled for better performance */
					pHalData->BasicRateSet = BrateCfg = (BrateCfg |0xd)& 0x15d;
					BrateCfg |= 0x1; /*  default enable 1M ACK rate */
				}
				else /*  5G */
				{
					pHalData->BasicRateSet &= 0xFF0;
					BrateCfg |= 0x10; /*  default enable 6M ACK rate */
				}

				DBG_8192D("HW_VAR_BASIC_RATE: BrateCfg(%#x)\n", BrateCfg);

				/*  Set RRSR rate table. */
				rtw_write8(adapter, REG_RRSR, BrateCfg&0xff);
				rtw_write8(adapter, REG_RRSR+1, (BrateCfg>>8)&0xff);
				rtw_write8(adapter, REG_RRSR+2, rtw_read8(adapter, REG_RRSR+2)&0xf0);

				/*  Set RTS initial rate */
				while (BrateCfg > 0x1) {
					BrateCfg = (BrateCfg>> 1);
					RateIndex++;
				}
				rtw_write8(adapter, REG_INIRTS_RATE_SEL, RateIndex);
			}
			break;
		case HW_VAR_TXPAUSE:
			rtw_write8(adapter, REG_TXPAUSE, *((u8 *)val));
			break;
		case HW_VAR_BCN_FUNC:
			hw_var_set_bcn_func(adapter, variable, val);
			break;
		case HW_VAR_CORRECT_TSF:
#ifdef CONFIG_CONCURRENT_MODE
			hw_var_set_correct_tsf(adapter, variable, val);
#else /* CONFIG_CONCURRENT_MODE */
			{
				u64	tsf;
				struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
				struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

				tsf = pmlmeext->TSFValue - rtw_modular64(pmlmeext->TSFValue, (pmlmeinfo->bcn_interval*1024)) -1024; /* us */

				if (((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
				{
					StopTxBeacon(adapter);
				}

				/* disable related TSF function */
				rtw_write8(adapter, REG_BCN_CTRL, rtw_read8(adapter, REG_BCN_CTRL)&(~BIT(3)));

				rtw_write32(adapter, REG_TSFTR, tsf);
				rtw_write32(adapter, REG_TSFTR+4, tsf>>32);

				/* enable related TSF function */
				rtw_write8(adapter, REG_BCN_CTRL, rtw_read8(adapter, REG_BCN_CTRL)|BIT(3));

				if (((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
				{
					ResumeTxBeacon(adapter);
				}
			}
#endif /* CONFIG_CONCURRENT_MODE */
			break;
		case HW_VAR_CHECK_BSSID:
			if (*((u8 *)val))
			{
				rtw_write32(adapter, REG_RCR, rtw_read32(adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);
			}
			else
			{
				u32	val32;

				val32 = rtw_read32(adapter, REG_RCR);

				val32 &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN);

				rtw_write32(adapter, REG_RCR, val32);
			}
			break;
		case HW_VAR_MLME_DISCONNECT:
#ifdef CONFIG_CONCURRENT_MODE
			hw_var_set_mlme_disconnect(adapter, variable, val);
#else /* CONFIG_CONCURRENT_MODE */
			{
				/* Set RCR to not to receive data frame when NO LINK state */
				rtw_write16(adapter, REG_RXFLTMAP2, 0x00);

				/* reset TSF */
				rtw_write8(adapter, REG_DUAL_TSF_RST, (BIT(0)|BIT(1)));

				/* disable update TSF */
				rtw_write8(adapter, REG_BCN_CTRL, rtw_read8(adapter, REG_BCN_CTRL)|BIT(4));
			}
#endif /* CONFIG_CONCURRENT_MODE */
			break;
		case HW_VAR_MLME_SITESURVEY:
			hw_var_set_mlme_sitesurvey(adapter, variable,  val);
#ifdef CONFIG_DUALMAC_CONCURRENT
			dc_hw_var_mlme_sitesurvey(adapter, *((u8 *)val));
#endif /* CONFIG_DUALMAC_CONCURRENT */
			break;
		case HW_VAR_MLME_JOIN:
#ifdef CONFIG_CONCURRENT_MODE
			hw_var_set_mlme_join(adapter, variable,  val);
#else /* CONFIG_CONCURRENT_MODE */
			{
				u8	RetryLimit = 0x30;
				u8	type = *((u8 *)val);
				struct mlme_priv	*pmlmepriv = &adapter->mlmepriv;
#ifdef CONFIG_DUALMAC_CONCURRENT
				struct rtw_adapter *	Buddyadapter = adapter->pbuddy_adapter;
#endif

				if (type == 0) /*  prepare to join */
				{
					/* enable to rx data frame.Accept all data frame */
					rtw_write16(adapter, REG_RXFLTMAP2, 0xFFFF);

					rtw_write32(adapter, REG_RCR, rtw_read32(adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);

					if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true)
					{
						RetryLimit = (pHalData->CustomerID == RT_CID_CCX) ? 7 : 48;
					}
					else /*  Ad-hoc Mode */
					{
						RetryLimit = 0x7;
					}

					DBG_8192D("%s(): pHalData->bNeedIQK = true\n", __func__);
					pHalData->bNeedIQK = true; /* for 92D IQK */
#ifdef CONFIG_DUALMAC_CONCURRENT
					if ((Buddyadapter != NULL) && (pHalData->bSlaveOfDMSP))
					{
						GET_HAL_DATA(Buddyadapter)->bNeedIQK = true; /* for 92D IQK */
					}
#endif
				}
				else if (type == 1) /* joinbss_event call back when join res < 0 */
				{
					/* config RCR to receive different BSSID & not to receive data frame during linking */

					rtw_write16(adapter, REG_RXFLTMAP2, 0x00);
				}
				else if (type == 2) /* sta add event call back */
				{
					/* enable update TSF */
					rtw_write8(adapter, REG_BCN_CTRL, rtw_read8(adapter, REG_BCN_CTRL)&(~BIT(4)));

					if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE))
					{
						/* fixed beacon issue for 8191su........... */
						RetryLimit = 0x7;
					}
				}

				rtw_write16(adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);
#ifdef CONFIG_DUALMAC_CONCURRENT
				dc_hw_var_mlme_join(adapter, *((u8 *)val));
#endif /* CONFIG_DUALMAC_CONCURRENT */
			}
#endif /* CONFIG_CONCURRENT_MODE */
			break;

		case HW_VAR_ON_RCR_AM:
			rtw_write32(adapter, REG_RCR, rtw_read32(adapter, REG_RCR)|RCR_AM);
			DBG_8192D("%s, %d, RCR = %x\n", __func__, __LINE__, rtw_read32(adapter, REG_RCR));
			break;
		case HW_VAR_OFF_RCR_AM:
			rtw_write32(adapter, REG_RCR, rtw_read32(adapter, REG_RCR)& (~RCR_AM));
			DBG_8192D("%s, %d, RCR = %x\n", __func__, __LINE__, rtw_read32(adapter, REG_RCR));
			break;

		case HW_VAR_BEACON_INTERVAL:
			rtw_write16(adapter, REG_BCN_INTERVAL, *((u16 *)val));
			break;
		case HW_VAR_SLOT_TIME:
			{
				u8	u1bAIFS, aSifsTime;
				struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
				struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

				DBG_8192D("Set HW_VAR_SLOT_TIME: SlotTime(%#x)\n", val[0]);
				rtw_write8(adapter, REG_SLOT, val[0]);

				if (pmlmeinfo->WMM_enable == 0)
				{
					if (pmlmeext->cur_wireless_mode == WIRELESS_11B)
						aSifsTime = 10;
					else
						aSifsTime = 16;

					u1bAIFS = aSifsTime + (2 * pmlmeinfo->slotTime);

					/*  <Roger_EXP> Temporary removed, 2008.06.20. */
					rtw_write8(adapter, REG_EDCA_VO_PARAM, u1bAIFS);
					rtw_write8(adapter, REG_EDCA_VI_PARAM, u1bAIFS);
					rtw_write8(adapter, REG_EDCA_BE_PARAM, u1bAIFS);
					rtw_write8(adapter, REG_EDCA_BK_PARAM, u1bAIFS);
				}
			}
			break;
		case HW_VAR_ACK_PREAMBLE:
			{
				u8	regTmp;
				u8	bShortPreamble = *((bool *)val);
				/*  Joseph marked out for Netgear 3500 TKIP channel 7 issue.(Temporarily) */
				regTmp = (pHalData->nCur40MhzPrimeSC)<<5;
				/* regTmp = 0; */
				if (bShortPreamble)
					regTmp |= 0x80;

				rtw_write8(adapter, REG_RRSR+2, regTmp);
			}
			break;
		case HW_VAR_SEC_CFG:
#ifdef CONFIG_CONCURRENT_MODE
			rtw_write8(adapter, REG_SECCFG, 0x0c |BIT(5));/* only enable tx enc and rx dec engine. */
#else /* CONFIG_CONCURRENT_MODE */
			rtw_write8(adapter, REG_SECCFG, *((u8 *)val));
#endif /* CONFIG_CONCURRENT_MODE */
			break;
		case HW_VAR_DM_FLAG:
			pdmpriv->DMFlag = *((u8 *)val);
			break;
		case HW_VAR_DM_FUNC_OP:
			if (val[0])
			{/*  save dm flag */
				pdmpriv->DMFlag_tmp = pdmpriv->DMFlag;
			}
			else
			{/*  restore dm flag */
				pdmpriv->DMFlag = pdmpriv->DMFlag_tmp;
			}
			break;
		case HW_VAR_DM_FUNC_SET:
			pdmpriv->DMFlag |= *((u8 *)val);
			break;
		case HW_VAR_DM_FUNC_CLR:
			pdmpriv->DMFlag &= *((u8 *)val);
			break;
		case HW_VAR_DM_INIT_PWDB:
			pdmpriv->UndecoratedSmoothedPWDB = 0;
			break;
		case HW_VAR_CAM_EMPTY_ENTRY:
			{
				u8	ucIndex = *((u8 *)val);
				u8	i;
				u32	ulCommand = 0;
				u32	ulContent = 0;
				u32	ulEncAlgo = CAM_AES;

				for (i = 0;i<CAM_CONTENT_COUNT;i++)
				{
					/*  filled id in CAM config 2 byte */
					if (i == 0)
					{
						ulContent |= (ucIndex & 0x03) | ((u16)(ulEncAlgo)<<2);
					}
					else
					{
						ulContent = 0;
					}
					/*  polling bit, and No Write enable, and address */
					ulCommand = CAM_CONTENT_COUNT*ucIndex+i;
					ulCommand = ulCommand | CAM_POLLINIG|CAM_WRITE;
					/*  write content 0 is equall to mark invalid */
					rtw_write32(adapter, WCAMI, ulContent);  /* delay_ms(40); */
					rtw_write32(adapter, RWCAM, ulCommand);  /* delay_ms(40); */
				}
			}
			break;
		case HW_VAR_CAM_INVALID_ALL:
			rtw_write32(adapter, RWCAM, BIT(31)|BIT(30));
			break;
		case HW_VAR_CAM_WRITE:
			{
				u32	cmd;
				u32	*cam_val = (u32 *)val;
				rtw_write32(adapter, WCAMI, cam_val[0]);

				cmd = CAM_POLLINIG | CAM_WRITE | cam_val[1];
				rtw_write32(adapter, RWCAM, cmd);
			}
			break;
		case HW_VAR_AC_PARAM_VO:
			rtw_write32(adapter, REG_EDCA_VO_PARAM, ((u32 *)(val))[0]);
			break;
		case HW_VAR_AC_PARAM_VI:
			rtw_write32(adapter, REG_EDCA_VI_PARAM, ((u32 *)(val))[0]);
			break;
		case HW_VAR_AC_PARAM_BE:
			pHalData->AcParam_BE = ((u32 *)(val))[0];
			rtw_write32(adapter, REG_EDCA_BE_PARAM, ((u32 *)(val))[0]);
			break;
		case HW_VAR_AC_PARAM_BK:
			rtw_write32(adapter, REG_EDCA_BK_PARAM, ((u32 *)(val))[0]);
			break;
		case HW_VAR_ACM_CTRL:
			{
				u8	acm_ctrl = *((u8 *)val);
				u8	AcmCtrl = rtw_read8(adapter, REG_ACMHWCTRL);

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

				DBG_8192D("[HW_VAR_ACM_CTRL] Write 0x%X\n", AcmCtrl);
				rtw_write8(adapter, REG_ACMHWCTRL, AcmCtrl);
			}
			break;
		case HW_VAR_AMPDU_MIN_SPACE:
			{
				u8	MinSpacingToSet;
				u8	SecMinSpace;

				MinSpacingToSet = *((u8 *)val);
				if (MinSpacingToSet <= 7)
				{
					switch (adapter->securitypriv.dot11PrivacyAlgrthm)
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

					if (MinSpacingToSet < SecMinSpace) {
						MinSpacingToSet = SecMinSpace;
					}

					rtw_write8(adapter, REG_AMPDU_MIN_SPACE, (rtw_read8(adapter, REG_AMPDU_MIN_SPACE) & 0xf8) | MinSpacingToSet);
				}
			}
			break;
		case HW_VAR_AMPDU_FACTOR:
			{
				u8	FactorToSet;
				u32	RegToSet;
				u8	*pTmpByte = NULL;
				u8	index = 0;

				RegToSet = 0xb972a841;
				if (pHalData->MacPhyMode92D == SINGLEMAC_SINGLEPHY) {
					RegToSet = 0x88728841;
				}
				else if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY) {
					RegToSet = 0x66525541;
				}
				else if (pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY) {
					RegToSet = 0x44444441;
				}

				FactorToSet = *((u8 *)val);
				if (FactorToSet <= 3)
				{
					FactorToSet = (1<<(FactorToSet + 2));
					if (FactorToSet>0xf)
						FactorToSet = 0xf;

					for (index = 0; index<4; index++)
					{
						pTmpByte = (u8 *)(&RegToSet) + index;

						if ((*pTmpByte & 0xf0) > (FactorToSet<<4))
							*pTmpByte = (*pTmpByte & 0x0f) | (FactorToSet<<4);

						if ((*pTmpByte & 0x0f) > FactorToSet)
							*pTmpByte = (*pTmpByte & 0xf0) | (FactorToSet);
					}

					rtw_write32(adapter, REG_AGGLEN_LMT, RegToSet);
				}
			}
			break;
		case HW_VAR_RXDMA_AGG_PG_TH:
			{
				u8	threshold = *((u8 *)val);
				if (threshold == 0)
				{
					threshold = pHalData->UsbRxAggPageCount;
				}
				rtw_write8(adapter, REG_RXDMA_AGG_PG_TH, threshold);
			}
			break;
		case HW_VAR_SET_RPWM:
			{
				u8	RpwmVal = (*(u8 *)val);
				RpwmVal = RpwmVal & 0xf;

				FillH2CCmd92D(adapter, H2C_PWRM, 1, (u8 *)(&RpwmVal));
			}
			break;
		case HW_VAR_H2C_FW_PWRMODE:
			rtl8192d_set_FwPwrMode_cmd(adapter, (*(u8 *)val));
			break;
		case HW_VAR_H2C_FW_JOINBSSRPT:
			rtl8192d_set_FwJoinBssReport_cmd(adapter, (*(u8 *)val));
			break;
		case HW_VAR_INITIAL_GAIN:
			{
				struct DIG_T *dig_table = &pdmpriv->DM_DigTable;
				u32		rx_gain = ((u32 *)(val))[0];

				if (rx_gain == 0xff) {/* restore rx gain */
					dig_table->curigvalue = dig_table->backupigvalue;
					PHY_SetBBReg(adapter, rOFDM0_XAAGCCore1, 0x7f, dig_table->curigvalue);
					PHY_SetBBReg(adapter, rOFDM0_XBAGCCore1, 0x7f, dig_table->curigvalue);
				}
				else {
					dig_table->backupigvalue = dig_table->curigvalue;
					PHY_SetBBReg(adapter, rOFDM0_XAAGCCore1, 0x7f, rx_gain);
					PHY_SetBBReg(adapter, rOFDM0_XBAGCCore1, 0x7f, rx_gain);
					dig_table->curigvalue = (u8)rx_gain;
				}
			}
			break;
		case HW_VAR_TRIGGER_GPIO_0:
			rtl8192du_trigger_gpio_0(adapter);
			break;
		case HW_VAR_EFUSE_BYTES: /*  To set EFUE total used bytes, added by Roger, 2008.12.22. */
			pHalData->EfuseUsedBytes = *((u16 *)val);
			break;
		case HW_VAR_FIFO_CLEARN_UP:
			{
				#define RW_RELEASE_EN		BIT18
				#define RXDMA_IDLE			BIT17

				struct pwrctrl_priv *pwrpriv = &adapter->pwrctrlpriv;
				u8 trycnt = 100;

				/* pause tx */
				rtw_write8(adapter, REG_TXPAUSE, 0xff);

				/* keep sn */
				adapter->xmitpriv.nqos_ssn = rtw_read16(adapter, REG_NQOS_SEQ);

				if (pwrpriv->bkeepfwalive != true)
				{
					/* RX DMA stop */
					rtw_write32(adapter, REG_RXPKT_NUM, (rtw_read32(adapter, REG_RXPKT_NUM)|RW_RELEASE_EN));
					do{
						if (!(rtw_read32(adapter, REG_RXPKT_NUM)&RXDMA_IDLE))
							break;
					}while (trycnt--);
					if (trycnt == 0)
						DBG_8192D("Stop RX DMA failed......\n");

					/* RQPN Load 0 */
					rtw_write16(adapter, REG_RQPN_NPQ, 0x0);
					rtw_write32(adapter, REG_RQPN, 0x80000000);
					rtw_mdelay_os(10);
				}

			}
			break;
		case HW_VAR_WOWLAN:
#ifdef CONFIG_WOWLAN
			{
				struct wowlan_ioctl_param *poidparam;

				int res;

				poidparam = (struct wowlan_ioctl_param *)val;
				switch (poidparam->subcode) {
					case WOWLAN_PATTERN_MATCH:
						/* Turn on the Pattern Match feature */
						DBG_8192D("\n PATTERN_MATCH poidparam->subcode_value =%d\n", poidparam->subcode_value);
						if (poidparam->subcode_value == 1) {
							adapter->pwrctrlpriv.wowlan_pattern = true;
							DBG_8192D("%s adapter->pwrctrlpriv.wowlan_pattern =%x\n", __func__, adapter->pwrctrlpriv.wowlan_pattern);
						}
						else {
							adapter->pwrctrlpriv.wowlan_pattern = false;
						}
						break;
					case WOWLAN_MAGIC_PACKET:
						/* Turn on the Magic Packet feature */
						DBG_8192D("\n MAGIC_PACKET poidparam->subcode_value =%d\n", poidparam->subcode_value);
						if (poidparam->subcode_value == 1) {
							adapter->pwrctrlpriv.wowlan_magic = true;
							DBG_8192D("%s adapter->pwrctrlpriv.wowlan_magic =%x\n", __func__, adapter->pwrctrlpriv.wowlan_magic);
						}
						else {
							adapter->pwrctrlpriv.wowlan_magic = false;
						}
						break;
					case WOWLAN_UNICAST:
						/* Turn on the Unicast wakeup feature */
						if (poidparam->subcode_value == 1) {
							adapter->pwrctrlpriv.wowlan_unicast = true;
						}
						else {
							adapter->pwrctrlpriv.wowlan_unicast = false;
							DBG_8192D("%s adapter->pwrctrlpriv.wowlan_unicast =%x\n", __func__, adapter->pwrctrlpriv.wowlan_unicast);
						}
						break;
					case WOWLAN_SET_PATTERN:
						/* Setting the Pattern for wowlan */
						res = rtw_wowlan_set_pattern(adapter, poidparam->pattern);
						if (res)
							DBG_8192D("rtw_wowlan_set_pattern retern value = 0x%x", res);
						break;
					case WOWLAN_DUMP_REG:
						/* dump the WKFMCAM and WOW_CTRL register */

						break;
					case WOWLAN_ENABLE:
						SetFwRelatedForWoWLAN8192DU(adapter, true);
						/* Set Pattern */
						if (adapter->pwrctrlpriv.wowlan_pattern == true)
							rtw_wowlan_reload_pattern(adapter);
						rtl8192d_set_wowlan_cmd(adapter);
						rtw_msleep_os(10);
						break;

					case WOWLAN_DISABLE:
						adapter->pwrctrlpriv.wowlan_mode = false;
						rtl8192d_set_wowlan_cmd(adapter);
						rtw_msleep_os(10);
						break;

					case WOWLAN_STATUS:
						poidparam->wakeup_reason = rtw_read8(adapter, REG_WOWLAN_REASON);
						DBG_8192D("wake on wlan reason 0x%02x\n", poidparam->wakeup_reason);
						break;

					case WOWLAN_DEBUG_RELOAD_FW:
						break;
					case WOWLAN_DEBUG_1:
						{
							u16 GPIO_val;
							if (poidparam->subcode_value == 1)
							{
								GPIO_val = rtw_read16(adapter, REG_GPIO_PIN_CTRL+1);
								GPIO_val |= BIT(0)|BIT(8);
								/* set GPIO 0 to high for Toshiba */
								rtw_write16(adapter, REG_GPIO_PIN_CTRL+1, GPIO_val);
							}
							else
							{
								GPIO_val = rtw_read16(adapter, REG_GPIO_PIN_CTRL+1);
								GPIO_val |= BIT(8);
								GPIO_val &= ~BIT(0);
								/* set GPIO 0 to low for Toshiba */
								rtw_write16(adapter, REG_GPIO_PIN_CTRL+1, GPIO_val);
							}
						}
						break;
					case WOWLAN_DEBUG_2:
						{
							u16 GPIO_val;
							u8 reg = 0;
#ifdef CONFIG_WOWLAN_MANUAL
							if (poidparam->subcode_value == 1)
							{

								/* prevent 8051 to be reset by PERST# wake on wlan by Alex & Baron */
								reg = rtw_read8(adapter, REG_RSV_CTRL);
								rtw_write8(adapter, REG_RSV_CTRL, reg| BIT(5));
								rtw_write8(adapter, REG_RSV_CTRL, reg| BIT(6)|BIT(5));
								/* for Toshiba only, they should call rtw_suspend before suspend */
								rtw_suspend_toshiba(adapter);
							}
							else
							{
								/* unmask usb se0 reset by Alex and DD */
								reg = rtw_read8(adapter, 0xf8);
								reg |= BIT(3)|BIT(4);
								rtw_write8(adapter, 0xf8, reg);

								/* for Toshiba only, they should call rtw_resume before resume */
								rtw_resume_toshiba(adapter);
								/* suggest by Scott */
								reg = rtw_read8(adapter, REG_RSV_CTRL);
								reg &= ~(BIT(5)|BIT(6));
								rtw_write8(adapter, REG_RSV_CTRL, reg);

							}
#endif /* CONFIG_WOWLAN_MANUAL */
						}
						break;
					default:
						break;
				}
				if (adapter->pwrctrlpriv.wowlan_unicast||adapter->pwrctrlpriv.wowlan_magic || adapter->pwrctrlpriv.wowlan_pattern)
					adapter->pwrctrlpriv.wowlan_mode = true;
				else
					adapter->pwrctrlpriv.wowlan_mode = false;
			}
			break;
#endif /* CONFIG_WOWLAN */
		case HW_VAR_CHECK_TXBUF:
#if defined(CONFIG_CONCURRENT_MODE) || defined(CONFIG_DUALMAC_CONCURRENT)
			{
				int i;
				u8	RetryLimit = 0x01;

				rtw_write16(adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);

				for (i = 0;i<1000;i++)
				{
					if (rtw_read32(adapter, 0x200) != rtw_read32(adapter, 0x204))
					{
						rtw_msleep_os(10);
					}
					else
					{
						DBG_8192D("no packet in tx packet buffer (%d)\n", i);
						break;
					}
				}

				RetryLimit = 0x30;
				rtw_write16(adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);

			}
#endif
			break;
		case HW_VAR_BCN_VALID:
			/* BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2, write 1 to clear, Clear by sw */
			rtw_write8(adapter, REG_TDECTRL+2, rtw_read8(adapter, REG_TDECTRL+2) | BIT0);
			break;
		default:
			break;
	}

}

static void GetHwReg8192DU(struct rtw_adapter *adapter, u8 variable, u8 *val)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	switch (variable)
	{
		case HW_VAR_BASIC_RATE:
			*((u16 *)(val)) = pHalData->BasicRateSet;
			break;
		case HW_VAR_TXPAUSE:
			val[0] = rtw_read8(adapter, REG_TXPAUSE);
			break;
		case HW_VAR_BCN_VALID:
			/* BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2 */
			val[0] = (BIT0 & rtw_read8(adapter, REG_TDECTRL+2))?true:false;
			break;
		case HW_VAR_DM_FLAG:
			val[0] = pHalData->dmpriv.DMFlag;
			break;
		case HW_VAR_RF_TYPE:
			val[0] = pHalData->rf_type;
			break;
		case HW_VAR_FWLPS_RF_ON:
			{
				/* When we halt NIC, we should check if FW LPS is leave. */
				u32	valRCR;

				if (adapter->pwrctrlpriv.rf_pwrstate == rf_off)
				{
					/*  If it is in HW/SW Radio OFF or IPS state, we do not check Fw LPS Leave, */
					/*  because Fw is unload. */
					val[0] = true;
				}
				else
				{
					valRCR = rtw_read32(adapter, REG_RCR);
					valRCR &= 0x00070000;
					if (valRCR)
						val[0] = false;
					else
						val[0] = true;
				}
			}
			break;
		case HW_VAR_EFUSE_BYTES: /*  To get EFUE total used bytes, added by Roger, 2008.12.22. */
			*((u16 *)(val)) = pHalData->EfuseUsedBytes;
			break;
		case HW_VAR_VID:
			*((u16 *)(val)) = pHalData->EEPROMVID;
			break;
		case HW_VAR_PID:
			*((u16 *)(val)) = pHalData->EEPROMPID;
			break;
		default:
			break;
	}

}

/*  */
/*	Description: */
/*		Query setting of specified variable. */
/*  */
static u8 GetHalDefVar8192DUsb(struct rtw_adapter *adapter, enum HAL_DEF_VARIABLE eVariable, void  *pValue)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8 bResult = true;

	switch (eVariable) {
	case HAL_DEF_UNDERCORATEDSMOOTHEDPWDB:
		*((int *)pValue) = pHalData->dmpriv.UndecoratedSmoothedPWDB;
		break;
	case HAL_DEF_DRVINFO_SZ:
		*((u32*)pValue) = DRVINFO_SZ;
		break;
	case HAL_DEF_MAX_RECVBUF_SZ:
		*((u32*)pValue) = MAX_RECVBUF_SZ;
		break;
	case HAL_DEF_RX_PACKET_OFFSET:
		*((u32*)pValue) = RXDESC_SIZE + DRVINFO_SZ;
		break;
	case HAL_DEF_DBG_DM_FUNC:
		*((u8*)pValue) = pHalData->dmpriv.DMFlag;
		break;
	default:
		bResult = false;
		break;
	}
	return bResult;
}

/*  */
/*	Description: */
/*		Change default setting of specified variable. */
/*  */
static u8 SetHalDefVar8192DUsb(
	struct rtw_adapter *				adapter,
	enum HAL_DEF_VARIABLE		eVariable,
	void *pValue
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	u8 bResult = true;

	switch (eVariable) {
		case HAL_DEF_DBG_DM_FUNC:
			{
				u8 dm_func = *((u8*)pValue);
				struct dm_priv	*pdmpriv = &pHalData->dmpriv;

				if (dm_func == 0) { /* disable all dynamic func */
					pdmpriv->DMFlag = DYNAMIC_FUNC_DISABLE;
					DBG_8192D("==> Disable all dynamic function...\n");
				}
				else if (dm_func == 1) {/* disable DIG */
					pdmpriv->DMFlag &= (~DYNAMIC_FUNC_DIG);
					DBG_8192D("==> Disable DIG...\n");
				}
				else if (dm_func == 2) {/* disable High power */
					pdmpriv->DMFlag &= (~DYNAMIC_FUNC_HP);
				}
				else if (dm_func == 3) {/* disable tx power tracking */
					pdmpriv->DMFlag &= (~DYNAMIC_FUNC_SS);
					DBG_8192D("==> Disable tx power tracking...\n");
				}
				else if (dm_func == 4) {/* disable BT coexistence */
					pdmpriv->DMFlag &= (~DYNAMIC_FUNC_BT);
				}
				else if (dm_func == 5) {/* disable antenna diversity */
					pdmpriv->DMFlag &= (~DYNAMIC_FUNC_ANT_DIV);
				}
				else if (dm_func == 6) {/* turn on all dynamic func */
					if (!(pdmpriv->DMFlag & DYNAMIC_FUNC_DIG))
					{
						struct dm_priv	*pdmpriv = &pHalData->dmpriv;
						struct DIG_T	*dig_table = &pdmpriv->DM_DigTable;
						dig_table->preigvalue = rtw_read8(adapter, 0xc50);
					}

					pdmpriv->DMFlag |= (DYNAMIC_FUNC_DIG|DYNAMIC_FUNC_HP|DYNAMIC_FUNC_SS|
						DYNAMIC_FUNC_BT|DYNAMIC_FUNC_ANT_DIV) ;
					DBG_8192D("==> Turn on all dynamic function...\n");
				}
			}
			break;
		default:
			bResult = false;
			break;
	}

	return bResult;
}

static u32  _update_92cu_basic_rate(struct rtw_adapter *padapter, unsigned int mask)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(padapter);
#ifdef CONFIG_BT_COEXIST
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);
#endif
	unsigned int BrateCfg = 0;

#ifdef CONFIG_BT_COEXIST
	if (	(pbtpriv->BT_Coexist) &&	(pbtpriv->BT_CoexistType == BT_CSR_BC4)	)
	{
		BrateCfg = mask  & 0x151;
	}
	else
#endif
	{
		if (pHalData->VersionID != VERSION_TEST_CHIP_88C)
			BrateCfg = mask  & 0x15F;
		else	/* for 88CU 46PING setting, Disable CCK 2M, 5.5M, Others must tuning */
			BrateCfg = mask  & 0x159;
	}

	BrateCfg |= 0x01; /*  default enable 1M ACK rate */

	return BrateCfg;
}

static void _update_response_rate(struct rtw_adapter *padapter, unsigned int mask)
{
	u8	RateIndex = 0;
	/*  Set RRSR rate table. */
	rtw_write8(padapter, REG_RRSR, mask&0xff);
	rtw_write8(padapter, REG_RRSR+1, (mask>>8)&0xff);

	/*  Set RTS initial rate */
	while (mask > 0x1)
	{
		mask = (mask>> 1);
		RateIndex++;
	}
	rtw_write8(padapter, REG_INIRTS_RATE_SEL, RateIndex);
}

static void UpdateHalRAMask8192DUsb(struct rtw_adapter *padapter, u32 mac_id)
{
	u32	value[2];
	u8	init_rate = 0;
	u8	networkType, raid;
	u32	mask;
	u8	shortGIrate = false;
	int	supportRateNum = 0;
	struct sta_info	*psta;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(padapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex	*cur_network = &(pmlmeinfo->network);
#ifdef CONFIG_BT_COEXIST
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);
#endif

	if (mac_id >= NUM_STA)
	{
		return;
	}

	psta = pmlmeinfo->FW_sta_info[mac_id].psta;
	if (psta == NULL)
	{
		return;
	}

	switch (mac_id) {
	case 0:/*  for infra mode */
#ifdef CONFIG_CONCURRENT_MODE
	case 2:/*  first station uses macid = 0, second station uses macid = 2 */
#endif /* CONFIG_CONCURRENT_MODE */
		supportRateNum = rtw_get_rateset_len(cur_network->SupportedRates);
		networkType = judge_network_type(padapter, cur_network->SupportedRates, supportRateNum);
		raid = networktype_to_raid(networkType);

		mask = update_supported_rate(cur_network->SupportedRates, supportRateNum);
		mask |= (pmlmeinfo->HT_enable)? update_MSC_rate(&(pmlmeinfo->HT_caps)): 0;

		mask |= ((raid<<28)&0xf0000000);

		if (support_short_GI(padapter, &(pmlmeinfo->HT_caps)))
		{
			shortGIrate = true;
		}

		break;

	case 1:/* for broadcast/multicast */
		supportRateNum = rtw_get_rateset_len(pmlmeinfo->FW_sta_info[mac_id].SupportedRates);
		if (pmlmeext->cur_wireless_mode & WIRELESS_11B)
			networkType = WIRELESS_11B;
		else
			networkType = WIRELESS_11G;
		raid = networktype_to_raid(networkType);

		mask = update_basic_rate(cur_network->SupportedRates, supportRateNum);
		mask |= ((raid<<28)&0xf0000000);

		break;

	default: /* for each sta in IBSS */
		supportRateNum = rtw_get_rateset_len(pmlmeinfo->FW_sta_info[mac_id].SupportedRates);
		networkType = judge_network_type(padapter, pmlmeinfo->FW_sta_info[mac_id].SupportedRates, supportRateNum);
		raid = networktype_to_raid(networkType);

		mask = update_supported_rate(cur_network->SupportedRates, supportRateNum);
		mask |= ((raid<<28)&0xf0000000);

		/* todo: support HT in IBSS */

		break;
	}

#ifdef CONFIG_BT_COEXIST
	if ((pbtpriv->BT_Coexist) &&
		(pbtpriv->BT_CoexistType == BT_CSR_BC4) &&
		(pbtpriv->BT_CUR_State) &&
		(pbtpriv->BT_Ant_isolation) &&
		((pbtpriv->BT_Service == BT_SCO)||
		(pbtpriv->BT_Service == BT_Busy)))
		mask &= 0xffffcfc0;
	else
#endif
		mask &= 0xffffffff;

	init_rate = get_highest_rate_idx(mask)&0x3f;

	if (pHalData->fw_ractrl == true)
	{
		value[0] = mask;
		value[1] = mac_id | (shortGIrate?0x20:0x00) | 0x80;

		DBG_8192D("update raid entry, mask = 0x%x, arg = 0x%x\n", value[0], value[1]);

		FillH2CCmd92D(padapter, H2C_RA_MASK, 5, (u8 *)(&value));
	}
	else
	{
		if (shortGIrate == true)
			init_rate |= BIT(6);

		rtw_write8(padapter, (REG_INIDATA_RATE_SEL+mac_id), init_rate);
	}

	/* set ra_id */
	psta->raid = raid;
	psta->init_rate = init_rate;

	/* set correct initial date rate for each mac_id */
	pdmpriv->INIDATA_RATE[mac_id] = init_rate;
}

static void SetBeaconRelatedRegisters8192DUsb(struct rtw_adapter *padapter)
{
	u32	value32;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	rtw_write8(padapter, REG_ATIMWND, 0x02);

	rtw_write16(padapter, REG_BCN_INTERVAL, pmlmeinfo->bcn_interval);

	_InitBeaconParameters(padapter);

	rtw_write8(padapter, REG_SLOT, 0x09);

	/*  */
	/*  Reset TSF Timer to zero, added by Roger. 2008.06.24 */
	/*  */
	value32 = rtw_read32(padapter, REG_TCR);
	value32 &= ~TSFRST;
	rtw_write32(padapter, REG_TCR, value32);

	value32 |= TSFRST;
	rtw_write32(padapter, REG_TCR, value32);

	/*  NOTE: Fix test chip's bug (about contention windows's randomness) */
	rtw_write8(padapter,  REG_RXTSF_OFFSET_CCK, 0x50);
	rtw_write8(padapter, REG_RXTSF_OFFSET_OFDM, 0x50);

	_BeaconFunctionEnable(padapter, true, true);

	ResumeTxBeacon(padapter);

	rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)|BIT(1));
}

static void rtl8192du_init_default_value(struct rtw_adapter *padapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv *pwrctrlpriv = &padapter->pwrctrlpriv;
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	pHalData->CurrentWirelessMode = WIRELESS_MODE_AUTO;

	/* init default value */
	pHalData->fw_ractrl = false;
	if (!pwrctrlpriv->bkeepfwalive)
		pHalData->LastHMEBoxNum = 0;

	pHalData->bEarlyModeEnable = 0;
	pHalData->pwrGroupCnt = 0;

	/* init dm default value */
	pdmpriv->TM_Trigger = 0;
	pdmpriv->prv_traffic_idx = 3;

	rtl8192d_PHY_ResetIQKResult(padapter);
}

void rtl8192du_set_hal_ops(struct rtw_adapter *padapter)
{
	struct hal_ops	*pHalFunc = &padapter->HalFunc;

	padapter->HalData = kzalloc(sizeof(struct hal_data_8192du), GFP_KERNEL);
	if (padapter->HalData == NULL) {
		DBG_8192D("cant not alloc memory for HAL DATA\n");
	}
	padapter->hal_data_sz = sizeof(struct hal_data_8192du);

	pHalFunc->hal_init = &rtl8192du_hal_init;
	pHalFunc->hal_deinit = &rtl8192du_hal_deinit;

	pHalFunc->inirp_init = &rtl8192du_inirp_init;
	pHalFunc->inirp_deinit = &rtl8192du_inirp_deinit;

	pHalFunc->init_xmit_priv = &rtl8192du_init_xmit_priv;
	pHalFunc->free_xmit_priv = &rtl8192du_free_xmit_priv;

	pHalFunc->init_recv_priv = &rtl8192du_init_recv_priv;
	pHalFunc->free_recv_priv = &rtl8192du_free_recv_priv;
	pHalFunc->InitSwLeds = &rtl8192du_InitSwLeds;
	pHalFunc->DeInitSwLeds = &rtl8192du_DeInitSwLeds;

	pHalFunc->init_default_value = &rtl8192du_init_default_value;
	pHalFunc->intf_chip_configure = &rtl8192du_interface_configure;
	pHalFunc->read_adapter_info = &ReadadapterInfo8192DU;

	pHalFunc->hal_dm_watchdog = &rtl8192d_HalDmWatchDog;

	pHalFunc->SetHwRegHandler = &SetHwReg8192DU;
	pHalFunc->GetHwRegHandler = &GetHwReg8192DU;
	pHalFunc->GetHalDefVarHandler = &GetHalDefVar8192DUsb;
	pHalFunc->SetHalDefVarHandler = &SetHalDefVar8192DUsb;

	pHalFunc->UpdateRAMaskHandler = &UpdateHalRAMask8192DUsb;
	pHalFunc->SetBeaconRelatedRegistersHandler = &SetBeaconRelatedRegisters8192DUsb;

	pHalFunc->hal_xmit = &rtl8192du_hal_xmit;
	pHalFunc->mgnt_xmit = &rtl8192du_mgnt_xmit;

#ifdef CONFIG_HOSTAPD_MLME
	pHalFunc->hostap_mgnt_xmit_entry = &rtl8192du_hostap_mgnt_xmit_entry;
#endif

	rtl8192d_set_hal_ops(pHalFunc);

}
