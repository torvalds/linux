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

#include "../mp_precomp.h"
#include "../phydm_precomp.h"

static bool
get_mix_mode_tx_agc_bb_swing_offset_8822b(void *dm_void,
					  enum pwrtrack_method method,
					  u8 rf_path, u8 tx_power_index_offest)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &dm->rf_calibrate_info;

	u8 bb_swing_upper_bound = cali_info->default_ofdm_index + 10;
	u8 bb_swing_lower_bound = 0;

	s8 tx_agc_index = 0;
	u8 tx_bb_swing_index = cali_info->default_ofdm_index;

	ODM_RT_TRACE(
		dm, ODM_COMP_TX_PWR_TRACK,
		"Path_%d cali_info->absolute_ofdm_swing_idx[rf_path]=%d, tx_power_index_offest=%d\n",
		rf_path, cali_info->absolute_ofdm_swing_idx[rf_path],
		tx_power_index_offest);

	if (tx_power_index_offest > 0XF)
		tx_power_index_offest = 0XF;

	if (cali_info->absolute_ofdm_swing_idx[rf_path] >= 0 &&
	    cali_info->absolute_ofdm_swing_idx[rf_path] <=
		    tx_power_index_offest) {
		tx_agc_index = cali_info->absolute_ofdm_swing_idx[rf_path];
		tx_bb_swing_index = cali_info->default_ofdm_index;
	} else if (cali_info->absolute_ofdm_swing_idx[rf_path] >
		   tx_power_index_offest) {
		tx_agc_index = tx_power_index_offest;
		cali_info->remnant_ofdm_swing_idx[rf_path] =
			cali_info->absolute_ofdm_swing_idx[rf_path] -
			tx_power_index_offest;
		tx_bb_swing_index = cali_info->default_ofdm_index +
				    cali_info->remnant_ofdm_swing_idx[rf_path];

		if (tx_bb_swing_index > bb_swing_upper_bound)
			tx_bb_swing_index = bb_swing_upper_bound;
	} else {
		tx_agc_index = 0;

		if (cali_info->default_ofdm_index >
		    (cali_info->absolute_ofdm_swing_idx[rf_path] * (-1)))
			tx_bb_swing_index =
				cali_info->default_ofdm_index +
				cali_info->absolute_ofdm_swing_idx[rf_path];
		else
			tx_bb_swing_index = bb_swing_lower_bound;

		if (tx_bb_swing_index < bb_swing_lower_bound)
			tx_bb_swing_index = bb_swing_lower_bound;
	}

	cali_info->absolute_ofdm_swing_idx[rf_path] = tx_agc_index;
	cali_info->bb_swing_idx_ofdm[rf_path] = tx_bb_swing_index;

	ODM_RT_TRACE(
		dm, ODM_COMP_TX_PWR_TRACK,
		"MixMode Offset Path_%d   cali_info->absolute_ofdm_swing_idx[rf_path]=%d   cali_info->bb_swing_idx_ofdm[rf_path]=%d   tx_power_index_offest=%d\n",
		rf_path, cali_info->absolute_ofdm_swing_idx[rf_path],
		cali_info->bb_swing_idx_ofdm[rf_path], tx_power_index_offest);

	return true;
}

void odm_tx_pwr_track_set_pwr8822b(void *dm_void, enum pwrtrack_method method,
				   u8 rf_path, u8 channel_mapped_index)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &dm->rf_calibrate_info;
	u8 tx_power_index_offest = 0;
	u8 tx_power_index = 0;

	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 channel = rtlphy->current_channel;
	u8 band_width = rtlphy->current_chan_bw;
	u8 tx_rate = 0xFF;

	if (!dm->mp_mode) {
		u16 rate = *dm->forced_data_rate;

		if (!rate) /*auto rate*/
			tx_rate = dm->tx_rate;
		else /*force rate*/
			tx_rate = (u8)rate;
	}

	ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK, "Call:%s tx_rate=0x%X\n",
		     __func__, tx_rate);

	ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK,
		     "pRF->default_ofdm_index=%d   pRF->default_cck_index=%d\n",
		     cali_info->default_ofdm_index,
		     cali_info->default_cck_index);

	ODM_RT_TRACE(
		dm, ODM_COMP_TX_PWR_TRACK,
		"pRF->absolute_ofdm_swing_idx=%d   pRF->remnant_ofdm_swing_idx=%d   pRF->absolute_cck_swing_idx=%d   pRF->remnant_cck_swing_idx=%d   rf_path=%d\n",
		cali_info->absolute_ofdm_swing_idx[rf_path],
		cali_info->remnant_ofdm_swing_idx[rf_path],
		cali_info->absolute_cck_swing_idx[rf_path],
		cali_info->remnant_cck_swing_idx, rf_path);

	if (dm->number_linked_client != 0)
		tx_power_index = odm_get_tx_power_index(
			dm, (enum odm_rf_radio_path)rf_path, tx_rate,
			band_width, channel);

	if (tx_power_index >= 63)
		tx_power_index = 63;

	tx_power_index_offest = 63 - tx_power_index;

	ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK,
		     "tx_power_index=%d tx_power_index_offest=%d rf_path=%d\n",
		     tx_power_index, tx_power_index_offest, rf_path);

	if (method ==
	    BBSWING) { /*use for mp driver clean power tracking status*/
		switch (rf_path) {
		case ODM_RF_PATH_A:
			odm_set_bb_reg(
				dm, 0xC94, (BIT(29) | BIT(28) | BIT(27) |
					    BIT(26) | BIT(25)),
				cali_info->absolute_ofdm_swing_idx[rf_path]);
			odm_set_bb_reg(
				dm, REG_A_TX_SCALE_JAGUAR, 0xFFE00000,
				tx_scaling_table_jaguar
					[cali_info
						 ->bb_swing_idx_ofdm[rf_path]]);
			break;
		case ODM_RF_PATH_B:
			odm_set_bb_reg(
				dm, 0xE94, (BIT(29) | BIT(28) | BIT(27) |
					    BIT(26) | BIT(25)),
				cali_info->absolute_ofdm_swing_idx[rf_path]);
			odm_set_bb_reg(
				dm, REG_B_TX_SCALE_JAGUAR, 0xFFE00000,
				tx_scaling_table_jaguar
					[cali_info
						 ->bb_swing_idx_ofdm[rf_path]]);
			break;

		default:
			break;
		}
	} else if (method == MIX_MODE) {
		switch (rf_path) {
		case ODM_RF_PATH_A:
			get_mix_mode_tx_agc_bb_swing_offset_8822b(
				dm, method, rf_path, tx_power_index_offest);
			odm_set_bb_reg(
				dm, 0xC94, (BIT(29) | BIT(28) | BIT(27) |
					    BIT(26) | BIT(25)),
				cali_info->absolute_ofdm_swing_idx[rf_path]);
			odm_set_bb_reg(
				dm, REG_A_TX_SCALE_JAGUAR, 0xFFE00000,
				tx_scaling_table_jaguar
					[cali_info
						 ->bb_swing_idx_ofdm[rf_path]]);

			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"TXAGC(0xC94)=0x%x BBSwing(0xc1c)=0x%x BBSwingIndex=%d rf_path=%d\n",
				odm_get_bb_reg(dm, 0xC94,
					       (BIT(29) | BIT(28) | BIT(27) |
						BIT(26) | BIT(25))),
				odm_get_bb_reg(dm, 0xc1c, 0xFFE00000),
				cali_info->bb_swing_idx_ofdm[rf_path], rf_path);
			break;

		case ODM_RF_PATH_B:
			get_mix_mode_tx_agc_bb_swing_offset_8822b(
				dm, method, rf_path, tx_power_index_offest);
			odm_set_bb_reg(
				dm, 0xE94, (BIT(29) | BIT(28) | BIT(27) |
					    BIT(26) | BIT(25)),
				cali_info->absolute_ofdm_swing_idx[rf_path]);
			odm_set_bb_reg(
				dm, REG_B_TX_SCALE_JAGUAR, 0xFFE00000,
				tx_scaling_table_jaguar
					[cali_info
						 ->bb_swing_idx_ofdm[rf_path]]);

			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"TXAGC(0xE94)=0x%x BBSwing(0xe1c)=0x%x BBSwingIndex=%d rf_path=%d\n",
				odm_get_bb_reg(dm, 0xE94,
					       (BIT(29) | BIT(28) | BIT(27) |
						BIT(26) | BIT(25))),
				odm_get_bb_reg(dm, 0xe1c, 0xFFE00000),
				cali_info->bb_swing_idx_ofdm[rf_path], rf_path);
			break;

		default:
			break;
		}
	}
}

void get_delta_swing_table_8822b(void *dm_void, u8 **temperature_up_a,
				 u8 **temperature_down_a, u8 **temperature_up_b,
				 u8 **temperature_down_b)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &dm->rf_calibrate_info;

	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u8 channel = rtlphy->current_channel;

	*temperature_up_a = cali_info->delta_swing_table_idx_2ga_p;
	*temperature_down_a = cali_info->delta_swing_table_idx_2ga_n;
	*temperature_up_b = cali_info->delta_swing_table_idx_2gb_p;
	*temperature_down_b = cali_info->delta_swing_table_idx_2gb_n;

	if (channel >= 36 && channel <= 64) {
		*temperature_up_a = cali_info->delta_swing_table_idx_5ga_p[0];
		*temperature_down_a = cali_info->delta_swing_table_idx_5ga_n[0];
		*temperature_up_b = cali_info->delta_swing_table_idx_5gb_p[0];
		*temperature_down_b = cali_info->delta_swing_table_idx_5gb_n[0];
	} else if (channel >= 100 && channel <= 144) {
		*temperature_up_a = cali_info->delta_swing_table_idx_5ga_p[1];
		*temperature_down_a = cali_info->delta_swing_table_idx_5ga_n[1];
		*temperature_up_b = cali_info->delta_swing_table_idx_5gb_p[1];
		*temperature_down_b = cali_info->delta_swing_table_idx_5gb_n[1];
	} else if (channel >= 149 && channel <= 177) {
		*temperature_up_a = cali_info->delta_swing_table_idx_5ga_p[2];
		*temperature_down_a = cali_info->delta_swing_table_idx_5ga_n[2];
		*temperature_up_b = cali_info->delta_swing_table_idx_5gb_p[2];
		*temperature_down_b = cali_info->delta_swing_table_idx_5gb_n[2];
	}
}

static void _phy_lc_calibrate_8822b(struct phy_dm_struct *dm)
{
	u32 lc_cal = 0, cnt = 0;

	/*backup RF0x18*/
	lc_cal = odm_get_rf_reg(dm, ODM_RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK);

	/*Start LCK*/
	odm_set_rf_reg(dm, ODM_RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK,
		       lc_cal | 0x08000);

	ODM_delay_ms(100);

	for (cnt = 0; cnt < 100; cnt++) {
		if (odm_get_rf_reg(dm, ODM_RF_PATH_A, RF_CHNLBW, 0x8000) != 0x1)
			break;
		ODM_delay_ms(10);
	}

	/*Recover channel number*/
	odm_set_rf_reg(dm, ODM_RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK, lc_cal);
}

void phy_lc_calibrate_8822b(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	bool is_start_cont_tx = false, is_single_tone = false,
	     is_carrier_suppression = false;
	u64 start_time;
	u64 progressing_time;

	if (is_start_cont_tx || is_single_tone || is_carrier_suppression) {
		ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
			     "[LCK]continues TX ing !!! LCK return\n");
		return;
	}

	start_time = odm_get_current_time(dm);
	_phy_lc_calibrate_8822b(dm);
	progressing_time = odm_get_progressing_time(dm, start_time);
	ODM_RT_TRACE(dm, ODM_COMP_CALIBRATION,
		     "[LCK]LCK progressing_time = %lld\n", progressing_time);
}

void configure_txpower_track_8822b(struct txpwrtrack_cfg *config)
{
	config->swing_table_size_cck = TXSCALE_TABLE_SIZE;
	config->swing_table_size_ofdm = TXSCALE_TABLE_SIZE;
	config->threshold_iqk = IQK_THRESHOLD;
	config->threshold_dpk = DPK_THRESHOLD;
	config->average_thermal_num = AVG_THERMAL_NUM_8822B;
	config->rf_path_count = MAX_PATH_NUM_8822B;
	config->thermal_reg_addr = RF_T_METER_8822B;

	config->odm_tx_pwr_track_set_pwr = odm_tx_pwr_track_set_pwr8822b;
	config->do_iqk = do_iqk_8822b;
	config->phy_lc_calibrate = phy_lc_calibrate_8822b;

	config->get_delta_swing_table = get_delta_swing_table_8822b;
}

void phy_set_rf_path_switch_8822b(struct phy_dm_struct *dm, bool is_main)
{
	/*BY SY Request */
	odm_set_bb_reg(dm, 0x4C, (BIT(24) | BIT(23)), 0x2);
	odm_set_bb_reg(dm, 0x974, 0xff, 0xff);

	/*odm_set_bb_reg(dm, 0x1991, 0x3, 0x0);*/
	odm_set_bb_reg(dm, 0x1990, (BIT(9) | BIT(8)), 0x0);

	/*odm_set_bb_reg(dm, 0xCBE, 0x8, 0x0);*/
	odm_set_bb_reg(dm, 0xCBC, BIT(19), 0x0);

	odm_set_bb_reg(dm, 0xCB4, 0xff, 0x77);

	odm_set_bb_reg(dm, 0x70, MASKBYTE3, 0x0e);
	odm_set_bb_reg(dm, 0x1704, MASKDWORD, 0x0000ff00);
	odm_set_bb_reg(dm, 0x1700, MASKDWORD, 0xc00f0038);

	if (is_main) {
		/*odm_set_bb_reg(dm, 0xCBD, 0x3, 0x2);		WiFi */
		odm_set_bb_reg(dm, 0xCBC, (BIT(9) | BIT(8)), 0x2); /*WiFi */
	} else {
		/*odm_set_bb_reg(dm, 0xCBD, 0x3, 0x1);	 BT*/
		odm_set_bb_reg(dm, 0xCBC, (BIT(9) | BIT(8)), 0x1); /*BT*/
	}
}
