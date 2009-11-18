/*
 * Copyright (c) 2009 Atheros Communications Inc.
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

static const struct ath_btcoex_config ath_bt_config = { 0, true, true,
			ATH_BT_COEX_MODE_SLOTTED, true, true, 2, 5, true };

static const u16 ath_subsysid_tbl[] = {
	AR9280_COEX2WIRE_SUBSYSID,
	AT9285_COEX3WIRE_SA_SUBSYSID,
	AT9285_COEX3WIRE_DA_SUBSYSID
};

/*
 * Checks the subsystem id of the device to see if it
 * supports btcoex
 */
bool ath_btcoex_supported(u16 subsysid)
{
	int i;

	if (!subsysid)
		return false;

	for (i = 0; i < ARRAY_SIZE(ath_subsysid_tbl); i++)
		if (subsysid == ath_subsysid_tbl[i])
			return true;

	return false;
}

/*
 * Detects if there is any priority bt traffic
 */
static void ath_detect_bt_priority(struct ath_softc *sc)
{
	struct ath_btcoex_info *btinfo = &sc->btcoex_info;

	if (ath9k_hw_gpio_get(sc->sc_ah, btinfo->btpriority_gpio))
		btinfo->bt_priority_cnt++;

	if (time_after(jiffies, btinfo->bt_priority_time +
			msecs_to_jiffies(ATH_BT_PRIORITY_TIME_THRESHOLD))) {
		if (btinfo->bt_priority_cnt >= ATH_BT_CNT_THRESHOLD) {
			DPRINTF(sc, ATH_DBG_BTCOEX,
				"BT priority traffic detected");
			sc->sc_flags |= SC_OP_BT_PRIORITY_DETECTED;
		} else {
			sc->sc_flags &= ~SC_OP_BT_PRIORITY_DETECTED;
		}

		btinfo->bt_priority_cnt = 0;
		btinfo->bt_priority_time = jiffies;
	}
}

/*
 * Configures appropriate weight based on stomp type.
 */
static void ath_btcoex_bt_stomp(struct ath_softc *sc,
				struct ath_btcoex_info *btinfo,
				int stomp_type)
{

	switch (stomp_type) {
	case ATH_BTCOEX_STOMP_ALL:
		ath_btcoex_set_weight(btinfo, AR_BT_COEX_WGHT,
				      AR_STOMP_ALL_WLAN_WGHT);
		break;
	case ATH_BTCOEX_STOMP_LOW:
		ath_btcoex_set_weight(btinfo, AR_BT_COEX_WGHT,
				      AR_STOMP_LOW_WLAN_WGHT);
		break;
	case ATH_BTCOEX_STOMP_NONE:
		ath_btcoex_set_weight(btinfo, AR_BT_COEX_WGHT,
				      AR_STOMP_NONE_WLAN_WGHT);
		break;
	default:
		DPRINTF(sc, ATH_DBG_BTCOEX, "Invalid Stomptype\n");
		break;
	}

	ath9k_hw_btcoex_enable(sc->sc_ah);
}

/*
 * This is the master bt coex timer which runs for every
 * 45ms, bt traffic will be given priority during 55% of this
 * period while wlan gets remaining 45%
 */

static void ath_btcoex_period_timer(unsigned long data)
{
	struct ath_softc *sc = (struct ath_softc *) data;
	struct ath_btcoex_info *btinfo = &sc->btcoex_info;

	ath_detect_bt_priority(sc);

	spin_lock_bh(&btinfo->btcoex_lock);

	ath_btcoex_bt_stomp(sc, btinfo, btinfo->bt_stomp_type);

	spin_unlock_bh(&btinfo->btcoex_lock);

	if (btinfo->btcoex_period != btinfo->btcoex_no_stomp) {
		if (btinfo->hw_timer_enabled)
			ath_gen_timer_stop(sc->sc_ah, btinfo->no_stomp_timer);

		ath_gen_timer_start(sc->sc_ah,
			btinfo->no_stomp_timer,
			(ath9k_hw_gettsf32(sc->sc_ah) +
				btinfo->btcoex_no_stomp),
				btinfo->btcoex_no_stomp * 10);
		btinfo->hw_timer_enabled = true;
	}

	mod_timer(&btinfo->period_timer, jiffies +
				  msecs_to_jiffies(ATH_BTCOEX_DEF_BT_PERIOD));
}

/*
 * Generic tsf based hw timer which configures weight
 * registers to time slice between wlan and bt traffic
 */

static void ath_btcoex_no_stomp_timer(void *arg)
{
	struct ath_softc *sc = (struct ath_softc *)arg;
	struct ath_btcoex_info *btinfo = &sc->btcoex_info;

	DPRINTF(sc, ATH_DBG_BTCOEX, "no stomp timer running \n");

	spin_lock_bh(&btinfo->btcoex_lock);

	if (btinfo->bt_stomp_type == ATH_BTCOEX_STOMP_LOW)
		ath_btcoex_bt_stomp(sc, btinfo, ATH_BTCOEX_STOMP_NONE);
	 else if (btinfo->bt_stomp_type == ATH_BTCOEX_STOMP_ALL)
		ath_btcoex_bt_stomp(sc, btinfo, ATH_BTCOEX_STOMP_LOW);

	spin_unlock_bh(&btinfo->btcoex_lock);
}

static int ath_init_btcoex_info(struct ath_hw *hw,
				struct ath_btcoex_info *btcoex_info)
{
	u32 i;
	int qnum;

	qnum = ath_tx_get_qnum(hw->ah_sc, ATH9K_TX_QUEUE_DATA, ATH9K_WME_AC_BE);

	btcoex_info->bt_coex_mode =
		(btcoex_info->bt_coex_mode & AR_BT_QCU_THRESH) |
		SM(ath_bt_config.bt_time_extend, AR_BT_TIME_EXTEND) |
		SM(ath_bt_config.bt_txstate_extend, AR_BT_TXSTATE_EXTEND) |
		SM(ath_bt_config.bt_txframe_extend, AR_BT_TX_FRAME_EXTEND) |
		SM(ath_bt_config.bt_mode, AR_BT_MODE) |
		SM(ath_bt_config.bt_quiet_collision, AR_BT_QUIET) |
		SM(ath_bt_config.bt_rxclear_polarity, AR_BT_RX_CLEAR_POLARITY) |
		SM(ath_bt_config.bt_priority_time, AR_BT_PRIORITY_TIME) |
		SM(ath_bt_config.bt_first_slot_time, AR_BT_FIRST_SLOT_TIME) |
		SM(qnum, AR_BT_QCU_THRESH);

	btcoex_info->bt_coex_mode2 =
		SM(ath_bt_config.bt_hold_rx_clear, AR_BT_HOLD_RX_CLEAR) |
		SM(ATH_BTCOEX_BMISS_THRESH, AR_BT_BCN_MISS_THRESH) |
		AR_BT_DISABLE_BT_ANT;

	btcoex_info->bt_stomp_type = ATH_BTCOEX_STOMP_LOW;

	btcoex_info->btcoex_period = ATH_BTCOEX_DEF_BT_PERIOD * 1000;

	btcoex_info->btcoex_no_stomp = (100 - ATH_BTCOEX_DEF_DUTY_CYCLE) *
		btcoex_info->btcoex_period / 100;

	for (i = 0; i < 32; i++)
		hw->hw_gen_timers.gen_timer_index[(debruijn32 << i) >> 27] = i;

	setup_timer(&btcoex_info->period_timer, ath_btcoex_period_timer,
			(unsigned long) hw->ah_sc);

	btcoex_info->no_stomp_timer = ath_gen_timer_alloc(hw,
			ath_btcoex_no_stomp_timer,
			ath_btcoex_no_stomp_timer,
			(void *)hw->ah_sc, AR_FIRST_NDP_TIMER);

	if (btcoex_info->no_stomp_timer == NULL)
		return -ENOMEM;

	spin_lock_init(&btcoex_info->btcoex_lock);

	return 0;
}

int ath9k_hw_btcoex_init(struct ath_hw *ah)
{
	struct ath_btcoex_info *btcoex_info = &ah->ah_sc->btcoex_info;
	int ret = 0;

	if (btcoex_info->btcoex_scheme == ATH_BTCOEX_CFG_2WIRE) {
		/* connect bt_active to baseband */
		REG_CLR_BIT(ah, AR_GPIO_INPUT_EN_VAL,
				(AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_DEF |
				 AR_GPIO_INPUT_EN_VAL_BT_FREQUENCY_DEF));

		REG_SET_BIT(ah, AR_GPIO_INPUT_EN_VAL,
				AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_BB);

		/* Set input mux for bt_active to gpio pin */
		REG_RMW_FIELD(ah, AR_GPIO_INPUT_MUX1,
				AR_GPIO_INPUT_MUX1_BT_ACTIVE,
				btcoex_info->btactive_gpio);

		/* Configure the desired gpio port for input */
		ath9k_hw_cfg_gpio_input(ah, btcoex_info->btactive_gpio);
	} else {
		/* btcoex 3-wire */
		REG_SET_BIT(ah, AR_GPIO_INPUT_EN_VAL,
				(AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_BB |
				 AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_BB));

		/* Set input mux for bt_prority_async and
		 *                  bt_active_async to GPIO pins */
		REG_RMW_FIELD(ah, AR_GPIO_INPUT_MUX1,
				AR_GPIO_INPUT_MUX1_BT_ACTIVE,
				btcoex_info->btactive_gpio);

		REG_RMW_FIELD(ah, AR_GPIO_INPUT_MUX1,
				AR_GPIO_INPUT_MUX1_BT_PRIORITY,
				btcoex_info->btpriority_gpio);

		/* Configure the desired GPIO ports for input */

		ath9k_hw_cfg_gpio_input(ah, btcoex_info->btactive_gpio);
		ath9k_hw_cfg_gpio_input(ah, btcoex_info->btpriority_gpio);

		ret = ath_init_btcoex_info(ah, btcoex_info);
	}

	return ret;
}

void ath9k_hw_btcoex_enable(struct ath_hw *ah)
{
	struct ath_btcoex_info *btcoex_info = &ah->ah_sc->btcoex_info;

	if (btcoex_info->btcoex_scheme == ATH_BTCOEX_CFG_2WIRE) {
		/* Configure the desired GPIO port for TX_FRAME output */
		ath9k_hw_cfg_output(ah, btcoex_info->wlanactive_gpio,
				AR_GPIO_OUTPUT_MUX_AS_TX_FRAME);
	} else {
		/*
		 * Program coex mode and weight registers to
		 * enable coex 3-wire
		 */
		REG_WRITE(ah, AR_BT_COEX_MODE, btcoex_info->bt_coex_mode);
		REG_WRITE(ah, AR_BT_COEX_WEIGHT, btcoex_info->bt_coex_weights);
		REG_WRITE(ah, AR_BT_COEX_MODE2, btcoex_info->bt_coex_mode2);

		REG_RMW_FIELD(ah, AR_QUIET1,
				AR_QUIET1_QUIET_ACK_CTS_ENABLE, 1);
		REG_RMW_FIELD(ah, AR_PCU_MISC,
				AR_PCU_BT_ANT_PREVENT_RX, 0);

		ath9k_hw_cfg_output(ah, btcoex_info->wlanactive_gpio,
				AR_GPIO_OUTPUT_MUX_AS_RX_CLEAR_EXTERNAL);
	}

	REG_RMW(ah, AR_GPIO_PDPU,
		(0x2 << (btcoex_info->btactive_gpio * 2)),
		(0x3 << (btcoex_info->btactive_gpio * 2)));

	ah->ah_sc->sc_flags |= SC_OP_BTCOEX_ENABLED;
}

void ath9k_hw_btcoex_disable(struct ath_hw *ah)
{
	struct ath_btcoex_info *btcoex_info = &ah->ah_sc->btcoex_info;

	ath9k_hw_set_gpio(ah, btcoex_info->wlanactive_gpio, 0);

	ath9k_hw_cfg_output(ah, btcoex_info->wlanactive_gpio,
			AR_GPIO_OUTPUT_MUX_AS_OUTPUT);

	if (btcoex_info->btcoex_scheme == ATH_BTCOEX_CFG_3WIRE) {
		REG_WRITE(ah, AR_BT_COEX_MODE, AR_BT_QUIET | AR_BT_MODE);
		REG_WRITE(ah, AR_BT_COEX_WEIGHT, 0);
		REG_WRITE(ah, AR_BT_COEX_MODE2, 0);
	}

	ah->ah_sc->sc_flags &= ~SC_OP_BTCOEX_ENABLED;
}

/*
 * Pause btcoex timer and bt duty cycle timer
 */
void ath_btcoex_timer_pause(struct ath_softc *sc,
			    struct ath_btcoex_info *btinfo)
{

	del_timer_sync(&btinfo->period_timer);

	if (btinfo->hw_timer_enabled)
		ath_gen_timer_stop(sc->sc_ah, btinfo->no_stomp_timer);

	btinfo->hw_timer_enabled = false;
}

/*
 * (Re)start btcoex timers
 */
void ath_btcoex_timer_resume(struct ath_softc *sc,
			     struct ath_btcoex_info *btinfo)
{

	DPRINTF(sc, ATH_DBG_BTCOEX, "Starting btcoex timers");

	/* make sure duty cycle timer is also stopped when resuming */
	if (btinfo->hw_timer_enabled)
		ath_gen_timer_stop(sc->sc_ah, btinfo->no_stomp_timer);

	btinfo->bt_priority_cnt = 0;
	btinfo->bt_priority_time = jiffies;
	sc->sc_flags &= ~SC_OP_BT_PRIORITY_DETECTED;

	mod_timer(&btinfo->period_timer, jiffies);
}
