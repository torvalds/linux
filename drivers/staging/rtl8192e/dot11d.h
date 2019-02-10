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
	u8 first_channel;
	u8  num_channels;
	u8  max_tx_power;
};

enum dot11d_state {
	DOT11D_STATE_NONE = 0,
	DOT11D_STATE_LEARNED,
	DOT11D_STATE_DONE,
};

/**
 * struct rt_dot11d_info * @country_len: value greater than 0 if
 *		  @country_buffer contains valid country information element.
 * @channel_map: holds channel values
 *		0 - invalid,
 *		1 - valid (active scan),
 *		2 - valid (passive scan)
 * @country_src_addr - Source AP of the country IE
 */

struct rt_dot11d_info {
	bool enabled;

	u16 country_len;
	u8  country_buffer[MAX_IE_LEN];
	u8  country_src_addr[6];
	u8  country_watchdog;

	u8  channel_map[MAX_CHANNEL_NUMBER + 1];
	u8  max_tx_power_list[MAX_CHANNEL_NUMBER + 1];

	enum dot11d_state state;
};

static inline void cpMacAddr(unsigned char *des, unsigned char *src)
{
	memcpy(des, src, 6);
}

#define GET_DOT11D_INFO(__pIeeeDev)			\
	 ((struct rt_dot11d_info *)((__pIeeeDev)->dot11d_info))

#define IS_DOT11D_ENABLE(__pIeeeDev)			\
	 (GET_DOT11D_INFO(__pIeeeDev)->enabled)
#define IS_COUNTRY_IE_VALID(__pIeeeDev)			\
	(GET_DOT11D_INFO(__pIeeeDev)->country_len > 0)

#define IS_EQUAL_CIE_SRC(__pIeeeDev, __pTa)		\
	 ether_addr_equal_unaligned( \
		GET_DOT11D_INFO(__pIeeeDev)->country_src_addr, __pTa)
#define UPDATE_CIE_SRC(__pIeeeDev, __pTa)		\
	cpMacAddr(GET_DOT11D_INFO(__pIeeeDev)->country_src_addr, __pTa)

#define GET_CIE_WATCHDOG(__pIeeeDev)				\
	 (GET_DOT11D_INFO(__pIeeeDev)->country_watchdog)
static inline void RESET_CIE_WATCHDOG(struct rtllib_device *__pIeeeDev)
{
	GET_CIE_WATCHDOG(__pIeeeDev) = 0;
}

#define UPDATE_CIE_WATCHDOG(__pIeeeDev) (++GET_CIE_WATCHDOG(__pIeeeDev))

void dot11d_init(struct rtllib_device *dev);
void dot11d_channel_map(u8 channel_plan, struct rtllib_device *ieee);
void dot11d_reset(struct rtllib_device *dev);
void Dot11d_UpdateCountryIe(struct rtllib_device *dev, u8 *pTaddr,
			    u16 CoutryIeLen, u8 *pCoutryIe);
void DOT11D_ScanComplete(struct rtllib_device *dev);

#endif
