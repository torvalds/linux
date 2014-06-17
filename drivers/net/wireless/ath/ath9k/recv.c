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
#include "ath9k.h"
#include "ar9003_mac.h"

#define SKB_CB_ATHBUF(__skb)	(*((struct ath_rxbuf **)__skb->cb))

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
static void ath_rx_buf_link(struct ath_softc *sc, struct ath_rxbuf *bf,
			    bool flush)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_desc *ds;
	struct sk_buff *skb;

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

	if (sc->rx.rxlink)
		*sc->rx.rxlink = bf->bf_daddr;
	else if (!flush)
		ath9k_hw_putrxbuf(ah, bf->bf_daddr);

	sc->rx.rxlink = &ds->ds_link;
}

static void ath_rx_buf_relink(struct ath_softc *sc, struct ath_rxbuf *bf,
			      bool flush)
{
	if (sc->rx.buf_hold)
		ath_rx_buf_link(sc, sc->rx.buf_hold, flush);

	sc->rx.buf_hold = bf;
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
	struct ath_rxbuf *bf;

	rx_edma = &sc->rx.rx_edma[qtype];
	if (skb_queue_len(&rx_edma->rx_fifo) >= rx_edma->rx_fifo_hwsize)
		return false;

	bf = list_first_entry(&sc->rx.rxbuf, struct ath_rxbuf, list);
	list_del_init(&bf->list);

	skb = bf->bf_mpdu;

	memset(skb->data, 0, ah->caps.rx_status_len);
	dma_sync_single_for_device(sc->dev, bf->bf_buf_addr,
				ah->caps.rx_status_len, DMA_TO_DEVICE);

	SKB_CB_ATHBUF(skb) = bf;
	ath9k_hw_addrxbuf_edma(ah, bf->bf_buf_addr, qtype);
	__skb_queue_tail(&rx_edma->rx_fifo, skb);

	return true;
}

static void ath_rx_addbuffer_edma(struct ath_softc *sc,
				  enum ath9k_rx_qtype qtype)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_rxbuf *bf, *tbf;

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
	struct ath_rxbuf *bf;
	struct ath_rx_edma *rx_edma;
	struct sk_buff *skb;

	rx_edma = &sc->rx.rx_edma[qtype];

	while ((skb = __skb_dequeue(&rx_edma->rx_fifo)) != NULL) {
		bf = SKB_CB_ATHBUF(skb);
		BUG_ON(!bf);
		list_add_tail(&bf->list, &sc->rx.rxbuf);
	}
}

static void ath_rx_edma_cleanup(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_rxbuf *bf;

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
	__skb_queue_head_init(&rx_edma->rx_fifo);
	rx_edma->rx_fifo_hwsize = size;
}

static int ath_rx_edma_init(struct ath_softc *sc, int nbufs)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_hw *ah = sc->sc_ah;
	struct sk_buff *skb;
	struct ath_rxbuf *bf;
	int error = 0, i;
	u32 size;

	ath9k_hw_set_rx_bufsize(ah, common->rx_bufsize -
				    ah->caps.rx_status_len);

	ath_rx_edma_init_queue(&sc->rx.rx_edma[ATH9K_RX_QUEUE_LP],
			       ah->caps.rx_lp_qdepth);
	ath_rx_edma_init_queue(&sc->rx.rx_edma[ATH9K_RX_QUEUE_HP],
			       ah->caps.rx_hp_qdepth);

	size = sizeof(struct ath_rxbuf) * nbufs;
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
	ath_rx_addbuffer_edma(sc, ATH9K_RX_QUEUE_HP);
	ath_rx_addbuffer_edma(sc, ATH9K_RX_QUEUE_LP);
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
	struct ath_rxbuf *bf;
	int error = 0;

	spin_lock_init(&sc->sc_pcu_lock);

	common->rx_bufsize = IEEE80211_MAX_MPDU_LEN / 2 +
			     sc->sc_ah->caps.rx_status_len;

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA)
		return ath_rx_edma_init(sc, nbufs);

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
	struct ath_rxbuf *bf;

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) {
		ath_rx_edma_cleanup(sc);
		return;
	}

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

	if (config_enabled(CONFIG_ATH9K_TX99))
		return 0;

	rfilt = ATH9K_RX_FILTER_UCAST | ATH9K_RX_FILTER_BCAST
		| ATH9K_RX_FILTER_MCAST;

	/* if operating on a DFS channel, enable radar pulse detection */
	if (sc->hw->conf.radar_enabled)
		rfilt |= ATH9K_RX_FILTER_PHYRADAR | ATH9K_RX_FILTER_PHYERR;

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

	if (AR_SREV_9550(sc->sc_ah) || AR_SREV_9531(sc->sc_ah))
		rfilt |= ATH9K_RX_FILTER_4ADDRESS;

	return rfilt;

}

int ath_startrecv(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_rxbuf *bf, *tbf;

	if (ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) {
		ath_edma_start_recv(sc);
		return 0;
	}

	if (list_empty(&sc->rx.rxbuf))
		goto start_recv;

	sc->rx.buf_hold = NULL;
	sc->rx.rxlink = NULL;
	list_for_each_entry_safe(bf, tbf, &sc->rx.rxbuf, list) {
		ath_rx_buf_link(sc, bf, false);
	}

	/* We could have deleted elements so the list may be empty now */
	if (list_empty(&sc->rx.rxbuf))
		goto start_recv;

	bf = list_first_entry(&sc->rx.rxbuf, struct ath_rxbuf, list);
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
			"Reconfigure beacon timers based on synchronized timestamp\n");
		if (!(WARN_ON_ONCE(sc->cur_beacon_conf.beacon_interval == 0)))
			ath9k_set_beacon(sc);
		if (sc->p2p_ps_vif)
			ath9k_update_p2p_ps(sc, sc->p2p_ps_vif->vif);
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
				 struct ath_rxbuf **dest)
{
	struct ath_rx_edma *rx_edma = &sc->rx.rx_edma[qtype];
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct sk_buff *skb;
	struct ath_rxbuf *bf;
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

static struct ath_rxbuf *ath_edma_get_next_rx_buf(struct ath_softc *sc,
						struct ath_rx_status *rs,
						enum ath9k_rx_qtype qtype)
{
	struct ath_rxbuf *bf = NULL;

	while (ath_edma_get_buffers(sc, qtype, rs, &bf)) {
		if (!bf)
			continue;

		return bf;
	}
	return NULL;
}

static struct ath_rxbuf *ath_get_next_rx_buf(struct ath_softc *sc,
					   struct ath_rx_status *rs)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_desc *ds;
	struct ath_rxbuf *bf;
	int ret;

	if (list_empty(&sc->rx.rxbuf)) {
		sc->rx.rxlink = NULL;
		return NULL;
	}

	bf = list_first_entry(&sc->rx.rxbuf, struct ath_rxbuf, list);
	if (bf == sc->rx.buf_hold)
		return NULL;

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
		struct ath_rxbuf *tbf;
		struct ath_desc *tds;

		memset(&trs, 0, sizeof(trs));
		if (list_is_last(&bf->list, &sc->rx.rxbuf)) {
			sc->rx.rxlink = NULL;
			return NULL;
		}

		tbf = list_entry(bf->list.next, struct ath_rxbuf, list);

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

		/*
		 * Re-check previous descriptor, in case it has been filled
		 * in the mean time.
		 */
		ret = ath9k_hw_rxprocdesc(ah, ds, rs);
		if (ret == -EINPROGRESS) {
			/*
			 * mark descriptor as zero-length and set the 'more'
			 * flag to ensure that both buffers get discarded
			 */
			rs->rs_datalen = 0;
			rs->rs_more = true;
		}
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

static void ath9k_process_tsf(struct ath_rx_status *rs,
			      struct ieee80211_rx_status *rxs,
			      u64 tsf)
{
	u32 tsf_lower = tsf & 0xffffffff;

	rxs->mactime = (tsf & ~0xffffffffULL) | rs->rs_tstamp;
	if (rs->rs_tstamp > tsf_lower &&
	    unlikely(rs->rs_tstamp - tsf_lower > 0x10000000))
		rxs->mactime -= 0x100000000ULL;

	if (rs->rs_tstamp < tsf_lower &&
	    unlikely(tsf_lower - rs->rs_tstamp > 0x10000000))
		rxs->mactime += 0x100000000ULL;
}

/*
 * For Decrypt or Demic errors, we only mark packet status here and always push
 * up the frame up to let mac80211 handle the actual error case, be it no
 * decryption key or real decryption error. This let us keep statistics there.
 */
static int ath9k_rx_skb_preprocess(struct ath_softc *sc,
				   struct sk_buff *skb,
				   struct ath_rx_status *rx_stats,
				   struct ieee80211_rx_status *rx_status,
				   bool *decrypt_error, u64 tsf)
{
	struct ieee80211_hw *hw = sc->hw;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_hdr *hdr;
	bool discard_current = sc->rx.discard_next;

	/*
	 * Discard corrupt descriptors which are marked in
	 * ath_get_next_rx_buf().
	 */
	if (discard_current)
		goto corrupt;

	sc->rx.discard_next = false;

	/*
	 * Discard zero-length packets.
	 */
	if (!rx_stats->rs_datalen) {
		RX_STAT_INC(rx_len_err);
		goto corrupt;
	}

	/*
	 * rs_status follows rs_datalen so if rs_datalen is too large
	 * we can take a hint that hardware corrupted it, so ignore
	 * those frames.
	 */
	if (rx_stats->rs_datalen > (common->rx_bufsize - ah->caps.rx_status_len)) {
		RX_STAT_INC(rx_len_err);
		goto corrupt;
	}

	/* Only use status info from the last fragment */
	if (rx_stats->rs_more)
		return 0;

	/*
	 * Return immediately if the RX descriptor has been marked
	 * as corrupt based on the various error bits.
	 *
	 * This is different from the other corrupt descriptor
	 * condition handled above.
	 */
	if (rx_stats->rs_status & ATH9K_RXERR_CORRUPT_DESC)
		goto corrupt;

	hdr = (struct ieee80211_hdr *) (skb->data + ah->caps.rx_status_len);

	ath9k_process_tsf(rx_stats, rx_status, tsf);
	ath_debug_stat_rx(sc, rx_stats);

	/*
	 * Process PHY errors and return so that the packet
	 * can be dropped.
	 */
	if (rx_stats->rs_status & ATH9K_RXERR_PHY) {
		ath9k_dfs_process_phyerr(sc, hdr, rx_stats, rx_status->mactime);
		if (ath_process_fft(sc, hdr, rx_stats, rx_status->mactime))
			RX_STAT_INC(rx_spectral);

		return -EINVAL;
	}

	/*
	 * everything but the rate is checked here, the rate check is done
	 * separately to avoid doing two lookups for a rate for each frame.
	 */
	if (!ath9k_cmn_rx_accept(common, hdr, rx_status, rx_stats, decrypt_error, sc->rx.rxfilter))
		return -EINVAL;

	if (ath_is_mybeacon(common, hdr)) {
		RX_STAT_INC(rx_beacons);
		rx_stats->is_mybeacon = true;
	}

	/*
	 * This shouldn't happen, but have a safety check anyway.
	 */
	if (WARN_ON(!ah->curchan))
		return -EINVAL;

	if (ath9k_cmn_process_rate(common, hw, rx_stats, rx_status)) {
		/*
		 * No valid hardware bitrate found -- we should not get here
		 * because hardware has already validated this frame as OK.
		 */
		ath_dbg(common, ANY, "unsupported hw bitrate detected 0x%02x using 1 Mbit\n",
			rx_stats->rs_rate);
		RX_STAT_INC(rx_rate_err);
		return -EINVAL;
	}

	ath9k_cmn_process_rssi(common, hw, rx_stats, rx_status);

	rx_status->band = ah->curchan->chan->band;
	rx_status->freq = ah->curchan->chan->center_freq;
	rx_status->antenna = rx_stats->rs_antenna;
	rx_status->flag |= RX_FLAG_MACTIME_END;

#ifdef CONFIG_ATH9K_BTCOEX_SUPPORT
	if (ieee80211_is_data_present(hdr->frame_control) &&
	    !ieee80211_is_qos_nullfunc(hdr->frame_control))
		sc->rx.num_pkts++;
#endif

	return 0;

corrupt:
	sc->rx.discard_next = rx_stats->rs_more;
	return -EINVAL;
}

/*
 * Run the LNA combining algorithm only in these cases:
 *
 * Standalone WLAN cards with both LNA/Antenna diversity
 * enabled in the EEPROM.
 *
 * WLAN+BT cards which are in the supported card list
 * in ath_pci_id_table and the user has loaded the
 * driver with "bt_ant_diversity" set to true.
 */
static void ath9k_antenna_check(struct ath_softc *sc,
				struct ath_rx_status *rs)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath9k_hw_capabilities *pCap = &ah->caps;
	struct ath_common *common = ath9k_hw_common(ah);

	if (!(ah->caps.hw_caps & ATH9K_HW_CAP_ANT_DIV_COMB))
		return;

	/*
	 * Change the default rx antenna if rx diversity
	 * chooses the other antenna 3 times in a row.
	 */
	if (sc->rx.defant != rs->rs_antenna) {
		if (++sc->rx.rxotherant >= 3)
			ath_setdefantenna(sc, rs->rs_antenna);
	} else {
		sc->rx.rxotherant = 0;
	}

	if (pCap->hw_caps & ATH9K_HW_CAP_BT_ANT_DIV) {
		if (common->bt_ant_diversity)
			ath_ant_comb_scan(sc, rs);
	} else {
		ath_ant_comb_scan(sc, rs);
	}
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
	struct ath_rxbuf *bf;
	struct sk_buff *skb = NULL, *requeue_skb, *hdr_skb;
	struct ieee80211_rx_status *rxs;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_hw *hw = sc->hw;
	int retval;
	struct ath_rx_status rs;
	enum ath9k_rx_qtype qtype;
	bool edma = !!(ah->caps.hw_caps & ATH9K_HW_CAP_EDMA);
	int dma_type;
	u64 tsf = 0;
	unsigned long flags;
	dma_addr_t new_buf_addr;
	unsigned int budget = 512;

	if (edma)
		dma_type = DMA_BIDIRECTIONAL;
	else
		dma_type = DMA_FROM_DEVICE;

	qtype = hp ? ATH9K_RX_QUEUE_HP : ATH9K_RX_QUEUE_LP;

	tsf = ath9k_hw_gettsf64(ah);

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

		rxs = IEEE80211_SKB_RXCB(hdr_skb);
		memset(rxs, 0, sizeof(struct ieee80211_rx_status));

		retval = ath9k_rx_skb_preprocess(sc, hdr_skb, &rs, rxs,
						 &decrypt_error, tsf);
		if (retval)
			goto requeue_drop_frag;

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

		/* We will now give hardware our shiny new allocated skb */
		new_buf_addr = dma_map_single(sc->dev, requeue_skb->data,
					      common->rx_bufsize, dma_type);
		if (unlikely(dma_mapping_error(sc->dev, new_buf_addr))) {
			dev_kfree_skb_any(requeue_skb);
			goto requeue_drop_frag;
		}

		/* Unmap the frame */
		dma_unmap_single(sc->dev, bf->bf_buf_addr,
				 common->rx_bufsize, dma_type);

		bf->bf_mpdu = requeue_skb;
		bf->bf_buf_addr = new_buf_addr;

		skb_put(skb, rs.rs_datalen + ah->caps.rx_status_len);
		if (ah->caps.rx_status_len)
			skb_pull(skb, ah->caps.rx_status_len);

		if (!rs.rs_more)
			ath9k_cmn_rx_skb_postprocess(common, hdr_skb, &rs,
						     rxs, decrypt_error);

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

		if (rxs->flag & RX_FLAG_MMIC_STRIPPED)
			skb_trim(skb, skb->len - 8);

		spin_lock_irqsave(&sc->sc_pm_lock, flags);
		if ((sc->ps_flags & (PS_WAIT_FOR_BEACON |
				     PS_WAIT_FOR_CAB |
				     PS_WAIT_FOR_PSPOLL_DATA)) ||
		    ath9k_check_auto_sleep(sc))
			ath_rx_ps(sc, skb, rs.is_mybeacon);
		spin_unlock_irqrestore(&sc->sc_pm_lock, flags);

		ath9k_antenna_check(sc, &rs);
		ath9k_apply_ampdu_details(sc, &rs, rxs);
		ath_debug_rate_stats(sc, &rs, skb);

		ieee80211_rx(hw, skb);

requeue_drop_frag:
		if (sc->rx.frag) {
			dev_kfree_skb_any(sc->rx.frag);
			sc->rx.frag = NULL;
		}
requeue:
		list_add_tail(&bf->list, &sc->rx.rxbuf);

		if (!edma) {
			ath_rx_buf_relink(sc, bf, flush);
			if (!flush)
				ath9k_hw_rxena(ah);
		} else if (!flush) {
			ath_rx_edma_buf_link(sc, qtype);
		}

		if (!budget--)
			break;
	} while (1);

	if (!(ah->imask & ATH9K_INT_RXEOL)) {
		ah->imask |= (ATH9K_INT_RXEOL | ATH9K_INT_RXORN);
		ath9k_hw_set_interrupts(ah);
	}

	return 0;
}
