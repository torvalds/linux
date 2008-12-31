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

#include "core.h"

/*
 *  This function will modify certain transmit queue properties depending on
 *  the operating mode of the station (AP or AdHoc).  Parameters are AIFS
 *  settings and channel width min/max
*/
static int ath_beaconq_config(struct ath_softc *sc)
{
	struct ath_hal *ah = sc->sc_ah;
	struct ath9k_tx_queue_info qi;

	ath9k_hw_get_txq_props(ah, sc->beacon.beaconq, &qi);
	if (sc->sc_ah->ah_opmode == NL80211_IFTYPE_AP) {
		/* Always burst out beacon and CAB traffic. */
		qi.tqi_aifs = 1;
		qi.tqi_cwmin = 0;
		qi.tqi_cwmax = 0;
	} else {
		/* Adhoc mode; important thing is to use 2x cwmin. */
		qi.tqi_aifs = sc->beacon.beacon_qi.tqi_aifs;
		qi.tqi_cwmin = 2*sc->beacon.beacon_qi.tqi_cwmin;
		qi.tqi_cwmax = sc->beacon.beacon_qi.tqi_cwmax;
	}

	if (!ath9k_hw_set_txq_props(ah, sc->beacon.beaconq, &qi)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"unable to update h/w beacon queue parameters\n");
		return 0;
	} else {
		ath9k_hw_resettxqueue(ah, sc->beacon.beaconq); /* push to h/w */
		return 1;
	}
}

static void ath_bstuck_process(struct ath_softc *sc)
{
	DPRINTF(sc, ATH_DBG_BEACON,
		"stuck beacon; resetting (bmiss count %u)\n",
		sc->beacon.bmisscnt);
	ath_reset(sc, false);
}

/*
 *  Associates the beacon frame buffer with a transmit descriptor.  Will set
 *  up all required antenna switch parameters, rate codes, and channel flags.
 *  Beacons are always sent out at the lowest rate, and are not retried.
*/
static void ath_beacon_setup(struct ath_softc *sc,
			     struct ath_vap *avp, struct ath_buf *bf)
{
	struct sk_buff *skb = (struct sk_buff *)bf->bf_mpdu;
	struct ath_hal *ah = sc->sc_ah;
	struct ath_desc *ds;
	struct ath9k_11n_rate_series series[4];
	struct ath_rate_table *rt;
	int flags, antenna;
	u8 rix, rate;
	int ctsrate = 0;
	int ctsduration = 0;

	DPRINTF(sc, ATH_DBG_BEACON, "m %p len %u\n", skb, skb->len);

	/* setup descriptors */
	ds = bf->bf_desc;

	flags = ATH9K_TXDESC_NOACK;

	if (sc->sc_ah->ah_opmode == NL80211_IFTYPE_ADHOC &&
	    (ah->ah_caps.hw_caps & ATH9K_HW_CAP_VEOL)) {
		ds->ds_link = bf->bf_daddr; /* self-linked */
		flags |= ATH9K_TXDESC_VEOL;
		/* Let hardware handle antenna switching. */
		antenna = 0;
	} else {
		ds->ds_link = 0;
		/*
		 * Switch antenna every beacon.
		 * Should only switch every beacon period, not for every
		 * SWBA's
		 * XXX assumes two antenna
		 */
		antenna = ((sc->beacon.ast_be_xmit / sc->sc_nbcnvaps) & 1 ? 2 : 1);
	}

	ds->ds_data = bf->bf_buf_addr;

	/*
	 * Calculate rate code.
	 * XXX everything at min xmit rate
	 */
	rix = 0;
	rt = sc->cur_rate_table;
	rate = rt->info[rix].ratecode;
	if (sc->sc_flags & SC_OP_PREAMBLE_SHORT)
		rate |= rt->info[rix].short_preamble;

	ath9k_hw_set11n_txdesc(ah, ds,
			       skb->len + FCS_LEN,     /* frame length */
			       ATH9K_PKT_TYPE_BEACON,  /* Atheros packet type */
			       MAX_RATE_POWER,         /* FIXME */
			       ATH9K_TXKEYIX_INVALID,  /* no encryption */
			       ATH9K_KEY_TYPE_CLEAR,   /* no encryption */
			       flags                   /* no ack,
							  veol for beacons */
		);

	/* NB: beacon's BufLen must be a multiple of 4 bytes */
	ath9k_hw_filltxdesc(ah, ds,
			    roundup(skb->len, 4), /* buffer length */
			    true,                 /* first segment */
			    true,                 /* last segment */
			    ds                    /* first descriptor */
		);

	memset(series, 0, sizeof(struct ath9k_11n_rate_series) * 4);
	series[0].Tries = 1;
	series[0].Rate = rate;
	series[0].ChSel = sc->sc_tx_chainmask;
	series[0].RateFlags = (ctsrate) ? ATH9K_RATESERIES_RTS_CTS : 0;
	ath9k_hw_set11n_ratescenario(ah, ds, ds, 0,
		ctsrate, ctsduration, series, 4, 0);
}

/* Generate beacon frame and queue cab data for a vap */
static struct ath_buf *ath_beacon_generate(struct ath_softc *sc, int if_id)
{
	struct ath_buf *bf;
	struct ath_vap *avp;
	struct sk_buff *skb;
	struct ath_txq *cabq;
	struct ieee80211_vif *vif;
	struct ieee80211_tx_info *info;
	int cabq_depth;

	vif = sc->sc_vaps[if_id];
	ASSERT(vif);

	avp = (void *)vif->drv_priv;
	cabq = sc->beacon.cabq;

	if (avp->av_bcbuf == NULL) {
		DPRINTF(sc, ATH_DBG_BEACON, "avp=%p av_bcbuf=%p\n",
			avp, avp->av_bcbuf);
		return NULL;
	}

	bf = avp->av_bcbuf;
	skb = (struct sk_buff *)bf->bf_mpdu;
	if (skb) {
		pci_unmap_single(sc->pdev, bf->bf_dmacontext,
				 skb->len,
				 PCI_DMA_TODEVICE);
		dev_kfree_skb_any(skb);
	}

	skb = ieee80211_beacon_get(sc->hw, vif);
	bf->bf_mpdu = skb;
	if (skb == NULL)
		return NULL;

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
		pci_map_single(sc->pdev, skb->data,
			       skb->len,
			       PCI_DMA_TODEVICE);
	if (unlikely(pci_dma_mapping_error(sc->pdev, bf->bf_buf_addr))) {
		dev_kfree_skb_any(skb);
		bf->bf_mpdu = NULL;
		DPRINTF(sc, ATH_DBG_CONFIG,
			"pci_dma_mapping_error() on beaconing\n");
		return NULL;
	}

	skb = ieee80211_get_buffered_bc(sc->hw, vif);

	/*
	 * if the CABQ traffic from previous DTIM is pending and the current
	 *  beacon is also a DTIM.
	 *  1) if there is only one vap let the cab traffic continue.
	 *  2) if there are more than one vap and we are using staggered
	 *     beacons, then drain the cabq by dropping all the frames in
	 *     the cabq so that the current vaps cab traffic can be scheduled.
	 */
	spin_lock_bh(&cabq->axq_lock);
	cabq_depth = cabq->axq_depth;
	spin_unlock_bh(&cabq->axq_lock);

	if (skb && cabq_depth) {
		/*
		 * Unlock the cabq lock as ath_tx_draintxq acquires
		 * the lock again which is a common function and that
		 * acquires txq lock inside.
		 */
		if (sc->sc_nvaps > 1) {
			ath_tx_draintxq(sc, cabq, false);
			DPRINTF(sc, ATH_DBG_BEACON,
				"flush previous cabq traffic\n");
		}
	}

	/* Construct tx descriptor. */
	ath_beacon_setup(sc, avp, bf);

	/*
	 * Enable the CAB queue before the beacon queue to
	 * insure cab frames are triggered by this beacon.
	 */
	while (skb) {
		ath_tx_cabq(sc, skb);
		skb = ieee80211_get_buffered_bc(sc->hw, vif);
	}

	return bf;
}

/*
 * Startup beacon transmission for adhoc mode when they are sent entirely
 * by the hardware using the self-linked descriptor + veol trick.
*/
static void ath_beacon_start_adhoc(struct ath_softc *sc, int if_id)
{
	struct ieee80211_vif *vif;
	struct ath_hal *ah = sc->sc_ah;
	struct ath_buf *bf;
	struct ath_vap *avp;
	struct sk_buff *skb;

	vif = sc->sc_vaps[if_id];
	ASSERT(vif);

	avp = (void *)vif->drv_priv;

	if (avp->av_bcbuf == NULL) {
		DPRINTF(sc, ATH_DBG_BEACON, "avp=%p av_bcbuf=%p\n",
			avp, avp != NULL ? avp->av_bcbuf : NULL);
		return;
	}
	bf = avp->av_bcbuf;
	skb = (struct sk_buff *) bf->bf_mpdu;

	/* Construct tx descriptor. */
	ath_beacon_setup(sc, avp, bf);

	/* NB: caller is known to have already stopped tx dma */
	ath9k_hw_puttxbuf(ah, sc->beacon.beaconq, bf->bf_daddr);
	ath9k_hw_txstart(ah, sc->beacon.beaconq);
	DPRINTF(sc, ATH_DBG_BEACON, "TXDP%u = %llx (%p)\n",
		sc->beacon.beaconq, ito64(bf->bf_daddr), bf->bf_desc);
}

int ath_beaconq_setup(struct ath_hal *ah)
{
	struct ath9k_tx_queue_info qi;

	memset(&qi, 0, sizeof(qi));
	qi.tqi_aifs = 1;
	qi.tqi_cwmin = 0;
	qi.tqi_cwmax = 0;
	/* NB: don't enable any interrupts */
	return ath9k_hw_setuptxqueue(ah, ATH9K_TX_QUEUE_BEACON, &qi);
}

int ath_beacon_alloc(struct ath_softc *sc, int if_id)
{
	struct ieee80211_vif *vif;
	struct ath_vap *avp;
	struct ieee80211_hdr *hdr;
	struct ath_buf *bf;
	struct sk_buff *skb;
	__le64 tstamp;

	vif = sc->sc_vaps[if_id];
	ASSERT(vif);

	avp = (void *)vif->drv_priv;

	/* Allocate a beacon descriptor if we haven't done so. */
	if (!avp->av_bcbuf) {
		/* Allocate beacon state for hostap/ibss.  We know
		 * a buffer is available. */
		avp->av_bcbuf = list_first_entry(&sc->beacon.bbuf,
						 struct ath_buf, list);
		list_del(&avp->av_bcbuf->list);

		if (sc->sc_ah->ah_opmode == NL80211_IFTYPE_AP ||
		    !(sc->sc_ah->ah_caps.hw_caps & ATH9K_HW_CAP_VEOL)) {
			int slot;
			/*
			 * Assign the vap to a beacon xmit slot. As
			 * above, this cannot fail to find one.
			 */
			avp->av_bslot = 0;
			for (slot = 0; slot < ATH_BCBUF; slot++)
				if (sc->beacon.bslot[slot] == ATH_IF_ID_ANY) {
					/*
					 * XXX hack, space out slots to better
					 * deal with misses
					 */
					if (slot+1 < ATH_BCBUF &&
					    sc->beacon.bslot[slot+1] ==
						ATH_IF_ID_ANY) {
						avp->av_bslot = slot+1;
						break;
					}
					avp->av_bslot = slot;
					/* NB: keep looking for a double slot */
				}
			BUG_ON(sc->beacon.bslot[avp->av_bslot] != ATH_IF_ID_ANY);
			sc->beacon.bslot[avp->av_bslot] = if_id;
			sc->sc_nbcnvaps++;
		}
	}

	/* release the previous beacon frame , if it already exists. */
	bf = avp->av_bcbuf;
	if (bf->bf_mpdu != NULL) {
		skb = (struct sk_buff *)bf->bf_mpdu;
		pci_unmap_single(sc->pdev, bf->bf_dmacontext,
				 skb->len,
				 PCI_DMA_TODEVICE);
		dev_kfree_skb_any(skb);
		bf->bf_mpdu = NULL;
	}

	/*
	 * NB: the beacon data buffer must be 32-bit aligned.
	 * FIXME: Fill avp->av_btxctl.txpower and
	 * avp->av_btxctl.shortPreamble
	 */
	skb = ieee80211_beacon_get(sc->hw, vif);
	if (skb == NULL) {
		DPRINTF(sc, ATH_DBG_BEACON, "cannot get skb\n");
		return -ENOMEM;
	}

	tstamp = ((struct ieee80211_mgmt *)skb->data)->u.beacon.timestamp;
	sc->beacon.bc_tstamp = le64_to_cpu(tstamp);

	/*
	 * Calculate a TSF adjustment factor required for
	 * staggered beacons.  Note that we assume the format
	 * of the beacon frame leaves the tstamp field immediately
	 * following the header.
	 */
	if (avp->av_bslot > 0) {
		u64 tsfadjust;
		__le64 val;
		int intval;

		intval = sc->hw->conf.beacon_int ?
			sc->hw->conf.beacon_int : ATH_DEFAULT_BINTVAL;

		/*
		 * The beacon interval is in TU's; the TSF in usecs.
		 * We figure out how many TU's to add to align the
		 * timestamp then convert to TSF units and handle
		 * byte swapping before writing it in the frame.
		 * The hardware will then add this each time a beacon
		 * frame is sent.  Note that we align vap's 1..N
		 * and leave vap 0 untouched.  This means vap 0
		 * has a timestamp in one beacon interval while the
		 * others get a timestamp aligned to the next interval.
		 */
		tsfadjust = (intval * (ATH_BCBUF - avp->av_bslot)) / ATH_BCBUF;
		val = cpu_to_le64(tsfadjust << 10);     /* TU->TSF */

		DPRINTF(sc, ATH_DBG_BEACON,
			"stagger beacons, bslot %d intval %u tsfadjust %llu\n",
			avp->av_bslot, intval, (unsigned long long)tsfadjust);

		hdr = (struct ieee80211_hdr *)skb->data;
		memcpy(&hdr[1], &val, sizeof(val));
	}

	bf->bf_mpdu = skb;
	bf->bf_buf_addr = bf->bf_dmacontext =
		pci_map_single(sc->pdev, skb->data,
			       skb->len,
			       PCI_DMA_TODEVICE);
	if (unlikely(pci_dma_mapping_error(sc->pdev, bf->bf_buf_addr))) {
		dev_kfree_skb_any(skb);
		bf->bf_mpdu = NULL;
		DPRINTF(sc, ATH_DBG_CONFIG,
			"pci_dma_mapping_error() on beacon alloc\n");
		return -ENOMEM;
	}

	return 0;
}

void ath_beacon_return(struct ath_softc *sc, struct ath_vap *avp)
{
	if (avp->av_bcbuf != NULL) {
		struct ath_buf *bf;

		if (avp->av_bslot != -1) {
			sc->beacon.bslot[avp->av_bslot] = ATH_IF_ID_ANY;
			sc->sc_nbcnvaps--;
		}

		bf = avp->av_bcbuf;
		if (bf->bf_mpdu != NULL) {
			struct sk_buff *skb = (struct sk_buff *)bf->bf_mpdu;
			pci_unmap_single(sc->pdev, bf->bf_dmacontext,
					 skb->len,
					 PCI_DMA_TODEVICE);
			dev_kfree_skb_any(skb);
			bf->bf_mpdu = NULL;
		}
		list_add_tail(&bf->list, &sc->beacon.bbuf);

		avp->av_bcbuf = NULL;
	}
}

void ath9k_beacon_tasklet(unsigned long data)
{
	struct ath_softc *sc = (struct ath_softc *)data;
	struct ath_hal *ah = sc->sc_ah;
	struct ath_buf *bf = NULL;
	int slot, if_id;
	u32 bfaddr;
	u32 rx_clear = 0, rx_frame = 0, tx_frame = 0;
	u32 show_cycles = 0;
	u32 bc = 0; /* beacon count */
	u64 tsf;
	u32 tsftu;
	u16 intval;

	if (sc->sc_flags & SC_OP_NO_RESET) {
		show_cycles = ath9k_hw_GetMibCycleCountsPct(ah,
					    &rx_clear, &rx_frame, &tx_frame);
	}

	/*
	 * Check if the previous beacon has gone out.  If
	 * not don't try to post another, skip this period
	 * and wait for the next.  Missed beacons indicate
	 * a problem and should not occur.  If we miss too
	 * many consecutive beacons reset the device.
	 *
	 * FIXME: Clean up this mess !!
	 */
	if (ath9k_hw_numtxpending(ah, sc->beacon.beaconq) != 0) {
		sc->beacon.bmisscnt++;
		/* XXX: doth needs the chanchange IE countdown decremented.
		 *      We should consider adding a mac80211 call to indicate
		 *      a beacon miss so appropriate action could be taken
		 *      (in that layer).
		 */
		if (sc->beacon.bmisscnt < BSTUCK_THRESH) {
			if (sc->sc_flags & SC_OP_NO_RESET) {
				DPRINTF(sc, ATH_DBG_BEACON,
					"missed %u consecutive beacons\n",
					sc->beacon.bmisscnt);
				if (show_cycles) {
					/*
					 * Display cycle counter stats from HW
					 * to aide in debug of stickiness.
					 */
					DPRINTF(sc, ATH_DBG_BEACON,
						"busy times: rx_clear=%d, "
						"rx_frame=%d, tx_frame=%d\n",
						rx_clear, rx_frame,
						tx_frame);
				} else {
					DPRINTF(sc, ATH_DBG_BEACON,
						"unable to obtain "
						"busy times\n");
				}
			} else {
				DPRINTF(sc, ATH_DBG_BEACON,
					"missed %u consecutive beacons\n",
					sc->beacon.bmisscnt);
			}
		} else if (sc->beacon.bmisscnt >= BSTUCK_THRESH) {
			if (sc->sc_flags & SC_OP_NO_RESET) {
				if (sc->beacon.bmisscnt == BSTUCK_THRESH) {
					DPRINTF(sc, ATH_DBG_BEACON,
						"beacon is officially "
						"stuck\n");
				}
			} else {
				DPRINTF(sc, ATH_DBG_BEACON,
					"beacon is officially stuck\n");
				ath_bstuck_process(sc);
			}
		}
		return;
	}

	if (sc->beacon.bmisscnt != 0) {
		if (sc->sc_flags & SC_OP_NO_RESET) {
			DPRINTF(sc, ATH_DBG_BEACON,
				"resume beacon xmit after %u misses\n",
				sc->beacon.bmisscnt);
		} else {
			DPRINTF(sc, ATH_DBG_BEACON,
				"resume beacon xmit after %u misses\n",
				sc->beacon.bmisscnt);
		}
		sc->beacon.bmisscnt = 0;
	}

	/*
	 * Generate beacon frames. we are sending frames
	 * staggered so calculate the slot for this frame based
	 * on the tsf to safeguard against missing an swba.
	 */

	intval = sc->hw->conf.beacon_int ?
		sc->hw->conf.beacon_int : ATH_DEFAULT_BINTVAL;

	tsf = ath9k_hw_gettsf64(ah);
	tsftu = TSF_TO_TU(tsf>>32, tsf);
	slot = ((tsftu % intval) * ATH_BCBUF) / intval;
	if_id = sc->beacon.bslot[(slot + 1) % ATH_BCBUF];

	DPRINTF(sc, ATH_DBG_BEACON,
		"slot %d [tsf %llu tsftu %u intval %u] if_id %d\n",
		slot, (unsigned long long)tsf, tsftu,
		intval, if_id);

	bfaddr = 0;
	if (if_id != ATH_IF_ID_ANY) {
		bf = ath_beacon_generate(sc, if_id);
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
	/* XXX locking */
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
			DPRINTF(sc, ATH_DBG_FATAL,
				"beacon queue %u did not stop?\n", sc->beacon.beaconq);
			/* NB: the HAL still stops DMA, so proceed */
		}

		/* NB: cabq traffic should already be queued and primed */
		ath9k_hw_puttxbuf(ah, sc->beacon.beaconq, bfaddr);
		ath9k_hw_txstart(ah, sc->beacon.beaconq);

		sc->beacon.ast_be_xmit += bc;     /* XXX per-vap? */
	}
}

/*
 * Configure the beacon and sleep timers.
 *
 * When operating as an AP this resets the TSF and sets
 * up the hardware to notify us when we need to issue beacons.
 *
 * When operating in station mode this sets up the beacon
 * timers according to the timestamp of the last received
 * beacon and the current TSF, configures PCF and DTIM
 * handling, programs the sleep registers so the hardware
 * will wakeup in time to receive beacons, and configures
 * the beacon miss handling so we'll receive a BMISS
 * interrupt when we stop seeing beacons from the AP
 * we've associated with.
 */
void ath_beacon_config(struct ath_softc *sc, int if_id)
{
	struct ieee80211_vif *vif;
	struct ath_hal *ah = sc->sc_ah;
	struct ath_beacon_config conf;
	struct ath_vap *avp;
	enum nl80211_iftype opmode;
	u32 nexttbtt, intval;

	if (if_id != ATH_IF_ID_ANY) {
		vif = sc->sc_vaps[if_id];
		ASSERT(vif);
		avp = (void *)vif->drv_priv;
		opmode = avp->av_opmode;
	} else {
		opmode = sc->sc_ah->ah_opmode;
	}

	memset(&conf, 0, sizeof(struct ath_beacon_config));

	conf.beacon_interval = sc->hw->conf.beacon_int ?
		sc->hw->conf.beacon_int : ATH_DEFAULT_BINTVAL;
	conf.listen_interval = 1;
	conf.dtim_period = conf.beacon_interval;
	conf.dtim_count = 1;
	conf.bmiss_timeout = ATH_DEFAULT_BMISS_LIMIT * conf.beacon_interval;

	/* extract tstamp from last beacon and convert to TU */
	nexttbtt = TSF_TO_TU(sc->beacon.bc_tstamp >> 32, sc->beacon.bc_tstamp);

	/* XXX conditionalize multi-bss support? */
	if (sc->sc_ah->ah_opmode == NL80211_IFTYPE_AP) {
		/*
		 * For multi-bss ap support beacons are either staggered
		 * evenly over N slots or burst together.  For the former
		 * arrange for the SWBA to be delivered for each slot.
		 * Slots that are not occupied will generate nothing.
		 */
		/* NB: the beacon interval is kept internally in TU's */
		intval = conf.beacon_interval & ATH9K_BEACON_PERIOD;
		intval /= ATH_BCBUF;    /* for staggered beacons */
	} else {
		intval = conf.beacon_interval & ATH9K_BEACON_PERIOD;
	}

	if (nexttbtt == 0)	/* e.g. for ap mode */
		nexttbtt = intval;
	else if (intval)	/* NB: can be 0 for monitor mode */
		nexttbtt = roundup(nexttbtt, intval);

	DPRINTF(sc, ATH_DBG_BEACON, "nexttbtt %u intval %u (%u)\n",
		nexttbtt, intval, conf.beacon_interval);

	/* Check for NL80211_IFTYPE_AP and sc_nostabeacons for WDS client */
	if (sc->sc_ah->ah_opmode == NL80211_IFTYPE_STATION) {
		struct ath9k_beacon_state bs;
		u64 tsf;
		u32 tsftu;
		int dtimperiod, dtimcount, sleepduration;
		int cfpperiod, cfpcount;

		/*
		 * Setup dtim and cfp parameters according to
		 * last beacon we received (which may be none).
		 */
		dtimperiod = conf.dtim_period;
		if (dtimperiod <= 0)		/* NB: 0 if not known */
			dtimperiod = 1;
		dtimcount = conf.dtim_count;
		if (dtimcount >= dtimperiod)	/* NB: sanity check */
			dtimcount = 0;
		cfpperiod = 1;			/* NB: no PCF support yet */
		cfpcount = 0;

		sleepduration = conf.listen_interval * intval;
		if (sleepduration <= 0)
			sleepduration = intval;

#define FUDGE 2
		/*
		 * Pull nexttbtt forward to reflect the current
		 * TSF and calculate dtim+cfp state for the result.
		 */
		tsf = ath9k_hw_gettsf64(ah);
		tsftu = TSF_TO_TU(tsf>>32, tsf) + FUDGE;
		do {
			nexttbtt += intval;
			if (--dtimcount < 0) {
				dtimcount = dtimperiod - 1;
				if (--cfpcount < 0)
					cfpcount = cfpperiod - 1;
			}
		} while (nexttbtt < tsftu);
#undef FUDGE
		memset(&bs, 0, sizeof(bs));
		bs.bs_intval = intval;
		bs.bs_nexttbtt = nexttbtt;
		bs.bs_dtimperiod = dtimperiod*intval;
		bs.bs_nextdtim = bs.bs_nexttbtt + dtimcount*intval;
		bs.bs_cfpperiod = cfpperiod*bs.bs_dtimperiod;
		bs.bs_cfpnext = bs.bs_nextdtim + cfpcount*bs.bs_dtimperiod;
		bs.bs_cfpmaxduration = 0;

		/*
		 * Calculate the number of consecutive beacons to miss
		 * before taking a BMISS interrupt.  The configuration
		 * is specified in TU so we only need calculate based
		 * on the beacon interval.  Note that we clamp the
		 * result to at most 15 beacons.
		 */
		if (sleepduration > intval) {
			bs.bs_bmissthreshold = conf.listen_interval *
				ATH_DEFAULT_BMISS_LIMIT / 2;
		} else {
			bs.bs_bmissthreshold =
				DIV_ROUND_UP(conf.bmiss_timeout, intval);
			if (bs.bs_bmissthreshold > 15)
				bs.bs_bmissthreshold = 15;
			else if (bs.bs_bmissthreshold <= 0)
				bs.bs_bmissthreshold = 1;
		}

		/*
		 * Calculate sleep duration.  The configuration is
		 * given in ms.  We insure a multiple of the beacon
		 * period is used.  Also, if the sleep duration is
		 * greater than the DTIM period then it makes senses
		 * to make it a multiple of that.
		 *
		 * XXX fixed at 100ms
		 */

		bs.bs_sleepduration = roundup(IEEE80211_MS_TO_TU(100),
					      sleepduration);
		if (bs.bs_sleepduration > bs.bs_dtimperiod)
			bs.bs_sleepduration = bs.bs_dtimperiod;

		DPRINTF(sc, ATH_DBG_BEACON,
			"tsf %llu "
			"tsf:tu %u "
			"intval %u "
			"nexttbtt %u "
			"dtim %u "
			"nextdtim %u "
			"bmiss %u "
			"sleep %u "
			"cfp:period %u "
			"maxdur %u "
			"next %u "
			"timoffset %u\n",
			(unsigned long long)tsf, tsftu,
			bs.bs_intval,
			bs.bs_nexttbtt,
			bs.bs_dtimperiod,
			bs.bs_nextdtim,
			bs.bs_bmissthreshold,
			bs.bs_sleepduration,
			bs.bs_cfpperiod,
			bs.bs_cfpmaxduration,
			bs.bs_cfpnext,
			bs.bs_timoffset
			);

		ath9k_hw_set_interrupts(ah, 0);
		ath9k_hw_set_sta_beacon_timers(ah, &bs);
		sc->sc_imask |= ATH9K_INT_BMISS;
		ath9k_hw_set_interrupts(ah, sc->sc_imask);
	} else {
		u64 tsf;
		u32 tsftu;
		ath9k_hw_set_interrupts(ah, 0);
		if (nexttbtt == intval)
			intval |= ATH9K_BEACON_RESET_TSF;
		if (sc->sc_ah->ah_opmode == NL80211_IFTYPE_ADHOC) {
			/*
			 * Pull nexttbtt forward to reflect the current
			 * TSF
			 */
#define FUDGE 2
			if (!(intval & ATH9K_BEACON_RESET_TSF)) {
				tsf = ath9k_hw_gettsf64(ah);
				tsftu = TSF_TO_TU((u32)(tsf>>32),
					(u32)tsf) + FUDGE;
				do {
					nexttbtt += intval;
				} while (nexttbtt < tsftu);
			}
#undef FUDGE
			DPRINTF(sc, ATH_DBG_BEACON,
				"IBSS nexttbtt %u intval %u (%u)\n",
				nexttbtt,
				intval & ~ATH9K_BEACON_RESET_TSF,
				conf.beacon_interval);

			/*
			 * In IBSS mode enable the beacon timers but only
			 * enable SWBA interrupts if we need to manually
			 * prepare beacon frames.  Otherwise we use a
			 * self-linked tx descriptor and let the hardware
			 * deal with things.
			 */
			intval |= ATH9K_BEACON_ENA;
			if (!(ah->ah_caps.hw_caps & ATH9K_HW_CAP_VEOL))
				sc->sc_imask |= ATH9K_INT_SWBA;
			ath_beaconq_config(sc);
		} else if (sc->sc_ah->ah_opmode == NL80211_IFTYPE_AP) {
			/*
			 * In AP mode we enable the beacon timers and
			 * SWBA interrupts to prepare beacon frames.
			 */
			intval |= ATH9K_BEACON_ENA;
			sc->sc_imask |= ATH9K_INT_SWBA;   /* beacon prepare */
			ath_beaconq_config(sc);
		}
		ath9k_hw_beaconinit(ah, nexttbtt, intval);
		sc->beacon.bmisscnt = 0;
		ath9k_hw_set_interrupts(ah, sc->sc_imask);
		/*
		 * When using a self-linked beacon descriptor in
		 * ibss mode load it once here.
		 */
		if (sc->sc_ah->ah_opmode == NL80211_IFTYPE_ADHOC &&
		    (ah->ah_caps.hw_caps & ATH9K_HW_CAP_VEOL))
			ath_beacon_start_adhoc(sc, 0);
	}
}

void ath_beacon_sync(struct ath_softc *sc, int if_id)
{
	/*
	 * Resync beacon timers using the tsf of the
	 * beacon frame we just received.
	 */
	ath_beacon_config(sc, if_id);
	sc->sc_flags |= SC_OP_BEACONS;
}
