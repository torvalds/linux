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
#define _HAL_COM_C_

#include <drv_types.h>
#include <rtw_debug.h>
#include "hal_com_h2c.h"

#include "odm_precomp.h"

u8 rtw_hal_data_init(struct adapter *padapter)
{
	if (is_primary_adapter(padapter)) {	/* if (padapter->isprimary) */
		padapter->hal_data_sz = sizeof(struct hal_com_data);
		padapter->HalData = vzalloc(padapter->hal_data_sz);
		if (padapter->HalData == NULL) {
			DBG_8192C("cannot alloc memory for HAL DATA\n");
			return _FAIL;
		}
	}
	return _SUCCESS;
}

void rtw_hal_data_deinit(struct adapter *padapter)
{
	if (is_primary_adapter(padapter)) {	/* if (padapter->isprimary) */
		if (padapter->HalData) {
			phy_free_filebuf(padapter);
			vfree(padapter->HalData);
			padapter->HalData = NULL;
			padapter->hal_data_sz = 0;
		}
	}
}


void dump_chip_info(HAL_VERSION	ChipVersion)
{
	int cnt = 0;
	u8 buf[128];

	cnt += sprintf((buf+cnt), "Chip Version Info: CHIP_8723B_");
	cnt += sprintf((buf+cnt), "%s_", IS_NORMAL_CHIP(ChipVersion) ? "Normal_Chip" : "Test_Chip");
	if (IS_CHIP_VENDOR_TSMC(ChipVersion))
		cnt += sprintf((buf+cnt), "%s_", "TSMC");
	else if (IS_CHIP_VENDOR_UMC(ChipVersion))
		cnt += sprintf((buf+cnt), "%s_", "UMC");
	else if (IS_CHIP_VENDOR_SMIC(ChipVersion))
		cnt += sprintf((buf+cnt), "%s_", "SMIC");

	if (IS_A_CUT(ChipVersion))
		cnt += sprintf((buf+cnt), "A_CUT_");
	else if (IS_B_CUT(ChipVersion))
		cnt += sprintf((buf+cnt), "B_CUT_");
	else if (IS_C_CUT(ChipVersion))
		cnt += sprintf((buf+cnt), "C_CUT_");
	else if (IS_D_CUT(ChipVersion))
		cnt += sprintf((buf+cnt), "D_CUT_");
	else if (IS_E_CUT(ChipVersion))
		cnt += sprintf((buf+cnt), "E_CUT_");
	else if (IS_I_CUT(ChipVersion))
		cnt += sprintf((buf+cnt), "I_CUT_");
	else if (IS_J_CUT(ChipVersion))
		cnt += sprintf((buf+cnt), "J_CUT_");
	else if (IS_K_CUT(ChipVersion))
		cnt += sprintf((buf+cnt), "K_CUT_");
	else
		cnt += sprintf((buf+cnt), "UNKNOWN_CUT(%d)_", ChipVersion.CUTVersion);

	if (IS_1T1R(ChipVersion))
		cnt += sprintf((buf+cnt), "1T1R_");
	else if (IS_1T2R(ChipVersion))
		cnt += sprintf((buf+cnt), "1T2R_");
	else if (IS_2T2R(ChipVersion))
		cnt += sprintf((buf+cnt), "2T2R_");
	else
		cnt += sprintf((buf+cnt), "UNKNOWN_RFTYPE(%d)_", ChipVersion.RFType);

	cnt += sprintf((buf+cnt), "RomVer(%d)\n", ChipVersion.ROMVer);

	DBG_871X("%s", buf);
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

	if (false == AutoLoadFail) {
		u8 hw_chnlPlan;

		hw_chnlPlan = hw_channel_plan & (~EEPROM_CHANNEL_PLAN_BY_HW_MASK);
		if (rtw_is_channel_plan_valid(hw_chnlPlan)) {
#ifndef CONFIG_SW_CHANNEL_PLAN
			if (hw_channel_plan & EEPROM_CHANNEL_PLAN_BY_HW_MASK)
				pHalData->bDisableSWChannelPlan = true;
#endif /*  !CONFIG_SW_CHANNEL_PLAN */

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

bool HAL_IsLegalChannel(struct adapter *Adapter, u32 Channel)
{
	bool bLegalChannel = true;

	if (Channel > 14) {
		bLegalChannel = false;
		DBG_871X("Channel > 14 but wireless_mode do not support 5G\n");
	} else if ((Channel <= 14) && (Channel >= 1)) {
		if (IsSupported24G(Adapter->registrypriv.wireless_mode) == false) {
			bLegalChannel = false;
			DBG_871X("(Channel <= 14) && (Channel >= 1) but wireless_mode do not support 2.4G\n");
		}
	} else {
		bLegalChannel = false;
		DBG_871X("Channel is Invalid !!!\n");
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
		DBG_871X("HwRateToMRate(): Non supported Rate [%x]!!!\n", rate);
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

	if (buf == NULL)
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

	RT_PRINT_DATA(
		_module_hal_init_c_,
		_drv_info_,
		"c2h_evt_read(): ",
		&c2h_evt,
		sizeof(c2h_evt)
	);

	DBG_871X(
		"%s id:%u, len:%u, seq:%u, trigger:0x%02x\n",
		__func__,
		c2h_evt->id,
		c2h_evt->plen,
		c2h_evt->seq,
		trigger
	);

	/* Read the content */
	for (i = 0; i < c2h_evt->plen; i++)
		c2h_evt->payload[i] = rtw_read8(adapter, REG_C2HEVT_MSG_NORMAL + 2 + i);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_info_, "c2h_evt_read(): Command Content:\n",
		c2h_evt->payload, c2h_evt->plen);

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


u8  rtw_hal_networktype_to_raid(struct adapter *adapter, struct sta_info *psta)
{
	return networktype_to_raid_ex(adapter, psta);
}

u8 rtw_get_mgntframe_raid(struct adapter *adapter, unsigned char network_type)
{

	u8 raid;
	raid = (network_type & WIRELESS_11B) ? RATEID_IDX_B : RATEID_IDX_G;
	return raid;
}

void rtw_hal_update_sta_rate_mask(struct adapter *padapter, struct sta_info *psta)
{
	u8 i, rf_type, limit;
	u32 tx_ra_bitmap;

	if (psta == NULL)
		return;

	tx_ra_bitmap = 0;

	/* b/g mode ra_bitmap */
	for (i = 0; i < sizeof(psta->bssrateset); i++) {
		if (psta->bssrateset[i])
			tx_ra_bitmap |= rtw_get_bit_value_from_ieee_value(psta->bssrateset[i]&0x7f);
	}

	/* n mode ra_bitmap */
	if (psta->htpriv.ht_option) {
		rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
		if (rf_type == RF_2T2R)
			limit = 16; /*  2R */
		else
			limit = 8; /*   1R */

		for (i = 0; i < limit; i++) {
			if (psta->htpriv.ht_cap.supp_mcs_set[i/8] & BIT(i%8))
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
	DM_ODM_T *odm = &(hal_data->odmpriv);

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
		DBG_871X_LEVEL(
			_drv_always_,
			FUNC_ADPT_FMT" variable(%d) not defined!\n",
			FUNC_ADPT_ARG(adapter),
			variable
		);
		break;
	}
}

void GetHwReg(struct adapter *adapter, u8 variable, u8 *val)
{
	struct hal_com_data *hal_data = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &(hal_data->odmpriv);

	switch (variable) {
	case HW_VAR_BASIC_RATE:
		*((u16 *)val) = hal_data->BasicRateSet;
		break;
	case HW_VAR_DM_FLAG:
		*((u32 *)val) = odm->SupportAbility;
		break;
	case HW_VAR_RF_TYPE:
		*((u8 *)val) = hal_data->rf_type;
		break;
	default:
		DBG_871X_LEVEL(
			_drv_always_,
			FUNC_ADPT_FMT" variable(%d) not defined!\n",
			FUNC_ADPT_ARG(adapter),
			variable
		);
		break;
	}
}




u8 SetHalDefVar(
	struct adapter *adapter, enum HAL_DEF_VARIABLE variable, void *value
)
{
	struct hal_com_data *hal_data = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &(hal_data->odmpriv);
	u8 bResult = _SUCCESS;

	switch (variable) {
	case HW_DEF_FA_CNT_DUMP:
		/* ODM_COMP_COMMON */
		if (*((u8 *)value))
			odm->DebugComponents |= (ODM_COMP_DIG | ODM_COMP_FA_CNT);
		else
			odm->DebugComponents &= ~(ODM_COMP_DIG | ODM_COMP_FA_CNT);
		break;
	case HAL_DEF_DBG_RX_INFO_DUMP:
		DBG_871X("============ Rx Info dump ===================\n");
		DBG_871X("bLinked = %d, RSSI_Min = %d(%%)\n",
			odm->bLinked, odm->RSSI_Min);

		if (odm->bLinked) {
			DBG_871X("RxRate = %s, RSSI_A = %d(%%), RSSI_B = %d(%%)\n",
				HDATA_RATE(odm->RxRate), odm->RSSI_A, odm->RSSI_B);

			#ifdef DBG_RX_SIGNAL_DISPLAY_RAW_DATA
			rtw_dump_raw_rssi_info(adapter);
			#endif
		}
		break;
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
			DBG_8192C("==> Disable all dynamic function...\n");
		} else if (dm_func == 1) {/* disable DIG */
			odm->SupportAbility  &= (~DYNAMIC_BB_DIG);
			DBG_8192C("==> Disable DIG...\n");
		} else if (dm_func == 2) {/* disable High power */
			odm->SupportAbility  &= (~DYNAMIC_BB_DYNAMIC_TXPWR);
		} else if (dm_func == 3) {/* disable tx power tracking */
			odm->SupportAbility  &= (~DYNAMIC_RF_CALIBRATION);
			DBG_8192C("==> Disable tx power tracking...\n");
		} else if (dm_func == 4) {/* disable BT coexistence */
			dm->DMFlag &= (~DYNAMIC_FUNC_BT);
		} else if (dm_func == 5) {/* disable antenna diversity */
			odm->SupportAbility  &= (~DYNAMIC_BB_ANT_DIV);
		} else if (dm_func == 6) {/* turn on all dynamic func */
			if (!(odm->SupportAbility  & DYNAMIC_BB_DIG)) {
				DIG_T	*pDigTable = &odm->DM_DigTable;
				pDigTable->CurIGValue = rtw_read8(adapter, 0xc50);
			}
			dm->DMFlag |= DYNAMIC_FUNC_BT;
			odm->SupportAbility = DYNAMIC_ALL_FUNC_ENABLE;
			DBG_8192C("==> Turn on all dynamic function...\n");
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
		DBG_871X_LEVEL(_drv_always_, "%s: [WARNING] HAL_DEF_VARIABLE(%d) not defined!\n", __func__, variable);
		bResult = _FAIL;
		break;
	}

	return bResult;
}

u8 GetHalDefVar(
	struct adapter *adapter, enum HAL_DEF_VARIABLE variable, void *value
)
{
	struct hal_com_data *hal_data = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &(hal_data->odmpriv);
	u8 bResult = _SUCCESS;

	switch (variable) {
	case HAL_DEF_UNDERCORATEDSMOOTHEDPWDB:
		{
			struct mlme_priv *pmlmepriv;
			struct sta_priv *pstapriv;
			struct sta_info *psta;

			pmlmepriv = &adapter->mlmepriv;
			pstapriv = &adapter->stapriv;
			psta = rtw_get_stainfo(pstapriv, pmlmepriv->cur_network.network.MacAddress);
			if (psta)
				*((int *)value) = psta->rssi_stat.UndecoratedSmoothedPWDB;
		}
		break;
	case HW_DEF_ODM_DBG_FLAG:
		*((u64 *)value) = odm->DebugComponents;
		break;
	case HW_DEF_ODM_DBG_LEVEL:
		*((u32 *)value) = odm->DebugLevel;
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
		DBG_871X_LEVEL(_drv_always_, "%s: [WARNING] HAL_DEF_VARIABLE(%d) not defined!\n", __func__, variable);
		bResult = _FAIL;
		break;
	}

	return bResult;
}

void GetHalODMVar(
	struct adapter *Adapter,
	enum HAL_ODM_VARIABLE eVariable,
	void *pValue1,
	void *pValue2
)
{
	switch (eVariable) {
#if defined(CONFIG_SIGNAL_DISPLAY_DBM) && defined(CONFIG_BACKGROUND_NOISE_MONITOR)
	case HAL_ODM_NOISE_MONITOR:
		{
			struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
			u8 chan = *(u8 *)pValue1;
			*(s16 *)pValue2 = pHalData->noise[chan];
			#ifdef DBG_NOISE_MONITOR
			DBG_8192C("### Noise monitor chan(%d)-noise:%d (dBm) ###\n",
				chan, pHalData->noise[chan]);
			#endif

		}
		break;
#endif/* ifdef CONFIG_BACKGROUND_NOISE_MONITOR */
	default:
		break;
	}
}

void SetHalODMVar(
	struct adapter *Adapter,
	enum HAL_ODM_VARIABLE eVariable,
	void *pValue1,
	bool bSet
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T podmpriv = &pHalData->odmpriv;
	/* _irqL irqL; */
	switch (eVariable) {
	case HAL_ODM_STA_INFO:
		{
			struct sta_info *psta = (struct sta_info *)pValue1;
			if (bSet) {
				DBG_8192C("### Set STA_(%d) info ###\n", psta->mac_id);
				ODM_CmnInfoPtrArrayHook(podmpriv, ODM_CMNINFO_STA_STATUS, psta->mac_id, psta);
			} else {
				DBG_8192C("### Clean STA_(%d) info ###\n", psta->mac_id);
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
	#if defined(CONFIG_SIGNAL_DISPLAY_DBM) && defined(CONFIG_BACKGROUND_NOISE_MONITOR)
	case HAL_ODM_NOISE_MONITOR:
		{
			struct noise_info *pinfo = (struct noise_info *)pValue1;

			#ifdef DBG_NOISE_MONITOR
			DBG_8192C("### Noise monitor chan(%d)-bPauseDIG:%d, IGIValue:0x%02x, max_time:%d (ms) ###\n",
				pinfo->chan, pinfo->bPauseDIG, pinfo->IGIValue, pinfo->max_time);
			#endif

			pHalData->noise[pinfo->chan] = ODM_InbandNoise_Monitor(podmpriv, pinfo->bPauseDIG, pinfo->IGIValue, pinfo->max_time);
			DBG_871X("chan_%d, noise = %d (dBm)\n", pinfo->chan, pHalData->noise[pinfo->chan]);
			#ifdef DBG_NOISE_MONITOR
			DBG_871X("noise_a = %d, noise_b = %d  noise_all:%d\n",
				podmpriv->noise_level.noise[ODM_RF_PATH_A],
				podmpriv->noise_level.noise[ODM_RF_PATH_B],
				podmpriv->noise_level.noise_all);
			#endif
		}
		break;
	#endif/* ifdef CONFIG_BACKGROUND_NOISE_MONITOR */

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

/*  */
/* 	Description: */
/* 		Return true if chTmp is represent for hex digit and */
/* 		false otherwise. */
/*  */
/*  */
bool IsHexDigit(char chTmp)
{
	if (
		(chTmp >= '0' && chTmp <= '9') ||
		(chTmp >= 'a' && chTmp <= 'f') ||
		(chTmp >= 'A' && chTmp <= 'F')
	)
		return true;
	else
		return false;
}


/*  */
/* 	Description: */
/* 		Translate a character to hex digit. */
/*  */
u32 MapCharToHexDigit(char chTmp)
{
	if (chTmp >= '0' && chTmp <= '9')
		return (chTmp - '0');
	else if (chTmp >= 'a' && chTmp <= 'f')
		return (10 + (chTmp - 'a'));
	else if (chTmp >= 'A' && chTmp <= 'F')
		return (10 + (chTmp - 'A'));
	else
		return 0;
}



/* 	Description: */
/* 		Parse hex number from the string pucStr. */
bool GetHexValueFromString(char *szStr, u32 *pu4bVal, u32 *pu4bMove)
{
	char *szScan = szStr;

	/*  Check input parameter. */
	if (szStr == NULL || pu4bVal == NULL || pu4bMove == NULL) {
		DBG_871X("GetHexValueFromString(): Invalid input arguments! szStr: %p, pu4bVal: %p, pu4bMove: %p\n",
			 szStr, pu4bVal, pu4bMove);
		return false;
	}

	/*  Initialize output. */
	*pu4bMove = 0;
	*pu4bVal = 0;

	/*  Skip leading space. */
	while (*szScan != '\0' && (*szScan == ' ' || *szScan == '\t')) {
		szScan++;
		(*pu4bMove)++;
	}

	/*  Skip leading '0x' or '0X'. */
	if (*szScan == '0' && (*(szScan+1) == 'x' || *(szScan+1) == 'X')) {
		szScan += 2;
		(*pu4bMove) += 2;
	}

	/*  Check if szScan is now pointer to a character for hex digit, */
	/*  if not, it means this is not a valid hex number. */
	if (!IsHexDigit(*szScan))
		return false;

	/*  Parse each digit. */
	do {
		(*pu4bVal) <<= 4;
		*pu4bVal += MapCharToHexDigit(*szScan);

		szScan++;
		(*pu4bMove)++;
	} while (IsHexDigit(*szScan));

	return true;
}

bool GetFractionValueFromString(
	char *szStr, u8 *pInteger, u8 *pFraction, u32 *pu4bMove
)
{
	char *szScan = szStr;

	/*  Initialize output. */
	*pu4bMove = 0;
	*pInteger = 0;
	*pFraction = 0;

	/*  Skip leading space. */
	while (*szScan != '\0' &&	(*szScan == ' ' || *szScan == '\t')) {
		++szScan;
		++(*pu4bMove);
	}

	/*  Parse each digit. */
	do {
		(*pInteger) *= 10;
		*pInteger += (*szScan - '0');

		++szScan;
		++(*pu4bMove);

		if (*szScan == '.') {
			++szScan;
			++(*pu4bMove);

			if (*szScan < '0' || *szScan > '9')
				return false;
			else {
				*pFraction = *szScan - '0';
				++szScan;
				++(*pu4bMove);
				return true;
			}
		}
	} while (*szScan >= '0' && *szScan <= '9');

	return true;
}

/*  */
/* 	Description: */
/* 		Return true if szStr is comment out with leading "//". */
/*  */
bool IsCommentString(char *szStr)
{
	if (*szStr == '/' && *(szStr+1) == '/')
		return true;
	else
		return false;
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

/*  <20121004, Kordan> For example,
 *  ParseQualifiedString(inString, 0, outString, '[', ']') gets "Kordan" from
 *  a string "Hello [Kordan]".
 *  If RightQualifier does not exist, it will hang in the while loop
 */
bool ParseQualifiedString(
	char *In, u32 *Start, char *Out, char LeftQualifier, char RightQualifier
)
{
	u32 i = 0, j = 0;
	char c = In[(*Start)++];

	if (c != LeftQualifier)
		return false;

	i = (*Start);
	while ((c = In[(*Start)++]) != RightQualifier)
		; /*  find ']' */
	j = (*Start) - 2;
	strncpy((char *)Out, (const char *)(In+i), j-i+1);

	return true;
}

bool isAllSpaceOrTab(u8 *data, u8 size)
{
	u8 cnt = 0, NumOfSpaceAndTab = 0;

	while (size > cnt) {
		if (data[cnt] == ' ' || data[cnt] == '\t' || data[cnt] == '\0')
			++NumOfSpaceAndTab;

		++cnt;
	}

	return size == NumOfSpaceAndTab;
}


void rtw_hal_check_rxfifo_full(struct adapter *adapter)
{
	struct dvobj_priv *psdpriv = adapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	int save_cnt = false;

	/* switch counter to RX fifo */
	/* printk("8723b or 8192e , MAC_667 set 0xf0\n"); */
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

void linked_info_dump(struct adapter *padapter, u8 benable)
{
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	if (padapter->bLinkInfoDump == benable)
		return;

	DBG_871X("%s %s\n", __func__, (benable) ? "enable" : "disable");

	if (benable) {
		pwrctrlpriv->org_power_mgnt = pwrctrlpriv->power_mgnt;/* keep org value */
		rtw_pm_set_lps(padapter, PS_MODE_ACTIVE);

		pwrctrlpriv->ips_org_mode = pwrctrlpriv->ips_mode;/* keep org value */
		rtw_pm_set_ips(padapter, IPS_NONE);
	} else {
		rtw_pm_set_ips(padapter, pwrctrlpriv->ips_org_mode);

		rtw_pm_set_lps(padapter, pwrctrlpriv->ips_org_mode);
	}
	padapter->bLinkInfoDump = benable;
}

#ifdef DBG_RX_SIGNAL_DISPLAY_RAW_DATA
void rtw_get_raw_rssi_info(void *sel, struct adapter *padapter)
{
	u8 isCCKrate, rf_path;
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	struct rx_raw_rssi *psample_pkt_rssi = &padapter->recvpriv.raw_rssi_info;

	DBG_871X_SEL_NL(
		sel,
		"RxRate = %s, PWDBALL = %d(%%), rx_pwr_all = %d(dBm)\n",
		HDATA_RATE(psample_pkt_rssi->data_rate),
		psample_pkt_rssi->pwdball, psample_pkt_rssi->pwr_all
	);

	isCCKrate = (psample_pkt_rssi->data_rate <= DESC_RATE11M) ? true : false;

	if (isCCKrate)
		psample_pkt_rssi->mimo_singal_strength[0] = psample_pkt_rssi->pwdball;

	for (rf_path = 0; rf_path < pHalData->NumTotalRFPath; rf_path++) {
		DBG_871X_SEL_NL(
			sel,
			"RF_PATH_%d =>singal_strength:%d(%%), singal_quality:%d(%%)\n",
			rf_path, psample_pkt_rssi->mimo_singal_strength[rf_path],
			psample_pkt_rssi->mimo_singal_quality[rf_path]
		);

		if (!isCCKrate) {
			DBG_871X_SEL_NL(
				sel,
				"\trx_ofdm_pwr:%d(dBm), rx_ofdm_snr:%d(dB)\n",
				psample_pkt_rssi->ofdm_pwr[rf_path],
				psample_pkt_rssi->ofdm_snr[rf_path]
			);
		}
	}
}

void rtw_dump_raw_rssi_info(struct adapter *padapter)
{
	u8 isCCKrate, rf_path;
	struct hal_com_data *pHalData =  GET_HAL_DATA(padapter);
	struct rx_raw_rssi *psample_pkt_rssi = &padapter->recvpriv.raw_rssi_info;
	DBG_871X("============ RAW Rx Info dump ===================\n");
	DBG_871X("RxRate = %s, PWDBALL = %d(%%), rx_pwr_all = %d(dBm)\n",
			HDATA_RATE(psample_pkt_rssi->data_rate), psample_pkt_rssi->pwdball, psample_pkt_rssi->pwr_all);

	isCCKrate = (psample_pkt_rssi->data_rate <= DESC_RATE11M) ? true : false;

	if (isCCKrate)
		psample_pkt_rssi->mimo_singal_strength[0] = psample_pkt_rssi->pwdball;

	for (rf_path = 0; rf_path < pHalData->NumTotalRFPath; rf_path++) {
		DBG_871X("RF_PATH_%d =>singal_strength:%d(%%), singal_quality:%d(%%)"
			, rf_path, psample_pkt_rssi->mimo_singal_strength[rf_path], psample_pkt_rssi->mimo_singal_quality[rf_path]);

		if (!isCCKrate) {
			printk(", rx_ofdm_pwr:%d(dBm), rx_ofdm_snr:%d(dB)\n",
			psample_pkt_rssi->ofdm_pwr[rf_path], psample_pkt_rssi->ofdm_snr[rf_path]);
		} else {
			printk("\n");
		}
	}
}

void rtw_store_phy_info(struct adapter *padapter, union recv_frame *prframe)
{
	u8 isCCKrate, rf_path;
	struct hal_com_data *pHalData =  GET_HAL_DATA(padapter);
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;

	PODM_PHY_INFO_T pPhyInfo  = (PODM_PHY_INFO_T)(&pattrib->phy_info);
	struct rx_raw_rssi *psample_pkt_rssi = &padapter->recvpriv.raw_rssi_info;

	psample_pkt_rssi->data_rate = pattrib->data_rate;
	isCCKrate = (pattrib->data_rate <= DESC_RATE11M) ? true : false;

	psample_pkt_rssi->pwdball = pPhyInfo->RxPWDBAll;
	psample_pkt_rssi->pwr_all = pPhyInfo->RecvSignalPower;

	for (rf_path = 0; rf_path < pHalData->NumTotalRFPath; rf_path++) {
		psample_pkt_rssi->mimo_singal_strength[rf_path] = pPhyInfo->RxMIMOSignalStrength[rf_path];
		psample_pkt_rssi->mimo_singal_quality[rf_path] = pPhyInfo->RxMIMOSignalQuality[rf_path];
		if (!isCCKrate) {
			psample_pkt_rssi->ofdm_pwr[rf_path] = pPhyInfo->RxPwr[rf_path];
			psample_pkt_rssi->ofdm_snr[rf_path] = pPhyInfo->RxSNR[rf_path];
		}
	}
}
#endif

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

void rtw_bb_rf_gain_offset(struct adapter *padapter)
{
	u8 value = padapter->eeprompriv.EEPROMRFGainOffset;
	u32 res, i = 0;
	u32 ArrayLen = sizeof(Array_kfreemap)/sizeof(u32);
	u32 *Array = Array_kfreemap;
	u32 v1 = 0, v2 = 0, target = 0;
	/* DBG_871X("+%s value: 0x%02x+\n", __func__, value); */

	if (value & BIT4) {
		DBG_871X("Offset RF Gain.\n");
		DBG_871X("Offset RF Gain.  padapter->eeprompriv.EEPROMRFGainVal = 0x%x\n", padapter->eeprompriv.EEPROMRFGainVal);
		if (padapter->eeprompriv.EEPROMRFGainVal != 0xff) {
			res = rtw_hal_read_rfreg(padapter, RF_PATH_A, 0x7f, 0xffffffff);
			res &= 0xfff87fff;
			DBG_871X("Offset RF Gain. before reg 0x7f = 0x%08x\n", res);
			/* res &= 0xfff87fff; */
			for (i = 0; i < ArrayLen; i += 2) {
				v1 = Array[i];
				v2 = Array[i+1];
				if (v1 == padapter->eeprompriv.EEPROMRFGainVal) {
					DBG_871X("Offset RF Gain. got v1 = 0x%x , v2 = 0x%x\n", v1, v2);
					target = v2;
					break;
				}
			}
			DBG_871X("padapter->eeprompriv.EEPROMRFGainVal = 0x%x , Gain offset Target Value = 0x%x\n", padapter->eeprompriv.EEPROMRFGainVal, target);
			PHY_SetRFReg(padapter, RF_PATH_A, REG_RF_BB_GAIN_OFFSET, BIT18|BIT17|BIT16|BIT15, target);

			/* res |= (padapter->eeprompriv.EEPROMRFGainVal & 0x0f)<< 15; */
			/* rtw_hal_write_rfreg(padapter, RF_PATH_A, REG_RF_BB_GAIN_OFFSET, RF_GAIN_OFFSET_MASK, res); */
			res = rtw_hal_read_rfreg(padapter, RF_PATH_A, 0x7f, 0xffffffff);
			DBG_871X("Offset RF Gain. After reg 0x7f = 0x%08x\n", res);
		} else
			DBG_871X("Offset RF Gain.  padapter->eeprompriv.EEPROMRFGainVal = 0x%x	!= 0xff, didn't run Kfree\n", padapter->eeprompriv.EEPROMRFGainVal);
	} else
		DBG_871X("Using the default RF gain.\n");
}
