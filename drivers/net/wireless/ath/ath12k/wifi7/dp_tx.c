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
#include "hal_desc.h"
#include "hal.h"
#include "hal_tx.h"

static void
ath12k_wifi7_hal_tx_cmd_ext_desc_setup(struct ath12k_base *ab,
				       struct hal_tx_msdu_ext_desc *tcl_ext_cmd,
				       struct hal_tx_info *ti)
{
	tcl_ext_cmd->info0 = le32_encode_bits(ti->paddr,
					      HAL_TX_MSDU_EXT_INFO0_BUF_PTR_LO);
	tcl_ext_cmd->info1 = le32_encode_bits(0x0,
					      HAL_TX_MSDU_EXT_INFO1_BUF_PTR_HI) |
			       le32_encode_bits(ti->data_len,
						HAL_TX_MSDU_EXT_INFO1_BUF_LEN);

	tcl_ext_cmd->info1 |= le32_encode_bits(1, HAL_TX_MSDU_EXT_INFO1_EXTN_OVERRIDE) |
				le32_encode_bits(ti->encap_type,
						 HAL_TX_MSDU_EXT_INFO1_ENCAP_TYPE) |
				le32_encode_bits(ti->encrypt_type,
						 HAL_TX_MSDU_EXT_INFO1_ENCRYPT_TYPE);
}

#define HTT_META_DATA_ALIGNMENT 0x8

/* Preparing HTT Metadata when utilized with ext MSDU */
static int ath12k_wifi7_dp_prepare_htt_metadata(struct sk_buff *skb)
{
	struct hal_tx_msdu_metadata *desc_ext;
	u8 htt_desc_size;
	/* Size rounded of multiple of 8 bytes */
	u8 htt_desc_size_aligned;

	htt_desc_size = sizeof(struct hal_tx_msdu_metadata);
	htt_desc_size_aligned = ALIGN(htt_desc_size, HTT_META_DATA_ALIGNMENT);

	desc_ext = ath12k_dp_metadata_align_skb(skb, htt_desc_size_aligned);
	if (!desc_ext)
		return -ENOMEM;

	desc_ext->info0 = le32_encode_bits(1, HAL_TX_MSDU_METADATA_INFO0_ENCRYPT_FLAG) |
			  le32_encode_bits(0, HAL_TX_MSDU_METADATA_INFO0_ENCRYPT_TYPE) |
			  le32_encode_bits(1,
					   HAL_TX_MSDU_METADATA_INFO0_HOST_TX_DESC_POOL);

	return 0;
}

/* TODO: Remove the export once this file is built with wifi7 ko */
int ath12k_wifi7_dp_tx(struct ath12k_pdev_dp *dp_pdev, struct ath12k_link_vif *arvif,
		       struct sk_buff *skb, bool gsn_valid, int mcbc_gsn,
		       bool is_mcast)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ath12k_hal *hal = dp->hal;
	struct ath12k_base *ab = dp->ab;
	struct hal_tx_info ti = {};
	struct ath12k_tx_desc_info *tx_desc;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ath12k_skb_cb *skb_cb = ATH12K_SKB_CB(skb);
	struct hal_tcl_data_cmd *hal_tcl_desc;
	struct hal_tx_msdu_ext_desc *msg;
	struct sk_buff *skb_ext_desc = NULL;
	struct hal_srng *tcl_ring;
	struct ieee80211_hdr *hdr = (void *)skb->data;
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ath12k_dp_vif *dp_vif = &ahvif->dp_vif;
	struct ath12k_dp_link_vif *dp_link_vif;
	struct dp_tx_ring *tx_ring;
	u8 pool_id;
	u8 hal_ring_id;
	int ret;
	u8 ring_selector, ring_map = 0;
	bool tcl_ring_retry;
	bool msdu_ext_desc = false;
	bool add_htt_metadata = false;
	u32 iova_mask = dp->hw_params->iova_mask;
	bool is_diff_encap = false;
	bool is_null_frame = false;

	if (test_bit(ATH12K_FLAG_CRASH_FLUSH, &ab->dev_flags))
		return -ESHUTDOWN;

	if (!(info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP) &&
	    !ieee80211_is_data(hdr->frame_control))
		return -EOPNOTSUPP;

	pool_id = skb_get_queue_mapping(skb) & (ATH12K_HW_MAX_QUEUES - 1);

	/* Let the default ring selection be based on current processor
	 * number, where one of the 3 tcl rings are selected based on
	 * the smp_processor_id(). In case that ring
	 * is full/busy, we resort to other available rings.
	 * If all rings are full, we drop the packet.
	 * TODO: Add throttling logic when all rings are full
	 */
	ring_selector = dp->hw_params->hw_ops->get_ring_selector(skb);

tcl_ring_sel:
	tcl_ring_retry = false;
	ti.ring_id = ring_selector % dp->hw_params->max_tx_ring;

	ring_map |= BIT(ti.ring_id);
	ti.rbm_id = hal->tcl_to_wbm_rbm_map[ti.ring_id].rbm_id;

	tx_ring = &dp->tx_ring[ti.ring_id];

	tx_desc = ath12k_dp_tx_assign_buffer(dp, pool_id);
	if (!tx_desc)
		return -ENOMEM;

	dp_link_vif = ath12k_dp_vif_to_dp_link_vif(&ahvif->dp_vif, arvif->link_id);

	ti.bank_id = dp_link_vif->bank_id;
	ti.meta_data_flags = dp_link_vif->tcl_metadata;

	if (dp_vif->tx_encap_type == HAL_TCL_ENCAP_TYPE_RAW &&
	    test_bit(ATH12K_FLAG_HW_CRYPTO_DISABLED, &ab->dev_flags)) {
		if (skb_cb->flags & ATH12K_SKB_CIPHER_SET) {
			ti.encrypt_type =
				ath12k_dp_tx_get_encrypt_type(skb_cb->cipher);

			if (ieee80211_has_protected(hdr->frame_control))
				skb_put(skb, IEEE80211_CCMP_MIC_LEN);
		} else {
			ti.encrypt_type = HAL_ENCRYPT_TYPE_OPEN;
		}

		msdu_ext_desc = true;
	}

	if (gsn_valid) {
		/* Reset and Initialize meta_data_flags with Global Sequence
		 * Number (GSN) info.
		 */
		ti.meta_data_flags =
			u32_encode_bits(HTT_TCL_META_DATA_TYPE_GLOBAL_SEQ_NUM,
					HTT_TCL_META_DATA_TYPE) |
			u32_encode_bits(mcbc_gsn, HTT_TCL_META_DATA_GLOBAL_SEQ_NUM);
	}

	ti.encap_type = ath12k_dp_tx_get_encap_type(ab, skb);
	ti.addr_search_flags = dp_link_vif->hal_addr_search_flags;
	ti.search_type = dp_link_vif->search_type;
	ti.type = HAL_TCL_DESC_TYPE_BUFFER;
	ti.pkt_offset = 0;
	ti.lmac_id = dp_link_vif->lmac_id;

	ti.vdev_id = dp_link_vif->vdev_id;
	if (gsn_valid)
		ti.vdev_id += HTT_TX_MLO_MCAST_HOST_REINJECT_BASE_VDEV_ID;

	ti.bss_ast_hash = dp_link_vif->ast_hash;
	ti.bss_ast_idx = dp_link_vif->ast_idx;
	ti.dscp_tid_tbl_idx = 0;

	if (skb->ip_summed == CHECKSUM_PARTIAL &&
	    ti.encap_type != HAL_TCL_ENCAP_TYPE_RAW) {
		ti.flags0 |= u32_encode_bits(1, HAL_TCL_DATA_CMD_INFO2_IP4_CKSUM_EN) |
			     u32_encode_bits(1, HAL_TCL_DATA_CMD_INFO2_UDP4_CKSUM_EN) |
			     u32_encode_bits(1, HAL_TCL_DATA_CMD_INFO2_UDP6_CKSUM_EN) |
			     u32_encode_bits(1, HAL_TCL_DATA_CMD_INFO2_TCP4_CKSUM_EN) |
			     u32_encode_bits(1, HAL_TCL_DATA_CMD_INFO2_TCP6_CKSUM_EN);
	}

	ti.flags1 |= u32_encode_bits(1, HAL_TCL_DATA_CMD_INFO3_TID_OVERWRITE);

	ti.tid = ath12k_dp_tx_get_tid(skb);

	switch (ti.encap_type) {
	case HAL_TCL_ENCAP_TYPE_NATIVE_WIFI:
		is_null_frame = ieee80211_is_nullfunc(hdr->frame_control);
		if (ahvif->vif->offload_flags & IEEE80211_OFFLOAD_ENCAP_ENABLED) {
			if (skb->protocol == cpu_to_be16(ETH_P_PAE) || is_null_frame)
				is_diff_encap = true;

			/* Firmware expects msdu ext descriptor for nwifi/raw packets
			 * received in ETH mode. Without this, observed tx fail for
			 * Multicast packets in ETH mode.
			 */
			msdu_ext_desc = true;
		} else {
			ath12k_dp_tx_encap_nwifi(skb);
		}
		break;
	case HAL_TCL_ENCAP_TYPE_RAW:
		if (!test_bit(ATH12K_FLAG_RAW_MODE, &ab->dev_flags)) {
			ret = -EINVAL;
			goto fail_remove_tx_buf;
		}
		break;
	case HAL_TCL_ENCAP_TYPE_ETHERNET:
		/* no need to encap */
		break;
	case HAL_TCL_ENCAP_TYPE_802_3:
	default:
		/* TODO: Take care of other encap modes as well */
		ret = -EINVAL;
		atomic_inc(&dp->device_stats.tx_err.misc_fail);
		goto fail_remove_tx_buf;
	}

	if (iova_mask &&
	    (unsigned long)skb->data & iova_mask) {
		ret = ath12k_dp_tx_align_payload(dp, &skb);
		if (ret) {
			ath12k_warn(ab, "failed to align TX buffer %d\n", ret);
			/* don't bail out, give original buffer
			 * a chance even unaligned.
			 */
			goto map;
		}

		/* hdr is pointing to a wrong place after alignment,
		 * so refresh it for later use.
		 */
		hdr = (void *)skb->data;
	}
map:
	ti.paddr = dma_map_single(dp->dev, skb->data, skb->len, DMA_TO_DEVICE);
	if (dma_mapping_error(dp->dev, ti.paddr)) {
		atomic_inc(&dp->device_stats.tx_err.misc_fail);
		ath12k_warn(ab, "failed to DMA map data Tx buffer\n");
		ret = -ENOMEM;
		goto fail_remove_tx_buf;
	}

	if ((!test_bit(ATH12K_FLAG_HW_CRYPTO_DISABLED, &ab->dev_flags) &&
	     !(skb_cb->flags & ATH12K_SKB_HW_80211_ENCAP) &&
	     !(skb_cb->flags & ATH12K_SKB_CIPHER_SET) &&
	     ieee80211_has_protected(hdr->frame_control)) ||
	    is_diff_encap) {
		/* Firmware is not expecting meta data for qos null
		 * nwifi packet received in ETH encap mode.
		 */
		if (is_null_frame && msdu_ext_desc)
			goto skip_htt_meta;

		/* Add metadata for sw encrypted vlan group traffic
		 * and EAPOL nwifi packet received in ETH encap mode.
		 */
		add_htt_metadata = true;
		msdu_ext_desc = true;
		ti.meta_data_flags |= HTT_TCL_META_DATA_VALID_HTT;
skip_htt_meta:
		ti.flags0 |= u32_encode_bits(1, HAL_TCL_DATA_CMD_INFO2_TO_FW);
		ti.encap_type = HAL_TCL_ENCAP_TYPE_RAW;
		ti.encrypt_type = HAL_ENCRYPT_TYPE_OPEN;
	}

	tx_desc->skb = skb;
	tx_desc->mac_id = dp_link_vif->pdev_idx;
	ti.desc_id = tx_desc->desc_id;
	ti.data_len = skb->len;
	skb_cb->paddr = ti.paddr;

	if (msdu_ext_desc) {
		skb_ext_desc = dev_alloc_skb(sizeof(struct hal_tx_msdu_ext_desc));
		if (!skb_ext_desc) {
			ret = -ENOMEM;
			goto fail_unmap_dma;
		}

		skb_put(skb_ext_desc, sizeof(struct hal_tx_msdu_ext_desc));
		memset(skb_ext_desc->data, 0, skb_ext_desc->len);

		msg = (struct hal_tx_msdu_ext_desc *)skb_ext_desc->data;
		ath12k_wifi7_hal_tx_cmd_ext_desc_setup(ab, msg, &ti);

		if (add_htt_metadata) {
			ret = ath12k_wifi7_dp_prepare_htt_metadata(skb_ext_desc);
			if (ret < 0) {
				ath12k_dbg(ab, ATH12K_DBG_DP_TX,
					   "Failed to add HTT meta data, dropping packet\n");
				goto fail_free_ext_skb;
			}
		}

		ti.paddr = dma_map_single(dp->dev, skb_ext_desc->data,
					  skb_ext_desc->len, DMA_TO_DEVICE);
		ret = dma_mapping_error(dp->dev, ti.paddr);
		if (ret)
			goto fail_free_ext_skb;

		ti.data_len = skb_ext_desc->len;
		ti.type = HAL_TCL_DESC_TYPE_EXT_DESC;

		skb_cb->paddr_ext_desc = ti.paddr;
		tx_desc->skb_ext_desc = skb_ext_desc;
	}

	hal_ring_id = tx_ring->tcl_data_ring.ring_id;
	tcl_ring = &hal->srng_list[hal_ring_id];

	spin_lock_bh(&tcl_ring->lock);

	ath12k_hal_srng_access_begin(ab, tcl_ring);

	hal_tcl_desc = ath12k_hal_srng_src_get_next_entry(ab, tcl_ring);
	if (!hal_tcl_desc) {
		/* NOTE: It is highly unlikely we'll be running out of tcl_ring
		 * desc because the desc is directly enqueued onto hw queue.
		 */
		ath12k_hal_srng_access_end(ab, tcl_ring);
		dp->device_stats.tx_err.desc_na[ti.ring_id]++;
		spin_unlock_bh(&tcl_ring->lock);
		ret = -ENOMEM;

		/* Checking for available tcl descriptors in another ring in
		 * case of failure due to full tcl ring now, is better than
		 * checking this ring earlier for each pkt tx.
		 * Restart ring selection if some rings are not checked yet.
		 */
		if (ring_map != (BIT(dp->hw_params->max_tx_ring) - 1) &&
		    dp->hw_params->tcl_ring_retry) {
			tcl_ring_retry = true;
			ring_selector++;
		}

		goto fail_unmap_dma_ext;
	}

	spin_lock_bh(&arvif->link_stats_lock);
	arvif->link_stats.tx_encap_type[ti.encap_type]++;
	arvif->link_stats.tx_encrypt_type[ti.encrypt_type]++;
	arvif->link_stats.tx_desc_type[ti.type]++;

	if (is_mcast)
		arvif->link_stats.tx_bcast_mcast++;
	else
		arvif->link_stats.tx_enqueued++;
	spin_unlock_bh(&arvif->link_stats_lock);

	dp->device_stats.tx_enqueued[ti.ring_id]++;

	ath12k_wifi7_hal_tx_cmd_desc_setup(ab, hal_tcl_desc, &ti);

	ath12k_hal_srng_access_end(ab, tcl_ring);

	spin_unlock_bh(&tcl_ring->lock);

	ath12k_dbg_dump(ab, ATH12K_DBG_DP_TX, NULL, "dp tx msdu: ",
			skb->data, skb->len);

	atomic_inc(&dp_pdev->num_tx_pending);

	return 0;

fail_unmap_dma_ext:
	if (skb_cb->paddr_ext_desc)
		dma_unmap_single(dp->dev, skb_cb->paddr_ext_desc,
				 skb_ext_desc->len,
				 DMA_TO_DEVICE);
fail_free_ext_skb:
	kfree_skb(skb_ext_desc);

fail_unmap_dma:
	dma_unmap_single(dp->dev, ti.paddr, ti.data_len, DMA_TO_DEVICE);

fail_remove_tx_buf:
	ath12k_dp_tx_release_txbuf(dp, tx_desc, pool_id);

	spin_lock_bh(&arvif->link_stats_lock);
	arvif->link_stats.tx_dropped++;
	spin_unlock_bh(&arvif->link_stats_lock);

	if (tcl_ring_retry)
		goto tcl_ring_sel;

	return ret;
}

static void
ath12k_dp_tx_htt_tx_complete_buf(struct ath12k_dp *dp,
				 struct ath12k_tx_desc_params *desc_params,
				 struct dp_tx_ring *tx_ring,
				 struct ath12k_dp_htt_wbm_tx_status *ts,
				 u16 peer_id)
{
	struct ath12k_base *ab = dp->ab;
	struct ieee80211_tx_info *info;
	struct ath12k_link_vif *arvif;
	struct ath12k_skb_cb *skb_cb;
	struct ieee80211_vif *vif;
	struct ath12k_vif *ahvif;
	struct sk_buff *msdu = desc_params->skb;
	s32 noise_floor;
	struct ieee80211_tx_status status = {};
	struct ath12k_dp_link_peer *peer;
	struct ath12k_pdev_dp *dp_pdev;
	u8 pdev_id;

	skb_cb = ATH12K_SKB_CB(msdu);
	info = IEEE80211_SKB_CB(msdu);

	pdev_id = ath12k_hw_mac_id_to_pdev_id(dp->hw_params, desc_params->mac_id);

	rcu_read_lock();
	dp_pdev = ath12k_dp_to_pdev_dp(dp, pdev_id);
	if (!dp_pdev) {
		rcu_read_unlock();
		return;
	}

	dp->device_stats.tx_completed[tx_ring->tcl_data_ring_id]++;

	if (atomic_dec_and_test(&dp_pdev->num_tx_pending))
		wake_up(&dp_pdev->tx_empty_waitq);

	dma_unmap_single(dp->dev, skb_cb->paddr, msdu->len, DMA_TO_DEVICE);
	if (skb_cb->paddr_ext_desc) {
		dma_unmap_single(dp->dev, skb_cb->paddr_ext_desc,
				 desc_params->skb_ext_desc->len, DMA_TO_DEVICE);
		dev_kfree_skb_any(desc_params->skb_ext_desc);
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

	memset(&info->status, 0, sizeof(info->status));

	if (ts->acked) {
		if (!(info->flags & IEEE80211_TX_CTL_NO_ACK)) {
			info->flags |= IEEE80211_TX_STAT_ACK;
			info->status.ack_signal = ts->ack_rssi;

			if (!test_bit(WMI_TLV_SERVICE_HW_DB2DBM_CONVERSION_SUPPORT,
				      ab->wmi_ab.svc_map)) {
				struct ath12k *ar = ath12k_pdev_dp_to_ar(dp_pdev);

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

	peer = ath12k_dp_link_peer_find_by_peerid(dp_pdev, peer_id);
	if (!peer || !peer->sta) {
		ath12k_dbg(ab, ATH12K_DBG_DATA,
			   "dp_tx: failed to find the peer with peer_id %d\n", peer_id);
		ieee80211_free_txskb(ath12k_pdev_dp_to_hw(dp_pdev), msdu);
		goto exit;
	} else {
		status.sta = peer->sta;
	}

	status.info = info;
	status.skb = msdu;
	ieee80211_tx_status_ext(ath12k_pdev_dp_to_hw(dp_pdev), &status);
exit:
	rcu_read_unlock();
}

static void
ath12k_dp_tx_process_htt_tx_complete(struct ath12k_dp *dp, void *desc,
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
	dp->device_stats.fw_tx_status[wbm_status]++;

	switch (wbm_status) {
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_OK:
		ts.acked = (wbm_status == HAL_WBM_REL_HTT_TX_COMP_STATUS_OK);
		ts.ack_rssi = le32_get_bits(status_desc->info2,
					    HTT_TX_WBM_COMP_INFO2_ACK_RSSI);

		peer_id = le32_get_bits(((struct hal_wbm_completion_ring_tx *)desc)->
				info3, HAL_WBM_COMPL_TX_INFO3_PEER_ID);

		ath12k_dp_tx_htt_tx_complete_buf(dp, desc_params, tx_ring, &ts, peer_id);
		break;
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_DROP:
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_TTL:
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_REINJ:
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_INSPECT:
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_VDEVID_MISMATCH:
		ath12k_dp_tx_free_txbuf(dp, tx_ring, desc_params);
		break;
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_MEC_NOTIFY:
		/* This event is to be handled only when the driver decides to
		 * use WDS offload functionality.
		 */
		break;
	default:
		ath12k_warn(dp->ab, "Unknown htt wbm tx status %d\n", wbm_status);
		break;
	}
}

static void ath12k_wifi7_dp_tx_update_txcompl(struct ath12k_pdev_dp *dp_pdev,
					      struct hal_tx_status *ts)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ath12k_dp_link_peer *peer;
	struct ath12k_link_sta *arsta;
	struct rate_info txrate = {};
	struct ieee80211_sta *sta;
	struct ath12k_sta *ahsta;
	u16 rate, ru_tones;
	u8 rate_idx = 0;
	int ret;

	peer = ath12k_dp_link_peer_find_by_peerid(dp_pdev, ts->peer_id);
	if (!peer || !peer->sta) {
		ath12k_dbg(dp->ab, ATH12K_DBG_DP_TX,
			   "failed to find the peer by id %u\n", ts->peer_id);
		return;
	}

	spin_lock_bh(&dp->dp_lock);

	sta = peer->sta;
	ahsta = ath12k_sta_to_ahsta(sta);
	arsta = &ahsta->deflink;

	spin_unlock_bh(&dp->dp_lock);

	/* This is to prefer choose the real NSS value arsta->last_txrate.nss,
	 * if it is invalid, then choose the NSS value while assoc.
	 */
	if (peer->last_txrate.nss)
		txrate.nss = peer->last_txrate.nss;
	else
		txrate.nss = arsta->peer_nss;

	switch (ts->pkt_type) {
	case HAL_TX_RATE_STATS_PKT_TYPE_11A:
	case HAL_TX_RATE_STATS_PKT_TYPE_11B:
		ret = ath12k_mac_hw_ratecode_to_legacy_rate(ts->mcs,
							    ts->pkt_type,
							    &rate_idx,
							    &rate);
		if (ret < 0) {
			ath12k_warn(dp->ab, "Invalid tx legacy rate %d\n", ret);
			return;
		}

		txrate.legacy = rate;
		break;
	case HAL_TX_RATE_STATS_PKT_TYPE_11N:
		if (ts->mcs > ATH12K_HT_MCS_MAX) {
			ath12k_warn(dp->ab, "Invalid HT mcs index %d\n", ts->mcs);
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
			ath12k_warn(dp->ab, "Invalid VHT mcs index %d\n", ts->mcs);
			return;
		}

		txrate.mcs = ts->mcs;
		txrate.flags = RATE_INFO_FLAGS_VHT_MCS;

		if (ts->sgi)
			txrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case HAL_TX_RATE_STATS_PKT_TYPE_11AX:
		if (ts->mcs > ATH12K_HE_MCS_MAX) {
			ath12k_warn(dp->ab, "Invalid HE mcs index %d\n", ts->mcs);
			return;
		}

		txrate.mcs = ts->mcs;
		txrate.flags = RATE_INFO_FLAGS_HE_MCS;
		txrate.he_gi = ath12k_he_gi_to_nl80211_he_gi(ts->sgi);
		break;
	case HAL_TX_RATE_STATS_PKT_TYPE_11BE:
		if (ts->mcs > ATH12K_EHT_MCS_MAX) {
			ath12k_warn(dp->ab, "Invalid EHT mcs index %d\n", ts->mcs);
			return;
		}

		txrate.mcs = ts->mcs;
		txrate.flags = RATE_INFO_FLAGS_EHT_MCS;
		txrate.eht_gi = ath12k_mac_eht_gi_to_nl80211_eht_gi(ts->sgi);
		break;
	default:
		ath12k_warn(dp->ab, "Invalid tx pkt type: %d\n", ts->pkt_type);
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

	spin_lock_bh(&dp->dp_lock);
	peer->txrate = txrate;
	spin_unlock_bh(&dp->dp_lock);
}

static void ath12k_wifi7_dp_tx_complete_msdu(struct ath12k_pdev_dp *dp_pdev,
					     struct ath12k_tx_desc_params *desc_params,
					     struct hal_tx_status *ts,
					     int ring)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ath12k_base *ab = dp->ab;
	struct ieee80211_tx_info *info;
	struct ath12k_link_vif *arvif;
	struct ath12k_skb_cb *skb_cb;
	struct ieee80211_vif *vif;
	struct ath12k_vif *ahvif;
	struct sk_buff *msdu = desc_params->skb;
	s32 noise_floor;
	struct ieee80211_tx_status status = {};
	struct ieee80211_rate_status status_rate = {};
	struct ath12k_dp_link_peer *peer;
	struct rate_info rate;

	if (WARN_ON_ONCE(ts->buf_rel_source != HAL_WBM_REL_SRC_MODULE_TQM)) {
		/* Must not happen */
		return;
	}

	skb_cb = ATH12K_SKB_CB(msdu);
	dp->device_stats.tx_completed[ring]++;

	dma_unmap_single(dp->dev, skb_cb->paddr, msdu->len, DMA_TO_DEVICE);
	if (skb_cb->paddr_ext_desc) {
		dma_unmap_single(dp->dev, skb_cb->paddr_ext_desc,
				 desc_params->skb_ext_desc->len, DMA_TO_DEVICE);
		dev_kfree_skb_any(desc_params->skb_ext_desc);
	}

	rcu_read_lock();

	if (!rcu_dereference(ab->pdevs_active[dp_pdev->mac_id])) {
		ieee80211_free_txskb(ath12k_pdev_dp_to_hw(dp_pdev), msdu);
		goto exit;
	}

	if (!skb_cb->vif) {
		ieee80211_free_txskb(ath12k_pdev_dp_to_hw(dp_pdev), msdu);
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
				struct ath12k *ar = ath12k_pdev_dp_to_ar(dp_pdev);

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
		ieee80211_free_txskb(ath12k_pdev_dp_to_hw(dp_pdev), msdu);
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

	ath12k_wifi7_dp_tx_update_txcompl(dp_pdev, ts);

	peer = ath12k_dp_link_peer_find_by_peerid(dp_pdev, ts->peer_id);
	if (!peer || !peer->sta) {
		ath12k_err(ab,
			   "dp_tx: failed to find the peer with peer_id %d\n",
			   ts->peer_id);
		ieee80211_free_txskb(ath12k_pdev_dp_to_hw(dp_pdev), msdu);
		goto exit;
	}

	status.sta = peer->sta;
	status.info = info;
	status.skb = msdu;
	rate = peer->last_txrate;

	status_rate.rate_idx = rate;
	status_rate.try_count = 1;

	status.rates = &status_rate;
	status.n_rates = 1;
	ieee80211_tx_status_ext(ath12k_pdev_dp_to_hw(dp_pdev), &status);

exit:
	rcu_read_unlock();
}

static void
ath12k_wifi7_dp_tx_status_parse(struct ath12k_dp *dp,
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

void ath12k_wifi7_dp_tx_completion_handler(struct ath12k_dp *dp, int ring_id)
{
	struct ath12k_base *ab = dp->ab;
	struct ath12k_pdev_dp *dp_pdev;
	int hal_ring_id = dp->tx_ring[ring_id].tcl_comp_ring.ring_id;
	struct hal_srng *status_ring = &dp->hal->srng_list[hal_ring_id];
	struct ath12k_tx_desc_info *tx_desc = NULL;
	struct hal_tx_status ts = {};
	struct ath12k_tx_desc_params desc_params;
	struct dp_tx_ring *tx_ring = &dp->tx_ring[ring_id];
	struct hal_wbm_release_ring *desc;
	u8 pdev_idx;
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
		ath12k_wifi7_dp_tx_status_parse(dp, tx_status, &ts);

		if (le32_get_bits(tx_status->info0, HAL_WBM_COMPL_TX_INFO0_CC_DONE)) {
			/* HW done cookie conversion */
			desc_va = ((u64)le32_to_cpu(tx_status->buf_va_hi) << 32 |
				   le32_to_cpu(tx_status->buf_va_lo));
			tx_desc = (struct ath12k_tx_desc_info *)((unsigned long)desc_va);
		} else {
			/* SW does cookie conversion to VA */
			desc_id = le32_get_bits(tx_status->buf_va_hi,
						BUFFER_ADDR_INFO1_SW_COOKIE);

			tx_desc = ath12k_dp_get_tx_desc(dp, desc_id);
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
		dp->device_stats.tx_wbm_rel_source[buf_rel_source]++;

		rel_status = le32_get_bits(tx_status->info0,
					   HAL_WBM_COMPL_TX_INFO0_TQM_RELEASE_REASON);
		dp->device_stats.tqm_rel_reason[rel_status]++;

		/* Release descriptor as soon as extracting necessary info
		 * to reduce contention
		 */
		ath12k_dp_tx_release_txbuf(dp, tx_desc, tx_desc->pool_id);
		if (ts.buf_rel_source == HAL_WBM_REL_SRC_MODULE_FW) {
			ath12k_dp_tx_process_htt_tx_complete(dp, (void *)tx_status,
							     tx_ring, &desc_params);
			continue;
		}

		pdev_idx = ath12k_hw_mac_id_to_pdev_id(dp->hw_params, desc_params.mac_id);

		rcu_read_lock();

		dp_pdev = ath12k_dp_to_pdev_dp(dp, pdev_idx);
		if (!dp_pdev) {
			rcu_read_unlock();
			continue;
		}

		if (atomic_dec_and_test(&dp_pdev->num_tx_pending))
			wake_up(&dp_pdev->tx_empty_waitq);

		ath12k_wifi7_dp_tx_complete_msdu(dp_pdev, &desc_params, &ts,
						 tx_ring->tcl_data_ring_id);
		rcu_read_unlock();
	}
}

u32 ath12k_wifi7_dp_tx_get_vdev_bank_config(struct ath12k_base *ab,
					    struct ath12k_link_vif *arvif)
{
	u32 bank_config = 0;
	u8 link_id = arvif->link_id;
	struct ath12k_vif *ahvif = arvif->ahvif;
	struct ath12k_dp_vif *dp_vif = &ahvif->dp_vif;
	struct ath12k_dp_link_vif *dp_link_vif;

	dp_link_vif = ath12k_dp_vif_to_dp_link_vif(dp_vif, link_id);

	/* Only valid for raw frames with HW crypto enabled.
	 * With SW crypto, mac80211 sets key per packet
	 */
	if (dp_vif->tx_encap_type == HAL_TCL_ENCAP_TYPE_RAW &&
	    test_bit(ATH12K_FLAG_HW_CRYPTO_DISABLED, &ab->dev_flags))
		bank_config |=
			u32_encode_bits(ath12k_dp_tx_get_encrypt_type(dp_vif->key_cipher),
					HAL_TX_BANK_CONFIG_ENCRYPT_TYPE);

	bank_config |= u32_encode_bits(dp_vif->tx_encap_type,
					HAL_TX_BANK_CONFIG_ENCAP_TYPE);
	bank_config |= u32_encode_bits(0, HAL_TX_BANK_CONFIG_SRC_BUFFER_SWAP) |
			u32_encode_bits(0, HAL_TX_BANK_CONFIG_LINK_META_SWAP) |
			u32_encode_bits(0, HAL_TX_BANK_CONFIG_EPD);

	/* only valid if idx_lookup_override is not set in tcl_data_cmd */
	if (ahvif->vdev_type == WMI_VDEV_TYPE_STA)
		bank_config |= u32_encode_bits(1, HAL_TX_BANK_CONFIG_INDEX_LOOKUP_EN);
	else
		bank_config |= u32_encode_bits(0, HAL_TX_BANK_CONFIG_INDEX_LOOKUP_EN);

	bank_config |= u32_encode_bits(dp_link_vif->hal_addr_search_flags &
				       HAL_TX_ADDRX_EN,
				       HAL_TX_BANK_CONFIG_ADDRX_EN) |
			u32_encode_bits(!!(dp_link_vif->hal_addr_search_flags &
					HAL_TX_ADDRY_EN),
					HAL_TX_BANK_CONFIG_ADDRY_EN);

	bank_config |= u32_encode_bits(ieee80211_vif_is_mesh(ahvif->vif) ? 3 : 0,
					HAL_TX_BANK_CONFIG_MESH_EN) |
			u32_encode_bits(dp_link_vif->vdev_id_check_en,
					HAL_TX_BANK_CONFIG_VDEV_ID_CHECK_EN);

	bank_config |= u32_encode_bits(0, HAL_TX_BANK_CONFIG_DSCP_TIP_MAP_ID);

	return bank_config;
}
