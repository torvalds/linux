// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/ieee80211.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <crypto/hash.h>
#include "core.h"
#include "debug.h"
#include "hal_desc.h"
#include "hw.h"
#include "dp_rx.h"
#include "hal_rx.h"
#include "dp_tx.h"
#include "peer.h"
#include "dp_mon.h"
#include "debugfs_htt_stats.h"

#define ATH12K_DP_RX_FRAGMENT_TIMEOUT_MS (2 * HZ)

static enum hal_encrypt_type ath12k_dp_rx_h_enctype(struct ath12k_base *ab,
						    struct hal_rx_desc *desc)
{
	if (!ab->hal_rx_ops->rx_desc_encrypt_valid(desc))
		return HAL_ENCRYPT_TYPE_OPEN;

	return ab->hal_rx_ops->rx_desc_get_encrypt_type(desc);
}

u8 ath12k_dp_rx_h_decap_type(struct ath12k_base *ab,
			     struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_decap_type(desc);
}

static u8 ath12k_dp_rx_h_mesh_ctl_present(struct ath12k_base *ab,
					  struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_mesh_ctl(desc);
}

static bool ath12k_dp_rx_h_seq_ctrl_valid(struct ath12k_base *ab,
					  struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_mpdu_seq_ctl_vld(desc);
}

static bool ath12k_dp_rx_h_fc_valid(struct ath12k_base *ab,
				    struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_mpdu_fc_valid(desc);
}

static bool ath12k_dp_rx_h_more_frags(struct ath12k_base *ab,
				      struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;

	hdr = (struct ieee80211_hdr *)(skb->data + ab->hal.hal_desc_sz);
	return ieee80211_has_morefrags(hdr->frame_control);
}

static u16 ath12k_dp_rx_h_frag_no(struct ath12k_base *ab,
				  struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;

	hdr = (struct ieee80211_hdr *)(skb->data + ab->hal.hal_desc_sz);
	return le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_FRAG;
}

static u16 ath12k_dp_rx_h_seq_no(struct ath12k_base *ab,
				 struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_mpdu_start_seq_no(desc);
}

static bool ath12k_dp_rx_h_msdu_done(struct ath12k_base *ab,
				     struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->dp_rx_h_msdu_done(desc);
}

static bool ath12k_dp_rx_h_l4_cksum_fail(struct ath12k_base *ab,
					 struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->dp_rx_h_l4_cksum_fail(desc);
}

static bool ath12k_dp_rx_h_ip_cksum_fail(struct ath12k_base *ab,
					 struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->dp_rx_h_ip_cksum_fail(desc);
}

static bool ath12k_dp_rx_h_is_decrypted(struct ath12k_base *ab,
					struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->dp_rx_h_is_decrypted(desc);
}

u32 ath12k_dp_rx_h_mpdu_err(struct ath12k_base *ab,
			    struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->dp_rx_h_mpdu_err(desc);
}

static u16 ath12k_dp_rx_h_msdu_len(struct ath12k_base *ab,
				   struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_msdu_len(desc);
}

static u8 ath12k_dp_rx_h_sgi(struct ath12k_base *ab,
			     struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_msdu_sgi(desc);
}

static u8 ath12k_dp_rx_h_rate_mcs(struct ath12k_base *ab,
				  struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_msdu_rate_mcs(desc);
}

static u8 ath12k_dp_rx_h_rx_bw(struct ath12k_base *ab,
			       struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_msdu_rx_bw(desc);
}

static u32 ath12k_dp_rx_h_freq(struct ath12k_base *ab,
			       struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_msdu_freq(desc);
}

static u8 ath12k_dp_rx_h_pkt_type(struct ath12k_base *ab,
				  struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_msdu_pkt_type(desc);
}

static u8 ath12k_dp_rx_h_nss(struct ath12k_base *ab,
			     struct hal_rx_desc *desc)
{
	return hweight8(ab->hal_rx_ops->rx_desc_get_msdu_nss(desc));
}

static u8 ath12k_dp_rx_h_tid(struct ath12k_base *ab,
			     struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_mpdu_tid(desc);
}

static u16 ath12k_dp_rx_h_peer_id(struct ath12k_base *ab,
				  struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_mpdu_peer_id(desc);
}

u8 ath12k_dp_rx_h_l3pad(struct ath12k_base *ab,
			struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_l3_pad_bytes(desc);
}

static bool ath12k_dp_rx_h_first_msdu(struct ath12k_base *ab,
				      struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_first_msdu(desc);
}

static bool ath12k_dp_rx_h_last_msdu(struct ath12k_base *ab,
				     struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_last_msdu(desc);
}

static void ath12k_dp_rx_desc_end_tlv_copy(struct ath12k_base *ab,
					   struct hal_rx_desc *fdesc,
					   struct hal_rx_desc *ldesc)
{
	ab->hal_rx_ops->rx_desc_copy_end_tlv(fdesc, ldesc);
}

static void ath12k_dp_rxdesc_set_msdu_len(struct ath12k_base *ab,
					  struct hal_rx_desc *desc,
					  u16 len)
{
	ab->hal_rx_ops->rx_desc_set_msdu_len(desc, len);
}

u32 ath12k_dp_rxdesc_get_ppduid(struct ath12k_base *ab,
				struct hal_rx_desc *rx_desc)
{
	return ab->hal_rx_ops->rx_desc_get_mpdu_ppdu_id(rx_desc);
}

bool ath12k_dp_rxdesc_mpdu_valid(struct ath12k_base *ab,
				 struct hal_rx_desc *rx_desc)
{
	u32 tlv_tag;

	tlv_tag = ab->hal_rx_ops->rx_desc_get_mpdu_start_tag(rx_desc);

	return tlv_tag == HAL_RX_MPDU_START;
}

static bool ath12k_dp_rx_h_is_da_mcbc(struct ath12k_base *ab,
				      struct hal_rx_desc *desc)
{
	return (ath12k_dp_rx_h_first_msdu(ab, desc) &&
		ab->hal_rx_ops->rx_desc_is_da_mcbc(desc));
}

static bool ath12k_dp_rxdesc_mac_addr2_valid(struct ath12k_base *ab,
					     struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_mac_addr2_valid(desc);
}

static u8 *ath12k_dp_rxdesc_get_mpdu_start_addr2(struct ath12k_base *ab,
						 struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_mpdu_start_addr2(desc);
}

static void ath12k_dp_rx_desc_get_dot11_hdr(struct ath12k_base *ab,
					    struct hal_rx_desc *desc,
					    struct ieee80211_hdr *hdr)
{
	ab->hal_rx_ops->rx_desc_get_dot11_hdr(desc, hdr);
}

static void ath12k_dp_rx_desc_get_crypto_header(struct ath12k_base *ab,
						struct hal_rx_desc *desc,
						u8 *crypto_hdr,
						enum hal_encrypt_type enctype)
{
	ab->hal_rx_ops->rx_desc_get_crypto_header(desc, crypto_hdr, enctype);
}

static inline u8 ath12k_dp_rx_get_msdu_src_link(struct ath12k_base *ab,
						struct hal_rx_desc *desc)
{
	return ab->hal_rx_ops->rx_desc_get_msdu_src_link_id(desc);
}

static void ath12k_dp_clean_up_skb_list(struct sk_buff_head *skb_list)
{
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(skb_list)))
		dev_kfree_skb_any(skb);
}

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
int ath12k_dp_rx_bufs_replenish(struct ath12k_base *ab,
				struct dp_rxdma_ring *rx_ring,
				struct list_head *used_list,
				int req_entries)
{
	struct ath12k_buffer_addr *desc;
	struct hal_srng *srng;
	struct sk_buff *skb;
	int num_free;
	int num_remain;
	u32 cookie;
	dma_addr_t paddr;
	struct ath12k_dp *dp = &ab->dp;
	struct ath12k_rx_desc_info *rx_desc;
	enum hal_rx_buf_return_buf_manager mgr = ab->hw_params->hal_params->rx_buf_rbm;

	req_entries = min(req_entries, rx_ring->bufs_max);

	srng = &ab->hal.srng_list[rx_ring->refill_buf_ring.ring_id];

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

		paddr = dma_map_single(ab->dev, skb->data,
				       skb->len + skb_tailroom(skb),
				       DMA_FROM_DEVICE);
		if (dma_mapping_error(ab->dev, paddr))
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

		ath12k_hal_rx_buf_addr_info_set(desc, paddr, cookie, mgr);
	}

	goto out;

fail_dma_unmap:
	dma_unmap_single(ab->dev, paddr, skb->len + skb_tailroom(skb),
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
	struct ath12k_dp *dp = &ab->dp;
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

	ath12k_dp_rx_bufs_replenish(ab, rx_ring, &list, 0);

	return 0;
}

static int ath12k_dp_rxdma_buf_setup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = &ab->dp;
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
	struct ath12k_dp *dp = &ab->dp;
	int i;

	for (i = 0; i < DP_REO_DST_RING_MAX; i++)
		ath12k_dp_srng_cleanup(ab, &dp->reo_dst_ring[i]);
}

int ath12k_dp_rx_pdev_reo_setup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = &ab->dp;
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

void ath12k_dp_rx_reo_cmd_list_cleanup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = &ab->dp;
	struct ath12k_dp_rx_reo_cmd *cmd, *tmp;
	struct ath12k_dp_rx_reo_cache_flush_elem *cmd_cache, *tmp_cache;

	spin_lock_bh(&dp->reo_cmd_lock);
	list_for_each_entry_safe(cmd, tmp, &dp->reo_cmd_list, list) {
		list_del(&cmd->list);
		dma_unmap_single(ab->dev, cmd->data.qbuf.paddr_aligned,
				 cmd->data.qbuf.size, DMA_BIDIRECTIONAL);
		kfree(cmd->data.qbuf.vaddr);
		kfree(cmd);
	}

	list_for_each_entry_safe(cmd_cache, tmp_cache,
				 &dp->reo_cmd_cache_flush_list, list) {
		list_del(&cmd_cache->list);
		dp->reo_cmd_cache_flush_count--;
		dma_unmap_single(ab->dev, cmd_cache->data.qbuf.paddr_aligned,
				 cmd_cache->data.qbuf.size, DMA_BIDIRECTIONAL);
		kfree(cmd_cache->data.qbuf.vaddr);
		kfree(cmd_cache);
	}
	spin_unlock_bh(&dp->reo_cmd_lock);
}

static void ath12k_dp_reo_cmd_free(struct ath12k_dp *dp, void *ctx,
				   enum hal_reo_cmd_status status)
{
	struct ath12k_dp_rx_tid *rx_tid = ctx;

	if (status != HAL_REO_CMD_SUCCESS)
		ath12k_warn(dp->ab, "failed to flush rx tid hw desc, tid %d status %d\n",
			    rx_tid->tid, status);

	dma_unmap_single(dp->ab->dev, rx_tid->qbuf.paddr_aligned, rx_tid->qbuf.size,
			 DMA_BIDIRECTIONAL);
	kfree(rx_tid->qbuf.vaddr);
	rx_tid->qbuf.vaddr = NULL;
}

static int ath12k_dp_reo_cmd_send(struct ath12k_base *ab, struct ath12k_dp_rx_tid *rx_tid,
				  enum hal_reo_cmd_type type,
				  struct ath12k_hal_reo_cmd *cmd,
				  void (*cb)(struct ath12k_dp *dp, void *ctx,
					     enum hal_reo_cmd_status status))
{
	struct ath12k_dp *dp = &ab->dp;
	struct ath12k_dp_rx_reo_cmd *dp_cmd;
	struct hal_srng *cmd_ring;
	int cmd_num;

	cmd_ring = &ab->hal.srng_list[dp->reo_cmd_ring.ring_id];
	cmd_num = ath12k_hal_reo_cmd_send(ab, cmd_ring, type, cmd);

	/* cmd_num should start from 1, during failure return the error code */
	if (cmd_num < 0)
		return cmd_num;

	/* reo cmd ring descriptors has cmd_num starting from 1 */
	if (cmd_num == 0)
		return -EINVAL;

	if (!cb)
		return 0;

	/* Can this be optimized so that we keep the pending command list only
	 * for tid delete command to free up the resource on the command status
	 * indication?
	 */
	dp_cmd = kzalloc(sizeof(*dp_cmd), GFP_ATOMIC);

	if (!dp_cmd)
		return -ENOMEM;

	memcpy(&dp_cmd->data, rx_tid, sizeof(*rx_tid));
	dp_cmd->cmd_num = cmd_num;
	dp_cmd->handler = cb;

	spin_lock_bh(&dp->reo_cmd_lock);
	list_add_tail(&dp_cmd->list, &dp->reo_cmd_list);
	spin_unlock_bh(&dp->reo_cmd_lock);

	return 0;
}

static void ath12k_dp_reo_cache_flush(struct ath12k_base *ab,
				      struct ath12k_dp_rx_tid *rx_tid)
{
	struct ath12k_hal_reo_cmd cmd = {};
	unsigned long tot_desc_sz, desc_sz;
	int ret;

	tot_desc_sz = rx_tid->qbuf.size;
	desc_sz = ath12k_hal_reo_qdesc_size(0, HAL_DESC_REO_NON_QOS_TID);

	while (tot_desc_sz > desc_sz) {
		tot_desc_sz -= desc_sz;
		cmd.addr_lo = lower_32_bits(rx_tid->qbuf.paddr_aligned + tot_desc_sz);
		cmd.addr_hi = upper_32_bits(rx_tid->qbuf.paddr_aligned);
		ret = ath12k_dp_reo_cmd_send(ab, rx_tid,
					     HAL_REO_CMD_FLUSH_CACHE, &cmd,
					     NULL);
		if (ret)
			ath12k_warn(ab,
				    "failed to send HAL_REO_CMD_FLUSH_CACHE, tid %d (%d)\n",
				    rx_tid->tid, ret);
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.addr_lo = lower_32_bits(rx_tid->qbuf.paddr_aligned);
	cmd.addr_hi = upper_32_bits(rx_tid->qbuf.paddr_aligned);
	cmd.flag = HAL_REO_CMD_FLG_NEED_STATUS;
	ret = ath12k_dp_reo_cmd_send(ab, rx_tid,
				     HAL_REO_CMD_FLUSH_CACHE,
				     &cmd, ath12k_dp_reo_cmd_free);
	if (ret) {
		ath12k_err(ab, "failed to send HAL_REO_CMD_FLUSH_CACHE cmd, tid %d (%d)\n",
			   rx_tid->tid, ret);
		dma_unmap_single(ab->dev, rx_tid->qbuf.paddr_aligned, rx_tid->qbuf.size,
				 DMA_BIDIRECTIONAL);
		kfree(rx_tid->qbuf.vaddr);
		rx_tid->qbuf.vaddr = NULL;
	}
}

static void ath12k_dp_rx_tid_del_func(struct ath12k_dp *dp, void *ctx,
				      enum hal_reo_cmd_status status)
{
	struct ath12k_base *ab = dp->ab;
	struct ath12k_dp_rx_tid *rx_tid = ctx;
	struct ath12k_dp_rx_reo_cache_flush_elem *elem, *tmp;

	if (status == HAL_REO_CMD_DRAIN) {
		goto free_desc;
	} else if (status != HAL_REO_CMD_SUCCESS) {
		/* Shouldn't happen! Cleanup in case of other failure? */
		ath12k_warn(ab, "failed to delete rx tid %d hw descriptor %d\n",
			    rx_tid->tid, status);
		return;
	}

	elem = kzalloc(sizeof(*elem), GFP_ATOMIC);
	if (!elem)
		goto free_desc;

	elem->ts = jiffies;
	memcpy(&elem->data, rx_tid, sizeof(*rx_tid));

	spin_lock_bh(&dp->reo_cmd_lock);
	list_add_tail(&elem->list, &dp->reo_cmd_cache_flush_list);
	dp->reo_cmd_cache_flush_count++;

	/* Flush and invalidate aged REO desc from HW cache */
	list_for_each_entry_safe(elem, tmp, &dp->reo_cmd_cache_flush_list,
				 list) {
		if (dp->reo_cmd_cache_flush_count > ATH12K_DP_RX_REO_DESC_FREE_THRES ||
		    time_after(jiffies, elem->ts +
			       msecs_to_jiffies(ATH12K_DP_RX_REO_DESC_FREE_TIMEOUT_MS))) {
			list_del(&elem->list);
			dp->reo_cmd_cache_flush_count--;

			/* Unlock the reo_cmd_lock before using ath12k_dp_reo_cmd_send()
			 * within ath12k_dp_reo_cache_flush. The reo_cmd_cache_flush_list
			 * is used in only two contexts, one is in this function called
			 * from napi and the other in ath12k_dp_free during core destroy.
			 * Before dp_free, the irqs would be disabled and would wait to
			 * synchronize. Hence there wouldn’t be any race against add or
			 * delete to this list. Hence unlock-lock is safe here.
			 */
			spin_unlock_bh(&dp->reo_cmd_lock);

			ath12k_dp_reo_cache_flush(ab, &elem->data);
			kfree(elem);
			spin_lock_bh(&dp->reo_cmd_lock);
		}
	}
	spin_unlock_bh(&dp->reo_cmd_lock);

	return;
free_desc:
	dma_unmap_single(ab->dev, rx_tid->qbuf.paddr_aligned, rx_tid->qbuf.size,
			 DMA_BIDIRECTIONAL);
	kfree(rx_tid->qbuf.vaddr);
	rx_tid->qbuf.vaddr = NULL;
}

static void ath12k_peer_rx_tid_qref_setup(struct ath12k_base *ab, u16 peer_id, u16 tid,
					  dma_addr_t paddr)
{
	struct ath12k_reo_queue_ref *qref;
	struct ath12k_dp *dp = &ab->dp;
	bool ml_peer = false;

	if (!ab->hw_params->reoq_lut_support)
		return;

	if (peer_id & ATH12K_PEER_ML_ID_VALID) {
		peer_id &= ~ATH12K_PEER_ML_ID_VALID;
		ml_peer = true;
	}

	if (ml_peer)
		qref = (struct ath12k_reo_queue_ref *)dp->ml_reoq_lut.vaddr +
				(peer_id * (IEEE80211_NUM_TIDS + 1) + tid);
	else
		qref = (struct ath12k_reo_queue_ref *)dp->reoq_lut.vaddr +
				(peer_id * (IEEE80211_NUM_TIDS + 1) + tid);

	qref->info0 = u32_encode_bits(lower_32_bits(paddr),
				      BUFFER_ADDR_INFO0_ADDR);
	qref->info1 = u32_encode_bits(upper_32_bits(paddr),
				      BUFFER_ADDR_INFO1_ADDR) |
		      u32_encode_bits(tid, DP_REO_QREF_NUM);
	ath12k_hal_reo_shared_qaddr_cache_clear(ab);
}

static void ath12k_peer_rx_tid_qref_reset(struct ath12k_base *ab, u16 peer_id, u16 tid)
{
	struct ath12k_reo_queue_ref *qref;
	struct ath12k_dp *dp = &ab->dp;
	bool ml_peer = false;

	if (!ab->hw_params->reoq_lut_support)
		return;

	if (peer_id & ATH12K_PEER_ML_ID_VALID) {
		peer_id &= ~ATH12K_PEER_ML_ID_VALID;
		ml_peer = true;
	}

	if (ml_peer)
		qref = (struct ath12k_reo_queue_ref *)dp->ml_reoq_lut.vaddr +
				(peer_id * (IEEE80211_NUM_TIDS + 1) + tid);
	else
		qref = (struct ath12k_reo_queue_ref *)dp->reoq_lut.vaddr +
				(peer_id * (IEEE80211_NUM_TIDS + 1) + tid);

	qref->info0 = u32_encode_bits(0, BUFFER_ADDR_INFO0_ADDR);
	qref->info1 = u32_encode_bits(0, BUFFER_ADDR_INFO1_ADDR) |
		      u32_encode_bits(tid, DP_REO_QREF_NUM);
}

void ath12k_dp_rx_peer_tid_delete(struct ath12k *ar,
				  struct ath12k_peer *peer, u8 tid)
{
	struct ath12k_hal_reo_cmd cmd = {};
	struct ath12k_dp_rx_tid *rx_tid = &peer->rx_tid[tid];
	int ret;

	if (!rx_tid->active)
		return;

	cmd.flag = HAL_REO_CMD_FLG_NEED_STATUS;
	cmd.addr_lo = lower_32_bits(rx_tid->qbuf.paddr_aligned);
	cmd.addr_hi = upper_32_bits(rx_tid->qbuf.paddr_aligned);
	cmd.upd0 = HAL_REO_CMD_UPD0_VLD;
	ret = ath12k_dp_reo_cmd_send(ar->ab, rx_tid,
				     HAL_REO_CMD_UPDATE_RX_QUEUE, &cmd,
				     ath12k_dp_rx_tid_del_func);
	if (ret) {
		ath12k_err(ar->ab, "failed to send HAL_REO_CMD_UPDATE_RX_QUEUE cmd, tid %d (%d)\n",
			   tid, ret);
		dma_unmap_single(ar->ab->dev, rx_tid->qbuf.paddr_aligned,
				 rx_tid->qbuf.size, DMA_BIDIRECTIONAL);
		kfree(rx_tid->qbuf.vaddr);
		rx_tid->qbuf.vaddr = NULL;
	}

	if (peer->mlo)
		ath12k_peer_rx_tid_qref_reset(ar->ab, peer->ml_id, tid);
	else
		ath12k_peer_rx_tid_qref_reset(ar->ab, peer->peer_id, tid);

	rx_tid->active = false;
}

int ath12k_dp_rx_link_desc_return(struct ath12k_base *ab,
				  struct ath12k_buffer_addr *buf_addr_info,
				  enum hal_wbm_rel_bm_act action)
{
	struct hal_wbm_release_ring *desc;
	struct ath12k_dp *dp = &ab->dp;
	struct hal_srng *srng;
	int ret = 0;

	srng = &ab->hal.srng_list[dp->wbm_desc_rel_ring.ring_id];

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

	desc = ath12k_hal_srng_src_get_next_entry(ab, srng);
	if (!desc) {
		ret = -ENOBUFS;
		goto exit;
	}

	ath12k_hal_rx_msdu_link_desc_set(ab, desc, buf_addr_info, action);

exit:
	ath12k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	return ret;
}

static void ath12k_dp_rx_frags_cleanup(struct ath12k_dp_rx_tid *rx_tid,
				       bool rel_link_desc)
{
	struct ath12k_buffer_addr *buf_addr_info;
	struct ath12k_base *ab = rx_tid->ab;

	lockdep_assert_held(&ab->base_lock);

	if (rx_tid->dst_ring_desc) {
		if (rel_link_desc) {
			buf_addr_info = &rx_tid->dst_ring_desc->buf_addr_info;
			ath12k_dp_rx_link_desc_return(ab, buf_addr_info,
						      HAL_WBM_REL_BM_ACT_PUT_IN_IDLE);
		}
		kfree(rx_tid->dst_ring_desc);
		rx_tid->dst_ring_desc = NULL;
	}

	rx_tid->cur_sn = 0;
	rx_tid->last_frag_no = 0;
	rx_tid->rx_frag_bitmap = 0;
	__skb_queue_purge(&rx_tid->rx_frags);
}

void ath12k_dp_rx_peer_tid_cleanup(struct ath12k *ar, struct ath12k_peer *peer)
{
	struct ath12k_dp_rx_tid *rx_tid;
	int i;

	lockdep_assert_held(&ar->ab->base_lock);

	for (i = 0; i <= IEEE80211_NUM_TIDS; i++) {
		rx_tid = &peer->rx_tid[i];

		ath12k_dp_rx_peer_tid_delete(ar, peer, i);
		ath12k_dp_rx_frags_cleanup(rx_tid, true);

		spin_unlock_bh(&ar->ab->base_lock);
		timer_delete_sync(&rx_tid->frag_timer);
		spin_lock_bh(&ar->ab->base_lock);
	}
}

static int ath12k_peer_rx_tid_reo_update(struct ath12k *ar,
					 struct ath12k_peer *peer,
					 struct ath12k_dp_rx_tid *rx_tid,
					 u32 ba_win_sz, u16 ssn,
					 bool update_ssn)
{
	struct ath12k_hal_reo_cmd cmd = {};
	int ret;

	cmd.addr_lo = lower_32_bits(rx_tid->qbuf.paddr_aligned);
	cmd.addr_hi = upper_32_bits(rx_tid->qbuf.paddr_aligned);
	cmd.flag = HAL_REO_CMD_FLG_NEED_STATUS;
	cmd.upd0 = HAL_REO_CMD_UPD0_BA_WINDOW_SIZE;
	cmd.ba_window_size = ba_win_sz;

	if (update_ssn) {
		cmd.upd0 |= HAL_REO_CMD_UPD0_SSN;
		cmd.upd2 = u32_encode_bits(ssn, HAL_REO_CMD_UPD2_SSN);
	}

	ret = ath12k_dp_reo_cmd_send(ar->ab, rx_tid,
				     HAL_REO_CMD_UPDATE_RX_QUEUE, &cmd,
				     NULL);
	if (ret) {
		ath12k_warn(ar->ab, "failed to update rx tid queue, tid %d (%d)\n",
			    rx_tid->tid, ret);
		return ret;
	}

	rx_tid->ba_win_sz = ba_win_sz;

	return 0;
}

static int ath12k_dp_rx_assign_reoq(struct ath12k_base *ab,
				    struct ath12k_sta *ahsta,
				    struct ath12k_dp_rx_tid *rx_tid,
				    u16 ssn, enum hal_pn_type pn_type)
{
	u32 ba_win_sz = rx_tid->ba_win_sz;
	struct ath12k_reoq_buf *buf;
	void *vaddr, *vaddr_aligned;
	dma_addr_t paddr_aligned;
	u8 tid = rx_tid->tid;
	u32 hw_desc_sz;
	int ret;

	buf = &ahsta->reoq_bufs[tid];
	if (!buf->vaddr) {
		/* TODO: Optimize the memory allocation for qos tid based on
		 * the actual BA window size in REO tid update path.
		 */
		if (tid == HAL_DESC_REO_NON_QOS_TID)
			hw_desc_sz = ath12k_hal_reo_qdesc_size(ba_win_sz, tid);
		else
			hw_desc_sz = ath12k_hal_reo_qdesc_size(DP_BA_WIN_SZ_MAX, tid);

		vaddr = kzalloc(hw_desc_sz + HAL_LINK_DESC_ALIGN - 1, GFP_ATOMIC);
		if (!vaddr)
			return -ENOMEM;

		vaddr_aligned = PTR_ALIGN(vaddr, HAL_LINK_DESC_ALIGN);

		ath12k_hal_reo_qdesc_setup(vaddr_aligned, tid, ba_win_sz,
					   ssn, pn_type);

		paddr_aligned = dma_map_single(ab->dev, vaddr_aligned, hw_desc_sz,
					       DMA_BIDIRECTIONAL);
		ret = dma_mapping_error(ab->dev, paddr_aligned);
		if (ret) {
			kfree(vaddr);
			return ret;
		}

		buf->vaddr = vaddr;
		buf->paddr_aligned = paddr_aligned;
		buf->size = hw_desc_sz;
	}

	rx_tid->qbuf = *buf;
	rx_tid->active = true;

	return 0;
}

int ath12k_dp_rx_peer_tid_setup(struct ath12k *ar, const u8 *peer_mac, int vdev_id,
				u8 tid, u32 ba_win_sz, u16 ssn,
				enum hal_pn_type pn_type)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = &ab->dp;
	struct ath12k_peer *peer;
	struct ath12k_sta *ahsta;
	struct ath12k_dp_rx_tid *rx_tid;
	dma_addr_t paddr_aligned;
	int ret;

	spin_lock_bh(&ab->base_lock);

	peer = ath12k_peer_find(ab, vdev_id, peer_mac);
	if (!peer) {
		spin_unlock_bh(&ab->base_lock);
		ath12k_warn(ab, "failed to find the peer to set up rx tid\n");
		return -ENOENT;
	}

	if (ab->hw_params->dp_primary_link_only &&
	    !peer->primary_link) {
		spin_unlock_bh(&ab->base_lock);
		return 0;
	}

	if (ab->hw_params->reoq_lut_support &&
	    (!dp->reoq_lut.vaddr || !dp->ml_reoq_lut.vaddr)) {
		spin_unlock_bh(&ab->base_lock);
		ath12k_warn(ab, "reo qref table is not setup\n");
		return -EINVAL;
	}

	if (peer->peer_id > DP_MAX_PEER_ID || tid > IEEE80211_NUM_TIDS) {
		ath12k_warn(ab, "peer id of peer %d or tid %d doesn't allow reoq setup\n",
			    peer->peer_id, tid);
		spin_unlock_bh(&ab->base_lock);
		return -EINVAL;
	}

	rx_tid = &peer->rx_tid[tid];
	/* Update the tid queue if it is already setup */
	if (rx_tid->active) {
		ret = ath12k_peer_rx_tid_reo_update(ar, peer, rx_tid,
						    ba_win_sz, ssn, true);
		spin_unlock_bh(&ab->base_lock);
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

	ahsta = ath12k_sta_to_ahsta(peer->sta);
	ret = ath12k_dp_rx_assign_reoq(ab, ahsta, rx_tid, ssn, pn_type);
	if (ret) {
		spin_unlock_bh(&ab->base_lock);
		ath12k_warn(ab, "failed to assign reoq buf for rx tid %u\n", tid);
		return ret;
	}

	paddr_aligned = rx_tid->qbuf.paddr_aligned;
	if (ab->hw_params->reoq_lut_support) {
		/* Update the REO queue LUT at the corresponding peer id
		 * and tid with qaddr.
		 */
		if (peer->mlo)
			ath12k_peer_rx_tid_qref_setup(ab, peer->ml_id, tid,
						      paddr_aligned);
		else
			ath12k_peer_rx_tid_qref_setup(ab, peer->peer_id, tid,
						      paddr_aligned);

		spin_unlock_bh(&ab->base_lock);
	} else {
		spin_unlock_bh(&ab->base_lock);
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
	struct ath12k_peer *peer;
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

	spin_lock_bh(&ab->base_lock);

	peer = ath12k_peer_find(ab, vdev_id, arsta->addr);
	if (!peer) {
		spin_unlock_bh(&ab->base_lock);
		ath12k_warn(ab, "failed to find the peer to stop rx aggregation\n");
		return -ENOENT;
	}

	active = peer->rx_tid[params->tid].active;

	if (!active) {
		spin_unlock_bh(&ab->base_lock);
		return 0;
	}

	ret = ath12k_peer_rx_tid_reo_update(ar, peer, peer->rx_tid, 1, 0, false);
	spin_unlock_bh(&ab->base_lock);
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
	struct ath12k_hal_reo_cmd cmd = {};
	struct ath12k_peer *peer;
	struct ath12k_dp_rx_tid *rx_tid;
	u8 tid;
	int ret = 0;

	/* NOTE: Enable PN/TSC replay check offload only for unicast frames.
	 * We use mac80211 PN/TSC replay check functionality for bcast/mcast
	 * for now.
	 */
	if (!(key->flags & IEEE80211_KEY_FLAG_PAIRWISE))
		return 0;

	cmd.flag = HAL_REO_CMD_FLG_NEED_STATUS;
	cmd.upd0 = HAL_REO_CMD_UPD0_PN |
		    HAL_REO_CMD_UPD0_PN_SIZE |
		    HAL_REO_CMD_UPD0_PN_VALID |
		    HAL_REO_CMD_UPD0_PN_CHECK |
		    HAL_REO_CMD_UPD0_SVLD;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		if (key_cmd == SET_KEY) {
			cmd.upd1 |= HAL_REO_CMD_UPD1_PN_CHECK;
			cmd.pn_size = 48;
		}
		break;
	default:
		break;
	}

	spin_lock_bh(&ab->base_lock);

	peer = ath12k_peer_find(ab, arvif->vdev_id, peer_addr);
	if (!peer) {
		spin_unlock_bh(&ab->base_lock);
		ath12k_warn(ab, "failed to find the peer %pM to configure pn replay detection\n",
			    peer_addr);
		return -ENOENT;
	}

	for (tid = 0; tid <= IEEE80211_NUM_TIDS; tid++) {
		rx_tid = &peer->rx_tid[tid];
		if (!rx_tid->active)
			continue;
		cmd.addr_lo = lower_32_bits(rx_tid->qbuf.paddr_aligned);
		cmd.addr_hi = upper_32_bits(rx_tid->qbuf.paddr_aligned);
		ret = ath12k_dp_reo_cmd_send(ab, rx_tid,
					     HAL_REO_CMD_UPDATE_RX_QUEUE,
					     &cmd, NULL);
		if (ret) {
			ath12k_warn(ab, "failed to configure rx tid %d queue of peer %pM for pn replay detection %d\n",
				    tid, peer_addr, ret);
			break;
		}
	}

	spin_unlock_bh(&ab->base_lock);

	return ret;
}

static int ath12k_get_ppdu_user_index(struct htt_ppdu_stats *ppdu_stats,
				      u16 peer_id)
{
	int i;

	for (i = 0; i < HTT_PPDU_STATS_MAX_USERS - 1; i++) {
		if (ppdu_stats->user_stats[i].is_valid_peer_id) {
			if (peer_id == ppdu_stats->user_stats[i].peer_id)
				return i;
		} else {
			return i;
		}
	}

	return -EINVAL;
}

static int ath12k_htt_tlv_ppdu_stats_parse(struct ath12k_base *ab,
					   u16 tag, u16 len, const void *ptr,
					   void *data)
{
	const struct htt_ppdu_stats_usr_cmpltn_ack_ba_status *ba_status;
	const struct htt_ppdu_stats_usr_cmpltn_cmn *cmplt_cmn;
	const struct htt_ppdu_stats_user_rate *user_rate;
	struct htt_ppdu_stats_info *ppdu_info;
	struct htt_ppdu_user_stats *user_stats;
	int cur_user;
	u16 peer_id;

	ppdu_info = data;

	switch (tag) {
	case HTT_PPDU_STATS_TAG_COMMON:
		if (len < sizeof(struct htt_ppdu_stats_common)) {
			ath12k_warn(ab, "Invalid len %d for the tag 0x%x\n",
				    len, tag);
			return -EINVAL;
		}
		memcpy(&ppdu_info->ppdu_stats.common, ptr,
		       sizeof(struct htt_ppdu_stats_common));
		break;
	case HTT_PPDU_STATS_TAG_USR_RATE:
		if (len < sizeof(struct htt_ppdu_stats_user_rate)) {
			ath12k_warn(ab, "Invalid len %d for the tag 0x%x\n",
				    len, tag);
			return -EINVAL;
		}
		user_rate = ptr;
		peer_id = le16_to_cpu(user_rate->sw_peer_id);
		cur_user = ath12k_get_ppdu_user_index(&ppdu_info->ppdu_stats,
						      peer_id);
		if (cur_user < 0)
			return -EINVAL;
		user_stats = &ppdu_info->ppdu_stats.user_stats[cur_user];
		user_stats->peer_id = peer_id;
		user_stats->is_valid_peer_id = true;
		memcpy(&user_stats->rate, ptr,
		       sizeof(struct htt_ppdu_stats_user_rate));
		user_stats->tlv_flags |= BIT(tag);
		break;
	case HTT_PPDU_STATS_TAG_USR_COMPLTN_COMMON:
		if (len < sizeof(struct htt_ppdu_stats_usr_cmpltn_cmn)) {
			ath12k_warn(ab, "Invalid len %d for the tag 0x%x\n",
				    len, tag);
			return -EINVAL;
		}

		cmplt_cmn = ptr;
		peer_id = le16_to_cpu(cmplt_cmn->sw_peer_id);
		cur_user = ath12k_get_ppdu_user_index(&ppdu_info->ppdu_stats,
						      peer_id);
		if (cur_user < 0)
			return -EINVAL;
		user_stats = &ppdu_info->ppdu_stats.user_stats[cur_user];
		user_stats->peer_id = peer_id;
		user_stats->is_valid_peer_id = true;
		memcpy(&user_stats->cmpltn_cmn, ptr,
		       sizeof(struct htt_ppdu_stats_usr_cmpltn_cmn));
		user_stats->tlv_flags |= BIT(tag);
		break;
	case HTT_PPDU_STATS_TAG_USR_COMPLTN_ACK_BA_STATUS:
		if (len <
		    sizeof(struct htt_ppdu_stats_usr_cmpltn_ack_ba_status)) {
			ath12k_warn(ab, "Invalid len %d for the tag 0x%x\n",
				    len, tag);
			return -EINVAL;
		}

		ba_status = ptr;
		peer_id = le16_to_cpu(ba_status->sw_peer_id);
		cur_user = ath12k_get_ppdu_user_index(&ppdu_info->ppdu_stats,
						      peer_id);
		if (cur_user < 0)
			return -EINVAL;
		user_stats = &ppdu_info->ppdu_stats.user_stats[cur_user];
		user_stats->peer_id = peer_id;
		user_stats->is_valid_peer_id = true;
		memcpy(&user_stats->ack_ba, ptr,
		       sizeof(struct htt_ppdu_stats_usr_cmpltn_ack_ba_status));
		user_stats->tlv_flags |= BIT(tag);
		break;
	}
	return 0;
}

int ath12k_dp_htt_tlv_iter(struct ath12k_base *ab, const void *ptr, size_t len,
			   int (*iter)(struct ath12k_base *ar, u16 tag, u16 len,
				       const void *ptr, void *data),
			   void *data)
{
	const struct htt_tlv *tlv;
	const void *begin = ptr;
	u16 tlv_tag, tlv_len;
	int ret = -EINVAL;

	while (len > 0) {
		if (len < sizeof(*tlv)) {
			ath12k_err(ab, "htt tlv parse failure at byte %zd (%zu bytes left, %zu expected)\n",
				   ptr - begin, len, sizeof(*tlv));
			return -EINVAL;
		}
		tlv = (struct htt_tlv *)ptr;
		tlv_tag = le32_get_bits(tlv->header, HTT_TLV_TAG);
		tlv_len = le32_get_bits(tlv->header, HTT_TLV_LEN);
		ptr += sizeof(*tlv);
		len -= sizeof(*tlv);

		if (tlv_len > len) {
			ath12k_err(ab, "htt tlv parse failure of tag %u at byte %zd (%zu bytes left, %u expected)\n",
				   tlv_tag, ptr - begin, len, tlv_len);
			return -EINVAL;
		}
		ret = iter(ab, tlv_tag, tlv_len, ptr, data);
		if (ret == -ENOMEM)
			return ret;

		ptr += tlv_len;
		len -= tlv_len;
	}
	return 0;
}

static void
ath12k_update_per_peer_tx_stats(struct ath12k *ar,
				struct htt_ppdu_stats *ppdu_stats, u8 user)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_peer *peer;
	struct ath12k_link_sta *arsta;
	struct htt_ppdu_stats_user_rate *user_rate;
	struct ath12k_per_peer_tx_stats *peer_stats = &ar->peer_tx_stats;
	struct htt_ppdu_user_stats *usr_stats = &ppdu_stats->user_stats[user];
	struct htt_ppdu_stats_common *common = &ppdu_stats->common;
	int ret;
	u8 flags, mcs, nss, bw, sgi, dcm, ppdu_type, rate_idx = 0;
	u32 v, succ_bytes = 0;
	u16 tones, rate = 0, succ_pkts = 0;
	u32 tx_duration = 0;
	u8 tid = HTT_PPDU_STATS_NON_QOS_TID;
	u16 tx_retry_failed = 0, tx_retry_count = 0;
	bool is_ampdu = false, is_ofdma;

	if (!(usr_stats->tlv_flags & BIT(HTT_PPDU_STATS_TAG_USR_RATE)))
		return;

	if (usr_stats->tlv_flags & BIT(HTT_PPDU_STATS_TAG_USR_COMPLTN_COMMON)) {
		is_ampdu =
			HTT_USR_CMPLTN_IS_AMPDU(usr_stats->cmpltn_cmn.flags);
		tx_retry_failed =
			__le16_to_cpu(usr_stats->cmpltn_cmn.mpdu_tried) -
			__le16_to_cpu(usr_stats->cmpltn_cmn.mpdu_success);
		tx_retry_count =
			HTT_USR_CMPLTN_LONG_RETRY(usr_stats->cmpltn_cmn.flags) +
			HTT_USR_CMPLTN_SHORT_RETRY(usr_stats->cmpltn_cmn.flags);
	}

	if (usr_stats->tlv_flags &
	    BIT(HTT_PPDU_STATS_TAG_USR_COMPLTN_ACK_BA_STATUS)) {
		succ_bytes = le32_to_cpu(usr_stats->ack_ba.success_bytes);
		succ_pkts = le32_get_bits(usr_stats->ack_ba.info,
					  HTT_PPDU_STATS_ACK_BA_INFO_NUM_MSDU_M);
		tid = le32_get_bits(usr_stats->ack_ba.info,
				    HTT_PPDU_STATS_ACK_BA_INFO_TID_NUM);
	}

	if (common->fes_duration_us)
		tx_duration = le32_to_cpu(common->fes_duration_us);

	user_rate = &usr_stats->rate;
	flags = HTT_USR_RATE_PREAMBLE(user_rate->rate_flags);
	bw = HTT_USR_RATE_BW(user_rate->rate_flags) - 2;
	nss = HTT_USR_RATE_NSS(user_rate->rate_flags) + 1;
	mcs = HTT_USR_RATE_MCS(user_rate->rate_flags);
	sgi = HTT_USR_RATE_GI(user_rate->rate_flags);
	dcm = HTT_USR_RATE_DCM(user_rate->rate_flags);

	ppdu_type = HTT_USR_RATE_PPDU_TYPE(user_rate->info1);
	is_ofdma = (ppdu_type == HTT_PPDU_STATS_PPDU_TYPE_MU_OFDMA) ||
		   (ppdu_type == HTT_PPDU_STATS_PPDU_TYPE_MU_MIMO_OFDMA);

	/* Note: If host configured fixed rates and in some other special
	 * cases, the broadcast/management frames are sent in different rates.
	 * Firmware rate's control to be skipped for this?
	 */

	if (flags == WMI_RATE_PREAMBLE_HE && mcs > ATH12K_HE_MCS_MAX) {
		ath12k_warn(ab, "Invalid HE mcs %d peer stats",  mcs);
		return;
	}

	if (flags == WMI_RATE_PREAMBLE_VHT && mcs > ATH12K_VHT_MCS_MAX) {
		ath12k_warn(ab, "Invalid VHT mcs %d peer stats",  mcs);
		return;
	}

	if (flags == WMI_RATE_PREAMBLE_HT && (mcs > ATH12K_HT_MCS_MAX || nss < 1)) {
		ath12k_warn(ab, "Invalid HT mcs %d nss %d peer stats",
			    mcs, nss);
		return;
	}

	if (flags == WMI_RATE_PREAMBLE_CCK || flags == WMI_RATE_PREAMBLE_OFDM) {
		ret = ath12k_mac_hw_ratecode_to_legacy_rate(mcs,
							    flags,
							    &rate_idx,
							    &rate);
		if (ret < 0)
			return;
	}

	rcu_read_lock();
	spin_lock_bh(&ab->base_lock);
	peer = ath12k_peer_find_by_id(ab, usr_stats->peer_id);

	if (!peer || !peer->sta) {
		spin_unlock_bh(&ab->base_lock);
		rcu_read_unlock();
		return;
	}

	arsta = ath12k_peer_get_link_sta(ab, peer);
	if (!arsta) {
		spin_unlock_bh(&ab->base_lock);
		rcu_read_unlock();
		return;
	}

	memset(&arsta->txrate, 0, sizeof(arsta->txrate));

	arsta->txrate.bw = ath12k_mac_bw_to_mac80211_bw(bw);

	switch (flags) {
	case WMI_RATE_PREAMBLE_OFDM:
		arsta->txrate.legacy = rate;
		break;
	case WMI_RATE_PREAMBLE_CCK:
		arsta->txrate.legacy = rate;
		break;
	case WMI_RATE_PREAMBLE_HT:
		arsta->txrate.mcs = mcs + 8 * (nss - 1);
		arsta->txrate.flags = RATE_INFO_FLAGS_MCS;
		if (sgi)
			arsta->txrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case WMI_RATE_PREAMBLE_VHT:
		arsta->txrate.mcs = mcs;
		arsta->txrate.flags = RATE_INFO_FLAGS_VHT_MCS;
		if (sgi)
			arsta->txrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case WMI_RATE_PREAMBLE_HE:
		arsta->txrate.mcs = mcs;
		arsta->txrate.flags = RATE_INFO_FLAGS_HE_MCS;
		arsta->txrate.he_dcm = dcm;
		arsta->txrate.he_gi = ath12k_he_gi_to_nl80211_he_gi(sgi);
		tones = le16_to_cpu(user_rate->ru_end) -
			le16_to_cpu(user_rate->ru_start) + 1;
		v = ath12k_he_ru_tones_to_nl80211_he_ru_alloc(tones);
		arsta->txrate.he_ru_alloc = v;
		if (is_ofdma)
			arsta->txrate.bw = RATE_INFO_BW_HE_RU;
		break;
	case WMI_RATE_PREAMBLE_EHT:
		arsta->txrate.mcs = mcs;
		arsta->txrate.flags = RATE_INFO_FLAGS_EHT_MCS;
		arsta->txrate.he_dcm = dcm;
		arsta->txrate.eht_gi = ath12k_mac_eht_gi_to_nl80211_eht_gi(sgi);
		tones = le16_to_cpu(user_rate->ru_end) -
			le16_to_cpu(user_rate->ru_start) + 1;
		v = ath12k_mac_eht_ru_tones_to_nl80211_eht_ru_alloc(tones);
		arsta->txrate.eht_ru_alloc = v;
		if (is_ofdma)
			arsta->txrate.bw = RATE_INFO_BW_EHT_RU;
		break;
	}

	arsta->tx_retry_failed += tx_retry_failed;
	arsta->tx_retry_count += tx_retry_count;
	arsta->txrate.nss = nss;
	arsta->tx_duration += tx_duration;
	memcpy(&arsta->last_txrate, &arsta->txrate, sizeof(struct rate_info));

	/* PPDU stats reported for mgmt packet doesn't have valid tx bytes.
	 * So skip peer stats update for mgmt packets.
	 */
	if (tid < HTT_PPDU_STATS_NON_QOS_TID) {
		memset(peer_stats, 0, sizeof(*peer_stats));
		peer_stats->succ_pkts = succ_pkts;
		peer_stats->succ_bytes = succ_bytes;
		peer_stats->is_ampdu = is_ampdu;
		peer_stats->duration = tx_duration;
		peer_stats->ba_fails =
			HTT_USR_CMPLTN_LONG_RETRY(usr_stats->cmpltn_cmn.flags) +
			HTT_USR_CMPLTN_SHORT_RETRY(usr_stats->cmpltn_cmn.flags);
	}

	spin_unlock_bh(&ab->base_lock);
	rcu_read_unlock();
}

static void ath12k_htt_update_ppdu_stats(struct ath12k *ar,
					 struct htt_ppdu_stats *ppdu_stats)
{
	u8 user;

	for (user = 0; user < HTT_PPDU_STATS_MAX_USERS - 1; user++)
		ath12k_update_per_peer_tx_stats(ar, ppdu_stats, user);
}

static
struct htt_ppdu_stats_info *ath12k_dp_htt_get_ppdu_desc(struct ath12k *ar,
							u32 ppdu_id)
{
	struct htt_ppdu_stats_info *ppdu_info;

	lockdep_assert_held(&ar->data_lock);
	if (!list_empty(&ar->ppdu_stats_info)) {
		list_for_each_entry(ppdu_info, &ar->ppdu_stats_info, list) {
			if (ppdu_info->ppdu_id == ppdu_id)
				return ppdu_info;
		}

		if (ar->ppdu_stat_list_depth > HTT_PPDU_DESC_MAX_DEPTH) {
			ppdu_info = list_first_entry(&ar->ppdu_stats_info,
						     typeof(*ppdu_info), list);
			list_del(&ppdu_info->list);
			ar->ppdu_stat_list_depth--;
			ath12k_htt_update_ppdu_stats(ar, &ppdu_info->ppdu_stats);
			kfree(ppdu_info);
		}
	}

	ppdu_info = kzalloc(sizeof(*ppdu_info), GFP_ATOMIC);
	if (!ppdu_info)
		return NULL;

	list_add_tail(&ppdu_info->list, &ar->ppdu_stats_info);
	ar->ppdu_stat_list_depth++;

	return ppdu_info;
}

static void ath12k_copy_to_delay_stats(struct ath12k_peer *peer,
				       struct htt_ppdu_user_stats *usr_stats)
{
	peer->ppdu_stats_delayba.sw_peer_id = le16_to_cpu(usr_stats->rate.sw_peer_id);
	peer->ppdu_stats_delayba.info0 = le32_to_cpu(usr_stats->rate.info0);
	peer->ppdu_stats_delayba.ru_end = le16_to_cpu(usr_stats->rate.ru_end);
	peer->ppdu_stats_delayba.ru_start = le16_to_cpu(usr_stats->rate.ru_start);
	peer->ppdu_stats_delayba.info1 = le32_to_cpu(usr_stats->rate.info1);
	peer->ppdu_stats_delayba.rate_flags = le32_to_cpu(usr_stats->rate.rate_flags);
	peer->ppdu_stats_delayba.resp_rate_flags =
		le32_to_cpu(usr_stats->rate.resp_rate_flags);

	peer->delayba_flag = true;
}

static void ath12k_copy_to_bar(struct ath12k_peer *peer,
			       struct htt_ppdu_user_stats *usr_stats)
{
	usr_stats->rate.sw_peer_id = cpu_to_le16(peer->ppdu_stats_delayba.sw_peer_id);
	usr_stats->rate.info0 = cpu_to_le32(peer->ppdu_stats_delayba.info0);
	usr_stats->rate.ru_end = cpu_to_le16(peer->ppdu_stats_delayba.ru_end);
	usr_stats->rate.ru_start = cpu_to_le16(peer->ppdu_stats_delayba.ru_start);
	usr_stats->rate.info1 = cpu_to_le32(peer->ppdu_stats_delayba.info1);
	usr_stats->rate.rate_flags = cpu_to_le32(peer->ppdu_stats_delayba.rate_flags);
	usr_stats->rate.resp_rate_flags =
		cpu_to_le32(peer->ppdu_stats_delayba.resp_rate_flags);

	peer->delayba_flag = false;
}

static int ath12k_htt_pull_ppdu_stats(struct ath12k_base *ab,
				      struct sk_buff *skb)
{
	struct ath12k_htt_ppdu_stats_msg *msg;
	struct htt_ppdu_stats_info *ppdu_info;
	struct ath12k_peer *peer = NULL;
	struct htt_ppdu_user_stats *usr_stats = NULL;
	u32 peer_id = 0;
	struct ath12k *ar;
	int ret, i;
	u8 pdev_id;
	u32 ppdu_id, len;

	msg = (struct ath12k_htt_ppdu_stats_msg *)skb->data;
	len = le32_get_bits(msg->info, HTT_T2H_PPDU_STATS_INFO_PAYLOAD_SIZE);
	if (len > (skb->len - struct_size(msg, data, 0))) {
		ath12k_warn(ab,
			    "HTT PPDU STATS event has unexpected payload size %u, should be smaller than %u\n",
			    len, skb->len);
		return -EINVAL;
	}

	pdev_id = le32_get_bits(msg->info, HTT_T2H_PPDU_STATS_INFO_PDEV_ID);
	ppdu_id = le32_to_cpu(msg->ppdu_id);

	rcu_read_lock();
	ar = ath12k_mac_get_ar_by_pdev_id(ab, pdev_id);
	if (!ar) {
		ret = -EINVAL;
		goto exit;
	}

	spin_lock_bh(&ar->data_lock);
	ppdu_info = ath12k_dp_htt_get_ppdu_desc(ar, ppdu_id);
	if (!ppdu_info) {
		spin_unlock_bh(&ar->data_lock);
		ret = -EINVAL;
		goto exit;
	}

	ppdu_info->ppdu_id = ppdu_id;
	ret = ath12k_dp_htt_tlv_iter(ab, msg->data, len,
				     ath12k_htt_tlv_ppdu_stats_parse,
				     (void *)ppdu_info);
	if (ret) {
		spin_unlock_bh(&ar->data_lock);
		ath12k_warn(ab, "Failed to parse tlv %d\n", ret);
		goto exit;
	}

	if (ppdu_info->ppdu_stats.common.num_users >= HTT_PPDU_STATS_MAX_USERS) {
		spin_unlock_bh(&ar->data_lock);
		ath12k_warn(ab,
			    "HTT PPDU STATS event has unexpected num_users %u, should be smaller than %u\n",
			    ppdu_info->ppdu_stats.common.num_users,
			    HTT_PPDU_STATS_MAX_USERS);
		ret = -EINVAL;
		goto exit;
	}

	/* back up data rate tlv for all peers */
	if (ppdu_info->frame_type == HTT_STATS_PPDU_FTYPE_DATA &&
	    (ppdu_info->tlv_bitmap & (1 << HTT_PPDU_STATS_TAG_USR_COMMON)) &&
	    ppdu_info->delay_ba) {
		for (i = 0; i < ppdu_info->ppdu_stats.common.num_users; i++) {
			peer_id = ppdu_info->ppdu_stats.user_stats[i].peer_id;
			spin_lock_bh(&ab->base_lock);
			peer = ath12k_peer_find_by_id(ab, peer_id);
			if (!peer) {
				spin_unlock_bh(&ab->base_lock);
				continue;
			}

			usr_stats = &ppdu_info->ppdu_stats.user_stats[i];
			if (usr_stats->delay_ba)
				ath12k_copy_to_delay_stats(peer, usr_stats);
			spin_unlock_bh(&ab->base_lock);
		}
	}

	/* restore all peers' data rate tlv to mu-bar tlv */
	if (ppdu_info->frame_type == HTT_STATS_PPDU_FTYPE_BAR &&
	    (ppdu_info->tlv_bitmap & (1 << HTT_PPDU_STATS_TAG_USR_COMMON))) {
		for (i = 0; i < ppdu_info->bar_num_users; i++) {
			peer_id = ppdu_info->ppdu_stats.user_stats[i].peer_id;
			spin_lock_bh(&ab->base_lock);
			peer = ath12k_peer_find_by_id(ab, peer_id);
			if (!peer) {
				spin_unlock_bh(&ab->base_lock);
				continue;
			}

			usr_stats = &ppdu_info->ppdu_stats.user_stats[i];
			if (peer->delayba_flag)
				ath12k_copy_to_bar(peer, usr_stats);
			spin_unlock_bh(&ab->base_lock);
		}
	}

	spin_unlock_bh(&ar->data_lock);

exit:
	rcu_read_unlock();

	return ret;
}

static void ath12k_htt_mlo_offset_event_handler(struct ath12k_base *ab,
						struct sk_buff *skb)
{
	struct ath12k_htt_mlo_offset_msg *msg;
	struct ath12k_pdev *pdev;
	struct ath12k *ar;
	u8 pdev_id;

	msg = (struct ath12k_htt_mlo_offset_msg *)skb->data;
	pdev_id = u32_get_bits(__le32_to_cpu(msg->info),
			       HTT_T2H_MLO_OFFSET_INFO_PDEV_ID);

	rcu_read_lock();
	ar = ath12k_mac_get_ar_by_pdev_id(ab, pdev_id);
	if (!ar) {
		/* It is possible that the ar is not yet active (started).
		 * The above function will only look for the active pdev
		 * and hence %NULL return is possible. Just silently
		 * discard this message
		 */
		goto exit;
	}

	spin_lock_bh(&ar->data_lock);
	pdev = ar->pdev;

	pdev->timestamp.info = __le32_to_cpu(msg->info);
	pdev->timestamp.sync_timestamp_lo_us = __le32_to_cpu(msg->sync_timestamp_lo_us);
	pdev->timestamp.sync_timestamp_hi_us = __le32_to_cpu(msg->sync_timestamp_hi_us);
	pdev->timestamp.mlo_offset_lo = __le32_to_cpu(msg->mlo_offset_lo);
	pdev->timestamp.mlo_offset_hi = __le32_to_cpu(msg->mlo_offset_hi);
	pdev->timestamp.mlo_offset_clks = __le32_to_cpu(msg->mlo_offset_clks);
	pdev->timestamp.mlo_comp_clks = __le32_to_cpu(msg->mlo_comp_clks);
	pdev->timestamp.mlo_comp_timer = __le32_to_cpu(msg->mlo_comp_timer);

	spin_unlock_bh(&ar->data_lock);
exit:
	rcu_read_unlock();
}

void ath12k_dp_htt_htc_t2h_msg_handler(struct ath12k_base *ab,
				       struct sk_buff *skb)
{
	struct ath12k_dp *dp = &ab->dp;
	struct htt_resp_msg *resp = (struct htt_resp_msg *)skb->data;
	enum htt_t2h_msg_type type;
	u16 peer_id;
	u8 vdev_id;
	u8 mac_addr[ETH_ALEN];
	u16 peer_mac_h16;
	u16 ast_hash = 0;
	u16 hw_peer_id;

	type = le32_get_bits(resp->version_msg.version, HTT_T2H_MSG_TYPE);

	ath12k_dbg(ab, ATH12K_DBG_DP_HTT, "dp_htt rx msg type :0x%0x\n", type);

	switch (type) {
	case HTT_T2H_MSG_TYPE_VERSION_CONF:
		dp->htt_tgt_ver_major = le32_get_bits(resp->version_msg.version,
						      HTT_T2H_VERSION_CONF_MAJOR);
		dp->htt_tgt_ver_minor = le32_get_bits(resp->version_msg.version,
						      HTT_T2H_VERSION_CONF_MINOR);
		complete(&dp->htt_tgt_version_received);
		break;
	/* TODO: remove unused peer map versions after testing */
	case HTT_T2H_MSG_TYPE_PEER_MAP:
		vdev_id = le32_get_bits(resp->peer_map_ev.info,
					HTT_T2H_PEER_MAP_INFO_VDEV_ID);
		peer_id = le32_get_bits(resp->peer_map_ev.info,
					HTT_T2H_PEER_MAP_INFO_PEER_ID);
		peer_mac_h16 = le32_get_bits(resp->peer_map_ev.info1,
					     HTT_T2H_PEER_MAP_INFO1_MAC_ADDR_H16);
		ath12k_dp_get_mac_addr(le32_to_cpu(resp->peer_map_ev.mac_addr_l32),
				       peer_mac_h16, mac_addr);
		ath12k_peer_map_event(ab, vdev_id, peer_id, mac_addr, 0, 0);
		break;
	case HTT_T2H_MSG_TYPE_PEER_MAP2:
		vdev_id = le32_get_bits(resp->peer_map_ev.info,
					HTT_T2H_PEER_MAP_INFO_VDEV_ID);
		peer_id = le32_get_bits(resp->peer_map_ev.info,
					HTT_T2H_PEER_MAP_INFO_PEER_ID);
		peer_mac_h16 = le32_get_bits(resp->peer_map_ev.info1,
					     HTT_T2H_PEER_MAP_INFO1_MAC_ADDR_H16);
		ath12k_dp_get_mac_addr(le32_to_cpu(resp->peer_map_ev.mac_addr_l32),
				       peer_mac_h16, mac_addr);
		ast_hash = le32_get_bits(resp->peer_map_ev.info2,
					 HTT_T2H_PEER_MAP_INFO2_AST_HASH_VAL);
		hw_peer_id = le32_get_bits(resp->peer_map_ev.info1,
					   HTT_T2H_PEER_MAP_INFO1_HW_PEER_ID);
		ath12k_peer_map_event(ab, vdev_id, peer_id, mac_addr, ast_hash,
				      hw_peer_id);
		break;
	case HTT_T2H_MSG_TYPE_PEER_MAP3:
		vdev_id = le32_get_bits(resp->peer_map_ev.info,
					HTT_T2H_PEER_MAP_INFO_VDEV_ID);
		peer_id = le32_get_bits(resp->peer_map_ev.info,
					HTT_T2H_PEER_MAP_INFO_PEER_ID);
		peer_mac_h16 = le32_get_bits(resp->peer_map_ev.info1,
					     HTT_T2H_PEER_MAP_INFO1_MAC_ADDR_H16);
		ath12k_dp_get_mac_addr(le32_to_cpu(resp->peer_map_ev.mac_addr_l32),
				       peer_mac_h16, mac_addr);
		ast_hash = le32_get_bits(resp->peer_map_ev.info2,
					 HTT_T2H_PEER_MAP3_INFO2_AST_HASH_VAL);
		hw_peer_id = le32_get_bits(resp->peer_map_ev.info2,
					   HTT_T2H_PEER_MAP3_INFO2_HW_PEER_ID);
		ath12k_peer_map_event(ab, vdev_id, peer_id, mac_addr, ast_hash,
				      hw_peer_id);
		break;
	case HTT_T2H_MSG_TYPE_PEER_UNMAP:
	case HTT_T2H_MSG_TYPE_PEER_UNMAP2:
		peer_id = le32_get_bits(resp->peer_unmap_ev.info,
					HTT_T2H_PEER_UNMAP_INFO_PEER_ID);
		ath12k_peer_unmap_event(ab, peer_id);
		break;
	case HTT_T2H_MSG_TYPE_PPDU_STATS_IND:
		ath12k_htt_pull_ppdu_stats(ab, skb);
		break;
	case HTT_T2H_MSG_TYPE_EXT_STATS_CONF:
		ath12k_debugfs_htt_ext_stats_handler(ab, skb);
		break;
	case HTT_T2H_MSG_TYPE_MLO_TIMESTAMP_OFFSET_IND:
		ath12k_htt_mlo_offset_event_handler(ab, skb);
		break;
	default:
		ath12k_dbg(ab, ATH12K_DBG_DP_HTT, "dp_htt event %d not handled\n",
			   type);
		break;
	}

	dev_kfree_skb_any(skb);
}

static int ath12k_dp_rx_msdu_coalesce(struct ath12k *ar,
				      struct sk_buff_head *msdu_list,
				      struct sk_buff *first, struct sk_buff *last,
				      u8 l3pad_bytes, int msdu_len)
{
	struct ath12k_base *ab = ar->ab;
	struct sk_buff *skb;
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(first);
	int buf_first_hdr_len, buf_first_len;
	struct hal_rx_desc *ldesc;
	int space_extra, rem_len, buf_len;
	u32 hal_rx_desc_sz = ar->ab->hal.hal_desc_sz;
	bool is_continuation;

	/* As the msdu is spread across multiple rx buffers,
	 * find the offset to the start of msdu for computing
	 * the length of the msdu in the first buffer.
	 */
	buf_first_hdr_len = hal_rx_desc_sz + l3pad_bytes;
	buf_first_len = DP_RX_BUFFER_SIZE - buf_first_hdr_len;

	if (WARN_ON_ONCE(msdu_len <= buf_first_len)) {
		skb_put(first, buf_first_hdr_len + msdu_len);
		skb_pull(first, buf_first_hdr_len);
		return 0;
	}

	ldesc = (struct hal_rx_desc *)last->data;
	rxcb->is_first_msdu = ath12k_dp_rx_h_first_msdu(ab, ldesc);
	rxcb->is_last_msdu = ath12k_dp_rx_h_last_msdu(ab, ldesc);

	/* MSDU spans over multiple buffers because the length of the MSDU
	 * exceeds DP_RX_BUFFER_SIZE - HAL_RX_DESC_SIZE. So assume the data
	 * in the first buf is of length DP_RX_BUFFER_SIZE - HAL_RX_DESC_SIZE.
	 */
	skb_put(first, DP_RX_BUFFER_SIZE);
	skb_pull(first, buf_first_hdr_len);

	/* When an MSDU spread over multiple buffers MSDU_END
	 * tlvs are valid only in the last buffer. Copy those tlvs.
	 */
	ath12k_dp_rx_desc_end_tlv_copy(ab, rxcb->rx_desc, ldesc);

	space_extra = msdu_len - (buf_first_len + skb_tailroom(first));
	if (space_extra > 0 &&
	    (pskb_expand_head(first, 0, space_extra, GFP_ATOMIC) < 0)) {
		/* Free up all buffers of the MSDU */
		while ((skb = __skb_dequeue(msdu_list)) != NULL) {
			rxcb = ATH12K_SKB_RXCB(skb);
			if (!rxcb->is_continuation) {
				dev_kfree_skb_any(skb);
				break;
			}
			dev_kfree_skb_any(skb);
		}
		return -ENOMEM;
	}

	rem_len = msdu_len - buf_first_len;
	while ((skb = __skb_dequeue(msdu_list)) != NULL && rem_len > 0) {
		rxcb = ATH12K_SKB_RXCB(skb);
		is_continuation = rxcb->is_continuation;
		if (is_continuation)
			buf_len = DP_RX_BUFFER_SIZE - hal_rx_desc_sz;
		else
			buf_len = rem_len;

		if (buf_len > (DP_RX_BUFFER_SIZE - hal_rx_desc_sz)) {
			WARN_ON_ONCE(1);
			dev_kfree_skb_any(skb);
			return -EINVAL;
		}

		skb_put(skb, buf_len + hal_rx_desc_sz);
		skb_pull(skb, hal_rx_desc_sz);
		skb_copy_from_linear_data(skb, skb_put(first, buf_len),
					  buf_len);
		dev_kfree_skb_any(skb);

		rem_len -= buf_len;
		if (!is_continuation)
			break;
	}

	return 0;
}

static struct sk_buff *ath12k_dp_rx_get_msdu_last_buf(struct sk_buff_head *msdu_list,
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

static void ath12k_dp_rx_h_csum_offload(struct sk_buff *msdu,
					struct ath12k_dp_rx_info *rx_info)
{
	msdu->ip_summed = (rx_info->ip_csum_fail || rx_info->l4_csum_fail) ?
			   CHECKSUM_NONE : CHECKSUM_UNNECESSARY;
}

int ath12k_dp_rx_crypto_mic_len(struct ath12k *ar, enum hal_encrypt_type enctype)
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

	ath12k_warn(ar->ab, "unsupported encryption type %d for mic len\n", enctype);
	return 0;
}

static int ath12k_dp_rx_crypto_param_len(struct ath12k *ar,
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

	ath12k_warn(ar->ab, "unsupported encryption type %d\n", enctype);
	return 0;
}

static int ath12k_dp_rx_crypto_icv_len(struct ath12k *ar,
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

	ath12k_warn(ar->ab, "unsupported encryption type %d\n", enctype);
	return 0;
}

static void ath12k_dp_rx_h_undecap_nwifi(struct ath12k *ar,
					 struct sk_buff *msdu,
					 enum hal_encrypt_type enctype,
					 struct ieee80211_rx_status *status)
{
	struct ath12k_base *ab = ar->ab;
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

	if (ath12k_dp_rx_h_mesh_ctl_present(ab, rxcb->rx_desc))
		qos_ctl |= IEEE80211_QOS_CTL_MESH_CONTROL_PRESENT;

	/* TODO: Add other QoS ctl fields when required */

	/* copy decap header before overwriting for reuse below */
	memcpy(decap_hdr, hdr, hdr_len);

	/* Rebuild crypto header for mac80211 use */
	if (!(status->flag & RX_FLAG_IV_STRIPPED)) {
		crypto_hdr = skb_push(msdu, ath12k_dp_rx_crypto_param_len(ar, enctype));
		ath12k_dp_rx_desc_get_crypto_header(ar->ab,
						    rxcb->rx_desc, crypto_hdr,
						    enctype);
	}

	memcpy(skb_push(msdu,
			IEEE80211_QOS_CTL_LEN), &qos_ctl,
			IEEE80211_QOS_CTL_LEN);
	memcpy(skb_push(msdu, hdr_len), decap_hdr, hdr_len);
}

static void ath12k_dp_rx_h_undecap_raw(struct ath12k *ar, struct sk_buff *msdu,
				       enum hal_encrypt_type enctype,
				       struct ieee80211_rx_status *status,
				       bool decrypted)
{
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
			 ath12k_dp_rx_crypto_mic_len(ar, enctype));

		skb_trim(msdu, msdu->len -
			 ath12k_dp_rx_crypto_icv_len(ar, enctype));
	} else {
		/* MIC */
		if (status->flag & RX_FLAG_MIC_STRIPPED)
			skb_trim(msdu, msdu->len -
				 ath12k_dp_rx_crypto_mic_len(ar, enctype));

		/* ICV */
		if (status->flag & RX_FLAG_ICV_STRIPPED)
			skb_trim(msdu, msdu->len -
				 ath12k_dp_rx_crypto_icv_len(ar, enctype));
	}

	/* MMIC */
	if ((status->flag & RX_FLAG_MMIC_STRIPPED) &&
	    !ieee80211_has_morefrags(hdr->frame_control) &&
	    enctype == HAL_ENCRYPT_TYPE_TKIP_MIC)
		skb_trim(msdu, msdu->len - IEEE80211_CCMP_MIC_LEN);

	/* Head */
	if (status->flag & RX_FLAG_IV_STRIPPED) {
		hdr_len = ieee80211_hdrlen(hdr->frame_control);
		crypto_len = ath12k_dp_rx_crypto_param_len(ar, enctype);

		memmove(msdu->data + crypto_len, msdu->data, hdr_len);
		skb_pull(msdu, crypto_len);
	}
}

static void ath12k_get_dot11_hdr_from_rx_desc(struct ath12k *ar,
					      struct sk_buff *msdu,
					      struct ath12k_skb_rxcb *rxcb,
					      struct ieee80211_rx_status *status,
					      enum hal_encrypt_type enctype)
{
	struct hal_rx_desc *rx_desc = rxcb->rx_desc;
	struct ath12k_base *ab = ar->ab;
	size_t hdr_len, crypto_len;
	struct ieee80211_hdr hdr;
	__le16 qos_ctl;
	u8 *crypto_hdr, mesh_ctrl;

	ath12k_dp_rx_desc_get_dot11_hdr(ab, rx_desc, &hdr);
	hdr_len = ieee80211_hdrlen(hdr.frame_control);
	mesh_ctrl = ath12k_dp_rx_h_mesh_ctl_present(ab, rx_desc);

	if (!(status->flag & RX_FLAG_IV_STRIPPED)) {
		crypto_len = ath12k_dp_rx_crypto_param_len(ar, enctype);
		crypto_hdr = skb_push(msdu, crypto_len);
		ath12k_dp_rx_desc_get_crypto_header(ab, rx_desc, crypto_hdr, enctype);
	}

	skb_push(msdu, hdr_len);
	memcpy(msdu->data, &hdr, min(hdr_len, sizeof(hdr)));

	if (rxcb->is_mcbc)
		status->flag &= ~RX_FLAG_PN_VALIDATED;

	/* Add QOS header */
	if (ieee80211_is_data_qos(hdr.frame_control)) {
		struct ieee80211_hdr *qos_ptr = (struct ieee80211_hdr *)msdu->data;

		qos_ctl = cpu_to_le16(rxcb->tid & IEEE80211_QOS_CTL_TID_MASK);
		if (mesh_ctrl)
			qos_ctl |= cpu_to_le16(IEEE80211_QOS_CTL_MESH_CONTROL_PRESENT);

		memcpy(ieee80211_get_qos_ctl(qos_ptr), &qos_ctl, IEEE80211_QOS_CTL_LEN);
	}
}

static void ath12k_dp_rx_h_undecap_eth(struct ath12k *ar,
				       struct sk_buff *msdu,
				       enum hal_encrypt_type enctype,
				       struct ieee80211_rx_status *status)
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
	ath12k_get_dot11_hdr_from_rx_desc(ar, msdu, rxcb, status, enctype);

	/* original 802.11 header has a different DA and in
	 * case of 4addr it may also have different SA
	 */
	hdr = (struct ieee80211_hdr *)msdu->data;
	ether_addr_copy(ieee80211_get_DA(hdr), da);
	ether_addr_copy(ieee80211_get_SA(hdr), sa);
}

static void ath12k_dp_rx_h_undecap(struct ath12k *ar, struct sk_buff *msdu,
				   struct hal_rx_desc *rx_desc,
				   enum hal_encrypt_type enctype,
				   struct ieee80211_rx_status *status,
				   bool decrypted)
{
	struct ath12k_base *ab = ar->ab;
	u8 decap;
	struct ethhdr *ehdr;

	decap = ath12k_dp_rx_h_decap_type(ab, rx_desc);

	switch (decap) {
	case DP_RX_DECAP_TYPE_NATIVE_WIFI:
		ath12k_dp_rx_h_undecap_nwifi(ar, msdu, enctype, status);
		break;
	case DP_RX_DECAP_TYPE_RAW:
		ath12k_dp_rx_h_undecap_raw(ar, msdu, enctype, status,
					   decrypted);
		break;
	case DP_RX_DECAP_TYPE_ETHERNET2_DIX:
		ehdr = (struct ethhdr *)msdu->data;

		/* mac80211 allows fast path only for authorized STA */
		if (ehdr->h_proto == cpu_to_be16(ETH_P_PAE)) {
			ATH12K_SKB_RXCB(msdu)->is_eapol = true;
			ath12k_dp_rx_h_undecap_eth(ar, msdu, enctype, status);
			break;
		}

		/* PN for mcast packets will be validated in mac80211;
		 * remove eth header and add 802.11 header.
		 */
		if (ATH12K_SKB_RXCB(msdu)->is_mcbc && decrypted)
			ath12k_dp_rx_h_undecap_eth(ar, msdu, enctype, status);
		break;
	case DP_RX_DECAP_TYPE_8023:
		/* TODO: Handle undecap for these formats */
		break;
	}
}

struct ath12k_peer *
ath12k_dp_rx_h_find_peer(struct ath12k_base *ab, struct sk_buff *msdu,
			 struct ath12k_dp_rx_info *rx_info)
{
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	struct ath12k_peer *peer = NULL;

	lockdep_assert_held(&ab->base_lock);

	if (rxcb->peer_id)
		peer = ath12k_peer_find_by_id(ab, rxcb->peer_id);

	if (peer)
		return peer;

	if (rx_info->addr2_present)
		peer = ath12k_peer_find_by_addr(ab, rx_info->addr2);

	return peer;
}

static void ath12k_dp_rx_h_mpdu(struct ath12k *ar,
				struct sk_buff *msdu,
				struct hal_rx_desc *rx_desc,
				struct ath12k_dp_rx_info *rx_info)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_skb_rxcb *rxcb;
	enum hal_encrypt_type enctype;
	bool is_decrypted = false;
	struct ieee80211_hdr *hdr;
	struct ath12k_peer *peer;
	struct ieee80211_rx_status *rx_status = rx_info->rx_status;
	u32 err_bitmap;

	/* PN for multicast packets will be checked in mac80211 */
	rxcb = ATH12K_SKB_RXCB(msdu);
	rxcb->is_mcbc = rx_info->is_mcbc;

	if (rxcb->is_mcbc)
		rxcb->peer_id = rx_info->peer_id;

	spin_lock_bh(&ar->ab->base_lock);
	peer = ath12k_dp_rx_h_find_peer(ar->ab, msdu, rx_info);
	if (peer) {
		/* resetting mcbc bit because mcbc packets are unicast
		 * packets only for AP as STA sends unicast packets.
		 */
		rxcb->is_mcbc = rxcb->is_mcbc && !peer->ucast_ra_only;

		if (rxcb->is_mcbc)
			enctype = peer->sec_type_grp;
		else
			enctype = peer->sec_type;
	} else {
		enctype = HAL_ENCRYPT_TYPE_OPEN;
	}
	spin_unlock_bh(&ar->ab->base_lock);

	err_bitmap = ath12k_dp_rx_h_mpdu_err(ab, rx_desc);
	if (enctype != HAL_ENCRYPT_TYPE_OPEN && !err_bitmap)
		is_decrypted = ath12k_dp_rx_h_is_decrypted(ab, rx_desc);

	/* Clear per-MPDU flags while leaving per-PPDU flags intact */
	rx_status->flag &= ~(RX_FLAG_FAILED_FCS_CRC |
			     RX_FLAG_MMIC_ERROR |
			     RX_FLAG_DECRYPTED |
			     RX_FLAG_IV_STRIPPED |
			     RX_FLAG_MMIC_STRIPPED);

	if (err_bitmap & HAL_RX_MPDU_ERR_FCS)
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
	if (err_bitmap & HAL_RX_MPDU_ERR_TKIP_MIC)
		rx_status->flag |= RX_FLAG_MMIC_ERROR;

	if (is_decrypted) {
		rx_status->flag |= RX_FLAG_DECRYPTED | RX_FLAG_MMIC_STRIPPED;

		if (rx_info->is_mcbc)
			rx_status->flag |= RX_FLAG_MIC_STRIPPED |
					RX_FLAG_ICV_STRIPPED;
		else
			rx_status->flag |= RX_FLAG_IV_STRIPPED |
					   RX_FLAG_PN_VALIDATED;
	}

	ath12k_dp_rx_h_csum_offload(msdu, rx_info);
	ath12k_dp_rx_h_undecap(ar, msdu, rx_desc,
			       enctype, rx_status, is_decrypted);

	if (!is_decrypted || rx_info->is_mcbc)
		return;

	if (rx_info->decap_type != DP_RX_DECAP_TYPE_ETHERNET2_DIX) {
		hdr = (void *)msdu->data;
		hdr->frame_control &= ~__cpu_to_le16(IEEE80211_FCTL_PROTECTED);
	}
}

static void ath12k_dp_rx_h_rate(struct ath12k *ar, struct ath12k_dp_rx_info *rx_info)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_rx_status *rx_status = rx_info->rx_status;
	enum rx_msdu_start_pkt_type pkt_type = rx_info->pkt_type;
	u8 bw = rx_info->bw, sgi = rx_info->sgi;
	u8 rate_mcs = rx_info->rate_mcs, nss = rx_info->nss;
	bool is_cck;

	switch (pkt_type) {
	case RX_MSDU_START_PKT_TYPE_11A:
	case RX_MSDU_START_PKT_TYPE_11B:
		is_cck = (pkt_type == RX_MSDU_START_PKT_TYPE_11B);
		sband = &ar->mac.sbands[rx_status->band];
		rx_status->rate_idx = ath12k_mac_hw_rate_to_idx(sband, rate_mcs,
								is_cck);
		break;
	case RX_MSDU_START_PKT_TYPE_11N:
		rx_status->encoding = RX_ENC_HT;
		if (rate_mcs > ATH12K_HT_MCS_MAX) {
			ath12k_warn(ar->ab,
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
			ath12k_warn(ar->ab,
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
			ath12k_warn(ar->ab,
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
			ath12k_warn(ar->ab,
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

void ath12k_dp_rx_h_fetch_info(struct ath12k_base *ab, struct hal_rx_desc *rx_desc,
			       struct ath12k_dp_rx_info *rx_info)
{
	rx_info->ip_csum_fail = ath12k_dp_rx_h_ip_cksum_fail(ab, rx_desc);
	rx_info->l4_csum_fail = ath12k_dp_rx_h_l4_cksum_fail(ab, rx_desc);
	rx_info->is_mcbc = ath12k_dp_rx_h_is_da_mcbc(ab, rx_desc);
	rx_info->decap_type = ath12k_dp_rx_h_decap_type(ab, rx_desc);
	rx_info->pkt_type = ath12k_dp_rx_h_pkt_type(ab, rx_desc);
	rx_info->sgi = ath12k_dp_rx_h_sgi(ab, rx_desc);
	rx_info->rate_mcs = ath12k_dp_rx_h_rate_mcs(ab, rx_desc);
	rx_info->bw = ath12k_dp_rx_h_rx_bw(ab, rx_desc);
	rx_info->nss = ath12k_dp_rx_h_nss(ab, rx_desc);
	rx_info->tid = ath12k_dp_rx_h_tid(ab, rx_desc);
	rx_info->peer_id = ath12k_dp_rx_h_peer_id(ab, rx_desc);
	rx_info->phy_meta_data = ath12k_dp_rx_h_freq(ab, rx_desc);

	if (ath12k_dp_rxdesc_mac_addr2_valid(ab, rx_desc)) {
		ether_addr_copy(rx_info->addr2,
				ath12k_dp_rxdesc_get_mpdu_start_addr2(ab, rx_desc));
		rx_info->addr2_present = true;
	}

	ath12k_dbg_dump(ab, ATH12K_DBG_DATA, NULL, "rx_desc: ",
			rx_desc, sizeof(*rx_desc));
}

void ath12k_dp_rx_h_ppdu(struct ath12k *ar, struct ath12k_dp_rx_info *rx_info)
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

	if (center_freq >= ATH12K_MIN_6GHZ_FREQ &&
	    center_freq <= ATH12K_MAX_6GHZ_FREQ) {
		rx_status->band = NL80211_BAND_6GHZ;
		rx_status->freq = center_freq;
	} else if (channel_num >= 1 && channel_num <= 14) {
		rx_status->band = NL80211_BAND_2GHZ;
	} else if (channel_num >= 36 && channel_num <= 173) {
		rx_status->band = NL80211_BAND_5GHZ;
	} else {
		spin_lock_bh(&ar->data_lock);
		channel = ar->rx_channel;
		if (channel) {
			rx_status->band = channel->band;
			channel_num =
				ieee80211_frequency_to_channel(channel->center_freq);
		}
		spin_unlock_bh(&ar->data_lock);
	}

	if (rx_status->band != NL80211_BAND_6GHZ)
		rx_status->freq = ieee80211_channel_to_frequency(channel_num,
								 rx_status->band);

	ath12k_dp_rx_h_rate(ar, rx_info);
}

static void ath12k_dp_rx_deliver_msdu(struct ath12k *ar, struct napi_struct *napi,
				      struct sk_buff *msdu,
				      struct ath12k_dp_rx_info *rx_info)
{
	struct ath12k_base *ab = ar->ab;
	struct ieee80211_rx_status *rx_status;
	struct ieee80211_sta *pubsta;
	struct ath12k_peer *peer;
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	struct ieee80211_rx_status *status = rx_info->rx_status;
	u8 decap = rx_info->decap_type;
	bool is_mcbc = rxcb->is_mcbc;
	bool is_eapol = rxcb->is_eapol;

	spin_lock_bh(&ab->base_lock);
	peer = ath12k_dp_rx_h_find_peer(ab, msdu, rx_info);

	pubsta = peer ? peer->sta : NULL;

	if (pubsta && pubsta->valid_links) {
		status->link_valid = 1;
		status->link_id = peer->link_id;
	}

	spin_unlock_bh(&ab->base_lock);

	ath12k_dbg(ab, ATH12K_DBG_DATA,
		   "rx skb %p len %u peer %pM %d %s sn %u %s%s%s%s%s%s%s%s%s%s rate_idx %u vht_nss %u freq %u band %u flag 0x%x fcs-err %i mic-err %i amsdu-more %i\n",
		   msdu,
		   msdu->len,
		   peer ? peer->addr : NULL,
		   rxcb->tid,
		   is_mcbc ? "mcast" : "ucast",
		   ath12k_dp_rx_h_seq_no(ab, rxcb->rx_desc),
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

	ath12k_dbg_dump(ab, ATH12K_DBG_DP_RX, NULL, "dp rx msdu: ",
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

	ieee80211_rx_napi(ath12k_ar_to_hw(ar), pubsta, msdu, napi);
}

static bool ath12k_dp_rx_check_nwifi_hdr_len_valid(struct ath12k_base *ab,
						   struct hal_rx_desc *rx_desc,
						   struct sk_buff *msdu)
{
	struct ieee80211_hdr *hdr;
	u8 decap_type;
	u32 hdr_len;

	decap_type = ath12k_dp_rx_h_decap_type(ab, rx_desc);
	if (decap_type != DP_RX_DECAP_TYPE_NATIVE_WIFI)
		return true;

	hdr = (struct ieee80211_hdr *)msdu->data;
	hdr_len = ieee80211_hdrlen(hdr->frame_control);

	if ((likely(hdr_len <= DP_MAX_NWIFI_HDR_LEN)))
		return true;

	ab->device_stats.invalid_rbm++;
	WARN_ON_ONCE(1);
	return false;
}

static int ath12k_dp_rx_process_msdu(struct ath12k *ar,
				     struct sk_buff *msdu,
				     struct sk_buff_head *msdu_list,
				     struct ath12k_dp_rx_info *rx_info)
{
	struct ath12k_base *ab = ar->ab;
	struct hal_rx_desc *rx_desc, *lrx_desc;
	struct ath12k_skb_rxcb *rxcb;
	struct sk_buff *last_buf;
	u8 l3_pad_bytes;
	u16 msdu_len;
	int ret;
	u32 hal_rx_desc_sz = ar->ab->hal.hal_desc_sz;

	last_buf = ath12k_dp_rx_get_msdu_last_buf(msdu_list, msdu);
	if (!last_buf) {
		ath12k_warn(ab,
			    "No valid Rx buffer to access MSDU_END tlv\n");
		ret = -EIO;
		goto free_out;
	}

	rx_desc = (struct hal_rx_desc *)msdu->data;
	lrx_desc = (struct hal_rx_desc *)last_buf->data;
	if (!ath12k_dp_rx_h_msdu_done(ab, lrx_desc)) {
		ath12k_warn(ab, "msdu_done bit in msdu_end is not set\n");
		ret = -EIO;
		goto free_out;
	}

	rxcb = ATH12K_SKB_RXCB(msdu);
	rxcb->rx_desc = rx_desc;
	msdu_len = ath12k_dp_rx_h_msdu_len(ab, lrx_desc);
	l3_pad_bytes = ath12k_dp_rx_h_l3pad(ab, lrx_desc);

	if (rxcb->is_frag) {
		skb_pull(msdu, hal_rx_desc_sz);
	} else if (!rxcb->is_continuation) {
		if ((msdu_len + hal_rx_desc_sz) > DP_RX_BUFFER_SIZE) {
			ret = -EINVAL;
			ath12k_warn(ab, "invalid msdu len %u\n", msdu_len);
			ath12k_dbg_dump(ab, ATH12K_DBG_DATA, NULL, "", rx_desc,
					sizeof(*rx_desc));
			goto free_out;
		}
		skb_put(msdu, hal_rx_desc_sz + l3_pad_bytes + msdu_len);
		skb_pull(msdu, hal_rx_desc_sz + l3_pad_bytes);
	} else {
		ret = ath12k_dp_rx_msdu_coalesce(ar, msdu_list,
						 msdu, last_buf,
						 l3_pad_bytes, msdu_len);
		if (ret) {
			ath12k_warn(ab,
				    "failed to coalesce msdu rx buffer%d\n", ret);
			goto free_out;
		}
	}

	if (unlikely(!ath12k_dp_rx_check_nwifi_hdr_len_valid(ab, rx_desc, msdu))) {
		ret = -EINVAL;
		goto free_out;
	}

	ath12k_dp_rx_h_fetch_info(ab, rx_desc, rx_info);
	ath12k_dp_rx_h_ppdu(ar, rx_info);
	ath12k_dp_rx_h_mpdu(ar, msdu, rx_desc, rx_info);

	rx_info->rx_status->flag |= RX_FLAG_SKIP_MONITOR | RX_FLAG_DUP_VALIDATED;

	return 0;

free_out:
	return ret;
}

static void ath12k_dp_rx_process_received_packets(struct ath12k_base *ab,
						  struct napi_struct *napi,
						  struct sk_buff_head *msdu_list,
						  int ring_id)
{
	struct ath12k_hw_group *ag = ab->ag;
	struct ieee80211_rx_status rx_status = {};
	struct ath12k_skb_rxcb *rxcb;
	struct sk_buff *msdu;
	struct ath12k *ar;
	struct ath12k_hw_link *hw_links = ag->hw_links;
	struct ath12k_base *partner_ab;
	struct ath12k_dp_rx_info rx_info;
	u8 hw_link_id, pdev_id;
	int ret;

	if (skb_queue_empty(msdu_list))
		return;

	rx_info.addr2_present = false;
	rx_info.rx_status = &rx_status;

	rcu_read_lock();

	while ((msdu = __skb_dequeue(msdu_list))) {
		rxcb = ATH12K_SKB_RXCB(msdu);
		hw_link_id = rxcb->hw_link_id;
		partner_ab = ath12k_ag_to_ab(ag,
					     hw_links[hw_link_id].device_id);
		pdev_id = ath12k_hw_mac_id_to_pdev_id(partner_ab->hw_params,
						      hw_links[hw_link_id].pdev_idx);
		ar = partner_ab->pdevs[pdev_id].ar;
		if (!rcu_dereference(partner_ab->pdevs_active[pdev_id])) {
			dev_kfree_skb_any(msdu);
			continue;
		}

		if (test_bit(ATH12K_FLAG_CAC_RUNNING, &ar->dev_flags)) {
			dev_kfree_skb_any(msdu);
			continue;
		}

		ret = ath12k_dp_rx_process_msdu(ar, msdu, msdu_list, &rx_info);
		if (ret) {
			ath12k_dbg(ab, ATH12K_DBG_DATA,
				   "Unable to process msdu %d", ret);
			dev_kfree_skb_any(msdu);
			continue;
		}

		ath12k_dp_rx_deliver_msdu(ar, napi, msdu, &rx_info);
	}

	rcu_read_unlock();
}

static u16 ath12k_dp_rx_get_peer_id(struct ath12k_base *ab,
				    enum ath12k_peer_metadata_version ver,
				    __le32 peer_metadata)
{
	switch (ver) {
	default:
		ath12k_warn(ab, "Unknown peer metadata version: %d", ver);
		fallthrough;
	case ATH12K_PEER_METADATA_V0:
		return le32_get_bits(peer_metadata,
				     RX_MPDU_DESC_META_DATA_V0_PEER_ID);
	case ATH12K_PEER_METADATA_V1:
		return le32_get_bits(peer_metadata,
				     RX_MPDU_DESC_META_DATA_V1_PEER_ID);
	case ATH12K_PEER_METADATA_V1A:
		return le32_get_bits(peer_metadata,
				     RX_MPDU_DESC_META_DATA_V1A_PEER_ID);
	case ATH12K_PEER_METADATA_V1B:
		return le32_get_bits(peer_metadata,
				     RX_MPDU_DESC_META_DATA_V1B_PEER_ID);
	}
}

int ath12k_dp_rx_process(struct ath12k_base *ab, int ring_id,
			 struct napi_struct *napi, int budget)
{
	struct ath12k_hw_group *ag = ab->ag;
	struct list_head rx_desc_used_list[ATH12K_MAX_DEVICES];
	struct ath12k_hw_link *hw_links = ag->hw_links;
	int num_buffs_reaped[ATH12K_MAX_DEVICES] = {};
	struct ath12k_rx_desc_info *desc_info;
	struct ath12k_dp *dp = &ab->dp;
	struct dp_rxdma_ring *rx_ring = &dp->rx_refill_buf_ring;
	struct hal_reo_dest_ring *desc;
	struct ath12k_base *partner_ab;
	struct sk_buff_head msdu_list;
	struct ath12k_skb_rxcb *rxcb;
	int total_msdu_reaped = 0;
	u8 hw_link_id, device_id;
	struct hal_srng *srng;
	struct sk_buff *msdu;
	bool done = false;
	u64 desc_va;

	__skb_queue_head_init(&msdu_list);

	for (device_id = 0; device_id < ATH12K_MAX_DEVICES; device_id++)
		INIT_LIST_HEAD(&rx_desc_used_list[device_id]);

	srng = &ab->hal.srng_list[dp->reo_dst_ring[ring_id].ring_id];

	spin_lock_bh(&srng->lock);

try_again:
	ath12k_hal_srng_access_begin(ab, srng);

	while ((desc = ath12k_hal_srng_dst_get_next_entry(ab, srng))) {
		struct rx_mpdu_desc *mpdu_info;
		struct rx_msdu_desc *msdu_info;
		enum hal_reo_dest_ring_push_reason push_reason;
		u32 cookie;

		cookie = le32_get_bits(desc->buf_addr_info.info1,
				       BUFFER_ADDR_INFO1_SW_COOKIE);

		hw_link_id = le32_get_bits(desc->info0,
					   HAL_REO_DEST_RING_INFO0_SRC_LINK_ID);

		desc_va = ((u64)le32_to_cpu(desc->buf_va_hi) << 32 |
			   le32_to_cpu(desc->buf_va_lo));
		desc_info = (struct ath12k_rx_desc_info *)((unsigned long)desc_va);

		device_id = hw_links[hw_link_id].device_id;
		partner_ab = ath12k_ag_to_ab(ag, device_id);
		if (unlikely(!partner_ab)) {
			if (desc_info->skb) {
				dev_kfree_skb_any(desc_info->skb);
				desc_info->skb = NULL;
			}

			continue;
		}

		/* retry manual desc retrieval */
		if (!desc_info) {
			desc_info = ath12k_dp_get_rx_desc(partner_ab, cookie);
			if (!desc_info) {
				ath12k_warn(partner_ab, "Invalid cookie in manual descriptor retrieval: 0x%x\n",
					    cookie);
				continue;
			}
		}

		if (desc_info->magic != ATH12K_DP_RX_DESC_MAGIC)
			ath12k_warn(ab, "Check HW CC implementation");

		msdu = desc_info->skb;
		desc_info->skb = NULL;

		list_add_tail(&desc_info->list, &rx_desc_used_list[device_id]);

		rxcb = ATH12K_SKB_RXCB(msdu);
		dma_unmap_single(partner_ab->dev, rxcb->paddr,
				 msdu->len + skb_tailroom(msdu),
				 DMA_FROM_DEVICE);

		num_buffs_reaped[device_id]++;
		ab->device_stats.reo_rx[ring_id][ab->device_id]++;

		push_reason = le32_get_bits(desc->info0,
					    HAL_REO_DEST_RING_INFO0_PUSH_REASON);
		if (push_reason !=
		    HAL_REO_DEST_RING_PUSH_REASON_ROUTING_INSTRUCTION) {
			dev_kfree_skb_any(msdu);
			ab->device_stats.hal_reo_error[ring_id]++;
			continue;
		}

		msdu_info = &desc->rx_msdu_info;
		mpdu_info = &desc->rx_mpdu_info;

		rxcb->is_first_msdu = !!(le32_to_cpu(msdu_info->info0) &
					 RX_MSDU_DESC_INFO0_FIRST_MSDU_IN_MPDU);
		rxcb->is_last_msdu = !!(le32_to_cpu(msdu_info->info0) &
					RX_MSDU_DESC_INFO0_LAST_MSDU_IN_MPDU);
		rxcb->is_continuation = !!(le32_to_cpu(msdu_info->info0) &
					   RX_MSDU_DESC_INFO0_MSDU_CONTINUATION);
		rxcb->hw_link_id = hw_link_id;
		rxcb->peer_id = ath12k_dp_rx_get_peer_id(ab, dp->peer_metadata_ver,
							 mpdu_info->peer_meta_data);
		rxcb->tid = le32_get_bits(mpdu_info->info0,
					  RX_MPDU_DESC_INFO0_TID);

		__skb_queue_tail(&msdu_list, msdu);

		if (!rxcb->is_continuation) {
			total_msdu_reaped++;
			done = true;
		} else {
			done = false;
		}

		if (total_msdu_reaped >= budget)
			break;
	}

	/* Hw might have updated the head pointer after we cached it.
	 * In this case, even though there are entries in the ring we'll
	 * get rx_desc NULL. Give the read another try with updated cached
	 * head pointer so that we can reap complete MPDU in the current
	 * rx processing.
	 */
	if (!done && ath12k_hal_srng_dst_num_free(ab, srng, true)) {
		ath12k_hal_srng_access_end(ab, srng);
		goto try_again;
	}

	ath12k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	if (!total_msdu_reaped)
		goto exit;

	for (device_id = 0; device_id < ATH12K_MAX_DEVICES; device_id++) {
		if (!num_buffs_reaped[device_id])
			continue;

		partner_ab = ath12k_ag_to_ab(ag, device_id);
		rx_ring = &partner_ab->dp.rx_refill_buf_ring;

		ath12k_dp_rx_bufs_replenish(partner_ab, rx_ring,
					    &rx_desc_used_list[device_id],
					    num_buffs_reaped[device_id]);
	}

	ath12k_dp_rx_process_received_packets(ab, napi, &msdu_list,
					      ring_id);

exit:
	return total_msdu_reaped;
}

static void ath12k_dp_rx_frag_timer(struct timer_list *timer)
{
	struct ath12k_dp_rx_tid *rx_tid = timer_container_of(rx_tid, timer,
							     frag_timer);

	spin_lock_bh(&rx_tid->ab->base_lock);
	if (rx_tid->last_frag_no &&
	    rx_tid->rx_frag_bitmap == GENMASK(rx_tid->last_frag_no, 0)) {
		spin_unlock_bh(&rx_tid->ab->base_lock);
		return;
	}
	ath12k_dp_rx_frags_cleanup(rx_tid, true);
	spin_unlock_bh(&rx_tid->ab->base_lock);
}

int ath12k_dp_rx_peer_frag_setup(struct ath12k *ar, const u8 *peer_mac, int vdev_id)
{
	struct ath12k_base *ab = ar->ab;
	struct crypto_shash *tfm;
	struct ath12k_peer *peer;
	struct ath12k_dp_rx_tid *rx_tid;
	int i;

	tfm = crypto_alloc_shash("michael_mic", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	spin_lock_bh(&ab->base_lock);

	peer = ath12k_peer_find(ab, vdev_id, peer_mac);
	if (!peer) {
		spin_unlock_bh(&ab->base_lock);
		crypto_free_shash(tfm);
		ath12k_warn(ab, "failed to find the peer to set up fragment info\n");
		return -ENOENT;
	}

	if (!peer->primary_link) {
		spin_unlock_bh(&ab->base_lock);
		crypto_free_shash(tfm);
		return 0;
	}

	for (i = 0; i <= IEEE80211_NUM_TIDS; i++) {
		rx_tid = &peer->rx_tid[i];
		rx_tid->ab = ab;
		timer_setup(&rx_tid->frag_timer, ath12k_dp_rx_frag_timer, 0);
		skb_queue_head_init(&rx_tid->rx_frags);
	}

	peer->tfm_mmic = tfm;
	peer->dp_setup_done = true;
	spin_unlock_bh(&ab->base_lock);

	return 0;
}

static int ath12k_dp_rx_h_michael_mic(struct crypto_shash *tfm, u8 *key,
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

static int ath12k_dp_rx_h_verify_tkip_mic(struct ath12k *ar, struct ath12k_peer *peer,
					  struct sk_buff *msdu)
{
	struct ath12k_base *ab = ar->ab;
	struct hal_rx_desc *rx_desc = (struct hal_rx_desc *)msdu->data;
	struct ieee80211_rx_status *rxs = IEEE80211_SKB_RXCB(msdu);
	struct ieee80211_key_conf *key_conf;
	struct ieee80211_hdr *hdr;
	struct ath12k_dp_rx_info rx_info;
	u8 mic[IEEE80211_CCMP_MIC_LEN];
	int head_len, tail_len, ret;
	size_t data_len;
	u32 hdr_len, hal_rx_desc_sz = ar->ab->hal.hal_desc_sz;
	u8 *key, *data;
	u8 key_idx;

	if (ath12k_dp_rx_h_enctype(ab, rx_desc) != HAL_ENCRYPT_TYPE_TKIP_MIC)
		return 0;

	rx_info.addr2_present = false;
	rx_info.rx_status = rxs;

	hdr = (struct ieee80211_hdr *)(msdu->data + hal_rx_desc_sz);
	hdr_len = ieee80211_hdrlen(hdr->frame_control);
	head_len = hdr_len + hal_rx_desc_sz + IEEE80211_TKIP_IV_LEN;
	tail_len = IEEE80211_CCMP_MIC_LEN + IEEE80211_TKIP_ICV_LEN + FCS_LEN;

	if (!is_multicast_ether_addr(hdr->addr1))
		key_idx = peer->ucast_keyidx;
	else
		key_idx = peer->mcast_keyidx;

	key_conf = peer->keys[key_idx];

	data = msdu->data + head_len;
	data_len = msdu->len - head_len - tail_len;
	key = &key_conf->key[NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY];

	ret = ath12k_dp_rx_h_michael_mic(peer->tfm_mmic, key, hdr, data, data_len, mic);
	if (ret || memcmp(mic, data + data_len, IEEE80211_CCMP_MIC_LEN))
		goto mic_fail;

	return 0;

mic_fail:
	(ATH12K_SKB_RXCB(msdu))->is_first_msdu = true;
	(ATH12K_SKB_RXCB(msdu))->is_last_msdu = true;

	ath12k_dp_rx_h_fetch_info(ab, rx_desc, &rx_info);

	rxs->flag |= RX_FLAG_MMIC_ERROR | RX_FLAG_MMIC_STRIPPED |
		    RX_FLAG_IV_STRIPPED | RX_FLAG_DECRYPTED;
	skb_pull(msdu, hal_rx_desc_sz);

	if (unlikely(!ath12k_dp_rx_check_nwifi_hdr_len_valid(ab, rx_desc, msdu)))
		return -EINVAL;

	ath12k_dp_rx_h_ppdu(ar, &rx_info);
	ath12k_dp_rx_h_undecap(ar, msdu, rx_desc,
			       HAL_ENCRYPT_TYPE_TKIP_MIC, rxs, true);
	ieee80211_rx(ath12k_ar_to_hw(ar), msdu);
	return -EINVAL;
}

static void ath12k_dp_rx_h_undecap_frag(struct ath12k *ar, struct sk_buff *msdu,
					enum hal_encrypt_type enctype, u32 flags)
{
	struct ieee80211_hdr *hdr;
	size_t hdr_len;
	size_t crypto_len;
	u32 hal_rx_desc_sz = ar->ab->hal.hal_desc_sz;

	if (!flags)
		return;

	hdr = (struct ieee80211_hdr *)(msdu->data + hal_rx_desc_sz);

	if (flags & RX_FLAG_MIC_STRIPPED)
		skb_trim(msdu, msdu->len -
			 ath12k_dp_rx_crypto_mic_len(ar, enctype));

	if (flags & RX_FLAG_ICV_STRIPPED)
		skb_trim(msdu, msdu->len -
			 ath12k_dp_rx_crypto_icv_len(ar, enctype));

	if (flags & RX_FLAG_IV_STRIPPED) {
		hdr_len = ieee80211_hdrlen(hdr->frame_control);
		crypto_len = ath12k_dp_rx_crypto_param_len(ar, enctype);

		memmove(msdu->data + hal_rx_desc_sz + crypto_len,
			msdu->data + hal_rx_desc_sz, hdr_len);
		skb_pull(msdu, crypto_len);
	}
}

static int ath12k_dp_rx_h_defrag(struct ath12k *ar,
				 struct ath12k_peer *peer,
				 struct ath12k_dp_rx_tid *rx_tid,
				 struct sk_buff **defrag_skb)
{
	struct ath12k_base *ab = ar->ab;
	struct hal_rx_desc *rx_desc;
	struct sk_buff *skb, *first_frag, *last_frag;
	struct ieee80211_hdr *hdr;
	enum hal_encrypt_type enctype;
	bool is_decrypted = false;
	int msdu_len = 0;
	int extra_space;
	u32 flags, hal_rx_desc_sz = ar->ab->hal.hal_desc_sz;

	first_frag = skb_peek(&rx_tid->rx_frags);
	last_frag = skb_peek_tail(&rx_tid->rx_frags);

	skb_queue_walk(&rx_tid->rx_frags, skb) {
		flags = 0;
		rx_desc = (struct hal_rx_desc *)skb->data;
		hdr = (struct ieee80211_hdr *)(skb->data + hal_rx_desc_sz);

		enctype = ath12k_dp_rx_h_enctype(ab, rx_desc);
		if (enctype != HAL_ENCRYPT_TYPE_OPEN)
			is_decrypted = ath12k_dp_rx_h_is_decrypted(ab,
								   rx_desc);

		if (is_decrypted) {
			if (skb != first_frag)
				flags |= RX_FLAG_IV_STRIPPED;
			if (skb != last_frag)
				flags |= RX_FLAG_ICV_STRIPPED |
					 RX_FLAG_MIC_STRIPPED;
		}

		/* RX fragments are always raw packets */
		if (skb != last_frag)
			skb_trim(skb, skb->len - FCS_LEN);
		ath12k_dp_rx_h_undecap_frag(ar, skb, enctype, flags);

		if (skb != first_frag)
			skb_pull(skb, hal_rx_desc_sz +
				      ieee80211_hdrlen(hdr->frame_control));
		msdu_len += skb->len;
	}

	extra_space = msdu_len - (DP_RX_BUFFER_SIZE + skb_tailroom(first_frag));
	if (extra_space > 0 &&
	    (pskb_expand_head(first_frag, 0, extra_space, GFP_ATOMIC) < 0))
		return -ENOMEM;

	__skb_unlink(first_frag, &rx_tid->rx_frags);
	while ((skb = __skb_dequeue(&rx_tid->rx_frags))) {
		skb_put_data(first_frag, skb->data, skb->len);
		dev_kfree_skb_any(skb);
	}

	hdr = (struct ieee80211_hdr *)(first_frag->data + hal_rx_desc_sz);
	hdr->frame_control &= ~__cpu_to_le16(IEEE80211_FCTL_MOREFRAGS);
	ATH12K_SKB_RXCB(first_frag)->is_frag = 1;

	if (ath12k_dp_rx_h_verify_tkip_mic(ar, peer, first_frag))
		first_frag = NULL;

	*defrag_skb = first_frag;
	return 0;
}

static int ath12k_dp_rx_h_defrag_reo_reinject(struct ath12k *ar,
					      struct ath12k_dp_rx_tid *rx_tid,
					      struct sk_buff *defrag_skb)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = &ab->dp;
	struct hal_rx_desc *rx_desc = (struct hal_rx_desc *)defrag_skb->data;
	struct hal_reo_entrance_ring *reo_ent_ring;
	struct hal_reo_dest_ring *reo_dest_ring;
	struct dp_link_desc_bank *link_desc_banks;
	struct hal_rx_msdu_link *msdu_link;
	struct hal_rx_msdu_details *msdu0;
	struct hal_srng *srng;
	dma_addr_t link_paddr, buf_paddr;
	u32 desc_bank, msdu_info, msdu_ext_info, mpdu_info;
	u32 cookie, hal_rx_desc_sz, dest_ring_info0, queue_addr_hi;
	int ret;
	struct ath12k_rx_desc_info *desc_info;
	enum hal_rx_buf_return_buf_manager idle_link_rbm = dp->idle_link_rbm;
	u8 dst_ind;

	hal_rx_desc_sz = ab->hal.hal_desc_sz;
	link_desc_banks = dp->link_desc_banks;
	reo_dest_ring = rx_tid->dst_ring_desc;

	ath12k_hal_rx_reo_ent_paddr_get(ab, &reo_dest_ring->buf_addr_info,
					&link_paddr, &cookie);
	desc_bank = u32_get_bits(cookie, DP_LINK_DESC_BANK_MASK);

	msdu_link = (struct hal_rx_msdu_link *)(link_desc_banks[desc_bank].vaddr +
			(link_paddr - link_desc_banks[desc_bank].paddr));
	msdu0 = &msdu_link->msdu_link[0];
	msdu_ext_info = le32_to_cpu(msdu0->rx_msdu_ext_info.info0);
	dst_ind = u32_get_bits(msdu_ext_info, RX_MSDU_EXT_DESC_INFO0_REO_DEST_IND);

	memset(msdu0, 0, sizeof(*msdu0));

	msdu_info = u32_encode_bits(1, RX_MSDU_DESC_INFO0_FIRST_MSDU_IN_MPDU) |
		    u32_encode_bits(1, RX_MSDU_DESC_INFO0_LAST_MSDU_IN_MPDU) |
		    u32_encode_bits(0, RX_MSDU_DESC_INFO0_MSDU_CONTINUATION) |
		    u32_encode_bits(defrag_skb->len - hal_rx_desc_sz,
				    RX_MSDU_DESC_INFO0_MSDU_LENGTH) |
		    u32_encode_bits(1, RX_MSDU_DESC_INFO0_VALID_SA) |
		    u32_encode_bits(1, RX_MSDU_DESC_INFO0_VALID_DA);
	msdu0->rx_msdu_info.info0 = cpu_to_le32(msdu_info);
	msdu0->rx_msdu_ext_info.info0 = cpu_to_le32(msdu_ext_info);

	/* change msdu len in hal rx desc */
	ath12k_dp_rxdesc_set_msdu_len(ab, rx_desc, defrag_skb->len - hal_rx_desc_sz);

	buf_paddr = dma_map_single(ab->dev, defrag_skb->data,
				   defrag_skb->len + skb_tailroom(defrag_skb),
				   DMA_TO_DEVICE);
	if (dma_mapping_error(ab->dev, buf_paddr))
		return -ENOMEM;

	spin_lock_bh(&dp->rx_desc_lock);
	desc_info = list_first_entry_or_null(&dp->rx_desc_free_list,
					     struct ath12k_rx_desc_info,
					     list);
	if (!desc_info) {
		spin_unlock_bh(&dp->rx_desc_lock);
		ath12k_warn(ab, "failed to find rx desc for reinject\n");
		ret = -ENOMEM;
		goto err_unmap_dma;
	}

	desc_info->skb = defrag_skb;
	desc_info->in_use = true;

	list_del(&desc_info->list);
	spin_unlock_bh(&dp->rx_desc_lock);

	ATH12K_SKB_RXCB(defrag_skb)->paddr = buf_paddr;

	ath12k_hal_rx_buf_addr_info_set(&msdu0->buf_addr_info, buf_paddr,
					desc_info->cookie,
					HAL_RX_BUF_RBM_SW3_BM);

	/* Fill mpdu details into reo entrance ring */
	srng = &ab->hal.srng_list[dp->reo_reinject_ring.ring_id];

	spin_lock_bh(&srng->lock);
	ath12k_hal_srng_access_begin(ab, srng);

	reo_ent_ring = ath12k_hal_srng_src_get_next_entry(ab, srng);
	if (!reo_ent_ring) {
		ath12k_hal_srng_access_end(ab, srng);
		spin_unlock_bh(&srng->lock);
		ret = -ENOSPC;
		goto err_free_desc;
	}
	memset(reo_ent_ring, 0, sizeof(*reo_ent_ring));

	ath12k_hal_rx_buf_addr_info_set(&reo_ent_ring->buf_addr_info, link_paddr,
					cookie,
					idle_link_rbm);

	mpdu_info = u32_encode_bits(1, RX_MPDU_DESC_INFO0_MSDU_COUNT) |
		    u32_encode_bits(0, RX_MPDU_DESC_INFO0_FRAG_FLAG) |
		    u32_encode_bits(1, RX_MPDU_DESC_INFO0_RAW_MPDU) |
		    u32_encode_bits(1, RX_MPDU_DESC_INFO0_VALID_PN) |
		    u32_encode_bits(rx_tid->tid, RX_MPDU_DESC_INFO0_TID);

	reo_ent_ring->rx_mpdu_info.info0 = cpu_to_le32(mpdu_info);
	reo_ent_ring->rx_mpdu_info.peer_meta_data =
		reo_dest_ring->rx_mpdu_info.peer_meta_data;

	if (ab->hw_params->reoq_lut_support) {
		reo_ent_ring->queue_addr_lo = reo_dest_ring->rx_mpdu_info.peer_meta_data;
		queue_addr_hi = 0;
	} else {
		reo_ent_ring->queue_addr_lo =
				cpu_to_le32(lower_32_bits(rx_tid->qbuf.paddr_aligned));
		queue_addr_hi = upper_32_bits(rx_tid->qbuf.paddr_aligned);
	}

	reo_ent_ring->info0 = le32_encode_bits(queue_addr_hi,
					       HAL_REO_ENTR_RING_INFO0_QUEUE_ADDR_HI) |
			      le32_encode_bits(dst_ind,
					       HAL_REO_ENTR_RING_INFO0_DEST_IND);

	reo_ent_ring->info1 = le32_encode_bits(rx_tid->cur_sn,
					       HAL_REO_ENTR_RING_INFO1_MPDU_SEQ_NUM);
	dest_ring_info0 = le32_get_bits(reo_dest_ring->info0,
					HAL_REO_DEST_RING_INFO0_SRC_LINK_ID);
	reo_ent_ring->info2 =
		cpu_to_le32(u32_get_bits(dest_ring_info0,
					 HAL_REO_ENTR_RING_INFO2_SRC_LINK_ID));

	ath12k_hal_srng_access_end(ab, srng);
	spin_unlock_bh(&srng->lock);

	return 0;

err_free_desc:
	spin_lock_bh(&dp->rx_desc_lock);
	desc_info->in_use = false;
	desc_info->skb = NULL;
	list_add_tail(&desc_info->list, &dp->rx_desc_free_list);
	spin_unlock_bh(&dp->rx_desc_lock);
err_unmap_dma:
	dma_unmap_single(ab->dev, buf_paddr, defrag_skb->len + skb_tailroom(defrag_skb),
			 DMA_TO_DEVICE);
	return ret;
}

static int ath12k_dp_rx_h_cmp_frags(struct ath12k_base *ab,
				    struct sk_buff *a, struct sk_buff *b)
{
	int frag1, frag2;

	frag1 = ath12k_dp_rx_h_frag_no(ab, a);
	frag2 = ath12k_dp_rx_h_frag_no(ab, b);

	return frag1 - frag2;
}

static void ath12k_dp_rx_h_sort_frags(struct ath12k_base *ab,
				      struct sk_buff_head *frag_list,
				      struct sk_buff *cur_frag)
{
	struct sk_buff *skb;
	int cmp;

	skb_queue_walk(frag_list, skb) {
		cmp = ath12k_dp_rx_h_cmp_frags(ab, skb, cur_frag);
		if (cmp < 0)
			continue;
		__skb_queue_before(frag_list, skb, cur_frag);
		return;
	}
	__skb_queue_tail(frag_list, cur_frag);
}

static u64 ath12k_dp_rx_h_get_pn(struct ath12k *ar, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;
	u64 pn = 0;
	u8 *ehdr;
	u32 hal_rx_desc_sz = ar->ab->hal.hal_desc_sz;

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

static bool
ath12k_dp_rx_h_defrag_validate_incr_pn(struct ath12k *ar, struct ath12k_dp_rx_tid *rx_tid)
{
	struct ath12k_base *ab = ar->ab;
	enum hal_encrypt_type encrypt_type;
	struct sk_buff *first_frag, *skb;
	struct hal_rx_desc *desc;
	u64 last_pn;
	u64 cur_pn;

	first_frag = skb_peek(&rx_tid->rx_frags);
	desc = (struct hal_rx_desc *)first_frag->data;

	encrypt_type = ath12k_dp_rx_h_enctype(ab, desc);
	if (encrypt_type != HAL_ENCRYPT_TYPE_CCMP_128 &&
	    encrypt_type != HAL_ENCRYPT_TYPE_CCMP_256 &&
	    encrypt_type != HAL_ENCRYPT_TYPE_GCMP_128 &&
	    encrypt_type != HAL_ENCRYPT_TYPE_AES_GCMP_256)
		return true;

	last_pn = ath12k_dp_rx_h_get_pn(ar, first_frag);
	skb_queue_walk(&rx_tid->rx_frags, skb) {
		if (skb == first_frag)
			continue;

		cur_pn = ath12k_dp_rx_h_get_pn(ar, skb);
		if (cur_pn != last_pn + 1)
			return false;
		last_pn = cur_pn;
	}
	return true;
}

static int ath12k_dp_rx_frag_h_mpdu(struct ath12k *ar,
				    struct sk_buff *msdu,
				    struct hal_reo_dest_ring *ring_desc)
{
	struct ath12k_base *ab = ar->ab;
	struct hal_rx_desc *rx_desc;
	struct ath12k_peer *peer;
	struct ath12k_dp_rx_tid *rx_tid;
	struct sk_buff *defrag_skb = NULL;
	u32 peer_id;
	u16 seqno, frag_no;
	u8 tid;
	int ret = 0;
	bool more_frags;

	rx_desc = (struct hal_rx_desc *)msdu->data;
	peer_id = ath12k_dp_rx_h_peer_id(ab, rx_desc);
	tid = ath12k_dp_rx_h_tid(ab, rx_desc);
	seqno = ath12k_dp_rx_h_seq_no(ab, rx_desc);
	frag_no = ath12k_dp_rx_h_frag_no(ab, msdu);
	more_frags = ath12k_dp_rx_h_more_frags(ab, msdu);

	if (!ath12k_dp_rx_h_seq_ctrl_valid(ab, rx_desc) ||
	    !ath12k_dp_rx_h_fc_valid(ab, rx_desc) ||
	    tid > IEEE80211_NUM_TIDS)
		return -EINVAL;

	/* received unfragmented packet in reo
	 * exception ring, this shouldn't happen
	 * as these packets typically come from
	 * reo2sw srngs.
	 */
	if (WARN_ON_ONCE(!frag_no && !more_frags))
		return -EINVAL;

	spin_lock_bh(&ab->base_lock);
	peer = ath12k_peer_find_by_id(ab, peer_id);
	if (!peer) {
		ath12k_warn(ab, "failed to find the peer to de-fragment received fragment peer_id %d\n",
			    peer_id);
		ret = -ENOENT;
		goto out_unlock;
	}

	if (!peer->dp_setup_done) {
		ath12k_warn(ab, "The peer %pM [%d] has uninitialized datapath\n",
			    peer->addr, peer_id);
		ret = -ENOENT;
		goto out_unlock;
	}

	rx_tid = &peer->rx_tid[tid];

	if ((!skb_queue_empty(&rx_tid->rx_frags) && seqno != rx_tid->cur_sn) ||
	    skb_queue_empty(&rx_tid->rx_frags)) {
		/* Flush stored fragments and start a new sequence */
		ath12k_dp_rx_frags_cleanup(rx_tid, true);
		rx_tid->cur_sn = seqno;
	}

	if (rx_tid->rx_frag_bitmap & BIT(frag_no)) {
		/* Fragment already present */
		ret = -EINVAL;
		goto out_unlock;
	}

	if ((!rx_tid->rx_frag_bitmap || frag_no > __fls(rx_tid->rx_frag_bitmap)))
		__skb_queue_tail(&rx_tid->rx_frags, msdu);
	else
		ath12k_dp_rx_h_sort_frags(ab, &rx_tid->rx_frags, msdu);

	rx_tid->rx_frag_bitmap |= BIT(frag_no);
	if (!more_frags)
		rx_tid->last_frag_no = frag_no;

	if (frag_no == 0) {
		rx_tid->dst_ring_desc = kmemdup(ring_desc,
						sizeof(*rx_tid->dst_ring_desc),
						GFP_ATOMIC);
		if (!rx_tid->dst_ring_desc) {
			ret = -ENOMEM;
			goto out_unlock;
		}
	} else {
		ath12k_dp_rx_link_desc_return(ab, &ring_desc->buf_addr_info,
					      HAL_WBM_REL_BM_ACT_PUT_IN_IDLE);
	}

	if (!rx_tid->last_frag_no ||
	    rx_tid->rx_frag_bitmap != GENMASK(rx_tid->last_frag_no, 0)) {
		mod_timer(&rx_tid->frag_timer, jiffies +
					       ATH12K_DP_RX_FRAGMENT_TIMEOUT_MS);
		goto out_unlock;
	}

	spin_unlock_bh(&ab->base_lock);
	timer_delete_sync(&rx_tid->frag_timer);
	spin_lock_bh(&ab->base_lock);

	peer = ath12k_peer_find_by_id(ab, peer_id);
	if (!peer)
		goto err_frags_cleanup;

	if (!ath12k_dp_rx_h_defrag_validate_incr_pn(ar, rx_tid))
		goto err_frags_cleanup;

	if (ath12k_dp_rx_h_defrag(ar, peer, rx_tid, &defrag_skb))
		goto err_frags_cleanup;

	if (!defrag_skb)
		goto err_frags_cleanup;

	if (ath12k_dp_rx_h_defrag_reo_reinject(ar, rx_tid, defrag_skb))
		goto err_frags_cleanup;

	ath12k_dp_rx_frags_cleanup(rx_tid, false);
	goto out_unlock;

err_frags_cleanup:
	dev_kfree_skb_any(defrag_skb);
	ath12k_dp_rx_frags_cleanup(rx_tid, true);
out_unlock:
	spin_unlock_bh(&ab->base_lock);
	return ret;
}

static int
ath12k_dp_process_rx_err_buf(struct ath12k *ar, struct hal_reo_dest_ring *desc,
			     struct list_head *used_list,
			     bool drop, u32 cookie)
{
	struct ath12k_base *ab = ar->ab;
	struct sk_buff *msdu;
	struct ath12k_skb_rxcb *rxcb;
	struct hal_rx_desc *rx_desc;
	u16 msdu_len;
	u32 hal_rx_desc_sz = ab->hal.hal_desc_sz;
	struct ath12k_rx_desc_info *desc_info;
	u64 desc_va;

	desc_va = ((u64)le32_to_cpu(desc->buf_va_hi) << 32 |
		   le32_to_cpu(desc->buf_va_lo));
	desc_info = (struct ath12k_rx_desc_info *)((unsigned long)desc_va);

	/* retry manual desc retrieval */
	if (!desc_info) {
		desc_info = ath12k_dp_get_rx_desc(ab, cookie);
		if (!desc_info) {
			ath12k_warn(ab, "Invalid cookie in DP rx error descriptor retrieval: 0x%x\n",
				    cookie);
			return -EINVAL;
		}
	}

	if (desc_info->magic != ATH12K_DP_RX_DESC_MAGIC)
		ath12k_warn(ab, " RX Exception, Check HW CC implementation");

	msdu = desc_info->skb;
	desc_info->skb = NULL;

	list_add_tail(&desc_info->list, used_list);

	rxcb = ATH12K_SKB_RXCB(msdu);
	dma_unmap_single(ar->ab->dev, rxcb->paddr,
			 msdu->len + skb_tailroom(msdu),
			 DMA_FROM_DEVICE);

	if (drop) {
		dev_kfree_skb_any(msdu);
		return 0;
	}

	rcu_read_lock();
	if (!rcu_dereference(ar->ab->pdevs_active[ar->pdev_idx])) {
		dev_kfree_skb_any(msdu);
		goto exit;
	}

	if (test_bit(ATH12K_FLAG_CAC_RUNNING, &ar->dev_flags)) {
		dev_kfree_skb_any(msdu);
		goto exit;
	}

	rx_desc = (struct hal_rx_desc *)msdu->data;
	msdu_len = ath12k_dp_rx_h_msdu_len(ar->ab, rx_desc);
	if ((msdu_len + hal_rx_desc_sz) > DP_RX_BUFFER_SIZE) {
		ath12k_warn(ar->ab, "invalid msdu leng %u", msdu_len);
		ath12k_dbg_dump(ar->ab, ATH12K_DBG_DATA, NULL, "", rx_desc,
				sizeof(*rx_desc));
		dev_kfree_skb_any(msdu);
		goto exit;
	}

	skb_put(msdu, hal_rx_desc_sz + msdu_len);

	if (ath12k_dp_rx_frag_h_mpdu(ar, msdu, desc)) {
		dev_kfree_skb_any(msdu);
		ath12k_dp_rx_link_desc_return(ar->ab, &desc->buf_addr_info,
					      HAL_WBM_REL_BM_ACT_PUT_IN_IDLE);
	}
exit:
	rcu_read_unlock();
	return 0;
}

int ath12k_dp_rx_process_err(struct ath12k_base *ab, struct napi_struct *napi,
			     int budget)
{
	struct ath12k_hw_group *ag = ab->ag;
	struct list_head rx_desc_used_list[ATH12K_MAX_DEVICES];
	u32 msdu_cookies[HAL_NUM_RX_MSDUS_PER_LINK_DESC];
	int num_buffs_reaped[ATH12K_MAX_DEVICES] = {};
	struct dp_link_desc_bank *link_desc_banks;
	enum hal_rx_buf_return_buf_manager rbm;
	struct hal_rx_msdu_link *link_desc_va;
	int tot_n_bufs_reaped, quota, ret, i;
	struct hal_reo_dest_ring *reo_desc;
	struct dp_rxdma_ring *rx_ring;
	struct dp_srng *reo_except;
	struct ath12k_hw_link *hw_links = ag->hw_links;
	struct ath12k_base *partner_ab;
	u8 hw_link_id, device_id;
	u32 desc_bank, num_msdus;
	struct hal_srng *srng;
	struct ath12k *ar;
	dma_addr_t paddr;
	bool is_frag;
	bool drop;
	int pdev_id;

	tot_n_bufs_reaped = 0;
	quota = budget;

	for (device_id = 0; device_id < ATH12K_MAX_DEVICES; device_id++)
		INIT_LIST_HEAD(&rx_desc_used_list[device_id]);

	reo_except = &ab->dp.reo_except_ring;

	srng = &ab->hal.srng_list[reo_except->ring_id];

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

	while (budget &&
	       (reo_desc = ath12k_hal_srng_dst_get_next_entry(ab, srng))) {
		drop = false;
		ab->device_stats.err_ring_pkts++;

		ret = ath12k_hal_desc_reo_parse_err(ab, reo_desc, &paddr,
						    &desc_bank);
		if (ret) {
			ath12k_warn(ab, "failed to parse error reo desc %d\n",
				    ret);
			continue;
		}

		hw_link_id = le32_get_bits(reo_desc->info0,
					   HAL_REO_DEST_RING_INFO0_SRC_LINK_ID);
		device_id = hw_links[hw_link_id].device_id;
		partner_ab = ath12k_ag_to_ab(ag, device_id);

		pdev_id = ath12k_hw_mac_id_to_pdev_id(partner_ab->hw_params,
						      hw_links[hw_link_id].pdev_idx);
		ar = partner_ab->pdevs[pdev_id].ar;

		link_desc_banks = partner_ab->dp.link_desc_banks;
		link_desc_va = link_desc_banks[desc_bank].vaddr +
			       (paddr - link_desc_banks[desc_bank].paddr);
		ath12k_hal_rx_msdu_link_info_get(link_desc_va, &num_msdus, msdu_cookies,
						 &rbm);
		if (rbm != partner_ab->dp.idle_link_rbm &&
		    rbm != HAL_RX_BUF_RBM_SW3_BM &&
		    rbm != partner_ab->hw_params->hal_params->rx_buf_rbm) {
			ab->device_stats.invalid_rbm++;
			ath12k_warn(ab, "invalid return buffer manager %d\n", rbm);
			ath12k_dp_rx_link_desc_return(partner_ab,
						      &reo_desc->buf_addr_info,
						      HAL_WBM_REL_BM_ACT_REL_MSDU);
			continue;
		}

		is_frag = !!(le32_to_cpu(reo_desc->rx_mpdu_info.info0) &
			     RX_MPDU_DESC_INFO0_FRAG_FLAG);

		/* Process only rx fragments with one msdu per link desc below, and drop
		 * msdu's indicated due to error reasons.
		 * Dynamic fragmentation not supported in Multi-link client, so drop the
		 * partner device buffers.
		 */
		if (!is_frag || num_msdus > 1 ||
		    partner_ab->device_id != ab->device_id) {
			drop = true;

			/* Return the link desc back to wbm idle list */
			ath12k_dp_rx_link_desc_return(partner_ab,
						      &reo_desc->buf_addr_info,
						      HAL_WBM_REL_BM_ACT_PUT_IN_IDLE);
		}

		for (i = 0; i < num_msdus; i++) {
			if (!ath12k_dp_process_rx_err_buf(ar, reo_desc,
							  &rx_desc_used_list[device_id],
							  drop,
							  msdu_cookies[i])) {
				num_buffs_reaped[device_id]++;
				tot_n_bufs_reaped++;
			}
		}

		if (tot_n_bufs_reaped >= quota) {
			tot_n_bufs_reaped = quota;
			goto exit;
		}

		budget = quota - tot_n_bufs_reaped;
	}

exit:
	ath12k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	for (device_id = 0; device_id < ATH12K_MAX_DEVICES; device_id++) {
		if (!num_buffs_reaped[device_id])
			continue;

		partner_ab = ath12k_ag_to_ab(ag, device_id);
		rx_ring = &partner_ab->dp.rx_refill_buf_ring;

		ath12k_dp_rx_bufs_replenish(partner_ab, rx_ring,
					    &rx_desc_used_list[device_id],
					    num_buffs_reaped[device_id]);
	}

	return tot_n_bufs_reaped;
}

static void ath12k_dp_rx_null_q_desc_sg_drop(struct ath12k *ar,
					     int msdu_len,
					     struct sk_buff_head *msdu_list)
{
	struct sk_buff *skb, *tmp;
	struct ath12k_skb_rxcb *rxcb;
	int n_buffs;

	n_buffs = DIV_ROUND_UP(msdu_len,
			       (DP_RX_BUFFER_SIZE - ar->ab->hal.hal_desc_sz));

	skb_queue_walk_safe(msdu_list, skb, tmp) {
		rxcb = ATH12K_SKB_RXCB(skb);
		if (rxcb->err_rel_src == HAL_WBM_REL_SRC_MODULE_REO &&
		    rxcb->err_code == HAL_REO_DEST_RING_ERROR_CODE_DESC_ADDR_ZERO) {
			if (!n_buffs)
				break;
			__skb_unlink(skb, msdu_list);
			dev_kfree_skb_any(skb);
			n_buffs--;
		}
	}
}

static int ath12k_dp_rx_h_null_q_desc(struct ath12k *ar, struct sk_buff *msdu,
				      struct ath12k_dp_rx_info *rx_info,
				      struct sk_buff_head *msdu_list)
{
	struct ath12k_base *ab = ar->ab;
	u16 msdu_len;
	struct hal_rx_desc *desc = (struct hal_rx_desc *)msdu->data;
	u8 l3pad_bytes;
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	u32 hal_rx_desc_sz = ar->ab->hal.hal_desc_sz;

	msdu_len = ath12k_dp_rx_h_msdu_len(ab, desc);

	if (!rxcb->is_frag && ((msdu_len + hal_rx_desc_sz) > DP_RX_BUFFER_SIZE)) {
		/* First buffer will be freed by the caller, so deduct it's length */
		msdu_len = msdu_len - (DP_RX_BUFFER_SIZE - hal_rx_desc_sz);
		ath12k_dp_rx_null_q_desc_sg_drop(ar, msdu_len, msdu_list);
		return -EINVAL;
	}

	/* Even after cleaning up the sg buffers in the msdu list with above check
	 * any msdu received with continuation flag needs to be dropped as invalid.
	 * This protects against some random err frame with continuation flag.
	 */
	if (rxcb->is_continuation)
		return -EINVAL;

	if (!ath12k_dp_rx_h_msdu_done(ab, desc)) {
		ath12k_warn(ar->ab,
			    "msdu_done bit not set in null_q_des processing\n");
		__skb_queue_purge(msdu_list);
		return -EIO;
	}

	/* Handle NULL queue descriptor violations arising out a missing
	 * REO queue for a given peer or a given TID. This typically
	 * may happen if a packet is received on a QOS enabled TID before the
	 * ADDBA negotiation for that TID, when the TID queue is setup. Or
	 * it may also happen for MC/BC frames if they are not routed to the
	 * non-QOS TID queue, in the absence of any other default TID queue.
	 * This error can show up both in a REO destination or WBM release ring.
	 */

	if (rxcb->is_frag) {
		skb_pull(msdu, hal_rx_desc_sz);
	} else {
		l3pad_bytes = ath12k_dp_rx_h_l3pad(ab, desc);

		if ((hal_rx_desc_sz + l3pad_bytes + msdu_len) > DP_RX_BUFFER_SIZE)
			return -EINVAL;

		skb_put(msdu, hal_rx_desc_sz + l3pad_bytes + msdu_len);
		skb_pull(msdu, hal_rx_desc_sz + l3pad_bytes);
	}
	if (unlikely(!ath12k_dp_rx_check_nwifi_hdr_len_valid(ab, desc, msdu)))
		return -EINVAL;

	ath12k_dp_rx_h_fetch_info(ab, desc, rx_info);
	ath12k_dp_rx_h_ppdu(ar, rx_info);
	ath12k_dp_rx_h_mpdu(ar, msdu, desc, rx_info);

	rxcb->tid = rx_info->tid;

	/* Please note that caller will having the access to msdu and completing
	 * rx with mac80211. Need not worry about cleaning up amsdu_list.
	 */

	return 0;
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

void ath12k_dp_rx_process_reo_status(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = &ab->dp;
	struct hal_tlv_64_hdr *hdr;
	struct hal_srng *srng;
	struct ath12k_dp_rx_reo_cmd *cmd, *tmp;
	bool found = false;
	u16 tag;
	struct hal_reo_status reo_status;

	srng = &ab->hal.srng_list[dp->reo_status_ring.ring_id];

	memset(&reo_status, 0, sizeof(reo_status));

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

	while ((hdr = ath12k_hal_srng_dst_get_next_entry(ab, srng))) {
		tag = le64_get_bits(hdr->tl, HAL_SRNG_TLV_HDR_TAG);

		switch (tag) {
		case HAL_REO_GET_QUEUE_STATS_STATUS:
			ath12k_hal_reo_status_queue_stats(ab, hdr,
							  &reo_status);
			break;
		case HAL_REO_FLUSH_QUEUE_STATUS:
			ath12k_hal_reo_flush_queue_status(ab, hdr,
							  &reo_status);
			break;
		case HAL_REO_FLUSH_CACHE_STATUS:
			ath12k_hal_reo_flush_cache_status(ab, hdr,
							  &reo_status);
			break;
		case HAL_REO_UNBLOCK_CACHE_STATUS:
			ath12k_hal_reo_unblk_cache_status(ab, hdr,
							  &reo_status);
			break;
		case HAL_REO_FLUSH_TIMEOUT_LIST_STATUS:
			ath12k_hal_reo_flush_timeout_list_status(ab, hdr,
								 &reo_status);
			break;
		case HAL_REO_DESCRIPTOR_THRESHOLD_REACHED_STATUS:
			ath12k_hal_reo_desc_thresh_reached_status(ab, hdr,
								  &reo_status);
			break;
		case HAL_REO_UPDATE_RX_REO_QUEUE_STATUS:
			ath12k_hal_reo_update_rx_reo_queue_status(ab, hdr,
								  &reo_status);
			break;
		default:
			ath12k_warn(ab, "Unknown reo status type %d\n", tag);
			continue;
		}

		spin_lock_bh(&dp->reo_cmd_lock);
		list_for_each_entry_safe(cmd, tmp, &dp->reo_cmd_list, list) {
			if (reo_status.uniform_hdr.cmd_num == cmd->cmd_num) {
				found = true;
				list_del(&cmd->list);
				break;
			}
		}
		spin_unlock_bh(&dp->reo_cmd_lock);

		if (found) {
			cmd->handler(dp, (void *)&cmd->data,
				     reo_status.uniform_hdr.cmd_status);
			kfree(cmd);
		}

		found = false;
	}

	ath12k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);
}

void ath12k_dp_rx_free(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = &ab->dp;
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

int ath12k_dp_rxdma_ring_sel_config_qcn9274(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = &ab->dp;
	struct htt_rx_ring_tlv_filter tlv_filter = {};
	u32 ring_id;
	int ret;
	u32 hal_rx_desc_sz = ab->hal.hal_desc_sz;

	ring_id = dp->rx_refill_buf_ring.refill_buf_ring.ring_id;

	tlv_filter.rx_filter = HTT_RX_TLV_FLAGS_RXDMA_RING;
	tlv_filter.pkt_filter_flags2 = HTT_RX_FP_CTRL_PKT_FILTER_TLV_FLAGS2_BAR;
	tlv_filter.pkt_filter_flags3 = HTT_RX_FP_DATA_PKT_FILTER_TLV_FLASG3_MCAST |
					HTT_RX_FP_DATA_PKT_FILTER_TLV_FLASG3_UCAST |
					HTT_RX_FP_DATA_PKT_FILTER_TLV_FLASG3_NULL_DATA;
	tlv_filter.offset_valid = true;
	tlv_filter.rx_packet_offset = hal_rx_desc_sz;

	tlv_filter.rx_mpdu_start_offset =
		ab->hal_rx_ops->rx_desc_get_mpdu_start_offset();
	tlv_filter.rx_msdu_end_offset =
		ab->hal_rx_ops->rx_desc_get_msdu_end_offset();

	if (ath12k_dp_wmask_compaction_rx_tlv_supported(ab)) {
		tlv_filter.rx_mpdu_start_wmask =
			ab->hw_params->hal_ops->rxdma_ring_wmask_rx_mpdu_start();
		tlv_filter.rx_msdu_end_wmask =
			ab->hw_params->hal_ops->rxdma_ring_wmask_rx_msdu_end();
		ath12k_dbg(ab, ATH12K_DBG_DATA,
			   "Configuring compact tlv masks rx_mpdu_start_wmask 0x%x rx_msdu_end_wmask 0x%x\n",
			   tlv_filter.rx_mpdu_start_wmask, tlv_filter.rx_msdu_end_wmask);
	}

	ret = ath12k_dp_tx_htt_rx_filter_setup(ab, ring_id, 0,
					       HAL_RXDMA_BUF,
					       DP_RXDMA_REFILL_RING_SIZE,
					       &tlv_filter);

	return ret;
}

int ath12k_dp_rxdma_ring_sel_config_wcn7850(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = &ab->dp;
	struct htt_rx_ring_tlv_filter tlv_filter = {};
	u32 ring_id;
	int ret = 0;
	u32 hal_rx_desc_sz = ab->hal.hal_desc_sz;
	int i;

	ring_id = dp->rx_refill_buf_ring.refill_buf_ring.ring_id;

	tlv_filter.rx_filter = HTT_RX_TLV_FLAGS_RXDMA_RING;
	tlv_filter.pkt_filter_flags2 = HTT_RX_FP_CTRL_PKT_FILTER_TLV_FLAGS2_BAR;
	tlv_filter.pkt_filter_flags3 = HTT_RX_FP_DATA_PKT_FILTER_TLV_FLASG3_MCAST |
					HTT_RX_FP_DATA_PKT_FILTER_TLV_FLASG3_UCAST |
					HTT_RX_FP_DATA_PKT_FILTER_TLV_FLASG3_NULL_DATA;
	tlv_filter.offset_valid = true;
	tlv_filter.rx_packet_offset = hal_rx_desc_sz;

	tlv_filter.rx_header_offset = offsetof(struct hal_rx_desc_wcn7850, pkt_hdr_tlv);

	tlv_filter.rx_mpdu_start_offset =
		ab->hal_rx_ops->rx_desc_get_mpdu_start_offset();
	tlv_filter.rx_msdu_end_offset =
		ab->hal_rx_ops->rx_desc_get_msdu_end_offset();

	/* TODO: Selectively subscribe to required qwords within msdu_end
	 * and mpdu_start and setup the mask in below msg
	 * and modify the rx_desc struct
	 */

	for (i = 0; i < ab->hw_params->num_rxdma_per_pdev; i++) {
		ring_id = dp->rx_mac_buf_ring[i].ring_id;
		ret = ath12k_dp_tx_htt_rx_filter_setup(ab, ring_id, i,
						       HAL_RXDMA_BUF,
						       DP_RXDMA_REFILL_RING_SIZE,
						       &tlv_filter);
	}

	return ret;
}

int ath12k_dp_rx_htt_setup(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = &ab->dp;
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
	struct ath12k_dp *dp = &ab->dp;
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
