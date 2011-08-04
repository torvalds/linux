/*
 * Copyright (c) 2004-2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "core.h"
#include "cfg80211.h"
#include "debug.h"

#define RATETAB_ENT(_rate, _rateid, _flags) {   \
	.bitrate    = (_rate),                  \
	.flags      = (_flags),                 \
	.hw_value   = (_rateid),                \
}

#define CHAN2G(_channel, _freq, _flags) {   \
	.band           = IEEE80211_BAND_2GHZ,  \
	.hw_value       = (_channel),           \
	.center_freq    = (_freq),              \
	.flags          = (_flags),             \
	.max_antenna_gain   = 0,                \
	.max_power      = 30,                   \
}

#define CHAN5G(_channel, _flags) {		    \
	.band           = IEEE80211_BAND_5GHZ,      \
	.hw_value       = (_channel),               \
	.center_freq    = 5000 + (5 * (_channel)),  \
	.flags          = (_flags),                 \
	.max_antenna_gain   = 0,                    \
	.max_power      = 30,                       \
}

static struct ieee80211_rate ath6kl_rates[] = {
	RATETAB_ENT(10, 0x1, 0),
	RATETAB_ENT(20, 0x2, 0),
	RATETAB_ENT(55, 0x4, 0),
	RATETAB_ENT(110, 0x8, 0),
	RATETAB_ENT(60, 0x10, 0),
	RATETAB_ENT(90, 0x20, 0),
	RATETAB_ENT(120, 0x40, 0),
	RATETAB_ENT(180, 0x80, 0),
	RATETAB_ENT(240, 0x100, 0),
	RATETAB_ENT(360, 0x200, 0),
	RATETAB_ENT(480, 0x400, 0),
	RATETAB_ENT(540, 0x800, 0),
};

#define ath6kl_a_rates     (ath6kl_rates + 4)
#define ath6kl_a_rates_size    8
#define ath6kl_g_rates     (ath6kl_rates + 0)
#define ath6kl_g_rates_size    12

static struct ieee80211_channel ath6kl_2ghz_channels[] = {
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

static struct ieee80211_channel ath6kl_5ghz_a_channels[] = {
	CHAN5G(34, 0), CHAN5G(36, 0),
	CHAN5G(38, 0), CHAN5G(40, 0),
	CHAN5G(42, 0), CHAN5G(44, 0),
	CHAN5G(46, 0), CHAN5G(48, 0),
	CHAN5G(52, 0), CHAN5G(56, 0),
	CHAN5G(60, 0), CHAN5G(64, 0),
	CHAN5G(100, 0), CHAN5G(104, 0),
	CHAN5G(108, 0), CHAN5G(112, 0),
	CHAN5G(116, 0), CHAN5G(120, 0),
	CHAN5G(124, 0), CHAN5G(128, 0),
	CHAN5G(132, 0), CHAN5G(136, 0),
	CHAN5G(140, 0), CHAN5G(149, 0),
	CHAN5G(153, 0), CHAN5G(157, 0),
	CHAN5G(161, 0), CHAN5G(165, 0),
	CHAN5G(184, 0), CHAN5G(188, 0),
	CHAN5G(192, 0), CHAN5G(196, 0),
	CHAN5G(200, 0), CHAN5G(204, 0),
	CHAN5G(208, 0), CHAN5G(212, 0),
	CHAN5G(216, 0),
};

static struct ieee80211_supported_band ath6kl_band_2ghz = {
	.n_channels = ARRAY_SIZE(ath6kl_2ghz_channels),
	.channels = ath6kl_2ghz_channels,
	.n_bitrates = ath6kl_g_rates_size,
	.bitrates = ath6kl_g_rates,
};

static struct ieee80211_supported_band ath6kl_band_5ghz = {
	.n_channels = ARRAY_SIZE(ath6kl_5ghz_a_channels),
	.channels = ath6kl_5ghz_a_channels,
	.n_bitrates = ath6kl_a_rates_size,
	.bitrates = ath6kl_a_rates,
};

static int ath6kl_set_wpa_version(struct ath6kl *ar,
				  enum nl80211_wpa_versions wpa_version)
{
	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "%s: %u\n", __func__, wpa_version);

	if (!wpa_version) {
		ar->auth_mode = NONE_AUTH;
	} else if (wpa_version & NL80211_WPA_VERSION_2) {
		ar->auth_mode = WPA2_AUTH;
	} else if (wpa_version & NL80211_WPA_VERSION_1) {
		ar->auth_mode = WPA_AUTH;
	} else {
		ath6kl_err("%s: %u not supported\n", __func__, wpa_version);
		return -ENOTSUPP;
	}

	return 0;
}

static int ath6kl_set_auth_type(struct ath6kl *ar,
				enum nl80211_auth_type auth_type)
{

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "%s: 0x%x\n", __func__, auth_type);

	switch (auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		ar->dot11_auth_mode = OPEN_AUTH;
		break;
	case NL80211_AUTHTYPE_SHARED_KEY:
		ar->dot11_auth_mode = SHARED_AUTH;
		break;
	case NL80211_AUTHTYPE_NETWORK_EAP:
		ar->dot11_auth_mode = LEAP_AUTH;
		break;

	case NL80211_AUTHTYPE_AUTOMATIC:
		ar->dot11_auth_mode = OPEN_AUTH;
		ar->auto_auth_stage = AUTH_OPEN_IN_PROGRESS;
		break;

	default:
		ath6kl_err("%s: 0x%x not spported\n", __func__, auth_type);
		return -ENOTSUPP;
	}

	return 0;
}

static int ath6kl_set_cipher(struct ath6kl *ar, u32 cipher, bool ucast)
{
	u8 *ar_cipher = ucast ? &ar->prwise_crypto : &ar->grp_crypto;
	u8 *ar_cipher_len = ucast ? &ar->prwise_crypto_len : &ar->grp_crpto_len;

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "%s: cipher 0x%x, ucast %u\n",
		   __func__, cipher, ucast);

	switch (cipher) {
	case 0:
		/* our own hack to use value 0 as no crypto used */
		*ar_cipher = NONE_CRYPT;
		*ar_cipher_len = 0;
		break;
	case WLAN_CIPHER_SUITE_WEP40:
		*ar_cipher = WEP_CRYPT;
		*ar_cipher_len = 5;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		*ar_cipher = WEP_CRYPT;
		*ar_cipher_len = 13;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		*ar_cipher = TKIP_CRYPT;
		*ar_cipher_len = 0;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		*ar_cipher = AES_CRYPT;
		*ar_cipher_len = 0;
		break;
	default:
		ath6kl_err("cipher 0x%x not supported\n", cipher);
		return -ENOTSUPP;
	}

	return 0;
}

static void ath6kl_set_key_mgmt(struct ath6kl *ar, u32 key_mgmt)
{
	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "%s: 0x%x\n", __func__, key_mgmt);

	if (key_mgmt == WLAN_AKM_SUITE_PSK) {
		if (ar->auth_mode == WPA_AUTH)
			ar->auth_mode = WPA_PSK_AUTH;
		else if (ar->auth_mode == WPA2_AUTH)
			ar->auth_mode = WPA2_PSK_AUTH;
	} else if (key_mgmt != WLAN_AKM_SUITE_8021X) {
		ar->auth_mode = NONE_AUTH;
	}
}

static bool ath6kl_cfg80211_ready(struct ath6kl *ar)
{
	if (!test_bit(WMI_READY, &ar->flag)) {
		ath6kl_err("wmi is not ready\n");
		return false;
	}

	if (!test_bit(WLAN_ENABLED, &ar->flag)) {
		ath6kl_err("wlan disabled\n");
		return false;
	}

	return true;
}

static int ath6kl_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev,
				   struct cfg80211_connect_params *sme)
{
	struct ath6kl *ar = ath6kl_priv(dev);
	int status;

	ar->sme_state = SME_CONNECTING;

	if (!ath6kl_cfg80211_ready(ar))
		return -EIO;

	if (test_bit(DESTROY_IN_PROGRESS, &ar->flag)) {
		ath6kl_err("destroy in progress\n");
		return -EBUSY;
	}

	if (test_bit(SKIP_SCAN, &ar->flag) &&
	    ((sme->channel && sme->channel->center_freq == 0) ||
	     (sme->bssid && is_zero_ether_addr(sme->bssid)))) {
		ath6kl_err("SkipScan: channel or bssid invalid\n");
		return -EINVAL;
	}

	if (down_interruptible(&ar->sem)) {
		ath6kl_err("busy, couldn't get access\n");
		return -ERESTARTSYS;
	}

	if (test_bit(DESTROY_IN_PROGRESS, &ar->flag)) {
		ath6kl_err("busy, destroy in progress\n");
		up(&ar->sem);
		return -EBUSY;
	}

	if (ar->tx_pending[ath6kl_wmi_get_control_ep(ar->wmi)]) {
		/*
		 * sleep until the command queue drains
		 */
		wait_event_interruptible_timeout(ar->event_wq,
			ar->tx_pending[ath6kl_wmi_get_control_ep(ar->wmi)] == 0,
			WMI_TIMEOUT);
		if (signal_pending(current)) {
			ath6kl_err("cmd queue drain timeout\n");
			up(&ar->sem);
			return -EINTR;
		}
	}

	if (test_bit(CONNECTED, &ar->flag) &&
	    ar->ssid_len == sme->ssid_len &&
	    !memcmp(ar->ssid, sme->ssid, ar->ssid_len)) {
		ar->reconnect_flag = true;
		status = ath6kl_wmi_reconnect_cmd(ar->wmi, ar->req_bssid,
						  ar->ch_hint);

		up(&ar->sem);
		if (status) {
			ath6kl_err("wmi_reconnect_cmd failed\n");
			return -EIO;
		}
		return 0;
	} else if (ar->ssid_len == sme->ssid_len &&
		   !memcmp(ar->ssid, sme->ssid, ar->ssid_len)) {
		ath6kl_disconnect(ar);
	}

	memset(ar->ssid, 0, sizeof(ar->ssid));
	ar->ssid_len = sme->ssid_len;
	memcpy(ar->ssid, sme->ssid, sme->ssid_len);

	if (sme->channel)
		ar->ch_hint = sme->channel->center_freq;

	memset(ar->req_bssid, 0, sizeof(ar->req_bssid));
	if (sme->bssid && !is_broadcast_ether_addr(sme->bssid))
		memcpy(ar->req_bssid, sme->bssid, sizeof(ar->req_bssid));

	ath6kl_set_wpa_version(ar, sme->crypto.wpa_versions);

	status = ath6kl_set_auth_type(ar, sme->auth_type);
	if (status) {
		up(&ar->sem);
		return status;
	}

	if (sme->crypto.n_ciphers_pairwise)
		ath6kl_set_cipher(ar, sme->crypto.ciphers_pairwise[0], true);
	else
		ath6kl_set_cipher(ar, 0, true);

	ath6kl_set_cipher(ar, sme->crypto.cipher_group, false);

	if (sme->crypto.n_akm_suites)
		ath6kl_set_key_mgmt(ar, sme->crypto.akm_suites[0]);

	if ((sme->key_len) &&
	    (ar->auth_mode == NONE_AUTH) && (ar->prwise_crypto == WEP_CRYPT)) {
		struct ath6kl_key *key = NULL;

		if (sme->key_idx < WMI_MIN_KEY_INDEX ||
		    sme->key_idx > WMI_MAX_KEY_INDEX) {
			ath6kl_err("key index %d out of bounds\n",
				   sme->key_idx);
			up(&ar->sem);
			return -ENOENT;
		}

		key = &ar->keys[sme->key_idx];
		key->key_len = sme->key_len;
		memcpy(key->key, sme->key, key->key_len);
		key->cipher = ar->prwise_crypto;
		ar->def_txkey_index = sme->key_idx;

		ath6kl_wmi_addkey_cmd(ar->wmi, sme->key_idx,
				      ar->prwise_crypto,
				      GROUP_USAGE | TX_USAGE,
				      key->key_len,
				      NULL,
				      key->key, KEY_OP_INIT_VAL, NULL,
				      NO_SYNC_WMIFLAG);
	}

	if (!ar->usr_bss_filter) {
		if (ath6kl_wmi_bssfilter_cmd(ar->wmi, ALL_BSS_FILTER, 0) != 0) {
			ath6kl_err("couldn't set bss filtering\n");
			up(&ar->sem);
			return -EIO;
		}
	}

	ar->nw_type = ar->next_mode;

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG,
		   "%s: connect called with authmode %d dot11 auth %d"
		   " PW crypto %d PW crypto len %d GRP crypto %d"
		   " GRP crypto len %d channel hint %u\n",
		   __func__,
		   ar->auth_mode, ar->dot11_auth_mode, ar->prwise_crypto,
		   ar->prwise_crypto_len, ar->grp_crypto,
		   ar->grp_crpto_len, ar->ch_hint);

	ar->reconnect_flag = 0;
	status = ath6kl_wmi_connect_cmd(ar->wmi, ar->nw_type,
					ar->dot11_auth_mode, ar->auth_mode,
					ar->prwise_crypto,
					ar->prwise_crypto_len,
					ar->grp_crypto, ar->grp_crpto_len,
					ar->ssid_len, ar->ssid,
					ar->req_bssid, ar->ch_hint,
					ar->connect_ctrl_flags);

	up(&ar->sem);

	if (status == -EINVAL) {
		memset(ar->ssid, 0, sizeof(ar->ssid));
		ar->ssid_len = 0;
		ath6kl_err("invalid request\n");
		return -ENOENT;
	} else if (status) {
		ath6kl_err("ath6kl_wmi_connect_cmd failed\n");
		return -EIO;
	}

	if ((!(ar->connect_ctrl_flags & CONNECT_DO_WPA_OFFLOAD)) &&
	    ((ar->auth_mode == WPA_PSK_AUTH)
	     || (ar->auth_mode == WPA2_PSK_AUTH))) {
		mod_timer(&ar->disconnect_timer,
			  jiffies + msecs_to_jiffies(DISCON_TIMER_INTVAL));
	}

	ar->connect_ctrl_flags &= ~CONNECT_DO_WPA_OFFLOAD;
	set_bit(CONNECT_PEND, &ar->flag);

	return 0;
}

void ath6kl_cfg80211_connect_event(struct ath6kl *ar, u16 channel,
				   u8 *bssid, u16 listen_intvl,
				   u16 beacon_intvl,
				   enum network_type nw_type,
				   u8 beacon_ie_len, u8 assoc_req_len,
				   u8 assoc_resp_len, u8 *assoc_info)
{
	u16 size = 0;
	u16 capability = 0;
	struct cfg80211_bss *bss = NULL;
	struct ieee80211_mgmt *mgmt = NULL;
	struct ieee80211_channel *ibss_ch = NULL;
	s32 signal = 50 * 100;
	u8 ie_buf_len = 0;
	unsigned char ie_buf[256];
	unsigned char *ptr_ie_buf = ie_buf;
	unsigned char *ieeemgmtbuf = NULL;
	u8 source_mac[ETH_ALEN];
	u16 capa_mask;
	u16 capa_val;

	/* capinfo + listen interval */
	u8 assoc_req_ie_offset = sizeof(u16) + sizeof(u16);

	/* capinfo + status code +  associd */
	u8 assoc_resp_ie_offset = sizeof(u16) + sizeof(u16) + sizeof(u16);

	u8 *assoc_req_ie = assoc_info + beacon_ie_len + assoc_req_ie_offset;
	u8 *assoc_resp_ie = assoc_info + beacon_ie_len + assoc_req_len +
	    assoc_resp_ie_offset;

	assoc_req_len -= assoc_req_ie_offset;
	assoc_resp_len -= assoc_resp_ie_offset;

	ar->auto_auth_stage = AUTH_IDLE;

	if (nw_type & ADHOC_NETWORK) {
		if (ar->wdev->iftype != NL80211_IFTYPE_ADHOC) {
			ath6kl_dbg(ATH6KL_DBG_WLAN_CFG,
				   "%s: ath6k not in ibss mode\n", __func__);
			return;
		}
	}

	if (nw_type & INFRA_NETWORK) {
		if (ar->wdev->iftype != NL80211_IFTYPE_STATION) {
			ath6kl_dbg(ATH6KL_DBG_WLAN_CFG,
				   "%s: ath6k not in station mode\n", __func__);
			return;
		}
	}

	if (nw_type & ADHOC_NETWORK) {
		capa_mask = WLAN_CAPABILITY_IBSS;
		capa_val = WLAN_CAPABILITY_IBSS;
	} else {
		capa_mask = WLAN_CAPABILITY_ESS;
		capa_val = WLAN_CAPABILITY_ESS;
	}

	/* Before informing the join/connect event, make sure that
	 * bss entry is present in scan list, if it not present
	 * construct and insert into scan list, otherwise that
	 * event will be dropped on the way by cfg80211, due to
	 * this keys will not be plumbed in case of WEP and
	 * application will not be aware of join/connect status. */
	bss = cfg80211_get_bss(ar->wdev->wiphy, NULL, bssid,
			       ar->wdev->ssid, ar->wdev->ssid_len,
			       capa_mask, capa_val);

	/*
	 * Earlier we were updating the cfg about bss by making a beacon frame
	 * only if the entry for bss is not there. This can have some issue if
	 * ROAM event is generated and a heavy traffic is ongoing. The ROAM
	 * event is handled through a work queue and by the time it really gets
	 * handled, BSS would have been aged out. So it is better to update the
	 * cfg about BSS irrespective of its entry being present right now or
	 * not.
	 */

	if (nw_type & ADHOC_NETWORK) {
		/* construct 802.11 mgmt beacon */
		if (ptr_ie_buf) {
			*ptr_ie_buf++ = WLAN_EID_SSID;
			*ptr_ie_buf++ = ar->ssid_len;
			memcpy(ptr_ie_buf, ar->ssid, ar->ssid_len);
			ptr_ie_buf += ar->ssid_len;

			*ptr_ie_buf++ = WLAN_EID_IBSS_PARAMS;
			*ptr_ie_buf++ = 2;	/* length */
			*ptr_ie_buf++ = 0;	/* ATIM window */
			*ptr_ie_buf++ = 0;	/* ATIM window */

			/* TODO: update ibss params and include supported rates,
			 * DS param set, extened support rates, wmm. */

			ie_buf_len = ptr_ie_buf - ie_buf;
		}

		capability |= WLAN_CAPABILITY_IBSS;

		if (ar->prwise_crypto == WEP_CRYPT)
			capability |= WLAN_CAPABILITY_PRIVACY;

		memcpy(source_mac, ar->net_dev->dev_addr, ETH_ALEN);
		ptr_ie_buf = ie_buf;
	} else {
		capability = *(u16 *) (&assoc_info[beacon_ie_len]);
		memcpy(source_mac, bssid, ETH_ALEN);
		ptr_ie_buf = assoc_req_ie;
		ie_buf_len = assoc_req_len;
	}

	size = offsetof(struct ieee80211_mgmt, u)
	+ sizeof(mgmt->u.beacon)
	+ ie_buf_len;

	ieeemgmtbuf = kzalloc(size, GFP_ATOMIC);
	if (!ieeemgmtbuf) {
		ath6kl_err("ieee mgmt buf alloc error\n");
		cfg80211_put_bss(bss);
		return;
	}

	mgmt = (struct ieee80211_mgmt *)ieeemgmtbuf;
	mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT |
					  IEEE80211_STYPE_BEACON);
	memset(mgmt->da, 0xff, ETH_ALEN);	/* broadcast addr */
	memcpy(mgmt->sa, source_mac, ETH_ALEN);
	memcpy(mgmt->bssid, bssid, ETH_ALEN);
	mgmt->u.beacon.beacon_int = cpu_to_le16(beacon_intvl);
	mgmt->u.beacon.capab_info = cpu_to_le16(capability);
	memcpy(mgmt->u.beacon.variable, ptr_ie_buf, ie_buf_len);

	ibss_ch = ieee80211_get_channel(ar->wdev->wiphy, (int)channel);

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG,
		   "%s: inform bss with bssid %pM channel %d beacon_intvl %d capability 0x%x\n",
		   __func__, mgmt->bssid, ibss_ch->hw_value,
		   beacon_intvl, capability);

	bss = cfg80211_inform_bss_frame(ar->wdev->wiphy,
					ibss_ch, mgmt,
					size, signal, GFP_KERNEL);
	kfree(ieeemgmtbuf);
	cfg80211_put_bss(bss);

	if (nw_type & ADHOC_NETWORK) {
		cfg80211_ibss_joined(ar->net_dev, bssid, GFP_KERNEL);
		return;
	}

	if (ar->sme_state == SME_CONNECTING) {
		/* inform connect result to cfg80211 */
		ar->sme_state = SME_CONNECTED;
		cfg80211_connect_result(ar->net_dev, bssid,
					assoc_req_ie, assoc_req_len,
					assoc_resp_ie, assoc_resp_len,
					WLAN_STATUS_SUCCESS, GFP_KERNEL);
	} else if (ar->sme_state == SME_CONNECTED) {
		/* inform roam event to cfg80211 */
		cfg80211_roamed(ar->net_dev, ibss_ch, bssid,
				assoc_req_ie, assoc_req_len,
				assoc_resp_ie, assoc_resp_len, GFP_KERNEL);
	}
}

static int ath6kl_cfg80211_disconnect(struct wiphy *wiphy,
				      struct net_device *dev, u16 reason_code)
{
	struct ath6kl *ar = (struct ath6kl *)ath6kl_priv(dev);

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "%s: reason=%u\n", __func__,
		   reason_code);

	if (!ath6kl_cfg80211_ready(ar))
		return -EIO;

	if (test_bit(DESTROY_IN_PROGRESS, &ar->flag)) {
		ath6kl_err("busy, destroy in progress\n");
		return -EBUSY;
	}

	if (down_interruptible(&ar->sem)) {
		ath6kl_err("busy, couldn't get access\n");
		return -ERESTARTSYS;
	}

	ar->reconnect_flag = 0;
	ath6kl_disconnect(ar);
	memset(ar->ssid, 0, sizeof(ar->ssid));
	ar->ssid_len = 0;

	if (!test_bit(SKIP_SCAN, &ar->flag))
		memset(ar->req_bssid, 0, sizeof(ar->req_bssid));

	up(&ar->sem);

	return 0;
}

void ath6kl_cfg80211_disconnect_event(struct ath6kl *ar, u8 reason,
				      u8 *bssid, u8 assoc_resp_len,
				      u8 *assoc_info, u16 proto_reason)
{
	struct ath6kl_key *key = NULL;
	u16 status;

	if (ar->scan_req) {
		cfg80211_scan_done(ar->scan_req, true);
		ar->scan_req = NULL;
	}

	if (ar->nw_type & ADHOC_NETWORK) {
		if (ar->wdev->iftype != NL80211_IFTYPE_ADHOC) {
			ath6kl_dbg(ATH6KL_DBG_WLAN_CFG,
				   "%s: ath6k not in ibss mode\n", __func__);
			return;
		}
		memset(bssid, 0, ETH_ALEN);
		cfg80211_ibss_joined(ar->net_dev, bssid, GFP_KERNEL);
		return;
	}

	if (ar->nw_type & INFRA_NETWORK) {
		if (ar->wdev->iftype != NL80211_IFTYPE_STATION) {
			ath6kl_dbg(ATH6KL_DBG_WLAN_CFG,
				   "%s: ath6k not in station mode\n", __func__);
			return;
		}
	}

	if (!test_bit(CONNECT_PEND, &ar->flag)) {
		if (reason != DISCONNECT_CMD)
			ath6kl_wmi_disconnect_cmd(ar->wmi);

		return;
	}

	if (reason == NO_NETWORK_AVAIL) {
		/* connect cmd failed */
		ath6kl_wmi_disconnect_cmd(ar->wmi);
		return;
	}

	if (reason != DISCONNECT_CMD)
		return;

	if (!ar->auto_auth_stage) {
		clear_bit(CONNECT_PEND, &ar->flag);

		if (ar->sme_state == SME_CONNECTING) {
			cfg80211_connect_result(ar->net_dev,
						bssid, NULL, 0,
						NULL, 0,
						WLAN_STATUS_UNSPECIFIED_FAILURE,
						GFP_KERNEL);
		} else {
			cfg80211_disconnected(ar->net_dev, reason,
					      NULL, 0, GFP_KERNEL);
		}

		ar->sme_state = SME_DISCONNECTED;
		return;
	}

	if (ar->dot11_auth_mode != OPEN_AUTH)
		return;

	/*
	 * If the current auth algorithm is open, try shared and
	 * make autoAuthStage idle. We do not make it leap for now
	 * being.
	 */
	key = &ar->keys[ar->def_txkey_index];
	if (down_interruptible(&ar->sem)) {
		ath6kl_err("busy, couldn't get access\n");
		return;
	}

	ar->dot11_auth_mode = SHARED_AUTH;
	ar->auto_auth_stage = AUTH_IDLE;

	ath6kl_wmi_addkey_cmd(ar->wmi,
			      ar->def_txkey_index,
			      ar->prwise_crypto,
			      GROUP_USAGE | TX_USAGE,
			      key->key_len, NULL,
			      key->key,
			      KEY_OP_INIT_VAL, NULL,
			      NO_SYNC_WMIFLAG);

	status = ath6kl_wmi_connect_cmd(ar->wmi,
					ar->nw_type,
					ar->dot11_auth_mode,
					ar->auth_mode,
					ar->prwise_crypto,
					ar->prwise_crypto_len,
					ar->grp_crypto,
					ar->grp_crpto_len,
					ar->ssid_len,
					ar->ssid,
					ar->req_bssid,
					ar->ch_hint,
					ar->connect_ctrl_flags);
	up(&ar->sem);
}

static inline bool is_ch_11a(u16 ch)
{
	return (!((ch >= 2412) && (ch <= 2484)));
}

/* struct ath6kl_node_table::nt_nodelock is locked when calling this */
void ath6kl_cfg80211_scan_node(struct wiphy *wiphy, struct bss *ni)
{
	u16 size;
	unsigned char *ieeemgmtbuf = NULL;
	struct ieee80211_mgmt *mgmt;
	struct ieee80211_channel *channel;
	struct ieee80211_supported_band *band;
	struct ath6kl_common_ie *cie;
	s32 signal;
	int freq;

	cie = &ni->ni_cie;

	if (is_ch_11a(cie->ie_chan))
		band = wiphy->bands[IEEE80211_BAND_5GHZ]; /* 11a */
	else if ((cie->ie_erp) || (cie->ie_xrates))
		band = wiphy->bands[IEEE80211_BAND_2GHZ]; /* 11g */
	else
		band = wiphy->bands[IEEE80211_BAND_2GHZ]; /* 11b */

	size = ni->ni_framelen + offsetof(struct ieee80211_mgmt, u);
	ieeemgmtbuf = kmalloc(size, GFP_ATOMIC);
	if (!ieeemgmtbuf) {
		ath6kl_err("ieee mgmt buf alloc error\n");
		return;
	}

	/*
	 * TODO: Update target to include 802.11 mac header while sending
	 * bss info. Target removes 802.11 mac header while sending the bss
	 * info to host, cfg80211 needs it, for time being just filling the
	 * da, sa and bssid fields alone.
	 */
	mgmt = (struct ieee80211_mgmt *)ieeemgmtbuf;
	memset(mgmt->da, 0xff, ETH_ALEN);	/*broadcast addr */
	memcpy(mgmt->sa, ni->ni_macaddr, ETH_ALEN);
	memcpy(mgmt->bssid, ni->ni_macaddr, ETH_ALEN);
	memcpy(ieeemgmtbuf + offsetof(struct ieee80211_mgmt, u),
	       ni->ni_buf, ni->ni_framelen);

	freq = cie->ie_chan;
	channel = ieee80211_get_channel(wiphy, freq);
	signal = ni->ni_snr * 100;

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG,
		   "%s: bssid %pM ch %d freq %d size %d\n", __func__,
		   mgmt->bssid, channel->hw_value, freq, size);
	cfg80211_inform_bss_frame(wiphy, channel, mgmt,
				  size, signal, GFP_ATOMIC);

	kfree(ieeemgmtbuf);
}

static int ath6kl_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
				struct cfg80211_scan_request *request)
{
	struct ath6kl *ar = (struct ath6kl *)ath6kl_priv(ndev);
	int ret = 0;

	if (!ath6kl_cfg80211_ready(ar))
		return -EIO;

	if (!ar->usr_bss_filter) {
		if (ath6kl_wmi_bssfilter_cmd(ar->wmi,
					     (test_bit(CONNECTED, &ar->flag) ?
					     ALL_BUT_BSS_FILTER :
					     ALL_BSS_FILTER), 0) != 0) {
			ath6kl_err("couldn't set bss filtering\n");
			return -EIO;
		}
	}

	if (request->n_ssids && request->ssids[0].ssid_len) {
		u8 i;

		if (request->n_ssids > (MAX_PROBED_SSID_INDEX - 1))
			request->n_ssids = MAX_PROBED_SSID_INDEX - 1;

		for (i = 0; i < request->n_ssids; i++)
			ath6kl_wmi_probedssid_cmd(ar->wmi, i + 1,
						  SPECIFIC_SSID_FLAG,
						  request->ssids[i].ssid_len,
						  request->ssids[i].ssid);
	}

	if (ath6kl_wmi_startscan_cmd(ar->wmi, WMI_LONG_SCAN, 0,
				     false, 0, 0, 0, NULL) != 0) {
		ath6kl_err("wmi_startscan_cmd failed\n");
		ret = -EIO;
	}

	ar->scan_req = request;

	return ret;
}

void ath6kl_cfg80211_scan_complete_event(struct ath6kl *ar, int status)
{
	int i;

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "%s: status %d\n", __func__, status);

	if (!ar->scan_req)
		return;

	if ((status == -ECANCELED) || (status == -EBUSY)) {
		cfg80211_scan_done(ar->scan_req, true);
		goto out;
	}

	/* Translate data to cfg80211 mgmt format */
	wlan_iterate_nodes(&ar->scan_table, ar->wdev->wiphy);

	cfg80211_scan_done(ar->scan_req, false);

	if (ar->scan_req->n_ssids && ar->scan_req->ssids[0].ssid_len) {
		for (i = 0; i < ar->scan_req->n_ssids; i++) {
			ath6kl_wmi_probedssid_cmd(ar->wmi, i + 1,
						  DISABLE_SSID_FLAG,
						  0, NULL);
		}
	}

out:
	ar->scan_req = NULL;
}

static int ath6kl_cfg80211_add_key(struct wiphy *wiphy, struct net_device *ndev,
				   u8 key_index, bool pairwise,
				   const u8 *mac_addr,
				   struct key_params *params)
{
	struct ath6kl *ar = (struct ath6kl *)ath6kl_priv(ndev);
	struct ath6kl_key *key = NULL;
	u8 key_usage;
	u8 key_type;
	int status = 0;

	if (!ath6kl_cfg80211_ready(ar))
		return -EIO;

	if (key_index < WMI_MIN_KEY_INDEX || key_index > WMI_MAX_KEY_INDEX) {
		ath6kl_dbg(ATH6KL_DBG_WLAN_CFG,
			   "%s: key index %d out of bounds\n", __func__,
			   key_index);
		return -ENOENT;
	}

	key = &ar->keys[key_index];
	memset(key, 0, sizeof(struct ath6kl_key));

	if (pairwise)
		key_usage = PAIRWISE_USAGE;
	else
		key_usage = GROUP_USAGE;

	if (params) {
		if (params->key_len > WLAN_MAX_KEY_LEN ||
		    params->seq_len > sizeof(key->seq))
			return -EINVAL;

		key->key_len = params->key_len;
		memcpy(key->key, params->key, key->key_len);
		key->seq_len = params->seq_len;
		memcpy(key->seq, params->seq, key->seq_len);
		key->cipher = params->cipher;
	}

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
		key_type = WEP_CRYPT;
		break;

	case WLAN_CIPHER_SUITE_TKIP:
		key_type = TKIP_CRYPT;
		break;

	case WLAN_CIPHER_SUITE_CCMP:
		key_type = AES_CRYPT;
		break;

	default:
		return -ENOTSUPP;
	}

	if (((ar->auth_mode == WPA_PSK_AUTH)
	     || (ar->auth_mode == WPA2_PSK_AUTH))
	    && (key_usage & GROUP_USAGE))
		del_timer(&ar->disconnect_timer);

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG,
		   "%s: index %d, key_len %d, key_type 0x%x, key_usage 0x%x, seq_len %d\n",
		   __func__, key_index, key->key_len, key_type,
		   key_usage, key->seq_len);

	ar->def_txkey_index = key_index;
	status = ath6kl_wmi_addkey_cmd(ar->wmi, ar->def_txkey_index,
				       key_type, key_usage, key->key_len,
				       key->seq, key->key, KEY_OP_INIT_VAL,
				       (u8 *) mac_addr, SYNC_BOTH_WMIFLAG);

	if (status)
		return -EIO;

	return 0;
}

static int ath6kl_cfg80211_del_key(struct wiphy *wiphy, struct net_device *ndev,
				   u8 key_index, bool pairwise,
				   const u8 *mac_addr)
{
	struct ath6kl *ar = (struct ath6kl *)ath6kl_priv(ndev);

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "%s: index %d\n", __func__, key_index);

	if (!ath6kl_cfg80211_ready(ar))
		return -EIO;

	if (key_index < WMI_MIN_KEY_INDEX || key_index > WMI_MAX_KEY_INDEX) {
		ath6kl_dbg(ATH6KL_DBG_WLAN_CFG,
			   "%s: key index %d out of bounds\n", __func__,
			   key_index);
		return -ENOENT;
	}

	if (!ar->keys[key_index].key_len) {
		ath6kl_dbg(ATH6KL_DBG_WLAN_CFG,
			   "%s: index %d is empty\n", __func__, key_index);
		return 0;
	}

	ar->keys[key_index].key_len = 0;

	return ath6kl_wmi_deletekey_cmd(ar->wmi, key_index);
}

static int ath6kl_cfg80211_get_key(struct wiphy *wiphy, struct net_device *ndev,
				   u8 key_index, bool pairwise,
				   const u8 *mac_addr, void *cookie,
				   void (*callback) (void *cookie,
						     struct key_params *))
{
	struct ath6kl *ar = (struct ath6kl *)ath6kl_priv(ndev);
	struct ath6kl_key *key = NULL;
	struct key_params params;

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "%s: index %d\n", __func__, key_index);

	if (!ath6kl_cfg80211_ready(ar))
		return -EIO;

	if (key_index < WMI_MIN_KEY_INDEX || key_index > WMI_MAX_KEY_INDEX) {
		ath6kl_dbg(ATH6KL_DBG_WLAN_CFG,
			   "%s: key index %d out of bounds\n", __func__,
			   key_index);
		return -ENOENT;
	}

	key = &ar->keys[key_index];
	memset(&params, 0, sizeof(params));
	params.cipher = key->cipher;
	params.key_len = key->key_len;
	params.seq_len = key->seq_len;
	params.seq = key->seq;
	params.key = key->key;

	callback(cookie, &params);

	return key->key_len ? 0 : -ENOENT;
}

static int ath6kl_cfg80211_set_default_key(struct wiphy *wiphy,
					   struct net_device *ndev,
					   u8 key_index, bool unicast,
					   bool multicast)
{
	struct ath6kl *ar = (struct ath6kl *)ath6kl_priv(ndev);
	struct ath6kl_key *key = NULL;
	int status = 0;
	u8 key_usage;

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "%s: index %d\n", __func__, key_index);

	if (!ath6kl_cfg80211_ready(ar))
		return -EIO;

	if (key_index < WMI_MIN_KEY_INDEX || key_index > WMI_MAX_KEY_INDEX) {
		ath6kl_dbg(ATH6KL_DBG_WLAN_CFG,
			   "%s: key index %d out of bounds\n",
			   __func__, key_index);
		return -ENOENT;
	}

	if (!ar->keys[key_index].key_len) {
		ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "%s: invalid key index %d\n",
			   __func__, key_index);
		return -EINVAL;
	}

	ar->def_txkey_index = key_index;
	key = &ar->keys[ar->def_txkey_index];
	key_usage = GROUP_USAGE;
	if (ar->prwise_crypto == WEP_CRYPT)
		key_usage |= TX_USAGE;

	status = ath6kl_wmi_addkey_cmd(ar->wmi, ar->def_txkey_index,
				       ar->prwise_crypto, key_usage,
				       key->key_len, key->seq, key->key,
				       KEY_OP_INIT_VAL, NULL,
				       SYNC_BOTH_WMIFLAG);
	if (status)
		return -EIO;

	return 0;
}

void ath6kl_cfg80211_tkip_micerr_event(struct ath6kl *ar, u8 keyid,
				       bool ismcast)
{
	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG,
		   "%s: keyid %d, ismcast %d\n", __func__, keyid, ismcast);

	cfg80211_michael_mic_failure(ar->net_dev, ar->bssid,
				     (ismcast ? NL80211_KEYTYPE_GROUP :
				      NL80211_KEYTYPE_PAIRWISE), keyid, NULL,
				     GFP_KERNEL);
}

static int ath6kl_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	struct ath6kl *ar = (struct ath6kl *)wiphy_priv(wiphy);
	int ret;

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "%s: changed 0x%x\n", __func__,
		   changed);

	if (!ath6kl_cfg80211_ready(ar))
		return -EIO;

	if (changed & WIPHY_PARAM_RTS_THRESHOLD) {
		ret = ath6kl_wmi_set_rts_cmd(ar->wmi, wiphy->rts_threshold);
		if (ret != 0) {
			ath6kl_err("ath6kl_wmi_set_rts_cmd failed\n");
			return -EIO;
		}
	}

	return 0;
}

/*
 * The type nl80211_tx_power_setting replaces the following
 * data type from 2.6.36 onwards
*/
static int ath6kl_cfg80211_set_txpower(struct wiphy *wiphy,
				       enum nl80211_tx_power_setting type,
				       int dbm)
{
	struct ath6kl *ar = (struct ath6kl *)wiphy_priv(wiphy);
	u8 ath6kl_dbm;

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "%s: type 0x%x, dbm %d\n", __func__,
		   type, dbm);

	if (!ath6kl_cfg80211_ready(ar))
		return -EIO;

	switch (type) {
	case NL80211_TX_POWER_AUTOMATIC:
		return 0;
	case NL80211_TX_POWER_LIMITED:
		ar->tx_pwr = ath6kl_dbm = dbm;
		break;
	default:
		ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "%s: type 0x%x not supported\n",
			   __func__, type);
		return -EOPNOTSUPP;
	}

	ath6kl_wmi_set_tx_pwr_cmd(ar->wmi, ath6kl_dbm);

	return 0;
}

static int ath6kl_cfg80211_get_txpower(struct wiphy *wiphy, int *dbm)
{
	struct ath6kl *ar = (struct ath6kl *)wiphy_priv(wiphy);

	if (!ath6kl_cfg80211_ready(ar))
		return -EIO;

	if (test_bit(CONNECTED, &ar->flag)) {
		ar->tx_pwr = 0;

		if (ath6kl_wmi_get_tx_pwr_cmd(ar->wmi) != 0) {
			ath6kl_err("ath6kl_wmi_get_tx_pwr_cmd failed\n");
			return -EIO;
		}

		wait_event_interruptible_timeout(ar->event_wq, ar->tx_pwr != 0,
						 5 * HZ);

		if (signal_pending(current)) {
			ath6kl_err("target did not respond\n");
			return -EINTR;
		}
	}

	*dbm = ar->tx_pwr;
	return 0;
}

static int ath6kl_cfg80211_set_power_mgmt(struct wiphy *wiphy,
					  struct net_device *dev,
					  bool pmgmt, int timeout)
{
	struct ath6kl *ar = ath6kl_priv(dev);
	struct wmi_power_mode_cmd mode;

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "%s: pmgmt %d, timeout %d\n",
		   __func__, pmgmt, timeout);

	if (!ath6kl_cfg80211_ready(ar))
		return -EIO;

	if (pmgmt) {
		ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "%s: max perf\n", __func__);
		mode.pwr_mode = REC_POWER;
	} else {
		ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "%s: rec power\n", __func__);
		mode.pwr_mode = MAX_PERF_POWER;
	}

	if (ath6kl_wmi_powermode_cmd(ar->wmi, mode.pwr_mode) != 0) {
		ath6kl_err("wmi_powermode_cmd failed\n");
		return -EIO;
	}

	return 0;
}

static int ath6kl_cfg80211_change_iface(struct wiphy *wiphy,
					struct net_device *ndev,
					enum nl80211_iftype type, u32 *flags,
					struct vif_params *params)
{
	struct ath6kl *ar = ath6kl_priv(ndev);
	struct wireless_dev *wdev = ar->wdev;

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG, "%s: type %u\n", __func__, type);

	if (!ath6kl_cfg80211_ready(ar))
		return -EIO;

	switch (type) {
	case NL80211_IFTYPE_STATION:
		ar->next_mode = INFRA_NETWORK;
		break;
	case NL80211_IFTYPE_ADHOC:
		ar->next_mode = ADHOC_NETWORK;
		break;
	default:
		ath6kl_err("invalid interface type %u\n", type);
		return -EOPNOTSUPP;
	}

	wdev->iftype = type;

	return 0;
}

static int ath6kl_cfg80211_join_ibss(struct wiphy *wiphy,
				     struct net_device *dev,
				     struct cfg80211_ibss_params *ibss_param)
{
	struct ath6kl *ar = ath6kl_priv(dev);
	int status;

	if (!ath6kl_cfg80211_ready(ar))
		return -EIO;

	ar->ssid_len = ibss_param->ssid_len;
	memcpy(ar->ssid, ibss_param->ssid, ar->ssid_len);

	if (ibss_param->channel)
		ar->ch_hint = ibss_param->channel->center_freq;

	if (ibss_param->channel_fixed) {
		/*
		 * TODO: channel_fixed: The channel should be fixed, do not
		 * search for IBSSs to join on other channels. Target
		 * firmware does not support this feature, needs to be
		 * updated.
		 */
		return -EOPNOTSUPP;
	}

	memset(ar->req_bssid, 0, sizeof(ar->req_bssid));
	if (ibss_param->bssid && !is_broadcast_ether_addr(ibss_param->bssid))
		memcpy(ar->req_bssid, ibss_param->bssid, sizeof(ar->req_bssid));

	ath6kl_set_wpa_version(ar, 0);

	status = ath6kl_set_auth_type(ar, NL80211_AUTHTYPE_OPEN_SYSTEM);
	if (status)
		return status;

	if (ibss_param->privacy) {
		ath6kl_set_cipher(ar, WLAN_CIPHER_SUITE_WEP40, true);
		ath6kl_set_cipher(ar, WLAN_CIPHER_SUITE_WEP40, false);
	} else {
		ath6kl_set_cipher(ar, 0, true);
		ath6kl_set_cipher(ar, 0, false);
	}

	ar->nw_type = ar->next_mode;

	ath6kl_dbg(ATH6KL_DBG_WLAN_CFG,
		   "%s: connect called with authmode %d dot11 auth %d"
		   " PW crypto %d PW crypto len %d GRP crypto %d"
		   " GRP crypto len %d channel hint %u\n",
		   __func__,
		   ar->auth_mode, ar->dot11_auth_mode, ar->prwise_crypto,
		   ar->prwise_crypto_len, ar->grp_crypto,
		   ar->grp_crpto_len, ar->ch_hint);

	status = ath6kl_wmi_connect_cmd(ar->wmi, ar->nw_type,
					ar->dot11_auth_mode, ar->auth_mode,
					ar->prwise_crypto,
					ar->prwise_crypto_len,
					ar->grp_crypto, ar->grp_crpto_len,
					ar->ssid_len, ar->ssid,
					ar->req_bssid, ar->ch_hint,
					ar->connect_ctrl_flags);
	set_bit(CONNECT_PEND, &ar->flag);

	return 0;
}

static int ath6kl_cfg80211_leave_ibss(struct wiphy *wiphy,
				      struct net_device *dev)
{
	struct ath6kl *ar = (struct ath6kl *)ath6kl_priv(dev);

	if (!ath6kl_cfg80211_ready(ar))
		return -EIO;

	ath6kl_disconnect(ar);
	memset(ar->ssid, 0, sizeof(ar->ssid));
	ar->ssid_len = 0;

	return 0;
}

static const u32 cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
};

static bool is_rate_legacy(s32 rate)
{
	static const s32 legacy[] = { 1000, 2000, 5500, 11000,
		6000, 9000, 12000, 18000, 24000,
		36000, 48000, 54000
	};
	u8 i;

	for (i = 0; i < ARRAY_SIZE(legacy); i++)
		if (rate == legacy[i])
			return true;

	return false;
}

static bool is_rate_ht20(s32 rate, u8 *mcs, bool *sgi)
{
	static const s32 ht20[] = { 6500, 13000, 19500, 26000, 39000,
		52000, 58500, 65000, 72200
	};
	u8 i;

	for (i = 0; i < ARRAY_SIZE(ht20); i++) {
		if (rate == ht20[i]) {
			if (i == ARRAY_SIZE(ht20) - 1)
				/* last rate uses sgi */
				*sgi = true;
			else
				*sgi = false;

			*mcs = i;
			return true;
		}
	}
	return false;
}

static bool is_rate_ht40(s32 rate, u8 *mcs, bool *sgi)
{
	static const s32 ht40[] = { 13500, 27000, 40500, 54000,
		81000, 108000, 121500, 135000,
		150000
	};
	u8 i;

	for (i = 0; i < ARRAY_SIZE(ht40); i++) {
		if (rate == ht40[i]) {
			if (i == ARRAY_SIZE(ht40) - 1)
				/* last rate uses sgi */
				*sgi = true;
			else
				*sgi = false;

			*mcs = i;
			return true;
		}
	}

	return false;
}

static int ath6kl_get_station(struct wiphy *wiphy, struct net_device *dev,
			      u8 *mac, struct station_info *sinfo)
{
	struct ath6kl *ar = ath6kl_priv(dev);
	long left;
	bool sgi;
	s32 rate;
	int ret;
	u8 mcs;

	if (memcmp(mac, ar->bssid, ETH_ALEN) != 0)
		return -ENOENT;

	if (down_interruptible(&ar->sem))
		return -EBUSY;

	set_bit(STATS_UPDATE_PEND, &ar->flag);

	ret = ath6kl_wmi_get_stats_cmd(ar->wmi);

	if (ret != 0) {
		up(&ar->sem);
		return -EIO;
	}

	left = wait_event_interruptible_timeout(ar->event_wq,
						!test_bit(STATS_UPDATE_PEND,
							  &ar->flag),
						WMI_TIMEOUT);

	up(&ar->sem);

	if (left == 0)
		return -ETIMEDOUT;
	else if (left < 0)
		return left;

	if (ar->target_stats.rx_byte) {
		sinfo->rx_bytes = ar->target_stats.rx_byte;
		sinfo->filled |= STATION_INFO_RX_BYTES;
		sinfo->rx_packets = ar->target_stats.rx_pkt;
		sinfo->filled |= STATION_INFO_RX_PACKETS;
	}

	if (ar->target_stats.tx_byte) {
		sinfo->tx_bytes = ar->target_stats.tx_byte;
		sinfo->filled |= STATION_INFO_TX_BYTES;
		sinfo->tx_packets = ar->target_stats.tx_pkt;
		sinfo->filled |= STATION_INFO_TX_PACKETS;
	}

	sinfo->signal = ar->target_stats.cs_rssi;
	sinfo->filled |= STATION_INFO_SIGNAL;

	rate = ar->target_stats.tx_ucast_rate;

	if (is_rate_legacy(rate)) {
		sinfo->txrate.legacy = rate / 100;
	} else if (is_rate_ht20(rate, &mcs, &sgi)) {
		if (sgi) {
			sinfo->txrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
			sinfo->txrate.mcs = mcs - 1;
		} else {
			sinfo->txrate.mcs = mcs;
		}

		sinfo->txrate.flags |= RATE_INFO_FLAGS_MCS;
	} else if (is_rate_ht40(rate, &mcs, &sgi)) {
		if (sgi) {
			sinfo->txrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
			sinfo->txrate.mcs = mcs - 1;
		} else {
			sinfo->txrate.mcs = mcs;
		}

		sinfo->txrate.flags |= RATE_INFO_FLAGS_40_MHZ_WIDTH;
		sinfo->txrate.flags |= RATE_INFO_FLAGS_MCS;
	} else {
		ath6kl_warn("invalid rate: %d\n", rate);
		return 0;
	}

	sinfo->filled |= STATION_INFO_TX_BITRATE;

	return 0;
}

static int ath6kl_set_pmksa(struct wiphy *wiphy, struct net_device *netdev,
			    struct cfg80211_pmksa *pmksa)
{
	struct ath6kl *ar = ath6kl_priv(netdev);
	return ath6kl_wmi_setpmkid_cmd(ar->wmi, pmksa->bssid,
				       pmksa->pmkid, true);
}

static int ath6kl_del_pmksa(struct wiphy *wiphy, struct net_device *netdev,
			    struct cfg80211_pmksa *pmksa)
{
	struct ath6kl *ar = ath6kl_priv(netdev);
	return ath6kl_wmi_setpmkid_cmd(ar->wmi, pmksa->bssid,
				       pmksa->pmkid, false);
}

static int ath6kl_flush_pmksa(struct wiphy *wiphy, struct net_device *netdev)
{
	struct ath6kl *ar = ath6kl_priv(netdev);
	if (test_bit(CONNECTED, &ar->flag))
		return ath6kl_wmi_setpmkid_cmd(ar->wmi, ar->bssid, NULL, false);
	return 0;
}

static struct cfg80211_ops ath6kl_cfg80211_ops = {
	.change_virtual_intf = ath6kl_cfg80211_change_iface,
	.scan = ath6kl_cfg80211_scan,
	.connect = ath6kl_cfg80211_connect,
	.disconnect = ath6kl_cfg80211_disconnect,
	.add_key = ath6kl_cfg80211_add_key,
	.get_key = ath6kl_cfg80211_get_key,
	.del_key = ath6kl_cfg80211_del_key,
	.set_default_key = ath6kl_cfg80211_set_default_key,
	.set_wiphy_params = ath6kl_cfg80211_set_wiphy_params,
	.set_tx_power = ath6kl_cfg80211_set_txpower,
	.get_tx_power = ath6kl_cfg80211_get_txpower,
	.set_power_mgmt = ath6kl_cfg80211_set_power_mgmt,
	.join_ibss = ath6kl_cfg80211_join_ibss,
	.leave_ibss = ath6kl_cfg80211_leave_ibss,
	.get_station = ath6kl_get_station,
	.set_pmksa = ath6kl_set_pmksa,
	.del_pmksa = ath6kl_del_pmksa,
	.flush_pmksa = ath6kl_flush_pmksa,
};

struct wireless_dev *ath6kl_cfg80211_init(struct device *dev)
{
	int ret = 0;
	struct wireless_dev *wdev;

	wdev = kzalloc(sizeof(struct wireless_dev), GFP_KERNEL);
	if (!wdev) {
		ath6kl_err("couldn't allocate wireless device\n");
		return NULL;
	}

	/* create a new wiphy for use with cfg80211 */
	wdev->wiphy = wiphy_new(&ath6kl_cfg80211_ops, sizeof(struct ath6kl));
	if (!wdev->wiphy) {
		ath6kl_err("couldn't allocate wiphy device\n");
		kfree(wdev);
		return NULL;
	}

	/* set device pointer for wiphy */
	set_wiphy_dev(wdev->wiphy, dev);

	wdev->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
	    BIT(NL80211_IFTYPE_ADHOC);
	/* max num of ssids that can be probed during scanning */
	wdev->wiphy->max_scan_ssids = MAX_PROBED_SSID_INDEX;
	wdev->wiphy->bands[IEEE80211_BAND_2GHZ] = &ath6kl_band_2ghz;
	wdev->wiphy->bands[IEEE80211_BAND_5GHZ] = &ath6kl_band_5ghz;
	wdev->wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

	wdev->wiphy->cipher_suites = cipher_suites;
	wdev->wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);

	ret = wiphy_register(wdev->wiphy);
	if (ret < 0) {
		ath6kl_err("couldn't register wiphy device\n");
		wiphy_free(wdev->wiphy);
		kfree(wdev);
		return NULL;
	}

	return wdev;
}

void ath6kl_cfg80211_deinit(struct ath6kl *ar)
{
	struct wireless_dev *wdev = ar->wdev;

	if (ar->scan_req) {
		cfg80211_scan_done(ar->scan_req, true);
		ar->scan_req = NULL;
	}

	if (!wdev)
		return;

	wiphy_unregister(wdev->wiphy);
	wiphy_free(wdev->wiphy);
	kfree(wdev);
}
