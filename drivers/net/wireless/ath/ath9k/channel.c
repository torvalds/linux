/*
 * Copyright (c) 2014 Qualcomm Atheros, Inc.
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

/* Set/change channels.  If the channel is really being changed, it's done
 * by reseting the chip.  To accomplish this we must first cleanup any pending
 * DMA, then restart stuff.
 */
static int ath_set_channel(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_hw *hw = sc->hw;
	struct ath9k_channel *hchan;
	struct cfg80211_chan_def *chandef = &sc->cur_chan->chandef;
	struct ieee80211_channel *chan = chandef->chan;
	int pos = chan->hw_value;
	int old_pos = -1;
	int r;

	if (test_bit(ATH_OP_INVALID, &common->op_flags))
		return -EIO;

	if (ah->curchan)
		old_pos = ah->curchan - &ah->channels[0];

	ath_dbg(common, CONFIG, "Set channel: %d MHz width: %d\n",
		chan->center_freq, chandef->width);

	/* update survey stats for the old channel before switching */
	spin_lock_bh(&common->cc_lock);
	ath_update_survey_stats(sc);
	spin_unlock_bh(&common->cc_lock);

	ath9k_cmn_get_channel(hw, ah, chandef);

	/* If the operating channel changes, change the survey in-use flags
	 * along with it.
	 * Reset the survey data for the new channel, unless we're switching
	 * back to the operating channel from an off-channel operation.
	 */
	if (!sc->cur_chan->offchannel && sc->cur_survey != &sc->survey[pos]) {
		if (sc->cur_survey)
			sc->cur_survey->filled &= ~SURVEY_INFO_IN_USE;

		sc->cur_survey = &sc->survey[pos];

		memset(sc->cur_survey, 0, sizeof(struct survey_info));
		sc->cur_survey->filled |= SURVEY_INFO_IN_USE;
	} else if (!(sc->survey[pos].filled & SURVEY_INFO_IN_USE)) {
		memset(&sc->survey[pos], 0, sizeof(struct survey_info));
	}

	hchan = &sc->sc_ah->channels[pos];
	r = ath_reset_internal(sc, hchan);
	if (r)
		return r;

	/* The most recent snapshot of channel->noisefloor for the old
	 * channel is only available after the hardware reset. Copy it to
	 * the survey stats now.
	 */
	if (old_pos >= 0)
		ath_update_survey_nf(sc, old_pos);

	/* Enable radar pulse detection if on a DFS channel. Spectral
	 * scanning and radar detection can not be used concurrently.
	 */
	if (hw->conf.radar_enabled) {
		u32 rxfilter;

		/* set HW specific DFS configuration */
		ath9k_hw_set_radar_params(ah);
		rxfilter = ath9k_hw_getrxfilter(ah);
		rxfilter |= ATH9K_RX_FILTER_PHYRADAR |
				ATH9K_RX_FILTER_PHYERR;
		ath9k_hw_setrxfilter(ah, rxfilter);
		ath_dbg(common, DFS, "DFS enabled at freq %d\n",
			chan->center_freq);
	} else {
		/* perform spectral scan if requested. */
		if (test_bit(ATH_OP_SCANNING, &common->op_flags) &&
			sc->spectral_mode == SPECTRAL_CHANSCAN)
			ath9k_spectral_scan_trigger(hw);
	}

	return 0;
}

static bool
ath_chanctx_send_vif_ps_frame(struct ath_softc *sc, struct ath_vif *avp,
			      bool powersave)
{
	struct ieee80211_vif *vif = avp->vif;
	struct ieee80211_sta *sta = NULL;
	struct ieee80211_hdr_3addr *nullfunc;
	struct ath_tx_control txctl;
	struct sk_buff *skb;
	int band = sc->cur_chan->chandef.chan->band;

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		if (!vif->bss_conf.assoc)
			return false;

		skb = ieee80211_nullfunc_get(sc->hw, vif);
		if (!skb)
			return false;

		nullfunc = (struct ieee80211_hdr_3addr *) skb->data;
		if (powersave)
			nullfunc->frame_control |=
				cpu_to_le16(IEEE80211_FCTL_PM);

		skb_set_queue_mapping(skb, IEEE80211_AC_VO);
		if (!ieee80211_tx_prepare_skb(sc->hw, vif, skb, band, &sta)) {
			dev_kfree_skb_any(skb);
			return false;
		}
		break;
	default:
		return false;
	}

	memset(&txctl, 0, sizeof(txctl));
	txctl.txq = sc->tx.txq_map[IEEE80211_AC_VO];
	txctl.sta = sta;
	txctl.force_channel = true;
	if (ath_tx_start(sc->hw, skb, &txctl)) {
		ieee80211_free_txskb(sc->hw, skb);
		return false;
	}

	return true;
}

void ath_chanctx_check_active(struct ath_softc *sc, struct ath_chanctx *ctx)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_vif *avp;
	bool active = false;
	u8 n_active = 0;

	if (!ctx)
		return;

	list_for_each_entry(avp, &ctx->vifs, list) {
		struct ieee80211_vif *vif = avp->vif;

		switch (vif->type) {
		case NL80211_IFTYPE_P2P_CLIENT:
		case NL80211_IFTYPE_STATION:
			if (vif->bss_conf.assoc)
				active = true;
			break;
		default:
			active = true;
			break;
		}
	}
	ctx->active = active;

	ath_for_each_chanctx(sc, ctx) {
		if (!ctx->assigned || list_empty(&ctx->vifs))
			continue;
		n_active++;
	}

	if (n_active <= 1) {
		clear_bit(ATH_OP_MULTI_CHANNEL, &common->op_flags);
		return;
	}
	if (test_and_set_bit(ATH_OP_MULTI_CHANNEL, &common->op_flags))
		return;
	ath_chanctx_event(sc, NULL, ATH_CHANCTX_EVENT_ENABLE_MULTICHANNEL);
}

static bool
ath_chanctx_send_ps_frame(struct ath_softc *sc, bool powersave)
{
	struct ath_vif *avp;
	bool sent = false;

	rcu_read_lock();
	list_for_each_entry(avp, &sc->cur_chan->vifs, list) {
		if (ath_chanctx_send_vif_ps_frame(sc, avp, powersave))
			sent = true;
	}
	rcu_read_unlock();

	return sent;
}

static bool ath_chanctx_defer_switch(struct ath_softc *sc)
{
	if (sc->cur_chan == &sc->offchannel.chan)
		return false;

	switch (sc->sched.state) {
	case ATH_CHANCTX_STATE_SWITCH:
		return false;
	case ATH_CHANCTX_STATE_IDLE:
		if (!sc->cur_chan->switch_after_beacon)
			return false;

		sc->sched.state = ATH_CHANCTX_STATE_WAIT_FOR_BEACON;
		break;
	default:
		break;
	}

	return true;
}

static void ath_chanctx_set_next(struct ath_softc *sc, bool force)
{
	struct timespec ts;
	bool measure_time = false;
	bool send_ps = false;

	spin_lock_bh(&sc->chan_lock);
	if (!sc->next_chan) {
		spin_unlock_bh(&sc->chan_lock);
		return;
	}

	if (!force && ath_chanctx_defer_switch(sc)) {
		spin_unlock_bh(&sc->chan_lock);
		return;
	}

	if (sc->cur_chan != sc->next_chan) {
		sc->cur_chan->stopped = true;
		spin_unlock_bh(&sc->chan_lock);

		if (sc->next_chan == &sc->offchannel.chan) {
			getrawmonotonic(&ts);
			measure_time = true;
		}
		__ath9k_flush(sc->hw, ~0, true);

		if (ath_chanctx_send_ps_frame(sc, true))
			__ath9k_flush(sc->hw, BIT(IEEE80211_AC_VO), false);

		send_ps = true;
		spin_lock_bh(&sc->chan_lock);

		if (sc->cur_chan != &sc->offchannel.chan) {
			getrawmonotonic(&sc->cur_chan->tsf_ts);
			sc->cur_chan->tsf_val = ath9k_hw_gettsf64(sc->sc_ah);
		}
	}
	sc->cur_chan = sc->next_chan;
	sc->cur_chan->stopped = false;
	sc->next_chan = NULL;
	sc->sched.offchannel_duration = 0;
	if (sc->sched.state != ATH_CHANCTX_STATE_FORCE_ACTIVE)
		sc->sched.state = ATH_CHANCTX_STATE_IDLE;

	spin_unlock_bh(&sc->chan_lock);

	if (sc->sc_ah->chip_fullsleep ||
	    memcmp(&sc->cur_chandef, &sc->cur_chan->chandef,
		   sizeof(sc->cur_chandef))) {
		ath_set_channel(sc);
		if (measure_time)
			sc->sched.channel_switch_time =
				ath9k_hw_get_tsf_offset(&ts, NULL);
	}
	if (send_ps)
		ath_chanctx_send_ps_frame(sc, false);

	ath_offchannel_channel_change(sc);
	ath_chanctx_event(sc, NULL, ATH_CHANCTX_EVENT_SWITCH);
}

void ath_chanctx_work(struct work_struct *work)
{
	struct ath_softc *sc = container_of(work, struct ath_softc,
					    chanctx_work);
	mutex_lock(&sc->mutex);
	ath_chanctx_set_next(sc, false);
	mutex_unlock(&sc->mutex);
}

void ath_chanctx_init(struct ath_softc *sc)
{
	struct ath_chanctx *ctx;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *chan;
	int i, j;

	sband = &common->sbands[IEEE80211_BAND_2GHZ];
	if (!sband->n_channels)
		sband = &common->sbands[IEEE80211_BAND_5GHZ];

	chan = &sband->channels[0];
	for (i = 0; i < ATH9K_NUM_CHANCTX; i++) {
		ctx = &sc->chanctx[i];
		cfg80211_chandef_create(&ctx->chandef, chan, NL80211_CHAN_HT20);
		INIT_LIST_HEAD(&ctx->vifs);
		ctx->txpower = ATH_TXPOWER_MAX;
		for (j = 0; j < ARRAY_SIZE(ctx->acq); j++)
			INIT_LIST_HEAD(&ctx->acq[j]);
	}
	ctx = &sc->offchannel.chan;
	cfg80211_chandef_create(&ctx->chandef, chan, NL80211_CHAN_HT20);
	INIT_LIST_HEAD(&ctx->vifs);
	ctx->txpower = ATH_TXPOWER_MAX;
	for (j = 0; j < ARRAY_SIZE(ctx->acq); j++)
		INIT_LIST_HEAD(&ctx->acq[j]);
	sc->offchannel.chan.offchannel = true;

}

void ath9k_chanctx_force_active(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct ath_softc *sc = hw->priv;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_vif *avp = (struct ath_vif *) vif->drv_priv;
	bool changed = false;

	if (!test_bit(ATH_OP_MULTI_CHANNEL, &common->op_flags))
		return;

	if (!avp->chanctx)
		return;

	mutex_lock(&sc->mutex);

	spin_lock_bh(&sc->chan_lock);
	if (sc->next_chan || (sc->cur_chan != avp->chanctx)) {
		sc->next_chan = avp->chanctx;
		changed = true;
	}
	sc->sched.state = ATH_CHANCTX_STATE_FORCE_ACTIVE;
	spin_unlock_bh(&sc->chan_lock);

	if (changed)
		ath_chanctx_set_next(sc, true);

	mutex_unlock(&sc->mutex);
}

void ath_chanctx_switch(struct ath_softc *sc, struct ath_chanctx *ctx,
			struct cfg80211_chan_def *chandef)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	spin_lock_bh(&sc->chan_lock);

	if (test_bit(ATH_OP_MULTI_CHANNEL, &common->op_flags) &&
	    (sc->cur_chan != ctx) && (ctx == &sc->offchannel.chan)) {
		sc->sched.offchannel_pending = true;
		spin_unlock_bh(&sc->chan_lock);
		return;
	}

	sc->next_chan = ctx;
	if (chandef)
		ctx->chandef = *chandef;

	if (sc->next_chan == &sc->offchannel.chan) {
		sc->sched.offchannel_duration =
			TU_TO_USEC(sc->offchannel.duration) +
			sc->sched.channel_switch_time;
	}
	spin_unlock_bh(&sc->chan_lock);
	ieee80211_queue_work(sc->hw, &sc->chanctx_work);
}

void ath_chanctx_set_channel(struct ath_softc *sc, struct ath_chanctx *ctx,
			     struct cfg80211_chan_def *chandef)
{
	bool cur_chan;

	spin_lock_bh(&sc->chan_lock);
	if (chandef)
		memcpy(&ctx->chandef, chandef, sizeof(*chandef));
	cur_chan = sc->cur_chan == ctx;
	spin_unlock_bh(&sc->chan_lock);

	if (!cur_chan)
		return;

	ath_set_channel(sc);
}

struct ath_chanctx *ath_chanctx_get_oper_chan(struct ath_softc *sc, bool active)
{
	struct ath_chanctx *ctx;

	ath_for_each_chanctx(sc, ctx) {
		if (!ctx->assigned || list_empty(&ctx->vifs))
			continue;
		if (active && !ctx->active)
			continue;

		if (ctx->switch_after_beacon)
			return ctx;
	}

	return &sc->chanctx[0];
}

void ath_chanctx_offchan_switch(struct ath_softc *sc,
				struct ieee80211_channel *chan)
{
	struct cfg80211_chan_def chandef;

	cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_NO_HT);

	ath_chanctx_switch(sc, &sc->offchannel.chan, &chandef);
}

static struct ath_chanctx *
ath_chanctx_get_next(struct ath_softc *sc, struct ath_chanctx *ctx)
{
	int idx = ctx - &sc->chanctx[0];

	return &sc->chanctx[!idx];
}

static void ath_chanctx_adjust_tbtt_delta(struct ath_softc *sc)
{
	struct ath_chanctx *prev, *cur;
	struct timespec ts;
	u32 cur_tsf, prev_tsf, beacon_int;
	s32 offset;

	beacon_int = TU_TO_USEC(sc->cur_chan->beacon.beacon_interval);

	cur = sc->cur_chan;
	prev = ath_chanctx_get_next(sc, cur);

	getrawmonotonic(&ts);
	cur_tsf = (u32) cur->tsf_val +
		  ath9k_hw_get_tsf_offset(&cur->tsf_ts, &ts);

	prev_tsf = prev->last_beacon - (u32) prev->tsf_val + cur_tsf;
	prev_tsf -= ath9k_hw_get_tsf_offset(&prev->tsf_ts, &ts);

	/* Adjust the TSF time of the AP chanctx to keep its beacons
	 * at half beacon interval offset relative to the STA chanctx.
	 */
	offset = cur_tsf - prev_tsf;

	/* Ignore stale data or spurious timestamps */
	if (offset < 0 || offset > 3 * beacon_int)
		return;

	offset = beacon_int / 2 - (offset % beacon_int);
	prev->tsf_val += offset;
}

void ath_chanctx_timer(unsigned long data)
{
	struct ath_softc *sc = (struct ath_softc *) data;

	ath_chanctx_event(sc, NULL, ATH_CHANCTX_EVENT_TSF_TIMER);
}

/* Configure the TSF based hardware timer for a channel switch.
 * Also set up backup software timer, in case the gen timer fails.
 * This could be caused by a hardware reset.
 */
static void ath_chanctx_setup_timer(struct ath_softc *sc, u32 tsf_time)
{
	struct ath_hw *ah = sc->sc_ah;

	ath9k_hw_gen_timer_start(ah, sc->p2p_ps_timer, tsf_time, 1000000);
	tsf_time -= ath9k_hw_gettsf32(ah);
	tsf_time = msecs_to_jiffies(tsf_time / 1000) + 1;
	mod_timer(&sc->sched.timer, tsf_time);
}

void ath_chanctx_event(struct ath_softc *sc, struct ieee80211_vif *vif,
		       enum ath_chanctx_event ev)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_beacon_config *cur_conf;
	struct ath_vif *avp = NULL;
	struct ath_chanctx *ctx;
	u32 tsf_time;
	u32 beacon_int;
	bool noa_changed = false;

	if (vif)
		avp = (struct ath_vif *) vif->drv_priv;

	spin_lock_bh(&sc->chan_lock);

	switch (ev) {
	case ATH_CHANCTX_EVENT_BEACON_PREPARE:
		if (avp->offchannel_duration)
			avp->offchannel_duration = 0;

		if (avp->chanctx != sc->cur_chan)
			break;

		if (sc->sched.offchannel_pending) {
			sc->sched.offchannel_pending = false;
			sc->next_chan = &sc->offchannel.chan;
			sc->sched.state = ATH_CHANCTX_STATE_WAIT_FOR_BEACON;
		}

		ctx = ath_chanctx_get_next(sc, sc->cur_chan);
		if (ctx->active && sc->sched.state == ATH_CHANCTX_STATE_IDLE) {
			sc->next_chan = ctx;
			sc->sched.state = ATH_CHANCTX_STATE_WAIT_FOR_BEACON;
		}

		/* if the timer missed its window, use the next interval */
		if (sc->sched.state == ATH_CHANCTX_STATE_WAIT_FOR_TIMER)
			sc->sched.state = ATH_CHANCTX_STATE_WAIT_FOR_BEACON;

		if (sc->sched.state != ATH_CHANCTX_STATE_WAIT_FOR_BEACON)
			break;

		sc->sched.beacon_pending = true;
		sc->sched.next_tbtt = REG_READ(ah, AR_NEXT_TBTT_TIMER);

		cur_conf = &sc->cur_chan->beacon;
		beacon_int = TU_TO_USEC(cur_conf->beacon_interval);

		/* defer channel switch by a quarter beacon interval */
		tsf_time = sc->sched.next_tbtt + beacon_int / 4;
		sc->sched.switch_start_time = tsf_time;
		sc->cur_chan->last_beacon = sc->sched.next_tbtt;

		/* Prevent wrap-around issues */
		if (avp->periodic_noa_duration &&
		    tsf_time - avp->periodic_noa_start > BIT(30))
			avp->periodic_noa_duration = 0;

		if (ctx->active && !avp->periodic_noa_duration) {
			avp->periodic_noa_start = tsf_time;
			avp->periodic_noa_duration =
				TU_TO_USEC(cur_conf->beacon_interval) / 2 -
				sc->sched.channel_switch_time;
			noa_changed = true;
		} else if (!ctx->active && avp->periodic_noa_duration) {
			avp->periodic_noa_duration = 0;
			noa_changed = true;
		}

		/* If at least two consecutive beacons were missed on the STA
		 * chanctx, stay on the STA channel for one extra beacon period,
		 * to resync the timer properly.
		 */
		if (ctx->active && sc->sched.beacon_miss >= 2)
			sc->sched.offchannel_duration = 3 * beacon_int / 2;

		if (sc->sched.offchannel_duration) {
			noa_changed = true;
			avp->offchannel_start = tsf_time;
			avp->offchannel_duration =
				sc->sched.offchannel_duration;
		}

		if (noa_changed)
			avp->noa_index++;
		break;
	case ATH_CHANCTX_EVENT_BEACON_SENT:
		if (!sc->sched.beacon_pending)
			break;

		sc->sched.beacon_pending = false;
		if (sc->sched.state != ATH_CHANCTX_STATE_WAIT_FOR_BEACON)
			break;

		sc->sched.state = ATH_CHANCTX_STATE_WAIT_FOR_TIMER;
		ath_chanctx_setup_timer(sc, sc->sched.switch_start_time);
		break;
	case ATH_CHANCTX_EVENT_TSF_TIMER:
		if (sc->sched.state != ATH_CHANCTX_STATE_WAIT_FOR_TIMER)
			break;

		if (!sc->cur_chan->switch_after_beacon &&
		    sc->sched.beacon_pending)
			sc->sched.beacon_miss++;

		sc->sched.state = ATH_CHANCTX_STATE_SWITCH;
		ieee80211_queue_work(sc->hw, &sc->chanctx_work);
		break;
	case ATH_CHANCTX_EVENT_BEACON_RECEIVED:
		if (!test_bit(ATH_OP_MULTI_CHANNEL, &common->op_flags) ||
		    sc->cur_chan == &sc->offchannel.chan)
			break;

		ath_chanctx_adjust_tbtt_delta(sc);
		sc->sched.beacon_pending = false;
		sc->sched.beacon_miss = 0;

		/* TSF time might have been updated by the incoming beacon,
		 * need update the channel switch timer to reflect the change.
		 */
		tsf_time = sc->sched.switch_start_time;
		tsf_time -= (u32) sc->cur_chan->tsf_val +
			ath9k_hw_get_tsf_offset(&sc->cur_chan->tsf_ts, NULL);
		tsf_time += ath9k_hw_gettsf32(ah);


		ath_chanctx_setup_timer(sc, tsf_time);
		break;
	case ATH_CHANCTX_EVENT_ASSOC:
		if (sc->sched.state != ATH_CHANCTX_STATE_FORCE_ACTIVE ||
		    avp->chanctx != sc->cur_chan)
			break;

		sc->sched.state = ATH_CHANCTX_STATE_IDLE;
		/* fall through */
	case ATH_CHANCTX_EVENT_SWITCH:
		if (!test_bit(ATH_OP_MULTI_CHANNEL, &common->op_flags) ||
		    sc->sched.state == ATH_CHANCTX_STATE_FORCE_ACTIVE ||
		    sc->cur_chan->switch_after_beacon ||
		    sc->cur_chan == &sc->offchannel.chan)
			break;

		/* If this is a station chanctx, stay active for a half
		 * beacon period (minus channel switch time)
		 */
		sc->next_chan = ath_chanctx_get_next(sc, sc->cur_chan);
		cur_conf = &sc->cur_chan->beacon;

		sc->sched.state = ATH_CHANCTX_STATE_WAIT_FOR_TIMER;

		tsf_time = TU_TO_USEC(cur_conf->beacon_interval) / 2;
		if (sc->sched.beacon_miss >= 2) {
			sc->sched.beacon_miss = 0;
			tsf_time *= 3;
		}

		tsf_time -= sc->sched.channel_switch_time;
		tsf_time += ath9k_hw_gettsf32(sc->sc_ah);
		sc->sched.switch_start_time = tsf_time;

		ath_chanctx_setup_timer(sc, tsf_time);
		sc->sched.beacon_pending = true;
		break;
	case ATH_CHANCTX_EVENT_ENABLE_MULTICHANNEL:
		if (sc->cur_chan == &sc->offchannel.chan ||
		    sc->cur_chan->switch_after_beacon)
			break;

		sc->next_chan = ath_chanctx_get_next(sc, sc->cur_chan);
		ieee80211_queue_work(sc->hw, &sc->chanctx_work);
		break;
	case ATH_CHANCTX_EVENT_UNASSIGN:
		if (sc->cur_chan->assigned) {
			if (sc->next_chan && !sc->next_chan->assigned &&
			    sc->next_chan != &sc->offchannel.chan)
				sc->sched.state = ATH_CHANCTX_STATE_IDLE;
			break;
		}

		ctx = ath_chanctx_get_next(sc, sc->cur_chan);
		sc->sched.state = ATH_CHANCTX_STATE_IDLE;
		if (!ctx->assigned)
			break;

		sc->next_chan = ctx;
		ieee80211_queue_work(sc->hw, &sc->chanctx_work);
		break;
	}

	spin_unlock_bh(&sc->chan_lock);
}
