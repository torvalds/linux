/******************************************************************************
 * ieee80211.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>.
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define _IEEE80211_C

#include "drv_types.h"
#include "ieee80211.h"
#include "wifi.h"
#include "osdep_service.h"
#include "wlan_bssdef.h"

static const u8 WPA_OUI_TYPE[] = {0x00, 0x50, 0xf2, 1};
static const u8 WPA_CIPHER_SUITE_NONE[] = {0x00, 0x50, 0xf2, 0};
static const u8 WPA_CIPHER_SUITE_WEP40[] = {0x00, 0x50, 0xf2, 1};
static const u8 WPA_CIPHER_SUITE_TKIP[] = {0x00, 0x50, 0xf2, 2};
static const u8 WPA_CIPHER_SUITE_CCMP[] = {0x00, 0x50, 0xf2, 4};
static const u8 WPA_CIPHER_SUITE_WEP104[] = {0x00, 0x50, 0xf2, 5};

static const u8 RSN_CIPHER_SUITE_NONE[] = {0x00, 0x0f, 0xac, 0};
static const u8 RSN_CIPHER_SUITE_WEP40[] = {0x00, 0x0f, 0xac, 1};
static const u8 RSN_CIPHER_SUITE_TKIP[] = {0x00, 0x0f, 0xac, 2};
static const u8 RSN_CIPHER_SUITE_CCMP[] = {0x00, 0x0f, 0xac, 4};
static const u8 RSN_CIPHER_SUITE_WEP104[] = {0x00, 0x0f, 0xac, 5};

/*-----------------------------------------------------------
 * for adhoc-master to generate ie and provide supported-rate to fw
 *-----------------------------------------------------------
 */

static u8 WIFI_CCKRATES[] =  {
	(IEEE80211_CCK_RATE_1MB | IEEE80211_BASIC_RATE_MASK),
	(IEEE80211_CCK_RATE_2MB | IEEE80211_BASIC_RATE_MASK),
	(IEEE80211_CCK_RATE_5MB | IEEE80211_BASIC_RATE_MASK),
	(IEEE80211_CCK_RATE_11MB | IEEE80211_BASIC_RATE_MASK)
};

static u8 WIFI_OFDMRATES[] = {
	(IEEE80211_OFDM_RATE_6MB),
	(IEEE80211_OFDM_RATE_9MB),
	(IEEE80211_OFDM_RATE_12MB),
	(IEEE80211_OFDM_RATE_18MB),
	(IEEE80211_OFDM_RATE_24MB),
	(IEEE80211_OFDM_RATE_36MB),
	(IEEE80211_OFDM_RATE_48MB),
	(IEEE80211_OFDM_RATE_54MB)
};

uint r8712_is_cckrates_included(u8 *rate)
{
	u32 i = 0;

	while (rate[i] != 0) {
		if ((((rate[i]) & 0x7f) == 2) || (((rate[i]) & 0x7f) == 4) ||
		    (((rate[i]) & 0x7f) == 11) || (((rate[i]) & 0x7f) == 22))
			return true;
		i++;
	}
	return false;
}

uint r8712_is_cckratesonly_included(u8 *rate)
{
	u32 i = 0;

	while (rate[i] != 0) {
		if ((((rate[i]) & 0x7f) != 2) && (((rate[i]) & 0x7f) != 4) &&
		    (((rate[i]) & 0x7f) != 11)  && (((rate[i]) & 0x7f) != 22))
			return false;
		i++;
	}
	return true;
}

/* r8712_set_ie will update frame length */
u8 *r8712_set_ie(u8 *pbuf, sint index, uint len, u8 *source, uint *frlen)
{
	*pbuf = (u8)index;
	*(pbuf + 1) = (u8)len;
	if (len > 0)
		memcpy((void *)(pbuf + 2), (void *)source, len);
	*frlen = *frlen + (len + 2);
	return pbuf + len + 2;
}

/* ---------------------------------------------------------------------------
 * index: the information element id index, limit is the limit for search
 * ---------------------------------------------------------------------------
 */
u8 *r8712_get_ie(u8 *pbuf, sint index, uint *len, sint limit)
{
	sint tmp, i;
	u8 *p;

	if (limit < 1)
		return NULL;
	p = pbuf;
	i = 0;
	*len = 0;
	while (1) {
		if (*p == index) {
			*len = *(p + 1);
			return p;
		}
		tmp = *(p + 1);
		p += (tmp + 2);
		i += (tmp + 2);
		if (i >= limit)
			break;
	}
	return NULL;
}

static void set_supported_rate(u8 *rates, uint mode)
{
	memset(rates, 0, NDIS_802_11_LENGTH_RATES_EX);
	switch (mode) {
	case WIRELESS_11B:
		memcpy(rates, WIFI_CCKRATES, IEEE80211_CCK_RATE_LEN);
		break;
	case WIRELESS_11G:
	case WIRELESS_11A:
		memcpy(rates, WIFI_OFDMRATES, IEEE80211_NUM_OFDM_RATESLEN);
		break;
	case WIRELESS_11BG:
		memcpy(rates, WIFI_CCKRATES, IEEE80211_CCK_RATE_LEN);
		memcpy(rates + IEEE80211_CCK_RATE_LEN, WIFI_OFDMRATES,
		       IEEE80211_NUM_OFDM_RATESLEN);
		break;
	}
}

static uint r8712_get_rateset_len(u8 *rateset)
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

int r8712_generate_ie(struct registry_priv *pregistrypriv)
{
	int rate_len;
	uint sz = 0;
	struct wlan_bssid_ex *pdev_network = &pregistrypriv->dev_network;
	u8 *ie = pdev_network->IEs;
	u16 beaconPeriod = (u16)pdev_network->Configuration.BeaconPeriod;

	/*timestamp will be inserted by hardware*/
	sz += 8;
	ie += sz;
	/*beacon interval : 2bytes*/
	*(__le16 *)ie = cpu_to_le16(beaconPeriod);
	sz += 2;
	ie += 2;
	/*capability info*/
	*(u16 *)ie = 0;
	*(__le16 *)ie |= cpu_to_le16(cap_IBSS);
	if (pregistrypriv->preamble == PREAMBLE_SHORT)
		*(__le16 *)ie |= cpu_to_le16(cap_ShortPremble);
	if (pdev_network->Privacy)
		*(__le16 *)ie |= cpu_to_le16(cap_Privacy);
	sz += 2;
	ie += 2;
	/*SSID*/
	ie = r8712_set_ie(ie, _SSID_IE_, pdev_network->Ssid.SsidLength,
			  pdev_network->Ssid.Ssid, &sz);
	/*supported rates*/
	set_supported_rate(pdev_network->rates, pregistrypriv->wireless_mode);
	rate_len = r8712_get_rateset_len(pdev_network->rates);
	if (rate_len > 8) {
		ie = r8712_set_ie(ie, _SUPPORTEDRATES_IE_, 8,
				  pdev_network->rates, &sz);
		ie = r8712_set_ie(ie, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8),
				  (pdev_network->rates + 8), &sz);
	} else {
		ie = r8712_set_ie(ie, _SUPPORTEDRATES_IE_,
				  rate_len, pdev_network->rates, &sz);
	}
	/*DS parameter set*/
	ie = r8712_set_ie(ie, _DSSET_IE_, 1,
			  (u8 *)&pdev_network->Configuration.DSConfig, &sz);
	/*IBSS Parameter Set*/
	ie = r8712_set_ie(ie, _IBSS_PARA_IE_, 2,
			  (u8 *)&pdev_network->Configuration.ATIMWindow, &sz);
	return sz;
}

unsigned char *r8712_get_wpa_ie(unsigned char *pie, uint *wpa_ie_len, int limit)
{
	u32 len;
	u16 val16;
	unsigned char wpa_oui_type[] = {0x00, 0x50, 0xf2, 0x01};
	u8 *pbuf = pie;

	while (1) {
		pbuf = r8712_get_ie(pbuf, _WPA_IE_ID_, &len, limit);
		if (pbuf) {
			/*check if oui matches...*/
			if (memcmp((pbuf + 2), wpa_oui_type,
				   sizeof(wpa_oui_type)))
				goto check_next_ie;
			/*check version...*/
			memcpy((u8 *)&val16, (pbuf + 6), sizeof(val16));
			le16_to_cpus(&val16);
			if (val16 != 0x0001)
				goto check_next_ie;
			*wpa_ie_len = *(pbuf + 1);
			return pbuf;
		}
		*wpa_ie_len = 0;
		return NULL;
check_next_ie:
		limit = limit - (pbuf - pie) - 2 - len;
		if (limit <= 0)
			break;
		pbuf += (2 + len);
	}
	*wpa_ie_len = 0;
	return NULL;
}

unsigned char *r8712_get_wpa2_ie(unsigned char *pie, uint *rsn_ie_len, int limit)
{
	return r8712_get_ie(pie, _WPA2_IE_ID_, rsn_ie_len, limit);
}

static int r8712_get_wpa_cipher_suite(u8 *s)
{
	if (!memcmp(s, (void *)WPA_CIPHER_SUITE_NONE, WPA_SELECTOR_LEN))
		return WPA_CIPHER_NONE;
	if (!memcmp(s, (void *)WPA_CIPHER_SUITE_WEP40, WPA_SELECTOR_LEN))
		return WPA_CIPHER_WEP40;
	if (!memcmp(s, (void *)WPA_CIPHER_SUITE_TKIP, WPA_SELECTOR_LEN))
		return WPA_CIPHER_TKIP;
	if (!memcmp(s, (void *)WPA_CIPHER_SUITE_CCMP, WPA_SELECTOR_LEN))
		return WPA_CIPHER_CCMP;
	if (!memcmp(s, (void *)WPA_CIPHER_SUITE_WEP104, WPA_SELECTOR_LEN))
		return WPA_CIPHER_WEP104;
	return 0;
}

static int r8712_get_wpa2_cipher_suite(u8 *s)
{
	if (!memcmp(s, (void *)RSN_CIPHER_SUITE_NONE, RSN_SELECTOR_LEN))
		return WPA_CIPHER_NONE;
	if (!memcmp(s, (void *)RSN_CIPHER_SUITE_WEP40, RSN_SELECTOR_LEN))
		return WPA_CIPHER_WEP40;
	if (!memcmp(s, (void *)RSN_CIPHER_SUITE_TKIP, RSN_SELECTOR_LEN))
		return WPA_CIPHER_TKIP;
	if (!memcmp(s, (void *)RSN_CIPHER_SUITE_CCMP, RSN_SELECTOR_LEN))
		return WPA_CIPHER_CCMP;
	if (!memcmp(s, (void *)RSN_CIPHER_SUITE_WEP104, RSN_SELECTOR_LEN))
		return WPA_CIPHER_WEP104;
	return 0;
}

int r8712_parse_wpa_ie(u8 *wpa_ie, int wpa_ie_len, int *group_cipher,
		       int *pairwise_cipher)
{
	int i;
	int left, count;
	u8 *pos;

	if (wpa_ie_len <= 0) {
		/* No WPA IE - fail silently */
		return _FAIL;
	}
	if ((*wpa_ie != _WPA_IE_ID_) ||
	    (*(wpa_ie + 1) != (u8)(wpa_ie_len - 2)) ||
	    (memcmp(wpa_ie + 2, (void *)WPA_OUI_TYPE, WPA_SELECTOR_LEN)))
		return _FAIL;
	pos = wpa_ie;
	pos += 8;
	left = wpa_ie_len - 8;
	/*group_cipher*/
	if (left >= WPA_SELECTOR_LEN) {
		*group_cipher = r8712_get_wpa_cipher_suite(pos);
		pos += WPA_SELECTOR_LEN;
		left -= WPA_SELECTOR_LEN;
	} else if (left > 0) {
		return _FAIL;
	}
	/*pairwise_cipher*/
	if (left >= 2) {
		count = le16_to_cpu(*(__le16 *)pos);
		pos += 2;
		left -= 2;
		if (count == 0 || left < count * WPA_SELECTOR_LEN)
			return _FAIL;
		for (i = 0; i < count; i++) {
			*pairwise_cipher |= r8712_get_wpa_cipher_suite(pos);
			pos += WPA_SELECTOR_LEN;
			left -= WPA_SELECTOR_LEN;
		}
	} else if (left == 1) {
		return _FAIL;
	}
	return _SUCCESS;
}

int r8712_parse_wpa2_ie(u8 *rsn_ie, int rsn_ie_len, int *group_cipher,
			int *pairwise_cipher)
{
	int i;
	int left, count;
	u8 *pos;

	if (rsn_ie_len <= 0) {
		/* No RSN IE - fail silently */
		return _FAIL;
	}
	if ((*rsn_ie != _WPA2_IE_ID_) ||
	    (*(rsn_ie + 1) != (u8)(rsn_ie_len - 2)))
		return _FAIL;
	pos = rsn_ie;
	pos += 4;
	left = rsn_ie_len - 4;
	/*group_cipher*/
	if (left >= RSN_SELECTOR_LEN) {
		*group_cipher = r8712_get_wpa2_cipher_suite(pos);
		pos += RSN_SELECTOR_LEN;
		left -= RSN_SELECTOR_LEN;
	} else if (left > 0) {
		return _FAIL;
	}
	/*pairwise_cipher*/
	if (left >= 2) {
		count = le16_to_cpu(*(__le16 *)pos);
		pos += 2;
		left -= 2;
		if (count == 0 || left < count * RSN_SELECTOR_LEN)
			return _FAIL;
		for (i = 0; i < count; i++) {
			*pairwise_cipher |= r8712_get_wpa2_cipher_suite(pos);
			pos += RSN_SELECTOR_LEN;
			left -= RSN_SELECTOR_LEN;
		}
	} else if (left == 1) {
		return _FAIL;
	}
	return _SUCCESS;
}

int r8712_get_sec_ie(u8 *in_ie, uint in_len, u8 *rsn_ie, u16 *rsn_len,
		     u8 *wpa_ie, u16 *wpa_len)
{
	u8 authmode;
	u8 wpa_oui[4] = {0x0, 0x50, 0xf2, 0x01};
	uint cnt;

	/*Search required WPA or WPA2 IE and copy to sec_ie[ ]*/
	cnt = _TIMESTAMP_ + _BEACON_ITERVAL_ + _CAPABILITY_;
	while (cnt < in_len) {
		authmode = in_ie[cnt];
		if ((authmode == _WPA_IE_ID_) &&
		    (!memcmp(&in_ie[cnt + 2], &wpa_oui[0], 4))) {
			memcpy(wpa_ie, &in_ie[cnt], in_ie[cnt + 1] + 2);
			*wpa_len = in_ie[cnt + 1] + 2;
			cnt += in_ie[cnt + 1] + 2;  /*get next */
		} else {
			if (authmode == _WPA2_IE_ID_) {
				memcpy(rsn_ie, &in_ie[cnt],
				       in_ie[cnt + 1] + 2);
				*rsn_len = in_ie[cnt + 1] + 2;
				cnt += in_ie[cnt + 1] + 2;  /*get next*/
			} else {
				cnt += in_ie[cnt + 1] + 2;   /*get next*/
			}
		}
	}
	return *rsn_len + *wpa_len;
}

int r8712_get_wps_ie(u8 *in_ie, uint in_len, u8 *wps_ie, uint *wps_ielen)
{
	int match;
	uint cnt;
	u8 eid, wps_oui[4] = {0x0, 0x50, 0xf2, 0x04};

	cnt = 12;
	match = false;
	while (cnt < in_len) {
		eid = in_ie[cnt];
		if ((eid == _WPA_IE_ID_) &&
		    (!memcmp(&in_ie[cnt + 2], wps_oui, 4))) {
			memcpy(wps_ie, &in_ie[cnt], in_ie[cnt + 1] + 2);
			*wps_ielen = in_ie[cnt + 1] + 2;
			cnt += in_ie[cnt + 1] + 2;
			match = true;
			break;
		}
			cnt += in_ie[cnt + 1] + 2; /* goto next */
	}
	return match;
}
