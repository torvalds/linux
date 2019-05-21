/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
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

static inline void copy_mac_addr(unsigned char *des, unsigned char *src)
{
	memcpy(des, src, 6);
}

#define GET_DOT11D_INFO(__ieee_dev)			\
	 ((struct rt_dot11d_info *)((__ieee_dev)->dot11d_info))

#define IS_DOT11D_ENABLE(__ieee_dev)			\
	 (GET_DOT11D_INFO(__ieee_dev)->enabled)
#define IS_COUNTRY_IE_VALID(__ieee_dev)			\
	(GET_DOT11D_INFO(__ieee_dev)->country_len > 0)

#define IS_EQUAL_CIE_SRC(__ieee_dev, __address)		\
	 ether_addr_equal_unaligned( \
		GET_DOT11D_INFO(__ieee_dev)->country_src_addr, __address)
#define UPDATE_CIE_SRC(__ieee_dev, __address)		\
	copy_mac_addr(GET_DOT11D_INFO(__ieee_dev)->country_src_addr, __address)

#define GET_CIE_WATCHDOG(__ieee_dev)				\
	 (GET_DOT11D_INFO(__ieee_dev)->country_watchdog)
static inline void RESET_CIE_WATCHDOG(struct rtllib_device *__ieee_dev)
{
	GET_CIE_WATCHDOG(__ieee_dev) = 0;
}

#define UPDATE_CIE_WATCHDOG(__ieee_dev) (++GET_CIE_WATCHDOG(__ieee_dev))

void dot11d_init(struct rtllib_device *dev);
void dot11d_channel_map(u8 channel_plan, struct rtllib_device *ieee);
void dot11d_reset(struct rtllib_device *dev);
void dot11d_update_country(struct rtllib_device *dev, u8 *address,
			   u16 country_len, u8 *country);
void dot11d_scan_complete(struct rtllib_device *dev);

#endif
