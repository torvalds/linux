/*
 * Copyright (c) 2008, Atheros Communications Inc.
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

 /* Implementation of the main "ATH" layer. */

#include "core.h"
#include "regd.h"

static int ath_outdoor;		/* enable outdoor use */

static u32 ath_chainmask_sel_up_rssi_thres =
	ATH_CHAINMASK_SEL_UP_RSSI_THRES;
static u32 ath_chainmask_sel_down_rssi_thres =
	ATH_CHAINMASK_SEL_DOWN_RSSI_THRES;
static u32 ath_chainmask_sel_period =
	ATH_CHAINMASK_SEL_TIMEOUT;

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

/*
 *  Set current operating mode
 *
 *  This function initializes and fills the rate table in the ATH object based
 *  on the operating mode.  The blink rates are also set up here, although
 *  they have been superceeded by the ath_led module.
*/

static void ath_setcurmode(struct ath_softc *sc, enum wireless_mode mode)
{
	const struct ath9k_rate_table *rt;
	int i;

	memset(sc->sc_rixmap, 0xff, sizeof(sc->sc_rixmap));
	rt = ath9k_hw_getratetable(sc->sc_ah, mode);
	BUG_ON(!rt);

	for (i = 0; i < rt->rateCount; i++)
		sc->sc_rixmap[rt->info[i].rateCode] = (u8) i;

	memzero(sc->sc_hwmap, sizeof(sc->sc_hwmap));
	for (i = 0; i < 256; i++) {
		u8 ix = rt->rateCodeToIndex[i];

		if (ix == 0xff)
			continue;

		sc->sc_hwmap[i].ieeerate =
		    rt->info[ix].dot11Rate & IEEE80211_RATE_VAL;
		sc->sc_hwmap[i].rateKbps = rt->info[ix].rateKbps;

		if (rt->info[ix].shortPreamble ||
		    rt->info[ix].phy == PHY_OFDM) {
			/* XXX: Handle this */
		}

		/* NB: this uses the last entry if the rate isn't found */
		/* XXX beware of overlow */
	}
	sc->sc_currates = rt;
	sc->sc_curmode = mode;
	/*
	 * All protection frames are transmited at 2Mb/s for
	 * 11g, otherwise at 1Mb/s.
	 * XXX select protection rate index from rate table.
	 */
	sc->sc_protrix = (mode == ATH9K_MODE_11G ? 1 : 0);
}

/*
 * Set up rate table (legacy rates)
 */
static void ath_setup_rates(struct ath_softc *sc, enum ieee80211_band band)
{
	struct ath_hal *ah = sc->sc_ah;
	const struct ath9k_rate_table *rt = NULL;
	struct ieee80211_supported_band *sband;
	struct ieee80211_rate *rate;
	int i, maxrates;

	switch (band) {
	case IEEE80211_BAND_2GHZ:
		rt = ath9k_hw_getratetable(ah, ATH9K_MODE_11G);
		break;
	case IEEE80211_BAND_5GHZ:
		rt = ath9k_hw_getratetable(ah, ATH9K_MODE_11A);
		break;
	default:
		break;
	}

	if (rt == NULL)
		return;

	sband = &sc->sbands[band];
	rate = sc->rates[band];

	if (rt->rateCount > ATH_RATE_MAX)
		maxrates = ATH_RATE_MAX;
	else
		maxrates = rt->rateCount;

	for (i = 0; i < maxrates; i++) {
		rate[i].bitrate = rt->info[i].rateKbps / 100;
		rate[i].hw_value = rt->info[i].rateCode;
		sband->n_bitrates++;
		DPRINTF(sc, ATH_DBG_CONFIG,
			"%s: Rate: %2dMbps, ratecode: %2d\n",
			__func__,
			rate[i].bitrate / 10,
			rate[i].hw_value);
	}
}

/*
 *  Set up channel list
 */
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
	if (!ath9k_regd_init_channels(ah,
				      ATH_CHAN_MAX,
				      (u32 *)&nchan,
				      regclassids,
				      ATH_REGCLASSIDS_MAX,
				      &nregclass,
				      CTRY_DEFAULT,
				      false,
				      1)) {
		u32 rd = ah->ah_currentRD;

		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: unable to collect channel list; "
			"regdomain likely %u country code %u\n",
			__func__, rd, CTRY_DEFAULT);
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
				chan_2ghz[a].flags |=
					IEEE80211_CHAN_NO_IBSS;
			if (c->channelFlags & CHANNEL_PASSIVE)
				chan_2ghz[a].flags |=
					IEEE80211_CHAN_PASSIVE_SCAN;

			band_2ghz->n_channels = ++a;

			DPRINTF(sc, ATH_DBG_CONFIG,
				"%s: 2MHz channel: %d, "
				"channelFlags: 0x%x\n",
				__func__,
				c->channel,
				c->channelFlags);
		} else if (IS_CHAN_5GHZ(c)) {
			chan_5ghz[b].band = IEEE80211_BAND_5GHZ;
			chan_5ghz[b].center_freq = c->channel;
			chan_5ghz[b].max_power = c->maxTxPower;

			if (c->privFlags & CHANNEL_DISALLOW_ADHOC)
				chan_5ghz[b].flags |=
					IEEE80211_CHAN_NO_IBSS;
			if (c->channelFlags & CHANNEL_PASSIVE)
				chan_5ghz[b].flags |=
					IEEE80211_CHAN_PASSIVE_SCAN;

			band_5ghz->n_channels = ++b;

			DPRINTF(sc, ATH_DBG_CONFIG,
				"%s: 5MHz channel: %d, "
				"channelFlags: 0x%x\n",
				__func__,
				c->channel,
				c->channelFlags);
		}
	}

	return 0;
}

/*
 *  Determine mode from channel flags
 *
 *  This routine will provide the enumerated WIRELESSS_MODE value based
 *  on the settings of the channel flags.  If ho valid set of flags
 *  exist, the lowest mode (11b) is selected.
*/

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

	/* NB: should not get here */
	return ATH9K_MODE_11B;
}

/*
 * Stop the device, grabbing the top-level lock to protect
 * against concurrent entry through ath_init (which can happen
 * if another thread does a system call and the thread doing the
 * stop is preempted).
 */

static int ath_stop(struct ath_softc *sc)
{
	struct ath_hal *ah = sc->sc_ah;

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: invalid %u\n",
		__func__, sc->sc_invalid);

	/*
	 * Shutdown the hardware and driver:
	 *    stop output from above
	 *    reset 802.11 state machine
	 *      (sends station deassoc/deauth frames)
	 *    turn off timers
	 *    disable interrupts
	 *    clear transmit machinery
	 *    clear receive machinery
	 *    turn off the radio
	 *    reclaim beacon resources
	 *
	 * Note that some of this work is not possible if the
	 * hardware is gone (invalid).
	 */

	if (!sc->sc_invalid)
		ath9k_hw_set_interrupts(ah, 0);
	ath_draintxq(sc, false);
	if (!sc->sc_invalid) {
		ath_stoprecv(sc);
		ath9k_hw_phy_disable(ah);
	} else
		sc->sc_rxlink = NULL;

	return 0;
}

/*
 * Set the current channel
 *
 * Set/change channels.  If the channel is really being changed, it's done
 * by reseting the chip.  To accomplish this we must first cleanup any pending
 * DMA, then restart stuff after a la ath_init.
*/
int ath_set_channel(struct ath_softc *sc, struct ath9k_channel *hchan)
{
	struct ath_hal *ah = sc->sc_ah;
	bool fastcc = true, stopped;
	enum ath9k_ht_macmode ht_macmode;

	if (sc->sc_invalid)	/* if the device is invalid or removed */
		return -EIO;

	DPRINTF(sc, ATH_DBG_CONFIG,
		"%s: %u (%u MHz) -> %u (%u MHz), cflags:%x\n",
		__func__,
		ath9k_hw_mhz2ieee(ah, sc->sc_curchan.channel,
				  sc->sc_curchan.channelFlags),
		sc->sc_curchan.channel,
		ath9k_hw_mhz2ieee(ah, hchan->channel, hchan->channelFlags),
		hchan->channel, hchan->channelFlags);

	ht_macmode = ath_cwm_macmode(sc);

	if (hchan->channel != sc->sc_curchan.channel ||
	    hchan->channelFlags != sc->sc_curchan.channelFlags ||
	    sc->sc_update_chainmask || sc->sc_full_reset) {
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
		ath9k_hw_set_interrupts(ah, 0);	/* disable interrupts */
		ath_draintxq(sc, false);	/* clear pending tx frames */
		stopped = ath_stoprecv(sc);	/* turn off frame recv */

		/* XXX: do not flush receive queue here. We don't want
		 * to flush data frames already in queue because of
		 * changing channel. */

		if (!stopped || sc->sc_full_reset)
			fastcc = false;

		spin_lock_bh(&sc->sc_resetlock);
		if (!ath9k_hw_reset(ah, sc->sc_opmode, hchan,
					ht_macmode, sc->sc_tx_chainmask,
					sc->sc_rx_chainmask,
					sc->sc_ht_extprotspacing,
					fastcc, &status)) {
			DPRINTF(sc, ATH_DBG_FATAL,
				"%s: unable to reset channel %u (%uMhz) "
				"flags 0x%x hal status %u\n", __func__,
				ath9k_hw_mhz2ieee(ah, hchan->channel,
						  hchan->channelFlags),
				hchan->channel, hchan->channelFlags, status);
			spin_unlock_bh(&sc->sc_resetlock);
			return -EIO;
		}
		spin_unlock_bh(&sc->sc_resetlock);

		sc->sc_curchan = *hchan;
		sc->sc_update_chainmask = 0;
		sc->sc_full_reset = 0;

		/* Re-enable rx framework */
		if (ath_startrecv(sc) != 0) {
			DPRINTF(sc, ATH_DBG_FATAL,
				"%s: unable to restart recv logic\n", __func__);
			return -EIO;
		}
		/*
		 * Change channels and update the h/w rate map
		 * if we're switching; e.g. 11a to 11b/g.
		 */
		ath_setcurmode(sc, ath_chan2mode(hchan));

		ath_update_txpow(sc);	/* update tx power state */
		/*
		 * Re-enable interrupts.
		 */
		ath9k_hw_set_interrupts(ah, sc->sc_imask);
	}
	return 0;
}

/**********************/
/* Chainmask Handling */
/**********************/

static void ath_chainmask_sel_timertimeout(unsigned long data)
{
	struct ath_chainmask_sel *cm = (struct ath_chainmask_sel *)data;
	cm->switch_allowed = 1;
}

/* Start chainmask select timer */
static void ath_chainmask_sel_timerstart(struct ath_chainmask_sel *cm)
{
	cm->switch_allowed = 0;
	mod_timer(&cm->timer, ath_chainmask_sel_period);
}

/* Stop chainmask select timer */
static void ath_chainmask_sel_timerstop(struct ath_chainmask_sel *cm)
{
	cm->switch_allowed = 0;
	del_timer_sync(&cm->timer);
}

static void ath_chainmask_sel_init(struct ath_softc *sc, struct ath_node *an)
{
	struct ath_chainmask_sel *cm = &an->an_chainmask_sel;

	memzero(cm, sizeof(struct ath_chainmask_sel));

	cm->cur_tx_mask = sc->sc_tx_chainmask;
	cm->cur_rx_mask = sc->sc_rx_chainmask;
	cm->tx_avgrssi = ATH_RSSI_DUMMY_MARKER;
	setup_timer(&cm->timer,
		ath_chainmask_sel_timertimeout, (unsigned long) cm);
}

int ath_chainmask_sel_logic(struct ath_softc *sc, struct ath_node *an)
{
	struct ath_chainmask_sel *cm = &an->an_chainmask_sel;

	/*
	 * Disable auto-swtiching in one of the following if conditions.
	 * sc_chainmask_auto_sel is used for internal global auto-switching
	 * enabled/disabled setting
	 */
	if (sc->sc_ah->ah_caps.tx_chainmask != ATH_CHAINMASK_SEL_3X3) {
		cm->cur_tx_mask = sc->sc_tx_chainmask;
		return cm->cur_tx_mask;
	}

	if (cm->tx_avgrssi == ATH_RSSI_DUMMY_MARKER)
		return cm->cur_tx_mask;

	if (cm->switch_allowed) {
		/* Switch down from tx 3 to tx 2. */
		if (cm->cur_tx_mask == ATH_CHAINMASK_SEL_3X3 &&
		    ATH_RSSI_OUT(cm->tx_avgrssi) >=
		    ath_chainmask_sel_down_rssi_thres) {
			cm->cur_tx_mask = sc->sc_tx_chainmask;

			/* Don't let another switch happen until
			 * this timer expires */
			ath_chainmask_sel_timerstart(cm);
		}
		/* Switch up from tx 2 to 3. */
		else if (cm->cur_tx_mask == sc->sc_tx_chainmask &&
			 ATH_RSSI_OUT(cm->tx_avgrssi) <=
			 ath_chainmask_sel_up_rssi_thres) {
			cm->cur_tx_mask = ATH_CHAINMASK_SEL_3X3;

			/* Don't let another switch happen
			 * until this timer expires */
			ath_chainmask_sel_timerstart(cm);
		}
	}

	return cm->cur_tx_mask;
}

/*
 * Update tx/rx chainmask. For legacy association,
 * hard code chainmask to 1x1, for 11n association, use
 * the chainmask configuration.
 */

void ath_update_chainmask(struct ath_softc *sc, int is_ht)
{
	sc->sc_update_chainmask = 1;
	if (is_ht) {
		sc->sc_tx_chainmask = sc->sc_ah->ah_caps.tx_chainmask;
		sc->sc_rx_chainmask = sc->sc_ah->ah_caps.rx_chainmask;
	} else {
		sc->sc_tx_chainmask = 1;
		sc->sc_rx_chainmask = 1;
	}

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: tx chmask: %d, rx chmask: %d\n",
		__func__, sc->sc_tx_chainmask, sc->sc_rx_chainmask);
}

/******************/
/* VAP management */
/******************/

/*
 *  VAP in Listen mode
 *
 *  This routine brings the VAP out of the down state into a "listen" state
 *  where it waits for association requests.  This is used in AP and AdHoc
 *  modes.
*/

int ath_vap_listen(struct ath_softc *sc, int if_id)
{
	struct ath_hal *ah = sc->sc_ah;
	struct ath_vap *avp;
	u32 rfilt = 0;
	DECLARE_MAC_BUF(mac);

	avp = sc->sc_vaps[if_id];
	if (avp == NULL) {
		DPRINTF(sc, ATH_DBG_FATAL, "%s: invalid interface id %u\n",
			__func__, if_id);
		return -EINVAL;
	}

#ifdef CONFIG_SLOW_ANT_DIV
	ath_slow_ant_div_stop(&sc->sc_antdiv);
#endif

	/* update ratectrl about the new state */
	ath_rate_newstate(sc, avp);

	rfilt = ath_calcrxfilter(sc);
	ath9k_hw_setrxfilter(ah, rfilt);

	if (sc->sc_opmode == ATH9K_M_STA || sc->sc_opmode == ATH9K_M_IBSS) {
		memcpy(sc->sc_curbssid, ath_bcast_mac, ETH_ALEN);
		ath9k_hw_write_associd(ah, sc->sc_curbssid, sc->sc_curaid);
	} else
		sc->sc_curaid = 0;

	DPRINTF(sc, ATH_DBG_CONFIG,
		"%s: RX filter 0x%x bssid %s aid 0x%x\n",
		__func__, rfilt, print_mac(mac,
			sc->sc_curbssid), sc->sc_curaid);

	/*
	 * XXXX
	 * Disable BMISS interrupt when we're not associated
	 */
	ath9k_hw_set_interrupts(ah,
		sc->sc_imask & ~(ATH9K_INT_SWBA | ATH9K_INT_BMISS));
	sc->sc_imask &= ~(ATH9K_INT_SWBA | ATH9K_INT_BMISS);
	/* need to reconfigure the beacons when it moves to RUN */
	sc->sc_beacons = 0;

	return 0;
}

int ath_vap_attach(struct ath_softc *sc,
		   int if_id,
		   struct ieee80211_vif *if_data,
		   enum ath9k_opmode opmode)
{
	struct ath_vap *avp;

	if (if_id >= ATH_BCBUF || sc->sc_vaps[if_id] != NULL) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: Invalid interface id = %u\n", __func__, if_id);
		return -EINVAL;
	}

	switch (opmode) {
	case ATH9K_M_STA:
	case ATH9K_M_IBSS:
	case ATH9K_M_MONITOR:
		break;
	case ATH9K_M_HOSTAP:
		/* XXX not right, beacon buffer is allocated on RUN trans */
		if (list_empty(&sc->sc_bbuf))
			return -ENOMEM;
		break;
	default:
		return -EINVAL;
	}

	/* create ath_vap */
	avp = kmalloc(sizeof(struct ath_vap), GFP_KERNEL);
	if (avp == NULL)
		return -ENOMEM;

	memzero(avp, sizeof(struct ath_vap));
	avp->av_if_data = if_data;
	/* Set the VAP opmode */
	avp->av_opmode = opmode;
	avp->av_bslot = -1;
	INIT_LIST_HEAD(&avp->av_mcastq.axq_q);
	INIT_LIST_HEAD(&avp->av_mcastq.axq_acq);
	spin_lock_init(&avp->av_mcastq.axq_lock);

	ath9k_hw_set_tsfadjust(sc->sc_ah, 1);

	sc->sc_vaps[if_id] = avp;
	sc->sc_nvaps++;
	/* Set the device opmode */
	sc->sc_opmode = opmode;

	/* default VAP configuration */
	avp->av_config.av_fixed_rateset = IEEE80211_FIXED_RATE_NONE;
	avp->av_config.av_fixed_retryset = 0x03030303;

	return 0;
}

int ath_vap_detach(struct ath_softc *sc, int if_id)
{
	struct ath_hal *ah = sc->sc_ah;
	struct ath_vap *avp;

	avp = sc->sc_vaps[if_id];
	if (avp == NULL) {
		DPRINTF(sc, ATH_DBG_FATAL, "%s: invalid interface id %u\n",
			__func__, if_id);
		return -EINVAL;
	}

	/*
	 * Quiesce the hardware while we remove the vap.  In
	 * particular we need to reclaim all references to the
	 * vap state by any frames pending on the tx queues.
	 *
	 * XXX can we do this w/o affecting other vap's?
	 */
	ath9k_hw_set_interrupts(ah, 0);	/* disable interrupts */
	ath_draintxq(sc, false);	/* stop xmit side */
	ath_stoprecv(sc);	/* stop recv side */
	ath_flushrecv(sc);	/* flush recv queue */

	/* Reclaim any pending mcast bufs on the vap. */
	ath_tx_draintxq(sc, &avp->av_mcastq, false);

	kfree(avp);
	sc->sc_vaps[if_id] = NULL;
	sc->sc_nvaps--;

	return 0;
}

int ath_vap_config(struct ath_softc *sc,
	int if_id, struct ath_vap_config *if_config)
{
	struct ath_vap *avp;

	if (if_id >= ATH_BCBUF) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: Invalid interface id = %u\n", __func__, if_id);
		return -EINVAL;
	}

	avp = sc->sc_vaps[if_id];
	ASSERT(avp != NULL);

	if (avp)
		memcpy(&avp->av_config, if_config, sizeof(avp->av_config));

	return 0;
}

/********/
/* Core */
/********/

int ath_open(struct ath_softc *sc, struct ath9k_channel *initial_chan)
{
	struct ath_hal *ah = sc->sc_ah;
	int status;
	int error = 0;
	enum ath9k_ht_macmode ht_macmode = ath_cwm_macmode(sc);

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: mode %d\n", __func__, sc->sc_opmode);

	/*
	 * Stop anything previously setup.  This is safe
	 * whether this is the first time through or not.
	 */
	ath_stop(sc);

	/* Initialize chanmask selection */
	sc->sc_tx_chainmask = ah->ah_caps.tx_chainmask;
	sc->sc_rx_chainmask = ah->ah_caps.rx_chainmask;

	/* Reset SERDES registers */
	ath9k_hw_configpcipowersave(ah, 0);

	/*
	 * The basic interface to setting the hardware in a good
	 * state is ``reset''.  On return the hardware is known to
	 * be powered up and with interrupts disabled.  This must
	 * be followed by initialization of the appropriate bits
	 * and then setup of the interrupt mask.
	 */
	sc->sc_curchan = *initial_chan;

	spin_lock_bh(&sc->sc_resetlock);
	if (!ath9k_hw_reset(ah, sc->sc_opmode, &sc->sc_curchan, ht_macmode,
			   sc->sc_tx_chainmask, sc->sc_rx_chainmask,
			   sc->sc_ht_extprotspacing, false, &status)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: unable to reset hardware; hal status %u "
			"(freq %u flags 0x%x)\n", __func__, status,
			sc->sc_curchan.channel, sc->sc_curchan.channelFlags);
		error = -EIO;
		spin_unlock_bh(&sc->sc_resetlock);
		goto done;
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
			"%s: unable to start recv logic\n", __func__);
		error = -EIO;
		goto done;
	}
	/* Setup our intr mask. */
	sc->sc_imask = ATH9K_INT_RX | ATH9K_INT_TX
		| ATH9K_INT_RXEOL | ATH9K_INT_RXORN
		| ATH9K_INT_FATAL | ATH9K_INT_GLOBAL;

	if (ah->ah_caps.hw_caps & ATH9K_HW_CAP_GTT)
		sc->sc_imask |= ATH9K_INT_GTT;

	if (ah->ah_caps.hw_caps & ATH9K_HW_CAP_HT)
		sc->sc_imask |= ATH9K_INT_CST;

	/*
	 * Enable MIB interrupts when there are hardware phy counters.
	 * Note we only do this (at the moment) for station mode.
	 */
	if (ath9k_hw_phycounters(ah) &&
	    ((sc->sc_opmode == ATH9K_M_STA) || (sc->sc_opmode == ATH9K_M_IBSS)))
		sc->sc_imask |= ATH9K_INT_MIB;
	/*
	 * Some hardware processes the TIM IE and fires an
	 * interrupt when the TIM bit is set.  For hardware
	 * that does, if not overridden by configuration,
	 * enable the TIM interrupt when operating as station.
	 */
	if ((ah->ah_caps.hw_caps & ATH9K_HW_CAP_ENHANCEDPM) &&
	    (sc->sc_opmode == ATH9K_M_STA) &&
	    !sc->sc_config.swBeaconProcess)
		sc->sc_imask |= ATH9K_INT_TIM;
	/*
	 *  Don't enable interrupts here as we've not yet built our
	 *  vap and node data structures, which will be needed as soon
	 *  as we start receiving.
	 */
	ath_setcurmode(sc, ath_chan2mode(initial_chan));

	/* XXX: we must make sure h/w is ready and clear invalid flag
	 * before turning on interrupt. */
	sc->sc_invalid = 0;
done:
	return error;
}

int ath_reset(struct ath_softc *sc, bool retry_tx)
{
	struct ath_hal *ah = sc->sc_ah;
	int status;
	int error = 0;
	enum ath9k_ht_macmode ht_macmode = ath_cwm_macmode(sc);

	ath9k_hw_set_interrupts(ah, 0);	/* disable interrupts */
	ath_draintxq(sc, retry_tx);	/* stop xmit */
	ath_stoprecv(sc);		/* stop recv */
	ath_flushrecv(sc);		/* flush recv queue */

	/* Reset chip */
	spin_lock_bh(&sc->sc_resetlock);
	if (!ath9k_hw_reset(ah, sc->sc_opmode, &sc->sc_curchan,
			   ht_macmode,
			   sc->sc_tx_chainmask, sc->sc_rx_chainmask,
			   sc->sc_ht_extprotspacing, false, &status)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: unable to reset hardware; hal status %u\n",
			__func__, status);
		error = -EIO;
	}
	spin_unlock_bh(&sc->sc_resetlock);

	if (ath_startrecv(sc) != 0)	/* restart recv */
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: unable to start recv logic\n", __func__);

	/*
	 * We may be doing a reset in response to a request
	 * that changes the channel so update any state that
	 * might change as a result.
	 */
	ath_setcurmode(sc, ath_chan2mode(&sc->sc_curchan));

	ath_update_txpow(sc);

	if (sc->sc_beacons)
		ath_beacon_config(sc, ATH_IF_ID_ANY);	/* restart beacons */

	ath9k_hw_set_interrupts(ah, sc->sc_imask);

	/* Restart the txq */
	if (retry_tx) {
		int i;
		for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++) {
			if (ATH_TXQ_SETUP(sc, i)) {
				spin_lock_bh(&sc->sc_txq[i].axq_lock);
				ath_txq_schedule(sc, &sc->sc_txq[i]);
				spin_unlock_bh(&sc->sc_txq[i].axq_lock);
			}
		}
	}

	return error;
}

int ath_suspend(struct ath_softc *sc)
{
	struct ath_hal *ah = sc->sc_ah;

	/* No I/O if device has been surprise removed */
	if (sc->sc_invalid)
		return -EIO;

	/* Shut off the interrupt before setting sc->sc_invalid to '1' */
	ath9k_hw_set_interrupts(ah, 0);

	/* XXX: we must make sure h/w will not generate any interrupt
	 * before setting the invalid flag. */
	sc->sc_invalid = 1;

	/* disable HAL and put h/w to sleep */
	ath9k_hw_disable(sc->sc_ah);

	ath9k_hw_configpcipowersave(sc->sc_ah, 1);

	return 0;
}

/* Interrupt handler.  Most of the actual processing is deferred.
 * It's the caller's responsibility to ensure the chip is awake. */

irqreturn_t ath_isr(int irq, void *dev)
{
	struct ath_softc *sc = dev;
	struct ath_hal *ah = sc->sc_ah;
	enum ath9k_int status;
	bool sched = false;

	do {
		if (sc->sc_invalid) {
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

	if (sched) {
		/* turn off every interrupt except SWBA */
		ath9k_hw_set_interrupts(ah, (sc->sc_imask & ATH9K_INT_SWBA));
		tasklet_schedule(&sc->intr_tq);
	}

	return IRQ_HANDLED;
}

/* Deferred interrupt processing  */

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
			/* XXX: fill me in */
			/*
			if (status & ATH9K_INT_RXORN) {
			}
			if (status & ATH9K_INT_RXEOL) {
			}
			*/
			spin_lock_bh(&sc->sc_rxflushlock);
			ath_rx_tasklet(sc, 0);
			spin_unlock_bh(&sc->sc_rxflushlock);
		}
		/* XXX: optimize this */
		if (status & ATH9K_INT_TX)
			ath_tx_tasklet(sc);
		/* XXX: fill me in */
		/*
		if (status & ATH9K_INT_BMISS) {
		}
		if (status & (ATH9K_INT_TIM | ATH9K_INT_DTIMSYNC)) {
			if (status & ATH9K_INT_TIM) {
			}
			if (status & ATH9K_INT_DTIMSYNC) {
			}
		}
		*/
	}

	/* re-enable hardware interrupt */
	ath9k_hw_set_interrupts(sc->sc_ah, sc->sc_imask);
}

int ath_init(u16 devid, struct ath_softc *sc)
{
	struct ath_hal *ah = NULL;
	int status;
	int error = 0, i;
	int csz = 0;
	u32 rd;

	/* XXX: hardware will not be ready until ath_open() being called */
	sc->sc_invalid = 1;

	sc->sc_debug = DBG_DEFAULT;
	DPRINTF(sc, ATH_DBG_CONFIG, "%s: devid 0x%x\n", __func__, devid);

	/* Initialize tasklet */
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

	spin_lock_init(&sc->sc_resetlock);

	ah = ath9k_hw_attach(devid, sc, sc->mem, &status);
	if (ah == NULL) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: unable to attach hardware; HAL status %u\n",
			__func__, status);
		error = -ENXIO;
		goto bad;
	}
	sc->sc_ah = ah;

	/* Get the chipset-specific aggr limit. */
	sc->sc_rtsaggrlimit = ah->ah_caps.rts_aggr_limit;

	/* Get the hardware key cache size. */
	sc->sc_keymax = ah->ah_caps.keycache_size;
	if (sc->sc_keymax > ATH_KEYMAX) {
		DPRINTF(sc, ATH_DBG_KEYCACHE,
			"%s: Warning, using only %u entries in %u key cache\n",
			__func__, ATH_KEYMAX, sc->sc_keymax);
		sc->sc_keymax = ATH_KEYMAX;
	}

	/*
	 * Reset the key cache since some parts do not
	 * reset the contents on initial power up.
	 */
	for (i = 0; i < sc->sc_keymax; i++)
		ath9k_hw_keyreset(ah, (u16) i);
	/*
	 * Mark key cache slots associated with global keys
	 * as in use.  If we knew TKIP was not to be used we
	 * could leave the +32, +64, and +32+64 slots free.
	 * XXX only for splitmic.
	 */
	for (i = 0; i < IEEE80211_WEP_NKID; i++) {
		set_bit(i, sc->sc_keymap);
		set_bit(i + 32, sc->sc_keymap);
		set_bit(i + 64, sc->sc_keymap);
		set_bit(i + 32 + 64, sc->sc_keymap);
	}
	/*
	 * Collect the channel list using the default country
	 * code and including outdoor channels.  The 802.11 layer
	 * is resposible for filtering this list based on settings
	 * like the phy mode.
	 */
	rd = ah->ah_currentRD;

	error = ath_setup_channels(sc);
	if (error)
		goto bad;

	/* default to STA mode */
	sc->sc_opmode = ATH9K_M_MONITOR;

	/* Setup rate tables */

	ath_setup_rates(sc, IEEE80211_BAND_2GHZ);
	ath_setup_rates(sc, IEEE80211_BAND_5GHZ);

	/* NB: setup here so ath_rate_update is happy */
	ath_setcurmode(sc, ATH9K_MODE_11A);

	/*
	 * Allocate hardware transmit queues: one queue for
	 * beacon frames and one data queue for each QoS
	 * priority.  Note that the hal handles reseting
	 * these queues at the needed time.
	 */
	sc->sc_bhalq = ath_beaconq_setup(ah);
	if (sc->sc_bhalq == -1) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: unable to setup a beacon xmit queue\n", __func__);
		error = -EIO;
		goto bad2;
	}
	sc->sc_cabq = ath_txq_setup(sc, ATH9K_TX_QUEUE_CAB, 0);
	if (sc->sc_cabq == NULL) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: unable to setup CAB xmit queue\n", __func__);
		error = -EIO;
		goto bad2;
	}

	sc->sc_config.cabqReadytime = ATH_CABQ_READY_TIME;
	ath_cabq_update(sc);

	for (i = 0; i < ARRAY_SIZE(sc->sc_haltype2q); i++)
		sc->sc_haltype2q[i] = -1;

	/* Setup data queues */
	/* NB: ensure BK queue is the lowest priority h/w queue */
	if (!ath_tx_setup(sc, ATH9K_WME_AC_BK)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: unable to setup xmit queue for BK traffic\n",
			__func__);
		error = -EIO;
		goto bad2;
	}

	if (!ath_tx_setup(sc, ATH9K_WME_AC_BE)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: unable to setup xmit queue for BE traffic\n",
			__func__);
		error = -EIO;
		goto bad2;
	}
	if (!ath_tx_setup(sc, ATH9K_WME_AC_VI)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: unable to setup xmit queue for VI traffic\n",
			__func__);
		error = -EIO;
		goto bad2;
	}
	if (!ath_tx_setup(sc, ATH9K_WME_AC_VO)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: unable to setup xmit queue for VO traffic\n",
			__func__);
		error = -EIO;
		goto bad2;
	}

	sc->sc_rc = ath_rate_attach(ah);
	if (sc->sc_rc == NULL) {
		error = EIO;
		goto bad2;
	}

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
		sc->sc_txaggr = 1;
		sc->sc_rxaggr = 1;
	}

	sc->sc_tx_chainmask = ah->ah_caps.tx_chainmask;
	sc->sc_rx_chainmask = ah->ah_caps.rx_chainmask;

	/* Configuration for rx chain detection */
	sc->sc_rxchaindetect_ref = 0;
	sc->sc_rxchaindetect_thresh5GHz = 35;
	sc->sc_rxchaindetect_thresh2GHz = 35;
	sc->sc_rxchaindetect_delta5GHz = 30;
	sc->sc_rxchaindetect_delta2GHz = 30;

	ath9k_hw_setcapability(ah, ATH9K_CAP_DIVERSITY, 1, true, NULL);
	sc->sc_defant = ath9k_hw_getdefantenna(ah);

	ath9k_hw_getmac(ah, sc->sc_myaddr);
	if (ah->ah_caps.hw_caps & ATH9K_HW_CAP_BSSIDMASK) {
		ath9k_hw_getbssidmask(ah, sc->sc_bssidmask);
		ATH_SET_VAP_BSSID_MASK(sc->sc_bssidmask);
		ath9k_hw_setbssidmask(ah, sc->sc_bssidmask);
	}
	sc->sc_slottime = ATH9K_SLOT_TIME_9;	/* default to short slot time */

	/* initialize beacon slots */
	for (i = 0; i < ARRAY_SIZE(sc->sc_bslot); i++)
		sc->sc_bslot[i] = ATH_IF_ID_ANY;

	/* save MISC configurations */
	sc->sc_config.swBeaconProcess = 1;

#ifdef CONFIG_SLOW_ANT_DIV
	/* range is 40 - 255, we use something in the middle */
	ath_slow_ant_div_init(&sc->sc_antdiv, sc, 0x127);
#endif

	return 0;
bad2:
	/* cleanup tx queues */
	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++)
		if (ATH_TXQ_SETUP(sc, i))
			ath_tx_cleanupq(sc, &sc->sc_txq[i]);
bad:
	if (ah)
		ath9k_hw_detach(ah);
	return error;
}

void ath_deinit(struct ath_softc *sc)
{
	struct ath_hal *ah = sc->sc_ah;
	int i;

	DPRINTF(sc, ATH_DBG_CONFIG, "%s\n", __func__);

	ath_stop(sc);
	if (!sc->sc_invalid)
		ath9k_hw_setpower(sc->sc_ah, ATH9K_PM_AWAKE);
	ath_rate_detach(sc->sc_rc);
	/* cleanup tx queues */
	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++)
		if (ATH_TXQ_SETUP(sc, i))
			ath_tx_cleanupq(sc, &sc->sc_txq[i]);
	ath9k_hw_detach(ah);
}

/*******************/
/* Node Management */
/*******************/

struct ath_node *ath_node_attach(struct ath_softc *sc, u8 *addr, int if_id)
{
	struct ath_vap *avp;
	struct ath_node *an;
	DECLARE_MAC_BUF(mac);

	avp = sc->sc_vaps[if_id];
	ASSERT(avp != NULL);

	/* mac80211 sta_notify callback is from an IRQ context, so no sleep */
	an = kmalloc(sizeof(struct ath_node), GFP_ATOMIC);
	if (an == NULL)
		return NULL;
	memzero(an, sizeof(*an));

	an->an_sc = sc;
	memcpy(an->an_addr, addr, ETH_ALEN);
	atomic_set(&an->an_refcnt, 1);

	/* set up per-node tx/rx state */
	ath_tx_node_init(sc, an);
	ath_rx_node_init(sc, an);

	ath_chainmask_sel_init(sc, an);
	ath_chainmask_sel_timerstart(&an->an_chainmask_sel);
	list_add(&an->list, &sc->node_list);

	return an;
}

void ath_node_detach(struct ath_softc *sc, struct ath_node *an, bool bh_flag)
{
	unsigned long flags;

	DECLARE_MAC_BUF(mac);

	ath_chainmask_sel_timerstop(&an->an_chainmask_sel);
	an->an_flags |= ATH_NODE_CLEAN;
	ath_tx_node_cleanup(sc, an, bh_flag);
	ath_rx_node_cleanup(sc, an);

	ath_tx_node_free(sc, an);
	ath_rx_node_free(sc, an);

	spin_lock_irqsave(&sc->node_lock, flags);

	list_del(&an->list);

	spin_unlock_irqrestore(&sc->node_lock, flags);

	kfree(an);
}

/* Finds a node and increases the refcnt if found */

struct ath_node *ath_node_get(struct ath_softc *sc, u8 *addr)
{
	struct ath_node *an = NULL, *an_found = NULL;

	if (list_empty(&sc->node_list)) /* FIXME */
		goto out;
	list_for_each_entry(an, &sc->node_list, list) {
		if (!compare_ether_addr(an->an_addr, addr)) {
			atomic_inc(&an->an_refcnt);
			an_found = an;
			break;
		}
	}
out:
	return an_found;
}

/* Decrements the refcnt and if it drops to zero, detach the node */

void ath_node_put(struct ath_softc *sc, struct ath_node *an, bool bh_flag)
{
	if (atomic_dec_and_test(&an->an_refcnt))
		ath_node_detach(sc, an, bh_flag);
}

/* Finds a node, doesn't increment refcnt. Caller must hold sc->node_lock */
struct ath_node *ath_node_find(struct ath_softc *sc, u8 *addr)
{
	struct ath_node *an = NULL, *an_found = NULL;

	if (list_empty(&sc->node_list))
		return NULL;

	list_for_each_entry(an, &sc->node_list, list)
		if (!compare_ether_addr(an->an_addr, addr)) {
			an_found = an;
			break;
		}

	return an_found;
}

/*
 * Set up New Node
 *
 * Setup driver-specific state for a newly associated node.  This routine
 * really only applies if compression or XR are enabled, there is no code
 * covering any other cases.
*/

void ath_newassoc(struct ath_softc *sc,
	struct ath_node *an, int isnew, int isuapsd)
{
	int tidno;

	/* if station reassociates, tear down the aggregation state. */
	if (!isnew) {
		for (tidno = 0; tidno < WME_NUM_TID; tidno++) {
			if (sc->sc_txaggr)
				ath_tx_aggr_teardown(sc, an, tidno);
			if (sc->sc_rxaggr)
				ath_rx_aggr_teardown(sc, an, tidno);
		}
	}
	an->an_flags = 0;
}

/**************/
/* Encryption */
/**************/

void ath_key_reset(struct ath_softc *sc, u16 keyix, int freeslot)
{
	ath9k_hw_keyreset(sc->sc_ah, keyix);
	if (freeslot)
		clear_bit(keyix, sc->sc_keymap);
}

int ath_keyset(struct ath_softc *sc,
	       u16 keyix,
	       struct ath9k_keyval *hk,
	       const u8 mac[ETH_ALEN])
{
	bool status;

	status = ath9k_hw_set_keycache_entry(sc->sc_ah,
		keyix, hk, mac, false);

	return status != false;
}

/***********************/
/* TX Power/Regulatory */
/***********************/

/*
 *  Set Transmit power in HAL
 *
 *  This routine makes the actual HAL calls to set the new transmit power
 *  limit.
*/

void ath_update_txpow(struct ath_softc *sc)
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

/* Return the current country and domain information */
void ath_get_currentCountry(struct ath_softc *sc,
	struct ath9k_country_entry *ctry)
{
	ath9k_regd_get_current_country(sc->sc_ah, ctry);

	/* If HAL not specific yet, since it is band dependent,
	 * use the one we passed in. */
	if (ctry->countryCode == CTRY_DEFAULT) {
		ctry->iso[0] = 0;
		ctry->iso[1] = 0;
	} else if (ctry->iso[0] && ctry->iso[1]) {
		if (!ctry->iso[2]) {
			if (ath_outdoor)
				ctry->iso[2] = 'O';
			else
				ctry->iso[2] = 'I';
		}
	}
}

/**************************/
/* Slow Antenna Diversity */
/**************************/

void ath_slow_ant_div_init(struct ath_antdiv *antdiv,
			   struct ath_softc *sc,
			   int32_t rssitrig)
{
	int trig;

	/* antdivf_rssitrig can range from 40 - 0xff */
	trig = (rssitrig > 0xff) ? 0xff : rssitrig;
	trig = (rssitrig < 40) ? 40 : rssitrig;

	antdiv->antdiv_sc = sc;
	antdiv->antdivf_rssitrig = trig;
}

void ath_slow_ant_div_start(struct ath_antdiv *antdiv,
			    u8 num_antcfg,
			    const u8 *bssid)
{
	antdiv->antdiv_num_antcfg =
		num_antcfg < ATH_ANT_DIV_MAX_CFG ?
		num_antcfg : ATH_ANT_DIV_MAX_CFG;
	antdiv->antdiv_state = ATH_ANT_DIV_IDLE;
	antdiv->antdiv_curcfg = 0;
	antdiv->antdiv_bestcfg = 0;
	antdiv->antdiv_laststatetsf = 0;

	memcpy(antdiv->antdiv_bssid, bssid, sizeof(antdiv->antdiv_bssid));

	antdiv->antdiv_start = 1;
}

void ath_slow_ant_div_stop(struct ath_antdiv *antdiv)
{
	antdiv->antdiv_start = 0;
}

static int32_t ath_find_max_val(int32_t *val,
	u8 num_val, u8 *max_index)
{
	u32 MaxVal = *val++;
	u32 cur_index = 0;

	*max_index = 0;
	while (++cur_index < num_val) {
		if (*val > MaxVal) {
			MaxVal = *val;
			*max_index = cur_index;
		}

		val++;
	}

	return MaxVal;
}

void ath_slow_ant_div(struct ath_antdiv *antdiv,
		      struct ieee80211_hdr *hdr,
		      struct ath_rx_status *rx_stats)
{
	struct ath_softc *sc = antdiv->antdiv_sc;
	struct ath_hal *ah = sc->sc_ah;
	u64 curtsf = 0;
	u8 bestcfg, curcfg = antdiv->antdiv_curcfg;
	__le16 fc = hdr->frame_control;

	if (antdiv->antdiv_start && ieee80211_is_beacon(fc)
	    && !compare_ether_addr(hdr->addr3, antdiv->antdiv_bssid)) {
		antdiv->antdiv_lastbrssi[curcfg] = rx_stats->rs_rssi;
		antdiv->antdiv_lastbtsf[curcfg] = ath9k_hw_gettsf64(sc->sc_ah);
		curtsf = antdiv->antdiv_lastbtsf[curcfg];
	} else {
		return;
	}

	switch (antdiv->antdiv_state) {
	case ATH_ANT_DIV_IDLE:
		if ((antdiv->antdiv_lastbrssi[curcfg] <
		     antdiv->antdivf_rssitrig)
		    && ((curtsf - antdiv->antdiv_laststatetsf) >
			ATH_ANT_DIV_MIN_IDLE_US)) {

			curcfg++;
			if (curcfg == antdiv->antdiv_num_antcfg)
				curcfg = 0;

			if (!ath9k_hw_select_antconfig(ah, curcfg)) {
				antdiv->antdiv_bestcfg = antdiv->antdiv_curcfg;
				antdiv->antdiv_curcfg = curcfg;
				antdiv->antdiv_laststatetsf = curtsf;
				antdiv->antdiv_state = ATH_ANT_DIV_SCAN;
			}
		}
		break;

	case ATH_ANT_DIV_SCAN:
		if ((curtsf - antdiv->antdiv_laststatetsf) <
		    ATH_ANT_DIV_MIN_SCAN_US)
			break;

		curcfg++;
		if (curcfg == antdiv->antdiv_num_antcfg)
			curcfg = 0;

		if (curcfg == antdiv->antdiv_bestcfg) {
			ath_find_max_val(antdiv->antdiv_lastbrssi,
				   antdiv->antdiv_num_antcfg, &bestcfg);
			if (!ath9k_hw_select_antconfig(ah, bestcfg)) {
				antdiv->antdiv_bestcfg = bestcfg;
				antdiv->antdiv_curcfg = bestcfg;
				antdiv->antdiv_laststatetsf = curtsf;
				antdiv->antdiv_state = ATH_ANT_DIV_IDLE;
			}
		} else {
			if (!ath9k_hw_select_antconfig(ah, curcfg)) {
				antdiv->antdiv_curcfg = curcfg;
				antdiv->antdiv_laststatetsf = curtsf;
				antdiv->antdiv_state = ATH_ANT_DIV_SCAN;
			}
		}

		break;
	}
}

/***********************/
/* Descriptor Handling */
/***********************/

/*
 *  Set up DMA descriptors
 *
 *  This function will allocate both the DMA descriptor structure, and the
 *  buffers it contains.  These are used to contain the descriptors used
 *  by the system.
*/

int ath_descdma_setup(struct ath_softc *sc,
		      struct ath_descdma *dd,
		      struct list_head *head,
		      const char *name,
		      int nbuf,
		      int ndesc)
{
#define	DS2PHYS(_dd, _ds)						\
	((_dd)->dd_desc_paddr + ((caddr_t)(_ds) - (caddr_t)(_dd)->dd_desc))
#define ATH_DESC_4KB_BOUND_CHECK(_daddr) ((((_daddr) & 0xFFF) > 0xF7F) ? 1 : 0)
#define ATH_DESC_4KB_BOUND_NUM_SKIPPED(_len) ((_len) / 4096)

	struct ath_desc *ds;
	struct ath_buf *bf;
	int i, bsize, error;

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: %s DMA: %u buffers %u desc/buf\n",
		__func__, name, nbuf, ndesc);

	/* ath_desc must be a multiple of DWORDs */
	if ((sizeof(struct ath_desc) % 4) != 0) {
		DPRINTF(sc, ATH_DBG_FATAL, "%s: ath_desc not DWORD aligned\n",
			__func__);
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
	DPRINTF(sc, ATH_DBG_CONFIG, "%s: %s DMA map: %p (%u) -> %llx (%u)\n",
		__func__, dd->dd_name, ds, (u32) dd->dd_desc_len,
		ito64(dd->dd_desc_paddr), /*XXX*/(u32) dd->dd_desc_len);

	/* allocate buffers */
	bsize = sizeof(struct ath_buf) * nbuf;
	bf = kmalloc(bsize, GFP_KERNEL);
	if (bf == NULL) {
		error = -ENOMEM;
		goto fail2;
	}
	memzero(bf, bsize);
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
	memzero(dd, sizeof(*dd));
	return error;
#undef ATH_DESC_4KB_BOUND_CHECK
#undef ATH_DESC_4KB_BOUND_NUM_SKIPPED
#undef DS2PHYS
}

/*
 *  Cleanup DMA descriptors
 *
 *  This function will free the DMA block that was allocated for the descriptor
 *  pool.  Since this was allocated as one "chunk", it is freed in the same
 *  manner.
*/

void ath_descdma_cleanup(struct ath_softc *sc,
			 struct ath_descdma *dd,
			 struct list_head *head)
{
	/* Free memory associated with descriptors */
	pci_free_consistent(sc->pdev,
		dd->dd_desc_len, dd->dd_desc, dd->dd_desc_paddr);

	INIT_LIST_HEAD(head);
	kfree(dd->dd_bufptr);
	memzero(dd, sizeof(*dd));
}

/*************/
/* Utilities */
/*************/

int ath_get_hal_qnum(u16 queue, struct ath_softc *sc)
{
	int qnum;

	switch (queue) {
	case 0:
		qnum = sc->sc_haltype2q[ATH9K_WME_AC_VO];
		break;
	case 1:
		qnum = sc->sc_haltype2q[ATH9K_WME_AC_VI];
		break;
	case 2:
		qnum = sc->sc_haltype2q[ATH9K_WME_AC_BE];
		break;
	case 3:
		qnum = sc->sc_haltype2q[ATH9K_WME_AC_BK];
		break;
	default:
		qnum = sc->sc_haltype2q[ATH9K_WME_AC_BE];
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


/*
 *  Expand time stamp to TSF
 *
 *  Extend 15-bit time stamp from rx descriptor to
 *  a full 64-bit TSF using the current h/w TSF.
*/

u64 ath_extend_tsf(struct ath_softc *sc, u32 rstamp)
{
	u64 tsf;

	tsf = ath9k_hw_gettsf64(sc->sc_ah);
	if ((tsf & 0x7fff) < rstamp)
		tsf -= 0x8000;
	return (tsf & ~0x7fff) | rstamp;
}

/*
 *  Set Default Antenna
 *
 *  Call into the HAL to set the default antenna to use.  Not really valid for
 *  MIMO technology.
*/

void ath_setdefantenna(void *context, u32 antenna)
{
	struct ath_softc *sc = (struct ath_softc *)context;
	struct ath_hal *ah = sc->sc_ah;

	/* XXX block beacon interrupts */
	ath9k_hw_setantenna(ah, antenna);
	sc->sc_defant = antenna;
	sc->sc_rxotherant = 0;
}

/*
 * Set Slot Time
 *
 * This will wake up the chip if required, and set the slot time for the
 * frame (maximum transmit time).  Slot time is assumed to be already set
 * in the ATH object member sc_slottime
*/

void ath_setslottime(struct ath_softc *sc)
{
	ath9k_hw_setslottime(sc->sc_ah, sc->sc_slottime);
	sc->sc_updateslot = OK;
}
