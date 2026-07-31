// SPDX-License-Identifier: GPL-2.0-only
/*
 * nxpwifi: cfg80211 support
 *
 * Copyright 2011-2024 NXP
 */

#include "cfg80211.h"
#include "main.h"
#include "cmdevt.h"
#include "11n.h"
#include "wmm.h"

static const struct ieee80211_iface_limit nxpwifi_ap_sta_limits[] = {
	{
		.max = NXPWIFI_MAX_BSS_NUM,
		.types = BIT(NL80211_IFTYPE_STATION) |
			 BIT(NL80211_IFTYPE_AP) |
			 BIT(NL80211_IFTYPE_MONITOR),
	},
};

static const struct ieee80211_iface_combination
nxpwifi_iface_comb_ap_sta = {
	.limits = nxpwifi_ap_sta_limits,
	.num_different_channels = 1,
	.n_limits = ARRAY_SIZE(nxpwifi_ap_sta_limits),
	.max_interfaces = NXPWIFI_MAX_BSS_NUM,
	.beacon_int_infra_match = true,
	.radar_detect_widths =	BIT(NL80211_CHAN_WIDTH_20_NOHT) |
				BIT(NL80211_CHAN_WIDTH_20) |
				BIT(NL80211_CHAN_WIDTH_40),
};

static const struct ieee80211_iface_combination
nxpwifi_iface_comb_ap_sta_vht = {
	.limits = nxpwifi_ap_sta_limits,
	.num_different_channels = 1,
	.n_limits = ARRAY_SIZE(nxpwifi_ap_sta_limits),
	.max_interfaces = NXPWIFI_MAX_BSS_NUM,
	.beacon_int_infra_match = true,
	.radar_detect_widths =	BIT(NL80211_CHAN_WIDTH_20_NOHT) |
				BIT(NL80211_CHAN_WIDTH_20) |
				BIT(NL80211_CHAN_WIDTH_40) |
				BIT(NL80211_CHAN_WIDTH_80),
};

/* Map nl80211 channel types to secondary channel offsets */
u8 nxpwifi_chan_type_to_sec_chan_offset(enum nl80211_channel_type chan_type)
{
	switch (chan_type) {
	case NL80211_CHAN_NO_HT:
	case NL80211_CHAN_HT20:
		return IEEE80211_HT_PARAM_CHA_SEC_NONE;
	case NL80211_CHAN_HT40PLUS:
		return IEEE80211_HT_PARAM_CHA_SEC_ABOVE;
	case NL80211_CHAN_HT40MINUS:
		return IEEE80211_HT_PARAM_CHA_SEC_BELOW;
	default:
		return IEEE80211_HT_PARAM_CHA_SEC_NONE;
	}
}

/* Map IEEE HT secondary‑channel type to nl80211 channel type */
u8 nxpwifi_get_chan_type(struct nxpwifi_private *priv)
{
	struct nxpwifi_channel_band channel_band;
	int ret;

	ret = nxpwifi_get_chan_info(priv, &channel_band);

	if (!ret) {
		switch (channel_band.band_config.chan_width) {
		case CHAN_BW_20MHZ:
			if (IS_11N_ENABLED(priv))
				return NL80211_CHAN_HT20;
			else
				return NL80211_CHAN_NO_HT;
		case CHAN_BW_40MHZ:
			if (channel_band.band_config.chan2_offset ==
			    IEEE80211_HT_PARAM_CHA_SEC_ABOVE)
				return NL80211_CHAN_HT40PLUS;
			else
				return NL80211_CHAN_HT40MINUS;
		default:
			return NL80211_CHAN_HT20;
		}
	}

	return NL80211_CHAN_HT20;
}

/* Retrieve the driver private data from the wiphy */
static void *nxpwifi_cfg80211_get_adapter(struct wiphy *wiphy)
{
	return (void *)(*(unsigned long *)wiphy_priv(wiphy));
}

/* cfg80211 operation handler to delete a network key. */
static int
nxpwifi_cfg80211_del_key(struct wiphy *wiphy, struct wireless_dev *wdev,
			 int link_id, u8 key_index, bool pairwise,
			 const u8 *mac_addr)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(wdev->netdev);
	static const u8 bc_mac[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	const u8 *peer_mac = pairwise ? mac_addr : bc_mac;
	int ret;

	ret = nxpwifi_set_encode(priv, NULL, NULL, 0, key_index, peer_mac, 1);
	if (ret)
		nxpwifi_dbg(priv->adapter, ERROR,
			    "crypto keys deleted failed %d\n", ret);
	else
		nxpwifi_dbg(priv->adapter, INFO, "info: crypto keys deleted\n");

	return ret;
}

/* Build an skb containing a management frame */
static void
nxpwifi_form_mgmt_frame(struct sk_buff *skb, const u8 *buf, size_t len)
{
	u8 addr[ETH_ALEN];
	u16 pkt_len;
	u32 tx_control = 0, pkt_type = PKT_TYPE_MGMT;

	eth_broadcast_addr(addr);
	pkt_len = len + ETH_ALEN;

	skb_reserve(skb, NXPWIFI_MIN_DATA_HEADER_LEN +
		    NXPWIFI_MGMT_FRAME_HEADER_SIZE + sizeof(pkt_len));
	memcpy(skb_push(skb, sizeof(pkt_len)), &pkt_len, sizeof(pkt_len));

	memcpy(skb_push(skb, sizeof(tx_control)),
	       &tx_control, sizeof(tx_control));

	memcpy(skb_push(skb, sizeof(pkt_type)), &pkt_type, sizeof(pkt_type));

	/* Add packet data and address4 */
	skb_put_data(skb, buf, sizeof(struct ieee80211_hdr_3addr));
	skb_put_data(skb, addr, ETH_ALEN);
	skb_put_data(skb, buf + sizeof(struct ieee80211_hdr_3addr),
		     len - sizeof(struct ieee80211_hdr_3addr));

	skb->priority = TC_PRIO_BESTEFFORT;
	__net_timestamp(skb);
}

/* cfg80211 operation handler to transmit a management frame. */
static int
nxpwifi_cfg80211_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
			 struct cfg80211_mgmt_tx_params *params, u64 cookie)
{
	const u8 *buf = params->buf;
	size_t len = params->len;
	struct sk_buff *skb;
	u16 pkt_len;
	const struct ieee80211_mgmt *mgmt;
	struct nxpwifi_txinfo *tx_info;
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(wdev->netdev);

	if (!buf || !len) {
		nxpwifi_dbg(priv->adapter, ERROR, "invalid buffer and length\n");
		return -EINVAL;
	}

	mgmt = (const struct ieee80211_mgmt *)buf;
	if (GET_BSS_ROLE(priv) != NXPWIFI_BSS_ROLE_STA &&
	    ieee80211_is_probe_resp(mgmt->frame_control)) {
		/* Offloaded probe responses; skip TX in AP/GO mode */
		nxpwifi_dbg(priv->adapter, INFO,
			    "info: skip to send probe resp in AP or GO mode\n");
		return 0;
	}

	if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_UAP) {
		if (ieee80211_is_auth(mgmt->frame_control))
			nxpwifi_dbg(priv->adapter, MSG,
				    "auth: send auth to %pM\n", mgmt->da);
		if (ieee80211_is_deauth(mgmt->frame_control))
			nxpwifi_dbg(priv->adapter, MSG,
				    "auth: send deauth to %pM\n", mgmt->da);
		if (ieee80211_is_disassoc(mgmt->frame_control))
			nxpwifi_dbg(priv->adapter, MSG,
				    "assoc: send disassoc to %pM\n", mgmt->da);
		if (ieee80211_is_assoc_resp(mgmt->frame_control))
			nxpwifi_dbg(priv->adapter, MSG,
				    "assoc: send assoc resp to %pM\n",
				    mgmt->da);
		if (ieee80211_is_reassoc_resp(mgmt->frame_control))
			nxpwifi_dbg(priv->adapter, MSG,
				    "assoc: send reassoc resp to %pM\n",
				    mgmt->da);
	}

	pkt_len = len + ETH_ALEN;
	skb = dev_alloc_skb(NXPWIFI_MIN_DATA_HEADER_LEN +
			    NXPWIFI_MGMT_FRAME_HEADER_SIZE +
			    pkt_len + sizeof(pkt_len));

	if (!skb) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "allocate skb failed for management frame\n");
		return -ENOMEM;
	}

	tx_info = NXPWIFI_SKB_TXCB(skb);
	memset(tx_info, 0, sizeof(*tx_info));
	tx_info->bss_num = priv->bss_num;
	tx_info->bss_type = priv->bss_type;
	tx_info->pkt_len = pkt_len;

	nxpwifi_form_mgmt_frame(skb, buf, len);

	if (ieee80211_is_action(mgmt->frame_control))
		skb = nxpwifi_clone_skb_for_tx_status(priv,
						      skb,
				NXPWIFI_BUF_FLAG_ACTION_TX_STATUS, &cookie);
	else
		cfg80211_mgmt_tx_status(wdev, cookie, buf, len, true,
					GFP_ATOMIC);

	nxpwifi_queue_tx_pkt(priv, skb);

	nxpwifi_dbg(priv->adapter, INFO, "info: management frame transmitted\n");
	return 0;
}

/* cfg80211 operation handler to register a mgmt frame. */
static void
nxpwifi_cfg80211_update_mgmt_frame_registrations(struct wiphy *wiphy,
						 struct wireless_dev *wdev,
						 struct mgmt_frame_regs *upd)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(wdev->netdev);
	u32 mask = upd->interface_stypes;

	if (mask != priv->mgmt_frame_mask) {
		priv->mgmt_frame_mask = mask;
		if (priv->host_mlme_reg &&
		    GET_BSS_ROLE(priv) != NXPWIFI_BSS_ROLE_UAP)
			priv->mgmt_frame_mask |= HOST_MLME_MGMT_MASK;

		nxpwifi_mgmt_frame_reg(priv, priv->mgmt_frame_mask);

		nxpwifi_dbg(priv->adapter, INFO, "info: mgmt frame registered\n");
	}
}

/* cfg80211 operation handler to remain on channel. */
static int
nxpwifi_cfg80211_remain_on_channel(struct wiphy *wiphy,
				   struct wireless_dev *wdev,
				   struct ieee80211_channel *chan,
				   unsigned int duration, u64 cookie,
				   const u8 *rx_addr)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(wdev->netdev);
	struct nxpwifi_adapter *adapter = priv->adapter;
	int ret;

	if (!chan) {
		nxpwifi_dbg(adapter, ERROR, "Invalid parameter for ROC\n");
		return -EINVAL;
	}

	if (priv->roc_cfg.cookie) {
		nxpwifi_dbg(adapter, INFO,
			    "info: ongoing ROC, cookie = 0x%llx\n",
			    priv->roc_cfg.cookie);
		return -EBUSY;
	}

	ret = nxpwifi_remain_on_chan_cfg(priv, HOST_ACT_GEN_SET, chan,
					 duration);

	if (!ret) {
		priv->roc_cfg.cookie = cookie;
		priv->roc_cfg.chan = *chan;

		cfg80211_ready_on_channel(wdev, cookie, chan,
					  duration, GFP_ATOMIC);

		nxpwifi_dbg(adapter, INFO,
			    "info: ROC, cookie = 0x%llx\n", cookie);
	}

	return ret;
}

/* cfg80211 operation handler to cancel remain on channel. */
static int
nxpwifi_cfg80211_cancel_remain_on_channel(struct wiphy *wiphy,
					  struct wireless_dev *wdev, u64 cookie)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(wdev->netdev);
	int ret;

	if (cookie != priv->roc_cfg.cookie)
		return -ENOENT;

	ret = nxpwifi_remain_on_chan_cfg(priv, HOST_ACT_GEN_REMOVE,
					 &priv->roc_cfg.chan, 0);

	if (!ret) {
		cfg80211_remain_on_channel_expired(wdev, cookie,
						   &priv->roc_cfg.chan,
						   GFP_ATOMIC);

		memset(&priv->roc_cfg, 0, sizeof(struct nxpwifi_roc_cfg));

		nxpwifi_dbg(priv->adapter, INFO,
			    "info: cancel ROC, cookie = 0x%llx\n", cookie);
	}

	return ret;
}

/* cfg80211 operation handler to set Tx power. */
static int
nxpwifi_cfg80211_set_tx_power(struct wiphy *wiphy,
			      struct wireless_dev *wdev,
			      int radio_idx,
			      enum nl80211_tx_power_setting type,
			      int mbm)
{
	struct nxpwifi_adapter *adapter = nxpwifi_cfg80211_get_adapter(wiphy);
	struct nxpwifi_private *priv;
	struct nxpwifi_power_cfg power_cfg;
	int dbm = MBM_TO_DBM(mbm);

	switch (type) {
	case NL80211_TX_POWER_FIXED:
		power_cfg.is_power_auto = 0;
		power_cfg.is_power_fixed = 1;
		power_cfg.power_level = dbm;
		break;
	case NL80211_TX_POWER_LIMITED:
		power_cfg.is_power_auto = 0;
		power_cfg.is_power_fixed = 0;
		power_cfg.power_level = dbm;
		break;
	case NL80211_TX_POWER_AUTOMATIC:
		power_cfg.is_power_auto = 1;
		break;
	}

	priv = nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_ANY);

	return nxpwifi_set_tx_power(priv, &power_cfg);
}

/* cfg80211 operation handler to get Tx power. */
static int
nxpwifi_cfg80211_get_tx_power(struct wiphy *wiphy,
			      struct wireless_dev *wdev,
			      int radio_idx,
			      unsigned int link_id,
			      int *dbm)
{
	struct nxpwifi_adapter *adapter = nxpwifi_cfg80211_get_adapter(wiphy);
	struct nxpwifi_private *priv = nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_ANY);
	int ret = nxpwifi_get_tx_pwr(priv);

	if (ret < 0)
		return ret;

	*dbm = priv->tx_power_level;

	return 0;
}

/*
 * cfg80211 handler for setting IEEE 802.11 Power Save mode.
 *
 * The 'timeout' parameter is not supported and is ignored.
 */
static int
nxpwifi_cfg80211_set_power_mgmt(struct wiphy *wiphy,
				struct net_device *dev,
				bool enabled, int timeout)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(dev);
	u32 ps_mode;

	if (timeout)
		nxpwifi_dbg(priv->adapter, INFO,
			    "info: ignore timeout value for IEEE Power Save\n");

	ps_mode = enabled;

	return nxpwifi_drv_set_power(priv, &ps_mode);
}

/*
 * cfg80211 handler for setting the default WEP key.
 *
 * Ignored if WEP is not enabled.
 */
static int
nxpwifi_cfg80211_set_default_key(struct wiphy *wiphy, struct net_device *netdev,
				 int link_id, u8 key_index, bool unicast,
				 bool multicast)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(netdev);
	int ret = 0;

	/* Return if WEP key not configured */
	if (!priv->sec_info.wep_enabled)
		return 0;

	if (priv->bss_type == NXPWIFI_BSS_TYPE_UAP) {
		priv->wep_key_curr_index = key_index;
	} else {
		ret = nxpwifi_set_encode(priv, NULL, NULL, 0, key_index,
					 NULL, 0);
		if (ret)
			nxpwifi_dbg(priv->adapter, ERROR,
				    "failed to set default Tx key index\n");
	}

	return ret;
}

/* cfg80211 handler for adding an 802.11 encryption key. */
static int
nxpwifi_cfg80211_add_key(struct wiphy *wiphy, struct wireless_dev *wdev,
			 int link_id, u8 key_index, bool pairwise,
			 const u8 *mac_addr, struct key_params *params)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(wdev->netdev);
	struct nxpwifi_wep_key *wep_key;
	u8 bc_mac[ETH_ALEN];
	const u8 *peer_mac;
	int ret;

	eth_broadcast_addr(bc_mac);
	peer_mac = pairwise ? mac_addr : bc_mac;

	if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_UAP &&
	    (params->cipher == WLAN_CIPHER_SUITE_WEP40 ||
	     params->cipher == WLAN_CIPHER_SUITE_WEP104)) {
		if (params->key && params->key_len) {
			wep_key = &priv->wep_key[key_index];
			memset(wep_key, 0, sizeof(struct nxpwifi_wep_key));
			memcpy(wep_key->key_material, params->key,
			       params->key_len);
			wep_key->key_index = key_index;
			wep_key->key_length = params->key_len;
			priv->sec_info.wep_enabled = 1;
		}
		return 0;
	}

	ret = nxpwifi_set_encode(priv, params, params->key, params->key_len,
				 key_index, peer_mac, 0);
	if (ret)
		nxpwifi_dbg(priv->adapter, ERROR,
			    "failed to add crypto keys\n");

	return ret;
}

/* cfg80211 handler for setting the default management key. */
static int
nxpwifi_cfg80211_set_default_mgmt_key(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      int link_id,
				      u8 key_index)
{
	return 0;
}

/*
 * Sends regulatory domain information to the firmware.
 *
 * Includes:
 *   - Country code
 *   - Sub-band definitions (first channel, channel count, max TX power)
 */
int nxpwifi_send_domain_info_cmd_fw(struct wiphy *wiphy, enum nl80211_band band)
{
	u8 no_of_triplet = 0;
	struct ieee80211_country_ie_triplet *t;
	u8 no_of_parsed_chan = 0;
	u8 first_chan = 0, next_chan = 0, max_pwr = 0;
	u8 i, flag = 0;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	struct nxpwifi_adapter *adapter = nxpwifi_cfg80211_get_adapter(wiphy);
	struct nxpwifi_private *priv;
	struct nxpwifi_802_11d_domain_reg *domain_info = &adapter->domain_reg;
	int ret;

	domain_info->dfs_region = adapter->dfs_region;

	/* Set country code */
	domain_info->country_code[0] = adapter->country_code[0];
	domain_info->country_code[1] = adapter->country_code[1];
	domain_info->country_code[2] = ' ';

	if (!wiphy->bands[band]) {
		nxpwifi_dbg(adapter, ERROR,
			    "11D: setting domain info in FW\n");
		return -EINVAL;
	}

	sband = wiphy->bands[band];

	for (i = 0; i < sband->n_channels ; i++) {
		ch = &sband->channels[i];
		if (ch->flags & IEEE80211_CHAN_DISABLED)
			continue;

		if (!flag) {
			flag = 1;
			first_chan = (u32)ch->hw_value;
			next_chan = first_chan;
			max_pwr = ch->max_power;
			no_of_parsed_chan = 1;
			continue;
		}

		if (ch->hw_value == next_chan + 1 &&
		    ch->max_power == max_pwr) {
			next_chan++;
			no_of_parsed_chan++;
		} else {
			t = &domain_info->triplet[no_of_triplet];
			t->chans.first_channel = first_chan;
			t->chans.num_channels = no_of_parsed_chan;
			t->chans.max_power = max_pwr;
			no_of_triplet++;
			first_chan = (u32)ch->hw_value;
			next_chan = first_chan;
			max_pwr = ch->max_power;
			no_of_parsed_chan = 1;
		}
	}

	if (flag) {
		t = &domain_info->triplet[no_of_triplet];
		t->chans.first_channel = first_chan;
		t->chans.num_channels = no_of_parsed_chan;
		t->chans.max_power = max_pwr;
		no_of_triplet++;
	}

	domain_info->no_of_triplet = no_of_triplet;

	priv = nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_ANY);

	ret = nxpwifi_apply_regdomain(priv);

	if (ret)
		nxpwifi_dbg(adapter, INFO,
			    "11D: failed to set domain info in FW\n");

	return ret;
}

static void nxpwifi_reg_apply_radar_flags(struct wiphy *wiphy)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *chan;
	unsigned int i;

	if (!wiphy->bands[NL80211_BAND_5GHZ])
		return;
	sband = wiphy->bands[NL80211_BAND_5GHZ];

	for (i = 0; i < sband->n_channels; i++) {
		chan = &sband->channels[i];
		if ((!(chan->flags & IEEE80211_CHAN_DISABLED)) &&
		    (chan->flags & IEEE80211_CHAN_RADAR))
			chan->flags |= IEEE80211_CHAN_NO_IR;
	}
}

/*
 * cfg80211 regulatory domain change callback.
 *
 * Invoked when the regulatory domain is updated by:
 *   - the driver
 *   - the system core
 *   - the user
 *   - a received Country IE
 */
static void nxpwifi_reg_notifier(struct wiphy *wiphy,
				 struct regulatory_request *request)
{
	struct nxpwifi_adapter *adapter = nxpwifi_cfg80211_get_adapter(wiphy);

	nxpwifi_dbg(adapter, INFO,
		    "info: cfg80211 regulatory domain callback for %c%c\n",
		    request->alpha2[0], request->alpha2[1]);
	nxpwifi_reg_apply_radar_flags(wiphy);

	switch (request->initiator) {
	case NL80211_REGDOM_SET_BY_DRIVER:
	case NL80211_REGDOM_SET_BY_CORE:
	case NL80211_REGDOM_SET_BY_USER:
	case NL80211_REGDOM_SET_BY_COUNTRY_IE:
		break;
	default:
		nxpwifi_dbg(adapter, ERROR,
			    "unknown regdom initiator: %d\n",
			    request->initiator);
		return;
	}

	/* Skip world/unchanged regulatory domains. */
	if (strncmp(request->alpha2, "00", 2) &&
	    strncmp(request->alpha2, adapter->country_code,
		    sizeof(request->alpha2))) {
		memcpy(adapter->country_code, request->alpha2,
		       sizeof(request->alpha2));
		adapter->dfs_region = request->dfs_region;
		nxpwifi_send_domain_info_cmd_fw(wiphy, NL80211_BAND_2GHZ);
		if (adapter->fw_bands & BAND_A)
			nxpwifi_send_domain_info_cmd_fw(wiphy,
							NL80211_BAND_5GHZ);
	}
}

/*
 * cfg80211 op: set wiphy parameters.
 * Updates RTS/fragmentation thresholds and retry limits based on 'changed'
 * flags.
 */
static int
nxpwifi_cfg80211_set_wiphy_params(struct wiphy *wiphy, int radio_idx, u32 changed)
{
	struct nxpwifi_adapter *adapter = nxpwifi_cfg80211_get_adapter(wiphy);
	struct nxpwifi_private *priv;
	struct nxpwifi_uap_bss_param *bss_cfg;
	int ret = 0;

	priv = nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_ANY);

	switch (priv->bss_role) {
	case NXPWIFI_BSS_ROLE_UAP:
		bss_cfg = kzalloc_obj(*bss_cfg, GFP_KERNEL);
		if (!bss_cfg) {
			ret = -ENOMEM;
			break;
		}

		nxpwifi_set_sys_config_invalid_data(bss_cfg);

		if (changed & WIPHY_PARAM_RTS_THRESHOLD)
			bss_cfg->rts_threshold = wiphy->rts_threshold;
		if (changed & WIPHY_PARAM_FRAG_THRESHOLD)
			bss_cfg->frag_threshold = wiphy->frag_threshold;
		if (changed & WIPHY_PARAM_RETRY_LONG)
			bss_cfg->retry_limit = wiphy->retry_long;

		ret = nxpwifi_set_uap_sys_cfg(priv, bss_cfg);

		kfree(bss_cfg);
		if (ret)
			nxpwifi_dbg(adapter, ERROR,
				    "Failed to set wiphy phy params\n");
		break;

	case NXPWIFI_BSS_ROLE_STA:
		if (changed & WIPHY_PARAM_RTS_THRESHOLD) {
			ret = nxpwifi_set_rts(priv,
					      wiphy->rts_threshold);
			if (ret)
				break;
		}
		if (changed & WIPHY_PARAM_FRAG_THRESHOLD)
			ret = nxpwifi_set_frag(priv,
					       wiphy->frag_threshold);
		break;
	}

	return ret;
}

static int nxpwifi_deinit_priv_params(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	int ret = 0;

	if (priv->mgmt_frame_mask) {
		priv->mgmt_frame_mask = 0;
		ret = nxpwifi_mgmt_frame_reg(priv, priv->mgmt_frame_mask);

		if (ret) {
			nxpwifi_dbg(adapter, ERROR,
				    "could not unregister mgmt frame rx\n");
			return ret;
		}
		priv->host_mlme_reg = false;

	}

	nxpwifi_deauthenticate(priv, NULL);

	atomic_set(&adapter->iface_changing, 1);
	flush_workqueue(adapter->workqueue);
	flush_workqueue(adapter->rx_workqueue);
	nxpwifi_free_priv(priv);
	priv->wdev.iftype = NL80211_IFTYPE_UNSPECIFIED;
	priv->bss_mode = NL80211_IFTYPE_UNSPECIFIED;
	priv->sec_info.authentication_mode = NL80211_AUTHTYPE_OPEN_SYSTEM;

	return ret;
}

static int
nxpwifi_init_new_priv_params(struct nxpwifi_private *priv,
			     struct net_device *dev,
			     enum nl80211_iftype type)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	nxpwifi_init_priv(priv);

	priv->bss_mode = type;
	priv->wdev.iftype = type;

	nxpwifi_init_priv_params(priv, priv->netdev);
	priv->bss_started = 0;

	switch (type) {
	case NL80211_IFTYPE_STATION:
		priv->bss_role = NXPWIFI_BSS_ROLE_STA;
		break;
	case NL80211_IFTYPE_AP:
		priv->bss_role = NXPWIFI_BSS_ROLE_UAP;
		break;
	default:
		nxpwifi_dbg(adapter, ERROR,
			    "%s: changing to %d not supported\n",
			    dev->name, type);
		return -EOPNOTSUPP;
	}

	priv->bss_num = nxpwifi_get_unused_bss_num(adapter, priv->bss_type);

	flush_workqueue(adapter->workqueue);
	atomic_set(&adapter->iface_changing, 0);

	nxpwifi_set_mac_address(priv, dev, false, NULL);

	return 0;
}

static bool
is_vif_type_change_allowed(struct nxpwifi_adapter *adapter,
			   enum nl80211_iftype old_iftype,
			   enum nl80211_iftype new_iftype)
{
	switch (old_iftype) {
	case NL80211_IFTYPE_STATION:
		switch (new_iftype) {
		case NL80211_IFTYPE_AP:
			return adapter->curr_iface_comb.uap_intf !=
			       adapter->iface_limit.uap_intf;
		default:
			return false;
		}

	case NL80211_IFTYPE_AP:
		switch (new_iftype) {
		case NL80211_IFTYPE_STATION:
			return adapter->curr_iface_comb.sta_intf !=
			       adapter->iface_limit.sta_intf;
		default:
			return false;
		}

	default:
		break;
	}

	return false;
}

static void
update_vif_type_counter(struct nxpwifi_adapter *adapter,
			enum nl80211_iftype iftype,
			int change)
{
	switch (iftype) {
	case NL80211_IFTYPE_UNSPECIFIED:
	case NL80211_IFTYPE_STATION:
		adapter->curr_iface_comb.sta_intf += change;
		break;
	case NL80211_IFTYPE_AP:
		adapter->curr_iface_comb.uap_intf += change;
		break;
	case NL80211_IFTYPE_MONITOR:
		break;
	default:
		nxpwifi_dbg(adapter, ERROR,
			    "%s: Unsupported iftype passed: %d\n",
			    __func__, iftype);
		break;
	}
}

static int
nxpwifi_change_vif_to_sta(struct net_device *dev,
			  enum nl80211_iftype curr_iftype,
			  enum nl80211_iftype type,
			  struct vif_params *params)
{
	struct nxpwifi_private *priv;
	struct nxpwifi_adapter *adapter;
	int ret;

	priv = nxpwifi_netdev_get_priv(dev);

	if (!priv)
		return -EINVAL;

	adapter = priv->adapter;

	nxpwifi_dbg(adapter, INFO,
		    "%s: changing role to station\n", dev->name);

	ret = nxpwifi_deinit_priv_params(priv);
	if (ret)
		goto done;
	ret = nxpwifi_init_new_priv_params(priv, dev, type);
	if (ret)
		goto done;

	update_vif_type_counter(adapter, curr_iftype, -1);
	update_vif_type_counter(adapter, type, 1);
	dev->ieee80211_ptr->iftype = type;

	if (nxpwifi_set_bss_mode(priv))
		return -1;

	if (ret)
		goto done;

	ret = nxpwifi_sta_init_cmd(priv, false, false);

done:
	return ret;
}

static int
nxpwifi_change_vif_to_ap(struct net_device *dev,
			 enum nl80211_iftype curr_iftype,
			 enum nl80211_iftype type,
			 struct vif_params *params)
{
	struct nxpwifi_private *priv;
	struct nxpwifi_adapter *adapter;
	int ret;

	priv = nxpwifi_netdev_get_priv(dev);

	if (!priv)
		return -EINVAL;

	adapter = priv->adapter;

	nxpwifi_dbg(adapter, INFO,
		    "%s: changing role to AP\n", dev->name);

	ret = nxpwifi_deinit_priv_params(priv);
	if (ret)
		goto done;

	ret = nxpwifi_init_new_priv_params(priv, dev, type);
	if (ret)
		goto done;

	update_vif_type_counter(adapter, curr_iftype, -1);
	update_vif_type_counter(adapter, type, 1);
	dev->ieee80211_ptr->iftype = type;

	if (nxpwifi_set_bss_mode(priv))
		return -1;

	if (ret)
		goto done;

	ret = nxpwifi_sta_init_cmd(priv, false, false);

done:
	return ret;
}

/* cfg80211 operation handler to change interface type. */
static int
nxpwifi_cfg80211_change_virtual_intf(struct wiphy *wiphy,
				     struct net_device *dev,
				     enum nl80211_iftype type,
				     struct vif_params *params)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(dev);
	enum nl80211_iftype curr_iftype = dev->ieee80211_ptr->iftype;

	if (priv->scan_request) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "change virtual interface: scan in process\n");
		return -EBUSY;
	}

	if (type == NL80211_IFTYPE_UNSPECIFIED) {
		nxpwifi_dbg(priv->adapter, INFO,
			    "%s: no new type specified, keeping old type %d\n",
			    dev->name, curr_iftype);
		return 0;
	}

	if (curr_iftype == type) {
		nxpwifi_dbg(priv->adapter, INFO,
			    "%s: interface already is of type %d\n",
			    dev->name, curr_iftype);
		return 0;
	}

	if (!is_vif_type_change_allowed(priv->adapter, curr_iftype, type)) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "%s: change from type %d to %d is not allowed\n",
			    dev->name, curr_iftype, type);
		return -EOPNOTSUPP;
	}

	switch (curr_iftype) {
	case NL80211_IFTYPE_STATION:
		switch (type) {
		case NL80211_IFTYPE_AP:
			return nxpwifi_change_vif_to_ap(dev, curr_iftype, type,
							params);
		default:
			goto errnotsupp;
		}

	case NL80211_IFTYPE_AP:
		switch (type) {
		case NL80211_IFTYPE_STATION:
			return nxpwifi_change_vif_to_sta(dev, curr_iftype,
							 type, params);
			break;
		default:
			goto errnotsupp;
		}

	default:
		goto errnotsupp;
	}

	return 0;

errnotsupp:
	nxpwifi_dbg(priv->adapter, ERROR,
		    "unsupported interface type transition: %d to %d\n",
		    curr_iftype, type);
	return -EOPNOTSUPP;
}

#define RATE_FORMAT_LG  0
#define RATE_FORMAT_HT  1
#define RATE_FORMAT_VHT 2
#define RATE_FORMAT_HE  3

static void
nxpwifi_parse_htinfo(struct nxpwifi_private *priv, u8 rateinfo, u8 htinfo,
		     struct rate_info *rate)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	u8 rate_format;
	u8 he_dcm;
	u8 stbc;
	u8 gi;
	u8 bw;
	/* Bitrates in multiples of 100kb/s. */
	static const int legacy_rates[] = {
		[0] = 10,
		[1] = 20,
		[2] = 55,
		[3] = 110,
		[4] = 60, /* NXPWIFI_RATE_INDEX_OFDM0 */
		[5] = 60,
		[6] = 90,
		[7] = 120,
		[8] = 180,
		[9] = 240,
		[10] = 360,
		[11] = 480,
		[12] = 540,
	};

	rate_format = htinfo & 0x3;

	switch (rate_format) {
	case RATE_FORMAT_LG:
		if (rateinfo < ARRAY_SIZE(legacy_rates))
			rate->legacy = legacy_rates[rateinfo];
		break;
	case RATE_FORMAT_HT:
		rate->mcs = rateinfo;
		rate->flags |= RATE_INFO_FLAGS_MCS;
		break;
	case RATE_FORMAT_VHT:
		rate->mcs = rateinfo & 0xF;
		rate->flags |= RATE_INFO_FLAGS_VHT_MCS;
		break;
	case RATE_FORMAT_HE:
		rate->mcs = rateinfo & 0xF;
		rate->flags |= RATE_INFO_FLAGS_HE_MCS;
		he_dcm = 0; /* ToDo: ext_rate_info */
		gi = (htinfo & BIT(4)) >> 4 |
		     (htinfo & BIT(7)) >> 6;
		stbc = (htinfo & BIT(5)) >> 5;
		if (gi > 3) {
			nxpwifi_dbg(adapter, ERROR, "Invalid gi value\n");
			break;
		}
		if (gi == 3 && stbc && he_dcm) {
			gi = 0;
			stbc = 0;
			he_dcm = 0;
		}
		if (gi > 0)
			gi -= 1;
		rate->he_gi = gi;
		rate->he_dcm = he_dcm;
		break;
	}

	bw = (htinfo & 0xC) >> 2;

	switch (bw) {
	case 0:
		rate->bw = RATE_INFO_BW_20;
		break;
	case 1:
		rate->bw = RATE_INFO_BW_40;
		break;
	case 2:
		rate->bw = RATE_INFO_BW_80;
		break;
	case 3:
		rate->bw = RATE_INFO_BW_160;
		break;
	}

	if (rate_format != RATE_FORMAT_HE && (htinfo & BIT(4)))
		rate->flags |= RATE_INFO_FLAGS_SHORT_GI;

	if ((rateinfo >> 4) == 1)
		rate->nss = 2;
	else
		rate->nss = 1;
}

/*
 * Dump station statistics into station_info.
 * Includes bytes/packets counters, signal level, and TX/RX rates.
 */
static int
nxpwifi_dump_station_info(struct nxpwifi_private *priv,
			  struct nxpwifi_sta_node *node,
			  struct station_info *sinfo)
{
	u32 rate;
	int ret;

	sinfo->filled = BIT_ULL(NL80211_STA_INFO_RX_BYTES) |
			BIT_ULL(NL80211_STA_INFO_TX_BYTES) |
			BIT_ULL(NL80211_STA_INFO_RX_PACKETS) |
			BIT_ULL(NL80211_STA_INFO_TX_PACKETS) |
			BIT_ULL(NL80211_STA_INFO_TX_BITRATE) |
			BIT_ULL(NL80211_STA_INFO_SIGNAL) |
			BIT_ULL(NL80211_STA_INFO_SIGNAL_AVG);

	if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_UAP) {
		if (!node)
			return -ENOENT;

		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_INACTIVE_TIME) |
				BIT_ULL(NL80211_STA_INFO_TX_FAILED);
		sinfo->inactive_time =
			jiffies_to_msecs(jiffies - node->stats.last_rx);

		sinfo->signal = node->stats.rssi;
		sinfo->signal_avg = node->stats.rssi;
		sinfo->rx_bytes = node->stats.rx_bytes;
		sinfo->tx_bytes = node->stats.tx_bytes;
		sinfo->rx_packets = node->stats.rx_packets;
		sinfo->tx_packets = node->stats.tx_packets;
		sinfo->tx_failed = node->stats.tx_failed;

		nxpwifi_parse_htinfo(priv, priv->tx_rate,
				     node->stats.last_tx_htinfo,
				     &sinfo->txrate);
		sinfo->txrate.legacy = node->stats.last_tx_rate * 5;

		return 0;
	}

	/* Get signal information from the firmware */
	ret = nxpwifi_get_rssi_info(priv);
	if (ret) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "failed to get signal information\n");
		goto done;
	}

	ret = nxpwifi_drv_get_data_rate(priv, &rate);
	if (ret) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "getting data rate error\n");
		goto done;
	}

	/* Retrieve DTIM period value from firmware. */
	nxpwifi_get_802_11_snmp_mib(priv, DTIM_PERIOD_I, &priv->dtim_period);

	nxpwifi_parse_htinfo(priv, priv->tx_rate, priv->tx_htinfo,
			     &sinfo->txrate);

	sinfo->signal_avg = priv->bcn_rssi_avg;
	sinfo->rx_bytes = priv->stats.rx_bytes;
	sinfo->tx_bytes = priv->stats.tx_bytes;
	sinfo->rx_packets = priv->stats.rx_packets;
	sinfo->tx_packets = priv->stats.tx_packets;
	sinfo->signal = priv->bcn_rssi_avg;
	/* Convert bitrate from 500 kb/s units to 100 kb/s units. */
	sinfo->txrate.legacy = rate * 5;

	sinfo->filled |= BIT(NL80211_STA_INFO_RX_BITRATE);
	nxpwifi_parse_htinfo(priv, priv->rxpd_rate, priv->rxpd_htinfo,
			     &sinfo->rxrate);

	if (priv->bss_mode == NL80211_IFTYPE_STATION) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_BSS_PARAM);
		sinfo->bss_param.flags = 0;
		if (priv->curr_bss_params.bss_descriptor.cap_info_bitmap &
						WLAN_CAPABILITY_SHORT_PREAMBLE)
			sinfo->bss_param.flags |=
					BSS_PARAM_FLAGS_SHORT_PREAMBLE;
		if (priv->curr_bss_params.bss_descriptor.cap_info_bitmap &
						WLAN_CAPABILITY_SHORT_SLOT_TIME)
			sinfo->bss_param.flags |=
					BSS_PARAM_FLAGS_SHORT_SLOT_TIME;
		sinfo->bss_param.dtim_period = priv->dtim_period;
		sinfo->bss_param.beacon_interval =
			priv->curr_bss_params.bss_descriptor.beacon_period;
	}

done:
	return ret;
}

/*
 * cfg80211 op: get station information.
 * Works only when connected and fills station_info with current stats.
 */
static int
nxpwifi_cfg80211_get_station(struct wiphy *wiphy, struct wireless_dev *wdev,
			     const u8 *mac, struct station_info *sinfo)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(wdev->netdev);
	struct nxpwifi_sta_node *node;

	if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_STA) {
		if (!priv->media_connected ||
		    memcmp(mac, priv->cfg_bssid, ETH_ALEN))
			return -ENOENT;
		node = NULL;
	} else {
		rcu_read_lock();
		node = nxpwifi_get_sta_entry(priv, mac);
		rcu_read_unlock();
	}

	return nxpwifi_dump_station_info(priv, node, sinfo);
}

/* cfg80211 operation handler to dump station information. */
static int
nxpwifi_cfg80211_dump_station(struct wiphy *wiphy, struct wireless_dev *wdev,
			      int idx, u8 *mac, struct station_info *sinfo)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(wdev->netdev);
	struct nxpwifi_sta_node *node;
	struct nxpwifi_sta_node *found = NULL;
	int i;

	if ((GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_STA) &&
	    priv->media_connected && idx == 0) {
		ether_addr_copy(mac, priv->cfg_bssid);
		return nxpwifi_dump_station_info(priv, NULL, sinfo);
	} else if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_UAP) {
		nxpwifi_ap_get_sta_list(priv);

		i = 0;
		rcu_read_lock();
		list_for_each_entry_rcu(node, &priv->sta_list, list) {
			if (i++ != idx)
				continue;
			found = node;
			break;
		}
		rcu_read_unlock();

		if (found) {
			ether_addr_copy(mac, node->mac_addr);
			return nxpwifi_dump_station_info(priv, node, sinfo);
		}
	}

	return -ENOENT;
}

static int
nxpwifi_cfg80211_dump_survey(struct wiphy *wiphy, struct net_device *dev,
			     int idx, struct survey_info *survey)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(dev);
	struct nxpwifi_chan_stats *pchan_stats = priv->adapter->chan_stats;
	enum nl80211_band band;
	u8 chan_num;

	nxpwifi_dbg(priv->adapter, DUMP, "dump_survey idx=%d\n", idx);

	memset(survey, 0, sizeof(struct survey_info));

	if ((GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_STA) &&
	    priv->media_connected && idx == 0) {
		u8 curr_bss_band = priv->curr_bss_params.band;
		u32 chan = priv->curr_bss_params.bss_descriptor.channel;

		band = nxpwifi_band_to_radio_type(curr_bss_band);
		survey->channel = ieee80211_get_channel
				  (wiphy,
				   ieee80211_channel_to_frequency(chan, band));

		if (priv->bcn_nf_last) {
			survey->filled = SURVEY_INFO_NOISE_DBM;
			survey->noise = priv->bcn_nf_last;
		}
		return 0;
	}

	if (idx >= priv->adapter->num_in_chan_stats)
		return -ENOENT;

	if (!pchan_stats[idx].cca_scan_dur)
		return 0;

	band = pchan_stats[idx].bandcfg;
	chan_num = pchan_stats[idx].chan_num;
	survey->channel = ieee80211_get_channel
			  (wiphy,
			   ieee80211_channel_to_frequency(chan_num, band));
	survey->filled = SURVEY_INFO_NOISE_DBM |
			 SURVEY_INFO_TIME |
			 SURVEY_INFO_TIME_BUSY;
	survey->noise = pchan_stats[idx].noise;
	survey->time = pchan_stats[idx].cca_scan_dur;
	survey->time_busy = pchan_stats[idx].cca_busy_dur;

	return 0;
}

/* Supported rates to be advertised to the cfg80211 */
static struct ieee80211_rate nxpwifi_rates[] = {
	{.bitrate = 10, .hw_value = 2, },
	{.bitrate = 20, .hw_value = 4, },
	{.bitrate = 55, .hw_value = 11, },
	{.bitrate = 110, .hw_value = 22, },
	{.bitrate = 60, .hw_value = 12, },
	{.bitrate = 90, .hw_value = 18, },
	{.bitrate = 120, .hw_value = 24, },
	{.bitrate = 180, .hw_value = 36, },
	{.bitrate = 240, .hw_value = 48, },
	{.bitrate = 360, .hw_value = 72, },
	{.bitrate = 480, .hw_value = 96, },
	{.bitrate = 540, .hw_value = 108, },
};

/* Channel definitions to be advertised to cfg80211 */
static struct ieee80211_channel nxpwifi_channels_2ghz[] = {
	{.center_freq = 2412, .hw_value = 1, },
	{.center_freq = 2417, .hw_value = 2, },
	{.center_freq = 2422, .hw_value = 3, },
	{.center_freq = 2427, .hw_value = 4, },
	{.center_freq = 2432, .hw_value = 5, },
	{.center_freq = 2437, .hw_value = 6, },
	{.center_freq = 2442, .hw_value = 7, },
	{.center_freq = 2447, .hw_value = 8, },
	{.center_freq = 2452, .hw_value = 9, },
	{.center_freq = 2457, .hw_value = 10, },
	{.center_freq = 2462, .hw_value = 11, },
	{.center_freq = 2467, .hw_value = 12, },
	{.center_freq = 2472, .hw_value = 13, },
	{.center_freq = 2484, .hw_value = 14, },
};

static struct ieee80211_supported_band nxpwifi_band_2ghz = {
	.band = NL80211_BAND_2GHZ,
	.channels = nxpwifi_channels_2ghz,
	.n_channels = ARRAY_SIZE(nxpwifi_channels_2ghz),
	.bitrates = nxpwifi_rates,
	.n_bitrates = ARRAY_SIZE(nxpwifi_rates),
};

static struct ieee80211_channel nxpwifi_channels_5ghz[] = {
	{.center_freq = 5040, .hw_value = 8, },
	{.center_freq = 5060, .hw_value = 12, },
	{.center_freq = 5080, .hw_value = 16, },
	{.center_freq = 5170, .hw_value = 34, },
	{.center_freq = 5190, .hw_value = 38, },
	{.center_freq = 5210, .hw_value = 42, },
	{.center_freq = 5230, .hw_value = 46, },
	{.center_freq = 5180, .hw_value = 36, },
	{.center_freq = 5200, .hw_value = 40, },
	{.center_freq = 5220, .hw_value = 44, },
	{.center_freq = 5240, .hw_value = 48, },
	{.center_freq = 5260, .hw_value = 52, },
	{.center_freq = 5280, .hw_value = 56, },
	{.center_freq = 5300, .hw_value = 60, },
	{.center_freq = 5320, .hw_value = 64, },
	{.center_freq = 5500, .hw_value = 100, },
	{.center_freq = 5520, .hw_value = 104, },
	{.center_freq = 5540, .hw_value = 108, },
	{.center_freq = 5560, .hw_value = 112, },
	{.center_freq = 5580, .hw_value = 116, },
	{.center_freq = 5600, .hw_value = 120, },
	{.center_freq = 5620, .hw_value = 124, },
	{.center_freq = 5640, .hw_value = 128, },
	{.center_freq = 5660, .hw_value = 132, },
	{.center_freq = 5680, .hw_value = 136, },
	{.center_freq = 5700, .hw_value = 140, },
	{.center_freq = 5745, .hw_value = 149, },
	{.center_freq = 5765, .hw_value = 153, },
	{.center_freq = 5785, .hw_value = 157, },
	{.center_freq = 5805, .hw_value = 161, },
	{.center_freq = 5825, .hw_value = 165, },
};

static struct ieee80211_supported_band nxpwifi_band_5ghz = {
	.band = NL80211_BAND_5GHZ,
	.channels = nxpwifi_channels_5ghz,
	.n_channels = ARRAY_SIZE(nxpwifi_channels_5ghz),
	.bitrates = nxpwifi_rates + 4,
	.n_bitrates = ARRAY_SIZE(nxpwifi_rates) - 4,
};

/* Supported crypto cipher suits to be advertised to cfg80211 */
static const u32 nxpwifi_cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
	WLAN_CIPHER_SUITE_SMS4,
	WLAN_CIPHER_SUITE_AES_CMAC,
	WLAN_CIPHER_SUITE_GCMP_256,
	WLAN_CIPHER_SUITE_CCMP_256,
	WLAN_CIPHER_SUITE_BIP_GMAC_256,
	WLAN_CIPHER_SUITE_BIP_CMAC_256,
};

/* Supported mgmt frame types to be advertised to cfg80211 */
static const struct ieee80211_txrx_stypes
nxpwifi_mgmt_stypes[NUM_NL80211_IFTYPES] = {
	[NL80211_IFTYPE_STATION] = {
		.tx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		      BIT(IEEE80211_STYPE_PROBE_RESP >> 4),
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		      BIT(IEEE80211_STYPE_PROBE_REQ >> 4),
	},
	[NL80211_IFTYPE_AP] = {
		.tx = 0xffff,
		.rx = BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
		      BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
		      BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
		      BIT(IEEE80211_STYPE_DISASSOC >> 4) |
		      BIT(IEEE80211_STYPE_AUTH >> 4) |
		      BIT(IEEE80211_STYPE_DEAUTH >> 4) |
		      BIT(IEEE80211_STYPE_ACTION >> 4),
	},
};

/*
 * cfg80211 op: set bitrate mask.
 * Converts cfg80211 bitrate selections into firmware bitmap format.
 */
static int
nxpwifi_cfg80211_set_bitrate_mask(struct wiphy *wiphy,
				  struct net_device *dev,
				  unsigned int link_id,
				  const u8 *peer,
				  const struct cfg80211_bitrate_mask *mask)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(dev);
	u16 bitmap_rates[MAX_BITMAP_RATES_SIZE];
	enum nl80211_band band;
	struct nxpwifi_adapter *adapter = priv->adapter;

	if (!priv->media_connected) {
		nxpwifi_dbg(adapter, ERROR,
			    "Can not set Tx data rate in disconnected state\n");
		return -EINVAL;
	}

	band = nxpwifi_band_to_radio_type(priv->curr_bss_params.band);

	memset(bitmap_rates, 0, sizeof(bitmap_rates));

	/* Fill HR/DSSS legacy rates (2.4 GHz only). */
	if (band == NL80211_BAND_2GHZ)
		bitmap_rates[0] = mask->control[band].legacy & 0x000f;

	/* Fill OFDM legacy rates. */
	if (band == NL80211_BAND_2GHZ)
		bitmap_rates[1] = (mask->control[band].legacy & 0x0ff0) >> 4;
	else
		bitmap_rates[1] = mask->control[band].legacy;

	/* Fill HT MCS bitmap (1x1 or 2x2 depending on hardware). */
	bitmap_rates[2] = mask->control[band].ht_mcs[0];
	if (adapter->hw_dev_mcs_support == HT_STREAM_2X2)
		bitmap_rates[2] |= mask->control[band].ht_mcs[1] << 8;

	/* Fill VHT MCS bitmap if supported by firmware. */
	if (adapter->fw_api_ver == NXPWIFI_FW_V15) {
		bitmap_rates[10] = mask->control[band].vht_mcs[0];
		if (adapter->hw_dev_mcs_support == HT_STREAM_2X2)
			bitmap_rates[11] = mask->control[band].vht_mcs[1];
	}

	return nxpwifi_set_tx_rate(priv, bitmap_rates);
}

/*
 * cfg80211 op: configure connection-quality monitoring.
 * Subscribes or unsubscribes HIGH_RSSI and LOW_RSSI events to firmware.
 */
static int nxpwifi_cfg80211_set_cqm_rssi_config(struct wiphy *wiphy,
						struct net_device *dev,
						s32 rssi_thold, u32 rssi_hyst)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(dev);
	struct nxpwifi_ds_misc_subsc_evt subsc_evt;
	int ret = 0;

	priv->cqm_rssi_thold = rssi_thold;
	priv->cqm_rssi_hyst = rssi_hyst;

	memset(&subsc_evt, 0x00, sizeof(struct nxpwifi_ds_misc_subsc_evt));
	subsc_evt.events = BITMASK_BCN_RSSI_LOW | BITMASK_BCN_RSSI_HIGH;

	/* Subscribe/unsubscribe low and high rssi events */
	if (rssi_thold && rssi_hyst) {
		subsc_evt.action = HOST_ACT_BITWISE_SET;
		subsc_evt.bcn_l_rssi_cfg.abs_value = abs(rssi_thold);
		subsc_evt.bcn_h_rssi_cfg.abs_value = abs(rssi_thold);
		subsc_evt.bcn_l_rssi_cfg.evt_freq = 1;
		subsc_evt.bcn_h_rssi_cfg.evt_freq = 1;
		ret = nxpwifi_802_11_subscribe_event(priv, &subsc_evt);
	} else {
		subsc_evt.action = HOST_ACT_BITWISE_CLR;
		ret = nxpwifi_802_11_subscribe_event(priv, &subsc_evt);
	}

	return ret;
}

/*
 * cfg80211 operation handler for change_beacon.
 * Function retrieves and sets modified management IEs to FW.
 */
int nxpwifi_cfg80211_change_beacon(struct wiphy *wiphy,
				   struct net_device *dev,
				   struct cfg80211_ap_update *params)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(dev);
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct cfg80211_beacon_data *data = &params->beacon;
	int ret;

	nxpwifi_cancel_scan(adapter);

	if (GET_BSS_ROLE(priv) != NXPWIFI_BSS_ROLE_UAP) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "%s: bss_type mismatched\n", __func__);
		return -EINVAL;
	}

	ret = nxpwifi_set_mgmt_ies(priv, data);
	if (ret)
		nxpwifi_dbg(priv->adapter, ERROR,
			    "%s: setting mgmt ies failed\n", __func__);

	return ret;
}

/*
 * cfg80211 operation handler for del_station.
 * Function deauthenticates station which value is provided in mac parameter.
 * If mac is NULL/broadcast, all stations in associated station list are
 * deauthenticated. If bss is not started or there are no stations in
 * associated stations list, no action is taken.
 */
static int
nxpwifi_cfg80211_del_station(struct wiphy *wiphy, struct wireless_dev *wdev,
			     struct station_del_parameters *params)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(wdev->netdev);
	struct nxpwifi_sta_node *sta_node;
	u8 deauth_mac[ETH_ALEN];
	int ret = 0;

	if (!priv->bss_started && priv->wdev.links[0].cac_started) {
		nxpwifi_dbg(priv->adapter, INFO, "%s: abort CAC!\n", __func__);
		nxpwifi_abort_cac(priv);
	}

	if (list_empty(&priv->sta_list) || !priv->bss_started)
		return 0;

	if (!params->mac || is_broadcast_ether_addr(params->mac))
		return 0;

	nxpwifi_dbg(priv->adapter, INFO, "%s: mac address %pM\n",
		    __func__, params->mac);

	eth_zero_addr(deauth_mac);

	sta_node = nxpwifi_get_sta_entry(priv, params->mac);
	if (sta_node)
		ether_addr_copy(deauth_mac, params->mac);

	if (is_valid_ether_addr(deauth_mac)) {
		ret = nxpwifi_uap_sta_deauth(priv, deauth_mac);
		nxpwifi_del_sta_entry(priv, deauth_mac);
	}
	return ret;
}

static int
nxpwifi_cfg80211_set_antenna(struct wiphy *wiphy, int radio_idx, u32 tx_ant, u32 rx_ant)
{
	struct nxpwifi_adapter *adapter = nxpwifi_cfg80211_get_adapter(wiphy);
	struct nxpwifi_private *priv = nxpwifi_get_priv(adapter,
							NXPWIFI_BSS_ROLE_ANY);
	struct nxpwifi_ds_ant_cfg ant_cfg;

	if (!tx_ant || !rx_ant)
		return -EOPNOTSUPP;

	if (adapter->hw_dev_mcs_support != HT_STREAM_2X2) {
		/*
		 * Not a MIMO chip. User should provide specific antenna number
		 * for Tx/Rx path or enable all antennas for diversity
		 */
		if (tx_ant != rx_ant)
			return -EOPNOTSUPP;

		if ((tx_ant & (tx_ant - 1)) &&
		    (tx_ant != BIT(adapter->number_of_antenna) - 1))
			return -EOPNOTSUPP;

		if ((tx_ant == BIT(adapter->number_of_antenna) - 1) &&
		    priv->adapter->number_of_antenna > 1) {
			tx_ant = RF_ANTENNA_AUTO;
			rx_ant = RF_ANTENNA_AUTO;
		}
	} else {
		struct ieee80211_sta_ht_cap *ht_info;
		int rx_mcs_supp;
		enum nl80211_band band;

		if ((tx_ant == 0x1 && rx_ant == 0x1)) {
			adapter->user_dev_mcs_support = HT_STREAM_1X1;
			if (adapter->is_hw_11ac_capable)
				adapter->usr_dot_11ac_mcs_support =
						NXPWIFI_11AC_MCS_MAP_1X1;
		} else {
			adapter->user_dev_mcs_support = HT_STREAM_2X2;
			if (adapter->is_hw_11ac_capable)
				adapter->usr_dot_11ac_mcs_support =
						NXPWIFI_11AC_MCS_MAP_2X2;
		}

		for (band = 0; band < NUM_NL80211_BANDS; band++) {
			if (!adapter->wiphy->bands[band])
				continue;

			ht_info = &adapter->wiphy->bands[band]->ht_cap;
			rx_mcs_supp =
				GET_RXMCSSUPP(adapter->user_dev_mcs_support);
			memset(&ht_info->mcs, 0, adapter->number_of_antenna);
			memset(&ht_info->mcs, 0xff, rx_mcs_supp);
		}
	}

	ant_cfg.tx_ant = tx_ant;
	ant_cfg.rx_ant = rx_ant;

	return nxpwifi_set_rf_antenna(priv, &ant_cfg);
}

static int
nxpwifi_cfg80211_get_antenna(struct wiphy *wiphy, int radio_idx, u32 *tx_ant, u32 *rx_ant)
{
	struct nxpwifi_adapter *adapter = nxpwifi_cfg80211_get_adapter(wiphy);
	struct nxpwifi_private *priv = nxpwifi_get_priv(adapter,
							NXPWIFI_BSS_ROLE_ANY);
	int ret;

	ret = nxpwifi_get_rf_antenna(priv, tx_ant, rx_ant);

	return ret;
}

/*
 * cfg80211 op: stop AP.
 * Stops the BSS running on the uAP interface.
 */
static int nxpwifi_cfg80211_stop_ap(struct wiphy *wiphy, struct net_device *dev,
				    unsigned int link_id)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(dev);
	int ret;

	nxpwifi_abort_cac(priv);

	if (nxpwifi_del_mgmt_ies(priv))
		nxpwifi_dbg(priv->adapter, ERROR,
			    "Failed to delete mgmt IEs!\n");

	priv->ap_11n_enabled = 0;
	memset(&priv->bss_cfg, 0, sizeof(priv->bss_cfg));

	ret = nxpwifi_ap_stop_bss(priv);
	if (ret) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "Failed to stop the BSS\n");
		goto done;
	}

	ret = nxpwifi_ap_sys_reset(priv);
	if (ret) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "Failed to reset BSS\n");
		goto done;
	}

	netif_carrier_off(priv->netdev);
	nxpwifi_stop_net_dev_queue(priv->netdev, priv->adapter);

	if (atomic_dec_and_test(&priv->adapter->uap_count)) {
		priv->adapter->chandef_valid = false;
		memset(&priv->adapter->chandef, 0, sizeof(priv->adapter->chandef));
	}

done:
	return ret;
}

/*
 * cfg80211 op: start AP.
 * Applies beacon/DTIM/SSID/security settings to the uAP configuration and
 * starts the BSS.
 */
static int nxpwifi_cfg80211_start_ap(struct wiphy *wiphy,
				     struct net_device *dev,
				     struct cfg80211_ap_settings *params)
{
	struct nxpwifi_uap_bss_param *bss_cfg;
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(dev);
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct cfg80211_chan_def use_chandef;
	bool is_first_uap = false;
	int ret;

	/*
	 * Adapter is the HW channel owner (single PHY).
	 * All UAP interfaces on the same adapter must share
	 * the same RF channel.
	 */
	use_chandef = params->chandef;

	if (adapter->chandef_valid) {
		if (!cfg80211_chandef_identical(&adapter->chandef,
						&params->chandef)) {
			nxpwifi_dbg(adapter, INFO,
				    "UAP already running on channel %d, ignore requested channel %d\n",
				    adapter->chandef.chan->hw_value,
				    params->chandef.chan->hw_value);
		}
		use_chandef = adapter->chandef;
	} else	{
		is_first_uap = true;
	}

	if (GET_BSS_ROLE(priv) != NXPWIFI_BSS_ROLE_UAP)
		return -EINVAL;

	if (!nxpwifi_is_channel_setting_allowable(priv, params->chandef.chan))
		return -EOPNOTSUPP;

	bss_cfg = kzalloc_obj(*bss_cfg, GFP_KERNEL);
	if (!bss_cfg)
		return -ENOMEM;

	nxpwifi_set_sys_config_invalid_data(bss_cfg);

	memcpy(bss_cfg->mac_addr, priv->curr_addr, ETH_ALEN);

	if (params->beacon_interval)
		bss_cfg->beacon_period = params->beacon_interval;
	if (params->dtim_period)
		bss_cfg->dtim_period = params->dtim_period;

	if (params->ssid && params->ssid_len) {
		memcpy(bss_cfg->ssid.ssid, params->ssid, params->ssid_len);
		bss_cfg->ssid.ssid_len = params->ssid_len;
	}
	if (params->inactivity_timeout > 0) {
		/* sta_ao_timer/ps_sta_ao_timer is in unit of 100ms */
		bss_cfg->sta_ao_timer = 10 * params->inactivity_timeout;
		bss_cfg->ps_sta_ao_timer = 10 * params->inactivity_timeout;
	}

	/* Default: SSID is visible */
	bss_cfg->bcast_ssid_ctl = NXPWIFI_BCAST_SSID_VISIBLE;

	switch (params->hidden_ssid) {
	case NL80211_HIDDEN_SSID_NOT_IN_USE:
		bss_cfg->bcast_ssid_ctl = NXPWIFI_BCAST_SSID_VISIBLE;
		break;
	case NL80211_HIDDEN_SSID_ZERO_LEN:
		bss_cfg->bcast_ssid_ctl = NXPWIFI_BCAST_SSID_HIDE_LEN_ZERO;
		break;
	case NL80211_HIDDEN_SSID_ZERO_CONTENTS:
		bss_cfg->bcast_ssid_ctl = NXPWIFI_BCAST_SSID_HIDE_LEN_RETAIN;
		break;
	}

	nxpwifi_uap_set_channel(priv, bss_cfg, use_chandef);
	nxpwifi_set_uap_rates(bss_cfg, params);

	ret = nxpwifi_set_secure_params(priv, bss_cfg, params);
	if (ret) {
		nxpwifi_dbg(adapter, ERROR,
			    "Failed to parse security parameters!\n");
		goto done;
	}

	nxpwifi_set_ht_params(priv, bss_cfg, params);

	if (adapter->is_hw_11ac_capable) {
		nxpwifi_set_vht_params(priv, bss_cfg, params);
		nxpwifi_set_vht_width(priv, use_chandef.width,
				      priv->ap_11ac_enabled);
	}

	if (priv->ap_11ac_enabled)
		nxpwifi_set_11ac_ba_params(priv);
	else
		nxpwifi_set_ba_params(priv);

	if (adapter->is_hw_11ax_capable) {
		priv->ap_11ax_enabled =
			nxpwifi_check_11ax_capability(priv, bss_cfg, params);
		if (priv->ap_11ax_enabled)
			nxpwifi_set_11ax_status(priv, bss_cfg, params);
	}

	nxpwifi_set_wmm_params(priv, bss_cfg, params);

	if (nxpwifi_is_11h_active(priv))
		nxpwifi_set_tpc_params(priv, bss_cfg, params);

	if (nxpwifi_is_11h_active(priv) &&
	    !cfg80211_chandef_dfs_required(wiphy, &params->chandef,
					   priv->bss_mode)) {
		nxpwifi_dbg(priv->adapter, INFO,
			    "Disable 11h extensions in FW\n");
		ret = nxpwifi_11h_activate(priv, false);
		if (ret) {
			nxpwifi_dbg(priv->adapter, ERROR,
				    "Failed to disable 11h extensions!!");
			goto done;
		}
		priv->state_11h.is_11h_active = false;
	}

	nxpwifi_config_uap_11d(priv, &params->beacon);

	ret = nxpwifi_set_mgmt_ies(priv, &params->beacon);
	if (ret)
		goto done;

	ret = nxpwifi_config_start_uap(priv, bss_cfg);
	if (ret) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "Failed to start AP\n");
		goto done;
	}

	/* First UAP records adapter-level HW channel */
	if (is_first_uap) {
		adapter->chandef = use_chandef;
		adapter->chandef_valid = true;
	}
	atomic_inc(&adapter->uap_count);
	netif_carrier_on(priv->netdev);
	nxpwifi_wake_up_net_dev_queue(priv->netdev, priv->adapter);

	memcpy(&priv->bss_cfg, bss_cfg, sizeof(priv->bss_cfg));

done:
	kfree(bss_cfg);
	return ret;
}

/*
 * cfg80211 op: handle scan request.
 * Issues a firmware scan using the requested parameters and reports the
 * results.
 */
static int
nxpwifi_cfg80211_scan(struct wiphy *wiphy,
		      struct cfg80211_scan_request *request)
{
	struct net_device *dev = request->wdev->netdev;
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(dev);
	int i, offset, ret;
	struct ieee80211_channel *chan;
	struct element *ie;
	struct nxpwifi_user_scan_cfg *user_scan_cfg;
	u8 mac_addr[ETH_ALEN];

	nxpwifi_dbg(priv->adapter, CMD,
		    "info: received scan request on %s\n", dev->name);

	/*
	 * Block scan requests during active scanning or scan cleanup.
	 * Prevents new scans when the interface is disabled or teardown is in
	 * progress.
	 */
	if (priv->scan_request || priv->scan_aborting) {
		nxpwifi_dbg(priv->adapter, WARN,
			    "cmd: Scan already in process..\n");
		return -EBUSY;
	}

	if (!priv->wdev.connected && priv->scan_block)
		priv->scan_block = false;

	if (!nxpwifi_stop_bg_scan(priv))
		cfg80211_sched_scan_stopped_locked(priv->wdev.wiphy, 0);

	user_scan_cfg = kzalloc_obj(*user_scan_cfg, GFP_KERNEL);
	if (!user_scan_cfg)
		return -ENOMEM;

	priv->scan_request = request;

	if (request->flags & NL80211_SCAN_FLAG_RANDOM_ADDR) {
		get_random_mask_addr(mac_addr, request->mac_addr,
				     request->mac_addr_mask);
		ether_addr_copy(request->mac_addr, mac_addr);
		ether_addr_copy(user_scan_cfg->random_mac, mac_addr);
	}

	user_scan_cfg->num_ssids = request->n_ssids;
	user_scan_cfg->ssid_list = request->ssids;

	if (request->ie && request->ie_len) {
		offset = 0;
		for (i = 0; i < NXPWIFI_MAX_VSIE_NUM; i++) {
			if (priv->vs_ie[i].mask != NXPWIFI_VSIE_MASK_CLEAR)
				continue;
			priv->vs_ie[i].mask = NXPWIFI_VSIE_MASK_SCAN;
			ie = (struct element *)(request->ie + offset);
			memcpy(&priv->vs_ie[i].ie, ie,
			       sizeof(*ie) + ie->datalen);
			offset += sizeof(*ie) + ie->datalen;

			if (offset >= request->ie_len)
				break;
		}
	}

	for (i = 0; i < min_t(u32, request->n_channels,
			      NXPWIFI_USER_SCAN_CHAN_MAX); i++) {
		chan = request->channels[i];
		user_scan_cfg->chan_list[i].chan_number = chan->hw_value;
		user_scan_cfg->chan_list[i].radio_type = chan->band;

		if ((chan->flags & IEEE80211_CHAN_NO_IR) || !request->n_ssids)
			user_scan_cfg->chan_list[i].scan_type =
						NXPWIFI_SCAN_TYPE_PASSIVE;
		else
			user_scan_cfg->chan_list[i].scan_type =
						NXPWIFI_SCAN_TYPE_ACTIVE;

		user_scan_cfg->chan_list[i].scan_time = 0;
	}

	if (priv->adapter->scan_chan_gap_enabled &&
	    nxpwifi_is_any_intf_active(priv))
		user_scan_cfg->scan_chan_gap =
					      priv->adapter->scan_chan_gap_time;

	ret = nxpwifi_scan_networks(priv, user_scan_cfg);
	kfree(user_scan_cfg);
	if (ret) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "scan failed: %d\n", ret);
		priv->scan_aborting = false;
		priv->scan_request = NULL;
		return ret;
	}

	if (request->ie && request->ie_len) {
		for (i = 0; i < NXPWIFI_MAX_VSIE_NUM; i++) {
			if (priv->vs_ie[i].mask == NXPWIFI_VSIE_MASK_SCAN) {
				priv->vs_ie[i].mask = NXPWIFI_VSIE_MASK_CLEAR;
				memset(&priv->vs_ie[i].ie, 0,
				       NXPWIFI_MAX_VSIE_LEN);
			}
		}
	}
	return 0;
}

/*
 * cfg80211 sched_scan_start handler.
 *
 * Send a bgscan configuration request to the firmware based on the
 * scheduled scan parameters. On success, the firmware later issues a
 * BGSCAN_REPORT event, after which the driver should query the firmware
 * for scan results.
 */
static int
nxpwifi_cfg80211_sched_scan_start(struct wiphy *wiphy,
				  struct net_device *dev,
				  struct cfg80211_sched_scan_request *request)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(dev);
	int i, offset;
	struct ieee80211_channel *chan;
	struct nxpwifi_bg_scan_cfg *bgscan_cfg;
	struct element *ie;
	int ret;

	if (!request || (!request->n_ssids && !request->n_match_sets)) {
		wiphy_err(wiphy, "%s : Invalid Sched_scan parameters",
			  __func__);
		return -EINVAL;
	}

	wiphy_info(wiphy, "sched_scan start : n_ssids=%d n_match_sets=%d ",
		   request->n_ssids, request->n_match_sets);
	wiphy_info(wiphy, "n_channels=%d interval=%d ie_len=%d\n",
		   request->n_channels, request->scan_plans->interval,
		   (int)request->ie_len);

	bgscan_cfg = kzalloc_obj(*bgscan_cfg, GFP_KERNEL);
	if (!bgscan_cfg)
		return -ENOMEM;

	if (priv->scan_request || priv->scan_aborting)
		bgscan_cfg->start_later = true;

	bgscan_cfg->num_ssids = request->n_match_sets;
	bgscan_cfg->ssid_list = request->match_sets;

	if (request->ie && request->ie_len) {
		offset = 0;
		for (i = 0; i < NXPWIFI_MAX_VSIE_NUM; i++) {
			if (priv->vs_ie[i].mask != NXPWIFI_VSIE_MASK_CLEAR)
				continue;
			priv->vs_ie[i].mask = NXPWIFI_VSIE_MASK_BGSCAN;
			ie = (struct element *)(request->ie + offset);
			memcpy(&priv->vs_ie[i].ie, ie,
			       sizeof(*ie) + ie->datalen);
			offset += sizeof(*ie) + ie->datalen;

			if (offset >= request->ie_len)
				break;
		}
	}

	for (i = 0; i < min_t(u32, request->n_channels,
			      NXPWIFI_BG_SCAN_CHAN_MAX); i++) {
		chan = request->channels[i];
		bgscan_cfg->chan_list[i].chan_number = chan->hw_value;
		bgscan_cfg->chan_list[i].radio_type = chan->band;

		if ((chan->flags & IEEE80211_CHAN_NO_IR) || !request->n_ssids)
			bgscan_cfg->chan_list[i].scan_type =
						NXPWIFI_SCAN_TYPE_PASSIVE;
		else
			bgscan_cfg->chan_list[i].scan_type =
						NXPWIFI_SCAN_TYPE_ACTIVE;

		bgscan_cfg->chan_list[i].scan_time = 0;
	}

	bgscan_cfg->chan_per_scan = min_t(u32, request->n_channels,
					  NXPWIFI_BG_SCAN_CHAN_MAX);

	/* Minimum scan cycle duration: 15 seconds */
	bgscan_cfg->scan_interval = (request->scan_plans->interval >
				     NXPWIFI_BGSCAN_INTERVAL) ?
				request->scan_plans->interval :
				NXPWIFI_BGSCAN_INTERVAL;

	bgscan_cfg->repeat_count = NXPWIFI_BGSCAN_REPEAT_COUNT;
	bgscan_cfg->report_condition = NXPWIFI_BGSCAN_SSID_MATCH |
				NXPWIFI_BGSCAN_WAIT_ALL_CHAN_DONE;
	bgscan_cfg->bss_type = NXPWIFI_BSS_MODE_INFRA;
	bgscan_cfg->action = NXPWIFI_BGSCAN_ACT_SET;
	bgscan_cfg->enable = true;
	if (request->min_rssi_thold != NL80211_SCAN_RSSI_THOLD_OFF) {
		bgscan_cfg->report_condition |= NXPWIFI_BGSCAN_SSID_RSSI_MATCH;
		bgscan_cfg->rssi_threshold = request->min_rssi_thold;
	}

	ret = nxpwifi_bg_scan_config(priv, bgscan_cfg);

	if (!ret)
		priv->sched_scanning = true;

	kfree(bgscan_cfg);
	return ret;
}

/*
 * cfg80211 sched_scan_stop handler.
 *
 * Send a bgscan configuration command to disable the previous
 * background scan settings in the firmware.
 */
static int nxpwifi_cfg80211_sched_scan_stop(struct wiphy *wiphy,
					    struct net_device *dev, u64 reqid)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(dev);

	wiphy_info(wiphy, "sched scan stop!");
	return nxpwifi_stop_bg_scan(priv);
}

/*
 * Set default cfg80211 HT capabilities.
 */
static void
nxpwifi_setup_ht_caps(struct nxpwifi_private *priv,
		      struct ieee80211_sta_ht_cap *ht_info)
{
	int rx_mcs_supp;
	struct ieee80211_mcs_info mcs_set;
	u8 *mcs = (u8 *)&mcs_set;
	struct nxpwifi_adapter *adapter = priv->adapter;

	ht_info->ht_supported = true;
	ht_info->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
	ht_info->ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE;

	memset(&ht_info->mcs, 0, sizeof(ht_info->mcs));

	/* Fill HT capability information */
	if (ISSUPP_CHANWIDTH40(adapter->hw_dot_11n_dev_cap))
		ht_info->cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
	else
		ht_info->cap &= ~IEEE80211_HT_CAP_SUP_WIDTH_20_40;

	if (ISSUPP_SHORTGI20(adapter->hw_dot_11n_dev_cap))
		ht_info->cap |= IEEE80211_HT_CAP_SGI_20;
	else
		ht_info->cap &= ~IEEE80211_HT_CAP_SGI_20;

	if (ISSUPP_SHORTGI40(adapter->hw_dot_11n_dev_cap))
		ht_info->cap |= IEEE80211_HT_CAP_SGI_40;
	else
		ht_info->cap &= ~IEEE80211_HT_CAP_SGI_40;

	if (adapter->user_dev_mcs_support == HT_STREAM_2X2)
		ht_info->cap |= 2 << IEEE80211_HT_CAP_RX_STBC_SHIFT;
	else
		ht_info->cap |= 1 << IEEE80211_HT_CAP_RX_STBC_SHIFT;

	if (ISSUPP_TXSTBC(adapter->hw_dot_11n_dev_cap))
		ht_info->cap |= IEEE80211_HT_CAP_TX_STBC;
	else
		ht_info->cap &= ~IEEE80211_HT_CAP_TX_STBC;

	if (ISSUPP_GREENFIELD(adapter->hw_dot_11n_dev_cap))
		ht_info->cap |= IEEE80211_HT_CAP_GRN_FLD;
	else
		ht_info->cap &= ~IEEE80211_HT_CAP_GRN_FLD;

	if (ISENABLED_40MHZ_INTOLERANT(adapter->hw_dot_11n_dev_cap))
		ht_info->cap |= IEEE80211_HT_CAP_40MHZ_INTOLERANT;
	else
		ht_info->cap &= ~IEEE80211_HT_CAP_40MHZ_INTOLERANT;

	if (ISSUPP_RXLDPC(adapter->hw_dot_11n_dev_cap))
		ht_info->cap |= IEEE80211_HT_CAP_LDPC_CODING;
	else
		ht_info->cap &= ~IEEE80211_HT_CAP_LDPC_CODING;

	ht_info->cap &= ~IEEE80211_HT_CAP_MAX_AMSDU;
	ht_info->cap |= IEEE80211_HT_CAP_SM_PS;

	rx_mcs_supp = GET_RXMCSSUPP(adapter->user_dev_mcs_support);
	/* Set MCS for 1x1/2x2 */
	memset(mcs, 0xff, rx_mcs_supp);
	/* Clear all the other values */
	memset(&mcs[rx_mcs_supp], 0,
	       sizeof(struct ieee80211_mcs_info) - rx_mcs_supp);
	if (priv->bss_mode == NL80211_IFTYPE_STATION ||
	    ISSUPP_CHANWIDTH40(adapter->hw_dot_11n_dev_cap))
		/* Set MCS32 for infra mode or ad-hoc mode with 40MHz support */
		SETHT_MCS32(mcs_set.rx_mask);

	memcpy((u8 *)&ht_info->mcs, mcs, sizeof(struct ieee80211_mcs_info));

	ht_info->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
}

static void
nxpwifi_setup_vht_caps(struct nxpwifi_private *priv,
		       struct ieee80211_sta_vht_cap *vht_info)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	vht_info->vht_supported = true;

	vht_info->cap = adapter->hw_dot_11ac_dev_cap;
	/* Update MCS support for VHT */
	vht_info->vht_mcs.rx_mcs_map =
		cpu_to_le16(adapter->hw_dot_11ac_mcs_support & 0xFFFF);
	vht_info->vht_mcs.rx_highest = 0;
	vht_info->vht_mcs.tx_mcs_map =
		cpu_to_le16(adapter->hw_dot_11ac_mcs_support >> 16);
	vht_info->vht_mcs.tx_highest = 0;
}

/*
 * 5 GHz HE capability masks for UAP mode.
 *
 * MAC: TWT requester/respondor, broadcast TWT, OMI control.
 *
 * PHY: 40/80 MHz width, puncturing, LDPC, NDP 4xLTF, STBC,
 *      Doppler, DCM, SU BF/BFe, STS, sounding dims, extended
 *      range, PPE present, 4xLTF 0.8us GI, Rx 1024-QAM.
 */
#define UAP_HE_MAC_CAP0_MASK (IEEE80211_HE_MAC_CAP0_TWT_REQ | \
			      IEEE80211_HE_MAC_CAP0_TWT_RES)

#define UAP_HE_MAC_CAP1_MASK 0
#define UAP_HE_MAC_CAP2_MASK IEEE80211_HE_MAC_CAP2_BCAST_TWT
#define UAP_HE_MAC_CAP3_MASK IEEE80211_HE_MAC_CAP3_OMI_CONTROL
#define UAP_HE_MAC_CAP4_MASK 0
#define UAP_HE_MAC_CAP5_MASK 0

#define UAP_HE_PHY_CAP0_MASK IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G
#define UAP_HE_PHY_CAP1_MASK (IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD | \
			      IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_80MHZ_ONLY_SECOND_20MHZ | \
			      IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_80MHZ_ONLY_SECOND_40MHZ)
#define UAP_HE_PHY_CAP2_MASK (IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US | \
			      IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ | \
			      IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ | \
			      IEEE80211_HE_PHY_CAP2_DOPPLER_TX | \
			      IEEE80211_HE_PHY_CAP2_DOPPLER_RX)
#define UAP_HE_PHY_CAP3_MASK (IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_BPSK | \
			      IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_1 | \
			      IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_BPSK | \
			      IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1 | \
			      IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER)
#define UAP_HE_PHY_CAP4_MASK (IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE | \
			      IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_8)
#define UAP_HE_PHY_CAP5_MASK IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_2
#define UAP_HE_PHY_CAP6_MASK (IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE | \
			      IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT)
#define UAP_HE_PHY_CAP7_MASK (IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI | \
			      IEEE80211_HE_PHY_CAP7_MAX_NC_1)
#define UAP_HE_PHY_CAP8_MASK 0
#define UAP_HE_PHY_CAP9_MASK IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU
#define UAP_HE_PHY_CAP10_MASK 0

/*
 * 2.4 GHz HE capability masks for UAP mode.
 *
 * MAC: HTC HE, OMI control (no UL OFDMA).
 * PHY: 40 MHz, LDPC, NDP 4xLTF, STBC, Doppler, DCM,
 *      SU BF/BFe, STS/sounding dims, extended range,
 *      PPE present, 4xLTF 0.8us GI, Rx 1024-QAM.
 */
#define UAP_HE_2G_MAC_CAP0_MASK 0x00
#define UAP_HE_2G_MAC_CAP1_MASK 0x00
#define UAP_HE_2G_MAC_CAP2_MASK 0x00
#define UAP_HE_2G_MAC_CAP3_MASK IEEE80211_HE_MAC_CAP3_OMI_CONTROL
#define UAP_HE_2G_MAC_CAP4_MASK 0x00
#define UAP_HE_2G_MAC_CAP5_MASK 0x00

#define UAP_HE_2G_PHY_CAP0_MASK IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G
#define UAP_HE_2G_PHY_CAP1_MASK IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD
#define UAP_HE_2G_PHY_CAP2_MASK (IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US | \
				 IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ | \
				 IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ | \
				 IEEE80211_HE_PHY_CAP2_DOPPLER_TX | \
				 IEEE80211_HE_PHY_CAP2_DOPPLER_RX)
#define UAP_HE_2G_PHY_CAP3_MASK (IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_BPSK | \
				 IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_1 | \
				 IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_BPSK | \
				 IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1 | \
				 IEEE80211_HE_PHY_CAP3_SU_BEAMFORMER)
#define UAP_HE_2G_PHY_CAP4_MASK (IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE | \
				 IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_8)
#define UAP_HE_2G_PHY_CAP5_MASK IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_2
#define UAP_HE_2G_PHY_CAP6_MASK (IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE | \
				 IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT)
#define UAP_HE_2G_PHY_CAP7_MASK (IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI | \
				 IEEE80211_HE_PHY_CAP7_MAX_NC_1)
#define UAP_HE_2G_PHY_CAP8_MASK 0x00
#define UAP_HE_2G_PHY_CAP9_MASK IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU
#define UAP_HE_2G_PHY_CAP10_MASK 0x00
#define HE_CAP_FIX_SIZE 22

static void
nxpwifi_update_11ax_ie(u8 band,
		       struct nxpwifi_11ax_he_cap_cfg *he_cap_cfg)
{
	if (band == BAND_A) {
		he_cap_cfg->cap_elem.mac_cap_info[0] &= UAP_HE_MAC_CAP0_MASK;
		he_cap_cfg->cap_elem.mac_cap_info[1] &= UAP_HE_MAC_CAP1_MASK;
		he_cap_cfg->cap_elem.mac_cap_info[2] &= UAP_HE_MAC_CAP2_MASK;
		he_cap_cfg->cap_elem.mac_cap_info[3] &= UAP_HE_MAC_CAP3_MASK;
		he_cap_cfg->cap_elem.mac_cap_info[4] &= UAP_HE_MAC_CAP4_MASK;
		he_cap_cfg->cap_elem.mac_cap_info[5] &= UAP_HE_MAC_CAP5_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[0] &= UAP_HE_PHY_CAP0_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[1] &= UAP_HE_PHY_CAP1_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[2] &= UAP_HE_PHY_CAP2_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[3] &= UAP_HE_PHY_CAP3_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[4] &= UAP_HE_PHY_CAP4_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[5] &= UAP_HE_PHY_CAP5_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[6] &= UAP_HE_PHY_CAP6_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[7] &= UAP_HE_PHY_CAP7_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[8] &= UAP_HE_PHY_CAP8_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[9] &= UAP_HE_PHY_CAP9_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[10] &= UAP_HE_PHY_CAP10_MASK;
	} else {
		he_cap_cfg->cap_elem.mac_cap_info[0] &= UAP_HE_2G_MAC_CAP0_MASK;
		he_cap_cfg->cap_elem.mac_cap_info[1] &= UAP_HE_2G_MAC_CAP1_MASK;
		he_cap_cfg->cap_elem.mac_cap_info[2] &= UAP_HE_2G_MAC_CAP2_MASK;
		he_cap_cfg->cap_elem.mac_cap_info[3] &= UAP_HE_2G_MAC_CAP3_MASK;
		he_cap_cfg->cap_elem.mac_cap_info[4] &= UAP_HE_2G_MAC_CAP4_MASK;
		he_cap_cfg->cap_elem.mac_cap_info[5] &= UAP_HE_2G_MAC_CAP5_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[0] &= UAP_HE_2G_PHY_CAP0_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[1] &= UAP_HE_2G_PHY_CAP1_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[2] &= UAP_HE_2G_PHY_CAP2_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[3] &= UAP_HE_2G_PHY_CAP3_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[4] &= UAP_HE_2G_PHY_CAP4_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[5] &= UAP_HE_2G_PHY_CAP5_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[6] &= UAP_HE_2G_PHY_CAP6_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[7] &= UAP_HE_2G_PHY_CAP7_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[8] &= UAP_HE_2G_PHY_CAP8_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[9] &= UAP_HE_2G_PHY_CAP9_MASK;
		he_cap_cfg->cap_elem.phy_cap_info[10] &= UAP_HE_2G_PHY_CAP10_MASK;
	}
}

static void
nxpwifi_setup_he_caps(struct nxpwifi_private *priv,
		      struct ieee80211_supported_band *band)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct ieee80211_sband_iftype_data *iftype_data;
	struct nxpwifi_11ax_he_cap_cfg he_cap_cfg;
	u8 hw_he_cap_len;
	u8 extra_mcs_size;
	int ppe_threshold_len;

	if (band->band == NL80211_BAND_5GHZ) {
		hw_he_cap_len = adapter->hw_he_cap_len;
		memcpy(&he_cap_cfg, adapter->hw_he_cap, hw_he_cap_len);
		nxpwifi_update_11ax_ie(BAND_A, &he_cap_cfg);
	} else {
		hw_he_cap_len = adapter->hw_2g_he_cap_len;
		memcpy(&he_cap_cfg, adapter->hw_2g_he_cap, hw_he_cap_len);
		nxpwifi_update_11ax_ie(BAND_G, &he_cap_cfg);
	}

	if (!hw_he_cap_len)
		return;

	iftype_data = kmalloc_obj(*iftype_data, GFP_KERNEL);
	if (!iftype_data)
		return;
	memset(iftype_data, 0, sizeof(*iftype_data));

	iftype_data->types_mask =
		BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_AP);
	iftype_data->he_cap.has_he = true;

	memcpy(iftype_data->he_cap.he_cap_elem.mac_cap_info,
	       he_cap_cfg.cap_elem.mac_cap_info,
	       sizeof(he_cap_cfg.cap_elem.mac_cap_info));
	memcpy(iftype_data->he_cap.he_cap_elem.phy_cap_info,
	       he_cap_cfg.cap_elem.phy_cap_info,
	       sizeof(he_cap_cfg.cap_elem.phy_cap_info));
	memset(&iftype_data->he_cap.he_mcs_nss_supp,
	       0xff,
	       sizeof(iftype_data->he_cap.he_mcs_nss_supp));
	memcpy(&iftype_data->he_cap.he_mcs_nss_supp,
	       he_cap_cfg.he_txrx_mcs_support,
	       sizeof(he_cap_cfg.he_txrx_mcs_support));

	extra_mcs_size = 0;
	/* Add 160 MHz MCS/NSS if supported */
	if (he_cap_cfg.cap_elem.phy_cap_info[0] & BIT(3))
		extra_mcs_size += 4;
	/* Add 80+80 MHz MCS/NSS if supported */
	if (he_cap_cfg.cap_elem.phy_cap_info[0] & BIT(4))
		extra_mcs_size += 4;
	if (extra_mcs_size)
		memcpy((u8 *)&iftype_data->he_cap.he_mcs_nss_supp.rx_mcs_160,
		       he_cap_cfg.val, extra_mcs_size);

	/* Parse PPE thresholds when present */
	ppe_threshold_len = he_cap_cfg.len - HE_CAP_FIX_SIZE - extra_mcs_size;
	if (he_cap_cfg.cap_elem.phy_cap_info[6] & BIT(7) && ppe_threshold_len) {
		memcpy(iftype_data->he_cap.ppe_thres,
		       &he_cap_cfg.val[extra_mcs_size],
		       ppe_threshold_len);
	} else {
		iftype_data->he_cap.he_cap_elem.phy_cap_info[6] &= BIT(7);
	}

	_ieee80211_set_sband_iftype_data(band, iftype_data, 1);
}

/* create a new virtual interface with the given name and name assign type */
struct wireless_dev *nxpwifi_add_virtual_intf(struct wiphy *wiphy,
					      const char *name,
					      unsigned char name_assign_type,
					      enum nl80211_iftype type,
					      struct vif_params *params)
{
	struct nxpwifi_adapter *adapter = nxpwifi_cfg80211_get_adapter(wiphy);
	struct nxpwifi_private *priv;
	struct net_device *dev;
	void *mdev_priv;
	int ret;

	if (!adapter)
		return ERR_PTR(-EFAULT);

	switch (type) {
	case NL80211_IFTYPE_UNSPECIFIED:
	case NL80211_IFTYPE_STATION:
		if (adapter->curr_iface_comb.sta_intf ==
		    adapter->iface_limit.sta_intf) {
			nxpwifi_dbg(adapter, ERROR,
				    "cannot create multiple sta ifaces\n");
			return ERR_PTR(-EINVAL);
		}

		priv = nxpwifi_get_unused_priv_by_bss_type
		       (adapter, NXPWIFI_BSS_TYPE_STA);
		if (!priv) {
			nxpwifi_dbg(adapter, ERROR,
				    "could not get free private struct\n");
			return ERR_PTR(-EFAULT);
		}

		priv->wdev.wiphy = wiphy;
		priv->wdev.iftype = NL80211_IFTYPE_STATION;

		if (type == NL80211_IFTYPE_UNSPECIFIED)
			priv->bss_mode = NL80211_IFTYPE_STATION;
		else
			priv->bss_mode = type;

		priv->bss_type = NXPWIFI_BSS_TYPE_STA;
		priv->frame_type = NXPWIFI_DATA_FRAME_TYPE_ETH_II;
		priv->bss_priority = 0;
		priv->bss_role = NXPWIFI_BSS_ROLE_STA;

		break;
	case NL80211_IFTYPE_AP:
		if (adapter->curr_iface_comb.uap_intf ==
		    adapter->iface_limit.uap_intf) {
			nxpwifi_dbg(adapter, ERROR,
				    "cannot create multiple AP ifaces\n");
			return ERR_PTR(-EINVAL);
		}

		priv = nxpwifi_get_unused_priv_by_bss_type
		       (adapter, NXPWIFI_BSS_TYPE_UAP);
		if (!priv) {
			nxpwifi_dbg(adapter, ERROR,
				    "could not get free private struct\n");
			return ERR_PTR(-EFAULT);
		}

		priv->wdev.wiphy = wiphy;
		priv->wdev.iftype = NL80211_IFTYPE_AP;

		priv->bss_type = NXPWIFI_BSS_TYPE_UAP;
		priv->frame_type = NXPWIFI_DATA_FRAME_TYPE_ETH_II;
		priv->bss_priority = 0;
		priv->bss_role = NXPWIFI_BSS_ROLE_UAP;
		priv->bss_started = 0;
		priv->bss_mode = type;

		break;
	case NL80211_IFTYPE_MONITOR:
		priv = nxpwifi_get_unused_priv_by_bss_type
			(adapter, NXPWIFI_BSS_TYPE_UAP);
		if (!priv) {
			nxpwifi_dbg(adapter, ERROR,
				    "could not get free private struct\n");
			return ERR_PTR(-EFAULT);
		}
		priv->wdev.wiphy = wiphy;
		priv->wdev.iftype = NL80211_IFTYPE_MONITOR;

		priv->bss_type = NXPWIFI_BSS_TYPE_UAP;
		priv->frame_type = NXPWIFI_DATA_FRAME_TYPE_ETH_II;
		priv->bss_priority = 0;
		priv->bss_started = 0;
		priv->bss_mode = type;

		break;
	default:
		nxpwifi_dbg(adapter, ERROR, "type not supported\n");
		return ERR_PTR(-EINVAL);
	}

	dev = alloc_netdev_mqs(sizeof(struct nxpwifi_private *), name,
			       name_assign_type, ether_setup,
			       IEEE80211_NUM_ACS, 1);
	if (!dev) {
		nxpwifi_dbg(adapter, ERROR,
			    "no memory available for netdevice\n");
		ret = -ENOMEM;
		goto err_alloc_netdev;
	}

	nxpwifi_init_priv_params(priv, dev);

	priv->netdev = dev;

	nxpwifi_set_mac_address(priv, dev, false, NULL);

	if (type != NL80211_IFTYPE_MONITOR) {
		ret = nxpwifi_set_bss_mode(priv);

		if (ret) {
			nxpwifi_dbg(adapter, ERROR,
				    "%s: err_set_bss_mode\n", __func__);
			goto err_set_bss_mode;
		}
	}

	ret = nxpwifi_sta_init_cmd(priv, false, false);
	if (ret)
		goto err_sta_init;

	dev_net_set(dev, wiphy_net(wiphy));
	dev->ieee80211_ptr = &priv->wdev;
	dev->ieee80211_ptr->iftype = priv->bss_mode;
	SET_NETDEV_DEV(dev, wiphy_dev(wiphy));

	dev->flags |= IFF_BROADCAST | IFF_MULTICAST;
	dev->watchdog_timeo = NXPWIFI_DEFAULT_WATCHDOG_TIMEOUT;
	dev->needed_headroom = NXPWIFI_MIN_DATA_HEADER_LEN;
	dev->ethtool_ops = &nxpwifi_ethtool_ops;

	mdev_priv = netdev_priv(dev);
	*((unsigned long *)mdev_priv) = (unsigned long)priv;

	if (type == NL80211_IFTYPE_MONITOR)
		dev->type = ARPHRD_IEEE80211_RADIOTAP;

	SET_NETDEV_DEV(dev, adapter->dev);

	wiphy_work_init(&priv->reset_conn_state_work, nxpwifi_reset_conn_state_work);

	wiphy_delayed_work_init(&priv->dfs_cac_work, nxpwifi_dfs_cac_work);

	wiphy_delayed_work_init(&priv->dfs_chan_sw_work, nxpwifi_dfs_chan_sw_work);

	/* Register network device */
	if (cfg80211_register_netdevice(dev)) {
		nxpwifi_dbg(adapter, ERROR, "cannot register network device\n");
		ret = -EFAULT;
		goto err_reg_netdev;
	}

	nxpwifi_dbg(adapter, INFO,
		    "info: %s: NXP 802.11 Adapter\n", dev->name);

#ifdef CONFIG_DEBUG_FS
	nxpwifi_dev_debugfs_init(priv);
#endif

	update_vif_type_counter(adapter, type, 1);

	return &priv->wdev;

err_reg_netdev:
	free_netdev(dev);
	priv->netdev = NULL;
err_sta_init:
err_set_bss_mode:
err_alloc_netdev:
	memset(&priv->wdev, 0, sizeof(priv->wdev));
	priv->wdev.iftype = NL80211_IFTYPE_UNSPECIFIED;
	priv->bss_mode = NL80211_IFTYPE_UNSPECIFIED;
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(nxpwifi_add_virtual_intf);

/* del_virtual_intf: remove the virtual interface determined by dev */
int nxpwifi_del_virtual_intf(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(wdev->netdev);
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct sk_buff *skb, *tmp;

#ifdef CONFIG_DEBUG_FS
	nxpwifi_dev_debugfs_remove(priv);
#endif
	if (priv->bss_mode == NL80211_IFTYPE_MONITOR) {
		struct nxpwifi_802_11_net_monitor netmon_cfg;

		memset(&netmon_cfg, 0, sizeof(struct nxpwifi_802_11_net_monitor));
		nxpwifi_config_monitor_mode(priv, &netmon_cfg);
	}

	if (priv->sched_scanning)
		priv->sched_scanning = false;

	nxpwifi_stop_net_dev_queue(priv->netdev, adapter);

	skb_queue_walk_safe(&priv->bypass_txq, skb, tmp) {
		skb_unlink(skb, &priv->bypass_txq);
		nxpwifi_write_data_complete(priv->adapter, skb, 0, -1);
	}

	netif_carrier_off(priv->netdev);

	if (wdev->netdev->reg_state == NETREG_REGISTERED)
		cfg80211_unregister_netdevice(wdev->netdev);

	/* Clear the priv in adapter */
	priv->netdev = NULL;

	update_vif_type_counter(adapter, priv->bss_mode, -1);

	priv->bss_mode = NL80211_IFTYPE_UNSPECIFIED;

	if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_STA ||
	    GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_UAP)
		kfree(priv->hist_data);

	return 0;
}
EXPORT_SYMBOL_GPL(nxpwifi_del_virtual_intf);

static bool
nxpwifi_is_pattern_supported(struct cfg80211_pkt_pattern *pat, s8 *byte_seq,
			     u8 max_byte_seq)
{
	int j, k, valid_byte_cnt = 0;
	bool dont_care_byte = false;

	for (j = 0; j < DIV_ROUND_UP(pat->pattern_len, 8); j++) {
		for (k = 0; k < 8; k++) {
			if (pat->mask[j] & 1 << k) {
				memcpy(byte_seq + valid_byte_cnt,
				       &pat->pattern[j * 8 + k], 1);
				valid_byte_cnt++;
				if (dont_care_byte)
					return false;
			} else {
				if (valid_byte_cnt)
					dont_care_byte = true;
			}

			/* wildcard bytes record as the offset before the valid byte */
			if (!valid_byte_cnt && !dont_care_byte)
				pat->pkt_offset++;

			if (valid_byte_cnt > max_byte_seq)
				return false;
		}
	}

	byte_seq[max_byte_seq] = valid_byte_cnt;

	return true;
}

#ifdef CONFIG_PM
static void nxpwifi_set_auto_arp_mef_entry(struct nxpwifi_private *priv,
					   struct nxpwifi_mef_entry *mef_entry)
{
	int i, filt_num = 0, num_ipv4 = 0;
	struct in_device *in_dev;
	struct in_ifaddr *ifa;
	__be32 ips[NXPWIFI_MAX_SUPPORTED_IPADDR];
	struct nxpwifi_adapter *adapter = priv->adapter;

	mef_entry->mode = MEF_MODE_HOST_SLEEP;
	mef_entry->action = MEF_ACTION_AUTO_ARP;

	/* Enable ARP offload feature */
	memset(ips, 0, sizeof(ips));
	for (i = 0; i < adapter->priv_num; i++) {
		if (adapter->priv[i]->netdev) {
			in_dev = __in_dev_get_rtnl(adapter->priv[i]->netdev);
			if (!in_dev)
				continue;
			ifa = rtnl_dereference(in_dev->ifa_list);
			if (!ifa || !ifa->ifa_local)
				continue;
			ips[i] = ifa->ifa_local;
			num_ipv4++;
		}
	}

	for (i = 0; i < num_ipv4; i++) {
		if (!ips[i])
			continue;
		mef_entry->filter[filt_num].repeat = 1;
		memcpy(mef_entry->filter[filt_num].byte_seq,
		       (u8 *)&ips[i], sizeof(ips[i]));
		mef_entry->filter[filt_num].byte_seq[NXPWIFI_MEF_MAX_BYTESEQ] =
			sizeof(ips[i]);
		mef_entry->filter[filt_num].offset = 46;
		mef_entry->filter[filt_num].filt_type = TYPE_EQ;
		if (filt_num) {
			mef_entry->filter[filt_num].filt_action =
				TYPE_OR;
		}
		filt_num++;
	}

	mef_entry->filter[filt_num].repeat = 1;
	mef_entry->filter[filt_num].byte_seq[0] = 0x08;
	mef_entry->filter[filt_num].byte_seq[1] = 0x06;
	mef_entry->filter[filt_num].byte_seq[NXPWIFI_MEF_MAX_BYTESEQ] = 2;
	mef_entry->filter[filt_num].offset = 20;
	mef_entry->filter[filt_num].filt_type = TYPE_EQ;
	mef_entry->filter[filt_num].filt_action = TYPE_AND;
}

static int nxpwifi_set_wowlan_mef_entry(struct nxpwifi_private *priv,
					struct nxpwifi_ds_mef_cfg *mef_cfg,
					struct nxpwifi_mef_entry *mef_entry,
					struct cfg80211_wowlan *wowlan)
{
	int i, filt_num = 0, ret = 0;
	bool first_pat = true;
	u8 byte_seq[NXPWIFI_MEF_MAX_BYTESEQ + 1];

	mef_entry->mode = MEF_MODE_HOST_SLEEP;
	mef_entry->action = MEF_ACTION_ALLOW_AND_WAKEUP_HOST;

	for (i = 0; i < wowlan->n_patterns; i++) {
		memset(byte_seq, 0, sizeof(byte_seq));
		if (!nxpwifi_is_pattern_supported
		     (&wowlan->patterns[i], byte_seq,
		      NXPWIFI_MEF_MAX_BYTESEQ)) {
			nxpwifi_dbg(priv->adapter, ERROR,
				    "Pattern not supported\n");
			return -EOPNOTSUPP;
		}

		if (!wowlan->patterns[i].pkt_offset) {
			if (is_unicast_ether_addr(byte_seq) &&
			    byte_seq[NXPWIFI_MEF_MAX_BYTESEQ] == 1) {
				mef_cfg->criteria |= NXPWIFI_CRITERIA_UNICAST;
				continue;
			} else if (is_broadcast_ether_addr(byte_seq)) {
				mef_cfg->criteria |= NXPWIFI_CRITERIA_BROADCAST;
				continue;
			} else if ((!memcmp(byte_seq, "\x33\x33", 2) &&
				    (byte_seq[NXPWIFI_MEF_MAX_BYTESEQ] == 2)) ||
				   (!memcmp(byte_seq, "\x01\x00\x5e", 3) &&
				    (byte_seq[NXPWIFI_MEF_MAX_BYTESEQ] == 3))) {
				mef_cfg->criteria |= NXPWIFI_CRITERIA_MULTICAST;
				continue;
			}
		}
		mef_entry->filter[filt_num].repeat = 1;
		mef_entry->filter[filt_num].offset =
			wowlan->patterns[i].pkt_offset;
		memcpy(mef_entry->filter[filt_num].byte_seq, byte_seq,
		       sizeof(byte_seq));
		mef_entry->filter[filt_num].filt_type = TYPE_EQ;

		if (first_pat) {
			first_pat = false;
			nxpwifi_dbg(priv->adapter, INFO, "Wake on patterns\n");
		} else {
			mef_entry->filter[filt_num].filt_action = TYPE_AND;
		}

		filt_num++;
	}

	if (wowlan->magic_pkt) {
		mef_cfg->criteria |= NXPWIFI_CRITERIA_UNICAST;
		mef_entry->filter[filt_num].repeat = 16;
		memcpy(mef_entry->filter[filt_num].byte_seq, priv->curr_addr,
		       ETH_ALEN);
		mef_entry->filter[filt_num].byte_seq[NXPWIFI_MEF_MAX_BYTESEQ] =
			ETH_ALEN;
		mef_entry->filter[filt_num].offset = 28;
		mef_entry->filter[filt_num].filt_type = TYPE_EQ;
		if (filt_num)
			mef_entry->filter[filt_num].filt_action = TYPE_OR;

		filt_num++;
		mef_entry->filter[filt_num].repeat = 16;
		memcpy(mef_entry->filter[filt_num].byte_seq, priv->curr_addr,
		       ETH_ALEN);
		mef_entry->filter[filt_num].byte_seq[NXPWIFI_MEF_MAX_BYTESEQ] =
			ETH_ALEN;
		mef_entry->filter[filt_num].offset = 56;
		mef_entry->filter[filt_num].filt_type = TYPE_EQ;
		mef_entry->filter[filt_num].filt_action = TYPE_OR;
		nxpwifi_dbg(priv->adapter, INFO, "Wake on magic packet\n");
	}
	return ret;
}

static int nxpwifi_set_mef_filter(struct nxpwifi_private *priv,
				  struct cfg80211_wowlan *wowlan)
{
	int ret = 0, num_entries = 1;
	struct nxpwifi_ds_mef_cfg mef_cfg;
	struct nxpwifi_mef_entry *mef_entry;

	if (wowlan->n_patterns || wowlan->magic_pkt)
		num_entries++;

	mef_entry = kzalloc_objs(*mef_entry, num_entries, GFP_KERNEL);
	if (!mef_entry)
		return -ENOMEM;

	memset(&mef_cfg, 0, sizeof(mef_cfg));
	mef_cfg.criteria |= NXPWIFI_CRITERIA_BROADCAST |
		NXPWIFI_CRITERIA_UNICAST;
	mef_cfg.num_entries = num_entries;
	mef_cfg.mef_entry = mef_entry;

	nxpwifi_set_auto_arp_mef_entry(priv, &mef_entry[0]);

	if (wowlan->n_patterns || wowlan->magic_pkt) {
		ret = nxpwifi_set_wowlan_mef_entry(priv, &mef_cfg,
						   &mef_entry[1], wowlan);
		if (ret)
			goto done;
	}

	if (!mef_cfg.criteria)
		mef_cfg.criteria = NXPWIFI_CRITERIA_BROADCAST |
			NXPWIFI_CRITERIA_UNICAST |
			NXPWIFI_CRITERIA_MULTICAST;

	ret = nxpwifi_mef_cfg(priv, &mef_cfg);

done:
	kfree(mef_entry);
	return ret;
}

static int nxpwifi_cfg80211_suspend(struct wiphy *wiphy,
				    struct cfg80211_wowlan *wowlan)
{
	struct nxpwifi_adapter *adapter = nxpwifi_cfg80211_get_adapter(wiphy);
	struct nxpwifi_ds_hs_cfg hs_cfg;
	int i, ret = 0, retry_num = 10;
	struct nxpwifi_private *priv;
	struct nxpwifi_private *sta_priv =
			nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_STA);

	adapter->wowlan_enabled = false;

	sta_priv->scan_aborting = true;
	for (i = 0; i < adapter->priv_num; i++) {
		priv = adapter->priv[i];
		nxpwifi_abort_cac(priv);
	}

	nxpwifi_cancel_all_pending_cmd(adapter);

	for (i = 0; i < adapter->priv_num; i++) {
		priv = adapter->priv[i];
		if (priv->netdev)
			netif_device_detach(priv->netdev);
	}

	for (i = 0; i < retry_num; i++) {
		if (!nxpwifi_wmm_lists_empty(adapter) ||
		    !nxpwifi_bypass_txlist_empty(adapter) ||
		    !skb_queue_empty(&adapter->tx_data_q))
			usleep_range(10000, 15000);
		else
			break;
	}

	if (!wowlan) {
		nxpwifi_dbg(adapter, INFO,
			    "None of the WOWLAN triggers enabled\n");
		ret = 0;
		goto done;
	}

	if (!sta_priv->media_connected && !wowlan->nd_config) {
		nxpwifi_dbg(adapter, ERROR,
			    "Can not configure WOWLAN in disconnected state\n");
		ret = 0;
		goto done;
	}

	ret = nxpwifi_set_mef_filter(sta_priv, wowlan);
	if (ret) {
		nxpwifi_dbg(adapter, ERROR, "Failed to set MEF filter\n");
		goto done;
	}

	memset(&hs_cfg, 0, sizeof(hs_cfg));
	hs_cfg.conditions = le32_to_cpu(adapter->hs_cfg.conditions);

	if (wowlan->nd_config) {
		nxpwifi_dbg(adapter, INFO, "Wake on net detect\n");
		hs_cfg.conditions |= HS_CFG_COND_MAC_EVENT;
		nxpwifi_cfg80211_sched_scan_start(wiphy, sta_priv->netdev,
						  wowlan->nd_config);
	}

	if (wowlan->disconnect) {
		hs_cfg.conditions |= HS_CFG_COND_MAC_EVENT;
		nxpwifi_dbg(sta_priv->adapter, INFO, "Wake on device disconnect\n");
	}

	hs_cfg.is_invoke_hostcmd = false;
	hs_cfg.gpio = adapter->hs_cfg.gpio;
	hs_cfg.gap = adapter->hs_cfg.gap;
	ret = nxpwifi_set_hs_params(sta_priv, HOST_ACT_GEN_SET,
				    NXPWIFI_SYNC_CMD, &hs_cfg);
	if (ret)
		nxpwifi_dbg(adapter, ERROR, "Failed to set HS params\n");

	adapter->wowlan_enabled = true;

done:
	sta_priv->scan_aborting = false;
	return ret;
}

static int nxpwifi_cfg80211_resume(struct wiphy *wiphy)
{
	struct nxpwifi_adapter *adapter = nxpwifi_cfg80211_get_adapter(wiphy);
	struct nxpwifi_private *priv;
	struct nxpwifi_ds_wakeup_reason wakeup_reason;
	struct cfg80211_wowlan_wakeup wakeup_report;
	int i;
	bool report_wakeup_reason = true;

	for (i = 0; i < adapter->priv_num; i++) {
		priv = adapter->priv[i];
		if (priv->netdev)
			netif_device_attach(priv->netdev);
	}

	if (!wiphy->wowlan_config)
		goto done;

	priv = nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_STA);
	nxpwifi_get_wakeup_reason(priv, HOST_ACT_GEN_GET, NXPWIFI_SYNC_CMD,
				  &wakeup_reason);
	memset(&wakeup_report, 0, sizeof(struct cfg80211_wowlan_wakeup));

	wakeup_report.pattern_idx = -1;

	switch (wakeup_reason.hs_wakeup_reason) {
	case NO_HSWAKEUP_REASON:
		break;
	case BCAST_DATA_MATCHED:
		break;
	case MCAST_DATA_MATCHED:
		break;
	case UCAST_DATA_MATCHED:
		break;
	case MASKTABLE_EVENT_MATCHED:
		break;
	case NON_MASKABLE_EVENT_MATCHED:
		if (wiphy->wowlan_config->disconnect)
			wakeup_report.disconnect = true;
		if (wiphy->wowlan_config->nd_config)
			wakeup_report.net_detect = adapter->nd_info;
		break;
	case NON_MASKABLE_CONDITION_MATCHED:
		break;
	case MAGIC_PATTERN_MATCHED:
		if (wiphy->wowlan_config->magic_pkt)
			wakeup_report.magic_pkt = true;
		if (wiphy->wowlan_config->n_patterns)
			wakeup_report.pattern_idx = 1;
		break;
	case GTK_REKEY_FAILURE:
		if (wiphy->wowlan_config->gtk_rekey_failure)
			wakeup_report.gtk_rekey_failure = true;
		break;
	default:
		report_wakeup_reason = false;
		break;
	}

	if (report_wakeup_reason)
		cfg80211_report_wowlan_wakeup(&priv->wdev, &wakeup_report,
					      GFP_KERNEL);

done:
	if (adapter->nd_info) {
		for (i = 0 ; i < adapter->nd_info->n_matches ; i++)
			kfree(adapter->nd_info->matches[i]);
		kfree(adapter->nd_info);
		adapter->nd_info = NULL;
	}

	return 0;
}

static void nxpwifi_cfg80211_set_wakeup(struct wiphy *wiphy,
					bool enabled)
{
	struct nxpwifi_adapter *adapter = nxpwifi_cfg80211_get_adapter(wiphy);

	device_set_wakeup_enable(adapter->dev, enabled);
}
#endif

static int nxpwifi_get_coalesce_pkt_type(u8 *byte_seq)
{
	if ((byte_seq[0] & 0x01) &&
	    byte_seq[NXPWIFI_COALESCE_MAX_BYTESEQ] == 1)
		return PACKET_TYPE_UNICAST;
	else if (is_broadcast_ether_addr(byte_seq))
		return PACKET_TYPE_BROADCAST;
	else if ((!memcmp(byte_seq, "\x33\x33", 2) &&
		  byte_seq[NXPWIFI_COALESCE_MAX_BYTESEQ] == 2) ||
		 (!memcmp(byte_seq, "\x01\x00\x5e", 3) &&
		  byte_seq[NXPWIFI_COALESCE_MAX_BYTESEQ] == 3))
		return PACKET_TYPE_MULTICAST;

	return 0;
}

static int
nxpwifi_fill_coalesce_rule_info(struct nxpwifi_private *priv,
				struct cfg80211_coalesce_rules *crule,
				struct nxpwifi_coalesce_rule *mrule)
{
	u8 byte_seq[NXPWIFI_COALESCE_MAX_BYTESEQ + 1];
	struct filt_field_param *param;
	int i;

	mrule->max_coalescing_delay = crule->delay;

	param = mrule->params;

	for (i = 0; i < crule->n_patterns; i++) {
		memset(byte_seq, 0, sizeof(byte_seq));
		if (!nxpwifi_is_pattern_supported(&crule->patterns[i],
						  byte_seq,
						NXPWIFI_COALESCE_MAX_BYTESEQ)) {
			nxpwifi_dbg(priv->adapter, ERROR,
				    "Pattern not supported\n");
			return -EOPNOTSUPP;
		}

		if (!crule->patterns[i].pkt_offset) {
			u8 pkt_type;

			pkt_type = nxpwifi_get_coalesce_pkt_type(byte_seq);
			if (pkt_type && mrule->pkt_type) {
				nxpwifi_dbg(priv->adapter, ERROR,
					    "Multiple packet types not allowed\n");
				return -EOPNOTSUPP;
			} else if (pkt_type) {
				mrule->pkt_type = pkt_type;
				continue;
			}
		}

		if (crule->condition == NL80211_COALESCE_CONDITION_MATCH)
			param->operation = RECV_FILTER_MATCH_TYPE_EQ;
		else
			param->operation = RECV_FILTER_MATCH_TYPE_NE;

		param->operand_len = byte_seq[NXPWIFI_COALESCE_MAX_BYTESEQ];
		memcpy(param->operand_byte_stream, byte_seq,
		       param->operand_len);
		param->offset = crule->patterns[i].pkt_offset;
		param++;

		mrule->num_of_fields++;
	}

	if (!mrule->pkt_type) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "Packet type can not be determined\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int nxpwifi_cfg80211_set_coalesce(struct wiphy *wiphy,
					 struct cfg80211_coalesce *coalesce)
{
	struct nxpwifi_adapter *adapter = nxpwifi_cfg80211_get_adapter(wiphy);
	int i, ret;
	struct nxpwifi_ds_coalesce_cfg coalesce_cfg;
	struct nxpwifi_private *priv =
			nxpwifi_get_priv(adapter, NXPWIFI_BSS_ROLE_STA);

	memset(&coalesce_cfg, 0, sizeof(coalesce_cfg));

	if (!coalesce)
		return nxpwifi_coalesce_cfg(priv, &coalesce_cfg);

	coalesce_cfg.num_of_rules = coalesce->n_rules;
	for (i = 0; i < coalesce->n_rules; i++) {
		ret = nxpwifi_fill_coalesce_rule_info(priv, &coalesce->rules[i],
						      &coalesce_cfg.rule[i]);
		if (ret) {
			nxpwifi_dbg(adapter, ERROR,
				    "Recheck the patterns provided for rule %d\n",
				    i + 1);
			return ret;
		}
	}

	return nxpwifi_coalesce_cfg(priv, &coalesce_cfg);
}

static int
nxpwifi_cfg80211_uap_add_station(struct nxpwifi_private *priv, const u8 *mac,
				 struct station_parameters *params)
{
	struct nxpwifi_sta_info add_sta;
	int ret;

	memcpy(add_sta.peer_mac, mac, ETH_ALEN);
	add_sta.params = params;

	ret = nxpwifi_add_new_station(priv, &add_sta);

	return ret;
}

static int
nxpwifi_cfg80211_add_station(struct wiphy *wiphy, struct wireless_dev *wdev,
			     const u8 *mac, struct station_parameters *params)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(wdev->netdev);
	int ret = -EOPNOTSUPP;

	if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_UAP)
		ret = nxpwifi_cfg80211_uap_add_station(priv, mac, params);

	return ret;
}

static int
nxpwifi_cfg80211_channel_switch(struct wiphy *wiphy, struct net_device *dev,
				struct cfg80211_csa_settings *params)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(dev);
	int chsw_msec;
	int ret;

	if (priv->adapter->scan_processing) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "radar detection: scan in process...\n");
		return -EBUSY;
	}

	if (priv->wdev.links[0].cac_started)
		return -EBUSY;

	if (cfg80211_chandef_identical(&params->chandef,
				       &priv->dfs_chandef))
		return -EINVAL;

	if (params->block_tx) {
		netif_carrier_off(priv->netdev);
		nxpwifi_stop_net_dev_queue(priv->netdev, priv->adapter);
		priv->uap_stop_tx = true;
	}

	ret = nxpwifi_del_mgmt_ies(priv);
	if (ret)
		nxpwifi_dbg(priv->adapter, ERROR,
			    "Failed to delete mgmt IEs!\n");

	ret = nxpwifi_set_mgmt_ies(priv, &params->beacon_csa);
	if (ret) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "%s: setting mgmt ies failed\n", __func__);
		goto done;
	}

	memcpy(&priv->dfs_chandef, &params->chandef, sizeof(priv->dfs_chandef));
	memcpy(&priv->ap_update_info.beacon, &params->beacon_after,
	       sizeof(priv->ap_update_info.beacon));

	chsw_msec = max(params->count * priv->bss_cfg.beacon_period, 100);

	nxpwifi_queue_delayed_wiphy_work(priv->adapter,
					 &priv->dfs_chan_sw_work,
					 msecs_to_jiffies(chsw_msec));

done:
	return ret;
}

static int nxpwifi_cfg80211_get_channel(struct wiphy *wiphy,
					struct wireless_dev *wdev,
					unsigned int link_id,
					struct cfg80211_chan_def *chandef)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(wdev->netdev);
	struct nxpwifi_bssdescriptor *curr_bss;
	struct ieee80211_channel *chan;
	enum nl80211_channel_type chan_type;
	enum nl80211_band band;
	int freq;
	int ret = -ENODATA;

	if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_UAP &&
	    cfg80211_chandef_valid(&priv->bss_chandef)) {
		*chandef = priv->bss_chandef;
		ret = 0;
	} else if (priv->media_connected) {
		curr_bss = &priv->curr_bss_params.bss_descriptor;
		band = nxpwifi_band_to_radio_type(priv->curr_bss_params.band);
		freq = ieee80211_channel_to_frequency(curr_bss->channel, band);
		chan = ieee80211_get_channel(wiphy, freq);

		if (priv->ht_param_present) {
			chan_type = nxpwifi_get_chan_type(priv);
			cfg80211_chandef_create(chandef, chan, chan_type);
		} else {
			cfg80211_chandef_create(chandef, chan,
						NL80211_CHAN_NO_HT);
		}
		ret = 0;
	}

	return ret;
}

#ifdef CONFIG_NL80211_TESTMODE

enum nxpwifi_tm_attr {
	__NXPWIFI_TM_ATTR_INVALID	= 0,
	NXPWIFI_TM_ATTR_CMD		= 1,
	NXPWIFI_TM_ATTR_DATA		= 2,

	/* keep last */
	__NXPWIFI_TM_ATTR_AFTER_LAST,
	NXPWIFI_TM_ATTR_MAX		= __NXPWIFI_TM_ATTR_AFTER_LAST - 1,
};

static const struct nla_policy nxpwifi_tm_policy[NXPWIFI_TM_ATTR_MAX + 1] = {
	[NXPWIFI_TM_ATTR_CMD]		= { .type = NLA_U32 },
	[NXPWIFI_TM_ATTR_DATA]		= { .type = NLA_BINARY,
					    .len = NXPWIFI_SIZE_OF_CMD_BUFFER },
};

enum nxpwifi_tm_command {
	NXPWIFI_TM_CMD_HOSTCMD	= 0,
};

static int nxpwifi_tm_cmd(struct wiphy *wiphy, struct wireless_dev *wdev,
			  void *data, int len)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(wdev->netdev);
	struct nxpwifi_ds_misc_cmd *hostcmd;
	struct nlattr *tb[NXPWIFI_TM_ATTR_MAX + 1];
	struct sk_buff *skb;
	int err;

	if (!priv)
		return -EINVAL;

	err = nla_parse_deprecated(tb, NXPWIFI_TM_ATTR_MAX, data, len,
				   nxpwifi_tm_policy, NULL);
	if (err)
		return err;

	if (!tb[NXPWIFI_TM_ATTR_CMD])
		return -EINVAL;

	switch (nla_get_u32(tb[NXPWIFI_TM_ATTR_CMD])) {
	case NXPWIFI_TM_CMD_HOSTCMD:
		if (!tb[NXPWIFI_TM_ATTR_DATA])
			return -EINVAL;

		hostcmd = kzalloc_obj(*hostcmd, GFP_KERNEL);
		if (!hostcmd)
			return -ENOMEM;

		hostcmd->len = nla_len(tb[NXPWIFI_TM_ATTR_DATA]);
		memcpy(hostcmd->cmd, nla_data(tb[NXPWIFI_TM_ATTR_DATA]),
		       hostcmd->len);

		if (nxpwifi_hostcmd(priv, hostcmd)) {
			nxpwifi_dbg(priv->adapter, ERROR, "Failed to process hostcmd\n");
			kfree(hostcmd);
			return -EFAULT;
		}

		/* process hostcmd response*/
		skb = cfg80211_testmode_alloc_reply_skb(wiphy, hostcmd->len);
		if (!skb) {
			kfree(hostcmd);
			return -ENOMEM;
		}
		err = nla_put(skb, NXPWIFI_TM_ATTR_DATA,
			      hostcmd->len, hostcmd->cmd);
		if (err) {
			kfree(hostcmd);
			kfree_skb(skb);
			return -EMSGSIZE;
		}

		err = cfg80211_testmode_reply(skb);
		kfree(hostcmd);
		return err;
	default:
		return -EOPNOTSUPP;
	}
}
#endif

static int
nxpwifi_cfg80211_start_radar_detection(struct wiphy *wiphy,
				       struct net_device *dev,
				       struct cfg80211_chan_def *chandef,
				       u32 cac_time_ms, int link_id)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(dev);
	struct nxpwifi_radar_params radar_params;
	int ret;

	if (priv->adapter->scan_processing) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "radar detection: scan already in process...\n");
		return -EBUSY;
	}

	if (!nxpwifi_is_11h_active(priv)) {
		nxpwifi_dbg(priv->adapter, INFO,
			    "Enable 11h extensions in FW\n");
		if (nxpwifi_11h_activate(priv, true)) {
			nxpwifi_dbg(priv->adapter, ERROR,
				    "Failed to activate 11h extensions!!");
			return -EPERM;
		}
		priv->state_11h.is_11h_active = true;
	}

	memset(&radar_params, 0, sizeof(struct nxpwifi_radar_params));
	radar_params.chandef = chandef;
	radar_params.cac_time_ms = cac_time_ms;

	memcpy(&priv->dfs_chandef, chandef, sizeof(priv->dfs_chandef));

	ret = nxpwifi_chan_report_request(priv, &radar_params);
	if (!ret)
		nxpwifi_queue_delayed_wiphy_work(priv->adapter,
						 &priv->dfs_cac_work,
						 msecs_to_jiffies(cac_time_ms));

	return ret;
}

static int
nxpwifi_cfg80211_change_station(struct wiphy *wiphy, struct wireless_dev *wdev,
				const u8 *mac,
				struct station_parameters *params)
{
	return 0;
}

static int
nxpwifi_cfg80211_authenticate(struct wiphy *wiphy,
			      struct net_device *dev,
			      struct cfg80211_auth_request *req)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(dev);
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct sk_buff *skb;
	u16 pkt_len, auth_alg;
	int ret;
	struct ieee80211_mgmt *mgmt;
	struct nxpwifi_txinfo *tx_info;
	u8 trans = 1, status_code = 0;
	u8 *varptr = NULL;

	if (GET_BSS_ROLE(priv) == NXPWIFI_BSS_ROLE_UAP) {
		nxpwifi_dbg(adapter, ERROR, "Interface role is AP\n");
		return -EINVAL;
	}

	if (priv->wdev.iftype != NL80211_IFTYPE_STATION) {
		nxpwifi_dbg(adapter, ERROR,
			    "Interface type is not correct (type %d)\n",
			    priv->wdev.iftype);
		return -EINVAL;
	}

	if (!nxpwifi_is_channel_setting_allowable(priv, req->bss->channel))
		return -EOPNOTSUPP;

	if (priv->auth_alg != WLAN_AUTH_SAE &&
	    (priv->auth_flag & HOST_MLME_AUTH_PENDING)) {
		nxpwifi_dbg(adapter, ERROR, "Pending auth on going\n");
		return -EBUSY;
	}

	if (!priv->host_mlme_reg) {
		priv->host_mlme_reg = true;
		priv->mgmt_frame_mask |= HOST_MLME_MGMT_MASK;
		nxpwifi_mgmt_frame_reg(priv, priv->mgmt_frame_mask);
	}

	switch (req->auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		auth_alg = WLAN_AUTH_OPEN;
		break;
	case NL80211_AUTHTYPE_SHARED_KEY:
		auth_alg = WLAN_AUTH_SHARED_KEY;
		break;
	case NL80211_AUTHTYPE_FT:
		auth_alg = WLAN_AUTH_FT;
		break;
	case NL80211_AUTHTYPE_NETWORK_EAP:
		auth_alg = WLAN_AUTH_LEAP;
		break;
	case NL80211_AUTHTYPE_SAE:
		auth_alg = WLAN_AUTH_SAE;
		break;
	default:
		nxpwifi_dbg(adapter, ERROR,
			    "unsupported auth type=%d\n", req->auth_type);
		return -EOPNOTSUPP;
	}

	if (!(priv->auth_flag & HOST_MLME_AUTH_PENDING)) {
		ret = nxpwifi_remain_on_chan_cfg(priv, HOST_ACT_GEN_SET,
						 req->bss->channel,
						 AUTH_TX_DEFAULT_WAIT_TIME);

		if (!ret) {
			priv->roc_cfg.cookie =
				nxpwifi_roc_cookie(adapter);
			priv->roc_cfg.chan = *req->bss->channel;
		} else {
			return -EPERM;
		}
	}

	priv->sec_info.authentication_mode = auth_alg;

	nxpwifi_cancel_scan(adapter);

	pkt_len = (u16)req->ie_len + req->auth_data_len +
		NXPWIFI_MGMT_HEADER_LEN + NXPWIFI_AUTH_BODY_LEN;

	if (req->auth_data_len >= 4)
		pkt_len -= 4;

	mgmt = kzalloc(pkt_len, GFP_KERNEL);

	skb = dev_alloc_skb(NXPWIFI_MIN_DATA_HEADER_LEN +
			    NXPWIFI_MGMT_FRAME_HEADER_SIZE +
			    pkt_len + sizeof(pkt_len));
	if (!skb) {
		nxpwifi_dbg(adapter, ERROR,
			    "allocate skb failed for management frame\n");
		return -ENOMEM;
	}

	tx_info = NXPWIFI_SKB_TXCB(skb);
	memset(tx_info, 0, sizeof(*tx_info));
	tx_info->bss_num = priv->bss_num;
	tx_info->bss_type = priv->bss_type;
	tx_info->pkt_len = pkt_len;

	memcpy(mgmt->da, req->bss->bssid, ETH_ALEN);
	memcpy(mgmt->sa, priv->curr_addr, ETH_ALEN);
	memcpy(mgmt->bssid, req->bss->bssid, ETH_ALEN);
	mgmt->frame_control =
		cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_AUTH);

	if (req->auth_data_len >= 4) {
		if (req->auth_type == NL80211_AUTHTYPE_SAE) {
			__le16 *pos = (__le16 *)req->auth_data;

			trans = le16_to_cpu(pos[0]);
			status_code = le16_to_cpu(pos[1]);
		}
		memcpy((u8 *)(&mgmt->u.auth.variable), req->auth_data + 4,
		       req->auth_data_len - 4);
		varptr = (u8 *)&mgmt->u.auth.variable +
			 (req->auth_data_len - 4);
	}

	mgmt->u.auth.auth_alg = cpu_to_le16(auth_alg);
	mgmt->u.auth.auth_transaction = cpu_to_le16(trans);
	mgmt->u.auth.status_code = cpu_to_le16(status_code);

	if (req->ie && req->ie_len) {
		if (!varptr)
			varptr = (u8 *)&mgmt->u.auth.variable;
		memcpy((u8 *)varptr, req->ie, req->ie_len);
	}

	nxpwifi_form_mgmt_frame(skb, (const u8 *)mgmt, pkt_len);
	kfree(mgmt);
	priv->auth_flag = HOST_MLME_AUTH_PENDING;
	priv->auth_alg = auth_alg;
	skb->priority = WMM_HIGHEST_PRIORITY;
	__net_timestamp(skb);

	nxpwifi_dbg(adapter, MSG,
		    "auth: send authentication to %pM\n", req->bss->bssid);

	nxpwifi_queue_tx_pkt(priv, skb);

	return 0;
}

static int
nxpwifi_cfg80211_associate(struct wiphy *wiphy, struct net_device *dev,
			   struct cfg80211_assoc_request *req)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(dev);
	struct nxpwifi_adapter *adapter = priv->adapter;
	int ret;
	struct cfg80211_ssid req_ssid;
	const u8 *ssid_ie;

	if (GET_BSS_ROLE(priv) != NXPWIFI_BSS_ROLE_STA) {
		nxpwifi_dbg(adapter, ERROR,
			    "%s: reject infra assoc request in non-STA role\n",
			    dev->name);
		return -EINVAL;
	}

	if (test_bit(NXPWIFI_SURPRISE_REMOVED, &adapter->work_flags) ||
	    test_bit(NXPWIFI_IS_CMD_TIMEDOUT, &adapter->work_flags)) {
		nxpwifi_dbg(adapter, ERROR,
			    "%s: Ignore association.\t"
			    "Card removed or FW in bad state\n",
			    dev->name);
		return -EPERM;
	}

	if (priv->auth_alg == WLAN_AUTH_SAE)
		priv->auth_flag = HOST_MLME_AUTH_DONE;

	if (priv->auth_flag && !(priv->auth_flag & HOST_MLME_AUTH_DONE))
		return -EBUSY;

	if (priv->roc_cfg.cookie) {
		ret = nxpwifi_remain_on_chan_cfg(priv, HOST_ACT_GEN_REMOVE,
						 &priv->roc_cfg.chan, 0);
		if (!ret)
			memset(&priv->roc_cfg, 0,
			       sizeof(struct nxpwifi_roc_cfg));
		else
			return ret;
	}

	if (!nxpwifi_stop_bg_scan(priv))
		cfg80211_sched_scan_stopped_locked(priv->wdev.wiphy, 0);

	memset(&req_ssid, 0, sizeof(struct cfg80211_ssid));
	rcu_read_lock();
	ssid_ie = ieee80211_bss_get_ie(req->bss, WLAN_EID_SSID);

	if (!ssid_ie)
		goto ssid_err;

	req_ssid.ssid_len = ssid_ie[1];
	if (req_ssid.ssid_len > IEEE80211_MAX_SSID_LEN) {
		nxpwifi_dbg(adapter, ERROR, "invalid SSID - aborting\n");
		goto ssid_err;
	}

	memcpy(req_ssid.ssid, ssid_ie + 2, req_ssid.ssid_len);
	if (!req_ssid.ssid_len || req_ssid.ssid[0] < 0x20) {
		nxpwifi_dbg(adapter, ERROR, "invalid SSID - aborting\n");
		goto ssid_err;
	}
	rcu_read_unlock();

	/*
	 * As this is new association, clear locally stored
	 * keys and security related flags
	 */
	priv->sec_info.wpa_enabled = false;
	priv->sec_info.wpa2_enabled = false;
	priv->wep_key_curr_index = 0;
	priv->sec_info.encryption_mode = 0;
	priv->sec_info.is_authtype_auto = 0;
	ret = nxpwifi_set_encode(priv, NULL, NULL, 0, 0, NULL, 1);

	if (req->crypto.n_ciphers_pairwise)
		priv->sec_info.encryption_mode =
			req->crypto.ciphers_pairwise[0];

	if (req->crypto.cipher_group)
		priv->sec_info.encryption_mode = req->crypto.cipher_group;

	if (req->ie)
		ret = nxpwifi_set_gen_ie(priv, req->ie, req->ie_len);

	memcpy(priv->cfg_bssid, req->bss->bssid, ETH_ALEN);

	nxpwifi_dbg(adapter, MSG,
		    "assoc: send association to %pM\n", req->bss->bssid);

	cfg80211_ref_bss(adapter->wiphy, req->bss);

	ret = nxpwifi_bss_start(priv, req->bss, &req_ssid);

	if (ret) {
		priv->auth_flag = 0;
		priv->auth_alg = WLAN_AUTH_NONE;
		eth_zero_addr(priv->cfg_bssid);
	}

	if (ret >= 0) {
		if (priv->assoc_rsp_size) {
			priv->req_bss = req->bss;
			adapter->assoc_resp_received = true;
			nxpwifi_queue_wiphy_work(adapter,
						 &adapter->host_mlme_work);
		}
		ret = 0;
	}

	cfg80211_put_bss(priv->adapter->wiphy, req->bss);

	return ret;

ssid_err:

	rcu_read_unlock();
	return -EINVAL;
}

static int
nxpwifi_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *dev,
			    u16 reason_code)
{
	struct nxpwifi_private *priv = nxpwifi_netdev_get_priv(dev);
	int ret;

	if (!nxpwifi_stop_bg_scan(priv))
		cfg80211_sched_scan_stopped_locked(priv->wdev.wiphy, 0);

	ret = nxpwifi_deauthenticate(priv, NULL);
	if (!ret) {
		eth_zero_addr(priv->cfg_bssid);
		priv->hs2_enabled = false;
	}

	return ret;
}

static int
nxpwifi_cfg80211_deauthenticate(struct wiphy *wiphy,
				struct net_device *dev,
				struct cfg80211_deauth_request *req)
{
	return nxpwifi_cfg80211_disconnect(wiphy, dev, req->reason_code);
}

static int
nxpwifi_cfg80211_disassociate(struct wiphy *wiphy,
			      struct net_device *dev,
			      struct cfg80211_disassoc_request *req)
{
	return nxpwifi_cfg80211_disconnect(wiphy, dev, req->reason_code);
}

static int
nxpwifi_cfg80211_probe_peer(struct wiphy *wiphy,
			      struct net_device *dev, const u8 *peer,
			      u64 cookie)
{
	/*
	 * hostapd looks for NL80211_CMD_PROBE_CLIENT support; otherwise,
	 * it requires monitor-mode support (which mwifiex doesn't support).
	 * Provide fake probe_peer support to work around this.
	 */
	return -EOPNOTSUPP;
}

/* station cfg80211 operations */
static const struct cfg80211_ops nxpwifi_cfg80211_ops = {
	.add_virtual_intf = nxpwifi_add_virtual_intf,
	.del_virtual_intf = nxpwifi_del_virtual_intf,
	.change_virtual_intf = nxpwifi_cfg80211_change_virtual_intf,
	.scan = nxpwifi_cfg80211_scan,
	.auth = nxpwifi_cfg80211_authenticate,
	.assoc = nxpwifi_cfg80211_associate,
	.deauth = nxpwifi_cfg80211_deauthenticate,
	.disassoc = nxpwifi_cfg80211_disassociate,
	.probe_peer = nxpwifi_cfg80211_probe_peer,
	.get_station = nxpwifi_cfg80211_get_station,
	.dump_station = nxpwifi_cfg80211_dump_station,
	.dump_survey = nxpwifi_cfg80211_dump_survey,
	.set_wiphy_params = nxpwifi_cfg80211_set_wiphy_params,
	.add_key = nxpwifi_cfg80211_add_key,
	.del_key = nxpwifi_cfg80211_del_key,
	.set_default_mgmt_key = nxpwifi_cfg80211_set_default_mgmt_key,
	.mgmt_tx = nxpwifi_cfg80211_mgmt_tx,
	.update_mgmt_frame_registrations =
		nxpwifi_cfg80211_update_mgmt_frame_registrations,
	.remain_on_channel = nxpwifi_cfg80211_remain_on_channel,
	.cancel_remain_on_channel = nxpwifi_cfg80211_cancel_remain_on_channel,
	.set_default_key = nxpwifi_cfg80211_set_default_key,
	.set_power_mgmt = nxpwifi_cfg80211_set_power_mgmt,
	.set_tx_power = nxpwifi_cfg80211_set_tx_power,
	.get_tx_power = nxpwifi_cfg80211_get_tx_power,
	.set_bitrate_mask = nxpwifi_cfg80211_set_bitrate_mask,
	.start_ap = nxpwifi_cfg80211_start_ap,
	.stop_ap = nxpwifi_cfg80211_stop_ap,
	.change_beacon = nxpwifi_cfg80211_change_beacon,
	.set_cqm_rssi_config = nxpwifi_cfg80211_set_cqm_rssi_config,
	.set_antenna = nxpwifi_cfg80211_set_antenna,
	.get_antenna = nxpwifi_cfg80211_get_antenna,
	.del_station = nxpwifi_cfg80211_del_station,
	.sched_scan_start = nxpwifi_cfg80211_sched_scan_start,
	.sched_scan_stop = nxpwifi_cfg80211_sched_scan_stop,
	.change_station = nxpwifi_cfg80211_change_station,
#ifdef CONFIG_PM
	.suspend = nxpwifi_cfg80211_suspend,
	.resume = nxpwifi_cfg80211_resume,
	.set_wakeup = nxpwifi_cfg80211_set_wakeup,
#endif
	.set_coalesce = nxpwifi_cfg80211_set_coalesce,
	.add_station = nxpwifi_cfg80211_add_station,
	CFG80211_TESTMODE_CMD(nxpwifi_tm_cmd)
	.get_channel = nxpwifi_cfg80211_get_channel,
	.start_radar_detection = nxpwifi_cfg80211_start_radar_detection,
	.channel_switch = nxpwifi_cfg80211_channel_switch,
};

#ifdef CONFIG_PM
static const struct wiphy_wowlan_support nxpwifi_wowlan_support = {
	.flags = WIPHY_WOWLAN_MAGIC_PKT | WIPHY_WOWLAN_DISCONNECT |
		WIPHY_WOWLAN_NET_DETECT | WIPHY_WOWLAN_SUPPORTS_GTK_REKEY |
		WIPHY_WOWLAN_GTK_REKEY_FAILURE,
	.n_patterns = NXPWIFI_MEF_MAX_FILTERS,
	.pattern_min_len = 1,
	.pattern_max_len = NXPWIFI_MAX_PATTERN_LEN,
	.max_pkt_offset = NXPWIFI_MAX_OFFSET_LEN,
	.max_nd_match_sets = NXPWIFI_MAX_ND_MATCH_SETS,
};

static const struct wiphy_wowlan_support nxpwifi_wowlan_support_no_gtk = {
	.flags = WIPHY_WOWLAN_MAGIC_PKT | WIPHY_WOWLAN_DISCONNECT |
		 WIPHY_WOWLAN_NET_DETECT,
	.n_patterns = NXPWIFI_MEF_MAX_FILTERS,
	.pattern_min_len = 1,
	.pattern_max_len = NXPWIFI_MAX_PATTERN_LEN,
	.max_pkt_offset = NXPWIFI_MAX_OFFSET_LEN,
	.max_nd_match_sets = NXPWIFI_MAX_ND_MATCH_SETS,
};
#endif

static const struct wiphy_coalesce_support nxpwifi_coalesce_support = {
	.n_rules = NXPWIFI_COALESCE_MAX_RULES,
	.max_delay = NXPWIFI_MAX_COALESCING_DELAY,
	.n_patterns = NXPWIFI_COALESCE_MAX_FILTERS,
	.pattern_min_len = 1,
	.pattern_max_len = NXPWIFI_MAX_PATTERN_LEN,
	.max_pkt_offset = NXPWIFI_MAX_OFFSET_LEN,
};

int nxpwifi_init_channel_scan_gap(struct nxpwifi_adapter *adapter)
{
	u32 n_channels_bg, n_channels_a = 0;

	n_channels_bg = nxpwifi_band_2ghz.n_channels;

	if (adapter->fw_bands & BAND_A)
		n_channels_a = nxpwifi_band_5ghz.n_channels;

	/*
	 * allocate twice the number total channels, since the driver issues an
	 * additional active scan request for hidden SSIDs on passive channels.
	 */
	adapter->num_in_chan_stats = 2 * (n_channels_bg + n_channels_a);
	adapter->chan_stats = vmalloc(array_size(sizeof(*adapter->chan_stats),
						 adapter->num_in_chan_stats));

	if (!adapter->chan_stats)
		return -ENOMEM;

	return 0;
}

/*
 * Register the device with cfg80211.
 *
 * Create and initialize the wiphy, fill in defaults and handlers,
 * then register it with the cfg80211 subsystem.
 */
int nxpwifi_register_cfg80211(struct nxpwifi_adapter *adapter)
{
	int ret;
	void *wdev_priv;
	struct wiphy *wiphy;
	struct nxpwifi_private *priv = adapter->priv[NXPWIFI_BSS_TYPE_STA];
	struct ieee80211_sta_ht_cap *ht_cap;
	struct ieee80211_sta_vht_cap *vht_cap;
	u8 *country_code;
	u32 thr, retry;

	/* create a new wiphy for use with cfg80211 */
	wiphy = wiphy_new(&nxpwifi_cfg80211_ops,
			  sizeof(struct nxpwifi_adapter *));
	if (!wiphy) {
		nxpwifi_dbg(adapter, ERROR,
			    "%s: creating new wiphy\n", __func__);
		return -ENOMEM;
	}

	wiphy->max_scan_ssids = NXPWIFI_MAX_SSID_LIST_LENGTH;
	wiphy->max_scan_ie_len = NXPWIFI_MAX_VSIE_LEN;

	wiphy->mgmt_stypes = nxpwifi_mgmt_stypes;
	wiphy->max_remain_on_channel_duration = 5000;
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				 BIT(NL80211_IFTYPE_AP) |
				 BIT(NL80211_IFTYPE_MONITOR);

	wiphy->max_num_akm_suites = CFG80211_MAX_NUM_AKM_SUITES;

	wiphy->bands[NL80211_BAND_2GHZ] =
		devm_kmemdup(adapter->dev, &nxpwifi_band_2ghz,
			     sizeof(nxpwifi_band_2ghz), GFP_KERNEL);
	if (!wiphy->bands[NL80211_BAND_2GHZ]) {
		ret = -ENOMEM;
		goto err;
	}

	if (adapter->fw_bands & BAND_A) {
		wiphy->bands[NL80211_BAND_5GHZ] =
			devm_kmemdup(adapter->dev, &nxpwifi_band_5ghz,
				     sizeof(nxpwifi_band_5ghz), GFP_KERNEL);
		if (!wiphy->bands[NL80211_BAND_5GHZ]) {
			ret = -ENOMEM;
			goto err;
		}
	} else {
		wiphy->bands[NL80211_BAND_5GHZ] = NULL;
	}

	ht_cap = &wiphy->bands[NL80211_BAND_2GHZ]->ht_cap;
	nxpwifi_setup_ht_caps(priv, ht_cap);

	if (adapter->is_hw_11ac_capable) {
		vht_cap = &wiphy->bands[NL80211_BAND_2GHZ]->vht_cap;
		nxpwifi_setup_vht_caps(priv, vht_cap);
	}

	if (adapter->is_hw_11ax_capable)
		nxpwifi_setup_he_caps(priv, wiphy->bands[NL80211_BAND_2GHZ]);

	if (adapter->fw_bands & BAND_A) {
		ht_cap = &wiphy->bands[NL80211_BAND_5GHZ]->ht_cap;
		nxpwifi_setup_ht_caps(priv, ht_cap);

		if (adapter->is_hw_11ac_capable) {
			vht_cap = &wiphy->bands[NL80211_BAND_5GHZ]->vht_cap;
			nxpwifi_setup_vht_caps(priv, vht_cap);
		}

		if (adapter->is_hw_11ax_capable)
			nxpwifi_setup_he_caps(priv, wiphy->bands[NL80211_BAND_5GHZ]);
	}

	if (adapter->is_hw_11ac_capable)
		wiphy->iface_combinations = &nxpwifi_iface_comb_ap_sta_vht;
	else
		wiphy->iface_combinations = &nxpwifi_iface_comb_ap_sta;
	wiphy->n_iface_combinations = 1;

	wiphy->max_ap_assoc_sta = adapter->max_sta_conn;

	/* Initialize cipher suits */
	wiphy->cipher_suites = nxpwifi_cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(nxpwifi_cipher_suites);

	if (adapter->regd) {
		wiphy->regulatory_flags |= REGULATORY_CUSTOM_REG |
					   REGULATORY_DISABLE_BEACON_HINTS |
					   REGULATORY_COUNTRY_IE_IGNORE;
		wiphy_apply_custom_regulatory(wiphy, adapter->regd);
	}

	ether_addr_copy(wiphy->perm_addr, adapter->perm_addr);
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	wiphy->flags |= WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD |
			WIPHY_FLAG_AP_UAPSD |
			WIPHY_FLAG_REPORTS_OBSS |
			WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL |
			WIPHY_FLAG_HAS_CHANNEL_SWITCH |
			WIPHY_FLAG_NETNS_OK |
			WIPHY_FLAG_PS_ON_BY_DEFAULT;
	wiphy->max_num_csa_counters = NXPWIFI_MAX_CSA_COUNTERS;

#ifdef CONFIG_PM
	if (ISSUPP_FIRMWARE_SUPPLICANT(priv->adapter->fw_cap_info))
		wiphy->wowlan = &nxpwifi_wowlan_support;
	else
		wiphy->wowlan = &nxpwifi_wowlan_support_no_gtk;
#endif

	wiphy->coalesce = &nxpwifi_coalesce_support;

	wiphy->probe_resp_offload = NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS |
				    NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS2;

	wiphy->max_sched_scan_reqs = 1;
	wiphy->max_sched_scan_ssids = NXPWIFI_MAX_SSID_LIST_LENGTH;
	wiphy->max_sched_scan_ie_len = NXPWIFI_MAX_VSIE_LEN;
	wiphy->max_match_sets = NXPWIFI_MAX_SSID_LIST_LENGTH;

	wiphy->available_antennas_tx = BIT(adapter->number_of_antenna) - 1;
	wiphy->available_antennas_rx = BIT(adapter->number_of_antenna) - 1;

	wiphy->features |= NL80211_FEATURE_SAE |
			   NL80211_FEATURE_INACTIVITY_TIMER |
			   NL80211_FEATURE_LOW_PRIORITY_SCAN |
			   NL80211_FEATURE_NEED_OBSS_SCAN;

	if (ISSUPP_RANDOM_MAC(adapter->fw_cap_info))
		wiphy->features |= NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR |
				   NL80211_FEATURE_SCHED_SCAN_RANDOM_MAC_ADDR |
				   NL80211_FEATURE_ND_RANDOM_MAC_ADDR;

	if (adapter->fw_api_ver == NXPWIFI_FW_V15)
		wiphy->features |= NL80211_FEATURE_SK_TX_STATUS;

	/* Reserve space for nxpwifi specific private data for BSS */
	wiphy->bss_priv_size = sizeof(struct nxpwifi_bss_priv);

	wiphy->reg_notifier = nxpwifi_reg_notifier;

	/* Set struct nxpwifi_adapter pointer in wiphy_priv */
	wdev_priv = wiphy_priv(wiphy);
	*(unsigned long *)wdev_priv = (unsigned long)adapter;

	set_wiphy_dev(wiphy, priv->adapter->dev);

	ret = wiphy_register(wiphy);
	if (ret < 0) {
		nxpwifi_dbg(adapter, ERROR,
			    "%s: wiphy_register failed: %d\n", __func__, ret);
		goto err;
	}

	if (!adapter->regd) {
		if (adapter->region_code == 0x00) {
			nxpwifi_dbg(adapter, WARN,
				    "Ignore world regulatory domain\n");
		} else {
			wiphy->regulatory_flags |=
				REGULATORY_DISABLE_BEACON_HINTS |
				REGULATORY_COUNTRY_IE_IGNORE;
			country_code =
				nxpwifi_11d_code_2_region(adapter->region_code);
			if (country_code &&
			    regulatory_hint(wiphy, country_code))
				nxpwifi_dbg(priv->adapter, ERROR,
					    "regulatory_hint() failed\n");
		}
	}

	nxpwifi_get_802_11_snmp_mib(priv, FRAG_THRESH_I, &thr);
	wiphy->frag_threshold = thr;
	nxpwifi_get_802_11_snmp_mib(priv, RTS_THRESH_I, &thr);
	wiphy->rts_threshold = thr;
	nxpwifi_get_802_11_snmp_mib(priv, SHORT_RETRY_LIM_I, &retry);
	wiphy->retry_short = (u8)retry;
	nxpwifi_get_802_11_snmp_mib(priv, LONG_RETRY_LIM_I, &retry);
	wiphy->retry_long = (u8)retry;

	adapter->wiphy = wiphy;
	return ret;

err:
	wiphy_free(wiphy);
	return ret;
}
