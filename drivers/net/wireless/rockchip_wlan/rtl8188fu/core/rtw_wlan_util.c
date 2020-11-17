/* SPDX-License-Identifier: GPL-2.0 */
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
#define _RTW_WLAN_UTIL_C_

#include <drv_types.h>
#include <hal_data.h>

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
	#include <linux/inetdevice.h>
	#define ETH_TYPE_OFFSET	12
	#define PROTOCOL_OFFSET	23
	#define IP_OFFSET	30
	#define IPv6_OFFSET	38
	#define IPv6_PROTOCOL_OFFSET	20
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

/* #define WAIT_FOR_BCN_TO_MIN	(3000) */
#define WAIT_FOR_BCN_TO_MIN	(6000)
#define WAIT_FOR_BCN_TO_MAX	(20000)

static u8 rtw_basic_rate_cck[4] = {
	IEEE80211_CCK_RATE_1MB | IEEE80211_BASIC_RATE_MASK, IEEE80211_CCK_RATE_2MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_CCK_RATE_5MB | IEEE80211_BASIC_RATE_MASK, IEEE80211_CCK_RATE_11MB | IEEE80211_BASIC_RATE_MASK
};

static u8 rtw_basic_rate_ofdm[3] = {
	IEEE80211_OFDM_RATE_6MB | IEEE80211_BASIC_RATE_MASK, IEEE80211_OFDM_RATE_12MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_OFDM_RATE_24MB | IEEE80211_BASIC_RATE_MASK
};

static u8 rtw_basic_rate_mix[7] = {
	IEEE80211_CCK_RATE_1MB | IEEE80211_BASIC_RATE_MASK, IEEE80211_CCK_RATE_2MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_CCK_RATE_5MB | IEEE80211_BASIC_RATE_MASK, IEEE80211_CCK_RATE_11MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_OFDM_RATE_6MB | IEEE80211_BASIC_RATE_MASK, IEEE80211_OFDM_RATE_12MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_OFDM_RATE_24MB | IEEE80211_BASIC_RATE_MASK
};

extern u8	WIFI_CCKRATES[];
bool rtw_is_cck_rate(u8 rate)
{
	int i;

	for (i = 0; i < 4; i++)
		if ((WIFI_CCKRATES[i] & 0x7F) == (rate & 0x7F))
			return 1;
	return 0;
}

extern u8	WIFI_OFDMRATES[];
bool rtw_is_ofdm_rate(u8 rate)
{
	int i;

	for (i = 0; i < 8; i++)
		if ((WIFI_OFDMRATES[i] & 0x7F) == (rate & 0x7F))
			return 1;
	return 0;
}

/* test if rate is defined in rtw_basic_rate_cck */
bool rtw_is_basic_rate_cck(u8 rate)
{
	int i;

	for (i = 0; i < 4; i++)
		if ((rtw_basic_rate_cck[i] & 0x7F) == (rate & 0x7F))
			return 1;
	return 0;
}

/* test if rate is defined in rtw_basic_rate_ofdm */
bool rtw_is_basic_rate_ofdm(u8 rate)
{
	int i;

	for (i = 0; i < 3; i++)
		if ((rtw_basic_rate_ofdm[i] & 0x7F) == (rate & 0x7F))
			return 1;
	return 0;
}

/* test if rate is defined in rtw_basic_rate_mix */
bool rtw_is_basic_rate_mix(u8 rate)
{
	int i;

	for (i = 0; i < 7; i++)
		if ((rtw_basic_rate_mix[i] & 0x7F) == (rate & 0x7F))
			return 1;
	return 0;
}
#ifdef CONFIG_BCN_CNT_CONFIRM_HDL
int new_bcn_max = 3;
#endif
int cckrates_included(unsigned char *rate, int ratelen)
{
	int	i;

	for (i = 0; i < ratelen; i++) {
		if ((((rate[i]) & 0x7f) == 2)	|| (((rate[i]) & 0x7f) == 4) ||
		    (((rate[i]) & 0x7f) == 11)  || (((rate[i]) & 0x7f) == 22))
			return _TRUE;
	}

	return _FALSE;

}

int cckratesonly_included(unsigned char *rate, int ratelen)
{
	int	i;

	for (i = 0; i < ratelen; i++) {
		if ((((rate[i]) & 0x7f) != 2) && (((rate[i]) & 0x7f) != 4) &&
		    (((rate[i]) & 0x7f) != 11)  && (((rate[i]) & 0x7f) != 22))
			return _FALSE;
	}

	return _TRUE;
}

s8 rtw_get_sta_rx_nss(_adapter *adapter, struct sta_info *psta)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u8 rf_type = RF_1T1R, custom_rf_type;
	s8 nss = 1;

	if (!psta)
		return nss;

	custom_rf_type = adapter->registrypriv.rf_config;
	rtw_hal_get_hwreg(adapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
	if (RF_TYPE_VALID(custom_rf_type))
		rf_type = custom_rf_type;

	nss = rtw_min(rf_type_to_rf_rx_cnt(rf_type), hal_spec->rx_nss_num);

#ifdef CONFIG_80211N_HT
	#ifdef CONFIG_80211AC_VHT
	if (psta->vhtpriv.vht_option)
		nss = rtw_min(nss, rtw_vht_mcsmap_to_nss(psta->vhtpriv.vht_mcs_map));
	else
	#endif /* CONFIG_80211AC_VHT */
	if (psta->htpriv.ht_option)
		nss = rtw_min(nss, rtw_ht_mcsset_to_nss(psta->htpriv.ht_cap.supp_mcs_set));
#endif /*CONFIG_80211N_HT*/
	RTW_INFO("%s: %d SS\n", __func__, nss);
	return nss;
}

s8 rtw_get_sta_tx_nss(_adapter *adapter, struct sta_info *psta)
{
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u8 rf_type = RF_1T1R, custom_rf_type;
	s8 nss = 1;

	if (!psta)
		return nss;

	custom_rf_type = adapter->registrypriv.rf_config;
	rtw_hal_get_hwreg(adapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
	if (RF_TYPE_VALID(custom_rf_type))
		rf_type = custom_rf_type;

	nss = rtw_min(rf_type_to_rf_tx_cnt(rf_type), hal_spec->tx_nss_num);

#ifdef CONFIG_80211N_HT
	#ifdef CONFIG_80211AC_VHT
	if (psta->vhtpriv.vht_option)
		nss = rtw_min(nss, rtw_vht_mcsmap_to_nss(psta->vhtpriv.vht_mcs_map));
	else
	#endif /* CONFIG_80211AC_VHT */
	if (psta->htpriv.ht_option)
		nss = rtw_min(nss, rtw_ht_mcsset_to_nss(psta->htpriv.ht_cap.supp_mcs_set));
#endif /*CONFIG_80211N_HT*/
	RTW_INFO("%s: %d SS\n", __func__, nss);
	return nss;
}

u8 judge_network_type(_adapter *padapter, unsigned char *rate, int ratelen)
{
	u8 network_type = 0;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);


	if (pmlmeext->cur_channel > 14) {
		if (pmlmeinfo->VHT_enable)
			network_type = WIRELESS_11AC;
		else if (pmlmeinfo->HT_enable)
			network_type = WIRELESS_11_5N;

		network_type |= WIRELESS_11A;
	} else {
		if (pmlmeinfo->HT_enable)
			network_type = WIRELESS_11_24N;

		if ((cckratesonly_included(rate, ratelen)) == _TRUE)
			network_type |= WIRELESS_11B;
		else if ((cckrates_included(rate, ratelen)) == _TRUE)
			network_type |= WIRELESS_11BG;
		else
			network_type |= WIRELESS_11G;
	}

	return	network_type;
}

unsigned char ratetbl_val_2wifirate(unsigned char rate);
unsigned char ratetbl_val_2wifirate(unsigned char rate)
{
	unsigned char val = 0;

	switch (rate & 0x7f) {
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

	for (i = 0; i < NumRates; i++) {
		val = pmlmeext->basicrate[i];

		if ((val != 0xff) && (val != 0xfe)) {
			if (rate == ratetbl_val_2wifirate(val))
				return _TRUE;
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

	for (i = 0; i < NumRates; i++) {
		rate = pmlmeext->datarate[i];

		if (rtw_get_oper_ch(padapter) > 14 && rate < _6M_RATE_) /*5G no support CCK rate*/
			continue;

		switch (rate) {
		case 0xff:
			return len;

		case 0xfe:
			continue;

		default:
			rate = ratetbl_val_2wifirate(rate);

			if (is_basicrate(padapter, rate) == _TRUE)
				rate |= IEEE80211_BASIC_RATE_MASK;

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
	u8 mcs_rate_1r = (u8)(mask & 0xff);
	u8 mcs_rate_2r = (u8)((mask >> 8) & 0xff);
	u8 mcs_rate_3r = (u8)((mask >> 16) & 0xff);
	u8 mcs_rate_4r = (u8)((mask >> 24) & 0xff);

	mcs_set[0] &= mcs_rate_1r;
	mcs_set[1] &= mcs_rate_2r;
	mcs_set[2] &= mcs_rate_3r;
	mcs_set[3] &= mcs_rate_4r;
}

void UpdateBrateTbl(
	PADAPTER		Adapter,
	u8			*mBratesOS
)
{
	u8	i;
	u8	rate;

	/* 1M, 2M, 5.5M, 11M, 6M, 12M, 24M are mandatory. */
	for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {
		rate = mBratesOS[i] & 0x7f;
		switch (rate) {
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

	for (i = 0; i < bssratelen; i++) {
		rate = bssrateset[i] & 0x7f;
		switch (rate) {
		case IEEE80211_CCK_RATE_1MB:
		case IEEE80211_CCK_RATE_2MB:
		case IEEE80211_CCK_RATE_5MB:
		case IEEE80211_CCK_RATE_11MB:
			bssrateset[i] |= IEEE80211_BASIC_RATE_MASK;
			break;
		}
	}

}
void Set_MSR(_adapter *padapter, u8 type)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_MEDIA_STATUS, (u8 *)(&type));
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
		cnt += snprintf(msg + cnt, len - cnt, "switch to ch %3u", ch);

		for (i = 0; i < dvobj->iface_nums; i++) {
			_adapter *iface = dvobj->padapters[i];
			cnt += snprintf(msg + cnt, len - cnt, " ["ADPT_FMT":", ADPT_ARG(iface));
			if (iface->mlmeextpriv.cur_channel == ch)
				cnt += snprintf(msg + cnt, len - cnt, "C");
			else
				cnt += snprintf(msg + cnt, len - cnt, "_");
			if (iface->wdinfo.listen_channel == ch && !rtw_p2p_chk_state(&iface->wdinfo, P2P_STATE_NONE))
				cnt += snprintf(msg + cnt, len - cnt, "L");
			else
				cnt += snprintf(msg + cnt, len - cnt, "_");
			cnt += snprintf(msg + cnt, len - cnt, "]");
		}

		RTW_INFO(FUNC_ADPT_FMT" %s\n", FUNC_ADPT_ARG(adapter), msg);
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

u8 rtw_get_offset_by_chbw(u8 ch, u8 bw, u8 *r_offset)
{
	u8 valid = 1;
	u8 offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;

	if (bw == CHANNEL_WIDTH_20)
		goto exit;

	if (bw >= CHANNEL_WIDTH_80 && ch <= 14) {
		valid = 0;
		goto exit;
	}

	if (ch >= 1 && ch <= 4)
		offset = HAL_PRIME_CHNL_OFFSET_LOWER;
	else if (ch >= 5 && ch <= 9) {
		if (*r_offset == HAL_PRIME_CHNL_OFFSET_LOWER || *r_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
			offset = *r_offset; /* both lower and upper is valid, obey input value */
		else
			offset = HAL_PRIME_CHNL_OFFSET_UPPER; /* default use upper */
	} else if (ch >= 10 && ch <= 13)
		offset = HAL_PRIME_CHNL_OFFSET_UPPER;
	else if (ch == 14) {
		valid = 0; /* ch14 doesn't support 40MHz bandwidth */
		goto exit;
	} else if (ch >= 36 && ch <= 177) {
		switch (ch) {
		case 36:
		case 44:
		case 52:
		case 60:
		case 100:
		case 108:
		case 116:
		case 124:
		case 132:
		case 140:
		case 149:
		case 157:
		case 165:
		case 173:
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
		case 144:
		case 153:
		case 161:
		case 169:
		case 177:
			offset = HAL_PRIME_CHNL_OFFSET_UPPER;
			break;
		default:
			valid = 0;
			break;
		}
	} else
		valid = 0;

exit:
	if (valid && r_offset)
		*r_offset = offset;
	return valid;
}

u8 rtw_get_center_ch(u8 channel, u8 chnl_bw, u8 chnl_offset)
{
	u8 center_ch = channel;

	if (chnl_bw == CHANNEL_WIDTH_80) {
		if (channel == 36 || channel == 40 || channel == 44 || channel == 48)
			center_ch = 42;
		else if (channel == 52 || channel == 56 || channel == 60 || channel == 64)
			center_ch = 58;
		else if (channel == 100 || channel == 104 || channel == 108 || channel == 112)
			center_ch = 106;
		else if (channel == 116 || channel == 120 || channel == 124 || channel == 128)
			center_ch = 122;
		else if (channel == 132 || channel == 136 || channel == 140 || channel == 144)
			center_ch = 138;
		else if (channel == 149 || channel == 153 || channel == 157 || channel == 161)
			center_ch = 155;
		else if (channel == 165 || channel == 169 || channel == 173 || channel == 177)
			center_ch = 171;
		else if (channel <= 14)
			center_ch = 7;
	} else if (chnl_bw == CHANNEL_WIDTH_40) {
		if (chnl_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
			center_ch = channel + 2;
		else
			center_ch = channel - 2;
	} else if (chnl_bw == CHANNEL_WIDTH_20)
		center_ch = channel;
	else
		rtw_warn_on(1);

	return center_ch;
}

inline systime rtw_get_on_oper_ch_time(_adapter *adapter)
{
	return adapter_to_dvobj(adapter)->on_oper_ch_time;
}

inline systime rtw_get_on_cur_ch_time(_adapter *adapter)
{
	if (adapter->mlmeextpriv.cur_channel == adapter_to_dvobj(adapter)->oper_channel)
		return adapter_to_dvobj(adapter)->on_oper_ch_time;
	else
		return 0;
}

void set_channel_bwmode(_adapter *padapter, unsigned char channel, unsigned char channel_offset, unsigned short bwmode)
{
	u8 center_ch, chnl_offset80 = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
#if (defined(CONFIG_TDLS) && defined(CONFIG_TDLS_CH_SW)) || defined(CONFIG_MCC_MODE)
	u8 iqk_info_backup = _FALSE;
#endif

	if (padapter->bNotifyChannelChange)
		RTW_INFO("[%s] ch = %d, offset = %d, bwmode = %d\n", __FUNCTION__, channel, channel_offset, bwmode);

	center_ch = rtw_get_center_ch(channel, bwmode, channel_offset);

	if (bwmode == CHANNEL_WIDTH_80) {
		if (center_ch > channel)
			chnl_offset80 = HAL_PRIME_CHNL_OFFSET_LOWER;
		else if (center_ch < channel)
			chnl_offset80 = HAL_PRIME_CHNL_OFFSET_UPPER;
		else
			chnl_offset80 = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	}
	_enter_critical_mutex(&(adapter_to_dvobj(padapter)->setch_mutex), NULL);

#ifdef CONFIG_MCC_MODE
	if (MCC_EN(padapter)) {
		/* driver doesn't set channel setting reg under MCC */
		if (rtw_hal_check_mcc_status(padapter, MCC_STATUS_DOING_MCC))
			RTW_INFO("Warning: Do not set channel setting reg MCC mode\n");
	}
#endif

#ifdef CONFIG_DFS_MASTER
	{
		struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
		bool ori_overlap_radar_detect_ch = rtw_rfctl_overlap_radar_detect_ch(rfctl);
		bool new_overlap_radar_detect_ch = _rtw_rfctl_overlap_radar_detect_ch(rfctl, channel, bwmode, channel_offset);

		if (new_overlap_radar_detect_ch && IS_CH_WAITING(rfctl)) {
			u8 pause = 0xFF;

			rtw_hal_set_hwreg(padapter, HW_VAR_TXPAUSE, &pause);
		}
#endif /* CONFIG_DFS_MASTER */

		/* set Channel */
		/* saved channel/bw info */
		rtw_set_oper_ch(padapter, channel);
		rtw_set_oper_bw(padapter, bwmode);
		rtw_set_oper_choffset(padapter, channel_offset);

#if (defined(CONFIG_TDLS) && defined(CONFIG_TDLS_CH_SW)) || defined(CONFIG_MCC_MODE)
		/* To check if we need to backup iqk info after switch chnl & bw */
		{
			u8 take_care_iqk, do_iqk;

			rtw_hal_get_hwreg(padapter, HW_VAR_CH_SW_NEED_TO_TAKE_CARE_IQK_INFO, &take_care_iqk);
			rtw_hal_get_hwreg(padapter, HW_VAR_DO_IQK, &do_iqk);
			if ((take_care_iqk == _TRUE) && (do_iqk == _TRUE))
				iqk_info_backup = _TRUE;
		}
#endif

		rtw_hal_set_chnl_bw(padapter, center_ch, bwmode, channel_offset, chnl_offset80); /* set center channel */

#if (defined(CONFIG_TDLS) && defined(CONFIG_TDLS_CH_SW)) || defined(CONFIG_MCC_MODE)
		if (iqk_info_backup == _TRUE)
			rtw_hal_ch_sw_iqk_info_backup(padapter);
#endif

#ifdef CONFIG_DFS_MASTER
		if (new_overlap_radar_detect_ch)
			rtw_odm_radar_detect_enable(padapter);
		else if (ori_overlap_radar_detect_ch) {
			u8 pause = 0x00;

			rtw_odm_radar_detect_disable(padapter);
			rtw_hal_set_hwreg(padapter, HW_VAR_TXPAUSE, &pause);
		}
	}
#endif /* CONFIG_DFS_MASTER */

	_exit_critical_mutex(&(adapter_to_dvobj(padapter)->setch_mutex), NULL);
}

__inline u8 *get_my_bssid(WLAN_BSSID_EX *pnetwork)
{
	return pnetwork->MacAddress;
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

	if (!padapter)
		return _FAIL;

	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &(pmlmeext->mlmext_info);

	if ((pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) && ((pmlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE))
		return _TRUE;
	else
		return _FAIL;
}

int is_client_associated_to_ibss(_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if ((pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) && ((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE))
		return _TRUE;
	else
		return _FAIL;
}

int is_IBSS_empty(_adapter *padapter)
{
	int i;
	struct macid_ctl_t *macid_ctl = &padapter->dvobj->macid_ctl;

	for (i = 0; i < macid_ctl->num; i++) {
		if (!rtw_macid_is_used(macid_ctl, i))
			continue;
		if (!rtw_macid_is_iface_specific(macid_ctl, i, padapter))
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
		return WAIT_FOR_BCN_TO_MIN;
	else if ((bcn_interval << 2) > WAIT_FOR_BCN_TO_MAX)
		return WAIT_FOR_BCN_TO_MAX;
	else
		return bcn_interval << 2;
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
	unsigned char null_key[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	rtw_sec_write_cam_ent(padapter, entry, 0, null_sta, null_key);
}

inline void write_cam(_adapter *adapter, u8 id, u16 ctrl, u8 *mac, u8 *key)
{
#ifdef CONFIG_WRITE_CACHE_ONLY
	write_cam_cache(adapter, id , ctrl, mac, key);
#else
	rtw_sec_write_cam_ent(adapter, id, ctrl, mac, key);
	write_cam_cache(adapter, id , ctrl, mac, key);
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
	RTW_PRINT_SEL(sel, "0x%08x\n", map->m0);
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 32)
	if (max_num && max_num > 32)
		RTW_PRINT_SEL(sel, "0x%08x\n", map->m1);
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 64)
	if (max_num && max_num > 64)
		RTW_PRINT_SEL(sel, "0x%08x\n", map->m2);
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 96)
	if (max_num && max_num > 96)
		RTW_PRINT_SEL(sel, "0x%08x\n", map->m3);
#endif
}

inline bool rtw_sec_camid_is_set(struct sec_cam_bmp *map, u8 id)
{
	if (id < 32)
		return map->m0 & BIT(id);
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 32)
	else if (id < 64)
		return map->m1 & BIT(id - 32);
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 64)
	else if (id < 96)
		return map->m2 & BIT(id - 64);
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 96)
	else if (id < 128)
		return map->m3 & BIT(id - 96);
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
		map->m1 &= ~BIT(id - 32);
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 64)
	else if (id < 96)
		map->m2 &= ~BIT(id - 64);
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 96)
	else if (id < 128)
		map->m3 &= ~BIT(id - 96);
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
		return forbid_map.m0 & BIT(id);
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 32)
	else if (id < 64)
		return forbid_map.m1 & BIT(id - 32);
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 64)
	else if (id < 96)
		return forbid_map.m2 & BIT(id - 64);
#endif
#if (SEC_CAM_ENT_NUM_SW_LIMIT > 96)
	else if (id < 128)
		return forbid_map.m3 & BIT(id - 96);
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
u8 rtw_get_sec_camid(_adapter *adapter, u8 max_bk_key_num, u8 *sec_key_id)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	int i;
	_irqL irqL;
	u8 sec_cam_num = 0;

	_enter_critical_bh(&cam_ctl->lock, &irqL);
	for (i = 0; i < cam_ctl->num; i++) {
		if (_rtw_sec_camid_is_used(cam_ctl, i)) {
			sec_key_id[sec_cam_num++] = i;
			if (sec_cam_num == max_bk_key_num)
				break;
		}
	}
	_exit_critical_bh(&cam_ctl->lock, &irqL);

	return sec_cam_num;
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

	ret = (dvobj->cam_cache[cam_id].ctrl & BIT6) ? _TRUE : _FALSE;

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
	if (kid >= 0 && kid != (dvobj->cam_cache[id].ctrl & 0x03))
		goto exit;
	if (gk != -1 && (gk ? _TRUE : _FALSE) != _rtw_camid_is_gk(adapter, id))
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
			RTW_INFO(FUNC_ADPT_FMT" addr:"MAC_FMT" kid:%d, gk:%d, return cam_id:%d\n"
				, FUNC_ADPT_ARG(adapter), MAC_ARG(addr), kid, gk, cam_id);
		else
			RTW_INFO(FUNC_ADPT_FMT" addr:%p kid:%d, gk:%d, return cam_id:%d\n"
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

s16 rtw_get_camid(_adapter *adapter, u8 *addr, s16 kid, u8 gk)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	int i;
#if 0 /* for testing */
	static u8 start_id = 0;
#else
	u8 start_id = 0;
#endif
	s16 cam_id = -1;

	if (addr == NULL) {
		RTW_PRINT(FUNC_ADPT_FMT" mac_address is NULL\n"
			  , FUNC_ADPT_ARG(adapter));
		rtw_warn_on(1);
		goto _exit;
	}

	/* find cam entry which has the same addr, kid (, gk bit) */
	if (_rtw_camctl_chk_cap(adapter, SEC_CAP_CHK_BMC) == _TRUE)
		i = _rtw_camid_search(adapter, addr, kid, gk);
	else
		i = _rtw_camid_search(adapter, addr, kid, -1);

	if (i >= 0) {
		cam_id = i;
		goto _exit;
	}

	for (i = 0; i < cam_ctl->num; i++) {
		/* bypass default key which is allocated statically */
#ifndef CONFIG_CONCURRENT_MODE
		if (((i + start_id) % cam_ctl->num) < 4)
			continue;
#endif
		if (_rtw_sec_camid_is_used(cam_ctl, ((i + start_id) % cam_ctl->num)) == _FALSE)
			break;
	}

	if (i == cam_ctl->num) {
		RTW_PRINT(FUNC_ADPT_FMT" %s key with "MAC_FMT" id:%u no room\n"
			, FUNC_ADPT_ARG(adapter), gk ? "group" : "pairwise", MAC_ARG(addr), kid);
		rtw_warn_on(1);
		goto _exit;
	}

	cam_id = ((i + start_id) % cam_ctl->num);
	start_id = ((i + start_id + 1) % cam_ctl->num);

_exit:
	return cam_id;
}

s16 rtw_camid_alloc(_adapter *adapter, struct sta_info *sta, u8 kid, u8 gk, bool *used)
{
	struct mlme_ext_info *mlmeinfo = &adapter->mlmeextpriv.mlmext_info;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	_irqL irqL;
	s16 cam_id = -1;

	*used = _FALSE;

	_enter_critical_bh(&cam_ctl->lock, &irqL);

	if ((((mlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) || ((mlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE))
	    && !sta) {
		/*
		* 1. non-STA mode WEP key
		* 2. group TX key
		*/
#ifndef CONFIG_CONCURRENT_MODE
		/* static alloction to default key by key ID when concurrent is not defined */
		if (kid > 3) {
			RTW_PRINT(FUNC_ADPT_FMT" group key with invalid key id:%u\n"
				  , FUNC_ADPT_ARG(adapter), kid);
			rtw_warn_on(1);
			goto bitmap_handle;
		}
		cam_id = kid;
#else
		u8 *addr = adapter_mac_addr(adapter);

		cam_id = rtw_get_camid(adapter, addr, kid, gk);
		if (1)
			RTW_PRINT(FUNC_ADPT_FMT" group key with "MAC_FMT" assigned cam_id:%u\n"
				, FUNC_ADPT_ARG(adapter), MAC_ARG(addr), cam_id);
#endif
	} else {
		/*
		* 1. STA mode WEP key
		* 2. STA mode group RX key
		* 3. sta key (pairwise, group RX)
		*/
		u8 *addr = sta ? sta->cmn.mac_addr : NULL;

		if (!sta) {
			if (!(mlmeinfo->state & WIFI_FW_ASSOC_SUCCESS)) {
				/* bypass STA mode group key setting before connected(ex:WEP) because bssid is not ready */
				goto bitmap_handle;
			}
			addr = get_bssid(&adapter->mlmepriv);/*A2*/
		}
		cam_id = rtw_get_camid(adapter, addr, kid, gk);
	}


bitmap_handle:
	if (cam_id >= 0) {
		*used = _rtw_sec_camid_is_used(cam_ctl, cam_id);
		rtw_sec_cam_map_set(&cam_ctl->used, cam_id);
	}

	_exit_critical_bh(&cam_ctl->lock, &irqL);

	return cam_id;
}

void rtw_camid_set(_adapter *adapter, u8 cam_id)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	_irqL irqL;

	_enter_critical_bh(&cam_ctl->lock, &irqL);

	if (cam_id < cam_ctl->num)
		rtw_sec_cam_map_set(&cam_ctl->used, cam_id);

	_exit_critical_bh(&cam_ctl->lock, &irqL);
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

/*Must pause TX/RX before use this API*/
inline void rtw_sec_cam_swap(_adapter *adapter, u8 cam_id_a, u8 cam_id_b)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	struct sec_cam_ent cache_a, cache_b;
	_irqL irqL;
	bool cam_a_used, cam_b_used;

	if (1)
		RTW_INFO(ADPT_FMT" - sec_cam %d,%d swap\n", ADPT_ARG(adapter), cam_id_a, cam_id_b);

	if (cam_id_a == cam_id_b)
		return;

#ifdef CONFIG_CONCURRENT_MODE
	rtw_mi_update_ap_bmc_camid(adapter, cam_id_a, cam_id_b);
#endif

	/*setp-1. backup org cam_info*/
	_enter_critical_bh(&cam_ctl->lock, &irqL);

	cam_a_used = _rtw_sec_camid_is_used(cam_ctl, cam_id_a);
	cam_b_used = _rtw_sec_camid_is_used(cam_ctl, cam_id_b);

	if (cam_a_used)
		_rtw_memcpy(&cache_a, &dvobj->cam_cache[cam_id_a], sizeof(struct sec_cam_ent));

	if (cam_b_used)
		_rtw_memcpy(&cache_b, &dvobj->cam_cache[cam_id_b], sizeof(struct sec_cam_ent));

	_exit_critical_bh(&cam_ctl->lock, &irqL);

	/*setp-2. clean cam_info*/
	if (cam_a_used) {
		rtw_camid_free(adapter, cam_id_a);
		clear_cam_entry(adapter, cam_id_a);
	}
	if (cam_b_used) {
		rtw_camid_free(adapter, cam_id_b);
		clear_cam_entry(adapter, cam_id_b);
	}

	/*setp-3. set cam_info*/
	if (cam_a_used) {
		write_cam(adapter, cam_id_b, cache_a.ctrl, cache_a.mac, cache_a.key);
		rtw_camid_set(adapter, cam_id_b);
	}

	if (cam_b_used) {
		write_cam(adapter, cam_id_a, cache_b.ctrl, cache_b.mac, cache_b.key);
		rtw_camid_set(adapter, cam_id_a);
	}
}

s16 rtw_get_empty_cam_entry(_adapter *adapter, u8 start_camid)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	_irqL irqL;
	int i;
	s16 cam_id = -1;

	_enter_critical_bh(&cam_ctl->lock, &irqL);
	for (i = start_camid; i < cam_ctl->num; i++) {
		if (_FALSE == _rtw_sec_camid_is_used(cam_ctl, i)) {
			cam_id = i;
			break;
		}
	}
	_exit_critical_bh(&cam_ctl->lock, &irqL);

	return cam_id;
}
void rtw_clean_dk_section(_adapter *adapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = dvobj_to_sec_camctl(dvobj);
	s16 ept_cam_id;
	int i;

	for (i = 0; i < 4; i++) {
		if (rtw_sec_camid_is_used(cam_ctl, i)) {
			ept_cam_id = rtw_get_empty_cam_entry(adapter, 4);
			if (ept_cam_id > 0)
				rtw_sec_cam_swap(adapter, i, ept_cam_id);
		}
	}
}
void rtw_clean_hw_dk_cam(_adapter *adapter)
{
	int i;

	for (i = 0; i < 4; i++)
		rtw_sec_clr_cam_ent(adapter, i);
		/*_clear_cam_entry(adapter, i);*/
}

void flush_all_cam_entry(_adapter *padapter)
{
#ifdef CONFIG_CONCURRENT_MODE
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct security_priv *psecpriv = &padapter->securitypriv;

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		struct sta_priv	*pstapriv = &padapter->stapriv;
		struct sta_info		*psta;

		psta = rtw_get_stainfo(pstapriv, pmlmeinfo->network.MacAddress);
		if (psta) {
			if (psta->state & WIFI_AP_STATE) {
				/*clear cam when ap free per sta_info*/
			} else
				rtw_clearstakey_cmd(padapter, psta, _FALSE);
		}
	} else if (MLME_IS_AP(padapter) || MLME_IS_MESH(padapter)) {
#if 1
		int cam_id = -1;
		u8 *addr = adapter_mac_addr(padapter);

		while ((cam_id = rtw_camid_search(padapter, addr, -1, -1)) >= 0) {
			RTW_PRINT("clear wep or group key for addr:"MAC_FMT", camid:%d\n", MAC_ARG(addr), cam_id);
			clear_cam_entry(padapter, cam_id);
			rtw_camid_free(padapter, cam_id);
		}
#else
		/* clear default key */
		int i, cam_id;
		u8 null_addr[ETH_ALEN] = {0, 0, 0, 0, 0, 0};

		for (i = 0; i < 4; i++) {
			cam_id = rtw_camid_search(padapter, null_addr, i, -1);
			if (cam_id >= 0) {
				clear_cam_entry(padapter, cam_id);
				rtw_camid_free(padapter, cam_id);
			}
		}
		/* clear default key related key search setting */
		rtw_hal_set_hwreg(padapter, HW_VAR_SEC_DK_CFG, (u8 *)_FALSE);
#endif
	}

#else /*NON CONFIG_CONCURRENT_MODE*/

	invalidate_cam_all(padapter);
	/* clear default key related key search setting */
	rtw_hal_set_hwreg(padapter, HW_VAR_SEC_DK_CFG, (u8 *)_FALSE);
#endif
}

#if defined(CONFIG_P2P) && defined(CONFIG_WFD)
void rtw_process_wfd_ie(_adapter *adapter, u8 *wfd_ie, u8 wfd_ielen, const char *tag)
{
	struct wifidirect_info *wdinfo = &adapter->wdinfo;

	u8 *attr_content;
	u32 attr_contentlen = 0;

	if (!hal_chk_wl_func(adapter, WL_FUNC_MIRACAST))
		return;

	RTW_INFO("[%s] Found WFD IE\n", tag);
	attr_content = rtw_get_wfd_attr_content(wfd_ie, wfd_ielen, WFD_ATTR_DEVICE_INFO, NULL, &attr_contentlen);
	if (attr_content && attr_contentlen) {
		wdinfo->wfd_info->peer_rtsp_ctrlport = RTW_GET_BE16(attr_content + 2);
		RTW_INFO("[%s] Peer PORT NUM = %d\n", tag, wdinfo->wfd_info->peer_rtsp_ctrlport);
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
	/* struct registry_priv	*pregpriv = &padapter->registrypriv; */
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (pmlmepriv->qospriv.qos_option == 0) {
		pmlmeinfo->WMM_enable = 0;
		return _FALSE;
	}

	if (_rtw_memcmp(&(pmlmeinfo->WMM_param), (pIE->data + 6), sizeof(struct WMM_para_element)))
		return _FALSE;
	else
		_rtw_memcpy(&(pmlmeinfo->WMM_param), (pIE->data + 6), sizeof(struct WMM_para_element));
	pmlmeinfo->WMM_enable = 1;
	return _TRUE;

#if 0
	if (pregpriv->wifi_spec == 1) {
		if (pmlmeinfo->WMM_enable == 1) {
			/* todo: compare the parameter set count & decide wheher to update or not */
			return _FAIL;
		} else {
			pmlmeinfo->WMM_enable = 1;
			_rtw_rtw_memcpy(&(pmlmeinfo->WMM_param), (pIE->data + 6), sizeof(struct WMM_para_element));
			return _TRUE;
		}
	} else {
		pmlmeinfo->WMM_enable = 0;
		return _FAIL;
	}
#endif

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
#ifdef CONFIG_WMMPS_STA
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct qos_priv	*pqospriv = &pmlmepriv->qospriv;
#endif /* CONFIG_WMMPS_STA */	

	acm_mask = 0;

	if (is_supported_5g(pmlmeext->cur_wireless_mode) ||
	    (pmlmeext->cur_wireless_mode & WIRELESS_11_24N))
		aSifsTime = 16;
	else
		aSifsTime = 10;

	if (pmlmeinfo->WMM_enable == 0) {
		padapter->mlmepriv.acm_mask = 0;

		AIFS = aSifsTime + (2 * pmlmeinfo->slotTime);

		if (pmlmeext->cur_wireless_mode & (WIRELESS_11G | WIRELESS_11A)) {
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
	} else {
		edca[0] = edca[1] = edca[2] = edca[3] = 0;

		for (i = 0; i < 4; i++) {
			ACI = (pmlmeinfo->WMM_param.ac_param[i].ACI_AIFSN >> 5) & 0x03;
			ACM = (pmlmeinfo->WMM_param.ac_param[i].ACI_AIFSN >> 4) & 0x01;

			/* AIFS = AIFSN * slot time + SIFS - r2t phy delay */
			AIFS = (pmlmeinfo->WMM_param.ac_param[i].ACI_AIFSN & 0x0f) * pmlmeinfo->slotTime + aSifsTime;

			ECWMin = (pmlmeinfo->WMM_param.ac_param[i].CW & 0x0f);
			ECWMax = (pmlmeinfo->WMM_param.ac_param[i].CW & 0xf0) >> 4;
			TXOP = le16_to_cpu(pmlmeinfo->WMM_param.ac_param[i].TXOP_limit);

			acParm = AIFS | (ECWMin << 8) | (ECWMax << 12) | (TXOP << 16);

			switch (ACI) {
			case 0x0:
				rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acParm));
				acm_mask |= (ACM ? BIT(1) : 0);
				edca[XMIT_BE_QUEUE] = acParm;
				break;

			case 0x1:
				rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acParm));
				/* acm_mask |= (ACM? BIT(0):0); */
				edca[XMIT_BK_QUEUE] = acParm;
				break;

			case 0x2:
				rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&acParm));
				acm_mask |= (ACM ? BIT(2) : 0);
				edca[XMIT_VI_QUEUE] = acParm;
				break;

			case 0x3:
				rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&acParm));
				acm_mask |= (ACM ? BIT(3) : 0);
				edca[XMIT_VO_QUEUE] = acParm;
				break;
			}

			RTW_INFO("WMM(%x): %x, %x\n", ACI, ACM, acParm);
		}

		if (padapter->registrypriv.acm_method == 1)
			rtw_hal_set_hwreg(padapter, HW_VAR_ACM_CTRL, (u8 *)(&acm_mask));
		else
			padapter->mlmepriv.acm_mask = acm_mask;

		inx[0] = 0;
		inx[1] = 1;
		inx[2] = 2;
		inx[3] = 3;

		if (pregpriv->wifi_spec == 1) {
			u32	j, tmp, change_inx = _FALSE;

			/* entry indx: 0->vo, 1->vi, 2->be, 3->bk. */
			for (i = 0; i < 4; i++) {
				for (j = i + 1; j < 4; j++) {
					/* compare CW and AIFS */
					if ((edca[j] & 0xFFFF) < (edca[i] & 0xFFFF))
						change_inx = _TRUE;
					else if ((edca[j] & 0xFFFF) == (edca[i] & 0xFFFF)) {
						/* compare TXOP */
						if ((edca[j] >> 16) > (edca[i] >> 16))
							change_inx = _TRUE;
					}

					if (change_inx) {
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

		for (i = 0; i < 4; i++) {
			pxmitpriv->wmm_para_seq[i] = inx[i];
			RTW_INFO("wmm_para_seq(%d): %d\n", i, pxmitpriv->wmm_para_seq[i]);
		}
		
#ifdef CONFIG_WMMPS_STA
		/* if AP supports UAPSD function, driver must set each uapsd TID to coresponding mac register 0x693 */
		if (pmlmeinfo->WMM_param.QoS_info & AP_SUPPORTED_UAPSD) {
			pqospriv->uapsd_ap_supported = 1;
			rtw_hal_set_hwreg(padapter, HW_VAR_UAPSD_TID, NULL);
		}
#endif /* CONFIG_WMMPS_STA */
	}
}

static void bwmode_update_check(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
#ifdef CONFIG_80211N_HT
	unsigned char	 new_bwmode;
	unsigned char  new_ch_offset;
	struct HT_info_element	*pHT_info;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct ht_priv			*phtpriv = &pmlmepriv->htpriv;
	u8	cbw40_enable = 0;

	if (!pIE)
		return;

	if (phtpriv->ht_option == _FALSE)
		return;

	if (pmlmeext->cur_bwmode >= CHANNEL_WIDTH_80)
		return;

	if (pIE->Length > sizeof(struct HT_info_element))
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

	if ((pHT_info->infos[0] & BIT(2)) && cbw40_enable) {
		new_bwmode = CHANNEL_WIDTH_40;

		switch (pHT_info->infos[0] & 0x3) {
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
	} else {
		new_bwmode = CHANNEL_WIDTH_20;
		new_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	}


	if ((new_bwmode != pmlmeext->cur_bwmode || new_ch_offset != pmlmeext->cur_ch_offset)
	    && new_bwmode < pmlmeext->cur_bwmode
	   ) {
		pmlmeinfo->bwmode_updated = _TRUE;

		pmlmeext->cur_bwmode = new_bwmode;
		pmlmeext->cur_ch_offset = new_ch_offset;

		/* update HT info also */
		HT_info_handler(padapter, pIE);
	} else
		pmlmeinfo->bwmode_updated = _FALSE;


	if (_TRUE == pmlmeinfo->bwmode_updated) {
		struct sta_info *psta;
		WLAN_BSSID_EX	*cur_network = &(pmlmeinfo->network);
		struct sta_priv	*pstapriv = &padapter->stapriv;

		/* set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode); */


		/* update ap's stainfo */
		psta = rtw_get_stainfo(pstapriv, cur_network->MacAddress);
		if (psta) {
			struct ht_priv	*phtpriv_sta = &psta->htpriv;

			if (phtpriv_sta->ht_option) {
				/* bwmode				 */
				psta->cmn.bw_mode = pmlmeext->cur_bwmode;
				phtpriv_sta->ch_offset = pmlmeext->cur_ch_offset;
			} else {
				psta->cmn.bw_mode = CHANNEL_WIDTH_20;
				phtpriv_sta->ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			}

			rtw_dm_ra_mask_wk_cmd(padapter, (u8 *)psta);
		}

		/* pmlmeinfo->bwmode_updated = _FALSE; */ /* bwmode_updated done, reset it! */
	}
#endif /* CONFIG_80211N_HT */
}

#ifdef ROKU_PRIVATE
void Supported_rate_infra_ap(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
	unsigned int	i;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info		*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (pIE == NULL)
		return;

	for (i = 0 ; i < pIE->Length; i++)
		pmlmeinfo->SupportedRates_infra_ap[i] = (pIE->data[i]);

}

void Extended_Supported_rate_infra_ap(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
	unsigned int i, j;
	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info		*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (pIE == NULL)
		return;

	if (pIE->Length > 0) {
		for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {
			if (pmlmeinfo->SupportedRates_infra_ap[i] == 0)
				break;
		}
		for (j = 0; j < pIE->Length; j++)
			pmlmeinfo->SupportedRates_infra_ap[i+j] = (pIE->data[j]);
	}

}

void HT_get_ss_from_mcs_set(u8 *mcs_set, u8 *Rx_ss)
{
	u8 i, j;
	u8 r_ss = 0, t_ss = 0;

	for (i = 0; i < 4; i++) {
		if ((mcs_set[3-i] & 0xff) != 0x00) {
			r_ss = 4-i;
			break;
		}
	}

	*Rx_ss = r_ss;
}

void HT_caps_handler_infra_ap(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
	unsigned int	i;
	u8	cur_stbc_cap_infra_ap = 0;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv_infra_ap		*phtpriv = &pmlmepriv->htpriv_infra_ap;

	struct mlme_ext_priv		*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info		*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (pIE == NULL)
		return;

	pmlmeinfo->ht_vht_received |= BIT(0);

	/*copy MCS_SET*/
	for (i = 3; i < 19; i++)
		phtpriv->MCS_set_infra_ap[i-3] = (pIE->data[i]);

	/*get number of stream from mcs set*/
	HT_get_ss_from_mcs_set(phtpriv->MCS_set_infra_ap, &phtpriv->Rx_ss_infra_ap);

	phtpriv->rx_highest_data_rate_infra_ap = le16_to_cpu(GET_HT_CAP_ELE_RX_HIGHEST_DATA_RATE(pIE->data));

	phtpriv->ldpc_cap_infra_ap = GET_HT_CAP_ELE_LDPC_CAP(pIE->data);

	if (GET_HT_CAP_ELE_RX_STBC(pIE->data))
		SET_FLAG(cur_stbc_cap_infra_ap, STBC_HT_ENABLE_RX);
	if (GET_HT_CAP_ELE_TX_STBC(pIE->data))
		SET_FLAG(cur_stbc_cap_infra_ap, STBC_HT_ENABLE_TX);
	phtpriv->stbc_cap_infra_ap = cur_stbc_cap_infra_ap;

	/*store ap info SGI 20m 40m*/
	phtpriv->sgi_20m_infra_ap = GET_HT_CAP_ELE_SHORT_GI20M(pIE->data);
	phtpriv->sgi_40m_infra_ap = GET_HT_CAP_ELE_SHORT_GI40M(pIE->data);

	/*store ap info for supported channel bandwidth*/
	phtpriv->channel_width_infra_ap = GET_HT_CAP_ELE_CHL_WIDTH(pIE->data);
}
#endif /* ROKU_PRIVATE */

void HT_caps_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
#ifdef CONFIG_80211N_HT
	unsigned int	i;
	u8	rf_type = RF_1T1R;
	u8	max_AMPDU_len, min_MPDU_spacing;
	u8	cur_ldpc_cap = 0, cur_stbc_cap = 0, cur_beamform_cap = 0, tx_nss = 0;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv			*phtpriv = &pmlmepriv->htpriv;
#ifdef CONFIG_DISABLE_MCS13TO15
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;
#endif
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(padapter);

	if (pIE == NULL)
		return;

	if (phtpriv->ht_option == _FALSE)
		return;

	pmlmeinfo->HT_caps_enable = 1;

	for (i = 0; i < (pIE->Length); i++) {
		if (i != 2) {
			/*	Commented by Albert 2010/07/12 */
			/*	Got the endian issue here. */
			pmlmeinfo->HT_caps.u.HT_cap[i] &= (pIE->data[i]);
		} else {
			/* AMPDU Parameters field */

			/* Get MIN of MAX AMPDU Length Exp */
			if ((pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x3) > (pIE->data[i] & 0x3))
				max_AMPDU_len = (pIE->data[i] & 0x3);
			else
				max_AMPDU_len = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x3);

			/* Get MAX of MIN MPDU Start Spacing */
			if ((pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x1c) > (pIE->data[i] & 0x1c))
				min_MPDU_spacing = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x1c);
			else
				min_MPDU_spacing = (pIE->data[i] & 0x1c);

			pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para = max_AMPDU_len | min_MPDU_spacing;
		}
	}

	/*	Commented by Albert 2010/07/12 */
	/*	Have to handle the endian issue after copying. */
	/*	HT_ext_caps didn't be used yet.	 */
	pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info = le16_to_cpu(pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info);
	pmlmeinfo->HT_caps.u.HT_cap_element.HT_ext_caps = le16_to_cpu(pmlmeinfo->HT_caps.u.HT_cap_element.HT_ext_caps);

	/* update the MCS set */
	for (i = 0; i < 16; i++)
		pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate[i] &= pmlmeext->default_supported_mcs_set[i];

	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
	tx_nss = rtw_min(rf_type_to_rf_tx_cnt(rf_type), hal_spec->tx_nss_num);

	switch (tx_nss) {
	case 1:
		set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_1R);
		break;
	case 2:
		#ifdef CONFIG_DISABLE_MCS13TO15
		if (pmlmeext->cur_bwmode == CHANNEL_WIDTH_40 && pregistrypriv->wifi_spec != 1)
			set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_2R_13TO15_OFF);
		else
		#endif
			set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_2R);
		break;
	case 3:
		set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_3R);
		break;
	case 4:
		set_mcs_rate_by_mask(pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate, MCS_RATE_4R);
		break;
	default:
		RTW_WARN("rf_type:%d or tx_nss:%u is not expected\n", rf_type, hal_spec->tx_nss_num);
	}

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		/* Config STBC setting */
		if (TEST_FLAG(phtpriv->stbc_cap, STBC_HT_ENABLE_TX) && GET_HT_CAP_ELE_RX_STBC(pIE->data)) {
			SET_FLAG(cur_stbc_cap, STBC_HT_ENABLE_TX);
			RTW_INFO("Enable HT Tx STBC !\n");
		}
		phtpriv->stbc_cap = cur_stbc_cap;

#ifdef CONFIG_BEAMFORMING
		/* Config Tx beamforming setting */
		if (TEST_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE) &&
		    GET_HT_CAP_TXBF_EXPLICIT_COMP_STEERING_CAP(pIE->data)) {
			SET_FLAG(cur_beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE);
			/* Shift to BEAMFORMING_HT_BEAMFORMEE_CHNL_EST_CAP*/
			SET_FLAG(cur_beamform_cap, GET_HT_CAP_TXBF_CHNL_ESTIMATION_NUM_ANTENNAS(pIE->data) << 6);
		}

		if (TEST_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE) &&
		    GET_HT_CAP_TXBF_EXPLICIT_COMP_FEEDBACK_CAP(pIE->data)) {
			SET_FLAG(cur_beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE);
			/* Shift to BEAMFORMING_HT_BEAMFORMER_STEER_NUM*/
			SET_FLAG(cur_beamform_cap, GET_HT_CAP_TXBF_COMP_STEERING_NUM_ANTENNAS(pIE->data) << 4);
		}
		phtpriv->beamform_cap = cur_beamform_cap;
		if (cur_beamform_cap)
			RTW_INFO("AP HT Beamforming Cap = 0x%02X\n", cur_beamform_cap);
#endif /*CONFIG_BEAMFORMING*/
	} else {
		/*WIFI_STATION_STATEorI_ADHOC_STATE or WIFI_ADHOC_MASTER_STATE*/
		/* Config LDPC Coding Capability */
		if (TEST_FLAG(phtpriv->ldpc_cap, LDPC_HT_ENABLE_TX) && GET_HT_CAP_ELE_LDPC_CAP(pIE->data)) {
			SET_FLAG(cur_ldpc_cap, (LDPC_HT_ENABLE_TX | LDPC_HT_CAP_TX));
			RTW_INFO("Enable HT Tx LDPC!\n");
		}
		phtpriv->ldpc_cap = cur_ldpc_cap;

		/* Config STBC setting */
		if (TEST_FLAG(phtpriv->stbc_cap, STBC_HT_ENABLE_TX) && GET_HT_CAP_ELE_RX_STBC(pIE->data)) {
			SET_FLAG(cur_stbc_cap, (STBC_HT_ENABLE_TX | STBC_HT_CAP_TX));
			RTW_INFO("Enable HT Tx STBC!\n");
		}
		phtpriv->stbc_cap = cur_stbc_cap;

#ifdef CONFIG_BEAMFORMING
#ifdef RTW_BEAMFORMING_VERSION_2
		/* Config beamforming setting */
		if (TEST_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE) &&
		    GET_HT_CAP_TXBF_EXPLICIT_COMP_STEERING_CAP(pIE->data)) {
			SET_FLAG(cur_beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE);
			/* Shift to BEAMFORMING_HT_BEAMFORMEE_CHNL_EST_CAP*/
			SET_FLAG(cur_beamform_cap, GET_HT_CAP_TXBF_CHNL_ESTIMATION_NUM_ANTENNAS(pIE->data) << 6);
		}

		if (TEST_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE) &&
		    GET_HT_CAP_TXBF_EXPLICIT_COMP_FEEDBACK_CAP(pIE->data)) {
			SET_FLAG(cur_beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE);
			/* Shift to BEAMFORMING_HT_BEAMFORMER_STEER_NUM*/
			SET_FLAG(cur_beamform_cap, GET_HT_CAP_TXBF_COMP_STEERING_NUM_ANTENNAS(pIE->data) << 4);
		}
#else /* !RTW_BEAMFORMING_VERSION_2 */
		/* Config Tx beamforming setting */
		if (TEST_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE) &&
		    GET_HT_CAP_TXBF_EXPLICIT_COMP_STEERING_CAP(pIE->data)) {
			SET_FLAG(cur_beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE);
			/* Shift to BEAMFORMING_HT_BEAMFORMEE_CHNL_EST_CAP*/
			SET_FLAG(cur_beamform_cap, GET_HT_CAP_TXBF_CHNL_ESTIMATION_NUM_ANTENNAS(pIE->data) << 6);
		}

		if (TEST_FLAG(phtpriv->beamform_cap, BEAMFORMING_HT_BEAMFORMER_ENABLE) &&
		    GET_HT_CAP_TXBF_EXPLICIT_COMP_FEEDBACK_CAP(pIE->data)) {
			SET_FLAG(cur_beamform_cap, BEAMFORMING_HT_BEAMFORMEE_ENABLE);
			/* Shift to BEAMFORMING_HT_BEAMFORMER_STEER_NUM*/
			SET_FLAG(cur_beamform_cap, GET_HT_CAP_TXBF_COMP_STEERING_NUM_ANTENNAS(pIE->data) << 4);
		}
#endif /* !RTW_BEAMFORMING_VERSION_2 */
		phtpriv->beamform_cap = cur_beamform_cap;
		if (cur_beamform_cap)
			RTW_INFO("Client HT Beamforming Cap = 0x%02X\n", cur_beamform_cap);
#endif /*CONFIG_BEAMFORMING*/
	}

#endif /* CONFIG_80211N_HT */
}

void HT_info_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
#ifdef CONFIG_80211N_HT
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv			*phtpriv = &pmlmepriv->htpriv;

	if (pIE == NULL)
		return;

	if (phtpriv->ht_option == _FALSE)
		return;


	if (pIE->Length > sizeof(struct HT_info_element))
		return;

	pmlmeinfo->HT_info_enable = 1;
	_rtw_memcpy(&(pmlmeinfo->HT_info), pIE->data, pIE->Length);
#endif /* CONFIG_80211N_HT */
	return;
}

void HTOnAssocRsp(_adapter *padapter)
{
	unsigned char		max_AMPDU_len;
	unsigned char		min_MPDU_spacing;
	/* struct registry_priv	 *pregpriv = &padapter->registrypriv; */
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	RTW_INFO("%s\n", __FUNCTION__);

	if ((pmlmeinfo->HT_info_enable) && (pmlmeinfo->HT_caps_enable))
		pmlmeinfo->HT_enable = 1;
	else {
		pmlmeinfo->HT_enable = 0;
		/* set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode); */
		return;
	}

	/* handle A-MPDU parameter field */
	/*
		AMPDU_para [1:0]:Max AMPDU Len => 0:8k , 1:16k, 2:32k, 3:64k
		AMPDU_para [4:2]:Min MPDU Start Spacing
	*/
	max_AMPDU_len = pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x03;

	min_MPDU_spacing = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x1c) >> 2;

	rtw_hal_set_hwreg(padapter, HW_VAR_AMPDU_MIN_SPACE, (u8 *)(&min_MPDU_spacing));
#ifdef CONFIG_80211N_HT
	rtw_hal_set_hwreg(padapter, HW_VAR_AMPDU_FACTOR, (u8 *)(&max_AMPDU_len));
#endif /* CONFIG_80211N_HT */
#if 0 /* move to rtw_update_ht_cap() */
	if ((pregpriv->bw_mode > 0) &&
	    (pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info & BIT(1)) &&
	    (pmlmeinfo->HT_info.infos[0] & BIT(2))) {
		/* switch to the 40M Hz mode accoring to the AP */
		pmlmeext->cur_bwmode = CHANNEL_WIDTH_40;
		switch ((pmlmeinfo->HT_info.infos[0] & 0x3)) {
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

	/* set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode); */

#if 0 /* move to rtw_update_ht_cap() */
	/*  */
	/* Config SM Power Save setting */
	/*  */
	pmlmeinfo->SM_PS = (pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info & 0x0C) >> 2;
	if (pmlmeinfo->SM_PS == WLAN_HT_CAP_SM_PS_STATIC) {
#if 0
		u8 i;
		/* update the MCS rates */
		for (i = 0; i < 16; i++)
			pmlmeinfo->HT_caps.HT_cap_element.MCS_rate[i] &= MCS_rate_1R[i];
#endif
		RTW_INFO("%s(): WLAN_HT_CAP_SM_PS_STATIC\n", __FUNCTION__);
	}

	/*  */
	/* Config current HT Protection mode. */
	/*  */
	pmlmeinfo->HT_protection = pmlmeinfo->HT_info.infos[1] & 0x3;
#endif

}

void ERP_IE_handler(_adapter *padapter, PNDIS_802_11_VARIABLE_IEs pIE)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (pIE->Length > 1)
		return;

	pmlmeinfo->ERP_enable = 1;
	_rtw_memcpy(&(pmlmeinfo->ERP_IE), pIE->data, pIE->Length);
}

void VCS_update(_adapter *padapter, struct sta_info *psta)
{
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	switch (pregpriv->vrtl_carrier_sense) { /* 0:off 1:on 2:auto */
	case 0: /* off */
		psta->rtsen = 0;
		psta->cts2self = 0;
		break;

	case 1: /* on */
		if (pregpriv->vcs_type == 1) { /* 1:RTS/CTS 2:CTS to self */
			psta->rtsen = 1;
			psta->cts2self = 0;
		} else {
			psta->rtsen = 0;
			psta->cts2self = 1;
		}
		break;

	case 2: /* auto */
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
		if (TEST_FLAG(psta->vhtpriv.ldpc_cap, LDPC_VHT_ENABLE_TX))
			psta->cmn.ldpc_en = VHT_LDPC_EN;
		else
			psta->cmn.ldpc_en = 0;

		if (TEST_FLAG(psta->vhtpriv.stbc_cap, STBC_VHT_ENABLE_TX))
			psta->cmn.stbc_en = VHT_STBC_EN;
		else
			psta->cmn.stbc_en = 0;
	} else
#endif /* CONFIG_80211AC_VHT */
		if (psta->htpriv.ht_option) {
			if (TEST_FLAG(psta->htpriv.ldpc_cap, LDPC_HT_ENABLE_TX))
				psta->cmn.ldpc_en = HT_LDPC_EN;
			else
				psta->cmn.ldpc_en = 0;

			if (TEST_FLAG(psta->htpriv.stbc_cap, STBC_HT_ENABLE_TX))
				psta->cmn.stbc_en = HT_STBC_EN;
			else
				psta->cmn.stbc_en = 0;
		} else {
			psta->cmn.ldpc_en = 0;
			psta->cmn.stbc_en = 0;
		}

#endif /* CONFIG_80211N_HT */
}

int check_ielen(u8 *start, uint len)
{
	int left = len;
	u8 *pos = start;
	u8 id, elen;

	while (left >= 2) {
		id = *pos++;
		elen = *pos++;
		left -= 2;

		if (elen > left) {
			RTW_INFO("IEEE 802.11 element parse failed (id=%d elen=%d left=%lu)\n",
					id, elen, (unsigned long) left);
			return _FALSE;
		}
		if ((id == WLAN_EID_VENDOR_SPECIFIC) && (elen < 3))
				return _FALSE;

		left -= elen;
		pos += elen;
	}
	if (left)
		return _FALSE;

	return _TRUE;
}

int validate_beacon_len(u8 *pframe, u32 len)
{
	u8 ie_offset = _BEACON_IE_OFFSET_ + sizeof(struct rtw_ieee80211_hdr_3addr);

	if (len < ie_offset) {
		RTW_INFO("%s: incorrect beacon length(%d)\n", __func__, len);
		return _FALSE;
	}

	if (check_ielen(pframe + ie_offset, len - ie_offset) == _FALSE)
		return _FALSE;

	return _TRUE;
}


u8 support_rate_ranges[] = {
	IEEE80211_CCK_RATE_1MB,
	IEEE80211_CCK_RATE_2MB,
	IEEE80211_CCK_RATE_5MB,
	IEEE80211_CCK_RATE_11MB,
	IEEE80211_OFDM_RATE_6MB,
	IEEE80211_OFDM_RATE_9MB,
	IEEE80211_OFDM_RATE_12MB,
	IEEE80211_OFDM_RATE_18MB,
	IEEE80211_OFDM_RATE_24MB,
	IEEE80211_OFDM_RATE_36MB,
	IEEE80211_OFDM_RATE_48MB,
	IEEE80211_OFDM_RATE_54MB,
};

inline bool match_ranges(u16 EID, u32 value)
{
	int i;
	int nr_range;

	switch (EID) {
	case _EXT_SUPPORTEDRATES_IE_:
	case _SUPPORTEDRATES_IE_:
		nr_range = sizeof(support_rate_ranges)/sizeof(u8);
		for (i = 0; i < nr_range; i++) {
			/*	clear bit7 before searching.	*/
			value &= ~BIT(7);
			if (value == support_rate_ranges[i])
				return _TRUE;
		}
		break;
	default:
		break;
	};
	return _FALSE;
}

/*
 * rtw_validate_value: validate the IE contain.
 *
 *	Input : 
 *		EID : Element ID
 *		p	: IE buffer (without EID & length)
 *		len	: IE length
 *	return: 
 * 		_TRUE	: All Values are validated.
 *		_FALSE	: At least one value is NOT validated.
 */
bool rtw_validate_value(u16 EID, u8 *p, u16 len)
{
	u8 rate;
	u32 i, nr_val;

	switch (EID) {
	case _EXT_SUPPORTEDRATES_IE_:
	case _SUPPORTEDRATES_IE_:
		nr_val = len;
		for (i=0; i<nr_val; i++) {
			rate = *(p+i);
			if (match_ranges(EID, rate) == _FALSE)
				return _FALSE;
		}
		break;
	default:
		break;
	};
	return _TRUE;
}

inline bool hidden_ssid_ap(WLAN_BSSID_EX *snetwork)
{
	return ((snetwork->Ssid.SsidLength == 0) ||  
		is_all_null(snetwork->Ssid.Ssid, snetwork->Ssid.SsidLength) == _TRUE);
}

/*
	Get SSID if this ilegal frame(probe resp) comes from a hidden SSID AP.
	Update the SSID to the corresponding pnetwork in scan queue.
*/
void rtw_absorb_ssid_ifneed(_adapter *padapter, WLAN_BSSID_EX *bssid, u8 *pframe)
{
	struct wlan_network *scanned = NULL;
	WLAN_BSSID_EX	*snetwork;
	u8 ie_offset, *p=NULL, *next_ie=NULL, *mac = get_addr2_ptr(pframe);
	sint len, ssid_len_ori;
	u32 remain_len = 0;
	u8 backupIE[MAX_IE_SZ];
	u16 subtype = get_frame_sub_type(pframe);
	_irqL irqL;

	if ((!bssid) || (!pframe))
		return;

	if (subtype == WIFI_BEACON) {
		bssid->Reserved[0] = BSS_TYPE_BCN;
		ie_offset = _BEACON_IE_OFFSET_;
	} else {
		/* FIXME : more type */
		if (subtype == WIFI_PROBERSP) {
			ie_offset = _PROBERSP_IE_OFFSET_;
			bssid->Reserved[0] = BSS_TYPE_PROB_RSP;
		} else if (subtype == WIFI_PROBEREQ) {
			ie_offset = _PROBEREQ_IE_OFFSET_;
			bssid->Reserved[0] = BSS_TYPE_PROB_REQ;
		} else {
			bssid->Reserved[0] = BSS_TYPE_UNDEF;
			ie_offset = _FIXED_IE_LENGTH_;
		}
	}
	
	_enter_critical_bh(&padapter->mlmepriv.scanned_queue.lock, &irqL);
	scanned = _rtw_find_network(&padapter->mlmepriv.scanned_queue, mac);
	if (!scanned) {
		_exit_critical_bh(&padapter->mlmepriv.scanned_queue.lock, &irqL);
		return;
	}

	snetwork = &(scanned->network);
	/* scan queue records as Hidden SSID && Input frame is NOT Hidden SSID	*/
	if (hidden_ssid_ap(snetwork) && !hidden_ssid_ap(bssid)) {
		p = rtw_get_ie(snetwork->IEs+ie_offset, _SSID_IE_, &ssid_len_ori, snetwork->IELength-ie_offset);
		if (!p) {
			_exit_critical_bh(&padapter->mlmepriv.scanned_queue.lock, &irqL);
			return;
		}
		next_ie = p + 2 + ssid_len_ori;
		remain_len = snetwork->IELength - (next_ie - snetwork->IEs);
		scanned->network.Ssid.SsidLength = bssid->Ssid.SsidLength;
		_rtw_memcpy(scanned->network.Ssid.Ssid, bssid->Ssid.Ssid, bssid->Ssid.SsidLength);

		//update pnetwork->ssid, pnetwork->ssidlen
		_rtw_memcpy(backupIE, next_ie, remain_len);
		*(p+1) = bssid->Ssid.SsidLength;
		_rtw_memcpy(p+2, bssid->Ssid.Ssid, bssid->Ssid.SsidLength);
		_rtw_memcpy(p+2+bssid->Ssid.SsidLength, backupIE, remain_len);
		snetwork->IELength += bssid->Ssid.SsidLength;
	}
	_exit_critical_bh(&padapter->mlmepriv.scanned_queue.lock, &irqL);
}

#ifdef DBG_RX_BCN
void rtw_debug_rx_bcn(_adapter *adapter, u8 *pframe, u32 packet_len)
{
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *mlmeinfo = &(pmlmeext->mlmext_info);
	u16 sn = ((struct rtw_ieee80211_hdr_3addr *)pframe)->seq_ctl >> 4;
	u64 tsf, tsf_offset;
	u8 dtim_cnt, dtim_period, tim_bmap, tim_pvbit;

	update_TSF(pmlmeext, pframe, packet_len);
	tsf = pmlmeext->TSFValue;
	tsf_offset = rtw_modular64(pmlmeext->TSFValue, (mlmeinfo->bcn_interval * 1024));

	/*get TIM IE*/
	/*DTIM Count*/
	dtim_cnt = pmlmeext->tim[0];
	/*DTIM Period*/
	dtim_period = pmlmeext->tim[1];
	/*Bitmap*/
	tim_bmap = pmlmeext->tim[2];
	/*Partial VBitmap AID 0 ~ 7*/
	tim_pvbit = pmlmeext->tim[3];

	RTW_INFO("[BCN] SN-%d, TSF-%lld(us), offset-%lld, bcn_interval-%d DTIM-%d[%d] bitmap-0x%02x-0x%02x\n",
		sn, tsf, tsf_offset, mlmeinfo->bcn_interval, dtim_period, dtim_cnt, tim_bmap, tim_pvbit);
}
#endif

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
		/* RTW_INFO("Oops: %s we don't find channel IE, so don't check it\n", __func__); */
		recv_beacon->bcn_channel = Adapter->mlmeextpriv.cur_channel;
	}

	/* checking SSID */
	if (elems.ssid) {
		if (elems.ssid_len > sizeof(recv_beacon->ssid))
			return _FALSE;

		_rtw_memcpy(recv_beacon->ssid, elems.ssid, elems.ssid_len);
		recv_beacon->ssid_len = elems.ssid_len;
	} else
		; /* means hidden ssid */

	/* checking RSN first */
	if (elems.rsn_ie && elems.rsn_ie_len) {
		recv_beacon->encryp_protocol = ENCRYP_PROTOCOL_WPA2;
		rtw_parse_wpa2_ie(elems.rsn_ie - 2, elems.rsn_ie_len + 2,
			&recv_beacon->group_cipher, &recv_beacon->pairwise_cipher,
				  &recv_beacon->akm, NULL);
	}
	/* checking WPA secon */
	else if (elems.wpa_ie && elems.wpa_ie_len) {
		recv_beacon->encryp_protocol = ENCRYP_PROTOCOL_WPA;
		rtw_parse_wpa_ie(elems.wpa_ie - 2, elems.wpa_ie_len + 2,
			&recv_beacon->group_cipher, &recv_beacon->pairwise_cipher,
				 &recv_beacon->akm);
	} else if (capability & BIT(4))
		recv_beacon->encryp_protocol = ENCRYP_PROTOCOL_WEP;

	if (elems.tim && elems.tim_len) {
		struct mlme_ext_priv *pmlmeext = &Adapter->mlmeextpriv;

		#ifdef DBG_RX_BCN
		_rtw_memcpy(pmlmeext->tim, elems.tim, 4);
		#endif
		pmlmeext->dtim = elems.tim[1];
	}

	return _TRUE;
}

void rtw_dump_bcn_keys(struct beacon_keys *recv_beacon)
{
	u8 ssid[IW_ESSID_MAX_SIZE + 1];

	_rtw_memcpy(ssid, recv_beacon->ssid, recv_beacon->ssid_len);
	ssid[recv_beacon->ssid_len] = '\0';

	RTW_INFO("%s: ssid = %s\n", __func__, ssid);
	RTW_INFO("%s: channel = %d\n", __func__, recv_beacon->bcn_channel);
	RTW_INFO("%s: ht_cap = 0x%04x\n", __func__,	recv_beacon->ht_cap_info);
	RTW_INFO("%s: ht_info_infos_0_sco = 0x%02x\n", __func__, recv_beacon->ht_info_infos_0_sco);
	RTW_INFO("%s: sec=%d, group = %x, pair = %x, akm = %x\n", __func__,
		 recv_beacon->encryp_protocol, recv_beacon->group_cipher,
		 recv_beacon->pairwise_cipher, recv_beacon->akm);
}
#define DBG_BCN_CNT
int rtw_check_bcn_info(ADAPTER *Adapter, u8 *pframe, u32 packet_len)
{
	unsigned int len;
	u8 *pbssid = GetAddr3Ptr(pframe);
	struct mlme_priv *pmlmepriv = &Adapter->mlmepriv;
	struct wlan_network *cur_network = &(Adapter->mlmepriv.cur_network);
	struct beacon_keys recv_beacon;

	if (is_client_associated_to_ap(Adapter) == _FALSE)
		return _TRUE;

	len = packet_len - sizeof(struct rtw_ieee80211_hdr_3addr);

	if (len > MAX_IE_SZ) {
		RTW_WARN("%s IE too long for survey event\n", __func__);
		return _FAIL;
	}

	if (_rtw_memcmp(cur_network->network.MacAddress, pbssid, 6) == _FALSE) {
		RTW_WARN("Oops: rtw_check_network_encrypt linked but recv other bssid bcn\n" MAC_FMT MAC_FMT,
			MAC_ARG(pbssid), MAC_ARG(cur_network->network.MacAddress));
		return _TRUE;
	}

	if (rtw_get_bcn_keys(Adapter, pframe, packet_len, &recv_beacon) == _FALSE)
		return _TRUE; /* parsing failed => broken IE */

#ifdef DBG_RX_BCN
	rtw_debug_bcn(Adapter, pframe, packet_len);
#endif

	/* don't care hidden ssid, use current beacon ssid directly */
	if (recv_beacon.ssid_len == 0) {
		_rtw_memcpy(recv_beacon.ssid, pmlmepriv->cur_beacon_keys.ssid,
			    pmlmepriv->cur_beacon_keys.ssid_len);
		recv_beacon.ssid_len = pmlmepriv->cur_beacon_keys.ssid_len;
	}
#ifdef CONFIG_BCN_CNT_CONFIRM_HDL
	if (_rtw_memcmp(&recv_beacon, &pmlmepriv->cur_beacon_keys, sizeof(recv_beacon)) == _TRUE)
		pmlmepriv->new_beacon_cnts = 0;
	else if ((pmlmepriv->new_beacon_cnts == 0) ||
		_rtw_memcmp(&recv_beacon, &pmlmepriv->new_beacon_keys, sizeof(recv_beacon)) == _FALSE) {
		RTW_DBG("%s: start new beacon (seq=%d)\n", __func__, GetSequence(pframe));

		if (pmlmepriv->new_beacon_cnts == 0) {
			RTW_ERR("%s: cur beacon key\n", __func__);
			RTW_DBG_EXPR(rtw_dump_bcn_keys(&pmlmepriv->cur_beacon_keys));
		}

		RTW_DBG("%s: new beacon key\n", __func__);
		RTW_DBG_EXPR(rtw_dump_bcn_keys(&recv_beacon));

		_rtw_memcpy(&pmlmepriv->new_beacon_keys, &recv_beacon, sizeof(recv_beacon));
		pmlmepriv->new_beacon_cnts = 1;
	} else {
		RTW_DBG("%s: new beacon again (seq=%d)\n", __func__, GetSequence(pframe));
		pmlmepriv->new_beacon_cnts++;
	}

	/* if counter >= max, it means beacon is changed really */
	if (pmlmepriv->new_beacon_cnts >= new_bcn_max)
#else
	if (_rtw_memcmp(&recv_beacon, &pmlmepriv->cur_beacon_keys, sizeof(recv_beacon)) == _FALSE)
#endif
	{
		/* check bw mode change only? */
		pmlmepriv->cur_beacon_keys.ht_cap_info = recv_beacon.ht_cap_info;
		pmlmepriv->cur_beacon_keys.ht_info_infos_0_sco = recv_beacon.ht_info_infos_0_sco;
		if (_rtw_memcmp(&recv_beacon, &pmlmepriv->cur_beacon_keys,
				sizeof(recv_beacon)) == _FALSE) {
			/* beacon is changed, have to do disconnect/connect */
			RTW_WARN("%s: new beacon occur!!\n", __func__);
			#ifdef DBG_BCN_CNT
			rtw_dump_bcn_keys(&recv_beacon);
			#endif
			return _FAIL;
		}
		#ifdef DBG_BCN_CNT
		RTW_INFO("%s bw mode change\n", __func__);
		RTW_INFO("%s bcn now: ht_cap_info:%x ht_info_infos_0:%x\n", __func__,
			 cur_network->BcnInfo.ht_cap_info,
			 cur_network->BcnInfo.ht_info_infos_0);
		#endif

		cur_network->BcnInfo.ht_cap_info = recv_beacon.ht_cap_info;
		cur_network->BcnInfo.ht_info_infos_0 =
			(cur_network->BcnInfo.ht_info_infos_0 & (~0x03)) |
			recv_beacon.ht_info_infos_0_sco;

		#ifdef DBG_BCN_CNT
		RTW_INFO("%s bcn link: ht_cap_info:%x ht_info_infos_0:%x\n", __func__,
			 cur_network->BcnInfo.ht_cap_info,
			 cur_network->BcnInfo.ht_info_infos_0);
		#endif
		_rtw_memcpy(&pmlmepriv->cur_beacon_keys, &recv_beacon, sizeof(recv_beacon));
		#ifdef CONFIG_BCN_CNT_CONFIRM_HDL
		pmlmepriv->new_beacon_cnts = 0;
		#endif
	}

	return _SUCCESS;
}

void update_beacon_info(_adapter *padapter, u8 *pframe, uint pkt_len, struct sta_info *psta)
{
	unsigned int i;
	unsigned int len;
	PNDIS_802_11_VARIABLE_IEs	pIE;

#ifdef CONFIG_TDLS
	struct tdls_info *ptdlsinfo = &padapter->tdlsinfo;
	u8 tdls_prohibited[] = { 0x00, 0x00, 0x00, 0x00, 0x10 }; /* bit(38): TDLS_prohibited */
#endif /* CONFIG_TDLS */

	len = pkt_len - (_BEACON_IE_OFFSET_ + WLAN_HDR_A3_LEN);

	for (i = 0; i < len;) {
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pframe + (_BEACON_IE_OFFSET_ + WLAN_HDR_A3_LEN) + i);

		switch (pIE->ElementID) {
		case _VENDOR_SPECIFIC_IE_:
			/* to update WMM paramter set while receiving beacon */
			if (_rtw_memcmp(pIE->data, WMM_PARA_OUI, 6) && pIE->Length == WLAN_WMM_LEN)	/* WMM */
				(WMM_param_handler(padapter, pIE)) ? report_wmm_edca_update(padapter) : 0;

			break;

		case _HT_EXTRA_INFO_IE_:	/* HT info */
			/* HT_info_handler(padapter, pIE); */
			bwmode_update_check(padapter, pIE);
			break;
#ifdef CONFIG_80211AC_VHT
		case EID_OpModeNotification:
			rtw_process_vht_op_mode_notify(padapter, pIE->data, psta);
			break;
#endif /* CONFIG_80211AC_VHT */
		case _ERPINFO_IE_:
			ERP_IE_handler(padapter, pIE);
			VCS_update(padapter, psta);
			break;

#ifdef CONFIG_TDLS
		case _EXT_CAP_IE_:
			if (check_ap_tdls_prohibited(pIE->data, pIE->Length) == _TRUE)
				ptdlsinfo->ap_prohibited = _TRUE;
			if (check_ap_tdls_ch_switching_prohibited(pIE->data, pIE->Length) == _TRUE)
				ptdlsinfo->ch_switch_prohibited = _TRUE;
			break;
#endif /* CONFIG_TDLS */
		default:
			break;
		}

		i += (pIE->Length + 2);
	}
}

#ifdef CONFIG_DFS
void process_csa_ie(_adapter *padapter, u8 *ies, uint ies_len)
{
	struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
	unsigned int i;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	u8 ch = 0;

	/* TODO: compare with scheduling CSA */
	if (rfctl->csa_ch)
		return;

	for (i = 0; i + 1 < ies_len;) {
		pIE = (PNDIS_802_11_VARIABLE_IEs)(ies + i);

		switch (pIE->ElementID) {
		case _CH_SWTICH_ANNOUNCE_:
			ch = *(pIE->data + 1);
			break;
		default:
			break;
		}

		i += (pIE->Length + 2);
	}

	if (ch != 0) {
		rfctl->csa_ch = ch;
		if (rtw_set_csa_cmd(padapter) != _SUCCESS)
			rfctl->csa_ch = 0;
	}
}
#endif /* CONFIG_DFS */

void parsing_eapol_packet(_adapter *padapter, u8 *key_payload, struct sta_info *psta, u8 trx_type)
{
	struct security_priv *psecuritypriv = &(padapter->securitypriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &(padapter->stapriv);
	struct ieee802_1x_hdr *hdr;
	struct wpa_eapol_key *key;
	u16 key_info, key_data_length;
	char *trx_msg = trx_type ? "send" : "recv";

	hdr = (struct ieee802_1x_hdr *) key_payload;

	 /* WPS - eapol start packet */
	if (hdr->type == 1 && hdr->length == 0) {
		RTW_INFO("%s eapol start packet\n", trx_msg);
		return;
	}

	if (hdr->type == 0) { /* WPS - eapol packet */
		RTW_INFO("%s eapol packet\n", trx_msg);
		return;
	}

	key = (struct wpa_eapol_key *) (hdr + 1);
	key_info = be16_to_cpu(*((u16 *)(key->key_info)));
	key_data_length = be16_to_cpu(*((u16 *)(key->key_data_length)));

	if (!(key_info & WPA_KEY_INFO_KEY_TYPE)) { /* WPA group key handshake */
		if (key_info & WPA_KEY_INFO_ACK) {
			RTW_PRINT("%s eapol packet - WPA Group Key 1/2\n", trx_msg);
		} else {
			RTW_PRINT("%s eapol packet - WPA Group Key 2/2\n", trx_msg);

			/* WPA key-handshake has completed */
			if (psecuritypriv->ndisauthtype == Ndis802_11AuthModeWPAPSK)
				psta->state &= (~WIFI_UNDER_KEY_HANDSHAKE);
		}
	} else if (key_info & WPA_KEY_INFO_MIC) {
		if (key_data_length == 0)
			RTW_PRINT("%s eapol packet 4/4\n", trx_msg);
		else if (key_info & WPA_KEY_INFO_ACK)
			RTW_PRINT("%s eapol packet 3/4\n", trx_msg);
		else
			RTW_PRINT("%s eapol packet 2/4\n", trx_msg);
	} else {
		RTW_PRINT("%s eapol packet 1/4\n", trx_msg);
	}

}

unsigned int is_ap_in_tkip(_adapter *padapter)
{
	u32 i;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*cur_network = &(pmlmeinfo->network);

	if (rtw_get_capability((WLAN_BSSID_EX *)cur_network) & WLAN_CAPABILITY_PRIVACY) {
		for (i = sizeof(NDIS_802_11_FIXED_IEs); i < pmlmeinfo->network.IELength;) {
			pIE = (PNDIS_802_11_VARIABLE_IEs)(pmlmeinfo->network.IEs + i);

			switch (pIE->ElementID) {
			case _VENDOR_SPECIFIC_IE_:
				if ((_rtw_memcmp(pIE->data, RTW_WPA_OUI, 4)) && (_rtw_memcmp((pIE->data + 12), WPA_TKIP_CIPHER, 4)))
					return _TRUE;
				break;

			case _RSN_IE_2_:
				if (_rtw_memcmp((pIE->data + 8), RSN_TKIP_CIPHER, 4))
					return _TRUE;

			default:
				break;
			}

			i += (pIE->Length + 2);
		}

		return _FALSE;
	} else
		return _FALSE;

}

unsigned int should_forbid_n_rate(_adapter *padapter)
{
	u32 i;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	WLAN_BSSID_EX  *cur_network = &pmlmepriv->cur_network.network;

	if (rtw_get_capability((WLAN_BSSID_EX *)cur_network) & WLAN_CAPABILITY_PRIVACY) {
		for (i = sizeof(NDIS_802_11_FIXED_IEs); i < cur_network->IELength;) {
			pIE = (PNDIS_802_11_VARIABLE_IEs)(cur_network->IEs + i);

			switch (pIE->ElementID) {
			case _VENDOR_SPECIFIC_IE_:
				if (_rtw_memcmp(pIE->data, RTW_WPA_OUI, 4) &&
				    ((_rtw_memcmp((pIE->data + 12), WPA_CIPHER_SUITE_CCMP, 4)) ||
				     (_rtw_memcmp((pIE->data + 16), WPA_CIPHER_SUITE_CCMP, 4))))
					return _FALSE;
				break;

			case _RSN_IE_2_:
				if ((_rtw_memcmp((pIE->data + 8), RSN_CIPHER_SUITE_CCMP, 4))  ||
				    (_rtw_memcmp((pIE->data + 12), RSN_CIPHER_SUITE_CCMP, 4)))
					return _FALSE;

			default:
				break;
			}

			i += (pIE->Length + 2);
		}

		return _TRUE;
	} else
		return _FALSE;

}


unsigned int is_ap_in_wep(_adapter *padapter)
{
	u32 i;
	PNDIS_802_11_VARIABLE_IEs	pIE;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*cur_network = &(pmlmeinfo->network);

	if (rtw_get_capability((WLAN_BSSID_EX *)cur_network) & WLAN_CAPABILITY_PRIVACY) {
		for (i = sizeof(NDIS_802_11_FIXED_IEs); i < pmlmeinfo->network.IELength;) {
			pIE = (PNDIS_802_11_VARIABLE_IEs)(pmlmeinfo->network.IEs + i);

			switch (pIE->ElementID) {
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
	} else
		return _FALSE;

}

int wifirate2_ratetbl_inx(unsigned char rate);
int wifirate2_ratetbl_inx(unsigned char rate)
{
	int	inx = 0;
	rate = rate & 0x7f;

	switch (rate) {
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

	num_of_rate = (ptn_sz > NumRates) ? NumRates : ptn_sz;

	for (i = 0; i < num_of_rate; i++) {
		if ((*(ptn + i)) & 0x80)
			mask |= 0x1 << wifirate2_ratetbl_inx(*(ptn + i));
	}
	return mask;
}

unsigned int update_supported_rate(unsigned char *ptn, unsigned int ptn_sz)
{
	unsigned int i, num_of_rate;
	unsigned int mask = 0;

	num_of_rate = (ptn_sz > NumRates) ? NumRates : ptn_sz;

	for (i = 0; i < num_of_rate; i++)
		mask |= 0x1 << wifirate2_ratetbl_inx(*(ptn + i));

	return mask;
}

int support_short_GI(_adapter *padapter, struct HT_caps_element *pHT_caps, u8 bwmode)
{
	unsigned char					bit_offset;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (!(pmlmeinfo->HT_enable))
		return _FAIL;

	bit_offset = (bwmode & CHANNEL_WIDTH_40) ? 6 : 5;

	if (pHT_caps->u.HT_cap_element.HT_caps_info & (0x1 << bit_offset))
		return _SUCCESS;
	else
		return _FAIL;
}

unsigned char get_highest_rate_idx(u64 mask)
{
	int i;
	unsigned char rate_idx = 0;

	for (i = 63; i >= 0; i--) {
		if ((mask >> i) & 0x01) {
			rate_idx = i;
			break;
		}
	}

	return rate_idx;
}
unsigned char get_lowest_rate_idx_ex(u64 mask, int start_bit)
{
	int i;
	unsigned char rate_idx = 0;

	for (i = start_bit; i < 64; i++) {
		if ((mask >> i) & 0x01) {
			rate_idx = i;
			break;
		}
	}

	return rate_idx;
}

void Update_RA_Entry(_adapter *padapter, struct sta_info *psta)
{
	rtw_hal_update_ra_mask(psta);
}

void set_sta_rate(_adapter *padapter, struct sta_info *psta)
{
	/* rate adaptive	 */
	rtw_hal_update_ra_mask(psta);
}

/* Update RRSR and Rate for USERATE */
void update_tx_basic_rate(_adapter *padapter, u8 wirelessmode)
{
	NDIS_802_11_RATES_EX	supported_rates;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &padapter->wdinfo;

	/*	Added by Albert 2011/03/22 */
	/*	In the P2P mode, the driver should not support the b mode. */
	/*	So, the Tx packet shouldn't use the CCK rate */
	if (!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		return;
#endif /* CONFIG_P2P */

	_rtw_memset(supported_rates, 0, NDIS_802_11_LENGTH_RATES_EX);

	/* clear B mod if current channel is in 5G band, avoid tx cck rate in 5G band. */
	if (pmlmeext->cur_channel > 14)
		wirelessmode &= ~(WIRELESS_11B);

	if ((wirelessmode & WIRELESS_11B) && (wirelessmode == WIRELESS_11B))
		_rtw_memcpy(supported_rates, rtw_basic_rate_cck, 4);
	else if (wirelessmode & WIRELESS_11B)
		_rtw_memcpy(supported_rates, rtw_basic_rate_mix, 7);
	else
		_rtw_memcpy(supported_rates, rtw_basic_rate_ofdm, 3);

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

	for (i = sizeof(NDIS_802_11_FIXED_IEs); i < len;) {
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pframe + i);

		switch (pIE->ElementID) {
		case _VENDOR_SPECIFIC_IE_:
			if ((_rtw_memcmp(pIE->data, ARTHEROS_OUI1, 3)) || (_rtw_memcmp(pIE->data, ARTHEROS_OUI2, 3))) {
				RTW_INFO("link to Artheros AP\n");
				return HT_IOT_PEER_ATHEROS;
			} else if ((_rtw_memcmp(pIE->data, BROADCOM_OUI1, 3))
				   || (_rtw_memcmp(pIE->data, BROADCOM_OUI2, 3))
				|| (_rtw_memcmp(pIE->data, BROADCOM_OUI3, 3))) {
				RTW_INFO("link to Broadcom AP\n");
				return HT_IOT_PEER_BROADCOM;
			} else if (_rtw_memcmp(pIE->data, MARVELL_OUI, 3)) {
				RTW_INFO("link to Marvell AP\n");
				return HT_IOT_PEER_MARVELL;
			} else if (_rtw_memcmp(pIE->data, RALINK_OUI, 3)) {
				RTW_INFO("link to Ralink AP\n");
				return HT_IOT_PEER_RALINK;
			} else if (_rtw_memcmp(pIE->data, CISCO_OUI, 3)) {
				RTW_INFO("link to Cisco AP\n");
				return HT_IOT_PEER_CISCO;
			} else if (_rtw_memcmp(pIE->data, REALTEK_OUI, 3)) {
				u32	Vender = HT_IOT_PEER_REALTEK;

				if (pIE->Length >= 5) {
					if (pIE->data[4] == 1) {
						/* if(pIE->data[5] & RT_HT_CAP_USE_LONG_PREAMBLE) */
						/*	bssDesc->BssHT.RT2RT_HT_Mode |= RT_HT_CAP_USE_LONG_PREAMBLE; */

						if (pIE->data[5] & RT_HT_CAP_USE_92SE) {
							/* bssDesc->BssHT.RT2RT_HT_Mode |= RT_HT_CAP_USE_92SE; */
							Vender = HT_IOT_PEER_REALTEK_92SE;
						}
					}

					if (pIE->data[5] & RT_HT_CAP_USE_SOFTAP)
						Vender = HT_IOT_PEER_REALTEK_SOFTAP;

					if (pIE->data[4] == 2) {
						if (pIE->data[6] & RT_HT_CAP_USE_JAGUAR_BCUT) {
							Vender = HT_IOT_PEER_REALTEK_JAGUAR_BCUTAP;
							RTW_INFO("link to Realtek JAGUAR_BCUTAP\n");
						}
						if (pIE->data[6] & RT_HT_CAP_USE_JAGUAR_CCUT) {
							Vender = HT_IOT_PEER_REALTEK_JAGUAR_CCUTAP;
							RTW_INFO("link to Realtek JAGUAR_CCUTAP\n");
						}
					}
				}

				RTW_INFO("link to Realtek AP\n");
				return Vender;
			} else if (_rtw_memcmp(pIE->data, AIRGOCAP_OUI, 3)) {
				RTW_INFO("link to Airgo Cap\n");
				return HT_IOT_PEER_AIRGO;
			} else
				break;

		default:
			break;
		}

		i += (pIE->Length + 2);
	}

	RTW_INFO("link to new AP\n");
	return HT_IOT_PEER_UNKNOWN;
}

void get_assoc_AP_Vendor(char *vendor, u8 assoc_AP_vendor)
{
	switch (assoc_AP_vendor) {
	
	case HT_IOT_PEER_UNKNOWN:
	sprintf(vendor, "%s", "unknown");
	break;

	case HT_IOT_PEER_REALTEK:
	case HT_IOT_PEER_REALTEK_92SE:
	case HT_IOT_PEER_REALTEK_SOFTAP:
	case HT_IOT_PEER_REALTEK_JAGUAR_BCUTAP:
	case HT_IOT_PEER_REALTEK_JAGUAR_CCUTAP:

	sprintf(vendor, "%s", "Realtek");
	break;

	case HT_IOT_PEER_BROADCOM:
	sprintf(vendor, "%s", "Broadcom");
	break;

	case HT_IOT_PEER_MARVELL:
	sprintf(vendor, "%s", "Marvell");
	break;

	case HT_IOT_PEER_RALINK:
	sprintf(vendor, "%s", "Ralink");
	break;

	case HT_IOT_PEER_CISCO:
	sprintf(vendor, "%s", "Cisco");
	break;

	case HT_IOT_PEER_AIRGO:
	sprintf(vendor, "%s", "Airgo");
	break;

	case HT_IOT_PEER_ATHEROS:
	sprintf(vendor, "%s", "Atheros");
	break;

	default:
	sprintf(vendor, "%s", "unkown");
	break;
	}

}
#ifdef CONFIG_RTS_FULL_BW
void rtw_parse_sta_vendor_ie_8812(_adapter *adapter, struct sta_info *sta, u8 *tlv_ies, u16 tlv_ies_len)
{
	struct mlme_priv *mlme = &adapter->mlmepriv;
	unsigned char REALTEK_OUI[] = {0x00,0xe0, 0x4c};
	u8 *p;
	int i;

	p = rtw_get_ie_ex(tlv_ies, tlv_ies_len, WLAN_EID_VENDOR_SPECIFIC, REALTEK_OUI, 3, NULL, NULL);
	if (!p)
		goto exit;
	else {
		if(*(p+1) > 6 ) {

			if(*(p+6) != 2)
				goto exit;
			
			if(*(p+8) == RT_HT_CAP_USE_JAGUAR_BCUT)
				sta->vendor_8812 = TRUE;
			else if (*(p+8) == RT_HT_CAP_USE_JAGUAR_CCUT)
				sta->vendor_8812 = TRUE;
		}
	}
exit:
	return;
}
#endif/*CONFIG_RTS_FULL_BW*/

#ifdef CONFIG_80211AC_VHT
unsigned char get_vht_mu_bfer_cap(u8 *pframe, uint len)
{
	unsigned int i;
	unsigned int mu_bfer=0;
	PNDIS_802_11_VARIABLE_IEs pIE;

	for (i = sizeof(NDIS_802_11_FIXED_IEs); i < len;) {
		pIE = (PNDIS_802_11_VARIABLE_IEs)(pframe + i);

		switch (pIE->ElementID) {

		case EID_VHTCapability:
			mu_bfer = GET_VHT_CAPABILITY_ELE_MU_BFER(pIE->data);
			break;
		default:
			break;
		}
		i += (pIE->Length + 2);
	}
	return mu_bfer;
}
#endif

void update_capinfo(PADAPTER Adapter, u16 updateCap)
{
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	BOOLEAN		ShortPreamble;

	/* Check preamble mode, 2005.01.06, by rcnjko. */
	/* Mark to update preamble value forever, 2008.03.18 by lanhsin */
	/* if( pMgntInfo->RegPreambleMode == PREAMBLE_AUTO ) */
	{

		if (updateCap & cShortPreamble) {
			/* Short Preamble */
			if (pmlmeinfo->preamble_mode != PREAMBLE_SHORT) { /* PREAMBLE_LONG or PREAMBLE_AUTO */
				ShortPreamble = _TRUE;
				pmlmeinfo->preamble_mode = PREAMBLE_SHORT;
				rtw_hal_set_hwreg(Adapter, HW_VAR_ACK_PREAMBLE, (u8 *)&ShortPreamble);
			}
		} else {
			/* Long Preamble */
			if (pmlmeinfo->preamble_mode != PREAMBLE_LONG) { /* PREAMBLE_SHORT or PREAMBLE_AUTO */
				ShortPreamble = _FALSE;
				pmlmeinfo->preamble_mode = PREAMBLE_LONG;
				rtw_hal_set_hwreg(Adapter, HW_VAR_ACK_PREAMBLE, (u8 *)&ShortPreamble);
			}
		}
	}

	if (updateCap & cIBSS) {
		/* Filen: See 802.11-2007 p.91 */
		pmlmeinfo->slotTime = NON_SHORT_SLOT_TIME;
	} else {
		/* Filen: See 802.11-2007 p.90 */
		if (pmlmeext->cur_wireless_mode & (WIRELESS_11_24N | WIRELESS_11A | WIRELESS_11_5N | WIRELESS_11AC))
			pmlmeinfo->slotTime = SHORT_SLOT_TIME;
		else if (pmlmeext->cur_wireless_mode & (WIRELESS_11G)) {
			if ((updateCap & cShortSlotTime) /* && (!(pMgntInfo->pHTInfo->RT2RT_HT_Mode & RT_HT_CAP_USE_LONG_PREAMBLE)) */) {
				/* Short Slot Time */
				pmlmeinfo->slotTime = SHORT_SLOT_TIME;
			} else {
				/* Long Slot Time */
				pmlmeinfo->slotTime = NON_SHORT_SLOT_TIME;
			}
		} else {
			/* B Mode */
			pmlmeinfo->slotTime = NON_SHORT_SLOT_TIME;
		}
	}

	rtw_hal_set_hwreg(Adapter, HW_VAR_SLOT_TIME, &pmlmeinfo->slotTime);

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
	WLAN_BSSID_EX		*cur_network = &(pmlmeinfo->network);
	unsigned char			*rate = cur_network->SupportedRates;
#ifdef CONFIG_P2P
	struct wifidirect_info	*pwdinfo = &(padapter->wdinfo);
#endif /* CONFIG_P2P */

	ratelen = rtw_get_rateset_len(cur_network->SupportedRates);

	if ((pmlmeinfo->HT_info_enable) && (pmlmeinfo->HT_caps_enable))
		pmlmeinfo->HT_enable = 1;

	if (pmlmeext->cur_channel > 14) {
		if (pmlmeinfo->VHT_enable)
			network_type = WIRELESS_11AC;
		else if (pmlmeinfo->HT_enable)
			network_type = WIRELESS_11_5N;

		network_type |= WIRELESS_11A;
	} else {
		if (pmlmeinfo->VHT_enable)
			network_type = WIRELESS_11AC;
		else if (pmlmeinfo->HT_enable)
			network_type = WIRELESS_11_24N;

		if ((cckratesonly_included(rate, ratelen)) == _TRUE)
			network_type |= WIRELESS_11B;
		else if ((cckrates_included(rate, ratelen)) == _TRUE)
			network_type |= WIRELESS_11BG;
		else
			network_type |= WIRELESS_11G;
	}

	pmlmeext->cur_wireless_mode = network_type & padapter->registrypriv.wireless_mode;
	/* RTW_INFO("network_type=%02x, padapter->registrypriv.wireless_mode=%02x\n", network_type, padapter->registrypriv.wireless_mode); */

#ifndef RTW_HALMAC
	/* HALMAC IC do not set HW_VAR_RESP_SIFS here */
#if 0
	if ((pmlmeext->cur_wireless_mode == WIRELESS_11G) ||
	    (pmlmeext->cur_wireless_mode == WIRELESS_11BG)) /* WIRELESS_MODE_G) */
		SIFS_Timer = 0x0a0a;/* CCK */
	else
		SIFS_Timer = 0x0e0e;/* pHalData->SifsTime; //OFDM */
#endif

	SIFS_Timer = 0x0a0a0808; /* 0x0808->for CCK, 0x0a0a->for OFDM
                              * change this value if having IOT issues. */

	rtw_hal_set_hwreg(padapter, HW_VAR_RESP_SIFS, (u8 *)&SIFS_Timer);
#endif

	rtw_hal_set_hwreg(padapter, HW_VAR_WIRELESS_MODE, (u8 *)&(pmlmeext->cur_wireless_mode));

	if ((pmlmeext->cur_wireless_mode & WIRELESS_11B)
		#ifdef CONFIG_P2P
		&& (rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE)
			#ifdef CONFIG_IOCTL_CFG80211
			|| !rtw_cfg80211_iface_has_p2p_group_cap(padapter)
			#endif
			)
		#endif
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

	ph2c = (struct cmd_obj *)rtw_zmalloc(sizeof(struct cmd_obj));
	if (ph2c == NULL)
		return;

	pwriteMacPara = (struct reg_rw_parm *)rtw_malloc(sizeof(struct reg_rw_parm));
	if (pwriteMacPara == NULL) {
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
	if (IsSupportedTxCCK(wireless_mode)) {
		/* Only B, B/G, and B/G/N AP could use CCK rate */
		_rtw_memcpy(psta->bssrateset, rtw_basic_rate_cck, 4);
		psta->bssratelen = 4;
	} else {
		_rtw_memcpy(psta->bssrateset, rtw_basic_rate_ofdm, 3);
		psta->bssratelen = 3;
	}
}

int rtw_ies_get_supported_rate(u8 *ies, uint ies_len, u8 *rate_set, u8 *rate_num)
{
	u8 *ie, *p;
	unsigned int ie_len;
	int i, j;

	struct support_rate_handler support_rate_tbl[] = {
		{IEEE80211_CCK_RATE_1MB, 		_FALSE,		_FALSE},
		{IEEE80211_CCK_RATE_2MB, 		_FALSE,		_FALSE},
		{IEEE80211_CCK_RATE_5MB, 		_FALSE,		_FALSE},
		{IEEE80211_CCK_RATE_11MB,		_FALSE,		_FALSE},
		{IEEE80211_OFDM_RATE_6MB,		_FALSE,		_FALSE},
		{IEEE80211_OFDM_RATE_9MB,		_FALSE,		_FALSE},
		{IEEE80211_OFDM_RATE_12MB,		_FALSE,		_FALSE},
		{IEEE80211_OFDM_RATE_18MB,		_FALSE,		_FALSE},
		{IEEE80211_OFDM_RATE_24MB,		_FALSE,		_FALSE},
		{IEEE80211_OFDM_RATE_36MB,		_FALSE,		_FALSE},
		{IEEE80211_OFDM_RATE_48MB,		_FALSE,		_FALSE},
		{IEEE80211_OFDM_RATE_54MB,		_FALSE,		_FALSE},
	};
		
	if (!rate_set || !rate_num)
		return _FALSE;

	*rate_num = 0;
	ie = rtw_get_ie(ies, _SUPPORTEDRATES_IE_, &ie_len, ies_len);
	if (ie == NULL)
		goto ext_rate;

	/* get valid supported rates */
	for (i = 0; i < 12; i++) {
		p = ie + 2;
		for (j = 0; j < ie_len; j++) {
			if ((*p & ~BIT(7)) == support_rate_tbl[i].rate){
				support_rate_tbl[i].existence = _TRUE;
				if ((*p) & BIT(7))
					support_rate_tbl[i].basic = _TRUE;
			}
			p++;
		}
	}

ext_rate:
	ie = rtw_get_ie(ies, _EXT_SUPPORTEDRATES_IE_, &ie_len, ies_len);
	if (ie) {
		/* get valid extended supported rates */
		for (i = 0; i < 12; i++) {
			p = ie + 2;
			for (j = 0; j < ie_len; j++) {
				if ((*p & ~BIT(7)) == support_rate_tbl[i].rate){
					support_rate_tbl[i].existence = _TRUE;
					if ((*p) & BIT(7))
						support_rate_tbl[i].basic = _TRUE;
				}
				p++;
			}
		}
	}

	for (i = 0; i < 12; i++){
		if (support_rate_tbl[i].existence){
			if (support_rate_tbl[i].basic)
				rate_set[*rate_num] = support_rate_tbl[i].rate | IEEE80211_BASIC_RATE_MASK;
			else
				rate_set[*rate_num] = support_rate_tbl[i].rate;
			*rate_num += 1;
		}
	}

	if (*rate_num == 0)
		return _FAIL;

	if (0) {
		int i;

		for (i = 0; i < *rate_num; i++)
			RTW_INFO("rate:0x%02x\n", *(rate_set + i));
	}

	return _SUCCESS;
}

void process_addba_req(_adapter *padapter, u8 *paddba_req, u8 *addr)
{
	struct sta_info *psta;
	u16 tid, start_seq, param;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct ADDBA_request	*preq = (struct ADDBA_request *)paddba_req;
	u8 size, accept = _FALSE;

	psta = rtw_get_stainfo(pstapriv, addr);
	if (!psta)
		goto exit;

	start_seq = le16_to_cpu(preq->BA_starting_seqctrl) >> 4;

	param = le16_to_cpu(preq->BA_para_set);
	tid = (param >> 2) & 0x0f;


	accept = rtw_rx_ampdu_is_accept(padapter);
	if (padapter->fix_rx_ampdu_size != RX_AMPDU_SIZE_INVALID)
		size = padapter->fix_rx_ampdu_size;
	else {
		size = rtw_rx_ampdu_size(padapter);
		size = rtw_min(size, rx_ampdu_size_sta_limit(padapter, psta));
	}

	if (accept == _TRUE)
		rtw_addbarsp_cmd(padapter, addr, tid, 0, size, start_seq);
	else
		rtw_addbarsp_cmd(padapter, addr, tid, 37, size, start_seq); /* reject ADDBA Req */

exit:
	return;
}

void rtw_process_bar_frame(_adapter *padapter, union recv_frame *precv_frame)
{
	struct sta_priv *pstapriv = &padapter->stapriv;
	u8 *pframe = precv_frame->u.hdr.rx_data;
	struct sta_info *psta = NULL;
	struct recv_reorder_ctrl *preorder_ctrl = NULL;
	u8 tid = 0;
	u16 start_seq=0;

	psta = rtw_get_stainfo(pstapriv, get_addr2_ptr(pframe));
	if (psta == NULL)
		goto exit;

	tid = ((cpu_to_le16((*(u16 *)(pframe + 16))) & 0xf000) >> 12);
	preorder_ctrl = &psta->recvreorder_ctrl[tid];
	start_seq = ((cpu_to_le16(*(u16 *)(pframe + 18))) >> 4);
	preorder_ctrl->indicate_seq = start_seq;

	/* for Debug use */
	if (0)
		RTW_INFO(FUNC_ADPT_FMT" tid=%d, start_seq=%d\n", FUNC_ADPT_ARG(padapter),  tid, start_seq);

exit:
	return;
}

void update_TSF(struct mlme_ext_priv *pmlmeext, u8 *pframe, uint len)
{
	u8 *pIE;
	u32 *pbuf;

	pIE = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);
	pbuf = (u32 *)pIE;

	pmlmeext->TSFValue = le32_to_cpu(*(pbuf + 1));

	pmlmeext->TSFValue = pmlmeext->TSFValue << 32;

	pmlmeext->TSFValue |= le32_to_cpu(*pbuf);
}

void correct_TSF(_adapter *padapter, u8 mlme_state)
{
	u8 m_state = mlme_state;

	rtw_hal_set_hwreg(padapter, HW_VAR_CORRECT_TSF, (u8 *)&m_state);
}

#ifdef CONFIG_BCN_RECV_TIME
/*	calculate beacon receiving time
	1.RxBCNTime(CCK_1M) = [192us(preamble)] + [length of beacon(byte)*8us] + [10us]
	2.RxBCNTime(OFDM_6M) = [8us(S) + 8us(L) + 4us(L-SIG)] + [(length of beacon(byte)/3 + 1] *4us] + [10us]
*/
inline u16 _rx_bcn_time_calculate(uint bcn_len, u8 data_rate)
{
	u16 rx_bcn_time = 0;/*us*/

	if (data_rate == DESC_RATE1M)
		rx_bcn_time = 192 + bcn_len * 8 + 10;
	else if(data_rate == DESC_RATE6M)
		rx_bcn_time = 8 + 8 + 4 + (bcn_len /3 + 1) * 4 + 10;
/*
	else
		RTW_ERR("%s invalid data rate(0x%02x)\n", __func__, data_rate);
*/
	return rx_bcn_time;
}
void rtw_rx_bcn_time_update(_adapter *adapter, uint bcn_len, u8 data_rate)
{
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;

	pmlmeext->bcn_rx_time = _rx_bcn_time_calculate(bcn_len, data_rate);
}
#endif

void beacon_timing_control(_adapter *padapter)
{
	rtw_hal_bcn_related_reg_setting(padapter);
}

inline bool _rtw_macid_ctl_chk_cap(_adapter *adapter, u8 cap)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct macid_ctl_t *macid_ctl = &dvobj->macid_ctl;

	if (macid_ctl->macid_cap & cap)
		return _TRUE;
	return _FALSE;
}

void dump_macid_map(void *sel, struct macid_bmp *map, u8 max_num)
{
	RTW_PRINT_SEL(sel, "0x%08x\n", map->m0);
#if (MACID_NUM_SW_LIMIT > 32)
	if (max_num && max_num > 32)
		RTW_PRINT_SEL(sel, "0x%08x\n", map->m1);
#endif
#if (MACID_NUM_SW_LIMIT > 64)
	if (max_num && max_num > 64)
		RTW_PRINT_SEL(sel, "0x%08x\n", map->m2);
#endif
#if (MACID_NUM_SW_LIMIT > 96)
	if (max_num && max_num > 96)
		RTW_PRINT_SEL(sel, "0x%08x\n", map->m3);
#endif
}

inline bool rtw_macid_is_set(struct macid_bmp *map, u8 id)
{
	if (id < 32)
		return map->m0 & BIT(id);
#if (MACID_NUM_SW_LIMIT > 32)
	else if (id < 64)
		return map->m1 & BIT(id - 32);
#endif
#if (MACID_NUM_SW_LIMIT > 64)
	else if (id < 96)
		return map->m2 & BIT(id - 64);
#endif
#if (MACID_NUM_SW_LIMIT > 96)
	else if (id < 128)
		return map->m3 & BIT(id - 96);
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
		map->m1 |= BIT(id - 32);
#endif
#if (MACID_NUM_SW_LIMIT > 64)
	else if (id < 96)
		map->m2 |= BIT(id - 64);
#endif
#if (MACID_NUM_SW_LIMIT > 96)
	else if (id < 128)
		map->m3 |= BIT(id - 96);
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
		map->m1 &= ~BIT(id - 32);
#endif
#if (MACID_NUM_SW_LIMIT > 64)
	else if (id < 96)
		map->m2 &= ~BIT(id - 64);
#endif
#if (MACID_NUM_SW_LIMIT > 96)
	else if (id < 128)
		map->m3 &= ~BIT(id - 96);
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

inline u8 rtw_macid_get_iface_bmp(struct macid_ctl_t *macid_ctl, u8 id)
{
	int i;
	u8 iface_bmp = 0;

	for (i = 0; i < CONFIG_IFACE_NUMBER; i++) {
		if (rtw_macid_is_set(&macid_ctl->if_g[i], id))
			iface_bmp |= BIT(i);
	}
	return iface_bmp;
}

inline bool rtw_macid_is_iface_shared(struct macid_ctl_t *macid_ctl, u8 id)
{
	int i;
	u8 iface_bmp = 0;

	for (i = 0; i < CONFIG_IFACE_NUMBER; i++) {
		if (rtw_macid_is_set(&macid_ctl->if_g[i], id)) {
			if (iface_bmp)
				return 1;
			iface_bmp |= BIT(i);
		}
	}

	return 0;
}

inline bool rtw_macid_is_iface_specific(struct macid_ctl_t *macid_ctl, u8 id, _adapter *adapter)
{
	int i;
	u8 iface_bmp = 0;

	for (i = 0; i < CONFIG_IFACE_NUMBER; i++) {
		if (rtw_macid_is_set(&macid_ctl->if_g[i], id)) {
			if (iface_bmp || i != adapter->iface_id)
				return 0;
			iface_bmp |= BIT(i);
		}
	}

	return iface_bmp ? 1 : 0;
}

inline s8 rtw_macid_get_ch_g(struct macid_ctl_t *macid_ctl, u8 id)
{
	int i;

	for (i = 0; i < 2; i++) {
		if (rtw_macid_is_set(&macid_ctl->ch_g[i], id))
			return i;
	}
	return -1;
}

/*Record bc's mac-id and sec-cam-id*/
inline void rtw_iface_bcmc_id_set(_adapter *padapter, u8 mac_id)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);

	macid_ctl->iface_bmc[padapter->iface_id] = mac_id;
}
inline u8 rtw_iface_bcmc_id_get(_adapter *padapter)
{
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);

	return macid_ctl->iface_bmc[padapter->iface_id];
}

void rtw_alloc_macid(_adapter *padapter, struct sta_info *psta)
{
	int i;
	_irqL irqL;
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	struct macid_bmp *used_map = &macid_ctl->used;
	/* static u8 last_id = 0;  for testing */
	u8 last_id = 0;
	u8 is_bc_sta = _FALSE;

	if (_rtw_memcmp(psta->cmn.mac_addr, adapter_mac_addr(padapter), ETH_ALEN)) {
		psta->cmn.mac_id = macid_ctl->num;
		return;
	}

	if (_rtw_memcmp(psta->cmn.mac_addr, bc_addr, ETH_ALEN)) {
		is_bc_sta = _TRUE;
		rtw_iface_bcmc_id_set(padapter, INVALID_SEC_MAC_CAM_ID);	/*init default value*/
	}

	if (is_bc_sta
		#ifdef CONFIG_CONCURRENT_MODE
		&& (MLME_IS_STA(padapter) || MLME_IS_NULL(padapter))
		#endif
	) {
		/* STA mode have no BMC data TX, shared with this macid */
		/* When non-concurrent, only one BMC data TX is used, shared with this macid */
		/* TODO: When concurrent, non-security BMC data TX may use this, but will not control by specific macid sleep */
		i = RTW_DEFAULT_MGMT_MACID;
		goto assigned;
	}

	_enter_critical_bh(&macid_ctl->lock, &irqL);

	for (i = last_id; i < macid_ctl->num; i++) {
#ifdef CONFIG_MCC_MODE
		/* macid 0/1 reserve for mcc for mgnt queue macid */
		if (MCC_EN(padapter)) {
			if (i == MCC_ROLE_STA_GC_MGMT_QUEUE_MACID)
				continue;
			if (i == MCC_ROLE_SOFTAP_GO_MGMT_QUEUE_MACID)
				continue;
		}
#endif /* CONFIG_MCC_MODE */

		if (is_bc_sta) {
			struct cam_ctl_t *cam_ctl = dvobj_to_sec_camctl(dvobj);

			if ((!rtw_macid_is_used(macid_ctl, i)) && (!rtw_sec_camid_is_used(cam_ctl, i)))
				break;
		} else {
			if (!rtw_macid_is_used(macid_ctl, i))
				break;
		}
	}

	if (i < macid_ctl->num) {

		rtw_macid_map_set(used_map, i);

		if (is_bc_sta) {
			struct cam_ctl_t *cam_ctl = dvobj_to_sec_camctl(dvobj);

			rtw_macid_map_set(&macid_ctl->bmc, i);
			rtw_iface_bcmc_id_set(padapter, i);
			rtw_sec_cam_map_set(&cam_ctl->used, i);
		}

		rtw_macid_map_set(&macid_ctl->if_g[padapter->iface_id], i);
		macid_ctl->sta[i] = psta;

		/* TODO ch_g? */

		last_id++;
		last_id %= macid_ctl->num;
	}

	_exit_critical_bh(&macid_ctl->lock, &irqL);

	if (i >= macid_ctl->num) {
		psta->cmn.mac_id = macid_ctl->num;
		RTW_ERR(FUNC_ADPT_FMT" if%u, mac_addr:"MAC_FMT" no available macid\n"
			, FUNC_ADPT_ARG(padapter), padapter->iface_id + 1, MAC_ARG(psta->cmn.mac_addr));
		rtw_warn_on(1);
		goto exit;
	} else
		goto assigned;

assigned:
	psta->cmn.mac_id = i;
	RTW_INFO(FUNC_ADPT_FMT" if%u, mac_addr:"MAC_FMT" macid:%u\n"
		, FUNC_ADPT_ARG(padapter), padapter->iface_id + 1, MAC_ARG(psta->cmn.mac_addr), psta->cmn.mac_id);

exit:
	return;
}

void rtw_release_macid(_adapter *padapter, struct sta_info *psta)
{
	_irqL irqL;
	u8 bc_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	u8 ifbmp;
	int i;

	if (_rtw_memcmp(psta->cmn.mac_addr, adapter_mac_addr(padapter), ETH_ALEN))
		goto exit;

	if (psta->cmn.mac_id >= macid_ctl->num) {
		RTW_WARN(FUNC_ADPT_FMT" if%u, mac_addr:"MAC_FMT" macid:%u not valid\n"
			, FUNC_ADPT_ARG(padapter), padapter->iface_id + 1
			, MAC_ARG(psta->cmn.mac_addr), psta->cmn.mac_id);
		rtw_warn_on(1);
		goto exit;
	}

	if (psta->cmn.mac_id == RTW_DEFAULT_MGMT_MACID)
		goto msg;

	_enter_critical_bh(&macid_ctl->lock, &irqL);

	if (!rtw_macid_is_used(macid_ctl, psta->cmn.mac_id)) {
		RTW_WARN(FUNC_ADPT_FMT" if%u, mac_addr:"MAC_FMT" macid:%u not used\n"
			, FUNC_ADPT_ARG(padapter), padapter->iface_id + 1
			, MAC_ARG(psta->cmn.mac_addr), psta->cmn.mac_id);
		_exit_critical_bh(&macid_ctl->lock, &irqL);
		rtw_warn_on(1);
		goto exit;
	}

	ifbmp = rtw_macid_get_iface_bmp(macid_ctl, psta->cmn.mac_id);
	if (!(ifbmp & BIT(padapter->iface_id))) {
		RTW_WARN(FUNC_ADPT_FMT" if%u, mac_addr:"MAC_FMT" macid:%u not used by self\n"
			, FUNC_ADPT_ARG(padapter), padapter->iface_id + 1
			, MAC_ARG(psta->cmn.mac_addr), psta->cmn.mac_id);
		_exit_critical_bh(&macid_ctl->lock, &irqL);
		rtw_warn_on(1);
		goto exit;
	}

	if (_rtw_memcmp(psta->cmn.mac_addr, bc_addr, ETH_ALEN)) {
		struct cam_ctl_t *cam_ctl = dvobj_to_sec_camctl(dvobj);
		u8 id = rtw_iface_bcmc_id_get(padapter);

		if ((id != INVALID_SEC_MAC_CAM_ID) && (id < cam_ctl->num))
			rtw_sec_cam_map_clr(&cam_ctl->used, id);

		rtw_iface_bcmc_id_set(padapter, INVALID_SEC_MAC_CAM_ID);
	}

	rtw_macid_map_clr(&macid_ctl->if_g[padapter->iface_id], psta->cmn.mac_id);

	ifbmp &= ~BIT(padapter->iface_id);
	if (!ifbmp) { /* only used by self */
		rtw_macid_map_clr(&macid_ctl->used, psta->cmn.mac_id);
		rtw_macid_map_clr(&macid_ctl->bmc, psta->cmn.mac_id);
		for (i = 0; i < 2; i++)
			rtw_macid_map_clr(&macid_ctl->ch_g[i], psta->cmn.mac_id);
		macid_ctl->sta[psta->cmn.mac_id] = NULL;
	}

	_exit_critical_bh(&macid_ctl->lock, &irqL);

msg:
	RTW_INFO(FUNC_ADPT_FMT" if%u, mac_addr:"MAC_FMT" macid:%u\n"
		, FUNC_ADPT_ARG(padapter), padapter->iface_id + 1
		, MAC_ARG(psta->cmn.mac_addr), psta->cmn.mac_id
	);

exit:
	psta->cmn.mac_id = macid_ctl->num;
}

/* For 8188E RA */
u8 rtw_search_max_mac_id(_adapter *padapter)
{
	u8 max_mac_id = 0;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	int i;
	_irqL irqL;

	/* TODO: Only search for connected macid? */

	_enter_critical_bh(&macid_ctl->lock, &irqL);
	for (i = (macid_ctl->num - 1); i > 0 ; i--) {
		if (rtw_macid_is_used(macid_ctl, i))
			break;
	}
	_exit_critical_bh(&macid_ctl->lock, &irqL);
	max_mac_id = i;

	return max_mac_id;
}

inline u8 rtw_macid_ctl_set_h2c_msr(struct macid_ctl_t *macid_ctl, u8 id, u8 h2c_msr)
{
	u8 op_num_change_bmp = 0;

	if (id >= macid_ctl->num) {
		rtw_warn_on(1);
		goto exit;
	}

	if (GET_H2CCMD_MSRRPT_PARM_OPMODE(&macid_ctl->h2c_msr[id])
		&& !GET_H2CCMD_MSRRPT_PARM_OPMODE(&h2c_msr)
	) {
		u8 role = GET_H2CCMD_MSRRPT_PARM_ROLE(&macid_ctl->h2c_msr[id]);

		if (role < H2C_MSR_ROLE_MAX) {
			macid_ctl->op_num[role]--;
			op_num_change_bmp |= BIT(role);
		}
	} else if (!GET_H2CCMD_MSRRPT_PARM_OPMODE(&macid_ctl->h2c_msr[id])
		&& GET_H2CCMD_MSRRPT_PARM_OPMODE(&h2c_msr)
	) {
		u8 role = GET_H2CCMD_MSRRPT_PARM_ROLE(&h2c_msr);

		if (role < H2C_MSR_ROLE_MAX) {
			macid_ctl->op_num[role]++;
			op_num_change_bmp |= BIT(role);
		}
	}

	macid_ctl->h2c_msr[id] = h2c_msr;
	if (0)
		RTW_INFO("macid:%u, h2c_msr:"H2C_MSR_FMT"\n", id, H2C_MSR_ARG(&macid_ctl->h2c_msr[id]));

exit:
	return op_num_change_bmp;
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

inline void rtw_macid_ctl_init_sleep_reg(struct macid_ctl_t *macid_ctl, u16 m0, u16 m1, u16 m2, u16 m3)
{
	macid_ctl->reg_sleep_m0 = m0;
#if (MACID_NUM_SW_LIMIT > 32)
	macid_ctl->reg_sleep_m1 = m1;
#endif
#if (MACID_NUM_SW_LIMIT > 64)
	macid_ctl->reg_sleep_m2 = m2;
#endif
#if (MACID_NUM_SW_LIMIT > 96)
	macid_ctl->reg_sleep_m3 = m3;
#endif
}

inline void rtw_macid_ctl_init_drop_reg(struct macid_ctl_t *macid_ctl, u16 m0, u16 m1, u16 m2, u16 m3)
{
	macid_ctl->reg_drop_m0 = m0;
#if (MACID_NUM_SW_LIMIT > 32)
	macid_ctl->reg_drop_m1 = m1;
#endif
#if (MACID_NUM_SW_LIMIT > 64)
	macid_ctl->reg_drop_m2 = m2;
#endif
#if (MACID_NUM_SW_LIMIT > 96)
	macid_ctl->reg_drop_m3 = m3;
#endif
}

inline void rtw_macid_ctl_init(struct macid_ctl_t *macid_ctl)
{
	int i;
	u8 id = RTW_DEFAULT_MGMT_MACID;

	rtw_macid_map_set(&macid_ctl->used, id);
	rtw_macid_map_set(&macid_ctl->bmc, id);
	for (i = 0; i < CONFIG_IFACE_NUMBER; i++)
		rtw_macid_map_set(&macid_ctl->if_g[i], id);
	macid_ctl->sta[id] = NULL;

	_rtw_spinlock_init(&macid_ctl->lock);
}

inline void rtw_macid_ctl_deinit(struct macid_ctl_t *macid_ctl)
{
	_rtw_spinlock_free(&macid_ctl->lock);
}

inline bool rtw_bmp_is_set(const u8 *bmp, u8 bmp_len, u8 id)
{
	if (id / 8 >= bmp_len)
		return 0;

	return bmp[id / 8] & BIT(id % 8);
}

inline void rtw_bmp_set(u8 *bmp, u8 bmp_len, u8 id)
{
	if (id / 8 < bmp_len)
		bmp[id / 8] |= BIT(id % 8);
}

inline void rtw_bmp_clear(u8 *bmp, u8 bmp_len, u8 id)
{
	if (id / 8 < bmp_len)
		bmp[id / 8] &= ~BIT(id % 8);
}

inline bool rtw_bmp_not_empty(const u8 *bmp, u8 bmp_len)
{
	int i;

	for (i = 0; i < bmp_len; i++) {
		if (bmp[i])
			return 1;
	}

	return 0;
}

inline bool rtw_bmp_not_empty_exclude_bit0(const u8 *bmp, u8 bmp_len)
{
	int i;

	for (i = 0; i < bmp_len; i++) {
		if (i == 0) {
			if (bmp[i] & 0xFE)
				return 1;
		} else {
			if (bmp[i])
				return 1;
		}
	}

	return 0;
}

#ifdef CONFIG_AP_MODE
/* Check the id be set or not in map , if yes , return a none zero value*/
bool rtw_tim_map_is_set(_adapter *padapter, const u8 *map, u8 id)
{
	return rtw_bmp_is_set(map, padapter->stapriv.aid_bmp_len, id);
}

/* Set the id into map array*/
void rtw_tim_map_set(_adapter *padapter, u8 *map, u8 id)
{
	rtw_bmp_set(map, padapter->stapriv.aid_bmp_len, id);
}

/* Clear the id from map array*/
void rtw_tim_map_clear(_adapter *padapter, u8 *map, u8 id)
{
	rtw_bmp_clear(map, padapter->stapriv.aid_bmp_len, id);
}

/* Check have anyone bit be set , if yes return true*/
bool rtw_tim_map_anyone_be_set(_adapter *padapter, const u8 *map)
{
	return rtw_bmp_not_empty(map, padapter->stapriv.aid_bmp_len);
}

/* Check have anyone bit be set exclude bit0 , if yes return true*/
bool rtw_tim_map_anyone_be_set_exclude_aid0(_adapter *padapter, const u8 *map)
{
	return rtw_bmp_not_empty_exclude_bit0(map, padapter->stapriv.aid_bmp_len);
}
#endif /* CONFIG_AP_MODE */

#if 0
unsigned int setup_beacon_frame(_adapter *padapter, unsigned char *beacon_frame)
{
	unsigned short				ATIMWindow;
	unsigned char					*pframe;
	struct tx_desc				*ptxdesc;
	struct rtw_ieee80211_hdr	*pwlanhdr;
	unsigned short				*fctrl;
	unsigned int					rate_len, len = 0;
	struct xmit_priv			*pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*cur_network = &(pmlmeinfo->network);
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	_rtw_memset(beacon_frame, 0, 256);

	pframe = beacon_frame + TXDESC_SIZE;

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;

	_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(cur_network), ETH_ALEN);

	set_frame_sub_type(pframe, WIFI_BEACON);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	len = sizeof(struct rtw_ieee80211_hdr_3addr);

	/* timestamp will be inserted by hardware */
	pframe += 8;
	len += 8;

	/* beacon interval: 2 bytes */
	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	len += 2;

	/* capability info: 2 bytes */
	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	len += 2;

	/* SSID */
	pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &len);

	/* supported rates... */
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8) ? 8 : rate_len), cur_network->SupportedRates, &len);

	/* DS parameter set */
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &len);

	/* IBSS Parameter Set... */
	/* ATIMWindow = cur->Configuration.ATIMWindow; */
	ATIMWindow = 0;
	pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &len);

	/* todo: ERP IE */

	/* EXTERNDED SUPPORTED RATE */
	if (rate_len > 8)
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &len);

	if ((len + TXDESC_SIZE) > 256) {
		/* RTW_INFO("marc: beacon frame too large\n"); */
		return 0;
	}

	/* fill the tx descriptor */
	ptxdesc = (struct tx_desc *)beacon_frame;

	/* offset 0	 */
	ptxdesc->txdw0 |= cpu_to_le32(len & 0x0000ffff);
	ptxdesc->txdw0 |= cpu_to_le32(((TXDESC_SIZE + OFFSET_SZ) << OFFSET_SHT) & 0x00ff0000); /* default = 32 bytes for TX Desc */

	/* offset 4	 */
	ptxdesc->txdw1 |= cpu_to_le32((0x10 << QSEL_SHT) & 0x00001f00);

	/* offset 8		 */
	ptxdesc->txdw2 |= cpu_to_le32(BMC);
	ptxdesc->txdw2 |= cpu_to_le32(BK);

	/* offset 16		 */
	ptxdesc->txdw4 = 0x80000000;

	/* offset 20 */
	ptxdesc->txdw5 = 0x00000000; /* 1M	 */

	return len + TXDESC_SIZE;
}
#endif

_adapter *dvobj_get_port0_adapter(struct dvobj_priv *dvobj)
{
	_adapter *port0_iface = NULL;
	int i;
	for (i = 0; i < dvobj->iface_nums; i++) {
		if (get_hw_port(dvobj->padapters[i]) == HW_PORT0)
			break;
	}

	if (i < 0 || i >= dvobj->iface_nums)
		rtw_warn_on(1);
	else
		port0_iface = dvobj->padapters[i];

	return port0_iface;
}

_adapter *dvobj_get_unregisterd_adapter(struct dvobj_priv *dvobj)
{
	_adapter *adapter = NULL;
	int i;

	for (i = 0; i < dvobj->iface_nums; i++) {
		if (dvobj->padapters[i]->registered == 0)
			break;
	}

	if (i < dvobj->iface_nums)
		adapter = dvobj->padapters[i];

	return adapter;
}

_adapter *dvobj_get_adapter_by_addr(struct dvobj_priv *dvobj, u8 *addr)
{
	_adapter *adapter = NULL;
	int i;

	for (i = 0; i < dvobj->iface_nums; i++) {
		if (_rtw_memcmp(dvobj->padapters[i]->mac_addr, addr, ETH_ALEN) == _TRUE)
			break;
	}

	if (i < dvobj->iface_nums)
		adapter = dvobj->padapters[i];

	return adapter;
}

#ifdef CONFIG_WOWLAN
bool rtw_wowlan_parser_pattern_cmd(u8 *input, char *pattern,
				   int *pattern_len, char *bit_mask)
{
	char *cp = NULL, *end = NULL;
	size_t len = 0;
	int pos = 0, mask_pos = 0, res = 0;
	u8 member[2] = {0};

	cp = strchr(input, '=');
	if (cp) {
		*cp = 0;
		cp++;
		input = cp;
	}

	while (1) {
		cp = strchr(input, ':');

		if (cp) {
			len = strlen(input) - strlen(cp);
			*cp = 0;
			cp++;
		} else
			len = 2;

		if (bit_mask && (strcmp(input, "-") == 0 ||
				 strcmp(input, "xx") == 0 ||
				 strcmp(input, "--") == 0)) {
			/* skip this byte and leave mask bit unset */
		} else {
			u8 hex;

			strncpy(member, input, len);
			if (!rtw_check_pattern_valid(member, sizeof(member))) {
				RTW_INFO("%s:[ERROR] pattern is invalid!!\n",
					 __func__);
				goto error;
			}

			res = sscanf(member, "%02hhx", &hex);
			pattern[pos] = hex;
			mask_pos = pos / 8;
			if (bit_mask)
				bit_mask[mask_pos] |= 1 << (pos % 8);
		}

		pos++;
		if (!cp)
			break;
		input = cp;
	}

	(*pattern_len) = pos;

	return _TRUE;
error:
	return _FALSE;
}

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
void rtw_wow_pattern_sw_reset(_adapter *adapter)
{
	int i;
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(adapter);

	if (pwrctrlpriv->default_patterns_en == _TRUE)
		pwrctrlpriv->wowlan_pattern_idx = DEFAULT_PATTERN_NUM;
	else
		pwrctrlpriv->wowlan_pattern_idx = 0;

	for (i = 0 ; i < MAX_WKFM_CAM_NUM; i++) {
		_rtw_memset(pwrctrlpriv->patterns[i].content, '\0', sizeof(pwrctrlpriv->patterns[i].content));
		_rtw_memset(pwrctrlpriv->patterns[i].mask, '\0', sizeof(pwrctrlpriv->patterns[i].mask));
		pwrctrlpriv->patterns[i].len = 0;
	}
}

u8 rtw_set_default_pattern(_adapter *adapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(adapter);
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	u8 index = 0;
	u8 multicast_addr[3] = {0x01, 0x00, 0x5e};
	u8 multicast_ip[4] = {0xe0, 0x28, 0x28, 0x2a};

	u8 unicast_mask[5] = {0x3f, 0x70, 0x80, 0xc0, 0x03};
	u8 icmpv6_mask[7] = {0x00, 0x70, 0x10, 0x00, 0xc0, 0xc0, 0x3f};
	u8 multicast_mask[5] = {0x07, 0x70, 0x80, 0xc0, 0x03};

	u8 ip_protocol[3] = {0x08, 0x00, 0x45};
	u8 ipv6_protocol[3] = {0x86, 0xdd, 0x60};

	u8 *target = NULL;

	if (pwrpriv->default_patterns_en == _FALSE)
		return 0;

	for (index = 0 ; index < DEFAULT_PATTERN_NUM ; index++) {
		_rtw_memset(pwrpriv->patterns[index].content, 0,
			    sizeof(pwrpriv->patterns[index].content));
		_rtw_memset(pwrpriv->patterns[index].mask, 0,
			    sizeof(pwrpriv->patterns[index].mask));
		pwrpriv->patterns[index].len = 0;
	}

	/*TCP/ICMP unicast*/
	for (index = 0 ; index < DEFAULT_PATTERN_NUM ; index++) {
		switch (index) {
		case 0:
			target = pwrpriv->patterns[index].content;
			_rtw_memcpy(target, adapter_mac_addr(adapter),
				    ETH_ALEN);

			target += ETH_TYPE_OFFSET;
			_rtw_memcpy(target, &ip_protocol,
				    sizeof(ip_protocol));

			/* TCP */
			target += (PROTOCOL_OFFSET - ETH_TYPE_OFFSET);
			_rtw_memset(target, 0x06, 1);

			target += (IP_OFFSET - PROTOCOL_OFFSET);

			_rtw_memcpy(target, pmlmeinfo->ip_addr,
				    RTW_IP_ADDR_LEN);

			_rtw_memcpy(pwrpriv->patterns[index].mask,
				    &unicast_mask, sizeof(unicast_mask));

			pwrpriv->patterns[index].len =
				IP_OFFSET + RTW_IP_ADDR_LEN;
			break;
		case 1:
			target = pwrpriv->patterns[index].content;
			_rtw_memcpy(target, adapter_mac_addr(adapter),
				    ETH_ALEN);

			target += ETH_TYPE_OFFSET;
			_rtw_memcpy(target, &ip_protocol, sizeof(ip_protocol));

			/* ICMP */
			target += (PROTOCOL_OFFSET - ETH_TYPE_OFFSET);
			_rtw_memset(target, 0x01, 1);

			target += (IP_OFFSET - PROTOCOL_OFFSET);
			_rtw_memcpy(target, pmlmeinfo->ip_addr,
				    RTW_IP_ADDR_LEN);

			_rtw_memcpy(pwrpriv->patterns[index].mask,
				    &unicast_mask, sizeof(unicast_mask));
			pwrpriv->patterns[index].len =

				IP_OFFSET + RTW_IP_ADDR_LEN;
			break;
#ifdef CONFIG_IPV6
		case 2:
			if (pwrpriv->wowlan_ns_offload_en == _TRUE) {
				target = pwrpriv->patterns[index].content;
				target += ETH_TYPE_OFFSET;

				_rtw_memcpy(target, &ipv6_protocol,
					    sizeof(ipv6_protocol));

				/* ICMPv6 */
				target += (IPv6_PROTOCOL_OFFSET -
					   ETH_TYPE_OFFSET);
				_rtw_memset(target, 0x3a, 1);

				target += (IPv6_OFFSET - IPv6_PROTOCOL_OFFSET);
				_rtw_memcpy(target, pmlmeinfo->ip6_addr,
					    RTW_IPv6_ADDR_LEN);

				_rtw_memcpy(pwrpriv->patterns[index].mask,
					    &icmpv6_mask, sizeof(icmpv6_mask));
				pwrpriv->patterns[index].len =
					IPv6_OFFSET + RTW_IPv6_ADDR_LEN;
			}
			break;
#endif /*CONFIG_IPV6*/
		case 3:
			target = pwrpriv->patterns[index].content;
			_rtw_memcpy(target, &multicast_addr,
				    sizeof(multicast_addr));

			target += ETH_TYPE_OFFSET;
			_rtw_memcpy(target, &ip_protocol, sizeof(ip_protocol));

			/* UDP */
			target += (PROTOCOL_OFFSET - ETH_TYPE_OFFSET);
			_rtw_memset(target, 0x11, 1);

			target += (IP_OFFSET - PROTOCOL_OFFSET);
			_rtw_memcpy(target, &multicast_ip,
				    sizeof(multicast_ip));

			_rtw_memcpy(pwrpriv->patterns[index].mask,
				    &multicast_mask, sizeof(multicast_mask));

			pwrpriv->patterns[index].len =
				IP_OFFSET + sizeof(multicast_ip);
			break;
		default:
			break;
		}
	}
	return index;
}

void rtw_dump_priv_pattern(_adapter *adapter, u8 idx)
{
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(adapter);
	char str_1[128];
	char *p_str;
	u8 val8 = 0;
	int i = 0, j = 0, len = 0, max_len = 0;

	RTW_INFO("=========[%d]========\n", idx);

	RTW_INFO(">>>priv_pattern_content:\n");
	p_str = str_1;
	max_len = sizeof(str_1);
	for (i = 0 ; i < MAX_WKFM_PATTERN_SIZE / 8 ; i++) {
		_rtw_memset(p_str, 0, max_len);
		len = 0;
		for (j = 0 ; j < 8 ; j++) {
			val8 = pwrctl->patterns[idx].content[i * 8 + j];
			len += snprintf(p_str + len, max_len - len,
					"%02x ", val8);
		}
		RTW_INFO("%s\n", p_str);
	}

	RTW_INFO(">>>priv_pattern_mask:\n");
	for (i = 0 ; i < MAX_WKFM_SIZE / 8 ; i++) {
		_rtw_memset(p_str, 0, max_len);
		len = 0;
		for (j = 0 ; j < 8 ; j++) {
			val8 = pwrctl->patterns[idx].mask[i * 8 + j];
			len += snprintf(p_str + len, max_len - len,
					"%02x ", val8);
		}
		RTW_INFO("%s\n", p_str);
	}

	RTW_INFO(">>>priv_pattern_len:\n");
	RTW_INFO("%s: len: %d\n", __func__, pwrctl->patterns[idx].len);
}

void rtw_wow_pattern_sw_dump(_adapter *adapter)
{
	int i;

	RTW_INFO("********[RTK priv-patterns]*********\n");
	for (i = 0 ; i < MAX_WKFM_CAM_NUM; i++)
		rtw_dump_priv_pattern(adapter, i);
}

void rtw_get_sec_iv(PADAPTER padapter, u8 *pcur_dot11txpn, u8 *StaAddr)
{
	struct sta_info		*psta;
	struct security_priv *psecpriv = &padapter->securitypriv;

	_rtw_memset(pcur_dot11txpn, 0, 8);
	if (NULL == StaAddr)
		return;
	psta = rtw_get_stainfo(&padapter->stapriv, StaAddr);
	RTW_INFO("%s(): StaAddr: %02x %02x %02x %02x %02x %02x\n",
		 __func__, StaAddr[0], StaAddr[1], StaAddr[2],
		 StaAddr[3], StaAddr[4], StaAddr[5]);

	if (psta) {
		if (psecpriv->dot11PrivacyAlgrthm == _AES_)
			AES_IV(pcur_dot11txpn, psta->dot11txpn, 0);
		else if (psecpriv->dot11PrivacyAlgrthm == _TKIP_)
			TKIP_IV(pcur_dot11txpn, psta->dot11txpn, 0);

		RTW_INFO("%s(): CurrentIV: %02x %02x %02x %02x %02x %02x %02x %02x\n"
			 , __func__, pcur_dot11txpn[0], pcur_dot11txpn[1],
			pcur_dot11txpn[2], pcur_dot11txpn[3], pcur_dot11txpn[4],
			pcur_dot11txpn[5], pcur_dot11txpn[6], pcur_dot11txpn[7]);
	}
}
#endif /* CONFIG_WOWLAN */

#ifdef CONFIG_PNO_SUPPORT
#define	CSCAN_TLV_TYPE_SSID_IE	'S'
#define CIPHER_IE "key_mgmt="
#define CIPHER_NONE "NONE"
#define CIPHER_WPA_PSK "WPA-PSK"
#define CIPHER_WPA_EAP "WPA-EAP IEEE8021X"
/*
 *  SSIDs list parsing from cscan tlv list
 */
int rtw_parse_ssid_list_tlv(char **list_str, pno_ssid_t *ssid,
			    int max, int *bytes_left)
{
	char *str;

	int idx = 0;

	if ((list_str == NULL) || (*list_str == NULL) || (*bytes_left < 0)) {
		RTW_INFO("%s error paramters\n", __func__);
		return -1;
	}

	str = *list_str;
	while (*bytes_left > 0) {

		if (str[0] != CSCAN_TLV_TYPE_SSID_IE) {
			*list_str = str;
			RTW_INFO("nssid=%d left_parse=%d %d\n", idx, *bytes_left, str[0]);
			return idx;
		}

		/* Get proper CSCAN_TLV_TYPE_SSID_IE */
		*bytes_left -= 1;
		str += 1;

		if (str[0] == 0) {
			/* Broadcast SSID */
			ssid[idx].SSID_len = 0;
			memset((char *)ssid[idx].SSID, 0x0, WLAN_SSID_MAXLEN);
			*bytes_left -= 1;
			str += 1;

			RTW_INFO("BROADCAST SCAN  left=%d\n", *bytes_left);
		} else if (str[0] <= WLAN_SSID_MAXLEN) {
			/* Get proper SSID size */
			ssid[idx].SSID_len = str[0];
			*bytes_left -= 1;
			str += 1;

			/* Get SSID */
			if (ssid[idx].SSID_len > *bytes_left) {
				RTW_INFO("%s out of memory range len=%d but left=%d\n",
					__func__, ssid[idx].SSID_len, *bytes_left);
				return -1;
			}

			memcpy((char *)ssid[idx].SSID, str, ssid[idx].SSID_len);

			*bytes_left -= ssid[idx].SSID_len;
			str += ssid[idx].SSID_len;

			RTW_INFO("%s :size=%d left=%d\n",
				(char *)ssid[idx].SSID, ssid[idx].SSID_len, *bytes_left);
		} else {
			RTW_INFO("### SSID size more that %d\n", str[0]);
			return -1;
		}

		if (idx++ >  max) {
			RTW_INFO("%s number of SSIDs more that %d\n", __func__, idx);
			return -1;
		}
	}

	*list_str = str;
	return idx;
}

int rtw_parse_cipher_list(struct pno_nlo_info *nlo_info, char *list_str)
{

	char *pch, *pnext, *pend;
	u8 key_len = 0, index = 0;

	pch = list_str;

	if (nlo_info == NULL || list_str == NULL) {
		RTW_INFO("%s error paramters\n", __func__);
		return -1;
	}

	while (strlen(pch) != 0) {
		pnext = strstr(pch, "key_mgmt=");
		if (pnext != NULL) {
			pch = pnext + strlen(CIPHER_IE);
			pend = strstr(pch, "}");
			if (strncmp(pch, CIPHER_NONE,
				    strlen(CIPHER_NONE)) == 0)
				nlo_info->ssid_cipher_info[index] = 0x00;
			else if (strncmp(pch, CIPHER_WPA_PSK,
					 strlen(CIPHER_WPA_PSK)) == 0)
				nlo_info->ssid_cipher_info[index] = 0x66;
			else if (strncmp(pch, CIPHER_WPA_EAP,
					 strlen(CIPHER_WPA_EAP)) == 0)
				nlo_info->ssid_cipher_info[index] = 0x01;
			index++;
			pch = pend + 1;
		} else
			break;
	}
	return 0;
}

int rtw_dev_nlo_info_set(struct pno_nlo_info *nlo_info, pno_ssid_t *ssid,
		 int num, int pno_time, int pno_repeat, int pno_freq_expo_max)
{

	int i = 0;
	struct file *fp;
	mm_segment_t fs;
	loff_t pos = 0;
	u8 *source = NULL;
	long len = 0;

	RTW_INFO("+%s+\n", __func__);

	nlo_info->fast_scan_period = pno_time;
	nlo_info->ssid_num = num & BIT_LEN_MASK_32(8);
	nlo_info->hidden_ssid_num = num & BIT_LEN_MASK_32(8);
	nlo_info->slow_scan_period = (pno_time * 2);
	nlo_info->fast_scan_iterations = 5;

	if (nlo_info->hidden_ssid_num > 8)
		nlo_info->hidden_ssid_num = 8;

	/* TODO: channel list and probe index is all empty. */
	for (i = 0 ; i < num ; i++) {
		nlo_info->ssid_length[i]
			= ssid[i].SSID_len;
	}

	/* cipher array */
	fp = filp_open("/data/misc/wifi/wpa_supplicant.conf", O_RDONLY,  0644);
	if (IS_ERR(fp)) {
		RTW_INFO("Error, wpa_supplicant.conf doesn't exist.\n");
		RTW_INFO("Error, cipher array using default value.\n");
		return 0;
	}

	len = i_size_read(fp->f_path.dentry->d_inode);
	if (len < 0 || len > 2048) {
		RTW_INFO("Error, file size is bigger than 2048.\n");
		RTW_INFO("Error, cipher array using default value.\n");
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

	RTW_INFO("-%s-\n", __func__);
	return 0;
}

int rtw_dev_ssid_list_set(struct pno_ssid_list *pno_ssid_list,
			  pno_ssid_t *ssid, u8 num)
{

	int i = 0;
	if (num > MAX_PNO_LIST_COUNT)
		num = MAX_PNO_LIST_COUNT;

	for (i = 0 ; i < num ; i++) {
		_rtw_memcpy(&pno_ssid_list->node[i].SSID,
			    ssid[i].SSID, ssid[i].SSID_len);
		pno_ssid_list->node[i].SSID_len = ssid[i].SSID_len;
	}
	return 0;
}

int rtw_dev_scan_info_set(_adapter *padapter, pno_ssid_t *ssid,
	  unsigned char ch, unsigned char ch_offset, unsigned short bw_mode)
{

	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	struct pno_scan_info *scan_info = pwrctl->pscan_info;
	int i;

	scan_info->channel_num = MAX_SCAN_LIST_COUNT;
	scan_info->orig_ch = ch;
	scan_info->orig_bw = bw_mode;
	scan_info->orig_40_offset = ch_offset;

	for (i = 0 ; i < scan_info->channel_num ; i++) {
		if (i < 11)
			scan_info->ssid_channel_info[i].active = 1;
		else
			scan_info->ssid_channel_info[i].active = 0;

		scan_info->ssid_channel_info[i].timeout = 100;

		scan_info->ssid_channel_info[i].tx_power =
			phy_get_tx_power_index(padapter, 0, 0x02, bw_mode, i + 1);

		scan_info->ssid_channel_info[i].channel = i + 1;
	}

	RTW_INFO("%s, channel_num: %d, orig_ch: %d, orig_bw: %d orig_40_offset: %d\n",
		 __func__, scan_info->channel_num, scan_info->orig_ch,
		 scan_info->orig_bw, scan_info->orig_40_offset);
	return 0;
}

int rtw_dev_pno_set(struct net_device *net, pno_ssid_t *ssid, int num,
		    int pno_time, int pno_repeat, int pno_freq_expo_max)
{

	_adapter *padapter = (_adapter *)rtw_netdev_priv(net);
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	int ret = -1;

	if (num == 0) {
		RTW_INFO("%s, nssid is zero, no need to setup pno ssid list\n", __func__);
		return 0;
	}

	if (pwrctl == NULL) {
		RTW_INFO("%s, ERROR: pwrctl is NULL\n", __func__);
		return -1;
	} else {
		pwrctl->pnlo_info =
			(pno_nlo_info_t *)rtw_zmalloc(sizeof(pno_nlo_info_t));
		pwrctl->pno_ssid_list =
			(pno_ssid_list_t *)rtw_zmalloc(sizeof(pno_ssid_list_t));
		pwrctl->pscan_info =
			(pno_scan_info_t *)rtw_zmalloc(sizeof(pno_scan_info_t));
	}

	if (pwrctl->pnlo_info == NULL ||
	    pwrctl->pscan_info == NULL ||
	    pwrctl->pno_ssid_list == NULL) {
		RTW_INFO("%s, ERROR: alloc nlo_info, ssid_list, scan_info fail\n", __func__);
		goto failing;
	}

	pwrctl->wowlan_in_resume = _FALSE;

	pwrctl->pno_inited = _TRUE;
	/* NLO Info */
	ret = rtw_dev_nlo_info_set(pwrctl->pnlo_info, ssid, num,
				   pno_time, pno_repeat, pno_freq_expo_max);

	/* SSID Info */
	ret = rtw_dev_ssid_list_set(pwrctl->pno_ssid_list, ssid, num);

	/* SCAN Info */
	ret = rtw_dev_scan_info_set(padapter, ssid, pmlmeext->cur_channel,
			    pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode);

	RTW_INFO("+%s num: %d, pno_time: %d, pno_repeat:%d, pno_freq_expo_max:%d+\n",
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
void rtw_dev_pno_debug(struct net_device *net)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(net);
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	int i = 0, j = 0;

	RTW_INFO("*******NLO_INFO********\n");
	RTW_INFO("ssid_num: %d\n", pwrctl->pnlo_info->ssid_num);
	RTW_INFO("fast_scan_iterations: %d\n",
		 pwrctl->pnlo_info->fast_scan_iterations);
	RTW_INFO("fast_scan_period: %d\n", pwrctl->pnlo_info->fast_scan_period);
	RTW_INFO("slow_scan_period: %d\n", pwrctl->pnlo_info->slow_scan_period);



	for (i = 0 ; i < MAX_PNO_LIST_COUNT ; i++) {
		RTW_INFO("%d SSID (%s) length (%d) cipher(%x) channel(%d)\n",
			i, pwrctl->pno_ssid_list->node[i].SSID, pwrctl->pnlo_info->ssid_length[i],
			pwrctl->pnlo_info->ssid_cipher_info[i], pwrctl->pnlo_info->ssid_channel_info[i]);
	}

	RTW_INFO("******SCAN_INFO******\n");
	RTW_INFO("ch_num: %d\n", pwrctl->pscan_info->channel_num);
	RTW_INFO("orig_ch: %d\n", pwrctl->pscan_info->orig_ch);
	RTW_INFO("orig bw: %d\n", pwrctl->pscan_info->orig_bw);
	RTW_INFO("orig 40 offset: %d\n", pwrctl->pscan_info->orig_40_offset);
	for (i = 0 ; i < MAX_SCAN_LIST_COUNT ; i++) {
		RTW_INFO("[%02d] avtive:%d, timeout:%d, tx_power:%d, ch:%02d\n",
			 i, pwrctl->pscan_info->ssid_channel_info[i].active,
			 pwrctl->pscan_info->ssid_channel_info[i].timeout,
			 pwrctl->pscan_info->ssid_channel_info[i].tx_power,
			 pwrctl->pscan_info->ssid_channel_info[i].channel);
	}
	RTW_INFO("*****************\n");
}
#endif /* CONFIG_PNO_SET_DEBUG */
#endif /* CONFIG_PNO_SUPPORT */

inline void rtw_collect_bcn_info(_adapter *adapter)
{
	struct mlme_ext_priv *pmlmeext = &adapter->mlmeextpriv;

	if (!is_client_associated_to_ap(adapter))
		return;

	pmlmeext->cur_bcn_cnt = pmlmeext->bcn_cnt - pmlmeext->last_bcn_cnt;
	pmlmeext->last_bcn_cnt = pmlmeext->bcn_cnt;
	/*TODO get offset of bcn's timestamp*/
	/*pmlmeext->bcn_timestamp;*/
}
