/*
 * Copyright (c) 2004 Sam Leffler, Errno Consulting
 * Copyright (c) 2004 Video54 Technologies, Inc.
 * Copyright (c) 2008-2009 Atheros Communications Inc.
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

#ifndef RC_H
#define RC_H

#include "hw.h"

struct ath_softc;

#define ATH_RATE_MAX     30
#define RATE_TABLE_SIZE  64
#define MAX_TX_RATE_PHY  48

/* VALID_ALL - valid for 20/40/Legacy,
 * VALID - Legacy only,
 * VALID_20 - HT 20 only,
 * VALID_40 - HT 40 only */

#define INVALID    0x0
#define VALID      0x1
#define VALID_20   0x2
#define VALID_40   0x4
#define VALID_2040 (VALID_20|VALID_40)
#define VALID_ALL  (VALID_2040|VALID)

enum {
	WLAN_RC_PHY_OFDM,
	WLAN_RC_PHY_CCK,
	WLAN_RC_PHY_HT_20_SS,
	WLAN_RC_PHY_HT_20_DS,
	WLAN_RC_PHY_HT_40_SS,
	WLAN_RC_PHY_HT_40_DS,
	WLAN_RC_PHY_HT_20_SS_HGI,
	WLAN_RC_PHY_HT_20_DS_HGI,
	WLAN_RC_PHY_HT_40_SS_HGI,
	WLAN_RC_PHY_HT_40_DS_HGI,
	WLAN_RC_PHY_MAX
};

#define WLAN_RC_PHY_DS(_phy)   ((_phy == WLAN_RC_PHY_HT_20_DS)		\
				|| (_phy == WLAN_RC_PHY_HT_40_DS)	\
				|| (_phy == WLAN_RC_PHY_HT_20_DS_HGI)	\
				|| (_phy == WLAN_RC_PHY_HT_40_DS_HGI))
#define WLAN_RC_PHY_20(_phy)   ((_phy == WLAN_RC_PHY_HT_20_SS)		\
				|| (_phy == WLAN_RC_PHY_HT_20_DS)	\
				|| (_phy == WLAN_RC_PHY_HT_20_SS_HGI)	\
				|| (_phy == WLAN_RC_PHY_HT_20_DS_HGI))
#define WLAN_RC_PHY_40(_phy)   ((_phy == WLAN_RC_PHY_HT_40_SS)		\
				|| (_phy == WLAN_RC_PHY_HT_40_DS)	\
				|| (_phy == WLAN_RC_PHY_HT_40_SS_HGI)	\
				|| (_phy == WLAN_RC_PHY_HT_40_DS_HGI))
#define WLAN_RC_PHY_SGI(_phy)  ((_phy == WLAN_RC_PHY_HT_20_SS_HGI)      \
				|| (_phy == WLAN_RC_PHY_HT_20_DS_HGI)   \
				|| (_phy == WLAN_RC_PHY_HT_40_SS_HGI)   \
				|| (_phy == WLAN_RC_PHY_HT_40_DS_HGI))

#define WLAN_RC_PHY_HT(_phy)    (_phy >= WLAN_RC_PHY_HT_20_SS)

#define WLAN_RC_CAP_MODE(capflag) (((capflag & WLAN_RC_HT_FLAG) ?	\
		(capflag & WLAN_RC_40_FLAG) ? VALID_40 : VALID_20 : VALID))

/* Return TRUE if flag supports HT20 && client supports HT20 or
 * return TRUE if flag supports HT40 && client supports HT40.
 * This is used becos some rates overlap between HT20/HT40.
 */
#define WLAN_RC_PHY_HT_VALID(flag, capflag)			\
	(((flag & VALID_20) && !(capflag & WLAN_RC_40_FLAG)) || \
	 ((flag & VALID_40) && (capflag & WLAN_RC_40_FLAG)))

#define WLAN_RC_DS_FLAG         (0x01)
#define WLAN_RC_40_FLAG         (0x02)
#define WLAN_RC_SGI_FLAG        (0x04)
#define WLAN_RC_HT_FLAG         (0x08)

/**
 * struct ath_rate_table - Rate Control table
 * @valid: valid for use in rate control
 * @valid_single_stream: valid for use in rate control for
 * 	single stream operation
 * @phy: CCK/OFDM
 * @ratekbps: rate in Kbits per second
 * @user_ratekbps: user rate in Kbits per second
 * @ratecode: rate that goes into HW descriptors
 * @short_preamble: Mask for enabling short preamble in ratecode for CCK
 * @dot11rate: value that goes into supported
 * 	rates info element of MLME
 * @ctrl_rate: Index of next lower basic rate, used for duration computation
 * @max_4ms_framelen: maximum frame length(bytes) for tx duration
 * @probe_interval: interval for rate control to probe for other rates
 * @rssi_reduce_interval: interval for rate control to reduce rssi
 * @initial_ratemax: initial ratemax value
 */
struct ath_rate_table {
	int rate_cnt;
	int mcs_start;
	struct {
		int valid;
		int valid_single_stream;
		u8 phy;
		u32 ratekbps;
		u32 user_ratekbps;
		u8 ratecode;
		u8 dot11rate;
		u8 ctrl_rate;
		u8 base_index;
		u8 cw40index;
		u8 sgi_index;
		u8 ht_index;
	} info[RATE_TABLE_SIZE];
	u32 probe_interval;
	u8 initial_ratemax;
};

struct ath_rateset {
	u8 rs_nrates;
	u8 rs_rates[ATH_RATE_MAX];
};

/**
 * struct ath_rate_priv - Rate Control priv data
 * @state: RC state
 * @probe_rate: rate we are probing at
 * @probe_time: msec timestamp for last probe
 * @hw_maxretry_pktcnt: num of packets since we got HW max retry error
 * @max_valid_rate: maximum number of valid rate
 * @per_down_time: msec timestamp for last PER down step
 * @valid_phy_ratecnt: valid rate count
 * @rate_max_phy: phy index for the max rate
 * @per: PER for every valid rate in %
 * @probe_interval: interval for ratectrl to probe for other rates
 * @prev_data_rix: rate idx of last data frame
 * @ht_cap: HT capabilities
 * @neg_rates: Negotatied rates
 * @neg_ht_rates: Negotiated HT rates
 */
struct ath_rate_priv {
	u8 rate_table_size;
	u8 probe_rate;
	u8 hw_maxretry_pktcnt;
	u8 max_valid_rate;
	u8 valid_rate_index[RATE_TABLE_SIZE];
	u8 ht_cap;
	u8 valid_phy_ratecnt[WLAN_RC_PHY_MAX];
	u8 valid_phy_rateidx[WLAN_RC_PHY_MAX][RATE_TABLE_SIZE];
	u8 rate_max_phy;
	u8 per[RATE_TABLE_SIZE];
	u32 probe_time;
	u32 per_down_time;
	u32 probe_interval;
	u32 prev_data_rix;
	u32 tx_triglevel_max;
	struct ath_rateset neg_rates;
	struct ath_rateset neg_ht_rates;
	struct ath_rate_softc *asc;
};

#define ATH_TX_INFO_FRAME_TYPE_INTERNAL	(1 << 0)
#define ATH_TX_INFO_FRAME_TYPE_PAUSE	(1 << 1)
#define ATH_TX_INFO_UPDATE_RC		(1 << 2)
#define ATH_TX_INFO_XRETRY		(1 << 3)
#define ATH_TX_INFO_UNDERRUN		(1 << 4)

enum ath9k_internal_frame_type {
	ATH9K_NOT_INTERNAL,
	ATH9K_INT_PAUSE,
	ATH9K_INT_UNPAUSE
};

int ath_rate_control_register(void);
void ath_rate_control_unregister(void);

#endif /* RC_H */
