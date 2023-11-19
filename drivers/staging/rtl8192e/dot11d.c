// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 ******************************************************************************/
#include "dot11d.h"

struct channel_list {
	u8      channel[32];
	u8      len;
};

static struct channel_list channel_array = {{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13};

void dot11d_channel_map(struct rtllib_device *ieee)
{
	int i;

	memset(GET_DOT11D_INFO(ieee)->channel_map, 0,
	       sizeof(GET_DOT11D_INFO(ieee)->channel_map));
	for (i = 0; i < channel_array.len; i++)
		GET_DOT11D_INFO(ieee)->channel_map[channel_array.channel[i]] = 1;

	for (i = 12; i <= 13; i++)
		GET_DOT11D_INFO(ieee)->channel_map[i] = 2;
	ieee->bss_start_channel = 10;
}
EXPORT_SYMBOL(dot11d_channel_map);
