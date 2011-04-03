/*
 * Copyright (c) 2010 Atheros Communications Inc.
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

#include "htc.h"

/******************/
/*     BTCOEX     */
/******************/

/*
 * Detects if there is any priority bt traffic
 */
static void ath_detect_bt_priority(struct ath9k_htc_priv *priv)
{
	struct ath_btcoex *btcoex = &priv->btcoex;
	struct ath_hw *ah = priv->ah;

	if (ath9k_hw_gpio_get(ah, ah->btcoex_hw.btpriority_gpio))
		btcoex->bt_priority_cnt++;

	if (time_after(jiffies, btcoex->bt_priority_time +
			msecs_to_jiffies(ATH_BT_PRIORITY_TIME_THRESHOLD))) {
		priv->op_flags &= ~(OP_BT_PRIORITY_DETECTED | OP_BT_SCAN);
		/* Detect if colocated bt started scanning */
		if (btcoex->bt_priority_cnt >= ATH_BT_CNT_SCAN_THRESHOLD) {
			ath_dbg(ath9k_hw_common(ah), ATH_DBG_BTCOEX,
				"BT scan detected\n");
			priv->op_flags |= (OP_BT_SCAN |
					 OP_BT_PRIORITY_DETECTED);
		} else if (btcoex->bt_priority_cnt >= ATH_BT_CNT_THRESHOLD) {
			ath_dbg(ath9k_hw_common(ah), ATH_DBG_BTCOEX,
				"BT priority traffic detected\n");
			priv->op_flags |= OP_BT_PRIORITY_DETECTED;
		}

		btcoex->bt_priority_cnt = 0;
		btcoex->bt_priority_time = jiffies;
	}
}

/*
 * This is the master bt coex work which runs for every
 * 45ms, bt traffic will be given priority during 55% of this
 * period while wlan gets remaining 45%
 */
static void ath_btcoex_period_work(struct work_struct *work)
{
	struct ath9k_htc_priv *priv = container_of(work, struct ath9k_htc_priv,
						   coex_period_work.work);
	struct ath_btcoex *btcoex = &priv->btcoex;
	struct ath_common *common = ath9k_hw_common(priv->ah);
	u32 timer_period;
	bool is_btscan;
	int ret;
	u8 cmd_rsp, aggr;

	ath_detect_bt_priority(priv);

	is_btscan = !!(priv->op_flags & OP_BT_SCAN);

	aggr = priv->op_flags & OP_BT_PRIORITY_DETECTED;

	WMI_CMD_BUF(WMI_AGGR_LIMIT_CMD, &aggr);

	ath9k_cmn_btcoex_bt_stomp(common, is_btscan ? ATH_BTCOEX_STOMP_ALL :
			btcoex->bt_stomp_type);

	timer_period = is_btscan ? btcoex->btscan_no_stomp :
		btcoex->btcoex_no_stomp;
	ieee80211_queue_delayed_work(priv->hw, &priv->duty_cycle_work,
				     msecs_to_jiffies(timer_period));
	ieee80211_queue_delayed_work(priv->hw, &priv->coex_period_work,
				     msecs_to_jiffies(btcoex->btcoex_period));
}

/*
 * Work to time slice between wlan and bt traffic and
 * configure weight registers
 */
static void ath_btcoex_duty_cycle_work(struct work_struct *work)
{
	struct ath9k_htc_priv *priv = container_of(work, struct ath9k_htc_priv,
						   duty_cycle_work.work);
	struct ath_hw *ah = priv->ah;
	struct ath_btcoex *btcoex = &priv->btcoex;
	struct ath_common *common = ath9k_hw_common(ah);
	bool is_btscan = priv->op_flags & OP_BT_SCAN;

	ath_dbg(common, ATH_DBG_BTCOEX,
		"time slice work for bt and wlan\n");

	if (btcoex->bt_stomp_type == ATH_BTCOEX_STOMP_LOW || is_btscan)
		ath9k_cmn_btcoex_bt_stomp(common, ATH_BTCOEX_STOMP_NONE);
	else if (btcoex->bt_stomp_type == ATH_BTCOEX_STOMP_ALL)
		ath9k_cmn_btcoex_bt_stomp(common, ATH_BTCOEX_STOMP_LOW);
}

void ath_htc_init_btcoex_work(struct ath9k_htc_priv *priv)
{
	struct ath_btcoex *btcoex = &priv->btcoex;

	btcoex->btcoex_period = ATH_BTCOEX_DEF_BT_PERIOD;
	btcoex->btcoex_no_stomp = (100 - ATH_BTCOEX_DEF_DUTY_CYCLE) *
		btcoex->btcoex_period / 100;
	btcoex->btscan_no_stomp = (100 - ATH_BTCOEX_BTSCAN_DUTY_CYCLE) *
				   btcoex->btcoex_period / 100;
	INIT_DELAYED_WORK(&priv->coex_period_work, ath_btcoex_period_work);
	INIT_DELAYED_WORK(&priv->duty_cycle_work, ath_btcoex_duty_cycle_work);
}

/*
 * (Re)start btcoex work
 */

void ath_htc_resume_btcoex_work(struct ath9k_htc_priv *priv)
{
	struct ath_btcoex *btcoex = &priv->btcoex;
	struct ath_hw *ah = priv->ah;

	ath_dbg(ath9k_hw_common(ah), ATH_DBG_BTCOEX, "Starting btcoex work\n");

	btcoex->bt_priority_cnt = 0;
	btcoex->bt_priority_time = jiffies;
	priv->op_flags &= ~(OP_BT_PRIORITY_DETECTED | OP_BT_SCAN);
	ieee80211_queue_delayed_work(priv->hw, &priv->coex_period_work, 0);
}


/*
 * Cancel btcoex and bt duty cycle work.
 */
void ath_htc_cancel_btcoex_work(struct ath9k_htc_priv *priv)
{
	cancel_delayed_work_sync(&priv->coex_period_work);
	cancel_delayed_work_sync(&priv->duty_cycle_work);
}

/*******/
/* LED */
/*******/

static void ath9k_led_blink_work(struct work_struct *work)
{
	struct ath9k_htc_priv *priv = container_of(work, struct ath9k_htc_priv,
						   ath9k_led_blink_work.work);

	if (!(priv->op_flags & OP_LED_ASSOCIATED))
		return;

	if ((priv->led_on_duration == ATH_LED_ON_DURATION_IDLE) ||
	    (priv->led_off_duration == ATH_LED_OFF_DURATION_IDLE))
		ath9k_hw_set_gpio(priv->ah, priv->ah->led_pin, 0);
	else
		ath9k_hw_set_gpio(priv->ah, priv->ah->led_pin,
				  (priv->op_flags & OP_LED_ON) ? 1 : 0);

	ieee80211_queue_delayed_work(priv->hw,
				     &priv->ath9k_led_blink_work,
				     (priv->op_flags & OP_LED_ON) ?
				     msecs_to_jiffies(priv->led_off_duration) :
				     msecs_to_jiffies(priv->led_on_duration));

	priv->led_on_duration = priv->led_on_cnt ?
		max((ATH_LED_ON_DURATION_IDLE - priv->led_on_cnt), 25) :
		ATH_LED_ON_DURATION_IDLE;
	priv->led_off_duration = priv->led_off_cnt ?
		max((ATH_LED_OFF_DURATION_IDLE - priv->led_off_cnt), 10) :
		ATH_LED_OFF_DURATION_IDLE;
	priv->led_on_cnt = priv->led_off_cnt = 0;

	if (priv->op_flags & OP_LED_ON)
		priv->op_flags &= ~OP_LED_ON;
	else
		priv->op_flags |= OP_LED_ON;
}

static void ath9k_led_brightness_work(struct work_struct *work)
{
	struct ath_led *led = container_of(work, struct ath_led,
					   brightness_work.work);
	struct ath9k_htc_priv *priv = led->priv;

	switch (led->brightness) {
	case LED_OFF:
		if (led->led_type == ATH_LED_ASSOC ||
		    led->led_type == ATH_LED_RADIO) {
			ath9k_hw_set_gpio(priv->ah, priv->ah->led_pin,
					  (led->led_type == ATH_LED_RADIO));
			priv->op_flags &= ~OP_LED_ASSOCIATED;
			if (led->led_type == ATH_LED_RADIO)
				priv->op_flags &= ~OP_LED_ON;
		} else {
			priv->led_off_cnt++;
		}
		break;
	case LED_FULL:
		if (led->led_type == ATH_LED_ASSOC) {
			priv->op_flags |= OP_LED_ASSOCIATED;
			ieee80211_queue_delayed_work(priv->hw,
					     &priv->ath9k_led_blink_work, 0);
		} else if (led->led_type == ATH_LED_RADIO) {
			ath9k_hw_set_gpio(priv->ah, priv->ah->led_pin, 0);
			priv->op_flags |= OP_LED_ON;
		} else {
			priv->led_on_cnt++;
		}
		break;
	default:
		break;
	}
}

static void ath9k_led_brightness(struct led_classdev *led_cdev,
				 enum led_brightness brightness)
{
	struct ath_led *led = container_of(led_cdev, struct ath_led, led_cdev);
	struct ath9k_htc_priv *priv = led->priv;

	led->brightness = brightness;
	if (!(priv->op_flags & OP_LED_DEINIT))
		ieee80211_queue_delayed_work(priv->hw,
					     &led->brightness_work, 0);
}

void ath9k_led_stop_brightness(struct ath9k_htc_priv *priv)
{
	cancel_delayed_work_sync(&priv->radio_led.brightness_work);
	cancel_delayed_work_sync(&priv->assoc_led.brightness_work);
	cancel_delayed_work_sync(&priv->tx_led.brightness_work);
	cancel_delayed_work_sync(&priv->rx_led.brightness_work);
}

static int ath9k_register_led(struct ath9k_htc_priv *priv, struct ath_led *led,
			      char *trigger)
{
	int ret;

	led->priv = priv;
	led->led_cdev.name = led->name;
	led->led_cdev.default_trigger = trigger;
	led->led_cdev.brightness_set = ath9k_led_brightness;

	ret = led_classdev_register(wiphy_dev(priv->hw->wiphy), &led->led_cdev);
	if (ret)
		ath_err(ath9k_hw_common(priv->ah),
			"Failed to register led:%s", led->name);
	else
		led->registered = 1;

	INIT_DELAYED_WORK(&led->brightness_work, ath9k_led_brightness_work);

	return ret;
}

static void ath9k_unregister_led(struct ath_led *led)
{
	if (led->registered) {
		led_classdev_unregister(&led->led_cdev);
		led->registered = 0;
	}
}

void ath9k_deinit_leds(struct ath9k_htc_priv *priv)
{
	priv->op_flags |= OP_LED_DEINIT;
	ath9k_unregister_led(&priv->assoc_led);
	priv->op_flags &= ~OP_LED_ASSOCIATED;
	ath9k_unregister_led(&priv->tx_led);
	ath9k_unregister_led(&priv->rx_led);
	ath9k_unregister_led(&priv->radio_led);
}

void ath9k_init_leds(struct ath9k_htc_priv *priv)
{
	char *trigger;
	int ret;

	if (AR_SREV_9287(priv->ah))
		priv->ah->led_pin = ATH_LED_PIN_9287;
	else if (AR_SREV_9271(priv->ah))
		priv->ah->led_pin = ATH_LED_PIN_9271;
	else if (AR_DEVID_7010(priv->ah))
		priv->ah->led_pin = ATH_LED_PIN_7010;
	else
		priv->ah->led_pin = ATH_LED_PIN_DEF;

	/* Configure gpio 1 for output */
	ath9k_hw_cfg_output(priv->ah, priv->ah->led_pin,
			    AR_GPIO_OUTPUT_MUX_AS_OUTPUT);
	/* LED off, active low */
	ath9k_hw_set_gpio(priv->ah, priv->ah->led_pin, 1);

	INIT_DELAYED_WORK(&priv->ath9k_led_blink_work, ath9k_led_blink_work);

	trigger = ieee80211_get_radio_led_name(priv->hw);
	snprintf(priv->radio_led.name, sizeof(priv->radio_led.name),
		"ath9k-%s::radio", wiphy_name(priv->hw->wiphy));
	ret = ath9k_register_led(priv, &priv->radio_led, trigger);
	priv->radio_led.led_type = ATH_LED_RADIO;
	if (ret)
		goto fail;

	trigger = ieee80211_get_assoc_led_name(priv->hw);
	snprintf(priv->assoc_led.name, sizeof(priv->assoc_led.name),
		"ath9k-%s::assoc", wiphy_name(priv->hw->wiphy));
	ret = ath9k_register_led(priv, &priv->assoc_led, trigger);
	priv->assoc_led.led_type = ATH_LED_ASSOC;
	if (ret)
		goto fail;

	trigger = ieee80211_get_tx_led_name(priv->hw);
	snprintf(priv->tx_led.name, sizeof(priv->tx_led.name),
		"ath9k-%s::tx", wiphy_name(priv->hw->wiphy));
	ret = ath9k_register_led(priv, &priv->tx_led, trigger);
	priv->tx_led.led_type = ATH_LED_TX;
	if (ret)
		goto fail;

	trigger = ieee80211_get_rx_led_name(priv->hw);
	snprintf(priv->rx_led.name, sizeof(priv->rx_led.name),
		"ath9k-%s::rx", wiphy_name(priv->hw->wiphy));
	ret = ath9k_register_led(priv, &priv->rx_led, trigger);
	priv->rx_led.led_type = ATH_LED_RX;
	if (ret)
		goto fail;

	priv->op_flags &= ~OP_LED_DEINIT;

	return;

fail:
	cancel_delayed_work_sync(&priv->ath9k_led_blink_work);
	ath9k_deinit_leds(priv);
}

/*******************/
/*	Rfkill	   */
/*******************/

static bool ath_is_rfkill_set(struct ath9k_htc_priv *priv)
{
	return ath9k_hw_gpio_get(priv->ah, priv->ah->rfkill_gpio) ==
		priv->ah->rfkill_polarity;
}

void ath9k_htc_rfkill_poll_state(struct ieee80211_hw *hw)
{
	struct ath9k_htc_priv *priv = hw->priv;
	bool blocked = !!ath_is_rfkill_set(priv);

	wiphy_rfkill_set_hw_state(hw->wiphy, blocked);
}

void ath9k_start_rfkill_poll(struct ath9k_htc_priv *priv)
{
	if (priv->ah->caps.hw_caps & ATH9K_HW_CAP_RFSILENT)
		wiphy_rfkill_start_polling(priv->hw->wiphy);
}

void ath9k_htc_radio_enable(struct ieee80211_hw *hw)
{
	struct ath9k_htc_priv *priv = hw->priv;
	struct ath_hw *ah = priv->ah;
	struct ath_common *common = ath9k_hw_common(ah);
	int ret;
	u8 cmd_rsp;

	if (!ah->curchan)
		ah->curchan = ath9k_cmn_get_curchannel(hw, ah);

	/* Reset the HW */
	ret = ath9k_hw_reset(ah, ah->curchan, ah->caldata, false);
	if (ret) {
		ath_err(common,
			"Unable to reset hardware; reset status %d (freq %u MHz)\n",
			ret, ah->curchan->channel);
	}

	ath9k_cmn_update_txpow(ah, priv->curtxpow, priv->txpowlimit,
			       &priv->curtxpow);

	/* Start RX */
	WMI_CMD(WMI_START_RECV_CMDID);
	ath9k_host_rx_init(priv);

	/* Start TX */
	htc_start(priv->htc);
	spin_lock_bh(&priv->tx_lock);
	priv->tx_queues_stop = false;
	spin_unlock_bh(&priv->tx_lock);
	ieee80211_wake_queues(hw);

	WMI_CMD(WMI_ENABLE_INTR_CMDID);

	/* Enable LED */
	ath9k_hw_cfg_output(ah, ah->led_pin,
			    AR_GPIO_OUTPUT_MUX_AS_OUTPUT);
	ath9k_hw_set_gpio(ah, ah->led_pin, 0);
}

void ath9k_htc_radio_disable(struct ieee80211_hw *hw)
{
	struct ath9k_htc_priv *priv = hw->priv;
	struct ath_hw *ah = priv->ah;
	struct ath_common *common = ath9k_hw_common(ah);
	int ret;
	u8 cmd_rsp;

	ath9k_htc_ps_wakeup(priv);

	/* Disable LED */
	ath9k_hw_set_gpio(ah, ah->led_pin, 1);
	ath9k_hw_cfg_gpio_input(ah, ah->led_pin);

	WMI_CMD(WMI_DISABLE_INTR_CMDID);

	/* Stop TX */
	ieee80211_stop_queues(hw);
	htc_stop(priv->htc);
	WMI_CMD(WMI_DRAIN_TXQ_ALL_CMDID);
	skb_queue_purge(&priv->tx_queue);

	/* Stop RX */
	WMI_CMD(WMI_STOP_RECV_CMDID);

	/*
	 * The MIB counters have to be disabled here,
	 * since the target doesn't do it.
	 */
	ath9k_hw_disable_mib_counters(ah);

	if (!ah->curchan)
		ah->curchan = ath9k_cmn_get_curchannel(hw, ah);

	/* Reset the HW */
	ret = ath9k_hw_reset(ah, ah->curchan, ah->caldata, false);
	if (ret) {
		ath_err(common,
			"Unable to reset hardware; reset status %d (freq %u MHz)\n",
			ret, ah->curchan->channel);
	}

	/* Disable the PHY */
	ath9k_hw_phy_disable(ah);

	ath9k_htc_ps_restore(priv);
	ath9k_htc_setpower(priv, ATH9K_PM_FULL_SLEEP);
}
