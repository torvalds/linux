/*
 * Copyright (c) 2010 - 2012 Espressif System.
 */
#if 0
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

static struct ieee80211_channel esp_2ghz_channels[] = {
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

static int esp_cfg80211_change_iface(struct wiphy *wiphy,
                                     struct net_device *ndev,
                                     enum nl80211_iftype type, u32 *flags,
                                     struct vif_params *params)
{
        struct esp_pub *epub = wdev_priv(dev->ieee80211_ptr);
        struct wireless_dev *wdev = epub->wdev;


        /* only support STA mode for now */
        if (type != NL80211_IFTYPE_STATION)
                return -EOPNOTSUPP;
}

wdev->iftype = type;

return 0;
}

static int esp_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
                             struct cfg80211_scan_request *request)
{
        struct esp_pub *epub = wdev_priv(dev->ieee80211_ptr);
        int ret = 0;

        if (!esp_ready(ar))
                return -EIO;

        if (request->n_ssids && request->ssids[0].ssid_len) {
                u8 i;

                if (request->n_ssids > (MAX_PROBED_SSID_INDEX - 1))
                        request->n_ssids = MAX_PROBED_SSID_INDEX - 1;

                for (i = 0; i < request->n_ssids; i++)
                        esp_wl_probedssid_cmd(epub->wl, i + 1,
                                              SPECIFIC_SSID_FLAG,
                                              request->ssids[i].ssid_len,
                                              request->ssids[i].ssid);
        }

        if (esp_wl_startscan_cmd(epub->wl, WL_LONG_SCAN, 0,
                                 false, 0, 0, 0, NULL) != 0) {
                esp_dbg(ESP_DBG_ERROR, "wl_startscan_cmd failed\n");
                ret = -EIO;
        }

        epub->wl->scan_req = request;

        return ret;
}

static struct cfg80211_ops esp_cfg80211_ops = {
        .change_virtual_intf = esp_cfg80211_change_iface,
        .scan = esp_cfg80211_scan,
        .connect = esp_cfg80211_connect,
        .disconnect = esp_cfg80211_disconnect,
        .add_key = esp_cfg80211_add_key,
        .get_key = esp_cfg80211_get_key,
        .del_key = esp_cfg80211_del_key,
        .set_default_key = esp_cfg80211_set_default_key,
        .set_wiphy_params = esp_cfg80211_set_wiphy_params,
        .set_tx_power = esp_cfg80211_set_txpower,
        .get_tx_power = esp_cfg80211_get_txpower,
        .set_power_mgmt = esp_cfg80211_set_power_mgmt,
        .join_ibss = esp_cfg80211_join_ibss,
        .leave_ibss = esp_cfg80211_leave_ibss,
        .get_station = esp_get_station,
        .set_pmksa = esp_set_pmksa,
        .del_pmksa = esp_del_pmksa,
        .flush_pmksa = esp_flush_pmksa,
};

static struct ieee80211_ops esp_ieee80211_ops = {
}

static struct cfg80211_ops esp_cfg80211_ops = {0};

static struct ieee80211_rate esp_g_rates[] = {
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

#define esp_g_rates_size 12
static struct ieee80211_supported_band esp_band_2ghz = {
        .n_channels = ARRAY_SIZE(esp_2ghz_channels),
        .channels = esp_2ghz_channels,
        .n_bitrates = esp_g_rates_size,
        .bitrates = esp_g_rates,
};

static const u32 cipher_suites[] = {
        WLAN_CIPHER_SUITE_WEP40,
        WLAN_CIPHER_SUITE_WEP104,
        WLAN_CIPHER_SUITE_TKIP,
        WLAN_CIPHER_SUITE_CCMP,
};

static struct wireless_dev *
esp_cfg80211_init(struct device *dev) {
        int ret = 0;
        struct wireless_dev *wdev;

        wdev = kzalloc(sizeof(struct wireless_dev), GFP_KERNEL);

        if (!wdev) {
                esp_dbg(ESP_DBG_ERROR, "couldn't allocate wireless device\n");
                return NULL;
        }

        wdev->wiphy = wiphy_new(&esp_cfg80211_ops, sizeof(struct esp_pub));

        if (!wdev->wiphy) {
                esp_dbg(ESP_DBG_ERROR, "couldn't allocate wiphy device\n");
                kfree(wdev);
                return NULL;
        }

        set_wiphy_dev(wdev->wiphy, dev);

        wdev->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION);
        wdev->wiphy->max_scan_ssids = MAX_PROBED_SSID_INDEX;
        wdev->wiphy->bands[IEEE80211_BAND_2GHZ] = &esp_band_2ghz;
        //wdev->wiphy->bands[IEEE80211_BAND_5GHZ] = &esp_band_5ghz;
        wdev->wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

        wdev->wiphy->cipher_suites = cipher_suites;
        wdev->wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites);

        ret = wiphy_register(wdev->wiphy);

        if (ret < 0) {
                esp_dbg(ESP_DBG_ERROR, "couldn't register wiphy device\n");
                wiphy_free(wdev->wiphy);
                kfree(wdev);
                return NULL;
        }

        return wdev;
}

static void
esp_cfg80211_descory(struct esp_pub *pub)
{
        return;
}

static int esp_open(struct net_device *dev)
{
        return 0;
}

static struct net_device_stats *
esp_get_stats(struct net_device *dev) {
        struct net_device_stats *stats = NULL;
        return stats;
}

static int esp_close(struct net_device *dev)
{
        return 0;
}


static int esp_data_tx(struct sk_buff *skb, struct net_device *dev)
{
        return 0;
}

static struct net_device_ops esp_netdev_ops = {
        .ndo_open               = esp_open,
        .ndo_stop               = esp_close,
        .ndo_start_xmit         = esp_data_tx,
        .ndo_get_stats          = esp_get_stats,
};

static inline void
esp_init_netdev(struct net_device *dev)
{
        dev->netdev_ops = &esp_netdev_ops;
        dev->watchdog_timeo = 10;

        dev->needed_headroom = ETH_HLEN + sizeof(struct llc_snap_hdr) + SIP_HDR_LEN;

        return;
}

static void
esp_disconnect(struct esp_pub *epub)
{
        return;
}

static void
esp_disconnect_timeout_handler(unsigned long ptr)
{
        struct net_device *netdev = (struct net_device *)ptr;
        struct esp_pub *epub = wdev_priv(netdev->ieee80211_ptr);

        //esp_init_profile(epub);
        esp_disconnect(epub);
}

struct esp_pub *
esp_pub_alloc_cfg80211(struct device *dev) {
        struct net_device *netdev;
        struct wireless_dev *wdev;
        struct esp_pub *epub;
        struct esp_wl *wl;

        wdev = esp_cfg80211_init(dev);

        if (wdev == NULL) {
                esp_dbg(ESP_DBG_ERROR, "%s: cfg80211_init failed \n", __func__);
                return NULL;
        }

        epub = wdev_priv(wdev);
        epub->dev = dev;
        epub->wdev = wdev;
        wdev->iftype = NL80211_IFTYPE_STATION;

        /* Still register ethernet device */
        netdev = alloc_netdev(0, "wlan%d", ether_setup);

        if (!netdev) {
                esp_dbg(ESP_DBG_ERROR, "%s: alloc_netdev failed \n", __func__);
                esp_cfg80211_descory(epub);
                return NULL;
        }

        netdev->ieee80211_ptr = wdev;
        SET_NETDEV_DEV(netdev, wiphy_dev(wdev->wiphy));
        wdev->netdev = netdev;

        esp_init_netdev(netdev);

        epub->net_dev = netdev;

        //spin_lock_init(&epub->lock);

        wl = &epub->wl;
        //esp_init_wl(wl);
        init_waitqueue_head(&epub->ev_waitq);
        //sema_init(epub->sem, 1);

        INIT_LIST_HEAD(&wl->amsdu_rx_buffer_queue);

        setup_timer(&wl->disconnect_timer, esp_disconnect_timeout_handler,
                    (unsigned long) netdev);

        return epub;
}

#endif
