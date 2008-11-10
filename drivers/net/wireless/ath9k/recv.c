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
 * Implementation of receive path.
 */

#include "core.h"

/*
 * Setup and link descriptors.
 *
 * 11N: we can no longer afford to self link the last descriptor.
 * MAC acknowledges BA status as long as it copies frames to host
 * buffer (or rx fifo). This can incorrectly acknowledge packets
 * to a sender if last desc is self-linked.
 *
 * NOTE: Caller should hold the rxbuf lock.
 */

static void ath_rx_buf_link(struct ath_softc *sc, struct ath_buf *bf)
{
	struct ath_hal *ah = sc->sc_ah;
	struct ath_desc *ds;
	struct sk_buff *skb;

	ATH_RXBUF_RESET(bf);

	ds = bf->bf_desc;
	ds->ds_link = 0;    /* link to null */
	ds->ds_data = bf->bf_buf_addr;

	/* XXX For RADAR?
	 * virtual addr of the beginning of the buffer. */
	skb = bf->bf_mpdu;
	ASSERT(skb != NULL);
	ds->ds_vdata = skb->data;

	/* setup rx descriptors */
	ath9k_hw_setuprxdesc(ah,
			     ds,
			     skb_tailroom(skb),   /* buffer size */
			     0);

	if (sc->sc_rxlink == NULL)
		ath9k_hw_putrxbuf(ah, bf->bf_daddr);
	else
		*sc->sc_rxlink = bf->bf_daddr;

	sc->sc_rxlink = &ds->ds_link;
	ath9k_hw_rxena(ah);
}

static struct sk_buff *ath_rxbuf_alloc(struct ath_softc *sc,
	u32 len)
{
	struct sk_buff *skb;
	u32 off;

	/*
	 * Cache-line-align.  This is important (for the
	 * 5210 at least) as not doing so causes bogus data
	 * in rx'd frames.
	 */

	skb = dev_alloc_skb(len + sc->sc_cachelsz - 1);
	if (skb != NULL) {
		off = ((unsigned long) skb->data) % sc->sc_cachelsz;
		if (off != 0)
			skb_reserve(skb, sc->sc_cachelsz - off);
	} else {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: skbuff alloc of size %u failed\n",
			__func__, len);
		return NULL;
	}

	return skb;
}

static void ath_rx_requeue(struct ath_softc *sc, struct sk_buff *skb)
{
	struct ath_buf *bf = ATH_RX_CONTEXT(skb)->ctx_rxbuf;

	ASSERT(bf != NULL);

	spin_lock_bh(&sc->sc_rxbuflock);
	if (bf->bf_status & ATH_BUFSTATUS_STALE) {
		/*
		 * This buffer is still held for hw acess.
		 * Mark it as free to be re-queued it later.
		 */
		bf->bf_status |= ATH_BUFSTATUS_FREE;
	} else {
		/* XXX: we probably never enter here, remove after
		 * verification */
		list_add_tail(&bf->list, &sc->sc_rxbuf);
		ath_rx_buf_link(sc, bf);
	}
	spin_unlock_bh(&sc->sc_rxbuflock);
}

/*
 * The skb indicated to upper stack won't be returned to us.
 * So we have to allocate a new one and queue it by ourselves.
 */
static int ath_rx_indicate(struct ath_softc *sc,
			   struct sk_buff *skb,
			   struct ath_recv_status *status,
			   u16 keyix)
{
	struct ath_buf *bf = ATH_RX_CONTEXT(skb)->ctx_rxbuf;
	struct sk_buff *nskb;
	int type;

	/* indicate frame to the stack, which will free the old skb. */
	type = _ath_rx_indicate(sc, skb, status, keyix);

	/* allocate a new skb and queue it to for H/W processing */
	nskb = ath_rxbuf_alloc(sc, sc->sc_rxbufsize);
	if (nskb != NULL) {
		bf->bf_mpdu = nskb;
		bf->bf_buf_addr = pci_map_single(sc->pdev, nskb->data,
					 skb_end_pointer(nskb) - nskb->head,
					 PCI_DMA_FROMDEVICE);
		bf->bf_dmacontext = bf->bf_buf_addr;
		ATH_RX_CONTEXT(nskb)->ctx_rxbuf = bf;

		/* queue the new wbuf to H/W */
		ath_rx_requeue(sc, nskb);
	}

	return type;
}

static void ath_opmode_init(struct ath_softc *sc)
{
	struct ath_hal *ah = sc->sc_ah;
	u32 rfilt, mfilt[2];

	/* configure rx filter */
	rfilt = ath_calcrxfilter(sc);
	ath9k_hw_setrxfilter(ah, rfilt);

	/* configure bssid mask */
	if (ah->ah_caps.hw_caps & ATH9K_HW_CAP_BSSIDMASK)
		ath9k_hw_setbssidmask(ah, sc->sc_bssidmask);

	/* configure operational mode */
	ath9k_hw_setopmode(ah);

	/* Handle any link-level address change. */
	ath9k_hw_setmac(ah, sc->sc_myaddr);

	/* calculate and install multicast filter */
	mfilt[0] = mfilt[1] = ~0;

	ath9k_hw_setmcastfilter(ah, mfilt[0], mfilt[1]);
	DPRINTF(sc, ATH_DBG_CONFIG ,
		"%s: RX filter 0x%x, MC filter %08x:%08x\n",
		__func__, rfilt, mfilt[0], mfilt[1]);
}

int ath_rx_init(struct ath_softc *sc, int nbufs)
{
	struct sk_buff *skb;
	struct ath_buf *bf;
	int error = 0;

	do {
		spin_lock_init(&sc->sc_rxflushlock);
		sc->sc_flags &= ~SC_OP_RXFLUSH;
		spin_lock_init(&sc->sc_rxbuflock);

		/*
		 * Cisco's VPN software requires that drivers be able to
		 * receive encapsulated frames that are larger than the MTU.
		 * Since we can't be sure how large a frame we'll get, setup
		 * to handle the larges on possible.
		 */
		sc->sc_rxbufsize = roundup(IEEE80211_MAX_MPDU_LEN,
					   min(sc->sc_cachelsz,
					       (u16)64));

		DPRINTF(sc, ATH_DBG_CONFIG, "%s: cachelsz %u rxbufsize %u\n",
			__func__, sc->sc_cachelsz, sc->sc_rxbufsize);

		/* Initialize rx descriptors */

		error = ath_descdma_setup(sc, &sc->sc_rxdma, &sc->sc_rxbuf,
					  "rx", nbufs, 1);
		if (error != 0) {
			DPRINTF(sc, ATH_DBG_FATAL,
				"%s: failed to allocate rx descriptors: %d\n",
				__func__, error);
			break;
		}

		/* Pre-allocate a wbuf for each rx buffer */

		list_for_each_entry(bf, &sc->sc_rxbuf, list) {
			skb = ath_rxbuf_alloc(sc, sc->sc_rxbufsize);
			if (skb == NULL) {
				error = -ENOMEM;
				break;
			}

			bf->bf_mpdu = skb;
			bf->bf_buf_addr = pci_map_single(sc->pdev, skb->data,
					 skb_end_pointer(skb) - skb->head,
					 PCI_DMA_FROMDEVICE);
			bf->bf_dmacontext = bf->bf_buf_addr;
			ATH_RX_CONTEXT(skb)->ctx_rxbuf = bf;
		}
		sc->sc_rxlink = NULL;

	} while (0);

	if (error)
		ath_rx_cleanup(sc);

	return error;
}

/* Reclaim all rx queue resources */

void ath_rx_cleanup(struct ath_softc *sc)
{
	struct sk_buff *skb;
	struct ath_buf *bf;

	list_for_each_entry(bf, &sc->sc_rxbuf, list) {
		skb = bf->bf_mpdu;
		if (skb)
			dev_kfree_skb(skb);
	}

	/* cleanup rx descriptors */

	if (sc->sc_rxdma.dd_desc_len != 0)
		ath_descdma_cleanup(sc, &sc->sc_rxdma, &sc->sc_rxbuf);
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
	if (sc->sc_ah->ah_opmode != ATH9K_M_STA)
		rfilt |= ATH9K_RX_FILTER_PROBEREQ;

	/* Can't set HOSTAP into promiscous mode */
	if (((sc->sc_ah->ah_opmode != ATH9K_M_HOSTAP) &&
	     (sc->rx_filter & FIF_PROMISC_IN_BSS)) ||
	    (sc->sc_ah->ah_opmode == ATH9K_M_MONITOR)) {
		rfilt |= ATH9K_RX_FILTER_PROM;
		/* ??? To prevent from sending ACK */
		rfilt &= ~ATH9K_RX_FILTER_UCAST;
	}

	if (((sc->sc_ah->ah_opmode == ATH9K_M_STA) &&
	     (sc->rx_filter & FIF_BCN_PRBRESP_PROMISC)) ||
	    (sc->sc_ah->ah_opmode == ATH9K_M_IBSS))
		rfilt |= ATH9K_RX_FILTER_BEACON;

	/* If in HOSTAP mode, want to enable reception of PSPOLL frames
	   & beacon frames */
	if (sc->sc_ah->ah_opmode == ATH9K_M_HOSTAP)
		rfilt |= (ATH9K_RX_FILTER_BEACON | ATH9K_RX_FILTER_PSPOLL);
	return rfilt;

#undef RX_FILTER_PRESERVE
}

/* Enable the receive h/w following a reset. */

int ath_startrecv(struct ath_softc *sc)
{
	struct ath_hal *ah = sc->sc_ah;
	struct ath_buf *bf, *tbf;

	spin_lock_bh(&sc->sc_rxbuflock);
	if (list_empty(&sc->sc_rxbuf))
		goto start_recv;

	sc->sc_rxlink = NULL;
	list_for_each_entry_safe(bf, tbf, &sc->sc_rxbuf, list) {
		if (bf->bf_status & ATH_BUFSTATUS_STALE) {
			/* restarting h/w, no need for holding descriptors */
			bf->bf_status &= ~ATH_BUFSTATUS_STALE;
			/*
			 * Upper layer may not be done with the frame yet so
			 * we can't just re-queue it to hardware. Remove it
			 * from h/w queue. It'll be re-queued when upper layer
			 * returns the frame and ath_rx_requeue_mpdu is called.
			 */
			if (!(bf->bf_status & ATH_BUFSTATUS_FREE)) {
				list_del(&bf->list);
				continue;
			}
		}
		/* chain descriptors */
		ath_rx_buf_link(sc, bf);
	}

	/* We could have deleted elements so the list may be empty now */
	if (list_empty(&sc->sc_rxbuf))
		goto start_recv;

	bf = list_first_entry(&sc->sc_rxbuf, struct ath_buf, list);
	ath9k_hw_putrxbuf(ah, bf->bf_daddr);
	ath9k_hw_rxena(ah);      /* enable recv descriptors */

start_recv:
	spin_unlock_bh(&sc->sc_rxbuflock);
	ath_opmode_init(sc);        /* set filters, etc. */
	ath9k_hw_startpcureceive(ah);	/* re-enable PCU/DMA engine */
	return 0;
}

/* Disable the receive h/w in preparation for a reset. */

bool ath_stoprecv(struct ath_softc *sc)
{
	struct ath_hal *ah = sc->sc_ah;
	u64 tsf;
	bool stopped;

	ath9k_hw_stoppcurecv(ah);	/* disable PCU */
	ath9k_hw_setrxfilter(ah, 0);	/* clear recv filter */
	stopped = ath9k_hw_stopdmarecv(ah);	/* disable DMA engine */
	mdelay(3);			/* 3ms is long enough for 1 frame */
	tsf = ath9k_hw_gettsf64(ah);
	sc->sc_rxlink = NULL;		/* just in case */
	return stopped;
}

/* Flush receive queue */

void ath_flushrecv(struct ath_softc *sc)
{
	/*
	 * ath_rx_tasklet may be used to handle rx interrupt and flush receive
	 * queue at the same time. Use a lock to serialize the access of rx
	 * queue.
	 * ath_rx_tasklet cannot hold the spinlock while indicating packets.
	 * Instead, do not claim the spinlock but check for a flush in
	 * progress (see references to sc_rxflush)
	 */
	spin_lock_bh(&sc->sc_rxflushlock);
	sc->sc_flags |= SC_OP_RXFLUSH;

	ath_rx_tasklet(sc, 1);

	sc->sc_flags &= ~SC_OP_RXFLUSH;
	spin_unlock_bh(&sc->sc_rxflushlock);
}

/* Process receive queue, as well as LED, etc. */

int ath_rx_tasklet(struct ath_softc *sc, int flush)
{
#define PA2DESC(_sc, _pa)                                               \
	((struct ath_desc *)((caddr_t)(_sc)->sc_rxdma.dd_desc +		\
			     ((_pa) - (_sc)->sc_rxdma.dd_desc_paddr)))

	struct ath_buf *bf, *bf_held = NULL;
	struct ath_desc *ds;
	struct ieee80211_hdr *hdr;
	struct sk_buff *skb = NULL;
	struct ath_recv_status rx_status;
	struct ath_hal *ah = sc->sc_ah;
	int type, rx_processed = 0;
	u32 phyerr;
	u8 chainreset = 0;
	int retval;
	__le16 fc;

	do {
		/* If handling rx interrupt and flush is in progress => exit */
		if ((sc->sc_flags & SC_OP_RXFLUSH) && (flush == 0))
			break;

		spin_lock_bh(&sc->sc_rxbuflock);
		if (list_empty(&sc->sc_rxbuf)) {
			sc->sc_rxlink = NULL;
			spin_unlock_bh(&sc->sc_rxbuflock);
			break;
		}

		bf = list_first_entry(&sc->sc_rxbuf, struct ath_buf, list);

		/*
		 * There is a race condition that BH gets scheduled after sw
		 * writes RxE and before hw re-load the last descriptor to get
		 * the newly chained one. Software must keep the last DONE
		 * descriptor as a holding descriptor - software does so by
		 * marking it with the STALE flag.
		 */
		if (bf->bf_status & ATH_BUFSTATUS_STALE) {
			bf_held = bf;
			if (list_is_last(&bf_held->list, &sc->sc_rxbuf)) {
				/*
				 * The holding descriptor is the last
				 * descriptor in queue. It's safe to
				 * remove the last holding descriptor
				 * in BH context.
				 */
				list_del(&bf_held->list);
				bf_held->bf_status &= ~ATH_BUFSTATUS_STALE;
				sc->sc_rxlink = NULL;

				if (bf_held->bf_status & ATH_BUFSTATUS_FREE) {
					list_add_tail(&bf_held->list,
						&sc->sc_rxbuf);
					ath_rx_buf_link(sc, bf_held);
				}
				spin_unlock_bh(&sc->sc_rxbuflock);
				break;
			}
			bf = list_entry(bf->list.next, struct ath_buf, list);
		}

		ds = bf->bf_desc;
		++rx_processed;

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
		retval = ath9k_hw_rxprocdesc(ah,
					     ds,
					     bf->bf_daddr,
					     PA2DESC(sc, ds->ds_link),
					     0);
		if (retval == -EINPROGRESS) {
			struct ath_buf *tbf;
			struct ath_desc *tds;

			if (list_is_last(&bf->list, &sc->sc_rxbuf)) {
				spin_unlock_bh(&sc->sc_rxbuflock);
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
			retval = ath9k_hw_rxprocdesc(ah,
				tds, tbf->bf_daddr,
				PA2DESC(sc, tds->ds_link), 0);
			if (retval == -EINPROGRESS) {
				spin_unlock_bh(&sc->sc_rxbuflock);
				break;
			}
		}

		/* XXX: we do not support frames spanning
		 * multiple descriptors */
		bf->bf_status |= ATH_BUFSTATUS_DONE;

		skb = bf->bf_mpdu;
		if (skb == NULL) {		/* XXX ??? can this happen */
			spin_unlock_bh(&sc->sc_rxbuflock);
			continue;
		}
		/*
		 * Now we know it's a completed frame, we can indicate the
		 * frame. Remove the previous holding descriptor and leave
		 * this one in the queue as the new holding descriptor.
		 */
		if (bf_held) {
			list_del(&bf_held->list);
			bf_held->bf_status &= ~ATH_BUFSTATUS_STALE;
			if (bf_held->bf_status & ATH_BUFSTATUS_FREE) {
				list_add_tail(&bf_held->list, &sc->sc_rxbuf);
				/* try to requeue this descriptor */
				ath_rx_buf_link(sc, bf_held);
			}
		}

		bf->bf_status |= ATH_BUFSTATUS_STALE;
		bf_held = bf;
		/*
		 * Release the lock here in case ieee80211_input() return
		 * the frame immediately by calling ath_rx_mpdu_requeue().
		 */
		spin_unlock_bh(&sc->sc_rxbuflock);

		if (flush) {
			/*
			 * If we're asked to flush receive queue, directly
			 * chain it back at the queue without processing it.
			 */
			goto rx_next;
		}

		hdr = (struct ieee80211_hdr *)skb->data;
		fc = hdr->frame_control;
		memset(&rx_status, 0, sizeof(struct ath_recv_status));

		if (ds->ds_rxstat.rs_more) {
			/*
			 * Frame spans multiple descriptors; this
			 * cannot happen yet as we don't support
			 * jumbograms.  If not in monitor mode,
			 * discard the frame.
			 */
#ifndef ERROR_FRAMES
			/*
			 * Enable this if you want to see
			 * error frames in Monitor mode.
			 */
			if (sc->sc_ah->ah_opmode != ATH9K_M_MONITOR)
				goto rx_next;
#endif
			/* fall thru for monitor mode handling... */
		} else if (ds->ds_rxstat.rs_status != 0) {
			if (ds->ds_rxstat.rs_status & ATH9K_RXERR_CRC)
				rx_status.flags |= ATH_RX_FCS_ERROR;
			if (ds->ds_rxstat.rs_status & ATH9K_RXERR_PHY) {
				phyerr = ds->ds_rxstat.rs_phyerr & 0x1f;
				goto rx_next;
			}

			if (ds->ds_rxstat.rs_status & ATH9K_RXERR_DECRYPT) {
				/*
				 * Decrypt error. We only mark packet status
				 * here and always push up the frame up to let
				 * mac80211 handle the actual error case, be
				 * it no decryption key or real decryption
				 * error. This let us keep statistics there.
				 */
				rx_status.flags |= ATH_RX_DECRYPT_ERROR;
			} else if (ds->ds_rxstat.rs_status & ATH9K_RXERR_MIC) {
				/*
				 * Demic error. We only mark frame status here
				 * and always push up the frame up to let
				 * mac80211 handle the actual error case. This
				 * let us keep statistics there. Hardware may
				 * post a false-positive MIC error.
				 */
				if (ieee80211_is_ctl(fc))
					/*
					 * Sometimes, we get invalid
					 * MIC failures on valid control frames.
					 * Remove these mic errors.
					 */
					ds->ds_rxstat.rs_status &=
						~ATH9K_RXERR_MIC;
				else
					rx_status.flags |= ATH_RX_MIC_ERROR;
			}
			/*
			 * Reject error frames with the exception of
			 * decryption and MIC failures. For monitor mode,
			 * we also ignore the CRC error.
			 */
			if (sc->sc_ah->ah_opmode == ATH9K_M_MONITOR) {
				if (ds->ds_rxstat.rs_status &
				    ~(ATH9K_RXERR_DECRYPT | ATH9K_RXERR_MIC |
					ATH9K_RXERR_CRC))
					goto rx_next;
			} else {
				if (ds->ds_rxstat.rs_status &
				    ~(ATH9K_RXERR_DECRYPT | ATH9K_RXERR_MIC)) {
					goto rx_next;
				}
			}
		}
		/*
		 * The status portion of the descriptor could get corrupted.
		 */
		if (sc->sc_rxbufsize < ds->ds_rxstat.rs_datalen)
			goto rx_next;
		/*
		 * Sync and unmap the frame.  At this point we're
		 * committed to passing the sk_buff somewhere so
		 * clear buf_skb; this means a new sk_buff must be
		 * allocated when the rx descriptor is setup again
		 * to receive another frame.
		 */
		skb_put(skb, ds->ds_rxstat.rs_datalen);
		skb->protocol = cpu_to_be16(ETH_P_CONTROL);
		rx_status.tsf = ath_extend_tsf(sc, ds->ds_rxstat.rs_tstamp);
		rx_status.rateieee =
			sc->sc_hwmap[ds->ds_rxstat.rs_rate].ieeerate;
		rx_status.rateKbps =
			sc->sc_hwmap[ds->ds_rxstat.rs_rate].rateKbps;
		rx_status.ratecode = ds->ds_rxstat.rs_rate;

		/* HT rate */
		if (rx_status.ratecode & 0x80) {
			/* TODO - add table to avoid division */
			if (ds->ds_rxstat.rs_flags & ATH9K_RX_2040) {
				rx_status.flags |= ATH_RX_40MHZ;
				rx_status.rateKbps =
					(rx_status.rateKbps * 27) / 13;
			}
			if (ds->ds_rxstat.rs_flags & ATH9K_RX_GI)
				rx_status.rateKbps =
					(rx_status.rateKbps * 10) / 9;
			else
				rx_status.flags |= ATH_RX_SHORT_GI;
		}

		/* sc_noise_floor is only available when the station
		   attaches to an AP, so we use a default value
		   if we are not yet attached. */
		rx_status.abs_rssi =
			ds->ds_rxstat.rs_rssi + sc->sc_ani.sc_noise_floor;

		pci_dma_sync_single_for_cpu(sc->pdev,
					    bf->bf_buf_addr,
					    skb_tailroom(skb),
					    PCI_DMA_FROMDEVICE);
		pci_unmap_single(sc->pdev,
				 bf->bf_buf_addr,
				 sc->sc_rxbufsize,
				 PCI_DMA_FROMDEVICE);

		/* XXX: Ah! make me more readable, use a helper */
		if (ah->ah_caps.hw_caps & ATH9K_HW_CAP_HT) {
			if (ds->ds_rxstat.rs_moreaggr == 0) {
				rx_status.rssictl[0] =
					ds->ds_rxstat.rs_rssi_ctl0;
				rx_status.rssictl[1] =
					ds->ds_rxstat.rs_rssi_ctl1;
				rx_status.rssictl[2] =
					ds->ds_rxstat.rs_rssi_ctl2;
				rx_status.rssi = ds->ds_rxstat.rs_rssi;
				if (ds->ds_rxstat.rs_flags & ATH9K_RX_2040) {
					rx_status.rssiextn[0] =
						ds->ds_rxstat.rs_rssi_ext0;
					rx_status.rssiextn[1] =
						ds->ds_rxstat.rs_rssi_ext1;
					rx_status.rssiextn[2] =
						ds->ds_rxstat.rs_rssi_ext2;
					rx_status.flags |=
						ATH_RX_RSSI_EXTN_VALID;
				}
				rx_status.flags |= ATH_RX_RSSI_VALID |
					ATH_RX_CHAIN_RSSI_VALID;
			}
		} else {
			/*
			 * Need to insert the "combined" rssi into the
			 * status structure for upper layer processing
			 */
			rx_status.rssi = ds->ds_rxstat.rs_rssi;
			rx_status.flags |= ATH_RX_RSSI_VALID;
		}

		/* Pass frames up to the stack. */

		type = ath_rx_indicate(sc, skb,
			&rx_status, ds->ds_rxstat.rs_keyix);

		/*
		 * change the default rx antenna if rx diversity chooses the
		 * other antenna 3 times in a row.
		 */
		if (sc->sc_defant != ds->ds_rxstat.rs_antenna) {
			if (++sc->sc_rxotherant >= 3)
				ath_setdefantenna(sc,
						ds->ds_rxstat.rs_antenna);
		} else {
			sc->sc_rxotherant = 0;
		}

#ifdef CONFIG_SLOW_ANT_DIV
		if ((rx_status.flags & ATH_RX_RSSI_VALID) &&
		    ieee80211_is_beacon(fc)) {
			ath_slow_ant_div(&sc->sc_antdiv, hdr, &ds->ds_rxstat);
		}
#endif
		/*
		 * For frames successfully indicated, the buffer will be
		 * returned to us by upper layers by calling
		 * ath_rx_mpdu_requeue, either synchronusly or asynchronously.
		 * So we don't want to do it here in this loop.
		 */
		continue;

rx_next:
		bf->bf_status |= ATH_BUFSTATUS_FREE;
	} while (TRUE);

	if (chainreset) {
		DPRINTF(sc, ATH_DBG_CONFIG,
			"%s: Reset rx chain mask. "
			"Do internal reset\n", __func__);
		ASSERT(flush == 0);
		ath_reset(sc, false);
	}

	return 0;
#undef PA2DESC
}
