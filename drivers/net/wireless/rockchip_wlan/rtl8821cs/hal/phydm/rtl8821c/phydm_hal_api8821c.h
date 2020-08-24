/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
#ifndef __INC_PHYDM_API_H_8821C__
#define __INC_PHYDM_API_H_8821C__

#if (RTL8821C_SUPPORT == 1)

#define PHY_CONFIG_VERSION_8821C "3.1.27" /*2017.08.08     (HW user guide version: R03, SW user guide version: R01, Modification: R27)*/

#define INVALID_RF_DATA 0xffffffff
#define INVALID_TXAGC_DATA 0xff

#define config_phydm_read_rf_check_8821c(data) (data != INVALID_RF_DATA)
#define config_phydm_read_txagc_check_8821c(data) (data != INVALID_TXAGC_DATA)

#define	PSD_VAL_NUM_8821C 5
#define	PSD_SMP_NUM_8821C 3
#define	FREQ_PT_5G_NUM_8821C 3

enum rf_set_8821c {
	SWITCH_TO_BTG = 0x0,
	SWITCH_TO_WLG = 0x1,
	SWITCH_TO_WLA = 0x2,
	SWITCH_TO_BT = 0x3
};

enum ant_num_8821c {
	SWITCH_TO_ANT1 = 0x0,
	SWITCH_TO_ANT2 = 0x1
};

enum ant_num_map_8821c {
	BOTH_AVAILABLE = 0x1,
	ONLY_ANT1 = 0x2,
	ONLY_ANT2 = 0x3,
	DONT_CARE = 0x4
};

s8 phydm_cck_rssi_8821c(struct dm_struct *dm, u8 lna_idx, u8 vga_idx);

u32 config_phydm_read_rf_reg_8821c(struct dm_struct *dm, enum rf_path path,
				   u32 reg_addr, u32 bit_mask);

boolean
config_phydm_write_rf_reg_8821c(struct dm_struct *dm, enum rf_path path,
				u32 reg_addr, u32 bit_mask, u32 data);

boolean
config_phydm_write_txagc_8821c(struct dm_struct *dm, u32 power_index,
			       enum rf_path path, u8 hw_rate);

u8 config_phydm_read_txagc_8821c(struct dm_struct *dm, enum rf_path path,
				 u8 hw_rate);

boolean
config_phydm_switch_band_8821c(struct dm_struct *dm, u8 central_ch);

boolean
config_phydm_switch_channel_8821c(struct dm_struct *dm, u8 central_ch);

boolean
config_phydm_switch_bandwidth_8821c(struct dm_struct *dm, u8 primary_ch_idx,
				    enum channel_width bandwidth);

boolean
config_phydm_switch_channel_bw_8821c(struct dm_struct *dm, u8 central_ch,
				     u8 primary_ch_idx,
				     enum channel_width bandwidth);

boolean
config_phydm_trx_mode_8821c(struct dm_struct *dm, enum bb_path tx_path,
			    enum bb_path rx_path, boolean is_tx2_path);

boolean
config_phydm_parameter_init_8821c(struct dm_struct *dm,
				  enum odm_parameter_init type);

void config_phydm_switch_rf_set_8821c(struct dm_struct *dm, u8 rf_set);

void config_phydm_set_ant_path(struct dm_struct *dm, u8 rf_set, u8 ant_num);

/* ======================================================================== */
/* These following functions can be used for PHY DM only*/

boolean
phydm_write_txagc_1byte_8821c(struct dm_struct *dm, u32 power_index,
			      enum rf_path path, u8 hw_rate);

void phydm_init_hw_info_by_rfe_type_8821c(struct dm_struct *dm);

void phydm_set_gnt_state_8821c(struct dm_struct *dm, boolean gnt_wl_state,
			       boolean gnt_bt_state);

/* ======================================================================== */

u32 query_phydm_trx_capability_8821c(struct dm_struct *dm);

u32 query_phydm_stbc_capability_8821c(struct dm_struct *dm);

u32 query_phydm_ldpc_capability_8821c(struct dm_struct *dm);

u32 query_phydm_txbf_parameters_8821c(struct dm_struct *dm);

u32 query_phydm_txbf_capability_8821c(struct dm_struct *dm);

u8 query_phydm_default_rf_set_8821c(struct dm_struct *dm);

u8 query_phydm_current_rf_set_8821c(struct dm_struct *dm);

u8 query_phydm_rfetype_8821c(struct dm_struct *dm);

u8 query_phydm_current_ant_num_8821c(struct dm_struct *dm);

u8 query_phydm_ant_num_map_8821c(struct dm_struct *dm);

#endif /* RTL8821C_SUPPORT == 1 */
#endif /*  __INC_PHYDM_API_H_8821C__ */
