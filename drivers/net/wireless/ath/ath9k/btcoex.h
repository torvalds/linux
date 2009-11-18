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

#ifndef BTCOEX_H
#define BTCOEX_H

#define ATH_WLANACTIVE_GPIO	5
#define ATH_BTACTIVE_GPIO	6
#define ATH_BTPRIORITY_GPIO	7

#define ATH_BTCOEX_DEF_BT_PERIOD  45
#define ATH_BTCOEX_DEF_DUTY_CYCLE 55
#define ATH_BTCOEX_BMISS_THRESH   50

#define ATH_BT_PRIORITY_TIME_THRESHOLD 1000 /* ms */
#define ATH_BT_CNT_THRESHOLD	       3

enum ath_btcoex_scheme {
	ATH_BTCOEX_CFG_NONE,
	ATH_BTCOEX_CFG_2WIRE,
	ATH_BTCOEX_CFG_3WIRE,
};

enum ath_stomp_type {
	ATH_BTCOEX_NO_STOMP,
	ATH_BTCOEX_STOMP_ALL,
	ATH_BTCOEX_STOMP_LOW,
	ATH_BTCOEX_STOMP_NONE
};

enum ath_bt_mode {
	ATH_BT_COEX_MODE_LEGACY,	/* legacy rx_clear mode */
	ATH_BT_COEX_MODE_UNSLOTTED,	/* untimed/unslotted mode */
	ATH_BT_COEX_MODE_SLOTTED,	/* slotted mode */
	ATH_BT_COEX_MODE_DISALBED,	/* coexistence disabled */
};

struct ath_btcoex_config {
	u8 bt_time_extend;
	bool bt_txstate_extend;
	bool bt_txframe_extend;
	enum ath_bt_mode bt_mode; /* coexistence mode */
	bool bt_quiet_collision;
	bool bt_rxclear_polarity; /* invert rx_clear as WLAN_ACTIVE*/
	u8 bt_priority_time;
	u8 bt_first_slot_time;
	bool bt_hold_rx_clear;
};

struct ath_btcoex_info {
	enum ath_btcoex_scheme btcoex_scheme;
	u8 wlanactive_gpio;
	u8 btactive_gpio;
	u8 btpriority_gpio;
	u8 bt_duty_cycle; 	/* BT duty cycle in percentage */
	int bt_stomp_type; 	/* Types of BT stomping */
	u32 bt_coex_mode; 	/* Register setting for AR_BT_COEX_MODE */
	u32 bt_coex_weights; 	/* Register setting for AR_BT_COEX_WEIGHT */
	u32 bt_coex_mode2; 	/* Register setting for AR_BT_COEX_MODE2 */
	u32 btcoex_no_stomp;   /* in usec */
	u32 btcoex_period;     	/* in usec */
	u32 bt_priority_cnt;
	unsigned long bt_priority_time;
	bool hw_timer_enabled;
	spinlock_t btcoex_lock;
	struct timer_list period_timer;      /* Timer for BT period */
	struct ath_gen_timer *no_stomp_timer; /*Timer for no BT stomping*/
};

bool ath_btcoex_supported(u16 subsysid);
int ath9k_hw_btcoex_init(struct ath_hw *ah);
void ath9k_hw_btcoex_enable(struct ath_hw *ah);
void ath9k_hw_btcoex_disable(struct ath_hw *ah);
void ath_btcoex_timer_resume(struct ath_softc *sc,
			     struct ath_btcoex_info *btinfo);
void ath_btcoex_timer_pause(struct ath_softc *sc,
			    struct ath_btcoex_info *btinfo);

static inline void ath_btcoex_set_weight(struct ath_btcoex_info *btcoex_info,
					 u32 bt_weight,
					 u32 wlan_weight)
{
	btcoex_info->bt_coex_weights = SM(bt_weight, AR_BTCOEX_BT_WGHT) |
				       SM(wlan_weight, AR_BTCOEX_WL_WGHT);
}

#endif
