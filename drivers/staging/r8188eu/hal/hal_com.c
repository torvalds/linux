// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#include "../include/osdep_service.h"
#include "../include/drv_types.h"

#include "../include/hal_intf.h"
#include "../include/hal_com.h"
#include "../include/rtl8188e_hal.h"

#define _HAL_INIT_C_

void dump_chip_info(struct HAL_VERSION	chip_vers)
{
	uint cnt = 0;
	char buf[128];

	cnt += sprintf((buf + cnt), "Chip Version Info: CHIP_8188E_");
	cnt += sprintf((buf + cnt), "%s_", IS_NORMAL_CHIP(chip_vers) ?
		       "Normal_Chip" : "Test_Chip");
	cnt += sprintf((buf + cnt), "%s_", IS_CHIP_VENDOR_TSMC(chip_vers) ?
		       "TSMC" : "UMC");

	switch (chip_vers.CUTVersion) {
	case A_CUT_VERSION:
		cnt += sprintf((buf + cnt), "A_CUT_");
		break;
	case B_CUT_VERSION:
		cnt += sprintf((buf + cnt), "B_CUT_");
		break;
	case C_CUT_VERSION:
		cnt += sprintf((buf + cnt), "C_CUT_");
		break;
	case D_CUT_VERSION:
		cnt += sprintf((buf + cnt), "D_CUT_");
		break;
	case E_CUT_VERSION:
		cnt += sprintf((buf + cnt), "E_CUT_");
		break;
	default:
		cnt += sprintf((buf + cnt), "UNKNOWN_CUT(%d)_", chip_vers.CUTVersion);
		break;
	}

	cnt += sprintf((buf + cnt), "1T1R_");

	cnt += sprintf((buf + cnt), "RomVer(%d)\n", 0);

	pr_info("%s", buf);
}

#define	CHAN_PLAN_HW	0x80

u8 /* return the final channel plan decision */
hal_com_get_channel_plan(struct adapter *padapter, u8 hw_channel_plan,
			 u8 sw_channel_plan, u8 def_channel_plan,
			 bool load_fail)
{
	u8 sw_cfg;
	u8 chnlplan;

	sw_cfg = true;
	if (!load_fail) {
		if (!rtw_is_channel_plan_valid(sw_channel_plan))
			sw_cfg = false;
		if (hw_channel_plan & CHAN_PLAN_HW)
			sw_cfg = false;
	}

	if (sw_cfg)
		chnlplan = sw_channel_plan;
	else
		chnlplan = hw_channel_plan & (~CHAN_PLAN_HW);

	if (!rtw_is_channel_plan_valid(chnlplan))
		chnlplan = def_channel_plan;

	return chnlplan;
}

u8 MRateToHwRate(u8 rate)
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
	default:
		break;
	}
	return ret;
}

void HalSetBrateCfg(struct adapter *adapt, u8 *brates, u16 *rate_cfg)
{
	u8 i, is_brate, brate;

	for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {
		is_brate = brates[i] & IEEE80211_BASIC_RATE_MASK;
		brate = brates[i] & 0x7f;

		if (is_brate) {
			switch (brate) {
			case IEEE80211_CCK_RATE_1MB:
				*rate_cfg |= RATE_1M;
				break;
			case IEEE80211_CCK_RATE_2MB:
				*rate_cfg |= RATE_2M;
				break;
			case IEEE80211_CCK_RATE_5MB:
				*rate_cfg |= RATE_5_5M;
				break;
			case IEEE80211_CCK_RATE_11MB:
				*rate_cfg |= RATE_11M;
				break;
			case IEEE80211_OFDM_RATE_6MB:
				*rate_cfg |= RATE_6M;
				break;
			case IEEE80211_OFDM_RATE_9MB:
				*rate_cfg |= RATE_9M;
				break;
			case IEEE80211_OFDM_RATE_12MB:
				*rate_cfg |= RATE_12M;
				break;
			case IEEE80211_OFDM_RATE_18MB:
				*rate_cfg |= RATE_18M;
				break;
			case IEEE80211_OFDM_RATE_24MB:
				*rate_cfg |= RATE_24M;
				break;
			case IEEE80211_OFDM_RATE_36MB:
				*rate_cfg |= RATE_36M;
				break;
			case IEEE80211_OFDM_RATE_48MB:
				*rate_cfg |= RATE_48M;
				break;
			case IEEE80211_OFDM_RATE_54MB:
				*rate_cfg |= RATE_54M;
				break;
			}
		}
	}
}

static void one_out_pipe(struct adapter *adapter)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(adapter);

	pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];/* VO */
	pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[0];/* VI */
	pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[0];/* BE */
	pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[0];/* BK */

	pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];/* BCN */
	pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];/* MGT */
	pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];/* HIGH */
	pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];/* TXCMD */
}

static void two_out_pipe(struct adapter *adapter, bool wifi_cfg)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(adapter);

	if (wifi_cfg) { /* WMM */
		/* BK, BE, VI, VO, BCN,	CMD, MGT, HIGH, HCCA */
		/*  0,  1,  0,  1,   0,   0,   0,    0,    0}; */
		/* 0:H, 1:L */

		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[1];/* VO */
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[0];/* VI */
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[1];/* BE */
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[0];/* BK */

		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];/* BCN */
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];/* MGT */
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];/* HIGH */
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];/* TXCMD */

	} else {/* typical setting */
		/* BK, BE, VI, VO, BCN,	CMD, MGT, HIGH, HCCA */
		/*  1,	1,  0,  0,   0,   0,   0,    0,    0}; */
		/* 0:H, 1:L */

		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];/* VO */
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[0];/* VI */
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[1];/* BE */
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[1];/* BK */

		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];/* BCN */
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];/* MGT */
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];/* HIGH */
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];/* TXCMD */
	}
}

static void three_out_pipe(struct adapter *adapter, bool wifi_cfg)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(adapter);

	if (wifi_cfg) {/* for WMM */
		/* BK, BE, VI, VO, BCN,	CMD, MGT, HIGH, HCCA */
		/*  1,	2,  1,  0,   0,   0,   0,    0,    0}; */
		/* 0:H, 1:N, 2:L */

		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];/* VO */
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[1];/* VI */
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[2];/* BE */
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[1];/* BK */

		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];/* BCN */
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];/* MGT */
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];/* HIGH */
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];/* TXCMD */

	} else {/* typical setting */
		/* BK, BE, VI, VO, BCN,	CMD, MGT, HIGH, HCCA */
		/*  2,  2,  1,  0,   0,   0,   0,    0,    0}; */
		/* 0:H, 1:N, 2:L */

		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];/* VO */
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[1];/* VI */
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[2];/* BE */
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[2];/* BK */

		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];/* BCN */
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];/* MGT */
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];/* HIGH */
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];/* TXCMD */
	}
}

bool Hal_MappingOutPipe(struct adapter *adapter, u8 numoutpipe)
{
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	bool wifi_cfg = pregistrypriv->wifi_spec;
	bool result = true;

	switch (numoutpipe) {
	case 2:
		two_out_pipe(adapter, wifi_cfg);
		break;
	case 3:
		three_out_pipe(adapter, wifi_cfg);
		break;
	case 1:
		one_out_pipe(adapter);
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

s32 c2h_evt_read(struct adapter *adapter, u8 *buf)
{
	s32 ret = _FAIL;
	struct c2h_evt_hdr *c2h_evt;
	int i;
	u8 trigger;

	if (!buf)
		goto exit;

	ret = rtw_read8(adapter, REG_C2HEVT_CLEAR, &trigger);
	if (ret)
		return _FAIL;

	if (trigger == C2H_EVT_HOST_CLOSE)
		goto exit; /* Not ready */
	else if (trigger != C2H_EVT_FW_CLOSE)
		goto clear_evt; /* Not a valid value */

	c2h_evt = (struct c2h_evt_hdr *)buf;

	memset(c2h_evt, 0, 16);

	ret = rtw_read8(adapter, REG_C2HEVT_MSG_NORMAL, buf);
	if (ret) {
		ret = _FAIL;
		goto clear_evt;
	}

	ret = rtw_read8(adapter, REG_C2HEVT_MSG_NORMAL + 1, buf + 1);
	if (ret) {
		ret = _FAIL;
		goto clear_evt;
	}
	/* Read the content */
	for (i = 0; i < c2h_evt->plen; i++) {
		ret = rtw_read8(adapter, REG_C2HEVT_MSG_NORMAL +
						sizeof(*c2h_evt) + i, c2h_evt->payload + i);
		if (ret) {
			ret = _FAIL;
			goto clear_evt;
		}
	}

	ret = _SUCCESS;

clear_evt:
	/*
	* Clear event to notify FW we have read the command.
	* If this field isn't clear, the FW won't update the next
	* command message.
	*/
	rtw_write8(adapter, REG_C2HEVT_CLEAR, C2H_EVT_HOST_CLOSE);
exit:
	return ret;
}
