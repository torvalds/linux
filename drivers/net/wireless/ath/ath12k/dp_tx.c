// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "core.h"
#include "dp_tx.h"
#include "debug.h"
#include "hw.h"

static enum hal_tcl_encap_type
ath12k_dp_tx_get_encap_type(struct ath12k_vif *arvif, struct sk_buff *skb)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ath12k_base *ab = arvif->ar->ab;

	if (test_bit(ATH12K_FLAG_RAW_MODE, &ab->dev_flags))
		return HAL_TCL_ENCAP_TYPE_RAW;

	if (tx_info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP)
		return HAL_TCL_ENCAP_TYPE_ETHERNET;

	return HAL_TCL_ENCAP_TYPE_NATIVE_WIFI;
}

static void ath12k_dp_tx_encap_nwifi(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	u8 *qos_ctl;

	if (!ieee80211_is_data_qos(hdr->frame_control))
		return;

	qos_ctl = ieee80211_get_qos_ctl(hdr);
	memmove(skb->data + IEEE80211_QOS_CTL_LEN,
		skb->data, (void *)qos_ctl - (void *)skb->data);
	skb_pull(skb, IEEE80211_QOS_CTL_LEN);

	hdr = (void *)skb->data;
	hdr->frame_control &= ~__cpu_to_le16(IEEE80211_STYPE_QOS_DATA);
}

static u8 ath12k_dp_tx_get_tid(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	struct ath12k_skb_cb *cb = ATH12K_SKB_CB(skb);

	if (cb->flags & ATH12K_SKB_HW_80211_ENCAP)
		return skb->priority & IEEE80211_QOS_CTL_TID_MASK;
	else if (!ieee80211_is_data_qos(hdr->frame_control))
		return HAL_DESC_REO_NON_QOS_TID;
	else
		return skb->priority & IEEE80211_QOS_CTL_TID_MASK;
}

enum hal_encrypt_type ath12k_dp_tx_get_encrypt_type(u32 cipher)
{
	switch (cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		return HAL_ENCRYPT_TYPE_WEP_40;
	case WLAN_CIPHER_SUITE_WEP104:
		return HAL_ENCRYPT_TYPE_WEP_104;
	case WLAN_CIPHER_SUITE_TKIP:
		return HAL_ENCRYPT_TYPE_TKIP_MIC;
	case WLAN_CIPHER_SUITE_CCMP:
		return HAL_ENCRYPT_TYPE_CCMP_128;
	case WLAN_CIPHER_SUITE_CCMP_256:
		return HAL_ENCRYPT_TYPE_CCMP_256;
	case WLAN_CIPHER_SUITE_GCMP:
		return HAL_ENCRYPT_TYPE_GCMP_128;
	case WLAN_CIPHER_SUITE_GCMP_256:
		return HAL_ENCRYPT_TYPE_AES_GCMP_256;
	default:
		return HAL_ENCRYPT_TYPE_OPEN;
	}
}

static void ath12k_dp_tx_release_txbuf(struct ath12k_dp *dp,
				       struct ath12k_tx_desc_info *tx_desc,
				       u8 pool_id)
{
	spin_lock_bh(&dp->tx_desc_lock[pool_id]);
	list_move_tail(&tx_desc->list, &dp->tx_desc_free_list[pool_id]);
	spin_unlock_bh(&dp->tx_desc_lock[pool_id]);
}

static struct ath12k_tx_desc_info *ath12k_dp_tx_assign_buffer(struct ath12k_dp *dp,
							      u8 pool_id)
{
	struct ath12k_tx_desc_info *desc;

	spin_lock_bh(&dp->tx_desc_lock[pool_id]);
	desc = list_first_entry_or_null(&dp->tx_desc_free_list[pool_id],
					struct ath12k_tx_desc_info,
					list);
	if (!desc) {
		spin_unlock_bh(&dp->tx_desc_lock[pool_id]);
		ath12k_warn(dp->ab, "failed to allocate data Tx buffer\n");
		return NULL;
	}

	list_move_tail(&desc->list, &dp->tx_desc_used_list[pool_id]);
	spin_unlock_bh(&dp->tx_desc_lock[pool_id]);

	return desc;
}

static void ath12k_hal_tx_cmd_ext_desc_setup(struct ath12k_base *ab, void *cmd,
					     struct hal_tx_info *ti)
{
	struct hal_tx_msdu_ext_desc *tcl_ext_cmd = (struct hal_tx_msdu_ext_desc *)cmd;

	tcl_ext_cmd->info0 = le32_encode_bits(ti->paddr,
					      HAL_TX_MSDU_EXT_INFO0_BUF_PTR_LO);
	tcl_ext_cmd->info1 = le32_encode_bits(0x0,
					      HAL_TX_MSDU_EXT_INFO1_BUF_PTR_HI) |
			       le32_encode_bits(ti->data_len,
						HAL_TX_MSDU_EXT_INFO1_BUF_LEN);

	tcl_ext_cmd->info1 = le32_encode_bits(1, HAL_TX_MSDU_EXT_INFO1_EXTN_OVERRIDE) |
				le32_encode_bits(ti->encap_type,
						 HAL_TX_MSDU_EXT_INFO1_ENCAP_TYPE) |
				le32_encode_bits(ti->encrypt_type,
						 HAL_TX_MSDU_EXT_INFO1_ENCRYPT_TYPE);
}

int ath12k_dp_tx(struct ath12k *ar, struct ath12k_vif *arvif,
		 struct sk_buff *skb)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = &ab->dp;
	struct hal_tx_info ti = {0};
	struct ath12k_tx_desc_info *tx_desc;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ath12k_skb_cb *skb_cb = ATH12K_SKB_CB(skb);
	struct hal_tcl_data_cmd *hal_tcl_desc;
	struct hal_tx_msdu_ext_desc *msg;
	struct sk_buff *skb_ext_desc;
	struct hal_srng *tcl_ring;
	struct ieee80211_hdr *hdr = (void *)skb->data;
	struct dp_tx_ring *tx_ring;
	u8 pool_id;
	u8 hal_ring_id;
	int ret;
	u8 ring_selector, ring_map = 0;
	bool tcl_ring_retry;
	bool msdu_ext_desc = false;

	if (test_bit(ATH12K_FLAG_CRASH_FLUSH, &ar->ab->dev_flags))
		return -ESHUTDOWN;

	if (!(info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP) &&
	    !ieee80211_is_data(hdr->frame_control))
		return -ENOTSUPP;

	pool_id = skb_get_queue_mapping(skb) & (ATH12K_HW_MAX_QUEUES - 1);

	/* Let the default ring selection be based on current processor
	 * number, where one of the 3 tcl rings are selected based on
	 * the smp_processor_id(). In case that ring
	 * is full/busy, we resort to other available rings.
	 * If all rings are full, we drop the packet.
	 * TODO: Add throttling logic when all rings are full
	 */
	ring_selector = ab->hw_params->hw_ops->get_ring_selector(skb);

tcl_ring_sel:
	tcl_ring_retry = false;
	ti.ring_id = ring_selector % ab->hw_params->max_tx_ring;

	ring_map |= BIT(ti.ring_id);
	ti.rbm_id = ab->hw_params->hal_ops->tcl_to_wbm_rbm_map[ti.ring_id].rbm_id;

	tx_ring = &dp->tx_ring[ti.ring_id];

	tx_desc = ath12k_dp_tx_assign_buffer(dp, pool_id);
	if (!tx_desc)
		return -ENOMEM;

	ti.bank_id = arvif->bank_id;
	ti.meta_data_flags = arvif->tcl_metadata;

	if (arvif->tx_encap_type == HAL_TCL_ENCAP_TYPE_RAW &&
	    test_bit(ATH12K_FLAG_HW_CRYPTO_DISABLED, &ar->ab->dev_flags)) {
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

	ti.encap_type = ath12k_dp_tx_get_encap_type(arvif, skb);
	ti.addr_search_flags = arvif->hal_addr_search_flags;
	ti.search_type = arvif->search_type;
	ti.type = HAL_TCL_DESC_TYPE_BUFFER;
	ti.pkt_offset = 0;
	ti.lmac_id = ar->lmac_id;
	ti.vdev_id = arvif->vdev_id;
	ti.bss_ast_hash = arvif->ast_hash;
	ti.bss_ast_idx = arvif->ast_idx;
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
		ath12k_dp_tx_encap_nwifi(skb);
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
		atomic_inc(&ab->soc_stats.tx_err.misc_fail);
		goto fail_remove_tx_buf;
	}

	ti.paddr = dma_map_single(ab->dev, skb->data, skb->len, DMA_TO_DEVICE);
	if (dma_mapping_error(ab->dev, ti.paddr)) {
		atomic_inc(&ab->soc_stats.tx_err.misc_fail);
		ath12k_warn(ab, "failed to DMA map data Tx buffer\n");
		ret = -ENOMEM;
		goto fail_remove_tx_buf;
	}

	tx_desc->skb = skb;
	tx_desc->mac_id = ar->pdev_idx;
	ti.desc_id = tx_desc->desc_id;
	ti.data_len = skb->len;
	skb_cb->paddr = ti.paddr;
	skb_cb->vif = arvif->vif;
	skb_cb->ar = ar;

	if (msdu_ext_desc) {
		skb_ext_desc = dev_alloc_skb(sizeof(struct hal_tx_msdu_ext_desc));
		if (!skb_ext_desc) {
			ret = -ENOMEM;
			goto fail_unmap_dma;
		}

		skb_put(skb_ext_desc, sizeof(struct hal_tx_msdu_ext_desc));
		memset(skb_ext_desc->data, 0, skb_ext_desc->len);

		msg = (struct hal_tx_msdu_ext_desc *)skb_ext_desc->data;
		ath12k_hal_tx_cmd_ext_desc_setup(ab, msg, &ti);

		ti.paddr = dma_map_single(ab->dev, skb_ext_desc->data,
					  skb_ext_desc->len, DMA_TO_DEVICE);
		ret = dma_mapping_error(ab->dev, ti.paddr);
		if (ret) {
			kfree_skb(skb_ext_desc);
			goto fail_unmap_dma;
		}

		ti.data_len = skb_ext_desc->len;
		ti.type = HAL_TCL_DESC_TYPE_EXT_DESC;

		skb_cb->paddr_ext_desc = ti.paddr;
	}

	hal_ring_id = tx_ring->tcl_data_ring.ring_id;
	tcl_ring = &ab->hal.srng_list[hal_ring_id];

	spin_lock_bh(&tcl_ring->lock);

	ath12k_hal_srng_access_begin(ab, tcl_ring);

	hal_tcl_desc = ath12k_hal_srng_src_get_next_entry(ab, tcl_ring);
	if (!hal_tcl_desc) {
		/* NOTE: It is highly unlikely we'll be running out of tcl_ring
		 * desc because the desc is directly enqueued onto hw queue.
		 */
		ath12k_hal_srng_access_end(ab, tcl_ring);
		ab->soc_stats.tx_err.desc_na[ti.ring_id]++;
		spin_unlock_bh(&tcl_ring->lock);
		ret = -ENOMEM;

		/* Checking for available tcl descriptors in another ring in
		 * case of failure due to full tcl ring now, is better than
		 * checking this ring earlier for each pkt tx.
		 * Restart ring selection if some rings are not checked yet.
		 */
		if (ring_map != (BIT(ab->hw_params->max_tx_ring) - 1) &&
		    ab->hw_params->tcl_ring_retry) {
			tcl_ring_retry = true;
			ring_selector++;
		}

		goto fail_unmap_dma;
	}

	ath12k_hal_tx_cmd_desc_setup(ab, hal_tcl_desc, &ti);

	ath12k_hal_srng_access_end(ab, tcl_ring);

	spin_unlock_bh(&tcl_ring->lock);

	ath12k_dbg_dump(ab, ATH12K_DBG_DP_TX, NULL, "dp tx msdu: ",
			skb->data, skb->len);

	atomic_inc(&ar->dp.num_tx_pending);

	return 0;

fail_unmap_dma:
	dma_unmap_single(ab->dev, ti.paddr, ti.data_len, DMA_TO_DEVICE);
	dma_unmap_single(ab->dev, skb_cb->paddr_ext_desc,
			 sizeof(struct hal_tx_msdu_ext_desc), DMA_TO_DEVICE);

fail_remove_tx_buf:
	ath12k_dp_tx_release_txbuf(dp, tx_desc, pool_id);
	if (tcl_ring_retry)
		goto tcl_ring_sel;

	return ret;
}

static void ath12k_dp_tx_free_txbuf(struct ath12k_base *ab,
				    struct sk_buff *msdu, u8 mac_id,
				    struct dp_tx_ring *tx_ring)
{
	struct ath12k *ar;
	struct ath12k_skb_cb *skb_cb;
	u8 pdev_id = ath12k_hw_mac_id_to_pdev_id(ab->hw_params, mac_id);

	skb_cb = ATH12K_SKB_CB(msdu);

	dma_unmap_single(ab->dev, skb_cb->paddr, msdu->len, DMA_TO_DEVICE);
	if (skb_cb->paddr_ext_desc)
		dma_unmap_single(ab->dev, skb_cb->paddr_ext_desc,
				 sizeof(struct hal_tx_msdu_ext_desc), DMA_TO_DEVICE);

	dev_kfree_skb_any(msdu);

	ar = ab->pdevs[pdev_id].ar;
	if (atomic_dec_and_test(&ar->dp.num_tx_pending))
		wake_up(&ar->dp.tx_empty_waitq);
}

static void
ath12k_dp_tx_htt_tx_complete_buf(struct ath12k_base *ab,
				 struct sk_buff *msdu,
				 struct dp_tx_ring *tx_ring,
				 struct ath12k_dp_htt_wbm_tx_status *ts)
{
	struct ieee80211_tx_info *info;
	struct ath12k_skb_cb *skb_cb;
	struct ath12k *ar;

	skb_cb = ATH12K_SKB_CB(msdu);
	info = IEEE80211_SKB_CB(msdu);

	ar = skb_cb->ar;

	if (atomic_dec_and_test(&ar->dp.num_tx_pending))
		wake_up(&ar->dp.tx_empty_waitq);

	dma_unmap_single(ab->dev, skb_cb->paddr, msdu->len, DMA_TO_DEVICE);
	if (skb_cb->paddr_ext_desc)
		dma_unmap_single(ab->dev, skb_cb->paddr_ext_desc,
				 sizeof(struct hal_tx_msdu_ext_desc), DMA_TO_DEVICE);

	memset(&info->status, 0, sizeof(info->status));

	if (ts->acked) {
		if (!(info->flags & IEEE80211_TX_CTL_NO_ACK)) {
			info->flags |= IEEE80211_TX_STAT_ACK;
			info->status.ack_signal = ATH12K_DEFAULT_NOISE_FLOOR +
						  ts->ack_rssi;
			info->status.flags = IEEE80211_TX_STATUS_ACK_SIGNAL_VALID;
		} else {
			info->flags |= IEEE80211_TX_STAT_NOACK_TRANSMITTED;
		}
	}

	ieee80211_tx_status(ar->hw, msdu);
}

static void
ath12k_dp_tx_process_htt_tx_complete(struct ath12k_base *ab,
				     void *desc, u8 mac_id,
				     struct sk_buff *msdu,
				     struct dp_tx_ring *tx_ring)
{
	struct htt_tx_wbm_completion *status_desc;
	struct ath12k_dp_htt_wbm_tx_status ts = {0};
	enum hal_wbm_htt_tx_comp_status wbm_status;

	status_desc = desc + HTT_TX_WBM_COMP_STATUS_OFFSET;

	wbm_status = le32_get_bits(status_desc->info0,
				   HTT_TX_WBM_COMP_INFO0_STATUS);

	switch (wbm_status) {
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_OK:
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_DROP:
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_TTL:
		ts.acked = (wbm_status == HAL_WBM_REL_HTT_TX_COMP_STATUS_OK);
		ts.ack_rssi = le32_get_bits(status_desc->info2,
					    HTT_TX_WBM_COMP_INFO2_ACK_RSSI);
		ath12k_dp_tx_htt_tx_complete_buf(ab, msdu, tx_ring, &ts);
		break;
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_REINJ:
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_INSPECT:
		ath12k_dp_tx_free_txbuf(ab, msdu, mac_id, tx_ring);
		break;
	case HAL_WBM_REL_HTT_TX_COMP_STATUS_MEC_NOTIFY:
		/* This event is to be handled only when the driver decides to
		 * use WDS offload functionality.
		 */
		break;
	default:
		ath12k_warn(ab, "Unknown htt tx status %d\n", wbm_status);
		break;
	}
}

static void ath12k_dp_tx_complete_msdu(struct ath12k *ar,
				       struct sk_buff *msdu,
				       struct hal_tx_status *ts)
{
	struct ath12k_base *ab = ar->ab;
	struct ieee80211_tx_info *info;
	struct ath12k_skb_cb *skb_cb;

	if (WARN_ON_ONCE(ts->buf_rel_source != HAL_WBM_REL_SRC_MODULE_TQM)) {
		/* Must not happen */
		return;
	}

	skb_cb = ATH12K_SKB_CB(msdu);

	dma_unmap_single(ab->dev, skb_cb->paddr, msdu->len, DMA_TO_DEVICE);
	if (skb_cb->paddr_ext_desc)
		dma_unmap_single(ab->dev, skb_cb->paddr_ext_desc,
				 sizeof(struct hal_tx_msdu_ext_desc), DMA_TO_DEVICE);

	rcu_read_lock();

	if (!rcu_dereference(ab->pdevs_active[ar->pdev_idx])) {
		dev_kfree_skb_any(msdu);
		goto exit;
	}

	if (!skb_cb->vif) {
		dev_kfree_skb_any(msdu);
		goto exit;
	}

	info = IEEE80211_SKB_CB(msdu);
	memset(&info->status, 0, sizeof(info->status));

	/* skip tx rate update from ieee80211_status*/
	info->status.rates[0].idx = -1;

	if (ts->status == HAL_WBM_TQM_REL_REASON_FRAME_ACKED &&
	    !(info->flags & IEEE80211_TX_CTL_NO_ACK)) {
		info->flags |= IEEE80211_TX_STAT_ACK;
		info->status.ack_signal = ATH12K_DEFAULT_NOISE_FLOOR +
					  ts->ack_rssi;
		info->status.flags = IEEE80211_TX_STATUS_ACK_SIGNAL_VALID;
	}

	if (ts->status == HAL_WBM_TQM_REL_REASON_CMD_REMOVE_TX &&
	    (info->flags & IEEE80211_TX_CTL_NO_ACK))
		info->flags |= IEEE80211_TX_STAT_NOACK_TRANSMITTED;

	/* NOTE: Tx rate status reporting. Tx completion status does not have
	 * necessary information (for example nss) to build the tx rate.
	 * Might end up reporting it out-of-band from HTT stats.
	 */

	ieee80211_tx_status(ar->hw, msdu);

exit:
	rcu_read_unlock();
}

static void ath12k_dp_tx_status_parse(struct ath12k_base *ab,
				      struct hal_wbm_completion_ring_tx *desc,
				      struct hal_tx_status *ts)
{
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
	if (le32_to_cpu(desc->rate_stats.info0) & HAL_TX_RATE_STATS_INFO0_VALID)
		ts->rate_stats = le32_to_cpu(desc->rate_stats.info0);
	else
		ts->rate_stats = 0;
}

void ath12k_dp_tx_completion_handler(struct ath12k_base *ab, int ring_id)
{
	struct ath12k *ar;
	struct ath12k_dp *dp = &ab->dp;
	int hal_ring_id = dp->tx_ring[ring_id].tcl_comp_ring.ring_id;
	struct hal_srng *status_ring = &ab->hal.srng_list[hal_ring_id];
	struct ath12k_tx_desc_info *tx_desc = NULL;
	struct sk_buff *msdu;
	struct hal_tx_status ts = { 0 };
	struct dp_tx_ring *tx_ring = &dp->tx_ring[ring_id];
	struct hal_wbm_release_ring *desc;
	u8 mac_id, pdev_id;
	u64 desc_va;

	spin_lock_bh(&status_ring->lock);

	ath12k_hal_srng_access_begin(ab, status_ring);

	while (ATH12K_TX_COMPL_NEXT(tx_ring->tx_status_head) != tx_ring->tx_status_tail) {
		desc = ath12k_hal_srng_dst_get_next_entry(ab, status_ring);
		if (!desc)
			break;

		memcpy(&tx_ring->tx_status[tx_ring->tx_status_head],
		       desc, sizeof(*desc));
		tx_ring->tx_status_head =
			ATH12K_TX_COMPL_NEXT(tx_ring->tx_status_head);
	}

	if (ath12k_hal_srng_dst_peek(ab, status_ring) &&
	    (ATH12K_TX_COMPL_NEXT(tx_ring->tx_status_head) == tx_ring->tx_status_tail)) {
		/* TODO: Process pending tx_status messages when kfifo_is_full() */
		ath12k_warn(ab, "Unable to process some of the tx_status ring desc because status_fifo is full\n");
	}

	ath12k_hal_srng_access_end(ab, status_ring);

	spin_unlock_bh(&status_ring->lock);

	while (ATH12K_TX_COMPL_NEXT(tx_ring->tx_status_tail) != tx_ring->tx_status_head) {
		struct hal_wbm_completion_ring_tx *tx_status;
		u32 desc_id;

		tx_ring->tx_status_tail =
			ATH12K_TX_COMPL_NEXT(tx_ring->tx_status_tail);
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

		msdu = tx_desc->skb;
		mac_id = tx_desc->mac_id;

		/* Release descriptor as soon as extracting necessary info
		 * to reduce contention
		 */
		ath12k_dp_tx_release_txbuf(dp, tx_desc, tx_desc->pool_id);
		if (ts.buf_rel_source == HAL_WBM_REL_SRC_MODULE_FW) {
			ath12k_dp_tx_process_htt_tx_complete(ab,
							     (void *)tx_status,
							     mac_id, msdu,
							     tx_ring);
			continue;
		}

		pdev_id = ath12k_hw_mac_id_to_pdev_id(ab->hw_params, mac_id);
		ar = ab->pdevs[pdev_id].ar;

		if (atomic_dec_and_test(&ar->dp.num_tx_pending))
			wake_up(&ar->dp.tx_empty_waitq);

		ath12k_dp_tx_complete_msdu(ar, msdu, &ts);
	}
}

static int
ath12k_dp_tx_get_ring_id_type(struct ath12k_base *ab,
			      int mac_id, u32 ring_id,
			      enum hal_ring_type ring_type,
			      enum htt_srng_ring_type *htt_ring_type,
			      enum htt_srng_ring_id *htt_ring_id)
{
	int ret = 0;

	switch (ring_type) {
	case HAL_RXDMA_BUF:
		/* for some targets, host fills rx buffer to fw and fw fills to
		 * rxbuf ring for each rxdma
		 */
		if (!ab->hw_params->rx_mac_buf_ring) {
			if (!(ring_id == HAL_SRNG_SW2RXDMA_BUF0 ||
			      ring_id == HAL_SRNG_SW2RXDMA_BUF1)) {
				ret = -EINVAL;
			}
			*htt_ring_id = HTT_RXDMA_HOST_BUF_RING;
			*htt_ring_type = HTT_SW_TO_HW_RING;
		} else {
			if (ring_id == HAL_SRNG_SW2RXDMA_BUF0) {
				*htt_ring_id = HTT_HOST1_TO_FW_RXBUF_RING;
				*htt_ring_type = HTT_SW_TO_SW_RING;
			} else {
				*htt_ring_id = HTT_RXDMA_HOST_BUF_RING;
				*htt_ring_type = HTT_SW_TO_HW_RING;
			}
		}
		break;
	case HAL_RXDMA_DST:
		*htt_ring_id = HTT_RXDMA_NON_MONITOR_DEST_RING;
		*htt_ring_type = HTT_HW_TO_SW_RING;
		break;
	case HAL_RXDMA_MONITOR_BUF:
		*htt_ring_id = HTT_RXDMA_MONITOR_BUF_RING;
		*htt_ring_type = HTT_SW_TO_HW_RING;
		break;
	case HAL_RXDMA_MONITOR_STATUS:
		*htt_ring_id = HTT_RXDMA_MONITOR_STATUS_RING;
		*htt_ring_type = HTT_SW_TO_HW_RING;
		break;
	case HAL_RXDMA_MONITOR_DST:
		*htt_ring_id = HTT_RXDMA_MONITOR_DEST_RING;
		*htt_ring_type = HTT_HW_TO_SW_RING;
		break;
	case HAL_RXDMA_MONITOR_DESC:
		*htt_ring_id = HTT_RXDMA_MONITOR_DESC_RING;
		*htt_ring_type = HTT_SW_TO_HW_RING;
		break;
	case HAL_TX_MONITOR_BUF:
		*htt_ring_id = HTT_TX_MON_HOST2MON_BUF_RING;
		*htt_ring_type = HTT_SW_TO_HW_RING;
		break;
	case HAL_TX_MONITOR_DST:
		*htt_ring_id = HTT_TX_MON_MON2HOST_DEST_RING;
		*htt_ring_type = HTT_HW_TO_SW_RING;
		break;
	default:
		ath12k_warn(ab, "Unsupported ring type in DP :%d\n", ring_type);
		ret = -EINVAL;
	}
	return ret;
}

int ath12k_dp_tx_htt_srng_setup(struct ath12k_base *ab, u32 ring_id,
				int mac_id, enum hal_ring_type ring_type)
{
	struct htt_srng_setup_cmd *cmd;
	struct hal_srng *srng = &ab->hal.srng_list[ring_id];
	struct hal_srng_params params;
	struct sk_buff *skb;
	u32 ring_entry_sz;
	int len = sizeof(*cmd);
	dma_addr_t hp_addr, tp_addr;
	enum htt_srng_ring_type htt_ring_type;
	enum htt_srng_ring_id htt_ring_id;
	int ret;

	skb = ath12k_htc_alloc_skb(ab, len);
	if (!skb)
		return -ENOMEM;

	memset(&params, 0, sizeof(params));
	ath12k_hal_srng_get_params(ab, srng, &params);

	hp_addr = ath12k_hal_srng_get_hp_addr(ab, srng);
	tp_addr = ath12k_hal_srng_get_tp_addr(ab, srng);

	ret = ath12k_dp_tx_get_ring_id_type(ab, mac_id, ring_id,
					    ring_type, &htt_ring_type,
					    &htt_ring_id);
	if (ret)
		goto err_free;

	skb_put(skb, len);
	cmd = (struct htt_srng_setup_cmd *)skb->data;
	cmd->info0 = le32_encode_bits(HTT_H2T_MSG_TYPE_SRING_SETUP,
				      HTT_SRNG_SETUP_CMD_INFO0_MSG_TYPE);
	if (htt_ring_type == HTT_SW_TO_HW_RING ||
	    htt_ring_type == HTT_HW_TO_SW_RING)
		cmd->info0 |= le32_encode_bits(DP_SW2HW_MACID(mac_id),
					       HTT_SRNG_SETUP_CMD_INFO0_PDEV_ID);
	else
		cmd->info0 |= le32_encode_bits(mac_id,
					       HTT_SRNG_SETUP_CMD_INFO0_PDEV_ID);
	cmd->info0 |= le32_encode_bits(htt_ring_type,
				       HTT_SRNG_SETUP_CMD_INFO0_RING_TYPE);
	cmd->info0 |= le32_encode_bits(htt_ring_id,
				       HTT_SRNG_SETUP_CMD_INFO0_RING_ID);

	cmd->ring_base_addr_lo = cpu_to_le32(params.ring_base_paddr &
					     HAL_ADDR_LSB_REG_MASK);

	cmd->ring_base_addr_hi = cpu_to_le32((u64)params.ring_base_paddr >>
					     HAL_ADDR_MSB_REG_SHIFT);

	ret = ath12k_hal_srng_get_entrysize(ab, ring_type);
	if (ret < 0)
		goto err_free;

	ring_entry_sz = ret;

	ring_entry_sz >>= 2;
	cmd->info1 = le32_encode_bits(ring_entry_sz,
				      HTT_SRNG_SETUP_CMD_INFO1_RING_ENTRY_SIZE);
	cmd->info1 |= le32_encode_bits(params.num_entries * ring_entry_sz,
				       HTT_SRNG_SETUP_CMD_INFO1_RING_SIZE);
	cmd->info1 |= le32_encode_bits(!!(params.flags & HAL_SRNG_FLAGS_MSI_SWAP),
				       HTT_SRNG_SETUP_CMD_INFO1_RING_FLAGS_MSI_SWAP);
	cmd->info1 |= le32_encode_bits(!!(params.flags & HAL_SRNG_FLAGS_DATA_TLV_SWAP),
				       HTT_SRNG_SETUP_CMD_INFO1_RING_FLAGS_TLV_SWAP);
	cmd->info1 |= le32_encode_bits(!!(params.flags & HAL_SRNG_FLAGS_RING_PTR_SWAP),
				       HTT_SRNG_SETUP_CMD_INFO1_RING_FLAGS_HOST_FW_SWAP);
	if (htt_ring_type == HTT_SW_TO_HW_RING)
		cmd->info1 |= cpu_to_le32(HTT_SRNG_SETUP_CMD_INFO1_RING_LOOP_CNT_DIS);

	cmd->ring_head_off32_remote_addr_lo = cpu_to_le32(lower_32_bits(hp_addr));
	cmd->ring_head_off32_remote_addr_hi = cpu_to_le32(upper_32_bits(hp_addr));

	cmd->ring_tail_off32_remote_addr_lo = cpu_to_le32(lower_32_bits(tp_addr));
	cmd->ring_tail_off32_remote_addr_hi = cpu_to_le32(upper_32_bits(tp_addr));

	cmd->ring_msi_addr_lo = cpu_to_le32(lower_32_bits(params.msi_addr));
	cmd->ring_msi_addr_hi = cpu_to_le32(upper_32_bits(params.msi_addr));
	cmd->msi_data = cpu_to_le32(params.msi_data);

	cmd->intr_info =
		le32_encode_bits(params.intr_batch_cntr_thres_entries * ring_entry_sz,
				 HTT_SRNG_SETUP_CMD_INTR_INFO_BATCH_COUNTER_THRESH);
	cmd->intr_info |=
		le32_encode_bits(params.intr_timer_thres_us >> 3,
				 HTT_SRNG_SETUP_CMD_INTR_INFO_INTR_TIMER_THRESH);

	cmd->info2 = 0;
	if (params.flags & HAL_SRNG_FLAGS_LOW_THRESH_INTR_EN) {
		cmd->info2 = le32_encode_bits(params.low_threshold,
					      HTT_SRNG_SETUP_CMD_INFO2_INTR_LOW_THRESH);
	}

	ath12k_dbg(ab, ATH12K_DBG_HAL,
		   "%s msi_addr_lo:0x%x, msi_addr_hi:0x%x, msi_data:0x%x\n",
		   __func__, cmd->ring_msi_addr_lo, cmd->ring_msi_addr_hi,
		   cmd->msi_data);

	ath12k_dbg(ab, ATH12K_DBG_HAL,
		   "ring_id:%d, ring_type:%d, intr_info:0x%x, flags:0x%x\n",
		   ring_id, ring_type, cmd->intr_info, cmd->info2);

	ret = ath12k_htc_send(&ab->htc, ab->dp.eid, skb);
	if (ret)
		goto err_free;

	return 0;

err_free:
	dev_kfree_skb_any(skb);

	return ret;
}

#define HTT_TARGET_VERSION_TIMEOUT_HZ (3 * HZ)

int ath12k_dp_tx_htt_h2t_ver_req_msg(struct ath12k_base *ab)
{
	struct ath12k_dp *dp = &ab->dp;
	struct sk_buff *skb;
	struct htt_ver_req_cmd *cmd;
	int len = sizeof(*cmd);
	int ret;

	init_completion(&dp->htt_tgt_version_received);

	skb = ath12k_htc_alloc_skb(ab, len);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, len);
	cmd = (struct htt_ver_req_cmd *)skb->data;
	cmd->ver_reg_info = le32_encode_bits(HTT_H2T_MSG_TYPE_VERSION_REQ,
					     HTT_VER_REQ_INFO_MSG_ID);

	ret = ath12k_htc_send(&ab->htc, dp->eid, skb);
	if (ret) {
		dev_kfree_skb_any(skb);
		return ret;
	}

	ret = wait_for_completion_timeout(&dp->htt_tgt_version_received,
					  HTT_TARGET_VERSION_TIMEOUT_HZ);
	if (ret == 0) {
		ath12k_warn(ab, "htt target version request timed out\n");
		return -ETIMEDOUT;
	}

	if (dp->htt_tgt_ver_major != HTT_TARGET_VERSION_MAJOR) {
		ath12k_err(ab, "unsupported htt major version %d supported version is %d\n",
			   dp->htt_tgt_ver_major, HTT_TARGET_VERSION_MAJOR);
		return -ENOTSUPP;
	}

	return 0;
}

int ath12k_dp_tx_htt_h2t_ppdu_stats_req(struct ath12k *ar, u32 mask)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = &ab->dp;
	struct sk_buff *skb;
	struct htt_ppdu_stats_cfg_cmd *cmd;
	int len = sizeof(*cmd);
	u8 pdev_mask;
	int ret;
	int i;

	for (i = 0; i < ab->hw_params->num_rxmda_per_pdev; i++) {
		skb = ath12k_htc_alloc_skb(ab, len);
		if (!skb)
			return -ENOMEM;

		skb_put(skb, len);
		cmd = (struct htt_ppdu_stats_cfg_cmd *)skb->data;
		cmd->msg = le32_encode_bits(HTT_H2T_MSG_TYPE_PPDU_STATS_CFG,
					    HTT_PPDU_STATS_CFG_MSG_TYPE);

		pdev_mask = 1 << (i + 1);
		cmd->msg |= le32_encode_bits(pdev_mask, HTT_PPDU_STATS_CFG_PDEV_ID);
		cmd->msg |= le32_encode_bits(mask, HTT_PPDU_STATS_CFG_TLV_TYPE_BITMASK);

		ret = ath12k_htc_send(&ab->htc, dp->eid, skb);
		if (ret) {
			dev_kfree_skb_any(skb);
			return ret;
		}
	}

	return 0;
}

int ath12k_dp_tx_htt_rx_filter_setup(struct ath12k_base *ab, u32 ring_id,
				     int mac_id, enum hal_ring_type ring_type,
				     int rx_buf_size,
				     struct htt_rx_ring_tlv_filter *tlv_filter)
{
	struct htt_rx_ring_selection_cfg_cmd *cmd;
	struct hal_srng *srng = &ab->hal.srng_list[ring_id];
	struct hal_srng_params params;
	struct sk_buff *skb;
	int len = sizeof(*cmd);
	enum htt_srng_ring_type htt_ring_type;
	enum htt_srng_ring_id htt_ring_id;
	int ret;

	skb = ath12k_htc_alloc_skb(ab, len);
	if (!skb)
		return -ENOMEM;

	memset(&params, 0, sizeof(params));
	ath12k_hal_srng_get_params(ab, srng, &params);

	ret = ath12k_dp_tx_get_ring_id_type(ab, mac_id, ring_id,
					    ring_type, &htt_ring_type,
					    &htt_ring_id);
	if (ret)
		goto err_free;

	skb_put(skb, len);
	cmd = (struct htt_rx_ring_selection_cfg_cmd *)skb->data;
	cmd->info0 = le32_encode_bits(HTT_H2T_MSG_TYPE_RX_RING_SELECTION_CFG,
				      HTT_RX_RING_SELECTION_CFG_CMD_INFO0_MSG_TYPE);
	if (htt_ring_type == HTT_SW_TO_HW_RING ||
	    htt_ring_type == HTT_HW_TO_SW_RING)
		cmd->info0 |=
			le32_encode_bits(DP_SW2HW_MACID(mac_id),
					 HTT_RX_RING_SELECTION_CFG_CMD_INFO0_PDEV_ID);
	else
		cmd->info0 |=
			le32_encode_bits(mac_id,
					 HTT_RX_RING_SELECTION_CFG_CMD_INFO0_PDEV_ID);
	cmd->info0 |= le32_encode_bits(htt_ring_id,
				       HTT_RX_RING_SELECTION_CFG_CMD_INFO0_RING_ID);
	cmd->info0 |= le32_encode_bits(!!(params.flags & HAL_SRNG_FLAGS_MSI_SWAP),
				       HTT_RX_RING_SELECTION_CFG_CMD_INFO0_SS);
	cmd->info0 |= le32_encode_bits(!!(params.flags & HAL_SRNG_FLAGS_DATA_TLV_SWAP),
				       HTT_RX_RING_SELECTION_CFG_CMD_INFO0_PS);
	cmd->info0 |= le32_encode_bits(tlv_filter->offset_valid,
				       HTT_RX_RING_SELECTION_CFG_CMD_OFFSET_VALID);
	cmd->info1 = le32_encode_bits(rx_buf_size,
				      HTT_RX_RING_SELECTION_CFG_CMD_INFO1_BUF_SIZE);
	cmd->pkt_type_en_flags0 = cpu_to_le32(tlv_filter->pkt_filter_flags0);
	cmd->pkt_type_en_flags1 = cpu_to_le32(tlv_filter->pkt_filter_flags1);
	cmd->pkt_type_en_flags2 = cpu_to_le32(tlv_filter->pkt_filter_flags2);
	cmd->pkt_type_en_flags3 = cpu_to_le32(tlv_filter->pkt_filter_flags3);
	cmd->rx_filter_tlv = cpu_to_le32(tlv_filter->rx_filter);

	if (tlv_filter->offset_valid) {
		cmd->rx_packet_offset =
			le32_encode_bits(tlv_filter->rx_packet_offset,
					 HTT_RX_RING_SELECTION_CFG_RX_PACKET_OFFSET);

		cmd->rx_packet_offset |=
			le32_encode_bits(tlv_filter->rx_header_offset,
					 HTT_RX_RING_SELECTION_CFG_RX_HEADER_OFFSET);

		cmd->rx_mpdu_offset =
			le32_encode_bits(tlv_filter->rx_mpdu_end_offset,
					 HTT_RX_RING_SELECTION_CFG_RX_MPDU_END_OFFSET);

		cmd->rx_mpdu_offset |=
			le32_encode_bits(tlv_filter->rx_mpdu_start_offset,
					 HTT_RX_RING_SELECTION_CFG_RX_MPDU_START_OFFSET);

		cmd->rx_msdu_offset =
			le32_encode_bits(tlv_filter->rx_msdu_end_offset,
					 HTT_RX_RING_SELECTION_CFG_RX_MSDU_END_OFFSET);

		cmd->rx_msdu_offset |=
			le32_encode_bits(tlv_filter->rx_msdu_start_offset,
					 HTT_RX_RING_SELECTION_CFG_RX_MSDU_START_OFFSET);

		cmd->rx_attn_offset =
			le32_encode_bits(tlv_filter->rx_attn_offset,
					 HTT_RX_RING_SELECTION_CFG_RX_ATTENTION_OFFSET);
	}

	ret = ath12k_htc_send(&ab->htc, ab->dp.eid, skb);
	if (ret)
		goto err_free;

	return 0;

err_free:
	dev_kfree_skb_any(skb);

	return ret;
}

int
ath12k_dp_tx_htt_h2t_ext_stats_req(struct ath12k *ar, u8 type,
				   struct htt_ext_stats_cfg_params *cfg_params,
				   u64 cookie)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = &ab->dp;
	struct sk_buff *skb;
	struct htt_ext_stats_cfg_cmd *cmd;
	int len = sizeof(*cmd);
	int ret;

	skb = ath12k_htc_alloc_skb(ab, len);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, len);

	cmd = (struct htt_ext_stats_cfg_cmd *)skb->data;
	memset(cmd, 0, sizeof(*cmd));
	cmd->hdr.msg_type = HTT_H2T_MSG_TYPE_EXT_STATS_CFG;

	cmd->hdr.pdev_mask = 1 << ar->pdev->pdev_id;

	cmd->hdr.stats_type = type;
	cmd->cfg_param0 = cpu_to_le32(cfg_params->cfg0);
	cmd->cfg_param1 = cpu_to_le32(cfg_params->cfg1);
	cmd->cfg_param2 = cpu_to_le32(cfg_params->cfg2);
	cmd->cfg_param3 = cpu_to_le32(cfg_params->cfg3);
	cmd->cookie_lsb = cpu_to_le32(lower_32_bits(cookie));
	cmd->cookie_msb = cpu_to_le32(upper_32_bits(cookie));

	ret = ath12k_htc_send(&ab->htc, dp->eid, skb);
	if (ret) {
		ath12k_warn(ab, "failed to send htt type stats request: %d",
			    ret);
		dev_kfree_skb_any(skb);
		return ret;
	}

	return 0;
}

int ath12k_dp_tx_htt_monitor_mode_ring_config(struct ath12k *ar, bool reset)
{
	struct ath12k_base *ab = ar->ab;
	int ret;

	ret = ath12k_dp_tx_htt_tx_monitor_mode_ring_config(ar, reset);
	if (ret) {
		ath12k_err(ab, "failed to setup tx monitor filter %d\n", ret);
		return ret;
	}

	ret = ath12k_dp_tx_htt_tx_monitor_mode_ring_config(ar, reset);
	if (ret) {
		ath12k_err(ab, "failed to setup rx monitor filter %d\n", ret);
		return ret;
	}

	return 0;
}

int ath12k_dp_tx_htt_rx_monitor_mode_ring_config(struct ath12k *ar, bool reset)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = &ab->dp;
	struct htt_rx_ring_tlv_filter tlv_filter = {0};
	int ret, ring_id;

	ring_id = dp->rxdma_mon_buf_ring.refill_buf_ring.ring_id;
	tlv_filter.offset_valid = false;

	if (!reset) {
		tlv_filter.rx_filter = HTT_RX_MON_FILTER_TLV_FLAGS_MON_BUF_RING;
		tlv_filter.pkt_filter_flags0 =
					HTT_RX_MON_FP_MGMT_FILTER_FLAGS0 |
					HTT_RX_MON_MO_MGMT_FILTER_FLAGS0;
		tlv_filter.pkt_filter_flags1 =
					HTT_RX_MON_FP_MGMT_FILTER_FLAGS1 |
					HTT_RX_MON_MO_MGMT_FILTER_FLAGS1;
		tlv_filter.pkt_filter_flags2 =
					HTT_RX_MON_FP_CTRL_FILTER_FLASG2 |
					HTT_RX_MON_MO_CTRL_FILTER_FLASG2;
		tlv_filter.pkt_filter_flags3 =
					HTT_RX_MON_FP_CTRL_FILTER_FLASG3 |
					HTT_RX_MON_MO_CTRL_FILTER_FLASG3 |
					HTT_RX_MON_FP_DATA_FILTER_FLASG3 |
					HTT_RX_MON_MO_DATA_FILTER_FLASG3;
	}

	if (ab->hw_params->rxdma1_enable) {
		ret = ath12k_dp_tx_htt_rx_filter_setup(ar->ab, ring_id, 0,
						       HAL_RXDMA_MONITOR_BUF,
						       DP_RXDMA_REFILL_RING_SIZE,
						       &tlv_filter);
		if (ret) {
			ath12k_err(ab,
				   "failed to setup filter for monitor buf %d\n", ret);
			return ret;
		}
	}

	return 0;
}

int ath12k_dp_tx_htt_tx_filter_setup(struct ath12k_base *ab, u32 ring_id,
				     int mac_id, enum hal_ring_type ring_type,
				     int tx_buf_size,
				     struct htt_tx_ring_tlv_filter *htt_tlv_filter)
{
	struct htt_tx_ring_selection_cfg_cmd *cmd;
	struct hal_srng *srng = &ab->hal.srng_list[ring_id];
	struct hal_srng_params params;
	struct sk_buff *skb;
	int len = sizeof(*cmd);
	enum htt_srng_ring_type htt_ring_type;
	enum htt_srng_ring_id htt_ring_id;
	int ret;

	skb = ath12k_htc_alloc_skb(ab, len);
	if (!skb)
		return -ENOMEM;

	memset(&params, 0, sizeof(params));
	ath12k_hal_srng_get_params(ab, srng, &params);

	ret = ath12k_dp_tx_get_ring_id_type(ab, mac_id, ring_id,
					    ring_type, &htt_ring_type,
					    &htt_ring_id);

	if (ret)
		goto err_free;

	skb_put(skb, len);
	cmd = (struct htt_tx_ring_selection_cfg_cmd *)skb->data;
	cmd->info0 = le32_encode_bits(HTT_H2T_MSG_TYPE_TX_MONITOR_CFG,
				      HTT_TX_RING_SELECTION_CFG_CMD_INFO0_MSG_TYPE);
	if (htt_ring_type == HTT_SW_TO_HW_RING ||
	    htt_ring_type == HTT_HW_TO_SW_RING)
		cmd->info0 |=
			le32_encode_bits(DP_SW2HW_MACID(mac_id),
					 HTT_TX_RING_SELECTION_CFG_CMD_INFO0_PDEV_ID);
	else
		cmd->info0 |=
			le32_encode_bits(mac_id,
					 HTT_TX_RING_SELECTION_CFG_CMD_INFO0_PDEV_ID);
	cmd->info0 |= le32_encode_bits(htt_ring_id,
				       HTT_TX_RING_SELECTION_CFG_CMD_INFO0_RING_ID);
	cmd->info0 |= le32_encode_bits(!!(params.flags & HAL_SRNG_FLAGS_MSI_SWAP),
				       HTT_TX_RING_SELECTION_CFG_CMD_INFO0_SS);
	cmd->info0 |= le32_encode_bits(!!(params.flags & HAL_SRNG_FLAGS_DATA_TLV_SWAP),
				       HTT_TX_RING_SELECTION_CFG_CMD_INFO0_PS);

	cmd->info1 |=
		le32_encode_bits(tx_buf_size,
				 HTT_TX_RING_SELECTION_CFG_CMD_INFO1_RING_BUFF_SIZE);

	if (htt_tlv_filter->tx_mon_mgmt_filter) {
		cmd->info1 |=
			le32_encode_bits(HTT_STATS_FRAME_CTRL_TYPE_MGMT,
					 HTT_TX_RING_SELECTION_CFG_CMD_INFO1_PKT_TYPE);
		cmd->info1 |=
		le32_encode_bits(htt_tlv_filter->tx_mon_pkt_dma_len,
				 HTT_TX_RING_SELECTION_CFG_CMD_INFO1_CONF_LEN_MGMT);
		cmd->info2 |=
		le32_encode_bits(HTT_STATS_FRAME_CTRL_TYPE_MGMT,
				 HTT_TX_RING_SELECTION_CFG_CMD_INFO2_PKT_TYPE_EN_FLAG);
	}

	if (htt_tlv_filter->tx_mon_data_filter) {
		cmd->info1 |=
			le32_encode_bits(HTT_STATS_FRAME_CTRL_TYPE_CTRL,
					 HTT_TX_RING_SELECTION_CFG_CMD_INFO1_PKT_TYPE);
		cmd->info1 |=
		le32_encode_bits(htt_tlv_filter->tx_mon_pkt_dma_len,
				 HTT_TX_RING_SELECTION_CFG_CMD_INFO1_CONF_LEN_CTRL);
		cmd->info2 |=
		le32_encode_bits(HTT_STATS_FRAME_CTRL_TYPE_CTRL,
				 HTT_TX_RING_SELECTION_CFG_CMD_INFO2_PKT_TYPE_EN_FLAG);
	}

	if (htt_tlv_filter->tx_mon_ctrl_filter) {
		cmd->info1 |=
			le32_encode_bits(HTT_STATS_FRAME_CTRL_TYPE_DATA,
					 HTT_TX_RING_SELECTION_CFG_CMD_INFO1_PKT_TYPE);
		cmd->info1 |=
		le32_encode_bits(htt_tlv_filter->tx_mon_pkt_dma_len,
				 HTT_TX_RING_SELECTION_CFG_CMD_INFO1_CONF_LEN_DATA);
		cmd->info2 |=
		le32_encode_bits(HTT_STATS_FRAME_CTRL_TYPE_DATA,
				 HTT_TX_RING_SELECTION_CFG_CMD_INFO2_PKT_TYPE_EN_FLAG);
	}

	cmd->tlv_filter_mask_in0 =
		cpu_to_le32(htt_tlv_filter->tx_mon_downstream_tlv_flags);
	cmd->tlv_filter_mask_in1 =
		cpu_to_le32(htt_tlv_filter->tx_mon_upstream_tlv_flags0);
	cmd->tlv_filter_mask_in2 =
		cpu_to_le32(htt_tlv_filter->tx_mon_upstream_tlv_flags1);
	cmd->tlv_filter_mask_in3 =
		cpu_to_le32(htt_tlv_filter->tx_mon_upstream_tlv_flags2);

	ret = ath12k_htc_send(&ab->htc, ab->dp.eid, skb);
	if (ret)
		goto err_free;

	return 0;

err_free:
	dev_kfree_skb_any(skb);
	return ret;
}

int ath12k_dp_tx_htt_tx_monitor_mode_ring_config(struct ath12k *ar, bool reset)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = &ab->dp;
	struct htt_tx_ring_tlv_filter tlv_filter = {0};
	int ret, ring_id;

	ring_id = dp->tx_mon_buf_ring.refill_buf_ring.ring_id;

	/* TODO: Need to set upstream/downstream tlv filters
	 * here
	 */

	if (ab->hw_params->rxdma1_enable) {
		ret = ath12k_dp_tx_htt_tx_filter_setup(ar->ab, ring_id, 0,
						       HAL_TX_MONITOR_BUF,
						       DP_RXDMA_REFILL_RING_SIZE,
						       &tlv_filter);
		if (ret) {
			ath12k_err(ab,
				   "failed to setup filter for monitor buf %d\n", ret);
			return ret;
		}
	}

	return 0;
}
