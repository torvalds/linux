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

	while (seq_less(r->head_seq_num, hseq)) {
		index = reorder_index(r, r->head_seq_num);
		wil_release_reorder_frame(wil, r, index);
	}
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

void wil_rx_reorder(struct wil6210_priv *wil, struct sk_buff *skb)
{
	struct net_device *ndev = wil_to_ndev(wil);
	struct vring_rx_desc *d = wil_skb_rxdesc(skb);
	int tid = wil_rxdesc_tid(d);
	int cid = wil_rxdesc_cid(d);
	int mid = wil_rxdesc_mid(d);
	u16 seq = wil_rxdesc_seq(d);
	struct wil_sta_info *sta = &wil->sta[cid];
	struct wil_tid_ampdu_rx *r = sta->tid_rx[tid];
	u16 hseq;
	int index;

	wil_dbg_txrx(wil, "MID %d CID %d TID %d Seq 0x%03x\n",
		     mid, cid, tid, seq);

	if (!r) {
		wil_netif_rx_any(skb, ndev);
		return;
	}

	hseq = r->head_seq_num;

	spin_lock(&r->reorder_lock);

	/* frame with out of date sequence number */
	if (seq_less(seq, r->head_seq_num)) {
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
	spin_unlock(&r->reorder_lock);
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

	spin_lock_init(&r->reorder_lock);
	r->ssn = ssn;
	r->head_seq_num = ssn;
	r->buf_size = size;
	r->stored_mpdu_num = 0;
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
