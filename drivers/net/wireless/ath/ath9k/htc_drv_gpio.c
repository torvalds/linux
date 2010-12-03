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
