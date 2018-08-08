/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __INC_DOT11D_H
#define __INC_DOT11D_H

#include "ieee80211.h"

struct chnl_txpower_triple {
	u8  first_channel;
	u8  num_channels;
	u8  max_tx_pwr_dbm;
};

enum dot11d_state {
	DOT11D_STATE_NONE = 0,
	DOT11D_STATE_LEARNED,
	DOT11D_STATE_DONE,
};

struct rt_dot11d_info {
	u16 country_ie_len; /* > 0 if country_ie_buf[] contains valid country information element. */

	/*  country_ie_src_addr u16 aligned for comparison and copy */
	u8  country_ie_src_addr[ETH_ALEN]; /* Source AP of the country IE. */
	u8  country_ie_buf[MAX_IE_LEN];
	u8  country_ie_watchdog;

	u8  channel_map[MAX_CHANNEL_NUMBER + 1];  /* !Value 0: Invalid, 1: Valid (active scan), 2: Valid (passive scan) */
	u8  max_tx_pwr_dbm_list[MAX_CHANNEL_NUMBER + 1];

	enum dot11d_state state;
	bool enabled; /* dot11MultiDomainCapabilityEnabled */
};

#define GET_DOT11D_INFO(ieee_dev) ((struct rt_dot11d_info *)((ieee_dev)->pDot11dInfo))

#define IS_DOT11D_ENABLE(ieee_dev) (GET_DOT11D_INFO(ieee_dev)->enabled)
#define IS_COUNTRY_IE_VALID(ieee_dev) (GET_DOT11D_INFO(ieee_dev)->country_ie_len > 0)

#define IS_EQUAL_CIE_SRC(ieee_dev, __pTa) ether_addr_equal(GET_DOT11D_INFO(ieee_dev)->country_ie_src_addr, __pTa)
#define UPDATE_CIE_SRC(ieee_dev, __pTa) ether_addr_copy(GET_DOT11D_INFO(ieee_dev)->country_ie_src_addr, __pTa)

#define GET_CIE_WATCHDOG(ieee_dev) (GET_DOT11D_INFO(ieee_dev)->country_ie_watchdog)
#define RESET_CIE_WATCHDOG(ieee_dev) (GET_CIE_WATCHDOG(ieee_dev) = 0)
#define UPDATE_CIE_WATCHDOG(ieee_dev) (++GET_CIE_WATCHDOG(ieee_dev))

void
Dot11d_Init(
	struct ieee80211_device *dev
	);

void
Dot11d_Reset(
	struct ieee80211_device *dev
	);

void
Dot11d_UpdateCountryIe(
	struct ieee80211_device *dev,
	u8 *pTaddr,
	u16 CoutryIeLen,
	u8 *pCoutryIe
	);

u8
DOT11D_GetMaxTxPwrInDbm(
	struct ieee80211_device *dev,
	u8 Channel
	);

void
DOT11D_ScanComplete(
	struct ieee80211_device *dev
	);

int IsLegalChannel(
	struct ieee80211_device *dev,
	u8 channel
);

int ToLegalChannel(
	struct ieee80211_device *dev,
	u8 channel
);
#endif /* #ifndef __INC_DOT11D_H */
