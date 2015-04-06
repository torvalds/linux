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
 *
 ******************************************************************************/
#define _RTW_WLAN_UTIL_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <wifi.h>

static unsigned char ARTHEROS_OUI1[] = {0x00, 0x03, 0x7f};
static unsigned char ARTHEROS_OUI2[] = {0x00, 0x13, 0x74};

static unsigned char BROADCOM_OUI1[] = {0x00, 0x10, 0x18};
static unsigned char BROADCOM_OUI2[] = {0x00, 0x0a, 0xf7};
static unsigned char BROADCOM_OUI3[] = {0x00, 0x05, 0xb5};

static unsigned char CISCO_OUI[] = {0x00, 0x40, 0x96};
static unsigned char MARVELL_OUI[] = {0x00, 0x50, 0x43};
static unsigned char RALINK_OUI[] = {0x00, 0x0c, 0x43};
static unsigned char REALTEK_OUI[] = {0x00, 0xe0, 0x4c};
static unsigned char AIRGOCAP_OUI[] = {0x00, 0x0a, 0xf5};

unsigned char REALTEK_96B_IE[] = {0x00, 0xe0, 0x4c, 0x02, 0x01, 0x20};

#define R2T_PHY_DELAY	(0)

/* define WAIT_FOR_BCN_TO_MIN	(3000) */
#define WAIT_FOR_BCN_TO_MIN	(6000)
#define WAIT_FOR_BCN_TO_MAX	(20000)

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

int cckrates_included(unsigned char *rate, int ratelen)
{
	int	i;

	for (i = 0; i < ratelen; i++) {
		if ((((rate[i]) & 0x7f) == 2)	|| (((rate[i]) & 0x7f) == 4) ||
		    (((rate[i]) & 0x7f) == 11)  || (((rate[i]) & 0x7f) == 22))
			return true;
	}

	return false;
}

int cckratesonly_included(unsigned char *rate, int ratelen)
{
	int	i;

	for (i = 0; i < ratelen; i++) {
		if ((((rate[i]) & 0x7f) != 2) && (((rate[i]) & 0x7f) != 4) &&
		    (((rate[i]) & 0x7f) != 11)  && (((rate[i]) & 0x7f) != 22))
			return false;
	}

	return true;
}

unsigned char networktype_to_raid(unsigned char network_type)
{
	unsigned char raid;

	switch (network_type) {
	case WIRELESS_11B:
		raid = 6;
		break;
	case WIRELESS_11A:
	case WIRELESS_11G:
		raid = 5;
		break;
	case WIRELESS_11BG:
		raid = 4;
		break;
	case WIRELESS_11_24N:
	case WIRELESS_11_5N:
		raid = 3;
		break;
	case WIRELESS_11A_5N:
	case WIRELESS_11G_24N:
		raid = 1;
		break;
	case WIRELESS_11BG_24N:
		raid = 0;
		break;
	default:
		raid = 4;
		break;
	}

	return raid;
}

int judge_network_type(struct rtw_adapter *padapter, unsigned char *rate, int ratelen)
{
	int network_type = 0;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (pmlmeext->cur_channel > 14) {
		if (pmlmeinfo->HT_enable)
			network_type = WIRELESS_11_5N;

		network_type |= WIRELESS_11A;
	} else {
		if (pmlmeinfo->HT_enable)
			network_type = WIRELESS_11_24N;

		if ((cckratesonly_included(rate, ratelen)) == true)
			network_type |= WIRELESS_11B;
		else if ((cckrates_included(rate, ratelen)) == true)
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

	for (i = 0; i < NUMRATES; i++) {
		val = pmlmeext->basicrate[i];

		if ((val != 0xff) && (val != 0xfe)) {
			if (rate == ratetbl_val_2wifirate(val))
				return true;
		}
	}

	return false;
}

static unsigned int ratetbl2rateset(struct rtw_adapter *padapter, unsigned char *rateset)
{
	int i;
	unsigned char rate;
	unsigned int	len = 0;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	for (i = 0; i < NUMRATES; i++) {
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

void get_rate_set(struct rtw_adapter *padapter, unsigned char *pbssrate, int *bssrate_len)
{
	unsigned char supportedrates[NUMRATES];

	memset(supportedrates, 0, NUMRATES);
	*bssrate_len = ratetbl2rateset(padapter, supportedrates);
	memcpy(pbssrate, supportedrates, *bssrate_len);
}

void UpdateBrateTbl(
	struct rtw_adapter *adapter,
	u8			*mBratesOS
)
{
	u8	i;
	u8	rate;

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

void Save_DM_Func_Flag(struct rtw_adapter *padapter)
{
	u8	bSaveFlag = true;

#ifdef CONFIG_CONCURRENT_MODE
	struct rtw_adapter *pbuddy_adapter = padapter->pbuddy_adapter;
	if (pbuddy_adapter)
	rtw_hal_set_hwreg(pbuddy_adapter, HW_VAR_DM_FUNC_OP, (u8 *)(&bSaveFlag));
#endif

	rtw_hal_set_hwreg(padapter, HW_VAR_DM_FUNC_OP, (u8 *)(&bSaveFlag));
}

void Restore_DM_Func_Flag(struct rtw_adapter *padapter)
{
	u8	bSaveFlag = false;
#ifdef CONFIG_CONCURRENT_MODE
	struct rtw_adapter *pbuddy_adapter = padapter->pbuddy_adapter;
	if (pbuddy_adapter)
	rtw_hal_set_hwreg(pbuddy_adapter, HW_VAR_DM_FUNC_OP, (u8 *)(&bSaveFlag));
#endif
	rtw_hal_set_hwreg(padapter, HW_VAR_DM_FUNC_OP, (u8 *)(&bSaveFlag));
}

void Switch_DM_Func(struct rtw_adapter *padapter, u8 mode, u8 enable)
{
#ifdef CONFIG_CONCURRENT_MODE
	struct rtw_adapter *pbuddy_adapter = padapter->pbuddy_adapter;
#endif

	if (enable == true) {
#ifdef CONFIG_CONCURRENT_MODE
		if (pbuddy_adapter)
		rtw_hal_set_hwreg(pbuddy_adapter, HW_VAR_DM_FUNC_SET, (u8 *)(&mode));
#endif
		rtw_hal_set_hwreg(padapter, HW_VAR_DM_FUNC_SET, (u8 *)(&mode));
	} else {
#ifdef CONFIG_CONCURRENT_MODE
		if (pbuddy_adapter)
		rtw_hal_set_hwreg(pbuddy_adapter, HW_VAR_DM_FUNC_CLR, (u8 *)(&mode));
#endif
		rtw_hal_set_hwreg(padapter, HW_VAR_DM_FUNC_CLR, (u8 *)(&mode));
	}
}

static void Set_NETYPE1_MSR(struct rtw_adapter *padapter, u8 type)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_MEDIA_STATUS1, (u8 *)(&type));
}

static void Set_NETYPE0_MSR(struct rtw_adapter *padapter, u8 type)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_MEDIA_STATUS, (u8 *)(&type));
}

void Set_MSR(struct rtw_adapter *padapter, u8 type)
{
#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->iface_type == IFACE_PORT1)
		Set_NETYPE1_MSR(padapter, type);
	else
#endif
		Set_NETYPE0_MSR(padapter, type);
}

inline u8 rtw_get_oper_ch(struct rtw_adapter *adapter)
{
	return adapter_to_dvobj(adapter)->oper_channel;
}

inline void rtw_set_oper_ch(struct rtw_adapter *adapter, u8 ch)
{
	adapter_to_dvobj(adapter)->oper_channel = ch;
}

inline u8 rtw_get_oper_bw(struct rtw_adapter *adapter)
{
	return adapter_to_dvobj(adapter)->oper_bwmode;
}

inline void rtw_set_oper_bw(struct rtw_adapter *adapter, u8 bw)
{
	adapter_to_dvobj(adapter)->oper_bwmode = bw;
}

inline u8 rtw_get_oper_choffset(struct rtw_adapter *adapter)
{
	return adapter_to_dvobj(adapter)->oper_ch_offset;
}

inline void rtw_set_oper_choffset(struct rtw_adapter *adapter, u8 offset)
{
	adapter_to_dvobj(adapter)->oper_ch_offset = offset;
}

void SelectChannel(struct rtw_adapter *padapter, unsigned char channel)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

#ifdef CONFIG_DUALMAC_CONCURRENT
	/* saved channel info */
	rtw_set_oper_ch(padapter, channel);
	dc_SelectChannel(padapter, channel);
#else /* CONFIG_DUALMAC_CONCURRENT */

	_enter_critical_mutex(&(adapter_to_dvobj(padapter)->setch_mutex));

	/* saved channel info */
	rtw_set_oper_ch(padapter, channel);

	rtw_hal_set_chan(padapter, channel);

	_exit_critical_mutex(&(adapter_to_dvobj(padapter)->setch_mutex));

#endif /*  CONFIG_DUALMAC_CONCURRENT */
}

void SetBWMode(struct rtw_adapter *padapter, unsigned short bwmode, unsigned char channel_offset)
{
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

#ifdef CONFIG_DUALMAC_CONCURRENT
	/* saved bw info */
	rtw_set_oper_bw(padapter, bwmode);
	rtw_set_oper_choffset(padapter, channel_offset);
	dc_SetBWMode(padapter, bwmode, channel_offset);
#else /* CONFIG_DUALMAC_CONCURRENT */

	_enter_critical_mutex(&(adapter_to_dvobj(padapter)->setbw_mutex));

	/* saved bw info */
	rtw_set_oper_bw(padapter, bwmode);
	rtw_set_oper_choffset(padapter, channel_offset);

	rtw_hal_set_bwmode(padapter, (enum HT_CHANNEL_WIDTH)bwmode, channel_offset);

	_exit_critical_mutex(&(adapter_to_dvobj(padapter)->setbw_mutex));

#endif /*  CONFIG_DUALMAC_CONCURRENT */
}

void set_channel_bwmode(struct rtw_adapter *padapter, unsigned char channel, unsigned char channel_offset, unsigned short bwmode)
{
	u8 center_ch;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	if (padapter->bNotifyChannelChange)
		DBG_8192D("[%s] ch = %d, offset = %d, bwmode = %d\n", __func__, channel, channel_offset, bwmode);

	if ((bwmode == HT_CHANNEL_WIDTH_20) ||
	    (channel_offset == HAL_PRIME_CHNL_OFFSET_DONT_CARE)) {
		/* SelectChannel(padapter, channel); */
		center_ch = channel;
	} else {
		/* switch to the proper channel */
		if (channel_offset == HAL_PRIME_CHNL_OFFSET_LOWER) {
			/* SelectChannel(padapter, channel + 2); */
			center_ch = channel + 2;
		} else {
			/* SelectChannel(padapter, channel - 2); */
			center_ch = channel - 2;
		}
	}

	/* set Channel , must be independant for correct co_ch value/ */
#ifdef CONFIG_DUALMAC_CONCURRENT
	/* saved channel/bw info */
	rtw_set_oper_ch(padapter, channel);
	rtw_set_oper_bw(padapter, bwmode);
	rtw_set_oper_choffset(padapter, channel_offset);
	dc_SelectChannel(padapter, center_ch);/*  set center channel */
#else /* CONFIG_DUALMAC_CONCURRENT */

	if (!mutex_is_locked(&(adapter_to_dvobj(padapter)->hw_init_mutex)))
		_enter_critical_mutex(&(adapter_to_dvobj(padapter)->setch_mutex));

	/* saved channel/bw info */
	rtw_set_oper_ch(padapter, channel);
	rtw_set_oper_bw(padapter, bwmode);
	rtw_set_oper_choffset(padapter, channel_offset);

	rtw_hal_set_chan(padapter, center_ch);

	if (!mutex_is_locked(&(adapter_to_dvobj(padapter)->hw_init_mutex)))
		_exit_critical_mutex(&(adapter_to_dvobj(padapter)->setch_mutex));

#endif /*  CONFIG_DUALMAC_CONCURRENT */

	/* set BandWidth */
	SetBWMode(padapter, bwmode, channel_offset);
}

int get_bsstype(unsigned short capability)
{
	if (capability & BIT(0))
		return WIFI_FW_AP_STATE;
	else if (capability & BIT(1))
		return WIFI_FW_ADHOC_STATE;
	else
		return 0;
}

inline u8 *get_my_bssid(struct wlan_bssid_ex *pnetwork)
{
	return pnetwork->MacAddress;
}

u16 get_beacon_interval(struct wlan_bssid_ex *bss)
{
	__le16 val;
	memcpy((unsigned char *)&val, rtw_get_beacon_interval_from_ie(bss->IEs), 2);

	return le16_to_cpu(val);
}

int is_client_associated_to_ap(struct rtw_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext;
	struct mlme_ext_info	*pmlmeinfo;

	if (!padapter)
		return _FAIL;

	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &(pmlmeext->mlmext_info);

	if ((pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) && ((pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE))
		return true;
	else
		return _FAIL;
}

int is_client_associated_to_ibss(struct rtw_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if ((pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) && ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE))
		return true;
	else
		return _FAIL;
}

int is_IBSS_empty(struct rtw_adapter *padapter)
{
	unsigned int i;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	for (i = IBSS_START_MAC_ID; i < NUM_STA; i++) {
		if (pmlmeinfo->FW_sta_info[i].status == 1)
			return _FAIL;
	}
	return true;
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

void CAM_empty_entry(struct rtw_adapter *adapter, u8 ucIndex)
{
	rtw_hal_set_hwreg(adapter, HW_VAR_CAM_EMPTY_ENTRY, (u8 *)(&ucIndex));
}

void invalidate_cam_all(struct rtw_adapter *padapter)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_CAM_INVALID_ALL, NULL);
}

void write_cam(struct rtw_adapter *padapter, u8 entry, u16 ctrl, u8 *mac, u8 *key)
{
	unsigned int	i, val, addr;
	/* unsigned int    cmd; */
	int j;
	u32	cam_val[2];

	addr = entry << 3;

	for (j = 5; j >= 0; j--) {
		switch (j) {
		case 0:
			val = (ctrl | (mac[0] << 16) | (mac[1] << 24));
			break;
		case 1:
			val = (mac[2] | (mac[3] << 8) | (mac[4] << 16) | (mac[5] << 24));
			break;
		default:
			i = (j - 2) << 2;
			val = (key[i] | (key[i+1] << 8) | (key[i+2] << 16) | (key[i+3] << 24));
			break;
		}

		cam_val[0] = val;
		cam_val[1] = addr + (unsigned int)j;

		rtw_hal_set_hwreg(padapter, HW_VAR_CAM_WRITE, (u8 *)cam_val);
	}
}

void clear_cam_entry(struct rtw_adapter *padapter, u8 entry)
{
	unsigned char null_sta[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	unsigned char null_key[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	write_cam(padapter, entry, 0, null_sta, null_key);
}

int allocate_fw_sta_entry(struct rtw_adapter *padapter)
{
	unsigned int mac_id;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	for (mac_id = IBSS_START_MAC_ID; mac_id < NUM_STA; mac_id++) {
		if (pmlmeinfo->FW_sta_info[mac_id].status == 0) {
			pmlmeinfo->FW_sta_info[mac_id].status = 1;
			pmlmeinfo->FW_sta_info[mac_id].retry = 0;
			break;
		}
	}

	return mac_id;
}

void flush_all_cam_entry(struct rtw_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

#ifdef CONFIG_CONCURRENT_MODE

	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	/* if (check_buddy_mlmeinfo_state(padapter, _HW_STATE_NOLINK_)) */
	if (check_buddy_fwstate(padapter, _FW_LINKED) == false) {
		rtw_hal_set_hwreg(padapter, HW_VAR_CAM_INVALID_ALL, NULL);
	} else {
		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
			struct sta_priv	*pstapriv = &padapter->stapriv;
			struct sta_info	*psta;
			u8 cam_id;/* cam_entry */

			psta = rtw_get_stainfo(pstapriv, pmlmeinfo->network.MacAddress);
			if (psta) {
				if (psta->state & WIFI_AP_STATE) {
					/* clear cam when ap free per sta_info */
				} else {
					if (psta->mac_id == 2)
						cam_id = 5;
					else
						cam_id = 4;
				}
				/* clear_cam_entry(padapter, cam_id); */
				rtw_clearstakey_cmd(padapter, (u8 *)psta, cam_id, false);
			}
		} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) {
			/* clear cam when ap free per sta_info */
		}
	}
#else /* CONFIG_CONCURRENT_MODE */

	rtw_hal_set_hwreg(padapter, HW_VAR_CAM_INVALID_ALL, NULL);

#endif /* CONFIG_CONCURRENT_MODE */

	memset((u8 *)(pmlmeinfo->FW_sta_info), 0, sizeof(pmlmeinfo->FW_sta_info));
}

int WMM_param_handler(struct rtw_adapter *padapter, struct ndis_802_11_variable_ies *pIE)
{
	/* struct registry_priv	*pregpriv = &padapter->registrypriv; */
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (pmlmepriv->qospriv.qos_option == 0) {
		pmlmeinfo->WMM_enable = 0;
		return _FAIL;
	}

	pmlmeinfo->WMM_enable = 1;
	memcpy(&(pmlmeinfo->WMM_param), (pIE->data + 6), sizeof(struct WMM_para_element));
	return true;
}

void WMMOnAssocRsp(struct rtw_adapter *padapter)
{
	u8	ACI, ACM, AIFS, ECWMin, ECWMax, aSifsTime;
	u8	acm_mask;
	u16	TXOP;
	u32	acParm, i;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

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
		AIFS = (pmlmeinfo->WMM_param.ac_param[i].ACI_AIFSN & 0x0f) * pmlmeinfo->slotTime + aSifsTime;

		ECWMin = (pmlmeinfo->WMM_param.ac_param[i].CW & 0x0f);
		ECWMax = (pmlmeinfo->WMM_param.ac_param[i].CW & 0xf0) >> 4;
		TXOP = le16_to_cpu(pmlmeinfo->WMM_param.ac_param[i].TXOP_limit);

		acParm = AIFS | (ECWMin << 8) | (ECWMax << 12) | (TXOP << 16);

		switch (ACI) {
		case 0x0:
			rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BE, (u8 *)(&acParm));
			acm_mask |= (ACM ? BIT(1) : 0);
			break;
		case 0x1:
			rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acParm));
			break;
		case 0x2:
			rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VI, (u8 *)(&acParm));
			acm_mask |= (ACM ? BIT(2) : 0);
			break;
		case 0x3:
			rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_VO, (u8 *)(&acParm));
			acm_mask |= (ACM ? BIT(3) : 0);
			break;
		}

		DBG_8192D("WMM(%x): %x, %x\n", ACI, ACM, acParm);
	}

	if (padapter->registrypriv.acm_method == 1)
		rtw_hal_set_hwreg(padapter, HW_VAR_ACM_CTRL, (u8 *)(&acm_mask));
	else
		padapter->mlmepriv.acm_mask = acm_mask;

	return;
}

static void bwmode_update_check(struct rtw_adapter *padapter, struct ndis_802_11_variable_ies *pIE)
{
	unsigned char	 new_bwmode;
	unsigned char  new_ch_offset;
	struct HT_info_element	 *pHT_info;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct ht_priv			*phtpriv = &pmlmepriv->htpriv;
	u8 cbw40_enable = 0;

	if (!pIE)
		return;

	if (phtpriv->ht_option == false)
		return;

	if (pIE->Length > sizeof(struct HT_info_element))
		return;

	pHT_info = (struct HT_info_element *)pIE->data;

	if (pmlmeext->cur_channel > 14) {
		if (pregistrypriv->cbw40_enable & BIT(1))
			cbw40_enable = 1;
	} else {
		if (pregistrypriv->cbw40_enable & BIT(0))
			cbw40_enable = 1;
	}

	if ((pHT_info->infos[0] & BIT(2)) && cbw40_enable) {
		new_bwmode = HT_CHANNEL_WIDTH_40;

		switch (pHT_info->infos[0] & 0x3) {
		case 1:
			new_ch_offset = HAL_PRIME_CHNL_OFFSET_LOWER;
			break;
		case 3:
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

	if ((new_bwmode != pmlmeext->cur_bwmode) ||
	    (new_ch_offset != pmlmeext->cur_ch_offset)) {
		pmlmeinfo->bwmode_updated = true;

		pmlmeext->cur_bwmode = new_bwmode;
		pmlmeext->cur_ch_offset = new_ch_offset;

		/* update HT info also */
		HT_info_handler(padapter, pIE);
	} else {
		pmlmeinfo->bwmode_updated = false;
	}

	if (true == pmlmeinfo->bwmode_updated) {
		struct sta_info *psta;
		struct wlan_bssid_ex	*cur_network = &(pmlmeinfo->network);
		struct sta_priv	*pstapriv = &padapter->stapriv;

		/* update ap's stainfo */
		psta = rtw_get_stainfo(pstapriv, cur_network->MacAddress);
		if (psta) {
			struct ht_priv	*phtpriv_sta = &psta->htpriv;

			if (phtpriv_sta->ht_option) {
				/*  bwmode */
				phtpriv_sta->bwmode = pmlmeext->cur_bwmode;
				phtpriv_sta->ch_offset = pmlmeext->cur_ch_offset;
			} else {
				phtpriv_sta->bwmode = HT_CHANNEL_WIDTH_20;
				phtpriv_sta->ch_offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
			}
		}
	}
}

void HT_caps_handler(struct rtw_adapter *padapter, struct ndis_802_11_variable_ies *pIE)
{
	unsigned int	i;
	u8	rf_type;
	u8	max_AMPDU_len, min_MPDU_spacing;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv			*phtpriv = &pmlmepriv->htpriv;
	struct registry_priv	*pregistrypriv = &padapter->registrypriv;

	if (pIE == NULL)
		return;

	if (phtpriv->ht_option == false)
		return;

	pmlmeinfo->HT_caps_enable = 1;

	for (i = 0; i < (pIE->Length); i++) {
		if (i != 2) {
			/*	Commented by Albert 2010/07/12 */
			/*	Got the endian issue here. */
			pmlmeinfo->HT_caps.u.HT_cap[i] &= (pIE->data[i]);
		} else {
			/* modify from  fw by Thomas 2010/11/17 */
			if ((pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x3) > (pIE->data[i] & 0x3))
				max_AMPDU_len = (pIE->data[i] & 0x3);
			else
				max_AMPDU_len = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x3);

			if ((pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x1c) > (pIE->data[i] & 0x1c))
				min_MPDU_spacing = (pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para & 0x1c);
			else
				min_MPDU_spacing = (pIE->data[i] & 0x1c);

			pmlmeinfo->HT_caps.u.HT_cap_element.AMPDU_para = max_AMPDU_len | min_MPDU_spacing;
		}
	}

	/*	Commented by Albert 2010/07/12 */
	/*	Have to handle the endian issue after copying. */
	/*	HT_ext_caps didn't be used yet. */
	pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info = pmlmeinfo->HT_caps.u.HT_cap_element.HT_caps_info;
	pmlmeinfo->HT_caps.u.HT_cap_element.HT_ext_caps = pmlmeinfo->HT_caps.u.HT_cap_element.HT_ext_caps;

	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));

	/* update the MCS rates */
	for (i = 0; i < 16; i++) {
		if ((rf_type == RF_1T1R) || (rf_type == RF_1T2R))
			pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate[i] &= MCS_rate_1R[i];
		else
			pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate[i] &= MCS_rate_2R[i];
		if (pregistrypriv->special_rf_path)
			pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate[i] &= MCS_rate_1R[i];
	}
	return;
}

void HT_info_handler(struct rtw_adapter *padapter, struct ndis_802_11_variable_ies *pIE)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv			*phtpriv = &pmlmepriv->htpriv;

	if (pIE == NULL)
		return;

	if (phtpriv->ht_option == false)
		return;

	if (pIE->Length > sizeof(struct HT_info_element))
		return;

	pmlmeinfo->HT_info_enable = 1;
	memcpy(&(pmlmeinfo->HT_info), pIE->data, pIE->Length);

	return;
}

void HTOnAssocRsp(struct rtw_adapter *padapter)
{
	unsigned char		max_AMPDU_len;
	unsigned char		min_MPDU_spacing;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	DBG_8192D("%s\n", __func__);

	if ((pmlmeinfo->HT_info_enable) && (pmlmeinfo->HT_caps_enable)) {
		pmlmeinfo->HT_enable = 1;
	} else {
		pmlmeinfo->HT_enable = 0;
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

	rtw_hal_set_hwreg(padapter, HW_VAR_AMPDU_FACTOR, (u8 *)(&max_AMPDU_len));
}

void ERP_IE_handler(struct rtw_adapter *padapter, struct ndis_802_11_variable_ies *pIE)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (pIE->Length > 1)
		return;

	pmlmeinfo->ERP_enable = 1;
	memcpy(&(pmlmeinfo->ERP_IE), pIE->data, pIE->Length);
}

void VCS_update(struct rtw_adapter *padapter, struct sta_info *psta)
{
	struct registry_priv	 *pregpriv = &padapter->registrypriv;
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
		if ((pmlmeinfo->ERP_enable) && (pmlmeinfo->ERP_IE & BIT(1))) {
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

void update_beacon_info(struct rtw_adapter *padapter, u8 *pframe, uint pkt_len, struct sta_info *psta)
{
	unsigned int i;
	unsigned int len;
	struct ndis_802_11_variable_ies *pIE;

	len = pkt_len - (_BEACON_IE_OFFSET_ + WLAN_HDR_A3_LEN);

	for (i = 0; i < len;) {
		pIE = (struct ndis_802_11_variable_ies *)(pframe + (_BEACON_IE_OFFSET_ + WLAN_HDR_A3_LEN) + i);

		switch (pIE->ElementID) {
		case _HT_EXTRA_INFO_IE_:	/* HT info */
			bwmode_update_check(padapter, pIE);
			break;
		case _ERPINFO_IE_:
			ERP_IE_handler(padapter, pIE);
			VCS_update(padapter, psta);
			break;
		default:
			break;
		}

		i += (pIE->Length + 2);
	}
}

#ifdef CONFIG_DFS
void process_csa_ie(struct rtw_adapter *padapter, u8 *pframe, uint pkt_len)
{
	unsigned int i;
	unsigned int len;
	struct ndis_802_11_variable_ies *pIE;
	u8 new_ch_no = 0;

	len = pkt_len - (_BEACON_IE_OFFSET_ + WLAN_HDR_A3_LEN);

	for (i = 0; i < len;) {
		pIE = (struct ndis_802_11_variable_ies *)(pframe + (_BEACON_IE_OFFSET_ + WLAN_HDR_A3_LEN) + i);

		switch (pIE->ElementID) {
		case _CH_SWTICH_ANNOUNCE_:
			memcpy(&new_ch_no, pIE->data+1, 1);
			rtw_set_csa_cmd(padapter, new_ch_no);
			break;
		default:
			break;
		}
		i += (pIE->Length + 2);
	}
}
#endif /* CONFIG_DFS */

unsigned int is_ap_in_tkip(struct rtw_adapter *padapter)
{
	u32 i;
	struct ndis_802_11_variable_ies *pIE;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex		*cur_network = &(pmlmeinfo->network);

	if (rtw_get_capability((struct wlan_bssid_ex *)cur_network) & WLAN_CAPABILITY_PRIVACY) {
		for (i = sizeof(struct ndis_802_11_fixed_ies); i < pmlmeinfo->network.IELength;) {
			pIE = (struct ndis_802_11_variable_ies *)(pmlmeinfo->network.IEs + i);

			switch (pIE->ElementID) {
			case _VENDOR_SPECIFIC_IE_:
				if ((_rtw_memcmp(pIE->data, RTW_WPA_OUI, 4)) && (_rtw_memcmp((pIE->data + 12), WPA_TKIP_CIPHER, 4)))
					return true;
				break;
			case _RSN_IE_2_:
				if (_rtw_memcmp((pIE->data + 8), RSN_TKIP_CIPHER, 4))
					return true;
			default:
				break;
			}

			i += (pIE->Length + 2);
		}

		return false;
	} else {
		return false;
	}
}

static int wifirate2_ratetbl_inx(unsigned char rate)
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

	num_of_rate = (ptn_sz > NUMRATES) ? NUMRATES : ptn_sz;

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

	num_of_rate = (ptn_sz > NUMRATES) ? NUMRATES : ptn_sz;

	for (i = 0; i < num_of_rate; i++)
		mask |= 0x1 << wifirate2_ratetbl_inx(*(ptn + i));

	return mask;
}

unsigned int update_MSC_rate(struct HT_caps_element *pHT_caps)
{
	unsigned int mask = 0;

	mask = ((pHT_caps->u.HT_cap_element.MCS_rate[0] << 12) | (pHT_caps->u.HT_cap_element.MCS_rate[1] << 20));

	return mask;
}

int support_short_GI(struct rtw_adapter *padapter, struct HT_caps_element *pHT_caps)
{
	unsigned char					bit_offset;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (!(pmlmeinfo->HT_enable))
		return _FAIL;

	if ((pmlmeinfo->assoc_AP_vendor == ralinkAP))
		return _FAIL;

	bit_offset = (pmlmeext->cur_bwmode & HT_CHANNEL_WIDTH_40) ? 6 : 5;

	if (le16_to_cpu(pHT_caps->u.HT_cap_element.HT_caps_info) & (0x1 << bit_offset))
		return _SUCCESS;
	else
		return _FAIL;
}

unsigned char get_highest_rate_idx(u32 mask)
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

static unsigned char get_highest_mcs_rate(struct HT_caps_element *pHT_caps)
{
	int i, mcs_rate;

	mcs_rate = (pHT_caps->u.HT_cap_element.MCS_rate[0] | (pHT_caps->u.HT_cap_element.MCS_rate[1] << 8));

	for (i = 15; i >= 0; i--) {
		if (mcs_rate & (0x1 << i))
			break;
	}
	return i;
}

void Update_RA_Entry(struct rtw_adapter *padapter, u32 mac_id)
{
	rtw_hal_update_ra_mask(padapter, mac_id);
}

static void enable_rate_adaptive(struct rtw_adapter *padapter, u32 mac_id)
{
	Update_RA_Entry(padapter, mac_id);
}

void set_sta_rate(struct rtw_adapter *padapter, struct sta_info *psta)
{
	/* rate adaptive */
	enable_rate_adaptive(padapter, psta->mac_id);
}

/*  Update RRSR and Rate for USERATE */
void update_tx_basic_rate(struct rtw_adapter *padapter, u8 wirelessmode)
{
	unsigned char supported_rates[NDIS_802_11_LENGTH_RATES_EX];
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	memset(supported_rates, 0, NDIS_802_11_LENGTH_RATES_EX);

	/* clear B mod if current channel is in 5G band, avoid tx cck rate in 5G band. */
	if (pmlmeext->cur_channel > 14)
		wirelessmode &= ~(WIRELESS_11B);

	if ((wirelessmode & WIRELESS_11B) && (wirelessmode == WIRELESS_11B))
		memcpy(supported_rates, rtw_basic_rate_cck, 4);
	else if (wirelessmode & WIRELESS_11B)
		memcpy(supported_rates, rtw_basic_rate_mix, 7);
	else
		memcpy(supported_rates, rtw_basic_rate_ofdm, 3);

	if (wirelessmode & WIRELESS_11B)
		update_mgnt_tx_rate(padapter, IEEE80211_CCK_RATE_1MB);
	else
		update_mgnt_tx_rate(padapter, IEEE80211_OFDM_RATE_6MB);

	rtw_hal_set_hwreg(padapter, HW_VAR_BASIC_RATE, supported_rates);
}

unsigned char check_assoc_AP(u8 *pframe, uint len)
{
	unsigned int	i;
	struct ndis_802_11_variable_ies *pIE;

	for (i = sizeof(struct ndis_802_11_fixed_ies); i < len;) {
		pIE = (struct ndis_802_11_variable_ies *)(pframe + i);

		switch (pIE->ElementID) {
		case _VENDOR_SPECIFIC_IE_:
			if ((_rtw_memcmp(pIE->data, ARTHEROS_OUI1, 3)) ||
			    (_rtw_memcmp(pIE->data, ARTHEROS_OUI2, 3))) {
				DBG_8192D("link to Artheros AP\n");
				return atherosAP;
			} else if ((_rtw_memcmp(pIE->data, BROADCOM_OUI1, 3)) ||
				   (_rtw_memcmp(pIE->data, BROADCOM_OUI2, 3)) ||
				   (_rtw_memcmp(pIE->data, BROADCOM_OUI2, 3))) {
				DBG_8192D("link to Broadcom AP\n");
				return broadcomAP;
			} else if (_rtw_memcmp(pIE->data, MARVELL_OUI, 3)) {
				DBG_8192D("link to Marvell AP\n");
				return marvellAP;
			} else if (_rtw_memcmp(pIE->data, RALINK_OUI, 3)) {
				DBG_8192D("link to Ralink AP\n");
				return ralinkAP;
			} else if (_rtw_memcmp(pIE->data, CISCO_OUI, 3)) {
				DBG_8192D("link to Cisco AP\n");
				return ciscoAP;
			} else if (_rtw_memcmp(pIE->data, REALTEK_OUI, 3)) {
				DBG_8192D("link to Realtek 96B\n");
				return realtekAP;
			} else if (_rtw_memcmp(pIE->data, AIRGOCAP_OUI, 3)) {
				DBG_8192D("link to Airgo Cap\n");
				return airgocapAP;
			} else {
				break;
			}

		default:
			break;
		}

		i += (pIE->Length + 2);
	}

	DBG_8192D("link to new AP\n");
	return unknownAP;
}

void update_IOT_info(struct rtw_adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	switch (pmlmeinfo->assoc_AP_vendor) {
	case marvellAP:
		pmlmeinfo->turboMode_cts2self = 1;
		pmlmeinfo->turboMode_rtsen = 0;
		break;
	case ralinkAP:
		pmlmeinfo->turboMode_cts2self = 0;
		pmlmeinfo->turboMode_rtsen = 1;
		/* disable high power */
		Switch_DM_Func(padapter, (~DYNAMIC_FUNC_HP), false);
		break;
	case realtekAP:
		/* rtw_write16(padapter, 0x4cc, 0xffff); */
		/* rtw_write16(padapter, 0x546, 0x01c0); */
		/* disable high power */
		Switch_DM_Func(padapter, (~DYNAMIC_FUNC_HP), false);
		break;
	default:
		pmlmeinfo->turboMode_cts2self = 0;
		pmlmeinfo->turboMode_rtsen = 1;
		break;
	}
}

void update_capinfo(struct rtw_adapter *adapter, u16 updateCap)
{
	struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	bool		shortpreamble;

	/*  Check preamble mode, 2005.01.06, by rcnjko. */
	/*  Mark to update preamble value forever, 2008.03.18 by lanhsin */
	/* if (pMgntInfo->RegPreambleMode == PREAMBLE_AUTO) */
	if (updateCap && shortpreamble) { /*  Short Preamble */
		if (pmlmeinfo->preamble_mode != PREAMBLE_SHORT) { /*  PREAMBLE_LONG or PREAMBLE_AUTO */
			shortpreamble = true;
			pmlmeinfo->preamble_mode = PREAMBLE_SHORT;
			rtw_hal_set_hwreg(adapter, HW_VAR_ACK_PREAMBLE, (u8 *)&shortpreamble);
		}
	} else { /*  Long Preamble */
		if (pmlmeinfo->preamble_mode != PREAMBLE_LONG) {  /*  PREAMBLE_SHORT or PREAMBLE_AUTO */
			shortpreamble = false;
			pmlmeinfo->preamble_mode = PREAMBLE_LONG;
			rtw_hal_set_hwreg(adapter, HW_VAR_ACK_PREAMBLE, (u8 *)&shortpreamble);
		}
	}

	if (updateCap & cIBSS) {
		/* Filen: See 802.11-2007 p.91 */
		pmlmeinfo->slotTime = NON_SHORT_SLOT_TIME;
	} else {
		/* Filen: See 802.11-2007 p.90 */
		if (pmlmeext->cur_wireless_mode & (WIRELESS_11G | WIRELESS_11_24N)) {
			if ((updateCap & cShortSlotTime)) { /*  Short Slot Time */
				if (pmlmeinfo->slotTime != SHORT_SLOT_TIME)
					pmlmeinfo->slotTime = SHORT_SLOT_TIME;
			} else { /*  Long Slot Time */
				if (pmlmeinfo->slotTime != NON_SHORT_SLOT_TIME)
					pmlmeinfo->slotTime = NON_SHORT_SLOT_TIME;
			}
		} else if (pmlmeext->cur_wireless_mode & (WIRELESS_11A | WIRELESS_11_5N)) {
			pmlmeinfo->slotTime = SHORT_SLOT_TIME;
		} else {
			/* B Mode */
			pmlmeinfo->slotTime = NON_SHORT_SLOT_TIME;
		}
	}

	rtw_hal_set_hwreg(adapter, HW_VAR_SLOT_TIME, &pmlmeinfo->slotTime);
}

void update_wireless_mode(struct rtw_adapter *padapter)
{
	u8	init_rate = 0;
	int	ratelen, network_type = 0;
	u32	SIFS_Timer, mask;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex		*cur_network = &(pmlmeinfo->network);
	unsigned char			*rate = cur_network->SupportedRates;
#ifdef CONFIG_CONCURRENT_MODE
	struct rtw_adapter *pbuddy_adapter = padapter->pbuddy_adapter;
#endif /* CONFIG_CONCURRENT_MODE */

	ratelen = rtw_get_rateset_len(cur_network->SupportedRates);

	if ((pmlmeinfo->HT_info_enable) && (pmlmeinfo->HT_caps_enable))
		pmlmeinfo->HT_enable = 1;

	if (pmlmeext->cur_channel > 14) {
		if (pmlmeinfo->HT_enable)
			network_type = WIRELESS_11_5N;

		network_type |= WIRELESS_11A;
	} else {
		if (pmlmeinfo->HT_enable)
			network_type = WIRELESS_11_24N;

		if ((cckratesonly_included(rate, ratelen)) == true)
			network_type |= WIRELESS_11B;
		else if ((cckrates_included(rate, ratelen)) == true)
			network_type |= WIRELESS_11BG;
		else
			network_type |= WIRELESS_11G;
	}

	pmlmeext->cur_wireless_mode = network_type & padapter->registrypriv.wireless_mode;

	/* For STA mode, driver need to modify initial data rate, or MAC will use wrong tx rate. */
	/* Modified by Thomas 2012-12-3 */
	mask = update_supported_rate(cur_network->SupportedRates, ratelen);
	init_rate = get_highest_rate_idx(mask)&0x3f;
	rtw_hal_set_hwreg(padapter, HW_VAR_INIT_DATA_RATE,  (u8 *)&init_rate);

	SIFS_Timer = 0x0a0a0808; /* 0x0808 -> for CCK, 0x0a0a -> for OFDM */
				/* change this value if having IOT issues. */

	rtw_hal_set_hwreg(padapter, HW_VAR_RESP_SIFS,  (u8 *)&SIFS_Timer);

	if (pmlmeext->cur_wireless_mode & WIRELESS_11B) {
		update_mgnt_tx_rate(padapter, IEEE80211_CCK_RATE_1MB);
	} else {
		update_mgnt_tx_rate(padapter, IEEE80211_OFDM_RATE_6MB);
#ifdef CONFIG_CONCURRENT_MODE
		if (pbuddy_adapter && (pmlmeext->cur_wireless_mode & WIRELESS_11A))
			update_mgnt_tx_rate(pbuddy_adapter, IEEE80211_OFDM_RATE_6MB);
#endif /* CONFIG_CONCURRENT_MODE */
	}
}

void update_bmc_sta_support_rate(struct rtw_adapter *padapter, u32 mac_id)
{
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (pmlmeext->cur_wireless_mode & WIRELESS_11B) {
		/*  Only B, B/G, and B/G/N AP could use CCK rate */
		memcpy((pmlmeinfo->FW_sta_info[mac_id].SupportedRates), rtw_basic_rate_cck, 4);
	} else {
		memcpy((pmlmeinfo->FW_sta_info[mac_id].SupportedRates), rtw_basic_rate_ofdm, 3);
	}
}

int update_sta_support_rate(struct rtw_adapter *padapter, u8 *pvar_ie, uint var_ie_len, int cam_idx)
{
	unsigned int	ie_len;
	struct ndis_802_11_variable_ies *pIE;
	int	supportRateNum = 0;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	pIE = (struct ndis_802_11_variable_ies *)rtw_get_ie(pvar_ie, _SUPPORTEDRATES_IE_, &ie_len, var_ie_len);
	if (pIE == NULL)
		return _FAIL;

	memcpy(pmlmeinfo->FW_sta_info[cam_idx].SupportedRates, pIE->data, ie_len);
	supportRateNum = ie_len;

	pIE = (struct ndis_802_11_variable_ies *)rtw_get_ie(pvar_ie, _EXT_SUPPORTEDRATES_IE_, &ie_len, var_ie_len);
	if (pIE)
		memcpy((pmlmeinfo->FW_sta_info[cam_idx].SupportedRates + supportRateNum), pIE->data, ie_len);

	return _SUCCESS;
}

void process_addba_req(struct rtw_adapter *padapter, u8 *paddba_req, u8 *addr)
{
	struct sta_info *psta;
	u16 tid;
	u16 param;
	struct recv_reorder_ctrl *preorder_ctrl;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct ADDBA_request	*preq = (struct ADDBA_request *)paddba_req;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	psta = rtw_get_stainfo(pstapriv, addr);

	if (psta) {
		param = le16_to_cpu(preq->BA_para_set);
		tid = (param>>2)&0x0f;

		preorder_ctrl = &psta->recvreorder_ctrl[tid];
		preorder_ctrl->indicate_seq = 0xffff;
		preorder_ctrl->enable = (pmlmeinfo->bAcceptAddbaReq == true) ? true : false;
	}
}

void update_TSF(struct mlme_ext_priv *pmlmeext, u8 *pframe, uint len)
{
	u8 *pIE;
	__le32 *pbuf;

	pIE = pframe + sizeof(struct rtw_ieee80211_hdr_3addr);
	pbuf = (__le32 *)pIE;
	pmlmeext->TSFValue = le32_to_cpu(*(pbuf+1));
	pmlmeext->TSFValue = pmlmeext->TSFValue << 32;
	pmlmeext->TSFValue |= le32_to_cpu(*pbuf);
}

void correct_TSF(struct rtw_adapter *padapter, struct mlme_ext_priv *pmlmeext)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_CORRECT_TSF, NULL);
}

void beacon_timing_control(struct rtw_adapter *padapter)
{
	rtw_hal_bcn_related_reg_setting(padapter);
}

static struct rtw_adapter *pbuddy_padapter;

int rtw_handle_dualmac(struct rtw_adapter *adapter, bool init)
{
	int status = _SUCCESS;
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);

	if (init) {
		if ((dvobj->NumInterfaces == 2) && (adapter->registrypriv.mac_phy_mode != 1)) {
			dvobj->DualMacMode = true;
			/*  temply disable IPS For 92D-VC */
			adapter->registrypriv.ips_mode = IPS_NONE;
		}

		/* For SMSP on 92DU-VC, driver do not probe another Interface. */
		if ((dvobj->DualMacMode != true) && (dvobj->InterfaceNumber != 0)) {
			DBG_8192D("%s(): Do not init another USB Interface because SMSP\n", __func__);
			status = _FAIL;
			goto exit;
		}
#ifdef CONFIG_DUALMAC_CONCURRENT
		if (pbuddy_padapter == NULL) {
			pbuddy_padapter = adapter;
			DBG_8192D("%s(): pbuddy_padapter == NULL, Set pbuddy_padapter\n", __func__);
		} else {
			adapter->pbuddy_adapter = pbuddy_padapter;
			pbuddy_padapter->pbuddy_adapter = adapter;
			/*  clear global value */
			pbuddy_padapter = NULL;
			DBG_8192D("%s(): pbuddy_padapter exist, Exchange Information\n", __func__);
		}

		if (dvobj->InterfaceNumber == 0) {
			/* set adapter_type/iface type */
			adapter->isprimary = true;
			adapter->adapter_type = PRIMARY_ADAPTER;
			adapter->iface_type = IFACE_PORT0;
			DBG_8192D("%s(): PRIMARY_ADAPTER\n", __func__);
		} else {
			/* set adapter_type/iface type */
			adapter->isprimary = false;
			adapter->adapter_type = SECONDARY_ADAPTER;
			adapter->iface_type = IFACE_PORT1;
			DBG_8192D("%s(): SECONDARY_ADAPTER\n", __func__);
		}
#endif
	} else {
		pbuddy_padapter = NULL;
	}
exit:
	return status;
}
