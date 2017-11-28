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
#define _HAL_COM_C_

#include <drv_types.h>
#include "hal_com_h2c.h"

#include "hal_data.h"

#ifdef RTW_HALMAC
#include "../../hal/hal_halmac.h"
#endif

void rtw_dump_fw_info(void *sel, _adapter *adapter)
{
	HAL_DATA_TYPE	*hal_data = NULL;

	if (!adapter)
		return;

	hal_data = GET_HAL_DATA(adapter);
	if (adapter->bFWReady)
		RTW_PRINT_SEL(sel, "FW VER -%d.%d\n", hal_data->firmware_version, hal_data->firmware_sub_version);
	else
		RTW_PRINT_SEL(sel, "FW not ready\n");
}

/* #define CONFIG_GTK_OL_DBG */

/*#define DBG_SEC_CAM_MOVE*/
#ifdef DBG_SEC_CAM_MOVE
void rtw_hal_move_sta_gk_to_dk(_adapter *adapter)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	int cam_id, index = 0;
	u8 *addr = NULL;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
		return;

	addr = get_bssid(pmlmepriv);

	if (addr == NULL) {
		RTW_INFO("%s: get bssid MAC addr fail!!\n", __func__);
		return;
	}

	rtw_clean_dk_section(adapter);

	do {
		cam_id = rtw_camid_search(adapter, addr, index, 1);

		if (cam_id == -1)
			RTW_INFO("%s: cam_id: %d, key_id:%d\n", __func__, cam_id, index);
		else
			rtw_sec_cam_swap(adapter, cam_id, index);

		index++;
	} while (index < 4);

}

void rtw_hal_read_sta_dk_key(_adapter *adapter, u8 key_id)
{
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	_irqL irqL;
	u8 get_key[16];

	_rtw_memset(get_key, 0, sizeof(get_key));

	if (key_id > 4) {
		RTW_INFO("%s [ERROR] gtk_keyindex:%d invalid\n", __func__, key_id);
		rtw_warn_on(1);
		return;
	}
	rtw_sec_read_cam_ent(adapter, key_id, NULL, NULL, get_key);

	/*update key into related sw variable*/
	_enter_critical_bh(&cam_ctl->lock, &irqL);
	if (_rtw_camid_is_gk(adapter, key_id)) {
		RTW_INFO("[HW KEY] -Key-id:%d "KEY_FMT"\n", key_id, KEY_ARG(get_key));
		RTW_INFO("[cam_cache KEY] - Key-id:%d "KEY_FMT"\n", key_id, KEY_ARG(&dvobj->cam_cache[key_id].key));
	}
	_exit_critical_bh(&cam_ctl->lock, &irqL);

}
#endif


#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
	char	rtw_phy_para_file_path[PATH_LENGTH_MAX];
#endif

void dump_chip_info(HAL_VERSION	ChipVersion)
{
	int cnt = 0;
	u8 buf[128] = {0};

	if (IS_8188E(ChipVersion))
		cnt += sprintf((buf + cnt), "Chip Version Info: CHIP_8188E_");
	else if (IS_8188F(ChipVersion))
		cnt += sprintf((buf + cnt), "Chip Version Info: CHIP_8188F_");
	else if (IS_8812_SERIES(ChipVersion))
		cnt += sprintf((buf + cnt), "Chip Version Info: CHIP_8812_");
	else if (IS_8192E(ChipVersion))
		cnt += sprintf((buf + cnt), "Chip Version Info: CHIP_8192E_");
	else if (IS_8821_SERIES(ChipVersion))
		cnt += sprintf((buf + cnt), "Chip Version Info: CHIP_8821_");
	else if (IS_8723B_SERIES(ChipVersion))
		cnt += sprintf((buf + cnt), "Chip Version Info: CHIP_8723B_");
	else if (IS_8703B_SERIES(ChipVersion))
		cnt += sprintf((buf + cnt), "Chip Version Info: CHIP_8703B_");
	else if (IS_8723D_SERIES(ChipVersion))
		cnt += sprintf((buf + cnt), "Chip Version Info: CHIP_8723D_");
	else if (IS_8814A_SERIES(ChipVersion))
		cnt += sprintf((buf + cnt), "Chip Version Info: CHIP_8814A_");
	else if (IS_8822B_SERIES(ChipVersion))
		cnt += sprintf((buf + cnt), "Chip Version Info: CHIP_8822B_");
	else if (IS_8821C_SERIES(ChipVersion))
		cnt += sprintf((buf + cnt), "Chip Version Info: CHIP_8821C_");
	else
		cnt += sprintf((buf + cnt), "Chip Version Info: CHIP_UNKNOWN_");

	cnt += sprintf((buf + cnt), "%s_", IS_NORMAL_CHIP(ChipVersion) ? "Normal_Chip" : "Test_Chip");
	if (IS_CHIP_VENDOR_TSMC(ChipVersion))
		cnt += sprintf((buf + cnt), "%s_", "TSMC");
	else if (IS_CHIP_VENDOR_UMC(ChipVersion))
		cnt += sprintf((buf + cnt), "%s_", "UMC");
	else if (IS_CHIP_VENDOR_SMIC(ChipVersion))
		cnt += sprintf((buf + cnt), "%s_", "SMIC");

	if (IS_A_CUT(ChipVersion))
		cnt += sprintf((buf + cnt), "A_CUT_");
	else if (IS_B_CUT(ChipVersion))
		cnt += sprintf((buf + cnt), "B_CUT_");
	else if (IS_C_CUT(ChipVersion))
		cnt += sprintf((buf + cnt), "C_CUT_");
	else if (IS_D_CUT(ChipVersion))
		cnt += sprintf((buf + cnt), "D_CUT_");
	else if (IS_E_CUT(ChipVersion))
		cnt += sprintf((buf + cnt), "E_CUT_");
	else if (IS_F_CUT(ChipVersion))
		cnt += sprintf((buf + cnt), "F_CUT_");
	else if (IS_I_CUT(ChipVersion))
		cnt += sprintf((buf + cnt), "I_CUT_");
	else if (IS_J_CUT(ChipVersion))
		cnt += sprintf((buf + cnt), "J_CUT_");
	else if (IS_K_CUT(ChipVersion))
		cnt += sprintf((buf + cnt), "K_CUT_");
	else
		cnt += sprintf((buf + cnt), "UNKNOWN_CUT(%d)_", ChipVersion.CUTVersion);

	if (IS_1T1R(ChipVersion))
		cnt += sprintf((buf + cnt), "1T1R_");
	else if (IS_1T2R(ChipVersion))
		cnt += sprintf((buf + cnt), "1T2R_");
	else if (IS_2T2R(ChipVersion))
		cnt += sprintf((buf + cnt), "2T2R_");
	else if (IS_3T3R(ChipVersion))
		cnt += sprintf((buf + cnt), "3T3R_");
	else if (IS_3T4R(ChipVersion))
		cnt += sprintf((buf + cnt), "3T4R_");
	else if (IS_4T4R(ChipVersion))
		cnt += sprintf((buf + cnt), "4T4R_");
	else
		cnt += sprintf((buf + cnt), "UNKNOWN_RFTYPE(%d)_", ChipVersion.RFType);

	cnt += sprintf((buf + cnt), "RomVer(%d)\n", ChipVersion.ROMVer);

	RTW_INFO("%s", buf);
}
void rtw_hal_config_rftype(PADAPTER  padapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);

	if (IS_1T1R(pHalData->version_id)) {
		pHalData->rf_type = RF_1T1R;
		pHalData->NumTotalRFPath = 1;
	} else if (IS_2T2R(pHalData->version_id)) {
		pHalData->rf_type = RF_2T2R;
		pHalData->NumTotalRFPath = 2;
	} else if (IS_1T2R(pHalData->version_id)) {
		pHalData->rf_type = RF_1T2R;
		pHalData->NumTotalRFPath = 2;
	} else if (IS_3T3R(pHalData->version_id)) {
		pHalData->rf_type = RF_3T3R;
		pHalData->NumTotalRFPath = 3;
	} else if (IS_4T4R(pHalData->version_id)) {
		pHalData->rf_type = RF_4T4R;
		pHalData->NumTotalRFPath = 4;
	} else {
		pHalData->rf_type = RF_1T1R;
		pHalData->NumTotalRFPath = 1;
	}

	RTW_INFO("%s RF_Type is %d TotalTxPath is %d\n", __FUNCTION__, pHalData->rf_type, pHalData->NumTotalRFPath);
}

#define	EEPROM_CHANNEL_PLAN_BY_HW_MASK	0x80

/*
 * Description:
 *	Use hardware(efuse), driver parameter(registry) and default channel plan
 *	to decide which one should be used.
 *
 * Parameters:
 *	padapter			pointer of adapter
 *	hw_alpha2		country code from HW (efuse/eeprom/mapfile)
 *	hw_chplan		channel plan from HW (efuse/eeprom/mapfile)
 *						BIT[7] software configure mode; 0:Enable, 1:disable
 *						BIT[6:0] Channel Plan
 *	sw_alpha2		country code from HW (registry/module param)
 *	sw_chplan		channel plan from SW (registry/module param)
 *	def_chplan		channel plan used when HW/SW both invalid
 *	AutoLoadFail		efuse autoload fail or not
 *
 * Return:
 *	Final channel plan decision
 *
 */
u8 hal_com_config_channel_plan(
	IN	PADAPTER padapter,
	IN	char *hw_alpha2,
	IN	u8 hw_chplan,
	IN	char *sw_alpha2,
	IN	u8 sw_chplan,
	IN	u8 def_chplan,
	IN	BOOLEAN AutoLoadFail
)
{
	PHAL_DATA_TYPE	pHalData;
	u8 force_hw_chplan = _FALSE;
	int chplan = -1;
	const struct country_chplan *country_ent = NULL, *ent;

	pHalData = GET_HAL_DATA(padapter);

	/* treat 0xFF as invalid value, bypass hw_chplan & force_hw_chplan parsing */
	if (hw_chplan == 0xFF)
		goto chk_hw_country_code;

	if (AutoLoadFail == _TRUE)
		goto chk_sw_config;

#ifndef CONFIG_FORCE_SW_CHANNEL_PLAN
	if (hw_chplan & EEPROM_CHANNEL_PLAN_BY_HW_MASK)
		force_hw_chplan = _TRUE;
#endif

	hw_chplan &= (~EEPROM_CHANNEL_PLAN_BY_HW_MASK);

chk_hw_country_code:
	if (hw_alpha2 && !IS_ALPHA2_NO_SPECIFIED(hw_alpha2)) {
		ent = rtw_get_chplan_from_country(hw_alpha2);
		if (ent) {
			/* get chplan from hw country code, by pass hw chplan setting */
			country_ent = ent;
			chplan = ent->chplan;
			goto chk_sw_config;
		} else
			RTW_PRINT("%s unsupported hw_alpha2:\"%c%c\"\n", __func__, hw_alpha2[0], hw_alpha2[1]);
	}

	if (rtw_is_channel_plan_valid(hw_chplan))
		chplan = hw_chplan;
	else if (force_hw_chplan == _TRUE) {
		RTW_PRINT("%s unsupported hw_chplan:0x%02X\n", __func__, hw_chplan);
		/* hw infomaton invalid, refer to sw information */
		force_hw_chplan = _FALSE;
	}

chk_sw_config:
	if (force_hw_chplan == _TRUE)
		goto done;

	if (sw_alpha2 && !IS_ALPHA2_NO_SPECIFIED(sw_alpha2)) {
		ent = rtw_get_chplan_from_country(sw_alpha2);
		if (ent) {
			/* get chplan from sw country code, by pass sw chplan setting */
			country_ent = ent;
			chplan = ent->chplan;
			goto done;
		} else
			RTW_PRINT("%s unsupported sw_alpha2:\"%c%c\"\n", __func__, sw_alpha2[0], sw_alpha2[1]);
	}

	if (rtw_is_channel_plan_valid(sw_chplan)) {
		/* cancel hw_alpha2 because chplan is specified by sw_chplan*/
		country_ent = NULL;
		chplan = sw_chplan;
	} else if (sw_chplan != RTW_CHPLAN_MAX)
		RTW_PRINT("%s unsupported sw_chplan:0x%02X\n", __func__, sw_chplan);

done:
	if (chplan == -1) {
		RTW_PRINT("%s use def_chplan:0x%02X\n", __func__, def_chplan);
		chplan = def_chplan;
	} else if (country_ent) {
		RTW_PRINT("%s country code:\"%c%c\" with chplan:0x%02X\n", __func__
			, country_ent->alpha2[0], country_ent->alpha2[1], country_ent->chplan);
	} else
		RTW_PRINT("%s chplan:0x%02X\n", __func__, chplan);

	padapter->mlmepriv.country_ent = country_ent;
	pHalData->bDisableSWChannelPlan = force_hw_chplan;

	return chplan;
}

BOOLEAN
HAL_IsLegalChannel(
	IN	PADAPTER	Adapter,
	IN	u32			Channel
)
{
	BOOLEAN bLegalChannel = _TRUE;

	if (Channel > 14) {
		if (is_supported_5g(Adapter->registrypriv.wireless_mode) == _FALSE) {
			bLegalChannel = _FALSE;
			RTW_INFO("Channel > 14 but wireless_mode do not support 5G\n");
		}
	} else if ((Channel <= 14) && (Channel >= 1)) {
		if (IsSupported24G(Adapter->registrypriv.wireless_mode) == _FALSE) {
			bLegalChannel = _FALSE;
			RTW_INFO("(Channel <= 14) && (Channel >=1) but wireless_mode do not support 2.4G\n");
		}
	} else {
		bLegalChannel = _FALSE;
		RTW_INFO("Channel is Invalid !!!\n");
	}

	return bLegalChannel;
}

u8	MRateToHwRate(u8 rate)
{
	u8	ret = DESC_RATE1M;

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
	case MGN_MCS8:
		ret = DESC_RATEMCS8;
		break;
	case MGN_MCS9:
		ret = DESC_RATEMCS9;
		break;
	case MGN_MCS10:
		ret = DESC_RATEMCS10;
		break;
	case MGN_MCS11:
		ret = DESC_RATEMCS11;
		break;
	case MGN_MCS12:
		ret = DESC_RATEMCS12;
		break;
	case MGN_MCS13:
		ret = DESC_RATEMCS13;
		break;
	case MGN_MCS14:
		ret = DESC_RATEMCS14;
		break;
	case MGN_MCS15:
		ret = DESC_RATEMCS15;
		break;
	case MGN_MCS16:
		ret = DESC_RATEMCS16;
		break;
	case MGN_MCS17:
		ret = DESC_RATEMCS17;
		break;
	case MGN_MCS18:
		ret = DESC_RATEMCS18;
		break;
	case MGN_MCS19:
		ret = DESC_RATEMCS19;
		break;
	case MGN_MCS20:
		ret = DESC_RATEMCS20;
		break;
	case MGN_MCS21:
		ret = DESC_RATEMCS21;
		break;
	case MGN_MCS22:
		ret = DESC_RATEMCS22;
		break;
	case MGN_MCS23:
		ret = DESC_RATEMCS23;
		break;
	case MGN_MCS24:
		ret = DESC_RATEMCS24;
		break;
	case MGN_MCS25:
		ret = DESC_RATEMCS25;
		break;
	case MGN_MCS26:
		ret = DESC_RATEMCS26;
		break;
	case MGN_MCS27:
		ret = DESC_RATEMCS27;
		break;
	case MGN_MCS28:
		ret = DESC_RATEMCS28;
		break;
	case MGN_MCS29:
		ret = DESC_RATEMCS29;
		break;
	case MGN_MCS30:
		ret = DESC_RATEMCS30;
		break;
	case MGN_MCS31:
		ret = DESC_RATEMCS31;
		break;

	case MGN_VHT1SS_MCS0:
		ret = DESC_RATEVHTSS1MCS0;
		break;
	case MGN_VHT1SS_MCS1:
		ret = DESC_RATEVHTSS1MCS1;
		break;
	case MGN_VHT1SS_MCS2:
		ret = DESC_RATEVHTSS1MCS2;
		break;
	case MGN_VHT1SS_MCS3:
		ret = DESC_RATEVHTSS1MCS3;
		break;
	case MGN_VHT1SS_MCS4:
		ret = DESC_RATEVHTSS1MCS4;
		break;
	case MGN_VHT1SS_MCS5:
		ret = DESC_RATEVHTSS1MCS5;
		break;
	case MGN_VHT1SS_MCS6:
		ret = DESC_RATEVHTSS1MCS6;
		break;
	case MGN_VHT1SS_MCS7:
		ret = DESC_RATEVHTSS1MCS7;
		break;
	case MGN_VHT1SS_MCS8:
		ret = DESC_RATEVHTSS1MCS8;
		break;
	case MGN_VHT1SS_MCS9:
		ret = DESC_RATEVHTSS1MCS9;
		break;
	case MGN_VHT2SS_MCS0:
		ret = DESC_RATEVHTSS2MCS0;
		break;
	case MGN_VHT2SS_MCS1:
		ret = DESC_RATEVHTSS2MCS1;
		break;
	case MGN_VHT2SS_MCS2:
		ret = DESC_RATEVHTSS2MCS2;
		break;
	case MGN_VHT2SS_MCS3:
		ret = DESC_RATEVHTSS2MCS3;
		break;
	case MGN_VHT2SS_MCS4:
		ret = DESC_RATEVHTSS2MCS4;
		break;
	case MGN_VHT2SS_MCS5:
		ret = DESC_RATEVHTSS2MCS5;
		break;
	case MGN_VHT2SS_MCS6:
		ret = DESC_RATEVHTSS2MCS6;
		break;
	case MGN_VHT2SS_MCS7:
		ret = DESC_RATEVHTSS2MCS7;
		break;
	case MGN_VHT2SS_MCS8:
		ret = DESC_RATEVHTSS2MCS8;
		break;
	case MGN_VHT2SS_MCS9:
		ret = DESC_RATEVHTSS2MCS9;
		break;
	case MGN_VHT3SS_MCS0:
		ret = DESC_RATEVHTSS3MCS0;
		break;
	case MGN_VHT3SS_MCS1:
		ret = DESC_RATEVHTSS3MCS1;
		break;
	case MGN_VHT3SS_MCS2:
		ret = DESC_RATEVHTSS3MCS2;
		break;
	case MGN_VHT3SS_MCS3:
		ret = DESC_RATEVHTSS3MCS3;
		break;
	case MGN_VHT3SS_MCS4:
		ret = DESC_RATEVHTSS3MCS4;
		break;
	case MGN_VHT3SS_MCS5:
		ret = DESC_RATEVHTSS3MCS5;
		break;
	case MGN_VHT3SS_MCS6:
		ret = DESC_RATEVHTSS3MCS6;
		break;
	case MGN_VHT3SS_MCS7:
		ret = DESC_RATEVHTSS3MCS7;
		break;
	case MGN_VHT3SS_MCS8:
		ret = DESC_RATEVHTSS3MCS8;
		break;
	case MGN_VHT3SS_MCS9:
		ret = DESC_RATEVHTSS3MCS9;
		break;
	case MGN_VHT4SS_MCS0:
		ret = DESC_RATEVHTSS4MCS0;
		break;
	case MGN_VHT4SS_MCS1:
		ret = DESC_RATEVHTSS4MCS1;
		break;
	case MGN_VHT4SS_MCS2:
		ret = DESC_RATEVHTSS4MCS2;
		break;
	case MGN_VHT4SS_MCS3:
		ret = DESC_RATEVHTSS4MCS3;
		break;
	case MGN_VHT4SS_MCS4:
		ret = DESC_RATEVHTSS4MCS4;
		break;
	case MGN_VHT4SS_MCS5:
		ret = DESC_RATEVHTSS4MCS5;
		break;
	case MGN_VHT4SS_MCS6:
		ret = DESC_RATEVHTSS4MCS6;
		break;
	case MGN_VHT4SS_MCS7:
		ret = DESC_RATEVHTSS4MCS7;
		break;
	case MGN_VHT4SS_MCS8:
		ret = DESC_RATEVHTSS4MCS8;
		break;
	case MGN_VHT4SS_MCS9:
		ret = DESC_RATEVHTSS4MCS9;
		break;
	default:
		break;
	}

	return ret;
}

u8	hw_rate_to_m_rate(u8 rate)
{
	u8	ret_rate = MGN_1M;

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
	case DESC_RATEMCS8:
		ret_rate = MGN_MCS8;
		break;
	case DESC_RATEMCS9:
		ret_rate = MGN_MCS9;
		break;
	case DESC_RATEMCS10:
		ret_rate = MGN_MCS10;
		break;
	case DESC_RATEMCS11:
		ret_rate = MGN_MCS11;
		break;
	case DESC_RATEMCS12:
		ret_rate = MGN_MCS12;
		break;
	case DESC_RATEMCS13:
		ret_rate = MGN_MCS13;
		break;
	case DESC_RATEMCS14:
		ret_rate = MGN_MCS14;
		break;
	case DESC_RATEMCS15:
		ret_rate = MGN_MCS15;
		break;
	case DESC_RATEMCS16:
		ret_rate = MGN_MCS16;
		break;
	case DESC_RATEMCS17:
		ret_rate = MGN_MCS17;
		break;
	case DESC_RATEMCS18:
		ret_rate = MGN_MCS18;
		break;
	case DESC_RATEMCS19:
		ret_rate = MGN_MCS19;
		break;
	case DESC_RATEMCS20:
		ret_rate = MGN_MCS20;
		break;
	case DESC_RATEMCS21:
		ret_rate = MGN_MCS21;
		break;
	case DESC_RATEMCS22:
		ret_rate = MGN_MCS22;
		break;
	case DESC_RATEMCS23:
		ret_rate = MGN_MCS23;
		break;
	case DESC_RATEMCS24:
		ret_rate = MGN_MCS24;
		break;
	case DESC_RATEMCS25:
		ret_rate = MGN_MCS25;
		break;
	case DESC_RATEMCS26:
		ret_rate = MGN_MCS26;
		break;
	case DESC_RATEMCS27:
		ret_rate = MGN_MCS27;
		break;
	case DESC_RATEMCS28:
		ret_rate = MGN_MCS28;
		break;
	case DESC_RATEMCS29:
		ret_rate = MGN_MCS29;
		break;
	case DESC_RATEMCS30:
		ret_rate = MGN_MCS30;
		break;
	case DESC_RATEMCS31:
		ret_rate = MGN_MCS31;
		break;
	case DESC_RATEVHTSS1MCS0:
		ret_rate = MGN_VHT1SS_MCS0;
		break;
	case DESC_RATEVHTSS1MCS1:
		ret_rate = MGN_VHT1SS_MCS1;
		break;
	case DESC_RATEVHTSS1MCS2:
		ret_rate = MGN_VHT1SS_MCS2;
		break;
	case DESC_RATEVHTSS1MCS3:
		ret_rate = MGN_VHT1SS_MCS3;
		break;
	case DESC_RATEVHTSS1MCS4:
		ret_rate = MGN_VHT1SS_MCS4;
		break;
	case DESC_RATEVHTSS1MCS5:
		ret_rate = MGN_VHT1SS_MCS5;
		break;
	case DESC_RATEVHTSS1MCS6:
		ret_rate = MGN_VHT1SS_MCS6;
		break;
	case DESC_RATEVHTSS1MCS7:
		ret_rate = MGN_VHT1SS_MCS7;
		break;
	case DESC_RATEVHTSS1MCS8:
		ret_rate = MGN_VHT1SS_MCS8;
		break;
	case DESC_RATEVHTSS1MCS9:
		ret_rate = MGN_VHT1SS_MCS9;
		break;
	case DESC_RATEVHTSS2MCS0:
		ret_rate = MGN_VHT2SS_MCS0;
		break;
	case DESC_RATEVHTSS2MCS1:
		ret_rate = MGN_VHT2SS_MCS1;
		break;
	case DESC_RATEVHTSS2MCS2:
		ret_rate = MGN_VHT2SS_MCS2;
		break;
	case DESC_RATEVHTSS2MCS3:
		ret_rate = MGN_VHT2SS_MCS3;
		break;
	case DESC_RATEVHTSS2MCS4:
		ret_rate = MGN_VHT2SS_MCS4;
		break;
	case DESC_RATEVHTSS2MCS5:
		ret_rate = MGN_VHT2SS_MCS5;
		break;
	case DESC_RATEVHTSS2MCS6:
		ret_rate = MGN_VHT2SS_MCS6;
		break;
	case DESC_RATEVHTSS2MCS7:
		ret_rate = MGN_VHT2SS_MCS7;
		break;
	case DESC_RATEVHTSS2MCS8:
		ret_rate = MGN_VHT2SS_MCS8;
		break;
	case DESC_RATEVHTSS2MCS9:
		ret_rate = MGN_VHT2SS_MCS9;
		break;
	case DESC_RATEVHTSS3MCS0:
		ret_rate = MGN_VHT3SS_MCS0;
		break;
	case DESC_RATEVHTSS3MCS1:
		ret_rate = MGN_VHT3SS_MCS1;
		break;
	case DESC_RATEVHTSS3MCS2:
		ret_rate = MGN_VHT3SS_MCS2;
		break;
	case DESC_RATEVHTSS3MCS3:
		ret_rate = MGN_VHT3SS_MCS3;
		break;
	case DESC_RATEVHTSS3MCS4:
		ret_rate = MGN_VHT3SS_MCS4;
		break;
	case DESC_RATEVHTSS3MCS5:
		ret_rate = MGN_VHT3SS_MCS5;
		break;
	case DESC_RATEVHTSS3MCS6:
		ret_rate = MGN_VHT3SS_MCS6;
		break;
	case DESC_RATEVHTSS3MCS7:
		ret_rate = MGN_VHT3SS_MCS7;
		break;
	case DESC_RATEVHTSS3MCS8:
		ret_rate = MGN_VHT3SS_MCS8;
		break;
	case DESC_RATEVHTSS3MCS9:
		ret_rate = MGN_VHT3SS_MCS9;
		break;
	case DESC_RATEVHTSS4MCS0:
		ret_rate = MGN_VHT4SS_MCS0;
		break;
	case DESC_RATEVHTSS4MCS1:
		ret_rate = MGN_VHT4SS_MCS1;
		break;
	case DESC_RATEVHTSS4MCS2:
		ret_rate = MGN_VHT4SS_MCS2;
		break;
	case DESC_RATEVHTSS4MCS3:
		ret_rate = MGN_VHT4SS_MCS3;
		break;
	case DESC_RATEVHTSS4MCS4:
		ret_rate = MGN_VHT4SS_MCS4;
		break;
	case DESC_RATEVHTSS4MCS5:
		ret_rate = MGN_VHT4SS_MCS5;
		break;
	case DESC_RATEVHTSS4MCS6:
		ret_rate = MGN_VHT4SS_MCS6;
		break;
	case DESC_RATEVHTSS4MCS7:
		ret_rate = MGN_VHT4SS_MCS7;
		break;
	case DESC_RATEVHTSS4MCS8:
		ret_rate = MGN_VHT4SS_MCS8;
		break;
	case DESC_RATEVHTSS4MCS9:
		ret_rate = MGN_VHT4SS_MCS9;
		break;

	default:
		RTW_INFO("hw_rate_to_m_rate(): Non supported Rate [%x]!!!\n", rate);
		break;
	}

	return ret_rate;
}

void	HalSetBrateCfg(
	IN PADAPTER		Adapter,
	IN u8			*mBratesOS,
	OUT u16			*pBrateCfg)
{
	u8	i, is_brate, brate;

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

static VOID
_OneOutPipeMapping(
	IN	PADAPTER	pAdapter
)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(pAdapter);

	pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];/* VO */
	pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[0];/* VI */
	pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[0];/* BE */
	pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[0];/* BK */

	pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];/* BCN */
	pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];/* MGT */
	pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];/* HIGH */
	pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];/* TXCMD */
}

static VOID
_TwoOutPipeMapping(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		bWIFICfg
)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(pAdapter);

	if (bWIFICfg) { /* WMM */

		/*	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA  */
		/* {  0, 	1, 	0, 	1, 	0, 	0, 	0, 	0, 		0	}; */
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


		/* BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA */
		/* {  1, 	1, 	0, 	0, 	0, 	0, 	0, 	0, 		0	};			 */
		/* 0:ep_0 num, 1:ep_1 num */

		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];/* VO */
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[0];/* VI */
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[1];/* BE */
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[1];/* BK */

		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];/* BCN */
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];/* MGT */
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];/* HIGH */
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];/* TXCMD	 */

	}

}

static VOID _ThreeOutPipeMapping(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		bWIFICfg
)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(pAdapter);

	if (bWIFICfg) { /* for WMM */

		/*	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA  */
		/* {  1, 	2, 	1, 	0, 	0, 	0, 	0, 	0, 		0	}; */
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


		/*	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA  */
		/* {  2, 	2, 	1, 	0, 	0, 	0, 	0, 	0, 		0	};			 */
		/* 0:H, 1:N, 2:L */

		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];/* VO */
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[1];/* VI */
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[2];/* BE */
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[2];/* BK */

		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];/* BCN */
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];/* MGT */
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];/* HIGH */
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];/* TXCMD	 */
	}

}
static VOID _FourOutPipeMapping(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN		bWIFICfg
)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(pAdapter);

	if (bWIFICfg) { /* for WMM */

		/*	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA  */
		/* {  1, 	2, 	1, 	0, 	0, 	0, 	0, 	0, 		0	}; */
		/* 0:H, 1:N, 2:L ,3:E */

		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];/* VO */
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[1];/* VI */
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[2];/* BE */
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[1];/* BK */

		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];/* BCN */
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];/* MGT */
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[3];/* HIGH */
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];/* TXCMD */

	} else { /* typical setting */


		/*	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA  */
		/* {  2, 	2, 	1, 	0, 	0, 	0, 	0, 	0, 		0	};			 */
		/* 0:H, 1:N, 2:L */

		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];/* VO */
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[1];/* VI */
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[2];/* BE */
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[2];/* BK */

		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];/* BCN */
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];/* MGT */
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[3];/* HIGH */
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];/* TXCMD	 */
	}

}
BOOLEAN
Hal_MappingOutPipe(
	IN	PADAPTER	pAdapter,
	IN	u8		NumOutPipe
)
{
	struct registry_priv *pregistrypriv = &pAdapter->registrypriv;

	BOOLEAN	 bWIFICfg = (pregistrypriv->wifi_spec) ? _TRUE : _FALSE;

	BOOLEAN result = _TRUE;

	switch (NumOutPipe) {
	case 2:
		_TwoOutPipeMapping(pAdapter, bWIFICfg);
		break;
	case 3:
	case 4:
		_ThreeOutPipeMapping(pAdapter, bWIFICfg);
		break;
	case 1:
		_OneOutPipeMapping(pAdapter);
		break;
	default:
		result = _FALSE;
		break;
	}

	return result;

}

void rtw_hal_reqtxrpt(_adapter *padapter, u8 macid)
{
	if (padapter->hal_func.reqtxrpt)
		padapter->hal_func.reqtxrpt(padapter, macid);
}

void rtw_hal_dump_macaddr(void *sel, _adapter *adapter)
{
	int i;
	_adapter *iface;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	u8 mac_addr[ETH_ALEN];

#ifdef CONFIG_MI_WITH_MBSSID_CAM
	rtw_mbid_cam_dump(sel, __func__, adapter);
#else
	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface) {
			rtw_hal_get_macaddr_port(iface, mac_addr);
			RTW_PRINT_SEL(sel, ADPT_FMT"- hw port(%d) mac_addr ="MAC_FMT"\n",
				ADPT_ARG(iface), iface->hw_port, MAC_ARG(mac_addr));
		}
	}
#endif
}

void rtw_restore_mac_addr(_adapter *adapter)
{
#ifdef CONFIG_MI_WITH_MBSSID_CAM
	_adapter *iface;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);

	rtw_mbid_cam_restore(adapter);
#else
	int i;
	_adapter *iface;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface)
			rtw_hal_set_macaddr_port(iface, adapter_mac_addr(iface));
	}
#endif
	if (1)
		rtw_hal_dump_macaddr(RTW_DBGDUMP, adapter);
}

void rtw_init_hal_com_default_value(PADAPTER Adapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct registry_priv *regsty = adapter_to_regsty(Adapter);

	pHalData->AntDetection = 1;
	pHalData->antenna_test = _FALSE;
	pHalData->u1ForcedIgiLb = regsty->force_igi_lb;
}

#ifdef CONFIG_FW_C2H_REG
void c2h_evt_clear(_adapter *adapter)
{
	rtw_write8(adapter, REG_C2HEVT_CLEAR, C2H_EVT_HOST_CLOSE);
}

s32 c2h_evt_read_88xx(_adapter *adapter, u8 *buf)
{
	s32 ret = _FAIL;
	int i;
	u8 trigger;

	if (buf == NULL)
		goto exit;

	trigger = rtw_read8(adapter, REG_C2HEVT_CLEAR);

	if (trigger == C2H_EVT_HOST_CLOSE) {
		goto exit; /* Not ready */
	} else if (trigger != C2H_EVT_FW_CLOSE) {
		goto clear_evt; /* Not a valid value */
	}

	_rtw_memset(buf, 0, C2H_REG_LEN);

	/* Read ID, LEN, SEQ */
	SET_C2H_ID_88XX(buf, rtw_read8(adapter, REG_C2HEVT_MSG_NORMAL));
	SET_C2H_SEQ_88XX(buf, rtw_read8(adapter, REG_C2HEVT_CMD_SEQ_88XX));
	SET_C2H_PLEN_88XX(buf, rtw_read8(adapter, REG_C2HEVT_CMD_LEN_88XX));

	if (0) {
		RTW_INFO("%s id=0x%02x, seq=%u, plen=%u, trigger=0x%02x\n", __func__
			, C2H_ID_88XX(buf), C2H_SEQ_88XX(buf), C2H_PLEN_88XX(buf), trigger);
	}

	/* Read the content */
	for (i = 0; i < C2H_PLEN_88XX(buf); i++)
		*(C2H_PAYLOAD_88XX(buf) + i) = rtw_read8(adapter, REG_C2HEVT_MSG_NORMAL + 2 + i);

	RTW_DBG_DUMP("payload:\n", C2H_PAYLOAD_88XX(buf), C2H_PLEN_88XX(buf));

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
#endif /* CONFIG_FW_C2H_REG */

#ifdef CONFIG_FW_C2H_PKT
#ifndef DBG_C2H_PKT_PRE_HDL
#define DBG_C2H_PKT_PRE_HDL 0
#endif
#ifndef DBG_C2H_PKT_HDL
#define DBG_C2H_PKT_HDL 0
#endif
void rtw_hal_c2h_pkt_pre_hdl(_adapter *adapter, u8 *buf, u16 len)
{
#ifdef RTW_HALMAC
	/* TODO: extract hal_mac IC's code here*/
#else
	u8 parse_fail = 0;
	u8 hdl_here = 0;
	s32 ret = _FAIL;
	u8 id, seq, plen;
	u8 *payload;

	if (rtw_hal_c2h_pkt_hdr_parse(adapter, buf, len, &id, &seq, &plen, &payload) != _SUCCESS) {
		parse_fail = 1;
		goto exit;
	}

	hdl_here = rtw_hal_c2h_id_handle_directly(adapter, id, seq, plen, payload) == _TRUE ? 1 : 0;
	if (hdl_here) 
		ret = rtw_hal_c2h_handler(adapter, id, seq, plen, payload);
	else
		ret = rtw_c2h_packet_wk_cmd(adapter, buf, len);

exit:
	if (parse_fail)
		RTW_ERR("%s parse fail, buf=%p, len=:%u\n", __func__, buf, len);
	else if (ret != _SUCCESS || DBG_C2H_PKT_PRE_HDL > 0) {
		RTW_PRINT("%s: id=0x%02x, seq=%u, plen=%u, %s %s\n", __func__, id, seq, plen
			, hdl_here ? "handle" : "enqueue"
			, ret == _SUCCESS ? "ok" : "fail"
		);
		if (DBG_C2H_PKT_PRE_HDL >= 2)
			RTW_PRINT_DUMP("dump: ", buf, len);
	}
#endif
}

void rtw_hal_c2h_pkt_hdl(_adapter *adapter, u8 *buf, u16 len)
{
#ifdef RTW_HALMAC
	adapter->hal_func.hal_mac_c2h_handler(adapter, buf, len);
#else
	u8 parse_fail = 0;
	u8 bypass = 0;
	s32 ret = _FAIL;
	u8 id, seq, plen;
	u8 *payload;

	if (rtw_hal_c2h_pkt_hdr_parse(adapter, buf, len, &id, &seq, &plen, &payload) != _SUCCESS) {
		parse_fail = 1;
		goto exit;
	}

#ifdef CONFIG_WOWLAN
	if (adapter_to_pwrctl(adapter)->wowlan_mode == _TRUE) {
		bypass = 1;
		ret = _SUCCESS;
		goto exit;
	}
#endif

	ret = rtw_hal_c2h_handler(adapter, id, seq, plen, payload);

exit:
	if (parse_fail)
		RTW_ERR("%s parse fail, buf=%p, len=:%u\n", __func__, buf, len);
	else if (ret != _SUCCESS || bypass || DBG_C2H_PKT_HDL > 0) {
		RTW_PRINT("%s: id=0x%02x, seq=%u, plen=%u, %s %s\n", __func__, id, seq, plen
			, !bypass ? "handle" : "bypass"
			, ret == _SUCCESS ? "ok" : "fail"
		);
		if (DBG_C2H_PKT_HDL >= 2)
			RTW_PRINT_DUMP("dump: ", buf, len);
	}
#endif
}
#endif /* CONFIG_FW_C2H_PKT */

void c2h_iqk_offload(_adapter *adapter, u8 *data, u8 len)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct submit_ctx *iqk_sctx = &hal_data->iqk_sctx;

	RTW_INFO("IQK offload finish in %dms\n", rtw_get_passing_time_ms(iqk_sctx->submit_time));
	if (0)
		RTW_INFO_DUMP("C2H_IQK_FINISH: ", data, len);

	rtw_sctx_done(&iqk_sctx);
}

int c2h_iqk_offload_wait(_adapter *adapter, u32 timeout_ms)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct submit_ctx *iqk_sctx = &hal_data->iqk_sctx;

	iqk_sctx->submit_time = rtw_get_current_time();
	iqk_sctx->timeout_ms = timeout_ms;
	iqk_sctx->status = RTW_SCTX_SUBMITTED;

	return rtw_sctx_wait(iqk_sctx, __func__);
}

#define	GET_C2H_MAC_HIDDEN_RPT_UUID_X(_data)			LE_BITS_TO_1BYTE(((u8 *)(_data)) + 0, 0, 8)
#define	GET_C2H_MAC_HIDDEN_RPT_UUID_Y(_data)			LE_BITS_TO_1BYTE(((u8 *)(_data)) + 1, 0, 8)
#define	GET_C2H_MAC_HIDDEN_RPT_UUID_Z(_data)			LE_BITS_TO_1BYTE(((u8 *)(_data)) + 2, 0, 5)
#define	GET_C2H_MAC_HIDDEN_RPT_UUID_CRC(_data)			LE_BITS_TO_2BYTE(((u8 *)(_data)) + 2, 5, 11)
#define	GET_C2H_MAC_HIDDEN_RPT_HCI_TYPE(_data)			LE_BITS_TO_1BYTE(((u8 *)(_data)) + 4, 0, 4)
#define	GET_C2H_MAC_HIDDEN_RPT_PACKAGE_TYPE(_data)		LE_BITS_TO_1BYTE(((u8 *)(_data)) + 4, 4, 3)
#define	GET_C2H_MAC_HIDDEN_RPT_TR_SWITCH(_data)			LE_BITS_TO_1BYTE(((u8 *)(_data)) + 4, 7, 1)
#define	GET_C2H_MAC_HIDDEN_RPT_WL_FUNC(_data)			LE_BITS_TO_1BYTE(((u8 *)(_data)) + 5, 0, 4)
#define	GET_C2H_MAC_HIDDEN_RPT_HW_STYPE(_data)			LE_BITS_TO_1BYTE(((u8 *)(_data)) + 5, 4, 4)
#define	GET_C2H_MAC_HIDDEN_RPT_BW(_data)				LE_BITS_TO_1BYTE(((u8 *)(_data)) + 6, 0, 3)
#define	GET_C2H_MAC_HIDDEN_RPT_FAB(_data)				LE_BITS_TO_1BYTE(((u8 *)(_data)) + 6, 3, 2)
#define	GET_C2H_MAC_HIDDEN_RPT_ANT_NUM(_data)			LE_BITS_TO_1BYTE(((u8 *)(_data)) + 6, 5, 3)
#define	GET_C2H_MAC_HIDDEN_RPT_80211_PROTOCOL(_data)	LE_BITS_TO_1BYTE(((u8 *)(_data)) + 7, 2, 2)
#define	GET_C2H_MAC_HIDDEN_RPT_NIC_ROUTER(_data)		LE_BITS_TO_1BYTE(((u8 *)(_data)) + 7, 6, 2)

#ifndef DBG_C2H_MAC_HIDDEN_RPT_HANDLE
#define DBG_C2H_MAC_HIDDEN_RPT_HANDLE 0
#endif

#ifdef CONFIG_RTW_MAC_HIDDEN_RPT
int c2h_mac_hidden_rpt_hdl(_adapter *adapter, u8 *data, u8 len)
{
	HAL_DATA_TYPE	*hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	int ret = _FAIL;

	u32 uuid;
	u8 uuid_x;
	u8 uuid_y;
	u8 uuid_z;
	u16 uuid_crc;

	u8 hci_type;
	u8 package_type;
	u8 tr_switch;
	u8 wl_func;
	u8 hw_stype;
	u8 bw;
	u8 fab;
	u8 ant_num;
	u8 protocol;
	u8 nic;

	int i;

	if (len < MAC_HIDDEN_RPT_LEN) {
		RTW_WARN("%s len(%u) < %d\n", __func__, len, MAC_HIDDEN_RPT_LEN);
		goto exit;
	}

	uuid_x = GET_C2H_MAC_HIDDEN_RPT_UUID_X(data);
	uuid_y = GET_C2H_MAC_HIDDEN_RPT_UUID_Y(data);
	uuid_z = GET_C2H_MAC_HIDDEN_RPT_UUID_Z(data);
	uuid_crc = GET_C2H_MAC_HIDDEN_RPT_UUID_CRC(data);

	hci_type = GET_C2H_MAC_HIDDEN_RPT_HCI_TYPE(data);
	package_type = GET_C2H_MAC_HIDDEN_RPT_PACKAGE_TYPE(data);

	tr_switch = GET_C2H_MAC_HIDDEN_RPT_TR_SWITCH(data);

	wl_func = GET_C2H_MAC_HIDDEN_RPT_WL_FUNC(data);
	hw_stype = GET_C2H_MAC_HIDDEN_RPT_HW_STYPE(data);

	bw = GET_C2H_MAC_HIDDEN_RPT_BW(data);
	fab = GET_C2H_MAC_HIDDEN_RPT_FAB(data);
	ant_num = GET_C2H_MAC_HIDDEN_RPT_ANT_NUM(data);

	protocol = GET_C2H_MAC_HIDDEN_RPT_80211_PROTOCOL(data);
	nic = GET_C2H_MAC_HIDDEN_RPT_NIC_ROUTER(data);

	if (DBG_C2H_MAC_HIDDEN_RPT_HANDLE) {
		for (i = 0; i < len; i++)
			RTW_PRINT("%s: 0x%02X\n", __func__, *(data + i));

		RTW_PRINT("uuid x:0x%02x y:0x%02x z:0x%x crc:0x%x\n", uuid_x, uuid_y, uuid_z, uuid_crc);
		RTW_PRINT("hci_type:0x%x\n", hci_type);
		RTW_PRINT("package_type:0x%x\n", package_type);
		RTW_PRINT("tr_switch:0x%x\n", tr_switch);
		RTW_PRINT("wl_func:0x%x\n", wl_func);
		RTW_PRINT("hw_stype:0x%x\n", hw_stype);
		RTW_PRINT("bw:0x%x\n", bw);
		RTW_PRINT("fab:0x%x\n", fab);
		RTW_PRINT("ant_num:0x%x\n", ant_num);
		RTW_PRINT("protocol:0x%x\n", protocol);
		RTW_PRINT("nic:0x%x\n", nic);
	}

	/*
	* NOTICE:
	* for now, the following is common info/format
	* if there is any hal difference need to export
	* some IC dependent code will need to be implement
	*/
	hal_data->PackageType = package_type;
	hal_spec->wl_func &= mac_hidden_wl_func_to_hal_wl_func(wl_func);
	hal_spec->bw_cap &= mac_hidden_max_bw_to_hal_bw_cap(bw);
	hal_spec->tx_nss_num = rtw_min(hal_spec->tx_nss_num, ant_num);
	hal_spec->rx_nss_num = rtw_min(hal_spec->rx_nss_num, ant_num);
	hal_spec->proto_cap &= mac_hidden_proto_to_hal_proto_cap(protocol);
	hal_spec->hci_type = hci_type;

	/* TODO: tr_switch */
	/* TODO: fab */

	ret = _SUCCESS;

exit:
	return ret;
}

int c2h_mac_hidden_rpt_2_hdl(_adapter *adapter, u8 *data, u8 len)
{
	HAL_DATA_TYPE	*hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	int ret = _FAIL;

	int i;

	if (len < MAC_HIDDEN_RPT_2_LEN) {
		RTW_WARN("%s len(%u) < %d\n", __func__, len, MAC_HIDDEN_RPT_2_LEN);
		goto exit;
	}

	if (DBG_C2H_MAC_HIDDEN_RPT_HANDLE) {
		for (i = 0; i < len; i++)
			RTW_PRINT("%s: 0x%02X\n", __func__, *(data + i));
	}

	ret = _SUCCESS;

exit:
	return ret;
}

int hal_read_mac_hidden_rpt(_adapter *adapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(adapter);
	int ret = _FAIL;
	int ret_fwdl;
	u8 mac_hidden_rpt[MAC_HIDDEN_RPT_LEN + MAC_HIDDEN_RPT_2_LEN] = {0};
	u32 start = rtw_get_current_time();
	u32 cnt = 0;
	u32 timeout_ms = 800;
	u32 min_cnt = 10;
	u8 id = C2H_DEFEATURE_RSVD;
	int i;

#if defined(CONFIG_USB_HCI) || defined(CONFIG_PCI_HCI)
	u8 hci_type = rtw_get_intf_type(adapter);

	if ((hci_type == RTW_USB || hci_type == RTW_PCIE)
		&& !rtw_is_hw_init_completed(adapter))
		rtw_hal_power_on(adapter);
#endif

	/* inform FW mac hidden rpt from reg is needed */
	rtw_write8(adapter, REG_C2HEVT_MSG_NORMAL, C2H_DEFEATURE_RSVD);

	/* download FW */
	pHalData->not_xmitframe_fw_dl = 1;
	ret_fwdl = rtw_hal_fw_dl(adapter, _FALSE);
	pHalData->not_xmitframe_fw_dl = 0;
	if (ret_fwdl != _SUCCESS)
		goto mac_hidden_rpt_hdl;

	/* polling for data ready */
	start = rtw_get_current_time();
	do {
		cnt++;
		id = rtw_read8(adapter, REG_C2HEVT_MSG_NORMAL);
		if (id == C2H_MAC_HIDDEN_RPT || RTW_CANNOT_IO(adapter))
			break;
		rtw_msleep_os(10);
	} while (rtw_get_passing_time_ms(start) < timeout_ms || cnt < min_cnt);

	if (id == C2H_MAC_HIDDEN_RPT) {
		/* read data */
		for (i = 0; i < MAC_HIDDEN_RPT_LEN + MAC_HIDDEN_RPT_2_LEN; i++)
			mac_hidden_rpt[i] = rtw_read8(adapter, REG_C2HEVT_MSG_NORMAL + 2 + i);
	}

	/* inform FW mac hidden rpt has read */
	rtw_write8(adapter, REG_C2HEVT_MSG_NORMAL, C2H_DBG);

mac_hidden_rpt_hdl:
	c2h_mac_hidden_rpt_hdl(adapter, mac_hidden_rpt, MAC_HIDDEN_RPT_LEN);
	c2h_mac_hidden_rpt_2_hdl(adapter, mac_hidden_rpt + MAC_HIDDEN_RPT_LEN, MAC_HIDDEN_RPT_2_LEN);

	if (ret_fwdl == _SUCCESS && id == C2H_MAC_HIDDEN_RPT)
		ret = _SUCCESS;

exit:

#if defined(CONFIG_USB_HCI) || defined(CONFIG_PCI_HCI)
	if ((hci_type == RTW_USB || hci_type == RTW_PCIE)
		&& !rtw_is_hw_init_completed(adapter))
		rtw_hal_power_off(adapter);
#endif

	RTW_INFO("%s %s! (%u, %dms), fwdl:%d, id:0x%02x\n", __func__
		, (ret == _SUCCESS) ? "OK" : "Fail", cnt, rtw_get_passing_time_ms(start), ret_fwdl, id);

	return ret;
}
#endif /* CONFIG_RTW_MAC_HIDDEN_RPT */

int c2h_defeature_dbg_hdl(_adapter *adapter, u8 *data, u8 len)
{
	HAL_DATA_TYPE	*hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	int ret = _FAIL;

	int i;

	if (len < DEFEATURE_DBG_LEN) {
		RTW_WARN("%s len(%u) < %d\n", __func__, len, DEFEATURE_DBG_LEN);
		goto exit;
	}

	for (i = 0; i < len; i++)
		RTW_PRINT("%s: 0x%02X\n", __func__, *(data + i));

	ret = _SUCCESS;
	
exit:
	return ret;
}

#ifndef DBG_CUSTOMER_STR_RPT_HANDLE
#define DBG_CUSTOMER_STR_RPT_HANDLE 0
#endif

#ifdef CONFIG_RTW_CUSTOMER_STR
s32 rtw_hal_h2c_customer_str_req(_adapter *adapter)
{
	u8 h2c_data[H2C_CUSTOMER_STR_REQ_LEN] = {0};

	SET_H2CCMD_CUSTOMER_STR_REQ_EN(h2c_data, 1);
	return rtw_hal_fill_h2c_cmd(adapter, H2C_CUSTOMER_STR_REQ, H2C_CUSTOMER_STR_REQ_LEN, h2c_data);
}

#define	C2H_CUSTOMER_STR_RPT_BYTE0(_data)		((u8 *)(_data))
#define	C2H_CUSTOMER_STR_RPT_2_BYTE8(_data)		((u8 *)(_data))

int c2h_customer_str_rpt_hdl(_adapter *adapter, u8 *data, u8 len)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	int ret = _FAIL;
	int i;

	if (len < CUSTOMER_STR_RPT_LEN) {
		RTW_WARN("%s len(%u) < %d\n", __func__, len, CUSTOMER_STR_RPT_LEN);
		goto exit;
	}

	if (DBG_CUSTOMER_STR_RPT_HANDLE)
		RTW_PRINT_DUMP("customer_str_rpt: ", data, CUSTOMER_STR_RPT_LEN);

	_enter_critical_mutex(&dvobj->customer_str_mutex, NULL);

	if (dvobj->customer_str_sctx != NULL) {
		if (dvobj->customer_str_sctx->status != RTW_SCTX_SUBMITTED)
			RTW_WARN("%s invalid sctx.status:%d\n", __func__, dvobj->customer_str_sctx->status);
		_rtw_memcpy(dvobj->customer_str,  C2H_CUSTOMER_STR_RPT_BYTE0(data), CUSTOMER_STR_RPT_LEN);
		dvobj->customer_str_sctx->status = RTX_SCTX_CSTR_WAIT_RPT2;
	} else
		RTW_WARN("%s sctx not set\n", __func__);

	_exit_critical_mutex(&dvobj->customer_str_mutex, NULL);

	ret = _SUCCESS;

exit:
	return ret;
}

int c2h_customer_str_rpt_2_hdl(_adapter *adapter, u8 *data, u8 len)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	int ret = _FAIL;
	int i;

	if (len < CUSTOMER_STR_RPT_2_LEN) {
		RTW_WARN("%s len(%u) < %d\n", __func__, len, CUSTOMER_STR_RPT_2_LEN);
		goto exit;
	}

	if (DBG_CUSTOMER_STR_RPT_HANDLE)
		RTW_PRINT_DUMP("customer_str_rpt_2: ", data, CUSTOMER_STR_RPT_2_LEN);

	_enter_critical_mutex(&dvobj->customer_str_mutex, NULL);

	if (dvobj->customer_str_sctx != NULL) {
		if (dvobj->customer_str_sctx->status != RTX_SCTX_CSTR_WAIT_RPT2)
			RTW_WARN("%s rpt not ready\n", __func__);
		_rtw_memcpy(dvobj->customer_str + CUSTOMER_STR_RPT_LEN,  C2H_CUSTOMER_STR_RPT_2_BYTE8(data), CUSTOMER_STR_RPT_2_LEN);
		rtw_sctx_done(&dvobj->customer_str_sctx);
	} else
		RTW_WARN("%s sctx not set\n", __func__);

	_exit_critical_mutex(&dvobj->customer_str_mutex, NULL);

	ret = _SUCCESS;

exit:
	return ret;
}

/* read customer str */
s32 rtw_hal_customer_str_read(_adapter *adapter, u8 *cs)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct submit_ctx sctx;
	s32 ret = _SUCCESS;

	_enter_critical_mutex(&dvobj->customer_str_mutex, NULL);
	if (dvobj->customer_str_sctx != NULL)
		ret = _FAIL;
	else {
		rtw_sctx_init(&sctx, 2 * 1000);
		dvobj->customer_str_sctx = &sctx;
	}
	_exit_critical_mutex(&dvobj->customer_str_mutex, NULL);

	if (ret == _FAIL) {
		RTW_WARN("%s another handle ongoing\n", __func__);
		goto exit;
	}

	ret = rtw_customer_str_req_cmd(adapter);
	if (ret != _SUCCESS) {
		RTW_WARN("%s read cmd fail\n", __func__);
		_enter_critical_mutex(&dvobj->customer_str_mutex, NULL);
		dvobj->customer_str_sctx = NULL;
		_exit_critical_mutex(&dvobj->customer_str_mutex, NULL);
		goto exit;
	}

	/* wait till rpt done or timeout */
	rtw_sctx_wait(&sctx, __func__);

	_enter_critical_mutex(&dvobj->customer_str_mutex, NULL);
	dvobj->customer_str_sctx = NULL;
	if (sctx.status == RTW_SCTX_DONE_SUCCESS)
		_rtw_memcpy(cs, dvobj->customer_str, RTW_CUSTOMER_STR_LEN);
	else
		ret = _FAIL;
	_exit_critical_mutex(&dvobj->customer_str_mutex, NULL);

exit:
	return ret;
}

s32 rtw_hal_h2c_customer_str_write(_adapter *adapter, const u8 *cs)
{
	u8 h2c_data_w1[H2C_CUSTOMER_STR_W1_LEN] = {0};
	u8 h2c_data_w2[H2C_CUSTOMER_STR_W2_LEN] = {0};
	u8 h2c_data_w3[H2C_CUSTOMER_STR_W3_LEN] = {0};
	s32 ret;

	SET_H2CCMD_CUSTOMER_STR_W1_EN(h2c_data_w1, 1);
	_rtw_memcpy(H2CCMD_CUSTOMER_STR_W1_BYTE0(h2c_data_w1), cs, 6);

	SET_H2CCMD_CUSTOMER_STR_W2_EN(h2c_data_w2, 1);
	_rtw_memcpy(H2CCMD_CUSTOMER_STR_W2_BYTE6(h2c_data_w2), cs + 6, 6);

	SET_H2CCMD_CUSTOMER_STR_W3_EN(h2c_data_w3, 1);
	_rtw_memcpy(H2CCMD_CUSTOMER_STR_W3_BYTE12(h2c_data_w3), cs + 6 + 6, 4);

	ret = rtw_hal_fill_h2c_cmd(adapter, H2C_CUSTOMER_STR_W1, H2C_CUSTOMER_STR_W1_LEN, h2c_data_w1);
	if (ret != _SUCCESS) {
		RTW_WARN("%s w1 fail\n", __func__);
		goto exit;
	}

	ret = rtw_hal_fill_h2c_cmd(adapter, H2C_CUSTOMER_STR_W2, H2C_CUSTOMER_STR_W2_LEN, h2c_data_w2);
	if (ret != _SUCCESS) {
		RTW_WARN("%s w2 fail\n", __func__);
		goto exit;
	}

	ret = rtw_hal_fill_h2c_cmd(adapter, H2C_CUSTOMER_STR_W3, H2C_CUSTOMER_STR_W3_LEN, h2c_data_w3);
	if (ret != _SUCCESS) {
		RTW_WARN("%s w3 fail\n", __func__);
		goto exit;
	}

exit:
	return ret;
}

/* write customer str and check if value reported is the same as requested */
s32 rtw_hal_customer_str_write(_adapter *adapter, const u8 *cs)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct submit_ctx sctx;
	s32 ret = _SUCCESS;

	_enter_critical_mutex(&dvobj->customer_str_mutex, NULL);
	if (dvobj->customer_str_sctx != NULL)
		ret = _FAIL;
	else {
		rtw_sctx_init(&sctx, 2 * 1000);
		dvobj->customer_str_sctx = &sctx;
	}
	_exit_critical_mutex(&dvobj->customer_str_mutex, NULL);

	if (ret == _FAIL) {
		RTW_WARN("%s another handle ongoing\n", __func__);
		goto exit;
	}

	ret = rtw_customer_str_write_cmd(adapter, cs);
	if (ret != _SUCCESS) {
		RTW_WARN("%s write cmd fail\n", __func__);
		_enter_critical_mutex(&dvobj->customer_str_mutex, NULL);
		dvobj->customer_str_sctx = NULL;
		_exit_critical_mutex(&dvobj->customer_str_mutex, NULL);
		goto exit;
	}

	ret = rtw_customer_str_req_cmd(adapter);
	if (ret != _SUCCESS) {
		RTW_WARN("%s read cmd fail\n", __func__);
		_enter_critical_mutex(&dvobj->customer_str_mutex, NULL);
		dvobj->customer_str_sctx = NULL;
		_exit_critical_mutex(&dvobj->customer_str_mutex, NULL);
		goto exit;
	}

	/* wait till rpt done or timeout */
	rtw_sctx_wait(&sctx, __func__);

	_enter_critical_mutex(&dvobj->customer_str_mutex, NULL);
	dvobj->customer_str_sctx = NULL;
	if (sctx.status == RTW_SCTX_DONE_SUCCESS) {
		if (_rtw_memcmp(cs, dvobj->customer_str, RTW_CUSTOMER_STR_LEN) != _TRUE) {
			RTW_WARN("%s read back check fail\n", __func__);
			RTW_INFO_DUMP("write req: ", cs, RTW_CUSTOMER_STR_LEN);
			RTW_INFO_DUMP("read back: ", dvobj->customer_str, RTW_CUSTOMER_STR_LEN);
			ret = _FAIL;
		}
	} else
		ret = _FAIL;
	_exit_critical_mutex(&dvobj->customer_str_mutex, NULL);

exit:
	return ret;
}
#endif /* CONFIG_RTW_CUSTOMER_STR */

u8  rtw_hal_networktype_to_raid(_adapter *adapter, struct sta_info *psta)
{
#ifdef CONFIG_GET_RAID_BY_DRV	/*Just for 8188E now*/
	if (IS_NEW_GENERATION_IC(adapter))
		return networktype_to_raid_ex(adapter, psta);
	else
		return networktype_to_raid(adapter, psta);
#else
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(adapter);
	u8 bw;

	bw = rtw_get_tx_bw_mode(adapter, psta);

	return phydm_rate_id_mapping(&pHalData->odmpriv, psta->wireless_mode, pHalData->rf_type, bw);
#endif
}
u8 rtw_get_mgntframe_raid(_adapter *adapter, unsigned char network_type)
{

	u8 raid;
	if (IS_NEW_GENERATION_IC(adapter)) {

		raid = (network_type & WIRELESS_11B)	? RATEID_IDX_B
		       : RATEID_IDX_G;
	} else {
		raid = (network_type & WIRELESS_11B)	? RATR_INX_WIRELESS_B
		       : RATR_INX_WIRELESS_G;
	}
	return raid;
}

void rtw_hal_update_sta_rate_mask(PADAPTER padapter, struct sta_info *psta)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(padapter);
	u8 i, rf_type, tx_nss;
	u64	tx_ra_bitmap;

	if (psta == NULL)
		return;

	tx_ra_bitmap = 0;

	/* b/g mode ra_bitmap  */
	for (i = 0; i < sizeof(psta->bssrateset); i++) {
		if (psta->bssrateset[i])
			tx_ra_bitmap |= rtw_get_bit_value_from_ieee_value(psta->bssrateset[i] & 0x7f);
	}

#ifdef CONFIG_80211N_HT
	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
	tx_nss = rtw_min(rf_type_to_rf_tx_cnt(rf_type), hal_spec->tx_nss_num);
#ifdef CONFIG_80211AC_VHT
	if (psta->vhtpriv.vht_option) {
		/* AC mode ra_bitmap */
		tx_ra_bitmap |= (rtw_vht_mcs_map_to_bitmap(psta->vhtpriv.vht_mcs_map, tx_nss) << 12);
	} else
#endif /* CONFIG_80211AC_VHT */
	if (psta->htpriv.ht_option) {
		/* n mode ra_bitmap */

		/* Handling SMPS mode for AP MODE only*/
		if (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == _TRUE) {
			/*0:static SMPS, 1:dynamic SMPS, 3:SMPS disabled, 2:reserved*/
			if (psta->htpriv.smps_cap == 0 || psta->htpriv.smps_cap == 1) {
				/*operate with only one active receive chain // 11n-MCS rate <= MSC7*/
				tx_nss = rtw_min(tx_nss, 1);
			}
		}

		tx_ra_bitmap |= (rtw_ht_mcs_set_to_bitmap(psta->htpriv.ht_cap.supp_mcs_set, tx_nss) << 12);
	}
#endif /* CONFIG_80211N_HT */
	psta->ra_mask = tx_ra_bitmap;
	psta->init_rate = get_highest_rate_idx(tx_ra_bitmap) & 0x3f;
}

#ifndef SEC_CAM_ACCESS_TIMEOUT_MS
	#define SEC_CAM_ACCESS_TIMEOUT_MS 200
#endif

#ifndef DBG_SEC_CAM_ACCESS
	#define DBG_SEC_CAM_ACCESS 0
#endif

u32 rtw_sec_read_cam(_adapter *adapter, u8 addr)
{
	_mutex *mutex = &adapter_to_dvobj(adapter)->cam_ctl.sec_cam_access_mutex;
	u32 rdata;
	u32 cnt = 0;
	u32 start = 0, end = 0;
	u8 timeout = 0;
	u8 sr = 0;

	_enter_critical_mutex(mutex, NULL);

	rtw_write32(adapter, REG_CAMCMD, CAM_POLLINIG | addr);

	start = rtw_get_current_time();
	while (1) {
		if (rtw_is_surprise_removed(adapter)) {
			sr = 1;
			break;
		}

		cnt++;
		if (0 == (rtw_read32(adapter, REG_CAMCMD) & CAM_POLLINIG))
			break;

		if (rtw_get_passing_time_ms(start) > SEC_CAM_ACCESS_TIMEOUT_MS) {
			timeout = 1;
			break;
		}
	}
	end = rtw_get_current_time();

	rdata = rtw_read32(adapter, REG_CAMREAD);

	_exit_critical_mutex(mutex, NULL);

	if (DBG_SEC_CAM_ACCESS || timeout) {
		RTW_INFO(FUNC_ADPT_FMT" addr:0x%02x, rdata:0x%08x, to:%u, polling:%u, %d ms\n"
			, FUNC_ADPT_ARG(adapter), addr, rdata, timeout, cnt, rtw_get_time_interval_ms(start, end));
	}

	return rdata;
}

void rtw_sec_write_cam(_adapter *adapter, u8 addr, u32 wdata)
{
	_mutex *mutex = &adapter_to_dvobj(adapter)->cam_ctl.sec_cam_access_mutex;
	u32 cnt = 0;
	u32 start = 0, end = 0;
	u8 timeout = 0;
	u8 sr = 0;

	_enter_critical_mutex(mutex, NULL);

	rtw_write32(adapter, REG_CAMWRITE, wdata);
	rtw_write32(adapter, REG_CAMCMD, CAM_POLLINIG | CAM_WRITE | addr);

	start = rtw_get_current_time();
	while (1) {
		if (rtw_is_surprise_removed(adapter)) {
			sr = 1;
			break;
		}

		cnt++;
		if (0 == (rtw_read32(adapter, REG_CAMCMD) & CAM_POLLINIG))
			break;

		if (rtw_get_passing_time_ms(start) > SEC_CAM_ACCESS_TIMEOUT_MS) {
			timeout = 1;
			break;
		}
	}
	end = rtw_get_current_time();

	_exit_critical_mutex(mutex, NULL);

	if (DBG_SEC_CAM_ACCESS || timeout) {
		RTW_INFO(FUNC_ADPT_FMT" addr:0x%02x, wdata:0x%08x, to:%u, polling:%u, %d ms\n"
			, FUNC_ADPT_ARG(adapter), addr, wdata, timeout, cnt, rtw_get_time_interval_ms(start, end));
	}
}

void rtw_sec_read_cam_ent(_adapter *adapter, u8 id, u8 *ctrl, u8 *mac, u8 *key)
{
	unsigned int val, addr;
	u8 i;
	u32 rdata;
	u8 begin = 0;
	u8 end = 5; /* TODO: consider other key length accordingly */

	if (!ctrl && !mac && !key) {
		rtw_warn_on(1);
		goto exit;
	}

	/* TODO: check id range */

	if (!ctrl && !mac)
		begin = 2; /* read from key */

	if (!key && !mac)
		end = 0; /* read to ctrl */
	else if (!key)
		end = 2; /* read to mac */

	for (i = begin; i <= end; i++) {
		rdata = rtw_sec_read_cam(adapter, (id << 3) | i);

		switch (i) {
		case 0:
			if (ctrl)
				_rtw_memcpy(ctrl, (u8 *)(&rdata), 2);
			if (mac)
				_rtw_memcpy(mac, ((u8 *)(&rdata)) + 2, 2);
			break;
		case 1:
			if (mac)
				_rtw_memcpy(mac + 2, (u8 *)(&rdata), 4);
			break;
		default:
			if (key)
				_rtw_memcpy(key + (i - 2) * 4, (u8 *)(&rdata), 4);
			break;
		}
	}

exit:
	return;
}


void rtw_sec_write_cam_ent(_adapter *adapter, u8 id, u16 ctrl, u8 *mac, u8 *key)
{
	unsigned int i;
	int j;
	u8 addr;
	u32 wdata;

	/* TODO: consider other key length accordingly */
#if 0
	switch ((ctrl & 0x1c) >> 2) {
	case _WEP40_:
	case _TKIP_:
	case _AES_:
	case _WEP104_:

	}
#else
	j = 7;
#endif

	for (; j >= 0; j--) {
		switch (j) {
		case 0:
			wdata = (ctrl | (mac[0] << 16) | (mac[1] << 24));
			break;
		case 1:
			wdata = (mac[2] | (mac[3] << 8) | (mac[4] << 16) | (mac[5] << 24));
			break;
		case 6:
		case 7:
			wdata = 0;
			break;
		default:
			i = (j - 2) << 2;
			wdata = (key[i] | (key[i + 1] << 8) | (key[i + 2] << 16) | (key[i + 3] << 24));
			break;
		}

		addr = (id << 3) + j;

		rtw_sec_write_cam(adapter, addr, wdata);
	}
}

void rtw_sec_clr_cam_ent(_adapter *adapter, u8 id)
{
	u8 addr;

	addr = (id << 3);
	rtw_sec_write_cam(adapter, addr, 0);
}

bool rtw_sec_read_cam_is_gk(_adapter *adapter, u8 id)
{
	bool res;
	u16 ctrl;

	rtw_sec_read_cam_ent(adapter, id, (u8 *)&ctrl, NULL, NULL);

	res = (ctrl & BIT6) ? _TRUE : _FALSE;
	return res;
}
#ifdef CONFIG_MBSSID_CAM
void rtw_mbid_cam_init(struct dvobj_priv *dvobj)
{
	struct mbid_cam_ctl_t *mbid_cam_ctl = &dvobj->mbid_cam_ctl;

	_rtw_spinlock_init(&mbid_cam_ctl->lock);
	mbid_cam_ctl->bitmap = 0;
	ATOMIC_SET(&mbid_cam_ctl->mbid_entry_num, 0);
	_rtw_memset(&dvobj->mbid_cam_cache, 0, sizeof(dvobj->mbid_cam_cache));
}

void rtw_mbid_cam_deinit(struct dvobj_priv *dvobj)
{
	struct mbid_cam_ctl_t *mbid_cam_ctl = &dvobj->mbid_cam_ctl;

	_rtw_spinlock_free(&mbid_cam_ctl->lock);
}

void rtw_mbid_cam_reset(_adapter *adapter)
{
	_irqL irqL;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mbid_cam_ctl_t *mbid_cam_ctl = &dvobj->mbid_cam_ctl;

	_enter_critical_bh(&mbid_cam_ctl->lock, &irqL);
	mbid_cam_ctl->bitmap = 0;
	_rtw_memset(&dvobj->mbid_cam_cache, 0, sizeof(dvobj->mbid_cam_cache));
	_exit_critical_bh(&mbid_cam_ctl->lock, &irqL);

	ATOMIC_SET(&mbid_cam_ctl->mbid_entry_num, 0);
}
static u8 _rtw_mbid_cam_search_by_macaddr(_adapter *adapter, u8 *mac_addr)
{
	u8 i;
	u8 cam_id = INVALID_CAM_ID;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);

	for (i = 0; i < TOTAL_MBID_CAM_NUM; i++) {
		if (mac_addr && _rtw_memcmp(dvobj->mbid_cam_cache[i].mac_addr, mac_addr, ETH_ALEN) == _TRUE) {
			cam_id = i;
			break;
		}
	}

	RTW_INFO("%s mac:"MAC_FMT" - cam_id:%d\n", __func__, MAC_ARG(mac_addr), cam_id);
	return cam_id;
}

u8 rtw_mbid_cam_search_by_macaddr(_adapter *adapter, u8 *mac_addr)
{
	_irqL irqL;

	u8 cam_id = INVALID_CAM_ID;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mbid_cam_ctl_t *mbid_cam_ctl = &dvobj->mbid_cam_ctl;

	_enter_critical_bh(&mbid_cam_ctl->lock, &irqL);
	cam_id = _rtw_mbid_cam_search_by_macaddr(adapter, mac_addr);
	_exit_critical_bh(&mbid_cam_ctl->lock, &irqL);

	return cam_id;
}
static u8 _rtw_mbid_cam_search_by_ifaceid(_adapter *adapter, u8 iface_id)
{
	u8 i;
	u8 cam_id = INVALID_CAM_ID;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);

	for (i = 0; i < TOTAL_MBID_CAM_NUM; i++) {
		if (iface_id == dvobj->mbid_cam_cache[i].iface_id) {
			cam_id = i;
			break;
		}
	}
	if (cam_id != INVALID_CAM_ID)
		RTW_INFO("%s iface_id:%d mac:"MAC_FMT" - cam_id:%d\n",
			__func__, iface_id, MAC_ARG(dvobj->mbid_cam_cache[cam_id].mac_addr), cam_id);

	return cam_id;
}

u8 rtw_mbid_cam_search_by_ifaceid(_adapter *adapter, u8 iface_id)
{
	_irqL irqL;
	u8 cam_id = INVALID_CAM_ID;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mbid_cam_ctl_t *mbid_cam_ctl = &dvobj->mbid_cam_ctl;

	_enter_critical_bh(&mbid_cam_ctl->lock, &irqL);
	cam_id = _rtw_mbid_cam_search_by_ifaceid(adapter, iface_id);
	_exit_critical_bh(&mbid_cam_ctl->lock, &irqL);

	return cam_id;
}
u8 rtw_get_max_mbid_cam_id(_adapter *adapter)
{
	_irqL irqL;
	s8 i;
	u8 cam_id = INVALID_CAM_ID;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mbid_cam_ctl_t *mbid_cam_ctl = &dvobj->mbid_cam_ctl;

	_enter_critical_bh(&mbid_cam_ctl->lock, &irqL);
	for (i = (TOTAL_MBID_CAM_NUM - 1); i >= 0; i--) {
		if (mbid_cam_ctl->bitmap & BIT(i)) {
			cam_id = i;
			break;
		}
	}
	_exit_critical_bh(&mbid_cam_ctl->lock, &irqL);
	/*RTW_INFO("%s max cam_id:%d\n", __func__, cam_id);*/
	return cam_id;
}

inline u8 rtw_get_mbid_cam_entry_num(_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mbid_cam_ctl_t *mbid_cam_ctl = &dvobj->mbid_cam_ctl;

	return ATOMIC_READ(&mbid_cam_ctl->mbid_entry_num);
}

static inline void mbid_cam_cache_init(_adapter *adapter, struct mbid_cam_cache *pmbid_cam, u8 *mac_addr)
{
	if (adapter && pmbid_cam && mac_addr) {
		_rtw_memcpy(pmbid_cam->mac_addr, mac_addr, ETH_ALEN);
		pmbid_cam->iface_id = adapter->iface_id;
	}
}
static inline void mbid_cam_cache_clr(struct mbid_cam_cache *pmbid_cam)
{
	if (pmbid_cam) {
		_rtw_memset(pmbid_cam->mac_addr, 0, ETH_ALEN);
		pmbid_cam->iface_id = CONFIG_IFACE_NUMBER;
	}
}

u8 rtw_mbid_camid_alloc(_adapter *adapter, u8 *mac_addr)
{
	_irqL irqL;
	u8 cam_id = INVALID_CAM_ID, i;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mbid_cam_ctl_t *mbid_cam_ctl = &dvobj->mbid_cam_ctl;
	u8 entry_num = ATOMIC_READ(&mbid_cam_ctl->mbid_entry_num);

	if (entry_num >= TOTAL_MBID_CAM_NUM) {
		RTW_INFO(FUNC_ADPT_FMT" failed !! MBSSID number :%d over TOTAL_CAM_ENTRY(8)\n", FUNC_ADPT_ARG(adapter), entry_num);
		rtw_warn_on(1);
	}

	if (INVALID_CAM_ID != rtw_mbid_cam_search_by_macaddr(adapter, mac_addr))
		goto exit;

	_enter_critical_bh(&mbid_cam_ctl->lock, &irqL);
	for (i = 0; i < TOTAL_MBID_CAM_NUM; i++) {
		if (!(mbid_cam_ctl->bitmap & BIT(i))) {
			mbid_cam_ctl->bitmap |= BIT(i);
			cam_id = i;
			break;
		}
	}
	if ((cam_id != INVALID_CAM_ID) && (mac_addr))
		mbid_cam_cache_init(adapter, &dvobj->mbid_cam_cache[cam_id], mac_addr);
	_exit_critical_bh(&mbid_cam_ctl->lock, &irqL);

	if (cam_id != INVALID_CAM_ID) {
		ATOMIC_INC(&mbid_cam_ctl->mbid_entry_num);
		RTW_INFO("%s mac:"MAC_FMT" - cam_id:%d\n", __func__, MAC_ARG(mac_addr), cam_id);
#ifdef DBG_MBID_CAM_DUMP
		rtw_mbid_cam_cache_dump(RTW_DBGDUMP, __func__, adapter);
#endif
	} else
		RTW_INFO("%s [WARN] "MAC_FMT" - invalid cam_id:%d\n", __func__, MAC_ARG(mac_addr), cam_id);
exit:
	return cam_id;
}

u8 rtw_mbid_cam_info_change(_adapter *adapter, u8 *mac_addr)
{
	_irqL irqL;
	u8 entry_id = INVALID_CAM_ID;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mbid_cam_ctl_t *mbid_cam_ctl = &dvobj->mbid_cam_ctl;

	_enter_critical_bh(&mbid_cam_ctl->lock, &irqL);
	entry_id = _rtw_mbid_cam_search_by_ifaceid(adapter, adapter->iface_id);
	if (entry_id != INVALID_CAM_ID)
		mbid_cam_cache_init(adapter, &dvobj->mbid_cam_cache[entry_id], mac_addr);

	_exit_critical_bh(&mbid_cam_ctl->lock, &irqL);

	return entry_id;
}

u8 rtw_mbid_cam_assign(_adapter *adapter, u8 *mac_addr, u8 camid)
{
	_irqL irqL;
	u8 ret = _FALSE;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mbid_cam_ctl_t *mbid_cam_ctl = &dvobj->mbid_cam_ctl;

	if ((camid >= TOTAL_MBID_CAM_NUM) || (camid == INVALID_CAM_ID)) {
		RTW_INFO(FUNC_ADPT_FMT" failed !! invlaid mbid_canid :%d\n", FUNC_ADPT_ARG(adapter), camid);
		rtw_warn_on(1);
	}
	if (INVALID_CAM_ID != rtw_mbid_cam_search_by_macaddr(adapter, mac_addr))
		goto exit;

	_enter_critical_bh(&mbid_cam_ctl->lock, &irqL);
	if (!(mbid_cam_ctl->bitmap & BIT(camid))) {
		if (mac_addr) {
			mbid_cam_ctl->bitmap |= BIT(camid);
			mbid_cam_cache_init(adapter, &dvobj->mbid_cam_cache[camid], mac_addr);
			ret = _TRUE;
		}
	}
	_exit_critical_bh(&mbid_cam_ctl->lock, &irqL);

	if (ret == _TRUE) {
		ATOMIC_INC(&mbid_cam_ctl->mbid_entry_num);
		RTW_INFO("%s mac:"MAC_FMT" - cam_id:%d\n", __func__, MAC_ARG(mac_addr), camid);
#ifdef DBG_MBID_CAM_DUMP
		rtw_mbid_cam_cache_dump(RTW_DBGDUMP, __func__, adapter);
#endif
	} else
		RTW_INFO("%s  [WARN] mac:"MAC_FMT" - cam_id:%d assigned failed\n", __func__, MAC_ARG(mac_addr), camid);

exit:
	return ret;
}

void rtw_mbid_camid_clean(_adapter *adapter, u8 mbss_canid)
{
	_irqL irqL;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mbid_cam_ctl_t *mbid_cam_ctl = &dvobj->mbid_cam_ctl;

	if ((mbss_canid >= TOTAL_MBID_CAM_NUM) || (mbss_canid == INVALID_CAM_ID)) {
		RTW_INFO(FUNC_ADPT_FMT" failed !! invlaid mbid_canid :%d\n", FUNC_ADPT_ARG(adapter), mbss_canid);
		rtw_warn_on(1);
	}
	_enter_critical_bh(&mbid_cam_ctl->lock, &irqL);
	mbid_cam_cache_clr(&dvobj->mbid_cam_cache[mbss_canid]);
	mbid_cam_ctl->bitmap &= (~BIT(mbss_canid));
	_exit_critical_bh(&mbid_cam_ctl->lock, &irqL);
	ATOMIC_DEC(&mbid_cam_ctl->mbid_entry_num);
	RTW_INFO("%s - cam_id:%d\n", __func__, mbss_canid);
}
int rtw_mbid_cam_cache_dump(void *sel, const char *fun_name, _adapter *adapter)
{
	_irqL irqL;
	u8 i;
	_adapter *iface;
	u8 iface_id;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mbid_cam_ctl_t *mbid_cam_ctl = &dvobj->mbid_cam_ctl;
	u8 entry_num = ATOMIC_READ(&mbid_cam_ctl->mbid_entry_num);
	u8 max_cam_id = rtw_get_max_mbid_cam_id(adapter);

	RTW_PRINT_SEL(sel, "== MBSSID CAM DUMP (%s)==\n", fun_name);

	_enter_critical_bh(&mbid_cam_ctl->lock, &irqL);
	RTW_PRINT_SEL(sel, "Entry numbers:%d, max_camid:%d, bitmap:0x%08x\n", entry_num, max_cam_id, mbid_cam_ctl->bitmap);
	for (i = 0; i < TOTAL_MBID_CAM_NUM; i++) {
		RTW_PRINT_SEL(sel, "CAM_ID = %d\t", i);

		if (mbid_cam_ctl->bitmap & BIT(i)) {
			iface_id = dvobj->mbid_cam_cache[i].iface_id;
			RTW_PRINT_SEL(sel, "IF_ID:%d\t", iface_id);
			RTW_PRINT_SEL(sel, "MAC Addr:"MAC_FMT"\t", MAC_ARG(dvobj->mbid_cam_cache[i].mac_addr));

			iface = dvobj->padapters[iface_id];
			if (iface) {
				if (check_fwstate(&iface->mlmepriv, WIFI_STATION_STATE) == _TRUE)
					RTW_PRINT_SEL(sel, "ROLE:%s\n", "STA");
				else if (check_fwstate(&iface->mlmepriv, WIFI_AP_STATE) == _TRUE)
					RTW_PRINT_SEL(sel, "ROLE:%s\n", "AP");
				else
					RTW_PRINT_SEL(sel, "ROLE:%s\n", "NONE");
			}

		} else
			RTW_PRINT_SEL(sel, "N/A\n");
	}
	_exit_critical_bh(&mbid_cam_ctl->lock, &irqL);
	return 0;
}

static void read_mbssid_cam(_adapter *padapter, u8 cam_addr, u8 *mac)
{
	u8 poll = 1;
	u8 cam_ready = _FALSE;
	u32 cam_data1 = 0;
	u16 cam_data2 = 0;

	if (RTW_CANNOT_RUN(padapter))
		return;

	rtw_write32(padapter, REG_MBIDCAMCFG_2, BIT_MBIDCAM_POLL | ((cam_addr & MBIDCAM_ADDR_MASK) << MBIDCAM_ADDR_SHIFT));

	do {
		if (0 == (rtw_read32(padapter, REG_MBIDCAMCFG_2) & BIT_MBIDCAM_POLL)) {
			cam_ready = _TRUE;
			break;
		}
		poll++;
	} while ((poll % 10) != 0 && !RTW_CANNOT_RUN(padapter));

	if (cam_ready) {
		cam_data1 = rtw_read32(padapter, REG_MBIDCAMCFG_1);
		mac[0] = cam_data1 & 0xFF;
		mac[1] = (cam_data1 >> 8) & 0xFF;
		mac[2] = (cam_data1 >> 16) & 0xFF;
		mac[3] = (cam_data1 >> 24) & 0xFF;

		cam_data2 = rtw_read16(padapter, REG_MBIDCAMCFG_2);
		mac[4] = cam_data2 & 0xFF;
		mac[5] = (cam_data2 >> 8) & 0xFF;
	}

}
int rtw_mbid_cam_dump(void *sel, const char *fun_name, _adapter *adapter)
{
	/*_irqL irqL;*/
	u8 i;
	u8 mac_addr[ETH_ALEN];

	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mbid_cam_ctl_t *mbid_cam_ctl = &dvobj->mbid_cam_ctl;

	RTW_PRINT_SEL(sel, "\n== MBSSID HW-CAM DUMP (%s)==\n", fun_name);

	/*_enter_critical_bh(&mbid_cam_ctl->lock, &irqL);*/
	for (i = 0; i < TOTAL_MBID_CAM_NUM; i++) {
		RTW_PRINT_SEL(sel, "CAM_ID = %d\t", i);
		_rtw_memset(mac_addr, 0, ETH_ALEN);
		read_mbssid_cam(adapter, i, mac_addr);
		RTW_PRINT_SEL(sel, "MAC Addr:"MAC_FMT"\n", MAC_ARG(mac_addr));
	}
	/*_exit_critical_bh(&mbid_cam_ctl->lock, &irqL);*/
	return 0;
}

static void write_mbssid_cam(_adapter *padapter, u8 cam_addr, u8 *mac)
{
	u32	cam_val[2] = {0};

	cam_val[0] = (mac[3] << 24) | (mac[2] << 16) | (mac[1] << 8) | mac[0];
	cam_val[1] = ((cam_addr & MBIDCAM_ADDR_MASK) << MBIDCAM_ADDR_SHIFT)  | (mac[5] << 8) | mac[4];

	rtw_hal_set_hwreg(padapter, HW_VAR_MBSSID_CAM_WRITE, (u8 *)cam_val);
}

static void clear_mbssid_cam(_adapter *padapter, u8 cam_addr)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_MBSSID_CAM_CLEAR, &cam_addr);
}
static void enable_mbssid_cam(_adapter *adapter)
{
	u8 max_cam_id = rtw_get_max_mbid_cam_id(adapter);
	/*enable MBSSID*/
	rtw_write32(adapter, REG_RCR, rtw_read32(adapter, REG_RCR) | RCR_ENMBID);
	if (max_cam_id != INVALID_CAM_ID) {
		rtw_write8(adapter, REG_MBID_NUM,
			((rtw_read8(adapter, REG_MBID_NUM) & 0xF8) | (max_cam_id & 0x07)));
	}
}
void rtw_mbid_cam_restore(_adapter *adapter)
{
	u8 i;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mbid_cam_ctl_t *mbid_cam_ctl = &dvobj->mbid_cam_ctl;

#ifdef DBG_MBID_CAM_DUMP
	rtw_mbid_cam_cache_dump(RTW_DBGDUMP, __func__, adapter);
#endif

	for (i = 0; i < TOTAL_MBID_CAM_NUM; i++) {
		if (mbid_cam_ctl->bitmap & BIT(i)) {
			write_mbssid_cam(adapter, i, dvobj->mbid_cam_cache[i].mac_addr);
			RTW_INFO("%s - cam_id:%d => mac:"MAC_FMT"\n", __func__, i, MAC_ARG(dvobj->mbid_cam_cache[i].mac_addr));
		}
	}
	enable_mbssid_cam(adapter);
}
#endif /*CONFIG_MBSSID_CAM*/

#ifdef CONFIG_MI_WITH_MBSSID_CAM
void rtw_hal_set_macaddr_mbid(_adapter *adapter, u8 *mac_addr)
{

#if 0 /*TODO - modify for more flexible*/
	u8 idx = 0;

	if ((check_fwstate(&adapter->mlmepriv, WIFI_STATION_STATE) == _TRUE) &&
	    (DEV_STA_NUM(adapter_to_dvobj(adapter)) == 1)) {
		for (idx = 0; idx < 6; idx++)
			rtw_write8(GET_PRIMARY_ADAPTER(adapter), (REG_MACID + idx), val[idx]);
	}  else {
		/*MBID entry_id = 0~7 ,0 for root AP, 1~7 for VAP*/
		u8 entry_id;

		if ((check_fwstate(&adapter->mlmepriv, WIFI_AP_STATE) == _TRUE) &&
		    (DEV_AP_NUM(adapter_to_dvobj(adapter)) == 1)) {
			entry_id = 0;
			if (rtw_mbid_cam_assign(adapter, val, entry_id)) {
				RTW_INFO(FUNC_ADPT_FMT" Root AP assigned success\n", FUNC_ADPT_ARG(adapter));
				write_mbssid_cam(adapter, entry_id, val);
			}
		} else {
			entry_id = rtw_mbid_camid_alloc(adapter, val);
			if (entry_id != INVALID_CAM_ID)
				write_mbssid_cam(adapter, entry_id, val);
		}
	}
#else
	{
		/*
			MBID entry_id = 0~7 ,for IFACE_ID0 ~ IFACE_IDx
		*/
		u8 entry_id = rtw_mbid_camid_alloc(adapter, mac_addr);


		if (entry_id != INVALID_CAM_ID) {
			write_mbssid_cam(adapter, entry_id, mac_addr);
			enable_mbssid_cam(adapter);
		}
	}
#endif
}

void rtw_hal_change_macaddr_mbid(_adapter *adapter, u8 *mac_addr)
{
	u8 idx = 0;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	u8 entry_id;

	if (!mac_addr) {
		rtw_warn_on(1);
		return;
	}


	entry_id = rtw_mbid_cam_info_change(adapter, mac_addr);

	if (entry_id != INVALID_CAM_ID)
		write_mbssid_cam(adapter, entry_id, mac_addr);
}


#endif/*#ifdef CONFIG_MI_WITH_MBSSID_CAM*/

void rtw_hal_set_macaddr_port(_adapter *adapter, u8 *val)
{
	u8 idx = 0;
	u32 reg_macid = 0;

	if (val == NULL)
		return;

	RTW_INFO("%s "ADPT_FMT"- hw port(%d) mac_addr ="MAC_FMT"\n",  __func__,
		 ADPT_ARG(adapter), adapter->hw_port, MAC_ARG(val));

	switch (adapter->hw_port) {
	case HW_PORT0:
	default:
		reg_macid = REG_MACID;
		break;
	case HW_PORT1:
		reg_macid = REG_MACID1;
		break;
#if defined(CONFIG_RTL8814A)
	case HW_PORT2:
		reg_macid = REG_MACID2;
		break;
	case HW_PORT3:
		reg_macid = REG_MACID3;
		break;
	case HW_PORT4:
		reg_macid = REG_MACID4;
		break;
#endif/*defined(CONFIG_RTL8814A)*/
	}

	for (idx = 0; idx < 6; idx++)
		rtw_write8(GET_PRIMARY_ADAPTER(adapter), (reg_macid + idx), val[idx]);
}
void rtw_hal_get_macaddr_port(_adapter *adapter, u8 *mac_addr)
{
	u8 idx = 0;
	u32 reg_macid = 0;

	if (mac_addr == NULL)
		return;

	_rtw_memset(mac_addr, 0, ETH_ALEN);
	switch (adapter->hw_port) {
	case HW_PORT0:
	default:
		reg_macid = REG_MACID;
		break;
	case HW_PORT1:
		reg_macid = REG_MACID1;
		break;
#if defined(CONFIG_RTL8814A)
	case HW_PORT2:
		reg_macid = REG_MACID2;
		break;
	case HW_PORT3:
		reg_macid = REG_MACID3;
		break;
	case HW_PORT4:
		reg_macid = REG_MACID4;
		break;
#endif /*defined(CONFIG_RTL8814A)*/
	}

	for (idx = 0; idx < 6; idx++)
		mac_addr[idx] = rtw_read8(GET_PRIMARY_ADAPTER(adapter), (reg_macid + idx));

	RTW_INFO("%s "ADPT_FMT"- hw port(%d) mac_addr ="MAC_FMT"\n",  __func__,
		 ADPT_ARG(adapter), adapter->hw_port, MAC_ARG(mac_addr));
}

void rtw_hal_set_bssid(_adapter *adapter, u8 *val)
{
	u8	idx = 0;
	u32 reg_bssid = 0;

	switch (adapter->hw_port) {
	case HW_PORT0:
	default:
		reg_bssid = REG_BSSID;
		break;
	case HW_PORT1:
		reg_bssid = REG_BSSID1;
		break;
#if defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8822B)
	case HW_PORT2:
		reg_bssid = REG_BSSID2;
		break;
	case HW_PORT3:
		reg_bssid = REG_BSSID3;
		break;
	case HW_PORT4:
		reg_bssid = REG_BSSID4;
		break;
#endif/*defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8822B)*/
	}

	for (idx = 0 ; idx < 6; idx++)
		rtw_write8(adapter, (reg_bssid + idx), val[idx]);

	RTW_INFO("%s "ADPT_FMT"- hw port -%d BSSID: "MAC_FMT"\n", __func__, ADPT_ARG(adapter), adapter->hw_port, MAC_ARG(val));
}

void rtw_hal_get_msr(_adapter *adapter, u8 *net_type)
{
	switch (adapter->hw_port) {
	case HW_PORT0:
		/*REG_CR - BIT[17:16]-Network Type for port 1*/
		*net_type = rtw_read8(adapter, MSR) & 0x03;
		break;
	case HW_PORT1:
		/*REG_CR - BIT[19:18]-Network Type for port 1*/
		*net_type = (rtw_read8(adapter, MSR) & 0x0C) >> 2;
		break;
#if defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8822B)
	case HW_PORT2:
		/*REG_CR_EXT- BIT[1:0]-Network Type for port 2*/
		*net_type = rtw_read8(adapter, MSR1) & 0x03;
		break;
	case HW_PORT3:
		/*REG_CR_EXT- BIT[3:2]-Network Type for port 3*/
		*net_type = (rtw_read8(adapter, MSR1) & 0x0C) >> 2;
		break;
	case HW_PORT4:
		/*REG_CR_EXT- BIT[5:4]-Network Type for port 4*/
		*net_type = (rtw_read8(adapter, MSR1) & 0x30) >> 4;
		break;
#endif /*#if defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8822B)*/
	default:
		RTW_INFO("[WARN] "ADPT_FMT"- invalid hw port -%d\n",
			 ADPT_ARG(adapter), adapter->hw_port);
		rtw_warn_on(1);
		break;
	}
}

void rtw_hal_set_msr(_adapter *adapter, u8 net_type)
{
	u8 val8 = 0;

	switch (adapter->hw_port) {
	case HW_PORT0:
#if defined(CONFIG_MI_WITH_MBSSID_CAM) && defined(CONFIG_MBSSID_CAM) /*For 2 hw ports - 88E/92E/8812/8821/8723B*/
		if (rtw_get_mbid_cam_entry_num(adapter)) {
			if (net_type != _HW_STATE_NOLINK_)
				net_type = _HW_STATE_AP_;
		}
#endif
		/*REG_CR - BIT[17:16]-Network Type for port 0*/
		val8 = rtw_read8(adapter, MSR) & 0x0C;
		val8 |= net_type;
		rtw_write8(adapter, MSR, val8);
		break;
	case HW_PORT1:
		/*REG_CR - BIT[19:18]-Network Type for port 1*/
		val8 = rtw_read8(adapter, MSR) & 0x03;
		val8 |= net_type << 2;
		rtw_write8(adapter, MSR, val8);
		break;
#if defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C)
	case HW_PORT2:
		/*REG_CR_EXT- BIT[1:0]-Network Type for port 2*/
		val8 = rtw_read8(adapter, MSR1) & 0xFC;
		val8 |= net_type;
		rtw_write8(adapter, MSR1, val8);
		break;
	case HW_PORT3:
		/*REG_CR_EXT- BIT[3:2]-Network Type for port 3*/
		val8 = rtw_read8(adapter, MSR1) & 0xF3;
		val8 |= net_type << 2;
		rtw_write8(adapter, MSR1, val8);
		break;
	case HW_PORT4:
		/*REG_CR_EXT- BIT[5:4]-Network Type for port 4*/
		val8 = rtw_read8(adapter, MSR1) & 0xCF;
		val8 |= net_type << 4;
		rtw_write8(adapter, MSR1, val8);
		break;
#endif /* CONFIG_RTL8814A | CONFIG_RTL8822B */
	default:
		RTW_INFO("[WARN] "ADPT_FMT"- invalid hw port -%d\n",
			 ADPT_ARG(adapter), adapter->hw_port);
		rtw_warn_on(1);
		break;
	}
}

void hw_var_port_switch(_adapter *adapter)
{
#ifdef CONFIG_CONCURRENT_MODE
#ifdef CONFIG_RUNTIME_PORT_SWITCH
	/*
	0x102: MSR
	0x550: REG_BCN_CTRL
	0x551: REG_BCN_CTRL_1
	0x55A: REG_ATIMWND
	0x560: REG_TSFTR
	0x568: REG_TSFTR1
	0x570: REG_ATIMWND_1
	0x610: REG_MACID
	0x618: REG_BSSID
	0x700: REG_MACID1
	0x708: REG_BSSID1
	*/

	int i;
	u8 msr;
	u8 bcn_ctrl;
	u8 bcn_ctrl_1;
	u8 atimwnd[2];
	u8 atimwnd_1[2];
	u8 tsftr[8];
	u8 tsftr_1[8];
	u8 macid[6];
	u8 bssid[6];
	u8 macid_1[6];
	u8 bssid_1[6];

	u8 hw_port;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	_adapter *iface = NULL;

	msr = rtw_read8(adapter, MSR);
	bcn_ctrl = rtw_read8(adapter, REG_BCN_CTRL);
	bcn_ctrl_1 = rtw_read8(adapter, REG_BCN_CTRL_1);

	for (i = 0; i < 2; i++)
		atimwnd[i] = rtw_read8(adapter, REG_ATIMWND + i);
	for (i = 0; i < 2; i++)
		atimwnd_1[i] = rtw_read8(adapter, REG_ATIMWND_1 + i);

	for (i = 0; i < 8; i++)
		tsftr[i] = rtw_read8(adapter, REG_TSFTR + i);
	for (i = 0; i < 8; i++)
		tsftr_1[i] = rtw_read8(adapter, REG_TSFTR1 + i);

	for (i = 0; i < 6; i++)
		macid[i] = rtw_read8(adapter, REG_MACID + i);

	for (i = 0; i < 6; i++)
		bssid[i] = rtw_read8(adapter, REG_BSSID + i);

	for (i = 0; i < 6; i++)
		macid_1[i] = rtw_read8(adapter, REG_MACID1 + i);

	for (i = 0; i < 6; i++)
		bssid_1[i] = rtw_read8(adapter, REG_BSSID1 + i);

#ifdef DBG_RUNTIME_PORT_SWITCH
	RTW_INFO(FUNC_ADPT_FMT" before switch\n"
		 "msr:0x%02x\n"
		 "bcn_ctrl:0x%02x\n"
		 "bcn_ctrl_1:0x%02x\n"
		 "atimwnd:0x%04x\n"
		 "atimwnd_1:0x%04x\n"
		 "tsftr:%llu\n"
		 "tsftr1:%llu\n"
		 "macid:"MAC_FMT"\n"
		 "bssid:"MAC_FMT"\n"
		 "macid_1:"MAC_FMT"\n"
		 "bssid_1:"MAC_FMT"\n"
		 , FUNC_ADPT_ARG(adapter)
		 , msr
		 , bcn_ctrl
		 , bcn_ctrl_1
		 , *((u16 *)atimwnd)
		 , *((u16 *)atimwnd_1)
		 , *((u64 *)tsftr)
		 , *((u64 *)tsftr_1)
		 , MAC_ARG(macid)
		 , MAC_ARG(bssid)
		 , MAC_ARG(macid_1)
		 , MAC_ARG(bssid_1)
		);
#endif /* DBG_RUNTIME_PORT_SWITCH */

	/* disable bcn function, disable update TSF */
	rtw_write8(adapter, REG_BCN_CTRL, (bcn_ctrl & (~EN_BCN_FUNCTION)) | DIS_TSF_UDT);
	rtw_write8(adapter, REG_BCN_CTRL_1, (bcn_ctrl_1 & (~EN_BCN_FUNCTION)) | DIS_TSF_UDT);

	/* switch msr */
	msr = (msr & 0xf0) | ((msr & 0x03) << 2) | ((msr & 0x0c) >> 2);
	rtw_write8(adapter, MSR, msr);

	/* write port0 */
	rtw_write8(adapter, REG_BCN_CTRL, bcn_ctrl_1 & ~EN_BCN_FUNCTION);
	for (i = 0; i < 2; i++)
		rtw_write8(adapter, REG_ATIMWND + i, atimwnd_1[i]);
	for (i = 0; i < 8; i++)
		rtw_write8(adapter, REG_TSFTR + i, tsftr_1[i]);
	for (i = 0; i < 6; i++)
		rtw_write8(adapter, REG_MACID + i, macid_1[i]);
	for (i = 0; i < 6; i++)
		rtw_write8(adapter, REG_BSSID + i, bssid_1[i]);

	/* write port1 */
	rtw_write8(adapter, REG_BCN_CTRL_1, bcn_ctrl & ~EN_BCN_FUNCTION);
	for (i = 0; i < 2; i++)
		rtw_write8(adapter, REG_ATIMWND_1 + i, atimwnd[i]);
	for (i = 0; i < 8; i++)
		rtw_write8(adapter, REG_TSFTR1 + i, tsftr[i]);
	for (i = 0; i < 6; i++)
		rtw_write8(adapter, REG_MACID1 + i, macid[i]);
	for (i = 0; i < 6; i++)
		rtw_write8(adapter, REG_BSSID1 + i, bssid[i]);

	/* write bcn ctl */
#ifdef CONFIG_BT_COEXIST
	/* always enable port0 beacon function for PSTDMA */
	if (IS_HARDWARE_TYPE_8723B(adapter) || IS_HARDWARE_TYPE_8703B(adapter)
	    || IS_HARDWARE_TYPE_8723D(adapter))
		bcn_ctrl_1 |= EN_BCN_FUNCTION;
	/* always disable port1 beacon function for PSTDMA */
	if (IS_HARDWARE_TYPE_8723B(adapter) || IS_HARDWARE_TYPE_8703B(adapter))
		bcn_ctrl &= ~EN_BCN_FUNCTION;
#endif
	rtw_write8(adapter, REG_BCN_CTRL, bcn_ctrl_1);
	rtw_write8(adapter, REG_BCN_CTRL_1, bcn_ctrl);

	if (adapter->iface_id == IFACE_ID0)
		iface = dvobj->padapters[IFACE_ID1];
	else if (adapter->iface_id == IFACE_ID1)
		iface = dvobj->padapters[IFACE_ID0];


	if (adapter->hw_port == HW_PORT0) {
		adapter->hw_port = HW_PORT1;
		iface->hw_port = HW_PORT0;
		RTW_PRINT("port switch - port0("ADPT_FMT"), port1("ADPT_FMT")\n",
			  ADPT_ARG(iface), ADPT_ARG(adapter));
	} else {
		adapter->hw_port = HW_PORT0;
		iface->hw_port = HW_PORT1;
		RTW_PRINT("port switch - port0("ADPT_FMT"), port1("ADPT_FMT")\n",
			  ADPT_ARG(adapter), ADPT_ARG(iface));
	}

#ifdef DBG_RUNTIME_PORT_SWITCH
	msr = rtw_read8(adapter, MSR);
	bcn_ctrl = rtw_read8(adapter, REG_BCN_CTRL);
	bcn_ctrl_1 = rtw_read8(adapter, REG_BCN_CTRL_1);

	for (i = 0; i < 2; i++)
		atimwnd[i] = rtw_read8(adapter, REG_ATIMWND + i);
	for (i = 0; i < 2; i++)
		atimwnd_1[i] = rtw_read8(adapter, REG_ATIMWND_1 + i);

	for (i = 0; i < 8; i++)
		tsftr[i] = rtw_read8(adapter, REG_TSFTR + i);
	for (i = 0; i < 8; i++)
		tsftr_1[i] = rtw_read8(adapter, REG_TSFTR1 + i);

	for (i = 0; i < 6; i++)
		macid[i] = rtw_read8(adapter, REG_MACID + i);

	for (i = 0; i < 6; i++)
		bssid[i] = rtw_read8(adapter, REG_BSSID + i);

	for (i = 0; i < 6; i++)
		macid_1[i] = rtw_read8(adapter, REG_MACID1 + i);

	for (i = 0; i < 6; i++)
		bssid_1[i] = rtw_read8(adapter, REG_BSSID1 + i);

	RTW_INFO(FUNC_ADPT_FMT" after switch\n"
		 "msr:0x%02x\n"
		 "bcn_ctrl:0x%02x\n"
		 "bcn_ctrl_1:0x%02x\n"
		 "atimwnd:%u\n"
		 "atimwnd_1:%u\n"
		 "tsftr:%llu\n"
		 "tsftr1:%llu\n"
		 "macid:"MAC_FMT"\n"
		 "bssid:"MAC_FMT"\n"
		 "macid_1:"MAC_FMT"\n"
		 "bssid_1:"MAC_FMT"\n"
		 , FUNC_ADPT_ARG(adapter)
		 , msr
		 , bcn_ctrl
		 , bcn_ctrl_1
		 , *((u16 *)atimwnd)
		 , *((u16 *)atimwnd_1)
		 , *((u64 *)tsftr)
		 , *((u64 *)tsftr_1)
		 , MAC_ARG(macid)
		 , MAC_ARG(bssid)
		 , MAC_ARG(macid_1)
		 , MAC_ARG(bssid_1)
		);
#endif /* DBG_RUNTIME_PORT_SWITCH */

#endif /* CONFIG_RUNTIME_PORT_SWITCH */
#endif /* CONFIG_CONCURRENT_MODE */
}

const char *const _h2c_msr_role_str[] = {
	"RSVD",
	"STA",
	"AP",
	"GC",
	"GO",
	"TDLS",
	"ADHOC",
	"INVALID",
};

#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
s32 rtw_hal_set_default_port_id_cmd(_adapter *adapter, u8 mac_id)
{
	s32 ret = _SUCCESS;
	u8 parm[H2C_DEFAULT_PORT_ID_LEN] = {0};
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);

	SET_H2CCMD_DFTPID_PORT_ID(parm, adapter->hw_port);
	SET_H2CCMD_DFTPID_MAC_ID(parm, mac_id);

	RTW_DBG_DUMP("DFT port id parm:", parm, H2C_DEFAULT_PORT_ID_LEN);
	RTW_INFO("%s port_id :%d, mad_id:%d\n", __func__, adapter->hw_port, mac_id);

	ret = rtw_hal_fill_h2c_cmd(adapter, H2C_DEFAULT_PORT_ID, H2C_DEFAULT_PORT_ID_LEN, parm);
	dvobj->default_port_id = adapter->hw_port;

	return ret;
}
s32 rtw_set_default_port_id(_adapter *adapter)
{
	s32 ret = _SUCCESS;
	struct sta_info		*psta;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);

	if (adapter->hw_port == dvobj->default_port_id)
		return ret;

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE) {
		psta = rtw_get_stainfo(&adapter->stapriv, get_bssid(pmlmepriv));
		if (psta)
			ret = rtw_hal_set_default_port_id_cmd(adapter, psta->mac_id);
	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) {

	} else {
	}

	return ret;
}
s32 rtw_set_ps_rsvd_page(_adapter *adapter)
{
	s32 ret = _SUCCESS;
	u16 media_status_rpt = RT_MEDIA_CONNECT;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);

	if (adapter->hw_port == dvobj->default_port_id)
		return ret;

	rtw_hal_set_hwreg(adapter, HW_VAR_H2C_FW_JOINBSSRPT,
			  (u8 *)&media_status_rpt);

	return ret;
}

#endif
/*
* rtw_hal_set_FwMediaStatusRpt_cmd -
*
* @adapter:
* @opmode:  0:disconnect, 1:connect
* @miracast: 0:it's not in miracast scenario. 1:it's in miracast scenario
* @miracast_sink: 0:source. 1:sink
* @role: The role of this macid. 0:rsvd. 1:STA. 2:AP. 3:GC. 4:GO. 5:TDLS
* @macid:
* @macid_ind:  0:update Media Status to macid.  1:update Media Status from macid to macid_end
* @macid_end:
*/
s32 rtw_hal_set_FwMediaStatusRpt_cmd(_adapter *adapter, bool opmode, bool miracast, bool miracast_sink, u8 role, u8 macid, bool macid_ind, u8 macid_end)
{
	struct macid_ctl_t *macid_ctl = &adapter->dvobj->macid_ctl;
	u8 parm[H2C_MEDIA_STATUS_RPT_LEN] = {0};
	int i;
	s32 ret;

	SET_H2CCMD_MSRRPT_PARM_OPMODE(parm, opmode);
	SET_H2CCMD_MSRRPT_PARM_MACID_IND(parm, macid_ind);
	SET_H2CCMD_MSRRPT_PARM_MIRACAST(parm, miracast);
	SET_H2CCMD_MSRRPT_PARM_MIRACAST_SINK(parm, miracast_sink);
	SET_H2CCMD_MSRRPT_PARM_ROLE(parm, role);
	SET_H2CCMD_MSRRPT_PARM_MACID(parm, macid);
	SET_H2CCMD_MSRRPT_PARM_MACID_END(parm, macid_end);
#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
	SET_H2CCMD_MSRRPT_PARM_PORT_NUM(parm, adapter->hw_port);
#endif
	RTW_DBG_DUMP("MediaStatusRpt parm:", parm, H2C_MEDIA_STATUS_RPT_LEN);

#ifdef CONFIG_DFS_MASTER
	/* workaround for TXPAUSE cleared issue by FW's MediaStatusRpt handling */
	if (macid_ind == 0 && macid == 1
		&& !rtw_odm_dfs_domain_unknown(adapter)
	) {
		u8 parm0_bak = parm[0];

		SET_H2CCMD_MSRRPT_PARM_MACID_IND(&parm0_bak, 0);
		if (macid_ctl->h2c_msr[macid] == parm0_bak) {
			ret = _SUCCESS;
			goto post_action;
		}
	}
#endif

	ret = rtw_hal_fill_h2c_cmd(adapter, H2C_MEDIA_STATUS_RPT, H2C_MEDIA_STATUS_RPT_LEN, parm);
	if (ret != _SUCCESS)
		goto exit;

#ifdef CONFIG_DFS_MASTER
post_action:
#endif

#if defined(CONFIG_RTL8188E)
	if (rtw_get_chip_type(adapter) == RTL8188E) {
		HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);

		/* 8188E FW doesn't set macid no link, driver does it by self */
		if (opmode)
			rtw_hal_set_hwreg(adapter, HW_VAR_MACID_LINK, &macid);
		else
			rtw_hal_set_hwreg(adapter, HW_VAR_MACID_NOLINK, &macid);

		/* for 8188E RA */
#if (RATE_ADAPTIVE_SUPPORT == 1)
		if (hal_data->fw_ractrl == _FALSE) {
			u8 max_macid;

			max_macid = rtw_search_max_mac_id(adapter);
			rtw_hal_set_hwreg(adapter, HW_VAR_TX_RPT_MAX_MACID, &max_macid);
		}
#endif
	}
#endif

#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
	/* TODO: this should move to IOT issue area */
	if (rtw_get_chip_type(adapter) == RTL8812
		|| rtw_get_chip_type(adapter) == RTL8821
	) {
		if (MLME_IS_STA(adapter))
			Hal_PatchwithJaguar_8812(adapter, opmode);
	}
#endif

	SET_H2CCMD_MSRRPT_PARM_MACID_IND(parm, 0);
	if (macid_ind == 0)
		macid_end = macid;

	for (i = macid; macid <= macid_end; macid++) {
		rtw_macid_ctl_set_h2c_msr(macid_ctl, macid, parm[0]);
		if (!opmode) {
			rtw_macid_ctl_set_bw(macid_ctl, macid, CHANNEL_WIDTH_20);
			rtw_macid_ctl_set_vht_en(macid_ctl, macid, 0);
			rtw_macid_ctl_set_rate_bmp0(macid_ctl, macid, 0);
			rtw_macid_ctl_set_rate_bmp1(macid_ctl, macid, 0);
		}
	}
	if (!opmode)
		rtw_update_tx_rate_bmp(adapter_to_dvobj(adapter));

exit:
	return ret;
}

inline s32 rtw_hal_set_FwMediaStatusRpt_single_cmd(_adapter *adapter, bool opmode, bool miracast, bool miracast_sink, u8 role, u8 macid)
{
	return rtw_hal_set_FwMediaStatusRpt_cmd(adapter, opmode, miracast, miracast_sink, role, macid, 0, 0);
}

inline s32 rtw_hal_set_FwMediaStatusRpt_range_cmd(_adapter *adapter, bool opmode, bool miracast, bool miracast_sink, u8 role, u8 macid, u8 macid_end)
{
	return rtw_hal_set_FwMediaStatusRpt_cmd(adapter, opmode, miracast, miracast_sink, role, macid, 1, macid_end);
}

void rtw_hal_set_FwRsvdPage_cmd(PADAPTER padapter, PRSVDPAGE_LOC rsvdpageloc)
{
	struct	hal_ops *pHalFunc = &padapter->hal_func;
	u8	u1H2CRsvdPageParm[H2C_RSVDPAGE_LOC_LEN] = {0};
	u8	ret = 0;

	RTW_INFO("RsvdPageLoc: ProbeRsp=%d PsPoll=%d Null=%d QoSNull=%d BTNull=%d\n",
		 rsvdpageloc->LocProbeRsp, rsvdpageloc->LocPsPoll,
		 rsvdpageloc->LocNullData, rsvdpageloc->LocQosNull,
		 rsvdpageloc->LocBTQosNull);

	SET_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(u1H2CRsvdPageParm, rsvdpageloc->LocProbeRsp);
	SET_H2CCMD_RSVDPAGE_LOC_PSPOLL(u1H2CRsvdPageParm, rsvdpageloc->LocPsPoll);
	SET_H2CCMD_RSVDPAGE_LOC_NULL_DATA(u1H2CRsvdPageParm, rsvdpageloc->LocNullData);
	SET_H2CCMD_RSVDPAGE_LOC_QOS_NULL_DATA(u1H2CRsvdPageParm, rsvdpageloc->LocQosNull);
	SET_H2CCMD_RSVDPAGE_LOC_BT_QOS_NULL_DATA(u1H2CRsvdPageParm, rsvdpageloc->LocBTQosNull);

	ret = rtw_hal_fill_h2c_cmd(padapter,
				   H2C_RSVD_PAGE,
				   H2C_RSVDPAGE_LOC_LEN,
				   u1H2CRsvdPageParm);

}

#ifdef CONFIG_GPIO_WAKEUP
void rtw_hal_switch_gpio_wl_ctrl(_adapter *padapter, u8 index, u8 enable)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);

	if (IS_8723D_SERIES(pHalData->version_id) || IS_8822B_SERIES(pHalData->version_id))
		rtw_hal_set_hwreg(padapter, HW_SET_GPIO_WL_CTRL, (u8 *)(&enable));
	/*
	* Switch GPIO_13, GPIO_14 to wlan control, or pull GPIO_13,14 MUST fail.
	* It happended at 8723B/8192E/8821A. New IC will check multi function GPIO,
	* and implement HAL function.
	* TODO: GPIO_8 multi function?
	*/

	if (index == 13 || index == 14)
		rtw_hal_set_hwreg(padapter, HW_SET_GPIO_WL_CTRL, (u8 *)(&enable));
}

void rtw_hal_set_output_gpio(_adapter *padapter, u8 index, u8 outputval)
{
	if (index <= 7) {
		/* config GPIO mode */
		rtw_write8(padapter, REG_GPIO_PIN_CTRL + 3,
			rtw_read8(padapter, REG_GPIO_PIN_CTRL + 3) & ~BIT(index));

		/* config GPIO Sel */
		/* 0: input */
		/* 1: output */
		rtw_write8(padapter, REG_GPIO_PIN_CTRL + 2,
			rtw_read8(padapter, REG_GPIO_PIN_CTRL + 2) | BIT(index));

		/* set output value */
		if (outputval) {
			rtw_write8(padapter, REG_GPIO_PIN_CTRL + 1,
				rtw_read8(padapter, REG_GPIO_PIN_CTRL + 1) | BIT(index));
		} else {
			rtw_write8(padapter, REG_GPIO_PIN_CTRL + 1,
				rtw_read8(padapter, REG_GPIO_PIN_CTRL + 1) & ~BIT(index));
		}
	} else if (index <= 15) {
		/* 88C Series: */
		/* index: 11~8 transform to 3~0 */
		/* 8723 Series: */
		/* index: 12~8 transform to 4~0 */

		index -= 8;

		/* config GPIO mode */
		rtw_write8(padapter, REG_GPIO_PIN_CTRL_2 + 3,
			rtw_read8(padapter, REG_GPIO_PIN_CTRL_2 + 3) & ~BIT(index));

		/* config GPIO Sel */
		/* 0: input */
		/* 1: output */
		rtw_write8(padapter, REG_GPIO_PIN_CTRL_2 + 2,
			rtw_read8(padapter, REG_GPIO_PIN_CTRL_2 + 2) | BIT(index));

		/* set output value */
		if (outputval) {
			rtw_write8(padapter, REG_GPIO_PIN_CTRL_2 + 1,
				rtw_read8(padapter, REG_GPIO_PIN_CTRL_2 + 1) | BIT(index));
		} else {
			rtw_write8(padapter, REG_GPIO_PIN_CTRL_2 + 1,
				rtw_read8(padapter, REG_GPIO_PIN_CTRL_2 + 1) & ~BIT(index));
		}
	} else {
		RTW_INFO("%s: invalid GPIO%d=%d\n",
			 __FUNCTION__, index, outputval);
	}
}
#endif

void rtw_hal_set_FwAoacRsvdPage_cmd(PADAPTER padapter, PRSVDPAGE_LOC rsvdpageloc)
{
	struct	hal_ops *pHalFunc = &padapter->hal_func;
	struct	pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8	res = 0, count = 0, ret = 0;
#ifdef CONFIG_WOWLAN
	u8 u1H2CAoacRsvdPageParm[H2C_AOAC_RSVDPAGE_LOC_LEN] = {0};

	RTW_INFO("AOACRsvdPageLoc: RWC=%d ArpRsp=%d NbrAdv=%d GtkRsp=%d GtkInfo=%d ProbeReq=%d NetworkList=%d\n",
		 rsvdpageloc->LocRemoteCtrlInfo, rsvdpageloc->LocArpRsp,
		 rsvdpageloc->LocNbrAdv, rsvdpageloc->LocGTKRsp,
		 rsvdpageloc->LocGTKInfo, rsvdpageloc->LocProbeReq,
		 rsvdpageloc->LocNetList);

	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_REMOTE_WAKE_CTRL_INFO(u1H2CAoacRsvdPageParm, rsvdpageloc->LocRemoteCtrlInfo);
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_ARP_RSP(u1H2CAoacRsvdPageParm, rsvdpageloc->LocArpRsp);
		/* SET_H2CCMD_AOAC_RSVDPAGE_LOC_NEIGHBOR_ADV(u1H2CAoacRsvdPageParm, rsvdpageloc->LocNbrAdv); */
#ifdef CONFIG_GTK_OL
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_GTK_RSP(u1H2CAoacRsvdPageParm, rsvdpageloc->LocGTKRsp);
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_GTK_INFO(u1H2CAoacRsvdPageParm, rsvdpageloc->LocGTKInfo);
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_GTK_EXT_MEM(u1H2CAoacRsvdPageParm, rsvdpageloc->LocGTKEXTMEM);
#endif /* CONFIG_GTK_OL */
		ret = rtw_hal_fill_h2c_cmd(padapter,
					   H2C_AOAC_RSVD_PAGE,
					   H2C_AOAC_RSVDPAGE_LOC_LEN,
					   u1H2CAoacRsvdPageParm);

		RTW_INFO("AOAC Report=%d\n", rsvdpageloc->LocAOACReport);
		_rtw_memset(&u1H2CAoacRsvdPageParm, 0, sizeof(u1H2CAoacRsvdPageParm));
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_AOAC_REPORT(u1H2CAoacRsvdPageParm,
					 rsvdpageloc->LocAOACReport);
		ret = rtw_hal_fill_h2c_cmd(padapter,
				   H2C_AOAC_RSVDPAGE3,
				   H2C_AOAC_RSVDPAGE_LOC_LEN,
				   u1H2CAoacRsvdPageParm);
		pwrpriv->wowlan_aoac_rpt_loc = rsvdpageloc->LocAOACReport;
	}
#ifdef CONFIG_PNO_SUPPORT
	else {

		if (!pwrpriv->wowlan_in_resume) {
			RTW_INFO("NLO_INFO=%d\n", rsvdpageloc->LocPNOInfo);
			_rtw_memset(&u1H2CAoacRsvdPageParm, 0,
				    sizeof(u1H2CAoacRsvdPageParm));
			SET_H2CCMD_AOAC_RSVDPAGE_LOC_NLO_INFO(u1H2CAoacRsvdPageParm,
						      rsvdpageloc->LocPNOInfo);
			ret = rtw_hal_fill_h2c_cmd(padapter,
						   H2C_AOAC_RSVDPAGE3,
						   H2C_AOAC_RSVDPAGE_LOC_LEN,
						   u1H2CAoacRsvdPageParm);
		}
	}
#endif /* CONFIG_PNO_SUPPORT */
#endif /* CONFIG_WOWLAN */
}

/*#define DBG_GET_RSVD_PAGE*/
int rtw_hal_get_rsvd_page(_adapter *adapter, u32 page_offset,
	u32 page_num, u8 *buffer, u32 buffer_size)
{
	u32 addr = 0, size = 0, count = 0;
	u32 page_size = 0, data_low = 0, data_high = 0;
	u16 txbndy = 0, offset = 0;
	u8 i = 0;
	bool rst = _FALSE;

	rtw_hal_get_def_var(adapter, HAL_DEF_TX_PAGE_SIZE, &page_size);

	addr = page_offset * page_size;
	size = page_num * page_size;

	if (buffer_size < size) {
		RTW_ERR("%s buffer_size(%d) < get page total size(%d)\n",
			__func__, buffer_size, size);
		return rst;
	}
#ifdef RTW_HALMAC
	if (rtw_halmac_dump_fifo(adapter_to_dvobj(adapter), 2, addr, size, buffer) < 0)
		rst = _FALSE;
	else
		rst = _TRUE;
#else
	txbndy = rtw_read8(adapter, REG_TDECTRL + 1);

	offset = (txbndy + page_offset) << 4;
	count = (buffer_size / 8) + 1;

	rtw_write8(adapter, REG_PKT_BUFF_ACCESS_CTRL, 0x69);

	for (i = 0 ; i < count ; i++) {
		rtw_write32(adapter, REG_PKTBUF_DBG_CTRL, offset + i);
		data_low = rtw_read32(adapter, REG_PKTBUF_DBG_DATA_L);
		data_high = rtw_read32(adapter, REG_PKTBUF_DBG_DATA_H);
		_rtw_memcpy(buffer + (i * 8),
			&data_low, sizeof(data_low));
		_rtw_memcpy(buffer + ((i * 8) + 4),
			&data_high, sizeof(data_high));
	}
	rtw_write8(adapter, REG_PKT_BUFF_ACCESS_CTRL, 0x0);
	rst = _TRUE;
#endif /*RTW_HALMAC*/

#ifdef DBG_GET_RSVD_PAGE
	RTW_INFO("%s [page_offset:%d , page_num:%d][start_addr:0x%04x , size:%d]\n",
		 __func__, page_offset, page_num, addr, size);
	RTW_INFO_DUMP("\n", buffer, size);
	RTW_INFO(" ==================================================\n");
#endif
	return rst;
}

void rtw_dump_rsvd_page(void *sel, _adapter *adapter, u8 page_offset, u8 page_num)
{
	u32 page_size = 0;
	u8 *buffer = NULL;
	u32 buf_size = 0;

	if (page_num == 0)
		return;

	RTW_PRINT_SEL(sel, "======= RSVG PAGE DUMP =======\n");
	RTW_PRINT_SEL(sel, "page_offset:%d, page_num:%d\n", page_offset, page_num);

	rtw_hal_get_def_var(adapter, HAL_DEF_TX_PAGE_SIZE, &page_size);
	if (page_size) {
		buf_size = page_size * page_num;
		buffer = rtw_zvmalloc(buf_size);

		if (buffer) {
			rtw_hal_get_rsvd_page(adapter, page_offset, page_num, buffer, buf_size);
			_RTW_DUMP_SEL(sel, buffer, buf_size);
			rtw_vmfree(buffer, buf_size);
		} else
			RTW_PRINT_SEL(sel, "ERROR - rsvd_buf mem allocate failed\n");
	} else
			RTW_PRINT_SEL(sel, "ERROR - Tx page size is zero ??\n");

	RTW_PRINT_SEL(sel, "==========================\n");
}

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
static void rtw_hal_force_enable_rxdma(_adapter *adapter)
{
	RTW_INFO("%s: Set 0x690=0x00\n", __func__);
	rtw_write8(adapter, REG_WOW_CTRL,
		   (rtw_read8(adapter, REG_WOW_CTRL) & 0xf0));
	RTW_PRINT("%s: Release RXDMA\n", __func__);
	rtw_write32(adapter, REG_RXPKT_NUM,
		    (rtw_read32(adapter, REG_RXPKT_NUM) & (~RW_RELEASE_EN)));
}
#if defined(CONFIG_RTL8188E)
static void rtw_hal_disable_tx_report(_adapter *adapter)
{
	rtw_write8(adapter, REG_TX_RPT_CTRL,
		   ((rtw_read8(adapter, REG_TX_RPT_CTRL) & ~BIT(1))) & ~BIT(5));
	RTW_INFO("disable TXRPT:0x%02x\n", rtw_read8(adapter, REG_TX_RPT_CTRL));
}

static void rtw_hal_enable_tx_report(_adapter *adapter)
{
	rtw_write8(adapter, REG_TX_RPT_CTRL,
		   ((rtw_read8(adapter, REG_TX_RPT_CTRL) | BIT(1))) | BIT(5));
	RTW_INFO("enable TX_RPT:0x%02x\n", rtw_read8(adapter, REG_TX_RPT_CTRL));
}
#endif
static void rtw_hal_release_rx_dma(_adapter *adapter)
{
	u32 val32 = 0;

	val32 = rtw_read32(adapter, REG_RXPKT_NUM);

	rtw_write32(adapter, REG_RXPKT_NUM, (val32 & (~RW_RELEASE_EN)));

	RTW_INFO("%s, [0x%04x]: 0x%08x\n",
		 __func__, REG_RXPKT_NUM, (val32 & (~RW_RELEASE_EN)));
}

static u8 rtw_hal_pause_rx_dma(_adapter *adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	u8 ret = 0;
	s8 trycnt = 100;
	u32 tmp = 0;
	int res = 0;
	/* RX DMA stop */
	RTW_PRINT("Pause DMA\n");
	rtw_write32(adapter, REG_RXPKT_NUM,
		    (rtw_read32(adapter, REG_RXPKT_NUM) | RW_RELEASE_EN));
	do {
		if ((rtw_read32(adapter, REG_RXPKT_NUM) & RXDMA_IDLE)) {
#ifdef CONFIG_USB_HCI
			/* stop interface before leave */
			if (_TRUE == hal->usb_intf_start) {
				rtw_intf_stop(adapter);
				RTW_ENABLE_FUNC(adapter, DF_RX_BIT);
				RTW_ENABLE_FUNC(adapter, DF_TX_BIT);
			}
#endif /* CONFIG_USB_HCI */

			RTW_PRINT("RX_DMA_IDLE is true\n");
			ret = _SUCCESS;
			break;
		}
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		else {
			res = RecvOnePkt(adapter);
			RTW_PRINT("RecvOnePkt Result: %d\n", res);
		}
#endif /* CONFIG_SDIO_HCI || CONFIG_GSPI_HCI */

#ifdef CONFIG_USB_HCI
		else {
			/* to avoid interface start repeatedly  */
			if (_FALSE == hal->usb_intf_start)
				rtw_intf_start(adapter);
		}
#endif /* CONFIG_USB_HCI */
	} while (trycnt--);

	if (trycnt < 0) {
		tmp = rtw_read16(adapter, REG_RXPKT_NUM + 3);

		RTW_PRINT("Stop RX DMA failed......\n");
		RTW_PRINT("%s, RXPKT_NUM: 0x%04x\n",
			  __func__, tmp);
		tmp = rtw_read16(adapter, REG_RXPKT_NUM + 2);
		if (tmp & BIT(3))
			RTW_PRINT("%s, RX DMA has req\n",
				  __func__);
		else
			RTW_PRINT("%s, RX DMA no req\n",
				  __func__);
		ret = _FAIL;
	}

	return ret;
}

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
#ifndef RTW_HALMAC
static u8 rtw_hal_enable_cpwm2(_adapter *adapter)
{
	u8 ret = 0;
	int res = 0;
	u32 tmp = 0;
#ifdef CONFIG_GPIO_WAKEUP
	return _SUCCESS;
#else
	RTW_PRINT("%s\n", __func__);

	res = sdio_local_read(adapter, SDIO_REG_HIMR, 4, (u8 *)&tmp);
	if (!res)
		RTW_INFO("read SDIO_REG_HIMR: 0x%08x\n", tmp);
	else
		RTW_INFO("sdio_local_read fail\n");

	tmp = SDIO_HIMR_CPWM2_MSK;

	res = sdio_local_write(adapter, SDIO_REG_HIMR, 4, (u8 *)&tmp);

	if (!res) {
		res = sdio_local_read(adapter, SDIO_REG_HIMR, 4, (u8 *)&tmp);
		RTW_INFO("read again SDIO_REG_HIMR: 0x%08x\n", tmp);
		ret = _SUCCESS;
	} else {
		RTW_INFO("sdio_local_write fail\n");
		ret = _FAIL;
	}
	return ret;
#endif /* CONFIG_CPIO_WAKEUP */
}
#endif
#endif /* CONFIG_SDIO_HCI, CONFIG_GSPI_HCI */
#endif /* CONFIG_WOWLAN || CONFIG_AP_WOWLAN */

#ifdef CONFIG_WOWLAN
/*
 * rtw_hal_check_wow_ctrl
 * chk_type: _TRUE means to check enable, if 0x690 & bit1, WOW enable successful
 *		     _FALSE means to check disable, if 0x690 & bit1, WOW disable fail
 */
static u8 rtw_hal_check_wow_ctrl(_adapter *adapter, u8 chk_type)
{
	u8 mstatus = 0;
	u8 trycnt = 25;
	u8 res = _FALSE;

	mstatus = rtw_read8(adapter, REG_WOW_CTRL);
	RTW_INFO("%s mstatus:0x%02x\n", __func__, mstatus);

	if (chk_type) {
		while (!(mstatus & BIT1) && trycnt > 1) {
			mstatus = rtw_read8(adapter, REG_WOW_CTRL);
			RTW_PRINT("Loop index: %d :0x%02x\n",
				  trycnt, mstatus);
			trycnt--;
			rtw_msleep_os(20);
		}
		if (mstatus & BIT1)
			res = _TRUE;
		else
			res = _FALSE;
	} else {
		while (mstatus & BIT1 && trycnt > 1) {
			mstatus = rtw_read8(adapter, REG_WOW_CTRL);
			RTW_PRINT("Loop index: %d :0x%02x\n",
				  trycnt, mstatus);
			trycnt--;
			rtw_msleep_os(20);
		}

		if (mstatus & BIT1)
			res = _FALSE;
		else
			res = _TRUE;
	}
	RTW_PRINT("%s check_type: %d res: %d trycnt: %d\n",
		  __func__, chk_type, res, (25 - trycnt));
	return res;
}

#ifdef CONFIG_PNO_SUPPORT
static u8 rtw_hal_check_pno_enabled(_adapter *adapter)
{
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(adapter);
	u8 res = 0, count = 0;
	u8 ret = _FALSE;

	if (ppwrpriv->wowlan_pno_enable && ppwrpriv->wowlan_in_resume == _FALSE) {
		res = rtw_read8(adapter, REG_PNO_STATUS);
		while (!(res & BIT(7)) && count < 25) {
			RTW_INFO("[%d] cmd: 0x81 REG_PNO_STATUS: 0x%02x\n",
				 count, res);
			res = rtw_read8(adapter, REG_PNO_STATUS);
			count++;
			rtw_msleep_os(2);
		}
		if (res & BIT(7))
			ret = _TRUE;
		else
			ret = _FALSE;
		RTW_INFO("cmd: 0x81 REG_PNO_STATUS: ret(%d)\n", ret);
	}
	return ret;
}
#endif

static void rtw_hal_backup_rate(_adapter *adapter)
{
	RTW_INFO("%s\n", __func__);
	/* backup data rate to register 0x8b for wowlan FW */
	rtw_write8(adapter, 0x8d, 1);
	rtw_write8(adapter, 0x8c, 0);
	rtw_write8(adapter, 0x8f, 0x40);
	rtw_write8(adapter, 0x8b, rtw_read8(adapter, 0x2f0));
}

#ifdef CONFIG_GTK_OL
static void rtw_hal_fw_sync_cam_id(_adapter *adapter)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	int cam_id, index = 0;
	u8 *addr = NULL;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
		return;

	addr = get_bssid(pmlmepriv);

	if (addr == NULL) {
		RTW_INFO("%s: get bssid MAC addr fail!!\n", __func__);
		return;
	}

	rtw_clean_dk_section(adapter);

	do {
		cam_id = rtw_camid_search(adapter, addr, index, 1);

		if (cam_id == -1)
			RTW_INFO("%s: cam_id: %d, key_id:%d\n", __func__, cam_id, index);
		else
			rtw_sec_cam_swap(adapter, cam_id, index);

		index++;
	} while (index < 4);

	rtw_write8(adapter, REG_SECCFG, 0xcc);
}

static void rtw_dump_aoac_rpt(_adapter *adapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	struct aoac_report *paoac_rpt = &pwrctl->wowlan_aoac_rpt;

	RTW_INFO_DUMP("[AOAC-RPT] IV -", paoac_rpt->iv, 8);
	RTW_INFO_DUMP("[AOAC-RPT] Replay counter of EAPOL key - ",
		paoac_rpt->replay_counter_eapol_key, 8);
	RTW_INFO_DUMP("[AOAC-RPT] Group key - ", paoac_rpt->group_key, 32);
	RTW_INFO("[AOAC-RPT] Key Index - %d\n", paoac_rpt->key_index);
	RTW_INFO("[AOAC-RPT] Security Type - %d\n", paoac_rpt->security_type);
}

static void rtw_hal_get_aoac_rpt(_adapter *adapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	struct aoac_report *paoac_rpt = &pwrctl->wowlan_aoac_rpt;
	u32 page_offset = 0, page_number = 0;
	u32 page_size = 0, buf_size = 0;
	u8 *buffer = NULL;
	u8 i = 0, tmp = 0;
	int ret = -1;

	/* read aoac report from rsvd page */
	page_offset = pwrctl->wowlan_aoac_rpt_loc;
	page_number = 1;

	rtw_hal_get_def_var(adapter, HAL_DEF_TX_PAGE_SIZE, &page_size);
	buf_size = page_size * page_number;

	buffer = rtw_zvmalloc(buf_size);

	if (NULL == buffer) {
		RTW_ERR("%s buffer allocate failed size(%d)\n",
			__func__, buf_size);
		return;
	}

	RTW_INFO("Get AOAC Report from rsvd page_offset:%d\n", page_offset);

	ret = rtw_hal_get_rsvd_page(adapter, page_offset,
		page_number, buffer, buf_size);

	if (ret == _FALSE) {
		RTW_ERR("%s get aoac report failed\n", __func__);
		rtw_warn_on(1);
		goto _exit;
	}

	_rtw_memset(paoac_rpt, 0, sizeof(struct aoac_report));
	_rtw_memcpy(paoac_rpt, buffer, sizeof(struct aoac_report));

	for (i = 0 ; i < 4 ; i++) {
		tmp = paoac_rpt->replay_counter_eapol_key[i];
		paoac_rpt->replay_counter_eapol_key[i] =
			paoac_rpt->replay_counter_eapol_key[7 - i];
		paoac_rpt->replay_counter_eapol_key[7 - i] = tmp;
	}

	/* rtw_dump_aoac_rpt(adapter); */

_exit:
	if (buffer)
		rtw_vmfree(buffer, buf_size);
}

static void rtw_hal_update_gtk_offload_info(_adapter *adapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	struct aoac_report *paoac_rpt = &pwrctl->wowlan_aoac_rpt;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	_irqL irqL;
	u8 get_key[16];
	u8 gtk_id = 0, offset = 0, i = 0, sz = 0;
	u64 replay_count = 0;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
		return;

	_rtw_memset(get_key, 0, sizeof(get_key));
	_rtw_memcpy(&replay_count,
		paoac_rpt->replay_counter_eapol_key, 8);

	/*read gtk key index*/
	gtk_id = paoac_rpt->key_index;

	if (gtk_id == 5 || gtk_id == 0) {
		RTW_INFO("%s no rekey event happened.\n", __func__);
	} else if (gtk_id > 0 && gtk_id < 4) {
		RTW_INFO("%s update security key.\n", __func__);
		/*read key from sec-cam,for DK ,keyindex is equal to cam-id*/
		rtw_sec_read_cam_ent(adapter, gtk_id,
				     NULL, NULL, get_key);
		rtw_clean_hw_dk_cam(adapter);

		if (_rtw_camid_is_gk(adapter, gtk_id)) {
			_enter_critical_bh(&cam_ctl->lock, &irqL);
			_rtw_memcpy(&dvobj->cam_cache[gtk_id].key,
				    get_key, 16);
			_exit_critical_bh(&cam_ctl->lock, &irqL);
		} else {
			struct setkey_parm parm_gtk;

			parm_gtk.algorithm = paoac_rpt->security_type;
			parm_gtk.keyid = gtk_id;
			_rtw_memcpy(parm_gtk.key, get_key, 16);
			setkey_hdl(adapter, (u8 *)&parm_gtk);
		}

		/*update key into related sw variable and sec-cam cache*/
		psecuritypriv->dot118021XGrpKeyid = gtk_id;
		_rtw_memcpy(&psecuritypriv->dot118021XGrpKey[gtk_id],
				get_key, 16);
		/* update SW TKIP TX/RX MIC value */
		if (psecuritypriv->dot118021XGrpPrivacy == _TKIP_) {
			offset = RTW_KEK_LEN + RTW_TKIP_MIC_LEN;
			_rtw_memcpy(
				&psecuritypriv->dot118021XGrptxmickey[gtk_id],
				&(paoac_rpt->group_key[offset]),
				RTW_TKIP_MIC_LEN);

			offset = RTW_KEK_LEN;
			_rtw_memcpy(
				&psecuritypriv->dot118021XGrprxmickey[gtk_id],
				&(paoac_rpt->group_key[offset]),
				RTW_TKIP_MIC_LEN);
		}
		/* Update broadcast RX IV */
		if (psecuritypriv->dot118021XGrpPrivacy == _AES_) {
			sz = sizeof(psecuritypriv->iv_seq[0]);
			for (i = 0 ; i < 4 ; i++)
				_rtw_memset(psecuritypriv->iv_seq[i], 0, sz);
		}

		RTW_PRINT("GTK (%d) "KEY_FMT"\n", gtk_id,
			KEY_ARG(psecuritypriv->dot118021XGrpKey[gtk_id].skey));
	}

	rtw_clean_dk_section(adapter);

	rtw_write8(adapter, REG_SECCFG, 0x0c);

	#ifdef CONFIG_GTK_OL_DBG
	/* if (gtk_keyindex != 5) */
	dump_sec_cam(RTW_DBGDUMP, adapter);
	dump_sec_cam_cache(RTW_DBGDUMP, adapter);
	#endif
}

static void rtw_hal_update_tx_iv(_adapter *adapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	struct aoac_report *paoac_rpt = &pwrctl->wowlan_aoac_rpt;
	struct sta_info	*psta;
	struct mlme_ext_priv	*pmlmeext = &(adapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct security_priv	*psecpriv = &adapter->securitypriv;

	u16 val16 = 0;
	u32 val32 = 0;
	u64 txiv = 0;
	u8 *pval = NULL;

	psta = rtw_get_stainfo(&adapter->stapriv,
			       get_my_bssid(&pmlmeinfo->network));

	/* Update TX iv data. */
	pval = (u8 *)&paoac_rpt->iv;

	if (psecpriv->dot11PrivacyAlgrthm == _TKIP_) {
		val16 = ((u16)(paoac_rpt->iv[2]) << 0) +
			((u16)(paoac_rpt->iv[0]) << 8);
		val32 = ((u32)(paoac_rpt->iv[4]) << 0) +
			((u32)(paoac_rpt->iv[5]) << 8) +
			((u32)(paoac_rpt->iv[6]) << 16) +
			((u32)(paoac_rpt->iv[7]) << 24);
	} else if (psecpriv->dot11PrivacyAlgrthm == _AES_) {
		val16 = ((u16)(paoac_rpt->iv[0]) << 0) +
			((u16)(paoac_rpt->iv[1]) << 8);
		val32 = ((u32)(paoac_rpt->iv[4]) << 0) +
			((u32)(paoac_rpt->iv[5]) << 8) +
			((u32)(paoac_rpt->iv[6]) << 16) +
			((u32)(paoac_rpt->iv[7]) << 24);
	}

	if (psta) {
		txiv = val16 + ((u64)val32 << 16);
		if (txiv != 0)
			psta->dot11txpn.val = txiv;
	}
}

static void rtw_hal_update_sw_security_info(_adapter *adapter)
{
	rtw_hal_update_tx_iv(adapter);
	rtw_hal_update_gtk_offload_info(adapter);
}
#endif /*CONFIG_GTK_OL*/

static u8 rtw_hal_set_keep_alive_cmd(_adapter *adapter, u8 enable, u8 pkt_type)
{
	struct hal_ops *pHalFunc = &adapter->hal_func;

	u8 u1H2CKeepAliveParm[H2C_KEEP_ALIVE_CTRL_LEN] = {0};
	u8 adopt = 1, check_period = 5;
	u8 ret = _FAIL;

	SET_H2CCMD_KEEPALIVE_PARM_ENABLE(u1H2CKeepAliveParm, enable);
	SET_H2CCMD_KEEPALIVE_PARM_ADOPT(u1H2CKeepAliveParm, adopt);
	SET_H2CCMD_KEEPALIVE_PARM_PKT_TYPE(u1H2CKeepAliveParm, pkt_type);
	SET_H2CCMD_KEEPALIVE_PARM_CHECK_PERIOD(u1H2CKeepAliveParm, check_period);
#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
	SET_H2CCMD_KEEPALIVE_PARM_PORT_NUM(u1H2CKeepAliveParm, adapter->hw_port);
	RTW_INFO("%s(): enable = %d, port = %d\n", __func__, enable, adapter->hw_port);
#else
	RTW_INFO("%s(): enable = %d\n", __func__, enable);
#endif
	ret = rtw_hal_fill_h2c_cmd(adapter,
				   H2C_KEEP_ALIVE,
				   H2C_KEEP_ALIVE_CTRL_LEN,
				   u1H2CKeepAliveParm);

	return ret;
}

static u8 rtw_hal_set_disconnect_decision_cmd(_adapter *adapter, u8 enable)
{
	struct hal_ops *pHalFunc = &adapter->hal_func;
	u8 u1H2CDisconDecisionParm[H2C_DISCON_DECISION_LEN] = {0};
	u8 adopt = 1, check_period = 10, trypkt_num = 0;
	u8 ret = _FAIL;

	SET_H2CCMD_DISCONDECISION_PARM_ENABLE(u1H2CDisconDecisionParm, enable);
	SET_H2CCMD_DISCONDECISION_PARM_ADOPT(u1H2CDisconDecisionParm, adopt);
	SET_H2CCMD_DISCONDECISION_PARM_CHECK_PERIOD(u1H2CDisconDecisionParm, check_period);
	SET_H2CCMD_DISCONDECISION_PARM_TRY_PKT_NUM(u1H2CDisconDecisionParm, trypkt_num);
#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
	SET_H2CCMD_DISCONDECISION_PORT_NUM(u1H2CDisconDecisionParm, adapter->hw_port);
	RTW_INFO("%s(): enable = %d, port = %d\n", __func__, enable, adapter->hw_port);
#else
	RTW_INFO("%s(): enable = %d\n", __func__, enable);
#endif

	ret = rtw_hal_fill_h2c_cmd(adapter,
				   H2C_DISCON_DECISION,
				   H2C_DISCON_DECISION_LEN,
				   u1H2CDisconDecisionParm);
	return ret;
}

static u8 rtw_hal_set_wowlan_ctrl_cmd(_adapter *adapter, u8 enable, u8 change_unit)
{
	struct security_priv *psecpriv = &adapter->securitypriv;
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(adapter);
	struct hal_ops *pHalFunc = &adapter->hal_func;

	u8 u1H2CWoWlanCtrlParm[H2C_WOWLAN_LEN] = {0};
	u8 discont_wake = 1, gpionum = 0, gpio_dur = 0;
	u8 hw_unicast = 0, gpio_pulse_cnt = 0, gpio_pulse_en = 0;
	u8 sdio_wakeup_enable = 1;
	u8 gpio_high_active = 0;
	u8 magic_pkt = 0;
	u8 gpio_unit = 0; /*0: 64ns, 1: 8ms*/
	u8 ret = _FAIL;

#ifdef CONFIG_GPIO_WAKEUP
	gpio_high_active = ppwrpriv->is_high_active;
	gpionum = WAKEUP_GPIO_IDX;
	sdio_wakeup_enable = 0;
#endif /* CONFIG_GPIO_WAKEUP */

	if (!ppwrpriv->wowlan_pno_enable)
		magic_pkt = enable;

	if (psecpriv->dot11PrivacyAlgrthm == _WEP40_ || psecpriv->dot11PrivacyAlgrthm == _WEP104_)
		hw_unicast = 1;
	else
		hw_unicast = 0;

	RTW_INFO("%s(): enable=%d change_unit=%d\n", __func__,
		 enable, change_unit);

	/* time = (gpio_dur/2) * gpio_unit, default:256 ms */
	if (enable && change_unit) {
		gpio_dur = 0x40;
		gpio_unit = 1;
		gpio_pulse_en = 1;
	}

#ifdef CONFIG_PLATFORM_ARM_RK3188
	if (enable) {
		gpio_pulse_en = 1;
		gpio_pulse_cnt = 0x04;
	}
#endif

	SET_H2CCMD_WOWLAN_FUNC_ENABLE(u1H2CWoWlanCtrlParm, enable);
	SET_H2CCMD_WOWLAN_PATTERN_MATCH_ENABLE(u1H2CWoWlanCtrlParm, enable);
	SET_H2CCMD_WOWLAN_MAGIC_PKT_ENABLE(u1H2CWoWlanCtrlParm, magic_pkt);
	SET_H2CCMD_WOWLAN_UNICAST_PKT_ENABLE(u1H2CWoWlanCtrlParm, hw_unicast);
	SET_H2CCMD_WOWLAN_ALL_PKT_DROP(u1H2CWoWlanCtrlParm, 0);
	SET_H2CCMD_WOWLAN_GPIO_ACTIVE(u1H2CWoWlanCtrlParm, gpio_high_active);

#ifdef CONFIG_GTK_OL
	/* GTK rekey only for AES, if GTK rekey is TKIP, then wake up*/
	if (psecpriv->binstallKCK_KEK == _TRUE)
		SET_H2CCMD_WOWLAN_REKEY_WAKE_UP(u1H2CWoWlanCtrlParm, 0);
	else
		SET_H2CCMD_WOWLAN_REKEY_WAKE_UP(u1H2CWoWlanCtrlParm, 1);
#else
	SET_H2CCMD_WOWLAN_REKEY_WAKE_UP(u1H2CWoWlanCtrlParm, enable);
#endif
	SET_H2CCMD_WOWLAN_DISCONNECT_WAKE_UP(u1H2CWoWlanCtrlParm, discont_wake);
	SET_H2CCMD_WOWLAN_GPIONUM(u1H2CWoWlanCtrlParm, gpionum);
	SET_H2CCMD_WOWLAN_DATAPIN_WAKE_UP(u1H2CWoWlanCtrlParm, sdio_wakeup_enable);

	SET_H2CCMD_WOWLAN_GPIO_DURATION(u1H2CWoWlanCtrlParm, gpio_dur);
	SET_H2CCMD_WOWLAN_CHANGE_UNIT(u1H2CWoWlanCtrlParm, gpio_unit);

	SET_H2CCMD_WOWLAN_GPIO_PULSE_EN(u1H2CWoWlanCtrlParm, gpio_pulse_en);
	SET_H2CCMD_WOWLAN_GPIO_PULSE_COUNT(u1H2CWoWlanCtrlParm, gpio_pulse_cnt);

	ret = rtw_hal_fill_h2c_cmd(adapter,
				   H2C_WOWLAN,
				   H2C_WOWLAN_LEN,
				   u1H2CWoWlanCtrlParm);
	return ret;
}

static u8 rtw_hal_set_remote_wake_ctrl_cmd(_adapter *adapter, u8 enable)
{
	struct hal_ops *pHalFunc = &adapter->hal_func;
	struct security_priv *psecuritypriv = &(adapter->securitypriv);
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(adapter);
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	u8 u1H2CRemoteWakeCtrlParm[H2C_REMOTE_WAKE_CTRL_LEN] = {0};
	u8 ret = _FAIL, count = 0;

	RTW_INFO("%s(): enable=%d\n", __func__, enable);

	if (!ppwrpriv->wowlan_pno_enable) {
		SET_H2CCMD_REMOTE_WAKECTRL_ENABLE(
			u1H2CRemoteWakeCtrlParm, enable);
		SET_H2CCMD_REMOTE_WAKE_CTRL_ARP_OFFLOAD_EN(
			u1H2CRemoteWakeCtrlParm, 1);
#ifdef CONFIG_GTK_OL
		if (psecuritypriv->binstallKCK_KEK == _TRUE) {
			SET_H2CCMD_REMOTE_WAKE_CTRL_GTK_OFFLOAD_EN(
				u1H2CRemoteWakeCtrlParm, 1);
		} else {
			RTW_INFO("no kck kek\n");
			SET_H2CCMD_REMOTE_WAKE_CTRL_GTK_OFFLOAD_EN(
				u1H2CRemoteWakeCtrlParm, 0);
		}
#endif /* CONFIG_GTK_OL */

		if (pregistrypriv->default_patterns_en == _FALSE) {
			SET_H2CCMD_REMOTE_WAKE_CTRL_FW_UNICAST_EN(
				u1H2CRemoteWakeCtrlParm, enable);
			/*
			 * filter NetBios name service pkt to avoid being waked-up
			 * by this kind of unicast pkt this exceptional modification
			 * is used for match competitor's behavior
			 */
			SET_H2CCMD_REMOTE_WAKE_CTRL_NBNS_FILTER_EN(
				u1H2CRemoteWakeCtrlParm, enable);
		}

		if ((psecuritypriv->dot11PrivacyAlgrthm == _AES_) ||
			(psecuritypriv->dot11PrivacyAlgrthm == _TKIP_) ||
			(psecuritypriv->dot11PrivacyAlgrthm == _NO_PRIVACY_)) {
			SET_H2CCMD_REMOTE_WAKE_CTRL_ARP_ACTION(
				u1H2CRemoteWakeCtrlParm, 0);
		} else {
			SET_H2CCMD_REMOTE_WAKE_CTRL_ARP_ACTION(
				u1H2CRemoteWakeCtrlParm, 1);
		}

		if (psecuritypriv->dot11PrivacyAlgrthm == _TKIP_) {
			SET_H2CCMD_REMOTE_WAKE_CTRL_TKIP_OFFLOAD_EN(
					u1H2CRemoteWakeCtrlParm, enable);

			if (IS_HARDWARE_TYPE_8188E(adapter) ||
			    IS_HARDWARE_TYPE_8812(adapter)) {
				SET_H2CCMD_REMOTE_WAKE_CTRL_TKIP_OFFLOAD_EN(
					u1H2CRemoteWakeCtrlParm, 0);
				SET_H2CCMD_REMOTE_WAKE_CTRL_ARP_ACTION(
					u1H2CRemoteWakeCtrlParm, 1);
			}
		}

		SET_H2CCMD_REMOTE_WAKE_CTRL_FW_PARSING_UNTIL_WAKEUP(
			u1H2CRemoteWakeCtrlParm, 1);
	}
#ifdef CONFIG_PNO_SUPPORT
	else {
		SET_H2CCMD_REMOTE_WAKECTRL_ENABLE(
			u1H2CRemoteWakeCtrlParm, enable);
		SET_H2CCMD_REMOTE_WAKE_CTRL_NLO_OFFLOAD_EN(
			u1H2CRemoteWakeCtrlParm, enable);
	}
#endif

#ifdef CONFIG_P2P_WOWLAN
	if (_TRUE == ppwrpriv->wowlan_p2p_mode) {
		RTW_INFO("P2P OFFLOAD ENABLE\n");
		SET_H2CCMD_REMOTE_WAKE_CTRL_P2P_OFFLAD_EN(u1H2CRemoteWakeCtrlParm, 1);
	} else {
		RTW_INFO("P2P OFFLOAD DISABLE\n");
		SET_H2CCMD_REMOTE_WAKE_CTRL_P2P_OFFLAD_EN(u1H2CRemoteWakeCtrlParm, 0);
	}
#endif /* CONFIG_P2P_WOWLAN */


	ret = rtw_hal_fill_h2c_cmd(adapter,
				   H2C_REMOTE_WAKE_CTRL,
				   H2C_REMOTE_WAKE_CTRL_LEN,
				   u1H2CRemoteWakeCtrlParm);
	return ret;
}

static u8 rtw_hal_set_global_info_cmd(_adapter *adapter, u8 group_alg, u8 pairwise_alg)
{
	struct hal_ops *pHalFunc = &adapter->hal_func;
	u8 ret = _FAIL;
	u8 u1H2CAOACGlobalInfoParm[H2C_AOAC_GLOBAL_INFO_LEN] = {0};

	RTW_INFO("%s(): group_alg=%d pairwise_alg=%d\n",
		 __func__, group_alg, pairwise_alg);
	SET_H2CCMD_AOAC_GLOBAL_INFO_PAIRWISE_ENC_ALG(u1H2CAOACGlobalInfoParm,
			pairwise_alg);
	SET_H2CCMD_AOAC_GLOBAL_INFO_GROUP_ENC_ALG(u1H2CAOACGlobalInfoParm,
			group_alg);

	ret = rtw_hal_fill_h2c_cmd(adapter,
				   H2C_AOAC_GLOBAL_INFO,
				   H2C_AOAC_GLOBAL_INFO_LEN,
				   u1H2CAOACGlobalInfoParm);

	return ret;
}

#ifdef CONFIG_PNO_SUPPORT
static u8 rtw_hal_set_scan_offload_info_cmd(_adapter *adapter,
		PRSVDPAGE_LOC rsvdpageloc, u8 enable)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	struct hal_ops *pHalFunc = &adapter->hal_func;

	u8 u1H2CScanOffloadInfoParm[H2C_SCAN_OFFLOAD_CTRL_LEN] = {0};
	u8 res = 0, count = 0, ret = _FAIL;

	RTW_INFO("%s: loc_probe_packet:%d, loc_scan_info: %d loc_ssid_info:%d\n",
		 __func__, rsvdpageloc->LocProbePacket,
		 rsvdpageloc->LocScanInfo, rsvdpageloc->LocSSIDInfo);

	SET_H2CCMD_AOAC_NLO_FUN_EN(u1H2CScanOffloadInfoParm, enable);
	SET_H2CCMD_AOAC_NLO_IPS_EN(u1H2CScanOffloadInfoParm, enable);
	SET_H2CCMD_AOAC_RSVDPAGE_LOC_SCAN_INFO(u1H2CScanOffloadInfoParm,
					       rsvdpageloc->LocScanInfo);
	SET_H2CCMD_AOAC_RSVDPAGE_LOC_PROBE_PACKET(u1H2CScanOffloadInfoParm,
			rsvdpageloc->LocProbePacket);
	/*
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_SSID_INFO(u1H2CScanOffloadInfoParm,
				rsvdpageloc->LocSSIDInfo);
	*/
	ret = rtw_hal_fill_h2c_cmd(adapter,
				   H2C_D0_SCAN_OFFLOAD_INFO,
				   H2C_SCAN_OFFLOAD_CTRL_LEN,
				   u1H2CScanOffloadInfoParm);
	return ret;
}
#endif /* CONFIG_PNO_SUPPORT */

void rtw_hal_set_fw_wow_related_cmd(_adapter *padapter, u8 enable)
{
	struct security_priv *psecpriv = &padapter->securitypriv;
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct sta_info *psta = NULL;
	u16 media_status_rpt;
	u8	pkt_type = 0;
	u8 ret = _SUCCESS;

	RTW_PRINT("+%s()+: enable=%d\n", __func__, enable);

	rtw_hal_set_wowlan_ctrl_cmd(padapter, enable, _FALSE);

	if (enable) {
		rtw_hal_set_global_info_cmd(padapter,
					    psecpriv->dot118021XGrpPrivacy,
					    psecpriv->dot11PrivacyAlgrthm);

		if (!(ppwrpriv->wowlan_pno_enable)) {
			rtw_hal_set_disconnect_decision_cmd(padapter, enable);
#ifdef CONFIG_ARP_KEEP_ALIVE
			if ((psecpriv->dot11PrivacyAlgrthm == _WEP40_) ||
			    (psecpriv->dot11PrivacyAlgrthm == _WEP104_))
				pkt_type = 0;
			else
				pkt_type = 1;
#else
			pkt_type = 0;
#endif /* CONFIG_ARP_KEEP_ALIVE */
			rtw_hal_set_keep_alive_cmd(padapter, enable, pkt_type);
		}
		rtw_hal_set_remote_wake_ctrl_cmd(padapter, enable);
#ifdef CONFIG_PNO_SUPPORT
		rtw_hal_check_pno_enabled(padapter);
#endif /* CONFIG_PNO_SUPPORT */
	} else {
#if 0
		{
			u32 PageSize = 0;
			rtw_hal_get_def_var(padapter, HAL_DEF_TX_PAGE_SIZE, (u8 *)&PageSize);
			dump_TX_FIFO(padapter, 4, PageSize);
		}
#endif

		rtw_hal_set_remote_wake_ctrl_cmd(padapter, enable);
	}
	RTW_PRINT("-%s()-\n", __func__);
}
#endif /* CONFIG_WOWLAN */

#ifdef CONFIG_AP_WOWLAN
static u8 rtw_hal_set_ap_wowlan_ctrl_cmd(_adapter *adapter, u8 enable)
{
	struct security_priv *psecpriv = &adapter->securitypriv;
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(adapter);
	struct hal_ops *pHalFunc = &adapter->hal_func;

	u8 u1H2CAPWoWlanCtrlParm[H2C_AP_WOW_GPIO_CTRL_LEN] = {0};
	u8 gpionum = 0, gpio_dur = 0;
	u8 gpio_pulse = enable;
	u8 sdio_wakeup_enable = 1;
	u8 gpio_high_active = 0;
	u8 ret = _FAIL;

#ifdef CONFIG_GPIO_WAKEUP
	gpio_high_active = ppwrpriv->is_high_active;
	gpionum = WAKEUP_GPIO_IDX;
	sdio_wakeup_enable = 0;
#endif /*CONFIG_GPIO_WAKEUP*/

	RTW_INFO("%s(): enable=%d\n", __func__, enable);

	SET_H2CCMD_AP_WOW_GPIO_CTRL_INDEX(u1H2CAPWoWlanCtrlParm,
					  gpionum);
	SET_H2CCMD_AP_WOW_GPIO_CTRL_PLUS(u1H2CAPWoWlanCtrlParm,
					 gpio_pulse);
	SET_H2CCMD_AP_WOW_GPIO_CTRL_HIGH_ACTIVE(u1H2CAPWoWlanCtrlParm,
						gpio_high_active);
	SET_H2CCMD_AP_WOW_GPIO_CTRL_EN(u1H2CAPWoWlanCtrlParm,
				       enable);
	SET_H2CCMD_AP_WOW_GPIO_CTRL_DURATION(u1H2CAPWoWlanCtrlParm,
					     gpio_dur);

	ret = rtw_hal_fill_h2c_cmd(adapter,
				   H2C_AP_WOW_GPIO_CTRL,
				   H2C_AP_WOW_GPIO_CTRL_LEN,
				   u1H2CAPWoWlanCtrlParm);

	return ret;
}

static u8 rtw_hal_set_ap_offload_ctrl_cmd(_adapter *adapter, u8 enable)
{
	struct hal_ops *pHalFunc = &adapter->hal_func;
	u8 u1H2CAPOffloadCtrlParm[H2C_WOWLAN_LEN] = {0};
	u8 ret = _FAIL;

	RTW_INFO("%s(): bFuncEn=%d\n", __func__, enable);

	SET_H2CCMD_AP_WOWLAN_EN(u1H2CAPOffloadCtrlParm, enable);

	ret = rtw_hal_fill_h2c_cmd(adapter,
				   H2C_AP_OFFLOAD,
				   H2C_AP_OFFLOAD_LEN,
				   u1H2CAPOffloadCtrlParm);

	return ret;
}

static u8 rtw_hal_set_ap_ps_cmd(_adapter *adapter, u8 enable)
{
	struct hal_ops *pHalFunc = &adapter->hal_func;
	u8 ap_ps_parm[H2C_AP_PS_LEN] = {0};
	u8 ret = _FAIL;

	RTW_INFO("%s(): enable=%d\n" , __func__ , enable);

	SET_H2CCMD_AP_WOW_PS_EN(ap_ps_parm, enable);
#ifndef CONFIG_USB_HCI
	SET_H2CCMD_AP_WOW_PS_32K_EN(ap_ps_parm, enable);
#endif /*CONFIG_USB_HCI*/
	SET_H2CCMD_AP_WOW_PS_RF(ap_ps_parm, enable);

	if (enable)
		SET_H2CCMD_AP_WOW_PS_DURATION(ap_ps_parm, 0x32);
	else
		SET_H2CCMD_AP_WOW_PS_DURATION(ap_ps_parm, 0x0);

	ret = rtw_hal_fill_h2c_cmd(adapter, H2C_SAP_PS_,
				   H2C_AP_PS_LEN, ap_ps_parm);

	return ret;
}

static void rtw_hal_set_ap_rsvdpage_loc_cmd(PADAPTER padapter,
		PRSVDPAGE_LOC rsvdpageloc)
{
	struct hal_ops *pHalFunc = &padapter->hal_func;
	u8 rsvdparm[H2C_AOAC_RSVDPAGE_LOC_LEN] = {0};
	u8 ret = _FAIL, header = 0;

	if (pHalFunc->fill_h2c_cmd == NULL) {
		RTW_INFO("%s: Please hook fill_h2c_cmd first!\n", __func__);
		return;
	}

	header = rtw_read8(padapter, REG_BCNQ_BDNY);

	RTW_INFO("%s: beacon: %d, probeRsp: %d, header:0x%02x\n", __func__,
		 rsvdpageloc->LocApOffloadBCN,
		 rsvdpageloc->LocProbeRsp,
		 header);

	SET_H2CCMD_AP_WOWLAN_RSVDPAGE_LOC_BCN(rsvdparm,
				      rsvdpageloc->LocApOffloadBCN + header);

	ret = rtw_hal_fill_h2c_cmd(padapter, H2C_BCN_RSVDPAGE,
				   H2C_BCN_RSVDPAGE_LEN, rsvdparm);

	if (ret == _FAIL)
		RTW_INFO("%s: H2C_BCN_RSVDPAGE cmd fail\n", __func__);

	rtw_msleep_os(10);

	_rtw_memset(&rsvdparm, 0, sizeof(rsvdparm));

	SET_H2CCMD_AP_WOWLAN_RSVDPAGE_LOC_ProbeRsp(rsvdparm,
			rsvdpageloc->LocProbeRsp + header);

	ret = rtw_hal_fill_h2c_cmd(padapter, H2C_PROBERSP_RSVDPAGE,
				   H2C_PROBERSP_RSVDPAGE_LEN, rsvdparm);

	if (ret == _FAIL)
		RTW_INFO("%s: H2C_PROBERSP_RSVDPAGE cmd fail\n", __func__);

	rtw_msleep_os(10);
}

static void rtw_hal_set_fw_ap_wow_related_cmd(_adapter *padapter, u8 enable)
{
	rtw_hal_set_ap_offload_ctrl_cmd(padapter, enable);
	rtw_hal_set_ap_wowlan_ctrl_cmd(padapter, enable);
	rtw_hal_set_ap_ps_cmd(padapter, enable);
}

static void rtw_hal_ap_wow_enable(_adapter *padapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct hal_ops *pHalFunc = &padapter->hal_func;
	struct sta_info *psta = NULL;
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
#ifdef DBG_CHECK_FW_PS_STATE
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
#endif /*DBG_CHECK_FW_PS_STATE*/
	int res;
	u16 media_status_rpt;

	RTW_INFO("%s, WOWLAN_AP_ENABLE\n", __func__);
#ifdef DBG_CHECK_FW_PS_STATE
	if (rtw_fw_ps_state(padapter) == _FAIL) {
		pdbgpriv->dbg_enwow_dload_fw_fail_cnt++;
		RTW_PRINT("wowlan enable no leave 32k\n");
	}
#endif /*DBG_CHECK_FW_PS_STATE*/

	/* 1. Download WOWLAN FW*/
	rtw_hal_fw_dl(padapter, _TRUE);

	media_status_rpt = RT_MEDIA_CONNECT;
	rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_JOINBSSRPT,
			  (u8 *)&media_status_rpt);

	issue_beacon(padapter, 0);

	rtw_msleep_os(2);
	#if defined(CONFIG_RTL8188E)
	if (IS_HARDWARE_TYPE_8188E(padapter))
		rtw_hal_disable_tx_report(padapter);
	#endif
	/* RX DMA stop */
	res = rtw_hal_pause_rx_dma(padapter);
	if (res == _FAIL)
		RTW_PRINT("[WARNING] pause RX DMA fail\n");

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	/* Enable CPWM2 only. */
	res = rtw_hal_enable_cpwm2(padapter);
	if (res == _FAIL)
		RTW_PRINT("[WARNING] enable cpwm2 fail\n");
#endif

#ifdef CONFIG_GPIO_WAKEUP
	rtw_hal_switch_gpio_wl_ctrl(padapter, WAKEUP_GPIO_IDX, _TRUE);
#endif
	/* 5. Set Enable WOWLAN H2C command. */
	RTW_PRINT("Set Enable AP WOWLan cmd\n");
	rtw_hal_set_fw_ap_wow_related_cmd(padapter, 1);

	rtw_write8(padapter, REG_MCUTST_WOWLAN, 0);
#ifdef CONFIG_USB_HCI
	rtw_mi_intf_stop(padapter);
	/* Invoid SE0 reset signal during suspending*/
	rtw_write8(padapter, REG_RSV_CTRL, 0x20);
	if (IS_8188F(pHalData->version_id) == FALSE)
		rtw_write8(padapter, REG_RSV_CTRL, 0x60);
#endif /*CONFIG_USB_HCI*/
}

static void rtw_hal_ap_wow_disable(_adapter *padapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	struct hal_ops *pHalFunc = &padapter->hal_func;
#ifdef DBG_CHECK_FW_PS_STATE
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
#endif /*DBG_CHECK_FW_PS_STATE*/
	u16 media_status_rpt;
	u8 val8;

	RTW_INFO("%s, WOWLAN_AP_DISABLE\n", __func__);
	/* 1. Read wakeup reason*/
	pwrctl->wowlan_wake_reason = rtw_read8(padapter, REG_MCUTST_WOWLAN);

	RTW_PRINT("wakeup_reason: 0x%02x\n",
		  pwrctl->wowlan_wake_reason);

	rtw_hal_set_fw_ap_wow_related_cmd(padapter, 0);

	rtw_msleep_os(2);
#ifdef DBG_CHECK_FW_PS_STATE
	if (rtw_fw_ps_state(padapter) == _FAIL) {
		pdbgpriv->dbg_diswow_dload_fw_fail_cnt++;
		RTW_PRINT("wowlan enable no leave 32k\n");
	}
#endif /*DBG_CHECK_FW_PS_STATE*/

	#if defined(CONFIG_RTL8188E)
	if (IS_HARDWARE_TYPE_8188E(padapter))
		rtw_hal_enable_tx_report(padapter);
	#endif

	rtw_hal_force_enable_rxdma(padapter);

	rtw_hal_fw_dl(padapter, _FALSE);

#ifdef CONFIG_GPIO_WAKEUP
	val8 = (pwrctl->is_high_active == 0) ? 1 : 0;
	RTW_PRINT("Set Wake GPIO to default(%d).\n", val8);
	rtw_hal_set_output_gpio(padapter, WAKEUP_GPIO_IDX, val8);

	rtw_hal_switch_gpio_wl_ctrl(padapter, WAKEUP_GPIO_IDX, _FALSE);
#endif
	media_status_rpt = RT_MEDIA_CONNECT;

	rtw_hal_set_hwreg(padapter, HW_VAR_H2C_FW_JOINBSSRPT,
			  (u8 *)&media_status_rpt);

	issue_beacon(padapter, 0);
}
#endif /*CONFIG_AP_WOWLAN*/

#ifdef CONFIG_P2P_WOWLAN
static int update_hidden_ssid(u8 *ies, u32 ies_len, u8 hidden_ssid_mode)
{
	u8 *ssid_ie;
	sint ssid_len_ori;
	int len_diff = 0;

	ssid_ie = rtw_get_ie(ies,  WLAN_EID_SSID, &ssid_len_ori, ies_len);

	/* RTW_INFO("%s hidden_ssid_mode:%u, ssid_ie:%p, ssid_len_ori:%d\n", __FUNCTION__, hidden_ssid_mode, ssid_ie, ssid_len_ori); */

	if (ssid_ie && ssid_len_ori > 0) {
		switch (hidden_ssid_mode) {
		case 1: {
			u8 *next_ie = ssid_ie + 2 + ssid_len_ori;
			u32 remain_len = 0;

			remain_len = ies_len - (next_ie - ies);

			ssid_ie[1] = 0;
			_rtw_memcpy(ssid_ie + 2, next_ie, remain_len);
			len_diff -= ssid_len_ori;

			break;
		}
		case 2:
			_rtw_memset(&ssid_ie[2], 0, ssid_len_ori);
			break;
		default:
			break;
		}
	}

	return len_diff;
}

static void rtw_hal_construct_P2PBeacon(_adapter *padapter, u8 *pframe, u32 *pLength)
{
	/* struct xmit_frame	*pmgntframe; */
	/* struct pkt_attrib	*pattrib; */
	/* unsigned char	*pframe; */
	struct rtw_ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	unsigned int	rate_len;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	u32	pktlen;
	/* #if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME) */
	/*	_irqL irqL;
	 *	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	 * #endif */ /* #if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME) */
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*cur_network = &(pmlmeinfo->network);
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
#endif /* CONFIG_P2P */

	/* for debug */
	u8 *dbgbuf = pframe;
	u8 dbgbufLen = 0, index = 0;

	RTW_INFO("%s\n", __FUNCTION__);
	/* #if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME) */
	/*	_enter_critical_bh(&pmlmepriv->bcn_update_lock, &irqL);
	 * #endif */ /* #if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME) */

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;


	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(cur_network), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0/*pmlmeext->mgnt_seq*/);
	/* pmlmeext->mgnt_seq++; */
	set_frame_sub_type(pframe, WIFI_BEACON);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	if ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) {
		/* RTW_INFO("ie len=%d\n", cur_network->IELength); */
#ifdef CONFIG_P2P
		/* for P2P : Primary Device Type & Device Name */
		u32 wpsielen = 0, insert_len = 0;
		u8 *wpsie = NULL;
		wpsie = rtw_get_wps_ie(cur_network->IEs + _FIXED_IE_LENGTH_, cur_network->IELength - _FIXED_IE_LENGTH_, NULL, &wpsielen);

		if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO) && wpsie && wpsielen > 0) {
			uint wps_offset, remainder_ielen;
			u8 *premainder_ie, *pframe_wscie;

			wps_offset = (uint)(wpsie - cur_network->IEs);

			premainder_ie = wpsie + wpsielen;

			remainder_ielen = cur_network->IELength - wps_offset - wpsielen;

#ifdef CONFIG_IOCTL_CFG80211
			if (pwdinfo->driver_interface == DRIVER_CFG80211) {
				if (pmlmepriv->wps_beacon_ie && pmlmepriv->wps_beacon_ie_len > 0) {
					_rtw_memcpy(pframe, cur_network->IEs, wps_offset);
					pframe += wps_offset;
					pktlen += wps_offset;

					_rtw_memcpy(pframe, pmlmepriv->wps_beacon_ie, pmlmepriv->wps_beacon_ie_len);
					pframe += pmlmepriv->wps_beacon_ie_len;
					pktlen += pmlmepriv->wps_beacon_ie_len;

					/* copy remainder_ie to pframe */
					_rtw_memcpy(pframe, premainder_ie, remainder_ielen);
					pframe += remainder_ielen;
					pktlen += remainder_ielen;
				} else {
					_rtw_memcpy(pframe, cur_network->IEs, cur_network->IELength);
					pframe += cur_network->IELength;
					pktlen += cur_network->IELength;
				}
			} else
#endif /* CONFIG_IOCTL_CFG80211 */
			{
				pframe_wscie = pframe + wps_offset;
				_rtw_memcpy(pframe, cur_network->IEs, wps_offset + wpsielen);
				pframe += (wps_offset + wpsielen);
				pktlen += (wps_offset + wpsielen);

				/* now pframe is end of wsc ie, insert Primary Device Type & Device Name */
				/*	Primary Device Type */
				/*	Type: */
				*(u16 *)(pframe + insert_len) = cpu_to_be16(WPS_ATTR_PRIMARY_DEV_TYPE);
				insert_len += 2;

				/*	Length: */
				*(u16 *)(pframe + insert_len) = cpu_to_be16(0x0008);
				insert_len += 2;

				/*	Value: */
				/*	Category ID */
				*(u16 *)(pframe + insert_len) = cpu_to_be16(WPS_PDT_CID_MULIT_MEDIA);
				insert_len += 2;

				/*	OUI */
				*(u32 *)(pframe + insert_len) = cpu_to_be32(WPSOUI);
				insert_len += 4;

				/*	Sub Category ID */
				*(u16 *)(pframe + insert_len) = cpu_to_be16(WPS_PDT_SCID_MEDIA_SERVER);
				insert_len += 2;


				/*	Device Name */
				/*	Type: */
				*(u16 *)(pframe + insert_len) = cpu_to_be16(WPS_ATTR_DEVICE_NAME);
				insert_len += 2;

				/*	Length: */
				*(u16 *)(pframe + insert_len) = cpu_to_be16(pwdinfo->device_name_len);
				insert_len += 2;

				/*	Value: */
				_rtw_memcpy(pframe + insert_len, pwdinfo->device_name, pwdinfo->device_name_len);
				insert_len += pwdinfo->device_name_len;


				/* update wsc ie length */
				*(pframe_wscie + 1) = (wpsielen - 2) + insert_len;

				/* pframe move to end */
				pframe += insert_len;
				pktlen += insert_len;

				/* copy remainder_ie to pframe */
				_rtw_memcpy(pframe, premainder_ie, remainder_ielen);
				pframe += remainder_ielen;
				pktlen += remainder_ielen;
			}
		} else
#endif /* CONFIG_P2P */
		{
			int len_diff;
			_rtw_memcpy(pframe, cur_network->IEs, cur_network->IELength);
			len_diff = update_hidden_ssid(
					   pframe + _BEACON_IE_OFFSET_
				   , cur_network->IELength - _BEACON_IE_OFFSET_
					   , pmlmeinfo->hidden_ssid_mode
				   );
			pframe += (cur_network->IELength + len_diff);
			pktlen += (cur_network->IELength + len_diff);
		}
#if 0
		{
			u8 *wps_ie;
			uint wps_ielen;
			u8 sr = 0;
			wps_ie = rtw_get_wps_ie(pmgntframe->buf_addr + TXDESC_OFFSET + sizeof(struct rtw_ieee80211_hdr_3addr) + _BEACON_IE_OFFSET_,
				pattrib->pktlen - sizeof(struct rtw_ieee80211_hdr_3addr) - _BEACON_IE_OFFSET_, NULL, &wps_ielen);
			if (wps_ie && wps_ielen > 0)
				rtw_get_wps_attr_content(wps_ie,  wps_ielen, WPS_ATTR_SELECTED_REGISTRAR, (u8 *)(&sr), NULL);
			if (sr != 0)
				set_fwstate(pmlmepriv, WIFI_UNDER_WPS);
			else
				_clr_fwstate_(pmlmepriv, WIFI_UNDER_WPS);
		}
#endif
#ifdef CONFIG_P2P
		if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
			u32 len;
#ifdef CONFIG_IOCTL_CFG80211
			if (pwdinfo->driver_interface == DRIVER_CFG80211) {
				len = pmlmepriv->p2p_beacon_ie_len;
				if (pmlmepriv->p2p_beacon_ie && len > 0)
					_rtw_memcpy(pframe, pmlmepriv->p2p_beacon_ie, len);
			} else
#endif /* CONFIG_IOCTL_CFG80211 */
			{
				len = build_beacon_p2p_ie(pwdinfo, pframe);
			}

			pframe += len;
			pktlen += len;

			#ifdef CONFIG_WFD
			len = rtw_append_beacon_wfd_ie(padapter, pframe);
			pframe += len;
			pktlen += len;
			#endif

		}
#endif /* CONFIG_P2P */

		goto _issue_bcn;

	}

	/* below for ad-hoc mode */

	/* timestamp will be inserted by hardware */
	pframe += 8;
	pktlen += 8;

	/* beacon interval: 2 bytes */

	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pktlen += 2;

	/* capability info: 2 bytes */

	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pktlen += 2;

	/* SSID */
	pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pktlen);

	/* supported rates... */
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8) ? 8 : rate_len), cur_network->SupportedRates, &pktlen);

	/* DS parameter set */
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pktlen);

	/* if( (pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) */
	{
		u8 erpinfo = 0;
		u32 ATIMWindow;
		/* IBSS Parameter Set... */
		/* ATIMWindow = cur->Configuration.ATIMWindow; */
		ATIMWindow = 0;
		pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pktlen);

		/* ERP IE */
		pframe = rtw_set_ie(pframe, _ERPINFO_IE_, 1, &erpinfo, &pktlen);
	}


	/* EXTERNDED SUPPORTED RATE */
	if (rate_len > 8)
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pktlen);


	/* todo:HT for adhoc */

_issue_bcn:

	/* #if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME) */
	/*	pmlmepriv->update_bcn = _FALSE;
	 *
	 *	_exit_critical_bh(&pmlmepriv->bcn_update_lock, &irqL);
	 * #endif */ /* #if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME) */

	*pLength = pktlen;
#if 0
	/* printf dbg msg */
	dbgbufLen = pktlen;
	RTW_INFO("======> DBG MSG FOR CONSTRAUCT P2P BEACON\n");

	for (index = 0; index < dbgbufLen; index++)
		printk("%x ", *(dbgbuf + index));

	printk("\n");
	RTW_INFO("<====== DBG MSG FOR CONSTRAUCT P2P BEACON\n");

#endif
}

static int get_reg_classes_full_count(struct p2p_channels channel_list)
{
	int cnt = 0;
	int i;

	for (i = 0; i < channel_list.reg_classes; i++)
		cnt += channel_list.reg_class[i].channels;

	return cnt;
}

static void rtw_hal_construct_P2PProbeRsp(_adapter *padapter, u8 *pframe, u32 *pLength)
{
	/* struct xmit_frame			*pmgntframe; */
	/* struct pkt_attrib			*pattrib; */
	/* unsigned char					*pframe; */
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	unsigned char					*mac;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	/* WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network); */
	u16					beacon_interval = 100;
	u16					capInfo = 0;
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	u8					wpsie[255] = { 0x00 };
	u32					wpsielen = 0, p2pielen = 0;
	u32					pktlen;
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif
#ifdef CONFIG_INTEL_WIDI
	u8 zero_array_check[L2SDTA_SERVICE_VE_LEN] = { 0x00 };
#endif /* CONFIG_INTEL_WIDI */

	/* for debug */
	u8 *dbgbuf = pframe;
	u8 dbgbufLen = 0, index = 0;

	RTW_INFO("%s\n", __FUNCTION__);
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	mac = adapter_mac_addr(padapter);

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	/* DA filled by FW */
	_rtw_memset(pwlanhdr->addr1, 0, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, mac, ETH_ALEN);

	/*	Use the device address for BSSID field.	 */
	_rtw_memcpy(pwlanhdr->addr3, mac, ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	set_frame_sub_type(fctrl, WIFI_PROBERSP);

	pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	pframe += pktlen;


	/* timestamp will be inserted by hardware */
	pframe += 8;
	pktlen += 8;

	/* beacon interval: 2 bytes */
	_rtw_memcpy(pframe, (unsigned char *) &beacon_interval, 2);
	pframe += 2;
	pktlen += 2;

	/*	capability info: 2 bytes */
	/*	ESS and IBSS bits must be 0 (defined in the 3.1.2.1.1 of WiFi Direct Spec) */
	capInfo |= cap_ShortPremble;
	capInfo |= cap_ShortSlot;

	_rtw_memcpy(pframe, (unsigned char *) &capInfo, 2);
	pframe += 2;
	pktlen += 2;


	/* SSID */
	pframe = rtw_set_ie(pframe, _SSID_IE_, 7, pwdinfo->p2p_wildcard_ssid, &pktlen);

	/* supported rates... */
	/*	Use the OFDM rate in the P2P probe response frame. ( 6(B), 9(B), 12, 18, 24, 36, 48, 54 ) */
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, 8, pwdinfo->support_rate, &pktlen);

	/* DS parameter set */
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&pwdinfo->listen_channel, &pktlen);

#ifdef CONFIG_IOCTL_CFG80211
	if (pwdinfo->driver_interface == DRIVER_CFG80211) {
		if (pmlmepriv->wps_probe_resp_ie != NULL && pmlmepriv->p2p_probe_resp_ie != NULL) {
			/* WPS IE */
			_rtw_memcpy(pframe, pmlmepriv->wps_probe_resp_ie, pmlmepriv->wps_probe_resp_ie_len);
			pktlen += pmlmepriv->wps_probe_resp_ie_len;
			pframe += pmlmepriv->wps_probe_resp_ie_len;

			/* P2P IE */
			_rtw_memcpy(pframe, pmlmepriv->p2p_probe_resp_ie, pmlmepriv->p2p_probe_resp_ie_len);
			pktlen += pmlmepriv->p2p_probe_resp_ie_len;
			pframe += pmlmepriv->p2p_probe_resp_ie_len;
		}
	} else
#endif /* CONFIG_IOCTL_CFG80211		 */
	{

		/*	Todo: WPS IE */
		/*	Noted by Albert 20100907 */
		/*	According to the WPS specification, all the WPS attribute is presented by Big Endian. */

		wpsielen = 0;
		/*	WPS OUI */
		*(u32 *)(wpsie) = cpu_to_be32(WPSOUI);
		wpsielen += 4;

		/*	WPS version */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_VER1);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0001);
		wpsielen += 2;

		/*	Value: */
		wpsie[wpsielen++] = WPS_VERSION_1;	/*	Version 1.0 */

#ifdef CONFIG_INTEL_WIDI
		/*	Commented by Kurt */
		/*	Appended WiDi info. only if we did issued_probereq_widi(), and then we saved ven. ext. in pmlmepriv->sa_ext. */
		if (_rtw_memcmp(pmlmepriv->sa_ext, zero_array_check, L2SDTA_SERVICE_VE_LEN) == _FALSE
		    || pmlmepriv->num_p2p_sdt != 0) {
			/* Sec dev type */
			*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_SEC_DEV_TYPE_LIST);
			wpsielen += 2;

			/*	Length: */
			*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0008);
			wpsielen += 2;

			/*	Value: */
			/*	Category ID */
			*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_PDT_CID_DISPLAYS);
			wpsielen += 2;

			/*	OUI */
			*(u32 *)(wpsie + wpsielen) = cpu_to_be32(INTEL_DEV_TYPE_OUI);
			wpsielen += 4;

			*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_PDT_SCID_WIDI_CONSUMER_SINK);
			wpsielen += 2;

			if (_rtw_memcmp(pmlmepriv->sa_ext, zero_array_check, L2SDTA_SERVICE_VE_LEN) == _FALSE) {
				/*	Vendor Extension */
				_rtw_memcpy(wpsie + wpsielen, pmlmepriv->sa_ext, L2SDTA_SERVICE_VE_LEN);
				wpsielen += L2SDTA_SERVICE_VE_LEN;
			}
		}
#endif /* CONFIG_INTEL_WIDI */

		/*	WiFi Simple Config State */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_SIMPLE_CONF_STATE);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0001);
		wpsielen += 2;

		/*	Value: */
		wpsie[wpsielen++] = WPS_WSC_STATE_NOT_CONFIG;	/*	Not Configured. */

		/*	Response Type */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_RESP_TYPE);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0001);
		wpsielen += 2;

		/*	Value: */
		wpsie[wpsielen++] = WPS_RESPONSE_TYPE_8021X;

		/*	UUID-E */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_UUID_E);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0010);
		wpsielen += 2;

		/*	Value: */
		if (pwdinfo->external_uuid == 0) {
			_rtw_memset(wpsie + wpsielen, 0x0, 16);
			_rtw_memcpy(wpsie + wpsielen, mac, ETH_ALEN);
		} else
			_rtw_memcpy(wpsie + wpsielen, pwdinfo->uuid, 0x10);
		wpsielen += 0x10;

		/*	Manufacturer */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_MANUFACTURER);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0007);
		wpsielen += 2;

		/*	Value: */
		_rtw_memcpy(wpsie + wpsielen, "Realtek", 7);
		wpsielen += 7;

		/*	Model Name */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_MODEL_NAME);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0006);
		wpsielen += 2;

		/*	Value: */
		_rtw_memcpy(wpsie + wpsielen, "8192CU", 6);
		wpsielen += 6;

		/*	Model Number */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_MODEL_NUMBER);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0001);
		wpsielen += 2;

		/*	Value: */
		wpsie[wpsielen++] = 0x31;		/*	character 1 */

		/*	Serial Number */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_SERIAL_NUMBER);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(ETH_ALEN);
		wpsielen += 2;

		/*	Value: */
		_rtw_memcpy(wpsie + wpsielen, "123456" , ETH_ALEN);
		wpsielen += ETH_ALEN;

		/*	Primary Device Type */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_PRIMARY_DEV_TYPE);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0008);
		wpsielen += 2;

		/*	Value: */
		/*	Category ID */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_PDT_CID_MULIT_MEDIA);
		wpsielen += 2;

		/*	OUI */
		*(u32 *)(wpsie + wpsielen) = cpu_to_be32(WPSOUI);
		wpsielen += 4;

		/*	Sub Category ID */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_PDT_SCID_MEDIA_SERVER);
		wpsielen += 2;

		/*	Device Name */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_DEVICE_NAME);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(pwdinfo->device_name_len);
		wpsielen += 2;

		/*	Value: */
		_rtw_memcpy(wpsie + wpsielen, pwdinfo->device_name, pwdinfo->device_name_len);
		wpsielen += pwdinfo->device_name_len;

		/*	Config Method */
		/*	Type: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_CONF_METHOD);
		wpsielen += 2;

		/*	Length: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0002);
		wpsielen += 2;

		/*	Value: */
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(pwdinfo->supported_wps_cm);
		wpsielen += 2;


		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pktlen);


		p2pielen = build_probe_resp_p2p_ie(pwdinfo, pframe);
		pframe += p2pielen;
		pktlen += p2pielen;
	}

#ifdef CONFIG_WFD
	wfdielen = rtw_append_probe_resp_wfd_ie(padapter, pframe);
	pframe += wfdielen;
	pktlen += wfdielen;
#endif

	*pLength = pktlen;

#if 0
	/* printf dbg msg */
	dbgbufLen = pktlen;
	RTW_INFO("======> DBG MSG FOR CONSTRAUCT P2P Probe Rsp\n");

	for (index = 0; index < dbgbufLen; index++)
		printk("%x ", *(dbgbuf + index));

	printk("\n");
	RTW_INFO("<====== DBG MSG FOR CONSTRAUCT P2P Probe Rsp\n");
#endif
}
static void rtw_hal_construct_P2PNegoRsp(_adapter *padapter, u8 *pframe, u32 *pLength)
{
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_GO_NEGO_RESP;
	u8			wpsie[255] = { 0x00 }, p2pie[255] = { 0x00 };
	u8			p2pielen = 0, i;
	uint			wpsielen = 0;
	u16			wps_devicepassword_id = 0x0000;
	uint			wps_devicepassword_id_len = 0;
	u8			channel_cnt_24g = 0, channel_cnt_5gl = 0, channel_cnt_5gh;
	u16			len_channellist_attr = 0;
	u32			pktlen;
	u8			dialogToken = 0;

	/* struct xmit_frame			*pmgntframe; */
	/* struct pkt_attrib			*pattrib; */
	/* unsigned char					*pframe; */
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	/* WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network); */

#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif

	/* for debug */
	u8 *dbgbuf = pframe;
	u8 dbgbufLen = 0, index = 0;

	RTW_INFO("%s\n", __FUNCTION__);
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	/* RA, filled by FW */
	_rtw_memset(pwlanhdr->addr1, 0, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, adapter_mac_addr(padapter), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	set_frame_sub_type(pframe, WIFI_ACTION);

	pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	pframe += pktlen;

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pktlen));

	/* dialog token, filled by FW */
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pktlen));

	_rtw_memset(wpsie, 0x00, 255);
	wpsielen = 0;

	/*	WPS Section */
	wpsielen = 0;
	/*	WPS OUI */
	*(u32 *)(wpsie) = cpu_to_be32(WPSOUI);
	wpsielen += 4;

	/*	WPS version */
	/*	Type: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_VER1);
	wpsielen += 2;

	/*	Length: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0001);
	wpsielen += 2;

	/*	Value: */
	wpsie[wpsielen++] = WPS_VERSION_1;	/*	Version 1.0 */

	/*	Device Password ID */
	/*	Type: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_DEVICE_PWID);
	wpsielen += 2;

	/*	Length: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0002);
	wpsielen += 2;

	/*	Value: */
	if (wps_devicepassword_id == WPS_DPID_USER_SPEC)
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_DPID_REGISTRAR_SPEC);
	else if (wps_devicepassword_id == WPS_DPID_REGISTRAR_SPEC)
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_DPID_USER_SPEC);
	else
		*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_DPID_PBC);
	wpsielen += 2;

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pktlen);


	/*	P2P IE Section. */

	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/*	Commented by Albert 20100908 */
	/*	According to the P2P Specification, the group negoitation response frame should contain 9 P2P attributes */
	/*	1. Status */
	/*	2. P2P Capability */
	/*	3. Group Owner Intent */
	/*	4. Configuration Timeout */
	/*	5. Operating Channel */
	/*	6. Intended P2P Interface Address */
	/*	7. Channel List */
	/*	8. Device Info */
	/*	9. Group ID	( Only GO ) */


	/*	ToDo: */

	/*	P2P Status */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_STATUS;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0001);
	p2pielen += 2;

	/*	Value, filled by FW */
	p2pie[p2pielen++] = 1;

	/*	P2P Capability */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CAPABILITY;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0002);
	p2pielen += 2;

	/*	Value: */
	/*	Device Capability Bitmap, 1 byte */

	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT)) {
		/*	Commented by Albert 2011/03/08 */
		/*	According to the P2P specification */
		/*	if the sending device will be client, the P2P Capability should be reserved of group negotation response frame */
		p2pie[p2pielen++] = 0;
	} else {
		/*	Be group owner or meet the error case */
		p2pie[p2pielen++] = DMP_P2P_DEVCAP_SUPPORT;
	}

	/*	Group Capability Bitmap, 1 byte */
	if (pwdinfo->persistent_supported)
		p2pie[p2pielen++] = P2P_GRPCAP_CROSS_CONN | P2P_GRPCAP_PERSISTENT_GROUP;
	else
		p2pie[p2pielen++] = P2P_GRPCAP_CROSS_CONN;

	/*	Group Owner Intent */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_GO_INTENT;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0001);
	p2pielen += 2;

	/*	Value: */
	if (pwdinfo->peer_intent & 0x01) {
		/*	Peer's tie breaker bit is 1, our tie breaker bit should be 0 */
		p2pie[p2pielen++] = (pwdinfo->intent << 1);
	} else {
		/*	Peer's tie breaker bit is 0, our tie breaker bit should be 1 */
		p2pie[p2pielen++] = ((pwdinfo->intent << 1) | BIT(0));
	}


	/*	Configuration Timeout */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CONF_TIMEOUT;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0002);
	p2pielen += 2;

	/*	Value: */
	p2pie[p2pielen++] = 200;	/*	2 seconds needed to be the P2P GO */
	p2pie[p2pielen++] = 200;	/*	2 seconds needed to be the P2P Client */

	/*	Operating Channel */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_OPERATING_CH;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0005);
	p2pielen += 2;

	/*	Value: */
	/*	Country String */
	p2pie[p2pielen++] = 'X';
	p2pie[p2pielen++] = 'X';

	/*	The third byte should be set to 0x04. */
	/*	Described in the "Operating Channel Attribute" section. */
	p2pie[p2pielen++] = 0x04;

	/*	Operating Class */
	if (pwdinfo->operating_channel <= 14) {
		/*	Operating Class */
		p2pie[p2pielen++] = 0x51;
	} else if ((pwdinfo->operating_channel >= 36) && (pwdinfo->operating_channel <= 48)) {
		/*	Operating Class */
		p2pie[p2pielen++] = 0x73;
	} else {
		/*	Operating Class */
		p2pie[p2pielen++] = 0x7c;
	}

	/*	Channel Number */
	p2pie[p2pielen++] = pwdinfo->operating_channel;	/*	operating channel number */

	/*	Intended P2P Interface Address	 */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_INTENDED_IF_ADDR;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(ETH_ALEN);
	p2pielen += 2;

	/*	Value: */
	_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
	p2pielen += ETH_ALEN;

	/*	Channel List */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CH_LIST;

	/* Country String(3) */
	/* + ( Operating Class (1) + Number of Channels(1) ) * Operation Classes (?) */
	/* + number of channels in all classes */
	len_channellist_attr = 3
		       + (1 + 1) * (u16)pmlmeext->channel_list.reg_classes
		       + get_reg_classes_full_count(pmlmeext->channel_list);

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_buddy_check_fwstate(padapter, _FW_LINKED))
		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(5 + 1);
	else
		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(len_channellist_attr);

#else

	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(len_channellist_attr);

#endif
	p2pielen += 2;

	/*	Value: */
	/*	Country String */
	p2pie[p2pielen++] = 'X';
	p2pie[p2pielen++] = 'X';

	/*	The third byte should be set to 0x04. */
	/*	Described in the "Operating Channel Attribute" section. */
	p2pie[p2pielen++] = 0x04;

	/*	Channel Entry List */

#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_check_status(padapter, MI_LINKED)) {
		u8 union_ch = rtw_mi_get_union_chan(padapter);

		/*	Operating Class */
		if (union_ch > 14) {
			if (union_ch >= 149)
				p2pie[p2pielen++] = 0x7c;
			else
				p2pie[p2pielen++] = 0x73;
		} else
			p2pie[p2pielen++] = 0x51;


		/*	Number of Channels */
		/*	Just support 1 channel and this channel is AP's channel */
		p2pie[p2pielen++] = 1;

		/*	Channel List */
		p2pie[p2pielen++] = union_ch;
	} else {
		int i, j;
		for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
			/*	Operating Class */
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].reg_class;

			/*	Number of Channels */
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channels;

			/*	Channel List */
			for (i = 0; i < pmlmeext->channel_list.reg_class[j].channels; i++)
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channel[i];
		}
	}
#else /* CONFIG_CONCURRENT_MODE */
	{
		int i, j;
		for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
			/*	Operating Class */
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].reg_class;

			/*	Number of Channels */
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channels;

			/*	Channel List */
			for (i = 0; i < pmlmeext->channel_list.reg_class[j].channels; i++)
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channel[i];
		}
	}
#endif /* CONFIG_CONCURRENT_MODE */


	/*	Device Info */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_DEVICE_INFO;

	/*	Length: */
	/*	21->P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes)  */
	/*	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes) */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(21 + pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	/*	P2P Device Address */
	_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
	p2pielen += ETH_ALEN;

	/*	Config Method */
	/*	This field should be big endian. Noted by P2P specification. */

	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(pwdinfo->supported_wps_cm);

	p2pielen += 2;

	/*	Primary Device Type */
	/*	Category ID */
	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(WPS_PDT_CID_MULIT_MEDIA);
	p2pielen += 2;

	/*	OUI */
	*(u32 *)(p2pie + p2pielen) = cpu_to_be32(WPSOUI);
	p2pielen += 4;

	/*	Sub Category ID */
	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(WPS_PDT_SCID_MEDIA_SERVER);
	p2pielen += 2;

	/*	Number of Secondary Device Types */
	p2pie[p2pielen++] = 0x00;	/*	No Secondary Device Type List */

	/*	Device Name */
	/*	Type: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(WPS_ATTR_DEVICE_NAME);
	p2pielen += 2;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_be16(pwdinfo->device_name_len);
	p2pielen += 2;

	/*	Value: */
	_rtw_memcpy(p2pie + p2pielen, pwdinfo->device_name , pwdinfo->device_name_len);
	p2pielen += pwdinfo->device_name_len;

	if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
		/*	Group ID Attribute */
		/*	Type: */
		p2pie[p2pielen++] = P2P_ATTR_GROUP_ID;

		/*	Length: */
		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(ETH_ALEN + pwdinfo->nego_ssidlen);
		p2pielen += 2;

		/*	Value: */
		/*	p2P Device Address */
		_rtw_memcpy(p2pie + p2pielen , pwdinfo->device_addr, ETH_ALEN);
		p2pielen += ETH_ALEN;

		/*	SSID */
		_rtw_memcpy(p2pie + p2pielen, pwdinfo->nego_ssid, pwdinfo->nego_ssidlen);
		p2pielen += pwdinfo->nego_ssidlen;

	}

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pktlen);

#ifdef CONFIG_WFD
	wfdielen = build_nego_resp_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pktlen += wfdielen;
#endif

	*pLength = pktlen;
#if 0
	/* printf dbg msg */
	dbgbufLen = pktlen;
	RTW_INFO("======> DBG MSG FOR CONSTRAUCT Nego Rsp\n");

	for (index = 0; index < dbgbufLen; index++)
		printk("%x ", *(dbgbuf + index));

	printk("\n");
	RTW_INFO("<====== DBG MSG FOR CONSTRAUCT Nego Rsp\n");
#endif
}

static void rtw_hal_construct_P2PInviteRsp(_adapter *padapter, u8 *pframe, u32 *pLength)
{
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_INVIT_RESP;
	u8			p2pie[255] = { 0x00 };
	u8			p2pielen = 0, i;
	u8			channel_cnt_24g = 0, channel_cnt_5gl = 0, channel_cnt_5gh = 0;
	u16			len_channellist_attr = 0;
	u32			pktlen;
	u8			dialogToken = 0;
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif

	/* struct xmit_frame			*pmgntframe; */
	/* struct pkt_attrib			*pattrib; */
	/* unsigned char					*pframe; */
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);

	/* for debug */
	u8 *dbgbuf = pframe;
	u8 dbgbufLen = 0, index = 0;


	RTW_INFO("%s\n", __FUNCTION__);
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	/* RA fill by FW */
	_rtw_memset(pwlanhdr->addr1, 0, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);

	/* BSSID fill by FW */
	_rtw_memset(pwlanhdr->addr3, 0, ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	set_frame_sub_type(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pktlen));

	/* dialog token, filled by FW */
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pktlen));

	/*	P2P IE Section. */

	/*	P2P OUI */
	p2pielen = 0;
	p2pie[p2pielen++] = 0x50;
	p2pie[p2pielen++] = 0x6F;
	p2pie[p2pielen++] = 0x9A;
	p2pie[p2pielen++] = 0x09;	/*	WFA P2P v1.0 */

	/*	Commented by Albert 20101005 */
	/*	According to the P2P Specification, the P2P Invitation response frame should contain 5 P2P attributes */
	/*	1. Status */
	/*	2. Configuration Timeout */
	/*	3. Operating Channel	( Only GO ) */
	/*	4. P2P Group BSSID	( Only GO ) */
	/*	5. Channel List */

	/*	P2P Status */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_STATUS;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0001);
	p2pielen += 2;

	/*	Value: filled by FW, defult value is FAIL INFO UNAVAILABLE */
	p2pie[p2pielen++] = P2P_STATUS_FAIL_INFO_UNAVAILABLE;

	/*	Configuration Timeout */
	/*	Type: */
	p2pie[p2pielen++] = P2P_ATTR_CONF_TIMEOUT;

	/*	Length: */
	*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0002);
	p2pielen += 2;

	/*	Value: */
	p2pie[p2pielen++] = 200;	/*	2 seconds needed to be the P2P GO */
	p2pie[p2pielen++] = 200;	/*	2 seconds needed to be the P2P Client */

	/* due to defult value is FAIL INFO UNAVAILABLE, so the following IE is not needed */
#if 0
	if (status_code == P2P_STATUS_SUCCESS) {
		if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
			/*	The P2P Invitation request frame asks this Wi-Fi device to be the P2P GO */
			/*	In this case, the P2P Invitation response frame should carry the two more P2P attributes. */
			/*	First one is operating channel attribute. */
			/*	Second one is P2P Group BSSID attribute. */

			/*	Operating Channel */
			/*	Type: */
			p2pie[p2pielen++] = P2P_ATTR_OPERATING_CH;

			/*	Length: */
			*(u16 *)(p2pie + p2pielen) = cpu_to_le16(0x0005);
			p2pielen += 2;

			/*	Value: */
			/*	Country String */
			p2pie[p2pielen++] = 'X';
			p2pie[p2pielen++] = 'X';

			/*	The third byte should be set to 0x04. */
			/*	Described in the "Operating Channel Attribute" section. */
			p2pie[p2pielen++] = 0x04;

			/*	Operating Class */
			p2pie[p2pielen++] = 0x51;	/*	Copy from SD7 */

			/*	Channel Number */
			p2pie[p2pielen++] = pwdinfo->operating_channel;	/*	operating channel number */


			/*	P2P Group BSSID */
			/*	Type: */
			p2pie[p2pielen++] = P2P_ATTR_GROUP_BSSID;

			/*	Length: */
			*(u16 *)(p2pie + p2pielen) = cpu_to_le16(ETH_ALEN);
			p2pielen += 2;

			/*	Value: */
			/*	P2P Device Address for GO */
			_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
			p2pielen += ETH_ALEN;

		}

		/*	Channel List */
		/*	Type: */
		p2pie[p2pielen++] = P2P_ATTR_CH_LIST;

		/*	Length: */
		/* Country String(3) */
		/* + ( Operating Class (1) + Number of Channels(1) ) * Operation Classes (?) */
		/* + number of channels in all classes */
		len_channellist_attr = 3
			+ (1 + 1) * (u16)pmlmeext->channel_list.reg_classes
			+ get_reg_classes_full_count(pmlmeext->channel_list);

#ifdef CONFIG_CONCURRENT_MODE
		if (rtw_mi_check_status(padapter, MI_LINKED))
			*(u16 *)(p2pie + p2pielen) = cpu_to_le16(5 + 1);
		else
			*(u16 *)(p2pie + p2pielen) = cpu_to_le16(len_channellist_attr);

#else

		*(u16 *)(p2pie + p2pielen) = cpu_to_le16(len_channellist_attr);

#endif
		p2pielen += 2;

		/*	Value: */
		/*	Country String */
		p2pie[p2pielen++] = 'X';
		p2pie[p2pielen++] = 'X';

		/*	The third byte should be set to 0x04. */
		/*	Described in the "Operating Channel Attribute" section. */
		p2pie[p2pielen++] = 0x04;

		/*	Channel Entry List */
#ifdef CONFIG_CONCURRENT_MODE
		if (rtw_mi_check_status(padapter, MI_LINKED)) {
			u8 union_ch = rtw_mi_get_union_chan(padapter);

			/*	Operating Class */
			if (union_ch > 14) {
				if (union_ch >= 149)
					p2pie[p2pielen++] = 0x7c;
				else
					p2pie[p2pielen++] = 0x73;

			} else
				p2pie[p2pielen++] = 0x51;


			/*	Number of Channels */
			/*	Just support 1 channel and this channel is AP's channel */
			p2pie[p2pielen++] = 1;

			/*	Channel List */
			p2pie[p2pielen++] = union_ch;
		} else {
			int i, j;
			for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
				/*	Operating Class */
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].reg_class;

				/*	Number of Channels */
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channels;

				/*	Channel List */
				for (i = 0; i < pmlmeext->channel_list.reg_class[j].channels; i++)
					p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channel[i];
			}
		}
#else /* CONFIG_CONCURRENT_MODE */
		{
			int i, j;
			for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
				/*	Operating Class */
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].reg_class;

				/*	Number of Channels */
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channels;

				/*	Channel List */
				for (i = 0; i < pmlmeext->channel_list.reg_class[j].channels; i++)
					p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channel[i];
			}
		}
#endif /* CONFIG_CONCURRENT_MODE */
	}
#endif

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pktlen);

#ifdef CONFIG_WFD
	wfdielen = build_invitation_resp_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pktlen += wfdielen;
#endif

	*pLength = pktlen;

#if 0
	/* printf dbg msg */
	dbgbufLen = pktlen;
	RTW_INFO("======> DBG MSG FOR CONSTRAUCT Invite Rsp\n");

	for (index = 0; index < dbgbufLen; index++)
		printk("%x ", *(dbgbuf + index));

	printk("\n");
	RTW_INFO("<====== DBG MSG FOR CONSTRAUCT Invite Rsp\n");
#endif
}


static void rtw_hal_construct_P2PProvisionDisRsp(_adapter *padapter, u8 *pframe, u32 *pLength)
{
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u8			dialogToken = 0;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_PROVISION_DISC_RESP;
	u8			wpsie[100] = { 0x00 };
	u8			wpsielen = 0;
	u32			pktlen;
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif

	/* struct xmit_frame			*pmgntframe; */
	/* struct pkt_attrib			*pattrib; */
	/* unsigned char					*pframe; */
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);

	/* for debug */
	u8 *dbgbuf = pframe;
	u8 dbgbufLen = 0, index = 0;

	RTW_INFO("%s\n", __FUNCTION__);

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	/* RA filled by FW */
	_rtw_memset(pwlanhdr->addr1, 0, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, adapter_mac_addr(padapter), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	set_frame_sub_type(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pktlen));
	/* dialog token, filled by FW */
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pktlen));

	wpsielen = 0;
	/*	WPS OUI */
	/* *(u32*) ( wpsie ) = cpu_to_be32( WPSOUI ); */
	RTW_PUT_BE32(wpsie, WPSOUI);
	wpsielen += 4;

#if 0
	/*	WPS version */
	/*	Type: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(WPS_ATTR_VER1);
	wpsielen += 2;

	/*	Length: */
	*(u16 *)(wpsie + wpsielen) = cpu_to_be16(0x0001);
	wpsielen += 2;

	/*	Value: */
	wpsie[wpsielen++] = WPS_VERSION_1;	/*	Version 1.0 */
#endif

	/*	Config Method */
	/*	Type: */
	/* *(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_CONF_METHOD ); */
	RTW_PUT_BE16(wpsie + wpsielen, WPS_ATTR_CONF_METHOD);
	wpsielen += 2;

	/*	Length: */
	/* *(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 ); */
	RTW_PUT_BE16(wpsie + wpsielen, 0x0002);
	wpsielen += 2;

	/*	Value: filled by FW, default value is PBC */
	/* *(u16*) ( wpsie + wpsielen ) = cpu_to_be16( config_method ); */
	RTW_PUT_BE16(wpsie + wpsielen, WPS_CM_PUSH_BUTTON);
	wpsielen += 2;

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pktlen);

#ifdef CONFIG_WFD
	wfdielen = build_provdisc_resp_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pktlen += wfdielen;
#endif

	*pLength = pktlen;

	/* printf dbg msg */
#if 0
	dbgbufLen = pktlen;
	RTW_INFO("======> DBG MSG FOR CONSTRAUCT  ProvisionDis Rsp\n");

	for (index = 0; index < dbgbufLen; index++)
		printk("%x ", *(dbgbuf + index));

	printk("\n");
	RTW_INFO("<====== DBG MSG FOR CONSTRAUCT ProvisionDis Rsp\n");
#endif
}

u8 rtw_hal_set_FwP2PRsvdPage_cmd(_adapter *adapter, PRSVDPAGE_LOC rsvdpageloc)
{
	u8 u1H2CP2PRsvdPageParm[H2C_P2PRSVDPAGE_LOC_LEN] = {0};
	struct hal_ops *pHalFunc = &adapter->hal_func;
	u8 ret = _FAIL;

	RTW_INFO("P2PRsvdPageLoc: P2PBeacon=%d P2PProbeRsp=%d NegoRsp=%d InviteRsp=%d PDRsp=%d\n",
		 rsvdpageloc->LocP2PBeacon, rsvdpageloc->LocP2PProbeRsp,
		 rsvdpageloc->LocNegoRsp, rsvdpageloc->LocInviteRsp,
		 rsvdpageloc->LocPDRsp);

	SET_H2CCMD_RSVDPAGE_LOC_P2P_BCN(u1H2CP2PRsvdPageParm, rsvdpageloc->LocProbeRsp);
	SET_H2CCMD_RSVDPAGE_LOC_P2P_PROBE_RSP(u1H2CP2PRsvdPageParm, rsvdpageloc->LocPsPoll);
	SET_H2CCMD_RSVDPAGE_LOC_P2P_NEGO_RSP(u1H2CP2PRsvdPageParm, rsvdpageloc->LocNullData);
	SET_H2CCMD_RSVDPAGE_LOC_P2P_INVITE_RSP(u1H2CP2PRsvdPageParm, rsvdpageloc->LocQosNull);
	SET_H2CCMD_RSVDPAGE_LOC_P2P_PD_RSP(u1H2CP2PRsvdPageParm, rsvdpageloc->LocBTQosNull);

	/* FillH2CCmd8723B(padapter, H2C_8723B_P2P_OFFLOAD_RSVD_PAGE, H2C_P2PRSVDPAGE_LOC_LEN, u1H2CP2PRsvdPageParm); */
	ret = rtw_hal_fill_h2c_cmd(adapter,
				   H2C_P2P_OFFLOAD_RSVD_PAGE,
				   H2C_P2PRSVDPAGE_LOC_LEN,
				   u1H2CP2PRsvdPageParm);

	return ret;
}

u8 rtw_hal_set_p2p_wowlan_offload_cmd(_adapter *adapter)
{

	u8 offload_cmd[H2C_P2P_OFFLOAD_LEN] = {0};
	struct wifidirect_info	*pwdinfo = &(adapter->wdinfo);
	struct P2P_WoWlan_Offload_t *p2p_wowlan_offload = (struct P2P_WoWlan_Offload_t *)offload_cmd;
	struct hal_ops *pHalFunc = &adapter->hal_func;
	u8 ret = _FAIL;

	_rtw_memset(p2p_wowlan_offload, 0 , sizeof(struct P2P_WoWlan_Offload_t));
	RTW_INFO("%s\n", __func__);
	switch (pwdinfo->role) {
	case P2P_ROLE_DEVICE:
		RTW_INFO("P2P_ROLE_DEVICE\n");
		p2p_wowlan_offload->role = 0;
		break;
	case P2P_ROLE_CLIENT:
		RTW_INFO("P2P_ROLE_CLIENT\n");
		p2p_wowlan_offload->role = 1;
		break;
	case P2P_ROLE_GO:
		RTW_INFO("P2P_ROLE_GO\n");
		p2p_wowlan_offload->role = 2;
		break;
	default:
		RTW_INFO("P2P_ROLE_DISABLE\n");
		break;
	}
	p2p_wowlan_offload->Wps_Config[0] = pwdinfo->supported_wps_cm >> 8;
	p2p_wowlan_offload->Wps_Config[1] = pwdinfo->supported_wps_cm;
	offload_cmd = (u8 *)p2p_wowlan_offload;
	RTW_INFO("p2p_wowlan_offload: %x:%x:%x\n", offload_cmd[0], offload_cmd[1], offload_cmd[2]);

	ret = rtw_hal_fill_h2c_cmd(adapter,
				   H2C_P2P_OFFLOAD,
				   H2C_P2P_OFFLOAD_LEN,
				   offload_cmd);
	return ret;

	/* FillH2CCmd8723B(adapter, H2C_8723B_P2P_OFFLOAD, sizeof(struct P2P_WoWlan_Offload_t), (u8 *)p2p_wowlan_offload); */
}
#endif /* CONFIG_P2P_WOWLAN */

static void rtw_hal_construct_beacon(_adapter *padapter,
				     u8 *pframe, u32 *pLength)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;
	u32					rate_len, pktlen;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*cur_network = &(pmlmeinfo->network);
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};


	/* RTW_INFO("%s\n", __FUNCTION__); */

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(cur_network), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0/*pmlmeext->mgnt_seq*/);
	/* pmlmeext->mgnt_seq++; */
	set_frame_sub_type(pframe, WIFI_BEACON);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	/* timestamp will be inserted by hardware */
	pframe += 8;
	pktlen += 8;

	/* beacon interval: 2 bytes */
	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pktlen += 2;

	/* capability info: 2 bytes */
	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pktlen += 2;

	if ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) {
		/* RTW_INFO("ie len=%d\n", cur_network->IELength); */
		pktlen += cur_network->IELength - sizeof(NDIS_802_11_FIXED_IEs);
		_rtw_memcpy(pframe, cur_network->IEs + sizeof(NDIS_802_11_FIXED_IEs), pktlen);

		goto _ConstructBeacon;
	}

	/* below for ad-hoc mode */

	/* SSID */
	pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pktlen);

	/* supported rates... */
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8) ? 8 : rate_len), cur_network->SupportedRates, &pktlen);

	/* DS parameter set */
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pktlen);

	if ((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) {
		u32 ATIMWindow;
		/* IBSS Parameter Set... */
		/* ATIMWindow = cur->Configuration.ATIMWindow; */
		ATIMWindow = 0;
		pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pktlen);
	}


	/* todo: ERP IE */


	/* EXTERNDED SUPPORTED RATE */
	if (rate_len > 8)
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pktlen);


	/* todo:HT for adhoc */

_ConstructBeacon:

	if ((pktlen + TXDESC_SIZE) > 512) {
		RTW_INFO("beacon frame too large\n");
		return;
	}

	*pLength = pktlen;

	/* RTW_INFO("%s bcn_sz=%d\n", __FUNCTION__, pktlen); */

}

static void rtw_hal_construct_PSPoll(_adapter *padapter,
				     u8 *pframe, u32 *pLength)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;
	u32					pktlen;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	/* RTW_INFO("%s\n", __FUNCTION__); */

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	/* Frame control. */
	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	SetPwrMgt(fctrl);
	set_frame_sub_type(pframe, WIFI_PSPOLL);

	/* AID. */
	set_duration(pframe, (pmlmeinfo->aid | 0xc000));

	/* BSSID. */
	_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	/* TA. */
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);

	*pLength = 16;
}

void rtw_hal_construct_NullFunctionData(
	PADAPTER padapter,
	u8		*pframe,
	u32		*pLength,
	u8		*StaAddr,
	u8		bQoS,
	u8		AC,
	u8		bEosp,
	u8		bForcePowerSave)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16						*fctrl;
	u32						pktlen;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wlan_network		*cur_network = &pmlmepriv->cur_network;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);


	/* RTW_INFO("%s:%d\n", __FUNCTION__, bForcePowerSave); */

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;
	if (bForcePowerSave)
		SetPwrMgt(fctrl);

	switch (cur_network->network.InfrastructureMode) {
	case Ndis802_11Infrastructure:
		SetToDs(fctrl);
		_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, StaAddr, ETH_ALEN);
		break;
	case Ndis802_11APMode:
		SetFrDs(fctrl);
		_rtw_memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr2, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, adapter_mac_addr(padapter), ETH_ALEN);
		break;
	case Ndis802_11IBSS:
	default:
		_rtw_memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
		break;
	}

	SetSeqNum(pwlanhdr, 0);

	if (bQoS == _TRUE) {
		struct rtw_ieee80211_hdr_3addr_qos *pwlanqoshdr;

		set_frame_sub_type(pframe, WIFI_QOS_DATA_NULL);

		pwlanqoshdr = (struct rtw_ieee80211_hdr_3addr_qos *)pframe;
		SetPriority(&pwlanqoshdr->qc, AC);
		SetEOSP(&pwlanqoshdr->qc, bEosp);

		pktlen = sizeof(struct rtw_ieee80211_hdr_3addr_qos);
	} else {
		set_frame_sub_type(pframe, WIFI_DATA_NULL);

		pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	}

	*pLength = pktlen;
}

void rtw_hal_construct_ProbeRsp(_adapter *padapter, u8 *pframe, u32 *pLength,
				u8 *StaAddr, BOOLEAN bHideSSID)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;
	u8					*mac, *bssid;
	u32					pktlen;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX  *cur_network = &(pmlmeinfo->network);

	/*RTW_INFO("%s\n", __FUNCTION__);*/

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	mac = adapter_mac_addr(padapter);
	bssid = cur_network->MacAddress;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	_rtw_memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, mac, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, bssid, ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	set_frame_sub_type(fctrl, WIFI_PROBERSP);

	pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	pframe += pktlen;

	if (cur_network->IELength > MAX_IE_SZ)
		return;

	_rtw_memcpy(pframe, cur_network->IEs, cur_network->IELength);
	pframe += cur_network->IELength;
	pktlen += cur_network->IELength;

	*pLength = pktlen;
}

#ifdef CONFIG_WOWLAN
static void rtw_hal_append_tkip_mic(PADAPTER padapter,
				    u8 *pframe, u32 offset)
{
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct rtw_ieee80211_hdr	*pwlanhdr;
	struct mic_data	micdata;
	struct sta_info	*psta = NULL;
	int res = 0;

	u8	*payload = (u8 *)(pframe + offset);

	u8	mic[8];
	u8	priority[4] = {0x0};
	u8	null_key[16] = {0x0};

	RTW_INFO("%s(): Add MIC, offset: %d\n", __func__, offset);

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	psta = rtw_get_stainfo(&padapter->stapriv,
			get_my_bssid(&(pmlmeinfo->network)));
	if (psta != NULL) {
		res = _rtw_memcmp(&psta->dot11tkiptxmickey.skey[0],
				  null_key, 16);
		if (res == _TRUE)
			RTW_INFO("%s(): STA dot11tkiptxmickey==0\n", __func__);
		rtw_secmicsetkey(&micdata, &psta->dot11tkiptxmickey.skey[0]);
	}

	rtw_secmicappend(&micdata, pwlanhdr->addr3, 6);  /* DA */

	rtw_secmicappend(&micdata, pwlanhdr->addr2, 6); /* SA */

	priority[0] = 0;

	rtw_secmicappend(&micdata, &priority[0], 4);

	rtw_secmicappend(&micdata, payload, 36); /* payload length = 8 + 28 */

	rtw_secgetmic(&micdata, &(mic[0]));

	payload += 36;

	_rtw_memcpy(payload, &(mic[0]), 8);
}
/*
 * Description:
 *	Construct the ARP response packet to support ARP offload.
 *   */
static void rtw_hal_construct_ARPRsp(
	PADAPTER padapter,
	u8			*pframe,
	u32			*pLength,
	u8			*pIPAddress
)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16	*fctrl;
	u32	pktlen;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct wlan_network	*cur_network = &pmlmepriv->cur_network;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	static u8	ARPLLCHeader[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x08, 0x06};
	u8	*pARPRspPkt = pframe;
	/* for TKIP Cal MIC */
	u8	*payload = pframe;
	u8	EncryptionHeadOverhead = 0, arp_offset = 0;
	/* RTW_INFO("%s:%d\n", __FUNCTION__, bForcePowerSave); */

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	/* ------------------------------------------------------------------------- */
	/* MAC Header. */
	/* ------------------------------------------------------------------------- */
	SetFrameType(fctrl, WIFI_DATA);
	/* set_frame_sub_type(fctrl, 0); */
	SetToDs(fctrl);
	_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	set_duration(pwlanhdr, 0);
	/* SET_80211_HDR_FRAME_CONTROL(pARPRspPkt, 0); */
	/* SET_80211_HDR_TYPE_AND_SUBTYPE(pARPRspPkt, Type_Data); */
	/* SET_80211_HDR_TO_DS(pARPRspPkt, 1); */
	/* SET_80211_HDR_ADDRESS1(pARPRspPkt, pMgntInfo->Bssid); */
	/* SET_80211_HDR_ADDRESS2(pARPRspPkt, Adapter->CurrentAddress); */
	/* SET_80211_HDR_ADDRESS3(pARPRspPkt, pMgntInfo->Bssid); */

	/* SET_80211_HDR_DURATION(pARPRspPkt, 0); */
	/* SET_80211_HDR_FRAGMENT_SEQUENCE(pARPRspPkt, 0); */
#ifdef CONFIG_WAPI_SUPPORT
	*pLength = sMacHdrLng;
#else
	*pLength = 24;
#endif
	switch (psecuritypriv->dot11PrivacyAlgrthm) {
	case _WEP40_:
	case _WEP104_:
		EncryptionHeadOverhead = 4;
		break;
	case _TKIP_:
		EncryptionHeadOverhead = 8;
		break;
	case _AES_:
		EncryptionHeadOverhead = 8;
		break;
#ifdef CONFIG_WAPI_SUPPORT
	case _SMS4_:
		EncryptionHeadOverhead = 18;
		break;
#endif
	default:
		EncryptionHeadOverhead = 0;
	}

	if (EncryptionHeadOverhead > 0) {
		_rtw_memset(&(pframe[*pLength]), 0, EncryptionHeadOverhead);
		*pLength += EncryptionHeadOverhead;
		/* SET_80211_HDR_WEP(pARPRspPkt, 1);  */ /* Suggested by CCW. */
		SetPrivacy(fctrl);
	}

	/* ------------------------------------------------------------------------- */
	/* Frame Body. */
	/* ------------------------------------------------------------------------- */
	arp_offset = *pLength;
	pARPRspPkt = (u8 *)(pframe + arp_offset);
	payload = pARPRspPkt; /* Get Payload pointer */
	/* LLC header */
	_rtw_memcpy(pARPRspPkt, ARPLLCHeader, 8);
	*pLength += 8;

	/* ARP element */
	pARPRspPkt += 8;
	SET_ARP_PKT_HW(pARPRspPkt, 0x0100);
	SET_ARP_PKT_PROTOCOL(pARPRspPkt, 0x0008);	/* IP protocol */
	SET_ARP_PKT_HW_ADDR_LEN(pARPRspPkt, 6);
	SET_ARP_PKT_PROTOCOL_ADDR_LEN(pARPRspPkt, 4);
	SET_ARP_PKT_OPERATION(pARPRspPkt, 0x0200);	/* ARP response */
	SET_ARP_PKT_SENDER_MAC_ADDR(pARPRspPkt, adapter_mac_addr(padapter));
	SET_ARP_PKT_SENDER_IP_ADDR(pARPRspPkt, pIPAddress);
#ifdef CONFIG_ARP_KEEP_ALIVE
	if (!is_zero_mac_addr(pmlmepriv->gw_mac_addr)) {
		SET_ARP_PKT_TARGET_MAC_ADDR(pARPRspPkt, pmlmepriv->gw_mac_addr);
		SET_ARP_PKT_TARGET_IP_ADDR(pARPRspPkt, pmlmepriv->gw_ip);
	} else
#endif
	{
		SET_ARP_PKT_TARGET_MAC_ADDR(pARPRspPkt,
				    get_my_bssid(&(pmlmeinfo->network)));
		SET_ARP_PKT_TARGET_IP_ADDR(pARPRspPkt,
					   pIPAddress);
		RTW_INFO("%s Target Mac Addr:" MAC_FMT "\n", __FUNCTION__,
			 MAC_ARG(get_my_bssid(&(pmlmeinfo->network))));
		RTW_INFO("%s Target IP Addr" IP_FMT "\n", __FUNCTION__,
			 IP_ARG(pIPAddress));
	}

	*pLength += 28;

	if (psecuritypriv->dot11PrivacyAlgrthm == _TKIP_) {
		if (IS_HARDWARE_TYPE_8188E(padapter) ||
		    IS_HARDWARE_TYPE_8812(padapter)) {
			rtw_hal_append_tkip_mic(padapter, pframe, arp_offset);
		}
		*pLength += 8;
	}
}

#ifdef CONFIG_PNO_SUPPORT
static void rtw_hal_construct_ProbeReq(_adapter *padapter, u8 *pframe,
				       u32 *pLength, pno_ssid_t *ssid)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16				*fctrl;
	u32				pktlen;
	unsigned char			*mac;
	unsigned char			bssrate[NumRates];
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	int	bssrate_len = 0;
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;
	mac = adapter_mac_addr(padapter);

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, bc_addr, ETH_ALEN);

	_rtw_memcpy(pwlanhdr->addr2, mac, ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	set_frame_sub_type(pframe, WIFI_PROBEREQ);

	pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	pframe += pktlen;

	if (ssid == NULL)
		pframe = rtw_set_ie(pframe, _SSID_IE_, 0, NULL, &pktlen);
	else {
		/* RTW_INFO("%s len:%d\n", ssid->SSID, ssid->SSID_len); */
		pframe = rtw_set_ie(pframe, _SSID_IE_, ssid->SSID_len, ssid->SSID, &pktlen);
	}

	get_rate_set(padapter, bssrate, &bssrate_len);

	if (bssrate_len > 8) {
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , 8, bssrate, &pktlen);
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_ , (bssrate_len - 8), (bssrate + 8), &pktlen);
	} else
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , bssrate_len , bssrate, &pktlen);

	*pLength = pktlen;
}

static void rtw_hal_construct_PNO_info(_adapter *padapter,
				       u8 *pframe, u32 *pLength)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	int i;

	u8	*pPnoInfoPkt = pframe;
	pPnoInfoPkt = (u8 *)(pframe + *pLength);
	_rtw_memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->ssid_num, 1);

	pPnoInfoPkt += 1;
	_rtw_memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->hidden_ssid_num, 1);

	pPnoInfoPkt += 3;
	_rtw_memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->fast_scan_period, 1);

	pPnoInfoPkt += 4;
	_rtw_memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->fast_scan_iterations, 4);

	pPnoInfoPkt += 4;
	_rtw_memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->slow_scan_period, 4);

	pPnoInfoPkt += 4;
	_rtw_memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->ssid_length, MAX_PNO_LIST_COUNT);

	pPnoInfoPkt += MAX_PNO_LIST_COUNT;
	_rtw_memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->ssid_cipher_info, MAX_PNO_LIST_COUNT);

	pPnoInfoPkt += MAX_PNO_LIST_COUNT;
	_rtw_memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->ssid_channel_info, MAX_PNO_LIST_COUNT);

	pPnoInfoPkt += MAX_PNO_LIST_COUNT;
	_rtw_memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->loc_probe_req, MAX_HIDDEN_AP);

	pPnoInfoPkt += MAX_HIDDEN_AP;

	/*
	SSID is located at 128th Byte in NLO info Page
	*/

	*pLength += 128;
	pPnoInfoPkt = pframe + 128;

	for (i = 0; i < pwrctl->pnlo_info->ssid_num ; i++) {
		_rtw_memcpy(pPnoInfoPkt, &pwrctl->pno_ssid_list->node[i].SSID,
			    pwrctl->pnlo_info->ssid_length[i]);
		*pLength += WLAN_SSID_MAXLEN;
		pPnoInfoPkt += WLAN_SSID_MAXLEN;
	}
}

static void rtw_hal_construct_ssid_list(_adapter *padapter,
					u8 *pframe, u32 *pLength)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	u8 *pSSIDListPkt = pframe;
	int i;

	pSSIDListPkt = (u8 *)(pframe + *pLength);

	for (i = 0; i < pwrctl->pnlo_info->ssid_num ; i++) {
		_rtw_memcpy(pSSIDListPkt, &pwrctl->pno_ssid_list->node[i].SSID,
			    pwrctl->pnlo_info->ssid_length[i]);

		*pLength += WLAN_SSID_MAXLEN;
		pSSIDListPkt += WLAN_SSID_MAXLEN;
	}
}

static void rtw_hal_construct_scan_info(_adapter *padapter,
					u8 *pframe, u32 *pLength)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	u8 *pScanInfoPkt = pframe;
	int i;

	pScanInfoPkt = (u8 *)(pframe + *pLength);

	_rtw_memcpy(pScanInfoPkt, &pwrctl->pscan_info->channel_num, 1);

	*pLength += 1;
	pScanInfoPkt += 1;
	_rtw_memcpy(pScanInfoPkt, &pwrctl->pscan_info->orig_ch, 1);


	*pLength += 1;
	pScanInfoPkt += 1;
	_rtw_memcpy(pScanInfoPkt, &pwrctl->pscan_info->orig_bw, 1);


	*pLength += 1;
	pScanInfoPkt += 1;
	_rtw_memcpy(pScanInfoPkt, &pwrctl->pscan_info->orig_40_offset, 1);

	*pLength += 1;
	pScanInfoPkt += 1;
	_rtw_memcpy(pScanInfoPkt, &pwrctl->pscan_info->orig_80_offset, 1);

	*pLength += 1;
	pScanInfoPkt += 1;
	_rtw_memcpy(pScanInfoPkt, &pwrctl->pscan_info->periodScan, 1);

	*pLength += 1;
	pScanInfoPkt += 1;
	_rtw_memcpy(pScanInfoPkt, &pwrctl->pscan_info->period_scan_time, 1);

	*pLength += 1;
	pScanInfoPkt += 1;
	_rtw_memcpy(pScanInfoPkt, &pwrctl->pscan_info->enableRFE, 1);

	*pLength += 1;
	pScanInfoPkt += 1;
	_rtw_memcpy(pScanInfoPkt, &pwrctl->pscan_info->rfe_type, 8);

	*pLength += 8;
	pScanInfoPkt += 8;

	for (i = 0 ; i < MAX_SCAN_LIST_COUNT ; i++) {
		_rtw_memcpy(pScanInfoPkt,
			    &pwrctl->pscan_info->ssid_channel_info[i], 4);
		*pLength += 4;
		pScanInfoPkt += 4;
	}
}
#endif /* CONFIG_PNO_SUPPORT */

#ifdef CONFIG_GTK_OL
static void rtw_hal_construct_GTKRsp(
	PADAPTER	padapter,
	u8		*pframe,
	u32		*pLength
)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16	*fctrl;
	u32	pktlen;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct wlan_network	*cur_network = &pmlmepriv->cur_network;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	static u8	LLCHeader[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
	static u8	GTKbody_a[11] = {0x01, 0x03, 0x00, 0x5F, 0x02, 0x03, 0x12, 0x00, 0x10, 0x42, 0x0B};
	u8	*pGTKRspPkt = pframe;
	u8	EncryptionHeadOverhead = 0;
	/* RTW_INFO("%s:%d\n", __FUNCTION__, bForcePowerSave); */

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	/* ------------------------------------------------------------------------- */
	/* MAC Header. */
	/* ------------------------------------------------------------------------- */
	SetFrameType(fctrl, WIFI_DATA);
	/* set_frame_sub_type(fctrl, 0); */
	SetToDs(fctrl);

	_rtw_memcpy(pwlanhdr->addr1,
		    get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	_rtw_memcpy(pwlanhdr->addr2,
		    adapter_mac_addr(padapter), ETH_ALEN);

	_rtw_memcpy(pwlanhdr->addr3,
		    get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	set_duration(pwlanhdr, 0);

#ifdef CONFIG_WAPI_SUPPORT
	*pLength = sMacHdrLng;
#else
	*pLength = 24;
#endif /* CONFIG_WAPI_SUPPORT */

	/* ------------------------------------------------------------------------- */
	/* Security Header: leave space for it if necessary. */
	/* ------------------------------------------------------------------------- */
	switch (psecuritypriv->dot11PrivacyAlgrthm) {
	case _WEP40_:
	case _WEP104_:
		EncryptionHeadOverhead = 4;
		break;
	case _TKIP_:
		EncryptionHeadOverhead = 8;
		break;
	case _AES_:
		EncryptionHeadOverhead = 8;
		break;
#ifdef CONFIG_WAPI_SUPPORT
	case _SMS4_:
		EncryptionHeadOverhead = 18;
		break;
#endif /* CONFIG_WAPI_SUPPORT */
	default:
		EncryptionHeadOverhead = 0;
	}

	if (EncryptionHeadOverhead > 0) {
		_rtw_memset(&(pframe[*pLength]), 0, EncryptionHeadOverhead);
		*pLength += EncryptionHeadOverhead;
		/* SET_80211_HDR_WEP(pGTKRspPkt, 1);  */ /* Suggested by CCW. */
		/* GTK's privacy bit is done by FW */
		/* SetPrivacy(fctrl); */
	}
	/* ------------------------------------------------------------------------- */
	/* Frame Body. */
	/* ------------------------------------------------------------------------- */
	pGTKRspPkt = (u8 *)(pframe + *pLength);
	/* LLC header */
	_rtw_memcpy(pGTKRspPkt, LLCHeader, 8);
	*pLength += 8;

	/* GTK element */
	pGTKRspPkt += 8;

	/* GTK frame body after LLC, part 1 */
	/* TKIP key_length = 32, AES key_length = 16 */
	if (psecuritypriv->dot118021XGrpPrivacy == _TKIP_)
		GTKbody_a[8] = 0x20;

	/* GTK frame body after LLC, part 1 */
	_rtw_memcpy(pGTKRspPkt, GTKbody_a, 11);
	*pLength += 11;
	pGTKRspPkt += 11;
	/* GTK frame body after LLC, part 2 */
	_rtw_memset(&(pframe[*pLength]), 0, 88);
	*pLength += 88;
	pGTKRspPkt += 88;

	if (psecuritypriv->dot118021XGrpPrivacy == _TKIP_)
		*pLength += 8;
}
#endif /* CONFIG_GTK_OL */

void rtw_hal_set_wow_fw_rsvd_page(_adapter *adapter, u8 *pframe, u16 index,
		  u8 tx_desc, u32 page_size, u8 *page_num, u32 *total_pkt_len,
				  RSVDPAGE_LOC *rsvd_page_loc)
{
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	struct mlme_ext_priv	*pmlmeext;
	struct mlme_ext_info	*pmlmeinfo;
	u32	ARPLength = 0, GTKLength = 0, PNOLength = 0, ScanInfoLength = 0;
	u32	SSIDLegnth = 0, ProbeReqLength = 0;
	u8 CurtPktPageNum = 0;
	u8 currentip[4];
	u8 cur_dot11txpn[8];

#ifdef CONFIG_GTK_OL
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct sta_info *psta;
	struct security_priv *psecpriv = &adapter->securitypriv;
	u8 kek[RTW_KEK_LEN];
	u8 kck[RTW_KCK_LEN];
#endif /* CONFIG_GTK_OL */
#ifdef CONFIG_PNO_SUPPORT
	int pno_index;
	u8 ssid_num;
#endif /* CONFIG_PNO_SUPPORT */

	pmlmeext = &adapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	if (pwrctl->wowlan_pno_enable == _FALSE) {
		/* ARP RSP * 1 page */
		rtw_get_current_ip_address(adapter, currentip);

		rsvd_page_loc->LocArpRsp = *page_num;

		RTW_INFO("LocArpRsp: %d\n", rsvd_page_loc->LocArpRsp);

		rtw_hal_construct_ARPRsp(adapter, &pframe[index],
					 &ARPLength, currentip);

		rtw_hal_fill_fake_txdesc(adapter,
					 &pframe[index - tx_desc],
					 ARPLength, _FALSE, _FALSE, _TRUE);

		CurtPktPageNum = (u8)PageNum(tx_desc + ARPLength, page_size);

		*page_num += CurtPktPageNum;

		index += (CurtPktPageNum * page_size);

		/* 3 SEC IV * 1 page */
		rtw_get_sec_iv(adapter, cur_dot11txpn,
			       get_my_bssid(&pmlmeinfo->network));

		rsvd_page_loc->LocRemoteCtrlInfo = *page_num;

		RTW_INFO("LocRemoteCtrlInfo: %d\n", rsvd_page_loc->LocRemoteCtrlInfo);

		_rtw_memcpy(pframe + index - tx_desc, cur_dot11txpn, _AES_IV_LEN_);

		CurtPktPageNum = (u8)PageNum(_AES_IV_LEN_, page_size);

		*page_num += CurtPktPageNum;

		*total_pkt_len = index + _AES_IV_LEN_;
#ifdef CONFIG_GTK_OL
		index += (CurtPktPageNum * page_size);

		/* if the ap staion info. exists, get the kek, kck from staion info. */
		psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
		if (psta == NULL) {
			_rtw_memset(kek, 0, RTW_KEK_LEN);
			_rtw_memset(kck, 0, RTW_KCK_LEN);
			RTW_INFO("%s, KEK, KCK download rsvd page all zero\n",
				 __func__);
		} else {
			_rtw_memcpy(kek, psta->kek, RTW_KEK_LEN);
			_rtw_memcpy(kck, psta->kck, RTW_KCK_LEN);
		}

		/* 3 KEK, KCK */
		rsvd_page_loc->LocGTKInfo = *page_num;
		RTW_INFO("LocGTKInfo: %d\n", rsvd_page_loc->LocGTKInfo);

		if (IS_HARDWARE_TYPE_8188E(adapter) || IS_HARDWARE_TYPE_8812(adapter)) {
			struct security_priv *psecpriv = NULL;

			psecpriv = &adapter->securitypriv;
			_rtw_memcpy(pframe + index - tx_desc,
				    &psecpriv->dot11PrivacyAlgrthm, 1);
			_rtw_memcpy(pframe + index - tx_desc + 1,
				    &psecpriv->dot118021XGrpPrivacy, 1);
			_rtw_memcpy(pframe + index - tx_desc + 2,
				    kck, RTW_KCK_LEN);
			_rtw_memcpy(pframe + index - tx_desc + 2 + RTW_KCK_LEN,
				    kek, RTW_KEK_LEN);
			CurtPktPageNum = (u8)PageNum(tx_desc + 2 + RTW_KCK_LEN + RTW_KEK_LEN, page_size);
		} else {

			_rtw_memcpy(pframe + index - tx_desc, kck, RTW_KCK_LEN);
			_rtw_memcpy(pframe + index - tx_desc + RTW_KCK_LEN,
				    kek, RTW_KEK_LEN);
			GTKLength = tx_desc + RTW_KCK_LEN + RTW_KEK_LEN;

			if (psta != NULL &&
				psecuritypriv->dot118021XGrpPrivacy == _TKIP_) {
				_rtw_memcpy(pframe + index - tx_desc + 56,
					&psta->dot11tkiptxmickey, RTW_TKIP_MIC_LEN);
				GTKLength += RTW_TKIP_MIC_LEN;
			}
			CurtPktPageNum = (u8)PageNum(GTKLength, page_size);
		}
#if 0
		{
			int i;
			printk("\ntoFW KCK: ");
			for (i = 0; i < 16; i++)
				printk(" %02x ", kck[i]);
			printk("\ntoFW KEK: ");
			for (i = 0; i < 16; i++)
				printk(" %02x ", kek[i]);
			printk("\n");
		}

		RTW_INFO("%s(): HW_VAR_SET_TX_CMD: KEK KCK %p %d\n",
			 __FUNCTION__, &pframe[index - tx_desc],
			 (tx_desc + RTW_KCK_LEN + RTW_KEK_LEN));
#endif

		*page_num += CurtPktPageNum;

		index += (CurtPktPageNum * page_size);

		/* 3 GTK Response */
		rsvd_page_loc->LocGTKRsp = *page_num;
		RTW_INFO("LocGTKRsp: %d\n", rsvd_page_loc->LocGTKRsp);
		rtw_hal_construct_GTKRsp(adapter, &pframe[index], &GTKLength);

		rtw_hal_fill_fake_txdesc(adapter, &pframe[index - tx_desc],
					 GTKLength, _FALSE, _FALSE, _TRUE);
#if 0
		{
			int gj;
			printk("123GTK pkt=>\n");
			for (gj = 0; gj < GTKLength + tx_desc; gj++) {
				printk(" %02x ", pframe[index - tx_desc + gj]);
				if ((gj + 1) % 16 == 0)
					printk("\n");
			}
			printk(" <=end\n");
		}

		RTW_INFO("%s(): HW_VAR_SET_TX_CMD: GTK RSP %p %d\n",
			 __FUNCTION__, &pframe[index - tx_desc],
			 (tx_desc + GTKLength));
#endif

		CurtPktPageNum = (u8)PageNum(tx_desc + GTKLength, page_size);

		*page_num += CurtPktPageNum;

		index += (CurtPktPageNum * page_size);

		/* below page is empty for GTK extension memory */
		/* 3(11) GTK EXT MEM */
		rsvd_page_loc->LocGTKEXTMEM = *page_num;
		RTW_INFO("LocGTKEXTMEM: %d\n", rsvd_page_loc->LocGTKEXTMEM);
		CurtPktPageNum = 2;

		if (page_size >= 256)
			CurtPktPageNum = 1;

		*page_num += CurtPktPageNum;
		/* extension memory for FW */
		*total_pkt_len = index + (page_size * CurtPktPageNum);
#endif /* CONFIG_GTK_OL */

		index += (CurtPktPageNum * page_size);

		/*Reserve 1 page for AOAC report*/
		rsvd_page_loc->LocAOACReport = *page_num;
		RTW_INFO("LocAOACReport: %d\n", rsvd_page_loc->LocAOACReport);
		*page_num += 1;
		*total_pkt_len = index + (page_size * 1);
	} else {
#ifdef CONFIG_PNO_SUPPORT
		if (pwrctl->wowlan_in_resume == _FALSE &&
		    pwrctl->pno_inited == _TRUE) {

			/* Broadcast Probe Request */
			rsvd_page_loc->LocProbePacket = *page_num;

			RTW_INFO("loc_probe_req: %d\n",
				 rsvd_page_loc->LocProbePacket);

			rtw_hal_construct_ProbeReq(
				adapter,
				&pframe[index],
				&ProbeReqLength,
				NULL);

			rtw_hal_fill_fake_txdesc(adapter,
						 &pframe[index - tx_desc],
				 ProbeReqLength, _FALSE, _FALSE, _FALSE);

			CurtPktPageNum =
				(u8)PageNum(tx_desc + ProbeReqLength, page_size);

			*page_num += CurtPktPageNum;

			index += (CurtPktPageNum * page_size);

			/* Hidden SSID Probe Request */
			ssid_num = pwrctl->pnlo_info->hidden_ssid_num;

			for (pno_index = 0 ; pno_index < ssid_num ; pno_index++) {
				pwrctl->pnlo_info->loc_probe_req[pno_index] =
					*page_num;

				rtw_hal_construct_ProbeReq(
					adapter,
					&pframe[index],
					&ProbeReqLength,
					&pwrctl->pno_ssid_list->node[pno_index]);

				rtw_hal_fill_fake_txdesc(adapter,
						 &pframe[index - tx_desc],
					ProbeReqLength, _FALSE, _FALSE, _FALSE);

				CurtPktPageNum =
					(u8)PageNum(tx_desc + ProbeReqLength, page_size);

				*page_num += CurtPktPageNum;

				index += (CurtPktPageNum * page_size);
			}

			/* PNO INFO Page */
			rsvd_page_loc->LocPNOInfo = *page_num;
			RTW_INFO("LocPNOInfo: %d\n", rsvd_page_loc->LocPNOInfo);
			rtw_hal_construct_PNO_info(adapter,
						   &pframe[index - tx_desc],
						   &PNOLength);

			CurtPktPageNum = (u8)PageNum(PNOLength, page_size);
			*page_num += CurtPktPageNum;
			index += (CurtPktPageNum * page_size);

			/* Scan Info Page */
			rsvd_page_loc->LocScanInfo = *page_num;
			RTW_INFO("LocScanInfo: %d\n", rsvd_page_loc->LocScanInfo);
			rtw_hal_construct_scan_info(adapter,
						    &pframe[index - tx_desc],
						    &ScanInfoLength);

			CurtPktPageNum = (u8)PageNum(ScanInfoLength, page_size);
			*page_num += CurtPktPageNum;
			*total_pkt_len = index + ScanInfoLength;
			index += (CurtPktPageNum * page_size);
		}
#endif /* CONFIG_PNO_SUPPORT */
	}
}

static void rtw_hal_gate_bb(_adapter *adapter, bool stop)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	u8 val8 = 0;
	u16 val16 = 0;

	if (stop) {
		/* Pause TX*/
		pwrpriv->wowlan_txpause_status = rtw_read8(adapter, REG_TXPAUSE);
		rtw_write8(adapter, REG_TXPAUSE, 0xff);
		val8 = rtw_read8(adapter, REG_SYS_FUNC_EN);
		val8 &= ~BIT(0);
		rtw_write8(adapter, REG_SYS_FUNC_EN, val8);
		RTW_INFO("%s: BB gated: 0x%02x, store TXPAUSE: %02x\n",
			 __func__,
			 rtw_read8(adapter, REG_SYS_FUNC_EN),
			 pwrpriv->wowlan_txpause_status);
	} else {
		val8 = rtw_read8(adapter, REG_SYS_FUNC_EN);
		val8 |= BIT(0);
		rtw_write8(adapter, REG_SYS_FUNC_EN, val8);
		RTW_INFO("%s: BB release: 0x%02x, recover TXPAUSE:%02x\n",
			 __func__, rtw_read8(adapter, REG_SYS_FUNC_EN),
			 pwrpriv->wowlan_txpause_status);
		/* release TX*/
		rtw_write8(adapter, REG_TXPAUSE, pwrpriv->wowlan_txpause_status);
	}
}

static void rtw_hal_reset_mac_rx(_adapter *adapter)
{
	u8 val8 = 0;
	/* Set REG_CR bit1, bit3, bit7 to 0*/
	val8 = rtw_read8(adapter, REG_CR);
	val8 &= 0x75;
	rtw_write8(adapter, REG_CR, val8);
	val8 = rtw_read8(adapter, REG_CR);
	/* Set REG_CR bit1, bit3, bit7 to 1*/
	val8 |= 0x8a;
	rtw_write8(adapter, REG_CR, val8);
	RTW_INFO("0x%04x: %02x\n", REG_CR, rtw_read8(adapter, REG_CR));
}

static u8 rtw_hal_wow_pattern_generate(_adapter *adapter, u8 idx, struct rtl_wow_pattern *pwow_pattern)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	 u8 *pattern;
	 u8 len = 0;
	 u8 *mask;

	u8 mask_hw[MAX_WKFM_SIZE] = {0};
	u8 content[MAX_WKFM_PATTERN_SIZE] = {0};
	u8 broadcast_addr[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u8 multicast_addr1[2] = {0x33, 0x33};
	u8 multicast_addr2[3] = {0x01, 0x00, 0x5e};
	u8  mask_len = 0;
	u8 mac_addr[ETH_ALEN] = {0};
	u16 count = 0;
	int i, j;

	if (pwrctl->wowlan_pattern_idx > MAX_WKFM_CAM_NUM) {
		RTW_INFO("%s pattern_idx is more than MAX_FMC_NUM: %d\n",
			 __func__, MAX_WKFM_CAM_NUM);
		return _FAIL;
	}

	pattern = pwrctl->patterns[idx].content;
	len = pwrctl->patterns[idx].len;
	mask = pwrctl->patterns[idx].mask;

	_rtw_memcpy(mac_addr, adapter_mac_addr(adapter), ETH_ALEN);
	_rtw_memset(pwow_pattern, 0, sizeof(struct rtl_wow_pattern));

	mask_len = DIV_ROUND_UP(len, 8);

	/* 1. setup A1 table */
	if (memcmp(pattern, broadcast_addr, ETH_ALEN) == 0)
		pwow_pattern->type = PATTERN_BROADCAST;
	else if (memcmp(pattern, multicast_addr1, 2) == 0)
		pwow_pattern->type = PATTERN_MULTICAST;
	else if (memcmp(pattern, multicast_addr2, 3) == 0)
		pwow_pattern->type = PATTERN_MULTICAST;
	else if (memcmp(pattern, mac_addr, ETH_ALEN) == 0)
		pwow_pattern->type = PATTERN_UNICAST;
	else
		pwow_pattern->type = PATTERN_INVALID;

	/* translate mask from os to mask for hw */

	/******************************************************************************
	 * pattern from OS uses 'ethenet frame', like this:

		|    6   |    6   |   2  |     20    |  Variable  |  4  |
		|--------+--------+------+-----------+------------+-----|
		|    802.3 Mac Header    | IP Header | TCP Packet | FCS |
		|   DA   |   SA   | Type |

	 * BUT, packet catched by our HW is in '802.11 frame', begin from LLC,

		|     24 or 30      |    6   |   2  |     20    |  Variable  |  4  |
		|-------------------+--------+------+-----------+------------+-----|
		| 802.11 MAC Header |       LLC     | IP Header | TCP Packet | FCS |
				    | Others | Tpye |

	 * Therefore, we need translate mask_from_OS to mask_to_hw.
	 * We should left-shift mask by 6 bits, then set the new bit[0~5] = 0,
	 * because new mask[0~5] means 'SA', but our HW packet begins from LLC,
	 * bit[0~5] corresponds to first 6 Bytes in LLC, they just don't match.
	 ******************************************************************************/
	/* Shift 6 bits */
	for (i = 0; i < mask_len - 1; i++) {
		mask_hw[i] = mask[i] >> 6;
		mask_hw[i] |= (mask[i + 1] & 0x3F) << 2;
	}

	mask_hw[i] = (mask[i] >> 6) & 0x3F;
	/* Set bit 0-5 to zero */
	mask_hw[0] &= 0xC0;

	for (i = 0; i < (MAX_WKFM_SIZE / 4); i++) {
		pwow_pattern->mask[i] = mask_hw[i * 4];
		pwow_pattern->mask[i] |= (mask_hw[i * 4 + 1] << 8);
		pwow_pattern->mask[i] |= (mask_hw[i * 4 + 2] << 16);
		pwow_pattern->mask[i] |= (mask_hw[i * 4 + 3] << 24);
	}

	/* To get the wake up pattern from the mask.
	 * We do not count first 12 bits which means
	 * DA[6] and SA[6] in the pattern to match HW design. */
	count = 0;
	for (i = 12; i < len; i++) {
		if ((mask[i / 8] >> (i % 8)) & 0x01) {
			content[count] = pattern[i];
			count++;
		}
	}

	pwow_pattern->crc = rtw_calc_crc(content, count);

	if (pwow_pattern->crc != 0) {
		if (pwow_pattern->type == PATTERN_INVALID)
			pwow_pattern->type = PATTERN_VALID;
	}

	return _SUCCESS;
}

#ifndef CONFIG_WOW_PATTERN_HW_CAM
static void rtw_hal_set_wow_rxff_boundary(_adapter *adapter, bool wow_mode)
{
	u8 val8 = 0;
	u16 rxff_bndy = 0;
	u32 rx_dma_buff_sz = 0;

	val8 = rtw_read8(adapter, REG_FIFOPAGE + 3);
	if (val8 != 0)
		RTW_INFO("%s:[%04x]some PKTs in TXPKTBUF\n",
			 __func__, (REG_FIFOPAGE + 3));

	rtw_hal_reset_mac_rx(adapter);

	if (wow_mode) {
		rtw_hal_get_def_var(adapter, HAL_DEF_RX_DMA_SZ_WOW,
				    (u8 *)&rx_dma_buff_sz);
		rxff_bndy = rx_dma_buff_sz - 1;

		rtw_write16(adapter, (REG_TRXFF_BNDY + 2), rxff_bndy);
		RTW_INFO("%s: wow mode, 0x%04x: 0x%04x\n", __func__,
			 REG_TRXFF_BNDY + 2,
			 rtw_read16(adapter, (REG_TRXFF_BNDY + 2)));
	} else {
		rtw_hal_get_def_var(adapter, HAL_DEF_RX_DMA_SZ,
				    (u8 *)&rx_dma_buff_sz);
		rxff_bndy = rx_dma_buff_sz - 1;
		rtw_write16(adapter, (REG_TRXFF_BNDY + 2), rxff_bndy);
		RTW_INFO("%s: normal mode, 0x%04x: 0x%04x\n", __func__,
			 REG_TRXFF_BNDY + 2,
			 rtw_read16(adapter, (REG_TRXFF_BNDY + 2)));
	}
}

bool rtw_read_from_frame_mask(_adapter *adapter, u8 idx)
{
	u32 data_l = 0, data_h = 0, rx_dma_buff_sz = 0, page_sz = 0;
	u16 offset, rx_buf_ptr = 0;
	u16 cam_start_offset = 0;
	u16 ctrl_l = 0, ctrl_h = 0;
	u8 count = 0, tmp = 0;
	int i = 0;
	bool res = _TRUE;

	if (idx > MAX_WKFM_CAM_NUM) {
		RTW_INFO("[Error]: %s, pattern index is out of range\n",
			 __func__);
		return _FALSE;
	}

	rtw_hal_get_def_var(adapter, HAL_DEF_RX_DMA_SZ_WOW,
			    (u8 *)&rx_dma_buff_sz);

	if (rx_dma_buff_sz == 0) {
		RTW_INFO("[Error]: %s, rx_dma_buff_sz is 0!!\n", __func__);
		return _FALSE;
	}

	rtw_hal_get_def_var(adapter, HAL_DEF_RX_PAGE_SIZE, (u8 *)&page_sz);

	if (page_sz == 0) {
		RTW_INFO("[Error]: %s, page_sz is 0!!\n", __func__);
		return _FALSE;
	}

	offset = (u16)PageNum(rx_dma_buff_sz, page_sz);
	cam_start_offset = offset * page_sz;

	ctrl_l = 0x0;
	ctrl_h = 0x0;

	/* Enable RX packet buffer access */
	rtw_write8(adapter, REG_PKT_BUFF_ACCESS_CTRL, RXPKT_BUF_SELECT);

	/* Read the WKFM CAM */
	for (i = 0; i < (WKFMCAM_ADDR_NUM / 2); i++) {
		/*
		 * Set Rx packet buffer offset.
		 * RxBufer pointer increases 1, we can access 8 bytes in Rx packet buffer.
		 * CAM start offset (unit: 1 byte) =  Index*WKFMCAM_SIZE
		 * RxBufer pointer addr = (CAM start offset + per entry offset of a WKFMCAM)/8
		 * * Index: The index of the wake up frame mask
		 * * WKFMCAM_SIZE: the total size of one WKFM CAM
		 * * per entry offset of a WKFM CAM: Addr i * 4 bytes
		 */
		rx_buf_ptr =
			(cam_start_offset + idx * WKFMCAM_SIZE + i * 8) >> 3;
		rtw_write16(adapter, REG_PKTBUF_DBG_CTRL, rx_buf_ptr);

		rtw_write16(adapter, REG_RXPKTBUF_CTRL, ctrl_l);
		data_l = rtw_read32(adapter, REG_PKTBUF_DBG_DATA_L);
		data_h = rtw_read32(adapter, REG_PKTBUF_DBG_DATA_H);

		RTW_INFO("[%d]: %08x %08x\n", i, data_h, data_l);

		count = 0;

		do {
			tmp = rtw_read8(adapter, REG_RXPKTBUF_CTRL);
			rtw_udelay_os(2);
			count++;
		} while (!tmp && count < 100);

		if (count >= 100) {
			RTW_INFO("%s count:%d\n", __func__, count);
			res = _FALSE;
		}
	}

	/* Disable RX packet buffer access */
	rtw_write8(adapter, REG_PKT_BUFF_ACCESS_CTRL,
		   DISABLE_TRXPKT_BUF_ACCESS);
	return res;
}

bool rtw_write_to_frame_mask(_adapter *adapter, u8 idx,
			     struct  rtl_wow_pattern *context)
{
	u32 data = 0, rx_dma_buff_sz = 0, page_sz = 0;
	u16 offset, rx_buf_ptr = 0;
	u16 cam_start_offset = 0;
	u16 ctrl_l = 0, ctrl_h = 0;
	u8 count = 0, tmp = 0;
	int res = 0, i = 0;

	if (idx > MAX_WKFM_CAM_NUM) {
		RTW_INFO("[Error]: %s, pattern index is out of range\n",
			 __func__);
		return _FALSE;
	}

	rtw_hal_get_def_var(adapter, HAL_DEF_RX_DMA_SZ_WOW,
			    (u8 *)&rx_dma_buff_sz);

	if (rx_dma_buff_sz == 0) {
		RTW_INFO("[Error]: %s, rx_dma_buff_sz is 0!!\n", __func__);
		return _FALSE;
	}

	rtw_hal_get_def_var(adapter, HAL_DEF_RX_PAGE_SIZE, (u8 *)&page_sz);

	if (page_sz == 0) {
		RTW_INFO("[Error]: %s, page_sz is 0!!\n", __func__);
		return _FALSE;
	}

	offset = (u16)PageNum(rx_dma_buff_sz, page_sz);

	cam_start_offset = offset * page_sz;

	if (IS_HARDWARE_TYPE_8188E(adapter)) {
		ctrl_l = 0x0001;
		ctrl_h = 0x0001;
	} else {
		ctrl_l = 0x0f01;
		ctrl_h = 0xf001;
	}

	/* Enable RX packet buffer access */
	rtw_write8(adapter, REG_PKT_BUFF_ACCESS_CTRL, RXPKT_BUF_SELECT);

	/* Write the WKFM CAM */
	for (i = 0; i < WKFMCAM_ADDR_NUM; i++) {
		/*
		 * Set Rx packet buffer offset.
		 * RxBufer pointer increases 1, we can access 8 bytes in Rx packet buffer.
		 * CAM start offset (unit: 1 byte) =  Index*WKFMCAM_SIZE
		 * RxBufer pointer addr = (CAM start offset + per entry offset of a WKFMCAM)/8
		 * * Index: The index of the wake up frame mask
		 * * WKFMCAM_SIZE: the total size of one WKFM CAM
		 * * per entry offset of a WKFM CAM: Addr i * 4 bytes
		 */
		rx_buf_ptr =
			(cam_start_offset + idx * WKFMCAM_SIZE + i * 4) >> 3;
		rtw_write16(adapter, REG_PKTBUF_DBG_CTRL, rx_buf_ptr);

		if (i == 0) {
			if (context->type == PATTERN_VALID)
				data = BIT(31);
			else if (context->type == PATTERN_BROADCAST)
				data = BIT(31) | BIT(26);
			else if (context->type == PATTERN_MULTICAST)
				data = BIT(31) | BIT(25);
			else if (context->type == PATTERN_UNICAST)
				data = BIT(31) | BIT(24);

			if (context->crc != 0)
				data |= context->crc;

			rtw_write32(adapter, REG_PKTBUF_DBG_DATA_L, data);
			rtw_write16(adapter, REG_RXPKTBUF_CTRL, ctrl_l);
		} else if (i == 1) {
			data = 0;
			rtw_write32(adapter, REG_PKTBUF_DBG_DATA_H, data);
			rtw_write16(adapter, REG_RXPKTBUF_CTRL, ctrl_h);
		} else if (i == 2 || i == 4) {
			data = context->mask[i - 2];
			rtw_write32(adapter, REG_PKTBUF_DBG_DATA_L, data);
			/* write to RX packet buffer*/
			rtw_write16(adapter, REG_RXPKTBUF_CTRL, ctrl_l);
		} else if (i == 3 || i == 5) {
			data = context->mask[i - 2];
			rtw_write32(adapter, REG_PKTBUF_DBG_DATA_H, data);
			/* write to RX packet buffer*/
			rtw_write16(adapter, REG_RXPKTBUF_CTRL, ctrl_h);
		}

		count = 0;
		do {
			tmp = rtw_read8(adapter, REG_RXPKTBUF_CTRL);
			rtw_udelay_os(2);
			count++;
		} while (tmp && count < 100);

		if (count >= 100)
			res = _FALSE;
		else
			res = _TRUE;
	}

	/* Disable RX packet buffer access */
	rtw_write8(adapter, REG_PKT_BUFF_ACCESS_CTRL,
		   DISABLE_TRXPKT_BUF_ACCESS);

	return res;
}
void rtw_clean_pattern(_adapter *adapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	struct rtl_wow_pattern zero_pattern;
	int i = 0;

	_rtw_memset(&zero_pattern, 0, sizeof(struct rtl_wow_pattern));

	zero_pattern.type = PATTERN_INVALID;

	for (i = 0; i < MAX_WKFM_CAM_NUM; i++)
		rtw_write_to_frame_mask(adapter, i, &zero_pattern);

	rtw_write8(adapter, REG_WKFMCAM_NUM, 0);
}
static int rtw_hal_set_pattern(_adapter *adapter, u8 *pattern,
			       u8 len, u8 *mask, u8 idx)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	struct mlme_ext_priv *pmlmeext = NULL;
	struct mlme_ext_info *pmlmeinfo = NULL;
	struct rtl_wow_pattern wow_pattern;
	u8 mask_hw[MAX_WKFM_SIZE] = {0};
	u8 content[MAX_WKFM_PATTERN_SIZE] = {0};
	u8 broadcast_addr[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u8 multicast_addr1[2] = {0x33, 0x33};
	u8 multicast_addr2[3] = {0x01, 0x00, 0x5e};
	u8 res = _FALSE, index = 0, mask_len = 0;
	u8 mac_addr[ETH_ALEN] = {0};
	u16 count = 0;
	int i, j;

	if (pwrctl->wowlan_pattern_idx > MAX_WKFM_CAM_NUM) {
		RTW_INFO("%s pattern_idx is more than MAX_FMC_NUM: %d\n",
			 __func__, MAX_WKFM_CAM_NUM);
		return _FALSE;
	}

	pmlmeext = &adapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;
	_rtw_memcpy(mac_addr, adapter_mac_addr(adapter), ETH_ALEN);
	_rtw_memset(&wow_pattern, 0, sizeof(struct rtl_wow_pattern));

	mask_len = DIV_ROUND_UP(len, 8);

	/* 1. setup A1 table */
	if (memcmp(pattern, broadcast_addr, ETH_ALEN) == 0)
		wow_pattern.type = PATTERN_BROADCAST;
	else if (memcmp(pattern, multicast_addr1, 2) == 0)
		wow_pattern.type = PATTERN_MULTICAST;
	else if (memcmp(pattern, multicast_addr2, 3) == 0)
		wow_pattern.type = PATTERN_MULTICAST;
	else if (memcmp(pattern, mac_addr, ETH_ALEN) == 0)
		wow_pattern.type = PATTERN_UNICAST;
	else
		wow_pattern.type = PATTERN_INVALID;

	/* translate mask from os to mask for hw */

/******************************************************************************
 * pattern from OS uses 'ethenet frame', like this:

	|    6   |    6   |   2  |     20    |  Variable  |  4  |
	|--------+--------+------+-----------+------------+-----|
	|    802.3 Mac Header    | IP Header | TCP Packet | FCS |
	|   DA   |   SA   | Type |

 * BUT, packet catched by our HW is in '802.11 frame', begin from LLC,

	|     24 or 30      |    6   |   2  |     20    |  Variable  |  4  |
	|-------------------+--------+------+-----------+------------+-----|
	| 802.11 MAC Header |       LLC     | IP Header | TCP Packet | FCS |
			    | Others | Tpye |

 * Therefore, we need translate mask_from_OS to mask_to_hw.
 * We should left-shift mask by 6 bits, then set the new bit[0~5] = 0,
 * because new mask[0~5] means 'SA', but our HW packet begins from LLC,
 * bit[0~5] corresponds to first 6 Bytes in LLC, they just don't match.
 ******************************************************************************/
	/* Shift 6 bits */
	for (i = 0; i < mask_len - 1; i++) {
		mask_hw[i] = mask[i] >> 6;
		mask_hw[i] |= (mask[i + 1] & 0x3F) << 2;
	}

	mask_hw[i] = (mask[i] >> 6) & 0x3F;
	/* Set bit 0-5 to zero */
	mask_hw[0] &= 0xC0;

	for (i = 0; i < (MAX_WKFM_SIZE / 4); i++) {
		wow_pattern.mask[i] = mask_hw[i * 4];
		wow_pattern.mask[i] |= (mask_hw[i * 4 + 1] << 8);
		wow_pattern.mask[i] |= (mask_hw[i * 4 + 2] << 16);
		wow_pattern.mask[i] |= (mask_hw[i * 4 + 3] << 24);
	}

	/* To get the wake up pattern from the mask.
	 * We do not count first 12 bits which means
	 * DA[6] and SA[6] in the pattern to match HW design. */
	count = 0;
	for (i = 12; i < len; i++) {
		if ((mask[i / 8] >> (i % 8)) & 0x01) {
			content[count] = pattern[i];
			count++;
		}
	}

	wow_pattern.crc = rtw_calc_crc(content, count);

	if (wow_pattern.crc != 0) {
		if (wow_pattern.type == PATTERN_INVALID)
			wow_pattern.type = PATTERN_VALID;
	}

	index = idx;

	if (!pwrctl->bInSuspend)
		index += 2;

	/* write pattern */
	res = rtw_write_to_frame_mask(adapter, index, &wow_pattern);

	if (res == _FALSE)
		RTW_INFO("%s: ERROR!! idx: %d write_to_frame_mask_cam fail\n",
			 __func__, idx);

	return res;
}
void rtw_fill_pattern(_adapter *adapter)
{
	int i = 0, total = 0, index;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	struct rtl_wow_pattern wow_pattern;

	total = pwrpriv->wowlan_pattern_idx;

	if (total > MAX_WKFM_CAM_NUM)
		total = MAX_WKFM_CAM_NUM;

	for (i = 0 ; i < total ; i++) {
		if (_SUCCESS == rtw_hal_wow_pattern_generate(adapter, i, &wow_pattern)) {

			index = i;
			if (!pwrpriv->bInSuspend)
				index += 2;

			if (rtw_write_to_frame_mask(adapter, index, &wow_pattern) == _FALSE)
				RTW_INFO("%s: ERROR!! idx: %d write_to_frame_mask_cam fail\n", __func__, i);
		}

	}
	rtw_write8(adapter, REG_WKFMCAM_NUM, total);

}

#else /*CONFIG_WOW_PATTERN_HW_CAM*/

#define WOW_CAM_ACCESS_TIMEOUT_MS	200
#define WOW_VALID_BIT	BIT31
#define WOW_BC_BIT		BIT26
#define WOW_MC_BIT		BIT25
#define WOW_UC_BIT		BIT24

static u32 _rtw_wow_pattern_read_cam(_adapter *adapter, u8 addr)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	_mutex *mutex = &pwrpriv->wowlan_pattern_cam_mutex;

	u32 rdata = 0;
	u32 cnt = 0;
	u32 start = 0;
	u8 timeout = 0;
	u8 rst = _FALSE;

	_enter_critical_mutex(mutex, NULL);

	rtw_write32(adapter, REG_WKFMCAM_CMD, BIT_WKFCAM_POLLING_V1 | BIT_WKFCAM_ADDR_V2(addr));

	start = rtw_get_current_time();
	while (1) {
		if (rtw_is_surprise_removed(adapter))
			break;

		cnt++;
		if (0 == (rtw_read32(adapter, REG_WKFMCAM_CMD) & BIT_WKFCAM_POLLING_V1)) {
			rst = _SUCCESS;
			break;
		}
		if (rtw_get_passing_time_ms(start) > WOW_CAM_ACCESS_TIMEOUT_MS) {
			timeout = 1;
			break;
		}
	}

	rdata = rtw_read32(adapter, REG_WKFMCAM_RWD);

	_exit_critical_mutex(mutex, NULL);

	/*RTW_INFO("%s ==> addr:0x%02x , rdata:0x%08x\n", __func__, addr, rdata);*/

	if (timeout)
		RTW_ERR(FUNC_ADPT_FMT" failed due to polling timeout\n", FUNC_ADPT_ARG(adapter));

	return rdata;
}
void rtw_wow_pattern_read_cam_ent(_adapter *adapter, u8 id, struct  rtl_wow_pattern *context)
{
	int i;
	u32 rdata;

	_rtw_memset(context, 0, sizeof(struct  rtl_wow_pattern));

	for (i = 4; i >= 0; i--) {
		rdata = _rtw_wow_pattern_read_cam(adapter, (id << 3) | i);

		switch (i) {
		case 4:
			if (rdata & WOW_BC_BIT)
				context->type = PATTERN_BROADCAST;
			else if (rdata & WOW_MC_BIT)
				context->type = PATTERN_MULTICAST;
			else if (rdata & WOW_UC_BIT)
				context->type = PATTERN_UNICAST;
			else
				context->type = PATTERN_INVALID;

			context->crc = rdata & 0xFFFF;
			break;
		default:
			_rtw_memcpy(&context->mask[i], (u8 *)(&rdata), 4);
			break;
		}
	}
}

static void _rtw_wow_pattern_write_cam(_adapter *adapter, u8 addr, u32 wdata)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	_mutex *mutex = &pwrpriv->wowlan_pattern_cam_mutex;
	u32 cnt = 0;
	u32 start = 0, end = 0;
	u8 timeout = 0;

	/*RTW_INFO("%s ==> addr:0x%02x , wdata:0x%08x\n", __func__, addr, wdata);*/
	_enter_critical_mutex(mutex, NULL);

	rtw_write32(adapter, REG_WKFMCAM_RWD, wdata);
	rtw_write32(adapter, REG_WKFMCAM_CMD, BIT_WKFCAM_POLLING_V1 | BIT_WKFCAM_WE | BIT_WKFCAM_ADDR_V2(addr));

	start = rtw_get_current_time();
	while (1) {
		if (rtw_is_surprise_removed(adapter))
			break;

		cnt++;
		if (0 == (rtw_read32(adapter, REG_WKFMCAM_CMD) & BIT_WKFCAM_POLLING_V1))
			break;

		if (rtw_get_passing_time_ms(start) > WOW_CAM_ACCESS_TIMEOUT_MS) {
			timeout = 1;
			break;
		}
	}
	end = rtw_get_current_time();

	_exit_critical_mutex(mutex, NULL);

	if (timeout) {
		RTW_ERR(FUNC_ADPT_FMT" addr:0x%02x, wdata:0x%08x, to:%u, polling:%u, %d ms\n"
			, FUNC_ADPT_ARG(adapter), addr, wdata, timeout, cnt, rtw_get_time_interval_ms(start, end));
	}
}

void rtw_wow_pattern_write_cam_ent(_adapter *adapter, u8 id, struct  rtl_wow_pattern *context)
{
	int j;
	u8 addr;
	u32 wdata = 0;

	for (j = 4; j >= 0; j--) {
		switch (j) {
		case 4:
			wdata = context->crc;

			if (PATTERN_BROADCAST == context->type)
				wdata |= WOW_BC_BIT;
			if (PATTERN_MULTICAST == context->type)
				wdata |= WOW_MC_BIT;
			if (PATTERN_UNICAST == context->type)
				wdata |= WOW_UC_BIT;
			if (PATTERN_INVALID != context->type)
				wdata |= WOW_VALID_BIT;
			break;
		default:
			wdata = context->mask[j];
			break;
		}

		addr = (id << 3) + j;

		_rtw_wow_pattern_write_cam(adapter, addr, wdata);
	}
}

static u8 _rtw_wow_pattern_clean_cam(_adapter *adapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	_mutex *mutex = &pwrpriv->wowlan_pattern_cam_mutex;
	u32 cnt = 0;
	u32 start = 0;
	u8 timeout = 0;
	u8 rst = _FAIL;

	_enter_critical_mutex(mutex, NULL);
	rtw_write32(adapter, REG_WKFMCAM_CMD, BIT_WKFCAM_POLLING_V1 | BIT_WKFCAM_CLR_V1);

	start = rtw_get_current_time();
	while (1) {
		if (rtw_is_surprise_removed(adapter))
			break;

		cnt++;
		if (0 == (rtw_read32(adapter, REG_WKFMCAM_CMD) & BIT_WKFCAM_POLLING_V1)) {
			rst = _SUCCESS;
			break;
		}
		if (rtw_get_passing_time_ms(start) > WOW_CAM_ACCESS_TIMEOUT_MS) {
			timeout = 1;
			break;
		}
	}
	_exit_critical_mutex(mutex, NULL);

	if (timeout)
		RTW_ERR(FUNC_ADPT_FMT" falied ,polling timeout\n", FUNC_ADPT_ARG(adapter));

	return rst;
}

void rtw_clean_pattern(_adapter *adapter)
{
	if (_FAIL == _rtw_wow_pattern_clean_cam(adapter))
		RTW_ERR("rtw_clean_pattern failed\n");
}

void rtw_dump_wow_pattern(void *sel, struct rtl_wow_pattern *pwow_pattern, u8 idx)
{
	int j;

	RTW_PRINT_SEL(sel, "=======WOW CAM-ID[%d]=======\n", idx);
	RTW_PRINT_SEL(sel, "[WOW CAM] type:%d\n", pwow_pattern->type);
	RTW_PRINT_SEL(sel, "[WOW CAM] crc:0x%04x\n", pwow_pattern->crc);
	for (j = 0; j < 4; j++)
		RTW_PRINT_SEL(sel, "[WOW CAM] Mask:0x%08x\n", pwow_pattern->mask[j]);
}

void rtw_fill_pattern(_adapter *adapter)
{
	int i = 0, total = 0;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	struct rtl_wow_pattern wow_pattern;

	total = pwrpriv->wowlan_pattern_idx;

	if (total > MAX_WKFM_CAM_NUM)
		total = MAX_WKFM_CAM_NUM;

	for (i = 0 ; i < total ; i++) {
		if (_SUCCESS == rtw_hal_wow_pattern_generate(adapter, i, &wow_pattern)) {
			rtw_dump_wow_pattern(RTW_DBGDUMP, &wow_pattern, i);
			rtw_wow_pattern_write_cam_ent(adapter, i, &wow_pattern);
		}
	}
}

#endif
void rtw_wow_pattern_cam_dump(_adapter *adapter)
{

#ifndef CONFIG_WOW_PATTERN_HW_CAM
	int i;

	for (i = 0 ; i < MAX_WKFM_CAM_NUM; i++) {
		RTW_INFO("=======[%d]=======\n", i);
		rtw_read_from_frame_mask(adapter, i);
	}
#else
	struct  rtl_wow_pattern context;
	int i;

	for (i = 0 ; i < MAX_WKFM_CAM_NUM; i++) {
		rtw_wow_pattern_read_cam_ent(adapter, i, &context);
		rtw_dump_wow_pattern(RTW_DBGDUMP, &context, i);
	}

#endif
}


static void rtw_hal_dl_pattern(_adapter *adapter, u8 mode)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);

	switch (mode) {
	case 0:
		rtw_clean_pattern(adapter);
		RTW_INFO("%s: total patterns: %d\n", __func__, pwrpriv->wowlan_pattern_idx);
		break;
	case 1:
		rtw_set_default_pattern(adapter);
		rtw_fill_pattern(adapter);
		RTW_INFO("%s: pattern total: %d downloaded\n", __func__, pwrpriv->wowlan_pattern_idx);
		break;
	case 2:
		rtw_clean_pattern(adapter);
		rtw_wow_pattern_sw_reset(adapter);
		RTW_INFO("%s: clean patterns\n", __func__);
		break;
	default:
		RTW_INFO("%s: unknown mode\n", __func__);
		break;
	}
}

static void rtw_hal_wow_enable(_adapter *adapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct hal_ops *pHalFunc = &adapter->hal_func;
	struct sta_info *psta = NULL;
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(adapter);
	int res;
	u16 media_status_rpt;


	RTW_PRINT("%s, WOWLAN_ENABLE\n", __func__);
	rtw_hal_gate_bb(adapter, _TRUE);
#ifdef CONFIG_GTK_OL
	if (psecuritypriv->binstallKCK_KEK == _TRUE)
		rtw_hal_fw_sync_cam_id(adapter);
#endif
	if (IS_HARDWARE_TYPE_8723B(adapter))
		rtw_hal_backup_rate(adapter);

	/* RX DMA stop */
	#if defined(CONFIG_RTL8188E)
	if (IS_HARDWARE_TYPE_8188E(adapter))
		rtw_hal_disable_tx_report(adapter);
	#endif

	res = rtw_hal_pause_rx_dma(adapter);
	if (res == _FAIL)
		RTW_PRINT("[WARNING] pause RX DMA fail\n");

	#ifndef CONFIG_WOW_PATTERN_HW_CAM
	/* Reconfig RX_FF Boundary */
	rtw_hal_set_wow_rxff_boundary(adapter, _TRUE);
	#endif

	/* redownload wow pattern */
	rtw_hal_dl_pattern(adapter, 1);

	rtw_hal_fw_dl(adapter, _TRUE);
	media_status_rpt = RT_MEDIA_CONNECT;
	rtw_hal_set_hwreg(adapter, HW_VAR_H2C_FW_JOINBSSRPT,
			  (u8 *)&media_status_rpt);

	if (!pwrctl->wowlan_pno_enable) {
		psta = rtw_get_stainfo(&adapter->stapriv, get_bssid(pmlmepriv));

		if (psta != NULL) {
			#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
			rtw_hal_set_default_port_id_cmd(adapter, psta->mac_id);
			#endif

			rtw_sta_media_status_rpt(adapter, psta, 1);
		}
	}

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	/* Enable CPWM2 only. */
	res = rtw_hal_enable_cpwm2(adapter);
	if (res == _FAIL)
		RTW_PRINT("[WARNING] enable cpwm2 fail\n");
#endif
#ifdef CONFIG_GPIO_WAKEUP
	rtw_hal_switch_gpio_wl_ctrl(adapter, WAKEUP_GPIO_IDX, _TRUE);
#endif
	/* Set WOWLAN H2C command. */
	RTW_PRINT("Set WOWLan cmd\n");
	rtw_hal_set_fw_wow_related_cmd(adapter, 1);

	res = rtw_hal_check_wow_ctrl(adapter, _TRUE);

	if (res == _FALSE)
		RTW_INFO("[Error]%s: set wowlan CMD fail!!\n", __func__);

	pwrctl->wowlan_wake_reason =
		rtw_read8(adapter, REG_WOWLAN_WAKE_REASON);

	RTW_PRINT("wowlan_wake_reason: 0x%02x\n",
		  pwrctl->wowlan_wake_reason);
#ifdef CONFIG_GTK_OL_DBG
	dump_sec_cam(RTW_DBGDUMP, adapter);
	dump_sec_cam_cache(RTW_DBGDUMP, adapter);
#endif
#ifdef CONFIG_USB_HCI
	/* free adapter's resource */
	rtw_mi_intf_stop(adapter);

	/* Invoid SE0 reset signal during suspending*/
	rtw_write8(adapter, REG_RSV_CTRL, 0x20);
	if (IS_8188F(pHalData->version_id) == FALSE)
		rtw_write8(adapter, REG_RSV_CTRL, 0x60);
#endif /*CONFIG_USB_HCI*/

	rtw_hal_gate_bb(adapter, _FALSE);
}

#define DBG_WAKEUP_REASON
#ifdef DBG_WAKEUP_REASON
void _dbg_wake_up_reason_string(_adapter *adapter, const char *srt_res)
{
	RTW_INFO(ADPT_FMT "- wake up reason - %s\n", ADPT_ARG(adapter), srt_res);
}
void _dbg_rtw_wake_up_reason(_adapter *adapter, u8 reason)
{
	if (RX_PAIRWISEKEY == reason)
		_dbg_wake_up_reason_string(adapter, "Rx pairwise key");
	else if (RX_GTK == reason)
		_dbg_wake_up_reason_string(adapter, "Rx GTK");
	else if (RX_FOURWAY_HANDSHAKE == reason)
		_dbg_wake_up_reason_string(adapter, "Rx four way handshake");
	else if (RX_DISASSOC == reason)
		_dbg_wake_up_reason_string(adapter, "Rx disassoc");
	else if (RX_DEAUTH == reason)
		_dbg_wake_up_reason_string(adapter, "Rx deauth");
	else if (RX_ARP_REQUEST == reason)
		_dbg_wake_up_reason_string(adapter, "Rx ARP request");
	else if (FW_DECISION_DISCONNECT == reason)
		_dbg_wake_up_reason_string(adapter, "FW detect disconnect");
	else if (RX_MAGIC_PKT == reason)
		_dbg_wake_up_reason_string(adapter, "Rx magic packet");
	else if (RX_UNICAST_PKT == reason)
		_dbg_wake_up_reason_string(adapter, "Rx unicast packet");
	else if (RX_PATTERN_PKT == reason)
		_dbg_wake_up_reason_string(adapter, "Rx pattern packet");
	else if (RTD3_SSID_MATCH == reason)
		_dbg_wake_up_reason_string(adapter, "RTD3 SSID match");
	else if (RX_REALWOW_V2_WAKEUP_PKT == reason)
		_dbg_wake_up_reason_string(adapter, "Rx real WOW V2 wakeup packet");
	else if (RX_REALWOW_V2_ACK_LOST == reason)
		_dbg_wake_up_reason_string(adapter, "Rx real WOW V2 ack lost");
	else if (ENABLE_FAIL_DMA_IDLE == reason)
		_dbg_wake_up_reason_string(adapter, "enable fail DMA idle");
	else if (ENABLE_FAIL_DMA_PAUSE == reason)
		_dbg_wake_up_reason_string(adapter, "enable fail DMA pause");
	else if (AP_OFFLOAD_WAKEUP == reason)
		_dbg_wake_up_reason_string(adapter, "AP offload wakeup");
	else if (CLK_32K_UNLOCK == reason)
		_dbg_wake_up_reason_string(adapter, "clk 32k unlock");
	else if (RTIME_FAIL_DMA_IDLE == reason)
		_dbg_wake_up_reason_string(adapter, "RTIME fail DMA idle");
	else if (CLK_32K_LOCK == reason)
		_dbg_wake_up_reason_string(adapter, "clk 32k lock");
	else
		_dbg_wake_up_reason_string(adapter, "unknown reasoen");
}
#endif

static void rtw_hal_wow_disable(_adapter *adapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct hal_ops *pHalFunc = &adapter->hal_func;
	struct sta_info *psta = NULL;
	int res;
	u16 media_status_rpt;
	u8 val8;

	RTW_PRINT("%s, WOWLAN_DISABLE\n", __func__);

	if (!pwrctl->wowlan_pno_enable) {
		psta = rtw_get_stainfo(&adapter->stapriv, get_bssid(pmlmepriv));
		if (psta != NULL)
			rtw_sta_media_status_rpt(adapter, psta, 0);
		else
			RTW_INFO("%s: psta is null\n", __func__);
	}

	if (0) {
		RTW_INFO("0x630:0x%02x\n", rtw_read8(adapter, 0x630));
		RTW_INFO("0x631:0x%02x\n", rtw_read8(adapter, 0x631));
		RTW_INFO("0x634:0x%02x\n", rtw_read8(adapter, 0x634));
		RTW_INFO("0x1c7:0x%02x\n", rtw_read8(adapter, 0x1c7));
	}

	pwrctl->wowlan_wake_reason = rtw_read8(adapter, REG_WOWLAN_WAKE_REASON);

	RTW_PRINT("wakeup_reason: 0x%02x\n",
		  pwrctl->wowlan_wake_reason);
	#ifdef DBG_WAKEUP_REASON
	_dbg_rtw_wake_up_reason(adapter, pwrctl->wowlan_wake_reason);
	#endif

	rtw_hal_set_fw_wow_related_cmd(adapter, 0);

	res = rtw_hal_check_wow_ctrl(adapter, _FALSE);

	if (res == _FALSE) {
		RTW_INFO("[Error]%s: disable WOW cmd fail\n!!", __func__);
		rtw_hal_force_enable_rxdma(adapter);
	}

	rtw_hal_gate_bb(adapter, _TRUE);

	res = rtw_hal_pause_rx_dma(adapter);
	if (res == _FAIL)
		RTW_PRINT("[WARNING] pause RX DMA fail\n");

	/* clean HW pattern match */
	rtw_hal_dl_pattern(adapter, 0);

	#ifndef CONFIG_WOW_PATTERN_HW_CAM
	/* config RXFF boundary to original */
	rtw_hal_set_wow_rxff_boundary(adapter, _FALSE);
	#endif
	rtw_hal_release_rx_dma(adapter);

	#if defined(CONFIG_RTL8188E)
	if (IS_HARDWARE_TYPE_8188E(adapter))
		rtw_hal_enable_tx_report(adapter);
	#endif

#ifdef CONFIG_GTK_OL
	if (((pwrctl->wowlan_wake_reason != RX_DISASSOC) ||
		(pwrctl->wowlan_wake_reason != RX_DEAUTH) ||
		(pwrctl->wowlan_wake_reason != FW_DECISION_DISCONNECT)) &&
		psecuritypriv->binstallKCK_KEK == _TRUE) {
		rtw_hal_get_aoac_rpt(adapter);
		rtw_hal_update_sw_security_info(adapter);
	}
#endif /*CONFIG_GTK_OL*/

	rtw_hal_fw_dl(adapter, _FALSE);

#ifdef CONFIG_GPIO_WAKEUP
	val8 = (pwrctl->is_high_active == 0) ? 1 : 0;
	RTW_PRINT("Set Wake GPIO to default(%d).\n", val8);
	rtw_hal_set_output_gpio(adapter, WAKEUP_GPIO_IDX, val8);

	rtw_hal_switch_gpio_wl_ctrl(adapter, WAKEUP_GPIO_IDX, _FALSE);
#endif

	if ((pwrctl->wowlan_wake_reason != FW_DECISION_DISCONNECT) &&
	    (pwrctl->wowlan_wake_reason != RX_PAIRWISEKEY) &&
	    (pwrctl->wowlan_wake_reason != RX_DISASSOC) &&
	    (pwrctl->wowlan_wake_reason != RX_DEAUTH)) {

		media_status_rpt = RT_MEDIA_CONNECT;
		rtw_hal_set_hwreg(adapter, HW_VAR_H2C_FW_JOINBSSRPT,
				  (u8 *)&media_status_rpt);

		if (psta != NULL) {
			#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
			rtw_hal_set_default_port_id_cmd(adapter, psta->mac_id);
			#endif
			rtw_sta_media_status_rpt(adapter, psta, 1);
		}
	}
	rtw_hal_gate_bb(adapter, _FALSE);
}
#endif /*CONFIG_WOWLAN*/

#ifdef CONFIG_P2P_WOWLAN
void rtw_hal_set_p2p_wow_fw_rsvd_page(_adapter *adapter, u8 *pframe, u16 index,
	      u8 tx_desc, u32 page_size, u8 *page_num, u32 *total_pkt_len,
				      RSVDPAGE_LOC *rsvd_page_loc)
{
	u32 P2PNegoRspLength = 0, P2PInviteRspLength = 0;
	u32 P2PPDRspLength = 0, P2PProbeRspLength = 0, P2PBCNLength = 0;
	u8 CurtPktPageNum = 0;

	/* P2P Beacon */
	rsvd_page_loc->LocP2PBeacon = *page_num;
	rtw_hal_construct_P2PBeacon(adapter, &pframe[index], &P2PBCNLength);
	rtw_hal_fill_fake_txdesc(adapter, &pframe[index - tx_desc],
				 P2PBCNLength, _FALSE, _FALSE, _FALSE);

#if 0
	RTW_INFO("%s(): HW_VAR_SET_TX_CMD: PROBE RSP %p %d\n",
		__FUNCTION__, &pframe[index - tx_desc], (P2PBCNLength + tx_desc));
#endif

	CurtPktPageNum = (u8)PageNum(tx_desc + P2PBCNLength, page_size);

	*page_num += CurtPktPageNum;

	index += (CurtPktPageNum * page_size);

	/* P2P Probe rsp */
	rsvd_page_loc->LocP2PProbeRsp = *page_num;
	rtw_hal_construct_P2PProbeRsp(adapter, &pframe[index],
				      &P2PProbeRspLength);
	rtw_hal_fill_fake_txdesc(adapter, &pframe[index - tx_desc],
				 P2PProbeRspLength, _FALSE, _FALSE, _FALSE);

	/* RTW_INFO("%s(): HW_VAR_SET_TX_CMD: PROBE RSP %p %d\n",  */
	/*	__FUNCTION__, &pframe[index-tx_desc], (P2PProbeRspLength+tx_desc)); */

	CurtPktPageNum = (u8)PageNum(tx_desc + P2PProbeRspLength, page_size);

	*page_num += CurtPktPageNum;

	index += (CurtPktPageNum * page_size);

	/* P2P nego rsp */
	rsvd_page_loc->LocNegoRsp = *page_num;
	rtw_hal_construct_P2PNegoRsp(adapter, &pframe[index],
				     &P2PNegoRspLength);
	rtw_hal_fill_fake_txdesc(adapter, &pframe[index - tx_desc],
				 P2PNegoRspLength, _FALSE, _FALSE, _FALSE);

	/* RTW_INFO("%s(): HW_VAR_SET_TX_CMD: QOS NULL DATA %p %d\n",  */
	/*	__FUNCTION__, &pframe[index-tx_desc], (NegoRspLength+tx_desc)); */

	CurtPktPageNum = (u8)PageNum(tx_desc + P2PNegoRspLength, page_size);

	*page_num += CurtPktPageNum;

	index += (CurtPktPageNum * page_size);

	/* P2P invite rsp */
	rsvd_page_loc->LocInviteRsp = *page_num;
	rtw_hal_construct_P2PInviteRsp(adapter, &pframe[index],
				       &P2PInviteRspLength);
	rtw_hal_fill_fake_txdesc(adapter, &pframe[index - tx_desc],
				 P2PInviteRspLength, _FALSE, _FALSE, _FALSE);

	/* RTW_INFO("%s(): HW_VAR_SET_TX_CMD: QOS NULL DATA %p %d\n",  */
	/* __FUNCTION__, &pframe[index-tx_desc], (InviteRspLength+tx_desc)); */

	CurtPktPageNum = (u8)PageNum(tx_desc + P2PInviteRspLength, page_size);

	*page_num += CurtPktPageNum;

	index += (CurtPktPageNum * page_size);

	/* P2P provision discovery rsp */
	rsvd_page_loc->LocPDRsp = *page_num;
	rtw_hal_construct_P2PProvisionDisRsp(adapter,
					     &pframe[index], &P2PPDRspLength);

	rtw_hal_fill_fake_txdesc(adapter, &pframe[index - tx_desc],
				 P2PPDRspLength, _FALSE, _FALSE, _FALSE);

	/* RTW_INFO("%s(): HW_VAR_SET_TX_CMD: QOS NULL DATA %p %d\n",  */
	/*	__FUNCTION__, &pframe[index-tx_desc], (PDRspLength+tx_desc)); */

	CurtPktPageNum = (u8)PageNum(tx_desc + P2PPDRspLength, page_size);

	*page_num += CurtPktPageNum;

	*total_pkt_len = index + P2PPDRspLength;

	index += (CurtPktPageNum * page_size);


}
#endif /* CONFIG_P2P_WOWLAN */

#ifdef CONFIG_LPS_PG
#include "hal_halmac.h"

#define DBG_LPSPG_SEC_DUMP
#define LPS_PG_INFO_RSVD_LEN	16
#define LPS_PG_INFO_RSVD_PAGE_NUM	1

#define DBG_LPSPG_INFO_DUMP
static void rtw_hal_set_lps_pg_info_rsvd_page(_adapter *adapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	struct sta_info	*psta = rtw_get_stainfo(&adapter->stapriv, get_bssid(&adapter->mlmepriv));
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	PHAL_DATA_TYPE phal_data = GET_HAL_DATA(adapter);
	u8 lps_pg_info[LPS_PG_INFO_RSVD_LEN] = {0};
#ifdef CONFIG_MBSSID_CAM
	u8 cam_id = INVALID_CAM_ID;
#endif
	u8 *psec_cam_id = lps_pg_info + 8;
	u8 sec_cam_num = 0;

	if (!psta) {
		RTW_ERR("%s [ERROR] sta is NULL\n", __func__);
		rtw_warn_on(1);
		return;
	}

	/*Byte 0 - used macid*/
	LPSPG_RSVD_PAGE_SET_MACID(lps_pg_info, psta->mac_id);
	RTW_INFO("[LPSPG-INFO] mac_id:%d\n", psta->mac_id);

#ifdef CONFIG_MBSSID_CAM
	/*Byte 1 - used BSSID CAM entry*/
	cam_id = rtw_mbid_cam_search_by_ifaceid(adapter, adapter->iface_id);
	if (cam_id != INVALID_CAM_ID)
		LPSPG_RSVD_PAGE_SET_MBSSCAMID(lps_pg_info, cam_id);
	RTW_INFO("[LPSPG-INFO] mbss_cam_id:%d\n", cam_id);
#endif

#ifdef CONFIG_WOWLAN /*&& pattern match cam used*/
	/*Btye 2 - Max used Pattern Match CAM entry*/
	if (pwrpriv->wowlan_mode == _TRUE &&
	    check_fwstate(&adapter->mlmepriv, _FW_LINKED) == _TRUE) {
		LPSPG_RSVD_PAGE_SET_PMC_NUM(lps_pg_info, pwrpriv->wowlan_pattern_idx);
		RTW_INFO("[LPSPG-INFO] Max Pattern Match CAM entry :%d\n", pwrpriv->wowlan_pattern_idx);
	}
#endif
#ifdef CONFIG_BEAMFORMING  /*&& MU BF*/
	/*Btye 3 - Max MU rate table Group ID*/
	LPSPG_RSVD_PAGE_SET_MU_RAID_GID(lps_pg_info, _value);
	RTW_INFO("[LPSPG-INFO] Max MU rate table Group ID :%d\n", _value);
#endif

	/*Btye 8 ~15 - used Security CAM entry */
	sec_cam_num = rtw_get_sec_camid(adapter, 8, psec_cam_id);

	/*Btye 4 - used Security CAM entry number*/
	if (sec_cam_num < 8)
		LPSPG_RSVD_PAGE_SET_SEC_CAM_NUM(lps_pg_info, sec_cam_num);
	RTW_INFO("[LPSPG-INFO] Security CAM entry number :%d\n", sec_cam_num);

	/*Btye 5 - Txbuf used page number for fw offload*/
	LPSPG_RSVD_PAGE_SET_DRV_RSVDPAGE_NUM(lps_pg_info, phal_data->drv_rsvd_page_number);
	RTW_INFO("[LPSPG-INFO] DRV's rsvd page numbers :%d\n", phal_data->drv_rsvd_page_number);

#ifdef DBG_LPSPG_SEC_DUMP
	{
		int i;

		for (i = 0; i < sec_cam_num; i++)
			RTW_INFO("%d = sec_cam_id:%d\n", i, psec_cam_id[i]);
	}
#endif

#ifdef DBG_LPSPG_INFO_DUMP
	RTW_INFO("==== DBG_LPSPG_INFO_RSVD_PAGE_DUMP====\n");
	RTW_INFO("  %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
		*(lps_pg_info), *(lps_pg_info + 1), *(lps_pg_info + 2), *(lps_pg_info + 3),
		*(lps_pg_info + 4), *(lps_pg_info + 5), *(lps_pg_info + 6), *(lps_pg_info + 7));
	RTW_INFO("  %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n",
		*(lps_pg_info + 8), *(lps_pg_info + 9), *(lps_pg_info + 10), *(lps_pg_info + 11),
		*(lps_pg_info + 12), *(lps_pg_info + 13), *(lps_pg_info + 14), *(lps_pg_info + 15));
	RTW_INFO("==== DBG_LPSPG_INFO_RSVD_PAGE_DUMP====\n");
#endif

	rtw_halmac_download_rsvd_page(dvobj, pwrpriv->lpspg_rsvd_page_locate, lps_pg_info, LPS_PG_INFO_RSVD_LEN);

#ifdef DBG_LPSPG_INFO_DUMP
	RTW_INFO("Get LPS-PG INFO from rsvd page_offset:%d\n", pwrpriv->lpspg_rsvd_page_locate);
	rtw_dump_rsvd_page(RTW_DBGDUMP, adapter, pwrpriv->lpspg_rsvd_page_locate, 1);
#endif
}


static u8 rtw_hal_set_lps_pg_info_cmd(_adapter *adapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	u8 lpspg_info[H2C_LPS_PG_INFO_LEN] = {0};
	u8 ret = _FAIL;

	RTW_INFO("%s: loc_lpspg_info:%d\n", __func__, pwrpriv->lpspg_rsvd_page_locate);

	if (_NO_PRIVACY_ != adapter->securitypriv.dot11PrivacyAlgrthm)
		SET_H2CCMD_LPSPG_SEC_CAM_EN(lpspg_info, 1);	/*SecurityCAM_En*/
#ifdef CONFIG_MBSSID_CAM
	SET_H2CCMD_LPSPG_MBID_CAM_EN(lpspg_info, 1);		/*BSSIDCAM_En*/
#endif

#if defined(CONFIG_WOWLAN) && defined(CONFIG_WOW_PATTERN_HW_CAM)
	if (pwrpriv->wowlan_mode == _TRUE &&
	    check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) {

		SET_H2CCMD_LPSPG_PMC_CAM_EN(lpspg_info, 1);	/*PatternMatchCAM_En*/
	}
#endif

#ifdef CONFIG_MACID_SEARCH
	SET_H2CCMD_LPSPG_MACID_SEARCH_EN(lpspg_info, 1);	/*MACIDSearch_En*/
#endif

#ifdef CONFIG_TX_SC
	SET_H2CCMD_LPSPG_TXSC_EN(lpspg_info, 1);	/*TXSC_En*/
#endif

#ifdef CONFIG_BEAMFORMING  /*&& MU BF*/
	SET_H2CCMD_LPSPG_MU_RATE_TB_EN(lpspg_info, 1);	/*MURateTable_En*/
#endif

	SET_H2CCMD_LPSPG_LOC(lpspg_info, pwrpriv->lpspg_rsvd_page_locate);

#ifdef DBG_LPSPG_INFO_DUMP
	RTW_INFO("==== DBG_LPSPG_INFO_CMD_DUMP====\n");
	RTW_INFO("  H2C_CMD: 0x%02x, H2C_LEN: %d\n", H2C_LPS_PG_INFO, H2C_LPS_PG_INFO_LEN);
	RTW_INFO("  %02X:%02X\n", *(lpspg_info), *(lpspg_info + 1));
	RTW_INFO("==== DBG_LPSPG_INFO_CMD_DUMP====\n");
#endif

	ret = rtw_hal_fill_h2c_cmd(adapter,
				   H2C_LPS_PG_INFO,
				   H2C_LPS_PG_INFO_LEN,
				   lpspg_info);
	return ret;
}
u8 rtw_hal_set_lps_pg_info(_adapter *adapter)
{
	u8 ret = _FAIL;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);

	if (pwrpriv->lpspg_rsvd_page_locate == 0) {
		RTW_ERR("%s [ERROR] lpspg_rsvd_page_locate = 0\n", __func__);
		rtw_warn_on(1);
		return ret;
	}

	rtw_hal_set_lps_pg_info_rsvd_page(adapter);
	ret = rtw_hal_set_lps_pg_info_cmd(adapter);
	if (_SUCCESS == ret)
		pwrpriv->blpspg_info_up = _FALSE;

	return ret;
}

void rtw_hal_lps_pg_handler(_adapter *adapter, enum lps_pg_hdl_id hdl_id)
{
	switch (hdl_id) {
	case LPS_PG_INFO_CFG:
		rtw_hal_set_lps_pg_info(adapter);
		break;
	case LPS_PG_REDLEMEM:
		/*rtw_halmac_redl_fw();*/
		break;

	case LPS_PG_RESEND_H2C:
		{
			struct macid_ctl_t *macid_ctl = &adapter->dvobj->macid_ctl;
			struct sta_info *sta;
			PHAL_DATA_TYPE hal_data = GET_HAL_DATA(adapter);
			int i;

			for (i = 0; i < MACID_NUM_SW_LIMIT; i++) {
				sta = macid_ctl->sta[i];
				if (sta && !is_broadcast_mac_addr(sta->hwaddr)) {
					/*rtw_dm_ra_mask_hdl(adapter, sta);*/
					/*phydm_rssi_report(hal_data->odmpriv, sta->mac_id);*/
				}
			}
		}
		break;

	default:
		break;
	}
}

#endif /*CONFIG_LPS_PG*/

/*
 * Description: Fill the reserved packets that FW will use to RSVD page.
 *			Now we just send 4 types packet to rsvd page.
 *			(1)Beacon, (2)Ps-poll, (3)Null data, (4)ProbeRsp.
 * Input:
 * finished - FALSE:At the first time we will send all the packets as a large packet to Hw,
 *		    so we need to set the packet length to total lengh.
 *	      TRUE: At the second time, we should send the first packet (default:beacon)
 *		    to Hw again and set the lengh in descriptor to the real beacon lengh.
 * 2009.10.15 by tynli.
 *
 * Page Size = 128: 8188e, 8723a/b, 8192c/d,
 * Page Size = 256: 8192e, 8821a
 * Page Size = 512: 8812a
 */

/*#define DBG_DUMP_SET_RSVD_PAGE*/
void rtw_hal_set_fw_rsvd_page(_adapter *adapter, bool finished)
{
	PHAL_DATA_TYPE pHalData;
	struct xmit_frame	*pcmdframe;
	struct pkt_attrib	*pattrib;
	struct xmit_priv	*pxmitpriv;
	struct mlme_ext_priv	*pmlmeext;
	struct mlme_ext_info	*pmlmeinfo;
	struct pwrctrl_priv *pwrctl;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct hal_ops *pHalFunc = &adapter->hal_func;
	u32	BeaconLength = 0, ProbeRspLength = 0, PSPollLength = 0;
	u32	NullDataLength = 0, QosNullLength = 0, BTQosNullLength = 0;
	u32	ProbeReqLength = 0, NullFunctionDataLength = 0;
	u8	TxDescLen = TXDESC_SIZE, TxDescOffset = TXDESC_OFFSET;
	u8	TotalPageNum = 0 , CurtPktPageNum = 0 , RsvdPageNum = 0;
	u8	*ReservedPagePacket;
	u16	BufIndex = 0;
	u32	TotalPacketLen = 0, MaxRsvdPageBufSize = 0, PageSize = 0;
	RSVDPAGE_LOC	RsvdPageLoc;

#ifdef DBG_CONFIG_ERROR_DETECT
	struct sreset_priv *psrtpriv;
#endif /* DBG_CONFIG_ERROR_DETECT */

#ifdef CONFIG_MCC_MODE
	u8 dl_mcc_page = _FAIL;
#endif /* CONFIG_MCC_MODE */

	pHalData = GET_HAL_DATA(adapter);
#ifdef DBG_CONFIG_ERROR_DETECT
	psrtpriv = &pHalData->srestpriv;
#endif
	pxmitpriv = &adapter->xmitpriv;
	pmlmeext = &adapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;
	pwrctl = adapter_to_pwrctl(adapter);

	rtw_hal_get_def_var(adapter, HAL_DEF_TX_PAGE_SIZE, (u8 *)&PageSize);

	if (PageSize == 0) {
		RTW_INFO("[Error]: %s, PageSize is zero!!\n", __func__);
		return;
	}

	if (pwrctl->wowlan_mode == _TRUE || pwrctl->wowlan_ap_mode == _TRUE)
		RsvdPageNum = rtw_hal_get_txbuff_rsvd_page_num(adapter, _TRUE);
	else
		RsvdPageNum = rtw_hal_get_txbuff_rsvd_page_num(adapter, _FALSE);

	RTW_INFO("%s PageSize: %d, RsvdPageNUm: %d\n", __func__, PageSize, RsvdPageNum);

	MaxRsvdPageBufSize = RsvdPageNum * PageSize;

	if (MaxRsvdPageBufSize > MAX_CMDBUF_SZ) {
		RTW_INFO("%s MaxRsvdPageBufSize(%d) is larger than MAX_CMDBUF_SZ(%d)",
			 __func__, MaxRsvdPageBufSize, MAX_CMDBUF_SZ);
		rtw_warn_on(1);
		return;
	}

	pcmdframe = rtw_alloc_cmdxmitframe(pxmitpriv);

	if (pcmdframe == NULL) {
		RTW_INFO("%s: alloc ReservedPagePacket fail!\n", __FUNCTION__);
		return;
	}

	ReservedPagePacket = pcmdframe->buf_addr;
	_rtw_memset(&RsvdPageLoc, 0, sizeof(RSVDPAGE_LOC));

	/* beacon * 2 pages */
	BufIndex = TxDescOffset;
	rtw_hal_construct_beacon(adapter,
				 &ReservedPagePacket[BufIndex], &BeaconLength);

	/*
	* When we count the first page size, we need to reserve description size for the RSVD
	* packet, it will be filled in front of the packet in TXPKTBUF.
	*/
	CurtPktPageNum = (u8)PageNum((TxDescLen + BeaconLength), PageSize);
	/* If we don't add 1 more page, ARP offload function will fail at 8723bs.*/
	if (CurtPktPageNum == 1)
		CurtPktPageNum += 1;

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum * PageSize);

	if (pwrctl->wowlan_ap_mode == _TRUE) {
		/* (4) probe response*/
		RsvdPageLoc.LocProbeRsp = TotalPageNum;
		rtw_hal_construct_ProbeRsp(
			adapter, &ReservedPagePacket[BufIndex],
			&ProbeRspLength,
			get_my_bssid(&pmlmeinfo->network), _FALSE);
		rtw_hal_fill_fake_txdesc(adapter,
				 &ReservedPagePacket[BufIndex - TxDescLen],
				 ProbeRspLength, _FALSE, _FALSE, _FALSE);

		CurtPktPageNum = (u8)PageNum(TxDescLen + ProbeRspLength, PageSize);
		TotalPageNum += CurtPktPageNum;
		TotalPacketLen = BufIndex + ProbeRspLength;
		BufIndex += (CurtPktPageNum * PageSize);
		goto download_page;
	}

	/* ps-poll * 1 page */
	RsvdPageLoc.LocPsPoll = TotalPageNum;
	RTW_INFO("LocPsPoll: %d\n", RsvdPageLoc.LocPsPoll);
	rtw_hal_construct_PSPoll(adapter,
				 &ReservedPagePacket[BufIndex], &PSPollLength);
	rtw_hal_fill_fake_txdesc(adapter,
				 &ReservedPagePacket[BufIndex - TxDescLen],
				 PSPollLength, _TRUE, _FALSE, _FALSE);

	CurtPktPageNum = (u8)PageNum((TxDescLen + PSPollLength), PageSize);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum * PageSize);

#ifdef CONFIG_BT_COEXIST
	/* BT Qos null data * 1 page */
	RsvdPageLoc.LocBTQosNull = TotalPageNum;
	RTW_INFO("LocBTQosNull: %d\n", RsvdPageLoc.LocBTQosNull);
	rtw_hal_construct_NullFunctionData(
		adapter,
		&ReservedPagePacket[BufIndex],
		&BTQosNullLength,
		get_my_bssid(&pmlmeinfo->network),
		_TRUE, 0, 0, _FALSE);
	rtw_hal_fill_fake_txdesc(adapter,
				 &ReservedPagePacket[BufIndex - TxDescLen],
				 BTQosNullLength, _FALSE, _TRUE, _FALSE);

	CurtPktPageNum = (u8)PageNum(TxDescLen + BTQosNullLength, PageSize);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum * PageSize);
#endif /* CONFIG_BT_COEXIT */

#ifdef CONFIG_MCC_MODE
	if (MCC_EN(adapter)) {
		dl_mcc_page = rtw_hal_dl_mcc_fw_rsvd_page(adapter, ReservedPagePacket,
				&BufIndex, TxDescLen, PageSize,
				&TotalPageNum, &TotalPacketLen, &RsvdPageLoc);
	} else
		dl_mcc_page = _FAIL;

	if (dl_mcc_page == _FAIL) {
#endif /* CONFIG_MCC_MODE */

		/* null data * 1 page */
		RsvdPageLoc.LocNullData = TotalPageNum;
		RTW_INFO("LocNullData: %d\n", RsvdPageLoc.LocNullData);
		rtw_hal_construct_NullFunctionData(
			adapter,
			&ReservedPagePacket[BufIndex],
			&NullDataLength,
			get_my_bssid(&pmlmeinfo->network),
			_FALSE, 0, 0, _FALSE);
		rtw_hal_fill_fake_txdesc(adapter,
				 &ReservedPagePacket[BufIndex - TxDescLen],
				 NullDataLength, _FALSE, _FALSE, _FALSE);

		CurtPktPageNum = (u8)PageNum(TxDescLen + NullDataLength, PageSize);

		TotalPageNum += CurtPktPageNum;

		BufIndex += (CurtPktPageNum * PageSize);
#ifdef CONFIG_MCC_MODE
	}
#endif /* CONFIG_MCC_MODE */

	/* Qos null data * 1 page */
	RsvdPageLoc.LocQosNull = TotalPageNum;
	RTW_INFO("LocQosNull: %d\n", RsvdPageLoc.LocQosNull);
	rtw_hal_construct_NullFunctionData(
		adapter,
		&ReservedPagePacket[BufIndex],
		&QosNullLength,
		get_my_bssid(&pmlmeinfo->network),
		_TRUE, 0, 0, _FALSE);
	rtw_hal_fill_fake_txdesc(adapter,
				 &ReservedPagePacket[BufIndex - TxDescLen],
				 QosNullLength, _FALSE, _FALSE, _FALSE);

	CurtPktPageNum = (u8)PageNum(TxDescLen + QosNullLength, PageSize);

	TotalPageNum += CurtPktPageNum;

	TotalPacketLen = BufIndex + QosNullLength;

	BufIndex += (CurtPktPageNum * PageSize);

#ifdef CONFIG_WOWLAN
	if (pwrctl->wowlan_mode == _TRUE &&
		pwrctl->wowlan_in_resume == _FALSE) {
		rtw_hal_set_wow_fw_rsvd_page(adapter, ReservedPagePacket,
					     BufIndex, TxDescLen, PageSize,
			     &TotalPageNum, &TotalPacketLen, &RsvdPageLoc);
	}
#endif /* CONFIG_WOWLAN */

#ifdef CONFIG_P2P_WOWLAN
	if (_TRUE == pwrctl->wowlan_p2p_mode) {
		rtw_hal_set_p2p_wow_fw_rsvd_page(adapter, ReservedPagePacket,
						 BufIndex, TxDescLen, PageSize,
				 &TotalPageNum, &TotalPacketLen, &RsvdPageLoc);
	}
#endif /* CONFIG_P2P_WOWLAN */

#ifdef CONFIG_LPS_PG
	/* must reserved last 1 x page for LPS PG Info*/
	pwrctl->lpspg_rsvd_page_locate = TotalPageNum;
	pwrctl->blpspg_info_up = _TRUE;
#endif

download_page:
	/* RTW_INFO("%s BufIndex(%d), TxDescLen(%d), PageSize(%d)\n",__func__, BufIndex, TxDescLen, PageSize);*/
	RTW_INFO("%s PageNum(%d), pktlen(%d)\n",
		 __func__, TotalPageNum, TotalPacketLen);

#ifdef CONFIG_LPS_PG
	if ((TotalPacketLen + (LPS_PG_INFO_RSVD_PAGE_NUM * PageSize)) > MaxRsvdPageBufSize) {
		pwrctl->lpspg_rsvd_page_locate = 0;
		pwrctl->blpspg_info_up = _FALSE;

		RTW_ERR("%s rsvd page size is not enough!!TotalPacketLen+LPS_PG_INFO_LEN %d, MaxRsvdPageBufSize %d\n",
			 __func__, (TotalPacketLen + (LPS_PG_INFO_RSVD_PAGE_NUM * PageSize)), MaxRsvdPageBufSize);
		rtw_warn_on(1);
	}
#endif

	if (TotalPacketLen > MaxRsvdPageBufSize) {
		RTW_ERR("%s(ERROR): rsvd page size is not enough!!TotalPacketLen %d, MaxRsvdPageBufSize %d\n",
			 __FUNCTION__, TotalPacketLen, MaxRsvdPageBufSize);
		rtw_warn_on(1);
		goto error;
	} else {
		/* update attribute */
		pattrib = &pcmdframe->attrib;
		update_mgntframe_attrib(adapter, pattrib);
		pattrib->qsel = QSLT_BEACON;
		pattrib->pktlen = TotalPacketLen - TxDescOffset;
		pattrib->last_txcmdsz = TotalPacketLen - TxDescOffset;
#ifdef CONFIG_PCI_HCI
		dump_mgntframe(adapter, pcmdframe);
#else
		dump_mgntframe_and_wait(adapter, pcmdframe, 100);
#endif
	}

	RTW_INFO("%s: Set RSVD page location to Fw ,TotalPacketLen(%d), TotalPageNum(%d)\n",
		 __func__, TotalPacketLen, TotalPageNum);
#ifdef DBG_DUMP_SET_RSVD_PAGE
	RTW_INFO(" ==================================================\n");
	RTW_INFO_DUMP("\n", ReservedPagePacket, TotalPacketLen);
	RTW_INFO(" ==================================================\n");
#endif
	if (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) {
		rtw_hal_set_FwRsvdPage_cmd(adapter, &RsvdPageLoc);
#ifdef CONFIG_WOWLAN
		if (pwrctl->wowlan_mode == _TRUE &&
			pwrctl->wowlan_in_resume == _FALSE)
			rtw_hal_set_FwAoacRsvdPage_cmd(adapter, &RsvdPageLoc);
#endif /* CONFIG_WOWLAN */
#ifdef CONFIG_AP_WOWLAN
		if (pwrctl->wowlan_ap_mode == _TRUE)
			rtw_hal_set_ap_rsvdpage_loc_cmd(adapter, &RsvdPageLoc);
#endif /* CONFIG_AP_WOWLAN */
	} else if (pwrctl->wowlan_pno_enable) {
#ifdef CONFIG_PNO_SUPPORT
		rtw_hal_set_FwAoacRsvdPage_cmd(adapter, &RsvdPageLoc);
		if (pwrctl->wowlan_in_resume)
			rtw_hal_set_scan_offload_info_cmd(adapter,
							  &RsvdPageLoc, 0);
		else
			rtw_hal_set_scan_offload_info_cmd(adapter,
							  &RsvdPageLoc, 1);
#endif /* CONFIG_PNO_SUPPORT */
	}
#ifdef CONFIG_P2P_WOWLAN
	if (_TRUE == pwrctl->wowlan_p2p_mode)
		rtw_hal_set_FwP2PRsvdPage_cmd(adapter, &RsvdPageLoc);
#endif /* CONFIG_P2P_WOWLAN */
	return;
error:
	rtw_free_xmitframe(pxmitpriv, pcmdframe);
}
static void rtw_hal_set_hw_update_tsf(PADAPTER padapter)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

#if defined(CONFIG_RTL8822B) || defined(CONFIG_MI_WITH_MBSSID_CAM)
	RTW_INFO("[Warn] %s "ADPT_FMT" enter func\n", __func__, ADPT_ARG(padapter));
	rtw_warn_on(1);
	return;
#endif

	if (!pmlmeext->en_hw_update_tsf)
		return;

	/* check REG_RCR bit is set */
	if (!(rtw_read32(padapter, REG_RCR) & RCR_CBSSID_BCN))
		return;

	/* enable hw update tsf function for non-AP */
	if (rtw_linked_check(padapter) &&
	    check_fwstate(pmlmepriv, WIFI_AP_STATE) != _TRUE) {
#ifdef CONFIG_CONCURRENT_MODE
		if (padapter->hw_port == HW_PORT1)
			rtw_write8(padapter, REG_BCN_CTRL_1, rtw_read8(padapter, REG_BCN_CTRL_1) & (~DIS_TSF_UDT));
		else
#endif
			rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL) & (~DIS_TSF_UDT));
	}
	pmlmeext->en_hw_update_tsf = _FALSE;
}

#ifdef CONFIG_TDLS
#ifdef CONFIG_TDLS_CH_SW
s32 rtw_hal_ch_sw_oper_offload(_adapter *padapter, u8 channel, u8 channel_offset, u16 bwmode)
{
	PHAL_DATA_TYPE	pHalData =  GET_HAL_DATA(padapter);
	u8 ch_sw_h2c_buf[4] = {0x00, 0x00, 0x00, 0x00};


	SET_H2CCMD_CH_SW_OPER_OFFLOAD_CH_NUM(ch_sw_h2c_buf, channel);
	SET_H2CCMD_CH_SW_OPER_OFFLOAD_BW_MODE(ch_sw_h2c_buf, bwmode);
	switch (bwmode) {
	case CHANNEL_WIDTH_40:
		SET_H2CCMD_CH_SW_OPER_OFFLOAD_BW_40M_SC(ch_sw_h2c_buf, channel_offset);
		break;
	case CHANNEL_WIDTH_80:
		SET_H2CCMD_CH_SW_OPER_OFFLOAD_BW_80M_SC(ch_sw_h2c_buf, channel_offset);
		break;
	case CHANNEL_WIDTH_20:
	default:
		break;
	}
	SET_H2CCMD_CH_SW_OPER_OFFLOAD_RFE_TYPE(ch_sw_h2c_buf, pHalData->rfe_type);

	return rtw_hal_fill_h2c_cmd(padapter, H2C_CHNL_SWITCH_OPER_OFFLOAD, sizeof(ch_sw_h2c_buf), ch_sw_h2c_buf);
}
#endif
#endif

#ifdef CONFIG_WMMPS
void rtw_hal_update_uapsd_tid(_adapter *adapter)
{
	rtw_write8(adapter, REG_WMMPS_UAPSD_TID, 0xFF);
}
#endif

#if defined(CONFIG_BT_COEXIST) && defined(CONFIG_FW_MULTI_PORT_SUPPORT)
/* For multi-port support, driver needs to inform the port ID to FW for btc operations */
s32 rtw_hal_set_wifi_port_id_cmd(_adapter *adapter)
{
	u8 port_id = 0;
	u8 h2c_buf[H2C_BTC_WL_PORT_ID_LEN] = {0};

	SET_H2CCMD_BTC_WL_PORT_ID(h2c_buf, adapter->hw_port);
	return rtw_hal_fill_h2c_cmd(adapter, H2C_BTC_WL_PORT_ID, H2C_BTC_WL_PORT_ID_LEN, h2c_buf);
}
#endif

void SetHwReg(_adapter *adapter, u8 variable, u8 *val)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);

	switch (variable) {
	case HW_VAR_MEDIA_STATUS: {
		u8 net_type = *((u8 *)val);

		rtw_hal_set_msr(adapter, net_type);
	}
	break;
	case HW_VAR_MAC_ADDR:
#ifdef CONFIG_MI_WITH_MBSSID_CAM
		rtw_hal_set_macaddr_mbid(adapter, val);
#else
		rtw_hal_set_macaddr_port(adapter, val);
#endif
		break;
	case HW_VAR_BSSID:
		rtw_hal_set_bssid(adapter, val);
		break;
#ifdef CONFIG_MBSSID_CAM
	case HW_VAR_MBSSID_CAM_WRITE: {
		u32	cmd = 0;
		u32	*cam_val = (u32 *)val;

		rtw_write32(adapter, REG_MBIDCAMCFG_1, cam_val[0]);
		cmd = BIT_MBIDCAM_POLL | BIT_MBIDCAM_WT_EN | BIT_MBIDCAM_VALID | cam_val[1];
		rtw_write32(adapter, REG_MBIDCAMCFG_2, cmd);
	}
		break;
	case HW_VAR_MBSSID_CAM_CLEAR: {
		u32 cmd;
		u8 entry_id = *(u8 *)val;

		rtw_write32(adapter, REG_MBIDCAMCFG_1, 0);

		cmd = BIT_MBIDCAM_POLL | BIT_MBIDCAM_WT_EN | ((entry_id & MBIDCAM_ADDR_MASK) << MBIDCAM_ADDR_SHIFT);
		rtw_write32(adapter, REG_MBIDCAMCFG_2, cmd);
	}
		break;
	case HW_VAR_RCR_MBSSID_EN:
		if (*((u8 *)val))
			rtw_write32(adapter, REG_RCR, rtw_read32(adapter, REG_RCR) | RCR_ENMBID);
		else {
			u32	val32;

			val32 = rtw_read32(adapter, REG_RCR);
			val32 &= ~(RCR_ENMBID);
			rtw_write32(adapter, REG_RCR, val32);
		}
		break;
#endif
	case HW_VAR_PORT_SWITCH:
		hw_var_port_switch(adapter);
		break;
	case HW_VAR_INIT_RTS_RATE: {
		u16 brate_cfg = *((u16 *)val);
		u8 rate_index = 0;
		HAL_VERSION *hal_ver = &hal_data->version_id;

		if (IS_8188E(*hal_ver)) {

			while (brate_cfg > 0x1) {
				brate_cfg = (brate_cfg >> 1);
				rate_index++;
			}
			rtw_write8(adapter, REG_INIRTS_RATE_SEL, rate_index);
		} else
			rtw_warn_on(1);
	}
		break;
	case HW_VAR_SEC_CFG: {
		u16 reg_scr_ori;
		u16 reg_scr;

		reg_scr = reg_scr_ori = rtw_read16(adapter, REG_SECCFG);
		reg_scr |= (SCR_CHK_KEYID | SCR_RxDecEnable | SCR_TxEncEnable);

		if (_rtw_camctl_chk_cap(adapter, SEC_CAP_CHK_BMC))
			reg_scr |= SCR_CHK_BMC;

		if (_rtw_camctl_chk_flags(adapter, SEC_STATUS_STA_PK_GK_CONFLICT_DIS_BMC_SEARCH))
			reg_scr |= SCR_NoSKMC;

		if (reg_scr != reg_scr_ori)
			rtw_write16(adapter, REG_SECCFG, reg_scr);
	}
		break;
	case HW_VAR_SEC_DK_CFG: {
		struct security_priv *sec = &adapter->securitypriv;
		u8 reg_scr = rtw_read8(adapter, REG_SECCFG);

		if (val) { /* Enable default key related setting */
			reg_scr |= SCR_TXBCUSEDK;
			if (sec->dot11AuthAlgrthm != dot11AuthAlgrthm_8021X)
				reg_scr |= (SCR_RxUseDK | SCR_TxUseDK);
		} else /* Disable default key related setting */
			reg_scr &= ~(SCR_RXBCUSEDK | SCR_TXBCUSEDK | SCR_RxUseDK | SCR_TxUseDK);

		rtw_write8(adapter, REG_SECCFG, reg_scr);
	}
		break;

	case HW_VAR_ASIX_IOT:
		/* enable  ASIX IOT function */
		if (*((u8 *)val) == _TRUE) {
			/* 0xa2e[0]=0 (disable rake receiver) */
			rtw_write8(adapter, rCCK0_FalseAlarmReport + 2,
				rtw_read8(adapter, rCCK0_FalseAlarmReport + 2) & ~(BIT0));
			/* 0xa1c=0xa0 (reset channel estimation if signal quality is bad) */
			rtw_write8(adapter, rCCK0_DSPParameter2, 0xa0);
		} else {
			/* restore reg:0xa2e,   reg:0xa1c */
			rtw_write8(adapter, rCCK0_FalseAlarmReport + 2,
				rtw_read8(adapter, rCCK0_FalseAlarmReport + 2) | (BIT0));
			rtw_write8(adapter, rCCK0_DSPParameter2, 0x00);
		}
		break;
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	case HW_VAR_WOWLAN: {
		struct wowlan_ioctl_param *poidparam;

		poidparam = (struct wowlan_ioctl_param *)val;
		switch (poidparam->subcode) {
#ifdef CONFIG_WOWLAN
		case WOWLAN_PATTERN_CLEAN:
			rtw_hal_dl_pattern(adapter, 2);
			break;
		case WOWLAN_ENABLE:
			rtw_hal_wow_enable(adapter);
			break;
		case WOWLAN_DISABLE:
			rtw_hal_wow_disable(adapter);
			break;
#endif /*CONFIG_WOWLAN*/
#ifdef CONFIG_AP_WOWLAN
		case WOWLAN_AP_ENABLE:
			rtw_hal_ap_wow_enable(adapter);
			break;
		case WOWLAN_AP_DISABLE:
			rtw_hal_ap_wow_disable(adapter);
			break;
#endif /*CONFIG_AP_WOWLAN*/
		default:
			break;
		}
	}
		break;
#endif /*defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)*/

	case HW_VAR_EN_HW_UPDATE_TSF:
		rtw_hal_set_hw_update_tsf(adapter);
		break;

	case HW_VAR_APFM_ON_MAC:
		hal_data->bMacPwrCtrlOn = *val;
		RTW_INFO("%s: bMacPwrCtrlOn=%d\n", __func__, hal_data->bMacPwrCtrlOn);
		break;
#ifdef CONFIG_WMMPS
	case  HW_VAR_UAPSD_TID:
		rtw_hal_update_uapsd_tid(adapter);
		break;
#endif
#ifdef CONFIG_LPS_PG
	case HW_VAR_LPS_PG_HANDLE:
		rtw_hal_lps_pg_handler(adapter, *val);
		break;
#endif

	default:
		if (0)
			RTW_PRINT(FUNC_ADPT_FMT" variable(%d) not defined!\n",
				  FUNC_ADPT_ARG(adapter), variable);
		break;
	}

}

void GetHwReg(_adapter *adapter, u8 variable, u8 *val)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);


	switch (variable) {
	case HW_VAR_BASIC_RATE:
		*((u16 *)val) = hal_data->BasicRateSet;
		break;
	case HW_VAR_RF_TYPE:
		*((u8 *)val) = hal_data->rf_type;
		break;
	case HW_VAR_MEDIA_STATUS:
		rtw_hal_get_msr(adapter, val);
		break;
	case HW_VAR_DO_IQK:
		*val = hal_data->bNeedIQK;
		break;
	case HW_VAR_CH_SW_NEED_TO_TAKE_CARE_IQK_INFO:
		if (hal_is_band_support(adapter, BAND_ON_5G))
			*val = _TRUE;
		else
			*val = _FALSE;
		break;
	case HW_VAR_APFM_ON_MAC:
		*val = hal_data->bMacPwrCtrlOn;
		break;
	default:
		if (0)
			RTW_PRINT(FUNC_ADPT_FMT" variable(%d) not defined!\n",
				  FUNC_ADPT_ARG(adapter), variable);
		break;
	}

}

u8
SetHalDefVar(_adapter *adapter, HAL_DEF_VARIABLE variable, void *value)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	u8 bResult = _SUCCESS;

	switch (variable) {

	case HAL_DEF_DBG_DUMP_RXPKT:
		hal_data->bDumpRxPkt = *((u8 *)value);
		break;
	case HAL_DEF_DBG_DUMP_TXPKT:
		hal_data->bDumpTxPkt = *((u8 *)value);
		break;
	case HAL_DEF_ANT_DETECT:
		hal_data->AntDetection = *((u8 *)value);
		break;
	case HAL_DEF_DBG_DIS_PWT:
		hal_data->bDisableTXPowerTraining = *((u8 *)value);
		break;
	default:
		RTW_PRINT("%s: [WARNING] HAL_DEF_VARIABLE(%d) not defined!\n", __FUNCTION__, variable);
		bResult = _FAIL;
		break;
	}

	return bResult;
}

#ifdef CONFIG_BEAMFORMING
u8 rtw_hal_query_txbfer_rf_num(_adapter *adapter)
{
	struct registry_priv	*pregistrypriv = &adapter->registrypriv;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);

	if ((pregistrypriv->beamformer_rf_num) && (IS_HARDWARE_TYPE_8814AE(adapter) || IS_HARDWARE_TYPE_8814AU(adapter) || IS_HARDWARE_TYPE_8822BU(adapter) || IS_HARDWARE_TYPE_8821C(adapter)))
		return pregistrypriv->beamformer_rf_num;
	else if (IS_HARDWARE_TYPE_8814AE(adapter)
#if 0
#if defined(CONFIG_USB_HCI)
		|| (IS_HARDWARE_TYPE_8814AU(adapter) && (pUsbModeMech->CurUsbMode == 2 || pUsbModeMech->HubUsbMode == 2))  /* for USB3.0 */
#endif
#endif
		) {
		/*BF cap provided by Yu Chen, Sean, 2015, 01 */
		if (hal_data->rf_type == RF_3T3R)
			return 2;
		else if (hal_data->rf_type == RF_4T4R)
			return 3;
		else
			return 1;
	} else
		return 1;

}
u8 rtw_hal_query_txbfee_rf_num(_adapter *adapter)
{
	struct registry_priv		*pregistrypriv = &adapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);

	if ((pregistrypriv->beamformee_rf_num) && (IS_HARDWARE_TYPE_8814AE(adapter) || IS_HARDWARE_TYPE_8814AU(adapter) || IS_HARDWARE_TYPE_8822BU(adapter) || IS_HARDWARE_TYPE_8821C(adapter)))
		return pregistrypriv->beamformee_rf_num;
	else if (IS_HARDWARE_TYPE_8814AE(adapter) || IS_HARDWARE_TYPE_8814AU(adapter)) {
		if (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_BROADCOM)
			return 2;
		else
			return 2;/*TODO: May be 3 in the future, by ChenYu. */
	} else
		return 1;

}
#endif

u8
GetHalDefVar(_adapter *adapter, HAL_DEF_VARIABLE variable, void *value)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	u8 bResult = _SUCCESS;

	switch (variable) {
	case HAL_DEF_UNDERCORATEDSMOOTHEDPWDB: {
		struct mlme_priv *pmlmepriv;
		struct sta_priv *pstapriv;
		struct sta_info *psta;

		pmlmepriv = &adapter->mlmepriv;
		pstapriv = &adapter->stapriv;
		psta = rtw_get_stainfo(pstapriv, pmlmepriv->cur_network.network.MacAddress);
		if (psta)
			*((int *)value) = psta->rssi_stat.undecorated_smoothed_pwdb;
	}
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
		*(u8 *)value = _FALSE;
		break;
	case HAL_DEF_TX_PAGE_SIZE:
		*((u32 *)value) = PAGE_SIZE_128;
		break;
	case HAL_DEF_DBG_DIS_PWT:
		*(u8 *)value = hal_data->bDisableTXPowerTraining;
		break;
	case HAL_DEF_EXPLICIT_BEAMFORMER:
	case HAL_DEF_EXPLICIT_BEAMFORMEE:
	case HAL_DEF_VHT_MU_BEAMFORMER:
	case HAL_DEF_VHT_MU_BEAMFORMEE:
		*(u8 *)value = _FALSE;
		break;
#ifdef CONFIG_BEAMFORMING
	case HAL_DEF_BEAMFORMER_CAP:
		*(u8 *)value = rtw_hal_query_txbfer_rf_num(adapter);
		break;
	case HAL_DEF_BEAMFORMEE_CAP:
		*(u8 *)value = rtw_hal_query_txbfee_rf_num(adapter);
		break;
#endif
	default:
		RTW_PRINT("%s: [WARNING] HAL_DEF_VARIABLE(%d) not defined!\n", __FUNCTION__, variable);
		bResult = _FAIL;
		break;
	}

	return bResult;
}

void SetHalODMVar(
	PADAPTER				Adapter,
	HAL_ODM_VARIABLE		eVariable,
	PVOID					pValue1,
	BOOLEAN					bSet)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct PHY_DM_STRUCT *podmpriv = &pHalData->odmpriv;
	/* _irqL irqL; */
	switch (eVariable) {
	case HAL_ODM_STA_INFO: {
		struct sta_info *psta = (struct sta_info *)pValue1;
		if (bSet) {
			RTW_INFO("### Set STA_(%d) info ###\n", psta->mac_id);
			odm_cmn_info_ptr_array_hook(podmpriv, ODM_CMNINFO_STA_STATUS, psta->mac_id, psta);
		} else {
			RTW_INFO("### Clean STA_(%d) info ###\n", psta->mac_id);
			/* _enter_critical_bh(&pHalData->odm_stainfo_lock, &irqL); */
			psta->rssi_level = 0;
			odm_cmn_info_ptr_array_hook(podmpriv, ODM_CMNINFO_STA_STATUS, psta->mac_id, NULL);

			/* _exit_critical_bh(&pHalData->odm_stainfo_lock, &irqL); */
		}
	}
		break;
	case HAL_ODM_P2P_STATE:
		odm_cmn_info_update(podmpriv, ODM_CMNINFO_WIFI_DIRECT, bSet);
		break;
	case HAL_ODM_WIFI_DISPLAY_STATE:
		odm_cmn_info_update(podmpriv, ODM_CMNINFO_WIFI_DISPLAY, bSet);
		break;
	case HAL_ODM_REGULATION:
		odm_cmn_info_init(podmpriv, ODM_CMNINFO_DOMAIN_CODE_2G, pHalData->Regulation2_4G);
		odm_cmn_info_init(podmpriv, ODM_CMNINFO_DOMAIN_CODE_5G, pHalData->Regulation5G);
		break;
#if defined(CONFIG_SIGNAL_DISPLAY_DBM) && defined(CONFIG_BACKGROUND_NOISE_MONITOR)
	case HAL_ODM_NOISE_MONITOR: {
		struct noise_info *pinfo = (struct noise_info *)pValue1;

#ifdef DBG_NOISE_MONITOR
		RTW_INFO("### Noise monitor chan(%d)-bPauseDIG:%d,IGIValue:0x%02x,max_time:%d (ms) ###\n",
			pinfo->chan, pinfo->bPauseDIG, pinfo->IGIValue, pinfo->max_time);
#endif

		pHalData->noise[pinfo->chan] = odm_inband_noise_monitor(podmpriv, pinfo->is_pause_dig, pinfo->igi_value, pinfo->max_time);
		RTW_INFO("chan_%d, noise = %d (dBm)\n", pinfo->chan, pHalData->noise[pinfo->chan]);
#ifdef DBG_NOISE_MONITOR
		RTW_INFO("noise_a = %d, noise_b = %d  noise_all:%d\n",
			 podmpriv->noise_level.noise[ODM_RF_PATH_A],
			 podmpriv->noise_level.noise[ODM_RF_PATH_B],
			 podmpriv->noise_level.noise_all);
#endif
	}
		break;
#endif/*#ifdef CONFIG_BACKGROUND_NOISE_MONITOR*/

	case HAL_ODM_INITIAL_GAIN: {
		u8 rx_gain = *((u8 *)(pValue1));
		/*printk("rx_gain:%x\n",rx_gain);*/
		if (rx_gain == 0xff) {/*restore rx gain*/
			/*odm_write_dig(podmpriv,pDigTable->backup_ig_value);*/
			odm_pause_dig(podmpriv, PHYDM_RESUME, PHYDM_PAUSE_LEVEL_0, rx_gain);
		} else {
			/*pDigTable->backup_ig_value = pDigTable->cur_ig_value;*/
			/*odm_write_dig(podmpriv,rx_gain);*/
			odm_pause_dig(podmpriv, PHYDM_PAUSE, PHYDM_PAUSE_LEVEL_0, rx_gain);
		}
	}
	break;
	case HAL_ODM_FA_CNT_DUMP:
		if (*((u8 *)pValue1))
			podmpriv->debug_components |= (ODM_COMP_DIG | ODM_COMP_FA_CNT);
		else
			podmpriv->debug_components &= ~(ODM_COMP_DIG | ODM_COMP_FA_CNT);
		break;
	case HAL_ODM_DBG_FLAG:
		odm_cmn_info_update(podmpriv, ODM_CMNINFO_DBG_COMP, *((u8Byte *)pValue1));
		break;
	case HAL_ODM_DBG_LEVEL:
		odm_cmn_info_update(podmpriv, ODM_CMNINFO_DBG_LEVEL, *((u4Byte *)pValue1));
		break;
	case HAL_ODM_RX_INFO_DUMP: {
		struct _FALSE_ALARM_STATISTICS *false_alm_cnt = (struct _FALSE_ALARM_STATISTICS *)phydm_get_structure(podmpriv , PHYDM_FALSEALMCNT);
		struct _dynamic_initial_gain_threshold_	*pDM_DigTable = &podmpriv->dm_dig_table;
		void *sel;

		sel = pValue1;

		_RTW_PRINT_SEL(sel , "============ Rx Info dump ===================\n");
		_RTW_PRINT_SEL(sel , "is_linked = %d, rssi_min = %d(%%), current_igi = 0x%x\n", podmpriv->is_linked, podmpriv->rssi_min, pDM_DigTable->cur_ig_value);
		_RTW_PRINT_SEL(sel , "cnt_cck_fail = %d, cnt_ofdm_fail = %d, Total False Alarm = %d\n", false_alm_cnt->cnt_cck_fail, false_alm_cnt->cnt_ofdm_fail, false_alm_cnt->cnt_all);

		if (podmpriv->is_linked) {
			_RTW_PRINT_SEL(sel , "rx_rate = %s", HDATA_RATE(podmpriv->rx_rate));
			_RTW_PRINT_SEL(sel , " RSSI_A = %d(%%), RSSI_B = %d(%%)\n", podmpriv->RSSI_A, podmpriv->RSSI_B);
#ifdef DBG_RX_SIGNAL_DISPLAY_RAW_DATA
			rtw_dump_raw_rssi_info(Adapter, sel);
#endif
		}
	}
		break;
	case HAL_ODM_RX_Dframe_INFO: {
		void *sel;

		sel = pValue1;

		/*_RTW_PRINT_SEL(sel , "HAL_ODM_RX_Dframe_INFO\n");*/
#ifdef DBG_RX_DFRAME_RAW_DATA
		rtw_dump_rx_dframe_info(Adapter, sel);
#endif
	}
		break;

#ifdef CONFIG_AUTO_CHNL_SEL_NHM
	case HAL_ODM_AUTO_CHNL_SEL: {
		ACS_OP	acs_op = *(ACS_OP *)pValue1;

		rtw_phydm_func_set(Adapter, ODM_BB_NHM_CNT);

		if (ACS_INIT == acs_op) {
#ifdef DBG_AUTO_CHNL_SEL_NHM
			RTW_INFO("[ACS-"ADPT_FMT"] HAL_ODM_AUTO_CHNL_SEL: ACS_INIT\n", ADPT_ARG(Adapter));
#endif
			odm_AutoChannelSelectInit(podmpriv);
		} else if (ACS_RESET == acs_op) {
			/* Reset statistics for auto channel selection mechanism.*/
#ifdef DBG_AUTO_CHNL_SEL_NHM
			RTW_INFO("[ACS-"ADPT_FMT"] HAL_ODM_AUTO_CHNL_SEL: ACS_RESET\n", ADPT_ARG(Adapter));
#endif
			odm_auto_channel_select_reset(podmpriv);

		} else if (ACS_SELECT == acs_op) {
			/* Collect NHM measurement result after current channel */
#ifdef DBG_AUTO_CHNL_SEL_NHM
			RTW_INFO("[ACS-"ADPT_FMT"] HAL_ODM_AUTO_CHNL_SEL: ACS_SELECT, CH(%d)\n", ADPT_ARG(Adapter), rtw_get_acs_channel(Adapter));
#endif
			odm_AutoChannelSelect(podmpriv, rtw_get_acs_channel(Adapter));
		} else
			RTW_INFO("[ACS-"ADPT_FMT"] HAL_ODM_AUTO_CHNL_SEL: Unexpected OP\n", ADPT_ARG(Adapter));

	}
		break;
#endif
#ifdef CONFIG_ANTENNA_DIVERSITY
	case HAL_ODM_ANTDIV_SELECT: {
		u8	antenna = (*(u8 *)pValue1);

		/*switch antenna*/
		odm_update_rx_idle_ant(&pHalData->odmpriv, antenna);
		/*RTW_INFO("==> HAL_ODM_ANTDIV_SELECT, Ant_(%s)\n", (antenna == MAIN_ANT) ? "MAIN_ANT" : "AUX_ANT");*/

	}
		break;
#endif

	default:
		break;
	}
}

void GetHalODMVar(
	PADAPTER				Adapter,
	HAL_ODM_VARIABLE		eVariable,
	PVOID					pValue1,
	PVOID					pValue2)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct PHY_DM_STRUCT *podmpriv = &pHalData->odmpriv;

	switch (eVariable) {
#if defined(CONFIG_SIGNAL_DISPLAY_DBM) && defined(CONFIG_BACKGROUND_NOISE_MONITOR)
	case HAL_ODM_NOISE_MONITOR: {
		u8 chan = *(u8 *)pValue1;
		*(s16 *)pValue2 = pHalData->noise[chan];
#ifdef DBG_NOISE_MONITOR
		RTW_INFO("### Noise monitor chan(%d)-noise:%d (dBm) ###\n",
			 chan, pHalData->noise[chan]);
#endif
	}
		break;
#endif/*#ifdef CONFIG_BACKGROUND_NOISE_MONITOR*/
	case HAL_ODM_DBG_FLAG:
		*((u8Byte *)pValue1) = podmpriv->debug_components;
		break;
	case HAL_ODM_DBG_LEVEL:
		*((u4Byte *)pValue1) = podmpriv->debug_level;
		break;

#ifdef CONFIG_AUTO_CHNL_SEL_NHM
	case HAL_ODM_AUTO_CHNL_SEL: {
#ifdef DBG_AUTO_CHNL_SEL_NHM
		RTW_INFO("[ACS-"ADPT_FMT"] HAL_ODM_AUTO_CHNL_SEL: GET_BEST_CHAN\n", ADPT_ARG(Adapter));
#endif
		/* Retrieve better channel from NHM mechanism	*/
		if (IsSupported24G(Adapter->registrypriv.wireless_mode))
			*((u8 *)(pValue1)) = odm_get_auto_channel_select_result(podmpriv, BAND_ON_2_4G);
		if (is_supported_5g(Adapter->registrypriv.wireless_mode))
			*((u8 *)(pValue2)) = odm_get_auto_channel_select_result(podmpriv, BAND_ON_5G);
	}
		break;
#endif
#ifdef CONFIG_ANTENNA_DIVERSITY
	case HAL_ODM_ANTDIV_SELECT: {
		struct _FAST_ANTENNA_TRAINNING_	*pDM_FatTable = &podmpriv->dm_fat_table;
		*((u8 *)pValue1) = pDM_FatTable->rx_idle_ant;
	}
		break;
#endif
	case HAL_ODM_INITIAL_GAIN: {
		struct _dynamic_initial_gain_threshold_ *pDM_DigTable = &podmpriv->dm_dig_table;
		*((u8 *)pValue1) = pDM_DigTable->cur_ig_value;
	}
		break;
	default:
		break;
	}
}


u32 rtw_phydm_ability_ops(_adapter *adapter, HAL_PHYDM_OPS ops, u32 ability)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT *podmpriv = &pHalData->odmpriv;
	u32 result = 0;

	switch (ops) {
	case HAL_PHYDM_DIS_ALL_FUNC:
		podmpriv->support_ability = DYNAMIC_FUNC_DISABLE;
		break;
	case HAL_PHYDM_FUNC_SET:
		podmpriv->support_ability |= ability;
		break;
	case HAL_PHYDM_FUNC_CLR:
		podmpriv->support_ability &= ~(ability);
		break;
	case HAL_PHYDM_ABILITY_BK:
		/* dm flag backup*/
		podmpriv->bk_support_ability = podmpriv->support_ability;
		break;
	case HAL_PHYDM_ABILITY_RESTORE:
		/* restore dm flag */
		podmpriv->support_ability = podmpriv->bk_support_ability;
		break;
	case HAL_PHYDM_ABILITY_SET:
		podmpriv->support_ability = ability;
		break;
	case HAL_PHYDM_ABILITY_GET:
		result = podmpriv->support_ability;
		break;
	}
	return result;
}


BOOLEAN
eqNByte(
	u8	*str1,
	u8	*str2,
	u32	num
)
{
	if (num == 0)
		return _FALSE;
	while (num > 0) {
		num--;
		if (str1[num] != str2[num])
			return _FALSE;
	}
	return _TRUE;
}

/*
 *	Description:
 *		Translate a character to hex digit.
 *   */
u32
MapCharToHexDigit(
	IN		char		chTmp
)
{
	if (chTmp >= '0' && chTmp <= '9')
		return chTmp - '0';
	else if (chTmp >= 'a' && chTmp <= 'f')
		return 10 + (chTmp - 'a');
	else if (chTmp >= 'A' && chTmp <= 'F')
		return 10 + (chTmp - 'A');
	else
		return 0;
}



/*
 *	Description:
 *		Parse hex number from the string pucStr.
 *   */
BOOLEAN
GetHexValueFromString(
	IN		char			*szStr,
	IN OUT	u32			*pu4bVal,
	IN OUT	u32			*pu4bMove
)
{
	char		*szScan = szStr;

	/* Check input parameter. */
	if (szStr == NULL || pu4bVal == NULL || pu4bMove == NULL) {
		RTW_INFO("GetHexValueFromString(): Invalid inpur argumetns! szStr: %p, pu4bVal: %p, pu4bMove: %p\n", szStr, pu4bVal, pu4bMove);
		return _FALSE;
	}

	/* Initialize output. */
	*pu4bMove = 0;
	*pu4bVal = 0;

	/* Skip leading space. */
	while (*szScan != '\0' &&
	       (*szScan == ' ' || *szScan == '\t')) {
		szScan++;
		(*pu4bMove)++;
	}

	/* Skip leading '0x' or '0X'. */
	if (*szScan == '0' && (*(szScan + 1) == 'x' || *(szScan + 1) == 'X')) {
		szScan += 2;
		(*pu4bMove) += 2;
	}

	/* Check if szScan is now pointer to a character for hex digit, */
	/* if not, it means this is not a valid hex number. */
	if (!IsHexDigit(*szScan))
		return _FALSE;

	/* Parse each digit. */
	do {
		(*pu4bVal) <<= 4;
		*pu4bVal += MapCharToHexDigit(*szScan);

		szScan++;
		(*pu4bMove)++;
	} while (IsHexDigit(*szScan));

	return _TRUE;
}

BOOLEAN
GetFractionValueFromString(
	IN		char			*szStr,
	IN OUT	u8				*pInteger,
	IN OUT	u8				*pFraction,
	IN OUT	u32			*pu4bMove
)
{
	char	*szScan = szStr;

	/* Initialize output. */
	*pu4bMove = 0;
	*pInteger = 0;
	*pFraction = 0;

	/* Skip leading space. */
	while (*szScan != '\0' &&	(*szScan == ' ' || *szScan == '\t')) {
		++szScan;
		++(*pu4bMove);
	}

	/* Parse each digit. */
	do {
		(*pInteger) *= 10;
		*pInteger += (*szScan - '0');

		++szScan;
		++(*pu4bMove);

		if (*szScan == '.') {
			++szScan;
			++(*pu4bMove);

			if (*szScan < '0' || *szScan > '9')
				return _FALSE;
			else {
				*pFraction = *szScan - '0';
				++szScan;
				++(*pu4bMove);
				return _TRUE;
			}
		}
	} while (*szScan >= '0' && *szScan <= '9');

	return _TRUE;
}

/*
 *	Description:
 * Return TRUE if szStr is comment out with leading " */ /* ".
 *   */
BOOLEAN
IsCommentString(
	IN		char			*szStr
)
{
	if (*szStr == '/' && *(szStr + 1) == '/')
		return _TRUE;
	else
		return _FALSE;
}

BOOLEAN
GetU1ByteIntegerFromStringInDecimal(
	IN		char	*Str,
	IN OUT	u8		*pInt
)
{
	u16 i = 0;
	*pInt = 0;

	while (Str[i] != '\0') {
		if (Str[i] >= '0' && Str[i] <= '9') {
			*pInt *= 10;
			*pInt += (Str[i] - '0');
		} else
			return _FALSE;
		++i;
	}

	return _TRUE;
}

/* <20121004, Kordan> For example,
 * ParseQualifiedString(inString, 0, outString, '[', ']') gets "Kordan" from a string "Hello [Kordan]".
 * If RightQualifier does not exist, it will hang on in the while loop */
BOOLEAN
ParseQualifiedString(
	IN		char	*In,
	IN OUT	u32	*Start,
	OUT		char	*Out,
	IN		char		LeftQualifier,
	IN		char		RightQualifier
)
{
	u32	i = 0, j = 0;
	char	c = In[(*Start)++];

	if (c != LeftQualifier)
		return _FALSE;

	i = (*Start);
	while ((c = In[(*Start)++]) != RightQualifier)
		; /* find ']' */
	j = (*Start) - 2;
	strncpy((char *)Out, (const char *)(In + i), j - i + 1);

	return _TRUE;
}

BOOLEAN
isAllSpaceOrTab(
	u8	*data,
	u8	size
)
{
	u8	cnt = 0, NumOfSpaceAndTab = 0;

	while (size > cnt) {
		if (data[cnt] == ' ' || data[cnt] == '\t' || data[cnt] == '\0')
			++NumOfSpaceAndTab;

		++cnt;
	}

	return size == NumOfSpaceAndTab;
}


void rtw_hal_check_rxfifo_full(_adapter *adapter)
{
	struct dvobj_priv *psdpriv = adapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	struct registry_priv *regsty = &adapter->registrypriv;
	int save_cnt = _FALSE;

	if (regsty->check_hw_status == 1) {
		/* switch counter to RX fifo */
		if (IS_8188E(pHalData->version_id) ||
		    IS_8188F(pHalData->version_id) ||
		    IS_8812_SERIES(pHalData->version_id) ||
		    IS_8821_SERIES(pHalData->version_id) ||
		    IS_8723B_SERIES(pHalData->version_id) ||
		    IS_8192E(pHalData->version_id) ||
		    IS_8703B_SERIES(pHalData->version_id) ||
		    IS_8723D_SERIES(pHalData->version_id)) {
			rtw_write8(adapter, REG_RXERR_RPT + 3, rtw_read8(adapter, REG_RXERR_RPT + 3) | 0xa0);
			save_cnt = _TRUE;
		} else {
			/* todo: other chips */
		}


		if (save_cnt) {
			pdbgpriv->dbg_rx_fifo_last_overflow = pdbgpriv->dbg_rx_fifo_curr_overflow;
			pdbgpriv->dbg_rx_fifo_curr_overflow = rtw_read16(adapter, REG_RXERR_RPT);
			pdbgpriv->dbg_rx_fifo_diff_overflow = pdbgpriv->dbg_rx_fifo_curr_overflow - pdbgpriv->dbg_rx_fifo_last_overflow;
		} else {
			/* special value to indicate no implementation */
			pdbgpriv->dbg_rx_fifo_last_overflow = 1;
			pdbgpriv->dbg_rx_fifo_curr_overflow = 1;
			pdbgpriv->dbg_rx_fifo_diff_overflow = 1;
		}
	}
}

void linked_info_dump(_adapter *padapter, u8 benable)
{
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	if (padapter->bLinkInfoDump == benable)
		return;

	RTW_INFO("%s %s\n", __FUNCTION__, (benable) ? "enable" : "disable");

	if (benable) {
#ifdef CONFIG_LPS
		pwrctrlpriv->org_power_mgnt = pwrctrlpriv->power_mgnt;/* keep org value */
		rtw_pm_set_lps(padapter, PS_MODE_ACTIVE);
#endif

#ifdef CONFIG_IPS
		pwrctrlpriv->ips_org_mode = pwrctrlpriv->ips_mode;/* keep org value */
		rtw_pm_set_ips(padapter, IPS_NONE);
#endif
	} else {
#ifdef CONFIG_IPS
		rtw_pm_set_ips(padapter, pwrctrlpriv->ips_org_mode);
#endif /* CONFIG_IPS */

#ifdef CONFIG_LPS
		rtw_pm_set_lps(padapter, pwrctrlpriv->org_power_mgnt);
#endif /* CONFIG_LPS */
	}
	padapter->bLinkInfoDump = benable ;
}

#ifdef DBG_RX_SIGNAL_DISPLAY_RAW_DATA
void rtw_get_raw_rssi_info(void *sel, _adapter *padapter)
{
	u8 isCCKrate, rf_path;
	PHAL_DATA_TYPE	pHalData =  GET_HAL_DATA(padapter);
	struct rx_raw_rssi *psample_pkt_rssi = &padapter->recvpriv.raw_rssi_info;
	RTW_PRINT_SEL(sel, "RxRate = %s, PWDBALL = %d(%%), rx_pwr_all = %d(dBm)\n",
		HDATA_RATE(psample_pkt_rssi->data_rate), psample_pkt_rssi->pwdball, psample_pkt_rssi->pwr_all);
	isCCKrate = (psample_pkt_rssi->data_rate <= DESC_RATE11M) ? TRUE : FALSE;

	if (isCCKrate)
		psample_pkt_rssi->mimo_signal_strength[0] = psample_pkt_rssi->pwdball;

	for (rf_path = 0; rf_path < pHalData->NumTotalRFPath; rf_path++) {
		RTW_PRINT_SEL(sel, "RF_PATH_%d=>signal_strength:%d(%%),signal_quality:%d(%%)\n"
			, rf_path, psample_pkt_rssi->mimo_signal_strength[rf_path], psample_pkt_rssi->mimo_signal_quality[rf_path]);

		if (!isCCKrate) {
			RTW_PRINT_SEL(sel, "\trx_ofdm_pwr:%d(dBm),rx_ofdm_snr:%d(dB)\n",
				psample_pkt_rssi->ofdm_pwr[rf_path], psample_pkt_rssi->ofdm_snr[rf_path]);
		}
	}
}

void rtw_dump_raw_rssi_info(_adapter *padapter, void *sel)
{
	u8 isCCKrate, rf_path;
	PHAL_DATA_TYPE	pHalData =  GET_HAL_DATA(padapter);
	struct rx_raw_rssi *psample_pkt_rssi = &padapter->recvpriv.raw_rssi_info;
	_RTW_PRINT_SEL(sel, "============ RAW Rx Info dump ===================\n");
	_RTW_PRINT_SEL(sel, "RxRate = %s, PWDBALL = %d(%%), rx_pwr_all = %d(dBm)\n", HDATA_RATE(psample_pkt_rssi->data_rate), psample_pkt_rssi->pwdball, psample_pkt_rssi->pwr_all);

	isCCKrate = (psample_pkt_rssi->data_rate <= DESC_RATE11M) ? TRUE : FALSE;

	if (isCCKrate)
		psample_pkt_rssi->mimo_signal_strength[0] = psample_pkt_rssi->pwdball;

	for (rf_path = 0; rf_path < pHalData->NumTotalRFPath; rf_path++) {
		_RTW_PRINT_SEL(sel , "RF_PATH_%d=>signal_strength:%d(%%),signal_quality:%d(%%)"
			, rf_path, psample_pkt_rssi->mimo_signal_strength[rf_path], psample_pkt_rssi->mimo_signal_quality[rf_path]);

		if (!isCCKrate)
			_RTW_PRINT_SEL(sel , ",rx_ofdm_pwr:%d(dBm),rx_ofdm_snr:%d(dB)\n", psample_pkt_rssi->ofdm_pwr[rf_path], psample_pkt_rssi->ofdm_snr[rf_path]);
		else
			_RTW_PRINT_SEL(sel , "\n");

	}
}
#endif

#ifdef DBG_RX_DFRAME_RAW_DATA
void rtw_dump_rx_dframe_info(_adapter *padapter, void *sel)
{
	_irqL irqL;
	u8 isCCKrate, rf_path;
	struct recv_priv *precvpriv = &(padapter->recvpriv);
	PHAL_DATA_TYPE	pHalData =  GET_HAL_DATA(padapter);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta;
	struct sta_recv_dframe_info *psta_dframe_info;
	int i;
	_list	*plist, *phead;
	char *BW;
	u8 bc_addr[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u8 null_addr[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	if (precvpriv->store_law_data_flag) {

		_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);

		for (i = 0; i < NUM_STA; i++) {
			phead = &(pstapriv->sta_hash[i]);
			plist = get_next(phead);

			while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {

				psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);
				plist = get_next(plist);

				if (psta) {
					psta_dframe_info = &psta->sta_dframe_info;
					if ((_rtw_memcmp(psta->hwaddr, bc_addr, 6)  !=   _TRUE)
					    && (_rtw_memcmp(psta->hwaddr, null_addr, 6)  !=  _TRUE)
					    && (_rtw_memcmp(psta->hwaddr, adapter_mac_addr(padapter), 6)  !=  _TRUE)) {


						isCCKrate = (psta_dframe_info->sta_data_rate <= DESC_RATE11M) ? TRUE : FALSE;

						switch (psta_dframe_info->sta_bw_mode) {

						case CHANNEL_WIDTH_20:
							BW = "20M";
							break;

						case CHANNEL_WIDTH_40:
							BW = "40M";
							break;

						case CHANNEL_WIDTH_80:
							BW = "80M";
							break;

						case CHANNEL_WIDTH_160:
							BW = "160M";
							break;

						default:
							BW = "";
							break;
						}

						RTW_PRINT_SEL(sel, "==============================\n");
						_RTW_PRINT_SEL(sel, "macaddr =" MAC_FMT "\n", MAC_ARG(psta->hwaddr));
						_RTW_PRINT_SEL(sel, "BW=%s, sgi =%d\n", BW, psta_dframe_info->sta_sgi);
						_RTW_PRINT_SEL(sel, "Rx_Data_Rate = %s\n", HDATA_RATE(psta_dframe_info->sta_data_rate));

						for (rf_path = 0; rf_path < pHalData->NumTotalRFPath; rf_path++) {

							if (!isCCKrate) {

								_RTW_PRINT_SEL(sel , "RF_PATH_%d RSSI:%d(dBm)", rf_path, psta_dframe_info->sta_RxPwr[rf_path]);
								_RTW_PRINT_SEL(sel , ",rx_ofdm_snr:%d(dB)\n", psta_dframe_info->sta_ofdm_snr[rf_path]);

							} else

								_RTW_PRINT_SEL(sel , "RF_PATH_%d RSSI:%d(dBm)\n", rf_path, (psta_dframe_info->sta_mimo_signal_strength[rf_path]) - 100);
						}
					}
				}
			}
		}
		_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);
	}
}
#endif
void rtw_store_phy_info(_adapter *padapter, union recv_frame *prframe)
{
	u8 isCCKrate, rf_path , dframe_type;
	u8 *ptr;
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
#ifdef DBG_RX_DFRAME_RAW_DATA
	struct sta_recv_dframe_info *psta_dframe_info;
#endif
	struct recv_priv *precvpriv = &(padapter->recvpriv);
	PHAL_DATA_TYPE	pHalData =  GET_HAL_DATA(padapter);
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;
	struct sta_info *psta = prframe->u.hdr.psta;
	struct _odm_phy_status_info_ *p_phy_info  = (struct _odm_phy_status_info_ *)(&pattrib->phy_info);
	struct rx_raw_rssi *psample_pkt_rssi = &padapter->recvpriv.raw_rssi_info;
	psample_pkt_rssi->data_rate = pattrib->data_rate;
	ptr = prframe->u.hdr.rx_data;
	dframe_type = GetFrameType(ptr);
	/*RTW_INFO("=>%s\n", __FUNCTION__);*/


	if (precvpriv->store_law_data_flag) {
		isCCKrate = (pattrib->data_rate <= DESC_RATE11M) ? TRUE : FALSE;

		psample_pkt_rssi->pwdball = p_phy_info->rx_pwdb_all;
		psample_pkt_rssi->pwr_all = p_phy_info->recv_signal_power;

		for (rf_path = 0; rf_path < pHalData->NumTotalRFPath; rf_path++) {
			psample_pkt_rssi->mimo_signal_strength[rf_path] = p_phy_info->rx_mimo_signal_strength[rf_path];
			psample_pkt_rssi->mimo_signal_quality[rf_path] = p_phy_info->rx_mimo_signal_quality[rf_path];
			if (!isCCKrate) {
				psample_pkt_rssi->ofdm_pwr[rf_path] = p_phy_info->rx_pwr[rf_path];
				psample_pkt_rssi->ofdm_snr[rf_path] = p_phy_info->rx_snr[rf_path];
			}
		}
#ifdef DBG_RX_DFRAME_RAW_DATA
		if ((dframe_type == WIFI_DATA_TYPE) || (dframe_type == WIFI_QOS_DATA_TYPE) || (padapter->registrypriv.mp_mode == 1)) {

			/*RTW_INFO("=>%s WIFI_DATA_TYPE or WIFI_QOS_DATA_TYPE\n", __FUNCTION__);*/
			if (psta) {
				psta_dframe_info = &psta->sta_dframe_info;
				/*RTW_INFO("=>%s psta->hwaddr="MAC_FMT" !\n", __FUNCTION__, MAC_ARG(psta->hwaddr));*/
				if ((_rtw_memcmp(psta->hwaddr, bc_addr, ETH_ALEN) != _TRUE) || (padapter->registrypriv.mp_mode == 1)) {
					psta_dframe_info->sta_data_rate = pattrib->data_rate;
					psta_dframe_info->sta_sgi = pattrib->sgi;
					psta_dframe_info->sta_bw_mode = pattrib->bw;
					for (rf_path = 0; rf_path < pHalData->NumTotalRFPath; rf_path++) {

						psta_dframe_info->sta_mimo_signal_strength[rf_path] = (p_phy_info->rx_mimo_signal_strength[rf_path]);/*Percentage to dbm*/

						if (!isCCKrate) {
							psta_dframe_info->sta_ofdm_snr[rf_path] = p_phy_info->rx_snr[rf_path];
							psta_dframe_info->sta_RxPwr[rf_path] = p_phy_info->rx_pwr[rf_path];
						}
					}
				}
			}
		}
#endif
	}

}


int check_phy_efuse_tx_power_info_valid(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u8 *pContent = pHalData->efuse_eeprom_data;
	int index = 0;
	u16 tx_index_offset = 0x0000;

	switch (rtw_get_chip_type(padapter)) {
	case RTL8723B:
		tx_index_offset = EEPROM_TX_PWR_INX_8723B;
		break;
	case RTL8703B:
		tx_index_offset = EEPROM_TX_PWR_INX_8703B;
		break;
	case RTL8723D:
		tx_index_offset = EEPROM_TX_PWR_INX_8723D;
		break;
	case RTL8188E:
		tx_index_offset = EEPROM_TX_PWR_INX_88E;
		break;
	case RTL8188F:
		tx_index_offset = EEPROM_TX_PWR_INX_8188F;
		break;
	case RTL8192E:
		tx_index_offset = EEPROM_TX_PWR_INX_8192E;
		break;
	case RTL8821:
		tx_index_offset = EEPROM_TX_PWR_INX_8821;
		break;
	case RTL8812:
		tx_index_offset = EEPROM_TX_PWR_INX_8812;
		break;
	case RTL8814A:
		tx_index_offset = EEPROM_TX_PWR_INX_8814;
		break;
	case RTL8822B:
		tx_index_offset = EEPROM_TX_PWR_INX_8822B;
		break;
	case RTL8821C:
		tx_index_offset = EEPROM_TX_PWR_INX_8821C;
		break;
	default:
		tx_index_offset = 0x0010;
		break;
	}

	/* TODO: chacking length by ICs */
	for (index = 0 ; index < 11 ; index++) {
		if (pContent[tx_index_offset + index] == 0xFF)
			return _FALSE;
	}
	return _TRUE;
}

int hal_efuse_macaddr_offset(_adapter *adapter)
{
	u8 interface_type = 0;
	int addr_offset = -1;

	interface_type = rtw_get_intf_type(adapter);

	switch (rtw_get_chip_type(adapter)) {
#ifdef CONFIG_RTL8723B
	case RTL8723B:
		if (interface_type == RTW_USB)
			addr_offset = EEPROM_MAC_ADDR_8723BU;
		else if (interface_type == RTW_SDIO)
			addr_offset = EEPROM_MAC_ADDR_8723BS;
		else if (interface_type == RTW_PCIE)
			addr_offset = EEPROM_MAC_ADDR_8723BE;
		break;
#endif
#ifdef CONFIG_RTL8703B
	case RTL8703B:
		if (interface_type == RTW_USB)
			addr_offset = EEPROM_MAC_ADDR_8703BU;
		else if (interface_type == RTW_SDIO)
			addr_offset = EEPROM_MAC_ADDR_8703BS;
		break;
#endif
#ifdef CONFIG_RTL8723D
	case RTL8723D:
		if (interface_type == RTW_USB)
			addr_offset = EEPROM_MAC_ADDR_8723DU;
		else if (interface_type == RTW_SDIO)
			addr_offset = EEPROM_MAC_ADDR_8723DS;
		else if (interface_type == RTW_PCIE)
			addr_offset = EEPROM_MAC_ADDR_8723DE;
		break;
#endif

#ifdef CONFIG_RTL8188E
	case RTL8188E:
		if (interface_type == RTW_USB)
			addr_offset = EEPROM_MAC_ADDR_88EU;
		else if (interface_type == RTW_SDIO)
			addr_offset = EEPROM_MAC_ADDR_88ES;
		else if (interface_type == RTW_PCIE)
			addr_offset = EEPROM_MAC_ADDR_88EE;
		break;
#endif
#ifdef CONFIG_RTL8188F
	case RTL8188F:
		if (interface_type == RTW_USB)
			addr_offset = EEPROM_MAC_ADDR_8188FU;
		else if (interface_type == RTW_SDIO)
			addr_offset = EEPROM_MAC_ADDR_8188FS;
		break;
#endif
#ifdef CONFIG_RTL8812A
	case RTL8812:
		if (interface_type == RTW_USB)
			addr_offset = EEPROM_MAC_ADDR_8812AU;
		else if (interface_type == RTW_PCIE)
			addr_offset = EEPROM_MAC_ADDR_8812AE;
		break;
#endif
#ifdef CONFIG_RTL8821A
	case RTL8821:
		if (interface_type == RTW_USB)
			addr_offset = EEPROM_MAC_ADDR_8821AU;
		else if (interface_type == RTW_SDIO)
			addr_offset = EEPROM_MAC_ADDR_8821AS;
		else if (interface_type == RTW_PCIE)
			addr_offset = EEPROM_MAC_ADDR_8821AE;
		break;
#endif
#ifdef CONFIG_RTL8192E
	case RTL8192E:
		if (interface_type == RTW_USB)
			addr_offset = EEPROM_MAC_ADDR_8192EU;
		else if (interface_type == RTW_SDIO)
			addr_offset = EEPROM_MAC_ADDR_8192ES;
		else if (interface_type == RTW_PCIE)
			addr_offset = EEPROM_MAC_ADDR_8192EE;
		break;
#endif
#ifdef CONFIG_RTL8814A
	case RTL8814A:
		if (interface_type == RTW_USB)
			addr_offset = EEPROM_MAC_ADDR_8814AU;
		else if (interface_type == RTW_PCIE)
			addr_offset = EEPROM_MAC_ADDR_8814AE;
		break;
#endif

#ifdef CONFIG_RTL8822B
	case RTL8822B:
		if (interface_type == RTW_USB)
			addr_offset = EEPROM_MAC_ADDR_8822BU;
		else if (interface_type == RTW_SDIO)
			addr_offset = EEPROM_MAC_ADDR_8822BS;
		else if (interface_type == RTW_PCIE)
			addr_offset = EEPROM_MAC_ADDR_8822BE;
		break;
#endif /* CONFIG_RTL8822B */

#ifdef CONFIG_RTL8821C
	case RTL8821C:
		if (interface_type == RTW_USB)
			addr_offset = EEPROM_MAC_ADDR_8821CU;
		else if (interface_type == RTW_SDIO)
			addr_offset = EEPROM_MAC_ADDR_8821CS;
		else if (interface_type == RTW_PCIE)
			addr_offset = EEPROM_MAC_ADDR_8821CE;
		break;
#endif /* CONFIG_RTL8821C */
	}

	if (addr_offset == -1) {
		RTW_ERR("%s: unknown combination - chip_type:%u, interface:%u\n"
			, __func__, rtw_get_chip_type(adapter), rtw_get_intf_type(adapter));
	}

	return addr_offset;
}

int Hal_GetPhyEfuseMACAddr(PADAPTER padapter, u8 *mac_addr)
{
	int ret = _FAIL;
	int addr_offset;

	addr_offset = hal_efuse_macaddr_offset(padapter);
	if (addr_offset == -1)
		goto exit;

	ret = rtw_efuse_map_read(padapter, addr_offset, ETH_ALEN, mac_addr);

exit:
	return ret;
}

void rtw_dump_cur_efuse(PADAPTER padapter)
{
	int i =0;
	int mapsize =0;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(padapter);

	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_EFUSE_MAP_LEN , (void *)&mapsize, _FALSE);

	if (mapsize <= 0 || mapsize > EEPROM_MAX_SIZE) {
		RTW_ERR("wrong map size %d\n", mapsize);
		return;
	}

	if (hal_data->efuse_file_status == EFUSE_FILE_LOADED)
		RTW_INFO("EFUSE FILE\n");
	else
		RTW_INFO("HW EFUSE\n");

#ifdef CONFIG_RTW_DEBUG
	for (i = 0; i < mapsize; i++) {
		if (i % 16 == 0)
			RTW_PRINT_SEL(RTW_DBGDUMP, "0x%03x: ", i);

		_RTW_PRINT_SEL(RTW_DBGDUMP, "%02X%s"
			, hal_data->efuse_eeprom_data[i]
			, ((i + 1) % 16 == 0) ? "\n" : (((i + 1) % 8 == 0) ? "    " : " ")
		);
	}
	_RTW_PRINT_SEL(RTW_DBGDUMP, "\n");
#endif
}


#ifdef CONFIG_EFUSE_CONFIG_FILE
u32 Hal_readPGDataFromConfigFile(PADAPTER padapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(padapter);
	u32 ret = _FALSE;
	u32 maplen = 0;

	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_EFUSE_MAP_LEN , (void *)&maplen, _FALSE);

	if (maplen < 256 || maplen > EEPROM_MAX_SIZE) {
		RTW_ERR("eFuse length error :%d\n", maplen);
		return _FALSE;
	}	

	ret = rtw_read_efuse_from_file(EFUSE_MAP_PATH, hal_data->efuse_eeprom_data, maplen);

	hal_data->efuse_file_status = ((ret == _FAIL) ? EFUSE_FILE_FAILED : EFUSE_FILE_LOADED);

	if (hal_data->efuse_file_status == EFUSE_FILE_LOADED)
		rtw_dump_cur_efuse(padapter);

	return ret;
}

u32 Hal_ReadMACAddrFromFile(PADAPTER padapter, u8 *mac_addr)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(padapter);
	u32 ret = _FAIL;

	if (rtw_read_macaddr_from_file(WIFIMAC_PATH, mac_addr) == _SUCCESS
		&& rtw_check_invalid_mac_address(mac_addr, _TRUE) == _FALSE
	) {
		hal_data->macaddr_file_status = MACADDR_FILE_LOADED;
		ret = _SUCCESS;
	} else
		hal_data->macaddr_file_status = MACADDR_FILE_FAILED;

	return ret;
}
#endif /* CONFIG_EFUSE_CONFIG_FILE */

int hal_config_macaddr(_adapter *adapter, bool autoload_fail)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	u8 addr[ETH_ALEN];
	int addr_offset = hal_efuse_macaddr_offset(adapter);
	u8 *hw_addr = NULL;
	int ret = _SUCCESS;

	if (autoload_fail)
		goto bypass_hw_pg;

	if (addr_offset != -1)
		hw_addr = &hal_data->efuse_eeprom_data[addr_offset];

#ifdef CONFIG_EFUSE_CONFIG_FILE
	/* if the hw_addr is written by efuse file, set to NULL */
	if (hal_data->efuse_file_status == EFUSE_FILE_LOADED)
		hw_addr = NULL;
#endif

	if (!hw_addr) {
		/* try getting hw pg data */
		if (Hal_GetPhyEfuseMACAddr(adapter, addr) == _SUCCESS)
			hw_addr = addr;
	}

	/* check hw pg data */
	if (hw_addr && rtw_check_invalid_mac_address(hw_addr, _TRUE) == _FALSE) {
		_rtw_memcpy(hal_data->EEPROMMACAddr, hw_addr, ETH_ALEN);
		goto exit;
	}

bypass_hw_pg:

#ifdef CONFIG_EFUSE_CONFIG_FILE
	/* check wifi mac file */
	if (Hal_ReadMACAddrFromFile(adapter, addr) == _SUCCESS) {
		_rtw_memcpy(hal_data->EEPROMMACAddr, addr, ETH_ALEN);
		goto exit;
	}
#endif

	_rtw_memset(hal_data->EEPROMMACAddr, 0, ETH_ALEN);
	ret = _FAIL;

exit:
	return ret;
}

#ifdef CONFIG_RF_POWER_TRIM
u32 Array_kfreemap[] = {
	0x08, 0xe,
	0x06, 0xc,
	0x04, 0xa,
	0x02, 0x8,
	0x00, 0x6,
	0x03, 0x4,
	0x05, 0x2,
	0x07, 0x0,
	0x09, 0x0,
	0x0c, 0x0,
};

void rtw_bb_rf_gain_offset(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct registry_priv  *registry_par = &padapter->registrypriv;
	struct kfree_data_t *kfree_data = &pHalData->kfree_data;
	u8		value = pHalData->EEPROMRFGainOffset;
	u8		tmp = 0x3e;
	u32		res, i = 0;
	u4Byte		ArrayLen	= sizeof(Array_kfreemap) / sizeof(u32);
	pu4Byte		Array	= Array_kfreemap;
	u4Byte		v1 = 0, v2 = 0, GainValue = 0, target = 0;

	if (registry_par->RegPwrTrimEnable == 2) {
		RTW_INFO("Registry kfree default force disable.\n");
		return;
	}

#if defined(CONFIG_RTL8723B)
	if (value & BIT4 || (registry_par->RegPwrTrimEnable == 1)) {
		RTW_INFO("Offset RF Gain.\n");
		RTW_INFO("Offset RF Gain.  pHalData->EEPROMRFGainVal=0x%x\n", pHalData->EEPROMRFGainVal);

		if (pHalData->EEPROMRFGainVal != 0xff) {

			if (pHalData->ant_path == ODM_RF_PATH_A)
				GainValue = (pHalData->EEPROMRFGainVal & 0x0f);

			else
				GainValue = (pHalData->EEPROMRFGainVal & 0xf0) >> 4;
			RTW_INFO("Ant PATH_%d GainValue Offset = 0x%x\n", (pHalData->ant_path == ODM_RF_PATH_A) ? (ODM_RF_PATH_A) : (ODM_RF_PATH_B), GainValue);

			for (i = 0; i < ArrayLen; i += 2) {
				/* RTW_INFO("ArrayLen in =%d ,Array 1 =0x%x ,Array2 =0x%x\n",i,Array[i],Array[i]+1); */
				v1 = Array[i];
				v2 = Array[i + 1];
				if (v1 == GainValue) {
					RTW_INFO("Offset RF Gain. got v1 =0x%x ,v2 =0x%x\n", v1, v2);
					target = v2;
					break;
				}
			}
			RTW_INFO("pHalData->EEPROMRFGainVal=0x%x ,Gain offset Target Value=0x%x\n", pHalData->EEPROMRFGainVal, target);

			res = rtw_hal_read_rfreg(padapter, RF_PATH_A, 0x7f, 0xffffffff);
			RTW_INFO("Offset RF Gain. before reg 0x7f=0x%08x\n", res);
			phy_set_rf_reg(padapter, RF_PATH_A, REG_RF_BB_GAIN_OFFSET, BIT18 | BIT17 | BIT16 | BIT15, target);
			res = rtw_hal_read_rfreg(padapter, RF_PATH_A, 0x7f, 0xffffffff);

			RTW_INFO("Offset RF Gain. After reg 0x7f=0x%08x\n", res);

		} else

			RTW_INFO("Offset RF Gain.  pHalData->EEPROMRFGainVal=0x%x	!= 0xff, didn't run Kfree\n", pHalData->EEPROMRFGainVal);
	} else
		RTW_INFO("Using the default RF gain.\n");

#elif defined(CONFIG_RTL8188E)
	if (value & BIT4 || (registry_par->RegPwrTrimEnable == 1)) {
		RTW_INFO("8188ES Offset RF Gain.\n");
		RTW_INFO("8188ES Offset RF Gain. EEPROMRFGainVal=0x%x\n",
			 pHalData->EEPROMRFGainVal);

		if (pHalData->EEPROMRFGainVal != 0xff) {
			res = rtw_hal_read_rfreg(padapter, RF_PATH_A,
					 REG_RF_BB_GAIN_OFFSET, 0xffffffff);

			RTW_INFO("Offset RF Gain. reg 0x55=0x%x\n", res);
			res &= 0xfff87fff;

			res |= (pHalData->EEPROMRFGainVal & 0x0f) << 15;
			RTW_INFO("Offset RF Gain. res=0x%x\n", res);

			rtw_hal_write_rfreg(padapter, RF_PATH_A,
					    REG_RF_BB_GAIN_OFFSET,
					    RF_GAIN_OFFSET_MASK, res);
		} else {
			RTW_INFO("Offset RF Gain. EEPROMRFGainVal=0x%x == 0xff, didn't run Kfree\n",
				 pHalData->EEPROMRFGainVal);
		}
	} else
		RTW_INFO("Using the default RF gain.\n");
#else
	/* TODO: call this when channel switch */
	if (kfree_data->flag & KFREE_FLAG_ON)
		rtw_rf_apply_tx_gain_offset(padapter, 6); /* input ch6 to select BB_GAIN_2G */
#endif

}
#endif /*CONFIG_RF_POWER_TRIM */

bool kfree_data_is_bb_gain_empty(struct kfree_data_t *data)
{
#ifdef CONFIG_RF_POWER_TRIM
	int i, j;

	for (i = 0; i < BB_GAIN_NUM; i++)
		for (j = 0; j < RF_PATH_MAX; j++)
			if (data->bb_gain[i][j] != 0)
				return 0;
#endif
	return 1;
}

#ifdef CONFIG_USB_RX_AGGREGATION
void rtw_set_usb_agg_by_mode_normal(_adapter *padapter, u8 cur_wireless_mode)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	if (cur_wireless_mode < WIRELESS_11_24N
	    && cur_wireless_mode > 0) { /* ABG mode */
#ifdef CONFIG_PREALLOC_RX_SKB_BUFFER
		u32 remainder = 0;
		u8 quotient = 0;

		remainder = MAX_RECVBUF_SZ % (4 * 1024);
		quotient = (u8)(MAX_RECVBUF_SZ >> 12);

		if (quotient > 5) {
			pHalData->rxagg_usb_size = 0x6;
			pHalData->rxagg_usb_timeout = 0x10;
		} else {
			if (remainder >= 2048) {
				pHalData->rxagg_usb_size = quotient;
				pHalData->rxagg_usb_timeout = 0x10;
			} else {
				pHalData->rxagg_usb_size = (quotient - 1);
				pHalData->rxagg_usb_timeout = 0x10;
			}
		}
#else /* !CONFIG_PREALLOC_RX_SKB_BUFFER */
		if (0x6 != pHalData->rxagg_usb_size || 0x10 != pHalData->rxagg_usb_timeout) {
			pHalData->rxagg_usb_size = 0x6;
			pHalData->rxagg_usb_timeout = 0x10;
			rtw_write16(padapter, REG_RXDMA_AGG_PG_TH,
				pHalData->rxagg_usb_size | (pHalData->rxagg_usb_timeout << 8));
		}
#endif /* CONFIG_PREALLOC_RX_SKB_BUFFER */

	} else if (cur_wireless_mode >= WIRELESS_11_24N
		   && cur_wireless_mode <= WIRELESS_MODE_MAX) { /* N AC mode */
#ifdef CONFIG_PREALLOC_RX_SKB_BUFFER
		u32 remainder = 0;
		u8 quotient = 0;

		remainder = MAX_RECVBUF_SZ % (4 * 1024);
		quotient = (u8)(MAX_RECVBUF_SZ >> 12);

		if (quotient > 5) {
			pHalData->rxagg_usb_size = 0x5;
			pHalData->rxagg_usb_timeout = 0x20;
		} else {
			if (remainder >= 2048) {
				pHalData->rxagg_usb_size = quotient;
				pHalData->rxagg_usb_timeout = 0x10;
			} else {
				pHalData->rxagg_usb_size = (quotient - 1);
				pHalData->rxagg_usb_timeout = 0x10;
			}
		}
#else /* !CONFIG_PREALLOC_RX_SKB_BUFFER */
		if ((0x5 != pHalData->rxagg_usb_size) || (0x20 != pHalData->rxagg_usb_timeout)) {
			pHalData->rxagg_usb_size = 0x5;
			pHalData->rxagg_usb_timeout = 0x20;
			rtw_write16(padapter, REG_RXDMA_AGG_PG_TH,
				pHalData->rxagg_usb_size | (pHalData->rxagg_usb_timeout << 8));
		}
#endif /* CONFIG_PREALLOC_RX_SKB_BUFFER */

	} else {
		/* RTW_INFO("%s: Unknow wireless mode(0x%x)\n",__func__,padapter->mlmeextpriv.cur_wireless_mode); */
	}
}

void rtw_set_usb_agg_by_mode_customer(_adapter *padapter, u8 cur_wireless_mode, u8 UsbDmaSize, u8 Legacy_UsbDmaSize)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if (cur_wireless_mode < WIRELESS_11_24N
	    && cur_wireless_mode > 0) { /* ABG mode */
		if (Legacy_UsbDmaSize != pHalData->rxagg_usb_size
		    || 0x10 != pHalData->rxagg_usb_timeout) {
			pHalData->rxagg_usb_size = Legacy_UsbDmaSize;
			pHalData->rxagg_usb_timeout = 0x10;
			rtw_write16(padapter, REG_RXDMA_AGG_PG_TH,
				pHalData->rxagg_usb_size | (pHalData->rxagg_usb_timeout << 8));
		}
	} else if (cur_wireless_mode >= WIRELESS_11_24N
		   && cur_wireless_mode <= WIRELESS_MODE_MAX) { /* N AC mode */
		if (UsbDmaSize != pHalData->rxagg_usb_size
		    || 0x20 != pHalData->rxagg_usb_timeout) {
			pHalData->rxagg_usb_size = UsbDmaSize;
			pHalData->rxagg_usb_timeout = 0x20;
			rtw_write16(padapter, REG_RXDMA_AGG_PG_TH,
				pHalData->rxagg_usb_size | (pHalData->rxagg_usb_timeout << 8));
		}
	} else {
		/* RTW_INFO("%s: Unknown wireless mode(0x%x)\n",__func__,padapter->mlmeextpriv.cur_wireless_mode); */
	}
}

void rtw_set_usb_agg_by_mode(_adapter *padapter, u8 cur_wireless_mode)
{
#ifdef CONFIG_PLATFORM_NOVATEK_NT72668
	rtw_set_usb_agg_by_mode_customer(padapter, cur_wireless_mode, 0x3, 0x3);
	return;
#endif /* CONFIG_PLATFORM_NOVATEK_NT72668 */

	rtw_set_usb_agg_by_mode_normal(padapter, cur_wireless_mode);
}
#endif /* CONFIG_USB_RX_AGGREGATION */

/* To avoid RX affect TX throughput */
void dm_DynamicUsbTxAgg(_adapter *padapter, u8 from_timer)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct mlme_priv		*pmlmepriv = &(padapter->mlmepriv);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8 cur_wireless_mode = WIRELESS_INVALID;

#ifdef CONFIG_USB_RX_AGGREGATION
	if (IS_HARDWARE_TYPE_8821U(padapter)) { /* || IS_HARDWARE_TYPE_8192EU(padapter)) */
		/* This AGG_PH_TH only for UsbRxAggMode == USB_RX_AGG_USB */
		if ((pHalData->rxagg_mode == RX_AGG_USB) && (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)) {
			if (pdvobjpriv->traffic_stat.cur_tx_tp > 2 && pdvobjpriv->traffic_stat.cur_rx_tp < 30)
				rtw_write16(padapter , REG_RXDMA_AGG_PG_TH , 0x1010);
			else if (pdvobjpriv->traffic_stat.last_tx_bytes > 220000 && pdvobjpriv->traffic_stat.cur_rx_tp < 30)
				rtw_write16(padapter , REG_RXDMA_AGG_PG_TH , 0x1006);
			else
				rtw_write16(padapter, REG_RXDMA_AGG_PG_TH, 0x2005); /* dmc agg th 20K */

			/* RTW_INFO("TX_TP=%u, RX_TP=%u\n", pdvobjpriv->traffic_stat.cur_tx_tp, pdvobjpriv->traffic_stat.cur_rx_tp); */
		}
	} else if (IS_HARDWARE_TYPE_8812(padapter)) {
#ifdef CONFIG_CONCURRENT_MODE
		u8 i;
		_adapter *iface;
		u8 bassocaed = _FALSE;
		struct mlme_ext_priv *mlmeext;

		for (i = 0; i < pdvobjpriv->iface_nums; i++) {
			iface = pdvobjpriv->padapters[i];
			mlmeext = &iface->mlmeextpriv;
			if (rtw_linked_check(iface) == _TRUE) {
				if (mlmeext->cur_wireless_mode >= cur_wireless_mode)
					cur_wireless_mode = mlmeext->cur_wireless_mode;
				bassocaed = _TRUE;
			}
		}
		if (bassocaed)
#endif
			rtw_set_usb_agg_by_mode(padapter, cur_wireless_mode);
#ifdef CONFIG_PLATFORM_NOVATEK_NT72668
	} else {
		rtw_set_usb_agg_by_mode(padapter, cur_wireless_mode);
#endif /* CONFIG_PLATFORM_NOVATEK_NT72668 */
	}
#endif
}

/* bus-agg check for SoftAP mode */
inline u8 rtw_hal_busagg_qsel_check(_adapter *padapter, u8 pre_qsel, u8 next_qsel)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	u8 chk_rst = _SUCCESS;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != _TRUE)
		return chk_rst;

	/* if((pre_qsel == 0xFF)||(next_qsel== 0xFF)) */
	/*	return chk_rst; */

	if (((pre_qsel == QSLT_HIGH) || ((next_qsel == QSLT_HIGH)))
	    && (pre_qsel != next_qsel)) {
		/* RTW_INFO("### bus-agg break cause of qsel misatch, pre_qsel=0x%02x,next_qsel=0x%02x ###\n", */
		/*	pre_qsel,next_qsel); */
		chk_rst = _FAIL;
	}
	return chk_rst;
}

/*
 * Description:
 * dump_TX_FIFO: This is only used to dump TX_FIFO for debug WoW mode offload
 * contant.
 *
 * Input:
 * adapter: adapter pointer.
 * page_num: The max. page number that user want to dump.
 * page_size: page size of each page. eg. 128 bytes, 256 bytes, 512byte.
 */
void dump_TX_FIFO(_adapter *padapter, u8 page_num, u16 page_size)
{

	int i;
	u8 val = 0;
	u8 base = 0;
	u32 addr = 0;
	u32 count = (page_size / 8);

	if (page_num <= 0) {
		RTW_INFO("!!%s: incorrect input page_num paramter!\n", __func__);
		return;
	}

	if (page_size < 128 || page_size > 512) {
		RTW_INFO("!!%s: incorrect input page_size paramter!\n", __func__);
		return;
	}

	RTW_INFO("+%s+\n", __func__);
	val = rtw_read8(padapter, 0x106);
	rtw_write8(padapter, 0x106, 0x69);
	RTW_INFO("0x106: 0x%02x\n", val);
	base = rtw_read8(padapter, 0x209);
	RTW_INFO("0x209: 0x%02x\n", base);

	addr = ((base)*page_size) / 8;
	for (i = 0 ; i < page_num * count ; i += 2) {
		rtw_write32(padapter, 0x140, addr + i);
		printk(" %08x %08x ", rtw_read32(padapter, 0x144), rtw_read32(padapter, 0x148));
		rtw_write32(padapter, 0x140, addr + i + 1);
		printk(" %08x %08x\n", rtw_read32(padapter, 0x144), rtw_read32(padapter, 0x148));
	}
}

#ifdef CONFIG_GPIO_API
u8 rtw_hal_get_gpio(_adapter *adapter, u8 gpio_num)
{
	u8 value = 0;
	u8 direction = 0;
	u32 gpio_ctrl_reg_to_set = REG_GPIO_PIN_CTRL + 2;
	u8 gpio_num_to_set = gpio_num;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);

	if (rtw_hal_gpio_func_check(adapter, gpio_num) == _FAIL)
		return value;

	rtw_ps_deny(adapter, PS_DENY_IOCTL);

	RTW_INFO("rf_pwrstate=0x%02x\n", pwrpriv->rf_pwrstate);
	LeaveAllPowerSaveModeDirect(adapter);

	if (gpio_num > 7) {
		gpio_ctrl_reg_to_set = REG_GPIO_PIN_CTRL_2 + 2;
		gpio_num_to_set = gpio_num - 8;
	}

	/* Read GPIO Direction */
	direction = (rtw_read8(adapter, gpio_ctrl_reg_to_set) & BIT(gpio_num_to_set)) >> gpio_num;

	/* According the direction to read register value */
	if (direction)
		value =  (rtw_read8(adapter, gpio_ctrl_reg_to_set) & BIT(gpio_num_to_set)) >> gpio_num;
	else
		value =  (rtw_read8(adapter, gpio_ctrl_reg_to_set) & BIT(gpio_num_to_set)) >> gpio_num;

	rtw_ps_deny_cancel(adapter, PS_DENY_IOCTL);
	RTW_INFO("%s direction=%d value=%d\n", __FUNCTION__, direction, value);

	return value;
}

int  rtw_hal_set_gpio_output_value(_adapter *adapter, u8 gpio_num, bool isHigh)
{
	u8 direction = 0;
	u8 res = -1;
	u32 gpio_ctrl_reg_to_set = REG_GPIO_PIN_CTRL + 2;
	u8 gpio_num_to_set = gpio_num;

	if (rtw_hal_gpio_func_check(adapter, gpio_num) == _FAIL)
		return -1;

	rtw_ps_deny(adapter, PS_DENY_IOCTL);

	LeaveAllPowerSaveModeDirect(adapter);

	if (gpio_num > 7) {
		gpio_ctrl_reg_to_set = REG_GPIO_PIN_CTRL_2 + 2;
		gpio_num_to_set = gpio_num - 8;
	}

	/* Read GPIO direction */
	direction = (rtw_read8(adapter, REG_GPIO_PIN_CTRL + 2) & BIT(gpio_num)) >> gpio_num;

	/* If GPIO is output direction, setting value. */
	if (direction) {
		if (isHigh)
			rtw_write8(adapter, gpio_ctrl_reg_to_set, rtw_read8(adapter, gpio_ctrl_reg_to_set) | BIT(gpio_num_to_set));
		else
			rtw_write8(adapter, gpio_ctrl_reg_to_set, rtw_read8(adapter, gpio_ctrl_reg_to_set) & ~BIT(gpio_num_to_set));

		RTW_INFO("%s Set gpio %x[%d]=%d\n", __FUNCTION__, REG_GPIO_PIN_CTRL + 1, gpio_num, isHigh);
		res = 0;
	} else {
		RTW_INFO("%s The gpio is input,not be set!\n", __FUNCTION__);
		res = -1;
	}

	rtw_ps_deny_cancel(adapter, PS_DENY_IOCTL);
	return res;
}

int rtw_hal_config_gpio(_adapter *adapter, u8 gpio_num, bool isOutput)
{
	u32 gpio_ctrl_reg_to_set = REG_GPIO_PIN_CTRL + 2;
	u8 gpio_num_to_set = gpio_num;

	if (rtw_hal_gpio_func_check(adapter, gpio_num) == _FAIL)
		return -1;

	RTW_INFO("%s gpio_num =%d direction=%d\n", __FUNCTION__, gpio_num, isOutput);

	rtw_ps_deny(adapter, PS_DENY_IOCTL);

	LeaveAllPowerSaveModeDirect(adapter);

	rtw_hal_gpio_multi_func_reset(adapter, gpio_num);

	if (gpio_num > 7) {
		gpio_ctrl_reg_to_set = REG_GPIO_PIN_CTRL_2 + 2;
		gpio_num_to_set = gpio_num - 8;
	}

	if (isOutput)
		rtw_write8(adapter, gpio_ctrl_reg_to_set, rtw_read8(adapter, gpio_ctrl_reg_to_set) | BIT(gpio_num_to_set));
	else
		rtw_write8(adapter, gpio_ctrl_reg_to_set, rtw_read8(adapter, gpio_ctrl_reg_to_set) & ~BIT(gpio_num_to_set));

	rtw_ps_deny_cancel(adapter, PS_DENY_IOCTL);

	return 0;
}
int rtw_hal_register_gpio_interrupt(_adapter *adapter, int gpio_num, void(*callback)(u8 level))
{
	u8 value;
	u8 direction;
	PHAL_DATA_TYPE phal = GET_HAL_DATA(adapter);

	if (IS_HARDWARE_TYPE_8188E(adapter)) {
		if (gpio_num > 7 || gpio_num < 4) {
			RTW_PRINT("%s The gpio number does not included 4~7.\n", __FUNCTION__);
			return -1;
		}
	}

	rtw_ps_deny(adapter, PS_DENY_IOCTL);

	LeaveAllPowerSaveModeDirect(adapter);

	/* Read GPIO direction */
	direction = (rtw_read8(adapter, REG_GPIO_PIN_CTRL + 2) & BIT(gpio_num)) >> gpio_num;
	if (direction) {
		RTW_PRINT("%s Can't register output gpio as interrupt.\n", __FUNCTION__);
		return -1;
	}

	/* Config GPIO Mode */
	rtw_write8(adapter, REG_GPIO_PIN_CTRL + 3, rtw_read8(adapter, REG_GPIO_PIN_CTRL + 3) | BIT(gpio_num));

	/* Register GPIO interrupt handler*/
	adapter->gpiointpriv.callback[gpio_num] = callback;

	/* Set GPIO interrupt mode, 0:positive edge, 1:negative edge */
	value = rtw_read8(adapter, REG_GPIO_PIN_CTRL) & BIT(gpio_num);
	adapter->gpiointpriv.interrupt_mode = rtw_read8(adapter, REG_HSIMR + 2) ^ value;
	rtw_write8(adapter, REG_GPIO_INTM, adapter->gpiointpriv.interrupt_mode);

	/* Enable GPIO interrupt */
	adapter->gpiointpriv.interrupt_enable_mask = rtw_read8(adapter, REG_HSIMR + 2) | BIT(gpio_num);
	rtw_write8(adapter, REG_HSIMR + 2, adapter->gpiointpriv.interrupt_enable_mask);

	rtw_hal_update_hisr_hsisr_ind(adapter, 1);

	rtw_ps_deny_cancel(adapter, PS_DENY_IOCTL);

	return 0;
}
int rtw_hal_disable_gpio_interrupt(_adapter *adapter, int gpio_num)
{
	u8 value;
	u8 direction;
	PHAL_DATA_TYPE phal = GET_HAL_DATA(adapter);

	if (IS_HARDWARE_TYPE_8188E(adapter)) {
		if (gpio_num > 7 || gpio_num < 4) {
			RTW_INFO("%s The gpio number does not included 4~7.\n", __FUNCTION__);
			return -1;
		}
	}

	rtw_ps_deny(adapter, PS_DENY_IOCTL);

	LeaveAllPowerSaveModeDirect(adapter);

	/* Config GPIO Mode */
	rtw_write8(adapter, REG_GPIO_PIN_CTRL + 3, rtw_read8(adapter, REG_GPIO_PIN_CTRL + 3) & ~BIT(gpio_num));

	/* Unregister GPIO interrupt handler*/
	adapter->gpiointpriv.callback[gpio_num] = NULL;

	/* Reset GPIO interrupt mode, 0:positive edge, 1:negative edge */
	adapter->gpiointpriv.interrupt_mode = rtw_read8(adapter, REG_GPIO_INTM) & ~BIT(gpio_num);
	rtw_write8(adapter, REG_GPIO_INTM, 0x00);

	/* Disable GPIO interrupt */
	adapter->gpiointpriv.interrupt_enable_mask = rtw_read8(adapter, REG_HSIMR + 2) & ~BIT(gpio_num);
	rtw_write8(adapter, REG_HSIMR + 2, adapter->gpiointpriv.interrupt_enable_mask);

	if (!adapter->gpiointpriv.interrupt_enable_mask)
		rtw_hal_update_hisr_hsisr_ind(adapter, 0);

	rtw_ps_deny_cancel(adapter, PS_DENY_IOCTL);

	return 0;
}
#endif

s8 rtw_hal_ch_sw_iqk_info_search(_adapter *padapter, u8 central_chnl, u8 bw_mode)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	u8 i;

	for (i = 0; i < MAX_IQK_INFO_BACKUP_CHNL_NUM; i++) {
		if ((pHalData->iqk_reg_backup[i].central_chnl != 0)) {
			if ((pHalData->iqk_reg_backup[i].central_chnl == central_chnl)
			    && (pHalData->iqk_reg_backup[i].bw_mode == bw_mode))
				return i;
		}
	}

	return -1;
}

void rtw_hal_ch_sw_iqk_info_backup(_adapter *padapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	s8 res;
	u8 i;

	/* If it's an existed record, overwrite it */
	res = rtw_hal_ch_sw_iqk_info_search(padapter, pHalData->current_channel, pHalData->current_channel_bw);
	if ((res >= 0) && (res < MAX_IQK_INFO_BACKUP_CHNL_NUM)) {
		rtw_hal_set_hwreg(padapter, HW_VAR_CH_SW_IQK_INFO_BACKUP, (u8 *)&(pHalData->iqk_reg_backup[res]));
		return;
	}

	/* Search for the empty record to use */
	for (i = 0; i < MAX_IQK_INFO_BACKUP_CHNL_NUM; i++) {
		if (pHalData->iqk_reg_backup[i].central_chnl == 0) {
			rtw_hal_set_hwreg(padapter, HW_VAR_CH_SW_IQK_INFO_BACKUP, (u8 *)&(pHalData->iqk_reg_backup[i]));
			return;
		}
	}

	/* Else, overwrite the oldest record */
	for (i = 1; i < MAX_IQK_INFO_BACKUP_CHNL_NUM; i++)
		_rtw_memcpy(&(pHalData->iqk_reg_backup[i - 1]), &(pHalData->iqk_reg_backup[i]), sizeof(struct hal_iqk_reg_backup));

	rtw_hal_set_hwreg(padapter, HW_VAR_CH_SW_IQK_INFO_BACKUP, (u8 *)&(pHalData->iqk_reg_backup[MAX_IQK_INFO_BACKUP_CHNL_NUM - 1]));
}

void rtw_hal_ch_sw_iqk_info_restore(_adapter *padapter, u8 ch_sw_use_case)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_CH_SW_IQK_INFO_RESTORE, &ch_sw_use_case);
}

void rtw_dump_mac_rx_counters(_adapter *padapter, struct dbg_rx_counter *rx_counter)
{
	u32	mac_cck_ok = 0, mac_ofdm_ok = 0, mac_ht_ok = 0, mac_vht_ok = 0;
	u32	mac_cck_err = 0, mac_ofdm_err = 0, mac_ht_err = 0, mac_vht_err = 0;
	u32	mac_cck_fa = 0, mac_ofdm_fa = 0, mac_ht_fa = 0;
	u32	DropPacket = 0;

	if (!rx_counter) {
		rtw_warn_on(1);
		return;
	}
	if (IS_HARDWARE_TYPE_JAGUAR(padapter) || IS_HARDWARE_TYPE_JAGUAR2(padapter))
		phy_set_mac_reg(padapter, REG_RXERR_RPT, BIT26, 0x0);/*clear bit-26*/

	phy_set_mac_reg(padapter, REG_RXERR_RPT, BIT28 | BIT29 | BIT30 | BIT31, 0x3);
	mac_cck_ok	= phy_query_mac_reg(padapter, REG_RXERR_RPT, bMaskLWord);/* [15:0]	  */
	phy_set_mac_reg(padapter, REG_RXERR_RPT, BIT28 | BIT29 | BIT30 | BIT31, 0x0);
	mac_ofdm_ok	= phy_query_mac_reg(padapter, REG_RXERR_RPT, bMaskLWord);/* [15:0]	 */
	phy_set_mac_reg(padapter, REG_RXERR_RPT, BIT28 | BIT29 | BIT30 | BIT31, 0x6);
	mac_ht_ok	= phy_query_mac_reg(padapter, REG_RXERR_RPT, bMaskLWord);/* [15:0]	 */
	mac_vht_ok	= 0;
	if (IS_HARDWARE_TYPE_JAGUAR(padapter) || IS_HARDWARE_TYPE_JAGUAR2(padapter)) {
		phy_set_mac_reg(padapter, REG_RXERR_RPT, BIT28 | BIT29 | BIT30 | BIT31, 0x0);
		phy_set_mac_reg(padapter, REG_RXERR_RPT, BIT26, 0x1);
		mac_vht_ok	= phy_query_mac_reg(padapter, REG_RXERR_RPT, bMaskLWord);/* [15:0]*/
		phy_set_mac_reg(padapter, REG_RXERR_RPT, BIT26, 0x0);/*clear bit-26*/
	}

	phy_set_mac_reg(padapter, REG_RXERR_RPT, BIT28 | BIT29 | BIT30 | BIT31, 0x4);
	mac_cck_err	= phy_query_mac_reg(padapter, REG_RXERR_RPT, bMaskLWord);/* [15:0]	 */
	phy_set_mac_reg(padapter, REG_RXERR_RPT, BIT28 | BIT29 | BIT30 | BIT31, 0x1);
	mac_ofdm_err	= phy_query_mac_reg(padapter, REG_RXERR_RPT, bMaskLWord);/* [15:0]	 */
	phy_set_mac_reg(padapter, REG_RXERR_RPT, BIT28 | BIT29 | BIT30 | BIT31, 0x7);
	mac_ht_err	= phy_query_mac_reg(padapter, REG_RXERR_RPT, bMaskLWord);/* [15:0]		 */
	mac_vht_err	= 0;
	if (IS_HARDWARE_TYPE_JAGUAR(padapter) || IS_HARDWARE_TYPE_JAGUAR2(padapter)) {
		phy_set_mac_reg(padapter, REG_RXERR_RPT, BIT28 | BIT29 | BIT30 | BIT31, 0x1);
		phy_set_mac_reg(padapter, REG_RXERR_RPT, BIT26, 0x1);
		mac_vht_err	= phy_query_mac_reg(padapter, REG_RXERR_RPT, bMaskLWord);/* [15:0]*/
		phy_set_mac_reg(padapter, REG_RXERR_RPT, BIT26, 0x0);/*clear bit-26*/
	}

	phy_set_mac_reg(padapter, REG_RXERR_RPT, BIT28 | BIT29 | BIT30 | BIT31, 0x5);
	mac_cck_fa	= phy_query_mac_reg(padapter, REG_RXERR_RPT, bMaskLWord);/* [15:0]	 */
	phy_set_mac_reg(padapter, REG_RXERR_RPT, BIT28 | BIT29 | BIT30 | BIT31, 0x2);
	mac_ofdm_fa	= phy_query_mac_reg(padapter, REG_RXERR_RPT, bMaskLWord);/* [15:0]	 */
	phy_set_mac_reg(padapter, REG_RXERR_RPT, BIT28 | BIT29 | BIT30 | BIT31, 0x9);
	mac_ht_fa	= phy_query_mac_reg(padapter, REG_RXERR_RPT, bMaskLWord);/* [15:0]		 */

	/* Mac_DropPacket */
	rtw_write32(padapter, REG_RXERR_RPT, (rtw_read32(padapter, REG_RXERR_RPT) & 0x0FFFFFFF) | Mac_DropPacket);
	DropPacket = rtw_read32(padapter, REG_RXERR_RPT) & 0x0000FFFF;

	rx_counter->rx_pkt_ok = mac_cck_ok + mac_ofdm_ok + mac_ht_ok + mac_vht_ok;
	rx_counter->rx_pkt_crc_error = mac_cck_err + mac_ofdm_err + mac_ht_err + mac_vht_err;
	rx_counter->rx_cck_fa = mac_cck_fa;
	rx_counter->rx_ofdm_fa = mac_ofdm_fa;
	rx_counter->rx_ht_fa = mac_ht_fa;
	rx_counter->rx_pkt_drop = DropPacket;
}
void rtw_reset_mac_rx_counters(_adapter *padapter)
{

	/* If no packet rx, MaxRx clock be gating ,BIT_DISGCLK bit19 set 1 for fix*/
	if (IS_HARDWARE_TYPE_8703B(padapter) ||
	    IS_HARDWARE_TYPE_8723D(padapter) ||
	    IS_HARDWARE_TYPE_8188F(padapter))
		phy_set_mac_reg(padapter, REG_RCR, BIT19, 0x1);

	/* reset mac counter */
	phy_set_mac_reg(padapter, REG_RXERR_RPT, BIT27, 0x1);
	phy_set_mac_reg(padapter, REG_RXERR_RPT, BIT27, 0x0);
}

void rtw_dump_phy_rx_counters(_adapter *padapter, struct dbg_rx_counter *rx_counter)
{
	u32 cckok = 0, cckcrc = 0, ofdmok = 0, ofdmcrc = 0, htok = 0, htcrc = 0, OFDM_FA = 0, CCK_FA = 0, vht_ok = 0, vht_err = 0;
	if (!rx_counter) {
		rtw_warn_on(1);
		return;
	}
	if (IS_HARDWARE_TYPE_JAGUAR(padapter) || IS_HARDWARE_TYPE_JAGUAR2(padapter)) {
		cckok	= phy_query_bb_reg(padapter, 0xF04, 0x3FFF);	     /* [13:0] */
		ofdmok	= phy_query_bb_reg(padapter, 0xF14, 0x3FFF);	     /* [13:0] */
		htok		= phy_query_bb_reg(padapter, 0xF10, 0x3FFF);     /* [13:0] */
		vht_ok	= phy_query_bb_reg(padapter, 0xF0C, 0x3FFF);     /* [13:0] */
		cckcrc	= phy_query_bb_reg(padapter, 0xF04, 0x3FFF0000); /* [29:16]	 */
		ofdmcrc	= phy_query_bb_reg(padapter, 0xF14, 0x3FFF0000); /* [29:16] */
		htcrc	= phy_query_bb_reg(padapter, 0xF10, 0x3FFF0000); /* [29:16] */
		vht_err	= phy_query_bb_reg(padapter, 0xF0C, 0x3FFF0000); /* [29:16] */
		CCK_FA	= phy_query_bb_reg(padapter, 0xA5C, bMaskLWord);
		OFDM_FA	= phy_query_bb_reg(padapter, 0xF48, bMaskLWord);
	} else {
		cckok	= phy_query_bb_reg(padapter, 0xF88, bMaskDWord);
		ofdmok	= phy_query_bb_reg(padapter, 0xF94, bMaskLWord);
		htok		= phy_query_bb_reg(padapter, 0xF90, bMaskLWord);
		vht_ok	= 0;
		cckcrc	= phy_query_bb_reg(padapter, 0xF84, bMaskDWord);
		ofdmcrc	= phy_query_bb_reg(padapter, 0xF94, bMaskHWord);
		htcrc	= phy_query_bb_reg(padapter, 0xF90, bMaskHWord);
		vht_err	= 0;
		OFDM_FA = phy_query_bb_reg(padapter, 0xCF0, bMaskLWord) + phy_query_bb_reg(padapter, 0xCF2, bMaskLWord) +
			phy_query_bb_reg(padapter, 0xDA2, bMaskLWord) + phy_query_bb_reg(padapter, 0xDA4, bMaskLWord) +
			phy_query_bb_reg(padapter, 0xDA6, bMaskLWord) + phy_query_bb_reg(padapter, 0xDA8, bMaskLWord);

		CCK_FA = (rtw_read8(padapter, 0xA5B) << 8) | (rtw_read8(padapter, 0xA5C));
	}

	rx_counter->rx_pkt_ok = cckok + ofdmok + htok + vht_ok;
	rx_counter->rx_pkt_crc_error = cckcrc + ofdmcrc + htcrc + vht_err;
	rx_counter->rx_ofdm_fa = OFDM_FA;
	rx_counter->rx_cck_fa = CCK_FA;

}

void rtw_reset_phy_trx_ok_counters(_adapter *padapter)
{
	if (IS_HARDWARE_TYPE_JAGUAR(padapter) || IS_HARDWARE_TYPE_JAGUAR2(padapter)) {
		phy_set_bb_reg(padapter, 0xB58, BIT0, 0x1);
		phy_set_bb_reg(padapter, 0xB58, BIT0, 0x0);
	}
}
void rtw_reset_phy_rx_counters(_adapter *padapter)
{
	/* reset phy counter */
	if (IS_HARDWARE_TYPE_JAGUAR(padapter) || IS_HARDWARE_TYPE_JAGUAR2(padapter)) {
		rtw_reset_phy_trx_ok_counters(padapter);

		phy_set_bb_reg(padapter, 0x9A4, BIT17, 0x1);/* reset  OFDA FA counter */
		phy_set_bb_reg(padapter, 0x9A4, BIT17, 0x0);

		phy_set_bb_reg(padapter, 0xA2C, BIT15, 0x0);/* reset  CCK FA counter */
		phy_set_bb_reg(padapter, 0xA2C, BIT15, 0x1);
	} else {
		phy_set_bb_reg(padapter, 0xF14, BIT16, 0x1);
		rtw_msleep_os(10);
		phy_set_bb_reg(padapter, 0xF14, BIT16, 0x0);

		phy_set_bb_reg(padapter, 0xD00, BIT27, 0x1);/* reset  OFDA FA counter */
		phy_set_bb_reg(padapter, 0xC0C, BIT31, 0x1);/* reset  OFDA FA counter */
		phy_set_bb_reg(padapter, 0xD00, BIT27, 0x0);
		phy_set_bb_reg(padapter, 0xC0C, BIT31, 0x0);

		phy_set_bb_reg(padapter, 0xA2C, BIT15, 0x0);/* reset  CCK FA counter */
		phy_set_bb_reg(padapter, 0xA2C, BIT15, 0x1);
	}
}
#ifdef DBG_RX_COUNTER_DUMP
void rtw_dump_drv_rx_counters(_adapter *padapter, struct dbg_rx_counter *rx_counter)
{
	struct recv_priv *precvpriv = &padapter->recvpriv;
	if (!rx_counter) {
		rtw_warn_on(1);
		return;
	}
	rx_counter->rx_pkt_ok = padapter->drv_rx_cnt_ok;
	rx_counter->rx_pkt_crc_error = padapter->drv_rx_cnt_crcerror;
	rx_counter->rx_pkt_drop = precvpriv->rx_drop - padapter->drv_rx_cnt_drop;
}
void rtw_reset_drv_rx_counters(_adapter *padapter)
{
	struct recv_priv *precvpriv = &padapter->recvpriv;
	padapter->drv_rx_cnt_ok = 0;
	padapter->drv_rx_cnt_crcerror = 0;
	padapter->drv_rx_cnt_drop = precvpriv->rx_drop;
}
void rtw_dump_phy_rxcnts_preprocess(_adapter *padapter, u8 rx_cnt_mode)
{
	u8 initialgain;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(padapter);

	if ((!(padapter->dump_rx_cnt_mode & DUMP_PHY_RX_COUNTER)) && (rx_cnt_mode & DUMP_PHY_RX_COUNTER)) {
		rtw_hal_get_odm_var(padapter, HAL_ODM_INITIAL_GAIN, &initialgain, NULL);
		RTW_INFO("%s CurIGValue:0x%02x\n", __FUNCTION__, initialgain);
		rtw_hal_set_odm_var(padapter, HAL_ODM_INITIAL_GAIN, &initialgain, _FALSE);
		/*disable dynamic functions, such as high power, DIG*/
		rtw_phydm_ability_backup(padapter);
		rtw_phydm_func_clr(padapter, (ODM_BB_DIG | ODM_BB_FA_CNT));
	} else if ((padapter->dump_rx_cnt_mode & DUMP_PHY_RX_COUNTER) && (!(rx_cnt_mode & DUMP_PHY_RX_COUNTER))) {
		/* turn on phy-dynamic functions */
		rtw_phydm_ability_restore(padapter);
		initialgain = 0xff; /* restore RX GAIN */
		rtw_hal_set_odm_var(padapter, HAL_ODM_INITIAL_GAIN, &initialgain, _FALSE);

	}
}

void rtw_dump_rx_counters(_adapter *padapter)
{
	struct dbg_rx_counter rx_counter;

	if (padapter->dump_rx_cnt_mode & DUMP_DRV_RX_COUNTER) {
		_rtw_memset(&rx_counter, 0, sizeof(struct dbg_rx_counter));
		rtw_dump_drv_rx_counters(padapter, &rx_counter);
		RTW_INFO("Drv Received packet OK:%d CRC error:%d Drop Packets: %d\n",
			rx_counter.rx_pkt_ok, rx_counter.rx_pkt_crc_error, rx_counter.rx_pkt_drop);
		rtw_reset_drv_rx_counters(padapter);
	}

	if (padapter->dump_rx_cnt_mode & DUMP_MAC_RX_COUNTER) {
		_rtw_memset(&rx_counter, 0, sizeof(struct dbg_rx_counter));
		rtw_dump_mac_rx_counters(padapter, &rx_counter);
		RTW_INFO("Mac Received packet OK:%d CRC error:%d FA Counter: %d Drop Packets: %d\n",
			 rx_counter.rx_pkt_ok, rx_counter.rx_pkt_crc_error,
			rx_counter.rx_cck_fa + rx_counter.rx_ofdm_fa + rx_counter.rx_ht_fa,
			 rx_counter.rx_pkt_drop);
		rtw_reset_mac_rx_counters(padapter);
	}

	if (padapter->dump_rx_cnt_mode & DUMP_PHY_RX_COUNTER) {
		_rtw_memset(&rx_counter, 0, sizeof(struct dbg_rx_counter));
		rtw_dump_phy_rx_counters(padapter, &rx_counter);
		/* RTW_INFO("%s: OFDM_FA =%d\n", __FUNCTION__, rx_counter.rx_ofdm_fa); */
		/* RTW_INFO("%s: CCK_FA =%d\n", __FUNCTION__, rx_counter.rx_cck_fa); */
		RTW_INFO("Phy Received packet OK:%d CRC error:%d FA Counter: %d\n", rx_counter.rx_pkt_ok, rx_counter.rx_pkt_crc_error,
			 rx_counter.rx_ofdm_fa + rx_counter.rx_cck_fa);
		rtw_reset_phy_rx_counters(padapter);
	}
}
#endif
void rtw_get_noise(_adapter *padapter)
{
#if defined(CONFIG_SIGNAL_DISPLAY_DBM) && defined(CONFIG_BACKGROUND_NOISE_MONITOR)
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct noise_info info;
	if (rtw_linked_check(padapter)) {
		info.bPauseDIG = _TRUE;
		info.IGIValue = 0x1e;
		info.max_time = 100;/* ms */
		info.chan = pmlmeext->cur_channel ;/* rtw_get_oper_ch(padapter); */
		rtw_ps_deny(padapter, PS_DENY_IOCTL);
		LeaveAllPowerSaveModeDirect(padapter);

		rtw_hal_set_odm_var(padapter, HAL_ODM_NOISE_MONITOR, &info, _FALSE);
		/* odm_inband_noise_monitor(podmpriv,_TRUE,0x20,100); */
		rtw_ps_deny_cancel(padapter, PS_DENY_IOCTL);
		rtw_hal_get_odm_var(padapter, HAL_ODM_NOISE_MONITOR, &(info.chan), &(padapter->recvpriv.noise));
#ifdef DBG_NOISE_MONITOR
		RTW_INFO("chan:%d,noise_level:%d\n", info.chan, padapter->recvpriv.noise);
#endif
	}
#endif

}
u8 rtw_get_current_tx_sgi(_adapter *padapter, u8 macid)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct PHY_DM_STRUCT		*pDM_Odm = &pHalData->odmpriv;
	struct _rate_adaptive_table_			*pRA_Table = &pDM_Odm->dm_ra_table;
	u8 curr_tx_sgi = 0;

#if defined(CONFIG_RTL8188E)
	curr_tx_sgi = odm_ra_get_decision_rate_8188e(pDM_Odm, macid);
#else
	curr_tx_sgi = ((pRA_Table->link_tx_rate[macid]) & 0x80) >> 7;
#endif

	return curr_tx_sgi;

}
u8 rtw_get_current_tx_rate(_adapter *padapter, u8 macid)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct PHY_DM_STRUCT		*pDM_Odm = &pHalData->odmpriv;
	struct _rate_adaptive_table_			*pRA_Table = &pDM_Odm->dm_ra_table;
	u8 rate_id = 0;

#if (RATE_ADAPTIVE_SUPPORT == 1)
	rate_id = odm_ra_get_decision_rate_8188e(pDM_Odm, macid);
#else
	rate_id = (pRA_Table->link_tx_rate[macid]) & 0x7f;
#endif

	return rate_id;

}

void update_IOT_info(_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	switch (pmlmeinfo->assoc_AP_vendor) {
	case HT_IOT_PEER_MARVELL:
		pmlmeinfo->turboMode_cts2self = 1;
		pmlmeinfo->turboMode_rtsen = 0;
		break;

	case HT_IOT_PEER_RALINK:
		pmlmeinfo->turboMode_cts2self = 0;
		pmlmeinfo->turboMode_rtsen = 1;
		/* disable high power			 */
		rtw_phydm_func_clr(padapter, ODM_BB_DYNAMIC_TXPWR);
		break;
	case HT_IOT_PEER_REALTEK:
		/* rtw_write16(padapter, 0x4cc, 0xffff); */
		/* rtw_write16(padapter, 0x546, 0x01c0); */
		/* disable high power			 */
		rtw_phydm_func_clr(padapter, ODM_BB_DYNAMIC_TXPWR);
		break;
	default:
		pmlmeinfo->turboMode_cts2self = 0;
		pmlmeinfo->turboMode_rtsen = 1;
		break;
	}

}
#ifdef CONFIG_AUTO_CHNL_SEL_NHM
void rtw_acs_start(_adapter *padapter, bool bStart)
{
	if (_TRUE == bStart) {
		ACS_OP acs_op = ACS_INIT;

		rtw_hal_set_odm_var(padapter, HAL_ODM_AUTO_CHNL_SEL, &acs_op, _TRUE);
		rtw_set_acs_channel(padapter, 0);
		SET_ACS_STATE(padapter, ACS_ENABLE);
	} else {
		SET_ACS_STATE(padapter, ACS_DISABLE);
#ifdef DBG_AUTO_CHNL_SEL_NHM
		if (1) {
			u8 best_24g_ch = 0;
			u8 best_5g_ch = 0;

			rtw_hal_get_odm_var(padapter, HAL_ODM_AUTO_CHNL_SEL, &(best_24g_ch), &(best_5g_ch));
			RTW_INFO("[ACS-"ADPT_FMT"] Best 2.4G CH:%u\n", ADPT_ARG(padapter), best_24g_ch);
			RTW_INFO("[ACS-"ADPT_FMT"] Best 5G CH:%u\n", ADPT_ARG(padapter), best_5g_ch);
		}
#endif
	}
}
#endif

/* TODO: merge with phydm, see odm_SetCrystalCap() */
void hal_set_crystal_cap(_adapter *adapter, u8 crystal_cap)
{
	crystal_cap = crystal_cap & 0x3F;

	switch (rtw_get_chip_type(adapter)) {
#if defined(CONFIG_RTL8188E) || defined(CONFIG_RTL8188F)
	case RTL8188E:
	case RTL8188F:
		/* write 0x24[16:11] = 0x24[22:17] = CrystalCap */
		phy_set_bb_reg(adapter, REG_AFE_XTAL_CTRL, 0x007FF800, (crystal_cap | (crystal_cap << 6)));
		break;
#endif
#if defined(CONFIG_RTL8812A)
	case RTL8812:
		/* write 0x2C[30:25] = 0x2C[24:19] = CrystalCap */
		phy_set_bb_reg(adapter, REG_MAC_PHY_CTRL, 0x7FF80000, (crystal_cap | (crystal_cap << 6)));
		break;
#endif
#if defined(CONFIG_RTL8723B) || defined(CONFIG_RTL8703B) || \
		defined(CONFIG_RTL8723D) || defined(CONFIG_RTL8821A) || \
		defined(CONFIG_RTL8192E)
	case RTL8723B:
	case RTL8703B:
	case RTL8723D:
	case RTL8821:
	case RTL8192E:
		/* write 0x2C[23:18] = 0x2C[17:12] = CrystalCap */
		phy_set_bb_reg(adapter, REG_MAC_PHY_CTRL, 0x00FFF000, (crystal_cap | (crystal_cap << 6)));
		break;
#endif
#if defined(CONFIG_RTL8814A)
	case RTL8814A:
		/* write 0x2C[26:21] = 0x2C[20:15] = CrystalCap*/
		phy_set_bb_reg(adapter, REG_MAC_PHY_CTRL, 0x07FF8000, (crystal_cap | (crystal_cap << 6)));
		break;
#endif
#if defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C)

	case RTL8822B:
	case RTL8821C:
		/* write 0x28[6:1] = 0x24[30:25] = CrystalCap */
		crystal_cap = crystal_cap & 0x3F;
		phy_set_bb_reg(adapter, REG_AFE_XTAL_CTRL, 0x7E000000, crystal_cap);
		phy_set_bb_reg(adapter, REG_AFE_PLL_CTRL, 0x7E, crystal_cap);
		break;
#endif
	default:
		rtw_warn_on(1);
	}
}

int hal_spec_init(_adapter *adapter)
{
	u8 interface_type = 0;
	int ret = _SUCCESS;

	interface_type = rtw_get_intf_type(adapter);

	switch (rtw_get_chip_type(adapter)) {
#ifdef CONFIG_RTL8723B
	case RTL8723B:
		init_hal_spec_8723b(adapter);
		break;
#endif
#ifdef CONFIG_RTL8703B
	case RTL8703B:
		init_hal_spec_8703b(adapter);
		break;
#endif
#ifdef CONFIG_RTL8723D
	case RTL8723D:
		init_hal_spec_8723d(adapter);
		break;
#endif
#ifdef CONFIG_RTL8188E
	case RTL8188E:
		init_hal_spec_8188e(adapter);
		break;
#endif
#ifdef CONFIG_RTL8188F
	case RTL8188F:
		init_hal_spec_8188f(adapter);
		break;
#endif
#ifdef CONFIG_RTL8812A
	case RTL8812:
		init_hal_spec_8812a(adapter);
		break;
#endif
#ifdef CONFIG_RTL8821A
	case RTL8821:
		init_hal_spec_8821a(adapter);
		break;
#endif
#ifdef CONFIG_RTL8192E
	case RTL8192E:
		init_hal_spec_8192e(adapter);
		break;
#endif
#ifdef CONFIG_RTL8814A
	case RTL8814A:
		init_hal_spec_8814a(adapter);
		break;
#endif
#ifdef CONFIG_RTL8822B
	case RTL8822B:
		rtl8822b_init_hal_spec(adapter);
		break;
#endif
#ifdef CONFIG_RTL8821C
	case RTL8821C:
		init_hal_spec_rtl8821c(adapter);
		break;
#endif
	default:
		RTW_ERR("%s: unknown chip_type:%u\n"
			, __func__, rtw_get_chip_type(adapter));
		ret = _FAIL;
		break;
	}

	return ret;
}

static const char *const _band_cap_str[] = {
	/* BIT0 */"2G",
	/* BIT1 */"5G",
};

static const char *const _bw_cap_str[] = {
	/* BIT0 */"5M",
	/* BIT1 */"10M",
	/* BIT2 */"20M",
	/* BIT3 */"40M",
	/* BIT4 */"80M",
	/* BIT5 */"160M",
	/* BIT6 */"80_80M",
};

static const char *const _proto_cap_str[] = {
	/* BIT0 */"b",
	/* BIT1 */"g",
	/* BIT2 */"n",
	/* BIT3 */"ac",
};

static const char *const _wl_func_str[] = {
	/* BIT0 */"P2P",
	/* BIT1 */"MIRACAST",
	/* BIT2 */"TDLS",
	/* BIT3 */"FTM",
};

void dump_hal_spec(void *sel, _adapter *adapter)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	int i;

	RTW_PRINT_SEL(sel, "macid_num:%u\n", hal_spec->macid_num);
	RTW_PRINT_SEL(sel, "sec_cap:0x%02x\n", hal_spec->sec_cap);
	RTW_PRINT_SEL(sel, "sec_cam_ent_num:%u\n", hal_spec->sec_cam_ent_num);
	RTW_PRINT_SEL(sel, "rfpath_num_2g:%u\n", hal_spec->rfpath_num_2g);
	RTW_PRINT_SEL(sel, "rfpath_num_5g:%u\n", hal_spec->rfpath_num_5g);
	RTW_PRINT_SEL(sel, "max_tx_cnt:%u\n", hal_spec->max_tx_cnt);
	RTW_PRINT_SEL(sel, "tx_nss_num:%u\n", hal_spec->tx_nss_num);
	RTW_PRINT_SEL(sel, "rx_nss_num:%u\n", hal_spec->rx_nss_num);

	RTW_PRINT_SEL(sel, "band_cap:");
	for (i = 0; i < BAND_CAP_BIT_NUM; i++) {
		if (((hal_spec->band_cap) >> i) & BIT0 && _band_cap_str[i])
			_RTW_PRINT_SEL(sel, "%s ", _band_cap_str[i]);
	}
	_RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "bw_cap:");
	for (i = 0; i < BW_CAP_BIT_NUM; i++) {
		if (((hal_spec->bw_cap) >> i) & BIT0 && _bw_cap_str[i])
			_RTW_PRINT_SEL(sel, "%s ", _bw_cap_str[i]);
	}
	_RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "proto_cap:");
	for (i = 0; i < PROTO_CAP_BIT_NUM; i++) {
		if (((hal_spec->proto_cap) >> i) & BIT0 && _proto_cap_str[i])
			_RTW_PRINT_SEL(sel, "%s ", _proto_cap_str[i]);
	}
	_RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "wl_func:");
	for (i = 0; i < WL_FUNC_BIT_NUM; i++) {
		if (((hal_spec->wl_func) >> i) & BIT0 && _wl_func_str[i])
			_RTW_PRINT_SEL(sel, "%s ", _wl_func_str[i]);
	}
	_RTW_PRINT_SEL(sel, "\n");
}

inline bool hal_chk_band_cap(_adapter *adapter, u8 cap)
{
	return GET_HAL_SPEC(adapter)->band_cap & cap;
}

inline bool hal_chk_bw_cap(_adapter *adapter, u8 cap)
{
	return GET_HAL_SPEC(adapter)->bw_cap & cap;
}

inline bool hal_chk_proto_cap(_adapter *adapter, u8 cap)
{
	return GET_HAL_SPEC(adapter)->proto_cap & cap;
}

inline bool hal_chk_wl_func(_adapter *adapter, u8 func)
{
	return GET_HAL_SPEC(adapter)->wl_func & func;
}

inline bool hal_is_band_support(_adapter *adapter, u8 band)
{
	return GET_HAL_SPEC(adapter)->band_cap & band_to_band_cap(band);
}

inline bool hal_is_bw_support(_adapter *adapter, u8 bw)
{
	return GET_HAL_SPEC(adapter)->bw_cap & ch_width_to_bw_cap(bw);
}

inline bool hal_is_wireless_mode_support(_adapter *adapter, u8 mode)
{
	u8 proto_cap = GET_HAL_SPEC(adapter)->proto_cap;

	if (mode == WIRELESS_11B)
		if ((proto_cap & PROTO_CAP_11B) && hal_chk_band_cap(adapter, BAND_CAP_2G))
			return 1;

	if (mode == WIRELESS_11G)
		if ((proto_cap & PROTO_CAP_11G) && hal_chk_band_cap(adapter, BAND_CAP_2G))
			return 1;

	if (mode == WIRELESS_11A)
		if ((proto_cap & PROTO_CAP_11G) && hal_chk_band_cap(adapter, BAND_CAP_5G))
			return 1;

	if (mode == WIRELESS_11_24N)
		if ((proto_cap & PROTO_CAP_11N) && hal_chk_band_cap(adapter, BAND_CAP_2G))
			return 1;

	if (mode == WIRELESS_11_5N)
		if ((proto_cap & PROTO_CAP_11N) && hal_chk_band_cap(adapter, BAND_CAP_5G))
			return 1;

	if (mode == WIRELESS_11AC)
		if ((proto_cap & PROTO_CAP_11AC) && hal_chk_band_cap(adapter, BAND_CAP_5G))
			return 1;

	return 0;
}

/*
* hal_largest_bw - starting from in_bw, get largest bw supported by HAL
* @adapter:
* @in_bw: starting bw, value of CHANNEL_WIDTH
*
* Returns: value of CHANNEL_WIDTH
*/
u8 hal_largest_bw(_adapter *adapter, u8 in_bw)
{
	for (; in_bw > CHANNEL_WIDTH_20; in_bw--) {
		if (hal_is_bw_support(adapter, in_bw))
			break;
	}

	if (!hal_is_bw_support(adapter, in_bw))
		rtw_warn_on(1);

	return in_bw;
}

void rtw_hal_correct_tsf(_adapter *padapter, u8 hw_port, u64 tsf)
{
	if (hw_port == HW_PORT0) {
		/*disable related TSF function*/
		rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL) & (~EN_BCN_FUNCTION));

		rtw_write32(padapter, REG_TSFTR, tsf);
		rtw_write32(padapter, REG_TSFTR + 4, tsf >> 32);

		/*enable related TSF function*/
		rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL) | EN_BCN_FUNCTION);
	} else if (hw_port == HW_PORT1) {
		/*disable related TSF function*/
		rtw_write8(padapter, REG_BCN_CTRL_1, rtw_read8(padapter, REG_BCN_CTRL_1) & (~EN_BCN_FUNCTION));

		rtw_write32(padapter, REG_TSFTR1, tsf);
		rtw_write32(padapter, REG_TSFTR1 + 4, tsf >> 32);

		/*enable related TSF function*/
		rtw_write8(padapter, REG_BCN_CTRL_1, rtw_read8(padapter, REG_BCN_CTRL_1) | EN_BCN_FUNCTION);
	} else
		RTW_INFO("%s-[WARN] "ADPT_FMT" invalid hw_port:%d\n", __func__, ADPT_ARG(padapter), hw_port);
}

void ResumeTxBeacon(_adapter *padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);


	/* 2010.03.01. Marked by tynli. No need to call workitem beacause we record the value */
	/* which should be read from register to a global variable. */


	pHalData->RegFwHwTxQCtrl |= BIT(6);
	rtw_write8(padapter, REG_FWHW_TXQ_CTRL + 2, pHalData->RegFwHwTxQCtrl);
	/*TBTT hold time :4ms 0x540[19:8]*/
	rtw_write8(padapter, REG_TBTT_PROHIBIT + 1,
		TBTT_PROBIHIT_HOLD_TIME & 0xFF);
	rtw_write8(padapter, REG_TBTT_PROHIBIT + 2,
		(rtw_read8(padapter, REG_TBTT_PROHIBIT + 2) & 0xF0) | (TBTT_PROBIHIT_HOLD_TIME >> 8));
}

void StopTxBeacon(_adapter *padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);


	/* 2010.03.01. Marked by tynli. No need to call workitem beacause we record the value */
	/* which should be read from register to a global variable. */


	pHalData->RegFwHwTxQCtrl &= ~BIT(6);
	rtw_write8(padapter, REG_FWHW_TXQ_CTRL + 2, pHalData->RegFwHwTxQCtrl);
	rtw_write8(padapter, REG_TBTT_PROHIBIT + 1, 0x64);
	rtw_write8(padapter, REG_TBTT_PROHIBIT + 2,
		(rtw_read8(padapter, REG_TBTT_PROHIBIT + 2) & 0xF0));

	/*CheckFwRsvdPageContent(padapter);*/  /* 2010.06.23. Added by tynli. */
}

#ifdef CONFIG_MI_WITH_MBSSID_CAM /*HW port0 - MBSS*/
void hw_var_set_opmode_mbid(_adapter *Adapter, u8 mode)
{
	RTW_INFO("%s()-"ADPT_FMT" mode = %d\n", __func__, ADPT_ARG(Adapter), mode);

	rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR) & (~(RCR_CBSSID_DATA | RCR_CBSSID_BCN)));

	/* disable Port0 TSF update*/
	rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL) | DIS_TSF_UDT);

	/* set net_type */
	Set_MSR(Adapter, mode);

	if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_)) {
		if (!rtw_mi_check_status(Adapter, MI_AP_MODE))
			StopTxBeacon(Adapter);

		rtw_write8(Adapter, REG_BCN_CTRL, DIS_TSF_UDT | EN_BCN_FUNCTION | DIS_ATIM);/*disable atim wnd*/
	} else if (mode == _HW_STATE_ADHOC_) {
		ResumeTxBeacon(Adapter);
		rtw_write8(Adapter, REG_BCN_CTRL, DIS_TSF_UDT | EN_BCN_FUNCTION | DIS_BCNQ_SUB);

	} else if (mode == _HW_STATE_AP_) {
		ResumeTxBeacon(Adapter);

		rtw_write8(Adapter, REG_BCN_CTRL, DIS_TSF_UDT | DIS_BCNQ_SUB);

		/*enable to rx data frame*/
		rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);

		/*Beacon Control related register for first time*/
		rtw_write8(Adapter, REG_BCNDMATIM, 0x02); /* 2ms */

		/*rtw_write8(Adapter, REG_BCN_MAX_ERR, 0xFF);*/
		rtw_write8(Adapter, REG_ATIMWND, 0x0a); /* 10ms */
		rtw_write16(Adapter, REG_BCNTCFG, 0x00);
		rtw_write16(Adapter, REG_TBTT_PROHIBIT, 0xff04);
		rtw_write16(Adapter, REG_TSFTR_SYN_OFFSET, 0x7fff);/* +32767 (~32ms) */

		/*reset TSF*/
		rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(0));

		/*enable BCN0 Function for if1*/
		/*don't enable update TSF0 for if1 (due to TSF update when beacon,probe rsp are received)*/
		rtw_write8(Adapter, REG_BCN_CTRL, (DIS_TSF_UDT | EN_BCN_FUNCTION | EN_TXBCN_RPT | DIS_BCNQ_SUB));

		if (IS_HARDWARE_TYPE_8821(Adapter) || IS_HARDWARE_TYPE_8192E(Adapter))/* select BCN on port 0 for DualBeacon*/
			rtw_write8(Adapter, REG_CCK_CHECK, rtw_read8(Adapter, REG_CCK_CHECK) & (~BIT_BCN_PORT_SEL));

	}

}
#endif

#ifdef CONFIG_ANTENNA_DIVERSITY
u8	rtw_hal_antdiv_before_linked(_adapter *padapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	u8 cur_ant, change_ant;

	if (!pHalData->AntDivCfg)
		return _FALSE;

	if (pHalData->sw_antdiv_bl_state == 0) {
		pHalData->sw_antdiv_bl_state = 1;

		rtw_hal_get_odm_var(padapter, HAL_ODM_ANTDIV_SELECT, &cur_ant, NULL);
		change_ant = (cur_ant == MAIN_ANT) ? AUX_ANT : MAIN_ANT;

		return rtw_antenna_select_cmd(padapter, change_ant, _FALSE);
	}

	pHalData->sw_antdiv_bl_state = 0;
	return _FALSE;
}

void	rtw_hal_antdiv_rssi_compared(_adapter *padapter, WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);

	if (pHalData->AntDivCfg) {
		/*RTW_INFO("update_network=> org-RSSI(%d), new-RSSI(%d)\n", dst->Rssi, src->Rssi);*/
		/*select optimum_antenna for before linked =>For antenna diversity*/
		if (dst->Rssi >=  src->Rssi) {/*keep org parameter*/
			src->Rssi = dst->Rssi;
			src->PhyInfo.Optimum_antenna = dst->PhyInfo.Optimum_antenna;
		}
	}
}
#endif

#ifdef CONFIG_PHY_CAPABILITY_QUERY
void rtw_dump_phy_cap_by_phydmapi(void *sel, _adapter *adapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(adapter);
	struct phy_spec_t *phy_spec = &pHalData->phy_spec;

	RTW_PRINT_SEL(sel, "[PHY SPEC] TRx Capability : 0x%08x\n", phy_spec->trx_cap);
	RTW_PRINT_SEL(sel, "[PHY SPEC] Tx Stream Num Index : %d\n", (phy_spec->trx_cap >> 24) & 0xFF); /*Tx Stream Num Index [31:24]*/
	RTW_PRINT_SEL(sel, "[PHY SPEC] Rx Stream Num Index : %d\n", (phy_spec->trx_cap >> 16) & 0xFF); /*Rx Stream Num Index [23:16]*/
	RTW_PRINT_SEL(sel, "[PHY SPEC] Tx Path Num Index : %d\n", (phy_spec->trx_cap >> 8) & 0xFF);/*Tx Path Num Index	[15:8]*/
	RTW_PRINT_SEL(sel, "[PHY SPEC] Rx Path Num Index : %d\n\n", (phy_spec->trx_cap & 0xFF));/*Rx Path Num Index	[7:0]*/

	RTW_PRINT_SEL(sel, "[PHY SPEC] STBC Capability : 0x%08x\n", phy_spec->stbc_cap);
	RTW_PRINT_SEL(sel, "[PHY SPEC] VHT STBC Tx : %s\n", ((phy_spec->stbc_cap >> 24) & 0xFF) ? "Supported" : "N/A"); /*VHT STBC Tx [31:24]*/
	/*VHT STBC Rx [23:16]
	0 = not support
	1 = support for 1 spatial stream
	2 = support for 1 or 2 spatial streams
	3 = support for 1 or 2 or 3 spatial streams
	4 = support for 1 or 2 or 3 or 4 spatial streams*/
	RTW_PRINT_SEL(sel, "[PHY SPEC] VHT STBC Rx :%d\n", ((phy_spec->stbc_cap >> 16) & 0xFF));
	RTW_PRINT_SEL(sel, "[PHY SPEC] HT STBC Tx : %s\n", ((phy_spec->stbc_cap >> 8) & 0xFF) ? "Supported" : "N/A"); /*HT STBC Tx [15:8]*/
	/*HT STBC Rx [7:0]
	0 = not support
	1 = support for 1 spatial stream
	2 = support for 1 or 2 spatial streams
	3 = support for 1 or 2 or 3 spatial streams*/
	RTW_PRINT_SEL(sel, "[PHY SPEC] HT STBC Rx : %d\n\n", (phy_spec->stbc_cap & 0xFF));

	RTW_PRINT_SEL(sel, "[PHY SPEC] LDPC Capability : 0x%08x\n", phy_spec->ldpc_cap);
	RTW_PRINT_SEL(sel, "[PHY SPEC] VHT LDPC Tx : %s\n", ((phy_spec->ldpc_cap >> 24) & 0xFF) ? "Supported" : "N/A"); /*VHT LDPC Tx [31:24]*/
	RTW_PRINT_SEL(sel, "[PHY SPEC] VHT LDPC Rx : %s\n", ((phy_spec->ldpc_cap >> 16) & 0xFF) ? "Supported" : "N/A"); /*VHT LDPC Rx [23:16]*/
	RTW_PRINT_SEL(sel, "[PHY SPEC] HT LDPC Tx : %s\n", ((phy_spec->ldpc_cap >> 8) & 0xFF) ? "Supported" : "N/A"); /*HT LDPC Tx [15:8]*/
	RTW_PRINT_SEL(sel, "[PHY SPEC] HT LDPC Rx : %s\n\n", (phy_spec->ldpc_cap & 0xFF) ? "Supported" : "N/A"); /*HT LDPC Rx [7:0]*/
	#ifdef CONFIG_BEAMFORMING
	RTW_PRINT_SEL(sel, "[PHY SPEC] TxBF Capability : 0x%08x\n", phy_spec->txbf_cap);
	RTW_PRINT_SEL(sel, "[PHY SPEC] VHT MU Bfer : %s\n", ((phy_spec->txbf_cap >> 28) & 0xF) ? "Supported" : "N/A"); /*VHT MU Bfer [31:28]*/
	RTW_PRINT_SEL(sel, "[PHY SPEC] VHT MU Bfee : %s\n", ((phy_spec->txbf_cap >> 24) & 0xF) ? "Supported" : "N/A"); /*VHT MU Bfee [27:24]*/
	RTW_PRINT_SEL(sel, "[PHY SPEC] VHT SU Bfer : %s\n", ((phy_spec->txbf_cap >> 20) & 0xF) ? "Supported" : "N/A"); /*VHT SU Bfer [23:20]*/
	RTW_PRINT_SEL(sel, "[PHY SPEC] VHT SU Bfee : %s\n", ((phy_spec->txbf_cap >> 16) & 0xF) ? "Supported" : "N/A"); /*VHT SU Bfee [19:16]*/
	RTW_PRINT_SEL(sel, "[PHY SPEC] HT Bfer : %s\n", ((phy_spec->txbf_cap >> 4) & 0xF)  ? "Supported" : "N/A"); /*HT Bfer [7:4]*/
	RTW_PRINT_SEL(sel, "[PHY SPEC] HT Bfee : %s\n\n", (phy_spec->txbf_cap & 0xF) ? "Supported" : "N/A"); /*HT Bfee [3:0]*/

	RTW_PRINT_SEL(sel, "[PHY SPEC] TxBF parameter : 0x%08x\n", phy_spec->txbf_param);
	RTW_PRINT_SEL(sel, "[PHY SPEC] VHT Sounding Dim : %d\n", (phy_spec->txbf_param >> 24) & 0xFF); /*VHT Sounding Dim [31:24]*/
	RTW_PRINT_SEL(sel, "[PHY SPEC] VHT Steering Ant : %d\n", (phy_spec->txbf_param >> 16) & 0xFF); /*VHT Steering Ant [23:16]*/
	RTW_PRINT_SEL(sel, "[PHY SPEC] HT Sounding Dim : %d\n", (phy_spec->txbf_param >> 8) & 0xFF); /*HT Sounding Dim [15:8]*/
	RTW_PRINT_SEL(sel, "[PHY SPEC] HT Steering Ant : %d\n", phy_spec->txbf_param & 0xFF); /*HT Steering Ant [7:0]*/
	#endif
}
#else
void rtw_dump_phy_cap_by_hal(void *sel, _adapter *adapter)
{
	u8 phy_cap = _FALSE;

	/* STBC */
	rtw_hal_get_def_var(adapter, HAL_DEF_TX_STBC, (u8 *)&phy_cap);
	RTW_PRINT_SEL(sel, "[HAL] STBC Tx : %s\n", (_TRUE == phy_cap) ? "Supported" : "N/A");

	phy_cap = _FALSE;
	rtw_hal_get_def_var(adapter, HAL_DEF_RX_STBC, (u8 *)&phy_cap);
	RTW_PRINT_SEL(sel, "[HAL] STBC Rx : %s\n\n", (_TRUE == phy_cap) ? "Supported" : "N/A");

	/* LDPC support */
	phy_cap = _FALSE;
	rtw_hal_get_def_var(adapter, HAL_DEF_TX_LDPC, (u8 *)&phy_cap);
	RTW_PRINT_SEL(sel, "[HAL] LDPC Tx : %s\n", (_TRUE == phy_cap) ? "Supported" : "N/A");

	phy_cap = _FALSE;
	rtw_hal_get_def_var(adapter, HAL_DEF_RX_LDPC, (u8 *)&phy_cap);
	RTW_PRINT_SEL(sel, "[HAL] LDPC Rx : %s\n\n", (_TRUE == phy_cap) ? "Supported" : "N/A");
	
	#ifdef CONFIG_BEAMFORMING
	phy_cap = _FALSE;
	rtw_hal_get_def_var(adapter, HAL_DEF_EXPLICIT_BEAMFORMER, (u8 *)&phy_cap);
	RTW_PRINT_SEL(sel, "[HAL] Beamformer: %s\n", (_TRUE == phy_cap) ? "Supported" : "N/A");

	phy_cap = _FALSE;
	rtw_hal_get_def_var(adapter, HAL_DEF_EXPLICIT_BEAMFORMEE, (u8 *)&phy_cap);
	RTW_PRINT_SEL(sel, "[HAL] Beamformee: %s\n", (_TRUE == phy_cap) ? "Supported" : "N/A");

	phy_cap = _FALSE;
	rtw_hal_get_def_var(adapter, HAL_DEF_VHT_MU_BEAMFORMER, &phy_cap);
	RTW_PRINT_SEL(sel, "[HAL] VHT MU Beamformer: %s\n", (_TRUE == phy_cap) ? "Supported" : "N/A");

	phy_cap = _FALSE;
	rtw_hal_get_def_var(adapter, HAL_DEF_VHT_MU_BEAMFORMEE, &phy_cap);
	RTW_PRINT_SEL(sel, "[HAL] VHT MU Beamformee: %s\n", (_TRUE == phy_cap) ? "Supported" : "N/A");
	#endif
}
#endif
void rtw_dump_phy_cap(void *sel, _adapter *adapter)
{
	RTW_PRINT_SEL(sel, "\n ======== PHY Capability ========\n");
#ifdef CONFIG_PHY_CAPABILITY_QUERY
	rtw_dump_phy_cap_by_phydmapi(sel, adapter);
#else
	rtw_dump_phy_cap_by_hal(sel, adapter);
#endif
}

