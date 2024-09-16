// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */
#include <linux/ip.h>
#include <linux/udp.h>

#include "cam.h"
#include "chan.h"
#include "coex.h"
#include "core.h"
#include "efuse.h"
#include "fw.h"
#include "mac.h"
#include "phy.h"
#include "ps.h"
#include "reg.h"
#include "sar.h"
#include "ser.h"
#include "txrx.h"
#include "util.h"
#include "wow.h"

static bool rtw89_disable_ps_mode;
module_param_named(disable_ps_mode, rtw89_disable_ps_mode, bool, 0644);
MODULE_PARM_DESC(disable_ps_mode, "Set Y to disable low power mode");

#define RTW89_DEF_CHAN(_freq, _hw_val, _flags, _band)	\
	{ .center_freq = _freq, .hw_value = _hw_val, .flags = _flags, .band = _band, }
#define RTW89_DEF_CHAN_2G(_freq, _hw_val)	\
	RTW89_DEF_CHAN(_freq, _hw_val, 0, NL80211_BAND_2GHZ)
#define RTW89_DEF_CHAN_5G(_freq, _hw_val)	\
	RTW89_DEF_CHAN(_freq, _hw_val, 0, NL80211_BAND_5GHZ)
#define RTW89_DEF_CHAN_5G_NO_HT40MINUS(_freq, _hw_val)	\
	RTW89_DEF_CHAN(_freq, _hw_val, IEEE80211_CHAN_NO_HT40MINUS, NL80211_BAND_5GHZ)
#define RTW89_DEF_CHAN_6G(_freq, _hw_val)	\
	RTW89_DEF_CHAN(_freq, _hw_val, 0, NL80211_BAND_6GHZ)

static struct ieee80211_channel rtw89_channels_2ghz[] = {
	RTW89_DEF_CHAN_2G(2412, 1),
	RTW89_DEF_CHAN_2G(2417, 2),
	RTW89_DEF_CHAN_2G(2422, 3),
	RTW89_DEF_CHAN_2G(2427, 4),
	RTW89_DEF_CHAN_2G(2432, 5),
	RTW89_DEF_CHAN_2G(2437, 6),
	RTW89_DEF_CHAN_2G(2442, 7),
	RTW89_DEF_CHAN_2G(2447, 8),
	RTW89_DEF_CHAN_2G(2452, 9),
	RTW89_DEF_CHAN_2G(2457, 10),
	RTW89_DEF_CHAN_2G(2462, 11),
	RTW89_DEF_CHAN_2G(2467, 12),
	RTW89_DEF_CHAN_2G(2472, 13),
	RTW89_DEF_CHAN_2G(2484, 14),
};

static struct ieee80211_channel rtw89_channels_5ghz[] = {
	RTW89_DEF_CHAN_5G(5180, 36),
	RTW89_DEF_CHAN_5G(5200, 40),
	RTW89_DEF_CHAN_5G(5220, 44),
	RTW89_DEF_CHAN_5G(5240, 48),
	RTW89_DEF_CHAN_5G(5260, 52),
	RTW89_DEF_CHAN_5G(5280, 56),
	RTW89_DEF_CHAN_5G(5300, 60),
	RTW89_DEF_CHAN_5G(5320, 64),
	RTW89_DEF_CHAN_5G(5500, 100),
	RTW89_DEF_CHAN_5G(5520, 104),
	RTW89_DEF_CHAN_5G(5540, 108),
	RTW89_DEF_CHAN_5G(5560, 112),
	RTW89_DEF_CHAN_5G(5580, 116),
	RTW89_DEF_CHAN_5G(5600, 120),
	RTW89_DEF_CHAN_5G(5620, 124),
	RTW89_DEF_CHAN_5G(5640, 128),
	RTW89_DEF_CHAN_5G(5660, 132),
	RTW89_DEF_CHAN_5G(5680, 136),
	RTW89_DEF_CHAN_5G(5700, 140),
	RTW89_DEF_CHAN_5G(5720, 144),
	RTW89_DEF_CHAN_5G(5745, 149),
	RTW89_DEF_CHAN_5G(5765, 153),
	RTW89_DEF_CHAN_5G(5785, 157),
	RTW89_DEF_CHAN_5G(5805, 161),
	RTW89_DEF_CHAN_5G_NO_HT40MINUS(5825, 165),
	RTW89_DEF_CHAN_5G(5845, 169),
	RTW89_DEF_CHAN_5G(5865, 173),
	RTW89_DEF_CHAN_5G(5885, 177),
};

static_assert(RTW89_5GHZ_UNII4_START_INDEX + RTW89_5GHZ_UNII4_CHANNEL_NUM ==
	      ARRAY_SIZE(rtw89_channels_5ghz));

static struct ieee80211_channel rtw89_channels_6ghz[] = {
	RTW89_DEF_CHAN_6G(5955, 1),
	RTW89_DEF_CHAN_6G(5975, 5),
	RTW89_DEF_CHAN_6G(5995, 9),
	RTW89_DEF_CHAN_6G(6015, 13),
	RTW89_DEF_CHAN_6G(6035, 17),
	RTW89_DEF_CHAN_6G(6055, 21),
	RTW89_DEF_CHAN_6G(6075, 25),
	RTW89_DEF_CHAN_6G(6095, 29),
	RTW89_DEF_CHAN_6G(6115, 33),
	RTW89_DEF_CHAN_6G(6135, 37),
	RTW89_DEF_CHAN_6G(6155, 41),
	RTW89_DEF_CHAN_6G(6175, 45),
	RTW89_DEF_CHAN_6G(6195, 49),
	RTW89_DEF_CHAN_6G(6215, 53),
	RTW89_DEF_CHAN_6G(6235, 57),
	RTW89_DEF_CHAN_6G(6255, 61),
	RTW89_DEF_CHAN_6G(6275, 65),
	RTW89_DEF_CHAN_6G(6295, 69),
	RTW89_DEF_CHAN_6G(6315, 73),
	RTW89_DEF_CHAN_6G(6335, 77),
	RTW89_DEF_CHAN_6G(6355, 81),
	RTW89_DEF_CHAN_6G(6375, 85),
	RTW89_DEF_CHAN_6G(6395, 89),
	RTW89_DEF_CHAN_6G(6415, 93),
	RTW89_DEF_CHAN_6G(6435, 97),
	RTW89_DEF_CHAN_6G(6455, 101),
	RTW89_DEF_CHAN_6G(6475, 105),
	RTW89_DEF_CHAN_6G(6495, 109),
	RTW89_DEF_CHAN_6G(6515, 113),
	RTW89_DEF_CHAN_6G(6535, 117),
	RTW89_DEF_CHAN_6G(6555, 121),
	RTW89_DEF_CHAN_6G(6575, 125),
	RTW89_DEF_CHAN_6G(6595, 129),
	RTW89_DEF_CHAN_6G(6615, 133),
	RTW89_DEF_CHAN_6G(6635, 137),
	RTW89_DEF_CHAN_6G(6655, 141),
	RTW89_DEF_CHAN_6G(6675, 145),
	RTW89_DEF_CHAN_6G(6695, 149),
	RTW89_DEF_CHAN_6G(6715, 153),
	RTW89_DEF_CHAN_6G(6735, 157),
	RTW89_DEF_CHAN_6G(6755, 161),
	RTW89_DEF_CHAN_6G(6775, 165),
	RTW89_DEF_CHAN_6G(6795, 169),
	RTW89_DEF_CHAN_6G(6815, 173),
	RTW89_DEF_CHAN_6G(6835, 177),
	RTW89_DEF_CHAN_6G(6855, 181),
	RTW89_DEF_CHAN_6G(6875, 185),
	RTW89_DEF_CHAN_6G(6895, 189),
	RTW89_DEF_CHAN_6G(6915, 193),
	RTW89_DEF_CHAN_6G(6935, 197),
	RTW89_DEF_CHAN_6G(6955, 201),
	RTW89_DEF_CHAN_6G(6975, 205),
	RTW89_DEF_CHAN_6G(6995, 209),
	RTW89_DEF_CHAN_6G(7015, 213),
	RTW89_DEF_CHAN_6G(7035, 217),
	RTW89_DEF_CHAN_6G(7055, 221),
	RTW89_DEF_CHAN_6G(7075, 225),
	RTW89_DEF_CHAN_6G(7095, 229),
	RTW89_DEF_CHAN_6G(7115, 233),
};

static struct ieee80211_rate rtw89_bitrates[] = {
	{ .bitrate = 10,  .hw_value = 0x00, },
	{ .bitrate = 20,  .hw_value = 0x01, },
	{ .bitrate = 55,  .hw_value = 0x02, },
	{ .bitrate = 110, .hw_value = 0x03, },
	{ .bitrate = 60,  .hw_value = 0x04, },
	{ .bitrate = 90,  .hw_value = 0x05, },
	{ .bitrate = 120, .hw_value = 0x06, },
	{ .bitrate = 180, .hw_value = 0x07, },
	{ .bitrate = 240, .hw_value = 0x08, },
	{ .bitrate = 360, .hw_value = 0x09, },
	{ .bitrate = 480, .hw_value = 0x0a, },
	{ .bitrate = 540, .hw_value = 0x0b, },
};

static const struct ieee80211_iface_limit rtw89_iface_limits[] = {
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_STATION),
	},
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_P2P_CLIENT) |
			 BIT(NL80211_IFTYPE_P2P_GO) |
			 BIT(NL80211_IFTYPE_AP),
	},
};

static const struct ieee80211_iface_limit rtw89_iface_limits_mcc[] = {
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_STATION),
	},
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_P2P_CLIENT) |
			 BIT(NL80211_IFTYPE_P2P_GO),
	},
};

static const struct ieee80211_iface_combination rtw89_iface_combs[] = {
	{
		.limits = rtw89_iface_limits,
		.n_limits = ARRAY_SIZE(rtw89_iface_limits),
		.max_interfaces = 2,
		.num_different_channels = 1,
	},
	{
		.limits = rtw89_iface_limits_mcc,
		.n_limits = ARRAY_SIZE(rtw89_iface_limits_mcc),
		.max_interfaces = 2,
		.num_different_channels = 2,
	},
};

bool rtw89_ra_report_to_bitrate(struct rtw89_dev *rtwdev, u8 rpt_rate, u16 *bitrate)
{
	struct ieee80211_rate rate;

	if (unlikely(rpt_rate >= ARRAY_SIZE(rtw89_bitrates))) {
		rtw89_debug(rtwdev, RTW89_DBG_UNEXP, "invalid rpt rate %d\n", rpt_rate);
		return false;
	}

	rate = rtw89_bitrates[rpt_rate];
	*bitrate = rate.bitrate;

	return true;
}

static const struct ieee80211_supported_band rtw89_sband_2ghz = {
	.band		= NL80211_BAND_2GHZ,
	.channels	= rtw89_channels_2ghz,
	.n_channels	= ARRAY_SIZE(rtw89_channels_2ghz),
	.bitrates	= rtw89_bitrates,
	.n_bitrates	= ARRAY_SIZE(rtw89_bitrates),
	.ht_cap		= {0},
	.vht_cap	= {0},
};

static const struct ieee80211_supported_band rtw89_sband_5ghz = {
	.band		= NL80211_BAND_5GHZ,
	.channels	= rtw89_channels_5ghz,
	.n_channels	= ARRAY_SIZE(rtw89_channels_5ghz),

	/* 5G has no CCK rates, 1M/2M/5.5M/11M */
	.bitrates	= rtw89_bitrates + 4,
	.n_bitrates	= ARRAY_SIZE(rtw89_bitrates) - 4,
	.ht_cap		= {0},
	.vht_cap	= {0},
};

static const struct ieee80211_supported_band rtw89_sband_6ghz = {
	.band		= NL80211_BAND_6GHZ,
	.channels	= rtw89_channels_6ghz,
	.n_channels	= ARRAY_SIZE(rtw89_channels_6ghz),

	/* 6G has no CCK rates, 1M/2M/5.5M/11M */
	.bitrates	= rtw89_bitrates + 4,
	.n_bitrates	= ARRAY_SIZE(rtw89_bitrates) - 4,
};

static void rtw89_traffic_stats_accu(struct rtw89_dev *rtwdev,
				     struct rtw89_traffic_stats *stats,
				     struct sk_buff *skb, bool tx)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;

	if (tx && ieee80211_is_assoc_req(hdr->frame_control))
		rtw89_wow_parse_akm(rtwdev, skb);

	if (!ieee80211_is_data(hdr->frame_control))
		return;

	if (is_broadcast_ether_addr(hdr->addr1) ||
	    is_multicast_ether_addr(hdr->addr1))
		return;

	if (tx) {
		stats->tx_cnt++;
		stats->tx_unicast += skb->len;
	} else {
		stats->rx_cnt++;
		stats->rx_unicast += skb->len;
	}
}

void rtw89_get_default_chandef(struct cfg80211_chan_def *chandef)
{
	cfg80211_chandef_create(chandef, &rtw89_channels_2ghz[0],
				NL80211_CHAN_NO_HT);
}

void rtw89_get_channel_params(const struct cfg80211_chan_def *chandef,
			      struct rtw89_chan *chan)
{
	struct ieee80211_channel *channel = chandef->chan;
	enum nl80211_chan_width width = chandef->width;
	u32 primary_freq, center_freq;
	u8 center_chan;
	u8 bandwidth = RTW89_CHANNEL_WIDTH_20;
	u32 offset;
	u8 band;

	center_chan = channel->hw_value;
	primary_freq = channel->center_freq;
	center_freq = chandef->center_freq1;

	switch (width) {
	case NL80211_CHAN_WIDTH_20_NOHT:
	case NL80211_CHAN_WIDTH_20:
		bandwidth = RTW89_CHANNEL_WIDTH_20;
		break;
	case NL80211_CHAN_WIDTH_40:
		bandwidth = RTW89_CHANNEL_WIDTH_40;
		if (primary_freq > center_freq) {
			center_chan -= 2;
		} else {
			center_chan += 2;
		}
		break;
	case NL80211_CHAN_WIDTH_80:
	case NL80211_CHAN_WIDTH_160:
		bandwidth = nl_to_rtw89_bandwidth(width);
		if (primary_freq > center_freq) {
			offset = (primary_freq - center_freq - 10) / 20;
			center_chan -= 2 + offset * 4;
		} else {
			offset = (center_freq - primary_freq - 10) / 20;
			center_chan += 2 + offset * 4;
		}
		break;
	default:
		center_chan = 0;
		break;
	}

	switch (channel->band) {
	default:
	case NL80211_BAND_2GHZ:
		band = RTW89_BAND_2G;
		break;
	case NL80211_BAND_5GHZ:
		band = RTW89_BAND_5G;
		break;
	case NL80211_BAND_6GHZ:
		band = RTW89_BAND_6G;
		break;
	}

	rtw89_chan_create(chan, center_chan, channel->hw_value, band, bandwidth);
}

void rtw89_core_set_chip_txpwr(struct rtw89_dev *rtwdev)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_chan *chan;
	enum rtw89_chanctx_idx chanctx_idx;
	enum rtw89_chanctx_idx roc_idx;
	enum rtw89_phy_idx phy_idx;
	enum rtw89_entity_mode mode;
	bool entity_active;

	entity_active = rtw89_get_entity_state(rtwdev);
	if (!entity_active)
		return;

	mode = rtw89_get_entity_mode(rtwdev);
	switch (mode) {
	case RTW89_ENTITY_MODE_SCC:
	case RTW89_ENTITY_MODE_MCC:
		chanctx_idx = RTW89_CHANCTX_0;
		break;
	case RTW89_ENTITY_MODE_MCC_PREPARE:
		chanctx_idx = RTW89_CHANCTX_1;
		break;
	default:
		WARN(1, "Invalid ent mode: %d\n", mode);
		return;
	}

	roc_idx = atomic_read(&hal->roc_chanctx_idx);
	if (roc_idx != RTW89_CHANCTX_IDLE)
		chanctx_idx = roc_idx;

	phy_idx = RTW89_PHY_0;
	chan = rtw89_chan_get(rtwdev, chanctx_idx);
	chip->ops->set_txpwr(rtwdev, chan, phy_idx);
}

int rtw89_set_channel(struct rtw89_dev *rtwdev)
{
	struct rtw89_hal *hal = &rtwdev->hal;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_chan_rcd *chan_rcd;
	const struct rtw89_chan *chan;
	enum rtw89_chanctx_idx chanctx_idx;
	enum rtw89_chanctx_idx roc_idx;
	enum rtw89_mac_idx mac_idx;
	enum rtw89_phy_idx phy_idx;
	struct rtw89_channel_help_params bak;
	enum rtw89_entity_mode mode;
	bool entity_active;

	entity_active = rtw89_get_entity_state(rtwdev);

	mode = rtw89_entity_recalc(rtwdev);
	switch (mode) {
	case RTW89_ENTITY_MODE_SCC:
	case RTW89_ENTITY_MODE_MCC:
		chanctx_idx = RTW89_CHANCTX_0;
		break;
	case RTW89_ENTITY_MODE_MCC_PREPARE:
		chanctx_idx = RTW89_CHANCTX_1;
		break;
	default:
		WARN(1, "Invalid ent mode: %d\n", mode);
		return -EINVAL;
	}

	roc_idx = atomic_read(&hal->roc_chanctx_idx);
	if (roc_idx != RTW89_CHANCTX_IDLE)
		chanctx_idx = roc_idx;

	mac_idx = RTW89_MAC_0;
	phy_idx = RTW89_PHY_0;

	chan = rtw89_chan_get(rtwdev, chanctx_idx);
	chan_rcd = rtw89_chan_rcd_get(rtwdev, chanctx_idx);

	rtw89_chip_set_channel_prepare(rtwdev, &bak, chan, mac_idx, phy_idx);

	chip->ops->set_channel(rtwdev, chan, mac_idx, phy_idx);

	chip->ops->set_txpwr(rtwdev, chan, phy_idx);

	rtw89_chip_set_channel_done(rtwdev, &bak, chan, mac_idx, phy_idx);

	if (!entity_active || chan_rcd->band_changed) {
		rtw89_btc_ntfy_switch_band(rtwdev, phy_idx, chan->band_type);
		rtw89_chip_rfk_band_changed(rtwdev, phy_idx, chan);
	}

	rtw89_set_entity_state(rtwdev, true);
	return 0;
}

static enum rtw89_core_tx_type
rtw89_core_get_tx_type(struct rtw89_dev *rtwdev,
		       struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	__le16 fc = hdr->frame_control;

	if (ieee80211_is_mgmt(fc) || ieee80211_is_nullfunc(fc))
		return RTW89_CORE_TX_TYPE_MGMT;

	return RTW89_CORE_TX_TYPE_DATA;
}

static void
rtw89_core_tx_update_ampdu_info(struct rtw89_dev *rtwdev,
				struct rtw89_core_tx_request *tx_req,
				enum btc_pkt_type pkt_type)
{
	struct rtw89_sta_link *rtwsta_link = tx_req->rtwsta_link;
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;
	struct ieee80211_link_sta *link_sta;
	struct sk_buff *skb = tx_req->skb;
	struct rtw89_sta *rtwsta;
	u8 ampdu_num;
	u8 tid;

	if (pkt_type == PACKET_EAPOL) {
		desc_info->bk = true;
		return;
	}

	if (!(IEEE80211_SKB_CB(skb)->flags & IEEE80211_TX_CTL_AMPDU))
		return;

	if (!rtwsta_link) {
		rtw89_warn(rtwdev, "cannot set ampdu info without sta\n");
		return;
	}

	tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
	rtwsta = rtwsta_link->rtwsta;

	rcu_read_lock();

	link_sta = rtw89_sta_rcu_dereference_link(rtwsta_link, false);
	ampdu_num = (u8)((rtwsta->ampdu_params[tid].agg_num ?
			  rtwsta->ampdu_params[tid].agg_num :
			  4 << link_sta->ht_cap.ampdu_factor) - 1);

	desc_info->agg_en = true;
	desc_info->ampdu_density = link_sta->ht_cap.ampdu_density;
	desc_info->ampdu_num = ampdu_num;

	rcu_read_unlock();
}

static void
rtw89_core_tx_update_sec_key(struct rtw89_dev *rtwdev,
			     struct rtw89_core_tx_request *tx_req)
{
	struct rtw89_cam_info *cam_info = &rtwdev->cam_info;
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_sec_cam_entry *sec_cam;
	struct ieee80211_tx_info *info;
	struct ieee80211_key_conf *key;
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;
	struct sk_buff *skb = tx_req->skb;
	u8 sec_type = RTW89_SEC_KEY_TYPE_NONE;
	u8 sec_cam_idx;
	u64 pn64;

	info = IEEE80211_SKB_CB(skb);
	key = info->control.hw_key;
	sec_cam_idx = key->hw_key_idx;
	sec_cam = cam_info->sec_entries[sec_cam_idx];
	if (!sec_cam) {
		rtw89_warn(rtwdev, "sec cam entry is empty\n");
		return;
	}

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		sec_type = RTW89_SEC_KEY_TYPE_WEP40;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		sec_type = RTW89_SEC_KEY_TYPE_WEP104;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		sec_type = RTW89_SEC_KEY_TYPE_TKIP;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		sec_type = RTW89_SEC_KEY_TYPE_CCMP128;
		break;
	case WLAN_CIPHER_SUITE_CCMP_256:
		sec_type = RTW89_SEC_KEY_TYPE_CCMP256;
		break;
	case WLAN_CIPHER_SUITE_GCMP:
		sec_type = RTW89_SEC_KEY_TYPE_GCMP128;
		break;
	case WLAN_CIPHER_SUITE_GCMP_256:
		sec_type = RTW89_SEC_KEY_TYPE_GCMP256;
		break;
	default:
		rtw89_warn(rtwdev, "key cipher not supported %d\n", key->cipher);
		return;
	}

	desc_info->sec_en = true;
	desc_info->sec_keyid = key->keyidx;
	desc_info->sec_type = sec_type;
	desc_info->sec_cam_idx = sec_cam->sec_cam_idx;

	if (!chip->hw_sec_hdr)
		return;

	pn64 = atomic64_inc_return(&key->tx_pn);
	desc_info->sec_seq[0] = pn64;
	desc_info->sec_seq[1] = pn64 >> 8;
	desc_info->sec_seq[2] = pn64 >> 16;
	desc_info->sec_seq[3] = pn64 >> 24;
	desc_info->sec_seq[4] = pn64 >> 32;
	desc_info->sec_seq[5] = pn64 >> 40;
	desc_info->wp_offset = 1; /* in unit of 8 bytes for security header */
}

static u16 rtw89_core_get_mgmt_rate(struct rtw89_dev *rtwdev,
				    struct rtw89_core_tx_request *tx_req,
				    const struct rtw89_chan *chan)
{
	struct sk_buff *skb = tx_req->skb;
	struct rtw89_vif_link *rtwvif_link = tx_req->rtwvif_link;
	struct rtw89_sta_link *rtwsta_link = tx_req->rtwsta_link;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = tx_info->control.vif;
	struct ieee80211_bss_conf *bss_conf;
	u16 lowest_rate;
	u16 rate;

	if (tx_info->flags & IEEE80211_TX_CTL_NO_CCK_RATE ||
	    (vif && vif->p2p))
		lowest_rate = RTW89_HW_RATE_OFDM6;
	else if (chan->band_type == RTW89_BAND_2G)
		lowest_rate = RTW89_HW_RATE_CCK1;
	else
		lowest_rate = RTW89_HW_RATE_OFDM6;

	if (!rtwvif_link)
		return lowest_rate;

	rcu_read_lock();

	bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, false);
	if (!bss_conf->basic_rates || !rtwsta_link) {
		rate = lowest_rate;
		goto out;
	}

	rate = __ffs(bss_conf->basic_rates) + lowest_rate;

out:
	rcu_read_unlock();

	return rate;
}

static u8 rtw89_core_tx_get_mac_id(struct rtw89_dev *rtwdev,
				   struct rtw89_core_tx_request *tx_req)
{
	struct rtw89_vif_link *rtwvif_link = tx_req->rtwvif_link;
	struct rtw89_sta_link *rtwsta_link = tx_req->rtwsta_link;

	if (!rtwsta_link)
		return rtwvif_link->mac_id;

	return rtwsta_link->mac_id;
}

static void rtw89_core_tx_update_llc_hdr(struct rtw89_dev *rtwdev,
					 struct rtw89_tx_desc_info *desc_info,
					 struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	__le16 fc = hdr->frame_control;

	desc_info->hdr_llc_len = ieee80211_hdrlen(fc);
	desc_info->hdr_llc_len >>= 1; /* in unit of 2 bytes */
}

static void
rtw89_core_tx_update_mgmt_info(struct rtw89_dev *rtwdev,
			       struct rtw89_core_tx_request *tx_req)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_vif_link *rtwvif_link = tx_req->rtwvif_link;
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev,
						       rtwvif_link->chanctx_idx);
	struct sk_buff *skb = tx_req->skb;
	u8 qsel, ch_dma;

	qsel = desc_info->hiq ? RTW89_TX_QSEL_B0_HI : RTW89_TX_QSEL_B0_MGMT;
	ch_dma = rtw89_core_get_ch_dma(rtwdev, qsel);

	desc_info->qsel = qsel;
	desc_info->ch_dma = ch_dma;
	desc_info->port = desc_info->hiq ? rtwvif_link->port : 0;
	desc_info->mac_id = rtw89_core_tx_get_mac_id(rtwdev, tx_req);
	desc_info->hw_ssn_sel = RTW89_MGMT_HW_SSN_SEL;
	desc_info->hw_seq_mode = RTW89_MGMT_HW_SEQ_MODE;

	/* fixed data rate for mgmt frames */
	desc_info->en_wd_info = true;
	desc_info->use_rate = true;
	desc_info->dis_data_fb = true;
	desc_info->data_rate = rtw89_core_get_mgmt_rate(rtwdev, tx_req, chan);

	if (chip->hw_mgmt_tx_encrypt && IEEE80211_SKB_CB(skb)->control.hw_key) {
		rtw89_core_tx_update_sec_key(rtwdev, tx_req);
		rtw89_core_tx_update_llc_hdr(rtwdev, desc_info, skb);
	}

	rtw89_debug(rtwdev, RTW89_DBG_TXRX,
		    "tx mgmt frame with rate 0x%x on channel %d (band %d, bw %d)\n",
		    desc_info->data_rate, chan->channel, chan->band_type,
		    chan->band_width);
}

static void
rtw89_core_tx_update_h2c_info(struct rtw89_dev *rtwdev,
			      struct rtw89_core_tx_request *tx_req)
{
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;

	desc_info->is_bmc = false;
	desc_info->wd_page = false;
	desc_info->ch_dma = RTW89_DMA_H2C;
}

static void rtw89_core_get_no_ul_ofdma_htc(struct rtw89_dev *rtwdev, __le32 *htc,
					   const struct rtw89_chan *chan)
{
	static const u8 rtw89_bandwidth_to_om[] = {
		[RTW89_CHANNEL_WIDTH_20] = HTC_OM_CHANNEL_WIDTH_20,
		[RTW89_CHANNEL_WIDTH_40] = HTC_OM_CHANNEL_WIDTH_40,
		[RTW89_CHANNEL_WIDTH_80] = HTC_OM_CHANNEL_WIDTH_80,
		[RTW89_CHANNEL_WIDTH_160] = HTC_OM_CHANNEL_WIDTH_160_OR_80_80,
		[RTW89_CHANNEL_WIDTH_80_80] = HTC_OM_CHANNEL_WIDTH_160_OR_80_80,
	};
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_hal *hal = &rtwdev->hal;
	u8 om_bandwidth;

	if (!chip->dis_2g_40m_ul_ofdma ||
	    chan->band_type != RTW89_BAND_2G ||
	    chan->band_width != RTW89_CHANNEL_WIDTH_40)
		return;

	om_bandwidth = chan->band_width < ARRAY_SIZE(rtw89_bandwidth_to_om) ?
		       rtw89_bandwidth_to_om[chan->band_width] : 0;
	*htc = le32_encode_bits(RTW89_HTC_VARIANT_HE, RTW89_HTC_MASK_VARIANT) |
	       le32_encode_bits(RTW89_HTC_VARIANT_HE_CID_OM, RTW89_HTC_MASK_CTL_ID) |
	       le32_encode_bits(hal->rx_nss - 1, RTW89_HTC_MASK_HTC_OM_RX_NSS) |
	       le32_encode_bits(om_bandwidth, RTW89_HTC_MASK_HTC_OM_CH_WIDTH) |
	       le32_encode_bits(1, RTW89_HTC_MASK_HTC_OM_UL_MU_DIS) |
	       le32_encode_bits(hal->tx_nss - 1, RTW89_HTC_MASK_HTC_OM_TX_NSTS) |
	       le32_encode_bits(0, RTW89_HTC_MASK_HTC_OM_ER_SU_DIS) |
	       le32_encode_bits(0, RTW89_HTC_MASK_HTC_OM_DL_MU_MIMO_RR) |
	       le32_encode_bits(0, RTW89_HTC_MASK_HTC_OM_UL_MU_DATA_DIS);
}

static bool
__rtw89_core_tx_check_he_qos_htc(struct rtw89_dev *rtwdev,
				 struct rtw89_core_tx_request *tx_req,
				 enum btc_pkt_type pkt_type)
{
	struct rtw89_sta_link *rtwsta_link = tx_req->rtwsta_link;
	struct sk_buff *skb = tx_req->skb;
	struct ieee80211_hdr *hdr = (void *)skb->data;
	struct ieee80211_link_sta *link_sta;
	__le16 fc = hdr->frame_control;

	/* AP IOT issue with EAPoL, ARP and DHCP */
	if (pkt_type < PACKET_MAX)
		return false;

	if (!rtwsta_link)
		return false;

	rcu_read_lock();

	link_sta = rtw89_sta_rcu_dereference_link(rtwsta_link, false);
	if (!link_sta->he_cap.has_he) {
		rcu_read_unlock();
		return false;
	}

	rcu_read_unlock();

	if (!ieee80211_is_data_qos(fc))
		return false;

	if (skb_headroom(skb) < IEEE80211_HT_CTL_LEN)
		return false;

	if (rtwsta_link && rtwsta_link->ra_report.might_fallback_legacy)
		return false;

	return true;
}

static void
__rtw89_core_tx_adjust_he_qos_htc(struct rtw89_dev *rtwdev,
				  struct rtw89_core_tx_request *tx_req)
{
	struct rtw89_sta_link *rtwsta_link = tx_req->rtwsta_link;
	struct sk_buff *skb = tx_req->skb;
	struct ieee80211_hdr *hdr = (void *)skb->data;
	__le16 fc = hdr->frame_control;
	void *data;
	__le32 *htc;
	u8 *qc;
	int hdr_len;

	hdr_len = ieee80211_has_a4(fc) ? 32 : 26;
	data = skb_push(skb, IEEE80211_HT_CTL_LEN);
	memmove(data, data + IEEE80211_HT_CTL_LEN, hdr_len);

	hdr = data;
	htc = data + hdr_len;
	hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_ORDER);
	*htc = rtwsta_link->htc_template ? rtwsta_link->htc_template :
	       le32_encode_bits(RTW89_HTC_VARIANT_HE, RTW89_HTC_MASK_VARIANT) |
	       le32_encode_bits(RTW89_HTC_VARIANT_HE_CID_CAS, RTW89_HTC_MASK_CTL_ID);

	qc = data + hdr_len - IEEE80211_QOS_CTL_LEN;
	qc[0] |= IEEE80211_QOS_CTL_EOSP;
}

static void
rtw89_core_tx_update_he_qos_htc(struct rtw89_dev *rtwdev,
				struct rtw89_core_tx_request *tx_req,
				enum btc_pkt_type pkt_type)
{
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;
	struct rtw89_vif_link *rtwvif_link = tx_req->rtwvif_link;

	if (!__rtw89_core_tx_check_he_qos_htc(rtwdev, tx_req, pkt_type))
		goto desc_bk;

	__rtw89_core_tx_adjust_he_qos_htc(rtwdev, tx_req);

	desc_info->pkt_size += IEEE80211_HT_CTL_LEN;
	desc_info->a_ctrl_bsr = true;

desc_bk:
	if (!rtwvif_link || rtwvif_link->last_a_ctrl == desc_info->a_ctrl_bsr)
		return;

	rtwvif_link->last_a_ctrl = desc_info->a_ctrl_bsr;
	desc_info->bk = true;
}

static u16 rtw89_core_get_data_rate(struct rtw89_dev *rtwdev,
				    struct rtw89_core_tx_request *tx_req)
{
	struct rtw89_vif_link *rtwvif_link = tx_req->rtwvif_link;
	struct rtw89_sta_link *rtwsta_link = tx_req->rtwsta_link;
	struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	struct rtw89_phy_rate_pattern *rate_pattern = &rtwvif_link->rate_pattern;
	enum rtw89_chanctx_idx idx = rtwvif_link->chanctx_idx;
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev, idx);
	struct ieee80211_link_sta *link_sta;
	u16 lowest_rate;
	u16 rate;

	if (rate_pattern->enable)
		return rate_pattern->rate;

	if (vif->p2p)
		lowest_rate = RTW89_HW_RATE_OFDM6;
	else if (chan->band_type == RTW89_BAND_2G)
		lowest_rate = RTW89_HW_RATE_CCK1;
	else
		lowest_rate = RTW89_HW_RATE_OFDM6;

	if (!rtwsta_link)
		return lowest_rate;

	rcu_read_lock();

	link_sta = rtw89_sta_rcu_dereference_link(rtwsta_link, false);
	if (!link_sta->supp_rates[chan->band_type]) {
		rate = lowest_rate;
		goto out;
	}

	rate = __ffs(link_sta->supp_rates[chan->band_type]) + lowest_rate;

out:
	rcu_read_unlock();

	return rate;
}

static void
rtw89_core_tx_update_data_info(struct rtw89_dev *rtwdev,
			       struct rtw89_core_tx_request *tx_req)
{
	struct rtw89_vif_link *rtwvif_link = tx_req->rtwvif_link;
	struct rtw89_sta_link *rtwsta_link = tx_req->rtwsta_link;
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;
	struct sk_buff *skb = tx_req->skb;
	u8 tid, tid_indicate;
	u8 qsel, ch_dma;

	tid = skb->priority & IEEE80211_QOS_CTL_TAG1D_MASK;
	tid_indicate = rtw89_core_get_tid_indicate(rtwdev, tid);
	qsel = desc_info->hiq ? RTW89_TX_QSEL_B0_HI : rtw89_core_get_qsel(rtwdev, tid);
	ch_dma = rtw89_core_get_ch_dma(rtwdev, qsel);

	desc_info->ch_dma = ch_dma;
	desc_info->tid_indicate = tid_indicate;
	desc_info->qsel = qsel;
	desc_info->mac_id = rtw89_core_tx_get_mac_id(rtwdev, tx_req);
	desc_info->port = desc_info->hiq ? rtwvif_link->port : 0;
	desc_info->er_cap = rtwsta_link ? rtwsta_link->er_cap : false;
	desc_info->stbc = rtwsta_link ? rtwsta_link->ra.stbc_cap : false;
	desc_info->ldpc = rtwsta_link ? rtwsta_link->ra.ldpc_cap : false;

	/* enable wd_info for AMPDU */
	desc_info->en_wd_info = true;

	if (IEEE80211_SKB_CB(skb)->control.hw_key)
		rtw89_core_tx_update_sec_key(rtwdev, tx_req);

	desc_info->data_retry_lowest_rate = rtw89_core_get_data_rate(rtwdev, tx_req);
}

static enum btc_pkt_type
rtw89_core_tx_btc_spec_pkt_notify(struct rtw89_dev *rtwdev,
				  struct rtw89_core_tx_request *tx_req)
{
	struct sk_buff *skb = tx_req->skb;
	struct udphdr *udphdr;

	if (IEEE80211_SKB_CB(skb)->control.flags & IEEE80211_TX_CTRL_PORT_CTRL_PROTO) {
		ieee80211_queue_work(rtwdev->hw, &rtwdev->btc.eapol_notify_work);
		return PACKET_EAPOL;
	}

	if (skb->protocol == htons(ETH_P_ARP)) {
		ieee80211_queue_work(rtwdev->hw, &rtwdev->btc.arp_notify_work);
		return PACKET_ARP;
	}

	if (skb->protocol == htons(ETH_P_IP) &&
	    ip_hdr(skb)->protocol == IPPROTO_UDP) {
		udphdr = udp_hdr(skb);
		if (((udphdr->source == htons(67) && udphdr->dest == htons(68)) ||
		     (udphdr->source == htons(68) && udphdr->dest == htons(67))) &&
		    skb->len > 282) {
			ieee80211_queue_work(rtwdev->hw, &rtwdev->btc.dhcp_notify_work);
			return PACKET_DHCP;
		}
	}

	if (skb->protocol == htons(ETH_P_IP) &&
	    ip_hdr(skb)->protocol == IPPROTO_ICMP) {
		ieee80211_queue_work(rtwdev->hw, &rtwdev->btc.icmp_notify_work);
		return PACKET_ICMP;
	}

	return PACKET_MAX;
}

static void
rtw89_core_tx_wake(struct rtw89_dev *rtwdev,
		   struct rtw89_core_tx_request *tx_req)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	if (!RTW89_CHK_FW_FEATURE(TX_WAKE, &rtwdev->fw))
		return;

	if (!test_bit(RTW89_FLAG_LOW_POWER_MODE, rtwdev->flags))
		return;

	if (chip->chip_id != RTL8852C &&
	    tx_req->tx_type != RTW89_CORE_TX_TYPE_MGMT)
		return;

	rtw89_mac_notify_wake(rtwdev);
}

static void
rtw89_core_tx_update_desc_info(struct rtw89_dev *rtwdev,
			       struct rtw89_core_tx_request *tx_req)
{
	struct rtw89_tx_desc_info *desc_info = &tx_req->desc_info;
	struct sk_buff *skb = tx_req->skb;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (void *)skb->data;
	enum rtw89_core_tx_type tx_type;
	enum btc_pkt_type pkt_type;
	bool is_bmc;
	u16 seq;

	seq = (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4;
	if (tx_req->tx_type != RTW89_CORE_TX_TYPE_FWCMD) {
		tx_type = rtw89_core_get_tx_type(rtwdev, skb);
		tx_req->tx_type = tx_type;
	}
	is_bmc = (is_broadcast_ether_addr(hdr->addr1) ||
		  is_multicast_ether_addr(hdr->addr1));

	desc_info->seq = seq;
	desc_info->pkt_size = skb->len;
	desc_info->is_bmc = is_bmc;
	desc_info->wd_page = true;
	desc_info->hiq = info->flags & IEEE80211_TX_CTL_SEND_AFTER_DTIM;

	switch (tx_req->tx_type) {
	case RTW89_CORE_TX_TYPE_MGMT:
		rtw89_core_tx_update_mgmt_info(rtwdev, tx_req);
		break;
	case RTW89_CORE_TX_TYPE_DATA:
		rtw89_core_tx_update_data_info(rtwdev, tx_req);
		pkt_type = rtw89_core_tx_btc_spec_pkt_notify(rtwdev, tx_req);
		rtw89_core_tx_update_he_qos_htc(rtwdev, tx_req, pkt_type);
		rtw89_core_tx_update_ampdu_info(rtwdev, tx_req, pkt_type);
		rtw89_core_tx_update_llc_hdr(rtwdev, desc_info, skb);
		break;
	case RTW89_CORE_TX_TYPE_FWCMD:
		rtw89_core_tx_update_h2c_info(rtwdev, tx_req);
		break;
	}
}

void rtw89_core_tx_kick_off(struct rtw89_dev *rtwdev, u8 qsel)
{
	u8 ch_dma;

	ch_dma = rtw89_core_get_ch_dma(rtwdev, qsel);

	rtw89_hci_tx_kick_off(rtwdev, ch_dma);
}

int rtw89_core_tx_kick_off_and_wait(struct rtw89_dev *rtwdev, struct sk_buff *skb,
				    int qsel, unsigned int timeout)
{
	struct rtw89_tx_skb_data *skb_data = RTW89_TX_SKB_CB(skb);
	struct rtw89_tx_wait_info *wait;
	unsigned long time_left;
	int ret = 0;

	wait = kzalloc(sizeof(*wait), GFP_KERNEL);
	if (!wait) {
		rtw89_core_tx_kick_off(rtwdev, qsel);
		return 0;
	}

	init_completion(&wait->completion);
	rcu_assign_pointer(skb_data->wait, wait);

	rtw89_core_tx_kick_off(rtwdev, qsel);
	time_left = wait_for_completion_timeout(&wait->completion,
						msecs_to_jiffies(timeout));
	if (time_left == 0)
		ret = -ETIMEDOUT;
	else if (!wait->tx_done)
		ret = -EAGAIN;

	rcu_assign_pointer(skb_data->wait, NULL);
	kfree_rcu(wait, rcu_head);

	return ret;
}

int rtw89_h2c_tx(struct rtw89_dev *rtwdev,
		 struct sk_buff *skb, bool fwdl)
{
	struct rtw89_core_tx_request tx_req = {0};
	u32 cnt;
	int ret;

	if (!test_bit(RTW89_FLAG_POWERON, rtwdev->flags)) {
		rtw89_debug(rtwdev, RTW89_DBG_FW,
			    "ignore h2c due to power is off with firmware state=%d\n",
			    test_bit(RTW89_FLAG_FW_RDY, rtwdev->flags));
		dev_kfree_skb(skb);
		return 0;
	}

	tx_req.skb = skb;
	tx_req.tx_type = RTW89_CORE_TX_TYPE_FWCMD;
	if (fwdl)
		tx_req.desc_info.fw_dl = true;

	rtw89_core_tx_update_desc_info(rtwdev, &tx_req);

	if (!fwdl)
		rtw89_hex_dump(rtwdev, RTW89_DBG_FW, "H2C: ", skb->data, skb->len);

	cnt = rtw89_hci_check_and_reclaim_tx_resource(rtwdev, RTW89_TXCH_CH12);
	if (cnt == 0) {
		rtw89_err(rtwdev, "no tx fwcmd resource\n");
		return -ENOSPC;
	}

	ret = rtw89_hci_tx_write(rtwdev, &tx_req);
	if (ret) {
		rtw89_err(rtwdev, "failed to transmit skb to HCI\n");
		return ret;
	}
	rtw89_hci_tx_kick_off(rtwdev, RTW89_TXCH_CH12);

	return 0;
}

int rtw89_core_tx_write(struct rtw89_dev *rtwdev, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, struct sk_buff *skb, int *qsel)
{
	struct rtw89_sta *rtwsta = sta_to_rtwsta_safe(sta);
	struct rtw89_vif *rtwvif = vif_to_rtwvif(vif);
	struct rtw89_core_tx_request tx_req = {0};
	struct rtw89_sta_link *rtwsta_link = NULL;
	struct rtw89_vif_link *rtwvif_link;
	int ret;

	/* By default, driver writes tx via the link on HW-0. And then,
	 * according to links' status, HW can change tx to another link.
	 */

	if (rtwsta) {
		rtwsta_link = rtw89_sta_get_link_inst(rtwsta, 0);
		if (unlikely(!rtwsta_link)) {
			rtw89_err(rtwdev, "tx: find no sta link on HW-0\n");
			return -ENOLINK;
		}
	}

	rtwvif_link = rtw89_vif_get_link_inst(rtwvif, 0);
	if (unlikely(!rtwvif_link)) {
		rtw89_err(rtwdev, "tx: find no vif link on HW-0\n");
		return -ENOLINK;
	}

	tx_req.skb = skb;
	tx_req.rtwvif_link = rtwvif_link;
	tx_req.rtwsta_link = rtwsta_link;

	rtw89_traffic_stats_accu(rtwdev, &rtwdev->stats, skb, true);
	rtw89_traffic_stats_accu(rtwdev, &rtwvif->stats, skb, true);
	rtw89_core_tx_update_desc_info(rtwdev, &tx_req);
	rtw89_core_tx_wake(rtwdev, &tx_req);

	ret = rtw89_hci_tx_write(rtwdev, &tx_req);
	if (ret) {
		rtw89_err(rtwdev, "failed to transmit skb to HCI\n");
		return ret;
	}

	if (qsel)
		*qsel = tx_req.desc_info.qsel;

	return 0;
}

static __le32 rtw89_build_txwd_body0(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_BODY0_WP_OFFSET, desc_info->wp_offset) |
		    FIELD_PREP(RTW89_TXWD_BODY0_WD_INFO_EN, desc_info->en_wd_info) |
		    FIELD_PREP(RTW89_TXWD_BODY0_CHANNEL_DMA, desc_info->ch_dma) |
		    FIELD_PREP(RTW89_TXWD_BODY0_HDR_LLC_LEN, desc_info->hdr_llc_len) |
		    FIELD_PREP(RTW89_TXWD_BODY0_WD_PAGE, desc_info->wd_page) |
		    FIELD_PREP(RTW89_TXWD_BODY0_FW_DL, desc_info->fw_dl) |
		    FIELD_PREP(RTW89_TXWD_BODY0_HW_SSN_SEL, desc_info->hw_ssn_sel) |
		    FIELD_PREP(RTW89_TXWD_BODY0_HW_SSN_MODE, desc_info->hw_seq_mode);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_body0_v1(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_BODY0_WP_OFFSET_V1, desc_info->wp_offset) |
		    FIELD_PREP(RTW89_TXWD_BODY0_WD_INFO_EN, desc_info->en_wd_info) |
		    FIELD_PREP(RTW89_TXWD_BODY0_CHANNEL_DMA, desc_info->ch_dma) |
		    FIELD_PREP(RTW89_TXWD_BODY0_HDR_LLC_LEN, desc_info->hdr_llc_len) |
		    FIELD_PREP(RTW89_TXWD_BODY0_WD_PAGE, desc_info->wd_page) |
		    FIELD_PREP(RTW89_TXWD_BODY0_FW_DL, desc_info->fw_dl);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_body1_v1(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_BODY1_ADDR_INFO_NUM, desc_info->addr_info_nr) |
		    FIELD_PREP(RTW89_TXWD_BODY1_SEC_KEYID, desc_info->sec_keyid) |
		    FIELD_PREP(RTW89_TXWD_BODY1_SEC_TYPE, desc_info->sec_type);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_body2(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_BODY2_TID_INDICATE, desc_info->tid_indicate) |
		    FIELD_PREP(RTW89_TXWD_BODY2_QSEL, desc_info->qsel) |
		    FIELD_PREP(RTW89_TXWD_BODY2_TXPKT_SIZE, desc_info->pkt_size) |
		    FIELD_PREP(RTW89_TXWD_BODY2_MACID, desc_info->mac_id);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_body3(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_BODY3_SW_SEQ, desc_info->seq) |
		    FIELD_PREP(RTW89_TXWD_BODY3_AGG_EN, desc_info->agg_en) |
		    FIELD_PREP(RTW89_TXWD_BODY3_BK, desc_info->bk);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_body4(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_BODY4_SEC_IV_L0, desc_info->sec_seq[0]) |
		    FIELD_PREP(RTW89_TXWD_BODY4_SEC_IV_L1, desc_info->sec_seq[1]);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_body5(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_BODY5_SEC_IV_H2, desc_info->sec_seq[2]) |
		    FIELD_PREP(RTW89_TXWD_BODY5_SEC_IV_H3, desc_info->sec_seq[3]) |
		    FIELD_PREP(RTW89_TXWD_BODY5_SEC_IV_H4, desc_info->sec_seq[4]) |
		    FIELD_PREP(RTW89_TXWD_BODY5_SEC_IV_H5, desc_info->sec_seq[5]);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_body7_v1(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_BODY7_USE_RATE_V1, desc_info->use_rate) |
		    FIELD_PREP(RTW89_TXWD_BODY7_DATA_RATE, desc_info->data_rate);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_info0(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_INFO0_USE_RATE, desc_info->use_rate) |
		    FIELD_PREP(RTW89_TXWD_INFO0_DATA_RATE, desc_info->data_rate) |
		    FIELD_PREP(RTW89_TXWD_INFO0_DATA_STBC, desc_info->stbc) |
		    FIELD_PREP(RTW89_TXWD_INFO0_DATA_LDPC, desc_info->ldpc) |
		    FIELD_PREP(RTW89_TXWD_INFO0_DISDATAFB, desc_info->dis_data_fb) |
		    FIELD_PREP(RTW89_TXWD_INFO0_MULTIPORT_ID, desc_info->port);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_info0_v1(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_INFO0_DATA_STBC, desc_info->stbc) |
		    FIELD_PREP(RTW89_TXWD_INFO0_DATA_LDPC, desc_info->ldpc) |
		    FIELD_PREP(RTW89_TXWD_INFO0_DISDATAFB, desc_info->dis_data_fb) |
		    FIELD_PREP(RTW89_TXWD_INFO0_MULTIPORT_ID, desc_info->port) |
		    FIELD_PREP(RTW89_TXWD_INFO0_DATA_ER, desc_info->er_cap) |
		    FIELD_PREP(RTW89_TXWD_INFO0_DATA_BW_ER, 0);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_info1(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_INFO1_MAX_AGGNUM, desc_info->ampdu_num) |
		    FIELD_PREP(RTW89_TXWD_INFO1_A_CTRL_BSR, desc_info->a_ctrl_bsr) |
		    FIELD_PREP(RTW89_TXWD_INFO1_DATA_RTY_LOWEST_RATE,
			       desc_info->data_retry_lowest_rate);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_info2(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_INFO2_AMPDU_DENSITY, desc_info->ampdu_density) |
		    FIELD_PREP(RTW89_TXWD_INFO2_SEC_TYPE, desc_info->sec_type) |
		    FIELD_PREP(RTW89_TXWD_INFO2_SEC_HW_ENC, desc_info->sec_en) |
		    FIELD_PREP(RTW89_TXWD_INFO2_SEC_CAM_IDX, desc_info->sec_cam_idx);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_info2_v1(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(RTW89_TXWD_INFO2_AMPDU_DENSITY, desc_info->ampdu_density) |
		    FIELD_PREP(RTW89_TXWD_INFO2_FORCE_KEY_EN, desc_info->sec_en) |
		    FIELD_PREP(RTW89_TXWD_INFO2_SEC_CAM_IDX, desc_info->sec_cam_idx);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_info4(struct rtw89_tx_desc_info *desc_info)
{
	bool rts_en = !desc_info->is_bmc;
	u32 dword = FIELD_PREP(RTW89_TXWD_INFO4_RTS_EN, rts_en) |
		    FIELD_PREP(RTW89_TXWD_INFO4_HW_RTS_EN, 1);

	return cpu_to_le32(dword);
}

void rtw89_core_fill_txdesc(struct rtw89_dev *rtwdev,
			    struct rtw89_tx_desc_info *desc_info,
			    void *txdesc)
{
	struct rtw89_txwd_body *txwd_body = (struct rtw89_txwd_body *)txdesc;
	struct rtw89_txwd_info *txwd_info;

	txwd_body->dword0 = rtw89_build_txwd_body0(desc_info);
	txwd_body->dword2 = rtw89_build_txwd_body2(desc_info);
	txwd_body->dword3 = rtw89_build_txwd_body3(desc_info);

	if (!desc_info->en_wd_info)
		return;

	txwd_info = (struct rtw89_txwd_info *)(txwd_body + 1);
	txwd_info->dword0 = rtw89_build_txwd_info0(desc_info);
	txwd_info->dword1 = rtw89_build_txwd_info1(desc_info);
	txwd_info->dword2 = rtw89_build_txwd_info2(desc_info);
	txwd_info->dword4 = rtw89_build_txwd_info4(desc_info);

}
EXPORT_SYMBOL(rtw89_core_fill_txdesc);

void rtw89_core_fill_txdesc_v1(struct rtw89_dev *rtwdev,
			       struct rtw89_tx_desc_info *desc_info,
			       void *txdesc)
{
	struct rtw89_txwd_body_v1 *txwd_body = (struct rtw89_txwd_body_v1 *)txdesc;
	struct rtw89_txwd_info *txwd_info;

	txwd_body->dword0 = rtw89_build_txwd_body0_v1(desc_info);
	txwd_body->dword1 = rtw89_build_txwd_body1_v1(desc_info);
	txwd_body->dword2 = rtw89_build_txwd_body2(desc_info);
	txwd_body->dword3 = rtw89_build_txwd_body3(desc_info);
	if (desc_info->sec_en) {
		txwd_body->dword4 = rtw89_build_txwd_body4(desc_info);
		txwd_body->dword5 = rtw89_build_txwd_body5(desc_info);
	}
	txwd_body->dword7 = rtw89_build_txwd_body7_v1(desc_info);

	if (!desc_info->en_wd_info)
		return;

	txwd_info = (struct rtw89_txwd_info *)(txwd_body + 1);
	txwd_info->dword0 = rtw89_build_txwd_info0_v1(desc_info);
	txwd_info->dword1 = rtw89_build_txwd_info1(desc_info);
	txwd_info->dword2 = rtw89_build_txwd_info2_v1(desc_info);
	txwd_info->dword4 = rtw89_build_txwd_info4(desc_info);
}
EXPORT_SYMBOL(rtw89_core_fill_txdesc_v1);

static __le32 rtw89_build_txwd_body0_v2(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(BE_TXD_BODY0_WP_OFFSET_V1, desc_info->wp_offset) |
		    FIELD_PREP(BE_TXD_BODY0_WDINFO_EN, desc_info->en_wd_info) |
		    FIELD_PREP(BE_TXD_BODY0_CH_DMA, desc_info->ch_dma) |
		    FIELD_PREP(BE_TXD_BODY0_HDR_LLC_LEN, desc_info->hdr_llc_len) |
		    FIELD_PREP(BE_TXD_BODY0_WD_PAGE, desc_info->wd_page);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_body1_v2(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(BE_TXD_BODY1_ADDR_INFO_NUM, desc_info->addr_info_nr) |
		    FIELD_PREP(BE_TXD_BODY1_SEC_KEYID, desc_info->sec_keyid) |
		    FIELD_PREP(BE_TXD_BODY1_SEC_TYPE, desc_info->sec_type);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_body2_v2(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(BE_TXD_BODY2_TID_IND, desc_info->tid_indicate) |
		    FIELD_PREP(BE_TXD_BODY2_QSEL, desc_info->qsel) |
		    FIELD_PREP(BE_TXD_BODY2_TXPKTSIZE, desc_info->pkt_size) |
		    FIELD_PREP(BE_TXD_BODY2_AGG_EN, desc_info->agg_en) |
		    FIELD_PREP(BE_TXD_BODY2_BK, desc_info->bk) |
		    FIELD_PREP(BE_TXD_BODY2_MACID, desc_info->mac_id);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_body3_v2(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(BE_TXD_BODY3_WIFI_SEQ, desc_info->seq);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_body4_v2(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(BE_TXD_BODY4_SEC_IV_L0, desc_info->sec_seq[0]) |
		    FIELD_PREP(BE_TXD_BODY4_SEC_IV_L1, desc_info->sec_seq[1]);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_body5_v2(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(BE_TXD_BODY5_SEC_IV_H2, desc_info->sec_seq[2]) |
		    FIELD_PREP(BE_TXD_BODY5_SEC_IV_H3, desc_info->sec_seq[3]) |
		    FIELD_PREP(BE_TXD_BODY5_SEC_IV_H4, desc_info->sec_seq[4]) |
		    FIELD_PREP(BE_TXD_BODY5_SEC_IV_H5, desc_info->sec_seq[5]);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_body7_v2(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(BE_TXD_BODY7_USERATE_SEL, desc_info->use_rate) |
		    FIELD_PREP(BE_TXD_BODY7_DATA_ER, desc_info->er_cap) |
		    FIELD_PREP(BE_TXD_BODY7_DATA_BW_ER, 0) |
		    FIELD_PREP(BE_TXD_BODY7_DATARATE, desc_info->data_rate);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_info0_v2(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(BE_TXD_INFO0_DATA_STBC, desc_info->stbc) |
		    FIELD_PREP(BE_TXD_INFO0_DATA_LDPC, desc_info->ldpc) |
		    FIELD_PREP(BE_TXD_INFO0_DISDATAFB, desc_info->dis_data_fb) |
		    FIELD_PREP(BE_TXD_INFO0_MULTIPORT_ID, desc_info->port);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_info1_v2(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(BE_TXD_INFO1_MAX_AGG_NUM, desc_info->ampdu_num) |
		    FIELD_PREP(BE_TXD_INFO1_A_CTRL_BSR, desc_info->a_ctrl_bsr) |
		    FIELD_PREP(BE_TXD_INFO1_DATA_RTY_LOWEST_RATE,
			       desc_info->data_retry_lowest_rate);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_info2_v2(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(BE_TXD_INFO2_AMPDU_DENSITY, desc_info->ampdu_density) |
		    FIELD_PREP(BE_TXD_INFO2_FORCE_KEY_EN, desc_info->sec_en) |
		    FIELD_PREP(BE_TXD_INFO2_SEC_CAM_IDX, desc_info->sec_cam_idx);

	return cpu_to_le32(dword);
}

static __le32 rtw89_build_txwd_info4_v2(struct rtw89_tx_desc_info *desc_info)
{
	bool rts_en = !desc_info->is_bmc;
	u32 dword = FIELD_PREP(BE_TXD_INFO4_RTS_EN, rts_en) |
		    FIELD_PREP(BE_TXD_INFO4_HW_RTS_EN, 1);

	return cpu_to_le32(dword);
}

void rtw89_core_fill_txdesc_v2(struct rtw89_dev *rtwdev,
			       struct rtw89_tx_desc_info *desc_info,
			       void *txdesc)
{
	struct rtw89_txwd_body_v2 *txwd_body = txdesc;
	struct rtw89_txwd_info_v2 *txwd_info;

	txwd_body->dword0 = rtw89_build_txwd_body0_v2(desc_info);
	txwd_body->dword1 = rtw89_build_txwd_body1_v2(desc_info);
	txwd_body->dword2 = rtw89_build_txwd_body2_v2(desc_info);
	txwd_body->dword3 = rtw89_build_txwd_body3_v2(desc_info);
	if (desc_info->sec_en) {
		txwd_body->dword4 = rtw89_build_txwd_body4_v2(desc_info);
		txwd_body->dword5 = rtw89_build_txwd_body5_v2(desc_info);
	}
	txwd_body->dword7 = rtw89_build_txwd_body7_v2(desc_info);

	if (!desc_info->en_wd_info)
		return;

	txwd_info = (struct rtw89_txwd_info_v2 *)(txwd_body + 1);
	txwd_info->dword0 = rtw89_build_txwd_info0_v2(desc_info);
	txwd_info->dword1 = rtw89_build_txwd_info1_v2(desc_info);
	txwd_info->dword2 = rtw89_build_txwd_info2_v2(desc_info);
	txwd_info->dword4 = rtw89_build_txwd_info4_v2(desc_info);
}
EXPORT_SYMBOL(rtw89_core_fill_txdesc_v2);

static __le32 rtw89_build_txwd_fwcmd0_v1(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(AX_RXD_RPKT_LEN_MASK, desc_info->pkt_size) |
		    FIELD_PREP(AX_RXD_RPKT_TYPE_MASK, desc_info->fw_dl ?
						      RTW89_CORE_RX_TYPE_FWDL :
						      RTW89_CORE_RX_TYPE_H2C);

	return cpu_to_le32(dword);
}

void rtw89_core_fill_txdesc_fwcmd_v1(struct rtw89_dev *rtwdev,
				     struct rtw89_tx_desc_info *desc_info,
				     void *txdesc)
{
	struct rtw89_rxdesc_short *txwd_v1 = (struct rtw89_rxdesc_short *)txdesc;

	txwd_v1->dword0 = rtw89_build_txwd_fwcmd0_v1(desc_info);
}
EXPORT_SYMBOL(rtw89_core_fill_txdesc_fwcmd_v1);

static __le32 rtw89_build_txwd_fwcmd0_v2(struct rtw89_tx_desc_info *desc_info)
{
	u32 dword = FIELD_PREP(BE_RXD_RPKT_LEN_MASK, desc_info->pkt_size) |
		    FIELD_PREP(BE_RXD_RPKT_TYPE_MASK, desc_info->fw_dl ?
						      RTW89_CORE_RX_TYPE_FWDL :
						      RTW89_CORE_RX_TYPE_H2C);

	return cpu_to_le32(dword);
}

void rtw89_core_fill_txdesc_fwcmd_v2(struct rtw89_dev *rtwdev,
				     struct rtw89_tx_desc_info *desc_info,
				     void *txdesc)
{
	struct rtw89_rxdesc_short_v2 *txwd_v2 = (struct rtw89_rxdesc_short_v2 *)txdesc;

	txwd_v2->dword0 = rtw89_build_txwd_fwcmd0_v2(desc_info);
}
EXPORT_SYMBOL(rtw89_core_fill_txdesc_fwcmd_v2);

static int rtw89_core_rx_process_mac_ppdu(struct rtw89_dev *rtwdev,
					  struct sk_buff *skb,
					  struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_rxinfo *rxinfo = (const struct rtw89_rxinfo *)skb->data;
	const struct rtw89_rxinfo_user *user;
	enum rtw89_chip_gen chip_gen = rtwdev->chip->chip_gen;
	int rx_cnt_size = RTW89_PPDU_MAC_RX_CNT_SIZE;
	bool rx_cnt_valid = false;
	bool invalid = false;
	u8 plcp_size = 0;
	u8 *phy_sts;
	u8 usr_num;
	int i;

	if (chip_gen == RTW89_CHIP_BE) {
		invalid = le32_get_bits(rxinfo->w0, RTW89_RXINFO_W0_INVALID_V1);
		rx_cnt_size = RTW89_PPDU_MAC_RX_CNT_SIZE_V1;
	}

	if (invalid)
		return -EINVAL;

	rx_cnt_valid = le32_get_bits(rxinfo->w0, RTW89_RXINFO_W0_RX_CNT_VLD);
	if (chip_gen == RTW89_CHIP_BE) {
		plcp_size = le32_get_bits(rxinfo->w0, RTW89_RXINFO_W0_PLCP_LEN_V1) << 3;
		usr_num = le32_get_bits(rxinfo->w0, RTW89_RXINFO_W0_USR_NUM_V1);
	} else {
		plcp_size = le32_get_bits(rxinfo->w1, RTW89_RXINFO_W1_PLCP_LEN) << 3;
		usr_num = le32_get_bits(rxinfo->w0, RTW89_RXINFO_W0_USR_NUM);
	}
	if (usr_num > chip->ppdu_max_usr) {
		rtw89_warn(rtwdev, "Invalid user number (%d) in mac info\n",
			   usr_num);
		return -EINVAL;
	}

	for (i = 0; i < usr_num; i++) {
		user = &rxinfo->user[i];
		if (!le32_get_bits(user->w0, RTW89_RXINFO_USER_MAC_ID_VALID))
			continue;
		/* For WiFi 7 chips, RXWD.mac_id of PPDU status is not set
		 * by hardware, so update mac_id by rxinfo_user[].mac_id.
		 */
		if (chip_gen == RTW89_CHIP_BE)
			phy_ppdu->mac_id =
				le32_get_bits(user->w0, RTW89_RXINFO_USER_MACID);
		phy_ppdu->has_data =
			le32_get_bits(user->w0, RTW89_RXINFO_USER_DATA);
		phy_ppdu->has_bcn =
			le32_get_bits(user->w0, RTW89_RXINFO_USER_BCN);
		break;
	}

	phy_sts = skb->data + RTW89_PPDU_MAC_INFO_SIZE;
	phy_sts += usr_num * RTW89_PPDU_MAC_INFO_USR_SIZE;
	/* 8-byte alignment */
	if (usr_num & BIT(0))
		phy_sts += RTW89_PPDU_MAC_INFO_USR_SIZE;
	if (rx_cnt_valid)
		phy_sts += rx_cnt_size;
	phy_sts += plcp_size;

	if (phy_sts > skb->data + skb->len)
		return -EINVAL;

	phy_ppdu->buf = phy_sts;
	phy_ppdu->len = skb->data + skb->len - phy_sts;

	return 0;
}

static u8 rtw89_get_data_rate_nss(struct rtw89_dev *rtwdev, u16 data_rate)
{
	u8 data_rate_mode;

	data_rate_mode = rtw89_get_data_rate_mode(rtwdev, data_rate);
	switch (data_rate_mode) {
	case DATA_RATE_MODE_NON_HT:
		return 1;
	case DATA_RATE_MODE_HT:
		return rtw89_get_data_ht_nss(rtwdev, data_rate) + 1;
	case DATA_RATE_MODE_VHT:
	case DATA_RATE_MODE_HE:
	case DATA_RATE_MODE_EHT:
		return rtw89_get_data_nss(rtwdev, data_rate) + 1;
	default:
		rtw89_warn(rtwdev, "invalid RX rate mode %d\n", data_rate_mode);
		return 0;
	}
}

static void rtw89_core_rx_process_phy_ppdu_iter(void *data,
						struct ieee80211_sta *sta)
{
	struct rtw89_rx_phy_ppdu *phy_ppdu = (struct rtw89_rx_phy_ppdu *)data;
	struct rtw89_sta *rtwsta = sta_to_rtwsta(sta);
	struct rtw89_dev *rtwdev = rtwsta->rtwdev;
	struct rtw89_hal *hal = &rtwdev->hal;
	struct rtw89_sta_link *rtwsta_link;
	u8 ant_num = hal->ant_diversity ? 2 : rtwdev->chip->rf_path_num;
	u8 ant_pos = U8_MAX;
	u8 evm_pos = 0;
	int i;

	/* FIXME: For single link, taking link on HW-0 here is okay. But, when
	 * enabling multiple active links, we should determine the right link.
	 */
	rtwsta_link = rtw89_sta_get_link_inst(rtwsta, 0);
	if (unlikely(!rtwsta_link))
		return;

	if (rtwsta_link->mac_id != phy_ppdu->mac_id || !phy_ppdu->to_self)
		return;

	if (hal->ant_diversity && hal->antenna_rx) {
		ant_pos = __ffs(hal->antenna_rx);
		evm_pos = ant_pos;
	}

	ewma_rssi_add(&rtwsta_link->avg_rssi, phy_ppdu->rssi_avg);

	if (ant_pos < ant_num) {
		ewma_rssi_add(&rtwsta_link->rssi[ant_pos], phy_ppdu->rssi[0]);
	} else {
		for (i = 0; i < rtwdev->chip->rf_path_num; i++)
			ewma_rssi_add(&rtwsta_link->rssi[i], phy_ppdu->rssi[i]);
	}

	if (phy_ppdu->ofdm.has && (phy_ppdu->has_data || phy_ppdu->has_bcn)) {
		ewma_snr_add(&rtwsta_link->avg_snr, phy_ppdu->ofdm.avg_snr);
		if (rtw89_get_data_rate_nss(rtwdev, phy_ppdu->rate) == 1) {
			ewma_evm_add(&rtwsta_link->evm_1ss, phy_ppdu->ofdm.evm_min);
		} else {
			ewma_evm_add(&rtwsta_link->evm_min[evm_pos],
				     phy_ppdu->ofdm.evm_min);
			ewma_evm_add(&rtwsta_link->evm_max[evm_pos],
				     phy_ppdu->ofdm.evm_max);
		}
	}
}

#define VAR_LEN 0xff
#define VAR_LEN_UNIT 8
static u16 rtw89_core_get_phy_status_ie_len(struct rtw89_dev *rtwdev,
					    const struct rtw89_phy_sts_iehdr *iehdr)
{
	static const u8 physts_ie_len_tabs[RTW89_CHIP_GEN_NUM][32] = {
		[RTW89_CHIP_AX] = {
			16, 32, 24, 24, 8, 8, 8, 8, VAR_LEN, 8, VAR_LEN, 176, VAR_LEN,
			VAR_LEN, VAR_LEN, VAR_LEN, VAR_LEN, VAR_LEN, 16, 24, VAR_LEN,
			VAR_LEN, VAR_LEN, 0, 24, 24, 24, 24, 32, 32, 32, 32
		},
		[RTW89_CHIP_BE] = {
			32, 40, 24, 24, 8, 8, 8, 8, VAR_LEN, 8, VAR_LEN, 176, VAR_LEN,
			VAR_LEN, VAR_LEN, VAR_LEN, VAR_LEN, VAR_LEN, 16, 24, VAR_LEN,
			VAR_LEN, VAR_LEN, 0, 24, 24, 24, 24, 32, 32, 32, 32
		},
	};
	const u8 *physts_ie_len_tab;
	u16 ie_len;
	u8 ie;

	physts_ie_len_tab = physts_ie_len_tabs[rtwdev->chip->chip_gen];

	ie = le32_get_bits(iehdr->w0, RTW89_PHY_STS_IEHDR_TYPE);
	if (physts_ie_len_tab[ie] != VAR_LEN)
		ie_len = physts_ie_len_tab[ie];
	else
		ie_len = le32_get_bits(iehdr->w0, RTW89_PHY_STS_IEHDR_LEN) * VAR_LEN_UNIT;

	return ie_len;
}

static void rtw89_core_parse_phy_status_ie01_v2(struct rtw89_dev *rtwdev,
						const struct rtw89_phy_sts_iehdr *iehdr,
						struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	const struct rtw89_phy_sts_ie01_v2 *ie;
	u8 *rpl_fd = phy_ppdu->rpl_fd;

	ie = (const struct rtw89_phy_sts_ie01_v2 *)iehdr;
	rpl_fd[RF_PATH_A] = le32_get_bits(ie->w8, RTW89_PHY_STS_IE01_V2_W8_RPL_FD_A);
	rpl_fd[RF_PATH_B] = le32_get_bits(ie->w8, RTW89_PHY_STS_IE01_V2_W8_RPL_FD_B);
	rpl_fd[RF_PATH_C] = le32_get_bits(ie->w9, RTW89_PHY_STS_IE01_V2_W9_RPL_FD_C);
	rpl_fd[RF_PATH_D] = le32_get_bits(ie->w9, RTW89_PHY_STS_IE01_V2_W9_RPL_FD_D);

	phy_ppdu->bw_idx = le32_get_bits(ie->w5, RTW89_PHY_STS_IE01_V2_W5_BW_IDX);
}

static void rtw89_core_parse_phy_status_ie01(struct rtw89_dev *rtwdev,
					     const struct rtw89_phy_sts_iehdr *iehdr,
					     struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	const struct rtw89_phy_sts_ie01 *ie = (const struct rtw89_phy_sts_ie01 *)iehdr;
	s16 cfo;
	u32 t;

	phy_ppdu->chan_idx = le32_get_bits(ie->w0, RTW89_PHY_STS_IE01_W0_CH_IDX);

	if (rtwdev->hw->conf.flags & IEEE80211_CONF_MONITOR) {
		phy_ppdu->ldpc = le32_get_bits(ie->w2, RTW89_PHY_STS_IE01_W2_LDPC);
		phy_ppdu->stbc = le32_get_bits(ie->w2, RTW89_PHY_STS_IE01_W2_STBC);
	}

	if (!phy_ppdu->hdr_2_en)
		phy_ppdu->rx_path_en =
			le32_get_bits(ie->w0, RTW89_PHY_STS_IE01_W0_RX_PATH_EN);

	if (phy_ppdu->rate < RTW89_HW_RATE_OFDM6)
		return;

	if (!phy_ppdu->to_self)
		return;

	phy_ppdu->rpl_avg = le32_get_bits(ie->w0, RTW89_PHY_STS_IE01_W0_RSSI_AVG_FD);
	phy_ppdu->ofdm.avg_snr = le32_get_bits(ie->w2, RTW89_PHY_STS_IE01_W2_AVG_SNR);
	phy_ppdu->ofdm.evm_max = le32_get_bits(ie->w2, RTW89_PHY_STS_IE01_W2_EVM_MAX);
	phy_ppdu->ofdm.evm_min = le32_get_bits(ie->w2, RTW89_PHY_STS_IE01_W2_EVM_MIN);
	phy_ppdu->ofdm.has = true;

	/* sign conversion for S(12,2) */
	if (rtwdev->chip->cfo_src_fd) {
		t = le32_get_bits(ie->w1, RTW89_PHY_STS_IE01_W1_FD_CFO);
		cfo = sign_extend32(t, 11);
	} else {
		t = le32_get_bits(ie->w1, RTW89_PHY_STS_IE01_W1_PREMB_CFO);
		cfo = sign_extend32(t, 11);
	}

	rtw89_phy_cfo_parse(rtwdev, cfo, phy_ppdu);

	if (rtwdev->chip->chip_gen == RTW89_CHIP_BE)
		rtw89_core_parse_phy_status_ie01_v2(rtwdev, iehdr, phy_ppdu);
}

static void rtw89_core_parse_phy_status_ie00(struct rtw89_dev *rtwdev,
					     const struct rtw89_phy_sts_iehdr *iehdr,
					     struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	const struct rtw89_phy_sts_ie00 *ie = (const struct rtw89_phy_sts_ie00 *)iehdr;
	u16 tmp_rpl;

	tmp_rpl = le32_get_bits(ie->w0, RTW89_PHY_STS_IE00_W0_RPL);
	phy_ppdu->rpl_avg = tmp_rpl >> 1;
}

static void rtw89_core_parse_phy_status_ie00_v2(struct rtw89_dev *rtwdev,
						const struct rtw89_phy_sts_iehdr *iehdr,
						struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	const struct rtw89_phy_sts_ie00_v2 *ie;
	u8 *rpl_path = phy_ppdu->rpl_path;
	u16 tmp_rpl[RF_PATH_MAX];
	u8 i;

	ie = (const struct rtw89_phy_sts_ie00_v2 *)iehdr;
	tmp_rpl[RF_PATH_A] = le32_get_bits(ie->w4, RTW89_PHY_STS_IE00_V2_W4_RPL_TD_A);
	tmp_rpl[RF_PATH_B] = le32_get_bits(ie->w4, RTW89_PHY_STS_IE00_V2_W4_RPL_TD_B);
	tmp_rpl[RF_PATH_C] = le32_get_bits(ie->w4, RTW89_PHY_STS_IE00_V2_W4_RPL_TD_C);
	tmp_rpl[RF_PATH_D] = le32_get_bits(ie->w5, RTW89_PHY_STS_IE00_V2_W5_RPL_TD_D);

	for (i = 0; i < RF_PATH_MAX; i++)
		rpl_path[i] = tmp_rpl[i] >> 1;
}

static int rtw89_core_process_phy_status_ie(struct rtw89_dev *rtwdev,
					    const struct rtw89_phy_sts_iehdr *iehdr,
					    struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	u8 ie;

	ie = le32_get_bits(iehdr->w0, RTW89_PHY_STS_IEHDR_TYPE);

	switch (ie) {
	case RTW89_PHYSTS_IE00_CMN_CCK:
		rtw89_core_parse_phy_status_ie00(rtwdev, iehdr, phy_ppdu);
		if (rtwdev->chip->chip_gen == RTW89_CHIP_BE)
			rtw89_core_parse_phy_status_ie00_v2(rtwdev, iehdr, phy_ppdu);
		break;
	case RTW89_PHYSTS_IE01_CMN_OFDM:
		rtw89_core_parse_phy_status_ie01(rtwdev, iehdr, phy_ppdu);
		break;
	default:
		break;
	}

	return 0;
}

static void rtw89_core_update_phy_ppdu_hdr_v2(struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	const struct rtw89_phy_sts_hdr_v2 *hdr = phy_ppdu->buf + PHY_STS_HDR_LEN;

	phy_ppdu->rx_path_en = le32_get_bits(hdr->w0, RTW89_PHY_STS_HDR_V2_W0_PATH_EN);
}

static void rtw89_core_update_phy_ppdu(struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	const struct rtw89_phy_sts_hdr *hdr = phy_ppdu->buf;
	u8 *rssi = phy_ppdu->rssi;

	phy_ppdu->ie = le32_get_bits(hdr->w0, RTW89_PHY_STS_HDR_W0_IE_MAP);
	phy_ppdu->rssi_avg = le32_get_bits(hdr->w0, RTW89_PHY_STS_HDR_W0_RSSI_AVG);
	rssi[RF_PATH_A] = le32_get_bits(hdr->w1, RTW89_PHY_STS_HDR_W1_RSSI_A);
	rssi[RF_PATH_B] = le32_get_bits(hdr->w1, RTW89_PHY_STS_HDR_W1_RSSI_B);
	rssi[RF_PATH_C] = le32_get_bits(hdr->w1, RTW89_PHY_STS_HDR_W1_RSSI_C);
	rssi[RF_PATH_D] = le32_get_bits(hdr->w1, RTW89_PHY_STS_HDR_W1_RSSI_D);

	phy_ppdu->hdr_2_en = le32_get_bits(hdr->w0, RTW89_PHY_STS_HDR_W0_HDR_2_EN);
	if (phy_ppdu->hdr_2_en)
		rtw89_core_update_phy_ppdu_hdr_v2(phy_ppdu);
}

static int rtw89_core_rx_process_phy_ppdu(struct rtw89_dev *rtwdev,
					  struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	const struct rtw89_phy_sts_hdr *hdr = phy_ppdu->buf;
	u32 len_from_header;
	bool physts_valid;

	physts_valid = le32_get_bits(hdr->w0, RTW89_PHY_STS_HDR_W0_VALID);
	if (!physts_valid)
		return -EINVAL;

	len_from_header = le32_get_bits(hdr->w0, RTW89_PHY_STS_HDR_W0_LEN) << 3;

	if (rtwdev->chip->chip_gen == RTW89_CHIP_BE)
		len_from_header += PHY_STS_HDR_LEN;

	if (len_from_header != phy_ppdu->len) {
		rtw89_debug(rtwdev, RTW89_DBG_UNEXP, "phy ppdu len mismatch\n");
		return -EINVAL;
	}
	rtw89_core_update_phy_ppdu(phy_ppdu);

	return 0;
}

static int rtw89_core_rx_parse_phy_sts(struct rtw89_dev *rtwdev,
				       struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	u16 ie_len;
	void *pos, *end;

	/* mark invalid reports and bypass them */
	if (phy_ppdu->ie < RTW89_CCK_PKT)
		return -EINVAL;

	pos = phy_ppdu->buf + PHY_STS_HDR_LEN;
	end = phy_ppdu->buf + phy_ppdu->len;
	while (pos < end) {
		const struct rtw89_phy_sts_iehdr *iehdr = pos;

		ie_len = rtw89_core_get_phy_status_ie_len(rtwdev, iehdr);
		rtw89_core_process_phy_status_ie(rtwdev, iehdr, phy_ppdu);
		pos += ie_len;
		if (pos > end || ie_len == 0) {
			rtw89_debug(rtwdev, RTW89_DBG_TXRX,
				    "phy status parse failed\n");
			return -EINVAL;
		}
	}

	rtw89_chip_convert_rpl_to_rssi(rtwdev, phy_ppdu);
	rtw89_phy_antdiv_parse(rtwdev, phy_ppdu);

	return 0;
}

static void rtw89_core_rx_process_phy_sts(struct rtw89_dev *rtwdev,
					  struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	int ret;

	ret = rtw89_core_rx_parse_phy_sts(rtwdev, phy_ppdu);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_TXRX, "parse phy sts failed\n");
	else
		phy_ppdu->valid = true;

	ieee80211_iterate_stations_atomic(rtwdev->hw,
					  rtw89_core_rx_process_phy_ppdu_iter,
					  phy_ppdu);
}

static u8 rtw89_rxdesc_to_nl_he_eht_gi(struct rtw89_dev *rtwdev,
				       u8 desc_info_gi,
				       bool rx_status, bool eht)
{
	switch (desc_info_gi) {
	case RTW89_GILTF_SGI_4XHE08:
	case RTW89_GILTF_2XHE08:
	case RTW89_GILTF_1XHE08:
		return eht ? NL80211_RATE_INFO_EHT_GI_0_8 :
			     NL80211_RATE_INFO_HE_GI_0_8;
	case RTW89_GILTF_2XHE16:
	case RTW89_GILTF_1XHE16:
		return eht ? NL80211_RATE_INFO_EHT_GI_1_6 :
			     NL80211_RATE_INFO_HE_GI_1_6;
	case RTW89_GILTF_LGI_4XHE32:
		return eht ? NL80211_RATE_INFO_EHT_GI_3_2 :
			     NL80211_RATE_INFO_HE_GI_3_2;
	default:
		rtw89_warn(rtwdev, "invalid gi_ltf=%d", desc_info_gi);
		if (rx_status)
			return eht ? NL80211_RATE_INFO_EHT_GI_3_2 :
				     NL80211_RATE_INFO_HE_GI_3_2;
		return U8_MAX;
	}
}

static
bool rtw89_check_rx_statu_gi_match(struct ieee80211_rx_status *status, u8 gi_ltf,
				   bool eht)
{
	if (eht)
		return status->eht.gi == gi_ltf;

	return status->he_gi == gi_ltf;
}

static bool rtw89_core_rx_ppdu_match(struct rtw89_dev *rtwdev,
				     struct rtw89_rx_desc_info *desc_info,
				     struct ieee80211_rx_status *status)
{
	u8 band = desc_info->bb_sel ? RTW89_PHY_1 : RTW89_PHY_0;
	u8 data_rate_mode, bw, rate_idx = MASKBYTE0, gi_ltf;
	bool eht = false;
	u16 data_rate;
	bool ret;

	data_rate = desc_info->data_rate;
	data_rate_mode = rtw89_get_data_rate_mode(rtwdev, data_rate);
	if (data_rate_mode == DATA_RATE_MODE_NON_HT) {
		rate_idx = rtw89_get_data_not_ht_idx(rtwdev, data_rate);
		/* rate_idx is still hardware value here */
	} else if (data_rate_mode == DATA_RATE_MODE_HT) {
		rate_idx = rtw89_get_data_ht_mcs(rtwdev, data_rate);
	} else if (data_rate_mode == DATA_RATE_MODE_VHT ||
		   data_rate_mode == DATA_RATE_MODE_HE ||
		   data_rate_mode == DATA_RATE_MODE_EHT) {
		rate_idx = rtw89_get_data_mcs(rtwdev, data_rate);
	} else {
		rtw89_warn(rtwdev, "invalid RX rate mode %d\n", data_rate_mode);
	}

	eht = data_rate_mode == DATA_RATE_MODE_EHT;
	bw = rtw89_hw_to_rate_info_bw(desc_info->bw);
	gi_ltf = rtw89_rxdesc_to_nl_he_eht_gi(rtwdev, desc_info->gi_ltf, false, eht);
	ret = rtwdev->ppdu_sts.curr_rx_ppdu_cnt[band] == desc_info->ppdu_cnt &&
	      status->rate_idx == rate_idx &&
	      rtw89_check_rx_statu_gi_match(status, gi_ltf, eht) &&
	      status->bw == bw;

	return ret;
}

struct rtw89_vif_rx_stats_iter_data {
	struct rtw89_dev *rtwdev;
	struct rtw89_rx_phy_ppdu *phy_ppdu;
	struct rtw89_rx_desc_info *desc_info;
	struct sk_buff *skb;
	const u8 *bssid;
};

static void rtw89_stats_trigger_frame(struct rtw89_dev *rtwdev,
				      struct rtw89_vif_link *rtwvif_link,
				      struct ieee80211_bss_conf *bss_conf,
				      struct sk_buff *skb)
{
	struct ieee80211_trigger *tf = (struct ieee80211_trigger *)skb->data;
	struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	struct rtw89_vif *rtwvif = rtwvif_link->rtwvif;
	u8 *pos, *end, type, tf_bw;
	u16 aid, tf_rua;

	if (!ether_addr_equal(bss_conf->bssid, tf->ta) ||
	    rtwvif_link->wifi_role != RTW89_WIFI_ROLE_STATION ||
	    rtwvif_link->net_type == RTW89_NET_TYPE_NO_LINK)
		return;

	type = le64_get_bits(tf->common_info, IEEE80211_TRIGGER_TYPE_MASK);
	if (type != IEEE80211_TRIGGER_TYPE_BASIC && type != IEEE80211_TRIGGER_TYPE_MU_BAR)
		return;

	end = (u8 *)tf + skb->len;
	pos = tf->variable;

	while (end - pos >= RTW89_TF_BASIC_USER_INFO_SZ) {
		aid = RTW89_GET_TF_USER_INFO_AID12(pos);
		tf_rua = RTW89_GET_TF_USER_INFO_RUA(pos);
		tf_bw = le64_get_bits(tf->common_info, IEEE80211_TRIGGER_ULBW_MASK);
		rtw89_debug(rtwdev, RTW89_DBG_TXRX,
			    "[TF] aid: %d, ul_mcs: %d, rua: %d, bw: %d\n",
			    aid, RTW89_GET_TF_USER_INFO_UL_MCS(pos),
			    tf_rua, tf_bw);

		if (aid == RTW89_TF_PAD)
			break;

		if (aid == vif->cfg.aid) {
			enum nl80211_he_ru_alloc rua = rtw89_he_rua_to_ru_alloc(tf_rua >> 1);

			rtwvif->stats.rx_tf_acc++;
			rtwdev->stats.rx_tf_acc++;
			if (tf_bw == IEEE80211_TRIGGER_ULBW_160_80P80MHZ &&
			    rua <= NL80211_RATE_INFO_HE_RU_ALLOC_106)
				rtwvif_link->pwr_diff_en = true;
			break;
		}

		pos += RTW89_TF_BASIC_USER_INFO_SZ;
	}
}

static void rtw89_cancel_6ghz_probe_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev = container_of(work, struct rtw89_dev,
						cancel_6ghz_probe_work);
	struct list_head *pkt_list = rtwdev->scan_info.pkt_list;
	struct rtw89_pktofld_info *info;

	mutex_lock(&rtwdev->mutex);

	if (!rtwdev->scanning)
		goto out;

	list_for_each_entry(info, &pkt_list[NL80211_BAND_6GHZ], list) {
		if (!info->cancel || !test_bit(info->id, rtwdev->pkt_offload))
			continue;

		rtw89_fw_h2c_del_pkt_offload(rtwdev, info->id);

		/* Don't delete/free info from pkt_list at this moment. Let it
		 * be deleted/freed in rtw89_release_pkt_list() after scanning,
		 * since if during scanning, pkt_list is accessed in bottom half.
		 */
	}

out:
	mutex_unlock(&rtwdev->mutex);
}

static void rtw89_core_cancel_6ghz_probe_tx(struct rtw89_dev *rtwdev,
					    struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	struct list_head *pkt_list = rtwdev->scan_info.pkt_list;
	struct rtw89_pktofld_info *info;
	const u8 *ies = mgmt->u.beacon.variable, *ssid_ie;
	bool queue_work = false;

	if (rx_status->band != NL80211_BAND_6GHZ)
		return;

	ssid_ie = cfg80211_find_ie(WLAN_EID_SSID, ies, skb->len);

	list_for_each_entry(info, &pkt_list[NL80211_BAND_6GHZ], list) {
		if (ether_addr_equal(info->bssid, mgmt->bssid)) {
			info->cancel = true;
			queue_work = true;
			continue;
		}

		if (!ssid_ie || ssid_ie[1] != info->ssid_len || info->ssid_len == 0)
			continue;

		if (memcmp(&ssid_ie[2], info->ssid, info->ssid_len) == 0) {
			info->cancel = true;
			queue_work = true;
		}
	}

	if (queue_work)
		ieee80211_queue_work(rtwdev->hw, &rtwdev->cancel_6ghz_probe_work);
}

static void rtw89_vif_sync_bcn_tsf(struct rtw89_vif_link *rtwvif_link,
				   struct ieee80211_hdr *hdr, size_t len)
{
	struct ieee80211_mgmt *mgmt = (typeof(mgmt))hdr;

	if (len < offsetof(typeof(*mgmt), u.beacon.variable))
		return;

	WRITE_ONCE(rtwvif_link->sync_bcn_tsf, le64_to_cpu(mgmt->u.beacon.timestamp));
}

static void rtw89_vif_rx_stats_iter(void *data, u8 *mac,
				    struct ieee80211_vif *vif)
{
	struct rtw89_vif_rx_stats_iter_data *iter_data = data;
	struct rtw89_dev *rtwdev = iter_data->rtwdev;
	struct rtw89_vif *rtwvif = vif_to_rtwvif(vif);
	struct rtw89_pkt_stat *pkt_stat = &rtwdev->phystat.cur_pkt_stat;
	struct rtw89_rx_desc_info *desc_info = iter_data->desc_info;
	struct sk_buff *skb = iter_data->skb;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct rtw89_rx_phy_ppdu *phy_ppdu = iter_data->phy_ppdu;
	struct ieee80211_bss_conf *bss_conf;
	struct rtw89_vif_link *rtwvif_link;
	const u8 *bssid = iter_data->bssid;

	if (rtwdev->scanning &&
	    (ieee80211_is_beacon(hdr->frame_control) ||
	     ieee80211_is_probe_resp(hdr->frame_control)))
		rtw89_core_cancel_6ghz_probe_tx(rtwdev, skb);

	rcu_read_lock();

	/* FIXME: For single link, taking link on HW-0 here is okay. But, when
	 * enabling multiple active links, we should determine the right link.
	 */
	rtwvif_link = rtw89_vif_get_link_inst(rtwvif, 0);
	if (unlikely(!rtwvif_link))
		goto out;

	bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, false);
	if (!bss_conf->bssid)
		goto out;

	if (ieee80211_is_trigger(hdr->frame_control)) {
		rtw89_stats_trigger_frame(rtwdev, rtwvif_link, bss_conf, skb);
		goto out;
	}

	if (!ether_addr_equal(bss_conf->bssid, bssid))
		goto out;

	if (ieee80211_is_beacon(hdr->frame_control)) {
		if (vif->type == NL80211_IFTYPE_STATION &&
		    !test_bit(RTW89_FLAG_WOWLAN, rtwdev->flags)) {
			rtw89_vif_sync_bcn_tsf(rtwvif_link, hdr, skb->len);
			rtw89_fw_h2c_rssi_offload(rtwdev, phy_ppdu);
		}
		pkt_stat->beacon_nr++;
	}

	if (!ether_addr_equal(bss_conf->addr, hdr->addr1))
		goto out;

	if (desc_info->data_rate < RTW89_HW_RATE_NR)
		pkt_stat->rx_rate_cnt[desc_info->data_rate]++;

	rtw89_traffic_stats_accu(rtwdev, &rtwvif->stats, skb, false);

out:
	rcu_read_unlock();
}

static void rtw89_core_rx_stats(struct rtw89_dev *rtwdev,
				struct rtw89_rx_phy_ppdu *phy_ppdu,
				struct rtw89_rx_desc_info *desc_info,
				struct sk_buff *skb)
{
	struct rtw89_vif_rx_stats_iter_data iter_data;

	rtw89_traffic_stats_accu(rtwdev, &rtwdev->stats, skb, false);

	iter_data.rtwdev = rtwdev;
	iter_data.phy_ppdu = phy_ppdu;
	iter_data.desc_info = desc_info;
	iter_data.skb = skb;
	iter_data.bssid = get_hdr_bssid((struct ieee80211_hdr *)skb->data);
	rtw89_iterate_vifs_bh(rtwdev, rtw89_vif_rx_stats_iter, &iter_data);
}

static void rtw89_correct_cck_chan(struct rtw89_dev *rtwdev,
				   struct ieee80211_rx_status *status)
{
	const struct rtw89_chan_rcd *rcd =
		rtw89_chan_rcd_get(rtwdev, RTW89_CHANCTX_0);
	u16 chan = rcd->prev_primary_channel;
	u8 band = rtw89_hw_to_nl80211_band(rcd->prev_band_type);

	if (status->band != NL80211_BAND_2GHZ &&
	    status->encoding == RX_ENC_LEGACY &&
	    status->rate_idx < RTW89_HW_RATE_OFDM6) {
		status->freq = ieee80211_channel_to_frequency(chan, band);
		status->band = band;
	}
}

static void rtw89_core_hw_to_sband_rate(struct ieee80211_rx_status *rx_status)
{
	if (rx_status->band == NL80211_BAND_2GHZ ||
	    rx_status->encoding != RX_ENC_LEGACY)
		return;

	/* Some control frames' freq(ACKs in this case) are reported wrong due
	 * to FW notify timing, set to lowest rate to prevent overflow.
	 */
	if (rx_status->rate_idx < RTW89_HW_RATE_OFDM6) {
		rx_status->rate_idx = 0;
		return;
	}

	/* No 4 CCK rates for non-2G */
	rx_status->rate_idx -= 4;
}

static
void rtw89_core_update_rx_status_by_ppdu(struct rtw89_dev *rtwdev,
					 struct ieee80211_rx_status *rx_status,
					 struct rtw89_rx_phy_ppdu *phy_ppdu)
{
	if (!(rtwdev->hw->conf.flags & IEEE80211_CONF_MONITOR))
		return;

	if (!phy_ppdu)
		return;

	if (phy_ppdu->ldpc)
		rx_status->enc_flags |= RX_ENC_FLAG_LDPC;
	if (phy_ppdu->stbc)
		rx_status->enc_flags |= u8_encode_bits(1, RX_ENC_FLAG_STBC_MASK);
}

static const u8 rx_status_bw_to_radiotap_eht_usig[] = {
	[RATE_INFO_BW_20] = IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW_20MHZ,
	[RATE_INFO_BW_5] = U8_MAX,
	[RATE_INFO_BW_10] = U8_MAX,
	[RATE_INFO_BW_40] = IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW_40MHZ,
	[RATE_INFO_BW_80] = IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW_80MHZ,
	[RATE_INFO_BW_160] = IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW_160MHZ,
	[RATE_INFO_BW_HE_RU] = U8_MAX,
	[RATE_INFO_BW_320] = IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW_320MHZ_1,
	[RATE_INFO_BW_EHT_RU] = U8_MAX,
};

static void rtw89_core_update_radiotap_eht(struct rtw89_dev *rtwdev,
					   struct sk_buff *skb,
					   struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_radiotap_eht_usig *usig;
	struct ieee80211_radiotap_eht *eht;
	struct ieee80211_radiotap_tlv *tlv;
	int eht_len = struct_size(eht, user_info, 1);
	int usig_len = sizeof(*usig);
	int len;
	u8 bw;

	len = sizeof(*tlv) + ALIGN(eht_len, 4) +
	      sizeof(*tlv) + ALIGN(usig_len, 4);

	rx_status->flag |= RX_FLAG_RADIOTAP_TLV_AT_END;
	skb_reset_mac_header(skb);

	/* EHT */
	tlv = skb_push(skb, len);
	memset(tlv, 0, len);
	tlv->type = cpu_to_le16(IEEE80211_RADIOTAP_EHT);
	tlv->len = cpu_to_le16(eht_len);

	eht = (struct ieee80211_radiotap_eht *)tlv->data;
	eht->known = cpu_to_le32(IEEE80211_RADIOTAP_EHT_KNOWN_GI);
	eht->data[0] =
		le32_encode_bits(rx_status->eht.gi, IEEE80211_RADIOTAP_EHT_DATA0_GI);

	eht->user_info[0] =
		cpu_to_le32(IEEE80211_RADIOTAP_EHT_USER_INFO_MCS_KNOWN |
			    IEEE80211_RADIOTAP_EHT_USER_INFO_NSS_KNOWN_O |
			    IEEE80211_RADIOTAP_EHT_USER_INFO_CODING_KNOWN);
	eht->user_info[0] |=
		le32_encode_bits(rx_status->rate_idx, IEEE80211_RADIOTAP_EHT_USER_INFO_MCS) |
		le32_encode_bits(rx_status->nss, IEEE80211_RADIOTAP_EHT_USER_INFO_NSS_O);
	if (rx_status->enc_flags & RX_ENC_FLAG_LDPC)
		eht->user_info[0] |=
			cpu_to_le32(IEEE80211_RADIOTAP_EHT_USER_INFO_CODING);

	/* U-SIG */
	tlv = (void *)tlv + sizeof(*tlv) + ALIGN(eht_len, 4);
	tlv->type = cpu_to_le16(IEEE80211_RADIOTAP_EHT_USIG);
	tlv->len = cpu_to_le16(usig_len);

	if (rx_status->bw >= ARRAY_SIZE(rx_status_bw_to_radiotap_eht_usig))
		return;

	bw = rx_status_bw_to_radiotap_eht_usig[rx_status->bw];
	if (bw == U8_MAX)
		return;

	usig = (struct ieee80211_radiotap_eht_usig *)tlv->data;
	usig->common =
		le32_encode_bits(1, IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW_KNOWN) |
		le32_encode_bits(bw, IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW);
}

static void rtw89_core_update_radiotap(struct rtw89_dev *rtwdev,
				       struct sk_buff *skb,
				       struct ieee80211_rx_status *rx_status)
{
	static const struct ieee80211_radiotap_he known_he = {
		.data1 = cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_DATA_MCS_KNOWN |
				     IEEE80211_RADIOTAP_HE_DATA1_CODING_KNOWN |
				     IEEE80211_RADIOTAP_HE_DATA1_STBC_KNOWN |
				     IEEE80211_RADIOTAP_HE_DATA1_BW_RU_ALLOC_KNOWN),
		.data2 = cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA2_GI_KNOWN),
	};
	struct ieee80211_radiotap_he *he;

	if (!(rtwdev->hw->conf.flags & IEEE80211_CONF_MONITOR))
		return;

	if (rx_status->encoding == RX_ENC_HE) {
		rx_status->flag |= RX_FLAG_RADIOTAP_HE;
		he = skb_push(skb, sizeof(*he));
		*he = known_he;
	} else if (rx_status->encoding == RX_ENC_EHT) {
		rtw89_core_update_radiotap_eht(rtwdev, skb, rx_status);
	}
}

static void rtw89_core_rx_to_mac80211(struct rtw89_dev *rtwdev,
				      struct rtw89_rx_phy_ppdu *phy_ppdu,
				      struct rtw89_rx_desc_info *desc_info,
				      struct sk_buff *skb_ppdu,
				      struct ieee80211_rx_status *rx_status)
{
	struct napi_struct *napi = &rtwdev->napi;

	/* In low power mode, napi isn't scheduled. Receive it to netif. */
	if (unlikely(!napi_is_scheduled(napi)))
		napi = NULL;

	rtw89_core_hw_to_sband_rate(rx_status);
	rtw89_core_rx_stats(rtwdev, phy_ppdu, desc_info, skb_ppdu);
	rtw89_core_update_rx_status_by_ppdu(rtwdev, rx_status, phy_ppdu);
	rtw89_core_update_radiotap(rtwdev, skb_ppdu, rx_status);
	/* In low power mode, it does RX in thread context. */
	local_bh_disable();
	ieee80211_rx_napi(rtwdev->hw, NULL, skb_ppdu, napi);
	local_bh_enable();
	rtwdev->napi_budget_countdown--;
}

static void rtw89_core_rx_pending_skb(struct rtw89_dev *rtwdev,
				      struct rtw89_rx_phy_ppdu *phy_ppdu,
				      struct rtw89_rx_desc_info *desc_info,
				      struct sk_buff *skb)
{
	u8 band = desc_info->bb_sel ? RTW89_PHY_1 : RTW89_PHY_0;
	int curr = rtwdev->ppdu_sts.curr_rx_ppdu_cnt[band];
	struct sk_buff *skb_ppdu = NULL, *tmp;
	struct ieee80211_rx_status *rx_status;

	if (curr > RTW89_MAX_PPDU_CNT)
		return;

	skb_queue_walk_safe(&rtwdev->ppdu_sts.rx_queue[band], skb_ppdu, tmp) {
		skb_unlink(skb_ppdu, &rtwdev->ppdu_sts.rx_queue[band]);
		rx_status = IEEE80211_SKB_RXCB(skb_ppdu);
		if (rtw89_core_rx_ppdu_match(rtwdev, desc_info, rx_status))
			rtw89_chip_query_ppdu(rtwdev, phy_ppdu, rx_status);
		rtw89_correct_cck_chan(rtwdev, rx_status);
		rtw89_core_rx_to_mac80211(rtwdev, phy_ppdu, desc_info, skb_ppdu, rx_status);
	}
}

static void rtw89_core_rx_process_ppdu_sts(struct rtw89_dev *rtwdev,
					   struct rtw89_rx_desc_info *desc_info,
					   struct sk_buff *skb)
{
	struct rtw89_rx_phy_ppdu phy_ppdu = {.buf = skb->data, .valid = false,
					     .len = skb->len,
					     .to_self = desc_info->addr1_match,
					     .rate = desc_info->data_rate,
					     .mac_id = desc_info->mac_id};
	int ret;

	if (desc_info->mac_info_valid) {
		ret = rtw89_core_rx_process_mac_ppdu(rtwdev, skb, &phy_ppdu);
		if (ret)
			goto out;
	}

	ret = rtw89_core_rx_process_phy_ppdu(rtwdev, &phy_ppdu);
	if (ret)
		goto out;

	rtw89_core_rx_process_phy_sts(rtwdev, &phy_ppdu);

out:
	rtw89_core_rx_pending_skb(rtwdev, &phy_ppdu, desc_info, skb);
	dev_kfree_skb_any(skb);
}

static void rtw89_core_rx_process_report(struct rtw89_dev *rtwdev,
					 struct rtw89_rx_desc_info *desc_info,
					 struct sk_buff *skb)
{
	switch (desc_info->pkt_type) {
	case RTW89_CORE_RX_TYPE_C2H:
		rtw89_fw_c2h_irqsafe(rtwdev, skb);
		break;
	case RTW89_CORE_RX_TYPE_PPDU_STAT:
		rtw89_core_rx_process_ppdu_sts(rtwdev, desc_info, skb);
		break;
	default:
		rtw89_debug(rtwdev, RTW89_DBG_TXRX, "unhandled pkt_type=%d\n",
			    desc_info->pkt_type);
		dev_kfree_skb_any(skb);
		break;
	}
}

void rtw89_core_query_rxdesc(struct rtw89_dev *rtwdev,
			     struct rtw89_rx_desc_info *desc_info,
			     u8 *data, u32 data_offset)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_rxdesc_short *rxd_s;
	struct rtw89_rxdesc_long *rxd_l;
	u8 shift_len, drv_info_len;

	rxd_s = (struct rtw89_rxdesc_short *)(data + data_offset);
	desc_info->pkt_size = le32_get_bits(rxd_s->dword0, AX_RXD_RPKT_LEN_MASK);
	desc_info->drv_info_size = le32_get_bits(rxd_s->dword0, AX_RXD_DRV_INFO_SIZE_MASK);
	desc_info->long_rxdesc = le32_get_bits(rxd_s->dword0,  AX_RXD_LONG_RXD);
	desc_info->pkt_type = le32_get_bits(rxd_s->dword0,  AX_RXD_RPKT_TYPE_MASK);
	desc_info->mac_info_valid = le32_get_bits(rxd_s->dword0, AX_RXD_MAC_INFO_VLD);
	if (chip->chip_id == RTL8852C)
		desc_info->bw = le32_get_bits(rxd_s->dword1, AX_RXD_BW_v1_MASK);
	else
		desc_info->bw = le32_get_bits(rxd_s->dword1, AX_RXD_BW_MASK);
	desc_info->data_rate = le32_get_bits(rxd_s->dword1, AX_RXD_RX_DATARATE_MASK);
	desc_info->gi_ltf = le32_get_bits(rxd_s->dword1, AX_RXD_RX_GI_LTF_MASK);
	desc_info->user_id = le32_get_bits(rxd_s->dword1, AX_RXD_USER_ID_MASK);
	desc_info->sr_en = le32_get_bits(rxd_s->dword1, AX_RXD_SR_EN);
	desc_info->ppdu_cnt = le32_get_bits(rxd_s->dword1, AX_RXD_PPDU_CNT_MASK);
	desc_info->ppdu_type = le32_get_bits(rxd_s->dword1, AX_RXD_PPDU_TYPE_MASK);
	desc_info->free_run_cnt = le32_get_bits(rxd_s->dword2, AX_RXD_FREERUN_CNT_MASK);
	desc_info->icv_err = le32_get_bits(rxd_s->dword3, AX_RXD_ICV_ERR);
	desc_info->crc32_err = le32_get_bits(rxd_s->dword3, AX_RXD_CRC32_ERR);
	desc_info->hw_dec = le32_get_bits(rxd_s->dword3, AX_RXD_HW_DEC);
	desc_info->sw_dec = le32_get_bits(rxd_s->dword3, AX_RXD_SW_DEC);
	desc_info->addr1_match = le32_get_bits(rxd_s->dword3, AX_RXD_A1_MATCH);

	shift_len = desc_info->shift << 1; /* 2-byte unit */
	drv_info_len = desc_info->drv_info_size << 3; /* 8-byte unit */
	desc_info->offset = data_offset + shift_len + drv_info_len;
	if (desc_info->long_rxdesc)
		desc_info->rxd_len = sizeof(struct rtw89_rxdesc_long);
	else
		desc_info->rxd_len = sizeof(struct rtw89_rxdesc_short);
	desc_info->ready = true;

	if (!desc_info->long_rxdesc)
		return;

	rxd_l = (struct rtw89_rxdesc_long *)(data + data_offset);
	desc_info->frame_type = le32_get_bits(rxd_l->dword4, AX_RXD_TYPE_MASK);
	desc_info->addr_cam_valid = le32_get_bits(rxd_l->dword5, AX_RXD_ADDR_CAM_VLD);
	desc_info->addr_cam_id = le32_get_bits(rxd_l->dword5, AX_RXD_ADDR_CAM_MASK);
	desc_info->sec_cam_id = le32_get_bits(rxd_l->dword5, AX_RXD_SEC_CAM_IDX_MASK);
	desc_info->mac_id = le32_get_bits(rxd_l->dword5, AX_RXD_MAC_ID_MASK);
	desc_info->rx_pl_id = le32_get_bits(rxd_l->dword5, AX_RXD_RX_PL_ID_MASK);
}
EXPORT_SYMBOL(rtw89_core_query_rxdesc);

void rtw89_core_query_rxdesc_v2(struct rtw89_dev *rtwdev,
				struct rtw89_rx_desc_info *desc_info,
				u8 *data, u32 data_offset)
{
	struct rtw89_rxdesc_short_v2 *rxd_s;
	struct rtw89_rxdesc_long_v2 *rxd_l;
	u16 shift_len, drv_info_len, phy_rtp_len, hdr_cnv_len;

	rxd_s = (struct rtw89_rxdesc_short_v2 *)(data + data_offset);

	desc_info->pkt_size = le32_get_bits(rxd_s->dword0, BE_RXD_RPKT_LEN_MASK);
	desc_info->drv_info_size = le32_get_bits(rxd_s->dword0, BE_RXD_DRV_INFO_SZ_MASK);
	desc_info->phy_rpt_size = le32_get_bits(rxd_s->dword0, BE_RXD_PHY_RPT_SZ_MASK);
	desc_info->hdr_cnv_size = le32_get_bits(rxd_s->dword0, BE_RXD_HDR_CNV_SZ_MASK);
	desc_info->shift = le32_get_bits(rxd_s->dword0, BE_RXD_SHIFT_MASK);
	desc_info->long_rxdesc = le32_get_bits(rxd_s->dword0, BE_RXD_LONG_RXD);
	desc_info->pkt_type = le32_get_bits(rxd_s->dword0, BE_RXD_RPKT_TYPE_MASK);
	if (desc_info->pkt_type == RTW89_CORE_RX_TYPE_PPDU_STAT)
		desc_info->mac_info_valid = true;

	desc_info->frame_type = le32_get_bits(rxd_s->dword2, BE_RXD_TYPE_MASK);
	desc_info->mac_id = le32_get_bits(rxd_s->dword2, BE_RXD_MAC_ID_MASK);
	desc_info->addr_cam_valid = le32_get_bits(rxd_s->dword2, BE_RXD_ADDR_CAM_VLD);

	desc_info->icv_err = le32_get_bits(rxd_s->dword3, BE_RXD_ICV_ERR);
	desc_info->crc32_err = le32_get_bits(rxd_s->dword3, BE_RXD_CRC32_ERR);
	desc_info->hw_dec = le32_get_bits(rxd_s->dword3, BE_RXD_HW_DEC);
	desc_info->sw_dec = le32_get_bits(rxd_s->dword3, BE_RXD_SW_DEC);
	desc_info->addr1_match = le32_get_bits(rxd_s->dword3, BE_RXD_A1_MATCH);

	desc_info->bw = le32_get_bits(rxd_s->dword4, BE_RXD_BW_MASK);
	desc_info->data_rate = le32_get_bits(rxd_s->dword4, BE_RXD_RX_DATARATE_MASK);
	desc_info->gi_ltf = le32_get_bits(rxd_s->dword4, BE_RXD_RX_GI_LTF_MASK);
	desc_info->ppdu_cnt = le32_get_bits(rxd_s->dword4, BE_RXD_PPDU_CNT_MASK);
	desc_info->ppdu_type = le32_get_bits(rxd_s->dword4, BE_RXD_PPDU_TYPE_MASK);

	desc_info->free_run_cnt = le32_to_cpu(rxd_s->dword5);

	shift_len = desc_info->shift << 1; /* 2-byte unit */
	drv_info_len = desc_info->drv_info_size << 3; /* 8-byte unit */
	phy_rtp_len = desc_info->phy_rpt_size << 3; /* 8-byte unit */
	hdr_cnv_len = desc_info->hdr_cnv_size << 4; /* 16-byte unit */
	desc_info->offset = data_offset + shift_len + drv_info_len +
			    phy_rtp_len + hdr_cnv_len;

	if (desc_info->long_rxdesc)
		desc_info->rxd_len = sizeof(struct rtw89_rxdesc_long_v2);
	else
		desc_info->rxd_len = sizeof(struct rtw89_rxdesc_short_v2);
	desc_info->ready = true;

	if (!desc_info->long_rxdesc)
		return;

	rxd_l = (struct rtw89_rxdesc_long_v2 *)(data + data_offset);

	desc_info->sr_en = le32_get_bits(rxd_l->dword6, BE_RXD_SR_EN);
	desc_info->user_id = le32_get_bits(rxd_l->dword6, BE_RXD_USER_ID_MASK);
	desc_info->addr_cam_id = le32_get_bits(rxd_l->dword6, BE_RXD_ADDR_CAM_MASK);
	desc_info->sec_cam_id = le32_get_bits(rxd_l->dword6, BE_RXD_SEC_CAM_IDX_MASK);

	desc_info->rx_pl_id = le32_get_bits(rxd_l->dword7, BE_RXD_RX_PL_ID_MASK);
}
EXPORT_SYMBOL(rtw89_core_query_rxdesc_v2);

struct rtw89_core_iter_rx_status {
	struct rtw89_dev *rtwdev;
	struct ieee80211_rx_status *rx_status;
	struct rtw89_rx_desc_info *desc_info;
	u8 mac_id;
};

static
void rtw89_core_stats_sta_rx_status_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw89_core_iter_rx_status *iter_data =
				(struct rtw89_core_iter_rx_status *)data;
	struct ieee80211_rx_status *rx_status = iter_data->rx_status;
	struct rtw89_rx_desc_info *desc_info = iter_data->desc_info;
	struct rtw89_sta *rtwsta = sta_to_rtwsta(sta);
	struct rtw89_sta_link *rtwsta_link;
	u8 mac_id = iter_data->mac_id;

	/* FIXME: For single link, taking link on HW-0 here is okay. But, when
	 * enabling multiple active links, we should determine the right link.
	 */
	rtwsta_link = rtw89_sta_get_link_inst(rtwsta, 0);
	if (unlikely(!rtwsta_link))
		return;

	if (mac_id != rtwsta_link->mac_id)
		return;

	rtwsta_link->rx_status = *rx_status;
	rtwsta_link->rx_hw_rate = desc_info->data_rate;
}

static void rtw89_core_stats_sta_rx_status(struct rtw89_dev *rtwdev,
					   struct rtw89_rx_desc_info *desc_info,
					   struct ieee80211_rx_status *rx_status)
{
	struct rtw89_core_iter_rx_status iter_data;

	if (!desc_info->addr1_match || !desc_info->long_rxdesc)
		return;

	if (desc_info->frame_type != RTW89_RX_TYPE_DATA)
		return;

	iter_data.rtwdev = rtwdev;
	iter_data.rx_status = rx_status;
	iter_data.desc_info = desc_info;
	iter_data.mac_id = desc_info->mac_id;
	ieee80211_iterate_stations_atomic(rtwdev->hw,
					  rtw89_core_stats_sta_rx_status_iter,
					  &iter_data);
}

static void rtw89_core_update_rx_status(struct rtw89_dev *rtwdev,
					struct rtw89_rx_desc_info *desc_info,
					struct ieee80211_rx_status *rx_status)
{
	const struct cfg80211_chan_def *chandef =
		rtw89_chandef_get(rtwdev, RTW89_CHANCTX_0);
	u16 data_rate;
	u8 data_rate_mode;
	bool eht = false;
	u8 gi;

	/* currently using single PHY */
	rx_status->freq = chandef->chan->center_freq;
	rx_status->band = chandef->chan->band;

	if (rtwdev->scanning &&
	    RTW89_CHK_FW_FEATURE(SCAN_OFFLOAD, &rtwdev->fw)) {
		const struct rtw89_chan *cur = rtw89_scan_chan_get(rtwdev);
		u8 chan = cur->primary_channel;
		u8 band = cur->band_type;
		enum nl80211_band nl_band;

		nl_band = rtw89_hw_to_nl80211_band(band);
		rx_status->freq = ieee80211_channel_to_frequency(chan, nl_band);
		rx_status->band = nl_band;
	}

	if (desc_info->icv_err || desc_info->crc32_err)
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (desc_info->hw_dec &&
	    !(desc_info->sw_dec || desc_info->icv_err))
		rx_status->flag |= RX_FLAG_DECRYPTED;

	rx_status->bw = rtw89_hw_to_rate_info_bw(desc_info->bw);

	data_rate = desc_info->data_rate;
	data_rate_mode = rtw89_get_data_rate_mode(rtwdev, data_rate);
	if (data_rate_mode == DATA_RATE_MODE_NON_HT) {
		rx_status->encoding = RX_ENC_LEGACY;
		rx_status->rate_idx = rtw89_get_data_not_ht_idx(rtwdev, data_rate);
		/* convert rate_idx after we get the correct band */
	} else if (data_rate_mode == DATA_RATE_MODE_HT) {
		rx_status->encoding = RX_ENC_HT;
		rx_status->rate_idx = rtw89_get_data_ht_mcs(rtwdev, data_rate);
		if (desc_info->gi_ltf)
			rx_status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
	} else if (data_rate_mode == DATA_RATE_MODE_VHT) {
		rx_status->encoding = RX_ENC_VHT;
		rx_status->rate_idx = rtw89_get_data_mcs(rtwdev, data_rate);
		rx_status->nss = rtw89_get_data_nss(rtwdev, data_rate) + 1;
		if (desc_info->gi_ltf)
			rx_status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
	} else if (data_rate_mode == DATA_RATE_MODE_HE) {
		rx_status->encoding = RX_ENC_HE;
		rx_status->rate_idx = rtw89_get_data_mcs(rtwdev, data_rate);
		rx_status->nss = rtw89_get_data_nss(rtwdev, data_rate) + 1;
	} else if (data_rate_mode == DATA_RATE_MODE_EHT) {
		rx_status->encoding = RX_ENC_EHT;
		rx_status->rate_idx = rtw89_get_data_mcs(rtwdev, data_rate);
		rx_status->nss = rtw89_get_data_nss(rtwdev, data_rate) + 1;
		eht = true;
	} else {
		rtw89_warn(rtwdev, "invalid RX rate mode %d\n", data_rate_mode);
	}

	/* he_gi is used to match ppdu, so we always fill it. */
	gi = rtw89_rxdesc_to_nl_he_eht_gi(rtwdev, desc_info->gi_ltf, true, eht);
	if (eht)
		rx_status->eht.gi = gi;
	else
		rx_status->he_gi = gi;
	rx_status->flag |= RX_FLAG_MACTIME_START;
	rx_status->mactime = desc_info->free_run_cnt;

	rtw89_core_stats_sta_rx_status(rtwdev, desc_info, rx_status);
}

static enum rtw89_ps_mode rtw89_update_ps_mode(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;

	/* FIXME: Fix __rtw89_enter_ps_mode() to consider MLO cases. */
	if (rtwdev->support_mlo)
		return RTW89_PS_MODE_NONE;

	if (rtw89_disable_ps_mode || !chip->ps_mode_supported ||
	    RTW89_CHK_FW_FEATURE(NO_DEEP_PS, &rtwdev->fw))
		return RTW89_PS_MODE_NONE;

	if ((chip->ps_mode_supported & BIT(RTW89_PS_MODE_PWR_GATED)) &&
	    !RTW89_CHK_FW_FEATURE(NO_LPS_PG, &rtwdev->fw))
		return RTW89_PS_MODE_PWR_GATED;

	if (chip->ps_mode_supported & BIT(RTW89_PS_MODE_CLK_GATED))
		return RTW89_PS_MODE_CLK_GATED;

	if (chip->ps_mode_supported & BIT(RTW89_PS_MODE_RFOFF))
		return RTW89_PS_MODE_RFOFF;

	return RTW89_PS_MODE_NONE;
}

static void rtw89_core_flush_ppdu_rx_queue(struct rtw89_dev *rtwdev,
					   struct rtw89_rx_desc_info *desc_info)
{
	struct rtw89_ppdu_sts_info *ppdu_sts = &rtwdev->ppdu_sts;
	u8 band = desc_info->bb_sel ? RTW89_PHY_1 : RTW89_PHY_0;
	struct ieee80211_rx_status *rx_status;
	struct sk_buff *skb_ppdu, *tmp;

	skb_queue_walk_safe(&ppdu_sts->rx_queue[band], skb_ppdu, tmp) {
		skb_unlink(skb_ppdu, &ppdu_sts->rx_queue[band]);
		rx_status = IEEE80211_SKB_RXCB(skb_ppdu);
		rtw89_core_rx_to_mac80211(rtwdev, NULL, desc_info, skb_ppdu, rx_status);
	}
}

void rtw89_core_rx(struct rtw89_dev *rtwdev,
		   struct rtw89_rx_desc_info *desc_info,
		   struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status;
	struct rtw89_ppdu_sts_info *ppdu_sts = &rtwdev->ppdu_sts;
	u8 ppdu_cnt = desc_info->ppdu_cnt;
	u8 band = desc_info->bb_sel ? RTW89_PHY_1 : RTW89_PHY_0;

	if (desc_info->pkt_type != RTW89_CORE_RX_TYPE_WIFI) {
		rtw89_core_rx_process_report(rtwdev, desc_info, skb);
		return;
	}

	if (ppdu_sts->curr_rx_ppdu_cnt[band] != ppdu_cnt) {
		rtw89_core_flush_ppdu_rx_queue(rtwdev, desc_info);
		ppdu_sts->curr_rx_ppdu_cnt[band] = ppdu_cnt;
	}

	rx_status = IEEE80211_SKB_RXCB(skb);
	memset(rx_status, 0, sizeof(*rx_status));
	rtw89_core_update_rx_status(rtwdev, desc_info, rx_status);
	if (desc_info->long_rxdesc &&
	    BIT(desc_info->frame_type) & PPDU_FILTER_BITMAP)
		skb_queue_tail(&ppdu_sts->rx_queue[band], skb);
	else
		rtw89_core_rx_to_mac80211(rtwdev, NULL, desc_info, skb, rx_status);
}
EXPORT_SYMBOL(rtw89_core_rx);

void rtw89_core_napi_start(struct rtw89_dev *rtwdev)
{
	if (test_and_set_bit(RTW89_FLAG_NAPI_RUNNING, rtwdev->flags))
		return;

	napi_enable(&rtwdev->napi);
}
EXPORT_SYMBOL(rtw89_core_napi_start);

void rtw89_core_napi_stop(struct rtw89_dev *rtwdev)
{
	if (!test_and_clear_bit(RTW89_FLAG_NAPI_RUNNING, rtwdev->flags))
		return;

	napi_synchronize(&rtwdev->napi);
	napi_disable(&rtwdev->napi);
}
EXPORT_SYMBOL(rtw89_core_napi_stop);

int rtw89_core_napi_init(struct rtw89_dev *rtwdev)
{
	rtwdev->netdev = alloc_netdev_dummy(0);
	if (!rtwdev->netdev)
		return -ENOMEM;

	netif_napi_add(rtwdev->netdev, &rtwdev->napi,
		       rtwdev->hci.ops->napi_poll);
	return 0;
}
EXPORT_SYMBOL(rtw89_core_napi_init);

void rtw89_core_napi_deinit(struct rtw89_dev *rtwdev)
{
	rtw89_core_napi_stop(rtwdev);
	netif_napi_del(&rtwdev->napi);
	free_netdev(rtwdev->netdev);
}
EXPORT_SYMBOL(rtw89_core_napi_deinit);

static void rtw89_core_ba_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev =
		container_of(work, struct rtw89_dev, ba_work);
	struct rtw89_txq *rtwtxq, *tmp;
	int ret;

	spin_lock_bh(&rtwdev->ba_lock);
	list_for_each_entry_safe(rtwtxq, tmp, &rtwdev->ba_list, list) {
		struct ieee80211_txq *txq = rtw89_txq_to_txq(rtwtxq);
		struct ieee80211_sta *sta = txq->sta;
		struct rtw89_sta *rtwsta = sta_to_rtwsta_safe(sta);
		u8 tid = txq->tid;

		if (!sta) {
			rtw89_warn(rtwdev, "cannot start BA without sta\n");
			goto skip_ba_work;
		}

		if (rtwsta->disassoc) {
			rtw89_debug(rtwdev, RTW89_DBG_TXRX,
				    "cannot start BA with disassoc sta\n");
			goto skip_ba_work;
		}

		ret = ieee80211_start_tx_ba_session(sta, tid, 0);
		if (ret) {
			rtw89_debug(rtwdev, RTW89_DBG_TXRX,
				    "failed to setup BA session for %pM:%2d: %d\n",
				    sta->addr, tid, ret);
			if (ret == -EINVAL)
				set_bit(RTW89_TXQ_F_BLOCK_BA, &rtwtxq->flags);
		}
skip_ba_work:
		list_del_init(&rtwtxq->list);
	}
	spin_unlock_bh(&rtwdev->ba_lock);
}

void rtw89_core_free_sta_pending_ba(struct rtw89_dev *rtwdev,
				    struct ieee80211_sta *sta)
{
	struct rtw89_txq *rtwtxq, *tmp;

	spin_lock_bh(&rtwdev->ba_lock);
	list_for_each_entry_safe(rtwtxq, tmp, &rtwdev->ba_list, list) {
		struct ieee80211_txq *txq = rtw89_txq_to_txq(rtwtxq);

		if (sta == txq->sta)
			list_del_init(&rtwtxq->list);
	}
	spin_unlock_bh(&rtwdev->ba_lock);
}

void rtw89_core_free_sta_pending_forbid_ba(struct rtw89_dev *rtwdev,
					   struct ieee80211_sta *sta)
{
	struct rtw89_txq *rtwtxq, *tmp;

	spin_lock_bh(&rtwdev->ba_lock);
	list_for_each_entry_safe(rtwtxq, tmp, &rtwdev->forbid_ba_list, list) {
		struct ieee80211_txq *txq = rtw89_txq_to_txq(rtwtxq);

		if (sta == txq->sta) {
			clear_bit(RTW89_TXQ_F_FORBID_BA, &rtwtxq->flags);
			list_del_init(&rtwtxq->list);
		}
	}
	spin_unlock_bh(&rtwdev->ba_lock);
}

void rtw89_core_free_sta_pending_roc_tx(struct rtw89_dev *rtwdev,
					struct ieee80211_sta *sta)
{
	struct rtw89_sta *rtwsta = sta_to_rtwsta(sta);
	struct sk_buff *skb, *tmp;

	skb_queue_walk_safe(&rtwsta->roc_queue, skb, tmp) {
		skb_unlink(skb, &rtwsta->roc_queue);
		dev_kfree_skb_any(skb);
	}
}

static void rtw89_core_stop_tx_ba_session(struct rtw89_dev *rtwdev,
					  struct rtw89_txq *rtwtxq)
{
	struct ieee80211_txq *txq = rtw89_txq_to_txq(rtwtxq);
	struct ieee80211_sta *sta = txq->sta;
	struct rtw89_sta *rtwsta = sta_to_rtwsta_safe(sta);

	if (unlikely(!rtwsta) || unlikely(rtwsta->disassoc))
		return;

	if (!test_bit(RTW89_TXQ_F_AMPDU, &rtwtxq->flags) ||
	    test_bit(RTW89_TXQ_F_FORBID_BA, &rtwtxq->flags))
		return;

	spin_lock_bh(&rtwdev->ba_lock);
	if (!test_and_set_bit(RTW89_TXQ_F_FORBID_BA, &rtwtxq->flags))
		list_add_tail(&rtwtxq->list, &rtwdev->forbid_ba_list);
	spin_unlock_bh(&rtwdev->ba_lock);

	ieee80211_stop_tx_ba_session(sta, txq->tid);
	cancel_delayed_work(&rtwdev->forbid_ba_work);
	ieee80211_queue_delayed_work(rtwdev->hw, &rtwdev->forbid_ba_work,
				     RTW89_FORBID_BA_TIMER);
}

static void rtw89_core_txq_check_agg(struct rtw89_dev *rtwdev,
				     struct rtw89_txq *rtwtxq,
				     struct sk_buff *skb)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	struct ieee80211_txq *txq = rtw89_txq_to_txq(rtwtxq);
	struct ieee80211_sta *sta = txq->sta;
	struct rtw89_sta *rtwsta = sta_to_rtwsta_safe(sta);

	if (test_bit(RTW89_TXQ_F_FORBID_BA, &rtwtxq->flags))
		return;

	if (unlikely(skb->protocol == cpu_to_be16(ETH_P_PAE))) {
		rtw89_core_stop_tx_ba_session(rtwdev, rtwtxq);
		return;
	}

	if (unlikely(!sta))
		return;

	if (unlikely(test_bit(RTW89_TXQ_F_BLOCK_BA, &rtwtxq->flags)))
		return;

	if (test_bit(RTW89_TXQ_F_AMPDU, &rtwtxq->flags)) {
		IEEE80211_SKB_CB(skb)->flags |= IEEE80211_TX_CTL_AMPDU;
		return;
	}

	spin_lock_bh(&rtwdev->ba_lock);
	if (!rtwsta->disassoc && list_empty(&rtwtxq->list)) {
		list_add_tail(&rtwtxq->list, &rtwdev->ba_list);
		ieee80211_queue_work(hw, &rtwdev->ba_work);
	}
	spin_unlock_bh(&rtwdev->ba_lock);
}

static void rtw89_core_txq_push(struct rtw89_dev *rtwdev,
				struct rtw89_txq *rtwtxq,
				unsigned long frame_cnt,
				unsigned long byte_cnt)
{
	struct ieee80211_txq *txq = rtw89_txq_to_txq(rtwtxq);
	struct ieee80211_vif *vif = txq->vif;
	struct ieee80211_sta *sta = txq->sta;
	struct sk_buff *skb;
	unsigned long i;
	int ret;

	rcu_read_lock();
	for (i = 0; i < frame_cnt; i++) {
		skb = ieee80211_tx_dequeue_ni(rtwdev->hw, txq);
		if (!skb) {
			rtw89_debug(rtwdev, RTW89_DBG_TXRX, "dequeue a NULL skb\n");
			goto out;
		}
		rtw89_core_txq_check_agg(rtwdev, rtwtxq, skb);
		ret = rtw89_core_tx_write(rtwdev, vif, sta, skb, NULL);
		if (ret) {
			rtw89_err(rtwdev, "failed to push txq: %d\n", ret);
			ieee80211_free_txskb(rtwdev->hw, skb);
			break;
		}
	}
out:
	rcu_read_unlock();
}

static u32 rtw89_check_and_reclaim_tx_resource(struct rtw89_dev *rtwdev, u8 tid)
{
	u8 qsel, ch_dma;

	qsel = rtw89_core_get_qsel(rtwdev, tid);
	ch_dma = rtw89_core_get_ch_dma(rtwdev, qsel);

	return rtw89_hci_check_and_reclaim_tx_resource(rtwdev, ch_dma);
}

static bool rtw89_core_txq_agg_wait(struct rtw89_dev *rtwdev,
				    struct ieee80211_txq *txq,
				    unsigned long *frame_cnt,
				    bool *sched_txq, bool *reinvoke)
{
	struct rtw89_txq *rtwtxq = (struct rtw89_txq *)txq->drv_priv;
	struct rtw89_sta *rtwsta = sta_to_rtwsta_safe(txq->sta);
	struct rtw89_sta_link *rtwsta_link;

	if (!rtwsta)
		return false;

	rtwsta_link = rtw89_sta_get_link_inst(rtwsta, 0);
	if (unlikely(!rtwsta_link)) {
		rtw89_err(rtwdev, "agg wait: find no link on HW-0\n");
		return false;
	}

	if (rtwsta_link->max_agg_wait <= 0)
		return false;

	if (rtwdev->stats.tx_tfc_lv <= RTW89_TFC_MID)
		return false;

	if (*frame_cnt > 1) {
		*frame_cnt -= 1;
		*sched_txq = true;
		*reinvoke = true;
		rtwtxq->wait_cnt = 1;
		return false;
	}

	if (*frame_cnt == 1 && rtwtxq->wait_cnt < rtwsta_link->max_agg_wait) {
		*reinvoke = true;
		rtwtxq->wait_cnt++;
		return true;
	}

	rtwtxq->wait_cnt = 0;
	return false;
}

static void rtw89_core_txq_schedule(struct rtw89_dev *rtwdev, u8 ac, bool *reinvoke)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	struct ieee80211_txq *txq;
	struct rtw89_vif *rtwvif;
	struct rtw89_txq *rtwtxq;
	unsigned long frame_cnt;
	unsigned long byte_cnt;
	u32 tx_resource;
	bool sched_txq;

	ieee80211_txq_schedule_start(hw, ac);
	while ((txq = ieee80211_next_txq(hw, ac))) {
		rtwtxq = (struct rtw89_txq *)txq->drv_priv;
		rtwvif = vif_to_rtwvif(txq->vif);

		if (rtwvif->offchan) {
			ieee80211_return_txq(hw, txq, true);
			continue;
		}
		tx_resource = rtw89_check_and_reclaim_tx_resource(rtwdev, txq->tid);
		sched_txq = false;

		ieee80211_txq_get_depth(txq, &frame_cnt, &byte_cnt);
		if (rtw89_core_txq_agg_wait(rtwdev, txq, &frame_cnt, &sched_txq, reinvoke)) {
			ieee80211_return_txq(hw, txq, true);
			continue;
		}
		frame_cnt = min_t(unsigned long, frame_cnt, tx_resource);
		rtw89_core_txq_push(rtwdev, rtwtxq, frame_cnt, byte_cnt);
		ieee80211_return_txq(hw, txq, sched_txq);
		if (frame_cnt != 0)
			rtw89_core_tx_kick_off(rtwdev, rtw89_core_get_qsel(rtwdev, txq->tid));

		/* bound of tx_resource could get stuck due to burst traffic */
		if (frame_cnt == tx_resource)
			*reinvoke = true;
	}
	ieee80211_txq_schedule_end(hw, ac);
}

static void rtw89_ips_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev = container_of(work, struct rtw89_dev,
						ips_work);
	mutex_lock(&rtwdev->mutex);
	rtw89_enter_ips_by_hwflags(rtwdev);
	mutex_unlock(&rtwdev->mutex);
}

static void rtw89_core_txq_work(struct work_struct *w)
{
	struct rtw89_dev *rtwdev = container_of(w, struct rtw89_dev, txq_work);
	bool reinvoke = false;
	u8 ac;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++)
		rtw89_core_txq_schedule(rtwdev, ac, &reinvoke);

	if (reinvoke) {
		/* reinvoke to process the last frame */
		mod_delayed_work(rtwdev->txq_wq, &rtwdev->txq_reinvoke_work, 1);
	}
}

static void rtw89_core_txq_reinvoke_work(struct work_struct *w)
{
	struct rtw89_dev *rtwdev = container_of(w, struct rtw89_dev,
						txq_reinvoke_work.work);

	queue_work(rtwdev->txq_wq, &rtwdev->txq_work);
}

static void rtw89_forbid_ba_work(struct work_struct *w)
{
	struct rtw89_dev *rtwdev = container_of(w, struct rtw89_dev,
						forbid_ba_work.work);
	struct rtw89_txq *rtwtxq, *tmp;

	spin_lock_bh(&rtwdev->ba_lock);
	list_for_each_entry_safe(rtwtxq, tmp, &rtwdev->forbid_ba_list, list) {
		clear_bit(RTW89_TXQ_F_FORBID_BA, &rtwtxq->flags);
		list_del_init(&rtwtxq->list);
	}
	spin_unlock_bh(&rtwdev->ba_lock);
}

static void rtw89_core_sta_pending_tx_iter(void *data,
					   struct ieee80211_sta *sta)
{
	struct rtw89_sta *rtwsta = sta_to_rtwsta(sta);
	struct rtw89_dev *rtwdev = rtwsta->rtwdev;
	struct rtw89_vif *rtwvif = rtwsta->rtwvif;
	struct ieee80211_vif *vif = rtwvif_to_vif(rtwvif);
	struct rtw89_vif_link *target = data;
	struct rtw89_vif_link *rtwvif_link;
	struct sk_buff *skb, *tmp;
	unsigned int link_id;
	int qsel, ret;

	rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id)
		if (rtwvif_link->chanctx_idx == target->chanctx_idx)
			goto bottom;

	return;

bottom:
	if (skb_queue_len(&rtwsta->roc_queue) == 0)
		return;

	skb_queue_walk_safe(&rtwsta->roc_queue, skb, tmp) {
		skb_unlink(skb, &rtwsta->roc_queue);

		ret = rtw89_core_tx_write(rtwdev, vif, sta, skb, &qsel);
		if (ret) {
			rtw89_warn(rtwdev, "pending tx failed with %d\n", ret);
			dev_kfree_skb_any(skb);
		} else {
			rtw89_core_tx_kick_off(rtwdev, qsel);
		}
	}
}

static void rtw89_core_handle_sta_pending_tx(struct rtw89_dev *rtwdev,
					     struct rtw89_vif_link *rtwvif_link)
{
	ieee80211_iterate_stations_atomic(rtwdev->hw,
					  rtw89_core_sta_pending_tx_iter,
					  rtwvif_link);
}

static int rtw89_core_send_nullfunc(struct rtw89_dev *rtwdev,
				    struct rtw89_vif_link *rtwvif_link, bool qos, bool ps)
{
	struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	struct ieee80211_sta *sta;
	struct ieee80211_hdr *hdr;
	struct sk_buff *skb;
	int ret, qsel;

	if (vif->type != NL80211_IFTYPE_STATION || !vif->cfg.assoc)
		return 0;

	rcu_read_lock();
	sta = ieee80211_find_sta(vif, vif->cfg.ap_addr);
	if (!sta) {
		ret = -EINVAL;
		goto out;
	}

	skb = ieee80211_nullfunc_get(rtwdev->hw, vif, -1, qos);
	if (!skb) {
		ret = -ENOMEM;
		goto out;
	}

	hdr = (struct ieee80211_hdr *)skb->data;
	if (ps)
		hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PM);

	ret = rtw89_core_tx_write(rtwdev, vif, sta, skb, &qsel);
	if (ret) {
		rtw89_warn(rtwdev, "nullfunc transmit failed: %d\n", ret);
		dev_kfree_skb_any(skb);
		goto out;
	}

	rcu_read_unlock();

	return rtw89_core_tx_kick_off_and_wait(rtwdev, skb, qsel,
					       RTW89_ROC_TX_TIMEOUT);
out:
	rcu_read_unlock();

	return ret;
}

void rtw89_roc_start(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	struct ieee80211_hw *hw = rtwdev->hw;
	struct rtw89_roc *roc = &rtwvif->roc;
	struct rtw89_vif_link *rtwvif_link;
	struct cfg80211_chan_def roc_chan;
	struct rtw89_vif *tmp_vif;
	int ret;

	lockdep_assert_held(&rtwdev->mutex);

	rtw89_leave_ips_by_hwflags(rtwdev);
	rtw89_leave_lps(rtwdev);

	rtwvif_link = rtw89_vif_get_link_inst(rtwvif, 0);
	if (unlikely(!rtwvif_link)) {
		rtw89_err(rtwdev, "roc start: find no link on HW-0\n");
		return;
	}

	rtw89_chanctx_pause(rtwdev, RTW89_CHANCTX_PAUSE_REASON_ROC);

	ret = rtw89_core_send_nullfunc(rtwdev, rtwvif_link, true, true);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_TXRX,
			    "roc send null-1 failed: %d\n", ret);

	rtw89_for_each_rtwvif(rtwdev, tmp_vif) {
		struct rtw89_vif_link *tmp_link;
		unsigned int link_id;

		rtw89_vif_for_each_link(tmp_vif, tmp_link, link_id) {
			if (tmp_link->chanctx_idx == rtwvif_link->chanctx_idx) {
				tmp_vif->offchan = true;
				break;
			}
		}
	}

	cfg80211_chandef_create(&roc_chan, &roc->chan, NL80211_CHAN_NO_HT);
	rtw89_config_roc_chandef(rtwdev, rtwvif_link->chanctx_idx, &roc_chan);
	rtw89_set_channel(rtwdev);
	rtw89_write32_clr(rtwdev,
			  rtw89_mac_reg_by_idx(rtwdev, mac->rx_fltr, RTW89_MAC_0),
			  B_AX_A_UC_CAM_MATCH | B_AX_A_BC_CAM_MATCH);

	ieee80211_ready_on_channel(hw);
	cancel_delayed_work(&rtwvif->roc.roc_work);
	ieee80211_queue_delayed_work(hw, &rtwvif->roc.roc_work,
				     msecs_to_jiffies(rtwvif->roc.duration));
}

void rtw89_roc_end(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	struct ieee80211_hw *hw = rtwdev->hw;
	struct rtw89_roc *roc = &rtwvif->roc;
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_vif *tmp_vif;
	int ret;

	lockdep_assert_held(&rtwdev->mutex);

	ieee80211_remain_on_channel_expired(hw);

	rtw89_leave_ips_by_hwflags(rtwdev);
	rtw89_leave_lps(rtwdev);

	rtwvif_link = rtw89_vif_get_link_inst(rtwvif, 0);
	if (unlikely(!rtwvif_link)) {
		rtw89_err(rtwdev, "roc end: find no link on HW-0\n");
		return;
	}

	rtw89_write32_mask(rtwdev,
			   rtw89_mac_reg_by_idx(rtwdev, mac->rx_fltr, RTW89_MAC_0),
			   B_AX_RX_FLTR_CFG_MASK,
			   rtwdev->hal.rx_fltr);

	roc->state = RTW89_ROC_IDLE;
	rtw89_config_roc_chandef(rtwdev, rtwvif_link->chanctx_idx, NULL);
	rtw89_chanctx_proceed(rtwdev);
	ret = rtw89_core_send_nullfunc(rtwdev, rtwvif_link, true, false);
	if (ret)
		rtw89_debug(rtwdev, RTW89_DBG_TXRX,
			    "roc send null-0 failed: %d\n", ret);

	rtw89_for_each_rtwvif(rtwdev, tmp_vif)
		tmp_vif->offchan = false;

	rtw89_core_handle_sta_pending_tx(rtwdev, rtwvif_link);
	queue_work(rtwdev->txq_wq, &rtwdev->txq_work);

	if (hw->conf.flags & IEEE80211_CONF_IDLE)
		ieee80211_queue_delayed_work(hw, &roc->roc_work,
					     msecs_to_jiffies(RTW89_ROC_IDLE_TIMEOUT));
}

void rtw89_roc_work(struct work_struct *work)
{
	struct rtw89_vif *rtwvif = container_of(work, struct rtw89_vif,
						roc.roc_work.work);
	struct rtw89_dev *rtwdev = rtwvif->rtwdev;
	struct rtw89_roc *roc = &rtwvif->roc;

	mutex_lock(&rtwdev->mutex);

	switch (roc->state) {
	case RTW89_ROC_IDLE:
		rtw89_enter_ips_by_hwflags(rtwdev);
		break;
	case RTW89_ROC_MGMT:
	case RTW89_ROC_NORMAL:
		rtw89_roc_end(rtwdev, rtwvif);
		break;
	default:
		break;
	}

	mutex_unlock(&rtwdev->mutex);
}

static enum rtw89_tfc_lv rtw89_get_traffic_level(struct rtw89_dev *rtwdev,
						 u32 throughput, u64 cnt)
{
	if (cnt < 100)
		return RTW89_TFC_IDLE;
	if (throughput > 50)
		return RTW89_TFC_HIGH;
	if (throughput > 10)
		return RTW89_TFC_MID;
	if (throughput > 2)
		return RTW89_TFC_LOW;
	return RTW89_TFC_ULTRA_LOW;
}

static bool rtw89_traffic_stats_calc(struct rtw89_dev *rtwdev,
				     struct rtw89_traffic_stats *stats)
{
	enum rtw89_tfc_lv tx_tfc_lv = stats->tx_tfc_lv;
	enum rtw89_tfc_lv rx_tfc_lv = stats->rx_tfc_lv;

	stats->tx_throughput_raw = (u32)(stats->tx_unicast >> RTW89_TP_SHIFT);
	stats->rx_throughput_raw = (u32)(stats->rx_unicast >> RTW89_TP_SHIFT);

	ewma_tp_add(&stats->tx_ewma_tp, stats->tx_throughput_raw);
	ewma_tp_add(&stats->rx_ewma_tp, stats->rx_throughput_raw);

	stats->tx_throughput = ewma_tp_read(&stats->tx_ewma_tp);
	stats->rx_throughput = ewma_tp_read(&stats->rx_ewma_tp);
	stats->tx_tfc_lv = rtw89_get_traffic_level(rtwdev, stats->tx_throughput,
						   stats->tx_cnt);
	stats->rx_tfc_lv = rtw89_get_traffic_level(rtwdev, stats->rx_throughput,
						   stats->rx_cnt);
	stats->tx_avg_len = stats->tx_cnt ?
			    DIV_ROUND_DOWN_ULL(stats->tx_unicast, stats->tx_cnt) : 0;
	stats->rx_avg_len = stats->rx_cnt ?
			    DIV_ROUND_DOWN_ULL(stats->rx_unicast, stats->rx_cnt) : 0;

	stats->tx_unicast = 0;
	stats->rx_unicast = 0;
	stats->tx_cnt = 0;
	stats->rx_cnt = 0;
	stats->rx_tf_periodic = stats->rx_tf_acc;
	stats->rx_tf_acc = 0;

	if (tx_tfc_lv != stats->tx_tfc_lv || rx_tfc_lv != stats->rx_tfc_lv)
		return true;

	return false;
}

static bool rtw89_traffic_stats_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_vif *rtwvif;
	unsigned int link_id;
	bool tfc_changed;

	tfc_changed = rtw89_traffic_stats_calc(rtwdev, &rtwdev->stats);

	rtw89_for_each_rtwvif(rtwdev, rtwvif) {
		rtw89_traffic_stats_calc(rtwdev, &rtwvif->stats);

		rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id)
			rtw89_fw_h2c_tp_offload(rtwdev, rtwvif_link);
	}

	return tfc_changed;
}

static void rtw89_vif_enter_lps(struct rtw89_dev *rtwdev,
				struct rtw89_vif_link *rtwvif_link)
{
	if (rtwvif_link->wifi_role != RTW89_WIFI_ROLE_STATION &&
	    rtwvif_link->wifi_role != RTW89_WIFI_ROLE_P2P_CLIENT)
		return;

	rtw89_enter_lps(rtwdev, rtwvif_link, true);
}

static void rtw89_enter_lps_track(struct rtw89_dev *rtwdev)
{
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_vif *rtwvif;
	unsigned int link_id;

	rtw89_for_each_rtwvif(rtwdev, rtwvif) {
		if (rtwvif->tdls_peer)
			continue;
		if (rtwvif->offchan)
			continue;

		if (rtwvif->stats.tx_tfc_lv != RTW89_TFC_IDLE ||
		    rtwvif->stats.rx_tfc_lv != RTW89_TFC_IDLE)
			continue;

		rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id)
			rtw89_vif_enter_lps(rtwdev, rtwvif_link);
	}
}

static void rtw89_core_rfk_track(struct rtw89_dev *rtwdev)
{
	enum rtw89_entity_mode mode;

	mode = rtw89_get_entity_mode(rtwdev);
	if (mode == RTW89_ENTITY_MODE_MCC)
		return;

	rtw89_chip_rfk_track(rtwdev);
}

void rtw89_core_update_p2p_ps(struct rtw89_dev *rtwdev,
			      struct rtw89_vif_link *rtwvif_link,
			      struct ieee80211_bss_conf *bss_conf)
{
	enum rtw89_entity_mode mode = rtw89_get_entity_mode(rtwdev);

	if (mode == RTW89_ENTITY_MODE_MCC)
		rtw89_queue_chanctx_change(rtwdev, RTW89_CHANCTX_P2P_PS_CHANGE);
	else
		rtw89_process_p2p_ps(rtwdev, rtwvif_link, bss_conf);
}

void rtw89_traffic_stats_init(struct rtw89_dev *rtwdev,
			      struct rtw89_traffic_stats *stats)
{
	stats->tx_unicast = 0;
	stats->rx_unicast = 0;
	stats->tx_cnt = 0;
	stats->rx_cnt = 0;
	ewma_tp_init(&stats->tx_ewma_tp);
	ewma_tp_init(&stats->rx_ewma_tp);
}

static void rtw89_track_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev = container_of(work, struct rtw89_dev,
						track_work.work);
	bool tfc_changed;

	if (test_bit(RTW89_FLAG_FORBIDDEN_TRACK_WROK, rtwdev->flags))
		return;

	mutex_lock(&rtwdev->mutex);

	if (!test_bit(RTW89_FLAG_RUNNING, rtwdev->flags))
		goto out;

	ieee80211_queue_delayed_work(rtwdev->hw, &rtwdev->track_work,
				     RTW89_TRACK_WORK_PERIOD);

	tfc_changed = rtw89_traffic_stats_track(rtwdev);
	if (rtwdev->scanning)
		goto out;

	rtw89_leave_lps(rtwdev);

	if (tfc_changed) {
		rtw89_hci_recalc_int_mit(rtwdev);
		rtw89_btc_ntfy_wl_sta(rtwdev);
	}
	rtw89_mac_bf_monitor_track(rtwdev);
	rtw89_phy_stat_track(rtwdev);
	rtw89_phy_env_monitor_track(rtwdev);
	rtw89_phy_dig(rtwdev);
	rtw89_core_rfk_track(rtwdev);
	rtw89_phy_ra_update(rtwdev);
	rtw89_phy_cfo_track(rtwdev);
	rtw89_phy_tx_path_div_track(rtwdev);
	rtw89_phy_antdiv_track(rtwdev);
	rtw89_phy_ul_tb_ctrl_track(rtwdev);
	rtw89_phy_edcca_track(rtwdev);
	rtw89_tas_track(rtwdev);
	rtw89_chanctx_track(rtwdev);
	rtw89_core_rfkill_poll(rtwdev, false);

	if (rtwdev->lps_enabled && !rtwdev->btc.lps)
		rtw89_enter_lps_track(rtwdev);

out:
	mutex_unlock(&rtwdev->mutex);
}

u8 rtw89_core_acquire_bit_map(unsigned long *addr, unsigned long size)
{
	unsigned long bit;

	bit = find_first_zero_bit(addr, size);
	if (bit < size)
		set_bit(bit, addr);

	return bit;
}

void rtw89_core_release_bit_map(unsigned long *addr, u8 bit)
{
	clear_bit(bit, addr);
}

void rtw89_core_release_all_bits_map(unsigned long *addr, unsigned int nbits)
{
	bitmap_zero(addr, nbits);
}

int rtw89_core_acquire_sta_ba_entry(struct rtw89_dev *rtwdev,
				    struct rtw89_sta_link *rtwsta_link, u8 tid,
				    u8 *cam_idx)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_cam_info *cam_info = &rtwdev->cam_info;
	struct rtw89_ba_cam_entry *entry = NULL, *tmp;
	u8 idx;
	int i;

	lockdep_assert_held(&rtwdev->mutex);

	idx = rtw89_core_acquire_bit_map(cam_info->ba_cam_map, chip->bacam_num);
	if (idx == chip->bacam_num) {
		/* allocate a static BA CAM to tid=0/5, so replace the existing
		 * one if BA CAM is full. Hardware will process the original tid
		 * automatically.
		 */
		if (tid != 0 && tid != 5)
			return -ENOSPC;

		for_each_set_bit(i, cam_info->ba_cam_map, chip->bacam_num) {
			tmp = &cam_info->ba_cam_entry[i];
			if (tmp->tid == 0 || tmp->tid == 5)
				continue;

			idx = i;
			entry = tmp;
			list_del(&entry->list);
			break;
		}

		if (!entry)
			return -ENOSPC;
	} else {
		entry = &cam_info->ba_cam_entry[idx];
	}

	entry->tid = tid;
	list_add_tail(&entry->list, &rtwsta_link->ba_cam_list);

	*cam_idx = idx;

	return 0;
}

int rtw89_core_release_sta_ba_entry(struct rtw89_dev *rtwdev,
				    struct rtw89_sta_link *rtwsta_link, u8 tid,
				    u8 *cam_idx)
{
	struct rtw89_cam_info *cam_info = &rtwdev->cam_info;
	struct rtw89_ba_cam_entry *entry = NULL, *tmp;
	u8 idx;

	lockdep_assert_held(&rtwdev->mutex);

	list_for_each_entry_safe(entry, tmp, &rtwsta_link->ba_cam_list, list) {
		if (entry->tid != tid)
			continue;

		idx = entry - cam_info->ba_cam_entry;
		list_del(&entry->list);

		rtw89_core_release_bit_map(cam_info->ba_cam_map, idx);
		*cam_idx = idx;
		return 0;
	}

	return -ENOENT;
}

#define RTW89_TYPE_MAPPING(_type)	\
	case NL80211_IFTYPE_ ## _type:	\
		rtwvif_link->wifi_role = RTW89_WIFI_ROLE_ ## _type;	\
		break
void rtw89_vif_type_mapping(struct rtw89_vif_link *rtwvif_link, bool assoc)
{
	const struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	const struct ieee80211_bss_conf *bss_conf;

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		if (vif->p2p)
			rtwvif_link->wifi_role = RTW89_WIFI_ROLE_P2P_CLIENT;
		else
			rtwvif_link->wifi_role = RTW89_WIFI_ROLE_STATION;
		break;
	case NL80211_IFTYPE_AP:
		if (vif->p2p)
			rtwvif_link->wifi_role = RTW89_WIFI_ROLE_P2P_GO;
		else
			rtwvif_link->wifi_role = RTW89_WIFI_ROLE_AP;
		break;
	RTW89_TYPE_MAPPING(ADHOC);
	RTW89_TYPE_MAPPING(MONITOR);
	RTW89_TYPE_MAPPING(MESH_POINT);
	default:
		WARN_ON(1);
		break;
	}

	switch (vif->type) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
		rtwvif_link->net_type = RTW89_NET_TYPE_AP_MODE;
		rtwvif_link->self_role = RTW89_SELF_ROLE_AP;
		break;
	case NL80211_IFTYPE_ADHOC:
		rtwvif_link->net_type = RTW89_NET_TYPE_AD_HOC;
		rtwvif_link->self_role = RTW89_SELF_ROLE_CLIENT;
		break;
	case NL80211_IFTYPE_STATION:
		if (assoc) {
			rtwvif_link->net_type = RTW89_NET_TYPE_INFRA;

			rcu_read_lock();
			bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, false);
			rtwvif_link->trigger = bss_conf->he_support;
			rcu_read_unlock();
		} else {
			rtwvif_link->net_type = RTW89_NET_TYPE_NO_LINK;
			rtwvif_link->trigger = false;
		}
		rtwvif_link->self_role = RTW89_SELF_ROLE_CLIENT;
		rtwvif_link->addr_cam.sec_ent_mode = RTW89_ADDR_CAM_SEC_NORMAL;
		break;
	case NL80211_IFTYPE_MONITOR:
		break;
	default:
		WARN_ON(1);
		break;
	}
}

int rtw89_core_sta_link_add(struct rtw89_dev *rtwdev,
			    struct rtw89_vif_link *rtwvif_link,
			    struct rtw89_sta_link *rtwsta_link)
{
	const struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	const struct ieee80211_sta *sta = rtwsta_link_to_sta(rtwsta_link);
	struct rtw89_hal *hal = &rtwdev->hal;
	u8 ant_num = hal->ant_diversity ? 2 : rtwdev->chip->rf_path_num;
	int i;
	int ret;

	rtwsta_link->prev_rssi = 0;
	INIT_LIST_HEAD(&rtwsta_link->ba_cam_list);
	ewma_rssi_init(&rtwsta_link->avg_rssi);
	ewma_snr_init(&rtwsta_link->avg_snr);
	ewma_evm_init(&rtwsta_link->evm_1ss);
	for (i = 0; i < ant_num; i++) {
		ewma_rssi_init(&rtwsta_link->rssi[i]);
		ewma_evm_init(&rtwsta_link->evm_min[i]);
		ewma_evm_init(&rtwsta_link->evm_max[i]);
	}

	if (vif->type == NL80211_IFTYPE_STATION && !sta->tdls) {
		/* must do rtw89_reg_6ghz_recalc() before rfk channel */
		ret = rtw89_reg_6ghz_recalc(rtwdev, rtwvif_link, true);
		if (ret)
			return ret;

		rtw89_btc_ntfy_role_info(rtwdev, rtwvif_link, rtwsta_link,
					 BTC_ROLE_MSTS_STA_CONN_START);
		rtw89_chip_rfk_channel(rtwdev, rtwvif_link);
	} else if (vif->type == NL80211_IFTYPE_AP || sta->tdls) {
		ret = rtw89_mac_set_macid_pause(rtwdev, rtwsta_link->mac_id, false);
		if (ret) {
			rtw89_warn(rtwdev, "failed to send h2c macid pause\n");
			return ret;
		}

		ret = rtw89_fw_h2c_role_maintain(rtwdev, rtwvif_link, rtwsta_link,
						 RTW89_ROLE_CREATE);
		if (ret) {
			rtw89_warn(rtwdev, "failed to send h2c role info\n");
			return ret;
		}

		ret = rtw89_chip_h2c_default_cmac_tbl(rtwdev, rtwvif_link, rtwsta_link);
		if (ret)
			return ret;

		ret = rtw89_chip_h2c_default_dmac_tbl(rtwdev, rtwvif_link, rtwsta_link);
		if (ret)
			return ret;
	}

	return 0;
}

int rtw89_core_sta_link_disassoc(struct rtw89_dev *rtwdev,
				 struct rtw89_vif_link *rtwvif_link,
				 struct rtw89_sta_link *rtwsta_link)
{
	const struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);

	if (vif->type == NL80211_IFTYPE_STATION)
		rtw89_fw_h2c_set_bcn_fltr_cfg(rtwdev, rtwvif_link, false);

	return 0;
}

int rtw89_core_sta_link_disconnect(struct rtw89_dev *rtwdev,
				   struct rtw89_vif_link *rtwvif_link,
				   struct rtw89_sta_link *rtwsta_link)
{
	const struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	const struct ieee80211_sta *sta = rtwsta_link_to_sta(rtwsta_link);
	int ret;

	rtw89_mac_bf_monitor_calc(rtwdev, rtwsta_link, true);
	rtw89_mac_bf_disassoc(rtwdev, rtwvif_link, rtwsta_link);

	if (vif->type == NL80211_IFTYPE_AP || sta->tdls)
		rtw89_cam_deinit_addr_cam(rtwdev, &rtwsta_link->addr_cam);
	if (sta->tdls)
		rtw89_cam_deinit_bssid_cam(rtwdev, &rtwsta_link->bssid_cam);

	if (vif->type == NL80211_IFTYPE_STATION && !sta->tdls) {
		rtw89_vif_type_mapping(rtwvif_link, false);
		rtw89_fw_release_general_pkt_list_vif(rtwdev, rtwvif_link, true);
	}

	ret = rtw89_chip_h2c_assoc_cmac_tbl(rtwdev, rtwvif_link, rtwsta_link);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c cmac table\n");
		return ret;
	}

	ret = rtw89_fw_h2c_join_info(rtwdev, rtwvif_link, rtwsta_link, true);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c join info\n");
		return ret;
	}

	/* update cam aid mac_id net_type */
	ret = rtw89_fw_h2c_cam(rtwdev, rtwvif_link, rtwsta_link, NULL);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c cam\n");
		return ret;
	}

	return ret;
}

int rtw89_core_sta_link_assoc(struct rtw89_dev *rtwdev,
			      struct rtw89_vif_link *rtwvif_link,
			      struct rtw89_sta_link *rtwsta_link)
{
	const struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	const struct ieee80211_sta *sta = rtwsta_link_to_sta(rtwsta_link);
	struct rtw89_bssid_cam_entry *bssid_cam = rtw89_get_bssid_cam_of(rtwvif_link,
									 rtwsta_link);
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev,
						       rtwvif_link->chanctx_idx);
	int ret;

	if (vif->type == NL80211_IFTYPE_AP || sta->tdls) {
		if (sta->tdls) {
			struct ieee80211_link_sta *link_sta;

			rcu_read_lock();

			link_sta = rtw89_sta_rcu_dereference_link(rtwsta_link, true);
			ret = rtw89_cam_init_bssid_cam(rtwdev, rtwvif_link, bssid_cam,
						       link_sta->addr);
			if (ret) {
				rtw89_warn(rtwdev, "failed to send h2c init bssid cam for TDLS\n");
				rcu_read_unlock();
				return ret;
			}

			rcu_read_unlock();
		}

		ret = rtw89_cam_init_addr_cam(rtwdev, &rtwsta_link->addr_cam, bssid_cam);
		if (ret) {
			rtw89_warn(rtwdev, "failed to send h2c init addr cam\n");
			return ret;
		}
	}

	ret = rtw89_chip_h2c_assoc_cmac_tbl(rtwdev, rtwvif_link, rtwsta_link);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c cmac table\n");
		return ret;
	}

	ret = rtw89_fw_h2c_join_info(rtwdev, rtwvif_link, rtwsta_link, false);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c join info\n");
		return ret;
	}

	/* update cam aid mac_id net_type */
	ret = rtw89_fw_h2c_cam(rtwdev, rtwvif_link, rtwsta_link, NULL);
	if (ret) {
		rtw89_warn(rtwdev, "failed to send h2c cam\n");
		return ret;
	}

	rtw89_phy_ra_assoc(rtwdev, rtwsta_link);
	rtw89_mac_bf_assoc(rtwdev, rtwvif_link, rtwsta_link);
	rtw89_mac_bf_monitor_calc(rtwdev, rtwsta_link, false);

	if (vif->type == NL80211_IFTYPE_STATION && !sta->tdls) {
		struct ieee80211_bss_conf *bss_conf;

		rcu_read_lock();

		bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, true);
		if (bss_conf->he_support &&
		    !(bss_conf->he_oper.params & IEEE80211_HE_OPERATION_ER_SU_DISABLE))
			rtwsta_link->er_cap = true;

		rcu_read_unlock();

		rtw89_btc_ntfy_role_info(rtwdev, rtwvif_link, rtwsta_link,
					 BTC_ROLE_MSTS_STA_CONN_END);
		rtw89_core_get_no_ul_ofdma_htc(rtwdev, &rtwsta_link->htc_template, chan);
		rtw89_phy_ul_tb_assoc(rtwdev, rtwvif_link);

		ret = rtw89_fw_h2c_general_pkt(rtwdev, rtwvif_link, rtwsta_link->mac_id);
		if (ret) {
			rtw89_warn(rtwdev, "failed to send h2c general packet\n");
			return ret;
		}

		rtw89_fw_h2c_set_bcn_fltr_cfg(rtwdev, rtwvif_link, true);
	}

	return ret;
}

int rtw89_core_sta_link_remove(struct rtw89_dev *rtwdev,
			       struct rtw89_vif_link *rtwvif_link,
			       struct rtw89_sta_link *rtwsta_link)
{
	const struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	const struct ieee80211_sta *sta = rtwsta_link_to_sta(rtwsta_link);
	int ret;

	if (vif->type == NL80211_IFTYPE_STATION && !sta->tdls) {
		rtw89_reg_6ghz_recalc(rtwdev, rtwvif_link, false);
		rtw89_btc_ntfy_role_info(rtwdev, rtwvif_link, rtwsta_link,
					 BTC_ROLE_MSTS_STA_DIS_CONN);
	} else if (vif->type == NL80211_IFTYPE_AP || sta->tdls) {
		ret = rtw89_fw_h2c_role_maintain(rtwdev, rtwvif_link, rtwsta_link,
						 RTW89_ROLE_REMOVE);
		if (ret) {
			rtw89_warn(rtwdev, "failed to send h2c role info\n");
			return ret;
		}
	}

	return 0;
}

static void _rtw89_core_set_tid_config(struct rtw89_dev *rtwdev,
				       struct ieee80211_sta *sta,
				       struct cfg80211_tid_cfg *tid_conf)
{
	struct ieee80211_txq *txq;
	struct rtw89_txq *rtwtxq;
	u32 mask = tid_conf->mask;
	u8 tids = tid_conf->tids;
	int tids_nbit = BITS_PER_BYTE;
	int i;

	for (i = 0; i < tids_nbit; i++, tids >>= 1) {
		if (!tids)
			break;

		if (!(tids & BIT(0)))
			continue;

		txq = sta->txq[i];
		rtwtxq = (struct rtw89_txq *)txq->drv_priv;

		if (mask & BIT(NL80211_TID_CONFIG_ATTR_AMPDU_CTRL)) {
			if (tid_conf->ampdu == NL80211_TID_CONFIG_ENABLE) {
				clear_bit(RTW89_TXQ_F_FORBID_BA, &rtwtxq->flags);
			} else {
				if (test_bit(RTW89_TXQ_F_AMPDU, &rtwtxq->flags))
					ieee80211_stop_tx_ba_session(sta, txq->tid);
				spin_lock_bh(&rtwdev->ba_lock);
				list_del_init(&rtwtxq->list);
				set_bit(RTW89_TXQ_F_FORBID_BA, &rtwtxq->flags);
				spin_unlock_bh(&rtwdev->ba_lock);
			}
		}

		if (mask & BIT(NL80211_TID_CONFIG_ATTR_AMSDU_CTRL) && tids == 0xff) {
			if (tid_conf->amsdu == NL80211_TID_CONFIG_ENABLE)
				sta->max_amsdu_subframes = 0;
			else
				sta->max_amsdu_subframes = 1;
		}
	}
}

void rtw89_core_set_tid_config(struct rtw89_dev *rtwdev,
			       struct ieee80211_sta *sta,
			       struct cfg80211_tid_config *tid_config)
{
	int i;

	for (i = 0; i < tid_config->n_tid_conf; i++)
		_rtw89_core_set_tid_config(rtwdev, sta,
					   &tid_config->tid_conf[i]);
}

static void rtw89_init_ht_cap(struct rtw89_dev *rtwdev,
			      struct ieee80211_sta_ht_cap *ht_cap)
{
	static const __le16 highest[RF_PATH_MAX] = {
		cpu_to_le16(150), cpu_to_le16(300), cpu_to_le16(450), cpu_to_le16(600),
	};
	struct rtw89_hal *hal = &rtwdev->hal;
	u8 nss = hal->rx_nss;
	int i;

	ht_cap->ht_supported = true;
	ht_cap->cap = 0;
	ht_cap->cap |= IEEE80211_HT_CAP_SGI_20 |
		       IEEE80211_HT_CAP_MAX_AMSDU |
		       IEEE80211_HT_CAP_TX_STBC |
		       (1 << IEEE80211_HT_CAP_RX_STBC_SHIFT);
	ht_cap->cap |= IEEE80211_HT_CAP_LDPC_CODING;
	ht_cap->cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
		       IEEE80211_HT_CAP_DSSSCCK40 |
		       IEEE80211_HT_CAP_SGI_40;
	ht_cap->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
	ht_cap->ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE;
	ht_cap->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
	for (i = 0; i < nss; i++)
		ht_cap->mcs.rx_mask[i] = 0xFF;
	ht_cap->mcs.rx_mask[4] = 0x01;
	ht_cap->mcs.rx_highest = highest[nss - 1];
}

static void rtw89_init_vht_cap(struct rtw89_dev *rtwdev,
			       struct ieee80211_sta_vht_cap *vht_cap)
{
	static const __le16 highest_bw80[RF_PATH_MAX] = {
		cpu_to_le16(433), cpu_to_le16(867), cpu_to_le16(1300), cpu_to_le16(1733),
	};
	static const __le16 highest_bw160[RF_PATH_MAX] = {
		cpu_to_le16(867), cpu_to_le16(1733), cpu_to_le16(2600), cpu_to_le16(3467),
	};
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const __le16 *highest = chip->support_bandwidths & BIT(NL80211_CHAN_WIDTH_160) ?
				highest_bw160 : highest_bw80;
	struct rtw89_hal *hal = &rtwdev->hal;
	u16 tx_mcs_map = 0, rx_mcs_map = 0;
	u8 sts_cap = 3;
	int i;

	for (i = 0; i < 8; i++) {
		if (i < hal->tx_nss)
			tx_mcs_map |= IEEE80211_VHT_MCS_SUPPORT_0_9 << (i * 2);
		else
			tx_mcs_map |= IEEE80211_VHT_MCS_NOT_SUPPORTED << (i * 2);
		if (i < hal->rx_nss)
			rx_mcs_map |= IEEE80211_VHT_MCS_SUPPORT_0_9 << (i * 2);
		else
			rx_mcs_map |= IEEE80211_VHT_MCS_NOT_SUPPORTED << (i * 2);
	}

	vht_cap->vht_supported = true;
	vht_cap->cap = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454 |
		       IEEE80211_VHT_CAP_SHORT_GI_80 |
		       IEEE80211_VHT_CAP_RXSTBC_1 |
		       IEEE80211_VHT_CAP_HTC_VHT |
		       IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK |
		       0;
	vht_cap->cap |= IEEE80211_VHT_CAP_TXSTBC;
	vht_cap->cap |= IEEE80211_VHT_CAP_RXLDPC;
	vht_cap->cap |= IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE |
			IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE;
	vht_cap->cap |= sts_cap << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT;
	if (chip->support_bandwidths & BIT(NL80211_CHAN_WIDTH_160))
		vht_cap->cap |= IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ |
				IEEE80211_VHT_CAP_SHORT_GI_160;
	vht_cap->vht_mcs.rx_mcs_map = cpu_to_le16(rx_mcs_map);
	vht_cap->vht_mcs.tx_mcs_map = cpu_to_le16(tx_mcs_map);
	vht_cap->vht_mcs.rx_highest = highest[hal->rx_nss - 1];
	vht_cap->vht_mcs.tx_highest = highest[hal->tx_nss - 1];

	if (ieee80211_hw_check(rtwdev->hw, SUPPORTS_VHT_EXT_NSS_BW))
		vht_cap->vht_mcs.tx_highest |=
			cpu_to_le16(IEEE80211_VHT_EXT_NSS_BW_CAPABLE);
}

static void rtw89_init_he_cap(struct rtw89_dev *rtwdev,
			      enum nl80211_band band,
			      enum nl80211_iftype iftype,
			      struct ieee80211_sband_iftype_data *iftype_data)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct rtw89_hal *hal = &rtwdev->hal;
	bool no_ng16 = (chip->chip_id == RTL8852A && hal->cv == CHIP_CBV) ||
		       (chip->chip_id == RTL8852B && hal->cv == CHIP_CAV);
	struct ieee80211_sta_he_cap *he_cap;
	int nss = hal->rx_nss;
	u8 *mac_cap_info;
	u8 *phy_cap_info;
	u16 mcs_map = 0;
	int i;

	for (i = 0; i < 8; i++) {
		if (i < nss)
			mcs_map |= IEEE80211_HE_MCS_SUPPORT_0_11 << (i * 2);
		else
			mcs_map |= IEEE80211_HE_MCS_NOT_SUPPORTED << (i * 2);
	}

	he_cap = &iftype_data->he_cap;
	mac_cap_info = he_cap->he_cap_elem.mac_cap_info;
	phy_cap_info = he_cap->he_cap_elem.phy_cap_info;

	he_cap->has_he = true;
	mac_cap_info[0] = IEEE80211_HE_MAC_CAP0_HTC_HE;
	if (iftype == NL80211_IFTYPE_STATION)
		mac_cap_info[1] = IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US;
	mac_cap_info[2] = IEEE80211_HE_MAC_CAP2_ALL_ACK |
			  IEEE80211_HE_MAC_CAP2_BSR;
	mac_cap_info[3] = IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_2;
	if (iftype == NL80211_IFTYPE_AP)
		mac_cap_info[3] |= IEEE80211_HE_MAC_CAP3_OMI_CONTROL;
	mac_cap_info[4] = IEEE80211_HE_MAC_CAP4_OPS |
			  IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU;
	if (iftype == NL80211_IFTYPE_STATION)
		mac_cap_info[5] = IEEE80211_HE_MAC_CAP5_HT_VHT_TRIG_FRAME_RX;
	if (band == NL80211_BAND_2GHZ) {
		phy_cap_info[0] =
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
	} else {
		phy_cap_info[0] =
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G;
		if (chip->support_bandwidths & BIT(NL80211_CHAN_WIDTH_160))
			phy_cap_info[0] |= IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G;
	}
	phy_cap_info[1] = IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
			  IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD |
			  IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US;
	phy_cap_info[2] = IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
			  IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ |
			  IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ |
			  IEEE80211_HE_PHY_CAP2_DOPPLER_TX;
	phy_cap_info[3] = IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_16_QAM;
	if (iftype == NL80211_IFTYPE_STATION)
		phy_cap_info[3] |= IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_16_QAM |
				   IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_2;
	if (iftype == NL80211_IFTYPE_AP)
		phy_cap_info[3] |= IEEE80211_HE_PHY_CAP3_RX_PARTIAL_BW_SU_IN_20MHZ_MU;
	phy_cap_info[4] = IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE |
			  IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_4;
	if (chip->support_bandwidths & BIT(NL80211_CHAN_WIDTH_160))
		phy_cap_info[4] |= IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_4;
	phy_cap_info[5] = no_ng16 ? 0 :
			  IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK |
			  IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK;
	phy_cap_info[6] = IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU |
			  IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU |
			  IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB |
			  IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE;
	phy_cap_info[7] = IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_SUPP |
			  IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI |
			  IEEE80211_HE_PHY_CAP7_MAX_NC_1;
	phy_cap_info[8] = IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI |
			  IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI |
			  IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_996;
	if (chip->support_bandwidths & BIT(NL80211_CHAN_WIDTH_160))
		phy_cap_info[8] |= IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU |
				   IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU;
	phy_cap_info[9] = IEEE80211_HE_PHY_CAP9_LONGER_THAN_16_SIGB_OFDM_SYM |
			  IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU |
			  IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
			  IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB |
			  u8_encode_bits(IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_16US,
					 IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_MASK);
	if (iftype == NL80211_IFTYPE_STATION)
		phy_cap_info[9] |= IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU;
	he_cap->he_mcs_nss_supp.rx_mcs_80 = cpu_to_le16(mcs_map);
	he_cap->he_mcs_nss_supp.tx_mcs_80 = cpu_to_le16(mcs_map);
	if (chip->support_bandwidths & BIT(NL80211_CHAN_WIDTH_160)) {
		he_cap->he_mcs_nss_supp.rx_mcs_160 = cpu_to_le16(mcs_map);
		he_cap->he_mcs_nss_supp.tx_mcs_160 = cpu_to_le16(mcs_map);
	}

	if (band == NL80211_BAND_6GHZ) {
		__le16 capa;

		capa = le16_encode_bits(IEEE80211_HT_MPDU_DENSITY_NONE,
					IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START) |
		       le16_encode_bits(IEEE80211_VHT_MAX_AMPDU_1024K,
					IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP) |
		       le16_encode_bits(IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454,
					IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN);
		iftype_data->he_6ghz_capa.capa = capa;
	}
}

static void rtw89_init_eht_cap(struct rtw89_dev *rtwdev,
			       enum nl80211_band band,
			       enum nl80211_iftype iftype,
			       struct ieee80211_sband_iftype_data *iftype_data)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct ieee80211_eht_cap_elem_fixed *eht_cap_elem;
	struct ieee80211_eht_mcs_nss_supp *eht_nss;
	struct ieee80211_sta_eht_cap *eht_cap;
	struct rtw89_hal *hal = &rtwdev->hal;
	bool support_320mhz = false;
	int sts = 8;
	u8 val;

	if (chip->chip_gen == RTW89_CHIP_AX)
		return;

	if (band == NL80211_BAND_6GHZ &&
	    chip->support_bandwidths & BIT(NL80211_CHAN_WIDTH_320))
		support_320mhz = true;

	eht_cap = &iftype_data->eht_cap;
	eht_cap_elem = &eht_cap->eht_cap_elem;
	eht_nss = &eht_cap->eht_mcs_nss_supp;

	eht_cap->has_eht = true;

	eht_cap_elem->mac_cap_info[0] =
		u8_encode_bits(IEEE80211_EHT_MAC_CAP0_MAX_MPDU_LEN_7991,
			       IEEE80211_EHT_MAC_CAP0_MAX_MPDU_LEN_MASK);
	eht_cap_elem->mac_cap_info[1] = 0;

	eht_cap_elem->phy_cap_info[0] =
		IEEE80211_EHT_PHY_CAP0_NDP_4_EHT_LFT_32_GI |
		IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMEE;
	if (support_320mhz)
		eht_cap_elem->phy_cap_info[0] |=
			IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ;

	eht_cap_elem->phy_cap_info[0] |=
		u8_encode_bits(u8_get_bits(sts - 1, BIT(0)),
			       IEEE80211_EHT_PHY_CAP0_BEAMFORMEE_SS_80MHZ_MASK);
	eht_cap_elem->phy_cap_info[1] =
		u8_encode_bits(u8_get_bits(sts - 1, GENMASK(2, 1)),
			       IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_80MHZ_MASK) |
		u8_encode_bits(sts - 1,
			       IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_160MHZ_MASK);
	if (support_320mhz)
		eht_cap_elem->phy_cap_info[1] |=
			u8_encode_bits(sts - 1,
				       IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_320MHZ_MASK);

	eht_cap_elem->phy_cap_info[2] = 0;

	eht_cap_elem->phy_cap_info[3] =
		IEEE80211_EHT_PHY_CAP3_CODEBOOK_4_2_SU_FDBK |
		IEEE80211_EHT_PHY_CAP3_CODEBOOK_7_5_MU_FDBK |
		IEEE80211_EHT_PHY_CAP3_TRIG_SU_BF_FDBK |
		IEEE80211_EHT_PHY_CAP3_TRIG_MU_BF_PART_BW_FDBK;

	eht_cap_elem->phy_cap_info[4] =
		IEEE80211_EHT_PHY_CAP4_POWER_BOOST_FACT_SUPP |
		u8_encode_bits(1, IEEE80211_EHT_PHY_CAP4_MAX_NC_MASK);

	eht_cap_elem->phy_cap_info[5] =
		u8_encode_bits(IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_20US,
			       IEEE80211_EHT_PHY_CAP5_COMMON_NOMINAL_PKT_PAD_MASK);

	eht_cap_elem->phy_cap_info[6] = 0;
	eht_cap_elem->phy_cap_info[7] = 0;
	eht_cap_elem->phy_cap_info[8] = 0;

	val = u8_encode_bits(hal->rx_nss, IEEE80211_EHT_MCS_NSS_RX) |
	      u8_encode_bits(hal->tx_nss, IEEE80211_EHT_MCS_NSS_TX);
	eht_nss->bw._80.rx_tx_mcs9_max_nss = val;
	eht_nss->bw._80.rx_tx_mcs11_max_nss = val;
	eht_nss->bw._80.rx_tx_mcs13_max_nss = val;
	eht_nss->bw._160.rx_tx_mcs9_max_nss = val;
	eht_nss->bw._160.rx_tx_mcs11_max_nss = val;
	eht_nss->bw._160.rx_tx_mcs13_max_nss = val;
	if (support_320mhz) {
		eht_nss->bw._320.rx_tx_mcs9_max_nss = val;
		eht_nss->bw._320.rx_tx_mcs11_max_nss = val;
		eht_nss->bw._320.rx_tx_mcs13_max_nss = val;
	}
}

#define RTW89_SBAND_IFTYPES_NR 2

static void rtw89_init_he_eht_cap(struct rtw89_dev *rtwdev,
				  enum nl80211_band band,
				  struct ieee80211_supported_band *sband)
{
	struct ieee80211_sband_iftype_data *iftype_data;
	enum nl80211_iftype iftype;
	int idx = 0;

	iftype_data = kcalloc(RTW89_SBAND_IFTYPES_NR, sizeof(*iftype_data), GFP_KERNEL);
	if (!iftype_data)
		return;

	for (iftype = 0; iftype < NUM_NL80211_IFTYPES; iftype++) {
		switch (iftype) {
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_AP:
			break;
		default:
			continue;
		}

		if (idx >= RTW89_SBAND_IFTYPES_NR) {
			rtw89_warn(rtwdev, "run out of iftype_data\n");
			break;
		}

		iftype_data[idx].types_mask = BIT(iftype);

		rtw89_init_he_cap(rtwdev, band, iftype, &iftype_data[idx]);
		rtw89_init_eht_cap(rtwdev, band, iftype, &iftype_data[idx]);

		idx++;
	}

	_ieee80211_set_sband_iftype_data(sband, iftype_data, idx);
}

static int rtw89_core_set_supported_band(struct rtw89_dev *rtwdev)
{
	struct ieee80211_hw *hw = rtwdev->hw;
	struct ieee80211_supported_band *sband_2ghz = NULL, *sband_5ghz = NULL;
	struct ieee80211_supported_band *sband_6ghz = NULL;
	u32 size = sizeof(struct ieee80211_supported_band);
	u8 support_bands = rtwdev->chip->support_bands;

	if (support_bands & BIT(NL80211_BAND_2GHZ)) {
		sband_2ghz = kmemdup(&rtw89_sband_2ghz, size, GFP_KERNEL);
		if (!sband_2ghz)
			goto err;
		rtw89_init_ht_cap(rtwdev, &sband_2ghz->ht_cap);
		rtw89_init_he_eht_cap(rtwdev, NL80211_BAND_2GHZ, sband_2ghz);
		hw->wiphy->bands[NL80211_BAND_2GHZ] = sband_2ghz;
	}

	if (support_bands & BIT(NL80211_BAND_5GHZ)) {
		sband_5ghz = kmemdup(&rtw89_sband_5ghz, size, GFP_KERNEL);
		if (!sband_5ghz)
			goto err;
		rtw89_init_ht_cap(rtwdev, &sband_5ghz->ht_cap);
		rtw89_init_vht_cap(rtwdev, &sband_5ghz->vht_cap);
		rtw89_init_he_eht_cap(rtwdev, NL80211_BAND_5GHZ, sband_5ghz);
		hw->wiphy->bands[NL80211_BAND_5GHZ] = sband_5ghz;
	}

	if (support_bands & BIT(NL80211_BAND_6GHZ)) {
		sband_6ghz = kmemdup(&rtw89_sband_6ghz, size, GFP_KERNEL);
		if (!sband_6ghz)
			goto err;
		rtw89_init_he_eht_cap(rtwdev, NL80211_BAND_6GHZ, sband_6ghz);
		hw->wiphy->bands[NL80211_BAND_6GHZ] = sband_6ghz;
	}

	return 0;

err:
	hw->wiphy->bands[NL80211_BAND_2GHZ] = NULL;
	hw->wiphy->bands[NL80211_BAND_5GHZ] = NULL;
	hw->wiphy->bands[NL80211_BAND_6GHZ] = NULL;
	if (sband_2ghz)
		kfree((__force void *)sband_2ghz->iftype_data);
	if (sband_5ghz)
		kfree((__force void *)sband_5ghz->iftype_data);
	if (sband_6ghz)
		kfree((__force void *)sband_6ghz->iftype_data);
	kfree(sband_2ghz);
	kfree(sband_5ghz);
	kfree(sband_6ghz);
	return -ENOMEM;
}

static void rtw89_core_clr_supported_band(struct rtw89_dev *rtwdev)
{
	struct ieee80211_hw *hw = rtwdev->hw;

	if (hw->wiphy->bands[NL80211_BAND_2GHZ])
		kfree((__force void *)hw->wiphy->bands[NL80211_BAND_2GHZ]->iftype_data);
	if (hw->wiphy->bands[NL80211_BAND_5GHZ])
		kfree((__force void *)hw->wiphy->bands[NL80211_BAND_5GHZ]->iftype_data);
	if (hw->wiphy->bands[NL80211_BAND_6GHZ])
		kfree((__force void *)hw->wiphy->bands[NL80211_BAND_6GHZ]->iftype_data);
	kfree(hw->wiphy->bands[NL80211_BAND_2GHZ]);
	kfree(hw->wiphy->bands[NL80211_BAND_5GHZ]);
	kfree(hw->wiphy->bands[NL80211_BAND_6GHZ]);
	hw->wiphy->bands[NL80211_BAND_2GHZ] = NULL;
	hw->wiphy->bands[NL80211_BAND_5GHZ] = NULL;
	hw->wiphy->bands[NL80211_BAND_6GHZ] = NULL;
}

static void rtw89_core_ppdu_sts_init(struct rtw89_dev *rtwdev)
{
	int i;

	for (i = 0; i < RTW89_PHY_MAX; i++)
		skb_queue_head_init(&rtwdev->ppdu_sts.rx_queue[i]);
	for (i = 0; i < RTW89_PHY_MAX; i++)
		rtwdev->ppdu_sts.curr_rx_ppdu_cnt[i] = U8_MAX;
}

void rtw89_core_update_beacon_work(struct work_struct *work)
{
	struct rtw89_dev *rtwdev;
	struct rtw89_vif_link *rtwvif_link = container_of(work, struct rtw89_vif_link,
							  update_beacon_work);

	if (rtwvif_link->net_type != RTW89_NET_TYPE_AP_MODE)
		return;

	rtwdev = rtwvif_link->rtwvif->rtwdev;

	mutex_lock(&rtwdev->mutex);
	rtw89_chip_h2c_update_beacon(rtwdev, rtwvif_link);
	mutex_unlock(&rtwdev->mutex);
}

int rtw89_wait_for_cond(struct rtw89_wait_info *wait, unsigned int cond)
{
	struct completion *cmpl = &wait->completion;
	unsigned long time_left;
	unsigned int cur;

	cur = atomic_cmpxchg(&wait->cond, RTW89_WAIT_COND_IDLE, cond);
	if (cur != RTW89_WAIT_COND_IDLE)
		return -EBUSY;

	time_left = wait_for_completion_timeout(cmpl, RTW89_WAIT_FOR_COND_TIMEOUT);
	if (time_left == 0) {
		atomic_set(&wait->cond, RTW89_WAIT_COND_IDLE);
		return -ETIMEDOUT;
	}

	if (wait->data.err)
		return -EFAULT;

	return 0;
}

void rtw89_complete_cond(struct rtw89_wait_info *wait, unsigned int cond,
			 const struct rtw89_completion_data *data)
{
	unsigned int cur;

	cur = atomic_cmpxchg(&wait->cond, cond, RTW89_WAIT_COND_IDLE);
	if (cur != cond)
		return;

	wait->data = *data;
	complete(&wait->completion);
}

void rtw89_core_ntfy_btc_event(struct rtw89_dev *rtwdev, enum rtw89_btc_hmsg event)
{
	u16 bt_req_len;

	switch (event) {
	case RTW89_BTC_HMSG_SET_BT_REQ_SLOT:
		bt_req_len = rtw89_coex_query_bt_req_len(rtwdev, RTW89_PHY_0);
		rtw89_debug(rtwdev, RTW89_DBG_BTC,
			    "coex updates BT req len to %d TU\n", bt_req_len);
		rtw89_queue_chanctx_change(rtwdev, RTW89_CHANCTX_BT_SLOT_CHANGE);
		break;
	default:
		if (event < NUM_OF_RTW89_BTC_HMSG)
			rtw89_debug(rtwdev, RTW89_DBG_BTC,
				    "unhandled BTC HMSG event: %d\n", event);
		else
			rtw89_warn(rtwdev,
				   "unrecognized BTC HMSG event: %d\n", event);
		break;
	}
}

void rtw89_check_quirks(struct rtw89_dev *rtwdev, const struct dmi_system_id *quirks)
{
	const struct dmi_system_id *match;
	enum rtw89_quirks quirk;

	if (!quirks)
		return;

	for (match = dmi_first_match(quirks); match; match = dmi_first_match(match + 1)) {
		quirk = (uintptr_t)match->driver_data;
		if (quirk >= NUM_OF_RTW89_QUIRKS)
			continue;

		set_bit(quirk, rtwdev->quirks);
	}
}
EXPORT_SYMBOL(rtw89_check_quirks);

int rtw89_core_start(struct rtw89_dev *rtwdev)
{
	int ret;

	ret = rtw89_mac_init(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "mac init fail, ret:%d\n", ret);
		return ret;
	}

	rtw89_btc_ntfy_poweron(rtwdev);

	/* efuse process */

	/* pre-config BB/RF, BB reset/RFC reset */
	ret = rtw89_chip_reset_bb_rf(rtwdev);
	if (ret)
		return ret;

	rtw89_phy_init_bb_reg(rtwdev);
	rtw89_chip_bb_postinit(rtwdev);
	rtw89_phy_init_rf_reg(rtwdev, false);

	rtw89_btc_ntfy_init(rtwdev, BTC_MODE_NORMAL);

	rtw89_phy_dm_init(rtwdev);

	rtw89_mac_cfg_ppdu_status(rtwdev, RTW89_MAC_0, true);
	rtw89_mac_update_rts_threshold(rtwdev, RTW89_MAC_0);

	rtw89_tas_reset(rtwdev);

	ret = rtw89_hci_start(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to start hci\n");
		return ret;
	}

	ieee80211_queue_delayed_work(rtwdev->hw, &rtwdev->track_work,
				     RTW89_TRACK_WORK_PERIOD);

	set_bit(RTW89_FLAG_RUNNING, rtwdev->flags);

	rtw89_chip_rfk_init_late(rtwdev);
	rtw89_btc_ntfy_radio_state(rtwdev, BTC_RFCTRL_WL_ON);
	rtw89_fw_h2c_fw_log(rtwdev, rtwdev->fw.log.enable);
	rtw89_fw_h2c_init_ba_cam(rtwdev);

	return 0;
}

void rtw89_core_stop(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;

	/* Prvent to stop twice; enter_ips and ops_stop */
	if (!test_bit(RTW89_FLAG_RUNNING, rtwdev->flags))
		return;

	rtw89_btc_ntfy_radio_state(rtwdev, BTC_RFCTRL_WL_OFF);

	clear_bit(RTW89_FLAG_RUNNING, rtwdev->flags);

	mutex_unlock(&rtwdev->mutex);

	cancel_work_sync(&rtwdev->c2h_work);
	cancel_work_sync(&rtwdev->cancel_6ghz_probe_work);
	cancel_work_sync(&btc->eapol_notify_work);
	cancel_work_sync(&btc->arp_notify_work);
	cancel_work_sync(&btc->dhcp_notify_work);
	cancel_work_sync(&btc->icmp_notify_work);
	cancel_delayed_work_sync(&rtwdev->txq_reinvoke_work);
	cancel_delayed_work_sync(&rtwdev->track_work);
	cancel_delayed_work_sync(&rtwdev->chanctx_work);
	cancel_delayed_work_sync(&rtwdev->coex_act1_work);
	cancel_delayed_work_sync(&rtwdev->coex_bt_devinfo_work);
	cancel_delayed_work_sync(&rtwdev->coex_rfk_chk_work);
	cancel_delayed_work_sync(&rtwdev->cfo_track_work);
	cancel_delayed_work_sync(&rtwdev->forbid_ba_work);
	cancel_delayed_work_sync(&rtwdev->antdiv_work);

	mutex_lock(&rtwdev->mutex);

	rtw89_btc_ntfy_poweroff(rtwdev);
	rtw89_hci_flush_queues(rtwdev, BIT(rtwdev->hw->queues) - 1, true);
	rtw89_mac_flush_txq(rtwdev, BIT(rtwdev->hw->queues) - 1, true);
	rtw89_hci_stop(rtwdev);
	rtw89_hci_deinit(rtwdev);
	rtw89_mac_pwr_off(rtwdev);
	rtw89_hci_reset(rtwdev);
}

u8 rtw89_acquire_mac_id(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u8 mac_id_num;
	u8 mac_id;

	if (rtwdev->support_mlo)
		mac_id_num = chip->support_macid_num / chip->support_link_num;
	else
		mac_id_num = chip->support_macid_num;

	mac_id = find_first_zero_bit(rtwdev->mac_id_map, mac_id_num);
	if (mac_id == mac_id_num)
		return RTW89_MAX_MAC_ID_NUM;

	set_bit(mac_id, rtwdev->mac_id_map);
	return mac_id;
}

void rtw89_release_mac_id(struct rtw89_dev *rtwdev, u8 mac_id)
{
	clear_bit(mac_id, rtwdev->mac_id_map);
}

void rtw89_init_vif(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
		    u8 mac_id, u8 port)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u8 support_link_num = chip->support_link_num;
	u8 support_mld_num = 0;
	unsigned int link_id;
	u8 index;

	bitmap_zero(rtwvif->links_inst_map, __RTW89_MLD_MAX_LINK_NUM);
	for (link_id = 0; link_id < IEEE80211_MLD_MAX_NUM_LINKS; link_id++)
		rtwvif->links[link_id] = NULL;

	rtwvif->rtwdev = rtwdev;

	if (rtwdev->support_mlo) {
		rtwvif->links_inst_valid_num = support_link_num;
		support_mld_num = chip->support_macid_num / support_link_num;
	} else {
		rtwvif->links_inst_valid_num = 1;
	}

	for (index = 0; index < rtwvif->links_inst_valid_num; index++) {
		struct rtw89_vif_link *inst = &rtwvif->links_inst[index];

		inst->rtwvif = rtwvif;
		inst->mac_id = mac_id + index * support_mld_num;
		inst->mac_idx = RTW89_MAC_0 + index;
		inst->phy_idx = RTW89_PHY_0 + index;

		/* multi-link use the same port id on different HW bands */
		inst->port = port;
	}
}

void rtw89_init_sta(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif,
		    struct rtw89_sta *rtwsta, u8 mac_id)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u8 support_link_num = chip->support_link_num;
	u8 support_mld_num = 0;
	unsigned int link_id;
	u8 index;

	bitmap_zero(rtwsta->links_inst_map, __RTW89_MLD_MAX_LINK_NUM);
	for (link_id = 0; link_id < IEEE80211_MLD_MAX_NUM_LINKS; link_id++)
		rtwsta->links[link_id] = NULL;

	rtwsta->rtwdev = rtwdev;
	rtwsta->rtwvif = rtwvif;

	if (rtwdev->support_mlo) {
		rtwsta->links_inst_valid_num = support_link_num;
		support_mld_num = chip->support_macid_num / support_link_num;
	} else {
		rtwsta->links_inst_valid_num = 1;
	}

	for (index = 0; index < rtwsta->links_inst_valid_num; index++) {
		struct rtw89_sta_link *inst = &rtwsta->links_inst[index];

		inst->rtwvif_link = &rtwvif->links_inst[index];

		inst->rtwsta = rtwsta;
		inst->mac_id = mac_id + index * support_mld_num;
	}
}

struct rtw89_vif_link *rtw89_vif_set_link(struct rtw89_vif *rtwvif,
					  unsigned int link_id)
{
	struct rtw89_vif_link *rtwvif_link = rtwvif->links[link_id];
	u8 index;
	int ret;

	if (rtwvif_link)
		return rtwvif_link;

	index = find_first_zero_bit(rtwvif->links_inst_map,
				    rtwvif->links_inst_valid_num);
	if (index == rtwvif->links_inst_valid_num) {
		ret = -EBUSY;
		goto err;
	}

	rtwvif_link = &rtwvif->links_inst[index];
	rtwvif_link->link_id = link_id;

	set_bit(index, rtwvif->links_inst_map);
	rtwvif->links[link_id] = rtwvif_link;
	return rtwvif_link;

err:
	rtw89_err(rtwvif->rtwdev, "vif (link_id %u) failed to set link: %d\n",
		  link_id, ret);
	return NULL;
}

void rtw89_vif_unset_link(struct rtw89_vif *rtwvif, unsigned int link_id)
{
	struct rtw89_vif_link **container = &rtwvif->links[link_id];
	struct rtw89_vif_link *link = *container;
	u8 index;

	if (!link)
		return;

	index = rtw89_vif_link_inst_get_index(link);
	clear_bit(index, rtwvif->links_inst_map);
	*container = NULL;
}

struct rtw89_sta_link *rtw89_sta_set_link(struct rtw89_sta *rtwsta,
					  unsigned int link_id)
{
	struct rtw89_vif *rtwvif = rtwsta->rtwvif;
	struct rtw89_vif_link *rtwvif_link = rtwvif->links[link_id];
	struct rtw89_sta_link *rtwsta_link = rtwsta->links[link_id];
	u8 index;
	int ret;

	if (rtwsta_link)
		return rtwsta_link;

	if (!rtwvif_link) {
		ret = -ENOLINK;
		goto err;
	}

	index = rtw89_vif_link_inst_get_index(rtwvif_link);
	if (test_bit(index, rtwsta->links_inst_map)) {
		ret = -EBUSY;
		goto err;
	}

	rtwsta_link = &rtwsta->links_inst[index];
	rtwsta_link->link_id = link_id;

	set_bit(index, rtwsta->links_inst_map);
	rtwsta->links[link_id] = rtwsta_link;
	return rtwsta_link;

err:
	rtw89_err(rtwsta->rtwdev, "sta (link_id %u) failed to set link: %d\n",
		  link_id, ret);
	return NULL;
}

void rtw89_sta_unset_link(struct rtw89_sta *rtwsta, unsigned int link_id)
{
	struct rtw89_sta_link **container = &rtwsta->links[link_id];
	struct rtw89_sta_link *link = *container;
	u8 index;

	if (!link)
		return;

	index = rtw89_sta_link_inst_get_index(link);
	clear_bit(index, rtwsta->links_inst_map);
	*container = NULL;
}

int rtw89_core_init(struct rtw89_dev *rtwdev)
{
	struct rtw89_btc *btc = &rtwdev->btc;
	u8 band;

	INIT_LIST_HEAD(&rtwdev->ba_list);
	INIT_LIST_HEAD(&rtwdev->forbid_ba_list);
	INIT_LIST_HEAD(&rtwdev->rtwvifs_list);
	INIT_LIST_HEAD(&rtwdev->early_h2c_list);
	for (band = NL80211_BAND_2GHZ; band < NUM_NL80211_BANDS; band++) {
		if (!(rtwdev->chip->support_bands & BIT(band)))
			continue;
		INIT_LIST_HEAD(&rtwdev->scan_info.pkt_list[band]);
	}
	INIT_WORK(&rtwdev->ba_work, rtw89_core_ba_work);
	INIT_WORK(&rtwdev->txq_work, rtw89_core_txq_work);
	INIT_DELAYED_WORK(&rtwdev->txq_reinvoke_work, rtw89_core_txq_reinvoke_work);
	INIT_DELAYED_WORK(&rtwdev->track_work, rtw89_track_work);
	INIT_DELAYED_WORK(&rtwdev->chanctx_work, rtw89_chanctx_work);
	INIT_DELAYED_WORK(&rtwdev->coex_act1_work, rtw89_coex_act1_work);
	INIT_DELAYED_WORK(&rtwdev->coex_bt_devinfo_work, rtw89_coex_bt_devinfo_work);
	INIT_DELAYED_WORK(&rtwdev->coex_rfk_chk_work, rtw89_coex_rfk_chk_work);
	INIT_DELAYED_WORK(&rtwdev->cfo_track_work, rtw89_phy_cfo_track_work);
	INIT_DELAYED_WORK(&rtwdev->forbid_ba_work, rtw89_forbid_ba_work);
	INIT_DELAYED_WORK(&rtwdev->antdiv_work, rtw89_phy_antdiv_work);
	rtwdev->txq_wq = alloc_workqueue("rtw89_tx_wq", WQ_UNBOUND | WQ_HIGHPRI, 0);
	if (!rtwdev->txq_wq)
		return -ENOMEM;
	spin_lock_init(&rtwdev->ba_lock);
	spin_lock_init(&rtwdev->rpwm_lock);
	mutex_init(&rtwdev->mutex);
	mutex_init(&rtwdev->rf_mutex);
	rtwdev->total_sta_assoc = 0;

	rtw89_init_wait(&rtwdev->mcc.wait);
	rtw89_init_wait(&rtwdev->mac.fw_ofld_wait);
	rtw89_init_wait(&rtwdev->wow.wait);
	rtw89_init_wait(&rtwdev->mac.ps_wait);

	INIT_WORK(&rtwdev->c2h_work, rtw89_fw_c2h_work);
	INIT_WORK(&rtwdev->ips_work, rtw89_ips_work);
	INIT_WORK(&rtwdev->load_firmware_work, rtw89_load_firmware_work);
	INIT_WORK(&rtwdev->cancel_6ghz_probe_work, rtw89_cancel_6ghz_probe_work);

	skb_queue_head_init(&rtwdev->c2h_queue);
	rtw89_core_ppdu_sts_init(rtwdev);
	rtw89_traffic_stats_init(rtwdev, &rtwdev->stats);

	rtwdev->hal.rx_fltr = DEFAULT_AX_RX_FLTR;
	rtwdev->dbcc_en = false;
	rtwdev->mlo_dbcc_mode = MLO_DBCC_NOT_SUPPORT;
	rtwdev->mac.qta_mode = RTW89_QTA_SCC;

	if (rtwdev->chip->chip_gen == RTW89_CHIP_BE) {
		rtwdev->dbcc_en = true;
		rtwdev->mac.qta_mode = RTW89_QTA_DBCC;
		rtwdev->mlo_dbcc_mode = MLO_2_PLUS_0_1RF;
	}

	INIT_WORK(&btc->eapol_notify_work, rtw89_btc_ntfy_eapol_packet_work);
	INIT_WORK(&btc->arp_notify_work, rtw89_btc_ntfy_arp_packet_work);
	INIT_WORK(&btc->dhcp_notify_work, rtw89_btc_ntfy_dhcp_packet_work);
	INIT_WORK(&btc->icmp_notify_work, rtw89_btc_ntfy_icmp_packet_work);

	init_completion(&rtwdev->fw.req.completion);
	init_completion(&rtwdev->rfk_wait.completion);

	schedule_work(&rtwdev->load_firmware_work);

	rtw89_ser_init(rtwdev);
	rtw89_entity_init(rtwdev);
	rtw89_tas_init(rtwdev);

	return 0;
}
EXPORT_SYMBOL(rtw89_core_init);

void rtw89_core_deinit(struct rtw89_dev *rtwdev)
{
	rtw89_ser_deinit(rtwdev);
	rtw89_unload_firmware(rtwdev);
	rtw89_fw_free_all_early_h2c(rtwdev);

	destroy_workqueue(rtwdev->txq_wq);
	mutex_destroy(&rtwdev->rf_mutex);
	mutex_destroy(&rtwdev->mutex);
}
EXPORT_SYMBOL(rtw89_core_deinit);

void rtw89_core_scan_start(struct rtw89_dev *rtwdev, struct rtw89_vif_link *rtwvif_link,
			   const u8 *mac_addr, bool hw_scan)
{
	const struct rtw89_chan *chan = rtw89_chan_get(rtwdev,
						       rtwvif_link->chanctx_idx);

	rtwdev->scanning = true;
	rtw89_leave_lps(rtwdev);
	if (hw_scan)
		rtw89_leave_ips_by_hwflags(rtwdev);

	ether_addr_copy(rtwvif_link->mac_addr, mac_addr);
	rtw89_btc_ntfy_scan_start(rtwdev, RTW89_PHY_0, chan->band_type);
	rtw89_chip_rfk_scan(rtwdev, rtwvif_link, true);
	rtw89_hci_recalc_int_mit(rtwdev);
	rtw89_phy_config_edcca(rtwdev, true);

	rtw89_fw_h2c_cam(rtwdev, rtwvif_link, NULL, mac_addr);
}

void rtw89_core_scan_complete(struct rtw89_dev *rtwdev,
			      struct rtw89_vif_link *rtwvif_link, bool hw_scan)
{
	struct ieee80211_bss_conf *bss_conf;

	if (!rtwvif_link)
		return;

	rcu_read_lock();

	bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, true);
	ether_addr_copy(rtwvif_link->mac_addr, bss_conf->addr);

	rcu_read_unlock();

	rtw89_fw_h2c_cam(rtwdev, rtwvif_link, NULL, NULL);

	rtw89_chip_rfk_scan(rtwdev, rtwvif_link, false);
	rtw89_btc_ntfy_scan_finish(rtwdev, RTW89_PHY_0);
	rtw89_phy_config_edcca(rtwdev, false);

	rtwdev->scanning = false;
	rtwdev->dig.bypass_dig = true;
	if (hw_scan && (rtwdev->hw->conf.flags & IEEE80211_CONF_IDLE))
		ieee80211_queue_work(rtwdev->hw, &rtwdev->ips_work);
}

static void rtw89_read_chip_ver(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	int ret;
	u8 val;
	u8 cv;

	cv = rtw89_read32_mask(rtwdev, R_AX_SYS_CFG1, B_AX_CHIP_VER_MASK);
	if (chip->chip_id == RTL8852A && cv <= CHIP_CBV) {
		if (rtw89_read32(rtwdev, R_AX_GPIO0_7_FUNC_SEL) == RTW89_R32_DEAD)
			cv = CHIP_CAV;
		else
			cv = CHIP_CBV;
	}

	rtwdev->hal.cv = cv;

	if (rtw89_is_rtl885xb(rtwdev)) {
		ret = rtw89_mac_read_xtal_si(rtwdev, XTAL_SI_CV, &val);
		if (ret)
			return;

		rtwdev->hal.acv = u8_get_bits(val, XTAL_SI_ACV_MASK);
	}
}

static void rtw89_core_setup_phycap(struct rtw89_dev *rtwdev)
{
	rtwdev->hal.support_cckpd =
		!(rtwdev->chip->chip_id == RTL8852A && rtwdev->hal.cv <= CHIP_CBV) &&
		!(rtwdev->chip->chip_id == RTL8852B && rtwdev->hal.cv <= CHIP_CAV);
	rtwdev->hal.support_igi =
		rtwdev->chip->chip_id == RTL8852A && rtwdev->hal.cv <= CHIP_CBV;
}

static void rtw89_core_setup_rfe_parms(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	const struct rtw89_rfe_parms_conf *conf = chip->rfe_parms_conf;
	struct rtw89_efuse *efuse = &rtwdev->efuse;
	const struct rtw89_rfe_parms *sel;
	u8 rfe_type = efuse->rfe_type;

	if (!conf) {
		sel = chip->dflt_parms;
		goto out;
	}

	while (conf->rfe_parms) {
		if (rfe_type == conf->rfe_type) {
			sel = conf->rfe_parms;
			goto out;
		}
		conf++;
	}

	sel = chip->dflt_parms;

out:
	rtwdev->rfe_parms = rtw89_load_rfe_data_from_fw(rtwdev, sel);
	rtw89_load_txpwr_table(rtwdev, rtwdev->rfe_parms->byr_tbl);
}

static int rtw89_chip_efuse_info_setup(struct rtw89_dev *rtwdev)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	int ret;

	ret = rtw89_mac_partial_init(rtwdev, false);
	if (ret)
		return ret;

	ret = mac->parse_efuse_map(rtwdev);
	if (ret)
		return ret;

	ret = mac->parse_phycap_map(rtwdev);
	if (ret)
		return ret;

	ret = rtw89_mac_setup_phycap(rtwdev);
	if (ret)
		return ret;

	rtw89_core_setup_phycap(rtwdev);

	rtw89_hci_mac_pre_deinit(rtwdev);

	rtw89_mac_pwr_off(rtwdev);

	return 0;
}

static int rtw89_chip_board_info_setup(struct rtw89_dev *rtwdev)
{
	rtw89_chip_fem_setup(rtwdev);

	return 0;
}

static bool rtw89_chip_has_rfkill(struct rtw89_dev *rtwdev)
{
	return !!rtwdev->chip->rfkill_init;
}

static void rtw89_core_rfkill_init(struct rtw89_dev *rtwdev)
{
	const struct rtw89_rfkill_regs *regs = rtwdev->chip->rfkill_init;

	rtw89_write16_mask(rtwdev, regs->pinmux.addr,
			   regs->pinmux.mask, regs->pinmux.data);
	rtw89_write16_mask(rtwdev, regs->mode.addr,
			   regs->mode.mask, regs->mode.data);
}

static bool rtw89_core_rfkill_get(struct rtw89_dev *rtwdev)
{
	const struct rtw89_reg_def *reg = &rtwdev->chip->rfkill_get;

	return !rtw89_read8_mask(rtwdev, reg->addr, reg->mask);
}

static void rtw89_rfkill_polling_init(struct rtw89_dev *rtwdev)
{
	if (!rtw89_chip_has_rfkill(rtwdev))
		return;

	rtw89_core_rfkill_init(rtwdev);
	rtw89_core_rfkill_poll(rtwdev, true);
	wiphy_rfkill_start_polling(rtwdev->hw->wiphy);
}

static void rtw89_rfkill_polling_deinit(struct rtw89_dev *rtwdev)
{
	if (!rtw89_chip_has_rfkill(rtwdev))
		return;

	wiphy_rfkill_stop_polling(rtwdev->hw->wiphy);
}

void rtw89_core_rfkill_poll(struct rtw89_dev *rtwdev, bool force)
{
	bool prev, blocked;

	if (!rtw89_chip_has_rfkill(rtwdev))
		return;

	prev = test_bit(RTW89_FLAG_HW_RFKILL_STATE, rtwdev->flags);
	blocked = rtw89_core_rfkill_get(rtwdev);

	if (!force && prev == blocked)
		return;

	rtw89_info(rtwdev, "rfkill hardware state changed to %s\n",
		   blocked ? "disable" : "enable");

	if (blocked)
		set_bit(RTW89_FLAG_HW_RFKILL_STATE, rtwdev->flags);
	else
		clear_bit(RTW89_FLAG_HW_RFKILL_STATE, rtwdev->flags);

	wiphy_rfkill_set_hw_state(rtwdev->hw->wiphy, blocked);
}

int rtw89_chip_info_setup(struct rtw89_dev *rtwdev)
{
	int ret;

	rtw89_read_chip_ver(rtwdev);

	ret = rtw89_wait_firmware_completion(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to wait firmware completion\n");
		return ret;
	}

	ret = rtw89_fw_recognize(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to recognize firmware\n");
		return ret;
	}

	ret = rtw89_chip_efuse_info_setup(rtwdev);
	if (ret)
		return ret;

	ret = rtw89_fw_recognize_elements(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to recognize firmware elements\n");
		return ret;
	}

	ret = rtw89_chip_board_info_setup(rtwdev);
	if (ret)
		return ret;

	rtw89_core_setup_rfe_parms(rtwdev);
	rtwdev->ps_mode = rtw89_update_ps_mode(rtwdev);

	return 0;
}
EXPORT_SYMBOL(rtw89_chip_info_setup);

void rtw89_chip_cfg_txpwr_ul_tb_offset(struct rtw89_dev *rtwdev,
				       struct rtw89_vif_link *rtwvif_link)
{
	struct ieee80211_vif *vif = rtwvif_link_to_vif(rtwvif_link);
	const struct rtw89_chip_info *chip = rtwdev->chip;
	struct ieee80211_bss_conf *bss_conf;

	rcu_read_lock();

	bss_conf = rtw89_vif_rcu_dereference_link(rtwvif_link, false);
	if (!bss_conf->he_support || !vif->cfg.assoc) {
		rcu_read_unlock();
		return;
	}

	rcu_read_unlock();

	if (chip->ops->set_txpwr_ul_tb_offset)
		chip->ops->set_txpwr_ul_tb_offset(rtwdev, 0, rtwvif_link->mac_idx);
}

static int rtw89_core_register_hw(struct rtw89_dev *rtwdev)
{
	const struct rtw89_chip_info *chip = rtwdev->chip;
	u8 n = rtwdev->support_mlo ? chip->support_link_num : 1;
	struct ieee80211_hw *hw = rtwdev->hw;
	struct rtw89_efuse *efuse = &rtwdev->efuse;
	struct rtw89_hal *hal = &rtwdev->hal;
	int ret;
	int tx_headroom = IEEE80211_HT_CTL_LEN;

	hw->vif_data_size = struct_size_t(struct rtw89_vif, links_inst, n);
	hw->sta_data_size = struct_size_t(struct rtw89_sta, links_inst, n);
	hw->txq_data_size = sizeof(struct rtw89_txq);
	hw->chanctx_data_size = sizeof(struct rtw89_chanctx_cfg);

	SET_IEEE80211_PERM_ADDR(hw, efuse->addr);

	hw->extra_tx_headroom = tx_headroom;
	hw->queues = IEEE80211_NUM_ACS;
	hw->max_rx_aggregation_subframes = RTW89_MAX_RX_AGG_NUM;
	hw->max_tx_aggregation_subframes = RTW89_MAX_TX_AGG_NUM;
	hw->uapsd_max_sp_len = IEEE80211_WMM_IE_STA_QOSINFO_SP_ALL;

	hw->radiotap_mcs_details |= IEEE80211_RADIOTAP_MCS_HAVE_FEC |
				    IEEE80211_RADIOTAP_MCS_HAVE_STBC;
	hw->radiotap_vht_details |= IEEE80211_RADIOTAP_VHT_KNOWN_STBC;

	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, HAS_RATE_CONTROL);
	ieee80211_hw_set(hw, MFP_CAPABLE);
	ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(hw, AMPDU_AGGREGATION);
	ieee80211_hw_set(hw, RX_INCLUDES_FCS);
	ieee80211_hw_set(hw, TX_AMSDU);
	ieee80211_hw_set(hw, SUPPORT_FAST_XMIT);
	ieee80211_hw_set(hw, SUPPORTS_AMSDU_IN_AMPDU);
	ieee80211_hw_set(hw, SUPPORTS_PS);
	ieee80211_hw_set(hw, SUPPORTS_DYNAMIC_PS);
	ieee80211_hw_set(hw, SINGLE_SCAN_ON_ALL_BANDS);
	ieee80211_hw_set(hw, SUPPORTS_MULTI_BSSID);
	ieee80211_hw_set(hw, WANT_MONITOR_VIF);

	if (chip->support_bandwidths & BIT(NL80211_CHAN_WIDTH_160))
		ieee80211_hw_set(hw, SUPPORTS_VHT_EXT_NSS_BW);

	if (RTW89_CHK_FW_FEATURE(BEACON_FILTER, &rtwdev->fw))
		ieee80211_hw_set(hw, CONNECTION_MONITOR);

	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				     BIT(NL80211_IFTYPE_AP) |
				     BIT(NL80211_IFTYPE_P2P_CLIENT) |
				     BIT(NL80211_IFTYPE_P2P_GO);

	if (hal->ant_diversity) {
		hw->wiphy->available_antennas_tx = 0x3;
		hw->wiphy->available_antennas_rx = 0x3;
	} else {
		hw->wiphy->available_antennas_tx = BIT(rtwdev->chip->rf_path_num) - 1;
		hw->wiphy->available_antennas_rx = BIT(rtwdev->chip->rf_path_num) - 1;
	}

	hw->wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS |
			    WIPHY_FLAG_TDLS_EXTERNAL_SETUP |
			    WIPHY_FLAG_AP_UAPSD |
			    WIPHY_FLAG_SUPPORTS_EXT_KEK_KCK;

	if (!chip->support_rnr)
		hw->wiphy->flags |= WIPHY_FLAG_SPLIT_SCAN_6GHZ;

	if (chip->chip_gen == RTW89_CHIP_BE)
		hw->wiphy->flags |= WIPHY_FLAG_DISABLE_WEXT;

	if (rtwdev->support_mlo)
		hw->wiphy->flags |= WIPHY_FLAG_SUPPORTS_MLO;

	hw->wiphy->features |= NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR;

	hw->wiphy->max_scan_ssids = RTW89_SCANOFLD_MAX_SSID;
	hw->wiphy->max_scan_ie_len = RTW89_SCANOFLD_MAX_IE_LEN;

#ifdef CONFIG_PM
	hw->wiphy->wowlan = rtwdev->chip->wowlan_stub;
	hw->wiphy->max_sched_scan_ssids = RTW89_SCANOFLD_MAX_SSID;
#endif

	hw->wiphy->tid_config_support.vif |= BIT(NL80211_TID_CONFIG_ATTR_AMPDU_CTRL);
	hw->wiphy->tid_config_support.peer |= BIT(NL80211_TID_CONFIG_ATTR_AMPDU_CTRL);
	hw->wiphy->tid_config_support.vif |= BIT(NL80211_TID_CONFIG_ATTR_AMSDU_CTRL);
	hw->wiphy->tid_config_support.peer |= BIT(NL80211_TID_CONFIG_ATTR_AMSDU_CTRL);
	hw->wiphy->max_remain_on_channel_duration = 1000;

	wiphy_ext_feature_set(hw->wiphy, NL80211_EXT_FEATURE_CAN_REPLACE_PTK0);
	wiphy_ext_feature_set(hw->wiphy, NL80211_EXT_FEATURE_SCAN_RANDOM_SN);
	wiphy_ext_feature_set(hw->wiphy, NL80211_EXT_FEATURE_SET_SCAN_DWELL);

	ret = rtw89_core_set_supported_band(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to set supported band\n");
		return ret;
	}

	ret = rtw89_regd_setup(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to set up regd\n");
		goto err_free_supported_band;
	}

	hw->wiphy->sar_capa = &rtw89_sar_capa;

	ret = ieee80211_register_hw(hw);
	if (ret) {
		rtw89_err(rtwdev, "failed to register hw\n");
		goto err_free_supported_band;
	}

	ret = rtw89_regd_init(rtwdev, rtw89_regd_notifier);
	if (ret) {
		rtw89_err(rtwdev, "failed to init regd\n");
		goto err_unregister_hw;
	}

	rtw89_rfkill_polling_init(rtwdev);

	return 0;

err_unregister_hw:
	ieee80211_unregister_hw(hw);
err_free_supported_band:
	rtw89_core_clr_supported_band(rtwdev);

	return ret;
}

static void rtw89_core_unregister_hw(struct rtw89_dev *rtwdev)
{
	struct ieee80211_hw *hw = rtwdev->hw;

	rtw89_rfkill_polling_deinit(rtwdev);
	ieee80211_unregister_hw(hw);
	rtw89_core_clr_supported_band(rtwdev);
}

int rtw89_core_register(struct rtw89_dev *rtwdev)
{
	int ret;

	ret = rtw89_core_register_hw(rtwdev);
	if (ret) {
		rtw89_err(rtwdev, "failed to register core hw\n");
		return ret;
	}

	rtw89_debugfs_init(rtwdev);

	return 0;
}
EXPORT_SYMBOL(rtw89_core_register);

void rtw89_core_unregister(struct rtw89_dev *rtwdev)
{
	rtw89_core_unregister_hw(rtwdev);

	rtw89_debugfs_deinit(rtwdev);
}
EXPORT_SYMBOL(rtw89_core_unregister);

struct rtw89_dev *rtw89_alloc_ieee80211_hw(struct device *device,
					   u32 bus_data_size,
					   const struct rtw89_chip_info *chip)
{
	struct rtw89_fw_info early_fw = {};
	const struct firmware *firmware;
	struct ieee80211_hw *hw;
	struct rtw89_dev *rtwdev;
	struct ieee80211_ops *ops;
	u32 driver_data_size;
	int fw_format = -1;
	bool support_mlo;
	bool no_chanctx;

	firmware = rtw89_early_fw_feature_recognize(device, chip, &early_fw, &fw_format);

	ops = kmemdup(&rtw89_ops, sizeof(rtw89_ops), GFP_KERNEL);
	if (!ops)
		goto err;

	no_chanctx = chip->support_chanctx_num == 0 ||
		     !RTW89_CHK_FW_FEATURE(SCAN_OFFLOAD, &early_fw) ||
		     !RTW89_CHK_FW_FEATURE(BEACON_FILTER, &early_fw);

	if (no_chanctx) {
		ops->add_chanctx = ieee80211_emulate_add_chanctx;
		ops->remove_chanctx = ieee80211_emulate_remove_chanctx;
		ops->change_chanctx = ieee80211_emulate_change_chanctx;
		ops->switch_vif_chanctx = ieee80211_emulate_switch_vif_chanctx;
		ops->assign_vif_chanctx = NULL;
		ops->unassign_vif_chanctx = NULL;
		ops->remain_on_channel = NULL;
		ops->cancel_remain_on_channel = NULL;
	}

	driver_data_size = sizeof(struct rtw89_dev) + bus_data_size;
	hw = ieee80211_alloc_hw(driver_data_size, ops);
	if (!hw)
		goto err;

	/* TODO: When driver MLO arch. is done, determine whether to support MLO
	 * according to the following conditions.
	 * 1. run with chanctx_ops
	 * 2. chip->support_link_num != 0
	 * 3. FW feature supports AP_LINK_PS
	 */
	support_mlo = false;

	hw->wiphy->iface_combinations = rtw89_iface_combs;

	if (no_chanctx || chip->support_chanctx_num == 1)
		hw->wiphy->n_iface_combinations = 1;
	else
		hw->wiphy->n_iface_combinations = ARRAY_SIZE(rtw89_iface_combs);

	rtwdev = hw->priv;
	rtwdev->hw = hw;
	rtwdev->dev = device;
	rtwdev->ops = ops;
	rtwdev->chip = chip;
	rtwdev->fw.req.firmware = firmware;
	rtwdev->fw.fw_format = fw_format;
	rtwdev->support_mlo = support_mlo;

	rtw89_debug(rtwdev, RTW89_DBG_CHAN, "probe driver %s chanctx\n",
		    no_chanctx ? "without" : "with");
	rtw89_debug(rtwdev, RTW89_DBG_CHAN, "probe driver %s MLO cap\n",
		    support_mlo ? "with" : "without");

	return rtwdev;

err:
	kfree(ops);
	release_firmware(firmware);
	return NULL;
}
EXPORT_SYMBOL(rtw89_alloc_ieee80211_hw);

void rtw89_free_ieee80211_hw(struct rtw89_dev *rtwdev)
{
	kfree(rtwdev->ops);
	kfree(rtwdev->rfe_data);
	release_firmware(rtwdev->fw.req.firmware);
	ieee80211_free_hw(rtwdev->hw);
}
EXPORT_SYMBOL(rtw89_free_ieee80211_hw);

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ax wireless core module");
MODULE_LICENSE("Dual BSD/GPL");
