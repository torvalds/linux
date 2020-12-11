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
	unsigned long flags;
	int old_pos = -1;
	int r;

	if (test_bit(ATH_OP_INVALID, &common->op_flags))
		return -EIO;

	if (ah->curchan)
		old_pos = ah->curchan - &ah->channels[0];

	ath_dbg(common, CONFIG, "Set channel: %d MHz width: %d\n",
		chan->center_freq, chandef->width);

	/* update survey stats for the old channel before switching */
	spin_lock_irqsave(&common->cc_lock, flags);
	ath_update_survey_stats(sc);
	spin_unlock_irqrestore(&common->cc_lock, flags);

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
	r = ath_reset(sc, hchan);
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

		rxfilter = ath9k_hw_getrxfilter(ah);
		rxfilter |= ATH9K_RX_FILTER_PHYRADAR |
				ATH9K_RX_FILTER_PHYERR;
		ath9k_hw_setrxfilter(ah, rxfilter);
		ath_dbg(common, DFS, "DFS enabled at freq %d\n",
			chan->center_freq);
	} else {
		/* perform spectral scan if requested. */
		if (test_bit(ATH_OP_SCANNING, &common->op_flags) &&
			sc->spec_priv.spectral_mode == SPECTRAL_CHANSCAN)
			ath9k_cmn_spectral_scan_trigger(common, &sc->spec_priv);
	}

	return 0;
}

void ath_chanctx_init(struct ath_softc *sc)
{
	struct ath_chanctx *ctx;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *chan;
	int i, j;

	sband = &common->sbands[NL80211_BAND_2GHZ];
	if (!sband->n_channels)
		sband = &common->sbands[NL80211_BAND_5GHZ];

	chan = &sband->channels[0];
	for (i = 0; i < ATH9K_NUM_CHANCTX; i++) {
		ctx = &sc->chanctx[i];
		cfg80211_chandef_create(&ctx->chandef, chan, NL80211_CHAN_HT20);
		INIT_LIST_HEAD(&ctx->vifs);
		ctx->txpower = ATH_TXPOWER_MAX;
		ctx->flush_timeout = HZ / 5; /* 200ms */
		for (j = 0; j < ARRAY_SIZE(ctx->acq); j++) {
			INIT_LIST_HEAD(&ctx->acq[j].acq_new);
			INIT_LIST_HEAD(&ctx->acq[j].acq_old);
			spin_lock_init(&ctx->acq[j].lock);
		}
	}
}

void ath_chanctx_set_channel(struct ath_softc *sc, struct ath_chanctx *ctx,
			     struct cfg80211_chan_def *chandef)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	bool cur_chan;

	spin_lock_bh(&sc->chan_lock);
	if (chandef)
		memcpy(&ctx->chandef, chandef, sizeof(*chandef));
	cur_chan = sc->cur_chan == ctx;
	spin_unlock_bh(&sc->chan_lock);

	if (!cur_chan) {
		ath_dbg(common, CHAN_CTX,
			"Current context differs from the new context\n");
		return;
	}

	ath_set_channel(sc);
}

#ifdef CONFIG_ATH9K_CHANNEL_CONTEXT

/*************/
/* Utilities */
/*************/

struct ath_chanctx* ath_is_go_chanctx_present(struct ath_softc *sc)
{
	struct ath_chanctx *ctx;
	struct ath_vif *avp;
	struct ieee80211_vif *vif;

	spin_lock_bh(&sc->chan_lock);

	ath_for_each_chanctx(sc, ctx) {
		if (!ctx->active)
			continue;

		list_for_each_entry(avp, &ctx->vifs, list) {
			vif = avp->vif;

			if (ieee80211_vif_type_p2p(vif) == NL80211_IFTYPE_P2P_GO) {
				spin_unlock_bh(&sc->chan_lock);
				return ctx;
			}
		}
	}

	spin_unlock_bh(&sc->chan_lock);
	return NULL;
}

/**********************************************************/
/* Functions to handle the channel context state machine. */
/**********************************************************/

static const char *offchannel_state_string(enum ath_offchannel_state state)
{
	switch (state) {
		case_rtn_string(ATH_OFFCHANNEL_IDLE);
		case_rtn_string(ATH_OFFCHANNEL_PROBE_SEND);
		case_rtn_string(ATH_OFFCHANNEL_PROBE_WAIT);
		case_rtn_string(ATH_OFFCHANNEL_SUSPEND);
		case_rtn_string(ATH_OFFCHANNEL_ROC_START);
		case_rtn_string(ATH_OFFCHANNEL_ROC_WAIT);
		case_rtn_string(ATH_OFFCHANNEL_ROC_DONE);
	default:
		return "unknown";
	}
}

static const char *chanctx_event_string(enum ath_chanctx_event ev)
{
	switch (ev) {
		case_rtn_string(ATH_CHANCTX_EVENT_BEACON_PREPARE);
		case_rtn_string(ATH_CHANCTX_EVENT_BEACON_SENT);
		case_rtn_string(ATH_CHANCTX_EVENT_TSF_TIMER);
		case_rtn_string(ATH_CHANCTX_EVENT_BEACON_RECEIVED);
		case_rtn_string(ATH_CHANCTX_EVENT_AUTHORIZED);
		case_rtn_string(ATH_CHANCTX_EVENT_SWITCH);
		case_rtn_string(ATH_CHANCTX_EVENT_ASSIGN);
		case_rtn_string(ATH_CHANCTX_EVENT_UNASSIGN);
		case_rtn_string(ATH_CHANCTX_EVENT_CHANGE);
		case_rtn_string(ATH_CHANCTX_EVENT_ENABLE_MULTICHANNEL);
	default:
		return "unknown";
	}
}

static const char *chanctx_state_string(enum ath_chanctx_state state)
{
	switch (state) {
		case_rtn_string(ATH_CHANCTX_STATE_IDLE);
		case_rtn_string(ATH_CHANCTX_STATE_WAIT_FOR_BEACON);
		case_rtn_string(ATH_CHANCTX_STATE_WAIT_FOR_TIMER);
		case_rtn_string(ATH_CHANCTX_STATE_SWITCH);
		case_rtn_string(ATH_CHANCTX_STATE_FORCE_ACTIVE);
	default:
		return "unknown";
	}
}

static u32 chanctx_event_delta(struct ath_softc *sc)
{
	u64 ms;
	struct timespec64 ts, *old;

	ktime_get_raw_ts64(&ts);
	old = &sc->last_event_time;
	ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
	ms -= old->tv_sec * 1000 + old->tv_nsec / 1000000;
	sc->last_event_time = ts;

	return (u32)ms;
}

void ath_chanctx_check_active(struct ath_softc *sc, struct ath_chanctx *ctx)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_chanctx *ictx;
	struct ath_vif *avp;
	bool active = false;
	u8 n_active = 0;

	if (!ctx)
		return;

	if (ctx == &sc->offchannel.chan) {
		spin_lock_bh(&sc->chan_lock);

		if (likely(sc->sched.channel_switch_time))
			ctx->flush_timeout =
				usecs_to_jiffies(sc->sched.channel_switch_time);
		else
			ctx->flush_timeout =
				msecs_to_jiffies(10);

		spin_unlock_bh(&sc->chan_lock);

		/*
		 * There is no need to iterate over the
		 * active/assigned channel contexts if
		 * the current context is offchannel.
		 */
		return;
	}

	ictx = ctx;

	list_for_each_entry(avp, &ctx->vifs, list) {
		struct ieee80211_vif *vif = avp->vif;

		switch (vif->type) {
		case NL80211_IFTYPE_P2P_CLIENT:
		case NL80211_IFTYPE_STATION:
			if (avp->assoc)
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

	spin_lock_bh(&sc->chan_lock);

	if (n_active <= 1) {
		ictx->flush_timeout = HZ / 5;
		clear_bit(ATH_OP_MULTI_CHANNEL, &common->op_flags);
		spin_unlock_bh(&sc->chan_lock);
		return;
	}

	ictx->flush_timeout = usecs_to_jiffies(sc->sched.channel_switch_time);

	if (test_and_set_bit(ATH_OP_MULTI_CHANNEL, &common->op_flags)) {
		spin_unlock_bh(&sc->chan_lock);
		return;
	}

	spin_unlock_bh(&sc->chan_lock);

	if (ath9k_is_chanctx_enabled()) {
		ath_chanctx_event(sc, NULL,
				  ATH_CHANCTX_EVENT_ENABLE_MULTICHANNEL);
	}
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
	struct timespec64 ts;
	u32 cur_tsf, prev_tsf, beacon_int;
	s32 offset;

	beacon_int = TU_TO_USEC(sc->cur_chan->beacon.beacon_interval);

	cur = sc->cur_chan;
	prev = ath_chanctx_get_next(sc, cur);

	if (!prev->switch_after_beacon)
		return;

	ktime_get_raw_ts64(&ts);
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

/* Configure the TSF based hardware timer for a channel switch.
 * Also set up backup software timer, in case the gen timer fails.
 * This could be caused by a hardware reset.
 */
static void ath_chanctx_setup_timer(struct ath_softc *sc, u32 tsf_time)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_hw *ah = sc->sc_ah;
	unsigned long timeout;

	ath9k_hw_gen_timer_start(ah, sc->p2p_ps_timer, tsf_time, 1000000);
	tsf_time -= ath9k_hw_gettsf32(ah);
	timeout = msecs_to_jiffies(tsf_time / 1000) + 1;
	mod_timer(&sc->sched.timer, jiffies + timeout);

	ath_dbg(common, CHAN_CTX,
		"Setup chanctx timer with timeout: %d (%d) ms\n",
		tsf_time / 1000, jiffies_to_msecs(timeout));
}

static void ath_chanctx_handle_bmiss(struct ath_softc *sc,
				     struct ath_chanctx *ctx,
				     struct ath_vif *avp)
{
	/*
	 * Clear the extend_absence flag if it had been
	 * set during the previous beacon transmission,
	 * since we need to revert to the normal NoA
	 * schedule.
	 */
	if (ctx->active && sc->sched.extend_absence) {
		avp->noa_duration = 0;
		sc->sched.extend_absence = false;
	}

	/* If at least two consecutive beacons were missed on the STA
	 * chanctx, stay on the STA channel for one extra beacon period,
	 * to resync the timer properly.
	 */
	if (ctx->active && sc->sched.beacon_miss >= 2) {
		avp->noa_duration = 0;
		sc->sched.extend_absence = true;
	}
}

static void ath_chanctx_offchannel_noa(struct ath_softc *sc,
				       struct ath_chanctx *ctx,
				       struct ath_vif *avp,
				       u32 tsf_time)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	avp->noa_index++;
	avp->offchannel_start = tsf_time;
	avp->offchannel_duration = sc->sched.offchannel_duration;

	ath_dbg(common, CHAN_CTX,
		"offchannel noa_duration: %d, noa_start: %u, noa_index: %d\n",
		avp->offchannel_duration,
		avp->offchannel_start,
		avp->noa_index);

	/*
	 * When multiple contexts are active, the NoA
	 * has to be recalculated and advertised after
	 * an offchannel operation.
	 */
	if (ctx->active && avp->noa_duration)
		avp->noa_duration = 0;
}

static void ath_chanctx_set_periodic_noa(struct ath_softc *sc,
					 struct ath_vif *avp,
					 struct ath_beacon_config *cur_conf,
					 u32 tsf_time,
					 u32 beacon_int)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	avp->noa_index++;
	avp->noa_start = tsf_time;

	if (sc->sched.extend_absence)
		avp->noa_duration = (3 * beacon_int / 2) +
			sc->sched.channel_switch_time;
	else
		avp->noa_duration =
			TU_TO_USEC(cur_conf->beacon_interval) / 2 +
			sc->sched.channel_switch_time;

	if (test_bit(ATH_OP_SCANNING, &common->op_flags) ||
	    sc->sched.extend_absence)
		avp->periodic_noa = false;
	else
		avp->periodic_noa = true;

	ath_dbg(common, CHAN_CTX,
		"noa_duration: %d, noa_start: %u, noa_index: %d, periodic: %d\n",
		avp->noa_duration,
		avp->noa_start,
		avp->noa_index,
		avp->periodic_noa);
}

static void ath_chanctx_set_oneshot_noa(struct ath_softc *sc,
					struct ath_vif *avp,
					u32 tsf_time,
					u32 duration)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	avp->noa_index++;
	avp->noa_start = tsf_time;
	avp->periodic_noa = false;
	avp->oneshot_noa = true;
	avp->noa_duration = duration + sc->sched.channel_switch_time;

	ath_dbg(common, CHAN_CTX,
		"oneshot noa_duration: %d, noa_start: %u, noa_index: %d, periodic: %d\n",
		avp->noa_duration,
		avp->noa_start,
		avp->noa_index,
		avp->periodic_noa);
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

	if (vif)
		avp = (struct ath_vif *) vif->drv_priv;

	spin_lock_bh(&sc->chan_lock);

	ath_dbg(common, CHAN_CTX, "cur_chan: %d MHz, event: %s, state: %s, delta: %u ms\n",
		sc->cur_chan->chandef.center_freq1,
		chanctx_event_string(ev),
		chanctx_state_string(sc->sched.state),
		chanctx_event_delta(sc));

	switch (ev) {
	case ATH_CHANCTX_EVENT_BEACON_PREPARE:
		if (avp->offchannel_duration)
			avp->offchannel_duration = 0;

		if (avp->oneshot_noa) {
			avp->noa_duration = 0;
			avp->oneshot_noa = false;

			ath_dbg(common, CHAN_CTX,
				"Clearing oneshot NoA\n");
		}

		if (avp->chanctx != sc->cur_chan) {
			ath_dbg(common, CHAN_CTX,
				"Contexts differ, not preparing beacon\n");
			break;
		}

		if (sc->sched.offchannel_pending && !sc->sched.wait_switch) {
			sc->sched.offchannel_pending = false;
			sc->next_chan = &sc->offchannel.chan;
			sc->sched.state = ATH_CHANCTX_STATE_WAIT_FOR_BEACON;
			ath_dbg(common, CHAN_CTX,
				"Setting offchannel_pending to false\n");
		}

		ctx = ath_chanctx_get_next(sc, sc->cur_chan);
		if (ctx->active && sc->sched.state == ATH_CHANCTX_STATE_IDLE) {
			sc->next_chan = ctx;
			sc->sched.state = ATH_CHANCTX_STATE_WAIT_FOR_BEACON;
			ath_dbg(common, CHAN_CTX,
				"Set next context, move chanctx state to WAIT_FOR_BEACON\n");
		}

		/* if the timer missed its window, use the next interval */
		if (sc->sched.state == ATH_CHANCTX_STATE_WAIT_FOR_TIMER) {
			sc->sched.state = ATH_CHANCTX_STATE_WAIT_FOR_BEACON;
			ath_dbg(common, CHAN_CTX,
				"Move chanctx state from WAIT_FOR_TIMER to WAIT_FOR_BEACON\n");
		}

		if (sc->sched.mgd_prepare_tx)
			sc->sched.state = ATH_CHANCTX_STATE_WAIT_FOR_BEACON;

		/*
		 * When a context becomes inactive, for example,
		 * disassociation of a station context, the NoA
		 * attribute needs to be removed from subsequent
		 * beacons.
		 */
		if (!ctx->active && avp->noa_duration &&
		    sc->sched.state != ATH_CHANCTX_STATE_WAIT_FOR_BEACON) {
			avp->noa_duration = 0;
			avp->periodic_noa = false;

			ath_dbg(common, CHAN_CTX,
				"Clearing NoA schedule\n");
		}

		if (sc->sched.state != ATH_CHANCTX_STATE_WAIT_FOR_BEACON)
			break;

		ath_dbg(common, CHAN_CTX, "Preparing beacon for vif: %pM\n", vif->addr);

		sc->sched.beacon_pending = true;
		sc->sched.next_tbtt = REG_READ(ah, AR_NEXT_TBTT_TIMER);

		cur_conf = &sc->cur_chan->beacon;
		beacon_int = TU_TO_USEC(cur_conf->beacon_interval);

		/* defer channel switch by a quarter beacon interval */
		tsf_time = sc->sched.next_tbtt + beacon_int / 4;
		sc->sched.switch_start_time = tsf_time;
		sc->cur_chan->last_beacon = sc->sched.next_tbtt;

		/*
		 * If an offchannel switch is scheduled to happen after
		 * a beacon transmission, update the NoA with one-shot
		 * values and increment the index.
		 */
		if (sc->next_chan == &sc->offchannel.chan) {
			ath_chanctx_offchannel_noa(sc, ctx, avp, tsf_time);
			break;
		}

		ath_chanctx_handle_bmiss(sc, ctx, avp);

		/*
		 * If a mgd_prepare_tx() has been called by mac80211,
		 * a one-shot NoA needs to be sent. This can happen
		 * with one or more active channel contexts - in both
		 * cases, a new NoA schedule has to be advertised.
		 */
		if (sc->sched.mgd_prepare_tx) {
			ath_chanctx_set_oneshot_noa(sc, avp, tsf_time,
						    jiffies_to_usecs(HZ / 5));
			break;
		}

		/* Prevent wrap-around issues */
		if (avp->noa_duration && tsf_time - avp->noa_start > BIT(30))
			avp->noa_duration = 0;

		/*
		 * If multiple contexts are active, start periodic
		 * NoA and increment the index for the first
		 * announcement.
		 */
		if (ctx->active &&
		    (!avp->noa_duration || sc->sched.force_noa_update))
			ath_chanctx_set_periodic_noa(sc, avp, cur_conf,
						     tsf_time, beacon_int);

		if (ctx->active && sc->sched.force_noa_update)
			sc->sched.force_noa_update = false;

		break;
	case ATH_CHANCTX_EVENT_BEACON_SENT:
		if (!sc->sched.beacon_pending) {
			ath_dbg(common, CHAN_CTX,
				"No pending beacon\n");
			break;
		}

		sc->sched.beacon_pending = false;

		if (sc->sched.mgd_prepare_tx) {
			sc->sched.mgd_prepare_tx = false;
			complete(&sc->go_beacon);
			ath_dbg(common, CHAN_CTX,
				"Beacon sent, complete go_beacon\n");
			break;
		}

		if (sc->sched.state != ATH_CHANCTX_STATE_WAIT_FOR_BEACON)
			break;

		ath_dbg(common, CHAN_CTX,
			"Move chanctx state to WAIT_FOR_TIMER\n");

		sc->sched.state = ATH_CHANCTX_STATE_WAIT_FOR_TIMER;
		ath_chanctx_setup_timer(sc, sc->sched.switch_start_time);
		break;
	case ATH_CHANCTX_EVENT_TSF_TIMER:
		if (sc->sched.state != ATH_CHANCTX_STATE_WAIT_FOR_TIMER)
			break;

		if (!sc->cur_chan->switch_after_beacon &&
		    sc->sched.beacon_pending)
			sc->sched.beacon_miss++;

		ath_dbg(common, CHAN_CTX,
			"Move chanctx state to SWITCH\n");

		sc->sched.state = ATH_CHANCTX_STATE_SWITCH;
		ieee80211_queue_work(sc->hw, &sc->chanctx_work);
		break;
	case ATH_CHANCTX_EVENT_BEACON_RECEIVED:
		if (!test_bit(ATH_OP_MULTI_CHANNEL, &common->op_flags) ||
		    sc->cur_chan == &sc->offchannel.chan)
			break;

		sc->sched.beacon_pending = false;
		sc->sched.beacon_miss = 0;

		if (sc->sched.state == ATH_CHANCTX_STATE_FORCE_ACTIVE ||
		    !sc->sched.beacon_adjust ||
		    !sc->cur_chan->tsf_val)
			break;

		ath_chanctx_adjust_tbtt_delta(sc);

		/* TSF time might have been updated by the incoming beacon,
		 * need update the channel switch timer to reflect the change.
		 */
		tsf_time = sc->sched.switch_start_time;
		tsf_time -= (u32) sc->cur_chan->tsf_val +
			ath9k_hw_get_tsf_offset(&sc->cur_chan->tsf_ts, NULL);
		tsf_time += ath9k_hw_gettsf32(ah);

		sc->sched.beacon_adjust = false;
		ath_chanctx_setup_timer(sc, tsf_time);
		break;
	case ATH_CHANCTX_EVENT_AUTHORIZED:
		if (sc->sched.state != ATH_CHANCTX_STATE_FORCE_ACTIVE ||
		    avp->chanctx != sc->cur_chan)
			break;

		ath_dbg(common, CHAN_CTX,
			"Move chanctx state from FORCE_ACTIVE to IDLE\n");

		sc->sched.state = ATH_CHANCTX_STATE_IDLE;
		fallthrough;
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

		ath_dbg(common, CHAN_CTX,
			"Move chanctx state to WAIT_FOR_TIMER (event SWITCH)\n");

		sc->sched.state = ATH_CHANCTX_STATE_WAIT_FOR_TIMER;
		sc->sched.wait_switch = false;

		tsf_time = TU_TO_USEC(cur_conf->beacon_interval) / 2;

		if (sc->sched.extend_absence) {
			sc->sched.beacon_miss = 0;
			tsf_time *= 3;
		}

		tsf_time -= sc->sched.channel_switch_time;
		tsf_time += ath9k_hw_gettsf32(sc->sc_ah);
		sc->sched.switch_start_time = tsf_time;

		ath_chanctx_setup_timer(sc, tsf_time);
		sc->sched.beacon_pending = true;
		sc->sched.beacon_adjust = true;
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
	case ATH_CHANCTX_EVENT_ASSIGN:
		break;
	case ATH_CHANCTX_EVENT_CHANGE:
		break;
	}

	spin_unlock_bh(&sc->chan_lock);
}

void ath_chanctx_beacon_sent_ev(struct ath_softc *sc,
				enum ath_chanctx_event ev)
{
	if (sc->sched.beacon_pending)
		ath_chanctx_event(sc, NULL, ev);
}

void ath_chanctx_beacon_recv_ev(struct ath_softc *sc,
				enum ath_chanctx_event ev)
{
	ath_chanctx_event(sc, NULL, ev);
}

static int ath_scan_channel_duration(struct ath_softc *sc,
				     struct ieee80211_channel *chan)
{
	struct cfg80211_scan_request *req = sc->offchannel.scan_req;

	if (!req->n_ssids || (chan->flags & IEEE80211_CHAN_NO_IR))
		return (HZ / 9); /* ~110 ms */

	return (HZ / 16); /* ~60 ms */
}

static void ath_chanctx_switch(struct ath_softc *sc, struct ath_chanctx *ctx,
			       struct cfg80211_chan_def *chandef)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	spin_lock_bh(&sc->chan_lock);

	if (test_bit(ATH_OP_MULTI_CHANNEL, &common->op_flags) &&
	    (sc->cur_chan != ctx) && (ctx == &sc->offchannel.chan)) {
		if (chandef)
			ctx->chandef = *chandef;

		sc->sched.offchannel_pending = true;
		sc->sched.wait_switch = true;
		sc->sched.offchannel_duration =
			jiffies_to_usecs(sc->offchannel.duration) +
			sc->sched.channel_switch_time;

		spin_unlock_bh(&sc->chan_lock);
		ath_dbg(common, CHAN_CTX,
			"Set offchannel_pending to true\n");
		return;
	}

	sc->next_chan = ctx;
	if (chandef) {
		ctx->chandef = *chandef;
		ath_dbg(common, CHAN_CTX,
			"Assigned next_chan to %d MHz\n", chandef->center_freq1);
	}

	if (sc->next_chan == &sc->offchannel.chan) {
		sc->sched.offchannel_duration =
			jiffies_to_usecs(sc->offchannel.duration) +
			sc->sched.channel_switch_time;

		if (chandef) {
			ath_dbg(common, CHAN_CTX,
				"Offchannel duration for chan %d MHz : %u\n",
				chandef->center_freq1,
				sc->sched.offchannel_duration);
		}
	}
	spin_unlock_bh(&sc->chan_lock);
	ieee80211_queue_work(sc->hw, &sc->chanctx_work);
}

static void ath_chanctx_offchan_switch(struct ath_softc *sc,
				       struct ieee80211_channel *chan)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct cfg80211_chan_def chandef;

	cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_NO_HT);
	ath_dbg(common, CHAN_CTX,
		"Channel definition created: %d MHz\n", chandef.center_freq1);

	ath_chanctx_switch(sc, &sc->offchannel.chan, &chandef);
}

static struct ath_chanctx *ath_chanctx_get_oper_chan(struct ath_softc *sc,
						     bool active)
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

static void
ath_scan_next_channel(struct ath_softc *sc)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct cfg80211_scan_request *req = sc->offchannel.scan_req;
	struct ieee80211_channel *chan;

	if (sc->offchannel.scan_idx >= req->n_channels) {
		ath_dbg(common, CHAN_CTX,
			"Moving offchannel state to ATH_OFFCHANNEL_IDLE, "
			"scan_idx: %d, n_channels: %d\n",
			sc->offchannel.scan_idx,
			req->n_channels);

		sc->offchannel.state = ATH_OFFCHANNEL_IDLE;
		ath_chanctx_switch(sc, ath_chanctx_get_oper_chan(sc, false),
				   NULL);
		return;
	}

	ath_dbg(common, CHAN_CTX,
		"Moving offchannel state to ATH_OFFCHANNEL_PROBE_SEND, scan_idx: %d\n",
		sc->offchannel.scan_idx);

	chan = req->channels[sc->offchannel.scan_idx++];
	sc->offchannel.duration = ath_scan_channel_duration(sc, chan);
	sc->offchannel.state = ATH_OFFCHANNEL_PROBE_SEND;

	ath_chanctx_offchan_switch(sc, chan);
}

void ath_offchannel_next(struct ath_softc *sc)
{
	struct ieee80211_vif *vif;

	if (sc->offchannel.scan_req) {
		vif = sc->offchannel.scan_vif;
		sc->offchannel.chan.txpower = vif->bss_conf.txpower;
		ath_scan_next_channel(sc);
	} else if (sc->offchannel.roc_vif) {
		vif = sc->offchannel.roc_vif;
		sc->offchannel.chan.txpower = vif->bss_conf.txpower;
		sc->offchannel.duration =
			msecs_to_jiffies(sc->offchannel.roc_duration);
		sc->offchannel.state = ATH_OFFCHANNEL_ROC_START;
		ath_chanctx_offchan_switch(sc, sc->offchannel.roc_chan);
	} else {
		spin_lock_bh(&sc->chan_lock);
		sc->sched.offchannel_pending = false;
		sc->sched.wait_switch = false;
		spin_unlock_bh(&sc->chan_lock);

		ath_chanctx_switch(sc, ath_chanctx_get_oper_chan(sc, false),
				   NULL);
		sc->offchannel.state = ATH_OFFCHANNEL_IDLE;
		if (sc->ps_idle)
			ath_cancel_work(sc);
	}
}

void ath_roc_complete(struct ath_softc *sc, enum ath_roc_complete_reason reason)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	sc->offchannel.roc_vif = NULL;
	sc->offchannel.roc_chan = NULL;

	switch (reason) {
	case ATH_ROC_COMPLETE_ABORT:
		ath_dbg(common, CHAN_CTX, "RoC aborted\n");
		ieee80211_remain_on_channel_expired(sc->hw);
		break;
	case ATH_ROC_COMPLETE_EXPIRE:
		ath_dbg(common, CHAN_CTX, "RoC expired\n");
		ieee80211_remain_on_channel_expired(sc->hw);
		break;
	case ATH_ROC_COMPLETE_CANCEL:
		ath_dbg(common, CHAN_CTX, "RoC canceled\n");
		break;
	}

	ath_offchannel_next(sc);
	ath9k_ps_restore(sc);
}

void ath_scan_complete(struct ath_softc *sc, bool abort)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct cfg80211_scan_info info = {
		.aborted = abort,
	};

	if (abort)
		ath_dbg(common, CHAN_CTX, "HW scan aborted\n");
	else
		ath_dbg(common, CHAN_CTX, "HW scan complete\n");

	sc->offchannel.scan_req = NULL;
	sc->offchannel.scan_vif = NULL;
	sc->offchannel.state = ATH_OFFCHANNEL_IDLE;
	ieee80211_scan_completed(sc->hw, &info);
	clear_bit(ATH_OP_SCANNING, &common->op_flags);
	spin_lock_bh(&sc->chan_lock);
	if (test_bit(ATH_OP_MULTI_CHANNEL, &common->op_flags))
		sc->sched.force_noa_update = true;
	spin_unlock_bh(&sc->chan_lock);
	ath_offchannel_next(sc);
	ath9k_ps_restore(sc);
}

static void ath_scan_send_probe(struct ath_softc *sc,
				struct cfg80211_ssid *ssid)
{
	struct cfg80211_scan_request *req = sc->offchannel.scan_req;
	struct ieee80211_vif *vif = sc->offchannel.scan_vif;
	struct ath_tx_control txctl = {};
	struct sk_buff *skb;
	struct ieee80211_tx_info *info;
	int band = sc->offchannel.chan.chandef.chan->band;

	skb = ieee80211_probereq_get(sc->hw, vif->addr,
			ssid->ssid, ssid->ssid_len, req->ie_len);
	if (!skb)
		return;

	info = IEEE80211_SKB_CB(skb);
	if (req->no_cck)
		info->flags |= IEEE80211_TX_CTL_NO_CCK_RATE;

	if (req->ie_len)
		skb_put_data(skb, req->ie, req->ie_len);

	skb_set_queue_mapping(skb, IEEE80211_AC_VO);

	if (!ieee80211_tx_prepare_skb(sc->hw, vif, skb, band, NULL))
		goto error;

	txctl.txq = sc->tx.txq_map[IEEE80211_AC_VO];
	if (ath_tx_start(sc->hw, skb, &txctl))
		goto error;

	return;

error:
	ieee80211_free_txskb(sc->hw, skb);
}

static void ath_scan_channel_start(struct ath_softc *sc)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct cfg80211_scan_request *req = sc->offchannel.scan_req;
	int i;

	if (!(sc->cur_chan->chandef.chan->flags & IEEE80211_CHAN_NO_IR) &&
	    req->n_ssids) {
		for (i = 0; i < req->n_ssids; i++)
			ath_scan_send_probe(sc, &req->ssids[i]);

	}

	ath_dbg(common, CHAN_CTX,
		"Moving offchannel state to ATH_OFFCHANNEL_PROBE_WAIT\n");

	sc->offchannel.state = ATH_OFFCHANNEL_PROBE_WAIT;
	mod_timer(&sc->offchannel.timer, jiffies + sc->offchannel.duration);
}

static void ath_chanctx_timer(struct timer_list *t)
{
	struct ath_softc *sc = from_timer(sc, t, sched.timer);
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	ath_dbg(common, CHAN_CTX,
		"Channel context timer invoked\n");

	ath_chanctx_event(sc, NULL, ATH_CHANCTX_EVENT_TSF_TIMER);
}

static void ath_offchannel_timer(struct timer_list *t)
{
	struct ath_softc *sc = from_timer(sc, t, offchannel.timer);
	struct ath_chanctx *ctx;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	ath_dbg(common, CHAN_CTX, "%s: offchannel state: %s\n",
		__func__, offchannel_state_string(sc->offchannel.state));

	switch (sc->offchannel.state) {
	case ATH_OFFCHANNEL_PROBE_WAIT:
		if (!sc->offchannel.scan_req)
			return;

		/* get first active channel context */
		ctx = ath_chanctx_get_oper_chan(sc, true);
		if (ctx->active) {
			ath_dbg(common, CHAN_CTX,
				"Switch to oper/active context, "
				"move offchannel state to ATH_OFFCHANNEL_SUSPEND\n");

			sc->offchannel.state = ATH_OFFCHANNEL_SUSPEND;
			ath_chanctx_switch(sc, ctx, NULL);
			mod_timer(&sc->offchannel.timer, jiffies + HZ / 10);
			break;
		}
		fallthrough;
	case ATH_OFFCHANNEL_SUSPEND:
		if (!sc->offchannel.scan_req)
			return;

		ath_scan_next_channel(sc);
		break;
	case ATH_OFFCHANNEL_ROC_START:
	case ATH_OFFCHANNEL_ROC_WAIT:
		sc->offchannel.state = ATH_OFFCHANNEL_ROC_DONE;
		ath_roc_complete(sc, ATH_ROC_COMPLETE_EXPIRE);
		break;
	default:
		break;
	}
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
		if (!avp->assoc)
			return false;

		skb = ieee80211_nullfunc_get(sc->hw, vif, false);
		if (!skb)
			return false;

		nullfunc = (struct ieee80211_hdr_3addr *) skb->data;
		if (powersave)
			nullfunc->frame_control |=
				cpu_to_le16(IEEE80211_FCTL_PM);

		skb->priority = 7;
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
	if (ath_tx_start(sc->hw, skb, &txctl)) {
		ieee80211_free_txskb(sc->hw, skb);
		return false;
	}

	return true;
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
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	if (sc->cur_chan == &sc->offchannel.chan)
		return false;

	switch (sc->sched.state) {
	case ATH_CHANCTX_STATE_SWITCH:
		return false;
	case ATH_CHANCTX_STATE_IDLE:
		if (!sc->cur_chan->switch_after_beacon)
			return false;

		ath_dbg(common, CHAN_CTX,
			"Defer switch, set chanctx state to WAIT_FOR_BEACON\n");

		sc->sched.state = ATH_CHANCTX_STATE_WAIT_FOR_BEACON;
		break;
	default:
		break;
	}

	return true;
}

static void ath_offchannel_channel_change(struct ath_softc *sc)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	ath_dbg(common, CHAN_CTX, "%s: offchannel state: %s\n",
		__func__, offchannel_state_string(sc->offchannel.state));

	switch (sc->offchannel.state) {
	case ATH_OFFCHANNEL_PROBE_SEND:
		if (!sc->offchannel.scan_req)
			return;

		if (sc->cur_chan->chandef.chan !=
		    sc->offchannel.chan.chandef.chan)
			return;

		ath_scan_channel_start(sc);
		break;
	case ATH_OFFCHANNEL_IDLE:
		if (!sc->offchannel.scan_req)
			return;

		ath_scan_complete(sc, false);
		break;
	case ATH_OFFCHANNEL_ROC_START:
		if (sc->cur_chan != &sc->offchannel.chan)
			break;

		sc->offchannel.state = ATH_OFFCHANNEL_ROC_WAIT;
		mod_timer(&sc->offchannel.timer,
			  jiffies + sc->offchannel.duration);
		ieee80211_ready_on_channel(sc->hw);
		break;
	case ATH_OFFCHANNEL_ROC_DONE:
		break;
	default:
		break;
	}
}

void ath_chanctx_set_next(struct ath_softc *sc, bool force)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_chanctx *old_ctx;
	struct timespec64 ts;
	bool measure_time = false;
	bool send_ps = false;
	bool queues_stopped = false;

	spin_lock_bh(&sc->chan_lock);
	if (!sc->next_chan) {
		spin_unlock_bh(&sc->chan_lock);
		return;
	}

	if (!force && ath_chanctx_defer_switch(sc)) {
		spin_unlock_bh(&sc->chan_lock);
		return;
	}

	ath_dbg(common, CHAN_CTX,
		"%s: current: %d MHz, next: %d MHz\n",
		__func__,
		sc->cur_chan->chandef.center_freq1,
		sc->next_chan->chandef.center_freq1);

	if (sc->cur_chan != sc->next_chan) {
		ath_dbg(common, CHAN_CTX,
			"Stopping current chanctx: %d\n",
			sc->cur_chan->chandef.center_freq1);
		sc->cur_chan->stopped = true;
		spin_unlock_bh(&sc->chan_lock);

		if (sc->next_chan == &sc->offchannel.chan) {
			ktime_get_raw_ts64(&ts);
			measure_time = true;
		}

		ath9k_chanctx_stop_queues(sc, sc->cur_chan);
		queues_stopped = true;

		__ath9k_flush(sc->hw, ~0, true, false, false);

		if (ath_chanctx_send_ps_frame(sc, true))
			__ath9k_flush(sc->hw, BIT(IEEE80211_AC_VO),
				      false, false, false);

		send_ps = true;
		spin_lock_bh(&sc->chan_lock);

		if (sc->cur_chan != &sc->offchannel.chan) {
			ktime_get_raw_ts64(&sc->cur_chan->tsf_ts);
			sc->cur_chan->tsf_val = ath9k_hw_gettsf64(sc->sc_ah);
		}
	}
	old_ctx = sc->cur_chan;
	sc->cur_chan = sc->next_chan;
	sc->cur_chan->stopped = false;
	sc->next_chan = NULL;

	if (!sc->sched.offchannel_pending)
		sc->sched.offchannel_duration = 0;

	if (sc->sched.state != ATH_CHANCTX_STATE_FORCE_ACTIVE)
		sc->sched.state = ATH_CHANCTX_STATE_IDLE;

	spin_unlock_bh(&sc->chan_lock);

	if (sc->sc_ah->chip_fullsleep ||
	    memcmp(&sc->cur_chandef, &sc->cur_chan->chandef,
		   sizeof(sc->cur_chandef))) {
		ath_dbg(common, CHAN_CTX,
			"%s: Set channel %d MHz\n",
			__func__, sc->cur_chan->chandef.center_freq1);
		ath_set_channel(sc);
		if (measure_time)
			sc->sched.channel_switch_time =
				ath9k_hw_get_tsf_offset(&ts, NULL);
		/*
		 * A reset will ensure that all queues are woken up,
		 * so there is no need to awaken them again.
		 */
		goto out;
	}

	if (queues_stopped)
		ath9k_chanctx_wake_queues(sc, old_ctx);
out:
	if (send_ps)
		ath_chanctx_send_ps_frame(sc, false);

	ath_offchannel_channel_change(sc);
	ath_chanctx_event(sc, NULL, ATH_CHANCTX_EVENT_SWITCH);
}

static void ath_chanctx_work(struct work_struct *work)
{
	struct ath_softc *sc = container_of(work, struct ath_softc,
					    chanctx_work);
	mutex_lock(&sc->mutex);
	ath_chanctx_set_next(sc, false);
	mutex_unlock(&sc->mutex);
}

void ath9k_offchannel_init(struct ath_softc *sc)
{
	struct ath_chanctx *ctx;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *chan;
	int i;

	sband = &common->sbands[NL80211_BAND_2GHZ];
	if (!sband->n_channels)
		sband = &common->sbands[NL80211_BAND_5GHZ];

	chan = &sband->channels[0];

	ctx = &sc->offchannel.chan;
	INIT_LIST_HEAD(&ctx->vifs);
	ctx->txpower = ATH_TXPOWER_MAX;
	cfg80211_chandef_create(&ctx->chandef, chan, NL80211_CHAN_HT20);

	for (i = 0; i < ARRAY_SIZE(ctx->acq); i++) {
		INIT_LIST_HEAD(&ctx->acq[i].acq_new);
		INIT_LIST_HEAD(&ctx->acq[i].acq_old);
		spin_lock_init(&ctx->acq[i].lock);
	}

	sc->offchannel.chan.offchannel = true;
}

void ath9k_init_channel_context(struct ath_softc *sc)
{
	INIT_WORK(&sc->chanctx_work, ath_chanctx_work);

	timer_setup(&sc->offchannel.timer, ath_offchannel_timer, 0);
	timer_setup(&sc->sched.timer, ath_chanctx_timer, 0);

	init_completion(&sc->go_beacon);
}

void ath9k_deinit_channel_context(struct ath_softc *sc)
{
	cancel_work_sync(&sc->chanctx_work);
}

bool ath9k_is_chanctx_enabled(void)
{
	return (ath9k_use_chanctx == 1);
}

/********************/
/* Queue management */
/********************/

void ath9k_chanctx_stop_queues(struct ath_softc *sc, struct ath_chanctx *ctx)
{
	struct ath_hw *ah = sc->sc_ah;
	int i;

	if (ctx == &sc->offchannel.chan) {
		ieee80211_stop_queue(sc->hw,
				     sc->hw->offchannel_tx_hw_queue);
	} else {
		for (i = 0; i < IEEE80211_NUM_ACS; i++)
			ieee80211_stop_queue(sc->hw,
					     ctx->hw_queue_base + i);
	}

	if (ah->opmode == NL80211_IFTYPE_AP)
		ieee80211_stop_queue(sc->hw, sc->hw->queues - 2);
}


void ath9k_chanctx_wake_queues(struct ath_softc *sc, struct ath_chanctx *ctx)
{
	struct ath_hw *ah = sc->sc_ah;
	int i;

	if (ctx == &sc->offchannel.chan) {
		ieee80211_wake_queue(sc->hw,
				     sc->hw->offchannel_tx_hw_queue);
	} else {
		for (i = 0; i < IEEE80211_NUM_ACS; i++)
			ieee80211_wake_queue(sc->hw,
					     ctx->hw_queue_base + i);
	}

	if (ah->opmode == NL80211_IFTYPE_AP)
		ieee80211_wake_queue(sc->hw, sc->hw->queues - 2);
}

/*****************/
/* P2P Powersave */
/*****************/

static void ath9k_update_p2p_ps_timer(struct ath_softc *sc, struct ath_vif *avp)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_hw *ah = sc->sc_ah;
	u32 tsf, target_tsf;

	if (!avp || !avp->noa.has_next_tsf)
		return;

	ath9k_hw_gen_timer_stop(ah, sc->p2p_ps_timer);

	tsf = ath9k_hw_gettsf32(sc->sc_ah);

	target_tsf = avp->noa.next_tsf;
	if (!avp->noa.absent)
		target_tsf -= ATH_P2P_PS_STOP_TIME;
	else
		target_tsf += ATH_P2P_PS_STOP_TIME;

	if (target_tsf - tsf < ATH_P2P_PS_STOP_TIME)
		target_tsf = tsf + ATH_P2P_PS_STOP_TIME;

	ath_dbg(common, CHAN_CTX, "%s absent %d tsf 0x%08X next_tsf 0x%08X (%dms)\n",
		__func__, avp->noa.absent, tsf, target_tsf,
		(target_tsf - tsf) / 1000);

	ath9k_hw_gen_timer_start(ah, sc->p2p_ps_timer, target_tsf, 1000000);
}

static void ath9k_update_p2p_ps(struct ath_softc *sc, struct ieee80211_vif *vif)
{
	struct ath_vif *avp = (void *)vif->drv_priv;
	u32 tsf;

	if (!sc->p2p_ps_timer)
		return;

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	sc->p2p_ps_vif = avp;

	if (sc->ps_flags & PS_BEACON_SYNC)
		return;

	tsf = ath9k_hw_gettsf32(sc->sc_ah);
	ieee80211_parse_p2p_noa(&vif->bss_conf.p2p_noa_attr, &avp->noa, tsf);
	ath9k_update_p2p_ps_timer(sc, avp);
}

static u8 ath9k_get_ctwin(struct ath_softc *sc, struct ath_vif *avp)
{
	struct ath_beacon_config *cur_conf = &sc->cur_chan->beacon;
	u8 switch_time, ctwin;

	/*
	 * Channel switch in multi-channel mode is deferred
	 * by a quarter beacon interval when handling
	 * ATH_CHANCTX_EVENT_BEACON_PREPARE, so the P2P-GO
	 * interface is guaranteed to be discoverable
	 * for that duration after a TBTT.
	 */
	switch_time = cur_conf->beacon_interval / 4;

	ctwin = avp->vif->bss_conf.p2p_noa_attr.oppps_ctwindow;
	if (ctwin && (ctwin < switch_time))
		return ctwin;

	if (switch_time < P2P_DEFAULT_CTWIN)
		return 0;

	return P2P_DEFAULT_CTWIN;
}

void ath9k_beacon_add_noa(struct ath_softc *sc, struct ath_vif *avp,
			  struct sk_buff *skb)
{
	static const u8 noa_ie_hdr[] = {
		WLAN_EID_VENDOR_SPECIFIC,	/* type */
		0,				/* length */
		0x50, 0x6f, 0x9a,		/* WFA OUI */
		0x09,				/* P2P subtype */
		0x0c,				/* Notice of Absence */
		0x00,				/* LSB of little-endian len */
		0x00,				/* MSB of little-endian len */
	};

	struct ieee80211_p2p_noa_attr *noa;
	int noa_len, noa_desc, i = 0;
	u8 *hdr;

	if (!avp->offchannel_duration && !avp->noa_duration)
		return;

	noa_desc = !!avp->offchannel_duration + !!avp->noa_duration;
	noa_len = 2 + sizeof(struct ieee80211_p2p_noa_desc) * noa_desc;

	hdr = skb_put_data(skb, noa_ie_hdr, sizeof(noa_ie_hdr));
	hdr[1] = sizeof(noa_ie_hdr) + noa_len - 2;
	hdr[7] = noa_len;

	noa = skb_put_zero(skb, noa_len);

	noa->index = avp->noa_index;
	noa->oppps_ctwindow = ath9k_get_ctwin(sc, avp);
	if (noa->oppps_ctwindow)
		noa->oppps_ctwindow |= BIT(7);

	if (avp->noa_duration) {
		if (avp->periodic_noa) {
			u32 interval = TU_TO_USEC(sc->cur_chan->beacon.beacon_interval);
			noa->desc[i].count = 255;
			noa->desc[i].interval = cpu_to_le32(interval);
		} else {
			noa->desc[i].count = 1;
		}

		noa->desc[i].start_time = cpu_to_le32(avp->noa_start);
		noa->desc[i].duration = cpu_to_le32(avp->noa_duration);
		i++;
	}

	if (avp->offchannel_duration) {
		noa->desc[i].count = 1;
		noa->desc[i].start_time = cpu_to_le32(avp->offchannel_start);
		noa->desc[i].duration = cpu_to_le32(avp->offchannel_duration);
	}
}

void ath9k_p2p_ps_timer(void *priv)
{
	struct ath_softc *sc = priv;
	struct ath_vif *avp = sc->p2p_ps_vif;
	struct ieee80211_vif *vif;
	struct ieee80211_sta *sta;
	struct ath_node *an;
	u32 tsf;

	del_timer_sync(&sc->sched.timer);
	ath9k_hw_gen_timer_stop(sc->sc_ah, sc->p2p_ps_timer);
	ath_chanctx_event(sc, NULL, ATH_CHANCTX_EVENT_TSF_TIMER);

	if (!avp || avp->chanctx != sc->cur_chan)
		return;

	tsf = ath9k_hw_gettsf32(sc->sc_ah);
	if (!avp->noa.absent)
		tsf += ATH_P2P_PS_STOP_TIME;
	else
		tsf -= ATH_P2P_PS_STOP_TIME;

	if (!avp->noa.has_next_tsf ||
	    avp->noa.next_tsf - tsf > BIT(31))
		ieee80211_update_p2p_noa(&avp->noa, tsf);

	ath9k_update_p2p_ps_timer(sc, avp);

	rcu_read_lock();

	vif = avp->vif;
	sta = ieee80211_find_sta(vif, avp->bssid);
	if (!sta)
		goto out;

	an = (void *) sta->drv_priv;
	if (an->sleeping == !!avp->noa.absent)
		goto out;

	an->sleeping = avp->noa.absent;
	if (an->sleeping)
		ath_tx_aggr_sleep(sta, sc, an);
	else
		ath_tx_aggr_wakeup(sc, an);

out:
	rcu_read_unlock();
}

void ath9k_p2p_bss_info_changed(struct ath_softc *sc,
				struct ieee80211_vif *vif)
{
	unsigned long flags;

	spin_lock_bh(&sc->sc_pcu_lock);
	spin_lock_irqsave(&sc->sc_pm_lock, flags);
	ath9k_update_p2p_ps(sc, vif);
	spin_unlock_irqrestore(&sc->sc_pm_lock, flags);
	spin_unlock_bh(&sc->sc_pcu_lock);
}

void ath9k_p2p_beacon_sync(struct ath_softc *sc)
{
	if (sc->p2p_ps_vif)
		ath9k_update_p2p_ps(sc, sc->p2p_ps_vif->vif);
}

void ath9k_p2p_remove_vif(struct ath_softc *sc,
			  struct ieee80211_vif *vif)
{
	struct ath_vif *avp = (void *)vif->drv_priv;

	spin_lock_bh(&sc->sc_pcu_lock);
	if (avp == sc->p2p_ps_vif) {
		sc->p2p_ps_vif = NULL;
		ath9k_update_p2p_ps_timer(sc, NULL);
	}
	spin_unlock_bh(&sc->sc_pcu_lock);
}

int ath9k_init_p2p(struct ath_softc *sc)
{
	sc->p2p_ps_timer = ath_gen_timer_alloc(sc->sc_ah, ath9k_p2p_ps_timer,
					       NULL, sc, AR_FIRST_NDP_TIMER);
	if (!sc->p2p_ps_timer)
		return -ENOMEM;

	return 0;
}

void ath9k_deinit_p2p(struct ath_softc *sc)
{
	if (sc->p2p_ps_timer)
		ath_gen_timer_free(sc->sc_ah, sc->p2p_ps_timer);
}

#endif /* CONFIG_ATH9K_CHANNEL_CONTEXT */
