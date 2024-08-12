// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include <linux/etherdevice.h>
#include <drv_types.h>
#include <rtw_debug.h>
#include <linux/jiffies.h>

#include <rtw_wifi_regd.h>

#define RTW_MAX_MGMT_TX_CNT (8)

#define RTW_SCAN_IE_LEN_MAX      2304
#define RTW_MAX_REMAIN_ON_CHANNEL_DURATION 5000 /* ms */
#define RTW_MAX_NUM_PMKIDS 4

static const u32 rtw_cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
	WLAN_CIPHER_SUITE_AES_CMAC,
};

#define RATETAB_ENT(_rate, _rateid, _flags) \
	{								\
		.bitrate	= (_rate),				\
		.hw_value	= (_rateid),				\
		.flags		= (_flags),				\
	}

#define CHAN2G(_channel, _freq, _flags) {			\
	.band			= NL80211_BAND_2GHZ,		\
	.center_freq		= (_freq),			\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

/* if wowlan is not supported, kernel generate a disconnect at each suspend
 * cf: /net/wireless/sysfs.c, so register a stub wowlan.
 * Moreover wowlan has to be enabled via a the nl80211_set_wowlan callback.
 * (from user space, e.g. iw phy0 wowlan enable)
 */
static __maybe_unused const struct wiphy_wowlan_support wowlan_stub = {
	.flags = WIPHY_WOWLAN_ANY,
	.n_patterns = 0,
	.pattern_max_len = 0,
	.pattern_min_len = 0,
	.max_pkt_offset = 0,
};

static struct ieee80211_rate rtw_rates[] = {
	RATETAB_ENT(10,  0x1,   0),
	RATETAB_ENT(20,  0x2,   0),
	RATETAB_ENT(55,  0x4,   0),
	RATETAB_ENT(110, 0x8,   0),
	RATETAB_ENT(60,  0x10,  0),
	RATETAB_ENT(90,  0x20,  0),
	RATETAB_ENT(120, 0x40,  0),
	RATETAB_ENT(180, 0x80,  0),
	RATETAB_ENT(240, 0x100, 0),
	RATETAB_ENT(360, 0x200, 0),
	RATETAB_ENT(480, 0x400, 0),
	RATETAB_ENT(540, 0x800, 0),
};

#define rtw_g_rates		(rtw_rates + 0)
#define RTW_G_RATES_NUM	12

#define RTW_2G_CHANNELS_NUM 14

static struct ieee80211_channel rtw_2ghz_channels[] = {
	CHAN2G(1, 2412, 0),
	CHAN2G(2, 2417, 0),
	CHAN2G(3, 2422, 0),
	CHAN2G(4, 2427, 0),
	CHAN2G(5, 2432, 0),
	CHAN2G(6, 2437, 0),
	CHAN2G(7, 2442, 0),
	CHAN2G(8, 2447, 0),
	CHAN2G(9, 2452, 0),
	CHAN2G(10, 2457, 0),
	CHAN2G(11, 2462, 0),
	CHAN2G(12, 2467, 0),
	CHAN2G(13, 2472, 0),
	CHAN2G(14, 2484, 0),
};

static void rtw_2g_channels_init(struct ieee80211_channel *channels)
{
	memcpy((void *)channels, (void *)rtw_2ghz_channels,
	       sizeof(struct ieee80211_channel) * RTW_2G_CHANNELS_NUM
	);
}

static void rtw_2g_rates_init(struct ieee80211_rate *rates)
{
	memcpy(rates, rtw_g_rates,
	       sizeof(struct ieee80211_rate) * RTW_G_RATES_NUM
	);
}

static struct ieee80211_supported_band *rtw_spt_band_alloc(
	enum nl80211_band band
	)
{
	struct ieee80211_supported_band *spt_band = NULL;
	int n_channels, n_bitrates;

	if (band == NL80211_BAND_2GHZ) {
		n_channels = RTW_2G_CHANNELS_NUM;
		n_bitrates = RTW_G_RATES_NUM;
	} else {
		goto exit;
	}

	spt_band = rtw_zmalloc(sizeof(struct ieee80211_supported_band) +
			       sizeof(struct ieee80211_channel) * n_channels +
			       sizeof(struct ieee80211_rate) * n_bitrates);
	if (!spt_band)
		goto exit;

	spt_band->channels = (struct ieee80211_channel *)(((u8 *)spt_band) + sizeof(struct ieee80211_supported_band));
	spt_band->bitrates = (struct ieee80211_rate *)(((u8 *)spt_band->channels) + sizeof(struct ieee80211_channel) * n_channels);
	spt_band->band = band;
	spt_band->n_channels = n_channels;
	spt_band->n_bitrates = n_bitrates;

	if (band == NL80211_BAND_2GHZ) {
		rtw_2g_channels_init(spt_band->channels);
		rtw_2g_rates_init(spt_band->bitrates);
	}

	/* spt_band.ht_cap */

exit:

	return spt_band;
}

static const struct ieee80211_txrx_stypes
rtw_cfg80211_default_mgmt_stypes[NUM_NL80211_IFTYPES] = {
	[NL80211_IFTYPE_ADHOC] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4)
	},
	[NL80211_IFTYPE_STATION] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
	[NL80211_IFTYPE_AP] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
		BIT(IEEE80211_STYPE_DISASSOC >> 4) |
		BIT(IEEE80211_STYPE_AUTH >> 4) |
		BIT(IEEE80211_STYPE_DEAUTH >> 4) |
		BIT(IEEE80211_STYPE_ACTION >> 4)
	},
	[NL80211_IFTYPE_AP_VLAN] = {
		/* copy AP */
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
		BIT(IEEE80211_STYPE_DISASSOC >> 4) |
		BIT(IEEE80211_STYPE_AUTH >> 4) |
		BIT(IEEE80211_STYPE_DEAUTH >> 4) |
		BIT(IEEE80211_STYPE_ACTION >> 4)
	},
	[NL80211_IFTYPE_P2P_CLIENT] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
	[NL80211_IFTYPE_P2P_GO] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
		BIT(IEEE80211_STYPE_DISASSOC >> 4) |
		BIT(IEEE80211_STYPE_AUTH >> 4) |
		BIT(IEEE80211_STYPE_DEAUTH >> 4) |
		BIT(IEEE80211_STYPE_ACTION >> 4)
	},
};

static int rtw_ieee80211_channel_to_frequency(int chan, int band)
{
	if (band == NL80211_BAND_2GHZ) {
		if (chan == 14)
			return 2484;
		else if (chan < 14)
			return 2407 + chan * 5;
	}

	return 0; /* not supported */
}

#define MAX_BSSINFO_LEN 1000
struct cfg80211_bss *rtw_cfg80211_inform_bss(struct adapter *padapter, struct wlan_network *pnetwork)
{
	struct ieee80211_channel *notify_channel;
	struct cfg80211_bss *bss = NULL;
	/* struct ieee80211_supported_band *band; */
	u16 channel;
	u32 freq;
	u64 notify_timestamp;
	s32 notify_signal;
	u8 *buf = NULL, *pbuf;
	size_t len, bssinf_len = 0;
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;

	struct wireless_dev *wdev = padapter->rtw_wdev;
	struct wiphy *wiphy = wdev->wiphy;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	bssinf_len = pnetwork->network.ie_length + sizeof(struct ieee80211_hdr_3addr);
	if (bssinf_len > MAX_BSSINFO_LEN)
		goto exit;

	{
		u16 wapi_len = 0;

		if (rtw_get_wapi_ie(pnetwork->network.ies, pnetwork->network.ie_length, NULL, &wapi_len) > 0) {
			if (wapi_len > 0)
				goto exit;
		}
	}

	/* To reduce PBC Overlap rate */
	/* spin_lock_bh(&pwdev_priv->scan_req_lock); */
	if (adapter_wdev_data(padapter)->scan_request) {
		u8 *psr = NULL, sr = 0;
		struct ndis_802_11_ssid *pssid = &pnetwork->network.ssid;
		struct cfg80211_scan_request *request = adapter_wdev_data(padapter)->scan_request;
		struct cfg80211_ssid *ssids = request->ssids;
		u32 wpsielen = 0;
		u8 *wpsie = NULL;

		wpsie = rtw_get_wps_ie(pnetwork->network.ies + _FIXED_IE_LENGTH_, pnetwork->network.ie_length - _FIXED_IE_LENGTH_, NULL, &wpsielen);

		if (wpsie && wpsielen > 0)
			psr = rtw_get_wps_attr_content(wpsie, wpsielen, WPS_ATTR_SELECTED_REGISTRAR, (u8 *)(&sr), NULL);

		if (sr != 0) {
			/* it means under processing WPS */
			if (request->n_ssids == 1 && request->n_channels == 1) {
				if (ssids[0].ssid_len != 0 &&
				    (pssid->ssid_length != ssids[0].ssid_len ||
				     memcmp(pssid->ssid, ssids[0].ssid, ssids[0].ssid_len))) {
					if (psr)
						*psr = 0; /* clear sr */
				}
			}
		}
	}
	/* spin_unlock_bh(&pwdev_priv->scan_req_lock); */

	channel = pnetwork->network.configuration.ds_config;
	freq = rtw_ieee80211_channel_to_frequency(channel, NL80211_BAND_2GHZ);

	notify_channel = ieee80211_get_channel(wiphy, freq);

	notify_timestamp = ktime_to_us(ktime_get_boottime());

	/* We've set wiphy's signal_type as CFG80211_SIGNAL_TYPE_MBM: signal strength in mBm (100*dBm) */
	if (check_fwstate(pmlmepriv, _FW_LINKED) == true &&
	    is_same_network(&pmlmepriv->cur_network.network, &pnetwork->network, 0)) {
		notify_signal = 100 * translate_percentage_to_dbm(padapter->recvpriv.signal_strength);/* dbm */
	} else {
		notify_signal = 100 * translate_percentage_to_dbm(pnetwork->network.phy_info.signal_strength);/* dbm */
	}

	buf = kzalloc(MAX_BSSINFO_LEN, GFP_ATOMIC);
	if (!buf)
		goto exit;
	pbuf = buf;

	pwlanhdr = (struct ieee80211_hdr *)pbuf;
	fctrl = &(pwlanhdr->frame_control);
	*(fctrl) = 0;

	SetSeqNum(pwlanhdr, 0/*pmlmeext->mgnt_seq*/);
	/* pmlmeext->mgnt_seq++; */

	if (pnetwork->network.reserved[0] == 1) { /*  WIFI_BEACON */
		eth_broadcast_addr(pwlanhdr->addr1);
		SetFrameSubType(pbuf, WIFI_BEACON);
	} else {
		memcpy(pwlanhdr->addr1, myid(&(padapter->eeprompriv)), ETH_ALEN);
		SetFrameSubType(pbuf, WIFI_PROBERSP);
	}

	memcpy(pwlanhdr->addr2, pnetwork->network.mac_address, ETH_ALEN);
	memcpy(pwlanhdr->addr3, pnetwork->network.mac_address, ETH_ALEN);

	pbuf += sizeof(struct ieee80211_hdr_3addr);
	len = sizeof(struct ieee80211_hdr_3addr);

	memcpy(pbuf, pnetwork->network.ies, pnetwork->network.ie_length);
	len += pnetwork->network.ie_length;

	*((__le64 *)pbuf) = cpu_to_le64(notify_timestamp);

	bss = cfg80211_inform_bss_frame(wiphy, notify_channel, (struct ieee80211_mgmt *)buf,
					len, notify_signal, GFP_ATOMIC);

	if (unlikely(!bss))
		goto exit;

	cfg80211_put_bss(wiphy, bss);
	kfree(buf);

exit:
	return bss;
}

/*
 *	Check the given bss is valid by kernel API cfg80211_get_bss()
 *	@padapter : the given adapter
 *
 *	return true if bss is valid,  false for not found.
 */
int rtw_cfg80211_check_bss(struct adapter *padapter)
{
	struct wlan_bssid_ex  *pnetwork = &(padapter->mlmeextpriv.mlmext_info.network);
	struct cfg80211_bss *bss = NULL;
	struct ieee80211_channel *notify_channel = NULL;
	u32 freq;

	if (!(pnetwork) || !(padapter->rtw_wdev))
		return false;

	freq = rtw_ieee80211_channel_to_frequency(pnetwork->configuration.ds_config, NL80211_BAND_2GHZ);

	notify_channel = ieee80211_get_channel(padapter->rtw_wdev->wiphy, freq);
	bss = cfg80211_get_bss(padapter->rtw_wdev->wiphy, notify_channel,
			       pnetwork->mac_address, pnetwork->ssid.ssid,
			       pnetwork->ssid.ssid_length,
			       IEEE80211_BSS_TYPE_ANY, IEEE80211_PRIVACY_ANY);

	cfg80211_put_bss(padapter->rtw_wdev->wiphy, bss);

	return	(bss != NULL);
}

void rtw_cfg80211_ibss_indicate_connect(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network  *cur_network = &(pmlmepriv->cur_network);
	struct wireless_dev *pwdev = padapter->rtw_wdev;
	struct wiphy *wiphy = pwdev->wiphy;
	int freq = (int)cur_network->network.configuration.ds_config;
	struct ieee80211_channel *chan;

	if (pwdev->iftype != NL80211_IFTYPE_ADHOC)
		return;

	if (!rtw_cfg80211_check_bss(padapter)) {
		struct wlan_bssid_ex  *pnetwork = &(padapter->mlmeextpriv.mlmext_info.network);
		struct wlan_network *scanned = pmlmepriv->cur_network_scanned;

		if (check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) {
			memcpy(&cur_network->network, pnetwork, sizeof(struct wlan_bssid_ex));
			rtw_cfg80211_inform_bss(padapter, cur_network);
		} else {
			if (!scanned) {
				rtw_warn_on(1);
				return;
			}
			if (!memcmp(&(scanned->network.ssid), &(pnetwork->ssid), sizeof(struct ndis_802_11_ssid))
				&& !memcmp(scanned->network.mac_address, pnetwork->mac_address, sizeof(NDIS_802_11_MAC_ADDRESS))
			)
				rtw_cfg80211_inform_bss(padapter, scanned);
			else
				rtw_warn_on(1);
		}

		if (!rtw_cfg80211_check_bss(padapter))
			netdev_dbg(padapter->pnetdev,
				   FUNC_ADPT_FMT " BSS not found !!\n",
				   FUNC_ADPT_ARG(padapter));
	}
	/* notify cfg80211 that device joined an IBSS */
	chan = ieee80211_get_channel(wiphy, freq);
	cfg80211_ibss_joined(padapter->pnetdev, cur_network->network.mac_address, chan, GFP_ATOMIC);
}

void rtw_cfg80211_indicate_connect(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network  *cur_network = &(pmlmepriv->cur_network);
	struct wireless_dev *pwdev = padapter->rtw_wdev;

	if (pwdev->iftype != NL80211_IFTYPE_STATION
		&& pwdev->iftype != NL80211_IFTYPE_P2P_CLIENT
	) {
		return;
	}

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true)
		return;

	{
		struct wlan_bssid_ex  *pnetwork = &(padapter->mlmeextpriv.mlmext_info.network);
		struct wlan_network *scanned = pmlmepriv->cur_network_scanned;

		if (!scanned) {
			rtw_warn_on(1);
			goto check_bss;
		}

		if (!memcmp(scanned->network.mac_address, pnetwork->mac_address, sizeof(NDIS_802_11_MAC_ADDRESS))
			&& !memcmp(&(scanned->network.ssid), &(pnetwork->ssid), sizeof(struct ndis_802_11_ssid))
		)
			rtw_cfg80211_inform_bss(padapter, scanned);
		else
			rtw_warn_on(1);
	}

check_bss:
	if (!rtw_cfg80211_check_bss(padapter))
		netdev_dbg(padapter->pnetdev,
			   FUNC_ADPT_FMT " BSS not found !!\n",
			   FUNC_ADPT_ARG(padapter));

	if (rtw_to_roam(padapter) > 0) {
		struct wiphy *wiphy = pwdev->wiphy;
		struct ieee80211_channel *notify_channel;
		u32 freq;
		u16 channel = cur_network->network.configuration.ds_config;
		struct cfg80211_roam_info roam_info = {};

		freq = rtw_ieee80211_channel_to_frequency(channel, NL80211_BAND_2GHZ);

		notify_channel = ieee80211_get_channel(wiphy, freq);

		roam_info.links[0].channel = notify_channel;
		roam_info.links[0].bssid = cur_network->network.mac_address;
		roam_info.req_ie =
			pmlmepriv->assoc_req + sizeof(struct ieee80211_hdr_3addr) + 2;
		roam_info.req_ie_len =
			pmlmepriv->assoc_req_len - sizeof(struct ieee80211_hdr_3addr) - 2;
		roam_info.resp_ie =
			pmlmepriv->assoc_rsp + sizeof(struct ieee80211_hdr_3addr) + 6;
		roam_info.resp_ie_len =
			pmlmepriv->assoc_rsp_len - sizeof(struct ieee80211_hdr_3addr) - 6;
		cfg80211_roamed(padapter->pnetdev, &roam_info, GFP_ATOMIC);
	} else {
		cfg80211_connect_result(padapter->pnetdev, cur_network->network.mac_address
			, pmlmepriv->assoc_req + sizeof(struct ieee80211_hdr_3addr) + 2
			, pmlmepriv->assoc_req_len - sizeof(struct ieee80211_hdr_3addr) - 2
			, pmlmepriv->assoc_rsp + sizeof(struct ieee80211_hdr_3addr) + 6
			, pmlmepriv->assoc_rsp_len - sizeof(struct ieee80211_hdr_3addr) - 6
			, WLAN_STATUS_SUCCESS, GFP_ATOMIC);
	}
}

void rtw_cfg80211_indicate_disconnect(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wireless_dev *pwdev = padapter->rtw_wdev;

	if (pwdev->iftype != NL80211_IFTYPE_STATION
		&& pwdev->iftype != NL80211_IFTYPE_P2P_CLIENT
	) {
		return;
	}

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true)
		return;

	if (!padapter->mlmepriv.not_indic_disco) {
		if (check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {
			cfg80211_disconnected(padapter->pnetdev, 0,
					      NULL, 0, true, GFP_ATOMIC);
		} else {
			cfg80211_connect_result(padapter->pnetdev, NULL, NULL, 0, NULL, 0,
						WLAN_STATUS_UNSPECIFIED_FAILURE, GFP_ATOMIC/*GFP_KERNEL*/);
		}
	}
}

static int rtw_cfg80211_ap_set_encryption(struct net_device *dev, struct ieee_param *param, u32 param_len)
{
	int ret = 0;
	u32 wep_key_idx, wep_key_len;
	struct sta_info *psta = NULL, *pbcmc_sta = NULL;
	struct adapter *padapter = rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv =  &(padapter->securitypriv);
	struct sta_priv *pstapriv = &padapter->stapriv;
	char *grpkey = padapter->securitypriv.dot118021XGrpKey[param->u.crypt.idx].skey;
	char *txkey = padapter->securitypriv.dot118021XGrptxmickey[param->u.crypt.idx].skey;
	char *rxkey = padapter->securitypriv.dot118021XGrprxmickey[param->u.crypt.idx].skey;

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
		if (!psta)
			/* ret = -EINVAL; */
			goto exit;
	}

	if (strcmp(param->u.crypt.alg, "none") == 0 && !psta)
		goto exit;

	if (strcmp(param->u.crypt.alg, "WEP") == 0 && !psta) {
		wep_key_idx = param->u.crypt.idx;
		wep_key_len = param->u.crypt.key_len;

		if ((wep_key_idx >= WEP_KEYS) || (wep_key_len <= 0)) {
			ret = -EINVAL;
			goto exit;
		}

		if (wep_key_len > 0)
			wep_key_len = wep_key_len <= 5 ? 5 : 13;

		if (psecuritypriv->bWepDefaultKeyIdxSet == 0) {
			/* wep default key has not been set, so use this key index as default key. */

			psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;
			psecuritypriv->ndisencryptstatus = Ndis802_11Encryption1Enabled;
			psecuritypriv->dot11PrivacyAlgrthm = _WEP40_;
			psecuritypriv->dot118021XGrpPrivacy = _WEP40_;

			if (wep_key_len == 13) {
				psecuritypriv->dot11PrivacyAlgrthm = _WEP104_;
				psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
			}

			psecuritypriv->dot11PrivacyKeyIndex = wep_key_idx;
		}

		memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), param->u.crypt.key, wep_key_len);

		psecuritypriv->dot11DefKeylen[wep_key_idx] = wep_key_len;

		rtw_ap_set_wep_key(padapter, param->u.crypt.key, wep_key_len, wep_key_idx, 1);

		goto exit;
	}

	/* group key */
	if (!psta && check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		/* group key */
		if (param->u.crypt.set_tx == 0) {
			if (strcmp(param->u.crypt.alg, "WEP") == 0) {
				memcpy(grpkey, param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));

				psecuritypriv->dot118021XGrpPrivacy = _WEP40_;
				if (param->u.crypt.key_len == 13)
					psecuritypriv->dot118021XGrpPrivacy = _WEP104_;

			} else if (strcmp(param->u.crypt.alg, "TKIP") == 0) {
				psecuritypriv->dot118021XGrpPrivacy = _TKIP_;

				memcpy(grpkey, param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));

				/* DEBUG_ERR("set key length :param->u.crypt.key_len =%d\n", param->u.crypt.key_len); */
				/* set mic key */
				memcpy(txkey, &(param->u.crypt.key[16]), 8);
				memcpy(rxkey, &(param->u.crypt.key[24]), 8);

				psecuritypriv->busetkipkey = true;

			} else if (strcmp(param->u.crypt.alg, "CCMP") == 0) {
				psecuritypriv->dot118021XGrpPrivacy = _AES_;

				memcpy(grpkey, param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));
			} else {
				psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
			}

			psecuritypriv->dot118021XGrpKeyid = param->u.crypt.idx;

			psecuritypriv->binstallGrpkey = true;

			psecuritypriv->dot11PrivacyAlgrthm = psecuritypriv->dot118021XGrpPrivacy;/*  */

			rtw_ap_set_group_key(padapter, param->u.crypt.key, psecuritypriv->dot118021XGrpPrivacy, param->u.crypt.idx);

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
			if (param->u.crypt.set_tx == 1) { /* pairwise key */
				memcpy(psta->dot118021x_UncstKey.skey, param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));

				if (strcmp(param->u.crypt.alg, "WEP") == 0) {
					psta->dot118021XPrivacy = _WEP40_;
					if (param->u.crypt.key_len == 13)
						psta->dot118021XPrivacy = _WEP104_;
				} else if (strcmp(param->u.crypt.alg, "TKIP") == 0) {
					psta->dot118021XPrivacy = _TKIP_;

					/* DEBUG_ERR("set key length :param->u.crypt.key_len =%d\n", param->u.crypt.key_len); */
					/* set mic key */
					memcpy(psta->dot11tkiptxmickey.skey, &(param->u.crypt.key[16]), 8);
					memcpy(psta->dot11tkiprxmickey.skey, &(param->u.crypt.key[24]), 8);

					psecuritypriv->busetkipkey = true;

				} else if (strcmp(param->u.crypt.alg, "CCMP") == 0) {
					psta->dot118021XPrivacy = _AES_;
				} else {
					psta->dot118021XPrivacy = _NO_PRIVACY_;
				}

				rtw_ap_set_pairwise_key(padapter, psta);

				psta->ieee8021x_blocked = false;

				psta->bpairwise_key_installed = true;

			} else { /* group key??? */
				if (strcmp(param->u.crypt.alg, "WEP") == 0) {
					memcpy(grpkey, param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));

					psecuritypriv->dot118021XGrpPrivacy = _WEP40_;
					if (param->u.crypt.key_len == 13)
						psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
				} else if (strcmp(param->u.crypt.alg, "TKIP") == 0) {
					psecuritypriv->dot118021XGrpPrivacy = _TKIP_;

					memcpy(grpkey, param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));

					/* DEBUG_ERR("set key length :param->u.crypt.key_len =%d\n", param->u.crypt.key_len); */
					/* set mic key */
					memcpy(txkey, &(param->u.crypt.key[16]), 8);
					memcpy(rxkey, &(param->u.crypt.key[24]), 8);

					psecuritypriv->busetkipkey = true;

				} else if (strcmp(param->u.crypt.alg, "CCMP") == 0) {
					psecuritypriv->dot118021XGrpPrivacy = _AES_;

					memcpy(grpkey, param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));
				} else {
					psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
				}

				psecuritypriv->dot118021XGrpKeyid = param->u.crypt.idx;

				psecuritypriv->binstallGrpkey = true;

				psecuritypriv->dot11PrivacyAlgrthm = psecuritypriv->dot118021XGrpPrivacy;/*  */

				rtw_ap_set_group_key(padapter, param->u.crypt.key, psecuritypriv->dot118021XGrpPrivacy, param->u.crypt.idx);

				pbcmc_sta = rtw_get_bcmc_stainfo(padapter);
				if (pbcmc_sta) {
					pbcmc_sta->ieee8021x_blocked = false;
					pbcmc_sta->dot118021XPrivacy = psecuritypriv->dot118021XGrpPrivacy;/* rx will use bmc_sta's dot118021XPrivacy */
				}
			}
		}
	}

exit:

	return ret;
}

static int rtw_cfg80211_set_encryption(struct net_device *dev, struct ieee_param *param, u32 param_len)
{
	int ret = 0;
	u8 max_idx;
	u32 wep_key_idx, wep_key_len;
	struct adapter *padapter = rtw_netdev_priv(dev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	param->u.crypt.err = 0;
	param->u.crypt.alg[IEEE_CRYPT_ALG_NAME_LEN - 1] = '\0';

	if (param_len < (u32)((u8 *)param->u.crypt.key - (u8 *)param) + param->u.crypt.key_len) {
		ret =  -EINVAL;
		goto exit;
	}

	if (param->sta_addr[0] != 0xff || param->sta_addr[1] != 0xff ||
	    param->sta_addr[2] != 0xff || param->sta_addr[3] != 0xff ||
	    param->sta_addr[4] != 0xff || param->sta_addr[5] != 0xff) {
		ret = -EINVAL;
		goto exit;
	}

	if (strcmp(param->u.crypt.alg, "WEP") == 0)
		max_idx = WEP_KEYS - 1;
	else
		max_idx = BIP_MAX_KEYID;

	if (param->u.crypt.idx > max_idx) {
		netdev_err(dev, "Error crypt.idx %d > %d\n", param->u.crypt.idx, max_idx);
		ret = -EINVAL;
		goto exit;
	}

	if (strcmp(param->u.crypt.alg, "WEP") == 0) {
		wep_key_idx = param->u.crypt.idx;
		wep_key_len = param->u.crypt.key_len;

		if (wep_key_len <= 0) {
			ret = -EINVAL;
			goto exit;
		}

		if (psecuritypriv->bWepDefaultKeyIdxSet == 0) {
			/* wep default key has not been set, so use this key index as default key. */

			wep_key_len = wep_key_len <= 5 ? 5 : 13;

			psecuritypriv->ndisencryptstatus = Ndis802_11Encryption1Enabled;
			psecuritypriv->dot11PrivacyAlgrthm = _WEP40_;
			psecuritypriv->dot118021XGrpPrivacy = _WEP40_;

			if (wep_key_len == 13) {
				psecuritypriv->dot11PrivacyAlgrthm = _WEP104_;
				psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
			}

			psecuritypriv->dot11PrivacyKeyIndex = wep_key_idx;
		}

		memcpy(&(psecuritypriv->dot11DefKey[wep_key_idx].skey[0]), param->u.crypt.key, wep_key_len);

		psecuritypriv->dot11DefKeylen[wep_key_idx] = wep_key_len;

		rtw_set_key(padapter, psecuritypriv, wep_key_idx, 0, true);

		goto exit;
	}

	if (padapter->securitypriv.dot11AuthAlgrthm == dot11AuthAlgrthm_8021X) { /*  802_1x */
		struct sta_info *psta, *pbcmc_sta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_MP_STATE) == true) { /* sta mode */
			psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
			if (psta) {
				/* Jeff: don't disable ieee8021x_blocked while clearing key */
				if (strcmp(param->u.crypt.alg, "none") != 0)
					psta->ieee8021x_blocked = false;

				if ((padapter->securitypriv.ndisencryptstatus == Ndis802_11Encryption2Enabled) ||
				    (padapter->securitypriv.ndisencryptstatus ==  Ndis802_11Encryption3Enabled)) {
					psta->dot118021XPrivacy = padapter->securitypriv.dot11PrivacyAlgrthm;
				}

				if (param->u.crypt.set_tx == 1) { /* pairwise key */

					memcpy(psta->dot118021x_UncstKey.skey, param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));

					if (strcmp(param->u.crypt.alg, "TKIP") == 0) { /* set mic key */
						/* DEBUG_ERR(("\nset key length :param->u.crypt.key_len =%d\n", param->u.crypt.key_len)); */
						memcpy(psta->dot11tkiptxmickey.skey, &(param->u.crypt.key[16]), 8);
						memcpy(psta->dot11tkiprxmickey.skey, &(param->u.crypt.key[24]), 8);

						padapter->securitypriv.busetkipkey = false;
						/* _set_timer(&padapter->securitypriv.tkip_timer, 50); */
					}

					rtw_setstakey_cmd(padapter, psta, true, true);
				} else { /* group key */
					if (strcmp(param->u.crypt.alg, "TKIP") == 0 || strcmp(param->u.crypt.alg, "CCMP") == 0) {
						memcpy(padapter->securitypriv.dot118021XGrpKey[param->u.crypt.idx].skey, param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));
						memcpy(padapter->securitypriv.dot118021XGrptxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[16]), 8);
						memcpy(padapter->securitypriv.dot118021XGrprxmickey[param->u.crypt.idx].skey, &(param->u.crypt.key[24]), 8);
						padapter->securitypriv.binstallGrpkey = true;

						padapter->securitypriv.dot118021XGrpKeyid = param->u.crypt.idx;
						rtw_set_key(padapter, &padapter->securitypriv, param->u.crypt.idx, 1, true);
					} else if (strcmp(param->u.crypt.alg, "BIP") == 0) {
						/* save the IGTK key, length 16 bytes */
						memcpy(padapter->securitypriv.dot11wBIPKey[param->u.crypt.idx].skey, param->u.crypt.key, (param->u.crypt.key_len > 16 ? 16 : param->u.crypt.key_len));
						/*
						for (no = 0;no<16;no++)
							printk(" %02x ", padapter->securitypriv.dot11wBIPKey[param->u.crypt.idx].skey[no]);
						*/
						padapter->securitypriv.dot11wBIPKeyid = param->u.crypt.idx;
						padapter->securitypriv.binstallBIPkey = true;
					}
				}
			}

			pbcmc_sta = rtw_get_bcmc_stainfo(padapter);
			if (!pbcmc_sta) {
				/* DEBUG_ERR(("Set OID_802_11_ADD_KEY: bcmc stainfo is null\n")); */
			} else {
				/* Jeff: don't disable ieee8021x_blocked while clearing key */
				if (strcmp(param->u.crypt.alg, "none") != 0)
					pbcmc_sta->ieee8021x_blocked = false;

				if ((padapter->securitypriv.ndisencryptstatus == Ndis802_11Encryption2Enabled) ||
				    (padapter->securitypriv.ndisencryptstatus ==  Ndis802_11Encryption3Enabled)) {
					pbcmc_sta->dot118021XPrivacy = padapter->securitypriv.dot11PrivacyAlgrthm;
				}
			}
		} else if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE)) { /* adhoc mode */
		}
	}

exit:

	return ret;
}

static int cfg80211_rtw_add_key(struct wiphy *wiphy, struct net_device *ndev,
				int link_id, u8 key_index, bool pairwise,
				const u8 *mac_addr, struct key_params *params)
{
	char *alg_name;
	u32 param_len;
	struct ieee_param *param = NULL;
	int ret = 0;
	struct adapter *padapter = rtw_netdev_priv(ndev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	param_len = sizeof(struct ieee_param) + params->key_len;
	param = rtw_malloc(param_len);
	if (!param)
		return -1;

	memset(param, 0, param_len);

	param->cmd = IEEE_CMD_SET_ENCRYPTION;
	eth_broadcast_addr(param->sta_addr);

	switch (params->cipher) {
	case IW_AUTH_CIPHER_NONE:
		/* todo: remove key */
		/* remove = 1; */
		alg_name = "none";
		break;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		alg_name = "WEP";
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		alg_name = "TKIP";
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		alg_name = "CCMP";
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		alg_name = "BIP";
		break;
	default:
		ret = -ENOTSUPP;
		goto addkey_end;
	}

	strscpy(param->u.crypt.alg, alg_name);

	if (!mac_addr || is_broadcast_ether_addr(mac_addr))
		param->u.crypt.set_tx = 0; /* for wpa/wpa2 group key */
	else
		param->u.crypt.set_tx = 1; /* for wpa/wpa2 pairwise key */

	param->u.crypt.idx = key_index;

	if (params->seq_len && params->seq)
		memcpy(param->u.crypt.seq, (u8 *)params->seq, params->seq_len);

	if (params->key_len && params->key) {
		param->u.crypt.key_len = params->key_len;
		memcpy(param->u.crypt.key, (u8 *)params->key, params->key_len);
	}

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true) {
		ret =  rtw_cfg80211_set_encryption(ndev, param, param_len);
	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) {
		if (mac_addr)
			memcpy(param->sta_addr, (void *)mac_addr, ETH_ALEN);

		ret = rtw_cfg80211_ap_set_encryption(ndev, param, param_len);
	} else if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true
		|| check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) {
		ret =  rtw_cfg80211_set_encryption(ndev, param, param_len);
	}

addkey_end:
	kfree(param);

	return ret;
}

static int cfg80211_rtw_get_key(struct wiphy *wiphy, struct net_device *ndev,
				int link_id, u8 key_index, bool pairwise,
				const u8 *mac_addr, void *cookie,
				void (*callback)(void *cookie,
						 struct key_params*))
{
	return 0;
}

static int cfg80211_rtw_del_key(struct wiphy *wiphy, struct net_device *ndev,
				int link_id, u8 key_index, bool pairwise,
				const u8 *mac_addr)
{
	struct adapter *padapter = rtw_netdev_priv(ndev);
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	if (key_index == psecuritypriv->dot11PrivacyKeyIndex) {
		/* clear the flag of wep default key set. */
		psecuritypriv->bWepDefaultKeyIdxSet = 0;
	}

	return 0;
}

static int cfg80211_rtw_set_default_key(struct wiphy *wiphy,
					struct net_device *ndev, int link_id,
					u8 key_index, bool unicast,
					bool multicast)
{
	struct adapter *padapter = rtw_netdev_priv(ndev);
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	if ((key_index < WEP_KEYS) && ((psecuritypriv->dot11PrivacyAlgrthm == _WEP40_) || (psecuritypriv->dot11PrivacyAlgrthm == _WEP104_))) { /* set wep default key */
		psecuritypriv->ndisencryptstatus = Ndis802_11Encryption1Enabled;

		psecuritypriv->dot11PrivacyKeyIndex = key_index;

		psecuritypriv->dot11PrivacyAlgrthm = _WEP40_;
		psecuritypriv->dot118021XGrpPrivacy = _WEP40_;
		if (psecuritypriv->dot11DefKeylen[key_index] == 13) {
			psecuritypriv->dot11PrivacyAlgrthm = _WEP104_;
			psecuritypriv->dot118021XGrpPrivacy = _WEP104_;
		}

		psecuritypriv->bWepDefaultKeyIdxSet = 1; /* set the flag to represent that wep default key has been set */
	}

	return 0;
}

static int cfg80211_rtw_get_station(struct wiphy *wiphy,
				    struct net_device *ndev,
				const u8 *mac,
				struct station_info *sinfo)
{
	int ret = 0;
	struct adapter *padapter = rtw_netdev_priv(ndev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;

	sinfo->filled = 0;

	if (!mac) {
		ret = -ENOENT;
		goto exit;
	}

	psta = rtw_get_stainfo(pstapriv, (u8 *)mac);
	if (!psta) {
		ret = -ENOENT;
		goto exit;
	}

	/* for infra./P2PClient mode */
	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)
		&& check_fwstate(pmlmepriv, _FW_LINKED)) {
		struct wlan_network  *cur_network = &(pmlmepriv->cur_network);

		if (memcmp((u8 *)mac, cur_network->network.mac_address, ETH_ALEN)) {
			ret = -ENOENT;
			goto exit;
		}

		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL);
		sinfo->signal = translate_percentage_to_dbm(padapter->recvpriv.signal_strength);

		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
		sinfo->txrate.legacy = rtw_get_cur_max_rate(padapter);

		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_PACKETS);
		sinfo->rx_packets = sta_rx_data_pkts(psta);

		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_PACKETS);
		sinfo->tx_packets = psta->sta_stats.tx_pkts;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_FAILED);
	}

	/* for Ad-Hoc/AP mode */
	if ((check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) ||
	     check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) ||
	     check_fwstate(pmlmepriv, WIFI_AP_STATE)) &&
	    check_fwstate(pmlmepriv, _FW_LINKED)) {
		/* TODO: should acquire station info... */
	}

exit:
	return ret;
}

static int cfg80211_rtw_change_iface(struct wiphy *wiphy,
				     struct net_device *ndev,
				     enum nl80211_iftype type,
				     struct vif_params *params)
{
	enum nl80211_iftype old_type;
	enum ndis_802_11_network_infrastructure networkType;
	struct adapter *padapter = rtw_netdev_priv(ndev);
	struct wireless_dev *rtw_wdev = padapter->rtw_wdev;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	int ret = 0;

	if (adapter_to_dvobj(padapter)->processing_dev_remove == true) {
		ret = -EPERM;
		goto exit;
	}

	{
		if (netdev_open(ndev) != 0) {
			ret = -EPERM;
			goto exit;
		}
	}

	if (rtw_pwr_wakeup(padapter) == _FAIL) {
		ret = -EPERM;
		goto exit;
	}

	old_type = rtw_wdev->iftype;

	if (old_type != type) {
		pmlmeext->action_public_rxseq = 0xffff;
		pmlmeext->action_public_dialog_token = 0xff;
	}

	switch (type) {
	case NL80211_IFTYPE_ADHOC:
		networkType = Ndis802_11IBSS;
		break;
	case NL80211_IFTYPE_STATION:
		networkType = Ndis802_11Infrastructure;
		break;
	case NL80211_IFTYPE_AP:
		networkType = Ndis802_11APMode;
		break;
	default:
		ret = -EOPNOTSUPP;
		goto exit;
	}

	rtw_wdev->iftype = type;

	if (rtw_set_802_11_infrastructure_mode(padapter, networkType) == false) {
		rtw_wdev->iftype = old_type;
		ret = -EPERM;
		goto exit;
	}

	rtw_setopmode_cmd(padapter, networkType, true);

exit:

	return ret;
}

void rtw_cfg80211_indicate_scan_done(struct adapter *adapter, bool aborted)
{
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(adapter);
	struct cfg80211_scan_info info = {
		.aborted = aborted
	};

	spin_lock_bh(&pwdev_priv->scan_req_lock);
	if (pwdev_priv->scan_request) {
		/* avoid WARN_ON(request != wiphy_to_dev(request->wiphy)->scan_req); */
		if (pwdev_priv->scan_request->wiphy == pwdev_priv->rtw_wdev->wiphy)
			cfg80211_scan_done(pwdev_priv->scan_request, &info);

		pwdev_priv->scan_request = NULL;
	}
	spin_unlock_bh(&pwdev_priv->scan_req_lock);
}

void rtw_cfg80211_unlink_bss(struct adapter *padapter, struct wlan_network *pnetwork)
{
	struct wireless_dev *pwdev = padapter->rtw_wdev;
	struct wiphy *wiphy = pwdev->wiphy;
	struct cfg80211_bss *bss = NULL;
	struct wlan_bssid_ex *select_network = &pnetwork->network;

	bss = cfg80211_get_bss(wiphy, NULL/*notify_channel*/,
			       select_network->mac_address,
			       select_network->ssid.ssid,
			       select_network->ssid.ssid_length,
			       IEEE80211_BSS_TYPE_ANY, IEEE80211_PRIVACY_ANY);

	if (bss) {
		cfg80211_unlink_bss(wiphy, bss);
		cfg80211_put_bss(padapter->rtw_wdev->wiphy, bss);
	}
}

void rtw_cfg80211_surveydone_event_callback(struct adapter *padapter)
{
	struct list_head					*plist, *phead;
	struct	mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct __queue *queue	= &(pmlmepriv->scanned_queue);
	struct	wlan_network	*pnetwork = NULL;

	spin_lock_bh(&(pmlmepriv->scanned_queue.lock));

	phead = get_list_head(queue);
	list_for_each(plist, phead)
	{
		pnetwork = list_entry(plist, struct wlan_network, list);

		/* report network only if the current channel set contains the channel to which this network belongs */
		if (rtw_ch_set_search_ch(padapter->mlmeextpriv.channel_set, pnetwork->network.configuration.ds_config) >= 0
			&& true == rtw_validate_ssid(&(pnetwork->network.ssid))) {
			/* ev =translate_scan(padapter, a, pnetwork, ev, stop); */
			rtw_cfg80211_inform_bss(padapter, pnetwork);
		}
	}

	spin_unlock_bh(&(pmlmepriv->scanned_queue.lock));
}

static int rtw_cfg80211_set_probe_req_wpsp2pie(struct adapter *padapter, char *buf, int len)
{
	int ret = 0;
	uint wps_ielen = 0;
	u8 *wps_ie;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);

	if (len > 0) {
		wps_ie = rtw_get_wps_ie(buf, len, NULL, &wps_ielen);
		if (wps_ie) {
			if (pmlmepriv->wps_probe_req_ie) {
				pmlmepriv->wps_probe_req_ie_len = 0;
				kfree(pmlmepriv->wps_probe_req_ie);
				pmlmepriv->wps_probe_req_ie = NULL;
			}

			pmlmepriv->wps_probe_req_ie = rtw_malloc(wps_ielen);
			if (!pmlmepriv->wps_probe_req_ie)
				return -EINVAL;

			memcpy(pmlmepriv->wps_probe_req_ie, wps_ie, wps_ielen);
			pmlmepriv->wps_probe_req_ie_len = wps_ielen;
		}
	}

	return ret;
}

static int cfg80211_rtw_scan(struct wiphy *wiphy
	, struct cfg80211_scan_request *request)
{
	struct net_device *ndev = wdev_to_ndev(request->wdev);
	int i;
	u8 _status = false;
	int ret = 0;
	struct ndis_802_11_ssid *ssid = NULL;
	struct rtw_ieee80211_channel ch[RTW_CHANNEL_SCAN_AMOUNT];
	u8 survey_times = 3;
	u8 survey_times_for_one_ch = 6;
	struct cfg80211_ssid *ssids = request->ssids;
	int j = 0;
	bool need_indicate_scan_done = false;

	struct adapter *padapter;
	struct rtw_wdev_priv *pwdev_priv;
	struct mlme_priv *pmlmepriv;

	if (!ndev) {
		ret = -EINVAL;
		goto exit;
	}

	padapter = rtw_netdev_priv(ndev);
	pwdev_priv = adapter_wdev_data(padapter);
	pmlmepriv = &padapter->mlmepriv;
/* endif */

	spin_lock_bh(&pwdev_priv->scan_req_lock);
	pwdev_priv->scan_request = request;
	spin_unlock_bh(&pwdev_priv->scan_req_lock);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) {
		if (check_fwstate(pmlmepriv, WIFI_UNDER_WPS | _FW_UNDER_SURVEY | _FW_UNDER_LINKING) == true) {
			need_indicate_scan_done = true;
			goto check_need_indicate_scan_done;
		}
	}

	rtw_ps_deny(padapter, PS_DENY_SCAN);
	if (rtw_pwr_wakeup(padapter) == _FAIL) {
		need_indicate_scan_done = true;
		goto check_need_indicate_scan_done;
	}

	if (request->ie && request->ie_len > 0)
		rtw_cfg80211_set_probe_req_wpsp2pie(padapter, (u8 *)request->ie, request->ie_len);

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == true) {
		need_indicate_scan_done = true;
		goto check_need_indicate_scan_done;
	} else if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == true) {
		ret = -EBUSY;
		goto check_need_indicate_scan_done;
	}

	if (pmlmepriv->LinkDetectInfo.bBusyTraffic == true) {
		static unsigned long lastscantime;
		unsigned long passtime;

		passtime = jiffies_to_msecs(jiffies - lastscantime);
		lastscantime = jiffies;
		if (passtime > 12000) {
			need_indicate_scan_done = true;
			goto check_need_indicate_scan_done;
		}
	}

	if (rtw_is_scan_deny(padapter)) {
		need_indicate_scan_done = true;
		goto check_need_indicate_scan_done;
	}

	ssid = kcalloc(RTW_SSID_SCAN_AMOUNT, sizeof(*ssid), GFP_KERNEL);
	if (!ssid) {
		ret = -ENOMEM;
		goto check_need_indicate_scan_done;
	}

	/* parsing request ssids, n_ssids */
	for (i = 0; i < request->n_ssids && i < RTW_SSID_SCAN_AMOUNT; i++) {
		memcpy(ssid[i].ssid, ssids[i].ssid, ssids[i].ssid_len);
		ssid[i].ssid_length = ssids[i].ssid_len;
	}

	/* parsing channels, n_channels */
	memset(ch, 0, sizeof(struct rtw_ieee80211_channel) * RTW_CHANNEL_SCAN_AMOUNT);
	for (i = 0; i < request->n_channels && i < RTW_CHANNEL_SCAN_AMOUNT; i++) {
		ch[i].hw_value = request->channels[i]->hw_value;
		ch[i].flags = request->channels[i]->flags;
	}

	spin_lock_bh(&pmlmepriv->lock);
	if (request->n_channels == 1) {
		for (i = 1; i < survey_times_for_one_ch; i++)
			memcpy(&ch[i], &ch[0], sizeof(struct rtw_ieee80211_channel));
		_status = rtw_sitesurvey_cmd(padapter, ssid, RTW_SSID_SCAN_AMOUNT, ch, survey_times_for_one_ch);
	} else if (request->n_channels <= 4) {
		for (j = request->n_channels - 1; j >= 0; j--)
			for (i = 0; i < survey_times; i++)
				memcpy(&ch[j * survey_times + i], &ch[j], sizeof(struct rtw_ieee80211_channel));
		_status = rtw_sitesurvey_cmd(padapter, ssid, RTW_SSID_SCAN_AMOUNT, ch, survey_times * request->n_channels);
	} else {
		_status = rtw_sitesurvey_cmd(padapter, ssid, RTW_SSID_SCAN_AMOUNT, NULL, 0);
	}
	spin_unlock_bh(&pmlmepriv->lock);

	if (_status == false)
		ret = -1;

check_need_indicate_scan_done:
	kfree(ssid);
	if (need_indicate_scan_done) {
		rtw_cfg80211_surveydone_event_callback(padapter);
		rtw_cfg80211_indicate_scan_done(padapter, false);
	}

	rtw_ps_deny_cancel(padapter, PS_DENY_SCAN);

exit:
	return ret;
}

static int cfg80211_rtw_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	return 0;
}

static int rtw_cfg80211_set_wpa_version(struct security_priv *psecuritypriv, u32 wpa_version)
{
	if (!wpa_version) {
		psecuritypriv->ndisauthtype = Ndis802_11AuthModeOpen;
		return 0;
	}

	if (wpa_version & (NL80211_WPA_VERSION_1 | NL80211_WPA_VERSION_2))
		psecuritypriv->ndisauthtype = Ndis802_11AuthModeWPAPSK;

	return 0;
}

static int rtw_cfg80211_set_auth_type(struct security_priv *psecuritypriv,
				      enum nl80211_auth_type sme_auth_type)
{
	switch (sme_auth_type) {
	case NL80211_AUTHTYPE_AUTOMATIC:

		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Auto;

		break;
	case NL80211_AUTHTYPE_OPEN_SYSTEM:

		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;

		if (psecuritypriv->ndisauthtype > Ndis802_11AuthModeWPA)
			psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;

		break;
	case NL80211_AUTHTYPE_SHARED_KEY:

		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Shared;

		psecuritypriv->ndisencryptstatus = Ndis802_11Encryption1Enabled;

		break;
	default:
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open;
		/* return -ENOTSUPP; */
	}

	return 0;
}

static int rtw_cfg80211_set_cipher(struct security_priv *psecuritypriv, u32 cipher, bool ucast)
{
	u32 ndisencryptstatus = Ndis802_11EncryptionDisabled;

	u32 *profile_cipher = ucast ? &psecuritypriv->dot11PrivacyAlgrthm :
		&psecuritypriv->dot118021XGrpPrivacy;

	if (!cipher) {
		*profile_cipher = _NO_PRIVACY_;
		psecuritypriv->ndisencryptstatus = ndisencryptstatus;
		return 0;
	}

	switch (cipher) {
	case IW_AUTH_CIPHER_NONE:
		*profile_cipher = _NO_PRIVACY_;
		ndisencryptstatus = Ndis802_11EncryptionDisabled;
		break;
	case WLAN_CIPHER_SUITE_WEP40:
		*profile_cipher = _WEP40_;
		ndisencryptstatus = Ndis802_11Encryption1Enabled;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		*profile_cipher = _WEP104_;
		ndisencryptstatus = Ndis802_11Encryption1Enabled;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		*profile_cipher = _TKIP_;
		ndisencryptstatus = Ndis802_11Encryption2Enabled;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		*profile_cipher = _AES_;
		ndisencryptstatus = Ndis802_11Encryption3Enabled;
		break;
	default:
		return -ENOTSUPP;
	}

	if (ucast) {
		psecuritypriv->ndisencryptstatus = ndisencryptstatus;

		/* if (psecuritypriv->dot11PrivacyAlgrthm >= _AES_) */
		/*	psecuritypriv->ndisauthtype = Ndis802_11AuthModeWPA2PSK; */
	}

	return 0;
}

static int rtw_cfg80211_set_key_mgt(struct security_priv *psecuritypriv, u32 key_mgt)
{
	if (key_mgt == WLAN_AKM_SUITE_8021X)
		/* auth_type = UMAC_AUTH_TYPE_8021X; */
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
	else if (key_mgt == WLAN_AKM_SUITE_PSK) {
		psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
	}

	return 0;
}

static int rtw_cfg80211_set_wpa_ie(struct adapter *padapter, u8 *pie, size_t ielen)
{
	u8 *buf = NULL;
	int group_cipher = 0, pairwise_cipher = 0;
	int ret = 0;
	int wpa_ielen = 0;
	int wpa2_ielen = 0;
	u8 *pwpa, *pwpa2;
	u8 null_addr[] = {0, 0, 0, 0, 0, 0};

	if (!pie || !ielen) {
		/* Treat this as normal case, but need to clear WIFI_UNDER_WPS */
		_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);
		goto exit;
	}

	if (ielen > MAX_WPA_IE_LEN + MAX_WPS_IE_LEN + MAX_P2P_IE_LEN) {
		ret = -EINVAL;
		goto exit;
	}

	buf = rtw_zmalloc(ielen);
	if (!buf) {
		ret =  -ENOMEM;
		goto exit;
	}

	memcpy(buf, pie, ielen);

	if (ielen < RSN_HEADER_LEN) {
		ret  = -1;
		goto exit;
	}

	pwpa = rtw_get_wpa_ie(buf, &wpa_ielen, ielen);
	if (pwpa && wpa_ielen > 0) {
		if (rtw_parse_wpa_ie(pwpa, wpa_ielen + 2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeWPAPSK;
			memcpy(padapter->securitypriv.supplicant_ie, &pwpa[0], wpa_ielen + 2);
		}
	}

	pwpa2 = rtw_get_wpa2_ie(buf, &wpa2_ielen, ielen);
	if (pwpa2 && wpa2_ielen > 0) {
		if (rtw_parse_wpa2_ie(pwpa2, wpa2_ielen + 2, &group_cipher, &pairwise_cipher, NULL) == _SUCCESS) {
			padapter->securitypriv.dot11AuthAlgrthm = dot11AuthAlgrthm_8021X;
			padapter->securitypriv.ndisauthtype = Ndis802_11AuthModeWPA2PSK;
			memcpy(padapter->securitypriv.supplicant_ie, &pwpa2[0], wpa2_ielen + 2);
		}
	}

	if (group_cipher == 0)
		group_cipher = WPA_CIPHER_NONE;

	if (pairwise_cipher == 0)
		pairwise_cipher = WPA_CIPHER_NONE;

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

	{/* handle wps_ie */
		uint wps_ielen;
		u8 *wps_ie;

		wps_ie = rtw_get_wps_ie(buf, ielen, NULL, &wps_ielen);
		if (wps_ie && wps_ielen > 0) {
			padapter->securitypriv.wps_ie_len = min_t(uint, wps_ielen, MAX_WPS_IE_LEN);
			memcpy(padapter->securitypriv.wps_ie, wps_ie, padapter->securitypriv.wps_ie_len);
			set_fwstate(&padapter->mlmepriv, WIFI_UNDER_WPS);
		} else {
			_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);
		}
	}

	/* TKIP and AES disallow multicast packets until installing group key */
	if (padapter->securitypriv.dot11PrivacyAlgrthm == _TKIP_
		|| padapter->securitypriv.dot11PrivacyAlgrthm == _TKIP_WTMIC_
		|| padapter->securitypriv.dot11PrivacyAlgrthm == _AES_)
		/* WPS open need to enable multicast */
		/*  check_fwstate(&padapter->mlmepriv, WIFI_UNDER_WPS) == true) */
		rtw_hal_set_hwreg(padapter, HW_VAR_OFF_RCR_AM, null_addr);

exit:
	kfree(buf);
	if (ret)
		_clr_fwstate_(&padapter->mlmepriv, WIFI_UNDER_WPS);
	return ret;
}

static int cfg80211_rtw_join_ibss(struct wiphy *wiphy, struct net_device *ndev,
				  struct cfg80211_ibss_params *params)
{
	struct adapter *padapter = rtw_netdev_priv(ndev);
	struct ndis_802_11_ssid ndis_ssid;
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	int ret = 0;

	if (rtw_pwr_wakeup(padapter) == _FAIL) {
		ret = -EPERM;
		goto exit;
	}

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		ret = -EPERM;
		goto exit;
	}

	if (!params->ssid || !params->ssid_len) {
		ret = -EINVAL;
		goto exit;
	}

	if (params->ssid_len > IW_ESSID_MAX_SIZE) {
		ret = -E2BIG;
		goto exit;
	}

	memset(&ndis_ssid, 0, sizeof(struct ndis_802_11_ssid));
	ndis_ssid.ssid_length = params->ssid_len;
	memcpy(ndis_ssid.ssid, (u8 *)params->ssid, params->ssid_len);

	psecuritypriv->ndisencryptstatus = Ndis802_11EncryptionDisabled;
	psecuritypriv->dot11PrivacyAlgrthm = _NO_PRIVACY_;
	psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
	psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open; /* open system */
	psecuritypriv->ndisauthtype = Ndis802_11AuthModeOpen;

	ret = rtw_cfg80211_set_auth_type(psecuritypriv, NL80211_AUTHTYPE_OPEN_SYSTEM);
	rtw_set_802_11_authentication_mode(padapter, psecuritypriv->ndisauthtype);

	if (rtw_set_802_11_ssid(padapter, &ndis_ssid) == false) {
		ret = -1;
		goto exit;
	}

exit:
	return ret;
}

static int cfg80211_rtw_leave_ibss(struct wiphy *wiphy, struct net_device *ndev)
{
	struct adapter *padapter = rtw_netdev_priv(ndev);
	struct wireless_dev *rtw_wdev = padapter->rtw_wdev;
	enum nl80211_iftype old_type;
	int ret = 0;

	old_type = rtw_wdev->iftype;

	rtw_set_to_roam(padapter, 0);

	if (check_fwstate(&padapter->mlmepriv, _FW_LINKED)) {
		rtw_scan_abort(padapter);
		LeaveAllPowerSaveMode(padapter);

		rtw_wdev->iftype = NL80211_IFTYPE_STATION;

		if (rtw_set_802_11_infrastructure_mode(padapter, Ndis802_11Infrastructure) == false) {
			rtw_wdev->iftype = old_type;
			ret = -EPERM;
			goto leave_ibss;
		}
		rtw_setopmode_cmd(padapter, Ndis802_11Infrastructure, true);
	}

leave_ibss:
	return ret;
}

static int cfg80211_rtw_connect(struct wiphy *wiphy, struct net_device *ndev,
				struct cfg80211_connect_params *sme)
{
	int ret = 0;
	enum ndis_802_11_authentication_mode authmode;
	struct ndis_802_11_ssid ndis_ssid;
	struct adapter *padapter = rtw_netdev_priv(ndev);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	padapter->mlmepriv.not_indic_disco = true;

	if (adapter_wdev_data(padapter)->block == true) {
		ret = -EBUSY;
		goto exit;
	}

	rtw_ps_deny(padapter, PS_DENY_JOIN);
	if (rtw_pwr_wakeup(padapter) == _FAIL) {
		ret = -EPERM;
		goto exit;
	}

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		ret = -EPERM;
		goto exit;
	}

	if (!sme->ssid || !sme->ssid_len) {
		ret = -EINVAL;
		goto exit;
	}

	if (sme->ssid_len > IW_ESSID_MAX_SIZE) {
		ret = -E2BIG;
		goto exit;
	}

	memset(&ndis_ssid, 0, sizeof(struct ndis_802_11_ssid));
	ndis_ssid.ssid_length = sme->ssid_len;
	memcpy(ndis_ssid.ssid, (u8 *)sme->ssid, sme->ssid_len);

	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == true) {
		ret = -EBUSY;
		goto exit;
	}
	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == true)
		rtw_scan_abort(padapter);

	psecuritypriv->ndisencryptstatus = Ndis802_11EncryptionDisabled;
	psecuritypriv->dot11PrivacyAlgrthm = _NO_PRIVACY_;
	psecuritypriv->dot118021XGrpPrivacy = _NO_PRIVACY_;
	psecuritypriv->dot11AuthAlgrthm = dot11AuthAlgrthm_Open; /* open system */
	psecuritypriv->ndisauthtype = Ndis802_11AuthModeOpen;

	ret = rtw_cfg80211_set_wpa_version(psecuritypriv, sme->crypto.wpa_versions);
	if (ret < 0)
		goto exit;

	ret = rtw_cfg80211_set_auth_type(psecuritypriv, sme->auth_type);

	if (ret < 0)
		goto exit;

	ret = rtw_cfg80211_set_wpa_ie(padapter, (u8 *)sme->ie, sme->ie_len);
	if (ret < 0)
		goto exit;

	if (sme->crypto.n_ciphers_pairwise) {
		ret = rtw_cfg80211_set_cipher(psecuritypriv, sme->crypto.ciphers_pairwise[0], true);
		if (ret < 0)
			goto exit;
	}

	/* For WEP Shared auth */
	if ((psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_Shared ||
	     psecuritypriv->dot11AuthAlgrthm == dot11AuthAlgrthm_Auto) && sme->key) {
		u32 wep_key_idx, wep_key_len, wep_total_len;
		struct ndis_802_11_wep	 *pwep = NULL;

		wep_key_idx = sme->key_idx;
		wep_key_len = sme->key_len;

		if (sme->key_idx > WEP_KEYS) {
			ret = -EINVAL;
			goto exit;
		}

		if (wep_key_len > 0) {
			wep_key_len = wep_key_len <= 5 ? 5 : 13;
			wep_total_len = wep_key_len + FIELD_OFFSET(struct ndis_802_11_wep, key_material);
			pwep = rtw_malloc(wep_total_len);
			if (!pwep) {
				ret = -ENOMEM;
				goto exit;
			}

			memset(pwep, 0, wep_total_len);

			pwep->key_length = wep_key_len;
			pwep->length = wep_total_len;

			if (wep_key_len == 13) {
				padapter->securitypriv.dot11PrivacyAlgrthm = _WEP104_;
				padapter->securitypriv.dot118021XGrpPrivacy = _WEP104_;
			}
		} else {
			ret = -EINVAL;
			goto exit;
		}

		pwep->key_index = wep_key_idx;
		pwep->key_index |= 0x80000000;

		memcpy(pwep->key_material,  (void *)sme->key, pwep->key_length);

		if (rtw_set_802_11_add_wep(padapter, pwep) == (u8)_FAIL)
			ret = -EOPNOTSUPP;

		kfree(pwep);

		if (ret < 0)
			goto exit;
	}

	ret = rtw_cfg80211_set_cipher(psecuritypriv, sme->crypto.cipher_group, false);
	if (ret < 0)
		return ret;

	if (sme->crypto.n_akm_suites) {
		ret = rtw_cfg80211_set_key_mgt(psecuritypriv, sme->crypto.akm_suites[0]);
		if (ret < 0)
			goto exit;
	}

	authmode = psecuritypriv->ndisauthtype;
	rtw_set_802_11_authentication_mode(padapter, authmode);

	/* rtw_set_802_11_encryption_mode(padapter, padapter->securitypriv.ndisencryptstatus); */

	if (rtw_set_802_11_connect(padapter, (u8 *)sme->bssid, &ndis_ssid) == false) {
		ret = -1;
		goto exit;
	}

exit:

	rtw_ps_deny_cancel(padapter, PS_DENY_JOIN);

	padapter->mlmepriv.not_indic_disco = false;

	return ret;
}

static int cfg80211_rtw_disconnect(struct wiphy *wiphy, struct net_device *ndev,
				   u16 reason_code)
{
	struct adapter *padapter = rtw_netdev_priv(ndev);

	rtw_set_to_roam(padapter, 0);

	rtw_scan_abort(padapter);
	LeaveAllPowerSaveMode(padapter);
	rtw_disassoc_cmd(padapter, 500, false);

	rtw_indicate_disconnect(padapter);

	rtw_free_assoc_resources(padapter, 1);
	rtw_pwr_wakeup(padapter);

	return 0;
}

static int cfg80211_rtw_set_txpower(struct wiphy *wiphy,
				    struct wireless_dev *wdev,
				    enum nl80211_tx_power_setting type, int mbm)
{
	return 0;
}

static int cfg80211_rtw_get_txpower(struct wiphy *wiphy,
				    struct wireless_dev *wdev, int *dbm)
{
	*dbm = (12);

	return 0;
}

inline bool rtw_cfg80211_pwr_mgmt(struct adapter *adapter)
{
	struct rtw_wdev_priv *rtw_wdev_priv = adapter_wdev_data(adapter);

	return rtw_wdev_priv->power_mgmt;
}

static int cfg80211_rtw_set_power_mgmt(struct wiphy *wiphy,
				       struct net_device *ndev,
				       bool enabled, int timeout)
{
	struct adapter *padapter = rtw_netdev_priv(ndev);
	struct rtw_wdev_priv *rtw_wdev_priv = adapter_wdev_data(padapter);

	rtw_wdev_priv->power_mgmt = enabled;

	if (!enabled)
		LPS_Leave(padapter, "CFG80211_PWRMGMT");

	return 0;
}

static int cfg80211_rtw_set_pmksa(struct wiphy *wiphy,
				  struct net_device *ndev,
				  struct cfg80211_pmksa *pmksa)
{
	u8 index, blInserted = false;
	struct adapter *padapter = rtw_netdev_priv(ndev);
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	if (is_zero_ether_addr((u8 *)pmksa->bssid))
		return -EINVAL;

	blInserted = false;

	/* overwrite PMKID */
	for (index = 0 ; index < NUM_PMKID_CACHE; index++) {
		if (!memcmp(psecuritypriv->PMKIDList[index].Bssid, (u8 *)pmksa->bssid, ETH_ALEN)) {
			memcpy(psecuritypriv->PMKIDList[index].PMKID, (u8 *)pmksa->pmkid, WLAN_PMKID_LEN);
			psecuritypriv->PMKIDList[index].bUsed = true;
			psecuritypriv->PMKIDIndex = index + 1;
			blInserted = true;
			break;
		}
	}

	if (!blInserted) {
		memcpy(psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].Bssid, (u8 *)pmksa->bssid, ETH_ALEN);
		memcpy(psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].PMKID, (u8 *)pmksa->pmkid, WLAN_PMKID_LEN);

		psecuritypriv->PMKIDList[psecuritypriv->PMKIDIndex].bUsed = true;
		psecuritypriv->PMKIDIndex++;
		if (psecuritypriv->PMKIDIndex == 16)
			psecuritypriv->PMKIDIndex = 0;
	}

	return 0;
}

static int cfg80211_rtw_del_pmksa(struct wiphy *wiphy,
				  struct net_device *ndev,
				  struct cfg80211_pmksa *pmksa)
{
	u8 index, bMatched = false;
	struct adapter *padapter = rtw_netdev_priv(ndev);
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	for (index = 0 ; index < NUM_PMKID_CACHE; index++) {
		if (!memcmp(psecuritypriv->PMKIDList[index].Bssid, (u8 *)pmksa->bssid, ETH_ALEN)) {
			/*
			 * BSSID is matched, the same AP => Remove this PMKID information
			 * and reset it.
			 */
			eth_zero_addr(psecuritypriv->PMKIDList[index].Bssid);
			memset(psecuritypriv->PMKIDList[index].PMKID, 0x00, WLAN_PMKID_LEN);
			psecuritypriv->PMKIDList[index].bUsed = false;
			bMatched = true;
			break;
		}
	}

	if (!bMatched)
		return -EINVAL;

	return 0;
}

static int cfg80211_rtw_flush_pmksa(struct wiphy *wiphy,
				    struct net_device *ndev)
{
	struct adapter *padapter = rtw_netdev_priv(ndev);
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	memset(&psecuritypriv->PMKIDList[0], 0x00, sizeof(struct rt_pmkid_list) * NUM_PMKID_CACHE);
	psecuritypriv->PMKIDIndex = 0;

	return 0;
}

void rtw_cfg80211_indicate_sta_assoc(struct adapter *padapter, u8 *pmgmt_frame, uint frame_len)
{
	struct net_device *ndev = padapter->pnetdev;

	{
		struct station_info sinfo = {};
		u8 ie_offset;

		if (GetFrameSubType(pmgmt_frame) == WIFI_ASSOCREQ)
			ie_offset = _ASOCREQ_IE_OFFSET_;
		else /*  WIFI_REASSOCREQ */
			ie_offset = _REASOCREQ_IE_OFFSET_;

		sinfo.filled = 0;
		sinfo.assoc_req_ies = pmgmt_frame + WLAN_HDR_A3_LEN + ie_offset;
		sinfo.assoc_req_ies_len = frame_len - WLAN_HDR_A3_LEN - ie_offset;
		cfg80211_new_sta(ndev, GetAddr2Ptr(pmgmt_frame), &sinfo, GFP_ATOMIC);
	}
}

void rtw_cfg80211_indicate_sta_disassoc(struct adapter *padapter, unsigned char *da, unsigned short reason)
{
	struct net_device *ndev = padapter->pnetdev;

	cfg80211_del_sta(ndev, da, GFP_ATOMIC);
}

static u8 rtw_get_chan_type(struct adapter *adapter)
{
	struct mlme_ext_priv *mlme_ext = &adapter->mlmeextpriv;

	switch (mlme_ext->cur_bwmode) {
	case CHANNEL_WIDTH_20:
		if (is_supported_ht(adapter->registrypriv.wireless_mode))
			return NL80211_CHAN_HT20;
		else
			return NL80211_CHAN_NO_HT;
	case CHANNEL_WIDTH_40:
		if (mlme_ext->cur_ch_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
			return NL80211_CHAN_HT40PLUS;
		else
			return NL80211_CHAN_HT40MINUS;
	default:
		return NL80211_CHAN_HT20;
	}

	return NL80211_CHAN_HT20;
}

static int cfg80211_rtw_get_channel(struct wiphy *wiphy, struct wireless_dev *wdev,
				    unsigned int link_id,
				    struct cfg80211_chan_def *chandef)
{
	struct adapter *adapter = wiphy_to_adapter(wiphy);
	struct registry_priv *registrypriv = &adapter->registrypriv;
	enum nl80211_channel_type chan_type;
	struct ieee80211_channel *chan = NULL;
	int channel;
	int freq;

	if (!adapter->rtw_wdev)
		return -ENODEV;

	channel = rtw_get_oper_ch(adapter);
	if (!channel)
		return -ENODATA;

	freq = rtw_ieee80211_channel_to_frequency(channel, NL80211_BAND_2GHZ);

	chan = ieee80211_get_channel(adapter->rtw_wdev->wiphy, freq);

	if (registrypriv->ht_enable) {
		chan_type = rtw_get_chan_type(adapter);
		cfg80211_chandef_create(chandef, chan, chan_type);
	} else {
		cfg80211_chandef_create(chandef, chan, NL80211_CHAN_NO_HT);
	}

	return 0;
}

static netdev_tx_t rtw_cfg80211_monitor_if_xmit_entry(struct sk_buff *skb, struct net_device *ndev)
{
	int rtap_len;
	int qos_len = 0;
	int dot11_hdr_len = 24;
	int snap_len = 6;
	unsigned char *pdata;
	u16 frame_control;
	unsigned char src_mac_addr[6];
	unsigned char dst_mac_addr[6];
	struct ieee80211_hdr *dot11_hdr;
	struct ieee80211_radiotap_header *rtap_hdr;
	struct adapter *padapter = rtw_netdev_priv(ndev);

	if (!skb)
		goto fail;

	if (unlikely(skb->len < sizeof(struct ieee80211_radiotap_header)))
		goto fail;

	rtap_hdr = (struct ieee80211_radiotap_header *)skb->data;
	if (unlikely(rtap_hdr->it_version))
		goto fail;

	rtap_len = ieee80211_get_radiotap_len(skb->data);
	if (unlikely(skb->len < rtap_len))
		goto fail;

	if (rtap_len != 14)
		goto fail;

	/* Skip the ratio tap header */
	skb_pull(skb, rtap_len);

	dot11_hdr = (struct ieee80211_hdr *)skb->data;
	frame_control = le16_to_cpu(dot11_hdr->frame_control);
	/* Check if the QoS bit is set */
	if ((frame_control & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA) {
		/* Check if this ia a Wireless Distribution System (WDS) frame
		 * which has 4 MAC addresses
		 */
		if (frame_control & 0x0080)
			qos_len = 2;
		if ((frame_control & 0x0300) == 0x0300)
			dot11_hdr_len += 6;

		memcpy(dst_mac_addr, dot11_hdr->addr1, sizeof(dst_mac_addr));
		memcpy(src_mac_addr, dot11_hdr->addr2, sizeof(src_mac_addr));

		/* Skip the 802.11 header, QoS (if any) and SNAP, but leave spaces for
		 * two MAC addresses
		 */
		skb_pull(skb, dot11_hdr_len + qos_len + snap_len - sizeof(src_mac_addr) * 2);
		pdata = (unsigned char *)skb->data;
		memcpy(pdata, dst_mac_addr, sizeof(dst_mac_addr));
		memcpy(pdata + sizeof(dst_mac_addr), src_mac_addr, sizeof(src_mac_addr));

		/* Use the real net device to transmit the packet */
		_rtw_xmit_entry(skb, padapter->pnetdev);
		return NETDEV_TX_OK;

	} else if ((frame_control & (IEEE80211_FCTL_FTYPE | IEEE80211_FCTL_STYPE)) ==
		   (IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ACTION)) {
		/* only for action frames */
		struct xmit_frame		*pmgntframe;
		struct pkt_attrib	*pattrib;
		unsigned char *pframe;
		/* u8 category, action, OUI_Subtype, dialogToken = 0; */
		/* unsigned char *frame_body; */
		struct ieee80211_hdr *pwlanhdr;
		struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);
		struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
		u8 *buf = skb->data;
		u32 len = skb->len;
		u8 category, action;

		if (rtw_action_frame_parse(buf, len, &category, &action) == false)
			goto fail;

		/* starting alloc mgmt frame to dump it */
		pmgntframe = alloc_mgtxmitframe(pxmitpriv);
		if (!pmgntframe)
			goto fail;

		/* update attribute */
		pattrib = &pmgntframe->attrib;
		update_mgntframe_attrib(padapter, pattrib);
		pattrib->retry_ctrl = false;

		memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

		pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

		memcpy(pframe, (void *)buf, len);
		pattrib->pktlen = len;

		pwlanhdr = (struct ieee80211_hdr *)pframe;
		/* update seq number */
		pmlmeext->mgnt_seq = GetSequence(pwlanhdr);
		pattrib->seqnum = pmlmeext->mgnt_seq;
		pmlmeext->mgnt_seq++;

		pattrib->last_txcmdsz = pattrib->pktlen;

		dump_mgntframe(padapter, pmgntframe);
	}

fail:

	dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

static const struct net_device_ops rtw_cfg80211_monitor_if_ops = {
	.ndo_start_xmit = rtw_cfg80211_monitor_if_xmit_entry,
};

static int rtw_cfg80211_add_monitor_if(struct adapter *padapter, char *name, struct net_device **ndev)
{
	int ret = 0;
	struct net_device *mon_ndev = NULL;
	struct wireless_dev *mon_wdev = NULL;
	struct rtw_netdev_priv_indicator *pnpi;
	struct rtw_wdev_priv *pwdev_priv = adapter_wdev_data(padapter);

	if (!name) {
		ret = -EINVAL;
		goto out;
	}

	if (pwdev_priv->pmon_ndev) {
		ret = -EBUSY;
		goto out;
	}

	mon_ndev = alloc_etherdev(sizeof(struct rtw_netdev_priv_indicator));
	if (!mon_ndev) {
		ret = -ENOMEM;
		goto out;
	}

	mon_ndev->type = ARPHRD_IEEE80211_RADIOTAP;
	strscpy(mon_ndev->name, name);
	mon_ndev->needs_free_netdev = true;
	mon_ndev->priv_destructor = rtw_ndev_destructor;

	mon_ndev->netdev_ops = &rtw_cfg80211_monitor_if_ops;

	pnpi = netdev_priv(mon_ndev);
	pnpi->priv = padapter;
	pnpi->sizeof_priv = sizeof(struct adapter);

	/*  wdev */
	mon_wdev = rtw_zmalloc(sizeof(struct wireless_dev));
	if (!mon_wdev) {
		ret = -ENOMEM;
		goto out;
	}

	mon_wdev->wiphy = padapter->rtw_wdev->wiphy;
	mon_wdev->netdev = mon_ndev;
	mon_wdev->iftype = NL80211_IFTYPE_MONITOR;
	mon_ndev->ieee80211_ptr = mon_wdev;

	ret = cfg80211_register_netdevice(mon_ndev);
	if (ret)
		goto out;

	*ndev = pwdev_priv->pmon_ndev = mon_ndev;
	memcpy(pwdev_priv->ifname_mon, name, IFNAMSIZ + 1);

out:
	if (ret && mon_wdev) {
		kfree(mon_wdev);
		mon_wdev = NULL;
	}

	if (ret && mon_ndev) {
		free_netdev(mon_ndev);
		*ndev = mon_ndev = NULL;
	}

	return ret;
}

static struct wireless_dev *
	cfg80211_rtw_add_virtual_intf(
		struct wiphy *wiphy,
		const char *name,
		unsigned char name_assign_type,
		enum nl80211_iftype type, struct vif_params *params)
{
	int ret = 0;
	struct net_device *ndev = NULL;
	struct adapter *padapter = wiphy_to_adapter(wiphy);

	switch (type) {
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP_VLAN:
	case NL80211_IFTYPE_WDS:
	case NL80211_IFTYPE_MESH_POINT:
		ret = -ENODEV;
		break;
	case NL80211_IFTYPE_MONITOR:
		ret = rtw_cfg80211_add_monitor_if(padapter, (char *)name, &ndev);
		break;
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_STATION:
		ret = -ENODEV;
		break;
	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_AP:
		ret = -ENODEV;
		break;
	default:
		ret = -ENODEV;
		break;
	}

	return ndev ? ndev->ieee80211_ptr : ERR_PTR(ret);
}

static int cfg80211_rtw_del_virtual_intf(struct wiphy *wiphy,
					 struct wireless_dev *wdev
)
{
	struct net_device *ndev = wdev_to_ndev(wdev);
	int ret = 0;
	struct adapter *adapter;
	struct rtw_wdev_priv *pwdev_priv;

	if (!ndev) {
		ret = -EINVAL;
		goto exit;
	}

	adapter = rtw_netdev_priv(ndev);
	pwdev_priv = adapter_wdev_data(adapter);

	cfg80211_unregister_netdevice(ndev);

	if (ndev == pwdev_priv->pmon_ndev) {
		pwdev_priv->pmon_ndev = NULL;
		pwdev_priv->ifname_mon[0] = '\0';
	}

exit:
	return ret;
}

static int rtw_add_beacon(struct adapter *adapter, const u8 *head, size_t head_len, const u8 *tail, size_t tail_len)
{
	int ret = 0;
	u8 *pbuf = NULL;
	uint len, wps_ielen = 0;
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) != true)
		return -EINVAL;

	if (head_len < 24)
		return -EINVAL;

	pbuf = rtw_zmalloc(head_len + tail_len);
	if (!pbuf)
		return -ENOMEM;

	memcpy(pbuf, (void *)head + 24, head_len - 24);/*  24 =beacon header len. */
	memcpy(pbuf + head_len - 24, (void *)tail, tail_len);

	len = head_len + tail_len - 24;

	/* check wps ie if inclued */
	rtw_get_wps_ie(pbuf + _FIXED_IE_LENGTH_, len - _FIXED_IE_LENGTH_, NULL, &wps_ielen);

	/* pbss_network->ies will not include p2p_ie, wfd ie */
	rtw_ies_remove_ie(pbuf, &len, _BEACON_IE_OFFSET_, WLAN_EID_VENDOR_SPECIFIC, P2P_OUI, 4);
	rtw_ies_remove_ie(pbuf, &len, _BEACON_IE_OFFSET_, WLAN_EID_VENDOR_SPECIFIC, WFD_OUI, 4);

	if (rtw_check_beacon_data(adapter, pbuf,  len) == _SUCCESS)
		ret = 0;
	else
		ret = -EINVAL;

	kfree(pbuf);

	return ret;
}

static int cfg80211_rtw_start_ap(struct wiphy *wiphy, struct net_device *ndev,
				 struct cfg80211_ap_settings *settings)
{
	int ret = 0;
	struct adapter *adapter = rtw_netdev_priv(ndev);

	ret = rtw_add_beacon(adapter, settings->beacon.head,
			     settings->beacon.head_len, settings->beacon.tail,
			     settings->beacon.tail_len);

	adapter->mlmeextpriv.mlmext_info.hidden_ssid_mode = settings->hidden_ssid;

	if (settings->ssid && settings->ssid_len) {
		struct wlan_bssid_ex *pbss_network = &adapter->mlmepriv.cur_network.network;
		struct wlan_bssid_ex *pbss_network_ext = &adapter->mlmeextpriv.mlmext_info.network;

		memcpy(pbss_network->ssid.ssid, (void *)settings->ssid, settings->ssid_len);
		pbss_network->ssid.ssid_length = settings->ssid_len;
		memcpy(pbss_network_ext->ssid.ssid, (void *)settings->ssid, settings->ssid_len);
		pbss_network_ext->ssid.ssid_length = settings->ssid_len;
	}

	return ret;
}

static int cfg80211_rtw_change_beacon(struct wiphy *wiphy, struct net_device *ndev,
		struct cfg80211_ap_update *info)
{
	struct adapter *adapter = rtw_netdev_priv(ndev);

	return rtw_add_beacon(adapter, info->beacon.head,
			      info->beacon.head_len, info->beacon.tail,
			      info->beacon.tail_len);
}

static int cfg80211_rtw_stop_ap(struct wiphy *wiphy, struct net_device *ndev,
				unsigned int link_id)
{
	return 0;
}

static int	cfg80211_rtw_add_station(struct wiphy *wiphy,
					 struct net_device *ndev,
					 const u8 *mac,
					 struct station_parameters *params)
{
	return 0;
}

static int cfg80211_rtw_del_station(struct wiphy *wiphy, struct net_device *ndev,
				    struct station_del_parameters *params)
{
	int ret = 0;
	struct list_head *phead, *plist, *tmp;
	u8 updated = false;
	struct sta_info *psta = NULL;
	struct adapter *padapter = rtw_netdev_priv(ndev);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct sta_priv *pstapriv = &padapter->stapriv;
	const u8 *mac = params->mac;

	if (check_fwstate(pmlmepriv, (_FW_LINKED | WIFI_AP_STATE)) != true)
		return -EINVAL;

	if (!mac) {
		flush_all_cam_entry(padapter);	/* clear CAM */

		rtw_sta_flush(padapter);

		return 0;
	}

	if (mac[0] == 0xff && mac[1] == 0xff &&
	    mac[2] == 0xff && mac[3] == 0xff &&
	    mac[4] == 0xff && mac[5] == 0xff) {
		return -EINVAL;
	}

	spin_lock_bh(&pstapriv->asoc_list_lock);

	phead = &pstapriv->asoc_list;
	/* check asoc_queue */
	list_for_each_safe(plist, tmp, phead) {
		psta = list_entry(plist, struct sta_info, asoc_list);

		if (!memcmp((u8 *)mac, psta->hwaddr, ETH_ALEN)) {
			if (psta->dot8021xalg != 1 || psta->bpairwise_key_installed) {
				list_del_init(&psta->asoc_list);
				pstapriv->asoc_list_cnt--;

				updated = ap_free_sta(padapter, psta, true, WLAN_REASON_DEAUTH_LEAVING);

				psta = NULL;

				break;
			}
		}
	}

	spin_unlock_bh(&pstapriv->asoc_list_lock);

	associated_clients_update(padapter, updated);

	return ret;
}

static int cfg80211_rtw_change_station(struct wiphy *wiphy,
				       struct net_device *ndev,
				       const u8 *mac,
				       struct station_parameters *params)
{
	return 0;
}

static struct sta_info *rtw_sta_info_get_by_idx(const int idx, struct sta_priv *pstapriv)

{
	struct list_head	*phead, *plist;
	struct sta_info *psta = NULL;
	int i = 0;

	phead = &pstapriv->asoc_list;
	plist = get_next(phead);

	/* check asoc_queue */
	while (phead != plist) {
		if (idx == i)
			psta = container_of(plist, struct sta_info, asoc_list);
		plist = get_next(plist);
		i++;
	}
	return psta;
}

static int	cfg80211_rtw_dump_station(struct wiphy *wiphy,
					  struct net_device *ndev,
					  int idx, u8 *mac,
					  struct station_info *sinfo)
{
	int ret = 0;
	struct adapter *padapter = rtw_netdev_priv(ndev);
	struct sta_info *psta = NULL;
	struct sta_priv *pstapriv = &padapter->stapriv;

	spin_lock_bh(&pstapriv->asoc_list_lock);
	psta = rtw_sta_info_get_by_idx(idx, pstapriv);
	spin_unlock_bh(&pstapriv->asoc_list_lock);
	if (psta == NULL) {
		ret = -ENOENT;
		goto exit;
	}
	memcpy(mac, psta->hwaddr, ETH_ALEN);
	sinfo->filled = BIT_ULL(NL80211_STA_INFO_SIGNAL);
	sinfo->signal = psta->rssi;

exit:
	return ret;
}

static int	cfg80211_rtw_change_bss(struct wiphy *wiphy,
					struct net_device *ndev,
					struct bss_parameters *params)
{
	return 0;
}

void rtw_cfg80211_rx_action(struct adapter *adapter, u8 *frame, uint frame_len, const char *msg)
{
	s32 freq;
	int channel;
	u8 category, action;

	channel = rtw_get_oper_ch(adapter);

	rtw_action_frame_parse(frame, frame_len, &category, &action);

	freq = rtw_ieee80211_channel_to_frequency(channel, NL80211_BAND_2GHZ);

	rtw_cfg80211_rx_mgmt(adapter, freq, 0, frame, frame_len, GFP_ATOMIC);
}

static int _cfg80211_rtw_mgmt_tx(struct adapter *padapter, u8 tx_ch, const u8 *buf, size_t len)
{
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	unsigned char *pframe;
	int ret = _FAIL;
	bool __maybe_unused ack = true;
	struct ieee80211_hdr *pwlanhdr;
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);

	rtw_set_scan_deny(padapter, 1000);

	rtw_scan_abort(padapter);
	if (tx_ch != rtw_get_oper_ch(padapter)) {
		if (!check_fwstate(&padapter->mlmepriv, _FW_LINKED))
			pmlmeext->cur_channel = tx_ch;
		set_channel_bwmode(padapter, tx_ch, HAL_PRIME_CHNL_OFFSET_DONT_CARE, CHANNEL_WIDTH_20);
	}

	/* starting alloc mgmt frame to dump it */
	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe) {
		/* ret = -ENOMEM; */
		ret = _FAIL;
		goto exit;
	}

	/* update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->retry_ctrl = false;

	memset(pmgntframe->buf_addr, 0, WLANHDR_OFFSET + TXDESC_OFFSET);

	pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;

	memcpy(pframe, (void *)buf, len);
	pattrib->pktlen = len;

	pwlanhdr = (struct ieee80211_hdr *)pframe;
	/* update seq number */
	pmlmeext->mgnt_seq = GetSequence(pwlanhdr);
	pattrib->seqnum = pmlmeext->mgnt_seq;
	pmlmeext->mgnt_seq++;

	pattrib->last_txcmdsz = pattrib->pktlen;

	if (dump_mgntframe_and_wait_ack(padapter, pmgntframe) != _SUCCESS) {
		ack = false;
		ret = _FAIL;

	} else {
		msleep(50);

		ret = _SUCCESS;
	}

exit:

	return ret;
}

static int cfg80211_rtw_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
				struct cfg80211_mgmt_tx_params *params,
				u64 *cookie)
{
	struct net_device *ndev = wdev_to_ndev(wdev);
	struct ieee80211_channel *chan = params->chan;
	const u8 *buf = params->buf;
	size_t len = params->len;
	int ret = 0;
	int tx_ret;
	u32 dump_limit = RTW_MAX_MGMT_TX_CNT;
	u32 dump_cnt = 0;
	bool ack = true;
	u8 tx_ch = (u8)ieee80211_frequency_to_channel(chan->center_freq);
	u8 category, action;
	struct adapter *padapter;

	if (!ndev) {
		ret = -EINVAL;
		goto exit;
	}

	padapter = rtw_netdev_priv(ndev);

	/* cookie generation */
	*cookie = (unsigned long)buf;

	/* indicate ack before issue frame to avoid racing with rsp frame */
	rtw_cfg80211_mgmt_tx_status(padapter, *cookie, buf, len, ack, GFP_KERNEL);

	if (rtw_action_frame_parse(buf, len, &category, &action) == false)
		goto exit;

	rtw_ps_deny(padapter, PS_DENY_MGNT_TX);
	if (rtw_pwr_wakeup(padapter) == _FAIL) {
		ret = -EFAULT;
		goto cancel_ps_deny;
	}

	do {
		dump_cnt++;
		tx_ret = _cfg80211_rtw_mgmt_tx(padapter, tx_ch, buf, len);
	} while (dump_cnt < dump_limit && tx_ret != _SUCCESS);

cancel_ps_deny:
	rtw_ps_deny_cancel(padapter, PS_DENY_MGNT_TX);
exit:
	return ret;
}

static void rtw_cfg80211_init_ht_capab(struct ieee80211_sta_ht_cap *ht_cap, enum nl80211_band band)
{
#define MAX_BIT_RATE_40MHZ_MCS7		150	/* Mbps */

	ht_cap->ht_supported = true;

	ht_cap->cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
					IEEE80211_HT_CAP_SGI_40 | IEEE80211_HT_CAP_SGI_20 |
					IEEE80211_HT_CAP_DSSSCCK40 | IEEE80211_HT_CAP_MAX_AMSDU;

	/*
	 *Maximum length of AMPDU that the STA can receive.
	 *Length = 2 ^ (13 + max_ampdu_length_exp) - 1 (octets)
	 */
	ht_cap->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;

	/*Minimum MPDU start spacing , */
	ht_cap->ampdu_density = IEEE80211_HT_MPDU_DENSITY_16;

	ht_cap->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;

	/*
	 *hw->wiphy->bands[NL80211_BAND_2GHZ]
	 *base on ant_num
	 *rx_mask: RX mask
	 *if rx_ant = 1 rx_mask[0]= 0xff;==>MCS0-MCS7
	 *if rx_ant =2 rx_mask[1]= 0xff;==>MCS8-MCS15
	 *if rx_ant >=3 rx_mask[2]= 0xff;
	 *if BW_40 rx_mask[4]= 0x01;
	 *highest supported RX rate
	 */
	ht_cap->mcs.rx_mask[0] = 0xFF;
	ht_cap->mcs.rx_mask[1] = 0x00;
	ht_cap->mcs.rx_mask[4] = 0x01;

	ht_cap->mcs.rx_highest = cpu_to_le16(MAX_BIT_RATE_40MHZ_MCS7);
}

void rtw_cfg80211_init_wiphy(struct adapter *padapter)
{
	struct ieee80211_supported_band *bands;
	struct wireless_dev *pwdev = padapter->rtw_wdev;
	struct wiphy *wiphy = pwdev->wiphy;

	{
		bands = wiphy->bands[NL80211_BAND_2GHZ];
		if (bands)
			rtw_cfg80211_init_ht_capab(&bands->ht_cap, NL80211_BAND_2GHZ);
	}

	/* copy mac_addr to wiphy */
	memcpy(wiphy->perm_addr, padapter->eeprompriv.mac_addr, ETH_ALEN);
}

static void rtw_cfg80211_preinit_wiphy(struct adapter *padapter, struct wiphy *wiphy)
{
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

	wiphy->max_scan_ssids = RTW_SSID_SCAN_AMOUNT;
	wiphy->max_scan_ie_len = RTW_SCAN_IE_LEN_MAX;
	wiphy->max_num_pmkids = RTW_MAX_NUM_PMKIDS;

	wiphy->max_remain_on_channel_duration = RTW_MAX_REMAIN_ON_CHANNEL_DURATION;

	wiphy->interface_modes =	BIT(NL80211_IFTYPE_STATION)
								| BIT(NL80211_IFTYPE_ADHOC)
								| BIT(NL80211_IFTYPE_AP)
								| BIT(NL80211_IFTYPE_MONITOR)
								;

	wiphy->mgmt_stypes = rtw_cfg80211_default_mgmt_stypes;

	wiphy->software_iftypes |= BIT(NL80211_IFTYPE_MONITOR);

	wiphy->cipher_suites = rtw_cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(rtw_cipher_suites);

	/* if (padapter->registrypriv.wireless_mode & WIRELESS_11G) */
	wiphy->bands[NL80211_BAND_2GHZ] = rtw_spt_band_alloc(NL80211_BAND_2GHZ);

	wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
	wiphy->flags |= WIPHY_FLAG_OFFCHAN_TX | WIPHY_FLAG_HAVE_AP_SME;

#if defined(CONFIG_PM)
	wiphy->max_sched_scan_reqs = 1;
#endif

#if defined(CONFIG_PM)
	wiphy->wowlan = &wowlan_stub;
#endif

	if (padapter->registrypriv.power_mgnt != PS_MODE_ACTIVE)
		wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;
	else
		wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;
}

static struct cfg80211_ops rtw_cfg80211_ops = {
	.change_virtual_intf = cfg80211_rtw_change_iface,
	.add_key = cfg80211_rtw_add_key,
	.get_key = cfg80211_rtw_get_key,
	.del_key = cfg80211_rtw_del_key,
	.set_default_key = cfg80211_rtw_set_default_key,
	.get_station = cfg80211_rtw_get_station,
	.scan = cfg80211_rtw_scan,
	.set_wiphy_params = cfg80211_rtw_set_wiphy_params,
	.connect = cfg80211_rtw_connect,
	.disconnect = cfg80211_rtw_disconnect,
	.join_ibss = cfg80211_rtw_join_ibss,
	.leave_ibss = cfg80211_rtw_leave_ibss,
	.set_tx_power = cfg80211_rtw_set_txpower,
	.get_tx_power = cfg80211_rtw_get_txpower,
	.set_power_mgmt = cfg80211_rtw_set_power_mgmt,
	.set_pmksa = cfg80211_rtw_set_pmksa,
	.del_pmksa = cfg80211_rtw_del_pmksa,
	.flush_pmksa = cfg80211_rtw_flush_pmksa,
	.get_channel = cfg80211_rtw_get_channel,
	.add_virtual_intf = cfg80211_rtw_add_virtual_intf,
	.del_virtual_intf = cfg80211_rtw_del_virtual_intf,

	.start_ap = cfg80211_rtw_start_ap,
	.change_beacon = cfg80211_rtw_change_beacon,
	.stop_ap = cfg80211_rtw_stop_ap,

	.add_station = cfg80211_rtw_add_station,
	.del_station = cfg80211_rtw_del_station,
	.change_station = cfg80211_rtw_change_station,
	.dump_station = cfg80211_rtw_dump_station,
	.change_bss = cfg80211_rtw_change_bss,

	.mgmt_tx = cfg80211_rtw_mgmt_tx,
};

int rtw_wdev_alloc(struct adapter *padapter, struct device *dev)
{
	int ret = 0;
	struct wiphy *wiphy;
	struct wireless_dev *wdev;
	struct rtw_wdev_priv *pwdev_priv;
	struct net_device *pnetdev = padapter->pnetdev;

	/* wiphy */
	wiphy = wiphy_new(&rtw_cfg80211_ops, sizeof(struct adapter *));
	if (!wiphy) {
		ret = -ENOMEM;
		goto exit;
	}
	set_wiphy_dev(wiphy, dev);
	*((struct adapter **)wiphy_priv(wiphy)) = padapter;
	rtw_cfg80211_preinit_wiphy(padapter, wiphy);

	/* init regulary domain */
	rtw_regd_init(wiphy, rtw_reg_notifier);

	ret = wiphy_register(wiphy);
	if (ret < 0)
		goto free_wiphy;

	/*  wdev */
	wdev = rtw_zmalloc(sizeof(struct wireless_dev));
	if (!wdev) {
		ret = -ENOMEM;
		goto unregister_wiphy;
	}
	wdev->wiphy = wiphy;
	wdev->netdev = pnetdev;

	wdev->iftype = NL80211_IFTYPE_STATION; /*  will be init in rtw_hal_init() */
					   /*  Must sync with _rtw_init_mlme_priv() */
					   /*  pmlmepriv->fw_state = WIFI_STATION_STATE */
	padapter->rtw_wdev = wdev;
	pnetdev->ieee80211_ptr = wdev;

	/* init pwdev_priv */
	pwdev_priv = adapter_wdev_data(padapter);
	pwdev_priv->rtw_wdev = wdev;
	pwdev_priv->pmon_ndev = NULL;
	pwdev_priv->ifname_mon[0] = '\0';
	pwdev_priv->padapter = padapter;
	pwdev_priv->scan_request = NULL;
	spin_lock_init(&pwdev_priv->scan_req_lock);

	pwdev_priv->p2p_enabled = false;
	pwdev_priv->provdisc_req_issued = false;
	rtw_wdev_invit_info_init(&pwdev_priv->invit_info);
	rtw_wdev_nego_info_init(&pwdev_priv->nego_info);

	pwdev_priv->bandroid_scan = false;

	if (padapter->registrypriv.power_mgnt != PS_MODE_ACTIVE)
		pwdev_priv->power_mgmt = true;
	else
		pwdev_priv->power_mgmt = false;

	return ret;

unregister_wiphy:
	wiphy_unregister(wiphy);
 free_wiphy:
	wiphy_free(wiphy);
exit:
	return ret;
}

void rtw_wdev_free(struct wireless_dev *wdev)
{
	if (!wdev)
		return;

	kfree(wdev->wiphy->bands[NL80211_BAND_2GHZ]);

	wiphy_free(wdev->wiphy);

	kfree(wdev);
}

void rtw_wdev_unregister(struct wireless_dev *wdev)
{
	struct net_device *ndev;
	struct adapter *adapter;
	struct rtw_wdev_priv *pwdev_priv;

	if (!wdev)
		return;
	ndev = wdev_to_ndev(wdev);
	if (!ndev)
		return;

	adapter = rtw_netdev_priv(ndev);
	pwdev_priv = adapter_wdev_data(adapter);

	rtw_cfg80211_indicate_scan_done(adapter, true);

	if (pwdev_priv->pmon_ndev)
		unregister_netdev(pwdev_priv->pmon_ndev);

	wiphy_unregister(wdev->wiphy);
}
