/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __HALPHYRF_H__
#define __HALPHYRF_H__

#include "halrf/halrf_kfree.h"

#if (RTL8821C_SUPPORT == 1)
	#include "halrf/rtl8821c/halrf_iqk_8821c.h"
#endif

#if (RTL8195B_SUPPORT == 1)
//	#include "halrf/rtl8195b/halrf.h"
	#include "halrf/rtl8195b/halrf_iqk_8195b.h"
	#include "halrf/rtl8195b/halrf_txgapk_8195b.h"
	#include "halrf/rtl8195b/halrf_dpk_8195b.h"
#endif

#if (RTL8710C_SUPPORT == 1)
//	#include "halrf/rtl8710c/halrf.h"
	#include "halrf/rtl8710c/halrf_iqk_8710c.h"
//	#include "halrf/rtl8710c/halrf_txgapk_8710c.h"
//	#include "halrf/rtl8710c/halrf_dpk_8710c.h"
#endif

#include "halrf/halrf_powertracking_iot.h"


enum spur_cal_method {
	PLL_RESET,
	AFE_PHASE_SEL
};

enum pwrtrack_method {
	BBSWING,
	TXAGC,
	MIX_MODE,
	TSSI_MODE,
	MIX_2G_TSSI_5G_MODE,
	MIX_5G_TSSI_2G_MODE,
	CLEAN_MODE
};

typedef void	(*func_set_pwr)(void *, enum pwrtrack_method, u8, u8);
typedef void(*func_iqk)(void *, u8, u8, u8);
typedef void	(*func_lck)(void *);
#if (RTL8721D_SUPPORT == 1)
	typedef void	(*func_swing)(void *, u8 **, u8 **, u8 **, u8 **,
				      u8 **, u8 **, u8 **, u8 **);
#else
	typedef void	(*func_swing)(void *, u8 **, u8 **, u8 **, u8 **);
#endif
typedef void	(*func_swing8814only)(void *, u8 **, u8 **, u8 **, u8 **);
typedef void(*func_swing_xtal)(void *, s8 **, s8 **);
typedef void(*func_set_xtal)(void *);

struct txpwrtrack_cfg {
	u8		swing_table_size_cck;
	u8		swing_table_size_ofdm;
	u8		threshold_iqk;
	u8		threshold_dpk;
	u8		average_thermal_num;
	u8		rf_path_count;
	u32		thermal_reg_addr;
	func_set_pwr	odm_tx_pwr_track_set_pwr;
	func_iqk	do_iqk;
	func_lck		phy_lc_calibrate;
	func_swing	get_delta_swing_table;
	func_swing8814only	get_delta_swing_table8814only;
	func_swing_xtal			get_delta_swing_xtal_table;
	func_set_xtal			odm_txxtaltrack_set_xtal;
};

void
configure_txpower_track(
	void					*dm_void,
	struct txpwrtrack_cfg	*config
);


void
odm_clear_txpowertracking_state(
	void					*dm_void
);

void
odm_txpowertracking_callback_thermal_meter(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	void					*dm_void
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	void	*dm
#else
	void	*adapter
#endif
);



#define ODM_TARGET_CHNL_NUM_2G_5G	59


void
odm_reset_iqk_result(
	void					*dm_void
);
u8
odm_get_right_chnl_place_for_iqk(
	u8 chnl
);

void phydm_rf_init(void					*dm_void);
void phydm_rf_watchdog(void					*dm_void);

#endif	/*#ifndef __HALPHYRF_H__*/
