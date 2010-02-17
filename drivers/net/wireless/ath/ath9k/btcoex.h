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

#include "hw.h"

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

struct ath_btcoex_hw {
	enum ath_btcoex_scheme scheme;
	bool enabled;
	u8 wlanactive_gpio;
	u8 btactive_gpio;
	u8 btpriority_gpio;
	u32 bt_coex_mode; 	/* Register setting for AR_BT_COEX_MODE */
	u32 bt_coex_weights; 	/* Register setting for AR_BT_COEX_WEIGHT */
	u32 bt_coex_mode2; 	/* Register setting for AR_BT_COEX_MODE2 */
};

bool ath9k_hw_btcoex_supported(struct ath_hw *ah);
void ath9k_hw_btcoex_init_2wire(struct ath_hw *ah);
void ath9k_hw_btcoex_init_3wire(struct ath_hw *ah);
void ath9k_hw_init_btcoex_hw(struct ath_hw *ah, int qnum);
void ath9k_hw_btcoex_set_weight(struct ath_hw *ah,
				u32 bt_weight,
				u32 wlan_weight);
void ath9k_hw_btcoex_enable(struct ath_hw *ah);
void ath9k_hw_btcoex_disable(struct ath_hw *ah);

#endif
