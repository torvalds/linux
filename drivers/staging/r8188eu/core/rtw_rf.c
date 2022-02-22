// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _RTW_RF_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/recv_osdep.h"
#include "../include/xmit_osdep.h"

static const u32 ch_freq_map[] = {
	2412,
	2417,
	2422,
	2427,
	2432,
	2437,
	2442,
	2447,
	2452,
	2457,
	2462,
	2467,
	2472,
	2484
};

u32 rtw_ch2freq(u32 channel)
{
	if (channel == 0 || channel > ARRAY_SIZE(ch_freq_map))
		return 2412;

	return ch_freq_map[channel - 1];
}
