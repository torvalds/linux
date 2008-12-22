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
 * Setup and link descriptors.
 *
 * 11N: we can no longer afford to self link the last descriptor.
 * MAC acknowledges BA status as long as it copies frames to host
 * buffer (or rx fifo). This can incorrectly acknowledge packets
 * to a sender if last desc is self-linked.
 */
static void ath_rx_buf_link(struct ath_softc *sc, struct ath_buf *bf)
{
	struct ath_hal *ah = sc->sc_ah;
	struct ath_desc *ds;
	struct sk_buff *skb;

	ATH_RXBUF_RESET(bf);

	ds = bf->bf_desc;
	ds->ds_link = 0; /* link to null */
	ds->ds_data = bf->bf_buf_addr;

	/* virtual addr of the beginning of the buffer. */
	skb = bf->bf_mpdu;
	ASSERT(skb != NULL);
	ds->ds_vdata = skb->data;

	/* setup rx descriptors. The rx.bufsize here tells the harware
	 * how much data it can DMA to us and that we are prepared
	 * to process */
	ath9k_hw_setuprxdesc(ah, ds,
			     sc->rx.bufsize,
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

/*
 *  Extend 15-bit time stamp from rx descriptor to
 *  a full 64-bit TSF using the current h/w TSF.
*/
static u64 ath_extend_tsf(struct ath_softc *sc, u32 rstamp)
{
	u64 tsf;

	tsf = ath9k_hw_gettsf64(sc->sc_ah);
	if ((tsf & 0x7fff) < rstamp)
		tsf -= 0x8000;
	return (tsf & ~0x7fff) | rstamp;
}

static struct sk_buff *ath_rxbuf_alloc(struct ath_softc *sc, u32 len)
{
	struct sk_buff *skb;
	u32 off;

	/*
	 * Cache-line-align.  This is important (for the
	 * 5210 at least) as not doing so causes bogus data
	 * in rx'd frames.
	 */

	/* Note: the kernel can allocate a value greater than
	 * what we ask it to give us. We really only need 4 KB as that
	 * is this hardware supports and in fact we need at least 3849
	 * as that is the MAX AMSDU size this hardware supports.
	 * Unfortunately this means we may get 8 KB here from the
	 * kernel... and that is actually what is observed on some
	 * systems :( */
	skb = dev_alloc_skb(len + sc->sc_cachelsz - 1);
	if (skb != NULL) {
		off = ((unsigned long) skb->data) % sc->sc_cachelsz;
		if (off != 0)
			skb_reserve(skb, sc->sc_cachelsz - off);
	} else {
		DPRINTF(sc, ATH_DBG_FATAL,
			"skbuff alloc of size %u failed\n", len);
		return NULL;
	}

	return skb;
}

/*
 * For Decrypt or Demic errors, we only mark packet status here and always push
 * up the frame up to let mac80211 handle the actual error case, be it no
 * decryption key or real decryption error. This let us keep statistics there.
 */
static int ath_rx_prepare(struct sk_buff *skb, struct ath_desc *ds,
			  struct ieee80211_rx_status *rx_status, bool *decrypt_error,
			  struct ath_softc *sc)
{
	struct ieee80211_hdr *hdr;
	u8 ratecode;
	__le16 fc;

	hdr = (struct ieee80211_hdr *)skb->data;
	fc = hdr->frame_control;
	memset(rx_status, 0, sizeof(struct ieee80211_rx_status));

	if (ds->ds_rxstat.rs_more) {
		/*
		 * Frame spans multiple descriptors; this cannot happen yet
		 * as we don't support jumbograms. If not in monitor mode,
		 * discard the frame. Enable this if you want to see
		 * error frames in Monitor mode.
		 */
		if (sc->sc_ah->ah_opmode != NL80211_IFTYPE_MONITOR)
			goto rx_next;
	} else if (ds->ds_rxstat.rs_status != 0) {
		if (ds->ds_rxstat.rs_status & ATH9K_RXERR_CRC)
			rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
		if (ds->ds_rxstat.rs_status & ATH9K_RXERR_PHY)
			goto rx_next;

		if (ds->ds_rxstat.rs_status & ATH9K_RXERR_DECRYPT) {
			*decrypt_error = true;
		} else if (ds->ds_rxstat.rs_status & ATH9K_RXERR_MIC) {
			if (ieee80211_is_ctl(fc))
				/*
				 * Sometimes, we get invalid
				 * MIC failures on valid control frames.
				 * Remove these mic errors.
				 */
				ds->ds_rxstat.rs_status &= ~ATH9K_RXERR_MIC;
			else
				rx_status->flag |= RX_FLAG_MMIC_ERROR;
		}
		/*
		 * Reject error frames with the exception of
		 * decryption and MIC failures. For monitor mode,
		 * we also ignore the CRC error.
		 */
		if (sc->sc_ah->ah_opmode == NL80211_IFTYPE_MONITOR) {
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

	ratecode = ds->ds_rxstat.rs_rate;

	if (ratecode & 0x80) {
		/* HT rate */
		rx_status->flag |= RX_FLAG_HT;
		if (ds->ds_rxstat.rs_flags & ATH9K_RX_2040)
			rx_status->flag |= RX_FLAG_40MHZ;
		if (ds->ds_rxstat.rs_flags & ATH9K_RX_GI)
			rx_status->flag |= RX_FLAG_SHORT_GI;
		rx_status->rate_idx = ratecode & 0x7f;
	} else {
		int i = 0, cur_band, n_rates;
		struct ieee80211_hw *hw = sc->hw;

		cur_band = hw->conf.channel->band;
		n_rates = sc->sbands[cur_band].n_bitrates;

		for (i = 0; i < n_rates; i++) {
			if (sc->sbands[cur_band].bitrates[i].hw_value ==
			    ratecode) {
				rx_status->rate_idx = i;
				break;
			}

			if (sc->sbands[cur_band].bitrates[i].hw_value_short ==
			    ratecode) {
				rx_status->rate_idx = i;
				rx_status->flag |= RX_FLAG_SHORTPRE;
				break;
			}
		}
	}

	rx_status->mactime = ath_extend_tsf(sc, ds->ds_rxstat.rs_tstamp);
	rx_status->band = sc->hw->conf.channel->band;
	rx_status->freq =  sc->hw->conf.channel->center_freq;
	rx_status->noise = sc->sc_ani.sc_noise_floor;
	rx_status->signal = rx_status->noise + ds->ds_rxstat.rs_rssi;
	rx_status->antenna = ds->ds_rxstat.rs_antenna;

	/* at 45 you will be able to use MCS 15 reliably. A more elaborate
	 * scheme can be used here but it requires tables of SNR/throughput for
	 * each possible mode used. */
	rx_status->qual =  ds->ds_rxstat.rs_rssi * 100 / 45;

	/* rssi can be more than 45 though, anything above that
	 * should be considered at 100% */
	if (rx_status->qual > 100)
		rx_status->qual = 100;

	rx_status->flag |= RX_FLAG_TSFT;

	return 1;
rx_next:
	return 0;
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
}

int ath_rx_init(struct ath_softc *sc, int nbufs)
{
	struct sk_buff *skb;
	struct ath_buf *bf;
	int error = 0;

	do {
		spin_lock_init(&sc->rx.rxflushlock);
		sc->sc_flags &= ~SC_OP_RXFLUSH;
		spin_lock_init(&sc->rx.rxbuflock);

		sc->rx.bufsize = roundup(IEEE80211_MAX_MPDU_LEN,
					   min(sc->sc_cachelsz,
					       (u16)64));

		DPRINTF(sc, ATH_DBG_CONFIG, "cachelsz %u rxbufsize %u\n",
			sc->sc_cachelsz, sc->rx.bufsize);

		/* Initialize rx descriptors */

		error = ath_descdma_setup(sc, &sc->rx.rxdma, &sc->rx.rxbuf,
					  "rx", nbufs, 1);
		if (error != 0) {
			DPRINTF(sc, ATH_DBG_FATAL,
				"failed to allocate rx descriptors: %d\n", error);
			break;
		}

		list_for_each_entry(bf, &sc->rx.rxbuf, list) {
			skb = ath_rxbuf_alloc(sc, sc->rx.bufsize);
			if (skb == NULL) {
				error = -ENOMEM;
				break;
			}

			bf->bf_mpdu = skb;
			bf->bf_buf_addr = pci_map_single(sc->pdev, skb->data,
							 sc->rx.bufsize,
							 PCI_DMA_FROMDEVICE);
			if (unlikely(pci_dma_mapping_error(sc->pdev,
				  bf->bf_buf_addr))) {
				dev_kfree_skb_any(skb);
				bf->bf_mpdu = NULL;
				DPRINTF(sc, ATH_DBG_CONFIG,
					"pci_dma_mapping_error() on RX init\n");
				error = -ENOMEM;
				break;
			}
			bf->bf_dmacontext = bf->bf_buf_addr;
		}
		sc->rx.rxlink = NULL;

	} while (0);

	if (error)
		ath_rx_cleanup(sc);

	return error;
}

void ath_rx_cleanup(struct ath_softc *sc)
{
	struct sk_buff *skb;
	struct ath_buf *bf;

	list_for_each_entry(bf, &sc->rx.rxbuf, list) {
		skb = bf->bf_mpdu;
		if (skb)
			dev_kfree_skb(skb);
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
	if (sc->sc_ah->ah_opmode != NL80211_IFTYPE_STATION)
		rfilt |= ATH9K_RX_FILTER_PROBEREQ;

	/* Can't set HOSTAP into promiscous mode */
	if (((sc->sc_ah->ah_opmode != NL80211_IFTYPE_AP) &&
	     (sc->rx.rxfilter & FIF_PROMISC_IN_BSS)) ||
	    (sc->sc_ah->ah_opmode == NL80211_IFTYPE_MONITOR)) {
		rfilt |= ATH9K_RX_FILTER_PROM;
		/* ??? To prevent from sending ACK */
		rfilt &= ~ATH9K_RX_FILTER_UCAST;
	}

	if (sc->sc_ah->ah_opmode == NL80211_IFTYPE_STATION ||
	    sc->sc_ah->ah_opmode == NL80211_IFTYPE_ADHOC)
		rfilt |= ATH9K_RX_FILTER_BEACON;

	/* If in HOSTAP mode, want to enable reception of PSPOLL frames
	   & beacon frames */
	if (sc->sc_ah->ah_opmode == NL80211_IFTYPE_AP)
		rfilt |= (ATH9K_RX_FILTER_BEACON | ATH9K_RX_FILTER_PSPOLL);

	return rfilt;

#undef RX_FILTER_PRESERVE
}

int ath_startrecv(struct ath_softc *sc)
{
	struct ath_hal *ah = sc->sc_ah;
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
	struct ath_hal *ah = sc->sc_ah;
	bool stopped;

	ath9k_hw_stoppcurecv(ah);
	ath9k_hw_setrxfilter(ah, 0);
	stopped = ath9k_hw_stopdmarecv(ah);
	mdelay(3); /* 3ms is long enough for 1 frame */
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

int ath_rx_tasklet(struct ath_softc *sc, int flush)
{
#define PA2DESC(_sc, _pa)                                               \
	((struct ath_desc *)((caddr_t)(_sc)->rx.rxdma.dd_desc +		\
			     ((_pa) - (_sc)->rx.rxdma.dd_desc_paddr)))

	struct ath_buf *bf;
	struct ath_desc *ds;
	struct sk_buff *skb = NULL, *requeue_skb;
	struct ieee80211_rx_status rx_status;
	struct ath_hal *ah = sc->sc_ah;
	struct ieee80211_hdr *hdr;
	int hdrlen, padsize, retval;
	bool decrypt_error = false;
	u8 keyix;

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
		pci_dma_sync_single_for_cpu(sc->pdev, bf->bf_buf_addr,
				sc->rx.bufsize,
				PCI_DMA_FROMDEVICE);

		/*
		 * If we're asked to flush receive queue, directly
		 * chain it back at the queue without processing it.
		 */
		if (flush)
			goto requeue;

		if (!ds->ds_rxstat.rs_datalen)
			goto requeue;

		/* The status portion of the descriptor could get corrupted. */
		if (sc->rx.bufsize < ds->ds_rxstat.rs_datalen)
			goto requeue;

		if (!ath_rx_prepare(skb, ds, &rx_status, &decrypt_error, sc))
			goto requeue;

		/* Ensure we always have an skb to requeue once we are done
		 * processing the current buffer's skb */
		requeue_skb = ath_rxbuf_alloc(sc, sc->rx.bufsize);

		/* If there is no memory we ignore the current RX'd frame,
		 * tell hardware it can give us a new frame using the old
		 * skb and put it at the tail of the sc->rx.rxbuf list for
		 * processing. */
		if (!requeue_skb)
			goto requeue;

		/* Unmap the frame */
		pci_unmap_single(sc->pdev, bf->bf_buf_addr,
				 sc->rx.bufsize,
				 PCI_DMA_FROMDEVICE);

		skb_put(skb, ds->ds_rxstat.rs_datalen);
		skb->protocol = cpu_to_be16(ETH_P_CONTROL);

		/* see if any padding is done by the hw and remove it */
		hdr = (struct ieee80211_hdr *)skb->data;
		hdrlen = ieee80211_get_hdrlen_from_skb(skb);

		/* The MAC header is padded to have 32-bit boundary if the
		 * packet payload is non-zero. The general calculation for
		 * padsize would take into account odd header lengths:
		 * padsize = (4 - hdrlen % 4) % 4; However, since only
		 * even-length headers are used, padding can only be 0 or 2
		 * bytes and we can optimize this a bit. In addition, we must
		 * not try to remove padding from short control frames that do
		 * not have payload. */
		padsize = hdrlen & 3;
		if (padsize && hdrlen >= 24) {
			memmove(skb->data + padsize, skb->data, hdrlen);
			skb_pull(skb, padsize);
		}

		keyix = ds->ds_rxstat.rs_keyix;

		if (!(keyix == ATH9K_RXKEYIX_INVALID) && !decrypt_error) {
			rx_status.flag |= RX_FLAG_DECRYPTED;
		} else if ((le16_to_cpu(hdr->frame_control) & IEEE80211_FCTL_PROTECTED)
			   && !decrypt_error && skb->len >= hdrlen + 4) {
			keyix = skb->data[hdrlen + 3] >> 6;

			if (test_bit(keyix, sc->sc_keymap))
				rx_status.flag |= RX_FLAG_DECRYPTED;
		}

		/* Send the frame to mac80211 */
		__ieee80211_rx(sc->hw, skb, &rx_status);

		/* We will now give hardware our shiny new allocated skb */
		bf->bf_mpdu = requeue_skb;
		bf->bf_buf_addr = pci_map_single(sc->pdev, requeue_skb->data,
					 sc->rx.bufsize,
					 PCI_DMA_FROMDEVICE);
		if (unlikely(pci_dma_mapping_error(sc->pdev,
			  bf->bf_buf_addr))) {
			dev_kfree_skb_any(requeue_skb);
			bf->bf_mpdu = NULL;
			DPRINTF(sc, ATH_DBG_CONFIG,
				"pci_dma_mapping_error() on RX\n");
			break;
		}
		bf->bf_dmacontext = bf->bf_buf_addr;

		/*
		 * change the default rx antenna if rx diversity chooses the
		 * other antenna 3 times in a row.
		 */
		if (sc->rx.defant != ds->ds_rxstat.rs_antenna) {
			if (++sc->rx.rxotherant >= 3)
				ath_setdefantenna(sc, ds->ds_rxstat.rs_antenna);
		} else {
			sc->rx.rxotherant = 0;
		}
requeue:
		list_move_tail(&bf->list, &sc->rx.rxbuf);
		ath_rx_buf_link(sc, bf);
	} while (1);

	spin_unlock_bh(&sc->rx.rxbuflock);

	return 0;
#undef PA2DESC
}
