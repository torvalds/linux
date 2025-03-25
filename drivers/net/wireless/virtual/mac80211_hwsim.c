// SPDX-License-Identifier: GPL-2.0-only
/*
 * mac80211_hwsim - software simulator of 802.11 radio(s) for mac80211
 * Copyright (c) 2008, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2011, Javier Lopez <jlopex@gmail.com>
 * Copyright (c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright (C) 2018 - 2024 Intel Corporation
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
#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <linux/rhashtable.h>
#include <linux/nospec.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include "mac80211_hwsim.h"

#define WARN_QUEUE 100
#define MAX_QUEUE 200

MODULE_AUTHOR("Jouni Malinen");
MODULE_DESCRIPTION("Software simulator of 802.11 radio(s) for mac80211");
MODULE_LICENSE("GPL");

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

static bool support_p2p_device = true;
module_param(support_p2p_device, bool, 0444);
MODULE_PARM_DESC(support_p2p_device, "Support P2P-Device interface type");

static bool mlo;
module_param(mlo, bool, 0444);
MODULE_PARM_DESC(mlo, "Support MLO");

static bool multi_radio;
module_param(multi_radio, bool, 0444);
MODULE_PARM_DESC(multi_radio, "Support Multiple Radios per wiphy");

/**
 * enum hwsim_regtest - the type of regulatory tests we offer
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
 * @HWSIM_REGTEST_STRICT_FOLLOW: Used for testing strict regulatory domain
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
 *
 * These are the different values you can use for the regtest
 * module parameter. This is useful to help test world roaming
 * and the driver regulatory_hint() call and combinations of these.
 * If you want to do specific alpha2 regulatory domain tests simply
 * use the userspace regulatory request as that will be respected as
 * well without the need of this module parameter. This is designed
 * only for testing the driver regulatory request, world roaming
 * and all possible combinations.
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
	.n_reg_rules = 5,
	.alpha2 =  "99",
	.reg_rules = {
		REG_RULE(2412-10, 2462+10, 40, 0, 20, 0),
		REG_RULE(2484-10, 2484+10, 40, 0, 20, 0),
		REG_RULE(5150-10, 5240+10, 40, 0, 30, 0),
		REG_RULE(5745-10, 5825+10, 40, 0, 30, 0),
		REG_RULE(5855-10, 5925+10, 40, 0, 33, 0),
	}
};

static const struct ieee80211_regdomain hwsim_world_regdom_custom_02 = {
	.n_reg_rules = 3,
	.alpha2 =  "99",
	.reg_rules = {
		REG_RULE(2412-10, 2462+10, 40, 0, 20, 0),
		REG_RULE(5725-10, 5850+10, 40, 0, 30,
			 NL80211_RRF_NO_IR),
		REG_RULE(5855-10, 5925+10, 40, 0, 33, 0),
	}
};

static const struct ieee80211_regdomain hwsim_world_regdom_custom_03 = {
	.n_reg_rules = 6,
	.alpha2 =  "99",
	.reg_rules = {
		REG_RULE(2412 - 10, 2462 + 10, 40, 0, 20, 0),
		REG_RULE(2484 - 10, 2484 + 10, 40, 0, 20, 0),
		REG_RULE(5150 - 10, 5240 + 10, 40, 0, 30, 0),
		REG_RULE(5745 - 10, 5825 + 10, 40, 0, 30, 0),
		REG_RULE(5855 - 10, 5925 + 10, 40, 0, 33, 0),
		REG_RULE(5955 - 10, 7125 + 10, 320, 0, 33, 0),
	}
};

static const struct ieee80211_regdomain hwsim_world_regdom_custom_04 = {
	.n_reg_rules = 6,
	.alpha2 =  "99",
	.reg_rules = {
		REG_RULE(2412 - 10, 2462 + 10, 40, 0, 20, 0),
		REG_RULE(2484 - 10, 2484 + 10, 40, 0, 20, 0),
		REG_RULE(5150 - 10, 5240 + 10, 80, 0, 30, NL80211_RRF_AUTO_BW),
		REG_RULE(5260 - 10, 5320 + 10, 80, 0, 30,
			 NL80211_RRF_DFS_CONCURRENT | NL80211_RRF_DFS |
			 NL80211_RRF_AUTO_BW),
		REG_RULE(5500 - 10, 5720 + 10, 160, 0, 30,
			 NL80211_RRF_DFS_CONCURRENT | NL80211_RRF_DFS),
		REG_RULE(5745 - 10, 5825 + 10, 80, 0, 30, 0),
		REG_RULE(5855 - 10, 5925 + 10, 80, 0, 33, 0),
	}
};

static const struct ieee80211_regdomain *hwsim_world_regdom_custom[] = {
	&hwsim_world_regdom_custom_01,
	&hwsim_world_regdom_custom_02,
	&hwsim_world_regdom_custom_03,
	&hwsim_world_regdom_custom_04,
};

struct hwsim_vif_priv {
	u32 magic;
	u32 skip_beacons[IEEE80211_MLD_MAX_NUM_LINKS];
	u8 bssid[ETH_ALEN];
	bool assoc;
	bool bcn_en;
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
	unsigned int last_link;
	u16 active_links_rx;
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

static unsigned int hwsim_net_id;

static DEFINE_IDA(hwsim_netgroup_ida);

struct hwsim_net {
	int netgroup;
	u32 wmediumd;
};

static inline int hwsim_net_get_netgroup(struct net *net)
{
	struct hwsim_net *hwsim_net = net_generic(net, hwsim_net_id);

	return hwsim_net->netgroup;
}

static inline int hwsim_net_set_netgroup(struct net *net)
{
	struct hwsim_net *hwsim_net = net_generic(net, hwsim_net_id);

	hwsim_net->netgroup = ida_alloc(&hwsim_netgroup_ida, GFP_KERNEL);
	return hwsim_net->netgroup >= 0 ? 0 : -ENOMEM;
}

static inline u32 hwsim_net_get_wmediumd(struct net *net)
{
	struct hwsim_net *hwsim_net = net_generic(net, hwsim_net_id);

	return hwsim_net->wmediumd;
}

static inline void hwsim_net_set_wmediumd(struct net *net, u32 portid)
{
	struct hwsim_net *hwsim_net = net_generic(net, hwsim_net_id);

	hwsim_net->wmediumd = portid;
}

static struct class *hwsim_class;

static struct net_device *hwsim_mon; /* global monitor netdev */

#define CHAN2G(_freq)  { \
	.band = NL80211_BAND_2GHZ, \
	.center_freq = (_freq), \
	.hw_value = (_freq), \
}

#define CHAN5G(_freq) { \
	.band = NL80211_BAND_5GHZ, \
	.center_freq = (_freq), \
	.hw_value = (_freq), \
}

#define CHAN6G(_freq) { \
	.band = NL80211_BAND_6GHZ, \
	.center_freq = (_freq), \
	.hw_value = (_freq), \
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
	CHAN5G(5845), /* Channel 169 */

	CHAN5G(5855), /* Channel 171 */
	CHAN5G(5860), /* Channel 172 */
	CHAN5G(5865), /* Channel 173 */
	CHAN5G(5870), /* Channel 174 */

	CHAN5G(5875), /* Channel 175 */
	CHAN5G(5880), /* Channel 176 */
	CHAN5G(5885), /* Channel 177 */
	CHAN5G(5890), /* Channel 178 */
	CHAN5G(5895), /* Channel 179 */
	CHAN5G(5900), /* Channel 180 */
	CHAN5G(5905), /* Channel 181 */

	CHAN5G(5910), /* Channel 182 */
	CHAN5G(5915), /* Channel 183 */
	CHAN5G(5920), /* Channel 184 */
	CHAN5G(5925), /* Channel 185 */
};

static const struct ieee80211_channel hwsim_channels_6ghz[] = {
	CHAN6G(5955), /* Channel 1 */
	CHAN6G(5975), /* Channel 5 */
	CHAN6G(5995), /* Channel 9 */
	CHAN6G(6015), /* Channel 13 */
	CHAN6G(6035), /* Channel 17 */
	CHAN6G(6055), /* Channel 21 */
	CHAN6G(6075), /* Channel 25 */
	CHAN6G(6095), /* Channel 29 */
	CHAN6G(6115), /* Channel 33 */
	CHAN6G(6135), /* Channel 37 */
	CHAN6G(6155), /* Channel 41 */
	CHAN6G(6175), /* Channel 45 */
	CHAN6G(6195), /* Channel 49 */
	CHAN6G(6215), /* Channel 53 */
	CHAN6G(6235), /* Channel 57 */
	CHAN6G(6255), /* Channel 61 */
	CHAN6G(6275), /* Channel 65 */
	CHAN6G(6295), /* Channel 69 */
	CHAN6G(6315), /* Channel 73 */
	CHAN6G(6335), /* Channel 77 */
	CHAN6G(6355), /* Channel 81 */
	CHAN6G(6375), /* Channel 85 */
	CHAN6G(6395), /* Channel 89 */
	CHAN6G(6415), /* Channel 93 */
	CHAN6G(6435), /* Channel 97 */
	CHAN6G(6455), /* Channel 181 */
	CHAN6G(6475), /* Channel 105 */
	CHAN6G(6495), /* Channel 109 */
	CHAN6G(6515), /* Channel 113 */
	CHAN6G(6535), /* Channel 117 */
	CHAN6G(6555), /* Channel 121 */
	CHAN6G(6575), /* Channel 125 */
	CHAN6G(6595), /* Channel 129 */
	CHAN6G(6615), /* Channel 133 */
	CHAN6G(6635), /* Channel 137 */
	CHAN6G(6655), /* Channel 141 */
	CHAN6G(6675), /* Channel 145 */
	CHAN6G(6695), /* Channel 149 */
	CHAN6G(6715), /* Channel 153 */
	CHAN6G(6735), /* Channel 157 */
	CHAN6G(6755), /* Channel 161 */
	CHAN6G(6775), /* Channel 165 */
	CHAN6G(6795), /* Channel 169 */
	CHAN6G(6815), /* Channel 173 */
	CHAN6G(6835), /* Channel 177 */
	CHAN6G(6855), /* Channel 181 */
	CHAN6G(6875), /* Channel 185 */
	CHAN6G(6895), /* Channel 189 */
	CHAN6G(6915), /* Channel 193 */
	CHAN6G(6935), /* Channel 197 */
	CHAN6G(6955), /* Channel 201 */
	CHAN6G(6975), /* Channel 205 */
	CHAN6G(6995), /* Channel 209 */
	CHAN6G(7015), /* Channel 213 */
	CHAN6G(7035), /* Channel 217 */
	CHAN6G(7055), /* Channel 221 */
	CHAN6G(7075), /* Channel 225 */
	CHAN6G(7095), /* Channel 229 */
	CHAN6G(7115), /* Channel 233 */
};

#define NUM_S1G_CHANS_US 51
static struct ieee80211_channel hwsim_channels_s1g[NUM_S1G_CHANS_US];

static const struct ieee80211_sta_s1g_cap hwsim_s1g_cap = {
	.s1g = true,
	.cap = { S1G_CAP0_SGI_1MHZ | S1G_CAP0_SGI_2MHZ,
		 0,
		 0,
		 S1G_CAP3_MAX_MPDU_LEN,
		 0,
		 S1G_CAP5_AMPDU,
		 0,
		 S1G_CAP7_DUP_1MHZ,
		 S1G_CAP8_TWT_RESPOND | S1G_CAP8_TWT_REQUEST,
		 0},
	.nss_mcs = { 0xfc | 1, /* MCS 7 for 1 SS */
	/* RX Highest Supported Long GI Data Rate 0:7 */
		     0,
	/* RX Highest Supported Long GI Data Rate 0:7 */
	/* TX S1G MCS Map 0:6 */
		     0xfa,
	/* TX S1G MCS Map :7 */
	/* TX Highest Supported Long GI Data Rate 0:6 */
		     0x80,
	/* TX Highest Supported Long GI Data Rate 7:8 */
	/* Rx Single spatial stream and S1G-MCS Map for 1MHz */
	/* Tx Single spatial stream and S1G-MCS Map for 1MHz */
		     0 },
};

static void hwsim_init_s1g_channels(struct ieee80211_channel *chans)
{
	int ch, freq;

	for (ch = 0; ch < NUM_S1G_CHANS_US; ch++) {
		freq = 902000 + (ch + 1) * 500;
		chans[ch].band = NL80211_BAND_S1GHZ;
		chans[ch].center_freq = KHZ_TO_MHZ(freq);
		chans[ch].freq_offset = freq % 1000;
		chans[ch].hw_value = ch + 1;
	}
}

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

#define DEFAULT_RX_RSSI -50

static const u32 hwsim_ciphers[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
	WLAN_CIPHER_SUITE_CCMP_256,
	WLAN_CIPHER_SUITE_GCMP,
	WLAN_CIPHER_SUITE_GCMP_256,
	WLAN_CIPHER_SUITE_AES_CMAC,
	WLAN_CIPHER_SUITE_BIP_CMAC_256,
	WLAN_CIPHER_SUITE_BIP_GMAC_128,
	WLAN_CIPHER_SUITE_BIP_GMAC_256,
};

#define OUI_QCA 0x001374
#define QCA_NL80211_SUBCMD_TEST 1
enum qca_nl80211_vendor_subcmds {
	QCA_WLAN_VENDOR_ATTR_TEST = 8,
	QCA_WLAN_VENDOR_ATTR_MAX = QCA_WLAN_VENDOR_ATTR_TEST
};

static const struct nla_policy
hwsim_vendor_test_policy[QCA_WLAN_VENDOR_ATTR_MAX + 1] = {
	[QCA_WLAN_VENDOR_ATTR_MAX] = { .type = NLA_U32 },
};

static int mac80211_hwsim_vendor_cmd_test(struct wiphy *wiphy,
					  struct wireless_dev *wdev,
					  const void *data, int data_len)
{
	struct sk_buff *skb;
	struct nlattr *tb[QCA_WLAN_VENDOR_ATTR_MAX + 1];
	int err;
	u32 val;

	err = nla_parse_deprecated(tb, QCA_WLAN_VENDOR_ATTR_MAX, data,
				   data_len, hwsim_vendor_test_policy, NULL);
	if (err)
		return err;
	if (!tb[QCA_WLAN_VENDOR_ATTR_TEST])
		return -EINVAL;
	val = nla_get_u32(tb[QCA_WLAN_VENDOR_ATTR_TEST]);
	wiphy_dbg(wiphy, "%s: test=%u\n", __func__, val);

	/* Send a vendor event as a test. Note that this would not normally be
	 * done within a command handler, but rather, based on some other
	 * trigger. For simplicity, this command is used to trigger the event
	 * here.
	 *
	 * event_idx = 0 (index in mac80211_hwsim_vendor_commands)
	 */
	skb = cfg80211_vendor_event_alloc(wiphy, wdev, 100, 0, GFP_KERNEL);
	if (skb) {
		/* skb_put() or nla_put() will fill up data within
		 * NL80211_ATTR_VENDOR_DATA.
		 */

		/* Add vendor data */
		nla_put_u32(skb, QCA_WLAN_VENDOR_ATTR_TEST, val + 1);

		/* Send the event - this will call nla_nest_end() */
		cfg80211_vendor_event(skb, GFP_KERNEL);
	}

	/* Send a response to the command */
	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, 10);
	if (!skb)
		return -ENOMEM;

	/* skb_put() or nla_put() will fill up data within
	 * NL80211_ATTR_VENDOR_DATA
	 */
	nla_put_u32(skb, QCA_WLAN_VENDOR_ATTR_TEST, val + 2);

	return cfg80211_vendor_cmd_reply(skb);
}

static struct wiphy_vendor_command mac80211_hwsim_vendor_commands[] = {
	{
		.info = { .vendor_id = OUI_QCA,
			  .subcmd = QCA_NL80211_SUBCMD_TEST },
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = mac80211_hwsim_vendor_cmd_test,
		.policy = hwsim_vendor_test_policy,
		.maxattr = QCA_WLAN_VENDOR_ATTR_MAX,
	}
};

/* Advertise support vendor specific events */
static const struct nl80211_vendor_cmd_info mac80211_hwsim_vendor_events[] = {
	{ .vendor_id = OUI_QCA, .subcmd = 1 },
};

static DEFINE_SPINLOCK(hwsim_radio_lock);
static LIST_HEAD(hwsim_radios);
static struct rhashtable hwsim_radios_rht;
static int hwsim_radio_idx;
static int hwsim_radios_generation = 1;

static struct platform_driver mac80211_hwsim_driver = {
	.driver = {
		.name = "mac80211_hwsim",
	},
};

struct mac80211_hwsim_link_data {
	u32 link_id;
	u64 beacon_int	/* beacon interval in us */;
	struct hrtimer beacon_timer;
};

struct mac80211_hwsim_data {
	struct list_head list;
	struct rhash_head rht;
	struct ieee80211_hw *hw;
	struct device *dev;
	struct ieee80211_supported_band bands[NUM_NL80211_BANDS];
	struct ieee80211_channel channels_2ghz[ARRAY_SIZE(hwsim_channels_2ghz)];
	struct ieee80211_channel channels_5ghz[ARRAY_SIZE(hwsim_channels_5ghz)];
	struct ieee80211_channel channels_6ghz[ARRAY_SIZE(hwsim_channels_6ghz)];
	struct ieee80211_channel channels_s1g[ARRAY_SIZE(hwsim_channels_s1g)];
	struct ieee80211_rate rates[ARRAY_SIZE(hwsim_rates)];
	struct ieee80211_iface_combination if_combination;
	struct ieee80211_iface_limit if_limits[3];
	int n_if_limits;

	struct ieee80211_iface_combination if_combination_radio;
	struct wiphy_radio_freq_range radio_range[NUM_NL80211_BANDS];
	struct wiphy_radio radio[NUM_NL80211_BANDS];

	u32 ciphers[ARRAY_SIZE(hwsim_ciphers)];

	struct mac_address addresses[2];
	int channels, idx;
	bool use_chanctx;
	bool destroy_on_close;
	u32 portid;
	char alpha2[2];
	const struct ieee80211_regdomain *regd;

	struct ieee80211_channel *tmp_chan;
	struct ieee80211_channel *roc_chan;
	u32 roc_duration;
	struct delayed_work roc_start;
	struct delayed_work roc_done;
	struct delayed_work hw_scan;
	struct cfg80211_scan_request *hw_scan_request;
	struct ieee80211_vif *hw_scan_vif;
	int scan_chan_idx;
	u8 scan_addr[ETH_ALEN];
	struct {
		struct ieee80211_channel *channel;
		unsigned long next_start, start, end;
	} survey_data[ARRAY_SIZE(hwsim_channels_2ghz) +
		      ARRAY_SIZE(hwsim_channels_5ghz) +
		      ARRAY_SIZE(hwsim_channels_6ghz)];

	struct ieee80211_channel *channel;
	enum nl80211_chan_width bw;
	unsigned int rx_filter;
	bool started, idle, scanning;
	struct mutex mutex;
	enum ps_mode {
		PS_DISABLED, PS_ENABLED, PS_AUTO_POLL, PS_MANUAL_POLL
	} ps;
	bool ps_poll_pending;
	struct dentry *debugfs;

	atomic_t pending_cookie;
	struct sk_buff_head pending;	/* packets pending */
	/*
	 * Only radios in the same group can communicate together (the
	 * channel has to match too). Each bit represents a group. A
	 * radio can be in more than one group.
	 */
	u64 group;

	/* group shared by radios created in the same netns */
	int netgroup;
	/* wmediumd portid responsible for netgroup of this radio */
	u32 wmediumd;

	/* difference between this hw's clock and the real clock, in usecs */
	s64 tsf_offset;
	s64 bcn_delta;
	/* absolute beacon transmission time. Used to cover up "tx" delay. */
	u64 abs_bcn_ts;

	/* Stats */
	u64 tx_pkts;
	u64 rx_pkts;
	u64 tx_bytes;
	u64 rx_bytes;
	u64 tx_dropped;
	u64 tx_failed;

	/* RSSI in rx status of the receiver */
	int rx_rssi;

	/* only used when pmsr capability is supplied */
	struct cfg80211_pmsr_capabilities pmsr_capa;
	struct cfg80211_pmsr_request *pmsr_request;
	struct wireless_dev *pmsr_request_wdev;

	struct mac80211_hwsim_link_data link_data[IEEE80211_MLD_MAX_NUM_LINKS];
};

static const struct rhashtable_params hwsim_rht_params = {
	.nelem_hint = 2,
	.automatic_shrinking = true,
	.key_len = ETH_ALEN,
	.key_offset = offsetof(struct mac80211_hwsim_data, addresses[1]),
	.head_offset = offsetof(struct mac80211_hwsim_data, rht),
};

struct hwsim_radiotap_hdr {
	struct ieee80211_radiotap_header_fixed hdr;
	__le64 rt_tsft;
	u8 rt_flags;
	u8 rt_rate;
	__le16 rt_channel;
	__le16 rt_chbitmask;
} __packed;

struct hwsim_radiotap_ack_hdr {
	struct ieee80211_radiotap_header_fixed hdr;
	u8 rt_flags;
	u8 pad;
	__le16 rt_channel;
	__le16 rt_chbitmask;
} __packed;

static struct mac80211_hwsim_data *get_hwsim_data_ref_from_addr(const u8 *addr)
{
	return rhashtable_lookup_fast(&hwsim_radios_rht, addr, hwsim_rht_params);
}

/* MAC80211_HWSIM netlink family */
static struct genl_family hwsim_genl_family;

enum hwsim_multicast_groups {
	HWSIM_MCGRP_CONFIG,
};

static const struct genl_multicast_group hwsim_mcgrps[] = {
	[HWSIM_MCGRP_CONFIG] = { .name = "config", },
};

/* MAC80211_HWSIM netlink policy */

static const struct nla_policy
hwsim_rate_info_policy[HWSIM_RATE_INFO_ATTR_MAX + 1] = {
	[HWSIM_RATE_INFO_ATTR_FLAGS] = { .type = NLA_U8 },
	[HWSIM_RATE_INFO_ATTR_MCS] = { .type = NLA_U8 },
	[HWSIM_RATE_INFO_ATTR_LEGACY] = { .type = NLA_U16 },
	[HWSIM_RATE_INFO_ATTR_NSS] = { .type = NLA_U8 },
	[HWSIM_RATE_INFO_ATTR_BW] = { .type = NLA_U8 },
	[HWSIM_RATE_INFO_ATTR_HE_GI] = { .type = NLA_U8 },
	[HWSIM_RATE_INFO_ATTR_HE_DCM] = { .type = NLA_U8 },
	[HWSIM_RATE_INFO_ATTR_HE_RU_ALLOC] = { .type = NLA_U8 },
	[HWSIM_RATE_INFO_ATTR_N_BOUNDED_CH] = { .type = NLA_U8 },
	[HWSIM_RATE_INFO_ATTR_EHT_GI] = { .type = NLA_U8 },
	[HWSIM_RATE_INFO_ATTR_EHT_RU_ALLOC] = { .type = NLA_U8 },
};

static const struct nla_policy
hwsim_ftm_result_policy[NL80211_PMSR_FTM_RESP_ATTR_MAX + 1] = {
	[NL80211_PMSR_FTM_RESP_ATTR_FAIL_REASON] = { .type = NLA_U32 },
	[NL80211_PMSR_FTM_RESP_ATTR_BURST_INDEX] = { .type = NLA_U16 },
	[NL80211_PMSR_FTM_RESP_ATTR_NUM_FTMR_ATTEMPTS] = { .type = NLA_U32 },
	[NL80211_PMSR_FTM_RESP_ATTR_NUM_FTMR_SUCCESSES] = { .type = NLA_U32 },
	[NL80211_PMSR_FTM_RESP_ATTR_BUSY_RETRY_TIME] = { .type = NLA_U8 },
	[NL80211_PMSR_FTM_RESP_ATTR_NUM_BURSTS_EXP] = { .type = NLA_U8 },
	[NL80211_PMSR_FTM_RESP_ATTR_BURST_DURATION] = { .type = NLA_U8 },
	[NL80211_PMSR_FTM_RESP_ATTR_FTMS_PER_BURST] = { .type = NLA_U8 },
	[NL80211_PMSR_FTM_RESP_ATTR_RSSI_AVG] = { .type = NLA_U32 },
	[NL80211_PMSR_FTM_RESP_ATTR_RSSI_SPREAD] = { .type = NLA_U32 },
	[NL80211_PMSR_FTM_RESP_ATTR_TX_RATE] = NLA_POLICY_NESTED(hwsim_rate_info_policy),
	[NL80211_PMSR_FTM_RESP_ATTR_RX_RATE] = NLA_POLICY_NESTED(hwsim_rate_info_policy),
	[NL80211_PMSR_FTM_RESP_ATTR_RTT_AVG] = { .type = NLA_U64 },
	[NL80211_PMSR_FTM_RESP_ATTR_RTT_VARIANCE] = { .type = NLA_U64 },
	[NL80211_PMSR_FTM_RESP_ATTR_RTT_SPREAD] = { .type = NLA_U64 },
	[NL80211_PMSR_FTM_RESP_ATTR_DIST_AVG] = { .type = NLA_U64 },
	[NL80211_PMSR_FTM_RESP_ATTR_DIST_VARIANCE] = { .type = NLA_U64 },
	[NL80211_PMSR_FTM_RESP_ATTR_DIST_SPREAD] = { .type = NLA_U64 },
	[NL80211_PMSR_FTM_RESP_ATTR_LCI] = { .type = NLA_STRING },
	[NL80211_PMSR_FTM_RESP_ATTR_CIVICLOC] = { .type = NLA_STRING },
};

static const struct nla_policy
hwsim_pmsr_resp_type_policy[NL80211_PMSR_TYPE_MAX + 1] = {
	[NL80211_PMSR_TYPE_FTM] = NLA_POLICY_NESTED(hwsim_ftm_result_policy),
};

static const struct nla_policy
hwsim_pmsr_resp_policy[NL80211_PMSR_RESP_ATTR_MAX + 1] = {
	[NL80211_PMSR_RESP_ATTR_STATUS] = { .type = NLA_U32 },
	[NL80211_PMSR_RESP_ATTR_HOST_TIME] = { .type = NLA_U64 },
	[NL80211_PMSR_RESP_ATTR_AP_TSF] = { .type = NLA_U64 },
	[NL80211_PMSR_RESP_ATTR_FINAL] = { .type = NLA_FLAG },
	[NL80211_PMSR_RESP_ATTR_DATA] = NLA_POLICY_NESTED(hwsim_pmsr_resp_type_policy),
};

static const struct nla_policy
hwsim_pmsr_peer_result_policy[NL80211_PMSR_PEER_ATTR_MAX + 1] = {
	[NL80211_PMSR_PEER_ATTR_ADDR] = NLA_POLICY_ETH_ADDR_COMPAT,
	[NL80211_PMSR_PEER_ATTR_CHAN] = { .type = NLA_REJECT },
	[NL80211_PMSR_PEER_ATTR_REQ] = { .type = NLA_REJECT },
	[NL80211_PMSR_PEER_ATTR_RESP] = NLA_POLICY_NESTED(hwsim_pmsr_resp_policy),
};

static const struct nla_policy
hwsim_pmsr_peers_result_policy[NL80211_PMSR_ATTR_MAX + 1] = {
	[NL80211_PMSR_ATTR_MAX_PEERS] = { .type = NLA_REJECT },
	[NL80211_PMSR_ATTR_REPORT_AP_TSF] = { .type = NLA_REJECT },
	[NL80211_PMSR_ATTR_RANDOMIZE_MAC_ADDR] = { .type = NLA_REJECT },
	[NL80211_PMSR_ATTR_TYPE_CAPA] = { .type = NLA_REJECT },
	[NL80211_PMSR_ATTR_PEERS] = NLA_POLICY_NESTED_ARRAY(hwsim_pmsr_peer_result_policy),
};

static const struct nla_policy
hwsim_ftm_capa_policy[NL80211_PMSR_FTM_CAPA_ATTR_MAX + 1] = {
	[NL80211_PMSR_FTM_CAPA_ATTR_ASAP] = { .type = NLA_FLAG },
	[NL80211_PMSR_FTM_CAPA_ATTR_NON_ASAP] = { .type = NLA_FLAG },
	[NL80211_PMSR_FTM_CAPA_ATTR_REQ_LCI] = { .type = NLA_FLAG },
	[NL80211_PMSR_FTM_CAPA_ATTR_REQ_CIVICLOC] = { .type = NLA_FLAG },
	[NL80211_PMSR_FTM_CAPA_ATTR_PREAMBLES] = { .type = NLA_U32 },
	[NL80211_PMSR_FTM_CAPA_ATTR_BANDWIDTHS] = { .type = NLA_U32 },
	[NL80211_PMSR_FTM_CAPA_ATTR_MAX_BURSTS_EXPONENT] = NLA_POLICY_MAX(NLA_U8, 15),
	[NL80211_PMSR_FTM_CAPA_ATTR_MAX_FTMS_PER_BURST] = NLA_POLICY_MAX(NLA_U8, 31),
	[NL80211_PMSR_FTM_CAPA_ATTR_TRIGGER_BASED] = { .type = NLA_FLAG },
	[NL80211_PMSR_FTM_CAPA_ATTR_NON_TRIGGER_BASED] = { .type = NLA_FLAG },
};

static const struct nla_policy
hwsim_pmsr_capa_type_policy[NL80211_PMSR_TYPE_MAX + 1] = {
	[NL80211_PMSR_TYPE_FTM] = NLA_POLICY_NESTED(hwsim_ftm_capa_policy),
};

static const struct nla_policy
hwsim_pmsr_capa_policy[NL80211_PMSR_ATTR_MAX + 1] = {
	[NL80211_PMSR_ATTR_MAX_PEERS] = { .type = NLA_U32 },
	[NL80211_PMSR_ATTR_REPORT_AP_TSF] = { .type = NLA_FLAG },
	[NL80211_PMSR_ATTR_RANDOMIZE_MAC_ADDR] = { .type = NLA_FLAG },
	[NL80211_PMSR_ATTR_TYPE_CAPA] = NLA_POLICY_NESTED(hwsim_pmsr_capa_type_policy),
	[NL80211_PMSR_ATTR_PEERS] = { .type = NLA_REJECT }, // only for request.
};

static const struct nla_policy hwsim_genl_policy[HWSIM_ATTR_MAX + 1] = {
	[HWSIM_ATTR_ADDR_RECEIVER] = NLA_POLICY_ETH_ADDR_COMPAT,
	[HWSIM_ATTR_ADDR_TRANSMITTER] = NLA_POLICY_ETH_ADDR_COMPAT,
	[HWSIM_ATTR_FRAME] = { .type = NLA_BINARY,
			       .len = IEEE80211_MAX_DATA_LEN },
	[HWSIM_ATTR_FLAGS] = { .type = NLA_U32 },
	[HWSIM_ATTR_RX_RATE] = { .type = NLA_U32 },
	[HWSIM_ATTR_SIGNAL] = { .type = NLA_U32 },
	[HWSIM_ATTR_TX_INFO] = { .type = NLA_BINARY,
				 .len = IEEE80211_TX_MAX_RATES *
					sizeof(struct hwsim_tx_rate)},
	[HWSIM_ATTR_COOKIE] = { .type = NLA_U64 },
	[HWSIM_ATTR_CHANNELS] = { .type = NLA_U32 },
	[HWSIM_ATTR_RADIO_ID] = { .type = NLA_U32 },
	[HWSIM_ATTR_REG_HINT_ALPHA2] = { .type = NLA_STRING, .len = 2 },
	[HWSIM_ATTR_REG_CUSTOM_REG] = { .type = NLA_U32 },
	[HWSIM_ATTR_REG_STRICT_REG] = { .type = NLA_FLAG },
	[HWSIM_ATTR_SUPPORT_P2P_DEVICE] = { .type = NLA_FLAG },
	[HWSIM_ATTR_USE_CHANCTX] = { .type = NLA_FLAG },
	[HWSIM_ATTR_DESTROY_RADIO_ON_CLOSE] = { .type = NLA_FLAG },
	[HWSIM_ATTR_RADIO_NAME] = { .type = NLA_STRING },
	[HWSIM_ATTR_NO_VIF] = { .type = NLA_FLAG },
	[HWSIM_ATTR_FREQ] = { .type = NLA_U32 },
	[HWSIM_ATTR_TX_INFO_FLAGS] = { .type = NLA_BINARY },
	[HWSIM_ATTR_PERM_ADDR] = NLA_POLICY_ETH_ADDR_COMPAT,
	[HWSIM_ATTR_IFTYPE_SUPPORT] = { .type = NLA_U32 },
	[HWSIM_ATTR_CIPHER_SUPPORT] = { .type = NLA_BINARY },
	[HWSIM_ATTR_MLO_SUPPORT] = { .type = NLA_FLAG },
	[HWSIM_ATTR_PMSR_SUPPORT] = NLA_POLICY_NESTED(hwsim_pmsr_capa_policy),
	[HWSIM_ATTR_PMSR_RESULT] = NLA_POLICY_NESTED(hwsim_pmsr_peers_result_policy),
	[HWSIM_ATTR_MULTI_RADIO] = { .type = NLA_FLAG },
};

#if IS_REACHABLE(CONFIG_VIRTIO)

/* MAC80211_HWSIM virtio queues */
static struct virtqueue *hwsim_vqs[HWSIM_NUM_VQS];
static bool hwsim_virtio_enabled;
static DEFINE_SPINLOCK(hwsim_virtio_lock);

static void hwsim_virtio_rx_work(struct work_struct *work);
static DECLARE_WORK(hwsim_virtio_rx, hwsim_virtio_rx_work);

static int hwsim_tx_virtio(struct mac80211_hwsim_data *data,
			   struct sk_buff *skb)
{
	struct scatterlist sg[1];
	unsigned long flags;
	int err;

	spin_lock_irqsave(&hwsim_virtio_lock, flags);
	if (!hwsim_virtio_enabled) {
		err = -ENODEV;
		goto out_free;
	}

	sg_init_one(sg, skb->head, skb_end_offset(skb));
	err = virtqueue_add_outbuf(hwsim_vqs[HWSIM_VQ_TX], sg, 1, skb,
				   GFP_ATOMIC);
	if (err)
		goto out_free;
	virtqueue_kick(hwsim_vqs[HWSIM_VQ_TX]);
	spin_unlock_irqrestore(&hwsim_virtio_lock, flags);
	return 0;

out_free:
	spin_unlock_irqrestore(&hwsim_virtio_lock, flags);
	nlmsg_free(skb);
	return err;
}
#else
/* cause a linker error if this ends up being needed */
extern int hwsim_tx_virtio(struct mac80211_hwsim_data *data,
			   struct sk_buff *skb);
#define hwsim_virtio_enabled false
#endif

static int hwsim_get_chanwidth(enum nl80211_chan_width bw)
{
	switch (bw) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
		return 20;
	case NL80211_CHAN_WIDTH_40:
		return 40;
	case NL80211_CHAN_WIDTH_80:
		return 80;
	case NL80211_CHAN_WIDTH_80P80:
	case NL80211_CHAN_WIDTH_160:
		return 160;
	case NL80211_CHAN_WIDTH_320:
		return 320;
	case NL80211_CHAN_WIDTH_5:
		return 5;
	case NL80211_CHAN_WIDTH_10:
		return 10;
	case NL80211_CHAN_WIDTH_1:
		return 1;
	case NL80211_CHAN_WIDTH_2:
		return 2;
	case NL80211_CHAN_WIDTH_4:
		return 4;
	case NL80211_CHAN_WIDTH_8:
		return 8;
	case NL80211_CHAN_WIDTH_16:
		return 16;
	}

	return INT_MAX;
}

static void mac80211_hwsim_tx_frame(struct ieee80211_hw *hw,
				    struct sk_buff *skb,
				    struct ieee80211_channel *chan);

/* sysfs attributes */
static void hwsim_send_ps_poll(void *dat, u8 *mac, struct ieee80211_vif *vif)
{
	struct mac80211_hwsim_data *data = dat;
	struct hwsim_vif_priv *vp = (void *)vif->drv_priv;
	struct sk_buff *skb;
	struct ieee80211_pspoll *pspoll;

	if (!vp->assoc)
		return;

	wiphy_dbg(data->hw->wiphy,
		  "%s: send PS-Poll to %pM for aid %d\n",
		  __func__, vp->bssid, vp->aid);

	skb = dev_alloc_skb(sizeof(*pspoll));
	if (!skb)
		return;
	pspoll = skb_put(skb, sizeof(*pspoll));
	pspoll->frame_control = cpu_to_le16(IEEE80211_FTYPE_CTL |
					    IEEE80211_STYPE_PSPOLL |
					    IEEE80211_FCTL_PM);
	pspoll->aid = cpu_to_le16(0xc000 | vp->aid);
	memcpy(pspoll->bssid, vp->bssid, ETH_ALEN);
	memcpy(pspoll->ta, mac, ETH_ALEN);

	rcu_read_lock();
	mac80211_hwsim_tx_frame(data->hw, skb,
				rcu_dereference(vif->bss_conf.chanctx_conf)->def.chan);
	rcu_read_unlock();
}

static void hwsim_send_nullfunc(struct mac80211_hwsim_data *data, u8 *mac,
				struct ieee80211_vif *vif, int ps)
{
	struct hwsim_vif_priv *vp = (void *)vif->drv_priv;
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;
	struct ieee80211_tx_info *cb;

	if (!vp->assoc)
		return;

	wiphy_dbg(data->hw->wiphy,
		  "%s: send data::nullfunc to %pM ps=%d\n",
		  __func__, vp->bssid, ps);

	skb = dev_alloc_skb(sizeof(*hdr));
	if (!skb)
		return;
	hdr = skb_put(skb, sizeof(*hdr) - ETH_ALEN);
	hdr->frame_control = cpu_to_le16(IEEE80211_FTYPE_DATA |
					 IEEE80211_STYPE_NULLFUNC |
					 IEEE80211_FCTL_TODS |
					 (ps ? IEEE80211_FCTL_PM : 0));
	hdr->duration_id = cpu_to_le16(0);
	memcpy(hdr->addr1, vp->bssid, ETH_ALEN);
	memcpy(hdr->addr2, mac, ETH_ALEN);
	memcpy(hdr->addr3, vp->bssid, ETH_ALEN);

	cb = IEEE80211_SKB_CB(skb);
	cb->control.rates[0].count = 1;
	cb->control.rates[1].idx = -1;

	rcu_read_lock();
	mac80211_hwsim_tx_frame(data->hw, skb,
				rcu_dereference(vif->bss_conf.chanctx_conf)->def.chan);
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

	if (val == PS_MANUAL_POLL) {
		if (data->ps != PS_ENABLED)
			return -EINVAL;
		local_bh_disable();
		ieee80211_iterate_active_interfaces_atomic(
			data->hw, IEEE80211_IFACE_ITER_NORMAL,
			hwsim_send_ps_poll, data);
		local_bh_enable();
		return 0;
	}
	old_ps = data->ps;
	data->ps = val;

	local_bh_disable();
	if (old_ps == PS_DISABLED && val != PS_DISABLED) {
		ieee80211_iterate_active_interfaces_atomic(
			data->hw, IEEE80211_IFACE_ITER_NORMAL,
			hwsim_send_nullfunc_ps, data);
	} else if (old_ps != PS_DISABLED && val == PS_DISABLED) {
		ieee80211_iterate_active_interfaces_atomic(
			data->hw, IEEE80211_IFACE_ITER_NORMAL,
			hwsim_send_nullfunc_no_ps, data);
	}
	local_bh_enable();

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(hwsim_fops_ps, hwsim_fops_ps_read, hwsim_fops_ps_write,
			 "%llu\n");

static int hwsim_write_simulate_radar(void *dat, u64 val)
{
	struct mac80211_hwsim_data *data = dat;

	ieee80211_radar_detected(data->hw, NULL);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(hwsim_simulate_radar, NULL,
			 hwsim_write_simulate_radar, "%llu\n");

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

DEFINE_DEBUGFS_ATTRIBUTE(hwsim_fops_group,
			 hwsim_fops_group_read, hwsim_fops_group_write,
			 "%llx\n");

static int hwsim_fops_rx_rssi_read(void *dat, u64 *val)
{
	struct mac80211_hwsim_data *data = dat;
	*val = data->rx_rssi;
	return 0;
}

static int hwsim_fops_rx_rssi_write(void *dat, u64 val)
{
	struct mac80211_hwsim_data *data = dat;
	int rssi = (int)val;

	if (rssi >= 0 || rssi < -100)
		return -EINVAL;

	data->rx_rssi = rssi;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(hwsim_fops_rx_rssi,
			 hwsim_fops_rx_rssi_read, hwsim_fops_rx_rssi_write,
			 "%lld\n");

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
	/* MLD not supported here */
	u32 bcn_int = data->link_data[0].beacon_int;
	u64 delta = abs(tsf - now);

	/* adjust after beaconing with new timestamp at old TBTT */
	if (tsf > now) {
		data->tsf_offset += delta;
		data->bcn_delta = do_div(delta, bcn_int);
	} else {
		data->tsf_offset -= delta;
		data->bcn_delta = -(s64)do_div(delta, bcn_int);
	}
}

static void mac80211_hwsim_monitor_rx(struct ieee80211_hw *hw,
				      struct sk_buff *tx_skb,
				      struct ieee80211_channel *chan)
{
	struct mac80211_hwsim_data *data = hw->priv;
	struct sk_buff *skb;
	struct hwsim_radiotap_hdr *hdr;
	u16 flags, bitrate;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_skb);
	struct ieee80211_rate *txrate = ieee80211_get_tx_rate(hw, info);

	if (!txrate)
		bitrate = 0;
	else
		bitrate = txrate->bitrate;

	if (!netif_running(hwsim_mon))
		return;

	skb = skb_copy_expand(tx_skb, sizeof(*hdr), 0, GFP_ATOMIC);
	if (skb == NULL)
		return;

	hdr = skb_push(skb, sizeof(*hdr));
	hdr->hdr.it_version = PKTHDR_RADIOTAP_VERSION;
	hdr->hdr.it_pad = 0;
	hdr->hdr.it_len = cpu_to_le16(sizeof(*hdr));
	hdr->hdr.it_present = cpu_to_le32((1 << IEEE80211_RADIOTAP_FLAGS) |
					  (1 << IEEE80211_RADIOTAP_RATE) |
					  (1 << IEEE80211_RADIOTAP_TSFT) |
					  (1 << IEEE80211_RADIOTAP_CHANNEL));
	hdr->rt_tsft = __mac80211_hwsim_get_tsf(data);
	hdr->rt_flags = 0;
	hdr->rt_rate = bitrate / 5;
	hdr->rt_channel = cpu_to_le16(chan->center_freq);
	flags = IEEE80211_CHAN_2GHZ;
	if (txrate && txrate->flags & IEEE80211_RATE_ERP_G)
		flags |= IEEE80211_CHAN_OFDM;
	else
		flags |= IEEE80211_CHAN_CCK;
	hdr->rt_chbitmask = cpu_to_le16(flags);

	skb->dev = hwsim_mon;
	skb_reset_mac_header(skb);
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
	struct hwsim_radiotap_ack_hdr *hdr;
	u16 flags;
	struct ieee80211_hdr *hdr11;

	if (!netif_running(hwsim_mon))
		return;

	skb = dev_alloc_skb(100);
	if (skb == NULL)
		return;

	hdr = skb_put(skb, sizeof(*hdr));
	hdr->hdr.it_version = PKTHDR_RADIOTAP_VERSION;
	hdr->hdr.it_pad = 0;
	hdr->hdr.it_len = cpu_to_le16(sizeof(*hdr));
	hdr->hdr.it_present = cpu_to_le32((1 << IEEE80211_RADIOTAP_FLAGS) |
					  (1 << IEEE80211_RADIOTAP_CHANNEL));
	hdr->rt_flags = 0;
	hdr->pad = 0;
	hdr->rt_channel = cpu_to_le16(chan->center_freq);
	flags = IEEE80211_CHAN_2GHZ;
	hdr->rt_chbitmask = cpu_to_le16(flags);

	hdr11 = skb_put(skb, 10);
	hdr11->frame_control = cpu_to_le16(IEEE80211_FTYPE_CTL |
					   IEEE80211_STYPE_ACK);
	hdr11->duration_id = cpu_to_le16(0);
	memcpy(hdr11->addr1, addr, ETH_ALEN);

	skb->dev = hwsim_mon;
	skb_reset_mac_header(skb);
	skb->ip_summed = CHECKSUM_UNNECESSARY;
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);
	memset(skb->cb, 0, sizeof(skb->cb));
	netif_rx(skb);
}

struct mac80211_hwsim_addr_match_data {
	u8 addr[ETH_ALEN];
	bool ret;
};

static void mac80211_hwsim_addr_iter(void *data, u8 *mac,
				     struct ieee80211_vif *vif)
{
	int i;
	struct mac80211_hwsim_addr_match_data *md = data;

	if (memcmp(mac, md->addr, ETH_ALEN) == 0) {
		md->ret = true;
		return;
	}

	/* Match the link address */
	for (i = 0; i < ARRAY_SIZE(vif->link_conf); i++) {
		struct ieee80211_bss_conf *conf;

		conf = rcu_dereference(vif->link_conf[i]);
		if (!conf)
			continue;

		if (memcmp(conf->addr, md->addr, ETH_ALEN) == 0) {
			md->ret = true;
			return;
		}
	}
}

static bool mac80211_hwsim_addr_match(struct mac80211_hwsim_data *data,
				      const u8 *addr)
{
	struct mac80211_hwsim_addr_match_data md = {
		.ret = false,
	};

	if (data->scanning && memcmp(addr, data->scan_addr, ETH_ALEN) == 0)
		return true;

	memcpy(md.addr, addr, ETH_ALEN);

	ieee80211_iterate_active_interfaces_atomic(data->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   mac80211_hwsim_addr_iter,
						   &md);

	return md.ret;
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
		    mac80211_hwsim_addr_match(data, skb->data + 4)) {
			data->ps_poll_pending = false;
			return true;
		}
		return false;
	}

	return true;
}

static int hwsim_unicast_netgroup(struct mac80211_hwsim_data *data,
				  struct sk_buff *skb, int portid)
{
	struct net *net;
	bool found = false;
	int res = -ENOENT;

	rcu_read_lock();
	for_each_net_rcu(net) {
		if (data->netgroup == hwsim_net_get_netgroup(net)) {
			res = genlmsg_unicast(net, skb, portid);
			found = true;
			break;
		}
	}
	rcu_read_unlock();

	if (!found)
		nlmsg_free(skb);

	return res;
}

static void mac80211_hwsim_config_mac_nl(struct ieee80211_hw *hw,
					 const u8 *addr, bool add)
{
	struct mac80211_hwsim_data *data = hw->priv;
	u32 _portid = READ_ONCE(data->wmediumd);
	struct sk_buff *skb;
	void *msg_head;

	WARN_ON(!is_valid_ether_addr(addr));

	if (!_portid && !hwsim_virtio_enabled)
		return;

	skb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!skb)
		return;

	msg_head = genlmsg_put(skb, 0, 0, &hwsim_genl_family, 0,
			       add ? HWSIM_CMD_ADD_MAC_ADDR :
				     HWSIM_CMD_DEL_MAC_ADDR);
	if (!msg_head) {
		pr_debug("mac80211_hwsim: problem with msg_head\n");
		goto nla_put_failure;
	}

	if (nla_put(skb, HWSIM_ATTR_ADDR_TRANSMITTER,
		    ETH_ALEN, data->addresses[1].addr))
		goto nla_put_failure;

	if (nla_put(skb, HWSIM_ATTR_ADDR_RECEIVER, ETH_ALEN, addr))
		goto nla_put_failure;

	genlmsg_end(skb, msg_head);

	if (hwsim_virtio_enabled)
		hwsim_tx_virtio(data, skb);
	else
		hwsim_unicast_netgroup(data, skb, _portid);
	return;
nla_put_failure:
	nlmsg_free(skb);
}

static inline u16 trans_tx_rate_flags_ieee2hwsim(struct ieee80211_tx_rate *rate)
{
	u16 result = 0;

	if (rate->flags & IEEE80211_TX_RC_USE_RTS_CTS)
		result |= MAC80211_HWSIM_TX_RC_USE_RTS_CTS;
	if (rate->flags & IEEE80211_TX_RC_USE_CTS_PROTECT)
		result |= MAC80211_HWSIM_TX_RC_USE_CTS_PROTECT;
	if (rate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
		result |= MAC80211_HWSIM_TX_RC_USE_SHORT_PREAMBLE;
	if (rate->flags & IEEE80211_TX_RC_MCS)
		result |= MAC80211_HWSIM_TX_RC_MCS;
	if (rate->flags & IEEE80211_TX_RC_GREEN_FIELD)
		result |= MAC80211_HWSIM_TX_RC_GREEN_FIELD;
	if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
		result |= MAC80211_HWSIM_TX_RC_40_MHZ_WIDTH;
	if (rate->flags & IEEE80211_TX_RC_DUP_DATA)
		result |= MAC80211_HWSIM_TX_RC_DUP_DATA;
	if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
		result |= MAC80211_HWSIM_TX_RC_SHORT_GI;
	if (rate->flags & IEEE80211_TX_RC_VHT_MCS)
		result |= MAC80211_HWSIM_TX_RC_VHT_MCS;
	if (rate->flags & IEEE80211_TX_RC_80_MHZ_WIDTH)
		result |= MAC80211_HWSIM_TX_RC_80_MHZ_WIDTH;
	if (rate->flags & IEEE80211_TX_RC_160_MHZ_WIDTH)
		result |= MAC80211_HWSIM_TX_RC_160_MHZ_WIDTH;

	return result;
}

static void mac80211_hwsim_tx_frame_nl(struct ieee80211_hw *hw,
				       struct sk_buff *my_skb,
				       int dst_portid,
				       struct ieee80211_channel *channel)
{
	struct sk_buff *skb;
	struct mac80211_hwsim_data *data = hw->priv;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) my_skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(my_skb);
	void *msg_head;
	unsigned int hwsim_flags = 0;
	int i;
	struct hwsim_tx_rate tx_attempts[IEEE80211_TX_MAX_RATES];
	struct hwsim_tx_rate_flag tx_attempts_flags[IEEE80211_TX_MAX_RATES];
	uintptr_t cookie;

	if (data->ps != PS_DISABLED)
		hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PM);
	/* If the queue contains MAX_QUEUE skb's drop some */
	if (skb_queue_len(&data->pending) >= MAX_QUEUE) {
		/* Dropping until WARN_QUEUE level */
		while (skb_queue_len(&data->pending) >= WARN_QUEUE) {
			ieee80211_free_txskb(hw, skb_dequeue(&data->pending));
			data->tx_dropped++;
		}
	}

	skb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (skb == NULL)
		goto nla_put_failure;

	msg_head = genlmsg_put(skb, 0, 0, &hwsim_genl_family, 0,
			       HWSIM_CMD_FRAME);
	if (msg_head == NULL) {
		pr_debug("mac80211_hwsim: problem with msg_head\n");
		goto nla_put_failure;
	}

	if (nla_put(skb, HWSIM_ATTR_ADDR_TRANSMITTER,
		    ETH_ALEN, data->addresses[1].addr))
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

	if (nla_put_u32(skb, HWSIM_ATTR_FREQ, channel->center_freq))
		goto nla_put_failure;

	/* We get the tx control (rate and retries) info*/

	for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
		tx_attempts[i].idx = info->status.rates[i].idx;
		tx_attempts_flags[i].idx = info->status.rates[i].idx;
		tx_attempts[i].count = info->status.rates[i].count;
		tx_attempts_flags[i].flags =
				trans_tx_rate_flags_ieee2hwsim(
						&info->status.rates[i]);
	}

	if (nla_put(skb, HWSIM_ATTR_TX_INFO,
		    sizeof(struct hwsim_tx_rate)*IEEE80211_TX_MAX_RATES,
		    tx_attempts))
		goto nla_put_failure;

	if (nla_put(skb, HWSIM_ATTR_TX_INFO_FLAGS,
		    sizeof(struct hwsim_tx_rate_flag) * IEEE80211_TX_MAX_RATES,
		    tx_attempts_flags))
		goto nla_put_failure;

	/* We create a cookie to identify this skb */
	cookie = atomic_inc_return(&data->pending_cookie);
	info->rate_driver_data[0] = (void *)cookie;
	if (nla_put_u64_64bit(skb, HWSIM_ATTR_COOKIE, cookie, HWSIM_ATTR_PAD))
		goto nla_put_failure;

	genlmsg_end(skb, msg_head);

	if (hwsim_virtio_enabled) {
		if (hwsim_tx_virtio(data, skb))
			goto err_free_txskb;
	} else {
		if (hwsim_unicast_netgroup(data, skb, dst_portid))
			goto err_free_txskb;
	}

	/* Enqueue the packet */
	skb_queue_tail(&data->pending, my_skb);
	data->tx_pkts++;
	data->tx_bytes += my_skb->len;
	return;

nla_put_failure:
	nlmsg_free(skb);
err_free_txskb:
	pr_debug("mac80211_hwsim: error occurred in %s\n", __func__);
	ieee80211_free_txskb(hw, my_skb);
	data->tx_failed++;
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
	int i;

	for (i = 0; i < ARRAY_SIZE(vif->link_conf); i++) {
		struct ieee80211_bss_conf *conf;
		struct ieee80211_chanctx_conf *chanctx;

		conf = rcu_dereference(vif->link_conf[i]);
		if (!conf)
			continue;

		chanctx = rcu_dereference(conf->chanctx_conf);
		if (!chanctx)
			continue;

		if (!hwsim_chans_compat(data->channel, chanctx->def.chan))
			continue;

		data->receive = true;
		return;
	}
}

static void mac80211_hwsim_add_vendor_rtap(struct sk_buff *skb)
{
	/*
	 * To enable this code, #define the HWSIM_RADIOTAP_OUI,
	 * e.g. like this:
	 * #define HWSIM_RADIOTAP_OUI "\x02\x00\x00"
	 * (but you should use a valid OUI, not that)
	 *
	 * If anyone wants to 'donate' a radiotap OUI/subns code
	 * please send a patch removing this #ifdef and changing
	 * the values accordingly.
	 */
#ifdef HWSIM_RADIOTAP_OUI
	struct ieee80211_radiotap_vendor_tlv *rtap;
	static const char vendor_data[8] = "ABCDEFGH";

	// Make sure no padding is needed
	BUILD_BUG_ON(sizeof(vendor_data) % 4);
	/* this is last radiotap info before the mac header, so
	 * skb_reset_mac_header for mac8022 to know the end of
	 * the radiotap TLV/beginning of the 802.11 header
	 */
	skb_reset_mac_header(skb);

	/*
	 * Note that this code requires the headroom in the SKB
	 * that was allocated earlier.
	 */
	rtap = skb_push(skb, sizeof(*rtap) + sizeof(vendor_data));

	rtap->len = cpu_to_le16(sizeof(*rtap) -
				sizeof(struct ieee80211_radiotap_tlv) +
				sizeof(vendor_data));
	rtap->type = cpu_to_le16(IEEE80211_RADIOTAP_VENDOR_NAMESPACE);

	rtap->content.oui[0] = HWSIM_RADIOTAP_OUI[0];
	rtap->content.oui[1] = HWSIM_RADIOTAP_OUI[1];
	rtap->content.oui[2] = HWSIM_RADIOTAP_OUI[2];
	rtap->content.oui_subtype = 127;
	/* clear reserved field */
	rtap->content.reserved = 0;
	rtap->content.vendor_type = 0;
	memcpy(rtap->content.data, vendor_data, sizeof(vendor_data));

	IEEE80211_SKB_RXCB(skb)->flag |= RX_FLAG_RADIOTAP_TLV_AT_END;
#endif
}

static void mac80211_hwsim_rx(struct mac80211_hwsim_data *data,
			      struct ieee80211_rx_status *rx_status,
			      struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;

	if (!ieee80211_has_morefrags(hdr->frame_control) &&
	    !is_multicast_ether_addr(hdr->addr1) &&
	    (ieee80211_is_mgmt(hdr->frame_control) ||
	     ieee80211_is_data(hdr->frame_control))) {
		struct ieee80211_sta *sta;
		unsigned int link_id;

		rcu_read_lock();
		sta = ieee80211_find_sta_by_link_addrs(data->hw, hdr->addr2,
						       hdr->addr1, &link_id);
		if (sta) {
			struct hwsim_sta_priv *sp = (void *)sta->drv_priv;

			if (ieee80211_has_pm(hdr->frame_control))
				sp->active_links_rx &= ~BIT(link_id);
			else
				sp->active_links_rx |= BIT(link_id);

			rx_status->link_valid = true;
			rx_status->link_id = link_id;
		}
		rcu_read_unlock();
	}

	memcpy(IEEE80211_SKB_RXCB(skb), rx_status, sizeof(*rx_status));

	mac80211_hwsim_add_vendor_rtap(skb);

	data->rx_pkts++;
	data->rx_bytes += skb->len;
	ieee80211_rx_irqsafe(data->hw, skb);
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
	rx_status.freq_offset = chan->freq_offset ? 1 : 0;
	rx_status.band = chan->band;
	if (info->control.rates[0].flags & IEEE80211_TX_RC_VHT_MCS) {
		rx_status.rate_idx =
			ieee80211_rate_get_vht_mcs(&info->control.rates[0]);
		rx_status.nss =
			ieee80211_rate_get_vht_nss(&info->control.rates[0]);
		rx_status.encoding = RX_ENC_VHT;
	} else {
		rx_status.rate_idx = info->control.rates[0].idx;
		if (info->control.rates[0].flags & IEEE80211_TX_RC_MCS)
			rx_status.encoding = RX_ENC_HT;
	}
	if (info->control.rates[0].flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
		rx_status.bw = RATE_INFO_BW_40;
	else if (info->control.rates[0].flags & IEEE80211_TX_RC_80_MHZ_WIDTH)
		rx_status.bw = RATE_INFO_BW_80;
	else if (info->control.rates[0].flags & IEEE80211_TX_RC_160_MHZ_WIDTH)
		rx_status.bw = RATE_INFO_BW_160;
	else
		rx_status.bw = RATE_INFO_BW_20;
	if (info->control.rates[0].flags & IEEE80211_TX_RC_SHORT_GI)
		rx_status.enc_flags |= RX_ENC_FLAG_SHORT_GI;
	/* TODO: simulate optional packet loss */
	rx_status.signal = data->rx_rssi;
	if (info->control.vif)
		rx_status.signal += info->control.vif->bss_conf.txpower;

	if (data->ps != PS_DISABLED)
		hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PM);

	/* release the skb's source info */
	skb_orphan(skb);
	skb_dst_drop(skb);
	skb->mark = 0;
	skb_ext_reset(skb);
	nf_reset_ct(skb);

	/*
	 * Get absolute mactime here so all HWs RX at the "same time", and
	 * absolute TX time for beacon mactime so the timestamp matches.
	 * Giving beacons a different mactime than non-beacons looks messy, but
	 * it helps the Toffset be exact and a ~10us mactime discrepancy
	 * probably doesn't really matter.
	 */
	if (ieee80211_is_beacon(hdr->frame_control) ||
	    ieee80211_is_probe_resp(hdr->frame_control)) {
		rx_status.boottime_ns = ktime_get_boottime_ns();
		now = data->abs_bcn_ts;
	} else {
		now = mac80211_hwsim_get_tsf_raw();
	}

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

		if (data->netgroup != data2->netgroup)
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

		mac80211_hwsim_rx(data2, &rx_status, nskb);
	}
	spin_unlock(&hwsim_radio_lock);

	return ack;
}

static struct ieee80211_bss_conf *
mac80211_hwsim_select_tx_link(struct mac80211_hwsim_data *data,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta,
			      struct ieee80211_hdr *hdr,
			      struct ieee80211_link_sta **link_sta)
{
	struct hwsim_sta_priv *sp = (void *)sta->drv_priv;
	int i;

	if (!ieee80211_vif_is_mld(vif))
		return &vif->bss_conf;

	WARN_ON(is_multicast_ether_addr(hdr->addr1));

	if (WARN_ON_ONCE(!sta || !sta->valid_links))
		return &vif->bss_conf;

	for (i = 0; i < ARRAY_SIZE(vif->link_conf); i++) {
		struct ieee80211_bss_conf *bss_conf;
		unsigned int link_id;

		/* round-robin the available link IDs */
		link_id = (sp->last_link + i + 1) % ARRAY_SIZE(vif->link_conf);

		if (!(vif->active_links & BIT(link_id)))
			continue;

		if (!(sp->active_links_rx & BIT(link_id)))
			continue;

		*link_sta = rcu_dereference(sta->link[link_id]);
		if (!*link_sta)
			continue;

		bss_conf = rcu_dereference(vif->link_conf[link_id]);
		if (WARN_ON_ONCE(!bss_conf))
			continue;

		/* can happen while switching links */
		if (!rcu_access_pointer(bss_conf->chanctx_conf))
			continue;

		sp->last_link = link_id;
		return bss_conf;
	}

	return NULL;
}

static void mac80211_hwsim_tx(struct ieee80211_hw *hw,
			      struct ieee80211_tx_control *control,
			      struct sk_buff *skb)
{
	struct mac80211_hwsim_data *data = hw->priv;
	struct ieee80211_tx_info *txi = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (void *)skb->data;
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct ieee80211_channel *channel;
	bool ack;
	enum nl80211_chan_width confbw = NL80211_CHAN_WIDTH_20_NOHT;
	u32 _portid, i;

	if (WARN_ON(skb->len < 10)) {
		/* Should not happen; just a sanity check for addr1 use */
		ieee80211_free_txskb(hw, skb);
		return;
	}

	if (!data->use_chanctx) {
		channel = data->channel;
		confbw = data->bw;
	} else if (txi->hw_queue == 4) {
		channel = data->tmp_chan;
	} else {
		u8 link = u32_get_bits(IEEE80211_SKB_CB(skb)->control.flags,
				       IEEE80211_TX_CTRL_MLO_LINK);
		struct ieee80211_vif *vif = txi->control.vif;
		struct ieee80211_link_sta *link_sta = NULL;
		struct ieee80211_sta *sta = control->sta;
		struct ieee80211_bss_conf *bss_conf;

		if (link != IEEE80211_LINK_UNSPECIFIED) {
			bss_conf = rcu_dereference(txi->control.vif->link_conf[link]);
			if (sta)
				link_sta = rcu_dereference(sta->link[link]);
		} else {
			bss_conf = mac80211_hwsim_select_tx_link(data, vif, sta,
								 hdr, &link_sta);
		}

		if (unlikely(!bss_conf)) {
			/* if it's an MLO STA, it might have deactivated all
			 * links temporarily - but we don't handle real PS in
			 * this code yet, so just drop the frame in that case
			 */
			WARN(link != IEEE80211_LINK_UNSPECIFIED || !sta || !sta->mlo,
			     "link:%d, sta:%pM, sta->mlo:%d\n",
			     link, sta ? sta->addr : NULL, sta ? sta->mlo : -1);
			ieee80211_free_txskb(hw, skb);
			return;
		}

		if (sta && sta->mlo) {
			if (WARN_ON(!link_sta)) {
				ieee80211_free_txskb(hw, skb);
				return;
			}
			/* address translation to link addresses on TX */
			ether_addr_copy(hdr->addr1, link_sta->addr);
			ether_addr_copy(hdr->addr2, bss_conf->addr);
			/* translate A3 only if it's the BSSID */
			if (!ieee80211_has_tods(hdr->frame_control) &&
			    !ieee80211_has_fromds(hdr->frame_control)) {
				if (ether_addr_equal(hdr->addr3, sta->addr))
					ether_addr_copy(hdr->addr3, link_sta->addr);
				else if (ether_addr_equal(hdr->addr3, vif->addr))
					ether_addr_copy(hdr->addr3, bss_conf->addr);
			}
			/* no need to look at A4, if present it's SA */
		}

		chanctx_conf = rcu_dereference(bss_conf->chanctx_conf);
		if (chanctx_conf) {
			channel = chanctx_conf->def.chan;
			confbw = chanctx_conf->def.width;
		} else {
			channel = NULL;
		}
	}

	if (WARN(!channel, "TX w/o channel - queue = %d\n", txi->hw_queue)) {
		ieee80211_free_txskb(hw, skb);
		return;
	}

	if (data->idle && !data->tmp_chan) {
		wiphy_dbg(hw->wiphy, "Trying to TX when idle - reject\n");
		ieee80211_free_txskb(hw, skb);
		return;
	}

	if (txi->control.vif)
		hwsim_check_magic(txi->control.vif);
	if (control->sta)
		hwsim_check_sta_magic(control->sta);

	if (ieee80211_hw_check(hw, SUPPORTS_RC_TABLE))
		ieee80211_get_tx_rates(txi->control.vif, control->sta, skb,
				       txi->control.rates,
				       ARRAY_SIZE(txi->control.rates));

	for (i = 0; i < ARRAY_SIZE(txi->control.rates); i++) {
		u16 rflags = txi->control.rates[i].flags;
		/* initialize to data->bw for 5/10 MHz handling */
		enum nl80211_chan_width bw = data->bw;

		if (txi->control.rates[i].idx == -1)
			break;

		if (rflags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			bw = NL80211_CHAN_WIDTH_40;
		else if (rflags & IEEE80211_TX_RC_80_MHZ_WIDTH)
			bw = NL80211_CHAN_WIDTH_80;
		else if (rflags & IEEE80211_TX_RC_160_MHZ_WIDTH)
			bw = NL80211_CHAN_WIDTH_160;

		if (WARN_ON(hwsim_get_chanwidth(bw) > hwsim_get_chanwidth(confbw)))
			return;
	}

	if (skb->len >= 24 + 8 &&
	    ieee80211_is_probe_resp(hdr->frame_control)) {
		/* fake header transmission time */
		struct ieee80211_mgmt *mgmt;
		struct ieee80211_rate *txrate;
		/* TODO: get MCS */
		int bitrate = 100;
		u64 ts;

		mgmt = (struct ieee80211_mgmt *)skb->data;
		txrate = ieee80211_get_tx_rate(hw, txi);
		if (txrate)
			bitrate = txrate->bitrate;
		ts = mac80211_hwsim_get_tsf_raw();
		mgmt->u.probe_resp.timestamp =
			cpu_to_le64(ts + data->tsf_offset +
				    24 * 8 * 10 / bitrate);
	}

	mac80211_hwsim_monitor_rx(hw, skb, channel);

	/* wmediumd mode check */
	_portid = READ_ONCE(data->wmediumd);

	if (_portid || hwsim_virtio_enabled)
		return mac80211_hwsim_tx_frame_nl(hw, skb, _portid, channel);

	/* NO wmediumd detected, perfect medium simulation */
	data->tx_pkts++;
	data->tx_bytes += skb->len;
	ack = mac80211_hwsim_tx_frame_no_nl(hw, skb, channel);

	if (ack && skb->len >= 16)
		mac80211_hwsim_monitor_ack(channel, hdr->addr2);

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
	wiphy_dbg(hw->wiphy, "%s\n", __func__);
	data->started = true;
	return 0;
}


static void mac80211_hwsim_stop(struct ieee80211_hw *hw, bool suspend)
{
	struct mac80211_hwsim_data *data = hw->priv;
	int i;

	data->started = false;

	for (i = 0; i < ARRAY_SIZE(data->link_data); i++)
		hrtimer_cancel(&data->link_data[i].beacon_timer);

	while (!skb_queue_empty(&data->pending))
		ieee80211_free_txskb(hw, skb_dequeue(&data->pending));

	wiphy_dbg(hw->wiphy, "%s\n", __func__);
}


static int mac80211_hwsim_add_interface(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif)
{
	wiphy_dbg(hw->wiphy, "%s (type=%d mac_addr=%pM)\n",
		  __func__, ieee80211_vif_type_p2p(vif),
		  vif->addr);
	hwsim_set_magic(vif);

	if (vif->type != NL80211_IFTYPE_MONITOR)
		mac80211_hwsim_config_mac_nl(hw, vif->addr, true);

	vif->cab_queue = 0;
	vif->hw_queue[IEEE80211_AC_VO] = 0;
	vif->hw_queue[IEEE80211_AC_VI] = 1;
	vif->hw_queue[IEEE80211_AC_BE] = 2;
	vif->hw_queue[IEEE80211_AC_BK] = 3;

	return 0;
}

#ifdef CONFIG_MAC80211_DEBUGFS
static void
mac80211_hwsim_link_add_debugfs(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_bss_conf *link_conf,
				struct dentry *dir)
{
	struct hwsim_vif_priv *vp = (void *)vif->drv_priv;

	debugfs_create_u32("skip_beacons", 0600, dir,
			   &vp->skip_beacons[link_conf->link_id]);
}
#endif

static int mac80211_hwsim_change_interface(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif,
					   enum nl80211_iftype newtype,
					   bool newp2p)
{
	newtype = ieee80211_iftype_p2p(newtype, newp2p);
	wiphy_dbg(hw->wiphy,
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
	wiphy_dbg(hw->wiphy, "%s (type=%d mac_addr=%pM)\n",
		  __func__, ieee80211_vif_type_p2p(vif),
		  vif->addr);
	hwsim_check_magic(vif);
	hwsim_clear_magic(vif);
	if (vif->type != NL80211_IFTYPE_MONITOR)
		mac80211_hwsim_config_mac_nl(hw, vif->addr, false);
}

static void mac80211_hwsim_tx_frame(struct ieee80211_hw *hw,
				    struct sk_buff *skb,
				    struct ieee80211_channel *chan)
{
	struct mac80211_hwsim_data *data = hw->priv;
	u32 _portid = READ_ONCE(data->wmediumd);

	if (ieee80211_hw_check(hw, SUPPORTS_RC_TABLE)) {
		struct ieee80211_tx_info *txi = IEEE80211_SKB_CB(skb);
		ieee80211_get_tx_rates(txi->control.vif, NULL, skb,
				       txi->control.rates,
				       ARRAY_SIZE(txi->control.rates));
	}

	mac80211_hwsim_monitor_rx(hw, skb, chan);

	if (_portid || hwsim_virtio_enabled)
		return mac80211_hwsim_tx_frame_nl(hw, skb, _portid, chan);

	data->tx_pkts++;
	data->tx_bytes += skb->len;
	mac80211_hwsim_tx_frame_no_nl(hw, skb, chan);
	dev_kfree_skb(skb);
}

static void __mac80211_hwsim_beacon_tx(struct ieee80211_bss_conf *link_conf,
				       struct mac80211_hwsim_data *data,
				       struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct sk_buff *skb)
{
	struct hwsim_vif_priv *vp = (void *)vif->drv_priv;
	struct ieee80211_tx_info *info;
	struct ieee80211_rate *txrate;
	struct ieee80211_mgmt *mgmt;
	/* TODO: get MCS */
	int bitrate = 100;

	if (vp->skip_beacons[link_conf->link_id]) {
		vp->skip_beacons[link_conf->link_id]--;
		dev_kfree_skb(skb);
		return;
	}

	info = IEEE80211_SKB_CB(skb);
	if (ieee80211_hw_check(hw, SUPPORTS_RC_TABLE))
		ieee80211_get_tx_rates(vif, NULL, skb,
				       info->control.rates,
				       ARRAY_SIZE(info->control.rates));

	txrate = ieee80211_get_tx_rate(hw, info);
	if (txrate)
		bitrate = txrate->bitrate;

	mgmt = (struct ieee80211_mgmt *) skb->data;
	/* fake header transmission time */
	data->abs_bcn_ts = mac80211_hwsim_get_tsf_raw();
	if (ieee80211_is_s1g_beacon(mgmt->frame_control)) {
		struct ieee80211_ext *ext = (void *) mgmt;

		ext->u.s1g_beacon.timestamp = cpu_to_le32(data->abs_bcn_ts +
							  data->tsf_offset +
							  10 * 8 * 10 /
							  bitrate);
	} else {
		mgmt->u.beacon.timestamp = cpu_to_le64(data->abs_bcn_ts +
						       data->tsf_offset +
						       24 * 8 * 10 /
						       bitrate);
	}

	mac80211_hwsim_tx_frame(hw, skb,
			rcu_dereference(link_conf->chanctx_conf)->def.chan);
}

static void mac80211_hwsim_beacon_tx(void *arg, u8 *mac,
				     struct ieee80211_vif *vif)
{
	struct mac80211_hwsim_link_data *link_data = arg;
	u32 link_id = link_data->link_id;
	struct ieee80211_bss_conf *link_conf;
	struct mac80211_hwsim_data *data =
		container_of(link_data, struct mac80211_hwsim_data,
			     link_data[link_id]);
	struct ieee80211_hw *hw = data->hw;
	struct sk_buff *skb;

	hwsim_check_magic(vif);

	link_conf = rcu_dereference(vif->link_conf[link_id]);
	if (!link_conf)
		return;

	if (vif->type != NL80211_IFTYPE_AP &&
	    vif->type != NL80211_IFTYPE_MESH_POINT &&
	    vif->type != NL80211_IFTYPE_ADHOC &&
	    vif->type != NL80211_IFTYPE_OCB)
		return;

	if (vif->mbssid_tx_vif && vif->mbssid_tx_vif != vif)
		return;

	if (vif->bss_conf.ema_ap) {
		struct ieee80211_ema_beacons *ema;
		u8 i = 0;

		ema = ieee80211_beacon_get_template_ema_list(hw, vif, link_id);
		if (!ema || !ema->cnt)
			return;

		for (i = 0; i < ema->cnt; i++) {
			__mac80211_hwsim_beacon_tx(link_conf, data, hw, vif,
						   ema->bcn[i].skb);
			ema->bcn[i].skb = NULL; /* Already freed */
		}
		ieee80211_beacon_free_ema_list(ema);
	} else {
		skb = ieee80211_beacon_get(hw, vif, link_id);
		if (!skb)
			return;

		__mac80211_hwsim_beacon_tx(link_conf, data, hw, vif, skb);
	}

	while ((skb = ieee80211_get_buffered_bc(hw, vif)) != NULL) {
		mac80211_hwsim_tx_frame(hw, skb,
			rcu_dereference(link_conf->chanctx_conf)->def.chan);
	}

	if (link_conf->csa_active && ieee80211_beacon_cntdwn_is_complete(vif, link_id))
		ieee80211_csa_finish(vif, link_id);

	if (link_conf->color_change_active &&
	    ieee80211_beacon_cntdwn_is_complete(vif, link_id))
		ieee80211_color_change_finish(vif, link_id);
}

static enum hrtimer_restart
mac80211_hwsim_beacon(struct hrtimer *timer)
{
	struct mac80211_hwsim_link_data *link_data =
		container_of(timer, struct mac80211_hwsim_link_data, beacon_timer);
	struct mac80211_hwsim_data *data =
		container_of(link_data, struct mac80211_hwsim_data,
			     link_data[link_data->link_id]);
	struct ieee80211_hw *hw = data->hw;
	u64 bcn_int = link_data->beacon_int;

	if (!data->started)
		return HRTIMER_NORESTART;

	ieee80211_iterate_active_interfaces_atomic(
		hw, IEEE80211_IFACE_ITER_NORMAL,
		mac80211_hwsim_beacon_tx, link_data);

	/* beacon at new TBTT + beacon interval */
	if (data->bcn_delta) {
		bcn_int -= data->bcn_delta;
		data->bcn_delta = 0;
	}
	hrtimer_forward_now(&link_data->beacon_timer,
			    ns_to_ktime(bcn_int * NSEC_PER_USEC));
	return HRTIMER_RESTART;
}

static const char * const hwsim_chanwidths[] = {
	[NL80211_CHAN_WIDTH_5] = "ht5",
	[NL80211_CHAN_WIDTH_10] = "ht10",
	[NL80211_CHAN_WIDTH_20_NOHT] = "noht",
	[NL80211_CHAN_WIDTH_20] = "ht20",
	[NL80211_CHAN_WIDTH_40] = "ht40",
	[NL80211_CHAN_WIDTH_80] = "vht80",
	[NL80211_CHAN_WIDTH_80P80] = "vht80p80",
	[NL80211_CHAN_WIDTH_160] = "vht160",
	[NL80211_CHAN_WIDTH_1] = "1MHz",
	[NL80211_CHAN_WIDTH_2] = "2MHz",
	[NL80211_CHAN_WIDTH_4] = "4MHz",
	[NL80211_CHAN_WIDTH_8] = "8MHz",
	[NL80211_CHAN_WIDTH_16] = "16MHz",
	[NL80211_CHAN_WIDTH_320] = "eht320",
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
	int idx;

	if (conf->chandef.chan)
		wiphy_dbg(hw->wiphy,
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
		wiphy_dbg(hw->wiphy,
			  "%s (freq=0 idle=%d ps=%d smps=%s)\n",
			  __func__,
			  !!(conf->flags & IEEE80211_CONF_IDLE),
			  !!(conf->flags & IEEE80211_CONF_PS),
			  smps_modes[conf->smps_mode]);

	data->idle = !!(conf->flags & IEEE80211_CONF_IDLE);

	WARN_ON(conf->chandef.chan && data->use_chanctx);

	mutex_lock(&data->mutex);
	if (data->scanning && conf->chandef.chan) {
		for (idx = 0; idx < ARRAY_SIZE(data->survey_data); idx++) {
			if (data->survey_data[idx].channel == data->channel) {
				data->survey_data[idx].start =
					data->survey_data[idx].next_start;
				data->survey_data[idx].end = jiffies;
				break;
			}
		}

		data->channel = conf->chandef.chan;
		data->bw = conf->chandef.width;

		for (idx = 0; idx < ARRAY_SIZE(data->survey_data); idx++) {
			if (data->survey_data[idx].channel &&
			    data->survey_data[idx].channel != data->channel)
				continue;
			data->survey_data[idx].channel = data->channel;
			data->survey_data[idx].next_start = jiffies;
			break;
		}
	} else {
		data->channel = conf->chandef.chan;
		data->bw = conf->chandef.width;
	}
	mutex_unlock(&data->mutex);

	for (idx = 0; idx < ARRAY_SIZE(data->link_data); idx++) {
		struct mac80211_hwsim_link_data *link_data =
			&data->link_data[idx];

		if (!data->started || !link_data->beacon_int) {
			hrtimer_cancel(&link_data->beacon_timer);
		} else if (!hrtimer_active(&link_data->beacon_timer)) {
			u64 tsf = mac80211_hwsim_get_tsf(hw, NULL);
			u32 bcn_int = link_data->beacon_int;
			u64 until_tbtt = bcn_int - do_div(tsf, bcn_int);

			hrtimer_start(&link_data->beacon_timer,
				      ns_to_ktime(until_tbtt * NSEC_PER_USEC),
				      HRTIMER_MODE_REL_SOFT);
		}
	}

	return 0;
}


static void mac80211_hwsim_configure_filter(struct ieee80211_hw *hw,
					    unsigned int changed_flags,
					    unsigned int *total_flags,u64 multicast)
{
	struct mac80211_hwsim_data *data = hw->priv;

	wiphy_dbg(hw->wiphy, "%s\n", __func__);

	data->rx_filter = 0;
	if (*total_flags & FIF_ALLMULTI)
		data->rx_filter |= FIF_ALLMULTI;
	if (*total_flags & FIF_MCAST_ACTION)
		data->rx_filter |= FIF_MCAST_ACTION;

	*total_flags = data->rx_filter;
}

static void mac80211_hwsim_bcn_en_iter(void *data, u8 *mac,
				       struct ieee80211_vif *vif)
{
	unsigned int *count = data;
	struct hwsim_vif_priv *vp = (void *)vif->drv_priv;

	if (vp->bcn_en)
		(*count)++;
}

static void mac80211_hwsim_vif_info_changed(struct ieee80211_hw *hw,
					    struct ieee80211_vif *vif,
					    u64 changed)
{
	struct hwsim_vif_priv *vp = (void *)vif->drv_priv;

	hwsim_check_magic(vif);

	wiphy_dbg(hw->wiphy, "%s(changed=0x%llx vif->addr=%pM)\n",
		  __func__, changed, vif->addr);

	if (changed & BSS_CHANGED_ASSOC) {
		wiphy_dbg(hw->wiphy, "  ASSOC: assoc=%d aid=%d\n",
			  vif->cfg.assoc, vif->cfg.aid);
		vp->assoc = vif->cfg.assoc;
		vp->aid = vif->cfg.aid;
	}

	if (vif->type == NL80211_IFTYPE_STATION &&
	    changed & (BSS_CHANGED_MLD_VALID_LINKS | BSS_CHANGED_MLD_TTLM)) {
		u16 usable_links = ieee80211_vif_usable_links(vif);

		if (vif->active_links != usable_links)
			ieee80211_set_active_links_async(vif, usable_links);
	}
}

static void mac80211_hwsim_link_info_changed(struct ieee80211_hw *hw,
					     struct ieee80211_vif *vif,
					     struct ieee80211_bss_conf *info,
					     u64 changed)
{
	struct hwsim_vif_priv *vp = (void *)vif->drv_priv;
	struct mac80211_hwsim_data *data = hw->priv;
	unsigned int link_id = info->link_id;
	struct mac80211_hwsim_link_data *link_data = &data->link_data[link_id];

	hwsim_check_magic(vif);

	wiphy_dbg(hw->wiphy, "%s(changed=0x%llx vif->addr=%pM, link id %u)\n",
		  __func__, (unsigned long long)changed, vif->addr, link_id);

	if (changed & BSS_CHANGED_BSSID) {
		wiphy_dbg(hw->wiphy, "%s: BSSID changed: %pM\n",
			  __func__, info->bssid);
		memcpy(vp->bssid, info->bssid, ETH_ALEN);
	}

	if (changed & BSS_CHANGED_BEACON_ENABLED) {
		wiphy_dbg(hw->wiphy, "  BCN EN: %d (BI=%u)\n",
			  info->enable_beacon, info->beacon_int);
		vp->bcn_en = info->enable_beacon;
		if (data->started &&
		    !hrtimer_active(&link_data->beacon_timer) &&
		    info->enable_beacon) {
			u64 tsf, until_tbtt;
			u32 bcn_int;
			link_data->beacon_int = info->beacon_int * 1024;
			tsf = mac80211_hwsim_get_tsf(hw, vif);
			bcn_int = link_data->beacon_int;
			until_tbtt = bcn_int - do_div(tsf, bcn_int);

			hrtimer_start(&link_data->beacon_timer,
				      ns_to_ktime(until_tbtt * NSEC_PER_USEC),
				      HRTIMER_MODE_REL_SOFT);
		} else if (!info->enable_beacon) {
			unsigned int count = 0;
			ieee80211_iterate_active_interfaces_atomic(
				data->hw, IEEE80211_IFACE_ITER_NORMAL,
				mac80211_hwsim_bcn_en_iter, &count);
			wiphy_dbg(hw->wiphy, "  beaconing vifs remaining: %u",
				  count);
			if (count == 0) {
				hrtimer_cancel(&link_data->beacon_timer);
				link_data->beacon_int = 0;
			}
		}
	}

	if (changed & BSS_CHANGED_ERP_CTS_PROT) {
		wiphy_dbg(hw->wiphy, "  ERP_CTS_PROT: %d\n",
			  info->use_cts_prot);
	}

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		wiphy_dbg(hw->wiphy, "  ERP_PREAMBLE: %d\n",
			  info->use_short_preamble);
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		wiphy_dbg(hw->wiphy, "  ERP_SLOT: %d\n", info->use_short_slot);
	}

	if (changed & BSS_CHANGED_HT) {
		wiphy_dbg(hw->wiphy, "  HT: op_mode=0x%x\n",
			  info->ht_operation_mode);
	}

	if (changed & BSS_CHANGED_BASIC_RATES) {
		wiphy_dbg(hw->wiphy, "  BASIC_RATES: 0x%llx\n",
			  (unsigned long long) info->basic_rates);
	}

	if (changed & BSS_CHANGED_TXPOWER)
		wiphy_dbg(hw->wiphy, "  TX Power: %d dBm\n", info->txpower);
}

static void
mac80211_hwsim_sta_rc_update(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_link_sta *link_sta,
			     u32 changed)
{
	struct mac80211_hwsim_data *data = hw->priv;
	struct ieee80211_sta *sta = link_sta->sta;
	u32 bw = U32_MAX;
	int link_id;

	rcu_read_lock();
	for (link_id = 0;
	     link_id < ARRAY_SIZE(vif->link_conf);
	     link_id++) {
		enum nl80211_chan_width confbw = NL80211_CHAN_WIDTH_20_NOHT;
		struct ieee80211_bss_conf *vif_conf;

		link_sta = rcu_dereference(sta->link[link_id]);

		if (!link_sta)
			continue;

		switch (link_sta->bandwidth) {
#define C(_bw) case IEEE80211_STA_RX_BW_##_bw: bw = _bw; break
		C(20);
		C(40);
		C(80);
		C(160);
		C(320);
#undef C
		}

		if (!data->use_chanctx) {
			confbw = data->bw;
		} else {
			struct ieee80211_chanctx_conf *chanctx_conf;

			vif_conf = rcu_dereference(vif->link_conf[link_id]);
			if (WARN_ON(!vif_conf))
				continue;

			chanctx_conf = rcu_dereference(vif_conf->chanctx_conf);

			if (!WARN_ON(!chanctx_conf))
				confbw = chanctx_conf->def.width;
		}

		WARN(bw > hwsim_get_chanwidth(confbw),
		     "intf %pM [link=%d]: bad STA %pM bandwidth %d MHz (%d) > channel config %d MHz (%d)\n",
		     vif->addr, link_id, sta->addr, bw, sta->deflink.bandwidth,
		     hwsim_get_chanwidth(data->bw), data->bw);


	}
	rcu_read_unlock();


}

static int mac80211_hwsim_sta_add(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta)
{
	struct hwsim_sta_priv *sp = (void *)sta->drv_priv;

	hwsim_check_magic(vif);
	hwsim_set_sta_magic(sta);
	mac80211_hwsim_sta_rc_update(hw, vif, &sta->deflink, 0);

	if (sta->valid_links) {
		WARN(hweight16(sta->valid_links) > 1,
		     "expect to add STA with single link, have 0x%x\n",
		     sta->valid_links);
		sp->active_links_rx = sta->valid_links;
	}

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

static int mac80211_hwsim_sta_state(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif,
				    struct ieee80211_sta *sta,
				    enum ieee80211_sta_state old_state,
				    enum ieee80211_sta_state new_state)
{
	if (new_state == IEEE80211_STA_NOTEXIST)
		return mac80211_hwsim_sta_remove(hw, vif, sta);

	if (old_state == IEEE80211_STA_NOTEXIST)
		return mac80211_hwsim_sta_add(hw, vif, sta);

	/*
	 * in an MLO connection, when client is authorized
	 * (AP station marked as such), enable all links
	 */
	if (ieee80211_vif_is_mld(vif) &&
	    vif->type == NL80211_IFTYPE_STATION &&
	    new_state == IEEE80211_STA_AUTHORIZED && !sta->tdls)
		ieee80211_set_active_links_async(vif,
						 ieee80211_vif_usable_links(vif));

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

static int mac80211_hwsim_conf_tx(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  unsigned int link_id, u16 queue,
				  const struct ieee80211_tx_queue_params *params)
{
	wiphy_dbg(hw->wiphy,
		  "%s (queue=%d txop=%d cw_min=%d cw_max=%d aifs=%d)\n",
		  __func__, queue,
		  params->txop, params->cw_min,
		  params->cw_max, params->aifs);
	return 0;
}

static int mac80211_hwsim_get_survey(struct ieee80211_hw *hw, int idx,
				     struct survey_info *survey)
{
	struct mac80211_hwsim_data *hwsim = hw->priv;

	if (idx < 0 || idx >= ARRAY_SIZE(hwsim->survey_data))
		return -ENOENT;

	mutex_lock(&hwsim->mutex);
	survey->channel = hwsim->survey_data[idx].channel;
	if (!survey->channel) {
		mutex_unlock(&hwsim->mutex);
		return -ENOENT;
	}

	/*
	 * Magically conjured dummy values --- this is only ok for simulated hardware.
	 *
	 * A real driver which cannot determine real values noise MUST NOT
	 * report any, especially not a magically conjured ones :-)
	 */
	survey->filled = SURVEY_INFO_NOISE_DBM |
			 SURVEY_INFO_TIME |
			 SURVEY_INFO_TIME_BUSY;
	survey->noise = -92;
	survey->time =
		jiffies_to_msecs(hwsim->survey_data[idx].end -
				 hwsim->survey_data[idx].start);
	/* report 12.5% of channel time is used */
	survey->time_busy = survey->time/8;
	mutex_unlock(&hwsim->mutex);

	return 0;
}

static enum ieee80211_neg_ttlm_res
mac80211_hwsim_can_neg_ttlm(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_neg_ttlm *neg_ttlm)
{
	u32 i;

	/* For testing purposes, accept if all TIDs are mapped to the same links
	 * set, otherwise reject.
	 */
	for (i = 0; i < IEEE80211_TTLM_NUM_TIDS; i++) {
		if (neg_ttlm->downlink[i] != neg_ttlm->uplink[i] ||
		    neg_ttlm->downlink[i] != neg_ttlm->downlink[0])
			return NEG_TTLM_RES_REJECT;
	}

	return NEG_TTLM_RES_ACCEPT;
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

static int mac80211_hwsim_testmode_cmd(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       void *data, int len)
{
	struct mac80211_hwsim_data *hwsim = hw->priv;
	struct nlattr *tb[HWSIM_TM_ATTR_MAX + 1];
	struct sk_buff *skb;
	int err, ps;

	err = nla_parse_deprecated(tb, HWSIM_TM_ATTR_MAX, data, len,
				   hwsim_testmode_policy, NULL);
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
				       struct ieee80211_ampdu_params *params)
{
	struct ieee80211_sta *sta = params->sta;
	enum ieee80211_ampdu_mlme_action action = params->action;
	u16 tid = params->tid;

	switch (action) {
	case IEEE80211_AMPDU_TX_START:
		return IEEE80211_AMPDU_TX_START_IMMEDIATE;
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

static void mac80211_hwsim_flush(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 u32 queues, bool drop)
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
		struct cfg80211_scan_info info = {
			.aborted = false,
		};

		wiphy_dbg(hwsim->hw->wiphy, "hw scan complete\n");
		ieee80211_scan_completed(hwsim->hw, &info);
		hwsim->hw_scan_request = NULL;
		hwsim->hw_scan_vif = NULL;
		hwsim->tmp_chan = NULL;
		mutex_unlock(&hwsim->mutex);
		mac80211_hwsim_config_mac_nl(hwsim->hw, hwsim->scan_addr,
					     false);
		return;
	}

	wiphy_dbg(hwsim->hw->wiphy, "hw scan %d MHz\n",
		  req->channels[hwsim->scan_chan_idx]->center_freq);

	hwsim->tmp_chan = req->channels[hwsim->scan_chan_idx];
	if (hwsim->tmp_chan->flags & (IEEE80211_CHAN_NO_IR |
				      IEEE80211_CHAN_RADAR) ||
	    !req->n_ssids) {
		dwell = 120;
	} else {
		dwell = 30;
		/* send probes */
		for (i = 0; i < req->n_ssids; i++) {
			struct sk_buff *probe;
			struct ieee80211_mgmt *mgmt;

			probe = ieee80211_probereq_get(hwsim->hw,
						       hwsim->scan_addr,
						       req->ssids[i].ssid,
						       req->ssids[i].ssid_len,
						       req->ie_len);
			if (!probe)
				continue;

			mgmt = (struct ieee80211_mgmt *) probe->data;
			memcpy(mgmt->da, req->bssid, ETH_ALEN);
			memcpy(mgmt->bssid, req->bssid, ETH_ALEN);

			if (req->ie_len)
				skb_put_data(probe, req->ie, req->ie_len);

			rcu_read_lock();
			if (!ieee80211_tx_prepare_skb(hwsim->hw,
						      hwsim->hw_scan_vif,
						      probe,
						      hwsim->tmp_chan->band,
						      NULL)) {
				rcu_read_unlock();
				kfree_skb(probe);
				continue;
			}

			local_bh_disable();
			mac80211_hwsim_tx_frame(hwsim->hw, probe,
						hwsim->tmp_chan);
			rcu_read_unlock();
			local_bh_enable();
		}
	}
	ieee80211_queue_delayed_work(hwsim->hw, &hwsim->hw_scan,
				     msecs_to_jiffies(dwell));
	hwsim->survey_data[hwsim->scan_chan_idx].channel = hwsim->tmp_chan;
	hwsim->survey_data[hwsim->scan_chan_idx].start = jiffies;
	hwsim->survey_data[hwsim->scan_chan_idx].end =
		jiffies + msecs_to_jiffies(dwell);
	hwsim->scan_chan_idx++;
	mutex_unlock(&hwsim->mutex);
}

static int mac80211_hwsim_hw_scan(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_scan_request *hw_req)
{
	struct mac80211_hwsim_data *hwsim = hw->priv;
	struct cfg80211_scan_request *req = &hw_req->req;

	mutex_lock(&hwsim->mutex);
	if (WARN_ON(hwsim->tmp_chan || hwsim->hw_scan_request)) {
		mutex_unlock(&hwsim->mutex);
		return -EBUSY;
	}
	hwsim->hw_scan_request = req;
	hwsim->hw_scan_vif = vif;
	hwsim->scan_chan_idx = 0;
	if (req->flags & NL80211_SCAN_FLAG_RANDOM_ADDR)
		get_random_mask_addr(hwsim->scan_addr,
				     hw_req->req.mac_addr,
				     hw_req->req.mac_addr_mask);
	else
		memcpy(hwsim->scan_addr, vif->addr, ETH_ALEN);
	memset(hwsim->survey_data, 0, sizeof(hwsim->survey_data));
	mutex_unlock(&hwsim->mutex);

	mac80211_hwsim_config_mac_nl(hw, hwsim->scan_addr, true);
	wiphy_dbg(hw->wiphy, "hwsim hw_scan request\n");

	ieee80211_queue_delayed_work(hwsim->hw, &hwsim->hw_scan, 0);

	return 0;
}

static void mac80211_hwsim_cancel_hw_scan(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif)
{
	struct mac80211_hwsim_data *hwsim = hw->priv;
	struct cfg80211_scan_info info = {
		.aborted = true,
	};

	wiphy_dbg(hw->wiphy, "hwsim cancel_hw_scan\n");

	cancel_delayed_work_sync(&hwsim->hw_scan);

	mutex_lock(&hwsim->mutex);
	ieee80211_scan_completed(hwsim->hw, &info);
	hwsim->tmp_chan = NULL;
	hwsim->hw_scan_request = NULL;
	hwsim->hw_scan_vif = NULL;
	mutex_unlock(&hwsim->mutex);
}

static void mac80211_hwsim_sw_scan(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   const u8 *mac_addr)
{
	struct mac80211_hwsim_data *hwsim = hw->priv;

	mutex_lock(&hwsim->mutex);

	if (hwsim->scanning) {
		pr_debug("two hwsim sw_scans detected!\n");
		goto out;
	}

	pr_debug("hwsim sw_scan request, prepping stuff\n");

	memcpy(hwsim->scan_addr, mac_addr, ETH_ALEN);
	mac80211_hwsim_config_mac_nl(hw, hwsim->scan_addr, true);
	hwsim->scanning = true;
	memset(hwsim->survey_data, 0, sizeof(hwsim->survey_data));

out:
	mutex_unlock(&hwsim->mutex);
}

static void mac80211_hwsim_sw_scan_complete(struct ieee80211_hw *hw,
					    struct ieee80211_vif *vif)
{
	struct mac80211_hwsim_data *hwsim = hw->priv;

	mutex_lock(&hwsim->mutex);

	pr_debug("hwsim sw_scan_complete\n");
	hwsim->scanning = false;
	mac80211_hwsim_config_mac_nl(hw, hwsim->scan_addr, false);
	eth_zero_addr(hwsim->scan_addr);

	mutex_unlock(&hwsim->mutex);
}

static void hw_roc_start(struct work_struct *work)
{
	struct mac80211_hwsim_data *hwsim =
		container_of(work, struct mac80211_hwsim_data, roc_start.work);

	mutex_lock(&hwsim->mutex);

	wiphy_dbg(hwsim->hw->wiphy, "hwsim ROC begins\n");
	hwsim->tmp_chan = hwsim->roc_chan;
	ieee80211_ready_on_channel(hwsim->hw);

	ieee80211_queue_delayed_work(hwsim->hw, &hwsim->roc_done,
				     msecs_to_jiffies(hwsim->roc_duration));

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

	wiphy_dbg(hwsim->hw->wiphy, "hwsim ROC expired\n");
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

	hwsim->roc_chan = chan;
	hwsim->roc_duration = duration;
	mutex_unlock(&hwsim->mutex);

	wiphy_dbg(hw->wiphy, "hwsim ROC (%d MHz, %d ms)\n",
		  chan->center_freq, duration);
	ieee80211_queue_delayed_work(hw, &hwsim->roc_start, HZ/50);

	return 0;
}

static int mac80211_hwsim_croc(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif)
{
	struct mac80211_hwsim_data *hwsim = hw->priv;

	cancel_delayed_work_sync(&hwsim->roc_start);
	cancel_delayed_work_sync(&hwsim->roc_done);

	mutex_lock(&hwsim->mutex);
	hwsim->tmp_chan = NULL;
	mutex_unlock(&hwsim->mutex);

	wiphy_dbg(hw->wiphy, "hwsim ROC canceled\n");

	return 0;
}

static int mac80211_hwsim_add_chanctx(struct ieee80211_hw *hw,
				      struct ieee80211_chanctx_conf *ctx)
{
	hwsim_set_chanctx_magic(ctx);
	wiphy_dbg(hw->wiphy,
		  "add channel context control: %d MHz/width: %d/cfreqs:%d/%d MHz\n",
		  ctx->def.chan->center_freq, ctx->def.width,
		  ctx->def.center_freq1, ctx->def.center_freq2);
	return 0;
}

static void mac80211_hwsim_remove_chanctx(struct ieee80211_hw *hw,
					  struct ieee80211_chanctx_conf *ctx)
{
	wiphy_dbg(hw->wiphy,
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
	wiphy_dbg(hw->wiphy,
		  "change channel context control: %d MHz/width: %d/cfreqs:%d/%d MHz\n",
		  ctx->def.chan->center_freq, ctx->def.width,
		  ctx->def.center_freq1, ctx->def.center_freq2);
}

static int mac80211_hwsim_assign_vif_chanctx(struct ieee80211_hw *hw,
					     struct ieee80211_vif *vif,
					     struct ieee80211_bss_conf *link_conf,
					     struct ieee80211_chanctx_conf *ctx)
{
	hwsim_check_magic(vif);
	hwsim_check_chanctx_magic(ctx);

	/* if we activate a link while already associated wake it up */
	if (vif->type == NL80211_IFTYPE_STATION && vif->cfg.assoc) {
		struct sk_buff *skb;

		skb = ieee80211_nullfunc_get(hw, vif, link_conf->link_id, true);
		if (skb) {
			local_bh_disable();
			mac80211_hwsim_tx_frame(hw, skb, ctx->def.chan);
			local_bh_enable();
		}
	}

	return 0;
}

static void mac80211_hwsim_unassign_vif_chanctx(struct ieee80211_hw *hw,
						struct ieee80211_vif *vif,
						struct ieee80211_bss_conf *link_conf,
						struct ieee80211_chanctx_conf *ctx)
{
	hwsim_check_magic(vif);
	hwsim_check_chanctx_magic(ctx);

	/* if we deactivate a link while associated suspend it first */
	if (vif->type == NL80211_IFTYPE_STATION && vif->cfg.assoc) {
		struct sk_buff *skb;

		skb = ieee80211_nullfunc_get(hw, vif, link_conf->link_id, true);
		if (skb) {
			struct ieee80211_hdr *hdr = (void *)skb->data;

			hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PM);

			local_bh_disable();
			mac80211_hwsim_tx_frame(hw, skb, ctx->def.chan);
			local_bh_enable();
		}
	}
}

static int mac80211_hwsim_switch_vif_chanctx(struct ieee80211_hw *hw,
					     struct ieee80211_vif_chanctx_switch *vifs,
					     int n_vifs,
					     enum ieee80211_chanctx_switch_mode mode)
{
	int i;

	if (n_vifs <= 0)
		return -EINVAL;

	wiphy_dbg(hw->wiphy,
		  "switch vif channel context mode: %u\n", mode);

	for (i = 0; i < n_vifs; i++) {
		hwsim_check_chanctx_magic(vifs[i].old_ctx);
		wiphy_dbg(hw->wiphy,
			  "switch vif channel context: %d MHz/width: %d/cfreqs:%d/%d MHz -> %d MHz/width: %d/cfreqs:%d/%d MHz\n",
			  vifs[i].old_ctx->def.chan->center_freq,
			  vifs[i].old_ctx->def.width,
			  vifs[i].old_ctx->def.center_freq1,
			  vifs[i].old_ctx->def.center_freq2,
			  vifs[i].new_ctx->def.chan->center_freq,
			  vifs[i].new_ctx->def.width,
			  vifs[i].new_ctx->def.center_freq1,
			  vifs[i].new_ctx->def.center_freq2);

		switch (mode) {
		case CHANCTX_SWMODE_REASSIGN_VIF:
			hwsim_check_chanctx_magic(vifs[i].new_ctx);
			break;
		case CHANCTX_SWMODE_SWAP_CONTEXTS:
			hwsim_set_chanctx_magic(vifs[i].new_ctx);
			hwsim_clear_chanctx_magic(vifs[i].old_ctx);
			break;
		default:
			WARN(1, "Invalid mode %d\n", mode);
		}
	}
	return 0;
}

static const char mac80211_hwsim_gstrings_stats[][ETH_GSTRING_LEN] = {
	"tx_pkts_nic",
	"tx_bytes_nic",
	"rx_pkts_nic",
	"rx_bytes_nic",
	"d_tx_dropped",
	"d_tx_failed",
	"d_ps_mode",
	"d_group",
};

#define MAC80211_HWSIM_SSTATS_LEN ARRAY_SIZE(mac80211_hwsim_gstrings_stats)

static void mac80211_hwsim_get_et_strings(struct ieee80211_hw *hw,
					  struct ieee80211_vif *vif,
					  u32 sset, u8 *data)
{
	if (sset == ETH_SS_STATS)
		memcpy(data, mac80211_hwsim_gstrings_stats,
		       sizeof(mac80211_hwsim_gstrings_stats));
}

static int mac80211_hwsim_get_et_sset_count(struct ieee80211_hw *hw,
					    struct ieee80211_vif *vif, int sset)
{
	if (sset == ETH_SS_STATS)
		return MAC80211_HWSIM_SSTATS_LEN;
	return 0;
}

static void mac80211_hwsim_get_et_stats(struct ieee80211_hw *hw,
					struct ieee80211_vif *vif,
					struct ethtool_stats *stats, u64 *data)
{
	struct mac80211_hwsim_data *ar = hw->priv;
	int i = 0;

	data[i++] = ar->tx_pkts;
	data[i++] = ar->tx_bytes;
	data[i++] = ar->rx_pkts;
	data[i++] = ar->rx_bytes;
	data[i++] = ar->tx_dropped;
	data[i++] = ar->tx_failed;
	data[i++] = ar->ps;
	data[i++] = ar->group;

	WARN_ON(i != MAC80211_HWSIM_SSTATS_LEN);
}

static int mac80211_hwsim_tx_last_beacon(struct ieee80211_hw *hw)
{
	return 1;
}

static int mac80211_hwsim_set_rts_threshold(struct ieee80211_hw *hw, u32 value)
{
	return -EOPNOTSUPP;
}

static int mac80211_hwsim_change_vif_links(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif,
					   u16 old_links, u16 new_links,
					   struct ieee80211_bss_conf *old[IEEE80211_MLD_MAX_NUM_LINKS])
{
	unsigned long rem = old_links & ~new_links;
	unsigned long add = new_links & ~old_links;
	int i;

	if (!old_links)
		rem |= BIT(0);
	if (!new_links)
		add |= BIT(0);

	for_each_set_bit(i, &rem, IEEE80211_MLD_MAX_NUM_LINKS)
		mac80211_hwsim_config_mac_nl(hw, old[i]->addr, false);

	for_each_set_bit(i, &add, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct ieee80211_bss_conf *link_conf;

		link_conf = link_conf_dereference_protected(vif, i);
		if (WARN_ON(!link_conf))
			continue;

		mac80211_hwsim_config_mac_nl(hw, link_conf->addr, true);
	}

	return 0;
}

static int mac80211_hwsim_change_sta_links(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif,
					   struct ieee80211_sta *sta,
					   u16 old_links, u16 new_links)
{
	struct hwsim_sta_priv *sp = (void *)sta->drv_priv;

	hwsim_check_sta_magic(sta);

	if (vif->type == NL80211_IFTYPE_STATION)
		sp->active_links_rx = new_links;

	return 0;
}

static int mac80211_hwsim_send_pmsr_ftm_request_peer(struct sk_buff *msg,
						     struct cfg80211_pmsr_ftm_request_peer *request)
{
	struct nlattr *ftm;

	if (!request->requested)
		return -EINVAL;

	ftm = nla_nest_start(msg, NL80211_PMSR_TYPE_FTM);
	if (!ftm)
		return -ENOBUFS;

	if (nla_put_u32(msg, NL80211_PMSR_FTM_REQ_ATTR_PREAMBLE, request->preamble))
		return -ENOBUFS;

	if (nla_put_u16(msg, NL80211_PMSR_FTM_REQ_ATTR_BURST_PERIOD, request->burst_period))
		return -ENOBUFS;

	if (request->asap && nla_put_flag(msg, NL80211_PMSR_FTM_REQ_ATTR_ASAP))
		return -ENOBUFS;

	if (request->request_lci && nla_put_flag(msg, NL80211_PMSR_FTM_REQ_ATTR_REQUEST_LCI))
		return -ENOBUFS;

	if (request->request_civicloc &&
	    nla_put_flag(msg, NL80211_PMSR_FTM_REQ_ATTR_REQUEST_CIVICLOC))
		return -ENOBUFS;

	if (request->trigger_based && nla_put_flag(msg, NL80211_PMSR_FTM_REQ_ATTR_TRIGGER_BASED))
		return -ENOBUFS;

	if (request->non_trigger_based &&
	    nla_put_flag(msg, NL80211_PMSR_FTM_REQ_ATTR_NON_TRIGGER_BASED))
		return -ENOBUFS;

	if (request->lmr_feedback && nla_put_flag(msg, NL80211_PMSR_FTM_REQ_ATTR_LMR_FEEDBACK))
		return -ENOBUFS;

	if (nla_put_u8(msg, NL80211_PMSR_FTM_REQ_ATTR_NUM_BURSTS_EXP, request->num_bursts_exp))
		return -ENOBUFS;

	if (nla_put_u8(msg, NL80211_PMSR_FTM_REQ_ATTR_BURST_DURATION, request->burst_duration))
		return -ENOBUFS;

	if (nla_put_u8(msg, NL80211_PMSR_FTM_REQ_ATTR_FTMS_PER_BURST, request->ftms_per_burst))
		return -ENOBUFS;

	if (nla_put_u8(msg, NL80211_PMSR_FTM_REQ_ATTR_NUM_FTMR_RETRIES, request->ftmr_retries))
		return -ENOBUFS;

	if (nla_put_u8(msg, NL80211_PMSR_FTM_REQ_ATTR_BURST_DURATION, request->burst_duration))
		return -ENOBUFS;

	if (nla_put_u8(msg, NL80211_PMSR_FTM_REQ_ATTR_BSS_COLOR, request->bss_color))
		return -ENOBUFS;

	nla_nest_end(msg, ftm);

	return 0;
}

static int mac80211_hwsim_send_pmsr_request_peer(struct sk_buff *msg,
						 struct cfg80211_pmsr_request_peer *request)
{
	struct nlattr *peer, *chandef, *req, *data;
	int err;

	peer = nla_nest_start(msg, NL80211_PMSR_ATTR_PEERS);
	if (!peer)
		return -ENOBUFS;

	if (nla_put(msg, NL80211_PMSR_PEER_ATTR_ADDR, ETH_ALEN,
		    request->addr))
		return -ENOBUFS;

	chandef = nla_nest_start(msg, NL80211_PMSR_PEER_ATTR_CHAN);
	if (!chandef)
		return -ENOBUFS;

	err = nl80211_send_chandef(msg, &request->chandef);
	if (err)
		return err;

	nla_nest_end(msg, chandef);

	req = nla_nest_start(msg, NL80211_PMSR_PEER_ATTR_REQ);
	if (!req)
		return -ENOBUFS;

	if (request->report_ap_tsf && nla_put_flag(msg, NL80211_PMSR_REQ_ATTR_GET_AP_TSF))
		return -ENOBUFS;

	data = nla_nest_start(msg, NL80211_PMSR_REQ_ATTR_DATA);
	if (!data)
		return -ENOBUFS;

	err = mac80211_hwsim_send_pmsr_ftm_request_peer(msg, &request->ftm);
	if (err)
		return err;

	nla_nest_end(msg, data);
	nla_nest_end(msg, req);
	nla_nest_end(msg, peer);

	return 0;
}

static int mac80211_hwsim_send_pmsr_request(struct sk_buff *msg,
					    struct cfg80211_pmsr_request *request)
{
	struct nlattr *pmsr;
	int err;

	pmsr = nla_nest_start(msg, NL80211_ATTR_PEER_MEASUREMENTS);
	if (!pmsr)
		return -ENOBUFS;

	if (nla_put_u32(msg, NL80211_ATTR_TIMEOUT, request->timeout))
		return -ENOBUFS;

	if (!is_zero_ether_addr(request->mac_addr)) {
		if (nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, request->mac_addr))
			return -ENOBUFS;
		if (nla_put(msg, NL80211_ATTR_MAC_MASK, ETH_ALEN, request->mac_addr_mask))
			return -ENOBUFS;
	}

	for (int i = 0; i < request->n_peers; i++) {
		err = mac80211_hwsim_send_pmsr_request_peer(msg, &request->peers[i]);
		if (err)
			return err;
	}

	nla_nest_end(msg, pmsr);

	return 0;
}

static int mac80211_hwsim_start_pmsr(struct ieee80211_hw *hw,
				     struct ieee80211_vif *vif,
				     struct cfg80211_pmsr_request *request)
{
	struct mac80211_hwsim_data *data;
	struct sk_buff *skb = NULL;
	struct nlattr *pmsr;
	void *msg_head;
	u32 _portid;
	int err = 0;

	data = hw->priv;
	_portid = READ_ONCE(data->wmediumd);
	if (!_portid && !hwsim_virtio_enabled)
		return -EOPNOTSUPP;

	mutex_lock(&data->mutex);

	if (data->pmsr_request) {
		err = -EBUSY;
		goto out_free;
	}

	skb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);

	if (!skb) {
		err = -ENOMEM;
		goto out_free;
	}

	msg_head = genlmsg_put(skb, 0, 0, &hwsim_genl_family, 0, HWSIM_CMD_START_PMSR);

	if (nla_put(skb, HWSIM_ATTR_ADDR_TRANSMITTER,
		    ETH_ALEN, data->addresses[1].addr)) {
		err = -ENOMEM;
		goto out_free;
	}

	pmsr = nla_nest_start(skb, HWSIM_ATTR_PMSR_REQUEST);
	if (!pmsr) {
		err = -ENOMEM;
		goto out_free;
	}

	err = mac80211_hwsim_send_pmsr_request(skb, request);
	if (err)
		goto out_free;

	nla_nest_end(skb, pmsr);

	genlmsg_end(skb, msg_head);
	if (hwsim_virtio_enabled)
		hwsim_tx_virtio(data, skb);
	else
		hwsim_unicast_netgroup(data, skb, _portid);

	data->pmsr_request = request;
	data->pmsr_request_wdev = ieee80211_vif_to_wdev(vif);

out_free:
	if (err && skb)
		nlmsg_free(skb);

	mutex_unlock(&data->mutex);
	return err;
}

static void mac80211_hwsim_abort_pmsr(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct cfg80211_pmsr_request *request)
{
	struct mac80211_hwsim_data *data;
	struct sk_buff *skb = NULL;
	struct nlattr *pmsr;
	void *msg_head;
	u32 _portid;
	int err = 0;

	data = hw->priv;
	_portid = READ_ONCE(data->wmediumd);
	if (!_portid && !hwsim_virtio_enabled)
		return;

	mutex_lock(&data->mutex);

	if (data->pmsr_request != request) {
		err = -EINVAL;
		goto out;
	}

	skb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb) {
		err = -ENOMEM;
		goto out;
	}

	msg_head = genlmsg_put(skb, 0, 0, &hwsim_genl_family, 0, HWSIM_CMD_ABORT_PMSR);

	if (nla_put(skb, HWSIM_ATTR_ADDR_TRANSMITTER, ETH_ALEN, data->addresses[1].addr))
		goto out;

	pmsr = nla_nest_start(skb, HWSIM_ATTR_PMSR_REQUEST);
	if (!pmsr) {
		err = -ENOMEM;
		goto out;
	}

	err = mac80211_hwsim_send_pmsr_request(skb, request);
	if (err)
		goto out;

	err = nla_nest_end(skb, pmsr);
	if (err)
		goto out;

	genlmsg_end(skb, msg_head);
	if (hwsim_virtio_enabled)
		hwsim_tx_virtio(data, skb);
	else
		hwsim_unicast_netgroup(data, skb, _portid);

out:
	if (err && skb)
		nlmsg_free(skb);

	mutex_unlock(&data->mutex);
}

static int mac80211_hwsim_parse_rate_info(struct nlattr *rateattr,
					  struct rate_info *rate_info,
					  struct genl_info *info)
{
	struct nlattr *tb[HWSIM_RATE_INFO_ATTR_MAX + 1];
	int ret;

	ret = nla_parse_nested(tb, HWSIM_RATE_INFO_ATTR_MAX,
			       rateattr, hwsim_rate_info_policy, info->extack);
	if (ret)
		return ret;

	if (tb[HWSIM_RATE_INFO_ATTR_FLAGS])
		rate_info->flags = nla_get_u8(tb[HWSIM_RATE_INFO_ATTR_FLAGS]);

	if (tb[HWSIM_RATE_INFO_ATTR_MCS])
		rate_info->mcs = nla_get_u8(tb[HWSIM_RATE_INFO_ATTR_MCS]);

	if (tb[HWSIM_RATE_INFO_ATTR_LEGACY])
		rate_info->legacy = nla_get_u16(tb[HWSIM_RATE_INFO_ATTR_LEGACY]);

	if (tb[HWSIM_RATE_INFO_ATTR_NSS])
		rate_info->nss = nla_get_u8(tb[HWSIM_RATE_INFO_ATTR_NSS]);

	if (tb[HWSIM_RATE_INFO_ATTR_BW])
		rate_info->bw = nla_get_u8(tb[HWSIM_RATE_INFO_ATTR_BW]);

	if (tb[HWSIM_RATE_INFO_ATTR_HE_GI])
		rate_info->he_gi = nla_get_u8(tb[HWSIM_RATE_INFO_ATTR_HE_GI]);

	if (tb[HWSIM_RATE_INFO_ATTR_HE_DCM])
		rate_info->he_dcm = nla_get_u8(tb[HWSIM_RATE_INFO_ATTR_HE_DCM]);

	if (tb[HWSIM_RATE_INFO_ATTR_HE_RU_ALLOC])
		rate_info->he_ru_alloc =
			nla_get_u8(tb[HWSIM_RATE_INFO_ATTR_HE_RU_ALLOC]);

	if (tb[HWSIM_RATE_INFO_ATTR_N_BOUNDED_CH])
		rate_info->n_bonded_ch = nla_get_u8(tb[HWSIM_RATE_INFO_ATTR_N_BOUNDED_CH]);

	if (tb[HWSIM_RATE_INFO_ATTR_EHT_GI])
		rate_info->eht_gi = nla_get_u8(tb[HWSIM_RATE_INFO_ATTR_EHT_GI]);

	if (tb[HWSIM_RATE_INFO_ATTR_EHT_RU_ALLOC])
		rate_info->eht_ru_alloc = nla_get_u8(tb[HWSIM_RATE_INFO_ATTR_EHT_RU_ALLOC]);

	return 0;
}

static int mac80211_hwsim_parse_ftm_result(struct nlattr *ftm,
					   struct cfg80211_pmsr_ftm_result *result,
					   struct genl_info *info)
{
	struct nlattr *tb[NL80211_PMSR_FTM_RESP_ATTR_MAX + 1];
	int ret;

	ret = nla_parse_nested(tb, NL80211_PMSR_FTM_RESP_ATTR_MAX,
			       ftm, hwsim_ftm_result_policy, info->extack);
	if (ret)
		return ret;

	if (tb[NL80211_PMSR_FTM_RESP_ATTR_FAIL_REASON])
		result->failure_reason = nla_get_u32(tb[NL80211_PMSR_FTM_RESP_ATTR_FAIL_REASON]);

	if (tb[NL80211_PMSR_FTM_RESP_ATTR_BURST_INDEX])
		result->burst_index = nla_get_u16(tb[NL80211_PMSR_FTM_RESP_ATTR_BURST_INDEX]);

	if (tb[NL80211_PMSR_FTM_RESP_ATTR_NUM_FTMR_ATTEMPTS]) {
		result->num_ftmr_attempts_valid = 1;
		result->num_ftmr_attempts =
			nla_get_u32(tb[NL80211_PMSR_FTM_RESP_ATTR_NUM_FTMR_ATTEMPTS]);
	}

	if (tb[NL80211_PMSR_FTM_RESP_ATTR_NUM_FTMR_SUCCESSES]) {
		result->num_ftmr_successes_valid = 1;
		result->num_ftmr_successes =
			nla_get_u32(tb[NL80211_PMSR_FTM_RESP_ATTR_NUM_FTMR_SUCCESSES]);
	}

	if (tb[NL80211_PMSR_FTM_RESP_ATTR_BUSY_RETRY_TIME])
		result->busy_retry_time =
			nla_get_u8(tb[NL80211_PMSR_FTM_RESP_ATTR_BUSY_RETRY_TIME]);

	if (tb[NL80211_PMSR_FTM_RESP_ATTR_NUM_BURSTS_EXP])
		result->num_bursts_exp = nla_get_u8(tb[NL80211_PMSR_FTM_RESP_ATTR_NUM_BURSTS_EXP]);

	if (tb[NL80211_PMSR_FTM_RESP_ATTR_BURST_DURATION])
		result->burst_duration = nla_get_u8(tb[NL80211_PMSR_FTM_RESP_ATTR_BURST_DURATION]);

	if (tb[NL80211_PMSR_FTM_RESP_ATTR_FTMS_PER_BURST])
		result->ftms_per_burst = nla_get_u8(tb[NL80211_PMSR_FTM_RESP_ATTR_FTMS_PER_BURST]);

	if (tb[NL80211_PMSR_FTM_RESP_ATTR_RSSI_AVG]) {
		result->rssi_avg_valid = 1;
		result->rssi_avg = nla_get_s32(tb[NL80211_PMSR_FTM_RESP_ATTR_RSSI_AVG]);
	}
	if (tb[NL80211_PMSR_FTM_RESP_ATTR_RSSI_SPREAD]) {
		result->rssi_spread_valid = 1;
		result->rssi_spread =
			nla_get_s32(tb[NL80211_PMSR_FTM_RESP_ATTR_RSSI_SPREAD]);
	}

	if (tb[NL80211_PMSR_FTM_RESP_ATTR_TX_RATE]) {
		result->tx_rate_valid = 1;
		ret = mac80211_hwsim_parse_rate_info(tb[NL80211_PMSR_FTM_RESP_ATTR_TX_RATE],
						     &result->tx_rate, info);
		if (ret)
			return ret;
	}

	if (tb[NL80211_PMSR_FTM_RESP_ATTR_RX_RATE]) {
		result->rx_rate_valid = 1;
		ret = mac80211_hwsim_parse_rate_info(tb[NL80211_PMSR_FTM_RESP_ATTR_RX_RATE],
						     &result->rx_rate, info);
		if (ret)
			return ret;
	}

	if (tb[NL80211_PMSR_FTM_RESP_ATTR_RTT_AVG]) {
		result->rtt_avg_valid = 1;
		result->rtt_avg =
			nla_get_u64(tb[NL80211_PMSR_FTM_RESP_ATTR_RTT_AVG]);
	}
	if (tb[NL80211_PMSR_FTM_RESP_ATTR_RTT_VARIANCE]) {
		result->rtt_variance_valid = 1;
		result->rtt_variance =
			nla_get_u64(tb[NL80211_PMSR_FTM_RESP_ATTR_RTT_VARIANCE]);
	}
	if (tb[NL80211_PMSR_FTM_RESP_ATTR_RTT_SPREAD]) {
		result->rtt_spread_valid = 1;
		result->rtt_spread =
			nla_get_u64(tb[NL80211_PMSR_FTM_RESP_ATTR_RTT_SPREAD]);
	}
	if (tb[NL80211_PMSR_FTM_RESP_ATTR_DIST_AVG]) {
		result->dist_avg_valid = 1;
		result->dist_avg =
			nla_get_u64(tb[NL80211_PMSR_FTM_RESP_ATTR_DIST_AVG]);
	}
	if (tb[NL80211_PMSR_FTM_RESP_ATTR_DIST_VARIANCE]) {
		result->dist_variance_valid = 1;
		result->dist_variance =
			nla_get_u64(tb[NL80211_PMSR_FTM_RESP_ATTR_DIST_VARIANCE]);
	}
	if (tb[NL80211_PMSR_FTM_RESP_ATTR_DIST_SPREAD]) {
		result->dist_spread_valid = 1;
		result->dist_spread =
			nla_get_u64(tb[NL80211_PMSR_FTM_RESP_ATTR_DIST_SPREAD]);
	}

	if (tb[NL80211_PMSR_FTM_RESP_ATTR_LCI]) {
		result->lci = nla_data(tb[NL80211_PMSR_FTM_RESP_ATTR_LCI]);
		result->lci_len = nla_len(tb[NL80211_PMSR_FTM_RESP_ATTR_LCI]);
	}

	if (tb[NL80211_PMSR_FTM_RESP_ATTR_CIVICLOC]) {
		result->civicloc = nla_data(tb[NL80211_PMSR_FTM_RESP_ATTR_CIVICLOC]);
		result->civicloc_len = nla_len(tb[NL80211_PMSR_FTM_RESP_ATTR_CIVICLOC]);
	}

	return 0;
}

static int mac80211_hwsim_parse_pmsr_resp(struct nlattr *resp,
					  struct cfg80211_pmsr_result *result,
					  struct genl_info *info)
{
	struct nlattr *tb[NL80211_PMSR_RESP_ATTR_MAX + 1];
	struct nlattr *pmsr;
	int rem;
	int ret;

	ret = nla_parse_nested(tb, NL80211_PMSR_RESP_ATTR_MAX, resp, hwsim_pmsr_resp_policy,
			       info->extack);
	if (ret)
		return ret;

	if (tb[NL80211_PMSR_RESP_ATTR_STATUS])
		result->status = nla_get_u32(tb[NL80211_PMSR_RESP_ATTR_STATUS]);

	if (tb[NL80211_PMSR_RESP_ATTR_HOST_TIME])
		result->host_time = nla_get_u64(tb[NL80211_PMSR_RESP_ATTR_HOST_TIME]);

	if (tb[NL80211_PMSR_RESP_ATTR_AP_TSF]) {
		result->ap_tsf_valid = 1;
		result->ap_tsf = nla_get_u64(tb[NL80211_PMSR_RESP_ATTR_AP_TSF]);
	}

	result->final = !!tb[NL80211_PMSR_RESP_ATTR_FINAL];

	if (!tb[NL80211_PMSR_RESP_ATTR_DATA])
		return 0;

	nla_for_each_nested(pmsr, tb[NL80211_PMSR_RESP_ATTR_DATA], rem) {
		switch (nla_type(pmsr)) {
		case NL80211_PMSR_TYPE_FTM:
			result->type = NL80211_PMSR_TYPE_FTM;
			ret = mac80211_hwsim_parse_ftm_result(pmsr, &result->ftm, info);
			if (ret)
				return ret;
			break;
		default:
			NL_SET_ERR_MSG_ATTR(info->extack, pmsr, "Unknown pmsr resp type");
			return -EINVAL;
		}
	}

	return 0;
}

static int mac80211_hwsim_parse_pmsr_result(struct nlattr *peer,
					    struct cfg80211_pmsr_result *result,
					    struct genl_info *info)
{
	struct nlattr *tb[NL80211_PMSR_PEER_ATTR_MAX + 1];
	int ret;

	if (!peer)
		return -EINVAL;

	ret = nla_parse_nested(tb, NL80211_PMSR_PEER_ATTR_MAX, peer,
			       hwsim_pmsr_peer_result_policy, info->extack);
	if (ret)
		return ret;

	if (tb[NL80211_PMSR_PEER_ATTR_ADDR])
		memcpy(result->addr, nla_data(tb[NL80211_PMSR_PEER_ATTR_ADDR]),
		       ETH_ALEN);

	if (tb[NL80211_PMSR_PEER_ATTR_RESP]) {
		ret = mac80211_hwsim_parse_pmsr_resp(tb[NL80211_PMSR_PEER_ATTR_RESP], result, info);
		if (ret)
			return ret;
	}

	return 0;
};

static int hwsim_pmsr_report_nl(struct sk_buff *msg, struct genl_info *info)
{
	struct mac80211_hwsim_data *data;
	struct nlattr *peers, *peer;
	struct nlattr *reqattr;
	const u8 *src;
	int err;
	int rem;

	if (!info->attrs[HWSIM_ATTR_ADDR_TRANSMITTER])
		return -EINVAL;

	src = nla_data(info->attrs[HWSIM_ATTR_ADDR_TRANSMITTER]);
	data = get_hwsim_data_ref_from_addr(src);
	if (!data)
		return -EINVAL;

	mutex_lock(&data->mutex);
	if (!data->pmsr_request) {
		err = -EINVAL;
		goto out;
	}

	reqattr = info->attrs[HWSIM_ATTR_PMSR_RESULT];
	if (!reqattr) {
		err = -EINVAL;
		goto out;
	}

	peers = nla_find_nested(reqattr, NL80211_PMSR_ATTR_PEERS);
	if (!peers) {
		err = -EINVAL;
		goto out;
	}

	nla_for_each_nested(peer, peers, rem) {
		struct cfg80211_pmsr_result result = {};

		err = mac80211_hwsim_parse_pmsr_result(peer, &result, info);
		if (err)
			goto out;

		cfg80211_pmsr_report(data->pmsr_request_wdev,
				     data->pmsr_request, &result, GFP_KERNEL);
	}

	cfg80211_pmsr_complete(data->pmsr_request_wdev, data->pmsr_request, GFP_KERNEL);

	err = 0;
out:
	data->pmsr_request = NULL;
	data->pmsr_request_wdev = NULL;

	mutex_unlock(&data->mutex);
	return err;
}

#ifdef CONFIG_MAC80211_DEBUGFS
#define HWSIM_DEBUGFS_OPS					\
	.link_add_debugfs = mac80211_hwsim_link_add_debugfs,
#else
#define HWSIM_DEBUGFS_OPS
#endif

#define HWSIM_COMMON_OPS					\
	.tx = mac80211_hwsim_tx,				\
	.wake_tx_queue = ieee80211_handle_wake_tx_queue,	\
	.start = mac80211_hwsim_start,				\
	.stop = mac80211_hwsim_stop,				\
	.add_interface = mac80211_hwsim_add_interface,		\
	.change_interface = mac80211_hwsim_change_interface,	\
	.remove_interface = mac80211_hwsim_remove_interface,	\
	.config = mac80211_hwsim_config,			\
	.configure_filter = mac80211_hwsim_configure_filter,	\
	.vif_cfg_changed = mac80211_hwsim_vif_info_changed,	\
	.link_info_changed = mac80211_hwsim_link_info_changed,  \
	.tx_last_beacon = mac80211_hwsim_tx_last_beacon,	\
	.sta_notify = mac80211_hwsim_sta_notify,		\
	.link_sta_rc_update = mac80211_hwsim_sta_rc_update,	\
	.conf_tx = mac80211_hwsim_conf_tx,			\
	.get_survey = mac80211_hwsim_get_survey,		\
	CFG80211_TESTMODE_CMD(mac80211_hwsim_testmode_cmd)	\
	.ampdu_action = mac80211_hwsim_ampdu_action,		\
	.flush = mac80211_hwsim_flush,				\
	.get_et_sset_count = mac80211_hwsim_get_et_sset_count,	\
	.get_et_stats = mac80211_hwsim_get_et_stats,		\
	.get_et_strings = mac80211_hwsim_get_et_strings,	\
	.start_pmsr = mac80211_hwsim_start_pmsr,		\
	.abort_pmsr = mac80211_hwsim_abort_pmsr,		\
	HWSIM_DEBUGFS_OPS

#define HWSIM_NON_MLO_OPS					\
	.sta_add = mac80211_hwsim_sta_add,			\
	.sta_remove = mac80211_hwsim_sta_remove,		\
	.set_tim = mac80211_hwsim_set_tim,			\
	.get_tsf = mac80211_hwsim_get_tsf,			\
	.set_tsf = mac80211_hwsim_set_tsf,

static const struct ieee80211_ops mac80211_hwsim_ops = {
	HWSIM_COMMON_OPS
	HWSIM_NON_MLO_OPS
	.sw_scan_start = mac80211_hwsim_sw_scan,
	.sw_scan_complete = mac80211_hwsim_sw_scan_complete,
	.add_chanctx = ieee80211_emulate_add_chanctx,
	.remove_chanctx = ieee80211_emulate_remove_chanctx,
	.change_chanctx = ieee80211_emulate_change_chanctx,
	.switch_vif_chanctx = ieee80211_emulate_switch_vif_chanctx,
};

#define HWSIM_CHANCTX_OPS					\
	.hw_scan = mac80211_hwsim_hw_scan,			\
	.cancel_hw_scan = mac80211_hwsim_cancel_hw_scan,	\
	.remain_on_channel = mac80211_hwsim_roc,		\
	.cancel_remain_on_channel = mac80211_hwsim_croc,	\
	.add_chanctx = mac80211_hwsim_add_chanctx,		\
	.remove_chanctx = mac80211_hwsim_remove_chanctx,	\
	.change_chanctx = mac80211_hwsim_change_chanctx,	\
	.assign_vif_chanctx = mac80211_hwsim_assign_vif_chanctx,\
	.unassign_vif_chanctx = mac80211_hwsim_unassign_vif_chanctx, \
	.switch_vif_chanctx = mac80211_hwsim_switch_vif_chanctx,

static const struct ieee80211_ops mac80211_hwsim_mchan_ops = {
	HWSIM_COMMON_OPS
	HWSIM_NON_MLO_OPS
	HWSIM_CHANCTX_OPS
};

static const struct ieee80211_ops mac80211_hwsim_mlo_ops = {
	HWSIM_COMMON_OPS
	HWSIM_CHANCTX_OPS
	.set_rts_threshold = mac80211_hwsim_set_rts_threshold,
	.change_vif_links = mac80211_hwsim_change_vif_links,
	.change_sta_links = mac80211_hwsim_change_sta_links,
	.sta_state = mac80211_hwsim_sta_state,
	.can_neg_ttlm = mac80211_hwsim_can_neg_ttlm,
};

struct hwsim_new_radio_params {
	unsigned int channels;
	const char *reg_alpha2;
	const struct ieee80211_regdomain *regd;
	bool reg_strict;
	bool p2p_device;
	bool use_chanctx;
	bool multi_radio;
	bool destroy_on_close;
	const char *hwname;
	bool no_vif;
	const u8 *perm_addr;
	u32 iftypes;
	u32 *ciphers;
	u8 n_ciphers;
	bool mlo;
	const struct cfg80211_pmsr_capabilities *pmsr_capa;
};

static void hwsim_mcast_config_msg(struct sk_buff *mcast_skb,
				   struct genl_info *info)
{
	if (info)
		genl_notify(&hwsim_genl_family, mcast_skb, info,
			    HWSIM_MCGRP_CONFIG, GFP_KERNEL);
	else
		genlmsg_multicast(&hwsim_genl_family, mcast_skb, 0,
				  HWSIM_MCGRP_CONFIG, GFP_KERNEL);
}

static int append_radio_msg(struct sk_buff *skb, int id,
			    struct hwsim_new_radio_params *param)
{
	int ret;

	ret = nla_put_u32(skb, HWSIM_ATTR_RADIO_ID, id);
	if (ret < 0)
		return ret;

	if (param->channels) {
		ret = nla_put_u32(skb, HWSIM_ATTR_CHANNELS, param->channels);
		if (ret < 0)
			return ret;
	}

	if (param->reg_alpha2) {
		ret = nla_put(skb, HWSIM_ATTR_REG_HINT_ALPHA2, 2,
			      param->reg_alpha2);
		if (ret < 0)
			return ret;
	}

	if (param->regd) {
		int i;

		for (i = 0; i < ARRAY_SIZE(hwsim_world_regdom_custom); i++) {
			if (hwsim_world_regdom_custom[i] != param->regd)
				continue;

			ret = nla_put_u32(skb, HWSIM_ATTR_REG_CUSTOM_REG, i);
			if (ret < 0)
				return ret;
			break;
		}
	}

	if (param->reg_strict) {
		ret = nla_put_flag(skb, HWSIM_ATTR_REG_STRICT_REG);
		if (ret < 0)
			return ret;
	}

	if (param->p2p_device) {
		ret = nla_put_flag(skb, HWSIM_ATTR_SUPPORT_P2P_DEVICE);
		if (ret < 0)
			return ret;
	}

	if (param->use_chanctx) {
		ret = nla_put_flag(skb, HWSIM_ATTR_USE_CHANCTX);
		if (ret < 0)
			return ret;
	}

	if (param->multi_radio) {
		ret = nla_put_flag(skb, HWSIM_ATTR_MULTI_RADIO);
		if (ret < 0)
			return ret;
	}

	if (param->hwname) {
		ret = nla_put(skb, HWSIM_ATTR_RADIO_NAME,
			      strlen(param->hwname), param->hwname);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void hwsim_mcast_new_radio(int id, struct genl_info *info,
				  struct hwsim_new_radio_params *param)
{
	struct sk_buff *mcast_skb;
	void *data;

	mcast_skb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!mcast_skb)
		return;

	data = genlmsg_put(mcast_skb, 0, 0, &hwsim_genl_family, 0,
			   HWSIM_CMD_NEW_RADIO);
	if (!data)
		goto out_err;

	if (append_radio_msg(mcast_skb, id, param) < 0)
		goto out_err;

	genlmsg_end(mcast_skb, data);

	hwsim_mcast_config_msg(mcast_skb, info);
	return;

out_err:
	nlmsg_free(mcast_skb);
}

static const struct ieee80211_sband_iftype_data sband_capa_2ghz[] = {
	{
		.types_mask = BIT(NL80211_IFTYPE_STATION) |
			      BIT(NL80211_IFTYPE_P2P_CLIENT),
		.he_cap = {
			.has_he = true,
			.he_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_HE_MAC_CAP0_HTC_HE,
				.mac_cap_info[1] =
					IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US |
					IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8,
				.mac_cap_info[2] =
					IEEE80211_HE_MAC_CAP2_BSR |
					IEEE80211_HE_MAC_CAP2_MU_CASCADING |
					IEEE80211_HE_MAC_CAP2_ACK_EN,
				.mac_cap_info[3] =
					IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
					IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3,
				.mac_cap_info[4] = IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU,
				.phy_cap_info[0] =
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G,
				.phy_cap_info[1] =
					IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK |
					IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
					IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD |
					IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS,
				.phy_cap_info[2] =
					IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
					IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ |
					IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ |
					IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO |
					IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO,

				/* Leave all the other PHY capability bytes
				 * unset, as DCM, beam forming, RU and PPE
				 * threshold information are not supported
				 */
			},
			.he_mcs_nss_supp = {
				.rx_mcs_80 = cpu_to_le16(0xfffa),
				.tx_mcs_80 = cpu_to_le16(0xfffa),
				.rx_mcs_160 = cpu_to_le16(0xffff),
				.tx_mcs_160 = cpu_to_le16(0xffff),
				.rx_mcs_80p80 = cpu_to_le16(0xffff),
				.tx_mcs_80p80 = cpu_to_le16(0xffff),
			},
		},
		.eht_cap = {
			.has_eht = true,
			.eht_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_EHT_MAC_CAP0_EPCS_PRIO_ACCESS |
					IEEE80211_EHT_MAC_CAP0_OM_CONTROL |
					IEEE80211_EHT_MAC_CAP0_TRIG_TXOP_SHARING_MODE1,
				.phy_cap_info[0] =
					IEEE80211_EHT_PHY_CAP0_242_TONE_RU_GT20MHZ |
					IEEE80211_EHT_PHY_CAP0_NDP_4_EHT_LFT_32_GI |
					IEEE80211_EHT_PHY_CAP0_PARTIAL_BW_UL_MU_MIMO |
					IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMER |
					IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMEE,
				.phy_cap_info[3] =
					IEEE80211_EHT_PHY_CAP3_NG_16_SU_FEEDBACK |
					IEEE80211_EHT_PHY_CAP3_NG_16_MU_FEEDBACK |
					IEEE80211_EHT_PHY_CAP3_CODEBOOK_4_2_SU_FDBK |
					IEEE80211_EHT_PHY_CAP3_CODEBOOK_7_5_MU_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_SU_BF_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_MU_BF_PART_BW_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_CQI_FDBK,
				.phy_cap_info[4] =
					IEEE80211_EHT_PHY_CAP4_PART_BW_DL_MU_MIMO |
					IEEE80211_EHT_PHY_CAP4_PSR_SR_SUPP |
					IEEE80211_EHT_PHY_CAP4_POWER_BOOST_FACT_SUPP |
					IEEE80211_EHT_PHY_CAP4_EHT_MU_PPDU_4_EHT_LTF_08_GI |
					IEEE80211_EHT_PHY_CAP4_MAX_NC_MASK,
				.phy_cap_info[5] =
					IEEE80211_EHT_PHY_CAP5_NON_TRIG_CQI_FEEDBACK |
					IEEE80211_EHT_PHY_CAP5_TX_LESS_242_TONE_RU_SUPP |
					IEEE80211_EHT_PHY_CAP5_RX_LESS_242_TONE_RU_SUPP |
					IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT |
					IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_MASK |
					IEEE80211_EHT_PHY_CAP5_MAX_NUM_SUPP_EHT_LTF_MASK,
				.phy_cap_info[6] =
					IEEE80211_EHT_PHY_CAP6_MAX_NUM_SUPP_EHT_LTF_MASK |
					IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_MASK,
				.phy_cap_info[7] =
					IEEE80211_EHT_PHY_CAP7_20MHZ_STA_RX_NDP_WIDER_BW,
			},

			/* For all MCS and bandwidth, set 8 NSS for both Tx and
			 * Rx
			 */
			.eht_mcs_nss_supp = {
				/*
				 * Since B0, B1, B2 and B3 are not set in
				 * the supported channel width set field in the
				 * HE PHY capabilities information field the
				 * device is a 20MHz only device on 2.4GHz band.
				 */
				.only_20mhz = {
					.rx_tx_mcs7_max_nss = 0x88,
					.rx_tx_mcs9_max_nss = 0x88,
					.rx_tx_mcs11_max_nss = 0x88,
					.rx_tx_mcs13_max_nss = 0x88,
				},
			},
			/* PPE threshold information is not supported */
		},
	},
	{
		.types_mask = BIT(NL80211_IFTYPE_AP) |
			      BIT(NL80211_IFTYPE_P2P_GO),
		.he_cap = {
			.has_he = true,
			.he_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_HE_MAC_CAP0_HTC_HE,
				.mac_cap_info[1] =
					IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US |
					IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8,
				.mac_cap_info[2] =
					IEEE80211_HE_MAC_CAP2_BSR |
					IEEE80211_HE_MAC_CAP2_MU_CASCADING |
					IEEE80211_HE_MAC_CAP2_ACK_EN,
				.mac_cap_info[3] =
					IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
					IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3,
				.mac_cap_info[4] = IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU,
				.phy_cap_info[0] =
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G,
				.phy_cap_info[1] =
					IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK |
					IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
					IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD |
					IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS,
				.phy_cap_info[2] =
					IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
					IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ |
					IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ |
					IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO |
					IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO,

				/* Leave all the other PHY capability bytes
				 * unset, as DCM, beam forming, RU and PPE
				 * threshold information are not supported
				 */
			},
			.he_mcs_nss_supp = {
				.rx_mcs_80 = cpu_to_le16(0xfffa),
				.tx_mcs_80 = cpu_to_le16(0xfffa),
				.rx_mcs_160 = cpu_to_le16(0xffff),
				.tx_mcs_160 = cpu_to_le16(0xffff),
				.rx_mcs_80p80 = cpu_to_le16(0xffff),
				.tx_mcs_80p80 = cpu_to_le16(0xffff),
			},
		},
		.eht_cap = {
			.has_eht = true,
			.eht_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_EHT_MAC_CAP0_EPCS_PRIO_ACCESS |
					IEEE80211_EHT_MAC_CAP0_OM_CONTROL |
					IEEE80211_EHT_MAC_CAP0_TRIG_TXOP_SHARING_MODE1,
				.phy_cap_info[0] =
					IEEE80211_EHT_PHY_CAP0_242_TONE_RU_GT20MHZ |
					IEEE80211_EHT_PHY_CAP0_NDP_4_EHT_LFT_32_GI |
					IEEE80211_EHT_PHY_CAP0_PARTIAL_BW_UL_MU_MIMO |
					IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMER |
					IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMEE,
				.phy_cap_info[3] =
					IEEE80211_EHT_PHY_CAP3_NG_16_SU_FEEDBACK |
					IEEE80211_EHT_PHY_CAP3_NG_16_MU_FEEDBACK |
					IEEE80211_EHT_PHY_CAP3_CODEBOOK_4_2_SU_FDBK |
					IEEE80211_EHT_PHY_CAP3_CODEBOOK_7_5_MU_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_SU_BF_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_MU_BF_PART_BW_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_CQI_FDBK,
				.phy_cap_info[4] =
					IEEE80211_EHT_PHY_CAP4_PART_BW_DL_MU_MIMO |
					IEEE80211_EHT_PHY_CAP4_PSR_SR_SUPP |
					IEEE80211_EHT_PHY_CAP4_POWER_BOOST_FACT_SUPP |
					IEEE80211_EHT_PHY_CAP4_EHT_MU_PPDU_4_EHT_LTF_08_GI |
					IEEE80211_EHT_PHY_CAP4_MAX_NC_MASK,
				.phy_cap_info[5] =
					IEEE80211_EHT_PHY_CAP5_NON_TRIG_CQI_FEEDBACK |
					IEEE80211_EHT_PHY_CAP5_TX_LESS_242_TONE_RU_SUPP |
					IEEE80211_EHT_PHY_CAP5_RX_LESS_242_TONE_RU_SUPP |
					IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT |
					IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_MASK |
					IEEE80211_EHT_PHY_CAP5_MAX_NUM_SUPP_EHT_LTF_MASK,
				.phy_cap_info[6] =
					IEEE80211_EHT_PHY_CAP6_MAX_NUM_SUPP_EHT_LTF_MASK |
					IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_MASK,
				.phy_cap_info[7] =
					IEEE80211_EHT_PHY_CAP7_20MHZ_STA_RX_NDP_WIDER_BW,
			},

			/* For all MCS and bandwidth, set 8 NSS for both Tx and
			 * Rx
			 */
			.eht_mcs_nss_supp = {
				/*
				 * Since B0, B1, B2 and B3 are not set in
				 * the supported channel width set field in the
				 * HE PHY capabilities information field the
				 * device is a 20MHz only device on 2.4GHz band.
				 */
				.only_20mhz = {
					.rx_tx_mcs7_max_nss = 0x88,
					.rx_tx_mcs9_max_nss = 0x88,
					.rx_tx_mcs11_max_nss = 0x88,
					.rx_tx_mcs13_max_nss = 0x88,
				},
			},
			/* PPE threshold information is not supported */
		},
	},
#ifdef CONFIG_MAC80211_MESH
	{
		.types_mask = BIT(NL80211_IFTYPE_MESH_POINT),
		.he_cap = {
			.has_he = true,
			.he_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_HE_MAC_CAP0_HTC_HE,
				.mac_cap_info[1] =
					IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8,
				.mac_cap_info[2] =
					IEEE80211_HE_MAC_CAP2_ACK_EN,
				.mac_cap_info[3] =
					IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
					IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3,
				.mac_cap_info[4] = IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU,
				.phy_cap_info[0] =
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G,
				.phy_cap_info[1] =
					IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK |
					IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
					IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD |
					IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS,
				.phy_cap_info[2] = 0,

				/* Leave all the other PHY capability bytes
				 * unset, as DCM, beam forming, RU and PPE
				 * threshold information are not supported
				 */
			},
			.he_mcs_nss_supp = {
				.rx_mcs_80 = cpu_to_le16(0xfffa),
				.tx_mcs_80 = cpu_to_le16(0xfffa),
				.rx_mcs_160 = cpu_to_le16(0xffff),
				.tx_mcs_160 = cpu_to_le16(0xffff),
				.rx_mcs_80p80 = cpu_to_le16(0xffff),
				.tx_mcs_80p80 = cpu_to_le16(0xffff),
			},
		},
	},
#endif
};

static const struct ieee80211_sband_iftype_data sband_capa_5ghz[] = {
	{
		.types_mask = BIT(NL80211_IFTYPE_STATION) |
			      BIT(NL80211_IFTYPE_P2P_CLIENT),
		.he_cap = {
			.has_he = true,
			.he_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_HE_MAC_CAP0_HTC_HE,
				.mac_cap_info[1] =
					IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US |
					IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8,
				.mac_cap_info[2] =
					IEEE80211_HE_MAC_CAP2_BSR |
					IEEE80211_HE_MAC_CAP2_MU_CASCADING |
					IEEE80211_HE_MAC_CAP2_ACK_EN,
				.mac_cap_info[3] =
					IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
					IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3,
				.mac_cap_info[4] = IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU,
				.phy_cap_info[0] =
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G |
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G,
				.phy_cap_info[1] =
					IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK |
					IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
					IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD |
					IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS,
				.phy_cap_info[2] =
					IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
					IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ |
					IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ |
					IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO |
					IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO,

				/* Leave all the other PHY capability bytes
				 * unset, as DCM, beam forming, RU and PPE
				 * threshold information are not supported
				 */
			},
			.he_mcs_nss_supp = {
				.rx_mcs_80 = cpu_to_le16(0xfffa),
				.tx_mcs_80 = cpu_to_le16(0xfffa),
				.rx_mcs_160 = cpu_to_le16(0xfffa),
				.tx_mcs_160 = cpu_to_le16(0xfffa),
				.rx_mcs_80p80 = cpu_to_le16(0xfffa),
				.tx_mcs_80p80 = cpu_to_le16(0xfffa),
			},
		},
		.eht_cap = {
			.has_eht = true,
			.eht_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_EHT_MAC_CAP0_EPCS_PRIO_ACCESS |
					IEEE80211_EHT_MAC_CAP0_OM_CONTROL |
					IEEE80211_EHT_MAC_CAP0_TRIG_TXOP_SHARING_MODE1,
				.phy_cap_info[0] =
					IEEE80211_EHT_PHY_CAP0_242_TONE_RU_GT20MHZ |
					IEEE80211_EHT_PHY_CAP0_NDP_4_EHT_LFT_32_GI |
					IEEE80211_EHT_PHY_CAP0_PARTIAL_BW_UL_MU_MIMO |
					IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMER |
					IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMEE |
					IEEE80211_EHT_PHY_CAP0_BEAMFORMEE_SS_80MHZ_MASK,
				.phy_cap_info[1] =
					IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_80MHZ_MASK |
					IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_160MHZ_MASK,
				.phy_cap_info[2] =
					IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_80MHZ_MASK |
					IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_160MHZ_MASK,
				.phy_cap_info[3] =
					IEEE80211_EHT_PHY_CAP3_NG_16_SU_FEEDBACK |
					IEEE80211_EHT_PHY_CAP3_NG_16_MU_FEEDBACK |
					IEEE80211_EHT_PHY_CAP3_CODEBOOK_4_2_SU_FDBK |
					IEEE80211_EHT_PHY_CAP3_CODEBOOK_7_5_MU_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_SU_BF_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_MU_BF_PART_BW_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_CQI_FDBK,
				.phy_cap_info[4] =
					IEEE80211_EHT_PHY_CAP4_PART_BW_DL_MU_MIMO |
					IEEE80211_EHT_PHY_CAP4_PSR_SR_SUPP |
					IEEE80211_EHT_PHY_CAP4_POWER_BOOST_FACT_SUPP |
					IEEE80211_EHT_PHY_CAP4_EHT_MU_PPDU_4_EHT_LTF_08_GI |
					IEEE80211_EHT_PHY_CAP4_MAX_NC_MASK,
				.phy_cap_info[5] =
					IEEE80211_EHT_PHY_CAP5_NON_TRIG_CQI_FEEDBACK |
					IEEE80211_EHT_PHY_CAP5_TX_LESS_242_TONE_RU_SUPP |
					IEEE80211_EHT_PHY_CAP5_RX_LESS_242_TONE_RU_SUPP |
					IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT |
					IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_MASK |
					IEEE80211_EHT_PHY_CAP5_MAX_NUM_SUPP_EHT_LTF_MASK,
				.phy_cap_info[6] =
					IEEE80211_EHT_PHY_CAP6_MAX_NUM_SUPP_EHT_LTF_MASK |
					IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_MASK,
				.phy_cap_info[7] =
					IEEE80211_EHT_PHY_CAP7_20MHZ_STA_RX_NDP_WIDER_BW |
					IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_80MHZ |
					IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_160MHZ |
					IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_80MHZ |
					IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_160MHZ,
			},

			/* For all MCS and bandwidth, set 8 NSS for both Tx and
			 * Rx
			 */
			.eht_mcs_nss_supp = {
				/*
				 * As B1 and B2 are set in the supported
				 * channel width set field in the HE PHY
				 * capabilities information field include all
				 * the following MCS/NSS.
				 */
				.bw._80 = {
					.rx_tx_mcs9_max_nss = 0x88,
					.rx_tx_mcs11_max_nss = 0x88,
					.rx_tx_mcs13_max_nss = 0x88,
				},
				.bw._160 = {
					.rx_tx_mcs9_max_nss = 0x88,
					.rx_tx_mcs11_max_nss = 0x88,
					.rx_tx_mcs13_max_nss = 0x88,
				},
			},
			/* PPE threshold information is not supported */
		},
	},
	{
		.types_mask = BIT(NL80211_IFTYPE_AP) |
			      BIT(NL80211_IFTYPE_P2P_GO),
		.he_cap = {
			.has_he = true,
			.he_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_HE_MAC_CAP0_HTC_HE,
				.mac_cap_info[1] =
					IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US |
					IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8,
				.mac_cap_info[2] =
					IEEE80211_HE_MAC_CAP2_BSR |
					IEEE80211_HE_MAC_CAP2_MU_CASCADING |
					IEEE80211_HE_MAC_CAP2_ACK_EN,
				.mac_cap_info[3] =
					IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
					IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3,
				.mac_cap_info[4] = IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU,
				.phy_cap_info[0] =
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G |
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G,
				.phy_cap_info[1] =
					IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK |
					IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
					IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD |
					IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS,
				.phy_cap_info[2] =
					IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
					IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ |
					IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ |
					IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO |
					IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO,

				/* Leave all the other PHY capability bytes
				 * unset, as DCM, beam forming, RU and PPE
				 * threshold information are not supported
				 */
			},
			.he_mcs_nss_supp = {
				.rx_mcs_80 = cpu_to_le16(0xfffa),
				.tx_mcs_80 = cpu_to_le16(0xfffa),
				.rx_mcs_160 = cpu_to_le16(0xfffa),
				.tx_mcs_160 = cpu_to_le16(0xfffa),
				.rx_mcs_80p80 = cpu_to_le16(0xfffa),
				.tx_mcs_80p80 = cpu_to_le16(0xfffa),
			},
		},
		.eht_cap = {
			.has_eht = true,
			.eht_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_EHT_MAC_CAP0_EPCS_PRIO_ACCESS |
					IEEE80211_EHT_MAC_CAP0_OM_CONTROL |
					IEEE80211_EHT_MAC_CAP0_TRIG_TXOP_SHARING_MODE1,
				.phy_cap_info[0] =
					IEEE80211_EHT_PHY_CAP0_242_TONE_RU_GT20MHZ |
					IEEE80211_EHT_PHY_CAP0_NDP_4_EHT_LFT_32_GI |
					IEEE80211_EHT_PHY_CAP0_PARTIAL_BW_UL_MU_MIMO |
					IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMER |
					IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMEE |
					IEEE80211_EHT_PHY_CAP0_BEAMFORMEE_SS_80MHZ_MASK,
				.phy_cap_info[1] =
					IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_80MHZ_MASK |
					IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_160MHZ_MASK,
				.phy_cap_info[2] =
					IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_80MHZ_MASK |
					IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_160MHZ_MASK,
				.phy_cap_info[3] =
					IEEE80211_EHT_PHY_CAP3_NG_16_SU_FEEDBACK |
					IEEE80211_EHT_PHY_CAP3_NG_16_MU_FEEDBACK |
					IEEE80211_EHT_PHY_CAP3_CODEBOOK_4_2_SU_FDBK |
					IEEE80211_EHT_PHY_CAP3_CODEBOOK_7_5_MU_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_SU_BF_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_MU_BF_PART_BW_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_CQI_FDBK,
				.phy_cap_info[4] =
					IEEE80211_EHT_PHY_CAP4_PART_BW_DL_MU_MIMO |
					IEEE80211_EHT_PHY_CAP4_PSR_SR_SUPP |
					IEEE80211_EHT_PHY_CAP4_POWER_BOOST_FACT_SUPP |
					IEEE80211_EHT_PHY_CAP4_EHT_MU_PPDU_4_EHT_LTF_08_GI |
					IEEE80211_EHT_PHY_CAP4_MAX_NC_MASK,
				.phy_cap_info[5] =
					IEEE80211_EHT_PHY_CAP5_NON_TRIG_CQI_FEEDBACK |
					IEEE80211_EHT_PHY_CAP5_TX_LESS_242_TONE_RU_SUPP |
					IEEE80211_EHT_PHY_CAP5_RX_LESS_242_TONE_RU_SUPP |
					IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT |
					IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_MASK |
					IEEE80211_EHT_PHY_CAP5_MAX_NUM_SUPP_EHT_LTF_MASK,
				.phy_cap_info[6] =
					IEEE80211_EHT_PHY_CAP6_MAX_NUM_SUPP_EHT_LTF_MASK |
					IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_MASK,
				.phy_cap_info[7] =
					IEEE80211_EHT_PHY_CAP7_20MHZ_STA_RX_NDP_WIDER_BW |
					IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_80MHZ |
					IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_160MHZ |
					IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_80MHZ |
					IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_160MHZ,
			},

			/* For all MCS and bandwidth, set 8 NSS for both Tx and
			 * Rx
			 */
			.eht_mcs_nss_supp = {
				/*
				 * As B1 and B2 are set in the supported
				 * channel width set field in the HE PHY
				 * capabilities information field include all
				 * the following MCS/NSS.
				 */
				.bw._80 = {
					.rx_tx_mcs9_max_nss = 0x88,
					.rx_tx_mcs11_max_nss = 0x88,
					.rx_tx_mcs13_max_nss = 0x88,
				},
				.bw._160 = {
					.rx_tx_mcs9_max_nss = 0x88,
					.rx_tx_mcs11_max_nss = 0x88,
					.rx_tx_mcs13_max_nss = 0x88,
				},
			},
			/* PPE threshold information is not supported */
		},
	},
#ifdef CONFIG_MAC80211_MESH
	{
		/* TODO: should we support other types, e.g., IBSS?*/
		.types_mask = BIT(NL80211_IFTYPE_MESH_POINT),
		.he_cap = {
			.has_he = true,
			.he_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_HE_MAC_CAP0_HTC_HE,
				.mac_cap_info[1] =
					IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8,
				.mac_cap_info[2] =
					IEEE80211_HE_MAC_CAP2_ACK_EN,
				.mac_cap_info[3] =
					IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
					IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3,
				.mac_cap_info[4] = IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU,
				.phy_cap_info[0] =
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G |
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G,
				.phy_cap_info[1] =
					IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK |
					IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
					IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD |
					IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS,
				.phy_cap_info[2] = 0,

				/* Leave all the other PHY capability bytes
				 * unset, as DCM, beam forming, RU and PPE
				 * threshold information are not supported
				 */
			},
			.he_mcs_nss_supp = {
				.rx_mcs_80 = cpu_to_le16(0xfffa),
				.tx_mcs_80 = cpu_to_le16(0xfffa),
				.rx_mcs_160 = cpu_to_le16(0xfffa),
				.tx_mcs_160 = cpu_to_le16(0xfffa),
				.rx_mcs_80p80 = cpu_to_le16(0xfffa),
				.tx_mcs_80p80 = cpu_to_le16(0xfffa),
			},
		},
	},
#endif
};

static const struct ieee80211_sband_iftype_data sband_capa_6ghz[] = {
	{
		.types_mask = BIT(NL80211_IFTYPE_STATION) |
			      BIT(NL80211_IFTYPE_P2P_CLIENT),
		.he_6ghz_capa = {
			.capa = cpu_to_le16(IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START |
					    IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP |
					    IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN |
					    IEEE80211_HE_6GHZ_CAP_SM_PS |
					    IEEE80211_HE_6GHZ_CAP_RD_RESPONDER |
					    IEEE80211_HE_6GHZ_CAP_TX_ANTPAT_CONS |
					    IEEE80211_HE_6GHZ_CAP_RX_ANTPAT_CONS),
		},
		.he_cap = {
			.has_he = true,
			.he_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_HE_MAC_CAP0_HTC_HE,
				.mac_cap_info[1] =
					IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US |
					IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8,
				.mac_cap_info[2] =
					IEEE80211_HE_MAC_CAP2_BSR |
					IEEE80211_HE_MAC_CAP2_MU_CASCADING |
					IEEE80211_HE_MAC_CAP2_ACK_EN,
				.mac_cap_info[3] =
					IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
					IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3,
				.mac_cap_info[4] = IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU,
				.phy_cap_info[0] =
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G |
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G,
				.phy_cap_info[1] =
					IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK |
					IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
					IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD |
					IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS,
				.phy_cap_info[2] =
					IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
					IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ |
					IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ |
					IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO |
					IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO,

				/* Leave all the other PHY capability bytes
				 * unset, as DCM, beam forming, RU and PPE
				 * threshold information are not supported
				 */
			},
			.he_mcs_nss_supp = {
				.rx_mcs_80 = cpu_to_le16(0xfffa),
				.tx_mcs_80 = cpu_to_le16(0xfffa),
				.rx_mcs_160 = cpu_to_le16(0xfffa),
				.tx_mcs_160 = cpu_to_le16(0xfffa),
				.rx_mcs_80p80 = cpu_to_le16(0xfffa),
				.tx_mcs_80p80 = cpu_to_le16(0xfffa),
			},
		},
		.eht_cap = {
			.has_eht = true,
			.eht_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_EHT_MAC_CAP0_EPCS_PRIO_ACCESS |
					IEEE80211_EHT_MAC_CAP0_OM_CONTROL |
					IEEE80211_EHT_MAC_CAP0_TRIG_TXOP_SHARING_MODE1,
				.phy_cap_info[0] =
					IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ |
					IEEE80211_EHT_PHY_CAP0_242_TONE_RU_GT20MHZ |
					IEEE80211_EHT_PHY_CAP0_NDP_4_EHT_LFT_32_GI |
					IEEE80211_EHT_PHY_CAP0_PARTIAL_BW_UL_MU_MIMO |
					IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMER |
					IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMEE |
					IEEE80211_EHT_PHY_CAP0_BEAMFORMEE_SS_80MHZ_MASK,
				.phy_cap_info[1] =
					IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_80MHZ_MASK |
					IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_160MHZ_MASK |
					IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_320MHZ_MASK,
				.phy_cap_info[2] =
					IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_80MHZ_MASK |
					IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_160MHZ_MASK |
					IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_320MHZ_MASK,
				.phy_cap_info[3] =
					IEEE80211_EHT_PHY_CAP3_NG_16_SU_FEEDBACK |
					IEEE80211_EHT_PHY_CAP3_NG_16_MU_FEEDBACK |
					IEEE80211_EHT_PHY_CAP3_CODEBOOK_4_2_SU_FDBK |
					IEEE80211_EHT_PHY_CAP3_CODEBOOK_7_5_MU_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_SU_BF_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_MU_BF_PART_BW_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_CQI_FDBK,
				.phy_cap_info[4] =
					IEEE80211_EHT_PHY_CAP4_PART_BW_DL_MU_MIMO |
					IEEE80211_EHT_PHY_CAP4_PSR_SR_SUPP |
					IEEE80211_EHT_PHY_CAP4_POWER_BOOST_FACT_SUPP |
					IEEE80211_EHT_PHY_CAP4_EHT_MU_PPDU_4_EHT_LTF_08_GI |
					IEEE80211_EHT_PHY_CAP4_MAX_NC_MASK,
				.phy_cap_info[5] =
					IEEE80211_EHT_PHY_CAP5_NON_TRIG_CQI_FEEDBACK |
					IEEE80211_EHT_PHY_CAP5_TX_LESS_242_TONE_RU_SUPP |
					IEEE80211_EHT_PHY_CAP5_RX_LESS_242_TONE_RU_SUPP |
					IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT |
					IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_MASK |
					IEEE80211_EHT_PHY_CAP5_MAX_NUM_SUPP_EHT_LTF_MASK,
				.phy_cap_info[6] =
					IEEE80211_EHT_PHY_CAP6_MAX_NUM_SUPP_EHT_LTF_MASK |
					IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_MASK |
					IEEE80211_EHT_PHY_CAP6_EHT_DUP_6GHZ_SUPP,
				.phy_cap_info[7] =
					IEEE80211_EHT_PHY_CAP7_20MHZ_STA_RX_NDP_WIDER_BW |
					IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_80MHZ |
					IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_160MHZ |
					IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_320MHZ |
					IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_80MHZ |
					IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_160MHZ |
					IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_320MHZ,
			},

			/* For all MCS and bandwidth, set 8 NSS for both Tx and
			 * Rx
			 */
			.eht_mcs_nss_supp = {
				/*
				 * As B1 and B2 are set in the supported
				 * channel width set field in the HE PHY
				 * capabilities information field and 320MHz in
				 * 6GHz is supported include all the following
				 * MCS/NSS.
				 */
				.bw._80 = {
					.rx_tx_mcs9_max_nss = 0x88,
					.rx_tx_mcs11_max_nss = 0x88,
					.rx_tx_mcs13_max_nss = 0x88,
				},
				.bw._160 = {
					.rx_tx_mcs9_max_nss = 0x88,
					.rx_tx_mcs11_max_nss = 0x88,
					.rx_tx_mcs13_max_nss = 0x88,
				},
				.bw._320 = {
					.rx_tx_mcs9_max_nss = 0x88,
					.rx_tx_mcs11_max_nss = 0x88,
					.rx_tx_mcs13_max_nss = 0x88,
				},
			},
			/* PPE threshold information is not supported */
		},
	},
	{
		.types_mask = BIT(NL80211_IFTYPE_AP) |
			      BIT(NL80211_IFTYPE_P2P_GO),
		.he_6ghz_capa = {
			.capa = cpu_to_le16(IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START |
					    IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP |
					    IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN |
					    IEEE80211_HE_6GHZ_CAP_SM_PS |
					    IEEE80211_HE_6GHZ_CAP_RD_RESPONDER |
					    IEEE80211_HE_6GHZ_CAP_TX_ANTPAT_CONS |
					    IEEE80211_HE_6GHZ_CAP_RX_ANTPAT_CONS),
		},
		.he_cap = {
			.has_he = true,
			.he_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_HE_MAC_CAP0_HTC_HE,
				.mac_cap_info[1] =
					IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US |
					IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8,
				.mac_cap_info[2] =
					IEEE80211_HE_MAC_CAP2_BSR |
					IEEE80211_HE_MAC_CAP2_MU_CASCADING |
					IEEE80211_HE_MAC_CAP2_ACK_EN,
				.mac_cap_info[3] =
					IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
					IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3,
				.mac_cap_info[4] = IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU,
				.phy_cap_info[0] =
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G |
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G,
				.phy_cap_info[1] =
					IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK |
					IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
					IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD |
					IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS,
				.phy_cap_info[2] =
					IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
					IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ |
					IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ |
					IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO |
					IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO,

				/* Leave all the other PHY capability bytes
				 * unset, as DCM, beam forming, RU and PPE
				 * threshold information are not supported
				 */
			},
			.he_mcs_nss_supp = {
				.rx_mcs_80 = cpu_to_le16(0xfffa),
				.tx_mcs_80 = cpu_to_le16(0xfffa),
				.rx_mcs_160 = cpu_to_le16(0xfffa),
				.tx_mcs_160 = cpu_to_le16(0xfffa),
				.rx_mcs_80p80 = cpu_to_le16(0xfffa),
				.tx_mcs_80p80 = cpu_to_le16(0xfffa),
			},
		},
		.eht_cap = {
			.has_eht = true,
			.eht_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_EHT_MAC_CAP0_EPCS_PRIO_ACCESS |
					IEEE80211_EHT_MAC_CAP0_OM_CONTROL |
					IEEE80211_EHT_MAC_CAP0_TRIG_TXOP_SHARING_MODE1,
				.phy_cap_info[0] =
					IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ |
					IEEE80211_EHT_PHY_CAP0_242_TONE_RU_GT20MHZ |
					IEEE80211_EHT_PHY_CAP0_NDP_4_EHT_LFT_32_GI |
					IEEE80211_EHT_PHY_CAP0_PARTIAL_BW_UL_MU_MIMO |
					IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMER |
					IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMEE |
					IEEE80211_EHT_PHY_CAP0_BEAMFORMEE_SS_80MHZ_MASK,
				.phy_cap_info[1] =
					IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_80MHZ_MASK |
					IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_160MHZ_MASK |
					IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_320MHZ_MASK,
				.phy_cap_info[2] =
					IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_80MHZ_MASK |
					IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_160MHZ_MASK |
					IEEE80211_EHT_PHY_CAP2_SOUNDING_DIM_320MHZ_MASK,
				.phy_cap_info[3] =
					IEEE80211_EHT_PHY_CAP3_NG_16_SU_FEEDBACK |
					IEEE80211_EHT_PHY_CAP3_NG_16_MU_FEEDBACK |
					IEEE80211_EHT_PHY_CAP3_CODEBOOK_4_2_SU_FDBK |
					IEEE80211_EHT_PHY_CAP3_CODEBOOK_7_5_MU_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_SU_BF_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_MU_BF_PART_BW_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_CQI_FDBK,
				.phy_cap_info[4] =
					IEEE80211_EHT_PHY_CAP4_PART_BW_DL_MU_MIMO |
					IEEE80211_EHT_PHY_CAP4_PSR_SR_SUPP |
					IEEE80211_EHT_PHY_CAP4_POWER_BOOST_FACT_SUPP |
					IEEE80211_EHT_PHY_CAP4_EHT_MU_PPDU_4_EHT_LTF_08_GI |
					IEEE80211_EHT_PHY_CAP4_MAX_NC_MASK,
				.phy_cap_info[5] =
					IEEE80211_EHT_PHY_CAP5_NON_TRIG_CQI_FEEDBACK |
					IEEE80211_EHT_PHY_CAP5_TX_LESS_242_TONE_RU_SUPP |
					IEEE80211_EHT_PHY_CAP5_RX_LESS_242_TONE_RU_SUPP |
					IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT |
					IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_MASK |
					IEEE80211_EHT_PHY_CAP5_MAX_NUM_SUPP_EHT_LTF_MASK,
				.phy_cap_info[6] =
					IEEE80211_EHT_PHY_CAP6_MAX_NUM_SUPP_EHT_LTF_MASK |
					IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_MASK |
					IEEE80211_EHT_PHY_CAP6_EHT_DUP_6GHZ_SUPP,
				.phy_cap_info[7] =
					IEEE80211_EHT_PHY_CAP7_20MHZ_STA_RX_NDP_WIDER_BW |
					IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_80MHZ |
					IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_160MHZ |
					IEEE80211_EHT_PHY_CAP7_NON_OFDMA_UL_MU_MIMO_320MHZ |
					IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_80MHZ |
					IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_160MHZ |
					IEEE80211_EHT_PHY_CAP7_MU_BEAMFORMER_320MHZ,
			},

			/* For all MCS and bandwidth, set 8 NSS for both Tx and
			 * Rx
			 */
			.eht_mcs_nss_supp = {
				/*
				 * As B1 and B2 are set in the supported
				 * channel width set field in the HE PHY
				 * capabilities information field and 320MHz in
				 * 6GHz is supported include all the following
				 * MCS/NSS.
				 */
				.bw._80 = {
					.rx_tx_mcs9_max_nss = 0x88,
					.rx_tx_mcs11_max_nss = 0x88,
					.rx_tx_mcs13_max_nss = 0x88,
				},
				.bw._160 = {
					.rx_tx_mcs9_max_nss = 0x88,
					.rx_tx_mcs11_max_nss = 0x88,
					.rx_tx_mcs13_max_nss = 0x88,
				},
				.bw._320 = {
					.rx_tx_mcs9_max_nss = 0x88,
					.rx_tx_mcs11_max_nss = 0x88,
					.rx_tx_mcs13_max_nss = 0x88,
				},
			},
			/* PPE threshold information is not supported */
		},
	},
#ifdef CONFIG_MAC80211_MESH
	{
		/* TODO: should we support other types, e.g., IBSS?*/
		.types_mask = BIT(NL80211_IFTYPE_MESH_POINT),
		.he_6ghz_capa = {
			.capa = cpu_to_le16(IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START |
					    IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP |
					    IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN |
					    IEEE80211_HE_6GHZ_CAP_SM_PS |
					    IEEE80211_HE_6GHZ_CAP_RD_RESPONDER |
					    IEEE80211_HE_6GHZ_CAP_TX_ANTPAT_CONS |
					    IEEE80211_HE_6GHZ_CAP_RX_ANTPAT_CONS),
		},
		.he_cap = {
			.has_he = true,
			.he_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_HE_MAC_CAP0_HTC_HE,
				.mac_cap_info[1] =
					IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8,
				.mac_cap_info[2] =
					IEEE80211_HE_MAC_CAP2_ACK_EN,
				.mac_cap_info[3] =
					IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
					IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3,
				.mac_cap_info[4] = IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU,
				.phy_cap_info[0] =
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G |
					IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G,
				.phy_cap_info[1] =
					IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK |
					IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
					IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD |
					IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS,
				.phy_cap_info[2] = 0,

				/* Leave all the other PHY capability bytes
				 * unset, as DCM, beam forming, RU and PPE
				 * threshold information are not supported
				 */
			},
			.he_mcs_nss_supp = {
				.rx_mcs_80 = cpu_to_le16(0xfffa),
				.tx_mcs_80 = cpu_to_le16(0xfffa),
				.rx_mcs_160 = cpu_to_le16(0xfffa),
				.tx_mcs_160 = cpu_to_le16(0xfffa),
				.rx_mcs_80p80 = cpu_to_le16(0xfffa),
				.tx_mcs_80p80 = cpu_to_le16(0xfffa),
			},
		},
		.eht_cap = {
			.has_eht = true,
			.eht_cap_elem = {
				.mac_cap_info[0] = IEEE80211_EHT_MAC_CAP0_OM_CONTROL |
						   IEEE80211_EHT_MAC_CAP0_TRIG_TXOP_SHARING_MODE1,
				.phy_cap_info[0] = IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ,
				/* Leave all the other PHY capability bytes
				 * unset, as DCM, beam forming, RU and PPE
				 * threshold information are not supported
				 */
			},
			/* For all MCS and bandwidth, set 8 NSS for both Tx and
			 * Rx
			 */
			.eht_mcs_nss_supp = {
				/* As B1 and B2 are set in the supported
				 * channel width set field in the HE PHY
				 * capabilities information field and 320MHz in
				 * 6GHz is supported include all the following
				 * MCS/NSS.
				 */
				.bw._80 = {
					.rx_tx_mcs9_max_nss = 0x88,
					.rx_tx_mcs11_max_nss = 0x88,
					.rx_tx_mcs13_max_nss = 0x88,
				},
				.bw._160 = {
					.rx_tx_mcs9_max_nss = 0x88,
					.rx_tx_mcs11_max_nss = 0x88,
					.rx_tx_mcs13_max_nss = 0x88,
				},
				.bw._320 = {
					.rx_tx_mcs9_max_nss = 0x88,
					.rx_tx_mcs11_max_nss = 0x88,
					.rx_tx_mcs13_max_nss = 0x88,
				},
			},
			/* PPE threshold information is not supported */
		},
	},
#endif
};

static void mac80211_hwsim_sband_capab(struct ieee80211_supported_band *sband)
{
	switch (sband->band) {
	case NL80211_BAND_2GHZ:
		ieee80211_set_sband_iftype_data(sband, sband_capa_2ghz);
		break;
	case NL80211_BAND_5GHZ:
		ieee80211_set_sband_iftype_data(sband, sband_capa_5ghz);
		break;
	case NL80211_BAND_6GHZ:
		ieee80211_set_sband_iftype_data(sband, sband_capa_6ghz);
		break;
	default:
		break;
	}
}

#ifdef CONFIG_MAC80211_MESH
#define HWSIM_MESH_BIT BIT(NL80211_IFTYPE_MESH_POINT)
#else
#define HWSIM_MESH_BIT 0
#endif

#define HWSIM_DEFAULT_IF_LIMIT \
	(BIT(NL80211_IFTYPE_STATION) | \
	 BIT(NL80211_IFTYPE_P2P_CLIENT) | \
	 BIT(NL80211_IFTYPE_AP) | \
	 BIT(NL80211_IFTYPE_P2P_GO) | \
	 HWSIM_MESH_BIT)

#define HWSIM_IFTYPE_SUPPORT_MASK \
	(BIT(NL80211_IFTYPE_STATION) | \
	 BIT(NL80211_IFTYPE_AP) | \
	 BIT(NL80211_IFTYPE_P2P_CLIENT) | \
	 BIT(NL80211_IFTYPE_P2P_GO) | \
	 BIT(NL80211_IFTYPE_ADHOC) | \
	 BIT(NL80211_IFTYPE_MESH_POINT) | \
	 BIT(NL80211_IFTYPE_OCB))

static const u8 iftypes_ext_capa_ap[] = {
	 [0] = WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING,
	 [2] = WLAN_EXT_CAPA3_MULTI_BSSID_SUPPORT,
	 [7] = WLAN_EXT_CAPA8_OPMODE_NOTIF |
	       WLAN_EXT_CAPA8_MAX_MSDU_IN_AMSDU_LSB,
	 [8] = WLAN_EXT_CAPA9_MAX_MSDU_IN_AMSDU_MSB,
	 [9] = WLAN_EXT_CAPA10_TWT_RESPONDER_SUPPORT,
};

#define MAC80211_HWSIM_MLD_CAPA_OPS				\
	FIELD_PREP_CONST(IEEE80211_MLD_CAP_OP_TID_TO_LINK_MAP_NEG_SUPP, \
			 IEEE80211_MLD_CAP_OP_TID_TO_LINK_MAP_NEG_SUPP_SAME) | \
	FIELD_PREP_CONST(IEEE80211_MLD_CAP_OP_MAX_SIMUL_LINKS, \
			 IEEE80211_MLD_MAX_NUM_LINKS - 1)

static const struct wiphy_iftype_ext_capab mac80211_hwsim_iftypes_ext_capa[] = {
	{
		.iftype = NL80211_IFTYPE_AP,
		.extended_capabilities = iftypes_ext_capa_ap,
		.extended_capabilities_mask = iftypes_ext_capa_ap,
		.extended_capabilities_len = sizeof(iftypes_ext_capa_ap),
		.eml_capabilities = IEEE80211_EML_CAP_EMLSR_SUPP |
				    IEEE80211_EML_CAP_EMLMR_SUPPORT,
		.mld_capa_and_ops = MAC80211_HWSIM_MLD_CAPA_OPS,
	},
};

static int mac80211_hwsim_new_radio(struct genl_info *info,
				    struct hwsim_new_radio_params *param)
{
	int err;
	u8 addr[ETH_ALEN];
	struct mac80211_hwsim_data *data;
	struct ieee80211_hw *hw;
	enum nl80211_band band;
	const struct ieee80211_ops *ops = &mac80211_hwsim_ops;
	struct net *net;
	int idx, i;
	int n_limits = 0;
	int n_bands = 0;

	if (WARN_ON(param->channels > 1 && !param->use_chanctx))
		return -EINVAL;

	spin_lock_bh(&hwsim_radio_lock);
	idx = hwsim_radio_idx++;
	spin_unlock_bh(&hwsim_radio_lock);

	if (param->mlo)
		ops = &mac80211_hwsim_mlo_ops;
	else if (param->use_chanctx)
		ops = &mac80211_hwsim_mchan_ops;
	hw = ieee80211_alloc_hw_nm(sizeof(*data), ops, param->hwname);
	if (!hw) {
		pr_debug("mac80211_hwsim: ieee80211_alloc_hw failed\n");
		err = -ENOMEM;
		goto failed;
	}

	/* ieee80211_alloc_hw_nm may have used a default name */
	param->hwname = wiphy_name(hw->wiphy);

	if (info)
		net = genl_info_net(info);
	else
		net = &init_net;
	wiphy_net_set(hw->wiphy, net);

	data = hw->priv;
	data->hw = hw;

	data->dev = device_create(hwsim_class, NULL, 0, hw, "hwsim%d", idx);
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
		pr_debug("mac80211_hwsim: device_bind_driver failed (%d)\n",
		       err);
		goto failed_bind;
	}

	skb_queue_head_init(&data->pending);

	SET_IEEE80211_DEV(hw, data->dev);
	if (!param->perm_addr) {
		eth_zero_addr(addr);
		addr[0] = 0x02;
		addr[3] = idx >> 8;
		addr[4] = idx;
		memcpy(data->addresses[0].addr, addr, ETH_ALEN);
		/* Why need here second address ? */
		memcpy(data->addresses[1].addr, addr, ETH_ALEN);
		data->addresses[1].addr[0] |= 0x40;
		hw->wiphy->n_addresses = 2;
		hw->wiphy->addresses = data->addresses;
		/* possible address clash is checked at hash table insertion */
	} else {
		memcpy(data->addresses[0].addr, param->perm_addr, ETH_ALEN);
		/* compatibility with automatically generated mac addr */
		memcpy(data->addresses[1].addr, param->perm_addr, ETH_ALEN);
		hw->wiphy->n_addresses = 2;
		hw->wiphy->addresses = data->addresses;
	}

	data->channels = param->channels;
	data->use_chanctx = param->use_chanctx;
	data->idx = idx;
	data->destroy_on_close = param->destroy_on_close;
	if (info)
		data->portid = info->snd_portid;

	/* setup interface limits, only on interface types we support */
	if (param->iftypes & BIT(NL80211_IFTYPE_ADHOC)) {
		data->if_limits[n_limits].max = 1;
		data->if_limits[n_limits].types = BIT(NL80211_IFTYPE_ADHOC);
		n_limits++;
	}

	if (param->iftypes & HWSIM_DEFAULT_IF_LIMIT) {
		data->if_limits[n_limits].max = 2048;
		/*
		 * For this case, we may only support a subset of
		 * HWSIM_DEFAULT_IF_LIMIT, therefore we only want to add the
		 * bits that both param->iftype & HWSIM_DEFAULT_IF_LIMIT have.
		 */
		data->if_limits[n_limits].types =
					HWSIM_DEFAULT_IF_LIMIT & param->iftypes;
		n_limits++;
	}

	if (param->iftypes & BIT(NL80211_IFTYPE_P2P_DEVICE)) {
		data->if_limits[n_limits].max = 1;
		data->if_limits[n_limits].types =
						BIT(NL80211_IFTYPE_P2P_DEVICE);
		n_limits++;
	}

	data->if_combination.radar_detect_widths =
				BIT(NL80211_CHAN_WIDTH_5) |
				BIT(NL80211_CHAN_WIDTH_10) |
				BIT(NL80211_CHAN_WIDTH_20_NOHT) |
				BIT(NL80211_CHAN_WIDTH_20) |
				BIT(NL80211_CHAN_WIDTH_40) |
				BIT(NL80211_CHAN_WIDTH_80) |
				BIT(NL80211_CHAN_WIDTH_160);

	if (data->use_chanctx) {
		hw->wiphy->max_scan_ssids = 255;
		hw->wiphy->max_scan_ie_len = IEEE80211_MAX_DATA_LEN;
		hw->wiphy->max_remain_on_channel_duration = 1000;
		data->if_combination.num_different_channels = data->channels;
	} else {
		data->if_combination.num_different_channels = 1;
	}

	if (!n_limits) {
		err = -EINVAL;
		goto failed_hw;
	}

	data->if_combination.max_interfaces = 0;
	for (i = 0; i < n_limits; i++)
		data->if_combination.max_interfaces +=
			data->if_limits[i].max;

	data->if_combination.n_limits = n_limits;
	data->if_combination.limits = data->if_limits;

	/*
	 * If we actually were asked to support combinations,
	 * advertise them - if there's only a single thing like
	 * only IBSS then don't advertise it as combinations.
	 */
	if (data->if_combination.max_interfaces > 1) {
		hw->wiphy->iface_combinations = &data->if_combination;
		hw->wiphy->n_iface_combinations = 1;
	}

	if (param->ciphers) {
		memcpy(data->ciphers, param->ciphers,
		       param->n_ciphers * sizeof(u32));
		hw->wiphy->cipher_suites = data->ciphers;
		hw->wiphy->n_cipher_suites = param->n_ciphers;
	}

	hw->wiphy->mbssid_max_interfaces = 8;
	hw->wiphy->ema_max_profile_periodicity = 3;

	data->rx_rssi = DEFAULT_RX_RSSI;

	INIT_DELAYED_WORK(&data->roc_start, hw_roc_start);
	INIT_DELAYED_WORK(&data->roc_done, hw_roc_done);
	INIT_DELAYED_WORK(&data->hw_scan, hw_scan_work);

	hw->queues = 5;
	hw->offchannel_tx_hw_queue = 4;

	ieee80211_hw_set(hw, SUPPORT_FAST_XMIT);
	ieee80211_hw_set(hw, CHANCTX_STA_CSA);
	ieee80211_hw_set(hw, SUPPORTS_HT_CCK_RATES);
	ieee80211_hw_set(hw, QUEUE_CONTROL);
	ieee80211_hw_set(hw, WANT_MONITOR_VIF);
	ieee80211_hw_set(hw, AMPDU_AGGREGATION);
	ieee80211_hw_set(hw, MFP_CAPABLE);
	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, SUPPORTS_PS);
	ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(hw, TDLS_WIDER_BW);
	ieee80211_hw_set(hw, SUPPORTS_MULTI_BSSID);

	if (param->mlo) {
		hw->wiphy->flags |= WIPHY_FLAG_SUPPORTS_MLO;
		ieee80211_hw_set(hw, HAS_RATE_CONTROL);
		ieee80211_hw_set(hw, SUPPORTS_DYNAMIC_PS);
		ieee80211_hw_set(hw, CONNECTION_MONITOR);
		ieee80211_hw_set(hw, AP_LINK_PS);

		hw->wiphy->iftype_ext_capab = mac80211_hwsim_iftypes_ext_capa;
		hw->wiphy->num_iftype_ext_capab =
			ARRAY_SIZE(mac80211_hwsim_iftypes_ext_capa);
	} else {
		ieee80211_hw_set(hw, HOST_BROADCAST_PS_BUFFERING);
		ieee80211_hw_set(hw, PS_NULLFUNC_STACK);
		if (rctbl)
			ieee80211_hw_set(hw, SUPPORTS_RC_TABLE);
	}

	hw->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;
	hw->wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS |
			    WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL |
			    WIPHY_FLAG_AP_UAPSD |
			    WIPHY_FLAG_SUPPORTS_5_10_MHZ |
			    WIPHY_FLAG_HAS_CHANNEL_SWITCH;
	hw->wiphy->features |= NL80211_FEATURE_ACTIVE_MONITOR |
			       NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE |
			       NL80211_FEATURE_STATIC_SMPS |
			       NL80211_FEATURE_DYNAMIC_SMPS |
			       NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR;
	wiphy_ext_feature_set(hw->wiphy, NL80211_EXT_FEATURE_VHT_IBSS);
	wiphy_ext_feature_set(hw->wiphy, NL80211_EXT_FEATURE_BEACON_PROTECTION);
	wiphy_ext_feature_set(hw->wiphy,
			      NL80211_EXT_FEATURE_MULTICAST_REGISTRATIONS);
	wiphy_ext_feature_set(hw->wiphy,
			      NL80211_EXT_FEATURE_BEACON_RATE_LEGACY);
	wiphy_ext_feature_set(hw->wiphy, NL80211_EXT_FEATURE_ENABLE_FTM_RESPONDER);

	wiphy_ext_feature_set(hw->wiphy,
			      NL80211_EXT_FEATURE_SCAN_MIN_PREQ_CONTENT);
	wiphy_ext_feature_set(hw->wiphy,
			      NL80211_EXT_FEATURE_BSS_COLOR);

	hw->wiphy->interface_modes = param->iftypes;

	/* ask mac80211 to reserve space for magic */
	hw->vif_data_size = sizeof(struct hwsim_vif_priv);
	hw->sta_data_size = sizeof(struct hwsim_sta_priv);
	hw->chanctx_data_size = sizeof(struct hwsim_chanctx_priv);

	memcpy(data->channels_2ghz, hwsim_channels_2ghz,
		sizeof(hwsim_channels_2ghz));
	memcpy(data->channels_5ghz, hwsim_channels_5ghz,
		sizeof(hwsim_channels_5ghz));
	memcpy(data->channels_6ghz, hwsim_channels_6ghz,
		sizeof(hwsim_channels_6ghz));
	memcpy(data->channels_s1g, hwsim_channels_s1g,
	       sizeof(hwsim_channels_s1g));
	memcpy(data->rates, hwsim_rates, sizeof(hwsim_rates));

	for (band = NL80211_BAND_2GHZ; band < NUM_NL80211_BANDS; band++) {
		struct ieee80211_supported_band *sband = &data->bands[band];
		struct wiphy_radio_freq_range *radio_range;
		const struct ieee80211_channel *c;
		struct wiphy_radio *radio;

		sband->band = band;

		switch (band) {
		case NL80211_BAND_2GHZ:
			sband->channels = data->channels_2ghz;
			sband->n_channels = ARRAY_SIZE(hwsim_channels_2ghz);
			sband->bitrates = data->rates;
			sband->n_bitrates = ARRAY_SIZE(hwsim_rates);
			break;
		case NL80211_BAND_5GHZ:
			sband->channels = data->channels_5ghz;
			sband->n_channels = ARRAY_SIZE(hwsim_channels_5ghz);
			sband->bitrates = data->rates + 4;
			sband->n_bitrates = ARRAY_SIZE(hwsim_rates) - 4;

			sband->vht_cap.vht_supported = true;
			sband->vht_cap.cap =
				IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 |
				IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160_80PLUS80MHZ |
				IEEE80211_VHT_CAP_RXLDPC |
				IEEE80211_VHT_CAP_SHORT_GI_80 |
				IEEE80211_VHT_CAP_SHORT_GI_160 |
				IEEE80211_VHT_CAP_TXSTBC |
				IEEE80211_VHT_CAP_RXSTBC_4 |
				IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK;
			sband->vht_cap.vht_mcs.rx_mcs_map =
				cpu_to_le16(IEEE80211_VHT_MCS_SUPPORT_0_9 << 0 |
					    IEEE80211_VHT_MCS_SUPPORT_0_9 << 2 |
					    IEEE80211_VHT_MCS_SUPPORT_0_9 << 4 |
					    IEEE80211_VHT_MCS_SUPPORT_0_9 << 6 |
					    IEEE80211_VHT_MCS_SUPPORT_0_9 << 8 |
					    IEEE80211_VHT_MCS_SUPPORT_0_9 << 10 |
					    IEEE80211_VHT_MCS_SUPPORT_0_9 << 12 |
					    IEEE80211_VHT_MCS_SUPPORT_0_9 << 14);
			sband->vht_cap.vht_mcs.tx_mcs_map =
				sband->vht_cap.vht_mcs.rx_mcs_map;
			break;
		case NL80211_BAND_6GHZ:
			sband->channels = data->channels_6ghz;
			sband->n_channels = ARRAY_SIZE(hwsim_channels_6ghz);
			sband->bitrates = data->rates + 4;
			sband->n_bitrates = ARRAY_SIZE(hwsim_rates) - 4;
			break;
		case NL80211_BAND_S1GHZ:
			memcpy(&sband->s1g_cap, &hwsim_s1g_cap,
			       sizeof(sband->s1g_cap));
			sband->channels = data->channels_s1g;
			sband->n_channels = ARRAY_SIZE(hwsim_channels_s1g);
			break;
		default:
			continue;
		}

		if (band != NL80211_BAND_6GHZ){
			sband->ht_cap.ht_supported = true;
			sband->ht_cap.cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
					    IEEE80211_HT_CAP_GRN_FLD |
					    IEEE80211_HT_CAP_SGI_20 |
					    IEEE80211_HT_CAP_SGI_40 |
					    IEEE80211_HT_CAP_DSSSCCK40;
			sband->ht_cap.ampdu_factor = 0x3;
			sband->ht_cap.ampdu_density = 0x6;
			memset(&sband->ht_cap.mcs, 0,
			       sizeof(sband->ht_cap.mcs));
			sband->ht_cap.mcs.rx_mask[0] = 0xff;
			sband->ht_cap.mcs.rx_mask[1] = 0xff;
			sband->ht_cap.mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
		}

		mac80211_hwsim_sband_capab(sband);

		hw->wiphy->bands[band] = sband;

		if (!param->multi_radio)
			continue;

		c = sband->channels;
		radio_range = &data->radio_range[n_bands];
		radio_range->start_freq = ieee80211_channel_to_khz(c) - 10000;

		c += sband->n_channels - 1;
		radio_range->end_freq = ieee80211_channel_to_khz(c) + 10000;

		radio = &data->radio[n_bands++];
		radio->freq_range = radio_range;
		radio->n_freq_range = 1;
		radio->iface_combinations = &data->if_combination_radio;
		radio->n_iface_combinations = 1;
	}

	if (param->multi_radio) {
		hw->wiphy->radio = data->radio;
		hw->wiphy->n_radio = n_bands;

		memcpy(&data->if_combination_radio, &data->if_combination,
		       sizeof(data->if_combination));
		data->if_combination.num_different_channels *= n_bands;
	}

	if (data->use_chanctx)
		data->if_combination.radar_detect_widths = 0;

	/* By default all radios belong to the first group */
	data->group = 1;
	mutex_init(&data->mutex);

	data->netgroup = hwsim_net_get_netgroup(net);
	data->wmediumd = hwsim_net_get_wmediumd(net);

	/* Enable frame retransmissions for lossy channels */
	hw->max_rates = 4;
	hw->max_rate_tries = 11;

	hw->wiphy->vendor_commands = mac80211_hwsim_vendor_commands;
	hw->wiphy->n_vendor_commands =
		ARRAY_SIZE(mac80211_hwsim_vendor_commands);
	hw->wiphy->vendor_events = mac80211_hwsim_vendor_events;
	hw->wiphy->n_vendor_events = ARRAY_SIZE(mac80211_hwsim_vendor_events);

	if (param->reg_strict)
		hw->wiphy->regulatory_flags |= REGULATORY_STRICT_REG;
	if (param->regd) {
		data->regd = param->regd;
		hw->wiphy->regulatory_flags |= REGULATORY_CUSTOM_REG;
		wiphy_apply_custom_regulatory(hw->wiphy, param->regd);
		/* give the regulatory workqueue a chance to run */
		schedule_timeout_interruptible(1);
	}

	wiphy_ext_feature_set(hw->wiphy,
			      NL80211_EXT_FEATURE_DFS_CONCURRENT);

	if (param->no_vif)
		ieee80211_hw_set(hw, NO_AUTO_VIF);

	wiphy_ext_feature_set(hw->wiphy, NL80211_EXT_FEATURE_CQM_RSSI_LIST);

	for (i = 0; i < ARRAY_SIZE(data->link_data); i++) {
		hrtimer_setup(&data->link_data[i].beacon_timer, mac80211_hwsim_beacon,
			      CLOCK_MONOTONIC, HRTIMER_MODE_ABS_SOFT);
		data->link_data[i].link_id = i;
	}

	err = ieee80211_register_hw(hw);
	if (err < 0) {
		pr_debug("mac80211_hwsim: ieee80211_register_hw failed (%d)\n",
		       err);
		goto failed_hw;
	}

	wiphy_dbg(hw->wiphy, "hwaddr %pM registered\n", hw->wiphy->perm_addr);

	if (param->reg_alpha2) {
		data->alpha2[0] = param->reg_alpha2[0];
		data->alpha2[1] = param->reg_alpha2[1];
		regulatory_hint(hw->wiphy, param->reg_alpha2);
	}

	data->debugfs = debugfs_create_dir("hwsim", hw->wiphy->debugfsdir);
	debugfs_create_file("ps", 0666, data->debugfs, data, &hwsim_fops_ps);
	debugfs_create_file("group", 0666, data->debugfs, data,
			    &hwsim_fops_group);
	debugfs_create_file("rx_rssi", 0666, data->debugfs, data,
			    &hwsim_fops_rx_rssi);
	if (!data->use_chanctx)
		debugfs_create_file("dfs_simulate_radar", 0222,
				    data->debugfs,
				    data, &hwsim_simulate_radar);

	if (param->pmsr_capa) {
		data->pmsr_capa = *param->pmsr_capa;
		hw->wiphy->pmsr_capa = &data->pmsr_capa;
	}

	spin_lock_bh(&hwsim_radio_lock);
	err = rhashtable_insert_fast(&hwsim_radios_rht, &data->rht,
				     hwsim_rht_params);
	if (err < 0) {
		if (info) {
			GENL_SET_ERR_MSG(info, "perm addr already present");
			NL_SET_BAD_ATTR(info->extack,
					info->attrs[HWSIM_ATTR_PERM_ADDR]);
		}
		spin_unlock_bh(&hwsim_radio_lock);
		goto failed_final_insert;
	}

	list_add_tail(&data->list, &hwsim_radios);
	hwsim_radios_generation++;
	spin_unlock_bh(&hwsim_radio_lock);

	hwsim_mcast_new_radio(idx, info, param);

	return idx;

failed_final_insert:
	debugfs_remove_recursive(data->debugfs);
	ieee80211_unregister_hw(data->hw);
failed_hw:
	device_release_driver(data->dev);
failed_bind:
	device_unregister(data->dev);
failed_drvdata:
	ieee80211_free_hw(hw);
failed:
	return err;
}

static void hwsim_mcast_del_radio(int id, const char *hwname,
				  struct genl_info *info)
{
	struct sk_buff *skb;
	void *data;
	int ret;

	skb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb)
		return;

	data = genlmsg_put(skb, 0, 0, &hwsim_genl_family, 0,
			   HWSIM_CMD_DEL_RADIO);
	if (!data)
		goto error;

	ret = nla_put_u32(skb, HWSIM_ATTR_RADIO_ID, id);
	if (ret < 0)
		goto error;

	ret = nla_put(skb, HWSIM_ATTR_RADIO_NAME, strlen(hwname),
		      hwname);
	if (ret < 0)
		goto error;

	genlmsg_end(skb, data);

	hwsim_mcast_config_msg(skb, info);

	return;

error:
	nlmsg_free(skb);
}

static void mac80211_hwsim_del_radio(struct mac80211_hwsim_data *data,
				     const char *hwname,
				     struct genl_info *info)
{
	hwsim_mcast_del_radio(data->idx, hwname, info);
	debugfs_remove_recursive(data->debugfs);
	ieee80211_unregister_hw(data->hw);
	device_release_driver(data->dev);
	device_unregister(data->dev);
	ieee80211_free_hw(data->hw);
}

static int mac80211_hwsim_get_radio(struct sk_buff *skb,
				    struct mac80211_hwsim_data *data,
				    u32 portid, u32 seq,
				    struct netlink_callback *cb, int flags)
{
	void *hdr;
	struct hwsim_new_radio_params param = { };
	int res = -EMSGSIZE;

	hdr = genlmsg_put(skb, portid, seq, &hwsim_genl_family, flags,
			  HWSIM_CMD_GET_RADIO);
	if (!hdr)
		return -EMSGSIZE;

	if (cb)
		genl_dump_check_consistent(cb, hdr);

	if (data->alpha2[0] && data->alpha2[1])
		param.reg_alpha2 = data->alpha2;

	param.reg_strict = !!(data->hw->wiphy->regulatory_flags &
					REGULATORY_STRICT_REG);
	param.p2p_device = !!(data->hw->wiphy->interface_modes &
					BIT(NL80211_IFTYPE_P2P_DEVICE));
	param.use_chanctx = data->use_chanctx;
	param.regd = data->regd;
	param.channels = data->channels;
	param.hwname = wiphy_name(data->hw->wiphy);
	param.pmsr_capa = &data->pmsr_capa;

	res = append_radio_msg(skb, data->idx, &param);
	if (res < 0)
		goto out_err;

	genlmsg_end(skb, hdr);
	return 0;

out_err:
	genlmsg_cancel(skb, hdr);
	return res;
}

static void mac80211_hwsim_free(void)
{
	struct mac80211_hwsim_data *data;

	spin_lock_bh(&hwsim_radio_lock);
	while ((data = list_first_entry_or_null(&hwsim_radios,
						struct mac80211_hwsim_data,
						list))) {
		list_del(&data->list);
		spin_unlock_bh(&hwsim_radio_lock);
		mac80211_hwsim_del_radio(data, wiphy_name(data->hw->wiphy),
					 NULL);
		spin_lock_bh(&hwsim_radio_lock);
	}
	spin_unlock_bh(&hwsim_radio_lock);
	class_destroy(hwsim_class);
}

static const struct net_device_ops hwsim_netdev_ops = {
	.ndo_start_xmit 	= hwsim_mon_xmit,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

static void hwsim_mon_setup(struct net_device *dev)
{
	u8 addr[ETH_ALEN];

	dev->netdev_ops = &hwsim_netdev_ops;
	dev->needs_free_netdev = true;
	ether_setup(dev);
	dev->priv_flags |= IFF_NO_QUEUE;
	dev->type = ARPHRD_IEEE80211_RADIOTAP;
	eth_zero_addr(addr);
	addr[0] = 0x12;
	eth_hw_addr_set(dev, addr);
}

static void hwsim_register_wmediumd(struct net *net, u32 portid)
{
	struct mac80211_hwsim_data *data;

	hwsim_net_set_wmediumd(net, portid);

	spin_lock_bh(&hwsim_radio_lock);
	list_for_each_entry(data, &hwsim_radios, list) {
		if (data->netgroup == hwsim_net_get_netgroup(net))
			data->wmediumd = portid;
	}
	spin_unlock_bh(&hwsim_radio_lock);
}

static int hwsim_tx_info_frame_received_nl(struct sk_buff *skb_2,
					   struct genl_info *info)
{

	struct ieee80211_hdr *hdr;
	struct mac80211_hwsim_data *data2;
	struct ieee80211_tx_info *txi;
	struct hwsim_tx_rate *tx_attempts;
	u64 ret_skb_cookie;
	struct sk_buff *skb, *tmp;
	const u8 *src;
	unsigned int hwsim_flags;
	int i;
	unsigned long flags;
	bool found = false;

	if (!info->attrs[HWSIM_ATTR_ADDR_TRANSMITTER] ||
	    !info->attrs[HWSIM_ATTR_FLAGS] ||
	    !info->attrs[HWSIM_ATTR_COOKIE] ||
	    !info->attrs[HWSIM_ATTR_SIGNAL] ||
	    !info->attrs[HWSIM_ATTR_TX_INFO])
		goto out;

	src = (void *)nla_data(info->attrs[HWSIM_ATTR_ADDR_TRANSMITTER]);
	hwsim_flags = nla_get_u32(info->attrs[HWSIM_ATTR_FLAGS]);
	ret_skb_cookie = nla_get_u64(info->attrs[HWSIM_ATTR_COOKIE]);

	data2 = get_hwsim_data_ref_from_addr(src);
	if (!data2)
		goto out;

	if (!hwsim_virtio_enabled) {
		if (hwsim_net_get_netgroup(genl_info_net(info)) !=
		    data2->netgroup)
			goto out;

		if (info->snd_portid != data2->wmediumd)
			goto out;
	}

	/* look for the skb matching the cookie passed back from user */
	spin_lock_irqsave(&data2->pending.lock, flags);
	skb_queue_walk_safe(&data2->pending, skb, tmp) {
		uintptr_t skb_cookie;

		txi = IEEE80211_SKB_CB(skb);
		skb_cookie = (uintptr_t)txi->rate_driver_data[0];

		if (skb_cookie == ret_skb_cookie) {
			__skb_unlink(skb, &data2->pending);
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&data2->pending.lock, flags);

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
	}

	txi->status.ack_signal = nla_get_u32(info->attrs[HWSIM_ATTR_SIGNAL]);

	if (!(hwsim_flags & HWSIM_TX_CTL_NO_ACK) &&
	   (hwsim_flags & HWSIM_TX_STAT_ACK)) {
		if (skb->len >= 16) {
			hdr = (struct ieee80211_hdr *) skb->data;
			mac80211_hwsim_monitor_ack(data2->channel,
						   hdr->addr2);
		}
		txi->flags |= IEEE80211_TX_STAT_ACK;
	}

	if (hwsim_flags & HWSIM_TX_CTL_NO_ACK)
		txi->flags |= IEEE80211_TX_STAT_NOACK_TRANSMITTED;

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
	struct ieee80211_hdr *hdr;
	const u8 *dst;
	int frame_data_len;
	void *frame_data;
	struct sk_buff *skb = NULL;
	struct ieee80211_channel *channel = NULL;

	if (!info->attrs[HWSIM_ATTR_ADDR_RECEIVER] ||
	    !info->attrs[HWSIM_ATTR_FRAME] ||
	    !info->attrs[HWSIM_ATTR_RX_RATE] ||
	    !info->attrs[HWSIM_ATTR_SIGNAL])
		goto out;

	dst = (void *)nla_data(info->attrs[HWSIM_ATTR_ADDR_RECEIVER]);
	frame_data_len = nla_len(info->attrs[HWSIM_ATTR_FRAME]);
	frame_data = (void *)nla_data(info->attrs[HWSIM_ATTR_FRAME]);

	if (frame_data_len < sizeof(struct ieee80211_hdr_3addr) ||
	    frame_data_len > IEEE80211_MAX_DATA_LEN)
		goto err;

	/* Allocate new skb here */
	skb = alloc_skb(frame_data_len, GFP_KERNEL);
	if (skb == NULL)
		goto err;

	/* Copy the data */
	skb_put_data(skb, frame_data, frame_data_len);

	data2 = get_hwsim_data_ref_from_addr(dst);
	if (!data2)
		goto out;

	if (data2->use_chanctx) {
		if (data2->tmp_chan)
			channel = data2->tmp_chan;
	} else {
		channel = data2->channel;
	}

	if (!hwsim_virtio_enabled) {
		if (hwsim_net_get_netgroup(genl_info_net(info)) !=
		    data2->netgroup)
			goto out;

		if (info->snd_portid != data2->wmediumd)
			goto out;
	}

	/* check if radio is configured properly */

	if ((data2->idle && !data2->tmp_chan) || !data2->started)
		goto out;

	/* A frame is received from user space */
	memset(&rx_status, 0, sizeof(rx_status));
	if (info->attrs[HWSIM_ATTR_FREQ]) {
		struct tx_iter_data iter_data = {};

		/* throw away off-channel packets, but allow both the temporary
		 * ("hw" scan/remain-on-channel), regular channels and links,
		 * since the internal datapath also allows this
		 */
		rx_status.freq = nla_get_u32(info->attrs[HWSIM_ATTR_FREQ]);

		iter_data.channel = ieee80211_get_channel(data2->hw->wiphy,
							  rx_status.freq);
		if (!iter_data.channel)
			goto out;
		rx_status.band = iter_data.channel->band;

		mutex_lock(&data2->mutex);
		if (!hwsim_chans_compat(iter_data.channel, channel)) {
			ieee80211_iterate_active_interfaces_atomic(
				data2->hw, IEEE80211_IFACE_ITER_NORMAL,
				mac80211_hwsim_tx_iter, &iter_data);
			if (!iter_data.receive) {
				mutex_unlock(&data2->mutex);
				goto out;
			}
		}
		mutex_unlock(&data2->mutex);
	} else if (!channel) {
		goto out;
	} else {
		rx_status.freq = channel->center_freq;
		rx_status.band = channel->band;
	}

	rx_status.rate_idx = nla_get_u32(info->attrs[HWSIM_ATTR_RX_RATE]);
	if (rx_status.rate_idx >= data2->hw->wiphy->bands[rx_status.band]->n_bitrates)
		goto out;
	rx_status.signal = nla_get_u32(info->attrs[HWSIM_ATTR_SIGNAL]);

	hdr = (void *)skb->data;

	if (ieee80211_is_beacon(hdr->frame_control) ||
	    ieee80211_is_probe_resp(hdr->frame_control))
		rx_status.boottime_ns = ktime_get_boottime_ns();

	mac80211_hwsim_rx(data2, &rx_status, skb);

	return 0;
err:
	pr_debug("mac80211_hwsim: error occurred in %s\n", __func__);
out:
	dev_kfree_skb(skb);
	return -EINVAL;
}

static int hwsim_register_received_nl(struct sk_buff *skb_2,
				      struct genl_info *info)
{
	struct net *net = genl_info_net(info);
	struct mac80211_hwsim_data *data;
	int chans = 1;

	spin_lock_bh(&hwsim_radio_lock);
	list_for_each_entry(data, &hwsim_radios, list)
		chans = max(chans, data->channels);
	spin_unlock_bh(&hwsim_radio_lock);

	/* In the future we should revise the userspace API and allow it
	 * to set a flag that it does support multi-channel, then we can
	 * let this pass conditionally on the flag.
	 * For current userspace, prohibit it since it won't work right.
	 */
	if (chans > 1)
		return -EOPNOTSUPP;

	if (hwsim_net_get_wmediumd(net))
		return -EBUSY;

	hwsim_register_wmediumd(net, info->snd_portid);

	pr_debug("mac80211_hwsim: received a REGISTER, "
	       "switching to wmediumd mode with pid %d\n", info->snd_portid);

	return 0;
}

/* ensures ciphers only include ciphers listed in 'hwsim_ciphers' array */
static bool hwsim_known_ciphers(const u32 *ciphers, int n_ciphers)
{
	int i;

	for (i = 0; i < n_ciphers; i++) {
		int j;
		int found = 0;

		for (j = 0; j < ARRAY_SIZE(hwsim_ciphers); j++) {
			if (ciphers[i] == hwsim_ciphers[j]) {
				found = 1;
				break;
			}
		}

		if (!found)
			return false;
	}

	return true;
}

static int parse_ftm_capa(const struct nlattr *ftm_capa, struct cfg80211_pmsr_capabilities *out,
			  struct genl_info *info)
{
	struct nlattr *tb[NL80211_PMSR_FTM_CAPA_ATTR_MAX + 1];
	int ret;

	ret = nla_parse_nested(tb, NL80211_PMSR_FTM_CAPA_ATTR_MAX, ftm_capa, hwsim_ftm_capa_policy,
			       NULL);
	if (ret) {
		NL_SET_ERR_MSG_ATTR(info->extack, ftm_capa, "malformed FTM capability");
		return -EINVAL;
	}

	out->ftm.supported = 1;
	if (tb[NL80211_PMSR_FTM_CAPA_ATTR_PREAMBLES])
		out->ftm.preambles = nla_get_u32(tb[NL80211_PMSR_FTM_CAPA_ATTR_PREAMBLES]);
	if (tb[NL80211_PMSR_FTM_CAPA_ATTR_BANDWIDTHS])
		out->ftm.bandwidths = nla_get_u32(tb[NL80211_PMSR_FTM_CAPA_ATTR_BANDWIDTHS]);
	if (tb[NL80211_PMSR_FTM_CAPA_ATTR_MAX_BURSTS_EXPONENT])
		out->ftm.max_bursts_exponent =
			nla_get_u8(tb[NL80211_PMSR_FTM_CAPA_ATTR_MAX_BURSTS_EXPONENT]);
	if (tb[NL80211_PMSR_FTM_CAPA_ATTR_MAX_FTMS_PER_BURST])
		out->ftm.max_ftms_per_burst =
			nla_get_u8(tb[NL80211_PMSR_FTM_CAPA_ATTR_MAX_FTMS_PER_BURST]);
	out->ftm.asap = !!tb[NL80211_PMSR_FTM_CAPA_ATTR_ASAP];
	out->ftm.non_asap = !!tb[NL80211_PMSR_FTM_CAPA_ATTR_NON_ASAP];
	out->ftm.request_lci = !!tb[NL80211_PMSR_FTM_CAPA_ATTR_REQ_LCI];
	out->ftm.request_civicloc = !!tb[NL80211_PMSR_FTM_CAPA_ATTR_REQ_CIVICLOC];
	out->ftm.trigger_based = !!tb[NL80211_PMSR_FTM_CAPA_ATTR_TRIGGER_BASED];
	out->ftm.non_trigger_based = !!tb[NL80211_PMSR_FTM_CAPA_ATTR_NON_TRIGGER_BASED];

	return 0;
}

static int parse_pmsr_capa(const struct nlattr *pmsr_capa, struct cfg80211_pmsr_capabilities *out,
			   struct genl_info *info)
{
	struct nlattr *tb[NL80211_PMSR_ATTR_MAX + 1];
	struct nlattr *nla;
	int size;
	int ret;

	ret = nla_parse_nested(tb, NL80211_PMSR_ATTR_MAX, pmsr_capa, hwsim_pmsr_capa_policy, NULL);
	if (ret) {
		NL_SET_ERR_MSG_ATTR(info->extack, pmsr_capa, "malformed PMSR capability");
		return -EINVAL;
	}

	if (tb[NL80211_PMSR_ATTR_MAX_PEERS])
		out->max_peers = nla_get_u32(tb[NL80211_PMSR_ATTR_MAX_PEERS]);
	out->report_ap_tsf = !!tb[NL80211_PMSR_ATTR_REPORT_AP_TSF];
	out->randomize_mac_addr = !!tb[NL80211_PMSR_ATTR_RANDOMIZE_MAC_ADDR];

	if (!tb[NL80211_PMSR_ATTR_TYPE_CAPA]) {
		NL_SET_ERR_MSG_ATTR(info->extack, tb[NL80211_PMSR_ATTR_TYPE_CAPA],
				    "malformed PMSR type");
		return -EINVAL;
	}

	nla_for_each_nested(nla, tb[NL80211_PMSR_ATTR_TYPE_CAPA], size) {
		switch (nla_type(nla)) {
		case NL80211_PMSR_TYPE_FTM:
			parse_ftm_capa(nla, out, info);
			break;
		default:
			NL_SET_ERR_MSG_ATTR(info->extack, nla, "unsupported measurement type");
			return -EINVAL;
		}
	}

	return 0;
}

static int hwsim_new_radio_nl(struct sk_buff *msg, struct genl_info *info)
{
	struct hwsim_new_radio_params param = { 0 };
	const char *hwname = NULL;
	int ret;

	param.reg_strict = info->attrs[HWSIM_ATTR_REG_STRICT_REG];
	param.p2p_device = info->attrs[HWSIM_ATTR_SUPPORT_P2P_DEVICE];
	param.channels = channels;
	param.destroy_on_close =
		info->attrs[HWSIM_ATTR_DESTROY_RADIO_ON_CLOSE];

	if (info->attrs[HWSIM_ATTR_CHANNELS])
		param.channels = nla_get_u32(info->attrs[HWSIM_ATTR_CHANNELS]);

	if (param.channels < 1) {
		GENL_SET_ERR_MSG(info, "must have at least one channel");
		return -EINVAL;
	}

	if (info->attrs[HWSIM_ATTR_NO_VIF])
		param.no_vif = true;

	if (info->attrs[HWSIM_ATTR_USE_CHANCTX])
		param.use_chanctx = true;
	else
		param.use_chanctx = (param.channels > 1);

	if (info->attrs[HWSIM_ATTR_MULTI_RADIO])
		param.multi_radio = true;

	if (info->attrs[HWSIM_ATTR_REG_HINT_ALPHA2])
		param.reg_alpha2 =
			nla_data(info->attrs[HWSIM_ATTR_REG_HINT_ALPHA2]);

	if (info->attrs[HWSIM_ATTR_REG_CUSTOM_REG]) {
		u32 idx = nla_get_u32(info->attrs[HWSIM_ATTR_REG_CUSTOM_REG]);

		if (idx >= ARRAY_SIZE(hwsim_world_regdom_custom))
			return -EINVAL;

		idx = array_index_nospec(idx,
					 ARRAY_SIZE(hwsim_world_regdom_custom));
		param.regd = hwsim_world_regdom_custom[idx];
	}

	if (info->attrs[HWSIM_ATTR_PERM_ADDR]) {
		if (!is_valid_ether_addr(
				nla_data(info->attrs[HWSIM_ATTR_PERM_ADDR]))) {
			GENL_SET_ERR_MSG(info,"MAC is no valid source addr");
			NL_SET_BAD_ATTR(info->extack,
					info->attrs[HWSIM_ATTR_PERM_ADDR]);
			return -EINVAL;
		}

		param.perm_addr = nla_data(info->attrs[HWSIM_ATTR_PERM_ADDR]);
	}

	if (info->attrs[HWSIM_ATTR_IFTYPE_SUPPORT]) {
		param.iftypes =
			nla_get_u32(info->attrs[HWSIM_ATTR_IFTYPE_SUPPORT]);

		if (param.iftypes & ~HWSIM_IFTYPE_SUPPORT_MASK) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    info->attrs[HWSIM_ATTR_IFTYPE_SUPPORT],
					    "cannot support more iftypes than kernel");
			return -EINVAL;
		}
	} else {
		param.iftypes = HWSIM_IFTYPE_SUPPORT_MASK;
	}

	/* ensure both flag and iftype support is honored */
	if (param.p2p_device ||
	    param.iftypes & BIT(NL80211_IFTYPE_P2P_DEVICE)) {
		param.iftypes |= BIT(NL80211_IFTYPE_P2P_DEVICE);
		param.p2p_device = true;
	}

	if (info->attrs[HWSIM_ATTR_CIPHER_SUPPORT]) {
		u32 len = nla_len(info->attrs[HWSIM_ATTR_CIPHER_SUPPORT]);

		param.ciphers =
			nla_data(info->attrs[HWSIM_ATTR_CIPHER_SUPPORT]);

		if (len % sizeof(u32)) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    info->attrs[HWSIM_ATTR_CIPHER_SUPPORT],
					    "bad cipher list length");
			return -EINVAL;
		}

		param.n_ciphers = len / sizeof(u32);

		if (param.n_ciphers > ARRAY_SIZE(hwsim_ciphers)) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    info->attrs[HWSIM_ATTR_CIPHER_SUPPORT],
					    "too many ciphers specified");
			return -EINVAL;
		}

		if (!hwsim_known_ciphers(param.ciphers, param.n_ciphers)) {
			NL_SET_ERR_MSG_ATTR(info->extack,
					    info->attrs[HWSIM_ATTR_CIPHER_SUPPORT],
					    "unsupported ciphers specified");
			return -EINVAL;
		}
	}

	param.mlo = info->attrs[HWSIM_ATTR_MLO_SUPPORT];

	if (param.mlo || param.multi_radio)
		param.use_chanctx = true;

	if (info->attrs[HWSIM_ATTR_RADIO_NAME]) {
		hwname = kstrndup((char *)nla_data(info->attrs[HWSIM_ATTR_RADIO_NAME]),
				  nla_len(info->attrs[HWSIM_ATTR_RADIO_NAME]),
				  GFP_KERNEL);
		if (!hwname)
			return -ENOMEM;
		param.hwname = hwname;
	}

	if (info->attrs[HWSIM_ATTR_PMSR_SUPPORT]) {
		struct cfg80211_pmsr_capabilities *pmsr_capa;

		pmsr_capa = kmalloc(sizeof(*pmsr_capa), GFP_KERNEL);
		if (!pmsr_capa) {
			ret = -ENOMEM;
			goto out_free;
		}
		param.pmsr_capa = pmsr_capa;

		ret = parse_pmsr_capa(info->attrs[HWSIM_ATTR_PMSR_SUPPORT], pmsr_capa, info);
		if (ret)
			goto out_free;
	}

	ret = mac80211_hwsim_new_radio(info, &param);

out_free:
	kfree(hwname);
	kfree(param.pmsr_capa);
	return ret;
}

static int hwsim_del_radio_nl(struct sk_buff *msg, struct genl_info *info)
{
	struct mac80211_hwsim_data *data;
	s64 idx = -1;
	const char *hwname = NULL;

	if (info->attrs[HWSIM_ATTR_RADIO_ID]) {
		idx = nla_get_u32(info->attrs[HWSIM_ATTR_RADIO_ID]);
	} else if (info->attrs[HWSIM_ATTR_RADIO_NAME]) {
		hwname = kstrndup((char *)nla_data(info->attrs[HWSIM_ATTR_RADIO_NAME]),
				  nla_len(info->attrs[HWSIM_ATTR_RADIO_NAME]),
				  GFP_KERNEL);
		if (!hwname)
			return -ENOMEM;
	} else
		return -EINVAL;

	spin_lock_bh(&hwsim_radio_lock);
	list_for_each_entry(data, &hwsim_radios, list) {
		if (idx >= 0) {
			if (data->idx != idx)
				continue;
		} else {
			if (!hwname ||
			    strcmp(hwname, wiphy_name(data->hw->wiphy)))
				continue;
		}

		if (!net_eq(wiphy_net(data->hw->wiphy), genl_info_net(info)))
			continue;

		list_del(&data->list);
		rhashtable_remove_fast(&hwsim_radios_rht, &data->rht,
				       hwsim_rht_params);
		hwsim_radios_generation++;
		spin_unlock_bh(&hwsim_radio_lock);
		mac80211_hwsim_del_radio(data, wiphy_name(data->hw->wiphy),
					 info);
		kfree(hwname);
		return 0;
	}
	spin_unlock_bh(&hwsim_radio_lock);

	kfree(hwname);
	return -ENODEV;
}

static int hwsim_get_radio_nl(struct sk_buff *msg, struct genl_info *info)
{
	struct mac80211_hwsim_data *data;
	struct sk_buff *skb;
	int idx, res = -ENODEV;

	if (!info->attrs[HWSIM_ATTR_RADIO_ID])
		return -EINVAL;
	idx = nla_get_u32(info->attrs[HWSIM_ATTR_RADIO_ID]);

	spin_lock_bh(&hwsim_radio_lock);
	list_for_each_entry(data, &hwsim_radios, list) {
		if (data->idx != idx)
			continue;

		if (!net_eq(wiphy_net(data->hw->wiphy), genl_info_net(info)))
			continue;

		skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
		if (!skb) {
			res = -ENOMEM;
			goto out_err;
		}

		res = mac80211_hwsim_get_radio(skb, data, info->snd_portid,
					       info->snd_seq, NULL, 0);
		if (res < 0) {
			nlmsg_free(skb);
			goto out_err;
		}

		res = genlmsg_reply(skb, info);
		break;
	}

out_err:
	spin_unlock_bh(&hwsim_radio_lock);

	return res;
}

static int hwsim_dump_radio_nl(struct sk_buff *skb,
			       struct netlink_callback *cb)
{
	int last_idx = cb->args[0] - 1;
	struct mac80211_hwsim_data *data = NULL;
	int res = 0;
	void *hdr;

	spin_lock_bh(&hwsim_radio_lock);
	cb->seq = hwsim_radios_generation;

	if (last_idx >= hwsim_radio_idx-1)
		goto done;

	list_for_each_entry(data, &hwsim_radios, list) {
		if (data->idx <= last_idx)
			continue;

		if (!net_eq(wiphy_net(data->hw->wiphy), sock_net(skb->sk)))
			continue;

		res = mac80211_hwsim_get_radio(skb, data,
					       NETLINK_CB(cb->skb).portid,
					       cb->nlh->nlmsg_seq, cb,
					       NLM_F_MULTI);
		if (res < 0)
			break;

		last_idx = data->idx;
	}

	cb->args[0] = last_idx + 1;

	/* list changed, but no new element sent, set interrupted flag */
	if (skb->len == 0 && cb->prev_seq && cb->seq != cb->prev_seq) {
		hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid,
				  cb->nlh->nlmsg_seq, &hwsim_genl_family,
				  NLM_F_MULTI, HWSIM_CMD_GET_RADIO);
		if (hdr) {
			genl_dump_check_consistent(cb, hdr);
			genlmsg_end(skb, hdr);
		} else {
			res = -EMSGSIZE;
		}
	}

done:
	spin_unlock_bh(&hwsim_radio_lock);
	return res ?: skb->len;
}

/* Generic Netlink operations array */
static const struct genl_small_ops hwsim_ops[] = {
	{
		.cmd = HWSIM_CMD_REGISTER,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = hwsim_register_received_nl,
		.flags = GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd = HWSIM_CMD_FRAME,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = hwsim_cloned_frame_received_nl,
	},
	{
		.cmd = HWSIM_CMD_TX_INFO_FRAME,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = hwsim_tx_info_frame_received_nl,
	},
	{
		.cmd = HWSIM_CMD_NEW_RADIO,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = hwsim_new_radio_nl,
		.flags = GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd = HWSIM_CMD_DEL_RADIO,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = hwsim_del_radio_nl,
		.flags = GENL_UNS_ADMIN_PERM,
	},
	{
		.cmd = HWSIM_CMD_GET_RADIO,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = hwsim_get_radio_nl,
		.dumpit = hwsim_dump_radio_nl,
	},
	{
		.cmd = HWSIM_CMD_REPORT_PMSR,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = hwsim_pmsr_report_nl,
	},
};

static struct genl_family hwsim_genl_family __ro_after_init = {
	.name = "MAC80211_HWSIM",
	.version = 1,
	.maxattr = HWSIM_ATTR_MAX,
	.policy = hwsim_genl_policy,
	.netnsok = true,
	.module = THIS_MODULE,
	.small_ops = hwsim_ops,
	.n_small_ops = ARRAY_SIZE(hwsim_ops),
	.resv_start_op = HWSIM_CMD_REPORT_PMSR + 1, // match with __HWSIM_CMD_MAX
	.mcgrps = hwsim_mcgrps,
	.n_mcgrps = ARRAY_SIZE(hwsim_mcgrps),
};

static void remove_user_radios(u32 portid)
{
	struct mac80211_hwsim_data *entry, *tmp;
	LIST_HEAD(list);

	spin_lock_bh(&hwsim_radio_lock);
	list_for_each_entry_safe(entry, tmp, &hwsim_radios, list) {
		if (entry->destroy_on_close && entry->portid == portid) {
			list_move(&entry->list, &list);
			rhashtable_remove_fast(&hwsim_radios_rht, &entry->rht,
					       hwsim_rht_params);
			hwsim_radios_generation++;
		}
	}
	spin_unlock_bh(&hwsim_radio_lock);

	list_for_each_entry_safe(entry, tmp, &list, list) {
		list_del(&entry->list);
		mac80211_hwsim_del_radio(entry, wiphy_name(entry->hw->wiphy),
					 NULL);
	}
}

static int mac80211_hwsim_netlink_notify(struct notifier_block *nb,
					 unsigned long state,
					 void *_notify)
{
	struct netlink_notify *notify = _notify;

	if (state != NETLINK_URELEASE)
		return NOTIFY_DONE;

	remove_user_radios(notify->portid);

	if (notify->portid == hwsim_net_get_wmediumd(notify->net)) {
		printk(KERN_INFO "mac80211_hwsim: wmediumd released netlink"
		       " socket, switching to perfect channel medium\n");
		hwsim_register_wmediumd(notify->net, 0);
	}
	return NOTIFY_DONE;

}

static struct notifier_block hwsim_netlink_notifier = {
	.notifier_call = mac80211_hwsim_netlink_notify,
};

static int __init hwsim_init_netlink(void)
{
	int rc;

	printk(KERN_INFO "mac80211_hwsim: initializing netlink\n");

	rc = genl_register_family(&hwsim_genl_family);
	if (rc)
		goto failure;

	rc = netlink_register_notifier(&hwsim_netlink_notifier);
	if (rc) {
		genl_unregister_family(&hwsim_genl_family);
		goto failure;
	}

	return 0;

failure:
	pr_debug("mac80211_hwsim: error occurred in %s\n", __func__);
	return -EINVAL;
}

static __net_init int hwsim_init_net(struct net *net)
{
	return hwsim_net_set_netgroup(net);
}

static void __net_exit hwsim_exit_net(struct net *net)
{
	struct mac80211_hwsim_data *data, *tmp;
	LIST_HEAD(list);

	spin_lock_bh(&hwsim_radio_lock);
	list_for_each_entry_safe(data, tmp, &hwsim_radios, list) {
		if (!net_eq(wiphy_net(data->hw->wiphy), net))
			continue;

		/* Radios created in init_net are returned to init_net. */
		if (data->netgroup == hwsim_net_get_netgroup(&init_net))
			continue;

		list_move(&data->list, &list);
		rhashtable_remove_fast(&hwsim_radios_rht, &data->rht,
				       hwsim_rht_params);
		hwsim_radios_generation++;
	}
	spin_unlock_bh(&hwsim_radio_lock);

	list_for_each_entry_safe(data, tmp, &list, list) {
		list_del(&data->list);
		mac80211_hwsim_del_radio(data,
					 wiphy_name(data->hw->wiphy),
					 NULL);
	}

	ida_free(&hwsim_netgroup_ida, hwsim_net_get_netgroup(net));
}

static struct pernet_operations hwsim_net_ops = {
	.init = hwsim_init_net,
	.exit = hwsim_exit_net,
	.id   = &hwsim_net_id,
	.size = sizeof(struct hwsim_net),
};

static void hwsim_exit_netlink(void)
{
	/* unregister the notifier */
	netlink_unregister_notifier(&hwsim_netlink_notifier);
	/* unregister the family */
	genl_unregister_family(&hwsim_genl_family);
}

#if IS_REACHABLE(CONFIG_VIRTIO)
static void hwsim_virtio_tx_done(struct virtqueue *vq)
{
	unsigned int len;
	struct sk_buff *skb;
	unsigned long flags;

	spin_lock_irqsave(&hwsim_virtio_lock, flags);
	while ((skb = virtqueue_get_buf(vq, &len)))
		dev_kfree_skb_irq(skb);
	spin_unlock_irqrestore(&hwsim_virtio_lock, flags);
}

static int hwsim_virtio_handle_cmd(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	struct genlmsghdr *gnlh;
	struct nlattr *tb[HWSIM_ATTR_MAX + 1];
	struct genl_info info = {};
	int err;

	nlh = nlmsg_hdr(skb);
	gnlh = nlmsg_data(nlh);

	if (skb->len < nlh->nlmsg_len)
		return -EINVAL;

	err = genlmsg_parse(nlh, &hwsim_genl_family, tb, HWSIM_ATTR_MAX,
			    hwsim_genl_policy, NULL);
	if (err) {
		pr_err_ratelimited("hwsim: genlmsg_parse returned %d\n", err);
		return err;
	}

	info.attrs = tb;

	switch (gnlh->cmd) {
	case HWSIM_CMD_FRAME:
		hwsim_cloned_frame_received_nl(skb, &info);
		break;
	case HWSIM_CMD_TX_INFO_FRAME:
		hwsim_tx_info_frame_received_nl(skb, &info);
		break;
	case HWSIM_CMD_REPORT_PMSR:
		hwsim_pmsr_report_nl(skb, &info);
		break;
	default:
		pr_err_ratelimited("hwsim: invalid cmd: %d\n", gnlh->cmd);
		return -EPROTO;
	}
	return 0;
}

static void hwsim_virtio_rx_work(struct work_struct *work)
{
	struct virtqueue *vq;
	unsigned int len;
	struct sk_buff *skb;
	struct scatterlist sg[1];
	int err;
	unsigned long flags;

	spin_lock_irqsave(&hwsim_virtio_lock, flags);
	if (!hwsim_virtio_enabled)
		goto out_unlock;

	skb = virtqueue_get_buf(hwsim_vqs[HWSIM_VQ_RX], &len);
	if (!skb)
		goto out_unlock;
	spin_unlock_irqrestore(&hwsim_virtio_lock, flags);

	skb->data = skb->head;
	skb_reset_tail_pointer(skb);
	skb_put(skb, len);
	hwsim_virtio_handle_cmd(skb);

	spin_lock_irqsave(&hwsim_virtio_lock, flags);
	if (!hwsim_virtio_enabled) {
		dev_kfree_skb_irq(skb);
		goto out_unlock;
	}
	vq = hwsim_vqs[HWSIM_VQ_RX];
	sg_init_one(sg, skb->head, skb_end_offset(skb));
	err = virtqueue_add_inbuf(vq, sg, 1, skb, GFP_ATOMIC);
	if (WARN(err, "virtqueue_add_inbuf returned %d\n", err))
		dev_kfree_skb_irq(skb);
	else
		virtqueue_kick(vq);
	schedule_work(&hwsim_virtio_rx);

out_unlock:
	spin_unlock_irqrestore(&hwsim_virtio_lock, flags);
}

static void hwsim_virtio_rx_done(struct virtqueue *vq)
{
	schedule_work(&hwsim_virtio_rx);
}

static int init_vqs(struct virtio_device *vdev)
{
	struct virtqueue_info vqs_info[HWSIM_NUM_VQS] = {
		[HWSIM_VQ_TX] = { "tx", hwsim_virtio_tx_done },
		[HWSIM_VQ_RX] = { "rx", hwsim_virtio_rx_done },
	};

	return virtio_find_vqs(vdev, HWSIM_NUM_VQS,
			       hwsim_vqs, vqs_info, NULL);
}

static int fill_vq(struct virtqueue *vq)
{
	int i, err;
	struct sk_buff *skb;
	struct scatterlist sg[1];

	for (i = 0; i < virtqueue_get_vring_size(vq); i++) {
		skb = genlmsg_new(GENLMSG_DEFAULT_SIZE, GFP_KERNEL);
		if (!skb)
			return -ENOMEM;

		sg_init_one(sg, skb->head, skb_end_offset(skb));
		err = virtqueue_add_inbuf(vq, sg, 1, skb, GFP_KERNEL);
		if (err) {
			nlmsg_free(skb);
			return err;
		}
	}
	virtqueue_kick(vq);
	return 0;
}

static void remove_vqs(struct virtio_device *vdev)
{
	int i;

	virtio_reset_device(vdev);

	for (i = 0; i < ARRAY_SIZE(hwsim_vqs); i++) {
		struct virtqueue *vq = hwsim_vqs[i];
		struct sk_buff *skb;

		while ((skb = virtqueue_detach_unused_buf(vq)))
			nlmsg_free(skb);
	}

	vdev->config->del_vqs(vdev);
}

static int hwsim_virtio_probe(struct virtio_device *vdev)
{
	int err;
	unsigned long flags;

	spin_lock_irqsave(&hwsim_virtio_lock, flags);
	if (hwsim_virtio_enabled) {
		spin_unlock_irqrestore(&hwsim_virtio_lock, flags);
		return -EEXIST;
	}
	spin_unlock_irqrestore(&hwsim_virtio_lock, flags);

	err = init_vqs(vdev);
	if (err)
		return err;

	virtio_device_ready(vdev);

	err = fill_vq(hwsim_vqs[HWSIM_VQ_RX]);
	if (err)
		goto out_remove;

	spin_lock_irqsave(&hwsim_virtio_lock, flags);
	hwsim_virtio_enabled = true;
	spin_unlock_irqrestore(&hwsim_virtio_lock, flags);

	schedule_work(&hwsim_virtio_rx);
	return 0;

out_remove:
	remove_vqs(vdev);
	return err;
}

static void hwsim_virtio_remove(struct virtio_device *vdev)
{
	hwsim_virtio_enabled = false;

	cancel_work_sync(&hwsim_virtio_rx);

	remove_vqs(vdev);
}

/* MAC80211_HWSIM virtio device id table */
static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_MAC80211_HWSIM, VIRTIO_DEV_ANY_ID },
	{ 0 }
};
MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_hwsim = {
	.driver.name = KBUILD_MODNAME,
	.id_table = id_table,
	.probe = hwsim_virtio_probe,
	.remove = hwsim_virtio_remove,
};

static int hwsim_register_virtio_driver(void)
{
	return register_virtio_driver(&virtio_hwsim);
}

static void hwsim_unregister_virtio_driver(void)
{
	unregister_virtio_driver(&virtio_hwsim);
}
#else
static inline int hwsim_register_virtio_driver(void)
{
	return 0;
}

static inline void hwsim_unregister_virtio_driver(void)
{
}
#endif

static int __init init_mac80211_hwsim(void)
{
	int i, err;

	if (radios < 0 || radios > 100)
		return -EINVAL;

	if (channels < 1)
		return -EINVAL;

	err = rhashtable_init(&hwsim_radios_rht, &hwsim_rht_params);
	if (err)
		return err;

	err = register_pernet_device(&hwsim_net_ops);
	if (err)
		goto out_free_rht;

	err = platform_driver_register(&mac80211_hwsim_driver);
	if (err)
		goto out_unregister_pernet;

	err = hwsim_init_netlink();
	if (err)
		goto out_unregister_driver;

	err = hwsim_register_virtio_driver();
	if (err)
		goto out_exit_netlink;

	hwsim_class = class_create("mac80211_hwsim");
	if (IS_ERR(hwsim_class)) {
		err = PTR_ERR(hwsim_class);
		goto out_exit_virtio;
	}

	hwsim_init_s1g_channels(hwsim_channels_s1g);

	for (i = 0; i < radios; i++) {
		struct hwsim_new_radio_params param = { 0 };

		param.channels = channels;

		switch (regtest) {
		case HWSIM_REGTEST_DIFF_COUNTRY:
			if (i < ARRAY_SIZE(hwsim_alpha2s))
				param.reg_alpha2 = hwsim_alpha2s[i];
			break;
		case HWSIM_REGTEST_DRIVER_REG_FOLLOW:
			if (!i)
				param.reg_alpha2 = hwsim_alpha2s[0];
			break;
		case HWSIM_REGTEST_STRICT_ALL:
			param.reg_strict = true;
			fallthrough;
		case HWSIM_REGTEST_DRIVER_REG_ALL:
			param.reg_alpha2 = hwsim_alpha2s[0];
			break;
		case HWSIM_REGTEST_WORLD_ROAM:
			if (i == 0)
				param.regd = &hwsim_world_regdom_custom_01;
			break;
		case HWSIM_REGTEST_CUSTOM_WORLD:
			param.regd = &hwsim_world_regdom_custom_03;
			break;
		case HWSIM_REGTEST_CUSTOM_WORLD_2:
			if (i == 0)
				param.regd = &hwsim_world_regdom_custom_03;
			else if (i == 1)
				param.regd = &hwsim_world_regdom_custom_02;
			break;
		case HWSIM_REGTEST_STRICT_FOLLOW:
			if (i == 0) {
				param.reg_strict = true;
				param.reg_alpha2 = hwsim_alpha2s[0];
			}
			break;
		case HWSIM_REGTEST_STRICT_AND_DRIVER_REG:
			if (i == 0) {
				param.reg_strict = true;
				param.reg_alpha2 = hwsim_alpha2s[0];
			} else if (i == 1) {
				param.reg_alpha2 = hwsim_alpha2s[1];
			}
			break;
		case HWSIM_REGTEST_ALL:
			switch (i) {
			case 0:
				param.regd = &hwsim_world_regdom_custom_01;
				break;
			case 1:
				param.regd = &hwsim_world_regdom_custom_02;
				break;
			case 2:
				param.reg_alpha2 = hwsim_alpha2s[0];
				break;
			case 3:
				param.reg_alpha2 = hwsim_alpha2s[1];
				break;
			case 4:
				param.reg_strict = true;
				param.reg_alpha2 = hwsim_alpha2s[2];
				break;
			}
			break;
		default:
			break;
		}

		param.p2p_device = support_p2p_device;
		param.mlo = mlo;
		param.multi_radio = multi_radio;
		param.use_chanctx = channels > 1 || mlo || multi_radio;
		param.iftypes = HWSIM_IFTYPE_SUPPORT_MASK;
		if (param.p2p_device)
			param.iftypes |= BIT(NL80211_IFTYPE_P2P_DEVICE);

		err = mac80211_hwsim_new_radio(NULL, &param);
		if (err < 0)
			goto out_free_radios;
	}

	hwsim_mon = alloc_netdev(0, "hwsim%d", NET_NAME_UNKNOWN,
				 hwsim_mon_setup);
	if (hwsim_mon == NULL) {
		err = -ENOMEM;
		goto out_free_radios;
	}

	rtnl_lock();
	err = dev_alloc_name(hwsim_mon, hwsim_mon->name);
	if (err < 0) {
		rtnl_unlock();
		goto out_free_mon;
	}

	err = register_netdevice(hwsim_mon);
	if (err < 0) {
		rtnl_unlock();
		goto out_free_mon;
	}
	rtnl_unlock();

	return 0;

out_free_mon:
	free_netdev(hwsim_mon);
out_free_radios:
	mac80211_hwsim_free();
out_exit_virtio:
	hwsim_unregister_virtio_driver();
out_exit_netlink:
	hwsim_exit_netlink();
out_unregister_driver:
	platform_driver_unregister(&mac80211_hwsim_driver);
out_unregister_pernet:
	unregister_pernet_device(&hwsim_net_ops);
out_free_rht:
	rhashtable_destroy(&hwsim_radios_rht);
	return err;
}
module_init(init_mac80211_hwsim);

static void __exit exit_mac80211_hwsim(void)
{
	pr_debug("mac80211_hwsim: unregister radios\n");

	hwsim_unregister_virtio_driver();
	hwsim_exit_netlink();

	mac80211_hwsim_free();

	rhashtable_destroy(&hwsim_radios_rht);
	unregister_netdev(hwsim_mon);
	platform_driver_unregister(&mac80211_hwsim_driver);
	unregister_pernet_device(&hwsim_net_ops);
}
module_exit(exit_mac80211_hwsim);
