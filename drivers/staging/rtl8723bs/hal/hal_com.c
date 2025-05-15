// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include <linux/kernel.h>
#include <drv_types.h>
#include "hal_com_h2c.h"

#include "odm_precomp.h"

u8 rtw_hal_data_init(struct adapter *padapter)
{
	padapter->hal_data_sz = sizeof(struct hal_com_data);
	padapter->HalData = vzalloc(padapter->hal_data_sz);
	if (!padapter->HalData)
		return _FAIL;

	return _SUCCESS;
}

void rtw_hal_data_deinit(struct adapter *padapter)
{
	if (padapter->HalData) {
		vfree(padapter->HalData);
		padapter->HalData = NULL;
		padapter->hal_data_sz = 0;
	}
}


void dump_chip_info(struct hal_version	ChipVersion)
{
	char buf[128];
	size_t cnt = 0;

	cnt += scnprintf(buf + cnt, sizeof(buf) - cnt, "Chip Version Info: CHIP_8723B_%s_",
			IS_NORMAL_CHIP(ChipVersion) ? "Normal_Chip" : "Test_Chip");

	if (IS_CHIP_VENDOR_TSMC(ChipVersion))
		cnt += scnprintf(buf + cnt, sizeof(buf) - cnt, "TSMC_");
	else if (IS_CHIP_VENDOR_UMC(ChipVersion))
		cnt += scnprintf(buf + cnt, sizeof(buf) - cnt, "UMC_");
	else if (IS_CHIP_VENDOR_SMIC(ChipVersion))
		cnt += scnprintf(buf + cnt, sizeof(buf) - cnt, "SMIC_");

	if (IS_A_CUT(ChipVersion))
		cnt += scnprintf(buf + cnt, sizeof(buf) - cnt, "A_CUT_");
	else if (IS_B_CUT(ChipVersion))
		cnt += scnprintf(buf + cnt, sizeof(buf) - cnt, "B_CUT_");
	else if (IS_C_CUT(ChipVersion))
		cnt += scnprintf(buf + cnt, sizeof(buf) - cnt, "C_CUT_");
	else if (IS_D_CUT(ChipVersion))
		cnt += scnprintf(buf + cnt, sizeof(buf) - cnt, "D_CUT_");
	else if (IS_E_CUT(ChipVersion))
		cnt += scnprintf(buf + cnt, sizeof(buf) - cnt, "E_CUT_");
	else if (IS_I_CUT(ChipVersion))
		cnt += scnprintf(buf + cnt, sizeof(buf) - cnt, "I_CUT_");
	else if (IS_J_CUT(ChipVersion))
		cnt += scnprintf(buf + cnt, sizeof(buf) - cnt, "J_CUT_");
	else if (IS_K_CUT(ChipVersion))
		cnt += scnprintf(buf + cnt, sizeof(buf) - cnt, "K_CUT_");
	else
		cnt += scnprintf(buf + cnt, sizeof(buf) - cnt,
				"UNKNOWN_CUT(%d)_", ChipVersion.CUTVersion);

	cnt += scnprintf(buf + cnt, sizeof(buf) - cnt, "1T1R_");

	cnt += scnprintf(buf + cnt, sizeof(buf) - cnt, "RomVer(%d)\n", ChipVersion.ROMVer);
}


#define	EEPROM_CHANNEL_PLAN_BY_HW_MASK	0x80

/*
 * Description:
 *Use hardware(efuse), driver parameter(registry) and default channel plan
 *to decide which one should be used.
 *
 * Parameters:
 *padapter			pointer of adapter
 *hw_channel_plan		channel plan from HW (efuse/eeprom)
 *					BIT[7] software configure mode; 0:Enable, 1:disable
 *					BIT[6:0] Channel Plan
 *sw_channel_plan		channel plan from SW (registry/module param)
 *def_channel_plan	channel plan used when HW/SW both invalid
 *AutoLoadFail		efuse autoload fail or not
 *
 * Return:
 *Final channel plan decision
 *
 */
u8 hal_com_config_channel_plan(
	struct adapter *padapter,
	u8 hw_channel_plan,
	u8 sw_channel_plan,
	u8 def_channel_plan,
	bool AutoLoadFail
)
{
	struct hal_com_data *pHalData;
	u8 chnlPlan;

	pHalData = GET_HAL_DATA(padapter);
	pHalData->bDisableSWChannelPlan = false;
	chnlPlan = def_channel_plan;

	if (0xFF == hw_channel_plan)
		AutoLoadFail = true;

	if (!AutoLoadFail) {
		u8 hw_chnlPlan;

		hw_chnlPlan = hw_channel_plan & (~EEPROM_CHANNEL_PLAN_BY_HW_MASK);
		if (rtw_is_channel_plan_valid(hw_chnlPlan)) {
			if (hw_channel_plan & EEPROM_CHANNEL_PLAN_BY_HW_MASK)
				pHalData->bDisableSWChannelPlan = true;

			chnlPlan = hw_chnlPlan;
		}
	}

	if (
		(false == pHalData->bDisableSWChannelPlan) &&
		rtw_is_channel_plan_valid(sw_channel_plan)
	)
		chnlPlan = sw_channel_plan;

	return chnlPlan;
}

bool HAL_IsLegalChannel(struct adapter *adapter, u32 Channel)
{
	bool bLegalChannel = true;

	if ((Channel <= 14) && (Channel >= 1)) {
		if (is_supported_24g(adapter->registrypriv.wireless_mode) == false)
			bLegalChannel = false;
	} else {
		bLegalChannel = false;
	}

	return bLegalChannel;
}

u8 MRateToHwRate(u8 rate)
{
	u8 ret = DESC_RATE1M;

	switch (rate) {
	case MGN_1M:
		ret = DESC_RATE1M;
		break;
	case MGN_2M:
		ret = DESC_RATE2M;
		break;
	case MGN_5_5M:
		ret = DESC_RATE5_5M;
		break;
	case MGN_11M:
		ret = DESC_RATE11M;
		break;
	case MGN_6M:
		ret = DESC_RATE6M;
		break;
	case MGN_9M:
		ret = DESC_RATE9M;
		break;
	case MGN_12M:
		ret = DESC_RATE12M;
		break;
	case MGN_18M:
		ret = DESC_RATE18M;
		break;
	case MGN_24M:
		ret = DESC_RATE24M;
		break;
	case MGN_36M:
		ret = DESC_RATE36M;
		break;
	case MGN_48M:
		ret = DESC_RATE48M;
		break;
	case MGN_54M:
		ret = DESC_RATE54M;
		break;
	case MGN_MCS0:
		ret = DESC_RATEMCS0;
		break;
	case MGN_MCS1:
		ret = DESC_RATEMCS1;
		break;
	case MGN_MCS2:
		ret = DESC_RATEMCS2;
		break;
	case MGN_MCS3:
		ret = DESC_RATEMCS3;
		break;
	case MGN_MCS4:
		ret = DESC_RATEMCS4;
		break;
	case MGN_MCS5:
		ret = DESC_RATEMCS5;
		break;
	case MGN_MCS6:
		ret = DESC_RATEMCS6;
		break;
	case MGN_MCS7:
		ret = DESC_RATEMCS7;
		break;
	default:
		break;
	}

	return ret;
}

u8 HwRateToMRate(u8 rate)
{
	u8 ret_rate = MGN_1M;

	switch (rate) {
	case DESC_RATE1M:
		ret_rate = MGN_1M;
		break;
	case DESC_RATE2M:
		ret_rate = MGN_2M;
		break;
	case DESC_RATE5_5M:
		ret_rate = MGN_5_5M;
		break;
	case DESC_RATE11M:
		ret_rate = MGN_11M;
		break;
	case DESC_RATE6M:
		ret_rate = MGN_6M;
		break;
	case DESC_RATE9M:
		ret_rate = MGN_9M;
		break;
	case DESC_RATE12M:
		ret_rate = MGN_12M;
		break;
	case DESC_RATE18M:
		ret_rate = MGN_18M;
		break;
	case DESC_RATE24M:
		ret_rate = MGN_24M;
		break;
	case DESC_RATE36M:
		ret_rate = MGN_36M;
		break;
	case DESC_RATE48M:
		ret_rate = MGN_48M;
		break;
	case DESC_RATE54M:
		ret_rate = MGN_54M;
		break;
	case DESC_RATEMCS0:
		ret_rate = MGN_MCS0;
		break;
	case DESC_RATEMCS1:
		ret_rate = MGN_MCS1;
		break;
	case DESC_RATEMCS2:
		ret_rate = MGN_MCS2;
		break;
	case DESC_RATEMCS3:
		ret_rate = MGN_MCS3;
		break;
	case DESC_RATEMCS4:
		ret_rate = MGN_MCS4;
		break;
	case DESC_RATEMCS5:
		ret_rate = MGN_MCS5;
		break;
	case DESC_RATEMCS6:
		ret_rate = MGN_MCS6;
		break;
	case DESC_RATEMCS7:
		ret_rate = MGN_MCS7;
		break;
	default:
		break;
	}

	return ret_rate;
}

void HalSetBrateCfg(struct adapter *Adapter, u8 *mBratesOS, u16 *pBrateCfg)
{
	u8 i, is_brate, brate;

	for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {

		is_brate = mBratesOS[i] & IEEE80211_BASIC_RATE_MASK;
		brate = mBratesOS[i] & 0x7f;

		if (is_brate) {
			switch (brate) {
			case IEEE80211_CCK_RATE_1MB:
				*pBrateCfg |= RATE_1M;
				break;
			case IEEE80211_CCK_RATE_2MB:
				*pBrateCfg |= RATE_2M;
				break;
			case IEEE80211_CCK_RATE_5MB:
				*pBrateCfg |= RATE_5_5M;
				break;
			case IEEE80211_CCK_RATE_11MB:
				*pBrateCfg |= RATE_11M;
				break;
			case IEEE80211_OFDM_RATE_6MB:
				*pBrateCfg |= RATE_6M;
				break;
			case IEEE80211_OFDM_RATE_9MB:
				*pBrateCfg |= RATE_9M;
				break;
			case IEEE80211_OFDM_RATE_12MB:
				*pBrateCfg |= RATE_12M;
				break;
			case IEEE80211_OFDM_RATE_18MB:
				*pBrateCfg |= RATE_18M;
				break;
			case IEEE80211_OFDM_RATE_24MB:
				*pBrateCfg |= RATE_24M;
				break;
			case IEEE80211_OFDM_RATE_36MB:
				*pBrateCfg |= RATE_36M;
				break;
			case IEEE80211_OFDM_RATE_48MB:
				*pBrateCfg |= RATE_48M;
				break;
			case IEEE80211_OFDM_RATE_54MB:
				*pBrateCfg |= RATE_54M;
				break;
			}
		}
	}
}

static void _OneOutPipeMapping(struct adapter *padapter)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);

	pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];/* VO */
	pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[0];/* VI */
	pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[0];/* BE */
	pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[0];/* BK */

	pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];/* BCN */
	pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];/* MGT */
	pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];/* HIGH */
	pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];/* TXCMD */
}

static void _TwoOutPipeMapping(struct adapter *padapter, bool bWIFICfg)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);

	if (bWIFICfg) { /* WMM */

		/* 	BK,	BE,	VI,	VO,	BCN,	CMD, MGT, HIGH, HCCA */
		/*   0,		1,	0,	1,	0,	0,	0,	0,		0	}; */
		/* 0:ep_0 num, 1:ep_1 num */

		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[1];/* VO */
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[0];/* VI */
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[1];/* BE */
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[0];/* BK */

		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];/* BCN */
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];/* MGT */
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];/* HIGH */
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];/* TXCMD */

	} else { /* typical setting */


		/* BK,	BE,	VI,	VO,	BCN,	CMD, MGT, HIGH, HCCA */
		/*   1,		1,	0,	0,	0,	0,	0,	0,		0	}; */
		/* 0:ep_0 num, 1:ep_1 num */

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

static void _ThreeOutPipeMapping(struct adapter *padapter, bool bWIFICfg)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);

	if (bWIFICfg) { /* for WMM */

		/* 	BK,	BE,	VI,	VO,	BCN,	CMD, MGT, HIGH, HCCA */
		/*   1,		2,	1,	0,	0,	0,	0,	0,		0	}; */
		/* 0:H, 1:N, 2:L */

		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];/* VO */
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[1];/* VI */
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[2];/* BE */
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[1];/* BK */

		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];/* BCN */
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];/* MGT */
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];/* HIGH */
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];/* TXCMD */

	} else { /* typical setting */


		/* 	BK,	BE,	VI,	VO,	BCN,	CMD, MGT, HIGH, HCCA */
		/*   2,		2,	1,	0,	0,	0,	0,	0,		0	}; */
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

bool Hal_MappingOutPipe(struct adapter *padapter, u8 NumOutPipe)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;

	bool bWIFICfg = (pregistrypriv->wifi_spec) ? true : false;

	bool result = true;

	switch (NumOutPipe) {
	case 2:
		_TwoOutPipeMapping(padapter, bWIFICfg);
		break;
	case 3:
	case 4:
		_ThreeOutPipeMapping(padapter, bWIFICfg);
		break;
	case 1:
		_OneOutPipeMapping(padapter);
		break;
	default:
		result = false;
		break;
	}

	return result;

}

void hal_init_macaddr(struct adapter *adapter)
{
	rtw_hal_set_hwreg(adapter, HW_VAR_MAC_ADDR, adapter->eeprompriv.mac_addr);
}

void rtw_init_hal_com_default_value(struct adapter *Adapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);

	pHalData->AntDetection = 1;
}

/*
* C2H event format:
* Field	 TRIGGER		CONTENT	   CMD_SEQ	CMD_LEN		 CMD_ID
* BITS	 [127:120]	[119:16]      [15:8]		  [7:4]		   [3:0]
*/

void c2h_evt_clear(struct adapter *adapter)
{
	rtw_write8(adapter, REG_C2HEVT_CLEAR, C2H_EVT_HOST_CLOSE);
}

/*
* C2H event format:
* Field    TRIGGER    CMD_LEN    CONTENT    CMD_SEQ    CMD_ID
* BITS    [127:120]   [119:112]    [111:16]	     [15:8]         [7:0]
*/
s32 c2h_evt_read_88xx(struct adapter *adapter, u8 *buf)
{
	s32 ret = _FAIL;
	struct c2h_evt_hdr_88xx *c2h_evt;
	int i;
	u8 trigger;

	if (!buf)
		goto exit;

	trigger = rtw_read8(adapter, REG_C2HEVT_CLEAR);

	if (trigger == C2H_EVT_HOST_CLOSE)
		goto exit; /* Not ready */
	else if (trigger != C2H_EVT_FW_CLOSE)
		goto clear_evt; /* Not a valid value */

	c2h_evt = (struct c2h_evt_hdr_88xx *)buf;

	memset(c2h_evt, 0, 16);

	c2h_evt->id = rtw_read8(adapter, REG_C2HEVT_MSG_NORMAL);
	c2h_evt->seq = rtw_read8(adapter, REG_C2HEVT_CMD_SEQ_88XX);
	c2h_evt->plen = rtw_read8(adapter, REG_C2HEVT_CMD_LEN_88XX);

	/* Read the content */
	for (i = 0; i < c2h_evt->plen; i++)
		c2h_evt->payload[i] = rtw_read8(adapter, REG_C2HEVT_MSG_NORMAL + 2 + i);

	ret = _SUCCESS;

clear_evt:
	/*
	* Clear event to notify FW we have read the command.
	* If this field isn't clear, the FW won't update the next command message.
	*/
	c2h_evt_clear(adapter);
exit:
	return ret;
}

u8 rtw_get_mgntframe_raid(struct adapter *adapter, unsigned char network_type)
{
	return (network_type & WIRELESS_11B) ? RATEID_IDX_B : RATEID_IDX_G;
}

void rtw_hal_update_sta_rate_mask(struct adapter *padapter, struct sta_info *psta)
{
	u8 i, limit;
	u32 tx_ra_bitmap;

	if (!psta)
		return;

	tx_ra_bitmap = 0;

	/* b/g mode ra_bitmap */
	for (i = 0; i < sizeof(psta->bssrateset); i++) {
		if (psta->bssrateset[i])
			tx_ra_bitmap |= rtw_get_bit_value_from_ieee_value(psta->bssrateset[i]&0x7f);
	}

	/* n mode ra_bitmap */
	if (psta->htpriv.ht_option) {
		limit = 8; /*   1R */

		for (i = 0; i < limit; i++) {
			if (psta->htpriv.ht_cap.mcs.rx_mask[i/8] & BIT(i%8))
				tx_ra_bitmap |= BIT(i+12);
		}
	}

	psta->ra_mask = tx_ra_bitmap;
	psta->init_rate = get_highest_rate_idx(tx_ra_bitmap)&0x3f;
}

void hw_var_port_switch(struct adapter *adapter)
{
}

void SetHwReg(struct adapter *adapter, u8 variable, u8 *val)
{
	struct hal_com_data *hal_data = GET_HAL_DATA(adapter);
	struct dm_odm_t *odm = &(hal_data->odmpriv);

	switch (variable) {
	case HW_VAR_PORT_SWITCH:
		hw_var_port_switch(adapter);
		break;
	case HW_VAR_INIT_RTS_RATE:
		rtw_warn_on(1);
		break;
	case HW_VAR_SEC_CFG:
	{
		u16 reg_scr;

		reg_scr = rtw_read16(adapter, REG_SECCFG);
		rtw_write16(adapter, REG_SECCFG, reg_scr|SCR_CHK_KEYID|SCR_RxDecEnable|SCR_TxEncEnable);
	}
		break;
	case HW_VAR_SEC_DK_CFG:
	{
		struct security_priv *sec = &adapter->securitypriv;
		u8 reg_scr = rtw_read8(adapter, REG_SECCFG);

		if (val) { /* Enable default key related setting */
			reg_scr |= SCR_TXBCUSEDK;
			if (sec->dot11AuthAlgrthm != dot11AuthAlgrthm_8021X)
				reg_scr |= (SCR_RxUseDK|SCR_TxUseDK);
		} else /* Disable default key related setting */
			reg_scr &= ~(SCR_RXBCUSEDK|SCR_TXBCUSEDK|SCR_RxUseDK|SCR_TxUseDK);

		rtw_write8(adapter, REG_SECCFG, reg_scr);
	}
		break;
	case HW_VAR_DM_FLAG:
		odm->SupportAbility = *((u32 *)val);
		break;
	case HW_VAR_DM_FUNC_OP:
		if (*((u8 *)val) == true) {
			/* save dm flag */
			odm->BK_SupportAbility = odm->SupportAbility;
		} else {
			/* restore dm flag */
			odm->SupportAbility = odm->BK_SupportAbility;
		}
		break;
	case HW_VAR_DM_FUNC_SET:
		if (*((u32 *)val) == DYNAMIC_ALL_FUNC_ENABLE) {
			struct dm_priv *dm = &hal_data->dmpriv;
			dm->DMFlag = dm->InitDMFlag;
			odm->SupportAbility = dm->InitODMFlag;
		} else {
			odm->SupportAbility |= *((u32 *)val);
		}
		break;
	case HW_VAR_DM_FUNC_CLR:
		/*
		* input is already a mask to clear function
		* don't invert it again! George, Lucas@20130513
		*/
		odm->SupportAbility &= *((u32 *)val);
		break;
	case HW_VAR_AMPDU_MIN_SPACE:
		/* TODO - Is something needed here? */
		break;
	case HW_VAR_WIRELESS_MODE:
		/* TODO - Is something needed here? */
		break;
	default:
		netdev_dbg(adapter->pnetdev,
			   FUNC_ADPT_FMT " variable(%d) not defined!\n",
			   FUNC_ADPT_ARG(adapter), variable);
		break;
	}
}

void GetHwReg(struct adapter *adapter, u8 variable, u8 *val)
{
	struct hal_com_data *hal_data = GET_HAL_DATA(adapter);
	struct dm_odm_t *odm = &(hal_data->odmpriv);

	switch (variable) {
	case HW_VAR_BASIC_RATE:
		*((u16 *)val) = hal_data->BasicRateSet;
		break;
	case HW_VAR_DM_FLAG:
		*((u32 *)val) = odm->SupportAbility;
		break;
	default:
		netdev_dbg(adapter->pnetdev,
			   FUNC_ADPT_FMT " variable(%d) not defined!\n",
			   FUNC_ADPT_ARG(adapter), variable);
		break;
	}
}




u8 SetHalDefVar(
	struct adapter *adapter, enum hal_def_variable variable, void *value
)
{
	struct hal_com_data *hal_data = GET_HAL_DATA(adapter);
	struct dm_odm_t *odm = &(hal_data->odmpriv);
	u8 bResult = _SUCCESS;

	switch (variable) {
	case HW_DEF_ODM_DBG_FLAG:
		ODM_CmnInfoUpdate(odm, ODM_CMNINFO_DBG_COMP, *((u64 *)value));
		break;
	case HW_DEF_ODM_DBG_LEVEL:
		ODM_CmnInfoUpdate(odm, ODM_CMNINFO_DBG_LEVEL, *((u32 *)value));
		break;
	case HAL_DEF_DBG_DM_FUNC:
	{
		u8 dm_func = *((u8 *)value);
		struct dm_priv *dm = &hal_data->dmpriv;

		if (dm_func == 0) { /* disable all dynamic func */
			odm->SupportAbility = DYNAMIC_FUNC_DISABLE;
		} else if (dm_func == 1) {/* disable DIG */
			odm->SupportAbility  &= (~DYNAMIC_BB_DIG);
		} else if (dm_func == 2) {/* disable High power */
			odm->SupportAbility  &= (~DYNAMIC_BB_DYNAMIC_TXPWR);
		} else if (dm_func == 3) {/* disable tx power tracking */
			odm->SupportAbility  &= (~DYNAMIC_RF_CALIBRATION);
		} else if (dm_func == 4) {/* disable BT coexistence */
			dm->DMFlag &= (~DYNAMIC_FUNC_BT);
		} else if (dm_func == 5) {/* disable antenna diversity */
			odm->SupportAbility  &= (~DYNAMIC_BB_ANT_DIV);
		} else if (dm_func == 6) {/* turn on all dynamic func */
			if (!(odm->SupportAbility  & DYNAMIC_BB_DIG)) {
				struct dig_t	*pDigTable = &odm->DM_DigTable;
				pDigTable->CurIGValue = rtw_read8(adapter, 0xc50);
			}
			dm->DMFlag |= DYNAMIC_FUNC_BT;
			odm->SupportAbility = DYNAMIC_ALL_FUNC_ENABLE;
		}
	}
		break;
	case HAL_DEF_DBG_DUMP_RXPKT:
		hal_data->bDumpRxPkt = *((u8 *)value);
		break;
	case HAL_DEF_DBG_DUMP_TXPKT:
		hal_data->bDumpTxPkt = *((u8 *)value);
		break;
	case HAL_DEF_ANT_DETECT:
		hal_data->AntDetection = *((u8 *)value);
		break;
	default:
		netdev_dbg(adapter->pnetdev,
			   "%s: [WARNING] HAL_DEF_VARIABLE(%d) not defined!\n",
			   __func__, variable);
		bResult = _FAIL;
		break;
	}

	return bResult;
}

u8 GetHalDefVar(
	struct adapter *adapter, enum hal_def_variable variable, void *value
)
{
	struct hal_com_data *hal_data = GET_HAL_DATA(adapter);
	u8 bResult = _SUCCESS;

	switch (variable) {
	case HAL_DEF_UNDERCORATEDSMOOTHEDPWDB:
		{
			struct mlme_priv *pmlmepriv;
			struct sta_priv *pstapriv;
			struct sta_info *psta;

			pmlmepriv = &adapter->mlmepriv;
			pstapriv = &adapter->stapriv;
			psta = rtw_get_stainfo(pstapriv, pmlmepriv->cur_network.network.mac_address);
			if (psta)
				*((int *)value) = psta->rssi_stat.UndecoratedSmoothedPWDB;
		}
		break;
	case HAL_DEF_DBG_DM_FUNC:
		*((u32 *)value) = hal_data->odmpriv.SupportAbility;
		break;
	case HAL_DEF_DBG_DUMP_RXPKT:
		*((u8 *)value) = hal_data->bDumpRxPkt;
		break;
	case HAL_DEF_DBG_DUMP_TXPKT:
		*((u8 *)value) = hal_data->bDumpTxPkt;
		break;
	case HAL_DEF_ANT_DETECT:
		*((u8 *)value) = hal_data->AntDetection;
		break;
	case HAL_DEF_MACID_SLEEP:
		*(u8 *)value = false;
		break;
	case HAL_DEF_TX_PAGE_SIZE:
		*((u32 *)value) = PAGE_SIZE_128;
		break;
	default:
		netdev_dbg(adapter->pnetdev,
			   "%s: [WARNING] HAL_DEF_VARIABLE(%d) not defined!\n",
			   __func__, variable);
		bResult = _FAIL;
		break;
	}

	return bResult;
}

void SetHalODMVar(
	struct adapter *Adapter,
	enum hal_odm_variable eVariable,
	void *pValue1,
	bool bSet
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_odm_t *podmpriv = &pHalData->odmpriv;
	/* _irqL irqL; */
	switch (eVariable) {
	case HAL_ODM_STA_INFO:
		{
			struct sta_info *psta = pValue1;
			if (bSet) {
				ODM_CmnInfoPtrArrayHook(podmpriv, ODM_CMNINFO_STA_STATUS, psta->mac_id, psta);
			} else {
				/* spin_lock_bh(&pHalData->odm_stainfo_lock); */
				ODM_CmnInfoPtrArrayHook(podmpriv, ODM_CMNINFO_STA_STATUS, psta->mac_id, NULL);

				/* spin_unlock_bh(&pHalData->odm_stainfo_lock); */
		    }
		}
		break;
	case HAL_ODM_P2P_STATE:
			ODM_CmnInfoUpdate(podmpriv, ODM_CMNINFO_WIFI_DIRECT, bSet);
		break;
	case HAL_ODM_WIFI_DISPLAY_STATE:
			ODM_CmnInfoUpdate(podmpriv, ODM_CMNINFO_WIFI_DISPLAY, bSet);
		break;

	default:
		break;
	}
}


bool eqNByte(u8 *str1, u8 *str2, u32 num)
{
	if (num == 0)
		return false;
	while (num > 0) {
		num--;
		if (str1[num] != str2[num])
			return false;
	}
	return true;
}

bool GetU1ByteIntegerFromStringInDecimal(char *Str, u8 *pInt)
{
	u16 i = 0;
	*pInt = 0;

	while (Str[i] != '\0') {
		if (Str[i] >= '0' && Str[i] <= '9') {
			*pInt *= 10;
			*pInt += (Str[i] - '0');
		} else
			return false;

		++i;
	}

	return true;
}

void rtw_hal_check_rxfifo_full(struct adapter *adapter)
{
	struct dvobj_priv *psdpriv = adapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	int save_cnt = false;

	/* switch counter to RX fifo */
	rtw_write8(adapter, REG_RXERR_RPT+3, rtw_read8(adapter, REG_RXERR_RPT+3)|0xf0);
	save_cnt = true;
	/* todo: other chips */

	if (save_cnt) {
		/* rtw_write8(adapter, REG_RXERR_RPT+3, rtw_read8(adapter, REG_RXERR_RPT+3)|0xa0); */
		pdbgpriv->dbg_rx_fifo_last_overflow = pdbgpriv->dbg_rx_fifo_curr_overflow;
		pdbgpriv->dbg_rx_fifo_curr_overflow = rtw_read16(adapter, REG_RXERR_RPT);
		pdbgpriv->dbg_rx_fifo_diff_overflow = pdbgpriv->dbg_rx_fifo_curr_overflow-pdbgpriv->dbg_rx_fifo_last_overflow;
	}
}

static u32 Array_kfreemap[] = {
	0xf8, 0xe,
	0xf6, 0xc,
	0xf4, 0xa,
	0xf2, 0x8,
	0xf0, 0x6,
	0xf3, 0x4,
	0xf5, 0x2,
	0xf7, 0x0,
	0xf9, 0x0,
	0xfc, 0x0,
};

#define		REG_RF_BB_GAIN_OFFSET	0x7f
//#define		RF_GAIN_OFFSET_MASK	0xfffff

void rtw_bb_rf_gain_offset(struct adapter *padapter)
{
	u8 value = padapter->eeprompriv.EEPROMRFGainOffset;
	u32 res, i = 0;
	u32 *Array = Array_kfreemap;
	u32 v1 = 0, v2 = 0, target = 0;

	if (value & BIT4) {
		if (padapter->eeprompriv.EEPROMRFGainVal != 0xff) {
			res = rtw_hal_read_rfreg(padapter, RF_PATH_A, 0x7f, 0xffffffff);
			res &= 0xfff87fff;
			/* res &= 0xfff87fff; */
			for (i = 0; i < ARRAY_SIZE(Array_kfreemap); i += 2) {
				v1 = Array[i];
				v2 = Array[i+1];
				if (v1 == padapter->eeprompriv.EEPROMRFGainVal) {
					target = v2;
					break;
				}
			}
			PHY_SetRFReg(padapter, RF_PATH_A, REG_RF_BB_GAIN_OFFSET, BIT18|BIT17|BIT16|BIT15, target);

			/* res |= (padapter->eeprompriv.EEPROMRFGainVal & 0x0f)<< 15; */
			/* rtw_hal_write_rfreg(padapter, RF_PATH_A, REG_RF_BB_GAIN_OFFSET, RF_GAIN_OFFSET_MASK, res); */
			res = rtw_hal_read_rfreg(padapter, RF_PATH_A, 0x7f, 0xffffffff);
		}
	}
}
