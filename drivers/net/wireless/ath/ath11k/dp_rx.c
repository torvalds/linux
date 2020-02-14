// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#include <linux/ieee80211.h>
#include "core.h"
#include "debug.h"
#include "hal_desc.h"
#include "hw.h"
#include "dp_rx.h"
#include "hal_rx.h"
#include "dp_tx.h"
#include "peer.h"

static u8 *ath11k_dp_rx_h_80211_hdr(struct hal_rx_desc *desc)
{
	return desc->hdr_status;
}

static enum hal_encrypt_type ath11k_dp_rx_h_mpdu_start_enctype(struct hal_rx_desc *desc)
{
	if (!(__le32_to_cpu(desc->mpdu_start.info1) &
	    RX_MPDU_START_INFO1_ENCRYPT_INFO_VALID))
		return HAL_ENCRYPT_TYPE_OPEN;

	return FIELD_GET(RX_MPDU_START_INFO2_ENC_TYPE,
			 __le32_to_cpu(desc->mpdu_start.info2));
}

static u8 ath11k_dp_rx_h_mpdu_start_decap_type(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MPDU_START_INFO5_DECAP_TYPE,
			 __le32_to_cpu(desc->mpdu_start.info5));
}

static bool ath11k_dp_rx_h_attn_msdu_done(struct hal_rx_desc *desc)
{
	return !!FIELD_GET(RX_ATTENTION_INFO2_MSDU_DONE,
			   __le32_to_cpu(desc->attention.info2));
}

static bool ath11k_dp_rx_h_attn_first_mpdu(struct hal_rx_desc *desc)
{
	return !!FIELD_GET(RX_ATTENTION_INFO1_FIRST_MPDU,
			   __le32_to_cpu(desc->attention.info1));
}

static bool ath11k_dp_rx_h_attn_l4_cksum_fail(struct hal_rx_desc *desc)
{
	return !!FIELD_GET(RX_ATTENTION_INFO1_TCP_UDP_CKSUM_FAIL,
			   __le32_to_cpu(desc->attention.info1));
}

static bool ath11k_dp_rx_h_attn_ip_cksum_fail(struct hal_rx_desc *desc)
{
	return !!FIELD_GET(RX_ATTENTION_INFO1_IP_CKSUM_FAIL,
			   __le32_to_cpu(desc->attention.info1));
}

static bool ath11k_dp_rx_h_attn_is_decrypted(struct hal_rx_desc *desc)
{
	return (FIELD_GET(RX_ATTENTION_INFO2_DCRYPT_STATUS_CODE,
			  __le32_to_cpu(desc->attention.info2)) ==
		RX_DESC_DECRYPT_STATUS_CODE_OK);
}

static u32 ath11k_dp_rx_h_attn_mpdu_err(struct hal_rx_desc *desc)
{
	u32 info = __le32_to_cpu(desc->attention.info1);
	u32 errmap = 0;

	if (info & RX_ATTENTION_INFO1_FCS_ERR)
		errmap |= DP_RX_MPDU_ERR_FCS;

	if (info & RX_ATTENTION_INFO1_DECRYPT_ERR)
		errmap |= DP_RX_MPDU_ERR_DECRYPT;

	if (info & RX_ATTENTION_INFO1_TKIP_MIC_ERR)
		errmap |= DP_RX_MPDU_ERR_TKIP_MIC;

	if (info & RX_ATTENTION_INFO1_A_MSDU_ERROR)
		errmap |= DP_RX_MPDU_ERR_AMSDU_ERR;

	if (info & RX_ATTENTION_INFO1_OVERFLOW_ERR)
		errmap |= DP_RX_MPDU_ERR_OVERFLOW;

	if (info & RX_ATTENTION_INFO1_MSDU_LEN_ERR)
		errmap |= DP_RX_MPDU_ERR_MSDU_LEN;

	if (info & RX_ATTENTION_INFO1_MPDU_LEN_ERR)
		errmap |= DP_RX_MPDU_ERR_MPDU_LEN;

	return errmap;
}

static u16 ath11k_dp_rx_h_msdu_start_msdu_len(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO1_MSDU_LENGTH,
			 __le32_to_cpu(desc->msdu_start.info1));
}

static u8 ath11k_dp_rx_h_msdu_start_sgi(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_SGI,
			 __le32_to_cpu(desc->msdu_start.info3));
}

static u8 ath11k_dp_rx_h_msdu_start_rate_mcs(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_RATE_MCS,
			 __le32_to_cpu(desc->msdu_start.info3));
}

static u8 ath11k_dp_rx_h_msdu_start_rx_bw(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_RECV_BW,
			 __le32_to_cpu(desc->msdu_start.info3));
}

static u32 ath11k_dp_rx_h_msdu_start_freq(struct hal_rx_desc *desc)
{
	return __le32_to_cpu(desc->msdu_start.phy_meta_data);
}

static u8 ath11k_dp_rx_h_msdu_start_pkt_type(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_START_INFO3_PKT_TYPE,
			 __le32_to_cpu(desc->msdu_start.info3));
}

static u8 ath11k_dp_rx_h_msdu_start_nss(struct hal_rx_desc *desc)
{
	u8 mimo_ss_bitmap = FIELD_GET(RX_MSDU_START_INFO3_MIMO_SS_BITMAP,
				      __le32_to_cpu(desc->msdu_start.info3));

	return hweight8(mimo_ss_bitmap);
}

static u8 ath11k_dp_rx_h_msdu_end_l3pad(struct hal_rx_desc *desc)
{
	return FIELD_GET(RX_MSDU_END_INFO2_L3_HDR_PADDING,
			 __le32_to_cpu(desc->msdu_end.info2));
}

static bool ath11k_dp_rx_h_msdu_end_first_msdu(struct hal_rx_desc *desc)
{
	return !!FIELD_GET(RX_MSDU_END_INFO2_FIRST_MSDU,
			   __le32_to_cpu(desc->msdu_end.info2));
}

static bool ath11k_dp_rx_h_msdu_end_last_msdu(struct hal_rx_desc *desc)
{
	return !!FIELD_GET(RX_MSDU_END_INFO2_LAST_MSDU,
			   __le32_to_cpu(desc->msdu_end.info2));
}

static void ath11k_dp_rx_desc_end_tlv_copy(struct hal_rx_desc *fdesc,
					   struct hal_rx_desc *ldesc)
{
	memcpy((u8 *)&fdesc->msdu_end, (u8 *)&ldesc->msdu_end,
	       sizeof(struct rx_msdu_end));
	memcpy((u8 *)&fdesc->attention, (u8 *)&ldesc->attention,
	       sizeof(struct rx_attention));
	memcpy((u8 *)&fdesc->mpdu_end, (u8 *)&ldesc->mpdu_end,
	       sizeof(struct rx_mpdu_end));
}

static u32 ath11k_dp_rxdesc_get_mpdulen_err(struct hal_rx_desc *rx_desc)
{
	struct rx_attention *rx_attn;

	rx_attn = &rx_desc->attention;

	return FIELD_GET(RX_ATTENTION_INFO1_MPDU_LEN_ERR,
			 __le32_to_cpu(rx_attn->info1));
}

static u32 ath11k_dp_rxdesc_get_decap_format(struct hal_rx_desc *rx_desc)
{
	struct rx_msdu_start *rx_msdu_start;

	rx_msdu_start = &rx_desc->msdu_start;

	return FIELD_GET(RX_MSDU_START_INFO2_DECAP_FORMAT,
			 __le32_to_cpu(rx_msdu_start->info2));
}

static u8 *ath11k_dp_rxdesc_get_80211hdr(struct hal_rx_desc *rx_desc)
{
	u8 *rx_pkt_hdr;

	rx_pkt_hdr = &rx_desc->msdu_payload[0];

	return rx_pkt_hdr;
}

static bool ath11k_dp_rxdesc_mpdu_valid(struct hal_rx_desc *rx_desc)
{
	u32 tlv_tag;

	tlv_tag = FIELD_GET(HAL_TLV_HDR_TAG,
			    __le32_to_cpu(rx_desc->mpdu_start_tag));

	return tlv_tag == HAL_RX_MPDU_START ? true : false;
}

static u32 ath11k_dp_rxdesc_get_ppduid(struct hal_rx_desc *rx_desc)
{
	return __le16_to_cpu(rx_desc->mpdu_start.phy_ppdu_id);
}

/* Returns number of Rx buffers replenished */
int ath11k_dp_rxbufs_replenish(struct ath11k_base *ab, int mac_id,
			       struct dp_rxdma_ring *rx_ring,
			       int req_entries,
			       enum hal_rx_buf_return_buf_manager mgr,
			       gfp_t gfp)
{
	struct hal_srng *srng;
	u32 *desc;
	struct sk_buff *skb;
	int num_free;
	int num_remain;
	int buf_id;
	u32 cookie;
	dma_addr_t paddr;

	req_entries = min(req_entries, rx_ring->bufs_max);

	srng = &ab->hal.srng_list[rx_ring->refill_buf_ring.ring_id];

	spin_lock_bh(&srng->lock);

	ath11k_hal_srng_access_begin(ab, srng);

	num_free = ath11k_hal_srng_src_num_free(ab, srng, true);
	if (!req_entries && (num_free > (rx_ring->bufs_max * 3) / 4))
		req_entries = num_free;

	req_entries = min(num_free, req_entries);
	num_remain = req_entries;

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

		spin_lock_bh(&rx_ring->idr_lock);
		buf_id = idr_alloc(&rx_ring->bufs_idr, skb, 0,
				   rx_ring->bufs_max * 3, gfp);
		spin_unlock_bh(&rx_ring->idr_lock);
		if (buf_id < 0)
			goto fail_dma_unmap;

		desc = ath11k_hal_srng_src_get_next_entry(ab, srng);
		if (!desc)
			goto fail_idr_remove;

		ATH11K_SKB_RXCB(skb)->paddr = paddr;

		cookie = FIELD_PREP(DP_RXDMA_BUF_COOKIE_PDEV_ID, mac_id) |
			 FIELD_PREP(DP_RXDMA_BUF_COOKIE_BUF_ID, buf_id);

		num_remain--;

		ath11k_hal_rx_buf_addr_info_set(desc, paddr, cookie, mgr);
	}

	ath11k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	return req_entries - num_remain;

fail_idr_remove:
	spin_lock_bh(&rx_ring->idr_lock);
	idr_remove(&rx_ring->bufs_idr, buf_id);
	spin_unlock_bh(&rx_ring->idr_lock);
fail_dma_unmap:
	dma_unmap_single(ab->dev, paddr, skb->len + skb_tailroom(skb),
			 DMA_FROM_DEVICE);
fail_free_skb:
	dev_kfree_skb_any(skb);

	ath11k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	return req_entries - num_remain;
}

static int ath11k_dp_rxdma_buf_ring_free(struct ath11k *ar,
					 struct dp_rxdma_ring *rx_ring)
{
	struct ath11k_pdev_dp *dp = &ar->dp;
	struct sk_buff *skb;
	int buf_id;

	spin_lock_bh(&rx_ring->idr_lock);
	idr_for_each_entry(&rx_ring->bufs_idr, skb, buf_id) {
		idr_remove(&rx_ring->bufs_idr, buf_id);
		/* TODO: Understand where internal driver does this dma_unmap of
		 * of rxdma_buffer.
		 */
		dma_unmap_single(ar->ab->dev, ATH11K_SKB_RXCB(skb)->paddr,
				 skb->len + skb_tailroom(skb), DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb);
	}

	idr_destroy(&rx_ring->bufs_idr);
	spin_unlock_bh(&rx_ring->idr_lock);

	rx_ring = &dp->rx_mon_status_refill_ring;

	spin_lock_bh(&rx_ring->idr_lock);
	idr_for_each_entry(&rx_ring->bufs_idr, skb, buf_id) {
		idr_remove(&rx_ring->bufs_idr, buf_id);
		/* XXX: Understand where internal driver does this dma_unmap of
		 * of rxdma_buffer.
		 */
		dma_unmap_single(ar->ab->dev, ATH11K_SKB_RXCB(skb)->paddr,
				 skb->len + skb_tailroom(skb), DMA_BIDIRECTIONAL);
		dev_kfree_skb_any(skb);
	}

	idr_destroy(&rx_ring->bufs_idr);
	spin_unlock_bh(&rx_ring->idr_lock);
	return 0;
}

static int ath11k_dp_rxdma_pdev_buf_free(struct ath11k *ar)
{
	struct ath11k_pdev_dp *dp = &ar->dp;
	struct dp_rxdma_ring *rx_ring = &dp->rx_refill_buf_ring;

	ath11k_dp_rxdma_buf_ring_free(ar, rx_ring);

	rx_ring = &dp->rxdma_mon_buf_ring;
	ath11k_dp_rxdma_buf_ring_free(ar, rx_ring);

	rx_ring = &dp->rx_mon_status_refill_ring;
	ath11k_dp_rxdma_buf_ring_free(ar, rx_ring);
	return 0;
}

static int ath11k_dp_rxdma_ring_buf_setup(struct ath11k *ar,
					  struct dp_rxdma_ring *rx_ring,
					  u32 ringtype)
{
	struct ath11k_pdev_dp *dp = &ar->dp;
	int num_entries;

	num_entries = rx_ring->refill_buf_ring.size /
		      ath11k_hal_srng_get_entrysize(ringtype);

	rx_ring->bufs_max = num_entries;
	ath11k_dp_rxbufs_replenish(ar->ab, dp->mac_id, rx_ring, num_entries,
				   HAL_RX_BUF_RBM_SW3_BM, GFP_KERNEL);
	return 0;
}

static int ath11k_dp_rxdma_pdev_buf_setup(struct ath11k *ar)
{
	struct ath11k_pdev_dp *dp = &ar->dp;
	struct dp_rxdma_ring *rx_ring = &dp->rx_refill_buf_ring;

	ath11k_dp_rxdma_ring_buf_setup(ar, rx_ring, HAL_RXDMA_BUF);

	rx_ring = &dp->rxdma_mon_buf_ring;
	ath11k_dp_rxdma_ring_buf_setup(ar, rx_ring, HAL_RXDMA_MONITOR_BUF);

	rx_ring = &dp->rx_mon_status_refill_ring;
	ath11k_dp_rxdma_ring_buf_setup(ar, rx_ring, HAL_RXDMA_MONITOR_STATUS);

	return 0;
}

static void ath11k_dp_rx_pdev_srng_free(struct ath11k *ar)
{
	struct ath11k_pdev_dp *dp = &ar->dp;

	ath11k_dp_srng_cleanup(ar->ab, &dp->rx_refill_buf_ring.refill_buf_ring);
	ath11k_dp_srng_cleanup(ar->ab, &dp->rxdma_err_dst_ring);
	ath11k_dp_srng_cleanup(ar->ab, &dp->rx_mon_status_refill_ring.refill_buf_ring);
	ath11k_dp_srng_cleanup(ar->ab, &dp->rxdma_mon_buf_ring.refill_buf_ring);
}

void ath11k_dp_pdev_reo_cleanup(struct ath11k_base *ab)
{
	struct ath11k_pdev_dp *dp;
	struct ath11k *ar;
	int i;

	for (i = 0; i < ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;
		dp = &ar->dp;
		ath11k_dp_srng_cleanup(ab, &dp->reo_dst_ring);
	}
}

int ath11k_dp_pdev_reo_setup(struct ath11k_base *ab)
{
	struct ath11k *ar;
	struct ath11k_pdev_dp *dp;
	int ret;
	int i;

	for (i = 0; i < ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;
		dp = &ar->dp;
		ret = ath11k_dp_srng_setup(ab, &dp->reo_dst_ring, HAL_REO_DST,
					   dp->mac_id, dp->mac_id,
					   DP_REO_DST_RING_SIZE);
		if (ret) {
			ath11k_warn(ar->ab, "failed to setup reo_dst_ring\n");
			goto err_reo_cleanup;
		}
	}

	return 0;

err_reo_cleanup:
	ath11k_dp_pdev_reo_cleanup(ab);

	return ret;
}

static int ath11k_dp_rx_pdev_srng_alloc(struct ath11k *ar)
{
	struct ath11k_pdev_dp *dp = &ar->dp;
	struct dp_srng *srng = NULL;
	int ret;

	ret = ath11k_dp_srng_setup(ar->ab,
				   &dp->rx_refill_buf_ring.refill_buf_ring,
				   HAL_RXDMA_BUF, 0,
				   dp->mac_id, DP_RXDMA_BUF_RING_SIZE);
	if (ret) {
		ath11k_warn(ar->ab, "failed to setup rx_refill_buf_ring\n");
		return ret;
	}

	ret = ath11k_dp_srng_setup(ar->ab, &dp->rxdma_err_dst_ring,
				   HAL_RXDMA_DST, 0, dp->mac_id,
				   DP_RXDMA_ERR_DST_RING_SIZE);
	if (ret) {
		ath11k_warn(ar->ab, "failed to setup rxdma_err_dst_ring\n");
		return ret;
	}

	srng = &dp->rx_mon_status_refill_ring.refill_buf_ring;
	ret = ath11k_dp_srng_setup(ar->ab,
				   srng,
				   HAL_RXDMA_MONITOR_STATUS, 0, dp->mac_id,
				   DP_RXDMA_MON_STATUS_RING_SIZE);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to setup rx_mon_status_refill_ring\n");
		return ret;
	}
	ret = ath11k_dp_srng_setup(ar->ab,
				   &dp->rxdma_mon_buf_ring.refill_buf_ring,
				   HAL_RXDMA_MONITOR_BUF, 0, dp->mac_id,
				   DP_RXDMA_MONITOR_BUF_RING_SIZE);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to setup HAL_RXDMA_MONITOR_BUF\n");
		return ret;
	}

	ret = ath11k_dp_srng_setup(ar->ab, &dp->rxdma_mon_dst_ring,
				   HAL_RXDMA_MONITOR_DST, 0, dp->mac_id,
				   DP_RXDMA_MONITOR_DST_RING_SIZE);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to setup HAL_RXDMA_MONITOR_DST\n");
		return ret;
	}

	ret = ath11k_dp_srng_setup(ar->ab, &dp->rxdma_mon_desc_ring,
				   HAL_RXDMA_MONITOR_DESC, 0, dp->mac_id,
				   DP_RXDMA_MONITOR_DESC_RING_SIZE);
	if (ret) {
		ath11k_warn(ar->ab,
			    "failed to setup HAL_RXDMA_MONITOR_DESC\n");
		return ret;
	}

	return 0;
}

void ath11k_dp_reo_cmd_list_cleanup(struct ath11k_base *ab)
{
	struct ath11k_dp *dp = &ab->dp;
	struct dp_reo_cmd *cmd, *tmp;
	struct dp_reo_cache_flush_elem *cmd_cache, *tmp_cache;

	spin_lock_bh(&dp->reo_cmd_lock);
	list_for_each_entry_safe(cmd, tmp, &dp->reo_cmd_list, list) {
		list_del(&cmd->list);
		dma_unmap_single(ab->dev, cmd->data.paddr,
				 cmd->data.size, DMA_BIDIRECTIONAL);
		kfree(cmd->data.vaddr);
		kfree(cmd);
	}

	list_for_each_entry_safe(cmd_cache, tmp_cache,
				 &dp->reo_cmd_cache_flush_list, list) {
		list_del(&cmd_cache->list);
		dma_unmap_single(ab->dev, cmd_cache->data.paddr,
				 cmd_cache->data.size, DMA_BIDIRECTIONAL);
		kfree(cmd_cache->data.vaddr);
		kfree(cmd_cache);
	}
	spin_unlock_bh(&dp->reo_cmd_lock);
}

static void ath11k_dp_reo_cmd_free(struct ath11k_dp *dp, void *ctx,
				   enum hal_reo_cmd_status status)
{
	struct dp_rx_tid *rx_tid = ctx;

	if (status != HAL_REO_CMD_SUCCESS)
		ath11k_warn(dp->ab, "failed to flush rx tid hw desc, tid %d status %d\n",
			    rx_tid->tid, status);

	dma_unmap_single(dp->ab->dev, rx_tid->paddr, rx_tid->size,
			 DMA_BIDIRECTIONAL);
	kfree(rx_tid->vaddr);
}

static void ath11k_dp_reo_cache_flush(struct ath11k_base *ab,
				      struct dp_rx_tid *rx_tid)
{
	struct ath11k_hal_reo_cmd cmd = {0};
	unsigned long tot_desc_sz, desc_sz;
	int ret;

	tot_desc_sz = rx_tid->size;
	desc_sz = ath11k_hal_reo_qdesc_size(0, HAL_DESC_REO_NON_QOS_TID);

	while (tot_desc_sz > desc_sz) {
		tot_desc_sz -= desc_sz;
		cmd.addr_lo = lower_32_bits(rx_tid->paddr + tot_desc_sz);
		cmd.addr_hi = upper_32_bits(rx_tid->paddr);
		ret = ath11k_dp_tx_send_reo_cmd(ab, rx_tid,
						HAL_REO_CMD_FLUSH_CACHE, &cmd,
						NULL);
		if (ret)
			ath11k_warn(ab,
				    "failed to send HAL_REO_CMD_FLUSH_CACHE, tid %d (%d)\n",
				    rx_tid->tid, ret);
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.addr_lo = lower_32_bits(rx_tid->paddr);
	cmd.addr_hi = upper_32_bits(rx_tid->paddr);
	cmd.flag |= HAL_REO_CMD_FLG_NEED_STATUS;
	ret = ath11k_dp_tx_send_reo_cmd(ab, rx_tid,
					HAL_REO_CMD_FLUSH_CACHE,
					&cmd, ath11k_dp_reo_cmd_free);
	if (ret) {
		ath11k_err(ab, "failed to send HAL_REO_CMD_FLUSH_CACHE cmd, tid %d (%d)\n",
			   rx_tid->tid, ret);
		dma_unmap_single(ab->dev, rx_tid->paddr, rx_tid->size,
				 DMA_BIDIRECTIONAL);
		kfree(rx_tid->vaddr);
	}
}

static void ath11k_dp_rx_tid_del_func(struct ath11k_dp *dp, void *ctx,
				      enum hal_reo_cmd_status status)
{
	struct ath11k_base *ab = dp->ab;
	struct dp_rx_tid *rx_tid = ctx;
	struct dp_reo_cache_flush_elem *elem, *tmp;

	if (status == HAL_REO_CMD_DRAIN) {
		goto free_desc;
	} else if (status != HAL_REO_CMD_SUCCESS) {
		/* Shouldn't happen! Cleanup in case of other failure? */
		ath11k_warn(ab, "failed to delete rx tid %d hw descriptor %d\n",
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
	spin_unlock_bh(&dp->reo_cmd_lock);

	/* Flush and invalidate aged REO desc from HW cache */
	spin_lock_bh(&dp->reo_cmd_lock);
	list_for_each_entry_safe(elem, tmp, &dp->reo_cmd_cache_flush_list,
				 list) {
		if (time_after(jiffies, elem->ts +
			       msecs_to_jiffies(DP_REO_DESC_FREE_TIMEOUT_MS))) {
			list_del(&elem->list);
			spin_unlock_bh(&dp->reo_cmd_lock);

			ath11k_dp_reo_cache_flush(ab, &elem->data);
			kfree(elem);
			spin_lock_bh(&dp->reo_cmd_lock);
		}
	}
	spin_unlock_bh(&dp->reo_cmd_lock);

	return;
free_desc:
	dma_unmap_single(ab->dev, rx_tid->paddr, rx_tid->size,
			 DMA_BIDIRECTIONAL);
	kfree(rx_tid->vaddr);
}

void ath11k_peer_rx_tid_delete(struct ath11k *ar,
			       struct ath11k_peer *peer, u8 tid)
{
	struct ath11k_hal_reo_cmd cmd = {0};
	struct dp_rx_tid *rx_tid = &peer->rx_tid[tid];
	int ret;

	if (!rx_tid->active)
		return;

	cmd.flag = HAL_REO_CMD_FLG_NEED_STATUS;
	cmd.addr_lo = lower_32_bits(rx_tid->paddr);
	cmd.addr_hi = upper_32_bits(rx_tid->paddr);
	cmd.upd0 |= HAL_REO_CMD_UPD0_VLD;
	ret = ath11k_dp_tx_send_reo_cmd(ar->ab, rx_tid,
					HAL_REO_CMD_UPDATE_RX_QUEUE, &cmd,
					ath11k_dp_rx_tid_del_func);
	if (ret) {
		ath11k_err(ar->ab, "failed to send HAL_REO_CMD_UPDATE_RX_QUEUE cmd, tid %d (%d)\n",
			   tid, ret);
		dma_unmap_single(ar->ab->dev, rx_tid->paddr, rx_tid->size,
				 DMA_BIDIRECTIONAL);
		kfree(rx_tid->vaddr);
	}

	rx_tid->active = false;
}

void ath11k_peer_rx_tid_cleanup(struct ath11k *ar, struct ath11k_peer *peer)
{
	int i;

	for (i = 0; i <= IEEE80211_NUM_TIDS; i++)
		ath11k_peer_rx_tid_delete(ar, peer, i);
}

static int ath11k_peer_rx_tid_reo_update(struct ath11k *ar,
					 struct ath11k_peer *peer,
					 struct dp_rx_tid *rx_tid,
					 u32 ba_win_sz, u16 ssn,
					 bool update_ssn)
{
	struct ath11k_hal_reo_cmd cmd = {0};
	int ret;

	cmd.addr_lo = lower_32_bits(rx_tid->paddr);
	cmd.addr_hi = upper_32_bits(rx_tid->paddr);
	cmd.flag = HAL_REO_CMD_FLG_NEED_STATUS;
	cmd.upd0 = HAL_REO_CMD_UPD0_BA_WINDOW_SIZE;
	cmd.ba_window_size = ba_win_sz;

	if (update_ssn) {
		cmd.upd0 |= HAL_REO_CMD_UPD0_SSN;
		cmd.upd2 = FIELD_PREP(HAL_REO_CMD_UPD2_SSN, ssn);
	}

	ret = ath11k_dp_tx_send_reo_cmd(ar->ab, rx_tid,
					HAL_REO_CMD_UPDATE_RX_QUEUE, &cmd,
					NULL);
	if (ret) {
		ath11k_warn(ar->ab, "failed to update rx tid queue, tid %d (%d)\n",
			    rx_tid->tid, ret);
		return ret;
	}

	rx_tid->ba_win_sz = ba_win_sz;

	return 0;
}

static void ath11k_dp_rx_tid_mem_free(struct ath11k_base *ab,
				      const u8 *peer_mac, int vdev_id, u8 tid)
{
	struct ath11k_peer *peer;
	struct dp_rx_tid *rx_tid;

	spin_lock_bh(&ab->base_lock);

	peer = ath11k_peer_find(ab, vdev_id, peer_mac);
	if (!peer) {
		ath11k_warn(ab, "failed to find the peer to free up rx tid mem\n");
		goto unlock_exit;
	}

	rx_tid = &peer->rx_tid[tid];
	if (!rx_tid->active)
		goto unlock_exit;

	dma_unmap_single(ab->dev, rx_tid->paddr, rx_tid->size,
			 DMA_BIDIRECTIONAL);
	kfree(rx_tid->vaddr);

	rx_tid->active = false;

unlock_exit:
	spin_unlock_bh(&ab->base_lock);
}

int ath11k_peer_rx_tid_setup(struct ath11k *ar, const u8 *peer_mac, int vdev_id,
			     u8 tid, u32 ba_win_sz, u16 ssn)
{
	struct ath11k_base *ab = ar->ab;
	struct ath11k_peer *peer;
	struct dp_rx_tid *rx_tid;
	u32 hw_desc_sz;
	u32 *addr_aligned;
	void *vaddr;
	dma_addr_t paddr;
	int ret;

	spin_lock_bh(&ab->base_lock);

	peer = ath11k_peer_find(ab, vdev_id, peer_mac);
	if (!peer) {
		ath11k_warn(ab, "failed to find the peer to set up rx tid\n");
		spin_unlock_bh(&ab->base_lock);
		return -ENOENT;
	}

	rx_tid = &peer->rx_tid[tid];
	/* Update the tid queue if it is already setup */
	if (rx_tid->active) {
		paddr = rx_tid->paddr;
		ret = ath11k_peer_rx_tid_reo_update(ar, peer, rx_tid,
						    ba_win_sz, ssn, true);
		spin_unlock_bh(&ab->base_lock);
		if (ret) {
			ath11k_warn(ab, "failed to update reo for rx tid %d\n", tid);
			return ret;
		}

		ret = ath11k_wmi_peer_rx_reorder_queue_setup(ar, vdev_id,
							     peer_mac, paddr,
							     tid, 1, ba_win_sz);
		if (ret)
			ath11k_warn(ab, "failed to send wmi command to update rx reorder queue, tid :%d (%d)\n",
				    tid, ret);
		return ret;
	}

	rx_tid->tid = tid;

	rx_tid->ba_win_sz = ba_win_sz;

	/* TODO: Optimize the memory allocation for qos tid based on the
	 * the actual BA window size in REO tid update path.
	 */
	if (tid == HAL_DESC_REO_NON_QOS_TID)
		hw_desc_sz = ath11k_hal_reo_qdesc_size(ba_win_sz, tid);
	else
		hw_desc_sz = ath11k_hal_reo_qdesc_size(DP_BA_WIN_SZ_MAX, tid);

	vaddr = kzalloc(hw_desc_sz + HAL_LINK_DESC_ALIGN - 1, GFP_KERNEL);
	if (!vaddr) {
		spin_unlock_bh(&ab->base_lock);
		return -ENOMEM;
	}

	addr_aligned = PTR_ALIGN(vaddr, HAL_LINK_DESC_ALIGN);

	ath11k_hal_reo_qdesc_setup(addr_aligned, tid, ba_win_sz, ssn);

	paddr = dma_map_single(ab->dev, addr_aligned, hw_desc_sz,
			       DMA_BIDIRECTIONAL);

	ret = dma_mapping_error(ab->dev, paddr);
	if (ret) {
		spin_unlock_bh(&ab->base_lock);
		goto err_mem_free;
	}

	rx_tid->vaddr = vaddr;
	rx_tid->paddr = paddr;
	rx_tid->size = hw_desc_sz;
	rx_tid->active = true;

	spin_unlock_bh(&ab->base_lock);

	ret = ath11k_wmi_peer_rx_reorder_queue_setup(ar, vdev_id, peer_mac,
						     paddr, tid, 1, ba_win_sz);
	if (ret) {
		ath11k_warn(ar->ab, "failed to setup rx reorder queue, tid :%d (%d)\n",
			    tid, ret);
		ath11k_dp_rx_tid_mem_free(ab, peer_mac, vdev_id, tid);
	}

	return ret;

err_mem_free:
	kfree(vaddr);

	return ret;
}

int ath11k_dp_rx_ampdu_start(struct ath11k *ar,
			     struct ieee80211_ampdu_params *params)
{
	struct ath11k_base *ab = ar->ab;
	struct ath11k_sta *arsta = (void *)params->sta->drv_priv;
	int vdev_id = arsta->arvif->vdev_id;
	int ret;

	ret = ath11k_peer_rx_tid_setup(ar, params->sta->addr, vdev_id,
				       params->tid, params->buf_size,
				       params->ssn);
	if (ret)
		ath11k_warn(ab, "failed to setup rx tid %d\n", ret);

	return ret;
}

int ath11k_dp_rx_ampdu_stop(struct ath11k *ar,
			    struct ieee80211_ampdu_params *params)
{
	struct ath11k_base *ab = ar->ab;
	struct ath11k_peer *peer;
	struct ath11k_sta *arsta = (void *)params->sta->drv_priv;
	int vdev_id = arsta->arvif->vdev_id;
	dma_addr_t paddr;
	bool active;
	int ret;

	spin_lock_bh(&ab->base_lock);

	peer = ath11k_peer_find(ab, vdev_id, params->sta->addr);
	if (!peer) {
		ath11k_warn(ab, "failed to find the peer to stop rx aggregation\n");
		spin_unlock_bh(&ab->base_lock);
		return -ENOENT;
	}

	paddr = peer->rx_tid[params->tid].paddr;
	active = peer->rx_tid[params->tid].active;

	if (!active) {
		spin_unlock_bh(&ab->base_lock);
		return 0;
	}

	ret = ath11k_peer_rx_tid_reo_update(ar, peer, peer->rx_tid, 1, 0, false);
	spin_unlock_bh(&ab->base_lock);
	if (ret) {
		ath11k_warn(ab, "failed to update reo for rx tid %d: %d\n",
			    params->tid, ret);
		return ret;
	}

	ret = ath11k_wmi_peer_rx_reorder_queue_setup(ar, vdev_id,
						     params->sta->addr, paddr,
						     params->tid, 1, 1);
	if (ret)
		ath11k_warn(ab, "failed to send wmi to delete rx tid %d\n",
			    ret);

	return ret;
}

static int ath11k_get_ppdu_user_index(struct htt_ppdu_stats *ppdu_stats,
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

static int ath11k_htt_tlv_ppdu_stats_parse(struct ath11k_base *ab,
					   u16 tag, u16 len, const void *ptr,
					   void *data)
{
	struct htt_ppdu_stats_info *ppdu_info;
	struct htt_ppdu_user_stats *user_stats;
	int cur_user;
	u16 peer_id;

	ppdu_info = (struct htt_ppdu_stats_info *)data;

	switch (tag) {
	case HTT_PPDU_STATS_TAG_COMMON:
		if (len < sizeof(struct htt_ppdu_stats_common)) {
			ath11k_warn(ab, "Invalid len %d for the tag 0x%x\n",
				    len, tag);
			return -EINVAL;
		}
		memcpy((void *)&ppdu_info->ppdu_stats.common, ptr,
		       sizeof(struct htt_ppdu_stats_common));
		break;
	case HTT_PPDU_STATS_TAG_USR_RATE:
		if (len < sizeof(struct htt_ppdu_stats_user_rate)) {
			ath11k_warn(ab, "Invalid len %d for the tag 0x%x\n",
				    len, tag);
			return -EINVAL;
		}

		peer_id = ((struct htt_ppdu_stats_user_rate *)ptr)->sw_peer_id;
		cur_user = ath11k_get_ppdu_user_index(&ppdu_info->ppdu_stats,
						      peer_id);
		if (cur_user < 0)
			return -EINVAL;
		user_stats = &ppdu_info->ppdu_stats.user_stats[cur_user];
		user_stats->peer_id = peer_id;
		user_stats->is_valid_peer_id = true;
		memcpy((void *)&user_stats->rate, ptr,
		       sizeof(struct htt_ppdu_stats_user_rate));
		user_stats->tlv_flags |= BIT(tag);
		break;
	case HTT_PPDU_STATS_TAG_USR_COMPLTN_COMMON:
		if (len < sizeof(struct htt_ppdu_stats_usr_cmpltn_cmn)) {
			ath11k_warn(ab, "Invalid len %d for the tag 0x%x\n",
				    len, tag);
			return -EINVAL;
		}

		peer_id = ((struct htt_ppdu_stats_usr_cmpltn_cmn *)ptr)->sw_peer_id;
		cur_user = ath11k_get_ppdu_user_index(&ppdu_info->ppdu_stats,
						      peer_id);
		if (cur_user < 0)
			return -EINVAL;
		user_stats = &ppdu_info->ppdu_stats.user_stats[cur_user];
		user_stats->peer_id = peer_id;
		user_stats->is_valid_peer_id = true;
		memcpy((void *)&user_stats->cmpltn_cmn, ptr,
		       sizeof(struct htt_ppdu_stats_usr_cmpltn_cmn));
		user_stats->tlv_flags |= BIT(tag);
		break;
	case HTT_PPDU_STATS_TAG_USR_COMPLTN_ACK_BA_STATUS:
		if (len <
		    sizeof(struct htt_ppdu_stats_usr_cmpltn_ack_ba_status)) {
			ath11k_warn(ab, "Invalid len %d for the tag 0x%x\n",
				    len, tag);
			return -EINVAL;
		}

		peer_id =
		((struct htt_ppdu_stats_usr_cmpltn_ack_ba_status *)ptr)->sw_peer_id;
		cur_user = ath11k_get_ppdu_user_index(&ppdu_info->ppdu_stats,
						      peer_id);
		if (cur_user < 0)
			return -EINVAL;
		user_stats = &ppdu_info->ppdu_stats.user_stats[cur_user];
		user_stats->peer_id = peer_id;
		user_stats->is_valid_peer_id = true;
		memcpy((void *)&user_stats->ack_ba, ptr,
		       sizeof(struct htt_ppdu_stats_usr_cmpltn_ack_ba_status));
		user_stats->tlv_flags |= BIT(tag);
		break;
	}
	return 0;
}

int ath11k_dp_htt_tlv_iter(struct ath11k_base *ab, const void *ptr, size_t len,
			   int (*iter)(struct ath11k_base *ar, u16 tag, u16 len,
				       const void *ptr, void *data),
			   void *data)
{
	const struct htt_tlv *tlv;
	const void *begin = ptr;
	u16 tlv_tag, tlv_len;
	int ret = -EINVAL;

	while (len > 0) {
		if (len < sizeof(*tlv)) {
			ath11k_err(ab, "htt tlv parse failure at byte %zd (%zu bytes left, %zu expected)\n",
				   ptr - begin, len, sizeof(*tlv));
			return -EINVAL;
		}
		tlv = (struct htt_tlv *)ptr;
		tlv_tag = FIELD_GET(HTT_TLV_TAG, tlv->header);
		tlv_len = FIELD_GET(HTT_TLV_LEN, tlv->header);
		ptr += sizeof(*tlv);
		len -= sizeof(*tlv);

		if (tlv_len > len) {
			ath11k_err(ab, "htt tlv parse failure of tag %hhu at byte %zd (%zu bytes left, %hhu expected)\n",
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

static inline u32 ath11k_he_gi_to_nl80211_he_gi(u8 sgi)
{
	u32 ret = 0;

	switch (sgi) {
	case RX_MSDU_START_SGI_0_8_US:
		ret = NL80211_RATE_INFO_HE_GI_0_8;
		break;
	case RX_MSDU_START_SGI_1_6_US:
		ret = NL80211_RATE_INFO_HE_GI_1_6;
		break;
	case RX_MSDU_START_SGI_3_2_US:
		ret = NL80211_RATE_INFO_HE_GI_3_2;
		break;
	}

	return ret;
}

static void
ath11k_update_per_peer_tx_stats(struct ath11k *ar,
				struct htt_ppdu_stats *ppdu_stats, u8 user)
{
	struct ath11k_base *ab = ar->ab;
	struct ath11k_peer *peer;
	struct ieee80211_sta *sta;
	struct ath11k_sta *arsta;
	struct htt_ppdu_stats_user_rate *user_rate;
	struct ath11k_per_peer_tx_stats *peer_stats = &ar->peer_tx_stats;
	struct htt_ppdu_user_stats *usr_stats = &ppdu_stats->user_stats[user];
	struct htt_ppdu_stats_common *common = &ppdu_stats->common;
	int ret;
	u8 flags, mcs, nss, bw, sgi, dcm, rate_idx = 0;
	u32 succ_bytes = 0;
	u16 rate = 0, succ_pkts = 0;
	u32 tx_duration = 0;
	u8 tid = HTT_PPDU_STATS_NON_QOS_TID;
	bool is_ampdu = false;

	if (!usr_stats)
		return;

	if (!(usr_stats->tlv_flags & BIT(HTT_PPDU_STATS_TAG_USR_RATE)))
		return;

	if (usr_stats->tlv_flags & BIT(HTT_PPDU_STATS_TAG_USR_COMPLTN_COMMON))
		is_ampdu =
			HTT_USR_CMPLTN_IS_AMPDU(usr_stats->cmpltn_cmn.flags);

	if (usr_stats->tlv_flags &
	    BIT(HTT_PPDU_STATS_TAG_USR_COMPLTN_ACK_BA_STATUS)) {
		succ_bytes = usr_stats->ack_ba.success_bytes;
		succ_pkts = FIELD_GET(HTT_PPDU_STATS_ACK_BA_INFO_NUM_MSDU_M,
				      usr_stats->ack_ba.info);
		tid = FIELD_GET(HTT_PPDU_STATS_ACK_BA_INFO_TID_NUM,
				usr_stats->ack_ba.info);
	}

	if (common->fes_duration_us)
		tx_duration = common->fes_duration_us;

	user_rate = &usr_stats->rate;
	flags = HTT_USR_RATE_PREAMBLE(user_rate->rate_flags);
	bw = HTT_USR_RATE_BW(user_rate->rate_flags) - 2;
	nss = HTT_USR_RATE_NSS(user_rate->rate_flags) + 1;
	mcs = HTT_USR_RATE_MCS(user_rate->rate_flags);
	sgi = HTT_USR_RATE_GI(user_rate->rate_flags);
	dcm = HTT_USR_RATE_DCM(user_rate->rate_flags);

	/* Note: If host configured fixed rates and in some other special
	 * cases, the broadcast/management frames are sent in different rates.
	 * Firmware rate's control to be skipped for this?
	 */

	if (flags == WMI_RATE_PREAMBLE_HE && mcs > 11) {
		ath11k_warn(ab, "Invalid HE mcs %hhd peer stats",  mcs);
		return;
	}

	if (flags == WMI_RATE_PREAMBLE_HE && mcs > ATH11K_HE_MCS_MAX) {
		ath11k_warn(ab, "Invalid HE mcs %hhd peer stats",  mcs);
		return;
	}

	if (flags == WMI_RATE_PREAMBLE_VHT && mcs > ATH11K_VHT_MCS_MAX) {
		ath11k_warn(ab, "Invalid VHT mcs %hhd peer stats",  mcs);
		return;
	}

	if (flags == WMI_RATE_PREAMBLE_HT && (mcs > ATH11K_HT_MCS_MAX || nss < 1)) {
		ath11k_warn(ab, "Invalid HT mcs %hhd nss %hhd peer stats",
			    mcs, nss);
		return;
	}

	if (flags == WMI_RATE_PREAMBLE_CCK || flags == WMI_RATE_PREAMBLE_OFDM) {
		ret = ath11k_mac_hw_ratecode_to_legacy_rate(mcs,
							    flags,
							    &rate_idx,
							    &rate);
		if (ret < 0)
			return;
	}

	rcu_read_lock();
	spin_lock_bh(&ab->base_lock);
	peer = ath11k_peer_find_by_id(ab, usr_stats->peer_id);

	if (!peer || !peer->sta) {
		spin_unlock_bh(&ab->base_lock);
		rcu_read_unlock();
		return;
	}

	sta = peer->sta;
	arsta = (struct ath11k_sta *)sta->drv_priv;

	memset(&arsta->txrate, 0, sizeof(arsta->txrate));

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
		arsta->txrate.he_gi = ath11k_he_gi_to_nl80211_he_gi(sgi);
		arsta->txrate.he_ru_alloc = ath11k_he_ru_tones_to_nl80211_he_ru_alloc(
						(user_rate->ru_end -
						 user_rate->ru_start) + 1);
		break;
	}

	arsta->txrate.nss = nss;
	arsta->txrate.bw = ath11k_mac_bw_to_mac80211_bw(bw);
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

		if (ath11k_debug_is_extd_tx_stats_enabled(ar))
			ath11k_accumulate_per_peer_tx_stats(arsta,
							    peer_stats, rate_idx);
	}

	spin_unlock_bh(&ab->base_lock);
	rcu_read_unlock();
}

static void ath11k_htt_update_ppdu_stats(struct ath11k *ar,
					 struct htt_ppdu_stats *ppdu_stats)
{
	u8 user;

	for (user = 0; user < HTT_PPDU_STATS_MAX_USERS - 1; user++)
		ath11k_update_per_peer_tx_stats(ar, ppdu_stats, user);
}

static
struct htt_ppdu_stats_info *ath11k_dp_htt_get_ppdu_desc(struct ath11k *ar,
							u32 ppdu_id)
{
	struct htt_ppdu_stats_info *ppdu_info;

	spin_lock_bh(&ar->data_lock);
	if (!list_empty(&ar->ppdu_stats_info)) {
		list_for_each_entry(ppdu_info, &ar->ppdu_stats_info, list) {
			if (ppdu_info->ppdu_id == ppdu_id) {
				spin_unlock_bh(&ar->data_lock);
				return ppdu_info;
			}
		}

		if (ar->ppdu_stat_list_depth > HTT_PPDU_DESC_MAX_DEPTH) {
			ppdu_info = list_first_entry(&ar->ppdu_stats_info,
						     typeof(*ppdu_info), list);
			list_del(&ppdu_info->list);
			ar->ppdu_stat_list_depth--;
			ath11k_htt_update_ppdu_stats(ar, &ppdu_info->ppdu_stats);
			kfree(ppdu_info);
		}
	}
	spin_unlock_bh(&ar->data_lock);

	ppdu_info = kzalloc(sizeof(*ppdu_info), GFP_KERNEL);
	if (!ppdu_info)
		return NULL;

	spin_lock_bh(&ar->data_lock);
	list_add_tail(&ppdu_info->list, &ar->ppdu_stats_info);
	ar->ppdu_stat_list_depth++;
	spin_unlock_bh(&ar->data_lock);

	return ppdu_info;
}

static int ath11k_htt_pull_ppdu_stats(struct ath11k_base *ab,
				      struct sk_buff *skb)
{
	struct ath11k_htt_ppdu_stats_msg *msg;
	struct htt_ppdu_stats_info *ppdu_info;
	struct ath11k *ar;
	int ret;
	u8 pdev_id;
	u32 ppdu_id, len;

	msg = (struct ath11k_htt_ppdu_stats_msg *)skb->data;
	len = FIELD_GET(HTT_T2H_PPDU_STATS_INFO_PAYLOAD_SIZE, msg->info);
	pdev_id = FIELD_GET(HTT_T2H_PPDU_STATS_INFO_PDEV_ID, msg->info);
	ppdu_id = msg->ppdu_id;

	rcu_read_lock();
	ar = ath11k_mac_get_ar_by_pdev_id(ab, pdev_id);
	if (!ar) {
		ret = -EINVAL;
		goto exit;
	}

	if (ath11k_debug_is_pktlog_lite_mode_enabled(ar))
		trace_ath11k_htt_ppdu_stats(ar, skb->data, len);

	ppdu_info = ath11k_dp_htt_get_ppdu_desc(ar, ppdu_id);
	if (!ppdu_info) {
		ret = -EINVAL;
		goto exit;
	}

	ppdu_info->ppdu_id = ppdu_id;
	ret = ath11k_dp_htt_tlv_iter(ab, msg->data, len,
				     ath11k_htt_tlv_ppdu_stats_parse,
				     (void *)ppdu_info);
	if (ret) {
		ath11k_warn(ab, "Failed to parse tlv %d\n", ret);
		goto exit;
	}

exit:
	rcu_read_unlock();

	return ret;
}

static void ath11k_htt_pktlog(struct ath11k_base *ab, struct sk_buff *skb)
{
	struct htt_pktlog_msg *data = (struct htt_pktlog_msg *)skb->data;
	struct ath_pktlog_hdr *hdr = (struct ath_pktlog_hdr *)data;
	struct ath11k *ar;
	u8 pdev_id;

	pdev_id = FIELD_GET(HTT_T2H_PPDU_STATS_INFO_PDEV_ID, data->hdr);
	ar = ath11k_mac_get_ar_by_pdev_id(ab, pdev_id);
	if (!ar) {
		ath11k_warn(ab, "invalid pdev id %d on htt pktlog\n", pdev_id);
		return;
	}

	trace_ath11k_htt_pktlog(ar, data->payload, hdr->size);
}

void ath11k_dp_htt_htc_t2h_msg_handler(struct ath11k_base *ab,
				       struct sk_buff *skb)
{
	struct ath11k_dp *dp = &ab->dp;
	struct htt_resp_msg *resp = (struct htt_resp_msg *)skb->data;
	enum htt_t2h_msg_type type = FIELD_GET(HTT_T2H_MSG_TYPE, *(u32 *)resp);
	u16 peer_id;
	u8 vdev_id;
	u8 mac_addr[ETH_ALEN];
	u16 peer_mac_h16;
	u16 ast_hash;

	ath11k_dbg(ab, ATH11K_DBG_DP_HTT, "dp_htt rx msg type :0x%0x\n", type);

	switch (type) {
	case HTT_T2H_MSG_TYPE_VERSION_CONF:
		dp->htt_tgt_ver_major = FIELD_GET(HTT_T2H_VERSION_CONF_MAJOR,
						  resp->version_msg.version);
		dp->htt_tgt_ver_minor = FIELD_GET(HTT_T2H_VERSION_CONF_MINOR,
						  resp->version_msg.version);
		complete(&dp->htt_tgt_version_received);
		break;
	case HTT_T2H_MSG_TYPE_PEER_MAP:
		vdev_id = FIELD_GET(HTT_T2H_PEER_MAP_INFO_VDEV_ID,
				    resp->peer_map_ev.info);
		peer_id = FIELD_GET(HTT_T2H_PEER_MAP_INFO_PEER_ID,
				    resp->peer_map_ev.info);
		peer_mac_h16 = FIELD_GET(HTT_T2H_PEER_MAP_INFO1_MAC_ADDR_H16,
					 resp->peer_map_ev.info1);
		ath11k_dp_get_mac_addr(resp->peer_map_ev.mac_addr_l32,
				       peer_mac_h16, mac_addr);
		ast_hash = FIELD_GET(HTT_T2H_PEER_MAP_INFO2_AST_HASH_VAL,
				     resp->peer_map_ev.info2);
		ath11k_peer_map_event(ab, vdev_id, peer_id, mac_addr, ast_hash);
		break;
	case HTT_T2H_MSG_TYPE_PEER_UNMAP:
		peer_id = FIELD_GET(HTT_T2H_PEER_UNMAP_INFO_PEER_ID,
				    resp->peer_unmap_ev.info);
		ath11k_peer_unmap_event(ab, peer_id);
		break;
	case HTT_T2H_MSG_TYPE_PPDU_STATS_IND:
		ath11k_htt_pull_ppdu_stats(ab, skb);
		break;
	case HTT_T2H_MSG_TYPE_EXT_STATS_CONF:
		ath11k_dbg_htt_ext_stats_handler(ab, skb);
		break;
	case HTT_T2H_MSG_TYPE_PKTLOG:
		ath11k_htt_pktlog(ab, skb);
		break;
	default:
		ath11k_warn(ab, "htt event %d not handled\n", type);
		break;
	}

	dev_kfree_skb_any(skb);
}

static int ath11k_dp_rx_msdu_coalesce(struct ath11k *ar,
				      struct sk_buff_head *msdu_list,
				      struct sk_buff *first, struct sk_buff *last,
				      u8 l3pad_bytes, int msdu_len)
{
	struct sk_buff *skb;
	struct ath11k_skb_rxcb *rxcb = ATH11K_SKB_RXCB(first);
	int buf_first_hdr_len, buf_first_len;
	struct hal_rx_desc *ldesc;
	int space_extra;
	int rem_len;
	int buf_len;

	/* As the msdu is spread across multiple rx buffers,
	 * find the offset to the start of msdu for computing
	 * the length of the msdu in the first buffer.
	 */
	buf_first_hdr_len = HAL_RX_DESC_SIZE + l3pad_bytes;
	buf_first_len = DP_RX_BUFFER_SIZE - buf_first_hdr_len;

	if (WARN_ON_ONCE(msdu_len <= buf_first_len)) {
		skb_put(first, buf_first_hdr_len + msdu_len);
		skb_pull(first, buf_first_hdr_len);
		return 0;
	}

	ldesc = (struct hal_rx_desc *)last->data;
	rxcb->is_first_msdu = ath11k_dp_rx_h_msdu_end_first_msdu(ldesc);
	rxcb->is_last_msdu = ath11k_dp_rx_h_msdu_end_last_msdu(ldesc);

	/* MSDU spans over multiple buffers because the length of the MSDU
	 * exceeds DP_RX_BUFFER_SIZE - HAL_RX_DESC_SIZE. So assume the data
	 * in the first buf is of length DP_RX_BUFFER_SIZE - HAL_RX_DESC_SIZE.
	 */
	skb_put(first, DP_RX_BUFFER_SIZE);
	skb_pull(first, buf_first_hdr_len);

	/* When an MSDU spread over multiple buffers attention, MSDU_END and
	 * MPDU_END tlvs are valid only in the last buffer. Copy those tlvs.
	 */
	ath11k_dp_rx_desc_end_tlv_copy(rxcb->rx_desc, ldesc);

	space_extra = msdu_len - (buf_first_len + skb_tailroom(first));
	if (space_extra > 0 &&
	    (pskb_expand_head(first, 0, space_extra, GFP_ATOMIC) < 0)) {
		/* Free up all buffers of the MSDU */
		while ((skb = __skb_dequeue(msdu_list)) != NULL) {
			rxcb = ATH11K_SKB_RXCB(skb);
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
		rxcb = ATH11K_SKB_RXCB(skb);
		if (rxcb->is_continuation)
			buf_len = DP_RX_BUFFER_SIZE - HAL_RX_DESC_SIZE;
		else
			buf_len = rem_len;

		if (buf_len > (DP_RX_BUFFER_SIZE - HAL_RX_DESC_SIZE)) {
			WARN_ON_ONCE(1);
			dev_kfree_skb_any(skb);
			return -EINVAL;
		}

		skb_put(skb, buf_len + HAL_RX_DESC_SIZE);
		skb_pull(skb, HAL_RX_DESC_SIZE);
		skb_copy_from_linear_data(skb, skb_put(first, buf_len),
					  buf_len);
		dev_kfree_skb_any(skb);

		rem_len -= buf_len;
		if (!rxcb->is_continuation)
			break;
	}

	return 0;
}

static struct sk_buff *ath11k_dp_rx_get_msdu_last_buf(struct sk_buff_head *msdu_list,
						      struct sk_buff *first)
{
	struct sk_buff *skb;
	struct ath11k_skb_rxcb *rxcb = ATH11K_SKB_RXCB(first);

	if (!rxcb->is_continuation)
		return first;

	skb_queue_walk(msdu_list, skb) {
		rxcb = ATH11K_SKB_RXCB(skb);
		if (!rxcb->is_continuation)
			return skb;
	}

	return NULL;
}

static int ath11k_dp_rx_retrieve_amsdu(struct ath11k *ar,
				       struct sk_buff_head *msdu_list,
				       struct sk_buff_head *amsdu_list)
{
	struct sk_buff *msdu = skb_peek(msdu_list);
	struct sk_buff *last_buf;
	struct ath11k_skb_rxcb *rxcb;
	struct ieee80211_hdr *hdr;
	struct hal_rx_desc *rx_desc, *lrx_desc;
	u16 msdu_len;
	u8 l3_pad_bytes;
	u8 *hdr_status;
	int ret;

	if (!msdu)
		return -ENOENT;

	rx_desc = (struct hal_rx_desc *)msdu->data;
	hdr_status = ath11k_dp_rx_h_80211_hdr(rx_desc);
	hdr = (struct ieee80211_hdr *)hdr_status;
	/* Process only data frames */
	if (!ieee80211_is_data(hdr->frame_control)) {
		__skb_unlink(msdu, msdu_list);
		dev_kfree_skb_any(msdu);
		return -EINVAL;
	}

	do {
		__skb_unlink(msdu, msdu_list);
		last_buf = ath11k_dp_rx_get_msdu_last_buf(msdu_list, msdu);
		if (!last_buf) {
			ath11k_warn(ar->ab,
				    "No valid Rx buffer to access Atten/MSDU_END/MPDU_END tlvs\n");
			ret = -EIO;
			goto free_out;
		}

		rx_desc = (struct hal_rx_desc *)msdu->data;
		lrx_desc = (struct hal_rx_desc *)last_buf->data;

		if (!ath11k_dp_rx_h_attn_msdu_done(lrx_desc)) {
			ath11k_warn(ar->ab, "msdu_done bit in attention is not set\n");
			ret = -EIO;
			goto free_out;
		}

		rxcb = ATH11K_SKB_RXCB(msdu);
		rxcb->rx_desc = rx_desc;
		msdu_len = ath11k_dp_rx_h_msdu_start_msdu_len(rx_desc);
		l3_pad_bytes = ath11k_dp_rx_h_msdu_end_l3pad(lrx_desc);

		if (!rxcb->is_continuation) {
			skb_put(msdu, HAL_RX_DESC_SIZE + l3_pad_bytes + msdu_len);
			skb_pull(msdu, HAL_RX_DESC_SIZE + l3_pad_bytes);
		} else {
			ret = ath11k_dp_rx_msdu_coalesce(ar, msdu_list,
							 msdu, last_buf,
							 l3_pad_bytes, msdu_len);
			if (ret) {
				ath11k_warn(ar->ab,
					    "failed to coalesce msdu rx buffer%d\n", ret);
				goto free_out;
			}
		}
		__skb_queue_tail(amsdu_list, msdu);

		/* Should we also consider msdu_cnt from mpdu_meta while
		 * preparing amsdu list?
		 */
		if (rxcb->is_last_msdu)
			break;
	} while ((msdu = skb_peek(msdu_list)) != NULL);

	return 0;

free_out:
	dev_kfree_skb_any(msdu);
	__skb_queue_purge(amsdu_list);

	return ret;
}

static void ath11k_dp_rx_h_csum_offload(struct sk_buff *msdu)
{
	struct ath11k_skb_rxcb *rxcb = ATH11K_SKB_RXCB(msdu);
	bool ip_csum_fail, l4_csum_fail;

	ip_csum_fail = ath11k_dp_rx_h_attn_ip_cksum_fail(rxcb->rx_desc);
	l4_csum_fail = ath11k_dp_rx_h_attn_l4_cksum_fail(rxcb->rx_desc);

	msdu->ip_summed = (ip_csum_fail || l4_csum_fail) ?
			  CHECKSUM_NONE : CHECKSUM_UNNECESSARY;
}

static int ath11k_dp_rx_crypto_mic_len(struct ath11k *ar,
				       enum hal_encrypt_type enctype)
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

	ath11k_warn(ar->ab, "unsupported encryption type %d for mic len\n", enctype);
	return 0;
}

static int ath11k_dp_rx_crypto_param_len(struct ath11k *ar,
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

	ath11k_warn(ar->ab, "unsupported encryption type %d\n", enctype);
	return 0;
}

static int ath11k_dp_rx_crypto_icv_len(struct ath11k *ar,
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

	ath11k_warn(ar->ab, "unsupported encryption type %d\n", enctype);
	return 0;
}

static void ath11k_dp_rx_h_undecap_nwifi(struct ath11k *ar,
					 struct sk_buff *msdu,
					 u8 *first_hdr,
					 enum hal_encrypt_type enctype,
					 struct ieee80211_rx_status *status)
{
	struct ieee80211_hdr *hdr;
	size_t hdr_len;
	u8 da[ETH_ALEN];
	u8 sa[ETH_ALEN];

	/* pull decapped header and copy SA & DA */
	hdr = (struct ieee80211_hdr *)msdu->data;
	ether_addr_copy(da, ieee80211_get_DA(hdr));
	ether_addr_copy(sa, ieee80211_get_SA(hdr));
	skb_pull(msdu, ieee80211_hdrlen(hdr->frame_control));

	/* push original 802.11 header */
	hdr = (struct ieee80211_hdr *)first_hdr;
	hdr_len = ieee80211_hdrlen(hdr->frame_control);

	if (!(status->flag & RX_FLAG_IV_STRIPPED)) {
		memcpy(skb_push(msdu,
				ath11k_dp_rx_crypto_param_len(ar, enctype)),
		       (void *)hdr + hdr_len,
		       ath11k_dp_rx_crypto_param_len(ar, enctype));
	}

	memcpy(skb_push(msdu, hdr_len), hdr, hdr_len);

	/* original 802.11 header has a different DA and in
	 * case of 4addr it may also have different SA
	 */
	hdr = (struct ieee80211_hdr *)msdu->data;
	ether_addr_copy(ieee80211_get_DA(hdr), da);
	ether_addr_copy(ieee80211_get_SA(hdr), sa);
}

static void ath11k_dp_rx_h_undecap_raw(struct ath11k *ar, struct sk_buff *msdu,
				       enum hal_encrypt_type enctype,
				       struct ieee80211_rx_status *status,
				       bool decrypted)
{
	struct ath11k_skb_rxcb *rxcb = ATH11K_SKB_RXCB(msdu);
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
			 ath11k_dp_rx_crypto_mic_len(ar, enctype));

		skb_trim(msdu, msdu->len -
			 ath11k_dp_rx_crypto_icv_len(ar, enctype));
	} else {
		/* MIC */
		if (status->flag & RX_FLAG_MIC_STRIPPED)
			skb_trim(msdu, msdu->len -
				 ath11k_dp_rx_crypto_mic_len(ar, enctype));

		/* ICV */
		if (status->flag & RX_FLAG_ICV_STRIPPED)
			skb_trim(msdu, msdu->len -
				 ath11k_dp_rx_crypto_icv_len(ar, enctype));
	}

	/* MMIC */
	if ((status->flag & RX_FLAG_MMIC_STRIPPED) &&
	    !ieee80211_has_morefrags(hdr->frame_control) &&
	    enctype == HAL_ENCRYPT_TYPE_TKIP_MIC)
		skb_trim(msdu, msdu->len - IEEE80211_CCMP_MIC_LEN);

	/* Head */
	if (status->flag & RX_FLAG_IV_STRIPPED) {
		hdr_len = ieee80211_hdrlen(hdr->frame_control);
		crypto_len = ath11k_dp_rx_crypto_param_len(ar, enctype);

		memmove((void *)msdu->data + crypto_len,
			(void *)msdu->data, hdr_len);
		skb_pull(msdu, crypto_len);
	}
}

static void *ath11k_dp_rx_h_find_rfc1042(struct ath11k *ar,
					 struct sk_buff *msdu,
					 enum hal_encrypt_type enctype)
{
	struct ath11k_skb_rxcb *rxcb = ATH11K_SKB_RXCB(msdu);
	struct ieee80211_hdr *hdr;
	size_t hdr_len, crypto_len;
	void *rfc1042;
	bool is_amsdu;

	is_amsdu = !(rxcb->is_first_msdu && rxcb->is_last_msdu);
	hdr = (struct ieee80211_hdr *)ath11k_dp_rx_h_80211_hdr(rxcb->rx_desc);
	rfc1042 = hdr;

	if (rxcb->is_first_msdu) {
		hdr_len = ieee80211_hdrlen(hdr->frame_control);
		crypto_len = ath11k_dp_rx_crypto_param_len(ar, enctype);

		rfc1042 += hdr_len + crypto_len;
	}

	if (is_amsdu)
		rfc1042 += sizeof(struct ath11k_dp_amsdu_subframe_hdr);

	return rfc1042;
}

static void ath11k_dp_rx_h_undecap_eth(struct ath11k *ar,
				       struct sk_buff *msdu,
				       u8 *first_hdr,
				       enum hal_encrypt_type enctype,
				       struct ieee80211_rx_status *status)
{
	struct ieee80211_hdr *hdr;
	struct ethhdr *eth;
	size_t hdr_len;
	u8 da[ETH_ALEN];
	u8 sa[ETH_ALEN];
	void *rfc1042;

	rfc1042 = ath11k_dp_rx_h_find_rfc1042(ar, msdu, enctype);
	if (WARN_ON_ONCE(!rfc1042))
		return;

	/* pull decapped header and copy SA & DA */
	eth = (struct ethhdr *)msdu->data;
	ether_addr_copy(da, eth->h_dest);
	ether_addr_copy(sa, eth->h_source);
	skb_pull(msdu, sizeof(struct ethhdr));

	/* push rfc1042/llc/snap */
	memcpy(skb_push(msdu, sizeof(struct ath11k_dp_rfc1042_hdr)), rfc1042,
	       sizeof(struct ath11k_dp_rfc1042_hdr));

	/* push original 802.11 header */
	hdr = (struct ieee80211_hdr *)first_hdr;
	hdr_len = ieee80211_hdrlen(hdr->frame_control);

	if (!(status->flag & RX_FLAG_IV_STRIPPED)) {
		memcpy(skb_push(msdu,
				ath11k_dp_rx_crypto_param_len(ar, enctype)),
		       (void *)hdr + hdr_len,
		       ath11k_dp_rx_crypto_param_len(ar, enctype));
	}

	memcpy(skb_push(msdu, hdr_len), hdr, hdr_len);

	/* original 802.11 header has a different DA and in
	 * case of 4addr it may also have different SA
	 */
	hdr = (struct ieee80211_hdr *)msdu->data;
	ether_addr_copy(ieee80211_get_DA(hdr), da);
	ether_addr_copy(ieee80211_get_SA(hdr), sa);
}

static void ath11k_dp_rx_h_undecap(struct ath11k *ar, struct sk_buff *msdu,
				   struct hal_rx_desc *rx_desc,
				   enum hal_encrypt_type enctype,
				   struct ieee80211_rx_status *status,
				   bool decrypted)
{
	u8 *first_hdr;
	u8 decap;

	first_hdr = ath11k_dp_rx_h_80211_hdr(rx_desc);
	decap = ath11k_dp_rx_h_mpdu_start_decap_type(rx_desc);

	switch (decap) {
	case DP_RX_DECAP_TYPE_NATIVE_WIFI:
		ath11k_dp_rx_h_undecap_nwifi(ar, msdu, first_hdr,
					     enctype, status);
		break;
	case DP_RX_DECAP_TYPE_RAW:
		ath11k_dp_rx_h_undecap_raw(ar, msdu, enctype, status,
					   decrypted);
		break;
	case DP_RX_DECAP_TYPE_ETHERNET2_DIX:
		ath11k_dp_rx_h_undecap_eth(ar, msdu, first_hdr,
					   enctype, status);
		break;
	case DP_RX_DECAP_TYPE_8023:
		/* TODO: Handle undecap for these formats */
		break;
	}
}

static void ath11k_dp_rx_h_mpdu(struct ath11k *ar,
				struct sk_buff_head *amsdu_list,
				struct hal_rx_desc *rx_desc,
				struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_hdr *hdr;
	enum hal_encrypt_type enctype;
	struct sk_buff *last_msdu;
	struct sk_buff *msdu;
	struct ath11k_skb_rxcb *last_rxcb;
	bool is_decrypted;
	u32 err_bitmap;
	u8 *qos;

	if (skb_queue_empty(amsdu_list))
		return;

	hdr = (struct ieee80211_hdr *)ath11k_dp_rx_h_80211_hdr(rx_desc);

	/* Each A-MSDU subframe will use the original header as the base and be
	 * reported as a separate MSDU so strip the A-MSDU bit from QoS Ctl.
	 */
	if (ieee80211_is_data_qos(hdr->frame_control)) {
		qos = ieee80211_get_qos_ctl(hdr);
		qos[0] &= ~IEEE80211_QOS_CTL_A_MSDU_PRESENT;
	}

	is_decrypted = ath11k_dp_rx_h_attn_is_decrypted(rx_desc);
	enctype = ath11k_dp_rx_h_mpdu_start_enctype(rx_desc);

	/* Some attention flags are valid only in the last MSDU. */
	last_msdu = skb_peek_tail(amsdu_list);
	last_rxcb = ATH11K_SKB_RXCB(last_msdu);

	err_bitmap = ath11k_dp_rx_h_attn_mpdu_err(last_rxcb->rx_desc);

	/* Clear per-MPDU flags while leaving per-PPDU flags intact. */
	rx_status->flag &= ~(RX_FLAG_FAILED_FCS_CRC |
			     RX_FLAG_MMIC_ERROR |
			     RX_FLAG_DECRYPTED |
			     RX_FLAG_IV_STRIPPED |
			     RX_FLAG_MMIC_STRIPPED);

	if (err_bitmap & DP_RX_MPDU_ERR_FCS)
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (err_bitmap & DP_RX_MPDU_ERR_TKIP_MIC)
		rx_status->flag |= RX_FLAG_MMIC_ERROR;

	if (is_decrypted)
		rx_status->flag |= RX_FLAG_DECRYPTED | RX_FLAG_MMIC_STRIPPED |
				   RX_FLAG_MIC_STRIPPED | RX_FLAG_ICV_STRIPPED;

	skb_queue_walk(amsdu_list, msdu) {
		ath11k_dp_rx_h_csum_offload(msdu);
		ath11k_dp_rx_h_undecap(ar, msdu, rx_desc,
				       enctype, rx_status, is_decrypted);
	}
}

static void ath11k_dp_rx_h_rate(struct ath11k *ar, struct hal_rx_desc *rx_desc,
				struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_supported_band *sband;
	enum rx_msdu_start_pkt_type pkt_type;
	u8 bw;
	u8 rate_mcs, nss;
	u8 sgi;
	bool is_cck;

	pkt_type = ath11k_dp_rx_h_msdu_start_pkt_type(rx_desc);
	bw = ath11k_dp_rx_h_msdu_start_rx_bw(rx_desc);
	rate_mcs = ath11k_dp_rx_h_msdu_start_rate_mcs(rx_desc);
	nss = ath11k_dp_rx_h_msdu_start_nss(rx_desc);
	sgi = ath11k_dp_rx_h_msdu_start_sgi(rx_desc);

	switch (pkt_type) {
	case RX_MSDU_START_PKT_TYPE_11A:
	case RX_MSDU_START_PKT_TYPE_11B:
		is_cck = (pkt_type == RX_MSDU_START_PKT_TYPE_11B);
		sband = &ar->mac.sbands[rx_status->band];
		rx_status->rate_idx = ath11k_mac_hw_rate_to_idx(sband, rate_mcs,
								is_cck);
		break;
	case RX_MSDU_START_PKT_TYPE_11N:
		rx_status->encoding = RX_ENC_HT;
		if (rate_mcs > ATH11K_HT_MCS_MAX) {
			ath11k_warn(ar->ab,
				    "Received with invalid mcs in HT mode %d\n",
				     rate_mcs);
			break;
		}
		rx_status->rate_idx = rate_mcs + (8 * (nss - 1));
		if (sgi)
			rx_status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		rx_status->bw = ath11k_mac_bw_to_mac80211_bw(bw);
		break;
	case RX_MSDU_START_PKT_TYPE_11AC:
		rx_status->encoding = RX_ENC_VHT;
		rx_status->rate_idx = rate_mcs;
		if (rate_mcs > ATH11K_VHT_MCS_MAX) {
			ath11k_warn(ar->ab,
				    "Received with invalid mcs in VHT mode %d\n",
				     rate_mcs);
			break;
		}
		rx_status->nss = nss;
		if (sgi)
			rx_status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		rx_status->bw = ath11k_mac_bw_to_mac80211_bw(bw);
		break;
	case RX_MSDU_START_PKT_TYPE_11AX:
		rx_status->rate_idx = rate_mcs;
		if (rate_mcs > ATH11K_HE_MCS_MAX) {
			ath11k_warn(ar->ab,
				    "Received with invalid mcs in HE mode %d\n",
				    rate_mcs);
			break;
		}
		rx_status->encoding = RX_ENC_HE;
		rx_status->nss = nss;
		rx_status->he_gi = ath11k_he_gi_to_nl80211_he_gi(sgi);
		rx_status->bw = ath11k_mac_bw_to_mac80211_bw(bw);
		break;
	}
}

static void ath11k_dp_rx_h_ppdu(struct ath11k *ar, struct hal_rx_desc *rx_desc,
				struct ieee80211_rx_status *rx_status)
{
	u8 channel_num;

	rx_status->freq = 0;
	rx_status->rate_idx = 0;
	rx_status->nss = 0;
	rx_status->encoding = RX_ENC_LEGACY;
	rx_status->bw = RATE_INFO_BW_20;

	rx_status->flag |= RX_FLAG_NO_SIGNAL_VAL;

	channel_num = ath11k_dp_rx_h_msdu_start_freq(rx_desc);

	if (channel_num >= 1 && channel_num <= 14) {
		rx_status->band = NL80211_BAND_2GHZ;
	} else if (channel_num >= 36 && channel_num <= 173) {
		rx_status->band = NL80211_BAND_5GHZ;
	} else {
		ath11k_warn(ar->ab, "Unsupported Channel info received %d\n",
			    channel_num);
		return;
	}

	rx_status->freq = ieee80211_channel_to_frequency(channel_num,
							 rx_status->band);

	ath11k_dp_rx_h_rate(ar, rx_desc, rx_status);
}

static void ath11k_dp_rx_process_amsdu(struct ath11k *ar,
				       struct sk_buff_head *amsdu_list,
				       struct ieee80211_rx_status *rx_status)
{
	struct sk_buff *first;
	struct ath11k_skb_rxcb *rxcb;
	struct hal_rx_desc *rx_desc;
	bool first_mpdu;

	if (skb_queue_empty(amsdu_list))
		return;

	first = skb_peek(amsdu_list);
	rxcb = ATH11K_SKB_RXCB(first);
	rx_desc = rxcb->rx_desc;

	first_mpdu = ath11k_dp_rx_h_attn_first_mpdu(rx_desc);
	if (first_mpdu)
		ath11k_dp_rx_h_ppdu(ar, rx_desc, rx_status);

	ath11k_dp_rx_h_mpdu(ar, amsdu_list, rx_desc, rx_status);
}

static char *ath11k_print_get_tid(struct ieee80211_hdr *hdr, char *out,
				  size_t size)
{
	u8 *qc;
	int tid;

	if (!ieee80211_is_data_qos(hdr->frame_control))
		return "";

	qc = ieee80211_get_qos_ctl(hdr);
	tid = *qc & IEEE80211_QOS_CTL_TID_MASK;
	snprintf(out, size, "tid %d", tid);

	return out;
}

static void ath11k_dp_rx_deliver_msdu(struct ath11k *ar, struct napi_struct *napi,
				      struct sk_buff *msdu)
{
	static const struct ieee80211_radiotap_he known = {
		.data1 = cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_DATA_MCS_KNOWN |
				     IEEE80211_RADIOTAP_HE_DATA1_BW_RU_ALLOC_KNOWN),
		.data2 = cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA2_GI_KNOWN),
	};
	struct ieee80211_rx_status *status;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)msdu->data;
	struct ieee80211_radiotap_he *he = NULL;
	char tid[32];

	status = IEEE80211_SKB_RXCB(msdu);
	if (status->encoding == RX_ENC_HE) {
		he = skb_push(msdu, sizeof(known));
		memcpy(he, &known, sizeof(known));
		status->flag |= RX_FLAG_RADIOTAP_HE;
	}

	ath11k_dbg(ar->ab, ATH11K_DBG_DATA,
		   "rx skb %pK len %u peer %pM %s %s sn %u %s%s%s%s%s%s%s %srate_idx %u vht_nss %u freq %u band %u flag 0x%x fcs-err %i mic-err %i amsdu-more %i\n",
		   msdu,
		   msdu->len,
		   ieee80211_get_SA(hdr),
		   ath11k_print_get_tid(hdr, tid, sizeof(tid)),
		   is_multicast_ether_addr(ieee80211_get_DA(hdr)) ?
							"mcast" : "ucast",
		   (__le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_SEQ) >> 4,
		   (status->encoding == RX_ENC_LEGACY) ? "legacy" : "",
		   (status->encoding == RX_ENC_HT) ? "ht" : "",
		   (status->encoding == RX_ENC_VHT) ? "vht" : "",
		   (status->encoding == RX_ENC_HE) ? "he" : "",
		   (status->bw == RATE_INFO_BW_40) ? "40" : "",
		   (status->bw == RATE_INFO_BW_80) ? "80" : "",
		   (status->bw == RATE_INFO_BW_160) ? "160" : "",
		   status->enc_flags & RX_ENC_FLAG_SHORT_GI ? "sgi " : "",
		   status->rate_idx,
		   status->nss,
		   status->freq,
		   status->band, status->flag,
		   !!(status->flag & RX_FLAG_FAILED_FCS_CRC),
		   !!(status->flag & RX_FLAG_MMIC_ERROR),
		   !!(status->flag & RX_FLAG_AMSDU_MORE));

	/* TODO: trace rx packet */

	ieee80211_rx_napi(ar->hw, NULL, msdu, napi);
}

static void ath11k_dp_rx_pre_deliver_amsdu(struct ath11k *ar,
					   struct sk_buff_head *amsdu_list,
					   struct ieee80211_rx_status *rxs)
{
	struct sk_buff *msdu;
	struct sk_buff *first_subframe;
	struct ieee80211_rx_status *status;

	first_subframe = skb_peek(amsdu_list);

	skb_queue_walk(amsdu_list, msdu) {
		/* Setup per-MSDU flags */
		if (skb_queue_empty(amsdu_list))
			rxs->flag &= ~RX_FLAG_AMSDU_MORE;
		else
			rxs->flag |= RX_FLAG_AMSDU_MORE;

		if (msdu == first_subframe) {
			first_subframe = NULL;
			rxs->flag &= ~RX_FLAG_ALLOW_SAME_PN;
		} else {
			rxs->flag |= RX_FLAG_ALLOW_SAME_PN;
		}
		rxs->flag |= RX_FLAG_SKIP_MONITOR;

		status = IEEE80211_SKB_RXCB(msdu);
		*status = *rxs;
	}
}

static void ath11k_dp_rx_process_pending_packets(struct ath11k_base *ab,
						 struct napi_struct *napi,
						 struct sk_buff_head *pending_q,
						 int *quota, u8 mac_id)
{
	struct ath11k *ar;
	struct sk_buff *msdu;
	struct ath11k_pdev *pdev;

	if (skb_queue_empty(pending_q))
		return;

	ar = ab->pdevs[mac_id].ar;

	rcu_read_lock();
	pdev = rcu_dereference(ab->pdevs_active[mac_id]);

	while (*quota && (msdu = __skb_dequeue(pending_q))) {
		if (!pdev) {
			dev_kfree_skb_any(msdu);
			continue;
		}

		ath11k_dp_rx_deliver_msdu(ar, napi, msdu);
		(*quota)--;
	}
	rcu_read_unlock();
}

int ath11k_dp_process_rx(struct ath11k_base *ab, int mac_id,
			 struct napi_struct *napi, struct sk_buff_head *pending_q,
			 int budget)
{
	struct ath11k *ar = ab->pdevs[mac_id].ar;
	struct ath11k_pdev_dp *dp = &ar->dp;
	struct ieee80211_rx_status *rx_status = &dp->rx_status;
	struct dp_rxdma_ring *rx_ring = &dp->rx_refill_buf_ring;
	struct hal_srng *srng;
	struct sk_buff *msdu;
	struct sk_buff_head msdu_list;
	struct sk_buff_head amsdu_list;
	struct ath11k_skb_rxcb *rxcb;
	u32 *rx_desc;
	int buf_id;
	int num_buffs_reaped = 0;
	int quota = budget;
	int ret;
	bool done = false;

	/* Process any pending packets from the previous napi poll.
	 * Note: All msdu's in this pending_q corresponds to the same mac id
	 * due to pdev based reo dest mapping and also since each irq group id
	 * maps to specific reo dest ring.
	 */
	ath11k_dp_rx_process_pending_packets(ab, napi, pending_q, &quota,
					     mac_id);

	/* If all quota is exhausted by processing the pending_q,
	 * Wait for the next napi poll to reap the new info
	 */
	if (!quota)
		goto exit;

	__skb_queue_head_init(&msdu_list);

	srng = &ab->hal.srng_list[dp->reo_dst_ring.ring_id];

	spin_lock_bh(&srng->lock);

	ath11k_hal_srng_access_begin(ab, srng);

try_again:
	while ((rx_desc = ath11k_hal_srng_dst_get_next_entry(ab, srng))) {
		struct hal_reo_dest_ring *desc = (struct hal_reo_dest_ring *)rx_desc;
		enum hal_reo_dest_ring_push_reason push_reason;
		u32 cookie;

		cookie = FIELD_GET(BUFFER_ADDR_INFO1_SW_COOKIE,
				   desc->buf_addr_info.info1);
		buf_id = FIELD_GET(DP_RXDMA_BUF_COOKIE_BUF_ID,
				   cookie);
		spin_lock_bh(&rx_ring->idr_lock);
		msdu = idr_find(&rx_ring->bufs_idr, buf_id);
		if (!msdu) {
			ath11k_warn(ab, "frame rx with invalid buf_id %d\n",
				    buf_id);
			spin_unlock_bh(&rx_ring->idr_lock);
			continue;
		}

		idr_remove(&rx_ring->bufs_idr, buf_id);
		spin_unlock_bh(&rx_ring->idr_lock);

		rxcb = ATH11K_SKB_RXCB(msdu);
		dma_unmap_single(ab->dev, rxcb->paddr,
				 msdu->len + skb_tailroom(msdu),
				 DMA_FROM_DEVICE);

		num_buffs_reaped++;

		push_reason = FIELD_GET(HAL_REO_DEST_RING_INFO0_PUSH_REASON,
					desc->info0);
		if (push_reason !=
		    HAL_REO_DEST_RING_PUSH_REASON_ROUTING_INSTRUCTION) {
			/* TODO: Check if the msdu can be sent up for processing */
			dev_kfree_skb_any(msdu);
			ab->soc_stats.hal_reo_error[dp->reo_dst_ring.ring_id]++;
			continue;
		}

		rxcb->is_first_msdu = !!(desc->rx_msdu_info.info0 &
					 RX_MSDU_DESC_INFO0_FIRST_MSDU_IN_MPDU);
		rxcb->is_last_msdu = !!(desc->rx_msdu_info.info0 &
					RX_MSDU_DESC_INFO0_LAST_MSDU_IN_MPDU);
		rxcb->is_continuation = !!(desc->rx_msdu_info.info0 &
					   RX_MSDU_DESC_INFO0_MSDU_CONTINUATION);
		rxcb->mac_id = mac_id;
		__skb_queue_tail(&msdu_list, msdu);

		/* Stop reaping from the ring once quota is exhausted
		 * and we've received all msdu's in the the AMSDU. The
		 * additional msdu's reaped in excess of quota here would
		 * be pushed into the pending queue to be processed during
		 * the next napi poll.
		 * Note: More profiling can be done to see the impact on
		 * pending_q and throughput during various traffic & density
		 * and how use of budget instead of remaining quota affects it.
		 */
		if (num_buffs_reaped >= quota && rxcb->is_last_msdu &&
		    !rxcb->is_continuation) {
			done = true;
			break;
		}
	}

	/* Hw might have updated the head pointer after we cached it.
	 * In this case, even though there are entries in the ring we'll
	 * get rx_desc NULL. Give the read another try with updated cached
	 * head pointer so that we can reap complete MPDU in the current
	 * rx processing.
	 */
	if (!done && ath11k_hal_srng_dst_num_free(ab, srng, true)) {
		ath11k_hal_srng_access_end(ab, srng);
		goto try_again;
	}

	ath11k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	if (!num_buffs_reaped)
		goto exit;

	/* Should we reschedule it later if we are not able to replenish all
	 * the buffers?
	 */
	ath11k_dp_rxbufs_replenish(ab, mac_id, rx_ring, num_buffs_reaped,
				   HAL_RX_BUF_RBM_SW3_BM, GFP_ATOMIC);

	rcu_read_lock();
	if (!rcu_dereference(ab->pdevs_active[mac_id])) {
		__skb_queue_purge(&msdu_list);
		goto rcu_unlock;
	}

	if (test_bit(ATH11K_CAC_RUNNING, &ar->dev_flags)) {
		__skb_queue_purge(&msdu_list);
		goto rcu_unlock;
	}

	while (!skb_queue_empty(&msdu_list)) {
		__skb_queue_head_init(&amsdu_list);
		ret = ath11k_dp_rx_retrieve_amsdu(ar, &msdu_list, &amsdu_list);
		if (ret) {
			if (ret == -EIO) {
				ath11k_err(ab, "rx ring got corrupted %d\n", ret);
				__skb_queue_purge(&msdu_list);
				/* Should stop processing any more rx in
				 * future from this ring?
				 */
				goto rcu_unlock;
			}

			/* A-MSDU retrieval got failed due to non-fatal condition,
			 * continue processing with the next msdu.
			 */
			continue;
		}

		ath11k_dp_rx_process_amsdu(ar, &amsdu_list, rx_status);

		ath11k_dp_rx_pre_deliver_amsdu(ar, &amsdu_list, rx_status);
		skb_queue_splice_tail(&amsdu_list, pending_q);
	}

	while (quota && (msdu = __skb_dequeue(pending_q))) {
		ath11k_dp_rx_deliver_msdu(ar, napi, msdu);
		quota--;
	}

rcu_unlock:
	rcu_read_unlock();
exit:
	return budget - quota;
}

static void ath11k_dp_rx_update_peer_stats(struct ath11k_sta *arsta,
					   struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct ath11k_rx_peer_stats *rx_stats = arsta->rx_stats;
	u32 num_msdu;

	if (!rx_stats)
		return;

	num_msdu = ppdu_info->tcp_msdu_count + ppdu_info->tcp_ack_msdu_count +
		   ppdu_info->udp_msdu_count + ppdu_info->other_msdu_count;

	rx_stats->num_msdu += num_msdu;
	rx_stats->tcp_msdu_count += ppdu_info->tcp_msdu_count +
				    ppdu_info->tcp_ack_msdu_count;
	rx_stats->udp_msdu_count += ppdu_info->udp_msdu_count;
	rx_stats->other_msdu_count += ppdu_info->other_msdu_count;

	if (ppdu_info->preamble_type == HAL_RX_PREAMBLE_11A ||
	    ppdu_info->preamble_type == HAL_RX_PREAMBLE_11B) {
		ppdu_info->nss = 1;
		ppdu_info->mcs = HAL_RX_MAX_MCS;
		ppdu_info->tid = IEEE80211_NUM_TIDS;
	}

	if (ppdu_info->nss > 0 && ppdu_info->nss <= HAL_RX_MAX_NSS)
		rx_stats->nss_count[ppdu_info->nss - 1] += num_msdu;

	if (ppdu_info->mcs <= HAL_RX_MAX_MCS)
		rx_stats->mcs_count[ppdu_info->mcs] += num_msdu;

	if (ppdu_info->gi < HAL_RX_GI_MAX)
		rx_stats->gi_count[ppdu_info->gi] += num_msdu;

	if (ppdu_info->bw < HAL_RX_BW_MAX)
		rx_stats->bw_count[ppdu_info->bw] += num_msdu;

	if (ppdu_info->ldpc < HAL_RX_SU_MU_CODING_MAX)
		rx_stats->coding_count[ppdu_info->ldpc] += num_msdu;

	if (ppdu_info->tid <= IEEE80211_NUM_TIDS)
		rx_stats->tid_count[ppdu_info->tid] += num_msdu;

	if (ppdu_info->preamble_type < HAL_RX_PREAMBLE_MAX)
		rx_stats->pream_cnt[ppdu_info->preamble_type] += num_msdu;

	if (ppdu_info->reception_type < HAL_RX_RECEPTION_TYPE_MAX)
		rx_stats->reception_type[ppdu_info->reception_type] += num_msdu;

	if (ppdu_info->is_stbc)
		rx_stats->stbc_count += num_msdu;

	if (ppdu_info->beamformed)
		rx_stats->beamformed_count += num_msdu;

	if (ppdu_info->num_mpdu_fcs_ok > 1)
		rx_stats->ampdu_msdu_count += num_msdu;
	else
		rx_stats->non_ampdu_msdu_count += num_msdu;

	rx_stats->num_mpdu_fcs_ok += ppdu_info->num_mpdu_fcs_ok;
	rx_stats->num_mpdu_fcs_err += ppdu_info->num_mpdu_fcs_err;
	rx_stats->dcm_count += ppdu_info->dcm;
	rx_stats->ru_alloc_cnt[ppdu_info->ru_alloc] += num_msdu;

	arsta->rssi_comb = ppdu_info->rssi_comb;
	rx_stats->rx_duration += ppdu_info->rx_duration;
	arsta->rx_duration = rx_stats->rx_duration;
}

static struct sk_buff *ath11k_dp_rx_alloc_mon_status_buf(struct ath11k_base *ab,
							 struct dp_rxdma_ring *rx_ring,
							 int *buf_id, gfp_t gfp)
{
	struct sk_buff *skb;
	dma_addr_t paddr;

	skb = dev_alloc_skb(DP_RX_BUFFER_SIZE +
			    DP_RX_BUFFER_ALIGN_SIZE);

	if (!skb)
		goto fail_alloc_skb;

	if (!IS_ALIGNED((unsigned long)skb->data,
			DP_RX_BUFFER_ALIGN_SIZE)) {
		skb_pull(skb, PTR_ALIGN(skb->data, DP_RX_BUFFER_ALIGN_SIZE) -
			 skb->data);
	}

	paddr = dma_map_single(ab->dev, skb->data,
			       skb->len + skb_tailroom(skb),
			       DMA_BIDIRECTIONAL);
	if (unlikely(dma_mapping_error(ab->dev, paddr)))
		goto fail_free_skb;

	spin_lock_bh(&rx_ring->idr_lock);
	*buf_id = idr_alloc(&rx_ring->bufs_idr, skb, 0,
			    rx_ring->bufs_max, gfp);
	spin_unlock_bh(&rx_ring->idr_lock);
	if (*buf_id < 0)
		goto fail_dma_unmap;

	ATH11K_SKB_RXCB(skb)->paddr = paddr;
	return skb;

fail_dma_unmap:
	dma_unmap_single(ab->dev, paddr, skb->len + skb_tailroom(skb),
			 DMA_BIDIRECTIONAL);
fail_free_skb:
	dev_kfree_skb_any(skb);
fail_alloc_skb:
	return NULL;
}

int ath11k_dp_rx_mon_status_bufs_replenish(struct ath11k_base *ab, int mac_id,
					   struct dp_rxdma_ring *rx_ring,
					   int req_entries,
					   enum hal_rx_buf_return_buf_manager mgr,
					   gfp_t gfp)
{
	struct hal_srng *srng;
	u32 *desc;
	struct sk_buff *skb;
	int num_free;
	int num_remain;
	int buf_id;
	u32 cookie;
	dma_addr_t paddr;

	req_entries = min(req_entries, rx_ring->bufs_max);

	srng = &ab->hal.srng_list[rx_ring->refill_buf_ring.ring_id];

	spin_lock_bh(&srng->lock);

	ath11k_hal_srng_access_begin(ab, srng);

	num_free = ath11k_hal_srng_src_num_free(ab, srng, true);

	req_entries = min(num_free, req_entries);
	num_remain = req_entries;

	while (num_remain > 0) {
		skb = ath11k_dp_rx_alloc_mon_status_buf(ab, rx_ring,
							&buf_id, gfp);
		if (!skb)
			break;
		paddr = ATH11K_SKB_RXCB(skb)->paddr;

		desc = ath11k_hal_srng_src_get_next_entry(ab, srng);
		if (!desc)
			goto fail_desc_get;

		cookie = FIELD_PREP(DP_RXDMA_BUF_COOKIE_PDEV_ID, mac_id) |
			 FIELD_PREP(DP_RXDMA_BUF_COOKIE_BUF_ID, buf_id);

		num_remain--;

		ath11k_hal_rx_buf_addr_info_set(desc, paddr, cookie, mgr);
	}

	ath11k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	return req_entries - num_remain;

fail_desc_get:
	spin_lock_bh(&rx_ring->idr_lock);
	idr_remove(&rx_ring->bufs_idr, buf_id);
	spin_unlock_bh(&rx_ring->idr_lock);
	dma_unmap_single(ab->dev, paddr, skb->len + skb_tailroom(skb),
			 DMA_BIDIRECTIONAL);
	dev_kfree_skb_any(skb);
	ath11k_hal_srng_access_end(ab, srng);
	spin_unlock_bh(&srng->lock);

	return req_entries - num_remain;
}

static int ath11k_dp_rx_reap_mon_status_ring(struct ath11k_base *ab, int mac_id,
					     int *budget, struct sk_buff_head *skb_list)
{
	struct ath11k *ar = ab->pdevs[mac_id].ar;
	struct ath11k_pdev_dp *dp = &ar->dp;
	struct dp_rxdma_ring *rx_ring = &dp->rx_mon_status_refill_ring;
	struct hal_srng *srng;
	void *rx_mon_status_desc;
	struct sk_buff *skb;
	struct ath11k_skb_rxcb *rxcb;
	struct hal_tlv_hdr *tlv;
	u32 cookie;
	int buf_id;
	dma_addr_t paddr;
	u8 rbm;
	int num_buffs_reaped = 0;

	srng = &ab->hal.srng_list[rx_ring->refill_buf_ring.ring_id];

	spin_lock_bh(&srng->lock);

	ath11k_hal_srng_access_begin(ab, srng);
	while (*budget) {
		*budget -= 1;
		rx_mon_status_desc =
			ath11k_hal_srng_src_peek(ab, srng);
		if (!rx_mon_status_desc)
			break;

		ath11k_hal_rx_buf_addr_info_get(rx_mon_status_desc, &paddr,
						&cookie, &rbm);
		if (paddr) {
			buf_id = FIELD_GET(DP_RXDMA_BUF_COOKIE_BUF_ID, cookie);

			spin_lock_bh(&rx_ring->idr_lock);
			skb = idr_find(&rx_ring->bufs_idr, buf_id);
			if (!skb) {
				ath11k_warn(ab, "rx monitor status with invalid buf_id %d\n",
					    buf_id);
				spin_unlock_bh(&rx_ring->idr_lock);
				continue;
			}

			idr_remove(&rx_ring->bufs_idr, buf_id);
			spin_unlock_bh(&rx_ring->idr_lock);

			rxcb = ATH11K_SKB_RXCB(skb);

			dma_sync_single_for_cpu(ab->dev, rxcb->paddr,
						skb->len + skb_tailroom(skb),
						DMA_FROM_DEVICE);

			dma_unmap_single(ab->dev, rxcb->paddr,
					 skb->len + skb_tailroom(skb),
					 DMA_BIDIRECTIONAL);

			tlv = (struct hal_tlv_hdr *)skb->data;
			if (FIELD_GET(HAL_TLV_HDR_TAG, tlv->tl) !=
					HAL_RX_STATUS_BUFFER_DONE) {
				ath11k_hal_srng_src_get_next_entry(ab, srng);
				continue;
			}

			__skb_queue_tail(skb_list, skb);
		}

		skb = ath11k_dp_rx_alloc_mon_status_buf(ab, rx_ring,
							&buf_id, GFP_ATOMIC);

		if (!skb) {
			ath11k_hal_rx_buf_addr_info_set(rx_mon_status_desc, 0, 0,
							HAL_RX_BUF_RBM_SW3_BM);
			num_buffs_reaped++;
			break;
		}
		rxcb = ATH11K_SKB_RXCB(skb);

		cookie = FIELD_PREP(DP_RXDMA_BUF_COOKIE_PDEV_ID, mac_id) |
			 FIELD_PREP(DP_RXDMA_BUF_COOKIE_BUF_ID, buf_id);

		ath11k_hal_rx_buf_addr_info_set(rx_mon_status_desc, rxcb->paddr,
						cookie, HAL_RX_BUF_RBM_SW3_BM);
		ath11k_hal_srng_src_get_next_entry(ab, srng);
		num_buffs_reaped++;
	}
	ath11k_hal_srng_access_end(ab, srng);
	spin_unlock_bh(&srng->lock);

	return num_buffs_reaped;
}

int ath11k_dp_rx_process_mon_status(struct ath11k_base *ab, int mac_id,
				    struct napi_struct *napi, int budget)
{
	struct ath11k *ar = ab->pdevs[mac_id].ar;
	enum hal_rx_mon_status hal_status;
	struct sk_buff *skb;
	struct sk_buff_head skb_list;
	struct hal_rx_mon_ppdu_info ppdu_info;
	struct ath11k_peer *peer;
	struct ath11k_sta *arsta;
	int num_buffs_reaped = 0;

	__skb_queue_head_init(&skb_list);

	num_buffs_reaped = ath11k_dp_rx_reap_mon_status_ring(ab, mac_id, &budget,
							     &skb_list);
	if (!num_buffs_reaped)
		goto exit;

	while ((skb = __skb_dequeue(&skb_list))) {
		memset(&ppdu_info, 0, sizeof(ppdu_info));
		ppdu_info.peer_id = HAL_INVALID_PEERID;

		if (ath11k_debug_is_pktlog_rx_stats_enabled(ar))
			trace_ath11k_htt_rxdesc(ar, skb->data, DP_RX_BUFFER_SIZE);

		hal_status = ath11k_hal_rx_parse_mon_status(ab, &ppdu_info, skb);

		if (ppdu_info.peer_id == HAL_INVALID_PEERID ||
		    hal_status != HAL_RX_MON_STATUS_PPDU_DONE) {
			dev_kfree_skb_any(skb);
			continue;
		}

		rcu_read_lock();
		spin_lock_bh(&ab->base_lock);
		peer = ath11k_peer_find_by_id(ab, ppdu_info.peer_id);

		if (!peer || !peer->sta) {
			ath11k_dbg(ab, ATH11K_DBG_DATA,
				   "failed to find the peer with peer_id %d\n",
				   ppdu_info.peer_id);
			spin_unlock_bh(&ab->base_lock);
			rcu_read_unlock();
			dev_kfree_skb_any(skb);
			continue;
		}

		arsta = (struct ath11k_sta *)peer->sta->drv_priv;
		ath11k_dp_rx_update_peer_stats(arsta, &ppdu_info);

		if (ath11k_debug_is_pktlog_peer_valid(ar, peer->addr))
			trace_ath11k_htt_rxdesc(ar, skb->data, DP_RX_BUFFER_SIZE);

		spin_unlock_bh(&ab->base_lock);
		rcu_read_unlock();

		dev_kfree_skb_any(skb);
	}
exit:
	return num_buffs_reaped;
}

static int ath11k_dp_rx_link_desc_return(struct ath11k_base *ab,
					 u32 *link_desc,
					 enum hal_wbm_rel_bm_act action)
{
	struct ath11k_dp *dp = &ab->dp;
	struct hal_srng *srng;
	u32 *desc;
	int ret = 0;

	srng = &ab->hal.srng_list[dp->wbm_desc_rel_ring.ring_id];

	spin_lock_bh(&srng->lock);

	ath11k_hal_srng_access_begin(ab, srng);

	desc = ath11k_hal_srng_src_get_next_entry(ab, srng);
	if (!desc) {
		ret = -ENOBUFS;
		goto exit;
	}

	ath11k_hal_rx_msdu_link_desc_set(ab, (void *)desc, (void *)link_desc,
					 action);

exit:
	ath11k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	return ret;
}

static void ath11k_dp_rx_frag_h_mpdu(struct ath11k *ar,
				     struct sk_buff *msdu,
				     struct hal_rx_desc *rx_desc,
				     struct ieee80211_rx_status *rx_status)
{
	u8 rx_channel;
	enum hal_encrypt_type enctype;
	bool is_decrypted;
	u32 err_bitmap;

	is_decrypted = ath11k_dp_rx_h_attn_is_decrypted(rx_desc);
	enctype = ath11k_dp_rx_h_mpdu_start_enctype(rx_desc);
	err_bitmap = ath11k_dp_rx_h_attn_mpdu_err(rx_desc);

	if (err_bitmap & DP_RX_MPDU_ERR_FCS)
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (err_bitmap & DP_RX_MPDU_ERR_TKIP_MIC)
		rx_status->flag |= RX_FLAG_MMIC_ERROR;

	rx_status->encoding = RX_ENC_LEGACY;
	rx_status->bw = RATE_INFO_BW_20;

	rx_status->flag |= RX_FLAG_NO_SIGNAL_VAL;

	rx_channel = ath11k_dp_rx_h_msdu_start_freq(rx_desc);

	if (rx_channel >= 1 && rx_channel <= 14) {
		rx_status->band = NL80211_BAND_2GHZ;
	} else if (rx_channel >= 36 && rx_channel <= 173) {
		rx_status->band = NL80211_BAND_5GHZ;
	} else {
		ath11k_warn(ar->ab, "Unsupported Channel info received %d\n",
			    rx_channel);
		return;
	}

	rx_status->freq = ieee80211_channel_to_frequency(rx_channel,
							 rx_status->band);
	ath11k_dp_rx_h_rate(ar, rx_desc, rx_status);

	/* Rx fragments are received in raw mode */
	skb_trim(msdu, msdu->len - FCS_LEN);

	if (is_decrypted) {
		rx_status->flag |= RX_FLAG_DECRYPTED | RX_FLAG_MIC_STRIPPED;
		skb_trim(msdu, msdu->len -
			 ath11k_dp_rx_crypto_mic_len(ar, enctype));
	}
}

static int
ath11k_dp_process_rx_err_buf(struct ath11k *ar, struct napi_struct *napi,
			     int buf_id, bool frag)
{
	struct ath11k_pdev_dp *dp = &ar->dp;
	struct dp_rxdma_ring *rx_ring = &dp->rx_refill_buf_ring;
	struct ieee80211_rx_status rx_status = {0};
	struct sk_buff *msdu;
	struct ath11k_skb_rxcb *rxcb;
	struct ieee80211_rx_status *status;
	struct hal_rx_desc *rx_desc;
	u16 msdu_len;

	spin_lock_bh(&rx_ring->idr_lock);
	msdu = idr_find(&rx_ring->bufs_idr, buf_id);
	if (!msdu) {
		ath11k_warn(ar->ab, "rx err buf with invalid buf_id %d\n",
			    buf_id);
		spin_unlock_bh(&rx_ring->idr_lock);
		return -EINVAL;
	}

	idr_remove(&rx_ring->bufs_idr, buf_id);
	spin_unlock_bh(&rx_ring->idr_lock);

	rxcb = ATH11K_SKB_RXCB(msdu);
	dma_unmap_single(ar->ab->dev, rxcb->paddr,
			 msdu->len + skb_tailroom(msdu),
			 DMA_FROM_DEVICE);

	if (!frag) {
		/* Process only rx fragments below, and drop
		 * msdu's indicated due to error reasons.
		 */
		dev_kfree_skb_any(msdu);
		return 0;
	}

	rcu_read_lock();
	if (!rcu_dereference(ar->ab->pdevs_active[ar->pdev_idx])) {
		dev_kfree_skb_any(msdu);
		goto exit;
	}

	if (test_bit(ATH11K_CAC_RUNNING, &ar->dev_flags)) {
		dev_kfree_skb_any(msdu);
		goto exit;
	}

	rx_desc = (struct hal_rx_desc *)msdu->data;
	msdu_len = ath11k_dp_rx_h_msdu_start_msdu_len(rx_desc);
	skb_put(msdu, HAL_RX_DESC_SIZE + msdu_len);
	skb_pull(msdu, HAL_RX_DESC_SIZE);

	ath11k_dp_rx_frag_h_mpdu(ar, msdu, rx_desc, &rx_status);

	status = IEEE80211_SKB_RXCB(msdu);

	*status = rx_status;

	ath11k_dp_rx_deliver_msdu(ar, napi, msdu);

exit:
	rcu_read_unlock();
	return 0;
}

int ath11k_dp_process_rx_err(struct ath11k_base *ab, struct napi_struct *napi,
			     int budget)
{
	u32 msdu_cookies[HAL_NUM_RX_MSDUS_PER_LINK_DESC];
	struct dp_link_desc_bank *link_desc_banks;
	enum hal_rx_buf_return_buf_manager rbm;
	int tot_n_bufs_reaped, quota, ret, i;
	int n_bufs_reaped[MAX_RADIOS] = {0};
	struct dp_rxdma_ring *rx_ring;
	struct dp_srng *reo_except;
	u32 desc_bank, num_msdus;
	struct hal_srng *srng;
	struct ath11k_dp *dp;
	void *link_desc_va;
	int buf_id, mac_id;
	struct ath11k *ar;
	dma_addr_t paddr;
	u32 *desc;
	bool is_frag;

	tot_n_bufs_reaped = 0;
	quota = budget;

	dp = &ab->dp;
	reo_except = &dp->reo_except_ring;
	link_desc_banks = dp->link_desc_banks;

	srng = &ab->hal.srng_list[reo_except->ring_id];

	spin_lock_bh(&srng->lock);

	ath11k_hal_srng_access_begin(ab, srng);

	while (budget &&
	       (desc = ath11k_hal_srng_dst_get_next_entry(ab, srng))) {
		struct hal_reo_dest_ring *reo_desc = (struct hal_reo_dest_ring *)desc;

		ab->soc_stats.err_ring_pkts++;
		ret = ath11k_hal_desc_reo_parse_err(ab, desc, &paddr,
						    &desc_bank);
		if (ret) {
			ath11k_warn(ab, "failed to parse error reo desc %d\n",
				    ret);
			continue;
		}
		link_desc_va = link_desc_banks[desc_bank].vaddr +
			       (paddr - link_desc_banks[desc_bank].paddr);
		ath11k_hal_rx_msdu_link_info_get(link_desc_va, &num_msdus, msdu_cookies,
						 &rbm);
		if (rbm != HAL_RX_BUF_RBM_WBM_IDLE_DESC_LIST &&
		    rbm != HAL_RX_BUF_RBM_SW3_BM) {
			ab->soc_stats.invalid_rbm++;
			ath11k_warn(ab, "invalid return buffer manager %d\n", rbm);
			ath11k_dp_rx_link_desc_return(ab, desc,
						      HAL_WBM_REL_BM_ACT_REL_MSDU);
			continue;
		}

		is_frag = !!(reo_desc->rx_mpdu_info.info0 & RX_MPDU_DESC_INFO0_FRAG_FLAG);

		/* Return the link desc back to wbm idle list */
		ath11k_dp_rx_link_desc_return(ab, desc,
					      HAL_WBM_REL_BM_ACT_PUT_IN_IDLE);

		for (i = 0; i < num_msdus; i++) {
			buf_id = FIELD_GET(DP_RXDMA_BUF_COOKIE_BUF_ID,
					   msdu_cookies[i]);

			mac_id = FIELD_GET(DP_RXDMA_BUF_COOKIE_PDEV_ID,
					   msdu_cookies[i]);

			ar = ab->pdevs[mac_id].ar;

			if (!ath11k_dp_process_rx_err_buf(ar, napi, buf_id,
							  is_frag)) {
				n_bufs_reaped[mac_id]++;
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
	ath11k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	for (i = 0; i <  ab->num_radios; i++) {
		if (!n_bufs_reaped[i])
			continue;

		ar = ab->pdevs[i].ar;
		rx_ring = &ar->dp.rx_refill_buf_ring;

		ath11k_dp_rxbufs_replenish(ab, i, rx_ring, n_bufs_reaped[i],
					   HAL_RX_BUF_RBM_SW3_BM, GFP_ATOMIC);
	}

	return tot_n_bufs_reaped;
}

static void ath11k_dp_rx_null_q_desc_sg_drop(struct ath11k *ar,
					     int msdu_len,
					     struct sk_buff_head *msdu_list)
{
	struct sk_buff *skb, *tmp;
	struct ath11k_skb_rxcb *rxcb;
	int n_buffs;

	n_buffs = DIV_ROUND_UP(msdu_len,
			       (DP_RX_BUFFER_SIZE - HAL_RX_DESC_SIZE));

	skb_queue_walk_safe(msdu_list, skb, tmp) {
		rxcb = ATH11K_SKB_RXCB(skb);
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

static int ath11k_dp_rx_h_null_q_desc(struct ath11k *ar, struct sk_buff *msdu,
				      struct ieee80211_rx_status *status,
				      struct sk_buff_head *msdu_list)
{
	struct sk_buff_head amsdu_list;
	u16 msdu_len;
	struct hal_rx_desc *desc = (struct hal_rx_desc *)msdu->data;
	u8 l3pad_bytes;
	struct ath11k_skb_rxcb *rxcb = ATH11K_SKB_RXCB(msdu);

	msdu_len = ath11k_dp_rx_h_msdu_start_msdu_len(desc);

	if ((msdu_len + HAL_RX_DESC_SIZE) > DP_RX_BUFFER_SIZE) {
		/* First buffer will be freed by the caller, so deduct it's length */
		msdu_len = msdu_len - (DP_RX_BUFFER_SIZE - HAL_RX_DESC_SIZE);
		ath11k_dp_rx_null_q_desc_sg_drop(ar, msdu_len, msdu_list);
		return -EINVAL;
	}

	if (!ath11k_dp_rx_h_attn_msdu_done(desc)) {
		ath11k_warn(ar->ab,
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

	__skb_queue_head_init(&amsdu_list);

	rxcb->is_first_msdu = ath11k_dp_rx_h_msdu_end_first_msdu(desc);
	rxcb->is_last_msdu = ath11k_dp_rx_h_msdu_end_last_msdu(desc);

	l3pad_bytes = ath11k_dp_rx_h_msdu_end_l3pad(desc);

	if ((HAL_RX_DESC_SIZE + l3pad_bytes + msdu_len) > DP_RX_BUFFER_SIZE)
		return -EINVAL;

	skb_put(msdu, HAL_RX_DESC_SIZE + l3pad_bytes + msdu_len);
	skb_pull(msdu, HAL_RX_DESC_SIZE + l3pad_bytes);

	ath11k_dp_rx_h_ppdu(ar, desc, status);

	__skb_queue_tail(&amsdu_list, msdu);

	ath11k_dp_rx_h_mpdu(ar, &amsdu_list, desc, status);

	/* Please note that caller will having the access to msdu and completing
	 * rx with mac80211. Need not worry about cleaning up amsdu_list.
	 */

	return 0;
}

static bool ath11k_dp_rx_h_reo_err(struct ath11k *ar, struct sk_buff *msdu,
				   struct ieee80211_rx_status *status,
				   struct sk_buff_head *msdu_list)
{
	struct ath11k_skb_rxcb *rxcb = ATH11K_SKB_RXCB(msdu);
	bool drop = false;

	ar->ab->soc_stats.reo_error[rxcb->err_code]++;

	switch (rxcb->err_code) {
	case HAL_REO_DEST_RING_ERROR_CODE_DESC_ADDR_ZERO:
		if (ath11k_dp_rx_h_null_q_desc(ar, msdu, status, msdu_list))
			drop = true;
		break;
	default:
		/* TODO: Review other errors and process them to mac80211
		 * as appropriate.
		 */
		drop = true;
		break;
	}

	return drop;
}

static void ath11k_dp_rx_h_tkip_mic_err(struct ath11k *ar, struct sk_buff *msdu,
					struct ieee80211_rx_status *status)
{
	u16 msdu_len;
	struct hal_rx_desc *desc = (struct hal_rx_desc *)msdu->data;
	u8 l3pad_bytes;
	struct ath11k_skb_rxcb *rxcb = ATH11K_SKB_RXCB(msdu);

	rxcb->is_first_msdu = ath11k_dp_rx_h_msdu_end_first_msdu(desc);
	rxcb->is_last_msdu = ath11k_dp_rx_h_msdu_end_last_msdu(desc);

	l3pad_bytes = ath11k_dp_rx_h_msdu_end_l3pad(desc);
	msdu_len = ath11k_dp_rx_h_msdu_start_msdu_len(desc);
	skb_put(msdu, HAL_RX_DESC_SIZE + l3pad_bytes + msdu_len);
	skb_pull(msdu, HAL_RX_DESC_SIZE + l3pad_bytes);

	ath11k_dp_rx_h_ppdu(ar, desc, status);

	status->flag |= (RX_FLAG_MMIC_STRIPPED | RX_FLAG_MMIC_ERROR |
			 RX_FLAG_DECRYPTED);

	ath11k_dp_rx_h_undecap(ar, msdu, desc,
			       HAL_ENCRYPT_TYPE_TKIP_MIC, status, false);
}

static bool ath11k_dp_rx_h_rxdma_err(struct ath11k *ar,  struct sk_buff *msdu,
				     struct ieee80211_rx_status *status)
{
	struct ath11k_skb_rxcb *rxcb = ATH11K_SKB_RXCB(msdu);
	bool drop = false;

	ar->ab->soc_stats.rxdma_error[rxcb->err_code]++;

	switch (rxcb->err_code) {
	case HAL_REO_ENTR_RING_RXDMA_ECODE_TKIP_MIC_ERR:
		ath11k_dp_rx_h_tkip_mic_err(ar, msdu, status);
		break;
	default:
		/* TODO: Review other rxdma error code to check if anything is
		 * worth reporting to mac80211
		 */
		drop = true;
		break;
	}

	return drop;
}

static void ath11k_dp_rx_wbm_err(struct ath11k *ar,
				 struct napi_struct *napi,
				 struct sk_buff *msdu,
				 struct sk_buff_head *msdu_list)
{
	struct ath11k_skb_rxcb *rxcb = ATH11K_SKB_RXCB(msdu);
	struct ieee80211_rx_status rxs = {0};
	struct ieee80211_rx_status *status;
	bool drop = true;

	switch (rxcb->err_rel_src) {
	case HAL_WBM_REL_SRC_MODULE_REO:
		drop = ath11k_dp_rx_h_reo_err(ar, msdu, &rxs, msdu_list);
		break;
	case HAL_WBM_REL_SRC_MODULE_RXDMA:
		drop = ath11k_dp_rx_h_rxdma_err(ar, msdu, &rxs);
		break;
	default:
		/* msdu will get freed */
		break;
	}

	if (drop) {
		dev_kfree_skb_any(msdu);
		return;
	}

	status = IEEE80211_SKB_RXCB(msdu);
	*status = rxs;

	ath11k_dp_rx_deliver_msdu(ar, napi, msdu);
}

int ath11k_dp_rx_process_wbm_err(struct ath11k_base *ab,
				 struct napi_struct *napi, int budget)
{
	struct ath11k *ar;
	struct ath11k_dp *dp = &ab->dp;
	struct dp_rxdma_ring *rx_ring;
	struct hal_rx_wbm_rel_info err_info;
	struct hal_srng *srng;
	struct sk_buff *msdu;
	struct sk_buff_head msdu_list[MAX_RADIOS];
	struct ath11k_skb_rxcb *rxcb;
	u32 *rx_desc;
	int buf_id, mac_id;
	int num_buffs_reaped[MAX_RADIOS] = {0};
	int total_num_buffs_reaped = 0;
	int ret, i;

	for (i = 0; i < MAX_RADIOS; i++)
		__skb_queue_head_init(&msdu_list[i]);

	srng = &ab->hal.srng_list[dp->rx_rel_ring.ring_id];

	spin_lock_bh(&srng->lock);

	ath11k_hal_srng_access_begin(ab, srng);

	while (budget) {
		rx_desc = ath11k_hal_srng_dst_get_next_entry(ab, srng);
		if (!rx_desc)
			break;

		ret = ath11k_hal_wbm_desc_parse_err(ab, rx_desc, &err_info);
		if (ret) {
			ath11k_warn(ab,
				    "failed to parse rx error in wbm_rel ring desc %d\n",
				    ret);
			continue;
		}

		buf_id = FIELD_GET(DP_RXDMA_BUF_COOKIE_BUF_ID, err_info.cookie);
		mac_id = FIELD_GET(DP_RXDMA_BUF_COOKIE_PDEV_ID, err_info.cookie);

		ar = ab->pdevs[mac_id].ar;
		rx_ring = &ar->dp.rx_refill_buf_ring;

		spin_lock_bh(&rx_ring->idr_lock);
		msdu = idr_find(&rx_ring->bufs_idr, buf_id);
		if (!msdu) {
			ath11k_warn(ab, "frame rx with invalid buf_id %d pdev %d\n",
				    buf_id, mac_id);
			spin_unlock_bh(&rx_ring->idr_lock);
			continue;
		}

		idr_remove(&rx_ring->bufs_idr, buf_id);
		spin_unlock_bh(&rx_ring->idr_lock);

		rxcb = ATH11K_SKB_RXCB(msdu);
		dma_unmap_single(ab->dev, rxcb->paddr,
				 msdu->len + skb_tailroom(msdu),
				 DMA_FROM_DEVICE);

		num_buffs_reaped[mac_id]++;
		total_num_buffs_reaped++;
		budget--;

		if (err_info.push_reason !=
		    HAL_REO_DEST_RING_PUSH_REASON_ERR_DETECTED) {
			dev_kfree_skb_any(msdu);
			continue;
		}

		rxcb->err_rel_src = err_info.err_rel_src;
		rxcb->err_code = err_info.err_code;
		rxcb->rx_desc = (struct hal_rx_desc *)msdu->data;
		__skb_queue_tail(&msdu_list[mac_id], msdu);
	}

	ath11k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	if (!total_num_buffs_reaped)
		goto done;

	for (i = 0; i <  ab->num_radios; i++) {
		if (!num_buffs_reaped[i])
			continue;

		ar = ab->pdevs[i].ar;
		rx_ring = &ar->dp.rx_refill_buf_ring;

		ath11k_dp_rxbufs_replenish(ab, i, rx_ring, num_buffs_reaped[i],
					   HAL_RX_BUF_RBM_SW3_BM, GFP_ATOMIC);
	}

	rcu_read_lock();
	for (i = 0; i <  ab->num_radios; i++) {
		if (!rcu_dereference(ab->pdevs_active[i])) {
			__skb_queue_purge(&msdu_list[i]);
			continue;
		}

		ar = ab->pdevs[i].ar;

		if (test_bit(ATH11K_CAC_RUNNING, &ar->dev_flags)) {
			__skb_queue_purge(&msdu_list[i]);
			continue;
		}

		while ((msdu = __skb_dequeue(&msdu_list[i])) != NULL)
			ath11k_dp_rx_wbm_err(ar, napi, msdu, &msdu_list[i]);
	}
	rcu_read_unlock();
done:
	return total_num_buffs_reaped;
}

int ath11k_dp_process_rxdma_err(struct ath11k_base *ab, int mac_id, int budget)
{
	struct ath11k *ar = ab->pdevs[mac_id].ar;
	struct dp_srng *err_ring = &ar->dp.rxdma_err_dst_ring;
	struct dp_rxdma_ring *rx_ring = &ar->dp.rx_refill_buf_ring;
	struct dp_link_desc_bank *link_desc_banks = ab->dp.link_desc_banks;
	struct hal_srng *srng;
	u32 msdu_cookies[HAL_NUM_RX_MSDUS_PER_LINK_DESC];
	enum hal_rx_buf_return_buf_manager rbm;
	enum hal_reo_entr_rxdma_ecode rxdma_err_code;
	struct ath11k_skb_rxcb *rxcb;
	struct sk_buff *skb;
	struct hal_reo_entrance_ring *entr_ring;
	void *desc;
	int num_buf_freed = 0;
	int quota = budget;
	dma_addr_t paddr;
	u32 desc_bank;
	void *link_desc_va;
	int num_msdus;
	int i;
	int buf_id;

	srng = &ab->hal.srng_list[err_ring->ring_id];

	spin_lock_bh(&srng->lock);

	ath11k_hal_srng_access_begin(ab, srng);

	while (quota-- &&
	       (desc = ath11k_hal_srng_dst_get_next_entry(ab, srng))) {
		ath11k_hal_rx_reo_ent_paddr_get(ab, desc, &paddr, &desc_bank);

		entr_ring = (struct hal_reo_entrance_ring *)desc;
		rxdma_err_code =
			FIELD_GET(HAL_REO_ENTR_RING_INFO1_RXDMA_ERROR_CODE,
				  entr_ring->info1);
		ab->soc_stats.rxdma_error[rxdma_err_code]++;

		link_desc_va = link_desc_banks[desc_bank].vaddr +
			       (paddr - link_desc_banks[desc_bank].paddr);
		ath11k_hal_rx_msdu_link_info_get(link_desc_va, &num_msdus,
						 msdu_cookies, &rbm);

		for (i = 0; i < num_msdus; i++) {
			buf_id = FIELD_GET(DP_RXDMA_BUF_COOKIE_BUF_ID,
					   msdu_cookies[i]);

			spin_lock_bh(&rx_ring->idr_lock);
			skb = idr_find(&rx_ring->bufs_idr, buf_id);
			if (!skb) {
				ath11k_warn(ab, "rxdma error with invalid buf_id %d\n",
					    buf_id);
				spin_unlock_bh(&rx_ring->idr_lock);
				continue;
			}

			idr_remove(&rx_ring->bufs_idr, buf_id);
			spin_unlock_bh(&rx_ring->idr_lock);

			rxcb = ATH11K_SKB_RXCB(skb);
			dma_unmap_single(ab->dev, rxcb->paddr,
					 skb->len + skb_tailroom(skb),
					 DMA_FROM_DEVICE);
			dev_kfree_skb_any(skb);

			num_buf_freed++;
		}

		ath11k_dp_rx_link_desc_return(ab, desc,
					      HAL_WBM_REL_BM_ACT_PUT_IN_IDLE);
	}

	ath11k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	if (num_buf_freed)
		ath11k_dp_rxbufs_replenish(ab, mac_id, rx_ring, num_buf_freed,
					   HAL_RX_BUF_RBM_SW3_BM, GFP_ATOMIC);

	return budget - quota;
}

void ath11k_dp_process_reo_status(struct ath11k_base *ab)
{
	struct ath11k_dp *dp = &ab->dp;
	struct hal_srng *srng;
	struct dp_reo_cmd *cmd, *tmp;
	bool found = false;
	u32 *reo_desc;
	u16 tag;
	struct hal_reo_status reo_status;

	srng = &ab->hal.srng_list[dp->reo_status_ring.ring_id];

	memset(&reo_status, 0, sizeof(reo_status));

	spin_lock_bh(&srng->lock);

	ath11k_hal_srng_access_begin(ab, srng);

	while ((reo_desc = ath11k_hal_srng_dst_get_next_entry(ab, srng))) {
		tag = FIELD_GET(HAL_SRNG_TLV_HDR_TAG, *reo_desc);

		switch (tag) {
		case HAL_REO_GET_QUEUE_STATS_STATUS:
			ath11k_hal_reo_status_queue_stats(ab, reo_desc,
							  &reo_status);
			break;
		case HAL_REO_FLUSH_QUEUE_STATUS:
			ath11k_hal_reo_flush_queue_status(ab, reo_desc,
							  &reo_status);
			break;
		case HAL_REO_FLUSH_CACHE_STATUS:
			ath11k_hal_reo_flush_cache_status(ab, reo_desc,
							  &reo_status);
			break;
		case HAL_REO_UNBLOCK_CACHE_STATUS:
			ath11k_hal_reo_unblk_cache_status(ab, reo_desc,
							  &reo_status);
			break;
		case HAL_REO_FLUSH_TIMEOUT_LIST_STATUS:
			ath11k_hal_reo_flush_timeout_list_status(ab, reo_desc,
								 &reo_status);
			break;
		case HAL_REO_DESCRIPTOR_THRESHOLD_REACHED_STATUS:
			ath11k_hal_reo_desc_thresh_reached_status(ab, reo_desc,
								  &reo_status);
			break;
		case HAL_REO_UPDATE_RX_REO_QUEUE_STATUS:
			ath11k_hal_reo_update_rx_reo_queue_status(ab, reo_desc,
								  &reo_status);
			break;
		default:
			ath11k_warn(ab, "Unknown reo status type %d\n", tag);
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

	ath11k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);
}

void ath11k_dp_rx_pdev_free(struct ath11k_base *ab, int mac_id)
{
	struct ath11k *ar = ab->pdevs[mac_id].ar;

	ath11k_dp_rx_pdev_srng_free(ar);
	ath11k_dp_rxdma_pdev_buf_free(ar);
}

int ath11k_dp_rx_pdev_alloc(struct ath11k_base *ab, int mac_id)
{
	struct ath11k *ar = ab->pdevs[mac_id].ar;
	struct ath11k_pdev_dp *dp = &ar->dp;
	u32 ring_id;
	int ret;

	ret = ath11k_dp_rx_pdev_srng_alloc(ar);
	if (ret) {
		ath11k_warn(ab, "failed to setup rx srngs\n");
		return ret;
	}

	ret = ath11k_dp_rxdma_pdev_buf_setup(ar);
	if (ret) {
		ath11k_warn(ab, "failed to setup rxdma ring\n");
		return ret;
	}

	ring_id = dp->rx_refill_buf_ring.refill_buf_ring.ring_id;
	ret = ath11k_dp_tx_htt_srng_setup(ab, ring_id, mac_id, HAL_RXDMA_BUF);
	if (ret) {
		ath11k_warn(ab, "failed to configure rx_refill_buf_ring %d\n",
			    ret);
		return ret;
	}

	ring_id = dp->rxdma_err_dst_ring.ring_id;
	ret = ath11k_dp_tx_htt_srng_setup(ab, ring_id, mac_id, HAL_RXDMA_DST);
	if (ret) {
		ath11k_warn(ab, "failed to configure rxdma_err_dest_ring %d\n",
			    ret);
		return ret;
	}

	ring_id = dp->rxdma_mon_buf_ring.refill_buf_ring.ring_id;
	ret = ath11k_dp_tx_htt_srng_setup(ab, ring_id,
					  mac_id, HAL_RXDMA_MONITOR_BUF);
	if (ret) {
		ath11k_warn(ab, "failed to configure rxdma_mon_buf_ring %d\n",
			    ret);
		return ret;
	}
	ret = ath11k_dp_tx_htt_srng_setup(ab,
					  dp->rxdma_mon_dst_ring.ring_id,
					  mac_id, HAL_RXDMA_MONITOR_DST);
	if (ret) {
		ath11k_warn(ab, "failed to configure rxdma_mon_dst_ring %d\n",
			    ret);
		return ret;
	}
	ret = ath11k_dp_tx_htt_srng_setup(ab,
					  dp->rxdma_mon_desc_ring.ring_id,
					  mac_id, HAL_RXDMA_MONITOR_DESC);
	if (ret) {
		ath11k_warn(ab, "failed to configure rxdma_mon_dst_ring %d\n",
			    ret);
		return ret;
	}
	ring_id = dp->rx_mon_status_refill_ring.refill_buf_ring.ring_id;
	ret = ath11k_dp_tx_htt_srng_setup(ab, ring_id, mac_id,
					  HAL_RXDMA_MONITOR_STATUS);
	if (ret) {
		ath11k_warn(ab,
			    "failed to configure mon_status_refill_ring %d\n",
			    ret);
		return ret;
	}
	return 0;
}

static void ath11k_dp_mon_set_frag_len(u32 *total_len, u32 *frag_len)
{
	if (*total_len >= (DP_RX_BUFFER_SIZE - sizeof(struct hal_rx_desc))) {
		*frag_len = DP_RX_BUFFER_SIZE - sizeof(struct hal_rx_desc);
		*total_len -= *frag_len;
	} else {
		*frag_len = *total_len;
		*total_len = 0;
	}
}

static
int ath11k_dp_rx_monitor_link_desc_return(struct ath11k *ar,
					  void *p_last_buf_addr_info,
					  u8 mac_id)
{
	struct ath11k_pdev_dp *dp = &ar->dp;
	struct dp_srng *dp_srng;
	void *hal_srng;
	void *src_srng_desc;
	int ret = 0;

	dp_srng = &dp->rxdma_mon_desc_ring;
	hal_srng = &ar->ab->hal.srng_list[dp_srng->ring_id];

	ath11k_hal_srng_access_begin(ar->ab, hal_srng);

	src_srng_desc = ath11k_hal_srng_src_get_next_entry(ar->ab, hal_srng);

	if (src_srng_desc) {
		struct ath11k_buffer_addr *src_desc =
				(struct ath11k_buffer_addr *)src_srng_desc;

		*src_desc = *((struct ath11k_buffer_addr *)p_last_buf_addr_info);
	} else {
		ath11k_dbg(ar->ab, ATH11K_DBG_DATA,
			   "Monitor Link Desc Ring %d Full", mac_id);
		ret = -ENOMEM;
	}

	ath11k_hal_srng_access_end(ar->ab, hal_srng);
	return ret;
}

static
void ath11k_dp_rx_mon_next_link_desc_get(void *rx_msdu_link_desc,
					 dma_addr_t *paddr, u32 *sw_cookie,
					 void **pp_buf_addr_info)
{
	struct hal_rx_msdu_link *msdu_link =
			(struct hal_rx_msdu_link *)rx_msdu_link_desc;
	struct ath11k_buffer_addr *buf_addr_info;
	u8 rbm = 0;

	buf_addr_info = (struct ath11k_buffer_addr *)&msdu_link->buf_addr_info;

	ath11k_hal_rx_buf_addr_info_get(buf_addr_info, paddr, sw_cookie, &rbm);

	*pp_buf_addr_info = (void *)buf_addr_info;
}

static int ath11k_dp_pkt_set_pktlen(struct sk_buff *skb, u32 len)
{
	if (skb->len > len) {
		skb_trim(skb, len);
	} else {
		if (skb_tailroom(skb) < len - skb->len) {
			if ((pskb_expand_head(skb, 0,
					      len - skb->len - skb_tailroom(skb),
					      GFP_ATOMIC))) {
				dev_kfree_skb_any(skb);
				return -ENOMEM;
			}
		}
		skb_put(skb, (len - skb->len));
	}
	return 0;
}

static void ath11k_hal_rx_msdu_list_get(struct ath11k *ar,
					void *msdu_link_desc,
					struct hal_rx_msdu_list *msdu_list,
					u16 *num_msdus)
{
	struct hal_rx_msdu_details *msdu_details = NULL;
	struct rx_msdu_desc *msdu_desc_info = NULL;
	struct hal_rx_msdu_link *msdu_link = NULL;
	int i;
	u32 last = FIELD_PREP(RX_MSDU_DESC_INFO0_LAST_MSDU_IN_MPDU, 1);
	u32 first = FIELD_PREP(RX_MSDU_DESC_INFO0_FIRST_MSDU_IN_MPDU, 1);
	u8  tmp  = 0;

	msdu_link = (struct hal_rx_msdu_link *)msdu_link_desc;
	msdu_details = &msdu_link->msdu_link[0];

	for (i = 0; i < HAL_RX_NUM_MSDU_DESC; i++) {
		if (FIELD_GET(BUFFER_ADDR_INFO0_ADDR,
			      msdu_details[i].buf_addr_info.info0) == 0) {
			msdu_desc_info = &msdu_details[i - 1].rx_msdu_info;
			msdu_desc_info->info0 |= last;
			;
			break;
		}
		msdu_desc_info = &msdu_details[i].rx_msdu_info;

		if (!i)
			msdu_desc_info->info0 |= first;
		else if (i == (HAL_RX_NUM_MSDU_DESC - 1))
			msdu_desc_info->info0 |= last;
		msdu_list->msdu_info[i].msdu_flags = msdu_desc_info->info0;
		msdu_list->msdu_info[i].msdu_len =
			 HAL_RX_MSDU_PKT_LENGTH_GET(msdu_desc_info->info0);
		msdu_list->sw_cookie[i] =
			FIELD_GET(BUFFER_ADDR_INFO1_SW_COOKIE,
				  msdu_details[i].buf_addr_info.info1);
		tmp = FIELD_GET(BUFFER_ADDR_INFO1_RET_BUF_MGR,
				msdu_details[i].buf_addr_info.info1);
		msdu_list->rbm[i] = tmp;
	}
	*num_msdus = i;
}

static u32 ath11k_dp_rx_mon_comp_ppduid(u32 msdu_ppdu_id, u32 *ppdu_id,
					u32 *rx_bufs_used)
{
	u32 ret = 0;

	if ((*ppdu_id < msdu_ppdu_id) &&
	    ((msdu_ppdu_id - *ppdu_id) < DP_NOT_PPDU_ID_WRAP_AROUND)) {
		*ppdu_id = msdu_ppdu_id;
		ret = msdu_ppdu_id;
	} else if ((*ppdu_id > msdu_ppdu_id) &&
		((*ppdu_id - msdu_ppdu_id) > DP_NOT_PPDU_ID_WRAP_AROUND)) {
		/* mon_dst is behind than mon_status
		 * skip dst_ring and free it
		 */
		*rx_bufs_used += 1;
		*ppdu_id = msdu_ppdu_id;
		ret = msdu_ppdu_id;
	}
	return ret;
}

static void ath11k_dp_mon_get_buf_len(struct hal_rx_msdu_desc_info *info,
				      bool *is_frag, u32 *total_len,
				      u32 *frag_len, u32 *msdu_cnt)
{
	if (info->msdu_flags & RX_MSDU_DESC_INFO0_MSDU_CONTINUATION) {
		if (!*is_frag) {
			*total_len = info->msdu_len;
			*is_frag = true;
		}
		ath11k_dp_mon_set_frag_len(total_len,
					   frag_len);
	} else {
		if (*is_frag) {
			ath11k_dp_mon_set_frag_len(total_len,
						   frag_len);
		} else {
			*frag_len = info->msdu_len;
		}
		*is_frag = false;
		*msdu_cnt -= 1;
	}
}

static u32
ath11k_dp_rx_mon_mpdu_pop(struct ath11k *ar,
			  void *ring_entry, struct sk_buff **head_msdu,
			  struct sk_buff **tail_msdu, u32 *npackets,
			  u32 *ppdu_id)
{
	struct ath11k_pdev_dp *dp = &ar->dp;
	struct ath11k_mon_data *pmon = (struct ath11k_mon_data *)&dp->mon_data;
	struct dp_rxdma_ring *rx_ring = &dp->rxdma_mon_buf_ring;
	struct sk_buff *msdu = NULL, *last = NULL;
	struct hal_rx_msdu_list msdu_list;
	void *p_buf_addr_info, *p_last_buf_addr_info;
	struct hal_rx_desc *rx_desc;
	void *rx_msdu_link_desc;
	dma_addr_t paddr;
	u16 num_msdus = 0;
	u32 rx_buf_size, rx_pkt_offset, sw_cookie;
	u32 rx_bufs_used = 0, i = 0;
	u32 msdu_ppdu_id = 0, msdu_cnt = 0;
	u32 total_len = 0, frag_len = 0;
	bool is_frag, is_first_msdu;
	bool drop_mpdu = false;
	struct ath11k_skb_rxcb *rxcb;
	struct hal_reo_entrance_ring *ent_desc =
			(struct hal_reo_entrance_ring *)ring_entry;
	int buf_id;

	ath11k_hal_rx_reo_ent_buf_paddr_get(ring_entry, &paddr,
					    &sw_cookie, &p_last_buf_addr_info,
					    &msdu_cnt);

	if (FIELD_GET(HAL_REO_ENTR_RING_INFO1_RXDMA_PUSH_REASON,
		      ent_desc->info1) ==
		      HAL_REO_DEST_RING_PUSH_REASON_ERR_DETECTED) {
		u8 rxdma_err =
			FIELD_GET(HAL_REO_ENTR_RING_INFO1_RXDMA_ERROR_CODE,
				  ent_desc->info1);
		if (rxdma_err == HAL_REO_ENTR_RING_RXDMA_ECODE_FLUSH_REQUEST_ERR ||
		    rxdma_err == HAL_REO_ENTR_RING_RXDMA_ECODE_MPDU_LEN_ERR ||
		    rxdma_err == HAL_REO_ENTR_RING_RXDMA_ECODE_OVERFLOW_ERR) {
			drop_mpdu = true;
			pmon->rx_mon_stats.dest_mpdu_drop++;
		}
	}

	is_frag = false;
	is_first_msdu = true;

	do {
		if (pmon->mon_last_linkdesc_paddr == paddr) {
			pmon->rx_mon_stats.dup_mon_linkdesc_cnt++;
			return rx_bufs_used;
		}

		rx_msdu_link_desc =
			(void *)pmon->link_desc_banks[sw_cookie].vaddr +
			(paddr - pmon->link_desc_banks[sw_cookie].paddr);

		ath11k_hal_rx_msdu_list_get(ar, rx_msdu_link_desc, &msdu_list,
					    &num_msdus);

		for (i = 0; i < num_msdus; i++) {
			u32 l2_hdr_offset;

			if (pmon->mon_last_buf_cookie == msdu_list.sw_cookie[i]) {
				ath11k_dbg(ar->ab, ATH11K_DBG_DATA,
					   "i %d last_cookie %d is same\n",
					   i, pmon->mon_last_buf_cookie);
				drop_mpdu = true;
				pmon->rx_mon_stats.dup_mon_buf_cnt++;
				continue;
			}
			buf_id = FIELD_GET(DP_RXDMA_BUF_COOKIE_BUF_ID,
					   msdu_list.sw_cookie[i]);

			spin_lock_bh(&rx_ring->idr_lock);
			msdu = idr_find(&rx_ring->bufs_idr, buf_id);
			spin_unlock_bh(&rx_ring->idr_lock);
			if (!msdu) {
				ath11k_dbg(ar->ab, ATH11K_DBG_DATA,
					   "msdu_pop: invalid buf_id %d\n", buf_id);
				break;
			}
			rxcb = ATH11K_SKB_RXCB(msdu);
			if (!rxcb->unmapped) {
				dma_unmap_single(ar->ab->dev, rxcb->paddr,
						 msdu->len +
						 skb_tailroom(msdu),
						 DMA_FROM_DEVICE);
				rxcb->unmapped = 1;
			}
			if (drop_mpdu) {
				ath11k_dbg(ar->ab, ATH11K_DBG_DATA,
					   "i %d drop msdu %p *ppdu_id %x\n",
					   i, msdu, *ppdu_id);
				dev_kfree_skb_any(msdu);
				msdu = NULL;
				goto next_msdu;
			}

			rx_desc = (struct hal_rx_desc *)msdu->data;

			rx_pkt_offset = sizeof(struct hal_rx_desc);
			l2_hdr_offset = ath11k_dp_rx_h_msdu_end_l3pad(rx_desc);

			if (is_first_msdu) {
				if (!ath11k_dp_rxdesc_mpdu_valid(rx_desc)) {
					drop_mpdu = true;
					dev_kfree_skb_any(msdu);
					msdu = NULL;
					pmon->mon_last_linkdesc_paddr = paddr;
					goto next_msdu;
				}

				msdu_ppdu_id =
					ath11k_dp_rxdesc_get_ppduid(rx_desc);

				if (ath11k_dp_rx_mon_comp_ppduid(msdu_ppdu_id,
								 ppdu_id,
								 &rx_bufs_used)) {
					if (rx_bufs_used) {
						drop_mpdu = true;
						dev_kfree_skb_any(msdu);
						msdu = NULL;
						goto next_msdu;
					}
					return rx_bufs_used;
				}
				pmon->mon_last_linkdesc_paddr = paddr;
				is_first_msdu = false;
			}
			ath11k_dp_mon_get_buf_len(&msdu_list.msdu_info[i],
						  &is_frag, &total_len,
						  &frag_len, &msdu_cnt);
			rx_buf_size = rx_pkt_offset + l2_hdr_offset + frag_len;

			ath11k_dp_pkt_set_pktlen(msdu, rx_buf_size);

			if (!(*head_msdu))
				*head_msdu = msdu;
			else if (last)
				last->next = msdu;

			last = msdu;
next_msdu:
			pmon->mon_last_buf_cookie = msdu_list.sw_cookie[i];
			rx_bufs_used++;
			spin_lock_bh(&rx_ring->idr_lock);
			idr_remove(&rx_ring->bufs_idr, buf_id);
			spin_unlock_bh(&rx_ring->idr_lock);
		}

		ath11k_dp_rx_mon_next_link_desc_get(rx_msdu_link_desc, &paddr,
						    &sw_cookie,
						    &p_buf_addr_info);

		if (ath11k_dp_rx_monitor_link_desc_return(ar,
							  p_last_buf_addr_info,
							  dp->mac_id))
			ath11k_dbg(ar->ab, ATH11K_DBG_DATA,
				   "dp_rx_monitor_link_desc_return failed");

		p_last_buf_addr_info = p_buf_addr_info;

	} while (paddr && msdu_cnt);

	if (last)
		last->next = NULL;

	*tail_msdu = msdu;

	if (msdu_cnt == 0)
		*npackets = 1;

	return rx_bufs_used;
}

static void ath11k_dp_rx_msdus_set_payload(struct sk_buff *msdu)
{
	u32 rx_pkt_offset, l2_hdr_offset;

	rx_pkt_offset = sizeof(struct hal_rx_desc);
	l2_hdr_offset = ath11k_dp_rx_h_msdu_end_l3pad((struct hal_rx_desc *)msdu->data);
	skb_pull(msdu, rx_pkt_offset + l2_hdr_offset);
}

static struct sk_buff *
ath11k_dp_rx_mon_merg_msdus(struct ath11k *ar,
			    u32 mac_id, struct sk_buff *head_msdu,
			    struct sk_buff *last_msdu,
			    struct ieee80211_rx_status *rxs)
{
	struct sk_buff *msdu, *mpdu_buf, *prev_buf;
	u32 decap_format, wifi_hdr_len;
	struct hal_rx_desc *rx_desc;
	char *hdr_desc;
	u8 *dest;
	struct ieee80211_hdr_3addr *wh;

	mpdu_buf = NULL;

	if (!head_msdu)
		goto err_merge_fail;

	rx_desc = (struct hal_rx_desc *)head_msdu->data;

	if (ath11k_dp_rxdesc_get_mpdulen_err(rx_desc))
		return NULL;

	decap_format = ath11k_dp_rxdesc_get_decap_format(rx_desc);

	ath11k_dp_rx_h_ppdu(ar, rx_desc, rxs);

	if (decap_format == DP_RX_DECAP_TYPE_RAW) {
		ath11k_dp_rx_msdus_set_payload(head_msdu);

		prev_buf = head_msdu;
		msdu = head_msdu->next;

		while (msdu) {
			ath11k_dp_rx_msdus_set_payload(msdu);

			prev_buf = msdu;
			msdu = msdu->next;
		}

		prev_buf->next = NULL;

		skb_trim(prev_buf, prev_buf->len - HAL_RX_FCS_LEN);
	} else if (decap_format == DP_RX_DECAP_TYPE_NATIVE_WIFI) {
		__le16 qos_field;
		u8 qos_pkt = 0;

		rx_desc = (struct hal_rx_desc *)head_msdu->data;
		hdr_desc = ath11k_dp_rxdesc_get_80211hdr(rx_desc);

		/* Base size */
		wifi_hdr_len = sizeof(struct ieee80211_hdr_3addr);
		wh = (struct ieee80211_hdr_3addr *)hdr_desc;

		if (ieee80211_is_data_qos(wh->frame_control)) {
			struct ieee80211_qos_hdr *qwh =
					(struct ieee80211_qos_hdr *)hdr_desc;

			qos_field = qwh->qos_ctrl;
			qos_pkt = 1;
		}
		msdu = head_msdu;

		while (msdu) {
			rx_desc = (struct hal_rx_desc *)msdu->data;
			hdr_desc = ath11k_dp_rxdesc_get_80211hdr(rx_desc);

			if (qos_pkt) {
				dest = skb_push(msdu, sizeof(__le16));
				if (!dest)
					goto err_merge_fail;
				memcpy(dest, hdr_desc, wifi_hdr_len);
				memcpy(dest + wifi_hdr_len,
				       (u8 *)&qos_field, sizeof(__le16));
			}
			ath11k_dp_rx_msdus_set_payload(msdu);
			prev_buf = msdu;
			msdu = msdu->next;
		}
		dest = skb_put(prev_buf, HAL_RX_FCS_LEN);
		if (!dest)
			goto err_merge_fail;

		ath11k_dbg(ar->ab, ATH11K_DBG_DATA,
			   "mpdu_buf %pK mpdu_buf->len %u",
			   prev_buf, prev_buf->len);
	} else {
		ath11k_dbg(ar->ab, ATH11K_DBG_DATA,
			   "decap format %d is not supported!\n",
			   decap_format);
		goto err_merge_fail;
	}

	return head_msdu;

err_merge_fail:
	if (mpdu_buf && decap_format != DP_RX_DECAP_TYPE_RAW) {
		ath11k_dbg(ar->ab, ATH11K_DBG_DATA,
			   "err_merge_fail mpdu_buf %pK", mpdu_buf);
		/* Free the head buffer */
		dev_kfree_skb_any(mpdu_buf);
	}
	return NULL;
}

static int ath11k_dp_rx_mon_deliver(struct ath11k *ar, u32 mac_id,
				    struct sk_buff *head_msdu,
				    struct sk_buff *tail_msdu,
				    struct napi_struct *napi)
{
	struct ath11k_pdev_dp *dp = &ar->dp;
	struct sk_buff *mon_skb, *skb_next, *header;
	struct ieee80211_rx_status *rxs = &dp->rx_status, *status;

	mon_skb = ath11k_dp_rx_mon_merg_msdus(ar, mac_id, head_msdu,
					      tail_msdu, rxs);

	if (!mon_skb)
		goto mon_deliver_fail;

	header = mon_skb;

	rxs->flag = 0;
	do {
		skb_next = mon_skb->next;
		if (!skb_next)
			rxs->flag &= ~RX_FLAG_AMSDU_MORE;
		else
			rxs->flag |= RX_FLAG_AMSDU_MORE;

		if (mon_skb == header) {
			header = NULL;
			rxs->flag &= ~RX_FLAG_ALLOW_SAME_PN;
		} else {
			rxs->flag |= RX_FLAG_ALLOW_SAME_PN;
		}
		rxs->flag |= RX_FLAG_ONLY_MONITOR;

		status = IEEE80211_SKB_RXCB(mon_skb);
		*status = *rxs;

		ath11k_dp_rx_deliver_msdu(ar, napi, mon_skb);
		mon_skb = skb_next;
	} while (mon_skb);
	rxs->flag = 0;

	return 0;

mon_deliver_fail:
	mon_skb = head_msdu;
	while (mon_skb) {
		skb_next = mon_skb->next;
		dev_kfree_skb_any(mon_skb);
		mon_skb = skb_next;
	}
	return -EINVAL;
}

static void ath11k_dp_rx_mon_dest_process(struct ath11k *ar, u32 quota,
					  struct napi_struct *napi)
{
	struct ath11k_pdev_dp *dp = &ar->dp;
	struct ath11k_mon_data *pmon = (struct ath11k_mon_data *)&dp->mon_data;
	void *ring_entry;
	void *mon_dst_srng;
	u32 ppdu_id;
	u32 rx_bufs_used;
	struct ath11k_pdev_mon_stats *rx_mon_stats;
	u32	 npackets = 0;

	mon_dst_srng = &ar->ab->hal.srng_list[dp->rxdma_mon_dst_ring.ring_id];

	if (!mon_dst_srng) {
		ath11k_warn(ar->ab,
			    "HAL Monitor Destination Ring Init Failed -- %pK",
			    mon_dst_srng);
		return;
	}

	spin_lock_bh(&pmon->mon_lock);

	ath11k_hal_srng_access_begin(ar->ab, mon_dst_srng);

	ppdu_id = pmon->mon_ppdu_info.ppdu_id;
	rx_bufs_used = 0;
	rx_mon_stats = &pmon->rx_mon_stats;

	while ((ring_entry = ath11k_hal_srng_dst_peek(ar->ab, mon_dst_srng))) {
		struct sk_buff *head_msdu, *tail_msdu;

		head_msdu = NULL;
		tail_msdu = NULL;

		rx_bufs_used += ath11k_dp_rx_mon_mpdu_pop(ar, ring_entry,
							  &head_msdu,
							  &tail_msdu,
							  &npackets, &ppdu_id);

		if (ppdu_id != pmon->mon_ppdu_info.ppdu_id) {
			pmon->mon_ppdu_status = DP_PPDU_STATUS_START;
			ath11k_dbg(ar->ab, ATH11K_DBG_DATA,
				   "dest_rx: new ppdu_id %x != status ppdu_id %x",
				   ppdu_id, pmon->mon_ppdu_info.ppdu_id);
			break;
		}
		if (head_msdu && tail_msdu) {
			ath11k_dp_rx_mon_deliver(ar, dp->mac_id, head_msdu,
						 tail_msdu, napi);
			rx_mon_stats->dest_mpdu_done++;
		}

		ring_entry = ath11k_hal_srng_dst_get_next_entry(ar->ab,
								mon_dst_srng);
	}
	ath11k_hal_srng_access_end(ar->ab, mon_dst_srng);

	spin_unlock_bh(&pmon->mon_lock);

	if (rx_bufs_used) {
		rx_mon_stats->dest_ppdu_done++;
		ath11k_dp_rxbufs_replenish(ar->ab, dp->mac_id,
					   &dp->rxdma_mon_buf_ring,
					   rx_bufs_used,
					   HAL_RX_BUF_RBM_SW3_BM, GFP_ATOMIC);
	}
}

static void ath11k_dp_rx_mon_status_process_tlv(struct ath11k *ar,
						u32 quota,
						struct napi_struct *napi)
{
	struct ath11k_pdev_dp *dp = &ar->dp;
	struct ath11k_mon_data *pmon = (struct ath11k_mon_data *)&dp->mon_data;
	struct hal_rx_mon_ppdu_info *ppdu_info;
	struct sk_buff *status_skb;
	u32 tlv_status = HAL_TLV_STATUS_BUF_DONE;
	struct ath11k_pdev_mon_stats *rx_mon_stats;

	ppdu_info = &pmon->mon_ppdu_info;
	rx_mon_stats = &pmon->rx_mon_stats;

	if (pmon->mon_ppdu_status != DP_PPDU_STATUS_START)
		return;

	while (!skb_queue_empty(&pmon->rx_status_q)) {
		status_skb = skb_dequeue(&pmon->rx_status_q);

		tlv_status = ath11k_hal_rx_parse_mon_status(ar->ab, ppdu_info,
							    status_skb);
		if (tlv_status == HAL_TLV_STATUS_PPDU_DONE) {
			rx_mon_stats->status_ppdu_done++;
			pmon->mon_ppdu_status = DP_PPDU_STATUS_DONE;
			ath11k_dp_rx_mon_dest_process(ar, quota, napi);
			pmon->mon_ppdu_status = DP_PPDU_STATUS_START;
		}
		dev_kfree_skb_any(status_skb);
	}
}

static int ath11k_dp_mon_process_rx(struct ath11k_base *ab, int mac_id,
				    struct napi_struct *napi, int budget)
{
	struct ath11k *ar = ab->pdevs[mac_id].ar;
	struct ath11k_pdev_dp *dp = &ar->dp;
	struct ath11k_mon_data *pmon = (struct ath11k_mon_data *)&dp->mon_data;
	int num_buffs_reaped = 0;

	num_buffs_reaped = ath11k_dp_rx_reap_mon_status_ring(ar->ab, dp->mac_id, &budget,
							     &pmon->rx_status_q);
	if (num_buffs_reaped)
		ath11k_dp_rx_mon_status_process_tlv(ar, budget, napi);

	return num_buffs_reaped;
}

int ath11k_dp_rx_process_mon_rings(struct ath11k_base *ab, int mac_id,
				   struct napi_struct *napi, int budget)
{
	struct ath11k *ar = ab->pdevs[mac_id].ar;
	int ret = 0;

	if (test_bit(ATH11K_FLAG_MONITOR_ENABLED, &ar->monitor_flags))
		ret = ath11k_dp_mon_process_rx(ab, mac_id, napi, budget);
	else
		ret = ath11k_dp_rx_process_mon_status(ab, mac_id, napi, budget);
	return ret;
}

static int ath11k_dp_rx_pdev_mon_status_attach(struct ath11k *ar)
{
	struct ath11k_pdev_dp *dp = &ar->dp;
	struct ath11k_mon_data *pmon = (struct ath11k_mon_data *)&dp->mon_data;

	skb_queue_head_init(&pmon->rx_status_q);

	pmon->mon_ppdu_status = DP_PPDU_STATUS_START;

	memset(&pmon->rx_mon_stats, 0,
	       sizeof(pmon->rx_mon_stats));
	return 0;
}

int ath11k_dp_rx_pdev_mon_attach(struct ath11k *ar)
{
	struct ath11k_pdev_dp *dp = &ar->dp;
	struct ath11k_mon_data *pmon = &dp->mon_data;
	struct hal_srng *mon_desc_srng = NULL;
	struct dp_srng *dp_srng;
	int ret = 0;
	u32 n_link_desc = 0;

	ret = ath11k_dp_rx_pdev_mon_status_attach(ar);
	if (ret) {
		ath11k_warn(ar->ab, "pdev_mon_status_attach() failed");
		return ret;
	}

	dp_srng = &dp->rxdma_mon_desc_ring;
	n_link_desc = dp_srng->size /
		ath11k_hal_srng_get_entrysize(HAL_RXDMA_MONITOR_DESC);
	mon_desc_srng =
		&ar->ab->hal.srng_list[dp->rxdma_mon_desc_ring.ring_id];

	ret = ath11k_dp_link_desc_setup(ar->ab, pmon->link_desc_banks,
					HAL_RXDMA_MONITOR_DESC, mon_desc_srng,
					n_link_desc);
	if (ret) {
		ath11k_warn(ar->ab, "mon_link_desc_pool_setup() failed");
		return ret;
	}
	pmon->mon_last_linkdesc_paddr = 0;
	pmon->mon_last_buf_cookie = DP_RX_DESC_COOKIE_MAX + 1;
	spin_lock_init(&pmon->mon_lock);
	return 0;
}

static int ath11k_dp_mon_link_free(struct ath11k *ar)
{
	struct ath11k_pdev_dp *dp = &ar->dp;
	struct ath11k_mon_data *pmon = &dp->mon_data;

	ath11k_dp_link_desc_cleanup(ar->ab, pmon->link_desc_banks,
				    HAL_RXDMA_MONITOR_DESC,
				    &dp->rxdma_mon_desc_ring);
	return 0;
}

int ath11k_dp_rx_pdev_mon_detach(struct ath11k *ar)
{
	ath11k_dp_mon_link_free(ar);
	return 0;
}
