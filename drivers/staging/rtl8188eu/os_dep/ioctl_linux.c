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
#define _IOCTL_LINUX_C_

#include <linux/ieee80211.h>

#include <osdep_service.h>
#include <drv_types.h>
#include <wlan_bssdef.h>
#include <rtw_debug.h>
#include <wifi.h>
#include <rtw_mlme.h>
#include <rtw_mlme_ext.h>
#include <rtw_ioctl.h>
#include <rtw_ioctl_set.h>
#include <rtl8188e_hal.h>

#include <rtw_iol.h>
#include <linux/vmalloc.h>
#include <linux/etherdevice.h>

#include "osdep_intf.h"

#define RTL_IOCTL_WPA_SUPPLICANT	(SIOCIWFIRSTPRIV + 30)

#define SCAN_ITEM_SIZE 768
#define MAX_CUSTOM_LEN 64
#define RATE_COUNT 4

/*  combo scan */
#define WEXT_CSCAN_AMOUNT 9
#define WEXT_CSCAN_BUF_LEN		360
#define WEXT_CSCAN_HEADER		"CSCAN S\x01\x00\x00S\x00"
#define WEXT_CSCAN_HEADER_SIZE		12
#define WEXT_CSCAN_SSID_SECTION		'S'
#define WEXT_CSCAN_CHANNEL_SECTION	'C'
#define WEXT_CSCAN_NPROBE_SECTION	'N'
#define WEXT_CSCAN_ACTV_DWELL_SECTION	'A'
#define WEXT_CSCAN_PASV_DWELL_SECTION	'P'
#define WEXT_CSCAN_HOME_DWELL_SECTION	'H'
#define WEXT_CSCAN_TYPE_SECTION		'T'

static u32 rtw_rates[] = {1000000, 2000000, 5500000, 11000000,
	6000000, 9000000, 12000000, 18000000, 24000000, 36000000,
	48000000, 54000000};

static const char * const iw_operation_mode[] = {
	"Auto", "Ad-Hoc", "Managed",  "Master", "Repeater",
	"Secondary", "Monitor"
};

void indicate_wx_scan_complete_event(struct adapter *padapter)
{
	union iwreq_data wrqu;

	memset(&wrqu, 0, sizeof(union iwreq_data));
	wireless_send_event(padapter->pnetdev, SIOCGIWSCAN, &wrqu, NULL);
}

void rtw_indicate_wx_assoc_event(struct adapter *padapter)
{
	union iwreq_data wrqu;
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;

	memset(&wrqu, 0, sizeof(union iwreq_data));

	wrqu.ap_addr.sa_family = ARPHRD_ETHER;

	memcpy(wrqu.ap_addr.sa_data, pmlmepriv->cur_network.network.MacAddress, ETH_ALEN);

	DBG_88E_LEVEL(_drv_always_, "assoc success\n");
	wireless_send_event(padapter->pnetdev, SIOCGIWAP, &wrqu, NULL);
}

void rtw_indicate_wx_disassoc_event(struct adapter *padapter)
{
	union iwreq_data wrqu;

	memset(&wrqu, 0, sizeof(union iwreq_data));

	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	eth_zero_addr(wrqu.ap_addr.sa_data);

	DBG_88E_LEVEL(_drv_always_, "indicate disassoc\n");
	wireless_send_event(padapter->pnetdev, SIOCGIWAP, &wrqu, NULL);
}

static char *translate_scan(struct adapter *padapter,
			    struct iw_request_info *info,
			    struct wlan_network *pnetwork,
			    char *start, char *stop)
{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct iw_event iwe;
	u16 cap;
	__le16 le_tmp;
	u32 ht_ielen = 0;
	char custom[MAX_CUSTOM_LEN];
	char *p;
	u16 max_rate = 0, rate, ht_cap = false;
	u32 i = 0;
	u8 bw_40MHz = 0, short_GI = 0;
	u16 mcs_rate = 0;
	u8 ss, sq;

	/*  AP MAC address  */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;

	memcpy(iwe.u.ap_addr.sa_data, pnetwork->network.MacAddress, ETH_ALEN);
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_ADDR_LEN);

	/* Add the ESSID */
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.flags = 1;
	iwe.u.data.length = min_t(u16, pnetwork->network.Ssid.SsidLength, 32);
	start = iwe_stream_add_point(info, start, stop, &iwe, pnetwork->network.Ssid.Ssid);

	/* parsing HT_CAP_IE */
	p = rtw_get_ie(&pnetwork->network.IEs[12], _HT_CAPABILITY_IE_, &ht_ielen, pnetwork->network.IELength-12);

	if (p && ht_ielen > 0) {
		struct rtw_ieee80211_ht_cap *pht_capie;
		ht_cap = true;
		pht_capie = (struct rtw_ieee80211_ht_cap *)(p+2);
		memcpy(&mcs_rate, pht_capie->supp_mcs_set, 2);
		bw_40MHz = (pht_capie->cap_info&IEEE80211_HT_CAP_SUP_WIDTH) ? 1 : 0;
		short_GI = (pht_capie->cap_info&(IEEE80211_HT_CAP_SGI_20|IEEE80211_HT_CAP_SGI_40)) ? 1 : 0;
	}

	/* Add the protocol name */
	iwe.cmd = SIOCGIWNAME;
	if ((rtw_is_cckratesonly_included((u8 *)&pnetwork->network.SupportedRates))) {
		if (ht_cap)
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bn");
		else
		snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11b");
	} else if ((rtw_is_cckrates_included((u8 *)&pnetwork->network.SupportedRates))) {
		if (ht_cap)
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bgn");
		else
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bg");
	} else {
		if (pnetwork->network.Configuration.DSConfig > 14) {
			if (ht_cap)
				snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11an");
			else
				snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11a");
		} else {
			if (ht_cap)
				snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11gn");
			else
				snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11g");
		}
	}

	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_CHAR_LEN);

	  /* Add mode */
	iwe.cmd = SIOCGIWMODE;
	memcpy(&le_tmp, rtw_get_capability_from_ie(pnetwork->network.IEs), 2);

	cap = le16_to_cpu(le_tmp);

	if (!WLAN_CAPABILITY_IS_STA_BSS(cap)) {
		if (cap & WLAN_CAPABILITY_ESS)
			iwe.u.mode = IW_MODE_MASTER;
		else
			iwe.u.mode = IW_MODE_ADHOC;

		start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_UINT_LEN);
	}

	if (pnetwork->network.Configuration.DSConfig < 1)
		pnetwork->network.Configuration.DSConfig = 1;

	 /* Add frequency/channel */
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = rtw_ch2freq(pnetwork->network.Configuration.DSConfig) * 100000;
	iwe.u.freq.e = 1;
	iwe.u.freq.i = pnetwork->network.Configuration.DSConfig;
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_FREQ_LEN);

	/* Add encryption capability */
	iwe.cmd = SIOCGIWENCODE;
	if (cap & WLAN_CAPABILITY_PRIVACY)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	start = iwe_stream_add_point(info, start, stop, &iwe, pnetwork->network.Ssid.Ssid);

	/*Add basic and extended rates */
	max_rate = 0;
	p = custom;
	p += snprintf(p, MAX_CUSTOM_LEN - (p - custom), " Rates (Mb/s): ");
	while (pnetwork->network.SupportedRates[i] != 0) {
		rate = pnetwork->network.SupportedRates[i]&0x7F;
		if (rate > max_rate)
			max_rate = rate;
		p += snprintf(p, MAX_CUSTOM_LEN - (p - custom),
			      "%d%s ", rate >> 1, (rate & 1) ? ".5" : "");
		i++;
	}

	if (ht_cap) {
		if (mcs_rate&0x8000)/* MCS15 */
			max_rate = (bw_40MHz) ? ((short_GI) ? 300 : 270) : ((short_GI) ? 144 : 130);
		else if (mcs_rate&0x0080)/* MCS7 */
			;
		else/* default MCS7 */
			max_rate = (bw_40MHz) ? ((short_GI) ? 150 : 135) : ((short_GI) ? 72 : 65);

		max_rate = max_rate*2;/* Mbps/2; */
	}

	iwe.cmd = SIOCGIWRATE;
	iwe.u.bitrate.fixed = 0;
	iwe.u.bitrate.disabled = 0;
	iwe.u.bitrate.value = max_rate * 500000;
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_PARAM_LEN);

	/* parsing WPA/WPA2 IE */
	{
		u8 buf[MAX_WPA_IE_LEN];
		u8 wpa_ie[255], rsn_ie[255];
		u16 wpa_len = 0, rsn_len = 0;
		u8 *p;

		rtw_get_sec_ie(pnetwork->network.IEs, pnetwork->network.IELength, rsn_ie, &rsn_len, wpa_ie, &wpa_len);
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_wx_get_scan: ssid =%s\n", pnetwork->network.Ssid.Ssid));
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_wx_get_scan: wpa_len =%d rsn_len =%d\n", wpa_len, rsn_len));

		if (wpa_len > 0) {
			p = buf;
			memset(buf, 0, MAX_WPA_IE_LEN);
			p += sprintf(p, "wpa_ie=");
			for (i = 0; i < wpa_len; i++)
				p += sprintf(p, "%02x", wpa_ie[i]);

			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVCUSTOM;
			iwe.u.data.length = strlen(buf);
			start = iwe_stream_add_point(info, start, stop, &iwe, buf);

			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVGENIE;
			iwe.u.data.length = wpa_len;
			start = iwe_stream_add_point(info, start, stop, &iwe, wpa_ie);
		}
		if (rsn_len > 0) {
			p = buf;
			memset(buf, 0, MAX_WPA_IE_LEN);
			p += sprintf(p, "rsn_ie=");
			for (i = 0; i < rsn_len; i++)
				p += sprintf(p, "%02x", rsn_ie[i]);
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVCUSTOM;
			iwe.u.data.length = strlen(buf);
			start = iwe_stream_add_point(info, start, stop, &iwe, buf);

			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVGENIE;
			iwe.u.data.length = rsn_len;
			start = iwe_stream_add_point(info, start, stop, &iwe, rsn_ie);
		}
	}

	{/* parsing WPS IE */
		uint cnt = 0, total_ielen;
		u8 *wpsie_ptr = NULL;
		uint wps_ielen = 0;

		u8 *ie_ptr = pnetwork->network.IEs + _FIXED_IE_LENGTH_;
		total_ielen = pnetwork->network.IELength - _FIXED_IE_LENGTH_;

		while (cnt < total_ielen) {
			if (rtw_is_wps_ie(&ie_ptr[cnt], &wps_ielen) && (wps_ielen > 2)) {
				wpsie_ptr = &ie_ptr[cnt];
				iwe.cmd = IWEVGENIE;
				iwe.u.data.length = (u16)wps_ielen;
				start = iwe_stream_add_point(info, start, stop, &iwe, wpsie_ptr);
			}
			cnt += ie_ptr[cnt+1]+2; /* goto next */
		}
	}

	/* Add quality statistics */
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.updated = IW_QUAL_QUAL_UPDATED | IW_QUAL_LEVEL_UPDATED | IW_QUAL_NOISE_INVALID;

	if (check_fwstate(pmlmepriv, _FW_LINKED) == true &&
	    is_same_network(&pmlmepriv->cur_network.network, &pnetwork->network)) {
		ss = padapter->recvpriv.signal_strength;
		sq = padapter->recvpriv.signal_qual;
	} else {
		ss = pnetwork->network.PhyInfo.SignalStrength;
		sq = pnetwork->network.PhyInfo.SignalQuality;
	}

	iwe.u.qual.level = (u8)ss;
	iwe.u.qual.qual = (u8)sq;   /*  signal quality */
	iwe.u.qual.noise = 0; /*  noise level */
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_QUAL_LEN);
	return start;
}

static int wpa_set_auth_algs(struct net_device *dev, u32 value)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	int ret = 0;

	if ((value & AUTH_ALG_SHARED_KEY) && (value & AUTH_ALG_OPEN_SYSTEM)) {
		DBG_88E("wpa_set_auth_algs, AUTH_ALG_SHARED_KEY and  AUTH_ALG_OPEN_SYSTEM [value:0x%x]\n", value);
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
		padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeAutoSwitch;
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;
	} else if (value & AUTH_ALG_SHARED_KEY) {
		DBG_88E("wpa_set_auth_algs, AUTH_ALG_SHARED_KEY  [value:0x%x]\n", value);
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;

		padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeShared;
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Shared;
	} else if (value & AUTH_ALG_OPEN_SYSTEM) {
		DBG_88E("wpa_set_auth_algs, AUTH_ALG_OPEN_SYSTEM\n");
		if (padapter->securitypriv.ndisauthtype < Ndis802_11AuthModeWPAPSK) {
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeOpen;
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
		}
	} else if (value & AUTH_ALG_LEAP) {
		DBG_88E("wpa_set_auth_algs, AUTH_ALG_LEAP\n");
	} else {
		DBG_88E("wpa_set_auth_algs, error!\n");
		ret = -EINVAL;
	}
	return ret;
}

static int wpa_set_encryption(struct net_device *dev, struct ieee_param *param, u32 param_len)
{
	int ret = 0;
	u32 wep_key_idx, wep_key_len, wep_total_len;
	struct ndis_802_11_wep	 *pwep = NULL;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	param->u.crypt.err = 0;
	param->u.crypt.alg[IEEE_CRYPT_ALG_NAME_LEN - 1] = '\0';

	if (param_len < (u32)((u8 *)param->u.crypt.key - (u8 *)param) + param->u.crypt.key_len) {
		ret =  -EINVAL;
		goto exit;
	}

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff) {
		if (param->u.crypt.idx >= WEP_KEYS) {
			ret = -EINVAL;
			goto exit;
		}
	} else {
		ret = -EINVAL;
		goto exit;
	}

	if (strcmp(param->u.crypt.alg, "WEP") == 0) {
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_, ("wpa_set_encryption, crypt.alg = WEP\n"));
		DBG_88E("wpa_set_encryption, crypt.alg = WEP\n");

		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
		padapter->securitypriv.dot11PrivacyAlgrthm = _WEP40_;
		padapter->securitypriv.dot118021XGrpPrivacy = _WEP40_;

		wep_key_idx = param->u.crypt.idx;
		wep_key_len = param->u.crypt.key_len;

		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_, ("(1)wep_key_idx =%d\n", wep_key_idx));
		DBG_88E("(1)wep_key_idx =%d\n", wep_key_idx);

		if (wep_key_idx > WEP_KEYS)
			return -EINVAL;

		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_, ("(2)wep_key_idx =%d\n", wep_key_idx));

		if (wep_key_len > 0) {
			wep_key_len = wep_key_len <= 5 ? 5 : 13;
			wep_total_len = wep_key_len + offsetof(struct ndis_802_11_wep, KeyMaterial);
			pwep = (struct ndis_802_11_wep *)rtw_malloc(wep_total_len);
			if (pwep == NULL) {
				RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_, (" wpa_set_encryption: pwep allocate fail !!!\n"));
				goto exit;
			}
			memset(pwep, 0, wep_total_len);
			pwep->KeyLength = wep_key_len;
			pwep->Length = wep_total_len;
			if (wep_key_len == 13) {
				padapter->securitypriv.dot11PrivacyAlgrthm = _WEP104_;
				padapter->securitypriv.dot118021XGrpPrivacy = _WEP104_;
			}
		} else {
			ret = -EINVAL;
			goto exit;
		}
		pwep->KeyIndex = wep_key_idx;
		pwep->KeyIndex |= 0x80000000;
		memcpy(pwep->KeyMaterial,  param->u.crypt.key, pwep->KeyLength);
		if (param->u.crypt.set_tx) {
			DBG_88E("wep, set_tx = 1\n");
			if (rtw_set_802_11_add_wep(padapter, pwep) == (u8)_FAIL)
				ret = -EOPNOTSUPP;
		} else {
			DBG_88E("wep, set_tx = 0\n");
			if (wep_key_idx >= WEP_KEYS) {
				ret = -EOPNOTSUPP;
				goto exit;
			}
		      memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), pwep->KeyMaterial, pwep->KeyLength);
			psecuritypriv->dot11DefKeylen[wep_key_idx] = pwep->KeyLength;
			rtw_set_key(padapter, psecuritypriv, wep_key_idx, 0);
		}
		goto exit;
	}

	if (padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) { /*  802_1x */
		struct sta_info *psta, *pbcmc_sta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_MP_STATE)) { /* sta mode */
			psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
			if (psta == NULL) {
				;
			} else {
				if (strcmp(param->u.crypt.alg, "none") != 0)
					psta->ieee8021x_blocked = false;

				if ((padapter->securitypriv.ndisencryptstatus == Ndis802_11Encryption2Enabled) ||
				    (padapter->securitypriv.ndisencryptstatus ==  Ndis802_11Encryption3Enabled))
					psta->dot118021XPrivacy = padapter->securitypriv.dot11PrivacyAlgrthm;

				if (param->u.crypt.set_tx == 1) { /* pairwise key */
					memcpy(psta->dot118021x_UncstKey.skey,  param->u.crypt.key, min_t(u16, param->u.crypt.key_len, 16));

					if (strcmp(param->u.crypt.alg, "TKIP") == 0) { /* set mic key */
						memcpy(psta->dot11tkiptxmickey.skey, &(param->u.crypt.key[16]), 8);
						memcpy(psta->dot11tkiprxmickey.skey, &(param->u.crypt.key[24]), 8);
						padapter->securitypriv.busetkipkey = false;
					}

					DBG_88E(" ~~~~set sta key:unicastkey\n");

					rtw_setstakey_cmd(padapter, (unsigned char *)psta, true);
				} else { /* group key */
					memcpy(padapter->securitypriv.dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, min_t(u16, param->u.crypt.key_len, 16 ));
					memcpy(padapter->securitypriv.dot118021XGrptxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[16]), 8);
					memcpy(padapter->securitypriv.dot118021XGrprxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[24]), 8);
					padapter->securitypriv.binstallGrpkey = true;
					DBG_88E(" ~~~~set sta key:groupkey\n");

					padapter->securitypriv.dot118021XGrpKeyid = param->u.crypt.idx;

					rtw_set_key(padapter, &padapter->securitypriv, param->u.crypt.idx, 1);
				}
			}
			pbcmc_sta = rtw_get_bcmc_stainfo(padapter);
			if (pbcmc_sta == NULL) {
				;
			} else {
				/* Jeff: don't disable ieee8021x_blocked while clearing key */
				if (strcmp(param->u.crypt.alg, "none") != 0)
					pbcmc_sta->ieee8021x_blocked = false;

				if ((padapter->securitypriv.ndisencryptstatus == Ndis802_11Encryption2Enabled) ||
				    (padapter->securitypriv.ndisencryptstatus ==  Ndis802_11Encryption3Enabled))
					pbcmc_sta->dot118021XPrivacy = padapter->securitypriv.dot11PrivacyAlgrthm;
			}
		}
	}

exit:

	kfree(pwep);
	return ret;
}

static int rtw_set_wpa_ie(struct adapter *padapter, char *pie, unsigned short ielen)
{
	u8 *buf = NULL;
	int group_cipher = 0, pairwise_cipher = 0;
	int ret = 0;

	if ((ielen > MAX_WPA_IE_LEN) || (pie == NULL)) {
		_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);
		if (pie == NULL)
			return ret;
		else
			return -EINVAL;
	}

	if (ielen) {
		buf = kmemdup(pie, ielen, GFP_KERNEL);
		if (buf == NULL) {
			ret =  -ENOMEM;
			goto exit;
		}

		/* dump */
		{
			int i;
			DBG_88E("\n wpa_ie(length:%d):\n", ielen);
			for (i = 0; i < ielen; i += 8)
				DBG_88E("0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x\n", buf[i], buf[i+1], buf[i+2], buf[i+3], buf[i+4], buf[i+5], buf[i+6], buf[i+7]);
		}

		if (ielen < RSN_HEADER_LEN) {
			RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_, ("Ie len too short %d\n", ielen));
			ret  = -1;
			goto exit;
		}

		if (rtw_parse_wpa_ie(buf, ielen, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeWPAPSK;
			memcpy(padapter->securitypriv.supplicant_ie, &buf[0], ielen);
		}

		if (rtw_parse_wpa2_ie(buf, ielen, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeWPA2PSK;
			memcpy(padapter->securitypriv.supplicant_ie, &buf[0], ielen);
		}

		switch (group_cipher) {
		case WPA_CIPHER_NONE:
			padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11EncryptionDisabled;
			break;
		case WPA_CIPHER_WEP40:
			padapter->securitypriv.dot118021XGrpPrivacy = _WEP40_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
			break;
		case WPA_CIPHER_TKIP:
			padapter->securitypriv.dot118021XGrpPrivacy = _TKIP_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption2Enabled;
			break;
		case WPA_CIPHER_CCMP:
			padapter->securitypriv.dot118021XGrpPrivacy = _AES_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption3Enabled;
			break;
		case WPA_CIPHER_WEP104:
			padapter->securitypriv.dot118021XGrpPrivacy = _WEP104_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
			break;
		}

		switch (pairwise_cipher) {
		case WPA_CIPHER_NONE:
			padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11EncryptionDisabled;
			break;
		case WPA_CIPHER_WEP40:
			padapter->securitypriv.dot11PrivacyAlgrthm = _WEP40_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
			break;
		case WPA_CIPHER_TKIP:
			padapter->securitypriv.dot11PrivacyAlgrthm = _TKIP_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption2Enabled;
			break;
		case WPA_CIPHER_CCMP:
			padapter->securitypriv.dot11PrivacyAlgrthm = _AES_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption3Enabled;
			break;
		case WPA_CIPHER_WEP104:
			padapter->securitypriv.dot11PrivacyAlgrthm = _WEP104_;
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
			break;
		}

		_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);
		{/* set wps_ie */
			u16 cnt = 0;
			u8 eid, wps_oui[4] = {0x0, 0x50, 0xf2, 0x04};

			while (cnt < ielen) {
				eid = buf[cnt];
				if ((eid == _VENDOR_SPECIFIC_IE_) && (!memcmp(&buf[cnt+2], wps_oui, 4))) {
					DBG_88E("SET WPS_IE\n");

					padapter->securitypriv.wps_ie_len = min(buf[cnt + 1] + 2, MAX_WPA_IE_LEN << 2);

					memcpy(padapter->securitypriv.wps_ie, &buf[cnt], padapter->securitypriv.wps_ie_len);

					set_fwstate(&padapter->mlmepriv, WIFI_UNDER_WPS);
					cnt += buf[cnt+1]+2;
					break;
				} else {
					cnt += buf[cnt+1]+2; /* goto next */
				}
			}
		}
	}

	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
		 ("rtw_set_wpa_ie: pairwise_cipher = 0x%08x padapter->securitypriv.ndisencryptstatus =%d padapter->securitypriv.ndisauthtype =%d\n",
		 pairwise_cipher, padapter->securitypriv.ndisencryptstatus, padapter->securitypriv.ndisauthtype));
exit:
	kfree(buf);
	return ret;
}

typedef unsigned char   NDIS_802_11_RATES_EX[NDIS_802_11_LENGTH_RATES_EX];

static int rtw_wx_get_name(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	u32 ht_ielen = 0;
	char *p;
	u8 ht_cap = false;
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct wlan_bssid_ex  *pcur_bss = &pmlmepriv->cur_network.network;
	NDIS_802_11_RATES_EX *prates = NULL;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("cmd_code =%x\n", info->cmd));

	if (check_fwstate(pmlmepriv, _FW_LINKED|WIFI_ADHOC_MASTER_STATE) == true) {
		/* parsing HT_CAP_IE */
		p = rtw_get_ie(&pcur_bss->IEs[12], _HT_CAPABILITY_IE_, &ht_ielen, pcur_bss->IELength-12);
		if (p && ht_ielen > 0)
			ht_cap = true;

		prates = &pcur_bss->SupportedRates;

		if (rtw_is_cckratesonly_included((u8 *)prates) == true) {
			if (ht_cap)
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bn");
			else
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11b");
		} else if ((rtw_is_cckrates_included((u8 *)prates)) == true) {
			if (ht_cap)
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bgn");
			else
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bg");
		} else {
			if (pcur_bss->Configuration.DSConfig > 14) {
				if (ht_cap)
					snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11an");
				else
					snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11a");
			} else {
				if (ht_cap)
					snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11gn");
				else
					snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11g");
			}
		}
	} else {
		snprintf(wrqu->name, IFNAMSIZ, "unassociated");
	}
	return 0;
}

static int rtw_wx_set_freq(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_notice_, ("+rtw_wx_set_freq\n"));
	return 0;
}

static int rtw_wx_get_freq(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct wlan_bssid_ex  *pcur_bss = &pmlmepriv->cur_network.network;

	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		/* wrqu->freq.m = ieee80211_wlan_frequencies[pcur_bss->Configuration.DSConfig-1] * 100000; */
		wrqu->freq.m = rtw_ch2freq(pcur_bss->Configuration.DSConfig) * 100000;
		wrqu->freq.e = 1;
		wrqu->freq.i = pcur_bss->Configuration.DSConfig;
	} else {
		wrqu->freq.m = rtw_ch2freq(padapter->mlmeextpriv.cur_channel) * 100000;
		wrqu->freq.e = 1;
		wrqu->freq.i = padapter->mlmeextpriv.cur_channel;
	}

	return 0;
}

static int rtw_wx_set_mode(struct net_device *dev, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	enum ndis_802_11_network_infra networkType;
	int ret = 0;

	if (_FAIL == rtw_pwr_wakeup(padapter)) {
		ret = -EPERM;
		goto exit;
	}

	if (!padapter->hw_init_completed) {
		ret = -EPERM;
		goto exit;
	}

	switch (wrqu->mode) {
	case IW_MODE_AUTO:
		networkType = Ndis802_11AutoUnknown;
		DBG_88E("set_mode = IW_MODE_AUTO\n");
		break;
	case IW_MODE_ADHOC:
		networkType = Ndis802_11IBSS;
		DBG_88E("set_mode = IW_MODE_ADHOC\n");
		break;
	case IW_MODE_MASTER:
		networkType = Ndis802_11APMode;
		DBG_88E("set_mode = IW_MODE_MASTER\n");
		break;
	case IW_MODE_INFRA:
		networkType = Ndis802_11Infrastructure;
		DBG_88E("set_mode = IW_MODE_INFRA\n");
		break;
	default:
		ret = -EINVAL;
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_, ("\n Mode: %s is not supported\n", iw_operation_mode[wrqu->mode]));
		goto exit;
	}
	if (rtw_set_802_11_infrastructure_mode(padapter, networkType) == false) {
		ret = -EPERM;
		goto exit;
	}
	rtw_setopmode_cmd(padapter, networkType);
exit:
	return ret;
}

static int rtw_wx_get_mode(struct net_device *dev, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, (" rtw_wx_get_mode\n"));

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE))
		wrqu->mode = IW_MODE_INFRA;
	else if  ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) ||
		  (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)))
		wrqu->mode = IW_MODE_ADHOC;
	else if (check_fwstate(pmlmepriv, WIFI_AP_STATE))
		wrqu->mode = IW_MODE_MASTER;
	else
		wrqu->mode = IW_MODE_AUTO;

	return 0;
}

static int rtw_wx_set_pmkid(struct net_device *dev,
			    struct iw_request_info *a,
			    union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	u8   j, blInserted = false;
	int  ret = false;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct iw_pmksa *pPMK = (struct iw_pmksa *)extra;
	u8     strZeroMacAddress[ETH_ALEN] = {0x00};
	u8     strIssueBssid[ETH_ALEN] = {0x00};

	memcpy(strIssueBssid, pPMK->bssid.sa_data, ETH_ALEN);
	if (pPMK->cmd == IW_PMKSA_ADD) {
		DBG_88E("[rtw_wx_set_pmkid] IW_PMKSA_ADD!\n");
		if (!memcmp(strIssueBssid, strZeroMacAddress, ETH_ALEN))
			return ret;
		else
			ret = true;
		blInserted = false;

		/* overwrite PMKID */
		for (j = 0; j < NUM_PMKID_CACHE; j++) {
			if (!memcmp(psecuritypriv->PMKIDList[j].Bssid, strIssueBssid, ETH_ALEN)) {
				/*  BSSID is matched, the same AP => rewrite with new PMKID. */
				DBG_88E("[rtw_wx_set_pmkid] BSSID exists in the PMKList.\n");
				memcpy(psecuritypriv->PMKIDList[j].PMKID, pPMK->pmkid, IW_PMKID_LEN);
				psecuritypriv->PMKIDList[j].bUsed = true;
				psecuritypriv->PMKIDIndex = j+1;
				blInserted = true;
				break;
			}
		}

		if (!blInserted) {
			/*  Find a new entry */
			DBG_88E("[rtw_wx_set_pmkid] Use the new entry index = %d for this PMKID.\n",
				psecuritypriv->PMKIDIndex);

			memcpy(psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].Bssid, strIssueBssid, ETH_ALEN);
			memcpy(psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].PMKID, pPMK->pmkid, IW_PMKID_LEN);

			psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].bUsed = true;
			psecuritypriv->PMKIDIndex++;
			if (psecuritypriv->PMKIDIndex == 16)
				psecuritypriv->PMKIDIndex = 0;
		}
	} else if (pPMK->cmd == IW_PMKSA_REMOVE) {
		DBG_88E("[rtw_wx_set_pmkid] IW_PMKSA_REMOVE!\n");
		ret = true;
		for (j = 0; j < NUM_PMKID_CACHE; j++) {
			if (!memcmp(psecuritypriv->PMKIDList[j].Bssid, strIssueBssid, ETH_ALEN)) {
				/*  BSSID is matched, the same AP => Remove this PMKID information and reset it. */
				eth_zero_addr(psecuritypriv->PMKIDList[j].Bssid);
				psecuritypriv->PMKIDList[j].bUsed = false;
				break;
			}
	       }
	} else if (pPMK->cmd == IW_PMKSA_FLUSH) {
		DBG_88E("[rtw_wx_set_pmkid] IW_PMKSA_FLUSH!\n");
		memset(&psecuritypriv->PMKIDList[0], 0x00, sizeof(struct rt_pmkid_list) * NUM_PMKID_CACHE);
		psecuritypriv->PMKIDIndex = 0;
		ret = true;
	}
	return ret;
}

static int rtw_wx_get_sens(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	wrqu->sens.value = 0;
	wrqu->sens.fixed = 0;	/* no auto select */
	wrqu->sens.disabled = 1;
	return 0;
}

static int rtw_wx_get_range(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct iw_range *range = (struct iw_range *)extra;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;

	u16 val;
	int i;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_wx_get_range. cmd_code =%x\n", info->cmd));

	wrqu->data.length = sizeof(*range);
	memset(range, 0, sizeof(*range));

	/* Let's try to keep this struct in the same order as in
	 * linux/include/wireless.h
	 */

	/* TODO: See what values we can set, and remove the ones we can't
	 * set, or fill them with some default data.
	 */

	/* ~5 Mb/s real (802.11b) */
	range->throughput = 5 * 1000 * 1000;

	/* signal level threshold range */

	/* percent values between 0 and 100. */
	range->max_qual.qual = 100;
	range->max_qual.level = 100;
	range->max_qual.noise = 100;
	range->max_qual.updated = 7; /* Updated all three */

	range->avg_qual.qual = 92; /* > 8% missed beacons is 'bad' */
	/* TODO: Find real 'good' to 'bad' threshol value for RSSI */
	range->avg_qual.level = 178; /* -78 dBm */
	range->avg_qual.noise = 0;
	range->avg_qual.updated = 7; /* Updated all three */

	range->num_bitrates = RATE_COUNT;

	for (i = 0; i < RATE_COUNT && i < IW_MAX_BITRATES; i++)
		range->bitrate[i] = rtw_rates[i];

	range->min_frag = MIN_FRAG_THRESHOLD;
	range->max_frag = MAX_FRAG_THRESHOLD;

	range->pm_capa = 0;

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 16;

	for (i = 0, val = 0; i < MAX_CHANNEL_NUM; i++) {
		/*  Include only legal frequencies for some countries */
		if (pmlmeext->channel_set[i].ChannelNum != 0) {
			range->freq[val].i = pmlmeext->channel_set[i].ChannelNum;
			range->freq[val].m = rtw_ch2freq(pmlmeext->channel_set[i].ChannelNum) * 100000;
			range->freq[val].e = 1;
			val++;
		}

		if (val == IW_MAX_FREQUENCIES)
			break;
	}

	range->num_channels = val;
	range->num_frequency = val;

/*  The following code will proivde the security capability to network manager. */
/*  If the driver doesn't provide this capability to network manager, */
/*  the WPA/WPA2 routers can't be chosen in the network manager. */

/*
#define IW_SCAN_CAPA_NONE		0x00
#define IW_SCAN_CAPA_ESSID		0x01
#define IW_SCAN_CAPA_BSSID		0x02
#define IW_SCAN_CAPA_CHANNEL		0x04
#define IW_SCAN_CAPA_MODE		0x08
#define IW_SCAN_CAPA_RATE		0x10
#define IW_SCAN_CAPA_TYPE		0x20
#define IW_SCAN_CAPA_TIME		0x40
*/

	range->enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_WPA2 |
			  IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_CIPHER_CCMP;

	range->scan_capa = IW_SCAN_CAPA_ESSID | IW_SCAN_CAPA_TYPE |
			   IW_SCAN_CAPA_BSSID | IW_SCAN_CAPA_CHANNEL |
			   IW_SCAN_CAPA_MODE | IW_SCAN_CAPA_RATE;
	return 0;
}

/* set bssid flow */
/* s1. rtw_set_802_11_infrastructure_mode() */
/* s2. rtw_set_802_11_authentication_mode() */
/* s3. set_802_11_encryption_mode() */
/* s4. rtw_set_802_11_bssid() */
static int rtw_wx_set_wap(struct net_device *dev,
			 struct iw_request_info *info,
			 union iwreq_data *awrq,
			 char *extra)
{
	uint ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct sockaddr *temp = (struct sockaddr *)awrq;
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct list_head *phead;
	u8 *dst_bssid, *src_bssid;
	struct __queue *queue	= &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	enum ndis_802_11_auth_mode	authmode;

	if (_FAIL == rtw_pwr_wakeup(padapter)) {
		ret = -1;
		goto exit;
	}

	if (!padapter->bup) {
		ret = -1;
		goto exit;
	}

	if (temp->sa_family != ARPHRD_ETHER) {
		ret = -EINVAL;
		goto exit;
	}

	authmode = padapter->securitypriv.ndisauthtype;
	spin_lock_bh(&queue->lock);
	phead = get_list_head(queue);
	pmlmepriv->pscanned = phead->next;

	while (phead != pmlmepriv->pscanned) {
		pnetwork = container_of(pmlmepriv->pscanned, struct wlan_network, list);

		pmlmepriv->pscanned = pmlmepriv->pscanned->next;

		dst_bssid = pnetwork->network.MacAddress;

		src_bssid = temp->sa_data;

		if ((!memcmp(dst_bssid, src_bssid, ETH_ALEN))) {
			if (!rtw_set_802_11_infrastructure_mode(padapter, pnetwork->network.InfrastructureMode)) {
				ret = -1;
				spin_unlock_bh(&queue->lock);
				goto exit;
			}

				break;
		}
	}
	spin_unlock_bh(&queue->lock);

	rtw_set_802_11_authentication_mode(padapter, authmode);
	/* set_802_11_encryption_mode(padapter, padapter->securitypriv.ndisencryptstatus); */
	if (rtw_set_802_11_bssid(padapter, temp->sa_data) == false) {
		ret = -1;
		goto exit;
	}

exit:

	return ret;
}

static int rtw_wx_get_wap(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct wlan_bssid_ex  *pcur_bss = &pmlmepriv->cur_network.network;

	wrqu->ap_addr.sa_family = ARPHRD_ETHER;

	eth_zero_addr(wrqu->ap_addr.sa_data);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_wx_get_wap\n"));

	if (((check_fwstate(pmlmepriv, _FW_LINKED)) == true) ||
	    ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) == true) ||
	    ((check_fwstate(pmlmepriv, WIFI_AP_STATE)) == true))
		memcpy(wrqu->ap_addr.sa_data, pcur_bss->MacAddress, ETH_ALEN);
	else
		eth_zero_addr(wrqu->ap_addr.sa_data);
	return 0;
}

static int rtw_wx_set_mlme(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	u16 reason;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct iw_mlme *mlme = (struct iw_mlme *)extra;

	if (mlme == NULL)
		return -1;

	DBG_88E("%s\n", __func__);

	reason = mlme->reason_code;

	DBG_88E("%s, cmd =%d, reason =%d\n", __func__, mlme->cmd, reason);

	switch (mlme->cmd) {
	case IW_MLME_DEAUTH:
		if (!rtw_set_802_11_disassociate(padapter))
			ret = -1;
		break;
	case IW_MLME_DISASSOC:
		if (!rtw_set_802_11_disassociate(padapter))
			ret = -1;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return ret;
}

static int rtw_wx_set_scan(struct net_device *dev, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *extra)
{
	u8 _status = false;
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ndis_802_11_ssid ssid[RTW_SSID_SCAN_AMOUNT];
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_wx_set_scan\n"));

	if (padapter->registrypriv.mp_mode == 1) {
		if (check_fwstate(pmlmepriv, WIFI_MP_STATE)) {
			ret = -1;
			goto exit;
		}
	}
	if (_FAIL == rtw_pwr_wakeup(padapter)) {
		ret = -1;
		goto exit;
	}

	if (padapter->bDriverStopped) {
		DBG_88E("bDriverStopped =%d\n", padapter->bDriverStopped);
		ret = -1;
		goto exit;
	}

	if (!padapter->bup) {
		ret = -1;
		goto exit;
	}

	if (!padapter->hw_init_completed) {
		ret = -1;
		goto exit;
	}

	/*  When Busy Traffic, driver do not site survey. So driver return success. */
	/*  wpa_supplicant will not issue SIOCSIWSCAN cmd again after scan timeout. */
	/*  modify by thomas 2011-02-22. */
	if (pmlmepriv->LinkDetectInfo.bBusyTraffic) {
		indicate_wx_scan_complete_event(padapter);
		goto exit;
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING)) {
		indicate_wx_scan_complete_event(padapter);
		goto exit;
	}

/*	For the DMP WiFi Display project, the driver won't to scan because */
/*	the pmlmepriv->scan_interval is always equal to 3. */
/*	So, the wpa_supplicant won't find out the WPS SoftAP. */

	memset(ssid, 0, sizeof(struct ndis_802_11_ssid)*RTW_SSID_SCAN_AMOUNT);

	if (wrqu->data.length == sizeof(struct iw_scan_req)) {
		struct iw_scan_req *req = (struct iw_scan_req *)extra;

		if (wrqu->data.flags & IW_SCAN_THIS_ESSID) {
			int len = min_t(int, req->essid_len,
					IW_ESSID_MAX_SIZE);

			memcpy(ssid[0].Ssid, req->essid, len);
			ssid[0].SsidLength = len;

			DBG_88E("IW_SCAN_THIS_ESSID, ssid =%s, len =%d\n", req->essid, req->essid_len);

			spin_lock_bh(&pmlmepriv->lock);

			_status = rtw_sitesurvey_cmd(padapter, ssid, 1, NULL, 0);

			spin_unlock_bh(&pmlmepriv->lock);
		} else if (req->scan_type == IW_SCAN_TYPE_PASSIVE) {
			DBG_88E("rtw_wx_set_scan, req->scan_type == IW_SCAN_TYPE_PASSIVE\n");
		}
	} else {
		if (wrqu->data.length >= WEXT_CSCAN_HEADER_SIZE &&
		    !memcmp(extra, WEXT_CSCAN_HEADER, WEXT_CSCAN_HEADER_SIZE)) {
			int len = wrqu->data.length - WEXT_CSCAN_HEADER_SIZE;
			char *pos = extra+WEXT_CSCAN_HEADER_SIZE;
			char section;
			char sec_len;
			int ssid_index = 0;

			while (len >= 1) {
				section = *(pos++);
				len -= 1;

				switch (section) {
				case WEXT_CSCAN_SSID_SECTION:
					if (len < 1) {
						len = 0;
						break;
					}
					sec_len = *(pos++); len -= 1;
					if (sec_len > 0 && sec_len <= len) {
						ssid[ssid_index].SsidLength = sec_len;
						memcpy(ssid[ssid_index].Ssid, pos, ssid[ssid_index].SsidLength);
						ssid_index++;
					}
					pos += sec_len;
					len -= sec_len;
					break;
				case WEXT_CSCAN_TYPE_SECTION:
				case WEXT_CSCAN_CHANNEL_SECTION:
					pos += 1;
					len -= 1;
					break;
				case WEXT_CSCAN_PASV_DWELL_SECTION:
				case WEXT_CSCAN_HOME_DWELL_SECTION:
				case WEXT_CSCAN_ACTV_DWELL_SECTION:
					pos += 2;
					len -= 2;
					break;
				default:
					len = 0; /*  stop parsing */
				}
			}

			/* it has still some scan parameter to parse, we only do this now... */
			_status = rtw_set_802_11_bssid_list_scan(padapter, ssid, RTW_SSID_SCAN_AMOUNT);
		} else {
			_status = rtw_set_802_11_bssid_list_scan(padapter, NULL, 0);
		}
	}

	if (!_status)
		ret = -1;

exit:

	return ret;
}

static int rtw_wx_get_scan(struct net_device *dev, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *extra)
{
	struct list_head *plist, *phead;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct __queue *queue	= &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	char *ev = extra;
	char *stop = ev + wrqu->data.length;
	u32 ret = 0;
	u32 cnt = 0;
	u32 wait_for_surveydone;
	int wait_status;
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_wx_get_scan\n"));
	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_, (" Start of Query SIOCGIWSCAN .\n"));

	if (padapter->pwrctrlpriv.brfoffbyhw && padapter->bDriverStopped) {
		ret = -EINVAL;
		goto exit;
	}

	wait_for_surveydone = 100;

	wait_status = _FW_UNDER_SURVEY | _FW_UNDER_LINKING;

	while (check_fwstate(pmlmepriv, wait_status)) {
		msleep(30);
		cnt++;
		if (cnt > wait_for_surveydone)
			break;
	}

	spin_lock_bh(&(pmlmepriv->scanned_queue.lock));

	phead = get_list_head(queue);
	plist = phead->next;

	while (phead != plist) {
		if ((stop - ev) < SCAN_ITEM_SIZE) {
			ret = -E2BIG;
			break;
		}

		pnetwork = container_of(plist, struct wlan_network, list);

		/* report network only if the current channel set contains the channel to which this network belongs */
		if (rtw_ch_set_search_ch(padapter->mlmeextpriv.channel_set, pnetwork->network.Configuration.DSConfig) >= 0)
			ev = translate_scan(padapter, a, pnetwork, ev, stop);

		plist = plist->next;
	}

	spin_unlock_bh(&pmlmepriv->scanned_queue.lock);

	wrqu->data.length = ev-extra;
	wrqu->data.flags = 0;

exit:
	return ret;
}

/* set ssid flow */
/* s1. rtw_set_802_11_infrastructure_mode() */
/* s2. set_802_11_authenticaion_mode() */
/* s3. set_802_11_encryption_mode() */
/* s4. rtw_set_802_11_ssid() */
static int rtw_wx_set_essid(struct net_device *dev,
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct __queue *queue = &pmlmepriv->scanned_queue;
	struct list_head *phead;
	struct wlan_network *pnetwork = NULL;
	enum ndis_802_11_auth_mode authmode;
	struct ndis_802_11_ssid ndis_ssid;
	u8 *dst_ssid, *src_ssid;

	uint ret = 0, len;


	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
		 ("+rtw_wx_set_essid: fw_state = 0x%08x\n", get_fwstate(pmlmepriv)));
	if (_FAIL == rtw_pwr_wakeup(padapter)) {
		ret = -1;
		goto exit;
	}

	if (!padapter->bup) {
		ret = -1;
		goto exit;
	}

	if (wrqu->essid.length > IW_ESSID_MAX_SIZE) {
		ret = -E2BIG;
		goto exit;
	}

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		ret = -1;
		goto exit;
	}

	authmode = padapter->securitypriv.ndisauthtype;
	DBG_88E("=>%s\n", __func__);
	if (wrqu->essid.flags && wrqu->essid.length) {
		len = min_t(uint, wrqu->essid.length, IW_ESSID_MAX_SIZE);

		if (wrqu->essid.length != 33)
			DBG_88E("ssid =%s, len =%d\n", extra, wrqu->essid.length);

		memset(&ndis_ssid, 0, sizeof(struct ndis_802_11_ssid));
		ndis_ssid.SsidLength = len;
		memcpy(ndis_ssid.Ssid, extra, len);
		src_ssid = ndis_ssid.Ssid;

		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_, ("rtw_wx_set_essid: ssid =[%s]\n", src_ssid));
		spin_lock_bh(&queue->lock);
	       phead = get_list_head(queue);
	      pmlmepriv->pscanned = phead->next;

		while (phead != pmlmepriv->pscanned) {
			pnetwork = container_of(pmlmepriv->pscanned, struct wlan_network, list);

			pmlmepriv->pscanned = pmlmepriv->pscanned->next;

			dst_ssid = pnetwork->network.Ssid.Ssid;

			RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
				 ("rtw_wx_set_essid: dst_ssid =%s\n",
				  pnetwork->network.Ssid.Ssid));

			if ((!memcmp(dst_ssid, src_ssid, ndis_ssid.SsidLength)) &&
			    (pnetwork->network.Ssid.SsidLength == ndis_ssid.SsidLength)) {
				RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
					 ("rtw_wx_set_essid: find match, set infra mode\n"));

				if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true) {
					if (pnetwork->network.InfrastructureMode != pmlmepriv->cur_network.network.InfrastructureMode)
						continue;
				}

				if (!rtw_set_802_11_infrastructure_mode(padapter, pnetwork->network.InfrastructureMode)) {
					ret = -1;
					spin_unlock_bh(&queue->lock);
					goto exit;
				}

				break;
			}
		}
		spin_unlock_bh(&queue->lock);
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
			 ("set ssid: set_802_11_auth. mode =%d\n", authmode));
		rtw_set_802_11_authentication_mode(padapter, authmode);
		if (rtw_set_802_11_ssid(padapter, &ndis_ssid) == false) {
			ret = -1;
			goto exit;
		}
	}

exit:

	DBG_88E("<=%s, ret %d\n", __func__, ret);


	return ret;
}

static int rtw_wx_get_essid(struct net_device *dev,
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *extra)
{
	u32 len, ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct wlan_bssid_ex  *pcur_bss = &pmlmepriv->cur_network.network;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_wx_get_essid\n"));


	if ((check_fwstate(pmlmepriv, _FW_LINKED)) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE))) {
		len = pcur_bss->Ssid.SsidLength;

		wrqu->essid.length = len;

		memcpy(extra, pcur_bss->Ssid.Ssid, len);

		wrqu->essid.flags = 1;
	} else {
		ret = -1;
		goto exit;
	}

exit:


	return ret;
}

static int rtw_wx_set_rate(struct net_device *dev,
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *extra)
{
	int i;
	u8 datarates[NumRates];
	u32	target_rate = wrqu->bitrate.value;
	u32	fixed = wrqu->bitrate.fixed;
	u32	ratevalue = 0;
	 u8 mpdatarate[NumRates] = {11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0xff};


	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, (" rtw_wx_set_rate\n"));
	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_, ("target_rate = %d, fixed = %d\n", target_rate, fixed));

	if (target_rate == -1) {
		ratevalue = 11;
		goto set_rate;
	}
	target_rate = target_rate/100000;

	switch (target_rate) {
	case 10:
		ratevalue = 0;
		break;
	case 20:
		ratevalue = 1;
		break;
	case 55:
		ratevalue = 2;
		break;
	case 60:
		ratevalue = 3;
		break;
	case 90:
		ratevalue = 4;
		break;
	case 110:
		ratevalue = 5;
		break;
	case 120:
		ratevalue = 6;
		break;
	case 180:
		ratevalue = 7;
		break;
	case 240:
		ratevalue = 8;
		break;
	case 360:
		ratevalue = 9;
		break;
	case 480:
		ratevalue = 10;
		break;
	case 540:
		ratevalue = 11;
		break;
	default:
		ratevalue = 11;
		break;
	}

set_rate:

	for (i = 0; i < NumRates; i++) {
		if (ratevalue == mpdatarate[i]) {
			datarates[i] = mpdatarate[i];
			if (fixed == 0)
				break;
		} else {
			datarates[i] = 0xff;
		}

		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_, ("datarate_inx =%d\n", datarates[i]));
	}

	return 0;
}

static int rtw_wx_get_rate(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	u16 max_rate = 0;

	max_rate = rtw_get_cur_max_rate((struct adapter *)rtw_netdev_priv(dev));

	if (max_rate == 0)
		return -EPERM;

	wrqu->bitrate.fixed = 0;	/* no auto select */
	wrqu->bitrate.value = max_rate * 100000;

	return 0;
}

static int rtw_wx_set_rts(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);


	if (wrqu->rts.disabled) {
		padapter->registrypriv.rts_thresh = 2347;
	} else {
		if (wrqu->rts.value < 0 ||
		    wrqu->rts.value > 2347)
			return -EINVAL;

		padapter->registrypriv.rts_thresh = wrqu->rts.value;
	}

	DBG_88E("%s, rts_thresh =%d\n", __func__, padapter->registrypriv.rts_thresh);


	return 0;
}

static int rtw_wx_get_rts(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);


	DBG_88E("%s, rts_thresh =%d\n", __func__, padapter->registrypriv.rts_thresh);

	wrqu->rts.value = padapter->registrypriv.rts_thresh;
	wrqu->rts.fixed = 0;	/* no auto select */
	/* wrqu->rts.disabled = (wrqu->rts.value == DEFAULT_RTS_THRESHOLD); */


	return 0;
}

static int rtw_wx_set_frag(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);


	if (wrqu->frag.disabled) {
		padapter->xmitpriv.frag_len = MAX_FRAG_THRESHOLD;
	} else {
		if (wrqu->frag.value < MIN_FRAG_THRESHOLD ||
		    wrqu->frag.value > MAX_FRAG_THRESHOLD)
			return -EINVAL;

		padapter->xmitpriv.frag_len = wrqu->frag.value & ~0x1;
	}

	DBG_88E("%s, frag_len =%d\n", __func__, padapter->xmitpriv.frag_len);


	return 0;
}

static int rtw_wx_get_frag(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);


	DBG_88E("%s, frag_len =%d\n", __func__, padapter->xmitpriv.frag_len);

	wrqu->frag.value = padapter->xmitpriv.frag_len;
	wrqu->frag.fixed = 0;	/* no auto select */


	return 0;
}

static int rtw_wx_get_retry(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	wrqu->retry.value = 7;
	wrqu->retry.fixed = 0;	/* no auto select */
	wrqu->retry.disabled = 1;

	return 0;
}

static int rtw_wx_set_enc(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *keybuf)
{
	u32 key, ret = 0;
	u32 keyindex_provided;
	struct ndis_802_11_wep	 wep;
	enum ndis_802_11_auth_mode authmode;

	struct iw_point *erq = &(wrqu->encoding);
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	DBG_88E("+rtw_wx_set_enc, flags = 0x%x\n", erq->flags);

	memset(&wep, 0, sizeof(struct ndis_802_11_wep));

	key = erq->flags & IW_ENCODE_INDEX;


	if (erq->flags & IW_ENCODE_DISABLED) {
		DBG_88E("EncryptionDisabled\n");
		padapter->securitypriv.ndisencryptstatus = Ndis802_11EncryptionDisabled;
		padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
		padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
		authmode = Ndis802_11AuthModeOpen;
		padapter->securitypriv.ndisauthtype = authmode;

		goto exit;
	}

	if (key) {
		if (key > WEP_KEYS)
			return -EINVAL;
		key--;
		keyindex_provided = 1;
	} else {
		keyindex_provided = 0;
		key = padapter->securitypriv.dot11PrivacyKeyIndex;
		DBG_88E("rtw_wx_set_enc, key =%d\n", key);
	}

	/* set authentication mode */
	if (erq->flags & IW_ENCODE_OPEN) {
		DBG_88E("rtw_wx_set_enc():IW_ENCODE_OPEN\n");
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;/* Ndis802_11EncryptionDisabled; */
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
		padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
		padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
		authmode = Ndis802_11AuthModeOpen;
		padapter->securitypriv.ndisauthtype = authmode;
	} else if (erq->flags & IW_ENCODE_RESTRICTED) {
		DBG_88E("rtw_wx_set_enc():IW_ENCODE_RESTRICTED\n");
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Shared;
		padapter->securitypriv.dot11PrivacyAlgrthm = _WEP40_;
		padapter->securitypriv.dot118021XGrpPrivacy = _WEP40_;
		authmode = Ndis802_11AuthModeShared;
		padapter->securitypriv.ndisauthtype = authmode;
	} else {
		DBG_88E("rtw_wx_set_enc():erq->flags = 0x%x\n", erq->flags);

		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;/* Ndis802_11EncryptionDisabled; */
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
		padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
		padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
		authmode = Ndis802_11AuthModeOpen;
		padapter->securitypriv.ndisauthtype = authmode;
	}

	wep.KeyIndex = key;
	if (erq->length > 0) {
		wep.KeyLength = erq->length <= 5 ? 5 : 13;

		wep.Length = wep.KeyLength + offsetof(struct ndis_802_11_wep, KeyMaterial);
	} else {
		wep.KeyLength = 0;

		if (keyindex_provided == 1) {
			/*  set key_id only, no given KeyMaterial(erq->length == 0). */
			padapter->securitypriv.dot11PrivacyKeyIndex = key;

			DBG_88E("(keyindex_provided == 1), keyid =%d, key_len =%d\n", key, padapter->securitypriv.dot11DefKeylen[key]);

			switch (padapter->securitypriv.dot11DefKeylen[key]) {
			case 5:
				padapter->securitypriv.dot11PrivacyAlgrthm = _WEP40_;
				break;
			case 13:
				padapter->securitypriv.dot11PrivacyAlgrthm = _WEP104_;
				break;
			default:
				padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
				break;
			}

			goto exit;
		}
	}

	wep.KeyIndex |= 0x80000000;

	memcpy(wep.KeyMaterial, keybuf, wep.KeyLength);

	if (rtw_set_802_11_add_wep(padapter, &wep) == false) {
		if (rf_on == pwrpriv->rf_pwrstate)
			ret = -EOPNOTSUPP;
		goto exit;
	}

exit:


	return ret;
}

static int rtw_wx_get_enc(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *keybuf)
{
	uint key, ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct iw_point *erq = &(wrqu->encoding);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);


	if (check_fwstate(pmlmepriv, _FW_LINKED) != true) {
		if (!check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) {
			erq->length = 0;
			erq->flags |= IW_ENCODE_DISABLED;
			return 0;
		}
	}

	key = erq->flags & IW_ENCODE_INDEX;

	if (key) {
		if (key > WEP_KEYS)
			return -EINVAL;
		key--;
	} else {
		key = padapter->securitypriv.dot11PrivacyKeyIndex;
	}

	erq->flags = key + 1;

	switch (padapter->securitypriv.ndisencryptstatus) {
	case Ndis802_11EncryptionNotSupported:
	case Ndis802_11EncryptionDisabled:
		erq->length = 0;
		erq->flags |= IW_ENCODE_DISABLED;
		break;
	case Ndis802_11Encryption1Enabled:
		erq->length = padapter->securitypriv.dot11DefKeylen[key];
		if (erq->length) {
			memcpy(keybuf, padapter->securitypriv.dot11DefKey[key].skey, padapter->securitypriv.dot11DefKeylen[key]);

			erq->flags |= IW_ENCODE_ENABLED;

			if (padapter->securitypriv.ndisauthtype == Ndis802_11AuthModeOpen)
				erq->flags |= IW_ENCODE_OPEN;
			else if (padapter->securitypriv.ndisauthtype == Ndis802_11AuthModeShared)
				erq->flags |= IW_ENCODE_RESTRICTED;
		} else {
			erq->length = 0;
			erq->flags |= IW_ENCODE_DISABLED;
		}
		break;
	case Ndis802_11Encryption2Enabled:
	case Ndis802_11Encryption3Enabled:
		erq->length = 16;
		erq->flags |= (IW_ENCODE_ENABLED | IW_ENCODE_OPEN | IW_ENCODE_NOKEY);
		break;
	default:
		erq->length = 0;
		erq->flags |= IW_ENCODE_DISABLED;
		break;
	}

	return ret;
}

static int rtw_wx_get_power(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	wrqu->power.value = 0;
	wrqu->power.fixed = 0;	/* no auto select */
	wrqu->power.disabled = 1;

	return 0;
}

static int rtw_wx_set_gen_ie(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

	return rtw_set_wpa_ie(padapter, extra, wrqu->data.length);
}

static int rtw_wx_set_auth(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct iw_param *param = (struct iw_param *)&(wrqu->param);
	int ret = 0;

	switch (param->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
		break;
	case IW_AUTH_CIPHER_PAIRWISE:

		break;
	case IW_AUTH_CIPHER_GROUP:

		break;
	case IW_AUTH_KEY_MGMT:
		/*
		 *  ??? does not use these parameters
		 */
		break;
	case IW_AUTH_TKIP_COUNTERMEASURES:
		if (param->value) {
			/*  wpa_supplicant is enabling the tkip countermeasure. */
			padapter->securitypriv.btkip_countermeasure = true;
		} else {
			/*  wpa_supplicant is disabling the tkip countermeasure. */
			padapter->securitypriv.btkip_countermeasure = false;
		}
		break;
	case IW_AUTH_DROP_UNENCRYPTED:
		/* HACK:
		 *
		 * wpa_supplicant calls set_wpa_enabled when the driver
		 * is loaded and unloaded, regardless of if WPA is being
		 * used.  No other calls are made which can be used to
		 * determine if encryption will be used or not prior to
		 * association being expected.  If encryption is not being
		 * used, drop_unencrypted is set to false, else true -- we
		 * can use this to determine if the CAP_PRIVACY_ON bit should
		 * be set.
		 */

		if (padapter->securitypriv.ndisencryptstatus == Ndis802_11Encryption1Enabled)
			break;/* it means init value, or using wep, ndisencryptstatus = Ndis802_11Encryption1Enabled, */
					/*  then it needn't reset it; */

		if (param->value) {
			padapter->securitypriv.ndisencryptstatus = Ndis802_11EncryptionDisabled;
			padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
			padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeOpen;
		}

		break;
	case IW_AUTH_80211_AUTH_ALG:
		/*
		 *  It's the starting point of a link layer connection using wpa_supplicant
		*/
		if (check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {
			LeaveAllPowerSaveMode(padapter);
			rtw_disassoc_cmd(padapter, 500, false);
			DBG_88E("%s...call rtw_indicate_disconnect\n ", __func__);
			rtw_indicate_disconnect(padapter);
			rtw_free_assoc_resources(padapter);
		}
		ret = wpa_set_auth_algs(dev, (u32)param->value);
		break;
	case IW_AUTH_WPA_ENABLED:
		break;
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		break;
	case IW_AUTH_PRIVACY_INVOKED:
		break;
	default:
		return -EOPNOTSUPP;
	}

	return ret;
}

static int rtw_wx_set_enc_ext(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	char *alg_name;
	u32 param_len;
	struct ieee_param *param = NULL;
	struct iw_point *pencoding = &wrqu->encoding;
	struct iw_encode_ext *pext = (struct iw_encode_ext *)extra;
	int ret = 0;

	param_len = sizeof(struct ieee_param) + pext->key_len;
	param = (struct ieee_param *)rtw_malloc(param_len);
	if (param == NULL)
		return -1;

	memset(param, 0, param_len);

	param->cmd = IEEE_CMD_SET_ENCRYPTION;
	eth_broadcast_addr(param->sta_addr);

	switch (pext->alg) {
	case IW_ENCODE_ALG_NONE:
		/* todo: remove key */
		/* remove = 1; */
		alg_name = "none";
		break;
	case IW_ENCODE_ALG_WEP:
		alg_name = "WEP";
		break;
	case IW_ENCODE_ALG_TKIP:
		alg_name = "TKIP";
		break;
	case IW_ENCODE_ALG_CCMP:
		alg_name = "CCMP";
		break;
	default:
		ret = -1;
		goto exit;
	}

	strncpy((char *)param->u.crypt.alg, alg_name, IEEE_CRYPT_ALG_NAME_LEN);

	if (pext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY)
		param->u.crypt.set_tx = 1;

	/* cliW: WEP does not have group key
	 * just not checking GROUP key setting
	 */
	if ((pext->alg != IW_ENCODE_ALG_WEP) &&
	    (pext->ext_flags & IW_ENCODE_EXT_GROUP_KEY))
		param->u.crypt.set_tx = 0;

	param->u.crypt.idx = (pencoding->flags&0x00FF) - 1;

	if (pext->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID)
		memcpy(param->u.crypt.seq, pext->rx_seq, 8);

	if (pext->key_len) {
		param->u.crypt.key_len = pext->key_len;
		memcpy(param->u.crypt.key, pext + 1, pext->key_len);
	}

	ret =  wpa_set_encryption(dev, param, param_len);

exit:
	kfree(param);
	return ret;
}

static int rtw_wx_get_nick(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	if (extra) {
		wrqu->data.length = 14;
		wrqu->data.flags = 1;
		memcpy(extra, "<WIFI@REALTEK>", 14);
	}

	/* dump debug info here */
	return 0;
}

static int dummy(struct net_device *dev, struct iw_request_info *a,
		 union iwreq_data *wrqu, char *b)
{
	return -1;
}

static int wpa_set_param(struct net_device *dev, u8 name, u32 value)
{
	uint ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

	switch (name) {
	case IEEE_PARAM_WPA_ENABLED:
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_8021X; /* 802.1x */
		switch ((value)&0xff) {
		case 1: /* WPA */
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeWPAPSK; /* WPA_PSK */
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption2Enabled;
			break;
		case 2: /* WPA2 */
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeWPA2PSK; /* WPA2_PSK */
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption3Enabled;
			break;
		}
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
			 ("wpa_set_param:padapter->securitypriv.ndisauthtype =%d\n", padapter->securitypriv.ndisauthtype));
		break;
	case IEEE_PARAM_TKIP_COUNTERMEASURES:
		break;
	case IEEE_PARAM_DROP_UNENCRYPTED: {
		/* HACK:
		 *
		 * wpa_supplicant calls set_wpa_enabled when the driver
		 * is loaded and unloaded, regardless of if WPA is being
		 * used.  No other calls are made which can be used to
		 * determine if encryption will be used or not prior to
		 * association being expected.  If encryption is not being
		 * used, drop_unencrypted is set to false, else true -- we
		 * can use this to determine if the CAP_PRIVACY_ON bit should
		 * be set.
		 */

		break;
	}
	case IEEE_PARAM_PRIVACY_INVOKED:
		break;

	case IEEE_PARAM_AUTH_ALGS:
		ret = wpa_set_auth_algs(dev, value);
		break;
	case IEEE_PARAM_IEEE_802_1X:
		break;
	case IEEE_PARAM_WPAX_SELECT:
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	return ret;
}

static int wpa_mlme(struct net_device *dev, u32 command, u32 reason)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

	switch (command) {
	case IEEE_MLME_STA_DEAUTH:
		if (!rtw_set_802_11_disassociate(padapter))
			ret = -1;
		break;
	case IEEE_MLME_STA_DISASSOC:
		if (!rtw_set_802_11_disassociate(padapter))
			ret = -1;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int wpa_supplicant_ioctl(struct net_device *dev, struct iw_point *p)
{
	struct ieee_param *param;
	uint ret = 0;

	if (p->length < sizeof(struct ieee_param) || !p->pointer) {
		ret = -EINVAL;
		goto out;
	}

	param = (struct ieee_param *)rtw_malloc(p->length);
	if (param == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	if (copy_from_user(param, p->pointer, p->length)) {
		kfree(param);
		ret = -EFAULT;
		goto out;
	}

	switch (param->cmd) {
	case IEEE_CMD_SET_WPA_PARAM:
		ret = wpa_set_param(dev, param->u.wpa_param.name, param->u.wpa_param.value);
		break;

	case IEEE_CMD_SET_WPA_IE:
		ret =  rtw_set_wpa_ie((struct adapter *)rtw_netdev_priv(dev),
				      (char *)param->u.wpa_ie.data, (u16)param->u.wpa_ie.len);
		break;

	case IEEE_CMD_SET_ENCRYPTION:
		ret = wpa_set_encryption(dev, param, p->length);
		break;

	case IEEE_CMD_MLME:
		ret = wpa_mlme(dev, param->u.mlme.command, param->u.mlme.reason_code);
		break;

	default:
		DBG_88E("Unknown WPA supplicant request: %d\n", param->cmd);
		ret = -EOPNOTSUPP;
		break;
	}

	if (ret == 0 && copy_to_user(p->pointer, param, p->length))
		ret = -EFAULT;

	kfree(param);

out:

	return ret;
}

#ifdef CONFIG_88EU_AP_MODE
static u8 set_pairwise_key(struct adapter *padapter, struct sta_info *psta)
{
	struct cmd_obj *ph2c;
	struct set_stakey_parm	*psetstakey_para;
	struct cmd_priv	*pcmdpriv = &padapter->cmdpriv;
	u8 res = _SUCCESS;

	ph2c = kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
	if (!ph2c) {
		res = _FAIL;
		goto exit;
	}

	psetstakey_para = kzalloc(sizeof(struct set_stakey_parm), GFP_KERNEL);
	if (!psetstakey_para) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetstakey_para, _SetStaKey_CMD_);

	psetstakey_para->algorithm = (u8)psta->dot118021XPrivacy;

	memcpy(psetstakey_para->addr, psta->hwaddr, ETH_ALEN);

	memcpy(psetstakey_para->key, &psta->dot118021x_UncstKey, 16);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;
}

static int set_group_key(struct adapter *padapter, u8 *key, u8 alg, int keyid)
{
	u8 keylen;
	struct cmd_obj *pcmd;
	struct setkey_parm *psetkeyparm;
	struct cmd_priv	*pcmdpriv = &(padapter->cmdpriv);
	int res = _SUCCESS;

	DBG_88E("%s\n", __func__);

	pcmd = kzalloc(sizeof(struct	cmd_obj), GFP_KERNEL);
	if (!pcmd) {
		res = _FAIL;
		goto exit;
	}
	psetkeyparm = kzalloc(sizeof(struct setkey_parm), GFP_KERNEL);
	if (!psetkeyparm) {
		kfree(pcmd);
		res = _FAIL;
		goto exit;
	}

	memset(psetkeyparm, 0, sizeof(struct setkey_parm));

	psetkeyparm->keyid = (u8)keyid;

	psetkeyparm->algorithm = alg;

	psetkeyparm->set_tx = 1;

	switch (alg) {
	case _WEP40_:
		keylen = 5;
		break;
	case _WEP104_:
		keylen = 13;
		break;
	case _TKIP_:
	case _TKIP_WTMIC_:
	case _AES_:
	default:
		keylen = 16;
	}

	memcpy(&(psetkeyparm->key[0]), key, keylen);

	pcmd->cmdcode = _SetKey_CMD_;
	pcmd->parmbuf = (u8 *)psetkeyparm;
	pcmd->cmdsz =  (sizeof(struct setkey_parm));
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	INIT_LIST_HEAD(&pcmd->list);

	res = rtw_enqueue_cmd(pcmdpriv, pcmd);

exit:

	return res;
}

static int set_wep_key(struct adapter *padapter, u8 *key, u8 keylen, int keyid)
{
	u8 alg;

	switch (keylen) {
	case 5:
		alg = _WEP40_;
		break;
	case 13:
		alg = _WEP104_;
		break;
	default:
		alg = _NO_PRIVACY_;
	}

	return set_group_key(padapter, key, alg, keyid);
}

static int rtw_set_encryption(struct net_device *dev, struct ieee_param *param, u32 param_len)
{
	int ret = 0;
	u32 wep_key_idx, wep_key_len, wep_total_len;
	struct ndis_802_11_wep	 *pwep = NULL;
	struct sta_info *psta = NULL, *pbcmc_sta = NULL;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &(padapter->securitypriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_88E("%s\n", __func__);
	param->u.crypt.err = 0;
	param->u.crypt.alg[IEEE_CRYPT_ALG_NAME_LEN - 1] = '\0';
	if (param_len !=  sizeof(struct ieee_param) + param->u.crypt.key_len) {
		ret =  -EINVAL;
		goto exit;
	}
	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff) {
		if (param->u.crypt.idx >= WEP_KEYS) {
			ret = -EINVAL;
			goto exit;
		}
	} else {
		psta = rtw_get_stainfo(pstapriv, param->sta_addr);
		if (!psta) {
			DBG_88E("rtw_set_encryption(), sta has already been removed or never been added\n");
			goto exit;
		}
	}

	if (strcmp(param->u.crypt.alg, "none") == 0 && (psta == NULL)) {
		/* todo:clear default encryption keys */

		DBG_88E("clear default encryption keys, keyid =%d\n", param->u.crypt.idx);
		goto exit;
	}
	if (strcmp(param->u.crypt.alg, "WEP") == 0 && (psta == NULL)) {
		DBG_88E("r871x_set_encryption, crypt.alg = WEP\n");
		wep_key_idx = param->u.crypt.idx;
		wep_key_len = param->u.crypt.key_len;
		DBG_88E("r871x_set_encryption, wep_key_idx=%d, len=%d\n", wep_key_idx, wep_key_len);
		if ((wep_key_idx >= WEP_KEYS) || (wep_key_len <= 0)) {
			ret = -EINVAL;
			goto exit;
		}

		if (wep_key_len > 0) {
			wep_key_len = wep_key_len <= 5 ? 5 : 13;
			wep_total_len = wep_key_len + offsetof(struct ndis_802_11_wep, KeyMaterial);
			pwep = (struct ndis_802_11_wep *)rtw_malloc(wep_total_len);
			if (pwep == NULL) {
				DBG_88E(" r871x_set_encryption: pwep allocate fail !!!\n");
				goto exit;
			}

			memset(pwep, 0, wep_total_len);

			pwep->KeyLength = wep_key_len;
			pwep->Length = wep_total_len;
		}

		pwep->KeyIndex = wep_key_idx;

		memcpy(pwep->KeyMaterial,  param->u.crypt.key, pwep->KeyLength);

		if (param->u.crypt.set_tx) {
			DBG_88E("wep, set_tx = 1\n");

			psecuritypriv->ndisencryptstatus = Ndis802_11Encryption1Enabled;
			psecuritypriv->dot11PrivacyAlgrthm = _WEP40_;
			psecuritypriv->dot118021XGrpPrivacy = _WEP40_;

			if (pwep->KeyLength == 13) {
				psecuritypriv->dot11PrivacyAlgrthm = _WEP104_;
				psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
			}

			psecuritypriv->dot11PrivacyKeyIndex = wep_key_idx;

			memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), pwep->KeyMaterial, pwep->KeyLength);

			psecuritypriv->dot11DefKeylen[wep_key_idx] = pwep->KeyLength;

			set_wep_key(padapter, pwep->KeyMaterial, pwep->KeyLength, wep_key_idx);
		} else {
			DBG_88E("wep, set_tx = 0\n");

			/* don't update "psecuritypriv->dot11PrivacyAlgrthm" and */
			/* psecuritypriv->dot11PrivacyKeyIndex = keyid", but can rtw_set_key to cam */

		      memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), pwep->KeyMaterial, pwep->KeyLength);

			psecuritypriv->dot11DefKeylen[wep_key_idx] = pwep->KeyLength;

			set_wep_key(padapter, pwep->KeyMaterial, pwep->KeyLength, wep_key_idx);
		}

		goto exit;
	}

	if (!psta && check_fwstate(pmlmepriv, WIFI_AP_STATE)) { /*  group key */
		if (param->u.crypt.set_tx == 1) {
			if (strcmp(param->u.crypt.alg, "WEP") == 0) {
				DBG_88E("%s, set group_key, WEP\n", __func__);

				memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,
					    param->u.crypt.key, min_t(u16, param->u.crypt.key_len, 16));

				psecuritypriv->dot118021XGrpPrivacy = _WEP40_;
				if (param->u.crypt.key_len == 13)
						psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
			} else if (strcmp(param->u.crypt.alg, "TKIP") == 0) {
				DBG_88E("%s, set group_key, TKIP\n", __func__);
				psecuritypriv->dot118021XGrpPrivacy = _TKIP_;
				memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,
					    param->u.crypt.key, min_t(u16, param->u.crypt.key_len, 16));
				/* set mic key */
				memcpy(psecuritypriv->dot118021XGrptxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[16]), 8);
				memcpy(psecuritypriv->dot118021XGrprxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[24]), 8);

				psecuritypriv->busetkipkey = true;
			} else if (strcmp(param->u.crypt.alg, "CCMP") == 0) {
				DBG_88E("%s, set group_key, CCMP\n", __func__);
				psecuritypriv->dot118021XGrpPrivacy = _AES_;
				memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,
					    param->u.crypt.key, min_t(u16, param->u.crypt.key_len, 16));
			} else {
				DBG_88E("%s, set group_key, none\n", __func__);
				psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
			}
			psecuritypriv->dot118021XGrpKeyid = param->u.crypt.idx;
			psecuritypriv->binstallGrpkey = true;
			psecuritypriv->dot11PrivacyAlgrthm = psecuritypriv->dot118021XGrpPrivacy;/*  */
			set_group_key(padapter, param->u.crypt.key, psecuritypriv->dot118021XGrpPrivacy, param->u.crypt.idx);
			pbcmc_sta = rtw_get_bcmc_stainfo(padapter);
			if (pbcmc_sta) {
				pbcmc_sta->ieee8021x_blocked = false;
				pbcmc_sta->dot118021XPrivacy = psecuritypriv->dot118021XGrpPrivacy;/* rx will use bmc_sta's dot118021XPrivacy */
			}
		}
		goto exit;
	}

	if (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X && psta) { /*  psk/802_1x */
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
			if (param->u.crypt.set_tx == 1) {
				memcpy(psta->dot118021x_UncstKey.skey,  param->u.crypt.key, min_t(u16, param->u.crypt.key_len, 16));

				if (strcmp(param->u.crypt.alg, "WEP") == 0) {
					DBG_88E("%s, set pairwise key, WEP\n", __func__);

					psta->dot118021XPrivacy = _WEP40_;
					if (param->u.crypt.key_len == 13)
						psta->dot118021XPrivacy = _WEP104_;
				} else if (strcmp(param->u.crypt.alg, "TKIP") == 0) {
					DBG_88E("%s, set pairwise key, TKIP\n", __func__);

					psta->dot118021XPrivacy = _TKIP_;

					/* set mic key */
					memcpy(psta->dot11tkiptxmickey.skey, &(param->u.crypt.key[16]), 8);
					memcpy(psta->dot11tkiprxmickey.skey, &(param->u.crypt.key[24]), 8);

					psecuritypriv->busetkipkey = true;
				} else if (strcmp(param->u.crypt.alg, "CCMP") == 0) {
					DBG_88E("%s, set pairwise key, CCMP\n", __func__);

					psta->dot118021XPrivacy = _AES_;
				} else {
					DBG_88E("%s, set pairwise key, none\n", __func__);

					psta->dot118021XPrivacy = _NO_PRIVACY_;
				}

				set_pairwise_key(padapter, psta);

				psta->ieee8021x_blocked = false;
			} else { /* group key??? */
				if (strcmp(param->u.crypt.alg, "WEP") == 0) {
					memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,
						    param->u.crypt.key, min_t(u16, param->u.crypt.key_len, 16));
					psecuritypriv->dot118021XGrpPrivacy = _WEP40_;
					if (param->u.crypt.key_len == 13)
						psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
				} else if (strcmp(param->u.crypt.alg, "TKIP") == 0) {
					psecuritypriv->dot118021XGrpPrivacy = _TKIP_;

					memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,
						    param->u.crypt.key, min_t(u16, param->u.crypt.key_len, 16));

					/* set mic key */
					memcpy(psecuritypriv->dot118021XGrptxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[16]), 8);
					memcpy(psecuritypriv->dot118021XGrprxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[24]), 8);

					psecuritypriv->busetkipkey = true;
				} else if (strcmp(param->u.crypt.alg, "CCMP") == 0) {
					psecuritypriv->dot118021XGrpPrivacy = _AES_;

					memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,
						    param->u.crypt.key, min_t(u16, param->u.crypt.key_len, 16));
				} else {
					psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
				}

				psecuritypriv->dot118021XGrpKeyid = param->u.crypt.idx;

				psecuritypriv->binstallGrpkey = true;

				psecuritypriv->dot11PrivacyAlgrthm = psecuritypriv->dot118021XGrpPrivacy;/*  */

				set_group_key(padapter, param->u.crypt.key, psecuritypriv->dot118021XGrpPrivacy, param->u.crypt.idx);

				pbcmc_sta = rtw_get_bcmc_stainfo(padapter);
				if (pbcmc_sta) {
					pbcmc_sta->ieee8021x_blocked = false;
					pbcmc_sta->dot118021XPrivacy = psecuritypriv->dot118021XGrpPrivacy;/* rx will use bmc_sta's dot118021XPrivacy */
				}
			}
		}
	}

exit:

	kfree(pwep);

	return ret;
}

static int rtw_set_beacon(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;
	unsigned char *pbuf = param->u.bcn_ie.buf;

	DBG_88E("%s, len =%d\n", __func__, len);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	memcpy(&pstapriv->max_num_sta, param->u.bcn_ie.reserved, 2);

	if ((pstapriv->max_num_sta > NUM_STA) || (pstapriv->max_num_sta <= 0))
		pstapriv->max_num_sta = NUM_STA;

	if (rtw_check_beacon_data(padapter, pbuf,  (len-12-2)) == _SUCCESS)/*  12 = param header, 2:no packed */
		ret = 0;
	else
		ret = -EINVAL;

	return ret;
}

static int rtw_hostapd_sta_flush(struct net_device *dev)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

	DBG_88E("%s\n", __func__);

	flush_all_cam_entry(padapter);	/* clear CAM */

	return rtw_sta_flush(padapter);
}

static int rtw_add_sta(struct net_device *dev, struct ieee_param *param)
{
	int ret = 0;
	struct sta_info *psta = NULL;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_88E("rtw_add_sta(aid =%d) =%pM\n", param->u.add_sta.aid, (param->sta_addr));

	if (!check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)))
		return -EINVAL;

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff)
		return -EINVAL;

	psta = rtw_get_stainfo(pstapriv, param->sta_addr);
	if (psta) {
		int flags = param->u.add_sta.flags;

		psta->aid = param->u.add_sta.aid;/* aid = 1~2007 */

		memcpy(psta->bssrateset, param->u.add_sta.tx_supp_rates, 16);

		/* check wmm cap. */
		if (WLAN_STA_WME&flags)
			psta->qos_option = 1;
		else
			psta->qos_option = 0;

		if (pmlmepriv->qospriv.qos_option == 0)
			psta->qos_option = 0;

		/* chec 802.11n ht cap. */
		if (WLAN_STA_HT&flags) {
			psta->htpriv.ht_option = true;
			psta->qos_option = 1;
			memcpy((void *)&psta->htpriv.ht_cap, (void *)&param->u.add_sta.ht_cap, sizeof(struct rtw_ieee80211_ht_cap));
		} else {
			psta->htpriv.ht_option = false;
		}

		if (pmlmepriv->htpriv.ht_option == false)
			psta->htpriv.ht_option = false;

		update_sta_info_apmode(padapter, psta);
	} else {
		ret = -ENOMEM;
	}

	return ret;
}

static int rtw_del_sta(struct net_device *dev, struct ieee_param *param)
{
	int ret = 0;
	struct sta_info *psta = NULL;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;
	int updated = 0;

	DBG_88E("rtw_del_sta =%pM\n", (param->sta_addr));

	if (check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)) != true)
		return -EINVAL;

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff)
		return -EINVAL;

	psta = rtw_get_stainfo(pstapriv, param->sta_addr);
	if (psta) {
		spin_lock_bh(&pstapriv->asoc_list_lock);
		if (!list_empty(&psta->asoc_list)) {
			list_del_init(&psta->asoc_list);
			pstapriv->asoc_list_cnt--;
			updated = ap_free_sta(padapter, psta, true, WLAN_REASON_DEAUTH_LEAVING);
		}
		spin_unlock_bh(&pstapriv->asoc_list_lock);
		associated_clients_update(padapter, updated);
		psta = NULL;
	} else {
		DBG_88E("rtw_del_sta(), sta has already been removed or never been added\n");
	}

	return ret;
}

static int rtw_ioctl_get_sta_data(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret = 0;
	struct sta_info *psta = NULL;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct ieee_param_ex *param_ex = (struct ieee_param_ex *)param;
	struct sta_data *psta_data = (struct sta_data *)param_ex->data;

	DBG_88E("rtw_ioctl_get_sta_info, sta_addr: %pM\n", (param_ex->sta_addr));

	if (check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)) != true)
		return -EINVAL;

	if (param_ex->sta_addr[0] == 0xff && param_ex->sta_addr[1] == 0xff &&
	    param_ex->sta_addr[2] == 0xff && param_ex->sta_addr[3] == 0xff &&
	    param_ex->sta_addr[4] == 0xff && param_ex->sta_addr[5] == 0xff)
		return -EINVAL;

	psta = rtw_get_stainfo(pstapriv, param_ex->sta_addr);
	if (psta) {
		psta_data->aid = (u16)psta->aid;
		psta_data->capability = psta->capability;
		psta_data->flags = psta->flags;

/*
		nonerp_set : BIT(0)
		no_short_slot_time_set : BIT(1)
		no_short_preamble_set : BIT(2)
		no_ht_gf_set : BIT(3)
		no_ht_set : BIT(4)
		ht_20mhz_set : BIT(5)
*/

		psta_data->sta_set = ((psta->nonerp_set) |
				      (psta->no_short_slot_time_set << 1) |
				      (psta->no_short_preamble_set << 2) |
				      (psta->no_ht_gf_set << 3) |
				      (psta->no_ht_set << 4) |
				      (psta->ht_20mhz_set << 5));
		psta_data->tx_supp_rates_len =  psta->bssratelen;
		memcpy(psta_data->tx_supp_rates, psta->bssrateset, psta->bssratelen);
		memcpy(&psta_data->ht_cap, &psta->htpriv.ht_cap, sizeof(struct rtw_ieee80211_ht_cap));
		psta_data->rx_pkts = psta->sta_stats.rx_data_pkts;
		psta_data->rx_bytes = psta->sta_stats.rx_bytes;
		psta_data->rx_drops = psta->sta_stats.rx_drops;
		psta_data->tx_pkts = psta->sta_stats.tx_pkts;
		psta_data->tx_bytes = psta->sta_stats.tx_bytes;
		psta_data->tx_drops = psta->sta_stats.tx_drops;
	} else {
		ret = -1;
	}

	return ret;
}

static int rtw_get_sta_wpaie(struct net_device *dev, struct ieee_param *param)
{
	int ret = 0;
	struct sta_info *psta = NULL;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_88E("rtw_get_sta_wpaie, sta_addr: %pM\n", (param->sta_addr));

	if (check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)) != true)
		return -EINVAL;

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff)
		return -EINVAL;

	psta = rtw_get_stainfo(pstapriv, param->sta_addr);
	if (psta) {
		if (psta->wpa_ie[0] == WLAN_EID_RSN ||
		    psta->wpa_ie[0] == WLAN_EID_VENDOR_SPECIFIC) {
			int wpa_ie_len;
			int copy_len;

			wpa_ie_len = psta->wpa_ie[1];
			copy_len = min_t(int, wpa_ie_len + 2, sizeof(psta->wpa_ie));
			param->u.wpa_ie.len = copy_len;
			memcpy(param->u.wpa_ie.reserved, psta->wpa_ie, copy_len);
		} else {
			DBG_88E("sta's wpa_ie is NONE\n");
		}
	} else {
		ret = -1;
	}

	return ret;
}

static int rtw_set_wps_beacon(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret = 0;
	unsigned char wps_oui[4] = {0x0, 0x50, 0xf2, 0x04};
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	int ie_len;

	DBG_88E("%s, len =%d\n", __func__, len);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	ie_len = len-12-2;/*  12 = param header, 2:no packed */

	kfree(pmlmepriv->wps_beacon_ie);
	pmlmepriv->wps_beacon_ie = NULL;

	if (ie_len > 0) {
		pmlmepriv->wps_beacon_ie = rtw_malloc(ie_len);
		pmlmepriv->wps_beacon_ie_len = ie_len;
		if (pmlmepriv->wps_beacon_ie == NULL) {
			DBG_88E("%s()-%d: rtw_malloc() ERROR!\n", __func__, __LINE__);
			return -EINVAL;
		}

		memcpy(pmlmepriv->wps_beacon_ie, param->u.bcn_ie.buf, ie_len);

		update_beacon(padapter, _VENDOR_SPECIFIC_IE_, wps_oui, true);

		pmlmeext->bstart_bss = true;
	}

	return ret;
}

static int rtw_set_wps_probe_resp(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	int ie_len;

	DBG_88E("%s, len =%d\n", __func__, len);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	ie_len = len-12-2;/*  12 = param header, 2:no packed */

	kfree(pmlmepriv->wps_probe_resp_ie);
	pmlmepriv->wps_probe_resp_ie = NULL;

	if (ie_len > 0) {
		pmlmepriv->wps_probe_resp_ie = rtw_malloc(ie_len);
		pmlmepriv->wps_probe_resp_ie_len = ie_len;
		if (pmlmepriv->wps_probe_resp_ie == NULL) {
			DBG_88E("%s()-%d: rtw_malloc() ERROR!\n", __func__, __LINE__);
			return -EINVAL;
		}
		memcpy(pmlmepriv->wps_probe_resp_ie, param->u.bcn_ie.buf, ie_len);
	}

	return ret;
}

static int rtw_set_wps_assoc_resp(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	int ie_len;

	DBG_88E("%s, len =%d\n", __func__, len);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	ie_len = len-12-2;/*  12 = param header, 2:no packed */

	kfree(pmlmepriv->wps_assoc_resp_ie);
	pmlmepriv->wps_assoc_resp_ie = NULL;

	if (ie_len > 0) {
		pmlmepriv->wps_assoc_resp_ie = rtw_malloc(ie_len);
		pmlmepriv->wps_assoc_resp_ie_len = ie_len;
		if (pmlmepriv->wps_assoc_resp_ie == NULL) {
			DBG_88E("%s()-%d: rtw_malloc() ERROR!\n", __func__, __LINE__);
			return -EINVAL;
		}

		memcpy(pmlmepriv->wps_assoc_resp_ie, param->u.bcn_ie.buf, ie_len);
	}

	return ret;
}

static int rtw_set_hidden_ssid(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	u8 value;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	if (param->u.wpa_param.name != 0) /* dummy test... */
		DBG_88E("%s name(%u) != 0\n", __func__, param->u.wpa_param.name);
	value = param->u.wpa_param.value;

	/* use the same definition of hostapd's ignore_broadcast_ssid */
	if (value != 1 && value != 2)
		value = 0;
	DBG_88E("%s value(%u)\n", __func__, value);
	pmlmeinfo->hidden_ssid_mode = value;
	return ret;
}

static int rtw_ioctl_acl_remove_sta(struct net_device *dev, struct ieee_param *param, int len)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff)
		return -EINVAL;
	return rtw_acl_remove_sta(padapter, param->sta_addr);
}

static int rtw_ioctl_acl_add_sta(struct net_device *dev, struct ieee_param *param, int len)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff)
		return -EINVAL;
	return rtw_acl_add_sta(padapter, param->sta_addr);
}

static int rtw_ioctl_set_macaddr_acl(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	rtw_set_macaddr_acl(padapter, param->u.mlme.command);

	return ret;
}

static int rtw_hostapd_ioctl(struct net_device *dev, struct iw_point *p)
{
	struct ieee_param *param;
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

	/*
	* this function is expect to call in master mode, which allows no power saving
	* so, we just check hw_init_completed
	*/

	if (!padapter->hw_init_completed) {
		ret = -EPERM;
		goto out;
	}

	if (!p->pointer) {
		ret = -EINVAL;
		goto out;
	}

	param = (struct ieee_param *)rtw_malloc(p->length);
	if (param == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	if (copy_from_user(param, p->pointer, p->length)) {
		kfree(param);
		ret = -EFAULT;
		goto out;
	}

	switch (param->cmd) {
	case RTL871X_HOSTAPD_FLUSH:
		ret = rtw_hostapd_sta_flush(dev);
		break;
	case RTL871X_HOSTAPD_ADD_STA:
		ret = rtw_add_sta(dev, param);
		break;
	case RTL871X_HOSTAPD_REMOVE_STA:
		ret = rtw_del_sta(dev, param);
		break;
	case RTL871X_HOSTAPD_SET_BEACON:
		ret = rtw_set_beacon(dev, param, p->length);
		break;
	case RTL871X_SET_ENCRYPTION:
		ret = rtw_set_encryption(dev, param, p->length);
		break;
	case RTL871X_HOSTAPD_GET_WPAIE_STA:
		ret = rtw_get_sta_wpaie(dev, param);
		break;
	case RTL871X_HOSTAPD_SET_WPS_BEACON:
		ret = rtw_set_wps_beacon(dev, param, p->length);
		break;
	case RTL871X_HOSTAPD_SET_WPS_PROBE_RESP:
		ret = rtw_set_wps_probe_resp(dev, param, p->length);
		break;
	case RTL871X_HOSTAPD_SET_WPS_ASSOC_RESP:
		ret = rtw_set_wps_assoc_resp(dev, param, p->length);
		break;
	case RTL871X_HOSTAPD_SET_HIDDEN_SSID:
		ret = rtw_set_hidden_ssid(dev, param, p->length);
		break;
	case RTL871X_HOSTAPD_GET_INFO_STA:
		ret = rtw_ioctl_get_sta_data(dev, param, p->length);
		break;
	case RTL871X_HOSTAPD_SET_MACADDR_ACL:
		ret = rtw_ioctl_set_macaddr_acl(dev, param, p->length);
		break;
	case RTL871X_HOSTAPD_ACL_ADD_STA:
		ret = rtw_ioctl_acl_add_sta(dev, param, p->length);
		break;
	case RTL871X_HOSTAPD_ACL_REMOVE_STA:
		ret = rtw_ioctl_acl_remove_sta(dev, param, p->length);
		break;
	default:
		DBG_88E("Unknown hostapd request: %d\n", param->cmd);
		ret = -EOPNOTSUPP;
		break;
	}

	if (ret == 0 && copy_to_user(p->pointer, param, p->length))
		ret = -EFAULT;
	kfree(param);
out:
	return ret;
}
#endif

#include <rtw_android.h>
static int rtw_wx_set_priv(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *awrq,
				char *extra)
{
	int ret = 0;
	int len = 0;
	char *ext;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct iw_point *dwrq = (struct iw_point *)awrq;

	if (dwrq->length == 0)
		return -EFAULT;

	len = dwrq->length;
	ext = vmalloc(len);
	if (!ext)
		return -ENOMEM;

	if (copy_from_user(ext, dwrq->pointer, len)) {
		vfree(ext);
		return -EFAULT;
	}

	/* added for wps2.0 @20110524 */
	if (dwrq->flags == 0x8766 && len > 8) {
		u32 cp_sz;
		struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
		u8 *probereq_wpsie = ext;
		int probereq_wpsie_len = len;
		u8 wps_oui[4] = {0x0, 0x50, 0xf2, 0x04};

		if ((_VENDOR_SPECIFIC_IE_ == probereq_wpsie[0]) &&
		    (!memcmp(&probereq_wpsie[2], wps_oui, 4))) {
			cp_sz = min(probereq_wpsie_len, MAX_WPS_IE_LEN);

			pmlmepriv->wps_probe_req_ie_len = 0;
			kfree(pmlmepriv->wps_probe_req_ie);
			pmlmepriv->wps_probe_req_ie = NULL;

			pmlmepriv->wps_probe_req_ie = rtw_malloc(cp_sz);
			if (pmlmepriv->wps_probe_req_ie == NULL) {
				pr_info("%s()-%d: rtw_malloc() ERROR!\n", __func__, __LINE__);
				ret =  -EINVAL;
				goto FREE_EXT;
			}
			memcpy(pmlmepriv->wps_probe_req_ie, probereq_wpsie, cp_sz);
			pmlmepriv->wps_probe_req_ie_len = cp_sz;
		}
		goto FREE_EXT;
	}

	if (len >= WEXT_CSCAN_HEADER_SIZE &&
	    !memcmp(ext, WEXT_CSCAN_HEADER, WEXT_CSCAN_HEADER_SIZE)) {
		ret = rtw_wx_set_scan(dev, info, awrq, ext);
		goto FREE_EXT;
	}

FREE_EXT:

	vfree(ext);

	return ret;
}

static iw_handler rtw_handlers[] = {
	NULL,					/* SIOCSIWCOMMIT */
	rtw_wx_get_name,		/* SIOCGIWNAME */
	dummy,					/* SIOCSIWNWID */
	dummy,					/* SIOCGIWNWID */
	rtw_wx_set_freq,		/* SIOCSIWFREQ */
	rtw_wx_get_freq,		/* SIOCGIWFREQ */
	rtw_wx_set_mode,		/* SIOCSIWMODE */
	rtw_wx_get_mode,		/* SIOCGIWMODE */
	dummy,					/* SIOCSIWSENS */
	rtw_wx_get_sens,		/* SIOCGIWSENS */
	NULL,					/* SIOCSIWRANGE */
	rtw_wx_get_range,		/* SIOCGIWRANGE */
	rtw_wx_set_priv,		/* SIOCSIWPRIV */
	NULL,					/* SIOCGIWPRIV */
	NULL,					/* SIOCSIWSTATS */
	NULL,					/* SIOCGIWSTATS */
	dummy,					/* SIOCSIWSPY */
	dummy,					/* SIOCGIWSPY */
	NULL,					/* SIOCGIWTHRSPY */
	NULL,					/* SIOCWIWTHRSPY */
	rtw_wx_set_wap,		/* SIOCSIWAP */
	rtw_wx_get_wap,		/* SIOCGIWAP */
	rtw_wx_set_mlme,		/* request MLME operation; uses struct iw_mlme */
	dummy,					/* SIOCGIWAPLIST -- depricated */
	rtw_wx_set_scan,		/* SIOCSIWSCAN */
	rtw_wx_get_scan,		/* SIOCGIWSCAN */
	rtw_wx_set_essid,		/* SIOCSIWESSID */
	rtw_wx_get_essid,		/* SIOCGIWESSID */
	dummy,					/* SIOCSIWNICKN */
	rtw_wx_get_nick,		/* SIOCGIWNICKN */
	NULL,					/* -- hole -- */
	NULL,					/* -- hole -- */
	rtw_wx_set_rate,		/* SIOCSIWRATE */
	rtw_wx_get_rate,		/* SIOCGIWRATE */
	rtw_wx_set_rts,			/* SIOCSIWRTS */
	rtw_wx_get_rts,			/* SIOCGIWRTS */
	rtw_wx_set_frag,		/* SIOCSIWFRAG */
	rtw_wx_get_frag,		/* SIOCGIWFRAG */
	dummy,					/* SIOCSIWTXPOW */
	dummy,					/* SIOCGIWTXPOW */
	dummy,					/* SIOCSIWRETRY */
	rtw_wx_get_retry,		/* SIOCGIWRETRY */
	rtw_wx_set_enc,			/* SIOCSIWENCODE */
	rtw_wx_get_enc,			/* SIOCGIWENCODE */
	dummy,					/* SIOCSIWPOWER */
	rtw_wx_get_power,		/* SIOCGIWPOWER */
	NULL,					/*---hole---*/
	NULL,					/*---hole---*/
	rtw_wx_set_gen_ie,		/* SIOCSIWGENIE */
	NULL,					/* SIOCGWGENIE */
	rtw_wx_set_auth,		/* SIOCSIWAUTH */
	NULL,					/* SIOCGIWAUTH */
	rtw_wx_set_enc_ext,		/* SIOCSIWENCODEEXT */
	NULL,					/* SIOCGIWENCODEEXT */
	rtw_wx_set_pmkid,		/* SIOCSIWPMKSA */
	NULL,					/*---hole---*/
};

static struct iw_statistics *rtw_get_wireless_stats(struct net_device *dev)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct iw_statistics *piwstats = &padapter->iwstats;
	int tmp_level = 0;
	int tmp_qual = 0;
	int tmp_noise = 0;

	if (!check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {
		piwstats->qual.qual = 0;
		piwstats->qual.level = 0;
		piwstats->qual.noise = 0;
	} else {
		tmp_level = padapter->recvpriv.signal_strength;
		tmp_qual = padapter->recvpriv.signal_qual;
		tmp_noise = padapter->recvpriv.noise;

		piwstats->qual.level = tmp_level;
		piwstats->qual.qual = tmp_qual;
		piwstats->qual.noise = tmp_noise;
	}
	piwstats->qual.updated = IW_QUAL_ALL_UPDATED;/* IW_QUAL_DBM; */
	return &padapter->iwstats;
}

struct iw_handler_def rtw_handlers_def = {
	.standard = rtw_handlers,
	.num_standard = ARRAY_SIZE(rtw_handlers),
	.get_wireless_stats = rtw_get_wireless_stats,
};

int rtw_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct iwreq *wrq = (struct iwreq *)rq;
	int ret = 0;

	switch (cmd) {
	case RTL_IOCTL_WPA_SUPPLICANT:
		ret = wpa_supplicant_ioctl(dev, &wrq->u.data);
		break;
#ifdef CONFIG_88EU_AP_MODE
	case RTL_IOCTL_HOSTAPD:
		ret = rtw_hostapd_ioctl(dev, &wrq->u.data);
		break;
#endif /*  CONFIG_88EU_AP_MODE */
	case (SIOCDEVPRIVATE+1):
		ret = rtw_android_priv_cmd(dev, rq, cmd);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	return ret;
}
