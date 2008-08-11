/*
 * Copyright (c) 2008 Atheros Communications Inc.
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

/*
 * Implementation of transmit path.
 */

#include "core.h"

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

/*
 * Insert a chain of ath_buf (descriptors) on a multicast txq
 * but do NOT start tx DMA on this queue.
 * NB: must be called with txq lock held
 */

static void ath_tx_mcastqaddbuf(struct ath_softc *sc,
				struct ath_txq *txq,
				struct list_head *head)
{
	struct ath_hal *ah = sc->sc_ah;
	struct ath_buf *bf;

	if (list_empty(head))
		return;

	/*
	 * Insert the frame on the outbound list and
	 * pass it on to the hardware.
	 */
	bf = list_first_entry(head, struct ath_buf, list);

	/*
	 * The CAB queue is started from the SWBA handler since
	 * frames only go out on DTIM and to avoid possible races.
	 */
	ath9k_hw_set_interrupts(ah, 0);

	/*
	 * If there is anything in the mcastq, we want to set
	 * the "more data" bit in the last item in the queue to
	 * indicate that there is "more data". It makes sense to add
	 * it here since you are *always* going to have
	 * more data when adding to this queue, no matter where
	 * you call from.
	 */

	if (txq->axq_depth) {
		struct ath_buf *lbf;
		struct ieee80211_hdr *hdr;

		/*
		 * Add the "more data flag" to the last frame
		 */

		lbf = list_entry(txq->axq_q.prev, struct ath_buf, list);
		hdr = (struct ieee80211_hdr *)
			((struct sk_buff *)(lbf->bf_mpdu))->data;
		hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_MOREDATA);
	}

	/*
	 * Now, concat the frame onto the queue
	 */
	list_splice_tail_init(head, &txq->axq_q);
	txq->axq_depth++;
	txq->axq_totalqueued++;
	txq->axq_linkbuf = list_entry(txq->axq_q.prev, struct ath_buf, list);

	DPRINTF(sc, ATH_DBG_QUEUE,
		"%s: txq depth = %d\n", __func__, txq->axq_depth);
	if (txq->axq_link != NULL) {
		*txq->axq_link = bf->bf_daddr;
		DPRINTF(sc, ATH_DBG_XMIT,
			"%s: link[%u](%p)=%llx (%p)\n",
			__func__,
			txq->axq_qnum, txq->axq_link,
			ito64(bf->bf_daddr), bf->bf_desc);
	}
	txq->axq_link = &(bf->bf_lastbf->bf_desc->ds_link);
	ath9k_hw_set_interrupts(ah, sc->sc_imask);
}

/*
 * Insert a chain of ath_buf (descriptors) on a txq and
 * assume the descriptors are already chained together by caller.
 * NB: must be called with txq lock held
 */

static void ath_tx_txqaddbuf(struct ath_softc *sc,
		struct ath_txq *txq, struct list_head *head)
{
	struct ath_hal *ah = sc->sc_ah;
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
		"%s: txq depth = %d\n", __func__, txq->axq_depth);

	if (txq->axq_link == NULL) {
		ath9k_hw_puttxbuf(ah, txq->axq_qnum, bf->bf_daddr);
		DPRINTF(sc, ATH_DBG_XMIT,
			"%s: TXDP[%u] = %llx (%p)\n",
			__func__, txq->axq_qnum,
			ito64(bf->bf_daddr), bf->bf_desc);
	} else {
		*txq->axq_link = bf->bf_daddr;
		DPRINTF(sc, ATH_DBG_XMIT, "%s: link[%u] (%p)=%llx (%p)\n",
			__func__,
			txq->axq_qnum, txq->axq_link,
			ito64(bf->bf_daddr), bf->bf_desc);
	}
	txq->axq_link = &(bf->bf_lastbf->bf_desc->ds_link);
	ath9k_hw_txstart(ah, txq->axq_qnum);
}

/* Get transmit rate index using rate in Kbps */

static int ath_tx_findindex(const struct ath9k_rate_table *rt, int rate)
{
	int i;
	int ndx = 0;

	for (i = 0; i < rt->rateCount; i++) {
		if (rt->info[i].rateKbps == rate) {
			ndx = i;
			break;
		}
	}

	return ndx;
}

/* Check if it's okay to send out aggregates */

static int ath_aggr_query(struct ath_softc *sc,
	struct ath_node *an, u8 tidno)
{
	struct ath_atx_tid *tid;
	tid = ATH_AN_2_TID(an, tidno);

	if (tid->addba_exchangecomplete || tid->addba_exchangeinprogress)
		return 1;
	else
		return 0;
}

static enum ath9k_pkt_type get_hal_packet_type(struct ieee80211_hdr *hdr)
{
	enum ath9k_pkt_type htype;
	__le16 fc;

	fc = hdr->frame_control;

	/* Calculate Atheros packet type from IEEE80211 packet header */

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

static void fill_min_rates(struct sk_buff *skb, struct ath_tx_control *txctl)
{
	struct ieee80211_hdr *hdr;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ath_tx_info_priv *tx_info_priv;
	__le16 fc;

	hdr = (struct ieee80211_hdr *)skb->data;
	fc = hdr->frame_control;
	tx_info_priv = (struct ath_tx_info_priv *)tx_info->driver_data[0];

	if (ieee80211_is_mgmt(fc) || ieee80211_is_ctl(fc)) {
		txctl->use_minrate = 1;
		txctl->min_rate = tx_info_priv->min_rate;
	} else if (ieee80211_is_data(fc)) {
		if (ieee80211_is_nullfunc(fc) ||
			/* Port Access Entity (IEEE 802.1X) */
			(skb->protocol == cpu_to_be16(0x888E))) {
			txctl->use_minrate = 1;
			txctl->min_rate = tx_info_priv->min_rate;
		}
		if (is_multicast_ether_addr(hdr->addr1))
			txctl->mcast_rate = tx_info_priv->min_rate;
	}

}

/* This function will setup additional txctl information, mostly rate stuff */
/* FIXME: seqno, ps */
static int ath_tx_prepare(struct ath_softc *sc,
			  struct sk_buff *skb,
			  struct ath_tx_control *txctl)
{
	struct ieee80211_hw *hw = sc->hw;
	struct ieee80211_hdr *hdr;
	struct ath_rc_series *rcs;
	struct ath_txq *txq = NULL;
	const struct ath9k_rate_table *rt;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ath_tx_info_priv *tx_info_priv;
	int hdrlen;
	u8 rix, antenna;
	__le16 fc;
	u8 *qc;

	memset(txctl, 0, sizeof(struct ath_tx_control));

	txctl->dev = sc;
	hdr = (struct ieee80211_hdr *)skb->data;
	hdrlen = ieee80211_get_hdrlen_from_skb(skb);
	fc = hdr->frame_control;

	rt = sc->sc_currates;
	BUG_ON(!rt);

	/* Fill misc fields */

	spin_lock_bh(&sc->node_lock);
	txctl->an = ath_node_get(sc, hdr->addr1);
	/* create a temp node, if the node is not there already */
	if (!txctl->an)
		txctl->an = ath_node_attach(sc, hdr->addr1, 0);
	spin_unlock_bh(&sc->node_lock);

	if (ieee80211_is_data_qos(fc)) {
		qc = ieee80211_get_qos_ctl(hdr);
		txctl->tidno = qc[0] & 0xf;
	}

	txctl->if_id = 0;
	txctl->nextfraglen = 0;
	txctl->frmlen = skb->len + FCS_LEN - (hdrlen & 3);
	txctl->txpower = MAX_RATE_POWER; /* FIXME */

	/* Fill Key related fields */

	txctl->keytype = ATH9K_KEY_TYPE_CLEAR;
	txctl->keyix = ATH9K_TXKEYIX_INVALID;

	if (tx_info->control.hw_key) {
		txctl->keyix = tx_info->control.hw_key->hw_key_idx;
		txctl->frmlen += tx_info->control.icv_len;

		if (sc->sc_keytype == ATH9K_CIPHER_WEP)
			txctl->keytype = ATH9K_KEY_TYPE_WEP;
		else if (sc->sc_keytype == ATH9K_CIPHER_TKIP)
			txctl->keytype = ATH9K_KEY_TYPE_TKIP;
		else if (sc->sc_keytype == ATH9K_CIPHER_AES_CCM)
			txctl->keytype = ATH9K_KEY_TYPE_AES;
	}

	/* Fill packet type */

	txctl->atype = get_hal_packet_type(hdr);

	/* Fill qnum */

	txctl->qnum = ath_get_hal_qnum(skb_get_queue_mapping(skb), sc);
	txq = &sc->sc_txq[txctl->qnum];
	spin_lock_bh(&txq->axq_lock);

	/* Try to avoid running out of descriptors */
	if (txq->axq_depth >= (ATH_TXBUF - 20)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: TX queue: %d is full, depth: %d\n",
			__func__,
			txctl->qnum,
			txq->axq_depth);
		ieee80211_stop_queue(hw, skb_get_queue_mapping(skb));
		txq->stopped = 1;
		spin_unlock_bh(&txq->axq_lock);
		return -1;
	}

	spin_unlock_bh(&txq->axq_lock);

	/* Fill rate */

	fill_min_rates(skb, txctl);

	/* Fill flags */

	txctl->flags = ATH9K_TXDESC_CLRDMASK;    /* needed for crypto errors */

	if (tx_info->flags & IEEE80211_TX_CTL_NO_ACK)
		tx_info->flags |= ATH9K_TXDESC_NOACK;
	if (tx_info->flags & IEEE80211_TX_CTL_USE_RTS_CTS)
		tx_info->flags |= ATH9K_TXDESC_RTSENA;

	/*
	 * Setup for rate calculations.
	 */
	tx_info_priv = (struct ath_tx_info_priv *)tx_info->driver_data[0];
	rcs = tx_info_priv->rcs;

	if (ieee80211_is_data(fc) && !txctl->use_minrate) {

		/* Enable HT only for DATA frames and not for EAPOL */
		txctl->ht = (hw->conf.ht_conf.ht_supported &&
			    (tx_info->flags & IEEE80211_TX_CTL_AMPDU));

		if (is_multicast_ether_addr(hdr->addr1)) {
			rcs[0].rix = (u8)
				ath_tx_findindex(rt, txctl->mcast_rate);

			/*
			 * mcast packets are not re-tried.
			 */
			rcs[0].tries = 1;
		}
		/* For HT capable stations, we save tidno for later use.
		 * We also override seqno set by upper layer with the one
		 * in tx aggregation state.
		 *
		 * First, the fragmentation stat is determined.
		 * If fragmentation is on, the sequence number is
		 * not overridden, since it has been
		 * incremented by the fragmentation routine.
		 */
		if (likely(!(txctl->flags & ATH9K_TXDESC_FRAG_IS_ON)) &&
			txctl->ht && sc->sc_txaggr) {
			struct ath_atx_tid *tid;

			tid = ATH_AN_2_TID(txctl->an, txctl->tidno);

			hdr->seq_ctrl = cpu_to_le16(tid->seq_next <<
				IEEE80211_SEQ_SEQ_SHIFT);
			txctl->seqno = tid->seq_next;
			INCR(tid->seq_next, IEEE80211_SEQ_MAX);
		}
	} else {
		/* for management and control frames,
		 * or for NULL and EAPOL frames */
		if (txctl->min_rate)
			rcs[0].rix = ath_rate_findrateix(sc, txctl->min_rate);
		else
			rcs[0].rix = 0;
		rcs[0].tries = ATH_MGT_TXMAXTRY;
	}
	rix = rcs[0].rix;

	/*
	 * Calculate duration.  This logically belongs in the 802.11
	 * layer but it lacks sufficient information to calculate it.
	 */
	if ((txctl->flags & ATH9K_TXDESC_NOACK) == 0 && !ieee80211_is_ctl(fc)) {
		u16 dur;
		/*
		 * XXX not right with fragmentation.
		 */
		if (sc->sc_flags & ATH_PREAMBLE_SHORT)
			dur = rt->info[rix].spAckDuration;
		else
			dur = rt->info[rix].lpAckDuration;

		if (le16_to_cpu(hdr->frame_control) &
				IEEE80211_FCTL_MOREFRAGS) {
			dur += dur;  /* Add additional 'SIFS + ACK' */

			/*
			** Compute size of next fragment in order to compute
			** durations needed to update NAV.
			** The last fragment uses the ACK duration only.
			** Add time for next fragment.
			*/
			dur += ath9k_hw_computetxtime(sc->sc_ah, rt,
					txctl->nextfraglen,
					rix, sc->sc_flags & ATH_PREAMBLE_SHORT);
		}

		if (ieee80211_has_morefrags(fc) ||
		     (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_FRAG)) {
			/*
			**  Force hardware to use computed duration for next
			**  fragment by disabling multi-rate retry, which
			**  updates duration based on the multi-rate
			**  duration table.
			*/
			rcs[1].tries = rcs[2].tries = rcs[3].tries = 0;
			rcs[1].rix = rcs[2].rix = rcs[3].rix = 0;
			/* reset tries but keep rate index */
			rcs[0].tries = ATH_TXMAXTRY;
		}

		hdr->duration_id = cpu_to_le16(dur);
	}

	/*
	 * Determine if a tx interrupt should be generated for
	 * this descriptor.  We take a tx interrupt to reap
	 * descriptors when the h/w hits an EOL condition or
	 * when the descriptor is specifically marked to generate
	 * an interrupt.  We periodically mark descriptors in this
	 * way to insure timely replenishing of the supply needed
	 * for sending frames.  Defering interrupts reduces system
	 * load and potentially allows more concurrent work to be
	 * done but if done to aggressively can cause senders to
	 * backup.
	 *
	 * NB: use >= to deal with sc_txintrperiod changing
	 *     dynamically through sysctl.
	 */
	spin_lock_bh(&txq->axq_lock);
	if ((++txq->axq_intrcnt >= sc->sc_txintrperiod)) {
		txctl->flags |= ATH9K_TXDESC_INTREQ;
		txq->axq_intrcnt = 0;
	}
	spin_unlock_bh(&txq->axq_lock);

	if (is_multicast_ether_addr(hdr->addr1)) {
		antenna = sc->sc_mcastantenna + 1;
		sc->sc_mcastantenna = (sc->sc_mcastantenna + 1) & 0x1;
	} else
		antenna = sc->sc_txantenna;

#ifdef USE_LEGACY_HAL
	txctl->antenna = antenna;
#endif
	return 0;
}

/* To complete a chain of buffers associated a frame */

static void ath_tx_complete_buf(struct ath_softc *sc,
				struct ath_buf *bf,
				struct list_head *bf_q,
				int txok, int sendbar)
{
	struct sk_buff *skb = bf->bf_mpdu;
	struct ath_xmit_status tx_status;
	dma_addr_t *pa;

	/*
	 * Set retry information.
	 * NB: Don't use the information in the descriptor, because the frame
	 * could be software retried.
	 */
	tx_status.retries = bf->bf_retries;
	tx_status.flags = 0;

	if (sendbar)
		tx_status.flags = ATH_TX_BAR;

	if (!txok) {
		tx_status.flags |= ATH_TX_ERROR;

		if (bf_isxretried(bf))
			tx_status.flags |= ATH_TX_XRETRY;
	}
	/* Unmap this frame */
	pa = get_dma_mem_context(bf, bf_dmacontext);
	pci_unmap_single(sc->pdev,
			 *pa,
			 skb->len,
			 PCI_DMA_TODEVICE);
	/* complete this frame */
	ath_tx_complete(sc, skb, &tx_status, bf->bf_node);

	/*
	 * Return the list of ath_buf of this mpdu to free queue
	 */
	spin_lock_bh(&sc->sc_txbuflock);
	list_splice_tail_init(bf_q, &sc->sc_txbuf);
	spin_unlock_bh(&sc->sc_txbuflock);
}

/*
 * queue up a dest/ac pair for tx scheduling
 * NB: must be called with txq lock held
 */

static void ath_tx_queue_tid(struct ath_txq *txq, struct ath_atx_tid *tid)
{
	struct ath_atx_ac *ac = tid->ac;

	/*
	 * if tid is paused, hold off
	 */
	if (tid->paused)
		return;

	/*
	 * add tid to ac atmost once
	 */
	if (tid->sched)
		return;

	tid->sched = true;
	list_add_tail(&tid->list, &ac->tid_q);

	/*
	 * add node ac to txq atmost once
	 */
	if (ac->sched)
		return;

	ac->sched = true;
	list_add_tail(&ac->list, &txq->axq_acq);
}

/* pause a tid */

static void ath_tx_pause_tid(struct ath_softc *sc, struct ath_atx_tid *tid)
{
	struct ath_txq *txq = &sc->sc_txq[tid->ac->qnum];

	spin_lock_bh(&txq->axq_lock);

	tid->paused++;

	spin_unlock_bh(&txq->axq_lock);
}

/* resume a tid and schedule aggregate */

void ath_tx_resume_tid(struct ath_softc *sc, struct ath_atx_tid *tid)
{
	struct ath_txq *txq = &sc->sc_txq[tid->ac->qnum];

	ASSERT(tid->paused > 0);
	spin_lock_bh(&txq->axq_lock);

	tid->paused--;

	if (tid->paused > 0)
		goto unlock;

	if (list_empty(&tid->buf_q))
		goto unlock;

	/*
	 * Add this TID to scheduler and try to send out aggregates
	 */
	ath_tx_queue_tid(txq, tid);
	ath_txq_schedule(sc, txq);
unlock:
	spin_unlock_bh(&txq->axq_lock);
}

/* Compute the number of bad frames */

static int ath_tx_num_badfrms(struct ath_softc *sc,
	struct ath_buf *bf, int txok)
{
	struct ath_node *an = bf->bf_node;
	int isnodegone = (an->an_flags & ATH_NODE_CLEAN);
	struct ath_buf *bf_last = bf->bf_lastbf;
	struct ath_desc *ds = bf_last->bf_desc;
	u16 seq_st = 0;
	u32 ba[WME_BA_BMP_SIZE >> 5];
	int ba_index;
	int nbad = 0;
	int isaggr = 0;

	if (isnodegone || ds->ds_txstat.ts_flags == ATH9K_TX_SW_ABORTED)
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

/* Update block ack window */

static void ath_tx_update_baw(struct ath_softc *sc,
	struct ath_atx_tid *tid, int seqno)
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

/*
 * ath_pkt_dur - compute packet duration (NB: not NAV)
 *
 * rix - rate index
 * pktlen - total bytes (delims + data + fcs + pads + pad delims)
 * width  - 0 for 20 MHz, 1 for 40 MHz
 * half_gi - to use 4us v/s 3.6 us for symbol time
 */

static u32 ath_pkt_duration(struct ath_softc *sc,
				  u8 rix,
				  struct ath_buf *bf,
				  int width,
				  int half_gi,
				  bool shortPreamble)
{
	const struct ath9k_rate_table *rt = sc->sc_currates;
	u32 nbits, nsymbits, duration, nsymbols;
	u8 rc;
	int streams, pktlen;

	pktlen = bf_isaggr(bf) ? bf->bf_al : bf->bf_frmlen;
	rc = rt->info[rix].rateCode;

	/*
	 * for legacy rates, use old function to compute packet duration
	 */
	if (!IS_HT_RATE(rc))
		return ath9k_hw_computetxtime(sc->sc_ah,
					     rt,
					     pktlen,
					     rix,
					     shortPreamble);
	/*
	 * find number of symbols: PLCP + data
	 */
	nbits = (pktlen << 3) + OFDM_PLCP_BITS;
	nsymbits = bits_per_symbol[HT_RC_2_MCS(rc)][width];
	nsymbols = (nbits + nsymbits - 1) / nsymbits;

	if (!half_gi)
		duration = SYMBOL_TIME(nsymbols);
	else
		duration = SYMBOL_TIME_HALFGI(nsymbols);

	/*
	 * addup duration for legacy/ht training and signal fields
	 */
	streams = HT_RC_2_STREAMS(rc);
	duration += L_STF + L_LTF + L_SIG + HT_SIG + HT_STF + HT_LTF(streams);
	return duration;
}

/* Rate module function to set rate related fields in tx descriptor */

static void ath_buf_set_rate(struct ath_softc *sc, struct ath_buf *bf)
{
	struct ath_hal *ah = sc->sc_ah;
	const struct ath9k_rate_table *rt;
	struct ath_desc *ds = bf->bf_desc;
	struct ath_desc *lastds = bf->bf_lastbf->bf_desc;
	struct ath9k_11n_rate_series series[4];
	int i, flags, rtsctsena = 0, dynamic_mimops = 0;
	u32 ctsduration = 0;
	u8 rix = 0, cix, ctsrate = 0;
	u32 aggr_limit_with_rts = sc->sc_rtsaggrlimit;
	struct ath_node *an = (struct ath_node *) bf->bf_node;

	/*
	 * get the cix for the lowest valid rix.
	 */
	rt = sc->sc_currates;
	for (i = 4; i--;) {
		if (bf->bf_rcs[i].tries) {
			rix = bf->bf_rcs[i].rix;
			break;
		}
	}
	flags = (bf->bf_flags & (ATH9K_TXDESC_RTSENA | ATH9K_TXDESC_CTSENA));
	cix = rt->info[rix].controlRate;

	/*
	 * If 802.11g protection is enabled, determine whether
	 * to use RTS/CTS or just CTS.  Note that this is only
	 * done for OFDM/HT unicast frames.
	 */
	if (sc->sc_protmode != PROT_M_NONE &&
	    (rt->info[rix].phy == PHY_OFDM ||
	     rt->info[rix].phy == PHY_HT) &&
	    (bf->bf_flags & ATH9K_TXDESC_NOACK) == 0) {
		if (sc->sc_protmode == PROT_M_RTSCTS)
			flags = ATH9K_TXDESC_RTSENA;
		else if (sc->sc_protmode == PROT_M_CTSONLY)
			flags = ATH9K_TXDESC_CTSENA;

		cix = rt->info[sc->sc_protrix].controlRate;
		rtsctsena = 1;
	}

	/* For 11n, the default behavior is to enable RTS for
	 * hw retried frames. We enable the global flag here and
	 * let rate series flags determine which rates will actually
	 * use RTS.
	 */
	if ((ah->ah_caps.hw_caps & ATH9K_HW_CAP_HT) && bf_isdata(bf)) {
		BUG_ON(!an);
		/*
		 * 802.11g protection not needed, use our default behavior
		 */
		if (!rtsctsena)
			flags = ATH9K_TXDESC_RTSENA;
		/*
		 * For dynamic MIMO PS, RTS needs to precede the first aggregate
		 * and the second aggregate should have any protection at all.
		 */
		if (an->an_smmode == ATH_SM_PWRSAV_DYNAMIC) {
			if (!bf_isaggrburst(bf)) {
				flags = ATH9K_TXDESC_RTSENA;
				dynamic_mimops = 1;
			} else {
				flags = 0;
			}
		}
	}

	/*
	 * Set protection if aggregate protection on
	 */
	if (sc->sc_config.ath_aggr_prot &&
	    (!bf_isaggr(bf) || (bf_isaggr(bf) && bf->bf_al < 8192))) {
		flags = ATH9K_TXDESC_RTSENA;
		cix = rt->info[sc->sc_protrix].controlRate;
		rtsctsena = 1;
	}

	/*
	 *  For AR5416 - RTS cannot be followed by a frame larger than 8K.
	 */
	if (bf_isaggr(bf) && (bf->bf_al > aggr_limit_with_rts)) {
		/*
		 * Ensure that in the case of SM Dynamic power save
		 * while we are bursting the second aggregate the
		 * RTS is cleared.
		 */
		flags &= ~(ATH9K_TXDESC_RTSENA);
	}

	/*
	 * CTS transmit rate is derived from the transmit rate
	 * by looking in the h/w rate table.  We must also factor
	 * in whether or not a short preamble is to be used.
	 */
	/* NB: cix is set above where RTS/CTS is enabled */
	BUG_ON(cix == 0xff);
	ctsrate = rt->info[cix].rateCode |
		(bf_isshpreamble(bf) ? rt->info[cix].shortPreamble : 0);

	/*
	 * Setup HAL rate series
	 */
	memzero(series, sizeof(struct ath9k_11n_rate_series) * 4);

	for (i = 0; i < 4; i++) {
		if (!bf->bf_rcs[i].tries)
			continue;

		rix = bf->bf_rcs[i].rix;

		series[i].Rate = rt->info[rix].rateCode |
			(bf_isshpreamble(bf) ? rt->info[rix].shortPreamble : 0);

		series[i].Tries = bf->bf_rcs[i].tries;

		series[i].RateFlags = (
			(bf->bf_rcs[i].flags & ATH_RC_RTSCTS_FLAG) ?
				ATH9K_RATESERIES_RTS_CTS : 0) |
			((bf->bf_rcs[i].flags & ATH_RC_CW40_FLAG) ?
				ATH9K_RATESERIES_2040 : 0) |
			((bf->bf_rcs[i].flags & ATH_RC_SGI_FLAG) ?
				ATH9K_RATESERIES_HALFGI : 0);

		series[i].PktDuration = ath_pkt_duration(
			sc, rix, bf,
			(bf->bf_rcs[i].flags & ATH_RC_CW40_FLAG) != 0,
			(bf->bf_rcs[i].flags & ATH_RC_SGI_FLAG),
			bf_isshpreamble(bf));

		if ((an->an_smmode == ATH_SM_PWRSAV_STATIC) &&
		    (bf->bf_rcs[i].flags & ATH_RC_DS_FLAG) == 0) {
			/*
			 * When sending to an HT node that has enabled static
			 * SM/MIMO power save, send at single stream rates but
			 * use maximum allowed transmit chains per user,
			 * hardware, regulatory, or country limits for
			 * better range.
			 */
			series[i].ChSel = sc->sc_tx_chainmask;
		} else {
			if (bf_isht(bf))
				series[i].ChSel =
					ath_chainmask_sel_logic(sc, an);
			else
				series[i].ChSel = sc->sc_tx_chainmask;
		}

		if (rtsctsena)
			series[i].RateFlags |= ATH9K_RATESERIES_RTS_CTS;

		/*
		 * Set RTS for all rates if node is in dynamic powersave
		 * mode and we are using dual stream rates.
		 */
		if (dynamic_mimops && (bf->bf_rcs[i].flags & ATH_RC_DS_FLAG))
			series[i].RateFlags |= ATH9K_RATESERIES_RTS_CTS;
	}

	/*
	 * For non-HT devices, calculate RTS/CTS duration in software
	 * and disable multi-rate retry.
	 */
	if (flags && !(ah->ah_caps.hw_caps & ATH9K_HW_CAP_HT)) {
		/*
		 * Compute the transmit duration based on the frame
		 * size and the size of an ACK frame.  We call into the
		 * HAL to do the computation since it depends on the
		 * characteristics of the actual PHY being used.
		 *
		 * NB: CTS is assumed the same size as an ACK so we can
		 *     use the precalculated ACK durations.
		 */
		if (flags & ATH9K_TXDESC_RTSENA) {    /* SIFS + CTS */
			ctsduration += bf_isshpreamble(bf) ?
				rt->info[cix].spAckDuration :
				rt->info[cix].lpAckDuration;
		}

		ctsduration += series[0].PktDuration;

		if ((bf->bf_flags & ATH9K_TXDESC_NOACK) == 0) { /* SIFS + ACK */
			ctsduration += bf_isshpreamble(bf) ?
				rt->info[rix].spAckDuration :
				rt->info[rix].lpAckDuration;
		}

		/*
		 * Disable multi-rate retry when using RTS/CTS by clearing
		 * series 1, 2 and 3.
		 */
		memzero(&series[1], sizeof(struct ath9k_11n_rate_series) * 3);
	}

	/*
	 * set dur_update_en for l-sig computation except for PS-Poll frames
	 */
	ath9k_hw_set11n_ratescenario(ah, ds, lastds,
				     !bf_ispspoll(bf),
				     ctsrate,
				     ctsduration,
				     series, 4, flags);
	if (sc->sc_config.ath_aggr_prot && flags)
		ath9k_hw_set11n_burstduration(ah, ds, 8192);
}

/*
 * Function to send a normal HT (non-AMPDU) frame
 * NB: must be called with txq lock held
 */

static int ath_tx_send_normal(struct ath_softc *sc,
			      struct ath_txq *txq,
			      struct ath_atx_tid *tid,
			      struct list_head *bf_head)
{
	struct ath_buf *bf;
	struct sk_buff *skb;
	struct ieee80211_tx_info *tx_info;
	struct ath_tx_info_priv *tx_info_priv;

	BUG_ON(list_empty(bf_head));

	bf = list_first_entry(bf_head, struct ath_buf, list);
	bf->bf_state.bf_type &= ~BUF_AMPDU; /* regular HT frame */

	skb = (struct sk_buff *)bf->bf_mpdu;
	tx_info = IEEE80211_SKB_CB(skb);
	tx_info_priv = (struct ath_tx_info_priv *)tx_info->driver_data[0];
	memcpy(bf->bf_rcs, tx_info_priv->rcs, 4 * sizeof(tx_info_priv->rcs[0]));

	/* update starting sequence number for subsequent ADDBA request */
	INCR(tid->seq_start, IEEE80211_SEQ_MAX);

	/* Queue to h/w without aggregation */
	bf->bf_nframes = 1;
	bf->bf_lastbf = bf->bf_lastfrm; /* one single frame */
	ath_buf_set_rate(sc, bf);
	ath_tx_txqaddbuf(sc, txq, bf_head);

	return 0;
}

/* flush tid's software queue and send frames as non-ampdu's */

static void ath_tx_flush_tid(struct ath_softc *sc, struct ath_atx_tid *tid)
{
	struct ath_txq *txq = &sc->sc_txq[tid->ac->qnum];
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
		list_cut_position(&bf_head, &tid->buf_q, &bf->bf_lastfrm->list);
		ath_tx_send_normal(sc, txq, tid, &bf_head);
	}

	spin_unlock_bh(&txq->axq_lock);
}

/* Completion routine of an aggregate */

static void ath_tx_complete_aggr_rifs(struct ath_softc *sc,
				      struct ath_txq *txq,
				      struct ath_buf *bf,
				      struct list_head *bf_q,
				      int txok)
{
	struct ath_node *an = bf->bf_node;
	struct ath_atx_tid *tid = ATH_AN_2_TID(an, bf->bf_tidno);
	struct ath_buf *bf_last = bf->bf_lastbf;
	struct ath_desc *ds = bf_last->bf_desc;
	struct ath_buf *bf_next, *bf_lastq = NULL;
	struct list_head bf_head, bf_pending;
	u16 seq_st = 0;
	u32 ba[WME_BA_BMP_SIZE >> 5];
	int isaggr, txfail, txpending, sendbar = 0, needreset = 0;
	int isnodegone = (an->an_flags & ATH_NODE_CLEAN);

	isaggr = bf_isaggr(bf);
	if (isaggr) {
		if (txok) {
			if (ATH_DS_TX_BA(ds)) {
				/*
				 * extract starting sequence and
				 * block-ack bitmap
				 */
				seq_st = ATH_DS_BA_SEQ(ds);
				memcpy(ba,
					ATH_DS_BA_BITMAP(ds),
					WME_BA_BMP_SIZE >> 3);
			} else {
				memzero(ba, WME_BA_BMP_SIZE >> 3);

				/*
				 * AR5416 can become deaf/mute when BA
				 * issue happens. Chip needs to be reset.
				 * But AP code may have sychronization issues
				 * when perform internal reset in this routine.
				 * Only enable reset in STA mode for now.
				 */
				if (sc->sc_opmode == ATH9K_M_STA)
					needreset = 1;
			}
		} else {
			memzero(ba, WME_BA_BMP_SIZE >> 3);
		}
	}

	INIT_LIST_HEAD(&bf_pending);
	INIT_LIST_HEAD(&bf_head);

	while (bf) {
		txfail = txpending = 0;
		bf_next = bf->bf_next;

		if (ATH_BA_ISSET(ba, ATH_BA_INDEX(seq_st, bf->bf_seqno))) {
			/* transmit completion, subframe is
			 * acked by block ack */
		} else if (!isaggr && txok) {
			/* transmit completion */
		} else {

			if (!tid->cleanup_inprogress && !isnodegone &&
			    ds->ds_txstat.ts_flags != ATH9K_TX_SW_ABORTED) {
				if (bf->bf_retries < ATH_MAX_SW_RETRIES) {
					ath_tx_set_retry(sc, bf);
					txpending = 1;
				} else {
					bf->bf_state.bf_type |= BUF_XRETRY;
					txfail = 1;
					sendbar = 1;
				}
			} else {
				/*
				 * cleanup in progress, just fail
				 * the un-acked sub-frames
				 */
				txfail = 1;
			}
		}
		/*
		 * Remove ath_buf's of this sub-frame from aggregate queue.
		 */
		if (bf_next == NULL) {  /* last subframe in the aggregate */
			ASSERT(bf->bf_lastfrm == bf_last);

			/*
			 * The last descriptor of the last sub frame could be
			 * a holding descriptor for h/w. If that's the case,
			 * bf->bf_lastfrm won't be in the bf_q.
			 * Make sure we handle bf_q properly here.
			 */

			if (!list_empty(bf_q)) {
				bf_lastq = list_entry(bf_q->prev,
					struct ath_buf, list);
				list_cut_position(&bf_head,
					bf_q, &bf_lastq->list);
			} else {
				/*
				 * XXX: if the last subframe only has one
				 * descriptor which is also being used as
				 * a holding descriptor. Then the ath_buf
				 * is not in the bf_q at all.
				 */
				INIT_LIST_HEAD(&bf_head);
			}
		} else {
			ASSERT(!list_empty(bf_q));
			list_cut_position(&bf_head,
				bf_q, &bf->bf_lastfrm->list);
		}

		if (!txpending) {
			/*
			 * complete the acked-ones/xretried ones; update
			 * block-ack window
			 */
			spin_lock_bh(&txq->axq_lock);
			ath_tx_update_baw(sc, tid, bf->bf_seqno);
			spin_unlock_bh(&txq->axq_lock);

			/* complete this sub-frame */
			ath_tx_complete_buf(sc, bf, &bf_head, !txfail, sendbar);
		} else {
			/*
			 * retry the un-acked ones
			 */
			/*
			 * XXX: if the last descriptor is holding descriptor,
			 * in order to requeue the frame to software queue, we
			 * need to allocate a new descriptor and
			 * copy the content of holding descriptor to it.
			 */
			if (bf->bf_next == NULL &&
			    bf_last->bf_status & ATH_BUFSTATUS_STALE) {
				struct ath_buf *tbf;

				/* allocate new descriptor */
				spin_lock_bh(&sc->sc_txbuflock);
				ASSERT(!list_empty((&sc->sc_txbuf)));
				tbf = list_first_entry(&sc->sc_txbuf,
						struct ath_buf, list);
				list_del(&tbf->list);
				spin_unlock_bh(&sc->sc_txbuflock);

				ATH_TXBUF_RESET(tbf);

				/* copy descriptor content */
				tbf->bf_mpdu = bf_last->bf_mpdu;
				tbf->bf_node = bf_last->bf_node;
				tbf->bf_buf_addr = bf_last->bf_buf_addr;
				*(tbf->bf_desc) = *(bf_last->bf_desc);

				/* link it to the frame */
				if (bf_lastq) {
					bf_lastq->bf_desc->ds_link =
						tbf->bf_daddr;
					bf->bf_lastfrm = tbf;
					ath9k_hw_cleartxdesc(sc->sc_ah,
						bf->bf_lastfrm->bf_desc);
				} else {
					tbf->bf_state = bf_last->bf_state;
					tbf->bf_lastfrm = tbf;
					ath9k_hw_cleartxdesc(sc->sc_ah,
						tbf->bf_lastfrm->bf_desc);

					/* copy the DMA context */
					copy_dma_mem_context(
						get_dma_mem_context(tbf,
							bf_dmacontext),
						get_dma_mem_context(bf_last,
							bf_dmacontext));
				}
				list_add_tail(&tbf->list, &bf_head);
			} else {
				/*
				 * Clear descriptor status words for
				 * software retry
				 */
				ath9k_hw_cleartxdesc(sc->sc_ah,
					bf->bf_lastfrm->bf_desc);
			}

			/*
			 * Put this buffer to the temporary pending
			 * queue to retain ordering
			 */
			list_splice_tail_init(&bf_head, &bf_pending);
		}

		bf = bf_next;
	}

	/*
	 * node is already gone. no more assocication
	 * with the node. the node might have been freed
	 * any  node acces can result in panic.note tid
	 * is part of the node.
	 */
	if (isnodegone)
		return;

	if (tid->cleanup_inprogress) {
		/* check to see if we're done with cleaning the h/w queue */
		spin_lock_bh(&txq->axq_lock);

		if (tid->baw_head == tid->baw_tail) {
			tid->addba_exchangecomplete = 0;
			tid->addba_exchangeattempts = 0;
			spin_unlock_bh(&txq->axq_lock);

			tid->cleanup_inprogress = false;

			/* send buffered frames as singles */
			ath_tx_flush_tid(sc, tid);
		} else
			spin_unlock_bh(&txq->axq_lock);

		return;
	}

	/*
	 * prepend un-acked frames to the beginning of the pending frame queue
	 */
	if (!list_empty(&bf_pending)) {
		spin_lock_bh(&txq->axq_lock);
		/* Note: we _prepend_, we _do_not_ at to
		 * the end of the queue ! */
		list_splice(&bf_pending, &tid->buf_q);
		ath_tx_queue_tid(txq, tid);
		spin_unlock_bh(&txq->axq_lock);
	}

	if (needreset)
		ath_reset(sc, false);

	return;
}

/* Process completed xmit descriptors from the specified queue */

static int ath_tx_processq(struct ath_softc *sc, struct ath_txq *txq)
{
	struct ath_hal *ah = sc->sc_ah;
	struct ath_buf *bf, *lastbf, *bf_held = NULL;
	struct list_head bf_head;
	struct ath_desc *ds, *tmp_ds;
	struct sk_buff *skb;
	struct ieee80211_tx_info *tx_info;
	struct ath_tx_info_priv *tx_info_priv;
	int nacked, txok, nbad = 0, isrifs = 0;
	int status;

	DPRINTF(sc, ATH_DBG_QUEUE,
		"%s: tx queue %d (%x), link %p\n", __func__,
		txq->axq_qnum, ath9k_hw_gettxbuf(sc->sc_ah, txq->axq_qnum),
		txq->axq_link);

	nacked = 0;
	for (;;) {
		spin_lock_bh(&txq->axq_lock);
		txq->axq_intrcnt = 0; /* reset periodic desc intr count */
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
		if (bf->bf_status & ATH_BUFSTATUS_STALE) {
			bf_held = bf;
			if (list_is_last(&bf_held->list, &txq->axq_q)) {
				/* FIXME:
				 * The holding descriptor is the last
				 * descriptor in queue. It's safe to remove
				 * the last holding descriptor in BH context.
				 */
				spin_unlock_bh(&txq->axq_lock);
				break;
			} else {
				/* Lets work with the next buffer now */
				bf = list_entry(bf_held->list.next,
					struct ath_buf, list);
			}
		}

		lastbf = bf->bf_lastbf;
		ds = lastbf->bf_desc;    /* NB: last decriptor */

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
		lastbf->bf_status |= ATH_BUFSTATUS_STALE;
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
			list_del(&bf_held->list);
			spin_lock_bh(&sc->sc_txbuflock);
			list_add_tail(&bf_held->list, &sc->sc_txbuf);
			spin_unlock_bh(&sc->sc_txbuflock);
		}

		if (!bf_isampdu(bf)) {
			/*
			 * This frame is sent out as a single frame.
			 * Use hardware retry status for this frame.
			 */
			bf->bf_retries = ds->ds_txstat.ts_longretry;
			if (ds->ds_txstat.ts_status & ATH9K_TXERR_XRETRY)
				bf->bf_state.bf_type |= BUF_XRETRY;
			nbad = 0;
		} else {
			nbad = ath_tx_num_badfrms(sc, bf, txok);
		}
		skb = bf->bf_mpdu;
		tx_info = IEEE80211_SKB_CB(skb);
		tx_info_priv = (struct ath_tx_info_priv *)
			tx_info->driver_data[0];
		if (ds->ds_txstat.ts_status & ATH9K_TXERR_FILT)
			tx_info->flags |= IEEE80211_TX_STAT_TX_FILTERED;
		if ((ds->ds_txstat.ts_status & ATH9K_TXERR_FILT) == 0 &&
				(bf->bf_flags & ATH9K_TXDESC_NOACK) == 0) {
			if (ds->ds_txstat.ts_status == 0)
				nacked++;

			if (bf_isdata(bf)) {
				if (isrifs)
					tmp_ds = bf->bf_rifslast->bf_desc;
				else
					tmp_ds = ds;
				memcpy(&tx_info_priv->tx,
					&tmp_ds->ds_txstat,
					sizeof(tx_info_priv->tx));
				tx_info_priv->n_frames = bf->bf_nframes;
				tx_info_priv->n_bad_frames = nbad;
			}
		}

		/*
		 * Complete this transmit unit
		 */
		if (bf_isampdu(bf))
			ath_tx_complete_aggr_rifs(sc, txq, bf, &bf_head, txok);
		else
			ath_tx_complete_buf(sc, bf, &bf_head, txok, 0);

		/* Wake up mac80211 queue */

		spin_lock_bh(&txq->axq_lock);
		if (txq->stopped && ath_txq_depth(sc, txq->axq_qnum) <=
				(ATH_TXBUF - 20)) {
			int qnum;
			qnum = ath_get_mac80211_qnum(txq->axq_qnum, sc);
			if (qnum != -1) {
				ieee80211_wake_queue(sc->hw, qnum);
				txq->stopped = 0;
			}

		}

		/*
		 * schedule any pending packets if aggregation is enabled
		 */
		if (sc->sc_txaggr)
			ath_txq_schedule(sc, txq);
		spin_unlock_bh(&txq->axq_lock);
	}
	return nacked;
}

static void ath_tx_stopdma(struct ath_softc *sc, struct ath_txq *txq)
{
	struct ath_hal *ah = sc->sc_ah;

	(void) ath9k_hw_stoptxdma(ah, txq->axq_qnum);
	DPRINTF(sc, ATH_DBG_XMIT, "%s: tx queue [%u] %x, link %p\n",
		__func__, txq->axq_qnum,
		ath9k_hw_gettxbuf(ah, txq->axq_qnum), txq->axq_link);
}

/* Drain only the data queues */

static void ath_drain_txdataq(struct ath_softc *sc, bool retry_tx)
{
	struct ath_hal *ah = sc->sc_ah;
	int i;
	int npend = 0;
	enum ath9k_ht_macmode ht_macmode = ath_cwm_macmode(sc);

	/* XXX return value */
	if (!sc->sc_invalid) {
		for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++) {
			if (ATH_TXQ_SETUP(sc, i)) {
				ath_tx_stopdma(sc, &sc->sc_txq[i]);

				/* The TxDMA may not really be stopped.
				 * Double check the hal tx pending count */
				npend += ath9k_hw_numtxpending(ah,
					sc->sc_txq[i].axq_qnum);
			}
		}
	}

	if (npend) {
		int status;

		/* TxDMA not stopped, reset the hal */
		DPRINTF(sc, ATH_DBG_XMIT,
			"%s: Unable to stop TxDMA. Reset HAL!\n", __func__);

		spin_lock_bh(&sc->sc_resetlock);
		if (!ath9k_hw_reset(ah, sc->sc_opmode,
			&sc->sc_curchan, ht_macmode,
			sc->sc_tx_chainmask, sc->sc_rx_chainmask,
			sc->sc_ht_extprotspacing, true, &status)) {

			DPRINTF(sc, ATH_DBG_FATAL,
				"%s: unable to reset hardware; hal status %u\n",
				__func__,
				status);
		}
		spin_unlock_bh(&sc->sc_resetlock);
	}

	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++) {
		if (ATH_TXQ_SETUP(sc, i))
			ath_tx_draintxq(sc, &sc->sc_txq[i], retry_tx);
	}
}

/* Add a sub-frame to block ack window */

static void ath_tx_addto_baw(struct ath_softc *sc,
			     struct ath_atx_tid *tid,
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
 * Function to send an A-MPDU
 * NB: must be called with txq lock held
 */

static int ath_tx_send_ampdu(struct ath_softc *sc,
			     struct ath_txq *txq,
			     struct ath_atx_tid *tid,
			     struct list_head *bf_head,
			     struct ath_tx_control *txctl)
{
	struct ath_buf *bf;
	struct sk_buff *skb;
	struct ieee80211_tx_info *tx_info;
	struct ath_tx_info_priv *tx_info_priv;

	BUG_ON(list_empty(bf_head));

	bf = list_first_entry(bf_head, struct ath_buf, list);
	bf->bf_state.bf_type |= BUF_AMPDU;
	bf->bf_seqno = txctl->seqno; /* save seqno and tidno in buffer */
	bf->bf_tidno = txctl->tidno;

	/*
	 * Do not queue to h/w when any of the following conditions is true:
	 * - there are pending frames in software queue
	 * - the TID is currently paused for ADDBA/BAR request
	 * - seqno is not within block-ack window
	 * - h/w queue depth exceeds low water mark
	 */
	if (!list_empty(&tid->buf_q) || tid->paused ||
	    !BAW_WITHIN(tid->seq_start, tid->baw_size, bf->bf_seqno) ||
	    txq->axq_depth >= ATH_AGGR_MIN_QDEPTH) {
		/*
		 * Add this frame to software queue for scheduling later
		 * for aggregation.
		 */
		list_splice_tail_init(bf_head, &tid->buf_q);
		ath_tx_queue_tid(txq, tid);
		return 0;
	}

	skb = (struct sk_buff *)bf->bf_mpdu;
	tx_info = IEEE80211_SKB_CB(skb);
	tx_info_priv = (struct ath_tx_info_priv *)tx_info->driver_data[0];
	memcpy(bf->bf_rcs, tx_info_priv->rcs, 4 * sizeof(tx_info_priv->rcs[0]));

	/* Add sub-frame to BAW */
	ath_tx_addto_baw(sc, tid, bf);

	/* Queue to h/w without aggregation */
	bf->bf_nframes = 1;
	bf->bf_lastbf = bf->bf_lastfrm; /* one single frame */
	ath_buf_set_rate(sc, bf);
	ath_tx_txqaddbuf(sc, txq, bf_head);
	return 0;
}

/*
 * looks up the rate
 * returns aggr limit based on lowest of the rates
 */

static u32 ath_lookup_rate(struct ath_softc *sc,
				 struct ath_buf *bf)
{
	const struct ath9k_rate_table *rt = sc->sc_currates;
	struct sk_buff *skb;
	struct ieee80211_tx_info *tx_info;
	struct ath_tx_info_priv *tx_info_priv;
	u32 max_4ms_framelen, frame_length;
	u16 aggr_limit, legacy = 0, maxampdu;
	int i;


	skb = (struct sk_buff *)bf->bf_mpdu;
	tx_info = IEEE80211_SKB_CB(skb);
	tx_info_priv = (struct ath_tx_info_priv *)
		tx_info->driver_data[0];
	memcpy(bf->bf_rcs,
		tx_info_priv->rcs, 4 * sizeof(tx_info_priv->rcs[0]));

	/*
	 * Find the lowest frame length among the rate series that will have a
	 * 4ms transmit duration.
	 * TODO - TXOP limit needs to be considered.
	 */
	max_4ms_framelen = ATH_AMPDU_LIMIT_MAX;

	for (i = 0; i < 4; i++) {
		if (bf->bf_rcs[i].tries) {
			frame_length = bf->bf_rcs[i].max_4ms_framelen;

			if (rt->info[bf->bf_rcs[i].rix].phy != PHY_HT) {
				legacy = 1;
				break;
			}

			max_4ms_framelen = min(max_4ms_framelen, frame_length);
		}
	}

	/*
	 * limit aggregate size by the minimum rate if rate selected is
	 * not a probe rate, if rate selected is a probe rate then
	 * avoid aggregation of this packet.
	 */
	if (tx_info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE || legacy)
		return 0;

	aggr_limit = min(max_4ms_framelen,
		(u32)ATH_AMPDU_LIMIT_DEFAULT);

	/*
	 * h/w can accept aggregates upto 16 bit lengths (65535).
	 * The IE, however can hold upto 65536, which shows up here
	 * as zero. Ignore 65536 since we  are constrained by hw.
	 */
	maxampdu = sc->sc_ht_info.maxampdu;
	if (maxampdu)
		aggr_limit = min(aggr_limit, maxampdu);

	return aggr_limit;
}

/*
 * returns the number of delimiters to be added to
 * meet the minimum required mpdudensity.
 * caller should make sure that the rate is  HT rate .
 */

static int ath_compute_num_delims(struct ath_softc *sc,
				  struct ath_buf *bf,
				  u16 frmlen)
{
	const struct ath9k_rate_table *rt = sc->sc_currates;
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
	mpdudensity = sc->sc_ht_info.mpdudensity;

	/*
	 * If there is no mpdu density restriction, no further calculation
	 * is needed.
	 */
	if (mpdudensity == 0)
		return ndelim;

	rix = bf->bf_rcs[0].rix;
	flags = bf->bf_rcs[0].flags;
	rc = rt->info[rix].rateCode;
	width = (flags & ATH_RC_CW40_FLAG) ? 1 : 0;
	half_gi = (flags & ATH_RC_SGI_FLAG) ? 1 : 0;

	if (half_gi)
		nsymbols = NUM_SYMBOLS_PER_USEC_HALFGI(mpdudensity);
	else
		nsymbols = NUM_SYMBOLS_PER_USEC(mpdudensity);

	if (nsymbols == 0)
		nsymbols = 1;

	nsymbits = bits_per_symbol[HT_RC_2_MCS(rc)][width];
	minlen = (nsymbols * nsymbits) / BITS_PER_BYTE;

	/* Is frame shorter than required minimum length? */
	if (frmlen < minlen) {
		/* Get the minimum number of delimiters required. */
		mindelim = (minlen - frmlen) / ATH_AGGR_DELIM_SZ;
		ndelim = max(mindelim, ndelim);
	}

	return ndelim;
}

/*
 * For aggregation from software buffer queue.
 * NB: must be called with txq lock held
 */

static enum ATH_AGGR_STATUS ath_tx_form_aggr(struct ath_softc *sc,
					struct ath_atx_tid *tid,
					struct list_head *bf_q,
					struct ath_buf **bf_last,
					struct aggr_rifs_param *param,
					int *prev_frames)
{
#define PADBYTES(_len) ((4 - ((_len) % 4)) % 4)
	struct ath_buf *bf, *tbf, *bf_first, *bf_prev = NULL;
	struct list_head bf_head;
	int rl = 0, nframes = 0, ndelim;
	u16 aggr_limit = 0, al = 0, bpad = 0,
		al_delta, h_baw = tid->baw_size / 2;
	enum ATH_AGGR_STATUS status = ATH_AGGR_DONE;
	int prev_al = 0, is_ds_rate = 0;
	INIT_LIST_HEAD(&bf_head);

	BUG_ON(list_empty(&tid->buf_q));

	bf_first = list_first_entry(&tid->buf_q, struct ath_buf, list);

	do {
		bf = list_first_entry(&tid->buf_q, struct ath_buf, list);

		/*
		 * do not step over block-ack window
		 */
		if (!BAW_WITHIN(tid->seq_start, tid->baw_size, bf->bf_seqno)) {
			status = ATH_AGGR_BAW_CLOSED;
			break;
		}

		if (!rl) {
			aggr_limit = ath_lookup_rate(sc, bf);
			rl = 1;
			/*
			 * Is rate dual stream
			 */
			is_ds_rate =
				(bf->bf_rcs[0].flags & ATH_RC_DS_FLAG) ? 1 : 0;
		}

		/*
		 * do not exceed aggregation limit
		 */
		al_delta = ATH_AGGR_DELIM_SZ + bf->bf_frmlen;

		if (nframes && (aggr_limit <
			(al + bpad + al_delta + prev_al))) {
			status = ATH_AGGR_LIMITED;
			break;
		}

		/*
		 * do not exceed subframe limit
		 */
		if ((nframes + *prev_frames) >=
		    min((int)h_baw, ATH_AMPDU_SUBFRAME_DEFAULT)) {
			status = ATH_AGGR_LIMITED;
			break;
		}

		/*
		 * add padding for previous frame to aggregation length
		 */
		al += bpad + al_delta;

		/*
		 * Get the delimiters needed to meet the MPDU
		 * density for this node.
		 */
		ndelim = ath_compute_num_delims(sc, bf_first, bf->bf_frmlen);

		bpad = PADBYTES(al_delta) + (ndelim << 2);

		bf->bf_next = NULL;
		bf->bf_lastfrm->bf_desc->ds_link = 0;

		/*
		 * this packet is part of an aggregate
		 * - remove all descriptors belonging to this frame from
		 *   software queue
		 * - add it to block ack window
		 * - set up descriptors for aggregation
		 */
		list_cut_position(&bf_head, &tid->buf_q, &bf->bf_lastfrm->list);
		ath_tx_addto_baw(sc, tid, bf);

		list_for_each_entry(tbf, &bf_head, list) {
			ath9k_hw_set11n_aggr_middle(sc->sc_ah,
				tbf->bf_desc, ndelim);
		}

		/*
		 * link buffers of this frame to the aggregate
		 */
		list_splice_tail_init(&bf_head, bf_q);
		nframes++;

		if (bf_prev) {
			bf_prev->bf_next = bf;
			bf_prev->bf_lastfrm->bf_desc->ds_link = bf->bf_daddr;
		}
		bf_prev = bf;

#ifdef AGGR_NOSHORT
		/*
		 * terminate aggregation on a small packet boundary
		 */
		if (bf->bf_frmlen < ATH_AGGR_MINPLEN) {
			status = ATH_AGGR_SHORTPKT;
			break;
		}
#endif
	} while (!list_empty(&tid->buf_q));

	bf_first->bf_al = al;
	bf_first->bf_nframes = nframes;
	*bf_last = bf_prev;
	return status;
#undef PADBYTES
}

/*
 * process pending frames possibly doing a-mpdu aggregation
 * NB: must be called with txq lock held
 */

static void ath_tx_sched_aggr(struct ath_softc *sc,
	struct ath_txq *txq, struct ath_atx_tid *tid)
{
	struct ath_buf *bf, *tbf, *bf_last, *bf_lastaggr = NULL;
	enum ATH_AGGR_STATUS status;
	struct list_head bf_q;
	struct aggr_rifs_param param = {0, 0, 0, 0, NULL};
	int prev_frames = 0;

	do {
		if (list_empty(&tid->buf_q))
			return;

		INIT_LIST_HEAD(&bf_q);

		status = ath_tx_form_aggr(sc, tid, &bf_q, &bf_lastaggr, &param,
					  &prev_frames);

		/*
		 * no frames picked up to be aggregated; block-ack
		 * window is not open
		 */
		if (list_empty(&bf_q))
			break;

		bf = list_first_entry(&bf_q, struct ath_buf, list);
		bf_last = list_entry(bf_q.prev, struct ath_buf, list);
		bf->bf_lastbf = bf_last;

		/*
		 * if only one frame, send as non-aggregate
		 */
		if (bf->bf_nframes == 1) {
			ASSERT(bf->bf_lastfrm == bf_last);

			bf->bf_state.bf_type &= ~BUF_AGGR;
			/*
			 * clear aggr bits for every descriptor
			 * XXX TODO: is there a way to optimize it?
			 */
			list_for_each_entry(tbf, &bf_q, list) {
				ath9k_hw_clr11n_aggr(sc->sc_ah, tbf->bf_desc);
			}

			ath_buf_set_rate(sc, bf);
			ath_tx_txqaddbuf(sc, txq, &bf_q);
			continue;
		}

		/*
		 * setup first desc with rate and aggr info
		 */
		bf->bf_state.bf_type |= BUF_AGGR;
		ath_buf_set_rate(sc, bf);
		ath9k_hw_set11n_aggr_first(sc->sc_ah, bf->bf_desc, bf->bf_al);

		/*
		 * anchor last frame of aggregate correctly
		 */
		ASSERT(bf_lastaggr);
		ASSERT(bf_lastaggr->bf_lastfrm == bf_last);
		tbf = bf_lastaggr;
		ath9k_hw_set11n_aggr_last(sc->sc_ah, tbf->bf_desc);

		/* XXX: We don't enter into this loop, consider removing this */
		while (!list_empty(&bf_q) && !list_is_last(&tbf->list, &bf_q)) {
			tbf = list_entry(tbf->list.next, struct ath_buf, list);
			ath9k_hw_set11n_aggr_last(sc->sc_ah, tbf->bf_desc);
		}

		txq->axq_aggr_depth++;

		/*
		 * Normal aggregate, queue to hardware
		 */
		ath_tx_txqaddbuf(sc, txq, &bf_q);

	} while (txq->axq_depth < ATH_AGGR_MIN_QDEPTH &&
		 status != ATH_AGGR_BAW_CLOSED);
}

/* Called with txq lock held */

static void ath_tid_drain(struct ath_softc *sc,
			  struct ath_txq *txq,
			  struct ath_atx_tid *tid,
			  bool bh_flag)
{
	struct ath_buf *bf;
	struct list_head bf_head;
	INIT_LIST_HEAD(&bf_head);

	for (;;) {
		if (list_empty(&tid->buf_q))
			break;
		bf = list_first_entry(&tid->buf_q, struct ath_buf, list);

		list_cut_position(&bf_head, &tid->buf_q, &bf->bf_lastfrm->list);

		/* update baw for software retried frame */
		if (bf_isretried(bf))
			ath_tx_update_baw(sc, tid, bf->bf_seqno);

		/*
		 * do not indicate packets while holding txq spinlock.
		 * unlock is intentional here
		 */
		if (likely(bh_flag))
			spin_unlock_bh(&txq->axq_lock);
		else
			spin_unlock(&txq->axq_lock);

		/* complete this sub-frame */
		ath_tx_complete_buf(sc, bf, &bf_head, 0, 0);

		if (likely(bh_flag))
			spin_lock_bh(&txq->axq_lock);
		else
			spin_lock(&txq->axq_lock);
	}

	/*
	 * TODO: For frame(s) that are in the retry state, we will reuse the
	 * sequence number(s) without setting the retry bit. The
	 * alternative is to give up on these and BAR the receiver's window
	 * forward.
	 */
	tid->seq_next = tid->seq_start;
	tid->baw_tail = tid->baw_head;
}

/*
 * Drain all pending buffers
 * NB: must be called with txq lock held
 */

static void ath_txq_drain_pending_buffers(struct ath_softc *sc,
					  struct ath_txq *txq,
					  bool bh_flag)
{
	struct ath_atx_ac *ac, *ac_tmp;
	struct ath_atx_tid *tid, *tid_tmp;

	list_for_each_entry_safe(ac, ac_tmp, &txq->axq_acq, list) {
		list_del(&ac->list);
		ac->sched = false;
		list_for_each_entry_safe(tid, tid_tmp, &ac->tid_q, list) {
			list_del(&tid->list);
			tid->sched = false;
			ath_tid_drain(sc, txq, tid, bh_flag);
		}
	}
}

static int ath_tx_start_dma(struct ath_softc *sc,
			    struct sk_buff *skb,
			    struct scatterlist *sg,
			    u32 n_sg,
			    struct ath_tx_control *txctl)
{
	struct ath_node *an = txctl->an;
	struct ath_buf *bf = NULL;
	struct list_head bf_head;
	struct ath_desc *ds;
	struct ath_hal *ah = sc->sc_ah;
	struct ath_txq *txq = &sc->sc_txq[txctl->qnum];
	struct ath_tx_info_priv *tx_info_priv;
	struct ath_rc_series *rcs;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *tx_info =  IEEE80211_SKB_CB(skb);
	__le16 fc = hdr->frame_control;

	/* For each sglist entry, allocate an ath_buf for DMA */
	INIT_LIST_HEAD(&bf_head);
	spin_lock_bh(&sc->sc_txbuflock);
	if (unlikely(list_empty(&sc->sc_txbuf))) {
		spin_unlock_bh(&sc->sc_txbuflock);
		return -ENOMEM;
	}

	bf = list_first_entry(&sc->sc_txbuf, struct ath_buf, list);
	list_del(&bf->list);
	spin_unlock_bh(&sc->sc_txbuflock);

	list_add_tail(&bf->list, &bf_head);

	/* set up this buffer */
	ATH_TXBUF_RESET(bf);
	bf->bf_frmlen = txctl->frmlen;

	ieee80211_is_data(fc) ?
		(bf->bf_state.bf_type |= BUF_DATA) :
		(bf->bf_state.bf_type &= ~BUF_DATA);
	ieee80211_is_back_req(fc) ?
		(bf->bf_state.bf_type |= BUF_BAR) :
		(bf->bf_state.bf_type &= ~BUF_BAR);
	ieee80211_is_pspoll(fc) ?
		(bf->bf_state.bf_type |= BUF_PSPOLL) :
		(bf->bf_state.bf_type &= ~BUF_PSPOLL);
	(sc->sc_flags & ATH_PREAMBLE_SHORT) ?
		(bf->bf_state.bf_type |= BUF_SHORT_PREAMBLE) :
		(bf->bf_state.bf_type &= ~BUF_SHORT_PREAMBLE);

	bf->bf_flags = txctl->flags;
	bf->bf_keytype = txctl->keytype;
	tx_info_priv = (struct ath_tx_info_priv *)tx_info->driver_data[0];
	rcs = tx_info_priv->rcs;
	bf->bf_rcs[0] = rcs[0];
	bf->bf_rcs[1] = rcs[1];
	bf->bf_rcs[2] = rcs[2];
	bf->bf_rcs[3] = rcs[3];
	bf->bf_node = an;
	bf->bf_mpdu = skb;
	bf->bf_buf_addr = sg_dma_address(sg);

	/* setup descriptor */
	ds = bf->bf_desc;
	ds->ds_link = 0;
	ds->ds_data = bf->bf_buf_addr;

	/*
	 * Save the DMA context in the first ath_buf
	 */
	copy_dma_mem_context(get_dma_mem_context(bf, bf_dmacontext),
			     get_dma_mem_context(txctl, dmacontext));

	/*
	 * Formulate first tx descriptor with tx controls.
	 */
	ath9k_hw_set11n_txdesc(ah,
			       ds,
			       bf->bf_frmlen, /* frame length */
			       txctl->atype, /* Atheros packet type */
			       min(txctl->txpower, (u16)60), /* txpower */
			       txctl->keyix,            /* key cache index */
			       txctl->keytype,          /* key type */
			       txctl->flags);           /* flags */
	ath9k_hw_filltxdesc(ah,
			    ds,
			    sg_dma_len(sg),     /* segment length */
			    true,            /* first segment */
			    (n_sg == 1) ? true : false, /* last segment */
			    ds);                /* first descriptor */

	bf->bf_lastfrm = bf;
	(txctl->ht) ?
		(bf->bf_state.bf_type |= BUF_HT) :
		(bf->bf_state.bf_type &= ~BUF_HT);

	spin_lock_bh(&txq->axq_lock);

	if (txctl->ht && sc->sc_txaggr) {
		struct ath_atx_tid *tid = ATH_AN_2_TID(an, txctl->tidno);
		if (ath_aggr_query(sc, an, txctl->tidno)) {
			/*
			 * Try aggregation if it's a unicast data frame
			 * and the destination is HT capable.
			 */
			ath_tx_send_ampdu(sc, txq, tid, &bf_head, txctl);
		} else {
			/*
			 * Send this frame as regular when ADDBA exchange
			 * is neither complete nor pending.
			 */
			ath_tx_send_normal(sc, txq, tid, &bf_head);
		}
	} else {
		bf->bf_lastbf = bf;
		bf->bf_nframes = 1;
		ath_buf_set_rate(sc, bf);

		if (ieee80211_is_back_req(fc)) {
			/* This is required for resuming tid
			 * during BAR completion */
			bf->bf_tidno = txctl->tidno;
		}

		if (is_multicast_ether_addr(hdr->addr1)) {
			struct ath_vap *avp = sc->sc_vaps[txctl->if_id];

			/*
			 * When servicing one or more stations in power-save
			 * mode (or) if there is some mcast data waiting on
			 * mcast queue (to prevent out of order delivery of
			 * mcast,bcast packets) multicast frames must be
			 * buffered until after the beacon. We use the private
			 * mcast queue for that.
			 */
			/* XXX? more bit in 802.11 frame header */
			spin_lock_bh(&avp->av_mcastq.axq_lock);
			if (txctl->ps || avp->av_mcastq.axq_depth)
				ath_tx_mcastqaddbuf(sc,
					&avp->av_mcastq, &bf_head);
			else
				ath_tx_txqaddbuf(sc, txq, &bf_head);
			spin_unlock_bh(&avp->av_mcastq.axq_lock);
		} else
			ath_tx_txqaddbuf(sc, txq, &bf_head);
	}
	spin_unlock_bh(&txq->axq_lock);
	return 0;
}

static void xmit_map_sg(struct ath_softc *sc,
			struct sk_buff *skb,
			dma_addr_t *pa,
			struct ath_tx_control *txctl)
{
	struct ath_xmit_status tx_status;
	struct ath_atx_tid *tid;
	struct scatterlist sg;

	*pa = pci_map_single(sc->pdev, skb->data, skb->len, PCI_DMA_TODEVICE);

	/* setup S/G list */
	memset(&sg, 0, sizeof(struct scatterlist));
	sg_dma_address(&sg) = *pa;
	sg_dma_len(&sg) = skb->len;

	if (ath_tx_start_dma(sc, skb, &sg, 1, txctl) != 0) {
		/*
		 *  We have to do drop frame here.
		 */
		pci_unmap_single(sc->pdev, *pa, skb->len, PCI_DMA_TODEVICE);

		tx_status.retries = 0;
		tx_status.flags = ATH_TX_ERROR;

		if (txctl->ht && sc->sc_txaggr) {
			/* Reclaim the seqno. */
			tid = ATH_AN_2_TID((struct ath_node *)
				txctl->an, txctl->tidno);
			DECR(tid->seq_next, IEEE80211_SEQ_MAX);
		}
		ath_tx_complete(sc, skb, &tx_status, txctl->an);
	}
}

/* Initialize TX queue and h/w */

int ath_tx_init(struct ath_softc *sc, int nbufs)
{
	int error = 0;

	do {
		spin_lock_init(&sc->sc_txbuflock);

		/* Setup tx descriptors */
		error = ath_descdma_setup(sc, &sc->sc_txdma, &sc->sc_txbuf,
			"tx", nbufs, 1);
		if (error != 0) {
			DPRINTF(sc, ATH_DBG_FATAL,
				"%s: failed to allocate tx descriptors: %d\n",
				__func__, error);
			break;
		}

		/* XXX allocate beacon state together with vap */
		error = ath_descdma_setup(sc, &sc->sc_bdma, &sc->sc_bbuf,
					  "beacon", ATH_BCBUF, 1);
		if (error != 0) {
			DPRINTF(sc, ATH_DBG_FATAL,
				"%s: failed to allocate "
				"beacon descripotrs: %d\n",
				__func__, error);
			break;
		}

	} while (0);

	if (error != 0)
		ath_tx_cleanup(sc);

	return error;
}

/* Reclaim all tx queue resources */

int ath_tx_cleanup(struct ath_softc *sc)
{
	/* cleanup beacon descriptors */
	if (sc->sc_bdma.dd_desc_len != 0)
		ath_descdma_cleanup(sc, &sc->sc_bdma, &sc->sc_bbuf);

	/* cleanup tx descriptors */
	if (sc->sc_txdma.dd_desc_len != 0)
		ath_descdma_cleanup(sc, &sc->sc_txdma, &sc->sc_txbuf);

	return 0;
}

/* Setup a h/w transmit queue */

struct ath_txq *ath_txq_setup(struct ath_softc *sc, int qtype, int subtype)
{
	struct ath_hal *ah = sc->sc_ah;
	struct ath9k_tx_queue_info qi;
	int qnum;

	memzero(&qi, sizeof(qi));
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
	if (qnum >= ARRAY_SIZE(sc->sc_txq)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: hal qnum %u out of range, max %u!\n",
			__func__, qnum, (unsigned int)ARRAY_SIZE(sc->sc_txq));
		ath9k_hw_releasetxqueue(ah, qnum);
		return NULL;
	}
	if (!ATH_TXQ_SETUP(sc, qnum)) {
		struct ath_txq *txq = &sc->sc_txq[qnum];

		txq->axq_qnum = qnum;
		txq->axq_link = NULL;
		INIT_LIST_HEAD(&txq->axq_q);
		INIT_LIST_HEAD(&txq->axq_acq);
		spin_lock_init(&txq->axq_lock);
		txq->axq_depth = 0;
		txq->axq_aggr_depth = 0;
		txq->axq_totalqueued = 0;
		txq->axq_intrcnt = 0;
		txq->axq_linkbuf = NULL;
		sc->sc_txqsetup |= 1<<qnum;
	}
	return &sc->sc_txq[qnum];
}

/* Reclaim resources for a setup queue */

void ath_tx_cleanupq(struct ath_softc *sc, struct ath_txq *txq)
{
	ath9k_hw_releasetxqueue(sc->sc_ah, txq->axq_qnum);
	sc->sc_txqsetup &= ~(1<<txq->axq_qnum);
}

/*
 * Setup a hardware data transmit queue for the specified
 * access control.  The hal may not support all requested
 * queues in which case it will return a reference to a
 * previously setup queue.  We record the mapping from ac's
 * to h/w queues for use by ath_tx_start and also track
 * the set of h/w queues being used to optimize work in the
 * transmit interrupt handler and related routines.
 */

int ath_tx_setup(struct ath_softc *sc, int haltype)
{
	struct ath_txq *txq;

	if (haltype >= ARRAY_SIZE(sc->sc_haltype2q)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: HAL AC %u out of range, max %zu!\n",
			__func__, haltype, ARRAY_SIZE(sc->sc_haltype2q));
		return 0;
	}
	txq = ath_txq_setup(sc, ATH9K_TX_QUEUE_DATA, haltype);
	if (txq != NULL) {
		sc->sc_haltype2q[haltype] = txq->axq_qnum;
		return 1;
	} else
		return 0;
}

int ath_tx_get_qnum(struct ath_softc *sc, int qtype, int haltype)
{
	int qnum;

	switch (qtype) {
	case ATH9K_TX_QUEUE_DATA:
		if (haltype >= ARRAY_SIZE(sc->sc_haltype2q)) {
			DPRINTF(sc, ATH_DBG_FATAL,
				"%s: HAL AC %u out of range, max %zu!\n",
				__func__,
				haltype, ARRAY_SIZE(sc->sc_haltype2q));
			return -1;
		}
		qnum = sc->sc_haltype2q[haltype];
		break;
	case ATH9K_TX_QUEUE_BEACON:
		qnum = sc->sc_bhalq;
		break;
	case ATH9K_TX_QUEUE_CAB:
		qnum = sc->sc_cabq->axq_qnum;
		break;
	default:
		qnum = -1;
	}
	return qnum;
}

/* Update parameters for a transmit queue */

int ath_txq_update(struct ath_softc *sc, int qnum,
		   struct ath9k_tx_queue_info *qinfo)
{
	struct ath_hal *ah = sc->sc_ah;
	int error = 0;
	struct ath9k_tx_queue_info qi;

	if (qnum == sc->sc_bhalq) {
		/*
		 * XXX: for beacon queue, we just save the parameter.
		 * It will be picked up by ath_beaconq_config when
		 * it's necessary.
		 */
		sc->sc_beacon_qi = *qinfo;
		return 0;
	}

	ASSERT(sc->sc_txq[qnum].axq_qnum == qnum);

	ath9k_hw_get_txq_props(ah, qnum, &qi);
	qi.tqi_aifs = qinfo->tqi_aifs;
	qi.tqi_cwmin = qinfo->tqi_cwmin;
	qi.tqi_cwmax = qinfo->tqi_cwmax;
	qi.tqi_burstTime = qinfo->tqi_burstTime;
	qi.tqi_readyTime = qinfo->tqi_readyTime;

	if (!ath9k_hw_set_txq_props(ah, qnum, &qi)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: unable to update hardware queue %u!\n",
			__func__, qnum);
		error = -EIO;
	} else {
		ath9k_hw_resettxqueue(ah, qnum); /* push to h/w */
	}

	return error;
}

int ath_cabq_update(struct ath_softc *sc)
{
	struct ath9k_tx_queue_info qi;
	int qnum = sc->sc_cabq->axq_qnum;
	struct ath_beacon_config conf;

	ath9k_hw_get_txq_props(sc->sc_ah, qnum, &qi);
	/*
	 * Ensure the readytime % is within the bounds.
	 */
	if (sc->sc_config.cabqReadytime < ATH9K_READY_TIME_LO_BOUND)
		sc->sc_config.cabqReadytime = ATH9K_READY_TIME_LO_BOUND;
	else if (sc->sc_config.cabqReadytime > ATH9K_READY_TIME_HI_BOUND)
		sc->sc_config.cabqReadytime = ATH9K_READY_TIME_HI_BOUND;

	ath_get_beaconconfig(sc, ATH_IF_ID_ANY, &conf);
	qi.tqi_readyTime =
		(conf.beacon_interval * sc->sc_config.cabqReadytime) / 100;
	ath_txq_update(sc, qnum, &qi);

	return 0;
}

int ath_tx_start(struct ath_softc *sc, struct sk_buff *skb)
{
	struct ath_tx_control txctl;
	int error = 0;

	error = ath_tx_prepare(sc, skb, &txctl);
	if (error == 0)
		/*
		 * Start DMA mapping.
		 * ath_tx_start_dma() will be called either synchronously
		 * or asynchrounsly once DMA is complete.
		 */
		xmit_map_sg(sc, skb,
			    get_dma_mem_context(&txctl, dmacontext),
			    &txctl);
	else
		ath_node_put(sc, txctl.an, ATH9K_BH_STATUS_CHANGE);

	/* failed packets will be dropped by the caller */
	return error;
}

/* Deferred processing of transmit interrupt */

void ath_tx_tasklet(struct ath_softc *sc)
{
	u64 tsf = ath9k_hw_gettsf64(sc->sc_ah);
	int i, nacked = 0;
	u32 qcumask = ((1 << ATH9K_NUM_TX_QUEUES) - 1);

	ath9k_hw_gettxintrtxqs(sc->sc_ah, &qcumask);

	/*
	 * Process each active queue.
	 */
	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++) {
		if (ATH_TXQ_SETUP(sc, i) && (qcumask & (1 << i)))
			nacked += ath_tx_processq(sc, &sc->sc_txq[i]);
	}
	if (nacked)
		sc->sc_lastrx = tsf;
}

void ath_tx_draintxq(struct ath_softc *sc,
	struct ath_txq *txq, bool retry_tx)
{
	struct ath_buf *bf, *lastbf;
	struct list_head bf_head;

	INIT_LIST_HEAD(&bf_head);

	/*
	 * NB: this assumes output has been stopped and
	 *     we do not need to block ath_tx_tasklet
	 */
	for (;;) {
		spin_lock_bh(&txq->axq_lock);

		if (list_empty(&txq->axq_q)) {
			txq->axq_link = NULL;
			txq->axq_linkbuf = NULL;
			spin_unlock_bh(&txq->axq_lock);
			break;
		}

		bf = list_first_entry(&txq->axq_q, struct ath_buf, list);

		if (bf->bf_status & ATH_BUFSTATUS_STALE) {
			list_del(&bf->list);
			spin_unlock_bh(&txq->axq_lock);

			spin_lock_bh(&sc->sc_txbuflock);
			list_add_tail(&bf->list, &sc->sc_txbuf);
			spin_unlock_bh(&sc->sc_txbuflock);
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
			ath_tx_complete_aggr_rifs(sc, txq, bf, &bf_head, 0);
		else
			ath_tx_complete_buf(sc, bf, &bf_head, 0, 0);
	}

	/* flush any pending frames if aggregation is enabled */
	if (sc->sc_txaggr) {
		if (!retry_tx) {
			spin_lock_bh(&txq->axq_lock);
			ath_txq_drain_pending_buffers(sc, txq,
				ATH9K_BH_STATUS_CHANGE);
			spin_unlock_bh(&txq->axq_lock);
		}
	}
}

/* Drain the transmit queues and reclaim resources */

void ath_draintxq(struct ath_softc *sc, bool retry_tx)
{
	/* stop beacon queue. The beacon will be freed when
	 * we go to INIT state */
	if (!sc->sc_invalid) {
		(void) ath9k_hw_stoptxdma(sc->sc_ah, sc->sc_bhalq);
		DPRINTF(sc, ATH_DBG_XMIT, "%s: beacon queue %x\n", __func__,
			ath9k_hw_gettxbuf(sc->sc_ah, sc->sc_bhalq));
	}

	ath_drain_txdataq(sc, retry_tx);
}

u32 ath_txq_depth(struct ath_softc *sc, int qnum)
{
	return sc->sc_txq[qnum].axq_depth;
}

u32 ath_txq_aggr_depth(struct ath_softc *sc, int qnum)
{
	return sc->sc_txq[qnum].axq_aggr_depth;
}

/* Check if an ADDBA is required. A valid node must be passed. */
enum ATH_AGGR_CHECK ath_tx_aggr_check(struct ath_softc *sc,
				      struct ath_node *an,
				      u8 tidno)
{
	struct ath_atx_tid *txtid;
	DECLARE_MAC_BUF(mac);

	if (!sc->sc_txaggr)
		return AGGR_NOT_REQUIRED;

	/* ADDBA exchange must be completed before sending aggregates */
	txtid = ATH_AN_2_TID(an, tidno);

	if (txtid->addba_exchangecomplete)
		return AGGR_EXCHANGE_DONE;

	if (txtid->cleanup_inprogress)
		return AGGR_CLEANUP_PROGRESS;

	if (txtid->addba_exchangeinprogress)
		return AGGR_EXCHANGE_PROGRESS;

	if (!txtid->addba_exchangecomplete) {
		if (!txtid->addba_exchangeinprogress &&
		    (txtid->addba_exchangeattempts < ADDBA_EXCHANGE_ATTEMPTS)) {
			txtid->addba_exchangeattempts++;
			return AGGR_REQUIRED;
		}
	}

	return AGGR_NOT_REQUIRED;
}

/* Start TX aggregation */

int ath_tx_aggr_start(struct ath_softc *sc,
		      const u8 *addr,
		      u16 tid,
		      u16 *ssn)
{
	struct ath_atx_tid *txtid;
	struct ath_node *an;

	spin_lock_bh(&sc->node_lock);
	an = ath_node_find(sc, (u8 *) addr);
	spin_unlock_bh(&sc->node_lock);

	if (!an) {
		DPRINTF(sc, ATH_DBG_AGGR,
			"%s: Node not found to initialize "
			"TX aggregation\n", __func__);
		return -1;
	}

	if (sc->sc_txaggr) {
		txtid = ATH_AN_2_TID(an, tid);
		txtid->addba_exchangeinprogress = 1;
		ath_tx_pause_tid(sc, txtid);
	}

	return 0;
}

/* Stop tx aggregation */

int ath_tx_aggr_stop(struct ath_softc *sc,
		     const u8 *addr,
		     u16 tid)
{
	struct ath_node *an;

	spin_lock_bh(&sc->node_lock);
	an = ath_node_find(sc, (u8 *) addr);
	spin_unlock_bh(&sc->node_lock);

	if (!an) {
		DPRINTF(sc, ATH_DBG_AGGR,
			"%s: TX aggr stop for non-existent node\n", __func__);
		return -1;
	}

	ath_tx_aggr_teardown(sc, an, tid);
	return 0;
}

/*
 * Performs transmit side cleanup when TID changes from aggregated to
 * unaggregated.
 * - Pause the TID and mark cleanup in progress
 * - Discard all retry frames from the s/w queue.
 */

void ath_tx_aggr_teardown(struct ath_softc *sc,
	struct ath_node *an, u8 tid)
{
	struct ath_atx_tid *txtid = ATH_AN_2_TID(an, tid);
	struct ath_txq *txq = &sc->sc_txq[txtid->ac->qnum];
	struct ath_buf *bf;
	struct list_head bf_head;
	INIT_LIST_HEAD(&bf_head);

	DPRINTF(sc, ATH_DBG_AGGR, "%s: teardown TX aggregation\n", __func__);

	if (txtid->cleanup_inprogress) /* cleanup is in progress */
		return;

	if (!txtid->addba_exchangecomplete) {
		txtid->addba_exchangeattempts = 0;
		return;
	}

	/* TID must be paused first */
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
		list_cut_position(&bf_head,
			&txtid->buf_q, &bf->bf_lastfrm->list);
		ath_tx_update_baw(sc, txtid, bf->bf_seqno);

		/* complete this sub-frame */
		ath_tx_complete_buf(sc, bf, &bf_head, 0, 0);
	}

	if (txtid->baw_head != txtid->baw_tail) {
		spin_unlock_bh(&txq->axq_lock);
		txtid->cleanup_inprogress = true;
	} else {
		txtid->addba_exchangecomplete = 0;
		txtid->addba_exchangeattempts = 0;
		spin_unlock_bh(&txq->axq_lock);
		ath_tx_flush_tid(sc, txtid);
	}
}

/*
 * Tx scheduling logic
 * NB: must be called with txq lock held
 */

void ath_txq_schedule(struct ath_softc *sc, struct ath_txq *txq)
{
	struct ath_atx_ac *ac;
	struct ath_atx_tid *tid;

	/* nothing to schedule */
	if (list_empty(&txq->axq_acq))
		return;
	/*
	 * get the first node/ac pair on the queue
	 */
	ac = list_first_entry(&txq->axq_acq, struct ath_atx_ac, list);
	list_del(&ac->list);
	ac->sched = false;

	/*
	 * process a single tid per destination
	 */
	do {
		/* nothing to schedule */
		if (list_empty(&ac->tid_q))
			return;

		tid = list_first_entry(&ac->tid_q, struct ath_atx_tid, list);
		list_del(&tid->list);
		tid->sched = false;

		if (tid->paused)    /* check next tid to keep h/w busy */
			continue;

		if (!(tid->an->an_smmode == ATH_SM_PWRSAV_DYNAMIC) ||
		    ((txq->axq_depth % 2) == 0)) {
			ath_tx_sched_aggr(sc, txq, tid);
		}

		/*
		 * add tid to round-robin queue if more frames
		 * are pending for the tid
		 */
		if (!list_empty(&tid->buf_q))
			ath_tx_queue_tid(txq, tid);

		/* only schedule one TID at a time */
		break;
	} while (!list_empty(&ac->tid_q));

	/*
	 * schedule AC if more TIDs need processing
	 */
	if (!list_empty(&ac->tid_q)) {
		/*
		 * add dest ac to txq if not already added
		 */
		if (!ac->sched) {
			ac->sched = true;
			list_add_tail(&ac->list, &txq->axq_acq);
		}
	}
}

/* Initialize per-node transmit state */

void ath_tx_node_init(struct ath_softc *sc, struct ath_node *an)
{
	if (sc->sc_txaggr) {
		struct ath_atx_tid *tid;
		struct ath_atx_ac *ac;
		int tidno, acno;

		sc->sc_ht_info.maxampdu = ATH_AMPDU_LIMIT_DEFAULT;

		/*
		 * Init per tid tx state
		 */
		for (tidno = 0, tid = &an->an_aggr.tx.tid[tidno];
				tidno < WME_NUM_TID;
				tidno++, tid++) {
			tid->an        = an;
			tid->tidno     = tidno;
			tid->seq_start = tid->seq_next = 0;
			tid->baw_size  = WME_MAX_BA;
			tid->baw_head  = tid->baw_tail = 0;
			tid->sched     = false;
			tid->paused = false;
			tid->cleanup_inprogress = false;
			INIT_LIST_HEAD(&tid->buf_q);

			acno = TID_TO_WME_AC(tidno);
			tid->ac = &an->an_aggr.tx.ac[acno];

			/* ADDBA state */
			tid->addba_exchangecomplete     = 0;
			tid->addba_exchangeinprogress   = 0;
			tid->addba_exchangeattempts     = 0;
		}

		/*
		 * Init per ac tx state
		 */
		for (acno = 0, ac = &an->an_aggr.tx.ac[acno];
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
}

/* Cleanupthe pending buffers for the node. */

void ath_tx_node_cleanup(struct ath_softc *sc,
	struct ath_node *an, bool bh_flag)
{
	int i;
	struct ath_atx_ac *ac, *ac_tmp;
	struct ath_atx_tid *tid, *tid_tmp;
	struct ath_txq *txq;
	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++) {
		if (ATH_TXQ_SETUP(sc, i)) {
			txq = &sc->sc_txq[i];

			if (likely(bh_flag))
				spin_lock_bh(&txq->axq_lock);
			else
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
					ath_tid_drain(sc, txq, tid, bh_flag);
					tid->addba_exchangecomplete = 0;
					tid->addba_exchangeattempts = 0;
					tid->cleanup_inprogress = false;
				}
			}

			if (likely(bh_flag))
				spin_unlock_bh(&txq->axq_lock);
			else
				spin_unlock(&txq->axq_lock);
		}
	}
}

/* Cleanup per node transmit state */

void ath_tx_node_free(struct ath_softc *sc, struct ath_node *an)
{
	if (sc->sc_txaggr) {
		struct ath_atx_tid *tid;
		int tidno, i;

		/* Init per tid rx state */
		for (tidno = 0, tid = &an->an_aggr.tx.tid[tidno];
			tidno < WME_NUM_TID;
		     tidno++, tid++) {

			for (i = 0; i < ATH_TID_MAX_BUFS; i++)
				ASSERT(tid->tx_buf[i] == NULL);
		}
	}
}
