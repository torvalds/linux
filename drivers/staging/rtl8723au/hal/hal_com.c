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
#include <osdep_service.h>
#include <drv_types.h>

#include <hal_intf.h>
#include <hal_com.h>
#include <rtl8723a_hal.h>
#include <usb_ops_linux.h>

#define _HAL_INIT_C_

#define	EEPROM_CHANNEL_PLAN_BY_HW_MASK	0x80

/* return the final channel plan decision */
/* hw_channel_plan:  channel plan from HW (efuse/eeprom) */
/* sw_channel_plan:  channel plan from SW (registry/module param) */
/* def_channel_plan: channel plan used when the former two is invalid */
u8 hal_com_get_channel_plan23a(struct rtw_adapter *padapter, u8 hw_channel_plan,
			    u8 sw_channel_plan, u8 def_channel_plan,
			    bool AutoLoadFail)
{
	u8 swConfig;
	u8 chnlPlan;

	swConfig = true;
	if (!AutoLoadFail) {
		if (!rtw_is_channel_plan_valid(sw_channel_plan))
			swConfig = false;
		if (hw_channel_plan & EEPROM_CHANNEL_PLAN_BY_HW_MASK)
			swConfig = false;
	}

	if (swConfig == true)
		chnlPlan = sw_channel_plan;
	else
		chnlPlan = hw_channel_plan & (~EEPROM_CHANNEL_PLAN_BY_HW_MASK);

	if (!rtw_is_channel_plan_valid(chnlPlan))
		chnlPlan = def_channel_plan;

	return chnlPlan;
}

u8 MRateToHwRate23a(u8 rate)
{
	u8 ret = DESC_RATE1M;

	switch (rate) {
		/*  CCK and OFDM non-HT rates */
	case IEEE80211_CCK_RATE_1MB:
		ret = DESC_RATE1M;
		break;
	case IEEE80211_CCK_RATE_2MB:
		ret = DESC_RATE2M;
		break;
	case IEEE80211_CCK_RATE_5MB:
		ret = DESC_RATE5_5M;
		break;
	case IEEE80211_CCK_RATE_11MB:
		ret = DESC_RATE11M;
		break;
	case IEEE80211_OFDM_RATE_6MB:
		ret = DESC_RATE6M;
		break;
	case IEEE80211_OFDM_RATE_9MB:
		ret = DESC_RATE9M;
		break;
	case IEEE80211_OFDM_RATE_12MB:
		ret = DESC_RATE12M;
		break;
	case IEEE80211_OFDM_RATE_18MB:
		ret = DESC_RATE18M;
		break;
	case IEEE80211_OFDM_RATE_24MB:
		ret = DESC_RATE24M;
		break;
	case IEEE80211_OFDM_RATE_36MB:
		ret = DESC_RATE36M;
		break;
	case IEEE80211_OFDM_RATE_48MB:
		ret = DESC_RATE48M;
		break;
	case IEEE80211_OFDM_RATE_54MB:
		ret = DESC_RATE54M;
		break;

		/*  HT rates since here */
		/* case MGN_MCS0:	ret = DESC_RATEMCS0;    break; */
		/* case MGN_MCS1:	ret = DESC_RATEMCS1;    break; */
		/* case MGN_MCS2:	ret = DESC_RATEMCS2;    break; */
		/* case MGN_MCS3:	ret = DESC_RATEMCS3;    break; */
		/* case MGN_MCS4:	ret = DESC_RATEMCS4;    break; */
		/* case MGN_MCS5:	ret = DESC_RATEMCS5;    break; */
		/* case MGN_MCS6:	ret = DESC_RATEMCS6;    break; */
		/* case MGN_MCS7:	ret = DESC_RATEMCS7;    break; */

	default:
		break;
	}
	return ret;
}

void HalSetBrateCfg23a(struct rtw_adapter *padapter, u8 *mBratesOS)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u8 i, is_brate, brate;
	u16 brate_cfg = 0;
	u8 rate_index;

	for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {
		is_brate = mBratesOS[i] & IEEE80211_BASIC_RATE_MASK;
		brate = mBratesOS[i] & 0x7f;

		if (is_brate) {
			switch (brate) {
			case IEEE80211_CCK_RATE_1MB:
				brate_cfg |= RATE_1M;
				break;
			case IEEE80211_CCK_RATE_2MB:
				brate_cfg |= RATE_2M;
				break;
			case IEEE80211_CCK_RATE_5MB:
				brate_cfg |= RATE_5_5M;
				break;
			case IEEE80211_CCK_RATE_11MB:
				brate_cfg |= RATE_11M;
				break;
			case IEEE80211_OFDM_RATE_6MB:
				brate_cfg |= RATE_6M;
				break;
			case IEEE80211_OFDM_RATE_9MB:
				brate_cfg |= RATE_9M;
				break;
			case IEEE80211_OFDM_RATE_12MB:
				brate_cfg |= RATE_12M;
				break;
			case IEEE80211_OFDM_RATE_18MB:
				brate_cfg |= RATE_18M;
				break;
			case IEEE80211_OFDM_RATE_24MB:
				brate_cfg |= RATE_24M;
				break;
			case IEEE80211_OFDM_RATE_36MB:
				brate_cfg |= RATE_36M;
				break;
			case IEEE80211_OFDM_RATE_48MB:
				brate_cfg |= RATE_48M;
				break;
			case IEEE80211_OFDM_RATE_54MB:
				brate_cfg |= RATE_54M;
				break;
			}
		}
	}

	/*  2007.01.16, by Emily */
	/*  Select RRSR (in Legacy-OFDM and CCK) */
	/*  For 8190, we select only 24M, 12M, 6M, 11M, 5.5M, 2M,
	    and 1M from the Basic rate. */
	/*  We do not use other rates. */
	/* 2011.03.30 add by Luke Lee */
	/* CCK 2M ACK should be disabled for some BCM and Atheros AP IOT */
	/* because CCK 2M has poor TXEVM */
	/* CCK 5.5M & 11M ACK should be enabled for better
	   performance */

	brate_cfg = (brate_cfg | 0xd) & 0x15d;
	pHalData->BasicRateSet = brate_cfg;
	brate_cfg |= 0x01;	/*  default enable 1M ACK rate */
	DBG_8723A("HW_VAR_BASIC_RATE: BrateCfg(%#x)\n", brate_cfg);

	/*  Set RRSR rate table. */
	rtl8723au_write8(padapter, REG_RRSR, brate_cfg & 0xff);
	rtl8723au_write8(padapter, REG_RRSR + 1, (brate_cfg >> 8) & 0xff);
	rtl8723au_write8(padapter, REG_RRSR + 2,
			 rtl8723au_read8(padapter, REG_RRSR + 2) & 0xf0);

	rate_index = 0;
	/*  Set RTS initial rate */
	while (brate_cfg > 0x1) {
		brate_cfg >>= 1;
		rate_index++;
	}
		/*  Ziv - Check */
	rtl8723au_write8(padapter, REG_INIRTS_RATE_SEL, rate_index);
}

static void _OneOutPipeMapping(struct rtw_adapter *pAdapter)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(pAdapter);

	pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];	/* VO */
	pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[0];	/* VI */
	pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[0];	/* BE */
	pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[0];	/* BK */

	pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];	/* BCN */
	pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];	/* MGT */
	pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];	/* HIGH */
	pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];	/* TXCMD */
}

static void _TwoOutPipeMapping(struct rtw_adapter *pAdapter, bool bWIFICfg)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(pAdapter);

	if (bWIFICfg) {		/* WMM */
		/*    BK,   BE,   VI,   VO,   BCN,  CMD,  MGT, HIGH, HCCA */
		/*     0,    1,    0,    1,     0,    0,    0,    0,    0 }; */
		/* 0:H, 1:L */
		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[1]; /* VO */
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[0]; /* VI */
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[1]; /* BE */
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[0]; /* BK */

		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0]; /* BCN */
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0]; /* MGT */
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0]; /* HIGH */
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0]; /* TXCMD*/
	} else {		/* typical setting */
		/*    BK,   BE,   VI,   VO,   BCN,  CMD,  MGT, HIGH, HCCA */
		/*     1,    1,    0,    0,     0,    0,    0,    0,    0 }; */
		/* 0:H, 1:L */
		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0]; /* VO */
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[0]; /* VI */
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[1]; /* BE */
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[1]; /* BK */

		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0]; /* BCN */
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0]; /* MGT */
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0]; /* HIGH */
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0]; /* TXCMD*/
	}
}

static void _ThreeOutPipeMapping(struct rtw_adapter *pAdapter, bool bWIFICfg)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(pAdapter);

	if (bWIFICfg) {		/* for WMM */
		/*    BK,   BE,   VI,   VO,   BCN,  CMD,  MGT, HIGH, HCCA */
		/*     1,    2,    1,    0,     0,    0,    0,    0,    0 }; */
		/* 0:H, 1:N, 2:L */
		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0]; /* VO */
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[1]; /* VI */
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[2]; /* BE */
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[1]; /* BK */

		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0]; /* BCN */
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0]; /* MGT */
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0]; /* HIGH */
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0]; /* TXCMD*/
	} else {		/* typical setting */
		/*    BK,   BE,   VI,   VO,   BCN,  CMD,  MGT, HIGH, HCCA */
		/*     2,    2,    1,    0,     0,    0,    0,    0,    0 }; */
		/* 0:H, 1:N, 2:L */
		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0]; /* VO */
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[1]; /* VI */
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[2]; /* BE */
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[2]; /* BK */

		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0]; /* BCN */
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0]; /* MGT */
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0]; /* HIGH */
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0]; /* TXCMD*/
	}
}

bool Hal_MappingOutPipe23a(struct rtw_adapter *pAdapter, u8 NumOutPipe)
{
	struct registry_priv *pregistrypriv = &pAdapter->registrypriv;
	bool bWIFICfg = (pregistrypriv->wifi_spec) ? true : false;
	bool result = true;

	switch (NumOutPipe) {
	case 2:
		_TwoOutPipeMapping(pAdapter, bWIFICfg);
		break;
	case 3:
		_ThreeOutPipeMapping(pAdapter, bWIFICfg);
		break;
	case 1:
		_OneOutPipeMapping(pAdapter);
		break;
	default:
		result = false;
		break;
	}

	return result;
}

/*
* C2H event format:
* Field	 TRIGGER		CONTENT	   CMD_SEQ	CMD_LEN		 CMD_ID
* BITS	 [127:120]	[119:16]      [15:8]		  [7:4]		   [3:0]
*/

void c2h_evt_clear23a(struct rtw_adapter *adapter)
{
	rtl8723au_write8(adapter, REG_C2HEVT_CLEAR, C2H_EVT_HOST_CLOSE);
}

int c2h_evt_read23a(struct rtw_adapter *adapter, u8 *buf)
{
	int ret = _FAIL;
	struct c2h_evt_hdr *c2h_evt;
	int i;
	u8 trigger;

	if (buf == NULL)
		goto exit;

	trigger = rtl8723au_read8(adapter, REG_C2HEVT_CLEAR);

	if (trigger == C2H_EVT_HOST_CLOSE)
		goto exit;	/* Not ready */
	else if (trigger != C2H_EVT_FW_CLOSE)
		goto clear_evt;	/* Not a valid value */

	c2h_evt = (struct c2h_evt_hdr *)buf;

	memset(c2h_evt, 0, 16);

	*buf = rtl8723au_read8(adapter, REG_C2HEVT_MSG_NORMAL);
	*(buf + 1) = rtl8723au_read8(adapter, REG_C2HEVT_MSG_NORMAL + 1);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_info_, "c2h_evt_read23a(): ",
		      &c2h_evt, sizeof(c2h_evt));

	if (0) {
		DBG_8723A("%s id:%u, len:%u, seq:%u, trigger:0x%02x\n",
			  __func__, c2h_evt->id, c2h_evt->plen, c2h_evt->seq,
			  trigger);
	}

	/* Read the content */
	for (i = 0; i < c2h_evt->plen; i++)
		c2h_evt->payload[i] = rtl8723au_read8(adapter,
						REG_C2HEVT_MSG_NORMAL +
						sizeof(*c2h_evt) + i);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_info_,
		      "c2h_evt_read23a(): Command Content:\n", c2h_evt->payload,
		      c2h_evt->plen);

	ret = _SUCCESS;

clear_evt:
	/*
	 * Clear event to notify FW we have read the command.
	 * If this field isn't clear, the FW won't update the
	 * next command message.
	 */
	c2h_evt_clear23a(adapter);
exit:
	return ret;
}

void
rtl8723a_set_ampdu_min_space(struct rtw_adapter *padapter, u8 MinSpacingToSet)
{
	u8 SecMinSpace;

	if (MinSpacingToSet <= 7) {
		switch (padapter->securitypriv.dot11PrivacyAlgrthm) {
		case 0:
		case WLAN_CIPHER_SUITE_CCMP:
			SecMinSpace = 0;
			break;

		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
		case WLAN_CIPHER_SUITE_TKIP:
			SecMinSpace = 6;
			break;
		default:
			SecMinSpace = 7;
			break;
		}

		if (MinSpacingToSet < SecMinSpace)
			MinSpacingToSet = SecMinSpace;

		MinSpacingToSet |=
			rtl8723au_read8(padapter, REG_AMPDU_MIN_SPACE) & 0xf8;
		rtl8723au_write8(padapter, REG_AMPDU_MIN_SPACE,
				 MinSpacingToSet);
	}
}

void rtl8723a_set_ampdu_factor(struct rtw_adapter *padapter, u8 FactorToSet)
{
	u8 RegToSet_Normal[4] = { 0x41, 0xa8, 0x72, 0xb9 };
	u8 MaxAggNum;
	u8 *pRegToSet;
	u8 index = 0;

	pRegToSet = RegToSet_Normal;	/*  0xb972a841; */

	if (rtl8723a_BT_enabled(padapter) &&
	    rtl8723a_BT_using_antenna_1(padapter))
		MaxAggNum = 0x8;
	else
		MaxAggNum = 0xF;

	if (FactorToSet <= 3) {
		FactorToSet = 1 << (FactorToSet + 2);
		if (FactorToSet > MaxAggNum)
			FactorToSet = MaxAggNum;

		for (index = 0; index < 4; index++) {
			if ((pRegToSet[index] & 0xf0) > (FactorToSet << 4))
				pRegToSet[index] = (pRegToSet[index] & 0x0f) |
					(FactorToSet << 4);

			if ((pRegToSet[index] & 0x0f) > FactorToSet)
				pRegToSet[index] = (pRegToSet[index] & 0xf0) |
					FactorToSet;

			rtl8723au_write8(padapter, REG_AGGLEN_LMT + index,
					 pRegToSet[index]);
		}
	}
}

void rtl8723a_set_acm_ctrl(struct rtw_adapter *padapter, u8 ctrl)
{
	u8 hwctrl = 0;

	if (ctrl != 0) {
		hwctrl |= AcmHw_HwEn;

		if (ctrl & BIT(1))	/*  BE */
			hwctrl |= AcmHw_BeqEn;

		if (ctrl & BIT(2))	/*  VI */
			hwctrl |= AcmHw_ViqEn;

		if (ctrl & BIT(3))	/*  VO */
			hwctrl |= AcmHw_VoqEn;
	}

	DBG_8723A("[HW_VAR_ACM_CTRL] Write 0x%02X\n", hwctrl);
	rtl8723au_write8(padapter, REG_ACMHWCTRL, hwctrl);
}

void rtl8723a_set_media_status(struct rtw_adapter *padapter, u8 status)
{
	u8 val8;

	val8 = rtl8723au_read8(padapter, MSR) & 0x0c;
	val8 |= status;
	rtl8723au_write8(padapter, MSR, val8);
}

void rtl8723a_set_media_status1(struct rtw_adapter *padapter, u8 status)
{
	u8 val8;

	val8 = rtl8723au_read8(padapter, MSR) & 0x03;
	val8 |= status << 2;
	rtl8723au_write8(padapter, MSR, val8);
}

void rtl8723a_set_bcn_func(struct rtw_adapter *padapter, u8 val)
{
	if (val)
		SetBcnCtrlReg23a(padapter, EN_BCN_FUNCTION | EN_TXBCN_RPT, 0);
	else
		SetBcnCtrlReg23a(padapter, 0, EN_BCN_FUNCTION | EN_TXBCN_RPT);
}

void rtl8723a_check_bssid(struct rtw_adapter *padapter, u8 val)
{
	u32 val32;

	val32 = rtl8723au_read32(padapter, REG_RCR);
	if (val)
		val32 |= RCR_CBSSID_DATA | RCR_CBSSID_BCN;
	else
		val32 &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN);
	rtl8723au_write32(padapter, REG_RCR, val32);
}

void rtl8723a_mlme_sitesurvey(struct rtw_adapter *padapter, u8 flag)
{
	if (flag) {	/* under sitesurvey */
		u32 v32;

		/*  config RCR to receive different BSSID & not
		    to receive data frame */
		v32 = rtl8723au_read32(padapter, REG_RCR);
		v32 &= ~(RCR_CBSSID_BCN);
		rtl8723au_write32(padapter, REG_RCR, v32);
		/*  reject all data frame */
		rtl8723au_write16(padapter, REG_RXFLTMAP2, 0);

		/*  disable update TSF */
		SetBcnCtrlReg23a(padapter, DIS_TSF_UDT, 0);
	} else {	/* sitesurvey done */

		struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
		struct mlme_ext_info *pmlmeinfo;
		u32 v32;

		pmlmeinfo = &pmlmeext->mlmext_info;

		if ((is_client_associated_to_ap23a(padapter) == true) ||
		    ((pmlmeinfo->state & 0x03) == MSR_ADHOC) ||
		    ((pmlmeinfo->state & 0x03) == MSR_AP)) {
			/*  enable to rx data frame */
			rtl8723au_write16(padapter, REG_RXFLTMAP2, 0xFFFF);

			/*  enable update TSF */
			SetBcnCtrlReg23a(padapter, 0, DIS_TSF_UDT);
		}

		v32 = rtl8723au_read32(padapter, REG_RCR);
		v32 |= RCR_CBSSID_BCN;
		rtl8723au_write32(padapter, REG_RCR, v32);
	}

	rtl8723a_BT_wifiscan_notify(padapter, flag ? true : false);
}

void rtl8723a_on_rcr_am(struct rtw_adapter *padapter)
{
	rtl8723au_write32(padapter, REG_RCR,
		    rtl8723au_read32(padapter, REG_RCR) | RCR_AM);
	DBG_8723A("%s, %d, RCR = %x\n", __func__, __LINE__,
		  rtl8723au_read32(padapter, REG_RCR));
}

void rtl8723a_off_rcr_am(struct rtw_adapter *padapter)
{
	rtl8723au_write32(padapter, REG_RCR,
		    rtl8723au_read32(padapter, REG_RCR) & (~RCR_AM));
	DBG_8723A("%s, %d, RCR = %x\n", __func__, __LINE__,
		  rtl8723au_read32(padapter, REG_RCR));
}

void rtl8723a_set_slot_time(struct rtw_adapter *padapter, u8 slottime)
{
	u8 u1bAIFS, aSifsTime;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	rtl8723au_write8(padapter, REG_SLOT, slottime);

	if (pmlmeinfo->WMM_enable == 0) {
		if (pmlmeext->cur_wireless_mode == WIRELESS_11B)
			aSifsTime = 10;
		else
			aSifsTime = 16;

		u1bAIFS = aSifsTime + (2 * pmlmeinfo->slotTime);

		/*  <Roger_EXP> Temporary removed, 2008.06.20. */
		rtl8723au_write8(padapter, REG_EDCA_VO_PARAM, u1bAIFS);
		rtl8723au_write8(padapter, REG_EDCA_VI_PARAM, u1bAIFS);
		rtl8723au_write8(padapter, REG_EDCA_BE_PARAM, u1bAIFS);
		rtl8723au_write8(padapter, REG_EDCA_BK_PARAM, u1bAIFS);
	}
}

void rtl8723a_ack_preamble(struct rtw_adapter *padapter, u8 bShortPreamble)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u8 regTmp;

	/*  Joseph marked out for Netgear 3500 TKIP
	    channel 7 issue.(Temporarily) */
	regTmp = (pHalData->nCur40MhzPrimeSC) << 5;
	/* regTmp = 0; */
	if (bShortPreamble)
		regTmp |= 0x80;
	rtl8723au_write8(padapter, REG_RRSR + 2, regTmp);
}

void rtl8723a_set_sec_cfg(struct rtw_adapter *padapter, u8 sec)
{
	rtl8723au_write8(padapter, REG_SECCFG, sec);
}

void rtl8723a_cam_empty_entry(struct rtw_adapter *padapter, u8 ucIndex)
{
	u8 i;
	u32 ulCommand = 0;
	u32 ulContent = 0;
	u32 ulEncAlgo = CAM_AES;

	for (i = 0; i < CAM_CONTENT_COUNT; i++) {
		/*  filled id in CAM config 2 byte */
		if (i == 0) {
			ulContent |= (ucIndex & 0x03) |
				((u16) (ulEncAlgo) << 2);
			/* ulContent |= CAM_VALID; */
		} else {
			ulContent = 0;
		}
		/*  polling bit, and No Write enable, and address */
		ulCommand = CAM_CONTENT_COUNT * ucIndex + i;
		ulCommand = ulCommand | CAM_POLLINIG | CAM_WRITE;
		/*  write content 0 is equall to mark invalid */
		/* delay_ms(40); */
		rtl8723au_write32(padapter, WCAMI, ulContent);
		/* delay_ms(40); */
		rtl8723au_write32(padapter, REG_CAMCMD, ulCommand);
	}
}

void rtl8723a_cam_invalidate_all(struct rtw_adapter *padapter)
{
	rtl8723au_write32(padapter, REG_CAMCMD, CAM_POLLINIG | BIT(30));
}

void rtl8723a_cam_write(struct rtw_adapter *padapter,
			u8 entry, u16 ctrl, const u8 *mac, const u8 *key)
{
	u32 cmd;
	unsigned int i, val, addr;
	int j;

	addr = entry << 3;

	for (j = 5; j >= 0; j--) {
		switch (j) {
		case 0:
			val = ctrl | (mac[0] << 16) | (mac[1] << 24);
			break;
		case 1:
			val = mac[2] | (mac[3] << 8) |
				(mac[4] << 16) | (mac[5] << 24);
			break;
		default:
			i = (j - 2) << 2;
			val = key[i] | (key[i+1] << 8) |
				(key[i+2] << 16) | (key[i+3] << 24);
			break;
		}

		rtl8723au_write32(padapter, WCAMI, val);
		cmd = CAM_POLLINIG | CAM_WRITE | (addr + j);
		rtl8723au_write32(padapter, REG_CAMCMD, cmd);

		/* DBG_8723A("%s => cam write: %x, %x\n", __func__, cmd, val);*/
	}
}

void rtl8723a_fifo_cleanup(struct rtw_adapter *padapter)
{
#define RW_RELEASE_EN		BIT(18)
#define RXDMA_IDLE		BIT(17)

	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	u8 trycnt = 100;

	/*  pause tx */
	rtl8723au_write8(padapter, REG_TXPAUSE, 0xff);

	/*  keep sn */
	padapter->xmitpriv.nqos_ssn = rtl8723au_read8(padapter, REG_NQOS_SEQ);

	if (pwrpriv->bkeepfwalive != true) {
		u32 v32;

		/*  RX DMA stop */
		v32 = rtl8723au_read32(padapter, REG_RXPKT_NUM);
		v32 |= RW_RELEASE_EN;
		rtl8723au_write32(padapter, REG_RXPKT_NUM, v32);
		do {
			v32 = rtl8723au_read32(padapter,
					       REG_RXPKT_NUM) & RXDMA_IDLE;
			if (!v32)
				break;
		} while (trycnt--);
		if (trycnt == 0)
			DBG_8723A("Stop RX DMA failed......\n");

		/*  RQPN Load 0 */
		rtl8723au_write16(padapter, REG_RQPN_NPQ, 0);
		rtl8723au_write32(padapter, REG_RQPN, 0x80000000);
		mdelay(10);
	}
}

void rtl8723a_bcn_valid(struct rtw_adapter *padapter)
{
	/* BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2,
	   write 1 to clear, Clear by sw */
	rtl8723au_write8(padapter, REG_TDECTRL + 2,
			 rtl8723au_read8(padapter, REG_TDECTRL + 2) | BIT(0));
}

bool rtl8723a_get_bcn_valid(struct rtw_adapter *padapter)
{
	bool retval;

	retval = (rtl8723au_read8(padapter, REG_TDECTRL + 2) & BIT(0)) ? true : false;

	return retval;
}

void rtl8723a_set_beacon_interval(struct rtw_adapter *padapter, u16 interval)
{
	rtl8723au_write16(padapter, REG_BCN_INTERVAL, interval);
}

void rtl8723a_set_resp_sifs(struct rtw_adapter *padapter,
			    u8 r2t1, u8 r2t2, u8 t2t1, u8 t2t2)
{
	/* SIFS_Timer = 0x0a0a0808; */
	/* RESP_SIFS for CCK */
	/*  SIFS_T2T_CCK (0x08) */
	rtl8723au_write8(padapter, REG_R2T_SIFS, r2t1);
	/* SIFS_R2T_CCK(0x08) */
	rtl8723au_write8(padapter, REG_R2T_SIFS + 1, r2t2);
	/* RESP_SIFS for OFDM */
	/* SIFS_T2T_OFDM (0x0a) */
	rtl8723au_write8(padapter, REG_T2T_SIFS, t2t1);
	/* SIFS_R2T_OFDM(0x0a) */
	rtl8723au_write8(padapter, REG_T2T_SIFS + 1, t2t2);
}

void rtl8723a_set_ac_param_vo(struct rtw_adapter *padapter, u32 vo)
{
	rtl8723au_write32(padapter, REG_EDCA_VO_PARAM, vo);
}

void rtl8723a_set_ac_param_vi(struct rtw_adapter *padapter, u32 vi)
{
	rtl8723au_write32(padapter, REG_EDCA_VI_PARAM, vi);
}

void rtl8723a_set_ac_param_be(struct rtw_adapter *padapter, u32 be)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	pHalData->AcParam_BE = be;
	rtl8723au_write32(padapter, REG_EDCA_BE_PARAM, be);
}

void rtl8723a_set_ac_param_bk(struct rtw_adapter *padapter, u32 bk)
{
	rtl8723au_write32(padapter, REG_EDCA_BK_PARAM, bk);
}

void rtl8723a_set_rxdma_agg_pg_th(struct rtw_adapter *padapter, u8 val)
{
	rtl8723au_write8(padapter, REG_RXDMA_AGG_PG_TH, val);
}

void rtl8723a_set_initial_gain(struct rtw_adapter *padapter, u32 rx_gain)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct dig_t *pDigTable = &pHalData->odmpriv.DM_DigTable;

	if (rx_gain == 0xff)	/* restore rx gain */
		ODM_Write_DIG23a(&pHalData->odmpriv, pDigTable->BackupIGValue);
	else {
		pDigTable->BackupIGValue = pDigTable->CurIGValue;
		ODM_Write_DIG23a(&pHalData->odmpriv, rx_gain);
	}
}

void rtl8723a_odm_support_ability_restore(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	pHalData->odmpriv.SupportAbility = pHalData->odmpriv.BK_SupportAbility;
}

void rtl8723a_odm_support_ability_backup(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	pHalData->odmpriv.BK_SupportAbility = pHalData->odmpriv.SupportAbility;
}

void rtl8723a_odm_support_ability_set(struct rtw_adapter *padapter, u32 val)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	if (val == DYNAMIC_ALL_FUNC_ENABLE)
		pHalData->odmpriv.SupportAbility = pHalData->dmpriv.InitODMFlag;
	else
		pHalData->odmpriv.SupportAbility |= val;
}

void rtl8723a_odm_support_ability_clr(struct rtw_adapter *padapter, u32 val)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	pHalData->odmpriv.SupportAbility &= val;
}

void rtl8723a_set_rpwm(struct rtw_adapter *padapter, u8 val)
{
	rtl8723au_write8(padapter, REG_USB_HRPWM, val);
}

u8 rtl8723a_get_rf_type(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	return pHalData->rf_type;
}

bool rtl8723a_get_fwlps_rf_on(struct rtw_adapter *padapter)
{
	bool retval;
	u32 valRCR;

	/*  When we halt NIC, we should check if FW LPS is leave. */

	if ((padapter->bSurpriseRemoved == true) ||
	    (padapter->pwrctrlpriv.rf_pwrstate == rf_off)) {
		/*  If it is in HW/SW Radio OFF or IPS state, we do
		    not check Fw LPS Leave, because Fw is unload. */
		retval = true;
	} else {
		valRCR = rtl8723au_read32(padapter, REG_RCR);
		if (valRCR & 0x00070000)
			retval = false;
		else
			retval = true;
	}

	return retval;
}

bool rtl8723a_chk_hi_queue_empty(struct rtw_adapter *padapter)
{
	u32 hgq;

	hgq = rtl8723au_read32(padapter, REG_HGQ_INFORMATION);

	return ((hgq & 0x0000ff00) == 0) ? true : false;
}
