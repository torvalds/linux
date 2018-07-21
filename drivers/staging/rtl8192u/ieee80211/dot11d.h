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
	/* DECLARE_RT_OBJECT(rt_dot11d_info); */

	bool enabled; /* dot11MultiDomainCapabilityEnabled */

	u16 country_ie_len; /* > 0 if CountryIeBuf[] contains valid country information element. */
	u8  CountryIeBuf[MAX_IE_LEN];
	u8  CountryIeSrcAddr[6]; /* Source AP of the country IE. */
	u8  CountryIeWatchdog;

	u8  channel_map[MAX_CHANNEL_NUMBER+1];  /* !Value 0: Invalid, 1: Valid (active scan), 2: Valid (passive scan) */
	u8  MaxTxPwrDbmList[MAX_CHANNEL_NUMBER+1];

	enum dot11d_state State;
};

#define eqMacAddr(a, b)		(((a)[0] == (b)[0] &&		    \
	(a)[1] == (b)[1] && (a)[2] == (b)[2] && (a)[3] == (b)[3] && \
	(a)[4] == (b)[4] && (a)[5] == (b)[5]) ? 1 : 0)
#define cpMacAddr(des, src)	      ((des)[0] = (src)[0], \
	(des)[1] = (src)[1], (des)[2] = (src)[2], \
	(des)[3] = (src)[3], (des)[4] = (src)[4], \
	(des)[5] = (src)[5])
#define GET_DOT11D_INFO(__pIeeeDev) ((struct rt_dot11d_info *)((__pIeeeDev)->pDot11dInfo))

#define IS_DOT11D_ENABLE(__pIeeeDev) (GET_DOT11D_INFO(__pIeeeDev)->enabled)
#define IS_COUNTRY_IE_VALID(__pIeeeDev) (GET_DOT11D_INFO(__pIeeeDev)->country_ie_len > 0)

#define IS_EQUAL_CIE_SRC(__pIeeeDev, __pTa) eqMacAddr(GET_DOT11D_INFO(__pIeeeDev)->CountryIeSrcAddr, __pTa)
#define UPDATE_CIE_SRC(__pIeeeDev, __pTa) cpMacAddr(GET_DOT11D_INFO(__pIeeeDev)->CountryIeSrcAddr, __pTa)

#define IS_COUNTRY_IE_CHANGED(__pIeeeDev, __Ie) \
	(((__Ie).Length == 0 || (__Ie).Length != GET_DOT11D_INFO(__pIeeeDev)->country_ie_len) ? \
	FALSE : \
	(!memcmp(GET_DOT11D_INFO(__pIeeeDev)->CountryIeBuf, (__Ie).Octet, (__Ie).Length)))

#define CIE_WATCHDOG_TH 1
#define GET_CIE_WATCHDOG(__pIeeeDev) (GET_DOT11D_INFO(__pIeeeDev)->CountryIeWatchdog)
#define RESET_CIE_WATCHDOG(__pIeeeDev) (GET_CIE_WATCHDOG(__pIeeeDev) = 0)
#define UPDATE_CIE_WATCHDOG(__pIeeeDev) (++GET_CIE_WATCHDOG(__pIeeeDev))

#define IS_DOT11D_STATE_DONE(__pIeeeDev) (GET_DOT11D_INFO(__pIeeeDev)->State == DOT11D_STATE_DONE)

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
