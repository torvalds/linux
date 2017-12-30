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

#ifndef __PHYDMDYNAMICTXPOWER_H__
#define __PHYDMDYNAMICTXPOWER_H__

/*#define DYNAMIC_TXPWR_VERSION	"1.0"*/
/*#define DYNAMIC_TXPWR_VERSION	"1.3" */ /*2015.08.26, Add 8814 Dynamic TX pwr*/
#define DYNAMIC_TXPWR_VERSION "1.4" /*2015.11.06,Add CE 8821A Dynamic TX pwr*/

#define TX_POWER_NEAR_FIELD_THRESH_LVL2 74
#define TX_POWER_NEAR_FIELD_THRESH_LVL1 60

#define tx_high_pwr_level_normal 0
#define tx_high_pwr_level_level1 1
#define tx_high_pwr_level_level2 2

#define tx_high_pwr_level_bt1 3
#define tx_high_pwr_level_bt2 4
#define tx_high_pwr_level_15 5
#define tx_high_pwr_level_35 6
#define tx_high_pwr_level_50 7
#define tx_high_pwr_level_70 8
#define tx_high_pwr_level_100 9

void odm_dynamic_tx_power_init(void *dm_void);

void odm_dynamic_tx_power_restore_power_index(void *dm_void);

void odm_dynamic_tx_power_nic(void *dm_void);

void odm_dynamic_tx_power_save_power_index(void *dm_void);

void odm_dynamic_tx_power_write_power_index(void *dm_void, u8 value);

void odm_dynamic_tx_power_8821(void *dm_void, u8 *desc, u8 mac_id);

void odm_dynamic_tx_power(void *dm_void);

void odm_dynamic_tx_power_ap(void *dm_void);

#endif
