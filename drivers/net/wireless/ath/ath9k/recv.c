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

static struct ieee80211_hw * ath_get_virt_hw(struct ath_softc *sc,
					     struct ieee80211_hdr *hdr)
{
	struct ieee80211_hw *hw = sc->pri_wiphy->hw;
	int i;

	spin_lock_bh(&sc->wiphy_lock);
	for (i = 0; i < sc->num_sec_wiphy; i++) {
		struct ath_wiphy *aphy = sc->sec_wiphy[i];
		if (aphy == NULL)
			continue;
		if (compare_ether_addr(hdr->addr1, aphy->hw->wiphy->perm_addr)
		    == 0) {
			hw = aphy->hw;
			break;
		}
	}
	spin_unlock_bh(&sc->wiphy_lock);
	return hw;
}

/*
 * Setup and link descriptors.
 *
 * 11N: we can no longer afford to self link the last descriptor.
 * MAC acknowledges BA status as long as it copies frames to host
 * buffer (or rx fifo). This can incorrectly acknowledge packets
 * to a sender if last desc is self-linked.
 */
static void ath_rx_buf_link(struct ath_softc *sc, struct ath_buf *bf)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_desc *ds;
	struct sk_buff *skb;

	ATH_RXBUF_RESET(bf);

	ds = bf->bf_desc;
	ds->ds_link = 0; /* link to null */
	ds->ds_data = bf->bf_buf_addr;

	/* virtual addr of the beginning of the buffer. */
	skb = bf->bf_mpdu;
	BUG_ON(skb == NULL);
	ds->ds_vdata = skb->data;

	/*
	 * setup rx descriptors. The rx_bufsize here tells the hardware
	 * how much data it can DMA to us and that we are prepared
	 * to process
	 */
	ath9k_hw_setuprxdesc(ah, ds,
			     common->rx_bufsize,
			     0);

	if (sc->rx.rxlink == NULL)
		ath9k_hw_putrxbuf(ah, bf->bf_daddr);
	else
		*sc->rx.rxlink = bf->bf_daddr;

	sc->rx.rxlink = &ds->ds_link;
	ath9k_hw_rxena(ah);
}

static void ath_setdefantenna(struct ath_softc *sc, u32 antenna)
{
	/* XXX block beacon interrupts */
	ath9k_hw_setantenna(sc->sc_ah, antenna);
	sc->rx.defant = antenna;
	sc->rx.rxotherant = 0;
}

static void ath_opmode_init(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);

	u32 rfilt, mfilt[2];

	/* configure rx filter */
	rfilt = ath_calcrxfilter(sc);
	ath9k_hw_setrxfilter(ah, rfilt);

	/* configure bssid mask */
	if (ah->caps.hw_caps & ATH9K_HW_CAP_BSSIDMASK)
		ath_hw_setbssidmask(common);

	/* configure operational mode */
	ath9k_hw_setopmode(ah);

	/* Handle any link-level address change. */
	ath9k_hw_setmac(ah, common->macaddr);

	/* calculate and install multicast filter */
	mfilt[0] = mfilt[1] = ~0;
	ath9k_hw_setmcastfilter(ah, mfilt[0], mfilt[1]);
}

int ath_rx_init(struct ath_softc *sc, int nbufs)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct sk_buff *skb;
	struct ath_buf *bf;
	int error = 0;

	spin_lock_init(&sc->rx.rxflushlock);
	sc->sc_flags &= ~SC_OP_RXFLUSH;
	spin_lock_init(&sc->rx.rxbuflock);

	common->rx_bufsize = roundup(IEEE80211_MAX_MPDU_LEN,
				     min(common->cachelsz, (u16)64));

	ath_print(common, ATH_DBG_CONFIG, "cachelsz %u rxbufsize %u\n",
		  common->cachelsz, common->rx_bufsize);

	/* Initialize rx descriptors */

	error = ath_descdma_setup(sc, &sc->rx.rxdma, &sc->rx.rxbuf,
				  "rx", nbufs, 1);
	if (error != 0) {
		ath_print(common, ATH_DBG_FATAL,
			  "failed to allocate rx descriptors: %d\n", error);
		goto err;
	}

	list_for_each_entry(bf, &sc->rx.rxbuf, list) {
		skb = ath_rxbuf_alloc(common, common->rx_bufsize, GFP_KERNEL);
		if (skb == NULL) {
			error = -ENOMEM;
			goto err;
		}

		bf->bf_mpdu = skb;
		bf->bf_buf_addr = dma_map_single(sc->dev, skb->data,
						 common->rx_bufsize,
						 DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(sc->dev,
					       bf->bf_buf_addr))) {
			dev_kfree_skb_any(skb);
			bf->bf_mpdu = NULL;
			ath_print(common, ATH_DBG_FATAL,
				  "dma_mapping_error() on RX init\n");
			error = -ENOMEM;
			goto err;
		}
		bf->bf_dmacontext = bf->bf_buf_addr;
	}
	sc->rx.rxlink = NULL;

err:
	if (error)
		ath_rx_cleanup(sc);

	return error;
}

void ath_rx_cleanup(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct sk_buff *skb;
	struct ath_buf *bf;

	list_for_each_entry(bf, &sc->rx.rxbuf, list) {
		skb = bf->bf_mpdu;
		if (skb) {
			dma_unmap_single(sc->dev, bf->bf_buf_addr,
					 common->rx_bufsize, DMA_FROM_DEVICE);
			dev_kfree_skb(skb);
		}
	}

	if (sc->rx.rxdma.dd_desc_len != 0)
		ath_descdma_cleanup(sc, &sc->rx.rxdma, &sc->rx.rxbuf);
}

/*
 * Calculate the receive filter according to the
 * operating mode and state:
 *
 * o always accept unicast, broadcast, and multicast traffic
 * o maintain current state of phy error reception (the hal
 *   may enable phy error frames for noise immunity work)
 * o probe request frames are accepted only when operating in
 *   hostap, adhoc, or monitor modes
 * o enable promiscuous mode according to the interface state
 * o accept beacons:
 *   - when operating in adhoc mode so the 802.11 layer creates
 *     node table entries for peers,
 *   - when operating in station mode for collecting rssi data when
 *     the station is otherwise quiet, or
 *   - when operating as a repeater so we see repeater-sta beacons
 *   - when scanning
 */

u32 ath_calcrxfilter(struct ath_softc *sc)
{
#define	RX_FILTER_PRESERVE (ATH9K_RX_FILTER_PHYERR | ATH9K_RX_FILTER_PHYRADAR)

	u32 rfilt;

	rfilt = (ath9k_hw_getrxfilter(sc->sc_ah) & RX_FILTER_PRESERVE)
		| ATH9K_RX_FILTER_UCAST | ATH9K_RX_FILTER_BCAST
		| ATH9K_RX_FILTER_MCAST;

	/* If not a STA, enable processing of Probe Requests */
	if (sc->sc_ah->opmode != NL80211_IFTYPE_STATION)
		rfilt |= ATH9K_RX_FILTER_PROBEREQ;

	/*
	 * Set promiscuous mode when FIF_PROMISC_IN_BSS is enabled for station
	 * mode interface or when in monitor mode. AP mode does not need this
	 * since it receives all in-BSS frames anyway.
	 */
	if (((sc->sc_ah->opmode != NL80211_IFTYPE_AP) &&
	     (sc->rx.rxfilter & FIF_PROMISC_IN_BSS)) ||
	    (sc->sc_ah->opmode == NL80211_IFTYPE_MONITOR))
		rfilt |= ATH9K_RX_FILTER_PROM;

	if (sc->rx.rxfilter & FIF_CONTROL)
		rfilt |= ATH9K_RX_FILTER_CONTROL;

	if ((sc->sc_ah->opmode == NL80211_IFTYPE_STATION) &&
	    !(sc->rx.rxfilter & FIF_BCN_PRBRESP_PROMISC))
		rfilt |= ATH9K_RX_FILTER_MYBEACON;
	else
		rfilt |= ATH9K_RX_FILTER_BEACON;

	if ((AR_SREV_9280_10_OR_LATER(sc->sc_ah) ||
	    AR_SREV_9285_10_OR_LATER(sc->sc_ah)) &&
	    (sc->sc_ah->opmode == NL80211_IFTYPE_AP) &&
	    (sc->rx.rxfilter & FIF_PSPOLL))
		rfilt |= ATH9K_RX_FILTER_PSPOLL;

	if (conf_is_ht(&sc->hw->conf))
		rfilt |= ATH9K_RX_FILTER_COMP_BAR;

	if (sc->sec_wiphy || (sc->rx.rxfilter & FIF_OTHER_BSS)) {
		/* TODO: only needed if more than one BSSID is in use in
		 * station/adhoc mode */
		/* The following may also be needed for other older chips */
		if (sc->sc_ah->hw_version.macVersion == AR_SREV_VERSION_9160)
			rfilt |= ATH9K_RX_FILTER_PROM;
		rfilt |= ATH9K_RX_FILTER_MCAST_BCAST_ALL;
	}

	return rfilt;

#undef RX_FILTER_PRESERVE
}

int ath_startrecv(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_buf *bf, *tbf;

	spin_lock_bh(&sc->rx.rxbuflock);
	if (list_empty(&sc->rx.rxbuf))
		goto start_recv;

	sc->rx.rxlink = NULL;
	list_for_each_entry_safe(bf, tbf, &sc->rx.rxbuf, list) {
		ath_rx_buf_link(sc, bf);
	}

	/* We could have deleted elements so the list may be empty now */
	if (list_empty(&sc->rx.rxbuf))
		goto start_recv;

	bf = list_first_entry(&sc->rx.rxbuf, struct ath_buf, list);
	ath9k_hw_putrxbuf(ah, bf->bf_daddr);
	ath9k_hw_rxena(ah);

start_recv:
	spin_unlock_bh(&sc->rx.rxbuflock);
	ath_opmode_init(sc);
	ath9k_hw_startpcureceive(ah);

	return 0;
}

bool ath_stoprecv(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	bool stopped;

	ath9k_hw_stoppcurecv(ah);
	ath9k_hw_setrxfilter(ah, 0);
	stopped = ath9k_hw_stopdmarecv(ah);
	sc->rx.rxlink = NULL;

	return stopped;
}

void ath_flushrecv(struct ath_softc *sc)
{
	spin_lock_bh(&sc->rx.rxflushlock);
	sc->sc_flags |= SC_OP_RXFLUSH;
	ath_rx_tasklet(sc, 1);
	sc->sc_flags &= ~SC_OP_RXFLUSH;
	spin_unlock_bh(&sc->rx.rxflushlock);
}

static bool ath_beacon_dtim_pending_cab(struct sk_buff *skb)
{
	/* Check whether the Beacon frame has DTIM indicating buffered bc/mc */
	struct ieee80211_mgmt *mgmt;
	u8 *pos, *end, id, elen;
	struct ieee80211_tim_ie *tim;

	mgmt = (struct ieee80211_mgmt *)skb->data;
	pos = mgmt->u.beacon.variable;
	end = skb->data + skb->len;

	while (pos + 2 < end) {
		id = *pos++;
		elen = *pos++;
		if (pos + elen > end)
			break;

		if (id == WLAN_EID_TIM) {
			if (elen < sizeof(*tim))
				break;
			tim = (struct ieee80211_tim_ie *) pos;
			if (tim->dtim_count != 0)
				break;
			return tim->bitmap_ctrl & 0x01;
		}

		pos += elen;
	}

	return false;
}

static void ath_rx_ps_beacon(struct ath_softc *sc, struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	if (skb->len < 24 + 8 + 2 + 2)
		return;

	mgmt = (struct ieee80211_mgmt *)skb->data;
	if (memcmp(common->curbssid, mgmt->bssid, ETH_ALEN) != 0)
		return; /* not from our current AP */

	sc->ps_flags &= ~PS_WAIT_FOR_BEACON;

	if (sc->ps_flags & PS_BEACON_SYNC) {
		sc->ps_flags &= ~PS_BEACON_SYNC;
		ath_print(common, ATH_DBG_PS,
			  "Reconfigure Beacon timers based on "
			  "timestamp from the AP\n");
		ath_beacon_config(sc, NULL);
	}

	if (ath_beacon_dtim_pending_cab(skb)) {
		/*
		 * Remain awake waiting for buffered broadcast/multicast
		 * frames. If the last broadcast/multicast frame is not
		 * received properly, the next beacon frame will work as
		 * a backup trigger for returning into NETWORK SLEEP state,
		 * so we are waiting for it as well.
		 */
		ath_print(common, ATH_DBG_PS, "Received DTIM beacon indicating "
			  "buffered broadcast/multicast frame(s)\n");
		sc->ps_flags |= PS_WAIT_FOR_CAB | PS_WAIT_FOR_BEACON;
		return;
	}

	if (sc->ps_flags & PS_WAIT_FOR_CAB) {
		/*
		 * This can happen if a broadcast frame is dropped or the AP
		 * fails to send a frame indicating that all CAB frames have
		 * been delivered.
		 */
		sc->ps_flags &= ~PS_WAIT_FOR_CAB;
		ath_print(common, ATH_DBG_PS,
			  "PS wait for CAB frames timed out\n");
	}
}

static void ath_rx_ps(struct ath_softc *sc, struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	hdr = (struct ieee80211_hdr *)skb->data;

	/* Process Beacon and CAB receive in PS state */
	if ((sc->ps_flags & PS_WAIT_FOR_BEACON) &&
	    ieee80211_is_beacon(hdr->frame_control))
		ath_rx_ps_beacon(sc, skb);
	else if ((sc->ps_flags & PS_WAIT_FOR_CAB) &&
		 (ieee80211_is_data(hdr->frame_control) ||
		  ieee80211_is_action(hdr->frame_control)) &&
		 is_multicast_ether_addr(hdr->addr1) &&
		 !ieee80211_has_moredata(hdr->frame_control)) {
		/*
		 * No more broadcast/multicast frames to be received at this
		 * point.
		 */
		sc->ps_flags &= ~PS_WAIT_FOR_CAB;
		ath_print(common, ATH_DBG_PS,
			  "All PS CAB frames received, back to sleep\n");
	} else if ((sc->ps_flags & PS_WAIT_FOR_PSPOLL_DATA) &&
		   !is_multicast_ether_addr(hdr->addr1) &&
		   !ieee80211_has_morefrags(hdr->frame_control)) {
		sc->ps_flags &= ~PS_WAIT_FOR_PSPOLL_DATA;
		ath_print(common, ATH_DBG_PS,
			  "Going back to sleep after having received "
			  "PS-Poll data (0x%lx)\n",
			sc->ps_flags & (PS_WAIT_FOR_BEACON |
					PS_WAIT_FOR_CAB |
					PS_WAIT_FOR_PSPOLL_DATA |
					PS_WAIT_FOR_TX_ACK));
	}
}

static void ath_rx_send_to_mac80211(struct ieee80211_hw *hw,
				    struct ath_softc *sc, struct sk_buff *skb,
				    struct ieee80211_rx_status *rxs)
{
	struct ieee80211_hdr *hdr;

	hdr = (struct ieee80211_hdr *)skb->data;

	/* Send the frame to mac80211 */
	if (is_multicast_ether_addr(hdr->addr1)) {
		int i;
		/*
		 * Deliver broadcast/multicast frames to all suitable
		 * virtual wiphys.
		 */
		/* TODO: filter based on channel configuration */
		for (i = 0; i < sc->num_sec_wiphy; i++) {
			struct ath_wiphy *aphy = sc->sec_wiphy[i];
			struct sk_buff *nskb;
			if (aphy == NULL)
				continue;
			nskb = skb_copy(skb, GFP_ATOMIC);
			if (!nskb)
				continue;
			ieee80211_rx(aphy->hw, nskb);
		}
		ieee80211_rx(sc->hw, skb);
	} else
		/* Deliver unicast frames based on receiver address */
		ieee80211_rx(hw, skb);
}

int ath_rx_tasklet(struct ath_softc *sc, int flush)
{
#define PA2DESC(_sc, _pa)                                               \
	((struct ath_desc *)((caddr_t)(_sc)->rx.rxdma.dd_desc +		\
			     ((_pa) - (_sc)->rx.rxdma.dd_desc_paddr)))

	struct ath_buf *bf;
	struct ath_desc *ds;
	struct ath_rx_status *rx_stats;
	struct sk_buff *skb = NULL, *requeue_skb;
	struct ieee80211_rx_status *rxs;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	/*
	 * The hw can techncically differ from common->hw when using ath9k
	 * virtual wiphy so to account for that we iterate over the active
	 * wiphys and find the appropriate wiphy and therefore hw.
	 */
	struct ieee80211_hw *hw = NULL;
	struct ieee80211_hdr *hdr;
	int retval;
	bool decrypt_error = false;

	spin_lock_bh(&sc->rx.rxbuflock);

	do {
		/* If handling rx interrupt and flush is in progress => exit */
		if ((sc->sc_flags & SC_OP_RXFLUSH) && (flush == 0))
			break;

		if (list_empty(&sc->rx.rxbuf)) {
			sc->rx.rxlink = NULL;
			break;
		}

		bf = list_first_entry(&sc->rx.rxbuf, struct ath_buf, list);
		ds = bf->bf_desc;

		/*
		 * Must provide the virtual address of the current
		 * descriptor, the physical address, and the virtual
		 * address of the next descriptor in the h/w chain.
		 * This allows the HAL to look ahead to see if the
		 * hardware is done with a descriptor by checking the
		 * done bit in the following descriptor and the address
		 * of the current descriptor the DMA engine is working
		 * on.  All this is necessary because of our use of
		 * a self-linked list to avoid rx overruns.
		 */
		retval = ath9k_hw_rxprocdesc(ah, ds,
					     bf->bf_daddr,
					     PA2DESC(sc, ds->ds_link),
					     0);
		if (retval == -EINPROGRESS) {
			struct ath_buf *tbf;
			struct ath_desc *tds;

			if (list_is_last(&bf->list, &sc->rx.rxbuf)) {
				sc->rx.rxlink = NULL;
				break;
			}

			tbf = list_entry(bf->list.next, struct ath_buf, list);

			/*
			 * On some hardware the descriptor status words could
			 * get corrupted, including the done bit. Because of
			 * this, check if the next descriptor's done bit is
			 * set or not.
			 *
			 * If the next descriptor's done bit is set, the current
			 * descriptor has been corrupted. Force s/w to discard
			 * this descriptor and continue...
			 */

			tds = tbf->bf_desc;
			retval = ath9k_hw_rxprocdesc(ah, tds, tbf->bf_daddr,
					     PA2DESC(sc, tds->ds_link), 0);
			if (retval == -EINPROGRESS) {
				break;
			}
		}

		skb = bf->bf_mpdu;
		if (!skb)
			continue;

		/*
		 * Synchronize the DMA transfer with CPU before
		 * 1. accessing the frame
		 * 2. requeueing the same buffer to h/w
		 */
		dma_sync_single_for_cpu(sc->dev, bf->bf_buf_addr,
				common->rx_bufsize,
				DMA_FROM_DEVICE);

		hdr = (struct ieee80211_hdr *) skb->data;
		rxs =  IEEE80211_SKB_RXCB(skb);

		hw = ath_get_virt_hw(sc, hdr);
		rx_stats = &ds->ds_rxstat;

		ath_debug_stat_rx(sc, bf);

		/*
		 * If we're asked to flush receive queue, directly
		 * chain it back at the queue without processing it.
		 */
		if (flush)
			goto requeue;

		retval = ath9k_cmn_rx_skb_preprocess(common, hw, skb, rx_stats,
						     rxs, &decrypt_error);
		if (retval)
			goto requeue;

		/* Ensure we always have an skb to requeue once we are done
		 * processing the current buffer's skb */
		requeue_skb = ath_rxbuf_alloc(common, common->rx_bufsize, GFP_ATOMIC);

		/* If there is no memory we ignore the current RX'd frame,
		 * tell hardware it can give us a new frame using the old
		 * skb and put it at the tail of the sc->rx.rxbuf list for
		 * processing. */
		if (!requeue_skb)
			goto requeue;

		/* Unmap the frame */
		dma_unmap_single(sc->dev, bf->bf_buf_addr,
				 common->rx_bufsize,
				 DMA_FROM_DEVICE);

		skb_put(skb, rx_stats->rs_datalen);

		ath9k_cmn_rx_skb_postprocess(common, skb, rx_stats,
					     rxs, decrypt_error);

		/* We will now give hardware our shiny new allocated skb */
		bf->bf_mpdu = requeue_skb;
		bf->bf_buf_addr = dma_map_single(sc->dev, requeue_skb->data,
						 common->rx_bufsize,
						 DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(sc->dev,
			  bf->bf_buf_addr))) {
			dev_kfree_skb_any(requeue_skb);
			bf->bf_mpdu = NULL;
			ath_print(common, ATH_DBG_FATAL,
				  "dma_mapping_error() on RX\n");
			ath_rx_send_to_mac80211(hw, sc, skb, rxs);
			break;
		}
		bf->bf_dmacontext = bf->bf_buf_addr;

		/*
		 * change the default rx antenna if rx diversity chooses the
		 * other antenna 3 times in a row.
		 */
		if (sc->rx.defant != ds->ds_rxstat.rs_antenna) {
			if (++sc->rx.rxotherant >= 3)
				ath_setdefantenna(sc, rx_stats->rs_antenna);
		} else {
			sc->rx.rxotherant = 0;
		}

		if (unlikely(sc->ps_flags & (PS_WAIT_FOR_BEACON |
					     PS_WAIT_FOR_CAB |
					     PS_WAIT_FOR_PSPOLL_DATA)))
			ath_rx_ps(sc, skb);

		ath_rx_send_to_mac80211(hw, sc, skb, rxs);

requeue:
		list_move_tail(&bf->list, &sc->rx.rxbuf);
		ath_rx_buf_link(sc, bf);
	} while (1);

	spin_unlock_bh(&sc->rx.rxbuflock);

	return 0;
#undef PA2DESC
}
