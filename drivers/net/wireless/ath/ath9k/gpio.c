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

#include "ath9k.h"

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
			if (led_blink)
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
		ath_err(ath9k_hw_common(sc->sc_ah),
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

void ath_deinit_leds(struct ath_softc *sc)
{
	ath_unregister_led(&sc->assoc_led);
	sc->sc_flags &= ~SC_OP_LED_ASSOCIATED;
	ath_unregister_led(&sc->tx_led);
	ath_unregister_led(&sc->rx_led);
	ath_unregister_led(&sc->radio_led);
	ath9k_hw_set_gpio(sc->sc_ah, sc->sc_ah->led_pin, 1);
}

void ath_init_leds(struct ath_softc *sc)
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

	if (led_blink)
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
	if (led_blink)
		cancel_delayed_work_sync(&sc->ath_led_blink_work);
	ath_deinit_leds(sc);
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

void ath9k_rfkill_poll_state(struct ieee80211_hw *hw)
{
	struct ath_wiphy *aphy = hw->priv;
	struct ath_softc *sc = aphy->sc;
	bool blocked = !!ath_is_rfkill_set(sc);

	wiphy_rfkill_set_hw_state(hw->wiphy, blocked);
}

void ath_start_rfkill_poll(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;

	if (ah->caps.hw_caps & ATH9K_HW_CAP_RFSILENT)
		wiphy_rfkill_start_polling(sc->hw->wiphy);
}

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
		sc->sc_flags &= ~(SC_OP_BT_PRIORITY_DETECTED | SC_OP_BT_SCAN);
		/* Detect if colocated bt started scanning */
		if (btcoex->bt_priority_cnt >= ATH_BT_CNT_SCAN_THRESHOLD) {
			ath_dbg(ath9k_hw_common(sc->sc_ah), ATH_DBG_BTCOEX,
				"BT scan detected\n");
			sc->sc_flags |= (SC_OP_BT_SCAN |
					 SC_OP_BT_PRIORITY_DETECTED);
		} else if (btcoex->bt_priority_cnt >= ATH_BT_CNT_THRESHOLD) {
			ath_dbg(ath9k_hw_common(sc->sc_ah), ATH_DBG_BTCOEX,
				"BT priority traffic detected\n");
			sc->sc_flags |= SC_OP_BT_PRIORITY_DETECTED;
		}

		btcoex->bt_priority_cnt = 0;
		btcoex->bt_priority_time = jiffies;
	}
}

static void ath9k_gen_timer_start(struct ath_hw *ah,
				  struct ath_gen_timer *timer,
				  u32 timer_next,
				  u32 timer_period)
{
	ath9k_hw_gen_timer_start(ah, timer, timer_next, timer_period);

	if ((ah->imask & ATH9K_INT_GENTIMER) == 0) {
		ath9k_hw_disable_interrupts(ah);
		ah->imask |= ATH9K_INT_GENTIMER;
		ath9k_hw_set_interrupts(ah, ah->imask);
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
		ath9k_hw_set_interrupts(ah, ah->imask);
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
	struct ath_common *common = ath9k_hw_common(ah);
	u32 timer_period;
	bool is_btscan;

	ath_detect_bt_priority(sc);

	is_btscan = sc->sc_flags & SC_OP_BT_SCAN;

	spin_lock_bh(&btcoex->btcoex_lock);

	ath9k_cmn_btcoex_bt_stomp(common, is_btscan ? ATH_BTCOEX_STOMP_ALL :
			      btcoex->bt_stomp_type);

	spin_unlock_bh(&btcoex->btcoex_lock);

	if (btcoex->btcoex_period != btcoex->btcoex_no_stomp) {
		if (btcoex->hw_timer_enabled)
			ath9k_gen_timer_stop(ah, btcoex->no_stomp_timer);

		timer_period = is_btscan ? btcoex->btscan_no_stomp :
					   btcoex->btcoex_no_stomp;
		ath9k_gen_timer_start(ah, btcoex->no_stomp_timer, 0,
				      timer_period * 10);
		btcoex->hw_timer_enabled = true;
	}

	mod_timer(&btcoex->period_timer, jiffies +
				  msecs_to_jiffies(ATH_BTCOEX_DEF_BT_PERIOD));
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
	bool is_btscan = sc->sc_flags & SC_OP_BT_SCAN;

	ath_dbg(common, ATH_DBG_BTCOEX,
		"no stomp timer running\n");

	spin_lock_bh(&btcoex->btcoex_lock);

	if (btcoex->bt_stomp_type == ATH_BTCOEX_STOMP_LOW || is_btscan)
		ath9k_cmn_btcoex_bt_stomp(common, ATH_BTCOEX_STOMP_NONE);
	 else if (btcoex->bt_stomp_type == ATH_BTCOEX_STOMP_ALL)
		ath9k_cmn_btcoex_bt_stomp(common, ATH_BTCOEX_STOMP_LOW);

	spin_unlock_bh(&btcoex->btcoex_lock);
}

int ath_init_btcoex_timer(struct ath_softc *sc)
{
	struct ath_btcoex *btcoex = &sc->btcoex;

	btcoex->btcoex_period = ATH_BTCOEX_DEF_BT_PERIOD * 1000;
	btcoex->btcoex_no_stomp = (100 - ATH_BTCOEX_DEF_DUTY_CYCLE) *
		btcoex->btcoex_period / 100;
	btcoex->btscan_no_stomp = (100 - ATH_BTCOEX_BTSCAN_DUTY_CYCLE) *
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

	ath_dbg(ath9k_hw_common(ah), ATH_DBG_BTCOEX,
		"Starting btcoex timers\n");

	/* make sure duty cycle timer is also stopped when resuming */
	if (btcoex->hw_timer_enabled)
		ath9k_gen_timer_stop(sc->sc_ah, btcoex->no_stomp_timer);

	btcoex->bt_priority_cnt = 0;
	btcoex->bt_priority_time = jiffies;
	sc->sc_flags &= ~(SC_OP_BT_PRIORITY_DETECTED | SC_OP_BT_SCAN);

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
