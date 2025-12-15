// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/ieee80211.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <crypto/hash.h>
#include "core.h"
#include "debug.h"
#include "hw.h"
#include "dp_rx.h"
#include "dp_tx.h"
#include "peer.h"
#include "dp_mon.h"
#include "debugfs_htt_stats.h"

static int ath12k_dp_rx_tid_delete_handler(struct ath12k_base *ab,
					   struct ath12k_dp_rx_tid_rxq *rx_tid);

static size_t ath12k_dp_list_cut_nodes(struct list_head *list,
				       struct list_head *head,
				       size_t count)
{
	struct list_head *cur;
	struct ath12k_rx_desc_info *rx_desc;
	size_t nodes = 0;

	if (!count) {
		INIT_LIST_HEAD(list);
		goto out;
	}

	list_for_each(cur, head) {
		if (!count)
			break;

		rx_desc = list_entry(cur, struct ath12k_rx_desc_info, list);
		rx_desc->in_use = true;

		count--;
		nodes++;
	}

	list_cut_before(list, head, cur);
out:
	return nodes;
}

static void ath12k_dp_rx_enqueue_free(struct ath12k_dp *dp,
				      struct list_head *used_list)
{
	struct ath12k_rx_desc_info *rx_desc, *safe;

	/* Reset the use flag */
	list_for_each_entry_safe(rx_desc, safe, used_list, list)
		rx_desc->in_use = false;

	spin_lock_bh(&dp->rx_desc_lock);
	list_splice_tail(used_list, &dp->rx_desc_free_list);
	spin_unlock_bh(&dp->rx_desc_lock);
}

/* Returns number of Rx buffers replenished */
int ath12k_dp_rx_bufs_replenish(struct ath12k_dp *dp,
				struct dp_rxdma_ring *rx_ring,
				struct list_head *used_list,
				int req_entries)
{
	struct ath12k_base *ab = dp->ab;
	struct ath12k_buffer_addr *desc;
	struct hal_srng *srng;
	struct sk_buff *skb;
	int num_free;
	int num_remain;
	u32 cookie;
	dma_addr_t paddr;
	struct ath12k_rx_desc_info *rx_desc;
	enum hal_rx_buf_return_buf_manager mgr = dp->hal->hal_params->rx_buf_rbm;

	req_entries = min(req_entries, rx_ring->bufs_max);

	srng = &dp->hal->srng_list[rx_ring->refill_buf_ring.ring_id];

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

	num_free = ath12k_hal_srng_src_num_free(ab, srng, true);
	if (!req_entries && (num_free > (rx_ring->bufs_max * 3) / 4))
		req_entries = num_free;

	req_entries = min(num_free, req_entries);
	num_remain = req_entries;

	if (!num_remain)
		goto out;

	/* Get the descriptor from free list */
	if (list_empty(used_list)) {
		spin_lock_bh(&dp->rx_desc_lock);
		req_entries = ath12k_dp_list_cut_nodes(used_list,
						       &dp->rx_desc_free_list,
						       num_remain);
		spin_unlock_bh(&dp->rx_desc_lock);
		num_remain = req_entries;
	}

	while (num_remain > 0) {
		skb = dev_alloc_skb(DP_RX_BUFFER_SIZE +
				    DP_RX_BUFFER_ALIGN_SIZE);
		if (!skb)
			break;

		if (!IS_ALIGNED((unsigned long)skb->data,
				DP_RX_BUFFER_ALIGN_SIZE)) {
			skb_pull(skb,
				 PTR_ALIGN(skb->data, DP_RX_BUFFER_ALIGN_SIZE) -
				 skb->data);
		}

		paddr = dma_map_single(dp->dev, skb->data,
				       skb->len + skb_tailroom(skb),
				       DMA_FROM_DEVICE);
		if (dma_mapping_error(dp->dev, paddr))
			goto fail_free_skb;

		rx_desc = list_first_entry_or_null(used_list,
						   struct ath12k_rx_desc_info,
						   list);
		if (!rx_desc)
			goto fail_dma_unmap;

		rx_desc->skb = skb;
		cookie = rx_desc->cookie;

		desc = ath12k_hal_srng_src_get_next_entry(ab, srng);
		if (!desc)
			goto fail_dma_unmap;

		list_del(&rx_desc->list);
		ATH12K_SKB_RXCB(skb)->paddr = paddr;

		num_remain--;

		ath12k_hal_rx_buf_addr_info_set(dp->hal, desc, paddr, cookie,
						mgr);
	}

	goto out;

fail_dma_unmap:
	dma_unmap_single(dp->dev, paddr, skb->len + skb_tailroom(skb),
			 DMA_FROM_DEVICE);
fail_free_skb:
	dev_kfree_skb_any(skb);
out:
	ath12k_hal_srng_access_end(ab, srng);

	if (!list_empty(used_list))
		ath12k_dp_rx_enqueue_free(dp, used_list);

	spin_unlock_bh(&srng->lock);

	return req_entries - num_remain;
}
EXPORT_SYMBOL(ath12k_dp_rx_bufs_replenish);

static int ath12k_dp_rxdma_mon_buf_ring_free(struct ath12k_base *ab,
					     struct dp_rxdma_mon_ring *rx_ring)
{
	struct sk_buff *skb;
	int buf_id;

	spin_lock_bh(&rx_ring->idr_lock);
	idr_for_each_entry(&rx_ring->bufs_idr, skb, buf_id) {
		idr_remove(&rx_ring->bufs_idr, buf_id);
		/* TODO: Understand where internal driver does this dma_unmap
		 * of rxdma_buffer.
		 */
		dma_unmap_single(ab->dev, ATH12K_SKB_RXCB(skb)->paddr,
				 skb->len + skb_tailroom(skb), DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb);
	}

	idr_destroy(&rx_ring->bufs_idr);
	spin_unlock_bh(&rx_ring->idr_lock);

	return 0;
}

static int ath12k_dp_rxdma_buf_free(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	int i;

	ath12k_dp_rxdma_mon_buf_ring_free(ab, &dp->rxdma_mon_buf_ring);

	if (ab->hw_params->rxdma1_enable)
		return 0;

	for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++)
		ath12k_dp_rxdma_mon_buf_ring_free(ab,
						  &dp->rx_mon_status_refill_ring[i]);

	return 0;
}

static int ath12k_dp_rxdma_mon_ring_buf_setup(struct ath12k_base *ab,
					      struct dp_rxdma_mon_ring *rx_ring,
					      u32 ringtype)
{
	int num_entries;

	num_entries = rx_ring->refill_buf_ring.size /
		ath12k_hal_srng_get_entrysize(ab, ringtype);

	rx_ring->bufs_max = num_entries;

	if (ringtype == HAL_RXDMA_MONITOR_STATUS)
		ath12k_dp_mon_status_bufs_replenish(ab, rx_ring,
						    num_entries);
	else
		ath12k_dp_mon_buf_replenish(ab, rx_ring, num_entries);

	return 0;
}

static int ath12k_dp_rxdma_ring_buf_setup(struct ath12k_base *ab,
					  struct dp_rxdma_ring *rx_ring)
{
	LIST_HEAD(list);

	rx_ring->bufs_max = rx_ring->refill_buf_ring.size /
			ath12k_hal_srng_get_entrysize(ab, HAL_RXDMA_BUF);

	ath12k_dp_rx_bufs_replenish(ath12k_ab_to_dp(ab), rx_ring, &list, 0);

	return 0;
}

static int ath12k_dp_rxdma_buf_setup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct dp_rxdma_mon_ring *mon_ring;
	int ret, i;

	ret = ath12k_dp_rxdma_ring_buf_setup(ab, &dp->rx_refill_buf_ring);
	if (ret) {
		ath12k_warn(ab,
			    "failed to setup HAL_RXDMA_BUF\n");
		return ret;
	}

	if (ab->hw_params->rxdma1_enable) {
		ret = ath12k_dp_rxdma_mon_ring_buf_setup(ab,
							 &dp->rxdma_mon_buf_ring,
							 HAL_RXDMA_MONITOR_BUF);
		if (ret)
			ath12k_warn(ab,
				    "failed to setup HAL_RXDMA_MONITOR_BUF\n");
		return ret;
	}

	for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++) {
		mon_ring = &dp->rx_mon_status_refill_ring[i];
		ret = ath12k_dp_rxdma_mon_ring_buf_setup(ab, mon_ring,
							 HAL_RXDMA_MONITOR_STATUS);
		if (ret) {
			ath12k_warn(ab,
				    "failed to setup HAL_RXDMA_MONITOR_STATUS\n");
			return ret;
		}
	}

	return 0;
}

static void ath12k_dp_rx_pdev_srng_free(struct ath12k *ar)
{
	struct ath12k_pdev_dp *dp = &ar->dp;
	struct ath12k_base *ab = ar->ab;
	int i;

	for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++)
		ath12k_dp_srng_cleanup(ab, &dp->rxdma_mon_dst_ring[i]);
}

void ath12k_dp_rx_pdev_reo_cleanup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	int i;

	for (i = 0; i < DP_REO_DST_RING_MAX; i++)
		ath12k_dp_srng_cleanup(ab, &dp->reo_dst_ring[i]);
}

int ath12k_dp_rx_pdev_reo_setup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	int ret;
	int i;

	for (i = 0; i < DP_REO_DST_RING_MAX; i++) {
		ret = ath12k_dp_srng_setup(ab, &dp->reo_dst_ring[i],
					   HAL_REO_DST, i, 0,
					   DP_REO_DST_RING_SIZE);
		if (ret) {
			ath12k_warn(ab, "failed to setup reo_dst_ring\n");
			goto err_reo_cleanup;
		}
	}

	return 0;

err_reo_cleanup:
	ath12k_dp_rx_pdev_reo_cleanup(ab);

	return ret;
}

static int ath12k_dp_rx_pdev_srng_alloc(struct ath12k *ar)
{
	struct ath12k_pdev_dp *dp = &ar->dp;
	struct ath12k_base *ab = ar->ab;
	int i;
	int ret;
	u32 mac_id = dp->mac_id;

	for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++) {
		ret = ath12k_dp_srng_setup(ar->ab,
					   &dp->rxdma_mon_dst_ring[i],
					   HAL_RXDMA_MONITOR_DST,
					   0, mac_id + i,
					   DP_RXDMA_MONITOR_DST_RING_SIZE(ab));
		if (ret) {
			ath12k_warn(ar->ab,
				    "failed to setup HAL_RXDMA_MONITOR_DST\n");
			return ret;
		}
	}

	return 0;
}

void ath12k_dp_init_rx_tid_rxq(struct ath12k_dp_rx_tid_rxq *rx_tid_rxq,
			       struct ath12k_dp_rx_tid *rx_tid,
			       bool active)
{
	rx_tid_rxq->tid = rx_tid->tid;
	rx_tid_rxq->active = active;
	rx_tid_rxq->qbuf = rx_tid->qbuf;
}
EXPORT_SYMBOL(ath12k_dp_init_rx_tid_rxq);

static void ath12k_dp_rx_tid_cleanup(struct ath12k_base *ab,
				     struct ath12k_reoq_buf *tid_qbuf)
{
	if (tid_qbuf->vaddr) {
		dma_unmap_single(ab->dev, tid_qbuf->paddr_aligned,
				 tid_qbuf->size, DMA_BIDIRECTIONAL);
		kfree(tid_qbuf->vaddr);
		tid_qbuf->vaddr = NULL;
	}
}

void ath12k_dp_rx_reo_cmd_list_cleanup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_dp_rx_reo_cmd *cmd, *tmp;
	struct ath12k_dp_rx_reo_cache_flush_elem *cmd_cache, *tmp_cache;
	struct dp_reo_update_rx_queue_elem *cmd_queue, *tmp_queue;

	spin_lock_bh(&dp->reo_rxq_flush_lock);
	list_for_each_entry_safe(cmd_queue, tmp_queue, &dp->reo_cmd_update_rx_queue_list,
				 list) {
		list_del(&cmd_queue->list);
		ath12k_dp_rx_tid_cleanup(ab, &cmd_queue->rx_tid.qbuf);
		kfree(cmd_queue);
	}
	list_for_each_entry_safe(cmd_cache, tmp_cache,
				 &dp->reo_cmd_cache_flush_list, list) {
		list_del(&cmd_cache->list);
		dp->reo_cmd_cache_flush_count--;
		ath12k_dp_rx_tid_cleanup(ab, &cmd_cache->data.qbuf);
		kfree(cmd_cache);
	}
	spin_unlock_bh(&dp->reo_rxq_flush_lock);

	spin_lock_bh(&dp->reo_cmd_lock);
	list_for_each_entry_safe(cmd, tmp, &dp->reo_cmd_list, list) {
		list_del(&cmd->list);
		ath12k_dp_rx_tid_cleanup(ab, &cmd->data.qbuf);
		kfree(cmd);
	}
	spin_unlock_bh(&dp->reo_cmd_lock);
}

void ath12k_dp_reo_cmd_free(struct ath12k_dp *dp, void *ctx,
			    enum hal_reo_cmd_status status)
{
	struct ath12k_dp_rx_tid_rxq *rx_tid = ctx;

	if (status != HAL_REO_CMD_SUCCESS)
		ath12k_warn(dp->ab, "failed to flush rx tid hw desc, tid %d status %d\n",
			    rx_tid->tid, status);

	ath12k_dp_rx_tid_cleanup(dp->ab, &rx_tid->qbuf);
}
EXPORT_SYMBOL(ath12k_dp_reo_cmd_free);

void ath12k_dp_rx_process_reo_cmd_update_rx_queue_list(struct ath12k_dp *dp)
{
	struct ath12k_base *ab = dp->ab;
	struct dp_reo_update_rx_queue_elem *elem, *tmp;

	spin_lock_bh(&dp->reo_rxq_flush_lock);

	list_for_each_entry_safe(elem, tmp, &dp->reo_cmd_update_rx_queue_list, list) {
		if (elem->rx_tid.active)
			continue;

		if (ath12k_dp_rx_tid_delete_handler(ab, &elem->rx_tid))
			break;

		ath12k_dp_arch_peer_rx_tid_qref_reset(dp,
						      elem->is_ml_peer ?
						      elem->ml_peer_id : elem->peer_id,
						      elem->rx_tid.tid);

		if (ab->hw_params->reoq_lut_support)
			ath12k_hal_reo_shared_qaddr_cache_clear(ab);

		list_del(&elem->list);
		kfree(elem);
	}

	spin_unlock_bh(&dp->reo_rxq_flush_lock);
}
EXPORT_SYMBOL(ath12k_dp_rx_process_reo_cmd_update_rx_queue_list);

void ath12k_dp_rx_tid_del_func(struct ath12k_dp *dp, void *ctx,
			       enum hal_reo_cmd_status status)
{
	struct ath12k_base *ab = dp->ab;
	struct ath12k_dp_rx_tid_rxq *rx_tid = ctx;
	struct ath12k_dp_rx_reo_cache_flush_elem *elem, *tmp;

	if (status == HAL_REO_CMD_DRAIN) {
		goto free_desc;
	} else if (status != HAL_REO_CMD_SUCCESS) {
		/* Shouldn't happen! Cleanup in case of other failure? */
		ath12k_warn(ab, "failed to delete rx tid %d hw descriptor %d\n",
			    rx_tid->tid, status);
		return;
	}

	/* Retry the HAL_REO_CMD_UPDATE_RX_QUEUE command for entries
	 * in the pending queue list marked TID as inactive
	 */
	spin_lock_bh(&dp->dp_lock);
	ath12k_dp_rx_process_reo_cmd_update_rx_queue_list(dp);
	spin_unlock_bh(&dp->dp_lock);

	elem = kzalloc(sizeof(*elem), GFP_ATOMIC);
	if (!elem)
		goto free_desc;

	elem->ts = jiffies;
	memcpy(&elem->data, rx_tid, sizeof(*rx_tid));

	spin_lock_bh(&dp->reo_rxq_flush_lock);
	list_add_tail(&elem->list, &dp->reo_cmd_cache_flush_list);
	dp->reo_cmd_cache_flush_count++;

	/* Flush and invalidate aged REO desc from HW cache */
	list_for_each_entry_safe(elem, tmp, &dp->reo_cmd_cache_flush_list,
				 list) {
		if (dp->reo_cmd_cache_flush_count > ATH12K_DP_RX_REO_DESC_FREE_THRES ||
		    time_after(jiffies, elem->ts +
			       msecs_to_jiffies(ATH12K_DP_RX_REO_DESC_FREE_TIMEOUT_MS))) {
			/* The reo_cmd_cache_flush_list is used in only two contexts,
			 * one is in this function called from napi and the
			 * other in ath12k_dp_free during core destroy.
			 * If cache command sent is success, delete the element in
			 * the cache list. ath12k_dp_rx_reo_cmd_list_cleanup
			 * will be called during core destroy.
			 */

			if (ath12k_dp_arch_reo_cache_flush(dp, &elem->data))
				break;

			list_del(&elem->list);
			dp->reo_cmd_cache_flush_count--;

			kfree(elem);
		}
	}
	spin_unlock_bh(&dp->reo_rxq_flush_lock);

	return;
free_desc:
	ath12k_dp_rx_tid_cleanup(ab, &rx_tid->qbuf);
}
EXPORT_SYMBOL(ath12k_dp_rx_tid_del_func);

static int ath12k_dp_rx_tid_delete_handler(struct ath12k_base *ab,
					   struct ath12k_dp_rx_tid_rxq *rx_tid)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);

	return ath12k_dp_arch_rx_tid_delete_handler(dp, rx_tid);
}

void ath12k_dp_mark_tid_as_inactive(struct ath12k_dp *dp, int peer_id, u8 tid)
{
	struct dp_reo_update_rx_queue_elem *elem;
	struct ath12k_dp_rx_tid_rxq *rx_tid;

	spin_lock_bh(&dp->reo_rxq_flush_lock);
	list_for_each_entry(elem, &dp->reo_cmd_update_rx_queue_list, list) {
		if (elem->peer_id == peer_id) {
			rx_tid = &elem->rx_tid;
			if (rx_tid->tid == tid) {
				rx_tid->active = false;
				break;
			}
		}
	}
	spin_unlock_bh(&dp->reo_rxq_flush_lock);
}
EXPORT_SYMBOL(ath12k_dp_mark_tid_as_inactive);

void ath12k_dp_rx_peer_tid_cleanup(struct ath12k *ar, struct ath12k_dp_link_peer *peer)
{
	struct ath12k_dp_rx_tid *rx_tid;
	int i;
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);

	lockdep_assert_held(&dp->dp_lock);

	if (!peer->primary_link)
		return;

	for (i = 0; i <= IEEE80211_NUM_TIDS; i++) {
		rx_tid = &peer->dp_peer->rx_tid[i];

		ath12k_dp_arch_rx_peer_tid_delete(dp, peer, i);
		ath12k_dp_arch_rx_frags_cleanup(dp, rx_tid, true);

		spin_unlock_bh(&dp->dp_lock);
		timer_delete_sync(&rx_tid->frag_timer);
		spin_lock_bh(&dp->dp_lock);
	}
}

static int ath12k_dp_prepare_reo_update_elem(struct ath12k_dp *dp,
					     struct ath12k_dp_link_peer *peer,
					     struct ath12k_dp_rx_tid *rx_tid)
{
	struct dp_reo_update_rx_queue_elem *elem;

	lockdep_assert_held(&dp->dp_lock);

	elem = kzalloc(sizeof(*elem), GFP_ATOMIC);
	if (!elem)
		return -ENOMEM;

	elem->peer_id = peer->peer_id;
	elem->is_ml_peer = peer->mlo;
	elem->ml_peer_id = peer->ml_id;

	ath12k_dp_init_rx_tid_rxq(&elem->rx_tid, rx_tid,
				  (peer->rx_tid_active_bitmask & (1 << rx_tid->tid)));

	spin_lock_bh(&dp->reo_rxq_flush_lock);
	list_add_tail(&elem->list, &dp->reo_cmd_update_rx_queue_list);
	spin_unlock_bh(&dp->reo_rxq_flush_lock);

	return 0;
}

int ath12k_dp_rx_peer_tid_setup(struct ath12k *ar, const u8 *peer_mac, int vdev_id,
				u8 tid, u32 ba_win_sz, u16 ssn,
				enum hal_pn_type pn_type)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_dp_link_peer *peer;
	struct ath12k_dp_rx_tid *rx_tid;
	dma_addr_t paddr_aligned;
	int ret;

	spin_lock_bh(&dp->dp_lock);

	peer = ath12k_dp_link_peer_find_by_vdev_and_addr(dp, vdev_id, peer_mac);
	if (!peer || !peer->dp_peer) {
		spin_unlock_bh(&dp->dp_lock);
		ath12k_warn(ab, "failed to find the peer to set up rx tid\n");
		return -ENOENT;
	}

	if (ab->hw_params->dp_primary_link_only &&
	    !peer->primary_link) {
		spin_unlock_bh(&dp->dp_lock);
		return 0;
	}

	if (ab->hw_params->reoq_lut_support &&
	    (!dp->reoq_lut.vaddr || !dp->ml_reoq_lut.vaddr)) {
		spin_unlock_bh(&dp->dp_lock);
		ath12k_warn(ab, "reo qref table is not setup\n");
		return -EINVAL;
	}

	if (peer->peer_id > DP_MAX_PEER_ID || tid > IEEE80211_NUM_TIDS) {
		ath12k_warn(ab, "peer id of peer %d or tid %d doesn't allow reoq setup\n",
			    peer->peer_id, tid);
		spin_unlock_bh(&dp->dp_lock);
		return -EINVAL;
	}

	rx_tid = &peer->dp_peer->rx_tid[tid];
	/* Update the tid queue if it is already setup */
	if (peer->rx_tid_active_bitmask & (1 << tid)) {
		ret = ath12k_dp_arch_peer_rx_tid_reo_update(dp, peer, rx_tid,
							    ba_win_sz, ssn, true);
		spin_unlock_bh(&dp->dp_lock);
		if (ret) {
			ath12k_warn(ab, "failed to update reo for rx tid %d\n", tid);
			return ret;
		}

		if (!ab->hw_params->reoq_lut_support) {
			paddr_aligned = rx_tid->qbuf.paddr_aligned;
			ret = ath12k_wmi_peer_rx_reorder_queue_setup(ar, vdev_id,
								     peer_mac,
								     paddr_aligned, tid,
								     1, ba_win_sz);
			if (ret) {
				ath12k_warn(ab, "failed to setup peer rx reorder queuefor tid %d: %d\n",
					    tid, ret);
				return ret;
			}
		}

		return 0;
	}

	rx_tid->tid = tid;

	rx_tid->ba_win_sz = ba_win_sz;

	ret = ath12k_dp_arch_rx_assign_reoq(dp, peer->dp_peer, rx_tid, ssn, pn_type);
	if (ret) {
		spin_unlock_bh(&dp->dp_lock);
		ath12k_warn(ab, "failed to assign reoq buf for rx tid %u\n", tid);
		return ret;
	}

	peer->rx_tid_active_bitmask |= (1 << tid);

	/* Pre-allocate the update_rxq_list for the corresponding tid
	 * This will be used during the tid delete. The reason we are not
	 * allocating during tid delete is that, if any alloc fail in update_rxq_list
	 * we may not be able to delete the tid vaddr/paddr and may lead to leak
	 */
	ret = ath12k_dp_prepare_reo_update_elem(dp, peer, rx_tid);
	if (ret) {
		ath12k_warn(ab, "failed to alloc update_rxq_list for rx tid %u\n", tid);
		ath12k_dp_rx_tid_cleanup(ab, &rx_tid->qbuf);
		spin_unlock_bh(&dp->dp_lock);
		return ret;
	}

	paddr_aligned = rx_tid->qbuf.paddr_aligned;
	if (ab->hw_params->reoq_lut_support) {
		/* Update the REO queue LUT at the corresponding peer id
		 * and tid with qaddr.
		 */
		if (peer->mlo)
			ath12k_dp_arch_peer_rx_tid_qref_setup(dp, peer->ml_id, tid,
							      paddr_aligned);
		else
			ath12k_dp_arch_peer_rx_tid_qref_setup(dp, peer->peer_id, tid,
							      paddr_aligned);

		spin_unlock_bh(&dp->dp_lock);
	} else {
		spin_unlock_bh(&dp->dp_lock);
		ret = ath12k_wmi_peer_rx_reorder_queue_setup(ar, vdev_id, peer_mac,
							     paddr_aligned, tid, 1,
							     ba_win_sz);
	}

	return ret;
}

int ath12k_dp_rx_ampdu_start(struct ath12k *ar,
			     struct ieee80211_ampdu_params *params,
			     u8 link_id)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_sta *ahsta = ath12k_sta_to_ahsta(params->sta);
	struct ath12k_link_sta *arsta;
	int vdev_id;
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	arsta = wiphy_dereference(ath12k_ar_to_hw(ar)->wiphy,
				  ahsta->link[link_id]);
	if (!arsta)
		return -ENOLINK;

	vdev_id = arsta->arvif->vdev_id;

	ret = ath12k_dp_rx_peer_tid_setup(ar, arsta->addr, vdev_id,
					  params->tid, params->buf_size,
					  params->ssn, arsta->ahsta->pn_type);
	if (ret)
		ath12k_warn(ab, "failed to setup rx tid %d\n", ret);

	return ret;
}

int ath12k_dp_rx_ampdu_stop(struct ath12k *ar,
			    struct ieee80211_ampdu_params *params,
			    u8 link_id)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_dp_link_peer *peer;
	struct ath12k_sta *ahsta = ath12k_sta_to_ahsta(params->sta);
	struct ath12k_link_sta *arsta;
	int vdev_id;
	bool active;
	int ret;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	arsta = wiphy_dereference(ath12k_ar_to_hw(ar)->wiphy,
				  ahsta->link[link_id]);
	if (!arsta)
		return -ENOLINK;

	vdev_id = arsta->arvif->vdev_id;

	spin_lock_bh(&dp->dp_lock);

	peer = ath12k_dp_link_peer_find_by_vdev_and_addr(dp, vdev_id, arsta->addr);
	if (!peer || !peer->dp_peer) {
		spin_unlock_bh(&dp->dp_lock);
		ath12k_warn(ab, "failed to find the peer to stop rx aggregation\n");
		return -ENOENT;
	}

	if (ab->hw_params->dp_primary_link_only &&
	    !peer->primary_link) {
		spin_unlock_bh(&dp->dp_lock);
		return 0;
	}

	active = peer->rx_tid_active_bitmask & (1 << params->tid);
	if (!active) {
		spin_unlock_bh(&dp->dp_lock);
		return 0;
	}

	ret = ath12k_dp_arch_peer_rx_tid_reo_update(dp, peer, peer->dp_peer->rx_tid,
						    1, 0, false);
	spin_unlock_bh(&dp->dp_lock);
	if (ret) {
		ath12k_warn(ab, "failed to update reo for rx tid %d: %d\n",
			    params->tid, ret);
		return ret;
	}

	return ret;
}

int ath12k_dp_rx_peer_pn_replay_config(struct ath12k_link_vif *arvif,
				       const u8 *peer_addr,
				       enum set_key_cmd key_cmd,
				       struct ieee80211_key_conf *key)
{
	struct ath12k *ar = arvif->ar;
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_hal_reo_cmd cmd = {};
	struct ath12k_dp_link_peer *peer;
	struct ath12k_dp_rx_tid *rx_tid;
	struct ath12k_dp_rx_tid_rxq rx_tid_rxq;
	u8 tid;
	int ret = 0;

	/* NOTE: Enable PN/TSC replay check offload only for unicast frames.
	 * We use mac80211 PN/TSC replay check functionality for bcast/mcast
	 * for now.
	 */
	if (!(key->flags & IEEE80211_KEY_FLAG_PAIRWISE))
		return 0;

	spin_lock_bh(&dp->dp_lock);

	peer = ath12k_dp_link_peer_find_by_vdev_and_addr(dp, arvif->vdev_id,
							 peer_addr);
	if (!peer || !peer->dp_peer) {
		spin_unlock_bh(&dp->dp_lock);
		ath12k_warn(ab, "failed to find the peer %pM to configure pn replay detection\n",
			    peer_addr);
		return -ENOENT;
	}

	for (tid = 0; tid <= IEEE80211_NUM_TIDS; tid++) {
		if (!(peer->rx_tid_active_bitmask & (1 << tid)))
			continue;

		rx_tid = &peer->dp_peer->rx_tid[tid];
		ath12k_dp_init_rx_tid_rxq(&rx_tid_rxq, rx_tid,
					  (peer->rx_tid_active_bitmask & (1 << tid)));
		ath12k_dp_arch_setup_pn_check_reo_cmd(dp, &cmd, rx_tid, key->cipher,
						      key_cmd);
		ret = ath12k_dp_arch_reo_cmd_send(dp, &rx_tid_rxq,
						  HAL_REO_CMD_UPDATE_RX_QUEUE,
						  &cmd, NULL);
		if (ret) {
			ath12k_warn(ab, "failed to configure rx tid %d queue of peer %pM for pn replay detection %d\n",
				    tid, peer_addr, ret);
			break;
		}
	}

	spin_unlock_bh(&dp->dp_lock);

	return ret;
}
EXPORT_SYMBOL(ath12k_dp_rx_get_msdu_last_buf);

struct sk_buff *ath12k_dp_rx_get_msdu_last_buf(struct sk_buff_head *msdu_list,
					       struct sk_buff *first)
{
	struct sk_buff *skb;
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(first);

	if (!rxcb->is_continuation)
		return first;

	skb_queue_walk(msdu_list, skb) {
		rxcb = ATH12K_SKB_RXCB(skb);
		if (!rxcb->is_continuation)
			return skb;
	}

	return NULL;
}

int ath12k_dp_rx_crypto_mic_len(struct ath12k_dp *dp, enum hal_encrypt_type enctype)
{
	switch (enctype) {
	case HAL_ENCRYPT_TYPE_OPEN:
	case HAL_ENCRYPT_TYPE_TKIP_NO_MIC:
	case HAL_ENCRYPT_TYPE_TKIP_MIC:
		return 0;
	case HAL_ENCRYPT_TYPE_CCMP_128:
		return IEEE80211_CCMP_MIC_LEN;
	case HAL_ENCRYPT_TYPE_CCMP_256:
		return IEEE80211_CCMP_256_MIC_LEN;
	case HAL_ENCRYPT_TYPE_GCMP_128:
	case HAL_ENCRYPT_TYPE_AES_GCMP_256:
		return IEEE80211_GCMP_MIC_LEN;
	case HAL_ENCRYPT_TYPE_WEP_40:
	case HAL_ENCRYPT_TYPE_WEP_104:
	case HAL_ENCRYPT_TYPE_WEP_128:
	case HAL_ENCRYPT_TYPE_WAPI_GCM_SM4:
	case HAL_ENCRYPT_TYPE_WAPI:
		break;
	}

	ath12k_warn(dp->ab, "unsupported encryption type %d for mic len\n", enctype);
	return 0;
}

static int ath12k_dp_rx_crypto_param_len(struct ath12k_pdev_dp *dp_pdev,
					 enum hal_encrypt_type enctype)
{
	switch (enctype) {
	case HAL_ENCRYPT_TYPE_OPEN:
		return 0;
	case HAL_ENCRYPT_TYPE_TKIP_NO_MIC:
	case HAL_ENCRYPT_TYPE_TKIP_MIC:
		return IEEE80211_TKIP_IV_LEN;
	case HAL_ENCRYPT_TYPE_CCMP_128:
		return IEEE80211_CCMP_HDR_LEN;
	case HAL_ENCRYPT_TYPE_CCMP_256:
		return IEEE80211_CCMP_256_HDR_LEN;
	case HAL_ENCRYPT_TYPE_GCMP_128:
	case HAL_ENCRYPT_TYPE_AES_GCMP_256:
		return IEEE80211_GCMP_HDR_LEN;
	case HAL_ENCRYPT_TYPE_WEP_40:
	case HAL_ENCRYPT_TYPE_WEP_104:
	case HAL_ENCRYPT_TYPE_WEP_128:
	case HAL_ENCRYPT_TYPE_WAPI_GCM_SM4:
	case HAL_ENCRYPT_TYPE_WAPI:
		break;
	}

	ath12k_warn(dp_pdev->dp->ab, "unsupported encryption type %d\n", enctype);
	return 0;
}

static int ath12k_dp_rx_crypto_icv_len(struct ath12k_pdev_dp *dp_pdev,
				       enum hal_encrypt_type enctype)
{
	switch (enctype) {
	case HAL_ENCRYPT_TYPE_OPEN:
	case HAL_ENCRYPT_TYPE_CCMP_128:
	case HAL_ENCRYPT_TYPE_CCMP_256:
	case HAL_ENCRYPT_TYPE_GCMP_128:
	case HAL_ENCRYPT_TYPE_AES_GCMP_256:
		return 0;
	case HAL_ENCRYPT_TYPE_TKIP_NO_MIC:
	case HAL_ENCRYPT_TYPE_TKIP_MIC:
		return IEEE80211_TKIP_ICV_LEN;
	case HAL_ENCRYPT_TYPE_WEP_40:
	case HAL_ENCRYPT_TYPE_WEP_104:
	case HAL_ENCRYPT_TYPE_WEP_128:
	case HAL_ENCRYPT_TYPE_WAPI_GCM_SM4:
	case HAL_ENCRYPT_TYPE_WAPI:
		break;
	}

	ath12k_warn(dp_pdev->dp->ab, "unsupported encryption type %d\n", enctype);
	return 0;
}

static void ath12k_dp_rx_h_undecap_nwifi(struct ath12k_pdev_dp *dp_pdev,
					 struct sk_buff *msdu,
					 enum hal_encrypt_type enctype,
					 struct hal_rx_desc_data *rx_info)
{
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	u8 decap_hdr[DP_MAX_NWIFI_HDR_LEN];
	struct ieee80211_hdr *hdr;
	size_t hdr_len;
	u8 *crypto_hdr;
	u16 qos_ctl;

	/* pull decapped header */
	hdr = (struct ieee80211_hdr *)msdu->data;
	hdr_len = ieee80211_hdrlen(hdr->frame_control);
	skb_pull(msdu, hdr_len);

	/*  Rebuild qos header */
	hdr->frame_control |= __cpu_to_le16(IEEE80211_STYPE_QOS_DATA);

	/* Reset the order bit as the HT_Control header is stripped */
	hdr->frame_control &= ~(__cpu_to_le16(IEEE80211_FCTL_ORDER));

	qos_ctl = rxcb->tid;

	if (rx_info->mesh_ctrl_present)
		qos_ctl |= IEEE80211_QOS_CTL_MESH_CONTROL_PRESENT;

	/* TODO: Add other QoS ctl fields when required */

	/* copy decap header before overwriting for reuse below */
	memcpy(decap_hdr, hdr, hdr_len);

	/* Rebuild crypto header for mac80211 use */
	if (!(rx_info->rx_status->flag & RX_FLAG_IV_STRIPPED)) {
		crypto_hdr = skb_push(msdu,
				      ath12k_dp_rx_crypto_param_len(dp_pdev, enctype));
		ath12k_dp_rx_desc_get_crypto_header(dp_pdev->dp->hal,
						    rxcb->rx_desc, crypto_hdr,
						    enctype);
	}

	memcpy(skb_push(msdu,
			IEEE80211_QOS_CTL_LEN), &qos_ctl,
			IEEE80211_QOS_CTL_LEN);
	memcpy(skb_push(msdu, hdr_len), decap_hdr, hdr_len);
}

static void ath12k_dp_rx_h_undecap_raw(struct ath12k_pdev_dp *dp_pdev,
				       struct sk_buff *msdu,
				       enum hal_encrypt_type enctype,
				       struct ieee80211_rx_status *status,
				       bool decrypted)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	struct ieee80211_hdr *hdr;
	size_t hdr_len;
	size_t crypto_len;

	if (!rxcb->is_first_msdu ||
	    !(rxcb->is_first_msdu && rxcb->is_last_msdu)) {
		WARN_ON_ONCE(1);
		return;
	}

	skb_trim(msdu, msdu->len - FCS_LEN);

	if (!decrypted)
		return;

	hdr = (void *)msdu->data;

	/* Tail */
	if (status->flag & RX_FLAG_IV_STRIPPED) {
		skb_trim(msdu, msdu->len -
			 ath12k_dp_rx_crypto_mic_len(dp, enctype));

		skb_trim(msdu, msdu->len -
			 ath12k_dp_rx_crypto_icv_len(dp_pdev, enctype));
	} else {
		/* MIC */
		if (status->flag & RX_FLAG_MIC_STRIPPED)
			skb_trim(msdu, msdu->len -
				 ath12k_dp_rx_crypto_mic_len(dp, enctype));

		/* ICV */
		if (status->flag & RX_FLAG_ICV_STRIPPED)
			skb_trim(msdu, msdu->len -
				 ath12k_dp_rx_crypto_icv_len(dp_pdev, enctype));
	}

	/* MMIC */
	if ((status->flag & RX_FLAG_MMIC_STRIPPED) &&
	    !ieee80211_has_morefrags(hdr->frame_control) &&
	    enctype == HAL_ENCRYPT_TYPE_TKIP_MIC)
		skb_trim(msdu, msdu->len - IEEE80211_CCMP_MIC_LEN);

	/* Head */
	if (status->flag & RX_FLAG_IV_STRIPPED) {
		hdr_len = ieee80211_hdrlen(hdr->frame_control);
		crypto_len = ath12k_dp_rx_crypto_param_len(dp_pdev, enctype);

		memmove(msdu->data + crypto_len, msdu->data, hdr_len);
		skb_pull(msdu, crypto_len);
	}
}

static void ath12k_get_dot11_hdr_from_rx_desc(struct ath12k_pdev_dp *dp_pdev,
					      struct sk_buff *msdu,
					      struct ath12k_skb_rxcb *rxcb,
					      enum hal_encrypt_type enctype,
					      struct hal_rx_desc_data *rx_info)
{
	struct hal_rx_desc *rx_desc = rxcb->rx_desc;
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ath12k_hal *hal = dp->hal;
	size_t hdr_len, crypto_len;
	struct ieee80211_hdr hdr;
	__le16 qos_ctl;
	u8 *crypto_hdr;

	ath12k_dp_rx_desc_get_dot11_hdr(hal, rx_desc, &hdr);
	hdr_len = ieee80211_hdrlen(hdr.frame_control);

	if (!(rx_info->rx_status->flag & RX_FLAG_IV_STRIPPED)) {
		crypto_len = ath12k_dp_rx_crypto_param_len(dp_pdev, enctype);
		crypto_hdr = skb_push(msdu, crypto_len);
		ath12k_dp_rx_desc_get_crypto_header(dp->hal, rx_desc, crypto_hdr,
						    enctype);
	}

	skb_push(msdu, hdr_len);
	memcpy(msdu->data, &hdr, min(hdr_len, sizeof(hdr)));

	if (rxcb->is_mcbc)
		rx_info->rx_status->flag &= ~RX_FLAG_PN_VALIDATED;

	/* Add QOS header */
	if (ieee80211_is_data_qos(hdr.frame_control)) {
		struct ieee80211_hdr *qos_ptr = (struct ieee80211_hdr *)msdu->data;

		qos_ctl = cpu_to_le16(rxcb->tid & IEEE80211_QOS_CTL_TID_MASK);
		if (rx_info->mesh_ctrl_present)
			qos_ctl |= cpu_to_le16(IEEE80211_QOS_CTL_MESH_CONTROL_PRESENT);

		memcpy(ieee80211_get_qos_ctl(qos_ptr), &qos_ctl, IEEE80211_QOS_CTL_LEN);
	}
}

static void ath12k_dp_rx_h_undecap_eth(struct ath12k_pdev_dp *dp_pdev,
				       struct sk_buff *msdu,
				       enum hal_encrypt_type enctype,
				       struct hal_rx_desc_data *rx_info)
{
	struct ieee80211_hdr *hdr;
	struct ethhdr *eth;
	u8 da[ETH_ALEN];
	u8 sa[ETH_ALEN];
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	struct ath12k_dp_rx_rfc1042_hdr rfc = {0xaa, 0xaa, 0x03, {0x00, 0x00, 0x00}};

	eth = (struct ethhdr *)msdu->data;
	ether_addr_copy(da, eth->h_dest);
	ether_addr_copy(sa, eth->h_source);
	rfc.snap_type = eth->h_proto;
	skb_pull(msdu, sizeof(*eth));
	memcpy(skb_push(msdu, sizeof(rfc)), &rfc,
	       sizeof(rfc));
	ath12k_get_dot11_hdr_from_rx_desc(dp_pdev, msdu, rxcb, enctype, rx_info);

	/* original 802.11 header has a different DA and in
	 * case of 4addr it may also have different SA
	 */
	hdr = (struct ieee80211_hdr *)msdu->data;
	ether_addr_copy(ieee80211_get_DA(hdr), da);
	ether_addr_copy(ieee80211_get_SA(hdr), sa);
}

void ath12k_dp_rx_h_undecap(struct ath12k_pdev_dp *dp_pdev, struct sk_buff *msdu,
			    struct hal_rx_desc *rx_desc,
			    enum hal_encrypt_type enctype,
			    bool decrypted,
			    struct hal_rx_desc_data *rx_info)
{
	struct ethhdr *ehdr;

	switch (rx_info->decap_type) {
	case DP_RX_DECAP_TYPE_NATIVE_WIFI:
		ath12k_dp_rx_h_undecap_nwifi(dp_pdev, msdu, enctype, rx_info);
		break;
	case DP_RX_DECAP_TYPE_RAW:
		ath12k_dp_rx_h_undecap_raw(dp_pdev, msdu, enctype, rx_info->rx_status,
					   decrypted);
		break;
	case DP_RX_DECAP_TYPE_ETHERNET2_DIX:
		ehdr = (struct ethhdr *)msdu->data;

		/* mac80211 allows fast path only for authorized STA */
		if (ehdr->h_proto == cpu_to_be16(ETH_P_PAE)) {
			ATH12K_SKB_RXCB(msdu)->is_eapol = true;
			ath12k_dp_rx_h_undecap_eth(dp_pdev, msdu, enctype, rx_info);
			break;
		}

		/* PN for mcast packets will be validated in mac80211;
		 * remove eth header and add 802.11 header.
		 */
		if (ATH12K_SKB_RXCB(msdu)->is_mcbc && decrypted)
			ath12k_dp_rx_h_undecap_eth(dp_pdev, msdu, enctype, rx_info);
		break;
	case DP_RX_DECAP_TYPE_8023:
		/* TODO: Handle undecap for these formats */
		break;
	}
}
EXPORT_SYMBOL(ath12k_dp_rx_h_undecap);

struct ath12k_dp_link_peer *
ath12k_dp_rx_h_find_link_peer(struct ath12k_pdev_dp *dp_pdev, struct sk_buff *msdu,
			      struct hal_rx_desc_data *rx_info)
{
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	struct ath12k_dp_link_peer *peer = NULL;
	struct ath12k_dp *dp = dp_pdev->dp;

	lockdep_assert_held(&dp->dp_lock);

	if (rxcb->peer_id)
		peer = ath12k_dp_link_peer_find_by_peerid(dp_pdev, rxcb->peer_id);

	if (peer)
		return peer;

	if (rx_info->addr2_present)
		peer = ath12k_dp_link_peer_find_by_addr(dp, rx_info->addr2);

	return peer;
}

static void ath12k_dp_rx_h_rate(struct ath12k_pdev_dp *dp_pdev,
				struct hal_rx_desc_data *rx_info)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ieee80211_supported_band *sband;
	struct ieee80211_rx_status *rx_status = rx_info->rx_status;
	enum rx_msdu_start_pkt_type pkt_type = rx_info->pkt_type;
	u8 bw = rx_info->bw, sgi = rx_info->sgi;
	u8 rate_mcs = rx_info->rate_mcs, nss = rx_info->nss;
	bool is_cck;
	struct ath12k *ar;

	switch (pkt_type) {
	case RX_MSDU_START_PKT_TYPE_11A:
	case RX_MSDU_START_PKT_TYPE_11B:
		ar = ath12k_pdev_dp_to_ar(dp_pdev);
		is_cck = (pkt_type == RX_MSDU_START_PKT_TYPE_11B);
		sband = &ar->mac.sbands[rx_status->band];
		rx_status->rate_idx = ath12k_mac_hw_rate_to_idx(sband, rate_mcs,
								is_cck);
		break;
	case RX_MSDU_START_PKT_TYPE_11N:
		rx_status->encoding = RX_ENC_HT;
		if (rate_mcs > ATH12K_HT_MCS_MAX) {
			ath12k_warn(dp->ab,
				    "Received with invalid mcs in HT mode %d\n",
				     rate_mcs);
			break;
		}
		rx_status->rate_idx = rate_mcs + (8 * (nss - 1));
		if (sgi)
			rx_status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		rx_status->bw = ath12k_mac_bw_to_mac80211_bw(bw);
		break;
	case RX_MSDU_START_PKT_TYPE_11AC:
		rx_status->encoding = RX_ENC_VHT;
		rx_status->rate_idx = rate_mcs;
		if (rate_mcs > ATH12K_VHT_MCS_MAX) {
			ath12k_warn(dp->ab,
				    "Received with invalid mcs in VHT mode %d\n",
				     rate_mcs);
			break;
		}
		rx_status->nss = nss;
		if (sgi)
			rx_status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		rx_status->bw = ath12k_mac_bw_to_mac80211_bw(bw);
		break;
	case RX_MSDU_START_PKT_TYPE_11AX:
		rx_status->rate_idx = rate_mcs;
		if (rate_mcs > ATH12K_HE_MCS_MAX) {
			ath12k_warn(dp->ab,
				    "Received with invalid mcs in HE mode %d\n",
				    rate_mcs);
			break;
		}
		rx_status->encoding = RX_ENC_HE;
		rx_status->nss = nss;
		rx_status->he_gi = ath12k_he_gi_to_nl80211_he_gi(sgi);
		rx_status->bw = ath12k_mac_bw_to_mac80211_bw(bw);
		break;
	case RX_MSDU_START_PKT_TYPE_11BE:
		rx_status->rate_idx = rate_mcs;

		if (rate_mcs > ATH12K_EHT_MCS_MAX) {
			ath12k_warn(dp->ab,
				    "Received with invalid mcs in EHT mode %d\n",
				    rate_mcs);
			break;
		}

		rx_status->encoding = RX_ENC_EHT;
		rx_status->nss = nss;
		rx_status->eht.gi = ath12k_mac_eht_gi_to_nl80211_eht_gi(sgi);
		rx_status->bw = ath12k_mac_bw_to_mac80211_bw(bw);
		break;
	default:
		break;
	}
}

void ath12k_dp_rx_h_ppdu(struct ath12k_pdev_dp *dp_pdev,
			 struct hal_rx_desc_data *rx_info)
{
	struct ieee80211_rx_status *rx_status = rx_info->rx_status;
	u8 channel_num;
	u32 center_freq, meta_data;
	struct ieee80211_channel *channel;

	rx_status->freq = 0;
	rx_status->rate_idx = 0;
	rx_status->nss = 0;
	rx_status->encoding = RX_ENC_LEGACY;
	rx_status->bw = RATE_INFO_BW_20;
	rx_status->enc_flags = 0;

	rx_status->flag |= RX_FLAG_NO_SIGNAL_VAL;

	meta_data = rx_info->phy_meta_data;
	channel_num = meta_data;
	center_freq = meta_data >> 16;

	rx_status->band = NUM_NL80211_BANDS;

	if (center_freq >= ATH12K_MIN_6GHZ_FREQ &&
	    center_freq <= ATH12K_MAX_6GHZ_FREQ) {
		rx_status->band = NL80211_BAND_6GHZ;
		rx_status->freq = center_freq;
	} else if (channel_num >= 1 && channel_num <= 14) {
		rx_status->band = NL80211_BAND_2GHZ;
	} else if (channel_num >= 36 && channel_num <= 173) {
		rx_status->band = NL80211_BAND_5GHZ;
	}

	if (unlikely(rx_status->band == NUM_NL80211_BANDS ||
		     !ath12k_pdev_dp_to_hw(dp_pdev)->wiphy->bands[rx_status->band])) {
		struct ath12k *ar = ath12k_pdev_dp_to_ar(dp_pdev);

		ath12k_warn(ar->ab, "sband is NULL for status band %d channel_num %d center_freq %d pdev_id %d\n",
			    rx_status->band, channel_num, center_freq, ar->pdev_idx);

		spin_lock_bh(&ar->data_lock);
		channel = ar->rx_channel;
		if (channel) {
			rx_status->band = channel->band;
			channel_num =
				ieee80211_frequency_to_channel(channel->center_freq);
			rx_status->freq = ieee80211_channel_to_frequency(channel_num,
									 rx_status->band);
		} else {
			ath12k_err(ar->ab, "unable to determine channel, band for rx packet");
		}
		spin_unlock_bh(&ar->data_lock);
		goto h_rate;
	}

	if (rx_status->band != NL80211_BAND_6GHZ)
		rx_status->freq = ieee80211_channel_to_frequency(channel_num,
								 rx_status->band);

h_rate:
	ath12k_dp_rx_h_rate(dp_pdev, rx_info);
}
EXPORT_SYMBOL(ath12k_dp_rx_h_ppdu);

void ath12k_dp_rx_deliver_msdu(struct ath12k_pdev_dp *dp_pdev, struct napi_struct *napi,
			       struct sk_buff *msdu,
			       struct hal_rx_desc_data *rx_info)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ieee80211_rx_status *rx_status;
	struct ieee80211_sta *pubsta;
	struct ath12k_dp_peer *peer;
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	struct ieee80211_rx_status *status = rx_info->rx_status;
	u8 decap = rx_info->decap_type;
	bool is_mcbc = rxcb->is_mcbc;
	bool is_eapol = rxcb->is_eapol;

	peer = ath12k_dp_peer_find_by_peerid(dp_pdev, rx_info->peer_id);

	pubsta = peer ? peer->sta : NULL;

	if (pubsta && pubsta->valid_links) {
		status->link_valid = 1;
		status->link_id = peer->hw_links[rxcb->hw_link_id];
	}

	ath12k_dbg(dp->ab, ATH12K_DBG_DATA,
		   "rx skb %p len %u peer %pM %d %s sn %u %s%s%s%s%s%s%s%s%s%s rate_idx %u vht_nss %u freq %u band %u flag 0x%x fcs-err %i mic-err %i amsdu-more %i\n",
		   msdu,
		   msdu->len,
		   peer ? peer->addr : NULL,
		   rxcb->tid,
		   is_mcbc ? "mcast" : "ucast",
		   rx_info->seq_no,
		   (status->encoding == RX_ENC_LEGACY) ? "legacy" : "",
		   (status->encoding == RX_ENC_HT) ? "ht" : "",
		   (status->encoding == RX_ENC_VHT) ? "vht" : "",
		   (status->encoding == RX_ENC_HE) ? "he" : "",
		   (status->encoding == RX_ENC_EHT) ? "eht" : "",
		   (status->bw == RATE_INFO_BW_40) ? "40" : "",
		   (status->bw == RATE_INFO_BW_80) ? "80" : "",
		   (status->bw == RATE_INFO_BW_160) ? "160" : "",
		   (status->bw == RATE_INFO_BW_320) ? "320" : "",
		   status->enc_flags & RX_ENC_FLAG_SHORT_GI ? "sgi " : "",
		   status->rate_idx,
		   status->nss,
		   status->freq,
		   status->band, status->flag,
		   !!(status->flag & RX_FLAG_FAILED_FCS_CRC),
		   !!(status->flag & RX_FLAG_MMIC_ERROR),
		   !!(status->flag & RX_FLAG_AMSDU_MORE));

	ath12k_dbg_dump(dp->ab, ATH12K_DBG_DP_RX, NULL, "dp rx msdu: ",
			msdu->data, msdu->len);

	rx_status = IEEE80211_SKB_RXCB(msdu);
	*rx_status = *status;

	/* TODO: trace rx packet */

	/* PN for multicast packets are not validate in HW,
	 * so skip 802.3 rx path
	 * Also, fast_rx expects the STA to be authorized, hence
	 * eapol packets are sent in slow path.
	 */
	if (decap == DP_RX_DECAP_TYPE_ETHERNET2_DIX && !is_eapol &&
	    !(is_mcbc && rx_status->flag & RX_FLAG_DECRYPTED))
		rx_status->flag |= RX_FLAG_8023;

	ieee80211_rx_napi(ath12k_pdev_dp_to_hw(dp_pdev), pubsta, msdu, napi);
}
EXPORT_SYMBOL(ath12k_dp_rx_deliver_msdu);

bool ath12k_dp_rx_check_nwifi_hdr_len_valid(struct ath12k_dp *dp,
					    struct hal_rx_desc *rx_desc,
					    struct sk_buff *msdu,
					    struct hal_rx_desc_data *rx_info)
{
	struct ieee80211_hdr *hdr;
	u32 hdr_len;

	if (rx_info->decap_type != DP_RX_DECAP_TYPE_NATIVE_WIFI)
		return true;

	hdr = (struct ieee80211_hdr *)msdu->data;
	hdr_len = ieee80211_hdrlen(hdr->frame_control);

	if ((likely(hdr_len <= DP_MAX_NWIFI_HDR_LEN)))
		return true;

	dp->device_stats.invalid_rbm++;
	WARN_ON_ONCE(1);
	return false;
}
EXPORT_SYMBOL(ath12k_dp_rx_check_nwifi_hdr_len_valid);

static void ath12k_dp_rx_frag_timer(struct timer_list *timer)
{
	struct ath12k_dp_rx_tid *rx_tid = timer_container_of(rx_tid, timer,
							     frag_timer);

	spin_lock_bh(&rx_tid->dp->dp_lock);
	if (rx_tid->last_frag_no &&
	    rx_tid->rx_frag_bitmap == GENMASK(rx_tid->last_frag_no, 0)) {
		spin_unlock_bh(&rx_tid->dp->dp_lock);
		return;
	}
	ath12k_dp_arch_rx_frags_cleanup(rx_tid->dp, rx_tid, true);
	spin_unlock_bh(&rx_tid->dp->dp_lock);
}

int ath12k_dp_rx_peer_frag_setup(struct ath12k *ar, const u8 *peer_mac, int vdev_id)
{
	struct ath12k_base *ab = ar->ab;
	struct crypto_shash *tfm;
	struct ath12k_dp_link_peer *peer;
	struct ath12k_dp_rx_tid *rx_tid;
	int i;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);

	tfm = crypto_alloc_shash("michael_mic", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	spin_lock_bh(&dp->dp_lock);

	peer = ath12k_dp_link_peer_find_by_vdev_and_addr(dp, vdev_id, peer_mac);
	if (!peer || !peer->dp_peer) {
		spin_unlock_bh(&dp->dp_lock);
		crypto_free_shash(tfm);
		ath12k_warn(ab, "failed to find the peer to set up fragment info\n");
		return -ENOENT;
	}

	if (!peer->primary_link) {
		spin_unlock_bh(&dp->dp_lock);
		crypto_free_shash(tfm);
		return 0;
	}

	for (i = 0; i <= IEEE80211_NUM_TIDS; i++) {
		rx_tid = &peer->dp_peer->rx_tid[i];
		rx_tid->dp = dp;
		timer_setup(&rx_tid->frag_timer, ath12k_dp_rx_frag_timer, 0);
		skb_queue_head_init(&rx_tid->rx_frags);
	}

	peer->dp_peer->tfm_mmic = tfm;
	peer->dp_peer->dp_setup_done = true;
	spin_unlock_bh(&dp->dp_lock);

	return 0;
}

int ath12k_dp_rx_h_michael_mic(struct crypto_shash *tfm, u8 *key,
			       struct ieee80211_hdr *hdr, u8 *data,
			       size_t data_len, u8 *mic)
{
	SHASH_DESC_ON_STACK(desc, tfm);
	u8 mic_hdr[16] = {};
	u8 tid = 0;
	int ret;

	if (!tfm)
		return -EINVAL;

	desc->tfm = tfm;

	ret = crypto_shash_setkey(tfm, key, 8);
	if (ret)
		goto out;

	ret = crypto_shash_init(desc);
	if (ret)
		goto out;

	/* TKIP MIC header */
	memcpy(mic_hdr, ieee80211_get_DA(hdr), ETH_ALEN);
	memcpy(mic_hdr + ETH_ALEN, ieee80211_get_SA(hdr), ETH_ALEN);
	if (ieee80211_is_data_qos(hdr->frame_control))
		tid = ieee80211_get_tid(hdr);
	mic_hdr[12] = tid;

	ret = crypto_shash_update(desc, mic_hdr, 16);
	if (ret)
		goto out;
	ret = crypto_shash_update(desc, data, data_len);
	if (ret)
		goto out;
	ret = crypto_shash_final(desc, mic);
out:
	shash_desc_zero(desc);
	return ret;
}
EXPORT_SYMBOL(ath12k_dp_rx_h_michael_mic);

void ath12k_dp_rx_h_undecap_frag(struct ath12k_pdev_dp *dp_pdev, struct sk_buff *msdu,
				 enum hal_encrypt_type enctype, u32 flags)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ieee80211_hdr *hdr;
	size_t hdr_len;
	size_t crypto_len;
	u32 hal_rx_desc_sz = dp->ab->hal.hal_desc_sz;

	if (!flags)
		return;

	hdr = (struct ieee80211_hdr *)(msdu->data + hal_rx_desc_sz);

	if (flags & RX_FLAG_MIC_STRIPPED)
		skb_trim(msdu, msdu->len -
			 ath12k_dp_rx_crypto_mic_len(dp, enctype));

	if (flags & RX_FLAG_ICV_STRIPPED)
		skb_trim(msdu, msdu->len -
			 ath12k_dp_rx_crypto_icv_len(dp_pdev, enctype));

	if (flags & RX_FLAG_IV_STRIPPED) {
		hdr_len = ieee80211_hdrlen(hdr->frame_control);
		crypto_len = ath12k_dp_rx_crypto_param_len(dp_pdev, enctype);

		memmove(msdu->data + hal_rx_desc_sz + crypto_len,
			msdu->data + hal_rx_desc_sz, hdr_len);
		skb_pull(msdu, crypto_len);
	}
}
EXPORT_SYMBOL(ath12k_dp_rx_h_undecap_frag);

static int ath12k_dp_rx_h_cmp_frags(struct ath12k_hal *hal,
				    struct sk_buff *a, struct sk_buff *b)
{
	int frag1, frag2;

	frag1 = ath12k_dp_rx_h_frag_no(hal, a);
	frag2 = ath12k_dp_rx_h_frag_no(hal, b);

	return frag1 - frag2;
}

void ath12k_dp_rx_h_sort_frags(struct ath12k_hal *hal,
			       struct sk_buff_head *frag_list,
			       struct sk_buff *cur_frag)
{
	struct sk_buff *skb;
	int cmp;

	skb_queue_walk(frag_list, skb) {
		cmp = ath12k_dp_rx_h_cmp_frags(hal, skb, cur_frag);
		if (cmp < 0)
			continue;
		__skb_queue_before(frag_list, skb, cur_frag);
		return;
	}
	__skb_queue_tail(frag_list, cur_frag);
}
EXPORT_SYMBOL(ath12k_dp_rx_h_sort_frags);

u64 ath12k_dp_rx_h_get_pn(struct ath12k_dp *dp, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;
	u64 pn = 0;
	u8 *ehdr;
	u32 hal_rx_desc_sz = dp->ab->hal.hal_desc_sz;

	hdr = (struct ieee80211_hdr *)(skb->data + hal_rx_desc_sz);
	ehdr = skb->data + hal_rx_desc_sz + ieee80211_hdrlen(hdr->frame_control);

	pn = ehdr[0];
	pn |= (u64)ehdr[1] << 8;
	pn |= (u64)ehdr[4] << 16;
	pn |= (u64)ehdr[5] << 24;
	pn |= (u64)ehdr[6] << 32;
	pn |= (u64)ehdr[7] << 40;

	return pn;
}
EXPORT_SYMBOL(ath12k_dp_rx_h_get_pn);

void ath12k_dp_rx_free(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct dp_srng *srng;
	int i;

	ath12k_dp_srng_cleanup(ab, &dp->rx_refill_buf_ring.refill_buf_ring);

	for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++) {
		if (ab->hw_params->rx_mac_buf_ring)
			ath12k_dp_srng_cleanup(ab, &dp->rx_mac_buf_ring[i]);
		if (!ab->hw_params->rxdma1_enable) {
			srng = &dp->rx_mon_status_refill_ring[i].refill_buf_ring;
			ath12k_dp_srng_cleanup(ab, srng);
		}
	}

	for (i = 0; i < ab->hw_params->num_rxdma_dst_ring; i++)
		ath12k_dp_srng_cleanup(ab, &dp->rxdma_err_dst_ring[i]);

	ath12k_dp_srng_cleanup(ab, &dp->rxdma_mon_buf_ring.refill_buf_ring);

	ath12k_dp_rxdma_buf_free(ab);
}

void ath12k_dp_rx_pdev_free(struct ath12k_base *ab, int mac_id)
{
	struct ath12k *ar = ab->pdevs[mac_id].ar;

	ath12k_dp_rx_pdev_srng_free(ar);
}

int ath12k_dp_rx_htt_setup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	u32 ring_id;
	int i, ret;

	/* TODO: Need to verify the HTT setup for QCN9224 */
	ring_id = dp->rx_refill_buf_ring.refill_buf_ring.ring_id;
	ret = ath12k_dp_tx_htt_srng_setup(ab, ring_id, 0, HAL_RXDMA_BUF);
	if (ret) {
		ath12k_warn(ab, "failed to configure rx_refill_buf_ring %d\n",
			    ret);
		return ret;
	}

	if (ab->hw_params->rx_mac_buf_ring) {
		for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++) {
			ring_id = dp->rx_mac_buf_ring[i].ring_id;
			ret = ath12k_dp_tx_htt_srng_setup(ab, ring_id,
							  i, HAL_RXDMA_BUF);
			if (ret) {
				ath12k_warn(ab, "failed to configure rx_mac_buf_ring%d %d\n",
					    i, ret);
				return ret;
			}
		}
	}

	for (i = 0; i < ab->hw_params->num_rxdma_dst_ring; i++) {
		ring_id = dp->rxdma_err_dst_ring[i].ring_id;
		ret = ath12k_dp_tx_htt_srng_setup(ab, ring_id,
						  i, HAL_RXDMA_DST);
		if (ret) {
			ath12k_warn(ab, "failed to configure rxdma_err_dest_ring%d %d\n",
				    i, ret);
			return ret;
		}
	}

	if (ab->hw_params->rxdma1_enable) {
		ring_id = dp->rxdma_mon_buf_ring.refill_buf_ring.ring_id;
		ret = ath12k_dp_tx_htt_srng_setup(ab, ring_id,
						  0, HAL_RXDMA_MONITOR_BUF);
		if (ret) {
			ath12k_warn(ab, "failed to configure rxdma_mon_buf_ring %d\n",
				    ret);
			return ret;
		}
	} else {
		for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++) {
			ring_id =
				dp->rx_mon_status_refill_ring[i].refill_buf_ring.ring_id;
			ret = ath12k_dp_tx_htt_srng_setup(ab, ring_id, i,
							  HAL_RXDMA_MONITOR_STATUS);
			if (ret) {
				ath12k_warn(ab,
					    "failed to configure mon_status_refill_ring%d %d\n",
					    i, ret);
				return ret;
			}
		}
	}

	ret = ab->hw_params->hw_ops->rxdma_ring_sel_config(ab);
	if (ret) {
		ath12k_warn(ab, "failed to setup rxdma ring selection config\n");
		return ret;
	}

	return 0;
}

int ath12k_dp_rx_alloc(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct dp_srng *srng;
	int i, ret;

	idr_init(&dp->rxdma_mon_buf_ring.bufs_idr);
	spin_lock_init(&dp->rxdma_mon_buf_ring.idr_lock);

	ret = ath12k_dp_srng_setup(ab,
				   &dp->rx_refill_buf_ring.refill_buf_ring,
				   HAL_RXDMA_BUF, 0, 0,
				   DP_RXDMA_BUF_RING_SIZE);
	if (ret) {
		ath12k_warn(ab, "failed to setup rx_refill_buf_ring\n");
		return ret;
	}

	if (ab->hw_params->rx_mac_buf_ring) {
		for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++) {
			ret = ath12k_dp_srng_setup(ab,
						   &dp->rx_mac_buf_ring[i],
						   HAL_RXDMA_BUF, 1,
						   i, DP_RX_MAC_BUF_RING_SIZE);
			if (ret) {
				ath12k_warn(ab, "failed to setup rx_mac_buf_ring %d\n",
					    i);
				return ret;
			}
		}
	}

	for (i = 0; i < ab->hw_params->num_rxdma_dst_ring; i++) {
		ret = ath12k_dp_srng_setup(ab, &dp->rxdma_err_dst_ring[i],
					   HAL_RXDMA_DST, 0, i,
					   DP_RXDMA_ERR_DST_RING_SIZE);
		if (ret) {
			ath12k_warn(ab, "failed to setup rxdma_err_dst_ring %d\n", i);
			return ret;
		}
	}

	if (ab->hw_params->rxdma1_enable) {
		ret = ath12k_dp_srng_setup(ab,
					   &dp->rxdma_mon_buf_ring.refill_buf_ring,
					   HAL_RXDMA_MONITOR_BUF, 0, 0,
					   DP_RXDMA_MONITOR_BUF_RING_SIZE(ab));
		if (ret) {
			ath12k_warn(ab, "failed to setup HAL_RXDMA_MONITOR_BUF\n");
			return ret;
		}
	} else {
		for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++) {
			idr_init(&dp->rx_mon_status_refill_ring[i].bufs_idr);
			spin_lock_init(&dp->rx_mon_status_refill_ring[i].idr_lock);
		}

		for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++) {
			srng = &dp->rx_mon_status_refill_ring[i].refill_buf_ring;
			ret = ath12k_dp_srng_setup(ab, srng,
						   HAL_RXDMA_MONITOR_STATUS, 0, i,
						   DP_RXDMA_MON_STATUS_RING_SIZE);
			if (ret) {
				ath12k_warn(ab, "failed to setup mon status ring %d\n",
					    i);
				return ret;
			}
		}
	}

	ret = ath12k_dp_rxdma_buf_setup(ab);
	if (ret) {
		ath12k_warn(ab, "failed to setup rxdma ring\n");
		return ret;
	}

	return 0;
}

int ath12k_dp_rx_pdev_alloc(struct ath12k_base *ab, int mac_id)
{
	struct ath12k *ar = ab->pdevs[mac_id].ar;
	struct ath12k_pdev_dp *dp = &ar->dp;
	u32 ring_id;
	int i;
	int ret;

	if (!ab->hw_params->rxdma1_enable)
		goto out;

	ret = ath12k_dp_rx_pdev_srng_alloc(ar);
	if (ret) {
		ath12k_warn(ab, "failed to setup rx srngs\n");
		return ret;
	}

	for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++) {
		ring_id = dp->rxdma_mon_dst_ring[i].ring_id;
		ret = ath12k_dp_tx_htt_srng_setup(ab, ring_id,
						  mac_id + i,
						  HAL_RXDMA_MONITOR_DST);
		if (ret) {
			ath12k_warn(ab,
				    "failed to configure rxdma_mon_dst_ring %d %d\n",
				    i, ret);
			return ret;
		}
	}
out:
	return 0;
}

static int ath12k_dp_rx_pdev_mon_status_attach(struct ath12k *ar)
{
	struct ath12k_pdev_dp *dp = &ar->dp;
	struct ath12k_mon_data *pmon = (struct ath12k_mon_data *)&dp->mon_data;

	skb_queue_head_init(&pmon->rx_status_q);

	pmon->mon_ppdu_status = DP_PPDU_STATUS_START;

	memset(&pmon->rx_mon_stats, 0,
	       sizeof(pmon->rx_mon_stats));
	return 0;
}

int ath12k_dp_rx_pdev_mon_attach(struct ath12k *ar)
{
	struct ath12k_pdev_dp *dp = &ar->dp;
	struct ath12k_mon_data *pmon = &dp->mon_data;
	int ret = 0;

	ret = ath12k_dp_rx_pdev_mon_status_attach(ar);
	if (ret) {
		ath12k_warn(ar->ab, "pdev_mon_status_attach() failed");
		return ret;
	}

	pmon->mon_last_linkdesc_paddr = 0;
	pmon->mon_last_buf_cookie = DP_RX_DESC_COOKIE_MAX + 1;
	spin_lock_init(&pmon->mon_lock);

	if (!ar->ab->hw_params->rxdma1_enable)
		return 0;

	INIT_LIST_HEAD(&pmon->dp_rx_mon_mpdu_list);
	pmon->mon_mpdu = NULL;

	return 0;
}
