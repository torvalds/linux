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

void ath9k_hw_btcoex_init(struct ath_hw *ah)
{
	struct ath_btcoex_info *btcoex_info = &ah->ah_sc->btcoex_info;

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
}

void ath9k_hw_btcoex_enable(struct ath_hw *ah)
{
	struct ath_btcoex_info *btcoex_info = &ah->ah_sc->btcoex_info;

	/* Configure the desired GPIO port for TX_FRAME output */
	ath9k_hw_cfg_output(ah, btcoex_info->wlanactive_gpio,
			    AR_GPIO_OUTPUT_MUX_AS_TX_FRAME);

	ah->ah_sc->sc_flags |= SC_OP_BTCOEX_ENABLED;
}

void ath9k_hw_btcoex_disable(struct ath_hw *ah)
{
	struct ath_btcoex_info *btcoex_info = &ah->ah_sc->btcoex_info;

	ath9k_hw_set_gpio(ah, btcoex_info->wlanactive_gpio, 0);

	ath9k_hw_cfg_output(ah, btcoex_info->wlanactive_gpio,
			AR_GPIO_OUTPUT_MUX_AS_OUTPUT);

	ah->ah_sc->sc_flags &= ~SC_OP_BTCOEX_ENABLED;
}
