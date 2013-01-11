/******************************************************************************
 * rtl871x_ioctl_linux.c
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define _RTL871X_IOCTL_LINUX_C_
#define _RTL871X_MP_IOCTL_C_

#include "osdep_service.h"
#include "drv_types.h"
#include "wlan_bssdef.h"
#include "rtl871x_debug.h"
#include "wifi.h"
#include "rtl871x_mlme.h"
#include "rtl871x_ioctl.h"
#include "rtl871x_ioctl_set.h"
#include "rtl871x_mp_ioctl.h"
#include "mlme_osdep.h"
#include <linux/wireless.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/semaphore.h>
#include <net/iw_handler.h>
#include <linux/if_arp.h>

#define RTL_IOCTL_WPA_SUPPLICANT	(SIOCIWFIRSTPRIV + 0x1E)

#define SCAN_ITEM_SIZE 768
#define MAX_CUSTOM_LEN 64
#define RATE_COUNT 4


static const u32 rtl8180_rates[] = {1000000, 2000000, 5500000, 11000000,
		       6000000, 9000000, 12000000, 18000000,
		       24000000, 36000000, 48000000, 54000000};

static const long ieee80211_wlan_frequencies[] = {
	2412, 2417, 2422, 2427,
	2432, 2437, 2442, 2447,
	2452, 2457, 2462, 2467,
	2472, 2484
};

static const char * const iw_operation_mode[] = {
	"Auto", "Ad-Hoc", "Managed",  "Master", "Repeater", "Secondary",
	 "Monitor"
};

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

		a = hex_to_bin(*txt++);
		if (a < 0)
			return -1;
		b = hex_to_bin(*txt++);
		if (b < 0)
			return -1;
		*addr++ = (a << 4) | b;
		if (i < 5 && *txt++ != ':')
			return -1;
	}
	return 0;
}

void r8712_indicate_wx_assoc_event(struct _adapter *padapter)
{
	union iwreq_data wrqu;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(wrqu.ap_addr.sa_data, pmlmepriv->cur_network.network.MacAddress,
		ETH_ALEN);
	wireless_send_event(padapter->pnetdev, SIOCGIWAP, &wrqu, NULL);
}

void r8712_indicate_wx_disassoc_event(struct _adapter *padapter)
{
	union iwreq_data wrqu;

	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	memset(wrqu.ap_addr.sa_data, 0, ETH_ALEN);
	wireless_send_event(padapter->pnetdev, SIOCGIWAP, &wrqu, NULL);
}

static inline void handle_pairwise_key(struct sta_info *psta,
				       struct ieee_param *param,
				       struct _adapter *padapter)
{
	/* pairwise key */
	memcpy(psta->x_UncstKey.skey, param->u.crypt.key,
	       (param->u.crypt. key_len > 16 ? 16 : param->u.crypt.key_len));
	if (strcmp(param->u.crypt.alg, "TKIP") == 0) { /* set mic key */
		memcpy(psta->tkiptxmickey. skey, &(param->u.crypt.
			key[16]), 8);
		memcpy(psta->tkiprxmickey. skey, &(param->u.crypt.
			key[24]), 8);
		padapter->securitypriv. busetkipkey = false;
		_set_timer(&padapter->securitypriv.tkip_timer, 50);
	}
	r8712_setstakey_cmd(padapter, (unsigned char *)psta, true);
}

static inline void handle_group_key(struct ieee_param *param,
				    struct _adapter *padapter)
{
	if (0 < param->u.crypt.idx &&
	    param->u.crypt.idx < 3) {
		/* group key idx is 1 or 2 */
		memcpy(padapter->securitypriv.XGrpKey[param->u.crypt.
			idx-1].skey, param->u.crypt.key, (param->u.crypt.key_len
			> 16 ? 16 : param->u.crypt.key_len));
		memcpy(padapter->securitypriv.XGrptxmickey[param->
			u.crypt.idx-1].skey, &(param->u.crypt.key[16]), 8);
		memcpy(padapter->securitypriv. XGrprxmickey[param->
			u.crypt.idx-1].skey, &(param->u.crypt.key[24]), 8);
		padapter->securitypriv.binstallGrpkey = true;
		r8712_set_key(padapter, &padapter->securitypriv,
			param->u.crypt.idx);
		if (padapter->registrypriv.power_mgnt > PS_MODE_ACTIVE) {
			if (padapter->registrypriv.power_mgnt != padapter->
			    pwrctrlpriv.pwr_mode)
				_set_timer(&(padapter->mlmepriv.dhcp_timer),
					   60000);
		}
	}
}

static inline char *translate_scan(struct _adapter *padapter,
				   struct iw_request_info *info,
				   struct wlan_network *pnetwork,
				   char *start, char *stop)
{
	struct iw_event iwe;
	struct ieee80211_ht_cap *pht_capie;
	char *current_val;
	s8 *p;
	u32 i = 0, ht_ielen = 0;
	u16	cap, ht_cap = false, mcs_rate;
	u8	rssi, bw_40MHz = 0, short_GI = 0;

	if ((pnetwork->network.Configuration.DSConfig < 1) ||
	    (pnetwork->network.Configuration.DSConfig > 14)) {
		if (pnetwork->network.Configuration.DSConfig < 1)
			pnetwork->network.Configuration.DSConfig = 1;
		else
			pnetwork->network.Configuration.DSConfig = 14;
	}
	/* AP MAC address */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, pnetwork->network.MacAddress, ETH_ALEN);
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_ADDR_LEN);
	/* Add the ESSID */
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.flags = 1;
	iwe.u.data.length = (u16)min((u16)pnetwork->network.Ssid.SsidLength,
			    (u16)32);
	start = iwe_stream_add_point(info, start, stop, &iwe,
				     pnetwork->network.Ssid.Ssid);
	/* parsing HT_CAP_IE */
	p = r8712_get_ie(&pnetwork->network.IEs[12], _HT_CAPABILITY_IE_,
			 &ht_ielen, pnetwork->network.IELength - 12);
	if (p && ht_ielen > 0) {
		ht_cap = true;
		pht_capie = (struct ieee80211_ht_cap *)(p + 2);
		memcpy(&mcs_rate , pht_capie->supp_mcs_set, 2);
		bw_40MHz = (pht_capie->cap_info&IEEE80211_HT_CAP_SUP_WIDTH)
			   ? 1 : 0;
		short_GI = (pht_capie->cap_info&(IEEE80211_HT_CAP_SGI_20 |
			    IEEE80211_HT_CAP_SGI_40)) ? 1 : 0;
	}
	/* Add the protocol name */
	iwe.cmd = SIOCGIWNAME;
	if ((r8712_is_cckratesonly_included((u8 *)&pnetwork->network.
	     SupportedRates)) == true) {
		if (ht_cap == true)
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bn");
		else
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11b");
	} else if ((r8712_is_cckrates_included((u8 *)&pnetwork->network.
		    SupportedRates)) == true) {
		if (ht_cap == true)
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bgn");
		else
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11bg");
	} else {
		if (ht_cap == true)
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11gn");
		else
			snprintf(iwe.u.name, IFNAMSIZ, "IEEE 802.11g");
	}
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_CHAR_LEN);
	/* Add mode */
	iwe.cmd = SIOCGIWMODE;
	memcpy((u8 *)&cap, r8712_get_capability_from_ie(pnetwork->network.IEs),
		2);
	cap = le16_to_cpu(cap);
	if (cap & (WLAN_CAPABILITY_IBSS|WLAN_CAPABILITY_BSS)) {
		if (cap & WLAN_CAPABILITY_BSS)
			iwe.u.mode = (u32)IW_MODE_MASTER;
		else
			iwe.u.mode = (u32)IW_MODE_ADHOC;
		start = iwe_stream_add_event(info, start, stop, &iwe,
			IW_EV_UINT_LEN);
	}
	/* Add frequency/channel */
	iwe.cmd = SIOCGIWFREQ;
	{
		/*  check legal index */
		u8 dsconfig = pnetwork->network.Configuration.DSConfig;
		if (dsconfig >= 1 && dsconfig <= sizeof(
		    ieee80211_wlan_frequencies) / sizeof(long))
			iwe.u.freq.m = (s32)(ieee80211_wlan_frequencies[
				       pnetwork->network.Configuration.
				       DSConfig - 1] * 100000);
		else
			iwe.u.freq.m = 0;
	}
	iwe.u.freq.e = (s16)1;
	iwe.u.freq.i = (u8)pnetwork->network.Configuration.DSConfig;
	start = iwe_stream_add_event(info, start, stop, &iwe,
		IW_EV_FREQ_LEN);
	/* Add encryption capability */
	iwe.cmd = SIOCGIWENCODE;
	if (cap & WLAN_CAPABILITY_PRIVACY)
		iwe.u.data.flags = (u16)(IW_ENCODE_ENABLED |
				    IW_ENCODE_NOKEY);
	else
		iwe.u.data.flags = (u16)(IW_ENCODE_DISABLED);
	iwe.u.data.length = (u16)0;
	start = iwe_stream_add_point(info, start, stop, &iwe,
		pnetwork->network.Ssid.Ssid);
	/*Add basic and extended rates */
	current_val = start + iwe_stream_lcp_len(info);
	iwe.cmd = SIOCGIWRATE;
	iwe.u.bitrate.fixed = 0;
	iwe.u.bitrate.disabled = 0;
	iwe.u.bitrate.value = 0;
	i = 0;
	while (pnetwork->network.SupportedRates[i] != 0) {
		/* Bit rate given in 500 kb/s units */
		iwe.u.bitrate.value = (pnetwork->network.SupportedRates[i++] &
				      0x7F) * 500000;
		current_val = iwe_stream_add_value(info, start, current_val,
			      stop, &iwe, IW_EV_PARAM_LEN);
	}
	/* Check if we added any event */
	if ((current_val - start) > iwe_stream_lcp_len(info))
		start = current_val;
	/* parsing WPA/WPA2 IE */
	{
		u8 buf[MAX_WPA_IE_LEN];
		u8 wpa_ie[255], rsn_ie[255];
		u16 wpa_len = 0, rsn_len = 0;
		int n;
		sint out_len = 0;
		out_len = r8712_get_sec_ie(pnetwork->network.IEs,
					   pnetwork->network.
					   IELength, rsn_ie, &rsn_len,
					   wpa_ie, &wpa_len);
		if (wpa_len > 0) {
			memset(buf, 0, MAX_WPA_IE_LEN);
			n = sprintf(buf, "wpa_ie=");
			for (i = 0; i < wpa_len; i++) {
				n += snprintf(buf + n, MAX_WPA_IE_LEN - n,
							"%02x", wpa_ie[i]);
				if (n >= MAX_WPA_IE_LEN)
					break;
			}
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVCUSTOM;
			iwe.u.data.length = (u16)strlen(buf);
			start = iwe_stream_add_point(info, start, stop,
				&iwe, buf);
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVGENIE;
			iwe.u.data.length = (u16)wpa_len;
			start = iwe_stream_add_point(info, start, stop,
				&iwe, wpa_ie);
		}
		if (rsn_len > 0) {
			memset(buf, 0, MAX_WPA_IE_LEN);
			n = sprintf(buf, "rsn_ie=");
			for (i = 0; i < rsn_len; i++) {
				n += snprintf(buf + n, MAX_WPA_IE_LEN - n,
							"%02x", rsn_ie[i]);
				if (n >= MAX_WPA_IE_LEN)
					break;
			}
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVCUSTOM;
			iwe.u.data.length = strlen(buf);
			start = iwe_stream_add_point(info, start, stop,
				&iwe, buf);
			memset(&iwe, 0, sizeof(iwe));
			iwe.cmd = IWEVGENIE;
			iwe.u.data.length = rsn_len;
			start = iwe_stream_add_point(info, start, stop, &iwe,
				rsn_ie);
		}
	}

	{ /* parsing WPS IE */
		u8 wps_ie[512];
		uint wps_ielen;

		if (r8712_get_wps_ie(pnetwork->network.IEs,
		    pnetwork->network.IELength,
		    wps_ie, &wps_ielen) == true) {
			if (wps_ielen > 2) {
				iwe.cmd = IWEVGENIE;
				iwe.u.data.length = (u16)wps_ielen;
				start = iwe_stream_add_point(info, start, stop,
					&iwe, wps_ie);
			}
		}
	}
	/* Add quality statistics */
	iwe.cmd = IWEVQUAL;
	rssi = r8712_signal_scale_mapping(pnetwork->network.Rssi);
	/* we only update signal_level (signal strength) that is rssi. */
	iwe.u.qual.updated = (u8)(IW_QUAL_QUAL_INVALID | IW_QUAL_LEVEL_UPDATED |
				  IW_QUAL_NOISE_INVALID);
	iwe.u.qual.level = rssi;  /* signal strength */
	iwe.u.qual.qual = 0; /* signal quality */
	iwe.u.qual.noise = 0; /* noise level */
	start = iwe_stream_add_event(info, start, stop, &iwe, IW_EV_QUAL_LEN);
	/* how to translate rssi to ?% */
	return start;
}

static int wpa_set_auth_algs(struct net_device *dev, u32 value)
{
	struct _adapter *padapter = (struct _adapter *) netdev_priv(dev);
	int ret = 0;

	if ((value & AUTH_ALG_SHARED_KEY) && (value & AUTH_ALG_OPEN_SYSTEM)) {
		padapter->securitypriv.ndisencryptstatus =
						 Ndis802_11Encryption1Enabled;
		padapter->securitypriv.ndisauthtype =
						 Ndis802_11AuthModeAutoSwitch;
		padapter->securitypriv.AuthAlgrthm = 3;
	} else if (value & AUTH_ALG_SHARED_KEY) {
		padapter->securitypriv.ndisencryptstatus =
						 Ndis802_11Encryption1Enabled;
		padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeShared;
		padapter->securitypriv.AuthAlgrthm = 1;
	} else if (value & AUTH_ALG_OPEN_SYSTEM) {
		if (padapter->securitypriv.ndisauthtype <
						 Ndis802_11AuthModeWPAPSK) {
			padapter->securitypriv.ndisauthtype =
						 Ndis802_11AuthModeOpen;
			padapter->securitypriv.AuthAlgrthm = 0;
		}
	} else
		ret = -EINVAL;
	return ret;
}

static int wpa_set_encryption(struct net_device *dev, struct ieee_param *param,
			      u32 param_len)
{
	int ret = 0;
	u32 wep_key_idx, wep_key_len = 0;
	struct NDIS_802_11_WEP	 *pwep = NULL;
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	param->u.crypt.err = 0;
	param->u.crypt.alg[IEEE_CRYPT_ALG_NAME_LEN - 1] = '\0';
	if (param_len != (u32)((u8 *) param->u.crypt.key - (u8 *)param) +
			 param->u.crypt.key_len)
		return -EINVAL;
	if (is_broadcast_ether_addr(param->sta_addr)) {
		if (param->u.crypt.idx >= WEP_KEYS) {
			/* for large key indices, set the default (0) */
			param->u.crypt.idx = 0;
		}
	} else
		return -EINVAL;
	if (strcmp(param->u.crypt.alg, "WEP") == 0) {
		printk(KERN_INFO "r8712u: wpa_set_encryption, crypt.alg ="
		       " WEP\n");
		padapter->securitypriv.ndisencryptstatus =
			     Ndis802_11Encryption1Enabled;
		padapter->securitypriv.PrivacyAlgrthm = _WEP40_;
		padapter->securitypriv.XGrpPrivacy = _WEP40_;
		wep_key_idx = param->u.crypt.idx;
		wep_key_len = param->u.crypt.key_len;
		if (wep_key_idx >= WEP_KEYS)
			wep_key_idx = 0;
		if (wep_key_len > 0) {
			wep_key_len = wep_key_len <= 5 ? 5 : 13;
			pwep = (struct NDIS_802_11_WEP *)_malloc((u32)
			       (wep_key_len +
			       FIELD_OFFSET(struct NDIS_802_11_WEP,
			       KeyMaterial)));
			if (pwep == NULL)
				return -ENOMEM;
			memset(pwep, 0, sizeof(struct NDIS_802_11_WEP));
			pwep->KeyLength = wep_key_len;
			pwep->Length = wep_key_len +
				 FIELD_OFFSET(struct NDIS_802_11_WEP,
				 KeyMaterial);
			if (wep_key_len == 13) {
				padapter->securitypriv.PrivacyAlgrthm =
					 _WEP104_;
				padapter->securitypriv.XGrpPrivacy =
					 _WEP104_;
			}
		} else
			return -EINVAL;
		pwep->KeyIndex = wep_key_idx;
		pwep->KeyIndex |= 0x80000000;
		memcpy(pwep->KeyMaterial, param->u.crypt.key, pwep->KeyLength);
		if (param->u.crypt.set_tx) {
			if (r8712_set_802_11_add_wep(padapter, pwep) ==
			    (u8)_FAIL)
				ret = -EOPNOTSUPP;
		} else {
			/* don't update "psecuritypriv->PrivacyAlgrthm" and
			 * "psecuritypriv->PrivacyKeyIndex=keyid", but can
			 * r8712_set_key to fw/cam
			 */
			if (wep_key_idx >= WEP_KEYS) {
				ret = -EOPNOTSUPP;
				goto exit;
			}
			memcpy(&(psecuritypriv->DefKey[wep_key_idx].
				skey[0]), pwep->KeyMaterial,
				pwep->KeyLength);
			psecuritypriv->DefKeylen[wep_key_idx] =
				pwep->KeyLength;
			r8712_set_key(padapter, psecuritypriv, wep_key_idx);
		}
		goto exit;
	}
	if (padapter->securitypriv.AuthAlgrthm == 2) { /* 802_1x */
		struct sta_info *psta, *pbcmc_sta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE |
		    WIFI_MP_STATE) == true) { /* sta mode */
			psta = r8712_get_stainfo(pstapriv,
						 get_bssid(pmlmepriv));
			if (psta) {
				psta->ieee8021x_blocked = false;
				if ((padapter->securitypriv.ndisencryptstatus ==
				    Ndis802_11Encryption2Enabled) ||
				    (padapter->securitypriv.ndisencryptstatus ==
				    Ndis802_11Encryption3Enabled))
					psta->XPrivacy = padapter->
					    securitypriv.PrivacyAlgrthm;
				if (param->u.crypt.set_tx == 1)
					handle_pairwise_key(psta, param,
							    padapter);
				else /* group key */
					handle_group_key(param, padapter);
			}
			pbcmc_sta = r8712_get_bcmc_stainfo(padapter);
			if (pbcmc_sta) {
				pbcmc_sta->ieee8021x_blocked = false;
				if ((padapter->securitypriv.ndisencryptstatus ==
				    Ndis802_11Encryption2Enabled) ||
				    (padapter->securitypriv.ndisencryptstatus ==
				    Ndis802_11Encryption3Enabled))
					pbcmc_sta->XPrivacy =
					  padapter->securitypriv.
					  PrivacyAlgrthm;
			}
		}
	}
exit:
	kfree((u8 *)pwep);
	return ret;
}

static int r871x_set_wpa_ie(struct _adapter *padapter, char *pie,
			    unsigned short ielen)
{
	u8 *buf = NULL, *pos = NULL;
	int group_cipher = 0, pairwise_cipher = 0;
	int ret = 0;

	if ((ielen > MAX_WPA_IE_LEN) || (pie == NULL))
		return -EINVAL;
	if (ielen) {
		buf = _malloc(ielen);
		if (buf == NULL)
			return -ENOMEM;
		memcpy(buf, pie , ielen);
		pos = buf;
		if (ielen < RSN_HEADER_LEN) {
			ret  = -EINVAL;
			goto exit;
		}
		if (r8712_parse_wpa_ie(buf, ielen, &group_cipher,
		    &pairwise_cipher) == _SUCCESS) {
			padapter->securitypriv.AuthAlgrthm = 2;
			padapter->securitypriv.ndisauthtype =
				  Ndis802_11AuthModeWPAPSK;
		}
		if (r8712_parse_wpa2_ie(buf, ielen, &group_cipher,
		    &pairwise_cipher) == _SUCCESS) {
			padapter->securitypriv.AuthAlgrthm = 2;
			padapter->securitypriv.ndisauthtype =
				  Ndis802_11AuthModeWPA2PSK;
		}
		switch (group_cipher) {
		case WPA_CIPHER_NONE:
			padapter->securitypriv.XGrpPrivacy =
				 _NO_PRIVACY_;
			padapter->securitypriv.ndisencryptstatus =
				 Ndis802_11EncryptionDisabled;
			break;
		case WPA_CIPHER_WEP40:
			padapter->securitypriv.XGrpPrivacy = _WEP40_;
			padapter->securitypriv.ndisencryptstatus =
				 Ndis802_11Encryption1Enabled;
			break;
		case WPA_CIPHER_TKIP:
			padapter->securitypriv.XGrpPrivacy = _TKIP_;
			padapter->securitypriv.ndisencryptstatus =
				 Ndis802_11Encryption2Enabled;
			break;
		case WPA_CIPHER_CCMP:
			padapter->securitypriv.XGrpPrivacy = _AES_;
			padapter->securitypriv.ndisencryptstatus =
				 Ndis802_11Encryption3Enabled;
			break;
		case WPA_CIPHER_WEP104:
			padapter->securitypriv.XGrpPrivacy = _WEP104_;
			padapter->securitypriv.ndisencryptstatus =
				 Ndis802_11Encryption1Enabled;
			break;
		}
		switch (pairwise_cipher) {
		case WPA_CIPHER_NONE:
			padapter->securitypriv.PrivacyAlgrthm =
				 _NO_PRIVACY_;
			padapter->securitypriv.ndisencryptstatus =
				 Ndis802_11EncryptionDisabled;
			break;
		case WPA_CIPHER_WEP40:
			padapter->securitypriv.PrivacyAlgrthm = _WEP40_;
			padapter->securitypriv.ndisencryptstatus =
				 Ndis802_11Encryption1Enabled;
			break;
		case WPA_CIPHER_TKIP:
			padapter->securitypriv.PrivacyAlgrthm = _TKIP_;
			padapter->securitypriv.ndisencryptstatus =
				 Ndis802_11Encryption2Enabled;
			break;
		case WPA_CIPHER_CCMP:
			padapter->securitypriv.PrivacyAlgrthm = _AES_;
			padapter->securitypriv.ndisencryptstatus =
				 Ndis802_11Encryption3Enabled;
			break;
		case WPA_CIPHER_WEP104:
			padapter->securitypriv.PrivacyAlgrthm = _WEP104_;
			padapter->securitypriv.ndisencryptstatus =
				 Ndis802_11Encryption1Enabled;
			break;
		}
		padapter->securitypriv.wps_phase = false;
		{/* set wps_ie */
			u16 cnt = 0;
			u8 eid, wps_oui[4] = {0x0, 0x50, 0xf2, 0x04};

			while (cnt < ielen) {
				eid = buf[cnt];

				if ((eid == _VENDOR_SPECIFIC_IE_) &&
				    (!memcmp(&buf[cnt+2], wps_oui, 4))) {
					printk(KERN_INFO "r8712u: "
					       "SET WPS_IE\n");
					padapter->securitypriv.wps_ie_len =
					    ((buf[cnt+1] + 2) <
					    (MAX_WPA_IE_LEN << 2)) ?
					    (buf[cnt + 1] + 2) :
					    (MAX_WPA_IE_LEN << 2);
					memcpy(padapter->securitypriv.wps_ie,
					    &buf[cnt],
					    padapter->securitypriv.wps_ie_len);
					padapter->securitypriv.wps_phase =
								 true;
					printk(KERN_INFO "r8712u: SET WPS_IE,"
					    " wps_phase==true\n");
					cnt += buf[cnt+1]+2;
					break;
				} else
					cnt += buf[cnt + 1] + 2;
			}
		}
	}
exit:
	kfree(buf);
	return ret;
}

static int r8711_wx_get_name(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	u32 ht_ielen = 0;
	char *p;
	u8 ht_cap = false;
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	struct ndis_wlan_bssid_ex  *pcur_bss = &pmlmepriv->cur_network.network;
	NDIS_802_11_RATES_EX *prates = NULL;

	if (check_fwstate(pmlmepriv, _FW_LINKED|WIFI_ADHOC_MASTER_STATE) ==
	    true) {
		/* parsing HT_CAP_IE */
		p = r8712_get_ie(&pcur_bss->IEs[12], _HT_CAPABILITY_IE_,
				 &ht_ielen, pcur_bss->IELength - 12);
		if (p && ht_ielen > 0)
			ht_cap = true;
		prates = &pcur_bss->SupportedRates;
		if (r8712_is_cckratesonly_included((u8 *)prates) == true) {
			if (ht_cap == true)
				snprintf(wrqu->name, IFNAMSIZ,
					 "IEEE 802.11bn");
			else
				snprintf(wrqu->name, IFNAMSIZ,
					 "IEEE 802.11b");
		} else if ((r8712_is_cckrates_included((u8 *)prates)) == true) {
			if (ht_cap == true)
				snprintf(wrqu->name, IFNAMSIZ,
					 "IEEE 802.11bgn");
			else
				snprintf(wrqu->name, IFNAMSIZ,
					 "IEEE 802.11bg");
		} else {
			if (ht_cap == true)
				snprintf(wrqu->name, IFNAMSIZ,
					 "IEEE 802.11gn");
			else
				snprintf(wrqu->name, IFNAMSIZ,
					 "IEEE 802.11g");
		}
	} else
		snprintf(wrqu->name, IFNAMSIZ, "unassociated");
	return 0;
}

static const long frequency_list[] = {
	2412, 2417, 2422, 2427, 2432, 2437, 2442, 2447, 2452, 2457, 2462,
	2467, 2472, 2484, 4915, 4920, 4925, 4935, 4940, 4945, 4960, 4980,
	5035, 5040, 5045, 5055, 5060, 5080, 5170, 5180, 5190, 5200, 5210,
	5220, 5230, 5240, 5260, 5280, 5300, 5320, 5500, 5520, 5540, 5560,
	5580, 5600, 5620, 5640, 5660, 5680, 5700, 5745, 5765, 5785, 5805,
	5825
};

static int r8711_wx_set_freq(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	struct iw_freq *fwrq = &wrqu->freq;
	int rc = 0;

/* If setting by frequency, convert to a channel */
	if ((fwrq->e == 1) &&
	  (fwrq->m >= (int) 2.412e8) &&
	  (fwrq->m <= (int) 2.487e8)) {
		int f = fwrq->m / 100000;
		int c = 0;
		while ((c < 14) && (f != frequency_list[c]))
			c++;
		fwrq->e = 0;
		fwrq->m = c + 1;
	}
	/* Setting by channel number */
	if ((fwrq->m > 14) || (fwrq->e > 0))
		rc = -EOPNOTSUPP;
	else {
		int channel = fwrq->m;
		if ((channel < 1) || (channel > 14))
			rc = -EINVAL;
		else {
			/* Yes ! We can set it !!! */
			padapter->registrypriv.channel = channel;
		}
	}
	return rc;
}

static int r8711_wx_get_freq(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ndis_wlan_bssid_ex *pcur_bss = &pmlmepriv->cur_network.network;

	if (check_fwstate(pmlmepriv, _FW_LINKED) == true) {
		wrqu->freq.m = ieee80211_wlan_frequencies[
			       pcur_bss->Configuration.DSConfig-1] * 100000;
		wrqu->freq.e = 1;
		wrqu->freq.i = pcur_bss->Configuration.DSConfig;
	} else {
		return -ENOLINK;
	}
	return 0;
}

static int r8711_wx_set_mode(struct net_device *dev,
			     struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	enum NDIS_802_11_NETWORK_INFRASTRUCTURE networkType;

	switch (wrqu->mode) {
	case IW_MODE_AUTO:
		networkType = Ndis802_11AutoUnknown;
		break;
	case IW_MODE_ADHOC:
		networkType = Ndis802_11IBSS;
		break;
	case IW_MODE_MASTER:
		networkType = Ndis802_11APMode;
		break;
	case IW_MODE_INFRA:
		networkType = Ndis802_11Infrastructure;
		break;
	default:
		return -EINVAL;
	}
	if (Ndis802_11APMode == networkType)
		r8712_setopmode_cmd(padapter, networkType);
	else
		r8712_setopmode_cmd(padapter, Ndis802_11AutoUnknown);

	r8712_set_802_11_infrastructure_mode(padapter, networkType);
	return 0;
}

static int r8711_wx_get_mode(struct net_device *dev, struct iw_request_info *a,
			     union iwreq_data *wrqu, char *b)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true)
		wrqu->mode = IW_MODE_INFRA;
	else if (check_fwstate(pmlmepriv,
		 WIFI_ADHOC_MASTER_STATE|WIFI_ADHOC_STATE) == true)
		wrqu->mode = IW_MODE_ADHOC;
	else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true)
		wrqu->mode = IW_MODE_MASTER;
	else
		wrqu->mode = IW_MODE_AUTO;
	return 0;
}

static int r871x_wx_set_pmkid(struct net_device *dev,
			     struct iw_request_info *a,
			     union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct iw_pmksa *pPMK = (struct iw_pmksa *) extra;
	u8 strZeroMacAddress[ETH_ALEN] = {0x00};
	u8 strIssueBssid[ETH_ALEN] = {0x00};
	u8 j, blInserted = false;
	int intReturn = false;

/*
	There are the BSSID information in the bssid.sa_data array.
	If cmd is IW_PMKSA_FLUSH, it means the wpa_supplicant wants to clear
	all the PMKID information. If cmd is IW_PMKSA_ADD, it means the
	wpa_supplicant wants to add a PMKID/BSSID to driver.
	If cmd is IW_PMKSA_REMOVE, it means the wpa_supplicant wants to
	remove a PMKID/BSSID from driver.
*/
	if (pPMK == NULL)
		return -EINVAL;
	memcpy(strIssueBssid, pPMK->bssid.sa_data, ETH_ALEN);
	switch (pPMK->cmd) {
	case IW_PMKSA_ADD:
		if (!memcmp(strIssueBssid, strZeroMacAddress, ETH_ALEN))
			return intReturn;
		else
			intReturn = true;
		blInserted = false;
		/* overwrite PMKID */
		for (j = 0 ; j < NUM_PMKID_CACHE; j++) {
			if (!memcmp(psecuritypriv->PMKIDList[j].Bssid,
			    strIssueBssid, ETH_ALEN)) {
				/* BSSID is matched, the same AP => rewrite
				 * with new PMKID. */
				printk(KERN_INFO "r8712u: r871x_wx_set_pmkid:"
				    " BSSID exists in the PMKList.\n");
				memcpy(psecuritypriv->PMKIDList[j].PMKID,
					pPMK->pmkid, IW_PMKID_LEN);
				psecuritypriv->PMKIDList[j].bUsed = true;
				psecuritypriv->PMKIDIndex = j + 1;
				blInserted = true;
				break;
			}
		}
		if (!blInserted) {
			/* Find a new entry */
			printk(KERN_INFO "r8712u: r871x_wx_set_pmkid: Use the"
			    " new entry index = %d for this PMKID.\n",
			    psecuritypriv->PMKIDIndex);
			memcpy(psecuritypriv->PMKIDList[psecuritypriv->
				PMKIDIndex].Bssid, strIssueBssid, ETH_ALEN);
			memcpy(psecuritypriv->PMKIDList[psecuritypriv->
				PMKIDIndex].PMKID, pPMK->pmkid, IW_PMKID_LEN);
			psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].
				bUsed = true;
			psecuritypriv->PMKIDIndex++ ;
			if (psecuritypriv->PMKIDIndex == NUM_PMKID_CACHE)
				psecuritypriv->PMKIDIndex = 0;
		}
		break;
	case IW_PMKSA_REMOVE:
		intReturn = true;
		for (j = 0; j < NUM_PMKID_CACHE; j++) {
			if (!memcmp(psecuritypriv->PMKIDList[j].Bssid,
			    strIssueBssid, ETH_ALEN)) {
				/* BSSID is matched, the same AP => Remove
				 * this PMKID information and reset it. */
				memset(psecuritypriv->PMKIDList[j].Bssid,
					0x00, ETH_ALEN);
				psecuritypriv->PMKIDList[j].bUsed = false;
				break;
			}
		}
		break;
	case IW_PMKSA_FLUSH:
		memset(psecuritypriv->PMKIDList, 0,
			sizeof(struct RT_PMKID_LIST) * NUM_PMKID_CACHE);
		psecuritypriv->PMKIDIndex = 0;
		intReturn = true;
		break;
	default:
		printk(KERN_INFO "r8712u: r871x_wx_set_pmkid: "
		       "unknown Command\n");
		intReturn = false;
		break;
	}
	return intReturn;
}

static int r8711_wx_get_sens(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	wrqu->sens.value = 0;
	wrqu->sens.fixed = 0;	/* no auto select */
	wrqu->sens.disabled = 1;
	return 0;
}

static int r8711_wx_get_range(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct iw_range *range = (struct iw_range *)extra;
	u16 val;
	int i;

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
	/* TODO: 8711 sensitivity ? */
	/* signal level threshold range */
	/* percent values between 0 and 100. */
	range->max_qual.qual = 100;
	range->max_qual.level = 100;
	range->max_qual.noise = 100;
	range->max_qual.updated = 7; /* Updated all three */
	range->avg_qual.qual = 92; /* > 8% missed beacons is 'bad' */
	/* TODO: Find real 'good' to 'bad' threshold value for RSSI */
	range->avg_qual.level = 20 + -98;
	range->avg_qual.noise = 0;
	range->avg_qual.updated = 7; /* Updated all three */
	range->num_bitrates = RATE_COUNT;
	for (i = 0; i < RATE_COUNT && i < IW_MAX_BITRATES; i++)
		range->bitrate[i] = rtl8180_rates[i];
	range->min_frag = MIN_FRAG_THRESHOLD;
	range->max_frag = MAX_FRAG_THRESHOLD;
	range->pm_capa = 0;
	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 16;
	range->num_channels = 14;
	for (i = 0, val = 0; i < 14; i++) {
		/* Include only legal frequencies for some countries */
		range->freq[val].i = i + 1;
		range->freq[val].m = ieee80211_wlan_frequencies[i] * 100000;
		range->freq[val].e = 1;
		val++;
		if (val == IW_MAX_FREQUENCIES)
			break;
	}
	range->num_frequency = val;
	range->enc_capa = IW_ENC_CAPA_WPA |
			  IW_ENC_CAPA_WPA2 |
			  IW_ENC_CAPA_CIPHER_TKIP |
			  IW_ENC_CAPA_CIPHER_CCMP;
	return 0;
}

static int r8711_wx_get_rate(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra);

static int r871x_wx_set_priv(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *awrq,
				char *extra)
{
	int ret = 0, len = 0;
	char *ext;
	struct _adapter *padapter = netdev_priv(dev);
	struct iw_point *dwrq = (struct iw_point *)awrq;

	len = dwrq->length;
	ext = _malloc(len);
	if (!ext)
		return -ENOMEM;
	if (copy_from_user(ext, dwrq->pointer, len)) {
		kfree(ext);
		return -EFAULT;
	}

	if (0 == strcasecmp(ext, "RSSI")) {
		/*Return received signal strength indicator in -db for */
		/* current AP */
		/*<ssid> Rssi xx */
		struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
		struct wlan_network *pcur_network = &pmlmepriv->cur_network;
		/*static u8 xxxx; */
		if (check_fwstate(pmlmepriv, _FW_LINKED) == true) {
			sprintf(ext, "%s rssi %d",
				pcur_network->network.Ssid.Ssid,
				/*(xxxx=xxxx+10) */
				((padapter->recvpriv.fw_rssi)>>1)-95
				/*pcur_network->network.Rssi */
				);
		} else {
			sprintf(ext, "OK");
		}
	} else if (0 == strcasecmp(ext, "LINKSPEED")) {
		/*Return link speed in MBPS */
		/*LinkSpeed xx */
		union iwreq_data wrqd;
		int ret_inner;
		int mbps;

		ret_inner = r8711_wx_get_rate(dev, info, &wrqd, extra);
		if (0 != ret_inner)
			mbps = 0;
		else
			mbps = wrqd.bitrate.value / 1000000;
		sprintf(ext, "LINKSPEED %d", mbps);
	} else if (0 == strcasecmp(ext, "MACADDR")) {
		/*Return mac address of the station */
		/*Macaddr = xx.xx.xx.xx.xx.xx */
		sprintf(ext,
			"MACADDR = %02x.%02x.%02x.%02x.%02x.%02x",
			*(dev->dev_addr), *(dev->dev_addr+1),
			*(dev->dev_addr+2), *(dev->dev_addr+3),
			*(dev->dev_addr+4), *(dev->dev_addr+5));
	} else if (0 == strcasecmp(ext, "SCAN-ACTIVE")) {
		/*Set scan type to active */
		/*OK if successful */
		struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
		pmlmepriv->passive_mode = 1;
		sprintf(ext, "OK");
	} else if (0 == strcasecmp(ext, "SCAN-PASSIVE")) {
		/*Set scan type to passive */
		/*OK if successful */
		struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
		pmlmepriv->passive_mode = 0;
		sprintf(ext, "OK");
	} else if (0 == strncmp(ext, "DCE-E", 5)) {
		/*Set scan type to passive */
		/*OK if successful */
		r8712_disconnectCtrlEx_cmd(padapter
			, 1 /*u32 enableDrvCtrl */
			, 5 /*u32 tryPktCnt */
			, 100 /*u32 tryPktInterval */
			, 5000 /*u32 firstStageTO */
		);
		sprintf(ext, "OK");
	} else if (0 == strncmp(ext, "DCE-D", 5)) {
		/*Set scan type to passive */
		/*OK if successfu */
		r8712_disconnectCtrlEx_cmd(padapter
			, 0 /*u32 enableDrvCtrl */
			, 5 /*u32 tryPktCnt */
			, 100 /*u32 tryPktInterval */
			, 5000 /*u32 firstStageTO */
		);
		sprintf(ext, "OK");
	} else {
		printk(KERN_INFO "r8712u: r871x_wx_set_priv: unknown Command"
		       " %s.\n", ext);
		goto FREE_EXT;
	}
	if (copy_to_user(dwrq->pointer, ext,
				min(dwrq->length, (__u16)(strlen(ext)+1))))
		ret = -EFAULT;

FREE_EXT:
	kfree(ext);
	return ret;
}

/* set bssid flow
 * s1. set_802_11_infrastructure_mode()
 * s2. set_802_11_authentication_mode()
 * s3. set_802_11_encryption_mode()
 * s4. set_802_11_bssid()
 *
 * This function intends to handle the Set AP command, which specifies the
 * MAC# of a preferred Access Point.
 * Currently, the request comes via Wireless Extensions' SIOCSIWAP ioctl.
 *
 * For this operation to succeed, there is no need for the interface to be up.
 *
 */
static int r8711_wx_set_wap(struct net_device *dev,
			 struct iw_request_info *info,
			 union iwreq_data *awrq,
			 char *extra)
{
	int ret = -EINPROGRESS;
	struct _adapter *padapter = (struct _adapter *) netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct  __queue *queue = &pmlmepriv->scanned_queue;
	struct sockaddr *temp = (struct sockaddr *)awrq;
	unsigned long irqL;
	struct list_head *phead;
	u8 *dst_bssid;
	struct wlan_network *pnetwork = NULL;
	enum NDIS_802_11_AUTHENTICATION_MODE	authmode;

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == true)
		return -EBUSY;
	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == true)
		return ret;
	if (temp->sa_family != ARPHRD_ETHER)
		return -EINVAL;
	authmode = padapter->securitypriv.ndisauthtype;
	spin_lock_irqsave(&queue->lock, irqL);
	phead = get_list_head(queue);
	pmlmepriv->pscanned = get_next(phead);
	while (1) {
		if (end_of_queue_search(phead, pmlmepriv->pscanned) == true)
			break;
		pnetwork = LIST_CONTAINOR(pmlmepriv->pscanned,
			   struct wlan_network, list);
		pmlmepriv->pscanned = get_next(pmlmepriv->pscanned);
		dst_bssid = pnetwork->network.MacAddress;
		if (!memcmp(dst_bssid, temp->sa_data, ETH_ALEN)) {
			r8712_set_802_11_infrastructure_mode(padapter,
			    pnetwork->network.InfrastructureMode);
			break;
		}
	}
	spin_unlock_irqrestore(&queue->lock, irqL);
	if (!ret) {
		if (!r8712_set_802_11_authentication_mode(padapter, authmode))
			ret = -ENOMEM;
		else {
			if (!r8712_set_802_11_bssid(padapter, temp->sa_data))
				ret = -1;
		}
	}
	return ret;
}

static int r8711_wx_get_wap(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ndis_wlan_bssid_ex *pcur_bss = &pmlmepriv->cur_network.network;

	wrqu->ap_addr.sa_family = ARPHRD_ETHER;
	memset(wrqu->ap_addr.sa_data, 0, ETH_ALEN);
	if (check_fwstate(pmlmepriv, _FW_LINKED |
	    WIFI_ADHOC_MASTER_STATE|WIFI_AP_STATE)) {
		memcpy(wrqu->ap_addr.sa_data, pcur_bss->MacAddress, ETH_ALEN);
	}
	return 0;
}

static int r871x_wx_set_mlme(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	u16 reason;
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	struct iw_mlme *mlme = (struct iw_mlme *) extra;

	if (mlme == NULL)
		return -1;
	reason = cpu_to_le16(mlme->reason_code);
	switch (mlme->cmd) {
	case IW_MLME_DEAUTH:
		if (!r8712_set_802_11_disassociate(padapter))
			ret = -1;
		break;
	case IW_MLME_DISASSOC:
		if (!r8712_set_802_11_disassociate(padapter))
			ret = -1;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return ret;
}

/**
 *
 * This function intends to handle the Set Scan command.
 * Currently, the request comes via Wireless Extensions' SIOCSIWSCAN ioctl.
 *
 * For this operation to succeed, the interface is brought Up beforehand.
 *
 */
static int r8711_wx_set_scan(struct net_device *dev,
			struct iw_request_info *a,
			union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 status = true;

	if (padapter->bDriverStopped == true) {
		printk(KERN_WARNING "r8712u: in r8711_wx_set_scan: "
		    "bDriverStopped=%d\n", padapter->bDriverStopped);
		return -1;
	}
	if (padapter->bup == false)
		return -ENETDOWN;
	if (padapter->hw_init_completed == false)
		return -1;
	if ((check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING)) ||
	    (pmlmepriv->sitesurveyctrl.traffic_busy == true))
		return 0;
	if (wrqu->data.length == sizeof(struct iw_scan_req)) {
		struct iw_scan_req *req = (struct iw_scan_req *)extra;
		if (wrqu->data.flags & IW_SCAN_THIS_ESSID) {
			struct ndis_802_11_ssid ssid;
			unsigned long irqL;
			u32 len = (u32) min((u8)req->essid_len,
				  (u8)IW_ESSID_MAX_SIZE);
			memset((unsigned char *)&ssid, 0,
				 sizeof(struct ndis_802_11_ssid));
			memcpy(ssid.Ssid, req->essid, len);
			ssid.SsidLength = len;
			spin_lock_irqsave(&pmlmepriv->lock, irqL);
			if ((check_fwstate(pmlmepriv, _FW_UNDER_SURVEY |
			     _FW_UNDER_LINKING)) ||
			    (pmlmepriv->sitesurveyctrl.traffic_busy == true)) {
				if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING))
					status = false;
			} else
				status = r8712_sitesurvey_cmd(padapter, &ssid);
			spin_unlock_irqrestore(&pmlmepriv->lock, irqL);
		}
	} else
		status = r8712_set_802_11_bssid_list_scan(padapter);
	if (status == false)
		return -1;
	return 0;
}

static int r8711_wx_get_scan(struct net_device *dev,
				struct iw_request_info *a,
				union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct  __queue *queue = &pmlmepriv->scanned_queue;
	struct wlan_network *pnetwork = NULL;
	unsigned long irqL;
	struct list_head *plist, *phead;
	char *ev = extra;
	char *stop = ev + wrqu->data.length;
	u32 ret = 0, cnt = 0;

	if (padapter->bDriverStopped)
		return -EINVAL;
	while (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING)) {
		msleep(30);
		cnt++;
		if (cnt > 100)
			break;
	}
	spin_lock_irqsave(&queue->lock, irqL);
	phead = get_list_head(queue);
	plist = get_next(phead);
	while (1) {
		if (end_of_queue_search(phead, plist) == true)
			break;
		if ((stop - ev) < SCAN_ITEM_SIZE) {
			ret = -E2BIG;
			break;
		}
		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);
		ev = translate_scan(padapter, a, pnetwork, ev, stop);
		plist = get_next(plist);
	}
	spin_unlock_irqrestore(&queue->lock, irqL);
	wrqu->data.length = ev - extra;
	wrqu->data.flags = 0;
	return ret;
}

/* set ssid flow
 * s1. set_802_11_infrastructure_mode()
 * s2. set_802_11_authenticaion_mode()
 * s3. set_802_11_encryption_mode()
 * s4. set_802_11_ssid()
 *
 * This function intends to handle the Set ESSID command.
 * Currently, the request comes via the Wireless Extensions' SIOCSIWESSID ioctl.
 *
 * For this operation to succeed, there is no need for the interface to be Up.
 *
 */
static int r8711_wx_set_essid(struct net_device *dev,
				struct iw_request_info *a,
				union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct  __queue *queue = &pmlmepriv->scanned_queue;
	struct wlan_network *pnetwork = NULL;
	enum NDIS_802_11_AUTHENTICATION_MODE	authmode;
	struct ndis_802_11_ssid ndis_ssid;
	u8 *dst_ssid, *src_ssid;
	struct list_head *phead;
	u32 len;

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY))
		return -EBUSY;
	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING))
		return 0;
	if (wrqu->essid.length > IW_ESSID_MAX_SIZE)
		return -E2BIG;
	authmode = padapter->securitypriv.ndisauthtype;
	if (wrqu->essid.flags && wrqu->essid.length) {
		len = (wrqu->essid.length < IW_ESSID_MAX_SIZE) ?
		       wrqu->essid.length : IW_ESSID_MAX_SIZE;
		memset(&ndis_ssid, 0, sizeof(struct ndis_802_11_ssid));
		ndis_ssid.SsidLength = len;
		memcpy(ndis_ssid.Ssid, extra, len);
		src_ssid = ndis_ssid.Ssid;
		phead = get_list_head(queue);
		pmlmepriv->pscanned = get_next(phead);
		while (1) {
			if (end_of_queue_search(phead, pmlmepriv->pscanned))
				break;
			pnetwork = LIST_CONTAINOR(pmlmepriv->pscanned,
				   struct wlan_network, list);
			pmlmepriv->pscanned = get_next(pmlmepriv->pscanned);
			dst_ssid = pnetwork->network.Ssid.Ssid;
			if ((!memcmp(dst_ssid, src_ssid, ndis_ssid.SsidLength))
			    && (pnetwork->network.Ssid.SsidLength ==
			     ndis_ssid.SsidLength)) {
				if (check_fwstate(pmlmepriv,
							WIFI_ADHOC_STATE)) {
					if (pnetwork->network.
						InfrastructureMode
						!=
						padapter->mlmepriv.
						cur_network.network.
						InfrastructureMode)
						continue;
				}

				r8712_set_802_11_infrastructure_mode(
				     padapter,
				     pnetwork->network.InfrastructureMode);
				break;
			}
		}
		r8712_set_802_11_authentication_mode(padapter, authmode);
		r8712_set_802_11_ssid(padapter, &ndis_ssid);
	}
	return -EINPROGRESS;
}

static int r8711_wx_get_essid(struct net_device *dev,
				struct iw_request_info *a,
				union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ndis_wlan_bssid_ex *pcur_bss = &pmlmepriv->cur_network.network;
	u32 len, ret = 0;

	if (check_fwstate(pmlmepriv, _FW_LINKED|WIFI_ADHOC_MASTER_STATE)) {
		len = pcur_bss->Ssid.SsidLength;
		wrqu->essid.length = len;
		memcpy(extra, pcur_bss->Ssid.Ssid, len);
		wrqu->essid.flags = 1;
	} else {
		ret = -ENOLINK;
	}
	return ret;
}

static int r8711_wx_set_rate(struct net_device *dev,
				struct iw_request_info *a,
				union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	u32 target_rate = wrqu->bitrate.value;
	u32 fixed = wrqu->bitrate.fixed;
	u32 ratevalue = 0;
	u8 datarates[NumRates];
	u8 mpdatarate[NumRates] = {11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0xff};
	int i, ret = 0;

	if (target_rate == -1) {
		ratevalue = 11;
		goto set_rate;
	}
	target_rate = target_rate / 100000;
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
		} else
			datarates[i] = 0xff;
	}
	if (r8712_setdatarate_cmd(padapter, datarates) != _SUCCESS)
		ret = -ENOMEM;
	return ret;
}

static int r8711_wx_get_rate(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct ndis_wlan_bssid_ex *pcur_bss = &pmlmepriv->cur_network.network;
	struct ieee80211_ht_cap *pht_capie;
	unsigned char rf_type = padapter->registrypriv.rf_config;
	int i;
	u8 *p;
	u16 rate, max_rate = 0, ht_cap = false;
	u32 ht_ielen = 0;
	u8 bw_40MHz = 0, short_GI = 0;
	u16 mcs_rate = 0;

	i = 0;
	if (check_fwstate(pmlmepriv, _FW_LINKED|WIFI_ADHOC_MASTER_STATE)) {
		p = r8712_get_ie(&pcur_bss->IEs[12],
				 _HT_CAPABILITY_IE_, &ht_ielen,
		    pcur_bss->IELength - 12);
		if (p && ht_ielen > 0) {
			ht_cap = true;
			pht_capie = (struct ieee80211_ht_cap *)(p + 2);
			memcpy(&mcs_rate , pht_capie->supp_mcs_set, 2);
			bw_40MHz = (pht_capie->cap_info &
				    IEEE80211_HT_CAP_SUP_WIDTH) ? 1 : 0;
			short_GI = (pht_capie->cap_info &
				    (IEEE80211_HT_CAP_SGI_20 |
				    IEEE80211_HT_CAP_SGI_40)) ? 1 : 0;
		}
		while ((pcur_bss->SupportedRates[i] != 0) &&
			(pcur_bss->SupportedRates[i] != 0xFF)) {
			rate = pcur_bss->SupportedRates[i] & 0x7F;
			if (rate > max_rate)
				max_rate = rate;
			wrqu->bitrate.fixed = 0;	/* no auto select */
			wrqu->bitrate.value = rate*500000;
			i++;
		}
		if (ht_cap == true) {
			if (mcs_rate & 0x8000 /* MCS15 */
				&&
				RTL8712_RF_2T2R == rf_type)
				max_rate = (bw_40MHz) ? ((short_GI) ? 300 :
					    270) : ((short_GI) ? 144 : 130);
			else if (mcs_rate & 0x0080) /* MCS7 */
				max_rate = (bw_40MHz) ? ((short_GI) ? 150 :
					    135) : ((short_GI) ? 72 : 65);
			else /* default MCS7 */
				max_rate = (bw_40MHz) ? ((short_GI) ? 150 :
					    135) : ((short_GI) ? 72 : 65);
			max_rate *= 2; /* Mbps/2 */
			wrqu->bitrate.value = max_rate * 500000;
		} else {
			wrqu->bitrate.value = max_rate * 500000;
		}
	} else
		return -ENOLINK;
	return 0;
}

static int r8711_wx_get_rts(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);

	wrqu->rts.value = padapter->registrypriv.rts_thresh;
	wrqu->rts.fixed = 0;	/* no auto select */
	return 0;
}

static int r8711_wx_set_frag(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);

	if (wrqu->frag.disabled)
		padapter->xmitpriv.frag_len = MAX_FRAG_THRESHOLD;
	else {
		if (wrqu->frag.value < MIN_FRAG_THRESHOLD ||
		    wrqu->frag.value > MAX_FRAG_THRESHOLD)
			return -EINVAL;
		padapter->xmitpriv.frag_len = wrqu->frag.value & ~0x1;
	}
	return 0;
}

static int r8711_wx_get_frag(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *) netdev_priv(dev);

	wrqu->frag.value = padapter->xmitpriv.frag_len;
	wrqu->frag.fixed = 0;	/* no auto select */
	return 0;
}

static int r8711_wx_get_retry(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	wrqu->retry.value = 7;
	wrqu->retry.fixed = 0;	/* no auto select */
	wrqu->retry.disabled = 1;
	return 0;
}

static int r8711_wx_set_enc(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *keybuf)
{
	u32 key;
	u32 keyindex_provided;
	struct NDIS_802_11_WEP	 wep;
	enum NDIS_802_11_AUTHENTICATION_MODE authmode;
	struct iw_point *erq = &(wrqu->encoding);
	struct _adapter *padapter = (struct _adapter *) netdev_priv(dev);

	key = erq->flags & IW_ENCODE_INDEX;
	memset(&wep, 0, sizeof(struct NDIS_802_11_WEP));
	if (erq->flags & IW_ENCODE_DISABLED) {
		printk(KERN_INFO "r8712u: r8711_wx_set_enc: "
		       "EncryptionDisabled\n");
		padapter->securitypriv.ndisencryptstatus =
				 Ndis802_11EncryptionDisabled;
		padapter->securitypriv.PrivacyAlgrthm = _NO_PRIVACY_;
		padapter->securitypriv.XGrpPrivacy = _NO_PRIVACY_;
		padapter->securitypriv.AuthAlgrthm = 0; /* open system */
		authmode = Ndis802_11AuthModeOpen;
		padapter->securitypriv.ndisauthtype = authmode;
		return 0;
	}
	if (key) {
		if (key > WEP_KEYS)
			return -EINVAL;
		key--;
		keyindex_provided = 1;
	} else {
		keyindex_provided = 0;
		key = padapter->securitypriv.PrivacyKeyIndex;
	}
	/* set authentication mode */
	if (erq->flags & IW_ENCODE_OPEN) {
		printk(KERN_INFO "r8712u: r8711_wx_set_enc: "
		       "IW_ENCODE_OPEN\n");
		padapter->securitypriv.ndisencryptstatus =
				 Ndis802_11Encryption1Enabled;
		padapter->securitypriv.AuthAlgrthm = 0; /* open system */
		padapter->securitypriv.PrivacyAlgrthm = _NO_PRIVACY_;
		padapter->securitypriv.XGrpPrivacy = _NO_PRIVACY_;
		authmode = Ndis802_11AuthModeOpen;
		padapter->securitypriv.ndisauthtype = authmode;
	} else if (erq->flags & IW_ENCODE_RESTRICTED) {
		printk(KERN_INFO "r8712u: r8711_wx_set_enc: "
		       "IW_ENCODE_RESTRICTED\n");
		padapter->securitypriv.ndisencryptstatus =
				 Ndis802_11Encryption1Enabled;
		padapter->securitypriv.AuthAlgrthm = 1; /* shared system */
		padapter->securitypriv.PrivacyAlgrthm = _WEP40_;
		padapter->securitypriv.XGrpPrivacy = _WEP40_;
		authmode = Ndis802_11AuthModeShared;
		padapter->securitypriv.ndisauthtype = authmode;
	} else {
		padapter->securitypriv.ndisencryptstatus =
				 Ndis802_11Encryption1Enabled;
		padapter->securitypriv.AuthAlgrthm = 0; /* open system */
		padapter->securitypriv.PrivacyAlgrthm = _NO_PRIVACY_;
		padapter->securitypriv.XGrpPrivacy = _NO_PRIVACY_;
		authmode = Ndis802_11AuthModeOpen;
		padapter->securitypriv.ndisauthtype = authmode;
	}
	wep.KeyIndex = key;
	if (erq->length > 0) {
		wep.KeyLength = erq->length <= 5 ? 5 : 13;
		wep.Length = wep.KeyLength +
			     FIELD_OFFSET(struct NDIS_802_11_WEP, KeyMaterial);
	} else {
		wep.KeyLength = 0 ;
		if (keyindex_provided == 1) { /* set key_id only, no given
					       * KeyMaterial(erq->length==0).*/
			padapter->securitypriv.PrivacyKeyIndex = key;
			switch (padapter->securitypriv.DefKeylen[key]) {
			case 5:
				padapter->securitypriv.PrivacyAlgrthm =
						 _WEP40_;
				break;
			case 13:
				padapter->securitypriv.PrivacyAlgrthm =
						 _WEP104_;
				break;
			default:
				padapter->securitypriv.PrivacyAlgrthm =
						 _NO_PRIVACY_;
				break;
			}
			return 0;
		}
	}
	wep.KeyIndex |= 0x80000000;	/* transmit key */
	memcpy(wep.KeyMaterial, keybuf, wep.KeyLength);
	if (r8712_set_802_11_add_wep(padapter, &wep) == _FAIL)
		return -EOPNOTSUPP;
	return 0;
}

static int r8711_wx_get_enc(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *keybuf)
{
	uint key, ret = 0;
	struct _adapter *padapter = (struct _adapter *) netdev_priv(dev);
	struct iw_point *erq = &(wrqu->encoding);
	struct	mlme_priv	*pmlmepriv = &(padapter->mlmepriv);

	if (check_fwstate(pmlmepriv, _FW_LINKED) == false) {
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
		key = padapter->securitypriv.PrivacyKeyIndex;
	}
	erq->flags = key + 1;
	switch (padapter->securitypriv.ndisencryptstatus) {
	case Ndis802_11EncryptionNotSupported:
	case Ndis802_11EncryptionDisabled:
		erq->length = 0;
		erq->flags |= IW_ENCODE_DISABLED;
		break;
	case Ndis802_11Encryption1Enabled:
		erq->length = padapter->securitypriv.DefKeylen[key];
		if (erq->length) {
			memcpy(keybuf, padapter->securitypriv.DefKey[
				key].skey, padapter->securitypriv.
				DefKeylen[key]);
			erq->flags |= IW_ENCODE_ENABLED;
			if (padapter->securitypriv.ndisauthtype ==
			    Ndis802_11AuthModeOpen)
				erq->flags |= IW_ENCODE_OPEN;
			else if (padapter->securitypriv.ndisauthtype ==
				 Ndis802_11AuthModeShared)
				erq->flags |= IW_ENCODE_RESTRICTED;
		} else {
			erq->length = 0;
			erq->flags |= IW_ENCODE_DISABLED;
		}
		break;
	case Ndis802_11Encryption2Enabled:
	case Ndis802_11Encryption3Enabled:
		erq->length = 16;
		erq->flags |= (IW_ENCODE_ENABLED | IW_ENCODE_OPEN |
			       IW_ENCODE_NOKEY);
		break;
	default:
		erq->length = 0;
		erq->flags |= IW_ENCODE_DISABLED;
		break;
	}
	return ret;
}

static int r8711_wx_get_power(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	wrqu->power.value = 0;
	wrqu->power.fixed = 0;	/* no auto select */
	wrqu->power.disabled = 1;
	return 0;
}

static int r871x_wx_set_gen_ie(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);

	return r871x_set_wpa_ie(padapter, extra, wrqu->data.length);
}

static int r871x_wx_set_auth(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	struct iw_param *param = (struct iw_param *)&(wrqu->param);
	int paramid;
	int paramval;
	int ret = 0;

	paramid = param->flags & IW_AUTH_INDEX;
	paramval = param->value;
	switch (paramid) {
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
		if (paramval) {
			/* wpa_supplicant is enabling tkip countermeasure. */
			padapter->securitypriv.btkip_countermeasure = true;
		} else {
			/* wpa_supplicant is disabling tkip countermeasure. */
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
		if (padapter->securitypriv.ndisencryptstatus ==
		    Ndis802_11Encryption1Enabled) {
				/* it means init value, or using wep,
				 * ndisencryptstatus =
				 *	Ndis802_11Encryption1Enabled,
				 * then it needn't reset it;
				 */
				break;
		}

		if (paramval) {
			padapter->securitypriv.ndisencryptstatus =
				   Ndis802_11EncryptionDisabled;
			padapter->securitypriv.PrivacyAlgrthm =
				  _NO_PRIVACY_;
			padapter->securitypriv.XGrpPrivacy =
				  _NO_PRIVACY_;
			padapter->securitypriv.AuthAlgrthm = 0;
			padapter->securitypriv.ndisauthtype =
				  Ndis802_11AuthModeOpen;
		}
		break;
	case IW_AUTH_80211_AUTH_ALG:
		ret = wpa_set_auth_algs(dev, (u32)paramval);
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

static int r871x_wx_set_enc_ext(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	struct iw_point *pencoding = &wrqu->encoding;
	struct iw_encode_ext *pext = (struct iw_encode_ext *)extra;
	struct ieee_param *param = NULL;
	char *alg_name;
	u32 param_len;
	int ret = 0;

	param_len = sizeof(struct ieee_param) + pext->key_len;
	param = (struct ieee_param *)_malloc(param_len);
	if (param == NULL)
		return -ENOMEM;
	memset(param, 0, param_len);
	param->cmd = IEEE_CMD_SET_ENCRYPTION;
	memset(param->sta_addr, 0xff, ETH_ALEN);
	switch (pext->alg) {
	case IW_ENCODE_ALG_NONE:
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
		return -EINVAL;
	}
	strncpy((char *)param->u.crypt.alg, alg_name, IEEE_CRYPT_ALG_NAME_LEN);
	if (pext->ext_flags & IW_ENCODE_EXT_GROUP_KEY)
		param->u.crypt.set_tx = 0;
	if (pext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY)
		param->u.crypt.set_tx = 1;
	param->u.crypt.idx = (pencoding->flags & 0x00FF) - 1;
	if (pext->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID)
		memcpy(param->u.crypt.seq, pext->rx_seq, 8);
	if (pext->key_len) {
		param->u.crypt.key_len = pext->key_len;
		memcpy(param + 1, pext + 1, pext->key_len);
	}
	ret = wpa_set_encryption(dev, param, param_len);
	kfree(param);
	return ret;
}

static int r871x_wx_get_nick(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *extra)
{
	if (extra) {
		wrqu->data.length = 8;
		wrqu->data.flags = 1;
		memcpy(extra, "rtl_wifi", 8);
	}
	return 0;
}

static int r8711_wx_read32(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *keybuf)
{
	struct _adapter *padapter = (struct _adapter *) netdev_priv(dev);
	u32 addr;
	u32 data32;

	get_user(addr, (u32 __user *)wrqu->data.pointer);
	data32 = r8712_read32(padapter, addr);
	put_user(data32, (u32 __user *)wrqu->data.pointer);
	wrqu->data.length = (data32 & 0xffff0000) >> 16;
	wrqu->data.flags = data32 & 0xffff;
	get_user(addr, (u32 __user *)wrqu->data.pointer);
	return 0;
}

static int r8711_wx_write32(struct net_device *dev,
				 struct iw_request_info *info,
				 union iwreq_data *wrqu, char *keybuf)
{
	struct _adapter *padapter = (struct _adapter *) netdev_priv(dev);
	u32 addr;
	u32 data32;

	get_user(addr, (u32 __user *)wrqu->data.pointer);
	data32 = ((u32)wrqu->data.length<<16) | (u32)wrqu->data.flags ;
	r8712_write32(padapter, addr, data32);
	return 0;
}

static int dummy(struct net_device *dev,
		struct iw_request_info *a,
		union iwreq_data *wrqu, char *b)
{
	return -ENOSYS;
}

static int r8711_drvext_hdl(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	return 0;
}

static int r871x_mp_ioctl_hdl(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	struct iw_point *p = &wrqu->data;
	struct oid_par_priv oid_par;
	struct mp_ioctl_handler *phandler;
	struct mp_ioctl_param *poidparam;
	unsigned long BytesRead, BytesWritten, BytesNeeded;
	u8 *pparmbuf = NULL, bset;
	u16 len;
	uint status;
	int ret = 0;

	if ((!p->length) || (!p->pointer)) {
		ret = -EINVAL;
		goto _r871x_mp_ioctl_hdl_exit;
	}
	bset = (u8)(p->flags & 0xFFFF);
	len = p->length;
	pparmbuf = NULL;
	pparmbuf = (u8 *)_malloc(len);
	if (pparmbuf == NULL) {
		ret = -ENOMEM;
		goto _r871x_mp_ioctl_hdl_exit;
	}
	if (copy_from_user(pparmbuf, p->pointer, len)) {
		ret = -EFAULT;
		goto _r871x_mp_ioctl_hdl_exit;
	}
	poidparam = (struct mp_ioctl_param *)pparmbuf;
	if (poidparam->subcode >= MAX_MP_IOCTL_SUBCODE) {
		ret = -EINVAL;
		goto _r871x_mp_ioctl_hdl_exit;
	}
	phandler = mp_ioctl_hdl + poidparam->subcode;
	if ((phandler->paramsize != 0) &&
	    (poidparam->len < phandler->paramsize)) {
		ret = -EINVAL;
		goto _r871x_mp_ioctl_hdl_exit;
	}
	if (phandler->oid == 0 && phandler->handler)
		status = phandler->handler(&oid_par);
	else if (phandler->handler) {
		oid_par.adapter_context = padapter;
		oid_par.oid = phandler->oid;
		oid_par.information_buf = poidparam->data;
		oid_par.information_buf_len = poidparam->len;
		oid_par.dbg = 0;
		BytesWritten = 0;
		BytesNeeded = 0;
		if (bset) {
			oid_par.bytes_rw = &BytesRead;
			oid_par.bytes_needed = &BytesNeeded;
			oid_par.type_of_oid = SET_OID;
		} else {
			oid_par.bytes_rw = &BytesWritten;
			oid_par.bytes_needed = &BytesNeeded;
			oid_par.type_of_oid = QUERY_OID;
		}
		status = phandler->handler(&oid_par);
		/* todo:check status, BytesNeeded, etc. */
	} else {
		printk(KERN_INFO "r8712u: r871x_mp_ioctl_hdl(): err!,"
		    " subcode=%d, oid=%d, handler=%p\n",
		    poidparam->subcode, phandler->oid, phandler->handler);
		ret = -EFAULT;
		goto _r871x_mp_ioctl_hdl_exit;
	}
	if (bset == 0x00) { /* query info */
		if (copy_to_user(p->pointer, pparmbuf, len))
			ret = -EFAULT;
	}
	if (status) {
		ret = -EFAULT;
		goto _r871x_mp_ioctl_hdl_exit;
	}
_r871x_mp_ioctl_hdl_exit:
	kfree(pparmbuf);
	return ret;
}

static int r871x_get_ap_info(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct  __queue *queue = &pmlmepriv->scanned_queue;
	struct iw_point *pdata = &wrqu->data;
	struct wlan_network *pnetwork = NULL;
	u32 cnt = 0, wpa_ielen;
	unsigned long irqL;
	struct list_head *plist, *phead;
	unsigned char *pbuf;
	u8 bssid[ETH_ALEN];
	char data[32];

	if (padapter->bDriverStopped || (pdata == NULL))
		return -EINVAL;
	while (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING)) {
		msleep(30);
		cnt++;
		if (cnt > 100)
			break;
	}
	pdata->flags = 0;
	if (pdata->length >= 32) {
		if (copy_from_user(data, pdata->pointer, 32))
			return -EINVAL;
	} else
		 return -EINVAL;
	spin_lock_irqsave(&(pmlmepriv->scanned_queue.lock), irqL);
	phead = get_list_head(queue);
	plist = get_next(phead);
	while (1) {
		if (end_of_queue_search(phead, plist) == true)
			break;
		pnetwork = LIST_CONTAINOR(plist, struct wlan_network, list);
		if (hwaddr_aton_i(data, bssid)) {
			printk(KERN_INFO "r8712u: Invalid BSSID '%s'.\n",
			       (u8 *)data);
			spin_unlock_irqrestore(&(pmlmepriv->scanned_queue.lock),
									irqL);
			return -EINVAL;
		}
		printk(KERN_INFO "r8712u: BSSID:%pM\n", bssid);
		if (!memcmp(bssid, pnetwork->network.MacAddress, ETH_ALEN)) {
			/* BSSID match, then check if supporting wpa/wpa2 */
			pbuf = r8712_get_wpa_ie(&pnetwork->network.IEs[12],
			       &wpa_ielen, pnetwork->network.IELength-12);
			if (pbuf && (wpa_ielen > 0)) {
				pdata->flags = 1;
				break;
			}
			pbuf = r8712_get_wpa2_ie(&pnetwork->network.IEs[12],
			       &wpa_ielen, pnetwork->network.IELength-12);
			if (pbuf && (wpa_ielen > 0)) {
				pdata->flags = 2;
				break;
			}
		}
		plist = get_next(plist);
	}
	spin_unlock_irqrestore(&(pmlmepriv->scanned_queue.lock), irqL);
	if (pdata->length >= 34) {
		if (copy_to_user((u8 __user *)pdata->pointer + 32,
		    (u8 *)&pdata->flags, 1))
			return -EINVAL;
	}
	return 0;
}

static int r871x_set_pid(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *) netdev_priv(dev);
	struct iw_point *pdata = &wrqu->data;

	if ((padapter->bDriverStopped) || (pdata == NULL))
		return -EINVAL;
	if (copy_from_user(&padapter->pid, pdata->pointer, sizeof(int)))
		return -EINVAL;
	return 0;
}

static int r871x_set_chplan(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	int ret = 0;
	struct _adapter *padapter = (struct _adapter *) netdev_priv(dev);
	struct iw_point *pdata = &wrqu->data;
	int ch_plan = -1;

	if ((padapter->bDriverStopped) || (pdata == NULL)) {
		ret = -EINVAL;
		goto exit;
	}
	ch_plan = (int)*extra;
	r8712_set_chplan_cmd(padapter, ch_plan);

exit:

	return ret;
}

static int r871x_wps_start(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct _adapter *padapter = (struct _adapter *)netdev_priv(dev);
	struct iw_point *pdata = &wrqu->data;
	u32   u32wps_start = 0;

	if ((padapter->bDriverStopped) || (pdata == NULL))
		return -EINVAL;
	if (copy_from_user((void *)&u32wps_start, pdata->pointer, 4))
		return -EFAULT;
	if (u32wps_start == 0)
		u32wps_start = *extra;
	if (u32wps_start == 1) /* WPS Start */
		padapter->ledpriv.LedControlHandler(padapter,
			   LED_CTL_START_WPS);
	else if (u32wps_start == 2) /* WPS Stop because of wps success */
		padapter->ledpriv.LedControlHandler(padapter,
			   LED_CTL_STOP_WPS);
	else if (u32wps_start == 3) /* WPS Stop because of wps fail */
		padapter->ledpriv.LedControlHandler(padapter,
			   LED_CTL_STOP_WPS_FAIL);
	return 0;
}

static int wpa_set_param(struct net_device *dev, u8 name, u32 value)
{
	struct _adapter *padapter = (struct _adapter *) netdev_priv(dev);

	switch (name) {
	case IEEE_PARAM_WPA_ENABLED:
		padapter->securitypriv.AuthAlgrthm = 2; /* 802.1x */
		switch ((value)&0xff) {
		case 1: /* WPA */
			padapter->securitypriv.ndisauthtype =
				Ndis802_11AuthModeWPAPSK; /* WPA_PSK */
			padapter->securitypriv.ndisencryptstatus =
				Ndis802_11Encryption2Enabled;
			break;
		case 2: /* WPA2 */
			padapter->securitypriv.ndisauthtype =
				Ndis802_11AuthModeWPA2PSK; /* WPA2_PSK */
			padapter->securitypriv.ndisencryptstatus =
				Ndis802_11Encryption3Enabled;
			break;
		}
		break;
	case IEEE_PARAM_TKIP_COUNTERMEASURES:
		break;
	case IEEE_PARAM_DROP_UNENCRYPTED:
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
	case IEEE_PARAM_PRIVACY_INVOKED:
		break;
	case IEEE_PARAM_AUTH_ALGS:
		return wpa_set_auth_algs(dev, value);
		break;
	case IEEE_PARAM_IEEE_802_1X:
		break;
	case IEEE_PARAM_WPAX_SELECT:
		/* added for WPA2 mixed mode */
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int wpa_mlme(struct net_device *dev, u32 command, u32 reason)
{
	struct _adapter *padapter = (struct _adapter *) netdev_priv(dev);

	switch (command) {
	case IEEE_MLME_STA_DEAUTH:
		if (!r8712_set_802_11_disassociate(padapter))
			return -1;
		break;
	case IEEE_MLME_STA_DISASSOC:
		if (!r8712_set_802_11_disassociate(padapter))
			return -1;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int wpa_supplicant_ioctl(struct net_device *dev, struct iw_point *p)
{
	struct ieee_param *param;
	int ret = 0;
	struct _adapter *padapter = (struct _adapter *) netdev_priv(dev);

	if (p->length < sizeof(struct ieee_param) || !p->pointer)
		return -EINVAL;
	param = (struct ieee_param *)_malloc(p->length);
	if (param == NULL)
		return -ENOMEM;
	if (copy_from_user(param, p->pointer, p->length)) {
		kfree((u8 *)param);
		return -EFAULT;
	}
	switch (param->cmd) {
	case IEEE_CMD_SET_WPA_PARAM:
		ret = wpa_set_param(dev, param->u.wpa_param.name,
		      param->u.wpa_param.value);
		break;
	case IEEE_CMD_SET_WPA_IE:
		ret =  r871x_set_wpa_ie(padapter, (char *)param->u.wpa_ie.data,
		       (u16)param->u.wpa_ie.len);
		break;
	case IEEE_CMD_SET_ENCRYPTION:
		ret = wpa_set_encryption(dev, param, p->length);
		break;
	case IEEE_CMD_MLME:
		ret = wpa_mlme(dev, param->u.mlme.command,
		      param->u.mlme.reason_code);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	if (ret == 0 && copy_to_user(p->pointer, param, p->length))
		ret = -EFAULT;
	kfree((u8 *)param);
	return ret;
}

/* based on "driver_ipw" and for hostapd */
int r871x_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct iwreq *wrq = (struct iwreq *)rq;

	switch (cmd) {
	case RTL_IOCTL_WPA_SUPPLICANT:
		return wpa_supplicant_ioctl(dev, &wrq->u.data);
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static iw_handler r8711_handlers[] = {
	NULL,				/* SIOCSIWCOMMIT */
	r8711_wx_get_name,		/* SIOCGIWNAME */
	dummy,				/* SIOCSIWNWID */
	dummy,				/* SIOCGIWNWID */
	r8711_wx_set_freq,		/* SIOCSIWFREQ */
	r8711_wx_get_freq,		/* SIOCGIWFREQ */
	r8711_wx_set_mode,		/* SIOCSIWMODE */
	r8711_wx_get_mode,		/* SIOCGIWMODE */
	dummy,				/* SIOCSIWSENS */
	r8711_wx_get_sens,		/* SIOCGIWSENS */
	NULL,				/* SIOCSIWRANGE */
	r8711_wx_get_range,		/* SIOCGIWRANGE */
	r871x_wx_set_priv,		/* SIOCSIWPRIV */
	NULL,				/* SIOCGIWPRIV */
	NULL,				/* SIOCSIWSTATS */
	NULL,				/* SIOCGIWSTATS */
	dummy,				/* SIOCSIWSPY */
	dummy,				/* SIOCGIWSPY */
	NULL,				/* SIOCGIWTHRSPY */
	NULL,				/* SIOCWIWTHRSPY */
	r8711_wx_set_wap,		/* SIOCSIWAP */
	r8711_wx_get_wap,		/* SIOCGIWAP */
	r871x_wx_set_mlme,		/* request MLME operation;
					 *  uses struct iw_mlme */
	dummy,				/* SIOCGIWAPLIST -- deprecated */
	r8711_wx_set_scan,		/* SIOCSIWSCAN */
	r8711_wx_get_scan,		/* SIOCGIWSCAN */
	r8711_wx_set_essid,		/* SIOCSIWESSID */
	r8711_wx_get_essid,		/* SIOCGIWESSID */
	dummy,				/* SIOCSIWNICKN */
	r871x_wx_get_nick,		/* SIOCGIWNICKN */
	NULL,				/* -- hole -- */
	NULL,				/* -- hole -- */
	r8711_wx_set_rate,		/* SIOCSIWRATE */
	r8711_wx_get_rate,		/* SIOCGIWRATE */
	dummy,				/* SIOCSIWRTS */
	r8711_wx_get_rts,		/* SIOCGIWRTS */
	r8711_wx_set_frag,		/* SIOCSIWFRAG */
	r8711_wx_get_frag,		/* SIOCGIWFRAG */
	dummy,				/* SIOCSIWTXPOW */
	dummy,				/* SIOCGIWTXPOW */
	dummy,				/* SIOCSIWRETRY */
	r8711_wx_get_retry,		/* SIOCGIWRETRY */
	r8711_wx_set_enc,		/* SIOCSIWENCODE */
	r8711_wx_get_enc,		/* SIOCGIWENCODE */
	dummy,				/* SIOCSIWPOWER */
	r8711_wx_get_power,		/* SIOCGIWPOWER */
	NULL,				/*---hole---*/
	NULL,				/*---hole---*/
	r871x_wx_set_gen_ie,		/* SIOCSIWGENIE */
	NULL,				/* SIOCGIWGENIE */
	r871x_wx_set_auth,		/* SIOCSIWAUTH */
	NULL,				/* SIOCGIWAUTH */
	r871x_wx_set_enc_ext,		/* SIOCSIWENCODEEXT */
	NULL,				/* SIOCGIWENCODEEXT */
	r871x_wx_set_pmkid,		/* SIOCSIWPMKSA */
	NULL,				/*---hole---*/
};

static const struct iw_priv_args r8711_private_args[] = {
	{
		SIOCIWFIRSTPRIV + 0x0,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "read32"
	},
	{
		SIOCIWFIRSTPRIV + 0x1,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "write32"
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
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "setpid"
	},
	{
		SIOCIWFIRSTPRIV + 0x6,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "wps_start"
	},
	{
		SIOCIWFIRSTPRIV + 0x7,
		IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "chplan"
	}
};

static iw_handler r8711_private_handler[] = {
	r8711_wx_read32,
	r8711_wx_write32,
	r8711_drvext_hdl,
	r871x_mp_ioctl_hdl,
	r871x_get_ap_info, /*for MM DTV platform*/
	r871x_set_pid,
	r871x_wps_start,
	r871x_set_chplan
};

static struct iw_statistics *r871x_get_wireless_stats(struct net_device *dev)
{
	struct _adapter *padapter = (struct _adapter *) netdev_priv(dev);
	struct iw_statistics *piwstats = &padapter->iwstats;
	int tmp_level = 0;
	int tmp_qual = 0;
	int tmp_noise = 0;

	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED) != true) {
		piwstats->qual.qual = 0;
		piwstats->qual.level = 0;
		piwstats->qual.noise = 0;
	} else {
		/* show percentage, we need transfer dbm to orignal value. */
		tmp_level = padapter->recvpriv.fw_rssi;
		tmp_qual = padapter->recvpriv.signal;
		tmp_noise = padapter->recvpriv.noise;
		piwstats->qual.level = tmp_level;
		piwstats->qual.qual = tmp_qual;
		piwstats->qual.noise = tmp_noise;
	}
	piwstats->qual.updated = IW_QUAL_ALL_UPDATED;
	return &padapter->iwstats;
}

struct iw_handler_def r871x_handlers_def = {
	.standard = r8711_handlers,
	.num_standard = ARRAY_SIZE(r8711_handlers),
	.private = r8711_private_handler,
	.private_args = (struct iw_priv_args *)r8711_private_args,
	.num_private = ARRAY_SIZE(r8711_private_handler),
	.num_private_args = sizeof(r8711_private_args) /
			    sizeof(struct iw_priv_args),
	.get_wireless_stats = r871x_get_wireless_stats
};
