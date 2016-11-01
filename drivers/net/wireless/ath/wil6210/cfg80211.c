/*
 * Copyright (c) 2012-2016 Qualcomm Atheros, Inc.
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

#include <linux/etherdevice.h>
#include "wil6210.h"
#include "wmi.h"

#define WIL_MAX_ROC_DURATION_MS 5000

#define CHAN60G(_channel, _flags) {				\
	.band			= NL80211_BAND_60GHZ,		\
	.center_freq		= 56160 + (2160 * (_channel)),	\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 40,				\
}

static struct ieee80211_channel wil_60ghz_channels[] = {
	CHAN60G(1, 0),
	CHAN60G(2, 0),
	CHAN60G(3, 0),
/* channel 4 not supported yet */
};

static struct ieee80211_supported_band wil_band_60ghz = {
	.channels = wil_60ghz_channels,
	.n_channels = ARRAY_SIZE(wil_60ghz_channels),
	.ht_cap = {
		.ht_supported = true,
		.cap = 0, /* TODO */
		.ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K, /* TODO */
		.ampdu_density = IEEE80211_HT_MPDU_DENSITY_8, /* TODO */
		.mcs = {
				/* MCS 1..12 - SC PHY */
			.rx_mask = {0xfe, 0x1f}, /* 1..12 */
			.tx_params = IEEE80211_HT_MCS_TX_DEFINED, /* TODO */
		},
	},
};

static const struct ieee80211_txrx_stypes
wil_mgmt_stypes[NUM_NL80211_IFTYPES] = {
	[NL80211_IFTYPE_STATION] = {
		.tx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_RESP >> 4),
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
	[NL80211_IFTYPE_AP] = {
		.tx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_RESP >> 4),
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
	[NL80211_IFTYPE_P2P_CLIENT] = {
		.tx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_RESP >> 4),
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
	[NL80211_IFTYPE_P2P_GO] = {
		.tx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_RESP >> 4),
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
	[NL80211_IFTYPE_P2P_DEVICE] = {
		.tx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_RESP >> 4),
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4)
	},
};

static const u32 wil_cipher_suites[] = {
	WLAN_CIPHER_SUITE_GCMP,
};

static const char * const key_usage_str[] = {
	[WMI_KEY_USE_PAIRWISE]	= "PTK",
	[WMI_KEY_USE_RX_GROUP]	= "RX_GTK",
	[WMI_KEY_USE_TX_GROUP]	= "TX_GTK",
};

int wil_iftype_nl2wmi(enum nl80211_iftype type)
{
	static const struct {
		enum nl80211_iftype nl;
		enum wmi_network_type wmi;
	} __nl2wmi[] = {
		{NL80211_IFTYPE_ADHOC,		WMI_NETTYPE_ADHOC},
		{NL80211_IFTYPE_STATION,	WMI_NETTYPE_INFRA},
		{NL80211_IFTYPE_AP,		WMI_NETTYPE_AP},
		{NL80211_IFTYPE_P2P_CLIENT,	WMI_NETTYPE_P2P},
		{NL80211_IFTYPE_P2P_GO,		WMI_NETTYPE_P2P},
		{NL80211_IFTYPE_MONITOR,	WMI_NETTYPE_ADHOC}, /* FIXME */
	};
	uint i;

	for (i = 0; i < ARRAY_SIZE(__nl2wmi); i++) {
		if (__nl2wmi[i].nl == type)
			return __nl2wmi[i].wmi;
	}

	return -EOPNOTSUPP;
}

int wil_cid_fill_sinfo(struct wil6210_priv *wil, int cid,
		       struct station_info *sinfo)
{
	struct wmi_notify_req_cmd cmd = {
		.cid = cid,
		.interval_usec = 0,
	};
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_notify_req_done_event evt;
	} __packed reply;
	struct wil_net_stats *stats = &wil->sta[cid].stats;
	int rc;

	rc = wmi_call(wil, WMI_NOTIFY_REQ_CMDID, &cmd, sizeof(cmd),
		      WMI_NOTIFY_REQ_DONE_EVENTID, &reply, sizeof(reply), 20);
	if (rc)
		return rc;

	wil_dbg_wmi(wil, "Link status for CID %d: {\n"
		    "  MCS %d TSF 0x%016llx\n"
		    "  BF status 0x%08x SNR 0x%08x SQI %d%%\n"
		    "  Tx Tpt %d goodput %d Rx goodput %d\n"
		    "  Sectors(rx:tx) my %d:%d peer %d:%d\n""}\n",
		    cid, le16_to_cpu(reply.evt.bf_mcs),
		    le64_to_cpu(reply.evt.tsf), reply.evt.status,
		    le32_to_cpu(reply.evt.snr_val),
		    reply.evt.sqi,
		    le32_to_cpu(reply.evt.tx_tpt),
		    le32_to_cpu(reply.evt.tx_goodput),
		    le32_to_cpu(reply.evt.rx_goodput),
		    le16_to_cpu(reply.evt.my_rx_sector),
		    le16_to_cpu(reply.evt.my_tx_sector),
		    le16_to_cpu(reply.evt.other_rx_sector),
		    le16_to_cpu(reply.evt.other_tx_sector));

	sinfo->generation = wil->sinfo_gen;

	sinfo->filled = BIT(NL80211_STA_INFO_RX_BYTES) |
			BIT(NL80211_STA_INFO_TX_BYTES) |
			BIT(NL80211_STA_INFO_RX_PACKETS) |
			BIT(NL80211_STA_INFO_TX_PACKETS) |
			BIT(NL80211_STA_INFO_RX_BITRATE) |
			BIT(NL80211_STA_INFO_TX_BITRATE) |
			BIT(NL80211_STA_INFO_RX_DROP_MISC) |
			BIT(NL80211_STA_INFO_TX_FAILED);

	sinfo->txrate.flags = RATE_INFO_FLAGS_MCS | RATE_INFO_FLAGS_60G;
	sinfo->txrate.mcs = le16_to_cpu(reply.evt.bf_mcs);
	sinfo->rxrate.flags = RATE_INFO_FLAGS_MCS | RATE_INFO_FLAGS_60G;
	sinfo->rxrate.mcs = stats->last_mcs_rx;
	sinfo->rx_bytes = stats->rx_bytes;
	sinfo->rx_packets = stats->rx_packets;
	sinfo->rx_dropped_misc = stats->rx_dropped;
	sinfo->tx_bytes = stats->tx_bytes;
	sinfo->tx_packets = stats->tx_packets;
	sinfo->tx_failed = stats->tx_errors;

	if (test_bit(wil_status_fwconnected, wil->status)) {
		sinfo->filled |= BIT(NL80211_STA_INFO_SIGNAL);
		sinfo->signal = reply.evt.sqi;
	}

	return rc;
}

static int wil_cfg80211_get_station(struct wiphy *wiphy,
				    struct net_device *ndev,
				    const u8 *mac, struct station_info *sinfo)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	int rc;

	int cid = wil_find_cid(wil, mac);

	wil_dbg_misc(wil, "%s(%pM) CID %d\n", __func__, mac, cid);
	if (cid < 0)
		return cid;

	rc = wil_cid_fill_sinfo(wil, cid, sinfo);

	return rc;
}

/*
 * Find @idx-th active STA for station dump.
 */
static int wil_find_cid_by_idx(struct wil6210_priv *wil, int idx)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(wil->sta); i++) {
		if (wil->sta[i].status == wil_sta_unused)
			continue;
		if (idx == 0)
			return i;
		idx--;
	}

	return -ENOENT;
}

static int wil_cfg80211_dump_station(struct wiphy *wiphy,
				     struct net_device *dev, int idx,
				     u8 *mac, struct station_info *sinfo)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	int rc;
	int cid = wil_find_cid_by_idx(wil, idx);

	if (cid < 0)
		return -ENOENT;

	ether_addr_copy(mac, wil->sta[cid].addr);
	wil_dbg_misc(wil, "%s(%pM) CID %d\n", __func__, mac, cid);

	rc = wil_cid_fill_sinfo(wil, cid, sinfo);

	return rc;
}

static struct wireless_dev *
wil_cfg80211_add_iface(struct wiphy *wiphy, const char *name,
		       unsigned char name_assign_type,
		       enum nl80211_iftype type,
		       u32 *flags, struct vif_params *params)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct net_device *ndev = wil_to_ndev(wil);
	struct wireless_dev *p2p_wdev;

	wil_dbg_misc(wil, "%s()\n", __func__);

	if (type != NL80211_IFTYPE_P2P_DEVICE) {
		wil_err(wil, "%s: unsupported iftype %d\n", __func__, type);
		return ERR_PTR(-EINVAL);
	}

	if (wil->p2p_wdev) {
		wil_err(wil, "%s: P2P_DEVICE interface already created\n",
			__func__);
		return ERR_PTR(-EINVAL);
	}

	p2p_wdev = kzalloc(sizeof(*p2p_wdev), GFP_KERNEL);
	if (!p2p_wdev)
		return ERR_PTR(-ENOMEM);

	p2p_wdev->iftype = type;
	p2p_wdev->wiphy = wiphy;
	/* use our primary ethernet address */
	ether_addr_copy(p2p_wdev->address, ndev->perm_addr);

	wil->p2p_wdev = p2p_wdev;

	return p2p_wdev;
}

static int wil_cfg80211_del_iface(struct wiphy *wiphy,
				  struct wireless_dev *wdev)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);

	wil_dbg_misc(wil, "%s()\n", __func__);

	if (wdev != wil->p2p_wdev) {
		wil_err(wil, "%s: delete of incorrect interface 0x%p\n",
			__func__, wdev);
		return -EINVAL;
	}

	wil_p2p_wdev_free(wil);

	return 0;
}

static int wil_cfg80211_change_iface(struct wiphy *wiphy,
				     struct net_device *ndev,
				     enum nl80211_iftype type, u32 *flags,
				     struct vif_params *params)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wireless_dev *wdev = wil_to_wdev(wil);
	int rc;

	wil_dbg_misc(wil, "%s() type=%d\n", __func__, type);

	if (netif_running(wil_to_ndev(wil)) && !wil_is_recovery_blocked(wil)) {
		wil_dbg_misc(wil, "interface is up. resetting...\n");
		mutex_lock(&wil->mutex);
		__wil_down(wil);
		rc = __wil_up(wil);
		mutex_unlock(&wil->mutex);

		if (rc)
			return rc;
	}

	switch (type) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_P2P_GO:
		break;
	case NL80211_IFTYPE_MONITOR:
		if (flags)
			wil->monitor_flags = *flags;
		else
			wil->monitor_flags = 0;

		break;
	default:
		return -EOPNOTSUPP;
	}

	wdev->iftype = type;

	return 0;
}

static int wil_cfg80211_scan(struct wiphy *wiphy,
			     struct cfg80211_scan_request *request)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wireless_dev *wdev = request->wdev;
	struct {
		struct wmi_start_scan_cmd cmd;
		u16 chnl[4];
	} __packed cmd;
	uint i, n;
	int rc;

	wil_dbg_misc(wil, "%s(), wdev=0x%p iftype=%d\n",
		     __func__, wdev, wdev->iftype);

	if (wil->scan_request) {
		wil_err(wil, "Already scanning\n");
		return -EAGAIN;
	}

	/* check we are client side */
	switch (wdev->iftype) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_P2P_DEVICE:
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* FW don't support scan after connection attempt */
	if (test_bit(wil_status_dontscan, wil->status)) {
		wil_err(wil, "Can't scan now\n");
		return -EBUSY;
	}

	/* social scan on P2P_DEVICE is handled as p2p search */
	if (wdev->iftype == NL80211_IFTYPE_P2P_DEVICE &&
	    wil_p2p_is_social_scan(request)) {
		if (!wil->p2p.p2p_dev_started) {
			wil_err(wil, "P2P search requested on stopped P2P device\n");
			return -EIO;
		}
		wil->scan_request = request;
		wil->radio_wdev = wdev;
		rc = wil_p2p_search(wil, request);
		if (rc) {
			wil->radio_wdev = wil_to_wdev(wil);
			wil->scan_request = NULL;
		}
		return rc;
	}

	(void)wil_p2p_stop_discovery(wil);

	wil_dbg_misc(wil, "Start scan_request 0x%p\n", request);
	wil_dbg_misc(wil, "SSID count: %d", request->n_ssids);

	for (i = 0; i < request->n_ssids; i++) {
		wil_dbg_misc(wil, "SSID[%d]", i);
		print_hex_dump_bytes("SSID ", DUMP_PREFIX_OFFSET,
				     request->ssids[i].ssid,
				     request->ssids[i].ssid_len);
	}

	if (request->n_ssids)
		rc = wmi_set_ssid(wil, request->ssids[0].ssid_len,
				  request->ssids[0].ssid);
	else
		rc = wmi_set_ssid(wil, 0, NULL);

	if (rc) {
		wil_err(wil, "set SSID for scan request failed: %d\n", rc);
		return rc;
	}

	wil->scan_request = request;
	mod_timer(&wil->scan_timer, jiffies + WIL6210_SCAN_TO);

	memset(&cmd, 0, sizeof(cmd));
	cmd.cmd.scan_type = WMI_ACTIVE_SCAN;
	cmd.cmd.num_channels = 0;
	n = min(request->n_channels, 4U);
	for (i = 0; i < n; i++) {
		int ch = request->channels[i]->hw_value;

		if (ch == 0) {
			wil_err(wil,
				"Scan requested for unknown frequency %dMhz\n",
				request->channels[i]->center_freq);
			continue;
		}
		/* 0-based channel indexes */
		cmd.cmd.channel_list[cmd.cmd.num_channels++].channel = ch - 1;
		wil_dbg_misc(wil, "Scan for ch %d  : %d MHz\n", ch,
			     request->channels[i]->center_freq);
	}

	if (request->ie_len)
		print_hex_dump_bytes("Scan IE ", DUMP_PREFIX_OFFSET,
				     request->ie, request->ie_len);
	else
		wil_dbg_misc(wil, "Scan has no IE's\n");

	rc = wmi_set_ie(wil, WMI_FRAME_PROBE_REQ, request->ie_len, request->ie);
	if (rc)
		goto out;

	if (wil->discovery_mode && cmd.cmd.scan_type == WMI_ACTIVE_SCAN) {
		cmd.cmd.discovery_mode = 1;
		wil_dbg_misc(wil, "active scan with discovery_mode=1\n");
	}

	wil->radio_wdev = wdev;
	rc = wmi_send(wil, WMI_START_SCAN_CMDID, &cmd, sizeof(cmd.cmd) +
			cmd.cmd.num_channels * sizeof(cmd.cmd.channel_list[0]));

out:
	if (rc) {
		del_timer_sync(&wil->scan_timer);
		wil->radio_wdev = wil_to_wdev(wil);
		wil->scan_request = NULL;
	}

	return rc;
}

static void wil_print_crypto(struct wil6210_priv *wil,
			     struct cfg80211_crypto_settings *c)
{
	int i, n;

	wil_dbg_misc(wil, "WPA versions: 0x%08x cipher group 0x%08x\n",
		     c->wpa_versions, c->cipher_group);
	wil_dbg_misc(wil, "Pairwise ciphers [%d] {\n", c->n_ciphers_pairwise);
	n = min_t(int, c->n_ciphers_pairwise, ARRAY_SIZE(c->ciphers_pairwise));
	for (i = 0; i < n; i++)
		wil_dbg_misc(wil, "  [%d] = 0x%08x\n", i,
			     c->ciphers_pairwise[i]);
	wil_dbg_misc(wil, "}\n");
	wil_dbg_misc(wil, "AKM suites [%d] {\n", c->n_akm_suites);
	n = min_t(int, c->n_akm_suites, ARRAY_SIZE(c->akm_suites));
	for (i = 0; i < n; i++)
		wil_dbg_misc(wil, "  [%d] = 0x%08x\n", i,
			     c->akm_suites[i]);
	wil_dbg_misc(wil, "}\n");
	wil_dbg_misc(wil, "Control port : %d, eth_type 0x%04x no_encrypt %d\n",
		     c->control_port, be16_to_cpu(c->control_port_ethertype),
		     c->control_port_no_encrypt);
}

static void wil_print_connect_params(struct wil6210_priv *wil,
				     struct cfg80211_connect_params *sme)
{
	wil_info(wil, "Connecting to:\n");
	if (sme->channel) {
		wil_info(wil, "  Channel: %d freq %d\n",
			 sme->channel->hw_value, sme->channel->center_freq);
	}
	if (sme->bssid)
		wil_info(wil, "  BSSID: %pM\n", sme->bssid);
	if (sme->ssid)
		print_hex_dump(KERN_INFO, "  SSID: ", DUMP_PREFIX_OFFSET,
			       16, 1, sme->ssid, sme->ssid_len, true);
	wil_info(wil, "  Privacy: %s\n", sme->privacy ? "secure" : "open");
	wil_info(wil, "  PBSS: %d\n", sme->pbss);
	wil_print_crypto(wil, &sme->crypto);
}

static int wil_cfg80211_connect(struct wiphy *wiphy,
				struct net_device *ndev,
				struct cfg80211_connect_params *sme)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct cfg80211_bss *bss;
	struct wmi_connect_cmd conn;
	const u8 *ssid_eid;
	const u8 *rsn_eid;
	int ch;
	int rc = 0;
	enum ieee80211_bss_type bss_type = IEEE80211_BSS_TYPE_ESS;

	wil_dbg_misc(wil, "%s()\n", __func__);
	wil_print_connect_params(wil, sme);

	if (test_bit(wil_status_fwconnecting, wil->status) ||
	    test_bit(wil_status_fwconnected, wil->status))
		return -EALREADY;

	if (sme->ie_len > WMI_MAX_IE_LEN) {
		wil_err(wil, "IE too large (%td bytes)\n", sme->ie_len);
		return -ERANGE;
	}

	rsn_eid = sme->ie ?
			cfg80211_find_ie(WLAN_EID_RSN, sme->ie, sme->ie_len) :
			NULL;
	if (sme->privacy && !rsn_eid)
		wil_info(wil, "WSC connection\n");

	if (sme->pbss)
		bss_type = IEEE80211_BSS_TYPE_PBSS;

	bss = cfg80211_get_bss(wiphy, sme->channel, sme->bssid,
			       sme->ssid, sme->ssid_len,
			       bss_type, IEEE80211_PRIVACY_ANY);
	if (!bss) {
		wil_err(wil, "Unable to find BSS\n");
		return -ENOENT;
	}

	ssid_eid = ieee80211_bss_get_ie(bss, WLAN_EID_SSID);
	if (!ssid_eid) {
		wil_err(wil, "No SSID\n");
		rc = -ENOENT;
		goto out;
	}
	wil->privacy = sme->privacy;

	if (wil->privacy) {
		/* For secure assoc, remove old keys */
		rc = wmi_del_cipher_key(wil, 0, bss->bssid,
					WMI_KEY_USE_PAIRWISE);
		if (rc) {
			wil_err(wil, "WMI_DELETE_CIPHER_KEY_CMD(PTK) failed\n");
			goto out;
		}
		rc = wmi_del_cipher_key(wil, 0, bss->bssid,
					WMI_KEY_USE_RX_GROUP);
		if (rc) {
			wil_err(wil, "WMI_DELETE_CIPHER_KEY_CMD(GTK) failed\n");
			goto out;
		}
	}

	/* WMI_SET_APPIE_CMD. ie may contain rsn info as well as other info
	 * elements. Send it also in case it's empty, to erase previously set
	 * ies in FW.
	 */
	rc = wmi_set_ie(wil, WMI_FRAME_ASSOC_REQ, sme->ie_len, sme->ie);
	if (rc)
		goto out;

	/* WMI_CONNECT_CMD */
	memset(&conn, 0, sizeof(conn));
	switch (bss->capability & WLAN_CAPABILITY_DMG_TYPE_MASK) {
	case WLAN_CAPABILITY_DMG_TYPE_AP:
		conn.network_type = WMI_NETTYPE_INFRA;
		break;
	case WLAN_CAPABILITY_DMG_TYPE_PBSS:
		conn.network_type = WMI_NETTYPE_P2P;
		break;
	default:
		wil_err(wil, "Unsupported BSS type, capability= 0x%04x\n",
			bss->capability);
		goto out;
	}
	if (wil->privacy) {
		if (rsn_eid) { /* regular secure connection */
			conn.dot11_auth_mode = WMI_AUTH11_SHARED;
			conn.auth_mode = WMI_AUTH_WPA2_PSK;
			conn.pairwise_crypto_type = WMI_CRYPT_AES_GCMP;
			conn.pairwise_crypto_len = 16;
			conn.group_crypto_type = WMI_CRYPT_AES_GCMP;
			conn.group_crypto_len = 16;
		} else { /* WSC */
			conn.dot11_auth_mode = WMI_AUTH11_WSC;
			conn.auth_mode = WMI_AUTH_NONE;
		}
	} else { /* insecure connection */
		conn.dot11_auth_mode = WMI_AUTH11_OPEN;
		conn.auth_mode = WMI_AUTH_NONE;
	}

	conn.ssid_len = min_t(u8, ssid_eid[1], 32);
	memcpy(conn.ssid, ssid_eid+2, conn.ssid_len);

	ch = bss->channel->hw_value;
	if (ch == 0) {
		wil_err(wil, "BSS at unknown frequency %dMhz\n",
			bss->channel->center_freq);
		rc = -EOPNOTSUPP;
		goto out;
	}
	conn.channel = ch - 1;

	ether_addr_copy(conn.bssid, bss->bssid);
	ether_addr_copy(conn.dst_mac, bss->bssid);

	set_bit(wil_status_fwconnecting, wil->status);

	rc = wmi_send(wil, WMI_CONNECT_CMDID, &conn, sizeof(conn));
	if (rc == 0) {
		netif_carrier_on(ndev);
		/* Connect can take lots of time */
		mod_timer(&wil->connect_timer,
			  jiffies + msecs_to_jiffies(2000));
	} else {
		clear_bit(wil_status_fwconnecting, wil->status);
	}

 out:
	cfg80211_put_bss(wiphy, bss);

	return rc;
}

static int wil_cfg80211_disconnect(struct wiphy *wiphy,
				   struct net_device *ndev,
				   u16 reason_code)
{
	int rc;
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);

	wil_dbg_misc(wil, "%s(reason=%d)\n", __func__, reason_code);

	if (!(test_bit(wil_status_fwconnecting, wil->status) ||
	      test_bit(wil_status_fwconnected, wil->status))) {
		wil_err(wil, "%s: Disconnect was called while disconnected\n",
			__func__);
		return 0;
	}

	rc = wmi_call(wil, WMI_DISCONNECT_CMDID, NULL, 0,
		      WMI_DISCONNECT_EVENTID, NULL, 0,
		      WIL6210_DISCONNECT_TO_MS);
	if (rc)
		wil_err(wil, "%s: disconnect error %d\n", __func__, rc);

	return rc;
}

int wil_cfg80211_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
			 struct cfg80211_mgmt_tx_params *params,
			 u64 *cookie)
{
	const u8 *buf = params->buf;
	size_t len = params->len;
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	int rc;
	bool tx_status = false;
	struct ieee80211_mgmt *mgmt_frame = (void *)buf;
	struct wmi_sw_tx_req_cmd *cmd;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_sw_tx_complete_event evt;
	} __packed evt;

	/* Note, currently we do not support the "wait" parameter, user-space
	 * must call remain_on_channel before mgmt_tx or listen on a channel
	 * another way (AP/PCP or connected station)
	 * in addition we need to check if specified "chan" argument is
	 * different from currently "listened" channel and fail if it is.
	 */

	wil_dbg_misc(wil, "%s()\n", __func__);
	print_hex_dump_bytes("mgmt tx frame ", DUMP_PREFIX_OFFSET, buf, len);

	cmd = kmalloc(sizeof(*cmd) + len, GFP_KERNEL);
	if (!cmd) {
		rc = -ENOMEM;
		goto out;
	}

	memcpy(cmd->dst_mac, mgmt_frame->da, WMI_MAC_LEN);
	cmd->len = cpu_to_le16(len);
	memcpy(cmd->payload, buf, len);

	rc = wmi_call(wil, WMI_SW_TX_REQ_CMDID, cmd, sizeof(*cmd) + len,
		      WMI_SW_TX_COMPLETE_EVENTID, &evt, sizeof(evt), 2000);
	if (rc == 0)
		tx_status = !evt.evt.status;

	kfree(cmd);
 out:
	cfg80211_mgmt_tx_status(wdev, cookie ? *cookie : 0, buf, len,
				tx_status, GFP_KERNEL);
	return rc;
}

static int wil_cfg80211_set_channel(struct wiphy *wiphy,
				    struct cfg80211_chan_def *chandef)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wireless_dev *wdev = wil_to_wdev(wil);

	wdev->preset_chandef = *chandef;

	return 0;
}

static enum wmi_key_usage wil_detect_key_usage(struct wil6210_priv *wil,
					       bool pairwise)
{
	struct wireless_dev *wdev = wil_to_wdev(wil);
	enum wmi_key_usage rc;

	if (pairwise) {
		rc = WMI_KEY_USE_PAIRWISE;
	} else {
		switch (wdev->iftype) {
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_P2P_CLIENT:
			rc = WMI_KEY_USE_RX_GROUP;
			break;
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_P2P_GO:
			rc = WMI_KEY_USE_TX_GROUP;
			break;
		default:
			/* TODO: Rx GTK or Tx GTK? */
			wil_err(wil, "Can't determine GTK type\n");
			rc = WMI_KEY_USE_RX_GROUP;
			break;
		}
	}
	wil_dbg_misc(wil, "%s() -> %s\n", __func__, key_usage_str[rc]);

	return rc;
}

static struct wil_tid_crypto_rx_single *
wil_find_crypto_ctx(struct wil6210_priv *wil, u8 key_index,
		    enum wmi_key_usage key_usage, const u8 *mac_addr)
{
	int cid = -EINVAL;
	int tid = 0;
	struct wil_sta_info *s;
	struct wil_tid_crypto_rx *c;

	if (key_usage == WMI_KEY_USE_TX_GROUP)
		return NULL; /* not needed */

	/* supplicant provides Rx group key in STA mode with NULL MAC address */
	if (mac_addr)
		cid = wil_find_cid(wil, mac_addr);
	else if (key_usage == WMI_KEY_USE_RX_GROUP)
		cid = wil_find_cid_by_idx(wil, 0);
	if (cid < 0) {
		wil_err(wil, "No CID for %pM %s[%d]\n", mac_addr,
			key_usage_str[key_usage], key_index);
		return ERR_PTR(cid);
	}

	s = &wil->sta[cid];
	if (key_usage == WMI_KEY_USE_PAIRWISE)
		c = &s->tid_crypto_rx[tid];
	else
		c = &s->group_crypto_rx;

	return &c->key_id[key_index];
}

static int wil_cfg80211_add_key(struct wiphy *wiphy,
				struct net_device *ndev,
				u8 key_index, bool pairwise,
				const u8 *mac_addr,
				struct key_params *params)
{
	int rc;
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	enum wmi_key_usage key_usage = wil_detect_key_usage(wil, pairwise);
	struct wil_tid_crypto_rx_single *cc = wil_find_crypto_ctx(wil,
								  key_index,
								  key_usage,
								  mac_addr);

	wil_dbg_misc(wil, "%s(%pM %s[%d] PN %*phN)\n", __func__,
		     mac_addr, key_usage_str[key_usage], key_index,
		     params->seq_len, params->seq);

	if (IS_ERR(cc)) {
		wil_err(wil, "Not connected, %s(%pM %s[%d] PN %*phN)\n",
			__func__, mac_addr, key_usage_str[key_usage], key_index,
			params->seq_len, params->seq);
		return -EINVAL;
	}

	if (cc)
		cc->key_set = false;

	if (params->seq && params->seq_len != IEEE80211_GCMP_PN_LEN) {
		wil_err(wil,
			"Wrong PN len %d, %s(%pM %s[%d] PN %*phN)\n",
			params->seq_len, __func__, mac_addr,
			key_usage_str[key_usage], key_index,
			params->seq_len, params->seq);
		return -EINVAL;
	}

	rc = wmi_add_cipher_key(wil, key_index, mac_addr, params->key_len,
				params->key, key_usage);
	if ((rc == 0) && cc) {
		if (params->seq)
			memcpy(cc->pn, params->seq, IEEE80211_GCMP_PN_LEN);
		else
			memset(cc->pn, 0, IEEE80211_GCMP_PN_LEN);
		cc->key_set = true;
	}

	return rc;
}

static int wil_cfg80211_del_key(struct wiphy *wiphy,
				struct net_device *ndev,
				u8 key_index, bool pairwise,
				const u8 *mac_addr)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	enum wmi_key_usage key_usage = wil_detect_key_usage(wil, pairwise);
	struct wil_tid_crypto_rx_single *cc = wil_find_crypto_ctx(wil,
								  key_index,
								  key_usage,
								  mac_addr);

	wil_dbg_misc(wil, "%s(%pM %s[%d])\n", __func__, mac_addr,
		     key_usage_str[key_usage], key_index);

	if (IS_ERR(cc))
		wil_info(wil, "Not connected, %s(%pM %s[%d])\n", __func__,
			 mac_addr, key_usage_str[key_usage], key_index);

	if (!IS_ERR_OR_NULL(cc))
		cc->key_set = false;

	return wmi_del_cipher_key(wil, key_index, mac_addr, key_usage);
}

/* Need to be present or wiphy_new() will WARN */
static int wil_cfg80211_set_default_key(struct wiphy *wiphy,
					struct net_device *ndev,
					u8 key_index, bool unicast,
					bool multicast)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);

	wil_dbg_misc(wil, "%s: entered\n", __func__);
	return 0;
}

static int wil_remain_on_channel(struct wiphy *wiphy,
				 struct wireless_dev *wdev,
				 struct ieee80211_channel *chan,
				 unsigned int duration,
				 u64 *cookie)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	int rc;

	wil_dbg_misc(wil, "%s() center_freq=%d, duration=%d iftype=%d\n",
		     __func__, chan->center_freq, duration, wdev->iftype);

	rc = wil_p2p_listen(wil, duration, chan, cookie);
	if (rc)
		return rc;

	wil->radio_wdev = wdev;

	cfg80211_ready_on_channel(wdev, *cookie, chan, duration,
				  GFP_KERNEL);

	return 0;
}

static int wil_cancel_remain_on_channel(struct wiphy *wiphy,
					struct wireless_dev *wdev,
					u64 cookie)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);

	wil_dbg_misc(wil, "%s()\n", __func__);

	return wil_p2p_cancel_listen(wil, cookie);
}

/**
 * find a specific IE in a list of IEs
 * return a pointer to the beginning of IE in the list
 * or NULL if not found
 */
static const u8 *_wil_cfg80211_find_ie(const u8 *ies, u16 ies_len, const u8 *ie,
				       u16 ie_len)
{
	struct ieee80211_vendor_ie *vie;
	u32 oui;

	/* IE tag at offset 0, length at offset 1 */
	if (ie_len < 2 || 2 + ie[1] > ie_len)
		return NULL;

	if (ie[0] != WLAN_EID_VENDOR_SPECIFIC)
		return cfg80211_find_ie(ie[0], ies, ies_len);

	/* make sure there is room for 3 bytes OUI + 1 byte OUI type */
	if (ie[1] < 4)
		return NULL;
	vie = (struct ieee80211_vendor_ie *)ie;
	oui = vie->oui[0] << 16 | vie->oui[1] << 8 | vie->oui[2];
	return cfg80211_find_vendor_ie(oui, vie->oui_type, ies,
				       ies_len);
}

/**
 * merge the IEs in two lists into a single list.
 * do not include IEs from the second list which exist in the first list.
 * add only vendor specific IEs from second list to keep
 * the merged list sorted (since vendor-specific IE has the
 * highest tag number)
 * caller must free the allocated memory for merged IEs
 */
static int _wil_cfg80211_merge_extra_ies(const u8 *ies1, u16 ies1_len,
					 const u8 *ies2, u16 ies2_len,
					 u8 **merged_ies, u16 *merged_len)
{
	u8 *buf, *dpos;
	const u8 *spos;

	if (ies1_len == 0 && ies2_len == 0) {
		*merged_ies = NULL;
		*merged_len = 0;
		return 0;
	}

	buf = kmalloc(ies1_len + ies2_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	memcpy(buf, ies1, ies1_len);
	dpos = buf + ies1_len;
	spos = ies2;
	while (spos + 1 < ies2 + ies2_len) {
		/* IE tag at offset 0, length at offset 1 */
		u16 ielen = 2 + spos[1];

		if (spos + ielen > ies2 + ies2_len)
			break;
		if (spos[0] == WLAN_EID_VENDOR_SPECIFIC &&
		    !_wil_cfg80211_find_ie(ies1, ies1_len, spos, ielen)) {
			memcpy(dpos, spos, ielen);
			dpos += ielen;
		}
		spos += ielen;
	}

	*merged_ies = buf;
	*merged_len = dpos - buf;
	return 0;
}

static void wil_print_bcon_data(struct cfg80211_beacon_data *b)
{
	print_hex_dump_bytes("head     ", DUMP_PREFIX_OFFSET,
			     b->head, b->head_len);
	print_hex_dump_bytes("tail     ", DUMP_PREFIX_OFFSET,
			     b->tail, b->tail_len);
	print_hex_dump_bytes("BCON IE  ", DUMP_PREFIX_OFFSET,
			     b->beacon_ies, b->beacon_ies_len);
	print_hex_dump_bytes("PROBE    ", DUMP_PREFIX_OFFSET,
			     b->probe_resp, b->probe_resp_len);
	print_hex_dump_bytes("PROBE IE ", DUMP_PREFIX_OFFSET,
			     b->proberesp_ies, b->proberesp_ies_len);
	print_hex_dump_bytes("ASSOC IE ", DUMP_PREFIX_OFFSET,
			     b->assocresp_ies, b->assocresp_ies_len);
}

/* internal functions for device reset and starting AP */
static int _wil_cfg80211_set_ies(struct wiphy *wiphy,
				 struct cfg80211_beacon_data *bcon)
{
	int rc;
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	u16 len = 0, proberesp_len = 0;
	u8 *ies = NULL, *proberesp = NULL;

	if (bcon->probe_resp) {
		struct ieee80211_mgmt *f =
			(struct ieee80211_mgmt *)bcon->probe_resp;
		size_t hlen = offsetof(struct ieee80211_mgmt,
				       u.probe_resp.variable);
		proberesp = f->u.probe_resp.variable;
		proberesp_len = bcon->probe_resp_len - hlen;
	}
	rc = _wil_cfg80211_merge_extra_ies(proberesp,
					   proberesp_len,
					   bcon->proberesp_ies,
					   bcon->proberesp_ies_len,
					   &ies, &len);

	if (rc)
		goto out;

	rc = wmi_set_ie(wil, WMI_FRAME_PROBE_RESP, len, ies);
	if (rc)
		goto out;

	if (bcon->assocresp_ies)
		rc = wmi_set_ie(wil, WMI_FRAME_ASSOC_RESP,
				bcon->assocresp_ies_len, bcon->assocresp_ies);
	else
		rc = wmi_set_ie(wil, WMI_FRAME_ASSOC_RESP, len, ies);
#if 0 /* to use beacon IE's, remove this #if 0 */
	if (rc)
		goto out;

	rc = wmi_set_ie(wil, WMI_FRAME_BEACON, bcon->tail_len, bcon->tail);
#endif
out:
	kfree(ies);
	return rc;
}

static int _wil_cfg80211_start_ap(struct wiphy *wiphy,
				  struct net_device *ndev,
				  const u8 *ssid, size_t ssid_len, u32 privacy,
				  int bi, u8 chan,
				  struct cfg80211_beacon_data *bcon,
				  u8 hidden_ssid, u32 pbss)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	int rc;
	struct wireless_dev *wdev = ndev->ieee80211_ptr;
	u8 wmi_nettype = wil_iftype_nl2wmi(wdev->iftype);
	u8 is_go = (wdev->iftype == NL80211_IFTYPE_P2P_GO);

	if (pbss)
		wmi_nettype = WMI_NETTYPE_P2P;

	wil_dbg_misc(wil, "%s: is_go=%d\n", __func__, is_go);
	if (is_go && !pbss) {
		wil_err(wil, "%s: P2P GO must be in PBSS\n", __func__);
		return -ENOTSUPP;
	}

	wil_set_recovery_state(wil, fw_recovery_idle);

	mutex_lock(&wil->mutex);

	__wil_down(wil);
	rc = __wil_up(wil);
	if (rc)
		goto out;

	rc = wmi_set_ssid(wil, ssid_len, ssid);
	if (rc)
		goto out;

	rc = _wil_cfg80211_set_ies(wiphy, bcon);
	if (rc)
		goto out;

	wil->privacy = privacy;
	wil->channel = chan;
	wil->hidden_ssid = hidden_ssid;
	wil->pbss = pbss;

	netif_carrier_on(ndev);

	rc = wmi_pcp_start(wil, bi, wmi_nettype, chan, hidden_ssid, is_go);
	if (rc)
		goto err_pcp_start;

	rc = wil_bcast_init(wil);
	if (rc)
		goto err_bcast;

	goto out; /* success */

err_bcast:
	wmi_pcp_stop(wil);
err_pcp_start:
	netif_carrier_off(ndev);
out:
	mutex_unlock(&wil->mutex);
	return rc;
}

static int wil_cfg80211_change_beacon(struct wiphy *wiphy,
				      struct net_device *ndev,
				      struct cfg80211_beacon_data *bcon)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	int rc;
	u32 privacy = 0;

	wil_dbg_misc(wil, "%s()\n", __func__);
	wil_print_bcon_data(bcon);

	if (bcon->tail &&
	    cfg80211_find_ie(WLAN_EID_RSN, bcon->tail,
			     bcon->tail_len))
		privacy = 1;

	/* in case privacy has changed, need to restart the AP */
	if (wil->privacy != privacy) {
		struct wireless_dev *wdev = ndev->ieee80211_ptr;

		wil_dbg_misc(wil, "privacy changed %d=>%d. Restarting AP\n",
			     wil->privacy, privacy);

		rc = _wil_cfg80211_start_ap(wiphy, ndev, wdev->ssid,
					    wdev->ssid_len, privacy,
					    wdev->beacon_interval,
					    wil->channel, bcon,
					    wil->hidden_ssid,
					    wil->pbss);
	} else {
		rc = _wil_cfg80211_set_ies(wiphy, bcon);
	}

	return rc;
}

static int wil_cfg80211_start_ap(struct wiphy *wiphy,
				 struct net_device *ndev,
				 struct cfg80211_ap_settings *info)
{
	int rc;
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct ieee80211_channel *channel = info->chandef.chan;
	struct cfg80211_beacon_data *bcon = &info->beacon;
	struct cfg80211_crypto_settings *crypto = &info->crypto;
	u8 hidden_ssid;

	wil_dbg_misc(wil, "%s()\n", __func__);

	if (!channel) {
		wil_err(wil, "AP: No channel???\n");
		return -EINVAL;
	}

	switch (info->hidden_ssid) {
	case NL80211_HIDDEN_SSID_NOT_IN_USE:
		hidden_ssid = WMI_HIDDEN_SSID_DISABLED;
		break;

	case NL80211_HIDDEN_SSID_ZERO_LEN:
		hidden_ssid = WMI_HIDDEN_SSID_SEND_EMPTY;
		break;

	case NL80211_HIDDEN_SSID_ZERO_CONTENTS:
		hidden_ssid = WMI_HIDDEN_SSID_CLEAR;
		break;

	default:
		wil_err(wil, "AP: Invalid hidden SSID %d\n", info->hidden_ssid);
		return -EOPNOTSUPP;
	}
	wil_dbg_misc(wil, "AP on Channel %d %d MHz, %s\n", channel->hw_value,
		     channel->center_freq, info->privacy ? "secure" : "open");
	wil_dbg_misc(wil, "Privacy: %d auth_type %d\n",
		     info->privacy, info->auth_type);
	wil_dbg_misc(wil, "Hidden SSID mode: %d\n",
		     info->hidden_ssid);
	wil_dbg_misc(wil, "BI %d DTIM %d\n", info->beacon_interval,
		     info->dtim_period);
	wil_dbg_misc(wil, "PBSS %d\n", info->pbss);
	print_hex_dump_bytes("SSID ", DUMP_PREFIX_OFFSET,
			     info->ssid, info->ssid_len);
	wil_print_bcon_data(bcon);
	wil_print_crypto(wil, crypto);

	rc = _wil_cfg80211_start_ap(wiphy, ndev,
				    info->ssid, info->ssid_len, info->privacy,
				    info->beacon_interval, channel->hw_value,
				    bcon, hidden_ssid, info->pbss);

	return rc;
}

static int wil_cfg80211_stop_ap(struct wiphy *wiphy,
				struct net_device *ndev)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);

	wil_dbg_misc(wil, "%s()\n", __func__);

	netif_carrier_off(ndev);
	wil_set_recovery_state(wil, fw_recovery_idle);

	mutex_lock(&wil->mutex);

	wmi_pcp_stop(wil);

	__wil_down(wil);

	mutex_unlock(&wil->mutex);

	return 0;
}

static int wil_cfg80211_del_station(struct wiphy *wiphy,
				    struct net_device *dev,
				    struct station_del_parameters *params)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);

	wil_dbg_misc(wil, "%s(%pM, reason=%d)\n", __func__, params->mac,
		     params->reason_code);

	mutex_lock(&wil->mutex);
	wil6210_disconnect(wil, params->mac, params->reason_code, false);
	mutex_unlock(&wil->mutex);

	return 0;
}

/* probe_client handling */
static void wil_probe_client_handle(struct wil6210_priv *wil,
				    struct wil_probe_client_req *req)
{
	struct net_device *ndev = wil_to_ndev(wil);
	struct wil_sta_info *sta = &wil->sta[req->cid];
	/* assume STA is alive if it is still connected,
	 * else FW will disconnect it
	 */
	bool alive = (sta->status == wil_sta_connected);

	cfg80211_probe_status(ndev, sta->addr, req->cookie, alive, GFP_KERNEL);
}

static struct list_head *next_probe_client(struct wil6210_priv *wil)
{
	struct list_head *ret = NULL;

	mutex_lock(&wil->probe_client_mutex);

	if (!list_empty(&wil->probe_client_pending)) {
		ret = wil->probe_client_pending.next;
		list_del(ret);
	}

	mutex_unlock(&wil->probe_client_mutex);

	return ret;
}

void wil_probe_client_worker(struct work_struct *work)
{
	struct wil6210_priv *wil = container_of(work, struct wil6210_priv,
						probe_client_worker);
	struct wil_probe_client_req *req;
	struct list_head *lh;

	while ((lh = next_probe_client(wil)) != NULL) {
		req = list_entry(lh, struct wil_probe_client_req, list);

		wil_probe_client_handle(wil, req);
		kfree(req);
	}
}

void wil_probe_client_flush(struct wil6210_priv *wil)
{
	struct wil_probe_client_req *req, *t;

	wil_dbg_misc(wil, "%s()\n", __func__);

	mutex_lock(&wil->probe_client_mutex);

	list_for_each_entry_safe(req, t, &wil->probe_client_pending, list) {
		list_del(&req->list);
		kfree(req);
	}

	mutex_unlock(&wil->probe_client_mutex);
}

static int wil_cfg80211_probe_client(struct wiphy *wiphy,
				     struct net_device *dev,
				     const u8 *peer, u64 *cookie)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wil_probe_client_req *req;
	int cid = wil_find_cid(wil, peer);

	wil_dbg_misc(wil, "%s(%pM => CID %d)\n", __func__, peer, cid);

	if (cid < 0)
		return -ENOLINK;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	req->cid = cid;
	req->cookie = cid;

	mutex_lock(&wil->probe_client_mutex);
	list_add_tail(&req->list, &wil->probe_client_pending);
	mutex_unlock(&wil->probe_client_mutex);

	*cookie = req->cookie;
	queue_work(wil->wq_service, &wil->probe_client_worker);
	return 0;
}

static int wil_cfg80211_change_bss(struct wiphy *wiphy,
				   struct net_device *dev,
				   struct bss_parameters *params)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);

	if (params->ap_isolate >= 0) {
		wil_dbg_misc(wil, "%s(ap_isolate %d => %d)\n", __func__,
			     wil->ap_isolate, params->ap_isolate);
		wil->ap_isolate = params->ap_isolate;
	}

	return 0;
}

static int wil_cfg80211_start_p2p_device(struct wiphy *wiphy,
					 struct wireless_dev *wdev)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);

	wil_dbg_misc(wil, "%s: entered\n", __func__);
	wil->p2p.p2p_dev_started = 1;
	return 0;
}

static void wil_cfg80211_stop_p2p_device(struct wiphy *wiphy,
					 struct wireless_dev *wdev)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	u8 started;

	wil_dbg_misc(wil, "%s: entered\n", __func__);
	mutex_lock(&wil->mutex);
	started = wil_p2p_stop_discovery(wil);
	if (started && wil->scan_request) {
		struct cfg80211_scan_info info = {
			.aborted = true,
		};

		cfg80211_scan_done(wil->scan_request, &info);
		wil->scan_request = NULL;
		wil->radio_wdev = wil->wdev;
	}
	mutex_unlock(&wil->mutex);

	wil->p2p.p2p_dev_started = 0;
}

static struct cfg80211_ops wil_cfg80211_ops = {
	.add_virtual_intf = wil_cfg80211_add_iface,
	.del_virtual_intf = wil_cfg80211_del_iface,
	.scan = wil_cfg80211_scan,
	.connect = wil_cfg80211_connect,
	.disconnect = wil_cfg80211_disconnect,
	.change_virtual_intf = wil_cfg80211_change_iface,
	.get_station = wil_cfg80211_get_station,
	.dump_station = wil_cfg80211_dump_station,
	.remain_on_channel = wil_remain_on_channel,
	.cancel_remain_on_channel = wil_cancel_remain_on_channel,
	.mgmt_tx = wil_cfg80211_mgmt_tx,
	.set_monitor_channel = wil_cfg80211_set_channel,
	.add_key = wil_cfg80211_add_key,
	.del_key = wil_cfg80211_del_key,
	.set_default_key = wil_cfg80211_set_default_key,
	/* AP mode */
	.change_beacon = wil_cfg80211_change_beacon,
	.start_ap = wil_cfg80211_start_ap,
	.stop_ap = wil_cfg80211_stop_ap,
	.del_station = wil_cfg80211_del_station,
	.probe_client = wil_cfg80211_probe_client,
	.change_bss = wil_cfg80211_change_bss,
	/* P2P device */
	.start_p2p_device = wil_cfg80211_start_p2p_device,
	.stop_p2p_device = wil_cfg80211_stop_p2p_device,
};

static void wil_wiphy_init(struct wiphy *wiphy)
{
	wiphy->max_scan_ssids = 1;
	wiphy->max_scan_ie_len = WMI_MAX_IE_LEN;
	wiphy->max_remain_on_channel_duration = WIL_MAX_ROC_DURATION_MS;
	wiphy->max_num_pmkids = 0 /* TODO: */;
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				 BIT(NL80211_IFTYPE_AP) |
				 BIT(NL80211_IFTYPE_P2P_CLIENT) |
				 BIT(NL80211_IFTYPE_P2P_GO) |
				 BIT(NL80211_IFTYPE_P2P_DEVICE) |
				 BIT(NL80211_IFTYPE_MONITOR);
	wiphy->flags |= WIPHY_FLAG_HAVE_AP_SME |
			WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL |
			WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD;
	dev_dbg(wiphy_dev(wiphy), "%s : flags = 0x%08x\n",
		__func__, wiphy->flags);
	wiphy->probe_resp_offload =
		NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS |
		NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS2 |
		NL80211_PROBE_RESP_OFFLOAD_SUPPORT_P2P;

	wiphy->bands[NL80211_BAND_60GHZ] = &wil_band_60ghz;

	/* TODO: figure this out */
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_UNSPEC;

	wiphy->cipher_suites = wil_cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(wil_cipher_suites);
	wiphy->mgmt_stypes = wil_mgmt_stypes;
	wiphy->features |= NL80211_FEATURE_SK_TX_STATUS;
}

struct wireless_dev *wil_cfg80211_init(struct device *dev)
{
	int rc = 0;
	struct wireless_dev *wdev;

	dev_dbg(dev, "%s()\n", __func__);

	wdev = kzalloc(sizeof(*wdev), GFP_KERNEL);
	if (!wdev)
		return ERR_PTR(-ENOMEM);

	wdev->wiphy = wiphy_new(&wil_cfg80211_ops,
				sizeof(struct wil6210_priv));
	if (!wdev->wiphy) {
		rc = -ENOMEM;
		goto out;
	}

	set_wiphy_dev(wdev->wiphy, dev);
	wil_wiphy_init(wdev->wiphy);

	rc = wiphy_register(wdev->wiphy);
	if (rc < 0)
		goto out_failed_reg;

	return wdev;

out_failed_reg:
	wiphy_free(wdev->wiphy);
out:
	kfree(wdev);

	return ERR_PTR(rc);
}

void wil_wdev_free(struct wil6210_priv *wil)
{
	struct wireless_dev *wdev = wil_to_wdev(wil);

	dev_dbg(wil_to_dev(wil), "%s()\n", __func__);

	if (!wdev)
		return;

	wiphy_unregister(wdev->wiphy);
	wiphy_free(wdev->wiphy);
	kfree(wdev);
}

void wil_p2p_wdev_free(struct wil6210_priv *wil)
{
	struct wireless_dev *p2p_wdev;

	mutex_lock(&wil->p2p_wdev_mutex);
	p2p_wdev = wil->p2p_wdev;
	if (p2p_wdev) {
		wil->p2p_wdev = NULL;
		wil->radio_wdev = wil_to_wdev(wil);
		cfg80211_unregister_wdev(p2p_wdev);
		kfree(p2p_wdev);
	}
	mutex_unlock(&wil->p2p_wdev_mutex);
}
