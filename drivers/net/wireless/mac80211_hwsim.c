/*
 * mac80211_hwsim - software simulator of 802.11 radio(s) for mac80211
 * Copyright (c) 2008, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * TODO:
 * - IBSS mode simulation (Beacon transmission with competition for "air time")
 * - IEEE 802.11a and 802.11n modes
 * - RX filtering based on filter configuration (data->rx_filter)
 */

#include <net/mac80211.h>
#include <net/ieee80211_radiotap.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/etherdevice.h>

MODULE_AUTHOR("Jouni Malinen");
MODULE_DESCRIPTION("Software simulator of 802.11 radio(s) for mac80211");
MODULE_LICENSE("GPL");

static int radios = 2;
module_param(radios, int, 0444);
MODULE_PARM_DESC(radios, "Number of simulated radios");


static struct class *hwsim_class;

static struct ieee80211_hw **hwsim_radios;
static int hwsim_radio_count;
static struct net_device *hwsim_mon; /* global monitor netdev */


static const struct ieee80211_channel hwsim_channels[] = {
	{ .center_freq = 2412 },
	{ .center_freq = 2417 },
	{ .center_freq = 2422 },
	{ .center_freq = 2427 },
	{ .center_freq = 2432 },
	{ .center_freq = 2437 },
	{ .center_freq = 2442 },
	{ .center_freq = 2447 },
	{ .center_freq = 2452 },
	{ .center_freq = 2457 },
	{ .center_freq = 2462 },
	{ .center_freq = 2467 },
	{ .center_freq = 2472 },
	{ .center_freq = 2484 },
};

static const struct ieee80211_rate hwsim_rates[] = {
	{ .bitrate = 10 },
	{ .bitrate = 20, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 55, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 110, .flags = IEEE80211_RATE_SHORT_PREAMBLE },
	{ .bitrate = 60 },
	{ .bitrate = 90 },
	{ .bitrate = 120 },
	{ .bitrate = 180 },
	{ .bitrate = 240 },
	{ .bitrate = 360 },
	{ .bitrate = 480 },
	{ .bitrate = 540 }
};

struct mac80211_hwsim_data {
	struct device *dev;
	struct ieee80211_supported_band band;
	struct ieee80211_channel channels[ARRAY_SIZE(hwsim_channels)];
	struct ieee80211_rate rates[ARRAY_SIZE(hwsim_rates)];

	struct ieee80211_channel *channel;
	int radio_enabled;
	unsigned long beacon_int; /* in jiffies unit */
	unsigned int rx_filter;
	int started;
	struct timer_list beacon_timer;
};


struct hwsim_radiotap_hdr {
	struct ieee80211_radiotap_header hdr;
	u8 rt_flags;
	u8 rt_rate;
	__le16 rt_channel;
	__le16 rt_chbitmask;
} __attribute__ ((packed));


static int hwsim_mon_xmit(struct sk_buff *skb, struct net_device *dev)
{
	/* TODO: allow packet injection */
	dev_kfree_skb(skb);
	return 0;
}


static void mac80211_hwsim_monitor_rx(struct ieee80211_hw *hw,
				      struct sk_buff *tx_skb)
{
	struct mac80211_hwsim_data *data = hw->priv;
	struct sk_buff *skb;
	struct hwsim_radiotap_hdr *hdr;
	u16 flags;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_skb);
	struct ieee80211_rate *txrate = ieee80211_get_tx_rate(hw, info);

	if (!netif_running(hwsim_mon))
		return;

	skb = skb_copy_expand(tx_skb, sizeof(*hdr), 0, GFP_ATOMIC);
	if (skb == NULL)
		return;

	hdr = (struct hwsim_radiotap_hdr *) skb_push(skb, sizeof(*hdr));
	hdr->hdr.it_version = PKTHDR_RADIOTAP_VERSION;
	hdr->hdr.it_pad = 0;
	hdr->hdr.it_len = cpu_to_le16(sizeof(*hdr));
	hdr->hdr.it_present = cpu_to_le32((1 << IEEE80211_RADIOTAP_FLAGS) |
					  (1 << IEEE80211_RADIOTAP_RATE) |
					  (1 << IEEE80211_RADIOTAP_CHANNEL));
	hdr->rt_flags = 0;
	hdr->rt_rate = txrate->bitrate / 5;
	hdr->rt_channel = cpu_to_le16(data->channel->center_freq);
	flags = IEEE80211_CHAN_2GHZ;
	if (txrate->flags & IEEE80211_RATE_ERP_G)
		flags |= IEEE80211_CHAN_OFDM;
	else
		flags |= IEEE80211_CHAN_CCK;
	hdr->rt_chbitmask = cpu_to_le16(flags);

	skb->dev = hwsim_mon;
	skb_set_mac_header(skb, 0);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);
	memset(skb->cb, 0, sizeof(skb->cb));
	netif_rx(skb);
}


static int mac80211_hwsim_tx_frame(struct ieee80211_hw *hw,
				   struct sk_buff *skb)
{
	struct mac80211_hwsim_data *data = hw->priv;
	int i, ack = 0;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_rx_status rx_status;

	memset(&rx_status, 0, sizeof(rx_status));
	/* TODO: set mactime */
	rx_status.freq = data->channel->center_freq;
	rx_status.band = data->channel->band;
	rx_status.rate_idx = info->tx_rate_idx;
	/* TODO: simulate signal strength (and optional packet drop) */

	/* Copy skb to all enabled radios that are on the current frequency */
	for (i = 0; i < hwsim_radio_count; i++) {
		struct mac80211_hwsim_data *data2;
		struct sk_buff *nskb;

		if (hwsim_radios[i] == NULL || hwsim_radios[i] == hw)
			continue;
		data2 = hwsim_radios[i]->priv;
		if (!data2->started || !data2->radio_enabled ||
		    data->channel->center_freq != data2->channel->center_freq)
			continue;

		nskb = skb_copy(skb, GFP_ATOMIC);
		if (nskb == NULL)
			continue;

		if (memcmp(hdr->addr1, hwsim_radios[i]->wiphy->perm_addr,
			   ETH_ALEN) == 0)
			ack = 1;
		ieee80211_rx_irqsafe(hwsim_radios[i], nskb, &rx_status);
	}

	return ack;
}


static int mac80211_hwsim_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct mac80211_hwsim_data *data = hw->priv;
	int ack;
	struct ieee80211_tx_info *txi;

	mac80211_hwsim_monitor_rx(hw, skb);

	if (skb->len < 10) {
		/* Should not happen; just a sanity check for addr1 use */
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	if (!data->radio_enabled) {
		printk(KERN_DEBUG "%s: dropped TX frame since radio "
		       "disabled\n", wiphy_name(hw->wiphy));
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	ack = mac80211_hwsim_tx_frame(hw, skb);

	txi = IEEE80211_SKB_CB(skb);
	memset(&txi->status, 0, sizeof(txi->status));
	if (!(txi->flags & IEEE80211_TX_CTL_NO_ACK)) {
		if (ack)
			txi->flags |= IEEE80211_TX_STAT_ACK;
		else
			txi->status.excessive_retries = 1;
	}
	ieee80211_tx_status_irqsafe(hw, skb);
	return NETDEV_TX_OK;
}


static int mac80211_hwsim_start(struct ieee80211_hw *hw)
{
	struct mac80211_hwsim_data *data = hw->priv;
	printk(KERN_DEBUG "%s:%s\n", wiphy_name(hw->wiphy), __func__);
	data->started = 1;
	return 0;
}


static void mac80211_hwsim_stop(struct ieee80211_hw *hw)
{
	struct mac80211_hwsim_data *data = hw->priv;
	data->started = 0;
	printk(KERN_DEBUG "%s:%s\n", wiphy_name(hw->wiphy), __func__);
}


static int mac80211_hwsim_add_interface(struct ieee80211_hw *hw,
					struct ieee80211_if_init_conf *conf)
{
	DECLARE_MAC_BUF(mac);
	printk(KERN_DEBUG "%s:%s (type=%d mac_addr=%s)\n",
	       wiphy_name(hw->wiphy), __func__, conf->type,
	       print_mac(mac, conf->mac_addr));
	return 0;
}


static void mac80211_hwsim_remove_interface(
	struct ieee80211_hw *hw, struct ieee80211_if_init_conf *conf)
{
	DECLARE_MAC_BUF(mac);
	printk(KERN_DEBUG "%s:%s (type=%d mac_addr=%s)\n",
	       wiphy_name(hw->wiphy), __func__, conf->type,
	       print_mac(mac, conf->mac_addr));
}


static void mac80211_hwsim_beacon_tx(void *arg, u8 *mac,
				     struct ieee80211_vif *vif)
{
	struct ieee80211_hw *hw = arg;
	struct sk_buff *skb;
	struct ieee80211_tx_info *info;

	if (vif->type != NL80211_IFTYPE_AP)
		return;

	skb = ieee80211_beacon_get(hw, vif);
	if (skb == NULL)
		return;
	info = IEEE80211_SKB_CB(skb);

	mac80211_hwsim_monitor_rx(hw, skb);
	mac80211_hwsim_tx_frame(hw, skb);
	dev_kfree_skb(skb);
}


static void mac80211_hwsim_beacon(unsigned long arg)
{
	struct ieee80211_hw *hw = (struct ieee80211_hw *) arg;
	struct mac80211_hwsim_data *data = hw->priv;

	if (!data->started || !data->radio_enabled)
		return;

	ieee80211_iterate_active_interfaces_atomic(
		hw, mac80211_hwsim_beacon_tx, hw);

	data->beacon_timer.expires = jiffies + data->beacon_int;
	add_timer(&data->beacon_timer);
}


static int mac80211_hwsim_config(struct ieee80211_hw *hw,
				 struct ieee80211_conf *conf)
{
	struct mac80211_hwsim_data *data = hw->priv;

	printk(KERN_DEBUG "%s:%s (freq=%d radio_enabled=%d beacon_int=%d)\n",
	       wiphy_name(hw->wiphy), __func__,
	       conf->channel->center_freq, conf->radio_enabled,
	       conf->beacon_int);

	data->channel = conf->channel;
	data->radio_enabled = conf->radio_enabled;
	data->beacon_int = 1024 * conf->beacon_int / 1000 * HZ / 1000;
	if (data->beacon_int < 1)
		data->beacon_int = 1;

	if (!data->started || !data->radio_enabled)
		del_timer(&data->beacon_timer);
	else
		mod_timer(&data->beacon_timer, jiffies + data->beacon_int);

	return 0;
}


static void mac80211_hwsim_configure_filter(struct ieee80211_hw *hw,
					    unsigned int changed_flags,
					    unsigned int *total_flags,
					    int mc_count,
					    struct dev_addr_list *mc_list)
{
	struct mac80211_hwsim_data *data = hw->priv;

	printk(KERN_DEBUG "%s:%s\n", wiphy_name(hw->wiphy), __func__);

	data->rx_filter = 0;
	if (*total_flags & FIF_PROMISC_IN_BSS)
		data->rx_filter |= FIF_PROMISC_IN_BSS;
	if (*total_flags & FIF_ALLMULTI)
		data->rx_filter |= FIF_ALLMULTI;

	*total_flags = data->rx_filter;
}



static const struct ieee80211_ops mac80211_hwsim_ops =
{
	.tx = mac80211_hwsim_tx,
	.start = mac80211_hwsim_start,
	.stop = mac80211_hwsim_stop,
	.add_interface = mac80211_hwsim_add_interface,
	.remove_interface = mac80211_hwsim_remove_interface,
	.config = mac80211_hwsim_config,
	.configure_filter = mac80211_hwsim_configure_filter,
};


static void mac80211_hwsim_free(void)
{
	int i;

	for (i = 0; i < hwsim_radio_count; i++) {
		if (hwsim_radios[i]) {
			struct mac80211_hwsim_data *data;
			data = hwsim_radios[i]->priv;
			ieee80211_unregister_hw(hwsim_radios[i]);
			device_unregister(data->dev);
			ieee80211_free_hw(hwsim_radios[i]);
		}
	}
	kfree(hwsim_radios);
	class_destroy(hwsim_class);
}


static struct device_driver mac80211_hwsim_driver = {
	.name = "mac80211_hwsim"
};


static void hwsim_mon_setup(struct net_device *dev)
{
	dev->hard_start_xmit = hwsim_mon_xmit;
	dev->destructor = free_netdev;
	ether_setup(dev);
	dev->tx_queue_len = 0;
	dev->type = ARPHRD_IEEE80211_RADIOTAP;
	memset(dev->dev_addr, 0, ETH_ALEN);
	dev->dev_addr[0] = 0x12;
}


static int __init init_mac80211_hwsim(void)
{
	int i, err = 0;
	u8 addr[ETH_ALEN];
	struct mac80211_hwsim_data *data;
	struct ieee80211_hw *hw;
	DECLARE_MAC_BUF(mac);

	if (radios < 1 || radios > 65535)
		return -EINVAL;

	hwsim_radio_count = radios;
	hwsim_radios = kcalloc(hwsim_radio_count,
			       sizeof(struct ieee80211_hw *), GFP_KERNEL);
	if (hwsim_radios == NULL)
		return -ENOMEM;

	hwsim_class = class_create(THIS_MODULE, "mac80211_hwsim");
	if (IS_ERR(hwsim_class)) {
		kfree(hwsim_radios);
		return PTR_ERR(hwsim_class);
	}

	memset(addr, 0, ETH_ALEN);
	addr[0] = 0x02;

	for (i = 0; i < hwsim_radio_count; i++) {
		printk(KERN_DEBUG "mac80211_hwsim: Initializing radio %d\n",
		       i);
		hw = ieee80211_alloc_hw(sizeof(*data), &mac80211_hwsim_ops);
		if (hw == NULL) {
			printk(KERN_DEBUG "mac80211_hwsim: ieee80211_alloc_hw "
			       "failed\n");
			err = -ENOMEM;
			goto failed;
		}
		hwsim_radios[i] = hw;

		data = hw->priv;
		data->dev = device_create_drvdata(hwsim_class, NULL, 0, hw,
						"hwsim%d", i);
		if (IS_ERR(data->dev)) {
			printk(KERN_DEBUG
			       "mac80211_hwsim: device_create_drvdata "
			       "failed (%ld)\n", PTR_ERR(data->dev));
			err = -ENOMEM;
			goto failed_drvdata;
		}
		data->dev->driver = &mac80211_hwsim_driver;

		SET_IEEE80211_DEV(hw, data->dev);
		addr[3] = i >> 8;
		addr[4] = i;
		SET_IEEE80211_PERM_ADDR(hw, addr);

		hw->channel_change_time = 1;
		hw->queues = 4;
		hw->wiphy->interface_modes =
			BIT(NL80211_IFTYPE_STATION) |
			BIT(NL80211_IFTYPE_AP);
		hw->ampdu_queues = 1;

		memcpy(data->channels, hwsim_channels, sizeof(hwsim_channels));
		memcpy(data->rates, hwsim_rates, sizeof(hwsim_rates));
		data->band.channels = data->channels;
		data->band.n_channels = ARRAY_SIZE(hwsim_channels);
		data->band.bitrates = data->rates;
		data->band.n_bitrates = ARRAY_SIZE(hwsim_rates);
		data->band.ht_info.ht_supported = 1;
		data->band.ht_info.cap = IEEE80211_HT_CAP_SUP_WIDTH |
			IEEE80211_HT_CAP_GRN_FLD |
			IEEE80211_HT_CAP_SGI_40 |
			IEEE80211_HT_CAP_DSSSCCK40;
		data->band.ht_info.ampdu_factor = 0x3;
		data->band.ht_info.ampdu_density = 0x6;
		memset(data->band.ht_info.supp_mcs_set, 0,
		       sizeof(data->band.ht_info.supp_mcs_set));
		data->band.ht_info.supp_mcs_set[0] = 0xff;
		data->band.ht_info.supp_mcs_set[1] = 0xff;
		data->band.ht_info.supp_mcs_set[12] =
			IEEE80211_HT_CAP_MCS_TX_DEFINED;
		hw->wiphy->bands[IEEE80211_BAND_2GHZ] = &data->band;

		err = ieee80211_register_hw(hw);
		if (err < 0) {
			printk(KERN_DEBUG "mac80211_hwsim: "
			       "ieee80211_register_hw failed (%d)\n", err);
			goto failed_hw;
		}

		printk(KERN_DEBUG "%s: hwaddr %s registered\n",
		       wiphy_name(hw->wiphy),
		       print_mac(mac, hw->wiphy->perm_addr));

		setup_timer(&data->beacon_timer, mac80211_hwsim_beacon,
			    (unsigned long) hw);
	}

	hwsim_mon = alloc_netdev(0, "hwsim%d", hwsim_mon_setup);
	if (hwsim_mon == NULL)
		goto failed;

	rtnl_lock();

	err = dev_alloc_name(hwsim_mon, hwsim_mon->name);
	if (err < 0)
		goto failed_mon;


	err = register_netdevice(hwsim_mon);
	if (err < 0)
		goto failed_mon;

	rtnl_unlock();

	return 0;

failed_mon:
	rtnl_unlock();
	free_netdev(hwsim_mon);
	mac80211_hwsim_free();
	return err;

failed_hw:
	device_unregister(data->dev);
failed_drvdata:
	ieee80211_free_hw(hw);
	hwsim_radios[i] = NULL;
failed:
	mac80211_hwsim_free();
	return err;
}


static void __exit exit_mac80211_hwsim(void)
{
	printk(KERN_DEBUG "mac80211_hwsim: unregister %d radios\n",
	       hwsim_radio_count);

	unregister_netdev(hwsim_mon);
	mac80211_hwsim_free();
}


module_init(init_mac80211_hwsim);
module_exit(exit_mac80211_hwsim);
