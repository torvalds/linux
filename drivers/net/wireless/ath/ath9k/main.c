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

static char *dev_info = "ath9k";

MODULE_AUTHOR("Atheros Communications");
MODULE_DESCRIPTION("Support for Atheros 802.11n wireless LAN cards.");
MODULE_SUPPORTED_DEVICE("Atheros 802.11n WLAN cards");
MODULE_LICENSE("Dual BSD/GPL");

static int modparam_nohwcrypt;
module_param_named(nohwcrypt, modparam_nohwcrypt, int, 0444);
MODULE_PARM_DESC(nohwcrypt, "Disable hardware encryption");

/* We use the hw_value as an index into our private channel structure */

#define CHAN2G(_freq, _idx)  { \
	.center_freq = (_freq), \
	.hw_value = (_idx), \
	.max_power = 20, \
}

#define CHAN5G(_freq, _idx) { \
	.band = IEEE80211_BAND_5GHZ, \
	.center_freq = (_freq), \
	.hw_value = (_idx), \
	.max_power = 20, \
}

/* Some 2 GHz radios are actually tunable on 2312-2732
 * on 5 MHz steps, we support the channels which we know
 * we have calibration data for all cards though to make
 * this static */
static struct ieee80211_channel ath9k_2ghz_chantable[] = {
	CHAN2G(2412, 0), /* Channel 1 */
	CHAN2G(2417, 1), /* Channel 2 */
	CHAN2G(2422, 2), /* Channel 3 */
	CHAN2G(2427, 3), /* Channel 4 */
	CHAN2G(2432, 4), /* Channel 5 */
	CHAN2G(2437, 5), /* Channel 6 */
	CHAN2G(2442, 6), /* Channel 7 */
	CHAN2G(2447, 7), /* Channel 8 */
	CHAN2G(2452, 8), /* Channel 9 */
	CHAN2G(2457, 9), /* Channel 10 */
	CHAN2G(2462, 10), /* Channel 11 */
	CHAN2G(2467, 11), /* Channel 12 */
	CHAN2G(2472, 12), /* Channel 13 */
	CHAN2G(2484, 13), /* Channel 14 */
};

/* Some 5 GHz radios are actually tunable on XXXX-YYYY
 * on 5 MHz steps, we support the channels which we know
 * we have calibration data for all cards though to make
 * this static */
static struct ieee80211_channel ath9k_5ghz_chantable[] = {
	/* _We_ call this UNII 1 */
	CHAN5G(5180, 14), /* Channel 36 */
	CHAN5G(5200, 15), /* Channel 40 */
	CHAN5G(5220, 16), /* Channel 44 */
	CHAN5G(5240, 17), /* Channel 48 */
	/* _We_ call this UNII 2 */
	CHAN5G(5260, 18), /* Channel 52 */
	CHAN5G(5280, 19), /* Channel 56 */
	CHAN5G(5300, 20), /* Channel 60 */
	CHAN5G(5320, 21), /* Channel 64 */
	/* _We_ call this "Middle band" */
	CHAN5G(5500, 22), /* Channel 100 */
	CHAN5G(5520, 23), /* Channel 104 */
	CHAN5G(5540, 24), /* Channel 108 */
	CHAN5G(5560, 25), /* Channel 112 */
	CHAN5G(5580, 26), /* Channel 116 */
	CHAN5G(5600, 27), /* Channel 120 */
	CHAN5G(5620, 28), /* Channel 124 */
	CHAN5G(5640, 29), /* Channel 128 */
	CHAN5G(5660, 30), /* Channel 132 */
	CHAN5G(5680, 31), /* Channel 136 */
	CHAN5G(5700, 32), /* Channel 140 */
	/* _We_ call this UNII 3 */
	CHAN5G(5745, 33), /* Channel 149 */
	CHAN5G(5765, 34), /* Channel 153 */
	CHAN5G(5785, 35), /* Channel 157 */
	CHAN5G(5805, 36), /* Channel 161 */
	CHAN5G(5825, 37), /* Channel 165 */
};

static void ath_cache_conf_rate(struct ath_softc *sc,
				struct ieee80211_conf *conf)
{
	switch (conf->channel->band) {
	case IEEE80211_BAND_2GHZ:
		if (conf_is_ht20(conf))
			sc->cur_rate_table =
			  sc->hw_rate_table[ATH9K_MODE_11NG_HT20];
		else if (conf_is_ht40_minus(conf))
			sc->cur_rate_table =
			  sc->hw_rate_table[ATH9K_MODE_11NG_HT40MINUS];
		else if (conf_is_ht40_plus(conf))
			sc->cur_rate_table =
			  sc->hw_rate_table[ATH9K_MODE_11NG_HT40PLUS];
		else
			sc->cur_rate_table =
			  sc->hw_rate_table[ATH9K_MODE_11G];
		break;
	case IEEE80211_BAND_5GHZ:
		if (conf_is_ht20(conf))
			sc->cur_rate_table =
			  sc->hw_rate_table[ATH9K_MODE_11NA_HT20];
		else if (conf_is_ht40_minus(conf))
			sc->cur_rate_table =
			  sc->hw_rate_table[ATH9K_MODE_11NA_HT40MINUS];
		else if (conf_is_ht40_plus(conf))
			sc->cur_rate_table =
			  sc->hw_rate_table[ATH9K_MODE_11NA_HT40PLUS];
		else
			sc->cur_rate_table =
			  sc->hw_rate_table[ATH9K_MODE_11A];
		break;
	default:
		BUG_ON(1);
		break;
	}
}

static void ath_update_txpow(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	u32 txpow;

	if (sc->curtxpow != sc->config.txpowlimit) {
		ath9k_hw_set_txpowerlimit(ah, sc->config.txpowlimit);
		/* read back in case value is clamped */
		ath9k_hw_getcapability(ah, ATH9K_CAP_TXPOW, 1, &txpow);
		sc->curtxpow = txpow;
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

static void ath_setup_rates(struct ath_softc *sc, enum ieee80211_band band)
{
	const struct ath_rate_table *rate_table = NULL;
	struct ieee80211_supported_band *sband;
	struct ieee80211_rate *rate;
	int i, maxrates;

	switch (band) {
	case IEEE80211_BAND_2GHZ:
		rate_table = sc->hw_rate_table[ATH9K_MODE_11G];
		break;
	case IEEE80211_BAND_5GHZ:
		rate_table = sc->hw_rate_table[ATH9K_MODE_11A];
		break;
	default:
		break;
	}

	if (rate_table == NULL)
		return;

	sband = &sc->sbands[band];
	rate = sc->rates[band];

	if (rate_table->rate_cnt > ATH_RATE_MAX)
		maxrates = ATH_RATE_MAX;
	else
		maxrates = rate_table->rate_cnt;

	for (i = 0; i < maxrates; i++) {
		rate[i].bitrate = rate_table->info[i].ratekbps / 100;
		rate[i].hw_value = rate_table->info[i].ratecode;
		if (rate_table->info[i].short_preamble) {
			rate[i].hw_value_short = rate_table->info[i].ratecode |
				rate_table->info[i].short_preamble;
			rate[i].flags = IEEE80211_RATE_SHORT_PREAMBLE;
		}
		sband->n_bitrates++;

		DPRINTF(sc, ATH_DBG_CONFIG, "Rate: %2dMbps, ratecode: %2d\n",
			rate[i].bitrate / 10, rate[i].hw_value);
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

/*
 * Set/change channels.  If the channel is really being changed, it's done
 * by reseting the chip.  To accomplish this we must first cleanup any pending
 * DMA, then restart stuff.
*/
int ath_set_channel(struct ath_softc *sc, struct ieee80211_hw *hw,
		    struct ath9k_channel *hchan)
{
	struct ath_hw *ah = sc->sc_ah;
	bool fastcc = true, stopped;
	struct ieee80211_channel *channel = hw->conf.channel;
	int r;

	if (sc->sc_flags & SC_OP_INVALID)
		return -EIO;

	ath9k_ps_wakeup(sc);

	/*
	 * This is only performed if the channel settings have
	 * actually changed.
	 *
	 * To switch channels clear any pending DMA operations;
	 * wait long enough for the RX fifo to drain, reset the
	 * hardware at the new frequency, and then re-enable
	 * the relevant bits of the h/w.
	 */
	ath9k_hw_set_interrupts(ah, 0);
	ath_drain_all_txq(sc, false);
	stopped = ath_stoprecv(sc);

	/* XXX: do not flush receive queue here. We don't want
	 * to flush data frames already in queue because of
	 * changing channel. */

	if (!stopped || (sc->sc_flags & SC_OP_FULL_RESET))
		fastcc = false;

	DPRINTF(sc, ATH_DBG_CONFIG,
		"(%u MHz) -> (%u MHz), chanwidth: %d\n",
		sc->sc_ah->curchan->channel,
		channel->center_freq, sc->tx_chan_width);

	spin_lock_bh(&sc->sc_resetlock);

	r = ath9k_hw_reset(ah, hchan, fastcc);
	if (r) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to reset channel (%u Mhz) "
			"reset status %d\n",
			channel->center_freq, r);
		spin_unlock_bh(&sc->sc_resetlock);
		goto ps_restore;
	}
	spin_unlock_bh(&sc->sc_resetlock);

	sc->sc_flags &= ~SC_OP_FULL_RESET;

	if (ath_startrecv(sc) != 0) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to restart recv logic\n");
		r = -EIO;
		goto ps_restore;
	}

	ath_cache_conf_rate(sc, &hw->conf);
	ath_update_txpow(sc);
	ath9k_hw_set_interrupts(ah, sc->imask);

 ps_restore:
	ath9k_ps_restore(sc);
	return r;
}

/*
 *  This routine performs the periodic noise floor calibration function
 *  that is used to adjust and optimize the chip performance.  This
 *  takes environmental changes (location, temperature) into account.
 *  When the task is complete, it reschedules itself depending on the
 *  appropriate interval that was calculated.
 */
static void ath_ani_calibrate(unsigned long data)
{
	struct ath_softc *sc = (struct ath_softc *)data;
	struct ath_hw *ah = sc->sc_ah;
	bool longcal = false;
	bool shortcal = false;
	bool aniflag = false;
	unsigned int timestamp = jiffies_to_msecs(jiffies);
	u32 cal_interval, short_cal_interval;

	short_cal_interval = (ah->opmode == NL80211_IFTYPE_AP) ?
		ATH_AP_SHORT_CALINTERVAL : ATH_STA_SHORT_CALINTERVAL;

	/*
	* don't calibrate when we're scanning.
	* we are most likely not on our home channel.
	*/
	spin_lock(&sc->ani_lock);
	if (sc->sc_flags & SC_OP_SCANNING)
		goto set_timer;

	/* Only calibrate if awake */
	if (sc->sc_ah->power_mode != ATH9K_PM_AWAKE)
		goto set_timer;

	ath9k_ps_wakeup(sc);

	/* Long calibration runs independently of short calibration. */
	if ((timestamp - sc->ani.longcal_timer) >= ATH_LONG_CALINTERVAL) {
		longcal = true;
		DPRINTF(sc, ATH_DBG_ANI, "longcal @%lu\n", jiffies);
		sc->ani.longcal_timer = timestamp;
	}

	/* Short calibration applies only while caldone is false */
	if (!sc->ani.caldone) {
		if ((timestamp - sc->ani.shortcal_timer) >= short_cal_interval) {
			shortcal = true;
			DPRINTF(sc, ATH_DBG_ANI, "shortcal @%lu\n", jiffies);
			sc->ani.shortcal_timer = timestamp;
			sc->ani.resetcal_timer = timestamp;
		}
	} else {
		if ((timestamp - sc->ani.resetcal_timer) >=
		    ATH_RESTART_CALINTERVAL) {
			sc->ani.caldone = ath9k_hw_reset_calvalid(ah);
			if (sc->ani.caldone)
				sc->ani.resetcal_timer = timestamp;
		}
	}

	/* Verify whether we must check ANI */
	if ((timestamp - sc->ani.checkani_timer) >= ATH_ANI_POLLINTERVAL) {
		aniflag = true;
		sc->ani.checkani_timer = timestamp;
	}

	/* Skip all processing if there's nothing to do. */
	if (longcal || shortcal || aniflag) {
		/* Call ANI routine if necessary */
		if (aniflag)
			ath9k_hw_ani_monitor(ah, ah->curchan);

		/* Perform calibration if necessary */
		if (longcal || shortcal) {
			sc->ani.caldone = ath9k_hw_calibrate(ah, ah->curchan,
						     sc->rx_chainmask, longcal);

			if (longcal)
				sc->ani.noise_floor = ath9k_hw_getchan_noise(ah,
								     ah->curchan);

			DPRINTF(sc, ATH_DBG_ANI," calibrate chan %u/%x nf: %d\n",
				ah->curchan->channel, ah->curchan->channelFlags,
				sc->ani.noise_floor);
		}
	}

	ath9k_ps_restore(sc);

set_timer:
	spin_unlock(&sc->ani_lock);
	/*
	* Set timer interval based on previous results.
	* The interval must be the shortest necessary to satisfy ANI,
	* short calibration and long calibration.
	*/
	cal_interval = ATH_LONG_CALINTERVAL;
	if (sc->sc_ah->config.enable_ani)
		cal_interval = min(cal_interval, (u32)ATH_ANI_POLLINTERVAL);
	if (!sc->ani.caldone)
		cal_interval = min(cal_interval, (u32)short_cal_interval);

	mod_timer(&sc->ani.timer, jiffies + msecs_to_jiffies(cal_interval));
}

static void ath_start_ani(struct ath_softc *sc)
{
	unsigned long timestamp = jiffies_to_msecs(jiffies);

	sc->ani.longcal_timer = timestamp;
	sc->ani.shortcal_timer = timestamp;
	sc->ani.checkani_timer = timestamp;

	mod_timer(&sc->ani.timer,
		  jiffies + msecs_to_jiffies(ATH_ANI_POLLINTERVAL));
}

/*
 * Update tx/rx chainmask. For legacy association,
 * hard code chainmask to 1x1, for 11n association, use
 * the chainmask configuration, for bt coexistence, use
 * the chainmask configuration even in legacy mode.
 */
void ath_update_chainmask(struct ath_softc *sc, int is_ht)
{
	if ((sc->sc_flags & SC_OP_SCANNING) || is_ht ||
	    (sc->btcoex_info.btcoex_scheme != ATH_BTCOEX_CFG_NONE)) {
		sc->tx_chainmask = sc->sc_ah->caps.tx_chainmask;
		sc->rx_chainmask = sc->sc_ah->caps.rx_chainmask;
	} else {
		sc->tx_chainmask = 1;
		sc->rx_chainmask = 1;
	}

	DPRINTF(sc, ATH_DBG_CONFIG, "tx chmask: %d, rx chmask: %d\n",
		sc->tx_chainmask, sc->rx_chainmask);
}

static void ath_node_attach(struct ath_softc *sc, struct ieee80211_sta *sta)
{
	struct ath_node *an;

	an = (struct ath_node *)sta->drv_priv;

	if (sc->sc_flags & SC_OP_TXAGGR) {
		ath_tx_node_init(sc, an);
		an->maxampdu = 1 << (IEEE80211_HT_MAX_AMPDU_FACTOR +
				     sta->ht_cap.ampdu_factor);
		an->mpdudensity = parse_mpdudensity(sta->ht_cap.ampdu_density);
		an->last_rssi = ATH_RSSI_DUMMY_MARKER;
	}
}

static void ath_node_detach(struct ath_softc *sc, struct ieee80211_sta *sta)
{
	struct ath_node *an = (struct ath_node *)sta->drv_priv;

	if (sc->sc_flags & SC_OP_TXAGGR)
		ath_tx_node_cleanup(sc, an);
}

static void ath9k_tasklet(unsigned long data)
{
	struct ath_softc *sc = (struct ath_softc *)data;
	u32 status = sc->intrstatus;

	ath9k_ps_wakeup(sc);

	if (status & ATH9K_INT_FATAL) {
		ath_reset(sc, false);
		ath9k_ps_restore(sc);
		return;
	}

	if (status & (ATH9K_INT_RX | ATH9K_INT_RXEOL | ATH9K_INT_RXORN)) {
		spin_lock_bh(&sc->rx.rxflushlock);
		ath_rx_tasklet(sc, 0);
		spin_unlock_bh(&sc->rx.rxflushlock);
	}

	if (status & ATH9K_INT_TX)
		ath_tx_tasklet(sc);

	if ((status & ATH9K_INT_TSFOOR) && sc->ps_enabled) {
		/*
		 * TSF sync does not look correct; remain awake to sync with
		 * the next Beacon.
		 */
		DPRINTF(sc, ATH_DBG_PS, "TSFOOR - Sync with next Beacon\n");
		sc->sc_flags |= SC_OP_WAIT_FOR_BEACON | SC_OP_BEACON_SYNC;
	}

	if (sc->btcoex_info.btcoex_scheme == ATH_BTCOEX_CFG_3WIRE)
		if (status & ATH9K_INT_GENTIMER)
			ath_gen_timer_isr(sc->sc_ah);

	/* re-enable hardware interrupt */
	ath9k_hw_set_interrupts(sc->sc_ah, sc->imask);
	ath9k_ps_restore(sc);
}

irqreturn_t ath_isr(int irq, void *dev)
{
#define SCHED_INTR (				\
		ATH9K_INT_FATAL |		\
		ATH9K_INT_RXORN |		\
		ATH9K_INT_RXEOL |		\
		ATH9K_INT_RX |			\
		ATH9K_INT_TX |			\
		ATH9K_INT_BMISS |		\
		ATH9K_INT_CST |			\
		ATH9K_INT_TSFOOR |		\
		ATH9K_INT_GENTIMER)

	struct ath_softc *sc = dev;
	struct ath_hw *ah = sc->sc_ah;
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
	status &= sc->imask;	/* discard unasked-for bits */

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
	if (status & (ATH9K_INT_FATAL | ATH9K_INT_RXORN))
		goto chip_reset;

	if (status & ATH9K_INT_SWBA)
		tasklet_schedule(&sc->bcon_tasklet);

	if (status & ATH9K_INT_TXURN)
		ath9k_hw_updatetxtriglevel(ah, true);

	if (status & ATH9K_INT_MIB) {
		/*
		 * Disable interrupts until we service the MIB
		 * interrupt; otherwise it will continue to
		 * fire.
		 */
		ath9k_hw_set_interrupts(ah, 0);
		/*
		 * Let the hal handle the event. We assume
		 * it will clear whatever condition caused
		 * the interrupt.
		 */
		ath9k_hw_procmibevent(ah);
		ath9k_hw_set_interrupts(ah, sc->imask);
	}

	if (!(ah->caps.hw_caps & ATH9K_HW_CAP_AUTOSLEEP))
		if (status & ATH9K_INT_TIM_TIMER) {
			/* Clear RxAbort bit so that we can
			 * receive frames */
			ath9k_hw_setpower(ah, ATH9K_PM_AWAKE);
			ath9k_hw_setrxabort(sc->sc_ah, 0);
			sc->sc_flags |= SC_OP_WAIT_FOR_BEACON;
		}

chip_reset:

	ath_debug_stat_interrupt(sc, status);

	if (sched) {
		/* turn off every interrupt except SWBA */
		ath9k_hw_set_interrupts(ah, (sc->imask & ATH9K_INT_SWBA));
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

static int ath_setkey_tkip(struct ath_softc *sc, u16 keyix, const u8 *key,
			   struct ath9k_keyval *hk, const u8 *addr,
			   bool authenticator)
{
	const u8 *key_rxmic;
	const u8 *key_txmic;

	key_txmic = key + NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY;
	key_rxmic = key + NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY;

	if (addr == NULL) {
		/*
		 * Group key installation - only two key cache entries are used
		 * regardless of splitmic capability since group key is only
		 * used either for TX or RX.
		 */
		if (authenticator) {
			memcpy(hk->kv_mic, key_txmic, sizeof(hk->kv_mic));
			memcpy(hk->kv_txmic, key_txmic, sizeof(hk->kv_mic));
		} else {
			memcpy(hk->kv_mic, key_rxmic, sizeof(hk->kv_mic));
			memcpy(hk->kv_txmic, key_rxmic, sizeof(hk->kv_mic));
		}
		return ath9k_hw_set_keycache_entry(sc->sc_ah, keyix, hk, addr);
	}
	if (!sc->splitmic) {
		/* TX and RX keys share the same key cache entry. */
		memcpy(hk->kv_mic, key_rxmic, sizeof(hk->kv_mic));
		memcpy(hk->kv_txmic, key_txmic, sizeof(hk->kv_txmic));
		return ath9k_hw_set_keycache_entry(sc->sc_ah, keyix, hk, addr);
	}

	/* Separate key cache entries for TX and RX */

	/* TX key goes at first index, RX key at +32. */
	memcpy(hk->kv_mic, key_txmic, sizeof(hk->kv_mic));
	if (!ath9k_hw_set_keycache_entry(sc->sc_ah, keyix, hk, NULL)) {
		/* TX MIC entry failed. No need to proceed further */
		DPRINTF(sc, ATH_DBG_FATAL,
			"Setting TX MIC Key Failed\n");
		return 0;
	}

	memcpy(hk->kv_mic, key_rxmic, sizeof(hk->kv_mic));
	/* XXX delete tx key on failure? */
	return ath9k_hw_set_keycache_entry(sc->sc_ah, keyix + 32, hk, addr);
}

static int ath_reserve_key_cache_slot_tkip(struct ath_softc *sc)
{
	int i;

	for (i = IEEE80211_WEP_NKID; i < sc->keymax / 2; i++) {
		if (test_bit(i, sc->keymap) ||
		    test_bit(i + 64, sc->keymap))
			continue; /* At least one part of TKIP key allocated */
		if (sc->splitmic &&
		    (test_bit(i + 32, sc->keymap) ||
		     test_bit(i + 64 + 32, sc->keymap)))
			continue; /* At least one part of TKIP key allocated */

		/* Found a free slot for a TKIP key */
		return i;
	}
	return -1;
}

static int ath_reserve_key_cache_slot(struct ath_softc *sc)
{
	int i;

	/* First, try to find slots that would not be available for TKIP. */
	if (sc->splitmic) {
		for (i = IEEE80211_WEP_NKID; i < sc->keymax / 4; i++) {
			if (!test_bit(i, sc->keymap) &&
			    (test_bit(i + 32, sc->keymap) ||
			     test_bit(i + 64, sc->keymap) ||
			     test_bit(i + 64 + 32, sc->keymap)))
				return i;
			if (!test_bit(i + 32, sc->keymap) &&
			    (test_bit(i, sc->keymap) ||
			     test_bit(i + 64, sc->keymap) ||
			     test_bit(i + 64 + 32, sc->keymap)))
				return i + 32;
			if (!test_bit(i + 64, sc->keymap) &&
			    (test_bit(i , sc->keymap) ||
			     test_bit(i + 32, sc->keymap) ||
			     test_bit(i + 64 + 32, sc->keymap)))
				return i + 64;
			if (!test_bit(i + 64 + 32, sc->keymap) &&
			    (test_bit(i, sc->keymap) ||
			     test_bit(i + 32, sc->keymap) ||
			     test_bit(i + 64, sc->keymap)))
				return i + 64 + 32;
		}
	} else {
		for (i = IEEE80211_WEP_NKID; i < sc->keymax / 2; i++) {
			if (!test_bit(i, sc->keymap) &&
			    test_bit(i + 64, sc->keymap))
				return i;
			if (test_bit(i, sc->keymap) &&
			    !test_bit(i + 64, sc->keymap))
				return i + 64;
		}
	}

	/* No partially used TKIP slots, pick any available slot */
	for (i = IEEE80211_WEP_NKID; i < sc->keymax; i++) {
		/* Do not allow slots that could be needed for TKIP group keys
		 * to be used. This limitation could be removed if we know that
		 * TKIP will not be used. */
		if (i >= 64 && i < 64 + IEEE80211_WEP_NKID)
			continue;
		if (sc->splitmic) {
			if (i >= 32 && i < 32 + IEEE80211_WEP_NKID)
				continue;
			if (i >= 64 + 32 && i < 64 + 32 + IEEE80211_WEP_NKID)
				continue;
		}

		if (!test_bit(i, sc->keymap))
			return i; /* Found a free slot for a key */
	}

	/* No free slot found */
	return -1;
}

static int ath_key_config(struct ath_softc *sc,
			  struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key)
{
	struct ath9k_keyval hk;
	const u8 *mac = NULL;
	int ret = 0;
	int idx;

	memset(&hk, 0, sizeof(hk));

	switch (key->alg) {
	case ALG_WEP:
		hk.kv_type = ATH9K_CIPHER_WEP;
		break;
	case ALG_TKIP:
		hk.kv_type = ATH9K_CIPHER_TKIP;
		break;
	case ALG_CCMP:
		hk.kv_type = ATH9K_CIPHER_AES_CCM;
		break;
	default:
		return -EOPNOTSUPP;
	}

	hk.kv_len = key->keylen;
	memcpy(hk.kv_val, key->key, key->keylen);

	if (!(key->flags & IEEE80211_KEY_FLAG_PAIRWISE)) {
		/* For now, use the default keys for broadcast keys. This may
		 * need to change with virtual interfaces. */
		idx = key->keyidx;
	} else if (key->keyidx) {
		if (WARN_ON(!sta))
			return -EOPNOTSUPP;
		mac = sta->addr;

		if (vif->type != NL80211_IFTYPE_AP) {
			/* Only keyidx 0 should be used with unicast key, but
			 * allow this for client mode for now. */
			idx = key->keyidx;
		} else
			return -EIO;
	} else {
		if (WARN_ON(!sta))
			return -EOPNOTSUPP;
		mac = sta->addr;

		if (key->alg == ALG_TKIP)
			idx = ath_reserve_key_cache_slot_tkip(sc);
		else
			idx = ath_reserve_key_cache_slot(sc);
		if (idx < 0)
			return -ENOSPC; /* no free key cache entries */
	}

	if (key->alg == ALG_TKIP)
		ret = ath_setkey_tkip(sc, idx, key->key, &hk, mac,
				      vif->type == NL80211_IFTYPE_AP);
	else
		ret = ath9k_hw_set_keycache_entry(sc->sc_ah, idx, &hk, mac);

	if (!ret)
		return -EIO;

	set_bit(idx, sc->keymap);
	if (key->alg == ALG_TKIP) {
		set_bit(idx + 64, sc->keymap);
		if (sc->splitmic) {
			set_bit(idx + 32, sc->keymap);
			set_bit(idx + 64 + 32, sc->keymap);
		}
	}

	return idx;
}

static void ath_key_delete(struct ath_softc *sc, struct ieee80211_key_conf *key)
{
	ath9k_hw_keyreset(sc->sc_ah, key->hw_key_idx);
	if (key->hw_key_idx < IEEE80211_WEP_NKID)
		return;

	clear_bit(key->hw_key_idx, sc->keymap);
	if (key->alg != ALG_TKIP)
		return;

	clear_bit(key->hw_key_idx + 64, sc->keymap);
	if (sc->splitmic) {
		clear_bit(key->hw_key_idx + 32, sc->keymap);
		clear_bit(key->hw_key_idx + 64 + 32, sc->keymap);
	}
}

static void setup_ht_cap(struct ath_softc *sc,
			 struct ieee80211_sta_ht_cap *ht_info)
{
	u8 tx_streams, rx_streams;

	ht_info->ht_supported = true;
	ht_info->cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
		       IEEE80211_HT_CAP_SM_PS |
		       IEEE80211_HT_CAP_SGI_40 |
		       IEEE80211_HT_CAP_DSSSCCK40;

	ht_info->ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K;
	ht_info->ampdu_density = IEEE80211_HT_MPDU_DENSITY_8;

	/* set up supported mcs set */
	memset(&ht_info->mcs, 0, sizeof(ht_info->mcs));
	tx_streams = !(sc->tx_chainmask & (sc->tx_chainmask - 1)) ? 1 : 2;
	rx_streams = !(sc->rx_chainmask & (sc->rx_chainmask - 1)) ? 1 : 2;

	if (tx_streams != rx_streams) {
		DPRINTF(sc, ATH_DBG_CONFIG, "TX streams %d, RX streams: %d\n",
			tx_streams, rx_streams);
		ht_info->mcs.tx_params |= IEEE80211_HT_MCS_TX_RX_DIFF;
		ht_info->mcs.tx_params |= ((tx_streams - 1) <<
				IEEE80211_HT_MCS_TX_MAX_STREAMS_SHIFT);
	}

	ht_info->mcs.rx_mask[0] = 0xff;
	if (rx_streams >= 2)
		ht_info->mcs.rx_mask[1] = 0xff;

	ht_info->mcs.tx_params |= IEEE80211_HT_MCS_TX_DEFINED;
}

static void ath9k_bss_assoc_info(struct ath_softc *sc,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *bss_conf)
{

	if (bss_conf->assoc) {
		DPRINTF(sc, ATH_DBG_CONFIG, "Bss Info ASSOC %d, bssid: %pM\n",
			bss_conf->aid, sc->curbssid);

		/* New association, store aid */
		sc->curaid = bss_conf->aid;
		ath9k_hw_write_associd(sc);

		/*
		 * Request a re-configuration of Beacon related timers
		 * on the receipt of the first Beacon frame (i.e.,
		 * after time sync with the AP).
		 */
		sc->sc_flags |= SC_OP_BEACON_SYNC;

		/* Configure the beacon */
		ath_beacon_config(sc, vif);

		/* Reset rssi stats */
		sc->sc_ah->stats.avgbrssi = ATH_RSSI_DUMMY_MARKER;

		ath_start_ani(sc);
	} else {
		DPRINTF(sc, ATH_DBG_CONFIG, "Bss Info DISASSOC\n");
		sc->curaid = 0;
		/* Stop ANI */
		del_timer_sync(&sc->ani.timer);
	}
}

/********************************/
/*	 LED functions		*/
/********************************/

static void ath_led_blink_work(struct work_struct *work)
{
	struct ath_softc *sc = container_of(work, struct ath_softc,
					    ath_led_blink_work.work);

	if (!(sc->sc_flags & SC_OP_LED_ASSOCIATED))
		return;

	if ((sc->led_on_duration == ATH_LED_ON_DURATION_IDLE) ||
	    (sc->led_off_duration == ATH_LED_OFF_DURATION_IDLE))
		ath9k_hw_set_gpio(sc->sc_ah, sc->sc_ah->led_pin, 0);
	else
		ath9k_hw_set_gpio(sc->sc_ah, sc->sc_ah->led_pin,
				  (sc->sc_flags & SC_OP_LED_ON) ? 1 : 0);

	ieee80211_queue_delayed_work(sc->hw,
				     &sc->ath_led_blink_work,
				     (sc->sc_flags & SC_OP_LED_ON) ?
					msecs_to_jiffies(sc->led_off_duration) :
					msecs_to_jiffies(sc->led_on_duration));

	sc->led_on_duration = sc->led_on_cnt ?
			max((ATH_LED_ON_DURATION_IDLE - sc->led_on_cnt), 25) :
			ATH_LED_ON_DURATION_IDLE;
	sc->led_off_duration = sc->led_off_cnt ?
			max((ATH_LED_OFF_DURATION_IDLE - sc->led_off_cnt), 10) :
			ATH_LED_OFF_DURATION_IDLE;
	sc->led_on_cnt = sc->led_off_cnt = 0;
	if (sc->sc_flags & SC_OP_LED_ON)
		sc->sc_flags &= ~SC_OP_LED_ON;
	else
		sc->sc_flags |= SC_OP_LED_ON;
}

static void ath_led_brightness(struct led_classdev *led_cdev,
			       enum led_brightness brightness)
{
	struct ath_led *led = container_of(led_cdev, struct ath_led, led_cdev);
	struct ath_softc *sc = led->sc;

	switch (brightness) {
	case LED_OFF:
		if (led->led_type == ATH_LED_ASSOC ||
		    led->led_type == ATH_LED_RADIO) {
			ath9k_hw_set_gpio(sc->sc_ah, sc->sc_ah->led_pin,
				(led->led_type == ATH_LED_RADIO));
			sc->sc_flags &= ~SC_OP_LED_ASSOCIATED;
			if (led->led_type == ATH_LED_RADIO)
				sc->sc_flags &= ~SC_OP_LED_ON;
		} else {
			sc->led_off_cnt++;
		}
		break;
	case LED_FULL:
		if (led->led_type == ATH_LED_ASSOC) {
			sc->sc_flags |= SC_OP_LED_ASSOCIATED;
			ieee80211_queue_delayed_work(sc->hw,
						     &sc->ath_led_blink_work, 0);
		} else if (led->led_type == ATH_LED_RADIO) {
			ath9k_hw_set_gpio(sc->sc_ah, sc->sc_ah->led_pin, 0);
			sc->sc_flags |= SC_OP_LED_ON;
		} else {
			sc->led_on_cnt++;
		}
		break;
	default:
		break;
	}
}

static int ath_register_led(struct ath_softc *sc, struct ath_led *led,
			    char *trigger)
{
	int ret;

	led->sc = sc;
	led->led_cdev.name = led->name;
	led->led_cdev.default_trigger = trigger;
	led->led_cdev.brightness_set = ath_led_brightness;

	ret = led_classdev_register(wiphy_dev(sc->hw->wiphy), &led->led_cdev);
	if (ret)
		DPRINTF(sc, ATH_DBG_FATAL,
			"Failed to register led:%s", led->name);
	else
		led->registered = 1;
	return ret;
}

static void ath_unregister_led(struct ath_led *led)
{
	if (led->registered) {
		led_classdev_unregister(&led->led_cdev);
		led->registered = 0;
	}
}

static void ath_deinit_leds(struct ath_softc *sc)
{
	ath_unregister_led(&sc->assoc_led);
	sc->sc_flags &= ~SC_OP_LED_ASSOCIATED;
	ath_unregister_led(&sc->tx_led);
	ath_unregister_led(&sc->rx_led);
	ath_unregister_led(&sc->radio_led);
	ath9k_hw_set_gpio(sc->sc_ah, sc->sc_ah->led_pin, 1);
}

static void ath_init_leds(struct ath_softc *sc)
{
	char *trigger;
	int ret;

	if (AR_SREV_9287(sc->sc_ah))
		sc->sc_ah->led_pin = ATH_LED_PIN_9287;
	else
		sc->sc_ah->led_pin = ATH_LED_PIN_DEF;

	/* Configure gpio 1 for output */
	ath9k_hw_cfg_output(sc->sc_ah, sc->sc_ah->led_pin,
			    AR_GPIO_OUTPUT_MUX_AS_OUTPUT);
	/* LED off, active low */
	ath9k_hw_set_gpio(sc->sc_ah, sc->sc_ah->led_pin, 1);

	INIT_DELAYED_WORK(&sc->ath_led_blink_work, ath_led_blink_work);

	trigger = ieee80211_get_radio_led_name(sc->hw);
	snprintf(sc->radio_led.name, sizeof(sc->radio_led.name),
		"ath9k-%s::radio", wiphy_name(sc->hw->wiphy));
	ret = ath_register_led(sc, &sc->radio_led, trigger);
	sc->radio_led.led_type = ATH_LED_RADIO;
	if (ret)
		goto fail;

	trigger = ieee80211_get_assoc_led_name(sc->hw);
	snprintf(sc->assoc_led.name, sizeof(sc->assoc_led.name),
		"ath9k-%s::assoc", wiphy_name(sc->hw->wiphy));
	ret = ath_register_led(sc, &sc->assoc_led, trigger);
	sc->assoc_led.led_type = ATH_LED_ASSOC;
	if (ret)
		goto fail;

	trigger = ieee80211_get_tx_led_name(sc->hw);
	snprintf(sc->tx_led.name, sizeof(sc->tx_led.name),
		"ath9k-%s::tx", wiphy_name(sc->hw->wiphy));
	ret = ath_register_led(sc, &sc->tx_led, trigger);
	sc->tx_led.led_type = ATH_LED_TX;
	if (ret)
		goto fail;

	trigger = ieee80211_get_rx_led_name(sc->hw);
	snprintf(sc->rx_led.name, sizeof(sc->rx_led.name),
		"ath9k-%s::rx", wiphy_name(sc->hw->wiphy));
	ret = ath_register_led(sc, &sc->rx_led, trigger);
	sc->rx_led.led_type = ATH_LED_RX;
	if (ret)
		goto fail;

	return;

fail:
	cancel_delayed_work_sync(&sc->ath_led_blink_work);
	ath_deinit_leds(sc);
}

void ath_radio_enable(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ieee80211_channel *channel = sc->hw->conf.channel;
	int r;

	ath9k_ps_wakeup(sc);
	ath9k_hw_configpcipowersave(ah, 0, 0);

	if (!ah->curchan)
		ah->curchan = ath_get_curchannel(sc, sc->hw);

	spin_lock_bh(&sc->sc_resetlock);
	r = ath9k_hw_reset(ah, ah->curchan, false);
	if (r) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to reset channel %u (%uMhz) ",
			"reset status %d\n",
			channel->center_freq, r);
	}
	spin_unlock_bh(&sc->sc_resetlock);

	ath_update_txpow(sc);
	if (ath_startrecv(sc) != 0) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to restart recv logic\n");
		return;
	}

	if (sc->sc_flags & SC_OP_BEACONS)
		ath_beacon_config(sc, NULL);	/* restart beacons */

	/* Re-Enable  interrupts */
	ath9k_hw_set_interrupts(ah, sc->imask);

	/* Enable LED */
	ath9k_hw_cfg_output(ah, ah->led_pin,
			    AR_GPIO_OUTPUT_MUX_AS_OUTPUT);
	ath9k_hw_set_gpio(ah, ah->led_pin, 0);

	ieee80211_wake_queues(sc->hw);
	ath9k_ps_restore(sc);
}

void ath_radio_disable(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ieee80211_channel *channel = sc->hw->conf.channel;
	int r;

	ath9k_ps_wakeup(sc);
	ieee80211_stop_queues(sc->hw);

	/* Disable LED */
	ath9k_hw_set_gpio(ah, ah->led_pin, 1);
	ath9k_hw_cfg_gpio_input(ah, ah->led_pin);

	/* Disable interrupts */
	ath9k_hw_set_interrupts(ah, 0);

	ath_drain_all_txq(sc, false);	/* clear pending tx frames */
	ath_stoprecv(sc);		/* turn off frame recv */
	ath_flushrecv(sc);		/* flush recv queue */

	if (!ah->curchan)
		ah->curchan = ath_get_curchannel(sc, sc->hw);

	spin_lock_bh(&sc->sc_resetlock);
	r = ath9k_hw_reset(ah, ah->curchan, false);
	if (r) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to reset channel %u (%uMhz) "
			"reset status %d\n",
			channel->center_freq, r);
	}
	spin_unlock_bh(&sc->sc_resetlock);

	ath9k_hw_phy_disable(ah);
	ath9k_hw_configpcipowersave(ah, 1, 1);
	ath9k_ps_restore(sc);
	ath9k_hw_setpower(ah, ATH9K_PM_FULL_SLEEP);
}

/*******************/
/*	Rfkill	   */
/*******************/

static bool ath_is_rfkill_set(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;

	return ath9k_hw_gpio_get(ah, ah->rfkill_gpio) ==
				  ah->rfkill_polarity;
}

static void ath9k_rfkill_poll_state(struct ieee80211_hw *hw)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	bool blocked = !!ath_is_rfkill_set(sc);

	wiphy_rfkill_set_hw_state(hw->wiphy, blocked);
}

static void ath_start_rfkill_poll(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;

	if (ah->caps.hw_caps & ATH9K_HW_CAP_RFSILENT)
		wiphy_rfkill_start_polling(sc->hw->wiphy);
}

void ath_cleanup(struct ath_softc *sc)
{
	ath_detach(sc);
	free_irq(sc->irq, sc);
	ath_bus_cleanup(sc);
	kfree(sc->sec_wiphy);
	ieee80211_free_hw(sc->hw);
}

void ath_detach(struct ath_softc *sc)
{
	struct ieee80211_hw *hw = sc->hw;
	int i = 0;

	ath9k_ps_wakeup(sc);

	DPRINTF(sc, ATH_DBG_CONFIG, "Detach ATH hw\n");

	ath_deinit_leds(sc);
	wiphy_rfkill_stop_polling(sc->hw->wiphy);

	for (i = 0; i < sc->num_sec_wiphy; i++) {
		struct ath_wiphy *aphy = sc->sec_wiphy[i];
		if (aphy == NULL)
			continue;
		sc->sec_wiphy[i] = NULL;
		ieee80211_unregister_hw(aphy->hw);
		ieee80211_free_hw(aphy->hw);
	}
	ieee80211_unregister_hw(hw);
	ath_rx_cleanup(sc);
	ath_tx_cleanup(sc);

	tasklet_kill(&sc->intr_tq);
	tasklet_kill(&sc->bcon_tasklet);

	if (!(sc->sc_flags & SC_OP_INVALID))
		ath9k_hw_setpower(sc->sc_ah, ATH9K_PM_AWAKE);

	/* cleanup tx queues */
	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++)
		if (ATH_TXQ_SETUP(sc, i))
			ath_tx_cleanupq(sc, &sc->tx.txq[i]);

	if ((sc->btcoex_info.no_stomp_timer) &&
	    sc->btcoex_info.btcoex_scheme == ATH_BTCOEX_CFG_3WIRE)
		ath_gen_timer_free(sc->sc_ah, sc->btcoex_info.no_stomp_timer);

	ath9k_hw_detach(sc->sc_ah);
	sc->sc_ah = NULL;
	ath9k_exit_debug(sc);
}

static int ath9k_reg_notifier(struct wiphy *wiphy,
			      struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath_regulatory *reg = &sc->common.regulatory;

	return ath_reg_notifier_apply(wiphy, request, reg);
}

/*
 * Initialize and fill ath_softc, ath_sofct is the
 * "Software Carrier" struct. Historically it has existed
 * to allow the separation between hardware specific
 * variables (now in ath_hw) and driver specific variables.
 */
static int ath_init_softc(u16 devid, struct ath_softc *sc, u16 subsysid)
{
	struct ath_hw *ah = NULL;
	int r = 0, i;
	int csz = 0;

	/* XXX: hardware will not be ready until ath_open() being called */
	sc->sc_flags |= SC_OP_INVALID;

	if (ath9k_init_debug(sc) < 0)
		printk(KERN_ERR "Unable to create debugfs files\n");

	spin_lock_init(&sc->wiphy_lock);
	spin_lock_init(&sc->sc_resetlock);
	spin_lock_init(&sc->sc_serial_rw);
	spin_lock_init(&sc->ani_lock);
	spin_lock_init(&sc->sc_pm_lock);
	mutex_init(&sc->mutex);
	tasklet_init(&sc->intr_tq, ath9k_tasklet, (unsigned long)sc);
	tasklet_init(&sc->bcon_tasklet, ath_beacon_tasklet,
		     (unsigned long)sc);

	/*
	 * Cache line size is used to size and align various
	 * structures used to communicate with the hardware.
	 */
	ath_read_cachesize(sc, &csz);
	/* XXX assert csz is non-zero */
	sc->common.cachelsz = csz << 2;	/* convert to bytes */

	ah = kzalloc(sizeof(struct ath_hw), GFP_KERNEL);
	if (!ah) {
		r = -ENOMEM;
		goto bad_no_ah;
	}

	ah->ah_sc = sc;
	ah->hw_version.devid = devid;
	ah->hw_version.subsysid = subsysid;
	sc->sc_ah = ah;

	r = ath9k_hw_init(ah);
	if (r) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to initialize hardware; "
			"initialization status: %d\n", r);
		goto bad;
	}

	/* Get the hardware key cache size. */
	sc->keymax = ah->caps.keycache_size;
	if (sc->keymax > ATH_KEYMAX) {
		DPRINTF(sc, ATH_DBG_ANY,
			"Warning, using only %u entries in %u key cache\n",
			ATH_KEYMAX, sc->keymax);
		sc->keymax = ATH_KEYMAX;
	}

	/*
	 * Reset the key cache since some parts do not
	 * reset the contents on initial power up.
	 */
	for (i = 0; i < sc->keymax; i++)
		ath9k_hw_keyreset(ah, (u16) i);

	/* default to MONITOR mode */
	sc->sc_ah->opmode = NL80211_IFTYPE_MONITOR;

	/* Setup rate tables */

	ath_rate_attach(sc);
	ath_setup_rates(sc, IEEE80211_BAND_2GHZ);
	ath_setup_rates(sc, IEEE80211_BAND_5GHZ);

	/*
	 * Allocate hardware transmit queues: one queue for
	 * beacon frames and one data queue for each QoS
	 * priority.  Note that the hal handles reseting
	 * these queues at the needed time.
	 */
	sc->beacon.beaconq = ath_beaconq_setup(ah);
	if (sc->beacon.beaconq == -1) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to setup a beacon xmit queue\n");
		r = -EIO;
		goto bad2;
	}
	sc->beacon.cabq = ath_txq_setup(sc, ATH9K_TX_QUEUE_CAB, 0);
	if (sc->beacon.cabq == NULL) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to setup CAB xmit queue\n");
		r = -EIO;
		goto bad2;
	}

	sc->config.cabqReadytime = ATH_CABQ_READY_TIME;
	ath_cabq_update(sc);

	for (i = 0; i < ARRAY_SIZE(sc->tx.hwq_map); i++)
		sc->tx.hwq_map[i] = -1;

	/* Setup data queues */
	/* NB: ensure BK queue is the lowest priority h/w queue */
	if (!ath_tx_setup(sc, ATH9K_WME_AC_BK)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to setup xmit queue for BK traffic\n");
		r = -EIO;
		goto bad2;
	}

	if (!ath_tx_setup(sc, ATH9K_WME_AC_BE)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to setup xmit queue for BE traffic\n");
		r = -EIO;
		goto bad2;
	}
	if (!ath_tx_setup(sc, ATH9K_WME_AC_VI)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to setup xmit queue for VI traffic\n");
		r = -EIO;
		goto bad2;
	}
	if (!ath_tx_setup(sc, ATH9K_WME_AC_VO)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to setup xmit queue for VO traffic\n");
		r = -EIO;
		goto bad2;
	}

	/* Initializes the noise floor to a reasonable default value.
	 * Later on this will be updated during ANI processing. */

	sc->ani.noise_floor = ATH_DEFAULT_NOISE_FLOOR;
	setup_timer(&sc->ani.timer, ath_ani_calibrate, (unsigned long)sc);

	if (ath9k_hw_getcapability(ah, ATH9K_CAP_CIPHER,
				   ATH9K_CIPHER_TKIP, NULL)) {
		/*
		 * Whether we should enable h/w TKIP MIC.
		 * XXX: if we don't support WME TKIP MIC, then we wouldn't
		 * report WMM capable, so it's always safe to turn on
		 * TKIP MIC in this case.
		 */
		ath9k_hw_setcapability(sc->sc_ah, ATH9K_CAP_TKIP_MIC,
				       0, 1, NULL);
	}

	/*
	 * Check whether the separate key cache entries
	 * are required to handle both tx+rx MIC keys.
	 * With split mic keys the number of stations is limited
	 * to 27 otherwise 59.
	 */
	if (ath9k_hw_getcapability(ah, ATH9K_CAP_CIPHER,
				   ATH9K_CIPHER_TKIP, NULL)
	    && ath9k_hw_getcapability(ah, ATH9K_CAP_CIPHER,
				      ATH9K_CIPHER_MIC, NULL)
	    && ath9k_hw_getcapability(ah, ATH9K_CAP_TKIP_SPLIT,
				      0, NULL))
		sc->splitmic = 1;

	/* turn on mcast key search if possible */
	if (!ath9k_hw_getcapability(ah, ATH9K_CAP_MCAST_KEYSRCH, 0, NULL))
		(void)ath9k_hw_setcapability(ah, ATH9K_CAP_MCAST_KEYSRCH, 1,
					     1, NULL);

	sc->config.txpowlimit = ATH_TXPOWER_MAX;

	/* 11n Capabilities */
	if (ah->caps.hw_caps & ATH9K_HW_CAP_HT) {
		sc->sc_flags |= SC_OP_TXAGGR;
		sc->sc_flags |= SC_OP_RXAGGR;
	}

	sc->tx_chainmask = ah->caps.tx_chainmask;
	sc->rx_chainmask = ah->caps.rx_chainmask;

	ath9k_hw_setcapability(ah, ATH9K_CAP_DIVERSITY, 1, true, NULL);
	sc->rx.defant = ath9k_hw_getdefantenna(ah);

	if (ah->caps.hw_caps & ATH9K_HW_CAP_BSSIDMASK)
		memcpy(sc->bssidmask, ath_bcast_mac, ETH_ALEN);

	sc->beacon.slottime = ATH9K_SLOT_TIME_9;	/* default to short slot time */

	/* initialize beacon slots */
	for (i = 0; i < ARRAY_SIZE(sc->beacon.bslot); i++) {
		sc->beacon.bslot[i] = NULL;
		sc->beacon.bslot_aphy[i] = NULL;
	}

	/* setup channels and rates */

	sc->sbands[IEEE80211_BAND_2GHZ].channels = ath9k_2ghz_chantable;
	sc->sbands[IEEE80211_BAND_2GHZ].bitrates =
		sc->rates[IEEE80211_BAND_2GHZ];
	sc->sbands[IEEE80211_BAND_2GHZ].band = IEEE80211_BAND_2GHZ;
	sc->sbands[IEEE80211_BAND_2GHZ].n_channels =
		ARRAY_SIZE(ath9k_2ghz_chantable);

	if (test_bit(ATH9K_MODE_11A, sc->sc_ah->caps.wireless_modes)) {
		sc->sbands[IEEE80211_BAND_5GHZ].channels = ath9k_5ghz_chantable;
		sc->sbands[IEEE80211_BAND_5GHZ].bitrates =
			sc->rates[IEEE80211_BAND_5GHZ];
		sc->sbands[IEEE80211_BAND_5GHZ].band = IEEE80211_BAND_5GHZ;
		sc->sbands[IEEE80211_BAND_5GHZ].n_channels =
			ARRAY_SIZE(ath9k_5ghz_chantable);
	}

	if (sc->btcoex_info.btcoex_scheme != ATH_BTCOEX_CFG_NONE) {
		r = ath9k_hw_btcoex_init(ah);
		if (r)
			goto bad2;
	}

	return 0;
bad2:
	/* cleanup tx queues */
	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++)
		if (ATH_TXQ_SETUP(sc, i))
			ath_tx_cleanupq(sc, &sc->tx.txq[i]);
bad:
	ath9k_hw_detach(ah);
	sc->sc_ah = NULL;
bad_no_ah:
	ath9k_exit_debug(sc);

	return r;
}

void ath_set_hw_capab(struct ath_softc *sc, struct ieee80211_hw *hw)
{
	struct ath_hw *ah = sc->sc_ah;

	hw->flags = IEEE80211_HW_RX_INCLUDES_FCS |
		IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING |
		IEEE80211_HW_SIGNAL_DBM |
		IEEE80211_HW_SUPPORTS_PS |
		IEEE80211_HW_PS_NULLFUNC_STACK |
		IEEE80211_HW_REPORTS_TX_ACK_STATUS |
		IEEE80211_HW_SPECTRUM_MGMT;

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_HT)
		 hw->flags |= IEEE80211_HW_AMPDU_AGGREGATION;

	if (AR_SREV_9160_10_OR_LATER(sc->sc_ah) || modparam_nohwcrypt)
		hw->flags |= IEEE80211_HW_MFP_CAPABLE;

	hw->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_AP) |
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC) |
		BIT(NL80211_IFTYPE_MESH_POINT);

	if (AR_SREV_5416(ah))
		hw->wiphy->ps_default = false;
	else
		hw->wiphy->ps_default = true;

	hw->queues = 4;
	hw->max_rates = 4;
	hw->channel_change_time = 5000;
	hw->max_listen_interval = 10;
	/* Hardware supports 10 but we use 4 */
	hw->max_rate_tries = 4;
	hw->sta_data_size = sizeof(struct ath_node);
	hw->vif_data_size = sizeof(struct ath_vif);

	hw->rate_control_algorithm = "ath9k_rate_control";

	hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
		&sc->sbands[IEEE80211_BAND_2GHZ];
	if (test_bit(ATH9K_MODE_11A, sc->sc_ah->caps.wireless_modes))
		hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
			&sc->sbands[IEEE80211_BAND_5GHZ];
}

/* Device driver core initialization */
int ath_init_device(u16 devid, struct ath_softc *sc, u16 subsysid)
{
	struct ieee80211_hw *hw = sc->hw;
	int error = 0, i;
	struct ath_regulatory *reg;

	DPRINTF(sc, ATH_DBG_CONFIG, "Attach ATH hw\n");

	error = ath_init_softc(devid, sc, subsysid);
	if (error != 0)
		return error;

	/* get mac address from hardware and set in mac80211 */

	SET_IEEE80211_PERM_ADDR(hw, sc->sc_ah->macaddr);

	ath_set_hw_capab(sc, hw);

	error = ath_regd_init(&sc->common.regulatory, sc->hw->wiphy,
			      ath9k_reg_notifier);
	if (error)
		return error;

	reg = &sc->common.regulatory;

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_HT) {
		setup_ht_cap(sc, &sc->sbands[IEEE80211_BAND_2GHZ].ht_cap);
		if (test_bit(ATH9K_MODE_11A, sc->sc_ah->caps.wireless_modes))
			setup_ht_cap(sc, &sc->sbands[IEEE80211_BAND_5GHZ].ht_cap);
	}

	/* initialize tx/rx engine */
	error = ath_tx_init(sc, ATH_TXBUF);
	if (error != 0)
		goto error_attach;

	error = ath_rx_init(sc, ATH_RXBUF);
	if (error != 0)
		goto error_attach;

	INIT_WORK(&sc->chan_work, ath9k_wiphy_chan_work);
	INIT_DELAYED_WORK(&sc->wiphy_work, ath9k_wiphy_work);
	sc->wiphy_scheduler_int = msecs_to_jiffies(500);

	error = ieee80211_register_hw(hw);

	if (!ath_is_world_regd(reg)) {
		error = regulatory_hint(hw->wiphy, reg->alpha2);
		if (error)
			goto error_attach;
	}

	/* Initialize LED control */
	ath_init_leds(sc);

	ath_start_rfkill_poll(sc);

	return 0;

error_attach:
	/* cleanup tx queues */
	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++)
		if (ATH_TXQ_SETUP(sc, i))
			ath_tx_cleanupq(sc, &sc->tx.txq[i]);

	ath9k_hw_detach(sc->sc_ah);
	sc->sc_ah = NULL;
	ath9k_exit_debug(sc);

	return error;
}

int ath_reset(struct ath_softc *sc, bool retry_tx)
{
	struct ath_hw *ah = sc->sc_ah;
	struct ieee80211_hw *hw = sc->hw;
	int r;

	ath9k_hw_set_interrupts(ah, 0);
	ath_drain_all_txq(sc, retry_tx);
	ath_stoprecv(sc);
	ath_flushrecv(sc);

	spin_lock_bh(&sc->sc_resetlock);
	r = ath9k_hw_reset(ah, sc->sc_ah->curchan, false);
	if (r)
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to reset hardware; reset status %d\n", r);
	spin_unlock_bh(&sc->sc_resetlock);

	if (ath_startrecv(sc) != 0)
		DPRINTF(sc, ATH_DBG_FATAL, "Unable to start recv logic\n");

	/*
	 * We may be doing a reset in response to a request
	 * that changes the channel so update any state that
	 * might change as a result.
	 */
	ath_cache_conf_rate(sc, &hw->conf);

	ath_update_txpow(sc);

	if (sc->sc_flags & SC_OP_BEACONS)
		ath_beacon_config(sc, NULL);	/* restart beacons */

	ath9k_hw_set_interrupts(ah, sc->imask);

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

	return r;
}

/*
 *  This function will allocate both the DMA descriptor structure, and the
 *  buffers it contains.  These are used to contain the descriptors used
 *  by the system.
*/
int ath_descdma_setup(struct ath_softc *sc, struct ath_descdma *dd,
		      struct list_head *head, const char *name,
		      int nbuf, int ndesc)
{
#define	DS2PHYS(_dd, _ds)						\
	((_dd)->dd_desc_paddr + ((caddr_t)(_ds) - (caddr_t)(_dd)->dd_desc))
#define ATH_DESC_4KB_BOUND_CHECK(_daddr) ((((_daddr) & 0xFFF) > 0xF7F) ? 1 : 0)
#define ATH_DESC_4KB_BOUND_NUM_SKIPPED(_len) ((_len) / 4096)

	struct ath_desc *ds;
	struct ath_buf *bf;
	int i, bsize, error;

	DPRINTF(sc, ATH_DBG_CONFIG, "%s DMA: %u buffers %u desc/buf\n",
		name, nbuf, ndesc);

	INIT_LIST_HEAD(head);
	/* ath_desc must be a multiple of DWORDs */
	if ((sizeof(struct ath_desc) % 4) != 0) {
		DPRINTF(sc, ATH_DBG_FATAL, "ath_desc not DWORD aligned\n");
		ASSERT((sizeof(struct ath_desc) % 4) == 0);
		error = -ENOMEM;
		goto fail;
	}

	dd->dd_desc_len = sizeof(struct ath_desc) * nbuf * ndesc;

	/*
	 * Need additional DMA memory because we can't use
	 * descriptors that cross the 4K page boundary. Assume
	 * one skipped descriptor per 4K page.
	 */
	if (!(sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_4KB_SPLITTRANS)) {
		u32 ndesc_skipped =
			ATH_DESC_4KB_BOUND_NUM_SKIPPED(dd->dd_desc_len);
		u32 dma_len;

		while (ndesc_skipped) {
			dma_len = ndesc_skipped * sizeof(struct ath_desc);
			dd->dd_desc_len += dma_len;

			ndesc_skipped = ATH_DESC_4KB_BOUND_NUM_SKIPPED(dma_len);
		};
	}

	/* allocate descriptors */
	dd->dd_desc = dma_alloc_coherent(sc->dev, dd->dd_desc_len,
					 &dd->dd_desc_paddr, GFP_KERNEL);
	if (dd->dd_desc == NULL) {
		error = -ENOMEM;
		goto fail;
	}
	ds = dd->dd_desc;
	DPRINTF(sc, ATH_DBG_CONFIG, "%s DMA map: %p (%u) -> %llx (%u)\n",
		name, ds, (u32) dd->dd_desc_len,
		ito64(dd->dd_desc_paddr), /*XXX*/(u32) dd->dd_desc_len);

	/* allocate buffers */
	bsize = sizeof(struct ath_buf) * nbuf;
	bf = kzalloc(bsize, GFP_KERNEL);
	if (bf == NULL) {
		error = -ENOMEM;
		goto fail2;
	}
	dd->dd_bufptr = bf;

	for (i = 0; i < nbuf; i++, bf++, ds += ndesc) {
		bf->bf_desc = ds;
		bf->bf_daddr = DS2PHYS(dd, ds);

		if (!(sc->sc_ah->caps.hw_caps &
		      ATH9K_HW_CAP_4KB_SPLITTRANS)) {
			/*
			 * Skip descriptor addresses which can cause 4KB
			 * boundary crossing (addr + length) with a 32 dword
			 * descriptor fetch.
			 */
			while (ATH_DESC_4KB_BOUND_CHECK(bf->bf_daddr)) {
				ASSERT((caddr_t) bf->bf_desc <
				       ((caddr_t) dd->dd_desc +
					dd->dd_desc_len));

				ds += ndesc;
				bf->bf_desc = ds;
				bf->bf_daddr = DS2PHYS(dd, ds);
			}
		}
		list_add_tail(&bf->list, head);
	}
	return 0;
fail2:
	dma_free_coherent(sc->dev, dd->dd_desc_len, dd->dd_desc,
			  dd->dd_desc_paddr);
fail:
	memset(dd, 0, sizeof(*dd));
	return error;
#undef ATH_DESC_4KB_BOUND_CHECK
#undef ATH_DESC_4KB_BOUND_NUM_SKIPPED
#undef DS2PHYS
}

void ath_descdma_cleanup(struct ath_softc *sc,
			 struct ath_descdma *dd,
			 struct list_head *head)
{
	dma_free_coherent(sc->dev, dd->dd_desc_len, dd->dd_desc,
			  dd->dd_desc_paddr);

	INIT_LIST_HEAD(head);
	kfree(dd->dd_bufptr);
	memset(dd, 0, sizeof(*dd));
}

int ath_get_hal_qnum(u16 queue, struct ath_softc *sc)
{
	int qnum;

	switch (queue) {
	case 0:
		qnum = sc->tx.hwq_map[ATH9K_WME_AC_VO];
		break;
	case 1:
		qnum = sc->tx.hwq_map[ATH9K_WME_AC_VI];
		break;
	case 2:
		qnum = sc->tx.hwq_map[ATH9K_WME_AC_BE];
		break;
	case 3:
		qnum = sc->tx.hwq_map[ATH9K_WME_AC_BK];
		break;
	default:
		qnum = sc->tx.hwq_map[ATH9K_WME_AC_BE];
		break;
	}

	return qnum;
}

int ath_get_mac80211_qnum(u32 queue, struct ath_softc *sc)
{
	int qnum;

	switch (queue) {
	case ATH9K_WME_AC_VO:
		qnum = 0;
		break;
	case ATH9K_WME_AC_VI:
		qnum = 1;
		break;
	case ATH9K_WME_AC_BE:
		qnum = 2;
		break;
	case ATH9K_WME_AC_BK:
		qnum = 3;
		break;
	default:
		qnum = -1;
		break;
	}

	return qnum;
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

	sc->tx_chan_width = ATH9K_HT_MACMODE_20;

	if (conf_is_ht(conf)) {
		if (conf_is_ht40(conf))
			sc->tx_chan_width = ATH9K_HT_MACMODE_2040;

		ichan->chanmode = ath_get_extchanmode(sc, chan,
					    conf->channel_type);
	}
}

/**********************/
/* mac80211 callbacks */
/**********************/

static int ath9k_start(struct ieee80211_hw *hw)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ieee80211_channel *curchan = hw->conf.channel;
	struct ath9k_channel *init_channel;
	int r;

	DPRINTF(sc, ATH_DBG_CONFIG, "Starting driver with "
		"initial channel: %d MHz\n", curchan->center_freq);

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
	ath9k_hw_configpcipowersave(sc->sc_ah, 0, 0);

	/*
	 * The basic interface to setting the hardware in a good
	 * state is ``reset''.  On return the hardware is known to
	 * be powered up and with interrupts disabled.  This must
	 * be followed by initialization of the appropriate bits
	 * and then setup of the interrupt mask.
	 */
	spin_lock_bh(&sc->sc_resetlock);
	r = ath9k_hw_reset(sc->sc_ah, init_channel, false);
	if (r) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to reset hardware; reset status %d "
			"(freq %u MHz)\n", r,
			curchan->center_freq);
		spin_unlock_bh(&sc->sc_resetlock);
		goto mutex_unlock;
	}
	spin_unlock_bh(&sc->sc_resetlock);

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
		DPRINTF(sc, ATH_DBG_FATAL, "Unable to start recv logic\n");
		r = -EIO;
		goto mutex_unlock;
	}

	/* Setup our intr mask. */
	sc->imask = ATH9K_INT_RX | ATH9K_INT_TX
		| ATH9K_INT_RXEOL | ATH9K_INT_RXORN
		| ATH9K_INT_FATAL | ATH9K_INT_GLOBAL;

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_GTT)
		sc->imask |= ATH9K_INT_GTT;

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_HT)
		sc->imask |= ATH9K_INT_CST;

	ath_cache_conf_rate(sc, &hw->conf);

	sc->sc_flags &= ~SC_OP_INVALID;

	/* Disable BMISS interrupt when we're not associated */
	sc->imask &= ~(ATH9K_INT_SWBA | ATH9K_INT_BMISS);
	ath9k_hw_set_interrupts(sc->sc_ah, sc->imask);

	ieee80211_wake_queues(hw);

	ieee80211_queue_delayed_work(sc->hw, &sc->tx_complete_work, 0);

	if ((sc->btcoex_info.btcoex_scheme != ATH_BTCOEX_CFG_NONE) &&
	    !(sc->sc_flags & SC_OP_BTCOEX_ENABLED)) {
		ath_btcoex_set_weight(&sc->btcoex_info, AR_BT_COEX_WGHT,
				      AR_STOMP_LOW_WLAN_WGHT);
		ath9k_hw_btcoex_enable(sc->sc_ah);

		ath_pcie_aspm_disable(sc);
		if (sc->btcoex_info.btcoex_scheme == ATH_BTCOEX_CFG_3WIRE)
			ath_btcoex_timer_resume(sc, &sc->btcoex_info);
	}

mutex_unlock:
	mutex_unlock(&sc->mutex);

	return r;
}

static int ath9k_tx(struct ieee80211_hw *hw,
		    struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath_tx_control txctl;
	int hdrlen, padsize;

	if (aphy->state != ATH_WIPHY_ACTIVE && aphy->state != ATH_WIPHY_SCAN) {
		printk(KERN_DEBUG "ath9k: %s: TX in unexpected wiphy state "
		       "%d\n", wiphy_name(hw->wiphy), aphy->state);
		goto exit;
	}

	if (sc->ps_enabled) {
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
		/*
		 * mac80211 does not set PM field for normal data frames, so we
		 * need to update that based on the current PS mode.
		 */
		if (ieee80211_is_data(hdr->frame_control) &&
		    !ieee80211_is_nullfunc(hdr->frame_control) &&
		    !ieee80211_has_pm(hdr->frame_control)) {
			DPRINTF(sc, ATH_DBG_PS, "Add PM=1 for a TX frame "
				"while in PS mode\n");
			hdr->frame_control |= cpu_to_le16(IEEE80211_FCTL_PM);
		}
	}

	if (unlikely(sc->sc_ah->power_mode != ATH9K_PM_AWAKE)) {
		/*
		 * We are using PS-Poll and mac80211 can request TX while in
		 * power save mode. Need to wake up hardware for the TX to be
		 * completed and if needed, also for RX of buffered frames.
		 */
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
		ath9k_ps_wakeup(sc);
		ath9k_hw_setrxabort(sc->sc_ah, 0);
		if (ieee80211_is_pspoll(hdr->frame_control)) {
			DPRINTF(sc, ATH_DBG_PS, "Sending PS-Poll to pick a "
				"buffered frame\n");
			sc->sc_flags |= SC_OP_WAIT_FOR_PSPOLL_DATA;
		} else {
			DPRINTF(sc, ATH_DBG_PS, "Wake up to complete TX\n");
			sc->sc_flags |= SC_OP_WAIT_FOR_TX_ACK;
		}
		/*
		 * The actual restore operation will happen only after
		 * the sc_flags bit is cleared. We are just dropping
		 * the ps_usecount here.
		 */
		ath9k_ps_restore(sc);
	}

	memset(&txctl, 0, sizeof(struct ath_tx_control));

	/*
	 * As a temporary workaround, assign seq# here; this will likely need
	 * to be cleaned up to work better with Beacon transmission and virtual
	 * BSSes.
	 */
	if (info->flags & IEEE80211_TX_CTL_ASSIGN_SEQ) {
		struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
		if (info->flags & IEEE80211_TX_CTL_FIRST_FRAGMENT)
			sc->tx.seq_no += 0x10;
		hdr->seq_ctrl &= cpu_to_le16(IEEE80211_SCTL_FRAG);
		hdr->seq_ctrl |= cpu_to_le16(sc->tx.seq_no);
	}

	/* Add the padding after the header if this is not already done */
	hdrlen = ieee80211_get_hdrlen_from_skb(skb);
	if (hdrlen & 3) {
		padsize = hdrlen % 4;
		if (skb_headroom(skb) < padsize)
			return -1;
		skb_push(skb, padsize);
		memmove(skb->data, skb->data + padsize, hdrlen);
	}

	/* Check if a tx queue is available */

	txctl.txq = ath_test_get_txq(sc, skb);
	if (!txctl.txq)
		goto exit;

	DPRINTF(sc, ATH_DBG_XMIT, "transmitting packet, skb: %p\n", skb);

	if (ath_tx_start(hw, skb, &txctl) != 0) {
		DPRINTF(sc, ATH_DBG_XMIT, "TX failed\n");
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

	mutex_lock(&sc->mutex);

	aphy->state = ATH_WIPHY_INACTIVE;

	cancel_delayed_work_sync(&sc->ath_led_blink_work);
	cancel_delayed_work_sync(&sc->tx_complete_work);

	if (!sc->num_sec_wiphy) {
		cancel_delayed_work_sync(&sc->wiphy_work);
		cancel_work_sync(&sc->chan_work);
	}

	if (sc->sc_flags & SC_OP_INVALID) {
		DPRINTF(sc, ATH_DBG_ANY, "Device not present\n");
		mutex_unlock(&sc->mutex);
		return;
	}

	if (ath9k_wiphy_started(sc)) {
		mutex_unlock(&sc->mutex);
		return; /* another wiphy still in use */
	}

	/* Ensure HW is awake when we try to shut it down. */
	ath9k_ps_wakeup(sc);

	if (sc->sc_flags & SC_OP_BTCOEX_ENABLED) {
		ath9k_hw_btcoex_disable(sc->sc_ah);
		if (sc->btcoex_info.btcoex_scheme == ATH_BTCOEX_CFG_3WIRE)
			ath_btcoex_timer_pause(sc, &sc->btcoex_info);
	}

	/* make sure h/w will not generate any interrupt
	 * before setting the invalid flag. */
	ath9k_hw_set_interrupts(sc->sc_ah, 0);

	if (!(sc->sc_flags & SC_OP_INVALID)) {
		ath_drain_all_txq(sc, false);
		ath_stoprecv(sc);
		ath9k_hw_phy_disable(sc->sc_ah);
	} else
		sc->rx.rxlink = NULL;

	/* disable HAL and put h/w to sleep */
	ath9k_hw_disable(sc->sc_ah);
	ath9k_hw_configpcipowersave(sc->sc_ah, 1, 1);
	ath9k_ps_restore(sc);

	/* Finally, put the chip in FULL SLEEP mode */
	ath9k_hw_setpower(sc->sc_ah, ATH9K_PM_FULL_SLEEP);

	sc->sc_flags |= SC_OP_INVALID;

	mutex_unlock(&sc->mutex);

	DPRINTF(sc, ATH_DBG_CONFIG, "Driver halt\n");
}

static int ath9k_add_interface(struct ieee80211_hw *hw,
			       struct ieee80211_if_init_conf *conf)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath_vif *avp = (void *)conf->vif->drv_priv;
	enum nl80211_iftype ic_opmode = NL80211_IFTYPE_UNSPECIFIED;
	int ret = 0;

	mutex_lock(&sc->mutex);

	if (!(sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_BSSIDMASK) &&
	    sc->nvifs > 0) {
		ret = -ENOBUFS;
		goto out;
	}

	switch (conf->type) {
	case NL80211_IFTYPE_STATION:
		ic_opmode = NL80211_IFTYPE_STATION;
		break;
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
		if (sc->nbcnvifs >= ATH_BCBUF) {
			ret = -ENOBUFS;
			goto out;
		}
		ic_opmode = conf->type;
		break;
	default:
		DPRINTF(sc, ATH_DBG_FATAL,
			"Interface type %d not yet supported\n", conf->type);
		ret = -EOPNOTSUPP;
		goto out;
	}

	DPRINTF(sc, ATH_DBG_CONFIG, "Attach a VIF of type: %d\n", ic_opmode);

	/* Set the VIF opmode */
	avp->av_opmode = ic_opmode;
	avp->av_bslot = -1;

	sc->nvifs++;

	if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_BSSIDMASK)
		ath9k_set_bssid_mask(hw);

	if (sc->nvifs > 1)
		goto out; /* skip global settings for secondary vif */

	if (ic_opmode == NL80211_IFTYPE_AP) {
		ath9k_hw_set_tsfadjust(sc->sc_ah, 1);
		sc->sc_flags |= SC_OP_TSF_RESET;
	}

	/* Set the device opmode */
	sc->sc_ah->opmode = ic_opmode;

	/*
	 * Enable MIB interrupts when there are hardware phy counters.
	 * Note we only do this (at the moment) for station mode.
	 */
	if ((conf->type == NL80211_IFTYPE_STATION) ||
	    (conf->type == NL80211_IFTYPE_ADHOC) ||
	    (conf->type == NL80211_IFTYPE_MESH_POINT)) {
		sc->imask |= ATH9K_INT_MIB;
		sc->imask |= ATH9K_INT_TSFOOR;
	}

	ath9k_hw_set_interrupts(sc->sc_ah, sc->imask);

	if (conf->type == NL80211_IFTYPE_AP    ||
	    conf->type == NL80211_IFTYPE_ADHOC ||
	    conf->type == NL80211_IFTYPE_MONITOR)
		ath_start_ani(sc);

out:
	mutex_unlock(&sc->mutex);
	return ret;
}

static void ath9k_remove_interface(struct ieee80211_hw *hw,
				   struct ieee80211_if_init_conf *conf)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath_vif *avp = (void *)conf->vif->drv_priv;
	int i;

	DPRINTF(sc, ATH_DBG_CONFIG, "Detach Interface\n");

	mutex_lock(&sc->mutex);

	/* Stop ANI */
	del_timer_sync(&sc->ani.timer);

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
		if (sc->beacon.bslot[i] == conf->vif) {
			printk(KERN_DEBUG "%s: vif had allocated beacon "
			       "slot\n", __func__);
			sc->beacon.bslot[i] = NULL;
			sc->beacon.bslot_aphy[i] = NULL;
		}
	}

	sc->nvifs--;

	mutex_unlock(&sc->mutex);
}

void ath9k_enable_ps(struct ath_softc *sc)
{
	sc->ps_enabled = true;
	if (!(sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_AUTOSLEEP)) {
		if ((sc->imask & ATH9K_INT_TIM_TIMER) == 0) {
			sc->imask |= ATH9K_INT_TIM_TIMER;
			ath9k_hw_set_interrupts(sc->sc_ah,
					sc->imask);
		}
	}
	ath9k_hw_setrxabort(sc->sc_ah, 1);
}

static int ath9k_config(struct ieee80211_hw *hw, u32 changed)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ieee80211_conf *conf = &hw->conf;
	struct ath_hw *ah = sc->sc_ah;
	bool all_wiphys_idle = false, disable_radio = false;

	mutex_lock(&sc->mutex);

	/* Leave this as the first check */
	if (changed & IEEE80211_CONF_CHANGE_IDLE) {

		spin_lock_bh(&sc->wiphy_lock);
		all_wiphys_idle =  ath9k_all_wiphys_idle(sc);
		spin_unlock_bh(&sc->wiphy_lock);

		if (conf->flags & IEEE80211_CONF_IDLE){
			if (all_wiphys_idle)
				disable_radio = true;
		}
		else if (all_wiphys_idle) {
			ath_radio_enable(sc);
			DPRINTF(sc, ATH_DBG_CONFIG,
				"not-idle: enabling radio\n");
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_PS) {
		if (conf->flags & IEEE80211_CONF_PS) {
			sc->sc_flags |= SC_OP_PS_ENABLED;
			if ((sc->sc_flags & SC_OP_NULLFUNC_COMPLETED)) {
				sc->sc_flags &= ~SC_OP_NULLFUNC_COMPLETED;
				ath9k_enable_ps(sc);
			}
		} else {
			sc->ps_enabled = false;
			sc->sc_flags &= ~(SC_OP_PS_ENABLED |
					  SC_OP_NULLFUNC_COMPLETED);
			ath9k_hw_setpower(sc->sc_ah, ATH9K_PM_AWAKE);
			if (!(ah->caps.hw_caps &
			      ATH9K_HW_CAP_AUTOSLEEP)) {
				ath9k_hw_setrxabort(sc->sc_ah, 0);
				sc->sc_flags &= ~(SC_OP_WAIT_FOR_BEACON |
						  SC_OP_WAIT_FOR_CAB |
						  SC_OP_WAIT_FOR_PSPOLL_DATA |
						  SC_OP_WAIT_FOR_TX_ACK);
				if (sc->imask & ATH9K_INT_TIM_TIMER) {
					sc->imask &= ~ATH9K_INT_TIM_TIMER;
					ath9k_hw_set_interrupts(sc->sc_ah,
							sc->imask);
				}
			}
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_CHANNEL) {
		struct ieee80211_channel *curchan = hw->conf.channel;
		int pos = curchan->hw_value;

		aphy->chan_idx = pos;
		aphy->chan_is_ht = conf_is_ht(conf);

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

		DPRINTF(sc, ATH_DBG_CONFIG, "Set channel: %d MHz\n",
			curchan->center_freq);

		/* XXX: remove me eventualy */
		ath9k_update_ichannel(sc, hw, &sc->sc_ah->channels[pos]);

		ath_update_chainmask(sc, conf_is_ht(conf));

		if (ath_set_channel(sc, hw, &sc->sc_ah->channels[pos]) < 0) {
			DPRINTF(sc, ATH_DBG_FATAL, "Unable to set channel\n");
			mutex_unlock(&sc->mutex);
			return -EINVAL;
		}
	}

skip_chan_change:
	if (changed & IEEE80211_CONF_CHANGE_POWER)
		sc->config.txpowlimit = 2 * conf->power_level;

	if (disable_radio) {
		DPRINTF(sc, ATH_DBG_CONFIG, "idle: disabling radio\n");
		ath_radio_disable(sc);
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

	DPRINTF(sc, ATH_DBG_CONFIG, "Set HW RX filter: 0x%x\n", rfilt);
}

static void ath9k_sta_notify(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     enum sta_notify_cmd cmd,
			     struct ieee80211_sta *sta)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;

	switch (cmd) {
	case STA_NOTIFY_ADD:
		ath_node_attach(sc, sta);
		break;
	case STA_NOTIFY_REMOVE:
		ath_node_detach(sc, sta);
		break;
	default:
		break;
	}
}

static int ath9k_conf_tx(struct ieee80211_hw *hw, u16 queue,
			 const struct ieee80211_tx_queue_params *params)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	struct ath9k_tx_queue_info qi;
	int ret = 0, qnum;

	if (queue >= WME_NUM_AC)
		return 0;

	mutex_lock(&sc->mutex);

	memset(&qi, 0, sizeof(struct ath9k_tx_queue_info));

	qi.tqi_aifs = params->aifs;
	qi.tqi_cwmin = params->cw_min;
	qi.tqi_cwmax = params->cw_max;
	qi.tqi_burstTime = params->txop;
	qnum = ath_get_hal_qnum(queue, sc);

	DPRINTF(sc, ATH_DBG_CONFIG,
		"Configure tx [queue/halq] [%d/%d],  "
		"aifs: %d, cw_min: %d, cw_max: %d, txop: %d\n",
		queue, qnum, params->aifs, params->cw_min,
		params->cw_max, params->txop);

	ret = ath_txq_update(sc, qnum, &qi);
	if (ret)
		DPRINTF(sc, ATH_DBG_FATAL, "TXQ Update failed\n");

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
	int ret = 0;

	if (modparam_nohwcrypt)
		return -ENOSPC;

	mutex_lock(&sc->mutex);
	ath9k_ps_wakeup(sc);
	DPRINTF(sc, ATH_DBG_CONFIG, "Set HW Key\n");

	switch (cmd) {
	case SET_KEY:
		ret = ath_key_config(sc, vif, sta, key);
		if (ret >= 0) {
			key->hw_key_idx = ret;
			/* push IV and Michael MIC generation to stack */
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
			if (key->alg == ALG_TKIP)
				key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
			if (sc->sc_ah->sw_mgmt_crypto && key->alg == ALG_CCMP)
				key->flags |= IEEE80211_KEY_FLAG_SW_MGMT;
			ret = 0;
		}
		break;
	case DISABLE_KEY:
		ath_key_delete(sc, key);
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
	struct ath_vif *avp = (void *)vif->drv_priv;
	u32 rfilt = 0;
	int error, i;

	mutex_lock(&sc->mutex);

	/*
	 * TODO: Need to decide which hw opmode to use for
	 *       multi-interface cases
	 * XXX: This belongs into add_interface!
	 */
	if (vif->type == NL80211_IFTYPE_AP &&
	    ah->opmode != NL80211_IFTYPE_AP) {
		ah->opmode = NL80211_IFTYPE_STATION;
		ath9k_hw_setopmode(ah);
		memcpy(sc->curbssid, sc->sc_ah->macaddr, ETH_ALEN);
		sc->curaid = 0;
		ath9k_hw_write_associd(sc);
		/* Request full reset to get hw opmode changed properly */
		sc->sc_flags |= SC_OP_FULL_RESET;
	}

	if ((changed & BSS_CHANGED_BSSID) &&
	    !is_zero_ether_addr(bss_conf->bssid)) {
		switch (vif->type) {
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_ADHOC:
		case NL80211_IFTYPE_MESH_POINT:
			/* Set BSSID */
			memcpy(sc->curbssid, bss_conf->bssid, ETH_ALEN);
			memcpy(avp->bssid, bss_conf->bssid, ETH_ALEN);
			sc->curaid = 0;
			ath9k_hw_write_associd(sc);

			/* Set aggregation protection mode parameters */
			sc->config.ath_aggr_prot = 0;

			DPRINTF(sc, ATH_DBG_CONFIG,
				"RX filter 0x%x bssid %pM aid 0x%x\n",
				rfilt, sc->curbssid, sc->curaid);

			/* need to reconfigure the beacon */
			sc->sc_flags &= ~SC_OP_BEACONS ;

			break;
		default:
			break;
		}
	}

	if ((vif->type == NL80211_IFTYPE_ADHOC) ||
	    (vif->type == NL80211_IFTYPE_AP) ||
	    (vif->type == NL80211_IFTYPE_MESH_POINT)) {
		if ((changed & BSS_CHANGED_BEACON) ||
		    (changed & BSS_CHANGED_BEACON_ENABLED &&
		     bss_conf->enable_beacon)) {
			/*
			 * Allocate and setup the beacon frame.
			 *
			 * Stop any previous beacon DMA.  This may be
			 * necessary, for example, when an ibss merge
			 * causes reconfiguration; we may be called
			 * with beacon transmission active.
			 */
			ath9k_hw_stoptxdma(sc->sc_ah, sc->beacon.beaconq);

			error = ath_beacon_alloc(aphy, vif);
			if (!error)
				ath_beacon_config(sc, vif);
		}
	}

	/* Check for WLAN_CAPABILITY_PRIVACY ? */
	if ((avp->av_opmode != NL80211_IFTYPE_STATION)) {
		for (i = 0; i < IEEE80211_WEP_NKID; i++)
			if (ath9k_hw_keyisvalid(sc->sc_ah, (u16)i))
				ath9k_hw_keysetmac(sc->sc_ah,
						   (u16)i,
						   sc->curbssid);
	}

	/* Only legacy IBSS for now */
	if (vif->type == NL80211_IFTYPE_ADHOC)
		ath_update_chainmask(sc, 0);

	if (changed & BSS_CHANGED_ERP_PREAMBLE) {
		DPRINTF(sc, ATH_DBG_CONFIG, "BSS Changed PREAMBLE %d\n",
			bss_conf->use_short_preamble);
		if (bss_conf->use_short_preamble)
			sc->sc_flags |= SC_OP_PREAMBLE_SHORT;
		else
			sc->sc_flags &= ~SC_OP_PREAMBLE_SHORT;
	}

	if (changed & BSS_CHANGED_ERP_CTS_PROT) {
		DPRINTF(sc, ATH_DBG_CONFIG, "BSS Changed CTS PROT %d\n",
			bss_conf->use_cts_prot);
		if (bss_conf->use_cts_prot &&
		    hw->conf.channel->band != IEEE80211_BAND_5GHZ)
			sc->sc_flags |= SC_OP_PROTECT_ENABLE;
		else
			sc->sc_flags &= ~SC_OP_PROTECT_ENABLE;
	}

	if (changed & BSS_CHANGED_ASSOC) {
		DPRINTF(sc, ATH_DBG_CONFIG, "BSS Changed ASSOC %d\n",
			bss_conf->assoc);
		ath9k_bss_assoc_info(sc, vif, bss_conf);
	}

	/*
	 * The HW TSF has to be reset when the beacon interval changes.
	 * We set the flag here, and ath_beacon_config_ap() would take this
	 * into account when it gets called through the subsequent
	 * config_interface() call - with IFCC_BEACON in the changed field.
	 */

	if (changed & BSS_CHANGED_BEACON_INT) {
		sc->sc_flags |= SC_OP_TSF_RESET;
		sc->beacon_interval = bss_conf->beacon_int;
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
	ath9k_hw_reset_tsf(sc->sc_ah);
	mutex_unlock(&sc->mutex);
}

static int ath9k_ampdu_action(struct ieee80211_hw *hw,
			      enum ieee80211_ampdu_mlme_action action,
			      struct ieee80211_sta *sta,
			      u16 tid, u16 *ssn)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	int ret = 0;

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		if (!(sc->sc_flags & SC_OP_RXAGGR))
			ret = -ENOTSUPP;
		break;
	case IEEE80211_AMPDU_RX_STOP:
		break;
	case IEEE80211_AMPDU_TX_START:
		ath9k_ps_wakeup(sc);
		ath_tx_aggr_start(sc, sta, tid, ssn);
		ieee80211_start_tx_ba_cb_irqsafe(hw, sta->addr, tid);
		ath9k_ps_restore(sc);
		break;
	case IEEE80211_AMPDU_TX_STOP:
		ath9k_ps_wakeup(sc);
		ath_tx_aggr_stop(sc, sta, tid);
		ieee80211_stop_tx_ba_cb_irqsafe(hw, sta->addr, tid);
		ath9k_ps_restore(sc);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		ath9k_ps_wakeup(sc);
		ath_tx_aggr_resume(sc, sta, tid);
		ath9k_ps_restore(sc);
		break;
	default:
		DPRINTF(sc, ATH_DBG_FATAL, "Unknown AMPDU action\n");
	}

	return ret;
}

static void ath9k_sw_scan_start(struct ieee80211_hw *hw)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;

	mutex_lock(&sc->mutex);
	if (ath9k_wiphy_scanning(sc)) {
		printk(KERN_DEBUG "ath9k: Two wiphys trying to scan at the "
		       "same time\n");
		/*
		 * Do not allow the concurrent scanning state for now. This
		 * could be improved with scanning control moved into ath9k.
		 */
		mutex_unlock(&sc->mutex);
		return;
	}

	aphy->state = ATH_WIPHY_SCAN;
	ath9k_wiphy_pause_all_forced(sc, aphy);

	spin_lock_bh(&sc->ani_lock);
	sc->sc_flags |= SC_OP_SCANNING;
	spin_unlock_bh(&sc->ani_lock);
	mutex_unlock(&sc->mutex);
}

static void ath9k_sw_scan_complete(struct ieee80211_hw *hw)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;

	mutex_lock(&sc->mutex);
	spin_lock_bh(&sc->ani_lock);
	aphy->state = ATH_WIPHY_ACTIVE;
	sc->sc_flags &= ~SC_OP_SCANNING;
	sc->sc_flags |= SC_OP_FULL_RESET;
	spin_unlock_bh(&sc->ani_lock);
	ath_beacon_config(sc, NULL);
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
	.sta_notify         = ath9k_sta_notify,
	.conf_tx 	    = ath9k_conf_tx,
	.bss_info_changed   = ath9k_bss_info_changed,
	.set_key            = ath9k_set_key,
	.get_tsf 	    = ath9k_get_tsf,
	.set_tsf 	    = ath9k_set_tsf,
	.reset_tsf 	    = ath9k_reset_tsf,
	.ampdu_action       = ath9k_ampdu_action,
	.sw_scan_start      = ath9k_sw_scan_start,
	.sw_scan_complete   = ath9k_sw_scan_complete,
	.rfkill_poll        = ath9k_rfkill_poll_state,
};

static struct {
	u32 version;
	const char * name;
} ath_mac_bb_names[] = {
	{ AR_SREV_VERSION_5416_PCI,	"5416" },
	{ AR_SREV_VERSION_5416_PCIE,	"5418" },
	{ AR_SREV_VERSION_9100,		"9100" },
	{ AR_SREV_VERSION_9160,		"9160" },
	{ AR_SREV_VERSION_9280,		"9280" },
	{ AR_SREV_VERSION_9285,		"9285" },
	{ AR_SREV_VERSION_9287,         "9287" }
};

static struct {
	u16 version;
	const char * name;
} ath_rf_names[] = {
	{ 0,				"5133" },
	{ AR_RAD5133_SREV_MAJOR,	"5133" },
	{ AR_RAD5122_SREV_MAJOR,	"5122" },
	{ AR_RAD2133_SREV_MAJOR,	"2133" },
	{ AR_RAD2122_SREV_MAJOR,	"2122" }
};

/*
 * Return the MAC/BB name. "????" is returned if the MAC/BB is unknown.
 */
const char *
ath_mac_bb_name(u32 mac_bb_version)
{
	int i;

	for (i=0; i<ARRAY_SIZE(ath_mac_bb_names); i++) {
		if (ath_mac_bb_names[i].version == mac_bb_version) {
			return ath_mac_bb_names[i].name;
		}
	}

	return "????";
}

/*
 * Return the RF name. "????" is returned if the RF is unknown.
 */
const char *
ath_rf_name(u16 rf_version)
{
	int i;

	for (i=0; i<ARRAY_SIZE(ath_rf_names); i++) {
		if (ath_rf_names[i].version == rf_version) {
			return ath_rf_names[i].name;
		}
	}

	return "????";
}

static int __init ath9k_init(void)
{
	int error;

	/* Register rate control algorithm */
	error = ath_rate_control_register();
	if (error != 0) {
		printk(KERN_ERR
			"ath9k: Unable to register rate control "
			"algorithm: %d\n",
			error);
		goto err_out;
	}

	error = ath9k_debug_create_root();
	if (error) {
		printk(KERN_ERR
			"ath9k: Unable to create debugfs root: %d\n",
			error);
		goto err_rate_unregister;
	}

	error = ath_pci_init();
	if (error < 0) {
		printk(KERN_ERR
			"ath9k: No PCI devices found, driver not installed.\n");
		error = -ENODEV;
		goto err_remove_root;
	}

	error = ath_ahb_init();
	if (error < 0) {
		error = -ENODEV;
		goto err_pci_exit;
	}

	return 0;

 err_pci_exit:
	ath_pci_exit();

 err_remove_root:
	ath9k_debug_remove_root();
 err_rate_unregister:
	ath_rate_control_unregister();
 err_out:
	return error;
}
module_init(ath9k_init);

static void __exit ath9k_exit(void)
{
	ath_ahb_exit();
	ath_pci_exit();
	ath9k_debug_remove_root();
	ath_rate_control_unregister();
	printk(KERN_INFO "%s: Driver unloaded\n", dev_info);
}
module_exit(ath9k_exit);
