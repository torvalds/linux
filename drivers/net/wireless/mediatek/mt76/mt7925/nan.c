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

	req->config_support_5g = 1;
	req->support_5g_val = 1;
	req->config_5g_channel = 1;

	if (chan->hw_value == NAN_5G_LOW_DISC_CHANNEL)
		ch5g |= BIT(0);
	else if (chan->hw_value == NAN_5G_HIGH_DISC_CHANNEL)
		ch5g |= BIT(1);

	req->channel_5g_val = cpu_to_le32(ch5g);
}

static void mt7925_nan_set_2g_support(struct mt7925_nan_enable_req_tlv *req,
				      struct cfg80211_nan_conf *conf)
{
	if (!conf->band_cfgs[NL80211_BAND_2GHZ].chan)
		return;

	req->config_2dot4g_support = 1;
	req->support_2dot4g_val = 1;
}

static void mt7925_nan_set_cluster_id(struct mt7925_nan_enable_req_tlv *req,
				      const u8 *cluster_id)
{
	if (!cluster_id)
		return;

	req->cluster_high = cpu_to_le16(cluster_id[4] | cluster_id[5] << 8);
	req->cluster_low = cpu_to_le16((u16)cluster_id[3]);
}

static void mt7925_nan_set_dw_interval(struct mt7925_nan_enable_req_tlv *req,
				       struct cfg80211_nan_conf *conf)
{
	if (conf->band_cfgs[NL80211_BAND_2GHZ].awake_dw_interval > 0) {
		req->config_dw.config_2dot4g_dw_band = 1;
		req->config_dw.dw_2dot4g_interval_val =
			cpu_to_le32(conf->band_cfgs[NL80211_BAND_2GHZ].awake_dw_interval);
	}

	if (conf->band_cfgs[NL80211_BAND_5GHZ].awake_dw_interval > 0) {
		req->config_dw.config_5g_dw_band = 1;
		req->config_dw.dw_5g_interval_val =
			cpu_to_le32(conf->band_cfgs[NL80211_BAND_5GHZ].awake_dw_interval);
	}
}

static void mt7925_nan_set_disc_beacon(struct mt7925_nan_enable_req_tlv *req,
				       struct cfg80211_nan_conf *conf)
{
	if (conf->discovery_beacon_interval > 0) {
		req->config_2dot4g_beacons = true;
		req->beacon_2dot4g_val = conf->discovery_beacon_interval;
	}
}

static void mt7925_nan_set_rssi_thresholds(struct mt7925_nan_enable_req_tlv *req,
					   struct cfg80211_nan_conf *conf)
{
	if (conf->band_cfgs[NL80211_BAND_2GHZ].chan) {
		req->config_2dot4g_rssi_close = 1;
		req->rssi_close_2dot4g_val =
			abs(conf->band_cfgs[NL80211_BAND_2GHZ].rssi_close);
		req->config_2dot4g_rssi_middle = 1;
		req->rssi_middle_2dot4g_val =
			abs(conf->band_cfgs[NL80211_BAND_2GHZ].rssi_middle);
	}

	if (conf->band_cfgs[NL80211_BAND_5GHZ].chan) {
		req->config_5g_rssi_close = 1;
		req->rssi_close_5g_val =
			abs(conf->band_cfgs[NL80211_BAND_5GHZ].rssi_close);
		req->config_5g_rssi_middle = 1;
		req->rssi_middle_5g_val =
			abs(conf->band_cfgs[NL80211_BAND_5GHZ].rssi_middle);
	}
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

int mt7925_nan_set_nmi_addr(struct mt792x_dev *dev, const u8 *addr)
{
	struct mt76_dev *mdev;
	struct {
		u8 rsv[4];
		struct mt7925_nan_nmi_addr_tlv nmi_addr_tlv;
	} nmi_cmd = {
		.rsv = { 0 },
		.nmi_addr_tlv = {
			.tag = cpu_to_le16(NAN_UNI_CMD_CHANGE_NMI_ADDRESS),
			.len = cpu_to_le16(sizeof(struct mt7925_nan_nmi_addr_tlv)),
		},
	};
	int ret;

	if (!dev || !addr)
		return -EINVAL;

	if (is_zero_ether_addr(addr) || is_multicast_ether_addr(addr)) {
		dev_err(dev->mt76.dev, "NAN: invalid NMI address %pM\n", addr);
		return -EINVAL;
	}

	mdev = &dev->mt76;
	memcpy(nmi_cmd.nmi_addr_tlv.nmi_addr, addr, ETH_ALEN);

	ret = mt76_mcu_send_msg(mdev, MCU_UNI_CMD(NAN), &nmi_cmd,
				sizeof(nmi_cmd), true);

	return ret;
}

int mt7925_nan_enable(struct ieee80211_vif *vif,
		      struct mt792x_dev *dev,
		      struct cfg80211_nan_conf *conf)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt76_dev *mdev = &dev->mt76;
	struct {
		u8 rsv[4];
		struct mt7925_nan_enable_req_tlv nan_req_tlv;
	} nan_cmd = {
		.rsv = { 0 },
		.nan_req_tlv = {
			.tag = cpu_to_le16(NAN_UNI_CMD_ENABLE_REQUEST),
			.len = cpu_to_le16(sizeof(struct mt7925_nan_enable_req_tlv)),
			.config_random_factor_force = 0,
			.random_factor_force_val = 0,
			.config_hop_count_force = 0,
			.hop_count_force_val = 0,
		},
	};
	struct mt7925_nan_enable_req_tlv *p_nan_req_tlv = &nan_cmd.nan_req_tlv;
	int ret;

	if (!vif || !dev || !conf)
		return -EINVAL;

	p_nan_req_tlv->master_pref = conf->master_pref;

	mt7925_nan_set_2g_support(p_nan_req_tlv, conf);
	mt7925_nan_set_5g_channel(dev, p_nan_req_tlv, conf);
	mt7925_nan_set_cluster_id(p_nan_req_tlv, conf->cluster_id);
	mt7925_nan_set_dw_interval(p_nan_req_tlv, conf);
	mt7925_nan_set_disc_beacon(p_nan_req_tlv, conf);
	mt7925_nan_set_rssi_thresholds(p_nan_req_tlv, conf);
	mt7925_nan_set_scan_params(p_nan_req_tlv, conf);

	mt7925_nan_update_conf(mvif, conf);

	ret = mt76_mcu_send_msg(mdev, MCU_UNI_CMD(NAN), &nan_cmd, sizeof(nan_cmd), true);

	return ret;
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

	if (master_pref > NAN_MAX_MASTER_PREFERENCE)
		return 0;

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

	if (de_evt->event_type != NAN_EVENT_ID_JOINED_CLUSTER)
		return;

	if (!dev->nan_vif || !ieee80211_vif_nan_started(dev->nan_vif)) {
		dev_warn(dev->mt76.dev, "nan: joined-cluster event but NAN not started\n");
		return;
	}

	dev_dbg(dev->mt76.dev, "nan: anchor_master_rank=%*phN\n",
		NAN_ANCHOR_MASTER_RANK_NUM, de_evt->anchor_master_rank);

	dev_dbg(dev->mt76.dev, "nan: own_nmi=%pM master_nmi=%pM\n",
		de_evt->own_nmi, de_evt->master_nmi);

	ieee80211_nan_cluster_joined(dev->nan_vif, cluster_id, true, GFP_KERNEL);
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
		case NAN_UNI_EVENT_REPORT_DW_END:
			mt7925_nan_handle_dw_ind(dev, tlv);
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
				u32 map = le32_to_cpu(ch_list[j].avail_map[0]);

				ch_list[j].avail_map[0] = cpu_to_le32(map | BIT(i));
				le32_add_cpu(&ch_list[j].num, 1);
				is_found = true;
				break;
			}
		}

		if (!is_found && num_channels < NAN_TIMELINE_MGMT_CHNL_LIST_NUM) {
			ch_list[num_channels].ch_info =
				cpu_to_le32(FIELD_PREP(NAN_CH_CTRL_OP_CLASS,
						       slot->channel_entry[0]) |
					    FIELD_PREP(NAN_CH_CTRL_PRIMARY_CH,
						       slot_chan->chan->hw_value));
			ch_list[num_channels].avail_map[0] = cpu_to_le32(BIT(i));
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
	bool deferred;
	struct sk_buff *skb;
	int ret = -ENOMEM;

	if (!dev || !vif)
		return;

	mdev = &dev->mt76;
	deferred = vif->cfg.nan_sched.deferred;

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

	ret = mt76_mcu_skb_send_msg(mdev, skb,
				    MCU_UNI_CMD(NAN), true);
out:
	mt792x_mutex_release(dev);

	if (deferred) {
		if (ret)
			dev_err(mdev->dev,
				"NAN: local schedule update failed: %d\n",
				ret);

		ieee80211_nan_sched_update_done(vif);
	}
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

static u8 mt7925_nan_get_supported_bands(struct mt792x_vif *mvif)
{
	struct wiphy *wiphy;
	u8 bands = 0;

	if (!mvif || !mvif->phy)
		return BIT(NAN_SUPPORTED_BAND_ID_2P4G);

	wiphy = mvif->phy->mt76->hw->wiphy;
	if (wiphy->nan_supported_bands & BIT(NL80211_BAND_2GHZ))
		bands |= BIT(NAN_SUPPORTED_BAND_ID_2P4G);
	if (wiphy->nan_supported_bands & BIT(NL80211_BAND_5GHZ))
		bands |= BIT(NAN_SUPPORTED_BAND_ID_5G);

	return bands ?: BIT(NAN_SUPPORTED_BAND_ID_2P4G);
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
	peer_cap_tlv->supported_bands =
		mt7925_nan_get_supported_bands(msta->vif);
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
			      struct ieee80211_vif *vif,
			      struct ieee80211_nan_peer_sched *sched)
{
	struct ieee80211_nan_sched_cfg *local_sched;
	u8 local_map_id;
	u32 m, slot;

	if (!vif || !sched)
		return;

	local_sched = &vif->cfg.nan_sched;
	local_map_id = mt7925_nan_avail_attr_ctrl(local_sched) &
		       NAN_AVAIL_CTRL_MAPID;

	for (m = 0; m < CFG80211_NAN_MAX_PEER_MAPS &&
	     m < NAN_TIMELINE_MGMT_SIZE; m++) {
		struct mt7925_nan_sched_timeline *tl =
			&crb_tlv->comm_faw_timeline[m];
		struct ieee80211_nan_peer_map *map = &sched->maps[m];
		u32 avail_map = 0;

		if (map->map_id == CFG80211_NAN_INVALID_MAP_ID)
			continue;

		tl->map_id = map->map_id;
		tl->local_map_id = local_map_id;

		for (slot = 0; slot < CFG80211_NAN_SCHED_NUM_TIME_SLOTS;
		     slot++) {
			struct ieee80211_nan_channel *local_ch;
			struct ieee80211_nan_channel *peer_ch;

			local_ch = local_sched->schedule[slot];
			peer_ch = map->slots[slot];

			if (!local_ch || !local_ch->chanctx_conf ||
			    !peer_ch || !peer_ch->chanctx_conf)
				continue;

			if (local_ch->chanctx_conf != peer_ch->chanctx_conf)
				continue;

			avail_map |= BIT(slot);
		}

		tl->avail_map[0] = cpu_to_le32(avail_map);
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

	mt7925_nan_fill_crb_committed(crb_tlv, msta->vif->phy->dev->nan_vif,
				      sta->nan_sched);

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
	struct mt7925_nan_common_hdr *hdr;
	struct mt792x_sta *msta;
	struct mt792x_nan *nan;
	struct sk_buff *skb;
	int ret;

	if (!mdev || !sta)
		return -EINVAL;

	skb = mt76_mcu_msg_alloc(mdev, NULL,
				 sizeof(struct mt7925_nan_common_hdr) +
				 sizeof(struct mt7925_nan_sched_manage_peer_rec_tlv));
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
	struct mt792x_vif *mvif;
	struct mt792x_sta *msta;
	u8 nmi_addr[ETH_ALEN];
	struct sk_buff *skb;
	int ndp_ctx_id = 0;
	int ret = -ENOMEM;
	struct mt792x_dev *dev;
	struct tlv *tlv;

	if (!mdev || !vif || !sta)
		return -EINVAL;

	dev = container_of(mdev, struct mt792x_dev, mt76);
	msta = (struct mt792x_sta *)sta->drv_priv;
	mvif = (struct mt792x_vif *)vif->drv_priv;

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

	if (!nmi_msta->nan_sched.idx_assigned) {
		if (!nmi_sta->nan_sched) {
			rcu_read_unlock();
			dev_err(mdev->dev,
				"NAN: peer schedule missing for NDI sta %pM\n",
				sta->addr);
			return -EAGAIN;
		}

		rcu_read_unlock();
		ret = mt792x_nan_set_peer_schedule(dev, nmi_sta);
		if (ret)
			return ret;

		rcu_read_lock();
		nmi_sta = rcu_dereference(sta->nmi);
		if (!nmi_sta) {
			rcu_read_unlock();
			dev_err(mdev->dev,
				"NAN: NMI sta not found for NDI sta %pM\n",
				sta->addr);
			return -EINVAL;
		}

		nmi_msta = (struct mt792x_sta *)nmi_sta->drv_priv;
	}

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
	map_tlv->role_idx = cpu_to_le32(mvif->bss_conf.mt76.idx);
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
