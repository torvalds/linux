// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2019-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "hal_desc.h"
#include "../dp_mon.h"
#include "dp_mon.h"
#include "../debug.h"
#include "hal_qcn9274.h"
#include "dp_rx.h"
#include "../peer.h"

static int
__ath12k_wifi7_dp_mon_process_ring(struct ath12k *ar, int mac_id,
				   struct napi_struct *napi, int *budget)
{
	struct ath12k_mon_data *pmon = (struct ath12k_mon_data *)&ar->dp.mon_data;
	struct ath12k_pdev_mon_stats *rx_mon_stats = &pmon->rx_mon_stats;
	struct hal_rx_mon_ppdu_info *ppdu_info = &pmon->mon_ppdu_info;
	enum hal_rx_mon_status hal_status;
	struct sk_buff_head skb_list;
	int num_buffs_reaped;
	struct sk_buff *skb;

	__skb_queue_head_init(&skb_list);

	num_buffs_reaped = ath12k_dp_rx_reap_mon_status_ring(ar->ab, mac_id,
							     budget, &skb_list);
	if (!num_buffs_reaped)
		goto exit;

	while ((skb = __skb_dequeue(&skb_list))) {
		memset(ppdu_info, 0, sizeof(*ppdu_info));
		ppdu_info->peer_id = HAL_INVALID_PEERID;

		hal_status = ath12k_dp_mon_parse_rx_dest(&ar->dp, pmon, skb);

		if (ar->monitor_started &&
		    pmon->mon_ppdu_status == DP_PPDU_STATUS_START &&
		    hal_status == HAL_TLV_STATUS_PPDU_DONE) {
			rx_mon_stats->status_ppdu_done++;
			pmon->mon_ppdu_status = DP_PPDU_STATUS_DONE;
			ath12k_dp_rx_mon_dest_process(ar, mac_id, *budget, napi);
			pmon->mon_ppdu_status = DP_PPDU_STATUS_START;
		}

		dev_kfree_skb_any(skb);
	}

exit:
	return num_buffs_reaped;
}

static int
ath12k_wifi7_dp_mon_srng_process(struct ath12k_pdev_dp *pdev_dp, int *budget,
				 struct napi_struct *napi)
{
	struct ath12k_dp *dp = pdev_dp->dp;
	struct ath12k_base *ab = dp->ab;
	struct ath12k_mon_data *pmon = (struct ath12k_mon_data *)&pdev_dp->mon_data;
	struct hal_rx_mon_ppdu_info *ppdu_info = &pmon->mon_ppdu_info;
	struct hal_mon_dest_desc *mon_dst_desc;
	struct sk_buff *skb;
	struct ath12k_skb_rxcb *rxcb;
	struct dp_srng *mon_dst_ring;
	struct hal_srng *srng;
	struct dp_rxdma_mon_ring *buf_ring;
	struct ath12k_dp_link_peer *peer;
	struct sk_buff_head skb_list;
	u64 cookie;
	int num_buffs_reaped = 0, srng_id, buf_id;
	u32 hal_status, end_offset, info0, end_reason;
	u8 pdev_idx = ath12k_hw_mac_id_to_pdev_id(ab->hw_params, pdev_dp->mac_id);

	__skb_queue_head_init(&skb_list);
	srng_id = ath12k_hw_mac_id_to_srng_id(ab->hw_params, pdev_idx);
	mon_dst_ring = &pdev_dp->rxdma_mon_dst_ring[srng_id];
	buf_ring = &dp->rxdma_mon_buf_ring;

	srng = &ab->hal.srng_list[mon_dst_ring->ring_id];
	spin_lock_bh(&srng->lock);
	ath12k_hal_srng_access_begin(ab, srng);

	while (likely(*budget)) {
		mon_dst_desc = ath12k_hal_srng_dst_peek(ab, srng);
		if (unlikely(!mon_dst_desc))
			break;

		/* In case of empty descriptor, the cookie in the ring descriptor
		 * is invalid. Therefore, this entry is skipped, and ring processing
		 * continues.
		 */
		info0 = le32_to_cpu(mon_dst_desc->info0);
		if (u32_get_bits(info0, HAL_MON_DEST_INFO0_EMPTY_DESC))
			goto move_next;

		cookie = le32_to_cpu(mon_dst_desc->cookie);
		buf_id = u32_get_bits(cookie, DP_RXDMA_BUF_COOKIE_BUF_ID);

		spin_lock_bh(&buf_ring->idr_lock);
		skb = idr_remove(&buf_ring->bufs_idr, buf_id);
		spin_unlock_bh(&buf_ring->idr_lock);

		if (unlikely(!skb)) {
			ath12k_warn(ab, "monitor destination with invalid buf_id %d\n",
				    buf_id);
			goto move_next;
		}

		rxcb = ATH12K_SKB_RXCB(skb);
		dma_unmap_single(ab->dev, rxcb->paddr,
				 skb->len + skb_tailroom(skb),
				 DMA_FROM_DEVICE);

		end_reason = u32_get_bits(info0, HAL_MON_DEST_INFO0_END_REASON);

		/* HAL_MON_FLUSH_DETECTED implies that an rx flush received at the end of
		 * rx PPDU and HAL_MON_PPDU_TRUNCATED implies that the PPDU got
		 * truncated due to a system level error. In both the cases, buffer data
		 * can be discarded
		 */
		if ((end_reason == HAL_MON_FLUSH_DETECTED) ||
		    (end_reason == HAL_MON_PPDU_TRUNCATED)) {
			ath12k_dbg(ab, ATH12K_DBG_DATA,
				   "Monitor dest descriptor end reason %d", end_reason);
			dev_kfree_skb_any(skb);
			goto move_next;
		}

		/* Calculate the budget when the ring descriptor with the
		 * HAL_MON_END_OF_PPDU to ensure that one PPDU worth of data is always
		 * reaped. This helps to efficiently utilize the NAPI budget.
		 */
		if (end_reason == HAL_MON_END_OF_PPDU) {
			*budget -= 1;
			rxcb->is_end_of_ppdu = true;
		}

		end_offset = u32_get_bits(info0, HAL_MON_DEST_INFO0_END_OFFSET);
		if (likely(end_offset <= DP_RX_BUFFER_SIZE)) {
			skb_put(skb, end_offset);
		} else {
			ath12k_warn(ab,
				    "invalid offset on mon stats destination %u\n",
				    end_offset);
			skb_put(skb, DP_RX_BUFFER_SIZE);
		}

		__skb_queue_tail(&skb_list, skb);

move_next:
		ath12k_dp_mon_buf_replenish(ab, buf_ring, 1);
		ath12k_hal_srng_dst_get_next_entry(ab, srng);
		num_buffs_reaped++;
	}

	ath12k_hal_srng_access_end(ab, srng);
	spin_unlock_bh(&srng->lock);

	if (!num_buffs_reaped)
		return 0;

	/* In some cases, one PPDU worth of data can be spread across multiple NAPI
	 * schedules, To avoid losing existing parsed ppdu_info information, skip
	 * the memset of the ppdu_info structure and continue processing it.
	 */
	if (!ppdu_info->ppdu_continuation)
		ath12k_dp_mon_rx_memset_ppdu_info(ppdu_info);

	while ((skb = __skb_dequeue(&skb_list))) {
		hal_status = ath12k_dp_mon_rx_parse_mon_status(pdev_dp, pmon, skb, napi);
		if (hal_status != HAL_RX_MON_STATUS_PPDU_DONE) {
			ppdu_info->ppdu_continuation = true;
			dev_kfree_skb_any(skb);
			continue;
		}

		if (ppdu_info->peer_id == HAL_INVALID_PEERID)
			goto free_skb;

		rcu_read_lock();
		peer = ath12k_dp_link_peer_find_by_peerid(pdev_dp, ppdu_info->peer_id);
		if (!peer || !peer->sta) {
			ath12k_dbg(ab, ATH12K_DBG_DATA,
				   "failed to find the peer with monitor peer_id %d\n",
				   ppdu_info->peer_id);
			goto next_skb;
		}

		if (ppdu_info->reception_type == HAL_RX_RECEPTION_TYPE_SU) {
			ath12k_dp_mon_rx_update_peer_su_stats(peer, ppdu_info);
		} else if ((ppdu_info->fc_valid) &&
			   (ppdu_info->ast_index != HAL_AST_IDX_INVALID)) {
			ath12k_dp_mon_rx_process_ulofdma(ppdu_info);
			ath12k_dp_mon_rx_update_peer_mu_stats(ab, ppdu_info);
		}

next_skb:
		rcu_read_unlock();
free_skb:
		dev_kfree_skb_any(skb);
		ath12k_dp_mon_rx_memset_ppdu_info(ppdu_info);
	}

	return num_buffs_reaped;
}

int ath12k_wifi7_dp_mon_process_ring(struct ath12k_dp *dp, int mac_id,
				     struct napi_struct *napi, int budget,
				     enum dp_monitor_mode monitor_mode)
{
	u8 pdev_idx = ath12k_hw_mac_id_to_pdev_id(dp->hw_params, mac_id);
	struct ath12k_pdev_dp *dp_pdev;
	struct ath12k *ar;
	int num_buffs_reaped = 0;

	rcu_read_lock();

	dp_pdev = ath12k_dp_to_pdev_dp(dp, pdev_idx);
	if (!dp_pdev) {
		rcu_read_unlock();
		return 0;
	}

	if (dp->hw_params->rxdma1_enable) {
		if (monitor_mode == ATH12K_DP_RX_MONITOR_MODE)
			num_buffs_reaped = ath12k_wifi7_dp_mon_srng_process(dp_pdev,
									    &budget,
									    napi);
	} else {
		ar = ath12k_pdev_dp_to_ar(dp_pdev);

		if (ar->monitor_started)
			num_buffs_reaped =
				__ath12k_wifi7_dp_mon_process_ring(ar, mac_id, napi,
								   &budget);
	}

	rcu_read_unlock();

	return num_buffs_reaped;
}
