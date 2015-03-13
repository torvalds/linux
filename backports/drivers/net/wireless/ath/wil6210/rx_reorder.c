/*
 * Copyright (c) 2014 Qualcomm Atheros, Inc.
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

static void wil_release_reorder_frame(struct wil6210_priv *wil,
				      struct wil_tid_ampdu_rx *r,
				      int index)
{
	struct net_device *ndev = wil_to_ndev(wil);
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

static void wil_release_reorder_frames(struct wil6210_priv *wil,
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
		wil_release_reorder_frame(wil, r, index);
	}
	r->head_seq_num = hseq;
}

static void wil_reorder_release(struct wil6210_priv *wil,
				struct wil_tid_ampdu_rx *r)
{
	int index = reorder_index(r, r->head_seq_num);

	while (r->reorder_buf[index]) {
		wil_release_reorder_frame(wil, r, index);
		index = reorder_index(r, r->head_seq_num);
	}
}

/* called in NAPI context */
void wil_rx_reorder(struct wil6210_priv *wil, struct sk_buff *skb)
__acquires(&sta->tid_rx_lock) __releases(&sta->tid_rx_lock)
{
	struct net_device *ndev = wil_to_ndev(wil);
	struct vring_rx_desc *d = wil_skb_rxdesc(skb);
	int tid = wil_rxdesc_tid(d);
	int cid = wil_rxdesc_cid(d);
	int mid = wil_rxdesc_mid(d);
	u16 seq = wil_rxdesc_seq(d);
	int mcast = wil_rxdesc_mcast(d);
	struct wil_sta_info *sta = &wil->sta[cid];
	struct wil_tid_ampdu_rx *r;
	u16 hseq;
	int index;

	wil_dbg_txrx(wil, "MID %d CID %d TID %d Seq 0x%03x mcast %01x\n",
		     mid, cid, tid, seq, mcast);

	if (unlikely(mcast)) {
		wil_netif_rx_any(skb, ndev);
		return;
	}

	spin_lock(&sta->tid_rx_lock);

	r = sta->tid_rx[tid];
	if (!r) {
		wil_netif_rx_any(skb, ndev);
		goto out;
	}

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
		wil_release_reorder_frames(wil, r, hseq);
	}

	/* Now the new frame is always in the range of the reordering buffer */

	index = reorder_index(r, seq);

	/* check if we already stored this frame */
	if (r->reorder_buf[index]) {
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
	r->reorder_time[index] = jiffies;
	r->stored_mpdu_num++;
	wil_reorder_release(wil, r);

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
	r->reorder_time =
		kcalloc(size, sizeof(unsigned long), GFP_KERNEL);
	if (!r->reorder_buf || !r->reorder_time) {
		kfree(r->reorder_buf);
		kfree(r->reorder_time);
		kfree(r);
		return NULL;
	}

	r->ssn = ssn;
	r->head_seq_num = ssn;
	r->buf_size = size;
	r->stored_mpdu_num = 0;
	r->first_time = true;
	return r;
}

void wil_tid_ampdu_rx_free(struct wil6210_priv *wil,
			   struct wil_tid_ampdu_rx *r)
{
	if (!r)
		return;
	wil_release_reorder_frames(wil, r, r->head_seq_num + r->buf_size);
	kfree(r->reorder_buf);
	kfree(r->reorder_time);
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

/* Block Ack - Rx side (recipient */
int wil_addba_rx_request(struct wil6210_priv *wil, u8 cidxtid,
			 u8 dialog_token, __le16 ba_param_set,
			 __le16 ba_timeout, __le16 ba_seq_ctrl)
{
	struct wil_back_rx *req = kzalloc(sizeof(*req), GFP_KERNEL);

	if (!req)
		return -ENOMEM;

	req->cidxtid = cidxtid;
	req->dialog_token = dialog_token;
	req->ba_param_set = le16_to_cpu(ba_param_set);
	req->ba_timeout = le16_to_cpu(ba_timeout);
	req->ba_seq_ctrl = le16_to_cpu(ba_seq_ctrl);

	mutex_lock(&wil->back_rx_mutex);
	list_add_tail(&req->list, &wil->back_rx_pending);
	mutex_unlock(&wil->back_rx_mutex);

	queue_work(wil->wq_service, &wil->back_rx_worker);

	return 0;
}

static void wil_back_rx_handle(struct wil6210_priv *wil,
			       struct wil_back_rx *req)
__acquires(&sta->tid_rx_lock) __releases(&sta->tid_rx_lock)
{
	struct wil_sta_info *sta;
	u8 cid, tid;
	u16 agg_wsize = 0;
	/* bit 0: A-MSDU supported
	 * bit 1: policy (should be 0 for us)
	 * bits 2..5: TID
	 * bits 6..15: buffer size
	 */
	u16 req_agg_wsize = WIL_GET_BITS(req->ba_param_set, 6, 15);
	bool agg_amsdu = !!(req->ba_param_set & BIT(0));
	int ba_policy = req->ba_param_set & BIT(1);
	u16 agg_timeout = req->ba_timeout;
	u16 status = WLAN_STATUS_SUCCESS;
	u16 ssn = req->ba_seq_ctrl >> 4;
	int rc;

	might_sleep();
	parse_cidxtid(req->cidxtid, &cid, &tid);

	/* sanity checks */
	if (cid >= WIL6210_MAX_CID) {
		wil_err(wil, "BACK: invalid CID %d\n", cid);
		return;
	}

	sta = &wil->sta[cid];
	if (sta->status != wil_sta_connected) {
		wil_err(wil, "BACK: CID %d not connected\n", cid);
		return;
	}

	wil_dbg_wmi(wil,
		    "ADDBA request for CID %d %pM TID %d size %d timeout %d AMSDU%s policy %d token %d SSN 0x%03x\n",
		    cid, sta->addr, tid, req_agg_wsize, req->ba_timeout,
		    agg_amsdu ? "+" : "-", !!ba_policy, req->dialog_token, ssn);

	/* apply policies */
	if (ba_policy) {
		wil_err(wil, "BACK requested unsupported ba_policy == 1\n");
		status = WLAN_STATUS_INVALID_QOS_PARAM;
	}
	if (status == WLAN_STATUS_SUCCESS)
		agg_wsize = wil_agg_size(wil, req_agg_wsize);

	rc = wmi_addba_rx_resp(wil, cid, tid, req->dialog_token, status,
			       agg_amsdu, agg_wsize, agg_timeout);
	if (rc || (status != WLAN_STATUS_SUCCESS))
		return;

	/* apply */
	spin_lock_bh(&sta->tid_rx_lock);

	wil_tid_ampdu_rx_free(wil, sta->tid_rx[tid]);
	sta->tid_rx[tid] = wil_tid_ampdu_rx_alloc(wil, agg_wsize, ssn);

	spin_unlock_bh(&sta->tid_rx_lock);
}

void wil_back_rx_flush(struct wil6210_priv *wil)
{
	struct wil_back_rx *evt, *t;

	wil_dbg_misc(wil, "%s()\n", __func__);

	mutex_lock(&wil->back_rx_mutex);

	list_for_each_entry_safe(evt, t, &wil->back_rx_pending, list) {
		list_del(&evt->list);
		kfree(evt);
	}

	mutex_unlock(&wil->back_rx_mutex);
}

/* Retrieve next ADDBA request from the pending list */
static struct list_head *next_back_rx(struct wil6210_priv *wil)
{
	struct list_head *ret = NULL;

	mutex_lock(&wil->back_rx_mutex);

	if (!list_empty(&wil->back_rx_pending)) {
		ret = wil->back_rx_pending.next;
		list_del(ret);
	}

	mutex_unlock(&wil->back_rx_mutex);

	return ret;
}

void wil_back_rx_worker(struct work_struct *work)
{
	struct wil6210_priv *wil = container_of(work, struct wil6210_priv,
						back_rx_worker);
	struct wil_back_rx *evt;
	struct list_head *lh;

	while ((lh = next_back_rx(wil)) != NULL) {
		evt = list_entry(lh, struct wil_back_rx, list);

		wil_back_rx_handle(wil, evt);
		kfree(evt);
	}
}

/* BACK - Tx (originator) side */
static void wil_back_tx_handle(struct wil6210_priv *wil,
			       struct wil_back_tx *req)
{
	struct vring_tx_data *txdata = &wil->vring_tx_data[req->ringid];
	int rc;

	if (txdata->addba_in_progress) {
		wil_dbg_misc(wil, "ADDBA for vring[%d] already in progress\n",
			     req->ringid);
		return;
	}
	if (txdata->agg_wsize) {
		wil_dbg_misc(wil,
			     "ADDBA for vring[%d] already established wsize %d\n",
			     req->ringid, txdata->agg_wsize);
		return;
	}
	txdata->addba_in_progress = true;
	rc = wmi_addba(wil, req->ringid, req->agg_wsize, req->agg_timeout);
	if (rc)
		txdata->addba_in_progress = false;
}

static struct list_head *next_back_tx(struct wil6210_priv *wil)
{
	struct list_head *ret = NULL;

	mutex_lock(&wil->back_tx_mutex);

	if (!list_empty(&wil->back_tx_pending)) {
		ret = wil->back_tx_pending.next;
		list_del(ret);
	}

	mutex_unlock(&wil->back_tx_mutex);

	return ret;
}

void wil_back_tx_worker(struct work_struct *work)
{
	struct wil6210_priv *wil = container_of(work, struct wil6210_priv,
						 back_tx_worker);
	struct wil_back_tx *evt;
	struct list_head *lh;

	while ((lh = next_back_tx(wil)) != NULL) {
		evt = list_entry(lh, struct wil_back_tx, list);

		wil_back_tx_handle(wil, evt);
		kfree(evt);
	}
}

void wil_back_tx_flush(struct wil6210_priv *wil)
{
	struct wil_back_tx *evt, *t;

	wil_dbg_misc(wil, "%s()\n", __func__);

	mutex_lock(&wil->back_tx_mutex);

	list_for_each_entry_safe(evt, t, &wil->back_tx_pending, list) {
		list_del(&evt->list);
		kfree(evt);
	}

	mutex_unlock(&wil->back_tx_mutex);
}

int wil_addba_tx_request(struct wil6210_priv *wil, u8 ringid, u16 wsize)
{
	struct wil_back_tx *req = kzalloc(sizeof(*req), GFP_KERNEL);

	if (!req)
		return -ENOMEM;

	req->ringid = ringid;
	req->agg_wsize = wil_agg_size(wil, wsize);
	req->agg_timeout = 0;

	mutex_lock(&wil->back_tx_mutex);
	list_add_tail(&req->list, &wil->back_tx_pending);
	mutex_unlock(&wil->back_tx_mutex);

	queue_work(wil->wq_service, &wil->back_tx_worker);

	return 0;
}
