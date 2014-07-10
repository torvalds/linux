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

#include <osdep_service.h>
#include <drv_types.h>
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

unsigned char REALTEK_96B_IE[] = {0x00, 0xe0, 0x4c, 0x02, 0x01, 0x20};

#define R2T_PHY_DELAY	(0)

/* define WAIT_FOR_BCN_TO_M	(3000) */
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
		if  ((((rate[i]) & 0x7f) == 2)	|| (((rate[i]) & 0x7f) == 4) ||
		     (((rate[i]) & 0x7f) == 11)  || (((rate[i]) & 0x7f) == 22))
			return true;
	}
	return false;
}

int cckratesonly_included(unsigned char *rate, int ratelen)
{
	int	i;

	for (i = 0; i < ratelen; i++) {
		if  ((((rate[i]) & 0x7f) != 2) && (((rate[i]) & 0x7f) != 4) &&
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

u8 judge_network_type(struct adapter *padapter, unsigned char *rate, int ratelen)
{
	u8 network_type = 0;
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

static int is_basicrate(struct adapter *padapter, unsigned char rate)
{
	int i;
	unsigned char val;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

	for (i = 0; i < NumRates; i++) {
		val = pmlmeext->basicrate[i];

		if ((val != 0xff) && (val != 0xfe)) {
			if (rate == ratetbl_val_2wifirate(val))
				return true;
		}
	}
	return false;
}

static unsigned int ratetbl2rateset(struct adapter *padapter, unsigned char *rateset)
{
	int i;
	unsigned char rate;
	unsigned int	len = 0;
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

void get_rate_set(struct adapter *padapter, unsigned char *pbssrate, int *bssrate_len)
{
	unsigned char supportedrates[NumRates];

	memset(supportedrates, 0, NumRates);
	*bssrate_len = ratetbl2rateset(padapter, supportedrates);
	memcpy(pbssrate, supportedrates, *bssrate_len);
}

void UpdateBrateTbl(struct adapter *Adapter, u8 *mbrate)
{
	u8	i;
	u8	rate;

	/*  1M, 2M, 5.5M, 11M, 6M, 12M, 24M are mandatory. */
	for (i = 0; i < NDIS_802_11_LENGTH_RATES_EX; i++) {
		rate = mbrate[i] & 0x7f;
		switch (rate) {
		case IEEE80211_CCK_RATE_1MB:
		case IEEE80211_CCK_RATE_2MB:
		case IEEE80211_CCK_RATE_5MB:
		case IEEE80211_CCK_RATE_11MB:
		case IEEE80211_OFDM_RATE_6MB:
		case IEEE80211_OFDM_RATE_12MB:
		case IEEE80211_OFDM_RATE_24MB:
			mbrate[i] |= IEEE80211_BASIC_RATE_MASK;
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

void Save_DM_Func_Flag(struct adapter *padapter)
{
	u8	saveflag = true;

	rtw_hal_set_hwreg(padapter, HW_VAR_DM_FUNC_OP, (u8 *)(&saveflag));
}

void Restore_DM_Func_Flag(struct adapter *padapter)
{
	u8	saveflag = false;

	rtw_hal_set_hwreg(padapter, HW_VAR_DM_FUNC_OP, (u8 *)(&saveflag));
}

void Switch_DM_Func(struct adapter *padapter, u32 mode, u8 enable)
{
	if (enable)
		rtw_hal_set_hwreg(padapter, HW_VAR_DM_FUNC_SET, (u8 *)(&mode));
	else
		rtw_hal_set_hwreg(padapter, HW_VAR_DM_FUNC_CLR, (u8 *)(&mode));
}

static void Set_NETYPE0_MSR(struct adapter *padapter, u8 type)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_MEDIA_STATUS, (u8 *)(&type));
}

void Set_MSR(struct adapter *padapter, u8 type)
{
	Set_NETYPE0_MSR(padapter, type);
}

inline u8 rtw_get_oper_ch(struct adapter *adapter)
{
	return adapter->mlmeextpriv.oper_channel;
}

inline void rtw_set_oper_ch(struct adapter *adapter, u8 ch)
{
	adapter->mlmeextpriv.oper_channel = ch;
}

inline u8 rtw_get_oper_bw(struct adapter *adapter)
{
	return adapter->mlmeextpriv.oper_bwmode;
}

inline void rtw_set_oper_bw(struct adapter *adapter, u8 bw)
{
	adapter->mlmeextpriv.oper_bwmode = bw;
}

inline u8 rtw_get_oper_choffset(struct adapter *adapter)
{
	return adapter->mlmeextpriv.oper_ch_offset;
}

inline void rtw_set_oper_choffset(struct adapter *adapter, u8 offset)
{
	adapter->mlmeextpriv.oper_ch_offset = offset;
}

void SelectChannel(struct adapter *padapter, unsigned char channel)
{
	/* saved channel info */
	rtw_set_oper_ch(padapter, channel);
	rtw_hal_set_chan(padapter, channel);
}

void SetBWMode(struct adapter *padapter, unsigned short bwmode,
	       unsigned char channel_offset)
{
	/* saved bw info */
	rtw_set_oper_bw(padapter, bwmode);
	rtw_set_oper_choffset(padapter, channel_offset);

	rtw_hal_set_bwmode(padapter, (enum ht_channel_width)bwmode, channel_offset);
}

void set_channel_bwmode(struct adapter *padapter, unsigned char channel, unsigned char channel_offset, unsigned short bwmode)
{
	u8 center_ch;

	if (padapter->bNotifyChannelChange)
		DBG_88E("[%s] ch = %d, offset = %d, bwmode = %d\n", __func__, channel, channel_offset, bwmode);

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

	/* set Channel */
	/* saved channel/bw info */
	rtw_set_oper_ch(padapter, channel);
	rtw_set_oper_bw(padapter, bwmode);
	rtw_set_oper_choffset(padapter, channel_offset);

	rtw_hal_set_chan(padapter, center_ch); /*  set center channel */
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

__inline u8 *get_my_bssid(struct wlan_bssid_ex *pnetwork)
{
	return pnetwork->MacAddress;
}

u16 get_beacon_interval(struct wlan_bssid_ex *bss)
{
	__le16 val;
	memcpy((unsigned char *)&val, rtw_get_beacon_interval_from_ie(bss->IEs), 2);

	return le16_to_cpu(val);
}

int is_client_associated_to_ap(struct adapter *padapter)
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

int is_client_associated_to_ibss(struct adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if ((pmlmeinfo->state & WIFI_FW_ASSOC_SUCCESS) && ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE))
		return true;
	else
		return _FAIL;
}

int is_IBSS_empty(struct adapter *padapter)
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

void CAM_empty_entry(struct adapter *Adapter, u8 ucIndex)
{
	rtw_hal_set_hwreg(Adapter, HW_VAR_CAM_EMPTY_ENTRY, (u8 *)(&ucIndex));
}

void invalidate_cam_all(struct adapter *padapter)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_CAM_INVALID_ALL, NULL);
}

void write_cam(struct adapter *padapter, u8 entry, u16 ctrl, u8 *mac, u8 *key)
{
	unsigned int	i, val, addr;
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

void clear_cam_entry(struct adapter *padapter, u8 entry)
{
	unsigned char null_sta[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	unsigned char null_key[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

	write_cam(padapter, entry, 0, null_sta, null_key);
}

int allocate_fw_sta_entry(struct adapter *padapter)
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

void flush_all_cam_entry(struct adapter *padapter)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	rtw_hal_set_hwreg(padapter, HW_VAR_CAM_INVALID_ALL, NULL);

	memset((u8 *)(pmlmeinfo->FW_sta_info), 0, sizeof(pmlmeinfo->FW_sta_info));
}

int WMM_param_handler(struct adapter *padapter, struct ndis_802_11_var_ie *pIE)
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

void WMMOnAssocRsp(struct adapter *padapter)
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
			edca[XMIT_BE_QUEUE] = acParm;
			break;
		case 0x1:
			rtw_hal_set_hwreg(padapter, HW_VAR_AC_PARAM_BK, (u8 *)(&acParm));
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

		DBG_88E("WMM(%x): %x, %x\n", ACI, ACM, acParm);
	}

	if (padapter->registrypriv.acm_method == 1)
		rtw_hal_set_hwreg(padapter, HW_VAR_ACM_CTRL, (u8 *)(&acm_mask));
	else
		padapter->mlmepriv.acm_mask = acm_mask;

	inx[0] = 0; inx[1] = 1; inx[2] = 2; inx[3] = 3;

	if (pregpriv->wifi_spec == 1) {
		u32	j, tmp, change_inx = false;

		/* entry indx: 0->vo, 1->vi, 2->be, 3->bk. */
		for (i = 0; i < 4; i++) {
			for (j = i+1; j < 4; j++) {
				/* compare CW and AIFS */
				if ((edca[j] & 0xFFFF) < (edca[i] & 0xFFFF)) {
					change_inx = true;
				} else if ((edca[j] & 0xFFFF) == (edca[i] & 0xFFFF)) {
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

	for (i = 0; i < 4; i++) {
		pxmitpriv->wmm_para_seq[i] = inx[i];
		DBG_88E("wmm_para_seq(%d): %d\n", i, pxmitpriv->wmm_para_seq[i]);
	}

	return;
}

static void bwmode_update_check(struct adapter *padapter, struct ndis_802_11_var_ie *pIE)
{
	unsigned char	 new_bwmode;
	unsigned char  new_ch_offset;
	struct HT_info_element	 *pHT_info;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct ht_priv			*phtpriv = &pmlmepriv->htpriv;

	if (!pIE)
		return;

	if (!phtpriv)
		return;

	if (pIE->Length > sizeof(struct HT_info_element))
		return;

	pHT_info = (struct HT_info_element *)pIE->data;

	if ((pHT_info->infos[0] & BIT(2)) && pregistrypriv->cbw40_enable) {
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

	if (pmlmeinfo->bwmode_updated) {
		struct sta_info *psta;
		struct wlan_bssid_ex	*cur_network = &(pmlmeinfo->network);
		struct sta_priv	*pstapriv = &padapter->stapriv;

		/* set_channel_bwmode(padapter, pmlmeext->cur_channel, pmlmeext->cur_ch_offset, pmlmeext->cur_bwmode); */

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

void HT_caps_handler(struct adapter *padapter, struct ndis_802_11_var_ie *pIE)
{
	unsigned int	i;
	u8	rf_type;
	u8	max_AMPDU_len, min_MPDU_spacing;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv			*phtpriv = &pmlmepriv->htpriv;

	if (pIE == NULL)
		return;

	if (!phtpriv->ht_option)
		return;

	pmlmeinfo->HT_caps_enable = 1;

	for (i = 0; i < (pIE->Length); i++) {
		if (i != 2) {
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

	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));

	/* update the MCS rates */
	for (i = 0; i < 16; i++) {
		if ((rf_type == RF_1T1R) || (rf_type == RF_1T2R))
			pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate[i] &= MCS_rate_1R[i];
		else
			pmlmeinfo->HT_caps.u.HT_cap_element.MCS_rate[i] &= MCS_rate_2R[i];
	}
	return;
}

void HT_info_handler(struct adapter *padapter, struct ndis_802_11_var_ie *pIE)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct ht_priv			*phtpriv = &pmlmepriv->htpriv;

	if (pIE == NULL)
		return;

	if (!phtpriv->ht_option)
		return;

	if (pIE->Length > sizeof(struct HT_info_element))
		return;

	pmlmeinfo->HT_info_enable = 1;
	memcpy(&(pmlmeinfo->HT_info), pIE->data, pIE->Length);
	return;
}

void HTOnAssocRsp(struct adapter *padapter)
{
	unsigned char		max_AMPDU_len;
	unsigned char		min_MPDU_spacing;
	/* struct registry_priv	 *pregpriv = &padapter->registrypriv; */
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	DBG_88E("%s\n", __func__);

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

void ERP_IE_handler(struct adapter *padapter, struct ndis_802_11_var_ie *pIE)
{
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (pIE->Length > 1)
		return;

	pmlmeinfo->ERP_enable = 1;
	memcpy(&(pmlmeinfo->ERP_IE), pIE->data, pIE->Length);
}

void VCS_update(struct adapter *padapter, struct sta_info *psta)
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

int rtw_check_bcn_info(struct adapter  *Adapter, u8 *pframe, u32 packet_len)
{
	unsigned int		len;
	unsigned char		*p;
	unsigned short	val16, subtype;
	struct wlan_network *cur_network = &(Adapter->mlmepriv.cur_network);
	/* u8 wpa_ie[255], rsn_ie[255]; */
	u16 wpa_len = 0, rsn_len = 0;
	u8 encryp_protocol = 0;
	struct wlan_bssid_ex *bssid;
	int group_cipher = 0, pairwise_cipher = 0, is_8021x = 0;
	unsigned char *pbuf;
	u32 wpa_ielen = 0;
	u8 *pbssid = GetAddr3Ptr(pframe);
	struct HT_info_element *pht_info = NULL;
	struct rtw_ieee80211_ht_cap *pht_cap = NULL;
	u32 bcn_channel;
	unsigned short	ht_cap_info;
	unsigned char	ht_info_infos_0;
	int ssid_len;

	if (is_client_associated_to_ap(Adapter) == false)
		return true;

	len = packet_len - sizeof(struct rtw_ieee80211_hdr_3addr);

	if (len > MAX_IE_SZ) {
		DBG_88E("%s IE too long for survey event\n", __func__);
		return _FAIL;
	}

	if (!memcmp(cur_network->network.MacAddress, pbssid, 6) == false) {
		DBG_88E("Oops: rtw_check_network_encrypt linked but recv other bssid bcn\n%pM %pM\n",
			(pbssid), (cur_network->network.MacAddress));
		return true;
	}

	bssid = kzalloc(sizeof(struct wlan_bssid_ex), GFP_KERNEL);

	subtype = GetFrameSubType(pframe) >> 4;

	if (subtype == WIFI_BEACON)
		bssid->Reserved[0] = 1;

	bssid->Length = sizeof(struct wlan_bssid_ex) - MAX_IE_SZ + len;

	/* below is to copy the information element */
	bssid->IELength = len;
	memcpy(bssid->IEs, (pframe + sizeof(struct rtw_ieee80211_hdr_3addr)), bssid->IELength);

	/* check bw and channel offset */
	/* parsing HT_CAP_IE */
	p = rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _HT_CAPABILITY_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_);
	if (p && len > 0) {
		pht_cap = (struct rtw_ieee80211_ht_cap *)(p + 2);
		ht_cap_info = pht_cap->cap_info;
	} else {
		ht_cap_info = 0;
	}
	/* parsing HT_INFO_IE */
	p = rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _HT_ADD_INFO_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_);
	if (p && len > 0) {
			pht_info = (struct HT_info_element *)(p + 2);
			ht_info_infos_0 = pht_info->infos[0];
	} else {
			ht_info_infos_0 = 0;
	}
	if (ht_cap_info != cur_network->BcnInfo.ht_cap_info ||
	    ((ht_info_infos_0&0x03) != (cur_network->BcnInfo.ht_info_infos_0&0x03))) {
			DBG_88E("%s bcn now: ht_cap_info:%x ht_info_infos_0:%x\n", __func__,
				ht_cap_info, ht_info_infos_0);
			DBG_88E("%s bcn link: ht_cap_info:%x ht_info_infos_0:%x\n", __func__,
				cur_network->BcnInfo.ht_cap_info, cur_network->BcnInfo.ht_info_infos_0);
			DBG_88E("%s bw mode change, disconnect\n", __func__);
			/* bcn_info_update */
			cur_network->BcnInfo.ht_cap_info = ht_cap_info;
			cur_network->BcnInfo.ht_info_infos_0 = ht_info_infos_0;
			/* to do : need to check that whether modify related register of BB or not */
			/* goto _mismatch; */
	}

	/* Checking for channel */
	p = rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _DSSET_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_);
	if (p) {
			bcn_channel = *(p + 2);
	} else {/* In 5G, some ap do not have DSSET IE checking HT info for channel */
			p = rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _HT_ADD_INFO_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_);
			if (pht_info) {
					bcn_channel = pht_info->primary_channel;
			} else { /* we don't find channel IE, so don't check it */
					DBG_88E("Oops: %s we don't find channel IE, so don't check it\n", __func__);
					bcn_channel = Adapter->mlmeextpriv.cur_channel;
			}
	}
	if (bcn_channel != Adapter->mlmeextpriv.cur_channel) {
			DBG_88E("%s beacon channel:%d cur channel:%d disconnect\n", __func__,
				bcn_channel, Adapter->mlmeextpriv.cur_channel);
			goto _mismatch;
	}

	/* checking SSID */
	ssid_len = 0;
	p = rtw_get_ie(bssid->IEs + _FIXED_IE_LENGTH_, _SSID_IE_, &len, bssid->IELength - _FIXED_IE_LENGTH_);
	if (p) {
		ssid_len = *(p + 1);
		if (ssid_len > NDIS_802_11_LENGTH_SSID)
			ssid_len = 0;
	}
	memcpy(bssid->Ssid.Ssid, (p + 2), ssid_len);
	bssid->Ssid.SsidLength = ssid_len;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("%s bssid.Ssid.Ssid:%s bssid.Ssid.SsidLength:%d "
				"cur_network->network.Ssid.Ssid:%s len:%d\n", __func__, bssid->Ssid.Ssid,
				bssid->Ssid.SsidLength, cur_network->network.Ssid.Ssid,
				cur_network->network.Ssid.SsidLength));

	if (memcmp(bssid->Ssid.Ssid, cur_network->network.Ssid.Ssid, 32) ||
	    bssid->Ssid.SsidLength != cur_network->network.Ssid.SsidLength) {
		if (bssid->Ssid.Ssid[0] != '\0' && bssid->Ssid.SsidLength != 0) { /* not hidden ssid */
			DBG_88E("%s(), SSID is not match return FAIL\n", __func__);
			goto _mismatch;
		}
	}

	/* check encryption info */
	val16 = rtw_get_capability((struct wlan_bssid_ex *)bssid);

	if (val16 & BIT(4))
		bssid->Privacy = 1;
	else
		bssid->Privacy = 0;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("%s(): cur_network->network.Privacy is %d, bssid.Privacy is %d\n",
		 __func__, cur_network->network.Privacy, bssid->Privacy));
	if (cur_network->network.Privacy != bssid->Privacy) {
		DBG_88E("%s(), privacy is not match return FAIL\n", __func__);
		goto _mismatch;
	}

	rtw_get_sec_ie(bssid->IEs, bssid->IELength, NULL, &rsn_len, NULL, &wpa_len);

	if (rsn_len > 0) {
		encryp_protocol = ENCRYP_PROTOCOL_WPA2;
	} else if (wpa_len > 0) {
		encryp_protocol = ENCRYP_PROTOCOL_WPA;
	} else {
		if (bssid->Privacy)
			encryp_protocol = ENCRYP_PROTOCOL_WEP;
	}

	if (cur_network->BcnInfo.encryp_protocol != encryp_protocol) {
		DBG_88E("%s(): encryption protocol is not match , return FAIL\n", __func__);
		goto _mismatch;
	}

	if (encryp_protocol == ENCRYP_PROTOCOL_WPA || encryp_protocol == ENCRYP_PROTOCOL_WPA2) {
		pbuf = rtw_get_wpa_ie(&bssid->IEs[12], &wpa_ielen, bssid->IELength-12);
		if (pbuf && (wpa_ielen > 0)) {
			if (_SUCCESS == rtw_parse_wpa_ie(pbuf, wpa_ielen+2, &group_cipher, &pairwise_cipher, &is_8021x)) {
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
					 ("%s pnetwork->pairwise_cipher: %d, group_cipher is %d, is_8021x is %d\n", __func__,
					 pairwise_cipher, group_cipher, is_8021x));
			}
		} else {
			pbuf = rtw_get_wpa2_ie(&bssid->IEs[12], &wpa_ielen, bssid->IELength-12);

			if (pbuf && (wpa_ielen > 0)) {
				if (_SUCCESS == rtw_parse_wpa2_ie(pbuf, wpa_ielen+2, &group_cipher, &pairwise_cipher, &is_8021x)) {
					RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
						 ("%s pnetwork->pairwise_cipher: %d, pnetwork->group_cipher is %d, is_802x is %d\n",
						  __func__, pairwise_cipher, group_cipher, is_8021x));
				}
			}
		}

		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("%s cur_network->group_cipher is %d: %d\n", __func__, cur_network->BcnInfo.group_cipher, group_cipher));
		if (pairwise_cipher != cur_network->BcnInfo.pairwise_cipher || group_cipher != cur_network->BcnInfo.group_cipher) {
			DBG_88E("%s pairwise_cipher(%x:%x) or group_cipher(%x:%x) is not match , return FAIL\n", __func__,
				pairwise_cipher, cur_network->BcnInfo.pairwise_cipher,
				group_cipher, cur_network->BcnInfo.group_cipher);
			goto _mismatch;
		}

		if (is_8021x != cur_network->BcnInfo.is_8021x) {
			DBG_88E("%s authentication is not match , return FAIL\n", __func__);
			goto _mismatch;
		}
	}

	kfree(bssid);
	return _SUCCESS;

_mismatch:
	kfree(bssid);
	return _FAIL;
}

void update_beacon_info(struct adapter *padapter, u8 *pframe, uint pkt_len, struct sta_info *psta)
{
	unsigned int i;
	unsigned int len;
	struct ndis_802_11_var_ie *pIE;

	len = pkt_len - (_BEACON_IE_OFFSET_ + WLAN_HDR_A3_LEN);

	for (i = 0; i < len;) {
		pIE = (struct ndis_802_11_var_ie *)(pframe + (_BEACON_IE_OFFSET_ + WLAN_HDR_A3_LEN) + i);

		switch (pIE->ElementID) {
		case _HT_EXTRA_INFO_IE_:	/* HT info */
			/* HT_info_handler(padapter, pIE); */
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

unsigned int is_ap_in_tkip(struct adapter *padapter)
{
	u32 i;
	struct ndis_802_11_var_ie *pIE;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex		*cur_network = &(pmlmeinfo->network);

	if (rtw_get_capability((struct wlan_bssid_ex *)cur_network) & WLAN_CAPABILITY_PRIVACY) {
		for (i = sizeof(struct ndis_802_11_fixed_ie); i < pmlmeinfo->network.IELength;) {
			pIE = (struct ndis_802_11_var_ie *)(pmlmeinfo->network.IEs + i);

			switch (pIE->ElementID) {
			case _VENDOR_SPECIFIC_IE_:
				if ((!memcmp(pIE->data, RTW_WPA_OUI, 4)) && (!memcmp((pIE->data + 12), WPA_TKIP_CIPHER, 4)))
					return true;
				break;
			case _RSN_IE_2_:
				if (!memcmp((pIE->data + 8), RSN_TKIP_CIPHER, 4))
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

unsigned int should_forbid_n_rate(struct adapter *padapter)
{
	u32 i;
	struct ndis_802_11_var_ie *pIE;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct wlan_bssid_ex  *cur_network = &pmlmepriv->cur_network.network;

	if (rtw_get_capability((struct wlan_bssid_ex *)cur_network) & WLAN_CAPABILITY_PRIVACY) {
		for (i = sizeof(struct ndis_802_11_fixed_ie); i < cur_network->IELength;) {
			pIE = (struct ndis_802_11_var_ie *)(cur_network->IEs + i);

			switch (pIE->ElementID) {
			case _VENDOR_SPECIFIC_IE_:
				if (!memcmp(pIE->data, RTW_WPA_OUI, 4) &&
				    ((!memcmp((pIE->data + 12), WPA_CIPHER_SUITE_CCMP, 4)) ||
				    (!memcmp((pIE->data + 16), WPA_CIPHER_SUITE_CCMP, 4))))
					return false;
				break;
			case _RSN_IE_2_:
				if  ((!memcmp((pIE->data + 8), RSN_CIPHER_SUITE_CCMP, 4))  ||
				       (!memcmp((pIE->data + 12), RSN_CIPHER_SUITE_CCMP, 4)))
					return false;
			default:
				break;
			}

			i += (pIE->Length + 2);
		}

		return true;
	} else {
		return false;
	}
}

unsigned int is_ap_in_wep(struct adapter *padapter)
{
	u32 i;
	struct ndis_802_11_var_ie *pIE;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex		*cur_network = &(pmlmeinfo->network);

	if (rtw_get_capability((struct wlan_bssid_ex *)cur_network) & WLAN_CAPABILITY_PRIVACY) {
		for (i = sizeof(struct ndis_802_11_fixed_ie); i < pmlmeinfo->network.IELength;) {
			pIE = (struct ndis_802_11_var_ie *)(pmlmeinfo->network.IEs + i);

			switch (pIE->ElementID) {
			case _VENDOR_SPECIFIC_IE_:
				if (!memcmp(pIE->data, RTW_WPA_OUI, 4))
					return false;
				break;
			case _RSN_IE_2_:
				return false;
			default:
				break;
			}
			i += (pIE->Length + 2);
		}
		return true;
	} else {
		return false;
	}
}

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

unsigned int update_MSC_rate(struct HT_caps_element *pHT_caps)
{
	unsigned int mask = 0;

	mask = ((pHT_caps->u.HT_cap_element.MCS_rate[0] << 12) | (pHT_caps->u.HT_cap_element.MCS_rate[1] << 20));

	return mask;
}

int support_short_GI(struct adapter *padapter, struct HT_caps_element *pHT_caps)
{
	unsigned char					bit_offset;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (!(pmlmeinfo->HT_enable))
		return _FAIL;

	if ((pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_RALINK))
		return _FAIL;

	bit_offset = (pmlmeext->cur_bwmode & HT_CHANNEL_WIDTH_40) ? 6 : 5;

	if (__le16_to_cpu(pHT_caps->u.HT_cap_element.HT_caps_info) & (0x1 << bit_offset))
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

void Update_RA_Entry(struct adapter *padapter, u32 mac_id)
{
	rtw_hal_update_ra_mask(padapter, mac_id, 0);
}

static void enable_rate_adaptive(struct adapter *padapter, u32 mac_id)
{
	Update_RA_Entry(padapter, mac_id);
}

void set_sta_rate(struct adapter *padapter, struct sta_info *psta)
{
	/* rate adaptive */
	enable_rate_adaptive(padapter, psta->mac_id);
}

/*  Update RRSR and Rate for USERATE */
void update_tx_basic_rate(struct adapter *padapter, u8 wirelessmode)
{
	unsigned char supported_rates[NDIS_802_11_LENGTH_RATES_EX];
	memset(supported_rates, 0, NDIS_802_11_LENGTH_RATES_EX);

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
	unsigned int i;
	struct ndis_802_11_var_ie *pIE;
	u8	epigram_vendor_flag;
	u8	ralink_vendor_flag;
	epigram_vendor_flag = 0;
	ralink_vendor_flag = 0;

	for (i = sizeof(struct ndis_802_11_fixed_ie); i < len;) {
		pIE = (struct ndis_802_11_var_ie *)(pframe + i);

		switch (pIE->ElementID) {
		case _VENDOR_SPECIFIC_IE_:
			if ((!memcmp(pIE->data, ARTHEROS_OUI1, 3)) ||
			    (!memcmp(pIE->data, ARTHEROS_OUI2, 3))) {
				DBG_88E("link to Artheros AP\n");
				return HT_IOT_PEER_ATHEROS;
			} else if ((!memcmp(pIE->data, BROADCOM_OUI1, 3)) ||
				   (!memcmp(pIE->data, BROADCOM_OUI2, 3)) ||
				   (!memcmp(pIE->data, BROADCOM_OUI2, 3))) {
				DBG_88E("link to Broadcom AP\n");
				return HT_IOT_PEER_BROADCOM;
			} else if (!memcmp(pIE->data, MARVELL_OUI, 3)) {
				DBG_88E("link to Marvell AP\n");
				return HT_IOT_PEER_MARVELL;
			} else if (!memcmp(pIE->data, RALINK_OUI, 3)) {
				if (!ralink_vendor_flag) {
					ralink_vendor_flag = 1;
				} else {
					DBG_88E("link to Ralink AP\n");
					return HT_IOT_PEER_RALINK;
				}
			} else if (!memcmp(pIE->data, CISCO_OUI, 3)) {
				DBG_88E("link to Cisco AP\n");
				return HT_IOT_PEER_CISCO;
			} else if (!memcmp(pIE->data, REALTEK_OUI, 3)) {
				DBG_88E("link to Realtek 96B\n");
				return HT_IOT_PEER_REALTEK;
			} else if (!memcmp(pIE->data, AIRGOCAP_OUI, 3)) {
				DBG_88E("link to Airgo Cap\n");
				return HT_IOT_PEER_AIRGO;
			} else if (!memcmp(pIE->data, EPIGRAM_OUI, 3)) {
				epigram_vendor_flag = 1;
				if (ralink_vendor_flag) {
					DBG_88E("link to Tenda W311R AP\n");
					 return HT_IOT_PEER_TENDA;
				} else {
					DBG_88E("Capture EPIGRAM_OUI\n");
				}
			} else {
				break;
			}

		default:
			break;
		}
		i += (pIE->Length + 2);
	}

	if (ralink_vendor_flag && !epigram_vendor_flag) {
		DBG_88E("link to Ralink AP\n");
		return HT_IOT_PEER_RALINK;
	} else if (ralink_vendor_flag && epigram_vendor_flag) {
		DBG_88E("link to Tenda W311R AP\n");
		return HT_IOT_PEER_TENDA;
	} else {
		DBG_88E("link to new AP\n");
		return HT_IOT_PEER_UNKNOWN;
	}
}

void update_IOT_info(struct adapter *padapter)
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
		/* disable high power */
		Switch_DM_Func(padapter, (~DYNAMIC_BB_DYNAMIC_TXPWR), false);
		break;
	case HT_IOT_PEER_REALTEK:
		/* rtw_write16(padapter, 0x4cc, 0xffff); */
		/* rtw_write16(padapter, 0x546, 0x01c0); */
		/* disable high power */
		Switch_DM_Func(padapter, (~DYNAMIC_BB_DYNAMIC_TXPWR), false);
		break;
	default:
		pmlmeinfo->turboMode_cts2self = 0;
		pmlmeinfo->turboMode_rtsen = 1;
		break;
	}
}

void update_capinfo(struct adapter *Adapter, u16 updateCap)
{
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	bool		ShortPreamble;

	/*  Check preamble mode, 2005.01.06, by rcnjko. */
	/*  Mark to update preamble value forever, 2008.03.18 by lanhsin */

	if (updateCap & cShortPreamble) { /*  Short Preamble */
		if (pmlmeinfo->preamble_mode != PREAMBLE_SHORT) { /*  PREAMBLE_LONG or PREAMBLE_AUTO */
			ShortPreamble = true;
			pmlmeinfo->preamble_mode = PREAMBLE_SHORT;
			rtw_hal_set_hwreg(Adapter, HW_VAR_ACK_PREAMBLE, (u8 *)&ShortPreamble);
		}
	} else { /*  Long Preamble */
		if (pmlmeinfo->preamble_mode != PREAMBLE_LONG) {  /*  PREAMBLE_SHORT or PREAMBLE_AUTO */
			ShortPreamble = false;
			pmlmeinfo->preamble_mode = PREAMBLE_LONG;
			rtw_hal_set_hwreg(Adapter, HW_VAR_ACK_PREAMBLE, (u8 *)&ShortPreamble);
		}
	}

	if (updateCap & cIBSS) {
		/* Filen: See 802.11-2007 p.91 */
		pmlmeinfo->slotTime = NON_SHORT_SLOT_TIME;
	} else { /* Filen: See 802.11-2007 p.90 */
		if (pmlmeext->cur_wireless_mode & (WIRELESS_11G | WIRELESS_11_24N)) {
			if (updateCap & cShortSlotTime) { /*  Short Slot Time */
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

	rtw_hal_set_hwreg(Adapter, HW_VAR_SLOT_TIME, &pmlmeinfo->slotTime);
}

void update_wireless_mode(struct adapter *padapter)
{
	int ratelen, network_type = 0;
	u32 SIFS_Timer;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex	*cur_network = &(pmlmeinfo->network);
	unsigned char		*rate = cur_network->SupportedRates;

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

	SIFS_Timer = 0x0a0a0808;/* 0x0808 -> for CCK, 0x0a0a -> for OFDM */
				/* change this value if having IOT issues. */

	padapter->HalFunc.SetHwRegHandler(padapter, HW_VAR_RESP_SIFS,  (u8 *)&SIFS_Timer);

	if (pmlmeext->cur_wireless_mode & WIRELESS_11B)
		update_mgnt_tx_rate(padapter, IEEE80211_CCK_RATE_1MB);
	 else
		update_mgnt_tx_rate(padapter, IEEE80211_OFDM_RATE_6MB);
}

void update_bmc_sta_support_rate(struct adapter *padapter, u32 mac_id)
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

int update_sta_support_rate(struct adapter *padapter, u8 *pvar_ie, uint var_ie_len, int cam_idx)
{
	unsigned int	ie_len;
	struct ndis_802_11_var_ie *pIE;
	int	supportRateNum = 0;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	pIE = (struct ndis_802_11_var_ie *)rtw_get_ie(pvar_ie, _SUPPORTEDRATES_IE_, &ie_len, var_ie_len);
	if (pIE == NULL)
		return _FAIL;
	if (ie_len > NDIS_802_11_LENGTH_RATES_EX)
		return _FAIL;

	memcpy(pmlmeinfo->FW_sta_info[cam_idx].SupportedRates, pIE->data, ie_len);
	supportRateNum = ie_len;

	pIE = (struct ndis_802_11_var_ie *)rtw_get_ie(pvar_ie, _EXT_SUPPORTEDRATES_IE_, &ie_len, var_ie_len);
	if (pIE) {
		if (supportRateNum + ie_len > NDIS_802_11_LENGTH_RATES_EX)
			return _FAIL;
		memcpy((pmlmeinfo->FW_sta_info[cam_idx].SupportedRates + supportRateNum), pIE->data, ie_len);
	}

	return _SUCCESS;
}

void process_addba_req(struct adapter *padapter, u8 *paddba_req, u8 *addr)
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
		preorder_ctrl->enable = (pmlmeinfo->bAcceptAddbaReq) ? true : false;
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

void correct_TSF(struct adapter *padapter, struct mlme_ext_priv *pmlmeext)
{
	rtw_hal_set_hwreg(padapter, HW_VAR_CORRECT_TSF, NULL);
}

void beacon_timing_control(struct adapter *padapter)
{
	rtw_hal_bcn_related_reg_setting(padapter);
}
