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

	if (n_active > 1)
		set_bit(ATH_OP_MULTI_CHANNEL, &common->op_flags);
	else
		clear_bit(ATH_OP_MULTI_CHANNEL, &common->op_flags);
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

void ath_chanctx_work(struct work_struct *work)
{
	struct ath_softc *sc = container_of(work, struct ath_softc,
					    chanctx_work);
	struct timespec ts;
	bool measure_time = false;
	bool send_ps = false;

	mutex_lock(&sc->mutex);
	spin_lock_bh(&sc->chan_lock);
	if (!sc->next_chan) {
		spin_unlock_bh(&sc->chan_lock);
		mutex_unlock(&sc->mutex);
		return;
	}

	if (ath_chanctx_defer_switch(sc)) {
		spin_unlock_bh(&sc->chan_lock);
		mutex_unlock(&sc->mutex);
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

void ath_chanctx_switch(struct ath_softc *sc, struct ath_chanctx *ctx,
			struct cfg80211_chan_def *chandef)
{

	spin_lock_bh(&sc->chan_lock);
	sc->next_chan = ctx;
	if (chandef)
		ctx->chandef = *chandef;
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

void ath_chanctx_event(struct ath_softc *sc, struct ieee80211_vif *vif,
		       enum ath_chanctx_event ev)
{
	struct ath_hw *ah = sc->sc_ah;
	u32 tsf_time;

	spin_lock_bh(&sc->chan_lock);

	switch (ev) {
	case ATH_CHANCTX_EVENT_BEACON_PREPARE:
		if (sc->sched.state != ATH_CHANCTX_STATE_WAIT_FOR_BEACON)
			break;

		sc->sched.beacon_pending = true;
		sc->sched.next_tbtt = REG_READ(ah, AR_NEXT_TBTT_TIMER);
		break;
	case ATH_CHANCTX_EVENT_BEACON_SENT:
		if (!sc->sched.beacon_pending)
			break;

		sc->sched.beacon_pending = false;
		if (sc->sched.state != ATH_CHANCTX_STATE_WAIT_FOR_BEACON)
			break;

		/* defer channel switch by a quarter beacon interval */
		tsf_time = TU_TO_USEC(sc->cur_chan->beacon.beacon_interval);
		tsf_time = sc->sched.next_tbtt + tsf_time / 4;
		sc->sched.state = ATH_CHANCTX_STATE_WAIT_FOR_TIMER;
		ath9k_hw_gen_timer_start(ah, sc->p2p_ps_timer, tsf_time,
				1000000);
		break;
	case ATH_CHANCTX_EVENT_TSF_TIMER:
		if (sc->sched.state != ATH_CHANCTX_STATE_WAIT_FOR_TIMER)
			break;

		sc->sched.state = ATH_CHANCTX_STATE_SWITCH;
		ieee80211_queue_work(sc->hw, &sc->chanctx_work);
		break;
	}

	spin_unlock_bh(&sc->chan_lock);
}
