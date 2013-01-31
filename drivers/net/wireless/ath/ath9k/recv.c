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

#include <linux/dma-mapping.h>
#include <linux/relay.h>
#include "ath9k.h"
#include "ar9003_mac.h"

#define SKB_CB_ATHBUF(__skb)	(*((struct ath_buf **)__skb->cb))

static inline bool ath9k_check_auto_sleep(struct ath_softc *sc)
{
	return sc->ps_enabled &&
	       (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_AUTOSLEEP);
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
	ath_hw_setbssidmask(common);

	/* configure operational mode */
	ath9k_hw_setopmode(ah);

	/* calculate and install multicast filter */
	mfilt[0] = mfilt[1] = ~0;
	ath9k_hw_setmcastfilter(ah, mfilt[0], mfilt[1]);
}

static bool ath_rx_edma_buf_link(struct ath_softc *sc,
				 enum ath9k_rx_qtype qtype)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_rx_edma *rx_edma;
	struct sk_buff *skb;
	struct ath_buf *bf;

	rx_edma = &sc->rx.rx_edma[qtype];
	if (skb_queue_len(&rx_edma->rx_fifo) >= rx_edma->rx_fifo_hwsize)
		return false;

	bf = list_first_entry(&sc->rx.rxbuf, struct ath_buf, list);
	list_del_init(&bf->list);

	skb = bf->bf_mpdu;

	ATH_RXBUF_RESET(bf);
	memset(skb->data, 0, ah->caps.rx_status_len);
	dma_sync_single_for_device(sc->dev, bf->bf_buf_addr,
				ah->caps.rx_status_len, DMA_TO_DEVICE);

	SKB_CB_ATHBUF(skb) = bf;
	ath9k_hw_addrxbuf_edma(ah, bf->bf_buf_addr, qtype);
	skb_queue_tail(&rx_edma->rx_fifo, skb);

	return true;
}

static void ath_rx_addbuffer_edma(struct ath_softc *sc,
				  enum ath9k_rx_qtype qtype, int size)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_buf *bf, *tbf;

	if (list_empty(&sc->rx.rxbuf)) {
		ath_dbg(common, QUEUE, "No free rx buf available\n");
		return;
	}

	list_for_each_entry_safe(bf, tbf, &sc->rx.rxbuf, list)
		if (!ath_rx_edma_buf_link(sc, qtype))
			break;

}

static void ath_rx_remove_buffer(struct ath_softc *sc,
				 enum ath9k_rx_qtype qtype)
{
	struct ath_buf *bf;
	struct ath_rx_edma *rx_edma;
	struct sk_buff *skb;

	rx_edma = &sc->rx.rx_edma[qtype];

	while ((skb = skb_dequeue(&rx_edma->rx_fifo)) != NULL) {
		bf = SKB_CB_ATHBUF(skb);
		BUG_ON(!bf);
		list_add_tail(&bf->list, &sc->rx.rxbuf);
	}
}

static void ath_rx_edma_cleanup(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_buf *bf;

	ath_rx_remove_buffer(sc, ATH9K_RX_QUEUE_LP);
	ath_rx_remove_buffer(sc, ATH9K_RX_QUEUE_HP);

	list_for_each_entry(bf, &sc->rx.rxbuf, list) {
		if (bf->bf_mpdu) {
			dma_unmap_single(sc->dev, bf->bf_buf_addr,
					common->rx_bufsize,
					DMA_BIDIRECTIONAL);
			dev_kfree_skb_any(bf->bf_mpdu);
			bf->bf_buf_addr = 0;
			bf->bf_mpdu = NULL;
		}
	}
}

static void ath_rx_edma_init_queue(struct ath_rx_edma *rx_edma, int size)
{
	skb_queue_head_init(&rx_edma->rx_fifo);
	rx_edma->rx_fifo_hwsize = size;
}

static int ath_rx_edma_init(struct ath_softc *sc, int nbufs)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_hw *ah = sc->sc_ah;
	struct sk_buff *skb;
	struct ath_buf *bf;
	int error = 0, i;
	u32 size;

	ath9k_hw_set_rx_bufsize(ah, common->rx_bufsize -
				    ah->caps.rx_status_len);

	ath_rx_edma_init_queue(&sc->rx.rx_edma[ATH9K_RX_QUEUE_LP],
			       ah->caps.rx_lp_qdepth);
	ath_rx_edma_init_queue(&sc->rx.rx_edma[ATH9K_RX_QUEUE_HP],
			       ah->caps.rx_hp_qdepth);

	size = sizeof(struct ath_buf) * nbufs;
	bf = devm_kzalloc(sc->dev, size, GFP_KERNEL);
	if (!bf)
		return -ENOMEM;

	INIT_LIST_HEAD(&sc->rx.rxbuf);

	for (i = 0; i < nbufs; i++, bf++) {
		skb = ath_rxbuf_alloc(common, common->rx_bufsize, GFP_KERNEL);
		if (!skb) {
			error = -ENOMEM;
			goto rx_init_fail;
		}

		memset(skb->data, 0, common->rx_bufsize);
		bf->bf_mpdu = skb;

		bf->bf_buf_addr = dma_map_single(sc->dev, skb->data,
						 common->rx_bufsize,
						 DMA_BIDIRECTIONAL);
		if (unlikely(dma_mapping_error(sc->dev,
						bf->bf_buf_addr))) {
				dev_kfree_skb_any(skb);
				bf->bf_mpdu = NULL;
				bf->bf_buf_addr = 0;
				ath_err(common,
					"dma_mapping_error() on RX init\n");
				error = -ENOMEM;
				goto rx_init_fail;
		}

		list_add_tail(&bf->list, &sc->rx.rxbuf);
	}

	return 0;

rx_init_fail:
	ath_rx_edma_cleanup(sc);
	return error;
}

static void ath_edma_start_recv(struct ath_softc *sc)
{
	ath9k_hw_rxena(sc->sc_ah);

	ath_rx_addbuffer_edma(sc, ATH9K_RX_QUEUE_HP,
			      sc->rx.rx_edma[ATH9K_RX_QUEUE_HP].rx_fifo_hwsize);

	ath_rx_addbuffer_edma(sc, ATH9K_RX_QUEUE_LP,
			      sc->rx.rx_edma[ATH9K_RX_QUEUE_LP].rx_fifo_hwsize);

	ath_opmode_init(sc);

	ath9k_hw_startpcureceive(sc->sc_ah, !!(sc->hw->conf.flags & IEEE80211_CONF_OFFCHANNEL));
}

static void ath_edma_stop_recv(struct ath_softc *sc)
{
	ath_rx_remove_buffer(sc, ATH9K_RX_QUEUE_HP);
	ath_rx_remove_buffer(sc, ATH9K_RX_QUEUE_LP);
}

int ath_rx_init(struct ath_softc *sc, int nbufs)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct sk_buff *skb;
	struct ath_buf *bf;
	int error = 0;

	spin_lock_init(&sc->sc_pcu_lock);

	common->rx_bufsize = IEEE80211_MAX_MPDU_LEN / 2 +
			     sc->sc_ah->caps.rx_status_len;

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) {
		return ath_rx_edma_init(sc, nbufs);
	} else {
		ath_dbg(common, CONFIG, "cachelsz %u rxbufsize %u\n",
			common->cachelsz, common->rx_bufsize);

		/* Initialize rx descriptors */

		error = ath_descdma_setup(sc, &sc->rx.rxdma, &sc->rx.rxbuf,
				"rx", nbufs, 1, 0);
		if (error != 0) {
			ath_err(common,
				"failed to allocate rx descriptors: %d\n",
				error);
			goto err;
		}

		list_for_each_entry(bf, &sc->rx.rxbuf, list) {
			skb = ath_rxbuf_alloc(common, common->rx_bufsize,
					      GFP_KERNEL);
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
				bf->bf_buf_addr = 0;
				ath_err(common,
					"dma_mapping_error() on RX init\n");
				error = -ENOMEM;
				goto err;
			}
		}
		sc->rx.rxlink = NULL;
	}

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

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) {
		ath_rx_edma_cleanup(sc);
		return;
	} else {
		list_for_each_entry(bf, &sc->rx.rxbuf, list) {
			skb = bf->bf_mpdu;
			if (skb) {
				dma_unmap_single(sc->dev, bf->bf_buf_addr,
						common->rx_bufsize,
						DMA_FROM_DEVICE);
				dev_kfree_skb(skb);
				bf->bf_buf_addr = 0;
				bf->bf_mpdu = NULL;
			}
		}
	}
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
	u32 rfilt;

	rfilt = ATH9K_RX_FILTER_UCAST | ATH9K_RX_FILTER_BCAST
		| ATH9K_RX_FILTER_MCAST;

	if (sc->rx.rxfilter & FIF_PROBE_REQ)
		rfilt |= ATH9K_RX_FILTER_PROBEREQ;

	/*
	 * Set promiscuous mode when FIF_PROMISC_IN_BSS is enabled for station
	 * mode interface or when in monitor mode. AP mode does not need this
	 * since it receives all in-BSS frames anyway.
	 */
	if (sc->sc_ah->is_monitoring)
		rfilt |= ATH9K_RX_FILTER_PROM;

	if (sc->rx.rxfilter & FIF_CONTROL)
		rfilt |= ATH9K_RX_FILTER_CONTROL;

	if ((sc->sc_ah->opmode == NL80211_IFTYPE_STATION) &&
	    (sc->nvifs <= 1) &&
	    !(sc->rx.rxfilter & FIF_BCN_PRBRESP_PROMISC))
		rfilt |= ATH9K_RX_FILTER_MYBEACON;
	else
		rfilt |= ATH9K_RX_FILTER_BEACON;

	if ((sc->sc_ah->opmode == NL80211_IFTYPE_AP) ||
	    (sc->rx.rxfilter & FIF_PSPOLL))
		rfilt |= ATH9K_RX_FILTER_PSPOLL;

	if (conf_is_ht(&sc->hw->conf))
		rfilt |= ATH9K_RX_FILTER_COMP_BAR;

	if (sc->nvifs > 1 || (sc->rx.rxfilter & FIF_OTHER_BSS)) {
		/* This is needed for older chips */
		if (sc->sc_ah->hw_version.macVersion <= AR_SREV_VERSION_9160)
			rfilt |= ATH9K_RX_FILTER_PROM;
		rfilt |= ATH9K_RX_FILTER_MCAST_BCAST_ALL;
	}

	if (AR_SREV_9550(sc->sc_ah))
		rfilt |= ATH9K_RX_FILTER_4ADDRESS;

	return rfilt;

}

int ath_startrecv(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_buf *bf, *tbf;

	if (ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) {
		ath_edma_start_recv(sc);
		return 0;
	}

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
	ath_opmode_init(sc);
	ath9k_hw_startpcureceive(ah, !!(sc->hw->conf.flags & IEEE80211_CONF_OFFCHANNEL));

	return 0;
}

static void ath_flushrecv(struct ath_softc *sc)
{
	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA)
		ath_rx_tasklet(sc, 1, true);
	ath_rx_tasklet(sc, 1, false);
}

bool ath_stoprecv(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	bool stopped, reset = false;

	ath9k_hw_abortpcurecv(ah);
	ath9k_hw_setrxfilter(ah, 0);
	stopped = ath9k_hw_stopdmarecv(ah, &reset);

	ath_flushrecv(sc);

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA)
		ath_edma_stop_recv(sc);
	else
		sc->rx.rxlink = NULL;

	if (!(ah->ah_flags & AH_UNPLUGGED) &&
	    unlikely(!stopped)) {
		ath_err(ath9k_hw_common(sc->sc_ah),
			"Could not stop RX, we could be "
			"confusing the DMA engine when we start RX up\n");
		ATH_DBG_WARN_ON_ONCE(!stopped);
	}
	return stopped && !reset;
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
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	if (skb->len < 24 + 8 + 2 + 2)
		return;

	sc->ps_flags &= ~PS_WAIT_FOR_BEACON;

	if (sc->ps_flags & PS_BEACON_SYNC) {
		sc->ps_flags &= ~PS_BEACON_SYNC;
		ath_dbg(common, PS,
			"Reconfigure Beacon timers based on timestamp from the AP\n");
		ath9k_set_beacon(sc);
	}

	if (ath_beacon_dtim_pending_cab(skb)) {
		/*
		 * Remain awake waiting for buffered broadcast/multicast
		 * frames. If the last broadcast/multicast frame is not
		 * received properly, the next beacon frame will work as
		 * a backup trigger for returning into NETWORK SLEEP state,
		 * so we are waiting for it as well.
		 */
		ath_dbg(common, PS,
			"Received DTIM beacon indicating buffered broadcast/multicast frame(s)\n");
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
		ath_dbg(common, PS, "PS wait for CAB frames timed out\n");
	}
}

static void ath_rx_ps(struct ath_softc *sc, struct sk_buff *skb, bool mybeacon)
{
	struct ieee80211_hdr *hdr;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	hdr = (struct ieee80211_hdr *)skb->data;

	/* Process Beacon and CAB receive in PS state */
	if (((sc->ps_flags & PS_WAIT_FOR_BEACON) || ath9k_check_auto_sleep(sc))
	    && mybeacon) {
		ath_rx_ps_beacon(sc, skb);
	} else if ((sc->ps_flags & PS_WAIT_FOR_CAB) &&
		   (ieee80211_is_data(hdr->frame_control) ||
		    ieee80211_is_action(hdr->frame_control)) &&
		   is_multicast_ether_addr(hdr->addr1) &&
		   !ieee80211_has_moredata(hdr->frame_control)) {
		/*
		 * No more broadcast/multicast frames to be received at this
		 * point.
		 */
		sc->ps_flags &= ~(PS_WAIT_FOR_CAB | PS_WAIT_FOR_BEACON);
		ath_dbg(common, PS,
			"All PS CAB frames received, back to sleep\n");
	} else if ((sc->ps_flags & PS_WAIT_FOR_PSPOLL_DATA) &&
		   !is_multicast_ether_addr(hdr->addr1) &&
		   !ieee80211_has_morefrags(hdr->frame_control)) {
		sc->ps_flags &= ~PS_WAIT_FOR_PSPOLL_DATA;
		ath_dbg(common, PS,
			"Going back to sleep after having received PS-Poll data (0x%lx)\n",
			sc->ps_flags & (PS_WAIT_FOR_BEACON |
					PS_WAIT_FOR_CAB |
					PS_WAIT_FOR_PSPOLL_DATA |
					PS_WAIT_FOR_TX_ACK));
	}
}

static bool ath_edma_get_buffers(struct ath_softc *sc,
				 enum ath9k_rx_qtype qtype,
				 struct ath_rx_status *rs,
				 struct ath_buf **dest)
{
	struct ath_rx_edma *rx_edma = &sc->rx.rx_edma[qtype];
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct sk_buff *skb;
	struct ath_buf *bf;
	int ret;

	skb = skb_peek(&rx_edma->rx_fifo);
	if (!skb)
		return false;

	bf = SKB_CB_ATHBUF(skb);
	BUG_ON(!bf);

	dma_sync_single_for_cpu(sc->dev, bf->bf_buf_addr,
				common->rx_bufsize, DMA_FROM_DEVICE);

	ret = ath9k_hw_process_rxdesc_edma(ah, rs, skb->data);
	if (ret == -EINPROGRESS) {
		/*let device gain the buffer again*/
		dma_sync_single_for_device(sc->dev, bf->bf_buf_addr,
				common->rx_bufsize, DMA_FROM_DEVICE);
		return false;
	}

	__skb_unlink(skb, &rx_edma->rx_fifo);
	if (ret == -EINVAL) {
		/* corrupt descriptor, skip this one and the following one */
		list_add_tail(&bf->list, &sc->rx.rxbuf);
		ath_rx_edma_buf_link(sc, qtype);

		skb = skb_peek(&rx_edma->rx_fifo);
		if (skb) {
			bf = SKB_CB_ATHBUF(skb);
			BUG_ON(!bf);

			__skb_unlink(skb, &rx_edma->rx_fifo);
			list_add_tail(&bf->list, &sc->rx.rxbuf);
			ath_rx_edma_buf_link(sc, qtype);
		}

		bf = NULL;
	}

	*dest = bf;
	return true;
}

static struct ath_buf *ath_edma_get_next_rx_buf(struct ath_softc *sc,
						struct ath_rx_status *rs,
						enum ath9k_rx_qtype qtype)
{
	struct ath_buf *bf = NULL;

	while (ath_edma_get_buffers(sc, qtype, rs, &bf)) {
		if (!bf)
			continue;

		return bf;
	}
	return NULL;
}

static struct ath_buf *ath_get_next_rx_buf(struct ath_softc *sc,
					   struct ath_rx_status *rs)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_desc *ds;
	struct ath_buf *bf;
	int ret;

	if (list_empty(&sc->rx.rxbuf)) {
		sc->rx.rxlink = NULL;
		return NULL;
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
	ret = ath9k_hw_rxprocdesc(ah, ds, rs);
	if (ret == -EINPROGRESS) {
		struct ath_rx_status trs;
		struct ath_buf *tbf;
		struct ath_desc *tds;

		memset(&trs, 0, sizeof(trs));
		if (list_is_last(&bf->list, &sc->rx.rxbuf)) {
			sc->rx.rxlink = NULL;
			return NULL;
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
		ret = ath9k_hw_rxprocdesc(ah, tds, &trs);
		if (ret == -EINPROGRESS)
			return NULL;
	}

	list_del(&bf->list);
	if (!bf->bf_mpdu)
		return bf;

	/*
	 * Synchronize the DMA transfer with CPU before
	 * 1. accessing the frame
	 * 2. requeueing the same buffer to h/w
	 */
	dma_sync_single_for_cpu(sc->dev, bf->bf_buf_addr,
			common->rx_bufsize,
			DMA_FROM_DEVICE);

	return bf;
}

/* Assumes you've already done the endian to CPU conversion */
static bool ath9k_rx_accept(struct ath_common *common,
			    struct ieee80211_hdr *hdr,
			    struct ieee80211_rx_status *rxs,
			    struct ath_rx_status *rx_stats,
			    bool *decrypt_error)
{
	struct ath_softc *sc = (struct ath_softc *) common->priv;
	bool is_mc, is_valid_tkip, strip_mic, mic_error;
	struct ath_hw *ah = common->ah;
	__le16 fc;
	u8 rx_status_len = ah->caps.rx_status_len;

	fc = hdr->frame_control;

	is_mc = !!is_multicast_ether_addr(hdr->addr1);
	is_valid_tkip = rx_stats->rs_keyix != ATH9K_RXKEYIX_INVALID &&
		test_bit(rx_stats->rs_keyix, common->tkip_keymap);
	strip_mic = is_valid_tkip && ieee80211_is_data(fc) &&
		ieee80211_has_protected(fc) &&
		!(rx_stats->rs_status &
		(ATH9K_RXERR_DECRYPT | ATH9K_RXERR_CRC | ATH9K_RXERR_MIC |
		 ATH9K_RXERR_KEYMISS));

	/*
	 * Key miss events are only relevant for pairwise keys where the
	 * descriptor does contain a valid key index. This has been observed
	 * mostly with CCMP encryption.
	 */
	if (rx_stats->rs_keyix == ATH9K_RXKEYIX_INVALID ||
	    !test_bit(rx_stats->rs_keyix, common->ccmp_keymap))
		rx_stats->rs_status &= ~ATH9K_RXERR_KEYMISS;

	if (!rx_stats->rs_datalen) {
		RX_STAT_INC(rx_len_err);
		return false;
	}

        /*
         * rs_status follows rs_datalen so if rs_datalen is too large
         * we can take a hint that hardware corrupted it, so ignore
         * those frames.
         */
	if (rx_stats->rs_datalen > (common->rx_bufsize - rx_status_len)) {
		RX_STAT_INC(rx_len_err);
		return false;
	}

	/* Only use error bits from the last fragment */
	if (rx_stats->rs_more)
		return true;

	mic_error = is_valid_tkip && !ieee80211_is_ctl(fc) &&
		!ieee80211_has_morefrags(fc) &&
		!(le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_FRAG) &&
		(rx_stats->rs_status & ATH9K_RXERR_MIC);

	/*
	 * The rx_stats->rs_status will not be set until the end of the
	 * chained descriptors so it can be ignored if rs_more is set. The
	 * rs_more will be false at the last element of the chained
	 * descriptors.
	 */
	if (rx_stats->rs_status != 0) {
		u8 status_mask;

		if (rx_stats->rs_status & ATH9K_RXERR_CRC) {
			rxs->flag |= RX_FLAG_FAILED_FCS_CRC;
			mic_error = false;
		}
		if (rx_stats->rs_status & ATH9K_RXERR_PHY)
			return false;

		if ((rx_stats->rs_status & ATH9K_RXERR_DECRYPT) ||
		    (!is_mc && (rx_stats->rs_status & ATH9K_RXERR_KEYMISS))) {
			*decrypt_error = true;
			mic_error = false;
		}

		/*
		 * Reject error frames with the exception of
		 * decryption and MIC failures. For monitor mode,
		 * we also ignore the CRC error.
		 */
		status_mask = ATH9K_RXERR_DECRYPT | ATH9K_RXERR_MIC |
			      ATH9K_RXERR_KEYMISS;

		if (ah->is_monitoring && (sc->rx.rxfilter & FIF_FCSFAIL))
			status_mask |= ATH9K_RXERR_CRC;

		if (rx_stats->rs_status & ~status_mask)
			return false;
	}

	/*
	 * For unicast frames the MIC error bit can have false positives,
	 * so all MIC error reports need to be validated in software.
	 * False negatives are not common, so skip software verification
	 * if the hardware considers the MIC valid.
	 */
	if (strip_mic)
		rxs->flag |= RX_FLAG_MMIC_STRIPPED;
	else if (is_mc && mic_error)
		rxs->flag |= RX_FLAG_MMIC_ERROR;

	return true;
}

static int ath9k_process_rate(struct ath_common *common,
			      struct ieee80211_hw *hw,
			      struct ath_rx_status *rx_stats,
			      struct ieee80211_rx_status *rxs)
{
	struct ieee80211_supported_band *sband;
	enum ieee80211_band band;
	unsigned int i = 0;
	struct ath_softc __maybe_unused *sc = common->priv;

	band = hw->conf.channel->band;
	sband = hw->wiphy->bands[band];

	if (rx_stats->rs_rate & 0x80) {
		/* HT rate */
		rxs->flag |= RX_FLAG_HT;
		if (rx_stats->rs_flags & ATH9K_RX_2040)
			rxs->flag |= RX_FLAG_40MHZ;
		if (rx_stats->rs_flags & ATH9K_RX_GI)
			rxs->flag |= RX_FLAG_SHORT_GI;
		rxs->rate_idx = rx_stats->rs_rate & 0x7f;
		return 0;
	}

	for (i = 0; i < sband->n_bitrates; i++) {
		if (sband->bitrates[i].hw_value == rx_stats->rs_rate) {
			rxs->rate_idx = i;
			return 0;
		}
		if (sband->bitrates[i].hw_value_short == rx_stats->rs_rate) {
			rxs->flag |= RX_FLAG_SHORTPRE;
			rxs->rate_idx = i;
			return 0;
		}
	}

	/*
	 * No valid hardware bitrate found -- we should not get here
	 * because hardware has already validated this frame as OK.
	 */
	ath_dbg(common, ANY,
		"unsupported hw bitrate detected 0x%02x using 1 Mbit\n",
		rx_stats->rs_rate);
	RX_STAT_INC(rx_rate_err);
	return -EINVAL;
}

static void ath9k_process_rssi(struct ath_common *common,
			       struct ieee80211_hw *hw,
			       struct ieee80211_hdr *hdr,
			       struct ath_rx_status *rx_stats)
{
	struct ath_softc *sc = hw->priv;
	struct ath_hw *ah = common->ah;
	int last_rssi;
	int rssi = rx_stats->rs_rssi;

	if (!rx_stats->is_mybeacon ||
	    ((ah->opmode != NL80211_IFTYPE_STATION) &&
	     (ah->opmode != NL80211_IFTYPE_ADHOC)))
		return;

	if (rx_stats->rs_rssi != ATH9K_RSSI_BAD && !rx_stats->rs_moreaggr)
		ATH_RSSI_LPF(sc->last_rssi, rx_stats->rs_rssi);

	last_rssi = sc->last_rssi;
	if (likely(last_rssi != ATH_RSSI_DUMMY_MARKER))
		rssi = ATH_EP_RND(last_rssi, ATH_RSSI_EP_MULTIPLIER);
	if (rssi < 0)
		rssi = 0;

	/* Update Beacon RSSI, this is used by ANI. */
	ah->stats.avgbrssi = rssi;
}

/*
 * For Decrypt or Demic errors, we only mark packet status here and always push
 * up the frame up to let mac80211 handle the actual error case, be it no
 * decryption key or real decryption error. This let us keep statistics there.
 */
static int ath9k_rx_skb_preprocess(struct ath_common *common,
				   struct ieee80211_hw *hw,
				   struct ieee80211_hdr *hdr,
				   struct ath_rx_status *rx_stats,
				   struct ieee80211_rx_status *rx_status,
				   bool *decrypt_error)
{
	struct ath_hw *ah = common->ah;

	/*
	 * everything but the rate is checked here, the rate check is done
	 * separately to avoid doing two lookups for a rate for each frame.
	 */
	if (!ath9k_rx_accept(common, hdr, rx_status, rx_stats, decrypt_error))
		return -EINVAL;

	/* Only use status info from the last fragment */
	if (rx_stats->rs_more)
		return 0;

	ath9k_process_rssi(common, hw, hdr, rx_stats);

	if (ath9k_process_rate(common, hw, rx_stats, rx_status))
		return -EINVAL;

	rx_status->band = hw->conf.channel->band;
	rx_status->freq = hw->conf.channel->center_freq;
	rx_status->signal = ah->noise + rx_stats->rs_rssi;
	rx_status->antenna = rx_stats->rs_antenna;
	rx_status->flag |= RX_FLAG_MACTIME_END;
	if (rx_stats->rs_moreaggr)
		rx_status->flag |= RX_FLAG_NO_SIGNAL_VAL;

	return 0;
}

static void ath9k_rx_skb_postprocess(struct ath_common *common,
				     struct sk_buff *skb,
				     struct ath_rx_status *rx_stats,
				     struct ieee80211_rx_status *rxs,
				     bool decrypt_error)
{
	struct ath_hw *ah = common->ah;
	struct ieee80211_hdr *hdr;
	int hdrlen, padpos, padsize;
	u8 keyix;
	__le16 fc;

	/* see if any padding is done by the hw and remove it */
	hdr = (struct ieee80211_hdr *) skb->data;
	hdrlen = ieee80211_get_hdrlen_from_skb(skb);
	fc = hdr->frame_control;
	padpos = ath9k_cmn_padpos(hdr->frame_control);

	/* The MAC header is padded to have 32-bit boundary if the
	 * packet payload is non-zero. The general calculation for
	 * padsize would take into account odd header lengths:
	 * padsize = (4 - padpos % 4) % 4; However, since only
	 * even-length headers are used, padding can only be 0 or 2
	 * bytes and we can optimize this a bit. In addition, we must
	 * not try to remove padding from short control frames that do
	 * not have payload. */
	padsize = padpos & 3;
	if (padsize && skb->len>=padpos+padsize+FCS_LEN) {
		memmove(skb->data + padsize, skb->data, padpos);
		skb_pull(skb, padsize);
	}

	keyix = rx_stats->rs_keyix;

	if (!(keyix == ATH9K_RXKEYIX_INVALID) && !decrypt_error &&
	    ieee80211_has_protected(fc)) {
		rxs->flag |= RX_FLAG_DECRYPTED;
	} else if (ieee80211_has_protected(fc)
		   && !decrypt_error && skb->len >= hdrlen + 4) {
		keyix = skb->data[hdrlen + 3] >> 6;

		if (test_bit(keyix, common->keymap))
			rxs->flag |= RX_FLAG_DECRYPTED;
	}
	if (ah->sw_mgmt_crypto &&
	    (rxs->flag & RX_FLAG_DECRYPTED) &&
	    ieee80211_is_mgmt(fc))
		/* Use software decrypt for management frames. */
		rxs->flag &= ~RX_FLAG_DECRYPTED;
}

#ifdef CONFIG_ATH9K_DEBUGFS
static s8 fix_rssi_inv_only(u8 rssi_val)
{
	if (rssi_val == 128)
		rssi_val = 0;
	return (s8) rssi_val;
}
#endif

/* returns 1 if this was a spectral frame, even if not handled. */
static int ath_process_fft(struct ath_softc *sc, struct ieee80211_hdr *hdr,
			   struct ath_rx_status *rs, u64 tsf)
{
#ifdef CONFIG_ATH9K_DEBUGFS
	struct ath_hw *ah = sc->sc_ah;
	u8 bins[SPECTRAL_HT20_NUM_BINS];
	u8 *vdata = (u8 *)hdr;
	struct fft_sample_ht20 fft_sample;
	struct ath_radar_info *radar_info;
	struct ath_ht20_mag_info *mag_info;
	int len = rs->rs_datalen;
	int dc_pos;

	/* AR9280 and before report via ATH9K_PHYERR_RADAR, AR93xx and newer
	 * via ATH9K_PHYERR_SPECTRAL. Haven't seen ATH9K_PHYERR_FALSE_RADAR_EXT
	 * yet, but this is supposed to be possible as well.
	 */
	if (rs->rs_phyerr != ATH9K_PHYERR_RADAR &&
	    rs->rs_phyerr != ATH9K_PHYERR_FALSE_RADAR_EXT &&
	    rs->rs_phyerr != ATH9K_PHYERR_SPECTRAL)
		return 0;

	/* check if spectral scan bit is set. This does not have to be checked
	 * if received through a SPECTRAL phy error, but shouldn't hurt.
	 */
	radar_info = ((struct ath_radar_info *)&vdata[len]) - 1;
	if (!(radar_info->pulse_bw_info & SPECTRAL_SCAN_BITMASK))
		return 0;

	/* Variation in the data length is possible and will be fixed later.
	 * Note that we only support HT20 for now.
	 *
	 * TODO: add HT20_40 support as well.
	 */
	if ((len > SPECTRAL_HT20_TOTAL_DATA_LEN + 2) ||
	    (len < SPECTRAL_HT20_TOTAL_DATA_LEN - 1))
		return 1;

	fft_sample.tlv.type = ATH_FFT_SAMPLE_HT20;
	fft_sample.tlv.length = sizeof(fft_sample) - sizeof(fft_sample.tlv);
	fft_sample.tlv.length = __cpu_to_be16(fft_sample.tlv.length);

	fft_sample.freq = __cpu_to_be16(ah->curchan->chan->center_freq);
	fft_sample.rssi = fix_rssi_inv_only(rs->rs_rssi_ctl0);
	fft_sample.noise = ah->noise;

	switch (len - SPECTRAL_HT20_TOTAL_DATA_LEN) {
	case 0:
		/* length correct, nothing to do. */
		memcpy(bins, vdata, SPECTRAL_HT20_NUM_BINS);
		break;
	case -1:
		/* first byte missing, duplicate it. */
		memcpy(&bins[1], vdata, SPECTRAL_HT20_NUM_BINS - 1);
		bins[0] = vdata[0];
		break;
	case 2:
		/* MAC added 2 extra bytes at bin 30 and 32, remove them. */
		memcpy(bins, vdata, 30);
		bins[30] = vdata[31];
		memcpy(&bins[31], &vdata[33], SPECTRAL_HT20_NUM_BINS - 31);
		break;
	case 1:
		/* MAC added 2 extra bytes AND first byte is missing. */
		bins[0] = vdata[0];
		memcpy(&bins[0], vdata, 30);
		bins[31] = vdata[31];
		memcpy(&bins[32], &vdata[33], SPECTRAL_HT20_NUM_BINS - 32);
		break;
	default:
		return 1;
	}

	/* DC value (value in the middle) is the blind spot of the spectral
	 * sample and invalid, interpolate it.
	 */
	dc_pos = SPECTRAL_HT20_NUM_BINS / 2;
	bins[dc_pos] = (bins[dc_pos + 1] + bins[dc_pos - 1]) / 2;

	/* mag data is at the end of the frame, in front of radar_info */
	mag_info = ((struct ath_ht20_mag_info *)radar_info) - 1;

	/* copy raw bins without scaling them */
	memcpy(fft_sample.data, bins, SPECTRAL_HT20_NUM_BINS);
	fft_sample.max_exp = mag_info->max_exp & 0xf;

	fft_sample.max_magnitude = spectral_max_magnitude(mag_info->all_bins);
	fft_sample.max_magnitude = __cpu_to_be16(fft_sample.max_magnitude);
	fft_sample.max_index = spectral_max_index(mag_info->all_bins);
	fft_sample.bitmap_weight = spectral_bitmap_weight(mag_info->all_bins);
	fft_sample.tsf = __cpu_to_be64(tsf);

	ath_debug_send_fft_sample(sc, &fft_sample.tlv);
	return 1;
#else
	return 0;
#endif
}

static void ath9k_apply_ampdu_details(struct ath_softc *sc,
	struct ath_rx_status *rs, struct ieee80211_rx_status *rxs)
{
	if (rs->rs_isaggr) {
		rxs->flag |= RX_FLAG_AMPDU_DETAILS | RX_FLAG_AMPDU_LAST_KNOWN;

		rxs->ampdu_reference = sc->rx.ampdu_ref;

		if (!rs->rs_moreaggr) {
			rxs->flag |= RX_FLAG_AMPDU_IS_LAST;
			sc->rx.ampdu_ref++;
		}

		if (rs->rs_flags & ATH9K_RX_DELIM_CRC_PRE)
			rxs->flag |= RX_FLAG_AMPDU_DELIM_CRC_ERROR;
	}
}

int ath_rx_tasklet(struct ath_softc *sc, int flush, bool hp)
{
	struct ath_buf *bf;
	struct sk_buff *skb = NULL, *requeue_skb, *hdr_skb;
	struct ieee80211_rx_status *rxs;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_hw *hw = sc->hw;
	struct ieee80211_hdr *hdr;
	int retval;
	struct ath_rx_status rs;
	enum ath9k_rx_qtype qtype;
	bool edma = !!(ah->caps.hw_caps & ATH9K_HW_CAP_EDMA);
	int dma_type;
	u8 rx_status_len = ah->caps.rx_status_len;
	u64 tsf = 0;
	u32 tsf_lower = 0;
	unsigned long flags;

	if (edma)
		dma_type = DMA_BIDIRECTIONAL;
	else
		dma_type = DMA_FROM_DEVICE;

	qtype = hp ? ATH9K_RX_QUEUE_HP : ATH9K_RX_QUEUE_LP;

	tsf = ath9k_hw_gettsf64(ah);
	tsf_lower = tsf & 0xffffffff;

	do {
		bool decrypt_error = false;

		memset(&rs, 0, sizeof(rs));
		if (edma)
			bf = ath_edma_get_next_rx_buf(sc, &rs, qtype);
		else
			bf = ath_get_next_rx_buf(sc, &rs);

		if (!bf)
			break;

		skb = bf->bf_mpdu;
		if (!skb)
			continue;

		/*
		 * Take frame header from the first fragment and RX status from
		 * the last one.
		 */
		if (sc->rx.frag)
			hdr_skb = sc->rx.frag;
		else
			hdr_skb = skb;

		hdr = (struct ieee80211_hdr *) (hdr_skb->data + rx_status_len);
		rxs = IEEE80211_SKB_RXCB(hdr_skb);
		if (ieee80211_is_beacon(hdr->frame_control)) {
			RX_STAT_INC(rx_beacons);
			if (!is_zero_ether_addr(common->curbssid) &&
			    ether_addr_equal(hdr->addr3, common->curbssid))
				rs.is_mybeacon = true;
			else
				rs.is_mybeacon = false;
		}
		else
			rs.is_mybeacon = false;

		if (ieee80211_is_data_present(hdr->frame_control) &&
		    !ieee80211_is_qos_nullfunc(hdr->frame_control))
			sc->rx.num_pkts++;

		ath_debug_stat_rx(sc, &rs);

		memset(rxs, 0, sizeof(struct ieee80211_rx_status));

		rxs->mactime = (tsf & ~0xffffffffULL) | rs.rs_tstamp;
		if (rs.rs_tstamp > tsf_lower &&
		    unlikely(rs.rs_tstamp - tsf_lower > 0x10000000))
			rxs->mactime -= 0x100000000ULL;

		if (rs.rs_tstamp < tsf_lower &&
		    unlikely(tsf_lower - rs.rs_tstamp > 0x10000000))
			rxs->mactime += 0x100000000ULL;

		if (rs.rs_status & ATH9K_RXERR_PHY) {
			if (ath_process_fft(sc, hdr, &rs, rxs->mactime)) {
				RX_STAT_INC(rx_spectral);
				goto requeue_drop_frag;
			}
		}

		retval = ath9k_rx_skb_preprocess(common, hw, hdr, &rs,
						 rxs, &decrypt_error);
		if (retval)
			goto requeue_drop_frag;

		if (rs.is_mybeacon) {
			sc->hw_busy_count = 0;
			ath_start_rx_poll(sc, 3);
		}
		/* Ensure we always have an skb to requeue once we are done
		 * processing the current buffer's skb */
		requeue_skb = ath_rxbuf_alloc(common, common->rx_bufsize, GFP_ATOMIC);

		/* If there is no memory we ignore the current RX'd frame,
		 * tell hardware it can give us a new frame using the old
		 * skb and put it at the tail of the sc->rx.rxbuf list for
		 * processing. */
		if (!requeue_skb) {
			RX_STAT_INC(rx_oom_err);
			goto requeue_drop_frag;
		}

		/* Unmap the frame */
		dma_unmap_single(sc->dev, bf->bf_buf_addr,
				 common->rx_bufsize,
				 dma_type);

		skb_put(skb, rs.rs_datalen + ah->caps.rx_status_len);
		if (ah->caps.rx_status_len)
			skb_pull(skb, ah->caps.rx_status_len);

		if (!rs.rs_more)
			ath9k_rx_skb_postprocess(common, hdr_skb, &rs,
						 rxs, decrypt_error);

		/* We will now give hardware our shiny new allocated skb */
		bf->bf_mpdu = requeue_skb;
		bf->bf_buf_addr = dma_map_single(sc->dev, requeue_skb->data,
						 common->rx_bufsize,
						 dma_type);
		if (unlikely(dma_mapping_error(sc->dev,
			  bf->bf_buf_addr))) {
			dev_kfree_skb_any(requeue_skb);
			bf->bf_mpdu = NULL;
			bf->bf_buf_addr = 0;
			ath_err(common, "dma_mapping_error() on RX\n");
			ieee80211_rx(hw, skb);
			break;
		}

		if (rs.rs_more) {
			RX_STAT_INC(rx_frags);
			/*
			 * rs_more indicates chained descriptors which can be
			 * used to link buffers together for a sort of
			 * scatter-gather operation.
			 */
			if (sc->rx.frag) {
				/* too many fragments - cannot handle frame */
				dev_kfree_skb_any(sc->rx.frag);
				dev_kfree_skb_any(skb);
				RX_STAT_INC(rx_too_many_frags_err);
				skb = NULL;
			}
			sc->rx.frag = skb;
			goto requeue;
		}

		if (sc->rx.frag) {
			int space = skb->len - skb_tailroom(hdr_skb);

			if (pskb_expand_head(hdr_skb, 0, space, GFP_ATOMIC) < 0) {
				dev_kfree_skb(skb);
				RX_STAT_INC(rx_oom_err);
				goto requeue_drop_frag;
			}

			sc->rx.frag = NULL;

			skb_copy_from_linear_data(skb, skb_put(hdr_skb, skb->len),
						  skb->len);
			dev_kfree_skb_any(skb);
			skb = hdr_skb;
		}


		if (ah->caps.hw_caps & ATH9K_HW_CAP_ANT_DIV_COMB) {

			/*
			 * change the default rx antenna if rx diversity
			 * chooses the other antenna 3 times in a row.
			 */
			if (sc->rx.defant != rs.rs_antenna) {
				if (++sc->rx.rxotherant >= 3)
					ath_setdefantenna(sc, rs.rs_antenna);
			} else {
				sc->rx.rxotherant = 0;
			}

		}

		if (rxs->flag & RX_FLAG_MMIC_STRIPPED)
			skb_trim(skb, skb->len - 8);

		spin_lock_irqsave(&sc->sc_pm_lock, flags);
		if ((sc->ps_flags & (PS_WAIT_FOR_BEACON |
				     PS_WAIT_FOR_CAB |
				     PS_WAIT_FOR_PSPOLL_DATA)) ||
		    ath9k_check_auto_sleep(sc))
			ath_rx_ps(sc, skb, rs.is_mybeacon);
		spin_unlock_irqrestore(&sc->sc_pm_lock, flags);

		if ((ah->caps.hw_caps & ATH9K_HW_CAP_ANT_DIV_COMB) && sc->ant_rx == 3)
			ath_ant_comb_scan(sc, &rs);

		ath9k_apply_ampdu_details(sc, &rs, rxs);

		ieee80211_rx(hw, skb);

requeue_drop_frag:
		if (sc->rx.frag) {
			dev_kfree_skb_any(sc->rx.frag);
			sc->rx.frag = NULL;
		}
requeue:
		list_add_tail(&bf->list, &sc->rx.rxbuf);
		if (flush)
			continue;

		if (edma) {
			ath_rx_edma_buf_link(sc, qtype);
		} else {
			ath_rx_buf_link(sc, bf);
			ath9k_hw_rxena(ah);
		}
	} while (1);

	if (!(ah->imask & ATH9K_INT_RXEOL)) {
		ah->imask |= (ATH9K_INT_RXEOL | ATH9K_INT_RXORN);
		ath9k_hw_set_interrupts(ah);
	}

	return 0;
}
