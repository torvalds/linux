/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 ******************************************************************************/
#ifndef __INC_DOT11D_H
#define __INC_DOT11D_H

#include "rtllib.h"

struct chnl_txpow_triple {
	u8 FirstChnl;
	u8  NumChnls;
	u8  MaxTxPowerInDbm;
};

enum dot11d_state {
	DOT11D_STATE_NONE = 0,
	DOT11D_STATE_LEARNED,
	DOT11D_STATE_DONE,
};

/**
 * struct rt_dot11d_info * @CountryIeLen: value greater than 0 if @CountryIeBuf contains
 *		  valid country information element.
 * @channel_map: holds channel values
 *		0 - invalid,
 *		1 - valid (active scan),
 *		2 - valid (passive scan)
 * @CountryIeSrcAddr - Source AP of the country IE
 */

struct rt_dot11d_info {
	bool bEnabled;

	u16 CountryIeLen;
	u8  CountryIeBuf[MAX_IE_LEN];
	u8  CountryIeSrcAddr[6];
	u8  CountryIeWatchdog;

	u8  channel_map[MAX_CHANNEL_NUMBER + 1];
	u8  MaxTxPwrDbmList[MAX_CHANNEL_NUMBER + 1];

	enum dot11d_state State;
};

static inline void cpMacAddr(unsigned char *des, unsigned char *src)
{
	memcpy(des, src, 6);
}

#define GET_DOT11D_INFO(__pIeeeDev)			\
	 ((struct rt_dot11d_info *)((__pIeeeDev)->pDot11dInfo))

#define IS_DOT11D_ENABLE(__pIeeeDev)			\
	 (GET_DOT11D_INFO(__pIeeeDev)->bEnabled)
#define IS_COUNTRY_IE_VALID(__pIeeeDev)			\
	(GET_DOT11D_INFO(__pIeeeDev)->CountryIeLen > 0)

#define IS_EQUAL_CIE_SRC(__pIeeeDev, __pTa)		\
	 ether_addr_equal_unaligned( \
		GET_DOT11D_INFO(__pIeeeDev)->CountryIeSrcAddr, __pTa)
#define UPDATE_CIE_SRC(__pIeeeDev, __pTa)		\
	cpMacAddr(GET_DOT11D_INFO(__pIeeeDev)->CountryIeSrcAddr, __pTa)

#define GET_CIE_WATCHDOG(__pIeeeDev)				\
	 (GET_DOT11D_INFO(__pIeeeDev)->CountryIeWatchdog)
static inline void RESET_CIE_WATCHDOG(struct rtllib_device *__pIeeeDev)
{
	GET_CIE_WATCHDOG(__pIeeeDev) = 0;
}

#define UPDATE_CIE_WATCHDOG(__pIeeeDev) (++GET_CIE_WATCHDOG(__pIeeeDev))

void dot11d_init(struct rtllib_device *dev);
void Dot11d_Channelmap(u8 channel_plan, struct rtllib_device *ieee);
void Dot11d_Reset(struct rtllib_device *dev);
void Dot11d_UpdateCountryIe(struct rtllib_device *dev, u8 *pTaddr,
			    u16 CoutryIeLen, u8 *pCoutryIe);
void DOT11D_ScanComplete(struct rtllib_device *dev);

#endif
