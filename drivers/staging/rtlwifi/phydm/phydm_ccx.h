/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
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
#ifndef __PHYDMCCX_H__
#define __PHYDMCCX_H__

#define CCX_EN 1

#define SET_NHM_SETTING 0
#define STORE_NHM_SETTING 1
#define RESTORE_NHM_SETTING 2

enum nhm_inexclude_cca { NHM_EXCLUDE_CCA, NHM_INCLUDE_CCA };

enum nhm_inexclude_txon { NHM_EXCLUDE_TXON, NHM_INCLUDE_TXON };

struct ccx_info {
	/*Settings*/
	u8 NHM_th[11];
	u16 NHM_period; /* 4us per unit */
	u16 CLM_period; /* 4us per unit */
	enum nhm_inexclude_txon nhm_inexclude_txon;
	enum nhm_inexclude_cca nhm_inexclude_cca;

	/*Previous Settings*/
	u8 NHM_th_restore[11];
	u16 NHM_period_restore; /* 4us per unit */
	u16 CLM_period_restore; /* 4us per unit */
	enum nhm_inexclude_txon NHM_inexclude_txon_restore;
	enum nhm_inexclude_cca NHM_inexclude_cca_restore;

	/*Report*/
	u8 NHM_result[12];
	u16 NHM_duration;
	u16 CLM_result;

	bool echo_NHM_en;
	bool echo_CLM_en;
	u8 echo_IGI;
};

/*NHM*/

void phydm_nhm_setting(void *dm_void, u8 nhm_setting);

void phydm_nhm_trigger(void *dm_void);

void phydm_get_nhm_result(void *dm_void);

bool phydm_check_nhm_ready(void *dm_void);

/*CLM*/

void phydm_clm_setting(void *dm_void);

void phydm_clm_trigger(void *dm_void);

bool phydm_check_cl_mready(void *dm_void);

void phydm_get_cl_mresult(void *dm_void);

#endif
