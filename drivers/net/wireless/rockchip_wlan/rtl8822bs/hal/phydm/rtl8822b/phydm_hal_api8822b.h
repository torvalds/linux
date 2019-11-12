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
#ifndef __INC_PHYDM_API_H_8822B__
#define __INC_PHYDM_API_H_8822B__

#if (RTL8822B_SUPPORT == 1)
#define PHY_CONFIG_VERSION_8822B "28.5.34"
/* @2017.01.18 */
/* @(HW user guide version: R28, SW user guide version: R05, Modification: R34)*/
/* @remove A cut setting, refine CCK txfilter and OFDM CCA setting by YuChen*/

#define SMTANT_TMP_RFE_TYPE 100
#define ANT_2T3R_RFE_TYPE 101
#define ANT_2T4R_RFE_TYPE 102

#define INVALID_RF_DATA 0xffffffff
#define INVALID_TXAGC_DATA 0xff

#define PSD_VAL_NUM 3
#define PSD_SMP_NUM 3
#define FREQ_PT_2G_NUM 14
#define FREQ_PT_5G_NUM 10

#define number_channel_interferecne 4

#define config_phydm_read_rf_check_8822b(data) (data != INVALID_RF_DATA)
#define config_phydm_read_txagc_check_8822b(data) (data != INVALID_TXAGC_DATA)

enum agc_tab_sel {
	DEFAULT_AGC_TABLE,
	LNA_SAT_AGC_TABLE,
	AUTO_AGC_TABLE
};

void phydm_rfe_8822b_init(struct dm_struct *dm);

boolean
phydm_rfe_8822b(struct dm_struct *dm, u8 channel);

u32 config_phydm_read_rf_reg_8822b(struct dm_struct *dm,
				   enum rf_path path,
				   u32 reg_addr, u32 bit_mask);

boolean
config_phydm_write_rf_reg_8822b(struct dm_struct *dm,
				enum rf_path path, u32 reg_addr,
				u32 bit_mask, u32 data);

boolean
config_phydm_write_txagc_8822b(struct dm_struct *dm, u32 pw_idx,
			       enum rf_path path, u8 hw_rate);

u8 config_phydm_read_txagc_8822b(struct dm_struct *dm,
				 enum rf_path path, u8 hw_rate);

void phydm_dynamic_spur_det_eliminate(struct dm_struct *dm);

boolean
config_phydm_switch_band_8822b(struct dm_struct *dm,
			       u8 central_ch);

boolean
config_phydm_switch_channel_8822b(struct dm_struct *dm,
				  u8 central_ch);

__iram_odm_func__
boolean
config_phydm_switch_agc_tab_8822b(struct dm_struct *dm,
				  u8 channel, enum agc_tab_sel tab_sel);

boolean
config_phydm_switch_bandwidth_8822b(struct dm_struct *dm,
				    u8 primary_ch_idx,
				    enum channel_width bw);

boolean
config_phydm_switch_channel_bw_8822b(struct dm_struct *dm,
				     u8 central_ch,
				     u8 primary_ch_idx,
				     enum channel_width bw);
void
phydm_config_tx_path_8822b(struct dm_struct *dm, enum bb_path tx_path,
			   enum bb_path tx_path_sel_1ss,
			   enum bb_path tx_path_sel_cck);

boolean
config_phydm_trx_mode_8822b(struct dm_struct *dm,
			    enum bb_path tx_path_en,
			    enum bb_path rx_path,
			    enum bb_path tx_path_sel_1ss);

boolean
config_phydm_parameter_init_8822b(struct dm_struct *dm,
				  enum odm_parameter_init type);

/* @======================================================================== */
/* @These following functions can be used for PHY DM only*/

boolean
phydm_write_txagc_1byte_8822b(struct dm_struct *dm, u32 pw_idx,
			      enum rf_path path, u8 hw_rate);

void phydm_get_condi_num_acc_8822b(void *dm_void);

u32 phydm_get_condi_num_8822b(struct dm_struct *dm);

__iram_odm_func__
void phydm_rxagc_switch_8822b(struct dm_struct *dm,
			      boolean enable_rxagc_switch);

boolean
phydm_rfe_8822b_lps(struct dm_struct *dm, boolean enable_sw_rfe);

/* @======================================================================== */

#endif /* RTL8822B_SUPPORT == 1 */
#endif /*  @__INC_PHYDM_API_H_8822B__ */
