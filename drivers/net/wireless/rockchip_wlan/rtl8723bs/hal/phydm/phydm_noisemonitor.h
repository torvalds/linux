/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#ifndef	__ODMNOISEMONITOR_H__
#define __ODMNOISEMONITOR_H__

#define	ODM_MAX_CHANNEL_NUM					38/* 14+24 */
struct noise_level {
	u8				value[PHYDM_MAX_RF_PATH];
	s8				sval[PHYDM_MAX_RF_PATH];
	s32				sum[PHYDM_MAX_RF_PATH];
	u8				valid[PHYDM_MAX_RF_PATH];
	u8				valid_cnt[PHYDM_MAX_RF_PATH];
};


struct _ODM_NOISE_MONITOR_ {
	s8			noise[PHYDM_MAX_RF_PATH];
	s16			noise_all;
};

s16 odm_inband_noise_monitor(
	void *p_dm_void,
	u8 is_pause_dig,
	u8 igi_value,
	u32 max_time
);

void
phydm_noisy_detection(
	void *p_dm_void
);

#endif
