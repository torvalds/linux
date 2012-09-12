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

#include "ath9k.h"

/********************************/
/*	 LED functions		*/
/********************************/

#ifdef CONFIG_MAC80211_LEDS
static void ath_led_brightness(struct led_classdev *led_cdev,
			       enum led_brightness brightness)
{
	struct ath_softc *sc = container_of(led_cdev, struct ath_softc, led_cdev);
	ath9k_hw_set_gpio(sc->sc_ah, sc->sc_ah->led_pin, (brightness == LED_OFF));
}

void ath_deinit_leds(struct ath_softc *sc)
{
	if (!sc->led_registered)
		return;

	ath_led_brightness(&sc->led_cdev, LED_OFF);
	led_classdev_unregister(&sc->led_cdev);
}

void ath_init_leds(struct ath_softc *sc)
{
	int ret;

	if (AR_SREV_9100(sc->sc_ah))
		return;

	if (sc->sc_ah->led_pin < 0) {
		if (AR_SREV_9287(sc->sc_ah))
			sc->sc_ah->led_pin = ATH_LED_PIN_9287;
		else if (AR_SREV_9485(sc->sc_ah))
			sc->sc_ah->led_pin = ATH_LED_PIN_9485;
		else if (AR_SREV_9300(sc->sc_ah))
			sc->sc_ah->led_pin = ATH_LED_PIN_9300;
		else if (AR_SREV_9462(sc->sc_ah))
			sc->sc_ah->led_pin = ATH_LED_PIN_9462;
		else
			sc->sc_ah->led_pin = ATH_LED_PIN_DEF;
	}

	/* Configure gpio 1 for output */
	ath9k_hw_cfg_output(sc->sc_ah, sc->sc_ah->led_pin,
			    AR_GPIO_OUTPUT_MUX_AS_OUTPUT);
	/* LED off, active low */
	ath9k_hw_set_gpio(sc->sc_ah, sc->sc_ah->led_pin, 1);

	if (!led_blink)
		sc->led_cdev.default_trigger =
			ieee80211_get_radio_led_name(sc->hw);

	snprintf(sc->led_name, sizeof(sc->led_name),
		"ath9k-%s", wiphy_name(sc->hw->wiphy));
	sc->led_cdev.name = sc->led_name;
	sc->led_cdev.brightness_set = ath_led_brightness;

	ret = led_classdev_register(wiphy_dev(sc->hw->wiphy), &sc->led_cdev);
	if (ret < 0)
		return;

	sc->led_registered = true;
}
#endif

/*******************/
/*	Rfkill	   */
/*******************/

static bool ath_is_rfkill_set(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	bool is_blocked;

	ath9k_ps_wakeup(sc);
	is_blocked = ath9k_hw_gpio_get(ah, ah->rfkill_gpio) ==
				  ah->rfkill_polarity;
	ath9k_ps_restore(sc);

	return is_blocked;
}

void ath9k_rfkill_poll_state(struct ieee80211_hw *hw)
{
	struct ath_softc *sc = hw->priv;
	bool blocked = !!ath_is_rfkill_set(sc);

	wiphy_rfkill_set_hw_state(hw->wiphy, blocked);
}

void ath_start_rfkill_poll(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;

	if (ah->caps.hw_caps & ATH9K_HW_CAP_RFSILENT)
		wiphy_rfkill_start_polling(sc->hw->wiphy);
}

#ifdef CONFIG_ATH9K_BTCOEX_SUPPORT

/******************/
/*     BTCOEX     */
/******************/

/*
 * Detects if there is any priority bt traffic
 */
static void ath_detect_bt_priority(struct ath_softc *sc)
{
	struct ath_btcoex *btcoex = &sc->btcoex;
	struct ath_hw *ah = sc->sc_ah;

	if (ath9k_hw_gpio_get(sc->sc_ah, ah->btcoex_hw.btpriority_gpio))
		btcoex->bt_priority_cnt++;

	if (time_after(jiffies, btcoex->bt_priority_time +
			msecs_to_jiffies(ATH_BT_PRIORITY_TIME_THRESHOLD))) {
		clear_bit(BT_OP_PRIORITY_DETECTED, &btcoex->op_flags);
		clear_bit(BT_OP_SCAN, &btcoex->op_flags);
		/* Detect if colocated bt started scanning */
		if (btcoex->bt_priority_cnt >= ATH_BT_CNT_SCAN_THRESHOLD) {
			ath_dbg(ath9k_hw_common(sc->sc_ah), BTCOEX,
				"BT scan detected\n");
			set_bit(BT_OP_PRIORITY_DETECTED, &btcoex->op_flags);
			set_bit(BT_OP_SCAN, &btcoex->op_flags);
		} else if (btcoex->bt_priority_cnt >= ATH_BT_CNT_THRESHOLD) {
			ath_dbg(ath9k_hw_common(sc->sc_ah), BTCOEX,
				"BT priority traffic detected\n");
			set_bit(BT_OP_PRIORITY_DETECTED, &btcoex->op_flags);
		}

		btcoex->bt_priority_cnt = 0;
		btcoex->bt_priority_time = jiffies;
	}
}

static void ath9k_gen_timer_start(struct ath_hw *ah,
				  struct ath_gen_timer *timer,
				  u32 trig_timeout,
				  u32 timer_period)
{
	ath9k_hw_gen_timer_start(ah, timer, trig_timeout, timer_period);

	if ((ah->imask & ATH9K_INT_GENTIMER) == 0) {
		ath9k_hw_disable_interrupts(ah);
		ah->imask |= ATH9K_INT_GENTIMER;
		ath9k_hw_set_interrupts(ah);
		ath9k_hw_enable_interrupts(ah);
	}
}

static void ath9k_gen_timer_stop(struct ath_hw *ah, struct ath_gen_timer *timer)
{
	struct ath_gen_timer_table *timer_table = &ah->hw_gen_timers;

	ath9k_hw_gen_timer_stop(ah, timer);

	/* if no timer is enabled, turn off interrupt mask */
	if (timer_table->timer_mask.val == 0) {
		ath9k_hw_disable_interrupts(ah);
		ah->imask &= ~ATH9K_INT_GENTIMER;
		ath9k_hw_set_interrupts(ah);
		ath9k_hw_enable_interrupts(ah);
	}
}

/*
 * This is the master bt coex timer which runs for every
 * 45ms, bt traffic will be given priority during 55% of this
 * period while wlan gets remaining 45%
 */
static void ath_btcoex_period_timer(unsigned long data)
{
	struct ath_softc *sc = (struct ath_softc *) data;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_btcoex *btcoex = &sc->btcoex;
	struct ath_mci_profile *mci = &btcoex->mci;
	u32 timer_period;
	bool is_btscan;
	unsigned long flags;

	spin_lock_irqsave(&sc->sc_pm_lock, flags);
	if (sc->sc_ah->power_mode == ATH9K_PM_NETWORK_SLEEP) {
		spin_unlock_irqrestore(&sc->sc_pm_lock, flags);
		goto skip_hw_wakeup;
	}
	spin_unlock_irqrestore(&sc->sc_pm_lock, flags);

	ath9k_ps_wakeup(sc);
	if (!(ah->caps.hw_caps & ATH9K_HW_CAP_MCI))
		ath_detect_bt_priority(sc);
	is_btscan = test_bit(BT_OP_SCAN, &btcoex->op_flags);

	btcoex->bt_wait_time += btcoex->btcoex_period;
	if (btcoex->bt_wait_time > ATH_BTCOEX_RX_WAIT_TIME) {
		if (ar9003_mci_state(ah, MCI_STATE_NEED_FTP_STOMP) &&
		    (mci->num_pan || mci->num_other_acl))
			ah->btcoex_hw.mci.stomp_ftp =
				(sc->rx.num_pkts < ATH_BTCOEX_STOMP_FTP_THRESH);
		else
			ah->btcoex_hw.mci.stomp_ftp = false;
		btcoex->bt_wait_time = 0;
		sc->rx.num_pkts = 0;
	}

	spin_lock_bh(&btcoex->btcoex_lock);

	ath9k_hw_btcoex_bt_stomp(ah, is_btscan ? ATH_BTCOEX_STOMP_ALL :
			      btcoex->bt_stomp_type);

	ath9k_hw_btcoex_enable(ah);
	spin_unlock_bh(&btcoex->btcoex_lock);

	if (btcoex->btcoex_period != btcoex->btcoex_no_stomp) {
		if (btcoex->hw_timer_enabled)
			ath9k_gen_timer_stop(ah, btcoex->no_stomp_timer);

		timer_period = is_btscan ? btcoex->btscan_no_stomp :
					   btcoex->btcoex_no_stomp;
		ath9k_gen_timer_start(ah, btcoex->no_stomp_timer, timer_period,
				      timer_period * 10);
		btcoex->hw_timer_enabled = true;
	}

	ath9k_ps_restore(sc);
skip_hw_wakeup:
	timer_period = btcoex->btcoex_period;
	mod_timer(&btcoex->period_timer, jiffies + msecs_to_jiffies(timer_period));
}

/*
 * Generic tsf based hw timer which configures weight
 * registers to time slice between wlan and bt traffic
 */
static void ath_btcoex_no_stomp_timer(void *arg)
{
	struct ath_softc *sc = (struct ath_softc *)arg;
	struct ath_hw *ah = sc->sc_ah;
	struct ath_btcoex *btcoex = &sc->btcoex;
	struct ath_common *common = ath9k_hw_common(ah);

	ath_dbg(common, BTCOEX, "no stomp timer running\n");

	ath9k_ps_wakeup(sc);
	spin_lock_bh(&btcoex->btcoex_lock);

	if (btcoex->bt_stomp_type == ATH_BTCOEX_STOMP_LOW ||
	    test_bit(BT_OP_SCAN, &btcoex->op_flags))
		ath9k_hw_btcoex_bt_stomp(ah, ATH_BTCOEX_STOMP_NONE);
	 else if (btcoex->bt_stomp_type == ATH_BTCOEX_STOMP_ALL)
		ath9k_hw_btcoex_bt_stomp(ah, ATH_BTCOEX_STOMP_LOW);

	ath9k_hw_btcoex_enable(ah);
	spin_unlock_bh(&btcoex->btcoex_lock);
	ath9k_ps_restore(sc);
}

static int ath_init_btcoex_timer(struct ath_softc *sc)
{
	struct ath_btcoex *btcoex = &sc->btcoex;

	btcoex->btcoex_period = ATH_BTCOEX_DEF_BT_PERIOD;
	btcoex->btcoex_no_stomp = (100 - ATH_BTCOEX_DEF_DUTY_CYCLE) * 1000 *
		btcoex->btcoex_period / 100;
	btcoex->btscan_no_stomp = (100 - ATH_BTCOEX_BTSCAN_DUTY_CYCLE) * 1000 *
				   btcoex->btcoex_period / 100;

	setup_timer(&btcoex->period_timer, ath_btcoex_period_timer,
			(unsigned long) sc);

	spin_lock_init(&btcoex->btcoex_lock);

	btcoex->no_stomp_timer = ath_gen_timer_alloc(sc->sc_ah,
			ath_btcoex_no_stomp_timer,
			ath_btcoex_no_stomp_timer,
			(void *) sc, AR_FIRST_NDP_TIMER);

	if (!btcoex->no_stomp_timer)
		return -ENOMEM;

	return 0;
}

/*
 * (Re)start btcoex timers
 */
void ath9k_btcoex_timer_resume(struct ath_softc *sc)
{
	struct ath_btcoex *btcoex = &sc->btcoex;
	struct ath_hw *ah = sc->sc_ah;

	ath_dbg(ath9k_hw_common(ah), BTCOEX, "Starting btcoex timers\n");

	/* make sure duty cycle timer is also stopped when resuming */
	if (btcoex->hw_timer_enabled)
		ath9k_gen_timer_stop(sc->sc_ah, btcoex->no_stomp_timer);

	btcoex->bt_priority_cnt = 0;
	btcoex->bt_priority_time = jiffies;
	clear_bit(BT_OP_PRIORITY_DETECTED, &btcoex->op_flags);
	clear_bit(BT_OP_SCAN, &btcoex->op_flags);

	mod_timer(&btcoex->period_timer, jiffies);
}


/*
 * Pause btcoex timer and bt duty cycle timer
 */
void ath9k_btcoex_timer_pause(struct ath_softc *sc)
{
	struct ath_btcoex *btcoex = &sc->btcoex;
	struct ath_hw *ah = sc->sc_ah;

	del_timer_sync(&btcoex->period_timer);

	if (btcoex->hw_timer_enabled)
		ath9k_gen_timer_stop(ah, btcoex->no_stomp_timer);

	btcoex->hw_timer_enabled = false;
}

void ath9k_btcoex_stop_gen_timer(struct ath_softc *sc)
{
	struct ath_btcoex *btcoex = &sc->btcoex;

	ath9k_gen_timer_stop(sc->sc_ah, btcoex->no_stomp_timer);
}

u16 ath9k_btcoex_aggr_limit(struct ath_softc *sc, u32 max_4ms_framelen)
{
	struct ath_btcoex *btcoex = &sc->btcoex;
	struct ath_mci_profile *mci = &sc->btcoex.mci;
	u16 aggr_limit = 0;

	if ((sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_MCI) && mci->aggr_limit)
		aggr_limit = (max_4ms_framelen * mci->aggr_limit) >> 4;
	else if (test_bit(BT_OP_PRIORITY_DETECTED, &btcoex->op_flags))
		aggr_limit = min((max_4ms_framelen * 3) / 8,
				 (u32)ATH_AMPDU_LIMIT_MAX);

	return aggr_limit;
}

void ath9k_btcoex_handle_interrupt(struct ath_softc *sc, u32 status)
{
	struct ath_hw *ah = sc->sc_ah;

	if (ath9k_hw_get_btcoex_scheme(ah) == ATH_BTCOEX_CFG_3WIRE)
		if (status & ATH9K_INT_GENTIMER)
			ath_gen_timer_isr(sc->sc_ah);

	if (status & ATH9K_INT_MCI)
		ath_mci_intr(sc);
}

void ath9k_start_btcoex(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;

	if ((ath9k_hw_get_btcoex_scheme(ah) != ATH_BTCOEX_CFG_NONE) &&
	    !ah->btcoex_hw.enabled) {
		if (!(sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_MCI))
			ath9k_hw_btcoex_set_weight(ah, AR_BT_COEX_WGHT,
						   AR_STOMP_LOW_WLAN_WGHT);
		ath9k_hw_btcoex_enable(ah);

		if (ath9k_hw_get_btcoex_scheme(ah) == ATH_BTCOEX_CFG_3WIRE)
			ath9k_btcoex_timer_resume(sc);
	}
}

void ath9k_stop_btcoex(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;

	if (ah->btcoex_hw.enabled &&
	    ath9k_hw_get_btcoex_scheme(ah) != ATH_BTCOEX_CFG_NONE) {
		if (ath9k_hw_get_btcoex_scheme(ah) == ATH_BTCOEX_CFG_3WIRE)
			ath9k_btcoex_timer_pause(sc);
		ath9k_hw_btcoex_disable(ah);
		if (AR_SREV_9462(ah))
			ath_mci_flush_profile(&sc->btcoex.mci);
	}
}

void ath9k_deinit_btcoex(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;

        if ((sc->btcoex.no_stomp_timer) &&
	    ath9k_hw_get_btcoex_scheme(sc->sc_ah) == ATH_BTCOEX_CFG_3WIRE)
		ath_gen_timer_free(sc->sc_ah, sc->btcoex.no_stomp_timer);

	if (ath9k_hw_mci_is_enabled(ah))
		ath_mci_cleanup(sc);
}

int ath9k_init_btcoex(struct ath_softc *sc)
{
	struct ath_txq *txq;
	struct ath_hw *ah = sc->sc_ah;
	int r;

	ath9k_hw_btcoex_init_scheme(ah);

	switch (ath9k_hw_get_btcoex_scheme(sc->sc_ah)) {
	case ATH_BTCOEX_CFG_NONE:
		break;
	case ATH_BTCOEX_CFG_2WIRE:
		ath9k_hw_btcoex_init_2wire(sc->sc_ah);
		break;
	case ATH_BTCOEX_CFG_3WIRE:
		ath9k_hw_btcoex_init_3wire(sc->sc_ah);
		r = ath_init_btcoex_timer(sc);
		if (r)
			return -1;
		txq = sc->tx.txq_map[WME_AC_BE];
		ath9k_hw_init_btcoex_hw(sc->sc_ah, txq->axq_qnum);
		sc->btcoex.bt_stomp_type = ATH_BTCOEX_STOMP_LOW;
		if (ath9k_hw_mci_is_enabled(ah)) {
			sc->btcoex.duty_cycle = ATH_BTCOEX_DEF_DUTY_CYCLE;
			INIT_LIST_HEAD(&sc->btcoex.mci.info);

			r = ath_mci_setup(sc);
			if (r)
				return r;

			ath9k_hw_btcoex_init_mci(ah);
		}

		break;
	default:
		WARN_ON(1);
		break;
	}

	return 0;
}

#endif /* CONFIG_ATH9K_BTCOEX_SUPPORT */
