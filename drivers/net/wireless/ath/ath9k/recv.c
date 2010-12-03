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
#include "ar9003_mac.h"

#define SKB_CB_ATHBUF(__skb)	(*((struct ath_buf **)__skb->cb))

static inline bool ath_is_alt_ant_ratio_better(int alt_ratio, int maxdelta,
					       int mindelta, int main_rssi_avg,
					       int alt_rssi_avg, int pkt_count)
{
	return (((alt_ratio >= ATH_ANT_DIV_COMB_ALT_ANT_RATIO2) &&
		(alt_rssi_avg > main_rssi_avg + maxdelta)) ||
		(alt_rssi_avg > main_rssi_avg + mindelta)) && (pkt_count > 50);
}

static inline bool ath9k_check_auto_sleep(struct ath_softc *sc)
{
	return sc->ps_enabled &&
	       (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_AUTOSLEEP);
}

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
	u32 nbuf = 0;

	if (list_empty(&sc->rx.rxbuf)) {
		ath_print(common, ATH_DBG_QUEUE, "No free rx buf available\n");
		return;
	}

	while (!list_empty(&sc->rx.rxbuf)) {
		nbuf++;

		if (!ath_rx_edma_buf_link(sc, qtype))
			break;

		if (nbuf >= size)
			break;
	}
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
	struct ath_buf *bf;

	ath_rx_remove_buffer(sc, ATH9K_RX_QUEUE_LP);
	ath_rx_remove_buffer(sc, ATH9K_RX_QUEUE_HP);

	list_for_each_entry(bf, &sc->rx.rxbuf, list) {
		if (bf->bf_mpdu)
			dev_kfree_skb_any(bf->bf_mpdu);
	}

	INIT_LIST_HEAD(&sc->rx.rxbuf);

	kfree(sc->rx.rx_bufptr);
	sc->rx.rx_bufptr = NULL;
}

static void ath_rx_edma_init_queue(struct ath_rx_edma *rx_edma, int size)
{
	skb_queue_head_init(&rx_edma->rx_fifo);
	skb_queue_head_init(&rx_edma->rx_buffers);
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


	common->rx_bufsize = roundup(IEEE80211_MAX_MPDU_LEN +
				     ah->caps.rx_status_len,
				     min(common->cachelsz, (u16)64));

	ath9k_hw_set_rx_bufsize(ah, common->rx_bufsize -
				    ah->caps.rx_status_len);

	ath_rx_edma_init_queue(&sc->rx.rx_edma[ATH9K_RX_QUEUE_LP],
			       ah->caps.rx_lp_qdepth);
	ath_rx_edma_init_queue(&sc->rx.rx_edma[ATH9K_RX_QUEUE_HP],
			       ah->caps.rx_hp_qdepth);

	size = sizeof(struct ath_buf) * nbufs;
	bf = kzalloc(size, GFP_KERNEL);
	if (!bf)
		return -ENOMEM;

	INIT_LIST_HEAD(&sc->rx.rxbuf);
	sc->rx.rx_bufptr = bf;

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
	spin_lock_bh(&sc->rx.rxbuflock);

	ath9k_hw_rxena(sc->sc_ah);

	ath_rx_addbuffer_edma(sc, ATH9K_RX_QUEUE_HP,
			      sc->rx.rx_edma[ATH9K_RX_QUEUE_HP].rx_fifo_hwsize);

	ath_rx_addbuffer_edma(sc, ATH9K_RX_QUEUE_LP,
			      sc->rx.rx_edma[ATH9K_RX_QUEUE_LP].rx_fifo_hwsize);

	ath_opmode_init(sc);

	ath9k_hw_startpcureceive(sc->sc_ah, (sc->sc_flags & SC_OP_OFFCHANNEL));

	spin_unlock_bh(&sc->rx.rxbuflock);
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
	sc->sc_flags &= ~SC_OP_RXFLUSH;
	spin_lock_init(&sc->rx.rxbuflock);

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) {
		return ath_rx_edma_init(sc, nbufs);
	} else {
		common->rx_bufsize = roundup(IEEE80211_MAX_MPDU_LEN,
				min(common->cachelsz, (u16)64));

		ath_print(common, ATH_DBG_CONFIG, "cachelsz %u rxbufsize %u\n",
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

		if (sc->rx.rxdma.dd_desc_len != 0)
			ath_descdma_cleanup(sc, &sc->rx.rxdma, &sc->rx.rxbuf);
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
#define	RX_FILTER_PRESERVE (ATH9K_RX_FILTER_PHYERR | ATH9K_RX_FILTER_PHYRADAR)

	u32 rfilt;

	rfilt = (ath9k_hw_getrxfilter(sc->sc_ah) & RX_FILTER_PRESERVE)
		| ATH9K_RX_FILTER_UCAST | ATH9K_RX_FILTER_BCAST
		| ATH9K_RX_FILTER_MCAST;

	if (sc->rx.rxfilter & FIF_PROBE_REQ)
		rfilt |= ATH9K_RX_FILTER_PROBEREQ;

	/*
	 * Set promiscuous mode when FIF_PROMISC_IN_BSS is enabled for station
	 * mode interface or when in monitor mode. AP mode does not need this
	 * since it receives all in-BSS frames anyway.
	 */
	if (((sc->sc_ah->opmode != NL80211_IFTYPE_AP) &&
	     (sc->rx.rxfilter & FIF_PROMISC_IN_BSS)) ||
	    (sc->sc_ah->is_monitoring))
		rfilt |= ATH9K_RX_FILTER_PROM;

	if (sc->rx.rxfilter & FIF_CONTROL)
		rfilt |= ATH9K_RX_FILTER_CONTROL;

	if ((sc->sc_ah->opmode == NL80211_IFTYPE_STATION) &&
	    (sc->nvifs <= 1) &&
	    !(sc->rx.rxfilter & FIF_BCN_PRBRESP_PROMISC))
		rfilt |= ATH9K_RX_FILTER_MYBEACON;
	else
		rfilt |= ATH9K_RX_FILTER_BEACON;

	if ((AR_SREV_9280_20_OR_LATER(sc->sc_ah) ||
	    AR_SREV_9285_12_OR_LATER(sc->sc_ah)) &&
	    (sc->sc_ah->opmode == NL80211_IFTYPE_AP) &&
	    (sc->rx.rxfilter & FIF_PSPOLL))
		rfilt |= ATH9K_RX_FILTER_PSPOLL;

	if (conf_is_ht(&sc->hw->conf))
		rfilt |= ATH9K_RX_FILTER_COMP_BAR;

	if (sc->sec_wiphy || (sc->nvifs > 1) ||
	    (sc->rx.rxfilter & FIF_OTHER_BSS)) {
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

	if (ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) {
		ath_edma_start_recv(sc);
		return 0;
	}

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
	ath_opmode_init(sc);
	ath9k_hw_startpcureceive(ah, (sc->sc_flags & SC_OP_OFFCHANNEL));

	spin_unlock_bh(&sc->rx.rxbuflock);

	return 0;
}

bool ath_stoprecv(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	bool stopped;

	spin_lock_bh(&sc->rx.rxbuflock);
	ath9k_hw_abortpcurecv(ah);
	ath9k_hw_setrxfilter(ah, 0);
	stopped = ath9k_hw_stopdmarecv(ah);

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA)
		ath_edma_stop_recv(sc);
	else
		sc->rx.rxlink = NULL;
	spin_unlock_bh(&sc->rx.rxbuflock);

	ATH_DBG_WARN(!stopped, "Could not stop RX, we could be "
		     "confusing the DMA engine when we start RX up\n");
	return stopped;
}

void ath_flushrecv(struct ath_softc *sc)
{
	sc->sc_flags |= SC_OP_RXFLUSH;
	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_EDMA)
		ath_rx_tasklet(sc, 1, true);
	ath_rx_tasklet(sc, 1, false);
	sc->sc_flags &= ~SC_OP_RXFLUSH;
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
	if (((sc->ps_flags & PS_WAIT_FOR_BEACON) || ath9k_check_auto_sleep(sc))
	    && ieee80211_is_beacon(hdr->frame_control))
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
		sc->ps_flags &= ~(PS_WAIT_FOR_CAB | PS_WAIT_FOR_BEACON);
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

static bool ath_edma_get_buffers(struct ath_softc *sc,
				 enum ath9k_rx_qtype qtype)
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

	ret = ath9k_hw_process_rxdesc_edma(ah, NULL, skb->data);
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
		if (!skb)
			return true;

		bf = SKB_CB_ATHBUF(skb);
		BUG_ON(!bf);

		__skb_unlink(skb, &rx_edma->rx_fifo);
		list_add_tail(&bf->list, &sc->rx.rxbuf);
		ath_rx_edma_buf_link(sc, qtype);
		return true;
	}
	skb_queue_tail(&rx_edma->rx_buffers, skb);

	return true;
}

static struct ath_buf *ath_edma_get_next_rx_buf(struct ath_softc *sc,
						struct ath_rx_status *rs,
						enum ath9k_rx_qtype qtype)
{
	struct ath_rx_edma *rx_edma = &sc->rx.rx_edma[qtype];
	struct sk_buff *skb;
	struct ath_buf *bf;

	while (ath_edma_get_buffers(sc, qtype));
	skb = __skb_dequeue(&rx_edma->rx_buffers);
	if (!skb)
		return NULL;

	bf = SKB_CB_ATHBUF(skb);
	ath9k_hw_process_rxdesc_edma(sc->sc_ah, rs, skb->data);
	return bf;
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
	ret = ath9k_hw_rxprocdesc(ah, ds, rs, 0);
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
		ret = ath9k_hw_rxprocdesc(ah, tds, &trs, 0);
		if (ret == -EINPROGRESS)
			return NULL;
	}

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
	struct ath_hw *ah = common->ah;
	__le16 fc;
	u8 rx_status_len = ah->caps.rx_status_len;

	fc = hdr->frame_control;

	if (!rx_stats->rs_datalen)
		return false;
        /*
         * rs_status follows rs_datalen so if rs_datalen is too large
         * we can take a hint that hardware corrupted it, so ignore
         * those frames.
         */
	if (rx_stats->rs_datalen > (common->rx_bufsize - rx_status_len))
		return false;

	/*
	 * rs_more indicates chained descriptors which can be used
	 * to link buffers together for a sort of scatter-gather
	 * operation.
	 * reject the frame, we don't support scatter-gather yet and
	 * the frame is probably corrupt anyway
	 */
	if (rx_stats->rs_more)
		return false;

	/*
	 * The rx_stats->rs_status will not be set until the end of the
	 * chained descriptors so it can be ignored if rs_more is set. The
	 * rs_more will be false at the last element of the chained
	 * descriptors.
	 */
	if (rx_stats->rs_status != 0) {
		if (rx_stats->rs_status & ATH9K_RXERR_CRC)
			rxs->flag |= RX_FLAG_FAILED_FCS_CRC;
		if (rx_stats->rs_status & ATH9K_RXERR_PHY)
			return false;

		if (rx_stats->rs_status & ATH9K_RXERR_DECRYPT) {
			*decrypt_error = true;
		} else if (rx_stats->rs_status & ATH9K_RXERR_MIC) {
			/*
			 * The MIC error bit is only valid if the frame
			 * is not a control frame or fragment, and it was
			 * decrypted using a valid TKIP key.
			 */
			if (!ieee80211_is_ctl(fc) &&
			    !ieee80211_has_morefrags(fc) &&
			    !(le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_FRAG) &&
			    test_bit(rx_stats->rs_keyix, common->tkip_keymap))
				rxs->flag |= RX_FLAG_MMIC_ERROR;
			else
				rx_stats->rs_status &= ~ATH9K_RXERR_MIC;
		}
		/*
		 * Reject error frames with the exception of
		 * decryption and MIC failures. For monitor mode,
		 * we also ignore the CRC error.
		 */
		if (ah->is_monitoring) {
			if (rx_stats->rs_status &
			    ~(ATH9K_RXERR_DECRYPT | ATH9K_RXERR_MIC |
			      ATH9K_RXERR_CRC))
				return false;
		} else {
			if (rx_stats->rs_status &
			    ~(ATH9K_RXERR_DECRYPT | ATH9K_RXERR_MIC)) {
				return false;
			}
		}
	}
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
	ath_print(common, ATH_DBG_XMIT, "unsupported hw bitrate detected "
		  "0x%02x using 1 Mbit\n", rx_stats->rs_rate);

	return -EINVAL;
}

static void ath9k_process_rssi(struct ath_common *common,
			       struct ieee80211_hw *hw,
			       struct ieee80211_hdr *hdr,
			       struct ath_rx_status *rx_stats)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_hw *ah = common->ah;
	int last_rssi;
	__le16 fc;

	if (ah->opmode != NL80211_IFTYPE_STATION)
		return;

	fc = hdr->frame_control;
	if (!ieee80211_is_beacon(fc) ||
	    compare_ether_addr(hdr->addr3, common->curbssid))
		return;

	if (rx_stats->rs_rssi != ATH9K_RSSI_BAD && !rx_stats->rs_moreaggr)
		ATH_RSSI_LPF(aphy->last_rssi, rx_stats->rs_rssi);

	last_rssi = aphy->last_rssi;
	if (likely(last_rssi != ATH_RSSI_DUMMY_MARKER))
		rx_stats->rs_rssi = ATH_EP_RND(last_rssi,
					      ATH_RSSI_EP_MULTIPLIER);
	if (rx_stats->rs_rssi < 0)
		rx_stats->rs_rssi = 0;

	/* Update Beacon RSSI, this is used by ANI. */
	ah->stats.avgbrssi = rx_stats->rs_rssi;
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
	memset(rx_status, 0, sizeof(struct ieee80211_rx_status));

	/*
	 * everything but the rate is checked here, the rate check is done
	 * separately to avoid doing two lookups for a rate for each frame.
	 */
	if (!ath9k_rx_accept(common, hdr, rx_status, rx_stats, decrypt_error))
		return -EINVAL;

	ath9k_process_rssi(common, hw, hdr, rx_stats);

	if (ath9k_process_rate(common, hw, rx_stats, rx_status))
		return -EINVAL;

	rx_status->band = hw->conf.channel->band;
	rx_status->freq = hw->conf.channel->center_freq;
	rx_status->signal = ATH_DEFAULT_NOISE_FLOOR + rx_stats->rs_rssi;
	rx_status->antenna = rx_stats->rs_antenna;
	rx_status->flag |= RX_FLAG_TSFT;

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

static void ath_lnaconf_alt_good_scan(struct ath_ant_comb *antcomb,
				      struct ath_hw_antcomb_conf ant_conf,
				      int main_rssi_avg)
{
	antcomb->quick_scan_cnt = 0;

	if (ant_conf.main_lna_conf == ATH_ANT_DIV_COMB_LNA2)
		antcomb->rssi_lna2 = main_rssi_avg;
	else if (ant_conf.main_lna_conf == ATH_ANT_DIV_COMB_LNA1)
		antcomb->rssi_lna1 = main_rssi_avg;

	switch ((ant_conf.main_lna_conf << 4) | ant_conf.alt_lna_conf) {
	case (0x10): /* LNA2 A-B */
		antcomb->main_conf = ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		antcomb->first_quick_scan_conf =
			ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		antcomb->second_quick_scan_conf = ATH_ANT_DIV_COMB_LNA1;
		break;
	case (0x20): /* LNA1 A-B */
		antcomb->main_conf = ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		antcomb->first_quick_scan_conf =
			ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		antcomb->second_quick_scan_conf = ATH_ANT_DIV_COMB_LNA2;
		break;
	case (0x21): /* LNA1 LNA2 */
		antcomb->main_conf = ATH_ANT_DIV_COMB_LNA2;
		antcomb->first_quick_scan_conf =
			ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		antcomb->second_quick_scan_conf =
			ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		break;
	case (0x12): /* LNA2 LNA1 */
		antcomb->main_conf = ATH_ANT_DIV_COMB_LNA1;
		antcomb->first_quick_scan_conf =
			ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		antcomb->second_quick_scan_conf =
			ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		break;
	case (0x13): /* LNA2 A+B */
		antcomb->main_conf = ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		antcomb->first_quick_scan_conf =
			ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		antcomb->second_quick_scan_conf = ATH_ANT_DIV_COMB_LNA1;
		break;
	case (0x23): /* LNA1 A+B */
		antcomb->main_conf = ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
		antcomb->first_quick_scan_conf =
			ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
		antcomb->second_quick_scan_conf = ATH_ANT_DIV_COMB_LNA2;
		break;
	default:
		break;
	}
}

static void ath_select_ant_div_from_quick_scan(struct ath_ant_comb *antcomb,
				struct ath_hw_antcomb_conf *div_ant_conf,
				int main_rssi_avg, int alt_rssi_avg,
				int alt_ratio)
{
	/* alt_good */
	switch (antcomb->quick_scan_cnt) {
	case 0:
		/* set alt to main, and alt to first conf */
		div_ant_conf->main_lna_conf = antcomb->main_conf;
		div_ant_conf->alt_lna_conf = antcomb->first_quick_scan_conf;
		break;
	case 1:
		/* set alt to main, and alt to first conf */
		div_ant_conf->main_lna_conf = antcomb->main_conf;
		div_ant_conf->alt_lna_conf = antcomb->second_quick_scan_conf;
		antcomb->rssi_first = main_rssi_avg;
		antcomb->rssi_second = alt_rssi_avg;

		if (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA1) {
			/* main is LNA1 */
			if (ath_is_alt_ant_ratio_better(alt_ratio,
						ATH_ANT_DIV_COMB_LNA1_DELTA_HI,
						ATH_ANT_DIV_COMB_LNA1_DELTA_LOW,
						main_rssi_avg, alt_rssi_avg,
						antcomb->total_pkt_count))
				antcomb->first_ratio = true;
			else
				antcomb->first_ratio = false;
		} else if (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA2) {
			if (ath_is_alt_ant_ratio_better(alt_ratio,
						ATH_ANT_DIV_COMB_LNA1_DELTA_MID,
						ATH_ANT_DIV_COMB_LNA1_DELTA_LOW,
						main_rssi_avg, alt_rssi_avg,
						antcomb->total_pkt_count))
				antcomb->first_ratio = true;
			else
				antcomb->first_ratio = false;
		} else {
			if ((((alt_ratio >= ATH_ANT_DIV_COMB_ALT_ANT_RATIO2) &&
			    (alt_rssi_avg > main_rssi_avg +
			    ATH_ANT_DIV_COMB_LNA1_DELTA_HI)) ||
			    (alt_rssi_avg > main_rssi_avg)) &&
			    (antcomb->total_pkt_count > 50))
				antcomb->first_ratio = true;
			else
				antcomb->first_ratio = false;
		}
		break;
	case 2:
		antcomb->alt_good = false;
		antcomb->scan_not_start = false;
		antcomb->scan = false;
		antcomb->rssi_first = main_rssi_avg;
		antcomb->rssi_third = alt_rssi_avg;

		if (antcomb->second_quick_scan_conf == ATH_ANT_DIV_COMB_LNA1)
			antcomb->rssi_lna1 = alt_rssi_avg;
		else if (antcomb->second_quick_scan_conf ==
			 ATH_ANT_DIV_COMB_LNA2)
			antcomb->rssi_lna2 = alt_rssi_avg;
		else if (antcomb->second_quick_scan_conf ==
			 ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2) {
			if (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA2)
				antcomb->rssi_lna2 = main_rssi_avg;
			else if (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA1)
				antcomb->rssi_lna1 = main_rssi_avg;
		}

		if (antcomb->rssi_lna2 > antcomb->rssi_lna1 +
		    ATH_ANT_DIV_COMB_LNA1_LNA2_SWITCH_DELTA)
			div_ant_conf->main_lna_conf = ATH_ANT_DIV_COMB_LNA2;
		else
			div_ant_conf->main_lna_conf = ATH_ANT_DIV_COMB_LNA1;

		if (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA1) {
			if (ath_is_alt_ant_ratio_better(alt_ratio,
						ATH_ANT_DIV_COMB_LNA1_DELTA_HI,
						ATH_ANT_DIV_COMB_LNA1_DELTA_LOW,
						main_rssi_avg, alt_rssi_avg,
						antcomb->total_pkt_count))
				antcomb->second_ratio = true;
			else
				antcomb->second_ratio = false;
		} else if (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA2) {
			if (ath_is_alt_ant_ratio_better(alt_ratio,
						ATH_ANT_DIV_COMB_LNA1_DELTA_MID,
						ATH_ANT_DIV_COMB_LNA1_DELTA_LOW,
						main_rssi_avg, alt_rssi_avg,
						antcomb->total_pkt_count))
				antcomb->second_ratio = true;
			else
				antcomb->second_ratio = false;
		} else {
			if ((((alt_ratio >= ATH_ANT_DIV_COMB_ALT_ANT_RATIO2) &&
			    (alt_rssi_avg > main_rssi_avg +
			    ATH_ANT_DIV_COMB_LNA1_DELTA_HI)) ||
			    (alt_rssi_avg > main_rssi_avg)) &&
			    (antcomb->total_pkt_count > 50))
				antcomb->second_ratio = true;
			else
				antcomb->second_ratio = false;
		}

		/* set alt to the conf with maximun ratio */
		if (antcomb->first_ratio && antcomb->second_ratio) {
			if (antcomb->rssi_second > antcomb->rssi_third) {
				/* first alt*/
				if ((antcomb->first_quick_scan_conf ==
				    ATH_ANT_DIV_COMB_LNA1) ||
				    (antcomb->first_quick_scan_conf ==
				    ATH_ANT_DIV_COMB_LNA2))
					/* Set alt LNA1 or LNA2*/
					if (div_ant_conf->main_lna_conf ==
					    ATH_ANT_DIV_COMB_LNA2)
						div_ant_conf->alt_lna_conf =
							ATH_ANT_DIV_COMB_LNA1;
					else
						div_ant_conf->alt_lna_conf =
							ATH_ANT_DIV_COMB_LNA2;
				else
					/* Set alt to A+B or A-B */
					div_ant_conf->alt_lna_conf =
						antcomb->first_quick_scan_conf;
			} else if ((antcomb->second_quick_scan_conf ==
				   ATH_ANT_DIV_COMB_LNA1) ||
				   (antcomb->second_quick_scan_conf ==
				   ATH_ANT_DIV_COMB_LNA2)) {
				/* Set alt LNA1 or LNA2 */
				if (div_ant_conf->main_lna_conf ==
				    ATH_ANT_DIV_COMB_LNA2)
					div_ant_conf->alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA1;
				else
					div_ant_conf->alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA2;
			} else {
				/* Set alt to A+B or A-B */
				div_ant_conf->alt_lna_conf =
					antcomb->second_quick_scan_conf;
			}
		} else if (antcomb->first_ratio) {
			/* first alt */
			if ((antcomb->first_quick_scan_conf ==
			    ATH_ANT_DIV_COMB_LNA1) ||
			    (antcomb->first_quick_scan_conf ==
			    ATH_ANT_DIV_COMB_LNA2))
					/* Set alt LNA1 or LNA2 */
				if (div_ant_conf->main_lna_conf ==
				    ATH_ANT_DIV_COMB_LNA2)
					div_ant_conf->alt_lna_conf =
							ATH_ANT_DIV_COMB_LNA1;
				else
					div_ant_conf->alt_lna_conf =
							ATH_ANT_DIV_COMB_LNA2;
			else
				/* Set alt to A+B or A-B */
				div_ant_conf->alt_lna_conf =
						antcomb->first_quick_scan_conf;
		} else if (antcomb->second_ratio) {
				/* second alt */
			if ((antcomb->second_quick_scan_conf ==
			    ATH_ANT_DIV_COMB_LNA1) ||
			    (antcomb->second_quick_scan_conf ==
			    ATH_ANT_DIV_COMB_LNA2))
				/* Set alt LNA1 or LNA2 */
				if (div_ant_conf->main_lna_conf ==
				    ATH_ANT_DIV_COMB_LNA2)
					div_ant_conf->alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA1;
				else
					div_ant_conf->alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA2;
			else
				/* Set alt to A+B or A-B */
				div_ant_conf->alt_lna_conf =
						antcomb->second_quick_scan_conf;
		} else {
			/* main is largest */
			if ((antcomb->main_conf == ATH_ANT_DIV_COMB_LNA1) ||
			    (antcomb->main_conf == ATH_ANT_DIV_COMB_LNA2))
				/* Set alt LNA1 or LNA2 */
				if (div_ant_conf->main_lna_conf ==
				    ATH_ANT_DIV_COMB_LNA2)
					div_ant_conf->alt_lna_conf =
							ATH_ANT_DIV_COMB_LNA1;
				else
					div_ant_conf->alt_lna_conf =
							ATH_ANT_DIV_COMB_LNA2;
			else
				/* Set alt to A+B or A-B */
				div_ant_conf->alt_lna_conf = antcomb->main_conf;
		}
		break;
	default:
		break;
	}
}

static void ath_ant_div_conf_fast_divbias(struct ath_hw_antcomb_conf *ant_conf)
{
	/* Adjust the fast_div_bias based on main and alt lna conf */
	switch ((ant_conf->main_lna_conf << 4) | ant_conf->alt_lna_conf) {
	case (0x01): /* A-B LNA2 */
		ant_conf->fast_div_bias = 0x3b;
		break;
	case (0x02): /* A-B LNA1 */
		ant_conf->fast_div_bias = 0x3d;
		break;
	case (0x03): /* A-B A+B */
		ant_conf->fast_div_bias = 0x1;
		break;
	case (0x10): /* LNA2 A-B */
		ant_conf->fast_div_bias = 0x7;
		break;
	case (0x12): /* LNA2 LNA1 */
		ant_conf->fast_div_bias = 0x2;
		break;
	case (0x13): /* LNA2 A+B */
		ant_conf->fast_div_bias = 0x7;
		break;
	case (0x20): /* LNA1 A-B */
		ant_conf->fast_div_bias = 0x6;
		break;
	case (0x21): /* LNA1 LNA2 */
		ant_conf->fast_div_bias = 0x0;
		break;
	case (0x23): /* LNA1 A+B */
		ant_conf->fast_div_bias = 0x6;
		break;
	case (0x30): /* A+B A-B */
		ant_conf->fast_div_bias = 0x1;
		break;
	case (0x31): /* A+B LNA2 */
		ant_conf->fast_div_bias = 0x3b;
		break;
	case (0x32): /* A+B LNA1 */
		ant_conf->fast_div_bias = 0x3d;
		break;
	default:
		break;
	}
}

/* Antenna diversity and combining */
static void ath_ant_comb_scan(struct ath_softc *sc, struct ath_rx_status *rs)
{
	struct ath_hw_antcomb_conf div_ant_conf;
	struct ath_ant_comb *antcomb = &sc->ant_comb;
	int alt_ratio = 0, alt_rssi_avg = 0, main_rssi_avg = 0, curr_alt_set;
	int curr_main_set, curr_bias;
	int main_rssi = rs->rs_rssi_ctl0;
	int alt_rssi = rs->rs_rssi_ctl1;
	int rx_ant_conf,  main_ant_conf;
	bool short_scan = false;

	rx_ant_conf = (rs->rs_rssi_ctl2 >> ATH_ANT_RX_CURRENT_SHIFT) &
		       ATH_ANT_RX_MASK;
	main_ant_conf = (rs->rs_rssi_ctl2 >> ATH_ANT_RX_MAIN_SHIFT) &
			 ATH_ANT_RX_MASK;

	/* Record packet only when alt_rssi is positive */
	if (alt_rssi > 0) {
		antcomb->total_pkt_count++;
		antcomb->main_total_rssi += main_rssi;
		antcomb->alt_total_rssi  += alt_rssi;
		if (main_ant_conf == rx_ant_conf)
			antcomb->main_recv_cnt++;
		else
			antcomb->alt_recv_cnt++;
	}

	/* Short scan check */
	if (antcomb->scan && antcomb->alt_good) {
		if (time_after(jiffies, antcomb->scan_start_time +
		    msecs_to_jiffies(ATH_ANT_DIV_COMB_SHORT_SCAN_INTR)))
			short_scan = true;
		else
			if (antcomb->total_pkt_count ==
			    ATH_ANT_DIV_COMB_SHORT_SCAN_PKTCOUNT) {
				alt_ratio = ((antcomb->alt_recv_cnt * 100) /
					    antcomb->total_pkt_count);
				if (alt_ratio < ATH_ANT_DIV_COMB_ALT_ANT_RATIO)
					short_scan = true;
			}
	}

	if (((antcomb->total_pkt_count < ATH_ANT_DIV_COMB_MAX_PKTCOUNT) ||
	    rs->rs_moreaggr) && !short_scan)
		return;

	if (antcomb->total_pkt_count) {
		alt_ratio = ((antcomb->alt_recv_cnt * 100) /
			     antcomb->total_pkt_count);
		main_rssi_avg = (antcomb->main_total_rssi /
				 antcomb->total_pkt_count);
		alt_rssi_avg = (antcomb->alt_total_rssi /
				 antcomb->total_pkt_count);
	}


	ath9k_hw_antdiv_comb_conf_get(sc->sc_ah, &div_ant_conf);
	curr_alt_set = div_ant_conf.alt_lna_conf;
	curr_main_set = div_ant_conf.main_lna_conf;
	curr_bias = div_ant_conf.fast_div_bias;

	antcomb->count++;

	if (antcomb->count == ATH_ANT_DIV_COMB_MAX_COUNT) {
		if (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO) {
			ath_lnaconf_alt_good_scan(antcomb, div_ant_conf,
						  main_rssi_avg);
			antcomb->alt_good = true;
		} else {
			antcomb->alt_good = false;
		}

		antcomb->count = 0;
		antcomb->scan = true;
		antcomb->scan_not_start = true;
	}

	if (!antcomb->scan) {
		if (alt_ratio > ATH_ANT_DIV_COMB_ALT_ANT_RATIO) {
			if (curr_alt_set == ATH_ANT_DIV_COMB_LNA2) {
				/* Switch main and alt LNA */
				div_ant_conf.main_lna_conf =
						ATH_ANT_DIV_COMB_LNA2;
				div_ant_conf.alt_lna_conf  =
						ATH_ANT_DIV_COMB_LNA1;
			} else if (curr_alt_set == ATH_ANT_DIV_COMB_LNA1) {
				div_ant_conf.main_lna_conf =
						ATH_ANT_DIV_COMB_LNA1;
				div_ant_conf.alt_lna_conf  =
						ATH_ANT_DIV_COMB_LNA2;
			}

			goto div_comb_done;
		} else if ((curr_alt_set != ATH_ANT_DIV_COMB_LNA1) &&
			   (curr_alt_set != ATH_ANT_DIV_COMB_LNA2)) {
			/* Set alt to another LNA */
			if (curr_main_set == ATH_ANT_DIV_COMB_LNA2)
				div_ant_conf.alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA1;
			else if (curr_main_set == ATH_ANT_DIV_COMB_LNA1)
				div_ant_conf.alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA2;

			goto div_comb_done;
		}

		if ((alt_rssi_avg < (main_rssi_avg +
		    ATH_ANT_DIV_COMB_LNA1_LNA2_DELTA)))
			goto div_comb_done;
	}

	if (!antcomb->scan_not_start) {
		switch (curr_alt_set) {
		case ATH_ANT_DIV_COMB_LNA2:
			antcomb->rssi_lna2 = alt_rssi_avg;
			antcomb->rssi_lna1 = main_rssi_avg;
			antcomb->scan = true;
			/* set to A+B */
			div_ant_conf.main_lna_conf =
				ATH_ANT_DIV_COMB_LNA1;
			div_ant_conf.alt_lna_conf  =
				ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
			break;
		case ATH_ANT_DIV_COMB_LNA1:
			antcomb->rssi_lna1 = alt_rssi_avg;
			antcomb->rssi_lna2 = main_rssi_avg;
			antcomb->scan = true;
			/* set to A+B */
			div_ant_conf.main_lna_conf = ATH_ANT_DIV_COMB_LNA2;
			div_ant_conf.alt_lna_conf  =
				ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
			break;
		case ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2:
			antcomb->rssi_add = alt_rssi_avg;
			antcomb->scan = true;
			/* set to A-B */
			div_ant_conf.alt_lna_conf =
				ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
			break;
		case ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2:
			antcomb->rssi_sub = alt_rssi_avg;
			antcomb->scan = false;
			if (antcomb->rssi_lna2 >
			    (antcomb->rssi_lna1 +
			    ATH_ANT_DIV_COMB_LNA1_LNA2_SWITCH_DELTA)) {
				/* use LNA2 as main LNA */
				if ((antcomb->rssi_add > antcomb->rssi_lna1) &&
				    (antcomb->rssi_add > antcomb->rssi_sub)) {
					/* set to A+B */
					div_ant_conf.main_lna_conf =
						ATH_ANT_DIV_COMB_LNA2;
					div_ant_conf.alt_lna_conf  =
						ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
				} else if (antcomb->rssi_sub >
					   antcomb->rssi_lna1) {
					/* set to A-B */
					div_ant_conf.main_lna_conf =
						ATH_ANT_DIV_COMB_LNA2;
					div_ant_conf.alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
				} else {
					/* set to LNA1 */
					div_ant_conf.main_lna_conf =
						ATH_ANT_DIV_COMB_LNA2;
					div_ant_conf.alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA1;
				}
			} else {
				/* use LNA1 as main LNA */
				if ((antcomb->rssi_add > antcomb->rssi_lna2) &&
				    (antcomb->rssi_add > antcomb->rssi_sub)) {
					/* set to A+B */
					div_ant_conf.main_lna_conf =
						ATH_ANT_DIV_COMB_LNA1;
					div_ant_conf.alt_lna_conf  =
						ATH_ANT_DIV_COMB_LNA1_PLUS_LNA2;
				} else if (antcomb->rssi_sub >
					   antcomb->rssi_lna1) {
					/* set to A-B */
					div_ant_conf.main_lna_conf =
						ATH_ANT_DIV_COMB_LNA1;
					div_ant_conf.alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA1_MINUS_LNA2;
				} else {
					/* set to LNA2 */
					div_ant_conf.main_lna_conf =
						ATH_ANT_DIV_COMB_LNA1;
					div_ant_conf.alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA2;
				}
			}
			break;
		default:
			break;
		}
	} else {
		if (!antcomb->alt_good) {
			antcomb->scan_not_start = false;
			/* Set alt to another LNA */
			if (curr_main_set == ATH_ANT_DIV_COMB_LNA2) {
				div_ant_conf.main_lna_conf =
						ATH_ANT_DIV_COMB_LNA2;
				div_ant_conf.alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA1;
			} else if (curr_main_set == ATH_ANT_DIV_COMB_LNA1) {
				div_ant_conf.main_lna_conf =
						ATH_ANT_DIV_COMB_LNA1;
				div_ant_conf.alt_lna_conf =
						ATH_ANT_DIV_COMB_LNA2;
			}
			goto div_comb_done;
		}
	}

	ath_select_ant_div_from_quick_scan(antcomb, &div_ant_conf,
					   main_rssi_avg, alt_rssi_avg,
					   alt_ratio);

	antcomb->quick_scan_cnt++;

div_comb_done:
	ath_ant_div_conf_fast_divbias(&div_ant_conf);

	ath9k_hw_antdiv_comb_conf_set(sc->sc_ah, &div_ant_conf);

	antcomb->scan_start_time = jiffies;
	antcomb->total_pkt_count = 0;
	antcomb->main_total_rssi = 0;
	antcomb->alt_total_rssi = 0;
	antcomb->main_recv_cnt = 0;
	antcomb->alt_recv_cnt = 0;
}

int ath_rx_tasklet(struct ath_softc *sc, int flush, bool hp)
{
	struct ath_buf *bf;
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
	spin_lock_bh(&sc->rx.rxbuflock);

	tsf = ath9k_hw_gettsf64(ah);
	tsf_lower = tsf & 0xffffffff;

	do {
		/* If handling rx interrupt and flush is in progress => exit */
		if ((sc->sc_flags & SC_OP_RXFLUSH) && (flush == 0))
			break;

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

		hdr = (struct ieee80211_hdr *) (skb->data + rx_status_len);
		rxs =  IEEE80211_SKB_RXCB(skb);

		hw = ath_get_virt_hw(sc, hdr);

		ath_debug_stat_rx(sc, &rs);

		/*
		 * If we're asked to flush receive queue, directly
		 * chain it back at the queue without processing it.
		 */
		if (flush)
			goto requeue;

		retval = ath9k_rx_skb_preprocess(common, hw, hdr, &rs,
						 rxs, &decrypt_error);
		if (retval)
			goto requeue;

		rxs->mactime = (tsf & ~0xffffffffULL) | rs.rs_tstamp;
		if (rs.rs_tstamp > tsf_lower &&
		    unlikely(rs.rs_tstamp - tsf_lower > 0x10000000))
			rxs->mactime -= 0x100000000ULL;

		if (rs.rs_tstamp < tsf_lower &&
		    unlikely(tsf_lower - rs.rs_tstamp > 0x10000000))
			rxs->mactime += 0x100000000ULL;

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
				 dma_type);

		skb_put(skb, rs.rs_datalen + ah->caps.rx_status_len);
		if (ah->caps.rx_status_len)
			skb_pull(skb, ah->caps.rx_status_len);

		ath9k_rx_skb_postprocess(common, skb, &rs,
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
			ath_rx_send_to_mac80211(hw, sc, skb, rxs);
			break;
		}

		/*
		 * change the default rx antenna if rx diversity chooses the
		 * other antenna 3 times in a row.
		 */
		if (sc->rx.defant != rs.rs_antenna) {
			if (++sc->rx.rxotherant >= 3)
				ath_setdefantenna(sc, rs.rs_antenna);
		} else {
			sc->rx.rxotherant = 0;
		}

		spin_lock_irqsave(&sc->sc_pm_lock, flags);
		if (unlikely(ath9k_check_auto_sleep(sc) ||
			     (sc->ps_flags & (PS_WAIT_FOR_BEACON |
					      PS_WAIT_FOR_CAB |
					      PS_WAIT_FOR_PSPOLL_DATA))))
			ath_rx_ps(sc, skb);
		spin_unlock_irqrestore(&sc->sc_pm_lock, flags);

		if (ah->caps.hw_caps & ATH9K_HW_CAP_ANT_DIV_COMB)
			ath_ant_comb_scan(sc, &rs);

		ath_rx_send_to_mac80211(hw, sc, skb, rxs);

requeue:
		if (edma) {
			list_add_tail(&bf->list, &sc->rx.rxbuf);
			ath_rx_edma_buf_link(sc, qtype);
		} else {
			list_move_tail(&bf->list, &sc->rx.rxbuf);
			ath_rx_buf_link(sc, bf);
		}
	} while (1);

	spin_unlock_bh(&sc->rx.rxbuflock);

	return 0;
}
