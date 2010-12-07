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

#include <linux/nl80211.h>
#include "ath9k.h"
#include "btcoex.h"

static void ath_update_txpow(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;

	if (sc->curtxpow != sc->config.txpowlimit) {
		ath9k_hw_set_txpowerlimit(ah, sc->config.txpowlimit, false);
		/* read back in case value is clamped */
		sc->curtxpow = ath9k_hw_regulatory(ah)->power_limit;
	}
}

static u8 parse_mpdudensity(u8 mpdudensity)
{
	/*
	 * 802.11n D2.0 defined values for "Minimum MPDU Start Spacing":
	 *   0 for no restriction
	 *   1 for 1/4 us
	 *   2 for 1/2 us
	 *   3 for 1 us
	 *   4 for 2 us
	 *   5 for 4 us
	 *   6 for 8 us
	 *   7 for 16 us
	 */
	switch (mpdudensity) {
	case 0:
		return 0;
	case 1:
	case 2:
	case 3:
		/* Our lower layer calculations limit our precision to
		   1 microsecond */
		return 1;
	case 4:
		return 2;
	case 5:
		return 4;
	case 6:
		return 8;
	case 7:
		return 16;
	default:
		return 0;
	}
}

static struct ath9k_channel *ath_get_curchannel(struct ath_softc *sc,
						struct ieee80211_hw *hw)
{
	struct ieee80211_channel *curchan = hw->conf.channel;
	struct ath9k_channel *channel;
	u8 chan_idx;

	chan_idx = curchan->hw_value;
	channel = &sc->sc_ah->channels[chan_idx];
	ath9k_update_ichannel(sc, hw, channel);
	return channel;
}

bool ath9k_setpower(struct ath_softc *sc, enum ath9k_power_mode mode)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&sc->sc_pm_lock, flags);
	ret = ath9k_hw_setpower(sc->sc_ah, mode);
	spin_unlock_irqrestore(&sc->sc_pm_lock, flags);

	return ret;
}

void ath9k_ps_wakeup(struct ath_softc *sc)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	unsigned long flags;
	enum ath9k_power_mode power_mode;

	spin_lock_irqsave(&sc->sc_pm_lock, flags);
	if (++sc->ps_usecount != 1)
		goto unlock;

	power_mode = sc->sc_ah->power_mode;
	ath9k_hw_setpower(sc->sc_ah, ATH9K_PM_AWAKE);

	/*
	 * While the hardware is asleep, the cycle counters contain no
	 * useful data. Better clear them now so that they don't mess up
	 * survey data results.
	 */
	if (power_mode != ATH9K_PM_AWAKE) {
		spin_lock(&common->cc_lock);
		ath_hw_cycle_counters_update(common);
		memset(&common->cc_survey, 0, sizeof(common->cc_survey));
		spin_unlock(&common->cc_lock);
	}

 unlock:
	spin_unlock_irqrestore(&sc->sc_pm_lock, flags);
}

void ath9k_ps_restore(struct ath_softc *sc)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	unsigned long flags;

	spin_lock_irqsave(&sc->sc_pm_lock, flags);
	if (--sc->ps_usecount != 0)
		goto unlock;

	spin_lock(&common->cc_lock);
	ath_hw_cycle_counters_update(common);
	spin_unlock(&common->cc_lock);

	if (sc->ps_idle)
		ath9k_hw_setpower(sc->sc_ah, ATH9K_PM_FULL_SLEEP);
	else if (sc->ps_enabled &&
		 !(sc->ps_flags & (PS_WAIT_FOR_BEACON |
			      PS_WAIT_FOR_CAB |
			      PS_WAIT_FOR_PSPOLL_DATA |
			      PS_WAIT_FOR_TX_ACK)))
		ath9k_hw_setpower(sc->sc_ah, ATH9K_PM_NETWORK_SLEEP);

 unlock:
	spin_unlock_irqrestore(&sc->sc_pm_lock, flags);
}

static void ath_start_ani(struct ath_common *common)
{
	struct ath_hw *ah = common->ah;
	unsigned long timestamp = jiffies_to_msecs(jiffies);
	struct ath_softc *sc = (struct ath_softc *) common->priv;

	if (!(sc->sc_flags & SC_OP_ANI_RUN))
		return;

	if (sc->sc_flags & SC_OP_OFFCHANNEL)
		return;

	common->ani.longcal_timer = timestamp;
	common->ani.shortcal_timer = timestamp;
	common->ani.checkani_timer = timestamp;

	mod_timer(&common->ani.timer,
		  jiffies +
			msecs_to_jiffies((u32)ah->config.ani_poll_interval));
}

static void ath_update_survey_nf(struct ath_softc *sc, int channel)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath9k_channel *chan = &ah->channels[channel];
	struct survey_info *survey = &sc->survey[channel];

	if (chan->noisefloor) {
		survey->filled |= SURVEY_INFO_NOISE_DBM;
		survey->noise = chan->noisefloor;
	}
}

static void ath_update_survey_stats(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	int pos = ah->curchan - &ah->channels[0];
	struct survey_info *survey = &sc->survey[pos];
	struct ath_cycle_counters *cc = &common->cc_survey;
	unsigned int div = common->clockrate * 1000;

	if (!ah->curchan)
		return;

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
	memset(cc, 0, sizeof(*cc));

	ath_update_survey_nf(sc, pos);
}

/*
 * Set/change channels.  If the channel is really being changed, it's done
 * by reseting the chip.  To accomplish this we must first cleanup any pending
 * DMA, then restart stuff.
*/
int ath_set_channel(struct ath_softc *sc, struct ieee80211_hw *hw,
		    struct ath9k_channel *hchan)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_conf *conf = &common->hw->conf;
	bool fastcc = true, stopped;
	struct ieee80211_channel *channel = hw->conf.channel;
	struct ath9k_hw_cal_data *caldata = NULL;
	int r;

	if (sc->sc_flags & SC_OP_INVALID)
		return -EIO;

	del_timer_sync(&common->ani.timer);
	cancel_work_sync(&sc->paprd_work);
	cancel_work_sync(&sc->hw_check_work);
	cancel_delayed_work_sync(&sc->tx_complete_work);

	ath9k_ps_wakeup(sc);

	spin_lock_bh(&sc->sc_pcu_lock);

	/*
	 * This is only performed if the channel settings have
	 * actually changed.
	 *
	 * To switch channels clear any pending DMA operations;
	 * wait long enough for the RX fifo to drain, reset the
	 * hardware at the new frequency, and then re-enable
	 * the relevant bits of the h/w.
	 */
	ath9k_hw_disable_interrupts(ah);
	ath_drain_all_txq(sc, false);

	stopped = ath_stoprecv(sc);

	/* XXX: do not flush receive queue here. We don't want
	 * to flush data frames already in queue because of
	 * changing channel. */

	if (!stopped || !(sc->sc_flags & SC_OP_OFFCHANNEL))
		fastcc = false;

	if (!(sc->sc_flags & SC_OP_OFFCHANNEL))
		caldata = &aphy->caldata;

	ath_dbg(common, ATH_DBG_CONFIG,
		"(%u MHz) -> (%u MHz), conf_is_ht40: %d fastcc: %d\n",
		sc->sc_ah->curchan->channel,
		channel->center_freq, conf_is_ht40(conf),
		fastcc);

	r = ath9k_hw_reset(ah, hchan, caldata, fastcc);
	if (r) {
		ath_err(common,
			"Unable to reset channel (%u MHz), reset status %d\n",
			channel->center_freq, r);
		goto ps_restore;
	}

	if (ath_startrecv(sc) != 0) {
		ath_err(common, "Unable to restart recv logic\n");
		r = -EIO;
		goto ps_restore;
	}

	ath_update_txpow(sc);
	ath9k_hw_set_interrupts(ah, ah->imask);

	if (!(sc->sc_flags & (SC_OP_OFFCHANNEL))) {
		ath_beacon_config(sc, NULL);
		ieee80211_queue_delayed_work(sc->hw, &sc->tx_complete_work, 0);
		ath_start_ani(common);
	}

 ps_restore:
	spin_unlock_bh(&sc->sc_pcu_lock);

	ath9k_ps_restore(sc);
	return r;
}

static void ath_paprd_activate(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath9k_hw_cal_data *caldata = ah->caldata;
	struct ath_common *common = ath9k_hw_common(ah);
	int chain;

	if (!caldata || !caldata->paprd_done)
		return;

	ath9k_ps_wakeup(sc);
	ar9003_paprd_enable(ah, false);
	for (chain = 0; chain < AR9300_MAX_CHAINS; chain++) {
		if (!(common->tx_chainmask & BIT(chain)))
			continue;

		ar9003_paprd_populate_single_table(ah, caldata, chain);
	}

	ar9003_paprd_enable(ah, true);
	ath9k_ps_restore(sc);
}

void ath_paprd_calibrate(struct work_struct *work)
{
	struct ath_softc *sc = container_of(work, struct ath_softc, paprd_work);
	struct ieee80211_hw *hw = sc->hw;
	struct ath_hw *ah = sc->sc_ah;
	struct ieee80211_hdr *hdr;
	struct sk_buff *skb = NULL;
	struct ieee80211_tx_info *tx_info;
	int band = hw->conf.channel->band;
	struct ieee80211_supported_band *sband = &sc->sbands[band];
	struct ath_tx_control txctl;
	struct ath9k_hw_cal_data *caldata = ah->caldata;
	struct ath_common *common = ath9k_hw_common(ah);
	int ftype;
	int chain_ok = 0;
	int chain;
	int len = 1800;
	int time_left;
	int i;

	if (!caldata)
		return;

	skb = alloc_skb(len, GFP_KERNEL);
	if (!skb)
		return;

	tx_info = IEEE80211_SKB_CB(skb);

	skb_put(skb, len);
	memset(skb->data, 0, len);
	hdr = (struct ieee80211_hdr *)skb->data;
	ftype = IEEE80211_FTYPE_DATA | IEEE80211_STYPE_NULLFUNC;
	hdr->frame_control = cpu_to_le16(ftype);
	hdr->duration_id = cpu_to_le16(10);
	memcpy(hdr->addr1, hw->wiphy->perm_addr, ETH_ALEN);
	memcpy(hdr->addr2, hw->wiphy->perm_addr, ETH_ALEN);
	memcpy(hdr->addr3, hw->wiphy->perm_addr, ETH_ALEN);

	memset(&txctl, 0, sizeof(txctl));
	txctl.txq = sc->tx.txq_map[WME_AC_BE];

	ath9k_ps_wakeup(sc);
	ar9003_paprd_init_table(ah);
	for (chain = 0; chain < AR9300_MAX_CHAINS; chain++) {
		if (!(common->tx_chainmask & BIT(chain)))
			continue;

		chain_ok = 0;
		memset(tx_info, 0, sizeof(*tx_info));
		tx_info->band = band;

		for (i = 0; i < 4; i++) {
			tx_info->control.rates[i].idx = sband->n_bitrates - 1;
			tx_info->control.rates[i].count = 6;
		}

		init_completion(&sc->paprd_complete);
		sc->paprd_pending = true;
		ar9003_paprd_setup_gain_table(ah, chain);
		txctl.paprd = BIT(chain);
		if (ath_tx_start(hw, skb, &txctl) != 0)
			break;

		time_left = wait_for_completion_timeout(&sc->paprd_complete,
				msecs_to_jiffies(ATH_PAPRD_TIMEOUT));
		sc->paprd_pending = false;
		if (!time_left) {
			ath_dbg(ath9k_hw_common(ah), ATH_DBG_CALIBRATE,
				"Timeout waiting for paprd training on TX chain %d\n",
				chain);
			goto fail_paprd;
		}

		if (!ar9003_paprd_is_done(ah))
			break;

		if (ar9003_paprd_create_curve(ah, caldata, chain) != 0)
			break;

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
 *  This routine performs the periodic noise floor calibration function
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
		ath_dbg(common, ATH_DBG_ANI, "longcal @%lu\n", jiffies);
		common->ani.longcal_timer = timestamp;
	}

	/* Short calibration applies only while caldone is false */
	if (!common->ani.caldone) {
		if ((timestamp - common->ani.shortcal_timer) >= short_cal_interval) {
			shortcal = true;
			ath_dbg(common, ATH_DBG_ANI,
				"shortcal @%lu\n", jiffies);
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
	if ((timestamp - common->ani.checkani_timer) >=
	     ah->config.ani_poll_interval) {
		aniflag = true;
		common->ani.checkani_timer = timestamp;
	}

	/* Skip all processing if there's nothing to do. */
	if (longcal || shortcal || aniflag) {
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
				ath9k_hw_calibrate(ah,
						   ah->curchan,
						   common->rx_chainmask,
						   longcal);
		}
	}

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
		else
			ath_paprd_activate(sc);
	}
}

/*
 * Update tx/rx chainmask. For legacy association,
 * hard code chainmask to 1x1, for 11n association, use
 * the chainmask configuration, for bt coexistence, use
 * the chainmask configuration even in legacy mode.
 */
void ath_update_chainmask(struct ath_softc *sc, int is_ht)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);

	if ((sc->sc_flags & SC_OP_OFFCHANNEL) || is_ht ||
	    (ah->btcoex_hw.scheme != ATH_BTCOEX_CFG_NONE)) {
		common->tx_chainmask = ah->caps.tx_chainmask;
		common->rx_chainmask = ah->caps.rx_chainmask;
	} else {
		common->tx_chainmask = 1;
		common->rx_chainmask = 1;
	}

	ath_dbg(common, ATH_DBG_CONFIG,
		"tx chmask: %d, rx chmask: %d\n",
		common->tx_chainmask,
		common->rx_chainmask);
}

static void ath_node_attach(struct ath_softc *sc, struct ieee80211_sta *sta)
{
	struct ath_node *an;
	struct ath_hw *ah = sc->sc_ah;
	an = (struct ath_node *)sta->drv_priv;

	if ((ah->caps.hw_caps) & ATH9K_HW_CAP_APM)
		sc->sc_flags |= SC_OP_ENABLE_APM;

	if (sc->sc_flags & SC_OP_TXAGGR) {
		ath_tx_node_init(sc, an);
		an->maxampdu = 1 << (IEEE80211_HT_MAX_AMPDU_FACTOR +
				     sta->ht_cap.ampdu_factor);
		an->mpdudensity = parse_mpdudensity(sta->ht_cap.ampdu_density);
	}
}

static void ath_node_detach(struct ath_softc *sc, struct ieee80211_sta *sta)
{
	struct ath_node *an = (struct ath_node *)sta->drv_priv;

	if (sc->sc_flags & SC_OP_TXAGGR)
		ath_tx_node_cleanup(sc, an);
}

void ath_hw_check(struct work_struct *work)
{
	struct ath_softc *sc = container_of(work, struct ath_softc, hw_check_work);
	int i;

	ath9k_ps_wakeup(sc);

	for (i = 0; i < 3; i++) {
		if (ath9k_hw_check_alive(sc->sc_ah))
			goto out;

		msleep(1);
	}
	ath_reset(sc, true);

out:
	ath9k_ps_restore(sc);
}

void ath9k_tasklet(unsigned long data)
{
	struct ath_softc *sc = (struct ath_softc *)data;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);

	u32 status = sc->intrstatus;
	u32 rxmask;

	ath9k_ps_wakeup(sc);

	if (status & ATH9K_INT_FATAL) {
		ath_reset(sc, true);
		ath9k_ps_restore(sc);
		return;
	}

	spin_lock_bh(&sc->sc_pcu_lock);

	if (!ath9k_hw_check_alive(ah))
		ieee80211_queue_work(sc->hw, &sc->hw_check_work);

	if (ah->caps.hw_caps & ATH9K_HW_CAP_EDMA)
		rxmask = (ATH9K_INT_RXHP | ATH9K_INT_RXLP | ATH9K_INT_RXEOL |
			  ATH9K_INT_RXORN);
	else
		rxmask = (ATH9K_INT_RX | ATH9K_INT_RXEOL | ATH9K_INT_RXORN);

	if (status & rxmask) {
		/* Check for high priority Rx first */
		if ((ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) &&
		    (status & ATH9K_INT_RXHP))
			ath_rx_tasklet(sc, 0, true);

		ath_rx_tasklet(sc, 0, false);
	}

	if (status & ATH9K_INT_TX) {
		if (ah->caps.hw_caps & ATH9K_HW_CAP_EDMA)
			ath_tx_edma_tasklet(sc);
		else
			ath_tx_tasklet(sc);
	}

	if ((status & ATH9K_INT_TSFOOR) && sc->ps_enabled) {
		/*
		 * TSF sync does not look correct; remain awake to sync with
		 * the next Beacon.
		 */
		ath_dbg(common, ATH_DBG_PS,
			"TSFOOR - Sync with next Beacon\n");
		sc->ps_flags |= PS_WAIT_FOR_BEACON | PS_BEACON_SYNC;
	}

	if (ah->btcoex_hw.scheme == ATH_BTCOEX_CFG_3WIRE)
		if (status & ATH9K_INT_GENTIMER)
			ath_gen_timer_isr(sc->sc_ah);

	/* re-enable hardware interrupt */
	ath9k_hw_enable_interrupts(ah);

	spin_unlock_bh(&sc->sc_pcu_lock);
	ath9k_ps_restore(sc);
}

irqreturn_t ath_isr(int irq, void *dev)
{
#define SCHED_INTR (				\
		ATH9K_INT_FATAL |		\
		ATH9K_INT_RXORN |		\
		ATH9K_INT_RXEOL |		\
		ATH9K_INT_RX |			\
		ATH9K_INT_RXLP |		\
		ATH9K_INT_RXHP |		\
		ATH9K_INT_TX |			\
		ATH9K_INT_BMISS |		\
		ATH9K_INT_CST |			\
		ATH9K_INT_TSFOOR |		\
		ATH9K_INT_GENTIMER)

	struct ath_softc *sc = dev;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	enum ath9k_int status;
	bool sched = false;

	/*
	 * The hardware is not ready/present, don't
	 * touch anything. Note this can happen early
	 * on if the IRQ is shared.
	 */
	if (sc->sc_flags & SC_OP_INVALID)
		return IRQ_NONE;


	/* shared irq, not for us */

	if (!ath9k_hw_intrpend(ah))
		return IRQ_NONE;

	/*
	 * Figure out the reason(s) for the interrupt.  Note
	 * that the hal returns a pseudo-ISR that may include
	 * bits we haven't explicitly enabled so we mask the
	 * value to insure we only process bits we requested.
	 */
	ath9k_hw_getisr(ah, &status);	/* NB: clears ISR too */
	status &= ah->imask;	/* discard unasked-for bits */

	/*
	 * If there are no status bits set, then this interrupt was not
	 * for me (should have been caught above).
	 */
	if (!status)
		return IRQ_NONE;

	/* Cache the status */
	sc->intrstatus = status;

	if (status & SCHED_INTR)
		sched = true;

	/*
	 * If a FATAL or RXORN interrupt is received, we have to reset the
	 * chip immediately.
	 */
	if ((status & ATH9K_INT_FATAL) || ((status & ATH9K_INT_RXORN) &&
	    !(ah->caps.hw_caps & ATH9K_HW_CAP_EDMA)))
		goto chip_reset;

	if ((ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) &&
	    (status & ATH9K_INT_BB_WATCHDOG)) {

		spin_lock(&common->cc_lock);
		ath_hw_cycle_counters_update(common);
		ar9003_hw_bb_watchdog_dbg_info(ah);
		spin_unlock(&common->cc_lock);

		goto chip_reset;
	}

	if (status & ATH9K_INT_SWBA)
		tasklet_schedule(&sc->bcon_tasklet);

	if (status & ATH9K_INT_TXURN)
		ath9k_hw_updatetxtriglevel(ah, true);

	if (ah->caps.hw_caps & ATH9K_HW_CAP_EDMA) {
		if (status & ATH9K_INT_RXEOL) {
			ah->imask &= ~(ATH9K_INT_RXEOL | ATH9K_INT_RXORN);
			ath9k_hw_set_interrupts(ah, ah->imask);
		}
	}

	if (status & ATH9K_INT_MIB) {
		/*
		 * Disable interrupts until we service the MIB
		 * interrupt; otherwise it will continue to
		 * fire.
		 */
		ath9k_hw_disable_interrupts(ah);
		/*
		 * Let the hal handle the event. We assume
		 * it will clear whatever condition caused
		 * the interrupt.
		 */
		spin_lock(&common->cc_lock);
		ath9k_hw_proc_mib_event(ah);
		spin_unlock(&common->cc_lock);
		ath9k_hw_enable_interrupts(ah);
	}

	if (!(ah->caps.hw_caps & ATH9K_HW_CAP_AUTOSLEEP))
		if (status & ATH9K_INT_TIM_TIMER) {
			if (ATH_DBG_WARN_ON_ONCE(sc->ps_idle))
				goto chip_reset;
			/* Clear RxAbort bit so that we can
			 * receive frames */
			ath9k_setpower(sc, ATH9K_PM_AWAKE);
			ath9k_hw_setrxabort(sc->sc_ah, 0);
			sc->ps_flags |= PS_WAIT_FOR_BEACON;
		}

chip_reset:

	ath_debug_stat_interrupt(sc, status);

	if (sched) {
		/* turn off every interrupt */
		ath9k_hw_disable_interrupts(ah);
		tasklet_schedule(&sc->intr_tq);
	}

	return IRQ_HANDLED;

#undef SCHED_INTR
}

static u32 ath_get_extchanmode(struct ath_softc *sc,
			       struct ieee80211_channel *chan,
			       enum nl80211_channel_type channel_type)
{
	u32 chanmode = 0;

	switch (chan->band) {
	case IEEE80211_BAND_2GHZ:
		switch(channel_type) {
		case NL80211_CHAN_NO_HT:
		case NL80211_CHAN_HT20:
			chanmode = CHANNEL_G_HT20;
			break;
		case NL80211_CHAN_HT40PLUS:
			chanmode = CHANNEL_G_HT40PLUS;
			break;
		case NL80211_CHAN_HT40MINUS:
			chanmode = CHANNEL_G_HT40MINUS;
			break;
		}
		break;
	case IEEE80211_BAND_5GHZ:
		switch(channel_type) {
		case NL80211_CHAN_NO_HT:
		case NL80211_CHAN_HT20:
			chanmode = CHANNEL_A_HT20;
			break;
		case NL80211_CHAN_HT40PLUS:
			chanmode = CHANNEL_A_HT40PLUS;
			break;
		case NL80211_CHAN_HT40MINUS:
			chanmode = CHANNEL_A_HT40MINUS;
			break;
		}
		break;
	default:
		break;
	}

	return chanmode;
}

static void ath9k_bss_assoc_info(struct ath_softc *sc,
				 struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *bss_conf)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);

	if (bss_conf->assoc) {
		ath_dbg(common, ATH_DBG_CONFIG,
			"Bss Info ASSOC %d, bssid: %pM\n",
			bss_conf->aid, common->curbssid);

		/* New association, store aid */
		common->curaid = bss_conf->aid;
		ath9k_hw_write_associd(ah);

		/*
		 * Request a re-configuration of Beacon related timers
		 * on the receipt of the first Beacon frame (i.e.,
		 * after time sync with the AP).
		 */
		sc->ps_flags |= PS_BEACON_SYNC;

		/* Configure the beacon */
		ath_beacon_config(sc, vif);

		/* Reset rssi stats */
		aphy->last_rssi = ATH_RSSI_DUMMY_MARKER;
		sc->sc_ah->stats.avgbrssi = ATH_RSSI_DUMMY_MARKER;

		sc->sc_flags |= SC_OP_ANI_RUN;
		ath_start_ani(common);
	} else {
		ath_dbg(common, ATH_DBG_CONFIG, "Bss Info DISASSOC\n");
		common->curaid = 0;
		/* Stop ANI */
		sc->sc_flags &= ~SC_OP_ANI_RUN;
		del_timer_sync(&common->ani.timer);
	}
}

void ath_radio_enable(struct ath_softc *sc, struct ieee80211_hw *hw)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_channel *channel = hw->conf.channel;
	int r;

	ath9k_ps_wakeup(sc);
	spin_lock_bh(&sc->sc_pcu_lock);

	ath9k_hw_configpcipowersave(ah, 0, 0);

	if (!ah->curchan)
		ah->curchan = ath_get_curchannel(sc, sc->hw);

	r = ath9k_hw_reset(ah, ah->curchan, ah->caldata, false);
	if (r) {
		ath_err(common,
			"Unable to reset channel (%u MHz), reset status %d\n",
			channel->center_freq, r);
	}

	ath_update_txpow(sc);
	if (ath_startrecv(sc) != 0) {
		ath_err(common, "Unable to restart recv logic\n");
		spin_unlock_bh(&sc->sc_pcu_lock);
		return;
	}
	if (sc->sc_flags & SC_OP_BEACONS)
		ath_beacon_config(sc, NULL);	/* restart beacons */

	/* Re-Enable  interrupts */
	ath9k_hw_set_interrupts(ah, ah->imask);

	/* Enable LED */
	ath9k_hw_cfg_output(ah, ah->led_pin,
			    AR_GPIO_OUTPUT_MUX_AS_OUTPUT);
	ath9k_hw_set_gpio(ah, ah->led_pin, 0);

	ieee80211_wake_queues(hw);
	spin_unlock_bh(&sc->sc_pcu_lock);

	ath9k_ps_restore(sc);
}

void ath_radio_disable(struct ath_softc *sc, struct ieee80211_hw *hw)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ieee80211_channel *channel = hw->conf.channel;
	int r;

	ath9k_ps_wakeup(sc);
	spin_lock_bh(&sc->sc_pcu_lock);

	ieee80211_stop_queues(hw);

	/*
	 * Keep the LED on when the radio is disabled
	 * during idle unassociated state.
	 */
	if (!sc->ps_idle) {
		ath9k_hw_set_gpio(ah, ah->led_pin, 1);
		ath9k_hw_cfg_gpio_input(ah, ah->led_pin);
	}

	/* Disable interrupts */
	ath9k_hw_disable_interrupts(ah);

	ath_drain_all_txq(sc, false);	/* clear pending tx frames */

	ath_stoprecv(sc);		/* turn off frame recv */
	ath_flushrecv(sc);		/* flush recv queue */

	if (!ah->curchan)
		ah->curchan = ath_get_curchannel(sc, hw);

	r = ath9k_hw_reset(ah, ah->curchan, ah->caldata, false);
	if (r) {
		ath_err(ath9k_hw_common(sc->sc_ah),
			"Unable to reset channel (%u MHz), reset status %d\n",
			channel->center_freq, r);
	}

	ath9k_hw_phy_disable(ah);

	ath9k_hw_configpcipowersave(ah, 1, 1);

	spin_unlock_bh(&sc->sc_pcu_lock);
	ath9k_ps_restore(sc);

	ath9k_setpower(sc, ATH9K_PM_FULL_SLEEP);
}

int ath_reset(struct ath_softc *sc, bool retry_tx)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_hw *hw = sc->hw;
	int r;

	/* Stop ANI */
	del_timer_sync(&common->ani.timer);

	spin_lock_bh(&sc->sc_pcu_lock);

	ieee80211_stop_queues(hw);

	ath9k_hw_disable_interrupts(ah);
	ath_drain_all_txq(sc, retry_tx);

	ath_stoprecv(sc);
	ath_flushrecv(sc);

	r = ath9k_hw_reset(ah, sc->sc_ah->curchan, ah->caldata, false);
	if (r)
		ath_err(common,
			"Unable to reset hardware; reset status %d\n", r);

	if (ath_startrecv(sc) != 0)
		ath_err(common, "Unable to start recv logic\n");

	/*
	 * We may be doing a reset in response to a request
	 * that changes the channel so update any state that
	 * might change as a result.
	 */
	ath_update_txpow(sc);

	if ((sc->sc_flags & SC_OP_BEACONS) || !(sc->sc_flags & (SC_OP_OFFCHANNEL)))
		ath_beacon_config(sc, NULL);	/* restart beacons */

	ath9k_hw_set_interrupts(ah, ah->imask);

	if (retry_tx) {
		int i;
		for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++) {
			if (ATH_TXQ_SETUP(sc, i)) {
				spin_lock_bh(&sc->tx.txq[i].axq_lock);
				ath_txq_schedule(sc, &sc->tx.txq[i]);
				spin_unlock_bh(&sc->tx.txq[i].axq_lock);
			}
		}
	}

	ieee80211_wake_queues(hw);
	spin_unlock_bh(&sc->sc_pcu_lock);

	/* Start ANI */
	ath_start_ani(common);

	return r;
}

/* XXX: Remove me once we don't depend on ath9k_channel for all
 * this redundant data */
void ath9k_update_ichannel(struct ath_softc *sc, struct ieee80211_hw *hw,
			   struct ath9k_channel *ichan)
{
	struct ieee80211_channel *chan = hw->conf.channel;
	struct ieee80211_conf *conf = &hw->conf;

	ichan->channel = chan->center_freq;
	ichan->chan = chan;

	if (chan->band == IEEE80211_BAND_2GHZ) {
		ichan->chanmode = CHANNEL_G;
		ichan->channelFlags = CHANNEL_2GHZ | CHANNEL_OFDM | CHANNEL_G;
	} else {
		ichan->chanmode = CHANNEL_A;
		ichan->channelFlags = CHANNEL_5GHZ | CHANNEL_OFDM;
	}

	if (conf_is_ht(conf))
		ichan->chanmode = ath_get_extchanmode(sc, chan,
					    conf->channel_type);
}

/**********************/
/* mac80211 callbacks */
/**********************/

static int ath9k_start(struct ieee80211_hw *hw)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_channel *curchan = hw->conf.channel;
	struct ath9k_channel *init_channel;
	int r;

	ath_dbg(common, ATH_DBG_CONFIG,
		"Starting driver with initial channel: %d MHz\n",
		curchan->center_freq);

	mutex_lock(&sc->mutex);

	if (ath9k_wiphy_started(sc)) {
		if (sc->chan_idx == curchan->hw_value) {
			/*
			 * Already on the operational channel, the new wiphy
			 * can be marked active.
			 */
			aphy->state = ATH_WIPHY_ACTIVE;
			ieee80211_wake_queues(hw);
		} else {
			/*
			 * Another wiphy is on another channel, start the new
			 * wiphy in paused state.
			 */
			aphy->state = ATH_WIPHY_PAUSED;
			ieee80211_stop_queues(hw);
		}
		mutex_unlock(&sc->mutex);
		return 0;
	}
	aphy->state = ATH_WIPHY_ACTIVE;

	/* setup initial channel */

	sc->chan_idx = curchan->hw_value;

	init_channel = ath_get_curchannel(sc, hw);

	/* Reset SERDES registers */
	ath9k_hw_configpcipowersave(ah, 0, 0);

	/*
	 * The basic interface to setting the hardware in a good
	 * state is ``reset''.  On return the hardware is known to
	 * be powered up and with interrupts disabled.  This must
	 * be followed by initialization of the appropriate bits
	 * and then setup of the interrupt mask.
	 */
	spin_lock_bh(&sc->sc_pcu_lock);
	r = ath9k_hw_reset(ah, init_channel, ah->caldata, false);
	if (r) {
		ath_err(common,
			"Unable to reset hardware; reset status %d (freq %u MHz)\n",
			r, curchan->center_freq);
		spin_unlock_bh(&sc->sc_pcu_lock);
		goto mutex_unlock;
	}

	/*
	 * This is needed only to setup initial state
	 * but it's best done after a reset.
	 */
	ath_update_txpow(sc);

	/*
	 * Setup the hardware after reset:
	 * The receive engine is set going.
	 * Frame transmit is handled entirely
	 * in the frame output path; there's nothing to do
	 * here except setup the interrupt mask.
	 */
	if (ath_startrecv(sc) != 0) {
		ath_err(common, "Unable to start recv logic\n");
		r = -EIO;
		spin_unlock_bh(&sc->sc_pcu_lock);
		goto mutex_unlock;
	}
	spin_unlock_bh(&sc->sc_pcu_lock);

	/* Setup our intr mask. */
	ah->imask = ATH9K_INT_TX | ATH9K_INT_RXEOL |
		    ATH9K_INT_RXORN | ATH9K_INT_FATAL |
		    ATH9K_INT_GLOBAL;

	if (ah->caps.hw_caps & ATH9K_HW_CAP_EDMA)
		ah->imask |= ATH9K_INT_RXHP |
			     ATH9K_INT_RXLP |
			     ATH9K_INT_BB_WATCHDOG;
	else
		ah->imask |= ATH9K_INT_RX;

	ah->imask |= ATH9K_INT_GTT;

	if (ah->caps.hw_caps & ATH9K_HW_CAP_HT)
		ah->imask |= ATH9K_INT_CST;

	sc->sc_flags &= ~SC_OP_INVALID;
	sc->sc_ah->is_monitoring = false;

	/* Disable BMISS interrupt when we're not associated */
	ah->imask &= ~(ATH9K_INT_SWBA | ATH9K_INT_BMISS);
	ath9k_hw_set_interrupts(ah, ah->imask);

	ieee80211_wake_queues(hw);

	ieee80211_queue_delayed_work(sc->hw, &sc->tx_complete_work, 0);

	if ((ah->btcoex_hw.scheme != ATH_BTCOEX_CFG_NONE) &&
	    !ah->btcoex_hw.enabled) {
		ath9k_hw_btcoex_set_weight(ah, AR_BT_COEX_WGHT,
					   AR_STOMP_LOW_WLAN_WGHT);
		ath9k_hw_btcoex_enable(ah);

		if (common->bus_ops->bt_coex_prep)
			common->bus_ops->bt_coex_prep(common);
		if (ah->btcoex_hw.scheme == ATH_BTCOEX_CFG_3WIRE)
			ath9k_btcoex_timer_resume(sc);
	}

	pm_qos_update_request(&sc->pm_qos_req, 55);

	if (ah->caps.pcie_lcr_extsync_en && common->bus_ops->extn_synch_en)
		common->bus_ops->extn_synch_en(common);

mutex_unlock:
	mutex_unlock(&sc->mutex);

	return r;
}

static int ath9k_tx(struct ieee80211_hw *hw,
		    struct sk_buff *skb)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_tx_control txctl;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;

	if (aphy->state != ATH_WIPHY_ACTIVE && aphy->state != ATH_WIPHY_SCAN) {
		ath_dbg(common, ATH_DBG_XMIT,
			"ath9k: %s: TX in unexpected wiphy state %d\n",
			wiphy_name(hw->wiphy), aphy->state);
		goto exit;
	}

	if (sc->ps_enabled) {
		/*
		 * mac80211 does not set PM field for normal data frames, so we
		 * need to update that based on the current PS mode.
		 */
		if (ieee80211_is_data(hdr->frame_control) &&
		    !ieee80211_is_nullfunc(hdr->frame_control) &&
		    !ieee80211_has_pm(hdr->frame_control)) {
			ath_dbg(common, ATH_DBG_PS,
				"Add PM=1 for a TX frame while in PS mode\n");
			hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PM);
		}
	}

	if (unlikely(sc->sc_ah->power_mode != ATH9K_PM_AWAKE)) {
		/*
		 * We are using PS-Poll and mac80211 can request TX while in
		 * power save mode. Need to wake up hardware for the TX to be
		 * completed and if needed, also for RX of buffered frames.
		 */
		ath9k_ps_wakeup(sc);
		if (!(sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_AUTOSLEEP))
			ath9k_hw_setrxabort(sc->sc_ah, 0);
		if (ieee80211_is_pspoll(hdr->frame_control)) {
			ath_dbg(common, ATH_DBG_PS,
				"Sending PS-Poll to pick a buffered frame\n");
			sc->ps_flags |= PS_WAIT_FOR_PSPOLL_DATA;
		} else {
			ath_dbg(common, ATH_DBG_PS,
				"Wake up to complete TX\n");
			sc->ps_flags |= PS_WAIT_FOR_TX_ACK;
		}
		/*
		 * The actual restore operation will happen only after
		 * the sc_flags bit is cleared. We are just dropping
		 * the ps_usecount here.
		 */
		ath9k_ps_restore(sc);
	}

	memset(&txctl, 0, sizeof(struct ath_tx_control));
	txctl.txq = sc->tx.txq_map[skb_get_queue_mapping(skb)];

	ath_dbg(common, ATH_DBG_XMIT, "transmitting packet, skb: %p\n", skb);

	if (ath_tx_start(hw, skb, &txctl) != 0) {
		ath_dbg(common, ATH_DBG_XMIT, "TX failed\n");
		goto exit;
	}

	return 0;
exit:
	dev_kfree_skb_any(skb);
	return 0;
}

static void ath9k_stop(struct ieee80211_hw *hw)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	int i;

	mutex_lock(&sc->mutex);

	aphy->state = ATH_WIPHY_INACTIVE;

	if (led_blink)
		cancel_delayed_work_sync(&sc->ath_led_blink_work);

	cancel_delayed_work_sync(&sc->tx_complete_work);
	cancel_work_sync(&sc->paprd_work);
	cancel_work_sync(&sc->hw_check_work);

	for (i = 0; i < sc->num_sec_wiphy; i++) {
		if (sc->sec_wiphy[i])
			break;
	}

	if (i == sc->num_sec_wiphy) {
		cancel_delayed_work_sync(&sc->wiphy_work);
		cancel_work_sync(&sc->chan_work);
	}

	if (sc->sc_flags & SC_OP_INVALID) {
		ath_dbg(common, ATH_DBG_ANY, "Device not present\n");
		mutex_unlock(&sc->mutex);
		return;
	}

	if (ath9k_wiphy_started(sc)) {
		mutex_unlock(&sc->mutex);
		return; /* another wiphy still in use */
	}

	/* Ensure HW is awake when we try to shut it down. */
	ath9k_ps_wakeup(sc);

	if (ah->btcoex_hw.enabled) {
		ath9k_hw_btcoex_disable(ah);
		if (ah->btcoex_hw.scheme == ATH_BTCOEX_CFG_3WIRE)
			ath9k_btcoex_timer_pause(sc);
	}

	spin_lock_bh(&sc->sc_pcu_lock);

	/* make sure h/w will not generate any interrupt
	 * before setting the invalid flag. */
	ath9k_hw_disable_interrupts(ah);

	if (!(sc->sc_flags & SC_OP_INVALID)) {
		ath_drain_all_txq(sc, false);
		ath_stoprecv(sc);
		ath9k_hw_phy_disable(ah);
	} else
		sc->rx.rxlink = NULL;

	/* disable HAL and put h/w to sleep */
	ath9k_hw_disable(ah);
	ath9k_hw_configpcipowersave(ah, 1, 1);

	spin_unlock_bh(&sc->sc_pcu_lock);

	ath9k_ps_restore(sc);

	/* Finally, put the chip in FULL SLEEP mode */
	ath9k_setpower(sc, ATH9K_PM_FULL_SLEEP);

	sc->sc_flags |= SC_OP_INVALID;

	pm_qos_update_request(&sc->pm_qos_req, PM_QOS_DEFAULT_VALUE);

	mutex_unlock(&sc->mutex);

	ath_dbg(common, ATH_DBG_CONFIG, "Driver halt\n");
}

static int ath9k_add_interface(struct ieee80211_hw *hw,
			       struct ieee80211_vif *vif)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_vif *avp = (void *)vif->drv_priv;
	enum nl80211_iftype ic_opmode = NL80211_IFTYPE_UNSPECIFIED;
	int ret = 0;

	mutex_lock(&sc->mutex);

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		ic_opmode = NL80211_IFTYPE_STATION;
		break;
	case NL80211_IFTYPE_WDS:
		ic_opmode = NL80211_IFTYPE_WDS;
		break;
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
		if (sc->nbcnvifs >= ATH_BCBUF) {
			ret = -ENOBUFS;
			goto out;
		}
		ic_opmode = vif->type;
		break;
	default:
		ath_err(common, "Interface type %d not yet supported\n",
			vif->type);
		ret = -EOPNOTSUPP;
		goto out;
	}

	ath_dbg(common, ATH_DBG_CONFIG,
		"Attach a VIF of type: %d\n", ic_opmode);

	/* Set the VIF opmode */
	avp->av_opmode = ic_opmode;
	avp->av_bslot = -1;

	sc->nvifs++;

	ath9k_set_bssid_mask(hw, vif);

	if (sc->nvifs > 1)
		goto out; /* skip global settings for secondary vif */

	if (ic_opmode == NL80211_IFTYPE_AP) {
		ath9k_hw_set_tsfadjust(ah, 1);
		sc->sc_flags |= SC_OP_TSF_RESET;
	}

	/* Set the device opmode */
	ah->opmode = ic_opmode;

	/*
	 * Enable MIB interrupts when there are hardware phy counters.
	 * Note we only do this (at the moment) for station mode.
	 */
	if ((vif->type == NL80211_IFTYPE_STATION) ||
	    (vif->type == NL80211_IFTYPE_ADHOC) ||
	    (vif->type == NL80211_IFTYPE_MESH_POINT)) {
		if (ah->config.enable_ani)
			ah->imask |= ATH9K_INT_MIB;
		ah->imask |= ATH9K_INT_TSFOOR;
	}

	ath9k_hw_set_interrupts(ah, ah->imask);

	if (vif->type == NL80211_IFTYPE_AP    ||
	    vif->type == NL80211_IFTYPE_ADHOC) {
		sc->sc_flags |= SC_OP_ANI_RUN;
		ath_start_ani(common);
	}

out:
	mutex_unlock(&sc->mutex);
	return ret;
}

static void ath9k_remove_interface(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_vif *avp = (void *)vif->drv_priv;
	bool bs_valid = false;
	int i;

	ath_dbg(common, ATH_DBG_CONFIG, "Detach Interface\n");

	mutex_lock(&sc->mutex);

	/* Stop ANI */
	sc->sc_flags &= ~SC_OP_ANI_RUN;
	del_timer_sync(&common->ani.timer);

	/* Reclaim beacon resources */
	if ((sc->sc_ah->opmode == NL80211_IFTYPE_AP) ||
	    (sc->sc_ah->opmode == NL80211_IFTYPE_ADHOC) ||
	    (sc->sc_ah->opmode == NL80211_IFTYPE_MESH_POINT)) {
		ath9k_ps_wakeup(sc);
		ath9k_hw_stoptxdma(sc->sc_ah, sc->beacon.beaconq);
		ath9k_ps_restore(sc);
	}

	ath_beacon_return(sc, avp);
	sc->sc_flags &= ~SC_OP_BEACONS;

	for (i = 0; i < ARRAY_SIZE(sc->beacon.bslot); i++) {
		if (sc->beacon.bslot[i] == vif) {
			printk(KERN_DEBUG "%s: vif had allocated beacon "
			       "slot\n", __func__);
			sc->beacon.bslot[i] = NULL;
			sc->beacon.bslot_aphy[i] = NULL;
		} else if (sc->beacon.bslot[i])
			bs_valid = true;
	}
	if (!bs_valid && (sc->sc_ah->imask & ATH9K_INT_SWBA)) {
		/* Disable SWBA interrupt */
		sc->sc_ah->imask &= ~ATH9K_INT_SWBA;
		ath9k_ps_wakeup(sc);
		ath9k_hw_set_interrupts(sc->sc_ah, sc->sc_ah->imask);
		ath9k_ps_restore(sc);
	}

	sc->nvifs--;

	mutex_unlock(&sc->mutex);
}

static void ath9k_enable_ps(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;

	sc->ps_enabled = true;
	if (!(ah->caps.hw_caps & ATH9K_HW_CAP_AUTOSLEEP)) {
		if ((ah->imask & ATH9K_INT_TIM_TIMER) == 0) {
			ah->imask |= ATH9K_INT_TIM_TIMER;
			ath9k_hw_set_interrupts(ah, ah->imask);
		}
		ath9k_hw_setrxabort(ah, 1);
	}
}

static void ath9k_disable_ps(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;

	sc->ps_enabled = false;
	ath9k_hw_setpower(ah, ATH9K_PM_AWAKE);
	if (!(ah->caps.hw_caps & ATH9K_HW_CAP_AUTOSLEEP)) {
		ath9k_hw_setrxabort(ah, 0);
		sc->ps_flags &= ~(PS_WAIT_FOR_BEACON |
				  PS_WAIT_FOR_CAB |
				  PS_WAIT_FOR_PSPOLL_DATA |
				  PS_WAIT_FOR_TX_ACK);
		if (ah->imask & ATH9K_INT_TIM_TIMER) {
			ah->imask &= ~ATH9K_INT_TIM_TIMER;
			ath9k_hw_set_interrupts(ah, ah->imask);
		}
	}

}

static int ath9k_config(struct ieee80211_hw *hw, u32 changed)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ieee80211_conf *conf = &hw->conf;
	bool disable_radio;

	mutex_lock(&sc->mutex);

	/*
	 * Leave this as the first check because we need to turn on the
	 * radio if it was disabled before prior to processing the rest
	 * of the changes. Likewise we must only disable the radio towards
	 * the end.
	 */
	if (changed & IEEE80211_CONF_CHANGE_IDLE) {
		bool enable_radio;
		bool all_wiphys_idle;
		bool idle = !!(conf->flags & IEEE80211_CONF_IDLE);

		spin_lock_bh(&sc->wiphy_lock);
		all_wiphys_idle =  ath9k_all_wiphys_idle(sc);
		ath9k_set_wiphy_idle(aphy, idle);

		enable_radio = (!idle && all_wiphys_idle);

		/*
		 * After we unlock here its possible another wiphy
		 * can be re-renabled so to account for that we will
		 * only disable the radio toward the end of this routine
		 * if by then all wiphys are still idle.
		 */
		spin_unlock_bh(&sc->wiphy_lock);

		if (enable_radio) {
			sc->ps_idle = false;
			ath_radio_enable(sc, hw);
			ath_dbg(common, ATH_DBG_CONFIG,
				"not-idle: enabling radio\n");
		}
	}

	/*
	 * We just prepare to enable PS. We have to wait until our AP has
	 * ACK'd our null data frame to disable RX otherwise we'll ignore
	 * those ACKs and end up retransmitting the same null data frames.
	 * IEEE80211_CONF_CHANGE_PS is only passed by mac80211 for STA mode.
	 */
	if (changed & IEEE80211_CONF_CHANGE_PS) {
		unsigned long flags;
		spin_lock_irqsave(&sc->sc_pm_lock, flags);
		if (conf->flags & IEEE80211_CONF_PS)
			ath9k_enable_ps(sc);
		else
			ath9k_disable_ps(sc);
		spin_unlock_irqrestore(&sc->sc_pm_lock, flags);
	}

	if (changed & IEEE80211_CONF_CHANGE_MONITOR) {
		if (conf->flags & IEEE80211_CONF_MONITOR) {
			ath_dbg(common, ATH_DBG_CONFIG,
				"Monitor mode is enabled\n");
			sc->sc_ah->is_monitoring = true;
		} else {
			ath_dbg(common, ATH_DBG_CONFIG,
				"Monitor mode is disabled\n");
			sc->sc_ah->is_monitoring = false;
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		struct ieee80211_channel *curchan = hw->conf.channel;
		int pos = curchan->hw_value;
		int old_pos = -1;
		unsigned long flags;

		if (ah->curchan)
			old_pos = ah->curchan - &ah->channels[0];

		aphy->chan_idx = pos;
		aphy->chan_is_ht = conf_is_ht(conf);
		if (hw->conf.flags & IEEE80211_CONF_OFFCHANNEL)
			sc->sc_flags |= SC_OP_OFFCHANNEL;
		else
			sc->sc_flags &= ~SC_OP_OFFCHANNEL;

		if (aphy->state == ATH_WIPHY_SCAN ||
		    aphy->state == ATH_WIPHY_ACTIVE)
			ath9k_wiphy_pause_all_forced(sc, aphy);
		else {
			/*
			 * Do not change operational channel based on a paused
			 * wiphy changes.
			 */
			goto skip_chan_change;
		}

		ath_dbg(common, ATH_DBG_CONFIG, "Set channel: %d MHz\n",
			curchan->center_freq);

		/* XXX: remove me eventualy */
		ath9k_update_ichannel(sc, hw, &sc->sc_ah->channels[pos]);

		ath_update_chainmask(sc, conf_is_ht(conf));

		/* update survey stats for the old channel before switching */
		spin_lock_irqsave(&common->cc_lock, flags);
		ath_update_survey_stats(sc);
		spin_unlock_irqrestore(&common->cc_lock, flags);

		/*
		 * If the operating channel changes, change the survey in-use flags
		 * along with it.
		 * Reset the survey data for the new channel, unless we're switching
		 * back to the operating channel from an off-channel operation.
		 */
		if (!(hw->conf.flags & IEEE80211_CONF_OFFCHANNEL) &&
		    sc->cur_survey != &sc->survey[pos]) {

			if (sc->cur_survey)
				sc->cur_survey->filled &= ~SURVEY_INFO_IN_USE;

			sc->cur_survey = &sc->survey[pos];

			memset(sc->cur_survey, 0, sizeof(struct survey_info));
			sc->cur_survey->filled |= SURVEY_INFO_IN_USE;
		} else if (!(sc->survey[pos].filled & SURVEY_INFO_IN_USE)) {
			memset(&sc->survey[pos], 0, sizeof(struct survey_info));
		}

		if (ath_set_channel(sc, hw, &sc->sc_ah->channels[pos]) < 0) {
			ath_err(common, "Unable to set channel\n");
			mutex_unlock(&sc->mutex);
			return -EINVAL;
		}

		/*
		 * The most recent snapshot of channel->noisefloor for the old
		 * channel is only available after the hardware reset. Copy it to
		 * the survey stats now.
		 */
		if (old_pos >= 0)
			ath_update_survey_nf(sc, old_pos);
	}

skip_chan_change:
	if (changed & IEEE80211_CONF_CHANGE_POWER) {
		sc->config.txpowlimit = 2 * conf->power_level;
		ath_update_txpow(sc);
	}

	spin_lock_bh(&sc->wiphy_lock);
	disable_radio = ath9k_all_wiphys_idle(sc);
	spin_unlock_bh(&sc->wiphy_lock);

	if (disable_radio) {
		ath_dbg(common, ATH_DBG_CONFIG, "idle: disabling radio\n");
		sc->ps_idle = true;
		ath_radio_disable(sc, hw);
	}

	mutex_unlock(&sc->mutex);

	return 0;
}

#define SUPPORTED_FILTERS			\
	(FIF_PROMISC_IN_BSS |			\
	FIF_ALLMULTI |				\
	FIF_CONTROL |				\
	FIF_PSPOLL |				\
	FIF_OTHER_BSS |				\
	FIF_BCN_PRBRESP_PROMISC |		\
	FIF_PROBE_REQ |				\
	FIF_FCSFAIL)

/* FIXME: sc->sc_full_reset ? */
static void ath9k_configure_filter(struct ieee80211_hw *hw,
				   unsigned int changed_flags,
				   unsigned int *total_flags,
				   u64 multicast)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	u32 rfilt;

	changed_flags &= SUPPORTED_FILTERS;
	*total_flags &= SUPPORTED_FILTERS;

	sc->rx.rxfilter = *total_flags;
	ath9k_ps_wakeup(sc);
	rfilt = ath_calcrxfilter(sc);
	ath9k_hw_setrxfilter(sc->sc_ah, rfilt);
	ath9k_ps_restore(sc);

	ath_dbg(ath9k_hw_common(sc->sc_ah), ATH_DBG_CONFIG,
		"Set HW RX filter: 0x%x\n", rfilt);
}

static int ath9k_sta_add(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;

	ath_node_attach(sc, sta);

	return 0;
}

static int ath9k_sta_remove(struct ieee80211_hw *hw,
			    struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;

	ath_node_detach(sc, sta);

	return 0;
}

static int ath9k_conf_tx(struct ieee80211_hw *hw, u16 queue,
			 const struct ieee80211_tx_queue_params *params)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_txq *txq;
	struct ath9k_tx_queue_info qi;
	int ret = 0;

	if (queue >= WME_NUM_AC)
		return 0;

	txq = sc->tx.txq_map[queue];

	mutex_lock(&sc->mutex);

	memset(&qi, 0, sizeof(struct ath9k_tx_queue_info));

	qi.tqi_aifs = params->aifs;
	qi.tqi_cwmin = params->cw_min;
	qi.tqi_cwmax = params->cw_max;
	qi.tqi_burstTime = params->txop;

	ath_dbg(common, ATH_DBG_CONFIG,
		"Configure tx [queue/halq] [%d/%d], aifs: %d, cw_min: %d, cw_max: %d, txop: %d\n",
		queue, txq->axq_qnum, params->aifs, params->cw_min,
		params->cw_max, params->txop);

	ret = ath_txq_update(sc, txq->axq_qnum, &qi);
	if (ret)
		ath_err(common, "TXQ Update failed\n");

	if (sc->sc_ah->opmode == NL80211_IFTYPE_ADHOC)
		if (queue == WME_AC_BE && !ret)
			ath_beaconq_config(sc);

	mutex_unlock(&sc->mutex);

	return ret;
}

static int ath9k_set_key(struct ieee80211_hw *hw,
			 enum set_key_cmd cmd,
			 struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta,
			 struct ieee80211_key_conf *key)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	int ret = 0;

	if (modparam_nohwcrypt)
		return -ENOSPC;

	mutex_lock(&sc->mutex);
	ath9k_ps_wakeup(sc);
	ath_dbg(common, ATH_DBG_CONFIG, "Set HW Key\n");

	switch (cmd) {
	case SET_KEY:
		ret = ath_key_config(common, vif, sta, key);
		if (ret >= 0) {
			key->hw_key_idx = ret;
			/* push IV and Michael MIC generation to stack */
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
			if (key->cipher == WLAN_CIPHER_SUITE_TKIP)
				key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
			if (sc->sc_ah->sw_mgmt_crypto &&
			    key->cipher == WLAN_CIPHER_SUITE_CCMP)
				key->flags |= IEEE80211_KEY_FLAG_SW_MGMT;
			ret = 0;
		}
		break;
	case DISABLE_KEY:
		ath_key_delete(common, key);
		break;
	default:
		ret = -EINVAL;
	}

	ath9k_ps_restore(sc);
	mutex_unlock(&sc->mutex);

	return ret;
}

static void ath9k_bss_info_changed(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_bss_conf *bss_conf,
				   u32 changed)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_vif *avp = (void *)vif->drv_priv;
	int slottime;
	int error;

	mutex_lock(&sc->mutex);

	if (changed & BSS_CHANGED_BSSID) {
		/* Set BSSID */
		memcpy(common->curbssid, bss_conf->bssid, ETH_ALEN);
		memcpy(avp->bssid, bss_conf->bssid, ETH_ALEN);
		common->curaid = 0;
		ath9k_hw_write_associd(ah);

		/* Set aggregation protection mode parameters */
		sc->config.ath_aggr_prot = 0;

		/* Only legacy IBSS for now */
		if (vif->type == NL80211_IFTYPE_ADHOC)
			ath_update_chainmask(sc, 0);

		ath_dbg(common, ATH_DBG_CONFIG, "BSSID: %pM aid: 0x%x\n",
			common->curbssid, common->curaid);

		/* need to reconfigure the beacon */
		sc->sc_flags &= ~SC_OP_BEACONS ;
	}

	/* Enable transmission of beacons (AP, IBSS, MESH) */
	if ((changed & BSS_CHANGED_BEACON) ||
	    ((changed & BSS_CHANGED_BEACON_ENABLED) && bss_conf->enable_beacon)) {
		ath9k_hw_stoptxdma(sc->sc_ah, sc->beacon.beaconq);
		error = ath_beacon_alloc(aphy, vif);
		if (!error)
			ath_beacon_config(sc, vif);
	}

	if (changed & BSS_CHANGED_ERP_SLOT) {
		if (bss_conf->use_short_slot)
			slottime = 9;
		else
			slottime = 20;
		if (vif->type == NL80211_IFTYPE_AP) {
			/*
			 * Defer update, so that connected stations can adjust
			 * their settings at the same time.
			 * See beacon.c for more details
			 */
			sc->beacon.slottime = slottime;
			sc->beacon.updateslot = UPDATE;
		} else {
			ah->slottime = slottime;
			ath9k_hw_init_global_settings(ah);
		}
	}

	/* Disable transmission of beacons */
	if ((changed & BSS_CHANGED_BEACON_ENABLED) && !bss_conf->enable_beacon)
		ath9k_hw_stoptxdma(sc->sc_ah, sc->beacon.beaconq);

	if (changed & BSS_CHANGED_BEACON_INT) {
		sc->beacon_interval = bss_conf->beacon_int;
		/*
		 * In case of AP mode, the HW TSF has to be reset
		 * when the beacon interval changes.
		 */
		if (vif->type == NL80211_IFTYPE_AP) {
			sc->sc_flags |= SC_OP_TSF_RESET;
			ath9k_hw_stoptxdma(sc->sc_ah, sc->beacon.beaconq);
			error = ath_beacon_alloc(aphy, vif);
			if (!error)
				ath_beacon_config(sc, vif);
		} else {
			ath_beacon_config(sc, vif);
		}
	}

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		ath_dbg(common, ATH_DBG_CONFIG, "BSS Changed PREAMBLE %d\n",
			bss_conf->use_short_preamble);
		if (bss_conf->use_short_preamble)
			sc->sc_flags |= SC_OP_PREAMBLE_SHORT;
		else
			sc->sc_flags &= ~SC_OP_PREAMBLE_SHORT;
	}

	if (changed & BSS_CHANGED_ERP_CTS_PROT) {
		ath_dbg(common, ATH_DBG_CONFIG, "BSS Changed CTS PROT %d\n",
			bss_conf->use_cts_prot);
		if (bss_conf->use_cts_prot &&
		    hw->conf.channel->band != IEEE80211_BAND_5GHZ)
			sc->sc_flags |= SC_OP_PROTECT_ENABLE;
		else
			sc->sc_flags &= ~SC_OP_PROTECT_ENABLE;
	}

	if (changed & BSS_CHANGED_ASSOC) {
		ath_dbg(common, ATH_DBG_CONFIG, "BSS Changed ASSOC %d\n",
			bss_conf->assoc);
		ath9k_bss_assoc_info(sc, hw, vif, bss_conf);
	}

	mutex_unlock(&sc->mutex);
}

static u64 ath9k_get_tsf(struct ieee80211_hw *hw)
{
	u64 tsf;
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;

	mutex_lock(&sc->mutex);
	tsf = ath9k_hw_gettsf64(sc->sc_ah);
	mutex_unlock(&sc->mutex);

	return tsf;
}

static void ath9k_set_tsf(struct ieee80211_hw *hw, u64 tsf)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;

	mutex_lock(&sc->mutex);
	ath9k_hw_settsf64(sc->sc_ah, tsf);
	mutex_unlock(&sc->mutex);
}

static void ath9k_reset_tsf(struct ieee80211_hw *hw)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;

	mutex_lock(&sc->mutex);

	ath9k_ps_wakeup(sc);
	ath9k_hw_reset_tsf(sc->sc_ah);
	ath9k_ps_restore(sc);

	mutex_unlock(&sc->mutex);
}

static int ath9k_ampdu_action(struct ieee80211_hw *hw,
			      struct ieee80211_vif *vif,
			      enum ieee80211_ampdu_mlme_action action,
			      struct ieee80211_sta *sta,
			      u16 tid, u16 *ssn)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	int ret = 0;

	local_bh_disable();

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		if (!(sc->sc_flags & SC_OP_RXAGGR))
			ret = -ENOTSUPP;
		break;
	case IEEE80211_AMPDU_RX_STOP:
		break;
	case IEEE80211_AMPDU_TX_START:
		if (!(sc->sc_flags & SC_OP_TXAGGR))
			return -EOPNOTSUPP;

		ath9k_ps_wakeup(sc);
		ret = ath_tx_aggr_start(sc, sta, tid, ssn);
		if (!ret)
			ieee80211_start_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		ath9k_ps_restore(sc);
		break;
	case IEEE80211_AMPDU_TX_STOP:
		ath9k_ps_wakeup(sc);
		ath_tx_aggr_stop(sc, sta, tid);
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		ath9k_ps_restore(sc);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		ath9k_ps_wakeup(sc);
		ath_tx_aggr_resume(sc, sta, tid);
		ath9k_ps_restore(sc);
		break;
	default:
		ath_err(ath9k_hw_common(sc->sc_ah), "Unknown AMPDU action\n");
	}

	local_bh_enable();

	return ret;
}

static int ath9k_get_survey(struct ieee80211_hw *hw, int idx,
			     struct survey_info *survey)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *chan;
	unsigned long flags;
	int pos;

	spin_lock_irqsave(&common->cc_lock, flags);
	if (idx == 0)
		ath_update_survey_stats(sc);

	sband = hw->wiphy->bands[IEEE80211_BAND_2GHZ];
	if (sband && idx >= sband->n_channels) {
		idx -= sband->n_channels;
		sband = NULL;
	}

	if (!sband)
		sband = hw->wiphy->bands[IEEE80211_BAND_5GHZ];

	if (!sband || idx >= sband->n_channels) {
		spin_unlock_irqrestore(&common->cc_lock, flags);
		return -ENOENT;
	}

	chan = &sband->channels[idx];
	pos = chan->hw_value;
	memcpy(survey, &sc->survey[pos], sizeof(*survey));
	survey->channel = chan;
	spin_unlock_irqrestore(&common->cc_lock, flags);

	return 0;
}

static void ath9k_sw_scan_start(struct ieee80211_hw *hw)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;

	mutex_lock(&sc->mutex);
	if (ath9k_wiphy_scanning(sc)) {
		/*
		 * There is a race here in mac80211 but fixing it requires
		 * we revisit how we handle the scan complete callback.
		 * After mac80211 fixes we will not have configured hardware
		 * to the home channel nor would we have configured the RX
		 * filter yet.
		 */
		mutex_unlock(&sc->mutex);
		return;
	}

	aphy->state = ATH_WIPHY_SCAN;
	ath9k_wiphy_pause_all_forced(sc, aphy);
	mutex_unlock(&sc->mutex);
}

/*
 * XXX: this requires a revisit after the driver
 * scan_complete gets moved to another place/removed in mac80211.
 */
static void ath9k_sw_scan_complete(struct ieee80211_hw *hw)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;

	mutex_lock(&sc->mutex);
	aphy->state = ATH_WIPHY_ACTIVE;
	mutex_unlock(&sc->mutex);
}

static void ath9k_set_coverage_class(struct ieee80211_hw *hw, u8 coverage_class)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath_hw *ah = sc->sc_ah;

	mutex_lock(&sc->mutex);
	ah->coverage_class = coverage_class;
	ath9k_hw_init_global_settings(ah);
	mutex_unlock(&sc->mutex);
}

struct ieee80211_ops ath9k_ops = {
	.tx 		    = ath9k_tx,
	.start 		    = ath9k_start,
	.stop 		    = ath9k_stop,
	.add_interface 	    = ath9k_add_interface,
	.remove_interface   = ath9k_remove_interface,
	.config 	    = ath9k_config,
	.configure_filter   = ath9k_configure_filter,
	.sta_add	    = ath9k_sta_add,
	.sta_remove	    = ath9k_sta_remove,
	.conf_tx 	    = ath9k_conf_tx,
	.bss_info_changed   = ath9k_bss_info_changed,
	.set_key            = ath9k_set_key,
	.get_tsf 	    = ath9k_get_tsf,
	.set_tsf 	    = ath9k_set_tsf,
	.reset_tsf 	    = ath9k_reset_tsf,
	.ampdu_action       = ath9k_ampdu_action,
	.get_survey	    = ath9k_get_survey,
	.sw_scan_start      = ath9k_sw_scan_start,
	.sw_scan_complete   = ath9k_sw_scan_complete,
	.rfkill_poll        = ath9k_rfkill_poll_state,
	.set_coverage_class = ath9k_set_coverage_class,
};
