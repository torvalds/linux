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

#define FUDGE 2

/*
 *  This function will modify certain transmit queue properties depending on
 *  the operating mode of the station (AP or AdHoc).  Parameters are AIFS
 *  settings and channel width min/max
*/
int ath_beaconq_config(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_tx_queue_info qi, qi_be;
	int qnum;

	ath9k_hw_get_txq_props(ah, sc->beacon.beaconq, &qi);
	if (sc->sc_ah->opmode == NL80211_IFTYPE_AP) {
		/* Always burst out beacon and CAB traffic. */
		qi.tqi_aifs = 1;
		qi.tqi_cwmin = 0;
		qi.tqi_cwmax = 0;
	} else {
		/* Adhoc mode; important thing is to use 2x cwmin. */
		qnum = ath_tx_get_qnum(sc, ATH9K_TX_QUEUE_DATA,
				       ATH9K_WME_AC_BE);
		ath9k_hw_get_txq_props(ah, qnum, &qi_be);
		qi.tqi_aifs = qi_be.tqi_aifs;
		qi.tqi_cwmin = 4*qi_be.tqi_cwmin;
		qi.tqi_cwmax = qi_be.tqi_cwmax;
	}

	if (!ath9k_hw_set_txq_props(ah, sc->beacon.beaconq, &qi)) {
		ath_print(common, ATH_DBG_FATAL,
			  "Unable to update h/w beacon queue parameters\n");
		return 0;
	} else {
		ath9k_hw_resettxqueue(ah, sc->beacon.beaconq);
		return 1;
	}
}

/*
 *  Associates the beacon frame buffer with a transmit descriptor.  Will set
 *  up all required antenna switch parameters, rate codes, and channel flags.
 *  Beacons are always sent out at the lowest rate, and are not retried.
*/
static void ath_beacon_setup(struct ath_softc *sc, struct ath_vif *avp,
			     struct ath_buf *bf)
{
	struct sk_buff *skb = bf->bf_mpdu;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_desc *ds;
	struct ath9k_11n_rate_series series[4];
	int flags, antenna, ctsrate = 0, ctsduration = 0;
	struct ieee80211_supported_band *sband;
	u8 rate = 0;

	ds = bf->bf_desc;
	flags = ATH9K_TXDESC_NOACK;

	if (((sc->sc_ah->opmode == NL80211_IFTYPE_ADHOC) ||
	     (sc->sc_ah->opmode == NL80211_IFTYPE_MESH_POINT)) &&
	    (ah->caps.hw_caps & ATH9K_HW_CAP_VEOL)) {
		ds->ds_link = bf->bf_daddr; /* self-linked */
		flags |= ATH9K_TXDESC_VEOL;
		/* Let hardware handle antenna switching. */
		antenna = 0;
	} else {
		ds->ds_link = 0;
		/*
		 * Switch antenna every beacon.
		 * Should only switch every beacon period, not for every SWBA
		 * XXX assumes two antennae
		 */
		antenna = ((sc->beacon.ast_be_xmit / sc->nbcnvifs) & 1 ? 2 : 1);
	}

	ds->ds_data = bf->bf_buf_addr;

	sband = &sc->sbands[common->hw->conf.channel->band];
	rate = sband->bitrates[0].hw_value;
	if (sc->sc_flags & SC_OP_PREAMBLE_SHORT)
		rate |= sband->bitrates[0].hw_value_short;

	ath9k_hw_set11n_txdesc(ah, ds, skb->len + FCS_LEN,
			       ATH9K_PKT_TYPE_BEACON,
			       MAX_RATE_POWER,
			       ATH9K_TXKEYIX_INVALID,
			       ATH9K_KEY_TYPE_CLEAR,
			       flags);

	/* NB: beacon's BufLen must be a multiple of 4 bytes */
	ath9k_hw_filltxdesc(ah, ds, roundup(skb->len, 4),
			    true, true, ds);

	memset(series, 0, sizeof(struct ath9k_11n_rate_series) * 4);
	series[0].Tries = 1;
	series[0].Rate = rate;
	series[0].ChSel = common->tx_chainmask;
	series[0].RateFlags = (ctsrate) ? ATH9K_RATESERIES_RTS_CTS : 0;
	ath9k_hw_set11n_ratescenario(ah, ds, ds, 0, ctsrate, ctsduration,
				     series, 4, 0);
}

static struct ath_buf *ath_beacon_generate(struct ieee80211_hw *hw,
					   struct ieee80211_vif *vif)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_buf *bf;
	struct ath_vif *avp;
	struct sk_buff *skb;
	struct ath_txq *cabq;
	struct ieee80211_tx_info *info;
	int cabq_depth;

	if (aphy->state != ATH_WIPHY_ACTIVE)
		return NULL;

	avp = (void *)vif->drv_priv;
	cabq = sc->beacon.cabq;

	if (avp->av_bcbuf == NULL)
		return NULL;

	/* Release the old beacon first */

	bf = avp->av_bcbuf;
	skb = bf->bf_mpdu;
	if (skb) {
		dma_unmap_single(sc->dev, bf->bf_dmacontext,
				 skb->len, DMA_TO_DEVICE);
		dev_kfree_skb_any(skb);
	}

	/* Get a new beacon from mac80211 */

	skb = ieee80211_beacon_get(hw, vif);
	bf->bf_mpdu = skb;
	if (skb == NULL)
		return NULL;
	((struct ieee80211_mgmt *)skb->data)->u.beacon.timestamp =
		avp->tsf_adjust;

	info = IEEE80211_SKB_CB(skb);
	if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ) {
		/*
		 * TODO: make sure the seq# gets assigned properly (vs. other
		 * TX frames)
		 */
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
		sc->tx.seq_no += 0x10;
		hdr->seq_ctrl &= cpu_to_le16(IEEE80211_SCTL_FRAG);
		hdr->seq_ctrl |= cpu_to_le16(sc->tx.seq_no);
	}

	bf->bf_buf_addr = bf->bf_dmacontext =
		dma_map_single(sc->dev, skb->data,
			       skb->len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(sc->dev, bf->bf_buf_addr))) {
		dev_kfree_skb_any(skb);
		bf->bf_mpdu = NULL;
		ath_print(common, ATH_DBG_FATAL,
			  "dma_mapping_error on beaconing\n");
		return NULL;
	}

	skb = ieee80211_get_buffered_bc(hw, vif);

	/*
	 * if the CABQ traffic from previous DTIM is pending and the current
	 *  beacon is also a DTIM.
	 *  1) if there is only one vif let the cab traffic continue.
	 *  2) if there are more than one vif and we are using staggered
	 *     beacons, then drain the cabq by dropping all the frames in
	 *     the cabq so that the current vifs cab traffic can be scheduled.
	 */
	spin_lock_bh(&cabq->axq_lock);
	cabq_depth = cabq->axq_depth;
	spin_unlock_bh(&cabq->axq_lock);

	if (skb && cabq_depth) {
		if (sc->nvifs > 1) {
			ath_print(common, ATH_DBG_BEACON,
				  "Flushing previous cabq traffic\n");
			ath_draintxq(sc, cabq, false);
		}
	}

	ath_beacon_setup(sc, avp, bf);

	while (skb) {
		ath_tx_cabq(hw, skb);
		skb = ieee80211_get_buffered_bc(hw, vif);
	}

	return bf;
}

/*
 * Startup beacon transmission for adhoc mode when they are sent entirely
 * by the hardware using the self-linked descriptor + veol trick.
*/
static void ath_beacon_start_adhoc(struct ath_softc *sc,
				   struct ieee80211_vif *vif)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_buf *bf;
	struct ath_vif *avp;
	struct sk_buff *skb;

	avp = (void *)vif->drv_priv;

	if (avp->av_bcbuf == NULL)
		return;

	bf = avp->av_bcbuf;
	skb = bf->bf_mpdu;

	ath_beacon_setup(sc, avp, bf);

	/* NB: caller is known to have already stopped tx dma */
	ath9k_hw_puttxbuf(ah, sc->beacon.beaconq, bf->bf_daddr);
	ath9k_hw_txstart(ah, sc->beacon.beaconq);
	ath_print(common, ATH_DBG_BEACON, "TXDP%u = %llx (%p)\n",
		  sc->beacon.beaconq, ito64(bf->bf_daddr), bf->bf_desc);
}

int ath_beacon_alloc(struct ath_wiphy *aphy, struct ieee80211_vif *vif)
{
	struct ath_softc *sc = aphy->sc;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_vif *avp;
	struct ath_buf *bf;
	struct sk_buff *skb;
	__le64 tstamp;

	avp = (void *)vif->drv_priv;

	/* Allocate a beacon descriptor if we haven't done so. */
	if (!avp->av_bcbuf) {
		/* Allocate beacon state for hostap/ibss.  We know
		 * a buffer is available. */
		avp->av_bcbuf = list_first_entry(&sc->beacon.bbuf,
						 struct ath_buf, list);
		list_del(&avp->av_bcbuf->list);

		if (sc->sc_ah->opmode == NL80211_IFTYPE_AP ||
		    !(sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_VEOL)) {
			int slot;
			/*
			 * Assign the vif to a beacon xmit slot. As
			 * above, this cannot fail to find one.
			 */
			avp->av_bslot = 0;
			for (slot = 0; slot < ATH_BCBUF; slot++)
				if (sc->beacon.bslot[slot] == NULL) {
					/*
					 * XXX hack, space out slots to better
					 * deal with misses
					 */
					if (slot+1 < ATH_BCBUF &&
					    sc->beacon.bslot[slot+1] == NULL) {
						avp->av_bslot = slot+1;
						break;
					}
					avp->av_bslot = slot;
					/* NB: keep looking for a double slot */
				}
			BUG_ON(sc->beacon.bslot[avp->av_bslot] != NULL);
			sc->beacon.bslot[avp->av_bslot] = vif;
			sc->beacon.bslot_aphy[avp->av_bslot] = aphy;
			sc->nbcnvifs++;
		}
	}

	/* release the previous beacon frame, if it already exists. */
	bf = avp->av_bcbuf;
	if (bf->bf_mpdu != NULL) {
		skb = bf->bf_mpdu;
		dma_unmap_single(sc->dev, bf->bf_dmacontext,
				 skb->len, DMA_TO_DEVICE);
		dev_kfree_skb_any(skb);
		bf->bf_mpdu = NULL;
	}

	/* NB: the beacon data buffer must be 32-bit aligned. */
	skb = ieee80211_beacon_get(sc->hw, vif);
	if (skb == NULL) {
		ath_print(common, ATH_DBG_BEACON, "cannot get skb\n");
		return -ENOMEM;
	}

	tstamp = ((struct ieee80211_mgmt *)skb->data)->u.beacon.timestamp;
	sc->beacon.bc_tstamp = le64_to_cpu(tstamp);
	/* Calculate a TSF adjustment factor required for staggered beacons. */
	if (avp->av_bslot > 0) {
		u64 tsfadjust;
		int intval;

		intval = sc->beacon_interval ? : ATH_DEFAULT_BINTVAL;

		/*
		 * Calculate the TSF offset for this beacon slot, i.e., the
		 * number of usecs that need to be added to the timestamp field
		 * in Beacon and Probe Response frames. Beacon slot 0 is
		 * processed at the correct offset, so it does not require TSF
		 * adjustment. Other slots are adjusted to get the timestamp
		 * close to the TBTT for the BSS.
		 */
		tsfadjust = intval * avp->av_bslot / ATH_BCBUF;
		avp->tsf_adjust = cpu_to_le64(TU_TO_USEC(tsfadjust));

		ath_print(common, ATH_DBG_BEACON,
			  "stagger beacons, bslot %d intval "
			  "%u tsfadjust %llu\n",
			  avp->av_bslot, intval, (unsigned long long)tsfadjust);

		((struct ieee80211_mgmt *)skb->data)->u.beacon.timestamp =
			avp->tsf_adjust;
	} else
		avp->tsf_adjust = cpu_to_le64(0);

	bf->bf_mpdu = skb;
	bf->bf_buf_addr = bf->bf_dmacontext =
		dma_map_single(sc->dev, skb->data,
			       skb->len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(sc->dev, bf->bf_buf_addr))) {
		dev_kfree_skb_any(skb);
		bf->bf_mpdu = NULL;
		ath_print(common, ATH_DBG_FATAL,
			  "dma_mapping_error on beacon alloc\n");
		return -ENOMEM;
	}

	return 0;
}

void ath_beacon_return(struct ath_softc *sc, struct ath_vif *avp)
{
	if (avp->av_bcbuf != NULL) {
		struct ath_buf *bf;

		if (avp->av_bslot != -1) {
			sc->beacon.bslot[avp->av_bslot] = NULL;
			sc->beacon.bslot_aphy[avp->av_bslot] = NULL;
			sc->nbcnvifs--;
		}

		bf = avp->av_bcbuf;
		if (bf->bf_mpdu != NULL) {
			struct sk_buff *skb = bf->bf_mpdu;
			dma_unmap_single(sc->dev, bf->bf_dmacontext,
					 skb->len, DMA_TO_DEVICE);
			dev_kfree_skb_any(skb);
			bf->bf_mpdu = NULL;
		}
		list_add_tail(&bf->list, &sc->beacon.bbuf);

		avp->av_bcbuf = NULL;
	}
}

void ath_beacon_tasklet(unsigned long data)
{
	struct ath_softc *sc = (struct ath_softc *)data;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_buf *bf = NULL;
	struct ieee80211_vif *vif;
	struct ath_wiphy *aphy;
	int slot;
	u32 bfaddr, bc = 0, tsftu;
	u64 tsf;
	u16 intval;

	/*
	 * Check if the previous beacon has gone out.  If
	 * not don't try to post another, skip this period
	 * and wait for the next.  Missed beacons indicate
	 * a problem and should not occur.  If we miss too
	 * many consecutive beacons reset the device.
	 */
	if (ath9k_hw_numtxpending(ah, sc->beacon.beaconq) != 0) {
		sc->beacon.bmisscnt++;

		if (sc->beacon.bmisscnt < BSTUCK_THRESH) {
			ath_print(common, ATH_DBG_BEACON,
				  "missed %u consecutive beacons\n",
				  sc->beacon.bmisscnt);
		} else if (sc->beacon.bmisscnt >= BSTUCK_THRESH) {
			ath_print(common, ATH_DBG_BEACON,
				  "beacon is officially stuck\n");
			sc->sc_flags |= SC_OP_TSF_RESET;
			ath_reset(sc, false);
		}

		return;
	}

	if (sc->beacon.bmisscnt != 0) {
		ath_print(common, ATH_DBG_BEACON,
			  "resume beacon xmit after %u misses\n",
			  sc->beacon.bmisscnt);
		sc->beacon.bmisscnt = 0;
	}

	/*
	 * Generate beacon frames. we are sending frames
	 * staggered so calculate the slot for this frame based
	 * on the tsf to safeguard against missing an swba.
	 */

	intval = sc->beacon_interval ? : ATH_DEFAULT_BINTVAL;

	tsf = ath9k_hw_gettsf64(ah);
	tsftu = TSF_TO_TU(tsf>>32, tsf);
	slot = ((tsftu % intval) * ATH_BCBUF) / intval;
	/*
	 * Reverse the slot order to get slot 0 on the TBTT offset that does
	 * not require TSF adjustment and other slots adding
	 * slot/ATH_BCBUF * beacon_int to timestamp. For example, with
	 * ATH_BCBUF = 4, we process beacon slots as follows: 3 2 1 0 3 2 1 ..
	 * and slot 0 is at correct offset to TBTT.
	 */
	slot = ATH_BCBUF - slot - 1;
	vif = sc->beacon.bslot[slot];
	aphy = sc->beacon.bslot_aphy[slot];

	ath_print(common, ATH_DBG_BEACON,
		  "slot %d [tsf %llu tsftu %u intval %u] vif %p\n",
		  slot, tsf, tsftu, intval, vif);

	bfaddr = 0;
	if (vif) {
		bf = ath_beacon_generate(aphy->hw, vif);
		if (bf != NULL) {
			bfaddr = bf->bf_daddr;
			bc = 1;
		}
	}

	/*
	 * Handle slot time change when a non-ERP station joins/leaves
	 * an 11g network.  The 802.11 layer notifies us via callback,
	 * we mark updateslot, then wait one beacon before effecting
	 * the change.  This gives associated stations at least one
	 * beacon interval to note the state change.
	 *
	 * NB: The slot time change state machine is clocked according
	 *     to whether we are bursting or staggering beacons.  We
	 *     recognize the request to update and record the current
	 *     slot then don't transition until that slot is reached
	 *     again.  If we miss a beacon for that slot then we'll be
	 *     slow to transition but we'll be sure at least one beacon
	 *     interval has passed.  When bursting slot is always left
	 *     set to ATH_BCBUF so this check is a noop.
	 */
	if (sc->beacon.updateslot == UPDATE) {
		sc->beacon.updateslot = COMMIT; /* commit next beacon */
		sc->beacon.slotupdate = slot;
	} else if (sc->beacon.updateslot == COMMIT && sc->beacon.slotupdate == slot) {
		ath9k_hw_setslottime(sc->sc_ah, sc->beacon.slottime);
		sc->beacon.updateslot = OK;
	}
	if (bfaddr != 0) {
		/*
		 * Stop any current dma and put the new frame(s) on the queue.
		 * This should never fail since we check above that no frames
		 * are still pending on the queue.
		 */
		if (!ath9k_hw_stoptxdma(ah, sc->beacon.beaconq)) {
			ath_print(common, ATH_DBG_FATAL,
				"beacon queue %u did not stop?\n", sc->beacon.beaconq);
		}

		/* NB: cabq traffic should already be queued and primed */
		ath9k_hw_puttxbuf(ah, sc->beacon.beaconq, bfaddr);
		ath9k_hw_txstart(ah, sc->beacon.beaconq);

		sc->beacon.ast_be_xmit += bc;     /* XXX per-vif? */
	}
}

static void ath9k_beacon_init(struct ath_softc *sc,
			      u32 next_beacon,
			      u32 beacon_period)
{
	if (beacon_period & ATH9K_BEACON_RESET_TSF)
		ath9k_ps_wakeup(sc);

	ath9k_hw_beaconinit(sc->sc_ah, next_beacon, beacon_period);

	if (beacon_period & ATH9K_BEACON_RESET_TSF)
		ath9k_ps_restore(sc);
}

/*
 * For multi-bss ap support beacons are either staggered evenly over N slots or
 * burst together.  For the former arrange for the SWBA to be delivered for each
 * slot. Slots that are not occupied will generate nothing.
 */
static void ath_beacon_config_ap(struct ath_softc *sc,
				 struct ath_beacon_config *conf)
{
	u32 nexttbtt, intval;

	/* Configure the timers only when the TSF has to be reset */

	if (!(sc->sc_flags & SC_OP_TSF_RESET))
		return;

	/* NB: the beacon interval is kept internally in TU's */
	intval = conf->beacon_interval & ATH9K_BEACON_PERIOD;
	intval /= ATH_BCBUF;    /* for staggered beacons */
	nexttbtt = intval;
	intval |= ATH9K_BEACON_RESET_TSF;

	/*
	 * In AP mode we enable the beacon timers and SWBA interrupts to
	 * prepare beacon frames.
	 */
	intval |= ATH9K_BEACON_ENA;
	sc->imask |= ATH9K_INT_SWBA;
	ath_beaconq_config(sc);

	/* Set the computed AP beacon timers */

	ath9k_hw_set_interrupts(sc->sc_ah, 0);
	ath9k_beacon_init(sc, nexttbtt, intval);
	sc->beacon.bmisscnt = 0;
	ath9k_hw_set_interrupts(sc->sc_ah, sc->imask);

	/* Clear the reset TSF flag, so that subsequent beacon updation
	   will not reset the HW TSF. */

	sc->sc_flags &= ~SC_OP_TSF_RESET;
}

/*
 * This sets up the beacon timers according to the timestamp of the last
 * received beacon and the current TSF, configures PCF and DTIM
 * handling, programs the sleep registers so the hardware will wakeup in
 * time to receive beacons, and configures the beacon miss handling so
 * we'll receive a BMISS interrupt when we stop seeing beacons from the AP
 * we've associated with.
 */
static void ath_beacon_config_sta(struct ath_softc *sc,
				  struct ath_beacon_config *conf)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath9k_beacon_state bs;
	int dtimperiod, dtimcount, sleepduration;
	int cfpperiod, cfpcount;
	u32 nexttbtt = 0, intval, tsftu;
	u64 tsf;
	int num_beacons, offset, dtim_dec_count, cfp_dec_count;

	memset(&bs, 0, sizeof(bs));
	intval = conf->beacon_interval & ATH9K_BEACON_PERIOD;

	/*
	 * Setup dtim and cfp parameters according to
	 * last beacon we received (which may be none).
	 */
	dtimperiod = conf->dtim_period;
	if (dtimperiod <= 0)		/* NB: 0 if not known */
		dtimperiod = 1;
	dtimcount = conf->dtim_count;
	if (dtimcount >= dtimperiod)	/* NB: sanity check */
		dtimcount = 0;
	cfpperiod = 1;			/* NB: no PCF support yet */
	cfpcount = 0;

	sleepduration = conf->listen_interval * intval;
	if (sleepduration <= 0)
		sleepduration = intval;

	/*
	 * Pull nexttbtt forward to reflect the current
	 * TSF and calculate dtim+cfp state for the result.
	 */
	tsf = ath9k_hw_gettsf64(sc->sc_ah);
	tsftu = TSF_TO_TU(tsf>>32, tsf) + FUDGE;

	num_beacons = tsftu / intval + 1;
	offset = tsftu % intval;
	nexttbtt = tsftu - offset;
	if (offset)
		nexttbtt += intval;

	/* DTIM Beacon every dtimperiod Beacon */
	dtim_dec_count = num_beacons % dtimperiod;
	/* CFP every cfpperiod DTIM Beacon */
	cfp_dec_count = (num_beacons / dtimperiod) % cfpperiod;
	if (dtim_dec_count)
		cfp_dec_count++;

	dtimcount -= dtim_dec_count;
	if (dtimcount < 0)
		dtimcount += dtimperiod;

	cfpcount -= cfp_dec_count;
	if (cfpcount < 0)
		cfpcount += cfpperiod;

	bs.bs_intval = intval;
	bs.bs_nexttbtt = nexttbtt;
	bs.bs_dtimperiod = dtimperiod*intval;
	bs.bs_nextdtim = bs.bs_nexttbtt + dtimcount*intval;
	bs.bs_cfpperiod = cfpperiod*bs.bs_dtimperiod;
	bs.bs_cfpnext = bs.bs_nextdtim + cfpcount*bs.bs_dtimperiod;
	bs.bs_cfpmaxduration = 0;

	/*
	 * Calculate the number of consecutive beacons to miss* before taking
	 * a BMISS interrupt. The configuration is specified in TU so we only
	 * need calculate based	on the beacon interval.  Note that we clamp the
	 * result to at most 15 beacons.
	 */
	if (sleepduration > intval) {
		bs.bs_bmissthreshold = conf->listen_interval *
			ATH_DEFAULT_BMISS_LIMIT / 2;
	} else {
		bs.bs_bmissthreshold = DIV_ROUND_UP(conf->bmiss_timeout, intval);
		if (bs.bs_bmissthreshold > 15)
			bs.bs_bmissthreshold = 15;
		else if (bs.bs_bmissthreshold <= 0)
			bs.bs_bmissthreshold = 1;
	}

	/*
	 * Calculate sleep duration. The configuration is given in ms.
	 * We ensure a multiple of the beacon period is used. Also, if the sleep
	 * duration is greater than the DTIM period then it makes senses
	 * to make it a multiple of that.
	 *
	 * XXX fixed at 100ms
	 */

	bs.bs_sleepduration = roundup(IEEE80211_MS_TO_TU(100), sleepduration);
	if (bs.bs_sleepduration > bs.bs_dtimperiod)
		bs.bs_sleepduration = bs.bs_dtimperiod;

	/* TSF out of range threshold fixed at 1 second */
	bs.bs_tsfoor_threshold = ATH9K_TSFOOR_THRESHOLD;

	ath_print(common, ATH_DBG_BEACON, "tsf: %llu tsftu: %u\n", tsf, tsftu);
	ath_print(common, ATH_DBG_BEACON,
		  "bmiss: %u sleep: %u cfp-period: %u maxdur: %u next: %u\n",
		  bs.bs_bmissthreshold, bs.bs_sleepduration,
		  bs.bs_cfpperiod, bs.bs_cfpmaxduration, bs.bs_cfpnext);

	/* Set the computed STA beacon timers */

	ath9k_hw_set_interrupts(sc->sc_ah, 0);
	ath9k_hw_set_sta_beacon_timers(sc->sc_ah, &bs);
	sc->imask |= ATH9K_INT_BMISS;
	ath9k_hw_set_interrupts(sc->sc_ah, sc->imask);
}

static void ath_beacon_config_adhoc(struct ath_softc *sc,
				    struct ath_beacon_config *conf,
				    struct ieee80211_vif *vif)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	u64 tsf;
	u32 tsftu, intval, nexttbtt;

	intval = conf->beacon_interval & ATH9K_BEACON_PERIOD;


	/* Pull nexttbtt forward to reflect the current TSF */

	nexttbtt = TSF_TO_TU(sc->beacon.bc_tstamp >> 32, sc->beacon.bc_tstamp);
	if (nexttbtt == 0)
                nexttbtt = intval;
        else if (intval)
                nexttbtt = roundup(nexttbtt, intval);

	tsf = ath9k_hw_gettsf64(sc->sc_ah);
	tsftu = TSF_TO_TU((u32)(tsf>>32), (u32)tsf) + FUDGE;
	do {
		nexttbtt += intval;
	} while (nexttbtt < tsftu);

	ath_print(common, ATH_DBG_BEACON,
		  "IBSS nexttbtt %u intval %u (%u)\n",
		  nexttbtt, intval, conf->beacon_interval);

	/*
	 * In IBSS mode enable the beacon timers but only enable SWBA interrupts
	 * if we need to manually prepare beacon frames.  Otherwise we use a
	 * self-linked tx descriptor and let the hardware deal with things.
	 */
	intval |= ATH9K_BEACON_ENA;
	if (!(sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_VEOL))
		sc->imask |= ATH9K_INT_SWBA;

	ath_beaconq_config(sc);

	/* Set the computed ADHOC beacon timers */

	ath9k_hw_set_interrupts(sc->sc_ah, 0);
	ath9k_beacon_init(sc, nexttbtt, intval);
	sc->beacon.bmisscnt = 0;
	ath9k_hw_set_interrupts(sc->sc_ah, sc->imask);

	/* FIXME: Handle properly when vif is NULL */
	if (vif && sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_VEOL)
		ath_beacon_start_adhoc(sc, vif);
}

void ath_beacon_config(struct ath_softc *sc, struct ieee80211_vif *vif)
{
	struct ath_beacon_config *cur_conf = &sc->cur_beacon_conf;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	enum nl80211_iftype iftype;

	/* Setup the beacon configuration parameters */

	if (vif) {
		struct ieee80211_bss_conf *bss_conf = &vif->bss_conf;

		iftype = vif->type;

		cur_conf->beacon_interval = bss_conf->beacon_int;
		cur_conf->dtim_period = bss_conf->dtim_period;
		cur_conf->listen_interval = 1;
		cur_conf->dtim_count = 1;
		cur_conf->bmiss_timeout =
			ATH_DEFAULT_BMISS_LIMIT * cur_conf->beacon_interval;
	} else {
		iftype = sc->sc_ah->opmode;
	}

	/*
	 * It looks like mac80211 may end up using beacon interval of zero in
	 * some cases (at least for mesh point). Avoid getting into an
	 * infinite loop by using a bit safer value instead. To be safe,
	 * do sanity check on beacon interval for all operating modes.
	 */
	if (cur_conf->beacon_interval == 0)
		cur_conf->beacon_interval = 100;

	switch (iftype) {
	case NL80211_IFTYPE_AP:
		ath_beacon_config_ap(sc, cur_conf);
		break;
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_MESH_POINT:
		ath_beacon_config_adhoc(sc, cur_conf, vif);
		break;
	case NL80211_IFTYPE_STATION:
		ath_beacon_config_sta(sc, cur_conf);
		break;
	default:
		ath_print(common, ATH_DBG_CONFIG,
			  "Unsupported beaconing mode\n");
		return;
	}

	sc->sc_flags |= SC_OP_BEACONS;
}
