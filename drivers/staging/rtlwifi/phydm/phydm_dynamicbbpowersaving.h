/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __PHYDMDYNAMICBBPOWERSAVING_H__
#define __PHYDMDYNAMICBBPOWERSAVING_H__

#define DYNAMIC_BBPWRSAV_VERSION "1.1"

struct dyn_pwr_saving {
	u8 pre_cca_state;
	u8 cur_cca_state;

	u8 pre_rf_state;
	u8 cur_rf_state;

	int rssi_val_min;

	u8 initialize;
	u32 reg874, regc70, reg85c, rega74;
};

#define dm_rf_saving odm_rf_saving

void odm_rf_saving(void *dm_void, u8 is_force_in_normal);

void odm_dynamic_bb_power_saving_init(void *dm_void);

#endif
