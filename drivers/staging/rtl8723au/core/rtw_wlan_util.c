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
 ******************************************************************************/
#define _RTW_WLAN_UTIL_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <linux/ieee80211.h>
#include <wifi.h>

static unsigned char ARTHEROS_OUI1[] = {0x00, 0x03, 0x7f};
static unsigned char ARTHEROS_OUI2[] = {0x00, 0x13, 0x74};

static unsigned char BROADCOM_OUI1[] = {0x00, 0x10, 0x18};
static unsigned char BROADCOM_OUI2[] = {0x00, 0x0a, 0xf7};

static unsigned char CISCO_OUI[] = {0x00, 0x40, 0x96};
static unsigned char MARVELL_OUI[] = {0x00, 0x50, 0x43};
static unsigned char RALINK_OUI[] = {0x00, 0x0c, 0x43};
static unsigned char REALTEK_OUI[] = {0x00, 0xe0, 0x4c};
static unsigned char AIRGOCAP_OUI[] = {0x00, 0x0a, 0xf5};
static unsigned char EPIGRAM_OUI[] = {0x00, 0x90, 0x4c};

static unsigned char WPA_TKIP_CIPHER[4] = {0x00, 0x50, 0xf2, 0x02};
static unsigned char RSN_TKIP_CIPHER[4] = {0x00, 0x0f, 0xac, 0x02};

#define R2T_PHY_DELAY		0

/* define WAIT_FOR_BCN_TO_MIN	3000 */
#define WAIT_FOR_BCN_TO_MIN	6000
#define WAIT_FOR_BCN_TO_MAX	20000

static u8 rtw_basic_rate_cck[4] = {
	IEEE80211_CCK_RATE_1MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_CCK_RATE_2MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_CCK_RATE_5MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_CCK_RATE_11MB | IEEE80211_BASIC_RATE_MASK
};

static u8 rtw_basic_rate_ofdm[3] = {
	IEEE80211_OFDM_RATE_6MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_OFDM_RATE_12MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_OFDM_RATE_24MB | IEEE80211_BASIC_RATE_MASK
};

static u8 rtw_basic_rate_mix[7] = {
	IEEE80211_CCK_RATE_1MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_CCK_RATE_2MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_CCK_RATE_5MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_CCK_RATE_11MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_OFDM_RATE_6MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_OFDM_RATE_12MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_OFDM_RATE_24MB | IEEE80211_BASIC_RATE_MASK
};

int cckrates_included23a(unsigned char *rate, int ratelen)
{
	int	i;

	for (i = 0; i < ratelen; i++) {
		if  (((rate[i]) & 0x7f) == 2 || ((rate[i]) & 0x7f) == 4 ||
		     ((rate[i]) & 0x7f) == 11  || ((rate[i]) & 0x7f) == 22)
			return true;
	}

	return false;
}

int cckratesonly_included23a(unsigned char *rate, int ratelen)
{
	int	i;

	for (i = 0; i < ratelen; i++) {
		if  (((rate[i]) & 0x7f) != 2 && ((rate[i]) & 0x7f) != 4 &&
		     ((rate[i]) & 0x7f) != 11 && ((rate[i]) & 0x7f) != 22)
		return false;
	}

	return true;
}

unsigned char networktype_to_raid23a(unsigned char network_type)
{
	unsigned char raid;

	switch (network_type) {
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

u8 judge_network_type23a(struct rtw_adapter *padapter,
			 unsigned char *rate, int ratelen)
{
	u8 network_type = 0;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	if (pmlmeext->cur_channel > 14) {
		if (pmlmeinfo->HT_enable)
			network_type = WIRELESS_11_5N;
		network_type |= WIRELESS_11A;
	} else {
		if (pmlmeinfo->HT_enable)
			network_type = WIRELESS_11_24N;

		if ((cckratesonly_included23a(rate, ratelen)) == true)
			network_type |= WIRELESS_11B;
		else if ((cckrates_included23a(rate, ratelen)) == true)
			network_type |= WIRELESS_11BG;
		else
			network_type |= WIRELESS_11G;
	}
	return	network_type;
}

static unsigned char ratetbl_val_2wifirate(unsigned char rate)
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

static int is_basicrate(struct rtw_adapter *padapter, unsigned char rate)
{
	int i;
	unsigned char val;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	for (i = 0; i < NumRates; i++) {
		val = pmlmeext->basicrate[i];

		if (val != 0xff && val != 0xfe) {
			if (rate == ratetbl_val_2wifirate(val))
				return true;
		}
	}

	return false;
}

static unsigned int ratetbl2rateset(struct rtw_adapter *padapter,
				    unsigned char *rateset)
{
	int i;
	unsigned char rate;
	unsigned int len = 0;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	for (i = 0; i < NumRates; i++) {
		rate = pmlmeext->datarate[i];

		switch (rate) {
		case 0xff:
			return len;
		case 0xfe:
			continue;
		default:
			rate = ratetbl_val_2wifirate(rate);

			if (is_basicrate(padapter, rate) == true)
				rate |= IEEE80211_BASIC_RATE_MASK;

			rateset[len] = rate;
			len++;
			break;
		}
	}
	return len;
}

void get_rate_set23a(struct rtw_adapter *padapter,
		     unsigned char *pbssrate, int *bssrate_len)
{
	unsigned char supportedrates[NumRates];

	memset(supportedrates, 0, NumRates);
	*bssrate_len = ratetbl2rateset(padapter, supportedrates);
	memcpy(pbssrate, supportedrates, *bssrate_len);
}

void UpdateBrateTbl23a(struct rtw_adapter *Adapter, u8 *mBratesOS)
{
	u8 i;
	u8 rate;

	/*  1M, 2M, 5.5M, 11M, 6M, 12M, 24M are mandatory. */
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
		default:
			break;
		}
	}
}

void Update23aTblForSoftAP(u8 *bssrateset, u32 bssratelen)
{
	u8 i;
	u8 rate;

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

void Set_MSR23a(struct rtw_adapter *padapter, u8 type)
{
	rtl8723a_set_media_status(padapter, type);
}

inline u8 rtw_get_oper_ch23a(struct rtw_adapter *adapter)
{
	return adapter_to_dvobj(adapter)->oper_channel;
}

inline void rtw_set_oper_ch23a(struct rtw_adapter *adapter, u8 ch)
{
	adapter_to_dvobj(adapter)->oper_channel = ch;
}

inline u8 rtw_get_oper_bw23a(struct rtw_adapter *adapter)
{
	return adapter_to_dvobj(adapter)->oper_bwmode;
}

inline void rtw_set_oper_bw23a(struct rtw_adapter *adapter, u8 bw)
{
	adapter_to_dvobj(adapter)->oper_bwmode = bw;
}

inline u8 rtw_get_oper_ch23aoffset(struct rtw_adapter *adapter)
{
	return adapter_to_dvobj(adapter)->oper_ch_offset;
}

inline void rtw_set_oper_ch23aoffset23a(struct rtw_adapter *adapter, u8 offset)
{
	adapter_to_dvobj(adapter)->oper_ch_offset = offset;
}

void SelectChannel23a(struct rtw_adapter *padapter, unsigned char channel)
{
	mutex_lock(&adapter_to_dvobj(padapter)->setch_mutex);

	/* saved channel info */
	rtw_set_oper_ch23a(padapter, channel);

	PHY_SwChnl8723A(padapter, channel);

	mutex_unlock(&adapter_to_dvobj(padapter)->setch_mutex);
}

static void set_bwmode(struct rtw_adapter *padapter, unsigned short bwmode,
		       unsigned char channel_offset)
{
	mutex_lock(&adapter_to_dvobj(padapter)->setbw_mutex);

	/* saved bw info */
	rtw_set_oper_bw23a(padapter, bwmode);
	rtw_set_oper_ch23aoffset23a(padapter, channel_offset);

	PHY_SetBWMode23a8723A(padapter, (enum ht_channel_width)bwmode,
			      channel_offset);

	mutex_unlock(&adapter_to_dvobj(padapter)->setbw_mutex);
}

void set_channel_bwmode23a(struct rtw_adapter *padapter, unsigned char channel,
		        unsigned char channel_offset, unsigned short bwmode)
{
	u8 center_ch;

	if (padapter->bNotifyChannelChange)
		DBG_8723A("[%s] ch = %d, offset = %d, bwmode = %d\n",
			  __func__, channel, channel_offset, bwmode);

	if (bwmode == HT_CHANNEL_WIDTH_20 ||
	    channel_offset == HAL_PRIME_CHNL_OFFSET_DONT_CARE) {
		/* SelectChannel23a(padapter, channel); */
		center_ch = channel;
	} else {
		/* switch to the proper channel */
		if (channel_offset == HAL_PRIME_CHNL_OFFSET_LOWER) {
			/* SelectChannel23a(padapter, channel + 2); */
			center_ch = channel + 2;
		} else {
			/* SelectChannel23a(padapter, channel - 2); */
			center_ch = channel - 2;
		}
	}

	/* set Channel */
	mutex_lock(&adapter_to_dvobj(padapter)->setch_mutex);

	/* saved channel/bw info */
	rtw_set_oper_ch23a(padapter, channel);
	rtw_set_oper_bw23a(padapter, bwmode);
	rtw_set_oper_ch23aoffset23a(padapter, channel_offset);

	PHY_SwChnl8723A(padapter, center_ch); /*  set center channel */

	mutex_unlock(&adapter_to_dvobj(padapter)->setch_mutex);

	set_bwmode(padapter, bwmode, channel_offset);
}

inline u8 *get_my_bssid23a(struct wlan_bssid_ex *pnetwork)
{
	return pnetwork->MacAddress;
}

u16 get_beacon_interval23a(struct wlan_bssid_ex *bss)
{
	unsigned short val;
	memcpy(&val, rtw_get_beacon_interval23a_from_ie(bss->IEs), 2);

	return le16_to_cpu(val);
}

bool is_client_associated_to_ap23a(struct rtw_adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;

	if (!padapter)
		return false;

	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS &&
	    (pmlmeinfo->state & 0x03) == WIFI_FW_STATION_STATE)
		return true;
	else
		return false;
}

bool is_client_associated_to_ibss23a(struct rtw_adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	if (pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS &&
	    (pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE)
		return true;
	else
		return false;
}

bool is_IBSS_empty23a(struct rtw_adapter *padapter)
{
	unsigned int i;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	for (i = IBSS_START_MAC_ID; i < NUM_STA; i++) {
		if (pmlmeinfo->FW_sta_info[i].status == 1)
			return false;
	}

	return true;
}

unsigned int decide_wait_for_beacon_timeout23a(unsigned int bcn_interval)
{
	if ((bcn_interval << 2) < WAIT_FOR_BCN_TO_MIN)
		return WAIT_FOR_BCN_TO_MIN;
	else if ((bcn_interval << 2) > WAIT_FOR_BCN_TO_MAX)
		return WAIT_FOR_BCN_TO_MAX;
	else
		return bcn_interval << 2;
}

void invalidate_cam_all23a(struct rtw_adapter *padapter)
{
	rtl8723a_cam_invalid_all(padapter);
}

void clear_cam_entry23a(struct rtw_adapter *padapter, u8 entry)
{
	unsigned char null_sta[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	unsigned char null_key[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				    0x00, 0x00, 0x00, 0x00};

	rtl8723a_cam_write(padapter, entry, 0, null_sta, null_key);
}

int allocate_fw_sta_entry23a(struct rtw_adapter *padapter)
{
	unsigned int mac_id;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	for (mac_id = IBSS_START_MAC_ID; mac_id < NUM_STA; mac_id++) {
		if (pmlmeinfo->FW_sta_info[mac_id].status == 0) {
			pmlmeinfo->FW_sta_info[mac_id].status = 1;
			pmlmeinfo->FW_sta_info[mac_id].retry = 0;
			break;
		}
	}

	return mac_id;
}

void flush_all_cam_entry23a(struct rtw_adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	rtl8723a_cam_invalid_all(padapter);

	memset(pmlmeinfo->FW_sta_info, 0, sizeof(pmlmeinfo->FW_sta_info));
}

int WMM_param_handler23a(struct rtw_adapter *padapter, u8 *p)
{
	/* struct registry_priv	*pregpriv = &padapter->registrypriv; */
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	if (pmlmepriv->qos_option == 0) {
		pmlmeinfo->WMM_enable = 0;
		return _FAIL;
	}

	pmlmeinfo->WMM_enable = 1;
	memcpy(&pmlmeinfo->WMM_param, p + 2 + 6,
	       sizeof(struct WMM_para_element));
	return true;
}

void WMMOnAssocRsp23a(struct rtw_adapter *padapter)
{
	u8 ACI, ACM, AIFS, ECWMin, ECWMax, aSifsTime;
	u8 acm_mask;
	u16 TXOP;
	u32 acParm, i;
	u32 edca[4], inx[4];
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct registry_priv *pregpriv = &padapter->registrypriv;

	if (pmlmeinfo->WMM_enable == 0) {
		padapter->mlmepriv.acm_mask = 0;
		return;
	}

	acm_mask = 0;

	if (pmlmeext->cur_wireless_mode == WIRELESS_11B)
		aSifsTime = 10;
	else
		aSifsTime = 16;

		for (i = 0; i < 4; i++) {
		ACI = (pmlmeinfo->WMM_param.ac_param[i].ACI_AIFSN >> 5) & 0x03;
		ACM = (pmlmeinfo->WMM_param.ac_param[i].ACI_AIFSN >> 4) & 0x01;

		/* AIFS = AIFSN * slot time + SIFS - r2t phy delay */
		AIFS = (pmlmeinfo->WMM_param.ac_param[i].ACI_AIFSN & 0x0f) *
			pmlmeinfo->slotTime + aSifsTime;

		ECWMin = pmlmeinfo->WMM_param.ac_param[i].CW & 0x0f;
		ECWMax = (pmlmeinfo->WMM_param.ac_param[i].CW & 0xf0) >> 4;
		TXOP = le16_to_cpu(pmlmeinfo->WMM_param.ac_param[i].TXOP_limit);

		acParm = AIFS | (ECWMin << 8) | (ECWMax << 12) | (TXOP << 16);

		switch (ACI) {
		case 0x0:
			rtl8723a_set_ac_param_be(padapter, acParm);
			acm_mask |= (ACM? BIT(1):0);
			edca[XMIT_BE_QUEUE] = acParm;
			break;
		case 0x1:
			rtl8723a_set_ac_param_bk(padapter, acParm);
			/* acm_mask |= (ACM? BIT(0):0); */
			edca[XMIT_BK_QUEUE] = acParm;
			break;
		case 0x2:
			rtl8723a_set_ac_param_vi(padapter, acParm);
			acm_mask |= (ACM? BIT(2):0);
			edca[XMIT_VI_QUEUE] = acParm;
			break;
		case 0x3:
			rtl8723a_set_ac_param_vo(padapter, acParm);
			acm_mask |= (ACM? BIT(3):0);
			edca[XMIT_VO_QUEUE] = acParm;
			break;
		}

		DBG_8723A("WMM(%x): %x, %x\n", ACI, ACM, acParm);
	}

	if (padapter->registrypriv.acm_method == 1)
		rtl8723a_set_acm_ctrl(padapter, acm_mask);
	else
		padapter->mlmepriv.acm_mask = acm_mask;

	inx[0] = 0; inx[1] = 1; inx[2] = 2; inx[3] = 3;

	if (pregpriv->wifi_spec == 1) {
		u32 j, tmp, change_inx = false;

		/* entry indx: 0->vo, 1->vi, 2->be, 3->bk. */
		for (i = 0; i < 4; i++) {
			for (j = i+1; j < 4; j++) {
				/* compare CW and AIFS */
				if ((edca[j] & 0xFFFF) < (edca[i] & 0xFFFF)) {
					change_inx = true;
				} else if ((edca[j] & 0xFFFF) ==
					   (edca[i] & 0xFFFF)) {
					/* compare TXOP */
					if ((edca[j] >> 16) > (edca[i] >> 16))
						change_inx = true;
				}

				if (change_inx) {
					tmp = edca[i];
					edca[i] = edca[j];
					edca[j] = tmp;

					tmp = inx[i];
					inx[i] = inx[j];
					inx[j] = tmp;

					change_inx = false;
				}
			}
		}
	}

	for (i = 0; i<4; i++) {
		pxmitpriv->wmm_para_seq[i] = inx[i];
		DBG_8723A("wmm_para_seq(%d): %d\n", i,
			  pxmitpriv->wmm_para_seq[i]);
	}

	return;
}

static void bwmode_update_check(struct rtw_adapter *padapter, const u8 *p)
{
	struct ieee80211_ht_operation *pHT_info;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct ht_priv *phtpriv = &pmlmepriv->htpriv;
	unsigned char new_bwmode;
	unsigned char new_ch_offset;

	if (!p)
		return;
	if (!phtpriv->ht_option)
		return;
	if (p[1] != sizeof(struct ieee80211_ht_operation))
		return;

	pHT_info = (struct ieee80211_ht_operation *)(p + 2);

	if ((pHT_info->ht_param & IEEE80211_HT_PARAM_CHAN_WIDTH_ANY) &&
	    pregistrypriv->cbw40_enable) {
		new_bwmode = HT_CHANNEL_WIDTH_40;

		switch (pHT_info->ht_param & IEEE80211_HT_PARAM_CHA_SEC_OFFSET){
		case IEEE80211_HT_PARAM_CHA_SEC_ABOVE:
			new_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
			break;
		case IEEE80211_HT_PARAM_CHA_SEC_BELOW:
			new_ch_offset = HAL_PRIME_CHNL_OFFSET_UPPER;
			break;
		default:
			new_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			break;
		}
	} else {
		new_bwmode = HT_CHANNEL_WIDTH_20;
		new_ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	}

	if (new_bwmode != pmlmeext->cur_bwmode ||
	    new_ch_offset != pmlmeext->cur_ch_offset) {
		pmlmeinfo->bwmode_updated = true;

		pmlmeext->cur_bwmode = new_bwmode;
		pmlmeext->cur_ch_offset = new_ch_offset;

		/* update HT info also */
		HT_info_handler23a(padapter, p);
	} else
		pmlmeinfo->bwmode_updated = false;

	if (pmlmeinfo->bwmode_updated) {
		struct sta_info *psta;
		struct wlan_bssid_ex *cur_network = &pmlmeinfo->network;
		struct sta_priv	*pstapriv = &padapter->stapriv;

		/* set_channel_bwmode23a(padapter, pmlmeext->cur_channel,
		   pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode); */

		/* update ap's stainfo */
		psta = rtw_get_stainfo23a(pstapriv, cur_network->MacAddress);
		if (psta) {
			struct ht_priv *phtpriv_sta = &psta->htpriv;

			if (phtpriv_sta->ht_option) {
				/*  bwmode */
				phtpriv_sta->bwmode = pmlmeext->cur_bwmode;
				phtpriv_sta->ch_offset =
					pmlmeext->cur_ch_offset;
			} else {
				phtpriv_sta->bwmode = HT_CHANNEL_WIDTH_20;
				phtpriv_sta->ch_offset =
					HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			}
		}
	}
}

void HT_caps_handler23a(struct rtw_adapter *padapter, u8 *p)
{
	unsigned int i;
	u8 rf_type;
	u8 max_AMPDU_len, min_MPDU_spacing;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ht_priv *phtpriv = &pmlmepriv->htpriv;
	struct ieee80211_ht_cap *cap;
	u8 *dstcap;

	if (!p)
		return;

	if (phtpriv->ht_option == false)
		return;

	pmlmeinfo->HT_caps_enable = 1;

	cap = &pmlmeinfo->ht_cap;
	dstcap = (u8 *)cap;
	for (i = 0; i < p[1]; i++) {
		if (i != 2) {
			dstcap[i] &= p[i + 2];
		} else {
			/* modify from  fw by Thomas 2010/11/17 */
			if ((cap->ampdu_params_info &
			     IEEE80211_HT_AMPDU_PARM_FACTOR) >
			    (p[i + 2] & IEEE80211_HT_AMPDU_PARM_FACTOR))
				max_AMPDU_len = p[i + 2] &
					IEEE80211_HT_AMPDU_PARM_FACTOR;
			else
				max_AMPDU_len = cap->ampdu_params_info &
					IEEE80211_HT_AMPDU_PARM_FACTOR;

			if ((cap->ampdu_params_info &
			     IEEE80211_HT_AMPDU_PARM_DENSITY) >
			    (p[i + 2] & IEEE80211_HT_AMPDU_PARM_DENSITY))
				min_MPDU_spacing = cap->ampdu_params_info &
					IEEE80211_HT_AMPDU_PARM_DENSITY;
			else
				min_MPDU_spacing = p[i + 2] &
					IEEE80211_HT_AMPDU_PARM_DENSITY;

			cap->ampdu_params_info =
				max_AMPDU_len | min_MPDU_spacing;
		}
	}

	rf_type = rtl8723a_get_rf_type(padapter);

	/* update the MCS rates */
	for (i = 0; i < IEEE80211_HT_MCS_MASK_LEN; i++) {
		if (rf_type == RF_1T1R || rf_type == RF_1T2R)
			cap->mcs.rx_mask[i] &= MCS_rate_1R23A[i];
		else
			cap->mcs.rx_mask[i] &= MCS_rate_2R23A[i];
	}
	return;
}

void HT_info_handler23a(struct rtw_adapter *padapter, const u8 *p)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ht_priv *phtpriv = &pmlmepriv->htpriv;

	if (!p)
		return;

	if (phtpriv->ht_option == false)
		return;

	if (p[1] != sizeof(struct ieee80211_ht_operation))
		return;

	pmlmeinfo->HT_info_enable = 1;
	memcpy(&pmlmeinfo->HT_info, p + 2, p[1]);
	return;
}

void HTOnAssocRsp23a(struct rtw_adapter *padapter)
{
	unsigned char max_AMPDU_len;
	unsigned char min_MPDU_spacing;
	/* struct registry_priv	 *pregpriv = &padapter->registrypriv; */
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	DBG_8723A("%s\n", __func__);

	if (pmlmeinfo->HT_info_enable && pmlmeinfo->HT_caps_enable)
		pmlmeinfo->HT_enable = 1;
	else {
		pmlmeinfo->HT_enable = 0;
		/* set_channel_bwmode23a(padapter, pmlmeext->cur_channel,
		   pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode); */
		return;
	}

	/* handle A-MPDU parameter field */
	/*
		AMPDU_para [1:0]:Max AMPDU Len => 0:8k , 1:16k, 2:32k, 3:64k
		AMPDU_para [4:2]:Min MPDU Start Spacing
	*/
	max_AMPDU_len = pmlmeinfo->ht_cap.ampdu_params_info &
		IEEE80211_HT_AMPDU_PARM_FACTOR;

	min_MPDU_spacing =
		(pmlmeinfo->ht_cap.ampdu_params_info &
		 IEEE80211_HT_AMPDU_PARM_DENSITY) >> 2;

	rtl8723a_set_ampdu_min_space(padapter, min_MPDU_spacing);
	rtl8723a_set_ampdu_factor(padapter, max_AMPDU_len);
}

void ERP_IE_handler23a(struct rtw_adapter *padapter, const u8 *p)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	if (p[1] > 1)
		return;

	pmlmeinfo->ERP_enable = 1;
	memcpy(&pmlmeinfo->ERP_IE, p + 2, p[1]);
}

void VCS_update23a(struct rtw_adapter *padapter, struct sta_info *psta)
{
	struct registry_priv *pregpriv = &padapter->registrypriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

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
		if (pmlmeinfo->ERP_enable && pmlmeinfo->ERP_IE & BIT(1)) {
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

int rtw_check_bcn_info23a(struct rtw_adapter *Adapter,
			  struct ieee80211_mgmt *mgmt, u32 pkt_len)
{
	struct wlan_network *cur_network = &Adapter->mlmepriv.cur_network;
	struct ieee80211_ht_operation *pht_info;
	struct wlan_bssid_ex *bssid;
	unsigned short val16;
	u16 wpa_len = 0, rsn_len = 0;
	u8 encryp_protocol;
	int group_cipher = 0, pairwise_cipher = 0, is_8021x = 0, r;
	u32 bcn_channel;
	int len, pie_len, ie_offset;
	const u8 *p;
	u8 *pie;

	if (is_client_associated_to_ap23a(Adapter) == false)
		return true;

	if (unlikely(!ieee80211_is_beacon(mgmt->frame_control))) {
		printk(KERN_WARNING "%s: received a non beacon frame!\n",
		       __func__);
		return false;
	}

	len = pkt_len - sizeof(struct ieee80211_hdr_3addr);

	if (len > MAX_IE_SZ) {
		DBG_8723A("%s IE too long for survey event\n", __func__);
		return _FAIL;
	}

	if (memcmp(cur_network->network.MacAddress, mgmt->bssid, 6)) {
		DBG_8723A("Oops: rtw_check_network_encrypt linked but recv "
			  "other bssid bcn\n" MAC_FMT MAC_FMT,
			  MAC_ARG(mgmt->bssid),
			  MAC_ARG(cur_network->network.MacAddress));
		return true;
	}

	bssid = kzalloc(sizeof(struct wlan_bssid_ex), GFP_ATOMIC);
	if (!bssid)
		return _FAIL;

	bssid->reserved = 1;

	bssid->Length = offsetof(struct wlan_bssid_ex, IEs) + len;

	/* below is to copy the information element */
	bssid->IELength = len;
	memcpy(bssid->IEs, &mgmt->u, len);

	/* check bw and channel offset */
	/* parsing HT_CAP_IE */
	ie_offset = offsetof(struct ieee80211_mgmt, u.beacon.variable) -
		offsetof(struct ieee80211_mgmt, u);
	pie = bssid->IEs + ie_offset;
	pie_len = pkt_len - ie_offset;

	/* Checking for channel */
	p = cfg80211_find_ie(WLAN_EID_DS_PARAMS, pie, pie_len);
	if (p)
		bcn_channel = p[2];
	else {
		/* In 5G, some ap do not have DSSET IE checking HT
		   info for channel */
		p = cfg80211_find_ie(WLAN_EID_HT_OPERATION, pie, pie_len);

		if (p && p[1] > 0) {
			pht_info = (struct ieee80211_ht_operation *)(p + 2);
			bcn_channel = pht_info->primary_chan;
		} else { /* we don't find channel IE, so don't check it */
			DBG_8723A("Oops: %s we don't find channel IE, so don't "
				  "check it\n", __func__);
			bcn_channel = Adapter->mlmeextpriv.cur_channel;
		}
	}
	if (bcn_channel != Adapter->mlmeextpriv.cur_channel) {
		DBG_8723A("%s beacon channel:%d cur channel:%d disconnect\n",
			  __func__, bcn_channel,
			  Adapter->mlmeextpriv.cur_channel);
		goto _mismatch;
	}

	/* checking SSID */
	p = cfg80211_find_ie(WLAN_EID_SSID, pie, pie_len);
	if (p && p[1]) {
		memcpy(bssid->Ssid.ssid, p + 2, p[1]);
		bssid->Ssid.ssid_len = p[1];
	} else {
		DBG_8723A("%s marc: cannot find SSID for survey event\n",
			  __func__);
		bssid->Ssid.ssid_len = 0;
		bssid->Ssid.ssid[0] = '\0';
	}

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("%s bssid.Ssid.Ssid:%s bssid.Ssid.SsidLength:%d "
		  "cur_network->network.Ssid.Ssid:%s len:%d\n", __func__,
		  bssid->Ssid.ssid, bssid->Ssid.ssid_len,
		  cur_network->network.Ssid.ssid,
		  cur_network->network.Ssid.ssid_len));

	if (memcmp(bssid->Ssid.ssid, cur_network->network.Ssid.ssid, 32) ||
	    bssid->Ssid.ssid_len != cur_network->network.Ssid.ssid_len) {
		if (bssid->Ssid.ssid[0] != '\0' &&
		    bssid->Ssid.ssid_len != 0) { /* not hidden ssid */
			DBG_8723A("%s(), SSID is not match return FAIL\n",
				  __func__);
			goto _mismatch;
		}
	}

	/* check encryption info */
	val16 = rtw_get_capability23a(bssid);

	if (val16 & WLAN_CAPABILITY_PRIVACY)
		bssid->Privacy = 1;
	else
		bssid->Privacy = 0;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("%s(): cur_network->network.Privacy is %d, bssid.Privacy "
		  "is %d\n", __func__, cur_network->network.Privacy,
		  bssid->Privacy));
	if (cur_network->network.Privacy != bssid->Privacy) {
		DBG_8723A("%s(), privacy is not match return FAIL\n", __func__);
		goto _mismatch;
	}

	rtw_get_sec_ie23a(bssid->IEs, bssid->IELength, NULL, &rsn_len, NULL,
			  &wpa_len);

	if (rsn_len > 0)
		encryp_protocol = ENCRYP_PROTOCOL_WPA2;
	else if (wpa_len > 0)
		encryp_protocol = ENCRYP_PROTOCOL_WPA;
	else {
		if (bssid->Privacy)
			encryp_protocol = ENCRYP_PROTOCOL_WEP;
		else
			encryp_protocol = ENCRYP_PROTOCOL_OPENSYS;
	}

	if (cur_network->BcnInfo.encryp_protocol != encryp_protocol) {
		DBG_8723A("%s(): enctyp is not match, return FAIL\n", __func__);
		goto _mismatch;
	}

	if (encryp_protocol == ENCRYP_PROTOCOL_WPA ||
	    encryp_protocol == ENCRYP_PROTOCOL_WPA2) {
		p = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
					    WLAN_OUI_TYPE_MICROSOFT_WPA,
					    pie, pie_len);
		if (p && p[1] > 0) {
			r = rtw_parse_wpa_ie23a(p, p[1] + 2, &group_cipher,
						&pairwise_cipher, &is_8021x);
			if (r == _SUCCESS)
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
					 ("%s pnetwork->pairwise_cipher: %d, "
					  "group_cipher is %d, is_8021x is "
					  "%d\n", __func__, pairwise_cipher,
					  group_cipher, is_8021x));
		} else {
			p = cfg80211_find_ie(WLAN_EID_RSN, pie, pie_len);

			if (p && p[1] > 0) {
				r = rtw_parse_wpa2_ie23a(p, p[1] + 2,
							 &group_cipher,
							 &pairwise_cipher,
							 &is_8021x);
				if (r == _SUCCESS)
					RT_TRACE(_module_rtl871x_mlme_c_,
						 _drv_info_,
						 ("%s pnetwork->pairwise_cipher"
						  ": %d, pnetwork->group_cipher"
						  " is %d, is_802x is %d\n",
						  __func__, pairwise_cipher,
						  group_cipher, is_8021x));
			}
		}

		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("%s cur_network->group_cipher is %d: %d\n", __func__,
			  cur_network->BcnInfo.group_cipher, group_cipher));
		if (pairwise_cipher != cur_network->BcnInfo.pairwise_cipher ||
		    group_cipher != cur_network->BcnInfo.group_cipher) {
			DBG_8723A("%s pairwise_cipher(%x:%x) or group_cipher "
				  "(%x:%x) is not match, return FAIL\n",
				  __func__, pairwise_cipher,
				  cur_network->BcnInfo.pairwise_cipher,
				  group_cipher,
				  cur_network->BcnInfo.group_cipher);
			goto _mismatch;
		}

		if (is_8021x != cur_network->BcnInfo.is_8021x) {
			DBG_8723A("%s authentication is not match, return "
				  "FAIL\n", __func__);
			goto _mismatch;
		}
	}

	kfree(bssid);
	return _SUCCESS;

_mismatch:
	kfree(bssid);

	return _FAIL;
}

void update_beacon23a_info(struct rtw_adapter *padapter,
			   struct ieee80211_mgmt *mgmt,
			   uint pkt_len, struct sta_info *psta)
{
	unsigned int len;
	const u8 *p;

	len = pkt_len - offsetof(struct ieee80211_mgmt, u.beacon.variable);

	p = cfg80211_find_ie(WLAN_EID_HT_OPERATION, mgmt->u.beacon.variable,
			     len);
	if (p)
		bwmode_update_check(padapter, p);

	p = cfg80211_find_ie(WLAN_EID_ERP_INFO, mgmt->u.beacon.variable, len);
	if (p) {
		ERP_IE_handler23a(padapter, p);
		VCS_update23a(padapter, psta);
	}
}

bool is_ap_in_tkip23a(struct rtw_adapter *padapter)
{
	u32 i;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *cur_network = &pmlmeinfo->network;
	const u8 *p;
	int bcn_fixed_size;

	bcn_fixed_size = offsetof(struct ieee80211_mgmt, u.beacon.variable) -
		offsetof(struct ieee80211_mgmt, u.beacon);

	if (rtw_get_capability23a(cur_network) & WLAN_CAPABILITY_PRIVACY) {
		for (i = bcn_fixed_size; i < pmlmeinfo->network.IELength;) {
			p = pmlmeinfo->network.IEs + i;

			switch (p[0]) {
			case WLAN_EID_VENDOR_SPECIFIC:
				if (!memcmp(p + 2, RTW_WPA_OUI23A_TYPE, 4) &&
				    !memcmp(p + 2 + 12, WPA_TKIP_CIPHER, 4))
					return true;
				break;
			case WLAN_EID_RSN:
				if (!memcmp(p + 2 + 8, RSN_TKIP_CIPHER, 4))
					return true;
				break;
			default:
				break;
			}
			i += (p[1] + 2);
		}
		return false;
	} else
		return false;
}

bool should_forbid_n_rate23a(struct rtw_adapter * padapter)
{
	u32 i;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_bssid_ex  *cur_network = &pmlmepriv->cur_network.network;
	const u8 *p;
	int bcn_fixed_size;

	bcn_fixed_size = offsetof(struct ieee80211_mgmt, u.beacon.variable) -
		offsetof(struct ieee80211_mgmt, u.beacon);

	if (rtw_get_capability23a(cur_network) & WLAN_CAPABILITY_PRIVACY) {
		for (i = bcn_fixed_size; i < cur_network->IELength;) {
			p = cur_network->IEs + i;

			switch (p[0]) {
			case WLAN_EID_VENDOR_SPECIFIC:
				if (!memcmp(p + 2, RTW_WPA_OUI23A_TYPE, 4) &&
				    (!memcmp(p + 2 + 12,
					     WPA_CIPHER_SUITE_CCMP23A, 4) ||
				     !memcmp(p + 2 + 16,
					     WPA_CIPHER_SUITE_CCMP23A, 4)))
					return false;
				break;
			case WLAN_EID_RSN:
				if (!memcmp(p + 2 + 8,
					    RSN_CIPHER_SUITE_CCMP23A, 4) ||
				    !memcmp(p + 2 + 12,
					    RSN_CIPHER_SUITE_CCMP23A, 4))
				return false;
			default:
				break;
			}

			i += (p[1] + 2);
		}
		return true;
	} else {
		return false;
	}
}

bool is_ap_in_wep23a(struct rtw_adapter *padapter)
{
	u32 i;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *cur_network = &pmlmeinfo->network;
	const u8 *p;
	int bcn_fixed_size;

	bcn_fixed_size = offsetof(struct ieee80211_mgmt, u.beacon.variable) -
		offsetof(struct ieee80211_mgmt, u.beacon);

	if (rtw_get_capability23a(cur_network) & WLAN_CAPABILITY_PRIVACY) {
		for (i = bcn_fixed_size; i < pmlmeinfo->network.IELength;) {
			p = pmlmeinfo->network.IEs + i;

			switch (p[0]) {
			case WLAN_EID_VENDOR_SPECIFIC:
				if (!memcmp(p + 2, RTW_WPA_OUI23A_TYPE, 4))
					return false;
				break;
			case WLAN_EID_RSN:
				return false;

			default:
				break;
			}

			i += (p[1] + 2);
		}

		return true;
	} else
		return false;
}

static int wifirate2_ratetbl_inx23a(unsigned char rate)
{
	int inx = 0;
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

unsigned int update_basic_rate23a(unsigned char *ptn, unsigned int ptn_sz)
{
	unsigned int i, num_of_rate;
	unsigned int mask = 0;

	num_of_rate = (ptn_sz > NumRates)? NumRates: ptn_sz;

	for (i = 0; i < num_of_rate; i++) {
		if ((*(ptn + i)) & 0x80)
			mask |= 0x1 << wifirate2_ratetbl_inx23a(*(ptn + i));
	}
	return mask;
}

unsigned int update_supported_rate23a(unsigned char *ptn, unsigned int ptn_sz)
{
	unsigned int i, num_of_rate;
	unsigned int mask = 0;

	num_of_rate = (ptn_sz > NumRates) ? NumRates : ptn_sz;

	for (i = 0; i < num_of_rate; i++)
		mask |= 0x1 << wifirate2_ratetbl_inx23a(*(ptn + i));
	return mask;
}

unsigned int update_MSC_rate23a(struct ieee80211_ht_cap *pHT_caps)
{
	unsigned int mask = 0;

	mask = pHT_caps->mcs.rx_mask[0] << 12 |
		pHT_caps->mcs.rx_mask[1] << 20;

	return mask;
}

int support_short_GI23a(struct rtw_adapter *padapter,
			struct ieee80211_ht_cap *pHT_caps)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	unsigned char bit_offset;

	if (!pmlmeinfo->HT_enable)
		return _FAIL;
	if (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_RALINK)
		return _FAIL;
	bit_offset = (pmlmeext->cur_bwmode & HT_CHANNEL_WIDTH_40)? 6: 5;

	if (pHT_caps->cap_info & cpu_to_le16(0x1 << bit_offset))
		return _SUCCESS;
	else
		return _FAIL;
}

unsigned char get_highest_rate_idx23a(u32 mask)
{
	int i;
	unsigned char rate_idx = 0;

	for (i = 27; i >= 0; i--) {
		if (mask & BIT(i)) {
			rate_idx = i;
			break;
		}
	}
	return rate_idx;
}

void Update_RA_Entry23a(struct rtw_adapter *padapter, struct sta_info *psta)
{
	rtw_hal_update_ra_mask23a(psta, 0);
}

static void enable_rate_adaptive(struct rtw_adapter *padapter,
				 struct sta_info *psta)
{
	Update_RA_Entry23a(padapter, psta);
}

void set_sta_rate23a(struct rtw_adapter *padapter, struct sta_info *psta)
{
	/* rate adaptive */
	enable_rate_adaptive(padapter, psta);
}

/*  Update RRSR and Rate for USERATE */
void update_tx_basic_rate23a(struct rtw_adapter *padapter, u8 wirelessmode)
{
	unsigned char supported_rates[NDIS_802_11_LENGTH_RATES_EX];

	memset(supported_rates, 0, NDIS_802_11_LENGTH_RATES_EX);

	if (wirelessmode == WIRELESS_11B) {
		memcpy(supported_rates, rtw_basic_rate_cck, 4);
	} else if (wirelessmode & WIRELESS_11B) {
		memcpy(supported_rates, rtw_basic_rate_mix, 7);
	} else {
		memcpy(supported_rates, rtw_basic_rate_ofdm, 3);
	}

	if (wirelessmode & WIRELESS_11B)
		update_mgnt_tx_rate23a(padapter, IEEE80211_CCK_RATE_1MB);
	else
		update_mgnt_tx_rate23a(padapter, IEEE80211_OFDM_RATE_6MB);

	HalSetBrateCfg23a(padapter, supported_rates);
}

unsigned char check_assoc_AP23a(u8 *pframe, uint len)
{
	int i, bcn_fixed_size;
	u8 epigram_vendor_flag;
	u8 ralink_vendor_flag;
	const u8 *p;
	epigram_vendor_flag = 0;
	ralink_vendor_flag = 0;

	bcn_fixed_size = offsetof(struct ieee80211_mgmt, u.beacon.variable) -
		offsetof(struct ieee80211_mgmt, u.beacon);

	for (i = bcn_fixed_size; i < len;) {
		p = pframe + i;

		switch (p[0]) {
		case WLAN_EID_VENDOR_SPECIFIC:
			if (!memcmp(p + 2, ARTHEROS_OUI1, 3) ||
			    !memcmp(p + 2, ARTHEROS_OUI2, 3)) {
				DBG_8723A("link to Artheros AP\n");
				return HT_IOT_PEER_ATHEROS;
			} else if (!memcmp(p + 2, BROADCOM_OUI1, 3) ||
				   !memcmp(p + 2, BROADCOM_OUI2, 3) ||
				   !memcmp(p + 2, BROADCOM_OUI2, 3)) {
				DBG_8723A("link to Broadcom AP\n");
				return HT_IOT_PEER_BROADCOM;
			} else if (!memcmp(p + 2, MARVELL_OUI, 3)) {
				DBG_8723A("link to Marvell AP\n");
				return HT_IOT_PEER_MARVELL;
			} else if (!memcmp(p + 2, RALINK_OUI, 3)) {
				if (!ralink_vendor_flag)
					ralink_vendor_flag = 1;
				else {
					DBG_8723A("link to Ralink AP\n");
					return HT_IOT_PEER_RALINK;
				}
			} else if (!memcmp(p + 2, CISCO_OUI, 3)) {
				DBG_8723A("link to Cisco AP\n");
				return HT_IOT_PEER_CISCO;
			} else if (!memcmp(p + 2, REALTEK_OUI, 3)) {
				DBG_8723A("link to Realtek 96B\n");
				return HT_IOT_PEER_REALTEK;
			} else if (!memcmp(p + 2, AIRGOCAP_OUI, 3)) {
				DBG_8723A("link to Airgo Cap\n");
				return HT_IOT_PEER_AIRGO;
			} else if (!memcmp(p + 2, EPIGRAM_OUI, 3)) {
				epigram_vendor_flag = 1;
				if (ralink_vendor_flag) {
					DBG_8723A("link to Tenda W311R AP\n");
					return HT_IOT_PEER_TENDA;
				} else
					DBG_8723A("Capture EPIGRAM_OUI\n");
			} else
				break;
		default:
			break;
		}

		i += (p[1] + 2);
	}

	if (ralink_vendor_flag && !epigram_vendor_flag) {
		DBG_8723A("link to Ralink AP\n");
		return HT_IOT_PEER_RALINK;
	} else if (ralink_vendor_flag && epigram_vendor_flag) {
		DBG_8723A("link to Tenda W311R AP\n");
		return HT_IOT_PEER_TENDA;
	} else {
		DBG_8723A("link to new AP\n");
		return HT_IOT_PEER_UNKNOWN;
	}
}

void update_IOT_info23a(struct rtw_adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	switch (pmlmeinfo->assoc_AP_vendor) {
	case HT_IOT_PEER_MARVELL:
		pmlmeinfo->turboMode_cts2self = 1;
		pmlmeinfo->turboMode_rtsen = 0;
		break;
	case HT_IOT_PEER_RALINK:
		pmlmeinfo->turboMode_cts2self = 0;
		pmlmeinfo->turboMode_rtsen = 1;
		/* disable high power */
		rtl8723a_odm_support_ability_clr(padapter, (u32)
						 ~DYNAMIC_BB_DYNAMIC_TXPWR);
		break;
	case HT_IOT_PEER_REALTEK:
		/* rtw_write16(padapter, 0x4cc, 0xffff); */
		/* rtw_write16(padapter, 0x546, 0x01c0); */
		/* disable high power */
		rtl8723a_odm_support_ability_clr(padapter, (u32)
						 ~DYNAMIC_BB_DYNAMIC_TXPWR);
		break;
	default:
		pmlmeinfo->turboMode_cts2self = 0;
		pmlmeinfo->turboMode_rtsen = 1;
		break;
	}
}

void update_capinfo23a(struct rtw_adapter *Adapter, u16 updateCap)
{
	struct mlme_ext_priv *pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	if (updateCap & cShortPreamble) {
		/*  Short Preamble */
		if (pmlmeinfo->preamble_mode != PREAMBLE_SHORT) {
			/*  PREAMBLE_LONG or PREAMBLE_AUTO */
			pmlmeinfo->preamble_mode = PREAMBLE_SHORT;
			rtl8723a_ack_preamble(Adapter, true);
		}
	} else { /*  Long Preamble */
		if (pmlmeinfo->preamble_mode != PREAMBLE_LONG) {
			/*  PREAMBLE_SHORT or PREAMBLE_AUTO */
			pmlmeinfo->preamble_mode = PREAMBLE_LONG;
			rtl8723a_ack_preamble(Adapter, false);
		}
	}
	if (updateCap & cIBSS) {
		/* Filen: See 802.11-2007 p.91 */
		pmlmeinfo->slotTime = NON_SHORT_SLOT_TIME;
	} else {
		/* Filen: See 802.11-2007 p.90 */
		if (pmlmeext->cur_wireless_mode &
		    (WIRELESS_11G | WIRELESS_11_24N)) {
			if (updateCap & cShortSlotTime) { /*  Short Slot Time */
				if (pmlmeinfo->slotTime != SHORT_SLOT_TIME)
					pmlmeinfo->slotTime = SHORT_SLOT_TIME;
			} else { /*  Long Slot Time */
				if (pmlmeinfo->slotTime != NON_SHORT_SLOT_TIME)
					pmlmeinfo->slotTime =
						NON_SHORT_SLOT_TIME;
			}
		} else if (pmlmeext->cur_wireless_mode &
			   (WIRELESS_11A | WIRELESS_11_5N)) {
			pmlmeinfo->slotTime = SHORT_SLOT_TIME;
		} else {
			/* B Mode */
			pmlmeinfo->slotTime = NON_SHORT_SLOT_TIME;
		}
	}
	rtl8723a_set_slot_time(Adapter, pmlmeinfo->slotTime);
}

void update_wireless_mode23a(struct rtw_adapter *padapter)
{
	int ratelen, network_type = 0;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *cur_network = &pmlmeinfo->network;
	unsigned char *rate = cur_network->SupportedRates;

	ratelen = rtw_get_rateset_len23a(cur_network->SupportedRates);

	if ((pmlmeinfo->HT_info_enable) && (pmlmeinfo->HT_caps_enable))
		pmlmeinfo->HT_enable = 1;

	if (pmlmeext->cur_channel > 14) {
		if (pmlmeinfo->HT_enable)
			network_type = WIRELESS_11_5N;
		network_type |= WIRELESS_11A;
	} else {
		if (pmlmeinfo->HT_enable)
			network_type = WIRELESS_11_24N;

		if (cckratesonly_included23a(rate, ratelen) == true)
			network_type |= WIRELESS_11B;
		else if (cckrates_included23a(rate, ratelen) == true)
			network_type |= WIRELESS_11BG;
		else
			network_type |= WIRELESS_11G;
	}

	pmlmeext->cur_wireless_mode =
		network_type & padapter->registrypriv.wireless_mode;

	/* 0x0808 -> for CCK, 0x0a0a -> for OFDM */
	/* change this value if having IOT issues. */
	rtl8723a_set_resp_sifs(padapter, 0x08, 0x08, 0x0a, 0x0a);

	if (pmlmeext->cur_wireless_mode & WIRELESS_11B)
		update_mgnt_tx_rate23a(padapter, IEEE80211_CCK_RATE_1MB);
	 else
		update_mgnt_tx_rate23a(padapter, IEEE80211_OFDM_RATE_6MB);
}

void update_bmc_sta_support_rate23a(struct rtw_adapter *padapter, u32 mac_id)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	if (pmlmeext->cur_wireless_mode & WIRELESS_11B) {
		/*  Only B, B/G, and B/G/N AP could use CCK rate */
		memcpy((pmlmeinfo->FW_sta_info[mac_id].SupportedRates),
		       rtw_basic_rate_cck, 4);
	} else {
		memcpy(pmlmeinfo->FW_sta_info[mac_id].SupportedRates,
		       rtw_basic_rate_ofdm, 3);
	}
}

int update_sta_support_rate23a(struct rtw_adapter *padapter, u8 *pvar_ie,
			       uint var_ie_len, int cam_idx)
{
	int supportRateNum = 0;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	const u8 *p;

	p = cfg80211_find_ie(WLAN_EID_SUPP_RATES, pvar_ie, var_ie_len);
	if (!p)
		return _FAIL;

	memcpy(pmlmeinfo->FW_sta_info[cam_idx].SupportedRates, p + 2, p[1]);
	supportRateNum = p[1];

	p = cfg80211_find_ie(WLAN_EID_EXT_SUPP_RATES, pvar_ie, var_ie_len);
	if (p)
		memcpy(pmlmeinfo->FW_sta_info[cam_idx].SupportedRates +
		       supportRateNum, p + 2, p[1]);
	return _SUCCESS;
}

void process_addba_req23a(struct rtw_adapter *padapter,
			  u8 *paddba_req, u8 *addr)
{
	struct sta_info *psta;
	u16 tid, start_seq, param;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct ADDBA_request *preq = (struct ADDBA_request*)paddba_req;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	psta = rtw_get_stainfo23a(pstapriv, addr);

	if (psta) {
		start_seq = le16_to_cpu(preq->BA_starting_seqctrl) >> 4;

		param = le16_to_cpu(preq->BA_para_set);
		tid = (param >> 2) & 0x0f;

		preorder_ctrl = &psta->recvreorder_ctrl[tid];

		preorder_ctrl->indicate_seq = 0xffff;

		preorder_ctrl->enable = (pmlmeinfo->bAcceptAddbaReq == true) ?
			true : false;
	}
}
