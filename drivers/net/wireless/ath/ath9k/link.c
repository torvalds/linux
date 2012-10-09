/*
 * Copyright (c) 2012 Qualcomm Atheros, Inc.
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

/*
 * TX polling - checks if the TX engine is stuck somewhere
 * and issues a chip reset if so.
 */
void ath_tx_complete_poll_work(struct work_struct *work)
{
	struct ath_softc *sc = container_of(work, struct ath_softc,
					    tx_complete_work.work);
	struct ath_txq *txq;
	int i;
	bool needreset = false;
#ifdef CONFIG_ATH9K_DEBUGFS
	sc->tx_complete_poll_work_seen++;
#endif

	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++)
		if (ATH_TXQ_SETUP(sc, i)) {
			txq = &sc->tx.txq[i];
			ath_txq_lock(sc, txq);
			if (txq->axq_depth) {
				if (txq->axq_tx_inprogress) {
					needreset = true;
					ath_txq_unlock(sc, txq);
					break;
				} else {
					txq->axq_tx_inprogress = true;
				}
			}
			ath_txq_unlock_complete(sc, txq);
		}

	if (needreset) {
		ath_dbg(ath9k_hw_common(sc->sc_ah), RESET,
			"tx hung, resetting the chip\n");
		ath9k_queue_reset(sc, RESET_TYPE_TX_HANG);
		return;
	}

	ieee80211_queue_delayed_work(sc->hw, &sc->tx_complete_work,
				     msecs_to_jiffies(ATH_TX_COMPLETE_POLL_INT));
}

/*
 * Checks if the BB/MAC is hung.
 */
void ath_hw_check(struct work_struct *work)
{
	struct ath_softc *sc = container_of(work, struct ath_softc, hw_check_work);
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	unsigned long flags;
	int busy;
	u8 is_alive, nbeacon = 1;
	enum ath_reset_type type;

	ath9k_ps_wakeup(sc);
	is_alive = ath9k_hw_check_alive(sc->sc_ah);

	if (is_alive && !AR_SREV_9300(sc->sc_ah))
		goto out;
	else if (!is_alive && AR_SREV_9300(sc->sc_ah)) {
		ath_dbg(common, RESET,
			"DCU stuck is detected. Schedule chip reset\n");
		type = RESET_TYPE_MAC_HANG;
		goto sched_reset;
	}

	spin_lock_irqsave(&common->cc_lock, flags);
	busy = ath_update_survey_stats(sc);
	spin_unlock_irqrestore(&common->cc_lock, flags);

	ath_dbg(common, RESET, "Possible baseband hang, busy=%d (try %d)\n",
		busy, sc->hw_busy_count + 1);
	if (busy >= 99) {
		if (++sc->hw_busy_count >= 3) {
			type = RESET_TYPE_BB_HANG;
			goto sched_reset;
		}
	} else if (busy >= 0) {
		sc->hw_busy_count = 0;
		nbeacon = 3;
	}

	ath_start_rx_poll(sc, nbeacon);
	goto out;

sched_reset:
	ath9k_queue_reset(sc, type);
out:
	ath9k_ps_restore(sc);
}

/*
 * PLL-WAR for AR9485/AR9340
 */
static bool ath_hw_pll_rx_hang_check(struct ath_softc *sc, u32 pll_sqsum)
{
	static int count;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	if (pll_sqsum >= 0x40000) {
		count++;
		if (count == 3) {
			ath_dbg(common, RESET, "PLL WAR, resetting the chip\n");
			ath9k_queue_reset(sc, RESET_TYPE_PLL_HANG);
			count = 0;
			return true;
		}
	} else {
		count = 0;
	}

	return false;
}

void ath_hw_pll_work(struct work_struct *work)
{
	u32 pll_sqsum;
	struct ath_softc *sc = container_of(work, struct ath_softc,
					    hw_pll_work.work);
	/*
	 * ensure that the PLL WAR is executed only
	 * after the STA is associated (or) if the
	 * beaconing had started in interfaces that
	 * uses beacons.
	 */
	if (!test_bit(SC_OP_BEACONS, &sc->sc_flags))
		return;

	ath9k_ps_wakeup(sc);
	pll_sqsum = ar9003_get_pll_sqsum_dvc(sc->sc_ah);
	ath9k_ps_restore(sc);
	if (ath_hw_pll_rx_hang_check(sc, pll_sqsum))
		return;

	ieee80211_queue_delayed_work(sc->hw, &sc->hw_pll_work,
				     msecs_to_jiffies(ATH_PLL_WORK_INTERVAL));
}

/*
 * RX Polling - monitors baseband hangs.
 */
void ath_start_rx_poll(struct ath_softc *sc, u8 nbeacon)
{
	if (!AR_SREV_9300(sc->sc_ah))
		return;

	if (!test_bit(SC_OP_PRIM_STA_VIF, &sc->sc_flags))
		return;

	mod_timer(&sc->rx_poll_timer, jiffies + msecs_to_jiffies
		  (nbeacon * sc->cur_beacon_conf.beacon_interval));
}

void ath_rx_poll(unsigned long data)
{
	struct ath_softc *sc = (struct ath_softc *)data;

	ieee80211_queue_work(sc->hw, &sc->hw_check_work);
}

/*
 * PA Pre-distortion.
 */
static void ath_paprd_activate(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath9k_hw_cal_data *caldata = ah->caldata;
	int chain;

	if (!caldata || !caldata->paprd_done)
		return;

	ath9k_ps_wakeup(sc);
	ar9003_paprd_enable(ah, false);
	for (chain = 0; chain < AR9300_MAX_CHAINS; chain++) {
		if (!(ah->txchainmask & BIT(chain)))
			continue;

		ar9003_paprd_populate_single_table(ah, caldata, chain);
	}

	ar9003_paprd_enable(ah, true);
	ath9k_ps_restore(sc);
}

static bool ath_paprd_send_frame(struct ath_softc *sc, struct sk_buff *skb, int chain)
{
	struct ieee80211_hw *hw = sc->hw;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_tx_control txctl;
	int time_left;

	memset(&txctl, 0, sizeof(txctl));
	txctl.txq = sc->tx.txq_map[WME_AC_BE];

	memset(tx_info, 0, sizeof(*tx_info));
	tx_info->band = hw->conf.channel->band;
	tx_info->flags |= IEEE80211_TX_CTL_NO_ACK;
	tx_info->control.rates[0].idx = 0;
	tx_info->control.rates[0].count = 1;
	tx_info->control.rates[0].flags = IEEE80211_TX_RC_MCS;
	tx_info->control.rates[1].idx = -1;

	init_completion(&sc->paprd_complete);
	txctl.paprd = BIT(chain);

	if (ath_tx_start(hw, skb, &txctl) != 0) {
		ath_dbg(common, CALIBRATE, "PAPRD TX failed\n");
		dev_kfree_skb_any(skb);
		return false;
	}

	time_left = wait_for_completion_timeout(&sc->paprd_complete,
			msecs_to_jiffies(ATH_PAPRD_TIMEOUT));

	if (!time_left)
		ath_dbg(common, CALIBRATE,
			"Timeout waiting for paprd training on TX chain %d\n",
			chain);

	return !!time_left;
}

void ath_paprd_calibrate(struct work_struct *work)
{
	struct ath_softc *sc = container_of(work, struct ath_softc, paprd_work);
	struct ieee80211_hw *hw = sc->hw;
	struct ath_hw *ah = sc->sc_ah;
	struct ieee80211_hdr *hdr;
	struct sk_buff *skb = NULL;
	struct ath9k_hw_cal_data *caldata = ah->caldata;
	struct ath_common *common = ath9k_hw_common(ah);
	int ftype;
	int chain_ok = 0;
	int chain;
	int len = 1800;

	if (!caldata)
		return;

	ath9k_ps_wakeup(sc);

	if (ar9003_paprd_init_table(ah) < 0)
		goto fail_paprd;

	skb = alloc_skb(len, GFP_KERNEL);
	if (!skb)
		goto fail_paprd;

	skb_put(skb, len);
	memset(skb->data, 0, len);
	hdr = (struct ieee80211_hdr *)skb->data;
	ftype = IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC;
	hdr->frame_control = cpu_to_le16(ftype);
	hdr->duration_id = cpu_to_le16(10);
	memcpy(hdr->addr1, hw->wiphy->perm_addr, ETH_ALEN);
	memcpy(hdr->addr2, hw->wiphy->perm_addr, ETH_ALEN);
	memcpy(hdr->addr3, hw->wiphy->perm_addr, ETH_ALEN);

	for (chain = 0; chain < AR9300_MAX_CHAINS; chain++) {
		if (!(ah->txchainmask & BIT(chain)))
			continue;

		chain_ok = 0;

		ath_dbg(common, CALIBRATE,
			"Sending PAPRD frame for thermal measurement on chain %d\n",
			chain);
		if (!ath_paprd_send_frame(sc, skb, chain))
			goto fail_paprd;

		ar9003_paprd_setup_gain_table(ah, chain);

		ath_dbg(common, CALIBRATE,
			"Sending PAPRD training frame on chain %d\n", chain);
		if (!ath_paprd_send_frame(sc, skb, chain))
			goto fail_paprd;

		if (!ar9003_paprd_is_done(ah)) {
			ath_dbg(common, CALIBRATE,
				"PAPRD not yet done on chain %d\n", chain);
			break;
		}

		if (ar9003_paprd_create_curve(ah, caldata, chain)) {
			ath_dbg(common, CALIBRATE,
				"PAPRD create curve failed on chain %d\n",
				chain);
			break;
		}

		chain_ok = 1;
	}
	kfree_skb(skb);

	if (chain_ok) {
		caldata->paprd_done = true;
		ath_paprd_activate(sc);
	}

fail_paprd:
	ath9k_ps_restore(sc);
}

/*
 *  ANI performs periodic noise floor calibration
 *  that is used to adjust and optimize the chip performance.  This
 *  takes environmental changes (location, temperature) into account.
 *  When the task is complete, it reschedules itself depending on the
 *  appropriate interval that was calculated.
 */
void ath_ani_calibrate(unsigned long data)
{
	struct ath_softc *sc = (struct ath_softc *)data;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	bool longcal = false;
	bool shortcal = false;
	bool aniflag = false;
	unsigned int timestamp = jiffies_to_msecs(jiffies);
	u32 cal_interval, short_cal_interval, long_cal_interval;
	unsigned long flags;

	if (ah->caldata && ah->caldata->nfcal_interference)
		long_cal_interval = ATH_LONG_CALINTERVAL_INT;
	else
		long_cal_interval = ATH_LONG_CALINTERVAL;

	short_cal_interval = (ah->opmode == NL80211_IFTYPE_AP) ?
		ATH_AP_SHORT_CALINTERVAL : ATH_STA_SHORT_CALINTERVAL;

	/* Only calibrate if awake */
	if (sc->sc_ah->power_mode != ATH9K_PM_AWAKE)
		goto set_timer;

	ath9k_ps_wakeup(sc);

	/* Long calibration runs independently of short calibration. */
	if ((timestamp - common->ani.longcal_timer) >= long_cal_interval) {
		longcal = true;
		common->ani.longcal_timer = timestamp;
	}

	/* Short calibration applies only while caldone is false */
	if (!common->ani.caldone) {
		if ((timestamp - common->ani.shortcal_timer) >= short_cal_interval) {
			shortcal = true;
			common->ani.shortcal_timer = timestamp;
			common->ani.resetcal_timer = timestamp;
		}
	} else {
		if ((timestamp - common->ani.resetcal_timer) >=
		    ATH_RESTART_CALINTERVAL) {
			common->ani.caldone = ath9k_hw_reset_calvalid(ah);
			if (common->ani.caldone)
				common->ani.resetcal_timer = timestamp;
		}
	}

	/* Verify whether we must check ANI */
	if (sc->sc_ah->config.enable_ani
	    && (timestamp - common->ani.checkani_timer) >=
	    ah->config.ani_poll_interval) {
		aniflag = true;
		common->ani.checkani_timer = timestamp;
	}

	/* Call ANI routine if necessary */
	if (aniflag) {
		spin_lock_irqsave(&common->cc_lock, flags);
		ath9k_hw_ani_monitor(ah, ah->curchan);
		ath_update_survey_stats(sc);
		spin_unlock_irqrestore(&common->cc_lock, flags);
	}

	/* Perform calibration if necessary */
	if (longcal || shortcal) {
		common->ani.caldone =
			ath9k_hw_calibrate(ah, ah->curchan,
					   ah->rxchainmask, longcal);
	}

	ath_dbg(common, ANI,
		"Calibration @%lu finished: %s %s %s, caldone: %s\n",
		jiffies,
		longcal ? "long" : "", shortcal ? "short" : "",
		aniflag ? "ani" : "", common->ani.caldone ? "true" : "false");

	ath9k_debug_samp_bb_mac(sc);
	ath9k_ps_restore(sc);

set_timer:
	/*
	* Set timer interval based on previous results.
	* The interval must be the shortest necessary to satisfy ANI,
	* short calibration and long calibration.
	*/
	cal_interval = ATH_LONG_CALINTERVAL;
	if (sc->sc_ah->config.enable_ani)
		cal_interval = min(cal_interval,
				   (u32)ah->config.ani_poll_interval);
	if (!common->ani.caldone)
		cal_interval = min(cal_interval, (u32)short_cal_interval);

	mod_timer(&common->ani.timer, jiffies + msecs_to_jiffies(cal_interval));
	if ((sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_PAPRD) && ah->caldata) {
		if (!ah->caldata->paprd_done)
			ieee80211_queue_work(sc->hw, &sc->paprd_work);
		else if (!ah->paprd_table_write_done)
			ath_paprd_activate(sc);
	}
}

void ath_start_ani(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	unsigned long timestamp = jiffies_to_msecs(jiffies);

	if (common->disable_ani ||
	    !test_bit(SC_OP_ANI_RUN, &sc->sc_flags) ||
	    (sc->hw->conf.flags & IEEE80211_CONF_OFFCHANNEL))
		return;

	common->ani.longcal_timer = timestamp;
	common->ani.shortcal_timer = timestamp;
	common->ani.checkani_timer = timestamp;

	ath_dbg(common, ANI, "Starting ANI\n");
	mod_timer(&common->ani.timer,
		  jiffies + msecs_to_jiffies((u32)ah->config.ani_poll_interval));
}

void ath_stop_ani(struct ath_softc *sc)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	ath_dbg(common, ANI, "Stopping ANI\n");
	del_timer_sync(&common->ani.timer);
}

void ath_check_ani(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_beacon_config *cur_conf = &sc->cur_beacon_conf;

	/*
	 * Check for the various conditions in which ANI has to
	 * be stopped.
	 */
	if (ah->opmode == NL80211_IFTYPE_ADHOC) {
		if (!cur_conf->enable_beacon)
			goto stop_ani;
	} else if (ah->opmode == NL80211_IFTYPE_AP) {
		if (!cur_conf->enable_beacon) {
			/*
			 * Disable ANI only when there are no
			 * associated stations.
			 */
			if (!test_bit(SC_OP_PRIM_STA_VIF, &sc->sc_flags))
				goto stop_ani;
		}
	} else if (ah->opmode == NL80211_IFTYPE_STATION) {
		if (!test_bit(SC_OP_PRIM_STA_VIF, &sc->sc_flags))
			goto stop_ani;
	}

	if (!test_bit(SC_OP_ANI_RUN, &sc->sc_flags)) {
		set_bit(SC_OP_ANI_RUN, &sc->sc_flags);
		ath_start_ani(sc);
	}

	return;

stop_ani:
	clear_bit(SC_OP_ANI_RUN, &sc->sc_flags);
	ath_stop_ani(sc);
}

void ath_update_survey_nf(struct ath_softc *sc, int channel)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath9k_channel *chan = &ah->channels[channel];
	struct survey_info *survey = &sc->survey[channel];

	if (chan->noisefloor) {
		survey->filled |= SURVEY_INFO_NOISE_DBM;
		survey->noise = ath9k_hw_getchan_noise(ah, chan);
	}
}

/*
 * Updates the survey statistics and returns the busy time since last
 * update in %, if the measurement duration was long enough for the
 * result to be useful, -1 otherwise.
 */
int ath_update_survey_stats(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	int pos = ah->curchan - &ah->channels[0];
	struct survey_info *survey = &sc->survey[pos];
	struct ath_cycle_counters *cc = &common->cc_survey;
	unsigned int div = common->clockrate * 1000;
	int ret = 0;

	if (!ah->curchan)
		return -1;

	if (ah->power_mode == ATH9K_PM_AWAKE)
		ath_hw_cycle_counters_update(common);

	if (cc->cycles > 0) {
		survey->filled |= SURVEY_INFO_CHANNEL_TIME |
			SURVEY_INFO_CHANNEL_TIME_BUSY |
			SURVEY_INFO_CHANNEL_TIME_RX |
			SURVEY_INFO_CHANNEL_TIME_TX;
		survey->channel_time += cc->cycles / div;
		survey->channel_time_busy += cc->rx_busy / div;
		survey->channel_time_rx += cc->rx_frame / div;
		survey->channel_time_tx += cc->tx_frame / div;
	}

	if (cc->cycles < div)
		return -1;

	if (cc->cycles > 0)
		ret = cc->rx_busy * 100 / cc->cycles;

	memset(cc, 0, sizeof(*cc));

	ath_update_survey_nf(sc, pos);

	return ret;
}
