/*
 * Copyright (c) 2014-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
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

#include "wil6210.h"
#include "txrx.h"

#define SEQ_MODULO 0x1000
#define SEQ_MASK   0xfff

static inline int seq_less(u16 sq1, u16 sq2)
{
	return ((sq1 - sq2) & SEQ_MASK) > (SEQ_MODULO >> 1);
}

static inline u16 seq_inc(u16 sq)
{
	return (sq + 1) & SEQ_MASK;
}

static inline u16 seq_sub(u16 sq1, u16 sq2)
{
	return (sq1 - sq2) & SEQ_MASK;
}

static inline int reorder_index(struct wil_tid_ampdu_rx *r, u16 seq)
{
	return seq_sub(seq, r->ssn) % r->buf_size;
}

static void wil_release_reorder_frame(struct net_device *ndev,
				      struct wil_tid_ampdu_rx *r,
				      int index)
{
	struct sk_buff *skb = r->reorder_buf[index];

	if (!skb)
		goto no_frame;

	/* release the frame from the reorder ring buffer */
	r->stored_mpdu_num--;
	r->reorder_buf[index] = NULL;
	wil_netif_rx_any(skb, ndev);

no_frame:
	r->head_seq_num = seq_inc(r->head_seq_num);
}

static void wil_release_reorder_frames(struct net_device *ndev,
				       struct wil_tid_ampdu_rx *r,
				       u16 hseq)
{
	int index;

	/* note: this function is never called with
	 * hseq preceding r->head_seq_num, i.e it is always true
	 * !seq_less(hseq, r->head_seq_num)
	 * and thus on loop exit it should be
	 * r->head_seq_num == hseq
	 */
	while (seq_less(r->head_seq_num, hseq) && r->stored_mpdu_num) {
		index = reorder_index(r, r->head_seq_num);
		wil_release_reorder_frame(ndev, r, index);
	}
	r->head_seq_num = hseq;
}

static void wil_reorder_release(struct net_device *ndev,
				struct wil_tid_ampdu_rx *r)
{
	int index = reorder_index(r, r->head_seq_num);

	while (r->reorder_buf[index]) {
		wil_release_reorder_frame(ndev, r, index);
		index = reorder_index(r, r->head_seq_num);
	}
}

/* called in NAPI context */
void wil_rx_reorder(struct wil6210_priv *wil, struct sk_buff *skb)
__acquires(&sta->tid_rx_lock) __releases(&sta->tid_rx_lock)
{
	struct wil6210_vif *vif;
	struct net_device *ndev;
	int tid, cid, mid, mcast, retry;
	u16 seq;
	struct wil_sta_info *sta;
	struct wil_tid_ampdu_rx *r;
	u16 hseq;
	int index;

	wil->txrx_ops.get_reorder_params(wil, skb, &tid, &cid, &mid, &seq,
					 &mcast, &retry);
	sta = &wil->sta[cid];

	wil_dbg_txrx(wil, "MID %d CID %d TID %d Seq 0x%03x mcast %01x\n",
		     mid, cid, tid, seq, mcast);

	vif = wil->vifs[mid];
	if (unlikely(!vif)) {
		wil_dbg_txrx(wil, "invalid VIF, mid %d\n", mid);
		dev_kfree_skb(skb);
		return;
	}
	ndev = vif_to_ndev(vif);

	spin_lock(&sta->tid_rx_lock);

	r = sta->tid_rx[tid];
	if (!r) {
		wil_netif_rx_any(skb, ndev);
		goto out;
	}

	if (unlikely(mcast)) {
		if (retry && seq == r->mcast_last_seq) {
			r->drop_dup_mcast++;
			wil_dbg_txrx(wil, "Rx drop: dup mcast seq 0x%03x\n",
				     seq);
			dev_kfree_skb(skb);
			goto out;
		}
		r->mcast_last_seq = seq;
		wil_netif_rx_any(skb, ndev);
		goto out;
	}

	r->total++;
	hseq = r->head_seq_num;

	/** Due to the race between WMI events, where BACK establishment
	 * reported, and data Rx, few packets may be pass up before reorder
	 * buffer get allocated. Catch up by pretending SSN is what we
	 * see in the 1-st Rx packet
	 *
	 * Another scenario, Rx get delayed and we got packet from before
	 * BACK. Pass it to the stack and wait.
	 */
	if (r->first_time) {
		r->first_time = false;
		if (seq != r->head_seq_num) {
			if (seq_less(seq, r->head_seq_num)) {
				wil_err(wil,
					"Error: frame with early sequence 0x%03x, should be 0x%03x. Waiting...\n",
					seq, r->head_seq_num);
				r->first_time = true;
				wil_netif_rx_any(skb, ndev);
				goto out;
			}
			wil_err(wil,
				"Error: 1-st frame with wrong sequence 0x%03x, should be 0x%03x. Fixing...\n",
				seq, r->head_seq_num);
			r->head_seq_num = seq;
			r->ssn = seq;
		}
	}

	/* frame with out of date sequence number */
	if (seq_less(seq, r->head_seq_num)) {
		r->ssn_last_drop = seq;
		r->drop_old++;
		wil_dbg_txrx(wil, "Rx drop: old seq 0x%03x head 0x%03x\n",
			     seq, r->head_seq_num);
		dev_kfree_skb(skb);
		goto out;
	}

	/*
	 * If frame the sequence number exceeds our buffering window
	 * size release some previous frames to make room for this one.
	 */
	if (!seq_less(seq, r->head_seq_num + r->buf_size)) {
		hseq = seq_inc(seq_sub(seq, r->buf_size));
		/* release stored frames up to new head to stack */
		wil_release_reorder_frames(ndev, r, hseq);
	}

	/* Now the new frame is always in the range of the reordering buffer */

	index = reorder_index(r, seq);

	/* check if we already stored this frame */
	if (r->reorder_buf[index]) {
		r->drop_dup++;
		wil_dbg_txrx(wil, "Rx drop: dup seq 0x%03x\n", seq);
		dev_kfree_skb(skb);
		goto out;
	}

	/*
	 * If the current MPDU is in the right order and nothing else
	 * is stored we can process it directly, no need to buffer it.
	 * If it is first but there's something stored, we may be able
	 * to release frames after this one.
	 */
	if (seq == r->head_seq_num && r->stored_mpdu_num == 0) {
		r->head_seq_num = seq_inc(r->head_seq_num);
		wil_netif_rx_any(skb, ndev);
		goto out;
	}

	/* put the frame in the reordering buffer */
	r->reorder_buf[index] = skb;
	r->stored_mpdu_num++;
	wil_reorder_release(ndev, r);

out:
	spin_unlock(&sta->tid_rx_lock);
}

/* process BAR frame, called in NAPI context */
void wil_rx_bar(struct wil6210_priv *wil, struct wil6210_vif *vif,
		u8 cid, u8 tid, u16 seq)
{
	struct wil_sta_info *sta = &wil->sta[cid];
	struct net_device *ndev = vif_to_ndev(vif);
	struct wil_tid_ampdu_rx *r;

	spin_lock(&sta->tid_rx_lock);

	r = sta->tid_rx[tid];
	if (!r) {
		wil_err(wil, "BAR for non-existing CID %d TID %d\n", cid, tid);
		goto out;
	}
	if (seq_less(seq, r->head_seq_num)) {
		wil_err(wil, "BAR Seq 0x%03x preceding head 0x%03x\n",
			seq, r->head_seq_num);
		goto out;
	}
	wil_dbg_txrx(wil, "BAR: CID %d MID %d TID %d Seq 0x%03x head 0x%03x\n",
		     cid, vif->mid, tid, seq, r->head_seq_num);
	wil_release_reorder_frames(ndev, r, seq);

out:
	spin_unlock(&sta->tid_rx_lock);
}

struct wil_tid_ampdu_rx *wil_tid_ampdu_rx_alloc(struct wil6210_priv *wil,
						int size, u16 ssn)
{
	struct wil_tid_ampdu_rx *r = kzalloc(sizeof(*r), GFP_KERNEL);

	if (!r)
		return NULL;

	r->reorder_buf =
		kcalloc(size, sizeof(struct sk_buff *), GFP_KERNEL);
	if (!r->reorder_buf) {
		kfree(r->reorder_buf);
		kfree(r);
		return NULL;
	}

	r->ssn = ssn;
	r->head_seq_num = ssn;
	r->buf_size = size;
	r->stored_mpdu_num = 0;
	r->first_time = true;
	r->mcast_last_seq = U16_MAX;
	return r;
}

void wil_tid_ampdu_rx_free(struct wil6210_priv *wil,
			   struct wil_tid_ampdu_rx *r)
{
	int i;

	if (!r)
		return;

	/* Do not pass remaining frames to the network stack - it may be
	 * not expecting to get any more Rx. Rx from here may lead to
	 * kernel OOPS since some per-socket accounting info was already
	 * released.
	 */
	for (i = 0; i < r->buf_size; i++)
		kfree_skb(r->reorder_buf[i]);

	kfree(r->reorder_buf);
	kfree(r);
}

/* ADDBA processing */
static u16 wil_agg_size(struct wil6210_priv *wil, u16 req_agg_wsize)
{
	u16 max_agg_size = min_t(u16, WIL_MAX_AGG_WSIZE, WIL_MAX_AMPDU_SIZE /
				 (mtu_max + WIL_MAX_MPDU_OVERHEAD));

	if (!req_agg_wsize)
		return max_agg_size;

	return min(max_agg_size, req_agg_wsize);
}

/* Block Ack - Rx side (recipient) */
int wil_addba_rx_request(struct wil6210_priv *wil, u8 mid,
			 u8 cidxtid, u8 dialog_token, __le16 ba_param_set,
			 __le16 ba_timeout, __le16 ba_seq_ctrl)
__acquires(&sta->tid_rx_lock) __releases(&sta->tid_rx_lock)
{
	u16 param_set = le16_to_cpu(ba_param_set);
	u16 agg_timeout = le16_to_cpu(ba_timeout);
	u16 seq_ctrl = le16_to_cpu(ba_seq_ctrl);
	struct wil_sta_info *sta;
	u8 cid, tid;
	u16 agg_wsize = 0;
	/* bit 0: A-MSDU supported
	 * bit 1: policy (should be 0 for us)
	 * bits 2..5: TID
	 * bits 6..15: buffer size
	 */
	u16 req_agg_wsize = WIL_GET_BITS(param_set, 6, 15);
	bool agg_amsdu = wil->use_enhanced_dma_hw &&
		wil->use_rx_hw_reordering &&
		test_bit(WMI_FW_CAPABILITY_AMSDU, wil->fw_capabilities) &&
		wil->amsdu_en && (param_set & BIT(0));
	int ba_policy = param_set & BIT(1);
	u16 status = WLAN_STATUS_SUCCESS;
	u16 ssn = seq_ctrl >> 4;
	struct wil_tid_ampdu_rx *r;
	int rc = 0;

	might_sleep();
	parse_cidxtid(cidxtid, &cid, &tid);

	/* sanity checks */
	if (cid >= WIL6210_MAX_CID) {
		wil_err(wil, "BACK: invalid CID %d\n", cid);
		rc = -EINVAL;
		goto out;
	}

	sta = &wil->sta[cid];
	if (sta->status != wil_sta_connected) {
		wil_err(wil, "BACK: CID %d not connected\n", cid);
		rc = -EINVAL;
		goto out;
	}

	wil_dbg_wmi(wil,
		    "ADDBA request for CID %d %pM TID %d size %d timeout %d AMSDU%s policy %d token %d SSN 0x%03x\n",
		    cid, sta->addr, tid, req_agg_wsize, agg_timeout,
		    agg_amsdu ? "+" : "-", !!ba_policy, dialog_token, ssn);

	/* apply policies */
	if (ba_policy) {
		wil_err(wil, "BACK requested unsupported ba_policy == 1\n");
		status = WLAN_STATUS_INVALID_QOS_PARAM;
	}
	if (status == WLAN_STATUS_SUCCESS) {
		if (req_agg_wsize == 0) {
			wil_dbg_misc(wil, "Suggest BACK wsize %d\n",
				     WIL_MAX_AGG_WSIZE);
			agg_wsize = WIL_MAX_AGG_WSIZE;
		} else {
			agg_wsize = min_t(u16,
					  WIL_MAX_AGG_WSIZE, req_agg_wsize);
		}
	}

	rc = wil->txrx_ops.wmi_addba_rx_resp(wil, mid, cid, tid, dialog_token,
					     status, agg_amsdu, agg_wsize,
					     agg_timeout);
	if (rc || (status != WLAN_STATUS_SUCCESS)) {
		wil_err(wil, "do not apply ba, rc(%d), status(%d)\n", rc,
			status);
		goto out;
	}

	/* apply */
	r = wil_tid_ampdu_rx_alloc(wil, agg_wsize, ssn);
	spin_lock_bh(&sta->tid_rx_lock);
	wil_tid_ampdu_rx_free(wil, sta->tid_rx[tid]);
	sta->tid_rx[tid] = r;
	spin_unlock_bh(&sta->tid_rx_lock);

out:
	return rc;
}

/* BACK - Tx side (originator) */
int wil_addba_tx_request(struct wil6210_priv *wil, u8 ringid, u16 wsize)
{
	u8 agg_wsize = wil_agg_size(wil, wsize);
	u16 agg_timeout = 0;
	struct wil_ring_tx_data *txdata = &wil->ring_tx_data[ringid];
	int rc = 0;

	if (txdata->addba_in_progress) {
		wil_dbg_misc(wil, "ADDBA for vring[%d] already in progress\n",
			     ringid);
		goto out;
	}
	if (txdata->agg_wsize) {
		wil_dbg_misc(wil,
			     "ADDBA for vring[%d] already done for wsize %d\n",
			     ringid, txdata->agg_wsize);
		goto out;
	}
	txdata->addba_in_progress = true;
	rc = wmi_addba(wil, txdata->mid, ringid, agg_wsize, agg_timeout);
	if (rc) {
		wil_err(wil, "wmi_addba failed, rc (%d)", rc);
		txdata->addba_in_progress = false;
	}

out:
	return rc;
}
