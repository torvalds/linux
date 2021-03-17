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

#define FUDGE 2

static void ath9k_reset_beacon_status(struct ath_softc *sc)
{
	sc->beacon.tx_processed = false;
	sc->beacon.tx_last = false;
}

/*
 *  This function will modify certain transmit queue properties depending on
 *  the operating mode of the station (AP or AdHoc).  Parameters are AIFS
 *  settings and channel width min/max
*/
static void ath9k_beaconq_config(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_tx_queue_info qi, qi_be;
	struct ath_txq *txq;

	ath9k_hw_get_txq_props(ah, sc->beacon.beaconq, &qi);

	if (sc->sc_ah->opmode == NL80211_IFTYPE_AP ||
	    sc->sc_ah->opmode == NL80211_IFTYPE_MESH_POINT) {
		/* Always burst out beacon and CAB traffic. */
		qi.tqi_aifs = 1;
		qi.tqi_cwmin = 0;
		qi.tqi_cwmax = 0;
	} else {
		/* Adhoc mode; important thing is to use 2x cwmin. */
		txq = sc->tx.txq_map[IEEE80211_AC_BE];
		ath9k_hw_get_txq_props(ah, txq->axq_qnum, &qi_be);
		qi.tqi_aifs = qi_be.tqi_aifs;
		if (ah->slottime == 20)
			qi.tqi_cwmin = 2*qi_be.tqi_cwmin;
		else
			qi.tqi_cwmin = 4*qi_be.tqi_cwmin;
		qi.tqi_cwmax = qi_be.tqi_cwmax;
	}

	if (!ath9k_hw_set_txq_props(ah, sc->beacon.beaconq, &qi)) {
		ath_err(common, "Unable to update h/w beacon queue parameters\n");
	} else {
		ath9k_hw_resettxqueue(ah, sc->beacon.beaconq);
	}
}

/*
 *  Associates the beacon frame buffer with a transmit descriptor.  Will set
 *  up rate codes, and channel flags. Beacons are always sent out at the
 *  lowest rate, and are not retried.
*/
static void ath9k_beacon_setup(struct ath_softc *sc, struct ieee80211_vif *vif,
			     struct ath_buf *bf, int rateidx)
{
	struct sk_buff *skb = bf->bf_mpdu;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_tx_info info;
	struct ieee80211_supported_band *sband;
	u8 chainmask = ah->txchainmask;
	u8 i, rate = 0;

	sband = &common->sbands[sc->cur_chandef.chan->band];
	rate = sband->bitrates[rateidx].hw_value;
	if (vif->bss_conf.use_short_preamble)
		rate |= sband->bitrates[rateidx].hw_value_short;

	memset(&info, 0, sizeof(info));
	info.pkt_len = skb->len + FCS_LEN;
	info.type = ATH9K_PKT_TYPE_BEACON;
	for (i = 0; i < 4; i++)
		info.txpower[i] = MAX_RATE_POWER;
	info.keyix = ATH9K_TXKEYIX_INVALID;
	info.keytype = ATH9K_KEY_TYPE_CLEAR;
	info.flags = ATH9K_TXDESC_NOACK | ATH9K_TXDESC_CLRDMASK;

	info.buf_addr[0] = bf->bf_buf_addr;
	info.buf_len[0] = roundup(skb->len, 4);

	info.is_first = true;
	info.is_last = true;

	info.qcu = sc->beacon.beaconq;

	info.rates[0].Tries = 1;
	info.rates[0].Rate = rate;
	info.rates[0].ChSel = ath_txchainmask_reduction(sc, chainmask, rate);

	ath9k_hw_set_txdesc(ah, bf->bf_desc, &info);
}

static struct ath_buf *ath9k_beacon_generate(struct ieee80211_hw *hw,
					     struct ieee80211_vif *vif)
{
	struct ath_softc *sc = hw->priv;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_buf *bf;
	struct ath_vif *avp = (void *)vif->drv_priv;
	struct sk_buff *skb;
	struct ath_txq *cabq = sc->beacon.cabq;
	struct ieee80211_tx_info *info;
	struct ieee80211_mgmt *mgmt_hdr;
	int cabq_depth;

	if (avp->av_bcbuf == NULL)
		return NULL;

	bf = avp->av_bcbuf;
	skb = bf->bf_mpdu;
	if (skb) {
		dma_unmap_single(sc->dev, bf->bf_buf_addr,
				 skb->len, DMA_TO_DEVICE);
		dev_kfree_skb_any(skb);
		bf->bf_buf_addr = 0;
		bf->bf_mpdu = NULL;
	}

	skb = ieee80211_beacon_get(hw, vif);
	if (skb == NULL)
		return NULL;

	bf->bf_mpdu = skb;

	mgmt_hdr = (struct ieee80211_mgmt *)skb->data;
	mgmt_hdr->u.beacon.timestamp = avp->tsf_adjust;

	info = IEEE80211_SKB_CB(skb);

	ath_assign_seq(common, skb);

	/* Always assign NOA attr when MCC enabled */
	if (ath9k_is_chanctx_enabled())
		ath9k_beacon_add_noa(sc, avp, skb);

	bf->bf_buf_addr = dma_map_single(sc->dev, skb->data,
					 skb->len, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(sc->dev, bf->bf_buf_addr))) {
		dev_kfree_skb_any(skb);
		bf->bf_mpdu = NULL;
		bf->bf_buf_addr = 0;
		ath_err(common, "dma_mapping_error on beaconing\n");
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
		if (sc->cur_chan->nvifs > 1) {
			ath_dbg(common, BEACON,
				"Flushing previous cabq traffic\n");
			ath_draintxq(sc, cabq);
		}
	}

	ath9k_beacon_setup(sc, vif, bf, info->control.rates[0].idx);

	if (skb)
		ath_tx_cabq(hw, vif, skb);

	return bf;
}

void ath9k_beacon_assign_slot(struct ath_softc *sc, struct ieee80211_vif *vif)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_vif *avp = (void *)vif->drv_priv;
	int slot;

	avp->av_bcbuf = list_first_entry(&sc->beacon.bbuf, struct ath_buf, list);
	list_del(&avp->av_bcbuf->list);

	for (slot = 0; slot < ATH_BCBUF; slot++) {
		if (sc->beacon.bslot[slot] == NULL) {
			avp->av_bslot = slot;
			break;
		}
	}

	sc->beacon.bslot[avp->av_bslot] = vif;

	ath_dbg(common, CONFIG, "Added interface at beacon slot: %d\n",
		avp->av_bslot);
}

void ath9k_beacon_remove_slot(struct ath_softc *sc, struct ieee80211_vif *vif)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_vif *avp = (void *)vif->drv_priv;
	struct ath_buf *bf = avp->av_bcbuf;

	ath_dbg(common, CONFIG, "Removing interface at beacon slot: %d\n",
		avp->av_bslot);

	tasklet_disable(&sc->bcon_tasklet);

	if (bf && bf->bf_mpdu) {
		struct sk_buff *skb = bf->bf_mpdu;
		dma_unmap_single(sc->dev, bf->bf_buf_addr,
				 skb->len, DMA_TO_DEVICE);
		dev_kfree_skb_any(skb);
		bf->bf_mpdu = NULL;
		bf->bf_buf_addr = 0;
	}

	avp->av_bcbuf = NULL;
	sc->beacon.bslot[avp->av_bslot] = NULL;
	list_add_tail(&bf->list, &sc->beacon.bbuf);

	tasklet_enable(&sc->bcon_tasklet);
}

void ath9k_beacon_ensure_primary_slot(struct ath_softc *sc)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ieee80211_vif *vif;
	struct ath_vif *avp;
	s64 tsfadjust;
	u32 offset;
	int first_slot = ATH_BCBUF;
	int slot;

	tasklet_disable(&sc->bcon_tasklet);

	/* Find first taken slot. */
	for (slot = 0; slot < ATH_BCBUF; slot++) {
		if (sc->beacon.bslot[slot]) {
			first_slot = slot;
			break;
		}
	}
	if (first_slot == 0)
		goto out;

	/* Re-enumarate all slots, moving them forward. */
	for (slot = 0; slot < ATH_BCBUF; slot++) {
		if (slot + first_slot < ATH_BCBUF) {
			vif = sc->beacon.bslot[slot + first_slot];
			sc->beacon.bslot[slot] = vif;

			if (vif) {
				avp = (void *)vif->drv_priv;
				avp->av_bslot = slot;
			}
		} else {
			sc->beacon.bslot[slot] = NULL;
		}
	}

	vif = sc->beacon.bslot[0];
	if (WARN_ON(!vif))
		goto out;

	/* Get the tsf_adjust value for the new first slot. */
	avp = (void *)vif->drv_priv;
	tsfadjust = le64_to_cpu(avp->tsf_adjust);

	ath_dbg(common, CONFIG,
		"Adjusting global TSF after beacon slot reassignment: %lld\n",
		(signed long long)tsfadjust);

	/* Modify TSF as required and update the HW. */
	avp->chanctx->tsf_val += tsfadjust;
	if (sc->cur_chan == avp->chanctx) {
		offset = ath9k_hw_get_tsf_offset(&avp->chanctx->tsf_ts, NULL);
		ath9k_hw_settsf64(sc->sc_ah, avp->chanctx->tsf_val + offset);
	}

	/* The slots tsf_adjust will be updated by ath9k_beacon_config later. */

out:
	tasklet_enable(&sc->bcon_tasklet);
}

static int ath9k_beacon_choose_slot(struct ath_softc *sc)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_beacon_config *cur_conf = &sc->cur_chan->beacon;
	u16 intval;
	u32 tsftu;
	u64 tsf;
	int slot;

	if (sc->sc_ah->opmode != NL80211_IFTYPE_AP &&
	    sc->sc_ah->opmode != NL80211_IFTYPE_MESH_POINT) {
		ath_dbg(common, BEACON, "slot 0, tsf: %llu\n",
			ath9k_hw_gettsf64(sc->sc_ah));
		return 0;
	}

	intval = cur_conf->beacon_interval ? : ATH_DEFAULT_BINTVAL;
	tsf = ath9k_hw_gettsf64(sc->sc_ah);
	tsf += TU_TO_USEC(sc->sc_ah->config.sw_beacon_response_time);
	tsftu = TSF_TO_TU((tsf * ATH_BCBUF) >>32, tsf * ATH_BCBUF);
	slot = (tsftu % (intval * ATH_BCBUF)) / intval;

	ath_dbg(common, BEACON, "slot: %d tsf: %llu tsftu: %u\n",
		slot, tsf, tsftu / ATH_BCBUF);

	return slot;
}

static void ath9k_set_tsfadjust(struct ath_softc *sc,
				struct ath_beacon_config *cur_conf)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	s64 tsfadjust;
	int slot;

	for (slot = 0; slot < ATH_BCBUF; slot++) {
		struct ath_vif *avp;

		if (!sc->beacon.bslot[slot])
			continue;

		avp = (void *)sc->beacon.bslot[slot]->drv_priv;

		/* tsf_adjust is added to the TSF value. We send out the
		 * beacon late, so need to adjust the TSF starting point to be
		 * later in time (i.e. the theoretical first beacon has a TSF
		 * of 0 after correction).
		 */
		tsfadjust = cur_conf->beacon_interval * avp->av_bslot;
		tsfadjust = -TU_TO_USEC(tsfadjust) / ATH_BCBUF;
		avp->tsf_adjust = cpu_to_le64(tsfadjust);

		ath_dbg(common, CONFIG, "tsfadjust is: %lld for bslot: %d\n",
			(signed long long)tsfadjust, avp->av_bslot);
	}
}

bool ath9k_csa_is_finished(struct ath_softc *sc, struct ieee80211_vif *vif)
{
	if (!vif || !vif->csa_active)
		return false;

	if (!ieee80211_beacon_cntdwn_is_complete(vif))
		return false;

	ieee80211_csa_finish(vif);
	return true;
}

static void ath9k_csa_update_vif(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct ath_softc *sc = data;
	ath9k_csa_is_finished(sc, vif);
}

void ath9k_csa_update(struct ath_softc *sc)
{
	ieee80211_iterate_active_interfaces_atomic(sc->hw,
						   IEEE80211_IFACE_ITER_NORMAL,
						   ath9k_csa_update_vif, sc);
}

void ath9k_beacon_tasklet(struct tasklet_struct *t)
{
	struct ath_softc *sc = from_tasklet(sc, t, bcon_tasklet);
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_buf *bf = NULL;
	struct ieee80211_vif *vif;
	bool edma = !!(ah->caps.hw_caps & ATH9K_HW_CAP_EDMA);
	int slot;

	if (test_bit(ATH_OP_HW_RESET, &common->op_flags)) {
		ath_dbg(common, RESET,
			"reset work is pending, skip beaconing now\n");
		return;
	}

	/*
	 * Check if the previous beacon has gone out.  If
	 * not don't try to post another, skip this period
	 * and wait for the next.  Missed beacons indicate
	 * a problem and should not occur.  If we miss too
	 * many consecutive beacons reset the device.
	 */
	if (ath9k_hw_numtxpending(ah, sc->beacon.beaconq) != 0) {
		sc->beacon.bmisscnt++;

		ath9k_hw_check_nav(ah);

		/*
		 * If the previous beacon has not been transmitted
		 * and a MAC/BB hang has been identified, return
		 * here because a chip reset would have been
		 * initiated.
		 */
		if (!ath_hw_check(sc))
			return;

		if (sc->beacon.bmisscnt < BSTUCK_THRESH * sc->nbcnvifs) {
			ath_dbg(common, BSTUCK,
				"missed %u consecutive beacons\n",
				sc->beacon.bmisscnt);
			ath9k_hw_stop_dma_queue(ah, sc->beacon.beaconq);
			if (sc->beacon.bmisscnt > 3)
				ath9k_hw_bstuck_nfcal(ah);
		} else if (sc->beacon.bmisscnt >= BSTUCK_THRESH) {
			ath_dbg(common, BSTUCK, "beacon is officially stuck\n");
			sc->beacon.bmisscnt = 0;
			ath9k_queue_reset(sc, RESET_TYPE_BEACON_STUCK);
		}

		return;
	}

	slot = ath9k_beacon_choose_slot(sc);
	vif = sc->beacon.bslot[slot];

	/* EDMA devices check that in the tx completion function. */
	if (!edma) {
		if (ath9k_is_chanctx_enabled()) {
			ath_chanctx_beacon_sent_ev(sc,
					  ATH_CHANCTX_EVENT_BEACON_SENT);
		}

		if (ath9k_csa_is_finished(sc, vif))
			return;
	}

	if (!vif || !vif->bss_conf.enable_beacon)
		return;

	if (ath9k_is_chanctx_enabled()) {
		ath_chanctx_event(sc, vif, ATH_CHANCTX_EVENT_BEACON_PREPARE);
	}

	bf = ath9k_beacon_generate(sc->hw, vif);

	if (sc->beacon.bmisscnt != 0) {
		ath_dbg(common, BSTUCK, "resume beacon xmit after %u misses\n",
			sc->beacon.bmisscnt);
		sc->beacon.bmisscnt = 0;
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
		sc->beacon.updateslot = COMMIT;
		sc->beacon.slotupdate = slot;
	} else if (sc->beacon.updateslot == COMMIT &&
		   sc->beacon.slotupdate == slot) {
		ah->slottime = sc->beacon.slottime;
		ath9k_hw_init_global_settings(ah);
		sc->beacon.updateslot = OK;
	}

	if (bf) {
		ath9k_reset_beacon_status(sc);

		ath_dbg(common, BEACON,
			"Transmitting beacon for slot: %d\n", slot);

		/* NB: cabq traffic should already be queued and primed */
		ath9k_hw_puttxbuf(ah, sc->beacon.beaconq, bf->bf_daddr);

		if (!edma)
			ath9k_hw_txstart(ah, sc->beacon.beaconq);
	}
}

/*
 * Both nexttbtt and intval have to be in usecs.
 */
static void ath9k_beacon_init(struct ath_softc *sc, u32 nexttbtt,
			      u32 intval)
{
	struct ath_hw *ah = sc->sc_ah;

	ath9k_hw_disable_interrupts(ah);
	ath9k_beaconq_config(sc);
	ath9k_hw_beaconinit(ah, nexttbtt, intval);
	ah->imask |= ATH9K_INT_SWBA;
	sc->beacon.bmisscnt = 0;
	ath9k_hw_set_interrupts(ah);
	ath9k_hw_enable_interrupts(ah);
}

static void ath9k_beacon_stop(struct ath_softc *sc)
{
	ath9k_hw_disable_interrupts(sc->sc_ah);
	sc->sc_ah->imask &= ~(ATH9K_INT_SWBA | ATH9K_INT_BMISS);
	sc->beacon.bmisscnt = 0;
	ath9k_hw_set_interrupts(sc->sc_ah);
	ath9k_hw_enable_interrupts(sc->sc_ah);
}

/*
 * For multi-bss ap support beacons are either staggered evenly over N slots or
 * burst together.  For the former arrange for the SWBA to be delivered for each
 * slot. Slots that are not occupied will generate nothing.
 */
static void ath9k_beacon_config_ap(struct ath_softc *sc,
				   struct ath_beacon_config *conf)
{
	struct ath_hw *ah = sc->sc_ah;

	ath9k_cmn_beacon_config_ap(ah, conf, ATH_BCBUF);
	ath9k_beacon_init(sc, conf->nexttbtt, conf->intval);
}

static void ath9k_beacon_config_sta(struct ath_hw *ah,
				    struct ath_beacon_config *conf)
{
	struct ath9k_beacon_state bs;

	if (ath9k_cmn_beacon_config_sta(ah, conf, &bs) == -EPERM)
		return;

	ath9k_hw_disable_interrupts(ah);
	ath9k_hw_set_sta_beacon_timers(ah, &bs);
	ah->imask |= ATH9K_INT_BMISS;

	ath9k_hw_set_interrupts(ah);
	ath9k_hw_enable_interrupts(ah);
}

static void ath9k_beacon_config_adhoc(struct ath_softc *sc,
				      struct ath_beacon_config *conf)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);

	ath9k_reset_beacon_status(sc);

	ath9k_cmn_beacon_config_adhoc(ah, conf);

	ath9k_beacon_init(sc, conf->nexttbtt, conf->intval);

	/*
	 * Set the global 'beacon has been configured' flag for the
	 * joiner case in IBSS mode.
	 */
	if (!conf->ibss_creator && conf->enable_beacon)
		set_bit(ATH_OP_BEACONS, &common->op_flags);
}

static void ath9k_cache_beacon_config(struct ath_softc *sc,
				      struct ath_chanctx *ctx,
				      struct ieee80211_bss_conf *bss_conf)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_beacon_config *cur_conf = &ctx->beacon;

	ath_dbg(common, BEACON,
		"Caching beacon data for BSS: %pM\n", bss_conf->bssid);

	cur_conf->beacon_interval = bss_conf->beacon_int;
	cur_conf->dtim_period = bss_conf->dtim_period;
	cur_conf->dtim_count = 1;
	cur_conf->ibss_creator = bss_conf->ibss_creator;

	/*
	 * It looks like mac80211 may end up using beacon interval of zero in
	 * some cases (at least for mesh point). Avoid getting into an
	 * infinite loop by using a bit safer value instead. To be safe,
	 * do sanity check on beacon interval for all operating modes.
	 */
	if (cur_conf->beacon_interval == 0)
		cur_conf->beacon_interval = 100;

	cur_conf->bmiss_timeout =
		ATH_DEFAULT_BMISS_LIMIT * cur_conf->beacon_interval;

	/*
	 * We don't parse dtim period from mac80211 during the driver
	 * initialization as it breaks association with hidden-ssid
	 * AP and it causes latency in roaming
	 */
	if (cur_conf->dtim_period == 0)
		cur_conf->dtim_period = 1;

	ath9k_set_tsfadjust(sc, cur_conf);
}

void ath9k_beacon_config(struct ath_softc *sc, struct ieee80211_vif *main_vif,
			 bool beacons)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_vif *avp;
	struct ath_chanctx *ctx;
	struct ath_beacon_config *cur_conf;
	unsigned long flags;
	bool enabled;
	bool skip_beacon = false;

	if (!beacons) {
		clear_bit(ATH_OP_BEACONS, &common->op_flags);
		ath9k_beacon_stop(sc);
		return;
	}

	if (WARN_ON(!main_vif))
		return;

	avp = (void *)main_vif->drv_priv;
	ctx = avp->chanctx;
	cur_conf = &ctx->beacon;
	enabled = cur_conf->enable_beacon;
	cur_conf->enable_beacon = beacons;

	if (sc->sc_ah->opmode == NL80211_IFTYPE_STATION) {
		ath9k_cache_beacon_config(sc, ctx, &main_vif->bss_conf);

		ath9k_set_beacon(sc);
		set_bit(ATH_OP_BEACONS, &common->op_flags);
		return;
	}

	/* Update the beacon configuration. */
	ath9k_cache_beacon_config(sc, ctx, &main_vif->bss_conf);

	/*
	 * Configure the HW beacon registers only when we have a valid
	 * beacon interval.
	 */
	if (cur_conf->beacon_interval) {
		/* Special case to sync the TSF when joining an existing IBSS.
		 * This is only done if no AP interface is active.
		 * Note that mac80211 always resets the TSF when creating a new
		 * IBSS interface.
		 */
		if (sc->sc_ah->opmode == NL80211_IFTYPE_ADHOC &&
		    !enabled && beacons && !main_vif->bss_conf.ibss_creator) {
			spin_lock_irqsave(&sc->sc_pm_lock, flags);
			sc->ps_flags |= PS_BEACON_SYNC | PS_WAIT_FOR_BEACON;
			spin_unlock_irqrestore(&sc->sc_pm_lock, flags);
			skip_beacon = true;
		}

		/*
		 * Do not set the ATH_OP_BEACONS flag for IBSS joiner mode
		 * here, it is done in ath9k_beacon_config_adhoc().
		 */
		if (beacons && !skip_beacon) {
			set_bit(ATH_OP_BEACONS, &common->op_flags);
			ath9k_set_beacon(sc);
		} else {
			clear_bit(ATH_OP_BEACONS, &common->op_flags);
			ath9k_beacon_stop(sc);
		}
	} else {
		clear_bit(ATH_OP_BEACONS, &common->op_flags);
		ath9k_beacon_stop(sc);
	}
}

void ath9k_set_beacon(struct ath_softc *sc)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_beacon_config *cur_conf = &sc->cur_chan->beacon;

	switch (sc->sc_ah->opmode) {
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
		ath9k_beacon_config_ap(sc, cur_conf);
		break;
	case NL80211_IFTYPE_ADHOC:
		ath9k_beacon_config_adhoc(sc, cur_conf);
		break;
	case NL80211_IFTYPE_STATION:
		ath9k_beacon_config_sta(sc->sc_ah, cur_conf);
		break;
	default:
		ath_dbg(common, CONFIG, "Unsupported beaconing mode\n");
		return;
	}
}
