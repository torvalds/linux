/*
 * Copyright (c) 2009-2011 Atheros Communications Inc.
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

#include <net/mac80211.h>

#include "../ath.h"

#include "hw.h"
#include "hw-ops.h"

/* Common header for Atheros 802.11n base driver cores */

#define WME_BA_BMP_SIZE         64
#define WME_MAX_BA              WME_BA_BMP_SIZE
#define ATH_TID_MAX_BUFS        (2 * WME_MAX_BA)

#define ATH_RSSI_DUMMY_MARKER   127
#define ATH_RSSI_LPF_LEN 		10
#define RSSI_LPF_THRESHOLD		-20
#define ATH_RSSI_EP_MULTIPLIER     (1<<7)
#define ATH_EP_MUL(x, mul)         ((x) * (mul))
#define ATH_RSSI_IN(x)             (ATH_EP_MUL((x), ATH_RSSI_EP_MULTIPLIER))
#define ATH_LPF_RSSI(x, y, len) \
    ((x != ATH_RSSI_DUMMY_MARKER) ? (((x) * ((len) - 1) + (y)) / (len)) : (y))
#define ATH_RSSI_LPF(x, y) do {                     			\
    if ((y) >= RSSI_LPF_THRESHOLD)                         		\
	x = ATH_LPF_RSSI((x), ATH_RSSI_IN((y)), ATH_RSSI_LPF_LEN);  	\
} while (0)
#define ATH_EP_RND(x, mul) 						\
	(((x) + ((mul)/2)) / (mul))

int ath9k_cmn_get_hw_crypto_keytype(struct sk_buff *skb);
void ath9k_cmn_update_ichannel(struct ath9k_channel *ichan,
			       struct ieee80211_channel *chan,
			       enum nl80211_channel_type channel_type);
struct ath9k_channel *ath9k_cmn_get_curchannel(struct ieee80211_hw *hw,
					       struct ath_hw *ah);
int ath9k_cmn_count_streams(unsigned int chainmask, int max);
void ath9k_cmn_btcoex_bt_stomp(struct ath_common *common,
				  enum ath_stomp_type stomp_type);
void ath9k_cmn_update_txpow(struct ath_hw *ah, u16 cur_txpow,
			    u16 new_txpow, u16 *txpower);
void ath9k_cmn_init_crypto(struct ath_hw *ah);
