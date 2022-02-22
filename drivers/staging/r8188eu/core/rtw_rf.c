// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _RTW_RF_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/recv_osdep.h"
#include "../include/xmit_osdep.h"

struct ch_freq {
	u32 channel;
	u32 frequency;
};

static struct ch_freq ch_freq_map[] = {
	{1, 2412}, {2, 2417}, {3, 2422}, {4, 2427}, {5, 2432},
	{6, 2437}, {7, 2442}, {8, 2447}, {9, 2452}, {10, 2457},
	{11, 2462}, {12, 2467}, {13, 2472}, {14, 2484},
};

static int ch_freq_map_num = ARRAY_SIZE(ch_freq_map);

u32 rtw_ch2freq(u32 channel)
{
	u8	i;
	u32	freq = 0;

	for (i = 0; i < ch_freq_map_num; i++) {
		if (channel == ch_freq_map[i].channel) {
			freq = ch_freq_map[i].frequency;
				break;
		}
	}
	if (i == ch_freq_map_num)
		freq = 2412;

	return freq;
}
