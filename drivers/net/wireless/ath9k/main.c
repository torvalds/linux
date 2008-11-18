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

/* mac80211 and PCI callbacks */

#include <linux/nl80211.h>
#include "core.h"
#include "reg.h"

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
	{ 0 }
};

static void ath_detach(struct ath_softc *sc);

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
				     struct ieee80211_channel *chan)
{
	u32 chanmode = 0;
	u8 ext_chan_offset = sc->sc_ht_info.ext_chan_offset;
	enum ath9k_ht_macmode tx_chan_width = sc->sc_ht_info.tx_chan_width;

	switch (chan->band) {
	case IEEE80211_BAND_2GHZ:
		if ((ext_chan_offset == IEEE80211_HT_PARAM_CHA_SEC_NONE) &&
		    (tx_chan_width == ATH9K_HT_MACMODE_20))
			chanmode = CHANNEL_G_HT20;
		if ((ext_chan_offset == IEEE80211_HT_PARAM_CHA_SEC_ABOVE) &&
		    (tx_chan_width == ATH9K_HT_MACMODE_2040))
			chanmode = CHANNEL_G_HT40PLUS;
		if ((ext_chan_offset == IEEE80211_HT_PARAM_CHA_SEC_BELOW) &&
		    (tx_chan_width == ATH9K_HT_MACMODE_2040))
			chanmode = CHANNEL_G_HT40MINUS;
		break;
	case IEEE80211_BAND_5GHZ:
		if ((ext_chan_offset == IEEE80211_HT_PARAM_CHA_SEC_NONE) &&
		    (tx_chan_width == ATH9K_HT_MACMODE_20))
			chanmode = CHANNEL_A_HT20;
		if ((ext_chan_offset == IEEE80211_HT_PARAM_CHA_SEC_ABOVE) &&
		    (tx_chan_width == ATH9K_HT_MACMODE_2040))
			chanmode = CHANNEL_A_HT40PLUS;
		if ((ext_chan_offset == IEEE80211_HT_PARAM_CHA_SEC_BELOW) &&
		    (tx_chan_width == ATH9K_HT_MACMODE_2040))
			chanmode = CHANNEL_A_HT40MINUS;
		break;
	default:
		break;
	}

	return chanmode;
}


static int ath_setkey_tkip(struct ath_softc *sc,
			   struct ieee80211_key_conf *key,
			   struct ath9k_keyval *hk,
			   const u8 *addr)
{
	u8 *key_rxmic = NULL;
	u8 *key_txmic = NULL;

	key_txmic = key->key + NL80211_TKIP_DATA_OFFSET_TX_MIC_KEY;
	key_rxmic = key->key + NL80211_TKIP_DATA_OFFSET_RX_MIC_KEY;

	if (addr == NULL) {
		/* Group key installation */
		memcpy(hk->kv_mic,  key_rxmic, sizeof(hk->kv_mic));
		return ath_keyset(sc, key->keyidx, hk, addr);
	}
	if (!sc->sc_splitmic) {
		/*
		 * data key goes at first index,
		 * the hal handles the MIC keys at index+64.
		 */
		memcpy(hk->kv_mic, key_rxmic, sizeof(hk->kv_mic));
		memcpy(hk->kv_txmic, key_txmic, sizeof(hk->kv_txmic));
		return ath_keyset(sc, key->keyidx, hk, addr);
	}
	/*
	 * TX key goes at first index, RX key at +32.
	 * The hal handles the MIC keys at index+64.
	 */
	memcpy(hk->kv_mic, key_txmic, sizeof(hk->kv_mic));
	if (!ath_keyset(sc, key->keyidx, hk, NULL)) {
		/* Txmic entry failed. No need to proceed further */
		DPRINTF(sc, ATH_DBG_KEYCACHE,
			"%s Setting TX MIC Key Failed\n", __func__);
		return 0;
	}

	memcpy(hk->kv_mic, key_rxmic, sizeof(hk->kv_mic));
	/* XXX delete tx key on failure? */
	return ath_keyset(sc, key->keyidx+32, hk, addr);
}

static int ath_key_config(struct ath_softc *sc,
			  const u8 *addr,
			  struct ieee80211_key_conf *key)
{
	struct ieee80211_vif *vif;
	struct ath9k_keyval hk;
	const u8 *mac = NULL;
	int ret = 0;
	enum nl80211_iftype opmode;

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

	hk.kv_len  = key->keylen;
	memcpy(hk.kv_val, key->key, key->keylen);

	if (!sc->sc_vaps[0])
		return -EIO;

	vif = sc->sc_vaps[0];
	opmode = vif->type;

	/*
	 *  Strategy:
	 *   For _M_STA mc tx, we will not setup a key at all since we never
	 *   tx mc.
	 *   _M_STA mc rx, we will use the keyID.
	 *   for _M_IBSS mc tx, we will use the keyID, and no macaddr.
	 *   for _M_IBSS mc rx, we will alloc a slot and plumb the mac of the
	 *   peer node. BUT we will plumb a cleartext key so that we can do
	 *   perSta default key table lookup in software.
	 */
	if (is_broadcast_ether_addr(addr)) {
		switch (opmode) {
		case NL80211_IFTYPE_STATION:
			/* default key:  could be group WPA key
			 * or could be static WEP key */
			mac = NULL;
			break;
		case NL80211_IFTYPE_ADHOC:
			break;
		case NL80211_IFTYPE_AP:
			break;
		default:
			ASSERT(0);
			break;
		}
	} else {
		mac = addr;
	}

	if (key->alg == ALG_TKIP)
		ret = ath_setkey_tkip(sc, key, &hk, mac);
	else
		ret = ath_keyset(sc, key->keyidx, &hk, mac);

	if (!ret)
		return -EIO;

	return 0;
}

static void ath_key_delete(struct ath_softc *sc, struct ieee80211_key_conf *key)
{
	int freeslot;

	freeslot = (key->keyidx >= 4) ? 1 : 0;
	ath_key_reset(sc, key->keyidx, freeslot);
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

static void ath9k_ht_conf(struct ath_softc *sc,
			  struct ieee80211_bss_conf *bss_conf)
{
	struct ath_ht_info *ht_info = &sc->sc_ht_info;

	if (sc->hw->conf.ht.enabled) {
		ht_info->ext_chan_offset = bss_conf->ht.secondary_channel_offset;

		if (bss_conf->ht.width_40_ok)
			ht_info->tx_chan_width = ATH9K_HT_MACMODE_2040;
		else
			ht_info->tx_chan_width = ATH9K_HT_MACMODE_20;

		ath9k_hw_set11nmac2040(sc->sc_ah, ht_info->tx_chan_width);
	}
}

static void ath9k_bss_assoc_info(struct ath_softc *sc,
				 struct ieee80211_vif *vif,
				 struct ieee80211_bss_conf *bss_conf)
{
	struct ieee80211_hw *hw = sc->hw;
	struct ieee80211_channel *curchan = hw->conf.channel;
	struct ath_vap *avp = (void *)vif->drv_priv;
	int pos;

	if (bss_conf->assoc) {
		DPRINTF(sc, ATH_DBG_CONFIG, "%s: Bss Info ASSOC %d\n",
			__func__,
			bss_conf->aid);

		/* New association, store aid */
		if (avp->av_opmode == ATH9K_M_STA) {
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

		/* Update chainmask */
		ath_update_chainmask(sc, hw->conf.ht.enabled);

		DPRINTF(sc, ATH_DBG_CONFIG,
			"%s: bssid %pM aid 0x%x\n",
			__func__,
			sc->sc_curbssid, sc->sc_curaid);

		DPRINTF(sc, ATH_DBG_CONFIG, "%s: Set channel: %d MHz\n",
			__func__,
			curchan->center_freq);

		pos = ath_get_channel(sc, curchan);
		if (pos == -1) {
			DPRINTF(sc, ATH_DBG_FATAL,
				"%s: Invalid channel\n", __func__);
			return;
		}

		if (hw->conf.ht.enabled)
			sc->sc_ah->ah_channels[pos].chanmode =
				ath_get_extchanmode(sc, curchan);
		else
			sc->sc_ah->ah_channels[pos].chanmode =
				(curchan->band == IEEE80211_BAND_2GHZ) ?
				CHANNEL_G : CHANNEL_A;

		/* set h/w channel */
		if (ath_set_channel(sc, &sc->sc_ah->ah_channels[pos]) < 0)
			DPRINTF(sc, ATH_DBG_FATAL,
				"%s: Unable to set channel\n",
				__func__);
		/* Start ANI */
		mod_timer(&sc->sc_ani.timer,
			jiffies + msecs_to_jiffies(ATH_ANI_POLLINTERVAL));

	} else {
		DPRINTF(sc, ATH_DBG_CONFIG,
		"%s: Bss Info DISSOC\n", __func__);
		sc->sc_curaid = 0;
	}
}

void ath_get_beaconconfig(struct ath_softc *sc,
			  int if_id,
			  struct ath_beacon_config *conf)
{
	struct ieee80211_hw *hw = sc->hw;

	/* fill in beacon config data */

	conf->beacon_interval = hw->conf.beacon_int;
	conf->listen_interval = 100;
	conf->dtim_count = 1;
	conf->bmiss_timeout = ATH_DEFAULT_BMISS_LIMIT * conf->listen_interval;
}

void ath_tx_complete(struct ath_softc *sc, struct sk_buff *skb,
		     struct ath_xmit_status *tx_status)
{
	struct ieee80211_hw *hw = sc->hw;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);

	DPRINTF(sc, ATH_DBG_XMIT,
		"%s: TX complete: skb: %p\n", __func__, skb);

	ieee80211_tx_info_clear_status(tx_info);
	if (tx_info->flags & IEEE80211_TX_CTL_NO_ACK ||
		tx_info->flags & IEEE80211_TX_STAT_TX_FILTERED) {
		/* free driver's private data area of tx_info, XXX: HACK! */
		if (tx_info->control.vif != NULL)
			kfree(tx_info->control.vif);
			tx_info->control.vif = NULL;
	}

	if (tx_status->flags & ATH_TX_BAR) {
		tx_info->flags |= IEEE80211_TX_STAT_AMPDU_NO_BACK;
		tx_status->flags &= ~ATH_TX_BAR;
	}

	if (!(tx_status->flags & (ATH_TX_ERROR | ATH_TX_XRETRY))) {
		/* Frame was ACKed */
		tx_info->flags |= IEEE80211_TX_STAT_ACK;
	}

	tx_info->status.rates[0].count = tx_status->retries + 1;

	ieee80211_tx_status(hw, skb);
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
			    sc->sc_ht_info.tx_chan_width,
			    sc->sc_tx_chainmask,
			    sc->sc_rx_chainmask,
			    sc->sc_ht_extprotspacing,
			    false, &status)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: unable to reset channel %u (%uMhz) "
			"flags 0x%x hal status %u\n", __func__,
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
			"%s: unable to restart recv logic\n", __func__);
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
			    sc->sc_ht_info.tx_chan_width,
			    sc->sc_tx_chainmask,
			    sc->sc_rx_chainmask,
			    sc->sc_ht_extprotspacing,
			    false, &status)) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: unable to reset channel %u (%uMhz) "
			"flags 0x%x hal status %u\n", __func__,
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
					"radio as it is disabled by h/w \n");
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

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: Detach ATH hw\n", __func__);

#if defined(CONFIG_RFKILL) || defined(CONFIG_RFKILL_MODULE)
	ath_deinit_rfkill(sc);
#endif
	ath_deinit_leds(sc);

	ieee80211_unregister_hw(hw);

	ath_rate_control_unregister();

	ath_rx_cleanup(sc);
	ath_tx_cleanup(sc);

	tasklet_kill(&sc->intr_tq);
	tasklet_kill(&sc->bcon_tasklet);

	if (!(sc->sc_flags & SC_OP_INVALID))
		ath9k_hw_setpower(sc->sc_ah, ATH9K_PM_AWAKE);

	/* cleanup tx queues */
	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++)
		if (ATH_TXQ_SETUP(sc, i))
			ath_tx_cleanupq(sc, &sc->sc_txq[i]);

	ath9k_hw_detach(sc->sc_ah);
}

static int ath_attach(u16 devid, struct ath_softc *sc)
{
	struct ieee80211_hw *hw = sc->hw;
	int error = 0;

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: Attach ATH hw\n", __func__);

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
	hw->sta_data_size = sizeof(struct ath_node);
	hw->vif_data_size = sizeof(struct ath_vap);

	/* Register rate control */
	hw->rate_control_algorithm = "ath9k_rate_control";
	error = ath_rate_control_register();
	if (error != 0) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: Unable to register rate control "
			"algorithm:%d\n", __func__, error);
		ath_rate_control_unregister();
		goto bad;
	}

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
		goto detach;

	error = ath_rx_init(sc, ATH_RXBUF);
	if (error != 0)
		goto detach;

#if defined(CONFIG_RFKILL) || defined(CONFIG_RFKILL_MODULE)
	/* Initialze h/w Rfkill */
	if (sc->sc_ah->ah_caps.hw_caps & ATH9K_HW_CAP_RFSILENT)
		INIT_DELAYED_WORK(&sc->rf_kill.rfkill_poll, ath_rfkill_poll);

	/* Initialize s/w rfkill */
	if (ath_init_sw_rfkill(sc))
		goto detach;
#endif

	error = ieee80211_register_hw(hw);
	if (error != 0) {
		ath_rate_control_unregister();
		goto bad;
	}

	/* Initialize LED control */
	ath_init_leds(sc);

	return 0;
detach:
	ath_detach(sc);
bad:
	return error;
}

static int ath9k_start(struct ieee80211_hw *hw)
{
	struct ath_softc *sc = hw->priv;
	struct ieee80211_channel *curchan = hw->conf.channel;
	int error = 0, pos;

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: Starting driver with "
		"initial channel: %d MHz\n", __func__, curchan->center_freq);

	memset(&sc->sc_ht_info, 0, sizeof(struct ath_ht_info));

	/* setup initial channel */

	pos = ath_get_channel(sc, curchan);
	if (pos == -1) {
		DPRINTF(sc, ATH_DBG_FATAL, "%s: Invalid channel\n", __func__);
		error = -EINVAL;
		goto exit;
	}

	sc->sc_ah->ah_channels[pos].chanmode =
		(curchan->band == IEEE80211_BAND_2GHZ) ? CHANNEL_G : CHANNEL_A;

	error = ath_open(sc, &sc->sc_ah->ah_channels[pos]);
	if (error) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: Unable to complete ath_open\n", __func__);
		goto exit;
	}

#if defined(CONFIG_RFKILL) || defined(CONFIG_RFKILL_MODULE)
	error = ath_start_rfkill_poll(sc);
#endif

exit:
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
			sc->seq_no += 0x10;
		hdr->seq_ctrl &= cpu_to_le16(IEEE80211_SCTL_FRAG);
		hdr->seq_ctrl |= cpu_to_le16(sc->seq_no);
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

	DPRINTF(sc, ATH_DBG_XMIT, "%s: transmitting packet, skb: %p\n",
		__func__,
		skb);

	if (ath_tx_start(sc, skb, &txctl) != 0) {
		DPRINTF(sc, ATH_DBG_XMIT, "%s: TX failed\n", __func__);
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
		DPRINTF(sc, ATH_DBG_ANY, "%s: Device not present\n", __func__);
		return;
	}

	ath_stop(sc);

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: Driver halt\n", __func__);
}

static int ath9k_add_interface(struct ieee80211_hw *hw,
			       struct ieee80211_if_init_conf *conf)
{
	struct ath_softc *sc = hw->priv;
	struct ath_vap *avp = (void *)conf->vif->drv_priv;
	int ic_opmode = 0;

	/* Support only vap for now */

	if (sc->sc_nvaps)
		return -ENOBUFS;

	switch (conf->type) {
	case NL80211_IFTYPE_STATION:
		ic_opmode = ATH9K_M_STA;
		break;
	case NL80211_IFTYPE_ADHOC:
		ic_opmode = ATH9K_M_IBSS;
		break;
	case NL80211_IFTYPE_AP:
		ic_opmode = ATH9K_M_HOSTAP;
		break;
	default:
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: Interface type %d not yet supported\n",
			__func__, conf->type);
		return -EOPNOTSUPP;
	}

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: Attach a VAP of type: %d\n",
		__func__,
		ic_opmode);

	/* Set the VAP opmode */
	avp->av_opmode = ic_opmode;
	avp->av_bslot = -1;

	if (ic_opmode == ATH9K_M_HOSTAP)
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

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: Detach VAP\n", __func__);

#ifdef CONFIG_SLOW_ANT_DIV
	ath_slow_ant_div_stop(&sc->sc_antdiv);
#endif
	/* Stop ANI */
	del_timer_sync(&sc->sc_ani.timer);

	/* Reclaim beacon resources */
	if (sc->sc_ah->ah_opmode == ATH9K_M_HOSTAP ||
	    sc->sc_ah->ah_opmode == ATH9K_M_IBSS) {
		ath9k_hw_stoptxdma(sc->sc_ah, sc->sc_bhalq);
		ath_beacon_return(sc, avp);
	}

	sc->sc_flags &= ~SC_OP_BEACONS;

	sc->sc_vaps[0] = NULL;
	sc->sc_nvaps--;
}

static int ath9k_config(struct ieee80211_hw *hw, u32 changed)
{
	struct ath_softc *sc = hw->priv;
	struct ieee80211_channel *curchan = hw->conf.channel;
	struct ieee80211_conf *conf = &hw->conf;
	int pos;

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: Set channel: %d MHz\n",
		__func__,
		curchan->center_freq);

	/* Update chainmask */
	ath_update_chainmask(sc, conf->ht.enabled);

	pos = ath_get_channel(sc, curchan);
	if (pos == -1) {
		DPRINTF(sc, ATH_DBG_FATAL, "%s: Invalid channel\n", __func__);
		return -EINVAL;
	}

	sc->sc_ah->ah_channels[pos].chanmode =
		(curchan->band == IEEE80211_BAND_2GHZ) ?
		CHANNEL_G : CHANNEL_A;

	if (sc->sc_curaid && hw->conf.ht.enabled)
		sc->sc_ah->ah_channels[pos].chanmode =
			ath_get_extchanmode(sc, curchan);

	if (changed & IEEE80211_CONF_CHANGE_POWER)
		sc->sc_config.txpowlimit = 2 * conf->power_level;

	/* set h/w channel */
	if (ath_set_channel(sc, &sc->sc_ah->ah_channels[pos]) < 0)
		DPRINTF(sc, ATH_DBG_FATAL, "%s: Unable to set channel\n",
			__func__);

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
	    ah->ah_opmode != ATH9K_M_HOSTAP) {
		ah->ah_opmode = ATH9K_M_HOSTAP;
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
				"%s: RX filter 0x%x bssid %pM aid 0x%x\n",
				__func__, rfilt,
				sc->sc_curbssid, sc->sc_curaid);

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
		ath9k_hw_stoptxdma(sc->sc_ah, sc->sc_bhalq);

		error = ath_beacon_alloc(sc, 0);
		if (error != 0)
			return error;

		ath_beacon_sync(sc, 0);
	}

	/* Check for WLAN_CAPABILITY_PRIVACY ? */
	if ((avp->av_opmode != ATH9K_M_STA)) {
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

	sc->rx_filter = *total_flags;
	rfilt = ath_calcrxfilter(sc);
	ath9k_hw_setrxfilter(sc->sc_ah, rfilt);

	if (changed_flags & FIF_BCN_PRBRESP_PROMISC) {
		if (*total_flags & FIF_BCN_PRBRESP_PROMISC)
			ath9k_hw_write_associd(sc->sc_ah, ath_bcast_mac, 0);
	}

	DPRINTF(sc, ATH_DBG_CONFIG, "%s: Set HW RX filter: 0x%x\n",
		__func__, sc->rx_filter);
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
		"%s: Configure tx [queue/halq] [%d/%d],  "
		"aifs: %d, cw_min: %d, cw_max: %d, txop: %d\n",
		__func__,
		queue,
		qnum,
		params->aifs,
		params->cw_min,
		params->cw_max,
		params->txop);

	ret = ath_txq_update(sc, qnum, &qi);
	if (ret)
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: TXQ Update failed\n", __func__);

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

	DPRINTF(sc, ATH_DBG_KEYCACHE, " %s: Set HW Key\n", __func__);

	switch (cmd) {
	case SET_KEY:
		ret = ath_key_config(sc, addr, key);
		if (!ret) {
			set_bit(key->keyidx, sc->sc_keymap);
			key->hw_key_idx = key->keyidx;
			/* push IV and Michael MIC generation to stack */
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_IV;
			if (key->alg == ALG_TKIP)
				key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIC;
		}
		break;
	case DISABLE_KEY:
		ath_key_delete(sc, key);
		clear_bit(key->keyidx, sc->sc_keymap);
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
		DPRINTF(sc, ATH_DBG_CONFIG, "%s: BSS Changed PREAMBLE %d\n",
			__func__,
			bss_conf->use_short_preamble);
		if (bss_conf->use_short_preamble)
			sc->sc_flags |= SC_OP_PREAMBLE_SHORT;
		else
			sc->sc_flags &= ~SC_OP_PREAMBLE_SHORT;
	}

	if (changed & BSS_CHANGED_ERP_CTS_PROT) {
		DPRINTF(sc, ATH_DBG_CONFIG, "%s: BSS Changed CTS PROT %d\n",
			__func__,
			bss_conf->use_cts_prot);
		if (bss_conf->use_cts_prot &&
		    hw->conf.channel->band != IEEE80211_BAND_5GHZ)
			sc->sc_flags |= SC_OP_PROTECT_ENABLE;
		else
			sc->sc_flags &= ~SC_OP_PROTECT_ENABLE;
	}

	if (changed & BSS_CHANGED_HT) {
		DPRINTF(sc, ATH_DBG_CONFIG, "%s: BSS Changed HT\n",
			__func__);
		ath9k_ht_conf(sc, bss_conf);
	}

	if (changed & BSS_CHANGED_ASSOC) {
		DPRINTF(sc, ATH_DBG_CONFIG, "%s: BSS Changed ASSOC %d\n",
			__func__,
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
				"%s: Unable to start TX aggregation\n",
				__func__);
		else
			ieee80211_start_tx_ba_cb_irqsafe(hw, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_STOP:
		ret = ath_tx_aggr_stop(sc, sta, tid);
		if (ret < 0)
			DPRINTF(sc, ATH_DBG_FATAL,
				"%s: Unable to stop TX aggregation\n",
				__func__);

		ieee80211_stop_tx_ba_cb_irqsafe(hw, sta->addr, tid);
		break;
	case IEEE80211_AMPDU_TX_RESUME:
		ath_tx_aggr_resume(sc, sta, tid);
		break;
	default:
		DPRINTF(sc, ATH_DBG_FATAL,
			"%s: Unknown AMPDU action\n", __func__);
	}

	return ret;
}

static int ath9k_no_fragmentation(struct ieee80211_hw *hw, u32 value)
{
	return -EOPNOTSUPP;
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
	.set_frag_threshold = ath9k_no_fragmentation,
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
			"DMA enable faled\n");
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
	printk(KERN_INFO "%s: %s\n", dev_info, ATH_PCI_VERSION);

	if (pci_register_driver(&ath_pci_driver) < 0) {
		printk(KERN_ERR
			"ath_pci: No devices found, driver not installed.\n");
		pci_unregister_driver(&ath_pci_driver);
		return -ENODEV;
	}

	return 0;
}
module_init(init_ath_pci);

static void __exit exit_ath_pci(void)
{
	pci_unregister_driver(&ath_pci_driver);
	printk(KERN_INFO "%s: driver unloaded\n", dev_info);
}
module_exit(exit_ath_pci);
