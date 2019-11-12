/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
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
	if (hal_data->bFWReady)
		RTW_PRINT_SEL(sel, "FW VER -%d.%d\n", hal_data->firmware_version, hal_data->firmware_sub_version);
	else
		RTW_PRINT_SEL(sel, "FW not ready\n");
}

bool rsvd_page_cache_update_all(struct rsvd_page_cache_t *cache, u8 loc
	, u8 txdesc_len, u32 page_size, u8 *info, u32 info_len)
{
	u8 page_num = (u8)PageNum(txdesc_len + info_len, page_size);
	bool modified = 0;
	bool loc_mod = 0, size_mod = 0, page_num_mod = 0;
	
	if (cache->loc != loc) {
		RTW_INFO("%s %s loc change (%u -> %u)\n"
			, __func__, cache->name, cache->loc, loc);
		loc_mod = 1;
	}
	if (cache->size != info_len) {
		RTW_INFO("%s %s size change (%u -> %u)\n"
			, __func__, cache->name, cache->size, info_len);
		size_mod = 1;
	}
	if (cache->page_num != page_num) {
		RTW_INFO("%s %s page_num change (%u -> %u)\n"
			, __func__, cache->name, cache->page_num, page_num);
		page_num_mod = 1;
	}

	if (info) {
		if (cache->data) {
			if (cache->size == info_len) {
				if (_rtw_memcmp(cache->data, info, info_len) != _TRUE) {
					RTW_INFO("%s %s data change\n", __func__, cache->name);
					modified = 1;
				}
			} else
				rsvd_page_cache_free_data(cache);
		}

		if (!cache->data) {
			cache->data = rtw_malloc(info_len);
			if (!cache->data) {
				RTW_ERR("%s %s alloc data with size(%u) fail\n"
					, __func__, cache->name, info_len);
				rtw_warn_on(1);
			} else {
				RTW_INFO("%s %s alloc data with size(%u)\n"
					, __func__, cache->name, info_len);
			}
			modified = 1;
		}

		if (cache->data && modified)
			_rtw_memcpy(cache->data, info, info_len);
	} else {
		if (cache->data && size_mod)
			rsvd_page_cache_free_data(cache);
	}

	cache->loc = loc;
	cache->page_num = page_num;
	cache->size = info_len;

	return modified | loc_mod | size_mod | page_num_mod;
}

bool rsvd_page_cache_update_data(struct rsvd_page_cache_t *cache, u8 *info, u32 info_len)
{
	bool modified = 0;

	if (!info || !info_len) {
		RTW_WARN("%s %s invalid input(info:%p, info_len:%u)\n"
			, __func__, cache->name, info, info_len);
		goto exit;
	}

	if (!cache->loc || !cache->page_num || !cache->size) {
		RTW_ERR("%s %s layout not ready(loc:%u, page_num:%u, size:%u)\n"
			, __func__, cache->name, cache->loc, cache->page_num, cache->size);
		rtw_warn_on(1);
		goto exit;
	}

	if (cache->size != info_len) {
		RTW_ERR("%s %s size(%u) differ with info_len(%u)\n"
			, __func__, cache->name, cache->size, info_len);
		rtw_warn_on(1);
		goto exit;
	}

	if (!cache->data) {
		cache->data = rtw_zmalloc(cache->size);
		if (!cache->data) {
			RTW_ERR("%s %s alloc data with size(%u) fail\n"
				, __func__, cache->name, cache->size);
			rtw_warn_on(1);
			goto exit;
		} else {
			RTW_INFO("%s %s alloc data with size(%u)\n"
				, __func__, cache->name, info_len);
		}
		modified = 1;
	}

	if (_rtw_memcmp(cache->data, info, cache->size) == _FALSE) {
		RTW_INFO("%s %s data change\n", __func__, cache->name);
		_rtw_memcpy(cache->data, info, cache->size);
		modified = 1;
	}

exit:
	return modified;
}

void rsvd_page_cache_free_data(struct rsvd_page_cache_t *cache)
{
	if (cache->data) {
		rtw_mfree(cache->data, cache->size);
		cache->data = NULL;
	}
}

void rsvd_page_cache_free(struct rsvd_page_cache_t *cache)
{
	cache->loc = 0;
	cache->page_num = 0;
	rsvd_page_cache_free_data(cache);
	cache->size = 0;
}

/* #define CONFIG_GTK_OL_DBG */

/*#define DBG_SEC_CAM_MOVE*/
#ifdef DBG_SEC_CAM_MOVE
void rtw_hal_move_sta_gk_to_dk(_adapter *adapter)
{
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	int cam_id, index = 0;
	u8 *addr = NULL;

	if (!MLME_IS_STA(adapter))
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
	else if (IS_8188GTV(ChipVersion))
		cnt += sprintf((buf + cnt), "Chip Version Info: CHIP_8188GTV_");
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
	else if (IS_8710B_SERIES(ChipVersion))
		cnt += sprintf((buf + cnt), "Chip Version Info: CHIP_8710B_");
	else if (IS_8192F_SERIES(ChipVersion))
		cnt += sprintf((buf + cnt), "Chip Version Info: CHIP_8192F_");
	else if (IS_8822C_SERIES(ChipVersion))
		cnt += sprintf((buf + cnt), "Chip Version Info: CHIP_8822C_");
	else
		cnt += sprintf((buf + cnt), "Chip Version Info: CHIP_UNKNOWN_");

	cnt += sprintf((buf + cnt), "%s", IS_NORMAL_CHIP(ChipVersion) ? "" : "T_");
	
	if (IS_CHIP_VENDOR_TSMC(ChipVersion))
		cnt += sprintf((buf + cnt), "%s", "T");
	else if (IS_CHIP_VENDOR_UMC(ChipVersion))
		cnt += sprintf((buf + cnt), "%s", "U");
	else if (IS_CHIP_VENDOR_SMIC(ChipVersion))
		cnt += sprintf((buf + cnt), "%s", "S");

	if (IS_A_CUT(ChipVersion))
		cnt += sprintf((buf + cnt), "1_");
	else if (IS_B_CUT(ChipVersion))
		cnt += sprintf((buf + cnt), "2_");
	else if (IS_C_CUT(ChipVersion))
		cnt += sprintf((buf + cnt), "3_");
	else if (IS_D_CUT(ChipVersion))
		cnt += sprintf((buf + cnt), "4_");
	else if (IS_E_CUT(ChipVersion))
		cnt += sprintf((buf + cnt), "5_");
	else if (IS_F_CUT(ChipVersion))
		cnt += sprintf((buf + cnt), "6_");
	else if (IS_I_CUT(ChipVersion))
		cnt += sprintf((buf + cnt), "9_");
	else if (IS_J_CUT(ChipVersion))
		cnt += sprintf((buf + cnt), "10_");
	else if (IS_K_CUT(ChipVersion))
		cnt += sprintf((buf + cnt), "11_");
	else
		cnt += sprintf((buf + cnt), "UNKNOWN_Cv(%d)_", ChipVersion.CUTVersion);

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

u8 rtw_hal_get_port(_adapter *adapter)
{
	u8 hw_port = get_hw_port(adapter);
#ifdef CONFIG_CLIENT_PORT_CFG
	u8 clt_port = get_clt_port(adapter);

	if (clt_port)
		hw_port = clt_port;

#ifdef DBG_HW_PORT
	if (MLME_IS_STA(adapter) && (adapter->client_id != MAX_CLIENT_PORT_NUM)) {
		if(hw_port == CLT_PORT_INVALID) {
			RTW_ERR(ADPT_FMT" @@@@@ Client port == 0 @@@@@\n", ADPT_ARG(adapter));
			rtw_warn_on(1);
		}
	}
	else if (MLME_IS_AP(adapter) || MLME_IS_MESH(adapter)) {
		if (hw_port != HW_PORT0) {
			RTW_ERR(ADPT_FMT" @@@@@ AP / MESH port != 0 @@@@@\n", ADPT_ARG(adapter));
			rtw_warn_on(1);
		}
	}
	if (0)
		RTW_INFO(ADPT_FMT" - HP:%d,CP:%d\n", ADPT_ARG(adapter), get_hw_port(adapter), get_clt_port(adapter));
#endif /*DBG_HW_PORT*/

#endif/*CONFIG_CLIENT_PORT_CFG*/

	return hw_port;
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
 *	AutoLoadFail		efuse autoload fail or not
 *
 */
void hal_com_config_channel_plan(
		PADAPTER padapter,
		char *hw_alpha2,
		u8 hw_chplan,
		char *sw_alpha2,
		u8 sw_chplan,
		BOOLEAN AutoLoadFail
)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
	PHAL_DATA_TYPE	pHalData;
	u8 force_hw_chplan = _FALSE;
	int chplan = -1;
	const struct country_chplan *country_ent = NULL, *ent;
	u8 def_chplan = 0x7F; /* Realtek define,  used when HW, SW both invalid */

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
	} else if (sw_chplan != RTW_CHPLAN_UNSPECIFIED)
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

	rfctl->country_ent = country_ent;
	rfctl->ChannelPlan = chplan;
	pHalData->bDisableSWChannelPlan = force_hw_chplan;
}

BOOLEAN
HAL_IsLegalChannel(
		PADAPTER	Adapter,
		u32			Channel
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
	PADAPTER		Adapter,
	u8			*mBratesOS,
	u16			*pBrateCfg)
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

static void
_OneOutPipeMapping(
		PADAPTER	pAdapter
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

static void
_TwoOutPipeMapping(
		PADAPTER	pAdapter,
		BOOLEAN		bWIFICfg
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

static void _ThreeOutPipeMapping(
		PADAPTER	pAdapter,
		BOOLEAN		bWIFICfg
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
#if 0
static void _FourOutPipeMapping(
		PADAPTER	pAdapter,
		BOOLEAN		bWIFICfg
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
#endif
BOOLEAN
Hal_MappingOutPipe(
		PADAPTER	pAdapter,
		u8		NumOutPipe
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
	case 5:
	case 6:
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
			rtw_hal_get_hwreg(iface, HW_VAR_MAC_ADDR, mac_addr);
			RTW_PRINT_SEL(sel, ADPT_FMT"- hw port(%d) mac_addr ="MAC_FMT"\n",
				ADPT_ARG(iface), iface->hw_port, MAC_ARG(mac_addr));
		}
	}
#endif
}

#ifdef RTW_HALMAC
void rtw_hal_hw_port_enable(_adapter *adapter)
{
#if 1
	u8 port_enable = _TRUE;

	rtw_hal_set_hwreg(adapter, HW_VAR_PORT_CFG, &port_enable);
#else
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct rtw_halmac_bcn_ctrl bcn_ctrl;

	_rtw_memset(&bcn_ctrl, 0, sizeof(struct rtw_halmac_bcn_ctrl));
	bcn_ctrl.enable_bcn = 1;
	bcn_ctrl.rx_bssid_fit = 1;
	bcn_ctrl.rxbcn_rpt = 1;

	/*rtw_halmac_get_bcn_ctrl(struct dvobj_priv *d, enum _hw_port hwport,
				struct rtw_halmac_bcn_ctrl *bcn_ctrl)*/
	if (rtw_halmac_set_bcn_ctrl(dvobj, get_hw_port(adapter), &bcn_ctrl) == -1) {
		RTW_ERR(ADPT_FMT" - hw port(%d) enable fail!!\n", ADPT_ARG(adapter), get_hw_port(adapter));
		rtw_warn_on(1);
	}
#endif
}
void rtw_hal_hw_port_disable(_adapter *adapter)
{
	u8 port_enable = _FALSE;

	rtw_hal_set_hwreg(adapter, HW_VAR_PORT_CFG, &port_enable);
}

void rtw_restore_hw_port_cfg(_adapter *adapter)
{
#ifdef CONFIG_MI_WITH_MBSSID_CAM

#else
	int i;
	_adapter *iface;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface)
			rtw_hal_hw_port_enable(iface);
	}
#endif
}
#endif

void rtw_mi_set_mac_addr(_adapter *adapter)
{
#ifdef CONFIG_MI_WITH_MBSSID_CAM
	rtw_mi_set_mbid_cam(adapter);
#else
	int i;
	_adapter *iface;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (iface)
			rtw_hal_set_hwreg(iface, HW_VAR_MAC_ADDR, adapter_mac_addr(iface));
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
	pHalData->RegIQKFWOffload = regsty->iqk_fw_offload;
	pHalData->ch_switch_offload = regsty->ch_switch_offload;
	pHalData->multi_ch_switch_mode = 0;
#ifdef RTW_REDUCE_SCAN_SWITCH_CH_TIME
	if (pHalData->ch_switch_offload == 0)
		pHalData->ch_switch_offload = 1;
#endif
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

	RTW_DBG_DUMP("payload: ", C2H_PAYLOAD_88XX(buf), C2H_PLEN_88XX(buf));

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
	enum rf_type rf_type;
	int ret = _FAIL;

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
	u8 ss_num = 4;
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
		RTW_PRINT("ant_num:0x%x\n", ant_num);
		RTW_PRINT("protocol:0x%x\n", protocol);
		RTW_PRINT("nic:0x%x\n", nic);
	}

#if defined(CONFIG_RTL8822C) || defined(CONFIG_RTL8814B)
	if (IS_8822C_SERIES(hal_data->version_id) || IS_8814B_SERIES(hal_data->version_id)) {
		#define GET_C2H_MAC_HIDDEN_RPT_SS_NUM(_data)	LE_BITS_TO_1BYTE(((u8 *)(_data)) + 6, 3, 2)
		ss_num = GET_C2H_MAC_HIDDEN_RPT_SS_NUM(data);

		if (DBG_C2H_MAC_HIDDEN_RPT_HANDLE)
			RTW_PRINT("ss_num:0x%x\n", ss_num);

		if (ss_num == 0x03)
			ss_num = 4;
	}
#endif

#if defined(CONFIG_RTL8822C)
	if (IS_8822C_SERIES(hal_data->version_id)) {
		if (hw_stype == 0xE)
			hal_spec->max_tx_cnt = rtw_min(hal_spec->max_tx_cnt, 1); /* limit 1TX only */
	}
#endif
	hal_data->PackageType = package_type;
	hal_spec->hci_type = hci_type;
	hal_spec->wl_func &= mac_hidden_wl_func_to_hal_wl_func(wl_func);
	hal_spec->bw_cap &= mac_hidden_max_bw_to_hal_bw_cap(bw);
	hal_spec->proto_cap &= mac_hidden_proto_to_hal_proto_cap(protocol);
	hal_spec->rf_reg_path_num = rtw_min(hal_spec->rf_reg_path_num, ant_num);

	rf_type = rtw_chip_rftype_to_hal_rftype(adapter, hal_spec->rf_reg_path_num);
	if (!RF_TYPE_VALID(rf_type)) {
		RTW_ERR("%s rtw_chip_rftype_to_hal_rftype failed\n", __func__);
		goto exit;
	}

	/*
	* RF TX path num >= max_tx_cnt >= tx_nss_num
	* ex: RF TX path num(4) >= max_tx_cnt(2) >= tx_nss_num(1)
	* Select at most 2 out of 4 TX RF path to do 1SS 2TX
	*/
	hal_spec->max_tx_cnt = rtw_min(hal_spec->max_tx_cnt, rf_type_to_rf_tx_cnt(rf_type));
	hal_spec->tx_nss_num = rtw_min(hal_spec->tx_nss_num, hal_spec->max_tx_cnt);
	hal_spec->tx_nss_num = rtw_min(hal_spec->tx_nss_num, ss_num);

	hal_spec->rx_nss_num = rtw_min(hal_spec->rx_nss_num, rf_type_to_rf_rx_cnt(rf_type));
	hal_spec->rx_nss_num = rtw_min(hal_spec->rx_nss_num, ss_num);

	ret = _SUCCESS;

exit:
	return ret;
}

int c2h_mac_hidden_rpt_2_hdl(_adapter *adapter, u8 *data, u8 len)
{
	HAL_DATA_TYPE	*hal_data = GET_HAL_DATA(adapter);
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

	#if defined(CONFIG_RTL8188F) || defined(CONFIG_RTL8188GTV)
	if (IS_8188F(hal_data->version_id) || IS_8188GTV(hal_data->version_id)) {
		#define GET_C2H_MAC_HIDDEN_RPT_IRV(_data)	LE_BITS_TO_1BYTE(((u8 *)(_data)) + 0, 0, 4)
		u8 irv = GET_C2H_MAC_HIDDEN_RPT_IRV(data);

		if (DBG_C2H_MAC_HIDDEN_RPT_HANDLE)
			RTW_PRINT("irv:0x%x\n", irv);

		if(irv != 0xf)
			hal_data->version_id.CUTVersion = irv;
	}
	#endif

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
	systime start = rtw_get_current_time();
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

#ifdef RTW_PER_CMD_SUPPORT_FW
#define H2C_REQ_PER_RPT_LEN 5
#define SET_H2CCMD_REQ_PER_RPT_GROUP_MACID(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 4, __Value)
#define SET_H2CCMD_REQ_PER_RPT_RPT_TYPE(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd, 4, 4, __Value)
#define SET_H2CCMD_REQ_PER_RPT_MACID_BMAP(__pH2CCmd, __Value)	SET_BITS_TO_LE_4BYTE(__pH2CCmd + 1, 0, 32, __Value)

u8 rtw_hal_set_req_per_rpt_cmd(_adapter *adapter, u8 group_macid,
				      u8 rpt_type, u32 macid_bitmap)
{
	u8 ret = _FAIL;
	u8 cmd_buf[H2C_REQ_PER_RPT_LEN] = {0};

	SET_H2CCMD_REQ_PER_RPT_GROUP_MACID(cmd_buf, group_macid);
	SET_H2CCMD_REQ_PER_RPT_RPT_TYPE(cmd_buf, rpt_type);
	SET_H2CCMD_REQ_PER_RPT_MACID_BMAP(cmd_buf, macid_bitmap);

	ret = rtw_hal_fill_h2c_cmd(adapter, 
				   H2C_REQ_PER_RPT, 
				   H2C_REQ_PER_RPT_LEN, 
				   cmd_buf);
	return ret;
}

#define	GET_C2H_PER_RATE_RPT_TYPE0_MACID0(_data)	LE_BITS_TO_1BYTE(((u8 *)(_data)), 0, 8)
#define	GET_C2H_PER_RATE_RPT_TYPE0_PER0(_data)		LE_BITS_TO_1BYTE(((u8 *)(_data)) + 1, 0, 8)
#define	GET_C2H_PER_RATE_RPT_TYPE0_RATE0(_data)		LE_BITS_TO_1BYTE(((u8 *)(_data)) + 2, 0, 8)
#define	GET_C2H_PER_RATE_RPT_TYPE0_BW0(_data)		LE_BITS_TO_1BYTE(((u8 *)(_data)) + 3, 0, 2)
#define	GET_C2H_PER_RATE_RPT_TYPE0_TOTAL_PKT0(_data)	LE_BITS_TO_2BYTE(((u8 *)(_data)) + 4, 0, 16)
#define	GET_C2H_PER_RATE_RPT_TYPE0_MACID1(_data)	LE_BITS_TO_1BYTE(((u8 *)(_data)) + 6, 0, 8)
#define	GET_C2H_PER_RATE_RPT_TYPE0_PER1(_data)		LE_BITS_TO_1BYTE(((u8 *)(_data)) + 7, 0, 8)
#define	GET_C2H_PER_RATE_RPT_TYPE0_RATE1(_data)		LE_BITS_TO_1BYTE(((u8 *)(_data)) + 8, 0, 8)
#define	GET_C2H_PER_RATE_RPT_TYPE0_BW1(_data)		LE_BITS_TO_1BYTE(((u8 *)(_data)) + 9, 0, 2)
#define	GET_C2H_PER_RATE_RPT_TYPE0_TOTAL_PKT1(_data)	LE_BITS_TO_2BYTE(((u8 *)(_data)) + 10, 0, 16)

#define	GET_C2H_PER_RATE_RPT_TYPE1_MACID0(_data)	LE_BITS_TO_1BYTE(((u8 *)(_data)), 0, 8)
#define	GET_C2H_PER_RATE_RPT_TYPE1_PER0(_data)		LE_BITS_TO_1BYTE(((u8 *)(_data)) + 1, 0, 8)
#define	GET_C2H_PER_RATE_RPT_TYPE1_RATE0(_data)		LE_BITS_TO_1BYTE(((u8 *)(_data)) + 2, 0, 8)
#define	GET_C2H_PER_RATE_RPT_TYPE1_BW0(_data)		LE_BITS_TO_1BYTE(((u8 *)(_data)) + 3, 0, 2)
#define	GET_C2H_PER_RATE_RPT_TYPE1_MACID1(_data)	LE_BITS_TO_1BYTE(((u8 *)(_data)) + 4, 0, 8)
#define	GET_C2H_PER_RATE_RPT_TYPE1_PER1(_data)		LE_BITS_TO_1BYTE(((u8 *)(_data)) + 5, 0, 8)
#define	GET_C2H_PER_RATE_RPT_TYPE1_RATE1(_data)		LE_BITS_TO_1BYTE(((u8 *)(_data)) + 6, 0, 8)
#define	GET_C2H_PER_RATE_RPT_TYPE1_BW1(_data)		LE_BITS_TO_1BYTE(((u8 *)(_data)) + 7, 0, 2)
#define	GET_C2H_PER_RATE_RPT_TYPE1_MACID2(_data)	LE_BITS_TO_1BYTE(((u8 *)(_data)) + 8, 0, 8)
#define	GET_C2H_PER_RATE_RPT_TYPE1_PER2(_data)		LE_BITS_TO_1BYTE(((u8 *)(_data)) + 9, 0, 8)
#define	GET_C2H_PER_RATE_RPT_TYPE1_RATE2(_data)		LE_BITS_TO_1BYTE(((u8 *)(_data)) + 10, 0, 8)
#define	GET_C2H_PER_RATE_RPT_TYPE1_BW2(_data)		LE_BITS_TO_1BYTE(((u8 *)(_data)) + 11, 0, 2)

static void per_rate_rpt_update(_adapter *adapter, u8 mac_id,
				u8 per, u8 rate,
				u8 bw, u8 total_pkt)
{
#ifdef CONFIG_RTW_MESH
	rtw_ieee80211s_update_metric(adapter, mac_id,
				     per, rate,
				     bw, total_pkt);
#endif
}

int c2h_per_rate_rpt_hdl(_adapter *adapter, u8 *data, u8 len)
{
	/* Now only consider type0, since it covers all params in type1
	 * type0: mac_id, per, rate, bw, total_pkt
	 * type1: mac_id, per, rate, bw
	 */
	u8 mac_id[2] = {0}, per[2] = {0}, rate[2] = {0}, bw[2] = {0};
	u16 total_pkt[2] = {0};
	int ret = _FAIL, i, macid_cnt = 0;

	/* type0:
	 * 1 macid includes   6 bytes info + 1 byte 0xff
	 * 2 macid includes 2*6 bytes info
	 */
	if (!(len == 7 || len == 12)) {
		RTW_WARN("%s len(%u) != 7 or 12\n", __FUNCTION__, len);
		goto exit;
	}

	macid_cnt++;
	mac_id[0] = GET_C2H_PER_RATE_RPT_TYPE0_MACID0(data);
	per[0] = GET_C2H_PER_RATE_RPT_TYPE0_PER0(data);
	rate[0] = GET_C2H_PER_RATE_RPT_TYPE0_RATE0(data);
	bw[0] = GET_C2H_PER_RATE_RPT_TYPE0_BW0(data);
	total_pkt[0] = GET_C2H_PER_RATE_RPT_TYPE0_TOTAL_PKT0(data);

	mac_id[1] = GET_C2H_PER_RATE_RPT_TYPE0_MACID1(data);
	/* 0xff means no report anymore */
	if (mac_id[1] == 0xff)
		goto update_per;
	if (len != 12) {
		RTW_WARN("%s incorrect format\n", __FUNCTION__);
		goto exit;
	}
	macid_cnt++;
	per[1] = GET_C2H_PER_RATE_RPT_TYPE0_PER1(data);
	rate[1] = GET_C2H_PER_RATE_RPT_TYPE0_RATE1(data);
	bw[1] = GET_C2H_PER_RATE_RPT_TYPE0_BW1(data);
	total_pkt[1] = GET_C2H_PER_RATE_RPT_TYPE0_TOTAL_PKT1(data);

update_per:
	for (i = 0; i < macid_cnt; i++) {
		RTW_DBG("[%s] type0 rpt[%d]: macid = %u, per = %u, "
			"rate = %u, bw = %u, total_pkt = %u\n",
			__FUNCTION__, i, mac_id[i], per[i],
			rate[i], bw[i], total_pkt[i]);
		per_rate_rpt_update(adapter, mac_id[i],
				    per[i], rate[i],
				    bw[i], total_pkt[i]);
	}
	ret = _SUCCESS;
exit:
	return ret;
}
#endif /* RTW_PER_CMD_SUPPORT_FW */

#ifdef CONFIG_LPS_ACK
#define	GET_C2H_LPS_STATUS_RPT_GET_ACTION(_data)	LE_BITS_TO_1BYTE(((u8 *)(_data)), 0, 8)
#define	GET_C2H_LPS_STATUS_RPT_GET_STATUS_CODE(_data)		LE_BITS_TO_1BYTE(((u8 *)(_data)) + 1, 0, 8)
#define DBG_LPS_STATUS_RPT 0

int c2h_lps_status_rpt(PADAPTER adapter, u8 *data, u8 len)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	struct submit_ctx *lps_sctx = &pwrpriv->lps_ack_sctx;
	u8 action = 0;
	s8 status_code = 0;
	int ret = _FAIL;

	if (len < LPS_STATUS_RPT_LEN) {
		RTW_WARN("%s len(%u) < %d\n", __func__, len, LPS_STATUS_RPT_LEN);
		goto exit;
	}

	action = GET_C2H_LPS_STATUS_RPT_GET_ACTION(data);
	status_code = GET_C2H_LPS_STATUS_RPT_GET_STATUS_CODE(data);

	/* action=0: report force leave null data status */
	switch (action) {
		case 0:
			pwrpriv->lps_ack_status = status_code;

			if (DBG_LPS_STATUS_RPT)
				RTW_INFO("=== [C2H LPS Action(%d)] LPS Status Code:%d ===\n", action, status_code);
			
			break;
		default:
			RTW_INFO("UnKnown Action(%d) for C2H LPS RPT\n", action);
			break;
	}

	rtw_sctx_done(&lps_sctx);
	ret = _SUCCESS;

exit:
	return ret;
}
#endif /* CONFIG_LPS_ACK */

void rtw_hal_update_sta_wset(_adapter *adapter, struct sta_info *psta)
{
	u8 w_set = 0;

	if (psta->wireless_mode & WIRELESS_11B)
		w_set |= WIRELESS_CCK;

	if ((psta->wireless_mode & WIRELESS_11G) || (psta->wireless_mode & WIRELESS_11A))
		w_set |= WIRELESS_OFDM;

	if (psta->wireless_mode & WIRELESS_11_24N)
		w_set |= WIRELESS_HT;

	if ((psta->wireless_mode & WIRELESS_11AC) || (psta->wireless_mode & WIRELESS_11_5N))
		w_set |= WIRELESS_VHT;

	psta->cmn.support_wireless_set = w_set;
}

void rtw_hal_update_sta_mimo_type(_adapter *adapter, struct sta_info *psta)
{
	s8 tx_nss, rx_nss;

	tx_nss = rtw_get_sta_tx_nss(adapter, psta);
	rx_nss =  rtw_get_sta_rx_nss(adapter, psta);
	if ((tx_nss == 1) && (rx_nss == 1))
		psta->cmn.mimo_type = RF_1T1R;
	else if ((tx_nss == 1) && (rx_nss == 2))
		psta->cmn.mimo_type = RF_1T2R;
	else if ((tx_nss == 2) && (rx_nss == 2))
		psta->cmn.mimo_type = RF_2T2R;
	else if ((tx_nss == 2) && (rx_nss == 3))
		psta->cmn.mimo_type = RF_2T3R;
	else if ((tx_nss == 2) && (rx_nss == 4))
		psta->cmn.mimo_type = RF_2T4R;
	else if ((tx_nss == 3) && (rx_nss == 3))
		psta->cmn.mimo_type = RF_3T3R;
	else if ((tx_nss == 3) && (rx_nss == 4))
		psta->cmn.mimo_type = RF_3T4R;
	else if ((tx_nss == 4) && (rx_nss == 4))
		psta->cmn.mimo_type = RF_4T4R;
	else
		rtw_warn_on(1);

#ifdef CONFIG_CTRL_TXSS_BY_TP
	rtw_ctrl_txss_update_mimo_type(adapter, psta);
#endif

	RTW_INFO("STA - MAC_ID:%d, Tx - %d SS, Rx - %d SS\n",
			psta->cmn.mac_id, tx_nss, rx_nss);
}

void rtw_hal_update_sta_smps_cap(_adapter *adapter, struct sta_info *psta)
{
	/*Spatial Multiplexing Power Save*/
#if 0
	if (check_fwstate(&adapter->mlmepriv, WIFI_AP_STATE) == _TRUE) {
		#ifdef CONFIG_80211N_HT
		if (psta->htpriv.ht_option) {
			if (psta->htpriv.smps_cap == 0)
				psta->cmn.sm_ps = SM_PS_STATIC;
			else if (psta->htpriv.smps_cap == 1)
				psta->cmn.sm_ps = SM_PS_DYNAMIC;
			else
				psta->cmn.sm_ps = SM_PS_DISABLE;
		}
		#endif /* CONFIG_80211N_HT */
	} else
#endif
		psta->cmn.sm_ps = SM_PS_DISABLE;

	RTW_INFO("STA - MAC_ID:%d, SM_PS %d\n",
			psta->cmn.mac_id, psta->cmn.sm_ps);
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
	u8 i, tx_nss;
	u64 tx_ra_bitmap = 0, tmp64=0;

	if (psta == NULL)
		return;

	/* b/g mode ra_bitmap  */
	for (i = 0; i < sizeof(psta->bssrateset); i++) {
		if (psta->bssrateset[i])
			tx_ra_bitmap |= rtw_get_bit_value_from_ieee_value(psta->bssrateset[i] & 0x7f);
	}

#ifdef CONFIG_80211N_HT
if (padapter->registrypriv.ht_enable && is_supported_ht(padapter->registrypriv.wireless_mode)) {
	tx_nss = GET_HAL_TX_NSS(padapter);
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

		tmp64 = rtw_ht_mcs_set_to_bitmap(psta->htpriv.ht_cap.supp_mcs_set, tx_nss);
		tx_ra_bitmap |= (tmp64 << 12);
	}
}
#endif /* CONFIG_80211N_HT */
	psta->cmn.ra_info.ramask = tx_ra_bitmap;
	psta->init_rate = get_highest_rate_idx(tx_ra_bitmap) & 0x3f;
}

void rtw_hal_update_sta_ra_info(PADAPTER padapter, struct sta_info *psta)
{
	rtw_hal_update_sta_mimo_type(padapter, psta);
	rtw_hal_update_sta_smps_cap(padapter, psta);
	rtw_hal_update_sta_rate_mask(padapter, psta);
}

#ifndef CONFIG_HAS_HW_VAR_BCN_CTRL_ADDR
static u32 hw_bcn_ctrl_addr(_adapter *adapter, u8 hw_port)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);

	if (hw_port >= hal_spec->port_num) {
		RTW_ERR(FUNC_ADPT_FMT" HW Port(%d) invalid\n", FUNC_ADPT_ARG(adapter), hw_port);
		rtw_warn_on(1);
		return 0;
	}

	switch (hw_port) {
	case HW_PORT0:
		return REG_BCN_CTRL;
	case HW_PORT1:
		return REG_BCN_CTRL_1;
	}

	return 0;
}
#endif

static void rtw_hal_get_msr(_adapter *adapter, u8 *net_type)
{
#ifdef RTW_HALMAC
	rtw_halmac_get_network_type(adapter_to_dvobj(adapter),
				adapter->hw_port, net_type);
#else /* !RTW_HALMAC */
	switch (adapter->hw_port) {
	case HW_PORT0:
		/*REG_CR - BIT[17:16]-Network Type for port 1*/
		*net_type = rtw_read8(adapter, MSR) & 0x03;
		break;
	case HW_PORT1:
		/*REG_CR - BIT[19:18]-Network Type for port 1*/
		*net_type = (rtw_read8(adapter, MSR) & 0x0C) >> 2;
		break;
#if defined(CONFIG_RTL8814A)
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
#endif /*#if defined(CONFIG_RTL8814A)*/
	default:
		RTW_INFO("[WARN] "ADPT_FMT"- invalid hw port -%d\n",
			 ADPT_ARG(adapter), adapter->hw_port);
		rtw_warn_on(1);
		break;
	}
#endif /* !RTW_HALMAC */
}

#if defined(CONFIG_MI_WITH_MBSSID_CAM) && defined(CONFIG_MBSSID_CAM) /*For 2 hw ports - 88E/92E/8812/8821/8723B*/
static u8 rtw_hal_net_type_decision(_adapter *adapter, u8 net_type)
{
	if ((adapter->hw_port == HW_PORT0) && (rtw_get_mbid_cam_entry_num(adapter))) {
		if (net_type != _HW_STATE_NOLINK_)
			return _HW_STATE_AP_;
	}
	return net_type;
}
#endif
static void rtw_hal_set_msr(_adapter *adapter, u8 net_type)
{
#ifdef RTW_HALMAC
	#if defined(CONFIG_MI_WITH_MBSSID_CAM) && defined(CONFIG_MBSSID_CAM)
	net_type = rtw_hal_net_type_decision(adapter, net_type);
	#endif
	rtw_halmac_set_network_type(adapter_to_dvobj(adapter),
				adapter->hw_port, net_type);
#else /* !RTW_HALMAC */
	u8 val8 = 0;

	switch (adapter->hw_port) {
	case HW_PORT0:
		#if defined(CONFIG_MI_WITH_MBSSID_CAM) && defined(CONFIG_MBSSID_CAM)
		net_type = rtw_hal_net_type_decision(adapter, net_type);
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
#if defined(CONFIG_RTL8814A)
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
#endif /* CONFIG_RTL8814A */
	default:
		RTW_INFO("[WARN] "ADPT_FMT"- invalid hw port -%d\n",
			 ADPT_ARG(adapter), adapter->hw_port);
		rtw_warn_on(1);
		break;
	}
#endif /* !RTW_HALMAC */
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
	systime start = 0, end = 0;
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
	systime start = 0, end = 0;
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
	u8 addr, addr1 = 0;
	u32 wdata, wdata1 = 0;

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

#if defined(CONFIG_RTL8192F)
		if(j == 1) {
			wdata1 = wdata;
			addr1 = addr;
			continue;
		}
#endif

		rtw_sec_write_cam(adapter, addr, wdata);
	}

#if defined(CONFIG_RTL8192F)
	rtw_sec_write_cam(adapter, addr1, wdata1);
#endif
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

	if (INVALID_CAM_ID != rtw_mbid_cam_search_by_macaddr(adapter, mac_addr))
		goto exit;

	if (entry_num >= TOTAL_MBID_CAM_NUM) {
		RTW_INFO(FUNC_ADPT_FMT" failed !! MBSSID number :%d over TOTAL_CAM_ENTRY(8)\n", FUNC_ADPT_ARG(adapter), entry_num);
		rtw_warn_on(1);
	}

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
			_RTW_PRINT_SEL(sel, "IF_ID:%d\t", iface_id);
			_RTW_PRINT_SEL(sel, "MAC Addr:"MAC_FMT"\t", MAC_ARG(dvobj->mbid_cam_cache[i].mac_addr));

			iface = dvobj->padapters[iface_id];
			if (iface) {
				if (MLME_IS_STA(iface))
					_RTW_PRINT_SEL(sel, "ROLE:%s\n", "STA");
				else if (MLME_IS_AP(iface))
					_RTW_PRINT_SEL(sel, "ROLE:%s\n", "AP");
				else if (MLME_IS_MESH(iface))
					_RTW_PRINT_SEL(sel, "ROLE:%s\n", "MESH");
				else
					_RTW_PRINT_SEL(sel, "ROLE:%s\n", "NONE");
			}

		} else
			_RTW_PRINT_SEL(sel, "N/A\n");
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
		_RTW_PRINT_SEL(sel, "MAC Addr:"MAC_FMT"\n", MAC_ARG(mac_addr));
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

/*
static void clear_mbssid_cam(_adapter *padapter, u8 cam_addr)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_MBSSID_CAM_CLEAR, &cam_addr);
}
*/

void rtw_ap_set_mbid_num(_adapter *adapter, u8 ap_num)
{
	rtw_write8(adapter, REG_MBID_NUM,
		((rtw_read8(adapter, REG_MBID_NUM) & 0xF8) | ((ap_num -1) & 0x07)));

}
void rtw_mbid_cam_enable(_adapter *adapter)
{
	/*enable MBSSID*/
	rtw_hal_rcr_add(adapter, RCR_ENMBID);
}
void rtw_mi_set_mbid_cam(_adapter *adapter)
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
	rtw_mbid_cam_enable(adapter);
}
#endif /*CONFIG_MBSSID_CAM*/

#ifdef CONFIG_FW_HANDLE_TXBCN
#define H2C_BCN_OFFLOAD_LEN	1

#define SET_H2CCMD_BCN_OFFLOAD_EN(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 1, __Value)
#define SET_H2CCMD_BCN_ROOT_TBTT_RPT(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd, 1, 1, __Value)
#define SET_H2CCMD_BCN_VAP1_TBTT_RPT(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd, 2, 1, __Value)
#define SET_H2CCMD_BCN_VAP2_TBTT_RPT(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd, 3, 1, __Value)
#define SET_H2CCMD_BCN_VAP3_TBTT_RPT(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd, 4, 1, __Value)
#define SET_H2CCMD_BCN_VAP4_TBTT_RPT(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd, 5, 1, __Value)

void rtw_hal_set_fw_ap_bcn_offload_cmd(_adapter *adapter, bool fw_bcn_en, u8 tbtt_rpt_map)
{
	u8 fw_bcn_offload[1] = {0};
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);

	if (fw_bcn_en)
		SET_H2CCMD_BCN_OFFLOAD_EN(fw_bcn_offload, 1);

	if (tbtt_rpt_map & BIT(0))
		SET_H2CCMD_BCN_ROOT_TBTT_RPT(fw_bcn_offload, 1);
	if (tbtt_rpt_map & BIT(1))
		SET_H2CCMD_BCN_VAP1_TBTT_RPT(fw_bcn_offload, 1);
	if (tbtt_rpt_map & BIT(2))
		SET_H2CCMD_BCN_VAP2_TBTT_RPT(fw_bcn_offload, 1);
	if (tbtt_rpt_map & BIT(3))
			SET_H2CCMD_BCN_VAP3_TBTT_RPT(fw_bcn_offload, 1);

	dvobj->vap_tbtt_rpt_map = tbtt_rpt_map;
	dvobj->fw_bcn_offload = fw_bcn_en;
	RTW_INFO("[FW BCN] Offload : %s\n", (dvobj->fw_bcn_offload) ? "EN" : "DIS");
	RTW_INFO("[FW BCN] TBTT RPT map : 0x%02x\n", dvobj->vap_tbtt_rpt_map);

	rtw_hal_fill_h2c_cmd(adapter, H2C_FW_BCN_OFFLOAD,
					H2C_BCN_OFFLOAD_LEN, fw_bcn_offload);
}

void rtw_hal_set_bcn_rsvdpage_loc_cmd(_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	u8 ret, vap_id;
	u32 page_size = 0;
	u8 bcn_rsvdpage[H2C_BCN_RSVDPAGE_LEN] = {0};

	rtw_hal_get_def_var(adapter, HAL_DEF_TX_PAGE_SIZE, (u8 *)&page_size);
	#if 1
	for (vap_id = 0; vap_id < CONFIG_LIMITED_AP_NUM; vap_id++) {
		if (dvobj->vap_map & BIT(vap_id))
			bcn_rsvdpage[vap_id] = vap_id * (MAX_BEACON_LEN / page_size);
	}
	#else
#define SET_H2CCMD_BCN_RSVDPAGE_LOC_ROOT(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd, 0, 8, __Value)
#define SET_H2CCMD_BCN_RSVDPAGE_LOC_VAP1(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd, 1, 8, __Value)
#define SET_H2CCMD_BCN_RSVDPAGE_LOC_VAP2(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd, 2, 8, __Value)
#define SET_H2CCMD_BCN_RSVDPAGE_LOC_VAP3(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd, 3, 8, __Value)
#define SET_H2CCMD_BCN_RSVDPAGE_LOC_VAP4(__pH2CCmd, __Value)	SET_BITS_TO_LE_1BYTE(__pH2CCmd, 4, 8, __Value)

	if (dvobj->vap_map & BIT(0))
 		SET_H2CCMD_BCN_RSVDPAGE_LOC_ROOT(bcn_rsvdpage, 0);
	if (dvobj->vap_map & BIT(1))
		SET_H2CCMD_BCN_RSVDPAGE_LOC_VAP1(bcn_rsvdpage,
					1 * (MAX_BEACON_LEN / page_size));
	if (dvobj->vap_map & BIT(2))
		SET_H2CCMD_BCN_RSVDPAGE_LOC_VAP2(bcn_rsvdpage,
					2 * (MAX_BEACON_LEN / page_size));
	if (dvobj->vap_map & BIT(3))
		SET_H2CCMD_BCN_RSVDPAGE_LOC_VAP3(bcn_rsvdpage,
					3 * (MAX_BEACON_LEN / page_size));
	if (dvobj->vap_map & BIT(4))
		SET_H2CCMD_BCN_RSVDPAGE_LOC_VAP4(bcn_rsvdpage,
					4 * (MAX_BEACON_LEN / page_size));
	#endif
	if (1) {
		RTW_INFO("[BCN_LOC] vap_map : 0x%02x\n", dvobj->vap_map);
		RTW_INFO("[BCN_LOC] page_size :%d, @bcn_page_num :%d\n"
			, page_size, (MAX_BEACON_LEN / page_size));
		RTW_INFO("[BCN_LOC] root ap : 0x%02x\n", *bcn_rsvdpage);
		RTW_INFO("[BCN_LOC] vap_1 : 0x%02x\n", *(bcn_rsvdpage + 1));
		RTW_INFO("[BCN_LOC] vap_2 : 0x%02x\n", *(bcn_rsvdpage + 2));
		RTW_INFO("[BCN_LOC] vap_3 : 0x%02x\n", *(bcn_rsvdpage + 3));
		RTW_INFO("[BCN_LOC] vap_4 : 0x%02x\n", *(bcn_rsvdpage + 4));
	}
	ret = rtw_hal_fill_h2c_cmd(adapter, H2C_BCN_RSVDPAGE,
					H2C_BCN_RSVDPAGE_LEN, bcn_rsvdpage);
}

void rtw_ap_multi_bcn_cfg(_adapter *adapter)
{
	u8 dft_bcn_space = DEFAULT_BCN_INTERVAL;
	u8 sub_bcn_space = (DEFAULT_BCN_INTERVAL / CONFIG_LIMITED_AP_NUM);

	/*enable to rx data frame*/
	rtw_write16(adapter, REG_RXFLTMAP2, 0xFFFF);

	/*Disable Port0's beacon function*/
	rtw_write8(adapter, REG_BCN_CTRL, rtw_read8(adapter, REG_BCN_CTRL) & ~BIT_EN_BCN_FUNCTION);
	/*Reset Port0's TSF*/
	rtw_write8(adapter, REG_DUAL_TSF_RST, BIT_TSFTR_RST);

	rtw_ap_set_mbid_num(adapter, CONFIG_LIMITED_AP_NUM);

	/*BCN space & BCN sub-space 0x554[15:0] = 0x64,0x5BC[23:16] = 0x21*/
	rtw_halmac_set_bcn_interval(adapter_to_dvobj(adapter), HW_PORT0, dft_bcn_space);
	rtw_write8(adapter, REG_MBSSID_BCN_SPACE3 + 2, sub_bcn_space);

	#if 0 /*setting in hw_var_set_opmode_mbid - ResumeTxBeacon*/
	/*BCN hold time  0x540[19:8] = 0x80*/
	rtw_write8(adapter, REG_TBTT_PROHIBIT + 1, TBTT_PROHIBIT_HOLD_TIME & 0xFF);
	rtw_write8(adapter, REG_TBTT_PROHIBIT + 2,
		(rtw_read8(adapter, REG_TBTT_PROHIBIT + 2) & 0xF0) | (TBTT_PROHIBIT_HOLD_TIME >> 8));
	#endif

	/*ATIM window -0x55A = 0x32, reg 0x570 = 0x32, reg 0x5A0 = 0x32 */
	rtw_write8(adapter, REG_ATIMWND, 0x32);
	rtw_write8(adapter, REG_ATIMWND1_V1, 0x32);
	rtw_write8(adapter, REG_ATIMWND2, 0x32);
	rtw_write8(adapter, REG_ATIMWND3, 0x32);
	/*
	rtw_write8(adapter, REG_ATIMWND4, 0x32);
	rtw_write8(adapter, REG_ATIMWND5, 0x32);
	rtw_write8(adapter, REG_ATIMWND6, 0x32);
	rtw_write8(adapter, REG_ATIMWND7, 0x32);*/

	/*no limit setting - 0x5A7 = 0xFF - Packet in Hi Queue Tx immediately*/
	rtw_write8(adapter, REG_HIQ_NO_LMT_EN, 0xFF);

	/*Mask all beacon*/
	rtw_write8(adapter, REG_MBSSID_CTRL, 0);

	/*BCN invalid bit setting 0x454[6] = 1*/
	/*rtw_write8(adapter, REG_CCK_CHECK, rtw_read8(adapter, REG_CCK_CHECK) | BIT_EN_BCN_PKT_REL);*/

	/*Enable Port0's beacon function*/
	rtw_write8(adapter, REG_BCN_CTRL,
	rtw_read8(adapter, REG_BCN_CTRL) | BIT_DIS_RX_BSSID_FIT | BIT_P0_EN_TXBCN_RPT | BIT_DIS_TSF_UDT  | BIT_EN_BCN_FUNCTION);

	/* Enable HW seq for BCN
	* 0x4FC[0]: EN_HWSEQ / 0x4FC[1]: EN_HWSEQEXT  */
	 #ifdef CONFIG_RTL8822B
	if (IS_HARDWARE_TYPE_8822B(adapter))
		rtw_write8(adapter, REG_DUMMY_PAGE4_V1_8822B, 0x01);
	#endif

	 #ifdef CONFIG_RTL8822C
	if (IS_HARDWARE_TYPE_8822C(adapter))
		rtw_write8(adapter, REG_DUMMY_PAGE4_V1_8822C, 0x01);
	#endif
}
static void _rtw_mbid_bcn_cfg(_adapter *adapter, bool mbcnq_en, u8 mbcnq_id)
{
	if (mbcnq_id >= CONFIG_LIMITED_AP_NUM) {
		RTW_ERR(FUNC_ADPT_FMT"- mbid bcnq_id(%d) invalid\n", FUNC_ADPT_ARG(adapter), mbcnq_id);
		rtw_warn_on(1);
	}

	if (mbcnq_en) {
		rtw_write8(adapter, REG_MBSSID_CTRL,
			rtw_read8(adapter, REG_MBSSID_CTRL) | BIT(mbcnq_id));
		RTW_INFO(FUNC_ADPT_FMT"- mbid bcnq_id(%d) enabled\n", FUNC_ADPT_ARG(adapter), mbcnq_id);
	} else {
		rtw_write8(adapter, REG_MBSSID_CTRL,
			rtw_read8(adapter, REG_MBSSID_CTRL) & (~BIT(mbcnq_id)));
		RTW_INFO(FUNC_ADPT_FMT"- mbid bcnq_id(%d) disabled\n", FUNC_ADPT_ARG(adapter), mbcnq_id);
	}
}
/*#define CONFIG_FW_TBTT_RPT*/
void rtw_ap_mbid_bcn_en(_adapter *adapter, u8 ap_id)
{
	RTW_INFO(FUNC_ADPT_FMT"- ap_id(%d)\n", FUNC_ADPT_ARG(adapter), ap_id);

	#ifdef CONFIG_FW_TBTT_RPT
	if (rtw_ap_get_nums(adapter) >= 1) {
		u8 tbtt_rpt_map = adapter_to_dvobj(adapter)->vap_tbtt_rpt_map;

		rtw_hal_set_fw_ap_bcn_offload_cmd(adapter, _TRUE,
			tbtt_rpt_map | BIT(ap_id));/*H2C-0xBA*/
	}
	#else
	if (rtw_ap_get_nums(adapter) == 1)
		rtw_hal_set_fw_ap_bcn_offload_cmd(adapter, _TRUE, 0);/*H2C-0xBA*/
	#endif

	rtw_hal_set_bcn_rsvdpage_loc_cmd(adapter);/*H2C-0x09*/

	_rtw_mbid_bcn_cfg(adapter, _TRUE, ap_id);
}
void rtw_ap_mbid_bcn_dis(_adapter *adapter, u8 ap_id)
{
	RTW_INFO(FUNC_ADPT_FMT"- ap_id(%d)\n", FUNC_ADPT_ARG(adapter), ap_id);
	_rtw_mbid_bcn_cfg(adapter, _FALSE, ap_id);

	if (rtw_ap_get_nums(adapter) == 0)
		rtw_hal_set_fw_ap_bcn_offload_cmd(adapter, _FALSE, 0);
	#ifdef CONFIG_FW_TBTT_RPT
	else if (rtw_ap_get_nums(adapter) >= 1) {
		u8 tbtt_rpt_map = adapter_to_dvobj(adapter)->vap_tbtt_rpt_map;

		rtw_hal_set_fw_ap_bcn_offload_cmd(adapter, _TRUE,
			tbtt_rpt_map & ~BIT(ap_id));/*H2C-0xBA*/
	}
	#endif
}
#endif
#ifdef CONFIG_SWTIMER_BASED_TXBCN
void rtw_ap_multi_bcn_cfg(_adapter *adapter)
{
	#if defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8822C)
	rtw_write8(adapter, REG_BCN_CTRL, DIS_TSF_UDT);
	#else
	rtw_write8(adapter, REG_BCN_CTRL, DIS_TSF_UDT | DIS_BCNQ_SUB);
	#endif
	/*enable to rx data frame*/
	rtw_write16(adapter, REG_RXFLTMAP2, 0xFFFF);

	/*Beacon Control related register for first time*/
	rtw_write8(adapter, REG_BCNDMATIM, 0x02); /* 2ms */

	/*rtw_write8(Adapter, REG_BCN_MAX_ERR, 0xFF);*/
	rtw_write8(adapter, REG_ATIMWND, 0x0c); /* 12ms */

	#ifndef CONFIG_HW_P0_TSF_SYNC
	rtw_write16(adapter, REG_TSFTR_SYN_OFFSET, 0x7fff);/* +32767 (~32ms) */
	#endif

	/*reset TSF*/
	rtw_write8(adapter, REG_DUAL_TSF_RST, BIT(0));

	/*enable BCN0 Function for if1*/
	/*don't enable update TSF0 for if1 (due to TSF update when beacon,probe rsp are received)*/
	#if defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8822C)
	rtw_write8(adapter, REG_BCN_CTRL, BIT_DIS_RX_BSSID_FIT | BIT_P0_EN_TXBCN_RPT | BIT_DIS_TSF_UDT |BIT_EN_BCN_FUNCTION);
	#else
	rtw_write8(adapter, REG_BCN_CTRL, (DIS_TSF_UDT | EN_BCN_FUNCTION | EN_TXBCN_RPT | DIS_BCNQ_SUB));
	#endif
	#ifdef CONFIG_BCN_XMIT_PROTECT
	rtw_write8(adapter, REG_CCK_CHECK, rtw_read8(adapter, REG_CCK_CHECK) | BIT_EN_BCN_PKT_REL);
	#endif

	if (IS_HARDWARE_TYPE_8821(adapter) || IS_HARDWARE_TYPE_8192E(adapter))/* select BCN on port 0 for DualBeacon*/
		rtw_write8(adapter, REG_CCK_CHECK, rtw_read8(adapter, REG_CCK_CHECK) & (~BIT_BCN_PORT_SEL));

	/* Enable HW seq for BCN 
	 * 0x4FC[0]: EN_HWSEQ / 0x4FC[1]: EN_HWSEQEXT  */
	#ifdef CONFIG_RTL8822B
	if (IS_HARDWARE_TYPE_8822B(adapter))
		rtw_write8(adapter, REG_DUMMY_PAGE4_V1_8822B, 0x01);
	#endif

	#ifdef CONFIG_RTL8822C
	if (IS_HARDWARE_TYPE_8822C(adapter))
		rtw_write8(adapter, REG_DUMMY_PAGE4_V1_8822C, 0x01);
	#endif
}
#endif

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
			RTW_INFO("%s "ADPT_FMT"- mbid(%d) mac_addr ="MAC_FMT"\n", __func__,
				ADPT_ARG(adapter), entry_id, MAC_ARG(mac_addr));
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

#ifdef CONFIG_SWTIMER_BASED_TXBCN
u16 rtw_hal_bcn_interval_adjust(_adapter *adapter, u16 bcn_interval)
{
	if (adapter_to_dvobj(adapter)->inter_bcn_space != bcn_interval)
		return adapter_to_dvobj(adapter)->inter_bcn_space;
	else
		return bcn_interval;
}
#endif/*CONFIG_SWTIMER_BASED_TXBCN*/

#else

#ifndef RTW_HALMAC
static u32 _get_macaddr_reg(enum _hw_port hwport)
{
	u32 reg_macaddr = REG_MACID;

	#ifdef CONFIG_CONCURRENT_MODE
	if (hwport == HW_PORT1)
		reg_macaddr = REG_MACID1;
	#if defined(CONFIG_RTL8814A)
	else if (hwport == HW_PORT2)
		reg_macaddr = REG_MACID2;
	else if (hwport == HW_PORT3)
		reg_macaddr = REG_MACID3;
	else if (hwport == HW_PORT4)
		reg_macaddr = REG_MACID4;
	#endif /*CONFIG_RTL8814A*/
	#endif /*CONFIG_CONCURRENT_MODE*/

	return reg_macaddr;
}
#endif /*!RTW_HALMAC*/

static void rtw_hal_set_macaddr_port(_adapter *adapter, u8 *mac_addr)
{
	enum _hw_port hwport;

	if (mac_addr == NULL)
		return;
	hwport = get_hw_port(adapter);

	RTW_INFO("%s "ADPT_FMT"- hw port(%d) mac_addr ="MAC_FMT"\n",  __func__,
		 ADPT_ARG(adapter), hwport, MAC_ARG(mac_addr));

#ifdef RTW_HALMAC /*8822B ~ 8814B*/
	rtw_halmac_set_mac_address(adapter_to_dvobj(adapter), hwport, mac_addr);
#else /* !RTW_HALMAC */
	{
		u8 idx = 0;
		u32 reg_macaddr = _get_macaddr_reg(hwport);

		for (idx = 0; idx < ETH_ALEN; idx++)
			rtw_write8(GET_PRIMARY_ADAPTER(adapter), (reg_macaddr + idx), mac_addr[idx]);
	}
#endif /* !RTW_HALMAC */
}
#endif/*#ifdef CONFIG_MI_WITH_MBSSID_CAM*/

static void rtw_hal_get_macaddr_port(_adapter *adapter, u8 *mac_addr)
{
	enum _hw_port hwport;

	if (mac_addr == NULL)
		return;
	hwport = get_hw_port(adapter);

	_rtw_memset(mac_addr, 0, ETH_ALEN);
#ifdef RTW_HALMAC /*8822B ~ 8814B*/
	rtw_halmac_get_mac_address(adapter_to_dvobj(adapter), hwport, mac_addr);
#else /* !RTW_HALMAC */
	{
		u8 idx = 0;
		u32 reg_macaddr = _get_macaddr_reg(hwport);

		for (idx = 0; idx < ETH_ALEN; idx++)
			mac_addr[idx] = rtw_read8(GET_PRIMARY_ADAPTER(adapter), (reg_macaddr + idx));
	}
#endif /* !RTW_HALMAC */

	RTW_INFO("%s "ADPT_FMT"- hw port(%d) mac_addr ="MAC_FMT"\n",  __func__,
		 ADPT_ARG(adapter), hwport, MAC_ARG(mac_addr));
}

#ifndef RTW_HALMAC
static u32 _get_bssid_reg(enum _hw_port hw_port)
{
	u32 reg_bssid = REG_BSSID;

	#ifdef CONFIG_CONCURRENT_MODE
	if (hw_port == HW_PORT1)
		reg_bssid = REG_BSSID1;
	#if defined(CONFIG_RTL8814A)
	else if (hw_port == HW_PORT2)
		reg_bssid = REG_BSSID2;
	else if (hw_port == HW_PORT3)
		reg_bssid = REG_BSSID3;
	else if (hw_port == HW_PORT4)
		reg_bssid = REG_BSSID4;
	#endif /*CONFIG_RTL8814A*/
	#endif /*CONFIG_CONCURRENT_MODE*/

	return reg_bssid;
}
#endif /*!RTW_HALMAC*/
static void rtw_hal_set_bssid(_adapter *adapter, u8 *val)
{
	enum _hw_port hw_port = rtw_hal_get_port(adapter);
#ifdef RTW_HALMAC

	rtw_halmac_set_bssid(adapter_to_dvobj(adapter), hw_port, val);
#else /* !RTW_HALMAC */
	u8 idx = 0;
	u32 reg_bssid = _get_bssid_reg(hw_port);

	for (idx = 0 ; idx < ETH_ALEN; idx++)
		rtw_write8(adapter, (reg_bssid + idx), val[idx]);
#endif /* !RTW_HALMAC */

	RTW_INFO("%s "ADPT_FMT"- hw port -%d BSSID: "MAC_FMT"\n",
		__func__, ADPT_ARG(adapter), hw_port, MAC_ARG(val));
}

static void rtw_hal_set_tsf_update(_adapter *adapter, u8 en)
{
	u32 addr = 0;
	u8 val8;

	rtw_hal_get_hwreg(adapter, HW_VAR_BCN_CTRL_ADDR, (u8 *)&addr);
	if (addr) {
		rtw_enter_protsel_port(adapter, get_hw_port(adapter));
		val8 = rtw_read8(adapter, addr);
		if (en && (val8 & DIS_TSF_UDT)) {
			rtw_write8(adapter, addr, val8 & ~DIS_TSF_UDT);
			#ifdef DBG_TSF_UPDATE
			RTW_INFO("port%u("ADPT_FMT") enable TSF update\n", adapter->hw_port, ADPT_ARG(adapter));
			#endif
		}
		if (!en && !(val8 & DIS_TSF_UDT)) {
			rtw_write8(adapter, addr, val8 | DIS_TSF_UDT);
			#ifdef DBG_TSF_UPDATE
			RTW_INFO("port%u("ADPT_FMT") disable TSF update\n", adapter->hw_port, ADPT_ARG(adapter));
			#endif
		}
		rtw_leave_protsel_port(adapter);
	} else {
		RTW_WARN("unknown port%d("ADPT_FMT") %s TSF update\n"
			, adapter->hw_port, ADPT_ARG(adapter), en ? "enable" : "disable");
		rtw_warn_on(1);
	}
}

static void rtw_hal_set_hw_update_tsf(PADAPTER padapter)
{
#ifdef CONFIG_MI_WITH_MBSSID_CAM

#else
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	if (!pmlmeext->en_hw_update_tsf)
		return;

	/* check RCR */
	if (!rtw_hal_rcr_check(padapter, RCR_CBSSID_BCN))
		return;

	if (pmlmeext->tsf_update_required) {
		pmlmeext->tsf_update_pause_stime = 0;
		rtw_hal_set_tsf_update(padapter, 1);
	}

	pmlmeext->en_hw_update_tsf = 0;
#endif
}

void rtw_iface_enable_tsf_update(_adapter *adapter)
{
	adapter->mlmeextpriv.tsf_update_pause_stime = 0;
	adapter->mlmeextpriv.tsf_update_required = 1;
#ifdef CONFIG_MI_WITH_MBSSID_CAM

#else
	rtw_hal_set_tsf_update(adapter, 1);
#endif
}

void rtw_iface_disable_tsf_update(_adapter *adapter)
{
	adapter->mlmeextpriv.tsf_update_required = 0;
	adapter->mlmeextpriv.tsf_update_pause_stime = 0;
	adapter->mlmeextpriv.en_hw_update_tsf = 0;
#ifdef CONFIG_MI_WITH_MBSSID_CAM

#else
	rtw_hal_set_tsf_update(adapter, 0);
#endif
}

static void rtw_hal_tsf_update_pause(_adapter *adapter)
{
#ifdef CONFIG_MI_WITH_MBSSID_CAM

#else
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	_adapter *iface;
	int i;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (!iface)
			continue;

		rtw_hal_set_tsf_update(iface, 0);
		if (iface->mlmeextpriv.tsf_update_required) {
			iface->mlmeextpriv.tsf_update_pause_stime = rtw_get_current_time();
			if (!iface->mlmeextpriv.tsf_update_pause_stime)
				iface->mlmeextpriv.tsf_update_pause_stime++;
		}
		iface->mlmeextpriv.en_hw_update_tsf = 0;
	}
#endif
}

static void rtw_hal_tsf_update_restore(_adapter *adapter)
{
#ifdef CONFIG_MI_WITH_MBSSID_CAM

#else
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	_adapter *iface;
	int i;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (!iface)
			continue;

		if (iface->mlmeextpriv.tsf_update_required) {
			/* enable HW TSF update when recive beacon*/
			iface->mlmeextpriv.en_hw_update_tsf = 1;
			#ifdef DBG_TSF_UPDATE
			RTW_INFO("port%d("ADPT_FMT") enabling TSF update...\n"
				, iface->hw_port, ADPT_ARG(iface));
			#endif
		}
	}
#endif
}

void rtw_hal_periodic_tsf_update_chk(_adapter *adapter)
{
#ifdef CONFIG_MI_WITH_MBSSID_CAM

#else
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	_adapter *iface;
	struct mlme_ext_priv *mlmeext;
	int i;
	u32 restore_ms = 0;

	if (dvobj->periodic_tsf_update_etime) {
		if (rtw_time_after(rtw_get_current_time(), dvobj->periodic_tsf_update_etime)) {
			/* end for restore status */
			dvobj->periodic_tsf_update_etime = 0;
			rtw_hal_rcr_set_chk_bssid(adapter, MLME_ACTION_NONE);
		}
		return;
	}

	if (dvobj->rf_ctl.offch_state != OFFCHS_NONE)
		return;

	/*
	* all required ifaces can switch to restore status together
	* loop all pause iface to get largest restore time required
	*/
	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (!iface)
			continue;

		mlmeext = &iface->mlmeextpriv;

		if (mlmeext->tsf_update_required
			&& mlmeext->tsf_update_pause_stime
			&& rtw_get_passing_time_ms(mlmeext->tsf_update_pause_stime)
				> mlmeext->mlmext_info.bcn_interval * mlmeext->tsf_update_pause_factor
		) {
			if (restore_ms < mlmeext->mlmext_info.bcn_interval * mlmeext->tsf_update_restore_factor)
				restore_ms = mlmeext->mlmext_info.bcn_interval * mlmeext->tsf_update_restore_factor;
		}
	}

	if (!restore_ms)
		return;

	dvobj->periodic_tsf_update_etime = rtw_get_current_time() + rtw_ms_to_systime(restore_ms);
	if (!dvobj->periodic_tsf_update_etime)
		dvobj->periodic_tsf_update_etime++;

	rtw_hal_rcr_set_chk_bssid(adapter, MLME_ACTION_NONE);

	/* set timer to end restore status */
	_set_timer(&dvobj->periodic_tsf_update_end_timer, restore_ms);
#endif
}

void rtw_hal_periodic_tsf_update_end_timer_hdl(void *ctx)
{
	struct dvobj_priv *dvobj = (struct dvobj_priv *)ctx;

	if (dev_is_surprise_removed(dvobj) || dev_is_drv_stopped(dvobj))
		return;

	rtw_periodic_tsf_update_end_cmd(dvobj_get_primary_adapter(dvobj));
}

static inline u8 hw_var_rcr_config(_adapter *adapter, u32 rcr)
{
	int err;

#ifdef CONFIG_CUSTOMER_ALIBABA_GENERAL
	rcr = RCR_AAP | RCR_APM | RCR_AM | RCR_AB | RCR_APWRMGT | RCR_ADF | RCR_AMF | RCR_APP_PHYST_RXFF | RCR_APP_MIC | RCR_APP_ICV;
#endif
	err = rtw_write32(adapter, REG_RCR, rcr);
	if (err == _SUCCESS)
		GET_HAL_DATA(adapter)->ReceiveConfig = rcr;
	return err;
}

static inline u8 hw_var_rcr_get(_adapter *adapter, u32 *rcr)
{
	u32 v32;

	v32 = rtw_read32(adapter, REG_RCR);
	if (rcr)
		*rcr = v32;
	GET_HAL_DATA(adapter)->ReceiveConfig = v32;
	return _SUCCESS;
}

/* only check SW RCR variable */
inline u8 rtw_hal_rcr_check(_adapter *adapter, u32 check_bit)
{
	PHAL_DATA_TYPE hal;
	u32 rcr;

	hal = GET_HAL_DATA(adapter);

	rcr = hal->ReceiveConfig;
	if ((rcr & check_bit) == check_bit)
		return 1;

	return 0;
}

inline u8 rtw_hal_rcr_add(_adapter *adapter, u32 add)
{
	PHAL_DATA_TYPE hal;
	u32 rcr;
	u8 ret = _SUCCESS;

	hal = GET_HAL_DATA(adapter);

	rtw_hal_get_hwreg(adapter, HW_VAR_RCR, (u8 *)&rcr);
	rcr |= add;
	if (rcr != hal->ReceiveConfig)
		ret = rtw_hal_set_hwreg(adapter, HW_VAR_RCR, (u8 *)&rcr);

	return ret;
}

inline u8 rtw_hal_rcr_clear(_adapter *adapter, u32 clear)
{
	PHAL_DATA_TYPE hal;
	u32 rcr;
	u8 ret = _SUCCESS;

	hal = GET_HAL_DATA(adapter);

	rtw_hal_get_hwreg(adapter, HW_VAR_RCR, (u8 *)&rcr);
	rcr &= ~clear;
	if (rcr != hal->ReceiveConfig)
		ret = rtw_hal_set_hwreg(adapter, HW_VAR_RCR, (u8 *)&rcr);

	return ret;
}

void rtw_hal_rcr_set_chk_bssid(_adapter *adapter, u8 self_action)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u32 rcr, rcr_new;
	struct mi_state mstate, mstate_s;

	rtw_hal_get_hwreg(adapter, HW_VAR_RCR, (u8 *)&rcr);
	rcr_new = rcr;

#if defined(CONFIG_MI_WITH_MBSSID_CAM) && !defined(CONFIG_CLIENT_PORT_CFG)
	rcr_new &= ~(RCR_CBSSID_BCN | RCR_CBSSID_DATA);
#else
	rtw_mi_status_no_self(adapter, &mstate);
	rtw_mi_status_no_others(adapter, &mstate_s);

	/* only adjust parameters interested */
	switch (self_action) {
	case MLME_SCAN_ENTER:
		mstate_s.scan_num = 1;
		mstate_s.scan_enter_num = 1;
		break;
	case MLME_SCAN_DONE:
		mstate_s.scan_enter_num = 0;
		break;
	case MLME_STA_CONNECTING:
		mstate_s.lg_sta_num = 1;
		mstate_s.ld_sta_num = 0;
		break;
	case MLME_STA_CONNECTED:
		mstate_s.lg_sta_num = 0;
		mstate_s.ld_sta_num = 1;
		break;
	case MLME_STA_DISCONNECTED:
		mstate_s.lg_sta_num = 0;
		mstate_s.ld_sta_num = 0;
		break;
#ifdef CONFIG_TDLS
	case MLME_TDLS_LINKED:
		mstate_s.ld_tdls_num = 1;
		break;
	case MLME_TDLS_NOLINK:
		mstate_s.ld_tdls_num = 0;
		break;
#endif
#ifdef CONFIG_AP_MODE
	case MLME_AP_STARTED:
		mstate_s.ap_num = 1;
		break;
	case MLME_AP_STOPPED:
		mstate_s.ap_num = 0;
		mstate_s.ld_ap_num = 0;
		break;
#endif
#ifdef CONFIG_RTW_MESH
	case MLME_MESH_STARTED:
		mstate_s.mesh_num = 1;
		break;
	case MLME_MESH_STOPPED:
		mstate_s.mesh_num = 0;
		mstate_s.ld_mesh_num = 0;
		break;
#endif
	case MLME_ACTION_NONE:
	case MLME_ADHOC_STARTED:
		/* caller without effect of decision */
		break;
	default:
		rtw_warn_on(1);
	};

	rtw_mi_status_merge(&mstate, &mstate_s);

	if (MSTATE_AP_NUM(&mstate) || MSTATE_MESH_NUM(&mstate) || MSTATE_TDLS_LD_NUM(&mstate)
		#ifdef CONFIG_FIND_BEST_CHANNEL
		|| MSTATE_SCAN_ENTER_NUM(&mstate)
		#endif
		|| hal_data->in_cta_test
	)
		rcr_new &= ~RCR_CBSSID_DATA;
	else
		rcr_new |= RCR_CBSSID_DATA;

	if (MSTATE_SCAN_ENTER_NUM(&mstate) || hal_data->in_cta_test)
		rcr_new &= ~RCR_CBSSID_BCN;
	else if (MSTATE_STA_LG_NUM(&mstate)
		|| adapter_to_dvobj(adapter)->periodic_tsf_update_etime
	)
		rcr_new |= RCR_CBSSID_BCN;
	else if ((MSTATE_AP_NUM(&mstate) && adapter->registrypriv.wifi_spec) /* for 11n Logo 4.2.31/4.2.32 */
		|| MSTATE_MESH_NUM(&mstate)
	)
		rcr_new &= ~RCR_CBSSID_BCN;	
	else
		rcr_new |= RCR_CBSSID_BCN;

	#ifdef CONFIG_CLIENT_PORT_CFG
	if (get_clt_num(adapter) > MAX_CLIENT_PORT_NUM)
		rcr_new &= ~RCR_CBSSID_BCN;
	#endif
#endif /* CONFIG_MI_WITH_MBSSID_CAM */

	if (rcr == rcr_new)
		return;

	if (!hal_spec->rx_tsf_filter
		&& (rcr & RCR_CBSSID_BCN) && !(rcr_new & RCR_CBSSID_BCN))
		rtw_hal_tsf_update_pause(adapter);

	rtw_hal_set_hwreg(adapter, HW_VAR_RCR, (u8 *)&rcr_new);

	if (!hal_spec->rx_tsf_filter
		&& !(rcr & RCR_CBSSID_BCN) && (rcr_new & RCR_CBSSID_BCN)
		&& self_action != MLME_STA_CONNECTING)
		rtw_hal_tsf_update_restore(adapter);
}

static void hw_var_set_rcr_am(_adapter *adapter, u8 enable)
{
	u32 rcr = RCR_AM;

	if (enable)
		rtw_hal_rcr_add(adapter, rcr);
	else
		rtw_hal_rcr_clear(adapter, rcr);
}

static void hw_var_set_bcn_interval(_adapter *adapter, u16 interval)
{
#ifdef CONFIG_SWTIMER_BASED_TXBCN
	interval = rtw_hal_bcn_interval_adjust(adapter, interval);
#endif

#ifdef RTW_HALMAC
	rtw_halmac_set_bcn_interval(adapter_to_dvobj(adapter), adapter->hw_port, interval);
#else
	rtw_write16(adapter, REG_MBSSID_BCN_SPACE, interval);
#endif

#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
	{
		struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
		struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

		if ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) {
			RTW_INFO("%s==> bcn_interval:%d, eraly_int:%d\n", __func__, interval, interval >> 1);
			rtw_write8(adapter, REG_DRVERLYINT, interval >> 1);
		}
	}
#endif
}

#if CONFIG_TX_AC_LIFETIME
const char *const _tx_aclt_conf_str[] = {
	"DEFAULT",
	"AP_M2U",
	"MESH",
	"INVALID",
};

void dump_tx_aclt_force_val(void *sel, struct dvobj_priv *dvobj)
{
#define TX_ACLT_FORCE_MSG_LEN 64
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(dvobj_get_primary_adapter(dvobj));
	struct tx_aclt_conf_t *conf = &dvobj->tx_aclt_force_val;
	char buf[TX_ACLT_FORCE_MSG_LEN];
	int cnt = 0;

	RTW_PRINT_SEL(sel, "unit:%uus, maximum:%uus\n"
		, hal_spec->tx_aclt_unit_factor * 32
		, 0xFFFF * hal_spec->tx_aclt_unit_factor * 32);

	RTW_PRINT_SEL(sel, "%-5s %-12s %-12s\n", "en", "vo_vi(us)", "be_bk(us)");
	RTW_PRINT_SEL(sel, " 0x%02x %12u %12u\n"
		, conf->en
		, conf->vo_vi * hal_spec->tx_aclt_unit_factor * 32
		, conf->be_bk * hal_spec->tx_aclt_unit_factor * 32
	);

	cnt += snprintf(buf + cnt, TX_ACLT_FORCE_MSG_LEN - cnt - 1, "%5s", conf->en == 0xFF ? "AUTO" : "FORCE");
	if (cnt >= TX_ACLT_FORCE_MSG_LEN - 1)
		goto exit;

	if (conf->vo_vi)
		cnt += snprintf(buf + cnt, TX_ACLT_FORCE_MSG_LEN - cnt - 1, " FORCE:0x%04x", conf->vo_vi);
	else
		cnt += snprintf(buf + cnt, TX_ACLT_FORCE_MSG_LEN - cnt - 1, "         AUTO");
	if (cnt >= TX_ACLT_FORCE_MSG_LEN - 1)
		goto exit;


	if (conf->be_bk)
		cnt += snprintf(buf + cnt, TX_ACLT_FORCE_MSG_LEN - cnt - 1, " FORCE:0x%04x", conf->be_bk);
	else
		cnt += snprintf(buf + cnt, TX_ACLT_FORCE_MSG_LEN - cnt - 1, "         AUTO");
	if (cnt >= TX_ACLT_FORCE_MSG_LEN - 1)
		goto exit;

	RTW_PRINT_SEL(sel, "%s\n", buf);

exit:
	return;
}

void rtw_hal_set_tx_aclt_force_val(_adapter *adapter, struct tx_aclt_conf_t *input, u8 arg_num)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct tx_aclt_conf_t *conf = &dvobj->tx_aclt_force_val;

	if (arg_num >= 1) {
		if (input->en == 0xFF)
			conf->en = input->en;
		else
			conf->en = input->en & 0xF;
	}
	if (arg_num >= 2) {
		conf->vo_vi = input->vo_vi / (hal_spec->tx_aclt_unit_factor * 32);
		if (conf->vo_vi > 0xFFFF)
			conf->vo_vi = 0xFFFF;
	}
	if (arg_num >= 3) {
		conf->be_bk = input->be_bk / (hal_spec->tx_aclt_unit_factor * 32);
		if (conf->be_bk > 0xFFFF)
			conf->be_bk = 0xFFFF;
	}
}

void dump_tx_aclt_confs(void *sel, struct dvobj_priv *dvobj)
{
#define TX_ACLT_CONF_MSG_LEN 32
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(dvobj_get_primary_adapter(dvobj));
	struct tx_aclt_conf_t *conf;
	char buf[TX_ACLT_CONF_MSG_LEN];
	int cnt;
	int i;

	RTW_PRINT_SEL(sel, "unit:%uus, maximum:%uus\n"
		, hal_spec->tx_aclt_unit_factor * 32
		, 0xFFFF * hal_spec->tx_aclt_unit_factor * 32);

	RTW_PRINT_SEL(sel, "%-7s %-1s %-3s %-9s %-9s %-10s %-10s\n"
		, "name", "#", "en", "vo_vi(us)", "be_bk(us)", "vo_vi(reg)", "be_bk(reg)");

	for (i = 0; i < TX_ACLT_CONF_NUM; i++) {
		conf = &dvobj->tx_aclt_confs[i];
		cnt = 0;

		if (conf->vo_vi)
			cnt += snprintf(buf + cnt, TX_ACLT_CONF_MSG_LEN - cnt - 1, "     0x%04x", conf->vo_vi);
		else
			cnt += snprintf(buf + cnt, TX_ACLT_CONF_MSG_LEN - cnt - 1, "        N/A");
		if (cnt >= TX_ACLT_CONF_MSG_LEN - 1)
			continue;
		
		if (conf->be_bk)
			cnt += snprintf(buf + cnt, TX_ACLT_CONF_MSG_LEN - cnt - 1, "     0x%04x", conf->be_bk);
		else
			cnt += snprintf(buf + cnt, TX_ACLT_CONF_MSG_LEN - cnt - 1, "        N/A");
		if (cnt >= TX_ACLT_CONF_MSG_LEN - 1)
			continue;

		RTW_PRINT_SEL(sel, "%7s %1u 0x%x %9u %9u%s\n"
			, tx_aclt_conf_str(i), i
			, conf->en
			, conf->vo_vi * hal_spec->tx_aclt_unit_factor * 32
			, conf->be_bk * hal_spec->tx_aclt_unit_factor * 32
			, buf
		);
	}
}

void rtw_hal_set_tx_aclt_conf(_adapter *adapter, u8 conf_idx, struct tx_aclt_conf_t *input, u8 arg_num)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct tx_aclt_conf_t *conf;

	if (conf_idx >= TX_ACLT_CONF_NUM)
		return;

	conf = &dvobj->tx_aclt_confs[conf_idx];

	if (arg_num >= 1) {
		if (input->en != 0xFF)
			conf->en = input->en & 0xF;
	}
	if (arg_num >= 2) {
		conf->vo_vi = input->vo_vi / (hal_spec->tx_aclt_unit_factor * 32);
		if (conf->vo_vi > 0xFFFF)
			conf->vo_vi = 0xFFFF;
	}
	if (arg_num >= 3) {
		conf->be_bk = input->be_bk / (hal_spec->tx_aclt_unit_factor * 32);
		if (conf->be_bk > 0xFFFF)
			conf->be_bk = 0xFFFF;
	}
}

void rtw_hal_update_tx_aclt(_adapter *adapter)
{
#ifdef CONFIG_TX_MCAST2UNI
	extern int rtw_mc2u_disable;
#endif
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct macid_ctl_t *macid_ctl = adapter_to_macidctl(adapter);
	u8 lt_en = 0, lt_en_ori;
	u16 lt_vo_vi = 0xFFFF, lt_be_bk = 0xFFFF;
	u32 lt, lt_ori;
	struct tx_aclt_conf_t *conf;
	int i;

	lt_en_ori = rtw_read8(adapter, REG_LIFETIME_EN);
	lt_ori = rtw_read32(adapter, REG_PKT_LIFE_TIME);

	for (i = 0; i < TX_ACLT_CONF_NUM; i++) {
		if (!(dvobj->tx_aclt_flags & BIT(i)))
			continue;

		conf = &dvobj->tx_aclt_confs[i];

		if (i == TX_ACLT_CONF_DEFAULT) {
			/* first and default status, assign directly */
			lt_en = conf->en;
			if (conf->vo_vi)
				lt_vo_vi = conf->vo_vi;
			if (conf->be_bk)
				lt_be_bk = conf->be_bk;
		}
		#if defined(CONFIG_TX_MCAST2UNI) || defined(CONFIG_RTW_MESH)
		else if (0
			#ifdef CONFIG_TX_MCAST2UNI
			|| (i == TX_ACLT_CONF_AP_M2U
				&& !rtw_mc2u_disable
				&& macid_ctl->op_num[H2C_MSR_ROLE_STA] /* having AP mode with STA connected */)
			#endif
			#ifdef CONFIG_RTW_MESH
			|| (i == TX_ACLT_CONF_MESH
				&& macid_ctl->op_num[H2C_MSR_ROLE_MESH] > 1 /* implies only 1 MESH mode supported */)
			#endif
		) {
			/* long term status, OR en and MIN lifetime */
			lt_en |= conf->en;
			if (conf->vo_vi && lt_vo_vi > conf->vo_vi)
				lt_vo_vi = conf->vo_vi;
			if (conf->be_bk && lt_be_bk > conf->be_bk)
				lt_be_bk = conf->be_bk;
		}
		#endif
	}

	if (dvobj->tx_aclt_force_val.en != 0xFF)
		lt_en = dvobj->tx_aclt_force_val.en;
	if (dvobj->tx_aclt_force_val.vo_vi)
		lt_vo_vi = dvobj->tx_aclt_force_val.vo_vi;
	if (dvobj->tx_aclt_force_val.be_bk)
		lt_be_bk = dvobj->tx_aclt_force_val.be_bk;

	lt_en = (lt_en_ori & 0xF0) | (lt_en & 0x0F);
	lt = (lt_be_bk << 16) | lt_vo_vi;

	if (0)
		RTW_INFO("lt_en:0x%x(0x%x), lt:0x%08x(0x%08x)\n", lt_en, lt_en_ori, lt, lt_ori);

	if (lt_en != lt_en_ori)
		rtw_write8(adapter, REG_LIFETIME_EN, lt_en);
	if (lt != lt_ori)
		rtw_write32(adapter, REG_PKT_LIFE_TIME, lt);
}
#endif /* CONFIG_TX_AC_LIFETIME */

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
#if defined(CONFIG_RTL8192F)
	u16 wlan_act_mask_ctrl = 0;
	u16 en_port_mask = EN_PORT_0_FUNCTION | EN_PORT_1_FUNCTION;
#endif

	u8 hw_port;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	_adapter *iface = NULL;

	msr = rtw_read8(adapter, MSR);
	bcn_ctrl = rtw_read8(adapter, REG_BCN_CTRL);
	bcn_ctrl_1 = rtw_read8(adapter, REG_BCN_CTRL_1);
#if defined(CONFIG_RTL8192F)
	wlan_act_mask_ctrl = rtw_read16(adapter, REG_WLAN_ACT_MASK_CTRL_1);
#endif

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
#if defined(CONFIG_RTL8192F)
		 "wlan_act_mask_ctrl:0x%02x\n"
#endif
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
#if defined(CONFIG_RTL8192F)
		 , wlan_act_mask_ctrl
#endif
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

#if defined(CONFIG_RTL8192F)
	rtw_write16(adapter, REG_WLAN_ACT_MASK_CTRL_1, wlan_act_mask_ctrl & ~en_port_mask);
#endif

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

#if defined(CONFIG_RTL8192F)
	/* if the setting of port0 and port1 are the same, it does not need to switch port setting*/
	if(((wlan_act_mask_ctrl & en_port_mask) != 0) && ((wlan_act_mask_ctrl & en_port_mask)
		!= (EN_PORT_0_FUNCTION | EN_PORT_1_FUNCTION)))
		wlan_act_mask_ctrl ^= en_port_mask;
	rtw_write16(adapter, REG_WLAN_ACT_MASK_CTRL_1, wlan_act_mask_ctrl);
#endif

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
#if defined(CONFIG_RTL8192F)
	wlan_act_mask_ctrl = rtw_read16(adapter, REG_WLAN_ACT_MASK_CTRL_1);
#endif

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
#if defined(CONFIG_RTL8192F)
		 "wlan_act_mask_ctrl:0x%02x\n"
#endif
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
#if defined(CONFIG_RTL8192F)
		 , wlan_act_mask_ctrl
#endif
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
	"MESH",
	"INVALID",
};

#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
s32 rtw_hal_set_default_port_id_cmd(_adapter *adapter, u8 mac_id)
{
	s32 ret = _SUCCESS;
	u8 parm[H2C_DEFAULT_PORT_ID_LEN] = {0};
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	u8 port_id = rtw_hal_get_port(adapter);

	if ((dvobj->dft.port_id == port_id) && (dvobj->dft.mac_id == mac_id))
		return ret;

	SET_H2CCMD_DFTPID_PORT_ID(parm, port_id);
	SET_H2CCMD_DFTPID_MAC_ID(parm, mac_id);

	RTW_DBG_DUMP("DFT port id parm:", parm, H2C_DEFAULT_PORT_ID_LEN);
	RTW_INFO("%s ("ADPT_FMT") port_id :%d, mad_id:%d\n",
		__func__, ADPT_ARG(adapter), port_id, mac_id);

	ret = rtw_hal_fill_h2c_cmd(adapter, H2C_DEFAULT_PORT_ID, H2C_DEFAULT_PORT_ID_LEN, parm);
	dvobj->dft.port_id = port_id;
	dvobj->dft.mac_id = mac_id;

	return ret;
}
s32 rtw_set_default_port_id(_adapter *adapter)
{
	s32 ret = _SUCCESS;
	struct sta_info		*psta;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	if (is_client_associated_to_ap(adapter)) {
		psta = rtw_get_stainfo(&adapter->stapriv, get_bssid(pmlmepriv));
		if (psta)
			ret = rtw_hal_set_default_port_id_cmd(adapter, psta->cmn.mac_id);
	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) {

	} else {
	}

	return ret;
}
s32 rtw_set_ps_rsvd_page(_adapter *adapter)
{
	s32 ret = _SUCCESS;
	u16 media_status_rpt = RT_MEDIA_CONNECT;
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);

	if (adapter->iface_id == pwrctl->fw_psmode_iface_id)
		return ret;

	rtw_hal_set_hwreg(adapter, HW_VAR_H2C_FW_JOINBSSRPT,
			  (u8 *)&media_status_rpt);

	return ret;
}

#if 0
_adapter * _rtw_search_dp_iface(_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	_adapter *iface;
	_adapter *target_iface = NULL;
	int i;
	u8 sta_num = 0, tdls_num = 0, ap_num = 0, mesh_num = 0, adhoc_num = 0;
	u8 p2p_go_num = 0, p2p_gc_num = 0;
	_adapter *sta_ifs[8];
	_adapter *ap_ifs[8];
	_adapter *mesh_ifs[8];
	_adapter *gc_ifs[8];
	_adapter *go_ifs[8];

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];

		if (check_fwstate(&iface->mlmepriv, WIFI_STATION_STATE) == _TRUE) {
			if (check_fwstate(&iface->mlmepriv, _FW_LINKED) == _TRUE) {
				sta_ifs[sta_num++] = iface;

				#ifdef CONFIG_TDLS
				if (iface->tdlsinfo.link_established == _TRUE)
					tdls_num++;
				#endif
				#ifdef CONFIG_P2P
				if (MLME_IS_GC(iface))
					gc_ifs[p2p_gc_num++] = iface;
				#endif
			}
#ifdef CONFIG_AP_MODE
		} else if (check_fwstate(&iface->mlmepriv, WIFI_AP_STATE) == _TRUE ) {
			if (check_fwstate(&iface->mlmepriv, _FW_LINKED) == _TRUE) {
				ap_ifs[ap_num++] = iface;
				#ifdef CONFIG_P2P
				if (MLME_IS_GO(iface))
					go_ifs[p2p_go_num++] = iface;
				#endif
			}
#endif
		} else if (check_fwstate(&iface->mlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE) == _TRUE
			&& check_fwstate(&iface->mlmepriv, _FW_LINKED) == _TRUE
		) {
			adhoc_num++;

#ifdef CONFIG_RTW_MESH
		} else if (check_fwstate(&iface->mlmepriv, WIFI_MESH_STATE) == _TRUE
			&& check_fwstate(&iface->mlmepriv, _FW_LINKED) == _TRUE
		) {
			mesh_ifs[mesh_num++] = iface;
#endif
		}
	}

	if (p2p_gc_num) {
		target_iface = gc_ifs[0];
	}
	else if (sta_num) {
		if(sta_num == 1) {
			target_iface = sta_ifs[0];
		} else if (sta_num >= 2) {
			/*TODO get target_iface by timestamp*/
			target_iface = sta_ifs[0];
		}
	} else if (ap_num) {
		target_iface = ap_ifs[0];
	}

	RTW_INFO("[IFS_ASSOC_STATUS] - STA :%d", sta_num);
	RTW_INFO("[IFS_ASSOC_STATUS] - TDLS :%d", tdls_num);
	RTW_INFO("[IFS_ASSOC_STATUS] - AP:%d", ap_num);
	RTW_INFO("[IFS_ASSOC_STATUS] - MESH :%d", mesh_num);
	RTW_INFO("[IFS_ASSOC_STATUS] - ADHOC :%d", adhoc_num);
	RTW_INFO("[IFS_ASSOC_STATUS] - P2P-GC :%d", p2p_gc_num);
	RTW_INFO("[IFS_ASSOC_STATUS] - P2P-GO :%d", p2p_go_num);

	if (target_iface)
		RTW_INFO("%s => target_iface ("ADPT_FMT")\n",
			__func__, ADPT_ARG(target_iface));
	else
		RTW_INFO("%s => target_iface NULL\n", __func__);

	return target_iface;
}

void rtw_search_default_port(_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	_adapter *adp_iface = NULL;
#ifdef CONFIG_WOWLAN
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(dvobj);

	if (pwrpriv->wowlan_mode == _TRUE) {
		adp_iface = adapter;
		goto exit;
	}
#endif
	adp_iface = _rtw_search_dp_iface(adapter);

exit :
	if ((adp_iface != NULL) && (MLME_IS_STA(adp_iface)))
		rtw_set_default_port_id(adp_iface);
	else
		rtw_hal_set_default_port_id_cmd(adapter, 0);

	if (1) {
		_adapter *tmp_adp;

		tmp_adp = (adp_iface) ? adp_iface : adapter;

		RTW_INFO("%s ("ADPT_FMT")=> hw_port :%d, default_port(%d)\n",
			__func__, ADPT_ARG(adapter), get_hw_port(tmp_adp), get_dft_portid(tmp_adp));
	}
}
#endif
#endif /*CONFIG_FW_MULTI_PORT_SUPPORT*/

#ifdef CONFIG_P2P_PS
#ifdef RTW_HALMAC
void rtw_set_p2p_ps_offload_cmd(_adapter *adapter, u8 p2p_ps_state)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	struct wifidirect_info *pwdinfo = &adapter->wdinfo;
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*cur_network = &(pmlmeinfo->network);
	struct sta_priv		*pstapriv = &adapter->stapriv;
	struct sta_info		*psta;
	HAL_P2P_PS_PARA p2p_ps_para;
	int status = -1;
	u8 i;
	u8 hw_port = rtw_hal_get_port(adapter);

	_rtw_memset(&p2p_ps_para, 0, sizeof(HAL_P2P_PS_PARA));
	_rtw_memcpy((&p2p_ps_para) , &hal->p2p_ps_offload , sizeof(hal->p2p_ps_offload));

	(&p2p_ps_para)->p2p_port_id = hw_port;
	(&p2p_ps_para)->p2p_group = 0;
	psta = rtw_get_stainfo(pstapriv, cur_network->MacAddress);
	if (psta) {
		(&p2p_ps_para)->p2p_macid = psta->cmn.mac_id;
	} else {
		if (p2p_ps_state != P2P_PS_DISABLE) {
			RTW_ERR("%s , psta was NULL\n", __func__);
			return;
		}
	}


	switch (p2p_ps_state) {
	case P2P_PS_DISABLE:
		RTW_INFO("P2P_PS_DISABLE\n");
		_rtw_memset(&p2p_ps_para , 0, sizeof(HAL_P2P_PS_PARA));
		break;

	case P2P_PS_ENABLE:
		RTW_INFO("P2P_PS_ENABLE\n");
		/* update CTWindow value. */
		if (pwdinfo->ctwindow > 0) {
			(&p2p_ps_para)->ctwindow_en = 1;
			(&p2p_ps_para)->ctwindow_length = pwdinfo->ctwindow;
			/*RTW_INFO("%s , ctwindow_length = %d\n" , __func__ , (&p2p_ps_para)->ctwindow_length);*/
		}


		if ((pwdinfo->opp_ps == 1) || (pwdinfo->noa_num > 0)) {
			(&p2p_ps_para)->offload_en = 1;
			if (pwdinfo->role == P2P_ROLE_GO) {
				(&p2p_ps_para)->role = 1;
				(&p2p_ps_para)->all_sta_sleep = 0;
			} else
				(&p2p_ps_para)->role = 0;

			(&p2p_ps_para)->discovery = 0;
		}
		/* hw only support 2 set of NoA */
		for (i = 0; i < pwdinfo->noa_num; i++) {
			/* To control the register setting for which NOA */
			(&p2p_ps_para)->noa_sel = i;
			(&p2p_ps_para)->noa_en = 1;
			(&p2p_ps_para)->disable_close_rf = 0;
#ifdef CONFIG_P2P_PS_NOA_USE_MACID_SLEEP
#ifdef CONFIG_CONCURRENT_MODE
			if (rtw_mi_buddy_check_fwstate(adapter, WIFI_ASOC_STATE))
#endif /* CONFIG_CONCURRENT_MODE */
				(&p2p_ps_para)->disable_close_rf = 1;
#endif /* CONFIG_P2P_PS_NOA_USE_MACID_SLEEP */
			/* config P2P NoA Descriptor Register */
			/* config NOA duration */
			(&p2p_ps_para)->noa_duration_para = pwdinfo->noa_duration[i];
			/* config NOA interval */
			(&p2p_ps_para)->noa_interval_para = pwdinfo->noa_interval[i];
			/* config NOA start time */
			(&p2p_ps_para)->noa_start_time_para = pwdinfo->noa_start_time[i];
			/* config NOA count */
			(&p2p_ps_para)->noa_count_para = pwdinfo->noa_count[i];
			/*RTW_INFO("%s , noa_duration_para = %d , noa_interval_para = %d , noa_start_time_para = %d , noa_count_para = %d\n" , __func__ ,
				(&p2p_ps_para)->noa_duration_para , (&p2p_ps_para)->noa_interval_para ,
				(&p2p_ps_para)->noa_start_time_para , (&p2p_ps_para)->noa_count_para);*/
			status = rtw_halmac_p2pps(adapter_to_dvobj(adapter) , (&p2p_ps_para));
			if (status == -1)
				RTW_ERR("%s , rtw_halmac_p2pps fail\n", __func__);
		}

		break;

	case P2P_PS_SCAN:
		/*This feature FW not ready 20161116 YiWei*/
		return;
		/*
		RTW_INFO("P2P_PS_SCAN\n");
		(&p2p_ps_para)->discovery = 1;
		(&p2p_ps_para)->ctwindow_length = pwdinfo->ctwindow;
		(&p2p_ps_para)->noa_duration_para = pwdinfo->noa_duration[0];
		(&p2p_ps_para)->noa_interval_para = pwdinfo->noa_interval[0];
		(&p2p_ps_para)->noa_start_time_para = pwdinfo->noa_start_time[0];
		(&p2p_ps_para)->noa_count_para = pwdinfo->noa_count[0];
		*/
		break;

	case P2P_PS_SCAN_DONE:
		/*This feature FW not ready 20161116 YiWei*/
		return;
		/*
		RTW_INFO("P2P_PS_SCAN_DONE\n");
		(&p2p_ps_para)->discovery = 0;
		pwdinfo->p2p_ps_state = P2P_PS_ENABLE;
		(&p2p_ps_para)->ctwindow_length = pwdinfo->ctwindow;
		(&p2p_ps_para)->noa_duration_para = pwdinfo->noa_duration[0];
		(&p2p_ps_para)->noa_interval_para = pwdinfo->noa_interval[0];
		(&p2p_ps_para)->noa_start_time_para = pwdinfo->noa_start_time[0];
		(&p2p_ps_para)->noa_count_para = pwdinfo->noa_count[0];
		*/
		break;

	default:
		break;
	}

	if (p2p_ps_state != P2P_PS_ENABLE || (&p2p_ps_para)->noa_en == 0) {
		status = rtw_halmac_p2pps(adapter_to_dvobj(adapter) , (&p2p_ps_para));
		if (status == -1)
			RTW_ERR("%s , rtw_halmac_p2pps fail\n", __func__);
	}
	_rtw_memcpy(&hal->p2p_ps_offload , (&p2p_ps_para) , sizeof(hal->p2p_ps_offload));

}
#endif /* RTW_HALMAC */
#endif /* CONFIG_P2P */

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
#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
	u8 hw_port = rtw_hal_get_port(adapter);
#endif
	u8 op_num_change_bmp = 0;

	SET_H2CCMD_MSRRPT_PARM_OPMODE(parm, opmode);
	SET_H2CCMD_MSRRPT_PARM_MACID_IND(parm, macid_ind);
	SET_H2CCMD_MSRRPT_PARM_MIRACAST(parm, miracast);
	SET_H2CCMD_MSRRPT_PARM_MIRACAST_SINK(parm, miracast_sink);
	SET_H2CCMD_MSRRPT_PARM_ROLE(parm, role);
	SET_H2CCMD_MSRRPT_PARM_MACID(parm, macid);
	SET_H2CCMD_MSRRPT_PARM_MACID_END(parm, macid_end);
#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
	SET_H2CCMD_MSRRPT_PARM_PORT_NUM(parm, hw_port);
#endif
	RTW_DBG_DUMP("MediaStatusRpt parm:", parm, H2C_MEDIA_STATUS_RPT_LEN);

	ret = rtw_hal_fill_h2c_cmd(adapter, H2C_MEDIA_STATUS_RPT, H2C_MEDIA_STATUS_RPT_LEN, parm);
	if (ret != _SUCCESS)
		goto exit;

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
		op_num_change_bmp |= rtw_macid_ctl_set_h2c_msr(macid_ctl, macid, parm[0]);
		if (!opmode) {
			rtw_macid_ctl_set_bw(macid_ctl, macid, CHANNEL_WIDTH_20);
			rtw_macid_ctl_set_vht_en(macid_ctl, macid, 0);
			rtw_macid_ctl_set_rate_bmp0(macid_ctl, macid, 0);
			rtw_macid_ctl_set_rate_bmp1(macid_ctl, macid, 0);
		}
	}

#if CONFIG_TX_AC_LIFETIME
	if (op_num_change_bmp)
		rtw_hal_update_tx_aclt(adapter);
#endif

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

	if (IS_8723D_SERIES(pHalData->version_id) || IS_8192F_SERIES(pHalData->version_id)
		|| IS_8822B_SERIES(pHalData->version_id) || IS_8821C_SERIES(pHalData->version_id)
		|| IS_8822C_SERIES(pHalData->version_id))
		rtw_hal_set_hwreg(padapter, HW_SET_GPIO_WL_CTRL, (u8 *)(&enable));
	/*
	* Switch GPIO_13, GPIO_14 to wlan control, or pull GPIO_13,14 MUST fail.
	* It happended at 8723B/8192E/8821A. New IC will check multi function GPIO,
	* and implement HAL function.
	* TODO: GPIO_8 multi function?
	*/

	if ((index == 13 || index == 14)
		#if defined(CONFIG_RTL8821A) && defined(CONFIG_SDIO_HCI)
		/* 8821A's LED2 circuit(used by HW_LED strategy) needs enable WL GPIO control of GPIO[14:13], can't disable */
		&& (!IS_HW_LED_STRATEGY(rtw_led_get_strategy(padapter)) || enable)
		#endif
	)
		rtw_hal_set_hwreg(padapter, HW_SET_GPIO_WL_CTRL, (u8 *)(&enable));
}

void rtw_hal_set_output_gpio(_adapter *padapter, u8 index, u8 outputval)
{
#if defined(CONFIG_RTL8192F)
	rtw_hal_set_hwreg(padapter, HW_VAR_WOW_OUTPUT_GPIO, (u8 *)(&index));
#else
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
#endif
}
void rtw_hal_set_input_gpio(_adapter *padapter, u8 index)
{
#if defined(CONFIG_RTL8192F)
	rtw_hal_set_hwreg(padapter, HW_VAR_WOW_INPUT_GPIO, (u8 *)(&index));
#else
	if (index <= 7) {
		/* config GPIO mode */
		rtw_write8(padapter, REG_GPIO_PIN_CTRL + 3,
			rtw_read8(padapter, REG_GPIO_PIN_CTRL + 3) & ~BIT(index));

		/* config GPIO Sel */
		/* 0: input */
		/* 1: output */
		rtw_write8(padapter, REG_GPIO_PIN_CTRL + 2,
			rtw_read8(padapter, REG_GPIO_PIN_CTRL + 2) & ~BIT(index));

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
			rtw_read8(padapter, REG_GPIO_PIN_CTRL_2 + 2) & ~BIT(index));
	} else
		RTW_INFO("%s: invalid GPIO%d\n", __func__, index);
#endif
}

#endif

void rtw_hal_set_FwAoacRsvdPage_cmd(PADAPTER padapter, PRSVDPAGE_LOC rsvdpageloc)
{
	struct	pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 ret = 0;
#ifdef CONFIG_WOWLAN
	u8 u1H2CAoacRsvdPageParm[H2C_AOAC_RSVDPAGE_LOC_LEN] = {0};

	RTW_INFO("%s: RWC: %d ArpRsp: %d NbrAdv: %d LocNDPInfo: %d\n",
		 __func__, rsvdpageloc->LocRemoteCtrlInfo,
		 rsvdpageloc->LocArpRsp, rsvdpageloc->LocNbrAdv,
		 rsvdpageloc->LocNDPInfo);
	RTW_INFO("%s:GtkRsp: %d GtkInfo: %d ProbeReq: %d NetworkList: %d\n",
		 __func__, rsvdpageloc->LocGTKRsp, rsvdpageloc->LocGTKInfo,
		 rsvdpageloc->LocProbeReq, rsvdpageloc->LocNetList);

	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_REMOTE_WAKE_CTRL_INFO(u1H2CAoacRsvdPageParm, rsvdpageloc->LocRemoteCtrlInfo);
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_ARP_RSP(u1H2CAoacRsvdPageParm, rsvdpageloc->LocArpRsp);
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_NEIGHBOR_ADV(u1H2CAoacRsvdPageParm,
							rsvdpageloc->LocNbrAdv);
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_NDP_INFO(u1H2CAoacRsvdPageParm,
						      rsvdpageloc->LocNDPInfo);
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

#ifdef DBG_FW_DEBUG_MSG_PKT
void rtw_hal_set_fw_dbg_msg_pkt_rsvd_page_cmd(PADAPTER padapter, PRSVDPAGE_LOC rsvdpageloc)
{
	struct	hal_ops *pHalFunc = &padapter->hal_func;
	u8	u1H2C_fw_dbg_msg_pkt_parm[H2C_FW_DBG_MSG_PKT_LEN] = {0};
	u8	ret = 0;


	RTW_INFO("RsvdPageLoc: loc_fw_dbg_msg_pkt =%d\n", rsvdpageloc->loc_fw_dbg_msg_pkt);

	SET_H2CCMD_FW_DBG_MSG_PKT_EN(u1H2C_fw_dbg_msg_pkt_parm, 1);
	SET_H2CCMD_RSVDPAGE_LOC_FW_DBG_MSG_PKT(u1H2C_fw_dbg_msg_pkt_parm, rsvdpageloc->loc_fw_dbg_msg_pkt);
	ret = rtw_hal_fill_h2c_cmd(padapter,
				   H2C_FW_DBG_MSG_PKT,
				   H2C_FW_DBG_MSG_PKT_LEN,
				   u1H2C_fw_dbg_msg_pkt_parm);

}
#endif /*DBG_FW_DEBUG_MSG_PKT*/

/*#define DBG_GET_RSVD_PAGE*/
int rtw_hal_get_rsvd_page(_adapter *adapter, u32 page_offset,
	u32 page_num, u8 *buffer, u32 buffer_size)
{
	u32 addr = 0, size = 0, count = 0;
	u32 page_size = 0, data_low = 0, data_high = 0;
	u16 txbndy = 0, offset = 0;
	u8 i = 0;
	bool rst = _FALSE;

#ifdef DBG_LA_MODE
	struct registry_priv *registry_par = &adapter->registrypriv;

	if(registry_par->la_mode_en == 1) {
		RTW_INFO("%s LA debug mode can't dump rsvd pg \n", __func__);
		return rst;
	}
#endif
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

	offset = (txbndy + page_offset) * page_size / 8;
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

	RTW_PRINT_SEL(sel, "======= RSVD PAGE DUMP =======\n");
	RTW_PRINT_SEL(sel, "page_offset:%d, page_num:%d\n", page_offset, page_num);

	rtw_hal_get_def_var(adapter, HAL_DEF_TX_PAGE_SIZE, &page_size);
	if (page_size) {
		buf_size = page_size * page_num;
		buffer = rtw_zvmalloc(buf_size);

		if (buffer) {
			rtw_hal_get_rsvd_page(adapter, page_offset, page_num, buffer, buf_size);
			RTW_DUMP_SEL(sel, buffer, buf_size);
			rtw_vmfree(buffer, buf_size);
		} else
			RTW_PRINT_SEL(sel, "ERROR - rsvd_buf mem allocate failed\n");
	} else
			RTW_PRINT_SEL(sel, "ERROR - Tx page size is zero ??\n");

	RTW_PRINT_SEL(sel, "==========================\n");
}

#ifdef CONFIG_SUPPORT_FIFO_DUMP
void rtw_dump_fifo(void *sel, _adapter *adapter, u8 fifo_sel, u32 fifo_addr, u32 fifo_size)
{
	u8 *buffer = NULL;
	u32 buff_size = 0;
	static const char * const fifo_sel_str[] = {
		"TX", "RX", "RSVD_PAGE", "REPORT", "LLT", "RXBUF_FW"
	};

	if (fifo_sel > 5) {
		RTW_ERR("fifo_sel:%d invalid\n", fifo_sel);
		return;
	}

	RTW_PRINT_SEL(sel, "========= FIFO DUMP =========\n");
	RTW_PRINT_SEL(sel, "%s FIFO DUMP [start_addr:0x%04x , size:%d]\n", fifo_sel_str[fifo_sel], fifo_addr, fifo_size);

	if (fifo_size) {
		buff_size = RND4(fifo_size);
		buffer = rtw_zvmalloc(buff_size);
		if (buffer == NULL)
			buff_size = 0;
	}

	rtw_halmac_dump_fifo(adapter_to_dvobj(adapter), fifo_sel, fifo_addr, buff_size, buffer);

	if (buffer) {
		RTW_DUMP_SEL(sel, buffer, fifo_size);
		rtw_vmfree(buffer, buff_size);
	}

	RTW_PRINT_SEL(sel, "==========================\n");
}
#endif

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
		 __func__, REG_RXPKT_NUM, (u32)(val32 & (~RW_RELEASE_EN)));
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
		tmp = rtw_read16(adapter, REG_RXPKT_NUM + 2);

		RTW_PRINT("Stop RX DMA failed......\n");
#if defined(CONFIG_RTL8822C) || defined(CONFIG_RTL8814B)
		RTW_PRINT("%s, RXPKT_NUM: 0x%04x\n",
			  __func__, rtw_read16(adapter, REG_RXPKTNUM));
#else
		RTW_PRINT("%s, RXPKT_NUM: 0x%02x\n",
			  __func__, ((tmp & 0xFF00) >> 8));
#endif
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
 * chk_type: _TRUE means to check enable, if 0x690 & bit1 (for 8051), WOW enable successful.
 *									If 0x1C7 == 0 (for 3081), WOW enable successful.
 *		     _FALSE means to check disable, if 0x690 & bit1 (for 8051), WOW disable fail.
 *									If 0x120 & bit16 || 0x284 & bit18 (for 3081), WOW disable fail.
 */
static u8 rtw_hal_check_wow_ctrl(_adapter *adapter, u8 chk_type)
{
	u32 fe1_imr = 0xFF, rxpkt_num = 0xFF;
	u8 mstatus = 0;
	u8 reason = 0xFF;
	u8 trycnt = 25;
	u8 res = _FALSE;

	if (IS_HARDWARE_TYPE_JAGUAR2(adapter) || IS_HARDWARE_TYPE_JAGUAR3(adapter)) {
		if (chk_type) {
			reason = rtw_read8(adapter, REG_WOWLAN_WAKE_REASON);
			RTW_INFO("%s reason:0x%02x\n", __func__, reason);

			while (reason && trycnt > 1) {
				reason = rtw_read8(adapter, REG_WOWLAN_WAKE_REASON);
				RTW_PRINT("Loop index: %d :0x%02x\n",
					  trycnt, reason);
				trycnt--;
				rtw_msleep_os(20);
			}
			if (!reason)
				res = _TRUE;
			else
				res = _FALSE;
		} else {
			/* Wait FW to cleare 0x120 bit16, 0x284 bit18 to 0 */
			fe1_imr = rtw_read32(adapter, REG_FE1IMR); /* RxDone IMR for 3081 */
			rxpkt_num = rtw_read32(adapter, REG_RXPKT_NUM); /* Release RXDMA */
			RTW_PRINT("%s REG_FE1IMR (reg120): 0x%x, REG_RXPKT_NUM(reg284): 0x%x\n", __func__, fe1_imr, rxpkt_num);

			while (((fe1_imr & BIT_FS_RXDONE_INT_EN) || (rxpkt_num & BIT_RW_RELEASE_EN)) && trycnt > 1) {
				rtw_msleep_os(20);
				fe1_imr = rtw_read32(adapter, REG_FE1IMR);
				rxpkt_num = rtw_read32(adapter, REG_RXPKT_NUM);
				RTW_PRINT("Loop index: %d :0x%x, 0x%x\n",
					  trycnt, fe1_imr, rxpkt_num);
				trycnt--;
			}

			if ((fe1_imr & BIT_FS_RXDONE_INT_EN) || (rxpkt_num & BIT_RW_RELEASE_EN))
				res = _FALSE;
			else
				res = _TRUE;
		}
	} else {
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

	if (!MLME_IS_STA(adapter))
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
	u8 gtk_id = 0, offset = 0, i = 0, sz = 0, aoac_rpt_ver = 0, has_rekey = _FALSE;
	u64 replay_count = 0, tmp_iv_hdr = 0, pkt_pn = 0;

	if (!MLME_IS_STA(adapter))
		return;

	_rtw_memset(get_key, 0, sizeof(get_key));
	_rtw_memcpy(&replay_count,
		paoac_rpt->replay_counter_eapol_key, 8);

	/*read gtk key index*/
	gtk_id = paoac_rpt->key_index;
	aoac_rpt_ver = paoac_rpt->version_info;

	if (aoac_rpt_ver == 0) {
		/* initial verison */
		if (gtk_id == 5)
			has_rekey = _FALSE;
		else
			has_rekey = _TRUE;
	} else if (aoac_rpt_ver >= 1) {
		/* Add krack patch */
		if (gtk_id == 5)
			RTW_WARN("%s FW check iv fail\n", __func__);

		if (aoac_rpt_ver == 1)
			RTW_WARN("%s aoac report version should be update to v2\n", __func__);

		/* Fix key id mismatch */
		if (aoac_rpt_ver == 2)
			has_rekey = paoac_rpt->rekey_ok == 1 ? _TRUE : _FALSE;
	}

	if (has_rekey == _FALSE) {
		RTW_INFO("%s no rekey event happened.\n", __func__);
	} else if (has_rekey == _TRUE) {
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
		RTW_PRINT("GTK (%d) "KEY_FMT"\n", gtk_id,
			KEY_ARG(psecuritypriv->dot118021XGrpKey[gtk_id].skey));
	}

	/* Update broadcast RX IV */
	if (psecuritypriv->dot118021XGrpPrivacy == _AES_) {
		sz = sizeof(psecuritypriv->iv_seq[0]);
		for (i = 0 ; i < 4 ; i++) {
			_rtw_memcpy(&tmp_iv_hdr, paoac_rpt->rxgtk_iv[i], sz);
			tmp_iv_hdr = le64_to_cpu(tmp_iv_hdr);
			pkt_pn = CCMPH_2_PN(tmp_iv_hdr);
			_rtw_memcpy(psecuritypriv->iv_seq[i], &pkt_pn, sz);
		}
	}

	rtw_clean_dk_section(adapter);

	rtw_write8(adapter, REG_SECCFG, 0x0c);

	#ifdef CONFIG_GTK_OL_DBG
	/* if (gtk_keyindex != 5) */
	dump_sec_cam(RTW_DBGDUMP, adapter);
	dump_sec_cam_cache(RTW_DBGDUMP, adapter);
	#endif
}
#endif /*CONFIG_GTK_OL*/

static void rtw_dump_aoac_rpt(_adapter *adapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	struct aoac_report *paoac_rpt = &pwrctl->wowlan_aoac_rpt;
	int i = 0;

	RTW_INFO_DUMP("[AOAC-RPT] IV -", paoac_rpt->iv, 8);
	RTW_INFO_DUMP("[AOAC-RPT] Replay counter of EAPOL key - ",
		paoac_rpt->replay_counter_eapol_key, 8);
	RTW_INFO_DUMP("[AOAC-RPT] Group key - ", paoac_rpt->group_key, 32);
	RTW_INFO("[AOAC-RPT] Key Index - %d\n", paoac_rpt->key_index);
	RTW_INFO("[AOAC-RPT] Security Type - %d\n", paoac_rpt->security_type);
	RTW_INFO("[AOAC-RPT] wow_pattern_idx - %d\n",
		 paoac_rpt->wow_pattern_idx);
	RTW_INFO("[AOAC-RPT] version_info - %d\n", paoac_rpt->version_info);
	RTW_INFO("[AOAC-RPT] rekey_ok - %d\n", paoac_rpt->rekey_ok);
	RTW_INFO_DUMP("[AOAC-RPT] RX PTK IV-", paoac_rpt->rxptk_iv, 8);
	RTW_INFO_DUMP("[AOAC-RPT] RX GTK[0] IV-", paoac_rpt->rxgtk_iv[0], 8);
	RTW_INFO_DUMP("[AOAC-RPT] RX GTK[1] IV-", paoac_rpt->rxgtk_iv[1], 8);
	RTW_INFO_DUMP("[AOAC-RPT] RX GTK[2] IV-", paoac_rpt->rxgtk_iv[2], 8);
	RTW_INFO_DUMP("[AOAC-RPT] RX GTK[3] IV-", paoac_rpt->rxgtk_iv[3], 8);
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

	if (buffer == NULL) {
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

	rtw_dump_aoac_rpt(adapter);

_exit:
	if (buffer)
		rtw_vmfree(buffer, buf_size);
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
	struct security_priv *psecpriv = &adapter->securitypriv;
	u8 sz = sizeof (psecpriv->iv_seq);

	rtw_hal_update_tx_iv(adapter);
#ifdef CONFIG_GTK_OL
	if (psecpriv->binstallKCK_KEK == _TRUE &&
	    psecpriv->ndisauthtype == Ndis802_11AuthModeWPA2PSK)
		rtw_hal_update_gtk_offload_info(adapter);
#else
	_rtw_memset(psecpriv->iv_seq, 0, sz);
#endif
}

static u8 rtw_hal_set_keep_alive_cmd(_adapter *adapter, u8 enable, u8 pkt_type)
{
	u8 u1H2CKeepAliveParm[H2C_KEEP_ALIVE_CTRL_LEN] = {0};
	u8 adopt = 1, check_period = 5;
	u8 ret = _FAIL;
	u8 hw_port = rtw_hal_get_port(adapter);

	SET_H2CCMD_KEEPALIVE_PARM_ENABLE(u1H2CKeepAliveParm, enable);
	SET_H2CCMD_KEEPALIVE_PARM_ADOPT(u1H2CKeepAliveParm, adopt);
	SET_H2CCMD_KEEPALIVE_PARM_PKT_TYPE(u1H2CKeepAliveParm, pkt_type);
	SET_H2CCMD_KEEPALIVE_PARM_CHECK_PERIOD(u1H2CKeepAliveParm, check_period);
#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
	SET_H2CCMD_KEEPALIVE_PARM_PORT_NUM(u1H2CKeepAliveParm, hw_port);
	RTW_INFO("%s(): enable = %d, port = %d\n", __func__, enable, hw_port);
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
	u8 u1H2CDisconDecisionParm[H2C_DISCON_DECISION_LEN] = {0};
	u8 adopt = 1, check_period = 30, trypkt_num = 5;
	u8 ret = _FAIL;
	u8 hw_port = rtw_hal_get_port(adapter);

	SET_H2CCMD_DISCONDECISION_PARM_ENABLE(u1H2CDisconDecisionParm, enable);
	SET_H2CCMD_DISCONDECISION_PARM_ADOPT(u1H2CDisconDecisionParm, adopt);
	/* SET_H2CCMD_DISCONDECISION_PARM_DISCONNECT_EN(u1H2CDisconDecisionParm, adopt); */
	SET_H2CCMD_DISCONDECISION_PARM_CHECK_PERIOD(u1H2CDisconDecisionParm, check_period);
	SET_H2CCMD_DISCONDECISION_PARM_TRY_PKT_NUM(u1H2CDisconDecisionParm, trypkt_num);
#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
	SET_H2CCMD_DISCONDECISION_PORT_NUM(u1H2CDisconDecisionParm, hw_port);
	RTW_INFO("%s(): enable = %d, port = %d\n", __func__, enable, hw_port);
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
	struct registry_priv  *registry_par = &adapter->registrypriv;
	struct security_priv *psecpriv = &adapter->securitypriv;
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(adapter);
	struct mlme_priv	*pmlmepriv = &(adapter->mlmepriv);

	u8 u1H2CWoWlanCtrlParm[H2C_WOWLAN_LEN] = {0};
	u8 discont_wake = 0, gpionum = 0, gpio_dur = 0, no_wake = 0;
	u8 hw_unicast = 0, gpio_pulse_cnt = 0, gpio_pulse_en = 0;
	u8 sdio_wakeup_enable = 1;
	u8 gpio_high_active = 0;
	u8 magic_pkt = 0;
	u8 gpio_unit = 0; /*0: 64ns, 1: 8ms*/
	u8 ret = _FAIL;
#ifdef CONFIG_DIS_UPHY
	u8 dis_uphy = 0, dis_uphy_unit = 0, dis_uphy_time = 0;
#endif /* CONFIG_DIS_UPHY */

#ifdef CONFIG_GPIO_WAKEUP
	gpio_high_active = ppwrpriv->is_high_active;
	gpionum = WAKEUP_GPIO_IDX;
	sdio_wakeup_enable = 0;
#endif /* CONFIG_GPIO_WAKEUP */
	
	if(registry_par->suspend_type == FW_IPS_DISABLE_BBRF &&
	!check_fwstate(pmlmepriv, _FW_LINKED))
		no_wake = 1;
		
	if (!ppwrpriv->wowlan_pno_enable &&
		registry_par->wakeup_event & BIT(0) && !no_wake)
		magic_pkt = enable;

	if ((registry_par->wakeup_event & BIT(1)) &&
		(psecpriv->dot11PrivacyAlgrthm == _WEP40_ ||
		psecpriv->dot11PrivacyAlgrthm == _WEP104_) && !no_wake)
			hw_unicast = 1;

	if (registry_par->wakeup_event & BIT(2) && !no_wake)
		discont_wake = enable;

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
	if(!no_wake)
		SET_H2CCMD_WOWLAN_PATTERN_MATCH_ENABLE(u1H2CWoWlanCtrlParm, enable);
	SET_H2CCMD_WOWLAN_MAGIC_PKT_ENABLE(u1H2CWoWlanCtrlParm, magic_pkt);
	SET_H2CCMD_WOWLAN_UNICAST_PKT_ENABLE(u1H2CWoWlanCtrlParm, hw_unicast);
	SET_H2CCMD_WOWLAN_ALL_PKT_DROP(u1H2CWoWlanCtrlParm, 0);
	SET_H2CCMD_WOWLAN_GPIO_ACTIVE(u1H2CWoWlanCtrlParm, gpio_high_active);

#ifdef CONFIG_GTK_OL
	if (psecpriv->binstallKCK_KEK == _TRUE &&
	    psecpriv->ndisauthtype == Ndis802_11AuthModeWPA2PSK)
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

#ifdef CONFIG_WAKEUP_GPIO_INPUT_MODE
	if (enable)
		SET_H2CCMD_WOWLAN_GPIO_INPUT_EN(u1H2CWoWlanCtrlParm, 1);
#endif

#ifdef CONFIG_DIS_UPHY
	if (enable) {
		dis_uphy = 1;
		/* time unit: 0 -> ms, 1 -> 256 ms*/
		dis_uphy_unit = 1;
		dis_uphy_time = 0x4;
	}

	SET_H2CCMD_WOWLAN_DISABLE_UPHY(u1H2CWoWlanCtrlParm, dis_uphy);
	SET_H2CCMD_WOWLAN_UNIT_FOR_UPHY_DISABLE(u1H2CWoWlanCtrlParm, dis_uphy_unit);
	SET_H2CCMD_WOWLAN_TIME_FOR_UPHY_DISABLE(u1H2CWoWlanCtrlParm, dis_uphy_time);
	if (ppwrpriv->hst2dev_high_active == 1)
		SET_H2CCMD_WOWLAN_RISE_HST2DEV(u1H2CWoWlanCtrlParm, 1);
#ifdef CONFIG_RTW_ONE_PIN_GPIO
	SET_H2CCMD_WOWLAN_GPIO_INPUT_EN(u1H2CWoWlanCtrlParm, 1);
	SET_H2CCMD_WOWLAN_DEV2HST_EN(u1H2CWoWlanCtrlParm, 1);
	SET_H2CCMD_WOWLAN_HST2DEV_EN(u1H2CWoWlanCtrlParm, 0);
#else
	SET_H2CCMD_WOWLAN_HST2DEV_EN(u1H2CWoWlanCtrlParm, 1);
#endif /* CONFIG_RTW_ONE_PIN_GPIO */
#endif /* CONFIG_DIS_UPHY */


	ret = rtw_hal_fill_h2c_cmd(adapter,
				   H2C_WOWLAN,
				   H2C_WOWLAN_LEN,
				   u1H2CWoWlanCtrlParm);
	return ret;
}

static u8 rtw_hal_set_remote_wake_ctrl_cmd(_adapter *adapter, u8 enable)
{
	struct security_priv *psecuritypriv = &(adapter->securitypriv);
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(adapter);
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	u8 u1H2CRemoteWakeCtrlParm[H2C_REMOTE_WAKE_CTRL_LEN] = {0};
	u8 ret = _FAIL, count = 0, no_wake = 0;
	struct mlme_priv	*pmlmepriv = &(adapter->mlmepriv);

	RTW_INFO("%s(): enable=%d\n", __func__, enable);

	if(pregistrypriv->suspend_type == FW_IPS_DISABLE_BBRF &&
	!check_fwstate(pmlmepriv, _FW_LINKED))
		no_wake = 1;
	if(no_wake) {
		SET_H2CCMD_REMOTE_WAKECTRL_ENABLE(
			u1H2CRemoteWakeCtrlParm, enable);
	} else {
		if (!ppwrpriv->wowlan_pno_enable) {
			SET_H2CCMD_REMOTE_WAKECTRL_ENABLE(
				u1H2CRemoteWakeCtrlParm, enable);
			SET_H2CCMD_REMOTE_WAKE_CTRL_ARP_OFFLOAD_EN(
				u1H2CRemoteWakeCtrlParm, 1);
	#ifdef CONFIG_GTK_OL
			if (psecuritypriv->binstallKCK_KEK == _TRUE &&
			    psecuritypriv->ndisauthtype == Ndis802_11AuthModeWPA2PSK) {
				SET_H2CCMD_REMOTE_WAKE_CTRL_GTK_OFFLOAD_EN(
					u1H2CRemoteWakeCtrlParm, 1);
			} else {
				RTW_INFO("no kck kek\n");
				SET_H2CCMD_REMOTE_WAKE_CTRL_GTK_OFFLOAD_EN(
					u1H2CRemoteWakeCtrlParm, 0);
			}
	#endif /* CONFIG_GTK_OL */
	
	#ifdef CONFIG_IPV6
			if (ppwrpriv->wowlan_ns_offload_en == _TRUE) {
				RTW_INFO("enable NS offload\n");
				SET_H2CCMD_REMOTE_WAKE_CTRL_NDP_OFFLOAD_EN(
					u1H2CRemoteWakeCtrlParm, enable);
			}
	
			/*
			 * filter NetBios name service pkt to avoid being waked-up
			 * by this kind of unicast pkt this exceptional modification
			 * is used for match competitor's behavior
			 */
	
			SET_H2CCMD_REMOTE_WAKE_CTRL_NBNS_FILTER_EN(
				u1H2CRemoteWakeCtrlParm, enable);
	#endif /*CONFIG_IPV6*/
#if 0 /* replaced by WOWLAN pattern match */
	#ifdef CONFIG_RTL8192F
			if (IS_HARDWARE_TYPE_8192F(adapter)){
				SET_H2CCMD_REMOTE_WAKE_CTRL_FW_UNICAST_EN(
					u1H2CRemoteWakeCtrlParm, enable);
			}
	#endif /* CONFIG_RTL8192F */
#endif
			if ((psecuritypriv->dot11PrivacyAlgrthm == _AES_) ||
				(psecuritypriv->dot11PrivacyAlgrthm == _TKIP_) ||
				(psecuritypriv->dot11PrivacyAlgrthm == _NO_PRIVACY_)) {
				SET_H2CCMD_REMOTE_WAKE_CTRL_ARP_ACTION(
					u1H2CRemoteWakeCtrlParm, 0);
			} else {
				SET_H2CCMD_REMOTE_WAKE_CTRL_ARP_ACTION(
					u1H2CRemoteWakeCtrlParm, 1);
			}
	
			if (psecuritypriv->dot11PrivacyAlgrthm == _TKIP_ &&
			    psecuritypriv->ndisauthtype == Ndis802_11AuthModeWPA2PSK) {
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
	}


	ret = rtw_hal_fill_h2c_cmd(adapter,
				   H2C_REMOTE_WAKE_CTRL,
				   H2C_REMOTE_WAKE_CTRL_LEN,
				   u1H2CRemoteWakeCtrlParm);
	return ret;
}

static u8 rtw_hal_set_global_info_cmd(_adapter *adapter, u8 group_alg, u8 pairwise_alg)
{
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
	struct registry_priv *pregistry = &padapter->registrypriv;
	u8	pkt_type = 0, no_wake = 0;
	
	if(pregistry->suspend_type == FW_IPS_DISABLE_BBRF &&
	!check_fwstate(pmlmepriv, _FW_LINKED))
		no_wake = 1;

	RTW_PRINT("+%s()+: enable=%d\n", __func__, enable);

	rtw_hal_set_wowlan_ctrl_cmd(padapter, enable, _FALSE);

	if (enable) {
		if(!no_wake)
			rtw_hal_set_global_info_cmd(padapter,
					    psecpriv->dot118021XGrpPrivacy,
					    psecpriv->dot11PrivacyAlgrthm);

		if (!(ppwrpriv->wowlan_pno_enable)) {
			if (pregistry->wakeup_event & BIT(2) && !no_wake)
				rtw_hal_set_disconnect_decision_cmd(padapter,
								    enable);
#ifdef CONFIG_ARP_KEEP_ALIVE
			if ((psecpriv->dot11PrivacyAlgrthm == _WEP40_) ||
			    (psecpriv->dot11PrivacyAlgrthm == _WEP104_))
				pkt_type = 0;
			else
				pkt_type = 1;
#else
			pkt_type = 0;
#endif /* CONFIG_ARP_KEEP_ALIVE */
			if(!no_wake)
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
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(adapter);

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
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
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
#endif
#if defined(CONFIG_USB_HCI) || defined(CONFIG_PCI_HCI)
	/* Invoid SE0 reset signal during suspending*/
	rtw_write8(padapter, REG_RSV_CTRL, 0x20);
	if (IS_8188F(pHalData->version_id) == FALSE
		&& IS_8188GTV(pHalData->version_id) == FALSE)
		rtw_write8(padapter, REG_RSV_CTRL, 0x60);
#endif
}

static void rtw_hal_ap_wow_disable(_adapter *padapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
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
#ifdef CONFIG_RTW_ONE_PIN_GPIO
	rtw_hal_set_input_gpio(padapter, WAKEUP_GPIO_IDX);
#else
	#ifdef CONFIG_WAKEUP_GPIO_INPUT_MODE
	if (pwrctl->is_high_active == 0)
		rtw_hal_set_input_gpio(padapter, WAKEUP_GPIO_IDX);
	else
		rtw_hal_set_output_gpio(padapter, WAKEUP_GPIO_IDX, 0);
	#else
	val8 = (pwrctl->is_high_active == 0) ? 1 : 0;
	RTW_PRINT("Set Wake GPIO to default(%d).\n", val8);
	rtw_hal_set_output_gpio(padapter, WAKEUP_GPIO_IDX, val8);

	rtw_hal_switch_gpio_wl_ctrl(padapter, WAKEUP_GPIO_IDX, _FALSE);
	#endif/*CONFIG_WAKEUP_GPIO_INPUT_MODE*/
#endif /* CONFIG_RTW_ONE_PIN_GPIO */
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
	struct p2p_channels *ch_list = &(adapter_to_rfctl(padapter)->channel_list);
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
		       + (1 + 1) * (u16)ch_list->reg_classes
		       + get_reg_classes_full_count(ch_list);

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
	} else
#endif /* CONFIG_CONCURRENT_MODE */
	{
		int i, j;
		for (j = 0; j < ch_list->reg_classes; j++) {
			/*	Operating Class */
			p2pie[p2pielen++] = ch_list->reg_class[j].reg_class;

			/*	Number of Channels */
			p2pie[p2pielen++] = ch_list->reg_class[j].channels;

			/*	Channel List */
			for (i = 0; i < ch_list->reg_class[j].channels; i++)
				p2pie[p2pielen++] = ch_list->reg_class[j].channel[i];
		}
	}

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
		struct p2p_channels *ch_list = &(adapter_to_rfctl(padapter)->channel_list);

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
			+ (1 + 1) * (u16)ch_list->reg_classes
			+ get_reg_classes_full_count(ch_list);

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
		} else
#endif /* CONFIG_CONCURRENT_MODE */
		{
			int i, j;
			for (j = 0; j < ch_list->reg_classes; j++) {
				/*	Operating Class */
				p2pie[p2pielen++] = ch_list->reg_class[j].reg_class;

				/*	Number of Channels */
				p2pie[p2pielen++] = ch_list->reg_class[j].channels;

				/*	Channel List */
				for (i = 0; i < ch_list->reg_class[j].channels; i++)
					p2pie[p2pielen++] = ch_list->reg_class[j].channel[i];
			}
		}
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

void rtw_hal_construct_beacon(_adapter *padapter,
				     u8 *pframe, u32 *pLength)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;
	u32					pktlen;
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

#if 0
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
#endif

	if ((pktlen + TXDESC_SIZE) > MAX_BEACON_LEN) {
		RTW_ERR("beacon frame too large ,len(%d,%d)\n",
			(pktlen + TXDESC_SIZE), MAX_BEACON_LEN);
		rtw_warn_on(1);
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


#ifdef DBG_FW_DEBUG_MSG_PKT
void rtw_hal_construct_fw_dbg_msg_pkt(
	PADAPTER padapter,
	u8		*pframe,
	u32		*plength)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16						*fctrl;
	u32						pktlen;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wlan_network		*cur_network = &pmlmepriv->cur_network;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};


	/* RTW_INFO("%s:%d\n", __FUNCTION__, bForcePowerSave); */

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);

	set_frame_sub_type(pframe, WIFI_DATA);

	pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	*plength = pktlen;
}
#endif /*DBG_FW_DEBUG_MSG_PKT*/

void rtw_hal_construct_NullFunctionData(
	PADAPTER padapter,
	u8		*pframe,
	u32		*pLength,
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
	u8 *sta_addr = NULL;
	u8 bssid[ETH_ALEN] = {0};

	/* RTW_INFO("%s:%d\n", __FUNCTION__, bForcePowerSave); */

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;
	if (bForcePowerSave)
		SetPwrMgt(fctrl);

	sta_addr = get_my_bssid(&pmlmeinfo->network);
	if (NULL == sta_addr) {
		_rtw_memcpy(bssid, adapter_mac_addr(padapter), ETH_ALEN);
		sta_addr = bssid;
	}

	switch (cur_network->network.InfrastructureMode) {
	case Ndis802_11Infrastructure:
		SetToDs(fctrl);
		_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, sta_addr, ETH_ALEN);
		break;
	case Ndis802_11APMode:
		SetFrDs(fctrl);
		_rtw_memcpy(pwlanhdr->addr1, sta_addr, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr2, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, adapter_mac_addr(padapter), ETH_ALEN);
		break;
	case Ndis802_11IBSS:
	default:
		_rtw_memcpy(pwlanhdr->addr1, sta_addr, ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
		_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
		break;
	}

	SetSeqNum(pwlanhdr, 0);
	set_duration(pwlanhdr, 0);

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
				BOOLEAN bHideSSID)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;
	u8					*mac, *bssid, *sta_addr;
	u32					pktlen;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX  *cur_network = &(pmlmeinfo->network);

	/*RTW_INFO("%s\n", __FUNCTION__);*/

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	mac = adapter_mac_addr(padapter);
	bssid = cur_network->MacAddress;
	sta_addr = get_my_bssid(&pmlmeinfo->network);

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	_rtw_memcpy(pwlanhdr->addr1, sta_addr, ETH_ALEN);
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
	SET_ARP_HTYPE(pARPRspPkt, 1);
	SET_ARP_PTYPE(pARPRspPkt, ETH_P_IP);	/* IP protocol */
	SET_ARP_HLEN(pARPRspPkt, ETH_ALEN);
	SET_ARP_PLEN(pARPRspPkt, RTW_IP_ADDR_LEN);
	SET_ARP_OPER(pARPRspPkt, 2);	/* ARP response */
	SET_ARP_SENDER_MAC_ADDR(pARPRspPkt, adapter_mac_addr(padapter));
	SET_ARP_SENDER_IP_ADDR(pARPRspPkt, pIPAddress);
#ifdef CONFIG_ARP_KEEP_ALIVE
	if (!is_zero_mac_addr(pmlmepriv->gw_mac_addr)) {
		SET_ARP_TARGET_MAC_ADDR(pARPRspPkt, pmlmepriv->gw_mac_addr);
		SET_ARP_TARGET_IP_ADDR(pARPRspPkt, pmlmepriv->gw_ip);
	} else
#endif
	{
		SET_ARP_TARGET_MAC_ADDR(pARPRspPkt,
				    get_my_bssid(&(pmlmeinfo->network)));
		SET_ARP_TARGET_IP_ADDR(pARPRspPkt,
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

#ifdef CONFIG_IPV6
/*
 * Description: Neighbor Discovery Offload.
 */
static void rtw_hal_construct_na_message(_adapter *padapter,
				     u8 *pframe, u32 *pLength)
{
	struct rtw_ieee80211_hdr *pwlanhdr = NULL;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;
	struct security_priv	*psecuritypriv = &padapter->securitypriv;

	u32 pktlen = 0;
	u16 *fctrl = NULL;

	u8 ns_hdr[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x86, 0xDD};
	u8 ipv6_info[4] = {0x60, 0x00, 0x00, 0x00};
	u8 ipv6_contx[4] = {0x00, 0x20, 0x3a, 0xff};
	u8 icmpv6_hdr[8] = {0x88, 0x00, 0x00, 0x00, 0x60, 0x00, 0x00, 0x00};
	u8 val8 = 0;

	u8 *p_na_msg = pframe;
	/* for TKIP Cal MIC */
	u8 *payload = pframe;
	u8 EncryptionHeadOverhead = 0, na_msg_offset = 0;
	/* RTW_INFO("%s:%d\n", __FUNCTION__, bForcePowerSave); */

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	/* ------------------------------------------------------------------------- */
	/* MAC Header. */
	/* ------------------------------------------------------------------------- */
	SetFrameType(fctrl, WIFI_DATA);
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
	na_msg_offset = *pLength;
	p_na_msg = (u8 *)(pframe + na_msg_offset);
	payload = p_na_msg; /* Get Payload pointer */

	/* LLC header */
	val8 = sizeof(ns_hdr);
	_rtw_memcpy(p_na_msg, ns_hdr, val8);
	*pLength += val8;
	p_na_msg += val8;

	/* IPv6 Header */
	/* 1 . Information (4 bytes): 0x60 0x00 0x00 0x00 */
	val8 = sizeof(ipv6_info);
	_rtw_memcpy(p_na_msg, ipv6_info, val8);
	*pLength += val8;
	p_na_msg += val8;

	/* 2 . playload : 0x00 0x20 , NextProt : 0x3a (ICMPv6) HopLim : 0xff */
	val8 = sizeof(ipv6_contx);
	_rtw_memcpy(p_na_msg, ipv6_contx, val8);
	*pLength += val8;
	p_na_msg += val8;

	/* 3 . SA : 16 bytes , DA : 16 bytes ( Fw will filled ) */
	_rtw_memset(&(p_na_msg[*pLength]), 0, 32);
	*pLength += 32;
	p_na_msg += 32;

	/* ICMPv6 */
	/* 1. Type : 0x88 (NA)
	 * 2. Code : 0x00
	 * 3. ChechSum : 0x00 0x00 (RSvd)
	 * 4. NAFlag: 0x60 0x00 0x00 0x00 ( Solicited , Override)
	 */
	val8 = sizeof(icmpv6_hdr);
	_rtw_memcpy(p_na_msg, icmpv6_hdr, val8);
	*pLength += val8;
	p_na_msg += val8;

	/* TA: 16 bytes*/
	_rtw_memset(&(p_na_msg[*pLength]), 0, 16);
	*pLength += 16;
	p_na_msg += 16;

	/* ICMPv6 Target Link Layer Address */
	p_na_msg[0] = 0x02; /* type */
	p_na_msg[1] = 0x01; /* len 1 unit of 8 octes */
	*pLength += 2;
	p_na_msg += 2;

	_rtw_memset(&(p_na_msg[*pLength]), 0, 6);
	*pLength += 6;
	p_na_msg += 6;

	if (psecuritypriv->dot11PrivacyAlgrthm == _TKIP_) {
		if (IS_HARDWARE_TYPE_8188E(padapter) ||
		    IS_HARDWARE_TYPE_8812(padapter)) {
			rtw_hal_append_tkip_mic(padapter, pframe,
						na_msg_offset);
		}
		*pLength += 8;
	}
}
/*
 * Description: Neighbor Discovery Protocol Information.
 */
static void rtw_hal_construct_ndp_info(_adapter *padapter,
				     u8 *pframe, u32 *pLength)
{
	struct mlme_ext_priv *pmlmeext = NULL;
	struct mlme_ext_info *pmlmeinfo = NULL;
	struct rtw_ndp_info ndp_info;
	u8	*pndp_info = pframe;
	u8	len = sizeof(struct rtw_ndp_info);

	RTW_INFO("%s: len: %d\n", __func__, len);

	pmlmeext =  &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	_rtw_memset(pframe, 0, len);
	_rtw_memset(&ndp_info, 0, len);

	ndp_info.enable = 1;
	ndp_info.check_remote_ip = 0;
	ndp_info.num_of_target_ip = 1;

	_rtw_memcpy(&ndp_info.target_link_addr, adapter_mac_addr(padapter),
		    ETH_ALEN);
	_rtw_memcpy(&ndp_info.target_ipv6_addr, pmlmeinfo->ip6_addr,
		    RTW_IPv6_ADDR_LEN);

	_rtw_memcpy(pndp_info, &ndp_info, len);
}
#endif /* CONFIG_IPV6 */

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

#define PN_2_CCMPH(ch,key_id)	((ch) & 0x000000000000ffff) \
				| (((ch) & 0x0000ffffffff0000) << 16) \
				| (((key_id) << 30)) \
				| BIT(29)
static void rtw_hal_construct_remote_control_info(_adapter *adapter,
						  u8 *pframe, u32 *pLength)
{
	struct mlme_priv   *pmlmepriv = &adapter->mlmepriv;
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct sta_info *psta;
	struct stainfo_rxcache *prxcache;
	u8 cur_dot11rxiv[8], id = 0, tid_id = 0, i = 0;
	size_t sz = 0, total = 0;
	u64 ccmp_hdr = 0, tmp_key = 0;

	psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));

	if (psta == NULL) {
		rtw_warn_on(1);
		return;
	}

	prxcache = &psta->sta_recvpriv.rxcache;
	sz = sizeof(cur_dot11rxiv);

	/* 3 SEC IV * 1 page */
	rtw_get_sec_iv(adapter, cur_dot11rxiv,
		       get_my_bssid(&pmlmeinfo->network));

	_rtw_memcpy(pframe, cur_dot11rxiv, sz);
	*pLength += sz;
	pframe += sz;

	_rtw_memset(&cur_dot11rxiv, 0, sz);

	if (psecuritypriv->ndisauthtype == Ndis802_11AuthModeWPA2PSK) {
		id = psecuritypriv->dot118021XGrpKeyid;
		tid_id = prxcache->last_tid;
		REMOTE_INFO_CTRL_SET_VALD_EN(cur_dot11rxiv, 0xdd);
		REMOTE_INFO_CTRL_SET_PTK_EN(cur_dot11rxiv, 1);
		REMOTE_INFO_CTRL_SET_GTK_EN(cur_dot11rxiv, 1);
		REMOTE_INFO_CTRL_SET_GTK_IDX(cur_dot11rxiv, id);
		_rtw_memcpy(pframe, cur_dot11rxiv, sz);
		*pLength += sz;
		pframe += sz;

		_rtw_memcpy(pframe, prxcache->iv[tid_id], sz);
		*pLength += sz;
		pframe += sz;

		total = sizeof(psecuritypriv->iv_seq);
		total /= sizeof(psecuritypriv->iv_seq[0]);

		for (i = 0 ; i < total ; i ++) {
			ccmp_hdr =
				le64_to_cpu(*(u64*)psecuritypriv->iv_seq[i]);
			_rtw_memset(&cur_dot11rxiv, 0, sz);
			if (ccmp_hdr != 0) {
				tmp_key = i;
				ccmp_hdr = PN_2_CCMPH(ccmp_hdr, tmp_key);
				*(u64*)cur_dot11rxiv = cpu_to_le64(ccmp_hdr);
				_rtw_memcpy(pframe, cur_dot11rxiv, sz);
			}
			*pLength += sz;
			pframe += sz;
		}
	}
}

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
	u32 ProbeReqLength = 0, ns_len = 0, rc_len = 0;
	u8 CurtPktPageNum = 0;

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

		rsvd_page_loc->LocArpRsp = *page_num;

		RTW_INFO("LocArpRsp: %d\n", rsvd_page_loc->LocArpRsp);

		rtw_hal_construct_ARPRsp(adapter, &pframe[index],
					 &ARPLength, pmlmeinfo->ip_addr);

		rtw_hal_fill_fake_txdesc(adapter,
					 &pframe[index - tx_desc],
					 ARPLength, _FALSE, _FALSE, _TRUE);

		CurtPktPageNum = (u8)PageNum(tx_desc + ARPLength, page_size);

		*page_num += CurtPktPageNum;

		index += (CurtPktPageNum * page_size);
		RSVD_PAGE_CFG("WOW-ARPRsp", CurtPktPageNum, *page_num, 0);

#ifdef CONFIG_IPV6
		/* 2 NS offload and NDP Info*/
		if (pwrctl->wowlan_ns_offload_en == _TRUE) {
			rsvd_page_loc->LocNbrAdv = *page_num;
			RTW_INFO("LocNbrAdv: %d\n", rsvd_page_loc->LocNbrAdv);
			rtw_hal_construct_na_message(adapter,
						     &pframe[index], &ns_len);
			rtw_hal_fill_fake_txdesc(adapter,
						 &pframe[index - tx_desc],
						 ns_len, _FALSE,
						 _FALSE, _TRUE);
			CurtPktPageNum = (u8)PageNum(tx_desc + ns_len,
						      page_size);
			*page_num += CurtPktPageNum;
			index += (CurtPktPageNum * page_size);
			RSVD_PAGE_CFG("WOW-NbrAdv", CurtPktPageNum, *page_num, 0);

			rsvd_page_loc->LocNDPInfo = *page_num;
			RTW_INFO("LocNDPInfo: %d\n",
				 rsvd_page_loc->LocNDPInfo);

			rtw_hal_construct_ndp_info(adapter,
						   &pframe[index - tx_desc],
						   &ns_len);
			CurtPktPageNum =
				(u8)PageNum(tx_desc + ns_len, page_size);
			*page_num += CurtPktPageNum;
			index += (CurtPktPageNum * page_size);
			RSVD_PAGE_CFG("WOW-NDPInfo", CurtPktPageNum, *page_num, 0);

		}
#endif /*CONFIG_IPV6*/
		/* 3 Remote Control Info. * 1 page */
		rsvd_page_loc->LocRemoteCtrlInfo = *page_num;
		RTW_INFO("LocRemoteCtrlInfo: %d\n", rsvd_page_loc->LocRemoteCtrlInfo);
		rtw_hal_construct_remote_control_info(adapter,
						      &pframe[index - tx_desc],
						      &rc_len);
		CurtPktPageNum = (u8)PageNum(rc_len, page_size);
		*page_num += CurtPktPageNum;
		*total_pkt_len = index + rc_len;
		RSVD_PAGE_CFG("WOW-RCI", CurtPktPageNum, *page_num, *total_pkt_len);
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
		RSVD_PAGE_CFG("WOW-GTKInfo", CurtPktPageNum, *page_num, 0);

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
		RSVD_PAGE_CFG("WOW-GTKRsp", CurtPktPageNum, *page_num, 0);

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
		RSVD_PAGE_CFG("WOW-GTKEXTMEM", CurtPktPageNum, *page_num, *total_pkt_len);
#endif /* CONFIG_GTK_OL */

		index += (CurtPktPageNum * page_size);

		/*Reserve 1 page for AOAC report*/
		rsvd_page_loc->LocAOACReport = *page_num;
		RTW_INFO("LocAOACReport: %d\n", rsvd_page_loc->LocAOACReport);
		*page_num += 1;
		*total_pkt_len = index + (page_size * 1);
		RSVD_PAGE_CFG("WOW-AOAC", 1, *page_num, *total_pkt_len);
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
			RSVD_PAGE_CFG("WOW-ProbeReq", CurtPktPageNum, *page_num, 0);

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
				RSVD_PAGE_CFG("WOW-ProbeReq", CurtPktPageNum, *page_num, 0);
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
			RSVD_PAGE_CFG("WOW-PNOInfo", CurtPktPageNum, *page_num, 0);

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
			RSVD_PAGE_CFG("WOW-ScanInfo", CurtPktPageNum, *page_num, *total_pkt_len);
		}
#endif /* CONFIG_PNO_SUPPORT */
	}
}

static void rtw_hal_gate_bb(_adapter *adapter, bool stop)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	u8 i = 0, val8 = 0, empty = _FAIL;

	if (stop) {
		/* checking TX queue status */
		for (i = 0 ; i < 5 ; i++) {
			rtw_hal_get_hwreg(adapter, HW_VAR_CHK_MGQ_CPU_EMPTY, &empty);
			if (empty) {
				break;
			} else {
				RTW_WARN("%s: MGQ_CPU is busy(%d)!\n",
					 __func__, i);
				rtw_mdelay_os(10);
			}
		}

		if (val8 == 5)
			RTW_ERR("%s: Polling MGQ_CPU empty fail!\n", __func__);

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
	u8 mask_len = 0;
	u8 mac_addr[ETH_ALEN] = {0};
	u16 count = 0;
	int i;

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

#ifndef CONFIG_WOW_PATTERN_IN_TXFIFO
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
#else /* CONFIG_WOW_PATTERN_IN_TXFIFO */
bool rtw_read_from_frame_mask(_adapter *adapter, u8 idx)
{
	u32 data_l = 0, data_h = 0, rx_dma_buff_sz = 0, page_sz = 0;
	u16 tx_page_start, tx_buf_ptr = 0;
	u16 cam_start_offset = 0;
	u16 ctrl_l = 0, ctrl_h = 0;
	u8 count = 0, tmp = 0, last_entry = 0;
	int i = 0;
	bool res = _TRUE;

	if (idx > MAX_WKFM_CAM_NUM) {
		RTW_INFO("[Error]: %s, pattern index is out of range\n",
			 __func__);
		return _FALSE;
	}

	rtw_hal_get_def_var(adapter, HAL_DEF_TX_PAGE_SIZE, (u8 *)&page_sz);
	if (page_sz == 0) {
		RTW_INFO("[Error]: %s, page_sz is 0!!\n", __func__);
		return _FALSE;
	}

	rtw_hal_get_def_var(adapter, HAL_DEF_TX_BUFFER_LAST_ENTRY, (u8 *)&last_entry);
	if (last_entry == 0) {
		RTW_INFO("[Error]: %s, last entry of tx buffer is 0!!\n", __func__);
		return _FALSE;
	}

	/* use the last 2 pages for wow pattern e.g. 0xfe and 0xff */
	tx_page_start = last_entry - 1;
	cam_start_offset = tx_page_start * page_sz / 8;
	ctrl_l = 0x0;
	ctrl_h = 0x0;

	/* Enable TX packet buffer access */
	rtw_write8(adapter, REG_PKT_BUFF_ACCESS_CTRL, TXPKT_BUF_SELECT);

	/* Read the WKFM CAM */
	for (i = 0; i < (WKFMCAM_ADDR_NUM / 2); i++) {
		/*
		 * Set Tx packet buffer offset.
		 * TxBufer pointer increases 1, we can access 8 bytes in Tx packet buffer.
		 * CAM start offset (unit: 1 byte) =  Index*WKFMCAM_SIZE
		 * TxBufer pointer addr = (CAM start offset + per entry offset of a WKFMCAM)/8
		 * * Index: The index of the wake up frame mask
		 * * WKFMCAM_SIZE: the total size of one WKFM CAM
		 * * per entry offset of a WKFM CAM: Addr i * 4 bytes
		 */
		tx_buf_ptr =
			(cam_start_offset + idx * WKFMCAM_SIZE + i * 8) >> 3;
		rtw_write16(adapter, REG_PKTBUF_DBG_CTRL, tx_buf_ptr);
		rtw_write16(adapter, REG_RXPKTBUF_CTRL, ctrl_l);
		data_l = rtw_read32(adapter, REG_PKTBUF_DBG_DATA_L);
		data_h = rtw_read32(adapter, REG_PKTBUF_DBG_DATA_H);

		RTW_INFO("[%d]: %08x %08x\n", i, data_h, data_l);

		count = 0;

		do {
			tmp = rtw_read32(adapter, REG_PKTBUF_DBG_CTRL) & BIT23;
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
	u32 tx_page_start = 0, page_sz = 0;
	u16 tx_buf_ptr = 0;
	u16 cam_start_offset = 0;
	u32 data_l = 0, data_h = 0;
	u8 count = 0, tmp = 0, last_entry = 0;
	int res = 0, i = 0;

	if (idx > MAX_WKFM_CAM_NUM) {
		RTW_INFO("[Error]: %s, pattern index is out of range\n",
			 __func__);
		return _FALSE;
	}

	rtw_hal_get_def_var(adapter, HAL_DEF_TX_PAGE_SIZE, (u8 *)&page_sz);
	if (page_sz == 0) {
		RTW_INFO("[Error]: %s, page_sz is 0!!\n", __func__);
		return _FALSE;
	}

	rtw_hal_get_def_var(adapter, HAL_DEF_TX_BUFFER_LAST_ENTRY, (u8 *)&last_entry);
	if (last_entry == 0) {
		RTW_INFO("[Error]: %s, last entry of tx buffer is 0!!\n", __func__);
		return _FALSE;
	}

	/* use the last 2 pages for wow pattern e.g. 0xfe and 0xff */
	tx_page_start = last_entry - 1;
	cam_start_offset = tx_page_start * page_sz / 8;

	/* Write the PATTERN location to BIT_TXBUF_WKCAM_OFFSET */
	rtw_write8(adapter, REG_TXBUF_WKCAM_OFFSET, cam_start_offset & 0xFF);
	rtw_write8(adapter, REG_TXBUF_WKCAM_OFFSET + 1, (cam_start_offset >> 8) & 0xFF);
	/* Enable TX packet buffer access */
	rtw_write8(adapter, REG_PKT_BUFF_ACCESS_CTRL, TXPKT_BUF_SELECT);

	/* Write the WKFM CAM */
	for (i = 0; i < WKFMCAM_ADDR_NUM / 2; i++) {
		/*
		 * Set Tx packet buffer offset.
		 * TxBufer pointer increases 1, we can access 8 bytes in Rx packet buffer.
		 * CAM start offset (unit: 1 byte) =  Index*WKFMCAM_SIZE
		 * TxBufer pointer addr = (CAM start offset + per entry offset of a WKFMCAM)/8
		 * * Index: The index of the wake up frame mask
		 * * WKFMCAM_SIZE: the total size of one WKFM CAM
		 * * per entry offset of a WKFM CAM: Addr i * 4 bytes
		 */
		tx_buf_ptr = cam_start_offset + ((idx * WKFMCAM_SIZE + i * 8) >> 3);

		if (i == 0) {
			if (context->type == PATTERN_VALID)
				data_l = BIT(31);
			else if (context->type == PATTERN_BROADCAST)
				data_l = BIT(31) | BIT(26);
			else if (context->type == PATTERN_MULTICAST)
				data_l = BIT(31) | BIT(25);
			else if (context->type == PATTERN_UNICAST)
				data_l = BIT(31) | BIT(24);

			if (context->crc != 0)
				data_l |= context->crc;

			rtw_write32(adapter, REG_PKTBUF_DBG_DATA_L, data_l);
		} else {
			data_l = context->mask[i * 2 - 2];
			data_h = context->mask[i * 2 - 1];
			rtw_write32(adapter, REG_PKTBUF_DBG_DATA_L, data_l);
			rtw_write32(adapter, REG_PKTBUF_DBG_DATA_H, data_h);
		}

		rtw_write32(adapter, REG_PKTBUF_DBG_CTRL, (tx_buf_ptr & 0xFFFF) | BIT23 | (0xff <<24));
		count = 0;
		do {
			tmp = rtw_read32(adapter, REG_PKTBUF_DBG_CTRL) & BIT23;
			rtw_udelay_os(2);
			count++;
		} while (tmp && count < 100);

		if (count >= 100) {
			res = _FALSE;
			RTW_INFO("%s write failed\n", __func__);
		} else {
			res = _TRUE;
			RTW_INFO("%s write OK\n", __func__);
		}
	}

	/* Disable TX packet buffer access */
	rtw_write8(adapter, REG_PKT_BUFF_ACCESS_CTRL, DISABLE_TRXPKT_BUF_ACCESS);
	return res;
}
#endif /* CONFIG_WOW_PATTERN_IN_TXFIFO */

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
#if 0
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
#endif

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
	systime start = 0;
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
	systime start = 0, end = 0;
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
	systime start = 0;
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
	struct registry_priv  *registry_par = &adapter->registrypriv;
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct sta_info *psta = NULL;
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(adapter);
	int res;
	u16 media_status_rpt;
	u8 no_wake = 0;
	
#ifdef CONFIG_LPS_PG
	u8 lps_pg_hdl_id = 0;
#endif



	if(registry_par->suspend_type == FW_IPS_DISABLE_BBRF &&
	!check_fwstate(pmlmepriv, _FW_LINKED))
		no_wake = 1;

	RTW_PRINT(FUNC_ADPT_FMT " WOWLAN_ENABLE\n", FUNC_ADPT_ARG(adapter));
	rtw_hal_gate_bb(adapter, _TRUE);
#ifdef CONFIG_GTK_OL
	if (psecuritypriv->binstallKCK_KEK == _TRUE)
		rtw_hal_fw_sync_cam_id(adapter);
#endif
	if (IS_HARDWARE_TYPE_8723B(adapter))
		rtw_hal_backup_rate(adapter);

	rtw_hal_fw_dl(adapter, _TRUE);
	if(no_wake)
		media_status_rpt = RT_MEDIA_DISCONNECT;
	else
		media_status_rpt = RT_MEDIA_CONNECT;
	rtw_hal_set_hwreg(adapter, HW_VAR_H2C_FW_JOINBSSRPT,
			  (u8 *)&media_status_rpt);

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
	if(!no_wake)
		rtw_hal_dl_pattern(adapter, 1);

	if (!pwrctl->wowlan_pno_enable) {
		psta = rtw_get_stainfo(&adapter->stapriv, get_bssid(pmlmepriv));

		if (psta != NULL) {
			#ifdef CONFIG_FW_MULTI_PORT_SUPPORT
			adapter_to_dvobj(adapter)->dft.port_id = 0xFF;
			adapter_to_dvobj(adapter)->dft.mac_id = 0xFF;
			rtw_hal_set_default_port_id_cmd(adapter, psta->cmn.mac_id);
			#endif
			if(!no_wake)
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

#ifdef CONFIG_LPS_PG
	if (pwrctl->lps_level == LPS_PG) {
		lps_pg_hdl_id = LPS_PG_INFO_CFG;
		rtw_hal_set_hwreg(adapter, HW_VAR_LPS_PG_HANDLE, (u8 *)(&lps_pg_hdl_id));
	}
#endif

#ifdef CONFIG_USB_HCI
	/* free adapter's resource */
	rtw_mi_intf_stop(adapter);

#endif
#if defined(CONFIG_USB_HCI) || defined(CONFIG_PCI_HCI)
	/* Invoid SE0 reset signal during suspending*/
	rtw_write8(adapter, REG_RSV_CTRL, 0x20);
	if (IS_8188F(pHalData->version_id) == FALSE
		&& IS_8188GTV(pHalData->version_id) == FALSE)
		rtw_write8(adapter, REG_RSV_CTRL, 0x60);
#endif

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
	struct sta_info *psta = NULL;
	struct registry_priv  *registry_par = &adapter->registrypriv;
	int res;
	u16 media_status_rpt;
	u8 val8;

	RTW_PRINT("%s, WOWLAN_DISABLE\n", __func__);
	
	if(registry_par->suspend_type == FW_IPS_DISABLE_BBRF && !check_fwstate(pmlmepriv, _FW_LINKED)) {
		RTW_INFO("FW_IPS_DISABLE_BBRF resume\n");
		return;
	}
	
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

	if ((pwrctl->wowlan_wake_reason != RX_DISASSOC) &&
		(pwrctl->wowlan_wake_reason != RX_DEAUTH) &&
		(pwrctl->wowlan_wake_reason != FW_DECISION_DISCONNECT)) {
		rtw_hal_get_aoac_rpt(adapter);
		rtw_hal_update_sw_security_info(adapter);
	}

	rtw_hal_fw_dl(adapter, _FALSE);

#ifdef CONFIG_GPIO_WAKEUP

#ifdef CONFIG_RTW_ONE_PIN_GPIO
	rtw_hal_set_input_gpio(adapter, WAKEUP_GPIO_IDX);
#else
#ifdef CONFIG_WAKEUP_GPIO_INPUT_MODE
	if (pwrctl->is_high_active == 0)
		rtw_hal_set_input_gpio(adapter, WAKEUP_GPIO_IDX);
	else
		rtw_hal_set_output_gpio(adapter, WAKEUP_GPIO_IDX, 0);
#else
	val8 = (pwrctl->is_high_active == 0) ? 1 : 0;
	RTW_PRINT("Set Wake GPIO to default(%d).\n", val8);

	rtw_hal_set_output_gpio(adapter, WAKEUP_GPIO_IDX, val8);
	rtw_hal_switch_gpio_wl_ctrl(adapter, WAKEUP_GPIO_IDX, _FALSE);
#endif
#endif /* CONFIG_RTW_ONE_PIN_GPIO */
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
			adapter_to_dvobj(adapter)->dft.port_id = 0xFF;
			adapter_to_dvobj(adapter)->dft.mac_id = 0xFF;
			rtw_hal_set_default_port_id_cmd(adapter, psta->cmn.mac_id);
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
	RSVD_PAGE_CFG("WOW-P2P-Beacon", CurtPktPageNum, *page_num, 0);

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
	RSVD_PAGE_CFG("WOW-P2P-ProbeRsp", CurtPktPageNum, *page_num, 0);

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
	RSVD_PAGE_CFG("WOW-P2P-NegoRsp", CurtPktPageNum, *page_num, 0);

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
	RSVD_PAGE_CFG("WOW-P2P-InviteRsp", CurtPktPageNum, *page_num, 0);

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
	RSVD_PAGE_CFG("WOW-P2P-PDR", CurtPktPageNum, *page_num, *total_pkt_len);

	index += (CurtPktPageNum * page_size);


}
#endif /* CONFIG_P2P_WOWLAN */

#ifdef CONFIG_LPS_PG
#ifndef DBG_LPSPG_INFO_DUMP
#define DBG_LPSPG_INFO_DUMP 1
#endif

#include "hal_halmac.h"

#ifdef CONFIG_RTL8822C
static int rtw_lps_pg_set_dpk_info_rsvd_page(_adapter *adapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct dm_struct *dm = adapter_to_phydm(adapter);
	struct rsvd_page_cache_t *cache = &pwrpriv->lpspg_dpk_info;
	u8 *info = NULL;
	u32 info_len;
	int ret = _FAIL;

	/* get length */
	halrf_dpk_info_rsvd_page(dm, NULL, &info_len);
	if (!info_len) {
		RTW_ERR("get %s length fail\n", cache->name);
		goto exit;
	}

	/* allocate buf */
	info = rtw_zmalloc(info_len);
	if (!info) {
		RTW_ERR("alloc %s buffer fail(len=%d)\n", cache->name, info_len);
		goto exit;
	}

	/* get content */
	halrf_dpk_info_rsvd_page(dm, info, NULL);

	if (rsvd_page_cache_update_data(cache, info, info_len)) {

		#if (DBG_LPSPG_INFO_DUMP >= 1)
		RTW_INFO_DUMP(cache->name, info, info_len);
		#endif

		ret = rtw_halmac_download_rsvd_page(dvobj, cache->loc, info, info_len);
		ret = !ret ? _SUCCESS : _FAIL;
		if (ret != _SUCCESS) {
			RTW_ERR("download %s rsvd page to offset:%u fail\n", cache->name, cache->loc);
			goto free_mem;
		}

		#if (DBG_LPSPG_INFO_DUMP >= 2)
		RTW_INFO("get %s from rsvd page offset:%d\n", cache->name, cache->loc);
		rtw_dump_rsvd_page(RTW_DBGDUMP, adapter, cache->loc, cache->page_num);
		#endif
	}

free_mem:
	rtw_mfree(info, info_len);

exit:
	return ret;
}

static int rtw_lps_pg_set_iqk_info_rsvd_page(_adapter *adapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct dm_struct *dm = adapter_to_phydm(adapter);
	struct rsvd_page_cache_t *cache = &pwrpriv->lpspg_iqk_info;
	u8 *info = NULL;
	u32 info_len = 0;
	int ret = _FAIL;

	if (hal_data->RegIQKFWOffload) {
		rsvd_page_cache_free_data(cache);
		ret = _SUCCESS;
		goto exit;
	}

	/* get length */
	halrf_iqk_info_rsvd_page(dm, NULL, &info_len);
	if (!info_len) {
		RTW_ERR("get %s length fail\n", cache->name);
		goto exit;
	}

	/* allocate buf */
	info = rtw_zmalloc(info_len);
	if (!info) {
		RTW_ERR("alloc %s buffer fail(len=%d)\n", cache->name, info_len);
		goto exit;
	}

	/* get content */
	halrf_iqk_info_rsvd_page(dm, info, NULL);

	if (rsvd_page_cache_update_data(cache, info, info_len)) {

		#if (DBG_LPSPG_INFO_DUMP >= 1)
		RTW_INFO_DUMP(cache->name, info, info_len);
		#endif

		ret = rtw_halmac_download_rsvd_page(dvobj, cache->loc, info, info_len);
		ret = !ret ? _SUCCESS : _FAIL;
		if (ret != _SUCCESS) {
			RTW_ERR("download %s rsvd page to offset:%u fail\n", cache->name, cache->loc);
			goto free_mem;
		}

		#if (DBG_LPSPG_INFO_DUMP >= 2)
		RTW_INFO("get %s from rsvd page offset:%d\n", cache->name, cache->loc);
		rtw_dump_rsvd_page(RTW_DBGDUMP, adapter, cache->loc, cache->page_num);
		#endif
	}

free_mem:
	rtw_mfree(info, info_len);

exit:
	return ret;
}
#endif /* CONFIG_RTL8822C */

static void rtw_hal_build_lps_pg_info_rsvd_page(_adapter *adapter, u8 *buf, u32 *buf_size)
{
#define LPS_PG_INFO_RSVD_LEN	16

	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	struct sta_info	*psta;
#ifdef CONFIG_MBSSID_CAM
	u8 cam_id = INVALID_CAM_ID;
#endif
	u8 *psec_cam_id = buf + 8;
	u8 sec_cam_num = 0;
	u8 drv_rsvdpage_num = 0;

	if (buf) {
		psta = rtw_get_stainfo(&adapter->stapriv, get_bssid(&adapter->mlmepriv));
		if (!psta) {
			RTW_ERR("%s [ERROR] sta is NULL\n", __func__);
			rtw_warn_on(1);
			return;
		}

		/*Byte 0 - used macid*/
		LPSPG_RSVD_PAGE_SET_MACID(buf, psta->cmn.mac_id);
		RTW_INFO("[LPSPG-INFO] mac_id:%d\n", psta->cmn.mac_id);

#ifdef CONFIG_MBSSID_CAM
		/*Byte 1 - used BSSID CAM entry*/
		cam_id = rtw_mbid_cam_search_by_ifaceid(adapter, adapter->iface_id);
		if (cam_id != INVALID_CAM_ID)
			LPSPG_RSVD_PAGE_SET_MBSSCAMID(buf, cam_id);
		RTW_INFO("[LPSPG-INFO] mbss_cam_id:%d\n", cam_id);
#endif

#ifdef CONFIG_WOWLAN /*&& pattern match cam used*/
		/*Btye 2 - Max used Pattern Match CAM entry*/
		if (pwrpriv->wowlan_mode == _TRUE &&
		    check_fwstate(&adapter->mlmepriv, _FW_LINKED) == _TRUE) {
			LPSPG_RSVD_PAGE_SET_PMC_NUM(buf, pwrpriv->wowlan_pattern_idx);
			RTW_INFO("[LPSPG-INFO] Max Pattern Match CAM entry :%d\n", pwrpriv->wowlan_pattern_idx);
		}
#endif
#ifdef CONFIG_BEAMFORMING  /*&& MU BF*/
		/*Btye 3 - Max MU rate table Group ID*/
		LPSPG_RSVD_PAGE_SET_MU_RAID_GID(buf, 0);
		RTW_INFO("[LPSPG-INFO] Max MU rate table Group ID :%d\n", 0);
#endif

		/*Btye 8 ~15 - used Security CAM entry */
		sec_cam_num = rtw_get_sec_camid(adapter, 8, psec_cam_id);

		/*Btye 4 - used Security CAM entry number*/
		if (sec_cam_num < 8)
			LPSPG_RSVD_PAGE_SET_SEC_CAM_NUM(buf, sec_cam_num);
		RTW_INFO("[LPSPG-INFO] Security CAM entry number :%d\n", sec_cam_num);

		/*Btye 5 - Txbuf used page number for fw offload*/
		if (pwrpriv->wowlan_mode == _TRUE || pwrpriv->wowlan_ap_mode == _TRUE)
			drv_rsvdpage_num = rtw_hal_get_txbuff_rsvd_page_num(adapter, _TRUE);
		else
			drv_rsvdpage_num = rtw_hal_get_txbuff_rsvd_page_num(adapter, _FALSE);
		LPSPG_RSVD_PAGE_SET_DRV_RSVDPAGE_NUM(buf, drv_rsvdpage_num);
		RTW_INFO("[LPSPG-INFO] DRV's rsvd page numbers :%d\n", drv_rsvdpage_num);
	}

	if (buf_size)
		*buf_size = LPS_PG_INFO_RSVD_LEN;
}

static int rtw_hal_set_lps_pg_info_rsvd_page(_adapter *adapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct rsvd_page_cache_t *cache = &pwrpriv->lpspg_info;
	u8 *info = NULL;
	u32 info_len = 0;
	int ret = _FAIL;

	/* get length */
	rtw_hal_build_lps_pg_info_rsvd_page(adapter, NULL, &info_len);
	if (!info_len) {
		RTW_ERR("get %s length fail\n", cache->name);
		goto exit;
	}

	/* allocate buf */
	info = rtw_zmalloc(info_len);
	if (!info) {
		RTW_ERR("alloc %s buffer fail(len=%d)\n", cache->name, info_len);
		goto exit;
	}

	/* get content */
	rtw_hal_build_lps_pg_info_rsvd_page(adapter, info, NULL);

	if (rsvd_page_cache_update_data(cache, info, info_len)) {

		#if (DBG_LPSPG_INFO_DUMP >= 1)
		RTW_INFO_DUMP(cache->name, info, info_len);
		#endif

		ret = rtw_halmac_download_rsvd_page(dvobj, cache->loc, info, info_len);
		ret = !ret ? _SUCCESS : _FAIL;
		if (ret != _SUCCESS) {
			RTW_ERR("download %s rsvd page to offset:%u fail\n", cache->name, cache->loc);
			goto free_mem;
		}

		#if (DBG_LPSPG_INFO_DUMP >= 2)
		RTW_INFO("get %s from rsvd page offset:%d\n", cache->name, cache->loc);
		rtw_dump_rsvd_page(RTW_DBGDUMP, adapter, cache->loc, cache->page_num);
		#endif
	}

free_mem:
	rtw_mfree(info, info_len);

exit:
	return ret;
}

static void rtw_lps_pg_set_rsvd_page(_adapter *adapter, u8 *frame, u16 *index
	, u8 txdesc_size, u32 page_size, u8 *total_page_num, bool is_wow_mode, bool only_get_page_num)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	struct rsvd_page_cache_t *cache;
	u8 *pos;
	u32 len;

	/* lps_level will not change when enter wow_mode */
	if (is_wow_mode && pwrctl->lps_level != LPS_PG)
		return;

	pos = only_get_page_num ? NULL : frame + *index;

#ifdef CONFIG_RTL8822C
	if (IS_8822C_SERIES(hal_data->version_id)) {
		/* LPSPG_DPK_INFO */
		cache = &pwrctl->lpspg_dpk_info;
		if (pwrctl->lps_level != LPS_PG)
			pos = NULL;
		halrf_dpk_info_rsvd_page(adapter_to_phydm(adapter), pos, &len);
		if (pos) {
			#if (DBG_LPSPG_INFO_DUMP >= 1)
			RTW_INFO_DUMP(cache->name, pos, len);
			#endif
		}
		rsvd_page_cache_update_all(cache, *total_page_num, txdesc_size, page_size, pos, len);
		*total_page_num += cache->page_num;
		*index += page_size * cache->page_num;
		pos = only_get_page_num ? NULL : frame + *index;
		RSVD_PAGE_CFG(cache->name, cache->page_num, *total_page_num, *index);

		/* LPSPG_IQK_INFO */
		cache = &pwrctl->lpspg_iqk_info;
		if (!(is_wow_mode && hal_data->RegIQKFWOffload)) { /* RegIQKFWOffload will not change when enter wow_mode */
			if (pwrctl->lps_level != LPS_PG || hal_data->RegIQKFWOffload)
				pos = NULL;
			halrf_iqk_info_rsvd_page(adapter_to_phydm(adapter), pos, &len);
			if (pos) {
				#if (DBG_LPSPG_INFO_DUMP >= 1)
				RTW_INFO_DUMP(cache->name, pos, len);
				#endif
			}
			rsvd_page_cache_update_all(cache, *total_page_num, txdesc_size, page_size, pos, len);
			*total_page_num += cache->page_num;
			*index += page_size * cache->page_num;
			pos = only_get_page_num ? NULL : frame + *index;
			RSVD_PAGE_CFG(cache->name, cache->page_num, *total_page_num, *index);
		} else
			rsvd_page_cache_free_data(cache);
	}
#endif

	/* LPSPG_INFO */
	cache = &pwrctl->lpspg_info;
	if (pwrctl->lps_level != LPS_PG)
		pos = NULL;
	rtw_hal_build_lps_pg_info_rsvd_page(adapter, pos, &len);
	if (pos) {
		#if (DBG_LPSPG_INFO_DUMP >= 1)
		RTW_INFO_DUMP(cache->name, pos, len);
		#endif
	}
	rsvd_page_cache_update_all(cache, *total_page_num, txdesc_size, page_size, pos, len);
	*total_page_num += cache->page_num;
	*index += page_size * cache->page_num;
	pos = only_get_page_num ? NULL : frame + *index;
	RSVD_PAGE_CFG(cache->name, cache->page_num, *total_page_num, *index);
}

static u8 rtw_hal_set_lps_pg_info_cmd(_adapter *adapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	u8 lpspg_info[H2C_LPS_PG_INFO_LEN] = {0};
	u8 ret = _FAIL;

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

	SET_H2CCMD_LPSPG_LOC(lpspg_info, pwrpriv->lpspg_info.loc);

#ifdef CONFIG_RTL8822C
	SET_H2CCMD_LPSPG_DPK_INFO_LOC(lpspg_info, pwrpriv->lpspg_dpk_info.loc);
	if (!GET_HAL_DATA(adapter)->RegIQKFWOffload)
		SET_H2CCMD_LPSPG_IQK_INFO_LOC(lpspg_info, pwrpriv->lpspg_iqk_info.loc);
#endif

#if (DBG_LPSPG_INFO_DUMP >= 1)
	RTW_INFO_DUMP("H2C_LPS_PG_INFO: ", lpspg_info, H2C_LPS_PG_INFO_LEN);
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

	if (pwrpriv->lpspg_info.loc == 0) {
		RTW_ERR("%s lpspg_info.loc = 0\n", __func__);
		rtw_warn_on(1);
		return ret;
	}
	#ifdef CONFIG_RTL8822C
	rtw_lps_pg_set_dpk_info_rsvd_page(adapter);
	rtw_lps_pg_set_iqk_info_rsvd_page(adapter);
	#endif
	rtw_hal_set_lps_pg_info_rsvd_page(adapter);

	ret = rtw_hal_set_lps_pg_info_cmd(adapter);

	return ret;
}

void rtw_hal_lps_pg_rssi_lv_decide(_adapter *adapter, struct sta_info *sta)
{
#if 0
	if (sta->cmn.ra_info.rssi_level >= 4)
		sta->lps_pg_rssi_lv = 3;	/*RSSI High - 1SS_VHT_MCS7*/
	else if (sta->cmn.ra_info.rssi_level >=  2)
		sta->lps_pg_rssi_lv = 2;	/*RSSI Middle - 1SS_VHT_MCS3*/
	else
		sta->lps_pg_rssi_lv = 1;	/*RSSI Lower - Lowest_rate*/
#else
	sta->lps_pg_rssi_lv = 0;
#endif
	RTW_INFO("%s mac-id:%d, rssi:%d, rssi_level:%d, lps_pg_rssi_lv:%d\n",
		__func__, sta->cmn.mac_id, sta->cmn.rssi_stat.rssi, sta->cmn.ra_info.rssi_level, sta->lps_pg_rssi_lv);
}

void rtw_hal_lps_pg_handler(_adapter *adapter, enum lps_pg_hdl_id hdl_id)
{
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct sta_info *sta;

	sta = rtw_get_stainfo(pstapriv, pmlmeinfo->network.MacAddress);

	switch (hdl_id) {
	case LPS_PG_INFO_CFG:
		rtw_hal_set_lps_pg_info(adapter);
		break;
	case LPS_PG_REDLEMEM:
		if (IS_8822C_SERIES(GET_HAL_DATA(adapter)->version_id))
			break;

		/*set xmit_block*/
		rtw_set_xmit_block(adapter, XMIT_BLOCK_REDLMEM);
		if (_FAIL == rtw_hal_fw_mem_dl(adapter, FW_EMEM))
			rtw_warn_on(1);
		/*clearn xmit_block*/
		rtw_clr_xmit_block(adapter, XMIT_BLOCK_REDLMEM);
		break;
	case LPS_PG_PHYDM_DIS:/*Disable RA and PT by H2C*/
		if (IS_8822C_SERIES(GET_HAL_DATA(adapter)->version_id))
			break;

		if (sta)
			rtw_phydm_lps_pg_hdl(adapter, sta, _TRUE);
		break;
	case LPS_PG_PHYDM_EN:/*Enable RA and PT by H2C*/
		if (IS_8822C_SERIES(GET_HAL_DATA(adapter)->version_id))
			break;

		if (sta) {
			rtw_hal_lps_pg_rssi_lv_decide(adapter, sta);
			rtw_phydm_lps_pg_hdl(adapter, sta, _FALSE);
			sta->lps_pg_rssi_lv = 0;
		}
		break;

	default:
		break;
	}
}

#endif /*CONFIG_LPS_PG*/

static u8 _rtw_mi_assoc_if_num(_adapter *adapter)
{
	u8 mi_iface_num = 0;

	if (0) {
		RTW_INFO("[IFS_ASSOC_STATUS] - STA :%d", DEV_STA_LD_NUM(adapter_to_dvobj(adapter)));
		RTW_INFO("[IFS_ASSOC_STATUS] - AP:%d", DEV_AP_NUM(adapter_to_dvobj(adapter)));
		RTW_INFO("[IFS_ASSOC_STATUS] - AP starting :%d", DEV_AP_STARTING_NUM(adapter_to_dvobj(adapter)));
		RTW_INFO("[IFS_ASSOC_STATUS] - MESH :%d", DEV_MESH_NUM(adapter_to_dvobj(adapter)));
		RTW_INFO("[IFS_ASSOC_STATUS] - ADHOC :%d", DEV_ADHOC_NUM(adapter_to_dvobj(adapter)));
		/*RTW_INFO("[IFS_ASSOC_STATUS] - P2P-GC :%d", DEV_P2P_GC_NUM(adapter_to_dvobj(adapter)));*/
		/*RTW_INFO("[IFS_ASSOC_STATUS] - P2P-GO :%d", DEV_P2P_GO_NUM(adapter_to_dvobj(adapter)));*/
	}

	mi_iface_num = (DEV_STA_LD_NUM(adapter_to_dvobj(adapter)) +
		DEV_AP_NUM(adapter_to_dvobj(adapter)) +
		DEV_AP_STARTING_NUM(adapter_to_dvobj(adapter)));
	return mi_iface_num;
}
#ifdef CONFIG_CONCURRENT_MODE
static _adapter *_rtw_search_sta_iface(_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	_adapter *iface = NULL;
	_adapter *sta_iface = NULL;
	int i;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (check_fwstate(&iface->mlmepriv, WIFI_STATION_STATE) == _TRUE) {
			if (check_fwstate(&iface->mlmepriv, _FW_LINKED) == _TRUE) {
				sta_iface = iface;
				break;
			}
		}
	}
	return sta_iface;
}
#if defined(CONFIG_AP_MODE) && defined(CONFIG_BT_COEXIST)
static _adapter *_rtw_search_ap_iface(_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	_adapter *iface = NULL;
	_adapter *ap_iface = NULL;
	int i;

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (check_fwstate(&iface->mlmepriv, WIFI_AP_STATE) == _TRUE ) {
			ap_iface = iface;
			break;
		}
	}
	return ap_iface;
}
#endif/*CONFIG_AP_MODE*/
#endif/*CONFIG_CONCURRENT_MODE*/

#ifdef CONFIG_CUSTOMER01_SMART_ANTENNA
void rtw_hal_set_pathb_phase(_adapter *adapter, u8 phase_idx)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT		*pDM_Odm = &pHalData->odmpriv;

	return phydm_pathb_q_matrix_rotate(pDM_Odm, phase_idx);
}
#endif

/*
 * Description: Fill the reserved packets that FW will use to RSVD page.
 *			Now we just send 4 types packet to rsvd page.
 *			(1)Beacon, (2)Ps-poll, (3)Null data, (4)ProbeRsp.
 * Input:
 * finished - FALSE:At the first time we will send all the packets as a large packet to Hw,
 *		    so we need to set the packet length to total lengh.
 *	      TRUE: At the second time, we should send the first packet (default:beacon)
 *		    to Hw again and set the lengh in descriptor to the real beacon lengh.
 * page_num - The amount of reserved page which driver need.
 *	      If this is not NULL, this function doesn't real download reserved
 *	      page, but just count the number of reserved page.
 *
 * 2009.10.15 by tynli.
 * 2017.06.20 modified by Lucas.
 *
 * Page Size = 128: 8188e, 8723a/b, 8192c/d,
 * Page Size = 256: 8192e, 8821a
 * Page Size = 512: 8812a
 */

/*#define DBG_DUMP_SET_RSVD_PAGE*/
static void _rtw_hal_set_fw_rsvd_page(_adapter *adapter, bool finished, u8 *page_num)
{
	PHAL_DATA_TYPE pHalData;
	struct xmit_frame	*pcmdframe = NULL;
	struct pkt_attrib	*pattrib;
	struct xmit_priv	*pxmitpriv;
	struct pwrctrl_priv *pwrctl;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	u32	BeaconLength = 0, ProbeRspLength = 0, PSPollLength = 0;
	u32	NullDataLength = 0, QosNullLength = 0, BTQosNullLength = 0;
	u32	ProbeReqLength = 0, NullFunctionDataLength = 0;
	u8	TxDescLen = TXDESC_SIZE, TxDescOffset = TXDESC_OFFSET;
	u8	TotalPageNum = 0 , CurtPktPageNum = 0 , RsvdPageNum = 0;
	u8	*ReservedPagePacket;
	u16	BufIndex = 0;
	u32	TotalPacketLen = 0, MaxRsvdPageBufSize = 0, PageSize = 0;
	RSVDPAGE_LOC	RsvdPageLoc;
	struct registry_priv  *registry_par = &adapter->registrypriv;

#ifdef DBG_FW_DEBUG_MSG_PKT
	u32	fw_dbg_msg_pkt_len = 0;
#endif /*DBG_FW_DEBUG_MSG_PKT*/

#ifdef DBG_CONFIG_ERROR_DETECT
	struct sreset_priv *psrtpriv;
#endif /* DBG_CONFIG_ERROR_DETECT */

#ifdef CONFIG_MCC_MODE
	u8 dl_mcc_page = _FAIL;
#endif /* CONFIG_MCC_MODE */
	u8 nr_assoc_if;

	_adapter *sta_iface = NULL;
	_adapter *ap_iface = NULL;

	bool is_wow_mode = _FALSE;

	pHalData = GET_HAL_DATA(adapter);
#ifdef DBG_CONFIG_ERROR_DETECT
	psrtpriv = &pHalData->srestpriv;
#endif
	pxmitpriv = &adapter->xmitpriv;
	pwrctl = adapter_to_pwrctl(adapter);

	rtw_hal_get_def_var(adapter, HAL_DEF_TX_PAGE_SIZE, (u8 *)&PageSize);

	if (PageSize == 0) {
		RTW_ERR("[Error]: %s, PageSize is zero!!\n", __func__);
		return;
	}
	nr_assoc_if = _rtw_mi_assoc_if_num(adapter);

	if ((pwrctl->wowlan_mode == _TRUE && pwrctl->wowlan_in_resume == _FALSE) ||
		pwrctl->wowlan_ap_mode == _TRUE ||
		pwrctl->wowlan_p2p_mode == _TRUE)
		is_wow_mode = _TRUE;

	/*page_num for init time to get rsvd page number*/
	/* Prepare ReservedPagePacket */
	if (page_num) {
		ReservedPagePacket = rtw_zmalloc(MAX_CMDBUF_SZ);
		if (!ReservedPagePacket) {
			RTW_WARN("%s: alloc ReservedPagePacket fail!\n", __FUNCTION__);
			*page_num = 0xFF;
			return;
		}
		RTW_INFO(FUNC_ADPT_FMT" Get [ %s ] RsvdPageNUm  ==>\n",
			FUNC_ADPT_ARG(adapter), (is_wow_mode) ? "WOW" : "NOR");

	} else {
		if (is_wow_mode)
			RsvdPageNum = rtw_hal_get_txbuff_rsvd_page_num(adapter, _TRUE);
		else
			RsvdPageNum = rtw_hal_get_txbuff_rsvd_page_num(adapter, _FALSE);

		RTW_INFO(FUNC_ADPT_FMT" PageSize: %d, [ %s ]-RsvdPageNUm: %d\n",
			FUNC_ADPT_ARG(adapter), PageSize, (is_wow_mode) ? "WOW" : "NOR", RsvdPageNum);

		MaxRsvdPageBufSize = RsvdPageNum * PageSize;
		if (MaxRsvdPageBufSize > MAX_CMDBUF_SZ) {
			RTW_ERR("%s MaxRsvdPageBufSize(%d) is larger than MAX_CMDBUF_SZ(%d)",
				 __func__, MaxRsvdPageBufSize, MAX_CMDBUF_SZ);
			rtw_warn_on(1);
			return;
		}

		pcmdframe = rtw_alloc_cmdxmitframe(pxmitpriv);
		if (pcmdframe == NULL) {
			RTW_ERR("%s: alloc ReservedPagePacket fail!\n", __FUNCTION__);
			return;
		}

		ReservedPagePacket = pcmdframe->buf_addr;
	}

	_rtw_memset(&RsvdPageLoc, 0, sizeof(RSVDPAGE_LOC));

	BufIndex = TxDescOffset;

	/*======== beacon content =======*/
	rtw_hal_construct_beacon(adapter,
				 &ReservedPagePacket[BufIndex], &BeaconLength);

	/*
	* When we count the first page size, we need to reserve description size for the RSVD
	* packet, it will be filled in front of the packet in TXPKTBUF.
	*/
	BeaconLength = MAX_BEACON_LEN - TxDescLen;
	CurtPktPageNum = (u8)PageNum((TxDescLen + BeaconLength), PageSize);

#ifdef CONFIG_FW_HANDLE_TXBCN
	CurtPktPageNum = CurtPktPageNum * CONFIG_LIMITED_AP_NUM;
#endif
	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum * PageSize);

	RSVD_PAGE_CFG("Beacon", CurtPktPageNum, TotalPageNum, TotalPacketLen);

	/*======== probe response content ========*/
	if (pwrctl->wowlan_ap_mode == _TRUE) {/*WOW mode*/
		#ifdef CONFIG_CONCURRENT_MODE
		if (nr_assoc_if >= 2)
			RTW_ERR("Not support > 2 net-interface in WOW\n");
		#endif
		/* (4) probe response*/
		RsvdPageLoc.LocProbeRsp = TotalPageNum;
		rtw_hal_construct_ProbeRsp(
			adapter, &ReservedPagePacket[BufIndex],
			&ProbeRspLength,
			_FALSE);
		rtw_hal_fill_fake_txdesc(adapter,
				 &ReservedPagePacket[BufIndex - TxDescLen],
				 ProbeRspLength, _FALSE, _FALSE, _FALSE);

		CurtPktPageNum = (u8)PageNum(TxDescLen + ProbeRspLength, PageSize);
		TotalPageNum += CurtPktPageNum;
		TotalPacketLen = BufIndex + ProbeRspLength;
		BufIndex += (CurtPktPageNum * PageSize);
		RSVD_PAGE_CFG("ProbeRsp", CurtPktPageNum, TotalPageNum, TotalPacketLen);
		goto download_page;
	}

	/*======== ps-poll content * 1 page ========*/
	sta_iface = adapter;
	#ifdef CONFIG_CONCURRENT_MODE
	if (!MLME_IS_STA(sta_iface) && DEV_STA_LD_NUM(adapter_to_dvobj(sta_iface))) {
		sta_iface = _rtw_search_sta_iface(adapter);
		RTW_INFO("get ("ADPT_FMT") to create PS-Poll/Null/QosNull\n", ADPT_ARG(sta_iface));
	}
	#endif

	if (MLME_IS_STA(sta_iface) || (nr_assoc_if == 0)) {
		RsvdPageLoc.LocPsPoll = TotalPageNum;
		RTW_INFO("LocPsPoll: %d\n", RsvdPageLoc.LocPsPoll);
		rtw_hal_construct_PSPoll(sta_iface,
					 &ReservedPagePacket[BufIndex], &PSPollLength);
		rtw_hal_fill_fake_txdesc(sta_iface,
					 &ReservedPagePacket[BufIndex - TxDescLen],
					 PSPollLength, _TRUE, _FALSE, _FALSE);

		CurtPktPageNum = (u8)PageNum((TxDescLen + PSPollLength), PageSize);

		TotalPageNum += CurtPktPageNum;

		BufIndex += (CurtPktPageNum * PageSize);
		RSVD_PAGE_CFG("PSPoll", CurtPktPageNum, TotalPageNum, TotalPacketLen);
	}

#ifdef CONFIG_MCC_MODE
	/*======== MCC * n page ======== */
	if (MCC_EN(adapter)) {/*Normal mode*/
		dl_mcc_page = rtw_hal_dl_mcc_fw_rsvd_page(adapter, ReservedPagePacket,
				&BufIndex, TxDescLen, PageSize, &TotalPageNum, &RsvdPageLoc, page_num);
	} else {
		dl_mcc_page = _FAIL;
	}

	if (dl_mcc_page == _FAIL)
#endif /* CONFIG_MCC_MODE */
	{	/*======== null data * 1 page ======== */
		if (MLME_IS_STA(sta_iface) || (nr_assoc_if == 0)) {
			RsvdPageLoc.LocNullData = TotalPageNum;
			RTW_INFO("LocNullData: %d\n", RsvdPageLoc.LocNullData);
			rtw_hal_construct_NullFunctionData(
				sta_iface,
				&ReservedPagePacket[BufIndex],
				&NullDataLength,
				_FALSE, 0, 0, _FALSE);
			rtw_hal_fill_fake_txdesc(sta_iface,
				 &ReservedPagePacket[BufIndex - TxDescLen],
				 NullDataLength, _FALSE, _FALSE, _FALSE);

			CurtPktPageNum = (u8)PageNum(TxDescLen + NullDataLength, PageSize);

			TotalPageNum += CurtPktPageNum;

			BufIndex += (CurtPktPageNum * PageSize);
			RSVD_PAGE_CFG("NullData", CurtPktPageNum, TotalPageNum, TotalPacketLen);
		}
	}

	/*======== Qos null data * 1 page ======== */
	if (pwrctl->wowlan_mode == _FALSE ||
		pwrctl->wowlan_in_resume == _TRUE) {/*Normal mode*/	
		if (MLME_IS_STA(sta_iface) || (nr_assoc_if == 0)) {
			RsvdPageLoc.LocQosNull = TotalPageNum;
			RTW_INFO("LocQosNull: %d\n", RsvdPageLoc.LocQosNull);
			rtw_hal_construct_NullFunctionData(sta_iface,
						&ReservedPagePacket[BufIndex],
						&QosNullLength,
						_TRUE, 0, 0, _FALSE);
			rtw_hal_fill_fake_txdesc(sta_iface,
					 &ReservedPagePacket[BufIndex - TxDescLen],
					 QosNullLength, _FALSE, _FALSE, _FALSE);

			CurtPktPageNum = (u8)PageNum(TxDescLen + QosNullLength,
						     PageSize);

			TotalPageNum += CurtPktPageNum;

			BufIndex += (CurtPktPageNum * PageSize);
			RSVD_PAGE_CFG("QosNull", CurtPktPageNum, TotalPageNum, TotalPacketLen);
		}
	}

#ifdef CONFIG_BT_COEXIST
	/*======== BT Qos null data * 1 page ======== */
	if (pwrctl->wowlan_mode == _FALSE ||
		pwrctl->wowlan_in_resume == _TRUE) {/*Normal mode*/

		ap_iface = adapter;
		#ifdef CONFIG_CONCURRENT_MODE
		if (!MLME_IS_AP(ap_iface) && DEV_AP_NUM(adapter_to_dvobj(ap_iface))) {	/*DEV_AP_STARTING_NUM*/
			ap_iface = _rtw_search_ap_iface(adapter);
			RTW_INFO("get ("ADPT_FMT") to create BTQoSNull\n", ADPT_ARG(ap_iface));
		}
		#endif

		if (MLME_IS_AP(ap_iface) || (nr_assoc_if == 0)) {
			RsvdPageLoc.LocBTQosNull = TotalPageNum;

			RTW_INFO("LocBTQosNull: %d\n", RsvdPageLoc.LocBTQosNull);

			rtw_hal_construct_NullFunctionData(ap_iface,
						&ReservedPagePacket[BufIndex],
						&BTQosNullLength,
						_TRUE, 0, 0, _FALSE);

			rtw_hal_fill_fake_txdesc(ap_iface,
					&ReservedPagePacket[BufIndex - TxDescLen],
					BTQosNullLength, _FALSE, _TRUE, _FALSE);

			CurtPktPageNum = (u8)PageNum(TxDescLen + BTQosNullLength,
							 PageSize);

			TotalPageNum += CurtPktPageNum;
			BufIndex += (CurtPktPageNum * PageSize);

			RSVD_PAGE_CFG("BTQosNull", CurtPktPageNum, TotalPageNum, TotalPacketLen);
		}
	}
#endif /* CONFIG_BT_COEXIT */

	TotalPacketLen = BufIndex;

#ifdef DBG_FW_DEBUG_MSG_PKT
		/*======== FW DEBUG MSG * n page ======== */
		RsvdPageLoc.loc_fw_dbg_msg_pkt = TotalPageNum;
		RTW_INFO("loc_fw_dbg_msg_pkt: %d\n", RsvdPageLoc.loc_fw_dbg_msg_pkt);
		rtw_hal_construct_fw_dbg_msg_pkt(
			adapter,
			&ReservedPagePacket[BufIndex],
			&fw_dbg_msg_pkt_len);

		rtw_hal_fill_fake_txdesc(adapter,
				 &ReservedPagePacket[BufIndex - TxDescLen],
				 fw_dbg_msg_pkt_len, _FALSE, _FALSE, _FALSE);

		CurtPktPageNum = (u8)PageNum(TxDescLen + fw_dbg_msg_pkt_len, PageSize);

		TotalPageNum += CurtPktPageNum;

		TotalPacketLen = BufIndex + fw_dbg_msg_pkt_len;
		BufIndex += (CurtPktPageNum * PageSize);
#endif /*DBG_FW_DEBUG_MSG_PKT*/

#ifdef CONFIG_LPS_PG
	rtw_lps_pg_set_rsvd_page(adapter, ReservedPagePacket, &BufIndex
		, TxDescLen, PageSize, &TotalPageNum, is_wow_mode, page_num ? 1 : 0);
	TotalPacketLen = BufIndex;
#endif

#ifdef CONFIG_WOWLAN
	/*======== WOW * n page ======== */
	if (pwrctl->wowlan_mode == _TRUE &&
		pwrctl->wowlan_in_resume == _FALSE &&
		!(registry_par->suspend_type == FW_IPS_DISABLE_BBRF && !check_fwstate(pmlmepriv, _FW_LINKED))) {/*WOW mode*/
		rtw_hal_set_wow_fw_rsvd_page(adapter, ReservedPagePacket,
					     BufIndex, TxDescLen, PageSize,
			     &TotalPageNum, &TotalPacketLen, &RsvdPageLoc);
	}
#endif /* CONFIG_WOWLAN */

#ifdef CONFIG_P2P_WOWLAN
	/*======== P2P WOW * n page ======== */
	if (_TRUE == pwrctl->wowlan_p2p_mode) {/*WOW mode*/
		rtw_hal_set_p2p_wow_fw_rsvd_page(adapter, ReservedPagePacket,
						 BufIndex, TxDescLen, PageSize,
				 &TotalPageNum, &TotalPacketLen, &RsvdPageLoc);
	}
#endif /* CONFIG_P2P_WOWLAN */

	/*Note:  BufIndex already add a TxDescOffset offset in first Beacon page
	* The "TotalPacketLen" is calculate by BufIndex.
	* We need to decrease TxDescOffset before doing length check. by yiwei
	*/
	TotalPacketLen = TotalPacketLen - TxDescOffset;

download_page:
	if (page_num) {
		*page_num = TotalPageNum;
		rtw_mfree(ReservedPagePacket, MAX_CMDBUF_SZ);
		ReservedPagePacket = NULL;
		RTW_INFO(FUNC_ADPT_FMT" Get [ %s ] RsvdPageNUm <==\n",
			FUNC_ADPT_ARG(adapter), (is_wow_mode) ? "WOW" : "NOR");
		return;
	}

	/* RTW_INFO("%s BufIndex(%d), TxDescLen(%d), PageSize(%d)\n",__func__, BufIndex, TxDescLen, PageSize);*/
	RTW_INFO("%s PageNum(%d), pktlen(%d)\n",
		 __func__, TotalPageNum, TotalPacketLen);

	if (TotalPacketLen > MaxRsvdPageBufSize) {
		RTW_ERR("%s : rsvd page size is not enough!!TotalPacketLen %d, MaxRsvdPageBufSize %d\n",
			 __FUNCTION__, TotalPacketLen, MaxRsvdPageBufSize);
		rtw_warn_on(1);
		goto error;
	} else {
		/* update attribute */
		pattrib = &pcmdframe->attrib;
		update_mgntframe_attrib(adapter, pattrib);
		pattrib->qsel = QSLT_BEACON;
		pattrib->pktlen = TotalPacketLen;
		pattrib->last_txcmdsz = TotalPacketLen;
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


	if (check_fwstate(pmlmepriv, _FW_LINKED)
		|| MLME_IS_AP(adapter) || MLME_IS_MESH(adapter)){
		rtw_hal_set_FwRsvdPage_cmd(adapter, &RsvdPageLoc);
#ifdef DBG_FW_DEBUG_MSG_PKT
		rtw_hal_set_fw_dbg_msg_pkt_rsvd_page_cmd(adapter, &RsvdPageLoc);
#endif /*DBG_FW_DEBUG_MSG_PKT*/
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

void rtw_hal_set_fw_rsvd_page(struct _ADAPTER *adapter, bool finished)
{
	if (finished)
		rtw_mi_tx_beacon_hdl(adapter);
	else
		_rtw_hal_set_fw_rsvd_page(adapter, finished, NULL);
}

/**
 * rtw_hal_get_rsvd_page_num() - Get needed reserved page number
 * @adapter:	struct _ADAPTER*
 *
 * Caculate needed reserved page number.
 * In different state would get different number, for example normal mode and
 * WOW mode would need different reserved page size.
 *
 * Return the number of reserved page which driver need.
 */
u8 rtw_hal_get_rsvd_page_num(struct _ADAPTER *adapter)
{
	u8 num = 0;


	_rtw_hal_set_fw_rsvd_page(adapter, _FALSE, &num);

	return num;
}

#ifndef CONFIG_HAS_HW_VAR_BCN_FUNC
static void hw_var_set_bcn_func(_adapter *adapter, u8 enable)
{
	u32 bcn_ctrl_reg;

#ifdef CONFIG_CONCURRENT_MODE
	if (adapter->hw_port == HW_PORT1)
		bcn_ctrl_reg = REG_BCN_CTRL_1;
	else
#endif
		bcn_ctrl_reg = REG_BCN_CTRL;

	if (enable)
		rtw_write8(adapter, bcn_ctrl_reg, (EN_BCN_FUNCTION | EN_TXBCN_RPT));
	else {
		u8 val8;

		val8 = rtw_read8(adapter, bcn_ctrl_reg);
		val8 &= ~(EN_BCN_FUNCTION | EN_TXBCN_RPT);

#ifdef CONFIG_BT_COEXIST
		if (GET_HAL_DATA(adapter)->EEPROMBluetoothCoexist == 1) {
			/* Always enable port0 beacon function for PSTDMA */
			if (REG_BCN_CTRL == bcn_ctrl_reg)
				val8 |= EN_BCN_FUNCTION;
		}
#endif

		rtw_write8(adapter, bcn_ctrl_reg, val8);
	}

#ifdef CONFIG_RTL8192F
	if (IS_HARDWARE_TYPE_8192F(adapter)) {
		u16 val16, val16_ori;

		val16_ori = val16 = rtw_read16(adapter, REG_WLAN_ACT_MASK_CTRL_1);

		#ifdef CONFIG_CONCURRENT_MODE
		if (adapter->hw_port == HW_PORT1) {
			if (enable)
				val16 |= EN_PORT_1_FUNCTION;
			else
				val16 &= ~EN_PORT_1_FUNCTION;
		} else
		#endif
		{
			if (enable)
				val16 |= EN_PORT_0_FUNCTION;
			else
				val16 &= ~EN_PORT_0_FUNCTION;

			#ifdef CONFIG_BT_COEXIST
			if (GET_HAL_DATA(adapter)->EEPROMBluetoothCoexist == 1)
				val16 |= EN_PORT_0_FUNCTION;
			#endif
		}

		if (val16 != val16_ori)
			rtw_write16(adapter, REG_WLAN_ACT_MASK_CTRL_1,  val16);
	}
#endif
}
#endif

#ifndef CONFIG_HAS_HW_VAR_MLME_DISCONNECT
static void hw_var_set_mlme_disconnect(_adapter *adapter)
{
	u8 val8;

	/* reject all data frames */
#ifdef CONFIG_CONCURRENT_MODE
	if (rtw_mi_check_status(adapter, MI_LINKED) == _FALSE)
#endif
		rtw_write16(adapter, REG_RXFLTMAP2, 0x0000);

#ifdef CONFIG_CONCURRENT_MODE
	if (adapter->hw_port == HW_PORT1) {
		/* reset TSF1 */
		rtw_write8(adapter, REG_DUAL_TSF_RST, BIT(1));

		/* disable update TSF1 */
		rtw_iface_disable_tsf_update(adapter);

		if (!IS_HARDWARE_TYPE_8723D(adapter)
			&& !IS_HARDWARE_TYPE_8192F(adapter)
			&& !IS_HARDWARE_TYPE_8710B(adapter)
		) {
			/* disable Port1's beacon function */
			val8 = rtw_read8(adapter, REG_BCN_CTRL_1);
			val8 &= ~EN_BCN_FUNCTION;
			rtw_write8(adapter, REG_BCN_CTRL_1, val8);
		}
	} else
#endif
	{
		/* reset TSF */
		rtw_write8(adapter, REG_DUAL_TSF_RST, BIT(0));

		/* disable update TSF */
		rtw_iface_disable_tsf_update(adapter);
	}
}
#endif

static void hw_var_set_mlme_sitesurvey(_adapter *adapter, u8 enable)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	u16 value_rxfltmap2;

#ifdef DBG_IFACE_STATUS
	DBG_IFACE_STATUS_DUMP(adapter);
#endif

#ifdef CONFIG_FIND_BEST_CHANNEL
	/* Receive all data frames */
	value_rxfltmap2 = 0xFFFF;
#else
	/* not to receive data frame */
	value_rxfltmap2 = 0;
#endif

	if (enable) { /* under sitesurvey */
		/*
		* 1. configure REG_RXFLTMAP2
		* 2. disable TSF update &  buddy TSF update to avoid updating wrong TSF due to clear RCR_CBSSID_BCN
		* 3. config RCR to receive different BSSID BCN or probe rsp
		*/
		rtw_write16(adapter, REG_RXFLTMAP2, value_rxfltmap2);

		rtw_hal_rcr_set_chk_bssid(adapter, MLME_SCAN_ENTER);

		/* Save orignal RRSR setting, only 8812 set RRSR after set ch/bw/band */
		#if defined (CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
		hal_data->RegRRSR = rtw_read32(adapter, REG_RRSR);
		hal_data->RegRRSR &= 0x000FFFFF;
		#endif

		#if defined(CONFIG_BEAMFORMING) && (defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A))
		if (IS_8812_SERIES(hal_data->version_id) || IS_8821_SERIES(hal_data->version_id)) {
			/* set 718[1:0]=2'b00 to avoid BF scan hang */
			hal_data->backup_snd_ptcl_ctrl = rtw_read8(adapter, REG_SND_PTCL_CTRL_8812A);
			rtw_write8(adapter, REG_SND_PTCL_CTRL_8812A, (hal_data->backup_snd_ptcl_ctrl & 0xfc));
		}
		#endif

		if (rtw_mi_get_ap_num(adapter) || rtw_mi_get_mesh_num(adapter))
			StopTxBeacon(adapter);
	} else { /* sitesurvey done */
		/*
		* 1. enable rx data frame
		* 2. config RCR not to receive different BSSID BCN or probe rsp
		* 3. doesn't enable TSF update &  buddy TSF right now to avoid HW conflict
		*	 so, we enable TSF update when rx first BCN after sitesurvey done
		*/
		if (rtw_mi_check_fwstate(adapter, _FW_LINKED | WIFI_AP_STATE | WIFI_MESH_STATE)) {
			/* enable to rx data frame */
			rtw_write16(adapter, REG_RXFLTMAP2, 0xFFFF);
		}

		rtw_hal_rcr_set_chk_bssid(adapter, MLME_SCAN_DONE);

		/* Restore orignal RRSR setting,only 8812 set RRSR after set ch/bw/band */
		#if defined (CONFIG_RTL8812A) || defined(CONFIG_RTL8821A)
			rtw_phydm_set_rrsr(adapter, hal_data->RegRRSR, TRUE);
		#endif

		#if defined(CONFIG_BEAMFORMING) && (defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A))
		if (IS_8812_SERIES(hal_data->version_id) || IS_8821_SERIES(hal_data->version_id)) {
			/* Restore orignal 0x718 setting*/
			rtw_write8(adapter, REG_SND_PTCL_CTRL_8812A, hal_data->backup_snd_ptcl_ctrl);
		}
		#endif

		if (rtw_mi_get_ap_num(adapter) || rtw_mi_get_mesh_num(adapter)) {
			ResumeTxBeacon(adapter);
			rtw_mi_tx_beacon_hdl(adapter);
		}
	}
}

#ifndef CONFIG_HAS_HW_VAR_MLME_JOIN
static void hw_var_set_mlme_join(_adapter *adapter, u8 type)
{
	u8 val8;
	u16 val16;
	u32 val32;
	u8 RetryLimit = RL_VAL_STA;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

#ifdef CONFIG_CONCURRENT_MODE
	if (type == 0) {
		/* prepare to join */
		if (rtw_mi_get_ap_num(adapter) || rtw_mi_get_mesh_num(adapter))
			StopTxBeacon(adapter);

		/* enable to rx data frame.Accept all data frame */
		rtw_write16(adapter, REG_RXFLTMAP2, 0xFFFF);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
			RetryLimit = (hal_data->CustomerID == RT_CID_CCX) ? RL_VAL_AP : RL_VAL_STA;
		else /* Ad-hoc Mode */
			RetryLimit = RL_VAL_AP;

		rtw_iface_enable_tsf_update(adapter);

	} else if (type == 1) {
		/* joinbss_event call back when join res < 0 */
		if (rtw_mi_check_status(adapter, MI_LINKED) == _FALSE)
			rtw_write16(adapter, REG_RXFLTMAP2, 0x00);

		rtw_iface_disable_tsf_update(adapter);

		if (rtw_mi_get_ap_num(adapter) || rtw_mi_get_mesh_num(adapter)) {
			ResumeTxBeacon(adapter);

			/* reset TSF 1/2 after ResumeTxBeacon */
			rtw_write8(adapter, REG_DUAL_TSF_RST, BIT(1) | BIT(0));
		}

	} else if (type == 2) {
		/* sta add event call back */
		if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE)) {
			/* fixed beacon issue for 8191su........... */
			rtw_write8(adapter, 0x542 , 0x02);
			RetryLimit = RL_VAL_AP;
		}

		if (rtw_mi_get_ap_num(adapter) || rtw_mi_get_mesh_num(adapter)) {
			ResumeTxBeacon(adapter);

			/* reset TSF 1/2 after ResumeTxBeacon */
			rtw_write8(adapter, REG_DUAL_TSF_RST, BIT(1) | BIT(0));
		}
	}

	val16 = BIT_SRL(RetryLimit) | BIT_LRL(RetryLimit);
	rtw_write16(adapter, REG_RETRY_LIMIT, val16);
#else /* !CONFIG_CONCURRENT_MODE */
	if (type == 0) { /* prepare to join */
		/* enable to rx data frame.Accept all data frame */
		rtw_write16(adapter, REG_RXFLTMAP2, 0xFFFF);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
			RetryLimit = (hal_data->CustomerID == RT_CID_CCX) ? RL_VAL_AP : RL_VAL_STA;
		else /* Ad-hoc Mode */
			RetryLimit = RL_VAL_AP;

		rtw_iface_enable_tsf_update(adapter);

	} else if (type == 1) { /* joinbss_event call back when join res < 0 */
		rtw_write16(adapter, REG_RXFLTMAP2, 0x00);

		rtw_iface_disable_tsf_update(adapter);

	} else if (type == 2) { /* sta add event call back */
		if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE))
			RetryLimit = RL_VAL_AP;
	}

	val16 = BIT_SRL(RetryLimit) | BIT_LRL(RetryLimit);
	rtw_write16(adapter, REG_RETRY_LIMIT, val16);
#endif /* !CONFIG_CONCURRENT_MODE */
}
#endif

#ifdef CONFIG_TSF_RESET_OFFLOAD
static int rtw_hal_h2c_reset_tsf(_adapter *adapter, u8 reset_port)
{
	u8 buf[2];
	int ret;

	if (reset_port == HW_PORT0) {
		buf[0] = 0x1;
		buf[1] = 0;
	} else {
		buf[0] = 0x0;
		buf[1] = 0x1;
	}

	ret = rtw_hal_fill_h2c_cmd(adapter, H2C_RESET_TSF, 2, buf);

	return ret;
}

int rtw_hal_reset_tsf(_adapter *adapter, u8 reset_port)
{
	u8 reset_cnt_before = 0, reset_cnt_after = 0, loop_cnt = 0;
	u32 reg_reset_tsf_cnt = (reset_port == HW_PORT0) ?
				REG_FW_RESET_TSF_CNT_0 : REG_FW_RESET_TSF_CNT_1;
	int ret;

	/* site survey will cause reset tsf fail */
	rtw_mi_buddy_scan_abort(adapter, _FALSE);
	reset_cnt_after = reset_cnt_before = rtw_read8(adapter, reg_reset_tsf_cnt);
	ret = rtw_hal_h2c_reset_tsf(adapter, reset_port);
	if (ret != _SUCCESS)
		return ret;

	while ((reset_cnt_after == reset_cnt_before) && (loop_cnt < 10)) {
		rtw_msleep_os(100);
		loop_cnt++;
		reset_cnt_after = rtw_read8(adapter, reg_reset_tsf_cnt);
	}

	return (loop_cnt >= 10) ? _FAIL : _SUCCESS;
}
#endif /* CONFIG_TSF_RESET_OFFLOAD */

#ifndef CONFIG_HAS_HW_VAR_CORRECT_TSF
#ifdef CONFIG_HW_P0_TSF_SYNC
#ifdef CONFIG_CONCURRENT_MODE
static void hw_port0_tsf_sync_sel(_adapter *adapter, u8 benable, u8 hw_port, u16 tr_offset)
{
	u8 val8;
	u8 client_id = 0;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);

#ifdef CONFIG_MCC_MODE
	if (MCC_EN(adapter) && (rtw_hal_check_mcc_status(adapter, MCC_STATUS_DOING_MCC))) {
		RTW_INFO("[MCC] do not set HW TSF sync\n");
		return;
	}
#endif
	/* check if port0 is already synced */
	if (benable && dvobj->p0_tsf.sync_port != MAX_HW_PORT && dvobj->p0_tsf.sync_port == hw_port) {
		RTW_WARN(FUNC_ADPT_FMT ": port0 already enable TSF sync(%d)\n",
			FUNC_ADPT_ARG(adapter), dvobj->p0_tsf.sync_port);
		return;
	}

	/* check if port0 already disable sync */
	if (!benable && dvobj->p0_tsf.sync_port == MAX_HW_PORT) {
		RTW_WARN(FUNC_ADPT_FMT ": port0 already disable TSF sync\n", FUNC_ADPT_ARG(adapter));
		return;
	}

	/* check if port0 sync to port0 */
	if (benable && hw_port == HW_PORT0) {
		RTW_ERR(FUNC_ADPT_FMT ": hw_port is port0 under enable\n", FUNC_ADPT_ARG(adapter));
		rtw_warn_on(1);
		return;
	}

	/*0x5B4 [6:4] :SYNC_CLI_SEL - The selector for the CLINT port of sync tsft source for port 0*/
	/*	Bit[5:4] : 0 for clint0, 1 for clint1, 2 for clint2, 3 for clint3.
		Bit6 : 1= enable sync to port 0. 0=disable sync to port 0.*/

	val8 = rtw_read8(adapter, REG_TIMER0_SRC_SEL);

	if (benable) {
		/*Disable Port0's beacon function*/
		rtw_write8(adapter, REG_BCN_CTRL, rtw_read8(adapter, REG_BCN_CTRL) & ~BIT_EN_BCN_FUNCTION);

		/*Reg 0x518[15:0]: TSFTR_SYN_OFFSET*/
		if (tr_offset)
			rtw_write16(adapter, REG_TSFTR_SYN_OFFSET, tr_offset);

		/*reg 0x577[6]=1*/	/*auto sync by tbtt*/
		rtw_write8(adapter, REG_MISC_CTRL, rtw_read8(adapter, REG_MISC_CTRL) | BIT_AUTO_SYNC_BY_TBTT);

		if (HW_PORT1 == hw_port)
			client_id = 0;
		else if (HW_PORT2 == hw_port)
			client_id = 1;
		else if (HW_PORT3 == hw_port)
			client_id = 2;
		else if (HW_PORT4 == hw_port)
			client_id = 3;

		val8 &= 0x8F;
		val8 |= (BIT(6) | (client_id << 4));

		dvobj->p0_tsf.sync_port = hw_port;
		dvobj->p0_tsf.offset = tr_offset;
		rtw_write8(adapter, REG_TIMER0_SRC_SEL, val8);

		/*Enable Port0's beacon function*/
		rtw_write8(adapter, REG_BCN_CTRL, rtw_read8(adapter, REG_BCN_CTRL)  | BIT_EN_BCN_FUNCTION);
		RTW_INFO("%s Port_%d TSF sync to P0, timer offset :%d\n", __func__, hw_port, tr_offset);
	} else {
		val8 &= ~BIT(6);

		dvobj->p0_tsf.sync_port = MAX_HW_PORT;
		dvobj->p0_tsf.offset = 0;
		rtw_write8(adapter, REG_TIMER0_SRC_SEL, val8);
		RTW_INFO("%s P0 TSF sync disable\n", __func__);
	}
}
static _adapter * _search_ld_sta(_adapter *adapter, u8 include_self)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	u8 i;
	_adapter *iface = NULL;

	if (rtw_mi_get_assoced_sta_num(adapter) == 0) {
		RTW_ERR("STA_LD_NUM == 0\n");
		rtw_warn_on(1);
	}

	for (i = 0; i < dvobj->iface_nums; i++) {
		iface = dvobj->padapters[i];
		if (!iface)
			continue;
		if (include_self == _FALSE && adapter == iface)
			continue;
		if (is_client_associated_to_ap(iface))
			break;
	}
	if (iface)
		RTW_INFO("search STA iface -"ADPT_FMT"\n", ADPT_ARG(iface));
	return iface;
}
#endif /*CONFIG_CONCURRENT_MODE*/
/*Correct port0's TSF*/
/*#define DBG_P0_TSF_SYNC*/
void hw_var_set_correct_tsf(PADAPTER adapter, u8 mlme_state)
{
#ifdef CONFIG_CONCURRENT_MODE
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	_adapter *sta_if = NULL;
	u8 hw_port;

	RTW_INFO(FUNC_ADPT_FMT "\n", FUNC_ADPT_ARG(adapter));
	#ifdef DBG_P0_TSF_SYNC
	RTW_INFO("[TSF_SYNC] AP_NUM = %d\n", rtw_mi_get_ap_num(adapter));
	RTW_INFO("[TSF_SYNC] MESH_NUM = %d\n", rtw_mi_get_mesh_num(adapter));
	RTW_INFO("[TSF_SYNC] LD_STA_NUM = %d\n", rtw_mi_get_assoced_sta_num(adapter));
	if (dvobj->p0_tsf.sync_port == MAX_HW_PORT)
		RTW_INFO("[TSF_SYNC] org p0 sync port = N/A\n");
	else
		RTW_INFO("[TSF_SYNC] org p0 sync port = %d\n", dvobj->p0_tsf.sync_port);
	RTW_INFO("[TSF_SYNC] timer offset = %d\n", dvobj->p0_tsf.offset);
	#endif
	switch (mlme_state) {
		case MLME_STA_CONNECTED :
			{
				hw_port = rtw_hal_get_port(adapter);

				if (!MLME_IS_STA(adapter)) {
					RTW_ERR("STA CON state,but iface("ADPT_FMT") is not STA\n", ADPT_ARG(adapter));
					rtw_warn_on(1);
				}

				if ((dvobj->p0_tsf.sync_port != MAX_HW_PORT) && (hw_port == HW_PORT0)) {
					RTW_ERR(ADPT_FMT" is STA with P0 connected => DIS P0_TSF_SYNC\n", ADPT_ARG(adapter));
					rtw_warn_on(1);
					hw_port0_tsf_sync_sel(adapter, _FALSE, 0, 0);
				}

				if ((dvobj->p0_tsf.sync_port == MAX_HW_PORT) &&
					(rtw_mi_get_ap_num(adapter) || rtw_mi_get_mesh_num(adapter))) {
					hw_port0_tsf_sync_sel(adapter, _TRUE, hw_port, 50);/*timer offset 50ms*/
					#ifdef DBG_P0_TSF_SYNC
					RTW_INFO("[TSF_SYNC] STA_LINKED => EN P0_TSF_SYNC\n");
					#endif
				}
			}
			break;
		case MLME_STA_DISCONNECTED :
			{
				hw_port = rtw_hal_get_port(adapter);

				if (!MLME_IS_STA(adapter)) {
					RTW_ERR("STA DIS_CON state,but iface("ADPT_FMT") is not STA\n", ADPT_ARG(adapter));
					rtw_warn_on(1);
				}

				if (dvobj->p0_tsf.sync_port == hw_port) {
					if (rtw_mi_get_assoced_sta_num(adapter) >= 2) {
						/* search next appropriate sta*/
						sta_if = _search_ld_sta(adapter, _FALSE);
						if (sta_if) {
							hw_port = rtw_hal_get_port(sta_if);
							hw_port0_tsf_sync_sel(adapter, _TRUE, hw_port, 50);/*timer offset 50ms*/
							#ifdef DBG_P0_TSF_SYNC
							RTW_INFO("[TSF_SYNC] STA_DIS_CON => CHANGE P0_TSF_SYNC\n");
							#endif
						}
					} else if (rtw_mi_get_assoced_sta_num(adapter) == 1) {
						hw_port0_tsf_sync_sel(adapter, _FALSE, 0, 0);
						#ifdef DBG_P0_TSF_SYNC
						RTW_INFO("[TSF_SYNC] STA_DIS_CON => DIS P0_TSF_SYNC\n");
						#endif
					}
				}
			}
			break;
		case MLME_AP_STARTED :
		case MLME_MESH_STARTED :
			{
				if (!(MLME_IS_AP(adapter) || MLME_IS_MESH(adapter))) {
					RTW_ERR("AP START state,but iface("ADPT_FMT") is not AP\n", ADPT_ARG(adapter));
					rtw_warn_on(1);
				}

				if ((dvobj->p0_tsf.sync_port == MAX_HW_PORT) &&
					rtw_mi_get_assoced_sta_num(adapter)) {
					/* get port of sta */
					sta_if = _search_ld_sta(adapter, _FALSE);
					if (sta_if) {
						hw_port = rtw_hal_get_port(sta_if);
						hw_port0_tsf_sync_sel(adapter, _TRUE, hw_port, 50);/*timer offset 50ms*/
						#ifdef DBG_P0_TSF_SYNC
						RTW_INFO("[TSF_SYNC] AP_START => EN P0_TSF_SYNC\n");
						#endif
					}
				}
			}
			break;
		case MLME_AP_STOPPED :
		case MLME_MESH_STOPPED :
			{
				if (!(MLME_IS_AP(adapter) || MLME_IS_MESH(adapter))) {
					RTW_ERR("AP START state,but iface("ADPT_FMT") is not AP\n", ADPT_ARG(adapter));
					rtw_warn_on(1);
				}
				/*stop ap mode*/
				if ((rtw_mi_get_ap_num(adapter) + rtw_mi_get_mesh_num(adapter) == 1) &&
					(dvobj->p0_tsf.sync_port != MAX_HW_PORT)) {
					hw_port0_tsf_sync_sel(adapter, _FALSE, 0, 0);
					#ifdef DBG_P0_TSF_SYNC
					RTW_INFO("[TSF_SYNC] AP_STOP => DIS P0_TSF_SYNC\n");
					#endif
				}
			}
			break;
		default :
			RTW_ERR(FUNC_ADPT_FMT" unknow state(0x%02x)\n", FUNC_ADPT_ARG(adapter), mlme_state);
			break;
	}

	/*#ifdef DBG_P0_TSF_SYNC*/
	#if 1
	if (dvobj->p0_tsf.sync_port == MAX_HW_PORT)
		RTW_INFO("[TSF_SYNC] p0 sync port = N/A\n");
	else
		RTW_INFO("[TSF_SYNC] p0 sync port = %d\n", dvobj->p0_tsf.sync_port);
	RTW_INFO("[TSF_SYNC] timer offset = %d\n", dvobj->p0_tsf.offset);
	#endif
#endif /*CONFIG_CONCURRENT_MODE*/
}

#else /*! CONFIG_HW_P0_TSF_SYNC*/

#ifdef CONFIG_MI_WITH_MBSSID_CAM
static void hw_var_set_correct_tsf(_adapter *adapter, u8 mlme_state)
{
	/*do nothing*/
}
#else /* !CONFIG_MI_WITH_MBSSID_CAM*/
static void rtw_hal_correct_tsf(_adapter *padapter, u8 hw_port, u64 tsf)
{
	if (hw_port == HW_PORT0) {
		/*disable related TSF function*/
		rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL) & (~EN_BCN_FUNCTION));
#if defined(CONFIG_RTL8192F)
		rtw_write16(padapter, REG_WLAN_ACT_MASK_CTRL_1, rtw_read16(padapter,
					REG_WLAN_ACT_MASK_CTRL_1) & ~EN_PORT_0_FUNCTION);
#endif

		rtw_write32(padapter, REG_TSFTR, tsf);
		rtw_write32(padapter, REG_TSFTR + 4, tsf >> 32);

		/*enable related TSF function*/
		rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL) | EN_BCN_FUNCTION);
#if defined(CONFIG_RTL8192F)
		rtw_write16(padapter, REG_WLAN_ACT_MASK_CTRL_1, rtw_read16(padapter,
					REG_WLAN_ACT_MASK_CTRL_1) | EN_PORT_0_FUNCTION);
#endif
	} else if (hw_port == HW_PORT1) {
		/*disable related TSF function*/
		rtw_write8(padapter, REG_BCN_CTRL_1, rtw_read8(padapter, REG_BCN_CTRL_1) & (~EN_BCN_FUNCTION));
#if defined(CONFIG_RTL8192F)
		rtw_write16(padapter, REG_WLAN_ACT_MASK_CTRL_1, rtw_read16(padapter,
					REG_WLAN_ACT_MASK_CTRL_1) & ~EN_PORT_1_FUNCTION);
#endif

		rtw_write32(padapter, REG_TSFTR1, tsf);
		rtw_write32(padapter, REG_TSFTR1 + 4, tsf >> 32);

		/*enable related TSF function*/
		rtw_write8(padapter, REG_BCN_CTRL_1, rtw_read8(padapter, REG_BCN_CTRL_1) | EN_BCN_FUNCTION);
#if defined(CONFIG_RTL8192F)
		rtw_write16(padapter, REG_WLAN_ACT_MASK_CTRL_1, rtw_read16(padapter,
					REG_WLAN_ACT_MASK_CTRL_1) | EN_PORT_1_FUNCTION);
#endif
	} else
		RTW_INFO("%s-[WARN] "ADPT_FMT" invalid hw_port:%d\n", __func__, ADPT_ARG(padapter), hw_port);
}
static void hw_var_set_correct_tsf(_adapter *adapter, u8 mlme_state)
{
	u64 tsf;
	struct mlme_ext_priv *mlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *mlmeinfo = &(mlmeext->mlmext_info);

	tsf = mlmeext->TSFValue - rtw_modular64(mlmeext->TSFValue, (mlmeinfo->bcn_interval * 1024)) - 1024; /*us*/

	if ((mlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE
		|| (mlmeinfo->state & 0x03) == WIFI_FW_AP_STATE)
		StopTxBeacon(adapter);

	rtw_hal_correct_tsf(adapter, adapter->hw_port, tsf);

#ifdef CONFIG_CONCURRENT_MODE
	/* Update buddy port's TSF if it is SoftAP/Mesh for beacon TX issue! */
	if ((mlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE
		&& (rtw_mi_get_ap_num(adapter) || rtw_mi_get_mesh_num(adapter))
	) {
		struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
		int i;
		_adapter *iface;

		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (!iface)
				continue;
			if (iface == adapter)
				continue;

			if ((MLME_IS_AP(iface) || MLME_IS_MESH(iface))
				&& check_fwstate(&iface->mlmepriv, WIFI_ASOC_STATE) == _TRUE
			) {
				rtw_hal_correct_tsf(iface, iface->hw_port, tsf);
				#ifdef CONFIG_TSF_RESET_OFFLOAD
				if (rtw_hal_reset_tsf(iface, iface->hw_port) == _FAIL)
					RTW_INFO("%s-[ERROR] "ADPT_FMT" Reset port%d TSF fail\n"
						, __func__, ADPT_ARG(iface), iface->hw_port);
				#endif	/* CONFIG_TSF_RESET_OFFLOAD*/
			}
		}
	}
#endif /* CONFIG_CONCURRENT_MODE */
	if ((mlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE
		|| (mlmeinfo->state & 0x03) == WIFI_FW_AP_STATE)
		ResumeTxBeacon(adapter);
}
#endif /*#ifdef CONFIG_MI_WITH_MBSSID_CAM*/
#endif /*#ifdef CONFIG_HW_P0_TSF_SYNC*/
#endif /*#ifndef CONFIG_HAS_HW_VAR_CORRECT_TSF*/

u64 rtw_hal_get_tsftr_by_port(_adapter *adapter, u8 port)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u64 tsftr = 0;

	if (port >= hal_spec->port_num) {
		RTW_ERR("%s invalid port(%d) \n", __func__, port);
		goto exit;
	}

	switch (rtw_get_chip_type(adapter)) {
#if defined(CONFIG_RTL8814B)
	case RTL8814B:
	{
		u8 val8;

		/* 0x1500[6:4] - BIT_BCN_TIMER_SEL_FWRD_V1 */
		val8 = rtw_read8(adapter, REG_PORT_CTRL_SEL);
		val8 &= ~0x70;
		val8 |= port << 4;
		rtw_write8(adapter, REG_PORT_CTRL_SEL, val8);

		tsftr = rtw_read32(adapter, REG_TSFTR_HIGH);
		tsftr = tsftr << 32;
		tsftr |= rtw_read32(adapter, REG_TSFTR_LOW);

		break;
	}
#endif
#if defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8822C)
	case RTL8814A:
	case RTL8822B:
	case RTL8821C:
	case RTL8822C:		
	{
		u8 val8;

		/* 0x554[30:28] - BIT_BCN_TIMER_SEL_FWRD */
		val8 = rtw_read8(adapter, REG_MBSSID_BCN_SPACE + 3);
		val8 &= 0x8F;
		val8 |= port << 4;
		rtw_write8(adapter, REG_MBSSID_BCN_SPACE + 3, val8);

		tsftr = rtw_read32(adapter, REG_TSFTR + 4);
		tsftr = tsftr << 32;
		tsftr |= rtw_read32(adapter, REG_TSFTR);

		break;
	}
#endif
#if defined(CONFIG_RTL8188E) || defined(CONFIG_RTL8188F) || defined(CONFIG_RTL8188GTV) \
		|| defined(CONFIG_RTL8192E) || defined(CONFIG_RTL8192F) \
		|| defined(CONFIG_RTL8723B) || defined(CONFIG_RTL8703B) || defined(CONFIG_RTL8723D) \
		|| defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A) \
		|| defined(CONFIG_RTL8710B)
	case RTL8188E:
	case RTL8188F:
	case RTL8188GTV:
	case RTL8192E:
	case RTL8192F:
	case RTL8723B:
	case RTL8703B:
	case RTL8723D:
	case RTL8812:
	case RTL8821:
	case RTL8710B:
	{
		u32 addr;

		if (port == HW_PORT0)
			addr = REG_TSFTR;
		else if (port == HW_PORT1)
			addr = REG_TSFTR1;
		else {
			RTW_ERR("%s unknown port(%d) \n", __func__, port);
			goto exit;
		}

		tsftr = rtw_read32(adapter, addr + 4);
		tsftr = tsftr << 32;
		tsftr |= rtw_read32(adapter, addr);

		break;
	}
#endif
	default:
		RTW_ERR("%s unknow chip type\n", __func__);
	}

exit:
	return tsftr;
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

#ifdef CONFIG_WMMPS_STA
void rtw_hal_update_uapsd_tid(_adapter *adapter)
{
	struct mlme_priv		*pmlmepriv = &adapter->mlmepriv;
	struct qos_priv		*pqospriv = &pmlmepriv->qospriv;

	/* write complement of pqospriv->uapsd_tid to mac register 0x693 because 
	    it's designed  for "0" represents "enable" and "1" represents "disable" */
	rtw_write8(adapter, REG_WMMPS_UAPSD_TID, (u8)(~pqospriv->uapsd_tid));
}
#endif /* CONFIG_WMMPS_STA */

#if defined(CONFIG_BT_COEXIST) && defined(CONFIG_FW_MULTI_PORT_SUPPORT)
/* For multi-port support, driver needs to inform the port ID to FW for btc operations */
s32 rtw_hal_set_wifi_btc_port_id_cmd(_adapter *adapter)
{
	u8 h2c_buf[H2C_BTC_WL_PORT_ID_LEN] = {0};
	u8 hw_port = rtw_hal_get_port(adapter);

	SET_H2CCMD_BTC_WL_PORT_ID(h2c_buf, hw_port);
	RTW_INFO("%s ("ADPT_FMT") - hw_port :%d\n", __func__, ADPT_ARG(adapter), hw_port);
	return rtw_hal_fill_h2c_cmd(adapter, H2C_BTC_WL_PORT_ID, H2C_BTC_WL_PORT_ID_LEN, h2c_buf);
}
#endif

#define LPS_ACTIVE_TIMEOUT	50 /*number of times*/
void rtw_lps_state_chk(_adapter *adapter, u8 ps_mode)
{
	struct pwrctrl_priv 		*pwrpriv = adapter_to_pwrctl(adapter);
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_priv		*pstapriv = &adapter->stapriv;
	struct sta_info		*psta = NULL;
	u8 ps_ready = _FALSE;
	s8 leave_wait_count = LPS_ACTIVE_TIMEOUT;

	if (ps_mode == PS_MODE_ACTIVE) {
#ifdef CONFIG_LPS_ACK
		if (rtw_sctx_wait(&pwrpriv->lps_ack_sctx, __func__)) {
			if (pwrpriv->lps_ack_status > 0) {
				psta = rtw_get_stainfo(pstapriv, pmlmeinfo->network.MacAddress);
				if (psta != NULL) {
					if(issue_nulldata(adapter, psta->cmn.mac_addr, PS_MODE_ACTIVE, 3, 1) == _FAIL)
						RTW_INFO(FUNC_ADPT_FMT" LPS state sync not yet finished.\n", FUNC_ADPT_ARG(adapter));
				}
			}
		} else {
			RTW_WARN("LPS sctx query timeout, operation abort!!\n");
			return;
		}
		pwrpriv->lps_ack_status = -1;
#else
		do {
			if ((rtw_read8(adapter, REG_TCR) & BIT_PWRBIT_OW_EN) == 0) {
				ps_ready = _TRUE;
				break;
			}
			rtw_msleep_os(1);
		} while (leave_wait_count--);

		if (ps_ready == _FALSE) {
			RTW_WARN(FUNC_ADPT_FMT" Power Bit Control is still in HW!\n", FUNC_ADPT_ARG(adapter));
			return;
		}
#endif /* CONFIG_LPS_ACK */
	}
}

void rtw_var_set_basic_rate(PADAPTER padapter, u8 *val) {

	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_info *mlmext_info = &padapter->mlmeextpriv.mlmext_info;
	u16 input_b = 0, masked = 0, ioted = 0, BrateCfg = 0;
	u16 rrsr_2g_force_mask = RRSR_CCK_RATES;
	u16 rrsr_2g_allow_mask = (RRSR_24M | RRSR_12M | RRSR_6M | RRSR_CCK_RATES);
	#if CONFIG_IEEE80211_BAND_5GHZ
	u16 rrsr_5g_force_mask = (RRSR_6M);
	u16 rrsr_5g_allow_mask = (RRSR_OFDM_RATES);
	#endif
	u32 temp_RRSR;

	HalSetBrateCfg(padapter, val, &BrateCfg);
	input_b = BrateCfg;

	/* apply force and allow mask */
	#if CONFIG_IEEE80211_BAND_5GHZ
	if (pHalData->current_band_type != BAND_ON_2_4G) {
		BrateCfg |= rrsr_5g_force_mask;
		BrateCfg &= rrsr_5g_allow_mask;
	} else
	#endif
	{ /* 2.4G */
		BrateCfg |= rrsr_2g_force_mask;
		BrateCfg &= rrsr_2g_allow_mask;
	}
	masked = BrateCfg;

#ifdef CONFIG_CMCC_TEST
	BrateCfg |= (RRSR_11M | RRSR_5_5M | RRSR_1M); /* use 11M to send ACK */
	BrateCfg |= (RRSR_24M | RRSR_18M | RRSR_12M); /*CMCC_OFDM_ACK 12/18/24M */
#endif

	/* IOT consideration */
	if (mlmext_info->assoc_AP_vendor == HT_IOT_PEER_CISCO) {
		/* if peer is cisco and didn't use ofdm rate, we enable 6M ack */
		if ((BrateCfg & (RRSR_24M | RRSR_12M | RRSR_6M)) == 0)
			BrateCfg |= RRSR_6M;
	}
		ioted = BrateCfg;

#ifdef CONFIG_NARROWBAND_SUPPORTING
	if ((padapter->registrypriv.rtw_nb_config == RTW_NB_CONFIG_WIDTH_10)
		|| (padapter->registrypriv.rtw_nb_config == RTW_NB_CONFIG_WIDTH_5)) {
		BrateCfg &= ~RRSR_CCK_RATES;
		BrateCfg |= RRSR_6M;
	}
#endif
	pHalData->BasicRateSet = BrateCfg;

	RTW_INFO("HW_VAR_BASIC_RATE: %#x->%#x->%#x\n", input_b, masked, ioted);

	/* Set RRSR rate table. */
	temp_RRSR = rtw_read32(padapter, REG_RRSR);
	temp_RRSR &=0xFFFF0000;
	temp_RRSR |=BrateCfg;
	rtw_phydm_set_rrsr(padapter, temp_RRSR, TRUE);

	rtw_write8(padapter, REG_RRSR + 2, rtw_read8(padapter, REG_RRSR + 2) & 0xf0);

	#if defined(CONFIG_RTL8188E)
	rtw_hal_set_hwreg(padapter, HW_VAR_INIT_RTS_RATE, (u8 *)&BrateCfg);
	#endif
}

u8 SetHwReg(_adapter *adapter, u8 variable, u8 *val)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	u8 ret = _SUCCESS;

	switch (variable) {
	case HW_VAR_MEDIA_STATUS: {
		u8 net_type = *((u8 *)val);

		rtw_hal_set_msr(adapter, net_type);
	}
	break;
	case HW_VAR_DO_IQK:
		if (*val)
			hal_data->bNeedIQK = _TRUE;
		else
			hal_data->bNeedIQK = _FALSE;
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
	case HW_VAR_RCR:
		ret = hw_var_rcr_config(adapter, *((u32 *)val));
		break;
	case HW_VAR_ON_RCR_AM:
		hw_var_set_rcr_am(adapter, 1);
		break;
	case HW_VAR_OFF_RCR_AM:
		hw_var_set_rcr_am(adapter, 0);
		break;
	case HW_VAR_BEACON_INTERVAL:
		hw_var_set_bcn_interval(adapter, *(u16 *)val);
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
			rtw_hal_rcr_add(adapter, RCR_ENMBID);
		else
			rtw_hal_rcr_clear(adapter, RCR_ENMBID);
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

#ifndef CONFIG_HAS_HW_VAR_BCN_FUNC
	case HW_VAR_BCN_FUNC:
		hw_var_set_bcn_func(adapter, *val);
		break;
#endif

#ifndef CONFIG_HAS_HW_VAR_MLME_DISCONNECT
	case HW_VAR_MLME_DISCONNECT:
		hw_var_set_mlme_disconnect(adapter);
		break;
#endif

	case HW_VAR_MLME_SITESURVEY:
		hw_var_set_mlme_sitesurvey(adapter, *val);
		#ifdef CONFIG_BT_COEXIST
		if (hal_data->EEPROMBluetoothCoexist == 1)
			rtw_btcoex_ScanNotify(adapter, *val ? _TRUE : _FALSE);
		#endif
		break;

#ifndef CONFIG_HAS_HW_VAR_MLME_JOIN
	case HW_VAR_MLME_JOIN:
		hw_var_set_mlme_join(adapter, *val);
		break;
#endif

	case HW_VAR_EN_HW_UPDATE_TSF:
		rtw_hal_set_hw_update_tsf(adapter);
		break;
#ifndef CONFIG_HAS_HW_VAR_CORRECT_TSF
	case HW_VAR_CORRECT_TSF:
		hw_var_set_correct_tsf(adapter, *val);
		break;
#endif

#if defined(CONFIG_HW_P0_TSF_SYNC) && defined(CONFIG_CONCURRENT_MODE) && defined(CONFIG_MCC_MODE)
	case HW_VAR_TSF_AUTO_SYNC:
		if (*val == _TRUE)
			hw_port0_tsf_sync_sel(adapter, _TRUE, adapter->hw_port, 50);
		else
			hw_port0_tsf_sync_sel(adapter, _FALSE, adapter->hw_port, 50);
		break;
#endif
	case HW_VAR_APFM_ON_MAC:
		hal_data->bMacPwrCtrlOn = *val;
		RTW_INFO("%s: bMacPwrCtrlOn=%d\n", __func__, hal_data->bMacPwrCtrlOn);
		break;
#ifdef CONFIG_WMMPS_STA
	case  HW_VAR_UAPSD_TID:
		rtw_hal_update_uapsd_tid(adapter);
		break;
#endif /* CONFIG_WMMPS_STA */
#ifdef CONFIG_LPS_PG
	case HW_VAR_LPS_PG_HANDLE:
		rtw_hal_lps_pg_handler(adapter, *val);
		break;
#endif
#ifdef CONFIG_LPS_LCLK_WD_TIMER
	case HW_VAR_DM_IN_LPS_LCLK:
		rtw_phydm_wd_lps_lclk_hdl(adapter);
		break;
#endif
	case HW_VAR_ENABLE_RX_BAR:
		if (*val == _TRUE) {
			/* enable RX BAR */
			u16 val16 = rtw_read16(adapter, REG_RXFLTMAP1);

			val16 |= BIT(8);
			rtw_write16(adapter, REG_RXFLTMAP1, val16);
		} else {
			/* disable RX BAR */
			u16 val16 = rtw_read16(adapter, REG_RXFLTMAP1);

			val16 &= (~BIT(8));
			rtw_write16(adapter, REG_RXFLTMAP1, val16);
		}
		RTW_INFO("[HW_VAR_ENABLE_RX_BAR] 0x%02X=0x%02X\n",
			REG_RXFLTMAP1, rtw_read16(adapter, REG_RXFLTMAP1));
		break;
	case HW_VAR_HCI_SUS_STATE:
		hal_data->hci_sus_state = *(u8 *)val;
		RTW_INFO("%s: hci_sus_state=%u\n", __func__, hal_data->hci_sus_state);
		break;
#if defined(CONFIG_AP_MODE) && defined(CONFIG_FW_HANDLE_TXBCN) && defined(CONFIG_SUPPORT_MULTI_BCN)
		case HW_VAR_BCN_HEAD_SEL:
		{
			u8 vap_id = *(u8 *)val;

			if ((vap_id >= CONFIG_LIMITED_AP_NUM) && (vap_id != 0xFF)) {
				RTW_ERR(ADPT_FMT " vap_id(%d:%d) is invalid\n", ADPT_ARG(adapter),vap_id, adapter->vap_id);
				rtw_warn_on(1);
			}
			if (MLME_IS_AP(adapter) || MLME_IS_MESH(adapter)) {
				u16 drv_pg_bndy = 0, bcn_addr = 0;
				u32 page_size = 0;

				/*rtw_hal_get_def_var(adapter, HAL_DEF_TX_PAGE_BOUNDARY, &drv_pg_bndy);*/
				rtw_halmac_get_rsvd_drv_pg_bndy(adapter_to_dvobj(adapter), &drv_pg_bndy);
				rtw_hal_get_def_var(adapter, HAL_DEF_TX_PAGE_SIZE, (u8 *)&page_size);

				if (vap_id != 0xFF)
					bcn_addr = drv_pg_bndy + (vap_id * (MAX_BEACON_LEN / page_size));
				else
					bcn_addr = drv_pg_bndy;
				RTW_INFO(ADPT_FMT" vap_id(%d) change BCN HEAD to 0x%04x\n",
					ADPT_ARG(adapter), vap_id, bcn_addr);
				rtw_write16(adapter, REG_FIFOPAGE_CTRL_2,
					(bcn_addr & BIT_MASK_BCN_HEAD_1_V1) | BIT_BCN_VALID_V1);
			}
		}
		break;
#endif
	case HW_VAR_LPS_STATE_CHK :
		rtw_lps_state_chk(adapter, *(u8 *)val);
		break;

#ifdef CONFIG_RTS_FULL_BW
	case HW_VAR_SET_RTS_BW:
	{
		#ifdef RTW_HALMAC
			rtw_halmac_set_rts_full_bw(adapter_to_dvobj(adapter), (*val));
		#else
			u8 temp;
			if(*val)
				temp = (( rtw_read8(adapter, REG_INIRTS_RATE_SEL)) | BIT5 );
			else
				temp = (( rtw_read8(adapter, REG_INIRTS_RATE_SEL)) & (~BIT5));
			rtw_write8(adapter, REG_INIRTS_RATE_SEL, temp);
			/*RTW_INFO("HW_VAR_SET_RTS_BW	val=%u REG480=0x%x\n", *val, rtw_read8(adapter, REG_INIRTS_RATE_SEL));*/
		#endif
	}
	break;
#endif/*CONFIG_RTS_FULL_BW*/
#if defined(CONFIG_PCI_HCI)
	case HW_VAR_ENSWBCN: 
	if (*val == _TRUE) {
		rtw_write8(adapter, REG_CR + 1,
			   rtw_read8(adapter, REG_CR + 1) | BIT(0));
	} else
		rtw_write8(adapter, REG_CR + 1,
			   rtw_read8(adapter, REG_CR + 1) & ~BIT(0));
	break;
#endif
	default:
		if (0)
			RTW_PRINT(FUNC_ADPT_FMT" variable(%d) not defined!\n",
				  FUNC_ADPT_ARG(adapter), variable);
		ret = _FAIL;
		break;
	}

	return ret;
}

void GetHwReg(_adapter *adapter, u8 variable, u8 *val)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	u64 val64;


	switch (variable) {
	case HW_VAR_MAC_ADDR:
		rtw_hal_get_macaddr_port(adapter, val);
		break;
	case HW_VAR_BASIC_RATE:
		*((u16 *)val) = hal_data->BasicRateSet;
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
	case HW_VAR_RCR:
		hw_var_rcr_get(adapter, (u32 *)val);
		break;
	case HW_VAR_FWLPS_RF_ON:
		/* When we halt NIC, we should check if FW LPS is leave. */
		if (rtw_is_surprise_removed(adapter)
			|| (adapter_to_pwrctl(adapter)->rf_pwrstate == rf_off)
		) {
			/*
			 * If it is in HW/SW Radio OFF or IPS state,
			 * we do not check Fw LPS Leave,
			 * because Fw is unload.
			 */
			*val = _TRUE;
		} else {
			u32 rcr = 0;

			rtw_hal_get_hwreg(adapter, HW_VAR_RCR, (u8 *)&rcr);
			if (rcr & (RCR_UC_MD_EN | RCR_BC_MD_EN | RCR_TIM_PARSER_EN))
				*val = _FALSE;
			else
				*val = _TRUE;
		}
		break;

	case HW_VAR_HCI_SUS_STATE:
		*((u8 *)val) = hal_data->hci_sus_state;
		break;

#ifndef CONFIG_HAS_HW_VAR_BCN_CTRL_ADDR
	case HW_VAR_BCN_CTRL_ADDR:
		*((u32 *)val) = hw_bcn_ctrl_addr(adapter, adapter->hw_port);
		break;
#endif

#ifdef CONFIG_WAPI_SUPPORT
	case HW_VAR_CAM_EMPTY_ENTRY: {
		u8	ucIndex = *((u8 *)val);
		u8	i;
		u32	ulCommand = 0;
		u32	ulContent = 0;
		u32	ulEncAlgo = CAM_AES;

		for (i = 0; i < CAM_CONTENT_COUNT; i++) {
			/* filled id in CAM config 2 byte */
			if (i == 0)
				ulContent |= (ucIndex & 0x03) | ((u16)(ulEncAlgo) << 2);
			else
				ulContent = 0;
			/* polling bit, and No Write enable, and address */
			ulCommand = CAM_CONTENT_COUNT * ucIndex + i;
			ulCommand = ulCommand | CAM_POLLINIG | CAM_WRITE;
			/* write content 0 is equall to mark invalid */
			rtw_write32(adapter, REG_CAMWRITE, ulContent);  /* delay_ms(40); */
			rtw_write32(adapter, REG_CAMCMD, ulCommand);  /* delay_ms(40); */
		}
	}
#endif

	default:
		if (0)
			RTW_PRINT(FUNC_ADPT_FMT" variable(%d) not defined!\n",
				  FUNC_ADPT_ARG(adapter), variable);
		break;
	}

}

static u32 _get_page_size(struct _ADAPTER *a)
{
#ifdef RTW_HALMAC
	struct dvobj_priv *d;
	u32 size = 0;
	int err = 0;


	d = adapter_to_dvobj(a);

	err = rtw_halmac_get_page_size(d, &size);
	if (!err)
		return size;

	RTW_WARN(FUNC_ADPT_FMT ": Fail to get Page size!!(err=%d)\n",
		 FUNC_ADPT_ARG(a), err);
#endif /* RTW_HALMAC */

	return PAGE_SIZE_128;
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
#ifdef RTW_BEAMFORMING_VERSION_2
void rtw_hal_beamforming_config_csirate(PADAPTER adapter)
{
	struct dm_struct *p_dm_odm;
	struct beamforming_info *bf_info;
	u8 fix_rate_enable = 0;
	u8 new_csi_rate_idx;
	u8 rrsr_54_en;
	u32 temp_rrsr;

	/* Acting as BFee */
	if (IS_BEAMFORMEE(adapter)) {
	#if 0
		/* Do not enable now because it will affect MU performance and CTS/BA rate. 2016.07.19. by tynli. [PCIE-1660] */
		if (IS_HARDWARE_TYPE_8821C(Adapter))
			FixRateEnable = 1;	/* Support after 8821C */
	#endif

		p_dm_odm = adapter_to_phydm(adapter);
		bf_info = GET_BEAMFORM_INFO(adapter);

		rtw_halmac_bf_cfg_csi_rate(adapter_to_dvobj(adapter),
				p_dm_odm->rssi_min,
				bf_info->cur_csi_rpt_rate,
				fix_rate_enable, &new_csi_rate_idx, &rrsr_54_en);

		temp_rrsr = rtw_read32(adapter, REG_RRSR);
		if (rrsr_54_en == 1)
			temp_rrsr |= RRSR_54M;
		else if (rrsr_54_en == 0)
			temp_rrsr &= ~RRSR_54M;
		rtw_phydm_set_rrsr(adapter, temp_rrsr, FALSE);

		if (new_csi_rate_idx != bf_info->cur_csi_rpt_rate)
			bf_info->cur_csi_rpt_rate = new_csi_rate_idx;
	}
}
#endif
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
			*((int *)value) = psta->cmn.rssi_stat.rssi;
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
	case HAL_DEF_TX_PAGE_SIZE:
		*((u32 *)value) = _get_page_size(adapter);
		break;
	case HAL_DEF_TX_STBC:
		#ifdef CONFIG_ALPHA_SMART_ANTENNA 
		*(u8 *)value = 0;
		#else
		*(u8 *)value = hal_data->max_tx_cnt > 1 ? 1 : 0;
		#endif
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
			char		chTmp
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
		char			*szStr,
		u32			*pu4bVal,
		u32			*pu4bMove
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
		char			*szStr,
		u8				*pInteger,
		u8				*pFraction,
		u32			*pu4bMove
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

	if (*szScan < '0' || *szScan > '9')
		return _FALSE;

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

			*pFraction += (*szScan - '0') * 10;
			++szScan;
			++(*pu4bMove);

			if (*szScan >= '0' && *szScan <= '9') {
				*pFraction += *szScan - '0';
				++szScan;
				++(*pu4bMove);
			}
			return _TRUE;
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
		char			*szStr
)
{
	if (*szStr == '/' && *(szStr + 1) == '/')
		return _TRUE;
	else
		return _FALSE;
}

BOOLEAN
GetU1ByteIntegerFromStringInDecimal(
		char	*Str,
		u8		*pInt
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
			char	*In,
			u32	*Start,
			char	*Out,
			char		LeftQualifier,
			char		RightQualifier
)
{
	u32	i = 0, j = 0;
	char	c = In[(*Start)++];

	if (c != LeftQualifier)
		return _FALSE;

	i = (*Start);
	c = In[(*Start)++];
	while (c != RightQualifier && c != '\0')
		c = In[(*Start)++];

	if (c == '\0')
		return _FALSE;

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
		    IS_8188GTV(pHalData->version_id) ||
		    IS_8812_SERIES(pHalData->version_id) ||
		    IS_8821_SERIES(pHalData->version_id) ||
		    IS_8723B_SERIES(pHalData->version_id) ||
		    IS_8192E(pHalData->version_id) ||
		    IS_8703B_SERIES(pHalData->version_id) ||
		    IS_8723D_SERIES(pHalData->version_id) ||
		    IS_8192F_SERIES(pHalData->version_id)) {
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
#define DBG_RX_DFRAME_RAW_DATA_UC		0
#define DBG_RX_DFRAME_RAW_DATA_BMC		1
#define DBG_RX_DFRAME_RAW_DATA_TYPES	2

	_irqL irqL;
	u8 isCCKrate, rf_path;
	struct recv_priv *precvpriv = &(padapter->recvpriv);
	PHAL_DATA_TYPE	pHalData =  GET_HAL_DATA(padapter);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta;
	struct sta_recv_dframe_info *psta_dframe_info;
	int i, j;
	_list	*plist, *phead;
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u8 null_addr[ETH_ALEN] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	if (precvpriv->store_law_data_flag) {

		_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);

		for (i = 0; i < NUM_STA; i++) {
			phead = &(pstapriv->sta_hash[i]);
			plist = get_next(phead);

			while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {

				psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);
				plist = get_next(plist);

				if (psta) {
					if ((_rtw_memcmp(psta->cmn.mac_addr, bc_addr, ETH_ALEN)  !=   _TRUE)
					    && (_rtw_memcmp(psta->cmn.mac_addr, null_addr, ETH_ALEN)  !=  _TRUE)
					    && (_rtw_memcmp(psta->cmn.mac_addr, adapter_mac_addr(padapter), ETH_ALEN)  !=  _TRUE)) {

						RTW_PRINT_SEL(sel, "==============================\n");
						RTW_PRINT_SEL(sel, "macaddr =" MAC_FMT "\n", MAC_ARG(psta->cmn.mac_addr));

						for (j = 0; j < DBG_RX_DFRAME_RAW_DATA_TYPES; j++) {
							if (j == DBG_RX_DFRAME_RAW_DATA_UC) {
								psta_dframe_info = &psta->sta_dframe_info;
								RTW_PRINT_SEL(sel, "\n");
								RTW_PRINT_SEL(sel, "Unicast:\n");
							} else if (j == DBG_RX_DFRAME_RAW_DATA_BMC) {
								psta_dframe_info = &psta->sta_dframe_info_bmc;
								RTW_PRINT_SEL(sel, "\n");
								RTW_PRINT_SEL(sel, "Broadcast/Multicast:\n");
							}

							isCCKrate = (psta_dframe_info->sta_data_rate <= DESC_RATE11M) ? TRUE : FALSE;

							RTW_PRINT_SEL(sel, "BW=%s, sgi =%d\n", ch_width_str(psta_dframe_info->sta_bw_mode), psta_dframe_info->sta_sgi);
							RTW_PRINT_SEL(sel, "Rx_Data_Rate = %s\n", HDATA_RATE(psta_dframe_info->sta_data_rate));

							for (rf_path = 0; rf_path < pHalData->NumTotalRFPath; rf_path++) {
								if (!isCCKrate) {
									RTW_PRINT_SEL(sel , "RF_PATH_%d RSSI:%d(dBm)", rf_path, psta_dframe_info->sta_RxPwr[rf_path]);
									_RTW_PRINT_SEL(sel , ",rx_ofdm_snr:%d(dB)\n", psta_dframe_info->sta_ofdm_snr[rf_path]);
								} else
									RTW_PRINT_SEL(sel , "RF_PATH_%d RSSI:%d(dBm)\n", rf_path, (psta_dframe_info->sta_mimo_signal_strength[rf_path]) - 100);
							}
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
	struct phydm_phyinfo_struct *p_phy_info = &pattrib->phy_info;
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
				if (IS_MCAST(get_ra(get_recvframe_data(prframe))))
					psta_dframe_info = &psta->sta_dframe_info_bmc;
				else
					psta_dframe_info = &psta->sta_dframe_info;
				/*RTW_INFO("=>%s psta->cmn.mac_addr="MAC_FMT" !\n",
					__FUNCTION__, MAC_ARG(psta->cmn.mac_addr));*/
				if ((_rtw_memcmp(psta->cmn.mac_addr, bc_addr, ETH_ALEN) != _TRUE) || (padapter->registrypriv.mp_mode == 1)) {
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
#ifdef CONFIG_RTL8188GTV
	case RTL8188GTV:
		if (interface_type == RTW_USB)
			addr_offset = EEPROM_MAC_ADDR_8188GTVU;
		else if (interface_type == RTW_SDIO)
			addr_offset = EEPROM_MAC_ADDR_8188GTVS;
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

#ifdef CONFIG_RTL8710B
	case RTL8710B:
		if (interface_type == RTW_USB)
			addr_offset = EEPROM_MAC_ADDR_8710B;
		break;
#endif

#ifdef CONFIG_RTL8192F
	case RTL8192F:
		if (interface_type == RTW_USB)
			addr_offset = EEPROM_MAC_ADDR_8192FU;
		else if (interface_type == RTW_SDIO)
			addr_offset = EEPROM_MAC_ADDR_8192FS;
		else if (interface_type == RTW_PCIE)
			addr_offset = EEPROM_MAC_ADDR_8192FE;
		break;
#endif /* CONFIG_RTL8192F */

#ifdef CONFIG_RTL8822C
	case RTL8822C:
		if (interface_type == RTW_USB)
			addr_offset = EEPROM_MAC_ADDR_8822CU;
		else if (interface_type == RTW_SDIO)
			addr_offset = EEPROM_MAC_ADDR_8822CS;
		else if (interface_type == RTW_PCIE)
			addr_offset = EEPROM_MAC_ADDR_8822CE;
		break;
#endif /* CONFIG_RTL8822C */

#ifdef CONFIG_RTL8814B
	case RTL8814B:
		if (interface_type == RTW_USB)
			addr_offset = EEPROM_MAC_ADDR_8814BU;
		else if (interface_type == RTW_PCIE)
			addr_offset = EEPROM_MAC_ADDR_8814BE;
		break;
#endif /* CONFIG_RTL8814B */
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
	int mapsize =0;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(padapter);

	EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_EFUSE_MAP_LEN , (void *)&mapsize, _FALSE);

	if (mapsize <= 0 || mapsize > EEPROM_MAX_SIZE) {
		RTW_ERR("wrong map size %d\n", mapsize);
		return;
	}

#ifdef CONFIG_RTW_DEBUG
	if (hal_data->efuse_file_status == EFUSE_FILE_LOADED)
		RTW_MAP_DUMP_SEL(RTW_DBGDUMP, "EFUSE FILE", hal_data->efuse_eeprom_data, mapsize);
	else
		RTW_MAP_DUMP_SEL(RTW_DBGDUMP, "HW EFUSE", hal_data->efuse_eeprom_data, mapsize);
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
#if defined(CONFIG_RTL8822B) && defined(CONFIG_USB_HCI)
	u8 ft_mac_addr[ETH_ALEN] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff}; /* FT USB2 for 8822B */
#endif

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

#if defined(CONFIG_RTL8822B) && defined(CONFIG_USB_HCI)
	if (_rtw_memcmp(hw_addr, ft_mac_addr, ETH_ALEN))
		hw_addr[0] = 0xff;
#endif

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
	u32		ArrayLen	= sizeof(Array_kfreemap) / sizeof(u32);
	u32		*Array = Array_kfreemap;
	u32		v1 = 0, v2 = 0, GainValue = 0, target = 0;

	if (registry_par->RegPwrTrimEnable == 2) {
		RTW_INFO("Registry kfree default force disable.\n");
		return;
	}

#if defined(CONFIG_RTL8723B)
	if (value & BIT4 && (registry_par->RegPwrTrimEnable == 1)) {
		RTW_INFO("Offset RF Gain.\n");
		RTW_INFO("Offset RF Gain.  pHalData->EEPROMRFGainVal=0x%x\n", pHalData->EEPROMRFGainVal);

		if (pHalData->EEPROMRFGainVal != 0xff) {

			if (pHalData->ant_path == RF_PATH_A)
				GainValue = (pHalData->EEPROMRFGainVal & 0x0f);

			else
				GainValue = (pHalData->EEPROMRFGainVal & 0xf0) >> 4;
			RTW_INFO("Ant PATH_%d GainValue Offset = 0x%x\n", (pHalData->ant_path == RF_PATH_A) ? (RF_PATH_A) : (RF_PATH_B), GainValue);

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
	if (value & BIT4 && (registry_par->RegPwrTrimEnable == 1)) {
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
	struct registry_priv *registry_par = &padapter->registrypriv;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8 cur_wireless_mode = WIRELESS_INVALID;

#ifdef CONFIG_USB_RX_AGGREGATION
	if (!registry_par->dynamic_agg_enable)
		return;

#ifdef RTW_HALMAC
	if (IS_HARDWARE_TYPE_8822BU(padapter) || IS_HARDWARE_TYPE_8821CU(padapter) 
		|| IS_HARDWARE_TYPE_8822CU(padapter) || IS_HARDWARE_TYPE_8814BU(padapter))
		rtw_hal_set_hwreg(padapter, HW_VAR_RXDMA_AGG_PG_TH, NULL);
#else /* !RTW_HALMAC */
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
#endif /* RTW_HALMAC */
#endif /* CONFIG_USB_RX_AGGREGATION */

}

/* bus-agg check for SoftAP mode */
inline u8 rtw_hal_busagg_qsel_check(_adapter *padapter, u8 pre_qsel, u8 next_qsel)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	u8 chk_rst = _SUCCESS;

	if (!MLME_IS_AP(padapter) && !MLME_IS_MESH(padapter))
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
	u32 gpio_pin_input_val = REG_GPIO_PIN_CTRL;
	u32 gpio_pin_output_val = REG_GPIO_PIN_CTRL + 1;
	u32 gpio_pin_output_en = REG_GPIO_PIN_CTRL + 2;
	u8 gpio_num_to_set = gpio_num;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);

	if (rtw_hal_gpio_func_check(adapter, gpio_num) == _FAIL)
		return value;

	rtw_ps_deny(adapter, PS_DENY_IOCTL);

	RTW_INFO("rf_pwrstate=0x%02x\n", pwrpriv->rf_pwrstate);
	LeaveAllPowerSaveModeDirect(adapter);

	if (gpio_num > 7) {
		gpio_pin_input_val = REG_GPIO_PIN_CTRL_2;
		gpio_pin_output_val = REG_GPIO_PIN_CTRL_2 + 1;
		gpio_pin_output_en = REG_GPIO_PIN_CTRL_2 + 2;
		gpio_num_to_set = gpio_num - 8;
	}

	/* Read GPIO Direction */
	direction = (rtw_read8(adapter, gpio_pin_output_en) & BIT(gpio_num_to_set)) >> gpio_num_to_set;

	/* According the direction to read register value */
	if (direction)
		value =  (rtw_read8(adapter, gpio_pin_output_val) & BIT(gpio_num_to_set)) >> gpio_num_to_set;
	else
		value =  (rtw_read8(adapter, gpio_pin_input_val) & BIT(gpio_num_to_set)) >> gpio_num_to_set;

	rtw_ps_deny_cancel(adapter, PS_DENY_IOCTL);
	RTW_INFO("%s direction=%d value=%d\n", __FUNCTION__, direction, value);

	return value;
}

int  rtw_hal_set_gpio_output_value(_adapter *adapter, u8 gpio_num, bool isHigh)
{
	u8 direction = 0;
	u8 res = -1;
	u32 gpio_pin_output_val = REG_GPIO_PIN_CTRL + 1;
	u32 gpio_pin_output_en = REG_GPIO_PIN_CTRL + 2;
	u8 gpio_num_to_set = gpio_num;

	if (rtw_hal_gpio_func_check(adapter, gpio_num) == _FAIL)
		return -1;

	rtw_ps_deny(adapter, PS_DENY_IOCTL);

	LeaveAllPowerSaveModeDirect(adapter);

	if (gpio_num > 7) {
		gpio_pin_output_val = REG_GPIO_PIN_CTRL_2 + 1;
		gpio_pin_output_en = REG_GPIO_PIN_CTRL_2 + 2;
		gpio_num_to_set = gpio_num - 8;
	}

	/* Read GPIO direction */
	direction = (rtw_read8(adapter, gpio_pin_output_en) & BIT(gpio_num_to_set)) >> gpio_num_to_set;

	/* If GPIO is output direction, setting value. */
	if (direction) {
		if (isHigh)
			rtw_write8(adapter, gpio_pin_output_val, rtw_read8(adapter, gpio_pin_output_val) | BIT(gpio_num_to_set));
		else
			rtw_write8(adapter, gpio_pin_output_val, rtw_read8(adapter, gpio_pin_output_val) & ~BIT(gpio_num_to_set));

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
		IS_HARDWARE_TYPE_8188F(padapter) ||
		IS_HARDWARE_TYPE_8188GTV(padapter) ||
		IS_HARDWARE_TYPE_8192F(padapter) ||
		IS_HARDWARE_TYPE_8822C(padapter))
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
	} else if(IS_HARDWARE_TYPE_JAGUAR3(padapter)){
		cckok = phy_query_bb_reg(padapter, 0x2c04, 0xffff);
		ofdmok = phy_query_bb_reg(padapter, 0x2c14, 0xffff);
		htok = phy_query_bb_reg(padapter, 0x2c10, 0xffff);
		vht_ok = phy_query_bb_reg(padapter, 0x2c0c, 0xffff);
		cckcrc = phy_query_bb_reg(padapter, 0x2c04, 0xffff0000);
		ofdmcrc = phy_query_bb_reg(padapter, 0x2c14, 0xffff0000);
		htcrc = phy_query_bb_reg(padapter, 0x2c10, 0xffff0000);
		vht_err = phy_query_bb_reg(padapter, 0x2c0c, 0xffff0000);
		CCK_FA	= phy_query_bb_reg(padapter, 0x1a5c, bMaskLWord);
		OFDM_FA = phy_query_bb_reg(padapter, 0x2d00, bMaskLWord) - phy_query_bb_reg(padapter, 0x2de0, bMaskLWord);

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
	} else if(IS_HARDWARE_TYPE_JAGUAR3(padapter)) {
		phy_set_bb_reg(padapter, 0x1EB4, BIT25, 0x1);
		phy_set_bb_reg(padapter, 0x1EB4, BIT25, 0x0);
	} else {
		phy_set_bb_reg(padapter, 0xF14, BIT16, 0x1);
		phy_set_bb_reg(padapter, 0xF14, BIT16, 0x0);
	}
}

void rtw_reset_phy_rx_counters(_adapter *padapter)
{
	/* reset phy counter */
	if (IS_HARDWARE_TYPE_JAGUAR3(padapter)) {
		/* reset CCK FA counter */
		phy_set_bb_reg(padapter, 0x1a2c, BIT(15) | BIT(14), 0);
		phy_set_bb_reg(padapter, 0x1a2c, BIT(15) | BIT(14), 2);

		/* reset CCK CCA counter */
		phy_set_bb_reg(padapter, 0x1a2c, BIT(13) | BIT(12), 0);
		phy_set_bb_reg(padapter, 0x1a2c, BIT(13) | BIT(12), 2);
		rtw_reset_phy_trx_ok_counters(padapter);

	} else if (IS_HARDWARE_TYPE_JAGUAR(padapter) || IS_HARDWARE_TYPE_JAGUAR2(padapter)) {
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
u8 rtw_get_current_tx_sgi(_adapter *padapter, struct sta_info *psta)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(padapter);
	u8 curr_tx_sgi = 0;
	struct ra_sta_info *ra_info;

	if (!psta)
		return curr_tx_sgi;

	if (padapter->fix_rate == 0xff) {
#if defined(CONFIG_RTL8188E)
#if (RATE_ADAPTIVE_SUPPORT == 1)
		curr_tx_sgi = hal_data->odmpriv.ra_info[psta->cmn.mac_id].rate_sgi;
#endif /* (RATE_ADAPTIVE_SUPPORT == 1)*/
#else
		ra_info = &psta->cmn.ra_info;
		curr_tx_sgi = ((ra_info->curr_tx_rate) & 0x80) >> 7;
#endif
	} else {
		curr_tx_sgi = ((padapter->fix_rate) & 0x80) >> 7;
	}

	return curr_tx_sgi;
}

u8 rtw_get_current_tx_rate(_adapter *padapter, struct sta_info *psta)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(padapter);
	u8 rate_id = 0;
	struct ra_sta_info *ra_info;

	if (!psta)
		return rate_id;

	if (padapter->fix_rate == 0xff) {
#if defined(CONFIG_RTL8188E)
#if (RATE_ADAPTIVE_SUPPORT == 1)
		rate_id = hal_data->odmpriv.ra_info[psta->cmn.mac_id].decision_rate;
#endif /* (RATE_ADAPTIVE_SUPPORT == 1)*/
#else
		ra_info = &psta->cmn.ra_info;
		rate_id = ra_info->curr_tx_rate & 0x7f;
#endif
	} else {
		rate_id = padapter->fix_rate & 0x7f;
	}

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
		break;
	case HT_IOT_PEER_REALTEK:
		/* rtw_write16(padapter, 0x4cc, 0xffff); */
		/* rtw_write16(padapter, 0x546, 0x01c0); */
		break;
	default:
		pmlmeinfo->turboMode_cts2self = 0;
		pmlmeinfo->turboMode_rtsen = 1;
		break;
	}

}
#ifdef CONFIG_RTS_FULL_BW 
/*
8188E: not support full RTS BW feature(mac REG no define 480[5])
*/
void rtw_set_rts_bw(_adapter *padapter) {
	int i;
	u8 enable = 1;
	bool connect_to_8812 = _FALSE;
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	struct sta_info *station = NULL;

	for (i = 0; i < macid_ctl->num; i++) {
		if (rtw_macid_is_used(macid_ctl, i)) {

			station = NULL;
			station = macid_ctl->sta[i];
			if(station) {
				
				 _adapter *sta_adapter =station->padapter;
				struct mlme_ext_priv	*pmlmeext = &(sta_adapter->mlmeextpriv);
				struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
				
				if ( pmlmeinfo->state != WIFI_FW_NULL_STATE) {
					if(_rtw_memcmp(macid_ctl->sta[i]->cmn.mac_addr, bc_addr, ETH_ALEN) !=  _TRUE) {
						if (  macid_ctl->sta[i]->vendor_8812) {
							connect_to_8812 = _TRUE;
							enable = 0;
						}	
					}	
				}
			}
		}

		if(connect_to_8812)
			break;
	}
	
		RTW_INFO("%s connect_to_8812=%d,enable=%u\n", __FUNCTION__,connect_to_8812,enable);
		rtw_hal_set_hwreg(padapter, HW_VAR_SET_RTS_BW, &enable);
}
#endif/*CONFIG_RTS_FULL_BW*/

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
#ifdef CONFIG_RTL8188GTV
	case RTL8188GTV:
		init_hal_spec_8188gtv(adapter);
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
#ifdef CONFIG_RTL8710B
	case RTL8710B:
		init_hal_spec_8710b(adapter);
		break;
#endif
#ifdef CONFIG_RTL8192F
	case RTL8192F:
		init_hal_spec_8192f(adapter);
		break;
#endif
#ifdef CONFIG_RTL8822C
	case RTL8822C:
		rtl8822c_init_hal_spec(adapter);
		break;
#endif
#ifdef CONFIG_RTL8814B
	case RTL8814B:
		rtl8814b_init_hal_spec(adapter);
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
	RTW_PRINT_SEL(sel, "rf_reg_path_num:%u\n", hal_spec->rf_reg_path_num);
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

	RTW_PRINT_SEL(sel, "txgi_max:%u\n", hal_spec->txgi_max);
	RTW_PRINT_SEL(sel, "txgi_pdbm:%u\n", hal_spec->txgi_pdbm);

	RTW_PRINT_SEL(sel, "wl_func:");
	for (i = 0; i < WL_FUNC_BIT_NUM; i++) {
		if (((hal_spec->wl_func) >> i) & BIT0 && _wl_func_str[i])
			_RTW_PRINT_SEL(sel, "%s ", _wl_func_str[i]);
	}
	_RTW_PRINT_SEL(sel, "\n");

#if CONFIG_TX_AC_LIFETIME
	RTW_PRINT_SEL(sel, "tx_aclt_unit_factor:%u (unit:%uus)\n"
		, hal_spec->tx_aclt_unit_factor, hal_spec->tx_aclt_unit_factor * 32);
#endif

	RTW_PRINT_SEL(sel, "rx_tsf_filter:%u\n", hal_spec->rx_tsf_filter);

	RTW_PRINT_SEL(sel, "pg_txpwr_saddr:0x%X\n", hal_spec->pg_txpwr_saddr);
	RTW_PRINT_SEL(sel, "pg_txgi_diff_factor:%u\n", hal_spec->pg_txgi_diff_factor);
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
inline bool hal_is_mimo_support(_adapter *adapter)
{
	if ((GET_HAL_TX_NSS(adapter) == 1) &&
		(GET_HAL_RX_NSS(adapter) == 1))
		return 0;
	return 1;
}

/*
* hal_largest_bw - starting from in_bw, get largest bw supported by HAL
* @adapter:
* @in_bw: starting bw, value of enum channel_width
*
* Returns: value of enum channel_width
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

#ifndef CONFIG_HAS_TX_BEACON_PAUSE
void ResumeTxBeacon(_adapter *padapter)
{
	rtw_write8(padapter, REG_FWHW_TXQ_CTRL + 2,
		rtw_read8(padapter, REG_FWHW_TXQ_CTRL + 2) | BIT(6));

#ifdef RTW_HALMAC
	/* Add this for driver using HALMAC because driver doesn't have setup time init by self */
	/* TBTT setup time */
	rtw_write8(padapter, REG_TBTT_PROHIBIT, TBTT_PROHIBIT_SETUP_TIME);
#endif

	/* TBTT hold time: 0x540[19:8] */
	rtw_write8(padapter, REG_TBTT_PROHIBIT + 1, TBTT_PROHIBIT_HOLD_TIME & 0xFF);
	rtw_write8(padapter, REG_TBTT_PROHIBIT + 2,
		(rtw_read8(padapter, REG_TBTT_PROHIBIT + 2) & 0xF0) | (TBTT_PROHIBIT_HOLD_TIME >> 8));
}

void StopTxBeacon(_adapter *padapter)
{
	rtw_write8(padapter, REG_FWHW_TXQ_CTRL + 2,
		rtw_read8(padapter, REG_FWHW_TXQ_CTRL + 2) & (~BIT6));

	/* TBTT hold time: 0x540[19:8] */
	rtw_write8(padapter, REG_TBTT_PROHIBIT + 1, TBTT_PROHIBIT_HOLD_TIME_STOP_BCN & 0xFF);
	rtw_write8(padapter, REG_TBTT_PROHIBIT + 2,
		(rtw_read8(padapter, REG_TBTT_PROHIBIT + 2) & 0xF0) | (TBTT_PROHIBIT_HOLD_TIME_STOP_BCN >> 8));
}
#endif /* CONFIG_HAS_TX_BEACON_PAUSE */

#ifdef CONFIG_MI_WITH_MBSSID_CAM /*HW port0 - MBSS*/

#ifdef CONFIG_CLIENT_PORT_CFG
const u8 _clt_port_id[MAX_CLIENT_PORT_NUM] = {
	CLT_PORT0,
	CLT_PORT1,
	CLT_PORT2,
	CLT_PORT3
};

void rtw_clt_port_init(struct clt_port_t  *cltp)
{
	cltp->bmp = 0;
	cltp->num = 0;
	_rtw_spinlock_init(&cltp->lock);
}
void rtw_clt_port_deinit(struct clt_port_t *cltp)
{
	_rtw_spinlock_free(&cltp->lock);
}
static void _hw_client_port_alloc(_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct clt_port_t  *cltp = &dvobj->clt_port;
	_irqL irql;
	int i;

	#if 0
	if (cltp->num > MAX_CLIENT_PORT_NUM) {
		RTW_ERR(ADPT_FMT" cann't  alloc client (%d)\n", ADPT_ARG(adapter), cltp->num);
		rtw_warn_on(1);
		return;
	}
	#endif

	if (adapter->client_id !=  MAX_CLIENT_PORT_NUM) {
		RTW_INFO(ADPT_FMT" client_id %d has allocated port:%d\n",
			ADPT_ARG(adapter), adapter->client_id, adapter->client_port);
		return;
	}
	_enter_critical_bh(&cltp->lock, &irql);
	for (i = 0; i < MAX_CLIENT_PORT_NUM; i++) {
		if (!(cltp->bmp & BIT(i)))
			break;
	}

	if (i < MAX_CLIENT_PORT_NUM) {
		adapter->client_id = i;
		cltp->bmp |= BIT(i);
		adapter->client_port = _clt_port_id[i];
	}
	cltp->num++;
	_exit_critical_bh(&cltp->lock, &irql);
	RTW_INFO("%s("ADPT_FMT")id:%d, port:%d clt_num:%d\n",
		__func__, ADPT_ARG(adapter), adapter->client_id, adapter->client_port, cltp->num);
}
static void _hw_client_port_free(_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct clt_port_t  *cltp = &dvobj->clt_port;
	_irqL irql;

	#if 0
	if (adapter->client_id >=  MAX_CLIENT_PORT_NUM) {
		RTW_ERR(ADPT_FMT" client_id %d is invalid\n", ADPT_ARG(adapter), adapter->client_id);
		/*rtw_warn_on(1);*/
	}
	#endif

	RTW_INFO("%s ("ADPT_FMT") id:%d, port:%d clt_num:%d\n",
		__func__, ADPT_ARG(adapter), adapter->client_id, adapter->client_port, cltp->num);

	_enter_critical_bh(&cltp->lock, &irql);
	if (adapter->client_id !=  MAX_CLIENT_PORT_NUM) {
		cltp->bmp &= ~ BIT(adapter->client_id);
		adapter->client_id = MAX_CLIENT_PORT_NUM;
		adapter->client_port = CLT_PORT_INVALID;
	}
	cltp->num--;
	if (cltp->num < 0)
		cltp->num = 0;
	_exit_critical_bh(&cltp->lock, &irql);
}
void rtw_hw_client_port_allocate(_adapter *adapter)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);

	if (hal_spec->port_num != 5)
		return;

	_hw_client_port_alloc(adapter);
}
void rtw_hw_client_port_release(_adapter *adapter)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);

	if (hal_spec->port_num != 5)
		return;

	_hw_client_port_free(adapter);
}
#endif /*CONFIG_CLIENT_PORT_CFG*/

void hw_var_set_opmode_mbid(_adapter *Adapter, u8 mode)
{
	RTW_INFO("%s()-"ADPT_FMT" mode = %d\n", __func__, ADPT_ARG(Adapter), mode);

	rtw_hal_rcr_set_chk_bssid(Adapter, MLME_ACTION_NONE);

	/* set net_type */
	Set_MSR(Adapter, mode);

	if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_)) {
		if (!rtw_mi_get_ap_num(Adapter) && !rtw_mi_get_mesh_num(Adapter))
			StopTxBeacon(Adapter);
	} else if (mode == _HW_STATE_ADHOC_)
		ResumeTxBeacon(Adapter);
	else if (mode == _HW_STATE_AP_)
		/* enable rx ps-poll */
		rtw_write16(Adapter, REG_RXFLTMAP1, rtw_read16(Adapter, REG_RXFLTMAP1) | BIT_CTRLFLT10EN);

	/* enable rx data frame */
	rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);

#ifdef CONFIG_CLIENT_PORT_CFG
	if (mode == _HW_STATE_STATION_)
		rtw_hw_client_port_allocate(Adapter);
	else
		rtw_hw_client_port_release(Adapter);
#endif
#if defined(CONFIG_RTL8192F)
		rtw_write16(Adapter, REG_WLAN_ACT_MASK_CTRL_1, rtw_read16(Adapter, 
					REG_WLAN_ACT_MASK_CTRL_1) | EN_PORT_0_FUNCTION);	
#endif
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

inline s16 translate_dbm_to_percentage(s16 signal)
{
	if ((signal <= -100) || (signal >= 20))
		return	0;
	else if (signal >= 0)
		return	100;
	else
		return 100 + signal;
}

#ifdef CONFIG_SWTIMER_BASED_TXBCN
#ifdef CONFIG_BCN_RECOVERY
#define REG_CPU_MGQ_INFO	0x041C
#define BIT_BCN_POLL			BIT(28)
u8 rtw_ap_bcn_recovery(_adapter *padapter)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(padapter);

	if (hal_data->issue_bcn_fail >= 2) {
		RTW_ERR("%s ISSUE BCN Fail\n", __func__);
		rtw_write8(padapter, REG_CPU_MGQ_INFO + 3, 0x10);
		hal_data->issue_bcn_fail = 0;
	}
	return _SUCCESS;
}
#endif /*CONFIG_BCN_RECOVERY*/

#ifdef CONFIG_BCN_XMIT_PROTECT
u8 rtw_ap_bcn_queue_empty_check(_adapter *padapter, u32 txbcn_timer_ms)
{
	u32 start_time = rtw_get_current_time();
	u8 bcn_queue_empty = _FALSE;

	do {
		if (rtw_read16(padapter, REG_TXPKT_EMPTY) & BIT(11)) {
			bcn_queue_empty = _TRUE;
			break;
		}
	} while (rtw_get_passing_time_ms(start_time) <= (txbcn_timer_ms + 10));

	if (bcn_queue_empty == _FALSE)
		RTW_ERR("%s BCN queue not empty\n", __func__);

	return bcn_queue_empty;
}
#endif /*CONFIG_BCN_XMIT_PROTECT*/
#endif /*CONFIG_SWTIMER_BASED_TXBCN*/

/**
 * rtw_hal_get_trx_path() - Get RF path related information
 * @d:		struct dvobj_priv*
 * @type:	RF type, nTnR
 * @tx:		Tx path
 * @rx:		Rx path
 *
 * Get RF type, TX path and RX path information.
 */
void rtw_hal_get_trx_path(struct dvobj_priv *d, enum rf_type *type,
			 enum bb_path *tx, enum bb_path *rx)
{
	struct _ADAPTER *a = dvobj_get_primary_adapter(d);
	enum rf_type t = GET_HAL_RFPATH(a);

	if (type)
		*type = t;

	if (tx || rx) {
		u8 tx_bmp = GET_HAL_TX_PATH_BMP(a);
		u8 rx_bmp = GET_HAL_RX_PATH_BMP(a);

		if (!tx_bmp && !rx_bmp)
			rf_type_to_default_trx_bmp(t, tx, rx);
		else {
			if (tx)
				*tx = GET_HAL_TX_PATH_BMP(a);
			if (rx)
				*rx = GET_HAL_RX_PATH_BMP(a);
		}
	}
}

#ifdef RTW_CHANNEL_SWITCH_OFFLOAD
void rtw_hal_switch_chnl_and_set_bw_offload(_adapter *adapter, u8 central_ch, u8 pri_ch_idx, u8 bw)
{
	u8 h2c[H2C_SINGLE_CHANNELSWITCH_V2_LEN] = {0};
	PHAL_DATA_TYPE hal;
	struct submit_ctx *chsw_sctx;

	hal = GET_HAL_DATA(adapter);
	chsw_sctx = &hal->chsw_sctx;

	SET_H2CCMD_SINGLE_CH_SWITCH_V2_CENTRAL_CH_NUM(h2c, central_ch);
	SET_H2CCMD_SINGLE_CH_SWITCH_V2_PRIMARY_CH_IDX(h2c, pri_ch_idx);
	SET_H2CCMD_SINGLE_CH_SWITCH_V2_BW(h2c, bw);

	rtw_sctx_init(chsw_sctx, 10);
	rtw_hal_fill_h2c_cmd(adapter, H2C_SINGLE_CHANNELSWITCH_V2, H2C_SINGLE_CHANNELSWITCH_V2_LEN, h2c);
	rtw_sctx_wait(chsw_sctx, __func__);
}
#endif /* RTW_CHANNEL_SWITCH_OFFLOAD */

u8 phy_get_current_tx_num(
		PADAPTER		pAdapter,
		u8				Rate
)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(pAdapter);
	u8	tx_num = 0;

	if (IS_1T_RATE(Rate))
		tx_num = hal_data->txpath_num_nss[0];
	else if (IS_2T_RATE(Rate))
		tx_num = hal_data->txpath_num_nss[1];
	else if (IS_3T_RATE(Rate))
		tx_num = hal_data->txpath_num_nss[2];
	else if (IS_4T_RATE(Rate))
		tx_num = hal_data->txpath_num_nss[3];
	else
		rtw_warn_on(1);

	return tx_num == 0 ? RF_1TX : tx_num - 1;
}

#ifdef CONFIG_RTL8812A
u8 * rtw_hal_set_8812a_vendor_ie(_adapter *padapter , u8 *pframe ,uint *frlen ) {
	int vender_len = 7;
	unsigned char	vendor_info[vender_len];
	unsigned char REALTEK_OUI[] = {0x00, 0xe0, 0x4c};
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if( !IS_HARDWARE_TYPE_8812(padapter) )
		return pframe;

	_rtw_memset(vendor_info,0,vender_len);
	_rtw_memcpy(vendor_info, REALTEK_OUI, 3);
	vendor_info[4] =2;
	if(pHalData->version_id.CUTVersion > B_CUT_VERSION )
		vendor_info[6] = RT_HT_CAP_USE_JAGUAR_CCUT;
	else
		vendor_info[6] = RT_HT_CAP_USE_JAGUAR_BCUT;
	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_,vender_len,vendor_info , frlen);
	
	return pframe;
}
#endif /*CONFIG_RTL8812A*/

static inline void rtw_enter_protsel(struct protsel *protsel, u32 sel)
{
	int refcnt;

	_enter_critical_mutex(&protsel->mutex, NULL);

	refcnt = ATOMIC_INC_RETURN(&protsel->refcnt);

	WARN_ON(refcnt > 1 && protsel->sel != sel);

	protsel->sel = sel;

	_exit_critical_mutex(&protsel->mutex, NULL);
}

static inline void rtw_leave_protsel(struct protsel *protsel)
{
	int refcnt;

	_enter_critical_mutex(&protsel->mutex, NULL);

	refcnt = ATOMIC_DEC_RETURN(&protsel->refcnt);

	_exit_critical_mutex(&protsel->mutex, NULL);

	WARN_ON(refcnt < 0);
}

static inline bool rtw_assert_protsel(struct protsel *protsel)
{
	int refcnt = ATOMIC_READ(&protsel->refcnt);

	if (refcnt > 0)
		return true;

	return false;
}

#ifdef CONFIG_PROTSEL_PORT
void rtw_enter_protsel_port(_adapter *padapter, u8 port_sel)
{
	u8 val8;

	rtw_enter_protsel(&padapter->dvobj->protsel_port, port_sel);

	val8 = rtw_read8(padapter, REG_PORT_CTRL_SEL);
	val8 &= ~BIT_MASK_PORT_CTRL_SEL;
	val8 |= BIT_PORT_CTRL_SEL(port_sel);
	rtw_write8(padapter, REG_PORT_CTRL_SEL, val8);
}

bool rtw_assert_protsel_port(_adapter *padapter, u32 addr, u8 len)
{
	if (!padapter->bup)	/* don't assert before IF up */
		return true;

	return rtw_assert_protsel(&padapter->dvobj->protsel_port);
}

void rtw_leave_protsel_port(_adapter *padapter)
{
	rtw_leave_protsel(&padapter->dvobj->protsel_port);
}
#endif

#ifdef CONFIG_PROTSEL_ATIMDTIM
void rtw_enter_protsel_atimdtim(_adapter *padapter, u8 port_sel)
{
	/* 0~15 is for port 0 MBSSID setting
	 * 16 is for port 1 setting
	 * 17 is for port 2 setting
	 * 18 is for port 3 setting
	 * 19 is for port 4 setting
	 */
	u8 val8;

	if (port_sel >= 1 && port_sel <= 4)
		port_sel += 15;

	rtw_enter_protsel(&padapter->dvobj->protsel_atimdtim, port_sel);

	val8 = rtw_read8(padapter, REG_ATIM_DTIM_CTRL_SEL);
	val8 &= ~BIT_MASK_ATIM_DTIM_SEL;
	val8 |= BIT_ATIM_DTIM_SEL(port_sel);
	rtw_write8(padapter, REG_ATIM_DTIM_CTRL_SEL, val8);
}

bool rtw_assert_protsel_atimdtim(_adapter *padapter, u32 addr, u8 len)
{
	return rtw_assert_protsel(&padapter->dvobj->protsel_atimdtim);
}

void rtw_leave_protsel_atimdtim(_adapter *padapter)
{
	rtw_leave_protsel(&padapter->dvobj->protsel_atimdtim);
}
#endif

#ifdef CONFIG_PROTSEL_MACSLEEP
void rtw_enter_protsel_macsleep(_adapter *padapter, u8 sel)
{
	u32 val32;

	rtw_enter_protsel(&padapter->dvobj->protsel_macsleep, sel);

	val32 = rtw_read32(padapter, REG_MACID_SLEEP_CTRL);
	val32 &= ~BIT_MASK_MACID_SLEEP_SEL;
	val32 |= BIT_MACID_SLEEP_SEL(sel);
	rtw_write32(padapter, REG_MACID_SLEEP_CTRL, val32);
}

bool rtw_assert_protsel_macsleep(_adapter *padapter, u32 addr, u8 len)
{
	return rtw_assert_protsel(&padapter->dvobj->protsel_macsleep);
}

void rtw_leave_protsel_macsleep(_adapter *padapter)
{
	rtw_leave_protsel(&padapter->dvobj->protsel_macsleep);
}
#endif
