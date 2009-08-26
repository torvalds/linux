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

enum ath_btcoex_scheme {
	ATH_BTCOEX_CFG_NONE,
	ATH_BTCOEX_CFG_2WIRE,
	ATH_BTCOEX_CFG_3WIRE,
};

struct ath_btcoex_info {
	enum ath_btcoex_scheme btcoex_scheme;
	u8 wlanactive_gpio;
	u8 btactive_gpio;
};

void ath9k_hw_btcoex_init(struct ath_hw *ah);
void ath9k_hw_btcoex_enable(struct ath_hw *ah);
void ath9k_hw_btcoex_disable(struct ath_hw *ah);

#endif
