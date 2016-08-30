/*
 * mac80211_hwsim - software simulator of 802.11 radio(s) for mac80211
 * Copyright (c) 2008, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2011, Javier Lopez <jlopex@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * TODO:
 * - Add TSF sync and fix IBSS beacon transmission by adding
 *   competition for "air time" at TBTT
 * - RX filtering based on filter configuration (data->rx_filter)
 */

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <net/dst.h>
#include <net/xfrm.h>
#include <net/mac80211.h>
#include <net/ieee80211_radiotap.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/etherdevice.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/ktime.h>
#include <net/genetlink.h>
#include "mac80211_hwsim.h"

#define WARN_QUEUE 100
#define MAX_QUEUE 200

MODULE_AUTHOR("Jouni Malinen");
MODULE_DESCRIPTION("Software simulator of 802.11 radio(s) for mac80211");
MODULE_LICENSE("GPL");

static u32 wmediumd_portid;

static int radios = 2;
module_param(radios, int, 0444);
MODULE_PARM_DESC(radios, "Number of simulated radios");

static int channels = 1;
module_param(channels, int, 0444);
MODULE_PARM_DESC(channels, "Number of concurrent channels");

static bool paged_rx = false;
module_param(paged_rx, bool, 0644);
MODULE_PARM_DESC(paged_rx, "Use paged SKBs for RX instead of linear ones");

static bool rctbl = false;
module_param(rctbl, bool, 0444);
MODULE_PARM_DESC(rctbl, "Handle rate control table");

/**
 * enum hwsim_regtest - the type of regulatory tests we offer
 *
 * These are the different values you can use for the regtest
 * module parameter. This is useful to help test world roaming
 * and the driver regulatory_hint() call and combinations of these.
 * If you want to do specific alpha2 regulatory domain tests simply
 * use the userspace regulatory request as that will be respected as
 * well without the need of this module parameter. This is designed
 * only for testing the driver regulatory request, world roaming
 * and all possible combinations.
 *
 * @HWSIM_REGTEST_DISABLED: No regulatory tests are performed,
 * 	this is the default value.
 * @HWSIM_REGTEST_DRIVER_REG_FOLLOW: Used for testing the driver regulatory
 *	hint, only one driver regulatory hint will be sent as such the
 * 	secondary radios are expected to follow.
 * @HWSIM_REGTEST_DRIVER_REG_ALL: Used for testing the driver regulatory
 * 	request with all radios reporting the same regulatory domain.
 * @HWSIM_REGTEST_DIFF_COUNTRY: Used for testing the drivers calling
 * 	different regulatory domains requests. Expected behaviour is for
 * 	an intersection to occur but each device will still use their
 * 	respective regulatory requested domains. Subsequent radios will
 * 	use the resulting intersection.
 * @HWSIM_REGTEST_WORLD_ROAM: Used for testing the world roaming. We accomplish
 *	this by using a custom beacon-capable regulatory domain for the first
 *	radio. All other device world roam.
 * @HWSIM_REGTEST_CUSTOM_WORLD: Used for testing the custom world regulatory
 * 	domain requests. All radios will adhere to this custom world regulatory
 * 	domain.
 * @HWSIM_REGTEST_CUSTOM_WORLD_2: Used for testing 2 custom world regulatory
 * 	domain requests. The first radio will adhere to the first custom world
 * 	regulatory domain, the second one to the second custom world regulatory
 * 	domain. All other devices will world roam.
 * @HWSIM_REGTEST_STRICT_FOLLOW_: Used for testing strict regulatory domain
 *	settings, only the first radio will send a regulatory domain request
 *	and use strict settings. The rest of the radios are expected to follow.
 * @HWSIM_REGTEST_STRICT_ALL: Used for testing strict regulatory domain
 *	settings. All radios will adhere to this.
 * @HWSIM_REGTEST_STRICT_AND_DRIVER_REG: Used for testing strict regulatory
 *	domain settings, combined with secondary driver regulatory domain
 *	settings. The first radio will get a strict regulatory domain setting
 *	using the first driver regulatory request and the second radio will use
 *	non-strict settings using the second driver regulatory request. All
 *	other devices should follow the intersection created between the
 *	first two.
 * @HWSIM_REGTEST_ALL: Used for testing every possible mix. You will need
 * 	at least 6 radios for a complete test. We will test in this order:
 * 	1 - driver custom world regulatory domain
 * 	2 - second custom world regulatory domain
 * 	3 - first driver regulatory domain request
 * 	4 - second driver regulatory domain request
 * 	5 - strict regulatory domain settings using the third driver regulatory
 * 	    domain request
 * 	6 and on - should follow the intersection of the 3rd, 4rth and 5th radio
 * 	           regulatory requests.
 */
enum hwsim_regtest {
	HWSIM_REGTEST_DISABLED = 0,
	HWSIM_REGTEST_DRIVER_REG_FOLLOW = 1,
	HWSIM_REGTEST_DRIVER_REG_ALL = 2,
	HWSIM_REGTEST_DIFF_COUNTRY = 3,
	HWSIM_REGTEST_WORLD_ROAM = 4,
	HWSIM_REGTEST_CUSTOM_WORLD = 5,
	HWSIM_REGTEST_CUSTOM_WORLD_2 = 6,
	HWSIM_REGTEST_STRICT_FOLLOW = 7,
	HWSIM_REGTEST_STRICT_ALL = 8,
	HWSIM_REGTEST_STRICT_AND_DRIVER_REG = 9,
	HWSIM_REGTEST_ALL = 10,
};

/* Set to one of the HWSIM_REGTEST_* values above */
static int regtest = HWSIM_REGTEST_DISABLED;
module_param(regtest, int, 0444);
MODULE_PARM_DESC(regtest, "The type of regulatory test we want to run");

static const char *hwsim_alpha2s[] = {
	"FI",
	"AL",
	"US",
	"DE",
	"JP",
	"AL",
};

static const struct ieee80211_regdomain hwsim_world_regdom_custom_01 = {
	.n_reg_rules = 4,
	.alpha2 =  "99",
	.reg_rules = {
		REG_RULE(2412-10, 2462+10, 40, 0, 20, 0),
		REG_RULE(2484-10, 2484+10, 40, 0, 20, 0),
		REG_RULE(5150-10, 5240+10, 40, 0, 30, 0),
		REG_RULE(5745-10, 5825+10, 40, 0, 30, 0),
	}
};

static const struct ieee80211_regdomain hwsim_world_regdom_custom_02 = {
	.n_reg_rules = 2,
	.alpha2 =  "99",
	.reg_rules = {
		REG_RULE(2412-10, 2462+10, 40, 0, 20, 0),
		REG_RULE(5725-10, 5850+10, 40, 0, 30,
			NL80211_RRF_PASSIVE_SCAN | NL80211_RRF_NO_IBSS),
	}
};

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
	WARN(vp->magic != HWSIM_VIF_MAGIC,
	     "Invalid VIF (%p) magic %#x, %pM, %d/%d\n",
	     vif, vp->magic, vif->addr, vif->type, vif->p2p);
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

#define HWSIM_STA_MAGIC	0x6d537749

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

struct hwsim_chanctx_priv {
	u32 magic;
};

#define HWSIM_CHANCTX_MAGIC 0x6d53774a

static inline void hwsim_check_chanctx_magic(struct ieee80211_chanctx_conf *c)
{
	struct hwsim_chanctx_priv *cp = (void *)c->drv_priv;
	WARN_ON(cp->magic != HWSIM_CHANCTX_MAGIC);
}

static inline void hwsim_set_chanctx_magic(struct ieee80211_chanctx_conf *c)
{
	struct hwsim_chanctx_priv *cp = (void *)c->drv_priv;
	cp->magic = HWSIM_CHANCTX_MAGIC;
}

static inline void hwsim_clear_chanctx_magic(struct ieee80211_chanctx_conf *c)
{
	struct hwsim_chanctx_priv *cp = (void *)c->drv_priv;
	cp->magic = 0;
}

static struct class *hwsim_class;

static struct net_device *hwsim_mon; /* global monitor netdev */

#define CHAN2G(_freq)  { \
	.band = IEEE80211_BAND_2GHZ, \
	.center_freq = (_freq), \
	.hw_value = (_freq), \
	.max_power = 20, \
}

#define CHAN5G(_freq) { \
	.band = IEEE80211_BAND_5GHZ, \
	.center_freq = (_freq), \
	.hw_value = (_freq), \
	.max_power = 20, \
}

static const struct ieee80211_channel hwsim_channels_2ghz[] = {
	CHAN2G(2412), /* Channel 1 */
	CHAN2G(2417), /* Channel 2 */
	CHAN2G(2422), /* Channel 3 */
	CHAN2G(2427), /* Channel 4 */
	CHAN2G(2432), /* Channel 5 */
	CHAN2G(2437), /* Channel 6 */
	CHAN2G(2442), /* Channel 7 */
	CHAN2G(2447), /* Channel 8 */
	CHAN2G(2452), /* Channel 9 */
	CHAN2G(2457), /* Channel 10 */
	CHAN2G(2462), /* Channel 11 */
	CHAN2G(2467), /* Channel 12 */
	CHAN2G(2472), /* Channel 13 */
	CHAN2G(2484), /* Channel 14 */
};

static const struct ieee80211_channel hwsim_channels_5ghz[] = {
	CHAN5G(5180), /* Channel 36 */
	CHAN5G(5200), /* Channel 40 */
	CHAN5G(5220), /* Channel 44 */
	CHAN5G(5240), /* Channel 48 */

	CHAN5G(5260), /* Channel 52 */
	CHAN5G(5280), /* Channel 56 */
	CHAN5G(5300), /* Channel 60 */
	CHAN5G(5320), /* Channel 64 */

	CHAN5G(5500), /* Channel 100 */
	CHAN5G(5520), /* Channel 104 */
	CHAN5G(5540), /* Channel 108 */
	CHAN5G(5560), /* Channel 112 */
	CHAN5G(5580), /* Channel 116 */
	CHAN5G(5600), /* Channel 120 */
	CHAN5G(5620), /* Channel 124 */
	CHAN5G(5640), /* Channel 128 */
	CHAN5G(5660), /* Channel 132 */
	CHAN5G(5680), /* Channel 136 */
	CHAN5G(5700), /* Channel 140 */

	CHAN5G(5745), /* Channel 149 */
	CHAN5G(5765), /* Channel 153 */
	CHAN5G(5785), /* Channel 157 */
	CHAN5G(5805), /* Channel 161 */
	CHAN5G(5825), /* Channel 165 */
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
	struct ieee80211_supported_band bands[IEEE80211_NUM_BANDS];
	struct ieee80211_channel channels_2ghz[ARRAY_SIZE(hwsim_channels_2ghz)];
	struct ieee80211_channel channels_5ghz[ARRAY_SIZE(hwsim_channels_5ghz)];
	struct ieee80211_rate rates[ARRAY_SIZE(hwsim_rates)];

	struct mac_address addresses[2];

	struct ieee80211_channel *tmp_chan;
	struct delayed_work roc_done;
	struct delayed_work hw_scan;
	struct cfg80211_scan_request *hw_scan_request;
	struct ieee80211_vif *hw_scan_vif;
	int scan_chan_idx;

	struct ieee80211_channel *channel;
	u64 beacon_int	/* beacon interval in us */;
	unsigned int rx_filter;
	bool started, idle, scanning;
	struct mutex mutex;
	struct tasklet_hrtimer beacon_timer;
	enum ps_mode {
		PS_DISABLED, PS_ENABLED, PS_AUTO_POLL, PS_MANUAL_POLL
	} ps;
	bool ps_poll_pending;
	struct dentry *debugfs;
	struct dentry *debugfs_ps;

	struct sk_buff_head pending;	/* packets pending */
	/*
	 * Only radios in the same group can communicate together (the
	 * channel has to match too). Each bit represents a group. A
	 * radio can be in more then one group.
	 */
	u64 group;
	struct dentry *debugfs_group;

	int power_level;

	/* difference between this hw's clock and the real clock, in usecs */
	s64 tsf_offset;
	s64 bcn_delta;
	/* absolute beacon transmission time. Used to cover up "tx" delay. */
	u64 abs_bcn_ts;
};


struct hwsim_radiotap_hdr {
	struct ieee80211_radiotap_header hdr;
	__le64 rt_tsft;
	u8 rt_flags;
	u8 rt_rate;
	__le16 rt_channel;
	__le16 rt_chbitmask;
} __packed;

/* MAC80211_HWSIM netlinf family */
static struct genl_family hwsim_genl_family = {
	.id = GENL_ID_GENERATE,
	.hdrsize = 0,
	.name = "MAC80211_HWSIM",
	.version = 1,
	.maxattr = HWSIM_ATTR_MAX,
};

/* MAC80211_HWSIM netlink policy */

static struct nla_policy hwsim_genl_policy[HWSIM_ATTR_MAX + 1] = {
	[HWSIM_ATTR_ADDR_RECEIVER] = { .type = NLA_UNSPEC,
				       .len = 6*sizeof(u8) },
	[HWSIM_ATTR_ADDR_TRANSMITTER] = { .type = NLA_UNSPEC,
					  .len = 6*sizeof(u8) },
	[HWSIM_ATTR_FRAME] = { .type = NLA_BINARY,
			       .len = IEEE80211_MAX_DATA_LEN },
	[HWSIM_ATTR_FLAGS] = { .type = NLA_U32 },
	[HWSIM_ATTR_RX_RATE] = { .type = NLA_U32 },
	[HWSIM_ATTR_SIGNAL] = { .type = NLA_U32 },
	[HWSIM_ATTR_TX_INFO] = { .type = NLA_UNSPEC,
				 .len = IEEE80211_TX_MAX_RATES*sizeof(
					struct hwsim_tx_rate)},
	[HWSIM_ATTR_COOKIE] = { .type = NLA_U64 },
};

static netdev_tx_t hwsim_mon_xmit(struct sk_buff *skb,
					struct net_device *dev)
{
	/* TODO: allow packet injection */
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static inline u64 mac80211_hwsim_get_tsf_raw(void)
{
	return ktime_to_us(ktime_get_real());
}

static __le64 __mac80211_hwsim_get_tsf(struct mac80211_hwsim_data *data)
{
	u64 now = mac80211_hwsim_get_tsf_raw();
	return cpu_to_le64(now + data->tsf_offset);
}

static u64 mac80211_hwsim_get_tsf(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif)
{
	struct mac80211_hwsim_data *data = hw->priv;
	return le64_to_cpu(__mac80211_hwsim_get_tsf(data));
}

static void mac80211_hwsim_set_tsf(struct ieee80211_hw *hw,
		struct ieee80211_vif *vif, u64 tsf)
{
	struct mac80211_hwsim_data *data = hw->priv;
	u64 now = mac80211_hwsim_get_tsf(hw, vif);
	u32 bcn_int = data->beacon_int;
	s64 delta = tsf - now;

	data->tsf_offset += delta;
	/* adjust after beaconing with new timestamp at old TBTT */
	data->bcn_delta = do_div(delta, bcn_int);
}

static void mac80211_hwsim_monitor_rx(struct ieee80211_hw *hw,
				      struct sk_buff *tx_skb,
				      struct ieee80211_channel *chan)
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
					  (1 << IEEE80211_RADIOTAP_TSFT) |
					  (1 << IEEE80211_RADIOTAP_CHANNEL));
	hdr->rt_tsft = __mac80211_hwsim_get_tsf(data);
	hdr->rt_flags = 0;
	hdr->rt_rate = txrate->bitrate / 5;
	hdr->rt_channel = cpu_to_le16(chan->center_freq);
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


static void mac80211_hwsim_monitor_ack(struct ieee80211_channel *chan,
				       const u8 *addr)
{
	struct sk_buff *skb;
	struct hwsim_radiotap_hdr *hdr;
	u16 flags;
	struct ieee80211_hdr *hdr11;

	if (!netif_running(hwsim_mon))
		return;

	skb = dev_alloc_skb(100);
	if (skb == NULL)
		return;

	hdr = (struct hwsim_radiotap_hdr *) skb_put(skb, sizeof(*hdr));
	hdr->hdr.it_version = PKTHDR_RADIOTAP_VERSION;
	hdr->hdr.it_pad = 0;
	hdr->hdr.it_len = cpu_to_le16(sizeof(*hdr));
	hdr->hdr.it_present = cpu_to_le32((1 << IEEE80211_RADIOTAP_FLAGS) |
					  (1 << IEEE80211_RADIOTAP_CHANNEL));
	hdr->rt_flags = 0;
	hdr->rt_rate = 0;
	hdr->rt_channel = cpu_to_le16(chan->center_freq);
	flags = IEEE80211_CHAN_2GHZ;
	hdr->rt_chbitmask = cpu_to_le16(flags);

	hdr11 = (struct ieee80211_hdr *) skb_put(skb, 10);
	hdr11->frame_control = cpu_to_le16(IEEE80211_FTYPE_CTL |
					   IEEE80211_STYPE_ACK);
	hdr11->duration_id = cpu_to_le16(0);
	memcpy(hdr11->addr1, addr, ETH_ALEN);

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


struct mac80211_hwsim_addr_match_data {
	bool ret;
	const u8 *addr;
};

static void mac80211_hwsim_addr_iter(void *data, u8 *mac,
				     struct ieee80211_vif *vif)
{
	struct mac80211_hwsim_addr_match_data *md = data;
	if (memcmp(mac, md->addr, ETH_ALEN) == 0)
		md->ret = true;
}


static bool mac80211_hwsim_addr_match(struct mac80211_hwsim_data *data,
				      const u8 *addr)
{
	struct mac80211_hwsim_addr_match_data md;

	if (memcmp(addr, data->hw->wiphy->perm_addr, ETH_ALEN) == 0)
		return true;

	md.ret = false;
	md.addr = addr;
	ieee80211_iterate_active_interfaces_atomic(data->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   mac80211_hwsim_addr_iter,
						   &md);

	return md.ret;
}

static void mac80211_hwsim_tx_frame_nl(struct ieee80211_hw *hw,
				       struct sk_buff *my_skb,
				       int dst_portid)
{
	struct sk_buff *skb;
	struct mac80211_hwsim_data *data = hw->priv;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) my_skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(my_skb);
	void *msg_head;
	unsigned int hwsim_flags = 0;
	int i;
	struct hwsim_tx_rate tx_attempts[IEEE80211_TX_MAX_RATES];

	if (data->ps != PS_DISABLED)
		hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PM);
	/* If the queue contains MAX_QUEUE skb's drop some */
	if (skb_queue_len(&data->pending) >= MAX_QUEUE) {
		/* Droping until WARN_QUEUE level */
		while (skb_queue_len(&data->pending) >= WARN_QUEUE)
			skb_dequeue(&data->pending);
	}

	skb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (skb == NULL)
		goto nla_put_failure;

	msg_head = genlmsg_put(skb, 0, 0, &hwsim_genl_family, 0,
			       HWSIM_CMD_FRAME);
	if (msg_head == NULL) {
		printk(KERN_DEBUG "mac80211_hwsim: problem with msg_head\n");
		goto nla_put_failure;
	}

	if (nla_put(skb, HWSIM_ATTR_ADDR_TRANSMITTER,
		    sizeof(struct mac_address), data->addresses[1].addr))
		goto nla_put_failure;

	/* We get the skb->data */
	if (nla_put(skb, HWSIM_ATTR_FRAME, my_skb->len, my_skb->data))
		goto nla_put_failure;

	/* We get the flags for this transmission, and we translate them to
	   wmediumd flags  */

	if (info->flags & IEEE80211_TX_CTL_REQ_TX_STATUS)
		hwsim_flags |= HWSIM_TX_CTL_REQ_TX_STATUS;

	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		hwsim_flags |= HWSIM_TX_CTL_NO_ACK;

	if (nla_put_u32(skb, HWSIM_ATTR_FLAGS, hwsim_flags))
		goto nla_put_failure;

	/* We get the tx control (rate and retries) info*/

	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		tx_attempts[i].idx = info->status.rates[i].idx;
		tx_attempts[i].count = info->status.rates[i].count;
	}

	if (nla_put(skb, HWSIM_ATTR_TX_INFO,
		    sizeof(struct hwsim_tx_rate)*IEEE80211_TX_MAX_RATES,
		    tx_attempts))
		goto nla_put_failure;

	/* We create a cookie to identify this skb */
	if (nla_put_u64(skb, HWSIM_ATTR_COOKIE, (unsigned long) my_skb))
		goto nla_put_failure;

	genlmsg_end(skb, msg_head);
	genlmsg_unicast(&init_net, skb, dst_portid);

	/* Enqueue the packet */
	skb_queue_tail(&data->pending, my_skb);
	return;

nla_put_failure:
	printk(KERN_DEBUG "mac80211_hwsim: error occurred in %s\n", __func__);
}

static bool hwsim_chans_compat(struct ieee80211_channel *c1,
			       struct ieee80211_channel *c2)
{
	if (!c1 || !c2)
		return false;

	return c1->center_freq == c2->center_freq;
}

struct tx_iter_data {
	struct ieee80211_channel *channel;
	bool receive;
};

static void mac80211_hwsim_tx_iter(void *_data, u8 *addr,
				   struct ieee80211_vif *vif)
{
	struct tx_iter_data *data = _data;

	if (!vif->chanctx_conf)
		return;

	if (!hwsim_chans_compat(data->channel,
				rcu_dereference(vif->chanctx_conf)->def.chan))
		return;

	data->receive = true;
}

static bool mac80211_hwsim_tx_frame_no_nl(struct ieee80211_hw *hw,
					  struct sk_buff *skb,
					  struct ieee80211_channel *chan)
{
	struct mac80211_hwsim_data *data = hw->priv, *data2;
	bool ack = false;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_rx_status rx_status;
	u64 now;

	memset(&rx_status, 0, sizeof(rx_status));
	rx_status.flag |= RX_FLAG_MACTIME_START;
	rx_status.freq = chan->center_freq;
	rx_status.band = chan->band;
	if (info->control.rates[0].flags & IEEE80211_TX_RC_VHT_MCS) {
		rx_status.rate_idx =
			ieee80211_rate_get_vht_mcs(&info->control.rates[0]);
		rx_status.vht_nss =
			ieee80211_rate_get_vht_nss(&info->control.rates[0]);
		rx_status.flag |= RX_FLAG_VHT;
	} else {
		rx_status.rate_idx = info->control.rates[0].idx;
		if (info->control.rates[0].flags & IEEE80211_TX_RC_MCS)
			rx_status.flag |= RX_FLAG_HT;
	}
	if (info->control.rates[0].flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
		rx_status.flag |= RX_FLAG_40MHZ;
	if (info->control.rates[0].flags & IEEE80211_TX_RC_SHORT_GI)
		rx_status.flag |= RX_FLAG_SHORT_GI;
	/* TODO: simulate real signal strength (and optional packet loss) */
	rx_status.signal = data->power_level - 50;

	if (data->ps != PS_DISABLED)
		hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PM);

	/* release the skb's source info */
	skb_orphan(skb);
	skb_dst_drop(skb);
	skb->mark = 0;
	secpath_reset(skb);
	nf_reset(skb);

	/*
	 * Get absolute mactime here so all HWs RX at the "same time", and
	 * absolute TX time for beacon mactime so the timestamp matches.
	 * Giving beacons a different mactime than non-beacons looks messy, but
	 * it helps the Toffset be exact and a ~10us mactime discrepancy
	 * probably doesn't really matter.
	 */
	if (ieee80211_is_beacon(hdr->frame_control) ||
	    ieee80211_is_probe_resp(hdr->frame_control))
		now = data->abs_bcn_ts;
	else
		now = mac80211_hwsim_get_tsf_raw();

	/* Copy skb to all enabled radios that are on the current frequency */
	spin_lock(&hwsim_radio_lock);
	list_for_each_entry(data2, &hwsim_radios, list) {
		struct sk_buff *nskb;
		struct tx_iter_data tx_iter_data = {
			.receive = false,
			.channel = chan,
		};

		if (data == data2)
			continue;

		if (!data2->started || (data2->idle && !data2->tmp_chan) ||
		    !hwsim_ps_rx_ok(data2, skb))
			continue;

		if (!(data->group & data2->group))
			continue;

		if (!hwsim_chans_compat(chan, data2->tmp_chan) &&
		    !hwsim_chans_compat(chan, data2->channel)) {
			ieee80211_iterate_active_interfaces_atomic(
				data2->hw, IEEE80211_IFACE_ITER_NORMAL,
				mac80211_hwsim_tx_iter, &tx_iter_data);
			if (!tx_iter_data.receive)
				continue;
		}

		/*
		 * reserve some space for our vendor and the normal
		 * radiotap header, since we're copying anyway
		 */
		if (skb->len < PAGE_SIZE && paged_rx) {
			struct page *page = alloc_page(GFP_ATOMIC);

			if (!page)
				continue;

			nskb = dev_alloc_skb(128);
			if (!nskb) {
				__free_page(page);
				continue;
			}

			memcpy(page_address(page), skb->data, skb->len);
			skb_add_rx_frag(nskb, 0, page, 0, skb->len, skb->len);
		} else {
			nskb = skb_copy(skb, GFP_ATOMIC);
			if (!nskb)
				continue;
		}

		if (mac80211_hwsim_addr_match(data2, hdr->addr1))
			ack = true;

		rx_status.mactime = now + data2->tsf_offset;
#if 0
		/*
		 * Don't enable this code by default as the OUI 00:00:00
		 * is registered to Xerox so we shouldn't use it here, it
		 * might find its way into pcap files.
		 * Note that this code requires the headroom in the SKB
		 * that was allocated earlier.
		 */
		rx_status.vendor_radiotap_oui[0] = 0x00;
		rx_status.vendor_radiotap_oui[1] = 0x00;
		rx_status.vendor_radiotap_oui[2] = 0x00;
		rx_status.vendor_radiotap_subns = 127;
		/*
		 * Radiotap vendor namespaces can (and should) also be
		 * split into fields by using the standard radiotap
		 * presence bitmap mechanism. Use just BIT(0) here for
		 * the presence bitmap.
		 */
		rx_status.vendor_radiotap_bitmap = BIT(0);
		/* We have 8 bytes of (dummy) data */
		rx_status.vendor_radiotap_len = 8;
		/* For testing, also require it to be aligned */
		rx_status.vendor_radiotap_align = 8;
		/* push the data */
		memcpy(skb_push(nskb, 8), "ABCDEFGH", 8);
#endif

		memcpy(IEEE80211_SKB_RXCB(nskb), &rx_status, sizeof(rx_status));
		ieee80211_rx_irqsafe(data2->hw, nskb);
	}
	spin_unlock(&hwsim_radio_lock);

	return ack;
}

static void mac80211_hwsim_tx(struct ieee80211_hw *hw,
			      struct ieee80211_tx_control *control,
			      struct sk_buff *skb)
{
	struct mac80211_hwsim_data *data = hw->priv;
	struct ieee80211_tx_info *txi = IEEE80211_SKB_CB(skb);
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ieee80211_channel *channel;
	bool ack;
	u32 _portid;

	if (WARN_ON(skb->len < 10)) {
		/* Should not happen; just a sanity check for addr1 use */
		dev_kfree_skb(skb);
		return;
	}

	if (channels == 1) {
		channel = data->channel;
	} else if (txi->hw_queue == 4) {
		channel = data->tmp_chan;
	} else {
		chanctx_conf = rcu_dereference(txi->control.vif->chanctx_conf);
		if (chanctx_conf)
			channel = chanctx_conf->def.chan;
		else
			channel = NULL;
	}

	if (WARN(!channel, "TX w/o channel - queue = %d\n", txi->hw_queue)) {
		dev_kfree_skb(skb);
		return;
	}

	if (data->idle && !data->tmp_chan) {
		wiphy_debug(hw->wiphy, "Trying to TX when idle - reject\n");
		dev_kfree_skb(skb);
		return;
	}

	if (txi->control.vif)
		hwsim_check_magic(txi->control.vif);
	if (control->sta)
		hwsim_check_sta_magic(control->sta);

	if (rctbl)
		ieee80211_get_tx_rates(txi->control.vif, control->sta, skb,
				       txi->control.rates,
				       ARRAY_SIZE(txi->control.rates));

	txi->rate_driver_data[0] = channel;
	mac80211_hwsim_monitor_rx(hw, skb, channel);

	/* wmediumd mode check */
	_portid = ACCESS_ONCE(wmediumd_portid);

	if (_portid)
		return mac80211_hwsim_tx_frame_nl(hw, skb, _portid);

	/* NO wmediumd detected, perfect medium simulation */
	ack = mac80211_hwsim_tx_frame_no_nl(hw, skb, channel);

	if (ack && skb->len >= 16) {
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
		mac80211_hwsim_monitor_ack(channel, hdr->addr2);
	}

	ieee80211_tx_info_clear_status(txi);

	/* frame was transmitted at most favorable rate at first attempt */
	txi->control.rates[0].count = 1;
	txi->control.rates[1].idx = -1;

	if (!(txi->flags & IEEE80211_TX_CTL_NO_ACK) && ack)
		txi->flags |= IEEE80211_TX_STAT_ACK;
	ieee80211_tx_status_irqsafe(hw, skb);
}


static int mac80211_hwsim_start(struct ieee80211_hw *hw)
{
	struct mac80211_hwsim_data *data = hw->priv;
	wiphy_debug(hw->wiphy, "%s\n", __func__);
	data->started = true;
	return 0;
}


static void mac80211_hwsim_stop(struct ieee80211_hw *hw)
{
	struct mac80211_hwsim_data *data = hw->priv;
	data->started = false;
	tasklet_hrtimer_cancel(&data->beacon_timer);
	wiphy_debug(hw->wiphy, "%s\n", __func__);
}


static int mac80211_hwsim_add_interface(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif)
{
	wiphy_debug(hw->wiphy, "%s (type=%d mac_addr=%pM)\n",
		    __func__, ieee80211_vif_type_p2p(vif),
		    vif->addr);
	hwsim_set_magic(vif);

	vif->cab_queue = 0;
	vif->hw_queue[IEEE80211_AC_VO] = 0;
	vif->hw_queue[IEEE80211_AC_VI] = 1;
	vif->hw_queue[IEEE80211_AC_BE] = 2;
	vif->hw_queue[IEEE80211_AC_BK] = 3;

	return 0;
}


static int mac80211_hwsim_change_interface(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif,
					   enum nl80211_iftype newtype,
					   bool newp2p)
{
	newtype = ieee80211_iftype_p2p(newtype, newp2p);
	wiphy_debug(hw->wiphy,
		    "%s (old type=%d, new type=%d, mac_addr=%pM)\n",
		    __func__, ieee80211_vif_type_p2p(vif),
		    newtype, vif->addr);
	hwsim_check_magic(vif);

	/*
	 * interface may change from non-AP to AP in
	 * which case this needs to be set up again
	 */
	vif->cab_queue = 0;

	return 0;
}

static void mac80211_hwsim_remove_interface(
	struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	wiphy_debug(hw->wiphy, "%s (type=%d mac_addr=%pM)\n",
		    __func__, ieee80211_vif_type_p2p(vif),
		    vif->addr);
	hwsim_check_magic(vif);
	hwsim_clear_magic(vif);
}

static void mac80211_hwsim_tx_frame(struct ieee80211_hw *hw,
				    struct sk_buff *skb,
				    struct ieee80211_channel *chan)
{
	u32 _pid = ACCESS_ONCE(wmediumd_portid);

	if (rctbl) {
		struct ieee80211_tx_info *txi = IEEE80211_SKB_CB(skb);
		ieee80211_get_tx_rates(txi->control.vif, NULL, skb,
				       txi->control.rates,
				       ARRAY_SIZE(txi->control.rates));
	}

	mac80211_hwsim_monitor_rx(hw, skb, chan);

	if (_pid)
		return mac80211_hwsim_tx_frame_nl(hw, skb, _pid);

	mac80211_hwsim_tx_frame_no_nl(hw, skb, chan);
	dev_kfree_skb(skb);
}

static void mac80211_hwsim_beacon_tx(void *arg, u8 *mac,
				     struct ieee80211_vif *vif)
{
	struct mac80211_hwsim_data *data = arg;
	struct ieee80211_hw *hw = data->hw;
	struct ieee80211_tx_info *info;
	struct ieee80211_rate *txrate;
	struct ieee80211_mgmt *mgmt;
	struct sk_buff *skb;

	hwsim_check_magic(vif);

	if (vif->type != NL80211_IFTYPE_AP &&
	    vif->type != NL80211_IFTYPE_MESH_POINT &&
	    vif->type != NL80211_IFTYPE_ADHOC)
		return;

	skb = ieee80211_beacon_get(hw, vif);
	if (skb == NULL)
		return;
	info = IEEE80211_SKB_CB(skb);
	if (rctbl)
		ieee80211_get_tx_rates(vif, NULL, skb,
				       info->control.rates,
				       ARRAY_SIZE(info->control.rates));

	txrate = ieee80211_get_tx_rate(hw, info);

	mgmt = (struct ieee80211_mgmt *) skb->data;
	/* fake header transmission time */
	data->abs_bcn_ts = mac80211_hwsim_get_tsf_raw();
	mgmt->u.beacon.timestamp = cpu_to_le64(data->abs_bcn_ts +
					       data->tsf_offset +
					       24 * 8 * 10 / txrate->bitrate);

	mac80211_hwsim_tx_frame(hw, skb,
				rcu_dereference(vif->chanctx_conf)->def.chan);
}

static enum hrtimer_restart
mac80211_hwsim_beacon(struct hrtimer *timer)
{
	struct mac80211_hwsim_data *data =
		container_of(timer, struct mac80211_hwsim_data,
			     beacon_timer.timer);
	struct ieee80211_hw *hw = data->hw;
	u64 bcn_int = data->beacon_int;
	ktime_t next_bcn;

	if (!data->started)
		goto out;

	ieee80211_iterate_active_interfaces_atomic(
		hw, IEEE80211_IFACE_ITER_NORMAL,
		mac80211_hwsim_beacon_tx, data);

	/* beacon at new TBTT + beacon interval */
	if (data->bcn_delta) {
		bcn_int -= data->bcn_delta;
		data->bcn_delta = 0;
	}

	next_bcn = ktime_add(hrtimer_get_expires(timer),
			     ns_to_ktime(bcn_int * 1000));
	tasklet_hrtimer_start(&data->beacon_timer, next_bcn, HRTIMER_MODE_ABS);
out:
	return HRTIMER_NORESTART;
}

static const char * const hwsim_chanwidths[] = {
	[NL80211_CHAN_WIDTH_20_NOHT] = "noht",
	[NL80211_CHAN_WIDTH_20] = "ht20",
	[NL80211_CHAN_WIDTH_40] = "ht40",
	[NL80211_CHAN_WIDTH_80] = "vht80",
	[NL80211_CHAN_WIDTH_80P80] = "vht80p80",
	[NL80211_CHAN_WIDTH_160] = "vht160",
};

static int mac80211_hwsim_config(struct ieee80211_hw *hw, u32 changed)
{
	struct mac80211_hwsim_data *data = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;
	static const char *smps_modes[IEEE80211_SMPS_NUM_MODES] = {
		[IEEE80211_SMPS_AUTOMATIC] = "auto",
		[IEEE80211_SMPS_OFF] = "off",
		[IEEE80211_SMPS_STATIC] = "static",
		[IEEE80211_SMPS_DYNAMIC] = "dynamic",
	};

	if (conf->chandef.chan)
		wiphy_debug(hw->wiphy,
			    "%s (freq=%d(%d - %d)/%s idle=%d ps=%d smps=%s)\n",
			    __func__,
			    conf->chandef.chan->center_freq,
			    conf->chandef.center_freq1,
			    conf->chandef.center_freq2,
			    hwsim_chanwidths[conf->chandef.width],
			    !!(conf->flags & IEEE80211_CONF_IDLE),
			    !!(conf->flags & IEEE80211_CONF_PS),
			    smps_modes[conf->smps_mode]);
	else
		wiphy_debug(hw->wiphy,
			    "%s (freq=0 idle=%d ps=%d smps=%s)\n",
			    __func__,
			    !!(conf->flags & IEEE80211_CONF_IDLE),
			    !!(conf->flags & IEEE80211_CONF_PS),
			    smps_modes[conf->smps_mode]);

	data->idle = !!(conf->flags & IEEE80211_CONF_IDLE);

	data->channel = conf->chandef.chan;

	WARN_ON(data->channel && channels > 1);

	data->power_level = conf->power_level;
	if (!data->started || !data->beacon_int)
		tasklet_hrtimer_cancel(&data->beacon_timer);
	else if (!hrtimer_is_queued(&data->beacon_timer.timer)) {
		u64 tsf = mac80211_hwsim_get_tsf(hw, NULL);
		u32 bcn_int = data->beacon_int;
		u64 until_tbtt = bcn_int - do_div(tsf, bcn_int);

		tasklet_hrtimer_start(&data->beacon_timer,
				      ns_to_ktime(until_tbtt * 1000),
				      HRTIMER_MODE_REL);
	}

	return 0;
}


static void mac80211_hwsim_configure_filter(struct ieee80211_hw *hw,
					    unsigned int changed_flags,
					    unsigned int *total_flags,u64 multicast)
{
	struct mac80211_hwsim_data *data = hw->priv;

	wiphy_debug(hw->wiphy, "%s\n", __func__);

	data->rx_filter = 0;
	if (*total_flags & FIF_PROMISC_IN_BSS)
		data->rx_filter |= FIF_PROMISC_IN_BSS;
	if (*total_flags & FIF_ALLMULTI)
		data->rx_filter |= FIF_ALLMULTI;

	*total_flags = data->rx_filter;
}

static void mac80211_hwsim_bss_info_changed(struct ieee80211_hw *hw,
					    struct ieee80211_vif *vif,
					    struct ieee80211_bss_conf *info,
					    u32 changed)
{
	struct hwsim_vif_priv *vp = (void *)vif->drv_priv;
	struct mac80211_hwsim_data *data = hw->priv;

	hwsim_check_magic(vif);

	wiphy_debug(hw->wiphy, "%s(changed=0x%x)\n", __func__, changed);

	if (changed & BSS_CHANGED_BSSID) {
		wiphy_debug(hw->wiphy, "%s: BSSID changed: %pM\n",
			    __func__, info->bssid);
		memcpy(vp->bssid, info->bssid, ETH_ALEN);
	}

	if (changed & BSS_CHANGED_ASSOC) {
		wiphy_debug(hw->wiphy, "  ASSOC: assoc=%d aid=%d\n",
			    info->assoc, info->aid);
		vp->assoc = info->assoc;
		vp->aid = info->aid;
	}

	if (changed & BSS_CHANGED_BEACON_INT) {
		wiphy_debug(hw->wiphy, "  BCNINT: %d\n", info->beacon_int);
		data->beacon_int = info->beacon_int * 1024;
	}

	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		wiphy_debug(hw->wiphy, "  BCN EN: %d\n", info->enable_beacon);
		if (data->started &&
		    !hrtimer_is_queued(&data->beacon_timer.timer) &&
		    info->enable_beacon) {
			u64 tsf, until_tbtt;
			u32 bcn_int;
			if (WARN_ON(!data->beacon_int))
				data->beacon_int = 1000 * 1024;
			tsf = mac80211_hwsim_get_tsf(hw, vif);
			bcn_int = data->beacon_int;
			until_tbtt = bcn_int - do_div(tsf, bcn_int);
			tasklet_hrtimer_start(&data->beacon_timer,
					      ns_to_ktime(until_tbtt * 1000),
					      HRTIMER_MODE_REL);
		} else if (!info->enable_beacon)
			tasklet_hrtimer_cancel(&data->beacon_timer);
	}

	if (changed & BSS_CHANGED_ERP_CTS_PROT) {
		wiphy_debug(hw->wiphy, "  ERP_CTS_PROT: %d\n",
			    info->use_cts_prot);
	}

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		wiphy_debug(hw->wiphy, "  ERP_PREAMBLE: %d\n",
			    info->use_short_preamble);
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		wiphy_debug(hw->wiphy, "  ERP_SLOT: %d\n", info->use_short_slot);
	}

	if (changed & BSS_CHANGED_HT) {
		wiphy_debug(hw->wiphy, "  HT: op_mode=0x%x\n",
			    info->ht_operation_mode);
	}

	if (changed & BSS_CHANGED_BASIC_RATES) {
		wiphy_debug(hw->wiphy, "  BASIC_RATES: 0x%llx\n",
			    (unsigned long long) info->basic_rates);
	}

	if (changed & BSS_CHANGED_TXPOWER)
		wiphy_debug(hw->wiphy, "  TX Power: %d dBm\n", info->txpower);
}

static int mac80211_hwsim_sta_add(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta)
{
	hwsim_check_magic(vif);
	hwsim_set_sta_magic(sta);

	return 0;
}

static int mac80211_hwsim_sta_remove(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct ieee80211_sta *sta)
{
	hwsim_check_magic(vif);
	hwsim_clear_sta_magic(sta);

	return 0;
}

static void mac80211_hwsim_sta_notify(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      enum sta_notify_cmd cmd,
				      struct ieee80211_sta *sta)
{
	hwsim_check_magic(vif);

	switch (cmd) {
	case STA_NOTIFY_SLEEP:
	case STA_NOTIFY_AWAKE:
		/* TODO: make good use of these flags */
		break;
	default:
		WARN(1, "Invalid sta notify: %d\n", cmd);
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
	struct ieee80211_hw *hw,
	struct ieee80211_vif *vif, u16 queue,
	const struct ieee80211_tx_queue_params *params)
{
	wiphy_debug(hw->wiphy,
		    "%s (queue=%d txop=%d cw_min=%d cw_max=%d aifs=%d)\n",
		    __func__, queue,
		    params->txop, params->cw_min,
		    params->cw_max, params->aifs);
	return 0;
}

static int mac80211_hwsim_get_survey(
	struct ieee80211_hw *hw, int idx,
	struct survey_info *survey)
{
	struct ieee80211_conf *conf = &hw->conf;

	wiphy_debug(hw->wiphy, "%s (idx=%d)\n", __func__, idx);

	if (idx != 0)
		return -ENOENT;

	/* Current channel */
	survey->channel = conf->chandef.chan;

	/*
	 * Magically conjured noise level --- this is only ok for simulated hardware.
	 *
	 * A real driver which cannot determine the real channel noise MUST NOT
	 * report any noise, especially not a magically conjured one :-)
	 */
	survey->filled = SURVEY_INFO_NOISE_DBM;
	survey->noise = -92;

	return 0;
}

#ifdef CONFIG_NL80211_TESTMODE
/*
 * This section contains example code for using netlink
 * attributes with the testmode command in nl80211.
 */

/* These enums need to be kept in sync with userspace */
enum hwsim_testmode_attr {
	__HWSIM_TM_ATTR_INVALID	= 0,
	HWSIM_TM_ATTR_CMD	= 1,
	HWSIM_TM_ATTR_PS	= 2,

	/* keep last */
	__HWSIM_TM_ATTR_AFTER_LAST,
	HWSIM_TM_ATTR_MAX	= __HWSIM_TM_ATTR_AFTER_LAST - 1
};

enum hwsim_testmode_cmd {
	HWSIM_TM_CMD_SET_PS		= 0,
	HWSIM_TM_CMD_GET_PS		= 1,
	HWSIM_TM_CMD_STOP_QUEUES	= 2,
	HWSIM_TM_CMD_WAKE_QUEUES	= 3,
};

static const struct nla_policy hwsim_testmode_policy[HWSIM_TM_ATTR_MAX + 1] = {
	[HWSIM_TM_ATTR_CMD] = { .type = NLA_U32 },
	[HWSIM_TM_ATTR_PS] = { .type = NLA_U32 },
};

static int hwsim_fops_ps_write(void *dat, u64 val);

static int mac80211_hwsim_testmode_cmd(struct ieee80211_hw *hw,
				       void *data, int len)
{
	struct mac80211_hwsim_data *hwsim = hw->priv;
	struct nlattr *tb[HWSIM_TM_ATTR_MAX + 1];
	struct sk_buff *skb;
	int err, ps;

	err = nla_parse(tb, HWSIM_TM_ATTR_MAX, data, len,
			hwsim_testmode_policy);
	if (err)
		return err;

	if (!tb[HWSIM_TM_ATTR_CMD])
		return -EINVAL;

	switch (nla_get_u32(tb[HWSIM_TM_ATTR_CMD])) {
	case HWSIM_TM_CMD_SET_PS:
		if (!tb[HWSIM_TM_ATTR_PS])
			return -EINVAL;
		ps = nla_get_u32(tb[HWSIM_TM_ATTR_PS]);
		return hwsim_fops_ps_write(hwsim, ps);
	case HWSIM_TM_CMD_GET_PS:
		skb = cfg80211_testmode_alloc_reply_skb(hw->wiphy,
						nla_total_size(sizeof(u32)));
		if (!skb)
			return -ENOMEM;
		if (nla_put_u32(skb, HWSIM_TM_ATTR_PS, hwsim->ps))
			goto nla_put_failure;
		return cfg80211_testmode_reply(skb);
	case HWSIM_TM_CMD_STOP_QUEUES:
		ieee80211_stop_queues(hw);
		return 0;
	case HWSIM_TM_CMD_WAKE_QUEUES:
		ieee80211_wake_queues(hw);
		return 0;
	default:
		return -EOPNOTSUPP;
	}

 nla_put_failure:
	kfree_skb(skb);
	return -ENOBUFS;
}
#endif

static int mac80211_hwsim_ampdu_action(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       enum ieee80211_ampdu_mlme_action action,
				       struct ieee80211_sta *sta, u16 tid, u16 *ssn,
				       u8 buf_size)
{
	switch (action) {
	case IEEE80211_AMPDU_TX_START:
		ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_STOP_CONT:
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		break;
	case IEEE80211_AMPDU_RX_START:
	case IEEE80211_AMPDU_RX_STOP:
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static void mac80211_hwsim_flush(struct ieee80211_hw *hw, u32 queues, bool drop)
{
	/* Not implemented, queues only on kernel side */
}

static void hw_scan_work(struct work_struct *work)
{
	struct mac80211_hwsim_data *hwsim =
		container_of(work, struct mac80211_hwsim_data, hw_scan.work);
	struct cfg80211_scan_request *req = hwsim->hw_scan_request;
	int dwell, i;

	mutex_lock(&hwsim->mutex);
	if (hwsim->scan_chan_idx >= req->n_channels) {
		wiphy_debug(hwsim->hw->wiphy, "hw scan complete\n");
		ieee80211_scan_completed(hwsim->hw, false);
		hwsim->hw_scan_request = NULL;
		hwsim->hw_scan_vif = NULL;
		hwsim->tmp_chan = NULL;
		mutex_unlock(&hwsim->mutex);
		return;
	}

	wiphy_debug(hwsim->hw->wiphy, "hw scan %d MHz\n",
		    req->channels[hwsim->scan_chan_idx]->center_freq);

	hwsim->tmp_chan = req->channels[hwsim->scan_chan_idx];
	if (hwsim->tmp_chan->flags & IEEE80211_CHAN_PASSIVE_SCAN ||
	    !req->n_ssids) {
		dwell = 120;
	} else {
		dwell = 30;
		/* send probes */
		for (i = 0; i < req->n_ssids; i++) {
			struct sk_buff *probe;

			probe = ieee80211_probereq_get(hwsim->hw,
						       hwsim->hw_scan_vif,
						       req->ssids[i].ssid,
						       req->ssids[i].ssid_len,
						       req->ie_len);
			if (!probe)
				continue;

			if (req->ie_len)
				memcpy(skb_put(probe, req->ie_len), req->ie,
				       req->ie_len);

			local_bh_disable();
			mac80211_hwsim_tx_frame(hwsim->hw, probe,
						hwsim->tmp_chan);
			local_bh_enable();
		}
	}
	ieee80211_queue_delayed_work(hwsim->hw, &hwsim->hw_scan,
				     msecs_to_jiffies(dwell));
	hwsim->scan_chan_idx++;
	mutex_unlock(&hwsim->mutex);
}

static int mac80211_hwsim_hw_scan(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct cfg80211_scan_request *req)
{
	struct mac80211_hwsim_data *hwsim = hw->priv;

	mutex_lock(&hwsim->mutex);
	if (WARN_ON(hwsim->tmp_chan || hwsim->hw_scan_request)) {
		mutex_unlock(&hwsim->mutex);
		return -EBUSY;
	}
	hwsim->hw_scan_request = req;
	hwsim->hw_scan_vif = vif;
	hwsim->scan_chan_idx = 0;
	mutex_unlock(&hwsim->mutex);

	wiphy_debug(hw->wiphy, "hwsim hw_scan request\n");

	ieee80211_queue_delayed_work(hwsim->hw, &hwsim->hw_scan, 0);

	return 0;
}

static void mac80211_hwsim_cancel_hw_scan(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif)
{
	struct mac80211_hwsim_data *hwsim = hw->priv;

	wiphy_debug(hw->wiphy, "hwsim cancel_hw_scan\n");

	cancel_delayed_work_sync(&hwsim->hw_scan);

	mutex_lock(&hwsim->mutex);
	ieee80211_scan_completed(hwsim->hw, true);
	hwsim->tmp_chan = NULL;
	hwsim->hw_scan_request = NULL;
	hwsim->hw_scan_vif = NULL;
	mutex_unlock(&hwsim->mutex);
}

static void mac80211_hwsim_sw_scan(struct ieee80211_hw *hw)
{
	struct mac80211_hwsim_data *hwsim = hw->priv;

	mutex_lock(&hwsim->mutex);

	if (hwsim->scanning) {
		printk(KERN_DEBUG "two hwsim sw_scans detected!\n");
		goto out;
	}

	printk(KERN_DEBUG "hwsim sw_scan request, prepping stuff\n");
	hwsim->scanning = true;

out:
	mutex_unlock(&hwsim->mutex);
}

static void mac80211_hwsim_sw_scan_complete(struct ieee80211_hw *hw)
{
	struct mac80211_hwsim_data *hwsim = hw->priv;

	mutex_lock(&hwsim->mutex);

	printk(KERN_DEBUG "hwsim sw_scan_complete\n");
	hwsim->scanning = false;

	mutex_unlock(&hwsim->mutex);
}

static void hw_roc_done(struct work_struct *work)
{
	struct mac80211_hwsim_data *hwsim =
		container_of(work, struct mac80211_hwsim_data, roc_done.work);

	mutex_lock(&hwsim->mutex);
	ieee80211_remain_on_channel_expired(hwsim->hw);
	hwsim->tmp_chan = NULL;
	mutex_unlock(&hwsim->mutex);

	wiphy_debug(hwsim->hw->wiphy, "hwsim ROC expired\n");
}

static int mac80211_hwsim_roc(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_channel *chan,
			      int duration,
			      enum ieee80211_roc_type type)
{
	struct mac80211_hwsim_data *hwsim = hw->priv;

	mutex_lock(&hwsim->mutex);
	if (WARN_ON(hwsim->tmp_chan || hwsim->hw_scan_request)) {
		mutex_unlock(&hwsim->mutex);
		return -EBUSY;
	}

	hwsim->tmp_chan = chan;
	mutex_unlock(&hwsim->mutex);

	wiphy_debug(hw->wiphy, "hwsim ROC (%d MHz, %d ms)\n",
		    chan->center_freq, duration);

	ieee80211_ready_on_channel(hw);

	ieee80211_queue_delayed_work(hw, &hwsim->roc_done,
				     msecs_to_jiffies(duration));
	return 0;
}

static int mac80211_hwsim_croc(struct ieee80211_hw *hw)
{
	struct mac80211_hwsim_data *hwsim = hw->priv;

	cancel_delayed_work_sync(&hwsim->roc_done);

	mutex_lock(&hwsim->mutex);
	hwsim->tmp_chan = NULL;
	mutex_unlock(&hwsim->mutex);

	wiphy_debug(hw->wiphy, "hwsim ROC canceled\n");

	return 0;
}

static int mac80211_hwsim_add_chanctx(struct ieee80211_hw *hw,
				      struct ieee80211_chanctx_conf *ctx)
{
	hwsim_set_chanctx_magic(ctx);
	wiphy_debug(hw->wiphy,
		    "add channel context control: %d MHz/width: %d/cfreqs:%d/%d MHz\n",
		    ctx->def.chan->center_freq, ctx->def.width,
		    ctx->def.center_freq1, ctx->def.center_freq2);
	return 0;
}

static void mac80211_hwsim_remove_chanctx(struct ieee80211_hw *hw,
					  struct ieee80211_chanctx_conf *ctx)
{
	wiphy_debug(hw->wiphy,
		    "remove channel context control: %d MHz/width: %d/cfreqs:%d/%d MHz\n",
		    ctx->def.chan->center_freq, ctx->def.width,
		    ctx->def.center_freq1, ctx->def.center_freq2);
	hwsim_check_chanctx_magic(ctx);
	hwsim_clear_chanctx_magic(ctx);
}

static void mac80211_hwsim_change_chanctx(struct ieee80211_hw *hw,
					  struct ieee80211_chanctx_conf *ctx,
					  u32 changed)
{
	hwsim_check_chanctx_magic(ctx);
	wiphy_debug(hw->wiphy,
		    "change channel context control: %d MHz/width: %d/cfreqs:%d/%d MHz\n",
		    ctx->def.chan->center_freq, ctx->def.width,
		    ctx->def.center_freq1, ctx->def.center_freq2);
}

static int mac80211_hwsim_assign_vif_chanctx(struct ieee80211_hw *hw,
					     struct ieee80211_vif *vif,
					     struct ieee80211_chanctx_conf *ctx)
{
	hwsim_check_magic(vif);
	hwsim_check_chanctx_magic(ctx);

	return 0;
}

static void mac80211_hwsim_unassign_vif_chanctx(struct ieee80211_hw *hw,
						struct ieee80211_vif *vif,
						struct ieee80211_chanctx_conf *ctx)
{
	hwsim_check_magic(vif);
	hwsim_check_chanctx_magic(ctx);
}

static struct ieee80211_ops mac80211_hwsim_ops =
{
	.tx = mac80211_hwsim_tx,
	.start = mac80211_hwsim_start,
	.stop = mac80211_hwsim_stop,
	.add_interface = mac80211_hwsim_add_interface,
	.change_interface = mac80211_hwsim_change_interface,
	.remove_interface = mac80211_hwsim_remove_interface,
	.config = mac80211_hwsim_config,
	.configure_filter = mac80211_hwsim_configure_filter,
	.bss_info_changed = mac80211_hwsim_bss_info_changed,
	.sta_add = mac80211_hwsim_sta_add,
	.sta_remove = mac80211_hwsim_sta_remove,
	.sta_notify = mac80211_hwsim_sta_notify,
	.set_tim = mac80211_hwsim_set_tim,
	.conf_tx = mac80211_hwsim_conf_tx,
	.get_survey = mac80211_hwsim_get_survey,
	CFG80211_TESTMODE_CMD(mac80211_hwsim_testmode_cmd)
	.ampdu_action = mac80211_hwsim_ampdu_action,
	.sw_scan_start = mac80211_hwsim_sw_scan,
	.sw_scan_complete = mac80211_hwsim_sw_scan_complete,
	.flush = mac80211_hwsim_flush,
	.get_tsf = mac80211_hwsim_get_tsf,
	.set_tsf = mac80211_hwsim_set_tsf,
};


static void mac80211_hwsim_free(void)
{
	struct list_head tmplist, *i, *tmp;
	struct mac80211_hwsim_data *data, *tmpdata;

	INIT_LIST_HEAD(&tmplist);

	spin_lock_bh(&hwsim_radio_lock);
	list_for_each_safe(i, tmp, &hwsim_radios)
		list_move(i, &tmplist);
	spin_unlock_bh(&hwsim_radio_lock);

	list_for_each_entry_safe(data, tmpdata, &tmplist, list) {
		debugfs_remove(data->debugfs_group);
		debugfs_remove(data->debugfs_ps);
		debugfs_remove(data->debugfs);
		ieee80211_unregister_hw(data->hw);
		device_release_driver(data->dev);
		device_unregister(data->dev);
		ieee80211_free_hw(data->hw);
	}
	class_destroy(hwsim_class);
}

static struct platform_driver mac80211_hwsim_driver = {
	.driver = {
		.name = "mac80211_hwsim",
		.owner = THIS_MODULE,
	},
};

static const struct net_device_ops hwsim_netdev_ops = {
	.ndo_start_xmit 	= hwsim_mon_xmit,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static void hwsim_mon_setup(struct net_device *dev)
{
	dev->netdev_ops = &hwsim_netdev_ops;
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
	struct sk_buff *skb;
	struct ieee80211_pspoll *pspoll;

	if (!vp->assoc)
		return;

	wiphy_debug(data->hw->wiphy,
		    "%s: send PS-Poll to %pM for aid %d\n",
		    __func__, vp->bssid, vp->aid);

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

	rcu_read_lock();
	mac80211_hwsim_tx_frame(data->hw, skb,
				rcu_dereference(vif->chanctx_conf)->def.chan);
	rcu_read_unlock();
}

static void hwsim_send_nullfunc(struct mac80211_hwsim_data *data, u8 *mac,
				struct ieee80211_vif *vif, int ps)
{
	struct hwsim_vif_priv *vp = (void *)vif->drv_priv;
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;

	if (!vp->assoc)
		return;

	wiphy_debug(data->hw->wiphy,
		    "%s: send data::nullfunc to %pM ps=%d\n",
		    __func__, vp->bssid, ps);

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

	rcu_read_lock();
	mac80211_hwsim_tx_frame(data->hw, skb,
				rcu_dereference(vif->chanctx_conf)->def.chan);
	rcu_read_unlock();
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
						    IEEE80211_IFACE_ITER_NORMAL,
						    hwsim_send_ps_poll, data);
		data->ps_poll_pending = true;
	} else if (old_ps == PS_DISABLED && val != PS_DISABLED) {
		ieee80211_iterate_active_interfaces(data->hw,
						    IEEE80211_IFACE_ITER_NORMAL,
						    hwsim_send_nullfunc_ps,
						    data);
	} else if (old_ps != PS_DISABLED && val == PS_DISABLED) {
		ieee80211_iterate_active_interfaces(data->hw,
						    IEEE80211_IFACE_ITER_NORMAL,
						    hwsim_send_nullfunc_no_ps,
						    data);
	}

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(hwsim_fops_ps, hwsim_fops_ps_read, hwsim_fops_ps_write,
			"%llu\n");


static int hwsim_fops_group_read(void *dat, u64 *val)
{
	struct mac80211_hwsim_data *data = dat;
	*val = data->group;
	return 0;
}

static int hwsim_fops_group_write(void *dat, u64 val)
{
	struct mac80211_hwsim_data *data = dat;
	data->group = val;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(hwsim_fops_group,
			hwsim_fops_group_read, hwsim_fops_group_write,
			"%llx\n");

static struct mac80211_hwsim_data *get_hwsim_data_ref_from_addr(
			     struct mac_address *addr)
{
	struct mac80211_hwsim_data *data;
	bool _found = false;

	spin_lock_bh(&hwsim_radio_lock);
	list_for_each_entry(data, &hwsim_radios, list) {
		if (memcmp(data->addresses[1].addr, addr,
			  sizeof(struct mac_address)) == 0) {
			_found = true;
			break;
		}
	}
	spin_unlock_bh(&hwsim_radio_lock);

	if (!_found)
		return NULL;

	return data;
}

static int hwsim_tx_info_frame_received_nl(struct sk_buff *skb_2,
					   struct genl_info *info)
{

	struct ieee80211_hdr *hdr;
	struct mac80211_hwsim_data *data2;
	struct ieee80211_tx_info *txi;
	struct hwsim_tx_rate *tx_attempts;
	unsigned long ret_skb_ptr;
	struct sk_buff *skb, *tmp;
	struct mac_address *src;
	unsigned int hwsim_flags;

	int i;
	bool found = false;

	if (!info->attrs[HWSIM_ATTR_ADDR_TRANSMITTER] ||
	   !info->attrs[HWSIM_ATTR_FLAGS] ||
	   !info->attrs[HWSIM_ATTR_COOKIE] ||
	   !info->attrs[HWSIM_ATTR_SIGNAL] ||
	   !info->attrs[HWSIM_ATTR_TX_INFO])
		goto out;

	src = (struct mac_address *)nla_data(
				   info->attrs[HWSIM_ATTR_ADDR_TRANSMITTER]);
	hwsim_flags = nla_get_u32(info->attrs[HWSIM_ATTR_FLAGS]);

	ret_skb_ptr = nla_get_u64(info->attrs[HWSIM_ATTR_COOKIE]);

	data2 = get_hwsim_data_ref_from_addr(src);

	if (data2 == NULL)
		goto out;

	/* look for the skb matching the cookie passed back from user */
	skb_queue_walk_safe(&data2->pending, skb, tmp) {
		if ((unsigned long)skb == ret_skb_ptr) {
			skb_unlink(skb, &data2->pending);
			found = true;
			break;
		}
	}

	/* not found */
	if (!found)
		goto out;

	/* Tx info received because the frame was broadcasted on user space,
	 so we get all the necessary info: tx attempts and skb control buff */

	tx_attempts = (struct hwsim_tx_rate *)nla_data(
		       info->attrs[HWSIM_ATTR_TX_INFO]);

	/* now send back TX status */
	txi = IEEE80211_SKB_CB(skb);

	ieee80211_tx_info_clear_status(txi);

	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		txi->status.rates[i].idx = tx_attempts[i].idx;
		txi->status.rates[i].count = tx_attempts[i].count;
		/*txi->status.rates[i].flags = 0;*/
	}

	txi->status.ack_signal = nla_get_u32(info->attrs[HWSIM_ATTR_SIGNAL]);

	if (!(hwsim_flags & HWSIM_TX_CTL_NO_ACK) &&
	   (hwsim_flags & HWSIM_TX_STAT_ACK)) {
		if (skb->len >= 16) {
			hdr = (struct ieee80211_hdr *) skb->data;
			mac80211_hwsim_monitor_ack(txi->rate_driver_data[0],
						   hdr->addr2);
		}
		txi->flags |= IEEE80211_TX_STAT_ACK;
	}
	ieee80211_tx_status_irqsafe(data2->hw, skb);
	return 0;
out:
	return -EINVAL;

}

static int hwsim_cloned_frame_received_nl(struct sk_buff *skb_2,
					  struct genl_info *info)
{

	struct mac80211_hwsim_data *data2;
	struct ieee80211_rx_status rx_status;
	struct mac_address *dst;
	int frame_data_len;
	char *frame_data;
	struct sk_buff *skb = NULL;

	if (!info->attrs[HWSIM_ATTR_ADDR_RECEIVER] ||
	    !info->attrs[HWSIM_ATTR_FRAME] ||
	    !info->attrs[HWSIM_ATTR_RX_RATE] ||
	    !info->attrs[HWSIM_ATTR_SIGNAL])
		goto out;

	dst = (struct mac_address *)nla_data(
				   info->attrs[HWSIM_ATTR_ADDR_RECEIVER]);

	frame_data_len = nla_len(info->attrs[HWSIM_ATTR_FRAME]);
	frame_data = (char *)nla_data(info->attrs[HWSIM_ATTR_FRAME]);

	/* Allocate new skb here */
	skb = alloc_skb(frame_data_len, GFP_KERNEL);
	if (skb == NULL)
		goto err;

	if (frame_data_len <= IEEE80211_MAX_DATA_LEN) {
		/* Copy the data */
		memcpy(skb_put(skb, frame_data_len), frame_data,
		       frame_data_len);
	} else
		goto err;

	data2 = get_hwsim_data_ref_from_addr(dst);

	if (data2 == NULL)
		goto out;

	/* check if radio is configured properly */

	if (data2->idle || !data2->started)
		goto out;

	/*A frame is received from user space*/
	memset(&rx_status, 0, sizeof(rx_status));
	rx_status.freq = data2->channel->center_freq;
	rx_status.band = data2->channel->band;
	rx_status.rate_idx = nla_get_u32(info->attrs[HWSIM_ATTR_RX_RATE]);
	rx_status.signal = nla_get_u32(info->attrs[HWSIM_ATTR_SIGNAL]);

	memcpy(IEEE80211_SKB_RXCB(skb), &rx_status, sizeof(rx_status));
	ieee80211_rx_irqsafe(data2->hw, skb);

	return 0;
err:
	printk(KERN_DEBUG "mac80211_hwsim: error occurred in %s\n", __func__);
	goto out;
out:
	dev_kfree_skb(skb);
	return -EINVAL;
}

static int hwsim_register_received_nl(struct sk_buff *skb_2,
				      struct genl_info *info)
{
	if (info == NULL)
		goto out;

	wmediumd_portid = info->snd_portid;

	printk(KERN_DEBUG "mac80211_hwsim: received a REGISTER, "
	       "switching to wmediumd mode with pid %d\n", info->snd_portid);

	return 0;
out:
	printk(KERN_DEBUG "mac80211_hwsim: error occurred in %s\n", __func__);
	return -EINVAL;
}

/* Generic Netlink operations array */
static struct genl_ops hwsim_ops[] = {
	{
		.cmd = HWSIM_CMD_REGISTER,
		.policy = hwsim_genl_policy,
		.doit = hwsim_register_received_nl,
		.flags = GENL_ADMIN_PERM,
	},
	{
		.cmd = HWSIM_CMD_FRAME,
		.policy = hwsim_genl_policy,
		.doit = hwsim_cloned_frame_received_nl,
	},
	{
		.cmd = HWSIM_CMD_TX_INFO_FRAME,
		.policy = hwsim_genl_policy,
		.doit = hwsim_tx_info_frame_received_nl,
	},
};

static int mac80211_hwsim_netlink_notify(struct notifier_block *nb,
					 unsigned long state,
					 void *_notify)
{
	struct netlink_notify *notify = _notify;

	if (state != NETLINK_URELEASE)
		return NOTIFY_DONE;

	if (notify->portid == wmediumd_portid) {
		printk(KERN_INFO "mac80211_hwsim: wmediumd released netlink"
		       " socket, switching to perfect channel medium\n");
		wmediumd_portid = 0;
	}
	return NOTIFY_DONE;

}

static struct notifier_block hwsim_netlink_notifier = {
	.notifier_call = mac80211_hwsim_netlink_notify,
};

static int hwsim_init_netlink(void)
{
	int rc;

	/* userspace test API hasn't been adjusted for multi-channel */
	if (channels > 1)
		return 0;

	printk(KERN_INFO "mac80211_hwsim: initializing netlink\n");

	rc = genl_register_family_with_ops(&hwsim_genl_family,
		hwsim_ops, ARRAY_SIZE(hwsim_ops));
	if (rc)
		goto failure;

	rc = netlink_register_notifier(&hwsim_netlink_notifier);
	if (rc)
		goto failure;

	return 0;

failure:
	printk(KERN_DEBUG "mac80211_hwsim: error occurred in %s\n", __func__);
	return -EINVAL;
}

static void hwsim_exit_netlink(void)
{
	int ret;

	/* userspace test API hasn't been adjusted for multi-channel */
	if (channels > 1)
		return;

	printk(KERN_INFO "mac80211_hwsim: closing netlink\n");
	/* unregister the notifier */
	netlink_unregister_notifier(&hwsim_netlink_notifier);
	/* unregister the family */
	ret = genl_unregister_family(&hwsim_genl_family);
	if (ret)
		printk(KERN_DEBUG "mac80211_hwsim: "
		       "unregister family %i\n", ret);
}

static const struct ieee80211_iface_limit hwsim_if_limits[] = {
	{ .max = 1, .types = BIT(NL80211_IFTYPE_ADHOC) },
	{ .max = 2048,  .types = BIT(NL80211_IFTYPE_STATION) |
				 BIT(NL80211_IFTYPE_P2P_CLIENT) |
#ifdef CONFIG_MAC80211_MESH
				 BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
				 BIT(NL80211_IFTYPE_AP) |
				 BIT(NL80211_IFTYPE_P2P_GO) },
	{ .max = 1, .types = BIT(NL80211_IFTYPE_P2P_DEVICE) },
};

static struct ieee80211_iface_combination hwsim_if_comb = {
	.limits = hwsim_if_limits,
	.n_limits = ARRAY_SIZE(hwsim_if_limits),
	.max_interfaces = 2048,
	.num_different_channels = 1,
};

static int __init init_mac80211_hwsim(void)
{
	int i, err = 0;
	u8 addr[ETH_ALEN];
	struct mac80211_hwsim_data *data;
	struct ieee80211_hw *hw;
	enum ieee80211_band band;

	if (radios < 1 || radios > 100)
		return -EINVAL;

	if (channels < 1)
		return -EINVAL;

	if (channels > 1) {
		hwsim_if_comb.num_different_channels = channels;
		mac80211_hwsim_ops.hw_scan = mac80211_hwsim_hw_scan;
		mac80211_hwsim_ops.cancel_hw_scan =
			mac80211_hwsim_cancel_hw_scan;
		mac80211_hwsim_ops.sw_scan_start = NULL;
		mac80211_hwsim_ops.sw_scan_complete = NULL;
		mac80211_hwsim_ops.remain_on_channel =
			mac80211_hwsim_roc;
		mac80211_hwsim_ops.cancel_remain_on_channel =
			mac80211_hwsim_croc;
		mac80211_hwsim_ops.add_chanctx =
			mac80211_hwsim_add_chanctx;
		mac80211_hwsim_ops.remove_chanctx =
			mac80211_hwsim_remove_chanctx;
		mac80211_hwsim_ops.change_chanctx =
			mac80211_hwsim_change_chanctx;
		mac80211_hwsim_ops.assign_vif_chanctx =
			mac80211_hwsim_assign_vif_chanctx;
		mac80211_hwsim_ops.unassign_vif_chanctx =
			mac80211_hwsim_unassign_vif_chanctx;
	}

	spin_lock_init(&hwsim_radio_lock);
	INIT_LIST_HEAD(&hwsim_radios);

	err = platform_driver_register(&mac80211_hwsim_driver);
	if (err)
		return err;

	hwsim_class = class_create(THIS_MODULE, "mac80211_hwsim");
	if (IS_ERR(hwsim_class)) {
		err = PTR_ERR(hwsim_class);
		goto failed_unregister_driver;
	}

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
			       "mac80211_hwsim: device_create failed (%ld)\n",
			       PTR_ERR(data->dev));
			err = -ENOMEM;
			goto failed_drvdata;
		}
		data->dev->driver = &mac80211_hwsim_driver.driver;
		err = device_bind_driver(data->dev);
		if (err != 0) {
			printk(KERN_DEBUG
			       "mac80211_hwsim: device_bind_driver failed (%d)\n",
			       err);
			goto failed_hw;
		}

		skb_queue_head_init(&data->pending);

		SET_IEEE80211_DEV(hw, data->dev);
		addr[3] = i >> 8;
		addr[4] = i;
		memcpy(data->addresses[0].addr, addr, ETH_ALEN);
		memcpy(data->addresses[1].addr, addr, ETH_ALEN);
		data->addresses[1].addr[0] |= 0x40;
		hw->wiphy->n_addresses = 2;
		hw->wiphy->addresses = data->addresses;

		hw->wiphy->iface_combinations = &hwsim_if_comb;
		hw->wiphy->n_iface_combinations = 1;

		if (channels > 1) {
			hw->wiphy->max_scan_ssids = 255;
			hw->wiphy->max_scan_ie_len = IEEE80211_MAX_DATA_LEN;
			hw->wiphy->max_remain_on_channel_duration = 1000;
		}

		INIT_DELAYED_WORK(&data->roc_done, hw_roc_done);
		INIT_DELAYED_WORK(&data->hw_scan, hw_scan_work);

		hw->channel_change_time = 1;
		hw->queues = 5;
		hw->offchannel_tx_hw_queue = 4;
		hw->wiphy->interface_modes =
			BIT(NL80211_IFTYPE_STATION) |
			BIT(NL80211_IFTYPE_AP) |
			BIT(NL80211_IFTYPE_P2P_CLIENT) |
			BIT(NL80211_IFTYPE_P2P_GO) |
			BIT(NL80211_IFTYPE_ADHOC) |
			BIT(NL80211_IFTYPE_MESH_POINT) |
			BIT(NL80211_IFTYPE_P2P_DEVICE);

		hw->flags = IEEE80211_HW_MFP_CAPABLE |
			    IEEE80211_HW_SIGNAL_DBM |
			    IEEE80211_HW_SUPPORTS_STATIC_SMPS |
			    IEEE80211_HW_SUPPORTS_DYNAMIC_SMPS |
			    IEEE80211_HW_AMPDU_AGGREGATION |
			    IEEE80211_HW_WANT_MONITOR_VIF |
			    IEEE80211_HW_QUEUE_CONTROL;
		if (rctbl)
			hw->flags |= IEEE80211_HW_SUPPORTS_RC_TABLE;

		hw->wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS |
				    WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;

		/* ask mac80211 to reserve space for magic */
		hw->vif_data_size = sizeof(struct hwsim_vif_priv);
		hw->sta_data_size = sizeof(struct hwsim_sta_priv);
		hw->chanctx_data_size = sizeof(struct hwsim_chanctx_priv);

		memcpy(data->channels_2ghz, hwsim_channels_2ghz,
			sizeof(hwsim_channels_2ghz));
		memcpy(data->channels_5ghz, hwsim_channels_5ghz,
			sizeof(hwsim_channels_5ghz));
		memcpy(data->rates, hwsim_rates, sizeof(hwsim_rates));

		for (band = IEEE80211_BAND_2GHZ; band < IEEE80211_NUM_BANDS; band++) {
			struct ieee80211_supported_band *sband = &data->bands[band];
			switch (band) {
			case IEEE80211_BAND_2GHZ:
				sband->channels = data->channels_2ghz;
				sband->n_channels =
					ARRAY_SIZE(hwsim_channels_2ghz);
				sband->bitrates = data->rates;
				sband->n_bitrates = ARRAY_SIZE(hwsim_rates);
				break;
			case IEEE80211_BAND_5GHZ:
				sband->channels = data->channels_5ghz;
				sband->n_channels =
					ARRAY_SIZE(hwsim_channels_5ghz);
				sband->bitrates = data->rates + 4;
				sband->n_bitrates = ARRAY_SIZE(hwsim_rates) - 4;
				break;
			default:
				continue;
			}

			sband->ht_cap.ht_supported = true;
			sband->ht_cap.cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
				IEEE80211_HT_CAP_GRN_FLD |
				IEEE80211_HT_CAP_SGI_40 |
				IEEE80211_HT_CAP_DSSSCCK40;
			sband->ht_cap.ampdu_factor = 0x3;
			sband->ht_cap.ampdu_density = 0x6;
			memset(&sband->ht_cap.mcs, 0,
			       sizeof(sband->ht_cap.mcs));
			sband->ht_cap.mcs.rx_mask[0] = 0xff;
			sband->ht_cap.mcs.rx_mask[1] = 0xff;
			sband->ht_cap.mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;

			hw->wiphy->bands[band] = sband;

			sband->vht_cap.vht_supported = true;
			sband->vht_cap.cap =
				IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 |
				IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ |
				IEEE80211_VHT_CAP_RXLDPC |
				IEEE80211_VHT_CAP_SHORT_GI_80 |
				IEEE80211_VHT_CAP_SHORT_GI_160 |
				IEEE80211_VHT_CAP_TXSTBC |
				IEEE80211_VHT_CAP_RXSTBC_1 |
				IEEE80211_VHT_CAP_RXSTBC_2 |
				IEEE80211_VHT_CAP_RXSTBC_3 |
				IEEE80211_VHT_CAP_RXSTBC_4 |
				IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK;
			sband->vht_cap.vht_mcs.rx_mcs_map =
				cpu_to_le16(IEEE80211_VHT_MCS_SUPPORT_0_8 << 0 |
					    IEEE80211_VHT_MCS_SUPPORT_0_8 << 2 |
					    IEEE80211_VHT_MCS_SUPPORT_0_9 << 4 |
					    IEEE80211_VHT_MCS_SUPPORT_0_8 << 6 |
					    IEEE80211_VHT_MCS_SUPPORT_0_8 << 8 |
					    IEEE80211_VHT_MCS_SUPPORT_0_9 << 10 |
					    IEEE80211_VHT_MCS_SUPPORT_0_9 << 12 |
					    IEEE80211_VHT_MCS_SUPPORT_0_8 << 14);
			sband->vht_cap.vht_mcs.tx_mcs_map =
				sband->vht_cap.vht_mcs.rx_mcs_map;
		}
		/* By default all radios are belonging to the first group */
		data->group = 1;
		mutex_init(&data->mutex);

		/* Enable frame retransmissions for lossy channels */
		hw->max_rates = 4;
		hw->max_rate_tries = 11;

		/* Work to be done prior to ieee80211_register_hw() */
		switch (regtest) {
		case HWSIM_REGTEST_DISABLED:
		case HWSIM_REGTEST_DRIVER_REG_FOLLOW:
		case HWSIM_REGTEST_DRIVER_REG_ALL:
		case HWSIM_REGTEST_DIFF_COUNTRY:
			/*
			 * Nothing to be done for driver regulatory domain
			 * hints prior to ieee80211_register_hw()
			 */
			break;
		case HWSIM_REGTEST_WORLD_ROAM:
			if (i == 0) {
				hw->wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY;
				wiphy_apply_custom_regulatory(hw->wiphy,
					&hwsim_world_regdom_custom_01);
			}
			break;
		case HWSIM_REGTEST_CUSTOM_WORLD:
			hw->wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY;
			wiphy_apply_custom_regulatory(hw->wiphy,
				&hwsim_world_regdom_custom_01);
			break;
		case HWSIM_REGTEST_CUSTOM_WORLD_2:
			if (i == 0) {
				hw->wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY;
				wiphy_apply_custom_regulatory(hw->wiphy,
					&hwsim_world_regdom_custom_01);
			} else if (i == 1) {
				hw->wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY;
				wiphy_apply_custom_regulatory(hw->wiphy,
					&hwsim_world_regdom_custom_02);
			}
			break;
		case HWSIM_REGTEST_STRICT_ALL:
			hw->wiphy->flags |= WIPHY_FLAG_STRICT_REGULATORY;
			break;
		case HWSIM_REGTEST_STRICT_FOLLOW:
		case HWSIM_REGTEST_STRICT_AND_DRIVER_REG:
			if (i == 0)
				hw->wiphy->flags |= WIPHY_FLAG_STRICT_REGULATORY;
			break;
		case HWSIM_REGTEST_ALL:
			if (i == 0) {
				hw->wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY;
				wiphy_apply_custom_regulatory(hw->wiphy,
					&hwsim_world_regdom_custom_01);
			} else if (i == 1) {
				hw->wiphy->flags |= WIPHY_FLAG_CUSTOM_REGULATORY;
				wiphy_apply_custom_regulatory(hw->wiphy,
					&hwsim_world_regdom_custom_02);
			} else if (i == 4)
				hw->wiphy->flags |= WIPHY_FLAG_STRICT_REGULATORY;
			break;
		default:
			break;
		}

		/* give the regulatory workqueue a chance to run */
		if (regtest)
			schedule_timeout_interruptible(1);
		err = ieee80211_register_hw(hw);
		if (err < 0) {
			printk(KERN_DEBUG "mac80211_hwsim: "
			       "ieee80211_register_hw failed (%d)\n", err);
			goto failed_hw;
		}

		/* Work to be done after to ieee80211_register_hw() */
		switch (regtest) {
		case HWSIM_REGTEST_WORLD_ROAM:
		case HWSIM_REGTEST_DISABLED:
			break;
		case HWSIM_REGTEST_DRIVER_REG_FOLLOW:
			if (!i)
				regulatory_hint(hw->wiphy, hwsim_alpha2s[0]);
			break;
		case HWSIM_REGTEST_DRIVER_REG_ALL:
		case HWSIM_REGTEST_STRICT_ALL:
			regulatory_hint(hw->wiphy, hwsim_alpha2s[0]);
			break;
		case HWSIM_REGTEST_DIFF_COUNTRY:
			if (i < ARRAY_SIZE(hwsim_alpha2s))
				regulatory_hint(hw->wiphy, hwsim_alpha2s[i]);
			break;
		case HWSIM_REGTEST_CUSTOM_WORLD:
		case HWSIM_REGTEST_CUSTOM_WORLD_2:
			/*
			 * Nothing to be done for custom world regulatory
			 * domains after to ieee80211_register_hw
			 */
			break;
		case HWSIM_REGTEST_STRICT_FOLLOW:
			if (i == 0)
				regulatory_hint(hw->wiphy, hwsim_alpha2s[0]);
			break;
		case HWSIM_REGTEST_STRICT_AND_DRIVER_REG:
			if (i == 0)
				regulatory_hint(hw->wiphy, hwsim_alpha2s[0]);
			else if (i == 1)
				regulatory_hint(hw->wiphy, hwsim_alpha2s[1]);
			break;
		case HWSIM_REGTEST_ALL:
			if (i == 2)
				regulatory_hint(hw->wiphy, hwsim_alpha2s[0]);
			else if (i == 3)
				regulatory_hint(hw->wiphy, hwsim_alpha2s[1]);
			else if (i == 4)
				regulatory_hint(hw->wiphy, hwsim_alpha2s[2]);
			break;
		default:
			break;
		}

		wiphy_debug(hw->wiphy, "hwaddr %pm registered\n",
			    hw->wiphy->perm_addr);

		data->debugfs = debugfs_create_dir("hwsim",
						   hw->wiphy->debugfsdir);
		data->debugfs_ps = debugfs_create_file("ps", 0666,
						       data->debugfs, data,
						       &hwsim_fops_ps);
		data->debugfs_group = debugfs_create_file("group", 0666,
							data->debugfs, data,
							&hwsim_fops_group);

		tasklet_hrtimer_init(&data->beacon_timer,
				     mac80211_hwsim_beacon,
				     CLOCK_REALTIME, HRTIMER_MODE_ABS);

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

	err = hwsim_init_netlink();
	if (err < 0)
		goto failed_nl;

	return 0;

failed_nl:
	printk(KERN_DEBUG "mac_80211_hwsim: failed initializing netlink\n");
	return err;

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
failed_unregister_driver:
	platform_driver_unregister(&mac80211_hwsim_driver);
	return err;
}
module_init(init_mac80211_hwsim);

static void __exit exit_mac80211_hwsim(void)
{
	printk(KERN_DEBUG "mac80211_hwsim: unregister radios\n");

	hwsim_exit_netlink();

	mac80211_hwsim_free();
	unregister_netdev(hwsim_mon);
	platform_driver_unregister(&mac80211_hwsim_driver);
}
module_exit(exit_mac80211_hwsim);
