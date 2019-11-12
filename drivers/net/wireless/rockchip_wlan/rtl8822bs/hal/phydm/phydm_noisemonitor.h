/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
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
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef __ODMNOISEMONITOR_H__
#define __ODMNOISEMONITOR_H__

#define VALID_CNT 5

struct noise_level {
	u8 value[PHYDM_MAX_RF_PATH];
	s8 sval[PHYDM_MAX_RF_PATH];
	s32 sum[PHYDM_MAX_RF_PATH];
	u8 valid[PHYDM_MAX_RF_PATH];
	u8 valid_cnt[PHYDM_MAX_RF_PATH];
};

struct odm_noise_monitor {
	s8 noise[PHYDM_MAX_RF_PATH];
	s16 noise_all;
};

s16 odm_inband_noise_monitor(void *dm_void, u8 is_pause_dig, u8 igi_value,
			     u32 max_time);

void phydm_noisy_detection(void *dm_void);

#endif
