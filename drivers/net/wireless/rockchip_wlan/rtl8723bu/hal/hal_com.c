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

//#define CONFIG_GTK_OL_DBG

#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
char	file_path[PATH_LENGTH_MAX];
#endif

void dump_chip_info(HAL_VERSION	ChipVersion)
{
	int cnt = 0;
	u8 buf[128]={0};
	
	if(IS_8188E(ChipVersion)){
		cnt += sprintf((buf+cnt), "Chip Version Info: CHIP_8188E_");
	}
	else if(IS_8812_SERIES(ChipVersion)){
		cnt += sprintf((buf+cnt), "Chip Version Info: CHIP_8812_");
	}
    else if(IS_8192E(ChipVersion)){
		cnt += sprintf((buf+cnt), "Chip Version Info: CHIP_8192E_");
	}
	else if(IS_8821_SERIES(ChipVersion)){
		cnt += sprintf((buf+cnt), "Chip Version Info: CHIP_8821_");
	}
	else if(IS_8723B_SERIES(ChipVersion)){
		cnt += sprintf((buf+cnt), "Chip Version Info: CHIP_8723B_");
	}

	cnt += sprintf((buf+cnt), "%s_", IS_NORMAL_CHIP(ChipVersion)?"Normal_Chip":"Test_Chip");
	if(IS_CHIP_VENDOR_TSMC(ChipVersion))
		cnt += sprintf((buf+cnt), "%s_","TSMC");
	else if(IS_CHIP_VENDOR_UMC(ChipVersion))	
		cnt += sprintf((buf+cnt), "%s_","UMC");
	else if(IS_CHIP_VENDOR_SMIC(ChipVersion))
		cnt += sprintf((buf+cnt), "%s_","SMIC");		
	
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
	else if (IS_F_CUT(ChipVersion))
		cnt += sprintf((buf+cnt), "F_CUT_");
	else if (IS_I_CUT(ChipVersion))
		cnt += sprintf((buf+cnt), "I_CUT_");
	else if (IS_J_CUT(ChipVersion))
		cnt += sprintf((buf+cnt), "J_CUT_");
	else if (IS_K_CUT(ChipVersion))
		cnt += sprintf((buf+cnt), "K_CUT_");
	else
		cnt += sprintf((buf+cnt), "UNKNOWN_CUT(%d)_", ChipVersion.CUTVersion);

	if(IS_1T1R(ChipVersion)) cnt += sprintf((buf+cnt), "1T1R_");
	else if(IS_1T2R(ChipVersion)) cnt += sprintf((buf+cnt), "1T2R_");
	else if(IS_2T2R(ChipVersion)) cnt += sprintf((buf+cnt), "2T2R_");
	else if(IS_3T3R(ChipVersion)) cnt += sprintf((buf+cnt), "3T3R_");
	else if(IS_3T4R(ChipVersion)) cnt += sprintf((buf+cnt), "3T4R_");
	else if(IS_4T4R(ChipVersion)) cnt += sprintf((buf+cnt), "4T4R_");
	else cnt += sprintf((buf+cnt), "UNKNOWN_RFTYPE(%d)_", ChipVersion.RFType);

	cnt += sprintf((buf+cnt), "RomVer(%d)\n", ChipVersion.ROMVer);

	DBG_871X("%s", buf);
}
void rtw_hal_config_rftype(PADAPTER  padapter)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	
	if (IS_1T1R(pHalData->VersionID)) {
		pHalData->rf_type = RF_1T1R;
		pHalData->NumTotalRFPath = 1;
	}	
	else if (IS_2T2R(pHalData->VersionID)) {
		pHalData->rf_type = RF_2T2R;
		pHalData->NumTotalRFPath = 2;
	}
	else if (IS_1T2R(pHalData->VersionID)) {
		pHalData->rf_type = RF_1T2R;
		pHalData->NumTotalRFPath = 2;
	}
	else if(IS_3T3R(pHalData->VersionID)) {
		pHalData->rf_type = RF_3T3R;
		pHalData->NumTotalRFPath = 3;
	}	
	else if(IS_4T4R(pHalData->VersionID)) {
		pHalData->rf_type = RF_4T4R;
		pHalData->NumTotalRFPath = 4;
	}
	else {
		pHalData->rf_type = RF_1T1R;
		pHalData->NumTotalRFPath = 1;
	}
	
	DBG_871X("%s RF_Type is %d TotalTxPath is %d \n", __FUNCTION__, pHalData->rf_type, pHalData->NumTotalRFPath);
}

#define	EEPROM_CHANNEL_PLAN_BY_HW_MASK	0x80

/*
 * Description:
 * 	Use hardware(efuse), driver parameter(registry) and default channel plan
 * 	to decide which one should be used.
 *
 * Parameters:
 *	padapter			pointer of adapter
 *	hw_channel_plan		channel plan from HW (efuse/eeprom)
 *						BIT[7] software configure mode; 0:Enable, 1:disable
 *						BIT[6:0] Channel Plan
 *	sw_channel_plan		channel plan from SW (registry/module param)
 *	def_channel_plan	channel plan used when HW/SW both invalid
 *	AutoLoadFail		efuse autoload fail or not
 *
 * Return:
 *	Final channel plan decision
 *
 */
u8
hal_com_config_channel_plan(
	IN	PADAPTER	padapter,
	IN	u8			hw_channel_plan,
	IN	u8			sw_channel_plan,
	IN	u8			def_channel_plan,
	IN	BOOLEAN		AutoLoadFail
	)
{
	PHAL_DATA_TYPE	pHalData;
	u8 hwConfig;
	u8 chnlPlan;


	pHalData = GET_HAL_DATA(padapter);
	pHalData->bDisableSWChannelPlan = _FALSE;
	chnlPlan = def_channel_plan;

	if (0xFF == hw_channel_plan)
		AutoLoadFail = _TRUE;

	if (_FALSE == AutoLoadFail)
	{
		u8 hw_chnlPlan;

		hw_chnlPlan = hw_channel_plan & (~EEPROM_CHANNEL_PLAN_BY_HW_MASK);
		if (rtw_is_channel_plan_valid(hw_chnlPlan))
		{
#ifndef CONFIG_SW_CHANNEL_PLAN
			if (hw_channel_plan & EEPROM_CHANNEL_PLAN_BY_HW_MASK)
				pHalData->bDisableSWChannelPlan = _TRUE;
#endif // !CONFIG_SW_CHANNEL_PLAN

			chnlPlan = hw_chnlPlan;
		}
	}

	if ((_FALSE == pHalData->bDisableSWChannelPlan)
		&& rtw_is_channel_plan_valid(sw_channel_plan))
	{
		chnlPlan = sw_channel_plan;
	}

	return chnlPlan;
}

BOOLEAN
HAL_IsLegalChannel(
	IN	PADAPTER	Adapter,
	IN	u32			Channel
	)
{
	BOOLEAN bLegalChannel = _TRUE;

	if (Channel > 14) {
		if(IsSupported5G(Adapter->registrypriv.wireless_mode) == _FALSE) {
			bLegalChannel = _FALSE;
			DBG_871X("Channel > 14 but wireless_mode do not support 5G\n");
		}
	} else if ((Channel <= 14) && (Channel >=1)){
		if(IsSupported24G(Adapter->registrypriv.wireless_mode) == _FALSE) {
			bLegalChannel = _FALSE;
			DBG_871X("(Channel <= 14) && (Channel >=1) but wireless_mode do not support 2.4G\n");
		}
	} else {
		bLegalChannel = _FALSE;
		DBG_871X("Channel is Invalid !!!\n");
	}

	return bLegalChannel;
}	

u8	MRateToHwRate(u8 rate)
{
	u8	ret = DESC_RATE1M;
		
	switch(rate)
	{
		case MGN_1M:		    ret = DESC_RATE1M;	break;
		case MGN_2M:		    ret = DESC_RATE2M;	break;
		case MGN_5_5M:		    ret = DESC_RATE5_5M;	break;
		case MGN_11M:		    ret = DESC_RATE11M;	break;
		case MGN_6M:		    ret = DESC_RATE6M;	break;
		case MGN_9M:		    ret = DESC_RATE9M;	break;
		case MGN_12M:		    ret = DESC_RATE12M;	break;
		case MGN_18M:		    ret = DESC_RATE18M;	break;
		case MGN_24M:		    ret = DESC_RATE24M;	break;
		case MGN_36M:		    ret = DESC_RATE36M;	break;
		case MGN_48M:		    ret = DESC_RATE48M;	break;
		case MGN_54M:		    ret = DESC_RATE54M;	break;

		case MGN_MCS0:		    ret = DESC_RATEMCS0;	break;
		case MGN_MCS1:		    ret = DESC_RATEMCS1;	break;
		case MGN_MCS2:		    ret = DESC_RATEMCS2;	break;
		case MGN_MCS3:		    ret = DESC_RATEMCS3;	break;
		case MGN_MCS4:		    ret = DESC_RATEMCS4;	break;
		case MGN_MCS5:		    ret = DESC_RATEMCS5;	break;
		case MGN_MCS6:		    ret = DESC_RATEMCS6;	break;
		case MGN_MCS7:		    ret = DESC_RATEMCS7;	break;
		case MGN_MCS8:		    ret = DESC_RATEMCS8;	break;
		case MGN_MCS9:		    ret = DESC_RATEMCS9;	break;
		case MGN_MCS10:	        ret = DESC_RATEMCS10;	break;
		case MGN_MCS11:	        ret = DESC_RATEMCS11;	break;
		case MGN_MCS12:	        ret = DESC_RATEMCS12;	break;
		case MGN_MCS13:	        ret = DESC_RATEMCS13;	break;
		case MGN_MCS14:	        ret = DESC_RATEMCS14;	break;
		case MGN_MCS15:	        ret = DESC_RATEMCS15;	break;
		case MGN_MCS16:		    ret = DESC_RATEMCS16;	break;
		case MGN_MCS17:		    ret = DESC_RATEMCS17;	break;
		case MGN_MCS18:		    ret = DESC_RATEMCS18;	break;
		case MGN_MCS19:		    ret = DESC_RATEMCS19;	break;
		case MGN_MCS20:	        ret = DESC_RATEMCS20;	break;
		case MGN_MCS21:	        ret = DESC_RATEMCS21;	break;
		case MGN_MCS22:	        ret = DESC_RATEMCS22;	break;
		case MGN_MCS23:	        ret = DESC_RATEMCS23;	break;
		case MGN_MCS24:	        ret = DESC_RATEMCS24;	break;
		case MGN_MCS25:	        ret = DESC_RATEMCS25;	break;
		case MGN_MCS26:		    ret = DESC_RATEMCS26;	break;
		case MGN_MCS27:		    ret = DESC_RATEMCS27;	break;
		case MGN_MCS28:		    ret = DESC_RATEMCS28;	break;
		case MGN_MCS29:		    ret = DESC_RATEMCS29;	break;
		case MGN_MCS30:	        ret = DESC_RATEMCS30;	break;
		case MGN_MCS31:	        ret = DESC_RATEMCS31;	break;

		case MGN_VHT1SS_MCS0:	ret = DESC_RATEVHTSS1MCS0;	break;
		case MGN_VHT1SS_MCS1:	ret = DESC_RATEVHTSS1MCS1;	break;
		case MGN_VHT1SS_MCS2:	ret = DESC_RATEVHTSS1MCS2;	break;
		case MGN_VHT1SS_MCS3:	ret = DESC_RATEVHTSS1MCS3;	break;
		case MGN_VHT1SS_MCS4:	ret = DESC_RATEVHTSS1MCS4;	break;
		case MGN_VHT1SS_MCS5:	ret = DESC_RATEVHTSS1MCS5;	break;
		case MGN_VHT1SS_MCS6:	ret = DESC_RATEVHTSS1MCS6;	break;
		case MGN_VHT1SS_MCS7:	ret = DESC_RATEVHTSS1MCS7;	break;
		case MGN_VHT1SS_MCS8:	ret = DESC_RATEVHTSS1MCS8;	break;
		case MGN_VHT1SS_MCS9:	ret = DESC_RATEVHTSS1MCS9;	break;	
		case MGN_VHT2SS_MCS0:	ret = DESC_RATEVHTSS2MCS0;	break;
		case MGN_VHT2SS_MCS1:	ret = DESC_RATEVHTSS2MCS1;	break;
		case MGN_VHT2SS_MCS2:	ret = DESC_RATEVHTSS2MCS2;	break;
		case MGN_VHT2SS_MCS3:	ret = DESC_RATEVHTSS2MCS3;	break;
		case MGN_VHT2SS_MCS4:	ret = DESC_RATEVHTSS2MCS4;	break;
		case MGN_VHT2SS_MCS5:	ret = DESC_RATEVHTSS2MCS5;	break;
		case MGN_VHT2SS_MCS6:	ret = DESC_RATEVHTSS2MCS6;	break;
		case MGN_VHT2SS_MCS7:	ret = DESC_RATEVHTSS2MCS7;	break;
		case MGN_VHT2SS_MCS8:	ret = DESC_RATEVHTSS2MCS8;	break;
		case MGN_VHT2SS_MCS9:	ret = DESC_RATEVHTSS2MCS9;	break;	
		case MGN_VHT3SS_MCS0:	ret = DESC_RATEVHTSS3MCS0;	break;
		case MGN_VHT3SS_MCS1:	ret = DESC_RATEVHTSS3MCS1;	break;
		case MGN_VHT3SS_MCS2:	ret = DESC_RATEVHTSS3MCS2;	break;
		case MGN_VHT3SS_MCS3:	ret = DESC_RATEVHTSS3MCS3;	break;
		case MGN_VHT3SS_MCS4:	ret = DESC_RATEVHTSS3MCS4;	break;
		case MGN_VHT3SS_MCS5:	ret = DESC_RATEVHTSS3MCS5;	break;
		case MGN_VHT3SS_MCS6:	ret = DESC_RATEVHTSS3MCS6;	break;
		case MGN_VHT3SS_MCS7:	ret = DESC_RATEVHTSS3MCS7;	break;
		case MGN_VHT3SS_MCS8:	ret = DESC_RATEVHTSS3MCS8;	break;
		case MGN_VHT3SS_MCS9:	ret = DESC_RATEVHTSS3MCS9;	break;
		case MGN_VHT4SS_MCS0:	ret = DESC_RATEVHTSS4MCS0;	break;
		case MGN_VHT4SS_MCS1:	ret = DESC_RATEVHTSS4MCS1;	break;
		case MGN_VHT4SS_MCS2:	ret = DESC_RATEVHTSS4MCS2;	break;
		case MGN_VHT4SS_MCS3:	ret = DESC_RATEVHTSS4MCS3;	break;
		case MGN_VHT4SS_MCS4:	ret = DESC_RATEVHTSS4MCS4;	break;
		case MGN_VHT4SS_MCS5:	ret = DESC_RATEVHTSS4MCS5;	break;
		case MGN_VHT4SS_MCS6:	ret = DESC_RATEVHTSS4MCS6;	break;
		case MGN_VHT4SS_MCS7:	ret = DESC_RATEVHTSS4MCS7;	break;
		case MGN_VHT4SS_MCS8:	ret = DESC_RATEVHTSS4MCS8;	break;
		case MGN_VHT4SS_MCS9:	ret = DESC_RATEVHTSS4MCS9;	break;
		default:		break;
	}

	return ret;
}

u8	HwRateToMRate(u8 rate)
{
	u8	ret_rate = MGN_1M;

	switch(rate)
	{
	
		case DESC_RATE1M:		    ret_rate = MGN_1M;		break;
		case DESC_RATE2M:		    ret_rate = MGN_2M;		break;
		case DESC_RATE5_5M:	        ret_rate = MGN_5_5M;	break;
		case DESC_RATE11M:		    ret_rate = MGN_11M;		break;
		case DESC_RATE6M:		    ret_rate = MGN_6M;		break;
		case DESC_RATE9M:		    ret_rate = MGN_9M;		break;
		case DESC_RATE12M:		    ret_rate = MGN_12M;		break;
		case DESC_RATE18M:		    ret_rate = MGN_18M;		break;
		case DESC_RATE24M:		    ret_rate = MGN_24M;		break;
		case DESC_RATE36M:		    ret_rate = MGN_36M;		break;
		case DESC_RATE48M:		    ret_rate = MGN_48M;		break;
		case DESC_RATE54M:		    ret_rate = MGN_54M;		break;			
		case DESC_RATEMCS0:	        ret_rate = MGN_MCS0;	break;
		case DESC_RATEMCS1:	        ret_rate = MGN_MCS1;	break;
		case DESC_RATEMCS2:	        ret_rate = MGN_MCS2;	break;
		case DESC_RATEMCS3:	        ret_rate = MGN_MCS3;	break;
		case DESC_RATEMCS4:	        ret_rate = MGN_MCS4;	break;
		case DESC_RATEMCS5:	        ret_rate = MGN_MCS5;	break;
		case DESC_RATEMCS6:	        ret_rate = MGN_MCS6;	break;
		case DESC_RATEMCS7:	        ret_rate = MGN_MCS7;	break;
		case DESC_RATEMCS8:	        ret_rate = MGN_MCS8;	break;
		case DESC_RATEMCS9:	        ret_rate = MGN_MCS9;	break;
		case DESC_RATEMCS10:	    ret_rate = MGN_MCS10;	break;
		case DESC_RATEMCS11:	    ret_rate = MGN_MCS11;	break;
		case DESC_RATEMCS12:	    ret_rate = MGN_MCS12;	break;
		case DESC_RATEMCS13:	    ret_rate = MGN_MCS13;	break;
		case DESC_RATEMCS14:	    ret_rate = MGN_MCS14;	break;
		case DESC_RATEMCS15:	    ret_rate = MGN_MCS15;	break;
		case DESC_RATEMCS16:	    ret_rate = MGN_MCS16;	break;
		case DESC_RATEMCS17:	    ret_rate = MGN_MCS17;	break;
		case DESC_RATEMCS18:	    ret_rate = MGN_MCS18;	break;
		case DESC_RATEMCS19:	    ret_rate = MGN_MCS19;	break;
		case DESC_RATEMCS20:	    ret_rate = MGN_MCS20;	break;
		case DESC_RATEMCS21:	    ret_rate = MGN_MCS21;	break;
		case DESC_RATEMCS22:	    ret_rate = MGN_MCS22;	break;
		case DESC_RATEMCS23:	    ret_rate = MGN_MCS23;	break;
		case DESC_RATEMCS24:	    ret_rate = MGN_MCS24;	break;
		case DESC_RATEMCS25:	    ret_rate = MGN_MCS25;	break;
		case DESC_RATEMCS26:	    ret_rate = MGN_MCS26;	break;
		case DESC_RATEMCS27:	    ret_rate = MGN_MCS27;	break;
		case DESC_RATEMCS28:	    ret_rate = MGN_MCS28;	break;
		case DESC_RATEMCS29:	    ret_rate = MGN_MCS29;	break;
		case DESC_RATEMCS30:	    ret_rate = MGN_MCS30;	break;
		case DESC_RATEMCS31:	    ret_rate = MGN_MCS31;	break;
		case DESC_RATEVHTSS1MCS0:	ret_rate = MGN_VHT1SS_MCS0;		break;
		case DESC_RATEVHTSS1MCS1:	ret_rate = MGN_VHT1SS_MCS1;		break;
		case DESC_RATEVHTSS1MCS2:	ret_rate = MGN_VHT1SS_MCS2;		break;
		case DESC_RATEVHTSS1MCS3:	ret_rate = MGN_VHT1SS_MCS3;		break;
		case DESC_RATEVHTSS1MCS4:	ret_rate = MGN_VHT1SS_MCS4;		break;
		case DESC_RATEVHTSS1MCS5:	ret_rate = MGN_VHT1SS_MCS5;		break;
		case DESC_RATEVHTSS1MCS6:	ret_rate = MGN_VHT1SS_MCS6;		break;
		case DESC_RATEVHTSS1MCS7:	ret_rate = MGN_VHT1SS_MCS7;		break;
		case DESC_RATEVHTSS1MCS8:	ret_rate = MGN_VHT1SS_MCS8;		break;
		case DESC_RATEVHTSS1MCS9:	ret_rate = MGN_VHT1SS_MCS9;		break;
		case DESC_RATEVHTSS2MCS0:	ret_rate = MGN_VHT2SS_MCS0;		break;
		case DESC_RATEVHTSS2MCS1:	ret_rate = MGN_VHT2SS_MCS1;		break;
		case DESC_RATEVHTSS2MCS2:	ret_rate = MGN_VHT2SS_MCS2;		break;
		case DESC_RATEVHTSS2MCS3:	ret_rate = MGN_VHT2SS_MCS3;		break;
		case DESC_RATEVHTSS2MCS4:	ret_rate = MGN_VHT2SS_MCS4;		break;
		case DESC_RATEVHTSS2MCS5:	ret_rate = MGN_VHT2SS_MCS5;		break;
		case DESC_RATEVHTSS2MCS6:	ret_rate = MGN_VHT2SS_MCS6;		break;
		case DESC_RATEVHTSS2MCS7:	ret_rate = MGN_VHT2SS_MCS7;		break;
		case DESC_RATEVHTSS2MCS8:	ret_rate = MGN_VHT2SS_MCS8;		break;
		case DESC_RATEVHTSS2MCS9:	ret_rate = MGN_VHT2SS_MCS9;		break;				
		case DESC_RATEVHTSS3MCS0:	ret_rate = MGN_VHT3SS_MCS0;		break;
		case DESC_RATEVHTSS3MCS1:	ret_rate = MGN_VHT3SS_MCS1;		break;
		case DESC_RATEVHTSS3MCS2:	ret_rate = MGN_VHT3SS_MCS2;		break;
		case DESC_RATEVHTSS3MCS3:	ret_rate = MGN_VHT3SS_MCS3;		break;
		case DESC_RATEVHTSS3MCS4:	ret_rate = MGN_VHT3SS_MCS4;		break;
		case DESC_RATEVHTSS3MCS5:	ret_rate = MGN_VHT3SS_MCS5;		break;
		case DESC_RATEVHTSS3MCS6:	ret_rate = MGN_VHT3SS_MCS6;		break;
		case DESC_RATEVHTSS3MCS7:	ret_rate = MGN_VHT3SS_MCS7;		break;
		case DESC_RATEVHTSS3MCS8:	ret_rate = MGN_VHT3SS_MCS8;		break;
		case DESC_RATEVHTSS3MCS9:	ret_rate = MGN_VHT3SS_MCS9;		break;				
		case DESC_RATEVHTSS4MCS0:	ret_rate = MGN_VHT4SS_MCS0;		break;
		case DESC_RATEVHTSS4MCS1:	ret_rate = MGN_VHT4SS_MCS1;		break;
		case DESC_RATEVHTSS4MCS2:	ret_rate = MGN_VHT4SS_MCS2;		break;
		case DESC_RATEVHTSS4MCS3:	ret_rate = MGN_VHT4SS_MCS3;		break;
		case DESC_RATEVHTSS4MCS4:	ret_rate = MGN_VHT4SS_MCS4;		break;
		case DESC_RATEVHTSS4MCS5:	ret_rate = MGN_VHT4SS_MCS5;		break;
		case DESC_RATEVHTSS4MCS6:	ret_rate = MGN_VHT4SS_MCS6;		break;
		case DESC_RATEVHTSS4MCS7:	ret_rate = MGN_VHT4SS_MCS7;		break;
		case DESC_RATEVHTSS4MCS8:	ret_rate = MGN_VHT4SS_MCS8;		break;
		case DESC_RATEVHTSS4MCS9:	ret_rate = MGN_VHT4SS_MCS9;		break;				
		
		default:							
			DBG_871X("HwRateToMRate(): Non supported Rate [%x]!!!\n",rate );
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

	for(i=0;i<NDIS_802_11_LENGTH_RATES_EX;i++)
	{
		is_brate = mBratesOS[i] & IEEE80211_BASIC_RATE_MASK;
		brate = mBratesOS[i] & 0x7f;
		
		if( is_brate )
		{		
			switch(brate)
			{
				case IEEE80211_CCK_RATE_1MB:	*pBrateCfg |= RATE_1M;	break;
				case IEEE80211_CCK_RATE_2MB:	*pBrateCfg |= RATE_2M;	break;
				case IEEE80211_CCK_RATE_5MB:	*pBrateCfg |= RATE_5_5M;break;
				case IEEE80211_CCK_RATE_11MB:	*pBrateCfg |= RATE_11M;	break;
				case IEEE80211_OFDM_RATE_6MB:	*pBrateCfg |= RATE_6M;	break;
				case IEEE80211_OFDM_RATE_9MB:	*pBrateCfg |= RATE_9M;	break;
				case IEEE80211_OFDM_RATE_12MB:	*pBrateCfg |= RATE_12M;	break;
				case IEEE80211_OFDM_RATE_18MB:	*pBrateCfg |= RATE_18M;	break;
				case IEEE80211_OFDM_RATE_24MB:	*pBrateCfg |= RATE_24M;	break;
				case IEEE80211_OFDM_RATE_36MB:	*pBrateCfg |= RATE_36M;	break;
				case IEEE80211_OFDM_RATE_48MB:	*pBrateCfg |= RATE_48M;	break;
				case IEEE80211_OFDM_RATE_54MB:	*pBrateCfg |= RATE_54M;	break;
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

	pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];//VO
	pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[0];//VI
	pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[0];//BE
	pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[0];//BK
	
	pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];//BCN
	pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];//MGT
	pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];//HIGH
	pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];//TXCMD
}

static VOID
_TwoOutPipeMapping(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN	 	bWIFICfg
	)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(pAdapter);

	if(bWIFICfg){ //WMM
		
		//	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA 
		//{  0, 	1, 	0, 	1, 	0, 	0, 	0, 	0, 		0	};
		//0:ep_0 num, 1:ep_1 num 
		
		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[1];//VO
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[0];//VI
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[1];//BE
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[0];//BK
		
		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];//BCN
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];//MGT
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];//HIGH
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];//TXCMD
		
	}
	else{//typical setting

		
		//BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA 
		//{  1, 	1, 	0, 	0, 	0, 	0, 	0, 	0, 		0	};			
		//0:ep_0 num, 1:ep_1 num
		
		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];//VO
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[0];//VI
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[1];//BE
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[1];//BK
		
		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];//BCN
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];//MGT
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];//HIGH
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];//TXCMD	
		
	}
	
}

static VOID _ThreeOutPipeMapping(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN	 	bWIFICfg
	)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(pAdapter);

	if(bWIFICfg){//for WMM
		
		//	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA 
		//{  1, 	2, 	1, 	0, 	0, 	0, 	0, 	0, 		0	};
		//0:H, 1:N, 2:L 
		
		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];//VO
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[1];//VI
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[2];//BE
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[1];//BK
		
		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];//BCN
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];//MGT
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];//HIGH
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];//TXCMD
		
	}
	else{//typical setting

		
		//	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA 
		//{  2, 	2, 	1, 	0, 	0, 	0, 	0, 	0, 		0	};			
		//0:H, 1:N, 2:L 
		
		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];//VO
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[1];//VI
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[2];//BE
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[2];//BK
		
		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];//BCN
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];//MGT
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[0];//HIGH
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];//TXCMD	
	}

}
static VOID _FourOutPipeMapping(
	IN	PADAPTER	pAdapter,
	IN	BOOLEAN	 	bWIFICfg
	)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(pAdapter);

	if(bWIFICfg){//for WMM
		
		//	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA 
		//{  1, 	2, 	1, 	0, 	0, 	0, 	0, 	0, 		0	};
		//0:H, 1:N, 2:L ,3:E
		
		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];//VO
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[1];//VI
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[2];//BE
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[1];//BK
		
		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];//BCN
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];//MGT
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[3];//HIGH
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];//TXCMD
		
	}
	else{//typical setting

		
		//	BK, 	BE, 	VI, 	VO, 	BCN,	CMD,MGT,HIGH,HCCA 
		//{  2, 	2, 	1, 	0, 	0, 	0, 	0, 	0, 		0	};			
		//0:H, 1:N, 2:L 
		
		pdvobjpriv->Queue2Pipe[0] = pdvobjpriv->RtOutPipe[0];//VO
		pdvobjpriv->Queue2Pipe[1] = pdvobjpriv->RtOutPipe[1];//VI
		pdvobjpriv->Queue2Pipe[2] = pdvobjpriv->RtOutPipe[2];//BE
		pdvobjpriv->Queue2Pipe[3] = pdvobjpriv->RtOutPipe[2];//BK
		
		pdvobjpriv->Queue2Pipe[4] = pdvobjpriv->RtOutPipe[0];//BCN
		pdvobjpriv->Queue2Pipe[5] = pdvobjpriv->RtOutPipe[0];//MGT
		pdvobjpriv->Queue2Pipe[6] = pdvobjpriv->RtOutPipe[3];//HIGH
		pdvobjpriv->Queue2Pipe[7] = pdvobjpriv->RtOutPipe[0];//TXCMD	
	}

}
BOOLEAN
Hal_MappingOutPipe(
	IN	PADAPTER	pAdapter,
	IN	u8		NumOutPipe
	)
{
	struct registry_priv *pregistrypriv = &pAdapter->registrypriv;

	BOOLEAN	 bWIFICfg = (pregistrypriv->wifi_spec) ?_TRUE:_FALSE;
	
	BOOLEAN result = _TRUE;

	switch(NumOutPipe)
	{
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

void hal_init_macaddr(_adapter *adapter)
{
	rtw_hal_set_hwreg(adapter, HW_VAR_MAC_ADDR, adapter_mac_addr(adapter));
#ifdef  CONFIG_CONCURRENT_MODE
	if (adapter->pbuddy_adapter)
		rtw_hal_set_hwreg(adapter->pbuddy_adapter, HW_VAR_MAC_ADDR, adapter_mac_addr(adapter->pbuddy_adapter));
#endif
}

void rtw_init_hal_com_default_value(PADAPTER Adapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);

	pHalData->AntDetection = 1;
}

/* 
* C2H event format:
* Field	 TRIGGER		CONTENT	   CMD_SEQ 	CMD_LEN		 CMD_ID
* BITS	 [127:120]	[119:16]      [15:8]		  [7:4]	 	   [3:0]
*/

void c2h_evt_clear(_adapter *adapter)
{
	rtw_write8(adapter, REG_C2HEVT_CLEAR, C2H_EVT_HOST_CLOSE);
}

s32 c2h_evt_read(_adapter *adapter, u8 *buf)
{
	s32 ret = _FAIL;
	struct c2h_evt_hdr *c2h_evt;
	int i;
	u8 trigger;

	if (buf == NULL)
		goto exit;

#if defined (CONFIG_RTL8188E)

	trigger = rtw_read8(adapter, REG_C2HEVT_CLEAR);

	if (trigger == C2H_EVT_HOST_CLOSE) {
		goto exit; /* Not ready */
	} else if (trigger != C2H_EVT_FW_CLOSE) {
		goto clear_evt; /* Not a valid value */
	}

	c2h_evt = (struct c2h_evt_hdr *)buf;

	_rtw_memset(c2h_evt, 0, 16);

	*buf = rtw_read8(adapter, REG_C2HEVT_MSG_NORMAL);
	*(buf+1) = rtw_read8(adapter, REG_C2HEVT_MSG_NORMAL + 1);	

	RT_PRINT_DATA(_module_hal_init_c_, _drv_info_, "c2h_evt_read(): ",
		&c2h_evt , sizeof(c2h_evt));

	if (0) {
		DBG_871X("%s id:%u, len:%u, seq:%u, trigger:0x%02x\n", __func__
			, c2h_evt->id, c2h_evt->plen, c2h_evt->seq, trigger);
	}

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
#endif
exit:
	return ret;
}

/* 
* C2H event format:
* Field    TRIGGER    CMD_LEN    CONTENT    CMD_SEQ    CMD_ID
* BITS    [127:120]   [119:112]    [111:16]	     [15:8]         [7:0]
*/
s32 c2h_evt_read_88xx(_adapter *adapter, u8 *buf)
{
	s32 ret = _FAIL;
	struct c2h_evt_hdr_88xx *c2h_evt;
	int i;
	u8 trigger;

	if (buf == NULL)
		goto exit;

#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A) || defined(CONFIG_RTL8192E) || defined(CONFIG_RTL8723B)

	trigger = rtw_read8(adapter, REG_C2HEVT_CLEAR);

	if (trigger == C2H_EVT_HOST_CLOSE) {
		goto exit; /* Not ready */
	} else if (trigger != C2H_EVT_FW_CLOSE) {
		goto clear_evt; /* Not a valid value */
	}

	c2h_evt = (struct c2h_evt_hdr_88xx *)buf;

	_rtw_memset(c2h_evt, 0, 16);

	c2h_evt->id = rtw_read8(adapter, REG_C2HEVT_MSG_NORMAL);
	c2h_evt->seq = rtw_read8(adapter, REG_C2HEVT_CMD_SEQ_88XX);
	c2h_evt->plen = rtw_read8(adapter, REG_C2HEVT_CMD_LEN_88XX);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_info_, "c2h_evt_read(): ",
		&c2h_evt , sizeof(c2h_evt));

	if (0) {
		DBG_871X("%s id:%u, len:%u, seq:%u, trigger:0x%02x\n", __func__
			, c2h_evt->id, c2h_evt->plen, c2h_evt->seq, trigger);
	}

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
#endif
exit:
	return ret;
}


u8  rtw_hal_networktype_to_raid(_adapter *adapter, struct sta_info *psta)
{
	if(IS_NEW_GENERATION_IC(adapter)){
		return networktype_to_raid_ex(adapter,psta);
	}
	else{
		return networktype_to_raid(adapter,psta);
	}

}
u8 rtw_get_mgntframe_raid(_adapter *adapter,unsigned char network_type)
{	

	u8 raid;
	if(IS_NEW_GENERATION_IC(adapter)){
		
		raid = (network_type & WIRELESS_11B)	?RATEID_IDX_B
											:RATEID_IDX_G;		
	}
	else{
		raid = (network_type & WIRELESS_11B)	?RATR_INX_WIRELESS_B
											:RATR_INX_WIRELESS_G;		
	}	
	return raid;
}

void rtw_hal_update_sta_rate_mask(PADAPTER padapter, struct sta_info *psta)
{
	u8	i, rf_type, limit;
	u64	tx_ra_bitmap;

	if(psta == NULL)
	{
		return;
	}

	tx_ra_bitmap = 0;

	//b/g mode ra_bitmap  
	for (i=0; i<sizeof(psta->bssrateset); i++)
	{
		if (psta->bssrateset[i])
			tx_ra_bitmap |= rtw_get_bit_value_from_ieee_value(psta->bssrateset[i]&0x7f);
	}

#ifdef CONFIG_80211N_HT
#ifdef CONFIG_80211AC_VHT
	//AC mode ra_bitmap
	if(psta->vhtpriv.vht_option) 
	{
		tx_ra_bitmap |= (rtw_vht_rate_to_bitmap(psta->vhtpriv.vht_mcs_map) << 12);
	}
	else
#endif //CONFIG_80211AC_VHT
	{
		//n mode ra_bitmap
		if(psta->htpriv.ht_option)
		{
			rf_type = RF_1T1R;
			rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
			if(rf_type == RF_2T2R)
				limit=16;// 2R
			else if(rf_type == RF_3T3R)
				limit=24;// 3R
			else
				limit=8;//  1R

			for (i=0; i<limit; i++) {
				if (psta->htpriv.ht_cap.supp_mcs_set[i/8] & BIT(i%8))
					tx_ra_bitmap |= BIT(i+12);
			}
		}
	}
#endif //CONFIG_80211N_HT
	DBG_871X("supp_mcs_set = %02x, %02x, %02x, rf_type=%d, tx_ra_bitmap=%016llx\n"
	, psta->htpriv.ht_cap.supp_mcs_set[0], psta->htpriv.ht_cap.supp_mcs_set[1], psta->htpriv.ht_cap.supp_mcs_set[2], rf_type, tx_ra_bitmap);
	psta->ra_mask = tx_ra_bitmap;
	psta->init_rate = get_highest_rate_idx(tx_ra_bitmap)&0x3f;
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

	u8 iface_type;

	msr = rtw_read8(adapter, MSR);
	bcn_ctrl = rtw_read8(adapter, REG_BCN_CTRL);
	bcn_ctrl_1 = rtw_read8(adapter, REG_BCN_CTRL_1);

	for (i=0; i<2; i++)
		atimwnd[i] = rtw_read8(adapter, REG_ATIMWND+i);
	for (i=0; i<2; i++)
		atimwnd_1[i] = rtw_read8(adapter, REG_ATIMWND_1+i);

	for (i=0; i<8; i++)
		tsftr[i] = rtw_read8(adapter, REG_TSFTR+i);
	for (i=0; i<8; i++)
		tsftr_1[i] = rtw_read8(adapter, REG_TSFTR1+i);

	for (i=0; i<6; i++)
		macid[i] = rtw_read8(adapter, REG_MACID+i);

	for (i=0; i<6; i++)
		bssid[i] = rtw_read8(adapter, REG_BSSID+i);

	for (i=0; i<6; i++)
		macid_1[i] = rtw_read8(adapter, REG_MACID1+i);

	for (i=0; i<6; i++)
		bssid_1[i] = rtw_read8(adapter, REG_BSSID1+i);

#ifdef DBG_RUNTIME_PORT_SWITCH
	DBG_871X(FUNC_ADPT_FMT" before switch\n"
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
		, *((u16*)atimwnd)
		, *((u16*)atimwnd_1)
		, *((u64*)tsftr)
		, *((u64*)tsftr_1)
		, MAC_ARG(macid)
		, MAC_ARG(bssid)
		, MAC_ARG(macid_1)
		, MAC_ARG(bssid_1)
	);
#endif /* DBG_RUNTIME_PORT_SWITCH */

	/* disable bcn function, disable update TSF  */
	rtw_write8(adapter, REG_BCN_CTRL, (bcn_ctrl & (~EN_BCN_FUNCTION)) | DIS_TSF_UDT);
	rtw_write8(adapter, REG_BCN_CTRL_1, (bcn_ctrl_1 & (~EN_BCN_FUNCTION)) | DIS_TSF_UDT);

	/* switch msr */
	msr = (msr&0xf0) |((msr&0x03) << 2) | ((msr&0x0c) >> 2);
	rtw_write8(adapter, MSR, msr);

	/* write port0 */
	rtw_write8(adapter, REG_BCN_CTRL, bcn_ctrl_1 & ~EN_BCN_FUNCTION);
	for (i=0; i<2; i++)
		rtw_write8(adapter, REG_ATIMWND+i, atimwnd_1[i]);
	for (i=0; i<8; i++)
		rtw_write8(adapter, REG_TSFTR+i, tsftr_1[i]);
	for (i=0; i<6; i++)
		rtw_write8(adapter, REG_MACID+i, macid_1[i]);
	for (i=0; i<6; i++)
		rtw_write8(adapter, REG_BSSID+i, bssid_1[i]);

	/* write port1 */
	rtw_write8(adapter, REG_BCN_CTRL_1, bcn_ctrl & ~EN_BCN_FUNCTION);
	for (i=0; i<2; i++)
		rtw_write8(adapter, REG_ATIMWND_1+1, atimwnd[i]);
	for (i=0; i<8; i++)
		rtw_write8(adapter, REG_TSFTR1+i, tsftr[i]);
	for (i=0; i<6; i++)
		rtw_write8(adapter, REG_MACID1+i, macid[i]);
	for (i=0; i<6; i++)
		rtw_write8(adapter, REG_BSSID1+i, bssid[i]);

	/* write bcn ctl */
#ifdef CONFIG_BT_COEXIST
#if defined(CONFIG_RTL8723B)
	// always enable port0 beacon function for PSTDMA
	bcn_ctrl_1 |= EN_BCN_FUNCTION;
	// always disable port1 beacon function for PSTDMA
	bcn_ctrl &= ~EN_BCN_FUNCTION;
#endif
#endif
	rtw_write8(adapter, REG_BCN_CTRL, bcn_ctrl_1);
	rtw_write8(adapter, REG_BCN_CTRL_1, bcn_ctrl);

	if (adapter->iface_type == IFACE_PORT0) {
		adapter->iface_type = IFACE_PORT1;
		adapter->pbuddy_adapter->iface_type = IFACE_PORT0;
		DBG_871X_LEVEL(_drv_always_, "port switch - port0("ADPT_FMT"), port1("ADPT_FMT")\n",
			ADPT_ARG(adapter->pbuddy_adapter), ADPT_ARG(adapter));
	} else {
		adapter->iface_type = IFACE_PORT0;
		adapter->pbuddy_adapter->iface_type = IFACE_PORT1;
		DBG_871X_LEVEL(_drv_always_, "port switch - port0("ADPT_FMT"), port1("ADPT_FMT")\n",
			ADPT_ARG(adapter), ADPT_ARG(adapter->pbuddy_adapter));
	}

#ifdef DBG_RUNTIME_PORT_SWITCH
	msr = rtw_read8(adapter, MSR);
	bcn_ctrl = rtw_read8(adapter, REG_BCN_CTRL);
	bcn_ctrl_1 = rtw_read8(adapter, REG_BCN_CTRL_1);

	for (i=0; i<2; i++)
		atimwnd[i] = rtw_read8(adapter, REG_ATIMWND+i);
	for (i=0; i<2; i++)
		atimwnd_1[i] = rtw_read8(adapter, REG_ATIMWND_1+i);

	for (i=0; i<8; i++)
		tsftr[i] = rtw_read8(adapter, REG_TSFTR+i);
	for (i=0; i<8; i++)
		tsftr_1[i] = rtw_read8(adapter, REG_TSFTR1+i);

	for (i=0; i<6; i++)
		macid[i] = rtw_read8(adapter, REG_MACID+i);

	for (i=0; i<6; i++)
		bssid[i] = rtw_read8(adapter, REG_BSSID+i);

	for (i=0; i<6; i++)
		macid_1[i] = rtw_read8(adapter, REG_MACID1+i);

	for (i=0; i<6; i++)
		bssid_1[i] = rtw_read8(adapter, REG_BSSID1+i);

	DBG_871X(FUNC_ADPT_FMT" after switch\n"
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
		, *((u16*)atimwnd)
		, *((u16*)atimwnd_1)
		, *((u64*)tsftr)
		, *((u64*)tsftr_1)
		, MAC_ARG(macid)
		, MAC_ARG(bssid)
		, MAC_ARG(macid_1)
		, MAC_ARG(bssid_1)
	);
#endif /* DBG_RUNTIME_PORT_SWITCH */

#endif /* CONFIG_RUNTIME_PORT_SWITCH */
#endif /* CONFIG_CONCURRENT_MODE */
}

void rtw_hal_set_FwRsvdPage_cmd(PADAPTER padapter, PRSVDPAGE_LOC rsvdpageloc)
{
	struct	hal_ops *pHalFunc = &padapter->HalFunc;
	u8	u1H2CRsvdPageParm[H2C_RSVDPAGE_LOC_LEN]={0};
	u8	ret = 0;

	DBG_871X("RsvdPageLoc: ProbeRsp=%d PsPoll=%d Null=%d QoSNull=%d BTNull=%d\n",
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

void rtw_hal_set_ap_wow_rsvdpage_cmd(PADAPTER padapter, PRSVDPAGE_LOC rsvdpageloc)
{
	struct	pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct	hal_ops *pHalFunc = &padapter->HalFunc;
	u8	res = 0, count = 0, header = 0;
	u8 rsvdparm[H2C_AOAC_RSVDPAGE_LOC_LEN] = {0};

	header = rtw_read8(padapter, REG_BCNQ_BDNY);

	DBG_871X("%s: beacon: %d, probeRsp: %d, header:0x%02x\n", __func__,
			rsvdpageloc->LocApOffloadBCN,
			rsvdpageloc->LocProbeRsp,
			header);

	SET_H2CCMD_AP_WOWLAN_RSVDPAGE_LOC_BCN(rsvdparm,
			rsvdpageloc->LocApOffloadBCN + header);

	if (pHalFunc->fill_h2c_cmd != NULL) {
		res = pHalFunc->fill_h2c_cmd(padapter,
				H2C_BCN_RSVDPAGE,
				H2C_BCN_RSVDPAGE_LEN,
				rsvdparm);
	} else {
		DBG_871X("%s: Please hook fill_h2c_cmd first!\n", __func__);
		res = _FAIL;
	}

	rtw_msleep_os(10);

	_rtw_memset(&rsvdparm, 0, sizeof(rsvdparm));

	SET_H2CCMD_AP_WOWLAN_RSVDPAGE_LOC_ProbeRsp(
			rsvdparm,
			rsvdpageloc->LocProbeRsp + header);

	if (pHalFunc->fill_h2c_cmd != NULL) {
		res = pHalFunc->fill_h2c_cmd(padapter,
				H2C_PROBERSP_RSVDPAGE,
				H2C_PROBERSP_RSVDPAGE_LEN,
				rsvdparm);
	} else {
		DBG_871X("%s: Please hook fill_h2c_cmd first!\n", __func__);
		res = _FAIL;
	}

	rtw_msleep_os(10);
}

#ifdef CONFIG_GPIO_WAKEUP
/*
 * Switch GPIO_13, GPIO_14 to wlan control, or pull GPIO_13,14 MUST fail.
 * It happended at 8723B/8192E/8821A. New IC will check multi function GPIO,
 * and implement HAL function.
 */
static void rtw_hal_switch_gpio_wl_ctrl(_adapter* padapter, u8 index, u8 enable)
{
	if (index !=13 && index != 14) return;

	rtw_hal_set_hwreg(padapter, HW_SET_GPIO_WL_CTRL, (u8 *)(&enable));
}

static void rtw_hal_set_output_gpio(_adapter* padapter, u8 index, u8 outputval)
{
	if ( index <= 7 ) {
		/* config GPIO mode */
		rtw_write8(padapter, REG_GPIO_PIN_CTRL + 3,
				rtw_read8(padapter, REG_GPIO_PIN_CTRL + 3) & ~BIT(index) );

		/* config GPIO Sel */
		/* 0: input */
		/* 1: output */
		rtw_write8(padapter, REG_GPIO_PIN_CTRL + 2,
				rtw_read8(padapter, REG_GPIO_PIN_CTRL + 2) | BIT(index));

		/* set output value */
		if ( outputval ) {
			rtw_write8(padapter, REG_GPIO_PIN_CTRL + 1,
					rtw_read8(padapter, REG_GPIO_PIN_CTRL + 1) | BIT(index));
		} else {
			rtw_write8(padapter, REG_GPIO_PIN_CTRL + 1,
					rtw_read8(padapter, REG_GPIO_PIN_CTRL + 1) & ~BIT(index));
		}
	} else if (index <= 15){
		/* 88C Series: */
		/* index: 11~8 transform to 3~0 */
		/* 8723 Series: */
		/* index: 12~8 transform to 4~0 */

		index -= 8;

		/* config GPIO mode */
		rtw_write8(padapter, REG_GPIO_PIN_CTRL_2 + 3,
				rtw_read8(padapter, REG_GPIO_PIN_CTRL_2 + 3) & ~BIT(index) );

		/* config GPIO Sel */
		/* 0: input */
		/* 1: output */
		rtw_write8(padapter, REG_GPIO_PIN_CTRL_2 + 2,
				rtw_read8(padapter, REG_GPIO_PIN_CTRL_2 + 2) | BIT(index));

		/* set output value */
		if ( outputval ) {
			rtw_write8(padapter, REG_GPIO_PIN_CTRL_2 + 1,
					rtw_read8(padapter, REG_GPIO_PIN_CTRL_2 + 1) | BIT(index));
		} else {
			rtw_write8(padapter, REG_GPIO_PIN_CTRL_2 + 1,
					rtw_read8(padapter, REG_GPIO_PIN_CTRL_2 + 1) & ~BIT(index));
		}
	} else {
		DBG_871X("%s: invalid GPIO%d=%d\n",
				__FUNCTION__, index, outputval);
	}
}
#endif

void rtw_hal_set_FwAoacRsvdPage_cmd(PADAPTER padapter, PRSVDPAGE_LOC rsvdpageloc)
{
	struct	hal_ops *pHalFunc = &padapter->HalFunc;
	struct	pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8	res = 0, count = 0, ret = 0;
#ifdef CONFIG_WOWLAN	
	u8 u1H2CAoacRsvdPageParm[H2C_AOAC_RSVDPAGE_LOC_LEN]={0};

	DBG_871X("AOACRsvdPageLoc: RWC=%d ArpRsp=%d NbrAdv=%d GtkRsp=%d GtkInfo=%d ProbeReq=%d NetworkList=%d\n",
			rsvdpageloc->LocRemoteCtrlInfo, rsvdpageloc->LocArpRsp,
			rsvdpageloc->LocNbrAdv, rsvdpageloc->LocGTKRsp,
			rsvdpageloc->LocGTKInfo, rsvdpageloc->LocProbeReq,
			rsvdpageloc->LocNetList);

	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_REMOTE_WAKE_CTRL_INFO(u1H2CAoacRsvdPageParm, rsvdpageloc->LocRemoteCtrlInfo);
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_ARP_RSP(u1H2CAoacRsvdPageParm, rsvdpageloc->LocArpRsp);
		//SET_H2CCMD_AOAC_RSVDPAGE_LOC_NEIGHBOR_ADV(u1H2CAoacRsvdPageParm, rsvdpageloc->LocNbrAdv);
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_GTK_RSP(u1H2CAoacRsvdPageParm, rsvdpageloc->LocGTKRsp);
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_GTK_INFO(u1H2CAoacRsvdPageParm, rsvdpageloc->LocGTKInfo);
#ifdef CONFIG_GTK_OL
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_GTK_EXT_MEM(u1H2CAoacRsvdPageParm, rsvdpageloc->LocGTKEXTMEM);
#endif // CONFIG_GTK_OL
		ret = rtw_hal_fill_h2c_cmd(padapter,
					H2C_AOAC_RSVD_PAGE,
					H2C_AOAC_RSVDPAGE_LOC_LEN,
					u1H2CAoacRsvdPageParm);
	}
#ifdef CONFIG_PNO_SUPPORT
	else
	{

		if(!pwrpriv->pno_in_resume) {
			DBG_871X("NLO_INFO=%d\n", rsvdpageloc->LocPNOInfo);
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
#endif //CONFIG_PNO_SUPPORT
#endif // CONFIG_WOWLAN
}

#ifdef CONFIG_WOWLAN
// rtw_hal_check_wow_ctrl
// chk_type: _TRUE means to check enable, if 0x690 & bit1, WOW enable successful
//           _FALSE means to check disable, if 0x690 & bit1, WOW disable fail
static u8 rtw_hal_check_wow_ctrl(_adapter* adapter, u8 chk_type)
{
	u8 mstatus = 0;
	u8 trycnt = 25;
	u8 res = _FALSE;

	mstatus = rtw_read8(adapter, REG_WOW_CTRL);
	DBG_871X_LEVEL(_drv_info_, "%s mstatus:0x%02x\n", __func__, mstatus);

	if (chk_type) {
		while(!(mstatus&BIT1) && trycnt>1) {
			mstatus = rtw_read8(adapter, REG_WOW_CTRL);
			DBG_871X_LEVEL(_drv_always_,
					"Loop index: %d :0x%02x\n",
					trycnt, mstatus);
			trycnt --;
			rtw_msleep_os(2);
		}
		if (mstatus & BIT1)
			res = _TRUE;
		else
			res = _FALSE;
	} else {
		while (mstatus&BIT1 && trycnt>1) {
			mstatus = rtw_read8(adapter, REG_WOW_CTRL);
			DBG_871X_LEVEL(_drv_always_,
					"Loop index: %d :0x%02x\n",
					trycnt, mstatus);
			trycnt --;
			rtw_msleep_os(2);
		}

		if (mstatus & BIT1)
			res = _FALSE;
		else
			res = _TRUE;
	}
	DBG_871X_LEVEL(_drv_always_, "%s check_type: %d res: %d trycnt: %d\n",
			__func__, chk_type, res, (25 - trycnt));
	return res;
}

#ifdef CONFIG_PNO_SUPPORT
static u8 rtw_hal_check_pno_enabled(_adapter* adapter)
{
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(adapter);
	u8 res = 0, count = 0;
	u8 ret = _FALSE;
	if (ppwrpriv->wowlan_pno_enable && ppwrpriv->pno_in_resume == _FALSE) {
		res = rtw_read8(adapter, REG_PNO_STATUS);
		while(!(res&BIT(7)) && count < 25) {
			DBG_871X("[%d] cmd: 0x81 REG_PNO_STATUS: 0x%02x\n",
					count, res);
			res = rtw_read8(adapter, REG_PNO_STATUS);
			count++;
			rtw_msleep_os(2);
		}
		if (res & BIT(7))
			ret = _TRUE;
		else
			ret = _FALSE;
		DBG_871X("cmd: 0x81 REG_PNO_STATUS: ret(%d)\n", ret);
	}
	return ret;
}
#endif

static void rtw_hal_force_enable_rxdma(_adapter* adapter)
{
	DBG_871X("%s: Set 0x690=0x00\n", __func__);
	rtw_write8(adapter, REG_WOW_CTRL,
			(rtw_read8(adapter, REG_WOW_CTRL)&0xf0));
	DBG_871X_LEVEL(_drv_always_, "%s: Release RXDMA\n", __func__);
	rtw_write32(adapter, REG_RXPKT_NUM,
			(rtw_read32(adapter,REG_RXPKT_NUM)&(~RW_RELEASE_EN)));
}

static void rtw_hal_disable_tx_report(_adapter* adapter)
{
	rtw_write8(adapter, REG_TX_RPT_CTRL,
			((rtw_read8(adapter, REG_TX_RPT_CTRL)&~BIT(1)))&~BIT(5));
	DBG_871X("disable TXRPT:0x%02x\n", rtw_read8(adapter, REG_TX_RPT_CTRL));
}

static void rtw_hal_enable_tx_report(_adapter* adapter)
{
	rtw_write8(adapter, REG_TX_RPT_CTRL,
			((rtw_read8(adapter, REG_TX_RPT_CTRL)|BIT(1)))|BIT(5));
	DBG_871X("enable TX_RPT:0x%02x\n", rtw_read8(adapter, REG_TX_RPT_CTRL));
}

static void rtw_hal_backup_rate(_adapter* adapter)
{
	DBG_871X("%s\n", __func__);
	//backup data rate to register 0x8b for wowlan FW
	rtw_write8(adapter, 0x8d, 1);
	rtw_write8(adapter, 0x8c, 0);
	rtw_write8(adapter, 0x8f, 0x40);
	rtw_write8(adapter, 0x8b, rtw_read8(adapter, 0x2f0));
}

static u8 rtw_hal_pause_rx_dma(_adapter* adapter)
{
	u8 ret = 0;
	u8 trycnt = 100;
	u16 len = 0;
	u32 tmp = 0;
	int res = 0;
	//RX DMA stop
	DBG_871X_LEVEL(_drv_always_, "Pause DMA\n");
	rtw_write32(adapter, REG_RXPKT_NUM,
			(rtw_read32(adapter,REG_RXPKT_NUM)|RW_RELEASE_EN));
	do{
		if((rtw_read32(adapter, REG_RXPKT_NUM)&RXDMA_IDLE)) {
			DBG_871X_LEVEL(_drv_always_, "RX_DMA_IDLE is true\n");
			ret = _SUCCESS;
			break;
		}
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
		else {
			// If RX_DMA is not idle, receive one pkt from DMA
			res = sdio_local_read(adapter,
					SDIO_REG_RX0_REQ_LEN, 4, (u8*)&tmp);
			len = le16_to_cpu(tmp);
			DBG_871X_LEVEL(_drv_always_, "RX len:%d\n", len);

			if (len > 0)
				res = RecvOnePkt(adapter, len);
			else
				DBG_871X_LEVEL(_drv_always_, "read length fail %d\n", len);

			DBG_871X_LEVEL(_drv_always_, "RecvOnePkt Result: %d\n", res);
		}
#endif //CONFIG_SDIO_HCI || CONFIG_GSPI_HCI
	}while(trycnt--);

	if(trycnt ==0) {
		DBG_871X_LEVEL(_drv_always_, "Stop RX DMA failed...... \n");
		ret = _FAIL;
	}

	return ret;
}

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
static u8 rtw_hal_enable_cpwm2(_adapter* adapter)
{
	u8 ret = 0;
	int res = 0;
	u32 tmp = 0;

	DBG_871X_LEVEL(_drv_always_, "%s\n", __func__);

	res = sdio_local_read(adapter, SDIO_REG_HIMR, 4, (u8*)&tmp);
	if (!res)
		DBG_871X_LEVEL(_drv_info_, "read SDIO_REG_HIMR: 0x%08x\n", tmp);
	else
		DBG_871X_LEVEL(_drv_info_, "sdio_local_read fail\n");

	tmp = SDIO_HIMR_CPWM2_MSK;

	res = sdio_local_write(adapter, SDIO_REG_HIMR, 4, (u8*)&tmp);

	if (!res){
		res = sdio_local_read(adapter, SDIO_REG_HIMR, 4, (u8*)&tmp);
		DBG_871X_LEVEL(_drv_info_, "read again SDIO_REG_HIMR: 0x%08x\n", tmp);
		ret = _SUCCESS;
	}else {
		DBG_871X_LEVEL(_drv_info_, "sdio_local_write fail\n");
		ret = _FAIL;
	}

	return ret;
}
#endif //CONFIG_SDIO_HCI, CONFIG_GSPI_HCI

#ifdef CONFIG_GTK_OL
static void rtw_hal_fw_sync_cam_id(_adapter* adapter)
{
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	u8 null_addr[] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
	int cam_id;
	u32 algorithm = 0;
	u16 ctrl = 0;
	u8 *addr;
	u8 index = 0;
	u8 get_key[16];

	addr = get_bssid(&adapter->mlmepriv);

	if (addr == NULL) {
		DBG_871X("%s: get bssid MAC addr fail!!\n", __func__);
		return;
	}

	do{
		cam_id = rtw_camid_search(adapter, addr, index);
		if (cam_id == -1) {
			DBG_871X("%s: cam_id: %d, key_id:%d\n",
					__func__, cam_id, index);
		} else if (rtw_camid_is_gk(adapter, cam_id) != _TRUE) {
			DBG_871X("%s: cam_id: %d key_id(%d) is not GK\n",
					__func__, cam_id, index);
		} else {
			read_cam(adapter ,cam_id, get_key);
			algorithm = psecuritypriv->dot11PrivacyAlgrthm;
			ctrl = BIT(15) | BIT6 |(algorithm << 2) | index;
			write_cam(adapter, index, ctrl, addr, get_key);
			ctrl = 0;
			write_cam(adapter, cam_id, ctrl, null_addr, get_key);
		}
		index++;
	}while(index < 4);

	rtw_write8(adapter, REG_SECCFG, 0xcc);
}

static void rtw_hal_update_gtk_offload_info(_adapter* adapter)
{
	struct security_priv *psecuritypriv = &adapter->securitypriv;
	u8 defualt_cam_id=0;
	u8 cam_id=5;
	u8 *addr;
	u8 null_addr[] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0};
	u8 gtk_keyindex=0;
	u8 get_key[16];
	u8 index = 1;
	u16 ctrl = 0;
	u32 algorithm = 0;

	addr = get_bssid(&adapter->mlmepriv);

	if (addr == NULL) {
		DBG_871X("%s: get bssid MAC addr fail!!\n", __func__);
		return;
	}

	_rtw_memset(get_key, 0, sizeof(get_key));

	algorithm = psecuritypriv->dot11PrivacyAlgrthm;

	if(psecuritypriv->binstallKCK_KEK == _TRUE) {

		//read gtk key index
		gtk_keyindex = rtw_read8(adapter, 0x48c);
		do{
			//chech if GK
			if(read_phy_cam_is_gtk(adapter, defualt_cam_id) == _TRUE)
			{
				read_cam(adapter ,defualt_cam_id, get_key);
				algorithm = psecuritypriv->dot11PrivacyAlgrthm;
				//in defualt cam entry, cam id = key id
				ctrl = BIT(15) | BIT6 |(algorithm << 2) | defualt_cam_id;
				write_cam(adapter, cam_id, ctrl, addr, get_key);
				cam_id++;
				ctrl = 0;
				write_cam(adapter, defualt_cam_id, ctrl, null_addr, get_key);
			}

			if (gtk_keyindex < 4 &&(defualt_cam_id == gtk_keyindex)) {
				psecuritypriv->dot118021XGrpKeyid = gtk_keyindex;
				_rtw_memcpy(psecuritypriv->dot118021XGrpKey[psecuritypriv->dot118021XGrpKeyid].skey,
						get_key, 16);

				DBG_871X_LEVEL(_drv_always_, "GTK (%d) = 0x%08x, 0x%08x, 0x%08x, 0x%08x\n",
						gtk_keyindex,
				psecuritypriv->dot118021XGrpKey[psecuritypriv->dot118021XGrpKeyid].lkey[0], 
				psecuritypriv->dot118021XGrpKey[psecuritypriv->dot118021XGrpKeyid].lkey[1],
				psecuritypriv->dot118021XGrpKey[psecuritypriv->dot118021XGrpKeyid].lkey[2],
				psecuritypriv->dot118021XGrpKey[psecuritypriv->dot118021XGrpKeyid].lkey[3]);
			}
			defualt_cam_id++;
		}while(defualt_cam_id < 4);

		rtw_write8(adapter, REG_SECCFG, 0x0c);
#ifdef CONFIG_GTK_OL_DBG
		//if (gtk_keyindex != 5)
		dump_cam_table(adapter);
#endif
	}
}
#endif

static void rtw_hal_update_tx_iv(_adapter* adapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	u64 iv_low = 0, iv_high = 0;

	// 3.1 read fw iv
	iv_low = rtw_read32(adapter, REG_TXPKTBUF_IV_LOW);
	//only low two bytes is PN, check AES_IV macro for detail
	iv_low &= 0xffff;
	iv_high = rtw_read32(adapter, REG_TXPKTBUF_IV_HIGH);
	//get the real packet number
	pwrctl->wowlan_fw_iv = iv_high << 16 | iv_low;
	DBG_871X_LEVEL(_drv_always_,
			"fw_iv: 0x%016llx\n", pwrctl->wowlan_fw_iv);
	//Update TX iv data.
	rtw_set_sec_pn(adapter);
}

static u8 rtw_hal_set_keep_alive_cmd(_adapter *adapter, u8 enable, u8 pkt_type)
{
	struct hal_ops *pHalFunc = &adapter->HalFunc;

	u8 u1H2CKeepAliveParm[H2C_KEEP_ALIVE_CTRL_LEN]={0};
	u8 adopt = 1, check_period = 5;
	u8 ret = _FAIL;

	DBG_871X("%s(): enable = %d\n", __func__, enable);
	SET_H2CCMD_KEEPALIVE_PARM_ENABLE(u1H2CKeepAliveParm, enable);
	SET_H2CCMD_KEEPALIVE_PARM_ADOPT(u1H2CKeepAliveParm, adopt);
	SET_H2CCMD_KEEPALIVE_PARM_PKT_TYPE(u1H2CKeepAliveParm, pkt_type);
	SET_H2CCMD_KEEPALIVE_PARM_CHECK_PERIOD(u1H2CKeepAliveParm, check_period);

	ret = rtw_hal_fill_h2c_cmd(adapter,
				H2C_KEEP_ALIVE,
				H2C_KEEP_ALIVE_CTRL_LEN,
				u1H2CKeepAliveParm);

	return ret;
}

static u8 rtw_hal_set_disconnect_decision_cmd(_adapter *adapter, u8 enable)
{
	struct hal_ops *pHalFunc = &adapter->HalFunc;
	u8 u1H2CDisconDecisionParm[H2C_DISCON_DECISION_LEN]={0};
	u8 adopt = 1, check_period = 10, trypkt_num = 0;
	u8 ret = _FAIL;

	DBG_871X("%s(): enable = %d\n", __func__, enable);
	SET_H2CCMD_DISCONDECISION_PARM_ENABLE(u1H2CDisconDecisionParm, enable);
	SET_H2CCMD_DISCONDECISION_PARM_ADOPT(u1H2CDisconDecisionParm, adopt);
	SET_H2CCMD_DISCONDECISION_PARM_CHECK_PERIOD(u1H2CDisconDecisionParm, check_period);
	SET_H2CCMD_DISCONDECISION_PARM_TRY_PKT_NUM(u1H2CDisconDecisionParm, trypkt_num);

	ret = rtw_hal_fill_h2c_cmd(adapter,
				H2C_DISCON_DECISION,
				H2C_DISCON_DECISION_LEN,
				u1H2CDisconDecisionParm);
	return ret;
}

static u8 rtw_hal_set_ap_offload_ctrl_cmd(_adapter *adapter, u8 enable)
{
	struct hal_ops *pHalFunc = &adapter->HalFunc;
	u8 u1H2CAPOffloadCtrlParm[H2C_WOWLAN_LEN]={0};
	u8 ret = _FAIL;

	DBG_871X("%s(): bFuncEn=%d\n", __func__, enable);

	SET_H2CCMD_AP_WOWLAN_EN(u1H2CAPOffloadCtrlParm, enable);

	ret = rtw_hal_fill_h2c_cmd(adapter,
				H2C_AP_OFFLOAD,
				H2C_AP_OFFLOAD_LEN,
				u1H2CAPOffloadCtrlParm);
	return ret;
}

static u8 rtw_hal_set_ap_rsvdpage_loc_cmd(_adapter *adapter,
		PRSVDPAGE_LOC rsvdpageloc)
{
	struct hal_ops *pHalFunc = &adapter->HalFunc;
	u8 rsvdparm[H2C_AOAC_RSVDPAGE_LOC_LEN]={0};
	u8 ret = _FAIL, header = 0;

	header = rtw_read8(adapter, REG_BCNQ_BDNY);

	DBG_871X("%s: beacon: %d, probeRsp: %d, header:0x%02x\n", __func__,
			rsvdpageloc->LocApOffloadBCN,
			rsvdpageloc->LocProbeRsp,
			header);

	SET_H2CCMD_AP_WOWLAN_RSVDPAGE_LOC_BCN(rsvdparm,
			rsvdpageloc->LocApOffloadBCN + header);

	ret = rtw_hal_fill_h2c_cmd(adapter, H2C_BCN_RSVDPAGE,
				H2C_BCN_RSVDPAGE_LEN, rsvdparm);

	if (ret == _FAIL)
		DBG_871X("%s: H2C_BCN_RSVDPAGE cmd fail\n", __func__);

	_rtw_memset(&rsvdparm, 0, sizeof(rsvdparm));

	SET_H2CCMD_AP_WOWLAN_RSVDPAGE_LOC_ProbeRsp(rsvdparm,
			rsvdpageloc->LocProbeRsp + header);

	ret = rtw_hal_fill_h2c_cmd(adapter, H2C_PROBERSP_RSVDPAGE,
				H2C_PROBERSP_RSVDPAGE_LEN, rsvdparm);

	if (ret == _FAIL)
		DBG_871X("%s: H2C_PROBERSP_RSVDPAGE cmd fail\n", __func__);

	return ret;
}

static u8 rtw_hal_set_wowlan_ctrl_cmd(_adapter *adapter, u8 enable)
{
	struct security_priv *psecpriv = &adapter->securitypriv;
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(adapter);
	struct hal_ops *pHalFunc = &adapter->HalFunc;

	u8 u1H2CWoWlanCtrlParm[H2C_WOWLAN_LEN]={0};
	u8 discont_wake = 1, gpionum = 0, gpio_dur = 0;
	u8 hw_unicast = 0, gpio_pulse_cnt=0;
	u8 sdio_wakeup_enable = 1;
	u8 gpio_high_active = 0; //0: low active, 1: high active
	u8 magic_pkt = 0;
	u8 ret = _FAIL;

#ifdef CONFIG_GPIO_WAKEUP
	gpionum = WAKEUP_GPIO_IDX;
	sdio_wakeup_enable = 0;
#endif //CONFIG_GPIO_WAKEUP

	if (!ppwrpriv->wowlan_pno_enable)
		magic_pkt = enable;

	if (psecpriv->dot11PrivacyAlgrthm == _WEP40_ || psecpriv->dot11PrivacyAlgrthm == _WEP104_)
		hw_unicast = 1;
	else
		hw_unicast = 0;

	DBG_871X("%s(): enable=%d\n", __func__, enable);

	SET_H2CCMD_WOWLAN_FUNC_ENABLE(u1H2CWoWlanCtrlParm, enable);
	SET_H2CCMD_WOWLAN_PATTERN_MATCH_ENABLE(u1H2CWoWlanCtrlParm, 0);
	SET_H2CCMD_WOWLAN_MAGIC_PKT_ENABLE(u1H2CWoWlanCtrlParm, magic_pkt);
	SET_H2CCMD_WOWLAN_UNICAST_PKT_ENABLE(u1H2CWoWlanCtrlParm, hw_unicast);
	SET_H2CCMD_WOWLAN_ALL_PKT_DROP(u1H2CWoWlanCtrlParm, 0);
	SET_H2CCMD_WOWLAN_GPIO_ACTIVE(u1H2CWoWlanCtrlParm, gpio_high_active);
#ifndef CONFIG_GTK_OL
	SET_H2CCMD_WOWLAN_REKEY_WAKE_UP(u1H2CWoWlanCtrlParm, enable);
#endif
	SET_H2CCMD_WOWLAN_DISCONNECT_WAKE_UP(u1H2CWoWlanCtrlParm, discont_wake);
	SET_H2CCMD_WOWLAN_GPIONUM(u1H2CWoWlanCtrlParm, gpionum);
	SET_H2CCMD_WOWLAN_DATAPIN_WAKE_UP(u1H2CWoWlanCtrlParm, sdio_wakeup_enable);
	SET_H2CCMD_WOWLAN_GPIO_DURATION(u1H2CWoWlanCtrlParm, gpio_dur);
#ifdef CONFIG_PLATFORM_ARM_RK3188
	SET_H2CCMD_WOWLAN_GPIO_PULSE_EN(u1H2CWoWlanCtrlParm, 1);
	SET_H2CCMD_WOWLAN_GPIO_PULSE_COUNT(u1H2CWoWlanCtrlParm, 0x04);
#else
	SET_H2CCMD_WOWLAN_GPIO_PULSE_EN(u1H2CWoWlanCtrlParm, 0);
	SET_H2CCMD_WOWLAN_GPIO_PULSE_COUNT(u1H2CWoWlanCtrlParm, gpio_pulse_cnt);
#endif

	ret = rtw_hal_fill_h2c_cmd(adapter,
				H2C_WOWLAN,
				H2C_WOWLAN_LEN,
				u1H2CWoWlanCtrlParm);
	return ret;
}

static u8 rtw_hal_set_remote_wake_ctrl_cmd(_adapter *adapter, u8 enable)
{
	struct hal_ops *pHalFunc = &adapter->HalFunc;
	struct security_priv* psecuritypriv=&(adapter->securitypriv);
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(adapter);
	u8 u1H2CRemoteWakeCtrlParm[H2C_REMOTE_WAKE_CTRL_LEN]={0};
	u8 ret = _FAIL, count = 0;

	DBG_871X("%s(): enable=%d\n", __func__, enable);

	if (!ppwrpriv->wowlan_pno_enable) {
		SET_H2CCMD_REMOTE_WAKECTRL_ENABLE(
				u1H2CRemoteWakeCtrlParm, enable);
		SET_H2CCMD_REMOTE_WAKE_CTRL_ARP_OFFLOAD_EN(
				u1H2CRemoteWakeCtrlParm, 1);
#ifdef CONFIG_GTK_OL
		if (psecuritypriv->binstallKCK_KEK == _TRUE &&
				psecuritypriv->dot11PrivacyAlgrthm == _AES_) {
			SET_H2CCMD_REMOTE_WAKE_CTRL_GTK_OFFLOAD_EN(
					u1H2CRemoteWakeCtrlParm, 1);
		} else {
			DBG_871X("no kck or security is not AES\n");
			SET_H2CCMD_REMOTE_WAKE_CTRL_GTK_OFFLOAD_EN(
					u1H2CRemoteWakeCtrlParm, 0);
		}
#endif //CONFIG_GTK_OL

		SET_H2CCMD_REMOTE_WAKE_CTRL_FW_UNICAST_EN(
				u1H2CRemoteWakeCtrlParm, 1);

		/*
		 * filter NetBios name service pkt to avoid being waked-up
		 * by this kind of unicast pkt this exceptional modification 
		 * is used for match competitor's behavior
		 */
		SET_H2CCMD_REMOTE_WAKE_CTRL_NBNS_FILTER_EN(
				u1H2CRemoteWakeCtrlParm, 1);
		
		if ((psecuritypriv->dot11PrivacyAlgrthm == _AES_) ||
			(psecuritypriv->dot11PrivacyAlgrthm == _NO_PRIVACY_)) {
			SET_H2CCMD_REMOTE_WAKE_CTRL_ARP_ACTION(
					u1H2CRemoteWakeCtrlParm, 0);
		} else {
			SET_H2CCMD_REMOTE_WAKE_CTRL_ARP_ACTION(
					u1H2CRemoteWakeCtrlParm, 1);
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
	if (_TRUE == ppwrpriv->wowlan_p2p_mode)
	{
		DBG_871X("P2P OFFLOAD ENABLE\n");
		SET_H2CCMD_REMOTE_WAKE_CTRL_P2P_OFFLAD_EN(u1H2CRemoteWakeCtrlParm,1);
	}
	else
	{
		DBG_871X("P2P OFFLOAD DISABLE\n");
		SET_H2CCMD_REMOTE_WAKE_CTRL_P2P_OFFLAD_EN(u1H2CRemoteWakeCtrlParm,0);
	}
#endif //CONFIG_P2P_WOWLAN


	ret = rtw_hal_fill_h2c_cmd(adapter,
				H2C_REMOTE_WAKE_CTRL,
				H2C_REMOTE_WAKE_CTRL_LEN,
				u1H2CRemoteWakeCtrlParm);
	return ret;
}

static u8 rtw_hal_set_global_info_cmd(_adapter* adapter, u8 group_alg, u8 pairwise_alg)
{
	struct hal_ops *pHalFunc = &adapter->HalFunc;
	u8 ret = _FAIL;
	u8 u1H2CAOACGlobalInfoParm[H2C_AOAC_GLOBAL_INFO_LEN]={0};

	DBG_871X("%s(): group_alg=%d pairwise_alg=%d\n",
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
static u8 rtw_hal_set_scan_offload_info_cmd(_adapter* adapter,
		PRSVDPAGE_LOC rsvdpageloc, u8 enable)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	struct hal_ops *pHalFunc = &adapter->HalFunc;

	u8 u1H2CScanOffloadInfoParm[H2C_SCAN_OFFLOAD_CTRL_LEN]={0};
	u8 res = 0, count = 0, ret = _FAIL;

	DBG_871X("%s: loc_probe_packet:%d, loc_scan_info: %d loc_ssid_info:%d\n",
		__func__, rsvdpageloc->LocProbePacket,
		rsvdpageloc->LocScanInfo, rsvdpageloc->LocSSIDInfo);

	SET_H2CCMD_AOAC_NLO_FUN_EN(u1H2CScanOffloadInfoParm, enable);
	SET_H2CCMD_AOAC_NLO_IPS_EN(u1H2CScanOffloadInfoParm, enable);
	SET_H2CCMD_AOAC_RSVDPAGE_LOC_SCAN_INFO(u1H2CScanOffloadInfoParm,
			rsvdpageloc->LocScanInfo);
	SET_H2CCMD_AOAC_RSVDPAGE_LOC_PROBE_PACKET(u1H2CScanOffloadInfoParm,
			rsvdpageloc->LocProbePacket);
	SET_H2CCMD_AOAC_RSVDPAGE_LOC_SSID_INFO(u1H2CScanOffloadInfoParm,
			rsvdpageloc->LocSSIDInfo);

	ret = rtw_hal_fill_h2c_cmd(adapter,
				H2C_D0_SCAN_OFFLOAD_INFO,
				H2C_SCAN_OFFLOAD_CTRL_LEN,
				u1H2CScanOffloadInfoParm);
	return ret;
}
#endif //CONFIG_PNO_SUPPORT

void rtw_hal_set_fw_wow_related_cmd(_adapter* padapter, u8 enable)
{
	struct security_priv *psecpriv = &padapter->securitypriv;
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct sta_info *psta = NULL;
	u16 media_status_rpt;
	u8	pkt_type = 0;
	u8 ret = _SUCCESS;

	DBG_871X_LEVEL(_drv_always_, "+%s()+: enable=%d\n", __func__, enable);
_func_enter_;

	rtw_hal_set_wowlan_ctrl_cmd(padapter, enable);

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
#endif //CONFIG_ARP_KEEP_ALIVE
			rtw_hal_set_keep_alive_cmd(padapter, enable, pkt_type);
		}
		rtw_hal_set_remote_wake_ctrl_cmd(padapter, enable);
#ifdef CONFIG_PNO_SUPPORT
		rtw_hal_check_pno_enabled(padapter);
#endif //CONFIG_PNO_SUPPORT
	} else {
#if 0
		{
			u32 PageSize = 0;
			rtw_hal_get_def_var(adapter, HAL_DEF_TX_PAGE_SIZE, (u8 *)&PageSize);
			dump_TX_FIFO(padapter, 4, PageSize);
		}
#endif

		rtw_hal_set_remote_wake_ctrl_cmd(padapter, enable);
		rtw_hal_set_wowlan_ctrl_cmd(padapter, enable);
	}
_func_exit_;
	DBG_871X_LEVEL(_drv_always_, "-%s()-\n", __func__);
}
#endif //CONFIG_WOWLAN

#ifdef CONFIG_P2P_WOWLAN
static int update_hidden_ssid(u8 *ies, u32 ies_len, u8 hidden_ssid_mode)
{
	u8 *ssid_ie;
	sint ssid_len_ori;
	int len_diff = 0;
	
	ssid_ie = rtw_get_ie(ies,  WLAN_EID_SSID, &ssid_len_ori, ies_len);

	//DBG_871X("%s hidden_ssid_mode:%u, ssid_ie:%p, ssid_len_ori:%d\n", __FUNCTION__, hidden_ssid_mode, ssid_ie, ssid_len_ori);
	
	if(ssid_ie && ssid_len_ori>0)
	{
		switch(hidden_ssid_mode)
		{
			case 1:
			{
				u8 *next_ie = ssid_ie + 2 + ssid_len_ori;
				u32 remain_len = 0;
				
				remain_len = ies_len -(next_ie-ies);
				
				ssid_ie[1] = 0;				
				_rtw_memcpy(ssid_ie+2, next_ie, remain_len);
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
	//struct xmit_frame	*pmgntframe;
	//struct pkt_attrib	*pattrib;
	//unsigned char	*pframe;
	struct rtw_ieee80211_hdr *pwlanhdr;
	unsigned short *fctrl;
	unsigned int	rate_len;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	u32	pktlen;
//#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
//	_irqL irqL;
//	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
//#endif //#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
#endif //CONFIG_P2P

	//for debug
	u8 *dbgbuf = pframe;
	u8 dbgbufLen = 0, index = 0;

	DBG_871X("%s\n", __FUNCTION__);
//#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
//	_enter_critical_bh(&pmlmepriv->bcn_update_lock, &irqL);
//#endif //#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
		
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;	
	
	
	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	
	_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(cur_network), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0/*pmlmeext->mgnt_seq*/);
	//pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_BEACON);
	
	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);	
	pktlen = sizeof (struct rtw_ieee80211_hdr_3addr);
	
	if( (pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
	{
		//DBG_871X("ie len=%d\n", cur_network->IELength);
#ifdef CONFIG_P2P
		// for P2P : Primary Device Type & Device Name
		u32 wpsielen=0, insert_len=0;
		u8 *wpsie=NULL;		
		wpsie = rtw_get_wps_ie(cur_network->IEs+_FIXED_IE_LENGTH_, cur_network->IELength-_FIXED_IE_LENGTH_, NULL, &wpsielen);
		
		if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO) && wpsie && wpsielen>0)
		{
			uint wps_offset, remainder_ielen;
			u8 *premainder_ie, *pframe_wscie;
	
			wps_offset = (uint)(wpsie - cur_network->IEs);

			premainder_ie = wpsie + wpsielen;

			remainder_ielen = cur_network->IELength - wps_offset - wpsielen;

#ifdef CONFIG_IOCTL_CFG80211
			if(pwdinfo->driver_interface == DRIVER_CFG80211 )
			{
				if(pmlmepriv->wps_beacon_ie && pmlmepriv->wps_beacon_ie_len>0)
				{
					_rtw_memcpy(pframe, cur_network->IEs, wps_offset);
					pframe += wps_offset;
					pktlen += wps_offset;

					_rtw_memcpy(pframe, pmlmepriv->wps_beacon_ie, pmlmepriv->wps_beacon_ie_len);
					pframe += pmlmepriv->wps_beacon_ie_len;
					pktlen += pmlmepriv->wps_beacon_ie_len;

					//copy remainder_ie to pframe
					_rtw_memcpy(pframe, premainder_ie, remainder_ielen);
					pframe += remainder_ielen;		
					pktlen += remainder_ielen;
				}
				else
				{
					_rtw_memcpy(pframe, cur_network->IEs, cur_network->IELength);
					pframe += cur_network->IELength;
					pktlen += cur_network->IELength;
				}
			}
			else
#endif //CONFIG_IOCTL_CFG80211
			{
				pframe_wscie = pframe + wps_offset;
				_rtw_memcpy(pframe, cur_network->IEs, wps_offset+wpsielen);			
				pframe += (wps_offset + wpsielen);		
				pktlen += (wps_offset + wpsielen);

				//now pframe is end of wsc ie, insert Primary Device Type & Device Name
				//	Primary Device Type
				//	Type:
				*(u16*) ( pframe + insert_len) = cpu_to_be16( WPS_ATTR_PRIMARY_DEV_TYPE );
				insert_len += 2;
				
				//	Length:
				*(u16*) ( pframe + insert_len ) = cpu_to_be16( 0x0008 );
				insert_len += 2;
				
				//	Value:
				//	Category ID
				*(u16*) ( pframe + insert_len ) = cpu_to_be16( WPS_PDT_CID_MULIT_MEDIA );
				insert_len += 2;

				//	OUI
				*(u32*) ( pframe + insert_len ) = cpu_to_be32( WPSOUI );
				insert_len += 4;

				//	Sub Category ID
				*(u16*) ( pframe + insert_len ) = cpu_to_be16( WPS_PDT_SCID_MEDIA_SERVER );
				insert_len += 2;


				//	Device Name
				//	Type:
				*(u16*) ( pframe + insert_len ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
				insert_len += 2;

				//	Length:
				*(u16*) ( pframe + insert_len ) = cpu_to_be16( pwdinfo->device_name_len );
				insert_len += 2;

				//	Value:
				_rtw_memcpy( pframe + insert_len, pwdinfo->device_name, pwdinfo->device_name_len );
				insert_len += pwdinfo->device_name_len;


				//update wsc ie length
				*(pframe_wscie+1) = (wpsielen -2) + insert_len;

				//pframe move to end
				pframe+=insert_len;
				pktlen += insert_len;

				//copy remainder_ie to pframe
				_rtw_memcpy(pframe, premainder_ie, remainder_ielen);
				pframe += remainder_ielen;		
				pktlen += remainder_ielen;
			}
		}
		else
#endif //CONFIG_P2P
		{
			int len_diff;
			_rtw_memcpy(pframe, cur_network->IEs, cur_network->IELength);
			len_diff = update_hidden_ssid(
				pframe+_BEACON_IE_OFFSET_
				, cur_network->IELength-_BEACON_IE_OFFSET_
				, pmlmeinfo->hidden_ssid_mode
			);
			pframe += (cur_network->IELength+len_diff);
			pktlen += (cur_network->IELength+len_diff);
		}
#if 0
		{
			u8 *wps_ie;
			uint wps_ielen;
			u8 sr = 0;
			wps_ie = rtw_get_wps_ie(pmgntframe->buf_addr+TXDESC_OFFSET+sizeof (struct rtw_ieee80211_hdr_3addr)+_BEACON_IE_OFFSET_,
				pattrib->pktlen-sizeof (struct rtw_ieee80211_hdr_3addr)-_BEACON_IE_OFFSET_, NULL, &wps_ielen);
			if (wps_ie && wps_ielen>0) {
				rtw_get_wps_attr_content(wps_ie,  wps_ielen, WPS_ATTR_SELECTED_REGISTRAR, (u8*)(&sr), NULL);
			}
			if (sr != 0)
				set_fwstate(pmlmepriv, WIFI_UNDER_WPS);
			else
				_clr_fwstate_(pmlmepriv, WIFI_UNDER_WPS);
		}
#endif 
#ifdef CONFIG_P2P
		if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
		{
			u32 len;
#ifdef CONFIG_IOCTL_CFG80211
			if(pwdinfo->driver_interface == DRIVER_CFG80211 )
			{
				len = pmlmepriv->p2p_beacon_ie_len;
				if(pmlmepriv->p2p_beacon_ie && len>0)				
					_rtw_memcpy(pframe, pmlmepriv->p2p_beacon_ie, len);
			}
			else
#endif //CONFIG_IOCTL_CFG80211
			{
				len = build_beacon_p2p_ie(pwdinfo, pframe);
			}

			pframe += len;
			pktlen += len;
#ifdef CONFIG_WFD
#ifdef CONFIG_IOCTL_CFG80211
			if(_TRUE == pwdinfo->wfd_info->wfd_enable)
#endif //CONFIG_IOCTL_CFG80211
			{
			len = build_beacon_wfd_ie( pwdinfo, pframe );
			}
#ifdef CONFIG_IOCTL_CFG80211
			else
			{	
				len = 0;
				if(pmlmepriv->wfd_beacon_ie && pmlmepriv->wfd_beacon_ie_len>0)
				{
					len = pmlmepriv->wfd_beacon_ie_len;
					_rtw_memcpy(pframe, pmlmepriv->wfd_beacon_ie, len);	
				}
			}		
#endif //CONFIG_IOCTL_CFG80211
			pframe += len;
			pktlen += len;
#endif //CONFIG_WFD
		}
#endif //CONFIG_P2P

		goto _issue_bcn;

	}

	//below for ad-hoc mode

	//timestamp will be inserted by hardware
	pframe += 8;
	pktlen += 8;

	// beacon interval: 2 bytes

	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2); 

	pframe += 2;
	pktlen += 2;

	// capability info: 2 bytes

	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pktlen += 2;

	// SSID
	pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pktlen);

	// supported rates...
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8)? 8: rate_len), cur_network->SupportedRates, &pktlen);

	// DS parameter set
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pktlen);

	//if( (pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
	{
		u8 erpinfo=0;
		u32 ATIMWindow;
		// IBSS Parameter Set...
		//ATIMWindow = cur->Configuration.ATIMWindow;
		ATIMWindow = 0;
		pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pktlen);

		//ERP IE
		pframe = rtw_set_ie(pframe, _ERPINFO_IE_, 1, &erpinfo, &pktlen);
	}	


	// EXTERNDED SUPPORTED RATE
	if (rate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pktlen);
	}


	//todo:HT for adhoc

_issue_bcn:

//#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)
//	pmlmepriv->update_bcn = _FALSE;
//	
//	_exit_critical_bh(&pmlmepriv->bcn_update_lock, &irqL);	
//#endif //#if defined (CONFIG_AP_MODE) && defined (CONFIG_NATIVEAP_MLME)

	*pLength = pktlen;
#if 0
	// printf dbg msg
	dbgbufLen = pktlen;
	DBG_871X("======> DBG MSG FOR CONSTRAUCT P2P BEACON\n");

	for(index=0;index<dbgbufLen;index++)
		printk("%x ",*(dbgbuf+index));

	printk("\n");
	DBG_871X("<====== DBG MSG FOR CONSTRAUCT P2P BEACON\n");
	
#endif
}

static int get_reg_classes_full_count(struct p2p_channels channel_list) {
	int cnt = 0;
	int i;

	for (i = 0; i < channel_list.reg_classes; i++) {
		cnt += channel_list.reg_class[i].channels;
	}

	return cnt;
}

static void rtw_hal_construct_P2PProbeRsp(_adapter *padapter, u8 *pframe, u32 *pLength)
{
	//struct xmit_frame			*pmgntframe;
	//struct pkt_attrib			*pattrib;
	//unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;	
	unsigned char					*mac;
	struct xmit_priv	*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	//WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	u16					beacon_interval = 100;
	u16					capInfo = 0;
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
	u8					wpsie[255] = { 0x00 };
	u32					wpsielen = 0, p2pielen = 0;
	u32					pktlen;
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif //CONFIG_WFD
#ifdef CONFIG_INTEL_WIDI
	u8 zero_array_check[L2SDTA_SERVICE_VE_LEN] = { 0x00 };
#endif //CONFIG_INTEL_WIDI

	//for debug
	u8 *dbgbuf = pframe;
	u8 dbgbufLen = 0, index = 0;

	DBG_871X("%s\n", __FUNCTION__);
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;	
	
	mac = adapter_mac_addr(padapter);
	
	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	//DA filled by FW
	_rtw_memset(pwlanhdr->addr1, 0, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, mac, ETH_ALEN);
	
	//	Use the device address for BSSID field.	
	_rtw_memcpy(pwlanhdr->addr3, mac, ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	SetFrameSubType(fctrl, WIFI_PROBERSP);

 	pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);
 	pframe += pktlen;


	//timestamp will be inserted by hardware
	pframe += 8;
	pktlen += 8;

	// beacon interval: 2 bytes
	_rtw_memcpy(pframe, (unsigned char *) &beacon_interval, 2); 
	pframe += 2;
	pktlen += 2;

	//	capability info: 2 bytes
	//	ESS and IBSS bits must be 0 (defined in the 3.1.2.1.1 of WiFi Direct Spec)
	capInfo |= cap_ShortPremble;
	capInfo |= cap_ShortSlot;
	
	_rtw_memcpy(pframe, (unsigned char *) &capInfo, 2);
	pframe += 2;
	pktlen += 2;


	// SSID
	pframe = rtw_set_ie(pframe, _SSID_IE_, 7, pwdinfo->p2p_wildcard_ssid, &pktlen);

	// supported rates...
	//	Use the OFDM rate in the P2P probe response frame. ( 6(B), 9(B), 12, 18, 24, 36, 48, 54 )
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, 8, pwdinfo->support_rate, &pktlen);

	// DS parameter set
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&pwdinfo->listen_channel, &pktlen);

#ifdef CONFIG_IOCTL_CFG80211
	if(pwdinfo->driver_interface == DRIVER_CFG80211 )
	{
		if( pmlmepriv->wps_probe_resp_ie != NULL && pmlmepriv->p2p_probe_resp_ie != NULL )
		{
			//WPS IE
			_rtw_memcpy(pframe, pmlmepriv->wps_probe_resp_ie, pmlmepriv->wps_probe_resp_ie_len);
			pktlen += pmlmepriv->wps_probe_resp_ie_len;
			pframe += pmlmepriv->wps_probe_resp_ie_len;

			//P2P IE
			_rtw_memcpy(pframe, pmlmepriv->p2p_probe_resp_ie, pmlmepriv->p2p_probe_resp_ie_len);
			pktlen += pmlmepriv->p2p_probe_resp_ie_len;
			pframe += pmlmepriv->p2p_probe_resp_ie_len;
		}
	}
	else
#endif //CONFIG_IOCTL_CFG80211		
	{

		//	Todo: WPS IE
		//	Noted by Albert 20100907
		//	According to the WPS specification, all the WPS attribute is presented by Big Endian.

		wpsielen = 0;
		//	WPS OUI
		*(u32*) ( wpsie ) = cpu_to_be32( WPSOUI );
		wpsielen += 4;

		//	WPS version
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_VER1 );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
		wpsielen += 2;

		//	Value:
		wpsie[wpsielen++] = WPS_VERSION_1;	//	Version 1.0

#ifdef CONFIG_INTEL_WIDI
		//	Commented by Kurt
		//	Appended WiDi info. only if we did issued_probereq_widi(), and then we saved ven. ext. in pmlmepriv->sa_ext.
		if(  _rtw_memcmp(pmlmepriv->sa_ext, zero_array_check, L2SDTA_SERVICE_VE_LEN) == _FALSE 
			|| pmlmepriv->num_p2p_sdt != 0 )
		{
			//Sec dev type
			*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_SEC_DEV_TYPE_LIST );
			wpsielen += 2;

			//	Length:
			*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0008 );
			wpsielen += 2;

			//	Value:
			//	Category ID
			*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_PDT_CID_DISPLAYS );
			wpsielen += 2;

			//	OUI
			*(u32*) ( wpsie + wpsielen ) = cpu_to_be32( INTEL_DEV_TYPE_OUI );
			wpsielen += 4;

			*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_PDT_SCID_WIDI_CONSUMER_SINK );
			wpsielen += 2;

			if(  _rtw_memcmp(pmlmepriv->sa_ext, zero_array_check, L2SDTA_SERVICE_VE_LEN) == _FALSE )
			{
				//	Vendor Extension
				_rtw_memcpy( wpsie + wpsielen, pmlmepriv->sa_ext, L2SDTA_SERVICE_VE_LEN );
				wpsielen += L2SDTA_SERVICE_VE_LEN;
			}
		}
#endif //CONFIG_INTEL_WIDI

		//	WiFi Simple Config State
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_SIMPLE_CONF_STATE );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
		wpsielen += 2;

		//	Value:
		wpsie[wpsielen++] = WPS_WSC_STATE_NOT_CONFIG;	//	Not Configured.

		//	Response Type
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_RESP_TYPE );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
		wpsielen += 2;

		//	Value:
		wpsie[wpsielen++] = WPS_RESPONSE_TYPE_8021X;

		//	UUID-E
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_UUID_E );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0010 );
		wpsielen += 2;

		//	Value:
		if (pwdinfo->external_uuid == 0) {
			_rtw_memset( wpsie + wpsielen, 0x0, 16 );
			_rtw_memcpy(wpsie + wpsielen, mac, ETH_ALEN);
		} else {
			_rtw_memcpy( wpsie + wpsielen, pwdinfo->uuid, 0x10 );
		}
		wpsielen += 0x10;

		//	Manufacturer
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_MANUFACTURER );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0007 );
		wpsielen += 2;

		//	Value:
		_rtw_memcpy( wpsie + wpsielen, "Realtek", 7 );
		wpsielen += 7;

		//	Model Name
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_MODEL_NAME );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0006 );
		wpsielen += 2;	

		//	Value:
		_rtw_memcpy( wpsie + wpsielen, "8192CU", 6 );
		wpsielen += 6;

		//	Model Number
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_MODEL_NUMBER );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
		wpsielen += 2;

		//	Value:
		wpsie[ wpsielen++ ] = 0x31;		//	character 1

		//	Serial Number
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_SERIAL_NUMBER );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( ETH_ALEN );
		wpsielen += 2;

		//	Value:
		_rtw_memcpy( wpsie + wpsielen, "123456" , ETH_ALEN );
		wpsielen += ETH_ALEN;

		//	Primary Device Type
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_PRIMARY_DEV_TYPE );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0008 );
		wpsielen += 2;

		//	Value:
		//	Category ID
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_PDT_CID_MULIT_MEDIA );
		wpsielen += 2;

		//	OUI
		*(u32*) ( wpsie + wpsielen ) = cpu_to_be32( WPSOUI );
		wpsielen += 4;

		//	Sub Category ID
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_PDT_SCID_MEDIA_SERVER );
		wpsielen += 2;

		//	Device Name
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( pwdinfo->device_name_len );
		wpsielen += 2;

		//	Value:
		_rtw_memcpy( wpsie + wpsielen, pwdinfo->device_name, pwdinfo->device_name_len );
		wpsielen += pwdinfo->device_name_len;

		//	Config Method
		//	Type:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_CONF_METHOD );
		wpsielen += 2;

		//	Length:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 );
		wpsielen += 2;

		//	Value:
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( pwdinfo->supported_wps_cm );
		wpsielen += 2;
		

		pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pktlen );
		

		p2pielen = build_probe_resp_p2p_ie(pwdinfo, pframe);
		pframe += p2pielen;
		pktlen += p2pielen;
	}

#ifdef CONFIG_WFD
#ifdef CONFIG_IOCTL_CFG80211
	if ( _TRUE == pwdinfo->wfd_info->wfd_enable )
#endif //CONFIG_IOCTL_CFG80211
	{
		wfdielen = build_probe_resp_wfd_ie(pwdinfo, pframe, 0);
		pframe += wfdielen;
		pktlen += wfdielen;
	}
#ifdef CONFIG_IOCTL_CFG80211
	else if (pmlmepriv->wfd_probe_resp_ie != NULL && pmlmepriv->wfd_probe_resp_ie_len>0)
	{
		//WFD IE
		_rtw_memcpy(pframe, pmlmepriv->wfd_probe_resp_ie, pmlmepriv->wfd_probe_resp_ie_len);
		pktlen += pmlmepriv->wfd_probe_resp_ie_len;
		pframe += pmlmepriv->wfd_probe_resp_ie_len;		
	}
#endif //CONFIG_IOCTL_CFG80211
#endif //CONFIG_WFD	

	*pLength = pktlen;

#if 0
	// printf dbg msg
	dbgbufLen = pktlen;
	DBG_871X("======> DBG MSG FOR CONSTRAUCT P2P Probe Rsp\n");

	for(index=0;index<dbgbufLen;index++)
		printk("%x ",*(dbgbuf+index));

	printk("\n");
	DBG_871X("<====== DBG MSG FOR CONSTRAUCT P2P Probe Rsp\n");
#endif
}
static void rtw_hal_construct_P2PNegoRsp(_adapter *padapter, u8 *pframe, u32 *pLength)
{
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_GO_NEGO_RESP;
	u8			wpsie[ 255 ] = { 0x00 }, p2pie[ 255 ] = { 0x00 };
	u8			p2pielen = 0, i;
	uint			wpsielen = 0;
	u16			wps_devicepassword_id = 0x0000;
	uint			wps_devicepassword_id_len = 0;
	u8			channel_cnt_24g = 0, channel_cnt_5gl = 0, channel_cnt_5gh;
	u16			len_channellist_attr = 0;
	u32			pktlen;
	u8			dialogToken = 0;
	
	//struct xmit_frame			*pmgntframe;
	//struct pkt_attrib			*pattrib;
	//unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo);
	//WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);

#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif //CONFIG_WFD

	//for debug
	u8 *dbgbuf = pframe;
	u8 dbgbufLen = 0, index = 0;

	DBG_871X( "%s\n", __FUNCTION__);
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	//RA, filled by FW
	_rtw_memset(pwlanhdr->addr1, 0, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, adapter_mac_addr(padapter), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	SetFrameSubType(pframe, WIFI_ACTION);

	pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	pframe += pktlen;

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pktlen));	
	
	//dialog token, filled by FW
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pktlen));

	_rtw_memset( wpsie, 0x00, 255 );
	wpsielen = 0;

	//	WPS Section
	wpsielen = 0;
	//	WPS OUI
	*(u32*) ( wpsie ) = cpu_to_be32( WPSOUI );
	wpsielen += 4;

	//	WPS version
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_VER1 );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
	wpsielen += 2;

	//	Value:
	wpsie[wpsielen++] = WPS_VERSION_1;	//	Version 1.0

	//	Device Password ID
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_DEVICE_PWID );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 );
	wpsielen += 2;

	//	Value:
	if ( wps_devicepassword_id == WPS_DPID_USER_SPEC )
	{
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_DPID_REGISTRAR_SPEC );
	}
	else if ( wps_devicepassword_id == WPS_DPID_REGISTRAR_SPEC )
	{
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_DPID_USER_SPEC );
	}
	else
	{
		*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_DPID_PBC );
	}
	wpsielen += 2;

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pktlen );


	//	P2P IE Section.

	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	//	Commented by Albert 20100908
	//	According to the P2P Specification, the group negoitation response frame should contain 9 P2P attributes
	//	1. Status
	//	2. P2P Capability
	//	3. Group Owner Intent
	//	4. Configuration Timeout
	//	5. Operating Channel
	//	6. Intended P2P Interface Address
	//	7. Channel List
	//	8. Device Info
	//	9. Group ID	( Only GO )


	//	ToDo:

	//	P2P Status
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_STATUS;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0001 );
	p2pielen += 2;

	//	Value, filled by FW
	p2pie[ p2pielen++ ] = 1;
	
	//	P2P Capability
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CAPABILITY;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	p2pielen += 2;

	//	Value:
	//	Device Capability Bitmap, 1 byte

	if ( rtw_p2p_chk_role(pwdinfo, P2P_ROLE_CLIENT) )
	{
		//	Commented by Albert 2011/03/08
		//	According to the P2P specification
		//	if the sending device will be client, the P2P Capability should be reserved of group negotation response frame
		p2pie[ p2pielen++ ] = 0;
	}
	else
	{
		//	Be group owner or meet the error case
		p2pie[ p2pielen++ ] = DMP_P2P_DEVCAP_SUPPORT;
	}
	
	//	Group Capability Bitmap, 1 byte
	if ( pwdinfo->persistent_supported )
	{
		p2pie[ p2pielen++ ] = P2P_GRPCAP_CROSS_CONN | P2P_GRPCAP_PERSISTENT_GROUP;
	}
	else
	{
		p2pie[ p2pielen++ ] = P2P_GRPCAP_CROSS_CONN;
	}

	//	Group Owner Intent
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_GO_INTENT;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0001 );
	p2pielen += 2;

	//	Value:
	if ( pwdinfo->peer_intent & 0x01 )
	{
		//	Peer's tie breaker bit is 1, our tie breaker bit should be 0
		p2pie[ p2pielen++ ] = ( pwdinfo->intent << 1 );
	}
	else
	{
		//	Peer's tie breaker bit is 0, our tie breaker bit should be 1
		p2pie[ p2pielen++ ] = ( ( pwdinfo->intent << 1 ) | BIT(0) );
	}


	//	Configuration Timeout
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CONF_TIMEOUT;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	p2pielen += 2;

	//	Value:
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P GO
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P Client

	//	Operating Channel
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_OPERATING_CH;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0005 );
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'X';
	p2pie[ p2pielen++ ] = 'X';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Operating Class
	if ( pwdinfo->operating_channel <= 14 )
	{
		//	Operating Class
		p2pie[ p2pielen++ ] = 0x51;
	}
	else if ( ( pwdinfo->operating_channel >= 36 ) && ( pwdinfo->operating_channel <= 48 ) )
	{
		//	Operating Class
		p2pie[ p2pielen++ ] = 0x73;
	}
	else
	{
		//	Operating Class
		p2pie[ p2pielen++ ] = 0x7c;
	}
	
	//	Channel Number
	p2pie[ p2pielen++ ] = pwdinfo->operating_channel;	//	operating channel number

	//	Intended P2P Interface Address	
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_INTENTED_IF_ADDR;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( ETH_ALEN );
	p2pielen += 2;

	//	Value:
	_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
	p2pielen += ETH_ALEN;

	//	Channel List
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CH_LIST;

	// Country String(3)
	// + ( Operating Class (1) + Number of Channels(1) ) * Operation Classes (?)
	// + number of channels in all classes
	len_channellist_attr = 3
	   + (1 + 1) * (u16)pmlmeext->channel_list.reg_classes
	   + get_reg_classes_full_count(pmlmeext->channel_list);

#ifdef CONFIG_CONCURRENT_MODE
	if ( check_buddy_fwstate(padapter, _FW_LINKED ) )
	{
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 5 + 1 );
	}
	else
	{
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( len_channellist_attr );
	}
#else

	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( len_channellist_attr );

 #endif
	p2pielen += 2;

	//	Value:
	//	Country String
	p2pie[ p2pielen++ ] = 'X';
	p2pie[ p2pielen++ ] = 'X';
	
	//	The third byte should be set to 0x04.
	//	Described in the "Operating Channel Attribute" section.
	p2pie[ p2pielen++ ] = 0x04;

	//	Channel Entry List

#ifdef CONFIG_CONCURRENT_MODE
	if ( check_buddy_fwstate(padapter, _FW_LINKED ) )
	{
		_adapter *pbuddy_adapter = padapter->pbuddy_adapter;	
		struct mlme_ext_priv	*pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;

		//	Operating Class
		if ( pbuddy_mlmeext->cur_channel > 14 )
		{
			if ( pbuddy_mlmeext->cur_channel >= 149 )
			{
				p2pie[ p2pielen++ ] = 0x7c;
			}
			else
			{
				p2pie[ p2pielen++ ] = 0x73;
			}
		}
		else
		{
			p2pie[ p2pielen++ ] = 0x51;
		}

		//	Number of Channels
		//	Just support 1 channel and this channel is AP's channel
		p2pie[ p2pielen++ ] = 1;

		//	Channel List
		p2pie[ p2pielen++ ] = pbuddy_mlmeext->cur_channel;
	}
	else
	{
		int i, j;
		for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
			//	Operating Class
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].reg_class;

			//	Number of Channels
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channels;

			//	Channel List
			for (i = 0; i < pmlmeext->channel_list.reg_class[j].channels; i++) {
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channel[i];
			}
		}
	}
#else // CONFIG_CONCURRENT_MODE
	{
		int i, j;
		for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
			//	Operating Class
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].reg_class;

			//	Number of Channels
			p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channels;

			//	Channel List
			for (i = 0; i < pmlmeext->channel_list.reg_class[j].channels; i++) {
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channel[i];
			}
		}
	}
#endif // CONFIG_CONCURRENT_MODE

	
	//	Device Info
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_DEVICE_INFO;

	//	Length:
	//	21 -> P2P Device Address (6bytes) + Config Methods (2bytes) + Primary Device Type (8bytes) 
	//	+ NumofSecondDevType (1byte) + WPS Device Name ID field (2bytes) + WPS Device Name Len field (2bytes)
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 21 + pwdinfo->device_name_len );
	p2pielen += 2;

	//	Value:
	//	P2P Device Address
	_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
	p2pielen += ETH_ALEN;

	//	Config Method
	//	This field should be big endian. Noted by P2P specification.

	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->supported_wps_cm );

	p2pielen += 2;

	//	Primary Device Type
	//	Category ID
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_CID_MULIT_MEDIA );
	p2pielen += 2;

	//	OUI
	*(u32*) ( p2pie + p2pielen ) = cpu_to_be32( WPSOUI );
	p2pielen += 4;

	//	Sub Category ID
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_PDT_SCID_MEDIA_SERVER );
	p2pielen += 2;

	//	Number of Secondary Device Types
	p2pie[ p2pielen++ ] = 0x00;	//	No Secondary Device Type List

	//	Device Name
	//	Type:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( WPS_ATTR_DEVICE_NAME );
	p2pielen += 2;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_be16( pwdinfo->device_name_len );
	p2pielen += 2;

	//	Value:
	_rtw_memcpy( p2pie + p2pielen, pwdinfo->device_name , pwdinfo->device_name_len );
	p2pielen += pwdinfo->device_name_len;	
	
	if ( rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO) )
	{
		//	Group ID Attribute
		//	Type:
		p2pie[ p2pielen++ ] = P2P_ATTR_GROUP_ID;

		//	Length:
		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( ETH_ALEN + pwdinfo->nego_ssidlen );
		p2pielen += 2;

		//	Value:
		//	p2P Device Address
		_rtw_memcpy( p2pie + p2pielen , pwdinfo->device_addr, ETH_ALEN );
		p2pielen += ETH_ALEN;

		//	SSID
		_rtw_memcpy( p2pie + p2pielen, pwdinfo->nego_ssid, pwdinfo->nego_ssidlen );
		p2pielen += pwdinfo->nego_ssidlen;
		
	}
	
	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pktlen );	
	
#ifdef CONFIG_WFD
	wfdielen = build_nego_resp_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pktlen += wfdielen;
#endif //CONFIG_WFD
	
	*pLength = pktlen;
#if 0
	// printf dbg msg
	dbgbufLen = pktlen;
	DBG_871X("======> DBG MSG FOR CONSTRAUCT Nego Rsp\n");

	for(index=0;index<dbgbufLen;index++)
		printk("%x ",*(dbgbuf+index));
	
	printk("\n");
	DBG_871X("<====== DBG MSG FOR CONSTRAUCT Nego Rsp\n");
#endif
}

static void rtw_hal_construct_P2PInviteRsp(_adapter * padapter, u8 * pframe, u32 * pLength)
{
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_INVIT_RESP;
	u8			p2pie[ 255 ] = { 0x00 };
	u8			p2pielen = 0, i;
	u8			channel_cnt_24g = 0, channel_cnt_5gl = 0, channel_cnt_5gh = 0;
	u16			len_channellist_attr = 0;
	u32			pktlen;
	u8			dialogToken = 0;
#ifdef CONFIG_CONCURRENT_MODE
	_adapter				*pbuddy_adapter = padapter->pbuddy_adapter;
	struct wifidirect_info	*pbuddy_wdinfo = &pbuddy_adapter->wdinfo;
	struct mlme_priv		*pbuddy_mlmepriv = &pbuddy_adapter->mlmepriv;
	struct mlme_ext_priv	*pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;
#endif	
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif //CONFIG_WFD
	
	//struct xmit_frame			*pmgntframe;
	//struct pkt_attrib			*pattrib;
	//unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo);

	//for debug
	u8 *dbgbuf = pframe;
	u8 dbgbufLen = 0, index = 0;


	DBG_871X( "%s\n", __FUNCTION__);
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	//RA fill by FW
	_rtw_memset(pwlanhdr->addr1, 0, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);

	//BSSID fill by FW
	_rtw_memset(pwlanhdr->addr3, 0, ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pktlen));	

	//dialog token, filled by FW
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pktlen));

	//	P2P IE Section.

	//	P2P OUI
	p2pielen = 0;
	p2pie[ p2pielen++ ] = 0x50;
	p2pie[ p2pielen++ ] = 0x6F;
	p2pie[ p2pielen++ ] = 0x9A;
	p2pie[ p2pielen++ ] = 0x09;	//	WFA P2P v1.0

	//	Commented by Albert 20101005
	//	According to the P2P Specification, the P2P Invitation response frame should contain 5 P2P attributes
	//	1. Status
	//	2. Configuration Timeout
	//	3. Operating Channel	( Only GO )
	//	4. P2P Group BSSID	( Only GO )
	//	5. Channel List

	//	P2P Status
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_STATUS;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0001 );
	p2pielen += 2;

	//	Value: filled by FW, defult value is FAIL INFO UNAVAILABLE
	p2pie[ p2pielen++ ] = P2P_STATUS_FAIL_INFO_UNAVAILABLE;
	
	//	Configuration Timeout
	//	Type:
	p2pie[ p2pielen++ ] = P2P_ATTR_CONF_TIMEOUT;

	//	Length:
	*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0002 );
	p2pielen += 2;

	//	Value:
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P GO
	p2pie[ p2pielen++ ] = 200;	//	2 seconds needed to be the P2P Client

	// due to defult value is FAIL INFO UNAVAILABLE, so the following IE is not needed
#if 0 
	if( status_code == P2P_STATUS_SUCCESS )
	{
		if( rtw_p2p_chk_role( pwdinfo, P2P_ROLE_GO ) )
		{
			//	The P2P Invitation request frame asks this Wi-Fi device to be the P2P GO
			//	In this case, the P2P Invitation response frame should carry the two more P2P attributes.
			//	First one is operating channel attribute.
			//	Second one is P2P Group BSSID attribute.

			//	Operating Channel
			//	Type:
			p2pie[ p2pielen++ ] = P2P_ATTR_OPERATING_CH;

			//	Length:
			*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 0x0005 );
			p2pielen += 2;

			//	Value:
			//	Country String
			p2pie[ p2pielen++ ] = 'X';
			p2pie[ p2pielen++ ] = 'X';
		
			//	The third byte should be set to 0x04.
			//	Described in the "Operating Channel Attribute" section.
			p2pie[ p2pielen++ ] = 0x04;

			//	Operating Class
			p2pie[ p2pielen++ ] = 0x51;	//	Copy from SD7
		
			//	Channel Number
			p2pie[ p2pielen++ ] = pwdinfo->operating_channel;	//	operating channel number
			

			//	P2P Group BSSID
			//	Type:
			p2pie[ p2pielen++ ] = P2P_ATTR_GROUP_BSSID;

			//	Length:
			*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( ETH_ALEN );
			p2pielen += 2;

			//	Value:
			//	P2P Device Address for GO
			_rtw_memcpy(p2pie + p2pielen, adapter_mac_addr(padapter), ETH_ALEN);
			p2pielen += ETH_ALEN;

		}

		//	Channel List
		//	Type:
		p2pie[ p2pielen++ ] = P2P_ATTR_CH_LIST;

		//	Length:
		// Country String(3)
		// + ( Operating Class (1) + Number of Channels(1) ) * Operation Classes (?)
		// + number of channels in all classes
		len_channellist_attr = 3
			+ (1 + 1) * (u16)pmlmeext->channel_list.reg_classes
			+ get_reg_classes_full_count(pmlmeext->channel_list);

#ifdef CONFIG_CONCURRENT_MODE
		if ( check_buddy_fwstate(padapter, _FW_LINKED ) )
		{
			*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( 5 + 1 );
		}
		else
		{
			*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( len_channellist_attr );
		}
#else

		*(u16*) ( p2pie + p2pielen ) = cpu_to_le16( len_channellist_attr );

#endif
		p2pielen += 2;

		//	Value:
		//	Country String
		p2pie[ p2pielen++ ] = 'X';
		p2pie[ p2pielen++ ] = 'X';

		//	The third byte should be set to 0x04.
		//	Described in the "Operating Channel Attribute" section.
		p2pie[ p2pielen++ ] = 0x04;

		//	Channel Entry List
#ifdef CONFIG_CONCURRENT_MODE
		if ( check_buddy_fwstate(padapter, _FW_LINKED ) )
		{
			_adapter *pbuddy_adapter = padapter->pbuddy_adapter;	
			struct mlme_ext_priv	*pbuddy_mlmeext = &pbuddy_adapter->mlmeextpriv;

			//	Operating Class
			if ( pbuddy_mlmeext->cur_channel > 14 )
			{
				if ( pbuddy_mlmeext->cur_channel >= 149 )
				{
					p2pie[ p2pielen++ ] = 0x7c;
				}
				else
				{
					p2pie[ p2pielen++ ] = 0x73;
				}
			}
			else
			{
				p2pie[ p2pielen++ ] = 0x51;
			}

			//	Number of Channels
			//	Just support 1 channel and this channel is AP's channel
			p2pie[ p2pielen++ ] = 1;

			//	Channel List
			p2pie[ p2pielen++ ] = pbuddy_mlmeext->cur_channel;
		}
		else
		{
			int i, j;
			for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
				//	Operating Class
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].reg_class;

				//	Number of Channels
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channels;

				//	Channel List
				for (i = 0; i < pmlmeext->channel_list.reg_class[j].channels; i++) {
					p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channel[i];
				}
			}
		}
#else // CONFIG_CONCURRENT_MODE
		{
			int i, j;
			for (j = 0; j < pmlmeext->channel_list.reg_classes; j++) {
				//	Operating Class
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].reg_class;

				//	Number of Channels
				p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channels;

				//	Channel List
				for (i = 0; i < pmlmeext->channel_list.reg_class[j].channels; i++) {
					p2pie[p2pielen++] = pmlmeext->channel_list.reg_class[j].channel[i];
				}
			}
		}
#endif // CONFIG_CONCURRENT_MODE
	}
#endif

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, p2pielen, (unsigned char *) p2pie, &pktlen );	
	
#ifdef CONFIG_WFD
	wfdielen = build_invitation_resp_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pktlen += wfdielen;
#endif //CONFIG_WFD

	*pLength = pktlen;

#if 0
	// printf dbg msg
	dbgbufLen = pktlen;
	DBG_871X("======> DBG MSG FOR CONSTRAUCT Invite Rsp\n");

	for(index=0;index<dbgbufLen;index++)
		printk("%x ",*(dbgbuf+index));
	
	printk("\n");
	DBG_871X("<====== DBG MSG FOR CONSTRAUCT Invite Rsp\n");
#endif
}


static void rtw_hal_construct_P2PProvisionDisRsp(_adapter * padapter, u8 * pframe, u32 * pLength)
{
	unsigned char category = RTW_WLAN_CATEGORY_PUBLIC;
	u8			action = P2P_PUB_ACTION_ACTION;
	u8			dialogToken = 0;	
	u32			p2poui = cpu_to_be32(P2POUI);
	u8			oui_subtype = P2P_PROVISION_DISC_RESP;
	u8			wpsie[ 100 ] = { 0x00 };
	u8			wpsielen = 0;
	u32			pktlen;
#ifdef CONFIG_WFD
	u32					wfdielen = 0;
#endif //CONFIG_WFD		
	
	//struct xmit_frame			*pmgntframe;
	//struct pkt_attrib			*pattrib;
	//unsigned char					*pframe;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo);

	//for debug
	u8 *dbgbuf = pframe;
	u8 dbgbufLen = 0, index = 0;

	DBG_871X( "%s\n", __FUNCTION__);

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	//RA filled by FW
	_rtw_memset(pwlanhdr->addr1, 0, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, adapter_mac_addr(padapter), ETH_ALEN);

	SetSeqNum(pwlanhdr,0);
	SetFrameSubType(pframe, WIFI_ACTION);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	pframe = rtw_set_fixed_ie(pframe, 1, &(category), &(pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(action), &(pktlen));
	pframe = rtw_set_fixed_ie(pframe, 4, (unsigned char *) &(p2poui), &(pktlen));
	pframe = rtw_set_fixed_ie(pframe, 1, &(oui_subtype), &(pktlen));	
	//dialog token, filled by FW
	pframe = rtw_set_fixed_ie(pframe, 1, &(dialogToken), &(pktlen));		

	wpsielen = 0;
	//	WPS OUI
	//*(u32*) ( wpsie ) = cpu_to_be32( WPSOUI );
	RTW_PUT_BE32(wpsie, WPSOUI);
	wpsielen += 4;

#if 0
	//	WPS version
	//	Type:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_VER1 );
	wpsielen += 2;

	//	Length:
	*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0001 );
	wpsielen += 2;

	//	Value:
	wpsie[wpsielen++] = WPS_VERSION_1;	//	Version 1.0
#endif

	//	Config Method
	//	Type:
	//*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( WPS_ATTR_CONF_METHOD );
	RTW_PUT_BE16(wpsie + wpsielen, WPS_ATTR_CONF_METHOD);
	wpsielen += 2;

	//	Length:
	//*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( 0x0002 );
	RTW_PUT_BE16(wpsie + wpsielen, 0x0002);
	wpsielen += 2;

	//	Value: filled by FW, default value is PBC
	//*(u16*) ( wpsie + wpsielen ) = cpu_to_be16( config_method );
	RTW_PUT_BE16(wpsie + wpsielen, WPS_CM_PUSH_BUTTON);
	wpsielen += 2;

	pframe = rtw_set_ie(pframe, _VENDOR_SPECIFIC_IE_, wpsielen, (unsigned char *) wpsie, &pktlen );	

#ifdef CONFIG_WFD
	wfdielen = build_provdisc_resp_wfd_ie(pwdinfo, pframe);
	pframe += wfdielen;
	pktlen += wfdielen;
#endif //CONFIG_WFD

	*pLength = pktlen;

	// printf dbg msg
#if 0
	dbgbufLen = pktlen;
	DBG_871X("======> DBG MSG FOR CONSTRAUCT  ProvisionDis Rsp\n");

	for(index=0;index<dbgbufLen;index++)
		printk("%x ",*(dbgbuf+index));

	printk("\n");
	DBG_871X("<====== DBG MSG FOR CONSTRAUCT ProvisionDis Rsp\n");
#endif
}

u8 rtw_hal_set_FwP2PRsvdPage_cmd(_adapter* adapter, PRSVDPAGE_LOC rsvdpageloc)
{
	u8 u1H2CP2PRsvdPageParm[H2C_P2PRSVDPAGE_LOC_LEN]={0};
	struct hal_ops *pHalFunc = &adapter->HalFunc;
	u8 ret = _FAIL;

	DBG_871X("P2PRsvdPageLoc: P2PBeacon=%d P2PProbeRsp=%d NegoRsp=%d InviteRsp=%d PDRsp=%d\n",  
		rsvdpageloc->LocP2PBeacon, rsvdpageloc->LocP2PProbeRsp,
		rsvdpageloc->LocNegoRsp, rsvdpageloc->LocInviteRsp,
		rsvdpageloc->LocPDRsp);

	SET_H2CCMD_RSVDPAGE_LOC_P2P_BCN(u1H2CP2PRsvdPageParm, rsvdpageloc->LocProbeRsp);
	SET_H2CCMD_RSVDPAGE_LOC_P2P_PROBE_RSP(u1H2CP2PRsvdPageParm, rsvdpageloc->LocPsPoll);
	SET_H2CCMD_RSVDPAGE_LOC_P2P_NEGO_RSP(u1H2CP2PRsvdPageParm, rsvdpageloc->LocNullData);
	SET_H2CCMD_RSVDPAGE_LOC_P2P_INVITE_RSP(u1H2CP2PRsvdPageParm, rsvdpageloc->LocQosNull);
	SET_H2CCMD_RSVDPAGE_LOC_P2P_PD_RSP(u1H2CP2PRsvdPageParm, rsvdpageloc->LocBTQosNull);
	
	//FillH2CCmd8723B(padapter, H2C_8723B_P2P_OFFLOAD_RSVD_PAGE, H2C_P2PRSVDPAGE_LOC_LEN, u1H2CP2PRsvdPageParm);
	ret = rtw_hal_fill_h2c_cmd(adapter,
				H2C_P2P_OFFLOAD_RSVD_PAGE,
				H2C_P2PRSVDPAGE_LOC_LEN,
				u1H2CP2PRsvdPageParm);

	return ret;
}

u8 rtw_hal_set_p2p_wowlan_offload_cmd(_adapter* adapter)
{

	u8 offload_cmd[H2C_P2P_OFFLOAD_LEN] = {0};
	struct wifidirect_info	*pwdinfo = &(adapter->wdinfo);
	struct P2P_WoWlan_Offload_t *p2p_wowlan_offload = (struct P2P_WoWlan_Offload_t *)offload_cmd;
	struct hal_ops *pHalFunc = &adapter->HalFunc;
	u8 ret = _FAIL;

	_rtw_memset(p2p_wowlan_offload,0 ,sizeof(struct P2P_WoWlan_Offload_t)); 
	DBG_871X("%s\n",__func__);	
	switch(pwdinfo->role)
	{
		case P2P_ROLE_DEVICE:
			DBG_871X("P2P_ROLE_DEVICE\n");
			p2p_wowlan_offload->role = 0;
			break;
		case P2P_ROLE_CLIENT:
			DBG_871X("P2P_ROLE_CLIENT\n");
			p2p_wowlan_offload->role = 1;
			break;
		case P2P_ROLE_GO:
			DBG_871X("P2P_ROLE_GO\n");
			p2p_wowlan_offload->role = 2;
			break;
		default: 
			DBG_871X("P2P_ROLE_DISABLE\n");
			break;
		}
	p2p_wowlan_offload->Wps_Config[0] = pwdinfo->supported_wps_cm>>8;
	p2p_wowlan_offload->Wps_Config[1] = pwdinfo->supported_wps_cm;
	offload_cmd = (u8*)p2p_wowlan_offload;
	DBG_871X("p2p_wowlan_offload: %x:%x:%x\n",offload_cmd[0],offload_cmd[1],offload_cmd[2]);	

	ret = rtw_hal_fill_h2c_cmd(adapter,
				H2C_P2P_OFFLOAD,
				H2C_P2P_OFFLOAD_LEN,
				offload_cmd);
	return ret;

	//FillH2CCmd8723B(adapter, H2C_8723B_P2P_OFFLOAD, sizeof(struct P2P_WoWlan_Offload_t), (u8 *)p2p_wowlan_offload);
}
#endif //CONFIG_P2P_WOWLAN

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


	//DBG_871X("%s\n", __FUNCTION__);

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(cur_network), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0/*pmlmeext->mgnt_seq*/);
	//pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_BEACON);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pktlen = sizeof (struct rtw_ieee80211_hdr_3addr);

	//timestamp will be inserted by hardware
	pframe += 8;
	pktlen += 8;

	// beacon interval: 2 bytes
	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pktlen += 2;

	// capability info: 2 bytes
	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pktlen += 2;

	if( (pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
	{
		//DBG_871X("ie len=%d\n", cur_network->IELength);
		pktlen += cur_network->IELength - sizeof(NDIS_802_11_FIXED_IEs);
		_rtw_memcpy(pframe, cur_network->IEs+sizeof(NDIS_802_11_FIXED_IEs), pktlen);

		goto _ConstructBeacon;
	}

	//below for ad-hoc mode

	// SSID
	pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pktlen);

	// supported rates...
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8)? 8: rate_len), cur_network->SupportedRates, &pktlen);

	// DS parameter set
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pktlen);

	if( (pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
	{
		u32 ATIMWindow;
		// IBSS Parameter Set...
		//ATIMWindow = cur->Configuration.ATIMWindow;
		ATIMWindow = 0;
		pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pktlen);
	}


	//todo: ERP IE


	// EXTERNDED SUPPORTED RATE
	if (rate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pktlen);
	}


	//todo:HT for adhoc

_ConstructBeacon:

	if ((pktlen + TXDESC_SIZE) > 512)
	{
		DBG_871X("beacon frame too large\n");
		return;
	}

	*pLength = pktlen;

	//DBG_871X("%s bcn_sz=%d\n", __FUNCTION__, pktlen);

}

static void rtw_hal_construct_PSPoll(_adapter *padapter,
		u8 *pframe, u32 *pLength)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;
	u32					pktlen;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	//DBG_871X("%s\n", __FUNCTION__);

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	// Frame control.
	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	SetPwrMgt(fctrl);
	SetFrameSubType(pframe, WIFI_PSPOLL);

	// AID.
	SetDuration(pframe, (pmlmeinfo->aid | 0xc000));

	// BSSID.
	_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	// TA.
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);

	*pLength = 16;
}

static void rtw_hal_construct_NullFunctionData(
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


	//DBG_871X("%s:%d\n", __FUNCTION__, bForcePowerSave);

	pwlanhdr = (struct rtw_ieee80211_hdr*)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;
	if (bForcePowerSave)
	{
		SetPwrMgt(fctrl);
	}

	switch(cur_network->network.InfrastructureMode)
	{
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

		SetFrameSubType(pframe, WIFI_QOS_DATA_NULL);

		pwlanqoshdr = (struct rtw_ieee80211_hdr_3addr_qos*)pframe;
		SetPriority(&pwlanqoshdr->qc, AC);
		SetEOSP(&pwlanqoshdr->qc, bEosp);

		pktlen = sizeof(struct rtw_ieee80211_hdr_3addr_qos);
	} else {
		SetFrameSubType(pframe, WIFI_DATA_NULL);

		pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	}

	*pLength = pktlen;
}

void rtw_hal_construct_ProbeRsp(_adapter *padapter, u8 *pframe, u32 *pLength, u8 *StaAddr, BOOLEAN bHideSSID)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;
	u8					*mac, *bssid;
	u32					pktlen;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX  *cur_network = &(pmlmeinfo->network);


	/*DBG_871X("%s\n", __FUNCTION__);*/

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	mac = adapter_mac_addr(padapter);
	bssid = cur_network->MacAddress;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	_rtw_memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, mac, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, bssid, ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	SetFrameSubType(fctrl, WIFI_PROBERSP);

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
//
// Description:
//	Construct the ARP response packet to support ARP offload.
//
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
	//for TKIP Cal MIC
	u8	*payload = pframe;
	u8	EncryptionHeadOverhead = 0;
	//DBG_871X("%s:%d\n", __FUNCTION__, bForcePowerSave);

	pwlanhdr = (struct rtw_ieee80211_hdr*)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	//-------------------------------------------------------------------------
	// MAC Header.
	//-------------------------------------------------------------------------
	SetFrameType(fctrl, WIFI_DATA);
	//SetFrameSubType(fctrl, 0);
	SetToDs(fctrl);
	_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	SetDuration(pwlanhdr, 0);
	//SET_80211_HDR_FRAME_CONTROL(pARPRspPkt, 0);
	//SET_80211_HDR_TYPE_AND_SUBTYPE(pARPRspPkt, Type_Data);
	//SET_80211_HDR_TO_DS(pARPRspPkt, 1);
	//SET_80211_HDR_ADDRESS1(pARPRspPkt, pMgntInfo->Bssid);
	//SET_80211_HDR_ADDRESS2(pARPRspPkt, Adapter->CurrentAddress);
	//SET_80211_HDR_ADDRESS3(pARPRspPkt, pMgntInfo->Bssid);

	//SET_80211_HDR_DURATION(pARPRspPkt, 0);
	//SET_80211_HDR_FRAGMENT_SEQUENCE(pARPRspPkt, 0);
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

	if(EncryptionHeadOverhead > 0) {
		_rtw_memset(&(pframe[*pLength]), 0,EncryptionHeadOverhead);
		*pLength += EncryptionHeadOverhead;
		//SET_80211_HDR_WEP(pARPRspPkt, 1);  //Suggested by CCW.
		SetPrivacy(fctrl);
	}

	//-------------------------------------------------------------------------
	// Frame Body.
	//-------------------------------------------------------------------------
	pARPRspPkt =  (u8*)(pframe+ *pLength);
	payload = pARPRspPkt; //Get Payload pointer
	// LLC header
	_rtw_memcpy(pARPRspPkt, ARPLLCHeader, 8);
	*pLength += 8;

	// ARP element
	pARPRspPkt += 8;
	SET_ARP_PKT_HW(pARPRspPkt, 0x0100);
	SET_ARP_PKT_PROTOCOL(pARPRspPkt, 0x0008);	// IP protocol
	SET_ARP_PKT_HW_ADDR_LEN(pARPRspPkt, 6);
	SET_ARP_PKT_PROTOCOL_ADDR_LEN(pARPRspPkt, 4);
	SET_ARP_PKT_OPERATION(pARPRspPkt, 0x0200);	// ARP response
	SET_ARP_PKT_SENDER_MAC_ADDR(pARPRspPkt, adapter_mac_addr(padapter));
	SET_ARP_PKT_SENDER_IP_ADDR(pARPRspPkt, pIPAddress);
#ifdef CONFIG_ARP_KEEP_ALIVE
	if (rtw_gw_addr_query(padapter)==0) {
		SET_ARP_PKT_TARGET_MAC_ADDR(pARPRspPkt, pmlmepriv->gw_mac_addr);
		SET_ARP_PKT_TARGET_IP_ADDR(pARPRspPkt, pmlmepriv->gw_ip);
	}
	else
#endif
	{
		SET_ARP_PKT_TARGET_MAC_ADDR(pARPRspPkt,
				get_my_bssid(&(pmlmeinfo->network)));
		SET_ARP_PKT_TARGET_IP_ADDR(pARPRspPkt,
				pIPAddress);
		DBG_871X("%s Target Mac Addr:" MAC_FMT "\n", __FUNCTION__,
				MAC_ARG(get_my_bssid(&(pmlmeinfo->network))));
		DBG_871X("%s Target IP Addr" IP_FMT "\n", __FUNCTION__,
				IP_ARG(pIPAddress));
	}

	*pLength += 28;

	if (psecuritypriv->dot11PrivacyAlgrthm == _TKIP_) {
		u8	mic[8];
		struct mic_data	micdata;
		struct sta_info	*psta = NULL;
		u8	priority[4]={0x0,0x0,0x0,0x0};
		u8	null_key[16]={0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0};

		DBG_871X("%s(): Add MIC\n",__FUNCTION__);

		psta = rtw_get_stainfo(&padapter->stapriv,
				get_my_bssid(&(pmlmeinfo->network)));
		if (psta != NULL) {
			if(_rtw_memcmp(&psta->dot11tkiptxmickey.skey[0],
						null_key, 16)==_TRUE) {
				DBG_871X("%s(): STA dot11tkiptxmickey==0\n",
						__func__);
			}
			//start to calculate the mic code
			rtw_secmicsetkey(&micdata,
					&psta->dot11tkiptxmickey.skey[0]);
		}

		rtw_secmicappend(&micdata, pwlanhdr->addr3, 6);  //DA

		rtw_secmicappend(&micdata, pwlanhdr->addr2, 6); //SA

		priority[0]=0;

		rtw_secmicappend(&micdata, &priority[0], 4);

		rtw_secmicappend(&micdata, payload, 36); //payload length = 8 + 28

		rtw_secgetmic(&micdata,&(mic[0]));

		pARPRspPkt += 28;
		_rtw_memcpy(pARPRspPkt, &(mic[0]),8);

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
	SetFrameSubType(pframe, WIFI_PROBEREQ);

	pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	pframe += pktlen;

	if (ssid == NULL) {
		pframe = rtw_set_ie(pframe, _SSID_IE_, 0, NULL, &pktlen);
	} else {
		//DBG_871X("%s len:%d\n", ssid->SSID, ssid->SSID_len);
		pframe = rtw_set_ie(pframe, _SSID_IE_, ssid->SSID_len, ssid->SSID, &pktlen);
	}

	get_rate_set(padapter, bssrate, &bssrate_len);

	if (bssrate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , 8, bssrate, &pktlen);
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_ , (bssrate_len - 8), (bssrate + 8), &pktlen);
	}
	else
	{
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_ , bssrate_len , bssrate, &pktlen);
	}

	*pLength = pktlen;
}

static void rtw_hal_construct_PNO_info(_adapter *padapter,
		u8 *pframe, u32*pLength)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);

	u8	*pPnoInfoPkt = pframe;
	pPnoInfoPkt =  (u8*)(pframe+ *pLength);
	_rtw_memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->ssid_num, 1);

	*pLength+=1;
	pPnoInfoPkt += 1;
	_rtw_memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->hidden_ssid_num, 1);

	*pLength+=3;
	pPnoInfoPkt += 3;
	_rtw_memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->fast_scan_period, 1);

	*pLength+=4;
	pPnoInfoPkt += 4;
	_rtw_memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->fast_scan_iterations, 4);

	*pLength+=4;
	pPnoInfoPkt += 4;
	_rtw_memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->slow_scan_period, 4);

	*pLength+=4;
	pPnoInfoPkt += 4;
	_rtw_memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->ssid_length,
			MAX_PNO_LIST_COUNT);

	*pLength+=MAX_PNO_LIST_COUNT;
	pPnoInfoPkt += MAX_PNO_LIST_COUNT;
	_rtw_memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->ssid_cipher_info,
			MAX_PNO_LIST_COUNT);

	*pLength+=MAX_PNO_LIST_COUNT;
	pPnoInfoPkt += MAX_PNO_LIST_COUNT;
	_rtw_memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->ssid_channel_info,
			MAX_PNO_LIST_COUNT);

	*pLength+=MAX_PNO_LIST_COUNT;
	pPnoInfoPkt += MAX_PNO_LIST_COUNT;
	_rtw_memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->loc_probe_req,
			MAX_HIDDEN_AP);

	*pLength+=MAX_HIDDEN_AP;
	pPnoInfoPkt += MAX_HIDDEN_AP;
}

static void rtw_hal_construct_ssid_list(_adapter *padapter,
	u8 *pframe, u32 *pLength)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	u8 *pSSIDListPkt = pframe;
	int i;

	pSSIDListPkt =  (u8*)(pframe+ *pLength);

	for(i = 0; i < pwrctl->pnlo_info->ssid_num ; i++) {
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

	pScanInfoPkt =  (u8*)(pframe+ *pLength);

	_rtw_memcpy(pScanInfoPkt, &pwrctl->pscan_info->channel_num, 1);

	*pLength+=1;
	pScanInfoPkt += 1;
	_rtw_memcpy(pScanInfoPkt, &pwrctl->pscan_info->orig_ch, 1);


	*pLength+=1;
	pScanInfoPkt += 1;
	_rtw_memcpy(pScanInfoPkt, &pwrctl->pscan_info->orig_bw, 1);


	*pLength+=1;
	pScanInfoPkt += 1;
	_rtw_memcpy(pScanInfoPkt, &pwrctl->pscan_info->orig_40_offset, 1);

	*pLength+=1;
	pScanInfoPkt += 1;
	_rtw_memcpy(pScanInfoPkt, &pwrctl->pscan_info->orig_80_offset, 1);

	*pLength+=1;
	pScanInfoPkt += 1;
	_rtw_memcpy(pScanInfoPkt, &pwrctl->pscan_info->periodScan, 1);

	*pLength+=1;
	pScanInfoPkt += 1;
	_rtw_memcpy(pScanInfoPkt, &pwrctl->pscan_info->period_scan_time, 1);

	*pLength+=1;
	pScanInfoPkt += 1;
	_rtw_memcpy(pScanInfoPkt, &pwrctl->pscan_info->enableRFE, 1);

	*pLength+=1;
	pScanInfoPkt += 1;
	_rtw_memcpy(pScanInfoPkt, &pwrctl->pscan_info->rfe_type, 8);

	*pLength+=8;
	pScanInfoPkt += 8;

	for(i = 0 ; i < MAX_SCAN_LIST_COUNT ; i ++) {
		_rtw_memcpy(pScanInfoPkt,
			&pwrctl->pscan_info->ssid_channel_info[i], 4);
		*pLength+=4;
		pScanInfoPkt += 4;
	}
}
#endif //CONFIG_PNO_SUPPORT

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
	static u8	GTKbody_a[11] ={0x01, 0x03, 0x00, 0x5F, 0x02, 0x03, 0x12, 0x00, 0x10, 0x42, 0x0B};
	u8	*pGTKRspPkt = pframe;
	u8	EncryptionHeadOverhead = 0;
	//DBG_871X("%s:%d\n", __FUNCTION__, bForcePowerSave);

	pwlanhdr = (struct rtw_ieee80211_hdr*)pframe;

	fctrl = &pwlanhdr->frame_ctl;
	*(fctrl) = 0;

	//-------------------------------------------------------------------------
	// MAC Header.
	//-------------------------------------------------------------------------
	SetFrameType(fctrl, WIFI_DATA);
	//SetFrameSubType(fctrl, 0);
	SetToDs(fctrl);

	_rtw_memcpy(pwlanhdr->addr1,
			get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	_rtw_memcpy(pwlanhdr->addr2,
			adapter_mac_addr(padapter), ETH_ALEN);

	_rtw_memcpy(pwlanhdr->addr3,
			get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	SetDuration(pwlanhdr, 0);

#ifdef CONFIG_WAPI_SUPPORT
	*pLength = sMacHdrLng;
#else
	*pLength = 24;
#endif //CONFIG_WAPI_SUPPORT

	//-------------------------------------------------------------------------
	// Security Header: leave space for it if necessary.
	//-------------------------------------------------------------------------
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
#endif //CONFIG_WAPI_SUPPORT
		default:
			EncryptionHeadOverhead = 0;
	}

	if (EncryptionHeadOverhead > 0) {
		_rtw_memset(&(pframe[*pLength]), 0,EncryptionHeadOverhead);
		*pLength += EncryptionHeadOverhead;
		//SET_80211_HDR_WEP(pGTKRspPkt, 1);  //Suggested by CCW.
		//GTK's privacy bit is done by FW
		//SetPrivacy(fctrl);
	}
	//-------------------------------------------------------------------------
	// Frame Body.
	//-------------------------------------------------------------------------
	pGTKRspPkt =  (u8*)(pframe+ *pLength);
	// LLC header
	_rtw_memcpy(pGTKRspPkt, LLCHeader, 8);
	*pLength += 8;

	// GTK element
	pGTKRspPkt += 8;

	//GTK frame body after LLC, part 1
	_rtw_memcpy(pGTKRspPkt, GTKbody_a, 11);
	*pLength += 11;
	pGTKRspPkt += 11;
	//GTK frame body after LLC, part 2
	_rtw_memset(&(pframe[*pLength]), 0, 88);
	*pLength += 88;
	pGTKRspPkt += 88;

}
#endif //CONFIG_GTK_OL
#endif //CONFIG_WOWLAN


//
// Description: Fill the reserved packets that FW will use to RSVD page.
//			Now we just send 4 types packet to rsvd page.
//			(1)Beacon, (2)Ps-poll, (3)Null data, (4)ProbeRsp.
// Input:
// finished - FALSE:At the first time we will send all the packets as a large packet to Hw,
//		    so we need to set the packet length to total lengh.
//	      TRUE: At the second time, we should send the first packet (default:beacon)
//		    to Hw again and set the lengh in descriptor to the real beacon lengh.
// 2009.10.15 by tynli.
//
//Page Size = 128: 8188e, 8723a/b, 8192c/d,  
//Page Size = 256: 8192e, 8821a
//Page Size = 512: 8812a
void rtw_hal_set_fw_rsvd_page(_adapter* adapter, bool finished)
{
	PHAL_DATA_TYPE pHalData;
	struct xmit_frame	*pcmdframe;
	struct pkt_attrib	*pattrib;
	struct xmit_priv	*pxmitpriv;
	struct mlme_ext_priv	*pmlmeext;
	struct mlme_ext_info	*pmlmeinfo;
	struct pwrctrl_priv *pwrctl;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	struct hal_ops *pHalFunc = &adapter->HalFunc;
	u32	BeaconLength = 0, ProbeRspLength = 0, PSPollLength = 0;
	u32	NullDataLength = 0, QosNullLength = 0, BTQosNullLength = 0;
	u32	ProbeReqLength = 0, NullFunctionDataLength = 0;
	u8	TxDescLen = TXDESC_SIZE, TxDescOffset = TXDESC_OFFSET;
	u8	TotalPageNum=0, CurtPktPageNum=0, RsvdPageNum=0;
	u8	*ReservedPagePacket;
	u16	BufIndex = 0;
	u32	TotalPacketLen = 0, MaxRsvdPageBufSize = 0, PageSize = 0;
	RSVDPAGE_LOC	RsvdPageLoc;
#ifdef CONFIG_WOWLAN	
	u32	ARPLegnth = 0, GTKLegnth = 0, PNOLength = 0, ScanInfoLength = 0;
	u32	SSIDLegnth = 0;
	struct security_priv *psecuritypriv = &adapter->securitypriv; //added by xx
	u8 currentip[4];
	u8 cur_dot11txpn[8];
#ifdef CONFIG_GTK_OL
	struct sta_priv *pstapriv = &adapter->stapriv;
	struct security_priv *psecpriv = NULL;
	struct sta_info * psta;
	u8 kek[RTW_KEK_LEN];
	u8 kck[RTW_KCK_LEN];
#endif //CONFIG_GTK_OL
#ifdef	CONFIG_PNO_SUPPORT 
	int index;
	u8 ssid_num;
#endif //CONFIG_PNO_SUPPORT
#endif
#ifdef DBG_CONFIG_ERROR_DETECT
	struct sreset_priv *psrtpriv;
#endif // DBG_CONFIG_ERROR_DETECT

#ifdef CONFIG_P2P_WOWLAN
	u32 P2PNegoRspLength = 0, P2PInviteRspLength = 0, P2PPDRspLength = 0, P2PProbeRspLength = 0, P2PBCNLength = 0;
#endif

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
		DBG_871X("[Error]: %s, PageSize is zero!!\n", __func__);
		return;
	}
	
	RsvdPageNum = rtw_hal_get_txbuff_rsvd_page_num(adapter, _TRUE);
	DBG_871X("%s PageSize: %d, RsvdPageNUm: %d\n",__func__, PageSize, RsvdPageNum);
	
	MaxRsvdPageBufSize = RsvdPageNum*PageSize;

	pcmdframe = rtw_alloc_cmdxmitframe(pxmitpriv);
	if (pcmdframe == NULL) {
		DBG_871X("%s: alloc ReservedPagePacket fail!\n", __FUNCTION__);
		return;
	}

	ReservedPagePacket = pcmdframe->buf_addr;
	_rtw_memset(&RsvdPageLoc, 0, sizeof(RSVDPAGE_LOC));

	//beacon * 2 pages
	BufIndex = TxDescOffset;
	rtw_hal_construct_beacon(adapter,
			&ReservedPagePacket[BufIndex], &BeaconLength);

	// When we count the first page size, we need to reserve description size for the RSVD
	// packet, it will be filled in front of the packet in TXPKTBUF.
	CurtPktPageNum = (u8)PageNum((TxDescLen + BeaconLength), PageSize);
	//If we don't add 1 more page, the WOWLAN function has a problem. Baron thinks it's a bug of firmware
	if (CurtPktPageNum == 1)
		CurtPktPageNum += 1;

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	//ps-poll * 1 page
	RsvdPageLoc.LocPsPoll = TotalPageNum;
	DBG_871X("LocPsPoll: %d\n", RsvdPageLoc.LocPsPoll);
	rtw_hal_construct_PSPoll(adapter,
			&ReservedPagePacket[BufIndex], &PSPollLength);
	rtw_hal_fill_fake_txdesc(adapter,
			&ReservedPagePacket[BufIndex-TxDescLen],
			PSPollLength, _TRUE, _FALSE, _FALSE);

	CurtPktPageNum = (u8)PageNum((TxDescLen + PSPollLength), PageSize);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

#ifdef CONFIG_BT_COEXIST
	//BT Qos null data * 1 page
	RsvdPageLoc.LocBTQosNull = TotalPageNum;
	DBG_871X("LocBTQosNull: %d\n", RsvdPageLoc.LocBTQosNull);
	rtw_hal_construct_NullFunctionData(
			adapter,
			&ReservedPagePacket[BufIndex],
			&BTQosNullLength,
			get_my_bssid(&pmlmeinfo->network),
			_TRUE, 0, 0, _FALSE);
	rtw_hal_fill_fake_txdesc(adapter,
			&ReservedPagePacket[BufIndex-TxDescLen],
			BTQosNullLength, _FALSE, _TRUE, _FALSE);

	CurtPktPageNum = (u8)PageNum(TxDescLen + BTQosNullLength, PageSize);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);
#endif //CONFIG_BT_COEXIT

	//null data * 1 page
	RsvdPageLoc.LocNullData = TotalPageNum;
	DBG_871X("LocNullData: %d\n", RsvdPageLoc.LocNullData);
	rtw_hal_construct_NullFunctionData(
			adapter,
			&ReservedPagePacket[BufIndex],
			&NullDataLength,
			get_my_bssid(&pmlmeinfo->network),
			_FALSE, 0, 0, _FALSE);
	rtw_hal_fill_fake_txdesc(adapter,
			&ReservedPagePacket[BufIndex-TxDescLen],
			NullDataLength, _FALSE, _FALSE, _FALSE);

	CurtPktPageNum = (u8)PageNum(TxDescLen + NullDataLength, PageSize);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	//Qos null data * 1 page
	RsvdPageLoc.LocQosNull = TotalPageNum;
	DBG_871X("LocQosNull: %d\n", RsvdPageLoc.LocQosNull);
	rtw_hal_construct_NullFunctionData(
			adapter,
			&ReservedPagePacket[BufIndex],
			&QosNullLength,
			get_my_bssid(&pmlmeinfo->network),
			_TRUE, 0, 0, _FALSE);
	rtw_hal_fill_fake_txdesc(adapter,
			&ReservedPagePacket[BufIndex-TxDescLen],
			QosNullLength, _FALSE, _FALSE, _FALSE);

	CurtPktPageNum = (u8)PageNum(TxDescLen + QosNullLength, PageSize);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

#ifdef CONFIG_WOWLAN
	if (pwrctl->wowlan_mode == _TRUE &&
			check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) {
		//ARP RSP * 1 page
		rtw_get_current_ip_address(adapter, currentip);

		RsvdPageLoc.LocArpRsp= TotalPageNum;

		rtw_hal_construct_ARPRsp(
				adapter,
				&ReservedPagePacket[BufIndex],
				&ARPLegnth,
				currentip);

		rtw_hal_fill_fake_txdesc(adapter,
				&ReservedPagePacket[BufIndex-TxDescLen],
				ARPLegnth, _FALSE, _FALSE, _TRUE);

		CurtPktPageNum = (u8)PageNum(TxDescLen + ARPLegnth, PageSize);

		TotalPageNum += CurtPktPageNum;

		BufIndex += (CurtPktPageNum*PageSize);

		//3 SEC IV * 1 page
		rtw_get_sec_iv(adapter, cur_dot11txpn,
				get_my_bssid(&pmlmeinfo->network));

		RsvdPageLoc.LocRemoteCtrlInfo = TotalPageNum;

		_rtw_memcpy(ReservedPagePacket+BufIndex-TxDescLen,
				cur_dot11txpn, _AES_IV_LEN_);

		CurtPktPageNum = (u8)PageNum(_AES_IV_LEN_, PageSize);

		TotalPageNum += CurtPktPageNum;
#ifdef CONFIG_GTK_OL
		BufIndex += (CurtPktPageNum*PageSize);

		//if the ap staion info. exists, get the kek, kck from staion info.
		psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
		if (psta == NULL) {
			_rtw_memset(kek, 0, RTW_KEK_LEN);
			_rtw_memset(kck, 0, RTW_KCK_LEN);
			DBG_8192C("%s, KEK, KCK download rsvd page all zero \n",
					__func__);
		} else {
			_rtw_memcpy(kek, psta->kek, RTW_KEK_LEN);
			_rtw_memcpy(kck, psta->kck, RTW_KCK_LEN);
		}

		//3 KEK, KCK
		RsvdPageLoc.LocGTKInfo = TotalPageNum;
		if(IS_HARDWARE_TYPE_8188E(adapter) || IS_HARDWARE_TYPE_8812(adapter)){
			psecpriv = &adapter->securitypriv;
			_rtw_memcpy(ReservedPagePacket+BufIndex-TxDescLen,
					&psecpriv->dot11PrivacyAlgrthm, 1);
			_rtw_memcpy(ReservedPagePacket+BufIndex-TxDescLen+1,
					&psecpriv->dot118021XGrpPrivacy, 1);
			_rtw_memcpy(ReservedPagePacket+BufIndex-TxDescLen+2,
					kck, RTW_KCK_LEN);
			_rtw_memcpy(ReservedPagePacket+BufIndex-TxDescLen+2+RTW_KCK_LEN,
					kek, RTW_KEK_LEN);
			
			CurtPktPageNum = (u8)PageNum(TxDescLen + 2 + RTW_KCK_LEN + RTW_KEK_LEN, PageSize);
		}
		else{
			_rtw_memcpy(ReservedPagePacket+BufIndex-TxDescLen,
					kck, RTW_KCK_LEN);
			_rtw_memcpy(ReservedPagePacket+BufIndex-TxDescLen+RTW_KCK_LEN,
					kek, RTW_KEK_LEN);

			CurtPktPageNum = (u8)PageNum(TxDescLen + RTW_KCK_LEN + RTW_KEK_LEN, PageSize);
		}
#if 0
		{
			int i;
			printk("\ntoFW KCK: ");
			for(i=0;i<16; i++)
				printk(" %02x ", kck[i]);
			printk("\ntoFW KEK: ");
			for(i=0;i<16; i++)
				printk(" %02x ", kek[i]);
			printk("\n");
		}
#endif

		//DBG_871X("%s(): HW_VAR_SET_TX_CMD: KEK KCK %p %d\n", 
		//	__FUNCTION__, &ReservedPagePacket[BufIndex-TxDescLen],
		//	(TxDescLen + RTW_KCK_LEN + RTW_KEK_LEN));

		TotalPageNum += CurtPktPageNum;

		BufIndex += (CurtPktPageNum*PageSize);

		//3 GTK Response
		RsvdPageLoc.LocGTKRsp= TotalPageNum;
		rtw_hal_construct_GTKRsp(
				adapter,
				&ReservedPagePacket[BufIndex],
				&GTKLegnth);

		rtw_hal_fill_fake_txdesc(adapter,
				&ReservedPagePacket[BufIndex-TxDescLen],
				GTKLegnth, _FALSE, _FALSE, _TRUE);
#if 0
		{
			int gj;
			printk("123GTK pkt=> \n");
			for(gj=0; gj < GTKLegnth+TxDescLen; gj++) {
				printk(" %02x ", ReservedPagePacket[BufIndex-TxDescLen+gj]);
				if ((gj + 1)%16==0)
					printk("\n");
			}
			printk(" <=end\n");
		}
#endif

		//DBG_871X("%s(): HW_VAR_SET_TX_CMD: GTK RSP %p %d\n", 
		//	__FUNCTION__, &ReservedPagePacket[BufIndex-TxDescLen],
		//	(TxDescLen + GTKLegnth));

		CurtPktPageNum = (u8)PageNum(TxDescLen + GTKLegnth, PageSize);

		TotalPageNum += CurtPktPageNum;

		BufIndex += (CurtPktPageNum*PageSize);

		//below page is empty for GTK extension memory
		//3(11) GTK EXT MEM
		RsvdPageLoc.LocGTKEXTMEM= TotalPageNum;

		CurtPktPageNum = 2;

		TotalPageNum += CurtPktPageNum;
		//extension memory for FW
		TotalPacketLen = BufIndex-TxDescLen + (PageSize*CurtPktPageNum);
#else //CONFIG_GTK_OL
		TotalPacketLen = BufIndex + _AES_IV_LEN_;
#endif //CONFIG_GTK_OL
	} else if (pwrctl->wowlan_pno_enable == _TRUE) {
#ifdef CONFIG_PNO_SUPPORT
		if (pwrctl->pno_in_resume == _FALSE &&
				pwrctl->pno_inited == _TRUE) {

			//Broadcast Probe Request
			RsvdPageLoc.LocProbePacket = TotalPageNum;

			DBG_871X("loc_probe_req: %d\n",
					RsvdPageLoc.LocProbePacket);

			rtw_hal_construct_ProbeReq(
				adapter,
				&ReservedPagePacket[BufIndex],
				&ProbeReqLength,
				NULL);

			rtw_hal_fill_fake_txdesc(adapter,
				&ReservedPagePacket[BufIndex-TxDescLen],
				ProbeReqLength, _FALSE, _FALSE, _FALSE);

			CurtPktPageNum =
				(u8)PageNum(TxDescLen + ProbeReqLength, PageSize);

			TotalPageNum += CurtPktPageNum;

			BufIndex += (CurtPktPageNum*PageSize);

			//Hidden SSID Probe Request
			ssid_num = pwrctl->pnlo_info->hidden_ssid_num;

			for (index = 0 ; index < ssid_num ; index++) {
				pwrctl->pnlo_info->loc_probe_req[index] =
					TotalPageNum;

				rtw_hal_construct_ProbeReq(
					adapter,
					&ReservedPagePacket[BufIndex],
					&ProbeReqLength,
					&pwrctl->pno_ssid_list->node[index]);

				rtw_hal_fill_fake_txdesc(adapter,
					&ReservedPagePacket[BufIndex-TxDescLen],
					ProbeReqLength, _FALSE, _FALSE, _FALSE);

				CurtPktPageNum =
					(u8)PageNum(TxDescLen + ProbeReqLength, PageSize);

				TotalPageNum += CurtPktPageNum;

				BufIndex += (CurtPktPageNum*PageSize);
			}

			//PNO INFO Page
			RsvdPageLoc.LocPNOInfo = TotalPageNum;
			rtw_hal_construct_PNO_info(adapter,
					&ReservedPagePacket[BufIndex -TxDescLen],
					&PNOLength);

			CurtPktPageNum = (u8)PageNum_128(PNOLength);
			TotalPageNum += CurtPktPageNum;
			BufIndex += (CurtPktPageNum*PageSize);

			//SSID List Page
			RsvdPageLoc.LocSSIDInfo = TotalPageNum;
			rtw_hal_construct_ssid_list(adapter,
					&ReservedPagePacket[BufIndex-TxDescLen],
					&SSIDLegnth);

			CurtPktPageNum = (u8)PageNum_128(SSIDLegnth);
			TotalPageNum += CurtPktPageNum;
			BufIndex += (CurtPktPageNum*PageSize);

			//Scan Info Page
			RsvdPageLoc.LocScanInfo = TotalPageNum;
			rtw_hal_construct_scan_info(adapter,
					&ReservedPagePacket[BufIndex-TxDescLen],
					&ScanInfoLength);

			CurtPktPageNum = (u8)PageNum(ScanInfoLength, PageSize);
			TotalPageNum += CurtPktPageNum;
			BufIndex += (CurtPktPageNum*PageSize);
			TotalPacketLen = BufIndex + ScanInfoLength;
		} else {
			TotalPacketLen = BufIndex + QosNullLength;
		}
#endif //CONFIG_PNO_SUPPORT
	} else {
		TotalPacketLen = BufIndex + QosNullLength;
	}
#else //CONFIG_WOWLAN
	TotalPacketLen = BufIndex + QosNullLength;
#endif //CONFIG_WOWLAN

#ifdef CONFIG_P2P_WOWLAN
	if(_TRUE == pwrctl->wowlan_p2p_mode)
	{

		// P2P Beacon
		RsvdPageLoc.LocP2PBeacon= TotalPageNum;
		rtw_hal_construct_P2PBeacon(
			adapter,
			&ReservedPagePacket[BufIndex],
			&P2PBCNLength);
		rtw_hal_fill_fake_txdesc(adapter, 
			&ReservedPagePacket[BufIndex-TxDescLen], 
			P2PBCNLength, _FALSE, _FALSE, _FALSE);

		//DBG_871X("%s(): HW_VAR_SET_TX_CMD: PROBE RSP %p %d\n", 
		//	__FUNCTION__, &ReservedPagePacket[BufIndex-TxDescLen], (P2PBCNLength+TxDescLen));

		CurtPktPageNum = (u8)PageNum(TxDescLen + P2PBCNLength, PageSize);

		TotalPageNum += CurtPktPageNum;

		BufIndex += (CurtPktPageNum*PageSize);

		// P2P Probe rsp
		RsvdPageLoc.LocP2PProbeRsp = TotalPageNum;
		rtw_hal_construct_P2PProbeRsp(
			adapter,
			&ReservedPagePacket[BufIndex],
			&P2PProbeRspLength);
		rtw_hal_fill_fake_txdesc(adapter, 
			&ReservedPagePacket[BufIndex-TxDescLen], 
			P2PProbeRspLength, _FALSE, _FALSE, _FALSE);

		//DBG_871X("%s(): HW_VAR_SET_TX_CMD: PROBE RSP %p %d\n", 
		//	__FUNCTION__, &ReservedPagePacket[BufIndex-TxDescLen], (P2PProbeRspLength+TxDescLen));

		CurtPktPageNum = (u8)PageNum(TxDescLen + P2PProbeRspLength, PageSize);

		TotalPageNum += CurtPktPageNum;

		BufIndex += (CurtPktPageNum*PageSize);

		//P2P nego rsp
		RsvdPageLoc.LocNegoRsp = TotalPageNum;
		rtw_hal_construct_P2PNegoRsp(
			adapter,
			&ReservedPagePacket[BufIndex],
			&P2PNegoRspLength);
		rtw_hal_fill_fake_txdesc(adapter, 
			&ReservedPagePacket[BufIndex-TxDescLen], 
			P2PNegoRspLength, _FALSE, _FALSE, _FALSE);

		//DBG_871X("%s(): HW_VAR_SET_TX_CMD: QOS NULL DATA %p %d\n", 
		//	__FUNCTION__, &ReservedPagePacket[BufIndex-TxDescLen], (NegoRspLength+TxDescLen));

		CurtPktPageNum = (u8)PageNum(TxDescLen + P2PNegoRspLength, PageSize);

		TotalPageNum += CurtPktPageNum;

		BufIndex += (CurtPktPageNum*PageSize);
		
		//P2P invite rsp
		RsvdPageLoc.LocInviteRsp = TotalPageNum;
		rtw_hal_construct_P2PInviteRsp(
			adapter,
			&ReservedPagePacket[BufIndex],
			&P2PInviteRspLength);
		rtw_hal_fill_fake_txdesc(adapter, 
			&ReservedPagePacket[BufIndex-TxDescLen], 
			P2PInviteRspLength, _FALSE, _FALSE, _FALSE);

		//DBG_871X("%s(): HW_VAR_SET_TX_CMD: QOS NULL DATA %p %d\n", 
		//	__FUNCTION__, &ReservedPagePacket[BufIndex-TxDescLen], (InviteRspLength+TxDescLen));

		CurtPktPageNum = (u8)PageNum(TxDescLen + P2PInviteRspLength, PageSize);

		TotalPageNum += CurtPktPageNum;

		BufIndex += (CurtPktPageNum*PageSize);
	
		//P2P provision discovery rsp
		RsvdPageLoc.LocPDRsp = TotalPageNum;
		rtw_hal_construct_P2PProvisionDisRsp(
			adapter,
			&ReservedPagePacket[BufIndex],
			&P2PPDRspLength);
		rtw_hal_fill_fake_txdesc(adapter, 
			&ReservedPagePacket[BufIndex-TxDescLen], 
			P2PPDRspLength, _FALSE, _FALSE, _FALSE);

		//DBG_871X("%s(): HW_VAR_SET_TX_CMD: QOS NULL DATA %p %d\n", 
		//	__FUNCTION__, &ReservedPagePacket[BufIndex-TxDescLen], (PDRspLength+TxDescLen));

		CurtPktPageNum = (u8)PageNum(TxDescLen + P2PPDRspLength, PageSize);

		TotalPageNum += CurtPktPageNum;

		BufIndex += (CurtPktPageNum*PageSize);

		TotalPacketLen = BufIndex + P2PPDRspLength;
	}
#endif //CONFIG_P2P_WOWLAN

	if(TotalPacketLen > MaxRsvdPageBufSize) {
		DBG_871X("%s(ERROR): rsvd page size is not enough!!TotalPacketLen %d, MaxRsvdPageBufSize %d\n",
				__FUNCTION__, TotalPacketLen,MaxRsvdPageBufSize);
		goto error;
	} else {
		// update attribute
		pattrib = &pcmdframe->attrib;
		update_mgntframe_attrib(adapter, pattrib);
		pattrib->qsel = 0x10;
		pattrib->pktlen = pattrib->last_txcmdsz = TotalPacketLen - TxDescOffset;
#ifdef CONFIG_PCI_HCI
		dump_mgntframe(adapter, pcmdframe);
#else
		dump_mgntframe_and_wait(adapter, pcmdframe, 100);
#endif
	}

	DBG_871X("%s: Set RSVD page location to Fw ,TotalPacketLen(%d), TotalPageNum(%d)\n",
			__func__,TotalPacketLen,TotalPageNum);

	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) {
		rtw_hal_set_FwRsvdPage_cmd(adapter, &RsvdPageLoc);
		if (pwrctl->wowlan_mode == _TRUE)
			rtw_hal_set_FwAoacRsvdPage_cmd(adapter, &RsvdPageLoc);
	} else if (pwrctl->wowlan_pno_enable) {
#ifdef CONFIG_PNO_SUPPORT
		rtw_hal_set_FwAoacRsvdPage_cmd(adapter, &RsvdPageLoc);
		if(pwrctl->pno_in_resume)
			rtw_hal_set_scan_offload_info_cmd(adapter,
					&RsvdPageLoc, 0);
		else
			rtw_hal_set_scan_offload_info_cmd(adapter,
					&RsvdPageLoc, 1);
#endif //CONFIG_PNO_SUPPORT
	}
#ifdef CONFIG_P2P_WOWLAN
	if(_TRUE == pwrctl->wowlan_p2p_mode)
		rtw_hal_set_FwP2PRsvdPage_cmd(adapter, &RsvdPageLoc);
	
#endif //CONFIG_P2P_WOWLAN
	return;
error:
	rtw_free_xmitframe(pxmitpriv, pcmdframe);
}

#ifdef CONFIG_AP_WOWLAN
/*
*Description: Fill the reserved packets that FW will use to RSVD page.
*Now we just send 2 types packet to rsvd page. (1)Beacon, (2)ProbeRsp.
*
*Input: bDLFinished	
*
*FALSE: At the first time we will send all the packets as a large packet to Hw,
*so we need to set the packet length to total length.
*
*TRUE: At the second time, we should send the first packet (default:beacon)
*to Hw again and set the length in descriptor to the real beacon length.
*2009.10.15 by tynli.
*
*Page Size = 128: 8188e, 8723a/b, 8192c/d,  
*Page Size = 256: 8192e, 8821a
*Page Size = 512: 8812a
*/
void rtw_hal_set_AP_fw_rsvd_page(_adapter *padapter , bool finished)
{
	PHAL_DATA_TYPE pHalData;
	struct xmit_frame	*pcmdframe;
	struct pkt_attrib	*pattrib;
	struct xmit_priv	*pxmitpriv;
	struct mlme_ext_priv	*pmlmeext;
	struct mlme_ext_info	*pmlmeinfo;
	struct pwrctrl_priv *pwrctl;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct hal_ops *pHalFunc = &padapter->HalFunc;
	u32	BeaconLength = 0 , ProbeRspLength = 0;
	u8	*ReservedPagePacket;
	u8	TxDescLen = TXDESC_SIZE, TxDescOffset = TXDESC_OFFSET;
	u8	TotalPageNum = 0 , CurtPktPageNum = 0 , RsvdPageNum = 0;
	u8	currentip[4];
	u16	BufIndex, PageSize = 0;
	u32	TotalPacketLen = 0 , MaxRsvdPageBufSize = 0;
	RSVDPAGE_LOC	RsvdPageLoc;
#ifdef DBG_CONFIG_ERROR_DETECT
	struct sreset_priv *psrtpriv;
#endif /* DBG_CONFIG_ERROR_DETECT */

	DBG_8192C("+" FUNC_ADPT_FMT ": iface_type=%d\n",
	FUNC_ADPT_ARG(padapter), get_iface_type(padapter));

	pHalData = GET_HAL_DATA(padapter);
#ifdef DBG_CONFIG_ERROR_DETECT
	psrtpriv = &pHalData->srestpriv;
#endif
	pxmitpriv = &padapter->xmitpriv;
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;
	pwrctl = adapter_to_pwrctl(padapter);

	rtw_hal_get_def_var(padapter, HAL_DEF_TX_PAGE_SIZE, (u8 *)&PageSize);
	DBG_871X("%s PAGE_SIZE: %d\n", __func__, PageSize);
	
	if (pHalFunc->hal_get_tx_buff_rsvd_page_num != NULL) {
		RsvdPageNum =
			pHalFunc->hal_get_tx_buff_rsvd_page_num(padapter, _TRUE);
		DBG_871X("%s RsvdPageNUm: %d\n", __func__, RsvdPageNum);
	} else {
		DBG_871X("[Error]: %s, missing tx_buff_rsvd_page_num func!!\n",
				__func__);
		return;
	}
	MaxRsvdPageBufSize = RsvdPageNum*PageSize;
	DBG_871X("%s: RsvdPageNum:%d, PageSize:%d\n", __func__ , RsvdPageNum , PageSize);

	pcmdframe = rtw_alloc_cmdxmitframe(pxmitpriv);
	if (pcmdframe == NULL) {
		DBG_871X("%s: alloc ReservedPagePacket fail!\n", __func__);
		return;
	}

	ReservedPagePacket = pcmdframe->buf_addr;
	_rtw_memset(&RsvdPageLoc, 0, sizeof(RSVDPAGE_LOC));

	/* (1) beacon*/
	BufIndex = TxDescOffset;
	rtw_hal_construct_beacon(padapter, &ReservedPagePacket[BufIndex], &BeaconLength);

	/* 
	*When we count the first page size, we need to reserve description size for the RSVD
	*packet, it will be filled in front of the packet in TXPKTBUF.
	*/
	CurtPktPageNum = (u8)PageNum(TxDescLen + BeaconLength, PageSize);
	/*If we don't add 1 more page, the WOWLAN function has a problem. Baron thinks it's a bug of firmware */
	if (CurtPktPageNum == 1) 
		CurtPktPageNum += 1;

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	/* (4) probe response*/
	RsvdPageLoc.LocProbeRsp = TotalPageNum;
	rtw_hal_construct_ProbeRsp(
		padapter,
		&ReservedPagePacket[BufIndex],
		&ProbeRspLength,
		get_my_bssid(&pmlmeinfo->network),
		_FALSE);
	rtw_hal_fill_fake_txdesc(padapter,
			&ReservedPagePacket[BufIndex-TxDescLen],
			ProbeRspLength, _FALSE, _FALSE, _FALSE);

	DBG_871X("%s(): HW_VAR_SET_TX_CMD: PROBE RSP %p %d\n",
		__func__, &ReservedPagePacket[BufIndex-TxDescLen],
		(ProbeRspLength+TxDescLen));

	CurtPktPageNum = (u8)PageNum(TxDescLen + BeaconLength, PageSize);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	TotalPacketLen = BufIndex + ProbeRspLength;

	if (TotalPacketLen > MaxRsvdPageBufSize) {
		DBG_871X("%s(): ERROR: The rsvd page size is not enough !!TotalPacketLen %d, MaxRsvdPageBufSize %d\n",
				__func__ , TotalPacketLen , MaxRsvdPageBufSize);
		goto error;
	} else {
		/* update attribute*/
		pattrib = &pcmdframe->attrib;
		update_mgntframe_attrib(padapter, pattrib);
		pattrib->qsel = QSLT_BEACON;
		pattrib->pktlen = TotalPacketLen - TxDescOffset;
		pattrib->last_txcmdsz = TotalPacketLen - TxDescOffset;
#ifdef CONFIG_PCI_HCI
		dump_mgntframe(padapter, pcmdframe);
#else
		dump_mgntframe_and_wait(padapter, pcmdframe, 100);
#endif
	}

	DBG_871X("%s: Set RSVD page location to Fw ,TotalPacketLen(%d), TotalPageNum(%d)\n" , __func__ , TotalPacketLen , TotalPageNum);
	rtw_hal_set_ap_wow_rsvdpage_cmd(padapter, &RsvdPageLoc);
	if (0)
		dump_TX_FIFO(padapter , 8 , 512);

	return;
error:
	rtw_free_xmitframe(pxmitpriv, pcmdframe);
}

#endif /*CONFIG_AP_WOWLAN*/



void SetHwReg(_adapter *adapter, u8 variable, u8 *val)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &(hal_data->odmpriv);

_func_enter_;

	switch (variable) {
		case HW_VAR_INITIAL_GAIN:
			{				
				u8 rx_gain = *((u8 *)(val));
				//printk("rx_gain:%x\n",rx_gain);
				if(rx_gain == 0xff){//restore rx gain					
					//ODM_Write_DIG(podmpriv,pDigTable->BackupIGValue);
					odm_PauseDIG(odm, ODM_RESUME_DIG,rx_gain);
				}
				else{
					//pDigTable->BackupIGValue = pDigTable->CurIGValue;
					//ODM_Write_DIG(podmpriv,rx_gain);
					odm_PauseDIG(odm, ODM_PAUSE_DIG,rx_gain);
				}
			}
			break;		
		case HW_VAR_PORT_SWITCH:
			hw_var_port_switch(adapter);
			break;
		case HW_VAR_INIT_RTS_RATE:
		{
			u16 brate_cfg = *((u16*)val);
			u8 rate_index = 0;
			HAL_VERSION *hal_ver = &hal_data->VersionID;

			if (IS_8188E(*hal_ver)) {

				while (brate_cfg > 0x1) {
					brate_cfg = (brate_cfg >> 1);
					rate_index++;
				}
				rtw_write8(adapter, REG_INIRTS_RATE_SEL, rate_index);
			} else {
				rtw_warn_on(1);
			}
		}
			break;
		case HW_VAR_SEC_CFG:
		{
			#if defined(CONFIG_CONCURRENT_MODE) && !defined(DYNAMIC_CAMID_ALLOC)
			// enable tx enc and rx dec engine, and no key search for MC/BC
			rtw_write8(adapter, REG_SECCFG, SCR_NoSKMC|SCR_RxDecEnable|SCR_TxEncEnable);
			#elif defined(DYNAMIC_CAMID_ALLOC)
			u16 reg_scr;

			reg_scr = rtw_read16(adapter, REG_SECCFG);
			rtw_write16(adapter, REG_SECCFG, reg_scr|SCR_CHK_KEYID|SCR_RxDecEnable|SCR_TxEncEnable);
			#else
			rtw_write8(adapter, REG_SECCFG, *((u8*)val));
			#endif
		}
			break;
		case HW_VAR_SEC_DK_CFG:
		{
			struct security_priv *sec = &adapter->securitypriv;
			u8 reg_scr = rtw_read8(adapter, REG_SECCFG);

			if (val) /* Enable default key related setting */
			{
				reg_scr |= SCR_TXBCUSEDK;
				if (sec->dot11AuthAlgrthm != dot11AuthAlgrthm_8021X)
					reg_scr |= (SCR_RxUseDK|SCR_TxUseDK);
			}
			else /* Disable default key related setting */
			{
				reg_scr &= ~(SCR_RXBCUSEDK|SCR_TXBCUSEDK|SCR_RxUseDK|SCR_TxUseDK);
			}

			rtw_write8(adapter, REG_SECCFG, reg_scr);
		}
			break;
		case HW_VAR_DM_FLAG:
			odm->SupportAbility = *((u32*)val);
			break;
		case HW_VAR_DM_FUNC_OP:
			if (*((u8*)val) == _TRUE) {
				/* save dm flag */
				odm->BK_SupportAbility = odm->SupportAbility;				
			} else {
				/* restore dm flag */
				odm->SupportAbility = odm->BK_SupportAbility;
			}
			break;
		case HW_VAR_DM_FUNC_SET:
			odm->SupportAbility |= *((u32 *)val);
			break;
		case HW_VAR_DM_FUNC_CLR:
			/*
			* input is already a mask to clear function
			* don't invert it again! George,Lucas@20130513
			*/
			odm->SupportAbility &= *((u32 *)val);
			break;
		case HW_VAR_ASIX_IOT:
			// enable  ASIX IOT function
			if (*((u8*)val) == _TRUE) {
				// 0xa2e[0]=0 (disable rake receiver)
				rtw_write8(adapter, rCCK0_FalseAlarmReport+2, 
						rtw_read8(adapter, rCCK0_FalseAlarmReport+2) & ~(BIT0));
				//  0xa1c=0xa0 (reset channel estimation if signal quality is bad)
				rtw_write8(adapter, rCCK0_DSPParameter2, 0xa0);
			} else {
			// restore reg:0xa2e,   reg:0xa1c
				rtw_write8(adapter, rCCK0_FalseAlarmReport+2, 
						rtw_read8(adapter, rCCK0_FalseAlarmReport+2)|(BIT0));
				rtw_write8(adapter, rCCK0_DSPParameter2, 0x00);
			}
			break;
#ifdef CONFIG_WOWLAN
	case HW_VAR_WOWLAN:
	{
		struct wowlan_ioctl_param *poidparam;
		struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
		struct security_priv *psecuritypriv = &adapter->securitypriv;
		struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
		struct hal_ops *pHalFunc = &adapter->HalFunc;
		struct sta_info *psta = NULL;
		int res;
		u16 media_status_rpt;
		u8 val8;

		poidparam = (struct wowlan_ioctl_param *)val;
		switch (poidparam->subcode) {
			case WOWLAN_ENABLE:
				DBG_871X_LEVEL(_drv_always_, "WOWLAN_ENABLE\n");

#ifdef CONFIG_GTK_OL
				if (psecuritypriv->dot11PrivacyAlgrthm == _AES_)
					rtw_hal_fw_sync_cam_id(adapter);
#endif
				if (IS_HARDWARE_TYPE_8723B(adapter))
					rtw_hal_backup_rate(adapter);

				rtw_hal_set_wowlan_fw(adapter, _TRUE);

				media_status_rpt = RT_MEDIA_CONNECT;
				rtw_hal_set_hwreg(adapter,
						HW_VAR_H2C_FW_JOINBSSRPT,
						(u8 *)&media_status_rpt);

				if (!pwrctl->wowlan_pno_enable) {
					psta = rtw_get_stainfo(&adapter->stapriv,
							get_bssid(pmlmepriv));
					media_status_rpt =
						(u16)((psta->mac_id<<8)|RT_MEDIA_CONNECT);
					if (psta != NULL) {
						rtw_hal_set_hwreg(adapter,
								HW_VAR_H2C_MEDIA_STATUS_RPT,
								(u8 *)&media_status_rpt);
					}
				}

				rtw_msleep_os(2);

				if (IS_HARDWARE_TYPE_8188E(adapter))
					rtw_hal_disable_tx_report(adapter);

				//RX DMA stop
				res = rtw_hal_pause_rx_dma(adapter);
				if (res == _FAIL)
					DBG_871X_LEVEL(_drv_always_, "[WARNING] pause RX DMA fail\n");

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
				//Enable CPWM2 only.
				res = rtw_hal_enable_cpwm2(adapter);
				if (res == _FAIL)
					DBG_871X_LEVEL(_drv_always_, "[WARNING] enable cpwm2 fail\n");
#endif
#ifdef CONFIG_GPIO_WAKEUP
				rtw_hal_switch_gpio_wl_ctrl(adapter,
						WAKEUP_GPIO_IDX, _TRUE);
#endif
				//Set WOWLAN H2C command.
				DBG_871X_LEVEL(_drv_always_, "Set WOWLan cmd\n");
				rtw_hal_set_fw_wow_related_cmd(adapter, 1);

				res = rtw_hal_check_wow_ctrl(adapter, _TRUE);
				if (res == _FALSE)
					DBG_871X("[Error]%s: set wowlan CMD fail!!\n", __func__);

				pwrctl->wowlan_wake_reason =
					rtw_read8(adapter, REG_WOWLAN_WAKE_REASON);

				DBG_871X_LEVEL(_drv_always_,
						"wowlan_wake_reason: 0x%02x\n",
						pwrctl->wowlan_wake_reason);
#ifdef CONFIG_GTK_OL_DBG
				dump_cam_table(adapter);
#endif
#ifdef CONFIG_USB_HCI
				if (adapter->intf_stop)		//free adapter's resource
					adapter->intf_stop(adapter);

#ifdef CONFIG_CONCURRENT_MODE
					if (rtw_buddy_adapter_up(adapter)) { //free buddy adapter's resource
						adapter->pbuddy_adapter->intf_stop(adapter->pbuddy_adapter);
					}
#endif //CONFIG_CONCURRENT_MODE

				/* Invoid SE0 reset signal during suspending*/
				rtw_write8(adapter, REG_RSV_CTRL, 0x20);
				rtw_write8(adapter, REG_RSV_CTRL, 0x60);
#endif //CONFIG_USB_HCI
				break;
			case WOWLAN_DISABLE:
				DBG_871X_LEVEL(_drv_always_, "WOWLAN_DISABLE\n");

				if (!pwrctl->wowlan_pno_enable) {
					psta = rtw_get_stainfo(&adapter->stapriv,
								get_bssid(pmlmepriv));

					if (psta != NULL) {
						media_status_rpt =
							(u16)((psta->mac_id<<8)|RT_MEDIA_DISCONNECT);
						rtw_hal_set_hwreg(adapter,
								HW_VAR_H2C_MEDIA_STATUS_RPT,
								(u8 *)&media_status_rpt);
					} else {
						DBG_871X("%s: psta is null\n", __func__);
					}
				}

				if (0) {
					DBG_871X("0x630:0x%02x\n",
							rtw_read8(adapter, 0x630));
					DBG_871X("0x631:0x%02x\n",
							rtw_read8(adapter, 0x631));
				}

				pwrctl->wowlan_wake_reason = rtw_read8(adapter,
						REG_WOWLAN_WAKE_REASON);

				DBG_871X_LEVEL(_drv_always_, "wakeup_reason: 0x%02x\n",
						pwrctl->wowlan_wake_reason);

				rtw_hal_set_fw_wow_related_cmd(adapter, 0);

				res = rtw_hal_check_wow_ctrl(adapter, _FALSE);
				if (res == _FALSE) {
					DBG_871X("[Error]%s: disable WOW cmd fail\n!!", __func__);
					rtw_hal_force_enable_rxdma(adapter);
				}

				if (IS_HARDWARE_TYPE_8188E(adapter))
					rtw_hal_enable_tx_report(adapter);

				rtw_hal_update_tx_iv(adapter);

#ifdef CONFIG_GTK_OL
				if (psecuritypriv->dot11PrivacyAlgrthm == _AES_)
					rtw_hal_update_gtk_offload_info(adapter);
#endif //CONFIG_GTK_OL

				rtw_hal_set_wowlan_fw(adapter, _FALSE);

#ifdef CONFIG_GPIO_WAKEUP
				DBG_871X_LEVEL(_drv_always_, "Set Wake GPIO to high for default.\n");
				rtw_hal_set_output_gpio(adapter, WAKEUP_GPIO_IDX, 1);
				rtw_hal_switch_gpio_wl_ctrl(adapter,
						WAKEUP_GPIO_IDX, _FALSE);
#endif
				if((pwrctl->wowlan_wake_reason != FWDecisionDisconnect) &&
					(pwrctl->wowlan_wake_reason != Rx_Pairwisekey) &&
					(pwrctl->wowlan_wake_reason != Rx_DisAssoc) &&
					(pwrctl->wowlan_wake_reason != Rx_DeAuth)) {

					//rtw_hal_download_rsvd_page(adapter, RT_MEDIA_CONNECT);

					media_status_rpt = RT_MEDIA_CONNECT;
					rtw_hal_set_hwreg(adapter,
						HW_VAR_H2C_FW_JOINBSSRPT,
						(u8 *)&media_status_rpt);

					if (psta != NULL) {
						media_status_rpt =
							(u16)((psta->mac_id<<8)|RT_MEDIA_CONNECT);
						rtw_hal_set_hwreg(adapter,
								HW_VAR_H2C_MEDIA_STATUS_RPT,
								(u8 *)&media_status_rpt);
					}
				}
				break;
			default:
				break;
			}
		}
		break;
#endif //CONFIG_WOWLAN
#ifdef CONFIG_AP_WOWLAN
		case HW_VAR_AP_WOWLAN:
		{
			u8 trycnt = 100;
			struct wowlan_ioctl_param *poidparam;
			struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
			struct security_priv *psecuritypriv = &adapter->securitypriv;
			struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
			struct hal_ops *pHalFunc = &adapter->HalFunc;
			struct sta_info *psta = NULL;
			int res;
			u16 media_status_rpt;
			u8 val8;

			poidparam = (struct wowlan_ioctl_param *) val;
			switch (poidparam->subcode) {
			case WOWLAN_AP_ENABLE:
				DBG_871X("%s, WOWLAN_AP_ENABLE\n", __func__);
				/* 1. Download WOWLAN FW*/
				DBG_871X_LEVEL(_drv_always_, "Re-download WoWlan FW!\n");
#ifdef DBG_CHECK_FW_PS_STATE
				if (rtw_fw_ps_state(adapter) == _FAIL) {
					pdbgpriv->dbg_enwow_dload_fw_fail_cnt++;
					DBG_871X_LEVEL(_drv_always_, "wowlan enable no leave 32k\n");
				}
#endif /*DBG_CHECK_FW_PS_STATE*/
				do {
					if (rtw_read8(adapter, REG_HMETFR) == 0x00) {
						DBG_871X_LEVEL(_drv_always_, "Ready to change FW.\n");
						break;
					}
					rtw_msleep_os(10);
					DBG_871X_LEVEL(_drv_always_, "trycnt: %d\n", (100-trycnt));
				} while (trycnt--);

				if (pHalFunc->hal_set_wowlan_fw != NULL)
					pHalFunc->hal_set_wowlan_fw(adapter, _TRUE);
				else
					DBG_871X("hal_set_wowlan_fw is null\n");

				/* 2. RX DMA stop*/
				DBG_871X_LEVEL(_drv_always_, "Pause DMA\n");
				trycnt = 100;
				rtw_write32(adapter , REG_RXPKT_NUM ,
					(rtw_read32(adapter , REG_RXPKT_NUM)|RW_RELEASE_EN));
				do {
					if ((rtw_read32(adapter, REG_RXPKT_NUM)&RXDMA_IDLE)) {
						DBG_871X_LEVEL(_drv_always_ , "RX_DMA_IDLE is true\n");
						/*if (Adapter->intf_stop)
							Adapter->intf_stop(Adapter);
						*/
						break;
					}
					/* If RX_DMA is not idle, receive one pkt from DMA*/
					DBG_871X_LEVEL(_drv_always_ , "RX_DMA_IDLE is not true\n");
				} while (trycnt--);

				if (trycnt == 0)
					DBG_871X_LEVEL(_drv_always_ , "Stop RX DMA failed......\n");

				/* 5. Set Enable WOWLAN H2C command. */
				DBG_871X_LEVEL(_drv_always_, "Set Enable AP WOWLan cmd\n");
				if (pHalFunc->hal_set_ap_wowlan_cmd != NULL)
					pHalFunc->hal_set_ap_wowlan_cmd(adapter, 1);
				else
					DBG_871X("hal_set_ap_wowlan_cmd is null\n");
		
				/* 6. add some delay for H2C cmd ready*/
				rtw_msleep_os(10);
				/* 7. enable AP power save*/

				rtw_write8(adapter, REG_MCUTST_WOWLAN, 0);

				if (adapter->intf_stop)
					adapter->intf_stop(adapter);

#ifdef CONFIG_USB_HCI 
				
#ifdef CONFIG_CONCURRENT_MODE
				if (rtw_buddy_adapter_up(adapter)) { /*free buddy adapter's resource*/
					adapter->pbuddy_adapter->intf_stop(adapter->pbuddy_adapter);
				}
#endif /*CONFIG_CONCURRENT_MODE*/

				/* Invoid SE0 reset signal during suspending*/
				rtw_write8(adapter, REG_RSV_CTRL, 0x20);
				rtw_write8(adapter, REG_RSV_CTRL, 0x60);
#endif /*CONFIG_USB_HCI*/
				break;
			case WOWLAN_AP_DISABLE:
				DBG_871X("%s, WOWLAN_AP_DISABLE\n", __func__);
				/* 1. Read wakeup reason*/
				pwrctl->wowlan_wake_reason =
					rtw_read8(adapter, REG_MCUTST_WOWLAN);

				DBG_871X_LEVEL(_drv_always_, "wakeup_reason: 0x%02x\n",
						pwrctl->wowlan_wake_reason);

				/* 2. disable AP power save*/
				if (pHalFunc->hal_set_ap_ps_wowlan_cmd != NULL)
					pHalFunc->hal_set_ap_ps_wowlan_cmd(adapter, 0);
				else
					DBG_871X("hal_set_ap_ps_wowlan_cmd is null\n");
				/* 3.  Set Disable WOWLAN H2C command.*/
				DBG_871X_LEVEL(_drv_always_, "Set Disable WOWLan cmd\n");
				if (pHalFunc->hal_set_ap_wowlan_cmd != NULL)
					pHalFunc->hal_set_ap_wowlan_cmd(adapter, 0);
				else
					DBG_871X("hal_set_ap_wowlan_cmd is null\n");
				/* 6. add some delay for H2C cmd ready*/
				rtw_msleep_os(2);
#ifdef DBG_CHECK_FW_PS_STATE
				if (rtw_fw_ps_state(adapter) == _FAIL) {
					pdbgpriv->dbg_diswow_dload_fw_fail_cnt++;
					DBG_871X_LEVEL(_drv_always_, "wowlan enable no leave 32k\n");
				}
#endif /*DBG_CHECK_FW_PS_STATE*/

				DBG_871X_LEVEL(_drv_always_, "Release RXDMA\n");

				rtw_write32(adapter, REG_RXPKT_NUM,
					(rtw_read32(adapter , REG_RXPKT_NUM) & (~RW_RELEASE_EN)));

				do {
					if (rtw_read8(adapter, REG_HMETFR) == 0x00) {
						DBG_871X_LEVEL(_drv_always_, "Ready to change FW.\n");
						break;
					}
					rtw_msleep_os(10);
					DBG_871X_LEVEL(_drv_always_, "trycnt: %d\n", (100-trycnt));
				} while (trycnt--);

				if (pHalFunc->hal_set_wowlan_fw != NULL)
					pHalFunc->hal_set_wowlan_fw(adapter, _FALSE);
				else
					DBG_871X("hal_set_wowlan_fw is null\n");
#ifdef CONFIG_GPIO_WAKEUP
				DBG_871X_LEVEL(_drv_always_, "Set Wake GPIO to high for default.\n");
				rtw_hal_set_output_gpio(adapter, WAKEUP_GPIO_IDX, 1);
#endif

#ifdef CONFIG_CONCURRENT_MODE
				if (rtw_buddy_adapter_up(adapter) == _TRUE &&
					check_buddy_fwstate(adapter, WIFI_AP_STATE) == _TRUE) {
					media_status_rpt = RT_MEDIA_CONNECT;
					rtw_hal_set_hwreg(adapter->pbuddy_adapter , HW_VAR_H2C_FW_JOINBSSRPT , (u8 *)&media_status_rpt);
					issue_beacon(adapter->pbuddy_adapter, 0);
				} else {
					media_status_rpt = RT_MEDIA_CONNECT;
					rtw_hal_set_hwreg(adapter , HW_VAR_H2C_FW_JOINBSSRPT , (u8 *)&media_status_rpt);
					issue_beacon(adapter, 0);
				}
#else
				media_status_rpt = RT_MEDIA_CONNECT;
				rtw_hal_set_hwreg(adapter , HW_VAR_H2C_FW_JOINBSSRPT , (u8 *)&media_status_rpt);
				issue_beacon(adapter , 0);
#endif

				break;
			default:
				break;
			}
		}
			break;
#endif /*CONFIG_AP_WOWLAN*/
		default:
			if (0)
				DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" variable(%d) not defined!\n",
					FUNC_ADPT_ARG(adapter), variable);
			break;
	}

_func_exit_;
}

void GetHwReg(_adapter *adapter, u8 variable, u8 *val)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &(hal_data->odmpriv);

_func_enter_;

	switch (variable) {
	case HW_VAR_BASIC_RATE:
		*((u16*)val) = hal_data->BasicRateSet;
		break;
	case HW_VAR_DM_FLAG:
		*((u32*)val) = odm->SupportAbility;
		break;
	case HW_VAR_RF_TYPE:
		*((u8*)val) = hal_data->rf_type;
		break;
	default:
		if (0)
			DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" variable(%d) not defined!\n",
				FUNC_ADPT_ARG(adapter), variable);
		break;
	}

_func_exit_;
}




u8
SetHalDefVar(_adapter *adapter, HAL_DEF_VARIABLE variable, void *value)
{	
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &(hal_data->odmpriv);
	u8 bResult = _SUCCESS;

	switch(variable) {
	case HW_DEF_FA_CNT_DUMP:		
		//ODM_COMP_COMMON
		if(*((u8*)value))
			odm->DebugComponents |= (ODM_COMP_DIG |ODM_COMP_FA_CNT);
		else
			odm->DebugComponents &= ~(ODM_COMP_DIG |ODM_COMP_FA_CNT);		
		break;
	case HAL_DEF_DBG_RX_INFO_DUMP:
		{
			PFALSE_ALARM_STATISTICS FalseAlmCnt = (PFALSE_ALARM_STATISTICS)PhyDM_Get_Structure( odm , PHYDM_FALSEALMCNT);
			pDIG_T	pDM_DigTable = &odm->DM_DigTable;

			DBG_871X("============ Rx Info dump ===================\n");
			DBG_871X("bLinked = %d, RSSI_Min = %d(%%), CurrentIGI = 0x%x \n",
				odm->bLinked, odm->RSSI_Min, pDM_DigTable->CurIGValue);
			DBG_871X("Cnt_Cck_fail = %d, Cnt_Ofdm_fail = %d, Total False Alarm = %d\n",	
				FalseAlmCnt->Cnt_Cck_fail, FalseAlmCnt->Cnt_Ofdm_fail, FalseAlmCnt->Cnt_all);

			if(odm->bLinked){
				DBG_871X("RxRate = %s, RSSI_A = %d(%%), RSSI_B = %d(%%)\n", 
					HDATA_RATE(odm->RxRate), odm->RSSI_A, odm->RSSI_B);	

				#ifdef DBG_RX_SIGNAL_DISPLAY_RAW_DATA
				rtw_dump_raw_rssi_info(adapter);
				#endif
			}
		}		
		break;		
	case HW_DEF_ODM_DBG_FLAG:
		ODM_CmnInfoUpdate(odm, ODM_CMNINFO_DBG_COMP, *((u8Byte*)value));
		break;
	case HW_DEF_ODM_DBG_LEVEL:
		ODM_CmnInfoUpdate(odm, ODM_CMNINFO_DBG_LEVEL, *((u4Byte*)value));
		break;
	case HAL_DEF_DBG_DUMP_RXPKT:
		hal_data->bDumpRxPkt = *((u8*)value);
		break;
	case HAL_DEF_DBG_DUMP_TXPKT:
		hal_data->bDumpTxPkt = *((u8*)value);
		break;
	case HAL_DEF_ANT_DETECT:
		hal_data->AntDetection = *((u8 *)value);
		break;
	case HAL_DEF_DBG_DIS_PWT:
		hal_data->bDisableTXPowerTraining = *((u8*)value);
		break;	
	default:
		DBG_871X_LEVEL(_drv_always_, "%s: [WARNING] HAL_DEF_VARIABLE(%d) not defined!\n", __FUNCTION__, variable);
		bResult = _FAIL;
		break;
	}

	return bResult;
}

u8
GetHalDefVar(_adapter *adapter, HAL_DEF_VARIABLE variable, void *value)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &(hal_data->odmpriv);
	u8 bResult = _SUCCESS;

	switch(variable) {
		case HAL_DEF_UNDERCORATEDSMOOTHEDPWDB:
			{
				struct mlme_priv *pmlmepriv;
				struct sta_priv *pstapriv;
				struct sta_info *psta;

				pmlmepriv = &adapter->mlmepriv;
				pstapriv = &adapter->stapriv;
				psta = rtw_get_stainfo(pstapriv, pmlmepriv->cur_network.network.MacAddress);
				if (psta)
				{
					*((int*)value) = psta->rssi_stat.UndecoratedSmoothedPWDB;     
				}
			}
			break;
		case HW_DEF_ODM_DBG_FLAG:
			*((u8Byte*)value) = odm->DebugComponents;
			break;
		case HW_DEF_ODM_DBG_LEVEL:
			*((u4Byte*)value) = odm->DebugLevel;
			break;
		case HAL_DEF_DBG_DUMP_RXPKT:
			*((u8*)value) = hal_data->bDumpRxPkt;
			break;
		case HAL_DEF_DBG_DUMP_TXPKT:
			*((u8*)value) = hal_data->bDumpTxPkt;
			break;
		case HAL_DEF_ANT_DETECT:
			*((u8 *)value) = hal_data->AntDetection;
			break;
		case HAL_DEF_MACID_SLEEP:
			*(u8*)value = _FALSE;
			break;
		case HAL_DEF_TX_PAGE_SIZE:
			*(( u32*)value) = PAGE_SIZE_128;
			break;
		case HAL_DEF_DBG_DIS_PWT:
			*(u8*)value = hal_data->bDisableTXPowerTraining;
			break;
		default:
			DBG_871X_LEVEL(_drv_always_, "%s: [WARNING] HAL_DEF_VARIABLE(%d) not defined!\n", __FUNCTION__, variable);
			bResult = _FAIL;
			break;
	}

	return bResult;
}

void GetHalODMVar(	
	PADAPTER				Adapter,
	HAL_ODM_VARIABLE		eVariable,
	PVOID					pValue1,
	PVOID					pValue2)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T podmpriv = &pHalData->odmpriv;
	switch(eVariable){
#if defined(CONFIG_SIGNAL_DISPLAY_DBM) && defined(CONFIG_BACKGROUND_NOISE_MONITOR)
		case HAL_ODM_NOISE_MONITOR:
			{
				u8 chan = *(u8*)pValue1;
				*(s16 *)pValue2 = pHalData->noise[chan];
				#ifdef DBG_NOISE_MONITOR
				DBG_8192C("### Noise monitor chan(%d)-noise:%d (dBm) ###\n",
					chan,pHalData->noise[chan]);
				#endif			
						
			}
			break;
#endif//#ifdef CONFIG_BACKGROUND_NOISE_MONITOR
		default:
			break;
	}
}

void SetHalODMVar(
	PADAPTER				Adapter,
	HAL_ODM_VARIABLE		eVariable,
	PVOID					pValue1,
	BOOLEAN					bSet)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T podmpriv = &pHalData->odmpriv;
	//_irqL irqL;
	switch(eVariable){
		case HAL_ODM_STA_INFO:
			{	
				struct sta_info *psta = (struct sta_info *)pValue1;				
				if(bSet){
					DBG_8192C("### Set STA_(%d) info ###\n",psta->mac_id);
					ODM_CmnInfoPtrArrayHook(podmpriv, ODM_CMNINFO_STA_STATUS,psta->mac_id,psta);
				}
				else{
					DBG_8192C("### Clean STA_(%d) info ###\n",psta->mac_id);
					//_enter_critical_bh(&pHalData->odm_stainfo_lock, &irqL);
					ODM_CmnInfoPtrArrayHook(podmpriv, ODM_CMNINFO_STA_STATUS,psta->mac_id,NULL);
					
					//_exit_critical_bh(&pHalData->odm_stainfo_lock, &irqL);
			            }
			}
			break;
		case HAL_ODM_P2P_STATE:		
				ODM_CmnInfoUpdate(podmpriv,ODM_CMNINFO_WIFI_DIRECT,bSet);
			break;
		case HAL_ODM_WIFI_DISPLAY_STATE:
				ODM_CmnInfoUpdate(podmpriv,ODM_CMNINFO_WIFI_DISPLAY,bSet);
			break;
		case HAL_ODM_REGULATION:
				ODM_CmnInfoInit(podmpriv, ODM_CMNINFO_DOMAIN_CODE_2G, pHalData->Regulation2_4G);
				ODM_CmnInfoInit(podmpriv, ODM_CMNINFO_DOMAIN_CODE_5G, pHalData->Regulation5G);
			break;
		#if defined(CONFIG_SIGNAL_DISPLAY_DBM) && defined(CONFIG_BACKGROUND_NOISE_MONITOR)		
		case HAL_ODM_NOISE_MONITOR:
			{
				struct noise_info *pinfo = (struct noise_info *)pValue1;

				#ifdef DBG_NOISE_MONITOR
				DBG_8192C("### Noise monitor chan(%d)-bPauseDIG:%d,IGIValue:0x%02x,max_time:%d (ms) ###\n",
					pinfo->chan,pinfo->bPauseDIG,pinfo->IGIValue,pinfo->max_time);
				#endif
				
				pHalData->noise[pinfo->chan] = ODM_InbandNoise_Monitor(podmpriv,pinfo->bPauseDIG,pinfo->IGIValue,pinfo->max_time);				
				DBG_871X("chan_%d, noise = %d (dBm)\n",pinfo->chan,pHalData->noise[pinfo->chan]);
				#ifdef DBG_NOISE_MONITOR
				DBG_871X("noise_a = %d, noise_b = %d  noise_all:%d \n", 
					podmpriv->noise_level.noise[ODM_RF_PATH_A], 
					podmpriv->noise_level.noise[ODM_RF_PATH_B],
					podmpriv->noise_level.noise_all);						
				#endif
			}
			break;
		#endif//#ifdef CONFIG_BACKGROUND_NOISE_MONITOR

		default:
			break;
	}
}	


BOOLEAN 
eqNByte(
	u8*	str1,
	u8*	str2,
	u32	num
	)
{
	if(num==0)
		return _FALSE;
	while(num>0)
	{
		num--;
		if(str1[num]!=str2[num])
			return _FALSE;
	}
	return _TRUE;
}

//
//	Description:
//		Return TRUE if chTmp is represent for hex digit and 
//		FALSE otherwise.
//
//
BOOLEAN
IsHexDigit(
	IN		char		chTmp
)
{
	if( (chTmp >= '0' && chTmp <= '9') ||  
		(chTmp >= 'a' && chTmp <= 'f') ||
		(chTmp >= 'A' && chTmp <= 'F') )
	{
		return _TRUE;
	}
	else
	{
		return _FALSE;
	}
}


//
//	Description:
//		Translate a character to hex digit.
//
u32
MapCharToHexDigit(
	IN		char		chTmp
)
{
	if(chTmp >= '0' && chTmp <= '9')
		return (chTmp - '0');
	else if(chTmp >= 'a' && chTmp <= 'f')
		return (10 + (chTmp - 'a'));
	else if(chTmp >= 'A' && chTmp <= 'F') 
		return (10 + (chTmp - 'A'));
	else
		return 0;	
}



//
//	Description:
//		Parse hex number from the string pucStr.
//
BOOLEAN 
GetHexValueFromString(
	IN		char*			szStr,
	IN OUT	u32*			pu4bVal,
	IN OUT	u32*			pu4bMove
)
{
	char*		szScan = szStr;

	// Check input parameter.
	if(szStr == NULL || pu4bVal == NULL || pu4bMove == NULL)
	{
		DBG_871X("GetHexValueFromString(): Invalid inpur argumetns! szStr: %p, pu4bVal: %p, pu4bMove: %p\n", szStr, pu4bVal, pu4bMove);
		return _FALSE;
	}

	// Initialize output.
	*pu4bMove = 0;
	*pu4bVal = 0;

	// Skip leading space.
	while(	*szScan != '\0' && 
			(*szScan == ' ' || *szScan == '\t') )
	{
		szScan++;
		(*pu4bMove)++;
	}

	// Skip leading '0x' or '0X'.
	if(*szScan == '0' && (*(szScan+1) == 'x' || *(szScan+1) == 'X'))
	{
		szScan += 2;
		(*pu4bMove) += 2;
	}	

	// Check if szScan is now pointer to a character for hex digit, 
	// if not, it means this is not a valid hex number.
	if(!IsHexDigit(*szScan))
	{
		return _FALSE;
	}

	// Parse each digit.
	do
	{
		(*pu4bVal) <<= 4;
		*pu4bVal += MapCharToHexDigit(*szScan);

		szScan++;
		(*pu4bMove)++;
	} while(IsHexDigit(*szScan));

	return _TRUE;
}

BOOLEAN 
GetFractionValueFromString(
	IN		char*			szStr,
	IN OUT	u8*				pInteger,
	IN OUT	u8*				pFraction,
	IN OUT	u32*			pu4bMove
)
{
	char	*szScan = szStr;

	// Initialize output.
	*pu4bMove = 0;
	*pInteger = 0;
	*pFraction = 0;

	// Skip leading space.
	while (	*szScan != '\0' && 	(*szScan == ' ' || *szScan == '\t') ) {
		++szScan;
		++(*pu4bMove);
	}

	// Parse each digit.
	do {
		(*pInteger) *= 10;
		*pInteger += ( *szScan - '0' );

		++szScan;
		++(*pu4bMove);

		if ( *szScan == '.' ) 
		{
			++szScan;
			++(*pu4bMove);
			
			if ( *szScan < '0' || *szScan > '9' )
				return _FALSE;
			else {
				*pFraction = *szScan - '0';
				++szScan;
				++(*pu4bMove);
				return _TRUE;
			}
		}
	} while(*szScan >= '0' && *szScan <= '9');

	return _TRUE;
}

//
//	Description:
//		Return TRUE if szStr is comment out with leading "//".
//
BOOLEAN
IsCommentString(
	IN		char			*szStr
)
{
	if(*szStr == '/' && *(szStr+1) == '/')
	{
		return _TRUE;
	}
	else
	{
		return _FALSE;
	}
}

BOOLEAN
GetU1ByteIntegerFromStringInDecimal(
	IN		char*	Str,
	IN OUT	u8*		pInt
	)
{
	u16 i = 0;
	*pInt = 0;

	while ( Str[i] != '\0' )
	{
		if ( Str[i] >= '0' && Str[i] <= '9' )
		{
			*pInt *= 10;
			*pInt += ( Str[i] - '0' );
		}
		else
		{
			return _FALSE;
		}
		++i;
	}

	return _TRUE;
}

// <20121004, Kordan> For example, 
// ParseQualifiedString(inString, 0, outString, '[', ']') gets "Kordan" from a string "Hello [Kordan]".
// If RightQualifier does not exist, it will hang on in the while loop
BOOLEAN 
ParseQualifiedString(
    IN		char*	In, 
    IN OUT	u32*	Start, 
    OUT		char*	Out, 
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
		; // find ']'
	j = (*Start) - 2;
	strncpy((char *)Out, (const char*)(In+i), j-i+1);

	return _TRUE;
}

BOOLEAN
isAllSpaceOrTab(
	u8*	data,
	u8	size
	)
{
	u8	cnt = 0, NumOfSpaceAndTab = 0;

	while( size > cnt )
	{
		if ( data[cnt] == ' ' || data[cnt] == '\t' || data[cnt] == '\0' )
			++NumOfSpaceAndTab;

		++cnt;
	}

	return size == NumOfSpaceAndTab;
}


void rtw_hal_check_rxfifo_full(_adapter *adapter)
{
	struct dvobj_priv *psdpriv = adapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(adapter);
	int save_cnt=_FALSE;
	
	//switch counter to RX fifo
	if( IS_8188E(pHalData->VersionID)
		|| IS_8812_SERIES(pHalData->VersionID) || IS_8821_SERIES(pHalData->VersionID)
		|| IS_8723B_SERIES(pHalData->VersionID) || IS_8192E(pHalData->VersionID))
	{
		rtw_write8(adapter, REG_RXERR_RPT+3, rtw_read8(adapter, REG_RXERR_RPT+3)|0xa0);
		save_cnt = _TRUE;
	}
	else 
	{
		//todo: other chips 
	}
	
		
	if(save_cnt)
	{
		//rtw_write8(adapter, REG_RXERR_RPT+3, rtw_read8(adapter, REG_RXERR_RPT+3)|0xa0);
		pdbgpriv->dbg_rx_fifo_last_overflow = pdbgpriv->dbg_rx_fifo_curr_overflow;
		pdbgpriv->dbg_rx_fifo_curr_overflow = rtw_read16(adapter, REG_RXERR_RPT);
		pdbgpriv->dbg_rx_fifo_diff_overflow = pdbgpriv->dbg_rx_fifo_curr_overflow-pdbgpriv->dbg_rx_fifo_last_overflow;
	}
}

void linked_info_dump(_adapter *padapter,u8 benable)
{			
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(padapter);

	if(padapter->bLinkInfoDump == benable)
		return;
	
	DBG_871X("%s %s \n",__FUNCTION__,(benable)?"enable":"disable");
										
	if(benable){
		#ifdef CONFIG_LPS
		pwrctrlpriv->org_power_mgnt = pwrctrlpriv->power_mgnt;//keep org value
		rtw_pm_set_lps(padapter,PS_MODE_ACTIVE);
		#endif	
								
		#ifdef CONFIG_IPS	
		pwrctrlpriv->ips_org_mode = pwrctrlpriv->ips_mode;//keep org value
		rtw_pm_set_ips(padapter,IPS_NONE);
		#endif	
	}
	else{
		#ifdef CONFIG_IPS		
		rtw_pm_set_ips(padapter, pwrctrlpriv->ips_org_mode);
		#endif // CONFIG_IPS

		#ifdef CONFIG_LPS	
		rtw_pm_set_lps(padapter, pwrctrlpriv->org_power_mgnt );
		#endif // CONFIG_LPS
	}
	padapter->bLinkInfoDump = benable ;	
}

#ifdef DBG_RX_SIGNAL_DISPLAY_RAW_DATA
void rtw_get_raw_rssi_info(void *sel, _adapter *padapter)
{
	u8 isCCKrate,rf_path;
	PHAL_DATA_TYPE	pHalData =  GET_HAL_DATA(padapter);
	struct rx_raw_rssi *psample_pkt_rssi = &padapter->recvpriv.raw_rssi_info;
	
	DBG_871X_SEL_NL(sel,"RxRate = %s, PWDBALL = %d(%%), rx_pwr_all = %d(dBm)\n", 
			HDATA_RATE(psample_pkt_rssi->data_rate), psample_pkt_rssi->pwdball, psample_pkt_rssi->pwr_all);
	isCCKrate = (psample_pkt_rssi->data_rate <= DESC_RATE11M)?TRUE :FALSE;

	if(isCCKrate)
		psample_pkt_rssi->mimo_singal_strength[0] = psample_pkt_rssi->pwdball;
		
	for(rf_path = 0;rf_path<pHalData->NumTotalRFPath;rf_path++)
	{
		DBG_871X_SEL_NL(sel,"RF_PATH_%d=>singal_strength:%d(%%),singal_quality:%d(%%)\n" 
			,rf_path,psample_pkt_rssi->mimo_singal_strength[rf_path],psample_pkt_rssi->mimo_singal_quality[rf_path]);
		
		if(!isCCKrate){
			DBG_871X_SEL_NL(sel,"\trx_ofdm_pwr:%d(dBm),rx_ofdm_snr:%d(dB)\n",
			psample_pkt_rssi->ofdm_pwr[rf_path],psample_pkt_rssi->ofdm_snr[rf_path]);
		}
	}	
}

void rtw_dump_raw_rssi_info(_adapter *padapter)
{
	u8 isCCKrate,rf_path;
	PHAL_DATA_TYPE	pHalData =  GET_HAL_DATA(padapter);
	struct rx_raw_rssi *psample_pkt_rssi = &padapter->recvpriv.raw_rssi_info;
	DBG_871X("============ RAW Rx Info dump ===================\n");
	DBG_871X("RxRate = %s, PWDBALL = %d(%%), rx_pwr_all = %d(dBm)\n", 
			HDATA_RATE(psample_pkt_rssi->data_rate), psample_pkt_rssi->pwdball, psample_pkt_rssi->pwr_all);	

	isCCKrate = (psample_pkt_rssi->data_rate <= DESC_RATE11M)?TRUE :FALSE;

	if(isCCKrate)
		psample_pkt_rssi->mimo_singal_strength[0] = psample_pkt_rssi->pwdball;
		
	for(rf_path = 0;rf_path<pHalData->NumTotalRFPath;rf_path++)
	{
		DBG_871X("RF_PATH_%d=>singal_strength:%d(%%),singal_quality:%d(%%)" 
			,rf_path,psample_pkt_rssi->mimo_singal_strength[rf_path],psample_pkt_rssi->mimo_singal_quality[rf_path]);
		
		if(!isCCKrate){
			printk(",rx_ofdm_pwr:%d(dBm),rx_ofdm_snr:%d(dB)\n",
			psample_pkt_rssi->ofdm_pwr[rf_path],psample_pkt_rssi->ofdm_snr[rf_path]);
		}else{
			printk("\n");	
		}
	}	
}

void rtw_store_phy_info(_adapter *padapter, union recv_frame *prframe)	
{
	u8 isCCKrate,rf_path;
	PHAL_DATA_TYPE	pHalData =  GET_HAL_DATA(padapter);
	struct rx_pkt_attrib *pattrib = &prframe->u.hdr.attrib;

	PODM_PHY_INFO_T pPhyInfo  = (PODM_PHY_INFO_T)(&pattrib->phy_info);
	struct rx_raw_rssi *psample_pkt_rssi = &padapter->recvpriv.raw_rssi_info;
	
	psample_pkt_rssi->data_rate = pattrib->data_rate;
	isCCKrate = (pattrib->data_rate <= DESC_RATE11M)?TRUE :FALSE;
	
	psample_pkt_rssi->pwdball = pPhyInfo->RxPWDBAll;
	psample_pkt_rssi->pwr_all = pPhyInfo->RecvSignalPower;

	for(rf_path = 0;rf_path<pHalData->NumTotalRFPath;rf_path++)
	{		
		psample_pkt_rssi->mimo_singal_strength[rf_path] = pPhyInfo->RxMIMOSignalStrength[rf_path];
		psample_pkt_rssi->mimo_singal_quality[rf_path] = pPhyInfo->RxMIMOSignalQuality[rf_path];
		if(!isCCKrate){
			psample_pkt_rssi->ofdm_pwr[rf_path] = pPhyInfo->RxPwr[rf_path];
			psample_pkt_rssi->ofdm_snr[rf_path] = pPhyInfo->RxSNR[rf_path];		
		}
	}
}
#endif

int check_phy_efuse_tx_power_info_valid(PADAPTER padapter) {
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u8* pContent = pHalData->efuse_eeprom_data;
	int index = 0;
	u16 tx_index_offset = 0x0000;

	switch(padapter->chip_type) {
		case RTL8723B:
			tx_index_offset = EEPROM_TX_PWR_INX_8723B;
		break;
		case RTL8188E:
			tx_index_offset = EEPROM_TX_PWR_INX_88E;
		break;
		case RTL8192E:
			tx_index_offset = EEPROM_TX_PWR_INX_8192E;
		break;
		case RTL8821:
			tx_index_offset = EEPROM_TX_PWR_INX_8821;
		break;
		default:
			tx_index_offset = 0x0010;
		break;
	}
	for (index = 0 ; index < 11 ; index++) {
		if (pContent[tx_index_offset + index] == 0xFF) {
			return _FALSE;
		} else {
			DBG_871X("0x%02x ,", pContent[tx_index_offset+index]);
		}
	}
	DBG_871X("\n");
	return _TRUE;
}

#ifdef CONFIG_EFUSE_CONFIG_FILE
int check_phy_efuse_macaddr_info_valid(PADAPTER padapter) {

	u8 val = 0;
	u16 addr_offset = 0x0000;

	switch(padapter->chip_type) {
		case RTL8723B:
			if (padapter->interface_type == RTW_USB) {
				addr_offset = EEPROM_MAC_ADDR_8723BU;
				DBG_871X("%s: interface is USB\n", __func__);
			} else if (padapter->interface_type == RTW_SDIO) {
				addr_offset = EEPROM_MAC_ADDR_8723BS;
				DBG_871X("%s: interface is SDIO\n", __func__);
			} else if (padapter->interface_type == RTW_PCIE) {
				addr_offset = EEPROM_MAC_ADDR_8723BE;
				DBG_871X("%s: interface is PCIE\n", __func__);
			} else if (padapter->interface_type == RTW_GSPI) {
				//addr_offset = EEPROM_MAC_ADDR_8723BS;
				DBG_871X("%s: interface is GSPI\n", __func__);
			}
		break;
		case RTL8188E:
			if (padapter->interface_type == RTW_USB) {
				addr_offset = EEPROM_MAC_ADDR_88EU;
				DBG_871X("%s: interface is USB\n", __func__);
			} else if (padapter->interface_type == RTW_SDIO) {
				addr_offset = EEPROM_MAC_ADDR_88ES;
				DBG_871X("%s: interface is SDIO\n", __func__);
			} else if (padapter->interface_type == RTW_PCIE) {
				addr_offset = EEPROM_MAC_ADDR_88EE;
				DBG_871X("%s: interface is PCIE\n", __func__);
			} else if (padapter->interface_type == RTW_GSPI) {
				//addr_offset = EEPROM_MAC_ADDR_8723BS;
				DBG_871X("%s: interface is GSPI\n", __func__);
			}
		break;
		case RTL8821:
			if (padapter->interface_type == RTW_USB) {
				addr_offset = EEPROM_MAC_ADDR_8821AU;
				DBG_871X("%s: interface is USB\n", __func__);
			} else if (padapter->interface_type == RTW_SDIO) {
				addr_offset = EEPROM_MAC_ADDR_8821AS;
				DBG_871X("%s: interface is SDIO\n", __func__);
			} else if (padapter->interface_type == RTW_PCIE) {
				addr_offset = EEPROM_MAC_ADDR_8821AE;
				DBG_871X("%s: interface is PCIE\n", __func__);
			} else if (padapter->interface_type == RTW_GSPI) {
				//addr_offset = EEPROM_MAC_ADDR_8723BS;
				DBG_871X("%s: interface is GSPI\n", __func__);
			}
		break;
	}

	if (addr_offset == 0x0000) {
		DBG_871X("phy efuse MAC addr offset is 0!!\n");
		return _FALSE;
	} else {
		rtw_efuse_map_read(padapter, addr_offset, 1, &val);
	}

	if (val == 0xFF) {
		return _FALSE;
	} else {
		DBG_871X("phy efuse with valid MAC addr\n");
		return _TRUE;
	}
}

u32 Hal_readPGDataFromConfigFile(
	PADAPTER	padapter,
	struct file *fp)
{
	u32 i;
	mm_segment_t fs;
	u8 temp[3];
	loff_t pos = 0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8	*PROMContent = pHalData->efuse_eeprom_data;

	temp[2] = 0; // add end of string '\0'

	fs = get_fs();
	set_fs(KERNEL_DS);

	for (i = 0 ; i < HWSET_MAX_SIZE ; i++) {
		vfs_read(fp, temp, 2, &pos);
		PROMContent[i] = simple_strtoul(temp, NULL, 16);
		if ((i % EFUSE_FILE_COLUMN_NUM) == (EFUSE_FILE_COLUMN_NUM - 1)) {
			//Filter the lates space char.
			vfs_read(fp, temp, 1, &pos);
			if (strchr(temp, ' ') == NULL) {
				pos--;
				vfs_read(fp, temp, 2, &pos);
			}
		} else {
			pos += 1; // Filter the space character
		}
	}

	set_fs(fs);
	pHalData->bloadfile_fail_flag = _FALSE;

#ifdef CONFIG_DEBUG
	DBG_871X("Efuse configure file:\n");
	for (i=0; i<HWSET_MAX_SIZE; i++)
	{
		if (i % 16 == 0)
			printk("\n");

		printk("%02X ", PROMContent[i]);
	}
	printk("\n");
#endif

	return _SUCCESS;
}

void Hal_ReadMACAddrFromFile(
	PADAPTER		padapter,
	struct file *fp)
{
	u32 i;
	mm_segment_t fs;
	u8 source_addr[18];
	loff_t pos = 0;
	u32	curtime = rtw_get_current_time();
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u8 *head, *end;

	_rtw_memset(source_addr, 0, 18);
	_rtw_memset(pHalData->EEPROMMACAddr, 0, ETH_ALEN);

	fs = get_fs();
	set_fs(KERNEL_DS);

	DBG_871X("wifi mac address:\n");
	vfs_read(fp, source_addr, 18, &pos);
	source_addr[17] = ':';

	head = end = source_addr;
	for (i=0; i<ETH_ALEN; i++) {
		while (end && (*end != ':') )
			end++;

		if (end && (*end == ':') )
			*end = '\0';

		pHalData->EEPROMMACAddr[i] = simple_strtoul(head, NULL, 16 );

		if (end) {
			end++;
			head = end;
		}
	}

	set_fs(fs);
	pHalData->bloadmac_fail_flag = _FALSE;

	if (rtw_check_invalid_mac_address(pHalData->EEPROMMACAddr) == _TRUE) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33))
		get_random_bytes(pHalData->EEPROMMACAddr, ETH_ALEN);
		pHalData->EEPROMMACAddr[0] = 0x00;
		pHalData->EEPROMMACAddr[1] = 0xe0;
		pHalData->EEPROMMACAddr[2] = 0x4c;
#else
		pHalData->EEPROMMACAddr[0] = 0x00;
		pHalData->EEPROMMACAddr[1] = 0xe0;
		pHalData->EEPROMMACAddr[2] = 0x4c;
		pHalData->EEPROMMACAddr[3] = (u8)(curtime & 0xff) ;
		pHalData->EEPROMMACAddr[4] = (u8)((curtime>>8) & 0xff) ;
		pHalData->EEPROMMACAddr[5] = (u8)((curtime>>16) & 0xff) ;
#endif
                DBG_871X("MAC Address from wifimac error is invalid, assign random MAC !!!\n");
	}

	DBG_871X("%s: Permanent Address = %02x-%02x-%02x-%02x-%02x-%02x\n",
			__func__, pHalData->EEPROMMACAddr[0], pHalData->EEPROMMACAddr[1],
			pHalData->EEPROMMACAddr[2], pHalData->EEPROMMACAddr[3],
			pHalData->EEPROMMACAddr[4], pHalData->EEPROMMACAddr[5]);
}

void Hal_GetPhyEfuseMACAddr(PADAPTER padapter, u8* mac_addr) {
	int i = 0;
	u16 addr_offset = 0x0000;

	switch(padapter->chip_type) {
		case RTL8723B:
			if (padapter->interface_type == RTW_USB) {
				addr_offset = EEPROM_MAC_ADDR_8723BU;
				DBG_871X("%s: interface is USB\n", __func__);
			} else if (padapter->interface_type == RTW_SDIO) {
				addr_offset = EEPROM_MAC_ADDR_8723BS;
				DBG_871X("%s: interface is SDIO\n", __func__);
			} else if (padapter->interface_type == RTW_PCIE) {
				addr_offset = EEPROM_MAC_ADDR_8723BE;
				DBG_871X("%s: interface is PCIE\n", __func__);
			} else if (padapter->interface_type == RTW_GSPI){
				//addr_offset = EEPROM_MAC_ADDR_8723BS;
				DBG_871X("%s: interface is GSPI\n", __func__);
			}
		break;
		case RTL8188E:
			if (padapter->interface_type == RTW_USB) {
				addr_offset = EEPROM_MAC_ADDR_88EU;
				DBG_871X("%s: interface is USB\n", __func__);
			} else if (padapter->interface_type == RTW_SDIO) {
				addr_offset = EEPROM_MAC_ADDR_88ES;
				DBG_871X("%s: interface is SDIO\n", __func__);
			} else if (padapter->interface_type == RTW_PCIE) {
				addr_offset = EEPROM_MAC_ADDR_88EE;
				DBG_871X("%s: interface is PCIE\n", __func__);
			} else if (padapter->interface_type == RTW_GSPI){
				//addr_offset = EEPROM_MAC_ADDR_8723BS;
				DBG_871X("%s: interface is GSPI\n", __func__);
			}
		break;
		case RTL8821:
			if (padapter->interface_type == RTW_USB) {
				addr_offset = EEPROM_MAC_ADDR_8821AU;
				DBG_871X("%s: interface is USB\n", __func__);
			} else if (padapter->interface_type == RTW_SDIO) {
				addr_offset = EEPROM_MAC_ADDR_8821AS;
				DBG_871X("%s: interface is SDIO\n", __func__);
			} else if (padapter->interface_type == RTW_PCIE) {
				addr_offset = EEPROM_MAC_ADDR_8821AE;
				DBG_871X("%s: interface is PCIE\n", __func__);
			} else if (padapter->interface_type == RTW_GSPI){
				//addr_offset = EEPROM_MAC_ADDR_8723BS;
				DBG_871X("%s: interface is GSPI\n", __func__);
			}
		break;
	}

	rtw_efuse_map_read(padapter, addr_offset, ETH_ALEN, mac_addr);

	if (rtw_check_invalid_mac_address(mac_addr) == _TRUE) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33))
		get_random_bytes(mac_addr, ETH_ALEN);
		mac_addr[0] = 0x00;
		mac_addr[1] = 0xe0;
		mac_addr[2] = 0x4c;
#else
		mac_addr[0] = 0x00;
		mac_addr[1] = 0xe0;
		mac_addr[2] = 0x4c;
		mac_addr[3] = (u8)(curtime & 0xff) ;
		mac_addr[4] = (u8)((curtime>>8) & 0xff) ;
		mac_addr[5] = (u8)((curtime>>16) & 0xff) ;
#endif
                DBG_871X("MAC Address from phy efuse error, assign random MAC !!!\n");
	}

	DBG_871X("%s: Permanent Address = %02x-%02x-%02x-%02x-%02x-%02x\n",
			__func__, mac_addr[0], mac_addr[1], mac_addr[2],
			mac_addr[3], mac_addr[4], mac_addr[5]);
}
#endif //CONFIG_EFUSE_CONFIG_FILE

#ifdef CONFIG_RF_GAIN_OFFSET
u32 Array_kfreemap[] = { 
0x08,0xe,
0x06,0xc,
0x04,0xa,
0x02,0x8,
0x00,0x6,
0x03,0x4,
0x05,0x2,
0x07,0x0,
0x09,0x0,
0x0c,0x0,
};

void rtw_bb_rf_gain_offset(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8		value = pHalData->EEPROMRFGainOffset;
	u8		tmp = 0x3e;
	u32 	res,i=0;
	u4Byte	   ArrayLen    = sizeof(Array_kfreemap)/sizeof(u32);
	pu4Byte    Array	   = Array_kfreemap;
	u4Byte v1=0,v2=0,GainValue,target=0; 
	//DBG_871X("+%s value: 0x%02x+\n", __func__, value);
#if defined(CONFIG_RTL8723B)
	if (value & BIT4) {
		DBG_871X("Offset RF Gain.\n");
		DBG_871X("Offset RF Gain.  pHalData->EEPROMRFGainVal=0x%x\n",pHalData->EEPROMRFGainVal);
		
		if(pHalData->EEPROMRFGainVal != 0xff){

			if(pHalData->ant_path == ODM_RF_PATH_A) {
				GainValue=(pHalData->EEPROMRFGainVal & 0x0f);
				
			} else {
				GainValue=(pHalData->EEPROMRFGainVal & 0xf0)>>4;
			}
			DBG_871X("Ant PATH_%d GainValue Offset = 0x%x\n",(pHalData->ant_path == ODM_RF_PATH_A) ? (ODM_RF_PATH_A) : (ODM_RF_PATH_B),GainValue);
			
			for (i = 0; i < ArrayLen; i += 2 )
			{
				//DBG_871X("ArrayLen in =%d ,Array 1 =0x%x ,Array2 =0x%x \n",i,Array[i],Array[i]+1);
				v1 = Array[i];
				v2 = Array[i+1];
				 if ( v1 == GainValue ) {
						DBG_871X("Offset RF Gain. got v1 =0x%x ,v2 =0x%x \n",v1,v2);
						target=v2;
						break;
				 }
			}	 
			DBG_871X("pHalData->EEPROMRFGainVal=0x%x ,Gain offset Target Value=0x%x\n",pHalData->EEPROMRFGainVal,target);

			res = rtw_hal_read_rfreg(padapter, RF_PATH_A, 0x7f, 0xffffffff);
			DBG_871X("Offset RF Gain. before reg 0x7f=0x%08x\n",res);
			PHY_SetRFReg(padapter, RF_PATH_A, REG_RF_BB_GAIN_OFFSET, BIT18|BIT17|BIT16|BIT15, target);
			res = rtw_hal_read_rfreg(padapter, RF_PATH_A, 0x7f, 0xffffffff);

			DBG_871X("Offset RF Gain. After reg 0x7f=0x%08x\n",res);
			
		}else {

			DBG_871X("Offset RF Gain.  pHalData->EEPROMRFGainVal=0x%x	!= 0xff, didn't run Kfree\n",pHalData->EEPROMRFGainVal);
		}
	} else {
		DBG_871X("Using the default RF gain.\n");
	}

#elif defined(CONFIG_RTL8188E)
	if (value & BIT4) {
		DBG_871X("8188ES Offset RF Gain.\n");
		DBG_871X("8188ES Offset RF Gain. EEPROMRFGainVal=0x%x\n",
				pHalData->EEPROMRFGainVal);

		if (pHalData->EEPROMRFGainVal != 0xff) {
			res = rtw_hal_read_rfreg(padapter, RF_PATH_A,
					REG_RF_BB_GAIN_OFFSET, 0xffffffff);

			DBG_871X("Offset RF Gain. reg 0x55=0x%x\n",res);
			res &= 0xfff87fff;

			res |= (pHalData->EEPROMRFGainVal & 0x0f) << 15;
			DBG_871X("Offset RF Gain. res=0x%x\n",res);

			rtw_hal_write_rfreg(padapter, RF_PATH_A,
					REG_RF_BB_GAIN_OFFSET,
					RF_GAIN_OFFSET_MASK, res);
		} else {
			DBG_871X("Offset RF Gain. EEPROMRFGainVal=0x%x == 0xff, didn't run Kfree\n",
					pHalData->EEPROMRFGainVal);
		}
	} else {
		DBG_871X("Using the default RF gain.\n");
	}
#else
	if (!(value & 0x01)) {
		//DBG_871X("Offset RF Gain.\n");
		res = rtw_hal_read_rfreg(padapter, RF_PATH_A, REG_RF_BB_GAIN_OFFSET, 0xffffffff);
		value &= tmp;
		res = value << 14;
		rtw_hal_write_rfreg(padapter, RF_PATH_A, REG_RF_BB_GAIN_OFFSET, RF_GAIN_OFFSET_MASK, res);
	} else {
		DBG_871X("Using the default RF gain.\n");
	}
#endif
	
}
#endif //CONFIG_RF_GAIN_OFFSET

#ifdef CONFIG_USB_RX_AGGREGATION	
void rtw_set_usb_agg_by_mode(_adapter *padapter, u8 cur_wireless_mode)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	if(cur_wireless_mode < WIRELESS_11_24N 
		&& cur_wireless_mode > 0) //ABG mode
	{
		if(0x6 != pHalData->RegAcUsbDmaSize || 0x10 !=pHalData->RegAcUsbDmaTime)
		{
			pHalData->RegAcUsbDmaSize = 0x6;
			pHalData->RegAcUsbDmaTime = 0x10;
			rtw_write16(padapter, REG_RXDMA_AGG_PG_TH,
				pHalData->RegAcUsbDmaSize | (pHalData->RegAcUsbDmaTime<<8));
		}
					
	}
	else if(cur_wireless_mode >= WIRELESS_11_24N
			&& cur_wireless_mode <= WIRELESS_MODE_MAX)//N AC mode
	{
		if(0x5 != pHalData->RegAcUsbDmaSize || 0x20 !=pHalData->RegAcUsbDmaTime)
		{
			pHalData->RegAcUsbDmaSize = 0x5;
			pHalData->RegAcUsbDmaTime = 0x20;
			rtw_write16(padapter, REG_RXDMA_AGG_PG_TH,
				pHalData->RegAcUsbDmaSize | (pHalData->RegAcUsbDmaTime<<8));
		}

	}
	else
	{
		/* DBG_871X("%s: Unknow wireless mode(0x%x)\n",__func__,padapter->mlmeextpriv.cur_wireless_mode); */
	}
}
#endif //CONFIG_USB_RX_AGGREGATION

//To avoid RX affect TX throughput
void dm_DynamicUsbTxAgg(_adapter *padapter, u8 from_timer)
{
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);
	struct mlme_priv		*pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeextpriv = &(padapter->mlmeextpriv);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8 cur_wireless_mode = pmlmeextpriv->cur_wireless_mode;
#ifdef CONFIG_CONCURRENT_MODE
	struct mlme_ext_priv	*pbuddymlmeextpriv = &(padapter->pbuddy_adapter->mlmeextpriv);
#endif //CONFIG_CONCURRENT_MODE

#ifdef CONFIG_USB_RX_AGGREGATION	
	if(IS_HARDWARE_TYPE_8821U(padapter) )//|| IS_HARDWARE_TYPE_8192EU(padapter))
	{
		//This AGG_PH_TH only for UsbRxAggMode == USB_RX_AGG_USB
		if((pHalData->UsbRxAggMode == USB_RX_AGG_USB) && (check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE))
		{
			if(pdvobjpriv->traffic_stat.cur_tx_tp > 2 && pdvobjpriv->traffic_stat.cur_rx_tp < 30)
				rtw_write16(padapter , REG_RXDMA_AGG_PG_TH , 0x1010);
			else
				rtw_write16(padapter, REG_RXDMA_AGG_PG_TH,0x2005); //dmc agg th 20K
			
			//DBG_871X("TX_TP=%u, RX_TP=%u \n", pdvobjpriv->traffic_stat.cur_tx_tp, pdvobjpriv->traffic_stat.cur_rx_tp);
		}
	}
	else if(IS_HARDWARE_TYPE_8812(padapter))
	{
#ifdef CONFIG_CONCURRENT_MODE
		if(rtw_linked_check(padapter) == _TRUE && rtw_linked_check(padapter->pbuddy_adapter) == _TRUE)
		{
			if(pbuddymlmeextpriv->cur_wireless_mode >= pmlmeextpriv->cur_wireless_mode)
				cur_wireless_mode = pbuddymlmeextpriv->cur_wireless_mode;
			else
				cur_wireless_mode = pmlmeextpriv->cur_wireless_mode;

			rtw_set_usb_agg_by_mode(padapter,cur_wireless_mode);
		}
		else if (rtw_linked_check(padapter) == _TRUE && rtw_linked_check(padapter->pbuddy_adapter) == _FALSE)
		{
			rtw_set_usb_agg_by_mode(padapter,cur_wireless_mode);
		}
#else //!CONFIG_CONCURRENT_MODE
		rtw_set_usb_agg_by_mode(padapter,cur_wireless_mode);
#endif //CONFIG_CONCURRENT_MODE
	}
#endif
}

//bus-agg check for SoftAP mode
inline u8 rtw_hal_busagg_qsel_check(_adapter *padapter,u8 pre_qsel,u8 next_qsel)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	u8 chk_rst = _SUCCESS;
	
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) != _TRUE)
		return chk_rst;

	//if((pre_qsel == 0xFF)||(next_qsel== 0xFF)) 
	//	return chk_rst;
	
	if(	((pre_qsel == QSLT_HIGH)||((next_qsel== QSLT_HIGH))) 
			&& (pre_qsel != next_qsel )){
			//DBG_871X("### bus-agg break cause of qsel misatch, pre_qsel=0x%02x,next_qsel=0x%02x ###\n",
			//	pre_qsel,next_qsel);
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
void dump_TX_FIFO(_adapter* padapter, u8 page_num, u16 page_size){

	int i;
	u8 val = 0;
	u8 base = 0;
	u32 addr = 0;
	u32 count = (page_size / 8);

	if (page_num <= 0) {
		DBG_871X("!!%s: incorrect input page_num paramter!\n", __func__);
		return;
	}

	if (page_size < 128 || page_size > 512) {
		DBG_871X("!!%s: incorrect input page_size paramter!\n", __func__);
		return;
	}

	DBG_871X("+%s+\n", __func__);
	val = rtw_read8(padapter, 0x106);
	rtw_write8(padapter, 0x106, 0x69);
	DBG_871X("0x106: 0x%02x\n", val);
	base = rtw_read8(padapter, 0x209);
	DBG_871X("0x209: 0x%02x\n", base);

	addr = ((base) * page_size)/8;
	for (i = 0 ; i < page_num * count ; i+=2) {
		rtw_write32(padapter, 0x140, addr + i);
		printk(" %08x %08x ", rtw_read32(padapter, 0x144), rtw_read32(padapter, 0x148));
		rtw_write32(padapter, 0x140, addr + i + 1);
		printk(" %08x %08x \n", rtw_read32(padapter, 0x144), rtw_read32(padapter, 0x148));
	}
}

#ifdef CONFIG_GPIO_API
u8 rtw_hal_get_gpio(_adapter* adapter, u8 gpio_num)
{
	u8 value;
	u8 direction;	
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);

	rtw_ps_deny(adapter, PS_DENY_IOCTL);

	DBG_871X("rf_pwrstate=0x%02x\n", pwrpriv->rf_pwrstate);
	LeaveAllPowerSaveModeDirect(adapter);

	/* Read GPIO Direction */
	direction = (rtw_read8(adapter,REG_GPIO_PIN_CTRL + 2) & BIT(gpio_num)) >> gpio_num;

	/* According the direction to read register value */
	if( direction )
		value =  (rtw_read8(adapter, REG_GPIO_PIN_CTRL + 1)& BIT(gpio_num)) >> gpio_num;
	else
		value =  (rtw_read8(adapter, REG_GPIO_PIN_CTRL)& BIT(gpio_num)) >> gpio_num;

	rtw_ps_deny_cancel(adapter, PS_DENY_IOCTL);
	DBG_871X("%s direction=%d value=%d\n",__FUNCTION__,direction,value);

	return value;
}

int  rtw_hal_set_gpio_output_value(_adapter* adapter, u8 gpio_num, bool isHigh)
{
	u8 direction = 0;
	u8 res = -1;
	if (IS_HARDWARE_TYPE_8188E(adapter)){
		/* Check GPIO is 4~7 */
		if( gpio_num > 7 || gpio_num < 4)
		{
			DBG_871X("%s The gpio number does not included 4~7.\n",__FUNCTION__);
			return -1;
		}
	}	
	
	rtw_ps_deny(adapter, PS_DENY_IOCTL);

	LeaveAllPowerSaveModeDirect(adapter);

	/* Read GPIO direction */
	direction = (rtw_read8(adapter,REG_GPIO_PIN_CTRL + 2) & BIT(gpio_num)) >> gpio_num;

	/* If GPIO is output direction, setting value. */
	if( direction )
	{
		if(isHigh)
			rtw_write8(adapter, REG_GPIO_PIN_CTRL + 1, rtw_read8(adapter, REG_GPIO_PIN_CTRL + 1) | BIT(gpio_num));
		else
			rtw_write8(adapter, REG_GPIO_PIN_CTRL + 1, rtw_read8(adapter, REG_GPIO_PIN_CTRL + 1) & ~BIT(gpio_num));

		DBG_871X("%s Set gpio %x[%d]=%d\n",__FUNCTION__,REG_GPIO_PIN_CTRL+1,gpio_num,isHigh );
		res = 0;
	}
	else
	{
		DBG_871X("%s The gpio is input,not be set!\n",__FUNCTION__);
		res = -1;
	}

	rtw_ps_deny_cancel(adapter, PS_DENY_IOCTL);
	return res;
}

int rtw_hal_config_gpio(_adapter* adapter, u8 gpio_num, bool isOutput)
{
	if (IS_HARDWARE_TYPE_8188E(adapter)){
		if( gpio_num > 7 || gpio_num < 4)
		{
			DBG_871X("%s The gpio number does not included 4~7.\n",__FUNCTION__);
			return -1;
		}
	}	

	DBG_871X("%s gpio_num =%d direction=%d\n",__FUNCTION__,gpio_num,isOutput);

	rtw_ps_deny(adapter, PS_DENY_IOCTL);

	LeaveAllPowerSaveModeDirect(adapter);

	if( isOutput )
	{
		rtw_write8(adapter, REG_GPIO_PIN_CTRL + 2, rtw_read8(adapter, REG_GPIO_PIN_CTRL + 2) | BIT(gpio_num));
	}
	else
	{
		rtw_write8(adapter, REG_GPIO_PIN_CTRL + 2, rtw_read8(adapter, REG_GPIO_PIN_CTRL + 2) & ~BIT(gpio_num));
	}

	rtw_ps_deny_cancel(adapter, PS_DENY_IOCTL);

	return 0;
}
int rtw_hal_register_gpio_interrupt(_adapter* adapter, int gpio_num, void(*callback)(u8 level))
{
	u8 value;
	u8 direction;
	PHAL_DATA_TYPE phal = GET_HAL_DATA(adapter);

	if (IS_HARDWARE_TYPE_8188E(adapter)){	
		if(gpio_num > 7 || gpio_num < 4)
		{
			DBG_871X_LEVEL(_drv_always_, "%s The gpio number does not included 4~7.\n",__FUNCTION__);
			return -1;
		}
	}

	rtw_ps_deny(adapter, PS_DENY_IOCTL);

	LeaveAllPowerSaveModeDirect(adapter);

	/* Read GPIO direction */
	direction = (rtw_read8(adapter,REG_GPIO_PIN_CTRL + 2) & BIT(gpio_num)) >> gpio_num;
	if(direction){
		DBG_871X_LEVEL(_drv_always_, "%s Can't register output gpio as interrupt.\n",__FUNCTION__);
		return -1;
	}

	/* Config GPIO Mode */
	rtw_write8(adapter, REG_GPIO_PIN_CTRL + 3, rtw_read8(adapter, REG_GPIO_PIN_CTRL + 3) | BIT(gpio_num));	

	/* Register GPIO interrupt handler*/
	adapter->gpiointpriv.callback[gpio_num] = callback;
	
	/* Set GPIO interrupt mode, 0:positive edge, 1:negative edge */
	value = rtw_read8(adapter, REG_GPIO_PIN_CTRL) & BIT(gpio_num);
	adapter->gpiointpriv.interrupt_mode = rtw_read8(adapter, REG_HSIMR + 2)^value;
	rtw_write8(adapter, REG_GPIO_INTM, adapter->gpiointpriv.interrupt_mode);
	
	/* Enable GPIO interrupt */
	adapter->gpiointpriv.interrupt_enable_mask = rtw_read8(adapter, REG_HSIMR + 2) | BIT(gpio_num);
	rtw_write8(adapter, REG_HSIMR + 2, adapter->gpiointpriv.interrupt_enable_mask);

	rtw_hal_update_hisr_hsisr_ind(adapter, 1);
	
	rtw_ps_deny_cancel(adapter, PS_DENY_IOCTL);

	return 0;
}
int rtw_hal_disable_gpio_interrupt(_adapter* adapter, int gpio_num)
{
	u8 value;
	u8 direction;
	PHAL_DATA_TYPE phal = GET_HAL_DATA(adapter);

	if (IS_HARDWARE_TYPE_8188E(adapter)){
		if(gpio_num > 7 || gpio_num < 4)
		{
			DBG_871X("%s The gpio number does not included 4~7.\n",__FUNCTION__);
			return -1;
		}
	}

	rtw_ps_deny(adapter, PS_DENY_IOCTL);

	LeaveAllPowerSaveModeDirect(adapter);

	/* Config GPIO Mode */
	rtw_write8(adapter, REG_GPIO_PIN_CTRL + 3, rtw_read8(adapter, REG_GPIO_PIN_CTRL + 3) &~ BIT(gpio_num));	

	/* Unregister GPIO interrupt handler*/
	adapter->gpiointpriv.callback[gpio_num] = NULL;

	/* Reset GPIO interrupt mode, 0:positive edge, 1:negative edge */
	adapter->gpiointpriv.interrupt_mode = rtw_read8(adapter, REG_GPIO_INTM) &~ BIT(gpio_num);
	rtw_write8(adapter, REG_GPIO_INTM, 0x00);
	
	/* Disable GPIO interrupt */
	adapter->gpiointpriv.interrupt_enable_mask = rtw_read8(adapter, REG_HSIMR + 2) &~ BIT(gpio_num);
	rtw_write8(adapter, REG_HSIMR + 2, adapter->gpiointpriv.interrupt_enable_mask);

	if(!adapter->gpiointpriv.interrupt_enable_mask)
		rtw_hal_update_hisr_hsisr_ind(adapter, 0);
	
	rtw_ps_deny_cancel(adapter, PS_DENY_IOCTL);

	return 0;
}
#endif

void rtw_dump_mac_rx_counters(_adapter* padapter,struct dbg_rx_counter *rx_counter)
{
	u32	mac_cck_ok=0, mac_ofdm_ok=0, mac_ht_ok=0, mac_vht_ok=0;
	u32	mac_cck_err=0, mac_ofdm_err=0, mac_ht_err=0, mac_vht_err=0;
	u32	mac_cck_fa=0, mac_ofdm_fa=0, mac_ht_fa=0;
	u32	DropPacket=0;
	
	if(!rx_counter){
		rtw_warn_on(1);
		return;
	}

	PHY_SetMacReg(padapter, REG_RXERR_RPT, BIT28|BIT29|BIT30|BIT31, 0x3);
	mac_cck_ok	= PHY_QueryMacReg(padapter, REG_RXERR_RPT, bMaskLWord);// [15:0]	  
	PHY_SetMacReg(padapter, REG_RXERR_RPT, BIT28|BIT29|BIT30|BIT31, 0x0);
	mac_ofdm_ok	= PHY_QueryMacReg(padapter, REG_RXERR_RPT, bMaskLWord);// [15:0]	 
	PHY_SetMacReg(padapter, REG_RXERR_RPT, BIT28|BIT29|BIT30|BIT31, 0x6);
	mac_ht_ok	= PHY_QueryMacReg(padapter, REG_RXERR_RPT, bMaskLWord);// [15:0]	
	mac_vht_ok	= 0;	
	if (IS_HARDWARE_TYPE_JAGUAR(padapter) || IS_HARDWARE_TYPE_JAGUAR2(padapter)) {
		PHY_SetMacReg(padapter, REG_RXERR_RPT, BIT28|BIT29|BIT30|BIT31, 0x0);
		PHY_SetMacReg(padapter, REG_RXERR_RPT, BIT26, 0x1);
		mac_vht_ok	= PHY_QueryMacReg(padapter, REG_RXERR_RPT, bMaskLWord);// [15:0]	 
	}	
		
	PHY_SetMacReg(padapter, REG_RXERR_RPT, BIT28|BIT29|BIT30|BIT31, 0x4);
	mac_cck_err	= PHY_QueryMacReg(padapter, REG_RXERR_RPT, bMaskLWord);// [15:0]	
	PHY_SetMacReg(padapter, REG_RXERR_RPT, BIT28|BIT29|BIT30|BIT31, 0x1);
	mac_ofdm_err	= PHY_QueryMacReg(padapter, REG_RXERR_RPT, bMaskLWord);// [15:0]	
	PHY_SetMacReg(padapter, REG_RXERR_RPT, BIT28|BIT29|BIT30|BIT31, 0x7);
	mac_ht_err	= PHY_QueryMacReg(padapter, REG_RXERR_RPT, bMaskLWord);// [15:0]		
	mac_vht_err	= 0;
	if (IS_HARDWARE_TYPE_JAGUAR(padapter) || IS_HARDWARE_TYPE_JAGUAR2(padapter)) {
		PHY_SetMacReg(padapter, REG_RXERR_RPT, BIT28|BIT29|BIT30|BIT31, 0x1);
		PHY_SetMacReg(padapter, REG_RXERR_RPT, BIT26, 0x1);
		mac_vht_err	= PHY_QueryMacReg(padapter, REG_RXERR_RPT, bMaskLWord);// [15:0]	 
	}

	PHY_SetMacReg(padapter, REG_RXERR_RPT, BIT28|BIT29|BIT30|BIT31, 0x5);
	mac_cck_fa	= PHY_QueryMacReg(padapter, REG_RXERR_RPT, bMaskLWord);// [15:0]	
	PHY_SetMacReg(padapter, REG_RXERR_RPT, BIT28|BIT29|BIT30|BIT31, 0x2);
	mac_ofdm_fa	= PHY_QueryMacReg(padapter, REG_RXERR_RPT, bMaskLWord);// [15:0]	
	PHY_SetMacReg(padapter, REG_RXERR_RPT, BIT28|BIT29|BIT30|BIT31, 0x9);
	mac_ht_fa	= PHY_QueryMacReg(padapter, REG_RXERR_RPT, bMaskLWord);// [15:0]		
	
	//Mac_DropPacket
	rtw_write32(padapter, REG_RXERR_RPT, (rtw_read32(padapter, REG_RXERR_RPT)& 0x0FFFFFFF)| Mac_DropPacket);
	DropPacket = rtw_read32(padapter, REG_RXERR_RPT)& 0x0000FFFF;

	rx_counter->rx_pkt_ok = mac_cck_ok+mac_ofdm_ok+mac_ht_ok+mac_vht_ok;
	rx_counter->rx_pkt_crc_error = mac_cck_err+mac_ofdm_err+mac_ht_err+mac_vht_err;
	rx_counter->rx_cck_fa = mac_cck_fa;
	rx_counter->rx_ofdm_fa = mac_ofdm_fa;
	rx_counter->rx_ht_fa = mac_ht_fa;
	rx_counter->rx_pkt_drop = DropPacket;
}
void rtw_reset_mac_rx_counters(_adapter* padapter)
{
	//reset mac counter
	PHY_SetMacReg(padapter, REG_RXERR_RPT, BIT27, 0x1); 
	PHY_SetMacReg(padapter, REG_RXERR_RPT, BIT27, 0x0);
}

void rtw_dump_phy_rx_counters(_adapter* padapter,struct dbg_rx_counter *rx_counter)
{
	u32 cckok=0,cckcrc=0,ofdmok=0,ofdmcrc=0,htok=0,htcrc=0,OFDM_FA=0,CCK_FA=0,vht_ok=0,vht_err=0;
	if(!rx_counter){
		rtw_warn_on(1);
		return;
	}
	if (IS_HARDWARE_TYPE_JAGUAR(padapter) || IS_HARDWARE_TYPE_JAGUAR2(padapter)){
		cckok	= PHY_QueryBBReg(padapter, 0xF04, 0x3FFF);	     // [13:0] 
		ofdmok	= PHY_QueryBBReg(padapter, 0xF14, 0x3FFF);	     // [13:0] 
		htok		= PHY_QueryBBReg(padapter, 0xF10, 0x3FFF);     // [13:0]
		vht_ok	= PHY_QueryBBReg(padapter, 0xF0C, 0x3FFF);     // [13:0]
		cckcrc	= PHY_QueryBBReg(padapter, 0xF04, 0x3FFF0000); // [29:16]	
		ofdmcrc	= PHY_QueryBBReg(padapter, 0xF14, 0x3FFF0000); // [29:16]
		htcrc	= PHY_QueryBBReg(padapter, 0xF10, 0x3FFF0000); // [29:16]
		vht_err	= PHY_QueryBBReg(padapter, 0xF0C, 0x3FFF0000); // [29:16]
		CCK_FA	= PHY_QueryBBReg(padapter, 0xA5C, bMaskLWord);
		OFDM_FA	= PHY_QueryBBReg(padapter, 0xF48, bMaskLWord);
	} 
	else
	{
		cckok	= PHY_QueryBBReg(padapter, 0xF88, bMaskDWord);
		ofdmok	= PHY_QueryBBReg(padapter, 0xF94, bMaskLWord);
		htok		= PHY_QueryBBReg(padapter, 0xF90, bMaskLWord);
		vht_ok	= 0;
		cckcrc	= PHY_QueryBBReg(padapter, 0xF84, bMaskDWord);
		ofdmcrc	= PHY_QueryBBReg(padapter, 0xF94, bMaskHWord);
		htcrc	= PHY_QueryBBReg(padapter, 0xF90, bMaskHWord);
		vht_err	= 0;
		OFDM_FA = PHY_QueryBBReg(padapter, 0xCF0, bMaskLWord) + PHY_QueryBBReg(padapter, 0xCF2, bMaskLWord) +
			PHY_QueryBBReg(padapter, 0xDA2, bMaskLWord) + PHY_QueryBBReg(padapter, 0xDA4, bMaskLWord) +
			PHY_QueryBBReg(padapter, 0xDA6, bMaskLWord) + PHY_QueryBBReg(padapter, 0xDA8, bMaskLWord);
	
		CCK_FA=(rtw_read8(padapter, 0xA5B )<<8 ) | (rtw_read8(padapter, 0xA5C));
	}
	
	rx_counter->rx_pkt_ok = cckok+ofdmok+htok+vht_ok;
	rx_counter->rx_pkt_crc_error = cckcrc+ofdmcrc+htcrc+vht_err;
	rx_counter->rx_ofdm_fa = OFDM_FA;
	rx_counter->rx_cck_fa = CCK_FA;
	
}

void rtw_reset_phy_rx_counters(_adapter* padapter)
{
	//reset phy counter
	if (IS_HARDWARE_TYPE_JAGUAR(padapter) || IS_HARDWARE_TYPE_JAGUAR2(padapter))
	{
		PHY_SetBBReg(padapter, 0xB58, BIT0, 0x1);
		PHY_SetBBReg(padapter, 0xB58, BIT0, 0x0);

		PHY_SetBBReg(padapter, 0x9A4, BIT17, 0x1);//reset  OFDA FA counter
		PHY_SetBBReg(padapter, 0x9A4, BIT17, 0x0);
			
		PHY_SetBBReg(padapter, 0xA2C, BIT15, 0x0);//reset  CCK FA counter
		PHY_SetBBReg(padapter, 0xA2C, BIT15, 0x1);
	}
	else
	{
		PHY_SetBBReg(padapter, 0xF14, BIT16, 0x1);
		rtw_msleep_os(10);
		PHY_SetBBReg(padapter, 0xF14, BIT16, 0x0);
		
		PHY_SetBBReg(padapter, 0xD00, BIT27, 0x1);//reset  OFDA FA counter
		PHY_SetBBReg(padapter, 0xC0C, BIT31, 0x1);//reset  OFDA FA counter
		PHY_SetBBReg(padapter, 0xD00, BIT27, 0x0);
		PHY_SetBBReg(padapter, 0xC0C, BIT31, 0x0);
			
		PHY_SetBBReg(padapter, 0xA2C, BIT15, 0x0);//reset  CCK FA counter
		PHY_SetBBReg(padapter, 0xA2C, BIT15, 0x1);
	}
}
#ifdef DBG_RX_COUNTER_DUMP
void rtw_dump_drv_rx_counters(_adapter* padapter,struct dbg_rx_counter *rx_counter)
{
	struct recv_priv *precvpriv = &padapter->recvpriv;
	if(!rx_counter){
		rtw_warn_on(1);
		return;
	}
	rx_counter->rx_pkt_ok = padapter->drv_rx_cnt_ok;
	rx_counter->rx_pkt_crc_error = padapter->drv_rx_cnt_crcerror;
	rx_counter->rx_pkt_drop = precvpriv->rx_drop - padapter->drv_rx_cnt_drop;	
}
void rtw_reset_drv_rx_counters(_adapter* padapter)
{
	struct recv_priv *precvpriv = &padapter->recvpriv;
	padapter->drv_rx_cnt_ok = 0;
	padapter->drv_rx_cnt_crcerror = 0;
	padapter->drv_rx_cnt_drop = precvpriv->rx_drop;
}
void rtw_dump_phy_rxcnts_preprocess(_adapter* padapter,u8 rx_cnt_mode)
{
	u8 initialgain;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(padapter);
	DM_ODM_T *odm = &(hal_data->odmpriv);
	DIG_T	*pDigTable = &odm->DM_DigTable;
	
	if((!(padapter->dump_rx_cnt_mode& DUMP_PHY_RX_COUNTER)) && (rx_cnt_mode & DUMP_PHY_RX_COUNTER))
	{
		initialgain = pDigTable->CurIGValue;
		DBG_871X("%s CurIGValue:0x%02x\n",__FUNCTION__,initialgain);
		rtw_hal_set_hwreg(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));
		//disable dynamic functions, such as high power, DIG
		Save_DM_Func_Flag(padapter);
		Switch_DM_Func(padapter, ~(ODM_BB_DIG|ODM_BB_FA_CNT), _FALSE);
	}
	else if((padapter->dump_rx_cnt_mode& DUMP_PHY_RX_COUNTER) &&(!(rx_cnt_mode & DUMP_PHY_RX_COUNTER )))
	{
		//turn on phy-dynamic functions
		Restore_DM_Func_Flag(padapter);			
		initialgain = 0xff; //restore RX GAIN
		rtw_hal_set_hwreg(padapter, HW_VAR_INITIAL_GAIN, (u8 *)(&initialgain));	
		
	}
}
	
void rtw_dump_rx_counters(_adapter* padapter)
{
	struct dbg_rx_counter rx_counter;

	if( padapter->dump_rx_cnt_mode & DUMP_DRV_RX_COUNTER ){
		_rtw_memset(&rx_counter,0,sizeof(struct dbg_rx_counter));
		rtw_dump_drv_rx_counters(padapter,&rx_counter);
		DBG_871X( "Drv Received packet OK:%d CRC error:%d Drop Packets: %d\n",
					rx_counter.rx_pkt_ok,rx_counter.rx_pkt_crc_error,rx_counter.rx_pkt_drop);		
		rtw_reset_drv_rx_counters(padapter);		
	}
		
	if( padapter->dump_rx_cnt_mode & DUMP_MAC_RX_COUNTER ){
		_rtw_memset(&rx_counter,0,sizeof(struct dbg_rx_counter));
		rtw_dump_mac_rx_counters(padapter,&rx_counter);
		DBG_871X( "Mac Received packet OK:%d CRC error:%d FA Counter: %d Drop Packets: %d\n",
					rx_counter.rx_pkt_ok,rx_counter.rx_pkt_crc_error,
					rx_counter.rx_cck_fa+rx_counter.rx_ofdm_fa+rx_counter.rx_ht_fa,
					rx_counter.rx_pkt_drop);			
		rtw_reset_mac_rx_counters(padapter);
	}

	if(padapter->dump_rx_cnt_mode & DUMP_PHY_RX_COUNTER ){		
		_rtw_memset(&rx_counter,0,sizeof(struct dbg_rx_counter));		
		rtw_dump_phy_rx_counters(padapter,&rx_counter);
		//DBG_871X("%s: OFDM_FA =%d\n", __FUNCTION__, rx_counter.rx_ofdm_fa);
		//DBG_871X("%s: CCK_FA =%d\n", __FUNCTION__, rx_counter.rx_cck_fa);
		DBG_871X("Phy Received packet OK:%d CRC error:%d FA Counter: %d\n",rx_counter.rx_pkt_ok,rx_counter.rx_pkt_crc_error,
		rx_counter.rx_ofdm_fa+rx_counter.rx_cck_fa);
		rtw_reset_phy_rx_counters(padapter);	
	}
}
#endif
void rtw_get_noise(_adapter* padapter)
{
#if defined(CONFIG_SIGNAL_DISPLAY_DBM) && defined(CONFIG_BACKGROUND_NOISE_MONITOR)
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct noise_info info;
	if(rtw_linked_check(padapter)){
		info.bPauseDIG = _TRUE;
		info.IGIValue = 0x1e;
		info.max_time = 100;//ms
		info.chan = pmlmeext->cur_channel ;//rtw_get_oper_ch(padapter);
		rtw_ps_deny(padapter, PS_DENY_IOCTL);
		LeaveAllPowerSaveModeDirect(padapter);

		rtw_hal_set_odm_var(padapter, HAL_ODM_NOISE_MONITOR,&info, _FALSE);	
		//ODM_InbandNoise_Monitor(podmpriv,_TRUE,0x20,100);
		rtw_ps_deny_cancel(padapter, PS_DENY_IOCTL);
		rtw_hal_get_odm_var(padapter, HAL_ODM_NOISE_MONITOR,&(info.chan), &(padapter->recvpriv.noise));	
		#ifdef DBG_NOISE_MONITOR
		DBG_871X("chan:%d,noise_level:%d\n",info.chan,padapter->recvpriv.noise);
		#endif
	}
#endif		

}

#ifdef CONFIG_FW_C2H_DEBUG

/*	C2H RX package original is 128.
if enable CONFIG_FW_C2H_DEBUG, it should increase to 256.
 C2H FW debug message:
 without aggregate:
 {C2H_CmdID,Seq,SubID,Len,Content[0~n]}
 Content[0~n]={'a','b','c',...,'z','\n'}
 with aggregate:
 {C2H_CmdID,Seq,SubID,Len,Content[0~n]}
 Content[0~n]={'a','b','c',...,'z','\n',Extend C2H pkt 2...}
 Extend C2H pkt 2={C2H CmdID,Seq,SubID,Len,Content = {'a','b','c',...,'z','\n'}}
 Author: Isaac	*/

void Debug_FwC2H(PADAPTER padapter, u8 *pdata, u8 len)
{
	int i = 0;
	int cnt = 0, total_length = 0;
	u8 buf[128] = {0};
	u8 more_data = _FALSE;
	u8 *nextdata = NULL;
	u8 test = 0;

	u8 data_len;
	u8 seq_no;

	nextdata = pdata;
	do {
		data_len = *(nextdata + 1);
		seq_no = *(nextdata + 2);

		for (i = 0 ; i < data_len - 2 ; i++) {
			cnt += sprintf((buf+cnt), "%c", nextdata[3 + i]);

			if (nextdata[3 + i] == 0x0a && nextdata[4 + i] == 0xff)
				more_data = _TRUE;
			else if (nextdata[3 + i] == 0x0a && nextdata[4 + i] != 0xff)
				more_data = _FALSE;
		}

		DBG_871X("[RTKFW, SEQ=%d]: %s", seq_no, buf);
		data_len += 3;
		total_length += data_len;

		if (more_data == _TRUE) {
			_rtw_memset(buf, '\0', 128);
			cnt = 0;
			nextdata = (pdata + total_length);
		}
	} while (more_data == _TRUE);
}
#endif /*CONFIG_FW_C2H_DEBUG*/
void update_IOT_info(_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
	switch (pmlmeinfo->assoc_AP_vendor)
	{
		case HT_IOT_PEER_MARVELL:
			pmlmeinfo->turboMode_cts2self = 1;
			pmlmeinfo->turboMode_rtsen = 0;
			break;
		
		case HT_IOT_PEER_RALINK:
			pmlmeinfo->turboMode_cts2self = 0;
			pmlmeinfo->turboMode_rtsen = 1;
			//disable high power			
			Switch_DM_Func(padapter, (~ODM_BB_DYNAMIC_TXPWR), _FALSE);
			break;
		case HT_IOT_PEER_REALTEK:
			//rtw_write16(padapter, 0x4cc, 0xffff);
			//rtw_write16(padapter, 0x546, 0x01c0);
			//disable high power			
			Switch_DM_Func(padapter, (~ODM_BB_DYNAMIC_TXPWR), _FALSE);
			break;
		default:
			pmlmeinfo->turboMode_cts2self = 0;
			pmlmeinfo->turboMode_rtsen = 1;
			break;	
	}
	
}



