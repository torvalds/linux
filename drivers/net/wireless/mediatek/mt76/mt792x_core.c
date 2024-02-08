// SPDX-License-Identifier: ISC
/* Copyright (C) 2023 MediaTek Inc. */

#include <linux/module.h>
#include <linux/firmware.h>

#include "mt792x.h"
#include "dma.h"

static const struct ieee80211_iface_limit if_limits[] = {
	{
		.max = MT792x_MAX_INTERFACES,
		.types = BIT(NL80211_IFTYPE_STATION)
	},
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_AP)
	}
};

static const struct ieee80211_iface_combination if_comb[] = {
	{
		.limits = if_limits,
		.n_limits = ARRAY_SIZE(if_limits),
		.max_interfaces = MT792x_MAX_INTERFACES,
		.num_different_channels = 1,
		.beacon_int_infra_match = true,
	},
};

static const struct ieee80211_iface_limit if_limits_chanctx[] = {
	{
		.max = 2,
		.types = BIT(NL80211_IFTYPE_STATION) |
			 BIT(NL80211_IFTYPE_P2P_CLIENT)
	},
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_AP) |
			 BIT(NL80211_IFTYPE_P2P_GO)
	}
};

static const struct ieee80211_iface_combination if_comb_chanctx[] = {
	{
		.limits = if_limits_chanctx,
		.n_limits = ARRAY_SIZE(if_limits_chanctx),
		.max_interfaces = 2,
		.num_different_channels = 2,
		.beacon_int_infra_match = false,
	}
};

void mt792x_tx(struct ieee80211_hw *hw, struct ieee80211_tx_control *control,
	       struct sk_buff *skb)
{
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt76_phy *mphy = hw->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;
	int qid;

	if (control->sta) {
		struct mt792x_sta *sta;

		sta = (struct mt792x_sta *)control->sta->drv_priv;
		wcid = &sta->wcid;
	}

	if (vif && !control->sta) {
		struct mt792x_vif *mvif;

		mvif = (struct mt792x_vif *)vif->drv_priv;
		wcid = &mvif->sta.wcid;
	}

	if (mt76_connac_pm_ref(mphy, &dev->pm)) {
		mt76_tx(mphy, control->sta, wcid, skb);
		mt76_connac_pm_unref(mphy, &dev->pm);
		return;
	}

	qid = skb_get_queue_mapping(skb);
	if (qid >= MT_TXQ_PSD) {
		qid = IEEE80211_AC_BE;
		skb_set_queue_mapping(skb, qid);
	}

	mt76_connac_pm_queue_skb(hw, &dev->pm, wcid, skb);
}
EXPORT_SYMBOL_GPL(mt792x_tx);

void mt792x_stop(struct ieee80211_hw *hw)
{
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt792x_phy *phy = mt792x_hw_phy(hw);

	cancel_delayed_work_sync(&phy->mt76->mac_work);

	cancel_delayed_work_sync(&dev->pm.ps_work);
	cancel_work_sync(&dev->pm.wake_work);
	cancel_work_sync(&dev->reset_work);
	mt76_connac_free_pending_tx_skbs(&dev->pm, NULL);

	if (is_mt7921(&dev->mt76)) {
		mt792x_mutex_acquire(dev);
		mt76_connac_mcu_set_mac_enable(&dev->mt76, 0, false, false);
		mt792x_mutex_release(dev);
	}

	clear_bit(MT76_STATE_RUNNING, &phy->mt76->state);
}
EXPORT_SYMBOL_GPL(mt792x_stop);

void mt792x_remove_interface(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt792x_sta *msta = &mvif->sta;
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt792x_phy *phy = mt792x_hw_phy(hw);
	int idx = msta->wcid.idx;

	mt792x_mutex_acquire(dev);
	mt76_connac_free_pending_tx_skbs(&dev->pm, &msta->wcid);
	mt76_connac_mcu_uni_add_dev(&dev->mphy, vif, &mvif->sta.wcid, false);

	rcu_assign_pointer(dev->mt76.wcid[idx], NULL);

	dev->mt76.vif_mask &= ~BIT_ULL(mvif->mt76.idx);
	phy->omac_mask &= ~BIT_ULL(mvif->mt76.omac_idx);
	mt792x_mutex_release(dev);

	spin_lock_bh(&dev->mt76.sta_poll_lock);
	if (!list_empty(&msta->wcid.poll_list))
		list_del_init(&msta->wcid.poll_list);
	spin_unlock_bh(&dev->mt76.sta_poll_lock);

	mt76_wcid_cleanup(&dev->mt76, &msta->wcid);
}
EXPORT_SYMBOL_GPL(mt792x_remove_interface);

int mt792x_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   unsigned int link_id, u16 queue,
		   const struct ieee80211_tx_queue_params *params)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;

	/* no need to update right away, we'll get BSS_CHANGED_QOS */
	queue = mt76_connac_lmac_mapping(queue);
	mvif->queue_params[queue] = *params;

	return 0;
}
EXPORT_SYMBOL_GPL(mt792x_conf_tx);

int mt792x_get_stats(struct ieee80211_hw *hw,
		     struct ieee80211_low_level_stats *stats)
{
	struct mt792x_phy *phy = mt792x_hw_phy(hw);
	struct mt76_mib_stats *mib = &phy->mib;

	mt792x_mutex_acquire(phy->dev);

	stats->dot11RTSSuccessCount = mib->rts_cnt;
	stats->dot11RTSFailureCount = mib->rts_retries_cnt;
	stats->dot11FCSErrorCount = mib->fcs_err_cnt;
	stats->dot11ACKFailureCount = mib->ack_fail_cnt;

	mt792x_mutex_release(phy->dev);

	return 0;
}
EXPORT_SYMBOL_GPL(mt792x_get_stats);

u64 mt792x_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	u8 omac_idx = mvif->mt76.omac_idx;
	union {
		u64 t64;
		u32 t32[2];
	} tsf;
	u16 n;

	mt792x_mutex_acquire(dev);

	n = omac_idx > HW_BSSID_MAX ? HW_BSSID_0 : omac_idx;
	/* TSF software read */
	mt76_set(dev, MT_LPON_TCR(0, n), MT_LPON_TCR_SW_MODE);
	tsf.t32[0] = mt76_rr(dev, MT_LPON_UTTR0(0));
	tsf.t32[1] = mt76_rr(dev, MT_LPON_UTTR1(0));

	mt792x_mutex_release(dev);

	return tsf.t64;
}
EXPORT_SYMBOL_GPL(mt792x_get_tsf);

void mt792x_set_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    u64 timestamp)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	u8 omac_idx = mvif->mt76.omac_idx;
	union {
		u64 t64;
		u32 t32[2];
	} tsf = { .t64 = timestamp, };
	u16 n;

	mt792x_mutex_acquire(dev);

	n = omac_idx > HW_BSSID_MAX ? HW_BSSID_0 : omac_idx;
	mt76_wr(dev, MT_LPON_UTTR0(0), tsf.t32[0]);
	mt76_wr(dev, MT_LPON_UTTR1(0), tsf.t32[1]);
	/* TSF software overwrite */
	mt76_set(dev, MT_LPON_TCR(0, n), MT_LPON_TCR_SW_WRITE);

	mt792x_mutex_release(dev);
}
EXPORT_SYMBOL_GPL(mt792x_set_tsf);

void mt792x_tx_worker(struct mt76_worker *w)
{
	struct mt792x_dev *dev = container_of(w, struct mt792x_dev,
					      mt76.tx_worker);

	if (!mt76_connac_pm_ref(&dev->mphy, &dev->pm)) {
		queue_work(dev->mt76.wq, &dev->pm.wake_work);
		return;
	}

	mt76_txq_schedule_all(&dev->mphy);
	mt76_connac_pm_unref(&dev->mphy, &dev->pm);
}
EXPORT_SYMBOL_GPL(mt792x_tx_worker);

void mt792x_roc_timer(struct timer_list *timer)
{
	struct mt792x_phy *phy = from_timer(phy, timer, roc_timer);

	ieee80211_queue_work(phy->mt76->hw, &phy->roc_work);
}
EXPORT_SYMBOL_GPL(mt792x_roc_timer);

void mt792x_flush(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  u32 queues, bool drop)
{
	struct mt792x_dev *dev = mt792x_hw_dev(hw);

	wait_event_timeout(dev->mt76.tx_wait,
			   !mt76_has_tx_pending(&dev->mphy), HZ / 2);
}
EXPORT_SYMBOL_GPL(mt792x_flush);

int mt792x_assign_vif_chanctx(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      struct ieee80211_bss_conf *link_conf,
			      struct ieee80211_chanctx_conf *ctx)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt792x_dev *dev = mt792x_hw_dev(hw);

	mutex_lock(&dev->mt76.mutex);
	mvif->mt76.ctx = ctx;
	mutex_unlock(&dev->mt76.mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(mt792x_assign_vif_chanctx);

void mt792x_unassign_vif_chanctx(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *link_conf,
				 struct ieee80211_chanctx_conf *ctx)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	struct mt792x_dev *dev = mt792x_hw_dev(hw);

	mutex_lock(&dev->mt76.mutex);
	mvif->mt76.ctx = NULL;
	mutex_unlock(&dev->mt76.mutex);
}
EXPORT_SYMBOL_GPL(mt792x_unassign_vif_chanctx);

void mt792x_set_wakeup(struct ieee80211_hw *hw, bool enabled)
{
	struct mt792x_dev *dev = mt792x_hw_dev(hw);
	struct mt76_dev *mdev = &dev->mt76;

	device_set_wakeup_enable(mdev->dev, enabled);
}
EXPORT_SYMBOL_GPL(mt792x_set_wakeup);

static const char mt792x_gstrings_stats[][ETH_GSTRING_LEN] = {
	/* tx counters */
	"tx_ampdu_cnt",
	"tx_mpdu_attempts",
	"tx_mpdu_success",
	"tx_pkt_ebf_cnt",
	"tx_pkt_ibf_cnt",
	"tx_ampdu_len:0-1",
	"tx_ampdu_len:2-10",
	"tx_ampdu_len:11-19",
	"tx_ampdu_len:20-28",
	"tx_ampdu_len:29-37",
	"tx_ampdu_len:38-46",
	"tx_ampdu_len:47-55",
	"tx_ampdu_len:56-79",
	"tx_ampdu_len:80-103",
	"tx_ampdu_len:104-127",
	"tx_ampdu_len:128-151",
	"tx_ampdu_len:152-175",
	"tx_ampdu_len:176-199",
	"tx_ampdu_len:200-223",
	"tx_ampdu_len:224-247",
	"ba_miss_count",
	"tx_beamformer_ppdu_iBF",
	"tx_beamformer_ppdu_eBF",
	"tx_beamformer_rx_feedback_all",
	"tx_beamformer_rx_feedback_he",
	"tx_beamformer_rx_feedback_vht",
	"tx_beamformer_rx_feedback_ht",
	"tx_msdu_pack_1",
	"tx_msdu_pack_2",
	"tx_msdu_pack_3",
	"tx_msdu_pack_4",
	"tx_msdu_pack_5",
	"tx_msdu_pack_6",
	"tx_msdu_pack_7",
	"tx_msdu_pack_8",
	/* rx counters */
	"rx_mpdu_cnt",
	"rx_ampdu_cnt",
	"rx_ampdu_bytes_cnt",
	"rx_ba_cnt",
	/* per vif counters */
	"v_tx_mode_cck",
	"v_tx_mode_ofdm",
	"v_tx_mode_ht",
	"v_tx_mode_ht_gf",
	"v_tx_mode_vht",
	"v_tx_mode_he_su",
	"v_tx_mode_he_ext_su",
	"v_tx_mode_he_tb",
	"v_tx_mode_he_mu",
	"v_tx_mode_eht_su",
	"v_tx_mode_eht_trig",
	"v_tx_mode_eht_mu",
	"v_tx_bw_20",
	"v_tx_bw_40",
	"v_tx_bw_80",
	"v_tx_bw_160",
	"v_tx_mcs_0",
	"v_tx_mcs_1",
	"v_tx_mcs_2",
	"v_tx_mcs_3",
	"v_tx_mcs_4",
	"v_tx_mcs_5",
	"v_tx_mcs_6",
	"v_tx_mcs_7",
	"v_tx_mcs_8",
	"v_tx_mcs_9",
	"v_tx_mcs_10",
	"v_tx_mcs_11",
	"v_tx_mcs_12",
	"v_tx_mcs_13",
	"v_tx_nss_1",
	"v_tx_nss_2",
	"v_tx_nss_3",
	"v_tx_nss_4",
};

void mt792x_get_et_strings(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   u32 sset, u8 *data)
{
	if (sset != ETH_SS_STATS)
		return;

	memcpy(data, mt792x_gstrings_stats, sizeof(mt792x_gstrings_stats));

	data += sizeof(mt792x_gstrings_stats);
	page_pool_ethtool_stats_get_strings(data);
}
EXPORT_SYMBOL_GPL(mt792x_get_et_strings);

int mt792x_get_et_sset_count(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			     int sset)
{
	if (sset != ETH_SS_STATS)
		return 0;

	return ARRAY_SIZE(mt792x_gstrings_stats) +
	       page_pool_ethtool_stats_get_count();
}
EXPORT_SYMBOL_GPL(mt792x_get_et_sset_count);

static void
mt792x_ethtool_worker(void *wi_data, struct ieee80211_sta *sta)
{
	struct mt792x_sta *msta = (struct mt792x_sta *)sta->drv_priv;
	struct mt76_ethtool_worker_info *wi = wi_data;

	if (msta->vif->mt76.idx != wi->idx)
		return;

	mt76_ethtool_worker(wi, &msta->wcid.stats, true);
}

void mt792x_get_et_stats(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 struct ethtool_stats *stats, u64 *data)
{
	struct mt792x_vif *mvif = (struct mt792x_vif *)vif->drv_priv;
	int stats_size = ARRAY_SIZE(mt792x_gstrings_stats);
	struct mt792x_phy *phy = mt792x_hw_phy(hw);
	struct mt792x_dev *dev = phy->dev;
	struct mt76_mib_stats *mib = &phy->mib;
	struct mt76_ethtool_worker_info wi = {
		.data = data,
		.idx = mvif->mt76.idx,
	};
	int i, ei = 0;

	mt792x_mutex_acquire(dev);

	mt792x_mac_update_mib_stats(phy);

	data[ei++] = mib->tx_ampdu_cnt;
	data[ei++] = mib->tx_mpdu_attempts_cnt;
	data[ei++] = mib->tx_mpdu_success_cnt;
	data[ei++] = mib->tx_pkt_ebf_cnt;
	data[ei++] = mib->tx_pkt_ibf_cnt;

	/* Tx ampdu stat */
	for (i = 0; i < 15; i++)
		data[ei++] = phy->mt76->aggr_stats[i];

	data[ei++] = phy->mib.ba_miss_cnt;

	/* Tx Beamformer monitor */
	data[ei++] = mib->tx_bf_ibf_ppdu_cnt;
	data[ei++] = mib->tx_bf_ebf_ppdu_cnt;

	/* Tx Beamformer Rx feedback monitor */
	data[ei++] = mib->tx_bf_rx_fb_all_cnt;
	data[ei++] = mib->tx_bf_rx_fb_he_cnt;
	data[ei++] = mib->tx_bf_rx_fb_vht_cnt;
	data[ei++] = mib->tx_bf_rx_fb_ht_cnt;

	/* Tx amsdu info (pack-count histogram) */
	for (i = 0; i < ARRAY_SIZE(mib->tx_amsdu); i++)
		data[ei++] = mib->tx_amsdu[i];

	/* rx counters */
	data[ei++] = mib->rx_mpdu_cnt;
	data[ei++] = mib->rx_ampdu_cnt;
	data[ei++] = mib->rx_ampdu_bytes_cnt;
	data[ei++] = mib->rx_ba_cnt;

	/* Add values for all stations owned by this vif */
	wi.initial_stat_idx = ei;
	ieee80211_iterate_stations_atomic(hw, mt792x_ethtool_worker, &wi);

	mt792x_mutex_release(dev);

	if (!wi.sta_count)
		return;

	ei += wi.worker_stat_count;

	mt76_ethtool_page_pool_stats(&dev->mt76, &data[ei], &ei);
	stats_size += page_pool_ethtool_stats_get_count();

	if (ei != stats_size)
		dev_err(dev->mt76.dev, "ei: %d  SSTATS_LEN: %d", ei,
			stats_size);
}
EXPORT_SYMBOL_GPL(mt792x_get_et_stats);

void mt792x_sta_statistics(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta,
			   struct station_info *sinfo)
{
	struct mt792x_sta *msta = (struct mt792x_sta *)sta->drv_priv;
	struct rate_info *txrate = &msta->wcid.rate;

	if (!txrate->legacy && !txrate->flags)
		return;

	if (txrate->legacy) {
		sinfo->txrate.legacy = txrate->legacy;
	} else {
		sinfo->txrate.mcs = txrate->mcs;
		sinfo->txrate.nss = txrate->nss;
		sinfo->txrate.bw = txrate->bw;
		sinfo->txrate.he_gi = txrate->he_gi;
		sinfo->txrate.he_dcm = txrate->he_dcm;
		sinfo->txrate.he_ru_alloc = txrate->he_ru_alloc;
	}
	sinfo->tx_failed = msta->wcid.stats.tx_failed;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_FAILED);

	sinfo->tx_retries = msta->wcid.stats.tx_retries;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_RETRIES);

	sinfo->txrate.flags = txrate->flags;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);

	sinfo->ack_signal = (s8)msta->ack_signal;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL);

	sinfo->avg_ack_signal = -(s8)ewma_avg_signal_read(&msta->avg_ack_signal);
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL_AVG);
}
EXPORT_SYMBOL_GPL(mt792x_sta_statistics);

void mt792x_set_coverage_class(struct ieee80211_hw *hw, s16 coverage_class)
{
	struct mt792x_phy *phy = mt792x_hw_phy(hw);
	struct mt792x_dev *dev = phy->dev;

	mt792x_mutex_acquire(dev);

	phy->coverage_class = max_t(s16, coverage_class, 0);
	mt792x_mac_set_timeing(phy);

	mt792x_mutex_release(dev);
}
EXPORT_SYMBOL_GPL(mt792x_set_coverage_class);

int mt792x_init_wiphy(struct ieee80211_hw *hw)
{
	struct mt792x_phy *phy = mt792x_hw_phy(hw);
	struct mt792x_dev *dev = phy->dev;
	struct wiphy *wiphy = hw->wiphy;

	hw->queues = 4;
	if (dev->has_eht) {
		hw->max_rx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF_EHT;
		hw->max_tx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF_EHT;
	} else {
		hw->max_rx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF_HE;
		hw->max_tx_aggregation_subframes = IEEE80211_MAX_AMPDU_BUF_HE;
	}
	hw->netdev_features = NETIF_F_RXCSUM;

	hw->radiotap_timestamp.units_pos =
		IEEE80211_RADIOTAP_TIMESTAMP_UNIT_US;

	phy->slottime = 9;

	hw->sta_data_size = sizeof(struct mt792x_sta);
	hw->vif_data_size = sizeof(struct mt792x_vif);

	if (dev->fw_features & MT792x_FW_CAP_CNM) {
		wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
		wiphy->iface_combinations = if_comb_chanctx;
		wiphy->n_iface_combinations = ARRAY_SIZE(if_comb_chanctx);
	} else {
		wiphy->flags &= ~WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;
		wiphy->iface_combinations = if_comb;
		wiphy->n_iface_combinations = ARRAY_SIZE(if_comb);
	}
	wiphy->flags &= ~(WIPHY_FLAG_IBSS_RSN | WIPHY_FLAG_4ADDR_AP |
			  WIPHY_FLAG_4ADDR_STATION);
	wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				 BIT(NL80211_IFTYPE_AP) |
				 BIT(NL80211_IFTYPE_P2P_CLIENT) |
				 BIT(NL80211_IFTYPE_P2P_GO);
	wiphy->max_remain_on_channel_duration = 5000;
	wiphy->max_scan_ie_len = MT76_CONNAC_SCAN_IE_LEN;
	wiphy->max_scan_ssids = 4;
	wiphy->max_sched_scan_plan_interval =
		MT76_CONNAC_MAX_TIME_SCHED_SCAN_INTERVAL;
	wiphy->max_sched_scan_ie_len = IEEE80211_MAX_DATA_LEN;
	wiphy->max_sched_scan_ssids = MT76_CONNAC_MAX_SCHED_SCAN_SSID;
	wiphy->max_match_sets = MT76_CONNAC_MAX_SCAN_MATCH;
	wiphy->max_sched_scan_reqs = 1;
	wiphy->flags |= WIPHY_FLAG_HAS_CHANNEL_SWITCH |
			WIPHY_FLAG_SPLIT_SCAN_6GHZ;

	wiphy->features |= NL80211_FEATURE_SCHED_SCAN_RANDOM_MAC_ADDR |
			   NL80211_FEATURE_SCAN_RANDOM_MAC_ADDR;
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_SET_SCAN_DWELL);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BEACON_RATE_LEGACY);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BEACON_RATE_HT);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BEACON_RATE_VHT);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_BEACON_RATE_HE);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_ACK_SIGNAL_SUPPORT);
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_CAN_REPLACE_PTK0);

	ieee80211_hw_set(hw, SINGLE_SCAN_ON_ALL_BANDS);
	ieee80211_hw_set(hw, HAS_RATE_CONTROL);
	ieee80211_hw_set(hw, SUPPORTS_TX_ENCAP_OFFLOAD);
	ieee80211_hw_set(hw, SUPPORTS_RX_DECAP_OFFLOAD);
	ieee80211_hw_set(hw, WANT_MONITOR_VIF);
	ieee80211_hw_set(hw, SUPPORTS_PS);
	ieee80211_hw_set(hw, SUPPORTS_DYNAMIC_PS);
	ieee80211_hw_set(hw, SUPPORTS_VHT_EXT_NSS_BW);
	ieee80211_hw_set(hw, CONNECTION_MONITOR);

	if (dev->pm.enable)
		ieee80211_hw_set(hw, CONNECTION_MONITOR);

	hw->max_tx_fragments = 4;

	return 0;
}
EXPORT_SYMBOL_GPL(mt792x_init_wiphy);

static u8
mt792x_get_offload_capability(struct device *dev, const char *fw_wm)
{
	const struct mt76_connac2_fw_trailer *hdr;
	struct mt792x_realease_info *rel_info;
	const struct firmware *fw;
	int ret, i, offset = 0;
	const u8 *data, *end;
	u8 offload_caps = 0;

	ret = request_firmware(&fw, fw_wm, dev);
	if (ret)
		return ret;

	if (!fw || !fw->data || fw->size < sizeof(*hdr)) {
		dev_err(dev, "Invalid firmware\n");
		goto out;
	}

	data = fw->data;
	hdr = (const void *)(fw->data + fw->size - sizeof(*hdr));

	for (i = 0; i < hdr->n_region; i++) {
		const struct mt76_connac2_fw_region *region;

		region = (const void *)((const u8 *)hdr -
					(hdr->n_region - i) * sizeof(*region));
		offset += le32_to_cpu(region->len);
	}

	data += offset + 16;
	rel_info = (struct mt792x_realease_info *)data;
	data += sizeof(*rel_info);
	end = data + le16_to_cpu(rel_info->len);

	while (data < end) {
		rel_info = (struct mt792x_realease_info *)data;
		data += sizeof(*rel_info);

		if (rel_info->tag == MT792x_FW_TAG_FEATURE) {
			struct mt792x_fw_features *features;

			features = (struct mt792x_fw_features *)data;
			offload_caps = features->data;
			break;
		}

		data += le16_to_cpu(rel_info->len) + rel_info->pad_len;
	}

out:
	release_firmware(fw);

	return offload_caps;
}

struct ieee80211_ops *
mt792x_get_mac80211_ops(struct device *dev,
			const struct ieee80211_ops *mac80211_ops,
			void *drv_data, u8 *fw_features)
{
	struct ieee80211_ops *ops;

	ops = devm_kmemdup(dev, mac80211_ops, sizeof(struct ieee80211_ops),
			   GFP_KERNEL);
	if (!ops)
		return NULL;

	*fw_features = mt792x_get_offload_capability(dev, drv_data);
	if (!(*fw_features & MT792x_FW_CAP_CNM)) {
		ops->remain_on_channel = NULL;
		ops->cancel_remain_on_channel = NULL;
		ops->add_chanctx = NULL;
		ops->remove_chanctx = NULL;
		ops->change_chanctx = NULL;
		ops->assign_vif_chanctx = NULL;
		ops->unassign_vif_chanctx = NULL;
		ops->mgd_prepare_tx = NULL;
		ops->mgd_complete_tx = NULL;
	}
	return ops;
}
EXPORT_SYMBOL_GPL(mt792x_get_mac80211_ops);

int mt792x_init_wcid(struct mt792x_dev *dev)
{
	int idx;

	/* Beacon and mgmt frames should occupy wcid 0 */
	idx = mt76_wcid_alloc(dev->mt76.wcid_mask, MT792x_WTBL_STA - 1);
	if (idx)
		return -ENOSPC;

	dev->mt76.global_wcid.idx = idx;
	dev->mt76.global_wcid.hw_key_idx = -1;
	dev->mt76.global_wcid.tx_info |= MT_WCID_TX_INFO_SET;
	rcu_assign_pointer(dev->mt76.wcid[idx], &dev->mt76.global_wcid);

	return 0;
}
EXPORT_SYMBOL_GPL(mt792x_init_wcid);

int mt792x_mcu_drv_pmctrl(struct mt792x_dev *dev)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt76_connac_pm *pm = &dev->pm;
	int err = 0;

	mutex_lock(&pm->mutex);

	if (!test_bit(MT76_STATE_PM, &mphy->state))
		goto out;

	err = __mt792x_mcu_drv_pmctrl(dev);
out:
	mutex_unlock(&pm->mutex);

	if (err)
		mt792x_reset(&dev->mt76);

	return err;
}
EXPORT_SYMBOL_GPL(mt792x_mcu_drv_pmctrl);

int mt792x_mcu_fw_pmctrl(struct mt792x_dev *dev)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt76_connac_pm *pm = &dev->pm;
	int err = 0;

	mutex_lock(&pm->mutex);

	if (mt76_connac_skip_fw_pmctrl(mphy, pm))
		goto out;

	err = __mt792x_mcu_fw_pmctrl(dev);
out:
	mutex_unlock(&pm->mutex);

	if (err)
		mt792x_reset(&dev->mt76);

	return err;
}
EXPORT_SYMBOL_GPL(mt792x_mcu_fw_pmctrl);

int __mt792xe_mcu_drv_pmctrl(struct mt792x_dev *dev)
{
	int i, err = 0;

	for (i = 0; i < MT792x_DRV_OWN_RETRY_COUNT; i++) {
		mt76_wr(dev, MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_CLR_OWN);
		if (mt76_poll_msec_tick(dev, MT_CONN_ON_LPCTL,
					PCIE_LPCR_HOST_OWN_SYNC, 0, 50, 1))
			break;
	}

	if (i == MT792x_DRV_OWN_RETRY_COUNT) {
		dev_err(dev->mt76.dev, "driver own failed\n");
		err = -EIO;
	}

	return err;
}
EXPORT_SYMBOL_GPL(__mt792xe_mcu_drv_pmctrl);

int mt792xe_mcu_drv_pmctrl(struct mt792x_dev *dev)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt76_connac_pm *pm = &dev->pm;
	int err;

	err = __mt792xe_mcu_drv_pmctrl(dev);
	if (err < 0)
		goto out;

	mt792x_wpdma_reinit_cond(dev);
	clear_bit(MT76_STATE_PM, &mphy->state);

	pm->stats.last_wake_event = jiffies;
	pm->stats.doze_time += pm->stats.last_wake_event -
			       pm->stats.last_doze_event;
out:
	return err;
}
EXPORT_SYMBOL_GPL(mt792xe_mcu_drv_pmctrl);

int mt792xe_mcu_fw_pmctrl(struct mt792x_dev *dev)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt76_connac_pm *pm = &dev->pm;
	int i;

	for (i = 0; i < MT792x_DRV_OWN_RETRY_COUNT; i++) {
		mt76_wr(dev, MT_CONN_ON_LPCTL, PCIE_LPCR_HOST_SET_OWN);
		if (mt76_poll_msec_tick(dev, MT_CONN_ON_LPCTL,
					PCIE_LPCR_HOST_OWN_SYNC, 4, 50, 1))
			break;
	}

	if (i == MT792x_DRV_OWN_RETRY_COUNT) {
		dev_err(dev->mt76.dev, "firmware own failed\n");
		clear_bit(MT76_STATE_PM, &mphy->state);
		return -EIO;
	}

	pm->stats.last_doze_event = jiffies;
	pm->stats.awake_time += pm->stats.last_doze_event -
				pm->stats.last_wake_event;

	return 0;
}
EXPORT_SYMBOL_GPL(mt792xe_mcu_fw_pmctrl);

int mt792x_load_firmware(struct mt792x_dev *dev)
{
	int ret;

	ret = mt76_connac2_load_patch(&dev->mt76, mt792x_patch_name(dev));
	if (ret)
		return ret;

	if (mt76_is_sdio(&dev->mt76)) {
		/* activate again */
		ret = __mt792x_mcu_fw_pmctrl(dev);
		if (!ret)
			ret = __mt792x_mcu_drv_pmctrl(dev);
	}

	ret = mt76_connac2_load_ram(&dev->mt76, mt792x_ram_name(dev), NULL);
	if (ret)
		return ret;

	if (!mt76_poll_msec(dev, MT_CONN_ON_MISC, MT_TOP_MISC2_FW_N9_RDY,
			    MT_TOP_MISC2_FW_N9_RDY, 1500)) {
		dev_err(dev->mt76.dev, "Timeout for initializing firmware\n");

		return -EIO;
	}

#ifdef CONFIG_PM
	dev->mt76.hw->wiphy->wowlan = &mt76_connac_wowlan_support;
#endif /* CONFIG_PM */

	dev_dbg(dev->mt76.dev, "Firmware init done\n");

	return 0;
}
EXPORT_SYMBOL_GPL(mt792x_load_firmware);

MODULE_DESCRIPTION("MediaTek MT792x core driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
