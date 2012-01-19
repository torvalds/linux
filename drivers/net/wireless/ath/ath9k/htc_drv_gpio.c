/*
 * Copyright (c) 2010-2011 Atheros Communications Inc.
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
			ath_dbg(ath9k_hw_common(ah), BTCOEX,
				"BT scan detected\n");
			priv->op_flags |= (OP_BT_SCAN |
					 OP_BT_PRIORITY_DETECTED);
		} else if (btcoex->bt_priority_cnt >= ATH_BT_CNT_THRESHOLD) {
			ath_dbg(ath9k_hw_common(ah), BTCOEX,
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

	ath_detect_bt_priority(priv);

	is_btscan = !!(priv->op_flags & OP_BT_SCAN);

	ret = ath9k_htc_update_cap_target(priv,
				  !!(priv->op_flags & OP_BT_PRIORITY_DETECTED));
	if (ret) {
		ath_err(common, "Unable to set BTCOEX parameters\n");
		return;
	}

	ath9k_hw_btcoex_bt_stomp(priv->ah, is_btscan ? ATH_BTCOEX_STOMP_ALL :
			btcoex->bt_stomp_type);

	ath9k_hw_btcoex_enable(priv->ah);
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

	ath_dbg(common, BTCOEX, "time slice work for bt and wlan\n");

	if (btcoex->bt_stomp_type == ATH_BTCOEX_STOMP_LOW || is_btscan)
		ath9k_hw_btcoex_bt_stomp(ah, ATH_BTCOEX_STOMP_NONE);
	else if (btcoex->bt_stomp_type == ATH_BTCOEX_STOMP_ALL)
		ath9k_hw_btcoex_bt_stomp(ah, ATH_BTCOEX_STOMP_LOW);
	ath9k_hw_btcoex_enable(priv->ah);
}

void ath_htc_init_btcoex_work(struct ath9k_htc_priv *priv)
{
	struct ath_btcoex *btcoex = &priv->btcoex;

	if (ath9k_hw_get_btcoex_scheme(priv->ah) == ATH_BTCOEX_CFG_NONE)
		return;

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

	if (ath9k_hw_get_btcoex_scheme(ah) == ATH_BTCOEX_CFG_NONE)
		return;

	ath_dbg(ath9k_hw_common(ah), BTCOEX, "Starting btcoex work\n");

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
	if (ath9k_hw_get_btcoex_scheme(priv->ah) == ATH_BTCOEX_CFG_NONE)
		return;

	cancel_delayed_work_sync(&priv->coex_period_work);
	cancel_delayed_work_sync(&priv->duty_cycle_work);
}

/*******/
/* LED */
/*******/

#ifdef CONFIG_MAC80211_LEDS
void ath9k_led_work(struct work_struct *work)
{
	struct ath9k_htc_priv *priv = container_of(work,
						   struct ath9k_htc_priv,
						   led_work);

	ath9k_hw_set_gpio(priv->ah, priv->ah->led_pin,
			  (priv->brightness == LED_OFF));
}

static void ath9k_led_brightness(struct led_classdev *led_cdev,
				 enum led_brightness brightness)
{
	struct ath9k_htc_priv *priv = container_of(led_cdev,
						   struct ath9k_htc_priv,
						   led_cdev);

	/* Not locked, but it's just a tiny green light..*/
	priv->brightness = brightness;
	ieee80211_queue_work(priv->hw, &priv->led_work);
}

void ath9k_deinit_leds(struct ath9k_htc_priv *priv)
{
	if (!priv->led_registered)
		return;

	ath9k_led_brightness(&priv->led_cdev, LED_OFF);
	led_classdev_unregister(&priv->led_cdev);
	cancel_work_sync(&priv->led_work);
}

void ath9k_init_leds(struct ath9k_htc_priv *priv)
{
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

	snprintf(priv->led_name, sizeof(priv->led_name),
		"ath9k_htc-%s", wiphy_name(priv->hw->wiphy));
	priv->led_cdev.name = priv->led_name;
	priv->led_cdev.brightness_set = ath9k_led_brightness;

	ret = led_classdev_register(wiphy_dev(priv->hw->wiphy), &priv->led_cdev);
	if (ret < 0)
		return;

	INIT_WORK(&priv->led_work, ath9k_led_work);
	priv->led_registered = true;

	return;
}
#endif

/*******************/
/*	Rfkill	   */
/*******************/

static bool ath_is_rfkill_set(struct ath9k_htc_priv *priv)
{
	bool is_blocked;

	ath9k_htc_ps_wakeup(priv);
	is_blocked = ath9k_hw_gpio_get(priv->ah, priv->ah->rfkill_gpio) ==
						 priv->ah->rfkill_polarity;
	ath9k_htc_ps_restore(priv);

	return is_blocked;
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
	spin_lock_bh(&priv->tx.tx_lock);
	priv->tx.flags &= ~ATH9K_HTC_OP_TX_QUEUES_STOP;
	spin_unlock_bh(&priv->tx.tx_lock);
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
	ath9k_htc_tx_drain(priv);
	WMI_CMD(WMI_DRAIN_TXQ_ALL_CMDID);

	/* Stop RX */
	WMI_CMD(WMI_STOP_RECV_CMDID);

	/* Clear the WMI event queue */
	ath9k_wmi_event_drain(priv);

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
