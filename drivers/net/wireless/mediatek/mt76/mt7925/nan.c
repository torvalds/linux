// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Copyright (C) 2025-2026 MediaTek Inc. */

#include <asm/byteorder.h>
#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>

#include "mt7925.h"
#include "mcu.h"
#include "nan.h"
#include "regd.h"

static void mt7925_nan_set_5g_channel(struct mt792x_dev *dev,
				      struct mt7925_nan_enable_req_tlv *req,
				      struct cfg80211_nan_conf *conf)
{
	struct ieee80211_channel *chan;
	u32 ch5g = 0;

	chan = conf->band_cfgs[NL80211_BAND_5GHZ].chan;

	if (!chan)
		return;

	if (!mt7925_regd_is_valid_channel(dev, NL80211_BAND_5GHZ, chan))
		return;

	req->config_5g_channel = 1;

	if (chan->hw_value == NAN_5G_LOW_DISC_CHANNEL)
		ch5g |= BIT(0);
	else if (chan->hw_value == NAN_5G_HIGH_DISC_CHANNEL)
		ch5g |= BIT(1);

	req->channel_5g_val = cpu_to_le32(ch5g);
}

static void mt7925_nan_set_scan_params(struct mt7925_nan_enable_req_tlv *req,
				       struct cfg80211_nan_conf *conf)
{
	req->scan_params_val.scan_period[0] =
		cpu_to_le16(conf->scan_period < 255 ? conf->scan_period : 255);
	req->scan_params_val.dwell_time[0] =
		conf->scan_dwell_time < 255 ? conf->scan_dwell_time : 255;
}

static u16
mt7925_nan_avail_attr_ctrl(const struct ieee80211_nan_sched_cfg *sched)
{
	if (sched->avail_blob_len < NAN_AVAIL_ATTR_CTRL_OFFSET + 2)
		return 0;

	return sched->avail_blob[NAN_AVAIL_ATTR_CTRL_OFFSET] |
	       sched->avail_blob[NAN_AVAIL_ATTR_CTRL_OFFSET + 1] << 8;
}

static void
mt7925_nan_update_conf(struct mt792x_vif *mvif,
		       const struct cfg80211_nan_conf *conf)
{
	mvif->nan.conf.master_pref = conf->master_pref;
	mvif->nan.conf.bands = conf->bands;
	mvif->nan.conf.discovery_beacon_interval =
		conf->discovery_beacon_interval;
	mvif->nan.conf.enable_dw_notification =
		conf->enable_dw_notification;

	memcpy(mvif->nan.conf.cluster_id, conf->cluster_id, ETH_ALEN);
}

static int
mt7925_nan_mp_tlv(struct sk_buff *skb, u8 master_pref)
{
	struct mt7925_nan_master_preference_tlv *mp_tlv = NULL;
	struct tlv *tlv = NULL;

	if (!skb)
		return -EINVAL;

	tlv = mt76_connac_mcu_add_tlv(skb, NAN_UNI_CMD_SET_MASTER_PREFERENCE,
				      sizeof(struct mt7925_nan_master_preference_tlv));
	if (!tlv)
		return -ENOMEM;

	mp_tlv = (struct mt7925_nan_master_preference_tlv *)tlv;

	mp_tlv->master_preference = master_pref;

	return 0;
}

static int
mt7925_nan_dw_tlv(struct sk_buff *skb, struct cfg80211_nan_conf *conf)
{
	struct mt7925_nan_dw_interval_tlv *dw_tlv = NULL;
	struct tlv *tlv = NULL;
	u16 interval;

	if (!skb || !conf)
		return -EINVAL;

	tlv = mt76_connac_mcu_add_tlv(skb, NAN_UNI_CMD_SET_DW_INTERVAL,
				      sizeof(struct mt7925_nan_dw_interval_tlv));

	if (!tlv)
		return -ENOMEM;

	dw_tlv = (struct mt7925_nan_dw_interval_tlv *)tlv;

	/* Set DW interval for 2.4GHz and 5GHz bands if available */
	if (conf->band_cfgs[NL80211_BAND_2GHZ].awake_dw_interval > 0) {
		dw_tlv->dw_interval = conf->band_cfgs[NL80211_BAND_2GHZ].awake_dw_interval;
	} else if (conf->band_cfgs[NL80211_BAND_5GHZ].awake_dw_interval > 0) {
		dw_tlv->dw_interval = conf->band_cfgs[NL80211_BAND_5GHZ].awake_dw_interval;
	} else {
		/* Fallback to a default value or log a warning */
		dw_tlv->dw_interval = NAN_DEFAULT_DW_INTERVAL;
	}

	/* Validate and set NAN Discovery Beacon Interval */
	interval = conf->discovery_beacon_interval > 0 ?
		   conf->discovery_beacon_interval :
		   NAN_DEFAULT_DISC_BCN_INTERVAL;

	dw_tlv->disc_bcn_interval = cpu_to_le16(interval);

	return 0;
}

static int
mt7925_nan_cluster_id_tlv(struct sk_buff *skb, const u8 *cluster_id)
{
	struct mt7925_nan_cluster_id_tlv *cluster_tlv = NULL;
	struct tlv *tlv = NULL;

	if (!skb || !cluster_id)
		return -EINVAL;

	tlv = mt76_connac_mcu_add_tlv(skb, NAN_UNI_CMD_SET_CLUSTER_ID,
				      sizeof(struct mt7925_nan_cluster_id_tlv));

	if (!tlv)
		return -ENOMEM;

	cluster_tlv = (struct mt7925_nan_cluster_id_tlv *)tlv;

	memcpy(cluster_tlv->cluster_id, cluster_id, ETH_ALEN);

	return 0;
}

static int
mt7925_nan_sync_rssi_tlv(struct sk_buff *skb, struct cfg80211_nan_conf *conf)
{
	struct mt7925_nan_sync_rssi_tlv *rssi_tlv = NULL;
	struct tlv *tlv = NULL;

	if (!skb || !conf)
		return -EINVAL;

	tlv = mt76_connac_mcu_add_tlv(skb, NAN_UNI_CMD_SET_SYNC_RSSI,
				      sizeof(struct mt7925_nan_sync_rssi_tlv));

	if (!tlv)
		return -ENOMEM;

	rssi_tlv = (struct mt7925_nan_sync_rssi_tlv *)tlv;

	if (conf->band_cfgs[NL80211_BAND_2GHZ].chan) {
		rssi_tlv->rssi_close_2g =
			conf->band_cfgs[NL80211_BAND_2GHZ].rssi_close;
		rssi_tlv->rssi_middle_2g =
			conf->band_cfgs[NL80211_BAND_2GHZ].rssi_middle;
	}

	if (conf->band_cfgs[NL80211_BAND_5GHZ].chan) {
		rssi_tlv->rssi_close_5g =
			conf->band_cfgs[NL80211_BAND_5GHZ].rssi_close;
		rssi_tlv->rssi_middle_5g =
			conf->band_cfgs[NL80211_BAND_5GHZ].rssi_middle;
	}

	return 0;
}

/* FW rate set bit definitions (matches FW wlan_def_cmm.h) */
#define NAN_RATE_SET_BIT_1M	BIT(0)
#define NAN_RATE_SET_BIT_2M	BIT(1)
#define NAN_RATE_SET_BIT_5_5M	BIT(2)
#define NAN_RATE_SET_BIT_11M	BIT(3)
#define NAN_RATE_SET_BIT_6M	BIT(6)
#define NAN_RATE_SET_BIT_9M	BIT(7)
#define NAN_RATE_SET_BIT_12M	BIT(8)
#define NAN_RATE_SET_BIT_18M	BIT(9)
#define NAN_RATE_SET_BIT_24M	BIT(10)
#define NAN_RATE_SET_BIT_36M	BIT(11)
#define NAN_RATE_SET_BIT_48M	BIT(12)
#define NAN_RATE_SET_BIT_54M	BIT(13)

#define NAN_RATE_SET_ALL_A	(NAN_RATE_SET_BIT_6M | NAN_RATE_SET_BIT_9M | \
				 NAN_RATE_SET_BIT_12M | NAN_RATE_SET_BIT_18M | \
				 NAN_RATE_SET_BIT_24M | NAN_RATE_SET_BIT_36M | \
				 NAN_RATE_SET_BIT_48M | NAN_RATE_SET_BIT_54M)

/* 5G basic: 6M+12M+24M (OFDM) */
#define NAN_BASIC_RATE_SET_5G	(NAN_RATE_SET_BIT_6M | NAN_RATE_SET_BIT_12M | \
				 NAN_RATE_SET_BIT_24M)

/* GF_MODE_DISALLOWED = 2, RIFS_MODE_DISALLOWED = 1 */
#define NAN_GF_MODE_DISALLOWED		2
#define NAN_RIFS_MODE_DISALLOWED	1

int mt7925_nan_update_phy_setting(struct mt792x_dev *dev)
{
	struct mt76_phy *mphy = &dev->mphy;
	struct ieee80211_supported_band *sband_5g;
	struct mt7925_nan_phy_setting *phy;
	struct {
		u8 rsv[4];
		struct mt7925_nan_update_phy_setting_tlv tlv;
	} req = {};

	sband_5g = mphy->hw->wiphy->bands[NL80211_BAND_5GHZ];

	req.tlv.tag = cpu_to_le16(NAN_UNI_CMD_UPDATE_PHY_SETTING);
	req.tlv.len = cpu_to_le16(sizeof(req.tlv));

	/* 2G: ERP + HT (no CCK/HR_DSSS - NAN uses OFDM only) */
	phy = &req.tlv.phy_2g;
	phy->phy_type_set = PHY_TYPE_BIT_ERP | PHY_TYPE_BIT_HT;
	phy->non_ht_basic_phy_type = PHY_TYPE_ERP_INDEX;
	phy->use_short_preamble = 1;
	phy->use_short_slot_time = 1;
	phy->operational_rate_set = cpu_to_le16(NAN_RATE_SET_ALL_A);
	phy->bss_basic_rate_set = cpu_to_le16(NAN_BASIC_RATE_SET_5G);
	phy->gf_operation_mode = cpu_to_le32(NAN_GF_MODE_DISALLOWED);
	phy->rifs_operation_mode = cpu_to_le32(NAN_RIFS_MODE_DISALLOWED);

	/* 5G: OFDM + HT + VHT (NAN_MODE_11A) */
	phy = &req.tlv.phy_5g;
	phy->phy_type_set = PHY_TYPE_BIT_OFDM | PHY_TYPE_BIT_HT |
			    PHY_TYPE_BIT_VHT;
	phy->non_ht_basic_phy_type = PHY_TYPE_OFDM_INDEX;
	phy->use_short_preamble = 1;
	phy->use_short_slot_time = 1;
	phy->operational_rate_set = cpu_to_le16(NAN_RATE_SET_ALL_A);
	phy->bss_basic_rate_set = cpu_to_le16(NAN_BASIC_RATE_SET_5G);
	phy->gf_operation_mode = cpu_to_le32(NAN_GF_MODE_DISALLOWED);
	phy->rifs_operation_mode = cpu_to_le32(NAN_RIFS_MODE_DISALLOWED);

	/* VHT basic MCS set from sband capability */
	if (sband_5g && sband_5g->vht_cap.vht_supported)
		phy->vht_basic_mcs_set =
			sband_5g->vht_cap.vht_mcs.rx_mcs_map;

	return mt76_mcu_send_msg(&dev->mt76, MCU_UNI_CMD(NAN),
				 &req, sizeof(req), true);
}

struct ieee80211_chanctx_conf *
mt7925_nan_seed_link_sta(struct mt792x_dev *dev,
			 struct ieee80211_link_sta *link_sta)
{
	struct ieee80211_supported_band *sband_2g, *sband_5g;
	struct ieee80211_chanctx_conf *nan_ctx = NULL;
	struct ieee80211_vif *nan_vif = dev->nan_vif;

	/* Fill HT cap from 2G sband */
	sband_2g = dev->mphy.hw->wiphy->bands[NL80211_BAND_2GHZ];
	sband_5g = dev->mphy.hw->wiphy->bands[NL80211_BAND_5GHZ];
	if (sband_2g)
		link_sta->ht_cap = sband_2g->ht_cap;

	link_sta->sta->wme = true;
	link_sta->rx_nss = hweight8(dev->mphy.antenna_mask);

	/* Get chanctx from NAN schedule.
	 * Prefer 5G committed slot for wider BW (VHT), fallback
	 * to first valid slot if no 5G data slot is scheduled.
	 */
	if (nan_vif) {
		struct ieee80211_nan_channel **slots =
			nan_vif->cfg.nan_sched.schedule;
		int i;

		for (i = 0; i < CFG80211_NAN_SCHED_NUM_TIME_SLOTS; i++) {
			struct ieee80211_chanctx_conf *ctx;

			if (!slots[i] || IS_ERR(slots[i]) ||
			    !slots[i]->chanctx_conf)
				continue;

			ctx = slots[i]->chanctx_conf;
			if (!nan_ctx)
				nan_ctx = ctx;
			if (ctx->def.chan->band == NL80211_BAND_5GHZ) {
				nan_ctx = ctx;
				break;
			}
		}
	}

	/* Capability describes what the device can do and must not be
	 * filtered by the current schedule - firmware gates the VHT rate
	 * mode per the data schedule and re-derives it on schedule
	 * change, which only works if the caps are present up front.
	 */
	if (sband_5g)
		link_sta->vht_cap = sband_5g->vht_cap;

	/* Bandwidth here is the capability ceiling, not the operating
	 * width - the per-slot operating bandwidth follows the current
	 * slot channel via the firmware RLM sync, so deriving it from
	 * the schedule at STA-add time would cap a later 5 GHz schedule
	 * at the bring-up width.
	 */
	link_sta->bandwidth = IEEE80211_STA_RX_BW_80;

	return nan_ctx;
}

int mt7925_nan_enable(struct ieee80211_vif *vif,
		      struct mt792x_dev *dev,
		      struct cfg80211_nan_conf *conf)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt76_dev *mdev = &dev->mt76;
	struct mt7925_nan_common_hdr *hdr;
	struct mt7925_nan_enable_req_tlv *req;
	struct sk_buff *skb;

	if (!vif || !dev || !conf)
		return -EINVAL;

	skb = mt76_mcu_msg_alloc(mdev, NULL, MT7925_NAN_ENABLE_MAX_SIZE);
	if (!skb)
		return -ENOMEM;

	hdr = (struct mt7925_nan_common_hdr *)skb_put(skb, sizeof(*hdr));
	memset(hdr, 0, sizeof(*hdr));

	/* Set cluster id before joining cluster */
	if (mt7925_nan_cluster_id_tlv(skb, conf->cluster_id)) {
		dev_kfree_skb(skb);
		return -ENOMEM;
	}

	/* NAN enable request tlv */
	req = (struct mt7925_nan_enable_req_tlv *)
		mt76_connac_mcu_add_tlv(skb, NAN_UNI_CMD_ENABLE_REQUEST,
					sizeof(*req));
	if (!req) {
		dev_kfree_skb(skb);
		return -ENOMEM;
	}

	req->master_pref = conf->master_pref;

	mt7925_nan_set_5g_channel(dev, req, conf);
	mt7925_nan_set_scan_params(req, conf);

	if (mt7925_nan_dw_tlv(skb, conf) ||
	    mt7925_nan_sync_rssi_tlv(skb, conf)) {
		dev_kfree_skb(skb);
		return -ENOMEM;
	}

	mt7925_nan_update_conf(mvif, conf);

	return mt76_mcu_skb_send_msg(mdev, skb, MCU_UNI_CMD(NAN), true);
}

int mt7925_nan_disable(struct ieee80211_vif *vif, struct mt792x_dev *dev)
{
	struct mt76_dev *mdev = &dev->mt76;
	struct {
		u8 rsv[4];
		struct tlv nan_dis_tlv;
	} nan_cmd = {
		.rsv = { 0 },
		.nan_dis_tlv = {
			.tag = cpu_to_le16(NAN_UNI_CMD_DISABLE_REQUEST),
			.len = cpu_to_le16(sizeof(struct tlv)),
		},
	};

	if (!dev)
		return -EINVAL;

	return mt76_mcu_send_msg(mdev, MCU_UNI_CMD(NAN), &nan_cmd, sizeof(nan_cmd), true);
}

int mt7925_nan_change_configure(struct ieee80211_vif *vif,
				struct mt792x_dev *dev,
				struct cfg80211_nan_conf *conf)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt7925_nan_common_hdr *hdr = NULL;
	struct mt76_dev *mdev = &dev->mt76;
	struct sk_buff *skb = NULL;

	if (!vif || !dev || !conf)
		return -EINVAL;

	skb = mt76_mcu_msg_alloc(mdev, NULL, MT7925_NAN_CONF_MAX_SIZE);
	if (!skb)
		return -ENOMEM;

	hdr = (struct mt7925_nan_common_hdr *)skb_put(skb, sizeof(*hdr));
	memset(hdr, 0, sizeof(*hdr));

	if (mt7925_nan_mp_tlv(skb, conf->master_pref) ||
	    mt7925_nan_dw_tlv(skb, conf) ||
	    mt7925_nan_cluster_id_tlv(skb, conf->cluster_id) ||
	    mt7925_nan_sync_rssi_tlv(skb, conf)) {
		dev_kfree_skb(skb);
		return -ENOMEM;
	}

	mt7925_nan_update_conf(mvif, conf);

	return mt76_mcu_skb_send_msg(mdev, skb,
				     MCU_UNI_CMD(NAN), true);
}

static void
mt7925_nan_handle_dw_ind(struct mt792x_dev *dev, struct tlv *tlv)
{
	struct ieee80211_channel *chan;
	struct nan_rpt_dw_evt *evt;
	struct wireless_dev *wdev;
	u16 len, channel, dw_num;
	struct mt792x_vif *mvif;
	enum nl80211_band band;
	int freq;

	if (!dev || !tlv)
		return;

	len = le16_to_cpu(tlv->len);
	if (len < sizeof(*tlv) + sizeof(*evt)) {
		dev_warn(dev->mt76.dev,
			 "nan: short dw event tlv len=%u\n", len);
		return;
	}

	if (!dev->nan_vif || !ieee80211_vif_nan_started(dev->nan_vif))
		return;

	wdev = ieee80211_vif_to_wdev(dev->nan_vif);
	if (!wdev)
		return;

	mvif = (struct mt792x_vif *)dev->nan_vif->drv_priv;
	if (!mvif->nan.conf.enable_dw_notification)
		return;

	evt = (struct nan_rpt_dw_evt *)tlv->data;
	channel = le16_to_cpu(evt->channel);
	dw_num = le16_to_cpu(evt->dw_num);

	band = channel > 13 ? NL80211_BAND_5GHZ : NL80211_BAND_2GHZ;
	freq = ieee80211_channel_to_frequency(channel, band);
	chan = ieee80211_get_channel(dev->mt76.hw->wiphy, freq);
	if (!chan) {
		dev_dbg(dev->mt76.dev,
			"nan: no channel for dw end event ch=%u dw=%u\n",
			channel, dw_num);
		return;
	}

	cfg80211_next_nan_dw_notif(wdev, chan, GFP_KERNEL);
}

static void
mt7925_nan_mcu_handle_de_event(struct mt792x_dev *dev, struct tlv *tlv)
{
	u8 cluster_id[ETH_ALEN] __aligned(2) = {0x50, 0x6f, 0x9a, 0x01, 0x00, 0x00};
	struct mt7925_nan_de_event *de_evt = NULL;
	u16 len;

	if (!dev || !tlv) {
		if (dev)
			dev_warn(dev->mt76.dev, "nan: failed to parse TLV\n");
		return;
	}

	len = le16_to_cpu(tlv->len);
	if (len < sizeof(*tlv) + sizeof(*de_evt)) {
		dev_warn(dev->mt76.dev,
			 "nan: short de_event tlv len=%u\n", len);
		return;
	}

	de_evt = (struct mt7925_nan_de_event *)tlv->data;
	if (!de_evt) {
		dev_warn(dev->mt76.dev, "nan: missing DE event payload\n");
		return;
	}

	if (de_evt->event_type == NAN_EVENT_ID_DISC_MAC_ADDR)
		return;

	memcpy(cluster_id, de_evt->cluster_id, ETH_ALEN);

	dev_dbg(dev->mt76.dev, "nan: evt=%u cluster=%pM\n",
		de_evt->event_type, de_evt->cluster_id);

	if (de_evt->event_type != NAN_EVENT_ID_JOINED_CLUSTER &&
	    de_evt->event_type != NAN_EVENT_ID_STARTED_CLUSTER)
		return;

	/* STARTED_CLUSTER fires during NAN_START, before nan.started is set and
	 * before the supplicant subscribes - defer past NAN_START via the work
	 * so ieee80211_nan_cluster_joined() actually reaches userspace.
	 */
	if (de_evt->event_type == NAN_EVENT_ID_STARTED_CLUSTER) {
		dev_dbg(dev->mt76.dev,
			"nan: deferring STARTED_CLUSTER cluster=%pM\n",
			cluster_id);
		spin_lock_bh(&dev->nan_deferred_lock);
		memcpy(dev->nan_started_cluster_id, cluster_id, ETH_ALEN);
		set_bit(MT7925_NAN_DEFERRED_STARTED_CLUSTER,
			&dev->nan_deferred_pending);
		spin_unlock_bh(&dev->nan_deferred_lock);
		ieee80211_queue_work(dev->mt76.hw, &dev->nan_deferred_work);
		return;
	}

	if (!dev->nan_vif || !ieee80211_vif_nan_started(dev->nan_vif)) {
		dev_warn(dev->mt76.dev, "nan: joined-cluster event but NAN not started\n");
		return;
	}

	dev_dbg(dev->mt76.dev, "nan: anchor_master_rank=%*phN\n",
		NAN_ANCHOR_MASTER_RANK_NUM, de_evt->anchor_master_rank);

	dev_dbg(dev->mt76.dev, "nan: own_nmi=%pM master_nmi=%pM\n",
		de_evt->own_nmi, de_evt->master_nmi);

	/* joined an existing cluster, not a self-anchored new one */
	ieee80211_nan_cluster_joined(dev->nan_vif, cluster_id, false, GFP_KERNEL);
}

/* Runs the deferred NAN MCU events in process context; takes wiphy_lock
 * before nan_vif, which the NAN stop path frees under that mutex.
 */
void
mt7925_nan_deferred_work(struct work_struct *work)
{
	struct mt792x_dev *dev = container_of(work, struct mt792x_dev,
					      nan_deferred_work);
	struct ieee80211_vif *vif;
	unsigned long pending;
	u8 cluster_id[ETH_ALEN];

	spin_lock_bh(&dev->nan_deferred_lock);
	pending = dev->nan_deferred_pending;
	dev->nan_deferred_pending = 0;
	memcpy(cluster_id, dev->nan_started_cluster_id, ETH_ALEN);
	spin_unlock_bh(&dev->nan_deferred_lock);

	if (!pending)
		return;

	wiphy_lock(dev->mt76.hw->wiphy);
	vif = dev->nan_vif;
	if (!vif || !ieee80211_vif_nan_started(vif))
		goto out;

	if (test_bit(MT7925_NAN_DEFERRED_STARTED_CLUSTER, &pending))
		ieee80211_nan_cluster_joined(vif, cluster_id, true, GFP_KERNEL);

	if (test_bit(MT7925_NAN_DEFERRED_SCHED_UPDATE_DONE, &pending))
		ieee80211_nan_sched_update_done(vif);
out:
	wiphy_unlock(dev->mt76.hw->wiphy);
}

static void
mt7925_nan_handle_ulw_update(struct mt792x_dev *dev, struct tlv *tlv)
{
	struct mt7925_nan_ulw_event *evt;
	struct mt7925_nan_ulw_attr attr;
	struct wireless_dev *wdev;
	u16 len;

	if (!dev || !tlv)
		return;

	if (!dev->nan_vif || !ieee80211_vif_nan_started(dev->nan_vif))
		return;

	len = le16_to_cpu(tlv->len);
	if (len < sizeof(*tlv) + sizeof(*evt)) {
		dev_warn(dev->mt76.dev,
			 "nan: short ulw event tlv len=%u\n", len);
		return;
	}

	evt = (struct mt7925_nan_ulw_event *)tlv->data;
	wdev = ieee80211_vif_to_wdev(dev->nan_vif);
	if (!wdev)
		return;

	dev_dbg(dev->mt76.dev,
		"nan: ulw_update wdev=%p owner_nlportid=%u sched_id=%u seq=%u dur=%u\n",
		wdev, wdev->owner_nlportid,
		evt->sched_id, evt->seq_id, le32_to_cpu(evt->duration));

	/* Reorder the FW fields into NAN spec Table 109 attribute layout */
	attr.attr_id = NAN_ULW_ATTR_ID;
	attr.length = cpu_to_le16(NAN_ULW_FIXED_PAYLOAD);
	attr.sched_id = evt->sched_id;
	attr.seq_id = evt->seq_id;
	attr.start_time = evt->start_time;
	attr.duration = evt->duration;
	attr.period = evt->period;
	attr.count_down = evt->count_down;
	attr.ulw_overwrite = evt->ulw_overwrite;

	cfg80211_nan_ulw_update(wdev, (const u8 *)&attr, sizeof(attr),
				GFP_KERNEL);
}

static void
mt7925_nan_handle_sched_update_done(struct mt792x_dev *dev, struct tlv *tlv)
{
	struct ieee80211_vif *vif;

	if (!dev || !tlv)
		return;

	vif = dev->nan_vif;
	if (!vif || !ieee80211_vif_nan_started(vif))
		return;

	/* Runs in the BH-disabled MCU-event RX path; the mac80211 helper needs
	 * the wiphy mutex and may sleep, so hand it to the work instead.
	 */
	spin_lock_bh(&dev->nan_deferred_lock);
	set_bit(MT7925_NAN_DEFERRED_SCHED_UPDATE_DONE, &dev->nan_deferred_pending);
	spin_unlock_bh(&dev->nan_deferred_lock);
	ieee80211_queue_work(dev->mt76.hw, &dev->nan_deferred_work);
}

void mt7925_nan_mcu_event(struct mt792x_dev *dev, struct sk_buff *skb)
{
	struct tlv *tlv;
	u32 tlv_len;

	if (!dev || !skb)
		return;

	if (skb->len < sizeof(struct mt7925_mcu_rxd) + 4)
		return;

	skb_pull(skb, sizeof(struct mt7925_mcu_rxd) + 4);
	tlv = (struct tlv *)skb->data;
	tlv_len = skb->len;

	while (tlv_len >= sizeof(*tlv)) {
		u16 len = le16_to_cpu(tlv->len);

		if (len < sizeof(*tlv) || len > tlv_len)
			break;

		switch (le16_to_cpu(tlv->tag)) {
		case NAN_UNI_EVENT_ID_DE_EVENT_IND:
			mt7925_nan_mcu_handle_de_event(dev, tlv);
			break;
		case NAN_UNI_EVENT_REPORT_DW_START:
			mt7925_nan_handle_dw_ind(dev, tlv);
			break;
		case NAN_UNI_EVENT_ID_ULW_UPDATE:
			mt7925_nan_handle_ulw_update(dev, tlv);
			break;
		case NAN_UNI_EVENT_ID_SCHED_UPDATE_DONE:
			mt7925_nan_handle_sched_update_done(dev, tlv);
			break;
		default:
			break;
		}

		tlv_len -= len;
		tlv = (struct tlv *)((u8 *)tlv + len);
	}
}

static int mt7925_nan_avail_ctrl_tlv(struct sk_buff *skb,
				     struct ieee80211_vif *vif)
{
	struct mt7925_nan_avail_ctrl_tlv *avail_ctrl_tlv;
	struct ieee80211_nan_sched_cfg *sched;
	struct tlv *tlv;
	u8 seq_id = 0;
	u16 ctrl = 0;

	if (!skb || !vif)
		return -EINVAL;

	tlv = mt76_connac_mcu_add_tlv(skb, NAN_UNI_CMD_UPDATE_AVAILABILITY_CTRL,
				      sizeof(struct mt7925_nan_avail_ctrl_tlv));

	if (!tlv)
		return -ENOMEM;

	sched = &vif->cfg.nan_sched;

	ctrl = mt7925_nan_avail_attr_ctrl(sched);
	if (sched->avail_blob_len >= NAN_AVAIL_ATTR_CTRL_OFFSET + 2)
		seq_id = sched->avail_blob[NAN_AVAIL_SEQ_ID_OFFSET];

	avail_ctrl_tlv = (struct mt7925_nan_avail_ctrl_tlv *)tlv;
	avail_ctrl_tlv->avail_ctrl =
		cpu_to_le16(ctrl & NAN_AVAIL_CTRL_CHECK_FOR_CHANGED);
	avail_ctrl_tlv->seq_id = seq_id;
	avail_ctrl_tlv->is_deferred = sched->deferred ? 1 : 0;

	return 0;
}

static u32 mt7925_nan_slot_to_bitmap(struct ieee80211_vif *vif,
				     struct mt7925_nan_ch_timeline *ch_list)
{
	struct ieee80211_nan_channel **slots = vif->cfg.nan_sched.schedule;
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	u32 num_channels = 0;
	u32 i, j;

	for (i = 0; i < ARRAY_SIZE(mvif->nan.local_sched); i++) {
		struct cfg80211_chan_def *slot_chan = &mvif->nan.local_sched[i];
		struct ieee80211_nan_channel *slot = slots[i];
		bool is_found = false;

		if (slot && !IS_ERR(slot) && slot->chanctx_conf) {
			*slot_chan = slot->chanctx_conf->def;
		} else {
			memset(slot_chan, 0, sizeof(*slot_chan));
			continue;
		}

		for (j = 0; j < num_channels; j++) {
			u32 raw = le32_to_cpu(ch_list[j].ch_info);

			if (FIELD_GET(NAN_CH_CTRL_PRIMARY_CH, raw) ==
			    slot_chan->chan->hw_value) {
				u32 dw;

				for (dw = 0; dw < NAN_TOTAL_DW; dw++) {
					u32 map = le32_to_cpu(ch_list[j].avail_map[dw]);

					ch_list[j].avail_map[dw] =
						cpu_to_le32(map | BIT(i));
				}
				le32_add_cpu(&ch_list[j].num, 1);
				is_found = true;
				break;
			}
		}

		if (!is_found && num_channels < NAN_TIMELINE_MGMT_CHNL_LIST_NUM) {
			u32 dw;

			ch_list[num_channels].ch_info =
				cpu_to_le32(FIELD_PREP(NAN_CH_CTRL_CH_TYPE,
						       NAN_BAND_CHANNEL_ENTRY_LIST_TYPE_CHANNEL) |
					    FIELD_PREP(NAN_CH_CTRL_OP_CLASS,
						       slot->channel_entry[0]) |
					    FIELD_PREP(NAN_CH_CTRL_PRIMARY_CH,
						       slot_chan->chan->hw_value));
			for (dw = 0; dw < NAN_TOTAL_DW; dw++)
				ch_list[num_channels].avail_map[dw] =
					cpu_to_le32(BIT(i));
			le32_add_cpu(&ch_list[num_channels].num, 1);
			ch_list[num_channels].is_valid++;
			num_channels++;
		}
	}

	return num_channels;
}

static int mt7925_nan_avail_tlv(struct sk_buff *skb,
				struct ieee80211_vif *vif)
{
	struct mt7925_nan_avail_entry_tlv *avail_tlv;
	struct ieee80211_nan_sched_cfg *sched;
	struct tlv *tlv;
	u16 ctrl = 0;

	if (!skb || !vif)
		return -EINVAL;

	tlv = mt76_connac_mcu_add_tlv(skb, NAN_UNI_CMD_UPDATE_AVAILABILITY,
				      sizeof(struct mt7925_nan_avail_entry_tlv));

	if (!tlv)
		return -ENOMEM;

	sched = &vif->cfg.nan_sched;

	ctrl = mt7925_nan_avail_attr_ctrl(sched);

	avail_tlv = (struct mt7925_nan_avail_entry_tlv *)tlv;
	avail_tlv->map_id = ctrl & NAN_AVAIL_CTRL_MAPID;
	avail_tlv->is_cond_avail = false;
	avail_tlv->timeline_idx = 0;

	mt7925_nan_slot_to_bitmap(vif, avail_tlv->ch_list);

	avail_tlv->is_multi_map = false;

	return 0;
}

void mt7925_nan_local_sched_changed(struct mt792x_dev *dev,
				    struct ieee80211_vif *vif)
{
	struct mt7925_nan_common_hdr *hdr;
	struct mt76_dev *mdev;
	struct sk_buff *skb;

	if (!dev || !vif)
		return;

	mdev = &dev->mt76;

	mt792x_mutex_acquire(dev);

	skb = mt76_mcu_msg_alloc(mdev, NULL, MT7925_NAN_AVAIL_MAX_SIZE);
	if (!skb)
		goto out;

	hdr = (struct mt7925_nan_common_hdr *)skb_put(skb, sizeof(*hdr));
	memset(hdr, 0, sizeof(*hdr));

	if (mt7925_nan_avail_ctrl_tlv(skb, vif) ||
	    mt7925_nan_avail_tlv(skb, vif)) {
		dev_kfree_skb(skb);
		goto out;
	}

	mt76_mcu_skb_send_msg(mdev, skb, MCU_UNI_CMD(NAN), false);
out:
	mt792x_mutex_release(dev);
}

static int mt7925_nan_peer_rec_tlv(struct sk_buff *skb,
				   struct ieee80211_sta *sta,
				   struct mt792x_sta *msta,
				   u8 is_activate)
{
	struct mt7925_nan_sched_manage_peer_rec_tlv *peer_rec_tlv;
	struct tlv *tlv;

	if (!skb || !sta || !msta)
		return -EINVAL;

	tlv = mt76_connac_mcu_add_tlv(skb, NAN_UNI_CMD_MANAGE_PEER_SCH_RECORD,
				      sizeof(struct mt7925_nan_sched_manage_peer_rec_tlv));

	if (!tlv)
		return -ENOMEM;

	peer_rec_tlv = (struct mt7925_nan_sched_manage_peer_rec_tlv *)tlv;
	peer_rec_tlv->sch_idx = cpu_to_le32(msta->nan_sched.sch_idx);
	peer_rec_tlv->is_activate = is_activate;
	memcpy(peer_rec_tlv->nmi_addr, sta->addr, ETH_ALEN);

	return 0;
}

static int mt7925_nan_peer_cap_tlv(struct sk_buff *skb,
				   struct ieee80211_sta *sta,
				   struct mt792x_sta *msta)
{
	struct mt7925_nan_sched_update_peer_cap_tlv *peer_cap_tlv;
	struct ieee80211_nan_peer_sched *sched;
	enum nl80211_band band;
	struct tlv *tlv;
	u16 primary_ch;
	u32 i;

	if (!skb || !sta || !msta)
		return -EINVAL;

	sched = sta->nan_sched;
	if (!sched)
		return -EINVAL;

	tlv = mt76_connac_mcu_add_tlv(skb, NAN_UNI_CMD_UPDATE_PEER_CAPABILITY,
				      sizeof(struct mt7925_nan_sched_update_peer_cap_tlv));

	if (!tlv)
		return -ENOMEM;

	peer_cap_tlv = (struct mt7925_nan_sched_update_peer_cap_tlv *)tlv;
	peer_cap_tlv->sch_idx = cpu_to_le32(msta->nan_sched.sch_idx);
	peer_cap_tlv->supported_bands = BIT(NAN_SUPPORTED_BAND_ID_2P4G);
	peer_cap_tlv->max_chnl_switch_time = cpu_to_le16(sched->max_chan_switch);

	for (i = 0; i < sched->n_channels; i++) {
		if (!sched->channels[i].chanctx_conf)
			continue;

		band = sched->channels[i].chanctx_conf->def.chan->band;
		primary_ch =
			sched->channels[i].chanctx_conf->def.chan->hw_value;

		if (band == NL80211_BAND_2GHZ)
			peer_cap_tlv->peer_supported_bands |=
				BIT(NAN_SUPPORTED_BN_2G);
		else if (primary_ch >= UNII1_LOWER_BOUND &&
			 primary_ch <= UNII1_UPPER_BOUND)
			peer_cap_tlv->peer_supported_bands |=
				BIT(NAN_SUPPORTED_BN_5G_LOW);
		else if (primary_ch >= UNII3_LOWER_BOUND &&
			 primary_ch <= UNII3_UPPER_BOUND)
			peer_cap_tlv->peer_supported_bands |=
				BIT(NAN_SUPPORTED_BN_5G_HIGH);
	}

	return 0;
}

static void
mt7925_nan_fill_crb_committed(struct mt7925_nan_sched_update_crb_tlv *crb_tlv,
			      struct ieee80211_nan_peer_sched *sched)
{
	u32 m, slot;

	if (!sched)
		return;

	for (m = 0; m < CFG80211_NAN_MAX_PEER_MAPS &&
	     m < NAN_TIMELINE_MGMT_SIZE; m++) {
		struct ieee80211_nan_peer_map *map = &sched->maps[m];
		struct mt7925_nan_sched_timeline *tl =
			&crb_tlv->comm_faw_timeline[m];

		if (map->map_id == CFG80211_NAN_INVALID_MAP_ID)
			continue;

		tl->map_id = map->map_id;

		/*
		 * Convert peer schedule slots to FW avail_map bitmap.
		 * Each bit represents one time slot where the peer has
		 * committed availability. Fill all DW intervals the same.
		 */
		for (slot = 0; slot < CFG80211_NAN_SCHED_NUM_TIME_SLOTS;
		     slot++) {
			struct ieee80211_nan_channel *ch = map->slots[slot];
			u32 dw;

			if (!ch || !ch->chanctx_conf)
				continue;

			for (dw = 0; dw < NAN_TOTAL_DW; dw++)
				tl->avail_map[dw] |= cpu_to_le32(BIT(slot));
		}
	}
}

static int mt7925_nan_update_crb_tlv(struct sk_buff *skb,
				     struct ieee80211_sta *sta,
				     struct mt792x_sta *msta)
{
	struct mt7925_nan_sched_update_crb_tlv *crb_tlv;
	struct tlv *tlv;

	if (!skb || !sta || !msta)
		return -EINVAL;

	tlv = mt76_connac_mcu_add_tlv(skb, NAN_UNI_CMD_UPDATE_CRB,
				      sizeof(struct mt7925_nan_sched_update_crb_tlv));

	if (!tlv)
		return -ENOMEM;

	crb_tlv = (struct mt7925_nan_sched_update_crb_tlv *)tlv;
	crb_tlv->sch_idx = cpu_to_le32(msta->nan_sched.sch_idx);
	crb_tlv->flags = NAN_CRB_USE_DATA_PATH;
	crb_tlv->is_use_ranging = false;
	crb_tlv->comm_ndc_ctrl.is_valid = false;

	mt7925_nan_fill_crb_committed(crb_tlv, sta->nan_sched);

	return 0;
}

static int
mt7925_nan_peer_ulw_tlv(struct sk_buff *skb,
			struct ieee80211_sta *sta,
			struct mt792x_sta *msta)
{
	struct mt7925_nan_update_ulw_tlv *ulw_tlv = NULL;
	struct ieee80211_nan_peer_sched *sched = NULL;
	struct tlv *tlv = NULL;

	if (!skb || !sta || !msta)
		return -EINVAL;

	sched = sta->nan_sched;
	if (!sched || !sched->init_ulw || !sched->ulw_size)
		return 0; /* No ULW to send, not an error */

	if (sched->ulw_size > NAN_ULW_MAX_SIZE)
		return -EINVAL;

	tlv = mt76_connac_mcu_add_tlv(skb, NAN_UNI_CMD_UPDATE_ULW,
				      sizeof(struct mt7925_nan_update_ulw_tlv));
	if (!tlv)
		return -ENOMEM;

	ulw_tlv = (struct mt7925_nan_update_ulw_tlv *)tlv;
	ether_addr_copy(ulw_tlv->nmi_addr, sta->addr);
	memcpy(ulw_tlv->ulw_attr, sched->init_ulw, sched->ulw_size);

	return 0;
}

int mt792x_nan_set_peer_schedule(struct mt792x_dev *dev,
				 struct ieee80211_sta *sta)
{
	struct mt7925_nan_common_hdr *hdr;
	bool idx_allocated = false;
	struct mt792x_sta *msta;
	struct mt792x_nan *nan;
	struct mt76_dev *mdev;
	struct sk_buff *skb;
	int ret;

	if (!dev || !sta)
		return -EINVAL;

	mdev = &dev->mt76;

	skb = mt76_mcu_msg_alloc(mdev, NULL, MT7925_NAN_PEER_MAX_SIZE);
	if (!skb)
		return -ENOMEM;

	hdr = (struct mt7925_nan_common_hdr *)skb_put(skb, sizeof(*hdr));
	memset(hdr, 0, sizeof(*hdr));

	msta = (struct mt792x_sta *)sta->drv_priv;
	nan = &msta->vif->nan;

	/* Allocate connection index on first call for this peer */
	if (!msta->nan_sched.idx_assigned) {
		int idx = find_first_zero_bit(&nan->conn_bitmap,
					      NAN_MAX_CONN_CFG);
		if (idx >= NAN_MAX_CONN_CFG) {
			dev_kfree_skb(skb);
			return -ENOSPC;
		}

		set_bit(idx, &nan->conn_bitmap);
		msta->nan_sched.sch_idx = idx;
		msta->nan_sched.idx_assigned = true;
		idx_allocated = true;

		if (mt7925_nan_peer_rec_tlv(skb, sta, msta, true) ||
		    mt7925_nan_peer_cap_tlv(skb, sta, msta)) {
			ret = -ENOMEM;
			goto free_skb;
		}
	}

	if (mt7925_nan_update_crb_tlv(skb, sta, msta)) {
		ret = -ENOMEM;
		goto free_skb;
	}

	ret = mt7925_nan_peer_ulw_tlv(skb, sta, msta);
	if (ret)
		goto free_skb;

	ret = mt76_mcu_skb_send_msg(mdev, skb, MCU_UNI_CMD(NAN), true);
	if (ret && idx_allocated)
		goto clear_idx;

	return ret;

free_skb:
	dev_kfree_skb(skb);
	if (!idx_allocated)
		return ret;

clear_idx:
	clear_bit(msta->nan_sched.sch_idx, &nan->conn_bitmap);
	msta->nan_sched.idx_assigned = false;

	return ret;
}

int mt792x_nan_set_peer_rec(struct mt76_dev *mdev,
			    struct ieee80211_sta *sta)
{
	struct mt7925_nan_sched_update_crb_tlv *crb_tlv;
	struct mt7925_nan_common_hdr *hdr;
	struct mt792x_sta *msta;
	struct mt792x_nan *nan;
	struct sk_buff *skb;
	struct tlv *tlv;
	int ret;

	if (!mdev || !sta)
		return -EINVAL;

	skb = mt76_mcu_msg_alloc(mdev, NULL, MT7925_NAN_PEER_MAX_SIZE);
	if (!skb)
		return -ENOMEM;

	hdr = (struct mt7925_nan_common_hdr *)skb_put(skb, sizeof(*hdr));
	memset(hdr, 0, sizeof(*hdr));

	msta = (struct mt792x_sta *)sta->drv_priv;
	nan = &msta->vif->nan;

	if (!msta->nan_sched.idx_assigned) {
		dev_kfree_skb(skb);
		return 0;
	}

	/* Send a zero-avail_map CRB TLV before deactivating the peer record so
	 * firmware clears the committed schedule slots for this peer.  Without
	 * this, stale CRB entries linger and cause scheduling conflicts for
	 * subsequent NDP connections that reuse the same sch_idx.
	 */
	tlv = mt76_connac_mcu_add_tlv(skb, NAN_UNI_CMD_UPDATE_CRB,
				      sizeof(struct mt7925_nan_sched_update_crb_tlv));
	if (!tlv) {
		dev_kfree_skb(skb);
		return -ENOMEM;
	}
	crb_tlv = (struct mt7925_nan_sched_update_crb_tlv *)tlv;
	crb_tlv->sch_idx = cpu_to_le32(msta->nan_sched.sch_idx);
	crb_tlv->flags = NAN_CRB_USE_DATA_PATH;
	crb_tlv->is_use_ranging = false;
	crb_tlv->comm_ndc_ctrl.is_valid = false;
	/* avail_map is zero-initialised by mt76_connac_mcu_add_tlv */

	/* Deactivate peer record and release connection index */
	if (mt7925_nan_peer_rec_tlv(skb, sta, msta, false)) {
		dev_kfree_skb(skb);
		return -ENOMEM;
	}

	ret = mt76_mcu_skb_send_msg(mdev, skb, MCU_UNI_CMD(NAN), true);
	if (ret)
		return ret;

	clear_bit(msta->nan_sched.sch_idx, &nan->conn_bitmap);
	msta->nan_sched.idx_assigned = false;

	return 0;
}

int mt792x_nan_map_sta_rec(struct mt76_dev *mdev,
			   struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta)
{
	struct mt7925_nan_sched_map_sta_rec_tlv *map_tlv;
	struct mt7925_nan_common_hdr *hdr;
	struct ieee80211_sta *nmi_sta;
	struct mt792x_sta *nmi_msta;
	struct mt792x_sta *msta;
	u8 nmi_addr[ETH_ALEN];
	struct sk_buff *skb;
	int ndp_ctx_id = 0;
	int ret = -ENOMEM;
	struct tlv *tlv;

	if (!mdev || !vif || !sta)
		return -EINVAL;

	msta = (struct mt792x_sta *)sta->drv_priv;

	rcu_read_lock();
	nmi_sta = rcu_dereference(sta->nmi);
	if (!nmi_sta) {
		rcu_read_unlock();
		dev_err(mdev->dev, "NAN: NMI sta not found for NDI sta %pM\n",
			sta->addr);
		return -EINVAL;
	}

	memcpy(nmi_addr, nmi_sta->addr, ETH_ALEN);
	nmi_msta = (struct mt792x_sta *)nmi_sta->drv_priv;

	ndp_ctx_id = find_first_zero_bit(&nmi_msta->nan_sched.ndp_ctx_bitmap,
					 NAN_MAX_NDP_CXT);
	if (ndp_ctx_id >= NAN_MAX_NDP_CXT) {
		rcu_read_unlock();
		return -ENOSPC;
	}

	set_bit(ndp_ctx_id, &nmi_msta->nan_sched.ndp_ctx_bitmap);
	rcu_read_unlock();

	msta->nan_sched.ndp_ctx_id = ndp_ctx_id;
	msta->nan_sched.ndp_ctx_assigned = true;

	skb = mt76_mcu_msg_alloc(mdev, NULL,
				 sizeof(struct mt7925_nan_common_hdr) +
				 sizeof(struct mt7925_nan_sched_map_sta_rec_tlv));
	if (!skb)
		goto clear_ndp_ctx;

	hdr = (struct mt7925_nan_common_hdr *)skb_put(skb, sizeof(*hdr));
	memset(hdr, 0, sizeof(*hdr));

	tlv = mt76_connac_mcu_add_tlv(skb, NAN_UNI_CMD_MAP_STA_RECORD,
				      sizeof(struct mt7925_nan_sched_map_sta_rec_tlv));
	if (!tlv) {
		dev_kfree_skb(skb);
		ret = -ENOMEM;
		goto clear_ndp_ctx;
	}

	map_tlv = (struct mt7925_nan_sched_map_sta_rec_tlv *)tlv;
	memcpy(map_tlv->nmi_addr, nmi_addr, ETH_ALEN);
	map_tlv->sta_rec_idx = msta->deflink.wcid.idx;
	map_tlv->ndp_ctx_id = ndp_ctx_id;
	map_tlv->role_idx = cpu_to_le32(NAN_BSS_INDEX_BAND0);
	memcpy(map_tlv->ndi_addr, vif->addr, ETH_ALEN);

	ret = mt76_mcu_skb_send_msg(mdev, skb,
				    MCU_UNI_CMD(NAN), true);
	if (ret)
		goto clear_ndp_ctx;

	return 0;

clear_ndp_ctx:
	rcu_read_lock();
	nmi_sta = rcu_dereference(sta->nmi);
	if (nmi_sta) {
		nmi_msta = (struct mt792x_sta *)nmi_sta->drv_priv;
		clear_bit(msta->nan_sched.ndp_ctx_id,
			  &nmi_msta->nan_sched.ndp_ctx_bitmap);
	}
	rcu_read_unlock();
	msta->nan_sched.ndp_ctx_assigned = false;

	return ret ?: -ENOMEM;
}
