/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
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
#include "ar9003_mac.h"

#define BITS_PER_BYTE           8
#define OFDM_PLCP_BITS          22
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


static u16 bits_per_symbol[][2] = {
	/* 20MHz 40MHz */
	{    26,   54 },     /*  0: BPSK */
	{    52,  108 },     /*  1: QPSK 1/2 */
	{    78,  162 },     /*  2: QPSK 3/4 */
	{   104,  216 },     /*  3: 16-QAM 1/2 */
	{   156,  324 },     /*  4: 16-QAM 3/4 */
	{   208,  432 },     /*  5: 64-QAM 2/3 */
	{   234,  486 },     /*  6: 64-QAM 3/4 */
	{   260,  540 },     /*  7: 64-QAM 5/6 */
};

#define IS_HT_RATE(_rate)     ((_rate) & 0x80)

static void ath_tx_send_normal(struct ath_softc *sc, struct ath_txq *txq,
			       struct ath_atx_tid *tid,
			       struct list_head *bf_head);
static void ath_tx_complete_buf(struct ath_softc *sc, struct ath_buf *bf,
				struct ath_txq *txq, struct list_head *bf_q,
				struct ath_tx_status *ts, int txok, int sendbar);
static void ath_tx_txqaddbuf(struct ath_softc *sc, struct ath_txq *txq,
			     struct list_head *head);
static void ath_buf_set_rate(struct ath_softc *sc, struct ath_buf *bf, int len);
static void ath_tx_rc_status(struct ath_softc *sc, struct ath_buf *bf,
			     struct ath_tx_status *ts, int nframes, int nbad,
			     int txok, bool update_rc);
static void ath_tx_update_baw(struct ath_softc *sc, struct ath_atx_tid *tid,
			      int seqno);

enum {
	MCS_HT20,
	MCS_HT20_SGI,
	MCS_HT40,
	MCS_HT40_SGI,
};

static int ath_max_4ms_framelen[4][32] = {
	[MCS_HT20] = {
		3212,  6432,  9648,  12864,  19300,  25736,  28952,  32172,
		6424,  12852, 19280, 25708,  38568,  51424,  57852,  64280,
		9628,  19260, 28896, 38528,  57792,  65532,  65532,  65532,
		12828, 25656, 38488, 51320,  65532,  65532,  65532,  65532,
	},
	[MCS_HT20_SGI] = {
		3572,  7144,  10720,  14296,  21444,  28596,  32172,  35744,
		7140,  14284, 21428,  28568,  42856,  57144,  64288,  65532,
		10700, 21408, 32112,  42816,  64228,  65532,  65532,  65532,
		14256, 28516, 42780,  57040,  65532,  65532,  65532,  65532,
	},
	[MCS_HT40] = {
		6680,  13360,  20044,  26724,  40092,  53456,  60140,  65532,
		13348, 26700,  40052,  53400,  65532,  65532,  65532,  65532,
		20004, 40008,  60016,  65532,  65532,  65532,  65532,  65532,
		26644, 53292,  65532,  65532,  65532,  65532,  65532,  65532,
	},
	[MCS_HT40_SGI] = {
		7420,  14844,  22272,  29696,  44544,  59396,  65532,  65532,
		14832, 29668,  44504,  59340,  65532,  65532,  65532,  65532,
		22232, 44464,  65532,  65532,  65532,  65532,  65532,  65532,
		29616, 59232,  65532,  65532,  65532,  65532,  65532,  65532,
	}
};

/*********************/
/* Aggregation logic */
/*********************/

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

static void ath_tx_resume_tid(struct ath_softc *sc, struct ath_atx_tid *tid)
{
	struct ath_txq *txq = tid->ac->txq;

	WARN_ON(!tid->paused);

	spin_lock_bh(&txq->axq_lock);
	tid->paused = false;

	if (list_empty(&tid->buf_q))
		goto unlock;

	ath_tx_queue_tid(txq, tid);
	ath_txq_schedule(sc, txq);
unlock:
	spin_unlock_bh(&txq->axq_lock);
}

static struct ath_frame_info *get_frame_info(struct sk_buff *skb)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	BUILD_BUG_ON(sizeof(struct ath_frame_info) >
		     sizeof(tx_info->rate_driver_data));
	return (struct ath_frame_info *) &tx_info->rate_driver_data[0];
}

static void ath_tx_flush_tid(struct ath_softc *sc, struct ath_atx_tid *tid)
{
	struct ath_txq *txq = tid->ac->txq;
	struct ath_buf *bf;
	struct list_head bf_head;
	struct ath_tx_status ts;
	struct ath_frame_info *fi;

	INIT_LIST_HEAD(&bf_head);

	memset(&ts, 0, sizeof(ts));
	spin_lock_bh(&txq->axq_lock);

	while (!list_empty(&tid->buf_q)) {
		bf = list_first_entry(&tid->buf_q, struct ath_buf, list);
		list_move_tail(&bf->list, &bf_head);

		spin_unlock_bh(&txq->axq_lock);
		fi = get_frame_info(bf->bf_mpdu);
		if (fi->retries) {
			ath_tx_update_baw(sc, tid, fi->seqno);
			ath_tx_complete_buf(sc, bf, txq, &bf_head, &ts, 0, 1);
		} else {
			ath_tx_send_normal(sc, txq, NULL, &bf_head);
		}
		spin_lock_bh(&txq->axq_lock);
	}

	spin_unlock_bh(&txq->axq_lock);
}

static void ath_tx_update_baw(struct ath_softc *sc, struct ath_atx_tid *tid,
			      int seqno)
{
	int index, cindex;

	index  = ATH_BA_INDEX(tid->seq_start, seqno);
	cindex = (tid->baw_head + index) & (ATH_TID_MAX_BUFS - 1);

	__clear_bit(cindex, tid->tx_buf);

	while (tid->baw_head != tid->baw_tail && !test_bit(tid->baw_head, tid->tx_buf)) {
		INCR(tid->seq_start, IEEE80211_SEQ_MAX);
		INCR(tid->baw_head, ATH_TID_MAX_BUFS);
	}
}

static void ath_tx_addto_baw(struct ath_softc *sc, struct ath_atx_tid *tid,
			     u16 seqno)
{
	int index, cindex;

	index  = ATH_BA_INDEX(tid->seq_start, seqno);
	cindex = (tid->baw_head + index) & (ATH_TID_MAX_BUFS - 1);
	__set_bit(cindex, tid->tx_buf);

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
	struct ath_tx_status ts;
	struct ath_frame_info *fi;

	memset(&ts, 0, sizeof(ts));
	INIT_LIST_HEAD(&bf_head);

	for (;;) {
		if (list_empty(&tid->buf_q))
			break;

		bf = list_first_entry(&tid->buf_q, struct ath_buf, list);
		list_move_tail(&bf->list, &bf_head);

		fi = get_frame_info(bf->bf_mpdu);
		if (fi->retries)
			ath_tx_update_baw(sc, tid, fi->seqno);

		spin_unlock(&txq->axq_lock);
		ath_tx_complete_buf(sc, bf, txq, &bf_head, &ts, 0, 0);
		spin_lock(&txq->axq_lock);
	}

	tid->seq_next = tid->seq_start;
	tid->baw_tail = tid->baw_head;
}

static void ath_tx_set_retry(struct ath_softc *sc, struct ath_txq *txq,
			     struct sk_buff *skb)
{
	struct ath_frame_info *fi = get_frame_info(skb);
	struct ieee80211_hdr *hdr;

	TX_STAT_INC(txq->axq_qnum, a_retries);
	if (fi->retries++ > 0)
		return;

	hdr = (struct ieee80211_hdr *)skb->data;
	hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_RETRY);
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

static void ath_tx_return_buffer(struct ath_softc *sc, struct ath_buf *bf)
{
	spin_lock_bh(&sc->tx.txbuflock);
	list_add_tail(&bf->list, &sc->tx.txbuf);
	spin_unlock_bh(&sc->tx.txbuflock);
}

static struct ath_buf* ath_clone_txbuf(struct ath_softc *sc, struct ath_buf *bf)
{
	struct ath_buf *tbf;

	tbf = ath_tx_get_buffer(sc);
	if (WARN_ON(!tbf))
		return NULL;

	ATH_TXBUF_RESET(tbf);

	tbf->bf_mpdu = bf->bf_mpdu;
	tbf->bf_buf_addr = bf->bf_buf_addr;
	memcpy(tbf->bf_desc, bf->bf_desc, sc->sc_ah->caps.tx_desc_len);
	tbf->bf_state = bf->bf_state;

	return tbf;
}

static void ath_tx_count_frames(struct ath_softc *sc, struct ath_buf *bf,
			        struct ath_tx_status *ts, int txok,
			        int *nframes, int *nbad)
{
	struct ath_frame_info *fi;
	u16 seq_st = 0;
	u32 ba[WME_BA_BMP_SIZE >> 5];
	int ba_index;
	int isaggr = 0;

	*nbad = 0;
	*nframes = 0;

	isaggr = bf_isaggr(bf);
	if (isaggr) {
		seq_st = ts->ts_seqnum;
		memcpy(ba, &ts->ba_low, WME_BA_BMP_SIZE >> 3);
	}

	while (bf) {
		fi = get_frame_info(bf->bf_mpdu);
		ba_index = ATH_BA_INDEX(seq_st, fi->seqno);

		(*nframes)++;
		if (!txok || (isaggr && !ATH_BA_ISSET(ba, ba_index)))
			(*nbad)++;

		bf = bf->bf_next;
	}
}


static void ath_tx_complete_aggr(struct ath_softc *sc, struct ath_txq *txq,
				 struct ath_buf *bf, struct list_head *bf_q,
				 struct ath_tx_status *ts, int txok, bool retry)
{
	struct ath_node *an = NULL;
	struct sk_buff *skb;
	struct ieee80211_sta *sta;
	struct ieee80211_hw *hw = sc->hw;
	struct ieee80211_hdr *hdr;
	struct ieee80211_tx_info *tx_info;
	struct ath_atx_tid *tid = NULL;
	struct ath_buf *bf_next, *bf_last = bf->bf_lastbf;
	struct list_head bf_head, bf_pending;
	u16 seq_st = 0, acked_cnt = 0, txfail_cnt = 0;
	u32 ba[WME_BA_BMP_SIZE >> 5];
	int isaggr, txfail, txpending, sendbar = 0, needreset = 0, nbad = 0;
	bool rc_update = true;
	struct ieee80211_tx_rate rates[4];
	struct ath_frame_info *fi;
	int nframes;
	u8 tidno;
	bool clear_filter;

	skb = bf->bf_mpdu;
	hdr = (struct ieee80211_hdr *)skb->data;

	tx_info = IEEE80211_SKB_CB(skb);

	memcpy(rates, tx_info->control.rates, sizeof(rates));

	rcu_read_lock();

	sta = ieee80211_find_sta_by_ifaddr(hw, hdr->addr1, hdr->addr2);
	if (!sta) {
		rcu_read_unlock();

		INIT_LIST_HEAD(&bf_head);
		while (bf) {
			bf_next = bf->bf_next;

			bf->bf_state.bf_type |= BUF_XRETRY;
			if ((sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) ||
			    !bf->bf_stale || bf_next != NULL)
				list_move_tail(&bf->list, &bf_head);

			ath_tx_rc_status(sc, bf, ts, 1, 1, 0, false);
			ath_tx_complete_buf(sc, bf, txq, &bf_head, ts,
				0, 0);

			bf = bf_next;
		}
		return;
	}

	an = (struct ath_node *)sta->drv_priv;
	tidno = ieee80211_get_qos_ctl(hdr)[0] & IEEE80211_QOS_CTL_TID_MASK;
	tid = ATH_AN_2_TID(an, tidno);

	/*
	 * The hardware occasionally sends a tx status for the wrong TID.
	 * In this case, the BA status cannot be considered valid and all
	 * subframes need to be retransmitted
	 */
	if (tidno != ts->tid)
		txok = false;

	isaggr = bf_isaggr(bf);
	memset(ba, 0, WME_BA_BMP_SIZE >> 3);

	if (isaggr && txok) {
		if (ts->ts_flags & ATH9K_TX_BA) {
			seq_st = ts->ts_seqnum;
			memcpy(ba, &ts->ba_low, WME_BA_BMP_SIZE >> 3);
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

	ath_tx_count_frames(sc, bf, ts, txok, &nframes, &nbad);
	while (bf) {
		txfail = txpending = sendbar = 0;
		bf_next = bf->bf_next;

		skb = bf->bf_mpdu;
		tx_info = IEEE80211_SKB_CB(skb);
		fi = get_frame_info(skb);

		if (ATH_BA_ISSET(ba, ATH_BA_INDEX(seq_st, fi->seqno))) {
			/* transmit completion, subframe is
			 * acked by block ack */
			acked_cnt++;
		} else if (!isaggr && txok) {
			/* transmit completion */
			acked_cnt++;
		} else {
			if ((tid->state & AGGR_CLEANUP) || !retry) {
				/*
				 * cleanup in progress, just fail
				 * the un-acked sub-frames
				 */
				txfail = 1;
			} else if (fi->retries < ATH_MAX_SW_RETRIES) {
				if (!(ts->ts_status & ATH9K_TXERR_FILT) ||
				    !an->sleeping)
					ath_tx_set_retry(sc, txq, bf->bf_mpdu);

				clear_filter = true;
				txpending = 1;
			} else {
				bf->bf_state.bf_type |= BUF_XRETRY;
				txfail = 1;
				sendbar = 1;
				txfail_cnt++;
			}
		}

		if (!(sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) &&
		    bf_next == NULL) {
			/*
			 * Make sure the last desc is reclaimed if it
			 * not a holding desc.
			 */
			if (!bf_last->bf_stale)
				list_move_tail(&bf->list, &bf_head);
			else
				INIT_LIST_HEAD(&bf_head);
		} else {
			BUG_ON(list_empty(bf_q));
			list_move_tail(&bf->list, &bf_head);
		}

		if (!txpending || (tid->state & AGGR_CLEANUP)) {
			/*
			 * complete the acked-ones/xretried ones; update
			 * block-ack window
			 */
			spin_lock_bh(&txq->axq_lock);
			ath_tx_update_baw(sc, tid, fi->seqno);
			spin_unlock_bh(&txq->axq_lock);

			if (rc_update && (acked_cnt == 1 || txfail_cnt == 1)) {
				memcpy(tx_info->control.rates, rates, sizeof(rates));
				ath_tx_rc_status(sc, bf, ts, nframes, nbad, txok, true);
				rc_update = false;
			} else {
				ath_tx_rc_status(sc, bf, ts, nframes, nbad, txok, false);
			}

			ath_tx_complete_buf(sc, bf, txq, &bf_head, ts,
				!txfail, sendbar);
		} else {
			/* retry the un-acked ones */
			ath9k_hw_set_clrdmask(sc->sc_ah, bf->bf_desc, false);
			if (!(sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA)) {
				if (bf->bf_next == NULL && bf_last->bf_stale) {
					struct ath_buf *tbf;

					tbf = ath_clone_txbuf(sc, bf_last);
					/*
					 * Update tx baw and complete the
					 * frame with failed status if we
					 * run out of tx buf.
					 */
					if (!tbf) {
						spin_lock_bh(&txq->axq_lock);
						ath_tx_update_baw(sc, tid, fi->seqno);
						spin_unlock_bh(&txq->axq_lock);

						bf->bf_state.bf_type |=
							BUF_XRETRY;
						ath_tx_rc_status(sc, bf, ts, nframes,
								nbad, 0, false);
						ath_tx_complete_buf(sc, bf, txq,
								    &bf_head,
								    ts, 0, 0);
						break;
					}

					ath9k_hw_cleartxdesc(sc->sc_ah,
							     tbf->bf_desc);
					list_add_tail(&tbf->list, &bf_head);
				} else {
					/*
					 * Clear descriptor status words for
					 * software retry
					 */
					ath9k_hw_cleartxdesc(sc->sc_ah,
							     bf->bf_desc);
				}
			}

			/*
			 * Put this buffer to the temporary pending
			 * queue to retain ordering
			 */
			list_splice_tail_init(&bf_head, &bf_pending);
		}

		bf = bf_next;
	}

	/* prepend un-acked frames to the beginning of the pending frame queue */
	if (!list_empty(&bf_pending)) {
		if (an->sleeping)
			ieee80211_sta_set_tim(sta);

		spin_lock_bh(&txq->axq_lock);
		if (clear_filter)
			tid->ac->clear_ps_filter = true;
		list_splice(&bf_pending, &tid->buf_q);
		ath_tx_queue_tid(txq, tid);
		spin_unlock_bh(&txq->axq_lock);
	}

	if (tid->state & AGGR_CLEANUP) {
		ath_tx_flush_tid(sc, tid);

		if (tid->baw_head == tid->baw_tail) {
			tid->state &= ~AGGR_ADDBA_COMPLETE;
			tid->state &= ~AGGR_CLEANUP;
		}
	}

	rcu_read_unlock();

	if (needreset) {
		spin_unlock_bh(&sc->sc_pcu_lock);
		ath_reset(sc, false);
		spin_lock_bh(&sc->sc_pcu_lock);
	}
}

static u32 ath_lookup_rate(struct ath_softc *sc, struct ath_buf *bf,
			   struct ath_atx_tid *tid)
{
	struct sk_buff *skb;
	struct ieee80211_tx_info *tx_info;
	struct ieee80211_tx_rate *rates;
	u32 max_4ms_framelen, frmlen;
	u16 aggr_limit, legacy = 0;
	int i;

	skb = bf->bf_mpdu;
	tx_info = IEEE80211_SKB_CB(skb);
	rates = tx_info->control.rates;

	/*
	 * Find the lowest frame length among the rate series that will have a
	 * 4ms transmit duration.
	 * TODO - TXOP limit needs to be considered.
	 */
	max_4ms_framelen = ATH_AMPDU_LIMIT_MAX;

	for (i = 0; i < 4; i++) {
		if (rates[i].count) {
			int modeidx;
			if (!(rates[i].flags & IEEE80211_TX_RC_MCS)) {
				legacy = 1;
				break;
			}

			if (rates[i].flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
				modeidx = MCS_HT40;
			else
				modeidx = MCS_HT20;

			if (rates[i].flags & IEEE80211_TX_RC_SHORT_GI)
				modeidx++;

			frmlen = ath_max_4ms_framelen[modeidx][rates[i].idx];
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

	if (sc->sc_flags & SC_OP_BT_PRIORITY_DETECTED)
		aggr_limit = min((max_4ms_framelen * 3) / 8,
				 (u32)ATH_AMPDU_LIMIT_MAX);
	else
		aggr_limit = min(max_4ms_framelen,
				 (u32)ATH_AMPDU_LIMIT_MAX);

	/*
	 * h/w can accept aggregates up to 16 bit lengths (65535).
	 * The IE, however can hold up to 65536, which shows up here
	 * as zero. Ignore 65536 since we  are constrained by hw.
	 */
	if (tid->an->maxampdu)
		aggr_limit = min(aggr_limit, tid->an->maxampdu);

	return aggr_limit;
}

/*
 * Returns the number of delimiters to be added to
 * meet the minimum required mpdudensity.
 */
static int ath_compute_num_delims(struct ath_softc *sc, struct ath_atx_tid *tid,
				  struct ath_buf *bf, u16 frmlen)
{
	struct sk_buff *skb = bf->bf_mpdu;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	u32 nsymbits, nsymbols;
	u16 minlen;
	u8 flags, rix;
	int width, streams, half_gi, ndelim, mindelim;
	struct ath_frame_info *fi = get_frame_info(bf->bf_mpdu);

	/* Select standard number of delimiters based on frame length alone */
	ndelim = ATH_AGGR_GET_NDELIM(frmlen);

	/*
	 * If encryption enabled, hardware requires some more padding between
	 * subframes.
	 * TODO - this could be improved to be dependent on the rate.
	 *      The hardware can keep up at lower rates, but not higher rates
	 */
	if (fi->keyix != ATH9K_TXKEYIX_INVALID)
		ndelim += ATH_AGGR_ENCRYPTDELIM;

	/*
	 * Convert desired mpdu density from microeconds to bytes based
	 * on highest rate in rate series (i.e. first rate) to determine
	 * required minimum length for subframe. Take into account
	 * whether high rate is 20 or 40Mhz and half or full GI.
	 *
	 * If there is no mpdu density restriction, no further calculation
	 * is needed.
	 */

	if (tid->an->mpdudensity == 0)
		return ndelim;

	rix = tx_info->control.rates[0].idx;
	flags = tx_info->control.rates[0].flags;
	width = (flags & IEEE80211_TX_RC_40_MHZ_WIDTH) ? 1 : 0;
	half_gi = (flags & IEEE80211_TX_RC_SHORT_GI) ? 1 : 0;

	if (half_gi)
		nsymbols = NUM_SYMBOLS_PER_USEC_HALFGI(tid->an->mpdudensity);
	else
		nsymbols = NUM_SYMBOLS_PER_USEC(tid->an->mpdudensity);

	if (nsymbols == 0)
		nsymbols = 1;

	streams = HT_RC_2_STREAMS(rix);
	nsymbits = bits_per_symbol[rix % 8][width] * streams;
	minlen = (nsymbols * nsymbits) / BITS_PER_BYTE;

	if (frmlen < minlen) {
		mindelim = (minlen - frmlen) / ATH_AGGR_DELIM_SZ;
		ndelim = max(mindelim, ndelim);
	}

	return ndelim;
}

static enum ATH_AGGR_STATUS ath_tx_form_aggr(struct ath_softc *sc,
					     struct ath_txq *txq,
					     struct ath_atx_tid *tid,
					     struct list_head *bf_q,
					     int *aggr_len)
{
#define PADBYTES(_len) ((4 - ((_len) % 4)) % 4)
	struct ath_buf *bf, *bf_first, *bf_prev = NULL;
	int rl = 0, nframes = 0, ndelim, prev_al = 0;
	u16 aggr_limit = 0, al = 0, bpad = 0,
		al_delta, h_baw = tid->baw_size / 2;
	enum ATH_AGGR_STATUS status = ATH_AGGR_DONE;
	struct ieee80211_tx_info *tx_info;
	struct ath_frame_info *fi;

	bf_first = list_first_entry(&tid->buf_q, struct ath_buf, list);

	do {
		bf = list_first_entry(&tid->buf_q, struct ath_buf, list);
		fi = get_frame_info(bf->bf_mpdu);

		/* do not step over block-ack window */
		if (!BAW_WITHIN(tid->seq_start, tid->baw_size, fi->seqno)) {
			status = ATH_AGGR_BAW_CLOSED;
			break;
		}

		if (!rl) {
			aggr_limit = ath_lookup_rate(sc, bf, tid);
			rl = 1;
		}

		/* do not exceed aggregation limit */
		al_delta = ATH_AGGR_DELIM_SZ + fi->framelen;

		if (nframes &&
		    (aggr_limit < (al + bpad + al_delta + prev_al))) {
			status = ATH_AGGR_LIMITED;
			break;
		}

		tx_info = IEEE80211_SKB_CB(bf->bf_mpdu);
		if (nframes && ((tx_info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE) ||
			!(tx_info->control.rates[0].flags & IEEE80211_TX_RC_MCS)))
			break;

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
		ndelim = ath_compute_num_delims(sc, tid, bf_first, fi->framelen);
		bpad = PADBYTES(al_delta) + (ndelim << 2);

		bf->bf_next = NULL;
		ath9k_hw_set_desc_link(sc->sc_ah, bf->bf_desc, 0);

		/* link buffers of this frame to the aggregate */
		if (!fi->retries)
			ath_tx_addto_baw(sc, tid, fi->seqno);
		ath9k_hw_set11n_aggr_middle(sc->sc_ah, bf->bf_desc, ndelim);
		list_move_tail(&bf->list, bf_q);
		if (bf_prev) {
			bf_prev->bf_next = bf;
			ath9k_hw_set_desc_link(sc->sc_ah, bf_prev->bf_desc,
					       bf->bf_daddr);
		}
		bf_prev = bf;

	} while (!list_empty(&tid->buf_q));

	*aggr_len = al;

	return status;
#undef PADBYTES
}

static void ath_tx_sched_aggr(struct ath_softc *sc, struct ath_txq *txq,
			      struct ath_atx_tid *tid)
{
	struct ath_buf *bf;
	enum ATH_AGGR_STATUS status;
	struct ath_frame_info *fi;
	struct list_head bf_q;
	int aggr_len;

	do {
		if (list_empty(&tid->buf_q))
			return;

		INIT_LIST_HEAD(&bf_q);

		status = ath_tx_form_aggr(sc, txq, tid, &bf_q, &aggr_len);

		/*
		 * no frames picked up to be aggregated;
		 * block-ack window is not open.
		 */
		if (list_empty(&bf_q))
			break;

		bf = list_first_entry(&bf_q, struct ath_buf, list);
		bf->bf_lastbf = list_entry(bf_q.prev, struct ath_buf, list);

		if (tid->ac->clear_ps_filter) {
			tid->ac->clear_ps_filter = false;
			ath9k_hw_set_clrdmask(sc->sc_ah, bf->bf_desc, true);
		}

		/* if only one frame, send as non-aggregate */
		if (bf == bf->bf_lastbf) {
			fi = get_frame_info(bf->bf_mpdu);

			bf->bf_state.bf_type &= ~BUF_AGGR;
			ath9k_hw_clr11n_aggr(sc->sc_ah, bf->bf_desc);
			ath_buf_set_rate(sc, bf, fi->framelen);
			ath_tx_txqaddbuf(sc, txq, &bf_q);
			continue;
		}

		/* setup first desc of aggregate */
		bf->bf_state.bf_type |= BUF_AGGR;
		ath_buf_set_rate(sc, bf, aggr_len);
		ath9k_hw_set11n_aggr_first(sc->sc_ah, bf->bf_desc, aggr_len);

		/* anchor last desc of aggregate */
		ath9k_hw_set11n_aggr_last(sc->sc_ah, bf->bf_lastbf->bf_desc);

		ath_tx_txqaddbuf(sc, txq, &bf_q);
		TX_STAT_INC(txq->axq_qnum, a_aggr);

	} while (txq->axq_ampdu_depth < ATH_AGGR_MIN_QDEPTH &&
		 status != ATH_AGGR_BAW_CLOSED);
}

int ath_tx_aggr_start(struct ath_softc *sc, struct ieee80211_sta *sta,
		      u16 tid, u16 *ssn)
{
	struct ath_atx_tid *txtid;
	struct ath_node *an;

	an = (struct ath_node *)sta->drv_priv;
	txtid = ATH_AN_2_TID(an, tid);

	if (txtid->state & (AGGR_CLEANUP | AGGR_ADDBA_COMPLETE))
		return -EAGAIN;

	txtid->state |= AGGR_ADDBA_PROGRESS;
	txtid->paused = true;
	*ssn = txtid->seq_start = txtid->seq_next;

	memset(txtid->tx_buf, 0, sizeof(txtid->tx_buf));
	txtid->baw_head = txtid->baw_tail = 0;

	return 0;
}

void ath_tx_aggr_stop(struct ath_softc *sc, struct ieee80211_sta *sta, u16 tid)
{
	struct ath_node *an = (struct ath_node *)sta->drv_priv;
	struct ath_atx_tid *txtid = ATH_AN_2_TID(an, tid);
	struct ath_txq *txq = txtid->ac->txq;

	if (txtid->state & AGGR_CLEANUP)
		return;

	if (!(txtid->state & AGGR_ADDBA_COMPLETE)) {
		txtid->state &= ~AGGR_ADDBA_PROGRESS;
		return;
	}

	spin_lock_bh(&txq->axq_lock);
	txtid->paused = true;

	/*
	 * If frames are still being transmitted for this TID, they will be
	 * cleaned up during tx completion. To prevent race conditions, this
	 * TID can only be reused after all in-progress subframes have been
	 * completed.
	 */
	if (txtid->baw_head != txtid->baw_tail)
		txtid->state |= AGGR_CLEANUP;
	else
		txtid->state &= ~AGGR_ADDBA_COMPLETE;
	spin_unlock_bh(&txq->axq_lock);

	ath_tx_flush_tid(sc, txtid);
}

bool ath_tx_aggr_sleep(struct ath_softc *sc, struct ath_node *an)
{
	struct ath_atx_tid *tid;
	struct ath_atx_ac *ac;
	struct ath_txq *txq;
	bool buffered = false;
	int tidno;

	for (tidno = 0, tid = &an->tid[tidno];
	     tidno < WME_NUM_TID; tidno++, tid++) {

		if (!tid->sched)
			continue;

		ac = tid->ac;
		txq = ac->txq;

		spin_lock_bh(&txq->axq_lock);

		if (!list_empty(&tid->buf_q))
			buffered = true;

		tid->sched = false;
		list_del(&tid->list);

		if (ac->sched) {
			ac->sched = false;
			list_del(&ac->list);
		}

		spin_unlock_bh(&txq->axq_lock);
	}

	return buffered;
}

void ath_tx_aggr_wakeup(struct ath_softc *sc, struct ath_node *an)
{
	struct ath_atx_tid *tid;
	struct ath_atx_ac *ac;
	struct ath_txq *txq;
	int tidno;

	for (tidno = 0, tid = &an->tid[tidno];
	     tidno < WME_NUM_TID; tidno++, tid++) {

		ac = tid->ac;
		txq = ac->txq;

		spin_lock_bh(&txq->axq_lock);
		ac->clear_ps_filter = true;

		if (!list_empty(&tid->buf_q) && !tid->paused) {
			ath_tx_queue_tid(txq, tid);
			ath_txq_schedule(sc, txq);
		}

		spin_unlock_bh(&txq->axq_lock);
	}
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
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_tx_queue_info qi;
	static const int subtype_txq_to_hwq[] = {
		[WME_AC_BE] = ATH_TXQ_AC_BE,
		[WME_AC_BK] = ATH_TXQ_AC_BK,
		[WME_AC_VI] = ATH_TXQ_AC_VI,
		[WME_AC_VO] = ATH_TXQ_AC_VO,
	};
	int axq_qnum, i;

	memset(&qi, 0, sizeof(qi));
	qi.tqi_subtype = subtype_txq_to_hwq[subtype];
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
	if (ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) {
		qi.tqi_qflags = TXQ_FLAG_TXOKINT_ENABLE |
				TXQ_FLAG_TXERRINT_ENABLE;
	} else {
		if (qtype == ATH9K_TX_QUEUE_UAPSD)
			qi.tqi_qflags = TXQ_FLAG_TXDESCINT_ENABLE;
		else
			qi.tqi_qflags = TXQ_FLAG_TXEOLINT_ENABLE |
					TXQ_FLAG_TXDESCINT_ENABLE;
	}
	axq_qnum = ath9k_hw_setuptxqueue(ah, qtype, &qi);
	if (axq_qnum == -1) {
		/*
		 * NB: don't print a message, this happens
		 * normally on parts with too few tx queues
		 */
		return NULL;
	}
	if (axq_qnum >= ARRAY_SIZE(sc->tx.txq)) {
		ath_err(common, "qnum %u out of range, max %zu!\n",
			axq_qnum, ARRAY_SIZE(sc->tx.txq));
		ath9k_hw_releasetxqueue(ah, axq_qnum);
		return NULL;
	}
	if (!ATH_TXQ_SETUP(sc, axq_qnum)) {
		struct ath_txq *txq = &sc->tx.txq[axq_qnum];

		txq->axq_qnum = axq_qnum;
		txq->mac80211_qnum = -1;
		txq->axq_link = NULL;
		INIT_LIST_HEAD(&txq->axq_q);
		INIT_LIST_HEAD(&txq->axq_acq);
		spin_lock_init(&txq->axq_lock);
		txq->axq_depth = 0;
		txq->axq_ampdu_depth = 0;
		txq->axq_tx_inprogress = false;
		sc->tx.txqsetup |= 1<<axq_qnum;

		txq->txq_headidx = txq->txq_tailidx = 0;
		for (i = 0; i < ATH_TXFIFO_DEPTH; i++)
			INIT_LIST_HEAD(&txq->txq_fifo[i]);
		INIT_LIST_HEAD(&txq->txq_fifo_pending);
	}
	return &sc->tx.txq[axq_qnum];
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

	BUG_ON(sc->tx.txq[qnum].axq_qnum != qnum);

	ath9k_hw_get_txq_props(ah, qnum, &qi);
	qi.tqi_aifs = qinfo->tqi_aifs;
	qi.tqi_cwmin = qinfo->tqi_cwmin;
	qi.tqi_cwmax = qinfo->tqi_cwmax;
	qi.tqi_burstTime = qinfo->tqi_burstTime;
	qi.tqi_readyTime = qinfo->tqi_readyTime;

	if (!ath9k_hw_set_txq_props(ah, qnum, &qi)) {
		ath_err(ath9k_hw_common(sc->sc_ah),
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
	struct ath_beacon_config *cur_conf = &sc->cur_beacon_conf;
	int qnum = sc->beacon.cabq->axq_qnum;

	ath9k_hw_get_txq_props(sc->sc_ah, qnum, &qi);
	/*
	 * Ensure the readytime % is within the bounds.
	 */
	if (sc->config.cabqReadytime < ATH9K_READY_TIME_LO_BOUND)
		sc->config.cabqReadytime = ATH9K_READY_TIME_LO_BOUND;
	else if (sc->config.cabqReadytime > ATH9K_READY_TIME_HI_BOUND)
		sc->config.cabqReadytime = ATH9K_READY_TIME_HI_BOUND;

	qi.tqi_readyTime = (cur_conf->beacon_interval *
			    sc->config.cabqReadytime) / 100;
	ath_txq_update(sc, qnum, &qi);

	return 0;
}

static bool bf_is_ampdu_not_probing(struct ath_buf *bf)
{
    struct ieee80211_tx_info *info = IEEE80211_SKB_CB(bf->bf_mpdu);
    return bf_isampdu(bf) && !(info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE);
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
	struct ath_tx_status ts;

	memset(&ts, 0, sizeof(ts));
	INIT_LIST_HEAD(&bf_head);

	for (;;) {
		spin_lock_bh(&txq->axq_lock);

		if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) {
			if (list_empty(&txq->txq_fifo[txq->txq_tailidx])) {
				txq->txq_headidx = txq->txq_tailidx = 0;
				spin_unlock_bh(&txq->axq_lock);
				break;
			} else {
				bf = list_first_entry(&txq->txq_fifo[txq->txq_tailidx],
						      struct ath_buf, list);
			}
		} else {
			if (list_empty(&txq->axq_q)) {
				txq->axq_link = NULL;
				spin_unlock_bh(&txq->axq_lock);
				break;
			}
			bf = list_first_entry(&txq->axq_q, struct ath_buf,
					      list);

			if (bf->bf_stale) {
				list_del(&bf->list);
				spin_unlock_bh(&txq->axq_lock);

				ath_tx_return_buffer(sc, bf);
				continue;
			}
		}

		lastbf = bf->bf_lastbf;

		if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) {
			list_cut_position(&bf_head,
					  &txq->txq_fifo[txq->txq_tailidx],
					  &lastbf->list);
			INCR(txq->txq_tailidx, ATH_TXFIFO_DEPTH);
		} else {
			/* remove ath_buf's of the same mpdu from txq */
			list_cut_position(&bf_head, &txq->axq_q, &lastbf->list);
		}

		txq->axq_depth--;
		if (bf_is_ampdu_not_probing(bf))
			txq->axq_ampdu_depth--;
		spin_unlock_bh(&txq->axq_lock);

		if (bf_isampdu(bf))
			ath_tx_complete_aggr(sc, txq, bf, &bf_head, &ts, 0,
					     retry_tx);
		else
			ath_tx_complete_buf(sc, bf, txq, &bf_head, &ts, 0, 0);
	}

	spin_lock_bh(&txq->axq_lock);
	txq->axq_tx_inprogress = false;
	spin_unlock_bh(&txq->axq_lock);

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) {
		spin_lock_bh(&txq->axq_lock);
		while (!list_empty(&txq->txq_fifo_pending)) {
			bf = list_first_entry(&txq->txq_fifo_pending,
					      struct ath_buf, list);
			list_cut_position(&bf_head,
					  &txq->txq_fifo_pending,
					  &bf->bf_lastbf->list);
			spin_unlock_bh(&txq->axq_lock);

			if (bf_isampdu(bf))
				ath_tx_complete_aggr(sc, txq, bf, &bf_head,
						     &ts, 0, retry_tx);
			else
				ath_tx_complete_buf(sc, bf, txq, &bf_head,
						    &ts, 0, 0);
			spin_lock_bh(&txq->axq_lock);
		}
		spin_unlock_bh(&txq->axq_lock);
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

bool ath_drain_all_txq(struct ath_softc *sc, bool retry_tx)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_txq *txq;
	int i, npend = 0;

	if (sc->sc_flags & SC_OP_INVALID)
		return true;

	ath9k_hw_abort_tx_dma(ah);

	/* Check if any queue remains active */
	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++) {
		if (!ATH_TXQ_SETUP(sc, i))
			continue;

		npend += ath9k_hw_numtxpending(ah, sc->tx.txq[i].axq_qnum);
	}

	if (npend)
		ath_err(common, "Failed to stop TX DMA!\n");

	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++) {
		if (!ATH_TXQ_SETUP(sc, i))
			continue;

		/*
		 * The caller will resume queues with ieee80211_wake_queues.
		 * Mark the queue as not stopped to prevent ath_tx_complete
		 * from waking the queue too early.
		 */
		txq = &sc->tx.txq[i];
		txq->stopped = false;
		ath_draintxq(sc, txq, retry_tx);
	}

	return !npend;
}

void ath_tx_cleanupq(struct ath_softc *sc, struct ath_txq *txq)
{
	ath9k_hw_releasetxqueue(sc->sc_ah, txq->axq_qnum);
	sc->tx.txqsetup &= ~(1<<txq->axq_qnum);
}

/* For each axq_acq entry, for each tid, try to schedule packets
 * for transmit until ampdu_depth has reached min Q depth.
 */
void ath_txq_schedule(struct ath_softc *sc, struct ath_txq *txq)
{
	struct ath_atx_ac *ac, *ac_tmp, *last_ac;
	struct ath_atx_tid *tid, *last_tid;

	if (list_empty(&txq->axq_acq) ||
	    txq->axq_ampdu_depth >= ATH_AGGR_MIN_QDEPTH)
		return;

	ac = list_first_entry(&txq->axq_acq, struct ath_atx_ac, list);
	last_ac = list_entry(txq->axq_acq.prev, struct ath_atx_ac, list);

	list_for_each_entry_safe(ac, ac_tmp, &txq->axq_acq, list) {
		last_tid = list_entry(ac->tid_q.prev, struct ath_atx_tid, list);
		list_del(&ac->list);
		ac->sched = false;

		while (!list_empty(&ac->tid_q)) {
			tid = list_first_entry(&ac->tid_q, struct ath_atx_tid,
					       list);
			list_del(&tid->list);
			tid->sched = false;

			if (tid->paused)
				continue;

			ath_tx_sched_aggr(sc, txq, tid);

			/*
			 * add tid to round-robin queue if more frames
			 * are pending for the tid
			 */
			if (!list_empty(&tid->buf_q))
				ath_tx_queue_tid(txq, tid);

			if (tid == last_tid ||
			    txq->axq_ampdu_depth >= ATH_AGGR_MIN_QDEPTH)
				break;
		}

		if (!list_empty(&ac->tid_q)) {
			if (!ac->sched) {
				ac->sched = true;
				list_add_tail(&ac->list, &txq->axq_acq);
			}
		}

		if (ac == last_ac ||
		    txq->axq_ampdu_depth >= ATH_AGGR_MIN_QDEPTH)
			return;
	}
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
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_buf *bf;

	/*
	 * Insert the frame on the outbound list and
	 * pass it on to the hardware.
	 */

	if (list_empty(head))
		return;

	bf = list_first_entry(head, struct ath_buf, list);

	ath_dbg(common, ATH_DBG_QUEUE,
		"qnum: %d, txq depth: %d\n", txq->axq_qnum, txq->axq_depth);

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) {
		if (txq->axq_depth >= ATH_TXFIFO_DEPTH) {
			list_splice_tail_init(head, &txq->txq_fifo_pending);
			return;
		}
		if (!list_empty(&txq->txq_fifo[txq->txq_headidx]))
			ath_dbg(common, ATH_DBG_XMIT,
				"Initializing tx fifo %d which is non-empty\n",
				txq->txq_headidx);
		INIT_LIST_HEAD(&txq->txq_fifo[txq->txq_headidx]);
		list_splice_init(head, &txq->txq_fifo[txq->txq_headidx]);
		INCR(txq->txq_headidx, ATH_TXFIFO_DEPTH);
		TX_STAT_INC(txq->axq_qnum, puttxbuf);
		ath9k_hw_puttxbuf(ah, txq->axq_qnum, bf->bf_daddr);
		ath_dbg(common, ATH_DBG_XMIT, "TXDP[%u] = %llx (%p)\n",
			txq->axq_qnum, ito64(bf->bf_daddr), bf->bf_desc);
	} else {
		list_splice_tail_init(head, &txq->axq_q);

		if (txq->axq_link == NULL) {
			TX_STAT_INC(txq->axq_qnum, puttxbuf);
			ath9k_hw_puttxbuf(ah, txq->axq_qnum, bf->bf_daddr);
			ath_dbg(common, ATH_DBG_XMIT, "TXDP[%u] = %llx (%p)\n",
				txq->axq_qnum, ito64(bf->bf_daddr),
				bf->bf_desc);
		} else {
			*txq->axq_link = bf->bf_daddr;
			ath_dbg(common, ATH_DBG_XMIT,
				"link[%u] (%p)=%llx (%p)\n",
				txq->axq_qnum, txq->axq_link,
				ito64(bf->bf_daddr), bf->bf_desc);
		}
		ath9k_hw_get_desc_link(ah, bf->bf_lastbf->bf_desc,
				       &txq->axq_link);
		TX_STAT_INC(txq->axq_qnum, txstart);
		ath9k_hw_txstart(ah, txq->axq_qnum);
	}
	txq->axq_depth++;
	if (bf_is_ampdu_not_probing(bf))
		txq->axq_ampdu_depth++;
}

static void ath_tx_send_ampdu(struct ath_softc *sc, struct ath_atx_tid *tid,
			      struct ath_buf *bf, struct ath_tx_control *txctl)
{
	struct ath_frame_info *fi = get_frame_info(bf->bf_mpdu);
	struct list_head bf_head;

	bf->bf_state.bf_type |= BUF_AMPDU;

	/*
	 * Do not queue to h/w when any of the following conditions is true:
	 * - there are pending frames in software queue
	 * - the TID is currently paused for ADDBA/BAR request
	 * - seqno is not within block-ack window
	 * - h/w queue depth exceeds low water mark
	 */
	if (!list_empty(&tid->buf_q) || tid->paused ||
	    !BAW_WITHIN(tid->seq_start, tid->baw_size, fi->seqno) ||
	    txctl->txq->axq_ampdu_depth >= ATH_AGGR_MIN_QDEPTH) {
		/*
		 * Add this frame to software queue for scheduling later
		 * for aggregation.
		 */
		TX_STAT_INC(txctl->txq->axq_qnum, a_queued_sw);
		list_add_tail(&bf->list, &tid->buf_q);
		ath_tx_queue_tid(txctl->txq, tid);
		return;
	}

	INIT_LIST_HEAD(&bf_head);
	list_add(&bf->list, &bf_head);

	/* Add sub-frame to BAW */
	if (!fi->retries)
		ath_tx_addto_baw(sc, tid, fi->seqno);

	/* Queue to h/w without aggregation */
	TX_STAT_INC(txctl->txq->axq_qnum, a_queued_hw);
	bf->bf_lastbf = bf;
	ath_buf_set_rate(sc, bf, fi->framelen);
	ath_tx_txqaddbuf(sc, txctl->txq, &bf_head);
}

static void ath_tx_send_normal(struct ath_softc *sc, struct ath_txq *txq,
			       struct ath_atx_tid *tid,
			       struct list_head *bf_head)
{
	struct ath_frame_info *fi;
	struct ath_buf *bf;

	bf = list_first_entry(bf_head, struct ath_buf, list);
	bf->bf_state.bf_type &= ~BUF_AMPDU;

	/* update starting sequence number for subsequent ADDBA request */
	if (tid)
		INCR(tid->seq_start, IEEE80211_SEQ_MAX);

	bf->bf_lastbf = bf;
	fi = get_frame_info(bf->bf_mpdu);
	ath_buf_set_rate(sc, bf, fi->framelen);
	ath_tx_txqaddbuf(sc, txq, bf_head);
	TX_STAT_INC(txq->axq_qnum, queued);
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

static void setup_frame_info(struct ieee80211_hw *hw, struct sk_buff *skb,
			     int framelen)
{
	struct ath_softc *sc = hw->priv;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_sta *sta = tx_info->control.sta;
	struct ieee80211_key_conf *hw_key = tx_info->control.hw_key;
	struct ieee80211_hdr *hdr;
	struct ath_frame_info *fi = get_frame_info(skb);
	struct ath_node *an = NULL;
	struct ath_atx_tid *tid;
	enum ath9k_key_type keytype;
	u16 seqno = 0;
	u8 tidno;

	keytype = ath9k_cmn_get_hw_crypto_keytype(skb);

	if (sta)
		an = (struct ath_node *) sta->drv_priv;

	hdr = (struct ieee80211_hdr *)skb->data;
	if (an && ieee80211_is_data_qos(hdr->frame_control) &&
		conf_is_ht(&hw->conf) && (sc->sc_flags & SC_OP_TXAGGR)) {

		tidno = ieee80211_get_qos_ctl(hdr)[0] & IEEE80211_QOS_CTL_TID_MASK;

		/*
		 * Override seqno set by upper layer with the one
		 * in tx aggregation state.
		 */
		tid = ATH_AN_2_TID(an, tidno);
		seqno = tid->seq_next;
		hdr->seq_ctrl = cpu_to_le16(seqno << IEEE80211_SEQ_SEQ_SHIFT);
		INCR(tid->seq_next, IEEE80211_SEQ_MAX);
	}

	memset(fi, 0, sizeof(*fi));
	if (hw_key)
		fi->keyix = hw_key->hw_key_idx;
	else if (an && ieee80211_is_data(hdr->frame_control) && an->ps_key > 0)
		fi->keyix = an->ps_key;
	else
		fi->keyix = ATH9K_TXKEYIX_INVALID;
	fi->keytype = keytype;
	fi->framelen = framelen;
	fi->seqno = seqno;
}

static int setup_tx_flags(struct sk_buff *skb)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	int flags = 0;

	flags |= ATH9K_TXDESC_INTREQ;

	if (tx_info->flags & IEEE80211_TX_CTL_NO_ACK)
		flags |= ATH9K_TXDESC_NOACK;

	if (tx_info->flags & IEEE80211_TX_CTL_LDPC)
		flags |= ATH9K_TXDESC_LDPC;

	return flags;
}

/*
 * rix - rate index
 * pktlen - total bytes (delims + data + fcs + pads + pad delims)
 * width  - 0 for 20 MHz, 1 for 40 MHz
 * half_gi - to use 4us v/s 3.6 us for symbol time
 */
static u32 ath_pkt_duration(struct ath_softc *sc, u8 rix, int pktlen,
			    int width, int half_gi, bool shortPreamble)
{
	u32 nbits, nsymbits, duration, nsymbols;
	int streams;

	/* find number of symbols: PLCP + data */
	streams = HT_RC_2_STREAMS(rix);
	nbits = (pktlen << 3) + OFDM_PLCP_BITS;
	nsymbits = bits_per_symbol[rix % 8][width] * streams;
	nsymbols = (nbits + nsymbits - 1) / nsymbits;

	if (!half_gi)
		duration = SYMBOL_TIME(nsymbols);
	else
		duration = SYMBOL_TIME_HALFGI(nsymbols);

	/* addup duration for legacy/ht training and signal fields */
	duration += L_STF + L_LTF + L_SIG + HT_SIG + HT_STF + HT_LTF(streams);

	return duration;
}

u8 ath_txchainmask_reduction(struct ath_softc *sc, u8 chainmask, u32 rate)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath9k_channel *curchan = ah->curchan;
	if ((sc->sc_flags & SC_OP_ENABLE_APM) &&
			(curchan->channelFlags & CHANNEL_5GHZ) &&
			(chainmask == 0x7) && (rate < 0x90))
		return 0x3;
	else
		return chainmask;
}

static void ath_buf_set_rate(struct ath_softc *sc, struct ath_buf *bf, int len)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath9k_11n_rate_series series[4];
	struct sk_buff *skb;
	struct ieee80211_tx_info *tx_info;
	struct ieee80211_tx_rate *rates;
	const struct ieee80211_rate *rate;
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
	rate = ieee80211_get_rts_cts_rate(sc->hw, tx_info);
	ctsrate = rate->hw_value;
	if (sc->sc_flags & SC_OP_PREAMBLE_SHORT)
		ctsrate |= rate->hw_value_short;

	for (i = 0; i < 4; i++) {
		bool is_40, is_sgi, is_sp;
		int phy;

		if (!rates[i].count || (rates[i].idx < 0))
			continue;

		rix = rates[i].idx;
		series[i].Tries = rates[i].count;

		    if (rates[i].flags & IEEE80211_TX_RC_USE_RTS_CTS) {
			series[i].RateFlags |= ATH9K_RATESERIES_RTS_CTS;
			flags |= ATH9K_TXDESC_RTSENA;
		} else if (rates[i].flags & IEEE80211_TX_RC_USE_CTS_PROTECT) {
			series[i].RateFlags |= ATH9K_RATESERIES_RTS_CTS;
			flags |= ATH9K_TXDESC_CTSENA;
		}

		if (rates[i].flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			series[i].RateFlags |= ATH9K_RATESERIES_2040;
		if (rates[i].flags & IEEE80211_TX_RC_SHORT_GI)
			series[i].RateFlags |= ATH9K_RATESERIES_HALFGI;

		is_sgi = !!(rates[i].flags & IEEE80211_TX_RC_SHORT_GI);
		is_40 = !!(rates[i].flags & IEEE80211_TX_RC_40_MHZ_WIDTH);
		is_sp = !!(rates[i].flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE);

		if (rates[i].flags & IEEE80211_TX_RC_MCS) {
			/* MCS rates */
			series[i].Rate = rix | 0x80;
			series[i].ChSel = ath_txchainmask_reduction(sc,
					common->tx_chainmask, series[i].Rate);
			series[i].PktDuration = ath_pkt_duration(sc, rix, len,
				 is_40, is_sgi, is_sp);
			if (rix < 8 && (tx_info->flags & IEEE80211_TX_CTL_STBC))
				series[i].RateFlags |= ATH9K_RATESERIES_STBC;
			continue;
		}

		/* legacy rates */
		if ((tx_info->band == IEEE80211_BAND_2GHZ) &&
		    !(rate->flags & IEEE80211_RATE_ERP_G))
			phy = WLAN_RC_PHY_CCK;
		else
			phy = WLAN_RC_PHY_OFDM;

		rate = &sc->sbands[tx_info->band].bitrates[rates[i].idx];
		series[i].Rate = rate->hw_value;
		if (rate->hw_value_short) {
			if (rates[i].flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
				series[i].Rate |= rate->hw_value_short;
		} else {
			is_sp = false;
		}

		if (bf->bf_state.bfs_paprd)
			series[i].ChSel = common->tx_chainmask;
		else
			series[i].ChSel = ath_txchainmask_reduction(sc,
					common->tx_chainmask, series[i].Rate);

		series[i].PktDuration = ath9k_hw_computetxtime(sc->sc_ah,
			phy, rate->bitrate * 100, len, rix, is_sp);
	}

	/* For AR5416 - RTS cannot be followed by a frame larger than 8K */
	if (bf_isaggr(bf) && (len > sc->sc_ah->caps.rts_aggr_limit))
		flags &= ~ATH9K_TXDESC_RTSENA;

	/* ATH9K_TXDESC_RTSENA and ATH9K_TXDESC_CTSENA are mutually exclusive. */
	if (flags & ATH9K_TXDESC_RTSENA)
		flags &= ~ATH9K_TXDESC_CTSENA;

	/* set dur_update_en for l-sig computation except for PS-Poll frames */
	ath9k_hw_set11n_ratescenario(sc->sc_ah, bf->bf_desc,
				     bf->bf_lastbf->bf_desc,
				     !is_pspoll, ctsrate,
				     0, series, 4, flags);

}

static struct ath_buf *ath_tx_setup_buffer(struct ieee80211_hw *hw,
					   struct ath_txq *txq,
					   struct sk_buff *skb)
{
	struct ath_softc *sc = hw->priv;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_frame_info *fi = get_frame_info(skb);
	struct ath_buf *bf;
	struct ath_desc *ds;
	int frm_type;

	bf = ath_tx_get_buffer(sc);
	if (!bf) {
		ath_dbg(common, ATH_DBG_XMIT, "TX buffers are full\n");
		return NULL;
	}

	ATH_TXBUF_RESET(bf);

	bf->bf_flags = setup_tx_flags(skb);
	bf->bf_mpdu = skb;

	bf->bf_buf_addr = dma_map_single(sc->dev, skb->data,
					 skb->len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(sc->dev, bf->bf_buf_addr))) {
		bf->bf_mpdu = NULL;
		bf->bf_buf_addr = 0;
		ath_err(ath9k_hw_common(sc->sc_ah),
			"dma_mapping_error() on TX\n");
		ath_tx_return_buffer(sc, bf);
		return NULL;
	}

	frm_type = get_hw_packet_type(skb);

	ds = bf->bf_desc;
	ath9k_hw_set_desc_link(ah, ds, 0);

	ath9k_hw_set11n_txdesc(ah, ds, fi->framelen, frm_type, MAX_RATE_POWER,
			       fi->keyix, fi->keytype, bf->bf_flags);

	ath9k_hw_filltxdesc(ah, ds,
			    skb->len,	/* segment length */
			    true,	/* first segment */
			    true,	/* last segment */
			    ds,		/* first descriptor */
			    bf->bf_buf_addr,
			    txq->axq_qnum);


	return bf;
}

/* FIXME: tx power */
static void ath_tx_start_dma(struct ath_softc *sc, struct ath_buf *bf,
			     struct ath_tx_control *txctl)
{
	struct sk_buff *skb = bf->bf_mpdu;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct list_head bf_head;
	struct ath_atx_tid *tid = NULL;
	u8 tidno;

	spin_lock_bh(&txctl->txq->axq_lock);
	if ((sc->sc_flags & SC_OP_TXAGGR) && txctl->an &&
		ieee80211_is_data_qos(hdr->frame_control)) {
		tidno = ieee80211_get_qos_ctl(hdr)[0] &
			IEEE80211_QOS_CTL_TID_MASK;
		tid = ATH_AN_2_TID(txctl->an, tidno);

		WARN_ON(tid->ac->txq != txctl->txq);
	}

	if ((tx_info->flags & IEEE80211_TX_CTL_AMPDU) && tid) {
		/*
		 * Try aggregation if it's a unicast data frame
		 * and the destination is HT capable.
		 */
		ath_tx_send_ampdu(sc, tid, bf, txctl);
	} else {
		INIT_LIST_HEAD(&bf_head);
		list_add_tail(&bf->list, &bf_head);

		bf->bf_state.bfs_ftype = txctl->frame_type;
		bf->bf_state.bfs_paprd = txctl->paprd;

		if (bf->bf_state.bfs_paprd)
			ar9003_hw_set_paprd_txdesc(sc->sc_ah, bf->bf_desc,
						   bf->bf_state.bfs_paprd);

		if (txctl->paprd)
			bf->bf_state.bfs_paprd_timestamp = jiffies;

		if (tx_info->flags & IEEE80211_TX_CTL_CLEAR_PS_FILT)
			ath9k_hw_set_clrdmask(sc->sc_ah, bf->bf_desc, true);

		ath_tx_send_normal(sc, txctl->txq, tid, &bf_head);
	}

	spin_unlock_bh(&txctl->txq->axq_lock);
}

/* Upon failure caller should free skb */
int ath_tx_start(struct ieee80211_hw *hw, struct sk_buff *skb,
		 struct ath_tx_control *txctl)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_sta *sta = info->control.sta;
	struct ieee80211_vif *vif = info->control.vif;
	struct ath_softc *sc = hw->priv;
	struct ath_txq *txq = txctl->txq;
	struct ath_buf *bf;
	int padpos, padsize;
	int frmlen = skb->len + FCS_LEN;
	int q;

	/* NOTE:  sta can be NULL according to net/mac80211.h */
	if (sta)
		txctl->an = (struct ath_node *)sta->drv_priv;

	if (info->control.hw_key)
		frmlen += info->control.hw_key->icv_len;

	/*
	 * As a temporary workaround, assign seq# here; this will likely need
	 * to be cleaned up to work better with Beacon transmission and virtual
	 * BSSes.
	 */
	if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ) {
		if (info->flags & IEEE80211_TX_CTL_FIRST_FRAGMENT)
			sc->tx.seq_no += 0x10;
		hdr->seq_ctrl &= cpu_to_le16(IEEE80211_SCTL_FRAG);
		hdr->seq_ctrl |= cpu_to_le16(sc->tx.seq_no);
	}

	/* Add the padding after the header if this is not already done */
	padpos = ath9k_cmn_padpos(hdr->frame_control);
	padsize = padpos & 3;
	if (padsize && skb->len > padpos) {
		if (skb_headroom(skb) < padsize)
			return -ENOMEM;

		skb_push(skb, padsize);
		memmove(skb->data, skb->data + padsize, padpos);
	}

	if ((vif && vif->type != NL80211_IFTYPE_AP &&
	            vif->type != NL80211_IFTYPE_AP_VLAN) ||
	    !ieee80211_is_data(hdr->frame_control))
		info->flags |= IEEE80211_TX_CTL_CLEAR_PS_FILT;

	setup_frame_info(hw, skb, frmlen);

	/*
	 * At this point, the vif, hw_key and sta pointers in the tx control
	 * info are no longer valid (overwritten by the ath_frame_info data.
	 */

	bf = ath_tx_setup_buffer(hw, txctl->txq, skb);
	if (unlikely(!bf))
		return -ENOMEM;

	q = skb_get_queue_mapping(skb);
	spin_lock_bh(&txq->axq_lock);
	if (txq == sc->tx.txq_map[q] &&
	    ++txq->pending_frames > ATH_MAX_QDEPTH && !txq->stopped) {
		ieee80211_stop_queue(sc->hw, q);
		txq->stopped = 1;
	}
	spin_unlock_bh(&txq->axq_lock);

	ath_tx_start_dma(sc, bf, txctl);

	return 0;
}

/*****************/
/* TX Completion */
/*****************/

static void ath_tx_complete(struct ath_softc *sc, struct sk_buff *skb,
			    int tx_flags, int ftype, struct ath_txq *txq)
{
	struct ieee80211_hw *hw = sc->hw;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ieee80211_hdr * hdr = (struct ieee80211_hdr *)skb->data;
	int q, padpos, padsize;

	ath_dbg(common, ATH_DBG_XMIT, "TX complete: skb: %p\n", skb);

	if (tx_flags & ATH_TX_BAR)
		tx_info->flags |= IEEE80211_TX_STAT_AMPDU_NO_BACK;

	if (!(tx_flags & (ATH_TX_ERROR | ATH_TX_XRETRY))) {
		/* Frame was ACKed */
		tx_info->flags |= IEEE80211_TX_STAT_ACK;
	}

	padpos = ath9k_cmn_padpos(hdr->frame_control);
	padsize = padpos & 3;
	if (padsize && skb->len>padpos+padsize) {
		/*
		 * Remove MAC header padding before giving the frame back to
		 * mac80211.
		 */
		memmove(skb->data + padsize, skb->data, padpos);
		skb_pull(skb, padsize);
	}

	if (sc->ps_flags & PS_WAIT_FOR_TX_ACK) {
		sc->ps_flags &= ~PS_WAIT_FOR_TX_ACK;
		ath_dbg(common, ATH_DBG_PS,
			"Going back to sleep after having received TX status (0x%lx)\n",
			sc->ps_flags & (PS_WAIT_FOR_BEACON |
					PS_WAIT_FOR_CAB |
					PS_WAIT_FOR_PSPOLL_DATA |
					PS_WAIT_FOR_TX_ACK));
	}

	q = skb_get_queue_mapping(skb);
	if (txq == sc->tx.txq_map[q]) {
		spin_lock_bh(&txq->axq_lock);
		if (WARN_ON(--txq->pending_frames < 0))
			txq->pending_frames = 0;

		if (txq->stopped && txq->pending_frames < ATH_MAX_QDEPTH) {
			ieee80211_wake_queue(sc->hw, q);
			txq->stopped = 0;
		}
		spin_unlock_bh(&txq->axq_lock);
	}

	ieee80211_tx_status(hw, skb);
}

static void ath_tx_complete_buf(struct ath_softc *sc, struct ath_buf *bf,
				struct ath_txq *txq, struct list_head *bf_q,
				struct ath_tx_status *ts, int txok, int sendbar)
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

	dma_unmap_single(sc->dev, bf->bf_buf_addr, skb->len, DMA_TO_DEVICE);
	bf->bf_buf_addr = 0;

	if (bf->bf_state.bfs_paprd) {
		if (time_after(jiffies,
				bf->bf_state.bfs_paprd_timestamp +
				msecs_to_jiffies(ATH_PAPRD_TIMEOUT)))
			dev_kfree_skb_any(skb);
		else
			complete(&sc->paprd_complete);
	} else {
		ath_debug_stat_tx(sc, bf, ts, txq);
		ath_tx_complete(sc, skb, tx_flags,
				bf->bf_state.bfs_ftype, txq);
	}
	/* At this point, skb (bf->bf_mpdu) is consumed...make sure we don't
	 * accidentally reference it later.
	 */
	bf->bf_mpdu = NULL;

	/*
	 * Return the list of ath_buf of this mpdu to free queue
	 */
	spin_lock_irqsave(&sc->tx.txbuflock, flags);
	list_splice_tail_init(bf_q, &sc->tx.txbuf);
	spin_unlock_irqrestore(&sc->tx.txbuflock, flags);
}

static void ath_tx_rc_status(struct ath_softc *sc, struct ath_buf *bf,
			     struct ath_tx_status *ts, int nframes, int nbad,
			     int txok, bool update_rc)
{
	struct sk_buff *skb = bf->bf_mpdu;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hw *hw = sc->hw;
	struct ath_hw *ah = sc->sc_ah;
	u8 i, tx_rateindex;

	if (txok)
		tx_info->status.ack_signal = ts->ts_rssi;

	tx_rateindex = ts->ts_rateindex;
	WARN_ON(tx_rateindex >= hw->max_rates);

	if (ts->ts_status & ATH9K_TXERR_FILT)
		tx_info->flags |= IEEE80211_TX_STAT_TX_FILTERED;
	if ((tx_info->flags & IEEE80211_TX_CTL_AMPDU) && update_rc) {
		tx_info->flags |= IEEE80211_TX_STAT_AMPDU;

		BUG_ON(nbad > nframes);

		tx_info->status.ampdu_len = nframes;
		tx_info->status.ampdu_ack_len = nframes - nbad;
	}

	if ((ts->ts_status & ATH9K_TXERR_FILT) == 0 &&
	    (bf->bf_flags & ATH9K_TXDESC_NOACK) == 0 && update_rc) {
		/*
		 * If an underrun error is seen assume it as an excessive
		 * retry only if max frame trigger level has been reached
		 * (2 KB for single stream, and 4 KB for dual stream).
		 * Adjust the long retry as if the frame was tried
		 * hw->max_rate_tries times to affect how rate control updates
		 * PER for the failed rate.
		 * In case of congestion on the bus penalizing this type of
		 * underruns should help hardware actually transmit new frames
		 * successfully by eventually preferring slower rates.
		 * This itself should also alleviate congestion on the bus.
		 */
		if (ieee80211_is_data(hdr->frame_control) &&
		    (ts->ts_flags & (ATH9K_TX_DATA_UNDERRUN |
		                     ATH9K_TX_DELIM_UNDERRUN)) &&
		    ah->tx_trig_level >= sc->sc_ah->config.max_txtrig_level)
			tx_info->status.rates[tx_rateindex].count =
				hw->max_rate_tries;
	}

	for (i = tx_rateindex + 1; i < hw->max_rates; i++) {
		tx_info->status.rates[i].count = 0;
		tx_info->status.rates[i].idx = -1;
	}

	tx_info->status.rates[tx_rateindex].count = ts->ts_longretry + 1;
}

static void ath_tx_processq(struct ath_softc *sc, struct ath_txq *txq)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_buf *bf, *lastbf, *bf_held = NULL;
	struct list_head bf_head;
	struct ath_desc *ds;
	struct ath_tx_status ts;
	int txok;
	int status;

	ath_dbg(common, ATH_DBG_QUEUE, "tx queue %d (%x), link %p\n",
		txq->axq_qnum, ath9k_hw_gettxbuf(sc->sc_ah, txq->axq_qnum),
		txq->axq_link);

	for (;;) {
		spin_lock_bh(&txq->axq_lock);
		if (list_empty(&txq->axq_q)) {
			txq->axq_link = NULL;
			if (sc->sc_flags & SC_OP_TXAGGR)
				ath_txq_schedule(sc, txq);
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
				spin_unlock_bh(&txq->axq_lock);
				break;
			} else {
				bf = list_entry(bf_held->list.next,
						struct ath_buf, list);
			}
		}

		lastbf = bf->bf_lastbf;
		ds = lastbf->bf_desc;

		memset(&ts, 0, sizeof(ts));
		status = ath9k_hw_txprocdesc(ah, ds, &ts);
		if (status == -EINPROGRESS) {
			spin_unlock_bh(&txq->axq_lock);
			break;
		}
		TX_STAT_INC(txq->axq_qnum, txprocdesc);

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
		txok = !(ts.ts_status & ATH9K_TXERR_MASK);
		txq->axq_tx_inprogress = false;
		if (bf_held)
			list_del(&bf_held->list);

		if (bf_is_ampdu_not_probing(bf))
			txq->axq_ampdu_depth--;

		spin_unlock_bh(&txq->axq_lock);

		if (bf_held)
			ath_tx_return_buffer(sc, bf_held);

		if (!bf_isampdu(bf)) {
			/*
			 * This frame is sent out as a single frame.
			 * Use hardware retry status for this frame.
			 */
			if (ts.ts_status & ATH9K_TXERR_XRETRY)
				bf->bf_state.bf_type |= BUF_XRETRY;
			ath_tx_rc_status(sc, bf, &ts, 1, txok ? 0 : 1, txok, true);
		}

		if (bf_isampdu(bf))
			ath_tx_complete_aggr(sc, txq, bf, &bf_head, &ts, txok,
					     true);
		else
			ath_tx_complete_buf(sc, bf, txq, &bf_head, &ts, txok, 0);

		spin_lock_bh(&txq->axq_lock);

		if (sc->sc_flags & SC_OP_TXAGGR)
			ath_txq_schedule(sc, txq);
		spin_unlock_bh(&txq->axq_lock);
	}
}

static void ath_tx_complete_poll_work(struct work_struct *work)
{
	struct ath_softc *sc = container_of(work, struct ath_softc,
			tx_complete_work.work);
	struct ath_txq *txq;
	int i;
	bool needreset = false;
#ifdef CONFIG_ATH9K_DEBUGFS
	sc->tx_complete_poll_work_seen++;
#endif

	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++)
		if (ATH_TXQ_SETUP(sc, i)) {
			txq = &sc->tx.txq[i];
			spin_lock_bh(&txq->axq_lock);
			if (txq->axq_depth) {
				if (txq->axq_tx_inprogress) {
					needreset = true;
					spin_unlock_bh(&txq->axq_lock);
					break;
				} else {
					txq->axq_tx_inprogress = true;
				}
			}
			spin_unlock_bh(&txq->axq_lock);
		}

	if (needreset) {
		ath_dbg(ath9k_hw_common(sc->sc_ah), ATH_DBG_RESET,
			"tx hung, resetting the chip\n");
		ath_reset(sc, true);
	}

	ieee80211_queue_delayed_work(sc->hw, &sc->tx_complete_work,
			msecs_to_jiffies(ATH_TX_COMPLETE_POLL_INT));
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

void ath_tx_edma_tasklet(struct ath_softc *sc)
{
	struct ath_tx_status txs;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_hw *ah = sc->sc_ah;
	struct ath_txq *txq;
	struct ath_buf *bf, *lastbf;
	struct list_head bf_head;
	int status;
	int txok;

	for (;;) {
		status = ath9k_hw_txprocdesc(ah, NULL, (void *)&txs);
		if (status == -EINPROGRESS)
			break;
		if (status == -EIO) {
			ath_dbg(common, ATH_DBG_XMIT,
				"Error processing tx status\n");
			break;
		}

		/* Skip beacon completions */
		if (txs.qid == sc->beacon.beaconq)
			continue;

		txq = &sc->tx.txq[txs.qid];

		spin_lock_bh(&txq->axq_lock);
		if (list_empty(&txq->txq_fifo[txq->txq_tailidx])) {
			spin_unlock_bh(&txq->axq_lock);
			return;
		}

		bf = list_first_entry(&txq->txq_fifo[txq->txq_tailidx],
				      struct ath_buf, list);
		lastbf = bf->bf_lastbf;

		INIT_LIST_HEAD(&bf_head);
		list_cut_position(&bf_head, &txq->txq_fifo[txq->txq_tailidx],
				  &lastbf->list);
		INCR(txq->txq_tailidx, ATH_TXFIFO_DEPTH);
		txq->axq_depth--;
		txq->axq_tx_inprogress = false;
		if (bf_is_ampdu_not_probing(bf))
			txq->axq_ampdu_depth--;
		spin_unlock_bh(&txq->axq_lock);

		txok = !(txs.ts_status & ATH9K_TXERR_MASK);

		if (!bf_isampdu(bf)) {
			if (txs.ts_status & ATH9K_TXERR_XRETRY)
				bf->bf_state.bf_type |= BUF_XRETRY;
			ath_tx_rc_status(sc, bf, &txs, 1, txok ? 0 : 1, txok, true);
		}

		if (bf_isampdu(bf))
			ath_tx_complete_aggr(sc, txq, bf, &bf_head, &txs,
					     txok, true);
		else
			ath_tx_complete_buf(sc, bf, txq, &bf_head,
					    &txs, txok, 0);

		spin_lock_bh(&txq->axq_lock);

		if (!list_empty(&txq->txq_fifo_pending)) {
			INIT_LIST_HEAD(&bf_head);
			bf = list_first_entry(&txq->txq_fifo_pending,
					      struct ath_buf, list);
			list_cut_position(&bf_head,
					  &txq->txq_fifo_pending,
					  &bf->bf_lastbf->list);
			ath_tx_txqaddbuf(sc, txq, &bf_head);
		} else if (sc->sc_flags & SC_OP_TXAGGR)
			ath_txq_schedule(sc, txq);

		spin_unlock_bh(&txq->axq_lock);
	}
}

/*****************/
/* Init, Cleanup */
/*****************/

static int ath_txstatus_setup(struct ath_softc *sc, int size)
{
	struct ath_descdma *dd = &sc->txsdma;
	u8 txs_len = sc->sc_ah->caps.txs_len;

	dd->dd_desc_len = size * txs_len;
	dd->dd_desc = dma_alloc_coherent(sc->dev, dd->dd_desc_len,
					 &dd->dd_desc_paddr, GFP_KERNEL);
	if (!dd->dd_desc)
		return -ENOMEM;

	return 0;
}

static int ath_tx_edma_init(struct ath_softc *sc)
{
	int err;

	err = ath_txstatus_setup(sc, ATH_TXSTATUS_RING_SIZE);
	if (!err)
		ath9k_hw_setup_statusring(sc->sc_ah, sc->txsdma.dd_desc,
					  sc->txsdma.dd_desc_paddr,
					  ATH_TXSTATUS_RING_SIZE);

	return err;
}

static void ath_tx_edma_cleanup(struct ath_softc *sc)
{
	struct ath_descdma *dd = &sc->txsdma;

	dma_free_coherent(sc->dev, dd->dd_desc_len, dd->dd_desc,
			  dd->dd_desc_paddr);
}

int ath_tx_init(struct ath_softc *sc, int nbufs)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	int error = 0;

	spin_lock_init(&sc->tx.txbuflock);

	error = ath_descdma_setup(sc, &sc->tx.txdma, &sc->tx.txbuf,
				  "tx", nbufs, 1, 1);
	if (error != 0) {
		ath_err(common,
			"Failed to allocate tx descriptors: %d\n", error);
		goto err;
	}

	error = ath_descdma_setup(sc, &sc->beacon.bdma, &sc->beacon.bbuf,
				  "beacon", ATH_BCBUF, 1, 1);
	if (error != 0) {
		ath_err(common,
			"Failed to allocate beacon descriptors: %d\n", error);
		goto err;
	}

	INIT_DELAYED_WORK(&sc->tx_complete_work, ath_tx_complete_poll_work);

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) {
		error = ath_tx_edma_init(sc);
		if (error)
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

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA)
		ath_tx_edma_cleanup(sc);
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
	}

	for (acno = 0, ac = &an->ac[acno];
	     acno < WME_NUM_AC; acno++, ac++) {
		ac->sched    = false;
		ac->txq = sc->tx.txq_map[acno];
		INIT_LIST_HEAD(&ac->tid_q);
	}
}

void ath_tx_node_cleanup(struct ath_softc *sc, struct ath_node *an)
{
	struct ath_atx_ac *ac;
	struct ath_atx_tid *tid;
	struct ath_txq *txq;
	int tidno;

	for (tidno = 0, tid = &an->tid[tidno];
	     tidno < WME_NUM_TID; tidno++, tid++) {

		ac = tid->ac;
		txq = ac->txq;

		spin_lock_bh(&txq->axq_lock);

		if (tid->sched) {
			list_del(&tid->list);
			tid->sched = false;
		}

		if (ac->sched) {
			list_del(&ac->list);
			tid->ac->sched = false;
		}

		ath_tid_drain(sc, txq, tid);
		tid->state &= ~AGGR_ADDBA_COMPLETE;
		tid->state &= ~AGGR_CLEANUP;

		spin_unlock_bh(&txq->axq_lock);
	}
}
