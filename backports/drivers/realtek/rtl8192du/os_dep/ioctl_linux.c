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
#define  _IOCTL_LINUX_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <wlan_bssdef.h>
#include <rtw_debug.h>
#include <wifi.h>
#include <rtw_mlme.h>
#include <rtw_mlme_ext.h>
#include <rtw_ioctl.h>
#include <rtw_ioctl_set.h>
#include <rtw_mp_ioctl.h>
#include <usb_osintf.h>
#include <usb_ops.h>
#include <linux/vmalloc.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 27))
#define  iwe_stream_add_event(a, b, c, d, e)  iwe_stream_add_event(b, c, d, e)
#define  iwe_stream_add_point(a, b, c, d, e)  iwe_stream_add_point(b, c, d, e)
#endif

#define RTL_IOCTL_WPA_SUPPLICANT	(SIOCIWFIRSTPRIV+30)

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

static u32 rtw_rates[] = {
	1000000, 2000000, 5500000, 11000000,
	6000000, 9000000, 12000000, 18000000,
	24000000, 36000000, 48000000, 54000000
};

static const char * const iw_operation_mode[] = {
	"Auto", "Ad-Hoc", "Managed",  "Master", "Repeater",
	"Secondary", "Monitor"
};

static int hex2num_i(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int hex2byte_i(const char *hex)
{
	int a, b;
	a = hex2num_i(*hex++);
	if (a < 0)
		return -1;
	b = hex2num_i(*hex++);
	if (b < 0)
		return -1;
	return (a << 4) | b;
}

/**
 * hwaddr_aton - Convert ASCII string to MAC address
 * @txt: MAC address as a string (e.g., "00:11:22:33:44:55")
 * @addr: Buffer for the MAC address (ETH_ALEN = 6 bytes)
 * Returns: 0 on success, -1 on failure (e.g., string not a MAC address)
 */
static int hwaddr_aton_i(const char *txt, u8 *addr)
{
	int i;

	for (i = 0; i < 6; i++) {
		int a, b;

		a = hex2num_i(*txt++);
		if (a < 0)
			return -1;
		b = hex2num_i(*txt++);
		if (b < 0)
			return -1;
		*addr++ = (a << 4) | b;
		if (i < 5 && *txt++ != ':')
			return -1;
	}

	return 0;
}

static void indicate_wx_custom_event(struct rtw_adapter *padapter, char *msg)
{
}

static void request_wps_pbc_event(struct rtw_adapter *padapter)
{
}

void indicate_wx_scan_complete_event(struct rtw_adapter *padapter)
{
}

void rtw_indicate_wx_assoc_event(struct rtw_adapter *padapter)
{
}

void rtw_indicate_wx_disassoc_event(struct rtw_adapter *padapter)
{
}

static char *translate_scan(struct rtw_adapter *padapter,
			    struct iw_request_info *info,
			    struct wlan_network *pnetwork,
			    char *start, char *stop)
{
	struct iw_event iwe;
	__le16 cap;
	u16 cpu_cap;
	u32 ht_ielen = 0;
	char custom[MAX_CUSTOM_LEN];
	char *p;
	u16 max_rate = 0, rate, ht_cap = false;
	u32 i = 0;
	char	*current_val;
	long rssi;
	u8 bw_40MHz = 0, short_GI = 0;
	u16 mcs_rate = 0;
	struct registry_priv *pregpriv = &padapter->registrypriv;

	/*  AP MAC address  */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;

	memcpy(iwe.u.ap_addr.sa_data, pnetwork->network.MacAddress, ETH_ALEN);
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_ADDR_LEN);

	/* Add the ESSID */
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.flags = 1;
	iwe.u.data.length = min((u16)pnetwork->network.Ssid.SsidLength, (u16)32);
	start = iwe_stream_add_point(info, start, stop, &iwe, pnetwork->network.Ssid.Ssid);

	/* parsing HT_CAP_IE */
		p = rtw_get_ie(&pnetwork->network.IEs[12], _HT_CAPABILITY_IE_, &ht_ielen, pnetwork->network.IELength-12);

	if (p && ht_ielen > 0)
	{
		struct rtw_ieee80211_ht_cap *pht_capie;
		ht_cap = true;
		pht_capie = (struct rtw_ieee80211_ht_cap *)(p+2);
		memcpy(&mcs_rate , pht_capie->supp_mcs_set, 2);
		bw_40MHz = (pht_capie->cap_info&IEEE80211_HT_CAP_SUP_WIDTH) ? 1 : 0;
		short_GI = (pht_capie->cap_info&(IEEE80211_HT_CAP_SGI_20|IEEE80211_HT_CAP_SGI_40)) ? 1 : 0;
	}

	/* Add the protocol name */
	iwe.cmd = SIOCGIWNAME;
	if (rtw_is_cckratesonly_included((u8 *)&pnetwork->network.SupportedRates)) {
		if (ht_cap == true)
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bn");
		else
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11b");
	} else if (rtw_is_cckrates_included((u8 *)&pnetwork->network.SupportedRates)) {
		if (ht_cap == true)
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bgn");
		else
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bg");
	} else {
		if (pnetwork->network.Configuration.DSConfig > 14) {
			if (ht_cap == true)
				snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11an");
			else
				snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11a");
		} else {
			if (ht_cap == true)
				snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11gn");
			else
				snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11g");
		}
	}

	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_CHAR_LEN);

	/* Add mode */
	iwe.cmd = SIOCGIWMODE;
	memcpy((u8 *)&cap, rtw_get_capability_from_ie(pnetwork->network.IEs), 2);

	cpu_cap = le16_to_cpu(cap);

	if (cpu_cap & (WLAN_CAPABILITY_IBSS |WLAN_CAPABILITY_BSS)) {
		if (cpu_cap & WLAN_CAPABILITY_BSS)
			iwe.u.mode = IW_MODE_MASTER;
		else
			iwe.u.mode = IW_MODE_ADHOC;

		start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_UINT_LEN);
	}

	if (pnetwork->network.Configuration.DSConfig<1 /*|| pnetwork->network.Configuration.DSConfig>14*/)
		pnetwork->network.Configuration.DSConfig = 1;

	 /* Add frequency/channel */
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = rtw_ch2freq(pnetwork->network.Configuration.DSConfig) * 100000;
	iwe.u.freq.e = 1;
	iwe.u.freq.i = pnetwork->network.Configuration.DSConfig;
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_FREQ_LEN);

	/* Add encryption capability */
	iwe.cmd = SIOCGIWENCODE;
	if (cpu_cap & WLAN_CAPABILITY_PRIVACY)
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

	if (ht_cap == true) {
		if (mcs_rate&0x8000) { /* MCS15 */
			max_rate = (bw_40MHz) ? ((short_GI)?300:270):((short_GI)?144:130);

		} else if (mcs_rate&0x0080) { /* MCS7 */
			max_rate = (bw_40MHz) ? ((short_GI)?150:135):((short_GI)?72:65);
		} else { /* default MCS7 */
			DBG_8192D("wx_get_scan, mcs_rate_bitmap = 0x%x\n", mcs_rate);
			max_rate = (bw_40MHz) ? ((short_GI)?150:135):((short_GI)?72:65);
		}

		max_rate = max_rate*2;/* Mbps/2; */
	}

	iwe.cmd = SIOCGIWRATE;
	iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;
	iwe.u.bitrate.value = max_rate * 500000;
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_PARAM_LEN);

	/* parsing WPA/WPA2 IE */
	{
		u8 buf[MAX_WPA_IE_LEN];
		u8 wpa_ie[255], rsn_ie[255];
		u16 wpa_len = 0, rsn_len = 0;
		u8 *p;

		rtw_get_sec_ie(pnetwork->network.IEs , pnetwork->network.IELength, rsn_ie,&rsn_len, wpa_ie,&wpa_len);
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_wx_get_scan: ssid =%s\n", pnetwork->network.Ssid.Ssid));
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_wx_get_scan: wpa_len =%d rsn_len =%d\n", wpa_len, rsn_len));

		if (wpa_len > 0) {
			p = buf;
			memset(buf, 0, MAX_WPA_IE_LEN);
			p += sprintf(p, "wpa_ie =");
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
			p += sprintf(p, "rsn_ie =");
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

	{ /* parsing WPS IE */
		uint cnt = 0, total_ielen;
		u8 *wpsie_ptr = NULL;
		uint wps_ielen = 0;

		u8 *ie_ptr = pnetwork->network.IEs +_FIXED_IE_LENGTH_;
		total_ielen = pnetwork->network.IELength - _FIXED_IE_LENGTH_;

		while (cnt < total_ielen) {
			if (rtw_is_wps_ie(&ie_ptr[cnt], &wps_ielen) && (wps_ielen>2)) {
				wpsie_ptr = &ie_ptr[cnt];
				iwe.cmd = IWEVGENIE;
				iwe.u.data.length = (u16)wps_ielen;
				start = iwe_stream_add_point(info, start, stop, &iwe, wpsie_ptr);
			}
			cnt+= ie_ptr[cnt+1]+2; /* goto next */
		}
	}

{
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	u8 ss, sq;

	/* Add quality statistics */
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.updated = IW_QUAL_QUAL_UPDATED | IW_QUAL_LEVEL_UPDATED | IW_QUAL_NOISE_INVALID
	#ifdef CONFIG_SIGNAL_DISPLAY_DBM
		| IW_QUAL_DBM
	#endif
	;

	if (check_fwstate(pmlmepriv, _FW_LINKED) == true &&
	    is_same_network(&pmlmepriv->cur_network.network, &pnetwork->network)) {
		ss = padapter->recvpriv.signal_strength;
		sq = padapter->recvpriv.signal_qual;
	} else {
		ss = pnetwork->network.PhyInfo.SignalStrength;
		sq = pnetwork->network.PhyInfo.SignalQuality;
	}

	#ifdef CONFIG_SIGNAL_DISPLAY_DBM
	iwe.u.qual.level = (u8) translate_percentage_to_dbm(ss);/* dbm */
	#else
	iwe.u.qual.level = (u8)ss;/*  */
	#endif

	iwe.u.qual.qual = (u8)sq;   /*  signal quality */

	iwe.u.qual.noise = 0; /*  noise level */

	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_QUAL_LEN);
}

	return start;
}

static int wpa_set_auth_algs(struct net_device *dev, u32 value)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *) rtw_netdev_priv(dev);
	int ret = 0;

	if ((value & AUTH_ALG_SHARED_KEY)&&(value & AUTH_ALG_OPEN_SYSTEM)) {
		DBG_8192D("wpa_set_auth_algs, AUTH_ALG_SHARED_KEY and  AUTH_ALG_OPEN_SYSTEM [value:0x%x]\n", value);
		padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION1ENABLED;
		padapter->securitypriv.ndisauthtype = NDIS802_11AUTHMODESWITCH;
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;
	} else if (value & AUTH_ALG_SHARED_KEY) {
		DBG_8192D("wpa_set_auth_algs, AUTH_ALG_SHARED_KEY  [value:0x%x]\n", value);
		padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION1ENABLED;

		padapter->securitypriv.ndisauthtype = NDIS802_11AUTHMODESHARED;
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Shared;
	} else if (value & AUTH_ALG_OPEN_SYSTEM) {
		DBG_8192D("wpa_set_auth_algs, AUTH_ALG_OPEN_SYSTEM\n");
		if (padapter->securitypriv.ndisauthtype < NDIS802_11AUTHMODEWPAPSK) {
			padapter->securitypriv.ndisauthtype = NDIS802_11AUTHMODEOPEN;
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
		}
	} else if (value & AUTH_ALG_LEAP) {
		DBG_8192D("wpa_set_auth_algs, AUTH_ALG_LEAP\n");
	} else {
		DBG_8192D("wpa_set_auth_algs, error!\n");
		ret = -EINVAL;
	}

	return ret;
}

static int wpa_set_encryption(struct net_device *dev, struct ieee_param *param, u32 param_len)
{
	int ret = 0;
	u32 wep_key_idx, wep_key_len, wep_total_len;
	struct ndis_802_11_wep *pwep = NULL;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	param->u.crypt.err = 0;
	param->u.crypt.alg[IEEE_CRYPT_ALG_NAME_LEN - 1] = '\0';

	if (param_len < (u32) ((u8 *) param->u.crypt.key - (u8 *) param) +
	    param->u.crypt.key_len) {
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
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_,
			 ("wpa_set_encryption, crypt.alg = WEP\n"));
		DBG_8192D("wpa_set_encryption, crypt.alg = WEP\n");

		padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION1ENABLED;
		padapter->securitypriv.dot11PrivacyAlgrthm = _WEP40_;
		padapter->securitypriv.dot118021XGrpPrivacy = _WEP40_;

		wep_key_idx = param->u.crypt.idx;
		wep_key_len = param->u.crypt.key_len;

		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
			 ("(1)wep_key_idx =%d\n", wep_key_idx));
		DBG_8192D("(1)wep_key_idx =%d\n", wep_key_idx);

		if (wep_key_idx > WEP_KEYS)
			return -EINVAL;

		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
			 ("(2)wep_key_idx =%d\n", wep_key_idx));

		if (wep_key_len > 0) {
			wep_key_len = wep_key_len <= 5 ? 5 : 13;
			wep_total_len = wep_key_len + FIELD_OFFSET(struct ndis_802_11_wep, KeyMaterial);
			pwep = (struct ndis_802_11_wep *) kmalloc(wep_total_len, GFP_KERNEL);
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
			DBG_8192D("wep, set_tx = 1\n");

			if (rtw_set_802_11_add_wep(padapter, pwep) == (u8)_FAIL)
				ret = -EOPNOTSUPP ;
		} else {
			DBG_8192D("wep, set_tx = 0\n");

			if (wep_key_idx >= WEP_KEYS) {
				ret = -EOPNOTSUPP ;
				goto exit;
			}

			memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), pwep->KeyMaterial, pwep->KeyLength);
			psecuritypriv->dot11DefKeylen[wep_key_idx]= pwep->KeyLength;
			rtw_set_key(padapter, psecuritypriv, wep_key_idx, 0);
		}

		goto exit;
	}

	if (padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) { /*  802_1x */
		struct sta_info * psta,*pbcmc_sta;
		struct sta_priv * pstapriv = &padapter->stapriv;

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_MP_STATE)) { /* sta mode */
			psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
			if (psta) {
				/* Jeff: don't disable ieee8021x_blocked while clearing key */
				if (strcmp(param->u.crypt.alg, "none") != 0)
					psta->ieee8021x_blocked = false;

				if ((padapter->securitypriv.ndisencryptstatus == NDIS802_11ENCRYPTION2ENABLED)||
				    (padapter->securitypriv.ndisencryptstatus ==  NDIS802_11ENCRYPTION3ENABLED))
					psta->dot118021XPrivacy = padapter->securitypriv.dot11PrivacyAlgrthm;

				if (param->u.crypt.set_tx == 1)/* pairwise key */ {
					memcpy(psta->dot118021x_UncstKey.skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));

					if (strcmp(param->u.crypt.alg, "TKIP") == 0) { /* set mic key */
						memcpy(psta->dot11tkiptxmickey.skey, &(param->u.crypt.key[16]), 8);
						memcpy(psta->dot11tkiprxmickey.skey, &(param->u.crypt.key[24]), 8);

						padapter->securitypriv.busetkipkey = false;
					}

					DBG_8192D("\n ~~~~stastakey:unicastkey\n");

					rtw_setstakey_cmd(padapter, (unsigned char *)psta, true);
				} else { /* group key */
					memcpy(padapter->securitypriv.dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
					memcpy(padapter->securitypriv.dot118021XGrptxmickey[param->u.crypt.idx].skey,&(param->u.crypt.key[16]), 8);
					memcpy(padapter->securitypriv.dot118021XGrprxmickey[param->u.crypt.idx].skey,&(param->u.crypt.key[24]), 8);
					padapter->securitypriv.binstallGrpkey = true;
					DBG_8192D("\n ~~~~stastakey:groupkey\n");

					padapter->securitypriv.dot118021XGrpKeyid = param->u.crypt.idx;

					rtw_set_key(padapter,&padapter->securitypriv, param->u.crypt.idx, 1);
				}
			}

			pbcmc_sta = rtw_get_bcmc_stainfo(padapter);
			if (pbcmc_sta) {
				/* Jeff: don't disable ieee8021x_blocked while clearing key */
				if (strcmp(param->u.crypt.alg, "none") != 0)
					pbcmc_sta->ieee8021x_blocked = false;

				if ((padapter->securitypriv.ndisencryptstatus == NDIS802_11ENCRYPTION2ENABLED)||
				    (padapter->securitypriv.ndisencryptstatus ==  NDIS802_11ENCRYPTION3ENABLED))
					pbcmc_sta->dot118021XPrivacy = padapter->securitypriv.dot11PrivacyAlgrthm;
			}
		}
	}

exit:

	kfree(pwep);

	return ret;
}

static int rtw_set_wpa_ie(struct rtw_adapter *padapter, char *pie, unsigned short ielen)
{
	u8 *buf = NULL;
	u32 left;
	int group_cipher = 0, pairwise_cipher = 0;
	int ret = 0;
	u8	null_addr[]= {0, 0, 0, 0, 0, 0};
	int i;

	if ((ielen > MAX_WPA_IE_LEN) || (pie == NULL)) {
		_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);
		if (pie == NULL)
			return ret;
		else
			return -EINVAL;
	}

	if (ielen) {
		buf = kzalloc(ielen, GFP_KERNEL);
		if (buf == NULL) {
			ret =  -ENOMEM;
			goto exit;
		}

		memcpy(buf, pie , ielen);

		/* dump */
		DBG_8192D("\n wpa_ie(length:%d):\n", ielen);
		for (i = 0;i<ielen;i = i+8)
			DBG_8192D("0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x\n",
				  buf[i], buf[i+1], buf[i+2], buf[i+3],
				  buf[i+4], buf[i+5], buf[i+6], buf[i+7]);

		if (ielen < RSN_HEADER_LEN) {
			RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_,
				 ("Ie len too short %d\n", ielen));
			ret  = -1;
			goto exit;
		}

		if (rtw_parse_wpa_ie(buf, ielen, &group_cipher, &pairwise_cipher) == _SUCCESS) {
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
			padapter->securitypriv.ndisauthtype = NDIS802_11AUTHMODEWPAPSK;
			memcpy(padapter->securitypriv.supplicant_ie, &buf[0], ielen);
		}

		if (rtw_parse_wpa2_ie(buf, ielen, &group_cipher, &pairwise_cipher) == _SUCCESS) {
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
			padapter->securitypriv.ndisauthtype = NDIS802_11AUTHMODEWPA2PSK;
			memcpy(padapter->securitypriv.supplicant_ie, &buf[0], ielen);
		}

		if (group_cipher == 0)
			group_cipher = WPA_CIPHER_NONE;
		if (pairwise_cipher == 0)
			pairwise_cipher = WPA_CIPHER_NONE;

		switch (group_cipher) {
		case WPA_CIPHER_NONE:
			padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
			padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION_DISABLED;
			break;
		case WPA_CIPHER_WEP40:
			padapter->securitypriv.dot118021XGrpPrivacy = _WEP40_;
			padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION1ENABLED;
			break;
		case WPA_CIPHER_TKIP:
			padapter->securitypriv.dot118021XGrpPrivacy = _TKIP_;
			padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION2ENABLED;
			break;
		case WPA_CIPHER_CCMP:
			padapter->securitypriv.dot118021XGrpPrivacy = _AES_;
			padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION3ENABLED;
			break;
		case WPA_CIPHER_WEP104:
			padapter->securitypriv.dot118021XGrpPrivacy = _WEP104_;
			padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION1ENABLED;
			break;
		}

		switch (pairwise_cipher) {
		case WPA_CIPHER_NONE:
			padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
			padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION_DISABLED;
			break;
		case WPA_CIPHER_WEP40:
			padapter->securitypriv.dot11PrivacyAlgrthm = _WEP40_;
			padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION1ENABLED;
			break;
		case WPA_CIPHER_TKIP:
			padapter->securitypriv.dot11PrivacyAlgrthm = _TKIP_;
			padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION2ENABLED;
			break;
		case WPA_CIPHER_CCMP:
			padapter->securitypriv.dot11PrivacyAlgrthm = _AES_;
			padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION3ENABLED;
			break;
		case WPA_CIPHER_WEP104:
			padapter->securitypriv.dot11PrivacyAlgrthm = _WEP104_;
			padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION1ENABLED;
			break;
		}

		_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);
		{/* set wps_ie */
			u16 cnt = 0;
			u8 eid, wps_oui[4]={0x0, 0x50, 0xf2, 0x04};

			while (cnt < ielen) {
				eid = buf[cnt];

				if ((eid == _VENDOR_SPECIFIC_IE_) && (!memcmp(&buf[cnt+2], wps_oui, 4))) {
					DBG_8192D("SET WPS_IE\n");

					padapter->securitypriv.wps_ie_len = ((buf[cnt+1]+2) < (MAX_WPA_IE_LEN<<2)) ? (buf[cnt+1]+2):(MAX_WPA_IE_LEN<<2);

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

	/* TKIP and AES disallow multicast packets until installing group key */
	if (padapter->securitypriv.dot11PrivacyAlgrthm == _TKIP_ ||
	    padapter->securitypriv.dot11PrivacyAlgrthm == _TKIP_WTMIC_ ||
	    padapter->securitypriv.dot11PrivacyAlgrthm == _AES_)
		/* WPS open need to enable multicast */
		rtw_hal_set_hwreg(padapter, HW_VAR_OFF_RCR_AM, null_addr);

	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
		 ("rtw_set_wpa_ie: pairwise_cipher = 0x%08x padapter->securitypriv.ndisencryptstatus =%d padapter->securitypriv.ndisauthtype =%d\n",
		  pairwise_cipher, padapter->securitypriv.ndisencryptstatus, padapter->securitypriv.ndisauthtype));

exit:

	kfree(buf);

	return ret;
}

static int rtw_wx_get_name(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	u16 cap;
	u32 ht_ielen = 0;
	char *p;
	u8 ht_cap = false;
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct wlan_bssid_ex *pcur_bss = &pmlmepriv->cur_network.network;
	unsigned char prates[NDIS_802_11_LENGTH_RATES_EX] = {0};

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("cmd_code =%x\n", info->cmd));

	if (check_fwstate(pmlmepriv, _FW_LINKED|WIFI_ADHOC_MASTER_STATE)) {
		/* parsing HT_CAP_IE */
		p = rtw_get_ie(&pcur_bss->IEs[12], _HT_CAPABILITY_IE_, &ht_ielen, pcur_bss->IELength-12);
		if (p && ht_ielen > 0)
			ht_cap = true;

		memcpy(prates, pcur_bss->SupportedRates, NDIS_802_11_LENGTH_RATES_EX);

		if (rtw_is_cckratesonly_included((u8 *)prates)) {
			if (ht_cap)
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bn");
			else
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11b");
		} else if ((rtw_is_cckrates_included((u8 *)prates))) {
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
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct wlan_bssid_ex *pcur_bss = &pmlmepriv->cur_network.network;

	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
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
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	enum NDIS_802_11_NETWORK_INFRASTRUCTURE networkType ;
	int ret = 0;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct __queue *queue = &pmlmepriv->scanned_queue;

	if (_FAIL == rtw_pwr_wakeup(padapter)) {
		ret = -EPERM;
		goto exit;
	}

	if (padapter->hw_init_completed == false) {
		ret = -EPERM;
		goto exit;
	}

	switch (wrqu->mode) {
	case IW_MODE_AUTO:
		networkType = NDIS802_11AUTOUNK;
		DBG_8192D("set_mode = IW_MODE_AUTO\n");
		break;
	case IW_MODE_ADHOC:
		networkType = NDIS802_11IBSS;
		DBG_8192D("set_mode = IW_MODE_ADHOC\n");
		break;
	case IW_MODE_MASTER:
		networkType = NDIS802_11APMODE;
		DBG_8192D("set_mode = IW_MODE_MASTER\n");
		break;
	case IW_MODE_INFRA:
		networkType = NDIS802_11INFRA;
		DBG_8192D("set_mode = IW_MODE_INFRA\n");
		break;
	default :
		ret = -EINVAL;;
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_,
			 ("\n Mode: %s is not supported\n",
			 iw_operation_mode[wrqu->mode]));
		goto exit;
	}

	spin_lock_bh(&pmlmepriv->lock);
	spin_lock_bh(&queue->lock);
	if (rtw_set_802_11_infrastructure_mode(padapter, networkType) == false) {

		ret = -EPERM;
		spin_unlock_bh(&queue->lock);
		spin_unlock_bh(&pmlmepriv->lock);
		goto exit;

	}
	spin_unlock_bh(&queue->lock);
	spin_unlock_bh(&pmlmepriv->lock);
	rtw_setopmode_cmd(padapter, networkType);

exit:

	return ret;
}

static int rtw_wx_get_mode(struct net_device *dev, struct iw_request_info *a,
			   union iwreq_data *wrqu, char *b)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, (" rtw_wx_get_mode\n"));

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true)
		wrqu->mode = IW_MODE_INFRA;
	else if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) ||
		 (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true))
		wrqu->mode = IW_MODE_ADHOC;
	else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true)
		wrqu->mode = IW_MODE_MASTER;
	else
		wrqu->mode = IW_MODE_AUTO;

	return 0;
}

static int rtw_wx_set_pmkid(struct net_device *dev,
	                    struct iw_request_info *a,
			    union iwreq_data *wrqu, char *extra)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv  *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct iw_pmksa*  pPMK = (struct iw_pmksa*) extra;
	u8          j, blInserted = false;
	int         intReturn = false;
	u8     strZeroMacAddress[ETH_ALEN] = { 0x00 };
	u8     strIssueBssid[ETH_ALEN] = { 0x00 };

	memcpy(strIssueBssid, pPMK->bssid.sa_data, ETH_ALEN);
	if (pPMK->cmd == IW_PMKSA_ADD) {
		DBG_8192D("[rtw_wx_set_pmkid] IW_PMKSA_ADD!\n");
		if (!memcmp(strIssueBssid, strZeroMacAddress, ETH_ALEN))
			return(intReturn);
		else
			intReturn = true;
		blInserted = false;

		/* overwrite PMKID */
		for (j = 0; j < NUM_PMKID_CACHE; j++) {
			if (!memcmp(psecuritypriv->PMKIDList[j].Bssid, strIssueBssid, ETH_ALEN)) {
				/*  BSSID is matched, the same AP => rewrite with new PMKID. */
				DBG_8192D("[rtw_wx_set_pmkid] BSSID exists in the PMKList.\n");

				memcpy(psecuritypriv->PMKIDList[j].PMKID, pPMK->pmkid, IW_PMKID_LEN);
				psecuritypriv->PMKIDList[j].bUsed = true;
				psecuritypriv->PMKIDIndex = j+1;
				blInserted = true;
				break;
			}
	        }

	        if (!blInserted) {
			/*  Find a new entry */
			DBG_8192D("[rtw_wx_set_pmkid] Use the new entry index = %d for this PMKID.\n",
				  psecuritypriv->PMKIDIndex);

			memcpy(psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].Bssid, strIssueBssid, ETH_ALEN);
			memcpy(psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].PMKID, pPMK->pmkid, IW_PMKID_LEN);

			psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].bUsed = true;
			psecuritypriv->PMKIDIndex++ ;
			if (psecuritypriv->PMKIDIndex == 16)
				psecuritypriv->PMKIDIndex = 0;
		}
	} else if (pPMK->cmd == IW_PMKSA_REMOVE) {
		DBG_8192D("[rtw_wx_set_pmkid] IW_PMKSA_REMOVE!\n");
		intReturn = true;
		for (j = 0; j < NUM_PMKID_CACHE; j++) {
			if (!memcmp(psecuritypriv->PMKIDList[j].Bssid, strIssueBssid, ETH_ALEN)) {
				/*  BSSID is matched, the same AP => Remove this PMKID information and reset it. */
				memset(psecuritypriv->PMKIDList[j].Bssid, 0x00, ETH_ALEN);
				psecuritypriv->PMKIDList[j].bUsed = false;
				break;
			}
	        }
	} else if (pPMK->cmd == IW_PMKSA_FLUSH) {
		DBG_8192D("[rtw_wx_set_pmkid] IW_PMKSA_FLUSH!\n");
		memset(&psecuritypriv->PMKIDList[0], 0x00, sizeof(struct RT_PMKID_LIST) * NUM_PMKID_CACHE);
		psecuritypriv->PMKIDIndex = 0;
		intReturn = true;
	}
	return intReturn;
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
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
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

	/* Old Frequency (backward compat - moved lower) */
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

	for (i = 0; i < RATE_COUNT && i < IW_MAX_BITRATES; i++) {
		range->bitrate[i] = rtw_rates[i];
	}

	range->min_frag = MIN_FRAG_THRESHOLD;
	range->max_frag = MAX_FRAG_THRESHOLD;

	range->pm_capa = 0;

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 16;

	for (i = 0, val = 0; i < MAX_CHANNEL_NUM; i++) {

		/*  Include only legal frequencies for some countries */
		if (pmlmeext->channel_set[i].ChannelNum != 0)
		{
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

/*  Commented by Albert 2009/10/13 */
/*  The following code will proivde the security capability to network manager. */
/*  If the driver doesn't provide this capability to network manager, */
/*  the WPA/WPA2 routers can't be choosen in the network manager. */

/*
#define IW_SCAN_CAPA_NONE		0x00
#define IW_SCAN_CAPA_ESSID		0x01
#define IW_SCAN_CAPA_BSSID		0x02
#define IW_SCAN_CAPA_CHANNEL	0x04
#define IW_SCAN_CAPA_MODE		0x08
#define IW_SCAN_CAPA_RATE		0x10
#define IW_SCAN_CAPA_TYPE		0x20
#define IW_SCAN_CAPA_TIME		0x40
*/

#if WIRELESS_EXT > 17
	range->enc_capa = IW_ENC_CAPA_WPA|IW_ENC_CAPA_WPA2|
			  IW_ENC_CAPA_CIPHER_TKIP|IW_ENC_CAPA_CIPHER_CCMP;
#endif

#ifdef IW_SCAN_CAPA_ESSID /* WIRELESS_EXT > 21 */
	range->scan_capa = IW_SCAN_CAPA_ESSID | IW_SCAN_CAPA_TYPE |IW_SCAN_CAPA_BSSID|
					IW_SCAN_CAPA_CHANNEL|IW_SCAN_CAPA_MODE|IW_SCAN_CAPA_RATE;
#endif

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
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct sockaddr *temp = (struct sockaddr *)awrq;
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct list_head *phead;
	u8 *dst_bssid, *src_bssid;
	struct __queue *queue = &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	enum NDIS_802_11_AUTHENTICATION_MODE	authmode;

/*
#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->iface_type > PRIMARY_IFACE)
	{
		ret = -EINVAL;
		goto exit;
	}
#endif
*/

#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == true)
	{
		DBG_8192D("set bssid, but buddy_intf is under scanning or linking\n");

		ret = -EINVAL;

		goto exit;
	}
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (dc_check_fwstate(padapter, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == true)
	{
		DBG_8192D("set bssid, but buddy_intf is under scanning or linking\n");
		ret = -EINVAL;
		goto exit;
	}
#endif

	if (_FAIL == rtw_pwr_wakeup(padapter))
	{
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
	spin_lock_bh(&pmlmepriv->lock);
	spin_lock_bh(&queue->lock);
	phead = get_list_head(queue);
	pmlmepriv->pscanned = phead->next;

	while (1) {
		if ((rtw_end_of_queue_search(phead, pmlmepriv->pscanned)) == true)
			break;

		pnetwork = container_of(pmlmepriv->pscanned, struct wlan_network, list);

		pmlmepriv->pscanned = pmlmepriv->pscanned->next;

		dst_bssid = pnetwork->network.MacAddress;

		src_bssid = temp->sa_data;

		if ((!memcmp(dst_bssid, src_bssid, ETH_ALEN)) == true) {
			if (!rtw_set_802_11_infrastructure_mode(padapter, pnetwork->network.InfrastructureMode)) {
				ret = -1;
				spin_unlock_bh(&queue->lock);
				spin_unlock_bh(&pmlmepriv->lock);
				goto exit;
			}

				break;
		}

	}
	spin_unlock_bh(&queue->lock);
	spin_unlock_bh(&pmlmepriv->lock);
	rtw_set_802_11_authentication_mode(padapter, authmode);
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

	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct wlan_bssid_ex *pcur_bss = &pmlmepriv->cur_network.network;

	wrqu->ap_addr.sa_family = ARPHRD_ETHER;

	memset(wrqu->ap_addr.sa_data, 0, ETH_ALEN);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_wx_get_wap\n"));

	if  (((check_fwstate(pmlmepriv, _FW_LINKED)) == true) ||
			((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) == true) ||
			((check_fwstate(pmlmepriv, WIFI_AP_STATE)) == true))
	{

		memcpy(wrqu->ap_addr.sa_data, pcur_bss->MacAddress, ETH_ALEN);
	}
	else
	{
		memset(wrqu->ap_addr.sa_data, 0, ETH_ALEN);
	}

	return 0;
}

static int rtw_wx_set_mlme(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	__le16 reason;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct iw_mlme *mlme = (struct iw_mlme *) extra;

	if (mlme == NULL)
		return -1;

	DBG_8192D("%s\n", __func__);

	reason = cpu_to_le16(mlme->reason_code);

	DBG_8192D("%s, cmd =%d, reason =%d\n", __func__, mlme->cmd, reason);

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
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ndis_802_11_ssid ssid[RTW_SSID_SCAN_AMOUNT];
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_wx_set_scan\n"));

	#ifdef DBG_IOCTL
	DBG_8192D("DBG_IOCTL %s:%d\n", __func__, __LINE__);
	#endif

	if (_FAIL == rtw_pwr_wakeup(padapter))
	{
		ret = -1;
		goto exit;
	}

	if (padapter->bDriverStopped) {
		DBG_8192D("bDriverStopped =%d\n", padapter->bDriverStopped);
		ret = -1;
		goto exit;
	}

	if (!padapter->bup) {
		ret = -1;
		goto exit;
	}

	if (padapter->hw_init_completed == false) {
		ret = -1;
		goto exit;
	}

	/*  When Busy Traffic, driver do not site survey. So driver return success. */
	/*  wpa_supplicant will not issue SIOCSIWSCAN cmd again after scan timeout. */
	/*  modify by thomas 2011-02-22. */
	if (pmlmepriv->LinkDetectInfo.bBusyTraffic == true)
	{
		indicate_wx_scan_complete_event(padapter);
		goto exit;
	}

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == true)
	{
		indicate_wx_scan_complete_event(padapter);
		goto exit;
	}

#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter,
		_FW_UNDER_SURVEY|_FW_UNDER_LINKING|WIFI_UNDER_WPS) == true)
	{
		if (check_buddy_fwstate(padapter, _FW_UNDER_SURVEY))
		{
			DBG_8192D("scanning_via_buddy_intf\n");
			pmlmepriv->scanning_via_buddy_intf = true;
		}

		indicate_wx_scan_complete_event(padapter);

		goto exit;
	}
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (dc_check_fwstate(padapter, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == true)
	{
		indicate_wx_scan_complete_event(padapter);
		goto exit;
	}
#endif

/*	Mareded by Albert 20101103 */
/*	For the DMP WiFi Display project, the driver won't to scan because */
/*	the pmlmepriv->scan_interval is always equal to 3. */
/*	So, the wpa_supplicant won't find out the WPS SoftAP. */

	memset(ssid, 0, sizeof(struct ndis_802_11_ssid)*RTW_SSID_SCAN_AMOUNT);

#if WIRELESS_EXT >= 17
	if (wrqu->data.length == sizeof(struct iw_scan_req))
	{
		struct iw_scan_req *req = (struct iw_scan_req *)extra;

		if (wrqu->data.flags & IW_SCAN_THIS_ESSID)
		{
			int len = min((int)req->essid_len, IW_ESSID_MAX_SIZE);

			memcpy(ssid[0].Ssid, req->essid, len);
			ssid[0].SsidLength = len;

			DBG_8192D("IW_SCAN_THIS_ESSID, ssid =%s, len =%d\n", req->essid, req->essid_len);

			spin_lock_bh(&pmlmepriv->lock);

			_status = rtw_sitesurvey_cmd(padapter, ssid, 1, NULL, 0);

			spin_unlock_bh(&pmlmepriv->lock);

		}
		else if (req->scan_type == IW_SCAN_TYPE_PASSIVE)
		{
			DBG_8192D("rtw_wx_set_scan, req->scan_type == IW_SCAN_TYPE_PASSIVE\n");
		}

	}
	else
#endif

	if (wrqu->data.length >= WEXT_CSCAN_HEADER_SIZE &&
	    !memcmp(extra, WEXT_CSCAN_HEADER, WEXT_CSCAN_HEADER_SIZE)) {
		int len = wrqu->data.length -WEXT_CSCAN_HEADER_SIZE;
		char *pos = extra+WEXT_CSCAN_HEADER_SIZE;
		char section;
		char sec_len;
		int ssid_index = 0;

		while (len >= 1) {
			section = *(pos++); len-= 1;

			switch (section) {
				case WEXT_CSCAN_SSID_SECTION:
					if (len < 1) {
						len = 0;
						break;
					}

					sec_len = *(pos++); len-= 1;

					if (sec_len > 0 && sec_len<= len) {
						ssid[ssid_index].SsidLength = sec_len;
						memcpy(ssid[ssid_index].Ssid, pos, ssid[ssid_index].SsidLength);
						ssid_index++;
					}

					pos+= sec_len; len-= sec_len;
					break;

				case WEXT_CSCAN_CHANNEL_SECTION:
					pos+= 1; len-= 1;
					break;
				case WEXT_CSCAN_ACTV_DWELL_SECTION:
					pos+= 2; len-= 2;
					break;
				case WEXT_CSCAN_PASV_DWELL_SECTION:
					pos+= 2; len-= 2;
					break;
				case WEXT_CSCAN_HOME_DWELL_SECTION:
					pos+= 2; len-= 2;
					break;
				case WEXT_CSCAN_TYPE_SECTION:
					pos+= 1; len-= 1;
					break;
				default:
					len = 0; /*  stop parsing */
			}
		}
		/* jeff: it has still some scan paramater to parse, we only do this now... */
		_status = rtw_set_802_11_bssid_list_scan(padapter, ssid, RTW_SSID_SCAN_AMOUNT);

	} else {
		_status = rtw_set_802_11_bssid_list_scan(padapter, NULL, 0);
	}

	if (_status == false)
		ret = -1;

exit:
	#ifdef DBG_IOCTL
	DBG_8192D("DBG_IOCTL %s:%d return %d\n", __func__, __LINE__, ret);
	#endif

	return ret;
}

static int rtw_wx_get_scan(struct net_device *dev, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *extra)
{
	struct list_head *plist, *phead;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct __queue *queue = &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	char *ev = extra;
	char *stop = ev + wrqu->data.length;
	u32 ret = 0;
	u32 cnt = 0;
	u32 wait_for_surveydone;
	int wait_status;
#ifdef CONFIG_CONCURRENT_MODE
#endif
	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_wx_get_scan\n"));
	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_, (" Start of Query SIOCGIWSCAN .\n"));

	#ifdef DBG_IOCTL
	DBG_8192D("DBG_IOCTL %s:%d\n", __func__, __LINE__);
	#endif

	if (padapter->pwrctrlpriv.brfoffbyhw && padapter->bDriverStopped)
	{
		ret = -EINVAL;
		goto exit;
	}

	wait_for_surveydone = 100;

	wait_status = _FW_UNDER_SURVEY
		#ifndef CONFIG_ANDROID
		|_FW_UNDER_LINKING
		#endif
	;

#ifdef CONFIG_DUALMAC_CONCURRENT
	while (dc_check_fwstate(padapter, wait_status) == true)
	{
		rtw_msleep_os(30);
		cnt++;
		if (cnt > wait_for_surveydone)
			break;
	}
#endif /*  CONFIG_DUALMAC_CONCURRENT */

	while (check_fwstate(pmlmepriv, wait_status) == true)
	{
		rtw_msleep_os(30);
		cnt++;
		if (cnt > wait_for_surveydone)
			break;
	}

	spin_lock_bh(&(pmlmepriv->scanned_queue.lock));

	phead = get_list_head(queue);
	plist = phead->next;

	while (1)
	{
		if (rtw_end_of_queue_search(phead, plist) == true)
			break;

		if ((stop - ev) < SCAN_ITEM_SIZE) {
			ret = -E2BIG;
			break;
		}

		pnetwork = container_of(plist, struct wlan_network, list);

		/* report network only if the current channel set contains the channel to which this network belongs */
		if (rtw_ch_set_search_ch(padapter->mlmeextpriv.channel_set, pnetwork->network.Configuration.DSConfig) >= 0
			#ifdef CONFIG_VALIDATE_SSID
			&& true == rtw_validate_ssid(&(pnetwork->network.Ssid))
			#endif
		)
		{
			ev = translate_scan(padapter, a, pnetwork, ev, stop);
		}

		plist = plist->next;

	}

	spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));

	wrqu->data.length = ev-extra;
	wrqu->data.flags = 0;

exit:

	#ifdef DBG_IOCTL
	DBG_8192D("DBG_IOCTL %s:%d return %d\n", __func__, __LINE__, ret);
	#endif

	return ret ;
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
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct __queue *queue = &pmlmepriv->scanned_queue;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct list_head *phead;
	s8 status = true;
	struct wlan_network *pnetwork = NULL;
	enum NDIS_802_11_AUTHENTICATION_MODE authmode;
	struct ndis_802_11_ssid ndis_ssid;
	u8 *dst_ssid, *src_ssid;

	uint ret = 0, len;

	#ifdef DBG_IOCTL
	DBG_8192D("DBG_IOCTL %s:%d\n", __func__, __LINE__);
	#endif

/*
#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->iface_type > PRIMARY_IFACE)
	{
		ret = -EINVAL;
		goto exit;
	}
#endif
*/

#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == true)
	{
		DBG_8192D("set ssid, but buddy_intf is under scanning or linking\n");

		ret = -EINVAL;

		goto exit;
	}
#endif

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (dc_check_fwstate(padapter, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == true)
	{
		DBG_8192D("set bssid, but buddy_intf is under scanning or linking\n");
		ret = -EINVAL;
		goto exit;
	}
#endif

	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
		 ("+rtw_wx_set_essid: fw_state = 0x%08x\n", get_fwstate(pmlmepriv)));
	if (_FAIL == rtw_pwr_wakeup(padapter))
	{
		ret = -1;
		goto exit;
	}

	if (!padapter->bup) {
		ret = -1;
		goto exit;
	}

#if WIRELESS_EXT <= 20
	if ((wrqu->essid.length-1) > IW_ESSID_MAX_SIZE) {
#else
	if (wrqu->essid.length > IW_ESSID_MAX_SIZE) {
#endif
		ret = -E2BIG;
		goto exit;
	}

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		ret = -1;
		goto exit;
	}

	authmode = padapter->securitypriv.ndisauthtype;
	DBG_8192D("=>%s\n", __func__);
	if (wrqu->essid.flags && wrqu->essid.length)
	{
		/*  Commented by Albert 20100519 */
		/*  We got the codes in "set_info" function of iwconfig source code. */
		/*	========================================= */
		/*	wrq.u.essid.length = strlen(essid) + 1; */
		/*	if (we_kernel_version > 20) */
		/*		wrq.u.essid.length--; */
		/*	========================================= */
		/*	That means, if the WIRELESS_EXT less than or equal to 20, the correct ssid len should subtract 1. */
#if WIRELESS_EXT <= 20
		len = ((wrqu->essid.length-1) < IW_ESSID_MAX_SIZE) ? (wrqu->essid.length-1) : IW_ESSID_MAX_SIZE;
#else
		len = (wrqu->essid.length < IW_ESSID_MAX_SIZE) ? wrqu->essid.length : IW_ESSID_MAX_SIZE;
#endif

		if (wrqu->essid.length != 33)
			DBG_8192D("ssid =%s, len =%d\n", extra, wrqu->essid.length);

		memset(&ndis_ssid, 0, sizeof(struct ndis_802_11_ssid));
		ndis_ssid.SsidLength = len;
		memcpy(ndis_ssid.Ssid, extra, len);
		src_ssid = ndis_ssid.Ssid;

		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_, ("rtw_wx_set_essid: ssid =[%s]\n", src_ssid));
		spin_lock_bh(&pmlmepriv->lock);
		spin_lock_bh(&queue->lock);
		phead = get_list_head(queue);
		pmlmepriv->pscanned = phead->next;

		while (1) {
			if (rtw_end_of_queue_search(phead, pmlmepriv->pscanned) == true) {
			        RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_warning_,
					 ("rtw_wx_set_essid: scan_q is empty, set ssid to check if scanning again!\n"));

				break;
			}

			pnetwork = container_of(pmlmepriv->pscanned, struct wlan_network, list);

			pmlmepriv->pscanned = pmlmepriv->pscanned->next;

			dst_ssid = pnetwork->network.Ssid.Ssid;

			RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
				 ("rtw_wx_set_essid: dst_ssid =%s\n",
				  pnetwork->network.Ssid.Ssid));

			if (!memcmp(dst_ssid, src_ssid, ndis_ssid.SsidLength) &&
			    pnetwork->network.Ssid.SsidLength == ndis_ssid.SsidLength) {
				RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
					 ("rtw_wx_set_essid: find match, set infra mode\n"));

				if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
					if (pnetwork->network.InfrastructureMode != pmlmepriv->cur_network.network.InfrastructureMode)
						continue;
				}

				if (rtw_set_802_11_infrastructure_mode(padapter, pnetwork->network.InfrastructureMode) == false)
				{
					ret = -1;
					spin_unlock_bh(&queue->lock);
					spin_unlock_bh(&pmlmepriv->lock);
					goto exit;
				}

				break;
			}
		}
		spin_unlock_bh(&queue->lock);
		spin_unlock_bh(&pmlmepriv->lock);
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
			 ("set ssid: set_802_11_auth. mode =%d\n", authmode));
		rtw_set_802_11_authentication_mode(padapter, authmode);
		if (rtw_set_802_11_ssid(padapter, &ndis_ssid) == false) {
			ret = -1;
			goto exit;
		}
	}

exit:

	DBG_8192D("<=%s, ret %d\n", __func__, ret);

	#ifdef DBG_IOCTL
	DBG_8192D("DBG_IOCTL %s:%d return %d\n", __func__, __LINE__, ret);
	#endif

	return ret;
}

static int rtw_wx_get_essid(struct net_device *dev,
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *extra)
{
	u32 len, ret = 0;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct wlan_bssid_ex *pcur_bss = &pmlmepriv->cur_network.network;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_wx_get_essid\n"));

	if ((check_fwstate(pmlmepriv, _FW_LINKED) == true) ||
	      (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true))
	{
		len = pcur_bss->Ssid.SsidLength;

		wrqu->essid.length = len;

		memcpy(extra, pcur_bss->Ssid.Ssid, len);

		wrqu->essid.flags = 1;
	}
	else
	{
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
	int	i, ret = 0;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	u8	datarates[NUMRATES];
	u32	target_rate = wrqu->bitrate.value;
	u32	fixed = wrqu->bitrate.fixed;
	u32	ratevalue = 0;
	 u8 mpdatarate[NUMRATES]={11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0xff};

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

	for (i = 0; i < NUMRATES; i++) {
		if (ratevalue == mpdatarate[i]) {
			datarates[i] = mpdatarate[i];
			if (fixed == 0)
				break;
		} else {
			datarates[i] = 0xff;
		}

		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_, ("datarate_inx =%d\n", datarates[i]));
	}

	if (rtw_setdatarate_cmd(padapter, datarates) != _SUCCESS) {
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_, ("rtw_wx_set_rate Fail!!!\n"));
		ret = -1;
	}

	return ret;
}

static int rtw_wx_get_rate(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	u16 max_rate = 0;

	max_rate = rtw_get_cur_max_rate((struct rtw_adapter *)rtw_netdev_priv(dev));

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
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);

	if (wrqu->rts.disabled)
		padapter->registrypriv.rts_thresh = 2347;
	else {
		if (wrqu->rts.value < 0 ||
		    wrqu->rts.value > 2347)
			return -EINVAL;

		padapter->registrypriv.rts_thresh = wrqu->rts.value;
	}

	DBG_8192D("%s, rts_thresh =%d\n", __func__, padapter->registrypriv.rts_thresh);

	return 0;
}

static int rtw_wx_get_rts(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);

	DBG_8192D("%s, rts_thresh =%d\n", __func__, padapter->registrypriv.rts_thresh);

	wrqu->rts.value = padapter->registrypriv.rts_thresh;
	wrqu->rts.fixed = 0;	/* no auto select */

	return 0;
}

static int rtw_wx_set_frag(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);

	if (wrqu->frag.disabled)
		padapter->xmitpriv.frag_len = MAX_FRAG_THRESHOLD;
	else {
		if (wrqu->frag.value < MIN_FRAG_THRESHOLD ||
		    wrqu->frag.value > MAX_FRAG_THRESHOLD)
			return -EINVAL;

		padapter->xmitpriv.frag_len = wrqu->frag.value & ~0x1;
	}

	DBG_8192D("%s, frag_len =%d\n", __func__, padapter->xmitpriv.frag_len);

	return 0;
}

static int rtw_wx_get_frag(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);

	DBG_8192D("%s, frag_len =%d\n", __func__, padapter->xmitpriv.frag_len);

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
	struct ndis_802_11_wep wep;
	enum NDIS_802_11_AUTHENTICATION_MODE authmode;

	struct iw_point *erq = &(wrqu->encoding);
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	DBG_8192D("+rtw_wx_set_enc, flags = 0x%x\n", erq->flags);

	memset(&wep, 0, sizeof(struct ndis_802_11_wep));

	key = erq->flags & IW_ENCODE_INDEX;

	if (erq->flags & IW_ENCODE_DISABLED)
	{
		DBG_8192D("EncryptionDisabled\n");
		padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION_DISABLED;
		padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
		padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open; /* open system */
		authmode = NDIS802_11AUTHMODEOPEN;
		padapter->securitypriv.ndisauthtype = authmode;

		goto exit;
	}

	if (key) {
		if (key > WEP_KEYS)
			return -EINVAL;
		key--;
		keyindex_provided = 1;
	}
	else
	{
		keyindex_provided = 0;
		key = padapter->securitypriv.dot11PrivacyKeyIndex;
		DBG_8192D("rtw_wx_set_enc, key =%d\n", key);
	}

	/* set authentication mode */
	if (erq->flags & IW_ENCODE_OPEN)
	{
		DBG_8192D("rtw_wx_set_enc():IW_ENCODE_OPEN\n");
		padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION1ENABLED;/* NDIS802_11ENCRYPTION_DISABLED; */

		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open;

		padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
		padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
		authmode = NDIS802_11AUTHMODEOPEN;
		padapter->securitypriv.ndisauthtype = authmode;
	}
	else if (erq->flags & IW_ENCODE_RESTRICTED)
	{
		DBG_8192D("rtw_wx_set_enc():IW_ENCODE_RESTRICTED\n");
		padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION1ENABLED;

		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Shared;

		padapter->securitypriv.dot11PrivacyAlgrthm = _WEP40_;
		padapter->securitypriv.dot118021XGrpPrivacy = _WEP40_;
		authmode = NDIS802_11AUTHMODESHARED;
		padapter->securitypriv.ndisauthtype = authmode;
	}
	else
	{
		DBG_8192D("rtw_wx_set_enc():erq->flags = 0x%x\n", erq->flags);

		padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION1ENABLED;/* NDIS802_11ENCRYPTION_DISABLED; */
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open; /* open system */
		padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
		padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
		authmode = NDIS802_11AUTHMODEOPEN;
		padapter->securitypriv.ndisauthtype = authmode;
	}

	wep.KeyIndex = key;
	if (erq->length > 0)
	{
		wep.KeyLength = erq->length <= 5 ? 5 : 13;

		wep.Length = wep.KeyLength + FIELD_OFFSET(struct ndis_802_11_wep, KeyMaterial);
	}
	else
	{
		wep.KeyLength = 0 ;

		if (keyindex_provided == 1)/*  set key_id only, no given KeyMaterial(erq->length == 0). */
		{
			padapter->securitypriv.dot11PrivacyKeyIndex = key;

			DBG_8192D("(keyindex_provided == 1), keyid =%d, key_len =%d\n", key, padapter->securitypriv.dot11DefKeylen[key]);

			switch (padapter->securitypriv.dot11DefKeylen[key])
			{
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
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct iw_point *erq = &(wrqu->encoding);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);

	if (check_fwstate(pmlmepriv, _FW_LINKED) != true)
	{
		 if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) != true)
		 {
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
	} else
	{
		key = padapter->securitypriv.dot11PrivacyKeyIndex;
	}

	erq->flags = key + 1;

	switch (padapter->securitypriv.ndisencryptstatus)
	{
		case NDIS802_11ENCRYPTIONNOTSUPPORTED:
		case NDIS802_11ENCRYPTION_DISABLED:

		erq->length = 0;
		erq->flags |= IW_ENCODE_DISABLED;

		break;

		case NDIS802_11ENCRYPTION1ENABLED:

		erq->length = padapter->securitypriv.dot11DefKeylen[key];

		if (erq->length)
		{
			memcpy(keybuf, padapter->securitypriv.dot11DefKey[key].skey, padapter->securitypriv.dot11DefKeylen[key]);

		erq->flags |= IW_ENCODE_ENABLED;

			if (padapter->securitypriv.ndisauthtype == NDIS802_11AUTHMODEOPEN)
			{
				erq->flags |= IW_ENCODE_OPEN;
			}
			else if (padapter->securitypriv.ndisauthtype == NDIS802_11AUTHMODESHARED)
			{
		erq->flags |= IW_ENCODE_RESTRICTED;
			}
		}
		else
		{
			erq->length = 0;
			erq->flags |= IW_ENCODE_DISABLED;
		}

		break;

		case NDIS802_11ENCRYPTION2ENABLED:
		case NDIS802_11ENCRYPTION3ENABLED:

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
	int ret;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);

	ret = rtw_set_wpa_ie(padapter, extra, wrqu->data.length);

	return ret;
}

static int rtw_wx_set_auth(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct iw_param *param = (struct iw_param*)&(wrqu->param);
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

		if (padapter->securitypriv.ndisencryptstatus == NDIS802_11ENCRYPTION1ENABLED) {
			break;/* it means init value, or using wep, ndisencryptstatus = NDIS802_11ENCRYPTION1ENABLED, */
					/*  then it needn't reset it; */
		}

		if (param->value) {
			padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION_DISABLED;
			padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
			padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open; /* open system */
			padapter->securitypriv.ndisauthtype = NDIS802_11AUTHMODEOPEN;
		}

		break;
	case IW_AUTH_80211_AUTH_ALG:

		#if defined(CONFIG_ANDROID) || 1
		/*
		 *  It's the starting point of a link layer connection using wpa_supplicant
		*/
		if (check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {
			LeaveAllPowerSaveMode(padapter);
			rtw_disassoc_cmd(padapter, 500, false);
			DBG_8192D("%s...call rtw_indicate_disconnect\n ", __func__);
			rtw_indicate_disconnect(padapter);
			rtw_free_assoc_resources(padapter, 1);
		}
		#endif

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
	param = (struct ieee_param *)kmalloc(param_len, GFP_KERNEL);
	if (param == NULL)
		return -1;

	memset(param, 0, param_len);

	param->cmd = IEEE_CMD_SET_ENCRYPTION;
	memset(param->sta_addr, 0xff, ETH_ALEN);

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
		return -1;
	}

	strncpy((char *)param->u.crypt.alg, alg_name, IEEE_CRYPT_ALG_NAME_LEN);

	if (pext->ext_flags & IW_ENCODE_EXT_GROUP_KEY)/*  */
	{
		param->u.crypt.set_tx = 0;
	}

	if (pext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY)/*  */
	{
		param->u.crypt.set_tx = 1;
	}

	param->u.crypt.idx = (pencoding->flags&0x00FF) -1 ;

	if (pext->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID)
	{
		memcpy(param->u.crypt.seq, pext->rx_seq, 8);
	}

	if (pext->key_len)
	{
		param->u.crypt.key_len = pext->key_len;
		memcpy(param->u.crypt.key, pext + 1, pext->key_len);
	}

	if (pencoding->flags & IW_ENCODE_DISABLED)
	{
		/* todo: remove key */
		/* remove = 1; */
	}

	ret =  wpa_set_encryption(dev, param, param_len);

	kfree(param);

	return ret;
}

static int rtw_wx_get_nick(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{

	if (extra)
	{
		wrqu->data.length = 14;
		wrqu->data.flags = 1;
		memcpy(extra, "<WIFI@REALTEK>", 14);
	}

	return 0;
}

static int rtw_wx_read32(struct net_device *dev,
			 struct iw_request_info *info,
			 union iwreq_data *wrqu, char *extra)
{
	struct rtw_adapter * padapter;
	struct iw_point *p;
	u16 len;
	u32 addr;
	u32 data32;
	u32 bytes;
	u8 *ptmp;

	padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	p = &wrqu->data;
	len = p->length;
	ptmp = (u8 *)kmalloc(len, GFP_KERNEL);
	if (NULL == ptmp)
		return -ENOMEM;

	if (copy_from_user(ptmp, p->pointer, len)) {
		kfree(ptmp);
		return -EFAULT;
	}

	bytes = 0;
	addr = 0;
	sscanf(ptmp, "%d,%x", &bytes, &addr);

	switch (bytes) {
		case 1:
			data32 = rtw_read8(padapter, addr);
			sprintf(extra, "0x%02X", data32);
			break;
		case 2:
			data32 = rtw_read16(padapter, addr);
			sprintf(extra, "0x%04X", data32);
			break;
		case 4:
			data32 = rtw_read32(padapter, addr);
			sprintf(extra, "0x%08X", data32);
			break;
		default:
			DBG_8192D("%s: usage> read [bytes],[address(hex)]\n", __func__);
			return -EINVAL;
	}
	DBG_8192D("%s: addr = 0x%08X data =%s\n", __func__, addr, extra);

	kfree(ptmp);

	return 0;
}

static int rtw_wx_write32(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	struct rtw_adapter * padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);

	u32 addr;
	u32 data32;
	u32 bytes;

	bytes = 0;
	addr = 0;
	data32 = 0;
	sscanf(extra, "%d,%x,%x", &bytes, &addr, &data32);

	switch (bytes) {
		case 1:
			rtw_write8(padapter, addr, (u8)data32);
			DBG_8192D("%s: addr = 0x%08X data = 0x%02X\n", __func__, addr, (u8)data32);
			break;
		case 2:
			rtw_write16(padapter, addr, (u16)data32);
			DBG_8192D("%s: addr = 0x%08X data = 0x%04X\n", __func__, addr, (u16)data32);
			break;
		case 4:
			rtw_write32(padapter, addr, data32);
			DBG_8192D("%s: addr = 0x%08X data = 0x%08X\n", __func__, addr, data32);
			break;
		default:
			DBG_8192D("%s: usage> write [bytes],[address(hex)],[data(hex)]\n", __func__);
			return -EINVAL;
	}

	return 0;
}

static int rtw_wx_read_rf(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	u32 path, addr, data32;

	path = *(u32*)extra;
	addr = *((u32*)extra + 1);
	data32 = rtw_hal_read_rfreg(padapter, path, addr, 0xFFFFF);
	/*
	 * IMPORTANT!!
	 * Only when wireless private ioctl is at odd order,
	 * "extra" would be copied to user space.
	 */
	sprintf(extra, "0x%05x", data32);

	return 0;
}

static int rtw_wx_write_rf(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	u32 path, addr, data32;

	path = *(u32*)extra;
	addr = *((u32*)extra + 1);
	data32 = *((u32*)extra + 2);
	rtw_hal_write_rfreg(padapter, path, addr, 0xFFFFF, data32);

	return 0;
}

static int rtw_wx_priv_null(struct net_device *dev, struct iw_request_info *a,
		 union iwreq_data *wrqu, char *b)
{
	return -1;
}

static int dummy(struct net_device *dev, struct iw_request_info *a,
		 union iwreq_data *wrqu, char *b)
{

	return -1;
}

static int rtw_wx_set_channel_plan(struct net_device *dev,
				   struct iw_request_info *info,
				   union iwreq_data *wrqu, char *extra)
{
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	extern int rtw_channel_plan;
	u8 channel_plan_req = (u8) (*((int *)wrqu));

	if (_SUCCESS == rtw_set_chplan_cmd(padapter, channel_plan_req, 1)) {
		DBG_8192D("%s set channel_plan = 0x%02X\n", __func__, pmlmepriv->ChannelPlan);
	} else
		return -EPERM;

	return 0;
}

static int rtw_wx_set_mtk_wps_probe_ie(struct net_device *dev,
		struct iw_request_info *a,
		union iwreq_data *wrqu, char *b)
{
	return 0;
}

static int rtw_wx_get_sensitivity(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *buf)
{
	return 0;
}

static int rtw_wx_set_mtk_wps_ie(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	return 0;
}

/*
 *	For all data larger than 16 octets, we need to use a
 *	pointer to memory allocated in user space.
 */
static  int rtw_drvext_hdl(struct net_device *dev, struct iw_request_info *info,
						union iwreq_data *wrqu, char *extra)
{

#ifdef CONFIG_DRVEXT_MODULE
	u8 res;
	struct drvext_handler *phandler;
	struct drvext_oidparam *poidparam;
	int ret;
	u16 len;
	u8 *pparmbuf, bset;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct iw_point *p = &wrqu->data;

	if ((!p->length) || (!p->pointer)) {
		ret = -EINVAL;
		goto _rtw_drvext_hdl_exit;
	}

	bset = (u8)(p->flags&0xFFFF);
	len = p->length;
	pparmbuf = (u8 *)kmalloc(len, GFP_KERNEL);
	if (pparmbuf == NULL) {
		ret = -ENOMEM;
		goto _rtw_drvext_hdl_exit;
	}

	if (bset)/* set info */
	{
		if (copy_from_user(pparmbuf, p->pointer, len)) {
			kfree(pparmbuf);
			ret = -EFAULT;
			goto _rtw_drvext_hdl_exit;
		}
	}
	else/* query info */
	{

	}

	/*  */
	poidparam = (struct drvext_oidparam *)pparmbuf;

	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_, ("drvext set oid subcode [%d], len[%d], InformationBufferLength[%d]\r\n",
						 poidparam->subcode, poidparam->len, len));

	/* check subcode */
	if (poidparam->subcode >= MAX_DRVEXT_HANDLERS)
	{
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_, ("no matching drvext handlers\r\n"));
		ret = -EINVAL;
		goto _rtw_drvext_hdl_exit;
	}

	if (poidparam->subcode >= MAX_DRVEXT_OID_SUBCODES)
	{
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_, ("no matching drvext subcodes\r\n"));
		ret = -EINVAL;
		goto _rtw_drvext_hdl_exit;
	}

	phandler = drvextoidhandlers + poidparam->subcode;

	if (poidparam->len != phandler->parmsize)
	{
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_, ("no matching drvext param size %d vs %d\r\n",
						poidparam->len , phandler->parmsize));
		ret = -EINVAL;
		goto _rtw_drvext_hdl_exit;
	}

	res = phandler->handler(&padapter->drvextpriv, bset, poidparam->data);

	if (res == 0)
	{
		ret = 0;

		if (bset == 0x00) {/* query info */
			if (copy_to_user(p->pointer, pparmbuf, len))
				ret = -EFAULT;
		}
	}
	else
		ret = -EFAULT;

_rtw_drvext_hdl_exit:

	return ret;

#endif

	return 0;
}

static void rtw_dbg_mode_hdl(struct rtw_adapter *padapter, u32 id, u8 *pdata, u32 len)
{
	struct mp_rw_reg *RegRWStruct;
	struct rf_reg_param *prfreg;
	u8 path;
	u8 offset;
	u32 value;

	DBG_8192D("%s\n", __func__);

	switch (id)
	{
		case GEN_MP_IOCTL_SUBCODE(MP_START):
			DBG_8192D("871x_driver is only for normal mode, can't enter mp mode\n");
			break;
		case GEN_MP_IOCTL_SUBCODE(READ_REG):
			RegRWStruct = (struct mp_rw_reg *)pdata;
			switch (RegRWStruct->width)
			{
				case 1:
					RegRWStruct->value = rtw_read8(padapter, RegRWStruct->offset);
					break;
				case 2:
					RegRWStruct->value = rtw_read16(padapter, RegRWStruct->offset);
					break;
				case 4:
					RegRWStruct->value = rtw_read32(padapter, RegRWStruct->offset);
					break;
				default:
					break;
			}

			break;
		case GEN_MP_IOCTL_SUBCODE(WRITE_REG):
			RegRWStruct = (struct mp_rw_reg *)pdata;
			switch (RegRWStruct->width)
			{
				case 1:
					rtw_write8(padapter, RegRWStruct->offset, (u8)RegRWStruct->value);
					break;
				case 2:
					rtw_write16(padapter, RegRWStruct->offset, (u16)RegRWStruct->value);
					break;
				case 4:
					rtw_write32(padapter, RegRWStruct->offset, (u32)RegRWStruct->value);
					break;
				default:
				break;
			}

			break;
		case GEN_MP_IOCTL_SUBCODE(READ_RF_REG):

			prfreg = (struct rf_reg_param *)pdata;

			path = (u8)prfreg->path;
			offset = (u8)prfreg->offset;

			value = rtw_hal_read_rfreg(padapter, path, offset, 0xffffffff);

			prfreg->value = value;

			break;
		case GEN_MP_IOCTL_SUBCODE(WRITE_RF_REG):

			prfreg = (struct rf_reg_param *)pdata;

			path = (u8)prfreg->path;
			offset = (u8)prfreg->offset;
			value = prfreg->value;

			rtw_hal_write_rfreg(padapter, path, offset, 0xffffffff, value);
			break;
		case GEN_MP_IOCTL_SUBCODE(TRIGGER_GPIO):
			DBG_8192D("==> trigger gpio 0\n");
			rtw_hal_set_hwreg(padapter, HW_VAR_TRIGGER_GPIO_0, NULL);
			break;
#ifdef CONFIG_BT_COEXIST
		case GEN_MP_IOCTL_SUBCODE(SET_DM_BT):
			DBG_8192D("==> set dm_bt_coexist:%x\n",*(u8 *)pdata);
			rtw_hal_set_hwreg(padapter, HW_VAR_BT_SET_COEXIST, pdata);
			break;
		case GEN_MP_IOCTL_SUBCODE(DEL_BA):
			DBG_8192D("==> delete ba:%x\n",*(u8 *)pdata);
			rtw_hal_set_hwreg(padapter, HW_VAR_BT_ISSUE_DELBA, pdata);
			break;
#endif
#ifdef DBG_CONFIG_ERROR_DETECT
		case GEN_MP_IOCTL_SUBCODE(GET_WIFI_STATUS):
			*pdata = rtw_hal_sreset_get_wifi_status(padapter);
			break;
#endif

		default:
			break;
	}
}

static int rtw_mp_ioctl_hdl(struct net_device *dev, struct iw_request_info *info,
						union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	u32 BytesRead, BytesWritten, BytesNeeded;
	struct oid_par_priv	oid_par;
	struct mp_ioctl_handler	*phandler;
	struct mp_ioctl_param	*poidparam;
	uint status = 0;
	u16 len;
	u8 *pparmbuf = NULL, bset;
	struct rtw_adapter * padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct iw_point *p = &wrqu->data;

	if ((!p->length) || (!p->pointer)) {
		ret = -EINVAL;
		goto _rtw_mp_ioctl_hdl_exit;
	}

	pparmbuf = NULL;
	bset = (u8)(p->flags & 0xFFFF);
	len = p->length;
	pparmbuf = (u8 *)kmalloc(len, GFP_KERNEL);
	if (pparmbuf == NULL) {
		ret = -ENOMEM;
		goto _rtw_mp_ioctl_hdl_exit;
	}

	if (copy_from_user(pparmbuf, p->pointer, len)) {
		ret = -EFAULT;
		goto _rtw_mp_ioctl_hdl_exit;
	}

	poidparam = (struct mp_ioctl_param *)pparmbuf;
	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
		 ("rtw_mp_ioctl_hdl: subcode [%d], len[%d], buffer_len[%d]\r\n",
		  poidparam->subcode, poidparam->len, len));

	if (poidparam->subcode >= MAX_MP_IOCTL_SUBCODE) {
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_, ("no matching drvext subcodes\r\n"));
		ret = -EINVAL;
		goto _rtw_mp_ioctl_hdl_exit;
	}
	rtw_dbg_mode_hdl(padapter, poidparam->subcode, poidparam->data, poidparam->len);

	if (bset == 0x00) {/* query info */
		if (copy_to_user(p->pointer, pparmbuf, len))
			ret = -EFAULT;
	}

	if (status) {
		ret = -EFAULT;
		goto _rtw_mp_ioctl_hdl_exit;
	}

_rtw_mp_ioctl_hdl_exit:

	kfree(pparmbuf);

	return ret;
}

static int rtw_get_ap_info(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	int bssid_match, ret = 0;
	u32 cnt = 0, wpa_ielen;
	struct list_head *plist, *phead;
	unsigned char *pbuf;
	u8 bssid[ETH_ALEN];
	char data[32];
	struct wlan_network *pnetwork = NULL;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct __queue *queue = &(pmlmepriv->scanned_queue);
	struct iw_point *pdata = &wrqu->data;

	DBG_8192D("+rtw_get_aplist_info\n");

	if ((padapter->bDriverStopped) || (pdata == NULL)) {
		ret = -EINVAL;
		goto exit;
	}

	while (check_fwstate(pmlmepriv, (_FW_UNDER_SURVEY|_FW_UNDER_LINKING))) {
		rtw_msleep_os(30);
		cnt++;
		if (cnt > 100)
			break;
	}

	pdata->flags = 0;
	if (pdata->length >= 32) {
		if (copy_from_user(data, pdata->pointer, 32)) {
			ret = -EINVAL;
			goto exit;
		}
	} else {
		ret = -EINVAL;
		goto exit;
	}

	spin_lock_bh(&(pmlmepriv->scanned_queue.lock));

	phead = get_list_head(queue);
	plist = phead->next;

	while (1) {
		if (rtw_end_of_queue_search(phead, plist) == true)
			break;

		pnetwork = container_of(plist, struct wlan_network, list);

		if (hwaddr_aton_i(data, bssid)) {
			DBG_8192D("Invalid BSSID '%s'.\n", (u8 *)data);
			spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
			return -EINVAL;
		}

		if (!memcmp(bssid, pnetwork->network.MacAddress, ETH_ALEN)) {
			/* BSSID match, then check if supporting wpa/wpa2 */
			DBG_8192D("BSSID:%pM\n", bssid);

			pbuf = rtw_get_wpa_ie(&pnetwork->network.IEs[12], &wpa_ielen, pnetwork->network.IELength-12);
			if (pbuf && (wpa_ielen > 0)) {
				pdata->flags = 1;
				break;
			}

			pbuf = rtw_get_wpa2_ie(&pnetwork->network.IEs[12], &wpa_ielen, pnetwork->network.IELength-12);
			if (pbuf && (wpa_ielen > 0)) {
				pdata->flags = 2;
				break;
			}

		}

		plist = plist->next;

	}

	spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));

	if (pdata->length>= 34) {
		if (copy_to_user((u8 __user *)pdata->pointer+32, (u8 *)&pdata->flags, 1)) {
			ret = -EINVAL;
			goto exit;
		}
	}

exit:

	return ret;
}

static int rtw_set_pid(struct net_device *dev,
		       struct iw_request_info *info,
		       union iwreq_data *wrqu, char *extra)
{

	int ret = 0;
	struct rtw_adapter *padapter = rtw_netdev_priv(dev);
	int *pdata = (int *)wrqu;
	int selector;

	if ((padapter->bDriverStopped) || (pdata == NULL))
	{
		ret = -EINVAL;
		goto exit;
	}

	selector = *pdata;
	if (selector < 3 && selector >= 0) {
		padapter->pid[selector] = *(pdata+1);
		DBG_8192D("%s set pid[%d]=%d\n", __func__, selector , padapter->pid[selector]);
	} else {
		DBG_8192D("%s selector %d error\n", __func__, selector);
	}
exit:
	return ret;
}

static int rtw_wps_start(struct net_device *dev,
			 struct iw_request_info *info,
			 union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct iw_point *pdata = &wrqu->data;
	u32   u32wps_start = 0;
	unsigned int uintRet = 0;

	uintRet = copy_from_user((void*) &u32wps_start, pdata->pointer, 4);
	if (uintRet || padapter->bDriverStopped || pdata == NULL) {
		ret = -EINVAL;
		goto exit;
	}

	if (u32wps_start == 0)
		u32wps_start = *extra;

	DBG_8192D("[%s] wps_start = %d\n", __func__, u32wps_start);

	if (u32wps_start == 1) /*  WPS Start */
	{
		rtw_led_control(padapter, LED_CTL_START_WPS);
	}
	else if (u32wps_start == 2) /*  WPS Stop because of wps success */
	{
		rtw_led_control(padapter, LED_CTL_STOP_WPS);
	}
	else if (u32wps_start == 3) /*  WPS Stop because of wps fail */
	{
		rtw_led_control(padapter, LED_CTL_STOP_WPS_FAIL);
	}

exit:

	return ret;
}

static int rtw_p2p_set(struct net_device *dev,
		       struct iw_request_info *info,
		       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

	return ret;
}

static int rtw_p2p_get(struct net_device *dev,
		       struct iw_request_info *info,
		       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

	return ret;
}

static int rtw_p2p_get2(struct net_device *dev,
						struct iw_request_info *info,
						union iwreq_data *wrqu, char *extra)
{

	int ret = 0;

	return ret;
}

static int rtw_rereg_nd_name(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct rtw_adapter *padapter = rtw_netdev_priv(dev);
	struct rereg_nd_name_data *rereg_priv = &padapter->rereg_nd_name_priv;
	char new_ifname[IFNAMSIZ];

	if (rereg_priv->old_ifname[0] == 0) {
		char *reg_ifname;
#ifdef CONFIG_CONCURRENT_MODE
		if (padapter->isprimary)
			reg_ifname = padapter->registrypriv.ifname;
		else
#endif
		reg_ifname = padapter->registrypriv.if2name;

		strncpy(rereg_priv->old_ifname, reg_ifname, IFNAMSIZ);
		rereg_priv->old_ifname[IFNAMSIZ-1] = 0;
	}

	/* DBG_8192D("%s wrqu->data.length:%d\n", __func__, wrqu->data.length); */
	if (wrqu->data.length > IFNAMSIZ)
		return -EFAULT;

	if (copy_from_user(new_ifname, wrqu->data.pointer, IFNAMSIZ)) {
		return -EFAULT;
	}

	if (0 == strcmp(rereg_priv->old_ifname, new_ifname)) {
		return ret;
	}

	DBG_8192D("%s new_ifname:%s\n", __func__, new_ifname);
	if (0 != (ret = rtw_change_ifname(padapter, new_ifname))) {
		goto exit;
	}

	if (!memcmp(rereg_priv->old_ifname, "disable%d", 9)) {
		padapter->ledpriv.bRegUseLed = rereg_priv->old_bRegUseLed;
		rtw_hal_sw_led_init(padapter);
		rtw_ips_mode_req(&padapter->pwrctrlpriv, rereg_priv->old_ips_mode);
	}

	strncpy(rereg_priv->old_ifname, new_ifname, IFNAMSIZ);
	rereg_priv->old_ifname[IFNAMSIZ-1] = 0;

	if (!memcmp(new_ifname, "disable%d", 9)) {
		DBG_8192D("%s disable\n", __func__);
		/*  free network queue for Android's timming issue */
		rtw_free_network_queue(padapter, true);

		/*  close led */
		rtw_led_control(padapter, LED_CTL_POWER_OFF);
		rereg_priv->old_bRegUseLed = padapter->ledpriv.bRegUseLed;
		padapter->ledpriv.bRegUseLed = false;
		rtw_hal_sw_led_deinit(padapter);

		/*  the interface is being "disabled", we can do deeper IPS */
		rereg_priv->old_ips_mode = rtw_get_ips_mode_req(&padapter->pwrctrlpriv);
		rtw_ips_mode_req(&padapter->pwrctrlpriv, IPS_NORMAL);
	}
exit:
	return ret;
}

static void mac_reg_dump(struct rtw_adapter *padapter)
{
	int i, j = 1;
	DBG_8192D("\n ======= MAC REG =======\n");
	for (i = 0x0; i < 0x300; i += 4) {
		if (j%4 == 1)
			DBG_8192D("0x%02x", i);
		DBG_8192D(" 0x%08x ", rtw_read32(padapter, i));
		if ((j++)%4 == 0)
			DBG_8192D("\n");
	}
	for (i = 0x400; i < 0x800; i+= 4) {
		if (j%4 == 1)
			DBG_8192D("0x%02x", i);
		DBG_8192D(" 0x%08x ", rtw_read32(padapter, i));
		if ((j++)%4 == 0)
			DBG_8192D("\n");
	}
}

static void bb_reg_dump(struct rtw_adapter *padapter)
{
	int i, j = 1;
	DBG_8192D("\n ======= BB REG =======\n");
	for (i = 0x800; i < 0x1000; i += 4) {
		if (j%4 == 1)
			DBG_8192D("0x%02x", i);

		DBG_8192D(" 0x%08x ", rtw_read32(padapter, i));
		if ((j++)%4 == 0)
			DBG_8192D("\n");
	}
}

static void rf_reg_dump(struct rtw_adapter *padapter)
{
	int i, j = 1, path;
	u32 value;
	u8 rf_type, path_nums = 0;
	rtw_hal_get_hwreg(padapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));

	DBG_8192D("\n ======= RF REG =======\n");
	if ((RF_1T2R == rf_type) || (RF_1T1R == rf_type))
		path_nums = 1;
	else
		path_nums = 2;

	for (path = 0; path < path_nums; path++) {
		DBG_8192D("\nRF_Path(%x)\n", path);
		for (i = 0; i < 0x100; i++) {
			value = rtw_hal_read_rfreg(padapter, path, i, 0xffffffff);
			if (j%4 == 1)
				DBG_8192D("0x%02x ", i);
			DBG_8192D(" 0x%08x ", value);
			if ((j++)%4 == 0)
				DBG_8192D("\n");
		}
	}
}

static int rtw_dbg_port(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	u8 major_cmd, minor_cmd;
	u16 arg;
	s32 extra_arg;
	u32 *pdata, val32;
	struct sta_info *psta;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct wlan_network *cur_network = &(pmlmepriv->cur_network);
	struct sta_priv *pstapriv = &padapter->stapriv;

	pdata = (u32*)&wrqu->data;

	val32 = *pdata;
	arg = (u16)(val32&0x0000ffff);
	major_cmd = (u8)(val32>>24);
	minor_cmd = (u8)((val32>>16)&0x00ff);

	extra_arg = *(pdata+1);

	switch (major_cmd)
	{
		case 0x70:/* read_reg */
			switch (minor_cmd)
			{
				case 1:
					DBG_8192D("rtw_read8(0x%x) = 0x%02x\n", arg, rtw_read8(padapter, arg));
					break;
				case 2:
					DBG_8192D("rtw_read16(0x%x) = 0x%04x\n", arg, rtw_read16(padapter, arg));
					break;
				case 4:
					DBG_8192D("rtw_read32(0x%x) = 0x%08x\n", arg, rtw_read32(padapter, arg));
					break;
			}
			break;
		case 0x71:/* write_reg */
			switch (minor_cmd)
			{
				case 1:
					rtw_write8(padapter, arg, extra_arg);
					DBG_8192D("rtw_write8(0x%x) = 0x%02x\n", arg, rtw_read8(padapter, arg));
					break;
				case 2:
					rtw_write16(padapter, arg, extra_arg);
					DBG_8192D("rtw_write16(0x%x) = 0x%04x\n", arg, rtw_read16(padapter, arg));
					break;
				case 4:
					rtw_write32(padapter, arg, extra_arg);
					DBG_8192D("rtw_write32(0x%x) = 0x%08x\n", arg, rtw_read32(padapter, arg));
					break;
			}
			break;
		case 0x72:/* read_bb */
			DBG_8192D("read_bbreg(0x%x) = 0x%x\n", arg, rtw_hal_read_bbreg(padapter, arg, 0xffffffff));
			break;
		case 0x73:/* write_bb */
			rtw_hal_write_bbreg(padapter, arg, 0xffffffff, extra_arg);
			DBG_8192D("write_bbreg(0x%x) = 0x%x\n", arg, rtw_hal_read_bbreg(padapter, arg, 0xffffffff));
			break;
		case 0x74:/* read_rf */
			DBG_8192D("read RF_reg path(0x%02x), offset(0x%x), value(0x%08x)\n", minor_cmd, arg, rtw_hal_read_rfreg(padapter, minor_cmd, arg, 0xffffffff));
			break;
		case 0x75:/* write_rf */
			rtw_hal_write_rfreg(padapter, minor_cmd, arg, 0xffffffff, extra_arg);
			DBG_8192D("write RF_reg path(0x%02x), offset(0x%x), value(0x%08x)\n", minor_cmd, arg, rtw_hal_read_rfreg(padapter, minor_cmd, arg, 0xffffffff));
			break;

		case 0x76:
			switch (minor_cmd)
			{
				case 0x00: /* normal mode, */
					padapter->recvpriv.is_signal_dbg = 0;
					break;
				case 0x01: /* dbg mode */
					padapter->recvpriv.is_signal_dbg = 1;
					extra_arg = extra_arg>100?100:extra_arg;
					extra_arg = extra_arg<0?0:extra_arg;
					padapter->recvpriv.signal_strength_dbg = extra_arg;
					break;
			}
			break;
		case 0x78: /* IOL test */
			break;
		case 0x7a:
			receive_disconnect(padapter, pmlmeinfo->network.MacAddress
				, WLAN_REASON_EXPIRATION_CHK);
			break;

		case 0x7F:
			switch (minor_cmd)
			{
				case 0x0:
					DBG_8192D("fwstate = 0x%x\n", get_fwstate(pmlmepriv));
					break;
				case 0x01:
					DBG_8192D("auth_alg = 0x%x, enc_alg = 0x%x, auth_type = 0x%x, enc_type = 0x%x\n",
						psecuritypriv->dot11AuthAlgrthm, psecuritypriv->dot11PrivacyAlgrthm,
						psecuritypriv->ndisauthtype, psecuritypriv->ndisencryptstatus);
					break;
				case 0x02:
					DBG_8192D("pmlmeinfo->state = 0x%x\n", pmlmeinfo->state);
					break;
				case 0x03:
					DBG_8192D("qos_option =%d\n", pmlmepriv->qospriv.qos_option);
					DBG_8192D("ht_option =%d\n", pmlmepriv->htpriv.ht_option);
					break;
				case 0x04:
					DBG_8192D("cur_ch =%d\n", pmlmeext->cur_channel);
					DBG_8192D("cur_bw =%d\n", pmlmeext->cur_bwmode);
					DBG_8192D("cur_ch_off =%d\n", pmlmeext->cur_ch_offset);
					break;
				case 0x05:
					psta = rtw_get_stainfo(pstapriv, cur_network->network.MacAddress);
					if (psta)
					{
						int i;
						struct recv_reorder_ctrl *preorder_ctrl;

						DBG_8192D("SSID =%s\n", cur_network->network.Ssid.Ssid);
						DBG_8192D("sta's macaddr:%pM\n", psta->hwaddr);
						DBG_8192D("cur_channel =%d, cur_bwmode =%d, cur_ch_offset =%d\n", pmlmeext->cur_channel, pmlmeext->cur_bwmode, pmlmeext->cur_ch_offset);
						DBG_8192D("rtsen =%d, cts2slef =%d\n", psta->rtsen, psta->cts2self);
						DBG_8192D("qos_en =%d, ht_en =%d, init_rate =%d\n", psta->qos_option, psta->htpriv.ht_option, psta->init_rate);
						DBG_8192D("state = 0x%x, aid =%d, macid =%d, raid =%d\n", psta->state, psta->aid, psta->mac_id, psta->raid);
						DBG_8192D("bwmode =%d, ch_offset =%d, sgi =%d\n", psta->htpriv.bwmode, psta->htpriv.ch_offset, psta->htpriv.sgi);
						DBG_8192D("ampdu_enable = %d\n", psta->htpriv.ampdu_enable);
						DBG_8192D("agg_enable_bitmap =%x, candidate_tid_bitmap =%x\n", psta->htpriv.agg_enable_bitmap, psta->htpriv.candidate_tid_bitmap);

						for (i = 0;i<16;i++)
						{
							preorder_ctrl = &psta->recvreorder_ctrl[i];
							if (preorder_ctrl->enable)
							{
								DBG_8192D("tid =%d, indicate_seq =%d\n", i, preorder_ctrl->indicate_seq);
							}
						}

					}
					else
					{
						DBG_8192D("can't get sta's macaddr, cur_network's macaddr:%pM\n", cur_network->network.MacAddress);
					}
					break;
				case 0x06:
					{
						u8	DMFlag;
						rtw_hal_get_hwreg(padapter, HW_VAR_DM_FLAG, (u8 *)(&DMFlag));
						DBG_8192D("(B)DMFlag = 0x%x, arg = 0x%x\n", DMFlag, arg);
						DMFlag = (u8)(0x0f&arg);
						DBG_8192D("(A)DMFlag = 0x%x\n", DMFlag);
						rtw_hal_set_hwreg(padapter, HW_VAR_DM_FLAG, (u8 *)(&DMFlag));
					}
					break;
				case 0x07:
					DBG_8192D("bSurpriseRemoved =%d, bDriverStopped =%d\n",
						padapter->bSurpriseRemoved, padapter->bDriverStopped);
					break;
				case 0x08: {
					struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
					struct recv_priv  *precvpriv = &padapter->recvpriv;

					DBG_8192D("free_xmitbuf_cnt =%d, free_xmitframe_cnt =%d, free_xmit_extbuf_cnt =%d\n",
						pxmitpriv->free_xmitbuf_cnt, pxmitpriv->free_xmitframe_cnt, pxmitpriv->free_xmit_extbuf_cnt);
					DBG_8192D("rx_urb_pending_cn =%d\n", precvpriv->rx_pending_cnt);
					break; }
				case 0x09:
					{
						int i, j;
						struct list_head *plist, *phead;
						struct recv_reorder_ctrl *preorder_ctrl;

#ifdef CONFIG_92D_AP_MODE
						DBG_8192D("sta_dz_bitmap = 0x%x, tim_bitmap = 0x%x\n", pstapriv->sta_dz_bitmap, pstapriv->tim_bitmap);
#endif
						spin_lock_bh(&pstapriv->sta_hash_lock);

						for (i = 0; i< NUM_STA; i++)
						{
							phead = &(pstapriv->sta_hash[i]);
							plist = phead->next;

							while ((rtw_end_of_queue_search(phead, plist)) == false)
							{
								psta = container_of(plist, struct sta_info, hash_list);

								plist = plist->next;

								if (extra_arg == psta->aid)
								{
									DBG_8192D("sta's macaddr:%pM\n", psta->hwaddr);
									DBG_8192D("rtsen =%d, cts2slef =%d\n", psta->rtsen, psta->cts2self);
									DBG_8192D("qos_en =%d, ht_en =%d, init_rate =%d\n", psta->qos_option, psta->htpriv.ht_option, psta->init_rate);
									DBG_8192D("state = 0x%x, aid =%d, macid =%d, raid =%d\n", psta->state, psta->aid, psta->mac_id, psta->raid);
									DBG_8192D("bwmode =%d, ch_offset =%d, sgi =%d\n", psta->htpriv.bwmode, psta->htpriv.ch_offset, psta->htpriv.sgi);
									DBG_8192D("ampdu_enable = %d\n", psta->htpriv.ampdu_enable);
									DBG_8192D("agg_enable_bitmap =%x, candidate_tid_bitmap =%x\n", psta->htpriv.agg_enable_bitmap, psta->htpriv.candidate_tid_bitmap);
#ifdef CONFIG_92D_AP_MODE
									DBG_8192D("capability = 0x%x\n", psta->capability);
									DBG_8192D("flags = 0x%x\n", psta->flags);
									DBG_8192D("wpa_psk = 0x%x\n", psta->wpa_psk);
									DBG_8192D("wpa2_group_cipher = 0x%x\n", psta->wpa2_group_cipher);
									DBG_8192D("wpa2_pairwise_cipher = 0x%x\n", psta->wpa2_pairwise_cipher);
									DBG_8192D("qos_info = 0x%x\n", psta->qos_info);
#endif
									DBG_8192D("dot118021XPrivacy = 0x%x\n", psta->dot118021XPrivacy);

									for (j = 0;j<16;j++)
									{
										preorder_ctrl = &psta->recvreorder_ctrl[j];
										if (preorder_ctrl->enable)
										{
											DBG_8192D("tid =%d, indicate_seq =%d\n", j, preorder_ctrl->indicate_seq);
										}
									}

								}

							}
						}

						spin_unlock_bh(&pstapriv->sta_hash_lock);

					}
					break;

				case 0x0c:/* dump rx packet */
					DBG_8192D("dump rx packet (%d)\n", extra_arg);
					/* pHalData->bDumpRxPkt = extra_arg; */
					rtw_hal_set_def_var(padapter, HAL_DEF_DBG_DUMP_RXPKT, &(extra_arg));
					break;
		#ifdef DBG_CONFIG_ERROR_DETECT
				case 0x0f:
						{
							if (extra_arg == 0) {
								DBG_8192D("###### silent reset test.......#####\n");
								rtw_hal_sreset_reset(padapter);
							}

						}
				break;
				case 0x15:
					{
						struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
						DBG_8192D("==>silent resete cnts:%d\n", pwrpriv->ips_enter_cnts);
					}
					break;

		#endif

				case 0x10:/*  driver version display */
					DBG_8192D("rtw driver version =%s\n", DRIVERVERSION);
					break;
				case 0x11:
					{
						DBG_8192D("turn %s Rx RSSI display function\n", (extra_arg == 1)?"on":"off");
						padapter->bRxRSSIDisplay = extra_arg ;
					}
					break;
				case 0x12: /* set rx_stbc */
				{
					struct registry_priv	*pregpriv = &padapter->registrypriv;
					/*  0: disable, bit(0):enable 2.4g, bit(1):enable 5g, 0x3: enable both 2.4g and 5g */
					/* default is set to enable 2.4GHZ for IOT issue with bufflao's AP at 5GHZ */
					if (pregpriv && (extra_arg == 0 || extra_arg == 1|| extra_arg == 2 || extra_arg == 3))
					{
						pregpriv->rx_stbc = extra_arg;
						DBG_8192D("set rx_stbc =%d\n", pregpriv->rx_stbc);
					}
					else
						DBG_8192D("get rx_stbc =%d\n", pregpriv->rx_stbc);

				}
				break;
				case 0x13: /* set ampdu_enable */
				{
					struct registry_priv	*pregpriv = &padapter->registrypriv;
					/*  0: disable, 0x1:enable (but wifi_spec should be 0), 0x2: force enable (don't care wifi_spec) */
					if (pregpriv && extra_arg >= 0 && extra_arg < 3)
					{
						pregpriv->ampdu_enable = extra_arg;
						DBG_8192D("set ampdu_enable =%d\n", pregpriv->ampdu_enable);
					}
					else
						DBG_8192D("get ampdu_enable =%d\n", pregpriv->ampdu_enable);

				}
				break;
				case 0x14: /* get wifi_spec */
				{
					struct registry_priv	*pregpriv = &padapter->registrypriv;
					DBG_8192D("get wifi_spec =%d\n", pregpriv->wifi_spec);

				}
				break;
				case 0x22:
					{
						DBG_8192D("turn %s the ForceWriteInitGain Variable\n", (extra_arg == 1)?"on":"off");
						padapter->bForceWriteInitGain = extra_arg;
						break;
					}
				case 0x23:
					{
						DBG_8192D("turn %s the bNotifyChannelChange Variable\n", (extra_arg == 1)?"on":"off");
						padapter->bNotifyChannelChange = extra_arg;
						break;
					}
				case 0x24:
					break;
				case 0xdd:/* registers dump , 0 for mac reg, 1 for bb reg, 2 for rf reg */
					if (extra_arg == 0) {
						mac_reg_dump(padapter);
					}
					else if (extra_arg == 1) {
						bb_reg_dump(padapter);
					}
					else if (extra_arg == 2) {
						rf_reg_dump(padapter);
					}

					break;
				case 0xee:/* turn on/off dynamic funcs */
					{
						u8 dm_flag;

						if (0xf == extra_arg) {
							rtw_hal_get_def_var(padapter, HAL_DEF_DBG_DM_FUNC,&dm_flag);
							DBG_8192D(" === DMFlag(0x%02x) ===\n", dm_flag);
							DBG_8192D("extra_arg = 0  - disable all dynamic func\n");
							DBG_8192D("extra_arg = 1  - disable DIG- BIT(0)\n");
							DBG_8192D("extra_arg = 2  - disable High power - BIT(1)\n");
							DBG_8192D("extra_arg = 3  - disable tx power tracking - BIT(2)\n");
							DBG_8192D("extra_arg = 4  - disable BT coexistence - BIT(3)\n");
							DBG_8192D("extra_arg = 5  - disable antenna diversity - BIT(4)\n");
							DBG_8192D("extra_arg = 6  - enable all dynamic func\n");
						}
						else {
							/*	extra_arg = 0  - disable all dynamic func
								extra_arg = 1  - disable DIG
								extra_arg = 2  - disable tx power tracking
								extra_arg = 3  - turn on all dynamic func
							*/
							rtw_hal_set_def_var(padapter, HAL_DEF_DBG_DM_FUNC, &(extra_arg));
							rtw_hal_get_def_var(padapter, HAL_DEF_DBG_DM_FUNC,&dm_flag);
							DBG_8192D(" === DMFlag(0x%02x) ===\n", dm_flag);
						}
					}
					break;

				case 0xfd:
					rtw_write8(padapter, 0xc50, arg);
					DBG_8192D("wr(0xc50) = 0x%x\n", rtw_read8(padapter, 0xc50));
					rtw_write8(padapter, 0xc58, arg);
					DBG_8192D("wr(0xc58) = 0x%x\n", rtw_read8(padapter, 0xc58));
					break;
				case 0xfe:
					DBG_8192D("rd(0xc50) = 0x%x\n", rtw_read8(padapter, 0xc50));
					DBG_8192D("rd(0xc58) = 0x%x\n", rtw_read8(padapter, 0xc58));
					break;
				case 0xff:
					{
						DBG_8192D("dbg(0x210) = 0x%x\n", rtw_read32(padapter, 0x210));
						DBG_8192D("dbg(0x608) = 0x%x\n", rtw_read32(padapter, 0x608));
						DBG_8192D("dbg(0x280) = 0x%x\n", rtw_read32(padapter, 0x280));
						DBG_8192D("dbg(0x284) = 0x%x\n", rtw_read32(padapter, 0x284));
						DBG_8192D("dbg(0x288) = 0x%x\n", rtw_read32(padapter, 0x288));

						DBG_8192D("dbg(0x664) = 0x%x\n", rtw_read32(padapter, 0x664));

						DBG_8192D("\n");

						DBG_8192D("dbg(0x430) = 0x%x\n", rtw_read32(padapter, 0x430));
						DBG_8192D("dbg(0x438) = 0x%x\n", rtw_read32(padapter, 0x438));

						DBG_8192D("dbg(0x440) = 0x%x\n", rtw_read32(padapter, 0x440));

						DBG_8192D("dbg(0x458) = 0x%x\n", rtw_read32(padapter, 0x458));

						DBG_8192D("dbg(0x484) = 0x%x\n", rtw_read32(padapter, 0x484));
						DBG_8192D("dbg(0x488) = 0x%x\n", rtw_read32(padapter, 0x488));

						DBG_8192D("dbg(0x444) = 0x%x\n", rtw_read32(padapter, 0x444));
						DBG_8192D("dbg(0x448) = 0x%x\n", rtw_read32(padapter, 0x448));
						DBG_8192D("dbg(0x44c) = 0x%x\n", rtw_read32(padapter, 0x44c));
						DBG_8192D("dbg(0x450) = 0x%x\n", rtw_read32(padapter, 0x450));
					}
					break;
			}
			break;
		default:
			DBG_8192D("error dbg cmd!\n");
			break;
	}

	return ret;
}

static int wpa_set_param(struct net_device *dev, u8 name, u32 value)
{
	uint ret = 0;
	u32 flags;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);

	switch (name) {
	case IEEE_PARAM_WPA_ENABLED:

		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_8021X; /* 802.1x */

		/* ret = ieee80211_wpa_enable(ieee, value); */

		switch ((value)&0xff)
		{
			case 1 : /* WPA */
			padapter->securitypriv.ndisauthtype = NDIS802_11AUTHMODEWPAPSK; /* WPA_PSK */
			padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION2ENABLED;
				break;
			case 2: /* WPA2 */
			padapter->securitypriv.ndisauthtype = NDIS802_11AUTHMODEWPA2PSK; /* WPA2_PSK */
			padapter->securitypriv.ndisencryptstatus = NDIS802_11ENCRYPTION3ENABLED;
				break;
		}

		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_, ("wpa_set_param:padapter->securitypriv.ndisauthtype =%d\n", padapter->securitypriv.ndisauthtype));

		break;

	case IEEE_PARAM_TKIP_COUNTERMEASURES:
		/* ieee->tkip_countermeasures = value; */
		break;

	case IEEE_PARAM_DROP_UNENCRYPTED:
	{
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

		/* ieee->privacy_invoked = value; */

		break;

	case IEEE_PARAM_AUTH_ALGS:

		ret = wpa_set_auth_algs(dev, value);

		break;

	case IEEE_PARAM_IEEE_802_1X:

		/* ieee->ieee802_1x = value; */

		break;

	case IEEE_PARAM_WPAX_SELECT:

		/*  added for WPA2 mixed mode */
		/* DBG_8192D(KERN_WARNING "------------------------>wpax value = %x\n", value); */
		/*
		spin_lock_irqsave(&ieee->wpax_suitlist_lock, flags);
		ieee->wpax_type_set = 1;
		ieee->wpax_type_notify = value;
		spin_unlock_irqrestore(&ieee->wpax_suitlist_lock, flags);
		*/

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
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);

	switch (command)
	{
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

	/* down(&ieee->wx_sem); */

	if (p->length < sizeof(struct ieee_param) || !p->pointer) {
		ret = -EINVAL;
		goto out;
	}

	param = (struct ieee_param *)kmalloc(p->length, GFP_KERNEL);
	if (param == NULL)
	{
		ret = -ENOMEM;
		goto out;
	}

	if (copy_from_user(param, p->pointer, p->length))
	{
		kfree(param);
		ret = -EFAULT;
		goto out;
	}

	switch (param->cmd) {

	case IEEE_CMD_SET_WPA_PARAM:
		ret = wpa_set_param(dev, param->u.wpa_param.name, param->u.wpa_param.value);
		break;

	case IEEE_CMD_SET_WPA_IE:
		/* ret = wpa_set_wpa_ie(dev, param, p->length); */
		ret =  rtw_set_wpa_ie((struct rtw_adapter *)rtw_netdev_priv(dev), (char *)param->u.wpa_ie.data, (u16)param->u.wpa_ie.len);
		break;

	case IEEE_CMD_SET_ENCRYPTION:
		ret = wpa_set_encryption(dev, param, p->length);
		break;

	case IEEE_CMD_MLME:
		ret = wpa_mlme(dev, param->u.mlme.command, param->u.mlme.reason_code);
		break;

	default:
		DBG_8192D("Unknown WPA supplicant request: %d\n", param->cmd);
		ret = -EOPNOTSUPP;
		break;

	}

	if (ret == 0 && copy_to_user(p->pointer, param, p->length))
		ret = -EFAULT;

	kfree(param);

out:

	/* up(&ieee->wx_sem); */

	return ret;
}

#ifdef CONFIG_92D_AP_MODE
static u8 set_pairwise_key(struct rtw_adapter *padapter, struct sta_info *psta)
{
	struct cmd_obj*			ph2c;
	struct set_stakey_parm	*psetstakey_para;
	struct cmd_priv				*pcmdpriv =&padapter->cmdpriv;
	u8	res = _SUCCESS;

	ph2c = (struct cmd_obj*)kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
	if (ph2c == NULL) {
		res = _FAIL;
		goto exit;
	}

	psetstakey_para = (struct set_stakey_parm*)kzalloc(sizeof(struct set_stakey_parm), GFP_KERNEL);
	if (psetstakey_para == NULL) {
		kfree(ph2c);
		res = _FAIL;
		goto exit;
	}

	init_h2fwcmd_w_parm_no_rsp(ph2c, psetstakey_para, _SETSTAKEY_CMD_);

	psetstakey_para->algorithm = (u8)psta->dot118021XPrivacy;

	memcpy(psetstakey_para->addr, psta->hwaddr, ETH_ALEN);

	memcpy(psetstakey_para->key, &psta->dot118021x_UncstKey, 16);

	res = rtw_enqueue_cmd(pcmdpriv, ph2c);

exit:

	return res;
}

static int set_group_key(struct rtw_adapter *padapter, u8 *key, u8 alg, int keyid)
{
	u8 keylen;
	struct cmd_obj* pcmd;
	struct setkey_parm *psetkeyparm;
	struct cmd_priv	*pcmdpriv =&(padapter->cmdpriv);
	int res = _SUCCESS;

	DBG_8192D("%s\n", __func__);

	pcmd = (struct cmd_obj*)kzalloc(sizeof(struct cmd_obj), GFP_KERNEL);
	if (pcmd == NULL) {
		res = _FAIL;
		goto exit;
	}
	psetkeyparm = (struct setkey_parm*)kzalloc(sizeof(struct setkey_parm), GFP_KERNEL);
	if (psetkeyparm == NULL) {
		kfree(pcmd);
		res = _FAIL;
		goto exit;
	}

	memset(psetkeyparm, 0, sizeof(struct setkey_parm));

	psetkeyparm->keyid = (u8)keyid;

	psetkeyparm->algorithm = alg;

	psetkeyparm->set_tx = 1;

	switch (alg)
	{
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

	pcmd->cmdcode = _SETKEY_CMD_;
	pcmd->parmbuf = (u8 *)psetkeyparm;
	pcmd->cmdsz =  (sizeof(struct setkey_parm));
	pcmd->rsp = NULL;
	pcmd->rspsz = 0;

	INIT_LIST_HEAD(&pcmd->list);

	res = rtw_enqueue_cmd(pcmdpriv, pcmd);

exit:

	return res;

}

static int set_wep_key(struct rtw_adapter *padapter, u8 *key, u8 keylen, int keyid)
{
	u8 alg;

	switch (keylen)
	{
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
	struct ndis_802_11_wep *pwep = NULL;
	struct sta_info *psta = NULL, *pbcmc_sta = NULL;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct security_priv* psecuritypriv =&(padapter->securitypriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_8192D("%s\n", __func__);

	param->u.crypt.err = 0;
	param->u.crypt.alg[IEEE_CRYPT_ALG_NAME_LEN - 1] = '\0';

	/* sizeof(struct ieee_param) = 64 bytes; */
	/* if (param_len !=  (u32) ((u8 *) param->u.crypt.key - (u8 *) param) + param->u.crypt.key_len) */
	if (param_len !=  sizeof(struct ieee_param) + param->u.crypt.key_len)
	{
		ret =  -EINVAL;
		goto exit;
	}

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff)
	{
		if (param->u.crypt.idx >= WEP_KEYS)
		{
			ret = -EINVAL;
			goto exit;
		}
	}
	else
	{
		psta = rtw_get_stainfo(pstapriv, param->sta_addr);
		if (!psta)
		{
			/* ret = -EINVAL; */
			DBG_8192D("rtw_set_encryption(), sta has already been removed or never been added\n");
			goto exit;
		}
	}

	if (strcmp(param->u.crypt.alg, "none") == 0 && (psta == NULL))
	{
		/* todo:clear default encryption keys */

		DBG_8192D("clear default encryption keys, keyid =%d\n", param->u.crypt.idx);

		goto exit;
	}

	if (strcmp(param->u.crypt.alg, "WEP") == 0 && (psta == NULL))
	{
		DBG_8192D("r871x_set_encryption, crypt.alg = WEP\n");

		wep_key_idx = param->u.crypt.idx;
		wep_key_len = param->u.crypt.key_len;

		DBG_8192D("r871x_set_encryption, wep_key_idx =%d, len =%d\n", wep_key_idx, wep_key_len);

		if ((wep_key_idx >= WEP_KEYS) || (wep_key_len<= 0))
		{
			ret = -EINVAL;
			goto exit;
		}

		if (wep_key_len > 0)
		{
			wep_key_len = wep_key_len <= 5 ? 5 : 13;
			wep_total_len = wep_key_len + FIELD_OFFSET(struct ndis_802_11_wep, KeyMaterial);
			pwep = (struct ndis_802_11_wep *)kmalloc(wep_total_len, GFP_KERNEL);
			if (pwep == NULL) {
				DBG_8192D(" r871x_set_encryption: pwep allocate fail !!!\n");
				goto exit;
			}

			memset(pwep, 0, wep_total_len);

			pwep->KeyLength = wep_key_len;
			pwep->Length = wep_total_len;

		}

		pwep->KeyIndex = wep_key_idx;

		memcpy(pwep->KeyMaterial,  param->u.crypt.key, pwep->KeyLength);

		if (param->u.crypt.set_tx)
		{
			DBG_8192D("wep, set_tx = 1\n");

			psecuritypriv->ndisencryptstatus = NDIS802_11ENCRYPTION1ENABLED;
			psecuritypriv->dot11PrivacyAlgrthm = _WEP40_;
			psecuritypriv->dot118021XGrpPrivacy = _WEP40_;

			if (pwep->KeyLength == 13)
			{
				psecuritypriv->dot11PrivacyAlgrthm = _WEP104_;
				psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
			}

			psecuritypriv->dot11PrivacyKeyIndex = wep_key_idx;

			memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), pwep->KeyMaterial, pwep->KeyLength);

			psecuritypriv->dot11DefKeylen[wep_key_idx]= pwep->KeyLength;

			set_wep_key(padapter, pwep->KeyMaterial, pwep->KeyLength, wep_key_idx);

		}
		else
		{
			DBG_8192D("wep, set_tx = 0\n");

			/* don't update "psecuritypriv->dot11PrivacyAlgrthm" and */
			/* psecuritypriv->dot11PrivacyKeyIndex = keyid", but can rtw_set_key to cam */

		      memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), pwep->KeyMaterial, pwep->KeyLength);

			psecuritypriv->dot11DefKeylen[wep_key_idx] = pwep->KeyLength;

			set_wep_key(padapter, pwep->KeyMaterial, pwep->KeyLength, wep_key_idx);

		}

		goto exit;

	}

	if (!psta && check_fwstate(pmlmepriv, WIFI_AP_STATE)) /*  group key */
	{
		if (param->u.crypt.set_tx == 1)
		{
			if (strcmp(param->u.crypt.alg, "WEP") == 0)
			{
				DBG_8192D("%s, set group_key, WEP\n", __func__);

				memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));

				psecuritypriv->dot118021XGrpPrivacy = _WEP40_;
				if (param->u.crypt.key_len == 13)
				{
						psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
				}

			}
			else if (strcmp(param->u.crypt.alg, "TKIP") == 0)
			{
				DBG_8192D("%s, set group_key, TKIP\n", __func__);

				psecuritypriv->dot118021XGrpPrivacy = _TKIP_;

				memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));

				/* DEBUG_ERR("set key length :param->u.crypt.key_len =%d\n", param->u.crypt.key_len); */
				/* set mic key */
				memcpy(psecuritypriv->dot118021XGrptxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[16]), 8);
				memcpy(psecuritypriv->dot118021XGrprxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[24]), 8);

				psecuritypriv->busetkipkey = true;

			}
			else if (strcmp(param->u.crypt.alg, "CCMP") == 0)
			{
				DBG_8192D("%s, set group_key, CCMP\n", __func__);

				psecuritypriv->dot118021XGrpPrivacy = _AES_;

				memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
			}
			else
			{
				DBG_8192D("%s, set group_key, none\n", __func__);

				psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
			}

			psecuritypriv->dot118021XGrpKeyid = param->u.crypt.idx;

			psecuritypriv->binstallGrpkey = true;

			psecuritypriv->dot11PrivacyAlgrthm = psecuritypriv->dot118021XGrpPrivacy;/*  */

			set_group_key(padapter, param->u.crypt.key, psecuritypriv->dot118021XGrpPrivacy, param->u.crypt.idx);

			pbcmc_sta = rtw_get_bcmc_stainfo(padapter);
			if (pbcmc_sta)
			{
				pbcmc_sta->ieee8021x_blocked = false;
				pbcmc_sta->dot118021XPrivacy = psecuritypriv->dot118021XGrpPrivacy;/* rx will use bmc_sta's dot118021XPrivacy */
			}

		}

		goto exit;

	}

	if (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X && psta) /*  psk/802_1x */
	{
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE))
		{
			if (param->u.crypt.set_tx == 1)
			{
				memcpy(psta->dot118021x_UncstKey.skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));

				if (strcmp(param->u.crypt.alg, "WEP") == 0)
				{
					DBG_8192D("%s, set pairwise key, WEP\n", __func__);

					psta->dot118021XPrivacy = _WEP40_;
					if (param->u.crypt.key_len == 13)
					{
						psta->dot118021XPrivacy = _WEP104_;
					}
				}
				else if (strcmp(param->u.crypt.alg, "TKIP") == 0)
				{
					DBG_8192D("%s, set pairwise key, TKIP\n", __func__);

					psta->dot118021XPrivacy = _TKIP_;

					/* DEBUG_ERR("set key length :param->u.crypt.key_len =%d\n", param->u.crypt.key_len); */
					/* set mic key */
					memcpy(psta->dot11tkiptxmickey.skey, &(param->u.crypt.key[16]), 8);
					memcpy(psta->dot11tkiprxmickey.skey, &(param->u.crypt.key[24]), 8);

					psecuritypriv->busetkipkey = true;

				}
				else if (strcmp(param->u.crypt.alg, "CCMP") == 0)
				{

					DBG_8192D("%s, set pairwise key, CCMP\n", __func__);

					psta->dot118021XPrivacy = _AES_;
				}
				else
				{
					DBG_8192D("%s, set pairwise key, none\n", __func__);

					psta->dot118021XPrivacy = _NO_PRIVACY_;
				}

				set_pairwise_key(padapter, psta);

				psta->ieee8021x_blocked = false;

			}
			else/* group key??? */
			{
				if (strcmp(param->u.crypt.alg, "WEP") == 0)
				{
					memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));

					psecuritypriv->dot118021XGrpPrivacy = _WEP40_;
					if (param->u.crypt.key_len == 13)
					{
						psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
					}
				}
				else if (strcmp(param->u.crypt.alg, "TKIP") == 0)
				{
					psecuritypriv->dot118021XGrpPrivacy = _TKIP_;

					memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));

					/* DEBUG_ERR("set key length :param->u.crypt.key_len =%d\n", param->u.crypt.key_len); */
					/* set mic key */
					memcpy(psecuritypriv->dot118021XGrptxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[16]), 8);
					memcpy(psecuritypriv->dot118021XGrprxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[24]), 8);

					psecuritypriv->busetkipkey = true;

				}
				else if (strcmp(param->u.crypt.alg, "CCMP") == 0)
				{
					psecuritypriv->dot118021XGrpPrivacy = _AES_;

					memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
				}
				else
				{
					psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
				}

				psecuritypriv->dot118021XGrpKeyid = param->u.crypt.idx;

				psecuritypriv->binstallGrpkey = true;

				psecuritypriv->dot11PrivacyAlgrthm = psecuritypriv->dot118021XGrpPrivacy;/*  */

				set_group_key(padapter, param->u.crypt.key, psecuritypriv->dot118021XGrpPrivacy, param->u.crypt.idx);

				pbcmc_sta = rtw_get_bcmc_stainfo(padapter);
				if (pbcmc_sta)
				{
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
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;
	unsigned char *pbuf = param->u.bcn_ie.buf;

	DBG_8192D("%s, len =%d\n", __func__, len);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	memcpy(&pstapriv->max_num_sta, param->u.bcn_ie.reserved, 2);

	if ((pstapriv->max_num_sta>NUM_STA) || (pstapriv->max_num_sta<= 0))
		pstapriv->max_num_sta = NUM_STA;

	if (rtw_check_beacon_data(padapter, pbuf,  (len-12-2)) == _SUCCESS)/*  12 = param header, 2:no packed */
		ret = 0;
	else
		ret = -EINVAL;

	return ret;
}

static int rtw_hostapd_sta_flush(struct net_device *dev)
{
	int ret = 0;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);

	DBG_8192D("%s\n", __func__);

	flush_all_cam_entry(padapter);	/* clear CAM */

	ret = rtw_sta_flush(padapter);

	return ret;
}

static int rtw_add_sta(struct net_device *dev, struct ieee_param *param)
{
	int ret = 0;
	struct sta_info *psta = NULL;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_8192D("rtw_add_sta(aid =%d) =%pM\n", param->u.add_sta.aid, param->sta_addr);

	if (check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)) != true)
	{
		return -EINVAL;
	}

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff)
	{
		return -EINVAL;
	}

/*
	psta = rtw_get_stainfo(pstapriv, param->sta_addr);
	if (psta)
	{
		DBG_8192D("rtw_add_sta(), free has been added psta =%p\n", psta);
		spin_lock_bh(&(pstapriv->sta_hash_lock));
		rtw_free_stainfo(padapter,  psta);
		spin_unlock_bh(&(pstapriv->sta_hash_lock));

		psta = NULL;
	}
*/
	/* psta = rtw_alloc_stainfo(pstapriv, param->sta_addr); */
	psta = rtw_get_stainfo(pstapriv, param->sta_addr);
	if (psta)
	{
		int flags = param->u.add_sta.flags;

		/* DBG_8192D("rtw_add_sta(), init sta's variables, psta =%p\n", psta); */

		psta->aid = param->u.add_sta.aid;/* aid = 1~2007 */

		memcpy(psta->bssrateset, param->u.add_sta.tx_supp_rates, 16);

		/* check wmm cap. */
		if (WLAN_STA_WME&flags)
			psta->qos_option = 1;
		else
			psta->qos_option = 0;

		if (pmlmepriv->qospriv.qos_option == 0)
			psta->qos_option = 0;

#ifdef CONFIG_80211N_HT
		/* chec 802.11n ht cap. */
		if (WLAN_STA_HT&flags)
		{
			psta->htpriv.ht_option = true;
			psta->qos_option = 1;
			memcpy((void*)&psta->htpriv.ht_cap, (void*)&param->u.add_sta.ht_cap, sizeof(struct rtw_ieee80211_ht_cap));
		}
		else
		{
			psta->htpriv.ht_option = false;
		}

		if (pmlmepriv->htpriv.ht_option == false)
			psta->htpriv.ht_option = false;
#endif

		update_sta_info_apmode(padapter, psta);

	}
	else
	{
		ret = -ENOMEM;
	}

	return ret;
}

static int rtw_del_sta(struct net_device *dev, struct ieee_param *param)
{
	int ret = 0;
	struct sta_info *psta = NULL;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_8192D("rtw_del_sta =%pM\n", param->sta_addr);

	if (check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)) != true)
		return -EINVAL;

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff)
		return -EINVAL;

	psta = rtw_get_stainfo(pstapriv, param->sta_addr);
	if (psta) {
		u8 updated;

		/* DBG_8192D("free psta =%p, aid =%d\n", psta, psta->aid); */

		spin_lock_bh(&pstapriv->asoc_list_lock);
		if (rtw_is_list_empty(&psta->asoc_list) == false)
		{
			list_del_init(&psta->asoc_list);
			pstapriv->asoc_list_cnt--;
			updated = ap_free_sta(padapter, psta, true, WLAN_REASON_DEAUTH_LEAVING);

		}
		spin_unlock_bh(&pstapriv->asoc_list_lock);

		associated_clients_update(padapter, updated);

		psta = NULL;

	}
	else
	{
		DBG_8192D("rtw_del_sta(), sta has already been removed or never been added\n");

		/* ret = -1; */
	}

	return ret;
}

static int rtw_ioctl_get_sta_data(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret = 0;
	struct sta_info *psta = NULL;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct ieee_param_ex *param_ex = (struct ieee_param_ex *)param;
	struct sta_data *psta_data = (struct sta_data *)param_ex->data;

	DBG_8192D("rtw_ioctl_get_sta_info, sta_addr: %pM\n", param_ex->sta_addr);

	if (check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)) != true)
	{
		return -EINVAL;
	}

	if (param_ex->sta_addr[0] == 0xff && param_ex->sta_addr[1] == 0xff &&
	    param_ex->sta_addr[2] == 0xff && param_ex->sta_addr[3] == 0xff &&
	    param_ex->sta_addr[4] == 0xff && param_ex->sta_addr[5] == 0xff)
	{
		return -EINVAL;
	}

	psta = rtw_get_stainfo(pstapriv, param_ex->sta_addr);
	if (psta)
	{
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
							(psta->no_short_slot_time_set <<1) |
							(psta->no_short_preamble_set <<2) |
							(psta->no_ht_gf_set <<3) |
							(psta->no_ht_set <<4) |
							(psta->ht_20mhz_set <<5));

		psta_data->tx_supp_rates_len =  psta->bssratelen;
		memcpy(psta_data->tx_supp_rates, psta->bssrateset, psta->bssratelen);

		memcpy(&psta_data->ht_cap, &psta->htpriv.ht_cap, sizeof(struct rtw_ieee80211_ht_cap));

		psta_data->rx_pkts = psta->sta_stats.rx_data_pkts;
		psta_data->rx_bytes = psta->sta_stats.rx_bytes;
		psta_data->rx_drops = psta->sta_stats.rx_drops;

		psta_data->tx_pkts = psta->sta_stats.tx_pkts;
		psta_data->tx_bytes = psta->sta_stats.tx_bytes;
		psta_data->tx_drops = psta->sta_stats.tx_drops;

	}
	else
	{
		ret = -1;
	}

	return ret;
}

static int rtw_get_sta_wpaie(struct net_device *dev, struct ieee_param *param)
{
	int ret = 0;
	struct sta_info *psta = NULL;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_8192D("rtw_get_sta_wpaie, sta_addr: %pM\n", param->sta_addr);

	if (check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)) != true)
	{
		return -EINVAL;
	}

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff)
	{
		return -EINVAL;
	}

	psta = rtw_get_stainfo(pstapriv, param->sta_addr);
	if (psta)
	{
		if ((psta->wpa_ie[0] == WLAN_EID_RSN) || (psta->wpa_ie[0] == WLAN_EID_GENERIC))
		{
			int wpa_ie_len;
			int copy_len;

			wpa_ie_len = psta->wpa_ie[1];

			copy_len = ((wpa_ie_len+2) > sizeof(psta->wpa_ie)) ? (sizeof(psta->wpa_ie)):(wpa_ie_len+2);

			param->u.wpa_ie.len = copy_len;

			memcpy(param->u.wpa_ie.reserved, psta->wpa_ie, copy_len);
		}
		else
		{
			/* ret = -1; */
			DBG_8192D("sta's wpa_ie is NONE\n");
		}
	}
	else
	{
		ret = -1;
	}

	return ret;
}

static int rtw_set_wps_beacon(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret = 0;
	unsigned char wps_oui[4]={0x0, 0x50, 0xf2, 0x04};
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	int ie_len;

	DBG_8192D("%s, len =%d\n", __func__, len);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	ie_len = len-12-2;/*  12 = param header, 2:no packed */

	kfree(pmlmepriv->wps_beacon_ie);
	pmlmepriv->wps_beacon_ie = NULL;

	if (ie_len > 0) {
		pmlmepriv->wps_beacon_ie = kmalloc(ie_len, GFP_KERNEL);
		pmlmepriv->wps_beacon_ie_len = ie_len;
		if (pmlmepriv->wps_beacon_ie == NULL) {
			DBG_8192D("%s()-%d: kmalloc() ERROR!\n", __func__, __LINE__);
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
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	int ie_len;

	DBG_8192D("%s, len =%d\n", __func__, len);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	ie_len = len-12-2;/*  12 = param header, 2:no packed */

	kfree(pmlmepriv->wps_probe_resp_ie);
	pmlmepriv->wps_probe_resp_ie = NULL;

	if (ie_len > 0) {
		pmlmepriv->wps_probe_resp_ie = kmalloc(ie_len, GFP_KERNEL);
		pmlmepriv->wps_probe_resp_ie_len = ie_len;
		if (pmlmepriv->wps_probe_resp_ie == NULL) {
			DBG_8192D("%s()-%d: kmalloc() ERROR!\n", __func__, __LINE__);
			return -EINVAL;
		}
		memcpy(pmlmepriv->wps_probe_resp_ie, param->u.bcn_ie.buf, ie_len);
	}
	return ret;
}

static int rtw_set_wps_assoc_resp(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret = 0;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	int ie_len;

	DBG_8192D("%s, len =%d\n", __func__, len);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	ie_len = len-12-2;/*  12 = param header, 2:no packed */

	kfree(pmlmepriv->wps_assoc_resp_ie);
	pmlmepriv->wps_assoc_resp_ie = NULL;

	if (ie_len > 0) {
		pmlmepriv->wps_assoc_resp_ie = kmalloc(ie_len, GFP_KERNEL);
		pmlmepriv->wps_assoc_resp_ie_len = ie_len;
		if (pmlmepriv->wps_assoc_resp_ie == NULL) {
			DBG_8192D("%s()-%d: kmalloc() ERROR!\n", __func__, __LINE__);
			return -EINVAL;
		}

		memcpy(pmlmepriv->wps_assoc_resp_ie, param->u.bcn_ie.buf, ie_len);
	}
	return ret;
}

static int rtw_set_hidden_ssid(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret = 0;
	struct rtw_adapter *adapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *mlmepriv = &(adapter->mlmepriv);
	struct mlme_ext_priv	*mlmeext = &(adapter->mlmeextpriv);
	struct mlme_ext_info	*mlmeinfo = &(mlmeext->mlmext_info);
	int ie_len;
	u8 *ssid_ie;
	char ssid[NDIS_802_11_LENGTH_SSID + 1];
	int ssid_len;
	u8 ignore_broadcast_ssid;

	if (check_fwstate(mlmepriv, WIFI_AP_STATE) != true)
		return -EPERM;

	if (param->u.bcn_ie.reserved[0] != 0xea)
		return -EINVAL;

	mlmeinfo->hidden_ssid_mode = ignore_broadcast_ssid = param->u.bcn_ie.reserved[1];

	ie_len = len-12-2;/*  12 = param header, 2:no packed */
	ssid_ie = rtw_get_ie(param->u.bcn_ie.buf,  WLAN_EID_SSID, &ssid_len, ie_len);

	if (ssid_ie && ssid_len) {
		struct wlan_bssid_ex *pbss_network = &mlmepriv->cur_network.network;
		struct wlan_bssid_ex *pbss_network_ext = &mlmeinfo->network;

		memcpy(ssid, ssid_ie+2, ssid_len);
		ssid[ssid_len>NDIS_802_11_LENGTH_SSID?NDIS_802_11_LENGTH_SSID:ssid_len] = 0x0;

		memcpy(pbss_network->Ssid.Ssid, (void *)ssid, ssid_len);
		pbss_network->Ssid.SsidLength = ssid_len;
		memcpy(pbss_network_ext->Ssid.Ssid, (void *)ssid, ssid_len);
		pbss_network_ext->Ssid.SsidLength = ssid_len;

	}

	DBG_8192D(FUNC_ADPT_FMT" ignore_broadcast_ssid:%d, %s,%d\n", FUNC_ADPT_ARG(adapter),
		ignore_broadcast_ssid, ssid, ssid_len);

	return ret;
}

static int rtw_ioctl_acl_remove_sta(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret = 0;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff)
		return -EINVAL;

	ret = rtw_acl_remove_sta(padapter, param->sta_addr);

	return ret;
}

static int rtw_ioctl_acl_add_sta(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret = 0;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff)
		return -EINVAL;

	ret = rtw_acl_add_sta(padapter, param->sta_addr);

	return ret;
}

static int rtw_ioctl_set_macaddr_acl(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret = 0;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
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
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);

	/* DBG_8192D("%s\n", __func__); */

	/*
	* this function is expect to call in master mode, which allows no power saving
	* so, we just check hw_init_completed instead of call rfpwrstate_check()
	*/

	if (padapter->hw_init_completed == false) {
		ret = -EPERM;
		goto out;
	}

	/* if (p->length < sizeof(struct ieee_param) || !p->pointer) { */
	if (!p->pointer) {
		ret = -EINVAL;
		goto out;
	}

	param = (struct ieee_param *)kmalloc(p->length, GFP_KERNEL);
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
		DBG_8192D("Unknown hostapd request: %d\n", param->cmd);
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

#ifdef CONFIG_DEBUG_RTW_WX_SET_PRIV
	char *ext_dbg;
#endif

	int ret = 0;
	int len = 0;
	char *ext;
	int i;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);
	struct iw_point *dwrq = (struct iw_point*)awrq;

	if (dwrq->length == 0)
		return -EFAULT;
	len = dwrq->length;
	if (!(ext = vmalloc(len)))
		return -ENOMEM;

	if (copy_from_user(ext, dwrq->pointer, len)) {
		vfree(ext);
		return -EFAULT;
	}

	#ifdef CONFIG_DEBUG_RTW_WX_SET_PRIV
	if (!(ext_dbg = rtw_vmalloc(len))) {
		vfree(ext);
		return -ENOMEM;
	}

	memcpy(ext_dbg, ext, len);
	#endif

	/* added for wps2.0 @20110524 */
	if (dwrq->flags == 0x8766 && len > 8) {
		u32 cp_sz;
		struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
		u8 *probereq_wpsie = ext;
		int probereq_wpsie_len = len;
		u8 wps_oui[4]={0x0, 0x50, 0xf2, 0x04};

		if ((_VENDOR_SPECIFIC_IE_ == probereq_wpsie[0]) &&
		    !memcmp(&probereq_wpsie[2], wps_oui, 4)) {
			cp_sz = probereq_wpsie_len>MAX_WPS_IE_LEN ? MAX_WPS_IE_LEN:probereq_wpsie_len;

			/* memcpy(pmlmepriv->probereq_wpsie, probereq_wpsie, cp_sz); */
			/* pmlmepriv->probereq_wpsie_len = cp_sz; */

			DBG_8192D("probe_req_wps_ielen =%d\n", cp_sz);

			if (pmlmepriv->wps_probe_req_ie) {
				u32 free_len = pmlmepriv->wps_probe_req_ie_len;
				pmlmepriv->wps_probe_req_ie_len = 0;
				kfree(pmlmepriv->wps_probe_req_ie);
				pmlmepriv->wps_probe_req_ie = NULL;
			}

			pmlmepriv->wps_probe_req_ie = kmalloc(cp_sz, GFP_KERNEL);
			if (pmlmepriv->wps_probe_req_ie == NULL) {
				DBG_8192D("%s()-%d: kmalloc() ERROR!\n", __func__, __LINE__);
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

#ifdef CONFIG_ANDROID
	/* DBG_8192D("rtw_wx_set_priv: %s req =%s\n", dev->name, ext); */

	i = rtw_android_cmdstr_to_num(ext);

	switch (i) {
	case ANDROID_WIFI_CMD_START :
		indicate_wx_custom_event(padapter, "START");
		break;
	case ANDROID_WIFI_CMD_STOP :
		indicate_wx_custom_event(padapter, "STOP");
		break;
	case ANDROID_WIFI_CMD_RSSI : {
		struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
		struct	wlan_network	*pcur_network = &pmlmepriv->cur_network;

		if (check_fwstate(pmlmepriv, _FW_LINKED) == true)
			sprintf(ext, "%s rssi %d", pcur_network->network.Ssid.Ssid, padapter->recvpriv.rssi);
		else
			sprintf(ext, "OK");
		break; }
	case ANDROID_WIFI_CMD_LINKSPEED : {
		u16 mbps = rtw_get_cur_max_rate(padapter)/10;
		sprintf(ext, "LINKSPEED %d", mbps);
		break; }
	case ANDROID_WIFI_CMD_MACADDR :
		sprintf(ext, "MACADDR = %pM", dev->dev_addr);
		break;
	case ANDROID_WIFI_CMD_SCAN_ACTIVE :
		/* rtw_set_scan_mode(padapter, SCAN_ACTIVE); */
		sprintf(ext, "OK");
		break;
	case ANDROID_WIFI_CMD_SCAN_PASSIVE :
		/* rtw_set_scan_mode(padapter, SCAN_PASSIVE); */
		sprintf(ext, "OK");
		break;

	case ANDROID_WIFI_CMD_COUNTRY : {
		char country_code[10];
		sscanf(ext, "%*s %s", country_code);
		rtw_set_country(padapter, country_code);
		sprintf(ext, "OK");
		break; }
	default :
		#ifdef  CONFIG_DEBUG_RTW_WX_SET_PRIV
		DBG_8192D("%s: %s unknowned req =%s\n", __func__,
			dev->name, ext_dbg);
		#endif

		sprintf(ext, "OK");
	}

	if (copy_to_user(dwrq->pointer, ext, min(dwrq->length, (u16)(strlen(ext)+1))))
		ret = -EFAULT;

	#ifdef CONFIG_DEBUG_RTW_WX_SET_PRIV
	DBG_8192D("%s: %s req =%s rep =%s dwrq->length =%d, strlen(ext)+1 =%d\n", __func__,
		dev->name, ext_dbg , ext, dwrq->length, (u16)(strlen(ext)+1));
	#endif
#endif /* end of CONFIG_ANDROID */

FREE_EXT:

	vfree(ext);
	#ifdef CONFIG_DEBUG_RTW_WX_SET_PRIV
	vfree(ext_dbg);
	#endif

	/* DBG_8192D("rtw_wx_set_priv: (SIOCSIWPRIV) %s ret =%d\n", */
	/*		dev->name, ret); */

	return ret;
}

static int rtw_mp_efuse_get(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wdata, char *extra)
{
	struct iw_point *wrqu = (struct iw_point *)wdata;
	struct rtw_adapter * padapter = rtw_netdev_priv(dev);
	struct mp_priv *pmp_priv;

	int i, j = 0;
	u8 data[EFUSE_MAP_SIZE];
	u8 rawdata[EFUSE_MAX_SIZE];
	u16 mapLen = 0;
	char *pch, *ptmp, *token, *tmp[3] = {NULL, NULL, NULL};
	u16 addr = 0, cnts = 0, max_available_size = 0, raw_cursize = 0, raw_maxsize = 0;

	memset(data, '\0', sizeof(data));
	memset(rawdata, '\0', sizeof(rawdata));

	if (copy_from_user(extra, wrqu->pointer, wrqu->length))
		return -EFAULT;

	pch = extra;
	DBG_8192D("%s: in =%s\n", __func__, extra);

	i = 0;
	/* mac 16 "00e04c871200" rmap, 00, 2 */
	while ((token = strsep(&pch,",")) != NULL) {
		if (i > 2)
			 break;
		tmp[i] = token;
		i++;
	}

	if (strcmp(tmp[0],"realmap") == 0) {

		DBG_8192D("strcmp OK =	%s\n" , tmp[0]);

		mapLen = EFUSE_MAP_SIZE;

		if (rtw_efuse_map_read(padapter, 0, mapLen, data) == _SUCCESS) {
			DBG_8192D("\t  rtw_efuse_map_read\n");
		} else {
			DBG_8192D("\t  rtw_efuse_map_read : Fail\n");
			return -EFAULT;
		}
		memset(extra, '\0', 1);
		DBG_8192D("\tOFFSET\tVALUE(hex)\n");
		sprintf(extra, "%s\n", extra);
		for (i = 0; i < EFUSE_MAP_SIZE; i += 16) {
			DBG_8192D("\t0x%02x\t", i);
			sprintf(extra, "%s \t0x%02x\t", extra, i);
			for (j = 0; j < 8; j++) {
				DBG_8192D("%02X ", data[i+j]);
				sprintf(extra, "%s %02X", extra, data[i+j]);
			}
			DBG_8192D("\t");
			sprintf(extra,"%s\t", extra);
			for (; j < 16; j++) {
				DBG_8192D("%02X ", data[i+j]);
				sprintf(extra, "%s %02X", extra, data[i+j]);
			}
			DBG_8192D("\n");
			sprintf(extra,"%s\n", extra);
		}
		DBG_8192D("\n");
		wrqu->length = strlen(extra);

		return 0;
	} else if (strcmp(tmp[0],"rmap") == 0) {
		if (tmp[1]== NULL || tmp[2]== NULL) return	-EINVAL;
		/*  rmap addr cnts */
		addr = simple_strtoul(tmp[1], &ptmp, 16);

		DBG_8192D("addr = %x\n" , addr);

		cnts = simple_strtoul(tmp[2], &ptmp, 10);
		if (cnts == 0)
			return -EINVAL;

		DBG_8192D("cnts = %d\n" , cnts);
		/* memset(extra, '\0', wrqu->data.length); */

		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);
		if ((addr + cnts) > max_available_size) {
			DBG_8192D("(addr + cnts parameter error\n");
			return -EFAULT;
		}

		if (rtw_efuse_map_read(padapter, addr, cnts, data) == _FAIL)
			DBG_8192D("rtw_efuse_access error\n");
		else
			DBG_8192D("rtw_efuse_access ok\n");

		memset(extra, '\0', 1);
		for (i = 0; i < cnts; i ++) {
			DBG_8192D("0x%02x", data[i]);
			sprintf(extra, "%s 0x%02X", extra, data[i]);
			DBG_8192D(" ");
			sprintf(extra,"%s ", extra);
		}

		wrqu->length = strlen(extra)+1;

		DBG_8192D("extra = %s ", extra);

		return 0;
	} else if (strcmp(tmp[0],"realraw") == 0) {
		addr = 0;
		mapLen = EFUSE_MAX_SIZE;

		if (rtw_efuse_access(padapter, false, addr, mapLen, rawdata) == _FAIL) {
			DBG_8192D("\t  rtw_efuse_map_read : Fail\n");
			return -EFAULT;
		} else {
			DBG_8192D("\t  rtw_efuse_access raw ok\n");
		}

		memset(extra, '\0', 1);
		for (i = 0; i < mapLen; i++) {
			DBG_8192D(" %02x", rawdata[i]);
			sprintf(extra, "%s %02x", extra, rawdata[i]);

			if ((i & 0xF) == 0xF) {
				DBG_8192D("\n\t");
				sprintf(extra, "%s\n\t", extra);
			} else if ((i & 0x7) == 0x7) {
				DBG_8192D("\t");
				sprintf(extra, "%s\t", extra);
			}
		}
		wrqu->length = strlen(extra);
		return 0;
	} else if (strcmp(tmp[0],"mac") == 0) {
		if (tmp[1]== NULL || tmp[2]== NULL) return	-EINVAL;
		addr = 0x19;
		cnts = 6;
		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);
		if ((addr + mapLen) > max_available_size) {
			DBG_8192D("(addr + cnts parameter error\n");
			return -EFAULT;
		}
		if (rtw_efuse_map_read(padapter, addr, cnts, data) == _FAIL)
			DBG_8192D("rtw_efuse_access error\n");
		else
			DBG_8192D("rtw_efuse_access ok\n");
		memset(extra, '\0', 1);
		for (i = 0; i < cnts; i ++) {
			DBG_8192D("0x%02x", data[i]);
			sprintf(extra, "%s 0x%02X", extra, data[i+j]);
			DBG_8192D(" ");
			sprintf(extra,"%s ", extra);
		}
		wrqu->length = strlen(extra);
		return 0;
	} else if (strcmp(tmp[0],"vidpid") == 0) {
		if (tmp[1]== NULL || tmp[2]== NULL)
			return	-EINVAL;
		addr = 0x0c;
		cnts = 4;
		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);
		if ((addr + mapLen) > max_available_size) {
			DBG_8192D("(addr + cnts parameter error\n");
			return -EFAULT;
		}
		if (rtw_efuse_map_read(padapter, addr, cnts, data) == _FAIL) {
			DBG_8192D("rtw_efuse_access error\n");
		} else {
			DBG_8192D("rtw_efuse_access ok\n");
		}
		memset(extra, '\0', 1);
		for (i = 0; i < cnts; i ++) {
			DBG_8192D("0x%02x", data[i]);
			sprintf(extra, "%s 0x%02X", extra, data[i+j]);
			DBG_8192D(" ");
			sprintf(extra,"%s ", extra);
		}
		wrqu->length = strlen(extra);
		return 0;
	} else if (strcmp(tmp[0],"ableraw") == 0) {
		efuse_GetCurrentSize(padapter,&raw_cursize);
		raw_maxsize = efuse_GetMaxSize(padapter);
		sprintf(extra, "%s : [available raw size] = %d", extra, raw_maxsize-raw_cursize);
		wrqu->length = strlen(extra);

		return 0;
	} else {
		 sprintf(extra, "%s : Command not found\n", extra);
		  wrqu->length = strlen(extra);
		  return 0;
	}

	return 0;
}

static int rtw_mp_efuse_set(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wdata, char *extra)
{
	struct iw_point *wrqu = (struct iw_point *)wdata;
	struct rtw_adapter *padapter = rtw_netdev_priv(dev);

	u8 buffer[40];
	u32 i, jj, kk;
	u8 setdata[EFUSE_MAP_SIZE];
	u8 setrawdata[EFUSE_MAX_SIZE];
	char *pch, *ptmp, *token, *edata, *tmp[3] = {NULL, NULL, NULL};

	u16 addr = 0, max_available_size = 0;
	u32  cnts = 0;

	pch = extra;
	DBG_8192D("%s: in =%s\n", __func__, extra);

	i = 0;
	while ((token = strsep (&pch,","))!= NULL) {
		if (i>2) break;
		tmp[i] = token;
		i++;
	}

	/*  tmp[0],[1],[2] */
	/*  wmap, addr, 00e04c871200 */
	if (strcmp(tmp[0],"wmap") == 0) {
		 if (tmp[1]== NULL || tmp[2]== NULL)
			return	-EINVAL;
		if (!strlen(tmp[2])/2 > 1)
			return -EFAULT;

		addr = simple_strtoul(tmp[1], &ptmp, 16);
		addr = addr & 0xFF;
		DBG_8192D("addr = %x\n" , addr);

		cnts = strlen(tmp[2])/2;
		if (cnts == 0) return -EFAULT;

		DBG_8192D("cnts = %d\n" , cnts);
		DBG_8192D("target data = %s\n" , tmp[2]);

		for (jj = 0, kk = 0; jj < cnts; jj++, kk += 2)
			setdata[jj] = key_2char2num(tmp[2][kk], tmp[2][kk+ 1]);

		EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);

		if ((addr + cnts) > max_available_size) {
					DBG_8192D("parameter error\n");
					return -EFAULT;
		}
		if (rtw_efuse_map_write(padapter, addr, cnts, setdata) == _FAIL) {
				DBG_8192D("rtw_efuse_map_write error\n");
				return -EFAULT;
		} else
		   DBG_8192D("rtw_efuse_map_write ok\n");

		return 0;
	} else if (strcmp(tmp[0],"wraw") == 0) {
			 if (tmp[1]== NULL || tmp[2]== NULL) return	-EINVAL;
			 if (! strlen(tmp[2])/2 > 1) return -EFAULT;
			addr = simple_strtoul(tmp[1], &ptmp, 16);
			addr = addr & 0xFF;
			DBG_8192D("addr = %x\n" , addr);

			cnts = strlen(tmp[2])/2;
			if (cnts == 0) return -EFAULT;

			DBG_8192D(" cnts = %d\n" , cnts);
			DBG_8192D("target data = %s\n" , tmp[2]);

			for (jj = 0, kk = 0; jj < cnts; jj++, kk += 2)
					setrawdata[jj] = key_2char2num(tmp[2][kk], tmp[2][kk+ 1]);

			if (rtw_efuse_access(padapter, true, addr, cnts, setrawdata) == _FAIL) {
					DBG_8192D("\t  rtw_efuse_map_read : Fail\n");
						return -EFAULT;
			} else {
			  DBG_8192D("\t  rtw_efuse_access raw ok\n");
			}
			return 0;
		} else if (strcmp(tmp[0],"mac") == 0) {
			 if (tmp[1]== NULL || tmp[2]== NULL) return	-EINVAL;
			/* mac, 00e04c871200 */
				addr = 0x19;
				cnts = strlen(tmp[1])/2;
				if (cnts == 0) return -EFAULT;
				if (cnts > 6) {
						DBG_8192D("error data for mac addr = %s\n" , tmp[1]);
						return -EFAULT;
				}

				DBG_8192D("target data = %s\n" , tmp[1]);

				for (jj = 0, kk = 0; jj < cnts; jj++, kk += 2)
					setdata[jj] = key_2char2num(tmp[1][kk], tmp[1][kk+ 1]);

				EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);

				if ((addr + cnts) > max_available_size) {
						DBG_8192D("parameter error\n");
						return -EFAULT;
				}
				if (rtw_efuse_map_write(padapter, addr, cnts, setdata) == _FAIL) {
					DBG_8192D("rtw_efuse_map_write error\n");
					return -EFAULT;
				} else
					DBG_8192D("rtw_efuse_map_write ok\n");

			return 0;
		} else if (strcmp(tmp[0],"vidpid") == 0) {
			if (tmp[1]== NULL || tmp[2]== NULL)
				return	-EINVAL;
				/*  pidvid, da0b7881 */
			addr = 0x0c;

			cnts = strlen(tmp[1])/2;
			if (cnts == 0)
				return -EFAULT;
			DBG_8192D("target data = %s\n" , tmp[1]);

			for (jj = 0, kk = 0; jj < cnts; jj++, kk += 2)
				setdata[jj] = key_2char2num(tmp[1][kk], tmp[1][kk+ 1]);

			EFUSE_GetEfuseDefinition(padapter, EFUSE_WIFI, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (void *)&max_available_size, false);

			if ((addr + cnts) > max_available_size) {
				DBG_8192D("parameter error\n");
				return -EFAULT;
			}

			if (rtw_efuse_map_write(padapter, addr, cnts, setdata) == _FAIL) {
				DBG_8192D("rtw_efuse_map_write error\n");
				return -EFAULT;
			} else
				DBG_8192D("rtw_efuse_map_write ok\n");

			return 0;
		} else {
			DBG_8192D("Command not found\n");
			return 0;
		}

	  return 0;
}

static int rtw_wfd_tdls_enable(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

	return ret;
}

static int rtw_tdls_weaksec(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

	return ret;
}

static int rtw_tdls_enable(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

	return ret;
}

static int rtw_tdls_setup(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

	return ret;
}

static int rtw_tdls_teardown(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

	return ret;
}

static int rtw_tdls_discovery(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

	return ret;
}

static int rtw_tdls_ch_switch (struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

	return ret;
}

static int rtw_tdls_pson(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

		return ret;
}

static int rtw_tdls_psoff(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

	return ret;
}

static int rtw_tdls_setip(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

	return ret;
}

static int rtw_tdls_getip(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

	return ret;
}

static int rtw_tdls_getport(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

	return ret;
}

/* WFDTDLS, for sigma test */
static int rtw_tdls_dis_result(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{

	int ret = 0;

	return ret;
}

/* WFDTDLS, for sigma test */
static int rtw_wfd_tdls_status(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

	return ret;
}

static int rtw_tdls_ch_switch_off(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

	return ret;
}

static int rtw_tdls(struct net_device *dev,
		    struct iw_request_info *info,
		    union iwreq_data *wrqu, char *extra)
{
	int ret = 0;

	return ret;
}

static int rtw_tdls_get(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	return 0;
}

static int rtw_pm_set(struct net_device *dev,
		      struct iw_request_info *info,
		      union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	unsigned	mode = 0;
	struct rtw_adapter *padapter = (struct rtw_adapter *)rtw_netdev_priv(dev);

	DBG_8192D("[%s] extra = %s\n", __func__, extra);

	if (!memcmp(extra, "lps =", 4)) {
		sscanf(extra+4, "%u", &mode);
		ret = rtw_pm_set_lps(padapter, mode);
	} else if (!memcmp(extra, "ips =", 4)) {
		sscanf(extra+4, "%u", &mode);
		ret = rtw_pm_set_ips(padapter, mode);
	} else {
		ret = -EINVAL;
	}
	return ret;
}

#ifdef CONFIG_WOWLAN
static int rtw_wowlan_ctrl(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct oid_par_priv	oid_par;
	struct wowlan_ioctl_param *poidparam;
	uint status = 0;
	u16 len;
	u8 *pparmbuf = NULL, bset;
	struct rtw_adapter *padapter =  (struct rtw_adapter *)rtw_netdev_priv(dev);

	struct iw_point *p = &wrqu->data;

	if ((!p->length) || (!p->pointer)) {
		ret = -EINVAL;
		goto _rtw_wowlan_ctrl_exit;
	}

	pparmbuf = NULL;
	bset = (u8)(p->flags & 0xFFFF);
	len = p->length;
	pparmbuf = (u8 *)kmalloc(len, GFP_KERNEL);
	if (pparmbuf == NULL) {
		ret = -ENOMEM;
		goto _rtw_wowlan_ctrl_exit;
	}

	if (copy_from_user(pparmbuf, p->pointer, len)) {
		ret = -EFAULT;
		goto _rtw_wowlan_ctrl_exit_free;
	}
	poidparam = (struct wowlan_ioctl_param *)pparmbuf;

	if (!padapter->pwrctrlpriv.bSupportRemoteWakeup) {
		ret = -EPERM;
		DBG_8192D("+rtw_wowlan_ctrl: Device didn't support the remote wakeup!!\n");
		goto _rtw_wowlan_ctrl_exit_free;
	}
	rtw_hal_set_hwreg(padapter, HW_VAR_WOWLAN, (u8 *)poidparam);

	DBG_8192D("rtw_wowlan_ctrl: subcode [%d], len[%d], buffer_len[%d]\r\n",
		  poidparam->subcode, poidparam->len, len);

	if (copy_to_user(p->pointer, pparmbuf, len))
		ret = -EFAULT;

_rtw_wowlan_ctrl_exit_free:
	kfree(pparmbuf);
_rtw_wowlan_ctrl_exit:

	return ret;
}
#endif /* CONFIG_WOWLAN */

static int rtw_test(struct net_device *dev, struct iw_request_info *info,
		    union iwreq_data *wrqu, char *extra)
{
	DBG_8192D("%s\n", __func__);
	return 0;
}

#include <rtw_android.h>
int rtw_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct iwreq *wrq = (struct iwreq *)rq;
	int ret = 0;

	switch (cmd) {
	case RTL_IOCTL_WPA_SUPPLICANT:
		ret = wpa_supplicant_ioctl(dev, &wrq->u.data);
		break;
#ifdef CONFIG_92D_AP_MODE
	case RTL_IOCTL_HOSTAPD:
		ret = rtw_hostapd_ioctl(dev, &wrq->u.data);
		break;
#ifdef CONFIG_NO_WIRELESS_HANDLERS
	case SIOCSIWMODE:
		ret = rtw_wx_set_mode(dev, NULL, &wrq->u, NULL);
		break;
#endif
#endif
	case (SIOCDEVPRIVATE+1):
		ret = rtw_android_priv_cmd(dev, rq, cmd);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static iw_handler rtw_handlers[] = {
	NULL,			/* SIOCSIWCOMMIT */
	rtw_wx_get_name,	/* SIOCGIWNAME */
	dummy,			/* SIOCSIWNWID */
	dummy,			/* SIOCGIWNWID */
	rtw_wx_set_freq,	/* SIOCSIWFREQ */
	rtw_wx_get_freq,	/* SIOCGIWFREQ */
	rtw_wx_set_mode,	/* SIOCSIWMODE */
	rtw_wx_get_mode,	/* SIOCGIWMODE */
	dummy,			/* SIOCSIWSENS */
	rtw_wx_get_sens,	/* SIOCGIWSENS */
	NULL,			/* SIOCSIWRANGE */
	rtw_wx_get_range,	/* SIOCGIWRANGE */
	rtw_wx_set_priv,	/* SIOCSIWPRIV */
	NULL,			/* SIOCGIWPRIV */
	NULL,			/* SIOCSIWSTATS */
	NULL,			/* SIOCGIWSTATS */
	dummy,			/* SIOCSIWSPY */
	dummy,			/* SIOCGIWSPY */
	NULL,			/* SIOCGIWTHRSPY */
	NULL,			/* SIOCWIWTHRSPY */
	rtw_wx_set_wap,		/* SIOCSIWAP */
	rtw_wx_get_wap,		/* SIOCGIWAP */
	rtw_wx_set_mlme,	/* request MLME op; uses struct iw_mlme */
	dummy,			/* SIOCGIWAPLIST -- depricated */
	rtw_wx_set_scan,	/* SIOCSIWSCAN */
	rtw_wx_get_scan,	/* SIOCGIWSCAN */
	rtw_wx_set_essid,	/* SIOCSIWESSID */
	rtw_wx_get_essid,	/* SIOCGIWESSID */
	dummy,			/* SIOCSIWNICKN */
	rtw_wx_get_nick,	/* SIOCGIWNICKN */
	NULL,			/* -- hole -- */
	NULL,			/* -- hole -- */
	rtw_wx_set_rate,	/* SIOCSIWRATE */
	rtw_wx_get_rate,	/* SIOCGIWRATE */
	rtw_wx_set_rts,		/* SIOCSIWRTS */
	rtw_wx_get_rts,		/* SIOCGIWRTS */
	rtw_wx_set_frag,	/* SIOCSIWFRAG */
	rtw_wx_get_frag,	/* SIOCGIWFRAG */
	dummy,			/* SIOCSIWTXPOW */
	dummy,			/* SIOCGIWTXPOW */
	dummy,			/* SIOCSIWRETRY */
	rtw_wx_get_retry,	/* SIOCGIWRETRY */
	rtw_wx_set_enc,		/* SIOCSIWENCODE */
	rtw_wx_get_enc,		/* SIOCGIWENCODE */
	dummy,			/* SIOCSIWPOWER */
	rtw_wx_get_power,	/* SIOCGIWPOWER */
	NULL,			/*---hole---*/
	NULL,			/*---hole---*/
	rtw_wx_set_gen_ie,	/* SIOCSIWGENIE */
	NULL,			/* SIOCGWGENIE */
	rtw_wx_set_auth,	/* SIOCSIWAUTH */
	NULL,			/* SIOCGIWAUTH */
	rtw_wx_set_enc_ext,	/* SIOCSIWENCODEEXT */
	NULL,			/* SIOCGIWENCODEEXT */
	rtw_wx_set_pmkid,	/* SIOCSIWPMKSA */
	NULL,			/*---hole---*/
};

static const struct iw_priv_args rtw_private_args[] = {
	{
		SIOCIWFIRSTPRIV + 0x0,
		IW_PRIV_TYPE_CHAR | 0x7FF, 0, "write"
	},
	{
		SIOCIWFIRSTPRIV + 0x1,
		IW_PRIV_TYPE_CHAR | 0x7FF,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | IFNAMSIZ, "read"
	},
	{
		SIOCIWFIRSTPRIV + 0x2, 0, 0, "driver_ext"
	},
	{
		SIOCIWFIRSTPRIV + 0x3, 0, 0, "mp_ioctl"
	},
	{
		SIOCIWFIRSTPRIV + 0x4,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "apinfo"
	},
	{
		SIOCIWFIRSTPRIV + 0x5,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, 0, "setpid"
	},
	{
		SIOCIWFIRSTPRIV + 0x6,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "wps_start"
	},
	{
		SIOCIWFIRSTPRIV + 0x7,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "get_sensitivity"
	},
	{
		SIOCIWFIRSTPRIV + 0x8,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "wps_prob_req_ie"
	},
	{
		SIOCIWFIRSTPRIV + 0x9,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "wps_assoc_req_ie"
	},

	{
		SIOCIWFIRSTPRIV + 0xA,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "channel_plan"
	},

	{
		SIOCIWFIRSTPRIV + 0xB,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, 0, "dbg"
	},
	{
		SIOCIWFIRSTPRIV + 0xC,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 3, 0, "rfw"
	},
	{
		SIOCIWFIRSTPRIV + 0xD,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, IW_PRIV_TYPE_CHAR |
		IW_PRIV_SIZE_FIXED | IFNAMSIZ, "rfr"
	},
#ifdef CONFIG_WOWLAN
	{
		SIOCIWFIRSTPRIV + 0xE, 0, 0, "wowlan_ctrl"
	},
#endif /*  CONFIG_WOWLAN */
	{
		SIOCIWFIRSTPRIV + 0x10,
		IW_PRIV_TYPE_CHAR | P2P_PRIVATE_IOCTL_SET_LEN, 0, "p2p_set"
	},
	{
		SIOCIWFIRSTPRIV + 0x11,
		IW_PRIV_TYPE_CHAR | P2P_PRIVATE_IOCTL_SET_LEN,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED |
		P2P_PRIVATE_IOCTL_SET_LEN , "p2p_get"
	},
	{
		SIOCIWFIRSTPRIV + 0x12, 0, 0, "NULL"
	},
	{
		SIOCIWFIRSTPRIV + 0x13,
		IW_PRIV_TYPE_CHAR | 64, IW_PRIV_TYPE_CHAR | 64 , "p2p_get2"
	},
	{
		SIOCIWFIRSTPRIV + 0x14,
		IW_PRIV_TYPE_CHAR  | 64, 0, "tdls"
	},
	{
		SIOCIWFIRSTPRIV + 0x15,
		IW_PRIV_TYPE_CHAR | P2P_PRIVATE_IOCTL_SET_LEN,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED |
		P2P_PRIVATE_IOCTL_SET_LEN, "tdls_get"
	},
	{
		SIOCIWFIRSTPRIV + 0x16,
		IW_PRIV_TYPE_CHAR | 64, 0, "pm_set"
	},

	{SIOCIWFIRSTPRIV + 0x18, IW_PRIV_TYPE_CHAR | IFNAMSIZ , 0 ,
	 "rereg_nd_name"},

	{SIOCIWFIRSTPRIV + 0x1A, IW_PRIV_TYPE_CHAR | 128, 0, "efuse_set"},
	{SIOCIWFIRSTPRIV + 0x1B, IW_PRIV_TYPE_CHAR | 128, IW_PRIV_TYPE_CHAR |
	 IW_PRIV_SIZE_FIXED | 0x700, "efuse_get"},
	{
		SIOCIWFIRSTPRIV + 0x1D,
		IW_PRIV_TYPE_CHAR | 40, IW_PRIV_TYPE_CHAR | 0x7FF, "test"
	},
};

static iw_handler rtw_private_handler[] = {
	rtw_wx_write32,			/* 0x00 */
	rtw_wx_read32,			/* 0x01 */
	rtw_drvext_hdl,			/* 0x02 */
	rtw_mp_ioctl_hdl,		/* 0x03 */

/*  for MM DTV platform */
	rtw_get_ap_info,		/* 0x04 */

	rtw_set_pid,			/* 0x05 */
	rtw_wps_start,			/* 0x06 */

	rtw_wx_get_sensitivity,		/* 0x07 */
	rtw_wx_set_mtk_wps_probe_ie,	/* 0x08 */
	rtw_wx_set_mtk_wps_ie,		/* 0x09 */

/*  Set Channel depend on the country code */
	rtw_wx_set_channel_plan,	/* 0x0A */

	rtw_dbg_port,			/* 0x0B */
	rtw_wx_write_rf,		/* 0x0C */
	rtw_wx_read_rf,			/* 0x0D */

#ifdef CONFIG_WOWLAN
	rtw_wowlan_ctrl,		/* 0x0E */
#else
	rtw_wx_priv_null,		/* 0x0E */
#endif /* CONFIG_WOWLAN */
	rtw_wx_priv_null,		/* 0x0F */

	rtw_p2p_set,			/* 0x10 */
	rtw_p2p_get,			/* 0x11 */
	NULL,				/* 0x12 */
	rtw_p2p_get2,			/* 0x13 */

	rtw_tdls,			/* 0x14 */
	rtw_tdls_get,			/* 0x15 */

	rtw_pm_set,			/* 0x16 */
	rtw_wx_priv_null,		/* 0x17 */
	rtw_rereg_nd_name,		/* 0x18 */
	rtw_wx_priv_null,		/* 0x19 */

	rtw_mp_efuse_set,		/* 0x1A */
	rtw_mp_efuse_get,		/* 0x1B */
	NULL,				/*  0x1C is reserved for hostapd */
	rtw_test	,		/*  0x1D */
};

#if WIRELESS_EXT >= 17
static struct iw_statistics *rtw_get_wireless_stats(struct net_device *dev)
{
	struct rtw_adapter *padapter =
		(struct rtw_adapter *)rtw_netdev_priv(dev);
	struct iw_statistics *piwstats = &padapter->iwstats;
	int tmp_level = 0;
	int tmp_qual = 0;
	int tmp_noise = 0;

	if (!check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {
		piwstats->qual.qual = 0;
		piwstats->qual.level = 0;
		piwstats->qual.noise = 0;
	} else {
		#ifdef CONFIG_SIGNAL_DISPLAY_DBM
		tmp_level = translate_percentage_to_dbm(padapter->recvpriv.signal_strength);
		#else
		tmp_level = padapter->recvpriv.signal_strength;
		#endif

		tmp_qual = padapter->recvpriv.signal_qual;
		tmp_noise = padapter->recvpriv.noise;

		piwstats->qual.level = tmp_level;
		piwstats->qual.qual = tmp_qual;
		piwstats->qual.noise = tmp_noise;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 14))
	piwstats->qual.updated = IW_QUAL_ALL_UPDATED ;/* IW_QUAL_DBM; */
#else
#endif

	#ifdef CONFIG_SIGNAL_DISPLAY_DBM
	piwstats->qual.updated = piwstats->qual.updated | IW_QUAL_DBM;
	#endif

	return &padapter->iwstats;
}
#endif

#ifdef CONFIG_WIRELESS_EXT
struct iw_handler_def rtw_handlers_def = {
	.standard = rtw_handlers,
	.num_standard = sizeof(rtw_handlers) / sizeof(iw_handler),
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33)) || defined(CONFIG_WEXT_PRIV)
	.private = rtw_private_handler,
	.private_args = (struct iw_priv_args *)rtw_private_args,
	.num_private = sizeof(rtw_private_handler) / sizeof(iw_handler),
	.num_private_args = sizeof(rtw_private_args) /
			    sizeof(struct iw_priv_args),
#endif
#if WIRELESS_EXT >= 17
	.get_wireless_stats = rtw_get_wireless_stats,
#endif
};
#endif
