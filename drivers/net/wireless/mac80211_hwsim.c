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

#include <linux/list.h>
#include <linux/spinlock.h>
#include <net/mac80211.h>
#include <net/ieee80211_radiotap.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/etherdevice.h>
#include <linux/debugfs.h>

MODULE_AUTHOR("Jouni Malinen");
MODULE_DESCRIPTION("Software simulator of 802.11 radio(s) for mac80211");
MODULE_LICENSE("GPL");

static int radios = 2;
module_param(radios, int, 0444);
MODULE_PARM_DESC(radios, "Number of simulated radios");

struct hwsim_vif_priv {
	u32 magic;
	u8 bssid[ETH_ALEN];
	bool assoc;
	u16 aid;
};

#define HWSIM_VIF_MAGIC	0x69537748

static inline void hwsim_check_magic(struct ieee80211_vif *vif)
{
	struct hwsim_vif_priv *vp = (void *)vif->drv_priv;
	WARN_ON(vp->magic != HWSIM_VIF_MAGIC);
}

static inline void hwsim_set_magic(struct ieee80211_vif *vif)
{
	struct hwsim_vif_priv *vp = (void *)vif->drv_priv;
	vp->magic = HWSIM_VIF_MAGIC;
}

static inline void hwsim_clear_magic(struct ieee80211_vif *vif)
{
	struct hwsim_vif_priv *vp = (void *)vif->drv_priv;
	vp->magic = 0;
}

struct hwsim_sta_priv {
	u32 magic;
};

#define HWSIM_STA_MAGIC	0x6d537748

static inline void hwsim_check_sta_magic(struct ieee80211_sta *sta)
{
	struct hwsim_sta_priv *sp = (void *)sta->drv_priv;
	WARN_ON(sp->magic != HWSIM_STA_MAGIC);
}

static inline void hwsim_set_sta_magic(struct ieee80211_sta *sta)
{
	struct hwsim_sta_priv *sp = (void *)sta->drv_priv;
	sp->magic = HWSIM_STA_MAGIC;
}

static inline void hwsim_clear_sta_magic(struct ieee80211_sta *sta)
{
	struct hwsim_sta_priv *sp = (void *)sta->drv_priv;
	sp->magic = 0;
}

static struct class *hwsim_class;

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

static spinlock_t hwsim_radio_lock;
static struct list_head hwsim_radios;

struct mac80211_hwsim_data {
	struct list_head list;
	struct ieee80211_hw *hw;
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
	enum ps_mode {
		PS_DISABLED, PS_ENABLED, PS_AUTO_POLL, PS_MANUAL_POLL
	} ps;
	bool ps_poll_pending;
	struct dentry *debugfs;
	struct dentry *debugfs_ps;
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


static bool hwsim_ps_rx_ok(struct mac80211_hwsim_data *data,
			   struct sk_buff *skb)
{
	switch (data->ps) {
	case PS_DISABLED:
		return true;
	case PS_ENABLED:
		return false;
	case PS_AUTO_POLL:
		/* TODO: accept (some) Beacons by default and other frames only
		 * if pending PS-Poll has been sent */
		return true;
	case PS_MANUAL_POLL:
		/* Allow unicast frames to own address if there is a pending
		 * PS-Poll */
		if (data->ps_poll_pending &&
		    memcmp(data->hw->wiphy->perm_addr, skb->data + 4,
			   ETH_ALEN) == 0) {
			data->ps_poll_pending = false;
			return true;
		}
		return false;
	}

	return true;
}


static bool mac80211_hwsim_tx_frame(struct ieee80211_hw *hw,
				    struct sk_buff *skb)
{
	struct mac80211_hwsim_data *data = hw->priv, *data2;
	bool ack = false;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_rx_status rx_status;

	memset(&rx_status, 0, sizeof(rx_status));
	/* TODO: set mactime */
	rx_status.freq = data->channel->center_freq;
	rx_status.band = data->channel->band;
	rx_status.rate_idx = info->control.rates[0].idx;
	/* TODO: simulate signal strength (and optional packet drop) */

	if (data->ps != PS_DISABLED)
		hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PM);

	/* Copy skb to all enabled radios that are on the current frequency */
	spin_lock(&hwsim_radio_lock);
	list_for_each_entry(data2, &hwsim_radios, list) {
		struct sk_buff *nskb;

		if (data == data2)
			continue;

		if (!data2->started || !data2->radio_enabled ||
		    !hwsim_ps_rx_ok(data2, skb) ||
		    data->channel->center_freq != data2->channel->center_freq)
			continue;

		nskb = skb_copy(skb, GFP_ATOMIC);
		if (nskb == NULL)
			continue;

		if (memcmp(hdr->addr1, data2->hw->wiphy->perm_addr,
			   ETH_ALEN) == 0)
			ack = true;
		ieee80211_rx_irqsafe(data2->hw, nskb, &rx_status);
	}
	spin_unlock(&hwsim_radio_lock);

	return ack;
}


static int mac80211_hwsim_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct mac80211_hwsim_data *data = hw->priv;
	bool ack;
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

	if (txi->control.vif)
		hwsim_check_magic(txi->control.vif);
	if (txi->control.sta)
		hwsim_check_sta_magic(txi->control.sta);

	ieee80211_tx_info_clear_status(txi);
	if (!(txi->flags & IEEE80211_TX_CTL_NO_ACK) && ack)
		txi->flags |= IEEE80211_TX_STAT_ACK;
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
	del_timer(&data->beacon_timer);
	printk(KERN_DEBUG "%s:%s\n", wiphy_name(hw->wiphy), __func__);
}


static int mac80211_hwsim_add_interface(struct ieee80211_hw *hw,
					struct ieee80211_if_init_conf *conf)
{
	printk(KERN_DEBUG "%s:%s (type=%d mac_addr=%pM)\n",
	       wiphy_name(hw->wiphy), __func__, conf->type,
	       conf->mac_addr);
	hwsim_set_magic(conf->vif);
	return 0;
}


static void mac80211_hwsim_remove_interface(
	struct ieee80211_hw *hw, struct ieee80211_if_init_conf *conf)
{
	printk(KERN_DEBUG "%s:%s (type=%d mac_addr=%pM)\n",
	       wiphy_name(hw->wiphy), __func__, conf->type,
	       conf->mac_addr);
	hwsim_check_magic(conf->vif);
	hwsim_clear_magic(conf->vif);
}


static void mac80211_hwsim_beacon_tx(void *arg, u8 *mac,
				     struct ieee80211_vif *vif)
{
	struct ieee80211_hw *hw = arg;
	struct sk_buff *skb;
	struct ieee80211_tx_info *info;

	hwsim_check_magic(vif);

	if (vif->type != NL80211_IFTYPE_AP &&
	    vif->type != NL80211_IFTYPE_MESH_POINT)
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


static int mac80211_hwsim_config(struct ieee80211_hw *hw, u32 changed)
{
	struct mac80211_hwsim_data *data = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;

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

static int mac80211_hwsim_config_interface(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif,
					   struct ieee80211_if_conf *conf)
{
	struct hwsim_vif_priv *vp = (void *)vif->drv_priv;

	hwsim_check_magic(vif);
	if (conf->changed & IEEE80211_IFCC_BSSID) {
		DECLARE_MAC_BUF(mac);
		printk(KERN_DEBUG "%s:%s: BSSID changed: %pM\n",
		       wiphy_name(hw->wiphy), __func__,
		       conf->bssid);
		memcpy(vp->bssid, conf->bssid, ETH_ALEN);
	}
	return 0;
}

static void mac80211_hwsim_bss_info_changed(struct ieee80211_hw *hw,
					    struct ieee80211_vif *vif,
					    struct ieee80211_bss_conf *info,
					    u32 changed)
{
	struct hwsim_vif_priv *vp = (void *)vif->drv_priv;

	hwsim_check_magic(vif);

	printk(KERN_DEBUG "%s:%s(changed=0x%x)\n",
	       wiphy_name(hw->wiphy), __func__, changed);

	if (changed & BSS_CHANGED_ASSOC) {
		printk(KERN_DEBUG "  %s: ASSOC: assoc=%d aid=%d\n",
		       wiphy_name(hw->wiphy), info->assoc, info->aid);
		vp->assoc = info->assoc;
		vp->aid = info->aid;
	}

	if (changed & BSS_CHANGED_ERP_CTS_PROT) {
		printk(KERN_DEBUG "  %s: ERP_CTS_PROT: %d\n",
		       wiphy_name(hw->wiphy), info->use_cts_prot);
	}

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		printk(KERN_DEBUG "  %s: ERP_PREAMBLE: %d\n",
		       wiphy_name(hw->wiphy), info->use_short_preamble);
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		printk(KERN_DEBUG "  %s: ERP_SLOT: %d\n",
		       wiphy_name(hw->wiphy), info->use_short_slot);
	}

	if (changed & BSS_CHANGED_HT) {
		printk(KERN_DEBUG "  %s: HT: op_mode=0x%x\n",
		       wiphy_name(hw->wiphy),
		       info->ht.operation_mode);
	}

	if (changed & BSS_CHANGED_BASIC_RATES) {
		printk(KERN_DEBUG "  %s: BASIC_RATES: 0x%llx\n",
		       wiphy_name(hw->wiphy),
		       (unsigned long long) info->basic_rates);
	}
}

static void mac80211_hwsim_sta_notify(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      enum sta_notify_cmd cmd,
				      struct ieee80211_sta *sta)
{
	hwsim_check_magic(vif);
	switch (cmd) {
	case STA_NOTIFY_ADD:
		hwsim_set_sta_magic(sta);
		break;
	case STA_NOTIFY_REMOVE:
		hwsim_clear_sta_magic(sta);
		break;
	case STA_NOTIFY_SLEEP:
	case STA_NOTIFY_AWAKE:
		/* TODO: make good use of these flags */
		break;
	}
}

static int mac80211_hwsim_set_tim(struct ieee80211_hw *hw,
				  struct ieee80211_sta *sta,
				  bool set)
{
	hwsim_check_sta_magic(sta);
	return 0;
}

static int mac80211_hwsim_conf_tx(
	struct ieee80211_hw *hw, u16 queue,
	const struct ieee80211_tx_queue_params *params)
{
	printk(KERN_DEBUG "%s:%s (queue=%d txop=%d cw_min=%d cw_max=%d "
	       "aifs=%d)\n",
	       wiphy_name(hw->wiphy), __func__, queue,
	       params->txop, params->cw_min, params->cw_max, params->aifs);
	return 0;
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
	.config_interface = mac80211_hwsim_config_interface,
	.bss_info_changed = mac80211_hwsim_bss_info_changed,
	.sta_notify = mac80211_hwsim_sta_notify,
	.set_tim = mac80211_hwsim_set_tim,
	.conf_tx = mac80211_hwsim_conf_tx,
};


static void mac80211_hwsim_free(void)
{
	struct list_head tmplist, *i, *tmp;
	struct mac80211_hwsim_data *data;

	INIT_LIST_HEAD(&tmplist);

	spin_lock_bh(&hwsim_radio_lock);
	list_for_each_safe(i, tmp, &hwsim_radios)
		list_move(i, &tmplist);
	spin_unlock_bh(&hwsim_radio_lock);

	list_for_each_entry(data, &tmplist, list) {
		debugfs_remove(data->debugfs_ps);
		debugfs_remove(data->debugfs);
		ieee80211_unregister_hw(data->hw);
		device_unregister(data->dev);
		ieee80211_free_hw(data->hw);
	}
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


static void hwsim_send_ps_poll(void *dat, u8 *mac, struct ieee80211_vif *vif)
{
	struct mac80211_hwsim_data *data = dat;
	struct hwsim_vif_priv *vp = (void *)vif->drv_priv;
	DECLARE_MAC_BUF(buf);
	struct sk_buff *skb;
	struct ieee80211_pspoll *pspoll;

	if (!vp->assoc)
		return;

	printk(KERN_DEBUG "%s:%s: send PS-Poll to %pM for aid %d\n",
	       wiphy_name(data->hw->wiphy), __func__, vp->bssid, vp->aid);

	skb = dev_alloc_skb(sizeof(*pspoll));
	if (!skb)
		return;
	pspoll = (void *) skb_put(skb, sizeof(*pspoll));
	pspoll->frame_control = cpu_to_le16(IEEE80211_FTYPE_CTL |
					    IEEE80211_STYPE_PSPOLL |
					    IEEE80211_FCTL_PM);
	pspoll->aid = cpu_to_le16(0xc000 | vp->aid);
	memcpy(pspoll->bssid, vp->bssid, ETH_ALEN);
	memcpy(pspoll->ta, mac, ETH_ALEN);
	if (data->radio_enabled &&
	    !mac80211_hwsim_tx_frame(data->hw, skb))
		printk(KERN_DEBUG "%s: PS-Poll frame not ack'ed\n", __func__);
	dev_kfree_skb(skb);
}


static void hwsim_send_nullfunc(struct mac80211_hwsim_data *data, u8 *mac,
				struct ieee80211_vif *vif, int ps)
{
	struct hwsim_vif_priv *vp = (void *)vif->drv_priv;
	DECLARE_MAC_BUF(buf);
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;

	if (!vp->assoc)
		return;

	printk(KERN_DEBUG "%s:%s: send data::nullfunc to %pM ps=%d\n",
	       wiphy_name(data->hw->wiphy), __func__, vp->bssid, ps);

	skb = dev_alloc_skb(sizeof(*hdr));
	if (!skb)
		return;
	hdr = (void *) skb_put(skb, sizeof(*hdr) - ETH_ALEN);
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
					 IEEE80211_STYPE_NULLFUNC |
					 (ps ? IEEE80211_FCTL_PM : 0));
	hdr->duration_id = cpu_to_le16(0);
	memcpy(hdr->addr1, vp->bssid, ETH_ALEN);
	memcpy(hdr->addr2, mac, ETH_ALEN);
	memcpy(hdr->addr3, vp->bssid, ETH_ALEN);
	if (data->radio_enabled &&
	    !mac80211_hwsim_tx_frame(data->hw, skb))
		printk(KERN_DEBUG "%s: nullfunc frame not ack'ed\n", __func__);
	dev_kfree_skb(skb);
}


static void hwsim_send_nullfunc_ps(void *dat, u8 *mac,
				   struct ieee80211_vif *vif)
{
	struct mac80211_hwsim_data *data = dat;
	hwsim_send_nullfunc(data, mac, vif, 1);
}


static void hwsim_send_nullfunc_no_ps(void *dat, u8 *mac,
				      struct ieee80211_vif *vif)
{
	struct mac80211_hwsim_data *data = dat;
	hwsim_send_nullfunc(data, mac, vif, 0);
}


static int hwsim_fops_ps_read(void *dat, u64 *val)
{
	struct mac80211_hwsim_data *data = dat;
	*val = data->ps;
	return 0;
}

static int hwsim_fops_ps_write(void *dat, u64 val)
{
	struct mac80211_hwsim_data *data = dat;
	enum ps_mode old_ps;

	if (val != PS_DISABLED && val != PS_ENABLED && val != PS_AUTO_POLL &&
	    val != PS_MANUAL_POLL)
		return -EINVAL;

	old_ps = data->ps;
	data->ps = val;

	if (val == PS_MANUAL_POLL) {
		ieee80211_iterate_active_interfaces(data->hw,
						    hwsim_send_ps_poll, data);
		data->ps_poll_pending = true;
	} else if (old_ps == PS_DISABLED && val != PS_DISABLED) {
		ieee80211_iterate_active_interfaces(data->hw,
						    hwsim_send_nullfunc_ps,
						    data);
	} else if (old_ps != PS_DISABLED && val == PS_DISABLED) {
		ieee80211_iterate_active_interfaces(data->hw,
						    hwsim_send_nullfunc_no_ps,
						    data);
	}

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(hwsim_fops_ps, hwsim_fops_ps_read, hwsim_fops_ps_write,
			"%llu\n");


static int __init init_mac80211_hwsim(void)
{
	int i, err = 0;
	u8 addr[ETH_ALEN];
	struct mac80211_hwsim_data *data;
	struct ieee80211_hw *hw;

	if (radios < 1 || radios > 100)
		return -EINVAL;

	spin_lock_init(&hwsim_radio_lock);
	INIT_LIST_HEAD(&hwsim_radios);

	hwsim_class = class_create(THIS_MODULE, "mac80211_hwsim");
	if (IS_ERR(hwsim_class))
		return PTR_ERR(hwsim_class);

	memset(addr, 0, ETH_ALEN);
	addr[0] = 0x02;

	for (i = 0; i < radios; i++) {
		printk(KERN_DEBUG "mac80211_hwsim: Initializing radio %d\n",
		       i);
		hw = ieee80211_alloc_hw(sizeof(*data), &mac80211_hwsim_ops);
		if (!hw) {
			printk(KERN_DEBUG "mac80211_hwsim: ieee80211_alloc_hw "
			       "failed\n");
			err = -ENOMEM;
			goto failed;
		}
		data = hw->priv;
		data->hw = hw;

		data->dev = device_create(hwsim_class, NULL, 0, hw,
					  "hwsim%d", i);
		if (IS_ERR(data->dev)) {
			printk(KERN_DEBUG
			       "mac80211_hwsim: device_create "
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
			BIT(NL80211_IFTYPE_AP) |
			BIT(NL80211_IFTYPE_MESH_POINT);
		hw->ampdu_queues = 1;

		/* ask mac80211 to reserve space for magic */
		hw->vif_data_size = sizeof(struct hwsim_vif_priv);
		hw->sta_data_size = sizeof(struct hwsim_sta_priv);

		memcpy(data->channels, hwsim_channels, sizeof(hwsim_channels));
		memcpy(data->rates, hwsim_rates, sizeof(hwsim_rates));
		data->band.channels = data->channels;
		data->band.n_channels = ARRAY_SIZE(hwsim_channels);
		data->band.bitrates = data->rates;
		data->band.n_bitrates = ARRAY_SIZE(hwsim_rates);
		data->band.ht_cap.ht_supported = true;
		data->band.ht_cap.cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
			IEEE80211_HT_CAP_GRN_FLD |
			IEEE80211_HT_CAP_SGI_40 |
			IEEE80211_HT_CAP_DSSSCCK40;
		data->band.ht_cap.ampdu_factor = 0x3;
		data->band.ht_cap.ampdu_density = 0x6;
		memset(&data->band.ht_cap.mcs, 0,
		       sizeof(data->band.ht_cap.mcs));
		data->band.ht_cap.mcs.rx_mask[0] = 0xff;
		data->band.ht_cap.mcs.rx_mask[1] = 0xff;
		data->band.ht_cap.mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
		hw->wiphy->bands[IEEE80211_BAND_2GHZ] = &data->band;

		err = ieee80211_register_hw(hw);
		if (err < 0) {
			printk(KERN_DEBUG "mac80211_hwsim: "
			       "ieee80211_register_hw failed (%d)\n", err);
			goto failed_hw;
		}

		printk(KERN_DEBUG "%s: hwaddr %pM registered\n",
		       wiphy_name(hw->wiphy),
		       hw->wiphy->perm_addr);

		data->debugfs = debugfs_create_dir("hwsim",
						   hw->wiphy->debugfsdir);
		data->debugfs_ps = debugfs_create_file("ps", 0666,
						       data->debugfs, data,
						       &hwsim_fops_ps);

		setup_timer(&data->beacon_timer, mac80211_hwsim_beacon,
			    (unsigned long) hw);

		list_add_tail(&data->list, &hwsim_radios);
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
failed:
	mac80211_hwsim_free();
	return err;
}


static void __exit exit_mac80211_hwsim(void)
{
	printk(KERN_DEBUG "mac80211_hwsim: unregister radios\n");

	unregister_netdev(hwsim_mon);
	mac80211_hwsim_free();
}


module_init(init_mac80211_hwsim);
module_exit(exit_mac80211_hwsim);
