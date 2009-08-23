/*
 * Copyright (c) 2008-2009 Atheros Communications Inc.
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

#include "ath9k.h"

#define BITS_PER_BYTE           8
#define OFDM_PLCP_BITS          22
#define HT_RC_2_MCS(_rc)        ((_rc) & 0x0f)
#define HT_RC_2_STREAMS(_rc)    ((((_rc) & 0x78) >> 3) + 1)
#define L_STF                   8
#define L_LTF                   8
#define L_SIG                   4
#define HT_SIG                  8
#define HT_STF                  4
#define HT_LTF(_ns)             (4 * (_ns))
#define SYMBOL_TIME(_ns)        ((_ns) << 2) /* ns * 4 us */
#define SYMBOL_TIME_HALFGI(_ns) (((_ns) * 18 + 4) / 5)  /* ns * 3.6 us */
#define NUM_SYMBOLS_PER_USEC(_usec) (_usec >> 2)
#define NUM_SYMBOLS_PER_USEC_HALFGI(_usec) (((_usec*5)-4)/18)

#define OFDM_SIFS_TIME    	    16

static u32 bits_per_symbol[][2] = {
	/* 20MHz 40MHz */
	{    26,   54 },     /*  0: BPSK */
	{    52,  108 },     /*  1: QPSK 1/2 */
	{    78,  162 },     /*  2: QPSK 3/4 */
	{   104,  216 },     /*  3: 16-QAM 1/2 */
	{   156,  324 },     /*  4: 16-QAM 3/4 */
	{   208,  432 },     /*  5: 64-QAM 2/3 */
	{   234,  486 },     /*  6: 64-QAM 3/4 */
	{   260,  540 },     /*  7: 64-QAM 5/6 */
	{    52,  108 },     /*  8: BPSK */
	{   104,  216 },     /*  9: QPSK 1/2 */
	{   156,  324 },     /* 10: QPSK 3/4 */
	{   208,  432 },     /* 11: 16-QAM 1/2 */
	{   312,  648 },     /* 12: 16-QAM 3/4 */
	{   416,  864 },     /* 13: 64-QAM 2/3 */
	{   468,  972 },     /* 14: 64-QAM 3/4 */
	{   520, 1080 },     /* 15: 64-QAM 5/6 */
};

#define IS_HT_RATE(_rate)     ((_rate) & 0x80)

static void ath_tx_send_ht_normal(struct ath_softc *sc, struct ath_txq *txq,
				  struct ath_atx_tid *tid,
				  struct list_head *bf_head);
static void ath_tx_complete_buf(struct ath_softc *sc, struct ath_buf *bf,
				struct list_head *bf_q,
				int txok, int sendbar);
static void ath_tx_txqaddbuf(struct ath_softc *sc, struct ath_txq *txq,
			     struct list_head *head);
static void ath_buf_set_rate(struct ath_softc *sc, struct ath_buf *bf);
static int ath_tx_num_badfrms(struct ath_softc *sc, struct ath_buf *bf,
			      int txok);
static void ath_tx_rc_status(struct ath_buf *bf, struct ath_desc *ds,
			     int nbad, int txok, bool update_rc);

/*********************/
/* Aggregation logic */
/*********************/

static int ath_aggr_query(struct ath_softc *sc, struct ath_node *an, u8 tidno)
{
	struct ath_atx_tid *tid;
	tid = ATH_AN_2_TID(an, tidno);

	if (tid->state & AGGR_ADDBA_COMPLETE ||
	    tid->state & AGGR_ADDBA_PROGRESS)
		return 1;
	else
		return 0;
}

static void ath_tx_queue_tid(struct ath_txq *txq, struct ath_atx_tid *tid)
{
	struct ath_atx_ac *ac = tid->ac;

	if (tid->paused)
		return;

	if (tid->sched)
		return;

	tid->sched = true;
	list_add_tail(&tid->list, &ac->tid_q);

	if (ac->sched)
		return;

	ac->sched = true;
	list_add_tail(&ac->list, &txq->axq_acq);
}

static void ath_tx_pause_tid(struct ath_softc *sc, struct ath_atx_tid *tid)
{
	struct ath_txq *txq = &sc->tx.txq[tid->ac->qnum];

	spin_lock_bh(&txq->axq_lock);
	tid->paused++;
	spin_unlock_bh(&txq->axq_lock);
}

static void ath_tx_resume_tid(struct ath_softc *sc, struct ath_atx_tid *tid)
{
	struct ath_txq *txq = &sc->tx.txq[tid->ac->qnum];

	ASSERT(tid->paused > 0);
	spin_lock_bh(&txq->axq_lock);

	tid->paused--;

	if (tid->paused > 0)
		goto unlock;

	if (list_empty(&tid->buf_q))
		goto unlock;

	ath_tx_queue_tid(txq, tid);
	ath_txq_schedule(sc, txq);
unlock:
	spin_unlock_bh(&txq->axq_lock);
}

static void ath_tx_flush_tid(struct ath_softc *sc, struct ath_atx_tid *tid)
{
	struct ath_txq *txq = &sc->tx.txq[tid->ac->qnum];
	struct ath_buf *bf;
	struct list_head bf_head;
	INIT_LIST_HEAD(&bf_head);

	ASSERT(tid->paused > 0);
	spin_lock_bh(&txq->axq_lock);

	tid->paused--;

	if (tid->paused > 0) {
		spin_unlock_bh(&txq->axq_lock);
		return;
	}

	while (!list_empty(&tid->buf_q)) {
		bf = list_first_entry(&tid->buf_q, struct ath_buf, list);
		ASSERT(!bf_isretried(bf));
		list_move_tail(&bf->list, &bf_head);
		ath_tx_send_ht_normal(sc, txq, tid, &bf_head);
	}

	spin_unlock_bh(&txq->axq_lock);
}

static void ath_tx_update_baw(struct ath_softc *sc, struct ath_atx_tid *tid,
			      int seqno)
{
	int index, cindex;

	index  = ATH_BA_INDEX(tid->seq_start, seqno);
	cindex = (tid->baw_head + index) & (ATH_TID_MAX_BUFS - 1);

	tid->tx_buf[cindex] = NULL;

	while (tid->baw_head != tid->baw_tail && !tid->tx_buf[tid->baw_head]) {
		INCR(tid->seq_start, IEEE80211_SEQ_MAX);
		INCR(tid->baw_head, ATH_TID_MAX_BUFS);
	}
}

static void ath_tx_addto_baw(struct ath_softc *sc, struct ath_atx_tid *tid,
			     struct ath_buf *bf)
{
	int index, cindex;

	if (bf_isretried(bf))
		return;

	index  = ATH_BA_INDEX(tid->seq_start, bf->bf_seqno);
	cindex = (tid->baw_head + index) & (ATH_TID_MAX_BUFS - 1);

	ASSERT(tid->tx_buf[cindex] == NULL);
	tid->tx_buf[cindex] = bf;

	if (index >= ((tid->baw_tail - tid->baw_head) &
		(ATH_TID_MAX_BUFS - 1))) {
		tid->baw_tail = cindex;
		INCR(tid->baw_tail, ATH_TID_MAX_BUFS);
	}
}

/*
 * TODO: For frame(s) that are in the retry state, we will reuse the
 * sequence number(s) without setting the retry bit. The
 * alternative is to give up on these and BAR the receiver's window
 * forward.
 */
static void ath_tid_drain(struct ath_softc *sc, struct ath_txq *txq,
			  struct ath_atx_tid *tid)

{
	struct ath_buf *bf;
	struct list_head bf_head;
	INIT_LIST_HEAD(&bf_head);

	for (;;) {
		if (list_empty(&tid->buf_q))
			break;

		bf = list_first_entry(&tid->buf_q, struct ath_buf, list);
		list_move_tail(&bf->list, &bf_head);

		if (bf_isretried(bf))
			ath_tx_update_baw(sc, tid, bf->bf_seqno);

		spin_unlock(&txq->axq_lock);
		ath_tx_complete_buf(sc, bf, &bf_head, 0, 0);
		spin_lock(&txq->axq_lock);
	}

	tid->seq_next = tid->seq_start;
	tid->baw_tail = tid->baw_head;
}

static void ath_tx_set_retry(struct ath_softc *sc, struct ath_buf *bf)
{
	struct sk_buff *skb;
	struct ieee80211_hdr *hdr;

	bf->bf_state.bf_type |= BUF_RETRY;
	bf->bf_retries++;

	skb = bf->bf_mpdu;
	hdr = (struct ieee80211_hdr *)skb->data;
	hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_RETRY);
}

static struct ath_buf* ath_clone_txbuf(struct ath_softc *sc, struct ath_buf *bf)
{
	struct ath_buf *tbf;

	spin_lock_bh(&sc->tx.txbuflock);
	ASSERT(!list_empty((&sc->tx.txbuf)));
	tbf = list_first_entry(&sc->tx.txbuf, struct ath_buf, list);
	list_del(&tbf->list);
	spin_unlock_bh(&sc->tx.txbuflock);

	ATH_TXBUF_RESET(tbf);

	tbf->bf_mpdu = bf->bf_mpdu;
	tbf->bf_buf_addr = bf->bf_buf_addr;
	*(tbf->bf_desc) = *(bf->bf_desc);
	tbf->bf_state = bf->bf_state;
	tbf->bf_dmacontext = bf->bf_dmacontext;

	return tbf;
}

static void ath_tx_complete_aggr(struct ath_softc *sc, struct ath_txq *txq,
				 struct ath_buf *bf, struct list_head *bf_q,
				 int txok)
{
	struct ath_node *an = NULL;
	struct sk_buff *skb;
	struct ieee80211_sta *sta;
	struct ieee80211_hdr *hdr;
	struct ath_atx_tid *tid = NULL;
	struct ath_buf *bf_next, *bf_last = bf->bf_lastbf;
	struct ath_desc *ds = bf_last->bf_desc;
	struct list_head bf_head, bf_pending;
	u16 seq_st = 0, acked_cnt = 0, txfail_cnt = 0;
	u32 ba[WME_BA_BMP_SIZE >> 5];
	int isaggr, txfail, txpending, sendbar = 0, needreset = 0, nbad = 0;
	bool rc_update = true;

	skb = bf->bf_mpdu;
	hdr = (struct ieee80211_hdr *)skb->data;

	rcu_read_lock();

	sta = ieee80211_find_sta(sc->hw, hdr->addr1);
	if (!sta) {
		rcu_read_unlock();
		return;
	}

	an = (struct ath_node *)sta->drv_priv;
	tid = ATH_AN_2_TID(an, bf->bf_tidno);

	isaggr = bf_isaggr(bf);
	memset(ba, 0, WME_BA_BMP_SIZE >> 3);

	if (isaggr && txok) {
		if (ATH_DS_TX_BA(ds)) {
			seq_st = ATH_DS_BA_SEQ(ds);
			memcpy(ba, ATH_DS_BA_BITMAP(ds),
			       WME_BA_BMP_SIZE >> 3);
		} else {
			/*
			 * AR5416 can become deaf/mute when BA
			 * issue happens. Chip needs to be reset.
			 * But AP code may have sychronization issues
			 * when perform internal reset in this routine.
			 * Only enable reset in STA mode for now.
			 */
			if (sc->sc_ah->opmode == NL80211_IFTYPE_STATION)
				needreset = 1;
		}
	}

	INIT_LIST_HEAD(&bf_pending);
	INIT_LIST_HEAD(&bf_head);

	nbad = ath_tx_num_badfrms(sc, bf, txok);
	while (bf) {
		txfail = txpending = 0;
		bf_next = bf->bf_next;

		if (ATH_BA_ISSET(ba, ATH_BA_INDEX(seq_st, bf->bf_seqno))) {
			/* transmit completion, subframe is
			 * acked by block ack */
			acked_cnt++;
		} else if (!isaggr && txok) {
			/* transmit completion */
			acked_cnt++;
		} else {
			if (!(tid->state & AGGR_CLEANUP) &&
			    ds->ds_txstat.ts_flags != ATH9K_TX_SW_ABORTED) {
				if (bf->bf_retries < ATH_MAX_SW_RETRIES) {
					ath_tx_set_retry(sc, bf);
					txpending = 1;
				} else {
					bf->bf_state.bf_type |= BUF_XRETRY;
					txfail = 1;
					sendbar = 1;
					txfail_cnt++;
				}
			} else {
				/*
				 * cleanup in progress, just fail
				 * the un-acked sub-frames
				 */
				txfail = 1;
			}
		}

		if (bf_next == NULL) {
			/*
			 * Make sure the last desc is reclaimed if it
			 * not a holding desc.
			 */
			if (!bf_last->bf_stale)
				list_move_tail(&bf->list, &bf_head);
			else
				INIT_LIST_HEAD(&bf_head);
		} else {
			ASSERT(!list_empty(bf_q));
			list_move_tail(&bf->list, &bf_head);
		}

		if (!txpending) {
			/*
			 * complete the acked-ones/xretried ones; update
			 * block-ack window
			 */
			spin_lock_bh(&txq->axq_lock);
			ath_tx_update_baw(sc, tid, bf->bf_seqno);
			spin_unlock_bh(&txq->axq_lock);

			if (rc_update && (acked_cnt == 1 || txfail_cnt == 1)) {
				ath_tx_rc_status(bf, ds, nbad, txok, true);
				rc_update = false;
			} else {
				ath_tx_rc_status(bf, ds, nbad, txok, false);
			}

			ath_tx_complete_buf(sc, bf, &bf_head, !txfail, sendbar);
		} else {
			/* retry the un-acked ones */
			if (bf->bf_next == NULL && bf_last->bf_stale) {
				struct ath_buf *tbf;

				tbf = ath_clone_txbuf(sc, bf_last);
				ath9k_hw_cleartxdesc(sc->sc_ah, tbf->bf_desc);
				list_add_tail(&tbf->list, &bf_head);
			} else {
				/*
				 * Clear descriptor status words for
				 * software retry
				 */
				ath9k_hw_cleartxdesc(sc->sc_ah, bf->bf_desc);
			}

			/*
			 * Put this buffer to the temporary pending
			 * queue to retain ordering
			 */
			list_splice_tail_init(&bf_head, &bf_pending);
		}

		bf = bf_next;
	}

	if (tid->state & AGGR_CLEANUP) {
		if (tid->baw_head == tid->baw_tail) {
			tid->state &= ~AGGR_ADDBA_COMPLETE;
			tid->addba_exchangeattempts = 0;
			tid->state &= ~AGGR_CLEANUP;

			/* send buffered frames as singles */
			ath_tx_flush_tid(sc, tid);
		}
		rcu_read_unlock();
		return;
	}

	/* prepend un-acked frames to the beginning of the pending frame queue */
	if (!list_empty(&bf_pending)) {
		spin_lock_bh(&txq->axq_lock);
		list_splice(&bf_pending, &tid->buf_q);
		ath_tx_queue_tid(txq, tid);
		spin_unlock_bh(&txq->axq_lock);
	}

	rcu_read_unlock();

	if (needreset)
		ath_reset(sc, false);
}

static u32 ath_lookup_rate(struct ath_softc *sc, struct ath_buf *bf,
			   struct ath_atx_tid *tid)
{
	const struct ath_rate_table *rate_table = sc->cur_rate_table;
	struct sk_buff *skb;
	struct ieee80211_tx_info *tx_info;
	struct ieee80211_tx_rate *rates;
	struct ath_tx_info_priv *tx_info_priv;
	u32 max_4ms_framelen, frmlen;
	u16 aggr_limit, legacy = 0, maxampdu;
	int i;

	skb = bf->bf_mpdu;
	tx_info = IEEE80211_SKB_CB(skb);
	rates = tx_info->control.rates;
	tx_info_priv = (struct ath_tx_info_priv *)tx_info->rate_driver_data[0];

	/*
	 * Find the lowest frame length among the rate series that will have a
	 * 4ms transmit duration.
	 * TODO - TXOP limit needs to be considered.
	 */
	max_4ms_framelen = ATH_AMPDU_LIMIT_MAX;

	for (i = 0; i < 4; i++) {
		if (rates[i].count) {
			if (!WLAN_RC_PHY_HT(rate_table->info[rates[i].idx].phy)) {
				legacy = 1;
				break;
			}

			frmlen = rate_table->info[rates[i].idx].max_4ms_framelen;
			max_4ms_framelen = min(max_4ms_framelen, frmlen);
		}
	}

	/*
	 * limit aggregate size by the minimum rate if rate selected is
	 * not a probe rate, if rate selected is a probe rate then
	 * avoid aggregation of this packet.
	 */
	if (tx_info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE || legacy)
		return 0;

	aggr_limit = min(max_4ms_framelen, (u32)ATH_AMPDU_LIMIT_DEFAULT);

	/*
	 * h/w can accept aggregates upto 16 bit lengths (65535).
	 * The IE, however can hold upto 65536, which shows up here
	 * as zero. Ignore 65536 since we  are constrained by hw.
	 */
	maxampdu = tid->an->maxampdu;
	if (maxampdu)
		aggr_limit = min(aggr_limit, maxampdu);

	return aggr_limit;
}

/*
 * Returns the number of delimiters to be added to
 * meet the minimum required mpdudensity.
 * caller should make sure that the rate is HT rate .
 */
static int ath_compute_num_delims(struct ath_softc *sc, struct ath_atx_tid *tid,
				  struct ath_buf *bf, u16 frmlen)
{
	const struct ath_rate_table *rt = sc->cur_rate_table;
	struct sk_buff *skb = bf->bf_mpdu;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	u32 nsymbits, nsymbols, mpdudensity;
	u16 minlen;
	u8 rc, flags, rix;
	int width, half_gi, ndelim, mindelim;

	/* Select standard number of delimiters based on frame length alone */
	ndelim = ATH_AGGR_GET_NDELIM(frmlen);

	/*
	 * If encryption enabled, hardware requires some more padding between
	 * subframes.
	 * TODO - this could be improved to be dependent on the rate.
	 *      The hardware can keep up at lower rates, but not higher rates
	 */
	if (bf->bf_keytype != ATH9K_KEY_TYPE_CLEAR)
		ndelim += ATH_AGGR_ENCRYPTDELIM;

	/*
	 * Convert desired mpdu density from microeconds to bytes based
	 * on highest rate in rate series (i.e. first rate) to determine
	 * required minimum length for subframe. Take into account
	 * whether high rate is 20 or 40Mhz and half or full GI.
	 */
	mpdudensity = tid->an->mpdudensity;

	/*
	 * If there is no mpdu density restriction, no further calculation
	 * is needed.
	 */
	if (mpdudensity == 0)
		return ndelim;

	rix = tx_info->control.rates[0].idx;
	flags = tx_info->control.rates[0].flags;
	rc = rt->info[rix].ratecode;
	width = (flags & IEEE80211_TX_RC_40_MHZ_WIDTH) ? 1 : 0;
	half_gi = (flags & IEEE80211_TX_RC_SHORT_GI) ? 1 : 0;

	if (half_gi)
		nsymbols = NUM_SYMBOLS_PER_USEC_HALFGI(mpdudensity);
	else
		nsymbols = NUM_SYMBOLS_PER_USEC(mpdudensity);

	if (nsymbols == 0)
		nsymbols = 1;

	nsymbits = bits_per_symbol[HT_RC_2_MCS(rc)][width];
	minlen = (nsymbols * nsymbits) / BITS_PER_BYTE;

	if (frmlen < minlen) {
		mindelim = (minlen - frmlen) / ATH_AGGR_DELIM_SZ;
		ndelim = max(mindelim, ndelim);
	}

	return ndelim;
}

static enum ATH_AGGR_STATUS ath_tx_form_aggr(struct ath_softc *sc,
					     struct ath_atx_tid *tid,
					     struct list_head *bf_q)
{
#define PADBYTES(_len) ((4 - ((_len) % 4)) % 4)
	struct ath_buf *bf, *bf_first, *bf_prev = NULL;
	int rl = 0, nframes = 0, ndelim, prev_al = 0;
	u16 aggr_limit = 0, al = 0, bpad = 0,
		al_delta, h_baw = tid->baw_size / 2;
	enum ATH_AGGR_STATUS status = ATH_AGGR_DONE;

	bf_first = list_first_entry(&tid->buf_q, struct ath_buf, list);

	do {
		bf = list_first_entry(&tid->buf_q, struct ath_buf, list);

		/* do not step over block-ack window */
		if (!BAW_WITHIN(tid->seq_start, tid->baw_size, bf->bf_seqno)) {
			status = ATH_AGGR_BAW_CLOSED;
			break;
		}

		if (!rl) {
			aggr_limit = ath_lookup_rate(sc, bf, tid);
			rl = 1;
		}

		/* do not exceed aggregation limit */
		al_delta = ATH_AGGR_DELIM_SZ + bf->bf_frmlen;

		if (nframes &&
		    (aggr_limit < (al + bpad + al_delta + prev_al))) {
			status = ATH_AGGR_LIMITED;
			break;
		}

		/* do not exceed subframe limit */
		if (nframes >= min((int)h_baw, ATH_AMPDU_SUBFRAME_DEFAULT)) {
			status = ATH_AGGR_LIMITED;
			break;
		}
		nframes++;

		/* add padding for previous frame to aggregation length */
		al += bpad + al_delta;

		/*
		 * Get the delimiters needed to meet the MPDU
		 * density for this node.
		 */
		ndelim = ath_compute_num_delims(sc, tid, bf_first, bf->bf_frmlen);
		bpad = PADBYTES(al_delta) + (ndelim << 2);

		bf->bf_next = NULL;
		bf->bf_desc->ds_link = 0;

		/* link buffers of this frame to the aggregate */
		ath_tx_addto_baw(sc, tid, bf);
		ath9k_hw_set11n_aggr_middle(sc->sc_ah, bf->bf_desc, ndelim);
		list_move_tail(&bf->list, bf_q);
		if (bf_prev) {
			bf_prev->bf_next = bf;
			bf_prev->bf_desc->ds_link = bf->bf_daddr;
		}
		bf_prev = bf;
	} while (!list_empty(&tid->buf_q));

	bf_first->bf_al = al;
	bf_first->bf_nframes = nframes;

	return status;
#undef PADBYTES
}

static void ath_tx_sched_aggr(struct ath_softc *sc, struct ath_txq *txq,
			      struct ath_atx_tid *tid)
{
	struct ath_buf *bf;
	enum ATH_AGGR_STATUS status;
	struct list_head bf_q;

	do {
		if (list_empty(&tid->buf_q))
			return;

		INIT_LIST_HEAD(&bf_q);

		status = ath_tx_form_aggr(sc, tid, &bf_q);

		/*
		 * no frames picked up to be aggregated;
		 * block-ack window is not open.
		 */
		if (list_empty(&bf_q))
			break;

		bf = list_first_entry(&bf_q, struct ath_buf, list);
		bf->bf_lastbf = list_entry(bf_q.prev, struct ath_buf, list);

		/* if only one frame, send as non-aggregate */
		if (bf->bf_nframes == 1) {
			bf->bf_state.bf_type &= ~BUF_AGGR;
			ath9k_hw_clr11n_aggr(sc->sc_ah, bf->bf_desc);
			ath_buf_set_rate(sc, bf);
			ath_tx_txqaddbuf(sc, txq, &bf_q);
			continue;
		}

		/* setup first desc of aggregate */
		bf->bf_state.bf_type |= BUF_AGGR;
		ath_buf_set_rate(sc, bf);
		ath9k_hw_set11n_aggr_first(sc->sc_ah, bf->bf_desc, bf->bf_al);

		/* anchor last desc of aggregate */
		ath9k_hw_set11n_aggr_last(sc->sc_ah, bf->bf_lastbf->bf_desc);

		txq->axq_aggr_depth++;
		ath_tx_txqaddbuf(sc, txq, &bf_q);

	} while (txq->axq_depth < ATH_AGGR_MIN_QDEPTH &&
		 status != ATH_AGGR_BAW_CLOSED);
}

int ath_tx_aggr_start(struct ath_softc *sc, struct ieee80211_sta *sta,
		      u16 tid, u16 *ssn)
{
	struct ath_atx_tid *txtid;
	struct ath_node *an;

	an = (struct ath_node *)sta->drv_priv;

	if (sc->sc_flags & SC_OP_TXAGGR) {
		txtid = ATH_AN_2_TID(an, tid);
		txtid->state |= AGGR_ADDBA_PROGRESS;
		ath_tx_pause_tid(sc, txtid);
		*ssn = txtid->seq_start;
	}

	return 0;
}

int ath_tx_aggr_stop(struct ath_softc *sc, struct ieee80211_sta *sta, u16 tid)
{
	struct ath_node *an = (struct ath_node *)sta->drv_priv;
	struct ath_atx_tid *txtid = ATH_AN_2_TID(an, tid);
	struct ath_txq *txq = &sc->tx.txq[txtid->ac->qnum];
	struct ath_buf *bf;
	struct list_head bf_head;
	INIT_LIST_HEAD(&bf_head);

	if (txtid->state & AGGR_CLEANUP)
		return 0;

	if (!(txtid->state & AGGR_ADDBA_COMPLETE)) {
		txtid->state &= ~AGGR_ADDBA_PROGRESS;
		txtid->addba_exchangeattempts = 0;
		return 0;
	}

	ath_tx_pause_tid(sc, txtid);

	/* drop all software retried frames and mark this TID */
	spin_lock_bh(&txq->axq_lock);
	while (!list_empty(&txtid->buf_q)) {
		bf = list_first_entry(&txtid->buf_q, struct ath_buf, list);
		if (!bf_isretried(bf)) {
			/*
			 * NB: it's based on the assumption that
			 * software retried frame will always stay
			 * at the head of software queue.
			 */
			break;
		}
		list_move_tail(&bf->list, &bf_head);
		ath_tx_update_baw(sc, txtid, bf->bf_seqno);
		ath_tx_complete_buf(sc, bf, &bf_head, 0, 0);
	}
	spin_unlock_bh(&txq->axq_lock);

	if (txtid->baw_head != txtid->baw_tail) {
		txtid->state |= AGGR_CLEANUP;
	} else {
		txtid->state &= ~AGGR_ADDBA_COMPLETE;
		txtid->addba_exchangeattempts = 0;
		ath_tx_flush_tid(sc, txtid);
	}

	return 0;
}

void ath_tx_aggr_resume(struct ath_softc *sc, struct ieee80211_sta *sta, u16 tid)
{
	struct ath_atx_tid *txtid;
	struct ath_node *an;

	an = (struct ath_node *)sta->drv_priv;

	if (sc->sc_flags & SC_OP_TXAGGR) {
		txtid = ATH_AN_2_TID(an, tid);
		txtid->baw_size =
			IEEE80211_MIN_AMPDU_BUF << sta->ht_cap.ampdu_factor;
		txtid->state |= AGGR_ADDBA_COMPLETE;
		txtid->state &= ~AGGR_ADDBA_PROGRESS;
		ath_tx_resume_tid(sc, txtid);
	}
}

bool ath_tx_aggr_check(struct ath_softc *sc, struct ath_node *an, u8 tidno)
{
	struct ath_atx_tid *txtid;

	if (!(sc->sc_flags & SC_OP_TXAGGR))
		return false;

	txtid = ATH_AN_2_TID(an, tidno);

	if (!(txtid->state & AGGR_ADDBA_COMPLETE)) {
		if (!(txtid->state & AGGR_ADDBA_PROGRESS) &&
		    (txtid->addba_exchangeattempts < ADDBA_EXCHANGE_ATTEMPTS)) {
			txtid->addba_exchangeattempts++;
			return true;
		}
	}

	return false;
}

/********************/
/* Queue Management */
/********************/

static void ath_txq_drain_pending_buffers(struct ath_softc *sc,
					  struct ath_txq *txq)
{
	struct ath_atx_ac *ac, *ac_tmp;
	struct ath_atx_tid *tid, *tid_tmp;

	list_for_each_entry_safe(ac, ac_tmp, &txq->axq_acq, list) {
		list_del(&ac->list);
		ac->sched = false;
		list_for_each_entry_safe(tid, tid_tmp, &ac->tid_q, list) {
			list_del(&tid->list);
			tid->sched = false;
			ath_tid_drain(sc, txq, tid);
		}
	}
}

struct ath_txq *ath_txq_setup(struct ath_softc *sc, int qtype, int subtype)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath9k_tx_queue_info qi;
	int qnum;

	memset(&qi, 0, sizeof(qi));
	qi.tqi_subtype = subtype;
	qi.tqi_aifs = ATH9K_TXQ_USEDEFAULT;
	qi.tqi_cwmin = ATH9K_TXQ_USEDEFAULT;
	qi.tqi_cwmax = ATH9K_TXQ_USEDEFAULT;
	qi.tqi_physCompBuf = 0;

	/*
	 * Enable interrupts only for EOL and DESC conditions.
	 * We mark tx descriptors to receive a DESC interrupt
	 * when a tx queue gets deep; otherwise waiting for the
	 * EOL to reap descriptors.  Note that this is done to
	 * reduce interrupt load and this only defers reaping
	 * descriptors, never transmitting frames.  Aside from
	 * reducing interrupts this also permits more concurrency.
	 * The only potential downside is if the tx queue backs
	 * up in which case the top half of the kernel may backup
	 * due to a lack of tx descriptors.
	 *
	 * The UAPSD queue is an exception, since we take a desc-
	 * based intr on the EOSP frames.
	 */
	if (qtype == ATH9K_TX_QUEUE_UAPSD)
		qi.tqi_qflags = TXQ_FLAG_TXDESCINT_ENABLE;
	else
		qi.tqi_qflags = TXQ_FLAG_TXEOLINT_ENABLE |
			TXQ_FLAG_TXDESCINT_ENABLE;
	qnum = ath9k_hw_setuptxqueue(ah, qtype, &qi);
	if (qnum == -1) {
		/*
		 * NB: don't print a message, this happens
		 * normally on parts with too few tx queues
		 */
		return NULL;
	}
	if (qnum >= ARRAY_SIZE(sc->tx.txq)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"qnum %u out of range, max %u!\n",
			qnum, (unsigned int)ARRAY_SIZE(sc->tx.txq));
		ath9k_hw_releasetxqueue(ah, qnum);
		return NULL;
	}
	if (!ATH_TXQ_SETUP(sc, qnum)) {
		struct ath_txq *txq = &sc->tx.txq[qnum];

		txq->axq_qnum = qnum;
		txq->axq_link = NULL;
		INIT_LIST_HEAD(&txq->axq_q);
		INIT_LIST_HEAD(&txq->axq_acq);
		spin_lock_init(&txq->axq_lock);
		txq->axq_depth = 0;
		txq->axq_aggr_depth = 0;
		txq->axq_totalqueued = 0;
		txq->axq_linkbuf = NULL;
		sc->tx.txqsetup |= 1<<qnum;
	}
	return &sc->tx.txq[qnum];
}

static int ath_tx_get_qnum(struct ath_softc *sc, int qtype, int haltype)
{
	int qnum;

	switch (qtype) {
	case ATH9K_TX_QUEUE_DATA:
		if (haltype >= ARRAY_SIZE(sc->tx.hwq_map)) {
			DPRINTF(sc, ATH_DBG_FATAL,
				"HAL AC %u out of range, max %zu!\n",
				haltype, ARRAY_SIZE(sc->tx.hwq_map));
			return -1;
		}
		qnum = sc->tx.hwq_map[haltype];
		break;
	case ATH9K_TX_QUEUE_BEACON:
		qnum = sc->beacon.beaconq;
		break;
	case ATH9K_TX_QUEUE_CAB:
		qnum = sc->beacon.cabq->axq_qnum;
		break;
	default:
		qnum = -1;
	}
	return qnum;
}

struct ath_txq *ath_test_get_txq(struct ath_softc *sc, struct sk_buff *skb)
{
	struct ath_txq *txq = NULL;
	int qnum;

	qnum = ath_get_hal_qnum(skb_get_queue_mapping(skb), sc);
	txq = &sc->tx.txq[qnum];

	spin_lock_bh(&txq->axq_lock);

	if (txq->axq_depth >= (ATH_TXBUF - 20)) {
		DPRINTF(sc, ATH_DBG_XMIT,
			"TX queue: %d is full, depth: %d\n",
			qnum, txq->axq_depth);
		ieee80211_stop_queue(sc->hw, skb_get_queue_mapping(skb));
		txq->stopped = 1;
		spin_unlock_bh(&txq->axq_lock);
		return NULL;
	}

	spin_unlock_bh(&txq->axq_lock);

	return txq;
}

int ath_txq_update(struct ath_softc *sc, int qnum,
		   struct ath9k_tx_queue_info *qinfo)
{
	struct ath_hw *ah = sc->sc_ah;
	int error = 0;
	struct ath9k_tx_queue_info qi;

	if (qnum == sc->beacon.beaconq) {
		/*
		 * XXX: for beacon queue, we just save the parameter.
		 * It will be picked up by ath_beaconq_config when
		 * it's necessary.
		 */
		sc->beacon.beacon_qi = *qinfo;
		return 0;
	}

	ASSERT(sc->tx.txq[qnum].axq_qnum == qnum);

	ath9k_hw_get_txq_props(ah, qnum, &qi);
	qi.tqi_aifs = qinfo->tqi_aifs;
	qi.tqi_cwmin = qinfo->tqi_cwmin;
	qi.tqi_cwmax = qinfo->tqi_cwmax;
	qi.tqi_burstTime = qinfo->tqi_burstTime;
	qi.tqi_readyTime = qinfo->tqi_readyTime;

	if (!ath9k_hw_set_txq_props(ah, qnum, &qi)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to update hardware queue %u!\n", qnum);
		error = -EIO;
	} else {
		ath9k_hw_resettxqueue(ah, qnum);
	}

	return error;
}

int ath_cabq_update(struct ath_softc *sc)
{
	struct ath9k_tx_queue_info qi;
	int qnum = sc->beacon.cabq->axq_qnum;

	ath9k_hw_get_txq_props(sc->sc_ah, qnum, &qi);
	/*
	 * Ensure the readytime % is within the bounds.
	 */
	if (sc->config.cabqReadytime < ATH9K_READY_TIME_LO_BOUND)
		sc->config.cabqReadytime = ATH9K_READY_TIME_LO_BOUND;
	else if (sc->config.cabqReadytime > ATH9K_READY_TIME_HI_BOUND)
		sc->config.cabqReadytime = ATH9K_READY_TIME_HI_BOUND;

	qi.tqi_readyTime = (sc->beacon_interval *
			    sc->config.cabqReadytime) / 100;
	ath_txq_update(sc, qnum, &qi);

	return 0;
}

/*
 * Drain a given TX queue (could be Beacon or Data)
 *
 * This assumes output has been stopped and
 * we do not need to block ath_tx_tasklet.
 */
void ath_draintxq(struct ath_softc *sc, struct ath_txq *txq, bool retry_tx)
{
	struct ath_buf *bf, *lastbf;
	struct list_head bf_head;

	INIT_LIST_HEAD(&bf_head);

	for (;;) {
		spin_lock_bh(&txq->axq_lock);

		if (list_empty(&txq->axq_q)) {
			txq->axq_link = NULL;
			txq->axq_linkbuf = NULL;
			spin_unlock_bh(&txq->axq_lock);
			break;
		}

		bf = list_first_entry(&txq->axq_q, struct ath_buf, list);

		if (bf->bf_stale) {
			list_del(&bf->list);
			spin_unlock_bh(&txq->axq_lock);

			spin_lock_bh(&sc->tx.txbuflock);
			list_add_tail(&bf->list, &sc->tx.txbuf);
			spin_unlock_bh(&sc->tx.txbuflock);
			continue;
		}

		lastbf = bf->bf_lastbf;
		if (!retry_tx)
			lastbf->bf_desc->ds_txstat.ts_flags =
				ATH9K_TX_SW_ABORTED;

		/* remove ath_buf's of the same mpdu from txq */
		list_cut_position(&bf_head, &txq->axq_q, &lastbf->list);
		txq->axq_depth--;

		spin_unlock_bh(&txq->axq_lock);

		if (bf_isampdu(bf))
			ath_tx_complete_aggr(sc, txq, bf, &bf_head, 0);
		else
			ath_tx_complete_buf(sc, bf, &bf_head, 0, 0);
	}

	/* flush any pending frames if aggregation is enabled */
	if (sc->sc_flags & SC_OP_TXAGGR) {
		if (!retry_tx) {
			spin_lock_bh(&txq->axq_lock);
			ath_txq_drain_pending_buffers(sc, txq);
			spin_unlock_bh(&txq->axq_lock);
		}
	}
}

void ath_drain_all_txq(struct ath_softc *sc, bool retry_tx)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_txq *txq;
	int i, npend = 0;

	if (sc->sc_flags & SC_OP_INVALID)
		return;

	/* Stop beacon queue */
	ath9k_hw_stoptxdma(sc->sc_ah, sc->beacon.beaconq);

	/* Stop data queues */
	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++) {
		if (ATH_TXQ_SETUP(sc, i)) {
			txq = &sc->tx.txq[i];
			ath9k_hw_stoptxdma(ah, txq->axq_qnum);
			npend += ath9k_hw_numtxpending(ah, txq->axq_qnum);
		}
	}

	if (npend) {
		int r;

		DPRINTF(sc, ATH_DBG_XMIT, "Unable to stop TxDMA. Reset HAL!\n");

		spin_lock_bh(&sc->sc_resetlock);
		r = ath9k_hw_reset(ah, sc->sc_ah->curchan, true);
		if (r)
			DPRINTF(sc, ATH_DBG_FATAL,
				"Unable to reset hardware; reset status %d\n",
				r);
		spin_unlock_bh(&sc->sc_resetlock);
	}

	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++) {
		if (ATH_TXQ_SETUP(sc, i))
			ath_draintxq(sc, &sc->tx.txq[i], retry_tx);
	}
}

void ath_tx_cleanupq(struct ath_softc *sc, struct ath_txq *txq)
{
	ath9k_hw_releasetxqueue(sc->sc_ah, txq->axq_qnum);
	sc->tx.txqsetup &= ~(1<<txq->axq_qnum);
}

void ath_txq_schedule(struct ath_softc *sc, struct ath_txq *txq)
{
	struct ath_atx_ac *ac;
	struct ath_atx_tid *tid;

	if (list_empty(&txq->axq_acq))
		return;

	ac = list_first_entry(&txq->axq_acq, struct ath_atx_ac, list);
	list_del(&ac->list);
	ac->sched = false;

	do {
		if (list_empty(&ac->tid_q))
			return;

		tid = list_first_entry(&ac->tid_q, struct ath_atx_tid, list);
		list_del(&tid->list);
		tid->sched = false;

		if (tid->paused)
			continue;

		if ((txq->axq_depth % 2) == 0)
			ath_tx_sched_aggr(sc, txq, tid);

		/*
		 * add tid to round-robin queue if more frames
		 * are pending for the tid
		 */
		if (!list_empty(&tid->buf_q))
			ath_tx_queue_tid(txq, tid);

		break;
	} while (!list_empty(&ac->tid_q));

	if (!list_empty(&ac->tid_q)) {
		if (!ac->sched) {
			ac->sched = true;
			list_add_tail(&ac->list, &txq->axq_acq);
		}
	}
}

int ath_tx_setup(struct ath_softc *sc, int haltype)
{
	struct ath_txq *txq;

	if (haltype >= ARRAY_SIZE(sc->tx.hwq_map)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"HAL AC %u out of range, max %zu!\n",
			 haltype, ARRAY_SIZE(sc->tx.hwq_map));
		return 0;
	}
	txq = ath_txq_setup(sc, ATH9K_TX_QUEUE_DATA, haltype);
	if (txq != NULL) {
		sc->tx.hwq_map[haltype] = txq->axq_qnum;
		return 1;
	} else
		return 0;
}

/***********/
/* TX, DMA */
/***********/

/*
 * Insert a chain of ath_buf (descriptors) on a txq and
 * assume the descriptors are already chained together by caller.
 */
static void ath_tx_txqaddbuf(struct ath_softc *sc, struct ath_txq *txq,
			     struct list_head *head)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_buf *bf;

	/*
	 * Insert the frame on the outbound list and
	 * pass it on to the hardware.
	 */

	if (list_empty(head))
		return;

	bf = list_first_entry(head, struct ath_buf, list);

	list_splice_tail_init(head, &txq->axq_q);
	txq->axq_depth++;
	txq->axq_totalqueued++;
	txq->axq_linkbuf = list_entry(txq->axq_q.prev, struct ath_buf, list);

	DPRINTF(sc, ATH_DBG_QUEUE,
		"qnum: %d, txq depth: %d\n", txq->axq_qnum, txq->axq_depth);

	if (txq->axq_link == NULL) {
		ath9k_hw_puttxbuf(ah, txq->axq_qnum, bf->bf_daddr);
		DPRINTF(sc, ATH_DBG_XMIT,
			"TXDP[%u] = %llx (%p)\n",
			txq->axq_qnum, ito64(bf->bf_daddr), bf->bf_desc);
	} else {
		*txq->axq_link = bf->bf_daddr;
		DPRINTF(sc, ATH_DBG_XMIT, "link[%u] (%p)=%llx (%p)\n",
			txq->axq_qnum, txq->axq_link,
			ito64(bf->bf_daddr), bf->bf_desc);
	}
	txq->axq_link = &(bf->bf_lastbf->bf_desc->ds_link);
	ath9k_hw_txstart(ah, txq->axq_qnum);
}

static struct ath_buf *ath_tx_get_buffer(struct ath_softc *sc)
{
	struct ath_buf *bf = NULL;

	spin_lock_bh(&sc->tx.txbuflock);

	if (unlikely(list_empty(&sc->tx.txbuf))) {
		spin_unlock_bh(&sc->tx.txbuflock);
		return NULL;
	}

	bf = list_first_entry(&sc->tx.txbuf, struct ath_buf, list);
	list_del(&bf->list);

	spin_unlock_bh(&sc->tx.txbuflock);

	return bf;
}

static void ath_tx_send_ampdu(struct ath_softc *sc, struct ath_atx_tid *tid,
			      struct list_head *bf_head,
			      struct ath_tx_control *txctl)
{
	struct ath_buf *bf;

	bf = list_first_entry(bf_head, struct ath_buf, list);
	bf->bf_state.bf_type |= BUF_AMPDU;

	/*
	 * Do not queue to h/w when any of the following conditions is true:
	 * - there are pending frames in software queue
	 * - the TID is currently paused for ADDBA/BAR request
	 * - seqno is not within block-ack window
	 * - h/w queue depth exceeds low water mark
	 */
	if (!list_empty(&tid->buf_q) || tid->paused ||
	    !BAW_WITHIN(tid->seq_start, tid->baw_size, bf->bf_seqno) ||
	    txctl->txq->axq_depth >= ATH_AGGR_MIN_QDEPTH) {
		/*
		 * Add this frame to software queue for scheduling later
		 * for aggregation.
		 */
		list_move_tail(&bf->list, &tid->buf_q);
		ath_tx_queue_tid(txctl->txq, tid);
		return;
	}

	/* Add sub-frame to BAW */
	ath_tx_addto_baw(sc, tid, bf);

	/* Queue to h/w without aggregation */
	bf->bf_nframes = 1;
	bf->bf_lastbf = bf;
	ath_buf_set_rate(sc, bf);
	ath_tx_txqaddbuf(sc, txctl->txq, bf_head);
}

static void ath_tx_send_ht_normal(struct ath_softc *sc, struct ath_txq *txq,
				  struct ath_atx_tid *tid,
				  struct list_head *bf_head)
{
	struct ath_buf *bf;

	bf = list_first_entry(bf_head, struct ath_buf, list);
	bf->bf_state.bf_type &= ~BUF_AMPDU;

	/* update starting sequence number for subsequent ADDBA request */
	INCR(tid->seq_start, IEEE80211_SEQ_MAX);

	bf->bf_nframes = 1;
	bf->bf_lastbf = bf;
	ath_buf_set_rate(sc, bf);
	ath_tx_txqaddbuf(sc, txq, bf_head);
}

static void ath_tx_send_normal(struct ath_softc *sc, struct ath_txq *txq,
			       struct list_head *bf_head)
{
	struct ath_buf *bf;

	bf = list_first_entry(bf_head, struct ath_buf, list);

	bf->bf_lastbf = bf;
	bf->bf_nframes = 1;
	ath_buf_set_rate(sc, bf);
	ath_tx_txqaddbuf(sc, txq, bf_head);
}

static enum ath9k_pkt_type get_hw_packet_type(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;
	enum ath9k_pkt_type htype;
	__le16 fc;

	hdr = (struct ieee80211_hdr *)skb->data;
	fc = hdr->frame_control;

	if (ieee80211_is_beacon(fc))
		htype = ATH9K_PKT_TYPE_BEACON;
	else if (ieee80211_is_probe_resp(fc))
		htype = ATH9K_PKT_TYPE_PROBE_RESP;
	else if (ieee80211_is_atim(fc))
		htype = ATH9K_PKT_TYPE_ATIM;
	else if (ieee80211_is_pspoll(fc))
		htype = ATH9K_PKT_TYPE_PSPOLL;
	else
		htype = ATH9K_PKT_TYPE_NORMAL;

	return htype;
}

static bool is_pae(struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;
	__le16 fc;

	hdr = (struct ieee80211_hdr *)skb->data;
	fc = hdr->frame_control;

	if (ieee80211_is_data(fc)) {
		if (ieee80211_is_nullfunc(fc) ||
		    /* Port Access Entity (IEEE 802.1X) */
		    (skb->protocol == cpu_to_be16(ETH_P_PAE))) {
			return true;
		}
	}

	return false;
}

static int get_hw_crypto_keytype(struct sk_buff *skb)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);

	if (tx_info->control.hw_key) {
		if (tx_info->control.hw_key->alg == ALG_WEP)
			return ATH9K_KEY_TYPE_WEP;
		else if (tx_info->control.hw_key->alg == ALG_TKIP)
			return ATH9K_KEY_TYPE_TKIP;
		else if (tx_info->control.hw_key->alg == ALG_CCMP)
			return ATH9K_KEY_TYPE_AES;
	}

	return ATH9K_KEY_TYPE_CLEAR;
}

static void assign_aggr_tid_seqno(struct sk_buff *skb,
				  struct ath_buf *bf)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr;
	struct ath_node *an;
	struct ath_atx_tid *tid;
	__le16 fc;
	u8 *qc;

	if (!tx_info->control.sta)
		return;

	an = (struct ath_node *)tx_info->control.sta->drv_priv;
	hdr = (struct ieee80211_hdr *)skb->data;
	fc = hdr->frame_control;

	if (ieee80211_is_data_qos(fc)) {
		qc = ieee80211_get_qos_ctl(hdr);
		bf->bf_tidno = qc[0] & 0xf;
	}

	/*
	 * For HT capable stations, we save tidno for later use.
	 * We also override seqno set by upper layer with the one
	 * in tx aggregation state.
	 *
	 * If fragmentation is on, the sequence number is
	 * not overridden, since it has been
	 * incremented by the fragmentation routine.
	 *
	 * FIXME: check if the fragmentation threshold exceeds
	 * IEEE80211 max.
	 */
	tid = ATH_AN_2_TID(an, bf->bf_tidno);
	hdr->seq_ctrl = cpu_to_le16(tid->seq_next <<
			IEEE80211_SEQ_SEQ_SHIFT);
	bf->bf_seqno = tid->seq_next;
	INCR(tid->seq_next, IEEE80211_SEQ_MAX);
}

static int setup_tx_flags(struct ath_softc *sc, struct sk_buff *skb,
			  struct ath_txq *txq)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	int flags = 0;

	flags |= ATH9K_TXDESC_CLRDMASK; /* needed for crypto errors */
	flags |= ATH9K_TXDESC_INTREQ;

	if (tx_info->flags & IEEE80211_TX_CTL_NO_ACK)
		flags |= ATH9K_TXDESC_NOACK;

	return flags;
}

/*
 * rix - rate index
 * pktlen - total bytes (delims + data + fcs + pads + pad delims)
 * width  - 0 for 20 MHz, 1 for 40 MHz
 * half_gi - to use 4us v/s 3.6 us for symbol time
 */
static u32 ath_pkt_duration(struct ath_softc *sc, u8 rix, struct ath_buf *bf,
			    int width, int half_gi, bool shortPreamble)
{
	const struct ath_rate_table *rate_table = sc->cur_rate_table;
	u32 nbits, nsymbits, duration, nsymbols;
	u8 rc;
	int streams, pktlen;

	pktlen = bf_isaggr(bf) ? bf->bf_al : bf->bf_frmlen;
	rc = rate_table->info[rix].ratecode;

	/* for legacy rates, use old function to compute packet duration */
	if (!IS_HT_RATE(rc))
		return ath9k_hw_computetxtime(sc->sc_ah, rate_table, pktlen,
					      rix, shortPreamble);

	/* find number of symbols: PLCP + data */
	nbits = (pktlen << 3) + OFDM_PLCP_BITS;
	nsymbits = bits_per_symbol[HT_RC_2_MCS(rc)][width];
	nsymbols = (nbits + nsymbits - 1) / nsymbits;

	if (!half_gi)
		duration = SYMBOL_TIME(nsymbols);
	else
		duration = SYMBOL_TIME_HALFGI(nsymbols);

	/* addup duration for legacy/ht training and signal fields */
	streams = HT_RC_2_STREAMS(rc);
	duration += L_STF + L_LTF + L_SIG + HT_SIG + HT_STF + HT_LTF(streams);

	return duration;
}

static void ath_buf_set_rate(struct ath_softc *sc, struct ath_buf *bf)
{
	const struct ath_rate_table *rt = sc->cur_rate_table;
	struct ath9k_11n_rate_series series[4];
	struct sk_buff *skb;
	struct ieee80211_tx_info *tx_info;
	struct ieee80211_tx_rate *rates;
	struct ieee80211_hdr *hdr;
	int i, flags = 0;
	u8 rix = 0, ctsrate = 0;
	bool is_pspoll;

	memset(series, 0, sizeof(struct ath9k_11n_rate_series) * 4);

	skb = bf->bf_mpdu;
	tx_info = IEEE80211_SKB_CB(skb);
	rates = tx_info->control.rates;
	hdr = (struct ieee80211_hdr *)skb->data;
	is_pspoll = ieee80211_is_pspoll(hdr->frame_control);

	/*
	 * We check if Short Preamble is needed for the CTS rate by
	 * checking the BSS's global flag.
	 * But for the rate series, IEEE80211_TX_RC_USE_SHORT_PREAMBLE is used.
	 */
	if (sc->sc_flags & SC_OP_PREAMBLE_SHORT)
		ctsrate = rt->info[tx_info->control.rts_cts_rate_idx].ratecode |
			rt->info[tx_info->control.rts_cts_rate_idx].short_preamble;
	else
		ctsrate = rt->info[tx_info->control.rts_cts_rate_idx].ratecode;

	/*
	 * ATH9K_TXDESC_RTSENA and ATH9K_TXDESC_CTSENA are mutually exclusive.
	 * Check the first rate in the series to decide whether RTS/CTS
	 * or CTS-to-self has to be used.
	 */
	if (rates[0].flags & IEEE80211_TX_RC_USE_CTS_PROTECT)
		flags = ATH9K_TXDESC_CTSENA;
	else if (rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS)
		flags = ATH9K_TXDESC_RTSENA;

	/* FIXME: Handle aggregation protection */
	if (sc->config.ath_aggr_prot &&
	    (!bf_isaggr(bf) || (bf_isaggr(bf) && bf->bf_al < 8192))) {
		flags = ATH9K_TXDESC_RTSENA;
	}

	/* For AR5416 - RTS cannot be followed by a frame larger than 8K */
	if (bf_isaggr(bf) && (bf->bf_al > sc->sc_ah->caps.rts_aggr_limit))
		flags &= ~(ATH9K_TXDESC_RTSENA);

	for (i = 0; i < 4; i++) {
		if (!rates[i].count || (rates[i].idx < 0))
			continue;

		rix = rates[i].idx;
		series[i].Tries = rates[i].count;
		series[i].ChSel = sc->tx_chainmask;

		if (rates[i].flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
			series[i].Rate = rt->info[rix].ratecode |
				rt->info[rix].short_preamble;
		else
			series[i].Rate = rt->info[rix].ratecode;

		if (rates[i].flags & IEEE80211_TX_RC_USE_RTS_CTS)
			series[i].RateFlags |= ATH9K_RATESERIES_RTS_CTS;
		if (rates[i].flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			series[i].RateFlags |= ATH9K_RATESERIES_2040;
		if (rates[i].flags & IEEE80211_TX_RC_SHORT_GI)
			series[i].RateFlags |= ATH9K_RATESERIES_HALFGI;

		series[i].PktDuration = ath_pkt_duration(sc, rix, bf,
			 (rates[i].flags & IEEE80211_TX_RC_40_MHZ_WIDTH) != 0,
			 (rates[i].flags & IEEE80211_TX_RC_SHORT_GI),
			 (rates[i].flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE));
	}

	/* set dur_update_en for l-sig computation except for PS-Poll frames */
	ath9k_hw_set11n_ratescenario(sc->sc_ah, bf->bf_desc,
				     bf->bf_lastbf->bf_desc,
				     !is_pspoll, ctsrate,
				     0, series, 4, flags);

	if (sc->config.ath_aggr_prot && flags)
		ath9k_hw_set11n_burstduration(sc->sc_ah, bf->bf_desc, 8192);
}

static int ath_tx_setup_buffer(struct ieee80211_hw *hw, struct ath_buf *bf,
				struct sk_buff *skb,
				struct ath_tx_control *txctl)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ath_tx_info_priv *tx_info_priv;
	int hdrlen;
	__le16 fc;

	tx_info_priv = kzalloc(sizeof(*tx_info_priv), GFP_ATOMIC);
	if (unlikely(!tx_info_priv))
		return -ENOMEM;
	tx_info->rate_driver_data[0] = tx_info_priv;
	tx_info_priv->aphy = aphy;
	tx_info_priv->frame_type = txctl->frame_type;
	hdrlen = ieee80211_get_hdrlen_from_skb(skb);
	fc = hdr->frame_control;

	ATH_TXBUF_RESET(bf);

	bf->bf_frmlen = skb->len + FCS_LEN - (hdrlen & 3);

	if (conf_is_ht(&sc->hw->conf) && !is_pae(skb))
		bf->bf_state.bf_type |= BUF_HT;

	bf->bf_flags = setup_tx_flags(sc, skb, txctl->txq);

	bf->bf_keytype = get_hw_crypto_keytype(skb);
	if (bf->bf_keytype != ATH9K_KEY_TYPE_CLEAR) {
		bf->bf_frmlen += tx_info->control.hw_key->icv_len;
		bf->bf_keyix = tx_info->control.hw_key->hw_key_idx;
	} else {
		bf->bf_keyix = ATH9K_TXKEYIX_INVALID;
	}

	if (ieee80211_is_data_qos(fc) && (sc->sc_flags & SC_OP_TXAGGR))
		assign_aggr_tid_seqno(skb, bf);

	bf->bf_mpdu = skb;

	bf->bf_dmacontext = dma_map_single(sc->dev, skb->data,
					   skb->len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(sc->dev, bf->bf_dmacontext))) {
		bf->bf_mpdu = NULL;
		kfree(tx_info_priv);
		tx_info->rate_driver_data[0] = NULL;
		DPRINTF(sc, ATH_DBG_FATAL, "dma_mapping_error() on TX\n");
		return -ENOMEM;
	}

	bf->bf_buf_addr = bf->bf_dmacontext;
	return 0;
}

/* FIXME: tx power */
static void ath_tx_start_dma(struct ath_softc *sc, struct ath_buf *bf,
			     struct ath_tx_control *txctl)
{
	struct sk_buff *skb = bf->bf_mpdu;
	struct ieee80211_tx_info *tx_info =  IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ath_node *an = NULL;
	struct list_head bf_head;
	struct ath_desc *ds;
	struct ath_atx_tid *tid;
	struct ath_hw *ah = sc->sc_ah;
	int frm_type;
	__le16 fc;

	frm_type = get_hw_packet_type(skb);
	fc = hdr->frame_control;

	INIT_LIST_HEAD(&bf_head);
	list_add_tail(&bf->list, &bf_head);

	ds = bf->bf_desc;
	ds->ds_link = 0;
	ds->ds_data = bf->bf_buf_addr;

	ath9k_hw_set11n_txdesc(ah, ds, bf->bf_frmlen, frm_type, MAX_RATE_POWER,
			       bf->bf_keyix, bf->bf_keytype, bf->bf_flags);

	ath9k_hw_filltxdesc(ah, ds,
			    skb->len,	/* segment length */
			    true,	/* first segment */
			    true,	/* last segment */
			    ds);	/* first descriptor */

	spin_lock_bh(&txctl->txq->axq_lock);

	if (bf_isht(bf) && (sc->sc_flags & SC_OP_TXAGGR) &&
	    tx_info->control.sta) {
		an = (struct ath_node *)tx_info->control.sta->drv_priv;
		tid = ATH_AN_2_TID(an, bf->bf_tidno);

		if (!ieee80211_is_data_qos(fc)) {
			ath_tx_send_normal(sc, txctl->txq, &bf_head);
			goto tx_done;
		}

		if (ath_aggr_query(sc, an, bf->bf_tidno)) {
			/*
			 * Try aggregation if it's a unicast data frame
			 * and the destination is HT capable.
			 */
			ath_tx_send_ampdu(sc, tid, &bf_head, txctl);
		} else {
			/*
			 * Send this frame as regular when ADDBA
			 * exchange is neither complete nor pending.
			 */
			ath_tx_send_ht_normal(sc, txctl->txq,
					      tid, &bf_head);
		}
	} else {
		ath_tx_send_normal(sc, txctl->txq, &bf_head);
	}

tx_done:
	spin_unlock_bh(&txctl->txq->axq_lock);
}

/* Upon failure caller should free skb */
int ath_tx_start(struct ieee80211_hw *hw, struct sk_buff *skb,
		 struct ath_tx_control *txctl)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath_buf *bf;
	int r;

	bf = ath_tx_get_buffer(sc);
	if (!bf) {
		DPRINTF(sc, ATH_DBG_XMIT, "TX buffers are full\n");
		return -1;
	}

	r = ath_tx_setup_buffer(hw, bf, skb, txctl);
	if (unlikely(r)) {
		struct ath_txq *txq = txctl->txq;

		DPRINTF(sc, ATH_DBG_FATAL, "TX mem alloc failure\n");

		/* upon ath_tx_processq() this TX queue will be resumed, we
		 * guarantee this will happen by knowing beforehand that
		 * we will at least have to run TX completionon one buffer
		 * on the queue */
		spin_lock_bh(&txq->axq_lock);
		if (sc->tx.txq[txq->axq_qnum].axq_depth > 1) {
			ieee80211_stop_queue(sc->hw,
				skb_get_queue_mapping(skb));
			txq->stopped = 1;
		}
		spin_unlock_bh(&txq->axq_lock);

		spin_lock_bh(&sc->tx.txbuflock);
		list_add_tail(&bf->list, &sc->tx.txbuf);
		spin_unlock_bh(&sc->tx.txbuflock);

		return r;
	}

	ath_tx_start_dma(sc, bf, txctl);

	return 0;
}

void ath_tx_cabq(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	int hdrlen, padsize;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ath_tx_control txctl;

	memset(&txctl, 0, sizeof(struct ath_tx_control));

	/*
	 * As a temporary workaround, assign seq# here; this will likely need
	 * to be cleaned up to work better with Beacon transmission and virtual
	 * BSSes.
	 */
	if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ) {
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
		if (info->flags & IEEE80211_TX_CTL_FIRST_FRAGMENT)
			sc->tx.seq_no += 0x10;
		hdr->seq_ctrl &= cpu_to_le16(IEEE80211_SCTL_FRAG);
		hdr->seq_ctrl |= cpu_to_le16(sc->tx.seq_no);
	}

	/* Add the padding after the header if this is not already done */
	hdrlen = ieee80211_get_hdrlen_from_skb(skb);
	if (hdrlen & 3) {
		padsize = hdrlen % 4;
		if (skb_headroom(skb) < padsize) {
			DPRINTF(sc, ATH_DBG_XMIT, "TX CABQ padding failed\n");
			dev_kfree_skb_any(skb);
			return;
		}
		skb_push(skb, padsize);
		memmove(skb->data, skb->data + padsize, hdrlen);
	}

	txctl.txq = sc->beacon.cabq;

	DPRINTF(sc, ATH_DBG_XMIT, "transmitting CABQ packet, skb: %p\n", skb);

	if (ath_tx_start(hw, skb, &txctl) != 0) {
		DPRINTF(sc, ATH_DBG_XMIT, "CABQ TX failed\n");
		goto exit;
	}

	return;
exit:
	dev_kfree_skb_any(skb);
}

/*****************/
/* TX Completion */
/*****************/

static void ath_tx_complete(struct ath_softc *sc, struct sk_buff *skb,
			    int tx_flags)
{
	struct ieee80211_hw *hw = sc->hw;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ath_tx_info_priv *tx_info_priv = ATH_TX_INFO_PRIV(tx_info);
	int hdrlen, padsize;
	int frame_type = ATH9K_NOT_INTERNAL;

	DPRINTF(sc, ATH_DBG_XMIT, "TX complete: skb: %p\n", skb);

	if (tx_info_priv) {
		hw = tx_info_priv->aphy->hw;
		frame_type = tx_info_priv->frame_type;
	}

	if (tx_info->flags & IEEE80211_TX_CTL_NO_ACK ||
	    tx_info->flags & IEEE80211_TX_STAT_TX_FILTERED) {
		kfree(tx_info_priv);
		tx_info->rate_driver_data[0] = NULL;
	}

	if (tx_flags & ATH_TX_BAR)
		tx_info->flags |= IEEE80211_TX_STAT_AMPDU_NO_BACK;

	if (!(tx_flags & (ATH_TX_ERROR | ATH_TX_XRETRY))) {
		/* Frame was ACKed */
		tx_info->flags |= IEEE80211_TX_STAT_ACK;
	}

	hdrlen = ieee80211_get_hdrlen_from_skb(skb);
	padsize = hdrlen & 3;
	if (padsize && hdrlen >= 24) {
		/*
		 * Remove MAC header padding before giving the frame back to
		 * mac80211.
		 */
		memmove(skb->data + padsize, skb->data, hdrlen);
		skb_pull(skb, padsize);
	}

	if (sc->sc_flags & SC_OP_WAIT_FOR_TX_ACK) {
		sc->sc_flags &= ~SC_OP_WAIT_FOR_TX_ACK;
		DPRINTF(sc, ATH_DBG_PS, "Going back to sleep after having "
			"received TX status (0x%x)\n",
			sc->sc_flags & (SC_OP_WAIT_FOR_BEACON |
					SC_OP_WAIT_FOR_CAB |
					SC_OP_WAIT_FOR_PSPOLL_DATA |
					SC_OP_WAIT_FOR_TX_ACK));
	}

	if (frame_type == ATH9K_NOT_INTERNAL)
		ieee80211_tx_status(hw, skb);
	else
		ath9k_tx_status(hw, skb);
}

static void ath_tx_complete_buf(struct ath_softc *sc, struct ath_buf *bf,
				struct list_head *bf_q,
				int txok, int sendbar)
{
	struct sk_buff *skb = bf->bf_mpdu;
	unsigned long flags;
	int tx_flags = 0;


	if (sendbar)
		tx_flags = ATH_TX_BAR;

	if (!txok) {
		tx_flags |= ATH_TX_ERROR;

		if (bf_isxretried(bf))
			tx_flags |= ATH_TX_XRETRY;
	}

	dma_unmap_single(sc->dev, bf->bf_dmacontext, skb->len, DMA_TO_DEVICE);
	ath_tx_complete(sc, skb, tx_flags);

	/*
	 * Return the list of ath_buf of this mpdu to free queue
	 */
	spin_lock_irqsave(&sc->tx.txbuflock, flags);
	list_splice_tail_init(bf_q, &sc->tx.txbuf);
	spin_unlock_irqrestore(&sc->tx.txbuflock, flags);
}

static int ath_tx_num_badfrms(struct ath_softc *sc, struct ath_buf *bf,
			      int txok)
{
	struct ath_buf *bf_last = bf->bf_lastbf;
	struct ath_desc *ds = bf_last->bf_desc;
	u16 seq_st = 0;
	u32 ba[WME_BA_BMP_SIZE >> 5];
	int ba_index;
	int nbad = 0;
	int isaggr = 0;

	if (ds->ds_txstat.ts_flags == ATH9K_TX_SW_ABORTED)
		return 0;

	isaggr = bf_isaggr(bf);
	if (isaggr) {
		seq_st = ATH_DS_BA_SEQ(ds);
		memcpy(ba, ATH_DS_BA_BITMAP(ds), WME_BA_BMP_SIZE >> 3);
	}

	while (bf) {
		ba_index = ATH_BA_INDEX(seq_st, bf->bf_seqno);
		if (!txok || (isaggr && !ATH_BA_ISSET(ba, ba_index)))
			nbad++;

		bf = bf->bf_next;
	}

	return nbad;
}

static void ath_tx_rc_status(struct ath_buf *bf, struct ath_desc *ds,
			     int nbad, int txok, bool update_rc)
{
	struct sk_buff *skb = bf->bf_mpdu;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ath_tx_info_priv *tx_info_priv = ATH_TX_INFO_PRIV(tx_info);
	struct ieee80211_hw *hw = tx_info_priv->aphy->hw;
	u8 i, tx_rateindex;

	if (txok)
		tx_info->status.ack_signal = ds->ds_txstat.ts_rssi;

	tx_rateindex = ds->ds_txstat.ts_rateindex;
	WARN_ON(tx_rateindex >= hw->max_rates);

	tx_info_priv->update_rc = update_rc;
	if (ds->ds_txstat.ts_status & ATH9K_TXERR_FILT)
		tx_info->flags |= IEEE80211_TX_STAT_TX_FILTERED;

	if ((ds->ds_txstat.ts_status & ATH9K_TXERR_FILT) == 0 &&
	    (bf->bf_flags & ATH9K_TXDESC_NOACK) == 0 && update_rc) {
		if (ieee80211_is_data(hdr->frame_control)) {
			memcpy(&tx_info_priv->tx, &ds->ds_txstat,
			       sizeof(tx_info_priv->tx));
			tx_info_priv->n_frames = bf->bf_nframes;
			tx_info_priv->n_bad_frames = nbad;
		}
	}

	for (i = tx_rateindex + 1; i < hw->max_rates; i++)
		tx_info->status.rates[i].count = 0;

	tx_info->status.rates[tx_rateindex].count = bf->bf_retries + 1;
}

static void ath_wake_mac80211_queue(struct ath_softc *sc, struct ath_txq *txq)
{
	int qnum;

	spin_lock_bh(&txq->axq_lock);
	if (txq->stopped &&
	    sc->tx.txq[txq->axq_qnum].axq_depth <= (ATH_TXBUF - 20)) {
		qnum = ath_get_mac80211_qnum(txq->axq_qnum, sc);
		if (qnum != -1) {
			ieee80211_wake_queue(sc->hw, qnum);
			txq->stopped = 0;
		}
	}
	spin_unlock_bh(&txq->axq_lock);
}

static void ath_tx_processq(struct ath_softc *sc, struct ath_txq *txq)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_buf *bf, *lastbf, *bf_held = NULL;
	struct list_head bf_head;
	struct ath_desc *ds;
	int txok;
	int status;

	DPRINTF(sc, ATH_DBG_QUEUE, "tx queue %d (%x), link %p\n",
		txq->axq_qnum, ath9k_hw_gettxbuf(sc->sc_ah, txq->axq_qnum),
		txq->axq_link);

	for (;;) {
		spin_lock_bh(&txq->axq_lock);
		if (list_empty(&txq->axq_q)) {
			txq->axq_link = NULL;
			txq->axq_linkbuf = NULL;
			spin_unlock_bh(&txq->axq_lock);
			break;
		}
		bf = list_first_entry(&txq->axq_q, struct ath_buf, list);

		/*
		 * There is a race condition that a BH gets scheduled
		 * after sw writes TxE and before hw re-load the last
		 * descriptor to get the newly chained one.
		 * Software must keep the last DONE descriptor as a
		 * holding descriptor - software does so by marking
		 * it with the STALE flag.
		 */
		bf_held = NULL;
		if (bf->bf_stale) {
			bf_held = bf;
			if (list_is_last(&bf_held->list, &txq->axq_q)) {
				txq->axq_link = NULL;
				txq->axq_linkbuf = NULL;
				spin_unlock_bh(&txq->axq_lock);

				/*
				 * The holding descriptor is the last
				 * descriptor in queue. It's safe to remove
				 * the last holding descriptor in BH context.
				 */
				spin_lock_bh(&sc->tx.txbuflock);
				list_move_tail(&bf_held->list, &sc->tx.txbuf);
				spin_unlock_bh(&sc->tx.txbuflock);

				break;
			} else {
				bf = list_entry(bf_held->list.next,
						struct ath_buf, list);
			}
		}

		lastbf = bf->bf_lastbf;
		ds = lastbf->bf_desc;

		status = ath9k_hw_txprocdesc(ah, ds);
		if (status == -EINPROGRESS) {
			spin_unlock_bh(&txq->axq_lock);
			break;
		}
		if (bf->bf_desc == txq->axq_lastdsWithCTS)
			txq->axq_lastdsWithCTS = NULL;
		if (ds == txq->axq_gatingds)
			txq->axq_gatingds = NULL;

		/*
		 * Remove ath_buf's of the same transmit unit from txq,
		 * however leave the last descriptor back as the holding
		 * descriptor for hw.
		 */
		lastbf->bf_stale = true;
		INIT_LIST_HEAD(&bf_head);
		if (!list_is_singular(&lastbf->list))
			list_cut_position(&bf_head,
				&txq->axq_q, lastbf->list.prev);

		txq->axq_depth--;
		if (bf_isaggr(bf))
			txq->axq_aggr_depth--;

		txok = (ds->ds_txstat.ts_status == 0);
		spin_unlock_bh(&txq->axq_lock);

		if (bf_held) {
			spin_lock_bh(&sc->tx.txbuflock);
			list_move_tail(&bf_held->list, &sc->tx.txbuf);
			spin_unlock_bh(&sc->tx.txbuflock);
		}

		if (!bf_isampdu(bf)) {
			/*
			 * This frame is sent out as a single frame.
			 * Use hardware retry status for this frame.
			 */
			bf->bf_retries = ds->ds_txstat.ts_longretry;
			if (ds->ds_txstat.ts_status & ATH9K_TXERR_XRETRY)
				bf->bf_state.bf_type |= BUF_XRETRY;
			ath_tx_rc_status(bf, ds, 0, txok, true);
		}

		if (bf_isampdu(bf))
			ath_tx_complete_aggr(sc, txq, bf, &bf_head, txok);
		else
			ath_tx_complete_buf(sc, bf, &bf_head, txok, 0);

		ath_wake_mac80211_queue(sc, txq);

		spin_lock_bh(&txq->axq_lock);
		if (sc->sc_flags & SC_OP_TXAGGR)
			ath_txq_schedule(sc, txq);
		spin_unlock_bh(&txq->axq_lock);
	}
}


void ath_tx_tasklet(struct ath_softc *sc)
{
	int i;
	u32 qcumask = ((1 << ATH9K_NUM_TX_QUEUES) - 1);

	ath9k_hw_gettxintrtxqs(sc->sc_ah, &qcumask);

	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++) {
		if (ATH_TXQ_SETUP(sc, i) && (qcumask & (1 << i)))
			ath_tx_processq(sc, &sc->tx.txq[i]);
	}
}

/*****************/
/* Init, Cleanup */
/*****************/

int ath_tx_init(struct ath_softc *sc, int nbufs)
{
	int error = 0;

	spin_lock_init(&sc->tx.txbuflock);

	error = ath_descdma_setup(sc, &sc->tx.txdma, &sc->tx.txbuf,
				  "tx", nbufs, 1);
	if (error != 0) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Failed to allocate tx descriptors: %d\n", error);
		goto err;
	}

	error = ath_descdma_setup(sc, &sc->beacon.bdma, &sc->beacon.bbuf,
				  "beacon", ATH_BCBUF, 1);
	if (error != 0) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Failed to allocate beacon descriptors: %d\n", error);
		goto err;
	}

err:
	if (error != 0)
		ath_tx_cleanup(sc);

	return error;
}

void ath_tx_cleanup(struct ath_softc *sc)
{
	if (sc->beacon.bdma.dd_desc_len != 0)
		ath_descdma_cleanup(sc, &sc->beacon.bdma, &sc->beacon.bbuf);

	if (sc->tx.txdma.dd_desc_len != 0)
		ath_descdma_cleanup(sc, &sc->tx.txdma, &sc->tx.txbuf);
}

void ath_tx_node_init(struct ath_softc *sc, struct ath_node *an)
{
	struct ath_atx_tid *tid;
	struct ath_atx_ac *ac;
	int tidno, acno;

	for (tidno = 0, tid = &an->tid[tidno];
	     tidno < WME_NUM_TID;
	     tidno++, tid++) {
		tid->an        = an;
		tid->tidno     = tidno;
		tid->seq_start = tid->seq_next = 0;
		tid->baw_size  = WME_MAX_BA;
		tid->baw_head  = tid->baw_tail = 0;
		tid->sched     = false;
		tid->paused    = false;
		tid->state &= ~AGGR_CLEANUP;
		INIT_LIST_HEAD(&tid->buf_q);
		acno = TID_TO_WME_AC(tidno);
		tid->ac = &an->ac[acno];
		tid->state &= ~AGGR_ADDBA_COMPLETE;
		tid->state &= ~AGGR_ADDBA_PROGRESS;
		tid->addba_exchangeattempts = 0;
	}

	for (acno = 0, ac = &an->ac[acno];
	     acno < WME_NUM_AC; acno++, ac++) {
		ac->sched    = false;
		INIT_LIST_HEAD(&ac->tid_q);

		switch (acno) {
		case WME_AC_BE:
			ac->qnum = ath_tx_get_qnum(sc,
				   ATH9K_TX_QUEUE_DATA, ATH9K_WME_AC_BE);
			break;
		case WME_AC_BK:
			ac->qnum = ath_tx_get_qnum(sc,
				   ATH9K_TX_QUEUE_DATA, ATH9K_WME_AC_BK);
			break;
		case WME_AC_VI:
			ac->qnum = ath_tx_get_qnum(sc,
				   ATH9K_TX_QUEUE_DATA, ATH9K_WME_AC_VI);
			break;
		case WME_AC_VO:
			ac->qnum = ath_tx_get_qnum(sc,
				   ATH9K_TX_QUEUE_DATA, ATH9K_WME_AC_VO);
			break;
		}
	}
}

void ath_tx_node_cleanup(struct ath_softc *sc, struct ath_node *an)
{
	int i;
	struct ath_atx_ac *ac, *ac_tmp;
	struct ath_atx_tid *tid, *tid_tmp;
	struct ath_txq *txq;

	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++) {
		if (ATH_TXQ_SETUP(sc, i)) {
			txq = &sc->tx.txq[i];

			spin_lock(&txq->axq_lock);

			list_for_each_entry_safe(ac,
					ac_tmp, &txq->axq_acq, list) {
				tid = list_first_entry(&ac->tid_q,
						struct ath_atx_tid, list);
				if (tid && tid->an != an)
					continue;
				list_del(&ac->list);
				ac->sched = false;

				list_for_each_entry_safe(tid,
						tid_tmp, &ac->tid_q, list) {
					list_del(&tid->list);
					tid->sched = false;
					ath_tid_drain(sc, txq, tid);
					tid->state &= ~AGGR_ADDBA_COMPLETE;
					tid->addba_exchangeattempts = 0;
					tid->state &= ~AGGR_CLEANUP;
				}
			}

			spin_unlock(&txq->axq_lock);
		}
	}
}
