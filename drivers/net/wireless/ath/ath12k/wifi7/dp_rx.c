// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "dp_rx.h"
#include "../dp_tx.h"

static bool ath12k_dp_rx_h_tkip_mic_err(struct ath12k *ar, struct sk_buff *msdu,
					struct ath12k_dp_rx_info *rx_info)
{
	struct ath12k_base *ab = ar->ab;
	u16 msdu_len;
	struct hal_rx_desc *desc = (struct hal_rx_desc *)msdu->data;
	u8 l3pad_bytes;
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	u32 hal_rx_desc_sz = ar->ab->hal.hal_desc_sz;

	rxcb->is_first_msdu = ath12k_dp_rx_h_first_msdu(ab, desc);
	rxcb->is_last_msdu = ath12k_dp_rx_h_last_msdu(ab, desc);

	l3pad_bytes = ath12k_dp_rx_h_l3pad(ab, desc);
	msdu_len = ath12k_dp_rx_h_msdu_len(ab, desc);

	if ((hal_rx_desc_sz + l3pad_bytes + msdu_len) > DP_RX_BUFFER_SIZE) {
		ath12k_dbg(ab, ATH12K_DBG_DATA,
			   "invalid msdu len in tkip mic err %u\n", msdu_len);
		ath12k_dbg_dump(ab, ATH12K_DBG_DATA, NULL, "", desc,
				sizeof(*desc));
		return true;
	}

	skb_put(msdu, hal_rx_desc_sz + l3pad_bytes + msdu_len);
	skb_pull(msdu, hal_rx_desc_sz + l3pad_bytes);

	if (unlikely(!ath12k_dp_rx_check_nwifi_hdr_len_valid(ab, desc, msdu)))
		return true;

	ath12k_dp_rx_h_ppdu(ar, rx_info);

	rx_info->rx_status->flag |= (RX_FLAG_MMIC_STRIPPED | RX_FLAG_MMIC_ERROR |
				     RX_FLAG_DECRYPTED);

	ath12k_dp_rx_h_undecap(ar, msdu, desc,
			       HAL_ENCRYPT_TYPE_TKIP_MIC, rx_info->rx_status, false);
	return false;
}

static bool ath12k_dp_rx_h_rxdma_err(struct ath12k *ar,  struct sk_buff *msdu,
				     struct ath12k_dp_rx_info *rx_info)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	struct hal_rx_desc *rx_desc = (struct hal_rx_desc *)msdu->data;
	bool drop = false;
	u32 err_bitmap;

	ar->ab->device_stats.rxdma_error[rxcb->err_code]++;

	switch (rxcb->err_code) {
	case HAL_REO_ENTR_RING_RXDMA_ECODE_DECRYPT_ERR:
	case HAL_REO_ENTR_RING_RXDMA_ECODE_TKIP_MIC_ERR:
		err_bitmap = ath12k_dp_rx_h_mpdu_err(ab, rx_desc);
		if (err_bitmap & HAL_RX_MPDU_ERR_TKIP_MIC) {
			ath12k_dp_rx_h_fetch_info(ab, rx_desc, rx_info);
			drop = ath12k_dp_rx_h_tkip_mic_err(ar, msdu, rx_info);
			break;
		}
		fallthrough;
	default:
		/* TODO: Review other rxdma error code to check if anything is
		 * worth reporting to mac80211
		 */
		drop = true;
		break;
	}

	return drop;
}

static bool ath12k_dp_rx_h_reo_err(struct ath12k *ar, struct sk_buff *msdu,
				   struct ath12k_dp_rx_info *rx_info,
				   struct sk_buff_head *msdu_list)
{
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	bool drop = false;

	ar->ab->device_stats.reo_error[rxcb->err_code]++;

	switch (rxcb->err_code) {
	case HAL_REO_DEST_RING_ERROR_CODE_DESC_ADDR_ZERO:
		if (ath12k_dp_rx_h_null_q_desc(ar, msdu, rx_info, msdu_list))
			drop = true;
		break;
	case HAL_REO_DEST_RING_ERROR_CODE_PN_CHECK_FAILED:
		/* TODO: Do not drop PN failed packets in the driver;
		 * instead, it is good to drop such packets in mac80211
		 * after incrementing the replay counters.
		 */
		fallthrough;
	default:
		/* TODO: Review other errors and process them to mac80211
		 * as appropriate.
		 */
		drop = true;
		break;
	}

	return drop;
}

static void ath12k_dp_rx_wbm_err(struct ath12k *ar,
				 struct napi_struct *napi,
				 struct sk_buff *msdu,
				 struct sk_buff_head *msdu_list)
{
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	struct ieee80211_rx_status rxs = {};
	struct ath12k_dp_rx_info rx_info;
	bool drop = true;

	rx_info.addr2_present = false;
	rx_info.rx_status = &rxs;

	switch (rxcb->err_rel_src) {
	case HAL_WBM_REL_SRC_MODULE_REO:
		drop = ath12k_dp_rx_h_reo_err(ar, msdu, &rx_info, msdu_list);
		break;
	case HAL_WBM_REL_SRC_MODULE_RXDMA:
		drop = ath12k_dp_rx_h_rxdma_err(ar, msdu, &rx_info);
		break;
	default:
		/* msdu will get freed */
		break;
	}

	if (drop) {
		dev_kfree_skb_any(msdu);
		return;
	}

	rx_info.rx_status->flag |= RX_FLAG_SKIP_MONITOR;

	ath12k_dp_rx_deliver_msdu(ar, napi, msdu, &rx_info);
}

int ath12k_dp_rx_process_wbm_err(struct ath12k_base *ab,
				 struct napi_struct *napi, int budget)
{
	struct list_head rx_desc_used_list[ATH12K_MAX_DEVICES];
	struct ath12k_hw_group *ag = ab->ag;
	struct ath12k *ar;
	struct ath12k_dp *dp = &ab->dp;
	struct dp_rxdma_ring *rx_ring;
	struct hal_rx_wbm_rel_info err_info;
	struct hal_srng *srng;
	struct sk_buff *msdu;
	struct sk_buff_head msdu_list, scatter_msdu_list;
	struct ath12k_skb_rxcb *rxcb;
	void *rx_desc;
	int num_buffs_reaped[ATH12K_MAX_DEVICES] = {};
	int total_num_buffs_reaped = 0;
	struct ath12k_rx_desc_info *desc_info;
	struct ath12k_device_dp_stats *device_stats = &ab->device_stats;
	struct ath12k_hw_link *hw_links = ag->hw_links;
	struct ath12k_base *partner_ab;
	u8 hw_link_id, device_id;
	int ret, pdev_id;
	struct hal_rx_desc *msdu_data;

	__skb_queue_head_init(&msdu_list);
	__skb_queue_head_init(&scatter_msdu_list);

	for (device_id = 0; device_id < ATH12K_MAX_DEVICES; device_id++)
		INIT_LIST_HEAD(&rx_desc_used_list[device_id]);

	srng = &ab->hal.srng_list[dp->rx_rel_ring.ring_id];
	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

	while (budget) {
		rx_desc = ath12k_hal_srng_dst_get_next_entry(ab, srng);
		if (!rx_desc)
			break;

		ret = ath12k_hal_wbm_desc_parse_err(ab, rx_desc, &err_info);
		if (ret) {
			ath12k_warn(ab,
				    "failed to parse rx error in wbm_rel ring desc %d\n",
				    ret);
			continue;
		}

		desc_info = err_info.rx_desc;

		/* retry manual desc retrieval if hw cc is not done */
		if (!desc_info) {
			desc_info = ath12k_dp_get_rx_desc(ab, err_info.cookie);
			if (!desc_info) {
				ath12k_warn(ab, "Invalid cookie in DP WBM rx error descriptor retrieval: 0x%x\n",
					    err_info.cookie);
				continue;
			}
		}

		if (desc_info->magic != ATH12K_DP_RX_DESC_MAGIC)
			ath12k_warn(ab, "WBM RX err, Check HW CC implementation");

		msdu = desc_info->skb;
		desc_info->skb = NULL;

		device_id = desc_info->device_id;
		partner_ab = ath12k_ag_to_ab(ag, device_id);
		if (unlikely(!partner_ab)) {
			dev_kfree_skb_any(msdu);

			/* In any case continuation bit is set
			 * in the previous record, cleanup scatter_msdu_list
			 */
			ath12k_dp_clean_up_skb_list(&scatter_msdu_list);
			continue;
		}

		list_add_tail(&desc_info->list, &rx_desc_used_list[device_id]);

		rxcb = ATH12K_SKB_RXCB(msdu);
		dma_unmap_single(partner_ab->dev, rxcb->paddr,
				 msdu->len + skb_tailroom(msdu),
				 DMA_FROM_DEVICE);

		num_buffs_reaped[device_id]++;
		total_num_buffs_reaped++;

		if (!err_info.continuation)
			budget--;

		if (err_info.push_reason !=
		    HAL_REO_DEST_RING_PUSH_REASON_ERR_DETECTED) {
			dev_kfree_skb_any(msdu);
			continue;
		}

		msdu_data = (struct hal_rx_desc *)msdu->data;
		rxcb->err_rel_src = err_info.err_rel_src;
		rxcb->err_code = err_info.err_code;
		rxcb->is_first_msdu = err_info.first_msdu;
		rxcb->is_last_msdu = err_info.last_msdu;
		rxcb->is_continuation = err_info.continuation;
		rxcb->rx_desc = msdu_data;

		if (err_info.continuation) {
			__skb_queue_tail(&scatter_msdu_list, msdu);
			continue;
		}

		hw_link_id = ath12k_dp_rx_get_msdu_src_link(partner_ab,
							    msdu_data);
		if (hw_link_id >= ATH12K_GROUP_MAX_RADIO) {
			dev_kfree_skb_any(msdu);

			/* In any case continuation bit is set
			 * in the previous record, cleanup scatter_msdu_list
			 */
			ath12k_dp_clean_up_skb_list(&scatter_msdu_list);
			continue;
		}

		if (!skb_queue_empty(&scatter_msdu_list)) {
			struct sk_buff *msdu;

			skb_queue_walk(&scatter_msdu_list, msdu) {
				rxcb = ATH12K_SKB_RXCB(msdu);
				rxcb->hw_link_id = hw_link_id;
			}

			skb_queue_splice_tail_init(&scatter_msdu_list,
						   &msdu_list);
		}

		rxcb = ATH12K_SKB_RXCB(msdu);
		rxcb->hw_link_id = hw_link_id;
		__skb_queue_tail(&msdu_list, msdu);
	}

	/* In any case continuation bit is set in the
	 * last record, cleanup scatter_msdu_list
	 */
	ath12k_dp_clean_up_skb_list(&scatter_msdu_list);

	ath12k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	if (!total_num_buffs_reaped)
		goto done;

	for (device_id = 0; device_id < ATH12K_MAX_DEVICES; device_id++) {
		if (!num_buffs_reaped[device_id])
			continue;

		partner_ab = ath12k_ag_to_ab(ag, device_id);
		rx_ring = &partner_ab->dp.rx_refill_buf_ring;

		ath12k_dp_rx_bufs_replenish(ab, rx_ring,
					    &rx_desc_used_list[device_id],
					    num_buffs_reaped[device_id]);
	}

	rcu_read_lock();
	while ((msdu = __skb_dequeue(&msdu_list))) {
		rxcb = ATH12K_SKB_RXCB(msdu);
		hw_link_id = rxcb->hw_link_id;

		device_id = hw_links[hw_link_id].device_id;
		partner_ab = ath12k_ag_to_ab(ag, device_id);
		if (unlikely(!partner_ab)) {
			ath12k_dbg(ab, ATH12K_DBG_DATA,
				   "Unable to process WBM error msdu due to invalid hw link id %d device id %d\n",
				   hw_link_id, device_id);
			dev_kfree_skb_any(msdu);
			continue;
		}

		pdev_id = ath12k_hw_mac_id_to_pdev_id(partner_ab->hw_params,
						      hw_links[hw_link_id].pdev_idx);
		ar = partner_ab->pdevs[pdev_id].ar;

		if (!ar || !rcu_dereference(ar->ab->pdevs_active[pdev_id])) {
			dev_kfree_skb_any(msdu);
			continue;
		}

		if (test_bit(ATH12K_FLAG_CAC_RUNNING, &ar->dev_flags)) {
			dev_kfree_skb_any(msdu);
			continue;
		}

		if (rxcb->err_rel_src < HAL_WBM_REL_SRC_MODULE_MAX) {
			device_id = ar->ab->device_id;
			device_stats->rx_wbm_rel_source[rxcb->err_rel_src][device_id]++;
		}

		ath12k_dp_rx_wbm_err(ar, napi, msdu, &msdu_list);
	}
	rcu_read_unlock();
done:
	return total_num_buffs_reaped;
}
