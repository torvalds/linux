// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _IOCTL_LINUX_C_

#include <linux/etherdevice.h>
#include <drv_types.h>
#include <rtw_debug.h>
#include <rtw_mp.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>

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

static u32 rtw_rates[] = {1000000, 2000000, 5500000, 11000000,
	6000000, 9000000, 12000000, 18000000, 24000000, 36000000, 48000000, 54000000};

static const char * const iw_operation_mode[] = {
	"Auto", "Ad-Hoc", "Managed",  "Master", "Repeater", "Secondary", "Monitor"
};

void indicate_wx_scan_complete_event(struct adapter *padapter)
{
	union iwreq_data wrqu;

	memset(&wrqu, 0, sizeof(union iwreq_data));

	/* DBG_871X("+rtw_indicate_wx_scan_complete_event\n"); */
}


void rtw_indicate_wx_assoc_event(struct adapter *padapter)
{
	union iwreq_data wrqu;
	struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex		*pnetwork = (struct wlan_bssid_ex*)(&(pmlmeinfo->network));

	memset(&wrqu, 0, sizeof(union iwreq_data));

	wrqu.ap_addr.sa_family = ARPHRD_ETHER;

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) ==true)
		memcpy(wrqu.ap_addr.sa_data, pnetwork->MacAddress, ETH_ALEN);
	else
		memcpy(wrqu.ap_addr.sa_data, pmlmepriv->cur_network.network.MacAddress, ETH_ALEN);

	DBG_871X_LEVEL(_drv_always_, "assoc success\n");
}

void rtw_indicate_wx_disassoc_event(struct adapter *padapter)
{
	union iwreq_data wrqu;

	memset(&wrqu, 0, sizeof(union iwreq_data));

	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	eth_zero_addr(wrqu.ap_addr.sa_data);
}

static char *translate_scan(struct adapter *padapter,
				struct iw_request_info* info, struct wlan_network *pnetwork,
				char *start, char *stop)
{
	struct iw_event iwe;
	u16 cap;
	u32 ht_ielen = 0;
	char *custom = NULL;
	char *p;
	u16 max_rate = 0, rate, ht_cap =false, vht_cap = false;
	u32 i = 0;
	u8 bw_40MHz = 0, short_GI = 0;
	u16 mcs_rate = 0, vht_data_rate = 0;
	u8 ie_offset = (pnetwork->network.Reserved[0] == 2? 0:12);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	u8 ss, sq;

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
	if (pnetwork->network.Reserved[0] == 2) { /*  Probe Request */
		p = rtw_get_ie(&pnetwork->network.IEs[0], _HT_CAPABILITY_IE_, &ht_ielen, pnetwork->network.IELength);
	} else {
		p = rtw_get_ie(&pnetwork->network.IEs[12], _HT_CAPABILITY_IE_, &ht_ielen, pnetwork->network.IELength-12);
	}
	if (p && ht_ielen>0) {
		struct rtw_ieee80211_ht_cap *pht_capie;
		ht_cap = true;
		pht_capie = (struct rtw_ieee80211_ht_cap *)(p+2);
		memcpy(&mcs_rate , pht_capie->supp_mcs_set, 2);
		bw_40MHz = (le16_to_cpu(pht_capie->cap_info) & IEEE80211_HT_CAP_SUP_WIDTH) ? 1 : 0;
		short_GI = (le16_to_cpu(pht_capie->cap_info) & (IEEE80211_HT_CAP_SGI_20 | IEEE80211_HT_CAP_SGI_40)) ? 1 : 0;
	}

	/* Add the protocol name */
	iwe.cmd = SIOCGIWNAME;
	if (rtw_is_cckratesonly_included((u8 *)&pnetwork->network.SupportedRates)) {
		if (ht_cap)
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bn");
		else
		snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11b");
	} else if (rtw_is_cckrates_included((u8 *)&pnetwork->network.SupportedRates)) {
		if (ht_cap)
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bgn");
		else
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bg");
	} else {
		if (pnetwork->network.Configuration.DSConfig > 14) {
			if (vht_cap)
				snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11AC");
			else if (ht_cap)
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
	if (pnetwork->network.Reserved[0] == 2) { /*  Probe Request */
		cap = 0;
	} else {
		__le16 le_tmp;

	        iwe.cmd = SIOCGIWMODE;
		memcpy((u8 *)&le_tmp, rtw_get_capability_from_ie(pnetwork->network.IEs), 2);
		cap = le16_to_cpu(le_tmp);
	}

	if (cap & (WLAN_CAPABILITY_IBSS |WLAN_CAPABILITY_BSS)) {
		if (cap & WLAN_CAPABILITY_BSS)
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
	if (cap & WLAN_CAPABILITY_PRIVACY)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	start = iwe_stream_add_point(info, start, stop, &iwe, pnetwork->network.Ssid.Ssid);

	/*Add basic and extended rates */
	max_rate = 0;
	custom = kzalloc(MAX_CUSTOM_LEN, GFP_ATOMIC);
	if (!custom)
		return start;
	p = custom;
	p += snprintf(p, MAX_CUSTOM_LEN - (p - custom), " Rates (Mb/s): ");
	while (pnetwork->network.SupportedRates[i]!= 0) {
		rate = pnetwork->network.SupportedRates[i]&0x7F;
		if (rate > max_rate)
			max_rate = rate;
		p += snprintf(p, MAX_CUSTOM_LEN - (p - custom),
			      "%d%s ", rate >> 1, (rate & 1) ? ".5" : "");
		i++;
	}

	if (vht_cap) {
		max_rate = vht_data_rate;
	} else if (ht_cap) {
		if (mcs_rate&0x8000) { /* MCS15 */
			max_rate = (bw_40MHz) ? ((short_GI)?300:270):((short_GI)?144:130);
		} else if (mcs_rate&0x0080) { /* MCS7 */
			max_rate = (bw_40MHz) ? ((short_GI)?150:135):((short_GI)?72:65);
		} else { /* default MCS7 */
			/* DBG_871X("wx_get_scan, mcs_rate_bitmap = 0x%x\n", mcs_rate); */
			max_rate = (bw_40MHz) ? ((short_GI)?150:135):((short_GI)?72:65);
		}

		max_rate = max_rate*2;/* Mbps/2; */
	}

	iwe.cmd = SIOCGIWRATE;
	iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;
	iwe.u.bitrate.value = max_rate * 500000;
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_PARAM_LEN);

	/* parsing WPA/WPA2 IE */
	if (pnetwork->network.Reserved[0] != 2) { /*  Probe Request */
		u8 *buf;
		u8 wpa_ie[255], rsn_ie[255];
		u16 wpa_len = 0, rsn_len = 0;
		u8 *p;
		sint out_len = 0;
		out_len =rtw_get_sec_ie(pnetwork->network.IEs , pnetwork->network.IELength, rsn_ie,&rsn_len, wpa_ie,&wpa_len);
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_wx_get_scan: ssid =%s\n", pnetwork->network.Ssid.Ssid));
		RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_wx_get_scan: wpa_len =%d rsn_len =%d\n", wpa_len, rsn_len));

		buf = kzalloc(MAX_WPA_IE_LEN*2, GFP_ATOMIC);
		if (!buf)
			return start;
		if (wpa_len > 0) {
			p =buf;
			p += sprintf(p, "wpa_ie =");
			for (i = 0; i < wpa_len; i++)
				p += sprintf(p, "%02x", wpa_ie[i]);

			if (wpa_len > 100) {
				printk("-----------------Len %d----------------\n", wpa_len);
				for (i = 0; i < wpa_len; i++)
					printk("%02x ", wpa_ie[i]);
				printk("\n");
				printk("-----------------Len %d----------------\n", wpa_len);
			}

			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVCUSTOM;
			iwe.u.data.length = strlen(buf);
			start = iwe_stream_add_point(info, start, stop, &iwe, buf);

			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd =IWEVGENIE;
			iwe.u.data.length = wpa_len;
			start = iwe_stream_add_point(info, start, stop, &iwe, wpa_ie);
		}
		if (rsn_len > 0) {
			p = buf;
			memset(buf, 0, MAX_WPA_IE_LEN*2);
			p += sprintf(p, "rsn_ie =");
			for (i = 0; i < rsn_len; i++)
				p += sprintf(p, "%02x", rsn_ie[i]);
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVCUSTOM;
			iwe.u.data.length = strlen(buf);
			start = iwe_stream_add_point(info, start, stop, &iwe, buf);

			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd =IWEVGENIE;
			iwe.u.data.length = rsn_len;
			start = iwe_stream_add_point(info, start, stop, &iwe, rsn_ie);
		}
		kfree(buf);
	}

	{ /* parsing WPS IE */
		uint cnt = 0, total_ielen;
		u8 *wpsie_ptr = NULL;
		uint wps_ielen = 0;

		u8 *ie_ptr;
		total_ielen = pnetwork->network.IELength - ie_offset;

		if (pnetwork->network.Reserved[0] == 2) { /*  Probe Request */
			ie_ptr = pnetwork->network.IEs;
			total_ielen = pnetwork->network.IELength;
		} else {    /*  Beacon or Probe Respones */
			ie_ptr = pnetwork->network.IEs + _FIXED_IE_LENGTH_;
			total_ielen = pnetwork->network.IELength - _FIXED_IE_LENGTH_;
		}

		while (cnt < total_ielen) {
			if (rtw_is_wps_ie(&ie_ptr[cnt], &wps_ielen) && (wps_ielen>2)) {
				wpsie_ptr = &ie_ptr[cnt];
				iwe.cmd =IWEVGENIE;
				iwe.u.data.length = (u16)wps_ielen;
				start = iwe_stream_add_point(info, start, stop, &iwe, wpsie_ptr);
			}
			cnt+=ie_ptr[cnt+1]+2; /* goto next */
		}
	}

	/* Add quality statistics */
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.updated = IW_QUAL_QUAL_UPDATED | IW_QUAL_LEVEL_UPDATED
	#if defined(CONFIG_SIGNAL_DISPLAY_DBM) && defined(CONFIG_BACKGROUND_NOISE_MONITOR)
		| IW_QUAL_NOISE_UPDATED
	#else
		| IW_QUAL_NOISE_INVALID
	#endif
	#ifdef CONFIG_SIGNAL_DISPLAY_DBM
		| IW_QUAL_DBM
	#endif
	;

	if (check_fwstate(pmlmepriv, _FW_LINKED) == true &&
		is_same_network(&pmlmepriv->cur_network.network, &pnetwork->network, 0)) {
		ss = padapter->recvpriv.signal_strength;
		sq = padapter->recvpriv.signal_qual;
	} else {
		ss = pnetwork->network.PhyInfo.SignalStrength;
		sq = pnetwork->network.PhyInfo.SignalQuality;
	}


	#ifdef CONFIG_SIGNAL_DISPLAY_DBM
	iwe.u.qual.level = (u8)translate_percentage_to_dbm(ss);/* dbm */
	#else
	#ifdef CONFIG_SKIP_SIGNAL_SCALE_MAPPING
	{
		/* Do signal scale mapping when using percentage as the unit of signal strength, since the scale mapping is skipped in odm */

		struct hal_com_data *pHal = GET_HAL_DATA(padapter);

		iwe.u.qual.level = (u8)odm_SignalScaleMapping(&pHal->odmpriv, ss);
	}
	#else
	iwe.u.qual.level = (u8)ss;/*  */
	#endif
	#endif

	iwe.u.qual.qual = (u8)sq;   /*  signal quality */

	#if defined(CONFIG_SIGNAL_DISPLAY_DBM) && defined(CONFIG_BACKGROUND_NOISE_MONITOR)
	{
		s16 tmp_noise = 0;
		rtw_hal_get_odm_var(padapter, HAL_ODM_NOISE_MONITOR,&(pnetwork->network.Configuration.DSConfig), &(tmp_noise));
		iwe.u.qual.noise = tmp_noise ;
	}
	#else
	iwe.u.qual.noise = 0; /*  noise level */
	#endif

	/* DBG_871X("iqual =%d, ilevel =%d, inoise =%d, iupdated =%d\n", iwe.u.qual.qual, iwe.u.qual.level , iwe.u.qual.noise, iwe.u.qual.updated); */

	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_QUAL_LEN);

	{
		u8 *buf;
		u8 *p, *pos;

		buf = kzalloc(MAX_WPA_IE_LEN, GFP_ATOMIC);
		if (!buf)
			goto exit;
		p = buf;
		pos = pnetwork->network.Reserved;
		p += sprintf(p, "fm =%02X%02X", pos[1], pos[0]);
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVCUSTOM;
		iwe.u.data.length = strlen(buf);
		start = iwe_stream_add_point(info, start, stop, &iwe, buf);
		kfree(buf);
	}
exit:
	kfree(custom);

	return start;
}

static int wpa_set_auth_algs(struct net_device *dev, u32 value)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	int ret = 0;

	if ((value & WLAN_AUTH_SHARED_KEY) && (value & WLAN_AUTH_OPEN)) {
		DBG_871X("wpa_set_auth_algs, WLAN_AUTH_SHARED_KEY and WLAN_AUTH_OPEN [value:0x%x]\n", value);
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
		padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeAutoSwitch;
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;
	} else if (value & WLAN_AUTH_SHARED_KEY)	{
		DBG_871X("wpa_set_auth_algs, WLAN_AUTH_SHARED_KEY  [value:0x%x]\n", value);
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;

		padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeShared;
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Shared;
	} else if (value & WLAN_AUTH_OPEN) {
		DBG_871X("wpa_set_auth_algs, WLAN_AUTH_OPEN\n");
		/* padapter->securitypriv.ndisencryptstatus = Ndis802_11EncryptionDisabled; */
		if (padapter->securitypriv.ndisauthtype < Ndis802_11AuthModeWPAPSK) {
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeOpen;
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
		}
	} else if (value & WLAN_AUTH_LEAP) {
		DBG_871X("wpa_set_auth_algs, WLAN_AUTH_LEAP\n");
	} else {
		DBG_871X("wpa_set_auth_algs, error!\n");
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
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
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
		if (param->u.crypt.idx >= WEP_KEYS ||
		    param->u.crypt.idx >= BIP_MAX_KEYID) {
			ret = -EINVAL;
			goto exit;
		}
	} else {
		{
			ret = -EINVAL;
			goto exit;
		}
	}

	if (strcmp(param->u.crypt.alg, "WEP") == 0) {
		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_, ("wpa_set_encryption, crypt.alg = WEP\n"));
		DBG_871X("wpa_set_encryption, crypt.alg = WEP\n");

		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;
		padapter->securitypriv.dot11PrivacyAlgrthm = _WEP40_;
		padapter->securitypriv.dot118021XGrpPrivacy = _WEP40_;

		wep_key_idx = param->u.crypt.idx;
		wep_key_len = param->u.crypt.key_len;

		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_, ("(1)wep_key_idx =%d\n", wep_key_idx));
		DBG_871X("(1)wep_key_idx =%d\n", wep_key_idx);

		if (wep_key_idx > WEP_KEYS)
			return -EINVAL;

		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_, ("(2)wep_key_idx =%d\n", wep_key_idx));

		if (wep_key_len > 0) {
			wep_key_len = wep_key_len <= 5 ? 5 : 13;
			wep_total_len = wep_key_len + FIELD_OFFSET(struct ndis_802_11_wep, KeyMaterial);
			pwep = rtw_malloc(wep_total_len);
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
			DBG_871X("wep, set_tx = 1\n");

			if (rtw_set_802_11_add_wep(padapter, pwep) == (u8)_FAIL)
				ret = -EOPNOTSUPP ;
		} else {
			DBG_871X("wep, set_tx = 0\n");

			/* don't update "psecuritypriv->dot11PrivacyAlgrthm" and */
			/* psecuritypriv->dot11PrivacyKeyIndex =keyid", but can rtw_set_key to fw/cam */

			if (wep_key_idx >= WEP_KEYS) {
				ret = -EOPNOTSUPP ;
				goto exit;
			}

			memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), pwep->KeyMaterial, pwep->KeyLength);
			psecuritypriv->dot11DefKeylen[wep_key_idx]=pwep->KeyLength;
			rtw_set_key(padapter, psecuritypriv, wep_key_idx, 0, true);
		}

		goto exit;
	}

	if (padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) { /*  802_1x */
		struct sta_info *psta, *pbcmc_sta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_MP_STATE) == true) { /* sta mode */
			psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
			if (psta == NULL) {
				/* DEBUG_ERR(("Set wpa_set_encryption: Obtain Sta_info fail\n")); */
			} else {
				/* Jeff: don't disable ieee8021x_blocked while clearing key */
				if (strcmp(param->u.crypt.alg, "none") != 0)
					psta->ieee8021x_blocked = false;

				if ((padapter->securitypriv.ndisencryptstatus == Ndis802_11Encryption2Enabled)||
						(padapter->securitypriv.ndisencryptstatus ==  Ndis802_11Encryption3Enabled)) {
					psta->dot118021XPrivacy = padapter->securitypriv.dot11PrivacyAlgrthm;
				}

				if (param->u.crypt.set_tx == 1) { /* pairwise key */
					memcpy(psta->dot118021x_UncstKey.skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));

					if (strcmp(param->u.crypt.alg, "TKIP") == 0) { /* set mic key */
						/* DEBUG_ERR(("\nset key length :param->u.crypt.key_len =%d\n", param->u.crypt.key_len)); */
						memcpy(psta->dot11tkiptxmickey.skey, &(param->u.crypt.key[16]), 8);
						memcpy(psta->dot11tkiprxmickey.skey, &(param->u.crypt.key[24]), 8);

						padapter->securitypriv.busetkipkey =false;
						/* _set_timer(&padapter->securitypriv.tkip_timer, 50); */
					}

					/* DEBUG_ERR((" param->u.crypt.key_len =%d\n", param->u.crypt.key_len)); */
					DBG_871X(" ~~~~set sta key:unicastkey\n");

					rtw_setstakey_cmd(padapter, psta, true, true);
				} else { /* group key */
					if (strcmp(param->u.crypt.alg, "TKIP") == 0 || strcmp(param->u.crypt.alg, "CCMP") == 0) {
						memcpy(padapter->securitypriv.dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
						/* only TKIP group key need to install this */
						if (param->u.crypt.key_len > 16) {
							memcpy(padapter->securitypriv.dot118021XGrptxmickey[param->u.crypt.idx].skey,&(param->u.crypt.key[16]), 8);
							memcpy(padapter->securitypriv.dot118021XGrprxmickey[param->u.crypt.idx].skey,&(param->u.crypt.key[24]), 8);
						}
						padapter->securitypriv.binstallGrpkey = true;
						/* DEBUG_ERR((" param->u.crypt.key_len =%d\n", param->u.crypt.key_len)); */
						DBG_871X(" ~~~~set sta key:groupkey\n");

						padapter->securitypriv.dot118021XGrpKeyid = param->u.crypt.idx;

						rtw_set_key(padapter,&padapter->securitypriv, param->u.crypt.idx, 1, true);
					} else if (strcmp(param->u.crypt.alg, "BIP") == 0) {
						/* printk("BIP key_len =%d , index =%d @@@@@@@@@@@@@@@@@@\n", param->u.crypt.key_len, param->u.crypt.idx); */
						/* save the IGTK key, length 16 bytes */
						memcpy(padapter->securitypriv.dot11wBIPKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
						/*printk("IGTK key below:\n");
						for (no = 0;no<16;no++)
							printk(" %02x ", padapter->securitypriv.dot11wBIPKey[param->u.crypt.idx].skey[no]);
						printk("\n");*/
						padapter->securitypriv.dot11wBIPKeyid = param->u.crypt.idx;
						padapter->securitypriv.binstallBIPkey = true;
						DBG_871X(" ~~~~set sta key:IGKT\n");
					}
				}
			}

			pbcmc_sta =rtw_get_bcmc_stainfo(padapter);
			if (pbcmc_sta == NULL) {
				/* DEBUG_ERR(("Set OID_802_11_ADD_KEY: bcmc stainfo is null\n")); */
			} else {
				/* Jeff: don't disable ieee8021x_blocked while clearing key */
				if (strcmp(param->u.crypt.alg, "none") != 0)
					pbcmc_sta->ieee8021x_blocked = false;

				if ((padapter->securitypriv.ndisencryptstatus == Ndis802_11Encryption2Enabled)||
						(padapter->securitypriv.ndisencryptstatus ==  Ndis802_11Encryption3Enabled)) {
					pbcmc_sta->dot118021XPrivacy = padapter->securitypriv.dot11PrivacyAlgrthm;
				}
			}
		} else if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) {
			/* adhoc mode */
		}
	}

exit:

	kfree(pwep);
	return ret;
}

static int rtw_set_wpa_ie(struct adapter *padapter, char *pie, unsigned short ielen)
{
	u8 *buf = NULL, *pos = NULL;
	int group_cipher = 0, pairwise_cipher = 0;
	int ret = 0;
	u8 null_addr[]= {0, 0, 0, 0, 0, 0};

	if ((ielen > MAX_WPA_IE_LEN) || (pie == NULL)) {
		_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);
		if (pie == NULL)
			return ret;
		else
			return -EINVAL;
	}

	if (ielen) {
		buf = rtw_zmalloc(ielen);
		if (buf == NULL) {
			ret =  -ENOMEM;
			goto exit;
		}

		memcpy(buf, pie , ielen);

		/* dump */
		{
			int i;
			DBG_871X("\n wpa_ie(length:%d):\n", ielen);
			for (i = 0;i<ielen;i =i+8)
				DBG_871X("0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x 0x%.2x\n", buf[i], buf[i+1], buf[i+2], buf[i+3], buf[i+4], buf[i+5], buf[i+6], buf[i+7]);
		}

		pos = buf;
		if (ielen < RSN_HEADER_LEN) {
			RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_, ("Ie len too short %d\n", ielen));
			ret  = -1;
			goto exit;
		}

		if (rtw_parse_wpa_ie(buf, ielen, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
			padapter->securitypriv.ndisauthtype =Ndis802_11AuthModeWPAPSK;
			memcpy(padapter->securitypriv.supplicant_ie, &buf[0], ielen);
		}

		if (rtw_parse_wpa2_ie(buf, ielen, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
			padapter->securitypriv.ndisauthtype =Ndis802_11AuthModeWPA2PSK;
			memcpy(padapter->securitypriv.supplicant_ie, &buf[0], ielen);
		}

		if (group_cipher == 0)
			group_cipher = WPA_CIPHER_NONE;
		if (pairwise_cipher == 0)
			pairwise_cipher = WPA_CIPHER_NONE;

		switch (group_cipher) {
			case WPA_CIPHER_NONE:
				padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
				padapter->securitypriv.ndisencryptstatus =Ndis802_11EncryptionDisabled;
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
				padapter->securitypriv.ndisencryptstatus =Ndis802_11EncryptionDisabled;
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
			u8 eid, wps_oui[4]={0x0, 0x50, 0xf2, 0x04};

			while (cnt < ielen) {
				eid = buf[cnt];

				if ((eid == _VENDOR_SPECIFIC_IE_) && (!memcmp(&buf[cnt+2], wps_oui, 4))) {
					DBG_871X("SET WPS_IE\n");

					padapter->securitypriv.wps_ie_len = ((buf[cnt+1]+2) < MAX_WPS_IE_LEN) ? (buf[cnt+1]+2):MAX_WPS_IE_LEN;

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
        if (padapter->securitypriv.dot11PrivacyAlgrthm == _TKIP_
                || padapter->securitypriv.dot11PrivacyAlgrthm == _TKIP_WTMIC_
                || padapter->securitypriv.dot11PrivacyAlgrthm == _AES_)
                /* WPS open need to enable multicast */
                /*  check_fwstate(&padapter->mlmepriv, WIFI_UNDER_WPS) == true) */
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
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	u32 ht_ielen = 0;
	char *p;
	u8 ht_cap =false, vht_cap =false;
	struct	mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct wlan_bssid_ex  *pcur_bss = &pmlmepriv->cur_network.network;
	NDIS_802_11_RATES_EX* prates = NULL;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("cmd_code =%x\n", info->cmd));

	if (check_fwstate(pmlmepriv, _FW_LINKED|WIFI_ADHOC_MASTER_STATE) == true) {
		/* parsing HT_CAP_IE */
		p = rtw_get_ie(&pcur_bss->IEs[12], _HT_CAPABILITY_IE_, &ht_ielen, pcur_bss->IELength-12);
		if (p && ht_ielen>0)
			ht_cap = true;

		prates = &pcur_bss->SupportedRates;

		if (rtw_is_cckratesonly_included((u8 *)prates)) {
			if (ht_cap)
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bn");
			else
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11b");
		} else if (rtw_is_cckrates_included((u8 *)prates)) {
			if (ht_cap)
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bgn");
			else
				snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11bg");
		} else {
			if (pcur_bss->Configuration.DSConfig > 14) {
				if (vht_cap)
					snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11AC");
				else if (ht_cap)
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
		/* prates = &padapter->registrypriv.dev_network.SupportedRates; */
		/* snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11g"); */
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
	struct	mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct wlan_bssid_ex  *pcur_bss = &pmlmepriv->cur_network.network;

	if (check_fwstate(pmlmepriv, _FW_LINKED) == true) {
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
	enum NDIS_802_11_NETWORK_INFRASTRUCTURE networkType ;
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
			DBG_871X("set_mode = IW_MODE_AUTO\n");
			break;
		case IW_MODE_ADHOC:
			networkType = Ndis802_11IBSS;
			DBG_871X("set_mode = IW_MODE_ADHOC\n");
			break;
		case IW_MODE_MASTER:
			networkType = Ndis802_11APMode;
			DBG_871X("set_mode = IW_MODE_MASTER\n");
                        /* rtw_setopmode_cmd(padapter, networkType, true); */
			break;
		case IW_MODE_INFRA:
			networkType = Ndis802_11Infrastructure;
			DBG_871X("set_mode = IW_MODE_INFRA\n");
			break;

		default :
			ret = -EINVAL;
			RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_err_, ("\n Mode: %s is not supported \n", iw_operation_mode[wrqu->mode]));
			goto exit;
	}

/*
	if (Ndis802_11APMode == networkType)
	{
		rtw_setopmode_cmd(padapter, networkType, true);
	}
	else
	{
		rtw_setopmode_cmd(padapter, Ndis802_11AutoUnknown, true);
	}
*/

	if (rtw_set_802_11_infrastructure_mode(padapter, networkType) ==false) {

		ret = -EPERM;
		goto exit;

	}

	rtw_setopmode_cmd(padapter, networkType, true);

exit:
	return ret;
}

static int rtw_wx_get_mode(struct net_device *dev, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, (" rtw_wx_get_mode\n"));

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true) {
		wrqu->mode = IW_MODE_INFRA;
	} else if  ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) ||
		       (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true)) {
		wrqu->mode = IW_MODE_ADHOC;
	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) {
		wrqu->mode = IW_MODE_MASTER;
	} else {
		wrqu->mode = IW_MODE_AUTO;
	}
	return 0;
}


static int rtw_wx_set_pmkid(struct net_device *dev,
	                     struct iw_request_info *a,
			     union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	u8          j, blInserted = false;
	int         intReturn = false;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
        struct iw_pmksa*  pPMK = (struct iw_pmksa*)extra;
        u8     strZeroMacAddress[ ETH_ALEN ] = { 0x00 };
        u8     strIssueBssid[ ETH_ALEN ] = { 0x00 };

	/*
        There are the BSSID information in the bssid.sa_data array.
        If cmd is IW_PMKSA_FLUSH, it means the wpa_suppplicant wants to clear all the PMKID information.
        If cmd is IW_PMKSA_ADD, it means the wpa_supplicant wants to add a PMKID/BSSID to driver.
        If cmd is IW_PMKSA_REMOVE, it means the wpa_supplicant wants to remove a PMKID/BSSID from driver.
        */

	memcpy(strIssueBssid, pPMK->bssid.sa_data, ETH_ALEN);
        if (pPMK->cmd == IW_PMKSA_ADD) {
                DBG_871X("[rtw_wx_set_pmkid] IW_PMKSA_ADD!\n");
                if (!memcmp(strIssueBssid, strZeroMacAddress, ETH_ALEN))
                    return(intReturn);
                else
                    intReturn = true;

		blInserted = false;

		/* overwrite PMKID */
		for (j = 0 ; j<NUM_PMKID_CACHE; j++) {
			if (!memcmp(psecuritypriv->PMKIDList[j].Bssid, strIssueBssid, ETH_ALEN)) {
				/*  BSSID is matched, the same AP => rewrite with new PMKID. */
                                DBG_871X("[rtw_wx_set_pmkid] BSSID exists in the PMKList.\n");

				memcpy(psecuritypriv->PMKIDList[j].PMKID, pPMK->pmkid, IW_PMKID_LEN);
                                psecuritypriv->PMKIDList[ j ].bUsed = true;
				psecuritypriv->PMKIDIndex = j+1;
				blInserted = true;
				break;
			}
	        }

	        if (!blInserted) {
		    /*  Find a new entry */
                    DBG_871X("[rtw_wx_set_pmkid] Use the new entry index = %d for this PMKID.\n",
                            psecuritypriv->PMKIDIndex);

	            memcpy(psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].Bssid, strIssueBssid, ETH_ALEN);
		    memcpy(psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].PMKID, pPMK->pmkid, IW_PMKID_LEN);

                    psecuritypriv->PMKIDList[ psecuritypriv->PMKIDIndex ].bUsed = true;
		    psecuritypriv->PMKIDIndex++ ;
		    if (psecuritypriv->PMKIDIndex == 16)
		        psecuritypriv->PMKIDIndex = 0;
		}
        } else if (pPMK->cmd == IW_PMKSA_REMOVE) {
                DBG_871X("[rtw_wx_set_pmkid] IW_PMKSA_REMOVE!\n");
                intReturn = true;
		for (j = 0 ; j<NUM_PMKID_CACHE; j++) {
			if (!memcmp(psecuritypriv->PMKIDList[j].Bssid, strIssueBssid, ETH_ALEN)) {
				/*  BSSID is matched, the same AP => Remove this PMKID information and reset it. */
                                eth_zero_addr(psecuritypriv->PMKIDList[j].Bssid);
                                psecuritypriv->PMKIDList[ j ].bUsed = false;
				break;
			}
	        }
        } else if (pPMK->cmd == IW_PMKSA_FLUSH) {
            DBG_871X("[rtw_wx_set_pmkid] IW_PMKSA_FLUSH!\n");
            memset(&psecuritypriv->PMKIDList[ 0 ], 0x00, sizeof(RT_PMKID_LIST) * NUM_PMKID_CACHE);
            psecuritypriv->PMKIDIndex = 0;
            intReturn = true;
        }
	return intReturn;
}

static int rtw_wx_get_sens(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	{
		wrqu->sens.value = 0;
		wrqu->sens.fixed = 0;	/* no auto select */
		wrqu->sens.disabled = 1;
	}
	return 0;
}

static int rtw_wx_get_range(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct iw_range *range = (struct iw_range *)extra;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;

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
	range->avg_qual.level = 256 - 78;
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

	range->enc_capa = IW_ENC_CAPA_WPA|IW_ENC_CAPA_WPA2|
			  IW_ENC_CAPA_CIPHER_TKIP|IW_ENC_CAPA_CIPHER_CCMP;

	range->scan_capa = IW_SCAN_CAPA_ESSID | IW_SCAN_CAPA_TYPE |IW_SCAN_CAPA_BSSID|
					IW_SCAN_CAPA_CHANNEL|IW_SCAN_CAPA_MODE|IW_SCAN_CAPA_RATE;

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
	struct	mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct list_head	*phead;
	u8 *dst_bssid, *src_bssid;
	struct __queue	*queue	= &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	enum NDIS_802_11_AUTHENTICATION_MODE	authmode;

	rtw_ps_deny(padapter, PS_DENY_JOIN);
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
	pmlmepriv->pscanned = get_next(phead);

	while (1) {
		if (phead == pmlmepriv->pscanned)
			break;

		pnetwork = LIST_CONTAINOR(pmlmepriv->pscanned, struct wlan_network, list);

		pmlmepriv->pscanned = get_next(pmlmepriv->pscanned);

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

	rtw_ps_deny_cancel(padapter, PS_DENY_JOIN);

	return ret;
}

static int rtw_wx_get_wap(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{

	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct wlan_bssid_ex  *pcur_bss = &pmlmepriv->cur_network.network;

	wrqu->ap_addr.sa_family = ARPHRD_ETHER;

	eth_zero_addr(wrqu->ap_addr.sa_data);

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_wx_get_wap\n"));

	if  (((check_fwstate(pmlmepriv, _FW_LINKED)) == true) ||
			((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) == true) ||
			((check_fwstate(pmlmepriv, WIFI_AP_STATE)) == true)) {
		memcpy(wrqu->ap_addr.sa_data, pcur_bss->MacAddress, ETH_ALEN);
	} else {
		eth_zero_addr(wrqu->ap_addr.sa_data);
	}

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

	DBG_871X("%s\n", __func__);

	reason = mlme->reason_code;

	DBG_871X("%s, cmd =%d, reason =%d\n", __func__, mlme->cmd, reason);

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

	#ifdef DBG_IOCTL
	DBG_871X("DBG_IOCTL %s:%d\n", __func__, __LINE__);
	#endif

	rtw_ps_deny(padapter, PS_DENY_SCAN);
	if (_FAIL == rtw_pwr_wakeup(padapter)) {
		ret = -1;
		goto exit;
	}

	if (padapter->bDriverStopped) {
		DBG_871X("bDriverStopped =%d\n", padapter->bDriverStopped);
		ret = -1;
		goto exit;
	}

	if (!padapter->bup) {
		ret = -1;
		goto exit;
	}

	if (!padapter->hw_init_completed ) {
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

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == true) {
		indicate_wx_scan_complete_event(padapter);
		goto exit;
	}

	memset(ssid, 0, sizeof(struct ndis_802_11_ssid)*RTW_SSID_SCAN_AMOUNT);

	if (wrqu->data.length == sizeof(struct iw_scan_req)) {
		struct iw_scan_req *req = (struct iw_scan_req *)extra;

		if (wrqu->data.flags & IW_SCAN_THIS_ESSID) {
			int len = min((int)req->essid_len, IW_ESSID_MAX_SIZE);

			memcpy(ssid[0].Ssid, req->essid, len);
			ssid[0].SsidLength = len;

			DBG_871X("IW_SCAN_THIS_ESSID, ssid =%s, len =%d\n", req->essid, req->essid_len);

			spin_lock_bh(&pmlmepriv->lock);

			_status = rtw_sitesurvey_cmd(padapter, ssid, 1, NULL, 0);

			spin_unlock_bh(&pmlmepriv->lock);

		} else if (req->scan_type == IW_SCAN_TYPE_PASSIVE) {
			DBG_871X("rtw_wx_set_scan, req->scan_type == IW_SCAN_TYPE_PASSIVE\n");
		}

	} else if (wrqu->data.length >= WEXT_CSCAN_HEADER_SIZE
		&& !memcmp(extra, WEXT_CSCAN_HEADER, WEXT_CSCAN_HEADER_SIZE)) {
		int len = wrqu->data.length -WEXT_CSCAN_HEADER_SIZE;
		char *pos = extra+WEXT_CSCAN_HEADER_SIZE;
		char section;
		char sec_len;
		int ssid_index = 0;

		/* DBG_871X("%s COMBO_SCAN header is recognized\n", __func__); */

		while (len >= 1) {
			section = *(pos++); len-= 1;

			switch (section) {
				case WEXT_CSCAN_SSID_SECTION:
					/* DBG_871X("WEXT_CSCAN_SSID_SECTION\n"); */
					if (len < 1) {
						len = 0;
						break;
					}

					sec_len = *(pos++); len-= 1;

					if (sec_len>0 && sec_len<=len) {
						ssid[ssid_index].SsidLength = sec_len;
						memcpy(ssid[ssid_index].Ssid, pos, ssid[ssid_index].SsidLength);
						/* DBG_871X("%s COMBO_SCAN with specific ssid:%s, %d\n", __func__ */
						/* 	, ssid[ssid_index].Ssid, ssid[ssid_index].SsidLength); */
						ssid_index++;
					}

					pos+=sec_len; len-=sec_len;
					break;


				case WEXT_CSCAN_CHANNEL_SECTION:
					/* DBG_871X("WEXT_CSCAN_CHANNEL_SECTION\n"); */
					pos+= 1; len-= 1;
					break;
				case WEXT_CSCAN_ACTV_DWELL_SECTION:
					/* DBG_871X("WEXT_CSCAN_ACTV_DWELL_SECTION\n"); */
					pos+=2; len-=2;
					break;
				case WEXT_CSCAN_PASV_DWELL_SECTION:
					/* DBG_871X("WEXT_CSCAN_PASV_DWELL_SECTION\n"); */
					pos+=2; len-=2;
					break;
				case WEXT_CSCAN_HOME_DWELL_SECTION:
					/* DBG_871X("WEXT_CSCAN_HOME_DWELL_SECTION\n"); */
					pos+=2; len-=2;
					break;
				case WEXT_CSCAN_TYPE_SECTION:
					/* DBG_871X("WEXT_CSCAN_TYPE_SECTION\n"); */
					pos+= 1; len-= 1;
					break;
				default:
					/* DBG_871X("Unknown CSCAN section %c\n", section); */
					len = 0; /*  stop parsing */
			}
			/* DBG_871X("len:%d\n", len); */

		}

		/* jeff: it has still some scan paramater to parse, we only do this now... */
		_status = rtw_set_802_11_bssid_list_scan(padapter, ssid, RTW_SSID_SCAN_AMOUNT);

	} else {
		_status = rtw_set_802_11_bssid_list_scan(padapter, NULL, 0);
	}

	if (_status == false)
		ret = -1;

exit:

	rtw_ps_deny_cancel(padapter, PS_DENY_SCAN);

	#ifdef DBG_IOCTL
	DBG_871X("DBG_IOCTL %s:%d return %d\n", __func__, __LINE__, ret);
	#endif

	return ret;
}

static int rtw_wx_get_scan(struct net_device *dev, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *extra)
{
	struct list_head					*plist, *phead;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct __queue				*queue	= &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;
	char *ev = extra;
	char *stop = ev + wrqu->data.length;
	u32 ret = 0;
	sint wait_status;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_wx_get_scan\n"));
	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_, (" Start of Query SIOCGIWSCAN .\n"));

	#ifdef DBG_IOCTL
	DBG_871X("DBG_IOCTL %s:%d\n", __func__, __LINE__);
	#endif

	if (adapter_to_pwrctl(padapter)->brfoffbyhw && padapter->bDriverStopped) {
		ret = -EINVAL;
		goto exit;
	}

	wait_status = _FW_UNDER_SURVEY | _FW_UNDER_LINKING;

	if (check_fwstate(pmlmepriv, wait_status))
		return -EAGAIN;

	spin_lock_bh(&(pmlmepriv->scanned_queue.lock));

	phead = get_list_head(queue);
	plist = get_next(phead);

	while (1) {
		if (phead == plist)
			break;

		if ((stop - ev) < SCAN_ITEM_SIZE) {
			ret = -E2BIG;
			break;
		}

		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);

		/* report network only if the current channel set contains the channel to which this network belongs */
		if (rtw_ch_set_search_ch(padapter->mlmeextpriv.channel_set, pnetwork->network.Configuration.DSConfig) >= 0
			&& rtw_mlme_band_check(padapter, pnetwork->network.Configuration.DSConfig) == true
			&& true == rtw_validate_ssid(&(pnetwork->network.Ssid))) {

			ev =translate_scan(padapter, a, pnetwork, ev, stop);
		}

		plist = get_next(plist);

	}

	spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));

	wrqu->data.length = ev-extra;
	wrqu->data.flags = 0;

exit:

	#ifdef DBG_IOCTL
	DBG_871X("DBG_IOCTL %s:%d return %d\n", __func__, __LINE__, ret);
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
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct __queue *queue = &pmlmepriv->scanned_queue;
	struct list_head *phead;
	struct wlan_network *pnetwork = NULL;
	enum NDIS_802_11_AUTHENTICATION_MODE authmode;
	struct ndis_802_11_ssid ndis_ssid;
	u8 *dst_ssid, *src_ssid;

	uint ret = 0, len;

	#ifdef DBG_IOCTL
	DBG_871X("DBG_IOCTL %s:%d\n", __func__, __LINE__);
	#endif

	RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
		 ("+rtw_wx_set_essid: fw_state = 0x%08x\n", get_fwstate(pmlmepriv)));

	rtw_ps_deny(padapter, PS_DENY_JOIN);
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
	DBG_871X("=>%s\n", __func__);
	if (wrqu->essid.flags && wrqu->essid.length) {
		len = (wrqu->essid.length < IW_ESSID_MAX_SIZE) ? wrqu->essid.length : IW_ESSID_MAX_SIZE;

		if (wrqu->essid.length != 33)
			DBG_871X("ssid =%s, len =%d\n", extra, wrqu->essid.length);

		memset(&ndis_ssid, 0, sizeof(struct ndis_802_11_ssid));
		ndis_ssid.SsidLength = len;
		memcpy(ndis_ssid.Ssid, extra, len);
		src_ssid = ndis_ssid.Ssid;

		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_, ("rtw_wx_set_essid: ssid =[%s]\n", src_ssid));
		spin_lock_bh(&queue->lock);
		phead = get_list_head(queue);
		pmlmepriv->pscanned = get_next(phead);

		while (1) {
			if (phead == pmlmepriv->pscanned) {
			        RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_warning_,
					 ("rtw_wx_set_essid: scan_q is empty, set ssid to check if scanning again!\n"));

				break;
			}

			pnetwork = LIST_CONTAINOR(pmlmepriv->pscanned, struct wlan_network, list);

			pmlmepriv->pscanned = get_next(pmlmepriv->pscanned);

			dst_ssid = pnetwork->network.Ssid.Ssid;

			RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
				 ("rtw_wx_set_essid: dst_ssid =%s\n",
				  pnetwork->network.Ssid.Ssid));

			if ((!memcmp(dst_ssid, src_ssid, ndis_ssid.SsidLength)) &&
				(pnetwork->network.Ssid.SsidLength ==ndis_ssid.SsidLength)) {
				RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_,
					 ("rtw_wx_set_essid: find match, set infra mode\n"));

				if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true) {
					if (pnetwork->network.InfrastructureMode != pmlmepriv->cur_network.network.InfrastructureMode)
						continue;
				}

				if (rtw_set_802_11_infrastructure_mode(padapter, pnetwork->network.InfrastructureMode) == false) {
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
		/* set_802_11_encryption_mode(padapter, padapter->securitypriv.ndisencryptstatus); */
		if (rtw_set_802_11_ssid(padapter, &ndis_ssid) == false) {
			ret = -1;
			goto exit;
		}
	}

exit:

	rtw_ps_deny_cancel(padapter, PS_DENY_JOIN);

	DBG_871X("<=%s, ret %d\n", __func__, ret);

	#ifdef DBG_IOCTL
	DBG_871X("DBG_IOCTL %s:%d return %d\n", __func__, __LINE__, ret);
	#endif

	return ret;
}

static int rtw_wx_get_essid(struct net_device *dev,
			      struct iw_request_info *a,
			      union iwreq_data *wrqu, char *extra)
{
	u32 len, ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct	mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct wlan_bssid_ex  *pcur_bss = &pmlmepriv->cur_network.network;

	RT_TRACE(_module_rtl871x_mlme_c_, _drv_info_, ("rtw_wx_get_essid\n"));

	if ((check_fwstate(pmlmepriv, _FW_LINKED) == true) ||
	      (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true)) {
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
	int	i, ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	u8 datarates[NumRates];
	u32 target_rate = wrqu->bitrate.value;
	u32 fixed = wrqu->bitrate.fixed;
	u32 ratevalue = 0;
	u8 mpdatarate[NumRates]={11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0xff};

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

	for (i = 0; i<NumRates; i++) {
		if (ratevalue ==mpdatarate[i]) {
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

	if (wrqu->rts.disabled)
		padapter->registrypriv.rts_thresh = 2347;
	else {
		if (wrqu->rts.value < 0 ||
		    wrqu->rts.value > 2347)
			return -EINVAL;

		padapter->registrypriv.rts_thresh = wrqu->rts.value;
	}

	DBG_871X("%s, rts_thresh =%d\n", __func__, padapter->registrypriv.rts_thresh);

	return 0;
}

static int rtw_wx_get_rts(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

	DBG_871X("%s, rts_thresh =%d\n", __func__, padapter->registrypriv.rts_thresh);

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

	if (wrqu->frag.disabled)
		padapter->xmitpriv.frag_len = MAX_FRAG_THRESHOLD;
	else {
		if (wrqu->frag.value < MIN_FRAG_THRESHOLD ||
		    wrqu->frag.value > MAX_FRAG_THRESHOLD)
			return -EINVAL;

		padapter->xmitpriv.frag_len = wrqu->frag.value & ~0x1;
	}

	DBG_871X("%s, frag_len =%d\n", __func__, padapter->xmitpriv.frag_len);

	return 0;

}

static int rtw_wx_get_frag(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

	DBG_871X("%s, frag_len =%d\n", __func__, padapter->xmitpriv.frag_len);

	wrqu->frag.value = padapter->xmitpriv.frag_len;
	wrqu->frag.fixed = 0;	/* no auto select */
	/* wrqu->frag.disabled = (wrqu->frag.value == DEFAULT_FRAG_THRESHOLD); */

	return 0;
}

static int rtw_wx_get_retry(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	/* struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev); */


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
	enum NDIS_802_11_AUTHENTICATION_MODE authmode;

	struct iw_point *erq = &(wrqu->encoding);
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	DBG_871X("+rtw_wx_set_enc, flags = 0x%x\n", erq->flags);

	memset(&wep, 0, sizeof(struct ndis_802_11_wep));

	key = erq->flags & IW_ENCODE_INDEX;

	if (erq->flags & IW_ENCODE_DISABLED) {
		DBG_871X("EncryptionDisabled\n");
		padapter->securitypriv.ndisencryptstatus = Ndis802_11EncryptionDisabled;
		padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
		padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open; /* open system */
		authmode = Ndis802_11AuthModeOpen;
		padapter->securitypriv.ndisauthtype =authmode;

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
		DBG_871X("rtw_wx_set_enc, key =%d\n", key);
	}

	/* set authentication mode */
	if (erq->flags & IW_ENCODE_OPEN) {
		DBG_871X("rtw_wx_set_enc():IW_ENCODE_OPEN\n");
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;/* Ndis802_11EncryptionDisabled; */

		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open;

		padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
		padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
		authmode = Ndis802_11AuthModeOpen;
		padapter->securitypriv.ndisauthtype =authmode;
	} else if (erq->flags & IW_ENCODE_RESTRICTED) {
		DBG_871X("rtw_wx_set_enc():IW_ENCODE_RESTRICTED\n");
		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;

		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Shared;

		padapter->securitypriv.dot11PrivacyAlgrthm = _WEP40_;
		padapter->securitypriv.dot118021XGrpPrivacy = _WEP40_;
		authmode = Ndis802_11AuthModeShared;
		padapter->securitypriv.ndisauthtype =authmode;
	} else {
		DBG_871X("rtw_wx_set_enc():erq->flags = 0x%x\n", erq->flags);

		padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption1Enabled;/* Ndis802_11EncryptionDisabled; */
		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open; /* open system */
		padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
		padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
		authmode = Ndis802_11AuthModeOpen;
		padapter->securitypriv.ndisauthtype =authmode;
	}

	wep.KeyIndex = key;
	if (erq->length > 0) {
		wep.KeyLength = erq->length <= 5 ? 5 : 13;

		wep.Length = wep.KeyLength + FIELD_OFFSET(struct ndis_802_11_wep, KeyMaterial);
	} else {
		wep.KeyLength = 0 ;

		if (keyindex_provided == 1) { /*  set key_id only, no given KeyMaterial(erq->length == 0). */
			padapter->securitypriv.dot11PrivacyKeyIndex = key;

			DBG_871X("(keyindex_provided == 1), keyid =%d, key_len =%d\n", key, padapter->securitypriv.dot11DefKeylen[key]);

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
	struct	mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	if (check_fwstate(pmlmepriv, _FW_LINKED) != true) {
		 if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) != true) {
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

	/* if (padapter->securitypriv.ndisauthtype == Ndis802_11AuthModeOpen) */
	/*  */
	/*       erq->flags |= IW_ENCODE_OPEN; */
	/*  */

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
	/* struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev); */

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
		/* wpa_supplicant is setting the tkip countermeasure. */
		if (param->value) /* enabling */
			padapter->securitypriv.btkip_countermeasure = true;
		else /* disabling */
			padapter->securitypriv.btkip_countermeasure = false;
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

		/*
		 * This means init value, or using wep, ndisencryptstatus =
		 * Ndis802_11Encryption1Enabled, then it needn't reset it;
		 */
		if (padapter->securitypriv.ndisencryptstatus == Ndis802_11Encryption1Enabled)
			break;

		if (param->value) {
			padapter->securitypriv.ndisencryptstatus = Ndis802_11EncryptionDisabled;
			padapter->securitypriv.dot11PrivacyAlgrthm = _NO_PRIVACY_;
			padapter->securitypriv.dot118021XGrpPrivacy = _NO_PRIVACY_;
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_Open; /* open system */
			padapter->securitypriv.ndisauthtype =Ndis802_11AuthModeOpen;
		}

		break;
	case IW_AUTH_80211_AUTH_ALG:
		/*
		 *  It's the starting point of a link layer connection using wpa_supplicant
		 */
		if (check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {
			LeaveAllPowerSaveMode(padapter);
			rtw_disassoc_cmd(padapter, 500, false);
			DBG_871X("%s...call rtw_indicate_disconnect\n ", __func__);
			rtw_indicate_disconnect(padapter);
			rtw_free_assoc_resources(padapter, 1);
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
	param = rtw_malloc(param_len);
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
	case IW_ENCODE_ALG_AES_CMAC:
		alg_name = "BIP";
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
		((pext->ext_flags & IW_ENCODE_EXT_GROUP_KEY)
		|| (pext->ext_flags & IW_ENCODE_ALG_AES_CMAC)))	{
		param->u.crypt.set_tx = 0;
	}

	param->u.crypt.idx = (pencoding->flags&0x00FF) -1 ;

	if (pext->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID)
		memcpy(param->u.crypt.seq, pext->rx_seq, 8);

	if (pext->key_len) {
		param->u.crypt.key_len = pext->key_len;
		/* memcpy(param + 1, pext + 1, pext->key_len); */
		memcpy(param->u.crypt.key, pext + 1, pext->key_len);
	}

	if (pencoding->flags & IW_ENCODE_DISABLED) {
		/* todo: remove key */
		/* remove = 1; */
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
	/* struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev); */
	 /* struct mlme_priv *pmlmepriv = &(padapter->mlmepriv); */
	 /* struct security_priv *psecuritypriv = &padapter->securitypriv; */

	if (extra) {
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
	struct adapter *padapter;
	struct iw_point *p;
	u16 len;
	u32 addr;
	u32 data32;
	u32 bytes;
	u8 *ptmp;
	int ret;


	ret = 0;
	padapter = (struct adapter *)rtw_netdev_priv(dev);
	p = &wrqu->data;
	len = p->length;
	if (0 == len)
		return -EINVAL;

	ptmp = rtw_malloc(len);
	if (NULL == ptmp)
		return -ENOMEM;

	if (copy_from_user(ptmp, p->pointer, len)) {
		ret = -EFAULT;
		goto exit;
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
			DBG_871X(KERN_INFO "%s: usage> read [bytes],[address(hex)]\n", __func__);
			ret = -EINVAL;
			goto exit;
	}
	DBG_871X(KERN_INFO "%s: addr = 0x%08X data =%s\n", __func__, addr, extra);

exit:
	kfree(ptmp);

	return ret;
}

static int rtw_wx_write32(struct net_device *dev,
                            struct iw_request_info *info,
                            union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

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
			DBG_871X(KERN_INFO "%s: addr = 0x%08X data = 0x%02X\n", __func__, addr, (u8)data32);
			break;
		case 2:
			rtw_write16(padapter, addr, (u16)data32);
			DBG_871X(KERN_INFO "%s: addr = 0x%08X data = 0x%04X\n", __func__, addr, (u16)data32);
			break;
		case 4:
			rtw_write32(padapter, addr, data32);
			DBG_871X(KERN_INFO "%s: addr = 0x%08X data = 0x%08X\n", __func__, addr, data32);
			break;
		default:
			DBG_871X(KERN_INFO "%s: usage> write [bytes],[address(hex)],[data(hex)]\n", __func__);
			return -EINVAL;
	}

	return 0;
}

static int rtw_wx_read_rf(struct net_device *dev,
                            struct iw_request_info *info,
                            union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
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
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	u32 path, addr, data32;


	path = *(u32*)extra;
	addr = *((u32*)extra + 1);
	data32 = *((u32*)extra + 2);
/* 	DBG_871X("%s: path =%d addr = 0x%02x data = 0x%05x\n", __func__, path, addr, data32); */
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
	/* struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev); */
	/* struct mlme_priv *pmlmepriv = &(padapter->mlmepriv); */

	/* DBG_871X("cmd_code =%x, fwstate = 0x%x\n", a->cmd, get_fwstate(pmlmepriv)); */

	return -1;

}

static int rtw_wx_set_channel_plan(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	u8 channel_plan_req = (u8)(*((int *)wrqu));

	if (_SUCCESS == rtw_set_chplan_cmd(padapter, channel_plan_req, 1, 1))
		DBG_871X("%s set channel_plan = 0x%02X\n", __func__, channel_plan_req);
	 else
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
typedef int (*iw_handler)(struct net_device *dev, struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra);
*/
/*
 *For all data larger than 16 octets, we need to use a
 *pointer to memory allocated in user space.
 */
static  int rtw_drvext_hdl(struct net_device *dev, struct iw_request_info *info,
						union iwreq_data *wrqu, char *extra)
{
	return 0;
}

static int rtw_mp_ioctl_hdl(struct net_device *dev, struct iw_request_info *info,
						union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	return ret;
}

static int rtw_get_ap_info(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	u32 cnt = 0, wpa_ielen;
	struct list_head	*plist, *phead;
	unsigned char *pbuf;
	u8 bssid[ETH_ALEN];
	char data[32];
	struct wlan_network *pnetwork = NULL;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct __queue *queue = &(pmlmepriv->scanned_queue);
	struct iw_point *pdata = &wrqu->data;

	DBG_871X("+rtw_get_aplist_info\n");

	if ((padapter->bDriverStopped) || (pdata == NULL)) {
		ret = -EINVAL;
		goto exit;
	}

	while ((check_fwstate(pmlmepriv, (_FW_UNDER_SURVEY|_FW_UNDER_LINKING))) == true) {
		msleep(30);
		cnt++;
		if (cnt > 100)
			break;
	}


	/* pdata->length = 0;? */
	pdata->flags = 0;
	if (pdata->length>=32) {
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
	plist = get_next(phead);

	while (1) {
		if (phead == plist)
			break;


		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);

		if (!mac_pton(data, bssid)) {
			DBG_871X("Invalid BSSID '%s'.\n", (u8 *)data);
			spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
			return -EINVAL;
		}


		if (!memcmp(bssid, pnetwork->network.MacAddress, ETH_ALEN)) { /* BSSID match, then check if supporting wpa/wpa2 */
			DBG_871X("BSSID:" MAC_FMT "\n", MAC_ARG(bssid));

			pbuf = rtw_get_wpa_ie(&pnetwork->network.IEs[12], &wpa_ielen, pnetwork->network.IELength-12);
			if (pbuf && (wpa_ielen>0)) {
				pdata->flags = 1;
				break;
			}

			pbuf = rtw_get_wpa2_ie(&pnetwork->network.IEs[12], &wpa_ielen, pnetwork->network.IELength-12);
			if (pbuf && (wpa_ielen>0)) {
				pdata->flags = 2;
				break;
			}
		}

		plist = get_next(plist);

	}

	spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));

	if (pdata->length>=34) {
		if (copy_to_user((u8 __force __user *)pdata->pointer+32, (u8 *)&pdata->flags, 1)) {
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
	struct adapter *padapter = rtw_netdev_priv(dev);
	int *pdata = (int *)wrqu;
	int selector;

	if ((padapter->bDriverStopped) || (pdata == NULL)) {
		ret = -EINVAL;
		goto exit;
	}

	selector = *pdata;
	if (selector < 3 && selector >= 0) {
		padapter->pid[selector] = *(pdata+1);
		DBG_871X("%s set pid[%d]=%d\n", __func__, selector , padapter->pid[selector]);
	}
	else
		DBG_871X("%s selector %d error\n", __func__, selector);

exit:

	return ret;

}

static int rtw_wps_start(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{

	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct iw_point *pdata = &wrqu->data;
	u32   u32wps_start = 0;
        unsigned int uintRet = 0;

	if ((true == padapter->bDriverStopped) ||(true ==padapter->bSurpriseRemoved) || (NULL == pdata)) {
		ret = -EINVAL;
		goto exit;
	}

	uintRet = copy_from_user((void*)&u32wps_start, pdata->pointer, 4);
	if (u32wps_start == 0)
		u32wps_start = *extra;

	DBG_871X("[%s] wps_start = %d\n", __func__, u32wps_start);

#ifdef CONFIG_INTEL_WIDI
	process_intel_widi_wps_status(padapter, u32wps_start);
#endif /* CONFIG_INTEL_WIDI */

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
	struct adapter *padapter = rtw_netdev_priv(dev);
	struct rereg_nd_name_data *rereg_priv = &padapter->rereg_nd_name_priv;
	char new_ifname[IFNAMSIZ];

	if (rereg_priv->old_ifname[0] == 0) {
		char *reg_ifname;
		reg_ifname = padapter->registrypriv.ifname;

		strncpy(rereg_priv->old_ifname, reg_ifname, IFNAMSIZ);
		rereg_priv->old_ifname[IFNAMSIZ-1] = 0;
	}

	/* DBG_871X("%s wrqu->data.length:%d\n", __func__, wrqu->data.length); */
	if (wrqu->data.length > IFNAMSIZ)
		return -EFAULT;

	if (copy_from_user(new_ifname, wrqu->data.pointer, IFNAMSIZ))
		return -EFAULT;

	if (0 == strcmp(rereg_priv->old_ifname, new_ifname))
		return ret;

	DBG_871X("%s new_ifname:%s\n", __func__, new_ifname);
	if (0 != (ret = rtw_change_ifname(padapter, new_ifname)))
		goto exit;

	strncpy(rereg_priv->old_ifname, new_ifname, IFNAMSIZ);
	rereg_priv->old_ifname[IFNAMSIZ-1] = 0;

	if (!memcmp(new_ifname, "disable%d", 9)) {

		DBG_871X("%s disable\n", __func__);
		/*  free network queue for Android's timming issue */
		rtw_free_network_queue(padapter, true);

		/*  the interface is being "disabled", we can do deeper IPS */
		/* rereg_priv->old_ips_mode = rtw_get_ips_mode_req(&padapter->pwrctrlpriv); */
		/* rtw_ips_mode_req(&padapter->pwrctrlpriv, IPS_NORMAL); */
	}
exit:
	return ret;

}

static int rtw_dbg_port(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	u8 major_cmd, minor_cmd;
	u16 arg;
	u32 extra_arg, *pdata, val32;
	struct sta_info *psta;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_network *cur_network = &(pmlmepriv->cur_network);
	struct sta_priv *pstapriv = &padapter->stapriv;


	pdata = (u32*)&wrqu->data;

	val32 = *pdata;
	arg = (u16)(val32&0x0000ffff);
	major_cmd = (u8)(val32>>24);
	minor_cmd = (u8)((val32>>16)&0x00ff);

	extra_arg = *(pdata+1);

	switch (major_cmd) {
		case 0x70:/* read_reg */
			switch (minor_cmd) {
				case 1:
					DBG_871X("rtw_read8(0x%x) = 0x%02x\n", arg, rtw_read8(padapter, arg));
					break;
				case 2:
					DBG_871X("rtw_read16(0x%x) = 0x%04x\n", arg, rtw_read16(padapter, arg));
					break;
				case 4:
					DBG_871X("rtw_read32(0x%x) = 0x%08x\n", arg, rtw_read32(padapter, arg));
					break;
			}
			break;
		case 0x71:/* write_reg */
			switch (minor_cmd) {
				case 1:
					rtw_write8(padapter, arg, extra_arg);
					DBG_871X("rtw_write8(0x%x) = 0x%02x\n", arg, rtw_read8(padapter, arg));
					break;
				case 2:
					rtw_write16(padapter, arg, extra_arg);
					DBG_871X("rtw_write16(0x%x) = 0x%04x\n", arg, rtw_read16(padapter, arg));
					break;
				case 4:
					rtw_write32(padapter, arg, extra_arg);
					DBG_871X("rtw_write32(0x%x) = 0x%08x\n", arg, rtw_read32(padapter, arg));
					break;
			}
			break;
		case 0x72:/* read_bb */
			DBG_871X("read_bbreg(0x%x) = 0x%x\n", arg, rtw_hal_read_bbreg(padapter, arg, 0xffffffff));
			break;
		case 0x73:/* write_bb */
			rtw_hal_write_bbreg(padapter, arg, 0xffffffff, extra_arg);
			DBG_871X("write_bbreg(0x%x) = 0x%x\n", arg, rtw_hal_read_bbreg(padapter, arg, 0xffffffff));
			break;
		case 0x74:/* read_rf */
			DBG_871X("read RF_reg path(0x%02x), offset(0x%x), value(0x%08x)\n", minor_cmd, arg, rtw_hal_read_rfreg(padapter, minor_cmd, arg, 0xffffffff));
			break;
		case 0x75:/* write_rf */
			rtw_hal_write_rfreg(padapter, minor_cmd, arg, 0xffffffff, extra_arg);
			DBG_871X("write RF_reg path(0x%02x), offset(0x%x), value(0x%08x)\n", minor_cmd, arg, rtw_hal_read_rfreg(padapter, minor_cmd, arg, 0xffffffff));
			break;

		case 0x76:
			switch (minor_cmd) {
				case 0x00: /* normal mode, */
					padapter->recvpriv.is_signal_dbg = 0;
					break;
				case 0x01: /* dbg mode */
					padapter->recvpriv.is_signal_dbg = 1;
					extra_arg = extra_arg>100?100:extra_arg;
					padapter->recvpriv.signal_strength_dbg =extra_arg;
					break;
			}
			break;
		case 0x78: /* IOL test */
			break;
		case 0x79:
			{
				/*
				* dbg 0x79000000 [value], set RESP_TXAGC to + value, value:0~15
				* dbg 0x79010000 [value], set RESP_TXAGC to - value, value:0~15
				*/
				u8 value =  extra_arg & 0x0f;
				u8 sign = minor_cmd;
				u16 write_value = 0;

				DBG_871X("%s set RESP_TXAGC to %s %u\n", __func__, sign?"minus":"plus", value);

				if (sign)
					value = value | 0x10;

				write_value = value | (value << 5);
				rtw_write16(padapter, 0x6d9, write_value);
			}
			break;
		case 0x7a:
			receive_disconnect(padapter, pmlmeinfo->network.MacAddress
				, WLAN_REASON_EXPIRATION_CHK);
			break;
		case 0x7F:
			switch (minor_cmd) {
				case 0x0:
					DBG_871X("fwstate = 0x%x\n", get_fwstate(pmlmepriv));
					break;
				case 0x01:
					DBG_871X("minor_cmd 0x%x\n", minor_cmd);
					break;
				case 0x02:
					DBG_871X("pmlmeinfo->state = 0x%x\n", pmlmeinfo->state);
					DBG_871X("DrvBcnEarly =%d\n", pmlmeext->DrvBcnEarly);
					DBG_871X("DrvBcnTimeOut =%d\n", pmlmeext->DrvBcnTimeOut);
					break;
				case 0x03:
					DBG_871X("qos_option =%d\n", pmlmepriv->qospriv.qos_option);
					DBG_871X("ht_option =%d\n", pmlmepriv->htpriv.ht_option);
					break;
				case 0x04:
					DBG_871X("cur_ch =%d\n", pmlmeext->cur_channel);
					DBG_871X("cur_bw =%d\n", pmlmeext->cur_bwmode);
					DBG_871X("cur_ch_off =%d\n", pmlmeext->cur_ch_offset);

					DBG_871X("oper_ch =%d\n", rtw_get_oper_ch(padapter));
					DBG_871X("oper_bw =%d\n", rtw_get_oper_bw(padapter));
					DBG_871X("oper_ch_offet =%d\n", rtw_get_oper_choffset(padapter));

					break;
				case 0x05:
					psta = rtw_get_stainfo(pstapriv, cur_network->network.MacAddress);
					if (psta) {
						int i;
						struct recv_reorder_ctrl *preorder_ctrl;

						DBG_871X("SSID =%s\n", cur_network->network.Ssid.Ssid);
						DBG_871X("sta's macaddr:" MAC_FMT "\n", MAC_ARG(psta->hwaddr));
						DBG_871X("cur_channel =%d, cur_bwmode =%d, cur_ch_offset =%d\n", pmlmeext->cur_channel, pmlmeext->cur_bwmode, pmlmeext->cur_ch_offset);
						DBG_871X("rtsen =%d, cts2slef =%d\n", psta->rtsen, psta->cts2self);
						DBG_871X("state = 0x%x, aid =%d, macid =%d, raid =%d\n", psta->state, psta->aid, psta->mac_id, psta->raid);
						DBG_871X("qos_en =%d, ht_en =%d, init_rate =%d\n", psta->qos_option, psta->htpriv.ht_option, psta->init_rate);
						DBG_871X("bwmode =%d, ch_offset =%d, sgi_20m =%d, sgi_40m =%d\n", psta->bw_mode, psta->htpriv.ch_offset, psta->htpriv.sgi_20m, psta->htpriv.sgi_40m);
						DBG_871X("ampdu_enable = %d\n", psta->htpriv.ampdu_enable);
						DBG_871X("agg_enable_bitmap =%x, candidate_tid_bitmap =%x\n", psta->htpriv.agg_enable_bitmap, psta->htpriv.candidate_tid_bitmap);

						for (i = 0;i<16;i++) {
							preorder_ctrl = &psta->recvreorder_ctrl[i];
							if (preorder_ctrl->enable)
								DBG_871X("tid =%d, indicate_seq =%d\n", i, preorder_ctrl->indicate_seq);
						}

					} else {
						DBG_871X("can't get sta's macaddr, cur_network's macaddr:" MAC_FMT "\n", MAC_ARG(cur_network->network.MacAddress));
					}
					break;
				case 0x06:
					{
						u32 ODMFlag;
						rtw_hal_get_hwreg(padapter, HW_VAR_DM_FLAG, (u8 *)(&ODMFlag));
						DBG_871X("(B)DMFlag = 0x%x, arg = 0x%x\n", ODMFlag, arg);
						ODMFlag = (u32)(0x0f&arg);
						DBG_871X("(A)DMFlag = 0x%x\n", ODMFlag);
						rtw_hal_set_hwreg(padapter, HW_VAR_DM_FLAG, (u8 *)(&ODMFlag));
					}
					break;
				case 0x07:
					DBG_871X("bSurpriseRemoved =%d, bDriverStopped =%d\n",
						padapter->bSurpriseRemoved, padapter->bDriverStopped);
					break;
				case 0x08:
					{
						DBG_871X("minor_cmd 0x%x\n", minor_cmd);
					}
					break;
				case 0x09:
					{
						int i, j;
						struct list_head	*plist, *phead;
						struct recv_reorder_ctrl *preorder_ctrl;

						DBG_871X("sta_dz_bitmap = 0x%x, tim_bitmap = 0x%x\n", pstapriv->sta_dz_bitmap, pstapriv->tim_bitmap);

						spin_lock_bh(&pstapriv->sta_hash_lock);

						for (i = 0; i< NUM_STA; i++) {
							phead = &(pstapriv->sta_hash[i]);
							plist = get_next(phead);

							while (phead != plist) {
								psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);

								plist = get_next(plist);

								if (extra_arg == psta->aid) {
									DBG_871X("sta's macaddr:" MAC_FMT "\n", MAC_ARG(psta->hwaddr));
									DBG_871X("rtsen =%d, cts2slef =%d\n", psta->rtsen, psta->cts2self);
									DBG_871X("state = 0x%x, aid =%d, macid =%d, raid =%d\n", psta->state, psta->aid, psta->mac_id, psta->raid);
									DBG_871X("qos_en =%d, ht_en =%d, init_rate =%d\n", psta->qos_option, psta->htpriv.ht_option, psta->init_rate);
									DBG_871X("bwmode =%d, ch_offset =%d, sgi_20m =%d, sgi_40m =%d\n", psta->bw_mode, psta->htpriv.ch_offset, psta->htpriv.sgi_20m, psta->htpriv.sgi_40m);
									DBG_871X("ampdu_enable = %d\n", psta->htpriv.ampdu_enable);
									DBG_871X("agg_enable_bitmap =%x, candidate_tid_bitmap =%x\n", psta->htpriv.agg_enable_bitmap, psta->htpriv.candidate_tid_bitmap);
									DBG_871X("capability = 0x%x\n", psta->capability);
									DBG_871X("flags = 0x%x\n", psta->flags);
									DBG_871X("wpa_psk = 0x%x\n", psta->wpa_psk);
									DBG_871X("wpa2_group_cipher = 0x%x\n", psta->wpa2_group_cipher);
									DBG_871X("wpa2_pairwise_cipher = 0x%x\n", psta->wpa2_pairwise_cipher);
									DBG_871X("qos_info = 0x%x\n", psta->qos_info);
									DBG_871X("dot118021XPrivacy = 0x%x\n", psta->dot118021XPrivacy);



									for (j = 0;j<16;j++) {
										preorder_ctrl = &psta->recvreorder_ctrl[j];
										if (preorder_ctrl->enable)
											DBG_871X("tid =%d, indicate_seq =%d\n", j, preorder_ctrl->indicate_seq);
									}
								}
							}
						}

						spin_unlock_bh(&pstapriv->sta_hash_lock);

					}
					break;
				case 0x0a:
					{
						int max_mac_id = 0;
						max_mac_id = rtw_search_max_mac_id(padapter);
						printk("%s ==> max_mac_id = %d\n", __func__, max_mac_id);
					}
					break;
				case 0x0b: /* Enable = 1, Disable = 0 driver control vrtl_carrier_sense. */
					if (arg == 0) {
						DBG_871X("disable driver ctrl vcs\n");
						padapter->driver_vcs_en = 0;
					} else if (arg == 1) {
						DBG_871X("enable driver ctrl vcs = %d\n", extra_arg);
						padapter->driver_vcs_en = 1;

						if (extra_arg>2)
							padapter->driver_vcs_type = 1;
						else
							padapter->driver_vcs_type = extra_arg;
					}
					break;
				case 0x0c:/* dump rx/tx packet */
					{
						if (arg == 0) {
							DBG_871X("dump rx packet (%d)\n", extra_arg);
							/* pHalData->bDumpRxPkt =extra_arg; */
							rtw_hal_set_def_var(padapter, HAL_DEF_DBG_DUMP_RXPKT, &(extra_arg));
						} else if (arg == 1) {
							DBG_871X("dump tx packet (%d)\n", extra_arg);
							rtw_hal_set_def_var(padapter, HAL_DEF_DBG_DUMP_TXPKT, &(extra_arg));
						}
					}
					break;
				case 0x0e:
					{
						if (arg == 0) {
							DBG_871X("disable driver ctrl rx_ampdu_factor\n");
							padapter->driver_rx_ampdu_factor = 0xFF;
						} else if (arg == 1) {

							DBG_871X("enable driver ctrl rx_ampdu_factor = %d\n", extra_arg);

							if ((extra_arg & 0x03) > 0x03)
								padapter->driver_rx_ampdu_factor = 0xFF;
							else
								padapter->driver_rx_ampdu_factor = extra_arg;
						}
					}
					break;

				case 0x10:/*  driver version display */
					dump_drv_version(RTW_DBGDUMP);
					break;
				case 0x11:/* dump linked status */
					{
						 linked_info_dump(padapter, extra_arg);
					}
					break;
				case 0x12: /* set rx_stbc */
				{
					struct registry_priv *pregpriv = &padapter->registrypriv;
					/*  0: disable, bit(0):enable 2.4g, bit(1):enable 5g, 0x3: enable both 2.4g and 5g */
					/* default is set to enable 2.4GHZ for IOT issue with bufflao's AP at 5GHZ */
					if (pregpriv && (extra_arg == 0 || extra_arg == 1|| extra_arg == 2 || extra_arg == 3)) {
						pregpriv->rx_stbc = extra_arg;
						DBG_871X("set rx_stbc =%d\n", pregpriv->rx_stbc);
					} else
						DBG_871X("get rx_stbc =%d\n", pregpriv->rx_stbc);

				}
				break;
				case 0x13: /* set ampdu_enable */
				{
					struct registry_priv *pregpriv = &padapter->registrypriv;
					/*  0: disable, 0x1:enable (but wifi_spec should be 0), 0x2: force enable (don't care wifi_spec) */
					if (pregpriv && extra_arg < 3) {
						pregpriv->ampdu_enable = extra_arg;
						DBG_871X("set ampdu_enable =%d\n", pregpriv->ampdu_enable);
					} else
						DBG_871X("get ampdu_enable =%d\n", pregpriv->ampdu_enable);

				}
				break;
				case 0x14:
				{
					DBG_871X("minor_cmd 0x%x\n", minor_cmd);
				}
				break;
				case 0x16:
				{
					if (arg == 0xff) {
						rtw_odm_dbg_comp_msg(RTW_DBGDUMP, padapter);
					} else {
						u64 dbg_comp = (u64)extra_arg;
						rtw_odm_dbg_comp_set(padapter, dbg_comp);
					}
				}
					break;
#ifdef DBG_FIXED_CHAN
				case 0x17:
					{
						struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
						printk("===>  Fixed channel to %d\n", extra_arg);
						pmlmeext->fixed_chan = extra_arg;

					}
					break;
#endif
				case 0x18:
					{
						printk("===>  Switch USB Mode %d\n", extra_arg);
						rtw_hal_set_hwreg(padapter, HW_VAR_USB_MODE, (u8 *)&extra_arg);
					}
					break;
				case 0x19:
					{
						struct registry_priv *pregistrypriv = &padapter->registrypriv;
						/*  extra_arg : */
						/*  BIT0: Enable VHT LDPC Rx, BIT1: Enable VHT LDPC Tx, */
						/*  BIT4: Enable HT LDPC Rx, BIT5: Enable HT LDPC Tx */
						if (arg == 0) {
							DBG_871X("driver disable LDPC\n");
							pregistrypriv->ldpc_cap = 0x00;
						} else if (arg == 1) {
							DBG_871X("driver set LDPC cap = 0x%x\n", extra_arg);
							pregistrypriv->ldpc_cap = (u8)(extra_arg&0x33);
						}
					}
                                        break;
				case 0x1a:
					{
						struct registry_priv *pregistrypriv = &padapter->registrypriv;
						/*  extra_arg : */
						/*  BIT0: Enable VHT STBC Rx, BIT1: Enable VHT STBC Tx, */
						/*  BIT4: Enable HT STBC Rx, BIT5: Enable HT STBC Tx */
						if (arg == 0) {
							DBG_871X("driver disable STBC\n");
							pregistrypriv->stbc_cap = 0x00;
						} else if (arg == 1) {
							DBG_871X("driver set STBC cap = 0x%x\n", extra_arg);
							pregistrypriv->stbc_cap = (u8)(extra_arg&0x33);
						}
					}
                                        break;
				case 0x1b:
					{
						struct registry_priv *pregistrypriv = &padapter->registrypriv;

						if (arg == 0) {
							DBG_871X("disable driver ctrl max_rx_rate, reset to default_rate_set\n");
							init_mlme_default_rate_set(padapter);
							pregistrypriv->ht_enable = (u8)rtw_ht_enable;
						} else if (arg == 1) {

							int i;
							u8 max_rx_rate;

							DBG_871X("enable driver ctrl max_rx_rate = 0x%x\n", extra_arg);

							max_rx_rate = (u8)extra_arg;

							if (max_rx_rate < 0xc) { /*  max_rx_rate < MSC0 -> B or G -> disable HT */
								pregistrypriv->ht_enable = 0;
								for (i = 0; i<NumRates; i++) {
									if (pmlmeext->datarate[i] > max_rx_rate)
										pmlmeext->datarate[i] = 0xff;
								}

							}
							else if (max_rx_rate < 0x1c) { /*  mcs0~mcs15 */
								u32 mcs_bitmap = 0x0;

								for (i = 0; i<((max_rx_rate+1)-0xc); i++)
									mcs_bitmap |= BIT(i);

								set_mcs_rate_by_mask(pmlmeext->default_supported_mcs_set, mcs_bitmap);
							}
						}
					}
                                        break;
				case 0x1c: /* enable/disable driver control AMPDU Density for peer sta's rx */
					{
						if (arg == 0) {
							DBG_871X("disable driver ctrl ampdu density\n");
							padapter->driver_ampdu_spacing = 0xFF;
						} else if (arg == 1) {

							DBG_871X("enable driver ctrl ampdu density = %d\n", extra_arg);

							if ((extra_arg & 0x07) > 0x07)
								padapter->driver_ampdu_spacing = 0xFF;
							else
								padapter->driver_ampdu_spacing = extra_arg;
						}
					}
					break;
#ifdef CONFIG_BACKGROUND_NOISE_MONITOR
				case 0x1e:
					{
						struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
						PDM_ODM_T pDM_Odm = &pHalData->odmpriv;
						u8 chan = rtw_get_oper_ch(padapter);
						DBG_871X("===========================================\n");
						ODM_InbandNoise_Monitor(pDM_Odm, true, 0x1e, 100);
						DBG_871X("channel(%d), noise_a = %d, noise_b = %d , noise_all:%d\n",
							chan, pDM_Odm->noise_level.noise[ODM_RF_PATH_A],
							pDM_Odm->noise_level.noise[ODM_RF_PATH_B],
							pDM_Odm->noise_level.noise_all);
						DBG_871X("===========================================\n");

					}
					break;
#endif
				case 0x23:
					{
						DBG_871X("turn %s the bNotifyChannelChange Variable\n", (extra_arg == 1)?"on":"off");
						padapter->bNotifyChannelChange = extra_arg;
						break;
					}
				case 0x24:
					{
						break;
					}
#ifdef CONFIG_GPIO_API
		            case 0x25: /* Get GPIO register */
		                    {
			                    /*
			                    * dbg 0x7f250000 [gpio_num], Get gpio value, gpio_num:0~7
			                    */

			                    int value;
			                    DBG_871X("Read GPIO Value  extra_arg = %d\n", extra_arg);
			                    value = rtw_get_gpio(dev, extra_arg);
			                    DBG_871X("Read GPIO Value = %d\n", value);
			                    break;
		                    }
		            case 0x26: /* Set GPIO direction */
		                    {

			                    /* dbg 0x7f26000x [y], Set gpio direction,
			                    * x: gpio_num, 4~7  y: indicate direction, 0~1
			                    */

			                    int value;
			                    DBG_871X("Set GPIO Direction! arg = %d , extra_arg =%d\n", arg , extra_arg);
			                    value = rtw_config_gpio(dev, arg, extra_arg);
			                    DBG_871X("Set GPIO Direction %s\n", (value ==-1)?"Fail!!!":"Success");
			                    break;
					}
				case 0x27: /* Set GPIO output direction value */
					{
						/*
						* dbg 0x7f27000x [y], Set gpio output direction value,
						* x: gpio_num, 4~7  y: indicate direction, 0~1
						*/

						int value;
						DBG_871X("Set GPIO Value! arg = %d , extra_arg =%d\n", arg , extra_arg);
						value = rtw_set_gpio_output_value(dev, arg, extra_arg);
						DBG_871X("Set GPIO Value %s\n", (value ==-1)?"Fail!!!":"Success");
						break;
					}
#endif
				case 0xaa:
					{
						if ((extra_arg & 0x7F)> 0x3F) extra_arg = 0xFF;
						DBG_871X("chang data rate to :0x%02x\n", extra_arg);
						padapter->fix_rate = extra_arg;
					}
					break;
				case 0xdd:/* registers dump , 0 for mac reg, 1 for bb reg, 2 for rf reg */
					{
						if (extra_arg == 0)
							mac_reg_dump(RTW_DBGDUMP, padapter);
						else if (extra_arg == 1)
							bb_reg_dump(RTW_DBGDUMP, padapter);
						else if (extra_arg ==2)
							rf_reg_dump(RTW_DBGDUMP, padapter);
					}
					break;

				case 0xee:/* turn on/off dynamic funcs */
					{
						u32 odm_flag;

						if (0xf ==extra_arg) {
							rtw_hal_get_def_var(padapter, HAL_DEF_DBG_DM_FUNC,&odm_flag);
							DBG_871X(" === DMFlag(0x%08x) ===\n", odm_flag);
							DBG_871X("extra_arg = 0  - disable all dynamic func\n");
							DBG_871X("extra_arg = 1  - disable DIG- BIT(0)\n");
							DBG_871X("extra_arg = 2  - disable High power - BIT(1)\n");
							DBG_871X("extra_arg = 3  - disable tx power tracking - BIT(2)\n");
							DBG_871X("extra_arg = 4  - disable BT coexistence - BIT(3)\n");
							DBG_871X("extra_arg = 5  - disable antenna diversity - BIT(4)\n");
							DBG_871X("extra_arg = 6  - enable all dynamic func\n");
						} else {
							/*extra_arg = 0  - disable all dynamic func
								extra_arg = 1  - disable DIG
								extra_arg = 2  - disable tx power tracking
								extra_arg = 3  - turn on all dynamic func
							*/
							rtw_hal_set_def_var(padapter, HAL_DEF_DBG_DM_FUNC, &(extra_arg));
							rtw_hal_get_def_var(padapter, HAL_DEF_DBG_DM_FUNC,&odm_flag);
							DBG_871X(" === DMFlag(0x%08x) ===\n", odm_flag);
						}
					}
					break;

				case 0xfd:
					rtw_write8(padapter, 0xc50, arg);
					DBG_871X("wr(0xc50) = 0x%x\n", rtw_read8(padapter, 0xc50));
					rtw_write8(padapter, 0xc58, arg);
					DBG_871X("wr(0xc58) = 0x%x\n", rtw_read8(padapter, 0xc58));
					break;
				case 0xfe:
					DBG_871X("rd(0xc50) = 0x%x\n", rtw_read8(padapter, 0xc50));
					DBG_871X("rd(0xc58) = 0x%x\n", rtw_read8(padapter, 0xc58));
					break;
				case 0xff:
					{
						DBG_871X("dbg(0x210) = 0x%x\n", rtw_read32(padapter, 0x210));
						DBG_871X("dbg(0x608) = 0x%x\n", rtw_read32(padapter, 0x608));
						DBG_871X("dbg(0x280) = 0x%x\n", rtw_read32(padapter, 0x280));
						DBG_871X("dbg(0x284) = 0x%x\n", rtw_read32(padapter, 0x284));
						DBG_871X("dbg(0x288) = 0x%x\n", rtw_read32(padapter, 0x288));

						DBG_871X("dbg(0x664) = 0x%x\n", rtw_read32(padapter, 0x664));


						DBG_871X("\n");

						DBG_871X("dbg(0x430) = 0x%x\n", rtw_read32(padapter, 0x430));
						DBG_871X("dbg(0x438) = 0x%x\n", rtw_read32(padapter, 0x438));

						DBG_871X("dbg(0x440) = 0x%x\n", rtw_read32(padapter, 0x440));

						DBG_871X("dbg(0x458) = 0x%x\n", rtw_read32(padapter, 0x458));

						DBG_871X("dbg(0x484) = 0x%x\n", rtw_read32(padapter, 0x484));
						DBG_871X("dbg(0x488) = 0x%x\n", rtw_read32(padapter, 0x488));

						DBG_871X("dbg(0x444) = 0x%x\n", rtw_read32(padapter, 0x444));
						DBG_871X("dbg(0x448) = 0x%x\n", rtw_read32(padapter, 0x448));
						DBG_871X("dbg(0x44c) = 0x%x\n", rtw_read32(padapter, 0x44c));
						DBG_871X("dbg(0x450) = 0x%x\n", rtw_read32(padapter, 0x450));
					}
					break;
			}
			break;
		default:
			DBG_871X("error dbg cmd!\n");
			break;
	}


	return ret;

}

static int wpa_set_param(struct net_device *dev, u8 name, u32 value)
{
	uint ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

	switch (name) {
	case IEEE_PARAM_WPA_ENABLED:

		padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_8021X; /* 802.1x */

		/* ret = ieee80211_wpa_enable(ieee, value); */

		switch ((value)&0xff) {
		case 1 : /* WPA */
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeWPAPSK; /* WPA_PSK */
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption2Enabled;
			break;
		case 2: /* WPA2 */
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeWPA2PSK; /* WPA2_PSK */
			padapter->securitypriv.ndisencryptstatus = Ndis802_11Encryption3Enabled;
			break;
		}

		RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_info_, ("wpa_set_param:padapter->securitypriv.ndisauthtype =%d\n", padapter->securitypriv.ndisauthtype));

		break;

	case IEEE_PARAM_TKIP_COUNTERMEASURES:
		/* ieee->tkip_countermeasures =value; */
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

		/* ieee->privacy_invoked =value; */

		break;

	case IEEE_PARAM_AUTH_ALGS:

		ret = wpa_set_auth_algs(dev, value);

		break;

	case IEEE_PARAM_IEEE_802_1X:

		/* ieee->ieee802_1x =value; */

		break;

	case IEEE_PARAM_WPAX_SELECT:

		/*  added for WPA2 mixed mode */
		/* DBG_871X(KERN_WARNING "------------------------>wpax value = %x\n", value); */
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

	/* down(&ieee->wx_sem); */

	if (p->length < sizeof(struct ieee_param) || !p->pointer) {
		ret = -EINVAL;
		goto out;
	}

	param = rtw_malloc(p->length);
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
		/* ret = wpa_set_wpa_ie(dev, param, p->length); */
		ret =  rtw_set_wpa_ie((struct adapter *)rtw_netdev_priv(dev), (char*)param->u.wpa_ie.data, (u16)param->u.wpa_ie.len);
		break;

	case IEEE_CMD_SET_ENCRYPTION:
		ret = wpa_set_encryption(dev, param, p->length);
		break;

	case IEEE_CMD_MLME:
		ret = wpa_mlme(dev, param->u.mlme.command, param->u.mlme.reason_code);
		break;

	default:
		DBG_871X("Unknown WPA supplicant request: %d\n", param->cmd);
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

static int rtw_set_encryption(struct net_device *dev, struct ieee_param *param, u32 param_len)
{
	int ret = 0;
	u32 wep_key_idx, wep_key_len, wep_total_len;
	struct ndis_802_11_wep	 *pwep = NULL;
	struct sta_info *psta = NULL, *pbcmc_sta = NULL;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv* psecuritypriv =&(padapter->securitypriv);
	struct sta_priv *pstapriv = &padapter->stapriv;

	DBG_871X("%s\n", __func__);

	param->u.crypt.err = 0;
	param->u.crypt.alg[IEEE_CRYPT_ALG_NAME_LEN - 1] = '\0';

	/* sizeof(struct ieee_param) = 64 bytes; */
	/* if (param_len !=  (u32) ((u8 *) param->u.crypt.key - (u8 *) param) + param->u.crypt.key_len) */
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
			/* ret = -EINVAL; */
			DBG_871X("rtw_set_encryption(), sta has already been removed or never been added\n");
			goto exit;
		}
	}

	if (strcmp(param->u.crypt.alg, "none") == 0 && (psta == NULL)) {
		/* todo:clear default encryption keys */

		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
		psecuritypriv->ndisencryptstatus = Ndis802_11EncryptionDisabled;
		psecuritypriv->dot11PrivacyAlgrthm = _NO_PRIVACY_;
		psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;

		DBG_871X("clear default encryption keys, keyid =%d\n", param->u.crypt.idx);

		goto exit;
	}


	if (strcmp(param->u.crypt.alg, "WEP") == 0 && (psta == NULL)) {
		DBG_871X("r871x_set_encryption, crypt.alg = WEP\n");

		wep_key_idx = param->u.crypt.idx;
		wep_key_len = param->u.crypt.key_len;

		DBG_871X("r871x_set_encryption, wep_key_idx =%d, len =%d\n", wep_key_idx, wep_key_len);

		if ((wep_key_idx >= WEP_KEYS) || (wep_key_len<= 0)) {
			ret = -EINVAL;
			goto exit;
		}


		if (wep_key_len > 0) {
			wep_key_len = wep_key_len <= 5 ? 5 : 13;
			wep_total_len = wep_key_len + FIELD_OFFSET(struct ndis_802_11_wep, KeyMaterial);
			pwep = rtw_malloc(wep_total_len);
			if (pwep == NULL) {
				DBG_871X(" r871x_set_encryption: pwep allocate fail !!!\n");
				goto exit;
			}

			memset(pwep, 0, wep_total_len);

			pwep->KeyLength = wep_key_len;
			pwep->Length = wep_total_len;

		}

		pwep->KeyIndex = wep_key_idx;

		memcpy(pwep->KeyMaterial,  param->u.crypt.key, pwep->KeyLength);

		if (param->u.crypt.set_tx) {
			DBG_871X("wep, set_tx = 1\n");

			psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;
			psecuritypriv->ndisencryptstatus = Ndis802_11Encryption1Enabled;
			psecuritypriv->dot11PrivacyAlgrthm = _WEP40_;
			psecuritypriv->dot118021XGrpPrivacy = _WEP40_;

			if (pwep->KeyLength == 13) {
				psecuritypriv->dot11PrivacyAlgrthm = _WEP104_;
				psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
			}


			psecuritypriv->dot11PrivacyKeyIndex = wep_key_idx;

			memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), pwep->KeyMaterial, pwep->KeyLength);

			psecuritypriv->dot11DefKeylen[wep_key_idx]=pwep->KeyLength;

			rtw_ap_set_wep_key(padapter, pwep->KeyMaterial, pwep->KeyLength, wep_key_idx, 1);
		} else {
			DBG_871X("wep, set_tx = 0\n");

			/* don't update "psecuritypriv->dot11PrivacyAlgrthm" and */
			/* psecuritypriv->dot11PrivacyKeyIndex =keyid", but can rtw_set_key to cam */

			memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), pwep->KeyMaterial, pwep->KeyLength);

			psecuritypriv->dot11DefKeylen[wep_key_idx] = pwep->KeyLength;

			rtw_ap_set_wep_key(padapter, pwep->KeyMaterial, pwep->KeyLength, wep_key_idx, 0);
		}

		goto exit;

	}


	if (!psta && check_fwstate(pmlmepriv, WIFI_AP_STATE)) { /*  group key */
		if (param->u.crypt.set_tx == 1) {
			if (strcmp(param->u.crypt.alg, "WEP") == 0) {
				DBG_871X("%s, set group_key, WEP\n", __func__);

				memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));

				psecuritypriv->dot118021XGrpPrivacy = _WEP40_;
				if (param->u.crypt.key_len == 13)
						psecuritypriv->dot118021XGrpPrivacy = _WEP104_;

			} else if (strcmp(param->u.crypt.alg, "TKIP") == 0) {
				DBG_871X("%s, set group_key, TKIP\n", __func__);

				psecuritypriv->dot118021XGrpPrivacy = _TKIP_;

				memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));

				/* DEBUG_ERR("set key length :param->u.crypt.key_len =%d\n", param->u.crypt.key_len); */
				/* set mic key */
				memcpy(psecuritypriv->dot118021XGrptxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[16]), 8);
				memcpy(psecuritypriv->dot118021XGrprxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[24]), 8);

				psecuritypriv->busetkipkey = true;

			}
			else if (strcmp(param->u.crypt.alg, "CCMP") == 0) {
				DBG_871X("%s, set group_key, CCMP\n", __func__);

				psecuritypriv->dot118021XGrpPrivacy = _AES_;

				memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
			} else {
				DBG_871X("%s, set group_key, none\n", __func__);

				psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
			}

			psecuritypriv->dot118021XGrpKeyid = param->u.crypt.idx;

			psecuritypriv->binstallGrpkey = true;

			psecuritypriv->dot11PrivacyAlgrthm = psecuritypriv->dot118021XGrpPrivacy;/*  */

			rtw_ap_set_group_key(padapter, param->u.crypt.key, psecuritypriv->dot118021XGrpPrivacy, param->u.crypt.idx);

			pbcmc_sta =rtw_get_bcmc_stainfo(padapter);
			if (pbcmc_sta) {
				pbcmc_sta->ieee8021x_blocked = false;
				pbcmc_sta->dot118021XPrivacy = psecuritypriv->dot118021XGrpPrivacy;/* rx will use bmc_sta's dot118021XPrivacy */
			}
		}

		goto exit;

	}

	if (psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_8021X && psta) { /*  psk/802_1x */
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
			if (param->u.crypt.set_tx == 1)	{
				memcpy(psta->dot118021x_UncstKey.skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));

				if (strcmp(param->u.crypt.alg, "WEP") == 0) {
					DBG_871X("%s, set pairwise key, WEP\n", __func__);

					psta->dot118021XPrivacy = _WEP40_;
					if (param->u.crypt.key_len == 13)
						psta->dot118021XPrivacy = _WEP104_;
				} else if (strcmp(param->u.crypt.alg, "TKIP") == 0) {
					DBG_871X("%s, set pairwise key, TKIP\n", __func__);

					psta->dot118021XPrivacy = _TKIP_;

					/* DEBUG_ERR("set key length :param->u.crypt.key_len =%d\n", param->u.crypt.key_len); */
					/* set mic key */
					memcpy(psta->dot11tkiptxmickey.skey, &(param->u.crypt.key[16]), 8);
					memcpy(psta->dot11tkiprxmickey.skey, &(param->u.crypt.key[24]), 8);

					psecuritypriv->busetkipkey = true;

				} else if (strcmp(param->u.crypt.alg, "CCMP") == 0) {

					DBG_871X("%s, set pairwise key, CCMP\n", __func__);

					psta->dot118021XPrivacy = _AES_;
				} else {
					DBG_871X("%s, set pairwise key, none\n", __func__);

					psta->dot118021XPrivacy = _NO_PRIVACY_;
				}

				rtw_ap_set_pairwise_key(padapter, psta);

				psta->ieee8021x_blocked = false;

			} else { /* group key??? */
				if (strcmp(param->u.crypt.alg, "WEP") == 0) {
					memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));

					psecuritypriv->dot118021XGrpPrivacy = _WEP40_;
					if (param->u.crypt.key_len == 13)
						psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
				} else if (strcmp(param->u.crypt.alg, "TKIP") == 0) {
					psecuritypriv->dot118021XGrpPrivacy = _TKIP_;

					memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));

					/* DEBUG_ERR("set key length :param->u.crypt.key_len =%d\n", param->u.crypt.key_len); */
					/* set mic key */
					memcpy(psecuritypriv->dot118021XGrptxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[16]), 8);
					memcpy(psecuritypriv->dot118021XGrprxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[24]), 8);

					psecuritypriv->busetkipkey = true;

				} else if (strcmp(param->u.crypt.alg, "CCMP") == 0) {
					psecuritypriv->dot118021XGrpPrivacy = _AES_;

					memcpy(psecuritypriv->dot118021XGrpKey[param->u.crypt.idx].skey,  param->u.crypt.key, (param->u.crypt.key_len>16 ?16:param->u.crypt.key_len));
				} else {
					psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
				}

				psecuritypriv->dot118021XGrpKeyid = param->u.crypt.idx;

				psecuritypriv->binstallGrpkey = true;

				psecuritypriv->dot11PrivacyAlgrthm = psecuritypriv->dot118021XGrpPrivacy;/*  */

				rtw_ap_set_group_key(padapter, param->u.crypt.key, psecuritypriv->dot118021XGrpPrivacy, param->u.crypt.idx);

				pbcmc_sta =rtw_get_bcmc_stainfo(padapter);
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


	DBG_871X("%s, len =%d\n", __func__, len);

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
	/* _irqL irqL; */
	/* struct list_head	*phead, *plist; */
	/* struct sta_info *psta = NULL; */
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	/* struct sta_priv *pstapriv = &padapter->stapriv; */

	DBG_871X("%s\n", __func__);

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

	DBG_871X("rtw_add_sta(aid =%d) =" MAC_FMT "\n", param->u.add_sta.aid, MAC_ARG(param->sta_addr));

	if (check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)) != true)
		return -EINVAL;

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff) {
		return -EINVAL;
	}

/*
	psta = rtw_get_stainfo(pstapriv, param->sta_addr);
	if (psta)
	{
		DBG_871X("rtw_add_sta(), free has been added psta =%p\n", psta);
		spin_lock_bh(&(pstapriv->sta_hash_lock));
		rtw_free_stainfo(padapter,  psta);
		spin_unlock_bh(&(pstapriv->sta_hash_lock));

		psta = NULL;
	}
*/
	/* psta = rtw_alloc_stainfo(pstapriv, param->sta_addr); */
	psta = rtw_get_stainfo(pstapriv, param->sta_addr);
	if (psta) {
		int flags = param->u.add_sta.flags;

		/* DBG_871X("rtw_add_sta(), init sta's variables, psta =%p\n", psta); */

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
			memcpy((void*)&psta->htpriv.ht_cap, (void*)&param->u.add_sta.ht_cap, sizeof(struct rtw_ieee80211_ht_cap));
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

	DBG_871X("rtw_del_sta =" MAC_FMT "\n", MAC_ARG(param->sta_addr));

	if (check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)) != true)
		return -EINVAL;

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff) {
		return -EINVAL;
	}

	psta = rtw_get_stainfo(pstapriv, param->sta_addr);
	if (psta) {
		u8 updated =false;

		/* DBG_871X("free psta =%p, aid =%d\n", psta, psta->aid); */

		spin_lock_bh(&pstapriv->asoc_list_lock);
		if (list_empty(&psta->asoc_list) ==false) {
			list_del_init(&psta->asoc_list);
			pstapriv->asoc_list_cnt--;
			updated = ap_free_sta(padapter, psta, true, WLAN_REASON_DEAUTH_LEAVING);

		}
		spin_unlock_bh(&pstapriv->asoc_list_lock);

		associated_clients_update(padapter, updated);

		psta = NULL;

	} else {
		DBG_871X("rtw_del_sta(), sta has already been removed or never been added\n");

		/* ret = -1; */
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

	DBG_871X("rtw_ioctl_get_sta_info, sta_addr: " MAC_FMT "\n", MAC_ARG(param_ex->sta_addr));

	if (check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)) != true)
		return -EINVAL;

	if (param_ex->sta_addr[0] == 0xff && param_ex->sta_addr[1] == 0xff &&
	    param_ex->sta_addr[2] == 0xff && param_ex->sta_addr[3] == 0xff &&
	    param_ex->sta_addr[4] == 0xff && param_ex->sta_addr[5] == 0xff) {
		return -EINVAL;
	}

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

		psta_data->sta_set =((psta->nonerp_set) |
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

	DBG_871X("rtw_get_sta_wpaie, sta_addr: " MAC_FMT "\n", MAC_ARG(param->sta_addr));

	if (check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)) != true)
		return -EINVAL;

	if (param->sta_addr[0] == 0xff && param->sta_addr[1] == 0xff &&
	    param->sta_addr[2] == 0xff && param->sta_addr[3] == 0xff &&
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff) {
		return -EINVAL;
	}

	psta = rtw_get_stainfo(pstapriv, param->sta_addr);
	if (psta) {
		if ((psta->wpa_ie[0] == WLAN_EID_RSN) || (psta->wpa_ie[0] == WLAN_EID_GENERIC)) {
			int wpa_ie_len;
			int copy_len;

			wpa_ie_len = psta->wpa_ie[1];

			copy_len = ((wpa_ie_len+2) > sizeof(psta->wpa_ie)) ? (sizeof(psta->wpa_ie)):(wpa_ie_len+2);

			param->u.wpa_ie.len = copy_len;

			memcpy(param->u.wpa_ie.reserved, psta->wpa_ie, copy_len);
		} else {
			/* ret = -1; */
			DBG_871X("sta's wpa_ie is NONE\n");
		}
	} else {
		ret = -1;
	}

	return ret;

}

static int rtw_set_wps_beacon(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret = 0;
	unsigned char wps_oui[4]={0x0, 0x50, 0xf2, 0x04};
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	int ie_len;

	DBG_871X("%s, len =%d\n", __func__, len);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	ie_len = len-12-2;/*  12 = param header, 2:no packed */


	kfree(pmlmepriv->wps_beacon_ie);
	pmlmepriv->wps_beacon_ie = NULL;

	if (ie_len>0) {
		pmlmepriv->wps_beacon_ie = rtw_malloc(ie_len);
		pmlmepriv->wps_beacon_ie_len = ie_len;
		if (pmlmepriv->wps_beacon_ie == NULL) {
			DBG_871X("%s()-%d: rtw_malloc() ERROR!\n", __func__, __LINE__);
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

	DBG_871X("%s, len =%d\n", __func__, len);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	ie_len = len-12-2;/*  12 = param header, 2:no packed */


	kfree(pmlmepriv->wps_probe_resp_ie);
	pmlmepriv->wps_probe_resp_ie = NULL;

	if (ie_len>0) {
		pmlmepriv->wps_probe_resp_ie = rtw_malloc(ie_len);
		pmlmepriv->wps_probe_resp_ie_len = ie_len;
		if (pmlmepriv->wps_probe_resp_ie == NULL) {
			DBG_871X("%s()-%d: rtw_malloc() ERROR!\n", __func__, __LINE__);
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

	DBG_871X("%s, len =%d\n", __func__, len);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	ie_len = len-12-2;/*  12 = param header, 2:no packed */


	kfree(pmlmepriv->wps_assoc_resp_ie);
	pmlmepriv->wps_assoc_resp_ie = NULL;

	if (ie_len>0) {
		pmlmepriv->wps_assoc_resp_ie = rtw_malloc(ie_len);
		pmlmepriv->wps_assoc_resp_ie_len = ie_len;
		if (pmlmepriv->wps_assoc_resp_ie == NULL) {
			DBG_871X("%s()-%d: rtw_malloc() ERROR!\n", __func__, __LINE__);
			return -EINVAL;
		}

		memcpy(pmlmepriv->wps_assoc_resp_ie, param->u.bcn_ie.buf, ie_len);
	}


	return ret;

}

static int rtw_set_hidden_ssid(struct net_device *dev, struct ieee_param *param, int len)
{
	int ret = 0;
	struct adapter *adapter = (struct adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *mlmepriv = &(adapter->mlmepriv);
	struct mlme_ext_priv *mlmeext = &(adapter->mlmeextpriv);
	struct mlme_ext_info *mlmeinfo = &(mlmeext->mlmext_info);
	int ie_len;
	u8 *ssid_ie;
	char ssid[NDIS_802_11_LENGTH_SSID + 1];
	sint ssid_len;
	u8 ignore_broadcast_ssid;

	if (check_fwstate(mlmepriv, WIFI_AP_STATE) != true)
		return -EPERM;

	if (param->u.bcn_ie.reserved[0] != 0xea)
		return -EINVAL;

	mlmeinfo->hidden_ssid_mode = ignore_broadcast_ssid = param->u.bcn_ie.reserved[1];

	ie_len = len-12-2;/*  12 = param header, 2:no packed */
	ssid_ie = rtw_get_ie(param->u.bcn_ie.buf,  WLAN_EID_SSID, &ssid_len, ie_len);

	if (ssid_ie && ssid_len > 0 && ssid_len <= NDIS_802_11_LENGTH_SSID) {
		struct wlan_bssid_ex *pbss_network = &mlmepriv->cur_network.network;
		struct wlan_bssid_ex *pbss_network_ext = &mlmeinfo->network;

		memcpy(ssid, ssid_ie+2, ssid_len);
		ssid[ssid_len] = 0x0;

		if (0)
			DBG_871X(FUNC_ADPT_FMT" ssid:(%s,%d), from ie:(%s,%d), (%s,%d)\n", FUNC_ADPT_ARG(adapter),
				 ssid, ssid_len,
				 pbss_network->Ssid.Ssid, pbss_network->Ssid.SsidLength,
				 pbss_network_ext->Ssid.Ssid, pbss_network_ext->Ssid.SsidLength);

		memcpy(pbss_network->Ssid.Ssid, (void *)ssid, ssid_len);
		pbss_network->Ssid.SsidLength = ssid_len;
		memcpy(pbss_network_ext->Ssid.Ssid, (void *)ssid, ssid_len);
		pbss_network_ext->Ssid.SsidLength = ssid_len;

		if (0)
			DBG_871X(FUNC_ADPT_FMT" after ssid:(%s,%d), (%s,%d)\n", FUNC_ADPT_ARG(adapter),
				 pbss_network->Ssid.Ssid, pbss_network->Ssid.SsidLength,
				 pbss_network_ext->Ssid.Ssid, pbss_network_ext->Ssid.SsidLength);
	}

	DBG_871X(FUNC_ADPT_FMT" ignore_broadcast_ssid:%d, %s,%d\n", FUNC_ADPT_ARG(adapter),
		ignore_broadcast_ssid, ssid, ssid_len);

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
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff) {
		return -EINVAL;
	}

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
	    param->sta_addr[4] == 0xff && param->sta_addr[5] == 0xff) {
		return -EINVAL;
	}

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

	/* DBG_871X("%s\n", __func__); */

	/*
	* this function is expect to call in master mode, which allows no power saving
	* so, we just check hw_init_completed
	*/

	if (!padapter->hw_init_completed) {
		ret = -EPERM;
		goto out;
	}


	/* if (p->length < sizeof(struct ieee_param) || !p->pointer) { */
	if (!p->pointer) {
		ret = -EINVAL;
		goto out;
	}

	param = rtw_malloc(p->length);
	if (param == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	if (copy_from_user(param, p->pointer, p->length)) {
		kfree(param);
		ret = -EFAULT;
		goto out;
	}

	/* DBG_871X("%s, cmd =%d\n", __func__, param->cmd); */

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
			DBG_871X("Unknown hostapd request: %d\n", param->cmd);
			ret = -EOPNOTSUPP;
			break;

	}

	if (ret == 0 && copy_to_user(p->pointer, param, p->length))
		ret = -EFAULT;


	kfree(param);

out:

	return ret;

}

static int rtw_wx_set_priv(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *awrq,
				char *extra)
{

#ifdef DEBUG_RTW_WX_SET_PRIV
	char *ext_dbg;
#endif

	int ret = 0;
	int len = 0;
	char *ext;

	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct iw_point *dwrq = (struct iw_point*)awrq;

	/* RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_notice_, ("+rtw_wx_set_priv\n")); */
	if (dwrq->length == 0)
		return -EFAULT;

	len = dwrq->length;
	if (!(ext = vmalloc(len)))
		return -ENOMEM;

	if (copy_from_user(ext, dwrq->pointer, len)) {
		vfree(ext);
		return -EFAULT;
	}


	/* RT_TRACE(_module_rtl871x_ioctl_os_c, _drv_notice_, */
	/* 	 ("rtw_wx_set_priv: %s req =%s\n", */
	/* 	  dev->name, ext)); */

	#ifdef DEBUG_RTW_WX_SET_PRIV
	if (!(ext_dbg = vmalloc(len))) {
		vfree(ext, len);
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
			(!memcmp(&probereq_wpsie[2], wps_oui, 4))) {
			cp_sz = probereq_wpsie_len>MAX_WPS_IE_LEN ? MAX_WPS_IE_LEN:probereq_wpsie_len;

			if (pmlmepriv->wps_probe_req_ie) {
				pmlmepriv->wps_probe_req_ie_len = 0;
				kfree(pmlmepriv->wps_probe_req_ie);
				pmlmepriv->wps_probe_req_ie = NULL;
			}

			pmlmepriv->wps_probe_req_ie = rtw_malloc(cp_sz);
			if (pmlmepriv->wps_probe_req_ie == NULL) {
				printk("%s()-%d: rtw_malloc() ERROR!\n", __func__, __LINE__);
				ret =  -EINVAL;
				goto FREE_EXT;

			}

			memcpy(pmlmepriv->wps_probe_req_ie, probereq_wpsie, cp_sz);
			pmlmepriv->wps_probe_req_ie_len = cp_sz;

		}

		goto FREE_EXT;

	}

	if (len >= WEXT_CSCAN_HEADER_SIZE
		&& !memcmp(ext, WEXT_CSCAN_HEADER, WEXT_CSCAN_HEADER_SIZE)) {
		ret = rtw_wx_set_scan(dev, info, awrq, ext);
		goto FREE_EXT;
	}

FREE_EXT:

	vfree(ext);
	#ifdef DEBUG_RTW_WX_SET_PRIV
	vfree(ext_dbg);
	#endif

	/* DBG_871X("rtw_wx_set_priv: (SIOCSIWPRIV) %s ret =%d\n", */
	/* 		dev->name, ret); */

	return ret;

}

static int rtw_pm_set(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	unsigned	mode = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

	DBG_871X("[%s] extra = %s\n", __func__, extra);

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

static int rtw_mp_efuse_get(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wdata, char *extra)
{
	int err = 0;
	return err;
}

static int rtw_mp_efuse_set(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wdata, char *extra)
{
	int err = 0;
	return err;
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
	int ret = 0;
	return ret;
}





#ifdef CONFIG_INTEL_WIDI
static int rtw_widi_set(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);

	process_intel_widi_cmd(padapter, extra);

	return ret;
}

static int rtw_widi_set_probe_request(struct net_device *dev,
                               struct iw_request_info *info,
                               union iwreq_data *wrqu, char *extra)
{
	int	ret = 0;
	u8 *pbuf = NULL;
	struct adapter	*padapter = (struct adapter *)rtw_netdev_priv(dev);

	pbuf = rtw_malloc(sizeof(l2_msg_t));
	if (pbuf) {
		if (copy_from_user(pbuf, wrqu->data.pointer, wrqu->data.length))
			ret = -EFAULT;
		/* memcpy(pbuf, wrqu->data.pointer, wrqu->data.length); */

		if (wrqu->data.flags == 0)
			intel_widi_wk_cmd(padapter, INTEL_WIDI_ISSUE_PROB_WK, pbuf, sizeof(l2_msg_t));
		else if (wrqu->data.flags == 1)
			rtw_set_wfd_rds_sink_info(padapter, (l2_msg_t *)pbuf);
	}
	return ret;
}
#endif /*  CONFIG_INTEL_WIDI */

static int rtw_test(
	struct net_device *dev,
	struct iw_request_info *info,
	union iwreq_data *wrqu, char *extra)
{
	u32 len;
	u8 *pbuf, *pch;
	char *ptmp;
	u8 *delim = ",";
	struct adapter *padapter = rtw_netdev_priv(dev);


	DBG_871X("+%s\n", __func__);
	len = wrqu->data.length;

	pbuf = rtw_zmalloc(len);
	if (pbuf == NULL) {
		DBG_871X("%s: no memory!\n", __func__);
		return -ENOMEM;
	}

	if (copy_from_user(pbuf, wrqu->data.pointer, len)) {
		kfree(pbuf);
		DBG_871X("%s: copy from user fail!\n", __func__);
		return -EFAULT;
	}
	DBG_871X("%s: string =\"%s\"\n", __func__, pbuf);

	ptmp = (char*)pbuf;
	pch = strsep(&ptmp, delim);
	if ((pch == NULL) || (strlen(pch) == 0)) {
		kfree(pbuf);
		DBG_871X("%s: parameter error(level 1)!\n", __func__);
		return -EFAULT;
	}

	if (strcmp(pch, "bton") == 0)
		rtw_btcoex_SetManualControl(padapter, false);

	if (strcmp(pch, "btoff") == 0)
		rtw_btcoex_SetManualControl(padapter, true);

	if (strcmp(pch, "h2c") == 0) {
		u8 param[8];
		u8 count = 0;
		u32 tmp;
		u8 i;
		u32 pos;
		s32 ret;


		do {
			pch = strsep(&ptmp, delim);
			if ((pch == NULL) || (strlen(pch) == 0))
				break;

			sscanf(pch, "%x", &tmp);
			param[count++] = (u8)tmp;
		} while (count < 8);

		if (count == 0) {
			kfree(pbuf);
			DBG_871X("%s: parameter error(level 2)!\n", __func__);
			return -EFAULT;
		}

		ret = rtw_hal_fill_h2c_cmd(padapter, param[0], count-1, &param[1]);

		pos = sprintf(extra, "H2C ID = 0x%02x content =", param[0]);
		for (i = 1; i<count; i++)
			pos += sprintf(extra+pos, "%02x,", param[i]);
		extra[pos] = 0;
		pos--;
		pos += sprintf(extra+pos, " %s", ret == _FAIL?"FAIL":"OK");

		wrqu->data.length = strlen(extra) + 1;
	}

	kfree(pbuf);
	return 0;
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
/* for PLATFORM_MT53XX */
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

/* for RTK_DMP_PLATFORM */
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
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | IFNAMSIZ, "rfr"
	},
	{
		SIOCIWFIRSTPRIV + 0x10,
		IW_PRIV_TYPE_CHAR | 1024, 0, "p2p_set"
	},
	{
		SIOCIWFIRSTPRIV + 0x11,
		IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK , "p2p_get"
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
		IW_PRIV_TYPE_CHAR | 1024, IW_PRIV_TYPE_CHAR | 1024 , "tdls_get"
	},
	{
		SIOCIWFIRSTPRIV + 0x16,
		IW_PRIV_TYPE_CHAR | 64, 0, "pm_set"
	},

	{SIOCIWFIRSTPRIV + 0x18, IW_PRIV_TYPE_CHAR | IFNAMSIZ , 0 , "rereg_nd_name"},
	{SIOCIWFIRSTPRIV + 0x1A, IW_PRIV_TYPE_CHAR | 1024, 0, "efuse_set"},
	{SIOCIWFIRSTPRIV + 0x1B, IW_PRIV_TYPE_CHAR | 128, IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_MASK, "efuse_get"},
	{
		SIOCIWFIRSTPRIV + 0x1D,
		IW_PRIV_TYPE_CHAR | 40, IW_PRIV_TYPE_CHAR | 0x7FF, "test"
	},

#ifdef CONFIG_INTEL_WIDI
	{
		SIOCIWFIRSTPRIV + 0x1E,
		IW_PRIV_TYPE_CHAR | 1024, 0, "widi_set"
	},
	{
		SIOCIWFIRSTPRIV + 0x1F,
		IW_PRIV_TYPE_CHAR | 128, 0, "widi_prob_req"
	},
#endif /*  CONFIG_INTEL_WIDI */

#ifdef CONFIG_WOWLAN
		{ MP_WOW_ENABLE , IW_PRIV_TYPE_CHAR | 1024, 0, "wow_mode" }, /* set */
#endif
#ifdef CONFIG_AP_WOWLAN
		{ MP_AP_WOW_ENABLE , IW_PRIV_TYPE_CHAR | 1024, 0, "ap_wow_mode" }, /* set */
#endif
};

static iw_handler rtw_private_handler[] = {
	rtw_wx_write32,					/* 0x00 */
	rtw_wx_read32,					/* 0x01 */
	rtw_drvext_hdl,					/* 0x02 */
	rtw_mp_ioctl_hdl,				/* 0x03 */

/*  for MM DTV platform */
	rtw_get_ap_info,					/* 0x04 */

	rtw_set_pid,						/* 0x05 */
	rtw_wps_start,					/* 0x06 */

/*  for PLATFORM_MT53XX */
	rtw_wx_get_sensitivity,			/* 0x07 */
	rtw_wx_set_mtk_wps_probe_ie,	/* 0x08 */
	rtw_wx_set_mtk_wps_ie,			/* 0x09 */

/*  for RTK_DMP_PLATFORM */
/*  Set Channel depend on the country code */
	rtw_wx_set_channel_plan,		/* 0x0A */

	rtw_dbg_port,					/* 0x0B */
	rtw_wx_write_rf,					/* 0x0C */
	rtw_wx_read_rf,					/* 0x0D */
	rtw_wx_priv_null,				/* 0x0E */
	rtw_wx_priv_null,				/* 0x0F */
	rtw_p2p_set,					/* 0x10 */
	rtw_p2p_get,					/* 0x11 */
	NULL,							/* 0x12 */
	rtw_p2p_get2,					/* 0x13 */

	rtw_tdls,						/* 0x14 */
	rtw_tdls_get,					/* 0x15 */

	rtw_pm_set,						/* 0x16 */
	rtw_wx_priv_null,				/* 0x17 */
	rtw_rereg_nd_name,				/* 0x18 */
	rtw_wx_priv_null,				/* 0x19 */
	rtw_mp_efuse_set,				/* 0x1A */
	rtw_mp_efuse_get,				/* 0x1B */
	NULL,							/*  0x1C is reserved for hostapd */
	rtw_test,						/*  0x1D */
#ifdef CONFIG_INTEL_WIDI
	rtw_widi_set,					/* 0x1E */
	rtw_widi_set_probe_request,		/* 0x1F */
#endif /*  CONFIG_INTEL_WIDI */
};

static struct iw_statistics *rtw_get_wireless_stats(struct net_device *dev)
{
	struct adapter *padapter = (struct adapter *)rtw_netdev_priv(dev);
	struct iw_statistics *piwstats =&padapter->iwstats;
	int tmp_level = 0;
	int tmp_qual = 0;
	int tmp_noise = 0;

	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED) != true) {
		piwstats->qual.qual = 0;
		piwstats->qual.level = 0;
		piwstats->qual.noise = 0;
		/* DBG_871X("No link  level:%d, qual:%d, noise:%d\n", tmp_level, tmp_qual, tmp_noise); */
	} else {
		#ifdef CONFIG_SIGNAL_DISPLAY_DBM
		tmp_level = translate_percentage_to_dbm(padapter->recvpriv.signal_strength);
		#else
		#ifdef CONFIG_SKIP_SIGNAL_SCALE_MAPPING
		{
			/* Do signal scale mapping when using percentage as the unit of signal strength, since the scale mapping is skipped in odm */

			struct hal_com_data *pHal = GET_HAL_DATA(padapter);

			tmp_level = (u8)odm_SignalScaleMapping(&pHal->odmpriv, padapter->recvpriv.signal_strength);
		}
		#else
		tmp_level = padapter->recvpriv.signal_strength;
		#endif
		#endif

		tmp_qual = padapter->recvpriv.signal_qual;
#if defined(CONFIG_SIGNAL_DISPLAY_DBM) && defined(CONFIG_BACKGROUND_NOISE_MONITOR)
		if (rtw_linked_check(padapter)) {
			struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
			struct noise_info info;
			info.bPauseDIG = true;
			info.IGIValue = 0x1e;
			info.max_time = 100;/* ms */
			info.chan = pmlmeext->cur_channel ;/* rtw_get_oper_ch(padapter); */
			rtw_ps_deny(padapter, PS_DENY_IOCTL);
			LeaveAllPowerSaveModeDirect(padapter);

			rtw_hal_set_odm_var(padapter, HAL_ODM_NOISE_MONITOR,&info, false);
			/* ODM_InbandNoise_Monitor(podmpriv, true, 0x20, 100); */
			rtw_ps_deny_cancel(padapter, PS_DENY_IOCTL);
			rtw_hal_get_odm_var(padapter, HAL_ODM_NOISE_MONITOR,&(info.chan), &(padapter->recvpriv.noise));
			DBG_871X("chan:%d, noise_level:%d\n", info.chan, padapter->recvpriv.noise);
		}
#endif
		tmp_noise = padapter->recvpriv.noise;
		DBG_871X("level:%d, qual:%d, noise:%d, rssi (%d)\n", tmp_level, tmp_qual, tmp_noise, padapter->recvpriv.rssi);

		piwstats->qual.level = tmp_level;
		piwstats->qual.qual = tmp_qual;
		piwstats->qual.noise = tmp_noise;
	}
	piwstats->qual.updated = IW_QUAL_ALL_UPDATED ;/* IW_QUAL_DBM; */

	#ifdef CONFIG_SIGNAL_DISPLAY_DBM
	piwstats->qual.updated = piwstats->qual.updated | IW_QUAL_DBM;
	#endif

	return &padapter->iwstats;
}

struct iw_handler_def rtw_handlers_def = {
	.standard = rtw_handlers,
	.num_standard = ARRAY_SIZE(rtw_handlers),
#if defined(CONFIG_WEXT_PRIV)
	.private = rtw_private_handler,
	.private_args = (struct iw_priv_args *)rtw_private_args,
	.num_private = ARRAY_SIZE(rtw_private_handler),
	.num_private_args = ARRAY_SIZE(rtw_private_args),
#endif
	.get_wireless_stats = rtw_get_wireless_stats,
};

/*  copy from net/wireless/wext.c start */
/* ---------------------------------------------------------------- */
/*
 * Calculate size of private arguments
 */
static const char iw_priv_type_size[] = {
	0,                              /* IW_PRIV_TYPE_NONE */
	1,                              /* IW_PRIV_TYPE_BYTE */
	1,                              /* IW_PRIV_TYPE_CHAR */
	0,                              /* Not defined */
	sizeof(__u32),                  /* IW_PRIV_TYPE_INT */
	sizeof(struct iw_freq),         /* IW_PRIV_TYPE_FLOAT */
	sizeof(struct sockaddr),        /* IW_PRIV_TYPE_ADDR */
	0,                              /* Not defined */
};

static int get_priv_size(__u16 args)
{
	int num = args & IW_PRIV_SIZE_MASK;
	int type = (args & IW_PRIV_TYPE_MASK) >> 12;

	return num * iw_priv_type_size[type];
}
/*  copy from net/wireless/wext.c end */

static int rtw_ioctl_wext_private(struct net_device *dev, union iwreq_data *wrq_data)
{
	int err = 0;
	u8 *input = NULL;
	u32 input_len = 0;
	const char delim[] = " ";
	u8 *output = NULL;
	u32 output_len = 0;
	u32 count = 0;
	u8 *buffer = NULL;
	u32 buffer_len = 0;
	char *ptr = NULL;
	u8 cmdname[17] = {0}; /*  IFNAMSIZ+1 */
	u32 cmdlen;
	s32 len;
	u8 *extra = NULL;
	u32 extra_size = 0;

	s32 k;
	const iw_handler *priv;		/* Private ioctl */
	const struct iw_priv_args *priv_args;	/* Private ioctl description */
	u32 num_priv;				/* Number of ioctl */
	u32 num_priv_args;			/* Number of descriptions */
	iw_handler handler;
	int temp;
	int subcmd = 0;				/* sub-ioctl index */
	int offset = 0;				/* Space for sub-ioctl index */

	union iwreq_data wdata;


	memcpy(&wdata, wrq_data, sizeof(wdata));

	input_len = 2048;
	input = rtw_zmalloc(input_len);
	if (NULL == input)
		return -ENOMEM;
	if (copy_from_user(input, wdata.data.pointer, input_len)) {
		err = -EFAULT;
		goto exit;
	}
	ptr = input;
	len = strlen(input);

	sscanf(ptr, "%16s", cmdname);
	cmdlen = strlen(cmdname);
	DBG_8192C("%s: cmd =%s\n", __func__, cmdname);

	/*  skip command string */
	if (cmdlen > 0)
		cmdlen += 1; /*  skip one space */
	ptr += cmdlen;
	len -= cmdlen;
	DBG_8192C("%s: parameters =%s\n", __func__, ptr);

	priv = rtw_private_handler;
	priv_args = rtw_private_args;
	num_priv = ARRAY_SIZE(rtw_private_handler);
	num_priv_args = ARRAY_SIZE(rtw_private_args);

	if (num_priv_args == 0) {
		err = -EOPNOTSUPP;
		goto exit;
	}

	/* Search the correct ioctl */
	k = -1;
	while ((++k < num_priv_args) && strcmp(priv_args[k].name, cmdname));

	/* If not found... */
	if (k == num_priv_args) {
		err = -EOPNOTSUPP;
		goto exit;
	}

	/* Watch out for sub-ioctls ! */
	if (priv_args[k].cmd < SIOCDEVPRIVATE) {
		int j = -1;

		/* Find the matching *real* ioctl */
		while ((++j < num_priv_args) && ((priv_args[j].name[0] != '\0') ||
			(priv_args[j].set_args != priv_args[k].set_args) ||
			(priv_args[j].get_args != priv_args[k].get_args)));

		/* If not found... */
		if (j == num_priv_args) {
			err = -EINVAL;
			goto exit;
		}

		/* Save sub-ioctl number */
		subcmd = priv_args[k].cmd;
		/* Reserve one int (simplify alignment issues) */
		offset = sizeof(__u32);
		/* Use real ioctl definition from now on */
		k = j;
	}

	buffer = rtw_zmalloc(4096);
	if (NULL == buffer) {
		err = -ENOMEM;
		goto exit;
	}

	/* If we have to set some data */
	if ((priv_args[k].set_args & IW_PRIV_TYPE_MASK) &&
		(priv_args[k].set_args & IW_PRIV_SIZE_MASK)) {
		u8 *str;

		switch (priv_args[k].set_args & IW_PRIV_TYPE_MASK) {
			case IW_PRIV_TYPE_BYTE:
				/* Fetch args */
				count = 0;
				do {
					str = strsep(&ptr, delim);
					if (NULL == str) break;
					sscanf(str, "%i", &temp);
					buffer[count++] = (u8)temp;
				} while (1);
				buffer_len = count;

				/* Number of args to fetch */
				wdata.data.length = count;
				if (wdata.data.length > (priv_args[k].set_args & IW_PRIV_SIZE_MASK))
					wdata.data.length = priv_args[k].set_args & IW_PRIV_SIZE_MASK;

				break;

			case IW_PRIV_TYPE_INT:
				/* Fetch args */
				count = 0;
				do {
					str = strsep(&ptr, delim);
					if (NULL == str) break;
					sscanf(str, "%i", &temp);
					((s32*)buffer)[count++] = (s32)temp;
				} while (1);
				buffer_len = count * sizeof(s32);

				/* Number of args to fetch */
				wdata.data.length = count;
				if (wdata.data.length > (priv_args[k].set_args & IW_PRIV_SIZE_MASK))
					wdata.data.length = priv_args[k].set_args & IW_PRIV_SIZE_MASK;

				break;

			case IW_PRIV_TYPE_CHAR:
				if (len > 0) {
					/* Size of the string to fetch */
					wdata.data.length = len;
					if (wdata.data.length > (priv_args[k].set_args & IW_PRIV_SIZE_MASK))
						wdata.data.length = priv_args[k].set_args & IW_PRIV_SIZE_MASK;

					/* Fetch string */
					memcpy(buffer, ptr, wdata.data.length);
				} else {
					wdata.data.length = 1;
					buffer[0] = '\0';
				}
				buffer_len = wdata.data.length;
				break;

			default:
				DBG_8192C("%s: Not yet implemented...\n", __func__);
				err = -1;
				goto exit;
		}

		if ((priv_args[k].set_args & IW_PRIV_SIZE_FIXED) &&
			(wdata.data.length != (priv_args[k].set_args & IW_PRIV_SIZE_MASK))) {
			DBG_8192C("%s: The command %s needs exactly %d argument(s)...\n",
					__func__, cmdname, priv_args[k].set_args & IW_PRIV_SIZE_MASK);
			err = -EINVAL;
			goto exit;
		}
	} else { /* if args to set */
		wdata.data.length = 0L;
	}

	/* Those two tests are important. They define how the driver
	* will have to handle the data */
	if ((priv_args[k].set_args & IW_PRIV_SIZE_FIXED) &&
		((get_priv_size(priv_args[k].set_args) + offset) <= IFNAMSIZ)) {
		/* First case : all SET args fit within wrq */
		if (offset)
			wdata.mode = subcmd;
		memcpy(wdata.name + offset, buffer, IFNAMSIZ - offset);
	} else {
		if ((priv_args[k].set_args == 0) &&
			(priv_args[k].get_args & IW_PRIV_SIZE_FIXED) &&
			(get_priv_size(priv_args[k].get_args) <= IFNAMSIZ)) {
			/* Second case : no SET args, GET args fit within wrq */
			if (offset)
				wdata.mode = subcmd;
		} else {
			/* Third case : args won't fit in wrq, or variable number of args */
			if (copy_to_user(wdata.data.pointer, buffer, buffer_len)) {
				err = -EFAULT;
				goto exit;
			}
			wdata.data.flags = subcmd;
		}
	}

	kfree(input);
	input = NULL;

	extra_size = 0;
	if (IW_IS_SET(priv_args[k].cmd)) {
		/* Size of set arguments */
		extra_size = get_priv_size(priv_args[k].set_args);

		/* Does it fits in iwr ? */
		if ((priv_args[k].set_args & IW_PRIV_SIZE_FIXED) &&
			((extra_size + offset) <= IFNAMSIZ))
			extra_size = 0;
	} else {
		/* Size of get arguments */
		extra_size = get_priv_size(priv_args[k].get_args);

		/* Does it fits in iwr ? */
		if ((priv_args[k].get_args & IW_PRIV_SIZE_FIXED) &&
			(extra_size <= IFNAMSIZ))
			extra_size = 0;
	}

	if (extra_size == 0) {
		extra = (u8 *)&wdata;
		kfree(buffer);
		buffer = NULL;
	} else
		extra = buffer;

	handler = priv[priv_args[k].cmd - SIOCIWFIRSTPRIV];
	err = handler(dev, NULL, &wdata, extra);

	/* If we have to get some data */
	if ((priv_args[k].get_args & IW_PRIV_TYPE_MASK) &&
		(priv_args[k].get_args & IW_PRIV_SIZE_MASK)) {
		int j;
		int n = 0;	/* number of args */
		u8 str[20] = {0};

		/* Check where is the returned data */
		if ((priv_args[k].get_args & IW_PRIV_SIZE_FIXED) &&
			(get_priv_size(priv_args[k].get_args) <= IFNAMSIZ))
			n = priv_args[k].get_args & IW_PRIV_SIZE_MASK;
		else
			n = wdata.data.length;

		output = rtw_zmalloc(4096);
		if (NULL == output) {
			err =  -ENOMEM;
			goto exit;
		}

		switch (priv_args[k].get_args & IW_PRIV_TYPE_MASK) {
			case IW_PRIV_TYPE_BYTE:
				/* Display args */
				for (j = 0; j < n; j++) {
					sprintf(str, "%d  ", extra[j]);
					len = strlen(str);
					output_len = strlen(output);
					if ((output_len + len + 1) > 4096) {
						err = -E2BIG;
						goto exit;
					}
					memcpy(output+output_len, str, len);
				}
				break;

			case IW_PRIV_TYPE_INT:
				/* Display args */
				for (j = 0; j < n; j++) {
					sprintf(str, "%d  ", ((__s32*)extra)[j]);
					len = strlen(str);
					output_len = strlen(output);
					if ((output_len + len + 1) > 4096) {
						err = -E2BIG;
						goto exit;
					}
					memcpy(output+output_len, str, len);
				}
				break;

			case IW_PRIV_TYPE_CHAR:
				/* Display args */
				memcpy(output, extra, n);
				break;

			default:
				DBG_8192C("%s: Not yet implemented...\n", __func__);
				err = -1;
				goto exit;
		}

		output_len = strlen(output) + 1;
		wrq_data->data.length = output_len;
		if (copy_to_user(wrq_data->data.pointer, output, output_len)) {
			err = -EFAULT;
			goto exit;
		}
	} else { /* if args to set */
		wrq_data->data.length = 0;
	}

exit:
	kfree(input);
	kfree(buffer);
	kfree(output);

	return err;
}

int rtw_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct iwreq *wrq = (struct iwreq *)rq;
	int ret = 0;

	switch (cmd) {
		case RTL_IOCTL_WPA_SUPPLICANT:
			ret = wpa_supplicant_ioctl(dev, &wrq->u.data);
			break;
		case RTL_IOCTL_HOSTAPD:
			ret = rtw_hostapd_ioctl(dev, &wrq->u.data);
			break;
		case SIOCDEVPRIVATE:
			ret = rtw_ioctl_wext_private(dev, &wrq->u);
			break;
		default:
			ret = -EOPNOTSUPP;
			break;
	}

	return ret;
}
