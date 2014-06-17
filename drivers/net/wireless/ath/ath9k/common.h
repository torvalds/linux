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

#include "common-init.h"
#include "common-beacon.h"
#include "common-debug.h"

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

#define IEEE80211_MS_TO_TU(x)   (((x) * 1000) / 1024)

struct ath_beacon_config {
	int beacon_interval;
	u16 dtim_period;
	u16 bmiss_timeout;
	u8 dtim_count;
	bool enable_beacon;
	bool ibss_creator;
	u32 nexttbtt;
	u32 intval;
};

bool ath9k_cmn_rx_accept(struct ath_common *common,
			 struct ieee80211_hdr *hdr,
			 struct ieee80211_rx_status *rxs,
			 struct ath_rx_status *rx_stats,
			 bool *decrypt_error,
			 unsigned int rxfilter);
void ath9k_cmn_rx_skb_postprocess(struct ath_common *common,
				  struct sk_buff *skb,
				  struct ath_rx_status *rx_stats,
				  struct ieee80211_rx_status *rxs,
				  bool decrypt_error);
int ath9k_cmn_process_rate(struct ath_common *common,
			   struct ieee80211_hw *hw,
			   struct ath_rx_status *rx_stats,
			   struct ieee80211_rx_status *rxs);
void ath9k_cmn_process_rssi(struct ath_common *common,
			    struct ieee80211_hw *hw,
			    struct ath_rx_status *rx_stats,
			    struct ieee80211_rx_status *rxs);
int ath9k_cmn_get_hw_crypto_keytype(struct sk_buff *skb);
struct ath9k_channel *ath9k_cmn_get_channel(struct ieee80211_hw *hw,
					    struct ath_hw *ah,
					    struct cfg80211_chan_def *chandef);
int ath9k_cmn_count_streams(unsigned int chainmask, int max);
void ath9k_cmn_btcoex_bt_stomp(struct ath_common *common,
				  enum ath_stomp_type stomp_type);
void ath9k_cmn_update_txpow(struct ath_hw *ah, u16 cur_txpow,
			    u16 new_txpow, u16 *txpower);
void ath9k_cmn_init_crypto(struct ath_hw *ah);
