/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/etherdevice.h>
#include "htt.h"
#include "mac.h"
#include "hif.h"
#include "txrx.h"
#include "debug.h"

void __ath10k_htt_tx_dec_pending(struct ath10k_htt *htt)
{
	htt->num_pending_tx--;
	if (htt->num_pending_tx == htt->max_num_pending_tx - 1)
		ieee80211_wake_queues(htt->ar->hw);
}

static void ath10k_htt_tx_dec_pending(struct ath10k_htt *htt)
{
	spin_lock_bh(&htt->tx_lock);
	__ath10k_htt_tx_dec_pending(htt);
	spin_unlock_bh(&htt->tx_lock);
}

static int ath10k_htt_tx_inc_pending(struct ath10k_htt *htt)
{
	int ret = 0;

	spin_lock_bh(&htt->tx_lock);

	if (htt->num_pending_tx >= htt->max_num_pending_tx) {
		ret = -EBUSY;
		goto exit;
	}

	htt->num_pending_tx++;
	if (htt->num_pending_tx == htt->max_num_pending_tx)
		ieee80211_stop_queues(htt->ar->hw);

exit:
	spin_unlock_bh(&htt->tx_lock);
	return ret;
}

int ath10k_htt_tx_alloc_msdu_id(struct ath10k_htt *htt)
{
	int msdu_id;

	lockdep_assert_held(&htt->tx_lock);

	msdu_id = find_first_zero_bit(htt->used_msdu_ids,
				      htt->max_num_pending_tx);
	if (msdu_id == htt->max_num_pending_tx)
		return -ENOBUFS;

	ath10k_dbg(ATH10K_DBG_HTT, "htt tx alloc msdu_id %d\n", msdu_id);
	__set_bit(msdu_id, htt->used_msdu_ids);
	return msdu_id;
}

void ath10k_htt_tx_free_msdu_id(struct ath10k_htt *htt, u16 msdu_id)
{
	lockdep_assert_held(&htt->tx_lock);

	if (!test_bit(msdu_id, htt->used_msdu_ids))
		ath10k_warn("trying to free unallocated msdu_id %d\n", msdu_id);

	ath10k_dbg(ATH10K_DBG_HTT, "htt tx free msdu_id %hu\n", msdu_id);
	__clear_bit(msdu_id, htt->used_msdu_ids);
}

int ath10k_htt_tx_attach(struct ath10k_htt *htt)
{
	u8 pipe;

	spin_lock_init(&htt->tx_lock);
	init_waitqueue_head(&htt->empty_tx_wq);

	/* At the beginning free queue number should hint us the maximum
	 * queue length */
	pipe = htt->ar->htc.endpoint[htt->eid].ul_pipe_id;
	htt->max_num_pending_tx = ath10k_hif_get_free_queue_number(htt->ar,
								   pipe);

	ath10k_dbg(ATH10K_DBG_BOOT, "htt tx max num pending tx %d\n",
		   htt->max_num_pending_tx);

	htt->pending_tx = kzalloc(sizeof(*htt->pending_tx) *
				  htt->max_num_pending_tx, GFP_KERNEL);
	if (!htt->pending_tx)
		return -ENOMEM;

	htt->used_msdu_ids = kzalloc(sizeof(unsigned long) *
				     BITS_TO_LONGS(htt->max_num_pending_tx),
				     GFP_KERNEL);
	if (!htt->used_msdu_ids) {
		kfree(htt->pending_tx);
		return -ENOMEM;
	}

	return 0;
}

static void ath10k_htt_tx_cleanup_pending(struct ath10k_htt *htt)
{
	struct sk_buff *txdesc;
	int msdu_id;

	/* No locks needed. Called after communication with the device has
	 * been stopped. */

	for (msdu_id = 0; msdu_id < htt->max_num_pending_tx; msdu_id++) {
		if (!test_bit(msdu_id, htt->used_msdu_ids))
			continue;

		txdesc = htt->pending_tx[msdu_id];
		if (!txdesc)
			continue;

		ath10k_dbg(ATH10K_DBG_HTT, "force cleanup msdu_id %hu\n",
			   msdu_id);

		if (ATH10K_SKB_CB(txdesc)->htt.refcount > 0)
			ATH10K_SKB_CB(txdesc)->htt.refcount = 1;

		ATH10K_SKB_CB(txdesc)->htt.discard = true;
		ath10k_txrx_tx_unref(htt, txdesc);
	}
}

void ath10k_htt_tx_detach(struct ath10k_htt *htt)
{
	ath10k_htt_tx_cleanup_pending(htt);
	kfree(htt->pending_tx);
	kfree(htt->used_msdu_ids);
	return;
}

void ath10k_htt_htc_tx_complete(struct ath10k *ar, struct sk_buff *skb)
{
	struct ath10k_skb_cb *skb_cb = ATH10K_SKB_CB(skb);
	struct ath10k_htt *htt = &ar->htt;

	if (skb_cb->htt.is_conf) {
		dev_kfree_skb_any(skb);
		return;
	}

	if (skb_cb->is_aborted) {
		skb_cb->htt.discard = true;

		/* if the skbuff is aborted we need to make sure we'll free up
		 * the tx resources, we can't simply run tx_unref() 2 times
		 * because if htt tx completion came in earlier we'd access
		 * unallocated memory */
		if (skb_cb->htt.refcount > 1)
			skb_cb->htt.refcount = 1;
	}

	ath10k_txrx_tx_unref(htt, skb);
}

int ath10k_htt_h2t_ver_req_msg(struct ath10k_htt *htt)
{
	struct sk_buff *skb;
	struct htt_cmd *cmd;
	int len = 0;
	int ret;

	len += sizeof(cmd->hdr);
	len += sizeof(cmd->ver_req);

	skb = ath10k_htc_alloc_skb(len);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, len);
	cmd = (struct htt_cmd *)skb->data;
	cmd->hdr.msg_type = HTT_H2T_MSG_TYPE_VERSION_REQ;

	ATH10K_SKB_CB(skb)->htt.is_conf = true;

	ret = ath10k_htc_send(&htt->ar->htc, htt->eid, skb);
	if (ret) {
		dev_kfree_skb_any(skb);
		return ret;
	}

	return 0;
}

int ath10k_htt_h2t_stats_req(struct ath10k_htt *htt, u8 mask, u64 cookie)
{
	struct htt_stats_req *req;
	struct sk_buff *skb;
	struct htt_cmd *cmd;
	int len = 0, ret;

	len += sizeof(cmd->hdr);
	len += sizeof(cmd->stats_req);

	skb = ath10k_htc_alloc_skb(len);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, len);
	cmd = (struct htt_cmd *)skb->data;
	cmd->hdr.msg_type = HTT_H2T_MSG_TYPE_STATS_REQ;

	req = &cmd->stats_req;

	memset(req, 0, sizeof(*req));

	/* currently we support only max 8 bit masks so no need to worry
	 * about endian support */
	req->upload_types[0] = mask;
	req->reset_types[0] = mask;
	req->stat_type = HTT_STATS_REQ_CFG_STAT_TYPE_INVALID;
	req->cookie_lsb = cpu_to_le32(cookie & 0xffffffff);
	req->cookie_msb = cpu_to_le32((cookie & 0xffffffff00000000ULL) >> 32);

	ATH10K_SKB_CB(skb)->htt.is_conf = true;

	ret = ath10k_htc_send(&htt->ar->htc, htt->eid, skb);
	if (ret) {
		ath10k_warn("failed to send htt type stats request: %d", ret);
		dev_kfree_skb_any(skb);
		return ret;
	}

	return 0;
}

int ath10k_htt_send_rx_ring_cfg_ll(struct ath10k_htt *htt)
{
	struct sk_buff *skb;
	struct htt_cmd *cmd;
	struct htt_rx_ring_setup_ring *ring;
	const int num_rx_ring = 1;
	u16 flags;
	u32 fw_idx;
	int len;
	int ret;

	/*
	 * the HW expects the buffer to be an integral number of 4-byte
	 * "words"
	 */
	BUILD_BUG_ON(!IS_ALIGNED(HTT_RX_BUF_SIZE, 4));
	BUILD_BUG_ON((HTT_RX_BUF_SIZE & HTT_MAX_CACHE_LINE_SIZE_MASK) != 0);

	len = sizeof(cmd->hdr) + sizeof(cmd->rx_setup.hdr)
	    + (sizeof(*ring) * num_rx_ring);
	skb = ath10k_htc_alloc_skb(len);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, len);

	cmd = (struct htt_cmd *)skb->data;
	ring = &cmd->rx_setup.rings[0];

	cmd->hdr.msg_type = HTT_H2T_MSG_TYPE_RX_RING_CFG;
	cmd->rx_setup.hdr.num_rings = 1;

	/* FIXME: do we need all of this? */
	flags = 0;
	flags |= HTT_RX_RING_FLAGS_MAC80211_HDR;
	flags |= HTT_RX_RING_FLAGS_MSDU_PAYLOAD;
	flags |= HTT_RX_RING_FLAGS_PPDU_START;
	flags |= HTT_RX_RING_FLAGS_PPDU_END;
	flags |= HTT_RX_RING_FLAGS_MPDU_START;
	flags |= HTT_RX_RING_FLAGS_MPDU_END;
	flags |= HTT_RX_RING_FLAGS_MSDU_START;
	flags |= HTT_RX_RING_FLAGS_MSDU_END;
	flags |= HTT_RX_RING_FLAGS_RX_ATTENTION;
	flags |= HTT_RX_RING_FLAGS_FRAG_INFO;
	flags |= HTT_RX_RING_FLAGS_UNICAST_RX;
	flags |= HTT_RX_RING_FLAGS_MULTICAST_RX;
	flags |= HTT_RX_RING_FLAGS_CTRL_RX;
	flags |= HTT_RX_RING_FLAGS_MGMT_RX;
	flags |= HTT_RX_RING_FLAGS_NULL_RX;
	flags |= HTT_RX_RING_FLAGS_PHY_DATA_RX;

	fw_idx = __le32_to_cpu(*htt->rx_ring.alloc_idx.vaddr);

	ring->fw_idx_shadow_reg_paddr =
		__cpu_to_le32(htt->rx_ring.alloc_idx.paddr);
	ring->rx_ring_base_paddr = __cpu_to_le32(htt->rx_ring.base_paddr);
	ring->rx_ring_len = __cpu_to_le16(htt->rx_ring.size);
	ring->rx_ring_bufsize = __cpu_to_le16(HTT_RX_BUF_SIZE);
	ring->flags = __cpu_to_le16(flags);
	ring->fw_idx_init_val = __cpu_to_le16(fw_idx);

#define desc_offset(x) (offsetof(struct htt_rx_desc, x) / 4)

	ring->mac80211_hdr_offset = __cpu_to_le16(desc_offset(rx_hdr_status));
	ring->msdu_payload_offset = __cpu_to_le16(desc_offset(msdu_payload));
	ring->ppdu_start_offset = __cpu_to_le16(desc_offset(ppdu_start));
	ring->ppdu_end_offset = __cpu_to_le16(desc_offset(ppdu_end));
	ring->mpdu_start_offset = __cpu_to_le16(desc_offset(mpdu_start));
	ring->mpdu_end_offset = __cpu_to_le16(desc_offset(mpdu_end));
	ring->msdu_start_offset = __cpu_to_le16(desc_offset(msdu_start));
	ring->msdu_end_offset = __cpu_to_le16(desc_offset(msdu_end));
	ring->rx_attention_offset = __cpu_to_le16(desc_offset(attention));
	ring->frag_info_offset = __cpu_to_le16(desc_offset(frag_info));

#undef desc_offset

	ATH10K_SKB_CB(skb)->htt.is_conf = true;

	ret = ath10k_htc_send(&htt->ar->htc, htt->eid, skb);
	if (ret) {
		dev_kfree_skb_any(skb);
		return ret;
	}

	return 0;
}

int ath10k_htt_mgmt_tx(struct ath10k_htt *htt, struct sk_buff *msdu)
{
	struct device *dev = htt->ar->dev;
	struct ath10k_skb_cb *skb_cb;
	struct sk_buff *txdesc = NULL;
	struct htt_cmd *cmd;
	u8 vdev_id = ATH10K_SKB_CB(msdu)->htt.vdev_id;
	int len = 0;
	int msdu_id = -1;
	int res;


	res = ath10k_htt_tx_inc_pending(htt);
	if (res)
		return res;

	len += sizeof(cmd->hdr);
	len += sizeof(cmd->mgmt_tx);

	txdesc = ath10k_htc_alloc_skb(len);
	if (!txdesc) {
		res = -ENOMEM;
		goto err;
	}

	spin_lock_bh(&htt->tx_lock);
	msdu_id = ath10k_htt_tx_alloc_msdu_id(htt);
	if (msdu_id < 0) {
		spin_unlock_bh(&htt->tx_lock);
		res = msdu_id;
		goto err;
	}
	htt->pending_tx[msdu_id] = txdesc;
	spin_unlock_bh(&htt->tx_lock);

	res = ath10k_skb_map(dev, msdu);
	if (res)
		goto err;

	skb_put(txdesc, len);
	cmd = (struct htt_cmd *)txdesc->data;
	cmd->hdr.msg_type         = HTT_H2T_MSG_TYPE_MGMT_TX;
	cmd->mgmt_tx.msdu_paddr = __cpu_to_le32(ATH10K_SKB_CB(msdu)->paddr);
	cmd->mgmt_tx.len        = __cpu_to_le32(msdu->len);
	cmd->mgmt_tx.desc_id    = __cpu_to_le32(msdu_id);
	cmd->mgmt_tx.vdev_id    = __cpu_to_le32(vdev_id);
	memcpy(cmd->mgmt_tx.hdr, msdu->data,
	       min_t(int, msdu->len, HTT_MGMT_FRM_HDR_DOWNLOAD_LEN));

	/* refcount is decremented by HTC and HTT completions until it reaches
	 * zero and is freed */
	skb_cb = ATH10K_SKB_CB(txdesc);
	skb_cb->htt.msdu_id = msdu_id;
	skb_cb->htt.refcount = 2;
	skb_cb->htt.msdu = msdu;

	res = ath10k_htc_send(&htt->ar->htc, htt->eid, txdesc);
	if (res)
		goto err;

	return 0;

err:
	ath10k_skb_unmap(dev, msdu);

	if (txdesc)
		dev_kfree_skb_any(txdesc);
	if (msdu_id >= 0) {
		spin_lock_bh(&htt->tx_lock);
		htt->pending_tx[msdu_id] = NULL;
		ath10k_htt_tx_free_msdu_id(htt, msdu_id);
		spin_unlock_bh(&htt->tx_lock);
	}
	ath10k_htt_tx_dec_pending(htt);
	return res;
}

int ath10k_htt_tx(struct ath10k_htt *htt, struct sk_buff *msdu)
{
	struct device *dev = htt->ar->dev;
	struct htt_cmd *cmd;
	struct htt_data_tx_desc_frag *tx_frags;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)msdu->data;
	struct ath10k_skb_cb *skb_cb;
	struct sk_buff *txdesc = NULL;
	struct sk_buff *txfrag = NULL;
	u8 vdev_id = ATH10K_SKB_CB(msdu)->htt.vdev_id;
	u8 tid;
	int prefetch_len, desc_len, frag_len;
	dma_addr_t frags_paddr;
	int msdu_id = -1;
	int res;
	u8 flags0;
	u16 flags1;

	res = ath10k_htt_tx_inc_pending(htt);
	if (res)
		return res;

	prefetch_len = min(htt->prefetch_len, msdu->len);
	prefetch_len = roundup(prefetch_len, 4);

	desc_len = sizeof(cmd->hdr) + sizeof(cmd->data_tx) + prefetch_len;
	frag_len = sizeof(*tx_frags) * 2;

	txdesc = ath10k_htc_alloc_skb(desc_len);
	if (!txdesc) {
		res = -ENOMEM;
		goto err;
	}

	/* Since HTT 3.0 there is no separate mgmt tx command. However in case
	 * of mgmt tx using TX_FRM there is not tx fragment list. Instead of tx
	 * fragment list host driver specifies directly frame pointer. */
	if (htt->target_version_major < 3 ||
	    !ieee80211_is_mgmt(hdr->frame_control)) {
		txfrag = dev_alloc_skb(frag_len);
		if (!txfrag) {
			res = -ENOMEM;
			goto err;
		}
	}

	if (!IS_ALIGNED((unsigned long)txdesc->data, 4)) {
		ath10k_warn("htt alignment check failed. dropping packet.\n");
		res = -EIO;
		goto err;
	}

	spin_lock_bh(&htt->tx_lock);
	msdu_id = ath10k_htt_tx_alloc_msdu_id(htt);
	if (msdu_id < 0) {
		spin_unlock_bh(&htt->tx_lock);
		res = msdu_id;
		goto err;
	}
	htt->pending_tx[msdu_id] = txdesc;
	spin_unlock_bh(&htt->tx_lock);

	res = ath10k_skb_map(dev, msdu);
	if (res)
		goto err;

	/* Since HTT 3.0 there is no separate mgmt tx command. However in case
	 * of mgmt tx using TX_FRM there is not tx fragment list. Instead of tx
	 * fragment list host driver specifies directly frame pointer. */
	if (htt->target_version_major < 3 ||
	    !ieee80211_is_mgmt(hdr->frame_control)) {
		/* tx fragment list must be terminated with zero-entry */
		skb_put(txfrag, frag_len);
		tx_frags = (struct htt_data_tx_desc_frag *)txfrag->data;
		tx_frags[0].paddr = __cpu_to_le32(ATH10K_SKB_CB(msdu)->paddr);
		tx_frags[0].len   = __cpu_to_le32(msdu->len);
		tx_frags[1].paddr = __cpu_to_le32(0);
		tx_frags[1].len   = __cpu_to_le32(0);

		res = ath10k_skb_map(dev, txfrag);
		if (res)
			goto err;

		ath10k_dbg(ATH10K_DBG_HTT, "txfrag 0x%llx\n",
			   (unsigned long long) ATH10K_SKB_CB(txfrag)->paddr);
		ath10k_dbg_dump(ATH10K_DBG_HTT_DUMP, NULL, "txfrag: ",
				txfrag->data, frag_len);
	}

	ath10k_dbg(ATH10K_DBG_HTT, "msdu 0x%llx\n",
		   (unsigned long long) ATH10K_SKB_CB(msdu)->paddr);
	ath10k_dbg_dump(ATH10K_DBG_HTT_DUMP, NULL, "msdu: ",
			msdu->data, msdu->len);

	skb_put(txdesc, desc_len);
	cmd = (struct htt_cmd *)txdesc->data;
	memset(cmd, 0, desc_len);

	tid = ATH10K_SKB_CB(msdu)->htt.tid;

	ath10k_dbg(ATH10K_DBG_HTT, "htt data tx using tid %hhu\n", tid);

	flags0  = 0;
	if (!ieee80211_has_protected(hdr->frame_control))
		flags0 |= HTT_DATA_TX_DESC_FLAGS0_NO_ENCRYPT;
	flags0 |= HTT_DATA_TX_DESC_FLAGS0_MAC_HDR_PRESENT;

	/* Since HTT 3.0 there is no separate mgmt tx command. However in case
	 * of mgmt tx using TX_FRM there is not tx fragment list. Instead of tx
	 * fragment list host driver specifies directly frame pointer. */
	if (htt->target_version_major >= 3 &&
	    ieee80211_is_mgmt(hdr->frame_control))
		flags0 |= SM(ATH10K_HW_TXRX_MGMT,
			     HTT_DATA_TX_DESC_FLAGS0_PKT_TYPE);
	else
		flags0 |= SM(ATH10K_HW_TXRX_NATIVE_WIFI,
			     HTT_DATA_TX_DESC_FLAGS0_PKT_TYPE);

	flags1  = 0;
	flags1 |= SM((u16)vdev_id, HTT_DATA_TX_DESC_FLAGS1_VDEV_ID);
	flags1 |= SM((u16)tid, HTT_DATA_TX_DESC_FLAGS1_EXT_TID);
	flags1 |= HTT_DATA_TX_DESC_FLAGS1_CKSUM_L3_OFFLOAD;
	flags1 |= HTT_DATA_TX_DESC_FLAGS1_CKSUM_L4_OFFLOAD;

	/* Since HTT 3.0 there is no separate mgmt tx command. However in case
	 * of mgmt tx using TX_FRM there is not tx fragment list. Instead of tx
	 * fragment list host driver specifies directly frame pointer. */
	if (htt->target_version_major >= 3 &&
	    ieee80211_is_mgmt(hdr->frame_control))
		frags_paddr = ATH10K_SKB_CB(msdu)->paddr;
	else
		frags_paddr = ATH10K_SKB_CB(txfrag)->paddr;

	cmd->hdr.msg_type        = HTT_H2T_MSG_TYPE_TX_FRM;
	cmd->data_tx.flags0      = flags0;
	cmd->data_tx.flags1      = __cpu_to_le16(flags1);
	cmd->data_tx.len         = __cpu_to_le16(msdu->len);
	cmd->data_tx.id          = __cpu_to_le16(msdu_id);
	cmd->data_tx.frags_paddr = __cpu_to_le32(frags_paddr);
	cmd->data_tx.peerid      = __cpu_to_le32(HTT_INVALID_PEERID);

	memcpy(cmd->data_tx.prefetch, msdu->data, prefetch_len);

	/* refcount is decremented by HTC and HTT completions until it reaches
	 * zero and is freed */
	skb_cb = ATH10K_SKB_CB(txdesc);
	skb_cb->htt.msdu_id = msdu_id;
	skb_cb->htt.refcount = 2;
	skb_cb->htt.txfrag = txfrag;
	skb_cb->htt.msdu = msdu;

	res = ath10k_htc_send(&htt->ar->htc, htt->eid, txdesc);
	if (res)
		goto err;

	return 0;
err:
	if (txfrag)
		ath10k_skb_unmap(dev, txfrag);
	if (txdesc)
		dev_kfree_skb_any(txdesc);
	if (txfrag)
		dev_kfree_skb_any(txfrag);
	if (msdu_id >= 0) {
		spin_lock_bh(&htt->tx_lock);
		htt->pending_tx[msdu_id] = NULL;
		ath10k_htt_tx_free_msdu_id(htt, msdu_id);
		spin_unlock_bh(&htt->tx_lock);
	}
	ath10k_htt_tx_dec_pending(htt);
	ath10k_skb_unmap(dev, msdu);
	return res;
}
