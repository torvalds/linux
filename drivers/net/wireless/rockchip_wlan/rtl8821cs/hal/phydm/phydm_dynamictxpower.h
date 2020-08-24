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

#ifndef __PHYDMDYNAMICTXPOWER_H__
#define __PHYDMDYNAMICTXPOWER_H__

#ifdef CONFIG_DYNAMIC_TX_TWR
/* @============================================================
 *  Definition
 * ============================================================
 */

/* 2019.6.14, Modify per sta API to fix the AP problem of early return*/
#define DYNAMIC_TXPWR_VERSION "2.1"

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
#define TX_POWER_NEAR_FIELD_THRESH_LVL2 74
#define TX_POWER_NEAR_FIELD_THRESH_LVL1 60
#define TX_POWER_NEAR_FIELD_THRESH_AP 0x3F
#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#define TX_POWER_NEAR_FIELD_THRESH_LVL2 74
#define TX_POWER_NEAR_FIELD_THRESH_LVL1 67
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
#define TX_POWER_NEAR_FIELD_THRESH_LVL2 74
#define TX_POWER_NEAR_FIELD_THRESH_LVL1 60
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
#define TX_PWR_NEAR_FIELD_TH_JGR3_LVL3 255
#define TX_PWR_NEAR_FIELD_TH_JGR3_LVL2 74
#define TX_PWR_NEAR_FIELD_TH_JGR3_LVL1 60
#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#define TX_PWR_NEAR_FIELD_TH_JGR3_LVL3 90
#define TX_PWR_NEAR_FIELD_TH_JGR3_LVL2 85
#define TX_PWR_NEAR_FIELD_TH_JGR3_LVL1 80
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
#define TX_PWR_NEAR_FIELD_TH_JGR3_LVL3 90
#define TX_PWR_NEAR_FIELD_TH_JGR3_LVL2 85
#define TX_PWR_NEAR_FIELD_TH_JGR3_LVL1 80
#endif

#define tx_high_pwr_level_normal 0
#define tx_high_pwr_level_level1 1
#define tx_high_pwr_level_level2 2
#define tx_high_pwr_level_level3 3
#define tx_high_pwr_level_unchange 4

/* @============================================================
 * enumrate
 * ============================================================
 */
enum phydm_dtp_power_offset {
	PHYDM_OFFSET_ZERO = 0,
	PHYDM_OFFSET_MINUS_3DB = 1,
	PHYDM_OFFSET_MINUS_7DB = 2,
	PHYDM_OFFSET_MINUS_11DB = 3,
	PHYDM_OFFSET_ADD_3DB = 4,
	PHYDM_OFFSET_ADD_6DB = 5
};

enum phydm_dtp_power_offset_2nd {
	PHYDM_2ND_OFFSET_ZERO = 0,
	PHYDM_2ND_OFFSET_MINUS_3DB = 1,
	PHYDM_2ND_OFFSET_MINUS_7DB = 2,
	PHYDM_2ND_OFFSET_MINUS_11DB = 3
};

enum phydm_dtp_power_offset_bbram {
	/*@ HW min use 1dB*/
	PHYDM_BBRAM_OFFSET_ZERO = 0,
	PHYDM_BBRAM_OFFSET_MINUS_3DB = -3,
	PHYDM_BBRAM_OFFSET_MINUS_7DB = -7,
	PHYDM_BBRAM_OFFSET_MINUS_11DB = -11
};

enum phydm_dtp_power_pkt_type {
	RAM_PWR_OFST0		= 0,
	RAM_PWR_OFST1		= 1,
	REG_PWR_OFST0		= 2,
	REG_PWR_OFST1		= 3
};

/* @============================================================
 *  structure
 * ============================================================
 */

/* @============================================================
 *  Function Prototype
 * ============================================================
 */

extern void
odm_set_dyntxpwr(void *dm_void, u8 *desc, u8 mac_id);

void phydm_dynamic_tx_power(void *dm_void);

void phydm_dynamic_tx_power_init(void *dm_void);

void phydm_dtp_debug(void *dm_void, char input[][16], u32 *_used, char *output,
			     u32 *_out_len);

void phydm_rd_reg_pwr(void *dm_void, u32 *_used, char *output, u32 *_out_len);

void phydm_wt_reg_pwr(void *dm_void, boolean is_ofst1, boolean pwr_ofst_en,
            		     s8 pwr_ofst);

void phydm_wt_ram_pwr(void *dm_void, u8 macid, boolean is_ofst1, 
		             boolean pwr_ofst_en, s8 pwr_ofst);


#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
void odm_dynamic_tx_power_win(void *dm_void);
#endif

#endif
#endif
