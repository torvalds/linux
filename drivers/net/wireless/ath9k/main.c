/*
 * Copyright (c) 2008 Atheros Communications Inc.
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
#include "core.h"
#include "reg.h"
#include "hw.h"

#define ATH_PCI_VERSION "0.1"

static char *dev_info = "ath9k";

MODULE_AUTHOR("Atheros Communications");
MODULE_DESCRIPTION("Support for Atheros 802.11n wireless LAN cards.");
MODULE_SUPPORTED_DEVICE("Atheros 802.11n WLAN cards");
MODULE_LICENSE("Dual BSD/GPL");

static struct pci_device_id ath_pci_id_table[] __devinitdata = {
	{ PCI_VDEVICE(ATHEROS, 0x0023) }, /* PCI   */
	{ PCI_VDEVICE(ATHEROS, 0x0024) }, /* PCI-E */
	{ PCI_VDEVICE(ATHEROS, 0x0027) }, /* PCI   */
	{ PCI_VDEVICE(ATHEROS, 0x0029) }, /* PCI   */
	{ PCI_VDEVICE(ATHEROS, 0x002A) }, /* PCI-E */
	{ PCI_VDEVICE(ATHEROS, 0x002B) }, /* PCI-E */
	{ 0 }
};

static void ath_detach(struct ath_softc *sc);

/* return bus cachesize in 4B word units */

static void bus_read_cachesize(struct ath_softc *sc, int *csz)
{
	u8 u8tmp;

	pci_read_config_byte(sc->pdev, PCI_CACHE_LINE_SIZE, (u8 *)&u8tmp);
	*csz = (int)u8tmp;

	/*
	 * This check was put in to avoid "unplesant" consequences if
	 * the bootrom has not fully initialized all PCI devices.
	 * Sometimes the cache line size register is not set
	 */

	if (*csz == 0)
		*csz = DEFAULT_CACHELINE >> 2;   /* Use the default size */
}

static void ath_setcurmode(struct ath_softc *sc, enum wireless_mode mode)
{
	sc->cur_rate_table = sc->hw_rate_table[mode];
	/*
	 * All protection frames are transmited at 2Mb/s for
	 * 11g, otherwise at 1Mb/s.
	 * XXX select protection rate index from rate table.
	 */
	sc->sc_protrix = (mode == ATH9K_MODE_11G ? 1 : 0);
}

static enum wireless_mode ath_chan2mode(struct ath9k_channel *chan)
{
	if (chan->chanmode == CHANNEL_A)
		return ATH9K_MODE_11A;
	else if (chan->chanmode == CHANNEL_G)
		return ATH9K_MODE_11G;
	else if (chan->chanmode == CHANNEL_B)
		return ATH9K_MODE_11B;
	else if (chan->chanmode == CHANNEL_A_HT20)
		return ATH9K_MODE_11NA_HT20;
	else if (chan->chanmode == CHANNEL_G_HT20)
		return ATH9K_MODE_11NG_HT20;
	else if (chan->chanmode == CHANNEL_A_HT40PLUS)
		return ATH9K_MODE_11NA_HT40PLUS;
	else if (chan->chanmode == CHANNEL_A_HT40MINUS)
		return ATH9K_MODE_11NA_HT40MINUS;
	else if (chan->chanmode == CHANNEL_G_HT40PLUS)
		return ATH9K_MODE_11NG_HT40PLUS;
	else if (chan->chanmode == CHANNEL_G_HT40MINUS)
		return ATH9K_MODE_11NG_HT40MINUS;

	WARN_ON(1); /* should not get here */

	return ATH9K_MODE_11B;
}

static void ath_update_txpow(struct ath_softc *sc)
{
	struct ath_hal *ah = sc->sc_ah;
	u32 txpow;

	if (sc->sc_curtxpow != sc->sc_config.txpowlimit) {
		ath9k_hw_set_txpowerlimit(ah, sc->sc_config.txpowlimit);
		/* read back in case value is clamped */
		ath9k_hw_getcapability(ah, ATH9K_CAP_TXPOW, 1, &txpow);
		sc->sc_curtxpow = txpow;
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
	struct ath_rate_table *rate_table = NULL;
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
		sband->n_bitrates++;
		DPRINTF(sc, ATH_DBG_CONFIG, "Rate: %2dMbps, ratecode: %2d\n",
			rate[i].bitrate / 10, rate[i].hw_value);
	}
}

static int ath_setup_channels(struct ath_softc *sc)
{
	struct ath_hal *ah = sc->sc_ah;
	int nchan, i, a = 0, b = 0;
	u8 regclassids[ATH_REGCLASSIDS_MAX];
	u32 nregclass = 0;
	struct ieee80211_supported_band *band_2ghz;
	struct ieee80211_supported_band *band_5ghz;
	struct ieee80211_channel *chan_2ghz;
	struct ieee80211_channel *chan_5ghz;
	struct ath9k_channel *c;

	/* Fill in ah->ah_channels */
	if (!ath9k_regd_init_channels(ah, ATH_CHAN_MAX, (u32 *)&nchan,
				      regclassids, ATH_REGCLASSIDS_MAX,
				      &nregclass, CTRY_DEFAULT, false, 1)) {
		u32 rd = ah->ah_currentRD;
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to collect channel list; "
			"regdomain likely %u country code %u\n",
			rd, CTRY_DEFAULT);
		return -EINVAL;
	}

	band_2ghz = &sc->sbands[IEEE80211_BAND_2GHZ];
	band_5ghz = &sc->sbands[IEEE80211_BAND_5GHZ];
	chan_2ghz = sc->channels[IEEE80211_BAND_2GHZ];
	chan_5ghz = sc->channels[IEEE80211_BAND_5GHZ];

	for (i = 0; i < nchan; i++) {
		c = &ah->ah_channels[i];
		if (IS_CHAN_2GHZ(c)) {
			chan_2ghz[a].band = IEEE80211_BAND_2GHZ;
			chan_2ghz[a].center_freq = c->channel;
			chan_2ghz[a].max_power = c->maxTxPower;

			if (c->privFlags & CHANNEL_DISALLOW_ADHOC)
				chan_2ghz[a].flags |= IEEE80211_CHAN_NO_IBSS;
			if (c->channelFlags & CHANNEL_PASSIVE)
				chan_2ghz[a].flags |= IEEE80211_CHAN_PASSIVE_SCAN;

			band_2ghz->n_channels = ++a;

			DPRINTF(sc, ATH_DBG_CONFIG, "2MHz channel: %d, "
				"channelFlags: 0x%x\n",
				c->channel, c->channelFlags);
		} else if (IS_CHAN_5GHZ(c)) {
			chan_5ghz[b].band = IEEE80211_BAND_5GHZ;
			chan_5ghz[b].center_freq = c->channel;
			chan_5ghz[b].max_power = c->maxTxPower;

			if (c->privFlags & CHANNEL_DISALLOW_ADHOC)
				chan_5ghz[b].flags |= IEEE80211_CHAN_NO_IBSS;
			if (c->channelFlags & CHANNEL_PASSIVE)
				chan_5ghz[b].flags |= IEEE80211_CHAN_PASSIVE_SCAN;

			band_5ghz->n_channels = ++b;

			DPRINTF(sc, ATH_DBG_CONFIG, "5MHz channel: %d, "
				"channelFlags: 0x%x\n",
				c->channel, c->channelFlags);
		}
	}

	return 0;
}

/*
 * Set/change channels.  If the channel is really being changed, it's done
 * by reseting the chip.  To accomplish this we must first cleanup any pending
 * DMA, then restart stuff.
*/
static int ath_set_channel(struct ath_softc *sc, struct ath9k_channel *hchan)
{
	struct ath_hal *ah = sc->sc_ah;
	bool fastcc = true, stopped;

	if (sc->sc_flags & SC_OP_INVALID)
		return -EIO;

	if (hchan->channel != sc->sc_ah->ah_curchan->channel ||
	    hchan->channelFlags != sc->sc_ah->ah_curchan->channelFlags ||
	    (sc->sc_flags & SC_OP_CHAINMASK_UPDATE) ||
	    (sc->sc_flags & SC_OP_FULL_RESET)) {
		int status;
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
		ath_draintxq(sc, false);
		stopped = ath_stoprecv(sc);

		/* XXX: do not flush receive queue here. We don't want
		 * to flush data frames already in queue because of
		 * changing channel. */

		if (!stopped || (sc->sc_flags & SC_OP_FULL_RESET))
			fastcc = false;

		DPRINTF(sc, ATH_DBG_CONFIG,
			"(%u MHz) -> (%u MHz), cflags:%x, chanwidth: %d\n",
			sc->sc_ah->ah_curchan->channel,
			hchan->channel, hchan->channelFlags, sc->tx_chan_width);

		spin_lock_bh(&sc->sc_resetlock);
		if (!ath9k_hw_reset(ah, hchan, sc->tx_chan_width,
				    sc->sc_tx_chainmask, sc->sc_rx_chainmask,
				    sc->sc_ht_extprotspacing, fastcc, &status)) {
			DPRINTF(sc, ATH_DBG_FATAL,
				"Unable to reset channel %u (%uMhz) "
				"flags 0x%x hal status %u\n",
				ath9k_hw_mhz2ieee(ah, hchan->channel,
						  hchan->channelFlags),
				hchan->channel, hchan->channelFlags, status);
			spin_unlock_bh(&sc->sc_resetlock);
			return -EIO;
		}
		spin_unlock_bh(&sc->sc_resetlock);

		sc->sc_flags &= ~SC_OP_CHAINMASK_UPDATE;
		sc->sc_flags &= ~SC_OP_FULL_RESET;

		if (ath_startrecv(sc) != 0) {
			DPRINTF(sc, ATH_DBG_FATAL,
				"Unable to restart recv logic\n");
			return -EIO;
		}

		ath_setcurmode(sc, ath_chan2mode(hchan));
		ath_update_txpow(sc);
		ath9k_hw_set_interrupts(ah, sc->sc_imask);
	}
	return 0;
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
	struct ath_softc *sc;
	struct ath_hal *ah;
	bool longcal = false;
	bool shortcal = false;
	bool aniflag = false;
	unsigned int timestamp = jiffies_to_msecs(jiffies);
	u32 cal_interval;

	sc = (struct ath_softc *)data;
	ah = sc->sc_ah;

	/*
	* don't calibrate when we're scanning.
	* we are most likely not on our home channel.
	*/
	if (sc->rx.rxfilter & FIF_BCN_PRBRESP_PROMISC)
		return;

	/* Long calibration runs independently of short calibration. */
	if ((timestamp - sc->sc_ani.sc_longcal_timer) >= ATH_LONG_CALINTERVAL) {
		longcal = true;
		DPRINTF(sc, ATH_DBG_ANI, "longcal @%lu\n", jiffies);
		sc->sc_ani.sc_longcal_timer = timestamp;
	}

	/* Short calibration applies only while sc_caldone is false */
	if (!sc->sc_ani.sc_caldone) {
		if ((timestamp - sc->sc_ani.sc_shortcal_timer) >=
		    ATH_SHORT_CALINTERVAL) {
			shortcal = true;
			DPRINTF(sc, ATH_DBG_ANI, "shortcal @%lu\n", jiffies);
			sc->sc_ani.sc_shortcal_timer = timestamp;
			sc->sc_ani.sc_resetcal_timer = timestamp;
		}
	} else {
		if ((timestamp - sc->sc_ani.sc_resetcal_timer) >=
		    ATH_RESTART_CALINTERVAL) {
			ath9k_hw_reset_calvalid(ah, ah->ah_curchan,
						&sc->sc_ani.sc_caldone);
			if (sc->sc_ani.sc_caldone)
				sc->sc_ani.sc_resetcal_timer = timestamp;
		}
	}

	/* Verify whether we must check ANI */
	if ((timestamp - sc->sc_ani.sc_checkani_timer) >=
	   ATH_ANI_POLLINTERVAL) {
		aniflag = true;
		sc->sc_ani.sc_checkani_timer = timestamp;
	}

	/* Skip all processing if there's nothing to do. */
	if (longcal || shortcal || aniflag) {
		/* Call ANI routine if necessary */
		if (aniflag)
			ath9k_hw_ani_monitor(ah, &sc->sc_halstats,
					     ah->ah_curchan);

		/* Perform calibration if necessary */
		if (longcal || shortcal) {
			bool iscaldone = false;

			if (ath9k_hw_calibrate(ah, ah->ah_curchan,
					       sc->sc_rx_chainmask, longcal,
					       &iscaldone)) {
				if (longcal)
					sc->sc_ani.sc_noise_floor =
						ath9k_hw_getchan_noise(ah,
							       ah->ah_curchan);

				DPRINTF(sc, ATH_DBG_ANI,
					"calibrate chan %u/%x nf: %d\n",
					ah->ah_curchan->channel,
					ah->ah_curchan->channelFlags,
					sc->sc_ani.sc_noise_floor);
			} else {
				DPRINTF(sc, ATH_DBG_ANY,
					"calibrate chan %u/%x failed\n",
					ah->ah_curchan->channel,
					ah->ah_curchan->channelFlags);
			}
			sc->sc_ani.sc_caldone = iscaldone;
		}
	}

	/*
	* Set timer interval based on previous results.
	* The interval must be the shortest necessary to satisfy ANI,
	* short calibration and long calibration.
	*/
	cal_interval = ATH_LONG_CALINTERVAL;
	if (sc->sc_ah->ah_config.enable_ani)
		cal_interval = min(cal_interval, (u32)ATH_ANI_POLLINTERVAL);
	if (!sc->sc_ani.sc_caldone)
		cal_interval = min(cal_interval, (u32)ATH_SHORT_CALINTERVAL);

	mod_timer(&sc->sc_ani.timer, jiffies + msecs_to_jiffies(cal_interval));
}

/*
 * Update tx/rx chainmask. For legacy association,
 * hard code chainmask to 1x1, for 11n association, use
 * the chainmask configuration.
 */
static void ath_update_chainmask(struct ath_softc *sc, int is_ht)
{
	sc->sc_flags |= SC_OP_CHAINMASK_UPDATE;
	if (is_ht) {
		sc->sc_tx_chainmask = sc->sc_ah->ah_caps.tx_chainmask;
		sc->sc_rx_chainmask = sc->sc_ah->ah_caps.rx_chainmask;
	} else {
		sc->sc_tx_chainmask = 1;
		sc->sc_rx_chainmask = 1;
	}

	DPRINTF(sc, ATH_DBG_CONFIG, "tx chmask: %d, rx chmask: %d\n",
		sc->sc_tx_chainmask, sc->sc_rx_chainmask);
}

static void ath_node_attach(struct ath_softc *sc, struct ieee80211_sta *sta)
{
	struct ath_node *an;

	an = (struct ath_node *)sta->drv_priv;

	if (sc->sc_flags & SC_OP_TXAGGR)
		ath_tx_node_init(sc, an);

	an->maxampdu = 1 << (IEEE80211_HTCAP_MAXRXAMPDU_FACTOR +
			     sta->ht_cap.ampdu_factor);
	an->mpdudensity = parse_mpdudensity(sta->ht_cap.ampdu_density);
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
	u32 status = sc->sc_intrstatus;

	if (status & ATH9K_INT_FATAL) {
		/* need a chip reset */
		ath_reset(sc, false);
		return;
	} else {

		if (status &
		    (ATH9K_INT_RX | ATH9K_INT_RXEOL | ATH9K_INT_RXORN)) {
			spin_lock_bh(&sc->rx.rxflushlock);
			ath_rx_tasklet(sc, 0);
			spin_unlock_bh(&sc->rx.rxflushlock);
		}
		/* XXX: optimize this */
		if (status & ATH9K_INT_TX)
			ath_tx_tasklet(sc);
	}

	/* re-enable hardware interrupt */
	ath9k_hw_set_interrupts(sc->sc_ah, sc->sc_imask);
}

static irqreturn_t ath_isr(int irq, void *dev)
{
	struct ath_softc *sc = dev;
	struct ath_hal *ah = sc->sc_ah;
	enum ath9k_int status;
	bool sched = false;

	do {
		if (sc->sc_flags & SC_OP_INVALID) {
			/*
			 * The hardware is not ready/present, don't
			 * touch anything. Note this can happen early
			 * on if the IRQ is shared.
			 */
			return IRQ_NONE;
		}
		if (!ath9k_hw_intrpend(ah)) {	/* shared irq, not for us */
			return IRQ_NONE;
		}

		/*
		 * Figure out the reason(s) for the interrupt.  Note
		 * that the hal returns a pseudo-ISR that may include
		 * bits we haven't explicitly enabled so we mask the
		 * value to insure we only process bits we requested.
		 */
		ath9k_hw_getisr(ah, &status);	/* NB: clears ISR too */

		status &= sc->sc_imask;	/* discard unasked-for bits */

		/*
		 * If there are no status bits set, then this interrupt was not
		 * for me (should have been caught above).
		 */
		if (!status)
			return IRQ_NONE;

		sc->sc_intrstatus = status;

		if (status & ATH9K_INT_FATAL) {
			/* need a chip reset */
			sched = true;
		} else if (status & ATH9K_INT_RXORN) {
			/* need a chip reset */
			sched = true;
		} else {
			if (status & ATH9K_INT_SWBA) {
				/* schedule a tasklet for beacon handling */
				tasklet_schedule(&sc->bcon_tasklet);
			}
			if (status & ATH9K_INT_RXEOL) {
				/*
				 * NB: the hardware should re-read the link when
				 *     RXE bit is written, but it doesn't work
				 *     at least on older hardware revs.
				 */
				sched = true;
			}

			if (status & ATH9K_INT_TXURN)
				/* bump tx trigger level */
				ath9k_hw_updatetxtriglevel(ah, true);
			/* XXX: optimize this */
			if (status & ATH9K_INT_RX)
				sched = true;
			if (status & ATH9K_INT_TX)
				sched = true;
			if (status & ATH9K_INT_BMISS)
				sched = true;
			/* carrier sense timeout */
			if (status & ATH9K_INT_CST)
				sched = true;
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
				ath9k_hw_procmibevent(ah, &sc->sc_halstats);
				ath9k_hw_set_interrupts(ah, sc->sc_imask);
			}
			if (status & ATH9K_INT_TIM_TIMER) {
				if (!(ah->ah_caps.hw_caps &
				      ATH9K_HW_CAP_AUTOSLEEP)) {
					/* Clear RxAbort bit so that we can
					 * receive frames */
					ath9k_hw_setrxabort(ah, 0);
					sched = true;
				}
			}
		}
	} while (0);

	ath_debug_stat_interrupt(sc, status);

	if (sched) {
		/* turn off every interrupt except SWBA */
		ath9k_hw_set_interrupts(ah, (sc->sc_imask & ATH9K_INT_SWBA));
		tasklet_schedule(&sc->intr_tq);
	}

	return IRQ_HANDLED;
}

static int ath_get_channel(struct ath_softc *sc,
			   struct ieee80211_channel *chan)
{
	int i;

	for (i = 0; i < sc->sc_ah->ah_nchan; i++) {
		if (sc->sc_ah->ah_channels[i].channel == chan->center_freq)
			return i;
	}

	return -1;
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

static int ath_keyset(struct ath_softc *sc, u16 keyix,
	       struct ath9k_keyval *hk, const u8 mac[ETH_ALEN])
{
	bool status;

	status = ath9k_hw_set_keycache_entry(sc->sc_ah,
		keyix, hk, mac, false);

	return status != false;
}

static int ath_setkey_tkip(struct ath_softc *sc, u16 keyix, const u8 *key,
			   struct ath9k_keyval *hk,
			   const u8 *addr)
{
	const u8 *key_rxmic;
	const u8 *key_txmic;

	key_txmic = key + NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY;
	key_rxmic = key + NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY;

	if (addr == NULL) {
		/* Group key installation */
		memcpy(hk->kv_mic, key_rxmic, sizeof(hk->kv_mic));
		return ath_keyset(sc, keyix, hk, addr);
	}
	if (!sc->sc_splitmic) {
		/*
		 * data key goes at first index,
		 * the hal handles the MIC keys at index+64.
		 */
		memcpy(hk->kv_mic, key_rxmic, sizeof(hk->kv_mic));
		memcpy(hk->kv_txmic, key_txmic, sizeof(hk->kv_txmic));
		return ath_keyset(sc, keyix, hk, addr);
	}
	/*
	 * TX key goes at first index, RX key at +32.
	 * The hal handles the MIC keys at index+64.
	 */
	memcpy(hk->kv_mic, key_txmic, sizeof(hk->kv_mic));
	if (!ath_keyset(sc, keyix, hk, NULL)) {
		/* Txmic entry failed. No need to proceed further */
		DPRINTF(sc, ATH_DBG_KEYCACHE,
			"Setting TX MIC Key Failed\n");
		return 0;
	}

	memcpy(hk->kv_mic, key_rxmic, sizeof(hk->kv_mic));
	/* XXX delete tx key on failure? */
	return ath_keyset(sc, keyix + 32, hk, addr);
}

static int ath_reserve_key_cache_slot_tkip(struct ath_softc *sc)
{
	int i;

	for (i = IEEE80211_WEP_NKID; i < sc->sc_keymax / 2; i++) {
		if (test_bit(i, sc->sc_keymap) ||
		    test_bit(i + 64, sc->sc_keymap))
			continue; /* At least one part of TKIP key allocated */
		if (sc->sc_splitmic &&
		    (test_bit(i + 32, sc->sc_keymap) ||
		     test_bit(i + 64 + 32, sc->sc_keymap)))
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
	if (sc->sc_splitmic) {
		for (i = IEEE80211_WEP_NKID; i < sc->sc_keymax / 4; i++) {
			if (!test_bit(i, sc->sc_keymap) &&
			    (test_bit(i + 32, sc->sc_keymap) ||
			     test_bit(i + 64, sc->sc_keymap) ||
			     test_bit(i + 64 + 32, sc->sc_keymap)))
				return i;
			if (!test_bit(i + 32, sc->sc_keymap) &&
			    (test_bit(i, sc->sc_keymap) ||
			     test_bit(i + 64, sc->sc_keymap) ||
			     test_bit(i + 64 + 32, sc->sc_keymap)))
				return i + 32;
			if (!test_bit(i + 64, sc->sc_keymap) &&
			    (test_bit(i , sc->sc_keymap) ||
			     test_bit(i + 32, sc->sc_keymap) ||
			     test_bit(i + 64 + 32, sc->sc_keymap)))
				return i + 64;
			if (!test_bit(i + 64 + 32, sc->sc_keymap) &&
			    (test_bit(i, sc->sc_keymap) ||
			     test_bit(i + 32, sc->sc_keymap) ||
			     test_bit(i + 64, sc->sc_keymap)))
				return i + 64 + 32;
		}
	} else {
		for (i = IEEE80211_WEP_NKID; i < sc->sc_keymax / 2; i++) {
			if (!test_bit(i, sc->sc_keymap) &&
			    test_bit(i + 64, sc->sc_keymap))
				return i;
			if (test_bit(i, sc->sc_keymap) &&
			    !test_bit(i + 64, sc->sc_keymap))
				return i + 64;
		}
	}

	/* No partially used TKIP slots, pick any available slot */
	for (i = IEEE80211_WEP_NKID; i < sc->sc_keymax; i++) {
		/* Do not allow slots that could be needed for TKIP group keys
		 * to be used. This limitation could be removed if we know that
		 * TKIP will not be used. */
		if (i >= 64 && i < 64 + IEEE80211_WEP_NKID)
			continue;
		if (sc->sc_splitmic) {
			if (i >= 32 && i < 32 + IEEE80211_WEP_NKID)
				continue;
			if (i >= 64 + 32 && i < 64 + 32 + IEEE80211_WEP_NKID)
				continue;
		}

		if (!test_bit(i, sc->sc_keymap))
			return i; /* Found a free slot for a key */
	}

	/* No free slot found */
	return -1;
}

static int ath_key_config(struct ath_softc *sc,
			  const u8 *addr,
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
		return -EINVAL;
	}

	hk.kv_len = key->keylen;
	memcpy(hk.kv_val, key->key, key->keylen);

	if (!(key->flags & IEEE80211_KEY_FLAG_PAIRWISE)) {
		/* For now, use the default keys for broadcast keys. This may
		 * need to change with virtual interfaces. */
		idx = key->keyidx;
	} else if (key->keyidx) {
		struct ieee80211_vif *vif;

		mac = addr;
		vif = sc->sc_vaps[0];
		if (vif->type != NL80211_IFTYPE_AP) {
			/* Only keyidx 0 should be used with unicast key, but
			 * allow this for client mode for now. */
			idx = key->keyidx;
		} else
			return -EIO;
	} else {
		mac = addr;
		if (key->alg == ALG_TKIP)
			idx = ath_reserve_key_cache_slot_tkip(sc);
		else
			idx = ath_reserve_key_cache_slot(sc);
		if (idx < 0)
			return -EIO; /* no free key cache entries */
	}

	if (key->alg == ALG_TKIP)
		ret = ath_setkey_tkip(sc, idx, key->key, &hk, mac);
	else
		ret = ath_keyset(sc, idx, &hk, mac);

	if (!ret)
		return -EIO;

	set_bit(idx, sc->sc_keymap);
	if (key->alg == ALG_TKIP) {
		set_bit(idx + 64, sc->sc_keymap);
		if (sc->sc_splitmic) {
			set_bit(idx + 32, sc->sc_keymap);
			set_bit(idx + 64 + 32, sc->sc_keymap);
		}
	}

	return idx;
}

static void ath_key_delete(struct ath_softc *sc, struct ieee80211_key_conf *key)
{
	ath9k_hw_keyreset(sc->sc_ah, key->hw_key_idx);
	if (key->hw_key_idx < IEEE80211_WEP_NKID)
		return;

	clear_bit(key->hw_key_idx, sc->sc_keymap);
	if (key->alg != ALG_TKIP)
		return;

	clear_bit(key->hw_key_idx + 64, sc->sc_keymap);
	if (sc->sc_splitmic) {
		clear_bit(key->hw_key_idx + 32, sc->sc_keymap);
		clear_bit(key->hw_key_idx + 64 + 32, sc->sc_keymap);
	}
}

static void setup_ht_cap(struct ieee80211_sta_ht_cap *ht_info)
{
#define	ATH9K_HT_CAP_MAXRXAMPDU_65536 0x3	/* 2 ^ 16 */
#define	ATH9K_HT_CAP_MPDUDENSITY_8 0x6		/* 8 usec */

	ht_info->ht_supported = true;
	ht_info->cap = IEEE80211_HT_CAP_SUP_WIDTH_20_40 |
		       IEEE80211_HT_CAP_SM_PS |
		       IEEE80211_HT_CAP_SGI_40 |
		       IEEE80211_HT_CAP_DSSSCCK40;

	ht_info->ampdu_factor = ATH9K_HT_CAP_MAXRXAMPDU_65536;
	ht_info->ampdu_density = ATH9K_HT_CAP_MPDUDENSITY_8;
	/* set up supported mcs set */
	memset(&ht_info->mcs, 0, sizeof(ht_info->mcs));
	ht_info->mcs.rx_mask[0] = 0xff;
	ht_info->mcs.rx_mask[1] = 0xff;
	ht_info->mcs.tx_params = IEEE80211_HT_MCS_TX_DEFINED;
}

static void ath9k_bss_assoc_info(struct ath_softc *sc,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *bss_conf)
{
	struct ath_vap *avp = (void *)vif->drv_priv;

	if (bss_conf->assoc) {
		DPRINTF(sc, ATH_DBG_CONFIG, "Bss Info ASSOC %d, bssid: %pM\n",
			bss_conf->aid, sc->sc_curbssid);

		/* New association, store aid */
		if (avp->av_opmode == NL80211_IFTYPE_STATION) {
			sc->sc_curaid = bss_conf->aid;
			ath9k_hw_write_associd(sc->sc_ah, sc->sc_curbssid,
					       sc->sc_curaid);
		}

		/* Configure the beacon */
		ath_beacon_config(sc, 0);
		sc->sc_flags |= SC_OP_BEACONS;

		/* Reset rssi stats */
		sc->sc_halstats.ns_avgbrssi = ATH_RSSI_DUMMY_MARKER;
		sc->sc_halstats.ns_avgrssi = ATH_RSSI_DUMMY_MARKER;
		sc->sc_halstats.ns_avgtxrssi = ATH_RSSI_DUMMY_MARKER;
		sc->sc_halstats.ns_avgtxrate = ATH_RATE_DUMMY_MARKER;

		/* Start ANI */
		mod_timer(&sc->sc_ani.timer,
			jiffies + msecs_to_jiffies(ATH_ANI_POLLINTERVAL));

	} else {
		DPRINTF(sc, ATH_DBG_CONFIG, "Bss Info DISSOC\n");
		sc->sc_curaid = 0;
	}
}

/********************************/
/*	 LED functions		*/
/********************************/

static void ath_led_brightness(struct led_classdev *led_cdev,
			       enum led_brightness brightness)
{
	struct ath_led *led = container_of(led_cdev, struct ath_led, led_cdev);
	struct ath_softc *sc = led->sc;

	switch (brightness) {
	case LED_OFF:
		if (led->led_type == ATH_LED_ASSOC ||
		    led->led_type == ATH_LED_RADIO)
			sc->sc_flags &= ~SC_OP_LED_ASSOCIATED;
		ath9k_hw_set_gpio(sc->sc_ah, ATH_LED_PIN,
				(led->led_type == ATH_LED_RADIO) ? 1 :
				!!(sc->sc_flags & SC_OP_LED_ASSOCIATED));
		break;
	case LED_FULL:
		if (led->led_type == ATH_LED_ASSOC)
			sc->sc_flags |= SC_OP_LED_ASSOCIATED;
		ath9k_hw_set_gpio(sc->sc_ah, ATH_LED_PIN, 0);
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
	ath9k_hw_set_gpio(sc->sc_ah, ATH_LED_PIN, 1);
}

static void ath_init_leds(struct ath_softc *sc)
{
	char *trigger;
	int ret;

	/* Configure gpio 1 for output */
	ath9k_hw_cfg_output(sc->sc_ah, ATH_LED_PIN,
			    AR_GPIO_OUTPUT_MUX_AS_OUTPUT);
	/* LED off, active low */
	ath9k_hw_set_gpio(sc->sc_ah, ATH_LED_PIN, 1);

	trigger = ieee80211_get_radio_led_name(sc->hw);
	snprintf(sc->radio_led.name, sizeof(sc->radio_led.name),
		"ath9k-%s:radio", wiphy_name(sc->hw->wiphy));
	ret = ath_register_led(sc, &sc->radio_led, trigger);
	sc->radio_led.led_type = ATH_LED_RADIO;
	if (ret)
		goto fail;

	trigger = ieee80211_get_assoc_led_name(sc->hw);
	snprintf(sc->assoc_led.name, sizeof(sc->assoc_led.name),
		"ath9k-%s:assoc", wiphy_name(sc->hw->wiphy));
	ret = ath_register_led(sc, &sc->assoc_led, trigger);
	sc->assoc_led.led_type = ATH_LED_ASSOC;
	if (ret)
		goto fail;

	trigger = ieee80211_get_tx_led_name(sc->hw);
	snprintf(sc->tx_led.name, sizeof(sc->tx_led.name),
		"ath9k-%s:tx", wiphy_name(sc->hw->wiphy));
	ret = ath_register_led(sc, &sc->tx_led, trigger);
	sc->tx_led.led_type = ATH_LED_TX;
	if (ret)
		goto fail;

	trigger = ieee80211_get_rx_led_name(sc->hw);
	snprintf(sc->rx_led.name, sizeof(sc->rx_led.name),
		"ath9k-%s:rx", wiphy_name(sc->hw->wiphy));
	ret = ath_register_led(sc, &sc->rx_led, trigger);
	sc->rx_led.led_type = ATH_LED_RX;
	if (ret)
		goto fail;

	return;

fail:
	ath_deinit_leds(sc);
}

#if defined(CONFIG_RFKILL) || defined(CONFIG_RFKILL_MODULE)

/*******************/
/*	Rfkill	   */
/*******************/

static void ath_radio_enable(struct ath_softc *sc)
{
	struct ath_hal *ah = sc->sc_ah;
	int status;

	spin_lock_bh(&sc->sc_resetlock);
	if (!ath9k_hw_reset(ah, ah->ah_curchan,
			    sc->tx_chan_width,
			    sc->sc_tx_chainmask,
			    sc->sc_rx_chainmask,
			    sc->sc_ht_extprotspacing,
			    false, &status)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to reset channel %u (%uMhz) "
			"flags 0x%x hal status %u\n",
			ath9k_hw_mhz2ieee(ah,
					  ah->ah_curchan->channel,
					  ah->ah_curchan->channelFlags),
			ah->ah_curchan->channel,
			ah->ah_curchan->channelFlags, status);
	}
	spin_unlock_bh(&sc->sc_resetlock);

	ath_update_txpow(sc);
	if (ath_startrecv(sc) != 0) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to restart recv logic\n");
		return;
	}

	if (sc->sc_flags & SC_OP_BEACONS)
		ath_beacon_config(sc, ATH_IF_ID_ANY);	/* restart beacons */

	/* Re-Enable  interrupts */
	ath9k_hw_set_interrupts(ah, sc->sc_imask);

	/* Enable LED */
	ath9k_hw_cfg_output(ah, ATH_LED_PIN,
			    AR_GPIO_OUTPUT_MUX_AS_OUTPUT);
	ath9k_hw_set_gpio(ah, ATH_LED_PIN, 0);

	ieee80211_wake_queues(sc->hw);
}

static void ath_radio_disable(struct ath_softc *sc)
{
	struct ath_hal *ah = sc->sc_ah;
	int status;


	ieee80211_stop_queues(sc->hw);

	/* Disable LED */
	ath9k_hw_set_gpio(ah, ATH_LED_PIN, 1);
	ath9k_hw_cfg_gpio_input(ah, ATH_LED_PIN);

	/* Disable interrupts */
	ath9k_hw_set_interrupts(ah, 0);

	ath_draintxq(sc, false);	/* clear pending tx frames */
	ath_stoprecv(sc);		/* turn off frame recv */
	ath_flushrecv(sc);		/* flush recv queue */

	spin_lock_bh(&sc->sc_resetlock);
	if (!ath9k_hw_reset(ah, ah->ah_curchan,
			    sc->tx_chan_width,
			    sc->sc_tx_chainmask,
			    sc->sc_rx_chainmask,
			    sc->sc_ht_extprotspacing,
			    false, &status)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to reset channel %u (%uMhz) "
			"flags 0x%x hal status %u\n",
			ath9k_hw_mhz2ieee(ah,
				ah->ah_curchan->channel,
				ah->ah_curchan->channelFlags),
			ah->ah_curchan->channel,
			ah->ah_curchan->channelFlags, status);
	}
	spin_unlock_bh(&sc->sc_resetlock);

	ath9k_hw_phy_disable(ah);
	ath9k_hw_setpower(ah, ATH9K_PM_FULL_SLEEP);
}

static bool ath_is_rfkill_set(struct ath_softc *sc)
{
	struct ath_hal *ah = sc->sc_ah;

	return ath9k_hw_gpio_get(ah, ah->ah_rfkill_gpio) ==
				  ah->ah_rfkill_polarity;
}

/* h/w rfkill poll function */
static void ath_rfkill_poll(struct work_struct *work)
{
	struct ath_softc *sc = container_of(work, struct ath_softc,
					    rf_kill.rfkill_poll.work);
	bool radio_on;

	if (sc->sc_flags & SC_OP_INVALID)
		return;

	radio_on = !ath_is_rfkill_set(sc);

	/*
	 * enable/disable radio only when there is a
	 * state change in RF switch
	 */
	if (radio_on == !!(sc->sc_flags & SC_OP_RFKILL_HW_BLOCKED)) {
		enum rfkill_state state;

		if (sc->sc_flags & SC_OP_RFKILL_SW_BLOCKED) {
			state = radio_on ? RFKILL_STATE_SOFT_BLOCKED
				: RFKILL_STATE_HARD_BLOCKED;
		} else if (radio_on) {
			ath_radio_enable(sc);
			state = RFKILL_STATE_UNBLOCKED;
		} else {
			ath_radio_disable(sc);
			state = RFKILL_STATE_HARD_BLOCKED;
		}

		if (state == RFKILL_STATE_HARD_BLOCKED)
			sc->sc_flags |= SC_OP_RFKILL_HW_BLOCKED;
		else
			sc->sc_flags &= ~SC_OP_RFKILL_HW_BLOCKED;

		rfkill_force_state(sc->rf_kill.rfkill, state);
	}

	queue_delayed_work(sc->hw->workqueue, &sc->rf_kill.rfkill_poll,
			   msecs_to_jiffies(ATH_RFKILL_POLL_INTERVAL));
}

/* s/w rfkill handler */
static int ath_sw_toggle_radio(void *data, enum rfkill_state state)
{
	struct ath_softc *sc = data;

	switch (state) {
	case RFKILL_STATE_SOFT_BLOCKED:
		if (!(sc->sc_flags & (SC_OP_RFKILL_HW_BLOCKED |
		    SC_OP_RFKILL_SW_BLOCKED)))
			ath_radio_disable(sc);
		sc->sc_flags |= SC_OP_RFKILL_SW_BLOCKED;
		return 0;
	case RFKILL_STATE_UNBLOCKED:
		if ((sc->sc_flags & SC_OP_RFKILL_SW_BLOCKED)) {
			sc->sc_flags &= ~SC_OP_RFKILL_SW_BLOCKED;
			if (sc->sc_flags & SC_OP_RFKILL_HW_BLOCKED) {
				DPRINTF(sc, ATH_DBG_FATAL, "Can't turn on the"
					"radio as it is disabled by h/w\n");
				return -EPERM;
			}
			ath_radio_enable(sc);
		}
		return 0;
	default:
		return -EINVAL;
	}
}

/* Init s/w rfkill */
static int ath_init_sw_rfkill(struct ath_softc *sc)
{
	sc->rf_kill.rfkill = rfkill_allocate(wiphy_dev(sc->hw->wiphy),
					     RFKILL_TYPE_WLAN);
	if (!sc->rf_kill.rfkill) {
		DPRINTF(sc, ATH_DBG_FATAL, "Failed to allocate rfkill\n");
		return -ENOMEM;
	}

	snprintf(sc->rf_kill.rfkill_name, sizeof(sc->rf_kill.rfkill_name),
		"ath9k-%s:rfkill", wiphy_name(sc->hw->wiphy));
	sc->rf_kill.rfkill->name = sc->rf_kill.rfkill_name;
	sc->rf_kill.rfkill->data = sc;
	sc->rf_kill.rfkill->toggle_radio = ath_sw_toggle_radio;
	sc->rf_kill.rfkill->state = RFKILL_STATE_UNBLOCKED;
	sc->rf_kill.rfkill->user_claim_unsupported = 1;

	return 0;
}

/* Deinitialize rfkill */
static void ath_deinit_rfkill(struct ath_softc *sc)
{
	if (sc->sc_ah->ah_caps.hw_caps & ATH9K_HW_CAP_RFSILENT)
		cancel_delayed_work_sync(&sc->rf_kill.rfkill_poll);

	if (sc->sc_flags & SC_OP_RFKILL_REGISTERED) {
		rfkill_unregister(sc->rf_kill.rfkill);
		sc->sc_flags &= ~SC_OP_RFKILL_REGISTERED;
		sc->rf_kill.rfkill = NULL;
	}
}

static int ath_start_rfkill_poll(struct ath_softc *sc)
{
	if (sc->sc_ah->ah_caps.hw_caps & ATH9K_HW_CAP_RFSILENT)
		queue_delayed_work(sc->hw->workqueue,
				   &sc->rf_kill.rfkill_poll, 0);

	if (!(sc->sc_flags & SC_OP_RFKILL_REGISTERED)) {
		if (rfkill_register(sc->rf_kill.rfkill)) {
			DPRINTF(sc, ATH_DBG_FATAL,
				"Unable to register rfkill\n");
			rfkill_free(sc->rf_kill.rfkill);

			/* Deinitialize the device */
			ath_detach(sc);
			if (sc->pdev->irq)
				free_irq(sc->pdev->irq, sc);
			pci_iounmap(sc->pdev, sc->mem);
			pci_release_region(sc->pdev, 0);
			pci_disable_device(sc->pdev);
			ieee80211_free_hw(sc->hw);
			return -EIO;
		} else {
			sc->sc_flags |= SC_OP_RFKILL_REGISTERED;
		}
	}

	return 0;
}
#endif /* CONFIG_RFKILL */

static void ath_detach(struct ath_softc *sc)
{
	struct ieee80211_hw *hw = sc->hw;
	int i = 0;

	DPRINTF(sc, ATH_DBG_CONFIG, "Detach ATH hw\n");

#if defined(CONFIG_RFKILL) || defined(CONFIG_RFKILL_MODULE)
	ath_deinit_rfkill(sc);
#endif
	ath_deinit_leds(sc);

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

	ath9k_hw_detach(sc->sc_ah);
	ath9k_exit_debug(sc);
}

static int ath_init(u16 devid, struct ath_softc *sc)
{
	struct ath_hal *ah = NULL;
	int status;
	int error = 0, i;
	int csz = 0;

	/* XXX: hardware will not be ready until ath_open() being called */
	sc->sc_flags |= SC_OP_INVALID;

	if (ath9k_init_debug(sc) < 0)
		printk(KERN_ERR "Unable to create debugfs files\n");

	spin_lock_init(&sc->sc_resetlock);
	spin_lock_init(&sc->sc_serial_rw);
	mutex_init(&sc->mutex);
	tasklet_init(&sc->intr_tq, ath9k_tasklet, (unsigned long)sc);
	tasklet_init(&sc->bcon_tasklet, ath9k_beacon_tasklet,
		     (unsigned long)sc);

	/*
	 * Cache line size is used to size and align various
	 * structures used to communicate with the hardware.
	 */
	bus_read_cachesize(sc, &csz);
	/* XXX assert csz is non-zero */
	sc->sc_cachelsz = csz << 2;	/* convert to bytes */

	ah = ath9k_hw_attach(devid, sc, sc->mem, &status);
	if (ah == NULL) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to attach hardware; HAL status %u\n", status);
		error = -ENXIO;
		goto bad;
	}
	sc->sc_ah = ah;

	/* Get the hardware key cache size. */
	sc->sc_keymax = ah->ah_caps.keycache_size;
	if (sc->sc_keymax > ATH_KEYMAX) {
		DPRINTF(sc, ATH_DBG_KEYCACHE,
			"Warning, using only %u entries in %u key cache\n",
			ATH_KEYMAX, sc->sc_keymax);
		sc->sc_keymax = ATH_KEYMAX;
	}

	/*
	 * Reset the key cache since some parts do not
	 * reset the contents on initial power up.
	 */
	for (i = 0; i < sc->sc_keymax; i++)
		ath9k_hw_keyreset(ah, (u16) i);

	/* Collect the channel list using the default country code */

	error = ath_setup_channels(sc);
	if (error)
		goto bad;

	/* default to MONITOR mode */
	sc->sc_ah->ah_opmode = NL80211_IFTYPE_MONITOR;


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
		error = -EIO;
		goto bad2;
	}
	sc->beacon.cabq = ath_txq_setup(sc, ATH9K_TX_QUEUE_CAB, 0);
	if (sc->beacon.cabq == NULL) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to setup CAB xmit queue\n");
		error = -EIO;
		goto bad2;
	}

	sc->sc_config.cabqReadytime = ATH_CABQ_READY_TIME;
	ath_cabq_update(sc);

	for (i = 0; i < ARRAY_SIZE(sc->tx.hwq_map); i++)
		sc->tx.hwq_map[i] = -1;

	/* Setup data queues */
	/* NB: ensure BK queue is the lowest priority h/w queue */
	if (!ath_tx_setup(sc, ATH9K_WME_AC_BK)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to setup xmit queue for BK traffic\n");
		error = -EIO;
		goto bad2;
	}

	if (!ath_tx_setup(sc, ATH9K_WME_AC_BE)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to setup xmit queue for BE traffic\n");
		error = -EIO;
		goto bad2;
	}
	if (!ath_tx_setup(sc, ATH9K_WME_AC_VI)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to setup xmit queue for VI traffic\n");
		error = -EIO;
		goto bad2;
	}
	if (!ath_tx_setup(sc, ATH9K_WME_AC_VO)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to setup xmit queue for VO traffic\n");
		error = -EIO;
		goto bad2;
	}

	/* Initializes the noise floor to a reasonable default value.
	 * Later on this will be updated during ANI processing. */

	sc->sc_ani.sc_noise_floor = ATH_DEFAULT_NOISE_FLOOR;
	setup_timer(&sc->sc_ani.timer, ath_ani_calibrate, (unsigned long)sc);

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
		sc->sc_splitmic = 1;

	/* turn on mcast key search if possible */
	if (!ath9k_hw_getcapability(ah, ATH9K_CAP_MCAST_KEYSRCH, 0, NULL))
		(void)ath9k_hw_setcapability(ah, ATH9K_CAP_MCAST_KEYSRCH, 1,
					     1, NULL);

	sc->sc_config.txpowlimit = ATH_TXPOWER_MAX;
	sc->sc_config.txpowlimit_override = 0;

	/* 11n Capabilities */
	if (ah->ah_caps.hw_caps & ATH9K_HW_CAP_HT) {
		sc->sc_flags |= SC_OP_TXAGGR;
		sc->sc_flags |= SC_OP_RXAGGR;
	}

	sc->sc_tx_chainmask = ah->ah_caps.tx_chainmask;
	sc->sc_rx_chainmask = ah->ah_caps.rx_chainmask;

	ath9k_hw_setcapability(ah, ATH9K_CAP_DIVERSITY, 1, true, NULL);
	sc->rx.defant = ath9k_hw_getdefantenna(ah);

	ath9k_hw_getmac(ah, sc->sc_myaddr);
	if (ah->ah_caps.hw_caps & ATH9K_HW_CAP_BSSIDMASK) {
		ath9k_hw_getbssidmask(ah, sc->sc_bssidmask);
		ATH_SET_VAP_BSSID_MASK(sc->sc_bssidmask);
		ath9k_hw_setbssidmask(ah, sc->sc_bssidmask);
	}

	sc->beacon.slottime = ATH9K_SLOT_TIME_9;	/* default to short slot time */

	/* initialize beacon slots */
	for (i = 0; i < ARRAY_SIZE(sc->beacon.bslot); i++)
		sc->beacon.bslot[i] = ATH_IF_ID_ANY;

	/* save MISC configurations */
	sc->sc_config.swBeaconProcess = 1;

	/* setup channels and rates */

	sc->sbands[IEEE80211_BAND_2GHZ].channels =
		sc->channels[IEEE80211_BAND_2GHZ];
	sc->sbands[IEEE80211_BAND_2GHZ].bitrates =
		sc->rates[IEEE80211_BAND_2GHZ];
	sc->sbands[IEEE80211_BAND_2GHZ].band = IEEE80211_BAND_2GHZ;

	if (test_bit(ATH9K_MODE_11A, sc->sc_ah->ah_caps.wireless_modes)) {
		sc->sbands[IEEE80211_BAND_5GHZ].channels =
			sc->channels[IEEE80211_BAND_5GHZ];
		sc->sbands[IEEE80211_BAND_5GHZ].bitrates =
			sc->rates[IEEE80211_BAND_5GHZ];
		sc->sbands[IEEE80211_BAND_5GHZ].band = IEEE80211_BAND_5GHZ;
	}

	return 0;
bad2:
	/* cleanup tx queues */
	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++)
		if (ATH_TXQ_SETUP(sc, i))
			ath_tx_cleanupq(sc, &sc->tx.txq[i]);
bad:
	if (ah)
		ath9k_hw_detach(ah);
	ath9k_exit_debug(sc);

	return error;
}

static int ath_attach(u16 devid, struct ath_softc *sc)
{
	struct ieee80211_hw *hw = sc->hw;
	int error = 0, i;

	DPRINTF(sc, ATH_DBG_CONFIG, "Attach ATH hw\n");

	error = ath_init(devid, sc);
	if (error != 0)
		return error;

	/* get mac address from hardware and set in mac80211 */

	SET_IEEE80211_PERM_ADDR(hw, sc->sc_myaddr);

	hw->flags = IEEE80211_HW_RX_INCLUDES_FCS |
		IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING |
		IEEE80211_HW_SIGNAL_DBM |
		IEEE80211_HW_AMPDU_AGGREGATION;

	hw->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_AP) |
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC);

	hw->queues = 4;
	hw->max_rates = 4;
	hw->max_rate_tries = ATH_11N_TXMAXTRY;
	hw->sta_data_size = sizeof(struct ath_node);
	hw->vif_data_size = sizeof(struct ath_vap);

	hw->rate_control_algorithm = "ath9k_rate_control";

	if (sc->sc_ah->ah_caps.hw_caps & ATH9K_HW_CAP_HT) {
		setup_ht_cap(&sc->sbands[IEEE80211_BAND_2GHZ].ht_cap);
		if (test_bit(ATH9K_MODE_11A, sc->sc_ah->ah_caps.wireless_modes))
			setup_ht_cap(&sc->sbands[IEEE80211_BAND_5GHZ].ht_cap);
	}

	hw->wiphy->bands[IEEE80211_BAND_2GHZ] =	&sc->sbands[IEEE80211_BAND_2GHZ];
	if (test_bit(ATH9K_MODE_11A, sc->sc_ah->ah_caps.wireless_modes))
		hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
			&sc->sbands[IEEE80211_BAND_5GHZ];

	/* initialize tx/rx engine */
	error = ath_tx_init(sc, ATH_TXBUF);
	if (error != 0)
		goto error_attach;

	error = ath_rx_init(sc, ATH_RXBUF);
	if (error != 0)
		goto error_attach;

#if defined(CONFIG_RFKILL) || defined(CONFIG_RFKILL_MODULE)
	/* Initialze h/w Rfkill */
	if (sc->sc_ah->ah_caps.hw_caps & ATH9K_HW_CAP_RFSILENT)
		INIT_DELAYED_WORK(&sc->rf_kill.rfkill_poll, ath_rfkill_poll);

	/* Initialize s/w rfkill */
	error = ath_init_sw_rfkill(sc);
	if (error)
		goto error_attach;
#endif

	error = ieee80211_register_hw(hw);

	/* Initialize LED control */
	ath_init_leds(sc);

	return 0;

error_attach:
	/* cleanup tx queues */
	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++)
		if (ATH_TXQ_SETUP(sc, i))
			ath_tx_cleanupq(sc, &sc->tx.txq[i]);

	ath9k_hw_detach(sc->sc_ah);
	ath9k_exit_debug(sc);

	return error;
}

int ath_reset(struct ath_softc *sc, bool retry_tx)
{
	struct ath_hal *ah = sc->sc_ah;
	int status;
	int error = 0;

	ath9k_hw_set_interrupts(ah, 0);
	ath_draintxq(sc, retry_tx);
	ath_stoprecv(sc);
	ath_flushrecv(sc);

	spin_lock_bh(&sc->sc_resetlock);
	if (!ath9k_hw_reset(ah, sc->sc_ah->ah_curchan,
			    sc->tx_chan_width,
			    sc->sc_tx_chainmask, sc->sc_rx_chainmask,
			    sc->sc_ht_extprotspacing, false, &status)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to reset hardware; hal status %u\n", status);
		error = -EIO;
	}
	spin_unlock_bh(&sc->sc_resetlock);

	if (ath_startrecv(sc) != 0)
		DPRINTF(sc, ATH_DBG_FATAL, "Unable to start recv logic\n");

	/*
	 * We may be doing a reset in response to a request
	 * that changes the channel so update any state that
	 * might change as a result.
	 */
	ath_setcurmode(sc, ath_chan2mode(sc->sc_ah->ah_curchan));

	ath_update_txpow(sc);

	if (sc->sc_flags & SC_OP_BEACONS)
		ath_beacon_config(sc, ATH_IF_ID_ANY);	/* restart beacons */

	ath9k_hw_set_interrupts(ah, sc->sc_imask);

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

	return error;
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

	/* ath_desc must be a multiple of DWORDs */
	if ((sizeof(struct ath_desc) % 4) != 0) {
		DPRINTF(sc, ATH_DBG_FATAL, "ath_desc not DWORD aligned\n");
		ASSERT((sizeof(struct ath_desc) % 4) == 0);
		error = -ENOMEM;
		goto fail;
	}

	dd->dd_name = name;
	dd->dd_desc_len = sizeof(struct ath_desc) * nbuf * ndesc;

	/*
	 * Need additional DMA memory because we can't use
	 * descriptors that cross the 4K page boundary. Assume
	 * one skipped descriptor per 4K page.
	 */
	if (!(sc->sc_ah->ah_caps.hw_caps & ATH9K_HW_CAP_4KB_SPLITTRANS)) {
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
	dd->dd_desc = pci_alloc_consistent(sc->pdev,
			      dd->dd_desc_len,
			      &dd->dd_desc_paddr);
	if (dd->dd_desc == NULL) {
		error = -ENOMEM;
		goto fail;
	}
	ds = dd->dd_desc;
	DPRINTF(sc, ATH_DBG_CONFIG, "%s DMA map: %p (%u) -> %llx (%u)\n",
		dd->dd_name, ds, (u32) dd->dd_desc_len,
		ito64(dd->dd_desc_paddr), /*XXX*/(u32) dd->dd_desc_len);

	/* allocate buffers */
	bsize = sizeof(struct ath_buf) * nbuf;
	bf = kmalloc(bsize, GFP_KERNEL);
	if (bf == NULL) {
		error = -ENOMEM;
		goto fail2;
	}
	memset(bf, 0, bsize);
	dd->dd_bufptr = bf;

	INIT_LIST_HEAD(head);
	for (i = 0; i < nbuf; i++, bf++, ds += ndesc) {
		bf->bf_desc = ds;
		bf->bf_daddr = DS2PHYS(dd, ds);

		if (!(sc->sc_ah->ah_caps.hw_caps &
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
	pci_free_consistent(sc->pdev,
		dd->dd_desc_len, dd->dd_desc, dd->dd_desc_paddr);
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
	pci_free_consistent(sc->pdev,
		dd->dd_desc_len, dd->dd_desc, dd->dd_desc_paddr);

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

/**********************/
/* mac80211 callbacks */
/**********************/

static int ath9k_start(struct ieee80211_hw *hw)
{
	struct ath_softc *sc = hw->priv;
	struct ieee80211_channel *curchan = hw->conf.channel;
	struct ath9k_channel *init_channel;
	int error = 0, pos, status;

	DPRINTF(sc, ATH_DBG_CONFIG, "Starting driver with "
		"initial channel: %d MHz\n", curchan->center_freq);

	/* setup initial channel */

	pos = ath_get_channel(sc, curchan);
	if (pos == -1) {
		DPRINTF(sc, ATH_DBG_FATAL, "Invalid channel: %d\n", curchan->center_freq);
		error = -EINVAL;
		goto error;
	}

	sc->tx_chan_width = ATH9K_HT_MACMODE_20;
	sc->sc_ah->ah_channels[pos].chanmode =
		(curchan->band == IEEE80211_BAND_2GHZ) ? CHANNEL_G : CHANNEL_A;
	init_channel = &sc->sc_ah->ah_channels[pos];

	/* Reset SERDES registers */
	ath9k_hw_configpcipowersave(sc->sc_ah, 0);

	/*
	 * The basic interface to setting the hardware in a good
	 * state is ``reset''.  On return the hardware is known to
	 * be powered up and with interrupts disabled.  This must
	 * be followed by initialization of the appropriate bits
	 * and then setup of the interrupt mask.
	 */
	spin_lock_bh(&sc->sc_resetlock);
	if (!ath9k_hw_reset(sc->sc_ah, init_channel,
			    sc->tx_chan_width,
			    sc->sc_tx_chainmask, sc->sc_rx_chainmask,
			    sc->sc_ht_extprotspacing, false, &status)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to reset hardware; hal status %u "
			"(freq %u flags 0x%x)\n", status,
			init_channel->channel, init_channel->channelFlags);
		error = -EIO;
		spin_unlock_bh(&sc->sc_resetlock);
		goto error;
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
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to start recv logic\n");
		error = -EIO;
		goto error;
	}

	/* Setup our intr mask. */
	sc->sc_imask = ATH9K_INT_RX | ATH9K_INT_TX
		| ATH9K_INT_RXEOL | ATH9K_INT_RXORN
		| ATH9K_INT_FATAL | ATH9K_INT_GLOBAL;

	if (sc->sc_ah->ah_caps.hw_caps & ATH9K_HW_CAP_GTT)
		sc->sc_imask |= ATH9K_INT_GTT;

	if (sc->sc_ah->ah_caps.hw_caps & ATH9K_HW_CAP_HT)
		sc->sc_imask |= ATH9K_INT_CST;

	/*
	 * Enable MIB interrupts when there are hardware phy counters.
	 * Note we only do this (at the moment) for station mode.
	 */
	if (ath9k_hw_phycounters(sc->sc_ah) &&
	    ((sc->sc_ah->ah_opmode == NL80211_IFTYPE_STATION) ||
	     (sc->sc_ah->ah_opmode == NL80211_IFTYPE_ADHOC)))
		sc->sc_imask |= ATH9K_INT_MIB;
	/*
	 * Some hardware processes the TIM IE and fires an
	 * interrupt when the TIM bit is set.  For hardware
	 * that does, if not overridden by configuration,
	 * enable the TIM interrupt when operating as station.
	 */
	if ((sc->sc_ah->ah_caps.hw_caps & ATH9K_HW_CAP_ENHANCEDPM) &&
	    (sc->sc_ah->ah_opmode == NL80211_IFTYPE_STATION) &&
	    !sc->sc_config.swBeaconProcess)
		sc->sc_imask |= ATH9K_INT_TIM;

	ath_setcurmode(sc, ath_chan2mode(init_channel));

	sc->sc_flags &= ~SC_OP_INVALID;

	/* Disable BMISS interrupt when we're not associated */
	sc->sc_imask &= ~(ATH9K_INT_SWBA | ATH9K_INT_BMISS);
	ath9k_hw_set_interrupts(sc->sc_ah, sc->sc_imask);

	ieee80211_wake_queues(sc->hw);

#if defined(CONFIG_RFKILL) || defined(CONFIG_RFKILL_MODULE)
	error = ath_start_rfkill_poll(sc);
#endif

error:
	return error;
}

static int ath9k_tx(struct ieee80211_hw *hw,
		    struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ath_softc *sc = hw->priv;
	struct ath_tx_control txctl;
	int hdrlen, padsize;

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

	if (ath_tx_start(sc, skb, &txctl) != 0) {
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
	struct ath_softc *sc = hw->priv;

	if (sc->sc_flags & SC_OP_INVALID) {
		DPRINTF(sc, ATH_DBG_ANY, "Device not present\n");
		return;
	}

	DPRINTF(sc, ATH_DBG_CONFIG, "Cleaning up\n");

	ieee80211_stop_queues(sc->hw);

	/* make sure h/w will not generate any interrupt
	 * before setting the invalid flag. */
	ath9k_hw_set_interrupts(sc->sc_ah, 0);

	if (!(sc->sc_flags & SC_OP_INVALID)) {
		ath_draintxq(sc, false);
		ath_stoprecv(sc);
		ath9k_hw_phy_disable(sc->sc_ah);
	} else
		sc->rx.rxlink = NULL;

#if defined(CONFIG_RFKILL) || defined(CONFIG_RFKILL_MODULE)
	if (sc->sc_ah->ah_caps.hw_caps & ATH9K_HW_CAP_RFSILENT)
		cancel_delayed_work_sync(&sc->rf_kill.rfkill_poll);
#endif
	/* disable HAL and put h/w to sleep */
	ath9k_hw_disable(sc->sc_ah);
	ath9k_hw_configpcipowersave(sc->sc_ah, 1);

	sc->sc_flags |= SC_OP_INVALID;

	DPRINTF(sc, ATH_DBG_CONFIG, "Driver halt\n");
}

static int ath9k_add_interface(struct ieee80211_hw *hw,
			       struct ieee80211_if_init_conf *conf)
{
	struct ath_softc *sc = hw->priv;
	struct ath_vap *avp = (void *)conf->vif->drv_priv;
	enum nl80211_iftype ic_opmode = NL80211_IFTYPE_UNSPECIFIED;

	/* Support only vap for now */

	if (sc->sc_nvaps)
		return -ENOBUFS;

	switch (conf->type) {
	case NL80211_IFTYPE_STATION:
		ic_opmode = NL80211_IFTYPE_STATION;
		break;
	case NL80211_IFTYPE_ADHOC:
		ic_opmode = NL80211_IFTYPE_ADHOC;
		break;
	case NL80211_IFTYPE_AP:
		ic_opmode = NL80211_IFTYPE_AP;
		break;
	default:
		DPRINTF(sc, ATH_DBG_FATAL,
			"Interface type %d not yet supported\n", conf->type);
		return -EOPNOTSUPP;
	}

	DPRINTF(sc, ATH_DBG_CONFIG, "Attach a VAP of type: %d\n", ic_opmode);

	/* Set the VAP opmode */
	avp->av_opmode = ic_opmode;
	avp->av_bslot = -1;

	if (ic_opmode == NL80211_IFTYPE_AP)
		ath9k_hw_set_tsfadjust(sc->sc_ah, 1);

	sc->sc_vaps[0] = conf->vif;
	sc->sc_nvaps++;

	/* Set the device opmode */
	sc->sc_ah->ah_opmode = ic_opmode;

	if (conf->type == NL80211_IFTYPE_AP) {
		/* TODO: is this a suitable place to start ANI for AP mode? */
		/* Start ANI */
		mod_timer(&sc->sc_ani.timer,
			  jiffies + msecs_to_jiffies(ATH_ANI_POLLINTERVAL));
	}

	return 0;
}

static void ath9k_remove_interface(struct ieee80211_hw *hw,
				   struct ieee80211_if_init_conf *conf)
{
	struct ath_softc *sc = hw->priv;
	struct ath_vap *avp = (void *)conf->vif->drv_priv;

	DPRINTF(sc, ATH_DBG_CONFIG, "Detach Interface\n");

	/* Stop ANI */
	del_timer_sync(&sc->sc_ani.timer);

	/* Reclaim beacon resources */
	if (sc->sc_ah->ah_opmode == NL80211_IFTYPE_AP ||
	    sc->sc_ah->ah_opmode == NL80211_IFTYPE_ADHOC) {
		ath9k_hw_stoptxdma(sc->sc_ah, sc->beacon.beaconq);
		ath_beacon_return(sc, avp);
	}

	sc->sc_flags &= ~SC_OP_BEACONS;

	sc->sc_vaps[0] = NULL;
	sc->sc_nvaps--;
}

static int ath9k_config(struct ieee80211_hw *hw, u32 changed)
{
	struct ath_softc *sc = hw->priv;
	struct ieee80211_conf *conf = &hw->conf;

	mutex_lock(&sc->mutex);
	if (changed & (IEEE80211_CONF_CHANGE_CHANNEL |
		       IEEE80211_CONF_CHANGE_HT)) {
		struct ieee80211_channel *curchan = hw->conf.channel;
		int pos;

		DPRINTF(sc, ATH_DBG_CONFIG, "Set channel: %d MHz\n",
			curchan->center_freq);

		pos = ath_get_channel(sc, curchan);
		if (pos == -1) {
			DPRINTF(sc, ATH_DBG_FATAL, "Invalid channel: %d\n",
				curchan->center_freq);
			mutex_unlock(&sc->mutex);
			return -EINVAL;
		}

		sc->tx_chan_width = ATH9K_HT_MACMODE_20;
		sc->sc_ah->ah_channels[pos].chanmode =
			(curchan->band == IEEE80211_BAND_2GHZ) ?
			CHANNEL_G : CHANNEL_A;

		if (conf->ht.enabled) {
			if (conf->ht.channel_type == NL80211_CHAN_HT40PLUS ||
			    conf->ht.channel_type == NL80211_CHAN_HT40MINUS)
				sc->tx_chan_width = ATH9K_HT_MACMODE_2040;

			sc->sc_ah->ah_channels[pos].chanmode =
				ath_get_extchanmode(sc, curchan,
						    conf->ht.channel_type);
		}

		ath_update_chainmask(sc, conf->ht.enabled);

		if (ath_set_channel(sc, &sc->sc_ah->ah_channels[pos]) < 0) {
			DPRINTF(sc, ATH_DBG_FATAL, "Unable to set channel\n");
			mutex_unlock(&sc->mutex);
			return -EINVAL;
		}
	}

	if (changed & IEEE80211_CONF_CHANGE_POWER)
		sc->sc_config.txpowlimit = 2 * conf->power_level;

	mutex_unlock(&sc->mutex);
	return 0;
}

static int ath9k_config_interface(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_if_conf *conf)
{
	struct ath_softc *sc = hw->priv;
	struct ath_hal *ah = sc->sc_ah;
	struct ath_vap *avp = (void *)vif->drv_priv;
	u32 rfilt = 0;
	int error, i;

	/* TODO: Need to decide which hw opmode to use for multi-interface
	 * cases */
	if (vif->type == NL80211_IFTYPE_AP &&
	    ah->ah_opmode != NL80211_IFTYPE_AP) {
		ah->ah_opmode = NL80211_IFTYPE_STATION;
		ath9k_hw_setopmode(ah);
		ath9k_hw_write_associd(ah, sc->sc_myaddr, 0);
		/* Request full reset to get hw opmode changed properly */
		sc->sc_flags |= SC_OP_FULL_RESET;
	}

	if ((conf->changed & IEEE80211_IFCC_BSSID) &&
	    !is_zero_ether_addr(conf->bssid)) {
		switch (vif->type) {
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_ADHOC:
			/* Set BSSID */
			memcpy(sc->sc_curbssid, conf->bssid, ETH_ALEN);
			sc->sc_curaid = 0;
			ath9k_hw_write_associd(sc->sc_ah, sc->sc_curbssid,
					       sc->sc_curaid);

			/* Set aggregation protection mode parameters */
			sc->sc_config.ath_aggr_prot = 0;

			DPRINTF(sc, ATH_DBG_CONFIG,
				"RX filter 0x%x bssid %pM aid 0x%x\n",
				rfilt, sc->sc_curbssid, sc->sc_curaid);

			/* need to reconfigure the beacon */
			sc->sc_flags &= ~SC_OP_BEACONS ;

			break;
		default:
			break;
		}
	}

	if ((conf->changed & IEEE80211_IFCC_BEACON) &&
	    ((vif->type == NL80211_IFTYPE_ADHOC) ||
	     (vif->type == NL80211_IFTYPE_AP))) {
		/*
		 * Allocate and setup the beacon frame.
		 *
		 * Stop any previous beacon DMA.  This may be
		 * necessary, for example, when an ibss merge
		 * causes reconfiguration; we may be called
		 * with beacon transmission active.
		 */
		ath9k_hw_stoptxdma(sc->sc_ah, sc->beacon.beaconq);

		error = ath_beacon_alloc(sc, 0);
		if (error != 0)
			return error;

		ath_beacon_sync(sc, 0);
	}

	/* Check for WLAN_CAPABILITY_PRIVACY ? */
	if ((avp->av_opmode != NL80211_IFTYPE_STATION)) {
		for (i = 0; i < IEEE80211_WEP_NKID; i++)
			if (ath9k_hw_keyisvalid(sc->sc_ah, (u16)i))
				ath9k_hw_keysetmac(sc->sc_ah,
						   (u16)i,
						   sc->sc_curbssid);
	}

	/* Only legacy IBSS for now */
	if (vif->type == NL80211_IFTYPE_ADHOC)
		ath_update_chainmask(sc, 0);

	return 0;
}

#define SUPPORTED_FILTERS			\
	(FIF_PROMISC_IN_BSS |			\
	FIF_ALLMULTI |				\
	FIF_CONTROL |				\
	FIF_OTHER_BSS |				\
	FIF_BCN_PRBRESP_PROMISC |		\
	FIF_FCSFAIL)

/* FIXME: sc->sc_full_reset ? */
static void ath9k_configure_filter(struct ieee80211_hw *hw,
				   unsigned int changed_flags,
				   unsigned int *total_flags,
				   int mc_count,
				   struct dev_mc_list *mclist)
{
	struct ath_softc *sc = hw->priv;
	u32 rfilt;

	changed_flags &= SUPPORTED_FILTERS;
	*total_flags &= SUPPORTED_FILTERS;

	sc->rx.rxfilter = *total_flags;
	rfilt = ath_calcrxfilter(sc);
	ath9k_hw_setrxfilter(sc->sc_ah, rfilt);

	if (changed_flags & FIF_BCN_PRBRESP_PROMISC) {
		if (*total_flags & FIF_BCN_PRBRESP_PROMISC)
			ath9k_hw_write_associd(sc->sc_ah, ath_bcast_mac, 0);
	}

	DPRINTF(sc, ATH_DBG_CONFIG, "Set HW RX filter: 0x%x\n", sc->rx.rxfilter);
}

static void ath9k_sta_notify(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     enum sta_notify_cmd cmd,
			     struct ieee80211_sta *sta)
{
	struct ath_softc *sc = hw->priv;

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

static int ath9k_conf_tx(struct ieee80211_hw *hw,
			 u16 queue,
			 const struct ieee80211_tx_queue_params *params)
{
	struct ath_softc *sc = hw->priv;
	struct ath9k_tx_queue_info qi;
	int ret = 0, qnum;

	if (queue >= WME_NUM_AC)
		return 0;

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

	return ret;
}

static int ath9k_set_key(struct ieee80211_hw *hw,
			 enum set_key_cmd cmd,
			 const u8 *local_addr,
			 const u8 *addr,
			 struct ieee80211_key_conf *key)
{
	struct ath_softc *sc = hw->priv;
	int ret = 0;

	DPRINTF(sc, ATH_DBG_KEYCACHE, "Set HW Key\n");

	switch (cmd) {
	case SET_KEY:
		ret = ath_key_config(sc, addr, key);
		if (ret >= 0) {
			key->hw_key_idx = ret;
			/* push IV and Michael MIC generation to stack */
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
			if (key->alg == ALG_TKIP)
				key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
			ret = 0;
		}
		break;
	case DISABLE_KEY:
		ath_key_delete(sc, key);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static void ath9k_bss_info_changed(struct ieee80211_hw *hw,
				   struct ieee80211_vif *vif,
				   struct ieee80211_bss_conf *bss_conf,
				   u32 changed)
{
	struct ath_softc *sc = hw->priv;

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
}

static u64 ath9k_get_tsf(struct ieee80211_hw *hw)
{
	u64 tsf;
	struct ath_softc *sc = hw->priv;
	struct ath_hal *ah = sc->sc_ah;

	tsf = ath9k_hw_gettsf64(ah);

	return tsf;
}

static void ath9k_reset_tsf(struct ieee80211_hw *hw)
{
	struct ath_softc *sc = hw->priv;
	struct ath_hal *ah = sc->sc_ah;

	ath9k_hw_reset_tsf(ah);
}

static int ath9k_ampdu_action(struct ieee80211_hw *hw,
		       enum ieee80211_ampdu_mlme_action action,
		       struct ieee80211_sta *sta,
		       u16 tid, u16 *ssn)
{
	struct ath_softc *sc = hw->priv;
	int ret = 0;

	switch (action) {
	case IEEE80211_AMPDU_RX_START:
		if (!(sc->sc_flags & SC_OP_RXAGGR))
			ret = -ENOTSUPP;
		break;
	case IEEE80211_AMPDU_RX_STOP:
		break;
	case IEEE80211_AMPDU_TX_START:
		ret = ath_tx_aggr_start(sc, sta, tid, ssn);
		if (ret < 0)
			DPRINTF(sc, ATH_DBG_FATAL,
				"Unable to start TX aggregation\n");
		else
			ieee80211_start_tx_ba_cb_irqsafe(hw, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_STOP:
		ret = ath_tx_aggr_stop(sc, sta, tid);
		if (ret < 0)
			DPRINTF(sc, ATH_DBG_FATAL,
				"Unable to stop TX aggregation\n");

		ieee80211_stop_tx_ba_cb_irqsafe(hw, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_RESUME:
		ath_tx_aggr_resume(sc, sta, tid);
		break;
	default:
		DPRINTF(sc, ATH_DBG_FATAL, "Unknown AMPDU action\n");
	}

	return ret;
}

static struct ieee80211_ops ath9k_ops = {
	.tx 		    = ath9k_tx,
	.start 		    = ath9k_start,
	.stop 		    = ath9k_stop,
	.add_interface 	    = ath9k_add_interface,
	.remove_interface   = ath9k_remove_interface,
	.config 	    = ath9k_config,
	.config_interface   = ath9k_config_interface,
	.configure_filter   = ath9k_configure_filter,
	.sta_notify         = ath9k_sta_notify,
	.conf_tx 	    = ath9k_conf_tx,
	.bss_info_changed   = ath9k_bss_info_changed,
	.set_key            = ath9k_set_key,
	.get_tsf 	    = ath9k_get_tsf,
	.reset_tsf 	    = ath9k_reset_tsf,
	.ampdu_action       = ath9k_ampdu_action,
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
	{ AR_SREV_VERSION_9285,		"9285" }
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
static const char *
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
static const char *
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

static int ath_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	void __iomem *mem;
	struct ath_softc *sc;
	struct ieee80211_hw *hw;
	u8 csz;
	u32 val;
	int ret = 0;
	struct ath_hal *ah;

	if (pci_enable_device(pdev))
		return -EIO;

	ret =  pci_set_dma_mask(pdev, DMA_32BIT_MASK);

	if (ret) {
		printk(KERN_ERR "ath9k: 32-bit DMA not available\n");
		goto bad;
	}

	ret = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);

	if (ret) {
		printk(KERN_ERR "ath9k: 32-bit DMA consistent "
			"DMA enable failed\n");
		goto bad;
	}

	/*
	 * Cache line size is used to size and align various
	 * structures used to communicate with the hardware.
	 */
	pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE, &csz);
	if (csz == 0) {
		/*
		 * Linux 2.4.18 (at least) writes the cache line size
		 * register as a 16-bit wide register which is wrong.
		 * We must have this setup properly for rx buffer
		 * DMA to work so force a reasonable value here if it
		 * comes up zero.
		 */
		csz = L1_CACHE_BYTES / sizeof(u32);
		pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE, csz);
	}
	/*
	 * The default setting of latency timer yields poor results,
	 * set it to the value used by other systems. It may be worth
	 * tweaking this setting more.
	 */
	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0xa8);

	pci_set_master(pdev);

	/*
	 * Disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state.
	 */
	pci_read_config_dword(pdev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(pdev, 0x40, val & 0xffff00ff);

	ret = pci_request_region(pdev, 0, "ath9k");
	if (ret) {
		dev_err(&pdev->dev, "PCI memory region reserve error\n");
		ret = -ENODEV;
		goto bad;
	}

	mem = pci_iomap(pdev, 0, 0);
	if (!mem) {
		printk(KERN_ERR "PCI memory map error\n") ;
		ret = -EIO;
		goto bad1;
	}

	hw = ieee80211_alloc_hw(sizeof(struct ath_softc), &ath9k_ops);
	if (hw == NULL) {
		printk(KERN_ERR "ath_pci: no memory for ieee80211_hw\n");
		goto bad2;
	}

	SET_IEEE80211_DEV(hw, &pdev->dev);
	pci_set_drvdata(pdev, hw);

	sc = hw->priv;
	sc->hw = hw;
	sc->pdev = pdev;
	sc->mem = mem;

	if (ath_attach(id->device, sc) != 0) {
		ret = -ENODEV;
		goto bad3;
	}

	/* setup interrupt service routine */

	if (request_irq(pdev->irq, ath_isr, IRQF_SHARED, "ath", sc)) {
		printk(KERN_ERR "%s: request_irq failed\n",
			wiphy_name(hw->wiphy));
		ret = -EIO;
		goto bad4;
	}

	ah = sc->sc_ah;
	printk(KERN_INFO
	       "%s: Atheros AR%s MAC/BB Rev:%x "
	       "AR%s RF Rev:%x: mem=0x%lx, irq=%d\n",
	       wiphy_name(hw->wiphy),
	       ath_mac_bb_name(ah->ah_macVersion),
	       ah->ah_macRev,
	       ath_rf_name((ah->ah_analog5GhzRev & AR_RADIO_SREV_MAJOR)),
	       ah->ah_phyRev,
	       (unsigned long)mem, pdev->irq);

	return 0;
bad4:
	ath_detach(sc);
bad3:
	ieee80211_free_hw(hw);
bad2:
	pci_iounmap(pdev, mem);
bad1:
	pci_release_region(pdev, 0);
bad:
	pci_disable_device(pdev);
	return ret;
}

static void ath_pci_remove(struct pci_dev *pdev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct ath_softc *sc = hw->priv;

	ath_detach(sc);
	if (pdev->irq)
		free_irq(pdev->irq, sc);
	pci_iounmap(pdev, sc->mem);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
	ieee80211_free_hw(hw);
}

#ifdef CONFIG_PM

static int ath_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct ath_softc *sc = hw->priv;

	ath9k_hw_set_gpio(sc->sc_ah, ATH_LED_PIN, 1);

#if defined(CONFIG_RFKILL) || defined(CONFIG_RFKILL_MODULE)
	if (sc->sc_ah->ah_caps.hw_caps & ATH9K_HW_CAP_RFSILENT)
		cancel_delayed_work_sync(&sc->rf_kill.rfkill_poll);
#endif

	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, 3);

	return 0;
}

static int ath_pci_resume(struct pci_dev *pdev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct ath_softc *sc = hw->priv;
	u32 val;
	int err;

	err = pci_enable_device(pdev);
	if (err)
		return err;
	pci_restore_state(pdev);
	/*
	 * Suspend/Resume resets the PCI configuration space, so we have to
	 * re-disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state
	 */
	pci_read_config_dword(pdev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(pdev, 0x40, val & 0xffff00ff);

	/* Enable LED */
	ath9k_hw_cfg_output(sc->sc_ah, ATH_LED_PIN,
			    AR_GPIO_OUTPUT_MUX_AS_OUTPUT);
	ath9k_hw_set_gpio(sc->sc_ah, ATH_LED_PIN, 1);

#if defined(CONFIG_RFKILL) || defined(CONFIG_RFKILL_MODULE)
	/*
	 * check the h/w rfkill state on resume
	 * and start the rfkill poll timer
	 */
	if (sc->sc_ah->ah_caps.hw_caps & ATH9K_HW_CAP_RFSILENT)
		queue_delayed_work(sc->hw->workqueue,
				   &sc->rf_kill.rfkill_poll, 0);
#endif

	return 0;
}

#endif /* CONFIG_PM */

MODULE_DEVICE_TABLE(pci, ath_pci_id_table);

static struct pci_driver ath_pci_driver = {
	.name       = "ath9k",
	.id_table   = ath_pci_id_table,
	.probe      = ath_pci_probe,
	.remove     = ath_pci_remove,
#ifdef CONFIG_PM
	.suspend    = ath_pci_suspend,
	.resume     = ath_pci_resume,
#endif /* CONFIG_PM */
};

static int __init init_ath_pci(void)
{
	int error;

	printk(KERN_INFO "%s: %s\n", dev_info, ATH_PCI_VERSION);

	/* Register rate control algorithm */
	error = ath_rate_control_register();
	if (error != 0) {
		printk(KERN_ERR
			"Unable to register rate control algorithm: %d\n",
			error);
		ath_rate_control_unregister();
		return error;
	}

	if (pci_register_driver(&ath_pci_driver) < 0) {
		printk(KERN_ERR
			"ath_pci: No devices found, driver not installed.\n");
		ath_rate_control_unregister();
		pci_unregister_driver(&ath_pci_driver);
		return -ENODEV;
	}

	return 0;
}
module_init(init_ath_pci);

static void __exit exit_ath_pci(void)
{
	ath_rate_control_unregister();
	pci_unregister_driver(&ath_pci_driver);
	printk(KERN_INFO "%s: Driver unloaded\n", dev_info);
}
module_exit(exit_ath_pci);
