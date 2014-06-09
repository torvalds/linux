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
#define _IEEE80211_C

#include <drv_types.h>
#include <linux/ieee80211.h>
#include <ieee80211.h>
#include <wifi.h>
#include <osdep_service.h>
#include <wlan_bssdef.h>

u8 RTW_WPA_OUI23A_TYPE[] = { 0x00, 0x50, 0xf2, 1 };
u16 RTW_WPA_VERSION23A = 1;
u8 WPA_AUTH_KEY_MGMT_NONE23A[] = { 0x00, 0x50, 0xf2, 0 };
u8 WPA_AUTH_KEY_MGMT_UNSPEC_802_1X23A[] = { 0x00, 0x50, 0xf2, 1 };
u8 WPA_AUTH_KEY_MGMT_PSK_OVER_802_1X23A[] = { 0x00, 0x50, 0xf2, 2 };
u8 WPA_CIPHER_SUITE_NONE23A[] = { 0x00, 0x50, 0xf2, 0 };
u8 WPA_CIPHER_SUITE_WEP4023A[] = { 0x00, 0x50, 0xf2, 1 };
u8 WPA_CIPHER_SUITE_TKIP23A[] = { 0x00, 0x50, 0xf2, 2 };
u8 WPA_CIPHER_SUITE_WRAP23A[] = { 0x00, 0x50, 0xf2, 3 };
u8 WPA_CIPHER_SUITE_CCMP23A[] = { 0x00, 0x50, 0xf2, 4 };
u8 WPA_CIPHER_SUITE_WEP10423A[] = { 0x00, 0x50, 0xf2, 5 };

u16 RSN_VERSION_BSD23A = 1;
u8 RSN_AUTH_KEY_MGMT_UNSPEC_802_1X23A[] = { 0x00, 0x0f, 0xac, 1 };
u8 RSN_AUTH_KEY_MGMT_PSK_OVER_802_1X23A[] = { 0x00, 0x0f, 0xac, 2 };
u8 RSN_CIPHER_SUITE_NONE23A[] = { 0x00, 0x0f, 0xac, 0 };
u8 RSN_CIPHER_SUITE_WEP4023A[] = { 0x00, 0x0f, 0xac, 1 };
u8 RSN_CIPHER_SUITE_TKIP23A[] = { 0x00, 0x0f, 0xac, 2 };
u8 RSN_CIPHER_SUITE_WRAP23A[] = { 0x00, 0x0f, 0xac, 3 };
u8 RSN_CIPHER_SUITE_CCMP23A[] = { 0x00, 0x0f, 0xac, 4 };
u8 RSN_CIPHER_SUITE_WEP10423A[] = { 0x00, 0x0f, 0xac, 5 };
/*  */
/*  for adhoc-master to generate ie and provide supported-rate to fw */
/*  */

static u8 WIFI_CCKRATES[] = {
	IEEE80211_CCK_RATE_1MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_CCK_RATE_2MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_CCK_RATE_5MB | IEEE80211_BASIC_RATE_MASK,
	IEEE80211_CCK_RATE_11MB | IEEE80211_BASIC_RATE_MASK
};

static u8 WIFI_OFDMRATES[] = {
	IEEE80211_OFDM_RATE_6MB,
	IEEE80211_OFDM_RATE_9MB,
	IEEE80211_OFDM_RATE_12MB,
	IEEE80211_OFDM_RATE_18MB,
	IEEE80211_OFDM_RATE_24MB,
	IEEE80211_OFDM_RATE_36MB,
	IEEE80211_OFDM_RATE_48MB,
	IEEE80211_OFDM_RATE_54MB
};

int rtw_get_bit_value_from_ieee_value23a(u8 val)
{
	unsigned char dot11_rate_table[]=
		{2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108, 0};

	int i = 0;
	while (dot11_rate_table[i] != 0) {
		if (dot11_rate_table[i] == val)
			return BIT(i);
		i++;
	}
	return 0;
}

static bool rtw_is_cckrates_included(u8 *rate)
{
	u32 i = 0;

	while (rate[i]) {
		if ((rate[i] & 0x7f) == 2 || (rate[i] & 0x7f) == 4 ||
		    (rate[i] & 0x7f) == 11 || (rate[i] & 0x7f) == 22)
			return true;
		i++;
	}

	return false;
}

static bool rtw_is_cckratesonly_included(u8 *rate)
{
	u32 i = 0;

	while (rate[i]) {
		if ((rate[i] & 0x7f) != 2 && (rate[i] & 0x7f) != 4 &&
		    (rate[i] & 0x7f) != 11 && (rate[i] & 0x7f) != 22)
			return false;

		i++;
	}

	return true;
}

int rtw_check_network_type23a(unsigned char *rate, int ratelen, int channel)
{
	if (channel > 14) {
		if (rtw_is_cckrates_included(rate))
			return WIRELESS_INVALID;
		else
			return WIRELESS_11A;
	} else {  /*  could be pure B, pure G, or B/G */
		if (rtw_is_cckratesonly_included(rate))
			return WIRELESS_11B;
		else if (rtw_is_cckrates_included(rate))
			return	WIRELESS_11BG;
		else
			return WIRELESS_11G;
	}
}

u8 *rtw_set_fixed_ie23a(unsigned char *pbuf, unsigned int len,
		     unsigned char *source, unsigned int *frlen)
{
	memcpy((void *)pbuf, (void *)source, len);
	*frlen = *frlen + len;
	return pbuf + len;
}

/*  rtw_set_ie23a will update frame length */
u8 *rtw_set_ie23a(u8 *pbuf, int index, uint len, const u8 *source, uint *frlen)
{

	*pbuf = (u8)index;

	*(pbuf + 1) = (u8)len;

	if (len > 0)
		memcpy((void *)(pbuf + 2), (void *)source, len);

	*frlen = *frlen + (len + 2);

	return pbuf + len + 2;
}

inline u8 *rtw_set_ie23a_ch_switch (u8 *buf, u32 *buf_len, u8 ch_switch_mode,
				u8 new_ch, u8 ch_switch_cnt)
{
	u8 ie_data[3];

	ie_data[0] = ch_switch_mode;
	ie_data[1] = new_ch;
	ie_data[2] = ch_switch_cnt;
	return rtw_set_ie23a(buf, WLAN_EID_CHANNEL_SWITCH,  3, ie_data, buf_len);
}

inline u8 hal_ch_offset_to_secondary_ch_offset23a(u8 ch_offset)
{
	if (ch_offset == HAL_PRIME_CHNL_OFFSET_DONT_CARE)
		return SCN;
	else if (ch_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
		return SCB;
	else if (ch_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
		return SCA;

	return SCN;
}

inline u8 *rtw_set_ie23a_secondary_ch_offset(u8 *buf, u32 *buf_len,
					  u8 secondary_ch_offset)
{
	return rtw_set_ie23a(buf, WLAN_EID_SECONDARY_CHANNEL_OFFSET,
			  1, &secondary_ch_offset, buf_len);
}

/*----------------------------------------------------------------------------
index: the information element id index, limit is the limit for search
-----------------------------------------------------------------------------*/
u8 *rtw_get_ie23a(u8 *pbuf, int index, int *len, int limit)
{
	int tmp, i;
	u8 *p;

	if (limit < 1) {

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

	return NULL;
}

/**
 * rtw_get_ie23a_ex - Search specific IE from a series of IEs
 * @in_ie: Address of IEs to search
 * @in_len: Length limit from in_ie
 * @eid: Element ID to match
 * @oui: OUI to match
 * @oui_len: OUI length
 * @ie: If not NULL and the specific IE is found, the IE will be copied
 *      to the buf starting from the specific IE
 * @ielen: If not NULL and the specific IE is found, will set to the length
 *         of the entire IE
 *
 * Returns: The address of the specific IE found, or NULL
 */
u8 *rtw_get_ie23a_ex(u8 *in_ie, uint in_len, u8 eid, u8 *oui, u8 oui_len,
		  u8 *ie, uint *ielen)
{
	uint cnt;
	u8 *target_ie = NULL;

	if (ielen)
		*ielen = 0;

	if (!in_ie || in_len <= 0)
		return target_ie;

	cnt = 0;

	while (cnt < in_len) {
		if (eid == in_ie[cnt] &&
		    (!oui || !memcmp(&in_ie[cnt+2], oui, oui_len))) {
			target_ie = &in_ie[cnt];

			if (ie)
				memcpy(ie, &in_ie[cnt], in_ie[cnt+1]+2);

			if (ielen)
				*ielen = in_ie[cnt+1]+2;
			break;
		} else {
			cnt += in_ie[cnt + 1] + 2; /* goto next */
		}
	}

	return target_ie;
}

/**
 * rtw_ies_remove_ie23a - Find matching IEs and remove
 * @ies: Address of IEs to search
 * @ies_len: Pointer of length of ies, will update to new length
 * @offset: The offset to start scarch
 * @eid: Element ID to match
 * @oui: OUI to match
 * @oui_len: OUI length
 *
 * Returns: _SUCCESS: ies is updated, _FAIL: not updated
 */
int rtw_ies_remove_ie23a(u8 *ies, uint *ies_len, uint offset, u8 eid,
		      u8 *oui, u8 oui_len)
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
		target_ie = rtw_get_ie23a_ex(start, search_len, eid, oui, oui_len,
					  NULL, &target_ielen);
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

void rtw_set_supported_rate23a(u8* SupportedRates, uint mode)
{


	memset(SupportedRates, 0, NDIS_802_11_LENGTH_RATES_EX);

	switch (mode)
	{
	case WIRELESS_11B:
		memcpy(SupportedRates, WIFI_CCKRATES, IEEE80211_CCK_RATE_LEN);
		break;

	case WIRELESS_11G:
	case WIRELESS_11A:
	case WIRELESS_11_5N:
	case WIRELESS_11A_5N:/* Todo: no basic rate for ofdm ? */
		memcpy(SupportedRates, WIFI_OFDMRATES,
		       IEEE80211_NUM_OFDM_RATESLEN);
		break;

	case WIRELESS_11BG:
	case WIRELESS_11G_24N:
	case WIRELESS_11_24N:
	case WIRELESS_11BG_24N:
		memcpy(SupportedRates, WIFI_CCKRATES, IEEE80211_CCK_RATE_LEN);
		memcpy(SupportedRates + IEEE80211_CCK_RATE_LEN, WIFI_OFDMRATES,
		       IEEE80211_NUM_OFDM_RATESLEN);
		break;
	}

}

uint rtw_get_rateset_len23a(u8 *rateset)
{
	uint i = 0;

	while(1) {
		if (rateset[i] == 0)
			break;

		if (i > 12)
			break;

		i++;
	}

	return i;
}

int rtw_generate_ie23a(struct registry_priv *pregistrypriv)
{
	u8	wireless_mode;
	int	sz = 0, rateLen;
	struct wlan_bssid_ex*	pdev_network = &pregistrypriv->dev_network;
	u8*	ie = pdev_network->IEs;



	/* timestamp will be inserted by hardware */
	sz += 8;
	ie += sz;

	/* beacon interval : 2bytes */
	/* BCN_INTERVAL; */
	*(u16*)ie = cpu_to_le16(pdev_network->BeaconPeriod);
	sz += 2;
	ie += 2;

	/* capability info */
	*(u16*)ie = 0;

	*(u16*)ie |= cpu_to_le16(WLAN_CAPABILITY_IBSS);

	if (pregistrypriv->preamble == PREAMBLE_SHORT)
		*(u16*)ie |= cpu_to_le16(WLAN_CAPABILITY_SHORT_PREAMBLE);

	if (pdev_network->Privacy)
		*(u16*)ie |= cpu_to_le16(WLAN_CAPABILITY_PRIVACY);

	sz += 2;
	ie += 2;

	/* SSID */
	ie = rtw_set_ie23a(ie, WLAN_EID_SSID, pdev_network->Ssid.ssid_len,
			pdev_network->Ssid.ssid, &sz);

	/* supported rates */
	if (pregistrypriv->wireless_mode == WIRELESS_11ABGN) {
		if (pdev_network->DSConfig > 14)
			wireless_mode = WIRELESS_11A_5N;
		else
			wireless_mode = WIRELESS_11BG_24N;
	} else {
		wireless_mode = pregistrypriv->wireless_mode;
	}

	rtw_set_supported_rate23a(pdev_network->SupportedRates, wireless_mode) ;

	rateLen = rtw_get_rateset_len23a(pdev_network->SupportedRates);

	if (rateLen > 8) {
		ie = rtw_set_ie23a(ie, WLAN_EID_SUPP_RATES, 8,
				pdev_network->SupportedRates, &sz);
		/* ie = rtw_set_ie23a(ie, _EXT_SUPPORTEDRATES_IE_, (rateLen - 8), (pdev_network->SupportedRates + 8), &sz); */
	} else {
		ie = rtw_set_ie23a(ie, WLAN_EID_SUPP_RATES, rateLen,
				pdev_network->SupportedRates, &sz);
	}

	/* DS parameter set */
	ie = rtw_set_ie23a(ie, WLAN_EID_DS_PARAMS, 1,
			   (u8 *)&pdev_network->DSConfig, &sz);

	/* IBSS Parameter Set */

	ie = rtw_set_ie23a(ie, WLAN_EID_IBSS_PARAMS, 2,
			   (u8 *)&pdev_network->ATIMWindow, &sz);

	if (rateLen > 8) {
		ie = rtw_set_ie23a(ie, WLAN_EID_EXT_SUPP_RATES, (rateLen - 8),
				(pdev_network->SupportedRates + 8), &sz);
	}



	/* return _SUCCESS; */

	return sz;
}

int rtw_get_wpa_cipher_suite23a(const u8 *s)
{
	if (!memcmp(s, WPA_CIPHER_SUITE_NONE23A, WPA_SELECTOR_LEN))
		return WPA_CIPHER_NONE;
	if (!memcmp(s, WPA_CIPHER_SUITE_WEP4023A, WPA_SELECTOR_LEN))
		return WPA_CIPHER_WEP40;
	if (!memcmp(s, WPA_CIPHER_SUITE_TKIP23A, WPA_SELECTOR_LEN))
		return WPA_CIPHER_TKIP;
	if (!memcmp(s, WPA_CIPHER_SUITE_CCMP23A, WPA_SELECTOR_LEN))
		return WPA_CIPHER_CCMP;
	if (!memcmp(s, WPA_CIPHER_SUITE_WEP10423A, WPA_SELECTOR_LEN))
		return WPA_CIPHER_WEP104;

	return 0;
}

int rtw_get_wpa2_cipher_suite23a(const u8 *s)
{
	if (!memcmp(s, RSN_CIPHER_SUITE_NONE23A, RSN_SELECTOR_LEN))
		return WPA_CIPHER_NONE;
	if (!memcmp(s, RSN_CIPHER_SUITE_WEP4023A, RSN_SELECTOR_LEN))
		return WPA_CIPHER_WEP40;
	if (!memcmp(s, RSN_CIPHER_SUITE_TKIP23A, RSN_SELECTOR_LEN))
		return WPA_CIPHER_TKIP;
	if (!memcmp(s, RSN_CIPHER_SUITE_CCMP23A, RSN_SELECTOR_LEN))
		return WPA_CIPHER_CCMP;
	if (!memcmp(s, RSN_CIPHER_SUITE_WEP10423A, RSN_SELECTOR_LEN))
		return WPA_CIPHER_WEP104;

	return 0;
}

int rtw_parse_wpa_ie23a(const u8* wpa_ie, int wpa_ie_len, int *group_cipher, int *pairwise_cipher, int *is_8021x)
{
	int i, ret = _SUCCESS;
	int left, count;
	const u8 *pos;

	if (wpa_ie_len <= 0) {
		/* No WPA IE - fail silently */
		return _FAIL;
	}

	if (wpa_ie[1] != (u8)(wpa_ie_len - 2))
		return _FAIL;

	pos = wpa_ie;

	pos += 8;
	left = wpa_ie_len - 8;

	/* group_cipher */
	if (left >= WPA_SELECTOR_LEN) {

		*group_cipher = rtw_get_wpa_cipher_suite23a(pos);

		pos += WPA_SELECTOR_LEN;
		left -= WPA_SELECTOR_LEN;
	} else if (left > 0) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("%s: ie length mismatch, %u too much",
			  __func__, left));

		return _FAIL;
	}

	/* pairwise_cipher */
	if (left >= 2) {
                /* count = le16_to_cpu(*(u16*)pos); */
		count = get_unaligned_le16(pos);
		pos += 2;
		left -= 2;

		if (count == 0 || left < count * WPA_SELECTOR_LEN) {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
				 ("%s: ie count botch (pairwise), "
				  "count %u left %u", __func__,
				  count, left));
			return _FAIL;
		}

		for (i = 0; i < count; i++) {
			*pairwise_cipher |= rtw_get_wpa_cipher_suite23a(pos);

			pos += WPA_SELECTOR_LEN;
			left -= WPA_SELECTOR_LEN;
		}
	} else if (left == 1) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("%s: ie too short (for key mgmt)", __func__));
		return _FAIL;
	}

	if (is_8021x) {
		if (left >= 6) {
			pos += 2;
			if (!memcmp(pos, RTW_WPA_OUI23A_TYPE, 4)) {
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
					 ("%s : there has 802.1x auth\n",
					  __func__));
				*is_8021x = 1;
			}
		}
	}

	return ret;
}

int rtw_parse_wpa2_ie23a(const u8* rsn_ie, int rsn_ie_len, int *group_cipher,
		      int *pairwise_cipher, int *is_8021x)
{
	int i, ret = _SUCCESS;
	int left, count;
	const u8 *pos;
	u8 SUITE_1X[4] = {0x00, 0x0f, 0xac, 0x01};

	if (rsn_ie_len <= 0) {
		/* No RSN IE - fail silently */
		return _FAIL;
	}

	if (*rsn_ie != _WPA2_IE_ID_ || *(rsn_ie+1) != (u8)(rsn_ie_len - 2)) {
		return _FAIL;
	}

	pos = rsn_ie;
	pos += 4;
	left = rsn_ie_len - 4;

	/* group_cipher */
	if (left >= RSN_SELECTOR_LEN) {
		*group_cipher = rtw_get_wpa2_cipher_suite23a(pos);

		pos += RSN_SELECTOR_LEN;
		left -= RSN_SELECTOR_LEN;
	} else if (left > 0) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("%s: ie length mismatch, %u too much",
			  __func__, left));
		return _FAIL;
	}

	/* pairwise_cipher */
	if (left >= 2) {
	        /* count = le16_to_cpu(*(u16*)pos); */
		count = get_unaligned_le16(pos);
		pos += 2;
		left -= 2;

		if (count == 0 || left < count * RSN_SELECTOR_LEN) {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
				 ("%s: ie count botch (pairwise), "
				  "count %u left %u",
				  __func__, count, left));
			return _FAIL;
		}

		for (i = 0; i < count; i++) {
			*pairwise_cipher |= rtw_get_wpa2_cipher_suite23a(pos);

			pos += RSN_SELECTOR_LEN;
			left -= RSN_SELECTOR_LEN;
		}
	} else if (left == 1) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_err_,
			 ("%s: ie too short (for key mgmt)",  __func__));

		return _FAIL;
	}

	if (is_8021x) {
		if (left >= 6) {
			pos += 2;
			if (!memcmp(pos, SUITE_1X, 4)) {
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
					 ("%s (): there has 802.1x auth\n",
					  __func__));
				*is_8021x = 1;
			}
		}
	}

	return ret;
}

int rtw_get_sec_ie23a(u8 *in_ie, uint in_len, u8 *rsn_ie, u16 *rsn_len,
		   u8 *wpa_ie, u16 *wpa_len)
{
	u8 authmode, sec_idx, i;
	uint cnt;


	/* Search required WPA or WPA2 IE and copy to sec_ie[ ] */

	cnt = (_TIMESTAMP_ + _BEACON_ITERVAL_ + _CAPABILITY_);

	sec_idx = 0;

	while(cnt < in_len) {
		authmode = in_ie[cnt];

		if ((authmode == WLAN_EID_VENDOR_SPECIFIC) &&
		    !memcmp(&in_ie[cnt+2], RTW_WPA_OUI23A_TYPE, 4)) {
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
					 ("\n rtw_get_wpa_ie23a: sec_idx =%d "
					  "in_ie[cnt+1]+2 =%d\n",
					  sec_idx, in_ie[cnt + 1] + 2));

				if (wpa_ie) {
				memcpy(wpa_ie, &in_ie[cnt], in_ie[cnt+1]+2);

				for (i = 0; i < (in_ie[cnt + 1] + 2); i = i + 8) {
					RT_TRACE(_module_rtl871x_mlme_c_,
						 _drv_info_,
						 ("\n %2x,%2x,%2x,%2x,%2x,%2x,"
						  "%2x,%2x\n", wpa_ie[i],
						  wpa_ie[i + 1], wpa_ie[i + 2],
						  wpa_ie[i + 3], wpa_ie[i + 4],
						  wpa_ie[i + 5], wpa_ie[i + 6],
						  wpa_ie[i + 7]));
					}
				}

				*wpa_len = in_ie[cnt + 1] + 2;
				cnt += in_ie[cnt + 1] + 2;  /* get next */
		} else {
			if (authmode == _WPA2_IE_ID_) {
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
					 ("\n get_rsn_ie: sec_idx =%d in_ie"
					  "[cnt+1]+2 =%d\n", sec_idx,
					  in_ie[cnt + 1] + 2));

				if (rsn_ie) {
				memcpy(rsn_ie, &in_ie[cnt], in_ie[cnt + 1] + 2);

				for (i = 0; i < (in_ie[cnt + 1] + 2); i = i + 8) {
					RT_TRACE(_module_rtl871x_mlme_c_,
						 _drv_info_,
						 ("\n %2x,%2x,%2x,%2x,%2x,%2x,"
						  "%2x,%2x\n", rsn_ie[i],
						  rsn_ie[i + 1], rsn_ie[i + 2],
						  rsn_ie[i + 3], rsn_ie[i + 4],
						  rsn_ie[i + 5], rsn_ie[i + 6],
						  rsn_ie[i + 7]));
					}
				}

				*rsn_len = in_ie[cnt + 1] + 2;
				cnt += in_ie[cnt + 1] + 2;  /* get next */
			} else {
				cnt += in_ie[cnt + 1] + 2;   /* get next */
			}
		}
	}



	return *rsn_len + *wpa_len;
}

/**
 * rtw_get_wps_ie23a - Search WPS IE from a series of IEs
 * @in_ie: Address of IEs to search
 * @in_len: Length limit from in_ie
 * @wps_ielen: If not NULL and WPS IE is found, will set to the length of
 *             the entire WPS IE
 *
 * Returns: The address of the WPS IE found, or NULL
 */
u8 *rtw_get_wps_ie23a(u8 *in_ie, uint in_len, uint *wps_ielen)
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

		if (eid == WLAN_EID_VENDOR_SPECIFIC &&
		    !memcmp(&in_ie[cnt+2], wps_oui, 4)) {
			wpsie_ptr = &in_ie[cnt];

			if (wps_ielen)
				*wps_ielen = in_ie[cnt + 1] + 2;

			cnt += in_ie[cnt + 1] + 2;

			break;
		} else {
			cnt += in_ie[cnt + 1] + 2; /* goto next */
		}
	}

	return wpsie_ptr;
}

/**
 * rtw_get_wps_attr23a - Search a specific WPS attribute from a given WPS IE
 * @wps_ie: Address of WPS IE to search
 * @wps_ielen: Length limit from wps_ie
 * @target_attr_id: The attribute ID of WPS attribute to search
 * @buf_attr: If not NULL and the WPS attribute is found, WPS attribute
 *            will be copied to the buf starting from buf_attr
 * @len_attr: If not NULL and the WPS attribute is found, will set to the
 *            length of the entire WPS attribute
 *
 * Returns: the address of the specific WPS attribute found, or NULL
 */
const u8 *rtw_get_wps_attr23a(const u8 *wps_ie, uint wps_ielen,
			      u16 target_attr_id, u8 *buf_attr, u32 *len_attr)
{
	const u8 *attr_ptr = NULL;
	const u8 *target_attr_ptr = NULL;
	u8 wps_oui[4] = {0x00, 0x50, 0xF2, 0x04};

	if (len_attr)
		*len_attr = 0;

	if (wps_ie[0] != WLAN_EID_VENDOR_SPECIFIC ||
	    memcmp(wps_ie + 2, wps_oui, 4)) {
		return attr_ptr;
	}

	/*  6 = 1(Element ID) + 1(Length) + 4(WPS OUI) */
	attr_ptr = wps_ie + 6; /* goto first attr */

	while (attr_ptr - wps_ie < wps_ielen) {
		/*  4 = 2(Attribute ID) + 2(Length) */
		u16 attr_id = get_unaligned_be16(attr_ptr);
		u16 attr_data_len = get_unaligned_be16(attr_ptr + 2);
		u16 attr_len = attr_data_len + 4;

		/* DBG_8723A("%s attr_ptr:%p, id:%u, length:%u\n", __func__, attr_ptr, attr_id, attr_data_len); */
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
 * rtw_get_wps_attr_content23a - Search a specific WPS attribute content
 * from a given WPS IE
 * @wps_ie: Address of WPS IE to search
 * @wps_ielen: Length limit from wps_ie
 * @target_attr_id: The attribute ID of WPS attribute to search
 * @buf_content: If not NULL and the WPS attribute is found, WPS attribute
 *               content will be copied to the buf starting from buf_content
 * @len_content: If not NULL and the WPS attribute is found, will set to the
 *               length of the WPS attribute content
 *
 * Returns: the address of the specific WPS attribute content found, or NULL
 */
const u8 *rtw_get_wps_attr_content23a(const u8 *wps_ie, uint wps_ielen,
				      u16 target_attr_id, u8 *buf_content,
				      uint *len_content)
{
	const u8 *attr_ptr;
	u32 attr_len;

	if (len_content)
		*len_content = 0;

	attr_ptr = rtw_get_wps_attr23a(wps_ie, wps_ielen, target_attr_id,
				    NULL, &attr_len);

	if (attr_ptr && attr_len) {
		if (buf_content)
			memcpy(buf_content, attr_ptr + 4, attr_len - 4);

		if (len_content)
			*len_content = attr_len - 4;

		return attr_ptr + 4;
	}

	return NULL;
}

static int rtw_get_cipher_info(struct wlan_network *pnetwork)
{
	const u8 *pbuf;
	int group_cipher = 0, pairwise_cipher = 0, is8021x = 0;
	int ret = _FAIL;
	int r, offset, plen;
	char *pie;

	offset = offsetof(struct ieee80211_mgmt, u.beacon.variable) -
		offsetof(struct ieee80211_mgmt, u);
	pie = &pnetwork->network.IEs[offset];
	plen = pnetwork->network.IELength - offset;

	pbuf = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
				       WLAN_OUI_TYPE_MICROSOFT_WPA, pie, plen);

	if (pbuf && pbuf[1] > 0) {
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
			 ("rtw_get_cipher_info: wpa_ielen: %d", pbuf[1]));
		r = rtw_parse_wpa_ie23a(pbuf, pbuf[1] + 2, &group_cipher,
				     &pairwise_cipher, &is8021x);
		if (r == _SUCCESS) {
			pnetwork->BcnInfo.pairwise_cipher = pairwise_cipher;
			pnetwork->BcnInfo.group_cipher = group_cipher;
			pnetwork->BcnInfo.is_8021x = is8021x;
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
				 ("%s: pnetwork->pairwise_cipher: %d, is_"
				  "8021x is %d", __func__,
				  pnetwork->BcnInfo.pairwise_cipher,
				  pnetwork->BcnInfo.is_8021x));
			ret = _SUCCESS;
		}
	} else {
		pbuf = cfg80211_find_ie(WLAN_EID_RSN, pie, plen);

		if (pbuf && pbuf[1] > 0) {
			RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
				 ("get RSN IE\n"));
			r = rtw_parse_wpa2_ie23a(pbuf, pbuf[1] + 2,
					      &group_cipher, &pairwise_cipher,
					      &is8021x);
			if (r == _SUCCESS) {
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
					 ("get RSN IE  OK!!!\n"));
				pnetwork->BcnInfo.pairwise_cipher =
					pairwise_cipher;
				pnetwork->BcnInfo.group_cipher = group_cipher;
				pnetwork->BcnInfo.is_8021x = is8021x;
				RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
					 ("%s: pnetwork->pairwise_cipher: %d,"
					  "pnetwork->group_cipher is %d, "
					  "is_8021x is %d", __func__,
					  pnetwork->BcnInfo.pairwise_cipher,
					  pnetwork->BcnInfo.group_cipher,
					  pnetwork->BcnInfo.is_8021x));
				ret = _SUCCESS;
			}
		}
	}

	return ret;
}

void rtw_get_bcn_info23a(struct wlan_network *pnetwork)
{
	unsigned short cap;
	u8 bencrypt = 0;
	/* u8 wpa_ie[255], rsn_ie[255]; */
	u16 wpa_len = 0, rsn_len = 0;
	struct HT_info_element *pht_info;
	struct ieee80211_ht_cap *pht_cap;
	const u8 *p;

	cap = get_unaligned_le16(
		rtw_get_capability23a_from_ie(pnetwork->network.IEs));
	if (cap & WLAN_CAPABILITY_PRIVACY) {
		bencrypt = 1;
		pnetwork->network.Privacy = 1;
	} else
		pnetwork->BcnInfo.encryp_protocol = ENCRYP_PROTOCOL_OPENSYS;

	rtw_get_sec_ie23a(pnetwork->network.IEs, pnetwork->network.IELength,
		       NULL, &rsn_len, NULL, &wpa_len);
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("rtw_get_bcn_info23a: ssid =%s\n", pnetwork->network.Ssid.ssid));
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("rtw_get_bcn_info23a: wpa_len =%d rsn_len =%d\n",
		  wpa_len, rsn_len));
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("rtw_get_bcn_info23a: ssid =%s\n", pnetwork->network.Ssid.ssid));
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("rtw_get_bcn_info23a: wpa_len =%d rsn_len =%d\n",
		  wpa_len, rsn_len));

	if (rsn_len > 0)
		pnetwork->BcnInfo.encryp_protocol = ENCRYP_PROTOCOL_WPA2;
	else if (wpa_len > 0)
		pnetwork->BcnInfo.encryp_protocol = ENCRYP_PROTOCOL_WPA;
	else {
		if (bencrypt)
			pnetwork->BcnInfo.encryp_protocol = ENCRYP_PROTOCOL_WEP;
	}
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("rtw_get_bcn_info23a: pnetwork->encryp_protocol is %x\n",
		  pnetwork->BcnInfo.encryp_protocol));
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("rtw_get_bcn_info23a: pnetwork->encryp_protocol is %x\n",
		  pnetwork->BcnInfo.encryp_protocol));
	rtw_get_cipher_info(pnetwork);

	/* get bwmode and ch_offset */
	/* parsing HT_CAP_IE */
	p = cfg80211_find_ie(WLAN_EID_HT_CAPABILITY,
			     pnetwork->network.IEs + _FIXED_IE_LENGTH_,
			     pnetwork->network.IELength - _FIXED_IE_LENGTH_);
	if (p && p[1] > 0) {
		pht_cap = (struct ieee80211_ht_cap *)(p + 2);
		pnetwork->BcnInfo.ht_cap_info = pht_cap->cap_info;
	} else
		pnetwork->BcnInfo.ht_cap_info = 0;

	/* parsing HT_INFO_IE */
	p = cfg80211_find_ie(WLAN_EID_HT_OPERATION,
			     pnetwork->network.IEs + _FIXED_IE_LENGTH_,
		       pnetwork->network.IELength - _FIXED_IE_LENGTH_);
	if (p && p[1] > 0) {
		pht_info = (struct HT_info_element *)(p + 2);
		pnetwork->BcnInfo.ht_info_infos_0 = pht_info->infos[0];
	} else
		pnetwork->BcnInfo.ht_info_infos_0 = 0;
}

/* show MCS rate, unit: 100Kbps */
u16 rtw_mcs_rate23a(u8 rf_type, u8 bw_40MHz, u8 short_GI_20, u8 short_GI_40,
		 unsigned char * MCS_rate)
{
	u16 max_rate = 0;

	if (rf_type == RF_1T1R) {
		if (MCS_rate[0] & BIT(7))
			max_rate = (bw_40MHz) ? ((short_GI_40)?1500:1350):
				((short_GI_20)?722:650);
		else if (MCS_rate[0] & BIT(6))
			max_rate = (bw_40MHz) ? ((short_GI_40)?1350:1215):
				((short_GI_20)?650:585);
		else if (MCS_rate[0] & BIT(5))
			max_rate = (bw_40MHz) ? ((short_GI_40)?1200:1080):
				((short_GI_20)?578:520);
		else if (MCS_rate[0] & BIT(4))
			max_rate = (bw_40MHz) ? ((short_GI_40)?900:810):
				((short_GI_20)?433:390);
		else if (MCS_rate[0] & BIT(3))
			max_rate = (bw_40MHz) ? ((short_GI_40)?600:540):
				((short_GI_20)?289:260);
		else if (MCS_rate[0] & BIT(2))
			max_rate = (bw_40MHz) ? ((short_GI_40)?450:405):
				((short_GI_20)?217:195);
		else if (MCS_rate[0] & BIT(1))
			max_rate = (bw_40MHz) ? ((short_GI_40)?300:270):
				((short_GI_20)?144:130);
		else if (MCS_rate[0] & BIT(0))
			max_rate = (bw_40MHz) ? ((short_GI_40)?150:135):
				((short_GI_20)?72:65);
	} else {
		if (MCS_rate[1]) {
			if (MCS_rate[1] & BIT(7))
				max_rate = (bw_40MHz) ? ((short_GI_40)?3000:2700):((short_GI_20)?1444:1300);
			else if (MCS_rate[1] & BIT(6))
				max_rate = (bw_40MHz) ? ((short_GI_40)?2700:2430):((short_GI_20)?1300:1170);
			else if (MCS_rate[1] & BIT(5))
				max_rate = (bw_40MHz) ? ((short_GI_40)?2400:2160):((short_GI_20)?1156:1040);
			else if (MCS_rate[1] & BIT(4))
				max_rate = (bw_40MHz) ? ((short_GI_40)?1800:1620):((short_GI_20)?867:780);
			else if (MCS_rate[1] & BIT(3))
				max_rate = (bw_40MHz) ? ((short_GI_40)?1200:1080):((short_GI_20)?578:520);
			else if (MCS_rate[1] & BIT(2))
				max_rate = (bw_40MHz) ? ((short_GI_40)?900:810):((short_GI_20)?433:390);
			else if (MCS_rate[1] & BIT(1))
				max_rate = (bw_40MHz) ? ((short_GI_40)?600:540):((short_GI_20)?289:260);
			else if (MCS_rate[1] & BIT(0))
				max_rate = (bw_40MHz) ? ((short_GI_40)?300:270):((short_GI_20)?144:130);
		} else {
			if (MCS_rate[0] & BIT(7))
				max_rate = (bw_40MHz) ? ((short_GI_40)?1500:1350):((short_GI_20)?722:650);
			else if (MCS_rate[0] & BIT(6))
				max_rate = (bw_40MHz) ? ((short_GI_40)?1350:1215):((short_GI_20)?650:585);
			else if (MCS_rate[0] & BIT(5))
				max_rate = (bw_40MHz) ? ((short_GI_40)?1200:1080):((short_GI_20)?578:520);
			else if (MCS_rate[0] & BIT(4))
				max_rate = (bw_40MHz) ? ((short_GI_40)?900:810):((short_GI_20)?433:390);
			else if (MCS_rate[0] & BIT(3))
				max_rate = (bw_40MHz) ? ((short_GI_40)?600:540):((short_GI_20)?289:260);
			else if (MCS_rate[0] & BIT(2))
				max_rate = (bw_40MHz) ? ((short_GI_40)?450:405):((short_GI_20)?217:195);
			else if (MCS_rate[0] & BIT(1))
				max_rate = (bw_40MHz) ? ((short_GI_40)?300:270):((short_GI_20)?144:130);
			else if (MCS_rate[0] & BIT(0))
				max_rate = (bw_40MHz) ? ((short_GI_40)?150:135):((short_GI_20)?72:65);
		}
	}
	return max_rate;
}

static const char *_action_public_str23a[] = {
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

const char *action_public_str23a(u8 action)
{
	action = (action >= ACT_PUBLIC_MAX) ? ACT_PUBLIC_MAX : action;
	return _action_public_str23a[action];
}
