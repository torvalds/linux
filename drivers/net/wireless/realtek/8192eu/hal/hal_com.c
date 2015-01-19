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

#include "../hal/OUTSRC/odm_precomp.h"


void dump_chip_info(HAL_VERSION	ChipVersion)
{
	int cnt = 0;
	u8 buf[128];
	
	if(IS_81XXC(ChipVersion)){
		cnt += sprintf((buf+cnt), "Chip Version Info: %s_", IS_92C_SERIAL(ChipVersion)?"CHIP_8192C":"CHIP_8188C");
	}
	else if(IS_92D(ChipVersion)){
		cnt += sprintf((buf+cnt), "Chip Version Info: CHIP_8192D_");
	}
	else if(IS_8723_SERIES(ChipVersion)){
		cnt += sprintf((buf+cnt), "Chip Version Info: CHIP_8723A_");
	}
	else if(IS_8188E(ChipVersion)){
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
	
	if(IS_A_CUT(ChipVersion)) cnt += sprintf((buf+cnt), "A_CUT_");
	else if(IS_B_CUT(ChipVersion)) cnt += sprintf((buf+cnt), "B_CUT_");
	else if(IS_C_CUT(ChipVersion)) cnt += sprintf((buf+cnt), "C_CUT_");
	else if(IS_D_CUT(ChipVersion)) cnt += sprintf((buf+cnt), "D_CUT_");
	else if(IS_E_CUT(ChipVersion)) cnt += sprintf((buf+cnt), "E_CUT_");
	else if(IS_I_CUT(ChipVersion)) cnt += sprintf((buf+cnt), "I_CUT_");
	else if(IS_J_CUT(ChipVersion)) cnt += sprintf((buf+cnt), "J_CUT_");
	else if(IS_K_CUT(ChipVersion)) cnt += sprintf((buf+cnt), "K_CUT_");
	else cnt += sprintf((buf+cnt), "UNKNOWN_CUT(%d)_", ChipVersion.CUTVersion);

	if(IS_1T1R(ChipVersion)) cnt += sprintf((buf+cnt), "1T1R_");
	else if(IS_1T2R(ChipVersion)) cnt += sprintf((buf+cnt), "1T2R_");
	else if(IS_2T2R(ChipVersion)) cnt += sprintf((buf+cnt), "2T2R_");
	else cnt += sprintf((buf+cnt), "UNKNOWN_RFTYPE(%d)_", ChipVersion.RFType);

	cnt += sprintf((buf+cnt), "RomVer(%d)\n", ChipVersion.ROMVer);

	DBG_871X("%s", buf);
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
		//0:H, 1:N 
		
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
		//0:H, 1:N 
		
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
	rtw_hal_set_hwreg(adapter, HW_VAR_MAC_ADDR, adapter->eeprompriv.mac_addr);
#ifdef  CONFIG_CONCURRENT_MODE
	if (adapter->pbuddy_adapter)
		rtw_hal_set_hwreg(adapter->pbuddy_adapter, HW_VAR_MAC_ADDR, adapter->pbuddy_adapter->eeprompriv.mac_addr);
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

#if defined(CONFIG_RTL8192C) || defined(CONFIG_RTL8192D) || defined(CONFIG_RTL8723A) || defined (CONFIG_RTL8188E)

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
		c2h_evt->payload[i] = rtw_read8(adapter, REG_C2HEVT_MSG_NORMAL + sizeof(*c2h_evt) + i);

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
	u32	tx_ra_bitmap;

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
			rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
			if(rf_type == RF_2T2R)
				limit=16;// 2R
			else
				limit=8;//  1R

			for (i=0; i<limit; i++) {
				if (psta->htpriv.ht_cap.supp_mcs_set[i/8] & BIT(i%8))
					tx_ra_bitmap |= BIT(i+12);
			}
		}
	}
#endif //CONFIG_80211N_HT

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
#if defined(CONFIG_RTL8723A) || defined(CONFIG_RTL8723B)
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

void SetHwReg(_adapter *adapter, u8 variable, u8 *val)
{
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	DM_ODM_T *odm = &(hal_data->odmpriv);

_func_enter_;

	switch (variable) {
	case HW_VAR_PORT_SWITCH:
		hw_var_port_switch(adapter);
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
		if(*((u32*)val) == DYNAMIC_ALL_FUNC_ENABLE){
			struct dm_priv	*dm = &hal_data->dmpriv;
			dm->DMFlag = dm->InitDMFlag;
			odm->SupportAbility = dm->InitODMFlag;
		} else {
			odm->SupportAbility |= *((u32 *)val);
		}
		break;
	case HW_VAR_DM_FUNC_CLR:
		/*
		* input is already a mask to clear function
		* don't invert it again! George,Lucas@20130513
		*/
		odm->SupportAbility &= *((u32 *)val);
		break;
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
			PFALSE_ALARM_STATISTICS FalseAlmCnt = &(odm->FalseAlmCnt);
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
	case HAL_DEF_DBG_DM_FUNC:
	{
		u8 dm_func = *((u8*)value);
		struct dm_priv *dm = &hal_data->dmpriv;

		if(dm_func == 0){ //disable all dynamic func
			odm->SupportAbility = DYNAMIC_FUNC_DISABLE;
			DBG_8192C("==> Disable all dynamic function...\n");
		}
		else if(dm_func == 1){//disable DIG
			odm->SupportAbility  &= (~DYNAMIC_BB_DIG);
			DBG_8192C("==> Disable DIG...\n");
		}
		else if(dm_func == 2){//disable High power
			odm->SupportAbility  &= (~DYNAMIC_BB_DYNAMIC_TXPWR);
		}
		else if(dm_func == 3){//disable tx power tracking
			odm->SupportAbility  &= (~DYNAMIC_RF_CALIBRATION);
			DBG_8192C("==> Disable tx power tracking...\n");
		}
		else if(dm_func == 4){//disable BT coexistence
			dm->DMFlag &= (~DYNAMIC_FUNC_BT);
		}
		else if(dm_func == 5){//disable antenna diversity
			odm->SupportAbility  &= (~DYNAMIC_BB_ANT_DIV);
		}
		else if(dm_func == 6){//turn on all dynamic func
			if(!(odm->SupportAbility  & DYNAMIC_BB_DIG)) {
				DIG_T	*pDigTable = &odm->DM_DigTable;
				pDigTable->CurIGValue= rtw_read8(adapter, 0xc50);
			}
			dm->DMFlag |= DYNAMIC_FUNC_BT;
			odm->SupportAbility = DYNAMIC_ALL_FUNC_ENABLE;
			DBG_8192C("==> Turn on all dynamic function...\n");
		}
	}
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
		case HAL_DEF_DBG_DM_FUNC:
			*(( u32*)value) =hal_data->odmpriv.SupportAbility;
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
		default:
			DBG_871X_LEVEL(_drv_always_, "%s: [WARNING] HAL_DEF_VARIABLE(%d) not defined!\n", __FUNCTION__, variable);
			bResult = _FAIL;
			break;
	}

	return bResult;
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
	if(IS_81XXC(pHalData->VersionID) || IS_92D(pHalData->VersionID) 
		|| IS_8188E(pHalData->VersionID) || IS_8723_SERIES(pHalData->VersionID)
		|| IS_8812_SERIES(pHalData->VersionID) || IS_8821_SERIES(pHalData->VersionID))
	{
		rtw_write8(adapter, REG_RXERR_RPT+3, rtw_read8(adapter, REG_RXERR_RPT+3)|0xa0);
		save_cnt = _TRUE;
	}
	else if(IS_8723B_SERIES(pHalData->VersionID) || IS_8192E(pHalData->VersionID))
	{
		//printk("8723b or 8192e , MAC_667 set 0xf0\n");
		rtw_write8(adapter, REG_RXERR_RPT+3, rtw_read8(adapter, REG_RXERR_RPT+3)|0xf0);
		save_cnt = _TRUE;
	}
	//todo: other chips 
		
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
		rtw_pm_set_lps(padapter, pwrctrlpriv->ips_org_mode);
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
