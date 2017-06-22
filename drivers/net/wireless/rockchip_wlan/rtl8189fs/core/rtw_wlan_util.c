/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
#define _RTW_WLAN_UTIL_C_

#include <drv_types.h>

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
#include <linux/inetdevice.h>
#endif

unsigned char ARTHEROS_OUI1[] = {0x00, 0x03, 0x7f};
unsigned char ARTHEROS_OUI2[] = {0x00, 0x13, 0x74};

unsigned char BROADCOM_OUI1[] = {0x00, 0x10, 0x18};
unsigned char BROADCOM_OUI2[] = {0x00, 0x0a, 0xf7};
unsigned char BROADCOM_OUI3[] = {0x00, 0x05, 0xb5};


unsigned char CISCO_OUI[] = {0x00, 0x40, 0x96};
unsigned char MARVELL_OUI[] = {0x00, 0x50, 0x43};
unsigned char RALINK_OUI[] = {0x00, 0x0c, 0x43};
unsigned char REALTEK_OUI[] = {0x00, 0xe0, 0x4c};
unsigned char AIRGOCAP_OUI[] = {0x00, 0x0a, 0xf5};

unsigned char REALTEK_96B_IE[] = {0x00, 0xe0, 0x4c, 0x02, 0x01, 0x20};

extern unsigned char RTW_WPA_OUI[];
extern unsigned char WPA_TKIP_CIPHER[4];
extern unsigned char RSN_TKIP_CIPHER[4];

#define R2T_PHY_DELAY	(0)

//#define WAIT_FOR_BCN_TO_MIN	(3000)
#define WAIT_FOR_BCN_TO_MIN	(6000)
#define WAIT_FOR_BCN_TO_MAX	(20000)

#define DISCONNECT_BY_CHK_BCN_FAIL_OBSERV_PERIOD_IN_MS 1000
#define DISCONNECT_BY_CHK_BCN_FAIL_THRESHOLD 3

static u8 rtw_basic_rate_cck[4] = {
	IEEE80211_CCK_RATE_1MB|IEEE80211_BASIC_RATE_MASK, IEEE80211_CCK_RATE_2MB|IEEE80211_BASIC_RATE_MASK,
	IEEE80211_CCK_RATE_5MB|IEEE80211_BASIC_RATE_MASK, IEEE80211_CCK_RATE_11MB|IEEE80211_BASIC_RATE_MASK
};

static u8 rtw_basic_rate_ofdm[3] = {
	IEEE80211_OFDM_RATE_6MB|IEEE80211_BASIC_RATE_MASK, IEEE80211_OFDM_RATE_12MB|IEEE80211_BASIC_RATE_MASK,
	IEEE80211_OFDM_RATE_24MB|IEEE80211_BASIC_RATE_MASK
};

static u8 rtw_basic_rate_mix[7] = {
	IEEE80211_CCK_RATE_1MB|IEEE80211_BASIC_RATE_MASK, IEEE80211_CCK_RATE_2MB|IEEE80211_BASIC_RATE_MASK,
	IEEE80211_CCK_RATE_5MB|IEEE80211_BASIC_RATE_MASK, IEEE80211_CCK_RATE_11MB|IEEE80211_BASIC_RATE_MASK,
	IEEE80211_OFDM_RATE_6MB|IEEE80211_BASIC_RATE_MASK, IEEE80211_OFDM_RATE_12MB|IEEE80211_BASIC_RATE_MASK,
	IEEE80211_OFDM_RATE_24MB|IEEE80211_BASIC_RATE_MASK
};

int new_bcn_max = 3;

int cckrates_included(unsigned char *rate, int ratelen)
{
	int	i;
	
	for(i = 0; i < ratelen; i++)
	{
		if  (  (((rate[i]) & 0x7f) == 2)	|| (((rate[i]) & 0x7f) == 4) ||
			   (((rate[i]) & 0x7f) == 11)  || (((rate[i]) & 0x7f) == 22) )
			return _TRUE;
	}

	return _FALSE;

}

int cckratesonly_included(unsigned char *rate, int ratelen)
{
	int	i;
	
	for(i = 0; i < ratelen; i++)
	{
		if  ( (((rate[i]) & 0x7f) != 2) && (((rate[i]) & 0x7f) != 4) &&
			   (((rate[i]) & 0x7f) != 11)  && (((rate[i]) & 0x7f) != 22) )
			return _FALSE;
	}
	
	return _TRUE;
}

s8 rtw_get_tx_nss(_adapter *adapter, struct sta_info *psta)
{
	u8 rf_type = RF_1T1R, custom_rf_type, vht_mcs[2];
	s8 nss = 1;

	custom_rf_type = adapter->registrypriv.rf_config;
	rtw_hal_get_hwreg(adapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
	
	if (!psta)
		return nss;
	
	/* rf_config is dependent on efuse or sw config */
	if (custom_rf_type != RF_MAX_TYPE)
		rf_type = custom_rf_type;
	
#ifdef CONFIG_80211AC_VHT
	if (psta->vhtpriv.vht_option) {
		u8 vht_mcs[2];
		struct mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
		struct vht_priv	*pvhtpriv_ap = &pmlmepriv->vhtpriv;
		
		_rtw_memcpy(vht_mcs, psta->vhtpriv.vht_mcs_map, 2);
		/* doesn't support 5~8 SS so far */
		vht_mcs[1] = 0xff;
		switch (rf_type) {
		case RF_1T1R:
		case RF_1T2R:
			vht_mcs[0] |= 0xfc;
			break;
		case RF_2T2R:
		case RF_2T4R:
		case RF_2T2R_GREEN:
		case RF_2T3R:
			vht_mcs[0] |= 0xf0;
			break;
		case RF_3T3R:
		case RF_3T4R:
			vht_mcs[0] |= 0xc0;
			break;
		default:
			DBG_871X("%s,%d, unknown rf type\n", __func__, __LINE__);
			break;
		}
		nss = rtw_vht_mcsmap_to_nss(vht_mcs);
	} else
#endif /* CONFIG_80211AC_VHT */
	if (psta->htpriv.ht_option) {
		u8 supp_mcs_set[4];

		_rtw_memcpy(supp_mcs_set, psta->htpriv.ht_cap.supp_mcs_set, 4);
		
		switch (rf_type) {
		case RF_1T1R:
		case RF_1T2R:
			supp_mcs_set[1] = supp_mcs_set[2] = supp_mcs_set[3] = 0;
			break;
		case RF_2T2R:
		case RF_2T4R:
		case RF_2T2R_GREEN:
		case RF_2T3R:
			supp_mcs_set[2] = supp_mcs_set[3] = 0;
			break;
		case RF_3T3R:
		case RF_3T4R:
			supp_mcs_set[3] = 0;
			break;
		default:
			DBG_871X("%s,%d, unknown rf type\n", __func__, __LINE__);
			break;
		}
		nss = rtw_ht_mcsset_to_nss(supp_mcs_set);
	}
	
	DBG_871X("%s: %d SS, rf_type=%d\n", __func__, nss, rf_type);
	return nss;
}

u8 networktype_to_raid(_adapter *adapter,struct sta_info *psta)
{
	unsigned char raid;
	switch(psta->wireless_mode)
	{
		case WIRELESS_11B:
			raid = RATR_INX_WIRELESS_B;
			break;
		case WIRELESS_11A:
		case WIRELESS_11G:
			raid = RATR_INX_WIRELESS_G;
			break;
		case WIRELESS_11BG:
			raid = RATR_INX_WIRELESS_GB;
			break;
		case WIRELESS_11_24N:
		case WIRELESS_11_5N:
			raid = RATR_INX_WIRELESS_N;
			break;
		case WIRELESS_11A_5N:
		case WIRELESS_11G_24N:
			raid = RATR_INX_WIRELESS_NG;
			break;
		case WIRELESS_11BG_24N:
			raid = RATR_INX_WIRELESS_NGB;
			break;
		default:
			raid = RATR_INX_WIRELESS_GB;
			break;	

	}
	return raid;
	
}

u8 networktype_to_raid_ex(_adapter *adapter, struct sta_info *psta)
{
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	u8 raid = RATEID_IDX_BGN_40M_1SS, cur_rf_type, rf_type, custom_rf_type;
	s8 tx_nss;

	tx_nss = rtw_get_tx_nss(adapter, psta);

	switch(psta->wireless_mode)
	{
		case WIRELESS_11B:
			raid = RATEID_IDX_B;
			break;
		case WIRELESS_11A:
		case WIRELESS_11G:
			raid = RATEID_IDX_G;
			break;
		case WIRELESS_11BG:
			raid = RATEID_IDX_BG;
			break;
		case WIRELESS_11_24N:
		case WIRELESS_11_5N:
		case WIRELESS_11A_5N:
		case WIRELESS_11G_24N:
			if (tx_nss == 1)
				raid = RATEID_IDX_GN_N1SS;
			else if (tx_nss == 2)
				raid = RATEID_IDX_GN_N2SS;
			else if (tx_nss == 3)
				raid = RATEID_IDX_BGN_3SS;
			else
				DBG_871X("tx_nss error!(tx_nss=%d)\n", tx_nss);
			break;
		case WIRELESS_11B_24N:
		case WIRELESS_11BG_24N:
			if (psta->bw_mode == CHANNEL_WIDTH_20) {
				if (tx_nss == 1)
					raid = RATEID_IDX_BGN_20M_1SS_BN;
				else if (tx_nss == 2)
					raid = RATEID_IDX_BGN_20M_2SS_BN;
				else if (tx_nss == 3)
					raid = RATEID_IDX_BGN_3SS;
				else
				DBG_871X("tx_nss error!(tx_nss=%d)\n", tx_nss);
			} else {
				if (tx_nss == 1)
					raid = RATEID_IDX_BGN_40M_1SS;
				else if (tx_nss == 2)
					raid = RATEID_IDX_BGN_40M_2SS;
				else if (tx_nss == 3)
					raid = RATEID_IDX_BGN_3SS;
				else
				DBG_871X("tx_nss error!(tx_nss=%d)\n", tx_nss);
			}
			break;
#ifdef CONFIG_80211AC_VHT
		case WIRELESS_11_5AC:
			if (tx_nss == 1)
				raid = RATEID_IDX_VHT_1SS;
			else if (tx_nss == 2)
				raid = RATEID_IDX_VHT_2SS;
			else if (tx_nss == 3)
				raid = RATEID_IDX_VHT_3SS;
			else
				DBG_871X("tx_nss error!(tx_nss=%d)\n", tx_nss);
			break;
		case WIRELESS_11_24AC:
			if (psta->bw_mode >= CHANNEL_WIDTH_80)
			{
				if (tx_nss == 1)
					raid = RATEID_IDX_VHT_1SS;
				else if (tx_nss == 2)
					raid = RATEID_IDX_VHT_2SS;
				else if (tx_nss == 3)
					raid = RATEID_IDX_VHT_3SS;
				else
					DBG_871X("tx_nss error!(tx_nss=%d)\n", tx_nss);
			}
			else
			{
				if (tx_nss == 1)
					raid = RATEID_IDX_MIX1;
				else if (tx_nss == 2)
					raid = RATEID_IDX_MIX2;
				else if (tx_nss == 3)
					raid = RATEID_IDX_VHT_3SS;
				else
					DBG_871X("tx_nss error!(tx_nss=%d)\n", tx_nss);
			}
			break;
#endif
		default:
			DBG_871X("unexpected wireless mode!(psta->wireless_mode=%x)\n", psta->wireless_mode);
			break;	

	}
	
	/* DBG_871X("psta->wireless_mode=%x,  tx_nss=%d\n", psta->wireless_mode, tx_nss); */
	
	return raid;
	
}

u8 judge_network_type(_adapter *padapter, unsigned char *rate, int ratelen)
{
	u8 network_type = 0;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	

	if(pmlmeext->cur_channel > 14)
	{
		if (pmlmeinfo->VHT_enable)
			network_type = WIRELESS_11AC;
		else if (pmlmeinfo->HT_enable)
			network_type = WIRELESS_11_5N;

		network_type |= WIRELESS_11A;
	}
	else
	{
		if (pmlmeinfo->HT_enable)
		{
			network_type = WIRELESS_11_24N;
		}

		if ((cckratesonly_included(rate, ratelen)) == _TRUE)
		{
			network_type |= WIRELESS_11B;
		}
		else if((cckrates_included(rate, ratelen)) == _TRUE)
		{
			network_type |= WIRELESS_11BG;
		}
		else
		{
			network_type |= WIRELESS_11G;
		}
	}
		
	return 	network_type;
}

unsigned char ratetbl_val_2wifirate(unsigned char rate);
unsigned char ratetbl_val_2wifirate(unsigned char rate)
{
	unsigned char val = 0;

	switch (rate & 0x7f) 
	{
		case 0:
			val = IEEE80211_CCK_RATE_1MB;
			break;

		case 1:
			val = IEEE80211_CCK_RATE_2MB;
			break;

		case 2:
			val = IEEE80211_CCK_RATE_5MB;
			break;

		case 3:
			val = IEEE80211_CCK_RATE_11MB;
			break;
			
		case 4:
			val = IEEE80211_OFDM_RATE_6MB;
			break;

		case 5:
			val = IEEE80211_OFDM_RATE_9MB;
			break;

		case 6:
			val = IEEE80211_OFDM_RATE_12MB;
			break;
			
		case 7:
			val = IEEE80211_OFDM_RATE_18MB;
			break;

		case 8:
			val = IEEE80211_OFDM_RATE_24MB;
			break;
			
		case 9:
			val = IEEE80211_OFDM_RATE_36MB;
			break;

		case 10:
			val = IEEE80211_OFDM_RATE_48MB;
			break;
		
		case 11:
			val = IEEE80211_OFDM_RATE_54MB;
			break;

	}

	return val;

}

int is_basicrate(_adapter *padapter, unsigned char rate);
int is_basicrate(_adapter *padapter, unsigned char rate)
{
	int i;
	unsigned char val;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	
	for(i = 0; i < NumRates; i++)
	{
		val = pmlmeext->basicrate[i];

		if ((val != 0xff) && (val != 0xfe))
		{
			if (rate == ratetbl_val_2wifirate(val))
			{
				return _TRUE;
			}
		}
	}
	
	return _FALSE;
}

unsigned int ratetbl2rateset(_adapter *padapter, unsigned char *rateset);
unsigned int ratetbl2rateset(_adapter *padapter, unsigned char *rateset)
{
	int i;
	unsigned char rate;
	unsigned int	len = 0;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	for (i = 0; i < NumRates; i++)
	{
		rate = pmlmeext->datarate[i];

		switch (rate)
		{
			case 0xff:
				return len;
				
			case 0xfe:
				continue;
				
			default:
				rate = ratetbl_val_2wifirate(rate);

				if (is_basicrate(padapter, rate) == _TRUE)
				{
					rate |= IEEE80211_BASIC_RATE_MASK;
				}
				
				rateset[len] = rate;
				len++;
				break;
		}
	}
	return len;
}

void get_rate_set(_adapter *padapter, unsigned char *pbssrate, int *bssrate_len)
{
	unsigned char supportedrates[NumRates];

	_rtw_memset(supportedrates, 0, NumRates);
	*bssrate_len = ratetbl2rateset(padapter, supportedrates);
	_rtw_memcpy(pbssrate, supportedrates, *bssrate_len);
}

void set_mcs_rate_by_mask(u8 *mcs_set, u32 mask)
{
	u8 mcs_rate_1r = (u8)(mask&0xff);
	u8 mcs_rate_2r = (u8)((mask>>8)&0xff);
	u8 mcs_rate_3r = (u8)((mask>>16)&0xff);
	u8 mcs_rate_4r = (u8)((mask>>24)&0xff);

	mcs_set[0] &= mcs_rate_1r;
	mcs_set[1] &= mcs_rate_2r;
	mcs_set[2] &= mcs_rate_3r;
	mcs_set[3] &= mcs_rate_4r;
}

void UpdateBrateTbl(
	IN PADAPTER		Adapter,
	IN u8			*mBratesOS
)
{
	u8	i;
	u8	rate;

	// 1M, 2M, 5.5M, 11M, 6M, 12M, 24M are mandatory.
	for(i=0;i<NDIS_802_11_LENGTH_RATES_EX;i++)
	{
		rate = mBratesOS[i] & 0x7f;
		switch(rate)
		{
			case IEEE80211_CCK_RATE_1MB:
			case IEEE80211_CCK_RATE_2MB:
			case IEEE80211_CCK_RATE_5MB:
			case IEEE80211_CCK_RATE_11MB:
			case IEEE80211_OFDM_RATE_6MB:
			case IEEE80211_OFDM_RATE_12MB:
			case IEEE80211_OFDM_RATE_24MB:
				mBratesOS[i] |= IEEE80211_BASIC_RATE_MASK;
				break;
		}
	}

}

void UpdateBrateTblForSoftAP(u8 *bssrateset, u32 bssratelen)
{
	u8	i;
	u8	rate;

	for(i=0;i<bssratelen;i++)
	{
		rate = bssrateset[i] & 0x7f;
		switch(rate)
		{
			case IEEE80211_CCK_RATE_1MB:
			case IEEE80211_CCK_RATE_2MB:
			case IEEE80211_CCK_RATE_5MB:
			case IEEE80211_CCK_RATE_11MB:
				bssrateset[i] |= IEEE80211_BASIC_RATE_MASK;
				break;
		}
	}

}
static void Set_NETYPE1_MSR(_adapter *padapter, u8 type)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_MEDIA_STATUS1, (u8 *)(&type));
}

static void Set_NETYPE0_MSR(_adapter *padapter, u8 type)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_MEDIA_STATUS, (u8 *)(&type));
}

void Set_MSR(_adapter *padapter, u8 type)
{
#ifdef CONFIG_CONCURRENT_MODE
	if(padapter->iface_type == IFACE_PORT1)
	{
		Set_NETYPE1_MSR(padapter, type);
	}
	else
#endif
	{
		Set_NETYPE0_MSR(padapter, type);
	}
}

inline u8 rtw_get_oper_ch(_adapter *adapter)
{
	return adapter_to_dvobj(adapter)->oper_channel;
}

inline void rtw_set_oper_ch(_adapter *adapter, u8 ch)
{
#ifdef DBG_CH_SWITCH
	const int len = 128;
	char msg[128] = {0};
	int cnt = 0;
	int i = 0;
#endif  /* DBG_CH_SWITCH */
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	
	if (dvobj->oper_channel != ch) {
		dvobj->on_oper_ch_time = rtw_get_current_time();

#ifdef DBG_CH_SWITCH
		cnt += snprintf(msg+cnt, len-cnt, "switch to ch %3u", ch);

		for (i = 0; i < dvobj->iface_nums; i++) {
			_adapter *iface = dvobj->padapters[i];
			cnt += snprintf(msg+cnt, len-cnt, " ["ADPT_FMT":", ADPT_ARG(iface));
			if (iface->mlmeextpriv.cur_channel == ch)
				cnt += snprintf(msg+cnt, len-cnt, "C");
			else
				cnt += snprintf(msg+cnt, len-cnt, "_");
			if (iface->wdinfo.listen_channel == ch && !rtw_p2p_chk_state(&iface->wdinfo, P2P_STATE_NONE))
				cnt += snprintf(msg+cnt, len-cnt, "L");
			else
				cnt += snprintf(msg+cnt, len-cnt, "_");
			cnt += snprintf(msg+cnt, len-cnt, "]");
		}

		DBG_871X(FUNC_ADPT_FMT" %s\n", FUNC_ADPT_ARG(adapter), msg);
#endif /* DBG_CH_SWITCH */
	}

	dvobj->oper_channel = ch;
}

inline u8 rtw_get_oper_bw(_adapter *adapter)
{
	return adapter_to_dvobj(adapter)->oper_bwmode;
}

inline void rtw_set_oper_bw(_adapter *adapter, u8 bw)
{
	adapter_to_dvobj(adapter)->oper_bwmode = bw;
}

inline u8 rtw_get_oper_choffset(_adapter *adapter)
{
	return adapter_to_dvobj(adapter)->oper_ch_offset;
}

inline void rtw_set_oper_choffset(_adapter *adapter, u8 offset)
{
	adapter_to_dvobj(adapter)->oper_ch_offset = offset;
}

u8 rtw_get_offset_by_ch(u8 channel)
{
	u8 offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	if(channel>=1 && channel<=4)
	{
		offset = HAL_PRIME_CHNL_OFFSET_LOWER;
	}
	else if(channel>=5 && channel<=14)
	{
		offset = HAL_PRIME_CHNL_OFFSET_UPPER;						
	}
	else
	{
		switch(channel)
		{
			case 36:
			case 44:
			case 52:
			case 60:
			case 100:
			case 108:
			case 116:
			case 124:
			case 132:
			case 149:
			case 157:
				offset = HAL_PRIME_CHNL_OFFSET_LOWER;				
				break;		
			case 40:
			case 48:
			case 56:
			case 64:
			case 104:
			case 112:
			case 120:
			case 128:
			case 136:
			case 153:
			case 161:				
				offset = HAL_PRIME_CHNL_OFFSET_UPPER;				
				break;				
			default:
				offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
				break;
		}

	}

	return offset;
		
}

u8	rtw_get_center_ch(u8 channel, u8 chnl_bw, u8 chnl_offset)
{
	u8	center_ch = channel;

	if(chnl_bw == CHANNEL_WIDTH_80)
	{
		if((channel == 36) || (channel == 40) || (channel == 44) || (channel == 48) )
			center_ch = 42;
		if((channel == 52) || (channel == 56) || (channel == 60) || (channel == 64) )
			center_ch = 58;
		if((channel == 100) || (channel == 104) || (channel == 108) || (channel == 112) )
			center_ch = 106;
		if((channel == 116) || (channel == 120) || (channel == 124) || (channel == 128) )
			center_ch = 122;
		if((channel == 132) || (channel == 136) || (channel == 140) || (channel == 144) )
			center_ch = 138;
		if((channel == 149) || (channel == 153) || (channel == 157) || (channel == 161) )
			center_ch = 155;
		else if(channel <= 14)
			center_ch = 7;
	}
	else if(chnl_bw == CHANNEL_WIDTH_40)
	{
		if (chnl_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
			center_ch = channel + 2;
		else
			center_ch = channel - 2;
	}

	return center_ch;
}

inline u32 rtw_get_on_oper_ch_time(_adapter *adapter)
{
	return adapter_to_dvobj(adapter)->on_oper_ch_time;
}

inline u32 rtw_get_on_cur_ch_time(_adapter *adapter)
{
	if (adapter->mlmeextpriv.cur_channel == adapter_to_dvobj(adapter)->oper_channel)
		return adapter_to_dvobj(adapter)->on_oper_ch_time;
	else
		return 0;
}

void set_channel_bwmode(_adapter *padapter, unsigned char channel, unsigned char channel_offset, unsigned short bwmode)
{
	u8 center_ch, chnl_offset80 = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	if ( padapter->bNotifyChannelChange )
	{
		DBG_871X( "[%s] ch = %d, offset = %d, bwmode = %d\n", __FUNCTION__, channel, channel_offset, bwmode );
	}

	center_ch = rtw_get_center_ch(channel, bwmode, channel_offset);

	if(bwmode == CHANNEL_WIDTH_80)
	{
		if(center_ch > channel)
			chnl_offset80 = HAL_PRIME_CHNL_OFFSET_LOWER;
		else if(center_ch < channel)
			chnl_offset80 = HAL_PRIME_CHNL_OFFSET_UPPER;
		else
			chnl_offset80 = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	}
	_enter_critical_mutex(&(adapter_to_dvobj(padapter)->setch_mutex), NULL);

#ifdef CONFIG_DFS_MASTER
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
	bool ori_overlap_radar_detect_ch = rtw_rfctl_overlap_radar_detect_ch(rfctl);
	bool new_overlap_radar_detect_ch = _rtw_rfctl_overlap_radar_detect_ch(rfctl, channel, bwmode, channel_offset);

	if (!ori_overlap_radar_detect_ch && new_overlap_radar_detect_ch)
		rtw_odm_radar_detect_enable(padapter);

	if (new_overlap_radar_detect_ch && IS_UNDER_CAC(rfctl)) {
		u8 pause = 0xFF;

		rtw_hal_set_hwreg(padapter, HW_VAR_TXPAUSE, &pause);
	}
#endif /* CONFIG_DFS_MASTER */

	//set Channel
	//saved channel/bw info
	rtw_set_oper_ch(padapter, channel);
	rtw_set_oper_bw(padapter, bwmode);
	rtw_set_oper_choffset(padapter, channel_offset);

	rtw_hal_set_chnl_bw(padapter, center_ch, bwmode, channel_offset, chnl_offset80); // set center channel

#ifdef CONFIG_DFS_MASTER
	if (ori_overlap_radar_detect_ch && !new_overlap_radar_detect_ch) {
		u8 pause = 0x00;

		rtw_odm_radar_detect_disable(padapter);
		rtw_hal_set_hwreg(padapter, HW_VAR_TXPAUSE, &pause);
	}
}
#endif /* CONFIG_DFS_MASTER */

	_exit_critical_mutex(&(adapter_to_dvobj(padapter)->setch_mutex), NULL);
}

int get_bsstype(unsigned short capability)
{
	if (capability & BIT(0))
	{
		return WIFI_FW_AP_STATE;
	}
	else if (capability & BIT(1))
	{
		return WIFI_FW_ADHOC_STATE;
	}
	else
	{
		return 0;		
	}
}

__inline u8 *get_my_bssid(WLAN_BSSID_EX *pnetwork)
{	
	return (pnetwork->MacAddress); 
}

u16 get_beacon_interval(WLAN_BSSID_EX *bss)
{
	unsigned short val;
	_rtw_memcpy((unsigned char *)&val, rtw_get_beacon_interval_from_ie(bss->IEs), 2);

	return le16_to_cpu(val);	

}

int is_client_associated_to_ap(_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext;
	struct mlme_ext_info	*pmlmeinfo;

	if(!padapter)
		return _FAIL;

	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &(pmlmeext->mlmext_info);
	
	if ((pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) && ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE))
	{
		return _TRUE;
	}
	else
	{
		return _FAIL;
	}
}

int is_client_associated_to_ibss(_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
	if ((pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) && ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE))
	{
		return _TRUE;
	}
	else
	{
		return _FAIL;
	}
}

int is_IBSS_empty(_adapter *padapter)
{
	int i;
	struct macid_ctl_t *macid_ctl = &padapter->dvobj->macid_ctl;

	for (i = 0; i < macid_ctl->num; i++) {
		if (!rtw_macid_is_used(macid_ctl, i))
			continue;
		if (rtw_macid_get_if_g(macid_ctl, i) != padapter->iface_id)
			continue;
		if (!GET_H2CCMD_MSRRPT_PARM_OPMODE(&macid_ctl->h2c_msr[i]))
			continue;
		if (GET_H2CCMD_MSRRPT_PARM_ROLE(&macid_ctl->h2c_msr[i]) == H2C_MSR_ROLE_ADHOC)
			return _FAIL;
	}

	return _TRUE;
}

unsigned int decide_wait_for_beacon_timeout(unsigned int bcn_interval)
{
	if ((bcn_interval << 2) < WAIT_FOR_BCN_TO_MIN)
	{
		return WAIT_FOR_BCN_TO_MIN;
	} 
	else if ((bcn_interval << 2) > WAIT_FOR_BCN_TO_MAX)
	{
		return WAIT_FOR_BCN_TO_MAX;
	}	
	else
	{
		return ((bcn_interval << 2));
	}
}

void CAM_empty_entry(
	PADAPTER     	Adapter,	
	u8 			ucIndex
)
{
	rtw_hal_set_hwreg(Adapter, HW_VAR_CAM_EMPTY_ENTRY, (u8 *)(&ucIndex));
}

void invalidate_cam_all(_adapter *padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	_irqL irqL;
	u8 val8 = 0;

	rtw_hal_set_hwreg(padapter, HW_VAR_CAM_INVALID_ALL, &val8);

	_enter_critical_bh(&cam_ctl->lock, &irqL);
	rtw_sec_cam_map_clr_all(&cam_ctl->used);
	_rtw_memset(dvobj->cam_cache, 0, sizeof(struct sec_cam_ent) * SEC_CAM_ENT_NUM_SW_LIMIT);
	_exit_critical_bh(&cam_ctl->lock, &irqL);
}

void _clear_cam_entry(_adapter *padapter, u8 entry)
{
	unsigned char null_sta[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	unsigned char null_key[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x00, 0x00, 0x00, 0x00,0x00, 0x00, 0x00, 0x00};

	rtw_sec_write_cam_ent(padapter, entry, 0, null_sta, null_key);
}

inline void write_cam(_adapter *adapter, u8 id, u16 ctrl, u8 *mac, u8 *key)
{
#ifdef CONFIG_WRITE_CACHE_ONLY
	write_cam_cache(adapter, id ,ctrl, mac, key);
#else
	rtw_sec_write_cam_ent(adapter, id, ctrl, mac, key);
	write_cam_cache(adapter, id ,ctrl, mac, key);
#endif
}

inline void clear_cam_entry(_adapter *adapter, u8 id)
{
	_clear_cam_entry(adapter, id);
	clear_cam_cache(adapter, id);
}

inline void write_cam_from_cache(_adapter *adapter, u8 id)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	_irqL irqL;
	struct sec_cam_ent cache;

	_enter_critical_bh(&cam_ctl->lock, &irqL);
	_rtw_memcpy(&cache, &dvobj->cam_cache[id], sizeof(struct sec_cam_ent));
	_exit_critical_bh(&cam_ctl->lock, &irqL);

	rtw_sec_write_cam_ent(adapter, id, cache.ctrl, cache.mac, cache.key);
}

void write_cam_cache(_adapter *adapter, u8 id, u16 ctrl, u8 *mac, u8 *key)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	_irqL irqL;

	_enter_critical_bh(&cam_ctl->lock, &irqL);

	dvobj->cam_cache[id].ctrl = ctrl;
	_rtw_memcpy(dvobj->cam_cache[id].mac, mac, ETH_ALEN);
	_rtw_memcpy(dvobj->cam_cache[id].key, key, 16);

	_exit_critical_bh(&cam_ctl->lock, &irqL);
}

void clear_cam_cache(_adapter *adapter, u8 id)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	_irqL irqL;

	_enter_critical_bh(&cam_ctl->lock, &irqL);

	_rtw_memset(&(dvobj->cam_cache[id]), 0, sizeof(struct sec_cam_ent));

	_exit_critical_bh(&cam_ctl->lock, &irqL);
}

s16 rtw_get_camid(_adapter *adapter, struct sta_info *sta, s16 kid)
{
	u8 macid;
	s16 camid;

	//cam_entry:
	//0~3 for default key

	//for concurrent mode (ap+sta, sta+sta):
	//default key is disable, using sw encrypt/decrypt
	//camid 0, 1, 2, 3 is default entry for default key/group key
	//macid = 1 is for bc/mc stainfo, no mapping to camid
	//macid = 0 mapping to camid 4
	//for macid >=2, camid = macid+3;

	if (sta) {
		struct mlme_ext_info *mlmeinfo = &adapter->mlmeextpriv.mlmext_info;
		macid = sta->mac_id;

		if((mlmeinfo->state&0x03) == WIFI_FW_AP_STATE) {
			if((macid == 1) || (macid>(NUM_STA-4))){
				DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" failed, mac_id=%d\n", FUNC_ADPT_ARG(adapter), macid);
				camid = -1;
				goto exit;
			}
		}

		if(macid==0)
			camid = 4;
		else if(macid >=2)
			camid = macid + 3;
		else
			camid = 4;
	}
	else {
		/* default key is disabled */
		camid = -1;
	}

exit:
	return (s16)camid;
}

inline bool _rtw_camctl_chk_cap(_adapter *adapter, u8 cap)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;

	if (cam_ctl->sec_cap & cap)
		return _TRUE;
	return _FALSE;
}

inline void _rtw_camctl_set_flags(_adapter *adapter, u32 flags)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;

	cam_ctl->flags |= flags;
}

inline void rtw_camctl_set_flags(_adapter *adapter, u32 flags)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	_irqL irqL;

	_enter_critical_bh(&cam_ctl->lock, &irqL);
	_rtw_camctl_set_flags(adapter, flags);
	_exit_critical_bh(&cam_ctl->lock, &irqL);
}

inline void _rtw_camctl_clr_flags(_adapter *adapter, u32 flags)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;

	cam_ctl->flags &= ~flags;
}

inline void rtw_camctl_clr_flags(_adapter *adapter, u32 flags)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	_irqL irqL;

	_enter_critical_bh(&cam_ctl->lock, &irqL);
	_rtw_camctl_clr_flags(adapter, flags);
	_exit_critical_bh(&cam_ctl->lock, &irqL);
}

inline bool _rtw_camctl_chk_flags(_adapter *adapter, u32 flags)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;

	if (cam_ctl->flags & flags)
		return _TRUE;
	return _FALSE;
}

void dump_sec_cam_map(void *sel, struct sec_cam_bmp *map, u8 max_num)
{
	DBG_871X_SEL_NL(sel, "0x%08x\n", map->m0);
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 32)
	if (max_num && max_num > 32)
		DBG_871X_SEL_NL(sel, "0x%08x\n", map->m1);
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 64)
	if (max_num && max_num > 64)
		DBG_871X_SEL_NL(sel, "0x%08x\n", map->m2);
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 96)
	if (max_num && max_num > 96)
		DBG_871X_SEL_NL(sel, "0x%08x\n", map->m3);
#endif
}

inline bool rtw_sec_camid_is_set(struct sec_cam_bmp *map, u8 id)
{
	if (id < 32)
		return (map->m0 & BIT(id));
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 32)
	else if (id < 64)
		return (map->m1 & BIT(id - 32));
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 64)
	else if (id < 96)
		return (map->m2 & BIT(id - 64));
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 96)
	else if (id < 128)
		return (map->m3 & BIT(id - 96));
#endif
	else
		rtw_warn_on(1);

	return 0;
}

inline void rtw_sec_cam_map_set(struct sec_cam_bmp *map, u8 id)
{
	if (id < 32)
		map->m0 |= BIT(id);
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 32)
	else if (id < 64)
		map->m1 |= BIT(id - 32);
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 64)
	else if (id < 96)
		map->m2 |= BIT(id - 64);
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 96)
	else if (id < 128)
		map->m3 |= BIT(id - 96);
#endif
	else
		rtw_warn_on(1);
}

inline void rtw_sec_cam_map_clr(struct sec_cam_bmp *map, u8 id)
{
	if (id < 32)
		map->m0 &= ~BIT(id);
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 32)
	else if (id < 64)
		map->m1 &= ~BIT(id-32);
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 64)
	else if (id < 96)
		map->m2 &= ~BIT(id-64);
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 96)
	else if (id < 128)
		map->m3 &= ~BIT(id-96);
#endif
	else
		rtw_warn_on(1);
}

inline void rtw_sec_cam_map_clr_all(struct sec_cam_bmp *map)
{
	map->m0 = 0;
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 32)
	map->m1 = 0;
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 64)
	map->m2 = 0;
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 96)
	map->m3 = 0;
#endif
}

inline bool rtw_sec_camid_is_drv_forbid(struct cam_ctl_t *cam_ctl, u8 id)
{
	struct sec_cam_bmp forbid_map;

	forbid_map.m0 = 0x00000ff0;
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 32)
	forbid_map.m1 = 0x00000000;
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 64)
	forbid_map.m2 = 0x00000000;
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 96)
	forbid_map.m3 = 0x00000000;
#endif

	if (id < 32)
		return (forbid_map.m0 & BIT(id));
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 32)
	else if (id < 64)
		return (forbid_map.m1 & BIT(id - 32));
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 64)
	else if (id < 96)
		return (forbid_map.m2 & BIT(id - 64));
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 96)
	else if (id < 128)
		return (forbid_map.m3 & BIT(id - 96));
#endif
	else
		rtw_warn_on(1);

	return 1;
}

bool _rtw_sec_camid_is_used(struct cam_ctl_t *cam_ctl, u8 id)
{
	bool ret = _FALSE;

	if (id >= cam_ctl->num) {
		rtw_warn_on(1);
		goto exit;
	}

	#if 0 /* for testing */
	if (rtw_sec_camid_is_drv_forbid(cam_ctl, id)) {
		ret = _TRUE;
		goto exit;
	}
	#endif

	ret = rtw_sec_camid_is_set(&cam_ctl->used, id);

exit:
	return ret;
}

inline bool rtw_sec_camid_is_used(struct cam_ctl_t *cam_ctl, u8 id)
{
	_irqL irqL;
	bool ret;

	_enter_critical_bh(&cam_ctl->lock, &irqL);
	ret = _rtw_sec_camid_is_used(cam_ctl, id);
	_exit_critical_bh(&cam_ctl->lock, &irqL);

	return ret;
}

inline bool _rtw_camid_is_gk(_adapter *adapter, u8 cam_id)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	bool ret = _FALSE;

	if (cam_id >= cam_ctl->num) {
		rtw_warn_on(1);
		goto exit;
	}

	if (_rtw_sec_camid_is_used(cam_ctl, cam_id) == _FALSE)
		goto exit;
	
	ret = (dvobj->cam_cache[cam_id].ctrl&BIT6)?_TRUE:_FALSE;

exit:
	return ret;
}

inline bool rtw_camid_is_gk(_adapter *adapter, u8 cam_id)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	_irqL irqL;
	bool ret;

	_enter_critical_bh(&cam_ctl->lock, &irqL);
	ret = _rtw_camid_is_gk(adapter, cam_id);
	_exit_critical_bh(&cam_ctl->lock, &irqL);

	return ret;
}

bool cam_cache_chk(_adapter *adapter, u8 id, u8 *addr, s16 kid, s8 gk)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	bool ret = _FALSE;

	if (addr && _rtw_memcmp(dvobj->cam_cache[id].mac, addr, ETH_ALEN) == _FALSE)
		goto exit;
	if (kid >= 0 && kid != (dvobj->cam_cache[id].ctrl&0x03))
		goto exit;
	if (gk != -1 && (gk?_TRUE:_FALSE) != _rtw_camid_is_gk(adapter, id))
		goto exit;

	ret = _TRUE;

exit:
	return ret;
}

s16 _rtw_camid_search(_adapter *adapter, u8 *addr, s16 kid, s8 gk)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	int i;
	s16 cam_id = -1;

	for (i = 0; i < cam_ctl->num; i++) {
		if (cam_cache_chk(adapter, i, addr, kid, gk)) {
			cam_id = i;
			break;
		}
	}

	if (0) {
		if (addr)
			DBG_871X(FUNC_ADPT_FMT" addr:"MAC_FMT" kid:%d, gk:%d, return cam_id:%d\n"
				, FUNC_ADPT_ARG(adapter), MAC_ARG(addr), kid, gk, cam_id);
		else
			DBG_871X(FUNC_ADPT_FMT" addr:%p kid:%d, gk:%d, return cam_id:%d\n"
				, FUNC_ADPT_ARG(adapter), addr, kid, gk, cam_id);
	}

	return cam_id;
}

s16 rtw_camid_search(_adapter *adapter, u8 *addr, s16 kid, s8 gk)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	_irqL irqL;
	s16 cam_id = -1;

	_enter_critical_bh(&cam_ctl->lock, &irqL);
	cam_id = _rtw_camid_search(adapter, addr, kid, gk);
	_exit_critical_bh(&cam_ctl->lock, &irqL);

	return cam_id;
}

s16 rtw_camid_alloc(_adapter *adapter, struct sta_info *sta, u8 kid, bool *used)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	_irqL irqL;
	s16 cam_id = -1;

	*used = _FALSE;

	_enter_critical_bh(&cam_ctl->lock, &irqL);

#ifdef DYNAMIC_CAMID_ALLOC
	{
		struct mlme_ext_info *mlmeinfo = &adapter->mlmeextpriv.mlmext_info;

		if((((mlmeinfo->state&0x03) == WIFI_FW_AP_STATE) || ((mlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE))
			&& !sta) {
			/* AP/Ad-hoc mode group key: static alloction to default key by key ID */
			if (kid > 3) {
				DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" group key with invalid key id:%u\n"
					, FUNC_ADPT_ARG(adapter), kid);
				rtw_warn_on(1);
				goto bitmap_handle;
			}
			cam_id = kid;
		}
		else {
			int i;
			u8 *addr = sta?sta->hwaddr:NULL;
			#if 0 /* for testing */
			static u8 start_id = 0;
			#else
			u8 start_id = 0;
			#endif

			if(!sta) {
				if (!(mlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)) {
					/* bypass STA mode group key setting before connected(ex:WEP) because bssid is not ready */
					goto bitmap_handle;
				}

				addr = get_bssid(&adapter->mlmepriv);
			}

			/* find cam entry which has the same addr, kid (, gk bit) */
			if (_rtw_camctl_chk_cap(adapter, SEC_CAP_CHK_BMC) == _TRUE)
				i = _rtw_camid_search(adapter, addr, kid, sta?_FALSE:_TRUE);
			else
				i = _rtw_camid_search(adapter, addr, kid, -1);

			if (i >= 0) {
				cam_id = i;
				goto bitmap_handle;
			}

			for (i = 0; i < cam_ctl->num; i++) {
				/* bypass default key which is allocated statically */
				if (((i + start_id) % cam_ctl->num) < 4)
					continue;

				if (_rtw_sec_camid_is_used(cam_ctl, ((i + start_id) % cam_ctl->num)) == _FALSE)
					break;
			}

			if (i == cam_ctl->num) {
				if (sta)
					DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" pairwise key with "MAC_FMT" id:%u no room\n"
					, FUNC_ADPT_ARG(adapter), MAC_ARG(addr), kid);
				else
					DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" group key with "MAC_FMT" id:%u no room\n"
					, FUNC_ADPT_ARG(adapter), MAC_ARG(addr), kid);
				rtw_warn_on(1);
				goto bitmap_handle;
			}

			cam_id = ((i + start_id) % cam_ctl->num);
			start_id = ((i + start_id + 1) % cam_ctl->num);
		}
	}
#else
	cam_id = rtw_get_camid(adapter, sta, kid);
#endif /* DYNAMIC_CAMID_ALLOC */

bitmap_handle:
	if (cam_id >= 0) {
		*used = _rtw_sec_camid_is_used(cam_ctl, cam_id);
		rtw_sec_cam_map_set(&cam_ctl->used, cam_id);
	}

	_exit_critical_bh(&cam_ctl->lock, &irqL);

	return cam_id;
}

void rtw_camid_free(_adapter *adapter, u8 cam_id)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	_irqL irqL;

	_enter_critical_bh(&cam_ctl->lock, &irqL);

	if (cam_id < cam_ctl->num)
		rtw_sec_cam_map_clr(&cam_ctl->used, cam_id);

	_exit_critical_bh(&cam_ctl->lock, &irqL);
}

void flush_all_cam_entry(_adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

#ifdef CONFIG_CONCURRENT_MODE
	if(check_buddy_fwstate(padapter, _FW_LINKED) == _TRUE)
	{
		if(check_fwstate(pmlmepriv, WIFI_STATION_STATE))
		{
			struct sta_priv	*pstapriv = &padapter->stapriv;
			struct sta_info	*psta;

			psta = rtw_get_stainfo(pstapriv, pmlmeinfo->network.MacAddress);
			if(psta) {
				if(psta->state & WIFI_AP_STATE)
				{}   //clear cam when ap free per sta_info        
				else {
					rtw_clearstakey_cmd(padapter, psta, _FALSE);
				}
			}
		}
		else if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
		{
			/* clear default key */
			int i, cam_id;
			u8 null_addr[ETH_ALEN]= {0,0,0,0,0,0};

			for (i=0;i<4;i++) {
				cam_id = rtw_camid_search(padapter, null_addr, i, -1);
				if (cam_id >= 0) {
					clear_cam_entry(padapter, cam_id);
					rtw_camid_free(padapter, cam_id);
				}
			}

			/* clear default key related key search setting */
			#ifdef DYNAMIC_CAMID_ALLOC
			rtw_hal_set_hwreg(padapter, HW_VAR_SEC_DK_CFG, (u8*)_FALSE);
			#endif

			/* leave pairwise key when ap free per sta_info */
		}
	}
	else
#endif //CONFIG_CONCURRENT_MODE
	{
		invalidate_cam_all(padapter);
		/* clear default key related key search setting */
		#ifdef DYNAMIC_CAMID_ALLOC
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_DK_CFG, (u8*)_FALSE);
		#endif
	}
}

#if defined(CONFIG_P2P) && defined(CONFIG_WFD)
void rtw_process_wfd_ie(_adapter *adapter, u8 *wfd_ie, u8 wfd_ielen, const char *tag)
{
	struct wifidirect_info *wdinfo = &adapter->wdinfo;

	u8 *attr_content;
	u32 attr_contentlen = 0;

	if (!hal_chk_wl_func(adapter, WL_FUNC_MIRACAST))
		return;

	DBG_871X("[%s] Found WFD IE\n", tag);
	attr_content = rtw_get_wfd_attr_content(wfd_ie, wfd_ielen, WFD_ATTR_DEVICE_INFO, NULL, &attr_contentlen);
	if (attr_content && attr_contentlen) {
		wdinfo->wfd_info->peer_rtsp_ctrlport = RTW_GET_BE16(attr_content + 2);
		DBG_871X("[%s] Peer PORT NUM = %d\n", tag, wdinfo->wfd_info->peer_rtsp_ctrlport);
	}
}

void rtw_process_wfd_ies(_adapter *adapter, u8 *ies, u8 ies_len, const char *tag)
{
	u8 *wfd_ie;
	u32	wfd_ielen;

	if (!hal_chk_wl_func(adapter, WL_FUNC_MIRACAST))
		return;

	wfd_ie = rtw_get_wfd_ie(ies, ies_len, NULL, &wfd_ielen);
	while (wfd_ie) {
		rtw_process_wfd_ie(adapter, wfd_ie, wfd_ielen, tag);
		wfd_ie = rtw_get_wfd_ie(wfd_ie + wfd_ielen, (ies + ies_len) - (wfd_ie + wfd_ielen), NULL, &wfd_ielen);
	}
}
#endif /* defined(CONFIG_P2P) && defined(CONFIG_WFD) */

int WMM_param_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs	pIE)
{
	//struct registry_priv	*pregpriv = &padapter->registrypriv;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);	
	
	if(pmlmepriv->qospriv.qos_option==0)
	{
		pmlmeinfo->WMM_enable = 0;
		return _FALSE;
	}	
	
	if(_rtw_memcmp(&(pmlmeinfo->WMM_param), (pIE->data + 6), sizeof(struct WMM_para_element)))
	{
		return _FALSE;
	}
	else
	{
		_rtw_memcpy(&(pmlmeinfo->WMM_param), (pIE->data + 6), sizeof(struct WMM_para_element));
	}
	pmlmeinfo->WMM_enable = 1;
	return _TRUE;

	/*if (pregpriv->wifi_spec == 1)
	{
		if (pmlmeinfo->WMM_enable == 1)
		{
			//todo: compare the parameter set count & decide wheher to update or not
			return _FAIL;
		}
		else
		{
			pmlmeinfo->WMM_enable = 1;
			_rtw_rtw_memcpy(&(pmlmeinfo->WMM_param), (pIE->data + 6), sizeof(struct WMM_para_element));
			return _TRUE;
		}
	}
	else
	{
		pmlmeinfo->WMM_enable = 0;
		return _FAIL;
	}*/
	
}

void WMMOnAssocRsp(_adapter *padapter)
{
	u8	ACI, ACM, AIFS, ECWMin, ECWMax, aSifsTime;
	u8	acm_mask;
	u16	TXOP;
	u32	acParm, i;
	u32	edca[4], inx[4];
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct xmit_priv		*pxmitpriv = &padapter->xmitpriv;
	struct registry_priv	*pregpriv = &padapter->registrypriv;

	acm_mask = 0;

	if (IsSupported5G(pmlmeext->cur_wireless_mode) || 
		(pmlmeext->cur_wireless_mode & WIRELESS_11_24N) )
		aSifsTime = 16;
	else
		aSifsTime = 10;

	if (pmlmeinfo->WMM_enable == 0)
	{
		padapter->mlmepriv.acm_mask = 0;

		AIFS = aSifsTime + (2 * pmlmeinfo->slotTime);

		if (pmlmeext->cur_wireless_mode & (WIRELESS_11G |WIRELESS_11A)) {
			ECWMin = 4;
			ECWMax = 10;
		} else if (pmlmeext->cur_wireless_mode & WIRELESS_11B) {
			ECWMin = 5;
			ECWMax = 10;
		} else {
			ECWMin = 4;
			ECWMax = 10;
		}

		TXOP = 0;
		acParm = AIFS | (ECWMin << 8) | (ECWMax << 12) | (TXOP << 16);
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acParm));
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acParm));
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&acParm));

		ECWMin = 2;
		ECWMax = 3;
		TXOP = 0x2f;
		acParm = AIFS | (ECWMin << 8) | (ECWMax << 12) | (TXOP << 16);
		rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&acParm));
	}
	else
	{
		edca[0] = edca[1] = edca[2] = edca[3] = 0;

		for (i = 0; i < 4; i++)  
		{
			ACI = (pmlmeinfo->WMM_param.ac_param[i].ACI_AIFSN >> 5) & 0x03;
			ACM = (pmlmeinfo->WMM_param.ac_param[i].ACI_AIFSN >> 4) & 0x01;

			//AIFS = AIFSN * slot time + SIFS - r2t phy delay
			AIFS = (pmlmeinfo->WMM_param.ac_param[i].ACI_AIFSN & 0x0f) * pmlmeinfo->slotTime + aSifsTime;

			ECWMin = (pmlmeinfo->WMM_param.ac_param[i].CW & 0x0f);
			ECWMax = (pmlmeinfo->WMM_param.ac_param[i].CW & 0xf0) >> 4;
			TXOP = le16_to_cpu(pmlmeinfo->WMM_param.ac_param[i].TXOP_limit);

			acParm = AIFS | (ECWMin << 8) | (ECWMax << 12) | (TXOP << 16);

			switch (ACI)
			{
				case 0x0:
					rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acParm));
					acm_mask |= (ACM? BIT(1):0);
					edca[XMIT_BE_QUEUE] = acParm;
					break;

				case 0x1:
					rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acParm));
					//acm_mask |= (ACM? BIT(0):0);
					edca[XMIT_BK_QUEUE] = acParm;
					break;

				case 0x2:
					rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&acParm));
					acm_mask |= (ACM? BIT(2):0);
					edca[XMIT_VI_QUEUE] = acParm;
					break;

				case 0x3:
					rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&acParm));
					acm_mask |= (ACM? BIT(3):0);
					edca[XMIT_VO_QUEUE] = acParm;
					break;							
			}

			DBG_871X("WMM(%x): %x, %x\n", ACI, ACM, acParm);
		}

		if(padapter->registrypriv.acm_method == 1)
			rtw_hal_set_hwreg(padapter, HW_VAR_ACM_CTRL, (u8 *)(&acm_mask));
		else
			padapter->mlmepriv.acm_mask = acm_mask;

		inx[0] = 0; inx[1] = 1; inx[2] = 2; inx[3] = 3;

		if(pregpriv->wifi_spec==1)
		{
			u32	j, tmp, change_inx=_FALSE;

			//entry indx: 0->vo, 1->vi, 2->be, 3->bk.
			for(i=0; i<4; i++)
			{
				for(j=i+1; j<4; j++)
				{
					//compare CW and AIFS
					if((edca[j] & 0xFFFF) < (edca[i] & 0xFFFF))
					{
						change_inx = _TRUE;
					}
					else if((edca[j] & 0xFFFF) == (edca[i] & 0xFFFF))
					{
						//compare TXOP
						if((edca[j] >> 16) > (edca[i] >> 16))
							change_inx = _TRUE;
					}
				
					if(change_inx)
					{
						tmp = edca[i];
						edca[i] = edca[j];
						edca[j] = tmp;

						tmp = inx[i];
						inx[i] = inx[j];
						inx[j] = tmp;

						change_inx = _FALSE;
					}
				}
			}
		}

		for(i=0; i<4; i++) {
			pxmitpriv->wmm_para_seq[i] = inx[i];
			DBG_871X("wmm_para_seq(%d): %d\n", i, pxmitpriv->wmm_para_seq[i]);
		}
	}
}

static void bwmode_update_check(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
#ifdef CONFIG_80211N_HT
	unsigned char	 new_bwmode;
	unsigned char  new_ch_offset;
	struct HT_info_element	 *pHT_info;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct ht_priv			*phtpriv = &pmlmepriv->htpriv;
	u8	cbw40_enable=0;

	if(!pIE)
		return;

	if(phtpriv->ht_option == _FALSE)	return;

	if(pmlmeext->cur_bwmode >= CHANNEL_WIDTH_80)	return;

	if(pIE->Length > sizeof(struct HT_info_element))
		return;
	
	pHT_info = (struct HT_info_element *)pIE->data;

	if (hal_chk_bw_cap(padapter, BW_CAP_40M)) {
		if (pmlmeext->cur_channel > 14) {
			if (REGSTY_IS_BW_5G_SUPPORT(pregistrypriv, CHANNEL_WIDTH_40))
				cbw40_enable = 1;
		} else {
			if (REGSTY_IS_BW_2G_SUPPORT(pregistrypriv, CHANNEL_WIDTH_40))
				cbw40_enable = 1;
		}
	}

	if((pHT_info->infos[0] & BIT(2)) && cbw40_enable)
	{
		new_bwmode = CHANNEL_WIDTH_40;

		switch (pHT_info->infos[0] & 0x3)
		{
			case 1:
				new_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
				break;
			
			case 3:
				new_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
				break;
				
			default:
				new_bwmode = CHANNEL_WIDTH_20;
				new_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
				break;
		}
	}
	else
	{
		new_bwmode = CHANNEL_WIDTH_20;
		new_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	}	

	
	if ((new_bwmode != pmlmeext->cur_bwmode || new_ch_offset != pmlmeext->cur_ch_offset)
		&& new_bwmode < pmlmeext->cur_bwmode
	) {
		pmlmeinfo->bwmode_updated = _TRUE;
		
		pmlmeext->cur_bwmode = new_bwmode;
		pmlmeext->cur_ch_offset = new_ch_offset;

		//update HT info also
		HT_info_handler(padapter, pIE);
	}
	else
	{
		pmlmeinfo->bwmode_updated = _FALSE;
	}
		

	if(_TRUE == pmlmeinfo->bwmode_updated)
	{
		struct sta_info *psta;
		WLAN_BSSID_EX 	*cur_network = &(pmlmeinfo->network);
		struct sta_priv	*pstapriv = &padapter->stapriv;
	
		//set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

		
		//update ap's stainfo
		psta = rtw_get_stainfo(pstapriv, cur_network->MacAddress);
		if(psta)
		{
			struct ht_priv	*phtpriv_sta = &psta->htpriv;
			
			if(phtpriv_sta->ht_option)
			{				
				// bwmode				
				psta->bw_mode = pmlmeext->cur_bwmode;
				phtpriv_sta->ch_offset = pmlmeext->cur_ch_offset;		
			}
			else
			{
				psta->bw_mode = CHANNEL_WIDTH_20;
				phtpriv_sta->ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			}

			rtw_dm_ra_mask_wk_cmd(padapter, (u8 *)psta);
		}

		//pmlmeinfo->bwmode_updated = _FALSE;//bwmode_updated done, reset it!
	}	
#endif //CONFIG_80211N_HT
}

void HT_caps_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
#ifdef CONFIG_80211N_HT
	unsigned int	i;
	u8	rf_type = RF_1T1R;
	u8	max_AMPDU_len, min_MPDU_spacing;
	u8	cur_ldpc_cap=0, cur_stbc_cap=0, cur_beamform_cap=0;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv 		*pmlmepriv = &padapter->mlmepriv;	
	struct ht_priv			*phtpriv = &pmlmepriv->htpriv;
	struct registry_priv 	*pregistrypriv = &padapter->registrypriv;
	
	if(pIE==NULL) return;
	
	if(phtpriv->ht_option == _FALSE)	return;

	pmlmeinfo->HT_caps_enable = 1;
	
	for (i = 0; i < (pIE->Length); i++)
	{
		if (i != 2)
		{
			//	Commented by Albert 2010/07/12
			//	Got the endian issue here.
			pmlmeinfo->HT_caps.u.HT_cap[i] &= (pIE->data[i]);
		}
		else
		{
			/* AMPDU Parameters field */

			/* Get MIN of MAX AMPDU Length Exp */
			if ((pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x3) > (pIE->data[i] & 0x3))
			{
				max_AMPDU_len = (pIE->data[i] & 0x3);
			}
			else
			{
				max_AMPDU_len = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x3);
			}

			/* Get MAX of MIN MPDU Start Spacing */
			if ((pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x1c) > (pIE->data[i] & 0x1c))
			{
				min_MPDU_spacing = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x1c);
			}
			else
			{
				min_MPDU_spacing = (pIE->data[i] & 0x1c);
			}

			pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para = max_AMPDU_len | min_MPDU_spacing;
		}
	}

	//	Commented by Albert 2010/07/12
	//	Have to handle the endian issue after copying.
	//	HT_ext_caps didn't be used yet.	
	pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info = le16_to_cpu( pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info );
	pmlmeinfo->HT_caps.u.HT_cap_element.HT_ext_caps = le16_to_cpu( pmlmeinfo->HT_caps.u.HT_cap_element.HT_ext_caps );

	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));


	//update the MCS set
	for (i = 0; i < 16; i++)
		pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate[i] &= pmlmeext->default_supported_mcs_set[i];
			
	//update the MCS rates
	switch(rf_type)
	{
		case RF_1T1R:
		case RF_1T2R:
			set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_1R);							
			break;
		case RF_2T2R:
			#ifdef CONFIG_DISABLE_MCS13TO15
			if(pmlmeext->cur_bwmode == CHANNEL_WIDTH_40 && pregistrypriv->wifi_spec != 1 )				
				set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_2R_13TO15_OFF);				
			else
				set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_2R);
#else //CONFIG_DISABLE_MCS13TO15
			set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_2R);
#endif //CONFIG_DISABLE_MCS13TO15
			break;
		case RF_3T3R:
			set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_3R);
			break;
		default:
			DBG_871X("[warning] rf_type %d is not expected\n", rf_type);
	}

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		// Config STBC setting
		if (TEST_FLAG(phtpriv->stbc_cap, STBC_HT_ENABLE_TX) && GET_HT_CAP_ELE_TX_STBC(pIE->data))
		{
			SET_FLAG(cur_stbc_cap, STBC_HT_ENABLE_TX);
			DBG_871X("Enable HT Tx STBC !\n");
		}
		phtpriv->stbc_cap = cur_stbc_cap;

#ifdef CONFIG_BEAMFORMING
		// Config Tx beamforming setting
		if (TEST_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE) && 
			GET_HT_CAP_TXBF_EXPLICIT_COMP_STEERING_CAP(pIE->data))
		{
			SET_FLAG(cur_beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE);
			/* Shift to BEAMFORMING_HT_BEAMFORMEE_CHNL_EST_CAP*/
			SET_FLAG(cur_beamform_cap, GET_HT_CAP_TXBF_CHNL_ESTIMATION_NUM_ANTENNAS(pIE->data) << 6);
		}

		if (TEST_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE) &&
			GET_HT_CAP_TXBF_EXPLICIT_COMP_FEEDBACK_CAP(pIE->data))
		{
			SET_FLAG(cur_beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE);
			/* Shift to BEAMFORMING_HT_BEAMFORMER_STEER_NUM*/
			SET_FLAG(cur_beamform_cap, GET_HT_CAP_TXBF_COMP_STEERING_NUM_ANTENNAS(pIE->data) << 4);
		}
		phtpriv->beamform_cap = cur_beamform_cap;
		if (cur_beamform_cap) {
			DBG_871X("AP HT Beamforming Cap = 0x%02X\n", cur_beamform_cap);
		}
#endif /*CONFIG_BEAMFORMING*/
	} else {
		/*WIFI_STATION_STATEorI_ADHOC_STATE or WIFI_ADHOC_MASTER_STATE*/
		// Config LDPC Coding Capability
		if (TEST_FLAG(phtpriv->ldpc_cap, LDPC_HT_ENABLE_TX) && GET_HT_CAP_ELE_LDPC_CAP(pIE->data))
		{
			SET_FLAG(cur_ldpc_cap, (LDPC_HT_ENABLE_TX | LDPC_HT_CAP_TX));
			DBG_871X("Enable HT Tx LDPC!\n");
		}
		phtpriv->ldpc_cap = cur_ldpc_cap;

		// Config STBC setting
		if (TEST_FLAG(phtpriv->stbc_cap, STBC_HT_ENABLE_TX) && GET_HT_CAP_ELE_RX_STBC(pIE->data))
		{
			SET_FLAG(cur_stbc_cap, (STBC_HT_ENABLE_TX | STBC_HT_CAP_TX) );
			DBG_871X("Enable HT Tx STBC!\n");
		}
		phtpriv->stbc_cap = cur_stbc_cap;

#ifdef CONFIG_BEAMFORMING
		// Config Tx beamforming setting
		if (TEST_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE) && 
			GET_HT_CAP_TXBF_EXPLICIT_COMP_STEERING_CAP(pIE->data))
		{
			SET_FLAG(cur_beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE);
			/* Shift to BEAMFORMING_HT_BEAMFORMEE_CHNL_EST_CAP*/
			SET_FLAG(cur_beamform_cap, GET_HT_CAP_TXBF_CHNL_ESTIMATION_NUM_ANTENNAS(pIE->data) << 6);
		}

		if (TEST_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE) &&
			GET_HT_CAP_TXBF_EXPLICIT_COMP_FEEDBACK_CAP(pIE->data))
		{
			SET_FLAG(cur_beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE);
			/* Shift to BEAMFORMING_HT_BEAMFORMER_STEER_NUM*/
			SET_FLAG(cur_beamform_cap, GET_HT_CAP_TXBF_COMP_STEERING_NUM_ANTENNAS(pIE->data) << 4);
		}
		phtpriv->beamform_cap = cur_beamform_cap;
		if (cur_beamform_cap) {
			DBG_871X("Client HT Beamforming Cap = 0x%02X\n", cur_beamform_cap);
		}
#endif /*CONFIG_BEAMFORMING*/
	}

#endif //CONFIG_80211N_HT
}

void HT_info_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
#ifdef CONFIG_80211N_HT
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv 		*pmlmepriv = &padapter->mlmepriv;	
	struct ht_priv			*phtpriv = &pmlmepriv->htpriv;

	if(pIE==NULL) return;

	if(phtpriv->ht_option == _FALSE)	return;


	if(pIE->Length > sizeof(struct HT_info_element))
		return;
	
	pmlmeinfo->HT_info_enable = 1;
	_rtw_memcpy(&(pmlmeinfo->HT_info), pIE->data, pIE->Length);
#endif //CONFIG_80211N_HT
	return;
}

void HTOnAssocRsp(_adapter *padapter)
{
	unsigned char		max_AMPDU_len;
	unsigned char		min_MPDU_spacing;
	//struct registry_priv	 *pregpriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
	DBG_871X("%s\n", __FUNCTION__);

	if ((pmlmeinfo->HT_info_enable) && (pmlmeinfo->HT_caps_enable))
	{
		pmlmeinfo->HT_enable = 1;
	}
	else
	{
		pmlmeinfo->HT_enable = 0;
		//set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);
		return;
	}
	
	//handle A-MPDU parameter field
	/* 	
		AMPDU_para [1:0]:Max AMPDU Len => 0:8k , 1:16k, 2:32k, 3:64k
		AMPDU_para [4:2]:Min MPDU Start Spacing	
	*/
	max_AMPDU_len = pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x03;	
	
	min_MPDU_spacing = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x1c) >> 2;	

	rtw_hal_set_hwreg(padapter, HW_VAR_AMPDU_MIN_SPACE, (u8 *)(&min_MPDU_spacing));

	rtw_hal_set_hwreg(padapter, HW_VAR_AMPDU_FACTOR, (u8 *)(&max_AMPDU_len));

#if 0 //move to rtw_update_ht_cap()
	if ((pregpriv->bw_mode > 0) &&
		(pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info & BIT(1)) && 
		(pmlmeinfo->HT_info.infos[0] & BIT(2)))
	{
		//switch to the 40M Hz mode accoring to the AP
		pmlmeext->cur_bwmode = CHANNEL_WIDTH_40;
		switch ((pmlmeinfo->HT_info.infos[0] & 0x3))
		{
			case EXTCHNL_OFFSET_UPPER:
				pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
				break;
			
			case EXTCHNL_OFFSET_LOWER:
				pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
				break;
				
			default:
				pmlmeext->cur_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
				break;
		}
	}
#endif

	//set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

#if 0 //move to rtw_update_ht_cap()
	//
	// Config SM Power Save setting
	//
	pmlmeinfo->SM_PS = (pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info & 0x0C) >> 2;
	if(pmlmeinfo->SM_PS == WLAN_HT_CAP_SM_PS_STATIC)
	{
		/*u8 i;
		//update the MCS rates
		for (i = 0; i < 16; i++)
		{
			pmlmeinfo->HT_caps.HT_cap_element.MCS_rate[i] &= MCS_rate_1R[i];
		}*/
		DBG_871X("%s(): WLAN_HT_CAP_SM_PS_STATIC\n",__FUNCTION__);
	}

	//
	// Config current HT Protection mode.
	//
	pmlmeinfo->HT_protection = pmlmeinfo->HT_info.infos[1] & 0x3;
#endif
	
}

void ERP_IE_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if(pIE->Length>1)
		return;
	
	pmlmeinfo->ERP_enable = 1;
	_rtw_memcpy(&(pmlmeinfo->ERP_IE), pIE->data, pIE->Length);
}

void VCS_update(_adapter *padapter, struct sta_info *psta)
{
	struct registry_priv	 *pregpriv = &padapter->registrypriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	switch (pregpriv->vrtl_carrier_sense)/* 0:off 1:on 2:auto */
	{
		case 0: //off
			psta->rtsen = 0;
			psta->cts2self = 0;
			break;
			
		case 1: //on
			if (pregpriv->vcs_type == 1) /* 1:RTS/CTS 2:CTS to self */
			{
				psta->rtsen = 1;
				psta->cts2self = 0;
			}
			else
			{
				psta->rtsen = 0;
				psta->cts2self = 1;
			}
			break;
			
		case 2: //auto
		default:
			if (((pmlmeinfo->ERP_enable) && (pmlmeinfo->ERP_IE & BIT(1)))
				/*||(pmlmepriv->ht_op_mode & HT_INFO_OPERATION_MODE_NON_GF_DEVS_PRESENT)*/
				) {
				if (pregpriv->vcs_type == 1) {
					psta->rtsen = 1;
					psta->cts2self = 0;
				} else {
					psta->rtsen = 0;
					psta->cts2self = 1;
				}
			} else {
				psta->rtsen = 0;
				psta->cts2self = 0;
			}	
			break;
	}
}

void	update_ldpc_stbc_cap(struct sta_info *psta)
{
#ifdef CONFIG_80211N_HT

#ifdef CONFIG_80211AC_VHT
	if (psta->vhtpriv.vht_option) {
		if(TEST_FLAG(psta->vhtpriv.ldpc_cap, LDPC_VHT_ENABLE_TX))
			psta->ldpc = 1;

		if(TEST_FLAG(psta->vhtpriv.stbc_cap, STBC_VHT_ENABLE_TX))
			psta->stbc = 1;
	}
	else
#endif //CONFIG_80211AC_VHT
	if (psta->htpriv.ht_option) {
		if(TEST_FLAG(psta->htpriv.ldpc_cap, LDPC_HT_ENABLE_TX))
			psta->ldpc = 1;

		if(TEST_FLAG(psta->htpriv.stbc_cap, STBC_HT_ENABLE_TX))
			psta->stbc = 1;
	} else {
		psta->ldpc = 0;
		psta->stbc = 0;
	}

#endif //CONFIG_80211N_HT
}


/*
 * rtw_get_bcn_keys: get beacon keys from recv frame
 *
 * TODO:
 *	WLAN_EID_COUNTRY
 *	WLAN_EID_ERP_INFO
 *	WLAN_EID_CHANNEL_SWITCH
 *	WLAN_EID_PWR_CONSTRAINT
 */
int rtw_get_bcn_keys(ADAPTER *Adapter, u8 *pframe, u32 packet_len,
		struct beacon_keys *recv_beacon)
{
	int left;
	u16 capability;
	unsigned char *pos;
	struct rtw_ieee802_11_elems elems;
	struct rtw_ieee80211_ht_cap *pht_cap = NULL;
	struct HT_info_element *pht_info = NULL;

	_rtw_memset(recv_beacon, 0, sizeof(*recv_beacon));

	/* checking capabilities */
	capability = le16_to_cpu(*(unsigned short *)(pframe + WLAN_HDR_A3_LEN + 10));

	/* checking IEs */
	left = packet_len - sizeof(struct rtw_ieee80211_hdr_3addr) - _BEACON_IE_OFFSET_;
	pos = pframe + sizeof(struct rtw_ieee80211_hdr_3addr) + _BEACON_IE_OFFSET_;
	if (rtw_ieee802_11_parse_elems(pos, left, &elems, 1) == ParseFailed)
		return _FALSE;

	/* check bw and channel offset */
	if (elems.ht_capabilities) {
		if (elems.ht_capabilities_len != sizeof(*pht_cap))
			return _FALSE;

		pht_cap = (struct rtw_ieee80211_ht_cap *) elems.ht_capabilities;
		recv_beacon->ht_cap_info = pht_cap->cap_info;
	}

	if (elems.ht_operation) {
		if (elems.ht_operation_len != sizeof(*pht_info))
			return _FALSE;

		pht_info = (struct HT_info_element *) elems.ht_operation;
		recv_beacon->ht_info_infos_0_sco = pht_info->infos[0] & 0x03;
	}

	/* Checking for channel */
	if (elems.ds_params && elems.ds_params_len == sizeof(recv_beacon->bcn_channel))
		_rtw_memcpy(&recv_beacon->bcn_channel, elems.ds_params,
				sizeof(recv_beacon->bcn_channel));
	else if (pht_info)
		/* In 5G, some ap do not have DSSET IE checking HT info for channel */
		recv_beacon->bcn_channel = pht_info->primary_channel;
	else {
		/* we don't find channel IE, so don't check it */
		//DBG_871X("Oops: %s we don't find channel IE, so don't check it \n", __func__);
		recv_beacon->bcn_channel = Adapter->mlmeextpriv.cur_channel;
	}

	/* checking SSID */
	if (elems.ssid) {
	       if (elems.ssid_len > sizeof(recv_beacon->ssid))
			return _FALSE;

	       _rtw_memcpy(recv_beacon->ssid, elems.ssid, elems.ssid_len);
	       recv_beacon->ssid_len = elems.ssid_len;
	} else; // means hidden ssid

	/* checking RSN first */
	if (elems.rsn_ie && elems.rsn_ie_len) {
		recv_beacon->encryp_protocol = ENCRYP_PROTOCOL_WPA2;
		rtw_parse_wpa2_ie(elems.rsn_ie - 2, elems.rsn_ie_len + 2,
			&recv_beacon->group_cipher, &recv_beacon->pairwise_cipher,
			&recv_beacon->is_8021x);
	}
	/* checking WPA secon */
	else if (elems.wpa_ie && elems.wpa_ie_len) {
		recv_beacon->encryp_protocol = ENCRYP_PROTOCOL_WPA;
		rtw_parse_wpa_ie(elems.wpa_ie - 2, elems.wpa_ie_len + 2,
			&recv_beacon->group_cipher, &recv_beacon->pairwise_cipher,
			&recv_beacon->is_8021x);
	}
	else if (capability & BIT(4)) {
		recv_beacon->encryp_protocol = ENCRYP_PROTOCOL_WEP;
	}

	return _TRUE;
}

void rtw_dump_bcn_keys(struct beacon_keys *recv_beacon)
{
	int i;
	char *p;
	u8 ssid[IW_ESSID_MAX_SIZE + 1];

	_rtw_memcpy(ssid, recv_beacon->ssid, recv_beacon->ssid_len);
	ssid[recv_beacon->ssid_len] = '\0';

	DBG_871X("%s: ssid = %s\n", __func__, ssid);
	DBG_871X("%s: channel = %x\n", __func__, recv_beacon->bcn_channel);
	DBG_871X("%s: ht_cap = %x\n", __func__,	recv_beacon->ht_cap_info);
	DBG_871X("%s: ht_info_infos_0_sco = %x\n", __func__, recv_beacon->ht_info_infos_0_sco);
	DBG_871X("%s: sec=%d, group = %x, pair = %x, 8021X = %x\n", __func__,
		recv_beacon->encryp_protocol, recv_beacon->group_cipher,
		recv_beacon->pairwise_cipher, recv_beacon->is_8021x);
}

int rtw_check_bcn_info(ADAPTER *Adapter, u8 *pframe, u32 packet_len)
{
#if 0
	unsigned int		len;
	unsigned char		*p;
	unsigned short	val16, subtype;
	struct wlan_network *cur_network = &(Adapter->mlmepriv.cur_network);
	//u8 wpa_ie[255],rsn_ie[255];
	u16 wpa_len=0,rsn_len=0;
	u8 encryp_protocol = 0;
	WLAN_BSSID_EX *bssid;
	int group_cipher = 0, pairwise_cipher = 0, is_8021x = 0;
	unsigned char *pbuf;
	u32 wpa_ielen = 0;
	u8 *pbssid = GetAddr3Ptr(pframe);
	u32 hidden_ssid = 0;
	u8 cur_network_type, network_type=0;
	struct HT_info_element *pht_info = NULL;
	struct rtw_ieee80211_ht_cap *pht_cap = NULL;
	u32 bcn_channel;
	unsigned short 	ht_cap_info;
	unsigned char	ht_info_infos_0;
#endif
	unsigned int len;
	u8 *pbssid = GetAddr3Ptr(pframe);
	struct mlme_priv *pmlmepriv = &Adapter->mlmepriv;
	struct wlan_network *cur_network = &(Adapter->mlmepriv.cur_network);
	struct beacon_keys recv_beacon;

	if (is_client_associated_to_ap(Adapter) == _FALSE)
		return _TRUE;

	len = packet_len - sizeof(struct rtw_ieee80211_hdr_3addr);

	if (len > MAX_IE_SZ) {
		DBG_871X("%s IE too long for survey event\n", __func__);
		return _FAIL;
	}

	if (_rtw_memcmp(cur_network->network.MacAddress, pbssid, 6) == _FALSE) {
		DBG_871X("Oops: rtw_check_network_encrypt linked but recv other bssid bcn\n" MAC_FMT MAC_FMT,
				MAC_ARG(pbssid), MAC_ARG(cur_network->network.MacAddress));
		return _TRUE;
	}

	if (rtw_get_bcn_keys(Adapter, pframe, packet_len, &recv_beacon) == _FALSE)
		return _TRUE; // parsing failed => broken IE

	// don't care hidden ssid, use current beacon ssid directly
	if (recv_beacon.ssid_len == 0) {
	       _rtw_memcpy(recv_beacon.ssid, pmlmepriv->cur_beacon_keys.ssid,
			       pmlmepriv->cur_beacon_keys.ssid_len);
	       recv_beacon.ssid_len = pmlmepriv->cur_beacon_keys.ssid_len;
	}

	if (_rtw_memcmp(&recv_beacon, &pmlmepriv->cur_beacon_keys, sizeof(recv_beacon)) == _TRUE)
	{
		pmlmepriv->new_beacon_cnts = 0;
	}
	else if ((pmlmepriv->new_beacon_cnts == 0) ||
		_rtw_memcmp(&recv_beacon, &pmlmepriv->new_beacon_keys, sizeof(recv_beacon)) == _FALSE)
	{
		DBG_871X_LEVEL(_drv_err_, "%s: start new beacon (seq=%d)\n", __func__, GetSequence(pframe));

		if (pmlmepriv->new_beacon_cnts == 0) {
			DBG_871X_LEVEL(_drv_err_, "%s: cur beacon key\n", __func__);
			DBG_871X_EXP(_drv_err_, rtw_dump_bcn_keys(&pmlmepriv->cur_beacon_keys));
		}

		DBG_871X_LEVEL(_drv_err_, "%s: new beacon key\n", __func__);
		DBG_871X_EXP(_drv_err_, rtw_dump_bcn_keys(&recv_beacon));

		memcpy(&pmlmepriv->new_beacon_keys, &recv_beacon, sizeof(recv_beacon));
		pmlmepriv->new_beacon_cnts = 1;
	}
	else
	{
		DBG_871X_LEVEL(_drv_err_, "%s: new beacon again (seq=%d)\n", __func__, GetSequence(pframe));
		pmlmepriv->new_beacon_cnts++;
	}

	// if counter >= max, it means beacon is changed really
	if (pmlmepriv->new_beacon_cnts >= new_bcn_max)
	{
		DBG_871X_LEVEL(_drv_err_, "%s: new beacon occur!!\n", __func__);

		// check bw mode change only?
		pmlmepriv->cur_beacon_keys.ht_cap_info = recv_beacon.ht_cap_info;
		pmlmepriv->cur_beacon_keys.ht_info_infos_0_sco = recv_beacon.ht_info_infos_0_sco;

		if (_rtw_memcmp(&recv_beacon, &pmlmepriv->cur_beacon_keys,
					sizeof(recv_beacon)) == _FALSE) {
			// beacon is changed, have to do disconnect/connect
			return _FAIL;
		}

		DBG_871X("%s bw mode change\n", __func__);
		DBG_871X("%s bcn now: ht_cap_info:%x ht_info_infos_0:%x\n", __func__,
				cur_network->BcnInfo.ht_cap_info,
				cur_network->BcnInfo.ht_info_infos_0);

		cur_network->BcnInfo.ht_cap_info = recv_beacon.ht_cap_info;
		cur_network->BcnInfo.ht_info_infos_0 =
			(cur_network->BcnInfo.ht_info_infos_0 & (~0x03)) |
			recv_beacon.ht_info_infos_0_sco;

		DBG_871X("%s bcn link: ht_cap_info:%x ht_info_infos_0:%x\n", __func__,
				cur_network->BcnInfo.ht_cap_info,
				cur_network->BcnInfo.ht_info_infos_0);

		memcpy(&pmlmepriv->cur_beacon_keys, &recv_beacon, sizeof(recv_beacon));
		pmlmepriv->new_beacon_cnts = 0;
	}

	return _SUCCESS;

#if 0
	bssid = (WLAN_BSSID_EX *)rtw_zmalloc(sizeof(WLAN_BSSID_EX));
	if (bssid == NULL) {
		DBG_871X("%s rtw_zmalloc fail !!!\n", __func__);
		return _TRUE;
	}

	if ((pmlmepriv->timeBcnInfoChkStart != 0) && (rtw_get_passing_time_ms(pmlmepriv->timeBcnInfoChkStart) > DISCONNECT_BY_CHK_BCN_FAIL_OBSERV_PERIOD_IN_MS))
	{
		pmlmepriv->timeBcnInfoChkStart = 0;
		pmlmepriv->NumOfBcnInfoChkFail = 0;
	}

	subtype = GetFrameSubType(pframe) >> 4;

	if(subtype==WIFI_BEACON)
		bssid->Reserved[0] = 1;

	bssid->Length = sizeof(WLAN_BSSID_EX) - MAX_IE_SZ + len;

	/* below is to copy the information element */
	bssid->IELength = len;
	_rtw_memcpy(bssid->IEs, (pframe + sizeof(struct rtw_ieee80211_hdr_3addr)), bssid->IELength);

	/* check bw and channel offset */
	/* parsing HT_CAP_IE */
	p = rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _HT_CAPABILITY_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_);
	if(p && len>0) {
			pht_cap = (struct rtw_ieee80211_ht_cap *)(p + 2);
			ht_cap_info = pht_cap->cap_info;
	} else {
			ht_cap_info = 0;
	}
	/* parsing HT_INFO_IE */
	p = rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _HT_ADD_INFO_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_);
	if(p && len>0) {
			pht_info = (struct HT_info_element *)(p + 2);
			ht_info_infos_0 = pht_info->infos[0];
	} else {
			ht_info_infos_0 = 0;
	}
	if (ht_cap_info != cur_network->BcnInfo.ht_cap_info ||
		((ht_info_infos_0&0x03) != (cur_network->BcnInfo.ht_info_infos_0&0x03))) {
			DBG_871X("%s bcn now: ht_cap_info:%x ht_info_infos_0:%x\n", __func__,
						   	ht_cap_info, ht_info_infos_0);
			DBG_871X("%s bcn link: ht_cap_info:%x ht_info_infos_0:%x\n", __func__,
						   	cur_network->BcnInfo.ht_cap_info, cur_network->BcnInfo.ht_info_infos_0);
			DBG_871X("%s bw mode change\n", __func__);
			{	
				//bcn_info_update
				cur_network->BcnInfo.ht_cap_info = ht_cap_info;
				cur_network->BcnInfo.ht_info_infos_0 = ht_info_infos_0;
				//to do : need to check that whether modify related register of BB or not 
			}
			//goto _mismatch;
	}

	/* Checking for channel */
	p = rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _DSSET_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_);
	if (p) {
			bcn_channel = *(p + 2);
	} else {/* In 5G, some ap do not have DSSET IE checking HT info for channel */
			rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _HT_ADD_INFO_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_);
			if(pht_info) {
					bcn_channel = pht_info->primary_channel;
			} else { /* we don't find channel IE, so don't check it */
					//DBG_871X("Oops: %s we don't find channel IE, so don't check it \n", __func__);
					bcn_channel = Adapter->mlmeextpriv.cur_channel;
			}
	}
	if (bcn_channel != Adapter->mlmeextpriv.cur_channel) {
			DBG_871X("%s beacon channel:%d cur channel:%d disconnect\n", __func__,
						   bcn_channel, Adapter->mlmeextpriv.cur_channel);
			goto _mismatch;
	}

	/* checking SSID */
	if ((p = rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _SSID_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_)) == NULL) {
		DBG_871X("%s marc: cannot find SSID for survey event\n", __func__);
		hidden_ssid = _TRUE;
	} else {
		hidden_ssid = _FALSE;
	}

	if((NULL != p) && (_FALSE == hidden_ssid && (*(p + 1)))) {
		_rtw_memcpy(bssid->Ssid.Ssid, (p + 2), *(p + 1));
		bssid->Ssid.SsidLength = *(p + 1);
	} else {
		bssid->Ssid.SsidLength = 0;
		bssid->Ssid.Ssid[0] = '\0';
	}

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,("%s bssid.Ssid.Ssid:%s bssid.Ssid.SsidLength:%d "
				"cur_network->network.Ssid.Ssid:%s len:%d\n", __func__, bssid->Ssid.Ssid,
				bssid->Ssid.SsidLength, cur_network->network.Ssid.Ssid,
				cur_network->network.Ssid.SsidLength));

	if (_rtw_memcmp(bssid->Ssid.Ssid, cur_network->network.Ssid.Ssid, 32) == _FALSE ||
			bssid->Ssid.SsidLength != cur_network->network.Ssid.SsidLength) {
		if (bssid->Ssid.Ssid[0] != '\0' && bssid->Ssid.SsidLength != 0) { /* not hidden ssid */
			DBG_871X("%s(), SSID is not match\n", __func__);
			goto _mismatch;
		}
	}

	/* check encryption info */
	val16 = rtw_get_capability((WLAN_BSSID_EX *)bssid);

	if (val16 & BIT(4))
		bssid->Privacy = 1;
	else
		bssid->Privacy = 0;

	RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,
			("%s(): cur_network->network.Privacy is %d, bssid.Privacy is %d\n",
			 __func__, cur_network->network.Privacy,bssid->Privacy));
	if (cur_network->network.Privacy != bssid->Privacy) {
		DBG_871X("%s(), privacy is not match\n",__func__);
		goto _mismatch;
	}

	rtw_get_sec_ie(bssid->IEs, bssid->IELength, NULL,&rsn_len,NULL,&wpa_len);

	if (rsn_len > 0) {
		encryp_protocol = ENCRYP_PROTOCOL_WPA2;
	} else if (wpa_len > 0) {
		encryp_protocol = ENCRYP_PROTOCOL_WPA;
	} else {
		if (bssid->Privacy)
			encryp_protocol = ENCRYP_PROTOCOL_WEP;
	}

	if (cur_network->BcnInfo.encryp_protocol != encryp_protocol) {
		DBG_871X("%s(): enctyp is not match\n",__func__);
		goto _mismatch;
	}

	if (encryp_protocol == ENCRYP_PROTOCOL_WPA || encryp_protocol == ENCRYP_PROTOCOL_WPA2) {
		pbuf = rtw_get_wpa_ie(&bssid->IEs[12], &wpa_ielen, bssid->IELength-12);
		if(pbuf && (wpa_ielen>0)) {
			if (_SUCCESS == rtw_parse_wpa_ie(pbuf, wpa_ielen+2, &group_cipher, &pairwise_cipher, &is_8021x)) {
				RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,
						("%s pnetwork->pairwise_cipher: %d, group_cipher is %d, is_8021x is %d\n", __func__,
						 pairwise_cipher, group_cipher, is_8021x));
			}
		} else {
			pbuf = rtw_get_wpa2_ie(&bssid->IEs[12], &wpa_ielen, bssid->IELength-12);

			if(pbuf && (wpa_ielen>0)) {
				if (_SUCCESS == rtw_parse_wpa2_ie(pbuf, wpa_ielen+2, &group_cipher, &pairwise_cipher, &is_8021x)) {
					RT_TRACE(_module_rtl871x_mlme_c_,_drv_info_,
							("%s pnetwork->pairwise_cipher: %d, pnetwork->group_cipher is %d, is_802x is %d\n",
							 __func__, pairwise_cipher, group_cipher, is_8021x));
				}
			}
		}

		RT_TRACE(_module_rtl871x_mlme_c_,_drv_err_,
				("%s cur_network->group_cipher is %d: %d\n",__func__, cur_network->BcnInfo.group_cipher, group_cipher));
		if (pairwise_cipher != cur_network->BcnInfo.pairwise_cipher || group_cipher != cur_network->BcnInfo.group_cipher) {
			DBG_871X("%s pairwise_cipher(%x:%x) or group_cipher(%x:%x) is not match\n",__func__,
					pairwise_cipher, cur_network->BcnInfo.pairwise_cipher,
					group_cipher, cur_network->BcnInfo.group_cipher);
			goto _mismatch;
		}

		if (is_8021x != cur_network->BcnInfo.is_8021x) {
			DBG_871X("%s authentication is not match\n", __func__);
			goto _mismatch;
		}
	}

	rtw_mfree((u8 *)bssid, sizeof(WLAN_BSSID_EX));
	return _SUCCESS;

_mismatch:
	rtw_mfree((u8 *)bssid, sizeof(WLAN_BSSID_EX));

	if (pmlmepriv->NumOfBcnInfoChkFail == 0)
	{
		pmlmepriv->timeBcnInfoChkStart = rtw_get_current_time();
	}

	pmlmepriv->NumOfBcnInfoChkFail++;
	DBG_871X("%s by "ADPT_FMT" - NumOfChkFail = %d (SeqNum of this Beacon frame = %d).\n", __func__, ADPT_ARG(Adapter), pmlmepriv->NumOfBcnInfoChkFail, GetSequence(pframe));

	if ((pmlmepriv->timeBcnInfoChkStart != 0) && (rtw_get_passing_time_ms(pmlmepriv->timeBcnInfoChkStart) <= DISCONNECT_BY_CHK_BCN_FAIL_OBSERV_PERIOD_IN_MS) 
		&& (pmlmepriv->NumOfBcnInfoChkFail >= DISCONNECT_BY_CHK_BCN_FAIL_THRESHOLD))
	{
		DBG_871X("%s by "ADPT_FMT" - NumOfChkFail = %d >= threshold : %d (in %d ms), return FAIL.\n", __func__, ADPT_ARG(Adapter), pmlmepriv->NumOfBcnInfoChkFail, 
			DISCONNECT_BY_CHK_BCN_FAIL_THRESHOLD, rtw_get_passing_time_ms(pmlmepriv->timeBcnInfoChkStart));
		pmlmepriv->timeBcnInfoChkStart = 0;
		pmlmepriv->NumOfBcnInfoChkFail = 0;
		return _FAIL;
	}

	return _SUCCESS;
#endif
}

void update_beacon_info(_adapter *padapter, u8 *pframe, uint pkt_len, struct sta_info *psta)
{
	unsigned int i;
	unsigned int len;
	PNDIS_802_11_VARIABLE_IEs	pIE;
		
#ifdef CONFIG_TDLS
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	u8 tdls_prohibited[] = { 0x00, 0x00, 0x00, 0x00, 0x10 }; //bit(38): TDLS_prohibited
#endif //CONFIG_TDLS
		
	len = pkt_len - (_BEACON_IE_OFFSET_ + WLAN_HDR_A3_LEN);

	for (i = 0; i < len;)
	{
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pframe + (_BEACON_IE_OFFSET_ + WLAN_HDR_A3_LEN) + i);
		
		switch (pIE->ElementID)
		{
			case _VENDOR_SPECIFIC_IE_:		
				//to update WMM paramter set while receiving beacon
				if (_rtw_memcmp(pIE->data, WMM_PARA_OUI, 6) && pIE->Length == WLAN_WMM_LEN)	//WMM
				{
					(WMM_param_handler(padapter, pIE))? report_wmm_edca_update(padapter): 0;
				}

				break;

			case _HT_EXTRA_INFO_IE_:	//HT info				
				//HT_info_handler(padapter, pIE);
				bwmode_update_check(padapter, pIE);
				break;
#ifdef CONFIG_80211AC_VHT
			case EID_OpModeNotification:
				rtw_process_vht_op_mode_notify(padapter, pIE->data, psta);
				break;
#endif //CONFIG_80211AC_VHT
			case _ERPINFO_IE_:
				ERP_IE_handler(padapter, pIE);
				VCS_update(padapter, psta);
				break;

#ifdef CONFIG_TDLS
			case _EXT_CAP_IE_:
				if( check_ap_tdls_prohibited(pIE->data, pIE->Length) == _TRUE )
					ptdlsinfo->ap_prohibited = _TRUE;
				if (check_ap_tdls_ch_switching_prohibited(pIE->data, pIE->Length) == _TRUE)
					ptdlsinfo->ch_switch_prohibited = _TRUE;
				break;
#endif //CONFIG_TDLS
			default:
				break;
		}
		
		i += (pIE->Length + 2);
	}
}

#ifdef CONFIG_DFS
void process_csa_ie(_adapter *padapter, u8 *pframe, uint pkt_len)
{
	unsigned int i;
	unsigned int len;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 new_ch_no = 0; 

	if(padapter->mlmepriv.handle_dfs == _TRUE )
		return;
	
	len = pkt_len - (_BEACON_IE_OFFSET_ + WLAN_HDR_A3_LEN);

	for (i = 0; i < len;)
	{
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pframe + (_BEACON_IE_OFFSET_ + WLAN_HDR_A3_LEN) + i);
		
		switch (pIE->ElementID)
		{
			case _CH_SWTICH_ANNOUNCE_:
				padapter->mlmepriv.handle_dfs = _TRUE;
				_rtw_memcpy(&new_ch_no, pIE->data+1, 1);
				rtw_set_csa_cmd(padapter, new_ch_no);
				break;
			default:
				break;
		}
		
		i += (pIE->Length + 2);
	}
}
#endif //CONFIG_DFS

unsigned int is_ap_in_tkip(_adapter *padapter)
{
	u32 i;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);

	if (rtw_get_capability((WLAN_BSSID_EX *)cur_network) & WLAN_CAPABILITY_PRIVACY)
	{
		for (i = sizeof(NDIS_802_11_FIXED_IEs); i < pmlmeinfo->network.IELength;)
		{
			pIE = (PNDIS_802_11_VARIABLE_IEs)(pmlmeinfo->network.IEs + i);
		
			switch (pIE->ElementID)
			{
				case _VENDOR_SPECIFIC_IE_:
					if ((_rtw_memcmp(pIE->data, RTW_WPA_OUI, 4)) && (_rtw_memcmp((pIE->data + 12), WPA_TKIP_CIPHER, 4))) 
					{
						return _TRUE;
					}
					break;
				
				case _RSN_IE_2_:
					if (_rtw_memcmp((pIE->data + 8), RSN_TKIP_CIPHER, 4)) 
					{
						return _TRUE;
					}
					
				default:
					break;
			}
		
			i += (pIE->Length + 2);
		}
		
		return _FALSE;
	}
	else
	{
		return _FALSE;
	}
	
}

unsigned int should_forbid_n_rate(_adapter * padapter)
{
	u32 i;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	WLAN_BSSID_EX  *cur_network = &pmlmepriv->cur_network.network;

	if (rtw_get_capability((WLAN_BSSID_EX *)cur_network) & WLAN_CAPABILITY_PRIVACY)
	{
		for (i = sizeof(NDIS_802_11_FIXED_IEs); i < cur_network->IELength;)
		{
			pIE = (PNDIS_802_11_VARIABLE_IEs)(cur_network->IEs + i);

			switch (pIE->ElementID)
			{
				case _VENDOR_SPECIFIC_IE_:
					if (_rtw_memcmp(pIE->data, RTW_WPA_OUI, 4) &&
						((_rtw_memcmp((pIE->data + 12), WPA_CIPHER_SUITE_CCMP, 4)) ||
						  (_rtw_memcmp((pIE->data + 16), WPA_CIPHER_SUITE_CCMP, 4))))
						return _FALSE;
					break;

				case _RSN_IE_2_:
					if  ((_rtw_memcmp((pIE->data + 8), RSN_CIPHER_SUITE_CCMP, 4))  ||
					       (_rtw_memcmp((pIE->data + 12), RSN_CIPHER_SUITE_CCMP, 4)))
						return _FALSE;

				default:
					break;
			}

			i += (pIE->Length + 2);
		}

		return _TRUE;
	}
	else
	{
		return _FALSE;
	}

}


unsigned int is_ap_in_wep(_adapter *padapter)
{
	u32 i;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);

	if (rtw_get_capability((WLAN_BSSID_EX *)cur_network) & WLAN_CAPABILITY_PRIVACY)
	{
		for (i = sizeof(NDIS_802_11_FIXED_IEs); i < pmlmeinfo->network.IELength;)
		{
			pIE = (PNDIS_802_11_VARIABLE_IEs)(pmlmeinfo->network.IEs + i);

			switch (pIE->ElementID)
			{
				case _VENDOR_SPECIFIC_IE_:
					if (_rtw_memcmp(pIE->data, RTW_WPA_OUI, 4))
						return _FALSE;
					break;

				case _RSN_IE_2_:
					return _FALSE;

				default:
					break;
			}

			i += (pIE->Length + 2);
		}

		return _TRUE;
	}
	else
	{
		return _FALSE;
	}

}

int wifirate2_ratetbl_inx(unsigned char rate);
int wifirate2_ratetbl_inx(unsigned char rate)
{
	int	inx = 0;
	rate = rate & 0x7f;

	switch (rate) 
	{
		case 54*2:
			inx = 11;
			break;

		case 48*2:
			inx = 10;
			break;

		case 36*2:
			inx = 9;
			break;

		case 24*2:
			inx = 8;
			break;
			
		case 18*2:
			inx = 7;
			break;

		case 12*2:
			inx = 6;
			break;

		case 9*2:
			inx = 5;
			break;
			
		case 6*2:
			inx = 4;
			break;

		case 11*2:
			inx = 3;
			break;
		case 11:
			inx = 2;
			break;

		case 2*2:
			inx = 1;
			break;
		
		case 1*2:
			inx = 0;
			break;

	}
	return inx;	
}

unsigned int update_basic_rate(unsigned char *ptn, unsigned int ptn_sz)
{
	unsigned int i, num_of_rate;
	unsigned int mask = 0;
	
	num_of_rate = (ptn_sz > NumRates)? NumRates: ptn_sz;
		
	for (i = 0; i < num_of_rate; i++)
	{
		if ((*(ptn + i)) & 0x80)
		{
			mask |= 0x1 << wifirate2_ratetbl_inx(*(ptn + i));
		}
	}
	return mask;
}

unsigned int update_supported_rate(unsigned char *ptn, unsigned int ptn_sz)
{
	unsigned int i, num_of_rate;
	unsigned int mask = 0;
	
	num_of_rate = (ptn_sz > NumRates)? NumRates: ptn_sz;
		
	for (i = 0; i < num_of_rate; i++)
	{
		mask |= 0x1 << wifirate2_ratetbl_inx(*(ptn + i));
	}

	return mask;
}

unsigned int update_MCS_rate(struct HT_caps_element *pHT_caps)
{
	unsigned int mask = 0;
	
	mask = ((pHT_caps->u.HT_cap_element.MCS_rate[0] << 12) | (pHT_caps->u.HT_cap_element.MCS_rate[1] << 20));
						
	return mask;
}

int support_short_GI(_adapter *padapter, struct HT_caps_element *pHT_caps, u8 bwmode)
{
	unsigned char					bit_offset;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
	if (!(pmlmeinfo->HT_enable))
		return _FAIL; 

	bit_offset = (bwmode & CHANNEL_WIDTH_40)? 6: 5;
	
	if (pHT_caps->u.HT_cap_element.HT_caps_info & (0x1 << bit_offset))
	{
		return _SUCCESS;
	}
	else
	{
		return _FAIL;
	}		
}

unsigned char get_highest_rate_idx(u32 mask)
{
	int i;
	unsigned char rate_idx=0;

	for(i=31; i>=0; i--)
	{
		if(mask & BIT(i))
		{
			rate_idx = i;
			break;
		}
	}

	return rate_idx;
}

unsigned char get_highest_mcs_rate(struct HT_caps_element *pHT_caps);
unsigned char get_highest_mcs_rate(struct HT_caps_element *pHT_caps)
{
	int i, mcs_rate;
	
	mcs_rate = (pHT_caps->u.HT_cap_element.MCS_rate[0] | (pHT_caps->u.HT_cap_element.MCS_rate[1] << 8));
	
	for (i = 15; i >= 0; i--)
	{
		if (mcs_rate & (0x1 << i))
		{
			break;
		}
	}
	
	return i;
}

void Update_RA_Entry(_adapter *padapter, struct sta_info *psta)
{
	rtw_hal_update_ra_mask(psta, psta->rssi_level);
}

void enable_rate_adaptive(_adapter *padapter, struct sta_info *psta);
void enable_rate_adaptive(_adapter *padapter, struct sta_info *psta)
{
	Update_RA_Entry(padapter, psta);
}

void set_sta_rate(_adapter *padapter, struct sta_info *psta)
{
	//rate adaptive	
	enable_rate_adaptive(padapter, psta);
}

// Update RRSR and Rate for USERATE
void update_tx_basic_rate(_adapter *padapter, u8 wirelessmode)
{
	NDIS_802_11_RATES_EX	supported_rates;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
#ifdef CONFIG_P2P
	struct wifidirect_info*	pwdinfo = &padapter->wdinfo;

	//	Added by Albert 2011/03/22
	//	In the P2P mode, the driver should not support the b mode.
	//	So, the Tx packet shouldn't use the CCK rate
	if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;
#endif //CONFIG_P2P
#ifdef CONFIG_INTEL_WIDI
	if (padapter->mlmepriv.widi_state != INTEL_WIDI_STATE_NONE)
		return;
#endif //CONFIG_INTEL_WIDI

	_rtw_memset(supported_rates, 0, NDIS_802_11_LENGTH_RATES_EX);

	//clear B mod if current channel is in 5G band, avoid tx cck rate in 5G band.
	if(pmlmeext->cur_channel > 14)
		wirelessmode &= ~(WIRELESS_11B);

	if ((wirelessmode & WIRELESS_11B) && (wirelessmode == WIRELESS_11B)) {
		_rtw_memcpy(supported_rates, rtw_basic_rate_cck, 4);
	} else if (wirelessmode & WIRELESS_11B) {
		_rtw_memcpy(supported_rates, rtw_basic_rate_mix, 7);
	} else {
		_rtw_memcpy(supported_rates, rtw_basic_rate_ofdm, 3);
	}

	if (wirelessmode & WIRELESS_11B)
		update_mgnt_tx_rate(padapter, IEEE80211_CCK_RATE_1MB);
	else
		update_mgnt_tx_rate(padapter, IEEE80211_OFDM_RATE_6MB);

	rtw_hal_set_hwreg(padapter, HW_VAR_BASIC_RATE, supported_rates);
}

unsigned char check_assoc_AP(u8 *pframe, uint len)
{
	unsigned int	i;
	PNDIS_802_11_VARIABLE_IEs	pIE;

	for (i = sizeof(NDIS_802_11_FIXED_IEs); i < len;)
	{
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pframe + i);
		
		switch (pIE->ElementID)
		{
			case _VENDOR_SPECIFIC_IE_:
				if ((_rtw_memcmp(pIE->data, ARTHEROS_OUI1, 3)) || (_rtw_memcmp(pIE->data, ARTHEROS_OUI2, 3)))
				{
					DBG_871X("link to Artheros AP\n");
					return HT_IOT_PEER_ATHEROS;
				}
				else if (	(_rtw_memcmp(pIE->data, BROADCOM_OUI1, 3))
						|| (_rtw_memcmp(pIE->data, BROADCOM_OUI2, 3))
						|| (_rtw_memcmp(pIE->data, BROADCOM_OUI3, 3)))						
				{
					DBG_871X("link to Broadcom AP\n");
					return HT_IOT_PEER_BROADCOM;
				}
				else if (_rtw_memcmp(pIE->data, MARVELL_OUI, 3))
				{
					DBG_871X("link to Marvell AP\n");
					return HT_IOT_PEER_MARVELL;
				}
				else if (_rtw_memcmp(pIE->data, RALINK_OUI, 3))
				{
					DBG_871X("link to Ralink AP\n");
					return HT_IOT_PEER_RALINK;
				}
				else if (_rtw_memcmp(pIE->data, CISCO_OUI, 3))
				{
					DBG_871X("link to Cisco AP\n");
					return HT_IOT_PEER_CISCO;
				}
				else if (_rtw_memcmp(pIE->data, REALTEK_OUI, 3))
				{
					u32	Vender = HT_IOT_PEER_REALTEK;

					if(pIE->Length >= 5) {
						if(pIE->data[4]==1)
						{
							//if(pIE->data[5] & RT_HT_CAP_USE_LONG_PREAMBLE)
							//	bssDesc->BssHT.RT2RT_HT_Mode |= RT_HT_CAP_USE_LONG_PREAMBLE;

							if(pIE->data[5] & RT_HT_CAP_USE_92SE)
							{
								//bssDesc->BssHT.RT2RT_HT_Mode |= RT_HT_CAP_USE_92SE;
								Vender = HT_IOT_PEER_REALTEK_92SE;
							}
						}

						if(pIE->data[5] & RT_HT_CAP_USE_SOFTAP)
							Vender = HT_IOT_PEER_REALTEK_SOFTAP;

						if(pIE->data[4] == 2)
						{
							if(pIE->data[6] & RT_HT_CAP_USE_JAGUAR_BCUT) {
								Vender = HT_IOT_PEER_REALTEK_JAGUAR_BCUTAP;
								DBG_871X("link to Realtek JAGUAR_BCUTAP\n");
							}
							if(pIE->data[6] & RT_HT_CAP_USE_JAGUAR_CCUT) {
								Vender = HT_IOT_PEER_REALTEK_JAGUAR_CCUTAP;
								DBG_871X("link to Realtek JAGUAR_CCUTAP\n");
							}
						}
					}
				
					DBG_871X("link to Realtek AP\n");
					return Vender;
				}
				else if (_rtw_memcmp(pIE->data, AIRGOCAP_OUI,3))
				{
					DBG_871X("link to Airgo Cap\n");
					return HT_IOT_PEER_AIRGO;
				}
				else
				{
					break;
				}
						
			default:
				break;
		}
				
		i += (pIE->Length + 2);
	}
	
	DBG_871X("link to new AP\n");
	return HT_IOT_PEER_UNKNOWN;
}

void update_capinfo(PADAPTER Adapter, u16 updateCap)
{
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	BOOLEAN		ShortPreamble;

	// Check preamble mode, 2005.01.06, by rcnjko.
	// Mark to update preamble value forever, 2008.03.18 by lanhsin
	//if( pMgntInfo->RegPreambleMode == PREAMBLE_AUTO )
	{
			
		if(updateCap & cShortPreamble)
		{ // Short Preamble
			if(pmlmeinfo->preamble_mode != PREAMBLE_SHORT) // PREAMBLE_LONG or PREAMBLE_AUTO
			{
				ShortPreamble = _TRUE;
				pmlmeinfo->preamble_mode = PREAMBLE_SHORT;
				rtw_hal_set_hwreg( Adapter, HW_VAR_ACK_PREAMBLE, (u8 *)&ShortPreamble );
			}
		}
		else
		{ // Long Preamble
			if(pmlmeinfo->preamble_mode != PREAMBLE_LONG)  // PREAMBLE_SHORT or PREAMBLE_AUTO
			{
				ShortPreamble = _FALSE;
				pmlmeinfo->preamble_mode = PREAMBLE_LONG;
				rtw_hal_set_hwreg( Adapter, HW_VAR_ACK_PREAMBLE, (u8 *)&ShortPreamble );
			}
		}
	}

	if ( updateCap & cIBSS ) {
		//Filen: See 802.11-2007 p.91
		pmlmeinfo->slotTime = NON_SHORT_SLOT_TIME;
	}
	else
	{
		//Filen: See 802.11-2007 p.90
		if( pmlmeext->cur_wireless_mode & (WIRELESS_11_24N | WIRELESS_11A | WIRELESS_11_5N | WIRELESS_11AC))
		{
			pmlmeinfo->slotTime = SHORT_SLOT_TIME;
		}
		else if( pmlmeext->cur_wireless_mode & (WIRELESS_11G))
		{
			if( (updateCap & cShortSlotTime) /* && (!(pMgntInfo->pHTInfo->RT2RT_HT_Mode & RT_HT_CAP_USE_LONG_PREAMBLE)) */)
			{ // Short Slot Time
				pmlmeinfo->slotTime = SHORT_SLOT_TIME;
			}
			else
			{ // Long Slot Time
				pmlmeinfo->slotTime = NON_SHORT_SLOT_TIME;
			}
		}
		else
		{
			//B Mode
			pmlmeinfo->slotTime = NON_SHORT_SLOT_TIME;
		}
 	}
 
	rtw_hal_set_hwreg( Adapter, HW_VAR_SLOT_TIME, &pmlmeinfo->slotTime );

}

/*
* set adapter.mlmeextpriv.mlmext_info.HT_enable
* set adapter.mlmeextpriv.cur_wireless_mode
* set SIFS register
* set mgmt tx rate
*/
void update_wireless_mode(_adapter *padapter)
{
	int ratelen, network_type = 0;
	u32 SIFS_Timer;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	unsigned char			*rate = cur_network->SupportedRates;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo= &(padapter->wdinfo);
#endif //CONFIG_P2P

	ratelen = rtw_get_rateset_len(cur_network->SupportedRates);

	if ((pmlmeinfo->HT_info_enable) && (pmlmeinfo->HT_caps_enable))
	{
		pmlmeinfo->HT_enable = 1;
	}

	if(pmlmeext->cur_channel > 14)
	{
		if (pmlmeinfo->VHT_enable)
			network_type = WIRELESS_11AC;
		else if (pmlmeinfo->HT_enable)
			network_type = WIRELESS_11_5N;

		network_type |= WIRELESS_11A;
	}
	else
	{
		if (pmlmeinfo->VHT_enable)
			network_type = WIRELESS_11AC;
		else if (pmlmeinfo->HT_enable)
			network_type = WIRELESS_11_24N;
	
		if ((cckratesonly_included(rate, ratelen)) == _TRUE)
		{
			network_type |= WIRELESS_11B;
		}
		else if((cckrates_included(rate, ratelen)) == _TRUE)
		{
			network_type |= WIRELESS_11BG;
		}
		else
		{
			network_type |= WIRELESS_11G;
		}
	}

	pmlmeext->cur_wireless_mode = network_type & padapter->registrypriv.wireless_mode;
	/* DBG_871X("network_type=%02x, padapter->registrypriv.wireless_mode=%02x\n", network_type, padapter->registrypriv.wireless_mode); */
/*
	if((pmlmeext->cur_wireless_mode==WIRELESS_11G) ||
		(pmlmeext->cur_wireless_mode==WIRELESS_11BG))//WIRELESS_MODE_G)
		SIFS_Timer = 0x0a0a;//CCK
	else
		SIFS_Timer = 0x0e0e;//pHalData->SifsTime; //OFDM
*/
	
	SIFS_Timer = 0x0a0a0808; //0x0808 -> for CCK, 0x0a0a -> for OFDM
                             //change this value if having IOT issues.
		
	rtw_hal_set_hwreg( padapter, HW_VAR_RESP_SIFS,  (u8 *)&SIFS_Timer);

	rtw_hal_set_hwreg( padapter, HW_VAR_WIRELESS_MODE,  (u8 *)&(pmlmeext->cur_wireless_mode));

	if ((pmlmeext->cur_wireless_mode & WIRELESS_11B)
#ifdef CONFIG_P2P
		&& rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)
#endif /* CONFIG_P2P */
		)
		update_mgnt_tx_rate(padapter, IEEE80211_CCK_RATE_1MB);
	else
		update_mgnt_tx_rate(padapter, IEEE80211_OFDM_RATE_6MB);
}

void fire_write_MAC_cmd(_adapter *padapter, unsigned int addr, unsigned int value);
void fire_write_MAC_cmd(_adapter *padapter, unsigned int addr, unsigned int value)
{
#if 0
	struct cmd_obj					*ph2c;
	struct reg_rw_parm			*pwriteMacPara;
	struct cmd_priv					*pcmdpriv = &(padapter->cmdpriv);

	if ((ph2c = (struct cmd_obj*)rtw_zmalloc(sizeof(struct cmd_obj))) == NULL)
	{
		return;
	}	

	if ((pwriteMacPara = (struct reg_rw_parm*)rtw_malloc(sizeof(struct reg_rw_parm))) == NULL) 
	{		
		rtw_mfree((unsigned char *)ph2c, sizeof(struct cmd_obj));
		return;
	}
	
	pwriteMacPara->rw = 1;
	pwriteMacPara->addr = addr;
	pwriteMacPara->value = value;
	
	init_h2fwcmd_w_parm_no_rsp(ph2c, pwriteMacPara, GEN_CMD_CODE(_Write_MACREG));
	rtw_enqueue_cmd(pcmdpriv, ph2c);
#endif	
}

void update_sta_basic_rate(struct sta_info *psta, u8 wireless_mode)
{
	if(IsSupportedTxCCK(wireless_mode))
	{
		// Only B, B/G, and B/G/N AP could use CCK rate
		_rtw_memcpy(psta->bssrateset, rtw_basic_rate_cck, 4);
		psta->bssratelen = 4;
	}
	else
	{
		_rtw_memcpy(psta->bssrateset, rtw_basic_rate_ofdm, 3);
		psta->bssratelen = 3;
	}
}

int rtw_ies_get_supported_rate(u8 *ies, uint ies_len, u8 *rate_set, u8 *rate_num)
{
	u8 *ie;
	unsigned int ie_len;

	if (!rate_set || !rate_num)
		return _FALSE;

	*rate_num = 0;

	ie = rtw_get_ie(ies, _SUPPORTEDRATES_IE_, &ie_len, ies_len);
	if (ie == NULL)
		goto ext_rate;

	_rtw_memcpy(rate_set, ie + 2, ie_len);
	*rate_num = ie_len;

ext_rate:
	ie = rtw_get_ie(ies, _EXT_SUPPORTEDRATES_IE_, &ie_len, ies_len);
	if (ie) {
		_rtw_memcpy(rate_set + *rate_num, ie + 2, ie_len);
		*rate_num += ie_len;
	}

	if (*rate_num == 0)
		return _FAIL;

	if (0) {
		int i;

		for (i = 0; i < *rate_num; i++)
			DBG_871X("rate:0x%02x\n", *(rate_set + i));
	}

	return _SUCCESS;
}

void process_addba_req(_adapter *padapter, u8 *paddba_req, u8 *addr)
{
	struct sta_info *psta;
	u16 tid, start_seq, param;	
	struct recv_reorder_ctrl *preorder_ctrl;
	struct sta_priv *pstapriv = &padapter->stapriv;	
	struct ADDBA_request	*preq = (struct ADDBA_request*)paddba_req;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u8 size;

	psta = rtw_get_stainfo(pstapriv, addr);
	if (!psta)
		goto exit;

	start_seq = le16_to_cpu(preq->BA_starting_seqctrl) >> 4;

	param = le16_to_cpu(preq->BA_para_set);
	tid = (param>>2)&0x0f;

	preorder_ctrl = &psta->recvreorder_ctrl[tid];

	#ifdef CONFIG_UPDATE_INDICATE_SEQ_WHILE_PROCESS_ADDBA_REQ
	preorder_ctrl->indicate_seq = start_seq;
	#ifdef DBG_RX_SEQ
	DBG_871X("DBG_RX_SEQ %s:%d IndicateSeq: %d, start_seq: %d\n", __func__, __LINE__,
		preorder_ctrl->indicate_seq, start_seq);
	#endif
	#else
	preorder_ctrl->indicate_seq = 0xffff;
	#endif

	preorder_ctrl->enable = rtw_rx_ampdu_is_accept(padapter);
	size = rtw_rx_ampdu_size(padapter);

	if (preorder_ctrl->enable == _TRUE) {
		preorder_ctrl->ampdu_size = size;
		issue_addba_rsp(padapter, addr, tid, 0, size);
	} else {
		issue_addba_rsp(padapter, addr, tid, 37, size); /* reject ADDBA Req */
	}

exit:
	return;
}

void update_TSF(struct mlme_ext_priv *pmlmeext, u8 *pframe, uint len)
{	
	u8* pIE;
	u32 *pbuf;
		
	pIE = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);
	pbuf = (u32*)pIE;

	pmlmeext->TSFValue = le32_to_cpu(*(pbuf+1));
	
	pmlmeext->TSFValue = pmlmeext->TSFValue << 32;

	pmlmeext->TSFValue |= le32_to_cpu(*pbuf);
}

void correct_TSF(_adapter *padapter, struct mlme_ext_priv *pmlmeext)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_CORRECT_TSF, 0);
}

void adaptive_early_32k(struct mlme_ext_priv *pmlmeext, u8 *pframe, uint len)
{	
	int i;
	u8* pIE;
	u32 *pbuf;
	u64 tsf=0;
	u32 delay_ms;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);


	pmlmeext->bcn_cnt++;

	pIE = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);
	pbuf = (u32*)pIE;

	tsf = le32_to_cpu(*(pbuf+1));	
	tsf = tsf << 32;
	tsf |= le32_to_cpu(*pbuf);

	//DBG_871X("%s(): tsf_upper= 0x%08x, tsf_lower=0x%08x\n", __func__, (u32)(tsf>>32), (u32)tsf);

	//delay = (timestamp mod 1024*100)/1000 (unit: ms)
	//delay_ms = do_div(tsf, (pmlmeinfo->bcn_interval*1024))/1000;
	delay_ms = rtw_modular64(tsf, (pmlmeinfo->bcn_interval*1024));
	delay_ms = delay_ms/1000;

	if(delay_ms >= 8)
	{
		pmlmeext->bcn_delay_cnt[8]++;
		//pmlmeext->bcn_delay_ratio[8] = (pmlmeext->bcn_delay_cnt[8] * 100) /pmlmeext->bcn_cnt;
	}
	else
	{
		pmlmeext->bcn_delay_cnt[delay_ms]++;
		//pmlmeext->bcn_delay_ratio[delay_ms] = (pmlmeext->bcn_delay_cnt[delay_ms] * 100) /pmlmeext->bcn_cnt;
	}

/*
	DBG_871X("%s(): (a)bcn_cnt = %d\n", __func__, pmlmeext->bcn_cnt);


	for(i=0; i<9; i++)
	{
		DBG_871X("%s():bcn_delay_cnt[%d]=%d,  bcn_delay_ratio[%d]=%d\n", __func__, i, 
			pmlmeext->bcn_delay_cnt[i] , i, pmlmeext->bcn_delay_ratio[i]);			
	}	
*/

	//dump for  adaptive_early_32k
	if(pmlmeext->bcn_cnt > 100 && (pmlmeext->adaptive_tsf_done==_TRUE))
	{	
		u8 ratio_20_delay, ratio_80_delay;
		u8 DrvBcnEarly, DrvBcnTimeOut;

		ratio_20_delay = 0;
		ratio_80_delay = 0;
		DrvBcnEarly = 0xff;
		DrvBcnTimeOut = 0xff;
	
		DBG_871X("%s(): bcn_cnt = %d\n", __func__, pmlmeext->bcn_cnt);

		for(i=0; i<9; i++)
		{
			pmlmeext->bcn_delay_ratio[i] = (pmlmeext->bcn_delay_cnt[i] * 100) /pmlmeext->bcn_cnt;
			
		
			//DBG_871X("%s():bcn_delay_cnt[%d]=%d,  bcn_delay_ratio[%d]=%d\n", __func__, i, 
			//	pmlmeext->bcn_delay_cnt[i] , i, pmlmeext->bcn_delay_ratio[i]);

			ratio_20_delay += pmlmeext->bcn_delay_ratio[i];
			ratio_80_delay += pmlmeext->bcn_delay_ratio[i];
		
			if(ratio_20_delay > 20 && DrvBcnEarly == 0xff)
			{			
				DrvBcnEarly = i;
				//DBG_871X("%s(): DrvBcnEarly = %d\n", __func__, DrvBcnEarly);
			}	

			if(ratio_80_delay > 80 && DrvBcnTimeOut == 0xff)
			{
				DrvBcnTimeOut = i;
				//DBG_871X("%s(): DrvBcnTimeOut = %d\n", __func__, DrvBcnTimeOut);
			}
			
			//reset adaptive_early_32k cnt
			pmlmeext->bcn_delay_cnt[i] = 0;
			pmlmeext->bcn_delay_ratio[i] = 0;			
		}	

		pmlmeext->DrvBcnEarly = DrvBcnEarly;
		pmlmeext->DrvBcnTimeOut = DrvBcnTimeOut;

		pmlmeext->bcn_cnt = 0;		
	}	
	
}


void beacon_timing_control(_adapter *padapter)
{
	rtw_hal_bcn_related_reg_setting(padapter);
}

#define CONFIG_SHARED_BMC_MACID

void dump_macid_map(void *sel, struct macid_bmp *map, u8 max_num)
{
	DBG_871X_SEL_NL(sel, "0x%08x\n", map->m0);
#if (MACID_NUM_SW_LIMIT > 32)
	if (max_num && max_num > 32)
		DBG_871X_SEL_NL(sel, "0x%08x\n", map->m1);
#endif
#if (MACID_NUM_SW_LIMIT > 64)
	if (max_num && max_num > 64)
		DBG_871X_SEL_NL(sel, "0x%08x\n", map->m2);
#endif
#if (MACID_NUM_SW_LIMIT > 96)
	if (max_num && max_num > 96)
		DBG_871X_SEL_NL(sel, "0x%08x\n", map->m3);
#endif
}

inline bool rtw_macid_is_set(struct macid_bmp *map, u8 id)
{
	if (id < 32)
		return (map->m0 & BIT(id));
#if (MACID_NUM_SW_LIMIT > 32)
	else if (id < 64)
		return (map->m1 & BIT(id-32));
#endif
#if (MACID_NUM_SW_LIMIT > 64)
	else if (id < 96)
		return (map->m2 & BIT(id-64));
#endif
#if (MACID_NUM_SW_LIMIT > 96)
	else if (id < 128)
		return (map->m3 & BIT(id-96));
#endif
	else
		rtw_warn_on(1);

	return 0;
}

inline void rtw_macid_map_set(struct macid_bmp *map, u8 id)
{
	if (id < 32)
		map->m0 |= BIT(id);
#if (MACID_NUM_SW_LIMIT > 32)
	else if (id < 64)
		map->m1 |= BIT(id-32);
#endif
#if (MACID_NUM_SW_LIMIT > 64)
	else if (id < 96)
		map->m2 |= BIT(id-64);
#endif
#if (MACID_NUM_SW_LIMIT > 96)
	else if (id < 128)
		map->m3 |= BIT(id-96);
#endif
	else
		rtw_warn_on(1);
}

inline void rtw_macid_map_clr(struct macid_bmp *map, u8 id)
{
	if (id < 32)
		map->m0 &= ~BIT(id);
#if (MACID_NUM_SW_LIMIT > 32)
	else if (id < 64)
		map->m1 &= ~BIT(id-32);
#endif
#if (MACID_NUM_SW_LIMIT > 64)
	else if (id < 96)
		map->m2 &= ~BIT(id-64);
#endif
#if (MACID_NUM_SW_LIMIT > 96)
	else if (id < 128)
		map->m3 &= ~BIT(id-96);
#endif
	else
		rtw_warn_on(1);
}

inline bool rtw_macid_is_used(struct macid_ctl_t *macid_ctl, u8 id)
{
	return rtw_macid_is_set(&macid_ctl->used, id);
}

inline bool rtw_macid_is_bmc(struct macid_ctl_t *macid_ctl, u8 id)
{
	return rtw_macid_is_set(&macid_ctl->bmc, id);
}

inline s8 rtw_macid_get_if_g(struct macid_ctl_t *macid_ctl, u8 id)
{
	int i;

#ifdef CONFIG_SHARED_BMC_MACID
	if (rtw_macid_is_bmc(macid_ctl,id))
		return -1;
#endif

	for (i=0;i<IFACE_ID_MAX;i++) {
		if (rtw_macid_is_set(&macid_ctl->if_g[i], id))
			return i;
	}
	return -1;
}

inline s8 rtw_macid_get_ch_g(struct macid_ctl_t *macid_ctl, u8 id)
{
	int i;

	for (i=0;i<2;i++) {
		if (rtw_macid_is_set(&macid_ctl->ch_g[i], id))
			return i;
	}
	return -1;
}

void rtw_alloc_macid(_adapter *padapter, struct sta_info *psta)
{
	int i;
	_irqL irqL;
	u8 bc_addr[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	struct macid_bmp *used_map = &macid_ctl->used;
	//static u8 last_id = 0; /* for testing */
	u8 last_id = 0;

	if (_rtw_memcmp(psta->hwaddr, adapter_mac_addr(padapter), ETH_ALEN)) {
		psta->mac_id = macid_ctl->num;
		return;
	}

#ifdef CONFIG_SHARED_BMC_MACID
	if(_rtw_memcmp(psta->hwaddr, bc_addr, ETH_ALEN)) {
		/* use shared broadcast & multicast macid 1 */
		_enter_critical_bh(&macid_ctl->lock, &irqL);
		rtw_macid_map_set(used_map, 1);
		rtw_macid_map_set(&macid_ctl->bmc, 1);
		for (i=0;i<IFACE_ID_MAX;i++)
			rtw_macid_map_set(&macid_ctl->if_g[padapter->iface_id], 1);
		macid_ctl->sta[1] = psta;
		/* TODO ch_g? */
		_exit_critical_bh(&macid_ctl->lock, &irqL);
		i = 1;
		goto assigned;
	}
#endif

	_enter_critical_bh(&macid_ctl->lock, &irqL);

	for (i=last_id;i<macid_ctl->num;i++) {
		#ifdef CONFIG_SHARED_BMC_MACID
		if (i == 1)
			continue;
		#endif
		if (!rtw_macid_is_used(macid_ctl, i))
			break;
	}

	if (i < macid_ctl->num) {

		rtw_macid_map_set(used_map, i);

		if(_rtw_memcmp(psta->hwaddr, bc_addr, ETH_ALEN))
			rtw_macid_map_set(&macid_ctl->bmc, i);

		rtw_macid_map_set(&macid_ctl->if_g[padapter->iface_id], i);
		macid_ctl->sta[i] = psta;

		/* TODO ch_g? */

		last_id++;
		last_id %= macid_ctl->num;
	}

	_exit_critical_bh(&macid_ctl->lock, &irqL);

	if (i >= macid_ctl->num) {
		psta->mac_id = macid_ctl->num;
		DBG_871X_LEVEL(_drv_err_, FUNC_ADPT_FMT" if%u, hwaddr:"MAC_FMT" no available macid\n"
			, FUNC_ADPT_ARG(padapter), padapter->iface_id+1, MAC_ARG(psta->hwaddr));
		rtw_warn_on(1);
		goto exit;
	} else {
		goto assigned;
	}

assigned:
	psta->mac_id = i;
	DBG_871X(FUNC_ADPT_FMT" if%u, hwaddr:"MAC_FMT" macid:%u\n"
		, FUNC_ADPT_ARG(padapter), padapter->iface_id+1, MAC_ARG(psta->hwaddr), psta->mac_id);

exit:
	return;
}

void rtw_release_macid(_adapter *padapter, struct sta_info *psta)
{
	_irqL irqL;
	u8 bc_addr[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);

	if (_rtw_memcmp(psta->hwaddr, adapter_mac_addr(padapter), ETH_ALEN))
		return;

#ifdef CONFIG_SHARED_BMC_MACID
	if(_rtw_memcmp(psta->hwaddr, bc_addr, ETH_ALEN))
		return;

	if (psta->mac_id == 1) {
		DBG_871X_LEVEL(_drv_err_, FUNC_ADPT_FMT" if%u, hwaddr:"MAC_FMT" with macid:%u\n"
			, FUNC_ADPT_ARG(padapter), padapter->iface_id+1, MAC_ARG(psta->hwaddr), psta->mac_id);
		rtw_warn_on(1);
		return;
	}
#endif

	_enter_critical_bh(&macid_ctl->lock, &irqL);

	if (psta->mac_id < macid_ctl->num) {
		int i;

		if (!rtw_macid_is_used(macid_ctl, psta->mac_id)) {
			DBG_871X_LEVEL(_drv_err_, FUNC_ADPT_FMT" if%u, hwaddr:"MAC_FMT" macid:%u not used\n"
				, FUNC_ADPT_ARG(padapter), padapter->iface_id+1, MAC_ARG(psta->hwaddr), psta->mac_id);
			rtw_warn_on(1);
		}

		rtw_macid_map_clr(&macid_ctl->used, psta->mac_id);
		rtw_macid_map_clr(&macid_ctl->bmc, psta->mac_id);
		for (i=0;i<IFACE_ID_MAX;i++)
			rtw_macid_map_clr(&macid_ctl->if_g[i], psta->mac_id);
		for (i=0;i<2;i++)
			rtw_macid_map_clr(&macid_ctl->ch_g[i], psta->mac_id);
		macid_ctl->sta[psta->mac_id] = NULL;
	}

	_exit_critical_bh(&macid_ctl->lock, &irqL);

	psta->mac_id = macid_ctl->num;
}

//For 8188E RA
u8 rtw_search_max_mac_id(_adapter *padapter)
{
	u8 max_mac_id=0;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	int i;
	_irqL irqL;

	/* TODO: Only search for connected macid? */

	_enter_critical_bh(&macid_ctl->lock, &irqL);
	for(i=(macid_ctl->num-1); i>0 ; i--) {
		if (!rtw_macid_is_used(macid_ctl, i))
			break;
	}
	_exit_critical_bh(&macid_ctl->lock, &irqL);
	max_mac_id = i;

	return max_mac_id;
}		

inline void rtw_macid_ctl_set_h2c_msr(struct macid_ctl_t *macid_ctl, u8 id, u8 h2c_msr)
{
	if (id >= macid_ctl->num) {
		rtw_warn_on(1);
		return;
	}

	macid_ctl->h2c_msr[id] = h2c_msr;
	if (0)
		DBG_871X("macid:%u, h2c_msr:"H2C_MSR_FMT"\n", id, H2C_MSR_ARG(&macid_ctl->h2c_msr[id]));
}

inline void rtw_macid_ctl_set_bw(struct macid_ctl_t *macid_ctl, u8 id, u8 bw)
{
	if (id >= macid_ctl->num) {
		rtw_warn_on(1);
		return;
	}

	macid_ctl->bw[id] = bw;
	if (0)
		RTW_INFO("macid:%u, bw:%s\n", id, ch_width_str(macid_ctl->bw[id]));
}

inline void rtw_macid_ctl_set_vht_en(struct macid_ctl_t *macid_ctl, u8 id, u8 en)
{
	if (id >= macid_ctl->num) {
		rtw_warn_on(1);
		return;
	}

	macid_ctl->vht_en[id] = en;
	if (0)
		RTW_INFO("macid:%u, vht_en:%u\n", id, macid_ctl->vht_en[id]);
}

inline void rtw_macid_ctl_set_rate_bmp0(struct macid_ctl_t *macid_ctl, u8 id, u32 bmp)
{
	if (id >= macid_ctl->num) {
		rtw_warn_on(1);
		return;
	}

	macid_ctl->rate_bmp0[id] = bmp;
	if (0)
		RTW_INFO("macid:%u, rate_bmp0:0x%08X\n", id, macid_ctl->rate_bmp0[id]);
}

inline void rtw_macid_ctl_set_rate_bmp1(struct macid_ctl_t *macid_ctl, u8 id, u32 bmp)
{
	if (id >= macid_ctl->num) {
		rtw_warn_on(1);
		return;
	}

	macid_ctl->rate_bmp1[id] = bmp;
	if (0)
		RTW_INFO("macid:%u, rate_bmp1:0x%08X\n", id, macid_ctl->rate_bmp1[id]);
}

inline void rtw_macid_ctl_init(struct macid_ctl_t *macid_ctl)
{
	_rtw_spinlock_init(&macid_ctl->lock);
}

inline void rtw_macid_ctl_deinit(struct macid_ctl_t *macid_ctl)
{
	_rtw_spinlock_free(&macid_ctl->lock);
}

#if 0
unsigned int setup_beacon_frame(_adapter *padapter, unsigned char *beacon_frame)
{
	unsigned short				ATIMWindow;
	unsigned char					*pframe;
	struct tx_desc 				*ptxdesc;
	struct rtw_ieee80211_hdr 	*pwlanhdr;
	unsigned short				*fctrl;
	unsigned int					rate_len, len = 0;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	
	_rtw_memset(beacon_frame, 0, 256);
	
	pframe = beacon_frame + TXDESC_SIZE;
	
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;	
	
	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	
	_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(cur_network), ETH_ALEN);
	
	SetFrameSubType(pframe, WIFI_BEACON);
	
	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);	
	len = sizeof(struct rtw_ieee80211_hdr_3addr);

	//timestamp will be inserted by hardware
	pframe += 8;
	len += 8;

	// beacon interval: 2 bytes
	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2); 

	pframe += 2;
	len += 2;

	// capability info: 2 bytes
	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	len += 2;

	// SSID
	pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &len);

	// supported rates...
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8)? 8: rate_len), cur_network->SupportedRates, &len);

	// DS parameter set
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &len);

	// IBSS Parameter Set...
	//ATIMWindow = cur->Configuration.ATIMWindow;
	ATIMWindow = 0;
	pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &len);

	//todo: ERP IE
	
	// EXTERNDED SUPPORTED RATE
	if (rate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &len);
	}

	if ((len + TXDESC_SIZE) > 256)
	{
		//DBG_871X("marc: beacon frame too large\n");
		return 0;
	}

	//fill the tx descriptor
	ptxdesc = (struct tx_desc *)beacon_frame;
	
	//offset 0	
	ptxdesc->txdw0 |= cpu_to_le32(len & 0x0000ffff); 
	ptxdesc->txdw0 |= cpu_to_le32(((TXDESC_SIZE + OFFSET_SZ) << OFFSET_SHT) & 0x00ff0000); //default = 32 bytes for TX Desc
	
	//offset 4	
	ptxdesc->txdw1 |= cpu_to_le32((0x10 << QSEL_SHT) & 0x00001f00);
	
	//offset 8		
	ptxdesc->txdw2 |= cpu_to_le32(BMC);
	ptxdesc->txdw2 |= cpu_to_le32(BK);

	//offset 16		
	ptxdesc->txdw4 = 0x80000000;
	
	//offset 20
	ptxdesc->txdw5 = 0x00000000; //1M	
	
	return (len + TXDESC_SIZE);
}
#endif

_adapter *dvobj_get_port0_adapter(struct dvobj_priv *dvobj)
{
	_adapter *port0_iface = NULL;
	int i;
	for (i=0;i<dvobj->iface_nums;i++) {
		if (get_iface_type(dvobj->padapters[i]) == IFACE_PORT0)
			break;
	}

	if (i<0 || i>=dvobj->iface_nums)
		rtw_warn_on(1);
	else
		port0_iface = dvobj->padapters[i];

	return port0_iface;
}

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
void rtw_get_current_ip_address(PADAPTER padapter, u8 *pcurrentip)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct in_device *my_ip_ptr = padapter->pnetdev->ip_ptr;
	u8 ipaddress[4];
	
	if ( (pmlmeinfo->state & WIFI_FW_LINKING_STATE) ||
			pmlmeinfo->state & WIFI_FW_AP_STATE) {
		if ( my_ip_ptr != NULL ) {
			struct in_ifaddr *my_ifa_list  = my_ip_ptr->ifa_list ;
			if ( my_ifa_list != NULL ) {
				ipaddress[0] = my_ifa_list->ifa_address & 0xFF;
				ipaddress[1] = (my_ifa_list->ifa_address >> 8) & 0xFF;
				ipaddress[2] = (my_ifa_list->ifa_address >> 16) & 0xFF;
				ipaddress[3] = my_ifa_list->ifa_address >> 24;
				DBG_871X("%s: %d.%d.%d.%d ==========\n", __func__, 
						ipaddress[0], ipaddress[1], ipaddress[2], ipaddress[3]);
				_rtw_memcpy(pcurrentip, ipaddress, 4);
			}
		}
	}
}
#endif
#ifdef CONFIG_WOWLAN
bool rtw_check_pattern_valid(u8 *input, u8 len)
{
	int i = 0;
	bool res = _FALSE;

	if (len != 2)
		goto exit;

	for (i = 0 ; i < len ; i++)
		if (IsHexDigit(input[i]) == _FALSE)
			goto exit;

	res = _SUCCESS;

exit:
	return res;
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

	if (idx > MAX_WKFM_NUM) {
		DBG_871X("[Error]: %s, pattern index is out of range\n",
			 __func__);
		return _FALSE;
	}

	rtw_hal_get_def_var(adapter, HAL_DEF_RX_DMA_SZ_WOW,
			    (u8 *)&rx_dma_buff_sz);
	
	if (rx_dma_buff_sz == 0) {
		DBG_871X("[Error]: %s, rx_dma_buff_sz is 0!!\n", __func__);
		return _FALSE;
	}

	rtw_hal_get_def_var(adapter, HAL_DEF_RX_PAGE_SIZE, (u8 *)&page_sz);
	
	if (page_sz == 0) {
		DBG_871X("[Error]: %s, page_sz is 0!!\n", __func__);
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
			(cam_start_offset + idx*WKFMCAM_SIZE + i*8) >> 3;
		rtw_write16(adapter, REG_PKTBUF_DBG_CTRL, rx_buf_ptr);

		rtw_write16(adapter, REG_RXPKTBUF_CTRL, ctrl_l);
		data_l = rtw_read32(adapter, REG_PKTBUF_DBG_DATA_L);
		data_h = rtw_read32(adapter, REG_PKTBUF_DBG_DATA_H);

		DBG_871X("[%d]: %08x %08x\n", i, data_h, data_l);

		count = 0;

		do {
			tmp = rtw_read8(adapter, REG_RXPKTBUF_CTRL);
			rtw_udelay_os(2);
			count++;
		} while (!tmp && count < 100);

		if (count >= 100) {
			DBG_871X("%s count:%d\n", __func__, count);
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

	if (idx > MAX_WKFM_NUM) {
		DBG_871X("[Error]: %s, pattern index is out of range\n",
			 __func__);
		return _FALSE;
	}

	rtw_hal_get_def_var(adapter, HAL_DEF_RX_DMA_SZ_WOW,
			    (u8 *)&rx_dma_buff_sz);
	
	if (rx_dma_buff_sz == 0) {
		DBG_871X("[Error]: %s, rx_dma_buff_sz is 0!!\n", __func__);
		return _FALSE;
	}

	rtw_hal_get_def_var(adapter, HAL_DEF_RX_PAGE_SIZE, (u8 *)&page_sz);
	
	if (page_sz == 0) {
		DBG_871X("[Error]: %s, page_sz is 0!!\n", __func__);
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
			(cam_start_offset + idx*WKFMCAM_SIZE + i*4) >> 3;
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


void rtw_dump_priv_pattern(_adapter *adapter, u8 idx)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	char str_1[128];
	char *p_str;
	u8 val8 = 0;
	int i = 0, j = 0, len = 0, max_len = 0;

	DBG_871X("=========[%d]========\n", idx);

	DBG_871X(">>>priv_pattern_content:\n");
	p_str = str_1;
	max_len = sizeof(str_1);
	for (i = 0 ; i < MAX_WKFM_PATTERN_SIZE/8 ; i++) {
		_rtw_memset(p_str, 0, max_len);
		len = 0;
		for (j = 0 ; j < 8 ; j++) {
			val8 = pwrctl->patterns[idx].content[i*8 + j];
			len += snprintf(p_str + len, max_len - len,
					"%02x ", val8);
		}
		DBG_871X("%s\n", p_str);
	}

	DBG_871X(">>>priv_pattern_mask:\n");
	for (i = 0 ; i < MAX_WKFM_SIZE/8 ; i++) {
		_rtw_memset(p_str, 0, max_len);
		len = 0;
		for (j = 0 ; j < 8 ; j++) {
			val8 = pwrctl->patterns[idx].mask[i*8 + j];
			len += snprintf(p_str + len, max_len - len,
					"%02x ", val8);
		}
		DBG_871X("%s\n", p_str);
	}
}

void rtw_clean_pattern(_adapter *adapter)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	struct rtl_wow_pattern zero_pattern;
	int i = 0;

	_rtw_memset(&zero_pattern, 0, sizeof(struct rtl_wow_pattern));

	zero_pattern.type = PATTERN_INVALID;

	for (i = 0; i < MAX_WKFM_NUM; i++)
		rtw_write_to_frame_mask(adapter, i, &zero_pattern);

	pwrctl->wowlan_pattern_idx = 0;
	rtw_write8(adapter, REG_WKFMCAM_NUM, pwrctl->wowlan_pattern_idx);
}

void rtw_get_sec_iv(PADAPTER padapter, u8*pcur_dot11txpn, u8 *StaAddr)
{
	struct sta_info		*psta;
	struct security_priv *psecpriv = &padapter->securitypriv;

	_rtw_memset(pcur_dot11txpn, 0, 8);
	if(NULL == StaAddr)
		return; 
	psta = rtw_get_stainfo(&padapter->stapriv, StaAddr);
	DBG_871X("%s(): StaAddr: %02x %02x %02x %02x %02x %02x\n", 
		__func__, StaAddr[0], StaAddr[1], StaAddr[2],
		StaAddr[3], StaAddr[4], StaAddr[5]);

	if(psta)
	{
		if (psecpriv->dot11PrivacyAlgrthm != _NO_PRIVACY_ && psta->dot11txpn.val > 0)
			psta->dot11txpn.val--;
		AES_IV(pcur_dot11txpn, psta->dot11txpn, 0);

		DBG_871X("%s(): CurrentIV: %02x %02x %02x %02x %02x %02x %02x %02x \n"
		, __func__, pcur_dot11txpn[0],pcur_dot11txpn[1],
		pcur_dot11txpn[2],pcur_dot11txpn[3], pcur_dot11txpn[4],
		pcur_dot11txpn[5],pcur_dot11txpn[6],pcur_dot11txpn[7]);
	}
}
void rtw_set_sec_pn(PADAPTER padapter)
{
        struct sta_info         *psta;
        struct mlme_ext_priv    *pmlmeext = &(padapter->mlmeextpriv);
        struct mlme_ext_info    *pmlmeinfo = &(pmlmeext->mlmext_info);
        struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
		struct security_priv *psecpriv = &padapter->securitypriv;

        psta = rtw_get_stainfo(&padapter->stapriv,
			get_my_bssid(&pmlmeinfo->network));

        if(psta)
        {
		if (pwrpriv->wowlan_fw_iv > psta->dot11txpn.val)
		{
			if (psecpriv->dot11PrivacyAlgrthm != _NO_PRIVACY_)
				psta->dot11txpn.val = pwrpriv->wowlan_fw_iv + 2;
		} else {
			DBG_871X("%s(): FW IV is smaller than driver\n", __func__);
			psta->dot11txpn.val += 2;
		}
		DBG_871X("%s: dot11txpn: 0x%016llx\n", __func__ ,psta->dot11txpn.val);
        }
}
#endif //CONFIG_WOWLAN

#ifdef CONFIG_PNO_SUPPORT
#define	CSCAN_TLV_TYPE_SSID_IE	'S'
#define CIPHER_IE "key_mgmt="
#define CIPHER_NONE "NONE"
#define CIPHER_WPA_PSK "WPA-PSK"
#define CIPHER_WPA_EAP "WPA-EAP IEEE8021X"
/*
 *  SSIDs list parsing from cscan tlv list
 */
int rtw_parse_ssid_list_tlv(char** list_str, pno_ssid_t* ssid,
	int max, int *bytes_left) {
	char* str;

	int idx = 0;

	if ((list_str == NULL) || (*list_str == NULL) || (*bytes_left < 0)) {
		DBG_871X("%s error paramters\n", __func__);
		return -1;
	}

	str = *list_str;
	while (*bytes_left > 0) {

		if (str[0] != CSCAN_TLV_TYPE_SSID_IE) {
			*list_str = str;
			DBG_871X("nssid=%d left_parse=%d %d\n", idx, *bytes_left, str[0]);
			return idx;
		}

		/* Get proper CSCAN_TLV_TYPE_SSID_IE */
		*bytes_left -= 1;
		str += 1;

		if (str[0] == 0) {
			/* Broadcast SSID */
			ssid[idx].SSID_len = 0;
			memset((char*)ssid[idx].SSID, 0x0, WLAN_SSID_MAXLEN);
			*bytes_left -= 1;
			str += 1;

			DBG_871X("BROADCAST SCAN  left=%d\n", *bytes_left);
		}
		else if (str[0] <= WLAN_SSID_MAXLEN) {
			 /* Get proper SSID size */
			ssid[idx].SSID_len = str[0];
			*bytes_left -= 1;
			str += 1;

			/* Get SSID */
			if (ssid[idx].SSID_len > *bytes_left) {
				DBG_871X("%s out of memory range len=%d but left=%d\n",
				__func__, ssid[idx].SSID_len, *bytes_left);
				return -1;
			}

			memcpy((char*)ssid[idx].SSID, str, ssid[idx].SSID_len);

			*bytes_left -= ssid[idx].SSID_len;
			str += ssid[idx].SSID_len;

			DBG_871X("%s :size=%d left=%d\n",
				(char*)ssid[idx].SSID, ssid[idx].SSID_len, *bytes_left);
		}
		else {
			DBG_871X("### SSID size more that %d\n", str[0]);
			return -1;
		}

		if (idx++ >  max) {
			DBG_871X("%s number of SSIDs more that %d\n", __func__, idx);
			return -1;
		}
	}

	*list_str = str;
	return idx;
}

int rtw_parse_cipher_list(struct pno_nlo_info *nlo_info, char* list_str) {

	char *pch, *pnext, *pend;
	u8 key_len = 0, index = 0;

	pch = list_str;

	if (nlo_info == NULL || list_str == NULL) {
		DBG_871X("%s error paramters\n", __func__);
		return -1;
	}

	while (strlen(pch) != 0) {
		pnext = strstr(pch, "key_mgmt=");
		if (pnext != NULL) {
			pch = pnext + strlen(CIPHER_IE);
			pend = strstr(pch, "}");
			if (strncmp(pch, CIPHER_NONE,
						strlen(CIPHER_NONE)) == 0) {
				nlo_info->ssid_cipher_info[index] = 0x00;
			} else if (strncmp(pch, CIPHER_WPA_PSK,
						strlen(CIPHER_WPA_PSK)) == 0) {
				nlo_info->ssid_cipher_info[index] = 0x66;
			} else if (strncmp(pch, CIPHER_WPA_EAP,
						strlen(CIPHER_WPA_EAP)) == 0) {
				nlo_info->ssid_cipher_info[index] = 0x01;
			}
			index ++;
			pch = pend + 1;
		} else {
			break;
		}
	}
	return 0;
}

int rtw_dev_nlo_info_set(struct pno_nlo_info *nlo_info, pno_ssid_t* ssid,
	int num, int pno_time, int pno_repeat, int pno_freq_expo_max) {

	int i = 0;
	struct file *fp;
	mm_segment_t fs;
	loff_t pos = 0;
	u8 *source = NULL;
	long len = 0;

	DBG_871X("+%s+\n", __func__);

	nlo_info->fast_scan_period = pno_time;
	nlo_info->ssid_num = num & BIT_LEN_MASK_32(8);
	nlo_info->hidden_ssid_num = num & BIT_LEN_MASK_32(8);
	nlo_info->slow_scan_period = (pno_time * 2);
	nlo_info->fast_scan_iterations = 5;

	if (nlo_info->hidden_ssid_num > 8)
		nlo_info->hidden_ssid_num = 8;

	//TODO: channel list and probe index is all empty.
	for (i = 0 ; i < num ; i++) {
		nlo_info->ssid_length[i]
			= ssid[i].SSID_len;
	}

	/* cipher array */
	fp = filp_open("/data/misc/wifi/wpa_supplicant.conf", O_RDONLY,  0644);
	if (IS_ERR(fp)) {
		DBG_871X("Error, wpa_supplicant.conf doesn't exist.\n");
		DBG_871X("Error, cipher array using default value.\n");
		return 0;
	}

	len = i_size_read(fp->f_path.dentry->d_inode);
	if (len < 0 || len > 2048) {
		DBG_871X("Error, file size is bigger than 2048.\n");
		DBG_871X("Error, cipher array using default value.\n");
		return 0;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);

	source = rtw_zmalloc(2048);

	if (source != NULL) {
		len = vfs_read(fp, source, len, &pos);
		rtw_parse_cipher_list(nlo_info, source);
		rtw_mfree(source, 2048);
	}

	set_fs(fs);
	filp_close(fp, NULL);

	DBG_871X("-%s-\n", __func__);
	return 0;
}

int rtw_dev_ssid_list_set(struct pno_ssid_list *pno_ssid_list,
	pno_ssid_t* ssid, u8 num) {

	int i = 0;
	if(num > MAX_PNO_LIST_COUNT)
		num = MAX_PNO_LIST_COUNT;

	for (i = 0 ; i < num ; i++) {
		_rtw_memcpy(&pno_ssid_list->node[i].SSID,
			ssid[i].SSID, ssid[i].SSID_len);
		pno_ssid_list->node[i].SSID_len = ssid[i].SSID_len;
	}
	return 0;
}

int rtw_dev_scan_info_set(_adapter *padapter, pno_ssid_t* ssid,
	unsigned char ch, unsigned char ch_offset, unsigned short bw_mode) {

	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	struct pno_scan_info *scan_info = pwrctl->pscan_info;
	int i;

	scan_info->channel_num = MAX_SCAN_LIST_COUNT;
	scan_info->orig_ch = ch;
	scan_info->orig_bw = bw_mode;
	scan_info->orig_40_offset = ch_offset;

	for(i = 0 ; i < scan_info->channel_num ; i++) {
		if (i < 11)
			scan_info->ssid_channel_info[i].active = 1;
		else
			scan_info->ssid_channel_info[i].active = 0;

		scan_info->ssid_channel_info[i].timeout = 100;

		scan_info->ssid_channel_info[i].tx_power =
			PHY_GetTxPowerIndex(padapter, 0, 0x02, bw_mode, i+1);

		scan_info->ssid_channel_info[i].channel = i+1;
	}

	DBG_871X("%s, channel_num: %d, orig_ch: %d, orig_bw: %d orig_40_offset: %d\n",
		__func__, scan_info->channel_num, scan_info->orig_ch,
		scan_info->orig_bw, scan_info->orig_40_offset);
	return 0;
}

int rtw_dev_pno_set(struct net_device *net, pno_ssid_t* ssid, int num,
	int pno_time, int pno_repeat, int pno_freq_expo_max) {

	_adapter *padapter = (_adapter *)rtw_netdev_priv(net);
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	int ret = -1;

	if (num == 0) {
		DBG_871X("%s, nssid is zero, no need to setup pno ssid list\n", __func__);
		return 0;
	}

	if (pwrctl == NULL) {
		DBG_871X("%s, ERROR: pwrctl is NULL\n", __func__);
		return -1;
	} else {
		pwrctl->pnlo_info =
			(pno_nlo_info_t*)rtw_zmalloc(sizeof(pno_nlo_info_t));
		pwrctl->pno_ssid_list =
			(pno_ssid_list_t*)rtw_zmalloc(sizeof(pno_ssid_list_t));
		pwrctl->pscan_info =
			(pno_scan_info_t*)rtw_zmalloc(sizeof(pno_scan_info_t));
	}

	if (pwrctl->pnlo_info == NULL ||
		pwrctl->pscan_info == NULL ||
		pwrctl->pno_ssid_list == NULL){
		DBG_871X("%s, ERROR: alloc nlo_info, ssid_list, scan_info fail\n", __func__);
		goto failing;
	}

	pwrctl->pno_in_resume = _FALSE;

	pwrctl->pno_inited = _TRUE;
	/* NLO Info */
	ret = rtw_dev_nlo_info_set(pwrctl->pnlo_info, ssid, num,
			pno_time, pno_repeat, pno_freq_expo_max);

	/* SSID Info */
	ret = rtw_dev_ssid_list_set(pwrctl->pno_ssid_list, ssid, num);

	/* SCAN Info */
	ret = rtw_dev_scan_info_set(padapter, ssid, pmlmeext->cur_channel,
			pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

	DBG_871X("+%s num: %d, pno_time: %d, pno_repeat:%d, pno_freq_expo_max:%d+\n",
		 __func__, num, pno_time, pno_repeat, pno_freq_expo_max);

	return 0;

failing:
	if (pwrctl->pnlo_info) {
		rtw_mfree((u8 *)pwrctl->pnlo_info, sizeof(pno_nlo_info_t));
		pwrctl->pnlo_info = NULL;
	}
	if (pwrctl->pno_ssid_list) {
		rtw_mfree((u8 *)pwrctl->pno_ssid_list, sizeof(pno_ssid_list_t));
		pwrctl->pno_ssid_list = NULL;
	}
	if (pwrctl->pscan_info) {
		rtw_mfree((u8 *)pwrctl->pscan_info, sizeof(pno_scan_info_t));
		pwrctl->pscan_info = NULL;
	}

	return -1;
}

#ifdef CONFIG_PNO_SET_DEBUG
void rtw_dev_pno_debug(struct net_device *net) {
	_adapter *padapter = (_adapter *)rtw_netdev_priv(net);
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	int i = 0, j = 0;

	DBG_871X("*******NLO_INFO********\n");
	DBG_871X("ssid_num: %d\n", pwrctl->pnlo_info->ssid_num);
	DBG_871X("fast_scan_iterations: %d\n",
			pwrctl->pnlo_info->fast_scan_iterations);
	DBG_871X("fast_scan_period: %d\n", pwrctl->pnlo_info->fast_scan_period);
	DBG_871X("slow_scan_period: %d\n", pwrctl->pnlo_info->slow_scan_period);
	DBG_871X("ssid_length: ");
	for (i = 0 ; i < MAX_PNO_LIST_COUNT ; i++) {
		printk("%d, ", pwrctl->pnlo_info->ssid_length[i]);
	}
	DBG_871X("\n");

	DBG_871X("cipher_info: ");
	for (i = 0 ; i < MAX_PNO_LIST_COUNT ; i++) {
		DBG_871X("%d, ", pwrctl->pnlo_info->ssid_cipher_info[i]);
	}
	DBG_871X("\n");

	DBG_871X("channel_info: ");
	for (i = 0 ; i < MAX_PNO_LIST_COUNT ; i++) {
		DBG_871X("%d, ", pwrctl->pnlo_info->ssid_channel_info[i]);
	}
	DBG_871X("\n");

	DBG_871X("******SSID_LISD******\n");
	for (i = 0 ; i < MAX_PNO_LIST_COUNT ; i++) {
		DBG_871X("[%d]SSID: %s \n", i,
			pwrctl->pno_ssid_list->node[i].SSID);
	}

	DBG_871X("******SCAN_INFO******\n");
	DBG_871X("ch_num: %d\n", pwrctl->pscan_info->channel_num);
	DBG_871X("orig_ch: %d\n", pwrctl->pscan_info->orig_ch);
	DBG_871X("orig bw: %d\n", pwrctl->pscan_info->orig_bw);
	DBG_871X("orig 40 offset: %d\n", pwrctl->pscan_info->orig_40_offset);
	for(i = 0 ; i < MAX_SCAN_LIST_COUNT ; i++) {
		DBG_871X("[%02d] avtive:%d, timeout:%d, tx_power:%d, ch:%02d\n",
			i, pwrctl->pscan_info->ssid_channel_info[i].active,
			pwrctl->pscan_info->ssid_channel_info[i].timeout,
			pwrctl->pscan_info->ssid_channel_info[i].tx_power,
			pwrctl->pscan_info->ssid_channel_info[i].channel);
	}
	DBG_871X("*****************\n");
}
#endif //CONFIG_PNO_SET_DEBUG
#endif //CONFIG_PNO_SUPPORT
