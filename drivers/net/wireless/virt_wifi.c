// SPDX-License-Identifier: GPL-2.0
/* drivers/net/wireless/virt_wifi.c
 *
 * A fake implementation of cfg80211_ops that can be tacked on to an ethernet
 * net_device to make it appear as a wireless connection.
 *
 * Copyright (C) 2018 Google, Inc.
 *
 * Author: schuffelen@google.com
 */

#include <net/cfg80211.h>
#include <net/rtnetlink.h>
#include <linux/etherdevice.h>
#include <linux/module.h>

#include <net/cfg80211.h>
#include <net/rtnetlink.h>
#include <linux/etherdevice.h>
#include <linux/module.h>

static struct wiphy *common_wiphy;

struct virt_wifi_wiphy_priv {
	struct delayed_work scan_result;
	struct cfg80211_scan_request *scan_request;
	bool being_deleted;
};

static struct ieee80211_channel channel_2ghz = {
	.band = NL80211_BAND_2GHZ,
	.center_freq = 2432,
	.hw_value = 2432,
	.max_power = 20,
};

static struct ieee80211_rate bitrates_2ghz[] = {
	{ .bitrate = 10 },
	{ .bitrate = 20 },
	{ .bitrate = 55 },
	{ .bitrate = 110 },
	{ .bitrate = 60 },
	{ .bitrate = 120 },
	{ .bitrate = 240 },
};

static struct ieee80211_supported_band band_2ghz = {
	.channels = &channel_2ghz,
	.bitrates = bitrates_2ghz,
	.band = NL80211_BAND_2GHZ,
	.n_channels = 1,
	.n_bitrates = ARRAY_SIZE(bitrates_2ghz),
	.ht_cap = {
		.ht_supported = true,
		.cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
		       IEEE80211_HT_CAP_GRN_FLD |
		       IEEE80211_HT_CAP_SGI_20 |
		       IEEE80211_HT_CAP_SGI_40 |
		       IEEE80211_HT_CAP_DSSSCCK40,
		.ampdu_factor = 0x3,
		.ampdu_density = 0x6,
		.mcs = {
			.rx_mask = {0xff, 0xff},
			.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
		},
	},
};

static struct ieee80211_channel channel_5ghz = {
	.band = NL80211_BAND_5GHZ,
	.center_freq = 5240,
	.hw_value = 5240,
	.max_power = 20,
};

static struct ieee80211_rate bitrates_5ghz[] = {
	{ .bitrate = 60 },
	{ .bitrate = 120 },
	{ .bitrate = 240 },
};

#define RX_MCS_MAP (IEEE80211_VHT_MCS_SUPPORT_0_9 << 0 | \
		    IEEE80211_VHT_MCS_SUPPORT_0_9 << 2 | \
		    IEEE80211_VHT_MCS_SUPPORT_0_9 << 4 | \
		    IEEE80211_VHT_MCS_SUPPORT_0_9 << 6 | \
		    IEEE80211_VHT_MCS_SUPPORT_0_9 << 8 | \
		    IEEE80211_VHT_MCS_SUPPORT_0_9 << 10 | \
		    IEEE80211_VHT_MCS_SUPPORT_0_9 << 12 | \
		    IEEE80211_VHT_MCS_SUPPORT_0_9 << 14)

#define TX_MCS_MAP (IEEE80211_VHT_MCS_SUPPORT_0_9 << 0 | \
		    IEEE80211_VHT_MCS_SUPPORT_0_9 << 2 | \
		    IEEE80211_VHT_MCS_SUPPORT_0_9 << 4 | \
		    IEEE80211_VHT_MCS_SUPPORT_0_9 << 6 | \
		    IEEE80211_VHT_MCS_SUPPORT_0_9 << 8 | \
		    IEEE80211_VHT_MCS_SUPPORT_0_9 << 10 | \
		    IEEE80211_VHT_MCS_SUPPORT_0_9 << 12 | \
		    IEEE80211_VHT_MCS_SUPPORT_0_9 << 14)

static struct ieee80211_supported_band band_5ghz = {
	.channels = &channel_5ghz,
	.bitrates = bitrates_5ghz,
	.band = NL80211_BAND_5GHZ,
	.n_channels = 1,
	.n_bitrates = ARRAY_SIZE(bitrates_5ghz),
	.ht_cap = {
		.ht_supported = true,
		.cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
		       IEEE80211_HT_CAP_GRN_FLD |
		       IEEE80211_HT_CAP_SGI_20 |
		       IEEE80211_HT_CAP_SGI_40 |
		       IEEE80211_HT_CAP_DSSSCCK40,
		.ampdu_factor = 0x3,
		.ampdu_density = 0x6,
		.mcs = {
			.rx_mask = {0xff, 0xff},
			.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
		},
	},
	.vht_cap = {
		.vht_supported = true,
		.cap = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 |
		       IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ |
		       IEEE80211_VHT_CAP_RXLDPC |
		       IEEE80211_VHT_CAP_SHORT_GI_80 |
		       IEEE80211_VHT_CAP_SHORT_GI_160 |
		       IEEE80211_VHT_CAP_TXSTBC |
		       IEEE80211_VHT_CAP_RXSTBC_1 |
		       IEEE80211_VHT_CAP_RXSTBC_2 |
		       IEEE80211_VHT_CAP_RXSTBC_3 |
		       IEEE80211_VHT_CAP_RXSTBC_4 |
		       IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK,
		.vht_mcs = {
			.rx_mcs_map = cpu_to_le16(RX_MCS_MAP),
			.tx_mcs_map = cpu_to_le16(TX_MCS_MAP),
		}
	},
};

/* Assigned at module init. Guaranteed locally-administered and unicast. */
static u8 fake_router_bssid[ETH_ALEN] __ro_after_init = {};

/* Called with the rtnl lock held. */
static int virt_wifi_scan(struct wiphy *wiphy,
			  struct cfg80211_scan_request *request)
{
	struct virt_wifi_wiphy_priv *priv = wiphy_priv(wiphy);

	wiphy_debug(wiphy, "scan\n");

	if (priv->scan_request || priv->being_deleted)
		return -EBUSY;

	priv->scan_request = request;
	schedule_delayed_work(&priv->scan_result, HZ * 2);

	return 0;
}

/* Acquires and releases the rdev BSS lock. */
static void virt_wifi_scan_result(struct work_struct *work)
{
	struct {
		u8 tag;
		u8 len;
		u8 ssid[8];
	} __packed ssid = {
		.tag = WLAN_EID_SSID, .len = 8, .ssid = "VirtWifi",
	};
	struct cfg80211_bss *informed_bss;
	struct virt_wifi_wiphy_priv *priv =
		container_of(work, struct virt_wifi_wiphy_priv,
			     scan_result.work);
	struct wiphy *wiphy = priv_to_wiphy(priv);
	struct cfg80211_scan_info scan_info = { .aborted = false };

	informed_bss = cfg80211_inform_bss(wiphy, &channel_5ghz,
					   CFG80211_BSS_FTYPE_PRESP,
					   fake_router_bssid,
					   ktime_get_boot_ns(),
					   WLAN_CAPABILITY_ESS, 0,
					   (void *)&ssid, sizeof(ssid),
					   DBM_TO_MBM(-50), GFP_KERNEL);
	cfg80211_put_bss(wiphy, informed_bss);

	/* Schedules work which acquires and releases the rtnl lock. */
	cfg80211_scan_done(priv->scan_request, &scan_info);
	priv->scan_request = NULL;
}

/* May acquire and release the rdev BSS lock. */
static void virt_wifi_cancel_scan(struct wiphy *wiphy)
{
	struct virt_wifi_wiphy_priv *priv = wiphy_priv(wiphy);

	cancel_delayed_work_sync(&priv->scan_result);
	/* Clean up dangling callbacks if necessary. */
	if (priv->scan_request) {
		struct cfg80211_scan_info scan_info = { .aborted = true };
		/* Schedules work which acquires and releases the rtnl lock. */
		cfg80211_scan_done(priv->scan_request, &scan_info);
		priv->scan_request = NULL;
	}
}

struct virt_wifi_netdev_priv {
	struct delayed_work connect;
	struct net_device *lowerdev;
	struct net_device *upperdev;
	u32 tx_packets;
	u32 tx_failed;
	u8 connect_requested_bss[ETH_ALEN];
	bool is_up;
	bool is_connected;
	bool being_deleted;
};

/* Called with the rtnl lock held. */
static int virt_wifi_connect(struct wiphy *wiphy, struct net_device *netdev,
			     struct cfg80211_connect_params *sme)
{
	struct virt_wifi_netdev_priv *priv = netdev_priv(netdev);
	bool could_schedule;

	if (priv->being_deleted || !priv->is_up)
		return -EBUSY;

	could_schedule = schedule_delayed_work(&priv->connect, HZ * 2);
	if (!could_schedule)
		return -EBUSY;

	if (sme->bssid)
		ether_addr_copy(priv->connect_requested_bss, sme->bssid);
	else
		eth_zero_addr(priv->connect_requested_bss);

	wiphy_debug(wiphy, "connect\n");

	return 0;
}

/* Acquires and releases the rdev event lock. */
static void virt_wifi_connect_complete(struct work_struct *work)
{
	struct virt_wifi_netdev_priv *priv =
		container_of(work, struct virt_wifi_netdev_priv, connect.work);
	u8 *requested_bss = priv->connect_requested_bss;
	bool has_addr = !is_zero_ether_addr(requested_bss);
	bool right_addr = ether_addr_equal(requested_bss, fake_router_bssid);
	u16 status = WLAN_STATUS_SUCCESS;

	if (!priv->is_up || (has_addr && !right_addr))
		status = WLAN_STATUS_UNSPECIFIED_FAILURE;
	else
		priv->is_connected = true;

	/* Schedules an event that acquires the rtnl lock. */
	cfg80211_connect_result(priv->upperdev, requested_bss, NULL, 0, NULL, 0,
				status, GFP_KERNEL);
	netif_carrier_on(priv->upperdev);
}

/* May acquire and release the rdev event lock. */
static void virt_wifi_cancel_connect(struct net_device *netdev)
{
	struct virt_wifi_netdev_priv *priv = netdev_priv(netdev);

	/* If there is work pending, clean up dangling callbacks. */
	if (cancel_delayed_work_sync(&priv->connect)) {
		/* Schedules an event that acquires the rtnl lock. */
		cfg80211_connect_result(priv->upperdev,
					priv->connect_requested_bss, NULL, 0,
					NULL, 0,
					WLAN_STATUS_UNSPECIFIED_FAILURE,
					GFP_KERNEL);
	}
}

/* Called with the rtnl lock held. Acquires the rdev event lock. */
static int virt_wifi_disconnect(struct wiphy *wiphy, struct net_device *netdev,
				u16 reason_code)
{
	struct virt_wifi_netdev_priv *priv = netdev_priv(netdev);

	if (priv->being_deleted)
		return -EBUSY;

	wiphy_debug(wiphy, "disconnect\n");
	virt_wifi_cancel_connect(netdev);

	cfg80211_disconnected(netdev, reason_code, NULL, 0, true, GFP_KERNEL);
	priv->is_connected = false;
	netif_carrier_off(netdev);

	return 0;
}

/* Called with the rtnl lock held. */
static int virt_wifi_get_station(struct wiphy *wiphy, struct net_device *dev,
				 const u8 *mac, struct station_info *sinfo)
{
	struct virt_wifi_netdev_priv *priv = netdev_priv(dev);

	wiphy_debug(wiphy, "get_station\n");

	if (!priv->is_connected || !ether_addr_equal(mac, fake_router_bssid))
		return -ENOENT;

	sinfo->filled = BIT_ULL(NL80211_STA_INFO_TX_PACKETS) |
		BIT_ULL(NL80211_STA_INFO_TX_FAILED) |
		BIT_ULL(NL80211_STA_INFO_SIGNAL) |
		BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
	sinfo->tx_packets = priv->tx_packets;
	sinfo->tx_failed = priv->tx_failed;
	/* For CFG80211_SIGNAL_TYPE_MBM, value is expressed in _dBm_ */
	sinfo->signal = -50;
	sinfo->txrate = (struct rate_info) {
		.legacy = 10, /* units are 100kbit/s */
	};
	return 0;
}

/* Called with the rtnl lock held. */
static int virt_wifi_dump_station(struct wiphy *wiphy, struct net_device *dev,
				  int idx, u8 *mac, struct station_info *sinfo)
{
	struct virt_wifi_netdev_priv *priv = netdev_priv(dev);

	wiphy_debug(wiphy, "dump_station\n");

	if (idx != 0 || !priv->is_connected)
		return -ENOENT;

	ether_addr_copy(mac, fake_router_bssid);
	return virt_wifi_get_station(wiphy, dev, fake_router_bssid, sinfo);
}

static const struct cfg80211_ops virt_wifi_cfg80211_ops = {
	.scan = virt_wifi_scan,

	.connect = virt_wifi_connect,
	.disconnect = virt_wifi_disconnect,

	.get_station = virt_wifi_get_station,
	.dump_station = virt_wifi_dump_station,
};

/* Acquires and releases the rtnl lock. */
static struct wiphy *virt_wifi_make_wiphy(void)
{
	struct wiphy *wiphy;
	struct virt_wifi_wiphy_priv *priv;
	int err;

	wiphy = wiphy_new(&virt_wifi_cfg80211_ops, sizeof(*priv));

	if (!wiphy)
		return NULL;

	wiphy->max_scan_ssids = 4;
	wiphy->max_scan_ie_len = 1000;
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

	wiphy->bands[NL80211_BAND_2GHZ] = &band_2ghz;
	wiphy->bands[NL80211_BAND_5GHZ] = &band_5ghz;
	wiphy->bands[NL80211_BAND_60GHZ] = NULL;

	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);

	priv = wiphy_priv(wiphy);
	priv->being_deleted = false;
	priv->scan_request = NULL;
	INIT_DELAYED_WORK(&priv->scan_result, virt_wifi_scan_result);

	err = wiphy_register(wiphy);
	if (err < 0) {
		wiphy_free(wiphy);
		return NULL;
	}

	return wiphy;
}

/* Acquires and releases the rtnl lock. */
static void virt_wifi_destroy_wiphy(struct wiphy *wiphy)
{
	struct virt_wifi_wiphy_priv *priv;

	WARN(!wiphy, "%s called with null wiphy", __func__);
	if (!wiphy)
		return;

	priv = wiphy_priv(wiphy);
	priv->being_deleted = true;
	virt_wifi_cancel_scan(wiphy);

	if (wiphy->registered)
		wiphy_unregister(wiphy);
	wiphy_free(wiphy);
}

/* Enters and exits a RCU-bh critical section. */
static netdev_tx_t virt_wifi_start_xmit(struct sk_buff *skb,
					struct net_device *dev)
{
	struct virt_wifi_netdev_priv *priv = netdev_priv(dev);

	priv->tx_packets++;
	if (!priv->is_connected) {
		priv->tx_failed++;
		return NET_XMIT_DROP;
	}

	skb->dev = priv->lowerdev;
	return dev_queue_xmit(skb);
}

/* Called with rtnl lock held. */
static int virt_wifi_net_device_open(struct net_device *dev)
{
	struct virt_wifi_netdev_priv *priv = netdev_priv(dev);

	priv->is_up = true;
	return 0;
}

/* Called with rtnl lock held. */
static int virt_wifi_net_device_stop(struct net_device *dev)
{
	struct virt_wifi_netdev_priv *n_priv = netdev_priv(dev);
	struct virt_wifi_wiphy_priv *w_priv;

	n_priv->is_up = false;

	if (!dev->ieee80211_ptr)
		return 0;
	w_priv = wiphy_priv(dev->ieee80211_ptr->wiphy);

	virt_wifi_cancel_scan(dev->ieee80211_ptr->wiphy);
	virt_wifi_cancel_connect(dev);
	netif_carrier_off(dev);

	return 0;
}

static const struct net_device_ops virt_wifi_ops = {
	.ndo_start_xmit = virt_wifi_start_xmit,
	.ndo_open = virt_wifi_net_device_open,
	.ndo_stop = virt_wifi_net_device_stop,
};

/* Invoked as part of rtnl lock release. */
static void virt_wifi_net_device_destructor(struct net_device *dev)
{
	/* Delayed past dellink to allow nl80211 to react to the device being
	 * deleted.
	 */
	kfree(dev->ieee80211_ptr);
	dev->ieee80211_ptr = NULL;
	free_netdev(dev);
}

/* No lock interaction. */
static void virt_wifi_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->netdev_ops = &virt_wifi_ops;
	dev->priv_destructor = virt_wifi_net_device_destructor;
}

/* Called in a RCU read critical section from netif_receive_skb */
static rx_handler_result_t virt_wifi_rx_handler(struct sk_buff **pskb)
{
	struct sk_buff *skb = *pskb;
	struct virt_wifi_netdev_priv *priv =
		rcu_dereference(skb->dev->rx_handler_data);

	if (!priv->is_connected)
		return RX_HANDLER_PASS;

	/* GFP_ATOMIC because this is a packet interrupt handler. */
	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb) {
		dev_err(&priv->upperdev->dev, "can't skb_share_check\n");
		return RX_HANDLER_CONSUMED;
	}

	*pskb = skb;
	skb->dev = priv->upperdev;
	skb->pkt_type = PACKET_HOST;
	return RX_HANDLER_ANOTHER;
}

/* Called with rtnl lock held. */
static int virt_wifi_newlink(struct net *src_net, struct net_device *dev,
			     struct nlattr *tb[], struct nlattr *data[],
			     struct netlink_ext_ack *extack)
{
	struct virt_wifi_netdev_priv *priv = netdev_priv(dev);
	int err;

	if (!tb[IFLA_LINK])
		return -EINVAL;

	netif_carrier_off(dev);

	priv->upperdev = dev;
	priv->lowerdev = __dev_get_by_index(src_net,
					    nla_get_u32(tb[IFLA_LINK]));

	if (!priv->lowerdev)
		return -ENODEV;
	if (!tb[IFLA_MTU])
		dev->mtu = priv->lowerdev->mtu;
	else if (dev->mtu > priv->lowerdev->mtu)
		return -EINVAL;

	err = netdev_rx_handler_register(priv->lowerdev, virt_wifi_rx_handler,
					 priv);
	if (err) {
		dev_err(&priv->lowerdev->dev,
			"can't netdev_rx_handler_register: %d\n", err);
		return err;
	}

	eth_hw_addr_inherit(dev, priv->lowerdev);
	netif_stacked_transfer_operstate(priv->lowerdev, dev);

	SET_NETDEV_DEV(dev, &priv->lowerdev->dev);
	dev->ieee80211_ptr = kzalloc(sizeof(*dev->ieee80211_ptr), GFP_KERNEL);

	if (!dev->ieee80211_ptr) {
		err = -ENOMEM;
		goto remove_handler;
	}

	dev->ieee80211_ptr->iftype = NL80211_IFTYPE_STATION;
	dev->ieee80211_ptr->wiphy = common_wiphy;

	err = register_netdevice(dev);
	if (err) {
		dev_err(&priv->lowerdev->dev, "can't register_netdevice: %d\n",
			err);
		goto free_wireless_dev;
	}

	err = netdev_upper_dev_link(priv->lowerdev, dev, extack);
	if (err) {
		dev_err(&priv->lowerdev->dev, "can't netdev_upper_dev_link: %d\n",
			err);
		goto unregister_netdev;
	}

	priv->being_deleted = false;
	priv->is_connected = false;
	priv->is_up = false;
	INIT_DELAYED_WORK(&priv->connect, virt_wifi_connect_complete);

	return 0;
unregister_netdev:
	unregister_netdevice(dev);
free_wireless_dev:
	kfree(dev->ieee80211_ptr);
	dev->ieee80211_ptr = NULL;
remove_handler:
	netdev_rx_handler_unregister(priv->lowerdev);

	return err;
}

/* Called with rtnl lock held. */
static void virt_wifi_dellink(struct net_device *dev,
			      struct list_head *head)
{
	struct virt_wifi_netdev_priv *priv = netdev_priv(dev);

	if (dev->ieee80211_ptr)
		virt_wifi_cancel_scan(dev->ieee80211_ptr->wiphy);

	priv->being_deleted = true;
	virt_wifi_cancel_connect(dev);
	netif_carrier_off(dev);

	netdev_rx_handler_unregister(priv->lowerdev);
	netdev_upper_dev_unlink(priv->lowerdev, dev);

	unregister_netdevice_queue(dev, head);

	/* Deleting the wiphy is handled in the module destructor. */
}

static struct rtnl_link_ops virt_wifi_link_ops = {
	.kind		= "virt_wifi",
	.setup		= virt_wifi_setup,
	.newlink	= virt_wifi_newlink,
	.dellink	= virt_wifi_dellink,
	.priv_size	= sizeof(struct virt_wifi_netdev_priv),
};

/* Acquires and releases the rtnl lock. */
static int __init virt_wifi_init_module(void)
{
	int err;

	/* Guaranteed to be locallly-administered and not multicast. */
	eth_random_addr(fake_router_bssid);

	common_wiphy = virt_wifi_make_wiphy();
	if (!common_wiphy)
		return -ENOMEM;

	err = rtnl_link_register(&virt_wifi_link_ops);
	if (err)
		virt_wifi_destroy_wiphy(common_wiphy);

	return err;
}

/* Acquires and releases the rtnl lock. */
static void __exit virt_wifi_cleanup_module(void)
{
	/* Will delete any devices that depend on the wiphy. */
	rtnl_link_unregister(&virt_wifi_link_ops);
	virt_wifi_destroy_wiphy(common_wiphy);
}

module_init(virt_wifi_init_module);
module_exit(virt_wifi_cleanup_module);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Cody Schuffelen <schuffelen@google.com>");
MODULE_DESCRIPTION("Driver for a wireless wrapper of ethernet devices");
MODULE_ALIAS_RTNL_LINK("virt_wifi");
