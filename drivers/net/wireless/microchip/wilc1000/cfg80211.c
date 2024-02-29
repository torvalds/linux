// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012 - 2018 Microchip Technology Inc., and its subsidiaries.
 * All rights reserved.
 */

#include "cfg80211.h"

#define GO_NEG_REQ			0x00
#define GO_NEG_RSP			0x01
#define GO_NEG_CONF			0x02
#define P2P_INV_REQ			0x03
#define P2P_INV_RSP			0x04

#define WILC_INVALID_CHANNEL		0

/* Operation at 2.4 GHz with channels 1-13 */
#define WILC_WLAN_OPERATING_CLASS_2_4GHZ		0x51

static const struct ieee80211_txrx_stypes
	wilc_wfi_cfg80211_mgmt_types[NUM_NL80211_IFTYPES] = {
	[NL80211_IFTYPE_STATION] = {
		.tx = BIT(IEEE80211_STYPE_ACTION >> 4) |
			BIT(IEEE80211_STYPE_AUTH >> 4),
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
			BIT(IEEE80211_STYPE_AUTH >> 4)
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
	[NL80211_IFTYPE_P2P_CLIENT] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
			BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
			BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
			BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
			BIT(IEEE80211_STYPE_DISASSOC >> 4) |
			BIT(IEEE80211_STYPE_AUTH >> 4) |
			BIT(IEEE80211_STYPE_DEAUTH >> 4)
	}
};

#ifdef CONFIG_PM
static const struct wiphy_wowlan_support wowlan_support = {
	.flags = WIPHY_WOWLAN_ANY
};
#endif

struct wilc_p2p_mgmt_data {
	int size;
	u8 *buff;
};

struct wilc_p2p_pub_act_frame {
	u8 category;
	u8 action;
	u8 oui[3];
	u8 oui_type;
	u8 oui_subtype;
	u8 dialog_token;
	u8 elem[];
} __packed;

struct wilc_vendor_specific_ie {
	u8 tag_number;
	u8 tag_len;
	u8 oui[3];
	u8 oui_type;
	u8 attr[];
} __packed;

struct wilc_attr_entry {
	u8  attr_type;
	__le16 attr_len;
	u8 val[];
} __packed;

struct wilc_attr_oper_ch {
	u8 attr_type;
	__le16 attr_len;
	u8 country_code[IEEE80211_COUNTRY_STRING_LEN];
	u8 op_class;
	u8 op_channel;
} __packed;

struct wilc_attr_ch_list {
	u8 attr_type;
	__le16 attr_len;
	u8 country_code[IEEE80211_COUNTRY_STRING_LEN];
	u8 elem[];
} __packed;

struct wilc_ch_list_elem {
	u8 op_class;
	u8 no_of_channels;
	u8 ch_list[];
} __packed;

static void cfg_scan_result(enum scan_event scan_event,
			    struct wilc_rcvd_net_info *info,
			    struct wilc_priv *priv)
{
	if (!priv->cfg_scanning)
		return;

	if (scan_event == SCAN_EVENT_NETWORK_FOUND) {
		s32 freq;
		struct ieee80211_channel *channel;
		struct cfg80211_bss *bss;
		struct wiphy *wiphy = priv->dev->ieee80211_ptr->wiphy;

		if (!wiphy || !info)
			return;

		freq = ieee80211_channel_to_frequency((s32)info->ch,
						      NL80211_BAND_2GHZ);
		channel = ieee80211_get_channel(wiphy, freq);
		if (!channel)
			return;

		bss = cfg80211_inform_bss_frame(wiphy, channel, info->mgmt,
						info->frame_len,
						(s32)info->rssi * 100,
						GFP_KERNEL);
		cfg80211_put_bss(wiphy, bss);
	} else if (scan_event == SCAN_EVENT_DONE) {
		mutex_lock(&priv->scan_req_lock);

		if (priv->scan_req) {
			struct cfg80211_scan_info info = {
				.aborted = false,
			};

			cfg80211_scan_done(priv->scan_req, &info);
			priv->cfg_scanning = false;
			priv->scan_req = NULL;
		}
		mutex_unlock(&priv->scan_req_lock);
	} else if (scan_event == SCAN_EVENT_ABORTED) {
		mutex_lock(&priv->scan_req_lock);

		if (priv->scan_req) {
			struct cfg80211_scan_info info = {
				.aborted = false,
			};

			cfg80211_scan_done(priv->scan_req, &info);
			priv->cfg_scanning = false;
			priv->scan_req = NULL;
		}
		mutex_unlock(&priv->scan_req_lock);
	}
}

static void cfg_connect_result(enum conn_event conn_disconn_evt, u8 mac_status,
			       struct wilc_priv *priv)
{
	struct net_device *dev = priv->dev;
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc *wl = vif->wilc;
	struct host_if_drv *wfi_drv = priv->hif_drv;
	struct wilc_conn_info *conn_info = &wfi_drv->conn_info;
	struct wiphy *wiphy = dev->ieee80211_ptr->wiphy;

	vif->connecting = false;

	if (conn_disconn_evt == CONN_DISCONN_EVENT_CONN_RESP) {
		u16 connect_status = conn_info->status;

		if (mac_status == WILC_MAC_STATUS_DISCONNECTED &&
		    connect_status == WLAN_STATUS_SUCCESS) {
			connect_status = WLAN_STATUS_UNSPECIFIED_FAILURE;
			wilc_wlan_set_bssid(priv->dev, NULL, WILC_STATION_MODE);

			if (vif->iftype != WILC_CLIENT_MODE)
				wl->sta_ch = WILC_INVALID_CHANNEL;

			netdev_err(dev, "Unspecified failure\n");
		}

		if (connect_status == WLAN_STATUS_SUCCESS)
			memcpy(priv->associated_bss, conn_info->bssid,
			       ETH_ALEN);

		cfg80211_ref_bss(wiphy, vif->bss);
		cfg80211_connect_bss(dev, conn_info->bssid, vif->bss,
				     conn_info->req_ies,
				     conn_info->req_ies_len,
				     conn_info->resp_ies,
				     conn_info->resp_ies_len,
				     connect_status, GFP_KERNEL,
				     NL80211_TIMEOUT_UNSPECIFIED);

		vif->bss = NULL;
	} else if (conn_disconn_evt == CONN_DISCONN_EVENT_DISCONN_NOTIF) {
		u16 reason = 0;

		eth_zero_addr(priv->associated_bss);
		wilc_wlan_set_bssid(priv->dev, NULL, WILC_STATION_MODE);

		if (vif->iftype != WILC_CLIENT_MODE) {
			wl->sta_ch = WILC_INVALID_CHANNEL;
		} else {
			if (wfi_drv->ifc_up)
				reason = 3;
			else
				reason = 1;
		}

		cfg80211_disconnected(dev, reason, NULL, 0, false, GFP_KERNEL);
	}
}

struct wilc_vif *wilc_get_wl_to_vif(struct wilc *wl)
{
	struct wilc_vif *vif;

	vif = list_first_or_null_rcu(&wl->vif_list, typeof(*vif), list);
	if (!vif)
		return ERR_PTR(-EINVAL);

	return vif;
}

static int set_channel(struct wiphy *wiphy,
		       struct cfg80211_chan_def *chandef)
{
	struct wilc *wl = wiphy_priv(wiphy);
	struct wilc_vif *vif;
	u32 channelnum;
	int result;
	int srcu_idx;

	srcu_idx = srcu_read_lock(&wl->srcu);
	vif = wilc_get_wl_to_vif(wl);
	if (IS_ERR(vif)) {
		srcu_read_unlock(&wl->srcu, srcu_idx);
		return PTR_ERR(vif);
	}

	channelnum = ieee80211_frequency_to_channel(chandef->chan->center_freq);

	wl->op_ch = channelnum;
	result = wilc_set_mac_chnl_num(vif, channelnum);
	if (result)
		netdev_err(vif->ndev, "Error in setting channel\n");

	srcu_read_unlock(&wl->srcu, srcu_idx);
	return result;
}

static int scan(struct wiphy *wiphy, struct cfg80211_scan_request *request)
{
	struct wilc_vif *vif = netdev_priv(request->wdev->netdev);
	struct wilc_priv *priv = &vif->priv;
	u32 i;
	int ret = 0;
	u8 scan_ch_list[WILC_MAX_NUM_SCANNED_CH];
	u8 scan_type;

	if (request->n_channels > WILC_MAX_NUM_SCANNED_CH) {
		netdev_err(vif->ndev, "Requested scanned channels over\n");
		return -EINVAL;
	}

	priv->scan_req = request;
	priv->cfg_scanning = true;
	for (i = 0; i < request->n_channels; i++) {
		u16 freq = request->channels[i]->center_freq;

		scan_ch_list[i] = ieee80211_frequency_to_channel(freq);
	}

	if (request->n_ssids)
		scan_type = WILC_FW_ACTIVE_SCAN;
	else
		scan_type = WILC_FW_PASSIVE_SCAN;

	ret = wilc_scan(vif, WILC_FW_USER_SCAN, scan_type,
			scan_ch_list, cfg_scan_result, request);

	if (ret) {
		priv->scan_req = NULL;
		priv->cfg_scanning = false;
	}

	return ret;
}

static int connect(struct wiphy *wiphy, struct net_device *dev,
		   struct cfg80211_connect_params *sme)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc_priv *priv = &vif->priv;
	struct host_if_drv *wfi_drv = priv->hif_drv;
	int ret;
	u32 i;
	u8 security = WILC_FW_SEC_NO;
	enum mfptype mfp_type = WILC_FW_MFP_NONE;
	enum authtype auth_type = WILC_FW_AUTH_ANY;
	u32 cipher_group;
	struct cfg80211_bss *bss;
	void *join_params;
	u8 ch;

	vif->connecting = true;

	cipher_group = sme->crypto.cipher_group;
	if (cipher_group != 0) {
		if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_2) {
			if (cipher_group == WLAN_CIPHER_SUITE_TKIP)
				security = WILC_FW_SEC_WPA2_TKIP;
			else
				security = WILC_FW_SEC_WPA2_AES;
		} else if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_1) {
			if (cipher_group == WLAN_CIPHER_SUITE_TKIP)
				security = WILC_FW_SEC_WPA_TKIP;
			else
				security = WILC_FW_SEC_WPA_AES;
		} else {
			ret = -ENOTSUPP;
			netdev_err(dev, "%s: Unsupported cipher\n",
				   __func__);
			goto out_error;
		}
	}

	if ((sme->crypto.wpa_versions & NL80211_WPA_VERSION_1) ||
	    (sme->crypto.wpa_versions & NL80211_WPA_VERSION_2)) {
		for (i = 0; i < sme->crypto.n_ciphers_pairwise; i++) {
			u32 ciphers_pairwise = sme->crypto.ciphers_pairwise[i];

			if (ciphers_pairwise == WLAN_CIPHER_SUITE_TKIP)
				security |= WILC_FW_TKIP;
			else
				security |= WILC_FW_AES;
		}
	}

	switch (sme->auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		auth_type = WILC_FW_AUTH_OPEN_SYSTEM;
		break;

	case NL80211_AUTHTYPE_SAE:
		auth_type = WILC_FW_AUTH_SAE;
		if (sme->ssid_len) {
			memcpy(vif->auth.ssid.ssid, sme->ssid, sme->ssid_len);
			vif->auth.ssid.ssid_len = sme->ssid_len;
		}
		vif->auth.key_mgmt_suite = cpu_to_be32(sme->crypto.akm_suites[0]);
		ether_addr_copy(vif->auth.bssid, sme->bssid);
		break;

	default:
		break;
	}

	if (sme->crypto.n_akm_suites) {
		if (sme->crypto.akm_suites[0] == WLAN_AKM_SUITE_8021X)
			auth_type = WILC_FW_AUTH_IEEE8021;
		else if (sme->crypto.akm_suites[0] == WLAN_AKM_SUITE_PSK_SHA256)
			auth_type = WILC_FW_AUTH_OPEN_SYSTEM_SHA256;
		else if (sme->crypto.akm_suites[0] == WLAN_AKM_SUITE_8021X_SHA256)
			auth_type = WILC_FW_AUTH_IEE8021X_SHA256;
	}

	if (wfi_drv->usr_scan_req.scan_result) {
		netdev_err(vif->ndev, "%s: Scan in progress\n", __func__);
		ret = -EBUSY;
		goto out_error;
	}

	bss = cfg80211_get_bss(wiphy, sme->channel, sme->bssid, sme->ssid,
			       sme->ssid_len, IEEE80211_BSS_TYPE_ANY,
			       IEEE80211_PRIVACY(sme->privacy));
	if (!bss) {
		ret = -EINVAL;
		goto out_error;
	}

	if (ether_addr_equal_unaligned(vif->bssid, bss->bssid)) {
		ret = -EALREADY;
		goto out_put_bss;
	}

	join_params = wilc_parse_join_bss_param(bss, &sme->crypto);
	if (!join_params) {
		netdev_err(dev, "%s: failed to construct join param\n",
			   __func__);
		ret = -EINVAL;
		goto out_put_bss;
	}

	ch = ieee80211_frequency_to_channel(bss->channel->center_freq);
	vif->wilc->op_ch = ch;
	if (vif->iftype != WILC_CLIENT_MODE)
		vif->wilc->sta_ch = ch;

	wilc_wlan_set_bssid(dev, bss->bssid, WILC_STATION_MODE);

	wfi_drv->conn_info.security = security;
	wfi_drv->conn_info.auth_type = auth_type;
	wfi_drv->conn_info.conn_result = cfg_connect_result;
	wfi_drv->conn_info.priv = priv;
	wfi_drv->conn_info.param = join_params;

	if (sme->mfp == NL80211_MFP_OPTIONAL)
		mfp_type = WILC_FW_MFP_OPTIONAL;
	else if (sme->mfp == NL80211_MFP_REQUIRED)
		mfp_type = WILC_FW_MFP_REQUIRED;

	wfi_drv->conn_info.mfp_type = mfp_type;

	ret = wilc_set_join_req(vif, bss->bssid, sme->ie, sme->ie_len);
	if (ret) {
		netdev_err(dev, "wilc_set_join_req(): Error\n");
		ret = -ENOENT;
		if (vif->iftype != WILC_CLIENT_MODE)
			vif->wilc->sta_ch = WILC_INVALID_CHANNEL;
		wilc_wlan_set_bssid(dev, NULL, WILC_STATION_MODE);
		wfi_drv->conn_info.conn_result = NULL;
		kfree(join_params);
		goto out_put_bss;
	}
	kfree(join_params);
	vif->bss = bss;
	cfg80211_put_bss(wiphy, bss);
	return 0;

out_put_bss:
	cfg80211_put_bss(wiphy, bss);

out_error:
	vif->connecting = false;
	return ret;
}

static int disconnect(struct wiphy *wiphy, struct net_device *dev,
		      u16 reason_code)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc_priv *priv = &vif->priv;
	struct wilc *wilc = vif->wilc;
	int ret;

	vif->connecting = false;

	if (!wilc)
		return -EIO;

	if (wilc->close) {
		/* already disconnected done */
		cfg80211_disconnected(dev, 0, NULL, 0, true, GFP_KERNEL);
		return 0;
	}

	if (vif->iftype != WILC_CLIENT_MODE)
		wilc->sta_ch = WILC_INVALID_CHANNEL;
	wilc_wlan_set_bssid(priv->dev, NULL, WILC_STATION_MODE);

	priv->hif_drv->p2p_timeout = 0;

	ret = wilc_disconnect(vif);
	if (ret != 0) {
		netdev_err(priv->dev, "Error in disconnecting\n");
		ret = -EINVAL;
	}

	vif->bss = NULL;

	return ret;
}

static int wilc_wfi_cfg_allocate_wpa_entry(struct wilc_priv *priv, u8 idx)
{
	if (!priv->wilc_gtk[idx]) {
		priv->wilc_gtk[idx] = kzalloc(sizeof(*priv->wilc_gtk[idx]),
					      GFP_KERNEL);
		if (!priv->wilc_gtk[idx])
			return -ENOMEM;
	}

	if (!priv->wilc_ptk[idx]) {
		priv->wilc_ptk[idx] = kzalloc(sizeof(*priv->wilc_ptk[idx]),
					      GFP_KERNEL);
		if (!priv->wilc_ptk[idx])
			return -ENOMEM;
	}

	return 0;
}

static int wilc_wfi_cfg_allocate_wpa_igtk_entry(struct wilc_priv *priv, u8 idx)
{
	idx -= 4;
	if (!priv->wilc_igtk[idx]) {
		priv->wilc_igtk[idx] = kzalloc(sizeof(*priv->wilc_igtk[idx]),
					       GFP_KERNEL);
		if (!priv->wilc_igtk[idx])
			return -ENOMEM;
	}
	return 0;
}

static int wilc_wfi_cfg_copy_wpa_info(struct wilc_wfi_key *key_info,
				      struct key_params *params)
{
	kfree(key_info->key);

	key_info->key = kmemdup(params->key, params->key_len, GFP_KERNEL);
	if (!key_info->key)
		return -ENOMEM;

	kfree(key_info->seq);

	if (params->seq_len > 0) {
		key_info->seq = kmemdup(params->seq, params->seq_len,
					GFP_KERNEL);
		if (!key_info->seq)
			return -ENOMEM;
	}

	key_info->cipher = params->cipher;
	key_info->key_len = params->key_len;
	key_info->seq_len = params->seq_len;

	return 0;
}

static int add_key(struct wiphy *wiphy, struct net_device *netdev, int link_id,
		   u8 key_index, bool pairwise, const u8 *mac_addr,
		   struct key_params *params)

{
	int ret = 0, keylen = params->key_len;
	const u8 *rx_mic = NULL;
	const u8 *tx_mic = NULL;
	u8 mode = WILC_FW_SEC_NO;
	u8 op_mode;
	struct wilc_vif *vif = netdev_priv(netdev);
	struct wilc_priv *priv = &vif->priv;
	struct wilc_wfi_key *key;

	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
		if (priv->wdev.iftype == NL80211_IFTYPE_AP ||
		    priv->wdev.iftype == NL80211_IFTYPE_P2P_GO) {
			struct wilc_wfi_key *key;

			ret = wilc_wfi_cfg_allocate_wpa_entry(priv, key_index);
			if (ret)
				return -ENOMEM;

			if (params->key_len > 16 &&
			    params->cipher == WLAN_CIPHER_SUITE_TKIP) {
				tx_mic = params->key + 24;
				rx_mic = params->key + 16;
				keylen = params->key_len - 16;
			}

			if (!pairwise) {
				if (params->cipher == WLAN_CIPHER_SUITE_TKIP)
					mode = WILC_FW_SEC_WPA_TKIP;
				else
					mode = WILC_FW_SEC_WPA2_AES;

				priv->wilc_groupkey = mode;

				key = priv->wilc_gtk[key_index];
			} else {
				if (params->cipher == WLAN_CIPHER_SUITE_TKIP)
					mode = WILC_FW_SEC_WPA_TKIP;
				else
					mode = priv->wilc_groupkey | WILC_FW_AES;

				key = priv->wilc_ptk[key_index];
			}
			ret = wilc_wfi_cfg_copy_wpa_info(key, params);
			if (ret)
				return -ENOMEM;

			op_mode = WILC_AP_MODE;
		} else {
			if (params->key_len > 16 &&
			    params->cipher == WLAN_CIPHER_SUITE_TKIP) {
				rx_mic = params->key + 24;
				tx_mic = params->key + 16;
				keylen = params->key_len - 16;
			}

			op_mode = WILC_STATION_MODE;
		}

		if (!pairwise)
			ret = wilc_add_rx_gtk(vif, params->key, keylen,
					      key_index, params->seq_len,
					      params->seq, rx_mic, tx_mic,
					      op_mode, mode);
		else
			ret = wilc_add_ptk(vif, params->key, keylen, mac_addr,
					   rx_mic, tx_mic, op_mode, mode,
					   key_index);

		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		ret = wilc_wfi_cfg_allocate_wpa_igtk_entry(priv, key_index);
		if (ret)
			return -ENOMEM;

		key = priv->wilc_igtk[key_index - 4];
		ret = wilc_wfi_cfg_copy_wpa_info(key, params);
		if (ret)
			return -ENOMEM;

		if (priv->wdev.iftype == NL80211_IFTYPE_AP ||
		    priv->wdev.iftype == NL80211_IFTYPE_P2P_GO)
			op_mode = WILC_AP_MODE;
		else
			op_mode = WILC_STATION_MODE;

		ret = wilc_add_igtk(vif, params->key, keylen, params->seq,
				    params->seq_len, mac_addr, op_mode,
				    key_index);
		break;

	default:
		netdev_err(netdev, "%s: Unsupported cipher\n", __func__);
		ret = -ENOTSUPP;
	}

	return ret;
}

static int del_key(struct wiphy *wiphy, struct net_device *netdev, int link_id,
		   u8 key_index,
		   bool pairwise,
		   const u8 *mac_addr)
{
	struct wilc_vif *vif = netdev_priv(netdev);
	struct wilc_priv *priv = &vif->priv;

	if (!pairwise && (key_index == 4 || key_index == 5)) {
		key_index -= 4;
		if (priv->wilc_igtk[key_index]) {
			kfree(priv->wilc_igtk[key_index]->key);
			priv->wilc_igtk[key_index]->key = NULL;
			kfree(priv->wilc_igtk[key_index]->seq);
			priv->wilc_igtk[key_index]->seq = NULL;
			kfree(priv->wilc_igtk[key_index]);
			priv->wilc_igtk[key_index] = NULL;
		}
	} else {
		if (priv->wilc_gtk[key_index]) {
			kfree(priv->wilc_gtk[key_index]->key);
			priv->wilc_gtk[key_index]->key = NULL;
			kfree(priv->wilc_gtk[key_index]->seq);
			priv->wilc_gtk[key_index]->seq = NULL;

			kfree(priv->wilc_gtk[key_index]);
			priv->wilc_gtk[key_index] = NULL;
		}
		if (priv->wilc_ptk[key_index]) {
			kfree(priv->wilc_ptk[key_index]->key);
			priv->wilc_ptk[key_index]->key = NULL;
			kfree(priv->wilc_ptk[key_index]->seq);
			priv->wilc_ptk[key_index]->seq = NULL;
			kfree(priv->wilc_ptk[key_index]);
			priv->wilc_ptk[key_index] = NULL;
		}
	}

	return 0;
}

static int get_key(struct wiphy *wiphy, struct net_device *netdev, int link_id,
		   u8 key_index, bool pairwise, const u8 *mac_addr,
		   void *cookie,
		   void (*callback)(void *cookie, struct key_params *))
{
	struct wilc_vif *vif = netdev_priv(netdev);
	struct wilc_priv *priv = &vif->priv;
	struct  key_params key_params;

	if (!pairwise) {
		if (key_index == 4 || key_index == 5) {
			key_index -= 4;
			key_params.key = priv->wilc_igtk[key_index]->key;
			key_params.cipher = priv->wilc_igtk[key_index]->cipher;
			key_params.key_len = priv->wilc_igtk[key_index]->key_len;
			key_params.seq = priv->wilc_igtk[key_index]->seq;
			key_params.seq_len = priv->wilc_igtk[key_index]->seq_len;
		} else {
			key_params.key = priv->wilc_gtk[key_index]->key;
			key_params.cipher = priv->wilc_gtk[key_index]->cipher;
			key_params.key_len = priv->wilc_gtk[key_index]->key_len;
			key_params.seq = priv->wilc_gtk[key_index]->seq;
			key_params.seq_len = priv->wilc_gtk[key_index]->seq_len;
		}
	} else {
		key_params.key = priv->wilc_ptk[key_index]->key;
		key_params.cipher = priv->wilc_ptk[key_index]->cipher;
		key_params.key_len = priv->wilc_ptk[key_index]->key_len;
		key_params.seq = priv->wilc_ptk[key_index]->seq;
		key_params.seq_len = priv->wilc_ptk[key_index]->seq_len;
	}

	callback(cookie, &key_params);

	return 0;
}

/* wiphy_new_nm() will WARNON if not present */
static int set_default_key(struct wiphy *wiphy, struct net_device *netdev,
			   int link_id, u8 key_index, bool unicast,
			   bool multicast)
{
	return 0;
}

static int set_default_mgmt_key(struct wiphy *wiphy, struct net_device *netdev,
				int link_id, u8 key_index)
{
	struct wilc_vif *vif = netdev_priv(netdev);

	return wilc_set_default_mgmt_key_index(vif, key_index);
}

static int get_station(struct wiphy *wiphy, struct net_device *dev,
		       const u8 *mac, struct station_info *sinfo)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc_priv *priv = &vif->priv;
	struct wilc *wilc = vif->wilc;
	u32 i = 0;
	u32 associatedsta = ~0;
	u32 inactive_time = 0;

	if (vif->iftype == WILC_AP_MODE || vif->iftype == WILC_GO_MODE) {
		for (i = 0; i < NUM_STA_ASSOCIATED; i++) {
			if (!(memcmp(mac,
				     priv->assoc_stainfo.sta_associated_bss[i],
				     ETH_ALEN))) {
				associatedsta = i;
				break;
			}
		}

		if (associatedsta == ~0) {
			netdev_err(dev, "sta required is not associated\n");
			return -ENOENT;
		}

		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_INACTIVE_TIME);

		wilc_get_inactive_time(vif, mac, &inactive_time);
		sinfo->inactive_time = 1000 * inactive_time;
	} else if (vif->iftype == WILC_STATION_MODE) {
		struct rf_info stats;

		if (!wilc->initialized)
			return -EBUSY;

		wilc_get_statistics(vif, &stats);

		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL) |
				 BIT_ULL(NL80211_STA_INFO_RX_PACKETS) |
				 BIT_ULL(NL80211_STA_INFO_TX_PACKETS) |
				 BIT_ULL(NL80211_STA_INFO_TX_FAILED) |
				 BIT_ULL(NL80211_STA_INFO_TX_BITRATE);

		sinfo->signal = stats.rssi;
		sinfo->rx_packets = stats.rx_cnt;
		sinfo->tx_packets = stats.tx_cnt + stats.tx_fail_cnt;
		sinfo->tx_failed = stats.tx_fail_cnt;
		sinfo->txrate.legacy = stats.link_speed * 10;

		if (stats.link_speed > TCP_ACK_FILTER_LINK_SPEED_THRESH &&
		    stats.link_speed != DEFAULT_LINK_SPEED)
			wilc_enable_tcp_ack_filter(vif, true);
		else if (stats.link_speed != DEFAULT_LINK_SPEED)
			wilc_enable_tcp_ack_filter(vif, false);
	}
	return 0;
}

static int change_bss(struct wiphy *wiphy, struct net_device *dev,
		      struct bss_parameters *params)
{
	return 0;
}

static int set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	int ret = -EINVAL;
	struct cfg_param_attr cfg_param_val;
	struct wilc *wl = wiphy_priv(wiphy);
	struct wilc_vif *vif;
	struct wilc_priv *priv;
	int srcu_idx;

	srcu_idx = srcu_read_lock(&wl->srcu);
	vif = wilc_get_wl_to_vif(wl);
	if (IS_ERR(vif))
		goto out;

	priv = &vif->priv;
	cfg_param_val.flag = 0;

	if (changed & WIPHY_PARAM_RETRY_SHORT) {
		netdev_dbg(vif->ndev,
			   "Setting WIPHY_PARAM_RETRY_SHORT %d\n",
			   wiphy->retry_short);
		cfg_param_val.flag  |= WILC_CFG_PARAM_RETRY_SHORT;
		cfg_param_val.short_retry_limit = wiphy->retry_short;
	}
	if (changed & WIPHY_PARAM_RETRY_LONG) {
		netdev_dbg(vif->ndev,
			   "Setting WIPHY_PARAM_RETRY_LONG %d\n",
			   wiphy->retry_long);
		cfg_param_val.flag |= WILC_CFG_PARAM_RETRY_LONG;
		cfg_param_val.long_retry_limit = wiphy->retry_long;
	}
	if (changed & WIPHY_PARAM_FRAG_THRESHOLD) {
		if (wiphy->frag_threshold > 255 &&
		    wiphy->frag_threshold < 7937) {
			netdev_dbg(vif->ndev,
				   "Setting WIPHY_PARAM_FRAG_THRESHOLD %d\n",
				   wiphy->frag_threshold);
			cfg_param_val.flag |= WILC_CFG_PARAM_FRAG_THRESHOLD;
			cfg_param_val.frag_threshold = wiphy->frag_threshold;
		} else {
			netdev_err(vif->ndev,
				   "Fragmentation threshold out of range\n");
			goto out;
		}
	}

	if (changed & WIPHY_PARAM_RTS_THRESHOLD) {
		if (wiphy->rts_threshold > 255) {
			netdev_dbg(vif->ndev,
				   "Setting WIPHY_PARAM_RTS_THRESHOLD %d\n",
				   wiphy->rts_threshold);
			cfg_param_val.flag |= WILC_CFG_PARAM_RTS_THRESHOLD;
			cfg_param_val.rts_threshold = wiphy->rts_threshold;
		} else {
			netdev_err(vif->ndev, "RTS threshold out of range\n");
			goto out;
		}
	}

	ret = wilc_hif_set_cfg(vif, &cfg_param_val);
	if (ret)
		netdev_err(priv->dev, "Error in setting WIPHY PARAMS\n");

out:
	srcu_read_unlock(&wl->srcu, srcu_idx);
	return ret;
}

static int set_pmksa(struct wiphy *wiphy, struct net_device *netdev,
		     struct cfg80211_pmksa *pmksa)
{
	struct wilc_vif *vif = netdev_priv(netdev);
	struct wilc_priv *priv = &vif->priv;
	u32 i;
	int ret = 0;
	u8 flag = 0;

	for (i = 0; i < priv->pmkid_list.numpmkid; i++)	{
		if (!memcmp(pmksa->bssid, priv->pmkid_list.pmkidlist[i].bssid,
			    ETH_ALEN)) {
			flag = PMKID_FOUND;
			break;
		}
	}
	if (i < WILC_MAX_NUM_PMKIDS) {
		memcpy(priv->pmkid_list.pmkidlist[i].bssid, pmksa->bssid,
		       ETH_ALEN);
		memcpy(priv->pmkid_list.pmkidlist[i].pmkid, pmksa->pmkid,
		       WLAN_PMKID_LEN);
		if (!(flag == PMKID_FOUND))
			priv->pmkid_list.numpmkid++;
	} else {
		netdev_err(netdev, "Invalid PMKID index\n");
		ret = -EINVAL;
	}

	if (!ret)
		ret = wilc_set_pmkid_info(vif, &priv->pmkid_list);

	return ret;
}

static int del_pmksa(struct wiphy *wiphy, struct net_device *netdev,
		     struct cfg80211_pmksa *pmksa)
{
	u32 i;
	struct wilc_vif *vif = netdev_priv(netdev);
	struct wilc_priv *priv = &vif->priv;

	for (i = 0; i < priv->pmkid_list.numpmkid; i++)	{
		if (!memcmp(pmksa->bssid, priv->pmkid_list.pmkidlist[i].bssid,
			    ETH_ALEN)) {
			memset(&priv->pmkid_list.pmkidlist[i], 0,
			       sizeof(struct wilc_pmkid));
			break;
		}
	}

	if (i == priv->pmkid_list.numpmkid)
		return -EINVAL;

	for (; i < (priv->pmkid_list.numpmkid - 1); i++) {
		memcpy(priv->pmkid_list.pmkidlist[i].bssid,
		       priv->pmkid_list.pmkidlist[i + 1].bssid,
		       ETH_ALEN);
		memcpy(priv->pmkid_list.pmkidlist[i].pmkid,
		       priv->pmkid_list.pmkidlist[i + 1].pmkid,
		       WLAN_PMKID_LEN);
	}
	priv->pmkid_list.numpmkid--;

	return 0;
}

static int flush_pmksa(struct wiphy *wiphy, struct net_device *netdev)
{
	struct wilc_vif *vif = netdev_priv(netdev);

	memset(&vif->priv.pmkid_list, 0, sizeof(struct wilc_pmkid_attr));

	return 0;
}

static inline void wilc_wfi_cfg_parse_ch_attr(u8 *buf, u32 len, u8 sta_ch)
{
	struct wilc_attr_entry *e;
	struct wilc_attr_ch_list *ch_list;
	struct wilc_attr_oper_ch *op_ch;
	u32 index = 0;
	u8 ch_list_idx = 0;
	u8 op_ch_idx = 0;

	if (sta_ch == WILC_INVALID_CHANNEL)
		return;

	while (index + sizeof(*e) <= len) {
		u16 attr_size;

		e = (struct wilc_attr_entry *)&buf[index];
		attr_size = le16_to_cpu(e->attr_len);

		if (index + sizeof(*e) + attr_size > len)
			return;

		if (e->attr_type == IEEE80211_P2P_ATTR_CHANNEL_LIST &&
		    attr_size >= (sizeof(struct wilc_attr_ch_list) - sizeof(*e)))
			ch_list_idx = index;
		else if (e->attr_type == IEEE80211_P2P_ATTR_OPER_CHANNEL &&
			 attr_size == (sizeof(struct wilc_attr_oper_ch) - sizeof(*e)))
			op_ch_idx = index;

		if (ch_list_idx && op_ch_idx)
			break;

		index += sizeof(*e) + attr_size;
	}

	if (ch_list_idx) {
		u16 elem_size;

		ch_list = (struct wilc_attr_ch_list *)&buf[ch_list_idx];
		/* the number of bytes following the final 'elem' member */
		elem_size = le16_to_cpu(ch_list->attr_len) -
			(sizeof(*ch_list) - sizeof(struct wilc_attr_entry));
		for (unsigned int i = 0; i < elem_size;) {
			struct wilc_ch_list_elem *e;

			e = (struct wilc_ch_list_elem *)(ch_list->elem + i);

			i += sizeof(*e);
			if (i > elem_size)
				break;

			i += e->no_of_channels;
			if (i > elem_size)
				break;

			if (e->op_class == WILC_WLAN_OPERATING_CLASS_2_4GHZ) {
				memset(e->ch_list, sta_ch, e->no_of_channels);
				break;
			}
		}
	}

	if (op_ch_idx) {
		op_ch = (struct wilc_attr_oper_ch *)&buf[op_ch_idx];
		op_ch->op_class = WILC_WLAN_OPERATING_CLASS_2_4GHZ;
		op_ch->op_channel = sta_ch;
	}
}

bool wilc_wfi_mgmt_frame_rx(struct wilc_vif *vif, u8 *buff, u32 size)
{
	struct wilc *wl = vif->wilc;
	struct wilc_priv *priv = &vif->priv;
	int freq;

	freq = ieee80211_channel_to_frequency(wl->op_ch, NL80211_BAND_2GHZ);

	return cfg80211_rx_mgmt(&priv->wdev, freq, 0, buff, size, 0);
}

void wilc_wfi_p2p_rx(struct wilc_vif *vif, u8 *buff, u32 size)
{
	struct wilc *wl = vif->wilc;
	struct wilc_priv *priv = &vif->priv;
	struct host_if_drv *wfi_drv = priv->hif_drv;
	struct ieee80211_mgmt *mgmt;
	struct wilc_vendor_specific_ie *p;
	struct wilc_p2p_pub_act_frame *d;
	int ie_offset = offsetof(struct ieee80211_mgmt, u) + sizeof(*d);
	const u8 *vendor_ie;
	u32 header, pkt_offset;
	s32 freq;

	header = get_unaligned_le32(buff - HOST_HDR_OFFSET);
	pkt_offset = FIELD_GET(WILC_PKT_HDR_OFFSET_FIELD, header);

	if (pkt_offset & IS_MANAGMEMENT_CALLBACK) {
		bool ack = false;
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)buff;

		if (ieee80211_is_probe_resp(hdr->frame_control) ||
		    pkt_offset & IS_MGMT_STATUS_SUCCES)
			ack = true;

		cfg80211_mgmt_tx_status(&priv->wdev, priv->tx_cookie, buff,
					size, ack, GFP_KERNEL);
		return;
	}

	freq = ieee80211_channel_to_frequency(wl->op_ch, NL80211_BAND_2GHZ);

	mgmt = (struct ieee80211_mgmt *)buff;
	if (!ieee80211_is_action(mgmt->frame_control))
		goto out_rx_mgmt;

	if (priv->cfg_scanning &&
	    time_after_eq(jiffies, (unsigned long)wfi_drv->p2p_timeout)) {
		netdev_dbg(vif->ndev, "Receiving action wrong ch\n");
		return;
	}

	if (!ieee80211_is_public_action((struct ieee80211_hdr *)buff, size))
		goto out_rx_mgmt;

	d = (struct wilc_p2p_pub_act_frame *)(&mgmt->u.action);
	if (d->oui_subtype != GO_NEG_REQ && d->oui_subtype != GO_NEG_RSP &&
	    d->oui_subtype != P2P_INV_REQ && d->oui_subtype != P2P_INV_RSP)
		goto out_rx_mgmt;

	vendor_ie = cfg80211_find_vendor_ie(WLAN_OUI_WFA, WLAN_OUI_TYPE_WFA_P2P,
					    buff + ie_offset, size - ie_offset);
	if (!vendor_ie)
		goto out_rx_mgmt;

	p = (struct wilc_vendor_specific_ie *)vendor_ie;
	wilc_wfi_cfg_parse_ch_attr(p->attr, p->tag_len - 4, vif->wilc->sta_ch);

out_rx_mgmt:
	cfg80211_rx_mgmt(&priv->wdev, freq, 0, buff, size, 0);
}

static void wilc_wfi_mgmt_tx_complete(void *priv, int status)
{
	struct wilc_p2p_mgmt_data *pv_data = priv;

	kfree(pv_data->buff);
	kfree(pv_data);
}

static void wilc_wfi_remain_on_channel_expired(struct wilc_vif *vif, u64 cookie)
{
	struct wilc_priv *priv = &vif->priv;
	struct wilc_wfi_p2p_listen_params *params = &priv->remain_on_ch_params;

	if (cookie != params->listen_cookie)
		return;

	priv->p2p_listen_state = false;

	cfg80211_remain_on_channel_expired(&priv->wdev, params->listen_cookie,
					   params->listen_ch, GFP_KERNEL);
}

static int remain_on_channel(struct wiphy *wiphy,
			     struct wireless_dev *wdev,
			     struct ieee80211_channel *chan,
			     unsigned int duration, u64 *cookie)
{
	int ret = 0;
	struct wilc_vif *vif = netdev_priv(wdev->netdev);
	struct wilc_priv *priv = &vif->priv;
	u64 id;

	if (wdev->iftype == NL80211_IFTYPE_AP) {
		netdev_dbg(vif->ndev, "Required while in AP mode\n");
		return ret;
	}

	id = ++priv->inc_roc_cookie;
	if (id == 0)
		id = ++priv->inc_roc_cookie;

	ret = wilc_remain_on_channel(vif, id, chan->hw_value,
				     wilc_wfi_remain_on_channel_expired);
	if (ret)
		return ret;

	vif->wilc->op_ch = chan->hw_value;

	priv->remain_on_ch_params.listen_ch = chan;
	priv->remain_on_ch_params.listen_cookie = id;
	*cookie = id;
	priv->p2p_listen_state = true;
	priv->remain_on_ch_params.listen_duration = duration;

	cfg80211_ready_on_channel(wdev, *cookie, chan, duration, GFP_KERNEL);
	mod_timer(&vif->hif_drv->remain_on_ch_timer,
		  jiffies + msecs_to_jiffies(duration + 1000));

	return ret;
}

static int cancel_remain_on_channel(struct wiphy *wiphy,
				    struct wireless_dev *wdev,
				    u64 cookie)
{
	struct wilc_vif *vif = netdev_priv(wdev->netdev);
	struct wilc_priv *priv = &vif->priv;

	if (cookie != priv->remain_on_ch_params.listen_cookie)
		return -ENOENT;

	return wilc_listen_state_expired(vif, cookie);
}

static int mgmt_tx(struct wiphy *wiphy,
		   struct wireless_dev *wdev,
		   struct cfg80211_mgmt_tx_params *params,
		   u64 *cookie)
{
	struct ieee80211_channel *chan = params->chan;
	unsigned int wait = params->wait;
	const u8 *buf = params->buf;
	size_t len = params->len;
	const struct ieee80211_mgmt *mgmt;
	struct wilc_p2p_mgmt_data *mgmt_tx;
	struct wilc_vif *vif = netdev_priv(wdev->netdev);
	struct wilc_priv *priv = &vif->priv;
	struct host_if_drv *wfi_drv = priv->hif_drv;
	struct wilc_vendor_specific_ie *p;
	struct wilc_p2p_pub_act_frame *d;
	int ie_offset = offsetof(struct ieee80211_mgmt, u) + sizeof(*d);
	const u8 *vendor_ie;
	int ret = 0;

	*cookie = get_random_u32();
	priv->tx_cookie = *cookie;
	mgmt = (const struct ieee80211_mgmt *)buf;

	if (!ieee80211_is_mgmt(mgmt->frame_control))
		goto out;

	mgmt_tx = kmalloc(sizeof(*mgmt_tx), GFP_KERNEL);
	if (!mgmt_tx) {
		ret = -ENOMEM;
		goto out;
	}

	mgmt_tx->buff = kmemdup(buf, len, GFP_KERNEL);
	if (!mgmt_tx->buff) {
		ret = -ENOMEM;
		kfree(mgmt_tx);
		goto out;
	}

	mgmt_tx->size = len;

	if (ieee80211_is_probe_resp(mgmt->frame_control)) {
		wilc_set_mac_chnl_num(vif, chan->hw_value);
		vif->wilc->op_ch = chan->hw_value;
		goto out_txq_add_pkt;
	}

	if (!ieee80211_is_public_action((struct ieee80211_hdr *)buf, len)) {
		if (chan)
			wilc_set_mac_chnl_num(vif, chan->hw_value);
		else
			wilc_set_mac_chnl_num(vif, vif->wilc->op_ch);

		goto out_set_timeout;
	}

	d = (struct wilc_p2p_pub_act_frame *)(&mgmt->u.action);
	if (d->oui_type != WLAN_OUI_TYPE_WFA_P2P ||
	    d->oui_subtype != GO_NEG_CONF) {
		wilc_set_mac_chnl_num(vif, chan->hw_value);
		vif->wilc->op_ch = chan->hw_value;
	}

	if (d->oui_subtype != P2P_INV_REQ && d->oui_subtype != P2P_INV_RSP)
		goto out_set_timeout;

	vendor_ie = cfg80211_find_vendor_ie(WLAN_OUI_WFA, WLAN_OUI_TYPE_WFA_P2P,
					    mgmt_tx->buff + ie_offset,
					    len - ie_offset);
	if (!vendor_ie)
		goto out_set_timeout;

	p = (struct wilc_vendor_specific_ie *)vendor_ie;
	wilc_wfi_cfg_parse_ch_attr(p->attr, p->tag_len - 4, vif->wilc->sta_ch);

out_set_timeout:
	wfi_drv->p2p_timeout = (jiffies + msecs_to_jiffies(wait));

out_txq_add_pkt:

	wilc_wlan_txq_add_mgmt_pkt(wdev->netdev, mgmt_tx,
				   mgmt_tx->buff, mgmt_tx->size,
				   wilc_wfi_mgmt_tx_complete);

out:

	return ret;
}

static int mgmt_tx_cancel_wait(struct wiphy *wiphy,
			       struct wireless_dev *wdev,
			       u64 cookie)
{
	struct wilc_vif *vif = netdev_priv(wdev->netdev);
	struct wilc_priv *priv = &vif->priv;
	struct host_if_drv *wfi_drv = priv->hif_drv;

	wfi_drv->p2p_timeout = jiffies;

	if (!priv->p2p_listen_state) {
		struct wilc_wfi_p2p_listen_params *params;

		params = &priv->remain_on_ch_params;

		cfg80211_remain_on_channel_expired(wdev,
						   params->listen_cookie,
						   params->listen_ch,
						   GFP_KERNEL);
	}

	return 0;
}

void wilc_update_mgmt_frame_registrations(struct wiphy *wiphy,
					  struct wireless_dev *wdev,
					  struct mgmt_frame_regs *upd)
{
	struct wilc *wl = wiphy_priv(wiphy);
	struct wilc_vif *vif = netdev_priv(wdev->netdev);
	u32 presp_bit = BIT(IEEE80211_STYPE_PROBE_REQ >> 4);
	u32 action_bit = BIT(IEEE80211_STYPE_ACTION >> 4);
	u32 pauth_bit = BIT(IEEE80211_STYPE_AUTH >> 4);

	if (wl->initialized) {
		bool prev = vif->mgmt_reg_stypes & presp_bit;
		bool now = upd->interface_stypes & presp_bit;

		if (now != prev)
			wilc_frame_register(vif, IEEE80211_STYPE_PROBE_REQ, now);

		prev = vif->mgmt_reg_stypes & action_bit;
		now = upd->interface_stypes & action_bit;

		if (now != prev)
			wilc_frame_register(vif, IEEE80211_STYPE_ACTION, now);

		prev = vif->mgmt_reg_stypes & pauth_bit;
		now = upd->interface_stypes & pauth_bit;
		if (now != prev)
			wilc_frame_register(vif, IEEE80211_STYPE_AUTH, now);
	}

	vif->mgmt_reg_stypes =
		upd->interface_stypes & (presp_bit | action_bit | pauth_bit);
}

static int external_auth(struct wiphy *wiphy, struct net_device *dev,
			 struct cfg80211_external_auth_params *auth)
{
	struct wilc_vif *vif = netdev_priv(dev);

	if (auth->status == WLAN_STATUS_SUCCESS)
		wilc_set_external_auth_param(vif, auth);

	return 0;
}

static int set_cqm_rssi_config(struct wiphy *wiphy, struct net_device *dev,
			       s32 rssi_thold, u32 rssi_hyst)
{
	return 0;
}

static int dump_station(struct wiphy *wiphy, struct net_device *dev,
			int idx, u8 *mac, struct station_info *sinfo)
{
	struct wilc_vif *vif = netdev_priv(dev);
	int ret;

	if (idx != 0)
		return -ENOENT;

	ret = wilc_get_rssi(vif, &sinfo->signal);
	if (ret)
		return ret;

	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL);
	memcpy(mac, vif->priv.associated_bss, ETH_ALEN);
	return 0;
}

static int set_power_mgmt(struct wiphy *wiphy, struct net_device *dev,
			  bool enabled, int timeout)
{
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc_priv *priv = &vif->priv;

	if (!priv->hif_drv)
		return -EIO;

	wilc_set_power_mgmt(vif, enabled, timeout);

	return 0;
}

static int change_virtual_intf(struct wiphy *wiphy, struct net_device *dev,
			       enum nl80211_iftype type,
			       struct vif_params *params)
{
	struct wilc *wl = wiphy_priv(wiphy);
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc_priv *priv = &vif->priv;

	switch (type) {
	case NL80211_IFTYPE_STATION:
		vif->connecting = false;
		dev->ieee80211_ptr->iftype = type;
		priv->wdev.iftype = type;
		vif->monitor_flag = 0;
		if (vif->iftype == WILC_AP_MODE || vif->iftype == WILC_GO_MODE)
			wilc_wfi_deinit_mon_interface(wl, true);
		vif->iftype = WILC_STATION_MODE;

		if (wl->initialized)
			wilc_set_operation_mode(vif, wilc_get_vif_idx(vif),
						WILC_STATION_MODE, vif->idx);

		memset(priv->assoc_stainfo.sta_associated_bss, 0,
		       WILC_MAX_NUM_STA * ETH_ALEN);
		break;

	case NL80211_IFTYPE_P2P_CLIENT:
		vif->connecting = false;
		dev->ieee80211_ptr->iftype = type;
		priv->wdev.iftype = type;
		vif->monitor_flag = 0;
		vif->iftype = WILC_CLIENT_MODE;

		if (wl->initialized)
			wilc_set_operation_mode(vif, wilc_get_vif_idx(vif),
						WILC_STATION_MODE, vif->idx);
		break;

	case NL80211_IFTYPE_AP:
		dev->ieee80211_ptr->iftype = type;
		priv->wdev.iftype = type;
		vif->iftype = WILC_AP_MODE;

		if (wl->initialized)
			wilc_set_operation_mode(vif, wilc_get_vif_idx(vif),
						WILC_AP_MODE, vif->idx);
		break;

	case NL80211_IFTYPE_P2P_GO:
		dev->ieee80211_ptr->iftype = type;
		priv->wdev.iftype = type;
		vif->iftype = WILC_GO_MODE;

		if (wl->initialized)
			wilc_set_operation_mode(vif, wilc_get_vif_idx(vif),
						WILC_AP_MODE, vif->idx);
		break;

	default:
		netdev_err(dev, "Unknown interface type= %d\n", type);
		return -EINVAL;
	}

	return 0;
}

static int start_ap(struct wiphy *wiphy, struct net_device *dev,
		    struct cfg80211_ap_settings *settings)
{
	struct wilc_vif *vif = netdev_priv(dev);
	int ret;

	ret = set_channel(wiphy, &settings->chandef);
	if (ret != 0)
		netdev_err(dev, "Error in setting channel\n");

	wilc_wlan_set_bssid(dev, dev->dev_addr, WILC_AP_MODE);

	return wilc_add_beacon(vif, settings->beacon_interval,
				   settings->dtim_period, &settings->beacon);
}

static int change_beacon(struct wiphy *wiphy, struct net_device *dev,
			 struct cfg80211_ap_update *params)
{
	struct wilc_vif *vif = netdev_priv(dev);

	return wilc_add_beacon(vif, 0, 0, &params->beacon);
}

static int stop_ap(struct wiphy *wiphy, struct net_device *dev,
		   unsigned int link_id)
{
	int ret;
	struct wilc_vif *vif = netdev_priv(dev);

	wilc_wlan_set_bssid(dev, NULL, WILC_AP_MODE);

	ret = wilc_del_beacon(vif);

	if (ret)
		netdev_err(dev, "Host delete beacon fail\n");

	return ret;
}

static int add_station(struct wiphy *wiphy, struct net_device *dev,
		       const u8 *mac, struct station_parameters *params)
{
	int ret = 0;
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc_priv *priv = &vif->priv;

	if (vif->iftype == WILC_AP_MODE || vif->iftype == WILC_GO_MODE) {
		memcpy(priv->assoc_stainfo.sta_associated_bss[params->aid], mac,
		       ETH_ALEN);

		ret = wilc_add_station(vif, mac, params);
		if (ret)
			netdev_err(dev, "Host add station fail\n");
	}

	return ret;
}

static int del_station(struct wiphy *wiphy, struct net_device *dev,
		       struct station_del_parameters *params)
{
	const u8 *mac = params->mac;
	int ret = 0;
	struct wilc_vif *vif = netdev_priv(dev);
	struct wilc_priv *priv = &vif->priv;
	struct sta_info *info;

	if (!(vif->iftype == WILC_AP_MODE || vif->iftype == WILC_GO_MODE))
		return ret;

	info = &priv->assoc_stainfo;

	if (!mac)
		ret = wilc_del_allstation(vif, info->sta_associated_bss);

	ret = wilc_del_station(vif, mac);
	if (ret)
		netdev_err(dev, "Host delete station fail\n");
	return ret;
}

static int change_station(struct wiphy *wiphy, struct net_device *dev,
			  const u8 *mac, struct station_parameters *params)
{
	int ret = 0;
	struct wilc_vif *vif = netdev_priv(dev);

	if (vif->iftype == WILC_AP_MODE || vif->iftype == WILC_GO_MODE) {
		ret = wilc_edit_station(vif, mac, params);
		if (ret)
			netdev_err(dev, "Host edit station fail\n");
	}
	return ret;
}

static struct wilc_vif *wilc_get_vif_from_type(struct wilc *wl, int type)
{
	struct wilc_vif *vif;

	list_for_each_entry_rcu(vif, &wl->vif_list, list) {
		if (vif->iftype == type)
			return vif;
	}

	return NULL;
}

static struct wireless_dev *add_virtual_intf(struct wiphy *wiphy,
					     const char *name,
					     unsigned char name_assign_type,
					     enum nl80211_iftype type,
					     struct vif_params *params)
{
	struct wilc *wl = wiphy_priv(wiphy);
	struct wilc_vif *vif;
	struct wireless_dev *wdev;
	int iftype;

	if (type == NL80211_IFTYPE_MONITOR) {
		struct net_device *ndev;
		int srcu_idx;

		srcu_idx = srcu_read_lock(&wl->srcu);
		vif = wilc_get_vif_from_type(wl, WILC_AP_MODE);
		if (!vif) {
			vif = wilc_get_vif_from_type(wl, WILC_GO_MODE);
			if (!vif) {
				srcu_read_unlock(&wl->srcu, srcu_idx);
				goto validate_interface;
			}
		}

		if (vif->monitor_flag) {
			srcu_read_unlock(&wl->srcu, srcu_idx);
			goto validate_interface;
		}

		ndev = wilc_wfi_init_mon_interface(wl, name, vif->ndev);
		if (ndev) {
			vif->monitor_flag = 1;
		} else {
			srcu_read_unlock(&wl->srcu, srcu_idx);
			return ERR_PTR(-EINVAL);
		}

		wdev = &vif->priv.wdev;
		srcu_read_unlock(&wl->srcu, srcu_idx);
		return wdev;
	}

validate_interface:
	mutex_lock(&wl->vif_mutex);
	if (wl->vif_num == WILC_NUM_CONCURRENT_IFC) {
		pr_err("Reached maximum number of interface\n");
		mutex_unlock(&wl->vif_mutex);
		return ERR_PTR(-EINVAL);
	}
	mutex_unlock(&wl->vif_mutex);

	switch (type) {
	case NL80211_IFTYPE_STATION:
		iftype = WILC_STATION_MODE;
		break;
	case NL80211_IFTYPE_AP:
		iftype = WILC_AP_MODE;
		break;
	default:
		return ERR_PTR(-EOPNOTSUPP);
	}

	vif = wilc_netdev_ifc_init(wl, name, iftype, type, true);
	if (IS_ERR(vif))
		return ERR_CAST(vif);

	return &vif->priv.wdev;
}

static int del_virtual_intf(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct wilc *wl = wiphy_priv(wiphy);
	struct wilc_vif *vif;

	if (wdev->iftype == NL80211_IFTYPE_AP ||
	    wdev->iftype == NL80211_IFTYPE_P2P_GO)
		wilc_wfi_deinit_mon_interface(wl, true);
	vif = netdev_priv(wdev->netdev);
	cfg80211_stop_iface(wiphy, wdev, GFP_KERNEL);
	cfg80211_unregister_netdevice(vif->ndev);
	vif->monitor_flag = 0;

	wilc_set_operation_mode(vif, 0, 0, 0);
	mutex_lock(&wl->vif_mutex);
	list_del_rcu(&vif->list);
	wl->vif_num--;
	mutex_unlock(&wl->vif_mutex);
	synchronize_srcu(&wl->srcu);
	return 0;
}

static int wilc_suspend(struct wiphy *wiphy, struct cfg80211_wowlan *wow)
{
	struct wilc *wl = wiphy_priv(wiphy);

	if (!wow && wilc_wlan_get_num_conn_ifcs(wl))
		wl->suspend_event = true;
	else
		wl->suspend_event = false;

	return 0;
}

static int wilc_resume(struct wiphy *wiphy)
{
	return 0;
}

static void wilc_set_wakeup(struct wiphy *wiphy, bool enabled)
{
	struct wilc *wl = wiphy_priv(wiphy);
	struct wilc_vif *vif;
	int srcu_idx;

	srcu_idx = srcu_read_lock(&wl->srcu);
	vif = wilc_get_wl_to_vif(wl);
	if (IS_ERR(vif)) {
		srcu_read_unlock(&wl->srcu, srcu_idx);
		return;
	}

	netdev_info(vif->ndev, "cfg set wake up = %d\n", enabled);
	wilc_set_wowlan_trigger(vif, enabled);
	srcu_read_unlock(&wl->srcu, srcu_idx);
}

static int set_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev,
			enum nl80211_tx_power_setting type, int mbm)
{
	int ret;
	int srcu_idx;
	s32 tx_power = MBM_TO_DBM(mbm);
	struct wilc *wl = wiphy_priv(wiphy);
	struct wilc_vif *vif;

	if (!wl->initialized)
		return -EIO;

	srcu_idx = srcu_read_lock(&wl->srcu);
	vif = wilc_get_wl_to_vif(wl);
	if (IS_ERR(vif)) {
		srcu_read_unlock(&wl->srcu, srcu_idx);
		return -EINVAL;
	}

	netdev_info(vif->ndev, "Setting tx power %d\n", tx_power);
	if (tx_power < 0)
		tx_power = 0;
	else if (tx_power > 18)
		tx_power = 18;
	ret = wilc_set_tx_power(vif, tx_power);
	if (ret)
		netdev_err(vif->ndev, "Failed to set tx power\n");
	srcu_read_unlock(&wl->srcu, srcu_idx);

	return ret;
}

static int get_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev,
			int *dbm)
{
	int ret;
	struct wilc_vif *vif = netdev_priv(wdev->netdev);
	struct wilc *wl = vif->wilc;

	/* If firmware is not started, return. */
	if (!wl->initialized)
		return -EIO;

	ret = wilc_get_tx_power(vif, (u8 *)dbm);
	if (ret)
		netdev_err(vif->ndev, "Failed to get tx power\n");

	return ret;
}

static const struct cfg80211_ops wilc_cfg80211_ops = {
	.set_monitor_channel = set_channel,
	.scan = scan,
	.connect = connect,
	.disconnect = disconnect,
	.add_key = add_key,
	.del_key = del_key,
	.get_key = get_key,
	.set_default_key = set_default_key,
	.set_default_mgmt_key = set_default_mgmt_key,
	.add_virtual_intf = add_virtual_intf,
	.del_virtual_intf = del_virtual_intf,
	.change_virtual_intf = change_virtual_intf,

	.start_ap = start_ap,
	.change_beacon = change_beacon,
	.stop_ap = stop_ap,
	.add_station = add_station,
	.del_station = del_station,
	.change_station = change_station,
	.get_station = get_station,
	.dump_station = dump_station,
	.change_bss = change_bss,
	.set_wiphy_params = set_wiphy_params,

	.external_auth = external_auth,
	.set_pmksa = set_pmksa,
	.del_pmksa = del_pmksa,
	.flush_pmksa = flush_pmksa,
	.remain_on_channel = remain_on_channel,
	.cancel_remain_on_channel = cancel_remain_on_channel,
	.mgmt_tx_cancel_wait = mgmt_tx_cancel_wait,
	.mgmt_tx = mgmt_tx,
	.update_mgmt_frame_registrations = wilc_update_mgmt_frame_registrations,
	.set_power_mgmt = set_power_mgmt,
	.set_cqm_rssi_config = set_cqm_rssi_config,

	.suspend = wilc_suspend,
	.resume = wilc_resume,
	.set_wakeup = wilc_set_wakeup,
	.set_tx_power = set_tx_power,
	.get_tx_power = get_tx_power,

};

static void wlan_init_locks(struct wilc *wl)
{
	mutex_init(&wl->hif_cs);
	mutex_init(&wl->rxq_cs);
	mutex_init(&wl->cfg_cmd_lock);
	mutex_init(&wl->vif_mutex);
	mutex_init(&wl->deinit_lock);

	spin_lock_init(&wl->txq_spinlock);
	mutex_init(&wl->txq_add_to_head_cs);

	init_completion(&wl->txq_event);
	init_completion(&wl->cfg_event);
	init_completion(&wl->sync_event);
	init_completion(&wl->txq_thread_started);
	init_srcu_struct(&wl->srcu);
}

void wlan_deinit_locks(struct wilc *wilc)
{
	mutex_destroy(&wilc->hif_cs);
	mutex_destroy(&wilc->rxq_cs);
	mutex_destroy(&wilc->cfg_cmd_lock);
	mutex_destroy(&wilc->txq_add_to_head_cs);
	mutex_destroy(&wilc->vif_mutex);
	mutex_destroy(&wilc->deinit_lock);
	cleanup_srcu_struct(&wilc->srcu);
}

int wilc_cfg80211_init(struct wilc **wilc, struct device *dev, int io_type,
		       const struct wilc_hif_func *ops)
{
	struct wilc *wl;
	struct wilc_vif *vif;
	int ret, i;

	wl = wilc_create_wiphy(dev);
	if (!wl)
		return -EINVAL;

	wlan_init_locks(wl);

	ret = wilc_wlan_cfg_init(wl);
	if (ret)
		goto free_wl;

	*wilc = wl;
	wl->io_type = io_type;
	wl->hif_func = ops;

	for (i = 0; i < NQUEUES; i++)
		INIT_LIST_HEAD(&wl->txq[i].txq_head.list);

	INIT_LIST_HEAD(&wl->rxq_head.list);
	INIT_LIST_HEAD(&wl->vif_list);

	vif = wilc_netdev_ifc_init(wl, "wlan%d", WILC_STATION_MODE,
				   NL80211_IFTYPE_STATION, false);
	if (IS_ERR(vif)) {
		ret = PTR_ERR(vif);
		goto free_cfg;
	}

	return 0;

free_cfg:
	wilc_wlan_cfg_deinit(wl);

free_wl:
	wlan_deinit_locks(wl);
	wiphy_unregister(wl->wiphy);
	wiphy_free(wl->wiphy);
	return ret;
}
EXPORT_SYMBOL_GPL(wilc_cfg80211_init);

struct wilc *wilc_create_wiphy(struct device *dev)
{
	struct wiphy *wiphy;
	struct wilc *wl;
	int ret;

	wiphy = wiphy_new(&wilc_cfg80211_ops, sizeof(*wl));
	if (!wiphy)
		return NULL;

	wl = wiphy_priv(wiphy);

	memcpy(wl->bitrates, wilc_bitrates, sizeof(wilc_bitrates));
	memcpy(wl->channels, wilc_2ghz_channels, sizeof(wilc_2ghz_channels));
	wl->band.bitrates = wl->bitrates;
	wl->band.n_bitrates = ARRAY_SIZE(wl->bitrates);
	wl->band.channels = wl->channels;
	wl->band.n_channels = ARRAY_SIZE(wilc_2ghz_channels);

	wl->band.ht_cap.ht_supported = 1;
	wl->band.ht_cap.cap |= (1 << IEEE80211_HT_CAP_RX_STBC_SHIFT);
	wl->band.ht_cap.mcs.rx_mask[0] = 0xff;
	wl->band.ht_cap.ampdu_factor = IEEE80211_HT_MAX_AMPDU_8K;
	wl->band.ht_cap.ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE;

	wiphy->bands[NL80211_BAND_2GHZ] = &wl->band;

	wiphy->max_scan_ssids = WILC_MAX_NUM_PROBED_SSID;
#ifdef CONFIG_PM
	wiphy->wowlan = &wowlan_support;
#endif
	wiphy->max_num_pmkids = WILC_MAX_NUM_PMKIDS;
	wiphy->max_scan_ie_len = 1000;
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	memcpy(wl->cipher_suites, wilc_cipher_suites,
	       sizeof(wilc_cipher_suites));
	wiphy->cipher_suites = wl->cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(wilc_cipher_suites);
	wiphy->mgmt_stypes = wilc_wfi_cfg80211_mgmt_types;

	wiphy->max_remain_on_channel_duration = 500;
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				BIT(NL80211_IFTYPE_AP) |
				BIT(NL80211_IFTYPE_MONITOR) |
				BIT(NL80211_IFTYPE_P2P_GO) |
				BIT(NL80211_IFTYPE_P2P_CLIENT);
	wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
	wiphy->features |= NL80211_FEATURE_SAE;
	set_wiphy_dev(wiphy, dev);
	wl->wiphy = wiphy;
	ret = wiphy_register(wiphy);
	if (ret) {
		wiphy_free(wiphy);
		return NULL;
	}
	return wl;
}

int wilc_init_host_int(struct net_device *net)
{
	int ret;
	struct wilc_vif *vif = netdev_priv(net);
	struct wilc_priv *priv = &vif->priv;

	priv->p2p_listen_state = false;

	mutex_init(&priv->scan_req_lock);
	ret = wilc_init(net, &priv->hif_drv);
	if (ret)
		netdev_err(net, "Error while initializing hostinterface\n");

	return ret;
}

void wilc_deinit_host_int(struct net_device *net)
{
	int ret;
	struct wilc_vif *vif = netdev_priv(net);
	struct wilc_priv *priv = &vif->priv;

	priv->p2p_listen_state = false;

	flush_workqueue(vif->wilc->hif_workqueue);
	mutex_destroy(&priv->scan_req_lock);
	ret = wilc_deinit(vif);

	if (ret)
		netdev_err(net, "Error while deinitializing host interface\n");
}

