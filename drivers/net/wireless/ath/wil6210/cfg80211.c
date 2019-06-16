/*
 * Copyright (c) 2012-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
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
#include <linux/moduleparam.h>
#include <net/netlink.h>
#include <net/cfg80211.h>
#include "wil6210.h"
#include "wmi.h"
#include "fw.h"

#define WIL_MAX_ROC_DURATION_MS 5000

bool disable_ap_sme;
module_param(disable_ap_sme, bool, 0444);
MODULE_PARM_DESC(disable_ap_sme, " let user space handle AP mode SME");

#ifdef CONFIG_PM
static struct wiphy_wowlan_support wil_wowlan_support = {
	.flags = WIPHY_WOWLAN_ANY | WIPHY_WOWLAN_DISCONNECT,
};
#endif

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
	CHAN60G(4, 0),
};

static void
wil_memdup_ie(u8 **pdst, size_t *pdst_len, const u8 *src, size_t src_len)
{
	kfree(*pdst);
	*pdst = NULL;
	*pdst_len = 0;
	if (src_len > 0) {
		*pdst = kmemdup(src, src_len, GFP_KERNEL);
		if (*pdst)
			*pdst_len = src_len;
	}
}

static int wil_num_supported_channels(struct wil6210_priv *wil)
{
	int num_channels = ARRAY_SIZE(wil_60ghz_channels);

	if (!test_bit(WMI_FW_CAPABILITY_CHANNEL_4, wil->fw_capabilities))
		num_channels--;

	return num_channels;
}

void update_supported_bands(struct wil6210_priv *wil)
{
	struct wiphy *wiphy = wil_to_wiphy(wil);

	wil_dbg_misc(wil, "update supported bands");

	wiphy->bands[NL80211_BAND_60GHZ]->n_channels =
						wil_num_supported_channels(wil);
}

/* Vendor id to be used in vendor specific command and events
 * to user space.
 * NOTE: The authoritative place for definition of QCA_NL80211_VENDOR_ID,
 * vendor subcmd definitions prefixed with QCA_NL80211_VENDOR_SUBCMD, and
 * qca_wlan_vendor_attr is open source file src/common/qca-vendor.h in
 * git://w1.fi/srv/git/hostap.git; the values here are just a copy of that
 */

#define QCA_NL80211_VENDOR_ID	0x001374

#define WIL_MAX_RF_SECTORS (128)
#define WIL_CID_ALL (0xff)

enum qca_wlan_vendor_attr_rf_sector {
	QCA_ATTR_MAC_ADDR = 6,
	QCA_ATTR_PAD = 13,
	QCA_ATTR_TSF = 29,
	QCA_ATTR_DMG_RF_SECTOR_INDEX = 30,
	QCA_ATTR_DMG_RF_SECTOR_TYPE = 31,
	QCA_ATTR_DMG_RF_MODULE_MASK = 32,
	QCA_ATTR_DMG_RF_SECTOR_CFG = 33,
	QCA_ATTR_DMG_RF_SECTOR_MAX,
};

enum qca_wlan_vendor_attr_dmg_rf_sector_type {
	QCA_ATTR_DMG_RF_SECTOR_TYPE_RX,
	QCA_ATTR_DMG_RF_SECTOR_TYPE_TX,
	QCA_ATTR_DMG_RF_SECTOR_TYPE_MAX
};

enum qca_wlan_vendor_attr_dmg_rf_sector_cfg {
	QCA_ATTR_DMG_RF_SECTOR_CFG_INVALID = 0,
	QCA_ATTR_DMG_RF_SECTOR_CFG_MODULE_INDEX,
	QCA_ATTR_DMG_RF_SECTOR_CFG_ETYPE0,
	QCA_ATTR_DMG_RF_SECTOR_CFG_ETYPE1,
	QCA_ATTR_DMG_RF_SECTOR_CFG_ETYPE2,
	QCA_ATTR_DMG_RF_SECTOR_CFG_PSH_HI,
	QCA_ATTR_DMG_RF_SECTOR_CFG_PSH_LO,
	QCA_ATTR_DMG_RF_SECTOR_CFG_DTYPE_X16,

	/* keep last */
	QCA_ATTR_DMG_RF_SECTOR_CFG_AFTER_LAST,
	QCA_ATTR_DMG_RF_SECTOR_CFG_MAX =
	QCA_ATTR_DMG_RF_SECTOR_CFG_AFTER_LAST - 1
};

static const struct
nla_policy wil_rf_sector_policy[QCA_ATTR_DMG_RF_SECTOR_MAX + 1] = {
	[QCA_ATTR_MAC_ADDR] = { .len = ETH_ALEN },
	[QCA_ATTR_DMG_RF_SECTOR_INDEX] = { .type = NLA_U16 },
	[QCA_ATTR_DMG_RF_SECTOR_TYPE] = { .type = NLA_U8 },
	[QCA_ATTR_DMG_RF_MODULE_MASK] = { .type = NLA_U32 },
	[QCA_ATTR_DMG_RF_SECTOR_CFG] = { .type = NLA_NESTED },
};

static const struct
nla_policy wil_rf_sector_cfg_policy[QCA_ATTR_DMG_RF_SECTOR_CFG_MAX + 1] = {
	[QCA_ATTR_DMG_RF_SECTOR_CFG_MODULE_INDEX] = { .type = NLA_U8 },
	[QCA_ATTR_DMG_RF_SECTOR_CFG_ETYPE0] = { .type = NLA_U32 },
	[QCA_ATTR_DMG_RF_SECTOR_CFG_ETYPE1] = { .type = NLA_U32 },
	[QCA_ATTR_DMG_RF_SECTOR_CFG_ETYPE2] = { .type = NLA_U32 },
	[QCA_ATTR_DMG_RF_SECTOR_CFG_PSH_HI] = { .type = NLA_U32 },
	[QCA_ATTR_DMG_RF_SECTOR_CFG_PSH_LO] = { .type = NLA_U32 },
	[QCA_ATTR_DMG_RF_SECTOR_CFG_DTYPE_X16] = { .type = NLA_U32 },
};

enum qca_nl80211_vendor_subcmds {
	QCA_NL80211_VENDOR_SUBCMD_DMG_RF_GET_SECTOR_CFG = 139,
	QCA_NL80211_VENDOR_SUBCMD_DMG_RF_SET_SECTOR_CFG = 140,
	QCA_NL80211_VENDOR_SUBCMD_DMG_RF_GET_SELECTED_SECTOR = 141,
	QCA_NL80211_VENDOR_SUBCMD_DMG_RF_SET_SELECTED_SECTOR = 142,
};

static int wil_rf_sector_get_cfg(struct wiphy *wiphy,
				 struct wireless_dev *wdev,
				 const void *data, int data_len);
static int wil_rf_sector_set_cfg(struct wiphy *wiphy,
				 struct wireless_dev *wdev,
				 const void *data, int data_len);
static int wil_rf_sector_get_selected(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data, int data_len);
static int wil_rf_sector_set_selected(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data, int data_len);

/* vendor specific commands */
static const struct wiphy_vendor_command wil_nl80211_vendor_commands[] = {
	{
		.info.vendor_id = QCA_NL80211_VENDOR_ID,
		.info.subcmd = QCA_NL80211_VENDOR_SUBCMD_DMG_RF_GET_SECTOR_CFG,
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV |
			 WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = wil_rf_sector_get_cfg
	},
	{
		.info.vendor_id = QCA_NL80211_VENDOR_ID,
		.info.subcmd = QCA_NL80211_VENDOR_SUBCMD_DMG_RF_SET_SECTOR_CFG,
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV |
			 WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = wil_rf_sector_set_cfg
	},
	{
		.info.vendor_id = QCA_NL80211_VENDOR_ID,
		.info.subcmd =
			QCA_NL80211_VENDOR_SUBCMD_DMG_RF_GET_SELECTED_SECTOR,
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV |
			 WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = wil_rf_sector_get_selected
	},
	{
		.info.vendor_id = QCA_NL80211_VENDOR_ID,
		.info.subcmd =
			QCA_NL80211_VENDOR_SUBCMD_DMG_RF_SET_SELECTED_SECTOR,
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV |
			 WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = wil_rf_sector_set_selected
	},
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
		BIT(IEEE80211_STYPE_PROBE_RESP >> 4) |
		BIT(IEEE80211_STYPE_ASSOC_RESP >> 4) |
		BIT(IEEE80211_STYPE_DISASSOC >> 4) |
		BIT(IEEE80211_STYPE_AUTH >> 4) |
		BIT(IEEE80211_STYPE_REASSOC_RESP >> 4),
		.rx = BIT(IEEE80211_STYPE_ACTION >> 4) |
		BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
		BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) |
		BIT(IEEE80211_STYPE_DISASSOC >> 4) |
		BIT(IEEE80211_STYPE_AUTH >> 4) |
		BIT(IEEE80211_STYPE_DEAUTH >> 4) |
		BIT(IEEE80211_STYPE_REASSOC_REQ >> 4)
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

int wil_cid_fill_sinfo(struct wil6210_vif *vif, int cid,
		       struct station_info *sinfo)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
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

	memset(&reply, 0, sizeof(reply));

	rc = wmi_call(wil, WMI_NOTIFY_REQ_CMDID, vif->mid, &cmd, sizeof(cmd),
		      WMI_NOTIFY_REQ_DONE_EVENTID, &reply, sizeof(reply), 20);
	if (rc)
		return rc;

	wil_dbg_wmi(wil, "Link status for CID %d MID %d: {\n"
		    "  MCS %d TSF 0x%016llx\n"
		    "  BF status 0x%08x RSSI %d SQI %d%%\n"
		    "  Tx Tpt %d goodput %d Rx goodput %d\n"
		    "  Sectors(rx:tx) my %d:%d peer %d:%d\n""}\n",
		    cid, vif->mid, le16_to_cpu(reply.evt.bf_mcs),
		    le64_to_cpu(reply.evt.tsf), reply.evt.status,
		    reply.evt.rssi,
		    reply.evt.sqi,
		    le32_to_cpu(reply.evt.tx_tpt),
		    le32_to_cpu(reply.evt.tx_goodput),
		    le32_to_cpu(reply.evt.rx_goodput),
		    le16_to_cpu(reply.evt.my_rx_sector),
		    le16_to_cpu(reply.evt.my_tx_sector),
		    le16_to_cpu(reply.evt.other_rx_sector),
		    le16_to_cpu(reply.evt.other_tx_sector));

	sinfo->generation = wil->sinfo_gen;

	sinfo->filled = BIT_ULL(NL80211_STA_INFO_RX_BYTES) |
			BIT_ULL(NL80211_STA_INFO_TX_BYTES) |
			BIT_ULL(NL80211_STA_INFO_RX_PACKETS) |
			BIT_ULL(NL80211_STA_INFO_TX_PACKETS) |
			BIT_ULL(NL80211_STA_INFO_RX_BITRATE) |
			BIT_ULL(NL80211_STA_INFO_TX_BITRATE) |
			BIT_ULL(NL80211_STA_INFO_RX_DROP_MISC) |
			BIT_ULL(NL80211_STA_INFO_TX_FAILED);

	sinfo->txrate.flags = RATE_INFO_FLAGS_60G;
	sinfo->txrate.mcs = le16_to_cpu(reply.evt.bf_mcs);
	sinfo->rxrate.mcs = stats->last_mcs_rx;
	sinfo->rx_bytes = stats->rx_bytes;
	sinfo->rx_packets = stats->rx_packets;
	sinfo->rx_dropped_misc = stats->rx_dropped;
	sinfo->tx_bytes = stats->tx_bytes;
	sinfo->tx_packets = stats->tx_packets;
	sinfo->tx_failed = stats->tx_errors;

	if (test_bit(wil_vif_fwconnected, vif->status)) {
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL);
		if (test_bit(WMI_FW_CAPABILITY_RSSI_REPORTING,
			     wil->fw_capabilities))
			sinfo->signal = reply.evt.rssi;
		else
			sinfo->signal = reply.evt.sqi;
	}

	return rc;
}

static int wil_cfg80211_get_station(struct wiphy *wiphy,
				    struct net_device *ndev,
				    const u8 *mac, struct station_info *sinfo)
{
	struct wil6210_vif *vif = ndev_to_vif(ndev);
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	int rc;

	int cid = wil_find_cid(wil, vif->mid, mac);

	wil_dbg_misc(wil, "get_station: %pM CID %d MID %d\n", mac, cid,
		     vif->mid);
	if (!wil_cid_valid(wil, cid))
		return -ENOENT;

	rc = wil_cid_fill_sinfo(vif, cid, sinfo);

	return rc;
}

/*
 * Find @idx-th active STA for specific MID for station dump.
 */
static int wil_find_cid_by_idx(struct wil6210_priv *wil, u8 mid, int idx)
{
	int i;

	for (i = 0; i < wil->max_assoc_sta; i++) {
		if (wil->sta[i].status == wil_sta_unused)
			continue;
		if (wil->sta[i].mid != mid)
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
	struct wil6210_vif *vif = ndev_to_vif(dev);
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	int rc;
	int cid = wil_find_cid_by_idx(wil, vif->mid, idx);

	if (!wil_cid_valid(wil, cid))
		return -ENOENT;

	ether_addr_copy(mac, wil->sta[cid].addr);
	wil_dbg_misc(wil, "dump_station: %pM CID %d MID %d\n", mac, cid,
		     vif->mid);

	rc = wil_cid_fill_sinfo(vif, cid, sinfo);

	return rc;
}

static int wil_cfg80211_start_p2p_device(struct wiphy *wiphy,
					 struct wireless_dev *wdev)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);

	wil_dbg_misc(wil, "start_p2p_device: entered\n");
	wil->p2p_dev_started = 1;
	return 0;
}

static void wil_cfg80211_stop_p2p_device(struct wiphy *wiphy,
					 struct wireless_dev *wdev)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);

	if (!wil->p2p_dev_started)
		return;

	wil_dbg_misc(wil, "stop_p2p_device: entered\n");
	mutex_lock(&wil->mutex);
	mutex_lock(&wil->vif_mutex);
	wil_p2p_stop_radio_operations(wil);
	wil->p2p_dev_started = 0;
	mutex_unlock(&wil->vif_mutex);
	mutex_unlock(&wil->mutex);
}

static int wil_cfg80211_validate_add_iface(struct wil6210_priv *wil,
					   enum nl80211_iftype new_type)
{
	int i;
	struct wireless_dev *wdev;
	struct iface_combination_params params = {
		.num_different_channels = 1,
	};

	for (i = 0; i < GET_MAX_VIFS(wil); i++) {
		if (wil->vifs[i]) {
			wdev = vif_to_wdev(wil->vifs[i]);
			params.iftype_num[wdev->iftype]++;
		}
	}
	params.iftype_num[new_type]++;
	return cfg80211_check_combinations(wil->wiphy, &params);
}

static int wil_cfg80211_validate_change_iface(struct wil6210_priv *wil,
					      struct wil6210_vif *vif,
					      enum nl80211_iftype new_type)
{
	int i, ret = 0;
	struct wireless_dev *wdev;
	struct iface_combination_params params = {
		.num_different_channels = 1,
	};
	bool check_combos = false;

	for (i = 0; i < GET_MAX_VIFS(wil); i++) {
		struct wil6210_vif *vif_pos = wil->vifs[i];

		if (vif_pos && vif != vif_pos) {
			wdev = vif_to_wdev(vif_pos);
			params.iftype_num[wdev->iftype]++;
			check_combos = true;
		}
	}

	if (check_combos) {
		params.iftype_num[new_type]++;
		ret = cfg80211_check_combinations(wil->wiphy, &params);
	}
	return ret;
}

static struct wireless_dev *
wil_cfg80211_add_iface(struct wiphy *wiphy, const char *name,
		       unsigned char name_assign_type,
		       enum nl80211_iftype type,
		       struct vif_params *params)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct net_device *ndev_main = wil->main_ndev, *ndev;
	struct wil6210_vif *vif;
	struct wireless_dev *p2p_wdev, *wdev;
	int rc;

	wil_dbg_misc(wil, "add_iface, type %d\n", type);

	/* P2P device is not a real virtual interface, it is a management-only
	 * interface that shares the main interface.
	 * Skip concurrency checks here.
	 */
	if (type == NL80211_IFTYPE_P2P_DEVICE) {
		if (wil->p2p_wdev) {
			wil_err(wil, "P2P_DEVICE interface already created\n");
			return ERR_PTR(-EINVAL);
		}

		p2p_wdev = kzalloc(sizeof(*p2p_wdev), GFP_KERNEL);
		if (!p2p_wdev)
			return ERR_PTR(-ENOMEM);

		p2p_wdev->iftype = type;
		p2p_wdev->wiphy = wiphy;
		/* use our primary ethernet address */
		ether_addr_copy(p2p_wdev->address, ndev_main->perm_addr);

		wil->p2p_wdev = p2p_wdev;

		return p2p_wdev;
	}

	if (!wil->wiphy->n_iface_combinations) {
		wil_err(wil, "virtual interfaces not supported\n");
		return ERR_PTR(-EINVAL);
	}

	rc = wil_cfg80211_validate_add_iface(wil, type);
	if (rc) {
		wil_err(wil, "iface validation failed, err=%d\n", rc);
		return ERR_PTR(rc);
	}

	vif = wil_vif_alloc(wil, name, name_assign_type, type);
	if (IS_ERR(vif))
		return ERR_CAST(vif);

	ndev = vif_to_ndev(vif);
	ether_addr_copy(ndev->perm_addr, ndev_main->perm_addr);
	if (is_valid_ether_addr(params->macaddr)) {
		ether_addr_copy(ndev->dev_addr, params->macaddr);
	} else {
		ether_addr_copy(ndev->dev_addr, ndev_main->perm_addr);
		ndev->dev_addr[0] = (ndev->dev_addr[0] ^ (1 << vif->mid)) |
			0x2; /* locally administered */
	}
	wdev = vif_to_wdev(vif);
	ether_addr_copy(wdev->address, ndev->dev_addr);

	rc = wil_vif_add(wil, vif);
	if (rc)
		goto out;

	wil_info(wil, "added VIF, mid %d iftype %d MAC %pM\n",
		 vif->mid, type, wdev->address);
	return wdev;
out:
	wil_vif_free(vif);
	return ERR_PTR(rc);
}

int wil_vif_prepare_stop(struct wil6210_vif *vif)
{
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wireless_dev *wdev = vif_to_wdev(vif);
	struct net_device *ndev;
	int rc;

	if (wdev->iftype != NL80211_IFTYPE_AP)
		return 0;

	ndev = vif_to_ndev(vif);
	if (netif_carrier_ok(ndev)) {
		rc = wmi_pcp_stop(vif);
		if (rc) {
			wil_info(wil, "failed to stop AP, status %d\n",
				 rc);
			/* continue */
		}
		wil_bcast_fini(vif);
		netif_carrier_off(ndev);
	}

	return 0;
}

static int wil_cfg80211_del_iface(struct wiphy *wiphy,
				  struct wireless_dev *wdev)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wil6210_vif *vif = wdev_to_vif(wil, wdev);
	int rc;

	wil_dbg_misc(wil, "del_iface\n");

	if (wdev->iftype == NL80211_IFTYPE_P2P_DEVICE) {
		if (wdev != wil->p2p_wdev) {
			wil_err(wil, "delete of incorrect interface 0x%p\n",
				wdev);
			return -EINVAL;
		}

		wil_cfg80211_stop_p2p_device(wiphy, wdev);
		wil_p2p_wdev_free(wil);
		return 0;
	}

	if (vif->mid == 0) {
		wil_err(wil, "cannot remove the main interface\n");
		return -EINVAL;
	}

	rc = wil_vif_prepare_stop(vif);
	if (rc)
		goto out;

	wil_info(wil, "deleted VIF, mid %d iftype %d MAC %pM\n",
		 vif->mid, wdev->iftype, wdev->address);

	wil_vif_remove(wil, vif->mid);
out:
	return rc;
}

static bool wil_is_safe_switch(enum nl80211_iftype from,
			       enum nl80211_iftype to)
{
	if (from == NL80211_IFTYPE_STATION &&
	    to == NL80211_IFTYPE_P2P_CLIENT)
		return true;

	return false;
}

static int wil_cfg80211_change_iface(struct wiphy *wiphy,
				     struct net_device *ndev,
				     enum nl80211_iftype type,
				     struct vif_params *params)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wil6210_vif *vif = ndev_to_vif(ndev);
	struct wireless_dev *wdev = vif_to_wdev(vif);
	int rc;
	bool fw_reset = false;

	wil_dbg_misc(wil, "change_iface: type=%d\n", type);

	if (wiphy->n_iface_combinations) {
		rc = wil_cfg80211_validate_change_iface(wil, vif, type);
		if (rc) {
			wil_err(wil, "iface validation failed, err=%d\n", rc);
			return rc;
		}
	}

	/* do not reset FW when there are active VIFs,
	 * because it can cause significant disruption
	 */
	if (!wil_has_other_active_ifaces(wil, ndev, true, false) &&
	    netif_running(ndev) && !wil_is_recovery_blocked(wil) &&
	    !wil_is_safe_switch(wdev->iftype, type)) {
		wil_dbg_misc(wil, "interface is up. resetting...\n");
		mutex_lock(&wil->mutex);
		__wil_down(wil);
		rc = __wil_up(wil);
		mutex_unlock(&wil->mutex);

		if (rc)
			return rc;
		fw_reset = true;
	}

	switch (type) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_P2P_GO:
		break;
	case NL80211_IFTYPE_MONITOR:
		if (params->flags)
			wil->monitor_flags = params->flags;
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (vif->mid != 0 && wil_has_active_ifaces(wil, true, false)) {
		if (!fw_reset)
			wil_vif_prepare_stop(vif);
		rc = wmi_port_delete(wil, vif->mid);
		if (rc)
			return rc;
		rc = wmi_port_allocate(wil, vif->mid, ndev->dev_addr, type);
		if (rc)
			return rc;
	}

	wdev->iftype = type;
	return 0;
}

static int wil_cfg80211_scan(struct wiphy *wiphy,
			     struct cfg80211_scan_request *request)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wireless_dev *wdev = request->wdev;
	struct wil6210_vif *vif = wdev_to_vif(wil, wdev);
	struct {
		struct wmi_start_scan_cmd cmd;
		u16 chnl[4];
	} __packed cmd;
	uint i, n;
	int rc;

	wil_dbg_misc(wil, "scan: wdev=0x%p iftype=%d\n", wdev, wdev->iftype);

	/* scan is supported on client interfaces and on AP interface */
	switch (wdev->iftype) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
	case NL80211_IFTYPE_P2P_DEVICE:
	case NL80211_IFTYPE_AP:
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* FW don't support scan after connection attempt */
	if (test_bit(wil_status_dontscan, wil->status)) {
		wil_err(wil, "Can't scan now\n");
		return -EBUSY;
	}

	mutex_lock(&wil->mutex);

	mutex_lock(&wil->vif_mutex);
	if (vif->scan_request || vif->p2p.discovery_started) {
		wil_err(wil, "Already scanning\n");
		mutex_unlock(&wil->vif_mutex);
		rc = -EAGAIN;
		goto out;
	}
	mutex_unlock(&wil->vif_mutex);

	if (wdev->iftype == NL80211_IFTYPE_P2P_DEVICE) {
		if (!wil->p2p_dev_started) {
			wil_err(wil, "P2P search requested on stopped P2P device\n");
			rc = -EIO;
			goto out;
		}
		/* social scan on P2P_DEVICE is handled as p2p search */
		if (wil_p2p_is_social_scan(request)) {
			vif->scan_request = request;
			if (vif->mid == 0)
				wil->radio_wdev = wdev;
			rc = wil_p2p_search(vif, request);
			if (rc) {
				if (vif->mid == 0)
					wil->radio_wdev =
						wil->main_ndev->ieee80211_ptr;
				vif->scan_request = NULL;
			}
			goto out;
		}
	}

	(void)wil_p2p_stop_discovery(vif);

	wil_dbg_misc(wil, "Start scan_request 0x%p\n", request);
	wil_dbg_misc(wil, "SSID count: %d", request->n_ssids);

	for (i = 0; i < request->n_ssids; i++) {
		wil_dbg_misc(wil, "SSID[%d]", i);
		wil_hex_dump_misc("SSID ", DUMP_PREFIX_OFFSET, 16, 1,
				  request->ssids[i].ssid,
				  request->ssids[i].ssid_len, true);
	}

	if (request->n_ssids)
		rc = wmi_set_ssid(vif, request->ssids[0].ssid_len,
				  request->ssids[0].ssid);
	else
		rc = wmi_set_ssid(vif, 0, NULL);

	if (rc) {
		wil_err(wil, "set SSID for scan request failed: %d\n", rc);
		goto out;
	}

	vif->scan_request = request;
	mod_timer(&vif->scan_timer, jiffies + WIL6210_SCAN_TO);

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
		wil_hex_dump_misc("Scan IE ", DUMP_PREFIX_OFFSET, 16, 1,
				  request->ie, request->ie_len, true);
	else
		wil_dbg_misc(wil, "Scan has no IE's\n");

	rc = wmi_set_ie(vif, WMI_FRAME_PROBE_REQ,
			request->ie_len, request->ie);
	if (rc)
		goto out_restore;

	if (wil->discovery_mode && cmd.cmd.scan_type == WMI_ACTIVE_SCAN) {
		cmd.cmd.discovery_mode = 1;
		wil_dbg_misc(wil, "active scan with discovery_mode=1\n");
	}

	if (vif->mid == 0)
		wil->radio_wdev = wdev;
	rc = wmi_send(wil, WMI_START_SCAN_CMDID, vif->mid,
		      &cmd, sizeof(cmd.cmd) +
		      cmd.cmd.num_channels * sizeof(cmd.cmd.channel_list[0]));

out_restore:
	if (rc) {
		del_timer_sync(&vif->scan_timer);
		if (vif->mid == 0)
			wil->radio_wdev = wil->main_ndev->ieee80211_ptr;
		vif->scan_request = NULL;
	}
out:
	mutex_unlock(&wil->mutex);
	return rc;
}

static void wil_cfg80211_abort_scan(struct wiphy *wiphy,
				    struct wireless_dev *wdev)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wil6210_vif *vif = wdev_to_vif(wil, wdev);

	wil_dbg_misc(wil, "wdev=0x%p iftype=%d\n", wdev, wdev->iftype);

	mutex_lock(&wil->mutex);
	mutex_lock(&wil->vif_mutex);

	if (!vif->scan_request)
		goto out;

	if (wdev != vif->scan_request->wdev) {
		wil_dbg_misc(wil, "abort scan was called on the wrong iface\n");
		goto out;
	}

	if (wdev == wil->p2p_wdev && wil->radio_wdev == wil->p2p_wdev)
		wil_p2p_stop_radio_operations(wil);
	else
		wil_abort_scan(vif, true);

out:
	mutex_unlock(&wil->vif_mutex);
	mutex_unlock(&wil->mutex);
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

static const char *
wil_get_auth_type_name(enum nl80211_auth_type auth_type)
{
	switch (auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		return "OPEN_SYSTEM";
	case NL80211_AUTHTYPE_SHARED_KEY:
		return "SHARED_KEY";
	case NL80211_AUTHTYPE_FT:
		return "FT";
	case NL80211_AUTHTYPE_NETWORK_EAP:
		return "NETWORK_EAP";
	case NL80211_AUTHTYPE_SAE:
		return "SAE";
	case NL80211_AUTHTYPE_AUTOMATIC:
		return "AUTOMATIC";
	default:
		return "unknown";
	}
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
	if (sme->prev_bssid)
		wil_info(wil, "  Previous BSSID=%pM\n", sme->prev_bssid);
	wil_info(wil, "  Auth Type: %s\n",
		 wil_get_auth_type_name(sme->auth_type));
	wil_info(wil, "  Privacy: %s\n", sme->privacy ? "secure" : "open");
	wil_info(wil, "  PBSS: %d\n", sme->pbss);
	wil_print_crypto(wil, &sme->crypto);
}

static int wil_ft_connect(struct wiphy *wiphy,
			  struct net_device *ndev,
			  struct cfg80211_connect_params *sme)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wil6210_vif *vif = ndev_to_vif(ndev);
	struct wmi_ft_auth_cmd auth_cmd;
	int rc;

	if (!test_bit(WMI_FW_CAPABILITY_FT_ROAMING, wil->fw_capabilities)) {
		wil_err(wil, "FT: FW does not support FT roaming\n");
		return -EOPNOTSUPP;
	}

	if (!sme->prev_bssid) {
		wil_err(wil, "FT: prev_bssid was not set\n");
		return -EINVAL;
	}

	if (ether_addr_equal(sme->prev_bssid, sme->bssid)) {
		wil_err(wil, "FT: can not roam to same AP\n");
		return -EINVAL;
	}

	if (!test_bit(wil_vif_fwconnected, vif->status)) {
		wil_err(wil, "FT: roam while not connected\n");
		return -EINVAL;
	}

	if (vif->privacy != sme->privacy) {
		wil_err(wil, "FT: privacy mismatch, current (%d) roam (%d)\n",
			vif->privacy, sme->privacy);
		return -EINVAL;
	}

	if (sme->pbss) {
		wil_err(wil, "FT: roam is not valid for PBSS\n");
		return -EINVAL;
	}

	memset(&auth_cmd, 0, sizeof(auth_cmd));
	auth_cmd.channel = sme->channel->hw_value - 1;
	ether_addr_copy(auth_cmd.bssid, sme->bssid);

	wil_info(wil, "FT: roaming\n");

	set_bit(wil_vif_ft_roam, vif->status);
	rc = wmi_send(wil, WMI_FT_AUTH_CMDID, vif->mid,
		      &auth_cmd, sizeof(auth_cmd));
	if (rc == 0)
		mod_timer(&vif->connect_timer,
			  jiffies + msecs_to_jiffies(5000));
	else
		clear_bit(wil_vif_ft_roam, vif->status);

	return rc;
}

static int wil_cfg80211_connect(struct wiphy *wiphy,
				struct net_device *ndev,
				struct cfg80211_connect_params *sme)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wil6210_vif *vif = ndev_to_vif(ndev);
	struct cfg80211_bss *bss;
	struct wmi_connect_cmd conn;
	const u8 *ssid_eid;
	const u8 *rsn_eid;
	int ch;
	int rc = 0;
	bool is_ft_roam = false;
	u8 network_type;
	enum ieee80211_bss_type bss_type = IEEE80211_BSS_TYPE_ESS;

	wil_dbg_misc(wil, "connect, mid=%d\n", vif->mid);
	wil_print_connect_params(wil, sme);

	if (sme->auth_type == NL80211_AUTHTYPE_FT)
		is_ft_roam = true;
	if (sme->auth_type == NL80211_AUTHTYPE_AUTOMATIC &&
	    test_bit(wil_vif_fwconnected, vif->status))
		is_ft_roam = true;

	if (!is_ft_roam)
		if (test_bit(wil_vif_fwconnecting, vif->status) ||
		    test_bit(wil_vif_fwconnected, vif->status))
			return -EALREADY;

	if (sme->ie_len > WMI_MAX_IE_LEN) {
		wil_err(wil, "IE too large (%td bytes)\n", sme->ie_len);
		return -ERANGE;
	}

	rsn_eid = sme->ie ?
			cfg80211_find_ie(WLAN_EID_RSN, sme->ie, sme->ie_len) :
			NULL;
	if (sme->privacy && !rsn_eid) {
		wil_info(wil, "WSC connection\n");
		if (is_ft_roam) {
			wil_err(wil, "No WSC with FT roam\n");
			return -EINVAL;
		}
	}

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
	vif->privacy = sme->privacy;
	vif->pbss = sme->pbss;

	rc = wmi_set_ie(vif, WMI_FRAME_ASSOC_REQ, sme->ie_len, sme->ie);
	if (rc)
		goto out;

	switch (bss->capability & WLAN_CAPABILITY_DMG_TYPE_MASK) {
	case WLAN_CAPABILITY_DMG_TYPE_AP:
		network_type = WMI_NETTYPE_INFRA;
		break;
	case WLAN_CAPABILITY_DMG_TYPE_PBSS:
		network_type = WMI_NETTYPE_P2P;
		break;
	default:
		wil_err(wil, "Unsupported BSS type, capability= 0x%04x\n",
			bss->capability);
		rc = -EINVAL;
		goto out;
	}

	ch = bss->channel->hw_value;
	if (ch == 0) {
		wil_err(wil, "BSS at unknown frequency %dMhz\n",
			bss->channel->center_freq);
		rc = -EOPNOTSUPP;
		goto out;
	}

	if (is_ft_roam) {
		if (network_type != WMI_NETTYPE_INFRA) {
			wil_err(wil, "FT: Unsupported BSS type, capability= 0x%04x\n",
				bss->capability);
			rc = -EINVAL;
			goto out;
		}
		rc = wil_ft_connect(wiphy, ndev, sme);
		if (rc == 0)
			vif->bss = bss;
		goto out;
	}

	if (vif->privacy) {
		/* For secure assoc, remove old keys */
		rc = wmi_del_cipher_key(vif, 0, bss->bssid,
					WMI_KEY_USE_PAIRWISE);
		if (rc) {
			wil_err(wil, "WMI_DELETE_CIPHER_KEY_CMD(PTK) failed\n");
			goto out;
		}
		rc = wmi_del_cipher_key(vif, 0, bss->bssid,
					WMI_KEY_USE_RX_GROUP);
		if (rc) {
			wil_err(wil, "WMI_DELETE_CIPHER_KEY_CMD(GTK) failed\n");
			goto out;
		}
	}

	/* WMI_CONNECT_CMD */
	memset(&conn, 0, sizeof(conn));
	conn.network_type = network_type;
	if (vif->privacy) {
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
	conn.channel = ch - 1;

	ether_addr_copy(conn.bssid, bss->bssid);
	ether_addr_copy(conn.dst_mac, bss->bssid);

	set_bit(wil_vif_fwconnecting, vif->status);

	rc = wmi_send(wil, WMI_CONNECT_CMDID, vif->mid, &conn, sizeof(conn));
	if (rc == 0) {
		netif_carrier_on(ndev);
		if (!wil_has_other_active_ifaces(wil, ndev, false, true))
			wil6210_bus_request(wil, WIL_MAX_BUS_REQUEST_KBPS);
		vif->bss = bss;
		/* Connect can take lots of time */
		mod_timer(&vif->connect_timer,
			  jiffies + msecs_to_jiffies(5000));
	} else {
		clear_bit(wil_vif_fwconnecting, vif->status);
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
	struct wil6210_vif *vif = ndev_to_vif(ndev);

	wil_dbg_misc(wil, "disconnect: reason=%d, mid=%d\n",
		     reason_code, vif->mid);

	if (!(test_bit(wil_vif_fwconnecting, vif->status) ||
	      test_bit(wil_vif_fwconnected, vif->status))) {
		wil_err(wil, "Disconnect was called while disconnected\n");
		return 0;
	}

	vif->locally_generated_disc = true;
	rc = wmi_call(wil, WMI_DISCONNECT_CMDID, vif->mid, NULL, 0,
		      WMI_DISCONNECT_EVENTID, NULL, 0,
		      WIL6210_DISCONNECT_TO_MS);
	if (rc)
		wil_err(wil, "disconnect error %d\n", rc);

	return rc;
}

static int wil_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	int rc;

	/* these parameters are explicitly not supported */
	if (changed & (WIPHY_PARAM_RETRY_LONG |
		       WIPHY_PARAM_FRAG_THRESHOLD |
		       WIPHY_PARAM_RTS_THRESHOLD))
		return -ENOTSUPP;

	if (changed & WIPHY_PARAM_RETRY_SHORT) {
		rc = wmi_set_mgmt_retry(wil, wiphy->retry_short);
		if (rc)
			return rc;
	}

	return 0;
}

int wil_cfg80211_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
			 struct cfg80211_mgmt_tx_params *params,
			 u64 *cookie)
{
	const u8 *buf = params->buf;
	size_t len = params->len;
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wil6210_vif *vif = wdev_to_vif(wil, wdev);
	int rc;
	bool tx_status;

	wil_dbg_misc(wil, "mgmt_tx: channel %d offchan %d, wait %d\n",
		     params->chan ? params->chan->hw_value : -1,
		     params->offchan,
		     params->wait);

	/* Note, currently we support the "wait" parameter only on AP mode.
	 * In other modes, user-space must call remain_on_channel before
	 * mgmt_tx or listen on a channel other than active one.
	 */

	if (params->chan && params->chan->hw_value == 0) {
		wil_err(wil, "invalid channel\n");
		return -EINVAL;
	}

	if (wdev->iftype != NL80211_IFTYPE_AP) {
		wil_dbg_misc(wil,
			     "send WMI_SW_TX_REQ_CMDID on non-AP interfaces\n");
		rc = wmi_mgmt_tx(vif, buf, len);
		goto out;
	}

	if (!params->chan || params->chan->hw_value == vif->channel) {
		wil_dbg_misc(wil,
			     "send WMI_SW_TX_REQ_CMDID for on-channel\n");
		rc = wmi_mgmt_tx(vif, buf, len);
		goto out;
	}

	if (params->offchan == 0) {
		wil_err(wil,
			"invalid channel params: current %d requested %d, off-channel not allowed\n",
			vif->channel, params->chan->hw_value);
		return -EBUSY;
	}

	/* use the wmi_mgmt_tx_ext only on AP mode and off-channel */
	rc = wmi_mgmt_tx_ext(vif, buf, len, params->chan->hw_value,
			     params->wait);

out:
	/* when the sent packet was not acked by receiver(ACK=0), rc will
	 * be -EAGAIN. In this case this function needs to return success,
	 * the ACK=0 will be reflected in tx_status.
	 */
	tx_status = (rc == 0);
	rc = (rc == -EAGAIN) ? 0 : rc;
	cfg80211_mgmt_tx_status(wdev, cookie ? *cookie : 0, buf, len,
				tx_status, GFP_KERNEL);

	return rc;
}

static int wil_cfg80211_set_channel(struct wiphy *wiphy,
				    struct cfg80211_chan_def *chandef)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);

	wil->monitor_chandef = *chandef;

	return 0;
}

static enum wmi_key_usage wil_detect_key_usage(struct wireless_dev *wdev,
					       bool pairwise)
{
	struct wil6210_priv *wil = wdev_to_wil(wdev);
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
	wil_dbg_misc(wil, "detect_key_usage: -> %s\n", key_usage_str[rc]);

	return rc;
}

static struct wil_sta_info *
wil_find_sta_by_key_usage(struct wil6210_priv *wil, u8 mid,
			  enum wmi_key_usage key_usage, const u8 *mac_addr)
{
	int cid = -EINVAL;

	if (key_usage == WMI_KEY_USE_TX_GROUP)
		return NULL; /* not needed */

	/* supplicant provides Rx group key in STA mode with NULL MAC address */
	if (mac_addr)
		cid = wil_find_cid(wil, mid, mac_addr);
	else if (key_usage == WMI_KEY_USE_RX_GROUP)
		cid = wil_find_cid_by_idx(wil, mid, 0);
	if (cid < 0) {
		wil_err(wil, "No CID for %pM %s\n", mac_addr,
			key_usage_str[key_usage]);
		return ERR_PTR(cid);
	}

	return &wil->sta[cid];
}

void wil_set_crypto_rx(u8 key_index, enum wmi_key_usage key_usage,
		       struct wil_sta_info *cs,
		       struct key_params *params)
{
	struct wil_tid_crypto_rx_single *cc;
	int tid;

	if (!cs)
		return;

	switch (key_usage) {
	case WMI_KEY_USE_PAIRWISE:
		for (tid = 0; tid < WIL_STA_TID_NUM; tid++) {
			cc = &cs->tid_crypto_rx[tid].key_id[key_index];
			if (params->seq)
				memcpy(cc->pn, params->seq,
				       IEEE80211_GCMP_PN_LEN);
			else
				memset(cc->pn, 0, IEEE80211_GCMP_PN_LEN);
			cc->key_set = true;
		}
		break;
	case WMI_KEY_USE_RX_GROUP:
		cc = &cs->group_crypto_rx.key_id[key_index];
		if (params->seq)
			memcpy(cc->pn, params->seq, IEEE80211_GCMP_PN_LEN);
		else
			memset(cc->pn, 0, IEEE80211_GCMP_PN_LEN);
		cc->key_set = true;
		break;
	default:
		break;
	}
}

static void wil_del_rx_key(u8 key_index, enum wmi_key_usage key_usage,
			   struct wil_sta_info *cs)
{
	struct wil_tid_crypto_rx_single *cc;
	int tid;

	if (!cs)
		return;

	switch (key_usage) {
	case WMI_KEY_USE_PAIRWISE:
		for (tid = 0; tid < WIL_STA_TID_NUM; tid++) {
			cc = &cs->tid_crypto_rx[tid].key_id[key_index];
			cc->key_set = false;
		}
		break;
	case WMI_KEY_USE_RX_GROUP:
		cc = &cs->group_crypto_rx.key_id[key_index];
		cc->key_set = false;
		break;
	default:
		break;
	}
}

static int wil_cfg80211_add_key(struct wiphy *wiphy,
				struct net_device *ndev,
				u8 key_index, bool pairwise,
				const u8 *mac_addr,
				struct key_params *params)
{
	int rc;
	struct wil6210_vif *vif = ndev_to_vif(ndev);
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wireless_dev *wdev = vif_to_wdev(vif);
	enum wmi_key_usage key_usage = wil_detect_key_usage(wdev, pairwise);
	struct wil_sta_info *cs = wil_find_sta_by_key_usage(wil, vif->mid,
							    key_usage,
							    mac_addr);

	if (!params) {
		wil_err(wil, "NULL params\n");
		return -EINVAL;
	}

	wil_dbg_misc(wil, "add_key: %pM %s[%d] PN %*phN\n",
		     mac_addr, key_usage_str[key_usage], key_index,
		     params->seq_len, params->seq);

	if (IS_ERR(cs)) {
		/* in FT, sta info may not be available as add_key may be
		 * sent by host before FW sends WMI_CONNECT_EVENT
		 */
		if (!test_bit(wil_vif_ft_roam, vif->status)) {
			wil_err(wil, "Not connected, %pM %s[%d] PN %*phN\n",
				mac_addr, key_usage_str[key_usage], key_index,
				params->seq_len, params->seq);
			return -EINVAL;
		}
	}

	if (!IS_ERR(cs))
		wil_del_rx_key(key_index, key_usage, cs);

	if (params->seq && params->seq_len != IEEE80211_GCMP_PN_LEN) {
		wil_err(wil,
			"Wrong PN len %d, %pM %s[%d] PN %*phN\n",
			params->seq_len, mac_addr,
			key_usage_str[key_usage], key_index,
			params->seq_len, params->seq);
		return -EINVAL;
	}

	rc = wmi_add_cipher_key(vif, key_index, mac_addr, params->key_len,
				params->key, key_usage);
	if (!rc && !IS_ERR(cs)) {
		/* update local storage used for AP recovery */
		if (key_usage == WMI_KEY_USE_TX_GROUP && params->key &&
		    params->key_len <= WMI_MAX_KEY_LEN) {
			vif->gtk_index = key_index;
			memcpy(vif->gtk, params->key, params->key_len);
			vif->gtk_len = params->key_len;
		}
		/* in FT set crypto will take place upon receiving
		 * WMI_RING_EN_EVENTID event
		 */
		wil_set_crypto_rx(key_index, key_usage, cs, params);
	}

	return rc;
}

static int wil_cfg80211_del_key(struct wiphy *wiphy,
				struct net_device *ndev,
				u8 key_index, bool pairwise,
				const u8 *mac_addr)
{
	struct wil6210_vif *vif = ndev_to_vif(ndev);
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wireless_dev *wdev = vif_to_wdev(vif);
	enum wmi_key_usage key_usage = wil_detect_key_usage(wdev, pairwise);
	struct wil_sta_info *cs = wil_find_sta_by_key_usage(wil, vif->mid,
							    key_usage,
							    mac_addr);

	wil_dbg_misc(wil, "del_key: %pM %s[%d]\n", mac_addr,
		     key_usage_str[key_usage], key_index);

	if (IS_ERR(cs))
		wil_info(wil, "Not connected, %pM %s[%d]\n",
			 mac_addr, key_usage_str[key_usage], key_index);

	if (!IS_ERR_OR_NULL(cs))
		wil_del_rx_key(key_index, key_usage, cs);

	return wmi_del_cipher_key(vif, key_index, mac_addr, key_usage);
}

/* Need to be present or wiphy_new() will WARN */
static int wil_cfg80211_set_default_key(struct wiphy *wiphy,
					struct net_device *ndev,
					u8 key_index, bool unicast,
					bool multicast)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);

	wil_dbg_misc(wil, "set_default_key: entered\n");
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

	wil_dbg_misc(wil,
		     "remain_on_channel: center_freq=%d, duration=%d iftype=%d\n",
		     chan->center_freq, duration, wdev->iftype);

	rc = wil_p2p_listen(wil, wdev, duration, chan, cookie);
	return rc;
}

static int wil_cancel_remain_on_channel(struct wiphy *wiphy,
					struct wireless_dev *wdev,
					u64 cookie)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wil6210_vif *vif = wdev_to_vif(wil, wdev);

	wil_dbg_misc(wil, "cancel_remain_on_channel\n");

	return wil_p2p_cancel_listen(vif, cookie);
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

	if (!ies1)
		ies1_len = 0;

	if (!ies2)
		ies2_len = 0;

	if (ies1_len == 0 && ies2_len == 0) {
		*merged_ies = NULL;
		*merged_len = 0;
		return 0;
	}

	buf = kmalloc(ies1_len + ies2_len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	if (ies1)
		memcpy(buf, ies1, ies1_len);
	dpos = buf + ies1_len;
	spos = ies2;
	while (spos && (spos + 1 < ies2 + ies2_len)) {
		/* IE tag at offset 0, length at offset 1 */
		u16 ielen = 2 + spos[1];

		if (spos + ielen > ies2 + ies2_len)
			break;
		if (spos[0] == WLAN_EID_VENDOR_SPECIFIC &&
		    (!ies1 || !_wil_cfg80211_find_ie(ies1, ies1_len,
						     spos, ielen))) {
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
	wil_hex_dump_misc("head     ", DUMP_PREFIX_OFFSET, 16, 1,
			  b->head, b->head_len, true);
	wil_hex_dump_misc("tail     ", DUMP_PREFIX_OFFSET, 16, 1,
			  b->tail, b->tail_len, true);
	wil_hex_dump_misc("BCON IE  ", DUMP_PREFIX_OFFSET, 16, 1,
			  b->beacon_ies, b->beacon_ies_len, true);
	wil_hex_dump_misc("PROBE    ", DUMP_PREFIX_OFFSET, 16, 1,
			  b->probe_resp, b->probe_resp_len, true);
	wil_hex_dump_misc("PROBE IE ", DUMP_PREFIX_OFFSET, 16, 1,
			  b->proberesp_ies, b->proberesp_ies_len, true);
	wil_hex_dump_misc("ASSOC IE ", DUMP_PREFIX_OFFSET, 16, 1,
			  b->assocresp_ies, b->assocresp_ies_len, true);
}

/* internal functions for device reset and starting AP */
static u8 *
_wil_cfg80211_get_proberesp_ies(const u8 *proberesp, u16 proberesp_len,
				u16 *ies_len)
{
	u8 *ies = NULL;

	if (proberesp) {
		struct ieee80211_mgmt *f =
			(struct ieee80211_mgmt *)proberesp;
		size_t hlen = offsetof(struct ieee80211_mgmt,
				       u.probe_resp.variable);

		ies = f->u.probe_resp.variable;
		if (ies_len)
			*ies_len = proberesp_len - hlen;
	}

	return ies;
}

static int _wil_cfg80211_set_ies(struct wil6210_vif *vif,
				 struct cfg80211_beacon_data *bcon)
{
	int rc;
	u16 len = 0, proberesp_len = 0;
	u8 *ies = NULL, *proberesp;

	/* update local storage used for AP recovery */
	wil_memdup_ie(&vif->proberesp, &vif->proberesp_len, bcon->probe_resp,
		      bcon->probe_resp_len);
	wil_memdup_ie(&vif->proberesp_ies, &vif->proberesp_ies_len,
		      bcon->proberesp_ies, bcon->proberesp_ies_len);
	wil_memdup_ie(&vif->assocresp_ies, &vif->assocresp_ies_len,
		      bcon->assocresp_ies, bcon->assocresp_ies_len);

	proberesp = _wil_cfg80211_get_proberesp_ies(bcon->probe_resp,
						    bcon->probe_resp_len,
						    &proberesp_len);
	rc = _wil_cfg80211_merge_extra_ies(proberesp,
					   proberesp_len,
					   bcon->proberesp_ies,
					   bcon->proberesp_ies_len,
					   &ies, &len);

	if (rc)
		goto out;

	rc = wmi_set_ie(vif, WMI_FRAME_PROBE_RESP, len, ies);
	if (rc)
		goto out;

	if (bcon->assocresp_ies)
		rc = wmi_set_ie(vif, WMI_FRAME_ASSOC_RESP,
				bcon->assocresp_ies_len, bcon->assocresp_ies);
	else
		rc = wmi_set_ie(vif, WMI_FRAME_ASSOC_RESP, len, ies);
#if 0 /* to use beacon IE's, remove this #if 0 */
	if (rc)
		goto out;

	rc = wmi_set_ie(vif, WMI_FRAME_BEACON,
			bcon->tail_len, bcon->tail);
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
	struct wil6210_vif *vif = ndev_to_vif(ndev);
	int rc;
	struct wireless_dev *wdev = ndev->ieee80211_ptr;
	u8 wmi_nettype = wil_iftype_nl2wmi(wdev->iftype);
	u8 is_go = (wdev->iftype == NL80211_IFTYPE_P2P_GO);
	u16 proberesp_len = 0;
	u8 *proberesp;
	bool ft = false;

	if (pbss)
		wmi_nettype = WMI_NETTYPE_P2P;

	wil_dbg_misc(wil, "start_ap: mid=%d, is_go=%d\n", vif->mid, is_go);
	if (is_go && !pbss) {
		wil_err(wil, "P2P GO must be in PBSS\n");
		return -ENOTSUPP;
	}

	wil_set_recovery_state(wil, fw_recovery_idle);

	proberesp = _wil_cfg80211_get_proberesp_ies(bcon->probe_resp,
						    bcon->probe_resp_len,
						    &proberesp_len);
	/* check that the probe response IEs has a MDE */
	if ((proberesp && proberesp_len > 0 &&
	     cfg80211_find_ie(WLAN_EID_MOBILITY_DOMAIN,
			      proberesp,
			      proberesp_len)))
		ft = true;

	if (ft) {
		if (!test_bit(WMI_FW_CAPABILITY_FT_ROAMING,
			      wil->fw_capabilities)) {
			wil_err(wil, "FW does not support FT roaming\n");
			return -ENOTSUPP;
		}
		set_bit(wil_vif_ft_roam, vif->status);
	}

	mutex_lock(&wil->mutex);

	if (!wil_has_other_active_ifaces(wil, ndev, true, false)) {
		__wil_down(wil);
		rc = __wil_up(wil);
		if (rc)
			goto out;
	}

	rc = wmi_set_ssid(vif, ssid_len, ssid);
	if (rc)
		goto out;

	rc = _wil_cfg80211_set_ies(vif, bcon);
	if (rc)
		goto out;

	vif->privacy = privacy;
	vif->channel = chan;
	vif->hidden_ssid = hidden_ssid;
	vif->pbss = pbss;
	vif->bi = bi;
	memcpy(vif->ssid, ssid, ssid_len);
	vif->ssid_len = ssid_len;

	netif_carrier_on(ndev);
	if (!wil_has_other_active_ifaces(wil, ndev, false, true))
		wil6210_bus_request(wil, WIL_MAX_BUS_REQUEST_KBPS);

	rc = wmi_pcp_start(vif, bi, wmi_nettype, chan, hidden_ssid, is_go);
	if (rc)
		goto err_pcp_start;

	rc = wil_bcast_init(vif);
	if (rc)
		goto err_bcast;

	goto out; /* success */

err_bcast:
	wmi_pcp_stop(vif);
err_pcp_start:
	netif_carrier_off(ndev);
	if (!wil_has_other_active_ifaces(wil, ndev, false, true))
		wil6210_bus_request(wil, WIL_DEFAULT_BUS_REQUEST_KBPS);
out:
	mutex_unlock(&wil->mutex);
	return rc;
}

void wil_cfg80211_ap_recovery(struct wil6210_priv *wil)
{
	int rc, i;
	struct wiphy *wiphy = wil_to_wiphy(wil);

	for (i = 0; i < GET_MAX_VIFS(wil); i++) {
		struct wil6210_vif *vif = wil->vifs[i];
		struct net_device *ndev;
		struct cfg80211_beacon_data bcon = {};
		struct key_params key_params = {};

		if (!vif || vif->ssid_len == 0)
			continue;

		ndev = vif_to_ndev(vif);
		bcon.proberesp_ies = vif->proberesp_ies;
		bcon.assocresp_ies = vif->assocresp_ies;
		bcon.probe_resp = vif->proberesp;
		bcon.proberesp_ies_len = vif->proberesp_ies_len;
		bcon.assocresp_ies_len = vif->assocresp_ies_len;
		bcon.probe_resp_len = vif->proberesp_len;

		wil_info(wil,
			 "AP (vif %d) recovery: privacy %d, bi %d, channel %d, hidden %d, pbss %d\n",
			 i, vif->privacy, vif->bi, vif->channel,
			 vif->hidden_ssid, vif->pbss);
		wil_hex_dump_misc("SSID ", DUMP_PREFIX_OFFSET, 16, 1,
				  vif->ssid, vif->ssid_len, true);
		rc = _wil_cfg80211_start_ap(wiphy, ndev,
					    vif->ssid, vif->ssid_len,
					    vif->privacy, vif->bi,
					    vif->channel, &bcon,
					    vif->hidden_ssid, vif->pbss);
		if (rc) {
			wil_err(wil, "vif %d recovery failed (%d)\n", i, rc);
			continue;
		}

		if (!vif->privacy || vif->gtk_len == 0)
			continue;

		key_params.key = vif->gtk;
		key_params.key_len = vif->gtk_len;
		key_params.seq_len = IEEE80211_GCMP_PN_LEN;
		rc = wil_cfg80211_add_key(wiphy, ndev, vif->gtk_index, false,
					  NULL, &key_params);
		if (rc)
			wil_err(wil, "vif %d recovery add key failed (%d)\n",
				i, rc);
	}
}

static int wil_cfg80211_change_beacon(struct wiphy *wiphy,
				      struct net_device *ndev,
				      struct cfg80211_beacon_data *bcon)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wireless_dev *wdev = ndev->ieee80211_ptr;
	struct wil6210_vif *vif = ndev_to_vif(ndev);
	int rc;
	u32 privacy = 0;

	wil_dbg_misc(wil, "change_beacon, mid=%d\n", vif->mid);
	wil_print_bcon_data(bcon);

	if (bcon->tail &&
	    cfg80211_find_ie(WLAN_EID_RSN, bcon->tail,
			     bcon->tail_len))
		privacy = 1;

	memcpy(vif->ssid, wdev->ssid, wdev->ssid_len);
	vif->ssid_len = wdev->ssid_len;

	/* in case privacy has changed, need to restart the AP */
	if (vif->privacy != privacy) {
		wil_dbg_misc(wil, "privacy changed %d=>%d. Restarting AP\n",
			     vif->privacy, privacy);

		rc = _wil_cfg80211_start_ap(wiphy, ndev, vif->ssid,
					    vif->ssid_len, privacy,
					    wdev->beacon_interval,
					    vif->channel, bcon,
					    vif->hidden_ssid,
					    vif->pbss);
	} else {
		rc = _wil_cfg80211_set_ies(vif, bcon);
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

	wil_dbg_misc(wil, "start_ap\n");

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
	wil_hex_dump_misc("SSID ", DUMP_PREFIX_OFFSET, 16, 1,
			  info->ssid, info->ssid_len, true);
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
	struct wil6210_vif *vif = ndev_to_vif(ndev);
	bool last;

	wil_dbg_misc(wil, "stop_ap, mid=%d\n", vif->mid);

	netif_carrier_off(ndev);
	last = !wil_has_other_active_ifaces(wil, ndev, false, true);
	if (last) {
		wil6210_bus_request(wil, WIL_DEFAULT_BUS_REQUEST_KBPS);
		wil_set_recovery_state(wil, fw_recovery_idle);
		set_bit(wil_status_resetting, wil->status);
	}

	mutex_lock(&wil->mutex);

	wmi_pcp_stop(vif);
	clear_bit(wil_vif_ft_roam, vif->status);
	vif->ssid_len = 0;
	wil_memdup_ie(&vif->proberesp, &vif->proberesp_len, NULL, 0);
	wil_memdup_ie(&vif->proberesp_ies, &vif->proberesp_ies_len, NULL, 0);
	wil_memdup_ie(&vif->assocresp_ies, &vif->assocresp_ies_len, NULL, 0);
	memset(vif->gtk, 0, WMI_MAX_KEY_LEN);
	vif->gtk_len = 0;

	if (last)
		__wil_down(wil);
	else
		wil_bcast_fini(vif);

	mutex_unlock(&wil->mutex);

	return 0;
}

static int wil_cfg80211_add_station(struct wiphy *wiphy,
				    struct net_device *dev,
				    const u8 *mac,
				    struct station_parameters *params)
{
	struct wil6210_vif *vif = ndev_to_vif(dev);
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);

	wil_dbg_misc(wil, "add station %pM aid %d mid %d mask 0x%x set 0x%x\n",
		     mac, params->aid, vif->mid,
		     params->sta_flags_mask, params->sta_flags_set);

	if (!disable_ap_sme) {
		wil_err(wil, "not supported with AP SME enabled\n");
		return -EOPNOTSUPP;
	}

	if (params->aid > WIL_MAX_DMG_AID) {
		wil_err(wil, "invalid aid\n");
		return -EINVAL;
	}

	return wmi_new_sta(vif, mac, params->aid);
}

static int wil_cfg80211_del_station(struct wiphy *wiphy,
				    struct net_device *dev,
				    struct station_del_parameters *params)
{
	struct wil6210_vif *vif = ndev_to_vif(dev);
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);

	wil_dbg_misc(wil, "del_station: %pM, reason=%d mid=%d\n",
		     params->mac, params->reason_code, vif->mid);

	mutex_lock(&wil->mutex);
	wil6210_disconnect(vif, params->mac, params->reason_code);
	mutex_unlock(&wil->mutex);

	return 0;
}

static int wil_cfg80211_change_station(struct wiphy *wiphy,
				       struct net_device *dev,
				       const u8 *mac,
				       struct station_parameters *params)
{
	struct wil6210_vif *vif = ndev_to_vif(dev);
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	int authorize;
	int cid, i;
	struct wil_ring_tx_data *txdata = NULL;

	wil_dbg_misc(wil, "change station %pM mask 0x%x set 0x%x mid %d\n",
		     mac, params->sta_flags_mask, params->sta_flags_set,
		     vif->mid);

	if (!disable_ap_sme) {
		wil_dbg_misc(wil, "not supported with AP SME enabled\n");
		return -EOPNOTSUPP;
	}

	if (!(params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED)))
		return 0;

	cid = wil_find_cid(wil, vif->mid, mac);
	if (cid < 0) {
		wil_err(wil, "station not found\n");
		return -ENOLINK;
	}

	for (i = 0; i < ARRAY_SIZE(wil->ring2cid_tid); i++)
		if (wil->ring2cid_tid[i][0] == cid) {
			txdata = &wil->ring_tx_data[i];
			break;
		}

	if (!txdata) {
		wil_err(wil, "ring data not found\n");
		return -ENOLINK;
	}

	authorize = params->sta_flags_set & BIT(NL80211_STA_FLAG_AUTHORIZED);
	txdata->dot1x_open = authorize ? 1 : 0;
	wil_dbg_misc(wil, "cid %d ring %d authorize %d\n", cid, i,
		     txdata->dot1x_open);

	return 0;
}

/* probe_client handling */
static void wil_probe_client_handle(struct wil6210_priv *wil,
				    struct wil6210_vif *vif,
				    struct wil_probe_client_req *req)
{
	struct net_device *ndev = vif_to_ndev(vif);
	struct wil_sta_info *sta = &wil->sta[req->cid];
	/* assume STA is alive if it is still connected,
	 * else FW will disconnect it
	 */
	bool alive = (sta->status == wil_sta_connected);

	cfg80211_probe_status(ndev, sta->addr, req->cookie, alive,
			      0, false, GFP_KERNEL);
}

static struct list_head *next_probe_client(struct wil6210_vif *vif)
{
	struct list_head *ret = NULL;

	mutex_lock(&vif->probe_client_mutex);

	if (!list_empty(&vif->probe_client_pending)) {
		ret = vif->probe_client_pending.next;
		list_del(ret);
	}

	mutex_unlock(&vif->probe_client_mutex);

	return ret;
}

void wil_probe_client_worker(struct work_struct *work)
{
	struct wil6210_vif *vif = container_of(work, struct wil6210_vif,
					       probe_client_worker);
	struct wil6210_priv *wil = vif_to_wil(vif);
	struct wil_probe_client_req *req;
	struct list_head *lh;

	while ((lh = next_probe_client(vif)) != NULL) {
		req = list_entry(lh, struct wil_probe_client_req, list);

		wil_probe_client_handle(wil, vif, req);
		kfree(req);
	}
}

void wil_probe_client_flush(struct wil6210_vif *vif)
{
	struct wil_probe_client_req *req, *t;
	struct wil6210_priv *wil = vif_to_wil(vif);

	wil_dbg_misc(wil, "probe_client_flush\n");

	mutex_lock(&vif->probe_client_mutex);

	list_for_each_entry_safe(req, t, &vif->probe_client_pending, list) {
		list_del(&req->list);
		kfree(req);
	}

	mutex_unlock(&vif->probe_client_mutex);
}

static int wil_cfg80211_probe_client(struct wiphy *wiphy,
				     struct net_device *dev,
				     const u8 *peer, u64 *cookie)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wil6210_vif *vif = ndev_to_vif(dev);
	struct wil_probe_client_req *req;
	int cid = wil_find_cid(wil, vif->mid, peer);

	wil_dbg_misc(wil, "probe_client: %pM => CID %d MID %d\n",
		     peer, cid, vif->mid);

	if (cid < 0)
		return -ENOLINK;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	req->cid = cid;
	req->cookie = cid;

	mutex_lock(&vif->probe_client_mutex);
	list_add_tail(&req->list, &vif->probe_client_pending);
	mutex_unlock(&vif->probe_client_mutex);

	*cookie = req->cookie;
	queue_work(wil->wq_service, &vif->probe_client_worker);
	return 0;
}

static int wil_cfg80211_change_bss(struct wiphy *wiphy,
				   struct net_device *dev,
				   struct bss_parameters *params)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wil6210_vif *vif = ndev_to_vif(dev);

	if (params->ap_isolate >= 0) {
		wil_dbg_misc(wil, "change_bss: ap_isolate MID %d, %d => %d\n",
			     vif->mid, vif->ap_isolate, params->ap_isolate);
		vif->ap_isolate = params->ap_isolate;
	}

	return 0;
}

static int wil_cfg80211_set_power_mgmt(struct wiphy *wiphy,
				       struct net_device *dev,
				       bool enabled, int timeout)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	enum wmi_ps_profile_type ps_profile;

	wil_dbg_misc(wil, "enabled=%d, timeout=%d\n",
		     enabled, timeout);

	if (enabled)
		ps_profile = WMI_PS_PROFILE_TYPE_DEFAULT;
	else
		ps_profile = WMI_PS_PROFILE_TYPE_PS_DISABLED;

	return wil_ps_update(wil, ps_profile);
}

static int wil_cfg80211_suspend(struct wiphy *wiphy,
				struct cfg80211_wowlan *wow)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	int rc;

	/* Setting the wakeup trigger based on wow is TBD */

	if (test_bit(wil_status_suspended, wil->status)) {
		wil_dbg_pm(wil, "trying to suspend while suspended\n");
		return 0;
	}

	rc = wil_can_suspend(wil, false);
	if (rc)
		goto out;

	wil_dbg_pm(wil, "suspending\n");

	mutex_lock(&wil->mutex);
	mutex_lock(&wil->vif_mutex);
	wil_p2p_stop_radio_operations(wil);
	wil_abort_scan_all_vifs(wil, true);
	mutex_unlock(&wil->vif_mutex);
	mutex_unlock(&wil->mutex);

out:
	return rc;
}

static int wil_cfg80211_resume(struct wiphy *wiphy)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);

	wil_dbg_pm(wil, "resuming\n");

	return 0;
}

static int
wil_cfg80211_sched_scan_start(struct wiphy *wiphy,
			      struct net_device *dev,
			      struct cfg80211_sched_scan_request *request)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wil6210_vif *vif = ndev_to_vif(dev);
	int i, rc;

	if (vif->mid != 0)
		return -EOPNOTSUPP;

	wil_dbg_misc(wil,
		     "sched scan start: n_ssids %d, ie_len %zu, flags 0x%x\n",
		     request->n_ssids, request->ie_len, request->flags);
	for (i = 0; i < request->n_ssids; i++) {
		wil_dbg_misc(wil, "SSID[%d]:", i);
		wil_hex_dump_misc("SSID ", DUMP_PREFIX_OFFSET, 16, 1,
				  request->ssids[i].ssid,
				  request->ssids[i].ssid_len, true);
	}
	wil_dbg_misc(wil, "channels:");
	for (i = 0; i < request->n_channels; i++)
		wil_dbg_misc(wil, " %d%s", request->channels[i]->hw_value,
			     i == request->n_channels - 1 ? "\n" : "");
	wil_dbg_misc(wil, "n_match_sets %d, min_rssi_thold %d, delay %d\n",
		     request->n_match_sets, request->min_rssi_thold,
		     request->delay);
	for (i = 0; i < request->n_match_sets; i++) {
		struct cfg80211_match_set *ms = &request->match_sets[i];

		wil_dbg_misc(wil, "MATCHSET[%d]: rssi_thold %d\n",
			     i, ms->rssi_thold);
		wil_hex_dump_misc("SSID ", DUMP_PREFIX_OFFSET, 16, 1,
				  ms->ssid.ssid,
				  ms->ssid.ssid_len, true);
	}
	wil_dbg_misc(wil, "n_scan_plans %d\n", request->n_scan_plans);
	for (i = 0; i < request->n_scan_plans; i++) {
		struct cfg80211_sched_scan_plan *sp = &request->scan_plans[i];

		wil_dbg_misc(wil, "SCAN PLAN[%d]: interval %d iterations %d\n",
			     i, sp->interval, sp->iterations);
	}

	rc = wmi_set_ie(vif, WMI_FRAME_PROBE_REQ,
			request->ie_len, request->ie);
	if (rc)
		return rc;
	return wmi_start_sched_scan(wil, request);
}

static int
wil_cfg80211_sched_scan_stop(struct wiphy *wiphy, struct net_device *dev,
			     u64 reqid)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wil6210_vif *vif = ndev_to_vif(dev);
	int rc;

	if (vif->mid != 0)
		return -EOPNOTSUPP;

	rc = wmi_stop_sched_scan(wil);
	/* device would return error if it thinks PNO is already stopped.
	 * ignore the return code so user space and driver gets back in-sync
	 */
	wil_dbg_misc(wil, "sched scan stopped (%d)\n", rc);

	return 0;
}

static int
wil_cfg80211_update_ft_ies(struct wiphy *wiphy, struct net_device *dev,
			   struct cfg80211_update_ft_ies_params *ftie)
{
	struct wil6210_priv *wil = wiphy_to_wil(wiphy);
	struct wil6210_vif *vif = ndev_to_vif(dev);
	struct cfg80211_bss *bss;
	struct wmi_ft_reassoc_cmd reassoc;
	int rc = 0;

	wil_dbg_misc(wil, "update ft ies, mid=%d\n", vif->mid);
	wil_hex_dump_misc("FT IE ", DUMP_PREFIX_OFFSET, 16, 1,
			  ftie->ie, ftie->ie_len, true);

	if (!test_bit(WMI_FW_CAPABILITY_FT_ROAMING, wil->fw_capabilities)) {
		wil_err(wil, "FW does not support FT roaming\n");
		return -EOPNOTSUPP;
	}

	rc = wmi_update_ft_ies(vif, ftie->ie_len, ftie->ie);
	if (rc)
		return rc;

	if (!test_bit(wil_vif_ft_roam, vif->status))
		/* vif is not roaming */
		return 0;

	/* wil_vif_ft_roam is set. wil_cfg80211_update_ft_ies is used as
	 * a trigger for reassoc
	 */

	bss = vif->bss;
	if (!bss) {
		wil_err(wil, "FT: bss is NULL\n");
		return -EINVAL;
	}

	memset(&reassoc, 0, sizeof(reassoc));
	ether_addr_copy(reassoc.bssid, bss->bssid);

	rc = wmi_send(wil, WMI_FT_REASSOC_CMDID, vif->mid,
		      &reassoc, sizeof(reassoc));
	if (rc)
		wil_err(wil, "FT: reassoc failed (%d)\n", rc);

	return rc;
}

static const struct cfg80211_ops wil_cfg80211_ops = {
	.add_virtual_intf = wil_cfg80211_add_iface,
	.del_virtual_intf = wil_cfg80211_del_iface,
	.scan = wil_cfg80211_scan,
	.abort_scan = wil_cfg80211_abort_scan,
	.connect = wil_cfg80211_connect,
	.disconnect = wil_cfg80211_disconnect,
	.set_wiphy_params = wil_cfg80211_set_wiphy_params,
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
	.add_station = wil_cfg80211_add_station,
	.del_station = wil_cfg80211_del_station,
	.change_station = wil_cfg80211_change_station,
	.probe_client = wil_cfg80211_probe_client,
	.change_bss = wil_cfg80211_change_bss,
	/* P2P device */
	.start_p2p_device = wil_cfg80211_start_p2p_device,
	.stop_p2p_device = wil_cfg80211_stop_p2p_device,
	.set_power_mgmt = wil_cfg80211_set_power_mgmt,
	.suspend = wil_cfg80211_suspend,
	.resume = wil_cfg80211_resume,
	.sched_scan_start = wil_cfg80211_sched_scan_start,
	.sched_scan_stop = wil_cfg80211_sched_scan_stop,
	.update_ft_ies = wil_cfg80211_update_ft_ies,
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
	wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL |
			WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD |
			WIPHY_FLAG_PS_ON_BY_DEFAULT;
	if (!disable_ap_sme)
		wiphy->flags |= WIPHY_FLAG_HAVE_AP_SME;
	dev_dbg(wiphy_dev(wiphy), "%s : flags = 0x%08x\n",
		__func__, wiphy->flags);
	wiphy->probe_resp_offload =
		NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS |
		NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS2 |
		NL80211_PROBE_RESP_OFFLOAD_SUPPORT_P2P;

	wiphy->bands[NL80211_BAND_60GHZ] = &wil_band_60ghz;

	/* may change after reading FW capabilities */
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_UNSPEC;

	wiphy->cipher_suites = wil_cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(wil_cipher_suites);
	wiphy->mgmt_stypes = wil_mgmt_stypes;
	wiphy->features |= NL80211_FEATURE_SK_TX_STATUS;

	wiphy->n_vendor_commands = ARRAY_SIZE(wil_nl80211_vendor_commands);
	wiphy->vendor_commands = wil_nl80211_vendor_commands;

#ifdef CONFIG_PM
	wiphy->wowlan = &wil_wowlan_support;
#endif
}

int wil_cfg80211_iface_combinations_from_fw(
	struct wil6210_priv *wil, const struct wil_fw_record_concurrency *conc)
{
	struct wiphy *wiphy = wil_to_wiphy(wil);
	u32 total_limits = 0;
	u16 n_combos;
	const struct wil_fw_concurrency_combo *combo;
	const struct wil_fw_concurrency_limit *limit;
	struct ieee80211_iface_combination *iface_combinations;
	struct ieee80211_iface_limit *iface_limit;
	int i, j;

	if (wiphy->iface_combinations) {
		wil_dbg_misc(wil, "iface_combinations already set, skipping\n");
		return 0;
	}

	combo = conc->combos;
	n_combos = le16_to_cpu(conc->n_combos);
	for (i = 0; i < n_combos; i++) {
		total_limits += combo->n_limits;
		limit = combo->limits + combo->n_limits;
		combo = (struct wil_fw_concurrency_combo *)limit;
	}

	iface_combinations =
		kzalloc(n_combos * sizeof(struct ieee80211_iface_combination) +
			total_limits * sizeof(struct ieee80211_iface_limit),
			GFP_KERNEL);
	if (!iface_combinations)
		return -ENOMEM;
	iface_limit = (struct ieee80211_iface_limit *)(iface_combinations +
						       n_combos);
	combo = conc->combos;
	for (i = 0; i < n_combos; i++) {
		iface_combinations[i].max_interfaces = combo->max_interfaces;
		iface_combinations[i].num_different_channels =
			combo->n_diff_channels;
		iface_combinations[i].beacon_int_infra_match =
			combo->same_bi;
		iface_combinations[i].n_limits = combo->n_limits;
		wil_dbg_misc(wil,
			     "iface_combination %d: max_if %d, num_ch %d, bi_match %d\n",
			     i, iface_combinations[i].max_interfaces,
			     iface_combinations[i].num_different_channels,
			     iface_combinations[i].beacon_int_infra_match);
		limit = combo->limits;
		for (j = 0; j < combo->n_limits; j++) {
			iface_limit[j].max = le16_to_cpu(limit[j].max);
			iface_limit[j].types = le16_to_cpu(limit[j].types);
			wil_dbg_misc(wil,
				     "limit %d: max %d types 0x%x\n", j,
				     iface_limit[j].max, iface_limit[j].types);
		}
		iface_combinations[i].limits = iface_limit;
		iface_limit += combo->n_limits;
		limit += combo->n_limits;
		combo = (struct wil_fw_concurrency_combo *)limit;
	}

	wil_dbg_misc(wil, "multiple VIFs supported, n_mids %d\n", conc->n_mids);
	wil->max_vifs = conc->n_mids + 1; /* including main interface */
	if (wil->max_vifs > WIL_MAX_VIFS) {
		wil_info(wil, "limited number of VIFs supported(%d, FW %d)\n",
			 WIL_MAX_VIFS, wil->max_vifs);
		wil->max_vifs = WIL_MAX_VIFS;
	}
	wiphy->n_iface_combinations = n_combos;
	wiphy->iface_combinations = iface_combinations;
	return 0;
}

struct wil6210_priv *wil_cfg80211_init(struct device *dev)
{
	struct wiphy *wiphy;
	struct wil6210_priv *wil;
	struct ieee80211_channel *ch;

	dev_dbg(dev, "%s()\n", __func__);

	/* Note: the wireless_dev structure is no longer allocated here.
	 * Instead, it is allocated as part of the net_device structure
	 * for main interface and each VIF.
	 */
	wiphy = wiphy_new(&wil_cfg80211_ops, sizeof(struct wil6210_priv));
	if (!wiphy)
		return ERR_PTR(-ENOMEM);

	set_wiphy_dev(wiphy, dev);
	wil_wiphy_init(wiphy);

	wil = wiphy_to_wil(wiphy);
	wil->wiphy = wiphy;

	/* default monitor channel */
	ch = wiphy->bands[NL80211_BAND_60GHZ]->channels;
	cfg80211_chandef_create(&wil->monitor_chandef, ch, NL80211_CHAN_NO_HT);

	return wil;
}

void wil_cfg80211_deinit(struct wil6210_priv *wil)
{
	struct wiphy *wiphy = wil_to_wiphy(wil);

	dev_dbg(wil_to_dev(wil), "%s()\n", __func__);

	if (!wiphy)
		return;

	kfree(wiphy->iface_combinations);
	wiphy->iface_combinations = NULL;

	wiphy_free(wiphy);
	/* do not access wil6210_priv after returning from here */
}

void wil_p2p_wdev_free(struct wil6210_priv *wil)
{
	struct wireless_dev *p2p_wdev;

	mutex_lock(&wil->vif_mutex);
	p2p_wdev = wil->p2p_wdev;
	wil->p2p_wdev = NULL;
	wil->radio_wdev = wil->main_ndev->ieee80211_ptr;
	mutex_unlock(&wil->vif_mutex);
	if (p2p_wdev) {
		cfg80211_unregister_wdev(p2p_wdev);
		kfree(p2p_wdev);
	}
}

static int wil_rf_sector_status_to_rc(u8 status)
{
	switch (status) {
	case WMI_RF_SECTOR_STATUS_SUCCESS:
		return 0;
	case WMI_RF_SECTOR_STATUS_BAD_PARAMETERS_ERROR:
		return -EINVAL;
	case WMI_RF_SECTOR_STATUS_BUSY_ERROR:
		return -EAGAIN;
	case WMI_RF_SECTOR_STATUS_NOT_SUPPORTED_ERROR:
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}
}

static int wil_rf_sector_get_cfg(struct wiphy *wiphy,
				 struct wireless_dev *wdev,
				 const void *data, int data_len)
{
	struct wil6210_priv *wil = wdev_to_wil(wdev);
	struct wil6210_vif *vif = wdev_to_vif(wil, wdev);
	int rc;
	struct nlattr *tb[QCA_ATTR_DMG_RF_SECTOR_MAX + 1];
	u16 sector_index;
	u8 sector_type;
	u32 rf_modules_vec;
	struct wmi_get_rf_sector_params_cmd cmd;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_get_rf_sector_params_done_event evt;
	} __packed reply = {
		.evt = {.status = WMI_RF_SECTOR_STATUS_NOT_SUPPORTED_ERROR},
	};
	struct sk_buff *msg;
	struct nlattr *nl_cfgs, *nl_cfg;
	u32 i;
	struct wmi_rf_sector_info *si;

	if (!test_bit(WMI_FW_CAPABILITY_RF_SECTORS, wil->fw_capabilities))
		return -EOPNOTSUPP;

	rc = nla_parse_deprecated(tb, QCA_ATTR_DMG_RF_SECTOR_MAX, data,
				  data_len, wil_rf_sector_policy, NULL);
	if (rc) {
		wil_err(wil, "Invalid rf sector ATTR\n");
		return rc;
	}

	if (!tb[QCA_ATTR_DMG_RF_SECTOR_INDEX] ||
	    !tb[QCA_ATTR_DMG_RF_SECTOR_TYPE] ||
	    !tb[QCA_ATTR_DMG_RF_MODULE_MASK]) {
		wil_err(wil, "Invalid rf sector spec\n");
		return -EINVAL;
	}

	sector_index = nla_get_u16(
		tb[QCA_ATTR_DMG_RF_SECTOR_INDEX]);
	if (sector_index >= WIL_MAX_RF_SECTORS) {
		wil_err(wil, "Invalid sector index %d\n", sector_index);
		return -EINVAL;
	}

	sector_type = nla_get_u8(tb[QCA_ATTR_DMG_RF_SECTOR_TYPE]);
	if (sector_type >= QCA_ATTR_DMG_RF_SECTOR_TYPE_MAX) {
		wil_err(wil, "Invalid sector type %d\n", sector_type);
		return -EINVAL;
	}

	rf_modules_vec = nla_get_u32(
		tb[QCA_ATTR_DMG_RF_MODULE_MASK]);
	if (rf_modules_vec >= BIT(WMI_MAX_RF_MODULES_NUM)) {
		wil_err(wil, "Invalid rf module mask 0x%x\n", rf_modules_vec);
		return -EINVAL;
	}

	cmd.sector_idx = cpu_to_le16(sector_index);
	cmd.sector_type = sector_type;
	cmd.rf_modules_vec = rf_modules_vec & 0xFF;
	rc = wmi_call(wil, WMI_GET_RF_SECTOR_PARAMS_CMDID, vif->mid,
		      &cmd, sizeof(cmd), WMI_GET_RF_SECTOR_PARAMS_DONE_EVENTID,
		      &reply, sizeof(reply),
		      500);
	if (rc)
		return rc;
	if (reply.evt.status) {
		wil_err(wil, "get rf sector cfg failed with status %d\n",
			reply.evt.status);
		return wil_rf_sector_status_to_rc(reply.evt.status);
	}

	msg = cfg80211_vendor_cmd_alloc_reply_skb(
		wiphy, 64 * WMI_MAX_RF_MODULES_NUM);
	if (!msg)
		return -ENOMEM;

	if (nla_put_u64_64bit(msg, QCA_ATTR_TSF,
			      le64_to_cpu(reply.evt.tsf),
			      QCA_ATTR_PAD))
		goto nla_put_failure;

	nl_cfgs = nla_nest_start_noflag(msg, QCA_ATTR_DMG_RF_SECTOR_CFG);
	if (!nl_cfgs)
		goto nla_put_failure;
	for (i = 0; i < WMI_MAX_RF_MODULES_NUM; i++) {
		if (!(rf_modules_vec & BIT(i)))
			continue;
		nl_cfg = nla_nest_start_noflag(msg, i);
		if (!nl_cfg)
			goto nla_put_failure;
		si = &reply.evt.sectors_info[i];
		if (nla_put_u8(msg, QCA_ATTR_DMG_RF_SECTOR_CFG_MODULE_INDEX,
			       i) ||
		    nla_put_u32(msg, QCA_ATTR_DMG_RF_SECTOR_CFG_ETYPE0,
				le32_to_cpu(si->etype0)) ||
		    nla_put_u32(msg, QCA_ATTR_DMG_RF_SECTOR_CFG_ETYPE1,
				le32_to_cpu(si->etype1)) ||
		    nla_put_u32(msg, QCA_ATTR_DMG_RF_SECTOR_CFG_ETYPE2,
				le32_to_cpu(si->etype2)) ||
		    nla_put_u32(msg, QCA_ATTR_DMG_RF_SECTOR_CFG_PSH_HI,
				le32_to_cpu(si->psh_hi)) ||
		    nla_put_u32(msg, QCA_ATTR_DMG_RF_SECTOR_CFG_PSH_LO,
				le32_to_cpu(si->psh_lo)) ||
		    nla_put_u32(msg, QCA_ATTR_DMG_RF_SECTOR_CFG_DTYPE_X16,
				le32_to_cpu(si->dtype_swch_off)))
			goto nla_put_failure;
		nla_nest_end(msg, nl_cfg);
	}

	nla_nest_end(msg, nl_cfgs);
	rc = cfg80211_vendor_cmd_reply(msg);
	return rc;
nla_put_failure:
	kfree_skb(msg);
	return -ENOBUFS;
}

static int wil_rf_sector_set_cfg(struct wiphy *wiphy,
				 struct wireless_dev *wdev,
				 const void *data, int data_len)
{
	struct wil6210_priv *wil = wdev_to_wil(wdev);
	struct wil6210_vif *vif = wdev_to_vif(wil, wdev);
	int rc, tmp;
	struct nlattr *tb[QCA_ATTR_DMG_RF_SECTOR_MAX + 1];
	struct nlattr *tb2[QCA_ATTR_DMG_RF_SECTOR_CFG_MAX + 1];
	u16 sector_index, rf_module_index;
	u8 sector_type;
	u32 rf_modules_vec = 0;
	struct wmi_set_rf_sector_params_cmd cmd;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_set_rf_sector_params_done_event evt;
	} __packed reply = {
		.evt = {.status = WMI_RF_SECTOR_STATUS_NOT_SUPPORTED_ERROR},
	};
	struct nlattr *nl_cfg;
	struct wmi_rf_sector_info *si;

	if (!test_bit(WMI_FW_CAPABILITY_RF_SECTORS, wil->fw_capabilities))
		return -EOPNOTSUPP;

	rc = nla_parse_deprecated(tb, QCA_ATTR_DMG_RF_SECTOR_MAX, data,
				  data_len, wil_rf_sector_policy, NULL);
	if (rc) {
		wil_err(wil, "Invalid rf sector ATTR\n");
		return rc;
	}

	if (!tb[QCA_ATTR_DMG_RF_SECTOR_INDEX] ||
	    !tb[QCA_ATTR_DMG_RF_SECTOR_TYPE] ||
	    !tb[QCA_ATTR_DMG_RF_SECTOR_CFG]) {
		wil_err(wil, "Invalid rf sector spec\n");
		return -EINVAL;
	}

	sector_index = nla_get_u16(
		tb[QCA_ATTR_DMG_RF_SECTOR_INDEX]);
	if (sector_index >= WIL_MAX_RF_SECTORS) {
		wil_err(wil, "Invalid sector index %d\n", sector_index);
		return -EINVAL;
	}

	sector_type = nla_get_u8(tb[QCA_ATTR_DMG_RF_SECTOR_TYPE]);
	if (sector_type >= QCA_ATTR_DMG_RF_SECTOR_TYPE_MAX) {
		wil_err(wil, "Invalid sector type %d\n", sector_type);
		return -EINVAL;
	}

	memset(&cmd, 0, sizeof(cmd));

	cmd.sector_idx = cpu_to_le16(sector_index);
	cmd.sector_type = sector_type;
	nla_for_each_nested(nl_cfg, tb[QCA_ATTR_DMG_RF_SECTOR_CFG],
			    tmp) {
		rc = nla_parse_nested_deprecated(tb2,
						 QCA_ATTR_DMG_RF_SECTOR_CFG_MAX,
						 nl_cfg,
						 wil_rf_sector_cfg_policy,
						 NULL);
		if (rc) {
			wil_err(wil, "invalid sector cfg\n");
			return -EINVAL;
		}

		if (!tb2[QCA_ATTR_DMG_RF_SECTOR_CFG_MODULE_INDEX] ||
		    !tb2[QCA_ATTR_DMG_RF_SECTOR_CFG_ETYPE0] ||
		    !tb2[QCA_ATTR_DMG_RF_SECTOR_CFG_ETYPE1] ||
		    !tb2[QCA_ATTR_DMG_RF_SECTOR_CFG_ETYPE2] ||
		    !tb2[QCA_ATTR_DMG_RF_SECTOR_CFG_PSH_HI] ||
		    !tb2[QCA_ATTR_DMG_RF_SECTOR_CFG_PSH_LO] ||
		    !tb2[QCA_ATTR_DMG_RF_SECTOR_CFG_DTYPE_X16]) {
			wil_err(wil, "missing cfg params\n");
			return -EINVAL;
		}

		rf_module_index = nla_get_u8(
			tb2[QCA_ATTR_DMG_RF_SECTOR_CFG_MODULE_INDEX]);
		if (rf_module_index >= WMI_MAX_RF_MODULES_NUM) {
			wil_err(wil, "invalid RF module index %d\n",
				rf_module_index);
			return -EINVAL;
		}
		rf_modules_vec |= BIT(rf_module_index);
		si = &cmd.sectors_info[rf_module_index];
		si->etype0 = cpu_to_le32(nla_get_u32(
			tb2[QCA_ATTR_DMG_RF_SECTOR_CFG_ETYPE0]));
		si->etype1 = cpu_to_le32(nla_get_u32(
			tb2[QCA_ATTR_DMG_RF_SECTOR_CFG_ETYPE1]));
		si->etype2 = cpu_to_le32(nla_get_u32(
			tb2[QCA_ATTR_DMG_RF_SECTOR_CFG_ETYPE2]));
		si->psh_hi = cpu_to_le32(nla_get_u32(
			tb2[QCA_ATTR_DMG_RF_SECTOR_CFG_PSH_HI]));
		si->psh_lo = cpu_to_le32(nla_get_u32(
			tb2[QCA_ATTR_DMG_RF_SECTOR_CFG_PSH_LO]));
		si->dtype_swch_off = cpu_to_le32(nla_get_u32(
			tb2[QCA_ATTR_DMG_RF_SECTOR_CFG_DTYPE_X16]));
	}

	cmd.rf_modules_vec = rf_modules_vec & 0xFF;
	rc = wmi_call(wil, WMI_SET_RF_SECTOR_PARAMS_CMDID, vif->mid,
		      &cmd, sizeof(cmd), WMI_SET_RF_SECTOR_PARAMS_DONE_EVENTID,
		      &reply, sizeof(reply),
		      500);
	if (rc)
		return rc;
	return wil_rf_sector_status_to_rc(reply.evt.status);
}

static int wil_rf_sector_get_selected(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data, int data_len)
{
	struct wil6210_priv *wil = wdev_to_wil(wdev);
	struct wil6210_vif *vif = wdev_to_vif(wil, wdev);
	int rc;
	struct nlattr *tb[QCA_ATTR_DMG_RF_SECTOR_MAX + 1];
	u8 sector_type, mac_addr[ETH_ALEN];
	int cid = 0;
	struct wmi_get_selected_rf_sector_index_cmd cmd;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_get_selected_rf_sector_index_done_event evt;
	} __packed reply = {
		.evt = {.status = WMI_RF_SECTOR_STATUS_NOT_SUPPORTED_ERROR},
	};
	struct sk_buff *msg;

	if (!test_bit(WMI_FW_CAPABILITY_RF_SECTORS, wil->fw_capabilities))
		return -EOPNOTSUPP;

	rc = nla_parse_deprecated(tb, QCA_ATTR_DMG_RF_SECTOR_MAX, data,
				  data_len, wil_rf_sector_policy, NULL);
	if (rc) {
		wil_err(wil, "Invalid rf sector ATTR\n");
		return rc;
	}

	if (!tb[QCA_ATTR_DMG_RF_SECTOR_TYPE]) {
		wil_err(wil, "Invalid rf sector spec\n");
		return -EINVAL;
	}
	sector_type = nla_get_u8(tb[QCA_ATTR_DMG_RF_SECTOR_TYPE]);
	if (sector_type >= QCA_ATTR_DMG_RF_SECTOR_TYPE_MAX) {
		wil_err(wil, "Invalid sector type %d\n", sector_type);
		return -EINVAL;
	}

	if (tb[QCA_ATTR_MAC_ADDR]) {
		ether_addr_copy(mac_addr, nla_data(tb[QCA_ATTR_MAC_ADDR]));
		cid = wil_find_cid(wil, vif->mid, mac_addr);
		if (cid < 0) {
			wil_err(wil, "invalid MAC address %pM\n", mac_addr);
			return -ENOENT;
		}
	} else {
		if (test_bit(wil_vif_fwconnected, vif->status)) {
			wil_err(wil, "must specify MAC address when connected\n");
			return -EINVAL;
		}
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.cid = (u8)cid;
	cmd.sector_type = sector_type;
	rc = wmi_call(wil, WMI_GET_SELECTED_RF_SECTOR_INDEX_CMDID, vif->mid,
		      &cmd, sizeof(cmd),
		      WMI_GET_SELECTED_RF_SECTOR_INDEX_DONE_EVENTID,
		      &reply, sizeof(reply),
		      500);
	if (rc)
		return rc;
	if (reply.evt.status) {
		wil_err(wil, "get rf selected sector cfg failed with status %d\n",
			reply.evt.status);
		return wil_rf_sector_status_to_rc(reply.evt.status);
	}

	msg = cfg80211_vendor_cmd_alloc_reply_skb(
		wiphy, 64 * WMI_MAX_RF_MODULES_NUM);
	if (!msg)
		return -ENOMEM;

	if (nla_put_u64_64bit(msg, QCA_ATTR_TSF,
			      le64_to_cpu(reply.evt.tsf),
			      QCA_ATTR_PAD) ||
	    nla_put_u16(msg, QCA_ATTR_DMG_RF_SECTOR_INDEX,
			le16_to_cpu(reply.evt.sector_idx)))
		goto nla_put_failure;

	rc = cfg80211_vendor_cmd_reply(msg);
	return rc;
nla_put_failure:
	kfree_skb(msg);
	return -ENOBUFS;
}

static int wil_rf_sector_wmi_set_selected(struct wil6210_priv *wil,
					  u8 mid, u16 sector_index,
					  u8 sector_type, u8 cid)
{
	struct wmi_set_selected_rf_sector_index_cmd cmd;
	struct {
		struct wmi_cmd_hdr wmi;
		struct wmi_set_selected_rf_sector_index_done_event evt;
	} __packed reply = {
		.evt = {.status = WMI_RF_SECTOR_STATUS_NOT_SUPPORTED_ERROR},
	};
	int rc;

	memset(&cmd, 0, sizeof(cmd));
	cmd.sector_idx = cpu_to_le16(sector_index);
	cmd.sector_type = sector_type;
	cmd.cid = (u8)cid;
	rc = wmi_call(wil, WMI_SET_SELECTED_RF_SECTOR_INDEX_CMDID, mid,
		      &cmd, sizeof(cmd),
		      WMI_SET_SELECTED_RF_SECTOR_INDEX_DONE_EVENTID,
		      &reply, sizeof(reply),
		      500);
	if (rc)
		return rc;
	return wil_rf_sector_status_to_rc(reply.evt.status);
}

static int wil_rf_sector_set_selected(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data, int data_len)
{
	struct wil6210_priv *wil = wdev_to_wil(wdev);
	struct wil6210_vif *vif = wdev_to_vif(wil, wdev);
	int rc;
	struct nlattr *tb[QCA_ATTR_DMG_RF_SECTOR_MAX + 1];
	u16 sector_index;
	u8 sector_type, mac_addr[ETH_ALEN], i;
	int cid = 0;

	if (!test_bit(WMI_FW_CAPABILITY_RF_SECTORS, wil->fw_capabilities))
		return -EOPNOTSUPP;

	rc = nla_parse_deprecated(tb, QCA_ATTR_DMG_RF_SECTOR_MAX, data,
				  data_len, wil_rf_sector_policy, NULL);
	if (rc) {
		wil_err(wil, "Invalid rf sector ATTR\n");
		return rc;
	}

	if (!tb[QCA_ATTR_DMG_RF_SECTOR_INDEX] ||
	    !tb[QCA_ATTR_DMG_RF_SECTOR_TYPE]) {
		wil_err(wil, "Invalid rf sector spec\n");
		return -EINVAL;
	}

	sector_index = nla_get_u16(
		tb[QCA_ATTR_DMG_RF_SECTOR_INDEX]);
	if (sector_index >= WIL_MAX_RF_SECTORS &&
	    sector_index != WMI_INVALID_RF_SECTOR_INDEX) {
		wil_err(wil, "Invalid sector index %d\n", sector_index);
		return -EINVAL;
	}

	sector_type = nla_get_u8(tb[QCA_ATTR_DMG_RF_SECTOR_TYPE]);
	if (sector_type >= QCA_ATTR_DMG_RF_SECTOR_TYPE_MAX) {
		wil_err(wil, "Invalid sector type %d\n", sector_type);
		return -EINVAL;
	}

	if (tb[QCA_ATTR_MAC_ADDR]) {
		ether_addr_copy(mac_addr, nla_data(tb[QCA_ATTR_MAC_ADDR]));
		if (!is_broadcast_ether_addr(mac_addr)) {
			cid = wil_find_cid(wil, vif->mid, mac_addr);
			if (cid < 0) {
				wil_err(wil, "invalid MAC address %pM\n",
					mac_addr);
				return -ENOENT;
			}
		} else {
			if (sector_index != WMI_INVALID_RF_SECTOR_INDEX) {
				wil_err(wil, "broadcast MAC valid only with unlocking\n");
				return -EINVAL;
			}
			cid = -1;
		}
	} else {
		if (test_bit(wil_vif_fwconnected, vif->status)) {
			wil_err(wil, "must specify MAC address when connected\n");
			return -EINVAL;
		}
		/* otherwise, using cid=0 for unassociated station */
	}

	if (cid >= 0) {
		rc = wil_rf_sector_wmi_set_selected(wil, vif->mid, sector_index,
						    sector_type, cid);
	} else {
		/* unlock all cids */
		rc = wil_rf_sector_wmi_set_selected(
			wil, vif->mid, WMI_INVALID_RF_SECTOR_INDEX,
			sector_type, WIL_CID_ALL);
		if (rc == -EINVAL) {
			for (i = 0; i < wil->max_assoc_sta; i++) {
				if (wil->sta[i].mid != vif->mid)
					continue;
				rc = wil_rf_sector_wmi_set_selected(
					wil, vif->mid,
					WMI_INVALID_RF_SECTOR_INDEX,
					sector_type, i);
				/* the FW will silently ignore and return
				 * success for unused cid, so abort the loop
				 * on any other error
				 */
				if (rc) {
					wil_err(wil, "unlock cid %d failed with status %d\n",
						i, rc);
					break;
				}
			}
		}
	}

	return rc;
}
