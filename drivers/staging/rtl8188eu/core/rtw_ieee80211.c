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
#define _IEEE80211_C

#include <drv_types.h>
#include <ieee80211.h>
#include <wifi.h>
#include <osdep_service.h>
#include <wlan_bssdef.h>
#include <usb_osintf.h>

u8 RTW_WPA_OUI_TYPE[] = { 0x00, 0x50, 0xf2, 1 };
u16 RTW_WPA_VERSION = 1;
u8 WPA_AUTH_KEY_MGMT_NONE[] = { 0x00, 0x50, 0xf2, 0 };
u8 WPA_AUTH_KEY_MGMT_UNSPEC_802_1X[] = { 0x00, 0x50, 0xf2, 1 };
u8 WPA_AUTH_KEY_MGMT_PSK_OVER_802_1X[] = { 0x00, 0x50, 0xf2, 2 };
u8 WPA_CIPHER_SUITE_NONE[] = { 0x00, 0x50, 0xf2, 0 };
u8 WPA_CIPHER_SUITE_WEP40[] = { 0x00, 0x50, 0xf2, 1 };
u8 WPA_CIPHER_SUITE_TKIP[] = { 0x00, 0x50, 0xf2, 2 };
u8 WPA_CIPHER_SUITE_WRAP[] = { 0x00, 0x50, 0xf2, 3 };
u8 WPA_CIPHER_SUITE_CCMP[] = { 0x00, 0x50, 0xf2, 4 };
u8 WPA_CIPHER_SUITE_WEP104[] = { 0x00, 0x50, 0xf2, 5 };

u16 RSN_VERSION_BSD = 1;
u8 RSN_AUTH_KEY_MGMT_UNSPEC_802_1X[] = { 0x00, 0x0f, 0xac, 1 };
u8 RSN_AUTH_KEY_MGMT_PSK_OVER_802_1X[] = { 0x00, 0x0f, 0xac, 2 };
u8 RSN_CIPHER_SUITE_NONE[] = { 0x00, 0x0f, 0xac, 0 };
u8 RSN_CIPHER_SUITE_WEP40[] = { 0x00, 0x0f, 0xac, 1 };
u8 RSN_CIPHER_SUITE_TKIP[] = { 0x00, 0x0f, 0xac, 2 };
u8 RSN_CIPHER_SUITE_WRAP[] = { 0x00, 0x0f, 0xac, 3 };
u8 RSN_CIPHER_SUITE_CCMP[] = { 0x00, 0x0f, 0xac, 4 };
u8 RSN_CIPHER_SUITE_WEP104[] = { 0x00, 0x0f, 0xac, 5 };
/*  */
/*  for adhoc-master to generate ie and provide supported-rate to fw */
/*  */

static u8	WIFI_CCKRATES[] = {
	(IEEE80211_CCK_RATE_1MB | IEEE80211_BASIC_RATE_MASK),
	(IEEE80211_CCK_RATE_2MB | IEEE80211_BASIC_RATE_MASK),
	(IEEE80211_CCK_RATE_5MB | IEEE80211_BASIC_RATE_MASK),
	(IEEE80211_CCK_RATE_11MB | IEEE80211_BASIC_RATE_MASK)
	};

static u8	WIFI_OFDMRATES[] = {
	 (IEEE80211_OFDM_RATE_6MB),
	 (IEEE80211_OFDM_RATE_9MB),
	 (IEEE80211_OFDM_RATE_12MB),
	 (IEEE80211_OFDM_RATE_18MB),
	 (IEEE80211_OFDM_RATE_24MB),
	 IEEE80211_OFDM_RATE_36MB,
	 IEEE80211_OFDM_RATE_48MB,
	 IEEE80211_OFDM_RATE_54MB
	};


int rtw_get_bit_value_from_ieee_value(u8 val)
{
	unsigned char dot11_rate_table[] = {
		2, 4, 11, 22, 12, 18, 24, 36, 48,
		72, 96, 108, 0}; /*  last element must be zero!! */

	int i = 0;
	while (dot11_rate_table[i] != 0) {
		if (dot11_rate_table[i] == val)
			return BIT(i);
		i++;
	}
	return 0;
}

uint	rtw_is_cckrates_included(u8 *rate)
{
	u32	i = 0;

	while (rate[i] != 0) {
		if  ((((rate[i]) & 0x7f) == 2) || (((rate[i]) & 0x7f) == 4) ||
		     (((rate[i]) & 0x7f) == 11)  || (((rate[i]) & 0x7f) == 22))
			return true;
		i++;
	}
	return false;
}

uint	rtw_is_cckratesonly_included(u8 *rate)
{
	u32 i = 0;

	while (rate[i] != 0) {
		if  ((((rate[i]) & 0x7f) != 2) && (((rate[i]) & 0x7f) != 4) &&
		     (((rate[i]) & 0x7f) != 11)  && (((rate[i]) & 0x7f) != 22))
			return false;
		i++;
	}

	return true;
}

int rtw_check_network_type(unsigned char *rate, int ratelen, int channel)
{
	if (channel > 14) {
		if ((rtw_is_cckrates_included(rate)) == true)
			return WIRELESS_INVALID;
		else
			return WIRELESS_11A;
	} else {  /*  could be pure B, pure G, or B/G */
		if ((rtw_is_cckratesonly_included(rate)) == true)
			return WIRELESS_11B;
		else if ((rtw_is_cckrates_included(rate)) == true)
			return	WIRELESS_11BG;
		else
			return WIRELESS_11G;
	}
}

u8 *rtw_set_fixed_ie(unsigned char *pbuf, unsigned int len, unsigned char *source,
				unsigned int *frlen)
{
	memcpy((void *)pbuf, (void *)source, len);
	*frlen = *frlen + len;
	return pbuf + len;
}

/*  rtw_set_ie will update frame length */
u8 *rtw_set_ie
(
	u8 *pbuf,
	int index,
	uint len,
	u8 *source,
	uint *frlen /* frame length */
)
{
_func_enter_;
	*pbuf = (u8)index;

	*(pbuf + 1) = (u8)len;

	if (len > 0)
		memcpy((void *)(pbuf + 2), (void *)source, len);

	*frlen = *frlen + (len + 2);

	return pbuf + len + 2;
_func_exit_;
}

inline u8 *rtw_set_ie_ch_switch (u8 *buf, u32 *buf_len, u8 ch_switch_mode,
	u8 new_ch, u8 ch_switch_cnt)
{
	u8 ie_data[3];

	ie_data[0] = ch_switch_mode;
	ie_data[1] = new_ch;
	ie_data[2] = ch_switch_cnt;
	return rtw_set_ie(buf, WLAN_EID_CHANNEL_SWITCH,  3, ie_data, buf_len);
}

inline u8 secondary_ch_offset_to_hal_ch_offset(u8 ch_offset)
{
	if (ch_offset == SCN)
		return HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	else if (ch_offset == SCA)
		return HAL_PRIME_CHNL_OFFSET_UPPER;
	else if (ch_offset == SCB)
		return HAL_PRIME_CHNL_OFFSET_LOWER;

	return HAL_PRIME_CHNL_OFFSET_DONT_CARE;
}

inline u8 hal_ch_offset_to_secondary_ch_offset(u8 ch_offset)
{
	if (ch_offset == HAL_PRIME_CHNL_OFFSET_DONT_CARE)
		return SCN;
	else if (ch_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
		return SCB;
	else if (ch_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
		return SCA;

	return SCN;
}

inline u8 *rtw_set_ie_secondary_ch_offset(u8 *buf, u32 *buf_len, u8 secondary_ch_offset)
{
	return rtw_set_ie(buf, WLAN_EID_SECONDARY_CHANNEL_OFFSET,  1, &secondary_ch_offset, buf_len);
}

inline u8 *rtw_set_ie_mesh_ch_switch_parm(u8 *buf, u32 *buf_len, u8 ttl,
	u8 flags, u16 reason, u16 precedence)
{
	u8 ie_data[6];

	ie_data[0] = ttl;
	ie_data[1] = flags;
	RTW_PUT_LE16((u8 *)&ie_data[2], reason);
	RTW_PUT_LE16((u8 *)&ie_data[4], precedence);

	return rtw_set_ie(buf, 0x118,  6, ie_data, buf_len);
}

/*----------------------------------------------------------------------------
index: the information element id index, limit is the limit for search
-----------------------------------------------------------------------------*/
u8 *rtw_get_ie(u8 *pbuf, int index, int *len, int limit)
{
	int tmp, i;
	u8 *p;
_func_enter_;
	if (limit < 1) {
		_func_exit_;
		return NULL;
	}

	p = pbuf;
	i = 0;
	*len = 0;
	while (1) {
		if (*p == index) {
			*len = *(p + 1);
			return p;
		} else {
			tmp = *(p + 1);
			p += (tmp + 2);
			i += (tmp + 2);
		}
		if (i >= limit)
			break;
	}
_func_exit_;
	return NULL;
}

/**
 * rtw_get_ie_ex - Search specific IE from a series of IEs
 * @in_ie: Address of IEs to search
 * @in_len: Length limit from in_ie
 * @eid: Element ID to match
 * @oui: OUI to match
 * @oui_len: OUI length
 * @ie: If not NULL and the specific IE is found, the IE will be copied to the buf starting from the specific IE
 * @ielen: If not NULL and the specific IE is found, will set to the length of the entire IE
 *
 * Returns: The address of the specific IE found, or NULL
 */
u8 *rtw_get_ie_ex(u8 *in_ie, uint in_len, u8 eid, u8 *oui, u8 oui_len, u8 *ie, uint *ielen)
{
	uint cnt;
	u8 *target_ie = NULL;


	if (ielen)
		*ielen = 0;

	if (!in_ie || in_len <= 0)
		return target_ie;

	cnt = 0;

	while (cnt < in_len) {
		if (eid == in_ie[cnt] && (!oui || _rtw_memcmp(&in_ie[cnt+2], oui, oui_len))) {
			target_ie = &in_ie[cnt];

			if (ie)
				memcpy(ie, &in_ie[cnt], in_ie[cnt+1]+2);

			if (ielen)
				*ielen = in_ie[cnt+1]+2;

			break;
		} else {
			cnt += in_ie[cnt+1]+2; /* goto next */
		}
	}
	return target_ie;
}

/**
 * rtw_ies_remove_ie - Find matching IEs and remove
 * @ies: Address of IEs to search
 * @ies_len: Pointer of length of ies, will update to new length
 * @offset: The offset to start scarch
 * @eid: Element ID to match
 * @oui: OUI to match
 * @oui_len: OUI length
 *
 * Returns: _SUCCESS: ies is updated, _FAIL: not updated
 */
int rtw_ies_remove_ie(u8 *ies, uint *ies_len, uint offset, u8 eid, u8 *oui, u8 oui_len)
{
	int ret = _FAIL;
	u8 *target_ie;
	u32 target_ielen;
	u8 *start;
	uint search_len;

	if (!ies || !ies_len || *ies_len <= offset)
		goto exit;

	start = ies + offset;
	search_len = *ies_len - offset;

	while (1) {
		target_ie = rtw_get_ie_ex(start, search_len, eid, oui, oui_len, NULL, &target_ielen);
		if (target_ie && target_ielen) {
			u8 buf[MAX_IE_SZ] = {0};
			u8 *remain_ies = target_ie + target_ielen;
			uint remain_len = search_len - (remain_ies - start);

			memcpy(buf, remain_ies, remain_len);
			memcpy(target_ie, buf, remain_len);
			*ies_len = *ies_len - target_ielen;
			ret = _SUCCESS;

			start = target_ie;
			search_len = remain_len;
		} else {
			break;
		}
	}
exit:
	return ret;
}

void rtw_set_supported_rate(u8 *SupportedRates, uint mode)
{
_func_enter_;

	_rtw_memset(SupportedRates, 0, NDIS_802_11_LENGTH_RATES_EX);

	switch (mode) {
	case WIRELESS_11B:
		memcpy(SupportedRates, WIFI_CCKRATES, IEEE80211_CCK_RATE_LEN);
		break;
	case WIRELESS_11G:
	case WIRELESS_11A:
	case WIRELESS_11_5N:
	case WIRELESS_11A_5N:/* Todo: no basic rate for ofdm ? */
		memcpy(SupportedRates, WIFI_OFDMRATES, IEEE80211_NUM_OFDM_RATESLEN);
		break;
	case WIRELESS_11BG:
	case WIRELESS_11G_24N:
	case WIRELESS_11_24N:
	case WIRELESS_11BG_24N:
		memcpy(SupportedRates, WIFI_CCKRATES, IEEE80211_CCK_RATE_LEN);
		memcpy(SupportedRates + IEEE80211_CCK_RATE_LEN, WIFI_OFDMRATES, IEEE80211_NUM_OFDM_RATESLEN);
		break;
	}
_func_exit_;
}

uint	rtw_get_rateset_len(u8	*rateset)
{
	uint i = 0;
_func_enter_;
	while (1) {
		if ((rateset[i]) == 0)
			break;
		if (i > 12)
			break;
		i++;
	}
_func_exit_;
	return i;
}

int rtw_generate_ie(struct registry_priv *pregistrypriv)
{
	u8	wireless_mode;
	int	sz = 0, rateLen;
	struct wlan_bssid_ex *pdev_network = &pregistrypriv->dev_network;
	u8 *ie = pdev_network->IEs;

_func_enter_;

	/* timestamp will be inserted by hardware */
	sz += 8;
	ie += sz;

	/* beacon interval : 2bytes */
	*(__le16 *)ie = cpu_to_le16((u16)pdev_network->Configuration.BeaconPeriod);/* BCN_INTERVAL; */
	sz += 2;
	ie += 2;

	/* capability info */
	*(u16 *)ie = 0;

	*(__le16 *)ie |= cpu_to_le16(cap_IBSS);

	if (pregistrypriv->preamble == PREAMBLE_SHORT)
		*(__le16 *)ie |= cpu_to_le16(cap_ShortPremble);

	if (pdev_network->Privacy)
		*(__le16 *)ie |= cpu_to_le16(cap_Privacy);

	sz += 2;
	ie += 2;

	/* SSID */
	ie = rtw_set_ie(ie, _SSID_IE_, pdev_network->Ssid.SsidLength, pdev_network->Ssid.Ssid, &sz);

	/* supported rates */
	if (pregistrypriv->wireless_mode == WIRELESS_11ABGN) {
		if (pdev_network->Configuration.DSConfig > 14)
			wireless_mode = WIRELESS_11A_5N;
		else
			wireless_mode = WIRELESS_11BG_24N;
	} else {
		wireless_mode = pregistrypriv->wireless_mode;
	}

		rtw_set_supported_rate(pdev_network->SupportedRates, wireless_mode);

	rateLen = rtw_get_rateset_len(pdev_network->SupportedRates);

	if (rateLen > 8) {
		ie = rtw_set_ie(ie, _SUPPORTEDRATES_IE_, 8, pdev_network->SupportedRates, &sz);
		/* ie = rtw_set_ie(ie, _EXT_SUPPORTEDRATES_IE_, (rateLen - 8), (pdev_network->SupportedRates + 8), &sz); */
	} else {
		ie = rtw_set_ie(ie, _SUPPORTEDRATES_IE_, rateLen, pdev_network->SupportedRates, &sz);
	}

	/* DS parameter set */
	ie = rtw_set_ie(ie, _DSSET_IE_, 1, (u8 *)&(pdev_network->Configuration.DSConfig), &sz);

	/* IBSS Parameter Set */

	ie = rtw_set_ie(ie, _IBSS_PARA_IE_, 2, (u8 *)&(pdev_network->Configuration.ATIMWindow), &sz);

	if (rateLen > 8)
		ie = rtw_set_ie(ie, _EXT_SUPPORTEDRATES_IE_, (rateLen - 8), (pdev_network->SupportedRates + 8), &sz);
_func_exit_;

	return sz;
}

unsigned char *rtw_get_wpa_ie(unsigned char *pie, int *wpa_ie_len, int limit)
{
	int len;
	u16 val16;
	__le16 le_tmp;
	unsigned char wpa_oui_type[] = {0x00, 0x50, 0xf2, 0x01};
	u8 *pbuf = pie;
	int limit_new = limit;

	while (1) {
		pbuf = rtw_get_ie(pbuf, _WPA_IE_ID_, &len, limit_new);

		if (pbuf) {
			/* check if oui matches... */
			if (_rtw_memcmp((pbuf + 2), wpa_oui_type, sizeof (wpa_oui_type)) == false)
				goto check_next_ie;

			/* check version... */
			memcpy((u8 *)&le_tmp, (pbuf + 6), sizeof(val16));

			val16 = le16_to_cpu(le_tmp);
			if (val16 != 0x0001)
				goto check_next_ie;
			*wpa_ie_len = *(pbuf + 1);
			return pbuf;
		} else {
			*wpa_ie_len = 0;
			return NULL;
		}

check_next_ie:
		limit_new = limit - (pbuf - pie) - 2 - len;
		if (limit_new <= 0)
			break;
		pbuf += (2 + len);
	}
	*wpa_ie_len = 0;
	return NULL;
}

unsigned char *rtw_get_wpa2_ie(unsigned char *pie, int *rsn_ie_len, int limit)
{

	return rtw_get_ie(pie, _WPA2_IE_ID_, rsn_ie_len, limit);
}

int rtw_get_wpa_cipher_suite(u8 *s)
{
	if (_rtw_memcmp(s, WPA_CIPHER_SUITE_NONE, WPA_SELECTOR_LEN) == true)
		return WPA_CIPHER_NONE;
	if (_rtw_memcmp(s, WPA_CIPHER_SUITE_WEP40, WPA_SELECTOR_LEN) == true)
		return WPA_CIPHER_WEP40;
	if (_rtw_memcmp(s, WPA_CIPHER_SUITE_TKIP, WPA_SELECTOR_LEN) == true)
		return WPA_CIPHER_TKIP;
	if (_rtw_memcmp(s, WPA_CIPHER_SUITE_CCMP, WPA_SELECTOR_LEN) == true)
		return WPA_CIPHER_CCMP;
	if (_rtw_memcmp(s, WPA_CIPHER_SUITE_WEP104, WPA_SELECTOR_LEN) == true)
		return WPA_CIPHER_WEP104;

	return 0;
}

int rtw_get_wpa2_cipher_suite(u8 *s)
{
	if (_rtw_memcmp(s, RSN_CIPHER_SUITE_NONE, RSN_SELECTOR_LEN) == true)
		return WPA_CIPHER_NONE;
	if (_rtw_memcmp(s, RSN_CIPHER_SUITE_WEP40, RSN_SELECTOR_LEN) == true)
		return WPA_CIPHER_WEP40;
	if (_rtw_memcmp(s, RSN_CIPHER_SUITE_TKIP, RSN_SELECTOR_LEN) == true)
		return WPA_CIPHER_TKIP;
	if (_rtw_memcmp(s, RSN_CIPHER_SUITE_CCMP, RSN_SELECTOR_LEN) == true)
		return WPA_CIPHER_CCMP;
	if (_rtw_memcmp(s, RSN_CIPHER_SUITE_WEP104, RSN_SELECTOR_LEN) == true)
		return WPA_CIPHER_WEP104;

	return 0;
}


int rtw_parse_wpa_ie(u8 *wpa_ie, int wpa_ie_len, int *group_cipher, int *pairwise_cipher, int *is_8021x)
{
	int i, ret = _SUCCESS;
	int left, count;
	u8 *pos;
	u8 SUITE_1X[4] = {0x00, 0x50, 0xf2, 1};

	if (wpa_ie_len <= 0) {
		/* No WPA IE - fail silently */
		return _FAIL;
	}


	if ((*wpa_ie != _WPA_IE_ID_) || (*(wpa_ie+1) != (u8)(wpa_ie_len - 2)) ||
	    (_rtw_memcmp(wpa_ie+2, RTW_WPA_OUI_TYPE, WPA_SELECTOR_LEN) != true))
		return _FAIL;

	pos = wpa_ie;

	pos += 8;
	left = wpa_ie_len - 8;


	/* group_cipher */
	if (left >= WPA_SELECTOR_LEN) {
		*group_cipher = rtw_get_wpa_cipher_suite(pos);
		pos += WPA_SELECTOR_LEN;
		left -= WPA_SELECTOR_LEN;
	} else if (left > 0) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("%s: ie length mismatch, %u too much", __func__, left));
		return _FAIL;
	}

	/* pairwise_cipher */
	if (left >= 2) {
		count = RTW_GET_LE16(pos);
		pos += 2;
		left -= 2;

		if (count == 0 || left < count * WPA_SELECTOR_LEN) {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("%s: ie count botch (pairwise), "
						"count %u left %u", __func__, count, left));
			return _FAIL;
		}

		for (i = 0; i < count; i++) {
			*pairwise_cipher |= rtw_get_wpa_cipher_suite(pos);

			pos += WPA_SELECTOR_LEN;
			left -= WPA_SELECTOR_LEN;
		}
	} else if (left == 1) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("%s: ie too short (for key mgmt)",   __func__));
		return _FAIL;
	}

	if (is_8021x) {
		if (left >= 6) {
			pos += 2;
			if (_rtw_memcmp(pos, SUITE_1X, 4) == 1) {
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("%s : there has 802.1x auth\n", __func__));
				*is_8021x = 1;
			}
		}
	}

	return ret;
}

int rtw_parse_wpa2_ie(u8 *rsn_ie, int rsn_ie_len, int *group_cipher, int *pairwise_cipher, int *is_8021x)
{
	int i, ret = _SUCCESS;
	int left, count;
	u8 *pos;
	u8 SUITE_1X[4] = {0x00, 0x0f, 0xac, 0x01};

	if (rsn_ie_len <= 0) {
		/* No RSN IE - fail silently */
		return _FAIL;
	}


	if ((*rsn_ie != _WPA2_IE_ID_) || (*(rsn_ie+1) != (u8)(rsn_ie_len - 2)))
		return _FAIL;

	pos = rsn_ie;
	pos += 4;
	left = rsn_ie_len - 4;

	/* group_cipher */
	if (left >= RSN_SELECTOR_LEN) {
		*group_cipher = rtw_get_wpa2_cipher_suite(pos);

		pos += RSN_SELECTOR_LEN;
		left -= RSN_SELECTOR_LEN;

	} else if (left > 0) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("%s: ie length mismatch, %u too much", __func__, left));
		return _FAIL;
	}

	/* pairwise_cipher */
	if (left >= 2) {
		count = RTW_GET_LE16(pos);
		pos += 2;
		left -= 2;

		if (count == 0 || left < count * RSN_SELECTOR_LEN) {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("%s: ie count botch (pairwise), "
						 "count %u left %u", __func__, count, left));
			return _FAIL;
		}

		for (i = 0; i < count; i++) {
			*pairwise_cipher |= rtw_get_wpa2_cipher_suite(pos);

			pos += RSN_SELECTOR_LEN;
			left -= RSN_SELECTOR_LEN;
		}

	} else if (left == 1) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_, ("%s: ie too short (for key mgmt)",  __func__));

		return _FAIL;
	}

	if (is_8021x) {
		if (left >= 6) {
			pos += 2;
			if (_rtw_memcmp(pos, SUITE_1X, 4) == 1) {
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("%s (): there has 802.1x auth\n", __func__));
				*is_8021x = 1;
			}
		}
	}
	return ret;
}

int rtw_get_sec_ie(u8 *in_ie, uint in_len, u8 *rsn_ie, u16 *rsn_len, u8 *wpa_ie, u16 *wpa_len)
{
	u8 authmode, sec_idx, i;
	u8 wpa_oui[4] = {0x0, 0x50, 0xf2, 0x01};
	uint	cnt;

_func_enter_;

	/* Search required WPA or WPA2 IE and copy to sec_ie[] */

	cnt = (_TIMESTAMP_ + _BEACON_ITERVAL_ + _CAPABILITY_);

	sec_idx = 0;

	while (cnt < in_len) {
		authmode = in_ie[cnt];

		if ((authmode == _WPA_IE_ID_) && (_rtw_memcmp(&in_ie[cnt+2], &wpa_oui[0], 4))) {
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
					 ("\n rtw_get_wpa_ie: sec_idx =%d in_ie[cnt+1]+2 =%d\n",
					 sec_idx, in_ie[cnt+1]+2));

				if (wpa_ie) {
					memcpy(wpa_ie, &in_ie[cnt], in_ie[cnt+1]+2);

					for (i = 0; i < (in_ie[cnt+1]+2); i += 8) {
						RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
							 ("\n %2x,%2x,%2x,%2x,%2x,%2x,%2x,%2x\n",
							 wpa_ie[i], wpa_ie[i+1], wpa_ie[i+2], wpa_ie[i+3], wpa_ie[i+4],
							 wpa_ie[i+5], wpa_ie[i+6], wpa_ie[i+7]));
					}
				}

				*wpa_len = in_ie[cnt+1]+2;
				cnt += in_ie[cnt+1]+2;  /* get next */
		} else {
			if (authmode == _WPA2_IE_ID_) {
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
					 ("\n get_rsn_ie: sec_idx =%d in_ie[cnt+1]+2 =%d\n",
					 sec_idx, in_ie[cnt+1]+2));

				if (rsn_ie) {
					memcpy(rsn_ie, &in_ie[cnt], in_ie[cnt+1]+2);

					for (i = 0; i < (in_ie[cnt+1]+2); i += 8) {
						RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
							 ("\n %2x,%2x,%2x,%2x,%2x,%2x,%2x,%2x\n",
							 rsn_ie[i], rsn_ie[i+1], rsn_ie[i+2], rsn_ie[i+3], rsn_ie[i+4],
							 rsn_ie[i+5], rsn_ie[i+6], rsn_ie[i+7]));
						}
				}

				*rsn_len = in_ie[cnt+1]+2;
				cnt += in_ie[cnt+1]+2;  /* get next */
			} else {
				cnt += in_ie[cnt+1]+2;   /* get next */
			}
		}
	}

_func_exit_;

	return *rsn_len + *wpa_len;
}

u8 rtw_is_wps_ie(u8 *ie_ptr, uint *wps_ielen)
{
	u8 match = false;
	u8 eid, wps_oui[4] = {0x0, 0x50, 0xf2, 0x04};

	if (ie_ptr == NULL)
		return match;

	eid = ie_ptr[0];

	if ((eid == _WPA_IE_ID_) && (_rtw_memcmp(&ie_ptr[2], wps_oui, 4))) {
		*wps_ielen = ie_ptr[1]+2;
		match = true;
	}
	return match;
}

/**
 * rtw_get_wps_ie - Search WPS IE from a series of IEs
 * @in_ie: Address of IEs to search
 * @in_len: Length limit from in_ie
 * @wps_ie: If not NULL and WPS IE is found, WPS IE will be copied to the buf starting from wps_ie
 * @wps_ielen: If not NULL and WPS IE is found, will set to the length of the entire WPS IE
 *
 * Returns: The address of the WPS IE found, or NULL
 */
u8 *rtw_get_wps_ie(u8 *in_ie, uint in_len, u8 *wps_ie, uint *wps_ielen)
{
	uint cnt;
	u8 *wpsie_ptr = NULL;
	u8 eid, wps_oui[4] = {0x0, 0x50, 0xf2, 0x04};

	if (wps_ielen)
		*wps_ielen = 0;

	if (!in_ie || in_len <= 0)
		return wpsie_ptr;

	cnt = 0;

	while (cnt < in_len) {
		eid = in_ie[cnt];

		if ((eid == _WPA_IE_ID_) && (_rtw_memcmp(&in_ie[cnt+2], wps_oui, 4))) {
			wpsie_ptr = &in_ie[cnt];

			if (wps_ie)
				memcpy(wps_ie, &in_ie[cnt], in_ie[cnt+1]+2);

			if (wps_ielen)
				*wps_ielen = in_ie[cnt+1]+2;

			cnt += in_ie[cnt+1]+2;

			break;
		} else {
			cnt += in_ie[cnt+1]+2; /* goto next */
		}
	}
	return wpsie_ptr;
}

/**
 * rtw_get_wps_attr - Search a specific WPS attribute from a given WPS IE
 * @wps_ie: Address of WPS IE to search
 * @wps_ielen: Length limit from wps_ie
 * @target_attr_id: The attribute ID of WPS attribute to search
 * @buf_attr: If not NULL and the WPS attribute is found, WPS attribute will be copied to the buf starting from buf_attr
 * @len_attr: If not NULL and the WPS attribute is found, will set to the length of the entire WPS attribute
 *
 * Returns: the address of the specific WPS attribute found, or NULL
 */
u8 *rtw_get_wps_attr(u8 *wps_ie, uint wps_ielen, u16 target_attr_id , u8 *buf_attr, u32 *len_attr)
{
	u8 *attr_ptr = NULL;
	u8 *target_attr_ptr = NULL;
	u8 wps_oui[4] = {0x00, 0x50, 0xF2, 0x04};

	if (len_attr)
		*len_attr = 0;

	if ((wps_ie[0] != _VENDOR_SPECIFIC_IE_) ||
	    (_rtw_memcmp(wps_ie + 2, wps_oui , 4) != true))
		return attr_ptr;

	/*  6 = 1(Element ID) + 1(Length) + 4(WPS OUI) */
	attr_ptr = wps_ie + 6; /* goto first attr */

	while (attr_ptr - wps_ie < wps_ielen) {
		/*  4 = 2(Attribute ID) + 2(Length) */
		u16 attr_id = RTW_GET_BE16(attr_ptr);
		u16 attr_data_len = RTW_GET_BE16(attr_ptr + 2);
		u16 attr_len = attr_data_len + 4;

		if (attr_id == target_attr_id) {
			target_attr_ptr = attr_ptr;
			if (buf_attr)
				memcpy(buf_attr, attr_ptr, attr_len);
			if (len_attr)
				*len_attr = attr_len;
			break;
		} else {
			attr_ptr += attr_len; /* goto next */
		}
	}
	return target_attr_ptr;
}

/**
 * rtw_get_wps_attr_content - Search a specific WPS attribute content from a given WPS IE
 * @wps_ie: Address of WPS IE to search
 * @wps_ielen: Length limit from wps_ie
 * @target_attr_id: The attribute ID of WPS attribute to search
 * @buf_content: If not NULL and the WPS attribute is found, WPS attribute content will be copied to the buf starting from buf_content
 * @len_content: If not NULL and the WPS attribute is found, will set to the length of the WPS attribute content
 *
 * Returns: the address of the specific WPS attribute content found, or NULL
 */
u8 *rtw_get_wps_attr_content(u8 *wps_ie, uint wps_ielen, u16 target_attr_id , u8 *buf_content, uint *len_content)
{
	u8 *attr_ptr;
	u32 attr_len;

	if (len_content)
		*len_content = 0;

	attr_ptr = rtw_get_wps_attr(wps_ie, wps_ielen, target_attr_id, NULL, &attr_len);

	if (attr_ptr && attr_len) {
		if (buf_content)
			memcpy(buf_content, attr_ptr+4, attr_len-4);

		if (len_content)
			*len_content = attr_len-4;

		return attr_ptr+4;
	}

	return NULL;
}

static int rtw_ieee802_11_parse_vendor_specific(u8 *pos, uint elen,
					    struct rtw_ieee802_11_elems *elems,
					    int show_errors)
{
	unsigned int oui;

	/* first 3 bytes in vendor specific information element are the IEEE
	 * OUI of the vendor. The following byte is used a vendor specific
	 * sub-type. */
	if (elen < 4) {
		if (show_errors) {
			DBG_88E("short vendor specific information element ignored (len=%lu)\n",
				(unsigned long) elen);
		}
		return -1;
	}

	oui = RTW_GET_BE24(pos);
	switch (oui) {
	case OUI_MICROSOFT:
		/* Microsoft/Wi-Fi information elements are further typed and
		 * subtyped */
		switch (pos[3]) {
		case 1:
			/* Microsoft OUI (00:50:F2) with OUI Type 1:
			 * real WPA information element */
			elems->wpa_ie = pos;
			elems->wpa_ie_len = elen;
			break;
		case WME_OUI_TYPE: /* this is a Wi-Fi WME info. element */
			if (elen < 5) {
				DBG_88E("short WME information element ignored (len=%lu)\n",
					(unsigned long) elen);
				return -1;
			}
			switch (pos[4]) {
			case WME_OUI_SUBTYPE_INFORMATION_ELEMENT:
			case WME_OUI_SUBTYPE_PARAMETER_ELEMENT:
				elems->wme = pos;
				elems->wme_len = elen;
				break;
			case WME_OUI_SUBTYPE_TSPEC_ELEMENT:
				elems->wme_tspec = pos;
				elems->wme_tspec_len = elen;
				break;
			default:
				DBG_88E("unknown WME information element ignored (subtype=%d len=%lu)\n",
					pos[4], (unsigned long) elen);
				return -1;
			}
			break;
		case 4:
			/* Wi-Fi Protected Setup (WPS) IE */
			elems->wps_ie = pos;
			elems->wps_ie_len = elen;
			break;
		default:
			DBG_88E("Unknown Microsoft information element ignored (type=%d len=%lu)\n",
				pos[3], (unsigned long) elen);
			return -1;
		}
		break;

	case OUI_BROADCOM:
		switch (pos[3]) {
		case VENDOR_HT_CAPAB_OUI_TYPE:
			elems->vendor_ht_cap = pos;
			elems->vendor_ht_cap_len = elen;
			break;
		default:
			DBG_88E("Unknown Broadcom information element ignored (type=%d len=%lu)\n",
				pos[3], (unsigned long) elen);
			return -1;
		}
		break;
	default:
		DBG_88E("unknown vendor specific information element ignored (vendor OUI %02x:%02x:%02x len=%lu)\n",
			pos[0], pos[1], pos[2], (unsigned long) elen);
		return -1;
	}
	return 0;
}

/**
 * ieee802_11_parse_elems - Parse information elements in management frames
 * @start: Pointer to the start of IEs
 * @len: Length of IE buffer in octets
 * @elems: Data structure for parsed elements
 * @show_errors: Whether to show parsing errors in debug log
 * Returns: Parsing result
 */
enum parse_res rtw_ieee802_11_parse_elems(u8 *start, uint len,
				struct rtw_ieee802_11_elems *elems,
				int show_errors)
{
	uint left = len;
	u8 *pos = start;
	int unknown = 0;

	_rtw_memset(elems, 0, sizeof(*elems));

	while (left >= 2) {
		u8 id, elen;

		id = *pos++;
		elen = *pos++;
		left -= 2;

		if (elen > left) {
			if (show_errors) {
				DBG_88E("IEEE 802.11 element parse failed (id=%d elen=%d left=%lu)\n",
					id, elen, (unsigned long) left);
			}
			return ParseFailed;
		}

		switch (id) {
		case WLAN_EID_SSID:
			elems->ssid = pos;
			elems->ssid_len = elen;
			break;
		case WLAN_EID_SUPP_RATES:
			elems->supp_rates = pos;
			elems->supp_rates_len = elen;
			break;
		case WLAN_EID_FH_PARAMS:
			elems->fh_params = pos;
			elems->fh_params_len = elen;
			break;
		case WLAN_EID_DS_PARAMS:
			elems->ds_params = pos;
			elems->ds_params_len = elen;
			break;
		case WLAN_EID_CF_PARAMS:
			elems->cf_params = pos;
			elems->cf_params_len = elen;
			break;
		case WLAN_EID_TIM:
			elems->tim = pos;
			elems->tim_len = elen;
			break;
		case WLAN_EID_IBSS_PARAMS:
			elems->ibss_params = pos;
			elems->ibss_params_len = elen;
			break;
		case WLAN_EID_CHALLENGE:
			elems->challenge = pos;
			elems->challenge_len = elen;
			break;
		case WLAN_EID_ERP_INFO:
			elems->erp_info = pos;
			elems->erp_info_len = elen;
			break;
		case WLAN_EID_EXT_SUPP_RATES:
			elems->ext_supp_rates = pos;
			elems->ext_supp_rates_len = elen;
			break;
		case WLAN_EID_VENDOR_SPECIFIC:
			if (rtw_ieee802_11_parse_vendor_specific(pos, elen, elems, show_errors))
				unknown++;
			break;
		case WLAN_EID_RSN:
			elems->rsn_ie = pos;
			elems->rsn_ie_len = elen;
			break;
		case WLAN_EID_PWR_CAPABILITY:
			elems->power_cap = pos;
			elems->power_cap_len = elen;
			break;
		case WLAN_EID_SUPPORTED_CHANNELS:
			elems->supp_channels = pos;
			elems->supp_channels_len = elen;
			break;
		case WLAN_EID_MOBILITY_DOMAIN:
			elems->mdie = pos;
			elems->mdie_len = elen;
			break;
		case WLAN_EID_FAST_BSS_TRANSITION:
			elems->ftie = pos;
			elems->ftie_len = elen;
			break;
		case WLAN_EID_TIMEOUT_INTERVAL:
			elems->timeout_int = pos;
			elems->timeout_int_len = elen;
			break;
		case WLAN_EID_HT_CAP:
			elems->ht_capabilities = pos;
			elems->ht_capabilities_len = elen;
			break;
		case WLAN_EID_HT_OPERATION:
			elems->ht_operation = pos;
			elems->ht_operation_len = elen;
			break;
		default:
			unknown++;
			if (!show_errors)
				break;
			DBG_88E("IEEE 802.11 element parse ignored unknown element (id=%d elen=%d)\n",
				id, elen);
			break;
		}
		left -= elen;
		pos += elen;
	}
	if (left)
		return ParseFailed;
	return unknown ? ParseUnknown : ParseOK;
}

u8 key_char2num(u8 ch)
{
	if ((ch >= '0') && (ch <= '9'))
		return ch - '0';
	else if ((ch >= 'a') && (ch <= 'f'))
		return ch - 'a' + 10;
	else if ((ch >= 'A') && (ch <= 'F'))
		return ch - 'A' + 10;
	else
		return 0xff;
}

u8 str_2char2num(u8 hch, u8 lch)
{
    return (key_char2num(hch) * 10) + key_char2num(lch);
}

u8 key_2char2num(u8 hch, u8 lch)
{
    return (key_char2num(hch) << 4) | key_char2num(lch);
}

void rtw_macaddr_cfg(u8 *mac_addr)
{
	u8 mac[ETH_ALEN];
	if (mac_addr == NULL)
		return;

	if (rtw_initmac) {	/* Users specify the mac address */
		int jj, kk;

		for (jj = 0, kk = 0; jj < ETH_ALEN; jj++, kk += 3)
			mac[jj] = key_2char2num(rtw_initmac[kk], rtw_initmac[kk + 1]);
		memcpy(mac_addr, mac, ETH_ALEN);
	} else {	/* Use the mac address stored in the Efuse */
		memcpy(mac, mac_addr, ETH_ALEN);
	}

	if (((mac[0] == 0xff) && (mac[1] == 0xff) && (mac[2] == 0xff) &&
	     (mac[3] == 0xff) && (mac[4] == 0xff) && (mac[5] == 0xff)) ||
	    ((mac[0] == 0x0) && (mac[1] == 0x0) && (mac[2] == 0x0) &&
	     (mac[3] == 0x0) && (mac[4] == 0x0) && (mac[5] == 0x0))) {
		mac[0] = 0x00;
		mac[1] = 0xe0;
		mac[2] = 0x4c;
		mac[3] = 0x87;
		mac[4] = 0x00;
		mac[5] = 0x00;
		/*  use default mac addresss */
		memcpy(mac_addr, mac, ETH_ALEN);
		DBG_88E("MAC Address from efuse error, assign default one !!!\n");
	}

	DBG_88E("rtw_macaddr_cfg MAC Address  = %pM\n", (mac_addr));
}

void dump_ies(u8 *buf, u32 buf_len)
{
	u8 *pos = (u8 *)buf;
	u8 id, len;

	while (pos-buf <= buf_len) {
		id = *pos;
		len = *(pos+1);

		DBG_88E("%s ID:%u, LEN:%u\n", __func__, id, len);
		#ifdef CONFIG_88EU_P2P
		dump_p2p_ie(pos, len);
		#endif
		dump_wps_ie(pos, len);

		pos += (2 + len);
	}
}

void dump_wps_ie(u8 *ie, u32 ie_len)
{
	u8 *pos = (u8 *)ie;
	u16 id;
	u16 len;
	u8 *wps_ie;
	uint wps_ielen;

	wps_ie = rtw_get_wps_ie(ie, ie_len, NULL, &wps_ielen);
	if (wps_ie != ie || wps_ielen == 0)
		return;

	pos += 6;
	while (pos-ie < ie_len) {
		id = RTW_GET_BE16(pos);
		len = RTW_GET_BE16(pos + 2);
		DBG_88E("%s ID:0x%04x, LEN:%u\n", __func__, id, len);
		pos += (4+len);
	}
}

#ifdef CONFIG_88EU_P2P
void dump_p2p_ie(u8 *ie, u32 ie_len)
{
	u8 *pos = (u8 *)ie;
	u8 id;
	u16 len;
	u8 *p2p_ie;
	uint p2p_ielen;

	p2p_ie = rtw_get_p2p_ie(ie, ie_len, NULL, &p2p_ielen);
	if (p2p_ie != ie || p2p_ielen == 0)
		return;

	pos += 6;
	while (pos-ie < ie_len) {
		id = *pos;
		len = RTW_GET_LE16(pos+1);
		DBG_88E("%s ID:%u, LEN:%u\n", __func__, id, len);
		pos += (3+len);
	}
}

/**
 * rtw_get_p2p_ie - Search P2P IE from a series of IEs
 * @in_ie: Address of IEs to search
 * @in_len: Length limit from in_ie
 * @p2p_ie: If not NULL and P2P IE is found, P2P IE will be copied to the buf starting from p2p_ie
 * @p2p_ielen: If not NULL and P2P IE is found, will set to the length of the entire P2P IE
 *
 * Returns: The address of the P2P IE found, or NULL
 */
u8 *rtw_get_p2p_ie(u8 *in_ie, int in_len, u8 *p2p_ie, uint *p2p_ielen)
{
	uint cnt = 0;
	u8 *p2p_ie_ptr;
	u8 eid, p2p_oui[4] = {0x50, 0x6F, 0x9A, 0x09};

	if (p2p_ielen != NULL)
		*p2p_ielen = 0;

	while (cnt < in_len) {
		eid = in_ie[cnt];
		if ((in_len < 0) || (cnt > MAX_IE_SZ)) {
			dump_stack();
			return NULL;
		}
		if ((eid == _VENDOR_SPECIFIC_IE_) && (_rtw_memcmp(&in_ie[cnt+2], p2p_oui, 4) == true)) {
			p2p_ie_ptr = in_ie + cnt;

			if (p2p_ie != NULL)
				memcpy(p2p_ie, &in_ie[cnt], in_ie[cnt + 1] + 2);
			if (p2p_ielen != NULL)
				*p2p_ielen = in_ie[cnt + 1] + 2;
			return p2p_ie_ptr;
		} else {
			cnt += in_ie[cnt + 1] + 2; /* goto next */
		}
	}
	return NULL;
}

/**
 * rtw_get_p2p_attr - Search a specific P2P attribute from a given P2P IE
 * @p2p_ie: Address of P2P IE to search
 * @p2p_ielen: Length limit from p2p_ie
 * @target_attr_id: The attribute ID of P2P attribute to search
 * @buf_attr: If not NULL and the P2P attribute is found, P2P attribute will be copied to the buf starting from buf_attr
 * @len_attr: If not NULL and the P2P attribute is found, will set to the length of the entire P2P attribute
 *
 * Returns: the address of the specific WPS attribute found, or NULL
 */
u8 *rtw_get_p2p_attr(u8 *p2p_ie, uint p2p_ielen, u8 target_attr_id , u8 *buf_attr, u32 *len_attr)
{
	u8 *attr_ptr = NULL;
	u8 *target_attr_ptr = NULL;
	u8 p2p_oui[4] = {0x50, 0x6F, 0x9A, 0x09};

	if (len_attr)
		*len_attr = 0;

	if (!p2p_ie || (p2p_ie[0] != _VENDOR_SPECIFIC_IE_) ||
	    (_rtw_memcmp(p2p_ie + 2, p2p_oui , 4) != true))
		return attr_ptr;

	/*  6 = 1(Element ID) + 1(Length) + 3 (OUI) + 1(OUI Type) */
	attr_ptr = p2p_ie + 6; /* goto first attr */

	while (attr_ptr - p2p_ie < p2p_ielen) {
		/*  3 = 1(Attribute ID) + 2(Length) */
		u8 attr_id = *attr_ptr;
		u16 attr_data_len = RTW_GET_LE16(attr_ptr + 1);
		u16 attr_len = attr_data_len + 3;

		if (attr_id == target_attr_id) {
			target_attr_ptr = attr_ptr;

			if (buf_attr)
				memcpy(buf_attr, attr_ptr, attr_len);
			if (len_attr)
				*len_attr = attr_len;
			break;
		} else {
			attr_ptr += attr_len; /* goto next */
		}
	}
	return target_attr_ptr;
}

/**
 * rtw_get_p2p_attr_content - Search a specific P2P attribute content from a given P2P IE
 * @p2p_ie: Address of P2P IE to search
 * @p2p_ielen: Length limit from p2p_ie
 * @target_attr_id: The attribute ID of P2P attribute to search
 * @buf_content: If not NULL and the P2P attribute is found, P2P attribute content will be copied to the buf starting from buf_content
 * @len_content: If not NULL and the P2P attribute is found, will set to the length of the P2P attribute content
 *
 * Returns: the address of the specific P2P attribute content found, or NULL
 */
u8 *rtw_get_p2p_attr_content(u8 *p2p_ie, uint p2p_ielen, u8 target_attr_id , u8 *buf_content, uint *len_content)
{
	u8 *attr_ptr;
	u32 attr_len;

	if (len_content)
		*len_content = 0;

	attr_ptr = rtw_get_p2p_attr(p2p_ie, p2p_ielen, target_attr_id, NULL, &attr_len);

	if (attr_ptr && attr_len) {
		if (buf_content)
			memcpy(buf_content, attr_ptr+3, attr_len-3);

		if (len_content)
			*len_content = attr_len-3;

		return attr_ptr+3;
	}

	return NULL;
}

u32 rtw_set_p2p_attr_content(u8 *pbuf, u8 attr_id, u16 attr_len, u8 *pdata_attr)
{
	u32 a_len;

	*pbuf = attr_id;

	/* u16*)(pbuf + 1) = cpu_to_le16(attr_len); */
	RTW_PUT_LE16(pbuf + 1, attr_len);

	if (pdata_attr)
		memcpy(pbuf + 3, pdata_attr, attr_len);

	a_len = attr_len + 3;

	return a_len;
}

static uint rtw_p2p_attr_remove(u8 *ie, uint ielen_ori, u8 attr_id)
{
	u8 *target_attr;
	u32 target_attr_len;
	uint ielen = ielen_ori;

	while (1) {
		target_attr = rtw_get_p2p_attr(ie, ielen, attr_id, NULL, &target_attr_len);
		if (target_attr && target_attr_len) {
			u8 *next_attr = target_attr+target_attr_len;
			uint remain_len = ielen-(next_attr-ie);

			_rtw_memset(target_attr, 0, target_attr_len);
			memcpy(target_attr, next_attr, remain_len);
			_rtw_memset(target_attr+remain_len, 0, target_attr_len);
			*(ie+1) -= target_attr_len;
			ielen -= target_attr_len;
		} else {
			break;
		}
	}
	return ielen;
}

void rtw_wlan_bssid_ex_remove_p2p_attr(struct wlan_bssid_ex *bss_ex, u8 attr_id)
{
	u8 *p2p_ie;
	uint p2p_ielen, p2p_ielen_ori;

	p2p_ie = rtw_get_p2p_ie(bss_ex->IEs+_FIXED_IE_LENGTH_, bss_ex->IELength-_FIXED_IE_LENGTH_, NULL, &p2p_ielen_ori);
	if (p2p_ie) {
		p2p_ielen = rtw_p2p_attr_remove(p2p_ie, p2p_ielen_ori, attr_id);
		if (p2p_ielen != p2p_ielen_ori) {
			u8 *next_ie_ori = p2p_ie+p2p_ielen_ori;
			u8 *next_ie = p2p_ie+p2p_ielen;
			uint remain_len = bss_ex->IELength-(next_ie_ori-bss_ex->IEs);

			memcpy(next_ie, next_ie_ori, remain_len);
			_rtw_memset(next_ie+remain_len, 0, p2p_ielen_ori-p2p_ielen);
			bss_ex->IELength -= p2p_ielen_ori-p2p_ielen;
		}
	}
}

#endif /* CONFIG_88EU_P2P */

/* Baron adds to avoid FreeBSD warning */
int ieee80211_is_empty_essid(const char *essid, int essid_len)
{
	/* Single white space is for Linksys APs */
	if (essid_len == 1 && essid[0] == ' ')
		return 1;

	/* Otherwise, if the entire essid is 0, we assume it is hidden */
	while (essid_len) {
		essid_len--;
		if (essid[essid_len] != '\0')
			return 0;
	}

	return 1;
}

int ieee80211_get_hdrlen(u16 fc)
{
	int hdrlen = 24;

	switch (WLAN_FC_GET_TYPE(fc)) {
	case RTW_IEEE80211_FTYPE_DATA:
		if (fc & RTW_IEEE80211_STYPE_QOS_DATA)
			hdrlen += 2;
		if ((fc & RTW_IEEE80211_FCTL_FROMDS) && (fc & RTW_IEEE80211_FCTL_TODS))
			hdrlen += 6; /* Addr4 */
		break;
	case RTW_IEEE80211_FTYPE_CTL:
		switch (WLAN_FC_GET_STYPE(fc)) {
		case RTW_IEEE80211_STYPE_CTS:
		case RTW_IEEE80211_STYPE_ACK:
			hdrlen = 10;
			break;
		default:
			hdrlen = 16;
			break;
		}
		break;
	}

	return hdrlen;
}

static int rtw_get_cipher_info(struct wlan_network *pnetwork)
{
	u32 wpa_ielen;
	unsigned char *pbuf;
	int group_cipher = 0, pairwise_cipher = 0, is8021x = 0;
	int ret = _FAIL;
	pbuf = rtw_get_wpa_ie(&pnetwork->network.IEs[12], &wpa_ielen, pnetwork->network.IELength-12);

	if (pbuf && (wpa_ielen > 0)) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_get_cipher_info: wpa_ielen: %d", wpa_ielen));
		if (_SUCCESS == rtw_parse_wpa_ie(pbuf, wpa_ielen+2, &group_cipher, &pairwise_cipher, &is8021x)) {
			pnetwork->BcnInfo.pairwise_cipher = pairwise_cipher;
			pnetwork->BcnInfo.group_cipher = group_cipher;
			pnetwork->BcnInfo.is_8021x = is8021x;
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("%s: pnetwork->pairwise_cipher: %d, is_8021x is %d",
				 __func__, pnetwork->BcnInfo.pairwise_cipher, pnetwork->BcnInfo.is_8021x));
			ret = _SUCCESS;
		}
	} else {
		pbuf = rtw_get_wpa2_ie(&pnetwork->network.IEs[12], &wpa_ielen, pnetwork->network.IELength-12);

		if (pbuf && (wpa_ielen > 0)) {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("get RSN IE\n"));
			if (_SUCCESS == rtw_parse_wpa2_ie(pbuf, wpa_ielen+2, &group_cipher, &pairwise_cipher, &is8021x)) {
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("get RSN IE  OK!!!\n"));
				pnetwork->BcnInfo.pairwise_cipher = pairwise_cipher;
				pnetwork->BcnInfo.group_cipher = group_cipher;
				pnetwork->BcnInfo.is_8021x = is8021x;
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("%s: pnetwork->pairwise_cipher: %d,"
							"pnetwork->group_cipher is %d, is_8021x is %d",	__func__, pnetwork->BcnInfo.pairwise_cipher,
							pnetwork->BcnInfo.group_cipher, pnetwork->BcnInfo.is_8021x));
				ret = _SUCCESS;
			}
		}
	}

	return ret;
}

void rtw_get_bcn_info(struct wlan_network *pnetwork)
{
	unsigned short cap = 0;
	u8 bencrypt = 0;
	__le16 le_tmp;
	u16 wpa_len = 0, rsn_len = 0;
	struct HT_info_element *pht_info = NULL;
	struct rtw_ieee80211_ht_cap *pht_cap = NULL;
	unsigned int		len;
	unsigned char		*p;

	memcpy(&le_tmp, rtw_get_capability_from_ie(pnetwork->network.IEs), 2);
	cap = le16_to_cpu(le_tmp);
	if (cap & WLAN_CAPABILITY_PRIVACY) {
		bencrypt = 1;
		pnetwork->network.Privacy = 1;
	} else {
		pnetwork->BcnInfo.encryp_protocol = ENCRYP_PROTOCOL_OPENSYS;
	}
	rtw_get_sec_ie(pnetwork->network.IEs , pnetwork->network.IELength, NULL, &rsn_len, NULL, &wpa_len);
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_get_bcn_info: ssid =%s\n", pnetwork->network.Ssid.Ssid));
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_get_bcn_info: wpa_len =%d rsn_len =%d\n", wpa_len, rsn_len));
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_get_bcn_info: ssid =%s\n", pnetwork->network.Ssid.Ssid));
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_get_bcn_info: wpa_len =%d rsn_len =%d\n", wpa_len, rsn_len));

	if (rsn_len > 0) {
		pnetwork->BcnInfo.encryp_protocol = ENCRYP_PROTOCOL_WPA2;
	} else if (wpa_len > 0) {
		pnetwork->BcnInfo.encryp_protocol = ENCRYP_PROTOCOL_WPA;
	} else {
		if (bencrypt)
			pnetwork->BcnInfo.encryp_protocol = ENCRYP_PROTOCOL_WEP;
	}
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_get_bcn_info: pnetwork->encryp_protocol is %x\n",
		 pnetwork->BcnInfo.encryp_protocol));
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_get_bcn_info: pnetwork->encryp_protocol is %x\n",
		 pnetwork->BcnInfo.encryp_protocol));
	rtw_get_cipher_info(pnetwork);

	/* get bwmode and ch_offset */
	/* parsing HT_CAP_IE */
	p = rtw_get_ie(pnetwork->network.IEs + _FIXED_IE_LENGTH_, _HT_CAPABILITY_IE_, &len, pnetwork->network.IELength - _FIXED_IE_LENGTH_);
	if (p && len > 0) {
			pht_cap = (struct rtw_ieee80211_ht_cap *)(p + 2);
			pnetwork->BcnInfo.ht_cap_info = pht_cap->cap_info;
	} else {
			pnetwork->BcnInfo.ht_cap_info = 0;
	}
	/* parsing HT_INFO_IE */
	p = rtw_get_ie(pnetwork->network.IEs + _FIXED_IE_LENGTH_, _HT_ADD_INFO_IE_, &len, pnetwork->network.IELength - _FIXED_IE_LENGTH_);
	if (p && len > 0) {
			pht_info = (struct HT_info_element *)(p + 2);
			pnetwork->BcnInfo.ht_info_infos_0 = pht_info->infos[0];
	} else {
			pnetwork->BcnInfo.ht_info_infos_0 = 0;
	}
}

/* show MCS rate, unit: 100Kbps */
u16 rtw_mcs_rate(u8 rf_type, u8 bw_40MHz, u8 short_GI_20, u8 short_GI_40, unsigned char *MCS_rate)
{
	u16 max_rate = 0;

	if (rf_type == RF_1T1R) {
		if (MCS_rate[0] & BIT(7))
			max_rate = (bw_40MHz) ? ((short_GI_40) ? 1500 : 1350) : ((short_GI_20) ? 722 : 650);
		else if (MCS_rate[0] & BIT(6))
			max_rate = (bw_40MHz) ? ((short_GI_40) ? 1350 : 1215) : ((short_GI_20) ? 650 : 585);
		else if (MCS_rate[0] & BIT(5))
			max_rate = (bw_40MHz) ? ((short_GI_40) ? 1200 : 1080) : ((short_GI_20) ? 578 : 520);
		else if (MCS_rate[0] & BIT(4))
			max_rate = (bw_40MHz) ? ((short_GI_40) ? 900 : 810) : ((short_GI_20) ? 433 : 390);
		else if (MCS_rate[0] & BIT(3))
			max_rate = (bw_40MHz) ? ((short_GI_40) ? 600 : 540) : ((short_GI_20) ? 289 : 260);
		else if (MCS_rate[0] & BIT(2))
			max_rate = (bw_40MHz) ? ((short_GI_40) ? 450 : 405) : ((short_GI_20) ? 217 : 195);
		else if (MCS_rate[0] & BIT(1))
			max_rate = (bw_40MHz) ? ((short_GI_40) ? 300 : 270) : ((short_GI_20) ? 144 : 130);
		else if (MCS_rate[0] & BIT(0))
			max_rate = (bw_40MHz) ? ((short_GI_40) ? 150 : 135) : ((short_GI_20) ? 72 : 65);
	} else {
		if (MCS_rate[1]) {
			if (MCS_rate[1] & BIT(7))
				max_rate = (bw_40MHz) ? ((short_GI_40) ? 3000 : 2700) : ((short_GI_20) ? 1444 : 1300);
			else if (MCS_rate[1] & BIT(6))
				max_rate = (bw_40MHz) ? ((short_GI_40) ? 2700 : 2430) : ((short_GI_20) ? 1300 : 1170);
			else if (MCS_rate[1] & BIT(5))
				max_rate = (bw_40MHz) ? ((short_GI_40) ? 2400 : 2160) : ((short_GI_20) ? 1156 : 1040);
			else if (MCS_rate[1] & BIT(4))
				max_rate = (bw_40MHz) ? ((short_GI_40) ? 1800 : 1620) : ((short_GI_20) ? 867 : 780);
			else if (MCS_rate[1] & BIT(3))
				max_rate = (bw_40MHz) ? ((short_GI_40) ? 1200 : 1080) : ((short_GI_20) ? 578 : 520);
			else if (MCS_rate[1] & BIT(2))
				max_rate = (bw_40MHz) ? ((short_GI_40) ? 900 : 810) : ((short_GI_20) ? 433 : 390);
			else if (MCS_rate[1] & BIT(1))
				max_rate = (bw_40MHz) ? ((short_GI_40) ? 600 : 540) : ((short_GI_20) ? 289 : 260);
			else if (MCS_rate[1] & BIT(0))
				max_rate = (bw_40MHz) ? ((short_GI_40) ? 300 : 270) : ((short_GI_20) ? 144 : 130);
		} else {
			if (MCS_rate[0] & BIT(7))
				max_rate = (bw_40MHz) ? ((short_GI_40) ? 1500 : 1350) : ((short_GI_20) ? 722 : 650);
			else if (MCS_rate[0] & BIT(6))
				max_rate = (bw_40MHz) ? ((short_GI_40) ? 1350 : 1215) : ((short_GI_20) ? 650 : 585);
			else if (MCS_rate[0] & BIT(5))
				max_rate = (bw_40MHz) ? ((short_GI_40) ? 1200 : 1080) : ((short_GI_20) ? 578 : 520);
			else if (MCS_rate[0] & BIT(4))
				max_rate = (bw_40MHz) ? ((short_GI_40) ? 900 : 810) : ((short_GI_20) ? 433 : 390);
			else if (MCS_rate[0] & BIT(3))
				max_rate = (bw_40MHz) ? ((short_GI_40) ? 600 : 540) : ((short_GI_20) ? 289 : 260);
			else if (MCS_rate[0] & BIT(2))
				max_rate = (bw_40MHz) ? ((short_GI_40) ? 450 : 405) : ((short_GI_20) ? 217 : 195);
			else if (MCS_rate[0] & BIT(1))
				max_rate = (bw_40MHz) ? ((short_GI_40) ? 300 : 270) : ((short_GI_20) ? 144 : 130);
			else if (MCS_rate[0] & BIT(0))
				max_rate = (bw_40MHz) ? ((short_GI_40) ? 150 : 135) : ((short_GI_20) ? 72 : 65);
		}
	}
	return max_rate;
}

int rtw_action_frame_parse(const u8 *frame, u32 frame_len, u8 *category, u8 *action)
{
	const u8 *frame_body = frame + sizeof(struct rtw_ieee80211_hdr_3addr);
	u16 fc;
	u8 c, a = 0;

	fc = le16_to_cpu(((struct rtw_ieee80211_hdr_3addr *)frame)->frame_ctl);

	if ((fc & (RTW_IEEE80211_FCTL_FTYPE|RTW_IEEE80211_FCTL_STYPE)) !=
	    (RTW_IEEE80211_FTYPE_MGMT|RTW_IEEE80211_STYPE_ACTION))
		return false;

	c = frame_body[0];

	switch (c) {
	case RTW_WLAN_CATEGORY_P2P: /* vendor-specific */
		break;
	default:
		a = frame_body[1];
	}

	if (category)
		*category = c;
	if (action)
		*action = a;

	return true;
}

static const char *_action_public_str[] = {
	"ACT_PUB_BSSCOEXIST",
	"ACT_PUB_DSE_ENABLE",
	"ACT_PUB_DSE_DEENABLE",
	"ACT_PUB_DSE_REG_LOCATION",
	"ACT_PUB_EXT_CHL_SWITCH",
	"ACT_PUB_DSE_MSR_REQ",
	"ACT_PUB_DSE_MSR_RPRT",
	"ACT_PUB_MP",
	"ACT_PUB_DSE_PWR_CONSTRAINT",
	"ACT_PUB_VENDOR",
	"ACT_PUB_GAS_INITIAL_REQ",
	"ACT_PUB_GAS_INITIAL_RSP",
	"ACT_PUB_GAS_COMEBACK_REQ",
	"ACT_PUB_GAS_COMEBACK_RSP",
	"ACT_PUB_TDLS_DISCOVERY_RSP",
	"ACT_PUB_LOCATION_TRACK",
	"ACT_PUB_RSVD",
};

const char *action_public_str(u8 action)
{
	action = (action >= ACT_PUBLIC_MAX) ? ACT_PUBLIC_MAX : action;
	return _action_public_str[action];
}
