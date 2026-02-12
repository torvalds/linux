// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "dp_rx.h"
#include "../dp_tx.h"
#include "../peer.h"
#include "hal_qcn9274.h"
#include "hal_wcn7850.h"
#include "hal_qcc2072.h"

static u16 ath12k_wifi7_dp_rx_get_peer_id(struct ath12k_dp *dp,
					  enum ath12k_peer_metadata_version ver,
					  __le32 peer_metadata)
{
	switch (ver) {
	default:
		ath12k_warn(dp->ab, "Unknown peer metadata version: %d", ver);
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

void ath12k_wifi7_peer_rx_tid_qref_setup(struct ath12k_base *ab, u16 peer_id, u16 tid,
					 dma_addr_t paddr)
{
	struct ath12k_reo_queue_ref *qref;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
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

void ath12k_wifi7_peer_rx_tid_qref_reset(struct ath12k_base *ab,
					 u16 peer_id, u16 tid)
{
	struct ath12k_reo_queue_ref *qref;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
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

void ath12k_wifi7_dp_rx_peer_tid_delete(struct ath12k_base *ab,
					struct ath12k_dp_link_peer *peer, u8 tid)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);

	if (!(peer->rx_tid_active_bitmask & (1 << tid)))
		return;

	ath12k_dp_mark_tid_as_inactive(dp, peer->peer_id, tid);
	ath12k_dp_rx_process_reo_cmd_update_rx_queue_list(dp);
}

int ath12k_wifi7_dp_rx_link_desc_return(struct ath12k_dp *dp,
					struct ath12k_buffer_addr *buf_addr_info,
					enum hal_wbm_rel_bm_act action)
{
	struct ath12k_base *ab = dp->ab;
	struct hal_wbm_release_ring *desc;
	struct hal_srng *srng;
	int ret = 0;

	srng = &dp->hal->srng_list[dp->wbm_desc_rel_ring.ring_id];

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

	desc = ath12k_hal_srng_src_get_next_entry(ab, srng);
	if (!desc) {
		ret = -ENOBUFS;
		goto exit;
	}

	ath12k_wifi7_hal_rx_msdu_link_desc_set(ab, desc, buf_addr_info, action);

exit:
	ath12k_hal_srng_access_end(ab, srng);

	spin_unlock_bh(&srng->lock);

	return ret;
}

int ath12k_wifi7_dp_reo_cmd_send(struct ath12k_base *ab,
				 struct ath12k_dp_rx_tid_rxq *rx_tid,
				 enum hal_reo_cmd_type type,
				 struct ath12k_hal_reo_cmd *cmd,
				 void (*cb)(struct ath12k_dp *dp, void *ctx,
					    enum hal_reo_cmd_status status))
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_dp_rx_reo_cmd *dp_cmd;
	struct hal_srng *cmd_ring;
	int cmd_num;

	cmd_ring = &ab->hal.srng_list[dp->reo_cmd_ring.ring_id];
	cmd_num = ath12k_wifi7_hal_reo_cmd_send(ab, cmd_ring, type, cmd);

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

int ath12k_wifi7_peer_rx_tid_reo_update(struct ath12k_dp *dp,
					struct ath12k_dp_link_peer *peer,
					struct ath12k_dp_rx_tid *rx_tid,
					u32 ba_win_sz, u16 ssn,
					bool update_ssn)
{
	struct ath12k_hal_reo_cmd cmd = {};
	struct ath12k_base *ab = dp->ab;
	int ret;
	struct ath12k_dp_rx_tid_rxq rx_tid_rxq;

	ath12k_dp_init_rx_tid_rxq(&rx_tid_rxq, rx_tid,
				  (peer->rx_tid_active_bitmask & (1 << rx_tid->tid)));

	cmd.addr_lo = lower_32_bits(rx_tid_rxq.qbuf.paddr_aligned);
	cmd.addr_hi = upper_32_bits(rx_tid_rxq.qbuf.paddr_aligned);
	cmd.flag = HAL_REO_CMD_FLG_NEED_STATUS;
	cmd.upd0 = HAL_REO_CMD_UPD0_BA_WINDOW_SIZE;
	cmd.ba_window_size = ba_win_sz;

	if (update_ssn) {
		cmd.upd0 |= HAL_REO_CMD_UPD0_SSN;
		cmd.upd2 = u32_encode_bits(ssn, HAL_REO_CMD_UPD2_SSN);
	}

	ret = ath12k_wifi7_dp_reo_cmd_send(ab, &rx_tid_rxq,
					   HAL_REO_CMD_UPDATE_RX_QUEUE, &cmd,
					   NULL);
	if (ret) {
		ath12k_warn(ab, "failed to update rx tid queue, tid %d (%d)\n",
			    rx_tid_rxq.tid, ret);
		return ret;
	}

	rx_tid->ba_win_sz = ba_win_sz;

	return 0;
}

int ath12k_wifi7_dp_reo_cache_flush(struct ath12k_base *ab,
				    struct ath12k_dp_rx_tid_rxq *rx_tid)
{
	struct ath12k_hal_reo_cmd cmd = {};
	int ret;

	cmd.addr_lo = lower_32_bits(rx_tid->qbuf.paddr_aligned);
	cmd.addr_hi = upper_32_bits(rx_tid->qbuf.paddr_aligned);
	/* HAL_REO_CMD_FLG_FLUSH_FWD_ALL_MPDUS - all pending MPDUs
	 *in the bitmap will be forwarded/flushed to REO output rings
	 */
	cmd.flag = HAL_REO_CMD_FLG_NEED_STATUS |
		   HAL_REO_CMD_FLG_FLUSH_FWD_ALL_MPDUS;

	/* For all QoS TIDs (except NON_QOS), the driver allocates a maximum
	 * window size of 1024. In such cases, the driver can issue a single
	 * 1KB descriptor flush command instead of sending multiple 128-byte
	 * flush commands for each QoS TID, improving efficiency.
	 */

	if (rx_tid->tid != HAL_DESC_REO_NON_QOS_TID)
		cmd.flag |= HAL_REO_CMD_FLG_FLUSH_QUEUE_1K_DESC;

	ret = ath12k_wifi7_dp_reo_cmd_send(ab, rx_tid,
					   HAL_REO_CMD_FLUSH_CACHE,
					   &cmd, ath12k_dp_reo_cmd_free);
	return ret;
}

int ath12k_wifi7_dp_rx_assign_reoq(struct ath12k_base *ab, struct ath12k_dp_peer *dp_peer,
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

	buf = &dp_peer->reoq_bufs[tid];
	if (!buf->vaddr) {
		/* TODO: Optimize the memory allocation for qos tid based on
		 * the actual BA window size in REO tid update path.
		 */
		if (tid == HAL_DESC_REO_NON_QOS_TID)
			hw_desc_sz = ath12k_wifi7_hal_reo_qdesc_size(ba_win_sz, tid);
		else
			hw_desc_sz = ath12k_wifi7_hal_reo_qdesc_size(DP_BA_WIN_SZ_MAX,
								     tid);

		vaddr = kzalloc(hw_desc_sz + HAL_LINK_DESC_ALIGN - 1, GFP_ATOMIC);
		if (!vaddr)
			return -ENOMEM;

		vaddr_aligned = PTR_ALIGN(vaddr, HAL_LINK_DESC_ALIGN);

		ath12k_wifi7_hal_reo_qdesc_setup(vaddr_aligned, tid, ba_win_sz,
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

	return 0;
}

int ath12k_wifi7_dp_rx_tid_delete_handler(struct ath12k_base *ab,
					  struct ath12k_dp_rx_tid_rxq *rx_tid)
{
	struct ath12k_hal_reo_cmd cmd = {};

	cmd.flag = HAL_REO_CMD_FLG_NEED_STATUS;
	cmd.addr_lo = lower_32_bits(rx_tid->qbuf.paddr_aligned);
	cmd.addr_hi = upper_32_bits(rx_tid->qbuf.paddr_aligned);
	cmd.upd0 |= HAL_REO_CMD_UPD0_VLD;
	/* Observed flush cache failure, to avoid that set vld bit during delete */
	cmd.upd1 |= HAL_REO_CMD_UPD1_VLD;

	return ath12k_wifi7_dp_reo_cmd_send(ab, rx_tid,
					    HAL_REO_CMD_UPDATE_RX_QUEUE, &cmd,
					    ath12k_dp_rx_tid_del_func);
}

static void ath12k_wifi7_dp_rx_h_csum_offload(struct sk_buff *msdu,
					      struct hal_rx_desc_data *rx_info)
{
	msdu->ip_summed = (rx_info->ip_csum_fail || rx_info->l4_csum_fail) ?
			   CHECKSUM_NONE : CHECKSUM_UNNECESSARY;
}

static void ath12k_wifi7_dp_rx_h_mpdu(struct ath12k_pdev_dp *dp_pdev,
				      struct sk_buff *msdu,
				      struct hal_rx_desc *rx_desc,
				      struct hal_rx_desc_data *rx_info)
{
	struct ath12k_skb_rxcb *rxcb;
	enum hal_encrypt_type enctype;
	bool is_decrypted = false;
	struct ieee80211_hdr *hdr;
	struct ath12k_dp_peer *peer;
	struct ieee80211_rx_status *rx_status = rx_info->rx_status;
	u32 err_bitmap = rx_info->err_bitmap;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(),
			 "dp_rx_h_mpdu called without rcu lock");

	/* PN for multicast packets will be checked in mac80211 */
	rxcb = ATH12K_SKB_RXCB(msdu);
	rxcb->is_mcbc = rx_info->is_mcbc;

	if (rxcb->is_mcbc)
		rxcb->peer_id = rx_info->peer_id;

	peer = ath12k_dp_peer_find_by_peerid(dp_pdev, rxcb->peer_id);
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

	if (enctype != HAL_ENCRYPT_TYPE_OPEN && !err_bitmap)
		is_decrypted = rx_info->is_decrypted;

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

	ath12k_wifi7_dp_rx_h_csum_offload(msdu, rx_info);
	ath12k_dp_rx_h_undecap(dp_pdev, msdu, rx_desc,
			       enctype, is_decrypted, rx_info);

	if (!is_decrypted || rx_info->is_mcbc)
		return;

	if (rx_info->decap_type != DP_RX_DECAP_TYPE_ETHERNET2_DIX) {
		hdr = (void *)msdu->data;
		hdr->frame_control &= ~__cpu_to_le16(IEEE80211_FCTL_PROTECTED);
	}
}

static int ath12k_wifi7_dp_rx_msdu_coalesce(struct ath12k_hal *hal,
					    struct sk_buff_head *msdu_list,
					    struct sk_buff *first, struct sk_buff *last,
					    u8 l3pad_bytes, int msdu_len,
					    struct hal_rx_desc_data *rx_info)
{
	struct sk_buff *skb;
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(first);
	int buf_first_hdr_len, buf_first_len;
	struct hal_rx_desc *ldesc;
	int space_extra, rem_len, buf_len;
	u32 hal_rx_desc_sz = hal->hal_desc_sz;
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
	rxcb->is_first_msdu = rx_info->is_first_msdu;
	rxcb->is_last_msdu = rx_info->is_last_msdu;

	/* MSDU spans over multiple buffers because the length of the MSDU
	 * exceeds DP_RX_BUFFER_SIZE - HAL_RX_DESC_SIZE. So assume the data
	 * in the first buf is of length DP_RX_BUFFER_SIZE - HAL_RX_DESC_SIZE.
	 */
	skb_put(first, DP_RX_BUFFER_SIZE);
	skb_pull(first, buf_first_hdr_len);

	/* When an MSDU spread over multiple buffers MSDU_END
	 * tlvs are valid only in the last buffer. Copy those tlvs.
	 */
	ath12k_dp_rx_desc_end_tlv_copy(hal, rxcb->rx_desc, ldesc);

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

static int ath12k_wifi7_dp_rx_process_msdu(struct ath12k_pdev_dp *dp_pdev,
					   struct sk_buff *msdu,
					   struct sk_buff_head *msdu_list,
					   struct hal_rx_desc_data *rx_info)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct hal_rx_desc *rx_desc, *lrx_desc;
	struct ath12k_skb_rxcb *rxcb;
	struct sk_buff *last_buf;
	struct ath12k_hal *hal = dp->hal;
	u8 l3_pad_bytes;
	u16 msdu_len;
	int ret;
	u32 hal_rx_desc_sz = hal->hal_desc_sz;

	last_buf = ath12k_dp_rx_get_msdu_last_buf(msdu_list, msdu);
	if (!last_buf) {
		ath12k_warn(dp->ab,
			    "No valid Rx buffer to access MSDU_END tlv\n");
		ret = -EIO;
		goto free_out;
	}

	rx_desc = (struct hal_rx_desc *)msdu->data;
	lrx_desc = (struct hal_rx_desc *)last_buf->data;

	ath12k_dp_extract_rx_desc_data(hal, rx_info, rx_desc, lrx_desc);
	if (!rx_info->msdu_done) {
		ath12k_warn(dp->ab, "msdu_done bit in msdu_end is not set\n");
		ret = -EIO;
		goto free_out;
	}

	rxcb = ATH12K_SKB_RXCB(msdu);
	rxcb->rx_desc = rx_desc;
	msdu_len = rx_info->msdu_len;
	l3_pad_bytes = rx_info->l3_pad_bytes;

	if (rxcb->is_frag) {
		skb_pull(msdu, hal_rx_desc_sz);
	} else if (!rxcb->is_continuation) {
		if ((msdu_len + hal_rx_desc_sz) > DP_RX_BUFFER_SIZE) {
			ret = -EINVAL;
			ath12k_warn(dp->ab, "invalid msdu len %u\n", msdu_len);
			ath12k_dbg_dump(dp->ab, ATH12K_DBG_DATA, NULL, "", rx_desc,
					sizeof(*rx_desc));
			goto free_out;
		}
		skb_put(msdu, hal_rx_desc_sz + l3_pad_bytes + msdu_len);
		skb_pull(msdu, hal_rx_desc_sz + l3_pad_bytes);
	} else {
		ret = ath12k_wifi7_dp_rx_msdu_coalesce(hal, msdu_list,
						       msdu, last_buf,
						       l3_pad_bytes, msdu_len,
						       rx_info);
		if (ret) {
			ath12k_warn(dp->ab,
				    "failed to coalesce msdu rx buffer%d\n", ret);
			goto free_out;
		}
	}

	if (unlikely(!ath12k_dp_rx_check_nwifi_hdr_len_valid(dp, rx_desc, msdu,
							     rx_info))) {
		ret = -EINVAL;
		goto free_out;
	}

	ath12k_dp_rx_h_ppdu(dp_pdev, rx_info);
	ath12k_wifi7_dp_rx_h_mpdu(dp_pdev, msdu, rx_desc, rx_info);

	rx_info->rx_status->flag |= RX_FLAG_SKIP_MONITOR | RX_FLAG_DUP_VALIDATED;

	return 0;

free_out:
	return ret;
}

static void
ath12k_wifi7_dp_rx_process_received_packets(struct ath12k_dp *dp,
					    struct napi_struct *napi,
					    struct sk_buff_head *msdu_list,
					    int ring_id)
{
	struct ath12k_hw_group *ag = dp->ag;
	struct ath12k_dp_hw_group *dp_hw_grp = &ag->dp_hw_grp;
	struct ieee80211_rx_status rx_status = {};
	struct ath12k_skb_rxcb *rxcb;
	struct sk_buff *msdu;
	struct ath12k *ar;
	struct ath12k_pdev_dp *dp_pdev;
	struct ath12k_hw_link *hw_links = ag->hw_links;
	struct ath12k_base *partner_ab;
	struct hal_rx_desc_data rx_info;
	struct ath12k_dp *partner_dp;
	u8 hw_link_id, pdev_idx;
	int ret;

	if (skb_queue_empty(msdu_list))
		return;

	rx_info.addr2_present = false;
	rx_info.rx_status = &rx_status;

	rcu_read_lock();

	while ((msdu = __skb_dequeue(msdu_list))) {
		rxcb = ATH12K_SKB_RXCB(msdu);
		hw_link_id = rxcb->hw_link_id;
		partner_dp = ath12k_dp_hw_grp_to_dp(dp_hw_grp,
						    hw_links[hw_link_id].device_id);
		pdev_idx = ath12k_hw_mac_id_to_pdev_id(partner_dp->hw_params,
						       hw_links[hw_link_id].pdev_idx);
		partner_ab = partner_dp->ab;
		ar = partner_ab->pdevs[pdev_idx].ar;
		if (!rcu_dereference(partner_ab->pdevs_active[pdev_idx])) {
			dev_kfree_skb_any(msdu);
			continue;
		}

		if (test_bit(ATH12K_FLAG_CAC_RUNNING, &ar->dev_flags)) {
			dev_kfree_skb_any(msdu);
			continue;
		}

		dp_pdev = ath12k_dp_to_pdev_dp(partner_dp, pdev_idx);
		if (!dp_pdev) {
			dev_kfree_skb_any(msdu);
			continue;
		}

		ret = ath12k_wifi7_dp_rx_process_msdu(dp_pdev, msdu, msdu_list, &rx_info);
		if (ret) {
			ath12k_dbg(dp->ab, ATH12K_DBG_DATA,
				   "Unable to process msdu %d", ret);
			dev_kfree_skb_any(msdu);
			continue;
		}

		ath12k_dp_rx_deliver_msdu(dp_pdev, napi, msdu, &rx_info);
	}

	rcu_read_unlock();
}

int ath12k_wifi7_dp_rx_process(struct ath12k_dp *dp, int ring_id,
			       struct napi_struct *napi, int budget)
{
	struct ath12k_hw_group *ag = dp->ag;
	struct ath12k_base *ab = dp->ab;
	struct ath12k_hal *hal = dp->hal;
	struct ath12k_dp_hw_group *dp_hw_grp = &ag->dp_hw_grp;
	struct list_head rx_desc_used_list[ATH12K_MAX_DEVICES];
	struct ath12k_hw_link *hw_links = ag->hw_links;
	int num_buffs_reaped[ATH12K_MAX_DEVICES] = {};
	struct ath12k_rx_desc_info *desc_info;
	struct dp_rxdma_ring *rx_ring = &dp->rx_refill_buf_ring;
	struct hal_reo_dest_ring *desc;
	struct ath12k_dp *partner_dp;
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

	srng = &hal->srng_list[dp->reo_dst_ring[ring_id].ring_id];

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
		partner_dp = ath12k_dp_hw_grp_to_dp(dp_hw_grp, device_id);
		if (unlikely(!partner_dp)) {
			if (desc_info->skb) {
				dev_kfree_skb_any(desc_info->skb);
				desc_info->skb = NULL;
			}

			continue;
		}

		/* retry manual desc retrieval */
		if (!desc_info) {
			desc_info = ath12k_dp_get_rx_desc(partner_dp, cookie);
			if (!desc_info) {
				ath12k_warn(partner_dp->ab, "Invalid cookie in manual descriptor retrieval: 0x%x\n",
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
		dma_unmap_single(partner_dp->dev, rxcb->paddr,
				 msdu->len + skb_tailroom(msdu),
				 DMA_FROM_DEVICE);

		num_buffs_reaped[device_id]++;
		dp->device_stats.reo_rx[ring_id][dp->device_id]++;

		push_reason = le32_get_bits(desc->info0,
					    HAL_REO_DEST_RING_INFO0_PUSH_REASON);
		if (push_reason !=
		    HAL_REO_DEST_RING_PUSH_REASON_ROUTING_INSTRUCTION) {
			dev_kfree_skb_any(msdu);
			dp->device_stats.hal_reo_error[ring_id]++;
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
		rxcb->peer_id = ath12k_wifi7_dp_rx_get_peer_id(dp, dp->peer_metadata_ver,
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

		partner_dp = ath12k_dp_hw_grp_to_dp(dp_hw_grp, device_id);
		rx_ring = &partner_dp->rx_refill_buf_ring;

		ath12k_dp_rx_bufs_replenish(partner_dp, rx_ring,
					    &rx_desc_used_list[device_id],
					    num_buffs_reaped[device_id]);
	}

	ath12k_wifi7_dp_rx_process_received_packets(dp, napi, &msdu_list,
						    ring_id);

exit:
	return total_msdu_reaped;
}

static bool
ath12k_wifi7_dp_rx_h_defrag_validate_incr_pn(struct ath12k_pdev_dp *dp_pdev,
					     struct ath12k_dp_rx_tid *rx_tid,
					     enum hal_encrypt_type encrypt_type)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct sk_buff *first_frag, *skb;
	u64 last_pn;
	u64 cur_pn;

	first_frag = skb_peek(&rx_tid->rx_frags);

	if (encrypt_type != HAL_ENCRYPT_TYPE_CCMP_128 &&
	    encrypt_type != HAL_ENCRYPT_TYPE_CCMP_256 &&
	    encrypt_type != HAL_ENCRYPT_TYPE_GCMP_128 &&
	    encrypt_type != HAL_ENCRYPT_TYPE_AES_GCMP_256)
		return true;

	last_pn = ath12k_dp_rx_h_get_pn(dp, first_frag);
	skb_queue_walk(&rx_tid->rx_frags, skb) {
		if (skb == first_frag)
			continue;

		cur_pn = ath12k_dp_rx_h_get_pn(dp, skb);
		if (cur_pn != last_pn + 1)
			return false;
		last_pn = cur_pn;
	}
	return true;
}

static int ath12k_wifi7_dp_rx_h_defrag_reo_reinject(struct ath12k_dp *dp,
						    struct ath12k_dp_rx_tid *rx_tid,
						    struct sk_buff *defrag_skb)
{
	struct ath12k_base *ab = dp->ab;
	struct ath12k_hal *hal = dp->hal;
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

	hal_rx_desc_sz = hal->hal_desc_sz;
	link_desc_banks = dp->link_desc_banks;
	reo_dest_ring = rx_tid->dst_ring_desc;

	ath12k_wifi7_hal_rx_reo_ent_paddr_get(&reo_dest_ring->buf_addr_info,
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
	ath12k_dp_rxdesc_set_msdu_len(hal, rx_desc, defrag_skb->len - hal_rx_desc_sz);

	buf_paddr = dma_map_single(dp->dev, defrag_skb->data,
				   defrag_skb->len + skb_tailroom(defrag_skb),
				   DMA_TO_DEVICE);
	if (dma_mapping_error(dp->dev, buf_paddr))
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

	ath12k_wifi7_hal_rx_buf_addr_info_set(&msdu0->buf_addr_info, buf_paddr,
					      desc_info->cookie,
					      HAL_RX_BUF_RBM_SW3_BM);

	/* Fill mpdu details into reo entrance ring */
	srng = &hal->srng_list[dp->reo_reinject_ring.ring_id];

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

	ath12k_wifi7_hal_rx_buf_addr_info_set(&reo_ent_ring->buf_addr_info, link_paddr,
					      cookie, idle_link_rbm);

	mpdu_info = u32_encode_bits(1, RX_MPDU_DESC_INFO0_MSDU_COUNT) |
		    u32_encode_bits(0, RX_MPDU_DESC_INFO0_FRAG_FLAG) |
		    u32_encode_bits(1, RX_MPDU_DESC_INFO0_RAW_MPDU) |
		    u32_encode_bits(1, RX_MPDU_DESC_INFO0_VALID_PN) |
		    u32_encode_bits(rx_tid->tid, RX_MPDU_DESC_INFO0_TID);

	reo_ent_ring->rx_mpdu_info.info0 = cpu_to_le32(mpdu_info);
	reo_ent_ring->rx_mpdu_info.peer_meta_data =
		reo_dest_ring->rx_mpdu_info.peer_meta_data;

	if (dp->hw_params->reoq_lut_support) {
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
	dma_unmap_single(dp->dev, buf_paddr, defrag_skb->len + skb_tailroom(defrag_skb),
			 DMA_TO_DEVICE);
	return ret;
}

static int ath12k_wifi7_dp_rx_h_verify_tkip_mic(struct ath12k_pdev_dp *dp_pdev,
						struct ath12k_dp_peer *peer,
						enum hal_encrypt_type enctype,
						struct sk_buff *msdu,
						struct hal_rx_desc_data *rx_info)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ath12k_hal *hal = dp->hal;
	struct hal_rx_desc *rx_desc = (struct hal_rx_desc *)msdu->data;
	struct ieee80211_rx_status *rxs = IEEE80211_SKB_RXCB(msdu);
	struct ieee80211_key_conf *key_conf;
	struct ieee80211_hdr *hdr;
	u8 mic[IEEE80211_CCMP_MIC_LEN];
	int head_len, tail_len, ret;
	size_t data_len;
	u32 hdr_len, hal_rx_desc_sz = hal->hal_desc_sz;
	u8 *key, *data;
	u8 key_idx;

	if (enctype != HAL_ENCRYPT_TYPE_TKIP_MIC)
		return 0;

	rx_info->addr2_present = false;
	rx_info->rx_status = rxs;

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

	ret = ath12k_dp_rx_h_michael_mic(peer->tfm_mmic, key, hdr, data,
					 data_len, mic);
	if (ret || memcmp(mic, data + data_len, IEEE80211_CCMP_MIC_LEN))
		goto mic_fail;

	return 0;

mic_fail:
	(ATH12K_SKB_RXCB(msdu))->is_first_msdu = true;
	(ATH12K_SKB_RXCB(msdu))->is_last_msdu = true;

	ath12k_dp_extract_rx_desc_data(hal, rx_info, rx_desc, rx_desc);

	rxs->flag |= RX_FLAG_MMIC_ERROR | RX_FLAG_MMIC_STRIPPED |
		    RX_FLAG_IV_STRIPPED | RX_FLAG_DECRYPTED;
	skb_pull(msdu, hal_rx_desc_sz);

	if (unlikely(!ath12k_dp_rx_check_nwifi_hdr_len_valid(dp, rx_desc, msdu,
							     rx_info)))
		return -EINVAL;

	ath12k_dp_rx_h_ppdu(dp_pdev, rx_info);
	ath12k_dp_rx_h_undecap(dp_pdev, msdu, rx_desc,
			       HAL_ENCRYPT_TYPE_TKIP_MIC, true, rx_info);
	ieee80211_rx(ath12k_pdev_dp_to_hw(dp_pdev), msdu);
	return -EINVAL;
}

static int ath12k_wifi7_dp_rx_h_defrag(struct ath12k_pdev_dp *dp_pdev,
				       struct ath12k_dp_peer *peer,
				       struct ath12k_dp_rx_tid *rx_tid,
				       struct sk_buff **defrag_skb,
				       enum hal_encrypt_type enctype,
				       struct hal_rx_desc_data *rx_info)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ath12k_base *ab = dp->ab;
	struct sk_buff *skb, *first_frag, *last_frag;
	struct ieee80211_hdr *hdr;
	bool is_decrypted = false;
	int msdu_len = 0;
	int extra_space;
	u32 flags, hal_rx_desc_sz = ab->hal.hal_desc_sz;

	first_frag = skb_peek(&rx_tid->rx_frags);
	last_frag = skb_peek_tail(&rx_tid->rx_frags);

	skb_queue_walk(&rx_tid->rx_frags, skb) {
		flags = 0;
		hdr = (struct ieee80211_hdr *)(skb->data + hal_rx_desc_sz);

		if (enctype != HAL_ENCRYPT_TYPE_OPEN)
			is_decrypted = rx_info->is_decrypted;

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
		ath12k_dp_rx_h_undecap_frag(dp_pdev, skb, enctype, flags);

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

	if (ath12k_wifi7_dp_rx_h_verify_tkip_mic(dp_pdev, peer, enctype, first_frag,
						 rx_info))
		first_frag = NULL;

	*defrag_skb = first_frag;
	return 0;
}

void ath12k_wifi7_dp_rx_frags_cleanup(struct ath12k_dp_rx_tid *rx_tid,
				      bool rel_link_desc)
{
	enum hal_wbm_rel_bm_act act = HAL_WBM_REL_BM_ACT_PUT_IN_IDLE;
	struct ath12k_buffer_addr *buf_addr_info;
	struct ath12k_dp *dp = rx_tid->dp;

	lockdep_assert_held(&dp->dp_lock);

	if (rx_tid->dst_ring_desc) {
		if (rel_link_desc) {
			buf_addr_info = &rx_tid->dst_ring_desc->buf_addr_info;
			ath12k_wifi7_dp_rx_link_desc_return(dp, buf_addr_info, act);
		}
		kfree(rx_tid->dst_ring_desc);
		rx_tid->dst_ring_desc = NULL;
	}

	rx_tid->cur_sn = 0;
	rx_tid->last_frag_no = 0;
	rx_tid->rx_frag_bitmap = 0;
	__skb_queue_purge(&rx_tid->rx_frags);
}

static int ath12k_wifi7_dp_rx_frag_h_mpdu(struct ath12k_pdev_dp *dp_pdev,
					  struct sk_buff *msdu,
					  struct hal_reo_dest_ring *ring_desc,
					  struct hal_rx_desc_data *rx_info)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ath12k_hal *hal = dp->hal;
	struct ath12k_base *ab = dp->ab;
	struct ath12k_dp_peer *peer;
	struct ath12k_dp_rx_tid *rx_tid;
	struct sk_buff *defrag_skb = NULL;
	u32 peer_id = rx_info->peer_id;
	u16 seqno, frag_no;
	u8 tid = rx_info->tid;
	int ret = 0;
	bool more_frags;
	enum hal_encrypt_type enctype = rx_info->enctype;

	frag_no = ath12k_dp_rx_h_frag_no(hal, msdu);
	more_frags = ath12k_dp_rx_h_more_frags(hal, msdu);
	seqno = rx_info->seq_no;

	if (!rx_info->seq_ctl_valid || !rx_info->fc_valid ||
	    tid > IEEE80211_NUM_TIDS)
		return -EINVAL;

	/* received unfragmented packet in reo
	 * exception ring, this shouldn't happen
	 * as these packets typically come from
	 * reo2sw srngs.
	 */
	if (WARN_ON_ONCE(!frag_no && !more_frags))
		return -EINVAL;

	spin_lock_bh(&dp->dp_lock);
	peer = ath12k_dp_peer_find_by_peerid(dp_pdev, peer_id);
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
		ath12k_wifi7_dp_rx_frags_cleanup(rx_tid, true);
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
		ath12k_dp_rx_h_sort_frags(hal, &rx_tid->rx_frags, msdu);

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
		ath12k_wifi7_dp_rx_link_desc_return(dp, &ring_desc->buf_addr_info,
						    HAL_WBM_REL_BM_ACT_PUT_IN_IDLE);
	}

	if (!rx_tid->last_frag_no ||
	    rx_tid->rx_frag_bitmap != GENMASK(rx_tid->last_frag_no, 0)) {
		mod_timer(&rx_tid->frag_timer, jiffies +
					       ATH12K_DP_RX_FRAGMENT_TIMEOUT_MS);
		goto out_unlock;
	}

	spin_unlock_bh(&dp->dp_lock);
	timer_delete_sync(&rx_tid->frag_timer);
	spin_lock_bh(&dp->dp_lock);

	peer = ath12k_dp_peer_find_by_peerid(dp_pdev, peer_id);
	if (!peer)
		goto err_frags_cleanup;

	if (!ath12k_wifi7_dp_rx_h_defrag_validate_incr_pn(dp_pdev, rx_tid, enctype))
		goto err_frags_cleanup;

	if (ath12k_wifi7_dp_rx_h_defrag(dp_pdev, peer, rx_tid, &defrag_skb,
					enctype, rx_info))
		goto err_frags_cleanup;

	if (!defrag_skb)
		goto err_frags_cleanup;

	if (ath12k_wifi7_dp_rx_h_defrag_reo_reinject(dp, rx_tid, defrag_skb))
		goto err_frags_cleanup;

	ath12k_wifi7_dp_rx_frags_cleanup(rx_tid, false);
	goto out_unlock;

err_frags_cleanup:
	dev_kfree_skb_any(defrag_skb);
	ath12k_wifi7_dp_rx_frags_cleanup(rx_tid, true);
out_unlock:
	spin_unlock_bh(&dp->dp_lock);
	return ret;
}

static int
ath12k_wifi7_dp_process_rx_err_buf(struct ath12k_pdev_dp *dp_pdev,
				   struct hal_reo_dest_ring *desc,
				   struct list_head *used_list,
				   bool drop, u32 cookie)
{
	struct ath12k *ar = ath12k_pdev_dp_to_ar(dp_pdev);
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ath12k_hal *hal = dp->hal;
	struct sk_buff *msdu;
	struct ath12k_skb_rxcb *rxcb;
	struct hal_rx_desc_data rx_info;
	struct hal_rx_desc *rx_desc;
	u16 msdu_len;
	u32 hal_rx_desc_sz = hal->hal_desc_sz;
	struct ath12k_rx_desc_info *desc_info;
	u64 desc_va;

	desc_va = ((u64)le32_to_cpu(desc->buf_va_hi) << 32 |
		   le32_to_cpu(desc->buf_va_lo));
	desc_info = (struct ath12k_rx_desc_info *)((unsigned long)desc_va);

	/* retry manual desc retrieval */
	if (!desc_info) {
		desc_info = ath12k_dp_get_rx_desc(dp, cookie);
		if (!desc_info) {
			ath12k_warn(dp->ab,
				    "Invalid cookie in DP rx error descriptor retrieval: 0x%x\n",
				    cookie);
			return -EINVAL;
		}
	}

	if (desc_info->magic != ATH12K_DP_RX_DESC_MAGIC)
		ath12k_warn(dp->ab, "RX Exception, Check HW CC implementation");

	msdu = desc_info->skb;
	desc_info->skb = NULL;

	list_add_tail(&desc_info->list, used_list);

	rxcb = ATH12K_SKB_RXCB(msdu);
	dma_unmap_single(dp->dev, rxcb->paddr,
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
	ath12k_dp_extract_rx_desc_data(hal, &rx_info, rx_desc, rx_desc);

	msdu_len = rx_info.msdu_len;
	if ((msdu_len + hal_rx_desc_sz) > DP_RX_BUFFER_SIZE) {
		ath12k_warn(dp->ab, "invalid msdu leng %u", msdu_len);
		ath12k_dbg_dump(dp->ab, ATH12K_DBG_DATA, NULL, "", rx_desc,
				sizeof(*rx_desc));
		dev_kfree_skb_any(msdu);
		goto exit;
	}

	skb_put(msdu, hal_rx_desc_sz + msdu_len);

	if (ath12k_wifi7_dp_rx_frag_h_mpdu(dp_pdev, msdu, desc, &rx_info)) {
		dev_kfree_skb_any(msdu);
		ath12k_wifi7_dp_rx_link_desc_return(dp, &desc->buf_addr_info,
						    HAL_WBM_REL_BM_ACT_PUT_IN_IDLE);
	}
exit:
	rcu_read_unlock();
	return 0;
}

static int ath12k_dp_h_msdu_buffer_type(struct ath12k_dp *dp,
					struct list_head *list,
					struct hal_reo_dest_ring *desc)
{
	struct ath12k_rx_desc_info *desc_info;
	struct ath12k_skb_rxcb *rxcb;
	struct sk_buff *msdu;
	u64 desc_va;

	dp->device_stats.reo_excep_msdu_buf_type++;

	desc_va = (u64)le32_to_cpu(desc->buf_va_hi) << 32 |
		  le32_to_cpu(desc->buf_va_lo);
	desc_info = (struct ath12k_rx_desc_info *)(uintptr_t)desc_va;
	if (!desc_info) {
		u32 cookie;

		cookie = le32_get_bits(desc->buf_addr_info.info1,
				       BUFFER_ADDR_INFO1_SW_COOKIE);
		desc_info = ath12k_dp_get_rx_desc(dp, cookie);
		if (!desc_info) {
			ath12k_warn(dp->ab, "Invalid cookie in manual descriptor retrieval: 0x%x\n",
				    cookie);
			return -EINVAL;
		}
	}

	if (desc_info->magic != ATH12K_DP_RX_DESC_MAGIC) {
		ath12k_warn(dp->ab, "rx exception, magic check failed with value: %u\n",
			    desc_info->magic);
		return -EINVAL;
	}

	msdu = desc_info->skb;
	desc_info->skb = NULL;
	list_add_tail(&desc_info->list, list);
	rxcb = ATH12K_SKB_RXCB(msdu);
	dma_unmap_single(dp->dev, rxcb->paddr, msdu->len + skb_tailroom(msdu),
			 DMA_FROM_DEVICE);
	dev_kfree_skb_any(msdu);

	return 0;
}

int ath12k_wifi7_dp_rx_process_err(struct ath12k_dp *dp, struct napi_struct *napi,
				   int budget)
{
	struct ath12k_base *ab = dp->ab;
	struct ath12k_hal *hal = dp->hal;
	struct ath12k_hw_group *ag = dp->ag;
	struct ath12k_dp_hw_group *dp_hw_grp = &ag->dp_hw_grp;
	struct ath12k_dp *partner_dp;
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
	struct ath12k_pdev_dp *dp_pdev;
	u8 hw_link_id, device_id;
	u32 desc_bank, num_msdus;
	struct hal_srng *srng;
	dma_addr_t paddr;
	bool is_frag;
	bool drop;
	int pdev_idx;
	struct list_head *used_list;
	enum hal_wbm_rel_bm_act act;

	tot_n_bufs_reaped = 0;
	quota = budget;

	for (device_id = 0; device_id < ATH12K_MAX_DEVICES; device_id++)
		INIT_LIST_HEAD(&rx_desc_used_list[device_id]);

	reo_except = &dp->reo_except_ring;

	srng = &hal->srng_list[reo_except->ring_id];

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

	while (budget &&
	       (reo_desc = ath12k_hal_srng_dst_get_next_entry(ab, srng))) {
		drop = false;
		dp->device_stats.err_ring_pkts++;

		hw_link_id = le32_get_bits(reo_desc->info0,
					   HAL_REO_DEST_RING_INFO0_SRC_LINK_ID);
		device_id = hw_links[hw_link_id].device_id;
		partner_dp = ath12k_dp_hw_grp_to_dp(dp_hw_grp, device_id);

		/* Below case is added to handle data packet from un-associated clients.
		 * As it is expected that AST lookup will fail for
		 * un-associated station's data packets.
		 */
		if (le32_get_bits(reo_desc->info0, HAL_REO_DEST_RING_INFO0_BUFFER_TYPE) ==
		    HAL_REO_DEST_RING_BUFFER_TYPE_MSDU) {
			if (!ath12k_dp_h_msdu_buffer_type(partner_dp,
							  &rx_desc_used_list[device_id],
							  reo_desc)) {
				num_buffs_reaped[device_id]++;
				tot_n_bufs_reaped++;
			}
			goto next_desc;
		}

		ret = ath12k_wifi7_hal_desc_reo_parse_err(dp, reo_desc, &paddr,
							  &desc_bank);
		if (ret) {
			ath12k_warn(ab, "failed to parse error reo desc %d\n",
				    ret);
			continue;
		}

		pdev_idx = ath12k_hw_mac_id_to_pdev_id(partner_dp->hw_params,
						       hw_links[hw_link_id].pdev_idx);

		link_desc_banks = partner_dp->link_desc_banks;
		link_desc_va = link_desc_banks[desc_bank].vaddr +
			       (paddr - link_desc_banks[desc_bank].paddr);
		ath12k_wifi7_hal_rx_msdu_link_info_get(link_desc_va, &num_msdus,
						       msdu_cookies, &rbm);
		if (rbm != partner_dp->idle_link_rbm &&
		    rbm != HAL_RX_BUF_RBM_SW3_BM &&
		    rbm != partner_dp->hal->hal_params->rx_buf_rbm) {
			act = HAL_WBM_REL_BM_ACT_REL_MSDU;
			dp->device_stats.invalid_rbm++;
			ath12k_warn(ab, "invalid return buffer manager %d\n", rbm);
			ath12k_wifi7_dp_rx_link_desc_return(partner_dp,
							    &reo_desc->buf_addr_info,
							    act);
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
		    partner_dp->device_id != dp->device_id) {
			drop = true;
			act = HAL_WBM_REL_BM_ACT_PUT_IN_IDLE;

			/* Return the link desc back to wbm idle list */
			ath12k_wifi7_dp_rx_link_desc_return(partner_dp,
							    &reo_desc->buf_addr_info,
							    act);
		}

		rcu_read_lock();

		dp_pdev = ath12k_dp_to_pdev_dp(dp, pdev_idx);
		if (!dp_pdev) {
			rcu_read_unlock();
			continue;
		}

		for (i = 0; i < num_msdus; i++) {
			used_list = &rx_desc_used_list[device_id];

			if (!ath12k_wifi7_dp_process_rx_err_buf(dp_pdev, reo_desc,
								used_list,
								drop,
								msdu_cookies[i])) {
				num_buffs_reaped[device_id]++;
				tot_n_bufs_reaped++;
			}
		}

		rcu_read_unlock();

next_desc:
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

		partner_dp = ath12k_dp_hw_grp_to_dp(dp_hw_grp, device_id);
		rx_ring = &partner_dp->rx_refill_buf_ring;

		ath12k_dp_rx_bufs_replenish(partner_dp, rx_ring,
					    &rx_desc_used_list[device_id],
					    num_buffs_reaped[device_id]);
	}

	return tot_n_bufs_reaped;
}

static void
ath12k_wifi7_dp_rx_null_q_desc_sg_drop(struct ath12k_dp *dp, int msdu_len,
				       struct sk_buff_head *msdu_list)
{
	struct sk_buff *skb, *tmp;
	struct ath12k_skb_rxcb *rxcb;
	int n_buffs;

	n_buffs = DIV_ROUND_UP(msdu_len,
			       (DP_RX_BUFFER_SIZE - dp->ab->hal.hal_desc_sz));

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

static int ath12k_wifi7_dp_rx_h_null_q_desc(struct ath12k_pdev_dp *dp_pdev,
					    struct sk_buff *msdu,
					    struct hal_rx_desc_data *rx_info,
					    struct sk_buff_head *msdu_list)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ath12k_base *ab = dp->ab;
	u16 msdu_len = rx_info->msdu_len;
	struct hal_rx_desc *desc = (struct hal_rx_desc *)msdu->data;
	u8 l3pad_bytes = rx_info->l3_pad_bytes;
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	u32 hal_rx_desc_sz = dp->ab->hal.hal_desc_sz;

	if (!rxcb->is_frag && ((msdu_len + hal_rx_desc_sz) > DP_RX_BUFFER_SIZE)) {
		/* First buffer will be freed by the caller, so deduct it's length */
		msdu_len = msdu_len - (DP_RX_BUFFER_SIZE - hal_rx_desc_sz);
		ath12k_wifi7_dp_rx_null_q_desc_sg_drop(dp, msdu_len, msdu_list);
		return -EINVAL;
	}

	/* Even after cleaning up the sg buffers in the msdu list with above check
	 * any msdu received with continuation flag needs to be dropped as invalid.
	 * This protects against some random err frame with continuation flag.
	 */
	if (rxcb->is_continuation)
		return -EINVAL;

	if (!rx_info->msdu_done) {
		ath12k_warn(ab,
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
		if ((hal_rx_desc_sz + l3pad_bytes + msdu_len) > DP_RX_BUFFER_SIZE)
			return -EINVAL;

		skb_put(msdu, hal_rx_desc_sz + l3pad_bytes + msdu_len);
		skb_pull(msdu, hal_rx_desc_sz + l3pad_bytes);
	}
	if (unlikely(!ath12k_dp_rx_check_nwifi_hdr_len_valid(dp, desc, msdu, rx_info)))
		return -EINVAL;

	ath12k_dp_rx_h_ppdu(dp_pdev, rx_info);
	ath12k_wifi7_dp_rx_h_mpdu(dp_pdev, msdu, desc, rx_info);

	rxcb->tid = rx_info->tid;

	/* Please note that caller will having the access to msdu and completing
	 * rx with mac80211. Need not worry about cleaning up amsdu_list.
	 */

	return 0;
}

static bool ath12k_wifi7_dp_rx_h_tkip_mic_err(struct ath12k_pdev_dp *dp_pdev,
					      struct sk_buff *msdu,
					      struct hal_rx_desc_data *rx_info)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ath12k_base *ab = dp->ab;
	u16 msdu_len = rx_info->msdu_len;
	struct hal_rx_desc *desc = (struct hal_rx_desc *)msdu->data;
	u8 l3pad_bytes = rx_info->l3_pad_bytes;
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	u32 hal_rx_desc_sz = ab->hal.hal_desc_sz;

	rxcb->is_first_msdu = rx_info->is_first_msdu;
	rxcb->is_last_msdu = rx_info->is_last_msdu;

	if ((hal_rx_desc_sz + l3pad_bytes + msdu_len) > DP_RX_BUFFER_SIZE) {
		ath12k_dbg(ab, ATH12K_DBG_DATA,
			   "invalid msdu len in tkip mic err %u\n", msdu_len);
		ath12k_dbg_dump(ab, ATH12K_DBG_DATA, NULL, "", desc,
				sizeof(*desc));
		return true;
	}

	skb_put(msdu, hal_rx_desc_sz + l3pad_bytes + msdu_len);
	skb_pull(msdu, hal_rx_desc_sz + l3pad_bytes);

	if (unlikely(!ath12k_dp_rx_check_nwifi_hdr_len_valid(dp, desc, msdu, rx_info)))
		return true;

	ath12k_dp_rx_h_ppdu(dp_pdev, rx_info);

	rx_info->rx_status->flag |= (RX_FLAG_MMIC_STRIPPED | RX_FLAG_MMIC_ERROR |
				     RX_FLAG_DECRYPTED);

	ath12k_dp_rx_h_undecap(dp_pdev, msdu, desc,
			       HAL_ENCRYPT_TYPE_TKIP_MIC, false, rx_info);
	return false;
}

static bool ath12k_wifi7_dp_rx_h_rxdma_err(struct ath12k_pdev_dp *dp_pdev,
					   struct sk_buff *msdu,
					   struct hal_rx_desc_data *rx_info)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	bool drop = false;

	dp->device_stats.rxdma_error[rxcb->err_code]++;

	switch (rxcb->err_code) {
	case HAL_REO_ENTR_RING_RXDMA_ECODE_DECRYPT_ERR:
	case HAL_REO_ENTR_RING_RXDMA_ECODE_TKIP_MIC_ERR:
		if (rx_info->err_bitmap & HAL_RX_MPDU_ERR_TKIP_MIC) {
			drop = ath12k_wifi7_dp_rx_h_tkip_mic_err(dp_pdev, msdu, rx_info);
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

static bool ath12k_wifi7_dp_rx_h_reo_err(struct ath12k_pdev_dp *dp_pdev,
					 struct sk_buff *msdu,
					 struct hal_rx_desc_data *rx_info,
					 struct sk_buff_head *msdu_list)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	bool drop = false;

	dp->device_stats.reo_error[rxcb->err_code]++;

	switch (rxcb->err_code) {
	case HAL_REO_DEST_RING_ERROR_CODE_DESC_ADDR_ZERO:
		if (ath12k_wifi7_dp_rx_h_null_q_desc(dp_pdev, msdu, rx_info, msdu_list))
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

static void ath12k_wifi7_dp_rx_wbm_err(struct ath12k_pdev_dp *dp_pdev,
				       struct napi_struct *napi,
				       struct sk_buff *msdu,
				       struct sk_buff_head *msdu_list)
{
	struct ath12k_dp *dp = dp_pdev->dp;
	struct hal_rx_desc *rx_desc = (struct hal_rx_desc *)msdu->data;
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	struct ieee80211_rx_status rxs = {};
	struct hal_rx_desc_data rx_info;
	bool drop = true;

	rx_info.addr2_present = false;
	rx_info.rx_status = &rxs;

	ath12k_dp_extract_rx_desc_data(dp->hal, &rx_info, rx_desc, rx_desc);

	switch (rxcb->err_rel_src) {
	case HAL_WBM_REL_SRC_MODULE_REO:
		drop = ath12k_wifi7_dp_rx_h_reo_err(dp_pdev, msdu, &rx_info, msdu_list);
		break;
	case HAL_WBM_REL_SRC_MODULE_RXDMA:
		drop = ath12k_wifi7_dp_rx_h_rxdma_err(dp_pdev, msdu, &rx_info);
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

	ath12k_dp_rx_deliver_msdu(dp_pdev, napi, msdu, &rx_info);
}

void ath12k_wifi7_dp_setup_pn_check_reo_cmd(struct ath12k_hal_reo_cmd *cmd,
					    struct ath12k_dp_rx_tid *rx_tid,
					    u32 cipher, enum set_key_cmd key_cmd)
{
	cmd->flag = HAL_REO_CMD_FLG_NEED_STATUS;
	cmd->upd0 = HAL_REO_CMD_UPD0_PN |
			HAL_REO_CMD_UPD0_PN_SIZE |
			HAL_REO_CMD_UPD0_PN_VALID |
			HAL_REO_CMD_UPD0_PN_CHECK |
			HAL_REO_CMD_UPD0_SVLD;

	switch (cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
		if (key_cmd == SET_KEY) {
			cmd->upd1 |= HAL_REO_CMD_UPD1_PN_CHECK;
			cmd->pn_size = 48;
		}
		break;
	default:
		break;
	}

	cmd->addr_lo = lower_32_bits(rx_tid->qbuf.paddr_aligned);
	cmd->addr_hi = upper_32_bits(rx_tid->qbuf.paddr_aligned);
}

int ath12k_wifi7_dp_rx_process_wbm_err(struct ath12k_dp *dp,
				       struct napi_struct *napi, int budget)
{
	struct list_head rx_desc_used_list[ATH12K_MAX_DEVICES];
	struct ath12k_base *ab = dp->ab;
	struct ath12k_hal *hal = dp->hal;
	struct ath12k *ar;
	struct ath12k_pdev_dp *dp_pdev;
	struct ath12k_hw_group *ag = dp->ag;
	struct ath12k_dp_hw_group *dp_hw_grp = &ag->dp_hw_grp;
	struct ath12k_dp *partner_dp;
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
	struct ath12k_device_dp_stats *device_stats = &dp->device_stats;
	struct ath12k_hw_link *hw_links = ag->hw_links;
	u8 hw_link_id, device_id;
	int ret, pdev_idx;
	struct hal_rx_desc *msdu_data;

	__skb_queue_head_init(&msdu_list);
	__skb_queue_head_init(&scatter_msdu_list);

	for (device_id = 0; device_id < ATH12K_MAX_DEVICES; device_id++)
		INIT_LIST_HEAD(&rx_desc_used_list[device_id]);

	srng = &hal->srng_list[dp->rx_rel_ring.ring_id];
	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

	while (budget) {
		rx_desc = ath12k_hal_srng_dst_get_next_entry(ab, srng);
		if (!rx_desc)
			break;

		ret = ath12k_wifi7_hal_wbm_desc_parse_err(dp, rx_desc,
							  &err_info);
		if (ret) {
			ath12k_warn(ab, "failed to parse rx error in wbm_rel ring desc %d\n",
				    ret);
			continue;
		}

		desc_info = err_info.rx_desc;

		/* retry manual desc retrieval if hw cc is not done */
		if (!desc_info) {
			desc_info = ath12k_dp_get_rx_desc(dp, err_info.cookie);
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
		partner_dp = ath12k_dp_hw_grp_to_dp(dp_hw_grp, device_id);
		if (unlikely(!partner_dp)) {
			dev_kfree_skb_any(msdu);

			/* In any case continuation bit is set
			 * in the previous record, cleanup scatter_msdu_list
			 */
			ath12k_dp_clean_up_skb_list(&scatter_msdu_list);
			continue;
		}

		list_add_tail(&desc_info->list, &rx_desc_used_list[device_id]);

		rxcb = ATH12K_SKB_RXCB(msdu);
		dma_unmap_single(partner_dp->dev, rxcb->paddr,
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
		rxcb->peer_id = ath12k_wifi7_dp_rx_get_peer_id(dp, dp->peer_metadata_ver,
							       err_info.peer_metadata);

		if (err_info.continuation) {
			__skb_queue_tail(&scatter_msdu_list, msdu);
			continue;
		}

		hw_link_id = ath12k_dp_rx_get_msdu_src_link(partner_dp->hal,
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

		partner_dp = ath12k_dp_hw_grp_to_dp(dp_hw_grp, device_id);
		rx_ring = &partner_dp->rx_refill_buf_ring;

		ath12k_dp_rx_bufs_replenish(dp, rx_ring,
					    &rx_desc_used_list[device_id],
					    num_buffs_reaped[device_id]);
	}

	rcu_read_lock();
	while ((msdu = __skb_dequeue(&msdu_list))) {
		rxcb = ATH12K_SKB_RXCB(msdu);
		hw_link_id = rxcb->hw_link_id;

		device_id = hw_links[hw_link_id].device_id;
		partner_dp = ath12k_dp_hw_grp_to_dp(dp_hw_grp, device_id);
		if (unlikely(!partner_dp)) {
			ath12k_dbg(ab, ATH12K_DBG_DATA,
				   "Unable to process WBM error msdu due to invalid hw link id %d device id %d\n",
				   hw_link_id, device_id);
			dev_kfree_skb_any(msdu);
			continue;
		}

		pdev_idx = ath12k_hw_mac_id_to_pdev_id(partner_dp->hw_params,
						       hw_links[hw_link_id].pdev_idx);

		dp_pdev = ath12k_dp_to_pdev_dp(partner_dp, pdev_idx);
		if (!dp_pdev) {
			dev_kfree_skb_any(msdu);
			continue;
		}
		ar = ath12k_pdev_dp_to_ar(dp_pdev);

		if (!ar || !rcu_dereference(ar->ab->pdevs_active[pdev_idx])) {
			dev_kfree_skb_any(msdu);
			continue;
		}

		if (test_bit(ATH12K_FLAG_CAC_RUNNING, &ar->dev_flags)) {
			dev_kfree_skb_any(msdu);
			continue;
		}

		if (rxcb->err_rel_src < HAL_WBM_REL_SRC_MODULE_MAX) {
			device_id = dp_pdev->dp->device_id;
			device_stats->rx_wbm_rel_source[rxcb->err_rel_src][device_id]++;
		}

		ath12k_wifi7_dp_rx_wbm_err(dp_pdev, napi, msdu, &msdu_list);
	}
	rcu_read_unlock();
done:
	return total_num_buffs_reaped;
}

int ath12k_dp_rxdma_ring_sel_config_qcn9274(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
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
		ath12k_hal_rx_desc_get_mpdu_start_offset_qcn9274();
	tlv_filter.rx_msdu_end_offset =
		ath12k_hal_rx_desc_get_msdu_end_offset_qcn9274();

	tlv_filter.rx_mpdu_start_wmask = ath12k_hal_rx_mpdu_start_wmask_get_qcn9274();
	tlv_filter.rx_msdu_end_wmask = ath12k_hal_rx_msdu_end_wmask_get_qcn9274();
	ath12k_dbg(ab, ATH12K_DBG_DATA,
		   "Configuring compact tlv masks rx_mpdu_start_wmask 0x%x rx_msdu_end_wmask 0x%x\n",
		   tlv_filter.rx_mpdu_start_wmask, tlv_filter.rx_msdu_end_wmask);

	ret = ath12k_dp_tx_htt_rx_filter_setup(ab, ring_id, 0,
					       HAL_RXDMA_BUF,
					       DP_RXDMA_REFILL_RING_SIZE,
					       &tlv_filter);

	return ret;
}

int ath12k_dp_rxdma_ring_sel_config_wcn7850(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
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
		ath12k_hal_rx_desc_get_mpdu_start_offset_wcn7850();
	tlv_filter.rx_msdu_end_offset =
		ath12k_hal_rx_desc_get_msdu_end_offset_wcn7850();

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

int ath12k_dp_rxdma_ring_sel_config_qcc2072(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
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

	tlv_filter.rx_header_offset = offsetof(struct hal_rx_desc_qcc2072, pkt_hdr_tlv);

	tlv_filter.rx_mpdu_start_offset =
		ath12k_hal_rx_desc_get_mpdu_start_offset_qcc2072();
	tlv_filter.rx_msdu_end_offset =
		ath12k_hal_rx_desc_get_msdu_end_offset_qcc2072();

	/*
	 * TODO: Selectively subscribe to required qwords within msdu_end
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

void ath12k_wifi7_dp_rx_process_reo_status(struct ath12k_dp *dp)
{
	struct ath12k_base *ab = dp->ab;
	struct ath12k_hal *hal = dp->hal;
	struct hal_srng *srng;
	struct ath12k_dp_rx_reo_cmd *cmd, *tmp;
	bool found = false;
	u16 tag;
	struct hal_reo_status reo_status;
	void *hdr, *desc;

	srng = &hal->srng_list[dp->reo_status_ring.ring_id];

	memset(&reo_status, 0, sizeof(reo_status));

	spin_lock_bh(&srng->lock);

	ath12k_hal_srng_access_begin(ab, srng);

	while ((hdr = ath12k_hal_srng_dst_get_next_entry(ab, srng))) {
		tag = hal->ops->reo_status_dec_tlv_hdr(hdr, &desc);

		switch (tag) {
		case HAL_REO_GET_QUEUE_STATS_STATUS:
			ath12k_wifi7_hal_reo_status_queue_stats(ab, desc,
								&reo_status);
			break;
		case HAL_REO_FLUSH_QUEUE_STATUS:
			ath12k_wifi7_hal_reo_flush_queue_status(ab, desc,
								&reo_status);
			break;
		case HAL_REO_FLUSH_CACHE_STATUS:
			ath12k_wifi7_hal_reo_flush_cache_status(ab, desc,
								&reo_status);
			break;
		case HAL_REO_UNBLOCK_CACHE_STATUS:
			ath12k_wifi7_hal_reo_unblk_cache_status(ab, desc,
								&reo_status);
			break;
		case HAL_REO_FLUSH_TIMEOUT_LIST_STATUS:
			ath12k_wifi7_hal_reo_flush_timeout_list_status(ab, desc,
								       &reo_status);
			break;
		case HAL_REO_DESCRIPTOR_THRESHOLD_REACHED_STATUS:
			ath12k_wifi7_hal_reo_desc_thresh_reached_status(ab, desc,
									&reo_status);
			break;
		case HAL_REO_UPDATE_RX_REO_QUEUE_STATUS:
			ath12k_wifi7_hal_reo_update_rx_reo_queue_status(ab, desc,
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

bool
ath12k_wifi7_dp_rxdesc_mpdu_valid(struct ath12k_base *ab,
				  struct hal_rx_desc *rx_desc)
{
	u32 tlv_tag;

	tlv_tag = ab->hal.ops->rx_desc_get_mpdu_start_tag(rx_desc);

	return tlv_tag == HAL_RX_MPDU_START;
}
