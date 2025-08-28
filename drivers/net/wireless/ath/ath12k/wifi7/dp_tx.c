// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2019-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "../core.h"
#include "../debug.h"
#include "../dp_tx.h"
#include "../peer.h"
#include "dp_tx.h"

static void
ath12k_dp_tx_htt_tx_complete_buf(struct ath12k_base *ab,
				 struct ath12k_tx_desc_params *desc_params,
				 struct dp_tx_ring *tx_ring,
				 struct ath12k_dp_htt_wbm_tx_status *ts,
				 u16 peer_id)
{
	struct ieee80211_tx_info *info;
	struct ath12k_link_vif *arvif;
	struct ath12k_skb_cb *skb_cb;
	struct ieee80211_vif *vif;
	struct ath12k_vif *ahvif;
	struct ath12k *ar;
	struct sk_buff *msdu = desc_params->skb;
	s32 noise_floor;
	struct ieee80211_tx_status status = {};
	struct ath12k_peer *peer;

	skb_cb = ATH12K_SKB_CB(msdu);
	info = IEEE80211_SKB_CB(msdu);

	ar = skb_cb->ar;
	ab->device_stats.tx_completed[tx_ring->tcl_data_ring_id]++;

	if (atomic_dec_and_test(&ar->dp.num_tx_pending))
		wake_up(&ar->dp.tx_empty_waitq);

	dma_unmap_single(ab->dev, skb_cb->paddr, msdu->len, DMA_TO_DEVICE);
	if (skb_cb->paddr_ext_desc) {
		dma_unmap_single(ab->dev, skb_cb->paddr_ext_desc,
				 desc_params->skb_ext_desc->len, DMA_TO_DEVICE);
		dev_kfree_skb_any(desc_params->skb_ext_desc);
	}

	vif = skb_cb->vif;
	if (vif) {
		ahvif = ath12k_vif_to_ahvif(vif);
		rcu_read_lock();
		arvif = rcu_dereference(ahvif->link[skb_cb->link_id]);
		if (arvif) {
			spin_lock_bh(&arvif->link_stats_lock);
			arvif->link_stats.tx_completed++;
			spin_unlock_bh(&arvif->link_stats_lock);
		}
		rcu_read_unlock();
	}

	memset(&info->status, 0, sizeof(info->status));

	if (ts->acked) {
		if (!(info->flags & IEEE80211_TX_CTL_NO_ACK)) {
			info->flags |= IEEE80211_TX_STAT_ACK;
			info->status.ack_signal = ts->ack_rssi;

			if (!test_bit(WMI_TLV_SERVICE_HW_DB2DBM_CONVERSION_SUPPORT,
				      ab->wmi_ab.svc_map)) {
				spin_lock_bh(&ar->data_lock);
				noise_floor = ath12k_pdev_get_noise_floor(ar);
				spin_unlock_bh(&ar->data_lock);

				info->status.ack_signal += noise_floor;
			}

			info->status.flags = IEEE80211_TX_STATUS_ACK_SIGNAL_VALID;
		} else {
			info->flags |= IEEE80211_TX_STAT_NOACK_TRANSMITTED;
		}
	}
	rcu_read_lock();
	spin_lock_bh(&ab->base_lock);
	peer = ath12k_peer_find_by_id(ab, peer_id);
	if (!peer || !peer->sta) {
		ath12k_dbg(ab, ATH12K_DBG_DATA,
			   "dp_tx: failed to find the peer with peer_id %d\n", peer_id);
		spin_unlock_bh(&ab->base_lock);
		ieee80211_free_txskb(ath12k_ar_to_hw(ar), msdu);
		goto exit;
	} else {
		status.sta = peer->sta;
	}
	spin_unlock_bh(&ab->base_lock);

	status.info = info;
	status.skb = msdu;
	ieee80211_tx_status_ext(ath12k_ar_to_hw(ar), &status);
exit:
	rcu_read_unlock();
}

static void
ath12k_dp_tx_process_htt_tx_complete(struct ath12k_base *ab, void *desc,
				     struct dp_tx_ring *tx_ring,
				     struct ath12k_tx_desc_params *desc_params)
{
	struct htt_tx_wbm_completion *status_desc;
	struct ath12k_dp_htt_wbm_tx_status ts = {};
	enum hal_wbm_htt_tx_comp_status wbm_status;
	u16 peer_id;

	status_desc = desc;

	wbm_status = le32_get_bits(status_desc->info0,
				   HTT_TX_WBM_COMP_INFO0_STATUS);
	ab->device_stats.fw_tx_status[wbm_status]++;

	switch (wbm_status) {
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_OK:
		ts.acked = (wbm_status == HAL_WBM_REL_HTT_TX_COMP_STATUS_OK);
		ts.ack_rssi = le32_get_bits(status_desc->info2,
					    HTT_TX_WBM_COMP_INFO2_ACK_RSSI);

		peer_id = le32_get_bits(((struct hal_wbm_completion_ring_tx *)desc)->
				info3, HAL_WBM_COMPL_TX_INFO3_PEER_ID);

		ath12k_dp_tx_htt_tx_complete_buf(ab, desc_params, tx_ring, &ts, peer_id);
		break;
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_DROP:
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_TTL:
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_REINJ:
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_INSPECT:
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_VDEVID_MISMATCH:
		ath12k_dp_tx_free_txbuf(ab, tx_ring, desc_params);
		break;
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_MEC_NOTIFY:
		/* This event is to be handled only when the driver decides to
		 * use WDS offload functionality.
		 */
		break;
	default:
		ath12k_warn(ab, "Unknown htt wbm tx status %d\n", wbm_status);
		break;
	}
}

static void ath12k_dp_tx_update_txcompl(struct ath12k *ar, struct hal_tx_status *ts)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_peer *peer;
	struct ieee80211_sta *sta;
	struct ath12k_sta *ahsta;
	struct ath12k_link_sta *arsta;
	struct rate_info txrate = {};
	u16 rate, ru_tones;
	u8 rate_idx = 0;
	int ret;

	spin_lock_bh(&ab->base_lock);
	peer = ath12k_peer_find_by_id(ab, ts->peer_id);
	if (!peer || !peer->sta) {
		ath12k_dbg(ab, ATH12K_DBG_DP_TX,
			   "failed to find the peer by id %u\n", ts->peer_id);
		spin_unlock_bh(&ab->base_lock);
		return;
	}
	sta = peer->sta;
	ahsta = ath12k_sta_to_ahsta(sta);
	arsta = &ahsta->deflink;

	/* This is to prefer choose the real NSS value arsta->last_txrate.nss,
	 * if it is invalid, then choose the NSS value while assoc.
	 */
	if (arsta->last_txrate.nss)
		txrate.nss = arsta->last_txrate.nss;
	else
		txrate.nss = arsta->peer_nss;
	spin_unlock_bh(&ab->base_lock);

	switch (ts->pkt_type) {
	case HAL_TX_RATE_STATS_PKT_TYPE_11A:
	case HAL_TX_RATE_STATS_PKT_TYPE_11B:
		ret = ath12k_mac_hw_ratecode_to_legacy_rate(ts->mcs,
							    ts->pkt_type,
							    &rate_idx,
							    &rate);
		if (ret < 0) {
			ath12k_warn(ab, "Invalid tx legacy rate %d\n", ret);
			return;
		}

		txrate.legacy = rate;
		break;
	case HAL_TX_RATE_STATS_PKT_TYPE_11N:
		if (ts->mcs > ATH12K_HT_MCS_MAX) {
			ath12k_warn(ab, "Invalid HT mcs index %d\n", ts->mcs);
			return;
		}

		if (txrate.nss != 0)
			txrate.mcs = ts->mcs + 8 * (txrate.nss - 1);

		txrate.flags = RATE_INFO_FLAGS_MCS;

		if (ts->sgi)
			txrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case HAL_TX_RATE_STATS_PKT_TYPE_11AC:
		if (ts->mcs > ATH12K_VHT_MCS_MAX) {
			ath12k_warn(ab, "Invalid VHT mcs index %d\n", ts->mcs);
			return;
		}

		txrate.mcs = ts->mcs;
		txrate.flags = RATE_INFO_FLAGS_VHT_MCS;

		if (ts->sgi)
			txrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case HAL_TX_RATE_STATS_PKT_TYPE_11AX:
		if (ts->mcs > ATH12K_HE_MCS_MAX) {
			ath12k_warn(ab, "Invalid HE mcs index %d\n", ts->mcs);
			return;
		}

		txrate.mcs = ts->mcs;
		txrate.flags = RATE_INFO_FLAGS_HE_MCS;
		txrate.he_gi = ath12k_he_gi_to_nl80211_he_gi(ts->sgi);
		break;
	case HAL_TX_RATE_STATS_PKT_TYPE_11BE:
		if (ts->mcs > ATH12K_EHT_MCS_MAX) {
			ath12k_warn(ab, "Invalid EHT mcs index %d\n", ts->mcs);
			return;
		}

		txrate.mcs = ts->mcs;
		txrate.flags = RATE_INFO_FLAGS_EHT_MCS;
		txrate.eht_gi = ath12k_mac_eht_gi_to_nl80211_eht_gi(ts->sgi);
		break;
	default:
		ath12k_warn(ab, "Invalid tx pkt type: %d\n", ts->pkt_type);
		return;
	}

	txrate.bw = ath12k_mac_bw_to_mac80211_bw(ts->bw);

	if (ts->ofdma && ts->pkt_type == HAL_TX_RATE_STATS_PKT_TYPE_11AX) {
		txrate.bw = RATE_INFO_BW_HE_RU;
		ru_tones = ath12k_mac_he_convert_tones_to_ru_tones(ts->tones);
		txrate.he_ru_alloc =
			ath12k_he_ru_tones_to_nl80211_he_ru_alloc(ru_tones);
	}

	if (ts->ofdma && ts->pkt_type == HAL_TX_RATE_STATS_PKT_TYPE_11BE) {
		txrate.bw = RATE_INFO_BW_EHT_RU;
		txrate.eht_ru_alloc =
			ath12k_mac_eht_ru_tones_to_nl80211_eht_ru_alloc(ts->tones);
	}

	spin_lock_bh(&ab->base_lock);
	arsta->txrate = txrate;
	spin_unlock_bh(&ab->base_lock);
}

static void ath12k_dp_tx_complete_msdu(struct ath12k *ar,
				       struct ath12k_tx_desc_params *desc_params,
				       struct hal_tx_status *ts,
				       int ring)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_hw *ah = ar->ah;
	struct ieee80211_tx_info *info;
	struct ath12k_link_vif *arvif;
	struct ath12k_skb_cb *skb_cb;
	struct ieee80211_vif *vif;
	struct ath12k_vif *ahvif;
	struct sk_buff *msdu = desc_params->skb;
	s32 noise_floor;
	struct ieee80211_tx_status status = {};
	struct ieee80211_rate_status status_rate = {};
	struct ath12k_peer *peer;
	struct ath12k_link_sta *arsta;
	struct ath12k_sta *ahsta;
	struct rate_info rate;

	if (WARN_ON_ONCE(ts->buf_rel_source != HAL_WBM_REL_SRC_MODULE_TQM)) {
		/* Must not happen */
		return;
	}

	skb_cb = ATH12K_SKB_CB(msdu);
	ab->device_stats.tx_completed[ring]++;

	dma_unmap_single(ab->dev, skb_cb->paddr, msdu->len, DMA_TO_DEVICE);
	if (skb_cb->paddr_ext_desc) {
		dma_unmap_single(ab->dev, skb_cb->paddr_ext_desc,
				 desc_params->skb_ext_desc->len, DMA_TO_DEVICE);
		dev_kfree_skb_any(desc_params->skb_ext_desc);
	}

	rcu_read_lock();

	if (!rcu_dereference(ab->pdevs_active[ar->pdev_idx])) {
		ieee80211_free_txskb(ah->hw, msdu);
		goto exit;
	}

	if (!skb_cb->vif) {
		ieee80211_free_txskb(ah->hw, msdu);
		goto exit;
	}

	vif = skb_cb->vif;
	if (vif) {
		ahvif = ath12k_vif_to_ahvif(vif);
		arvif = rcu_dereference(ahvif->link[skb_cb->link_id]);
		if (arvif) {
			spin_lock_bh(&arvif->link_stats_lock);
			arvif->link_stats.tx_completed++;
			spin_unlock_bh(&arvif->link_stats_lock);
		}
	}

	info = IEEE80211_SKB_CB(msdu);
	memset(&info->status, 0, sizeof(info->status));

	/* skip tx rate update from ieee80211_status*/
	info->status.rates[0].idx = -1;

	switch (ts->status) {
	case HAL_WBM_TQM_REL_REASON_FRAME_ACKED:
		if (!(info->flags & IEEE80211_TX_CTL_NO_ACK)) {
			info->flags |= IEEE80211_TX_STAT_ACK;
			info->status.ack_signal = ts->ack_rssi;

			if (!test_bit(WMI_TLV_SERVICE_HW_DB2DBM_CONVERSION_SUPPORT,
				      ab->wmi_ab.svc_map)) {
				spin_lock_bh(&ar->data_lock);
				noise_floor = ath12k_pdev_get_noise_floor(ar);
				spin_unlock_bh(&ar->data_lock);

				info->status.ack_signal += noise_floor;
			}

			info->status.flags = IEEE80211_TX_STATUS_ACK_SIGNAL_VALID;
		}
		break;
	case HAL_WBM_TQM_REL_REASON_CMD_REMOVE_TX:
		if (info->flags & IEEE80211_TX_CTL_NO_ACK) {
			info->flags |= IEEE80211_TX_STAT_NOACK_TRANSMITTED;
			break;
		}
		fallthrough;
	case HAL_WBM_TQM_REL_REASON_CMD_REMOVE_MPDU:
	case HAL_WBM_TQM_REL_REASON_DROP_THRESHOLD:
	case HAL_WBM_TQM_REL_REASON_CMD_REMOVE_AGED_FRAMES:
		/* The failure status is due to internal firmware tx failure
		 * hence drop the frame; do not update the status of frame to
		 * the upper layer
		 */
		ieee80211_free_txskb(ah->hw, msdu);
		goto exit;
	default:
		ath12k_dbg(ab, ATH12K_DBG_DP_TX, "tx frame is not acked status %d\n",
			   ts->status);
		break;
	}

	/* NOTE: Tx rate status reporting. Tx completion status does not have
	 * necessary information (for example nss) to build the tx rate.
	 * Might end up reporting it out-of-band from HTT stats.
	 */

	ath12k_dp_tx_update_txcompl(ar, ts);

	spin_lock_bh(&ab->base_lock);
	peer = ath12k_peer_find_by_id(ab, ts->peer_id);
	if (!peer || !peer->sta) {
		ath12k_err(ab,
			   "dp_tx: failed to find the peer with peer_id %d\n",
			   ts->peer_id);
		spin_unlock_bh(&ab->base_lock);
		ieee80211_free_txskb(ath12k_ar_to_hw(ar), msdu);
		goto exit;
	}
	ahsta = ath12k_sta_to_ahsta(peer->sta);
	arsta = &ahsta->deflink;

	spin_unlock_bh(&ab->base_lock);

	status.sta = peer->sta;
	status.info = info;
	status.skb = msdu;
	rate = arsta->last_txrate;

	status_rate.rate_idx = rate;
	status_rate.try_count = 1;

	status.rates = &status_rate;
	status.n_rates = 1;
	ieee80211_tx_status_ext(ath12k_ar_to_hw(ar), &status);

exit:
	rcu_read_unlock();
}

static void ath12k_dp_tx_status_parse(struct ath12k_base *ab,
				      struct hal_wbm_completion_ring_tx *desc,
				      struct hal_tx_status *ts)
{
	u32 info0 = le32_to_cpu(desc->rate_stats.info0);

	ts->buf_rel_source =
		le32_get_bits(desc->info0, HAL_WBM_COMPL_TX_INFO0_REL_SRC_MODULE);
	if (ts->buf_rel_source != HAL_WBM_REL_SRC_MODULE_FW &&
	    ts->buf_rel_source != HAL_WBM_REL_SRC_MODULE_TQM)
		return;

	if (ts->buf_rel_source == HAL_WBM_REL_SRC_MODULE_FW)
		return;

	ts->status = le32_get_bits(desc->info0,
				   HAL_WBM_COMPL_TX_INFO0_TQM_RELEASE_REASON);

	ts->ppdu_id = le32_get_bits(desc->info1,
				    HAL_WBM_COMPL_TX_INFO1_TQM_STATUS_NUMBER);

	ts->peer_id = le32_get_bits(desc->info3, HAL_WBM_COMPL_TX_INFO3_PEER_ID);

	ts->ack_rssi = le32_get_bits(desc->info2,
				     HAL_WBM_COMPL_TX_INFO2_ACK_FRAME_RSSI);

	if (info0 & HAL_TX_RATE_STATS_INFO0_VALID) {
		ts->pkt_type = u32_get_bits(info0, HAL_TX_RATE_STATS_INFO0_PKT_TYPE);
		ts->mcs = u32_get_bits(info0, HAL_TX_RATE_STATS_INFO0_MCS);
		ts->sgi = u32_get_bits(info0, HAL_TX_RATE_STATS_INFO0_SGI);
		ts->bw = u32_get_bits(info0, HAL_TX_RATE_STATS_INFO0_BW);
		ts->tones = u32_get_bits(info0, HAL_TX_RATE_STATS_INFO0_TONES_IN_RU);
		ts->ofdma = u32_get_bits(info0, HAL_TX_RATE_STATS_INFO0_OFDMA_TX);
	}
}

void ath12k_dp_tx_completion_handler(struct ath12k_base *ab, int ring_id)
{
	struct ath12k *ar;
	struct ath12k_dp *dp = &ab->dp;
	int hal_ring_id = dp->tx_ring[ring_id].tcl_comp_ring.ring_id;
	struct hal_srng *status_ring = &ab->hal.srng_list[hal_ring_id];
	struct ath12k_tx_desc_info *tx_desc = NULL;
	struct hal_tx_status ts = {};
	struct ath12k_tx_desc_params desc_params;
	struct dp_tx_ring *tx_ring = &dp->tx_ring[ring_id];
	struct hal_wbm_release_ring *desc;
	u8 pdev_id;
	u64 desc_va;
	enum hal_wbm_rel_src_module buf_rel_source;
	enum hal_wbm_tqm_rel_reason rel_status;

	spin_lock_bh(&status_ring->lock);

	ath12k_hal_srng_access_begin(ab, status_ring);

	while (ATH12K_TX_COMPL_NEXT(ab, tx_ring->tx_status_head) !=
	       tx_ring->tx_status_tail) {
		desc = ath12k_hal_srng_dst_get_next_entry(ab, status_ring);
		if (!desc)
			break;

		memcpy(&tx_ring->tx_status[tx_ring->tx_status_head],
		       desc, sizeof(*desc));
		tx_ring->tx_status_head =
			ATH12K_TX_COMPL_NEXT(ab, tx_ring->tx_status_head);
	}

	if (ath12k_hal_srng_dst_peek(ab, status_ring) &&
	    (ATH12K_TX_COMPL_NEXT(ab, tx_ring->tx_status_head) ==
	     tx_ring->tx_status_tail)) {
		/* TODO: Process pending tx_status messages when kfifo_is_full() */
		ath12k_warn(ab, "Unable to process some of the tx_status ring desc because status_fifo is full\n");
	}

	ath12k_hal_srng_access_end(ab, status_ring);

	spin_unlock_bh(&status_ring->lock);

	while (ATH12K_TX_COMPL_NEXT(ab, tx_ring->tx_status_tail) !=
	       tx_ring->tx_status_head) {
		struct hal_wbm_completion_ring_tx *tx_status;
		u32 desc_id;

		tx_ring->tx_status_tail =
			ATH12K_TX_COMPL_NEXT(ab, tx_ring->tx_status_tail);
		tx_status = &tx_ring->tx_status[tx_ring->tx_status_tail];
		ath12k_dp_tx_status_parse(ab, tx_status, &ts);

		if (le32_get_bits(tx_status->info0, HAL_WBM_COMPL_TX_INFO0_CC_DONE)) {
			/* HW done cookie conversion */
			desc_va = ((u64)le32_to_cpu(tx_status->buf_va_hi) << 32 |
				   le32_to_cpu(tx_status->buf_va_lo));
			tx_desc = (struct ath12k_tx_desc_info *)((unsigned long)desc_va);
		} else {
			/* SW does cookie conversion to VA */
			desc_id = le32_get_bits(tx_status->buf_va_hi,
						BUFFER_ADDR_INFO1_SW_COOKIE);

			tx_desc = ath12k_dp_get_tx_desc(ab, desc_id);
		}
		if (!tx_desc) {
			ath12k_warn(ab, "unable to retrieve tx_desc!");
			continue;
		}

		desc_params.mac_id = tx_desc->mac_id;
		desc_params.skb = tx_desc->skb;
		desc_params.skb_ext_desc = tx_desc->skb_ext_desc;

		/* Find the HAL_WBM_RELEASE_INFO0_REL_SRC_MODULE value */
		buf_rel_source = le32_get_bits(tx_status->info0,
					       HAL_WBM_RELEASE_INFO0_REL_SRC_MODULE);
		ab->device_stats.tx_wbm_rel_source[buf_rel_source]++;

		rel_status = le32_get_bits(tx_status->info0,
					   HAL_WBM_COMPL_TX_INFO0_TQM_RELEASE_REASON);
		ab->device_stats.tqm_rel_reason[rel_status]++;

		/* Release descriptor as soon as extracting necessary info
		 * to reduce contention
		 */
		ath12k_dp_tx_release_txbuf(dp, tx_desc, tx_desc->pool_id);
		if (ts.buf_rel_source == HAL_WBM_REL_SRC_MODULE_FW) {
			ath12k_dp_tx_process_htt_tx_complete(ab, (void *)tx_status,
							     tx_ring, &desc_params);
			continue;
		}

		pdev_id = ath12k_hw_mac_id_to_pdev_id(ab->hw_params, desc_params.mac_id);
		ar = ab->pdevs[pdev_id].ar;

		if (atomic_dec_and_test(&ar->dp.num_tx_pending))
			wake_up(&ar->dp.tx_empty_waitq);

		ath12k_dp_tx_complete_msdu(ar, &desc_params, &ts,
					   tx_ring->tcl_data_ring_id);
	}
}
