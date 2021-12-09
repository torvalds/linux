// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _IEEE80211_C

#include "../include/drv_types.h"
#include "../include/ieee80211.h"
#include "../include/wifi.h"
#include "../include/osdep_service.h"
#include "../include/wlan_bssdef.h"
#include "../include/usb_osintf.h"

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
		return WIRELESS_INVALID;
	} else {  /*  could be pure B, pure G, or B/G */
		if (rtw_is_cckratesonly_included(rate))
			return WIRELESS_11B;
		else if (rtw_is_cckrates_included(rate))
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

	*pbuf = (u8)index;

	*(pbuf + 1) = (u8)len;

	if (len > 0)
		memcpy((void *)(pbuf + 2), (void *)source, len);

	*frlen = *frlen + (len + 2);

	return pbuf + len + 2;
}

/*----------------------------------------------------------------------------
index: the information element id index, limit is the limit for search
-----------------------------------------------------------------------------*/
u8 *rtw_get_ie(u8 *pbuf, int index, int *len, int limit)
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

void rtw_set_supported_rate(u8 *SupportedRates, uint mode)
{

	memset(SupportedRates, 0, NDIS_802_11_LENGTH_RATES_EX);

	switch (mode) {
	case WIRELESS_11B:
		memcpy(SupportedRates, WIFI_CCKRATES, IEEE80211_CCK_RATE_LEN);
		break;
	case WIRELESS_11G:
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

}

uint	rtw_get_rateset_len(u8	*rateset)
{
	uint i = 0;

	while (1) {
		if ((rateset[i]) == 0)
			break;
		if (i > 12)
			break;
		i++;
	}

	return i;
}

int rtw_generate_ie(struct registry_priv *pregistrypriv)
{
	u8	wireless_mode;
	int	sz = 0, rateLen;
	struct wlan_bssid_ex *pdev_network = &pregistrypriv->dev_network;
	u8 *ie = pdev_network->IEs;

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
	wireless_mode = pregistrypriv->wireless_mode;

	rtw_set_supported_rate(pdev_network->SupportedRates, wireless_mode);

	rateLen = rtw_get_rateset_len(pdev_network->SupportedRates);

	if (rateLen > 8) {
		ie = rtw_set_ie(ie, _SUPPORTEDRATES_IE_, 8, pdev_network->SupportedRates, &sz);
		/* ie = rtw_set_ie(ie, _EXT_SUPPORTEDRATES_IE_, (rateLen - 8), (pdev_network->SupportedRates + 8), &sz); */
	} else {
		ie = rtw_set_ie(ie, _SUPPORTEDRATES_IE_, rateLen, pdev_network->SupportedRates, &sz);
	}

	/* DS parameter set */
	ie = rtw_set_ie(ie, _DSSET_IE_, 1, (u8 *)&pdev_network->Configuration.DSConfig, &sz);

	/* IBSS Parameter Set */

	ie = rtw_set_ie(ie, _IBSS_PARA_IE_, 2, (u8 *)&pdev_network->Configuration.ATIMWindow, &sz);

	if (rateLen > 8)
		ie = rtw_set_ie(ie, _EXT_SUPPORTEDRATES_IE_, (rateLen - 8), (pdev_network->SupportedRates + 8), &sz);

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
			if (memcmp((pbuf + 2), wpa_oui_type, sizeof(wpa_oui_type)))
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
	if (!memcmp(s, WPA_CIPHER_SUITE_NONE, WPA_SELECTOR_LEN))
		return WPA_CIPHER_NONE;
	if (!memcmp(s, WPA_CIPHER_SUITE_WEP40, WPA_SELECTOR_LEN))
		return WPA_CIPHER_WEP40;
	if (!memcmp(s, WPA_CIPHER_SUITE_TKIP, WPA_SELECTOR_LEN))
		return WPA_CIPHER_TKIP;
	if (!memcmp(s, WPA_CIPHER_SUITE_CCMP, WPA_SELECTOR_LEN))
		return WPA_CIPHER_CCMP;
	if (!memcmp(s, WPA_CIPHER_SUITE_WEP104, WPA_SELECTOR_LEN))
		return WPA_CIPHER_WEP104;

	return 0;
}

int rtw_get_wpa2_cipher_suite(u8 *s)
{
	if (!memcmp(s, RSN_CIPHER_SUITE_NONE, RSN_SELECTOR_LEN))
		return WPA_CIPHER_NONE;
	if (!memcmp(s, RSN_CIPHER_SUITE_WEP40, RSN_SELECTOR_LEN))
		return WPA_CIPHER_WEP40;
	if (!memcmp(s, RSN_CIPHER_SUITE_TKIP, RSN_SELECTOR_LEN))
		return WPA_CIPHER_TKIP;
	if (!memcmp(s, RSN_CIPHER_SUITE_CCMP, RSN_SELECTOR_LEN))
		return WPA_CIPHER_CCMP;
	if (!memcmp(s, RSN_CIPHER_SUITE_WEP104, RSN_SELECTOR_LEN))
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

	if ((*wpa_ie != _WPA_IE_ID_) || (*(wpa_ie + 1) != (u8)(wpa_ie_len - 2)) ||
	    (memcmp(wpa_ie + 2, RTW_WPA_OUI_TYPE, WPA_SELECTOR_LEN)))
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
		return _FAIL;
	}

	/* pairwise_cipher */
	if (left >= 2) {
		count = get_unaligned_le16(pos);
		pos += 2;
		left -= 2;

		if (count == 0 || left < count * WPA_SELECTOR_LEN)
			return _FAIL;

		for (i = 0; i < count; i++) {
			*pairwise_cipher |= rtw_get_wpa_cipher_suite(pos);

			pos += WPA_SELECTOR_LEN;
			left -= WPA_SELECTOR_LEN;
		}
	} else if (left == 1) {
		return _FAIL;
	}

	if (is_8021x) {
		if (left >= 6) {
			pos += 2;
			if (!memcmp(pos, SUITE_1X, 4))
				*is_8021x = 1;
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

	if ((*rsn_ie != _WPA2_IE_ID_) || (*(rsn_ie + 1) != (u8)(rsn_ie_len - 2)))
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
		return _FAIL;
	}

	/* pairwise_cipher */
	if (left >= 2) {
		count = get_unaligned_le16(pos);
		pos += 2;
		left -= 2;

		if (count == 0 || left < count * RSN_SELECTOR_LEN)
			return _FAIL;

		for (i = 0; i < count; i++) {
			*pairwise_cipher |= rtw_get_wpa2_cipher_suite(pos);

			pos += RSN_SELECTOR_LEN;
			left -= RSN_SELECTOR_LEN;
		}

	} else if (left == 1) {
		return _FAIL;
	}

	if (is_8021x) {
		if (left >= 6) {
			pos += 2;
			if (!memcmp(pos, SUITE_1X, 4))
				*is_8021x = 1;
		}
	}
	return ret;
}

int rtw_get_sec_ie(u8 *in_ie, uint in_len, u8 *rsn_ie, u16 *rsn_len, u8 *wpa_ie, u16 *wpa_len)
{
	u8 authmode;
	u8 wpa_oui[4] = {0x0, 0x50, 0xf2, 0x01};
	uint	cnt;

	/* Search required WPA or WPA2 IE and copy to sec_ie[] */

	cnt = (_TIMESTAMP_ + _BEACON_ITERVAL_ + _CAPABILITY_);

	while (cnt < in_len) {
		authmode = in_ie[cnt];

		if ((authmode == _WPA_IE_ID_) && (!memcmp(&in_ie[cnt + 2], &wpa_oui[0], 4))) {
				if (wpa_ie)
					memcpy(wpa_ie, &in_ie[cnt], in_ie[cnt + 1] + 2);

				*wpa_len = in_ie[cnt + 1] + 2;
				cnt += in_ie[cnt + 1] + 2;  /* get next */
		} else {
			if (authmode == _WPA2_IE_ID_) {
				if (rsn_ie)
					memcpy(rsn_ie, &in_ie[cnt], in_ie[cnt + 1] + 2);

				*rsn_len = in_ie[cnt + 1] + 2;
				cnt += in_ie[cnt + 1] + 2;  /* get next */
			} else {
				cnt += in_ie[cnt + 1] + 2;   /* get next */
			}
		}
	}

	return *rsn_len + *wpa_len;
}

u8 rtw_is_wps_ie(u8 *ie_ptr, uint *wps_ielen)
{
	u8 match = false;
	u8 eid, wps_oui[4] = {0x0, 0x50, 0xf2, 0x04};

	if (!ie_ptr)
		return match;

	eid = ie_ptr[0];

	if ((eid == _WPA_IE_ID_) && (!memcmp(&ie_ptr[2], wps_oui, 4))) {
		*wps_ielen = ie_ptr[1] + 2;
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

		if ((eid == _WPA_IE_ID_) && (!memcmp(&in_ie[cnt + 2], wps_oui, 4))) {
			wpsie_ptr = &in_ie[cnt];

			if (wps_ie)
				memcpy(wps_ie, &in_ie[cnt], in_ie[cnt + 1] + 2);

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
 * rtw_get_wps_attr - Search a specific WPS attribute from a given WPS IE
 * @wps_ie: Address of WPS IE to search
 * @wps_ielen: Length limit from wps_ie
 * @target_attr_id: The attribute ID of WPS attribute to search
 * @buf_attr: If not NULL and the WPS attribute is found, WPS attribute will be copied to the buf starting from buf_attr
 * @len_attr: If not NULL and the WPS attribute is found, will set to the length of the entire WPS attribute
 *
 * Returns: the address of the specific WPS attribute found, or NULL
 */
u8 *rtw_get_wps_attr(u8 *wps_ie, uint wps_ielen, u16 target_attr_id, u8 *buf_attr, u32 *len_attr)
{
	u8 *attr_ptr = NULL;
	u8 *target_attr_ptr = NULL;
	u8 wps_oui[4] = {0x00, 0x50, 0xF2, 0x04};

	if (len_attr)
		*len_attr = 0;

	if ((wps_ie[0] != _VENDOR_SPECIFIC_IE_) ||
	    (memcmp(wps_ie + 2, wps_oui, 4)))
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
u8 *rtw_get_wps_attr_content(u8 *wps_ie, uint wps_ielen, u16 target_attr_id, u8 *buf_content, uint *len_content)
{
	u8 *attr_ptr;
	u32 attr_len;

	if (len_content)
		*len_content = 0;

	attr_ptr = rtw_get_wps_attr(wps_ie, wps_ielen, target_attr_id, NULL, &attr_len);

	if (attr_ptr && attr_len) {
		if (buf_content)
			memcpy(buf_content, attr_ptr + 4, attr_len - 4);

		if (len_content)
			*len_content = attr_len - 4;

		return attr_ptr + 4;
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
				(unsigned long)elen);
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
					(unsigned long)elen);
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
					pos[4], (unsigned long)elen);
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
				pos[3], (unsigned long)elen);
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
				pos[3], (unsigned long)elen);
			return -1;
		}
		break;
	default:
		DBG_88E("unknown vendor specific information element ignored (vendor OUI %02x:%02x:%02x len=%lu)\n",
			pos[0], pos[1], pos[2], (unsigned long)elen);
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

	memset(elems, 0, sizeof(*elems));

	while (left >= 2) {
		u8 id, elen;

		id = *pos++;
		elen = *pos++;
		left -= 2;

		if (elen > left) {
			if (show_errors) {
				DBG_88E("IEEE 802.11 element parse failed (id=%d elen=%d left=%lu)\n",
					id, elen, (unsigned long)left);
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

	if (!mac_addr)
		return;

	if (rtw_initmac && mac_pton(rtw_initmac, mac)) {
		/* Users specify the mac address */
		ether_addr_copy(mac_addr, mac);
	} else {
		/* Use the mac address stored in the Efuse */
		ether_addr_copy(mac, mac_addr);
	}

	if (is_broadcast_ether_addr(mac) || is_zero_ether_addr(mac)) {
		eth_random_addr(mac_addr);
		DBG_88E("MAC Address from efuse error, assign random one !!!\n");
	}

	DBG_88E("rtw_macaddr_cfg MAC Address  = %pM\n", mac_addr);
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

	if (p2p_ielen)
		*p2p_ielen = 0;

	while (cnt < in_len) {
		eid = in_ie[cnt];
		if ((in_len < 0) || (cnt > MAX_IE_SZ)) {
			dump_stack();
			return NULL;
		}
		if ((eid == _VENDOR_SPECIFIC_IE_) && !memcmp(&in_ie[cnt + 2], p2p_oui, 4)) {
			p2p_ie_ptr = in_ie + cnt;

			if (p2p_ie)
				memcpy(p2p_ie, &in_ie[cnt], in_ie[cnt + 1] + 2);
			if (p2p_ielen)
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
u8 *rtw_get_p2p_attr(u8 *p2p_ie, uint p2p_ielen, u8 target_attr_id, u8 *buf_attr, u32 *len_attr)
{
	u8 *attr_ptr = NULL;
	u8 *target_attr_ptr = NULL;
	u8 p2p_oui[4] = {0x50, 0x6F, 0x9A, 0x09};

	if (len_attr)
		*len_attr = 0;

	if (!p2p_ie || (p2p_ie[0] != _VENDOR_SPECIFIC_IE_) ||
	    memcmp(p2p_ie + 2, p2p_oui, 4))
		return attr_ptr;

	/*  6 = 1(Element ID) + 1(Length) + 3 (OUI) + 1(OUI Type) */
	attr_ptr = p2p_ie + 6; /* goto first attr */

	while (attr_ptr - p2p_ie < p2p_ielen) {
		/*  3 = 1(Attribute ID) + 2(Length) */
		u8 attr_id = *attr_ptr;
		u16 attr_data_len = get_unaligned_le16(attr_ptr + 1);
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
u8 *rtw_get_p2p_attr_content(u8 *p2p_ie, uint p2p_ielen, u8 target_attr_id, u8 *buf_content, uint *len_content)
{
	u8 *attr_ptr;
	u32 attr_len;

	if (len_content)
		*len_content = 0;

	attr_ptr = rtw_get_p2p_attr(p2p_ie, p2p_ielen, target_attr_id, NULL, &attr_len);

	if (attr_ptr && attr_len) {
		if (buf_content)
			memcpy(buf_content, attr_ptr + 3, attr_len - 3);

		if (len_content)
			*len_content = attr_len - 3;

		return attr_ptr + 3;
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
			u8 *next_attr = target_attr + target_attr_len;
			uint remain_len = ielen - (next_attr - ie);

			memset(target_attr, 0, target_attr_len);
			memcpy(target_attr, next_attr, remain_len);
			memset(target_attr + remain_len, 0, target_attr_len);
			*(ie + 1) -= target_attr_len;
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

	p2p_ie = rtw_get_p2p_ie(bss_ex->IEs + _FIXED_IE_LENGTH_, bss_ex->IELength - _FIXED_IE_LENGTH_, NULL, &p2p_ielen_ori);
	if (p2p_ie) {
		p2p_ielen = rtw_p2p_attr_remove(p2p_ie, p2p_ielen_ori, attr_id);
		if (p2p_ielen != p2p_ielen_ori) {
			u8 *next_ie_ori = p2p_ie + p2p_ielen_ori;
			u8 *next_ie = p2p_ie + p2p_ielen;
			uint remain_len = bss_ex->IELength - (next_ie_ori - bss_ex->IEs);

			memcpy(next_ie, next_ie_ori, remain_len);
			memset(next_ie + remain_len, 0, p2p_ielen_ori - p2p_ielen);
			bss_ex->IELength -= p2p_ielen_ori - p2p_ielen;
		}
	}
}

static int rtw_get_cipher_info(struct wlan_network *pnetwork)
{
	u32 wpa_ielen;
	unsigned char *pbuf;
	int group_cipher = 0, pairwise_cipher = 0, is8021x = 0;
	int ret = _FAIL;
	pbuf = rtw_get_wpa_ie(&pnetwork->network.IEs[12], &wpa_ielen, pnetwork->network.IELength - 12);

	if (pbuf && (wpa_ielen > 0)) {
		if (_SUCCESS == rtw_parse_wpa_ie(pbuf, wpa_ielen + 2, &group_cipher, &pairwise_cipher, &is8021x)) {
			pnetwork->BcnInfo.pairwise_cipher = pairwise_cipher;
			pnetwork->BcnInfo.group_cipher = group_cipher;
			pnetwork->BcnInfo.is_8021x = is8021x;
			ret = _SUCCESS;
		}
	} else {
		pbuf = rtw_get_wpa2_ie(&pnetwork->network.IEs[12], &wpa_ielen, pnetwork->network.IELength - 12);

		if (pbuf && (wpa_ielen > 0)) {
			if (_SUCCESS == rtw_parse_wpa2_ie(pbuf, wpa_ielen + 2, &group_cipher, &pairwise_cipher, &is8021x)) {
				pnetwork->BcnInfo.pairwise_cipher = pairwise_cipher;
				pnetwork->BcnInfo.group_cipher = group_cipher;
				pnetwork->BcnInfo.is_8021x = is8021x;
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
	struct ieee80211_ht_cap *pht_cap = NULL;
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
	rtw_get_sec_ie(pnetwork->network.IEs, pnetwork->network.IELength, NULL, &rsn_len, NULL, &wpa_len);

	if (rsn_len > 0) {
		pnetwork->BcnInfo.encryp_protocol = ENCRYP_PROTOCOL_WPA2;
	} else if (wpa_len > 0) {
		pnetwork->BcnInfo.encryp_protocol = ENCRYP_PROTOCOL_WPA;
	} else {
		if (bencrypt)
			pnetwork->BcnInfo.encryp_protocol = ENCRYP_PROTOCOL_WEP;
	}
	rtw_get_cipher_info(pnetwork);

	/* get bwmode and ch_offset */
	/* parsing HT_CAP_IE */
	p = rtw_get_ie(pnetwork->network.IEs + _FIXED_IE_LENGTH_, _HT_CAPABILITY_IE_, &len, pnetwork->network.IELength - _FIXED_IE_LENGTH_);
	if (p && len > 0) {
			pht_cap = (struct ieee80211_ht_cap *)(p + 2);
			pnetwork->BcnInfo.ht_cap_info = le16_to_cpu(pht_cap->cap_info);
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
