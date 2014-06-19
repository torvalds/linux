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
	if (ch_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
		return IEEE80211_HT_PARAM_CHA_SEC_BELOW;
	else if (ch_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
		return IEEE80211_HT_PARAM_CHA_SEC_ABOVE;

	return IEEE80211_HT_PARAM_CHA_SEC_NONE;
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
	*(u16*)ie = cpu_to_le16(pdev_network->beacon_interval);
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

static int rtw_get_wpa_cipher_suite(const u8 *s)
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

static int rtw_get_wpa2_cipher_suite(const u8 *s)
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

		*group_cipher = rtw_get_wpa_cipher_suite(pos);

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
			*pairwise_cipher |= rtw_get_wpa_cipher_suite(pos);

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
		*group_cipher = rtw_get_wpa2_cipher_suite(pos);

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
			*pairwise_cipher |= rtw_get_wpa2_cipher_suite(pos);

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
	u8 bencrypt = 0;
	int pie_len, ie_offset;
	u8 *pie;
	const u8 *p;

	if (pnetwork->network.capability & WLAN_CAPABILITY_PRIVACY) {
		bencrypt = 1;
		pnetwork->network.Privacy = 1;
	} else
		pnetwork->BcnInfo.encryp_protocol = ENCRYP_PROTOCOL_OPENSYS;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("%s: ssid =%s\n", __func__, pnetwork->network.Ssid.ssid));

	ie_offset = offsetof(struct ieee80211_mgmt, u.beacon.variable) -
		offsetof(struct ieee80211_mgmt, u);
	pie = pnetwork->network.IEs + ie_offset;
	pie_len = pnetwork->network.IELength - ie_offset;

	p = cfg80211_find_ie(WLAN_EID_RSN, pie, pie_len);
	if (p && p[1]) {
		pnetwork->BcnInfo.encryp_protocol = ENCRYP_PROTOCOL_WPA2;
	} else if (cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
					   WLAN_OUI_TYPE_MICROSOFT_WPA,
					   pie, pie_len)) {
		pnetwork->BcnInfo.encryp_protocol = ENCRYP_PROTOCOL_WPA;
	} else {
		if (bencrypt)
			pnetwork->BcnInfo.encryp_protocol = ENCRYP_PROTOCOL_WEP;
	}
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("%s: pnetwork->encryp_protocol is %x\n", __func__,
		  pnetwork->BcnInfo.encryp_protocol));
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_,
		 ("%s: pnetwork->encryp_protocol is %x\n", __func__,
		  pnetwork->BcnInfo.encryp_protocol));
	rtw_get_cipher_info(pnetwork);

	/* get bwmode and ch_offset */
}

/* show MCS rate, unit: 100Kbps */
u16 rtw_mcs_rate23a(u8 rf_type, u8 bw_40MHz, u8 short_GI_20, u8 short_GI_40,
		    struct ieee80211_mcs_info *mcs)
{
	u16 max_rate = 0;

	if (rf_type == RF_1T1R) {
		if (mcs->rx_mask[0] & BIT(7))
			max_rate = (bw_40MHz) ? ((short_GI_40)?1500:1350):
				((short_GI_20)?722:650);
		else if (mcs->rx_mask[0] & BIT(6))
			max_rate = (bw_40MHz) ? ((short_GI_40)?1350:1215):
				((short_GI_20)?650:585);
		else if (mcs->rx_mask[0] & BIT(5))
			max_rate = (bw_40MHz) ? ((short_GI_40)?1200:1080):
				((short_GI_20)?578:520);
		else if (mcs->rx_mask[0] & BIT(4))
			max_rate = (bw_40MHz) ? ((short_GI_40)?900:810):
				((short_GI_20)?433:390);
		else if (mcs->rx_mask[0] & BIT(3))
			max_rate = (bw_40MHz) ? ((short_GI_40)?600:540):
				((short_GI_20)?289:260);
		else if (mcs->rx_mask[0] & BIT(2))
			max_rate = (bw_40MHz) ? ((short_GI_40)?450:405):
				((short_GI_20)?217:195);
		else if (mcs->rx_mask[0] & BIT(1))
			max_rate = (bw_40MHz) ? ((short_GI_40)?300:270):
				((short_GI_20)?144:130);
		else if (mcs->rx_mask[0] & BIT(0))
			max_rate = (bw_40MHz) ? ((short_GI_40)?150:135):
				((short_GI_20)?72:65);
	} else {
		if (mcs->rx_mask[1]) {
			if (mcs->rx_mask[1] & BIT(7))
				max_rate = (bw_40MHz) ? ((short_GI_40)?3000:2700):((short_GI_20)?1444:1300);
			else if (mcs->rx_mask[1] & BIT(6))
				max_rate = (bw_40MHz) ? ((short_GI_40)?2700:2430):((short_GI_20)?1300:1170);
			else if (mcs->rx_mask[1] & BIT(5))
				max_rate = (bw_40MHz) ? ((short_GI_40)?2400:2160):((short_GI_20)?1156:1040);
			else if (mcs->rx_mask[1] & BIT(4))
				max_rate = (bw_40MHz) ? ((short_GI_40)?1800:1620):((short_GI_20)?867:780);
			else if (mcs->rx_mask[1] & BIT(3))
				max_rate = (bw_40MHz) ? ((short_GI_40)?1200:1080):((short_GI_20)?578:520);
			else if (mcs->rx_mask[1] & BIT(2))
				max_rate = (bw_40MHz) ? ((short_GI_40)?900:810):((short_GI_20)?433:390);
			else if (mcs->rx_mask[1] & BIT(1))
				max_rate = (bw_40MHz) ? ((short_GI_40)?600:540):((short_GI_20)?289:260);
			else if (mcs->rx_mask[1] & BIT(0))
				max_rate = (bw_40MHz) ? ((short_GI_40)?300:270):((short_GI_20)?144:130);
		} else {
			if (mcs->rx_mask[0] & BIT(7))
				max_rate = (bw_40MHz) ? ((short_GI_40)?1500:1350):((short_GI_20)?722:650);
			else if (mcs->rx_mask[0] & BIT(6))
				max_rate = (bw_40MHz) ? ((short_GI_40)?1350:1215):((short_GI_20)?650:585);
			else if (mcs->rx_mask[0] & BIT(5))
				max_rate = (bw_40MHz) ? ((short_GI_40)?1200:1080):((short_GI_20)?578:520);
			else if (mcs->rx_mask[0] & BIT(4))
				max_rate = (bw_40MHz) ? ((short_GI_40)?900:810):((short_GI_20)?433:390);
			else if (mcs->rx_mask[0] & BIT(3))
				max_rate = (bw_40MHz) ? ((short_GI_40)?600:540):((short_GI_20)?289:260);
			else if (mcs->rx_mask[0] & BIT(2))
				max_rate = (bw_40MHz) ? ((short_GI_40)?450:405):((short_GI_20)?217:195);
			else if (mcs->rx_mask[0] & BIT(1))
				max_rate = (bw_40MHz) ? ((short_GI_40)?300:270):((short_GI_20)?144:130);
			else if (mcs->rx_mask[0] & BIT(0))
				max_rate = (bw_40MHz) ? ((short_GI_40)?150:135):((short_GI_20)?72:65);
		}
	}
	return max_rate;
}
