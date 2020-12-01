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

#include "mp_precomp.h"
#include "phydm_precomp.h"

#define	CALCULATE_SWINGTALBE_OFFSET(_offset, _direction, _size, _delta_thermal) \
	do {\
		for (_offset = 0; _offset < _size; _offset++) { \
			if (_delta_thermal < thermal_threshold[_direction][_offset]) { \
				if (_offset != 0)\
					_offset--;\
				break;\
			} \
		}			\
		if (_offset >= _size)\
			_offset = _size-1;\
	} while (0)

void configure_txpower_track(
	void					*dm_void,
	struct txpwrtrack_cfg	*config
)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
#if RTL8195B_SUPPORT
	if (dm->support_ic_type == ODM_RTL8195B)
		configure_txpower_track_8195b(config);
#endif
#if RTL8710C_SUPPORT
	if (dm->support_ic_type == ODM_RTL8710C)
		configure_txpower_track_8710c(config);
#endif
#if RTL8721D_SUPPORT
	if (dm->support_ic_type == ODM_RTL8721D)
		configure_txpower_track_8721d(config);
#endif

}

/* **********************************************************************
 * <20121113, Kordan> This function should be called when tx_agc changed.
 * Otherwise the previous compensation is gone, because we record the
 * delta of temperature between two TxPowerTracking watch dogs.
 *
 * NOTE: If Tx BB swing or Tx scaling is varified during run-time, still
 * need to call this function.
 * ********************************************************************** */
void
odm_clear_txpowertracking_state(
	void					*dm_void
)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _hal_rf_ *rf = &dm->rf_table;
	u8			p = 0;
	struct dm_rf_calibration_struct	*cali_info = &dm->rf_calibrate_info;

	cali_info->bb_swing_idx_cck_base = cali_info->default_cck_index;
	cali_info->bb_swing_idx_cck = cali_info->default_cck_index;
	dm->rf_calibrate_info.CCK_index = 0;

	for (p = RF_PATH_A; p < MAX_RF_PATH; ++p) {
		cali_info->bb_swing_idx_ofdm_base[p] = cali_info->default_ofdm_index;
		cali_info->bb_swing_idx_ofdm[p] = cali_info->default_ofdm_index;
		cali_info->OFDM_index[p] = cali_info->default_ofdm_index;

		cali_info->power_index_offset[p] = 0;
		cali_info->delta_power_index[p] = 0;
		cali_info->delta_power_index_last[p] = 0;

		cali_info->absolute_ofdm_swing_idx[p] = 0;
		cali_info->remnant_ofdm_swing_idx[p] = 0;
		cali_info->kfree_offset[p] = 0;
	}

	cali_info->modify_tx_agc_flag_path_a = false;
	cali_info->modify_tx_agc_flag_path_b = false;
	cali_info->modify_tx_agc_flag_path_c = false;
	cali_info->modify_tx_agc_flag_path_d = false;
	cali_info->remnant_cck_swing_idx = 0;
	cali_info->thermal_value = rf->eeprom_thermal;
	cali_info->modify_tx_agc_value_cck = 0;
	cali_info->modify_tx_agc_value_ofdm = 0;
}

void
odm_txpowertracking_callback_thermal_meter(
	void	*dm_void
)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _hal_rf_ *rf = &dm->rf_table;
	struct dm_rf_calibration_struct *cali_info = &dm->rf_calibrate_info;
	struct	dm_iqk_info *iqk_info = &dm->IQK_info;

	u8 thermal_value = 0, delta, delta_LCK, delta_IQK, p = 0, i = 0;
	u8 thermal_value_avg_count = 0;
	u32 thermal_value_avg = 0, regc80, regcd0, regcd4, regab4;

	u8 OFDM_min_index = 0;  /* OFDM BB Swing should be less than +3.0dB, which is required by Arthur */
	u8 indexforchannel = 0; /* get_right_chnl_place_for_iqk(hal_data->current_channel) */
	u8 power_tracking_type = rf->pwt_type;
	u8 xtal_offset_eanble = 0;
	s8 thermal_value_temp = 0;
	u8 xtal_track_efuse = 0;

	struct txpwrtrack_cfg	c = {0};

	/* 4 1. The following TWO tables decide the final index of OFDM/CCK swing table. */
	u8 *delta_swing_table_idx_tup_a = NULL;
	u8 *delta_swing_table_idx_tdown_a = NULL;
	u8 *delta_swing_table_idx_tup_b = NULL;
	u8 *delta_swing_table_idx_tdown_b = NULL;
#if (RTL8721D_SUPPORT == 1)
	u8 *delta_swing_table_idx_tup_a_cck = NULL;
	u8 *delta_swing_table_idx_tdown_a_cck = NULL;
	u8 *delta_swing_table_idx_tup_b_cck = NULL;
	u8 *delta_swing_table_idx_tdown_b_cck = NULL;
#endif
	/*for Xtal Offset by James.Tung*/
	s8 *delta_swing_table_xtal_up = NULL;
	s8 *delta_swing_table_xtal_down = NULL;

	/* 4 2. Initialization ( 7 steps in total ) */
	indexforchannel = odm_get_right_chnl_place_for_iqk(*dm->channel);
	configure_txpower_track(dm, &c);
#if (RTL8721D_SUPPORT == 1)
	(*c.get_delta_swing_table)(dm, (u8 **)&delta_swing_table_idx_tup_a, (u8 **)&delta_swing_table_idx_tdown_a,
		(u8 **)&delta_swing_table_idx_tup_b, (u8 **)&delta_swing_table_idx_tdown_b,
		(u8 **)&delta_swing_table_idx_tup_a_cck, (u8 **)&delta_swing_table_idx_tdown_a_cck,
		(u8 **)&delta_swing_table_idx_tup_b_cck, (u8 **)&delta_swing_table_idx_tdown_b_cck);
#else
	(*c.get_delta_swing_table)(dm, (u8 **)&delta_swing_table_idx_tup_a, (u8 **)&delta_swing_table_idx_tdown_a,
		(u8 **)&delta_swing_table_idx_tup_b, (u8 **)&delta_swing_table_idx_tdown_b);
#endif

	/*for Xtal Offset*/
	odm_efuse_one_byte_read(dm, 0xf7, &xtal_track_efuse, false);
	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "Read efuse 0xf7=0x%x\n", xtal_track_efuse);
	xtal_track_efuse = xtal_track_efuse & 0x3;
	if (dm->support_ic_type == ODM_RTL8195B ||
	    dm->support_ic_type == ODM_RTL8721D ||
	    (dm->support_ic_type == ODM_RTL8710C && xtal_track_efuse == 0x2))
		(*c.get_delta_swing_xtal_table)(dm,
		 (s8 **)&delta_swing_table_xtal_up,
		 (s8 **)&delta_swing_table_xtal_down);

	cali_info->txpowertracking_callback_cnt++;	/*cosa add for debug*/
	cali_info->is_txpowertracking_init = true;

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "===>odm_txpowertracking_callback_thermal_meter\n cali_info->bb_swing_idx_cck_base: %d, cali_info->bb_swing_idx_ofdm_base[A]: %d, cali_info->default_ofdm_index: %d\n",
	       cali_info->bb_swing_idx_cck_base,
	       cali_info->bb_swing_idx_ofdm_base[RF_PATH_A],
	       cali_info->default_ofdm_index);

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "cali_info->txpowertrack_control = %d, hal_data->eeprom_thermal_meter %d\n",
	       cali_info->txpowertrack_control, rf->eeprom_thermal);

	if (dm->support_ic_type == ODM_RTL8721D
		|| dm->support_ic_type == ODM_RTL8710C)
		thermal_value = (u8)odm_get_rf_reg(dm, RF_PATH_A,
						   c.thermal_reg_addr, 0x7e0);
		/* 0x42: RF Reg[10:5] 8721D */
	else
		thermal_value = (u8)odm_get_rf_reg(dm, RF_PATH_A,
						   c.thermal_reg_addr, 0xfc00);
		/* 0x42: RF Reg[15:10] 88E */

	thermal_value_temp = thermal_value + phydm_get_thermal_offset(dm);

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "thermal_value_temp(%d) = thermal_value(%d) + power_trim_thermal(%d)\n", thermal_value_temp, thermal_value, phydm_get_thermal_offset(dm));

	if (thermal_value_temp > 63)
		thermal_value = 63;
	else if (thermal_value_temp < 0)
		thermal_value = 0;
	else
		thermal_value = thermal_value_temp;

	if (!cali_info->txpowertrack_control)
		return;

	if (rf->eeprom_thermal == 0xff) {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "no pg, hal_data->eeprom_thermal_meter = 0x%x\n", rf->eeprom_thermal);
		return;
	}
#if 0
	/*4 3. Initialize ThermalValues of rf_calibrate_info*/
	//if (cali_info->is_reloadtxpowerindex)
	//	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "reload ofdm index for band switch\n");
#endif
	/*4 4. Calculate average thermal meter*/

	cali_info->thermal_value_avg[cali_info->thermal_value_avg_index] = thermal_value;
	cali_info->thermal_value_avg_index++;
	if (cali_info->thermal_value_avg_index == c.average_thermal_num)   /*Average times =  c.average_thermal_num*/
		cali_info->thermal_value_avg_index = 0;

	for (i = 0; i < c.average_thermal_num; i++) {
		if (cali_info->thermal_value_avg[i]) {
			thermal_value_avg += cali_info->thermal_value_avg[i];
			thermal_value_avg_count++;
		}
	}

	if (thermal_value_avg_count) {			  /* Calculate Average thermal_value after average enough times */
		thermal_value = (u8)(thermal_value_avg / thermal_value_avg_count);
		cali_info->thermal_value_delta = thermal_value - rf->eeprom_thermal;
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		       "AVG Thermal Meter = 0x%X, EFUSE Thermal base = 0x%X\n", thermal_value, rf->eeprom_thermal);
	}

	/* 4 5. Calculate delta, delta_LCK, delta_IQK. */
	/* "delta" here is used to determine whether thermal value changes or not. */
	delta	= (thermal_value > cali_info->thermal_value) ? (thermal_value - cali_info->thermal_value) : (cali_info->thermal_value - thermal_value);
	delta_LCK = (thermal_value > cali_info->thermal_value_lck) ? (thermal_value - cali_info->thermal_value_lck) : (cali_info->thermal_value_lck - thermal_value);
	delta_IQK = (thermal_value > cali_info->thermal_value_iqk) ? (thermal_value - cali_info->thermal_value_iqk) : (cali_info->thermal_value_iqk - thermal_value);

	/*4 6. If necessary, do LCK.*/
	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "(delta, delta_LCK, delta_IQK) = (%d, %d, %d)\n", delta, delta_LCK, delta_IQK);

	/* Wait sacn to do LCK by RF Jenyu*/
	if ((!*dm->is_scan_in_process) && !iqk_info->rfk_forbidden &&
	    (!*dm->is_tdma)) {
		/* Delta temperature is equal to or larger than 20 centigrade.*/
		if (delta_LCK >= c.threshold_iqk) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "delta_LCK(%d) >= threshold_iqk(%d)\n", delta_LCK, c.threshold_iqk);
			cali_info->thermal_value_lck = thermal_value;

			/*Use RTLCK, so close power tracking driver LCK*/
			(*c.phy_lc_calibrate)(dm);
		}
	}

	/*3 7. If necessary, move the index of swing table to adjust Tx power.*/
	if (delta > 0 && cali_info->txpowertrack_control) {
		/* "delta" here is used to record the absolute value of difference. */
		delta = thermal_value > rf->eeprom_thermal ? (thermal_value - rf->eeprom_thermal) : (rf->eeprom_thermal - thermal_value);

		if (delta >= TXPWR_TRACK_TABLE_SIZE)
			delta = TXPWR_TRACK_TABLE_SIZE - 1;

		/*4 7.1 The Final Power index = BaseIndex + power_index_offset*/
		if (thermal_value > rf->eeprom_thermal) {
			for (p = RF_PATH_A; p < c.rf_path_count; p++) {
				cali_info->delta_power_index_last[p] = cali_info->delta_power_index[p]; /*recording poer index offset*/
				switch (p) {
				case RF_PATH_B:
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					       "delta_swing_table_idx_tup_b[%d] = %d\n", delta, delta_swing_table_idx_tup_b[delta]);
#if (RTL8721D_SUPPORT == 1)
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					       "delta_swing_table_idx_tup_b_cck[%d] = %d\n", delta, delta_swing_table_idx_tup_b_cck[delta]);

					cali_info->absolute_cck_swing_idx[p] = delta_swing_table_idx_tup_b_cck[delta];

					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					       "******Temp is higher and cali_info->absolute_cck_swing_idx[RF_PATH_B] = %d\n",
					       cali_info->absolute_cck_swing_idx[p]);
#endif
					cali_info->delta_power_index[p] =
						delta_swing_table_idx_tup_b
						[delta];
					cali_info->absolute_ofdm_swing_idx[p] =
						delta_swing_table_idx_tup_b
						[delta];
					/*Record delta swing for mix mode*/
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					       "******Temp is higher and cali_info->absolute_ofdm_swing_idx[RF_PATH_B] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
					break;

				default:
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					       "delta_swing_table_idx_tup_a[%d] = %d\n", delta, delta_swing_table_idx_tup_a[delta]);
#if (RTL8721D_SUPPORT == 1)
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					       "delta_swing_table_idx_tup_a_cck[%d] = %d\n", delta, delta_swing_table_idx_tup_a_cck[delta]);

					cali_info->absolute_cck_swing_idx[p] = delta_swing_table_idx_tup_a_cck[delta];

					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					       "******Temp is higher and cali_info->absolute_cck_swing_idx[RF_PATH_A] = %d\n", cali_info->absolute_cck_swing_idx[p]);
#endif
					cali_info->delta_power_index[p] = delta_swing_table_idx_tup_a[delta];
					cali_info->absolute_ofdm_swing_idx[p] =
					delta_swing_table_idx_tup_a[delta];
					/*Record delta swing*/
					/*for mix mode power tracking*/
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					       "******Temp is higher and cali_info->absolute_ofdm_swing_idx[RF_PATH_A] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
					break;
				}
			}
			/* JJ ADD 20161014 */
			if (dm->support_ic_type == ODM_RTL8195B ||
			    dm->support_ic_type == ODM_RTL8721D ||
			    (dm->support_ic_type == ODM_RTL8710C && xtal_track_efuse == 0x2)) {
				/*Save xtal_offset from Xtal table*/
				cali_info->xtal_offset_last = cali_info->xtal_offset;	/*recording last Xtal offset*/
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				       "[Xtal] delta_swing_table_xtal_up[%d] = %d\n", delta, delta_swing_table_xtal_up[delta]);
				cali_info->xtal_offset = delta_swing_table_xtal_up[delta];
				xtal_offset_eanble = (cali_info->xtal_offset_last != cali_info->xtal_offset);
			}

		} else {
			for (p = RF_PATH_A; p < c.rf_path_count; p++) {
				cali_info->delta_power_index_last[p] = cali_info->delta_power_index[p]; /*recording poer index offset*/

				switch (p) {
				case RF_PATH_B:
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					       "delta_swing_table_idx_tdown_b[%d] = %d\n", delta, delta_swing_table_idx_tdown_b[delta]);
#if (RTL8721D_SUPPORT == 1)
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					       "delta_swing_table_idx_tdown_b_cck[%d] = %d\n", delta, delta_swing_table_idx_tdown_b_cck[delta]);

					cali_info->absolute_cck_swing_idx[p] = -1 * delta_swing_table_idx_tdown_b_cck[delta];

					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					       "******Temp is lower and cali_info->absolute_cck_swing_idx[RF_PATH_B] = %d\n", cali_info->absolute_cck_swing_idx[p]);
#endif
					cali_info->delta_power_index[p] = -1 * delta_swing_table_idx_tdown_b[delta];
					cali_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_b[delta]; /*Record delta swing for mix mode power tracking*/
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					       "******Temp is lower and cali_info->absolute_ofdm_swing_idx[RF_PATH_B] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
					break;

				default:
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					       "delta_swing_table_idx_tdown_a[%d] = %d\n", delta, delta_swing_table_idx_tdown_a[delta]);
#if (RTL8721D_SUPPORT == 1)
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					       "delta_swing_table_idx_tdown_a_cck[%d] = %d\n", delta, delta_swing_table_idx_tdown_a_cck[delta]);

					cali_info->absolute_cck_swing_idx[p] = -1 * delta_swing_table_idx_tdown_a_cck[delta];

					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					       "******Temp is lower and cali_info->absolute_cck_swing_idx[RF_PATH_A] = %d\n", cali_info->absolute_cck_swing_idx[p]);
#endif
					cali_info->delta_power_index[p] = -1 * delta_swing_table_idx_tdown_a[delta];
					cali_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_a[delta]; /*Record delta swing for mix mode power tracking*/
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					       "******Temp is lower and cali_info->absolute_ofdm_swing_idx[RF_PATH_A] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
					break;
				}
			}
			/* JJ ADD 20161014 */

			if (dm->support_ic_type == ODM_RTL8195B ||
			    dm->support_ic_type == ODM_RTL8721D ||
			    (dm->support_ic_type == ODM_RTL8710C && xtal_track_efuse == 0x2)) {
				/*Save xtal_offset from Xtal table*/
				cali_info->xtal_offset_last = cali_info->xtal_offset;	/*recording last Xtal offset*/
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				       "[Xtal] delta_swing_table_xtal_down[%d] = %d\n", delta, delta_swing_table_xtal_down[delta]);
				cali_info->xtal_offset = delta_swing_table_xtal_down[delta];
				xtal_offset_eanble = (cali_info->xtal_offset_last != cali_info->xtal_offset);
			}
		}
#if 0
		for (p = RF_PATH_A; p < c.rf_path_count; p++) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       "\n\n=========================== [path-%d] Calculating power_index_offset===========================\n", p);

			if (cali_info->delta_power_index[p] == cali_info->delta_power_index_last[p])		 /*If Thermal value changes but lookup table value still the same*/
				cali_info->power_index_offset[p] = 0;
			else
				cali_info->power_index_offset[p] = cali_info->delta_power_index[p] - cali_info->delta_power_index_last[p];		/*Power index diff between 2 times Power Tracking*/

			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       "[path-%d] power_index_offset(%d) = delta_power_index(%d) - delta_power_index_last(%d)\n", p, cali_info->power_index_offset[p], cali_info->delta_power_index[p], cali_info->delta_power_index_last[p]);

			cali_info->OFDM_index[p] = cali_info->bb_swing_idx_ofdm_base[p] + cali_info->power_index_offset[p];
			cali_info->CCK_index = cali_info->bb_swing_idx_cck_base + cali_info->power_index_offset[p];

			cali_info->bb_swing_idx_cck = cali_info->CCK_index;
			cali_info->bb_swing_idx_ofdm[p] = cali_info->OFDM_index[p];

			/*************Print BB Swing base and index Offset*************/

			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       "The 'CCK' final index(%d) = BaseIndex(%d) + power_index_offset(%d)\n", cali_info->bb_swing_idx_cck, cali_info->bb_swing_idx_cck_base, cali_info->power_index_offset[p]);
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       "The 'OFDM' final index(%d) = BaseIndex[%d](%d) + power_index_offset(%d)\n", cali_info->bb_swing_idx_ofdm[p], p, cali_info->bb_swing_idx_ofdm_base[p], cali_info->power_index_offset[p]);

			/*4 7.1 Handle boundary conditions of index.*/

			if (cali_info->OFDM_index[p] > c.swing_table_size_ofdm - 1)
				cali_info->OFDM_index[p] = c.swing_table_size_ofdm - 1;
			else if (cali_info->OFDM_index[p] <= OFDM_min_index)
				cali_info->OFDM_index[p] = OFDM_min_index;
		}

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		       "\n\n========================================================================================================\n");

		if (cali_info->CCK_index > c.swing_table_size_cck - 1)
			cali_info->CCK_index = c.swing_table_size_cck - 1;
		else if (cali_info->CCK_index <= 0)
			cali_info->CCK_index = 0;
#endif
	} else {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		       "The thermal meter is unchanged or TxPowerTracking OFF(%d): thermal_value: %d, cali_info->thermal_value: %d\n",
		       cali_info->txpowertrack_control, thermal_value, cali_info->thermal_value);

		for (p = RF_PATH_A; p < c.rf_path_count; p++)
			cali_info->power_index_offset[p] = 0;
	}
#if 0
	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "TxPowerTracking: [CCK] Swing Current index: %d, Swing base index: %d\n",
	       cali_info->CCK_index, cali_info->bb_swing_idx_cck_base);	   /*Print Swing base & current*/

	for (p = RF_PATH_A; p < c.rf_path_count; p++) {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		       "TxPowerTracking: [OFDM] Swing Current index: %d, Swing base index[%d]: %d\n",
		       cali_info->OFDM_index[p], p, cali_info->bb_swing_idx_ofdm_base[p]);
	}
#endif

#if (RTL8721D_SUPPORT == 1)
	if (thermal_value != cali_info->thermal_value) {
		if (thermal_value > rf->eeprom_thermal)
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       "Temperature(%d) higher than PG value(%d)\n",
			       thermal_value, rf->eeprom_thermal);
		else if (thermal_value < rf->eeprom_thermal)
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       "Temperature(%d) lower than PG value(%d)\n",
			       thermal_value, rf->eeprom_thermal);

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		       "**********Enter POWER Tracking MIX_MODE**********\n");
		for (p = RF_PATH_A; p < c.rf_path_count; p++)
			(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p,
						      indexforchannel);

		/*Record last time Power Tracking result as base.*/
		cali_info->bb_swing_idx_cck_base = cali_info->bb_swing_idx_cck;
		for (p = RF_PATH_A; p < c.rf_path_count; p++)
			cali_info->bb_swing_idx_ofdm_base[p] =
			cali_info->bb_swing_idx_ofdm[p];

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		       "cali_info->thermal_value = %d thermal_value= %d\n",
		       cali_info->thermal_value, thermal_value);
		/*Record last Power Tracking Thermal value*/
		cali_info->thermal_value = thermal_value;
	}

#else
	if (thermal_value > rf->eeprom_thermal) {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		       "Temperature(%d) higher than PG value(%d)\n", thermal_value, rf->eeprom_thermal);

		if (dm->support_ic_type == ODM_RTL8188E ||
		    dm->support_ic_type == ODM_RTL8192E ||
		    dm->support_ic_type == ODM_RTL8821 ||
		    dm->support_ic_type == ODM_RTL8812 ||
		    dm->support_ic_type == ODM_RTL8723B ||
		    dm->support_ic_type == ODM_RTL8814A ||
		    dm->support_ic_type == ODM_RTL8703B ||
		    dm->support_ic_type == ODM_RTL8188F ||
		    dm->support_ic_type == ODM_RTL8822B ||
		    dm->support_ic_type == ODM_RTL8723D ||
		    dm->support_ic_type == ODM_RTL8821C ||
		    dm->support_ic_type == ODM_RTL8710B ||
		    dm->support_ic_type == ODM_RTL8192F ||
		    dm->support_ic_type == ODM_RTL8195B ||
		    dm->support_ic_type == ODM_RTL8710C){
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking MIX_MODE**********\n");
			for (p = RF_PATH_A; p < c.rf_path_count; p++)
				(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p, 0);
		} else {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking BBSWING_MODE**********\n");
			for (p = RF_PATH_A; p < c.rf_path_count; p++)
				(*c.odm_tx_pwr_track_set_pwr)(dm, BBSWING, p, indexforchannel);
		}
	} else {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		       "Temperature(%d) lower than PG value(%d)\n", thermal_value, rf->eeprom_thermal);

		if (dm->support_ic_type == ODM_RTL8188E ||
		    dm->support_ic_type == ODM_RTL8192E ||
		    dm->support_ic_type == ODM_RTL8821 ||
		    dm->support_ic_type == ODM_RTL8812 ||
		    dm->support_ic_type == ODM_RTL8723B ||
		    dm->support_ic_type == ODM_RTL8814A ||
		    dm->support_ic_type == ODM_RTL8703B ||
		    dm->support_ic_type == ODM_RTL8188F ||
		    dm->support_ic_type == ODM_RTL8822B ||
		    dm->support_ic_type == ODM_RTL8723D ||
		    dm->support_ic_type == ODM_RTL8821C ||
		    dm->support_ic_type == ODM_RTL8710B ||
		    dm->support_ic_type == ODM_RTL8192F ||
		    dm->support_ic_type == ODM_RTL8195B ||
		    dm->support_ic_type == ODM_RTL8710C) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking MIX_MODE**********\n");
			for (p = RF_PATH_A; p < c.rf_path_count; p++)
				(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p, indexforchannel);
		} else {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking BBSWING_MODE**********\n");
			for (p = RF_PATH_A; p < c.rf_path_count; p++)
				(*c.odm_tx_pwr_track_set_pwr)(dm, BBSWING, p, indexforchannel);
		}

		cali_info->bb_swing_idx_cck_base = cali_info->bb_swing_idx_cck;    /*Record last time Power Tracking result as base.*/
		for (p = RF_PATH_A; p < c.rf_path_count; p++)
			cali_info->bb_swing_idx_ofdm_base[p] = cali_info->bb_swing_idx_ofdm[p];

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		       "cali_info->thermal_value = %d thermal_value= %d\n", cali_info->thermal_value, thermal_value);

		cali_info->thermal_value = thermal_value; /*Record last Power Tracking Thermal value*/
	}
#endif
	/* JJ ADD 20161014 */
	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			"cali_info->xtal_offset_last=%d   cali_info->xtal_offset=%d\n",
			cali_info->xtal_offset_last, cali_info->xtal_offset);

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			"xtal_offset_eanble=%d   cali_info->txpowertrack_control=%d   rf->eeprom_thermal=%d xtal_track_efuse=%d\n",
			xtal_offset_eanble, cali_info->txpowertrack_control, rf->eeprom_thermal, xtal_track_efuse);

	if (dm->support_ic_type == ODM_RTL8195B ||
	    dm->support_ic_type == ODM_RTL8721D ||
	    (dm->support_ic_type == ODM_RTL8710C && xtal_track_efuse == 0x2)) {
		if (xtal_offset_eanble != 0 && cali_info->txpowertrack_control && (rf->eeprom_thermal != 0xff)) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter Xtal Tracking**********\n");

			if (thermal_value > rf->eeprom_thermal) {
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				       "Temperature(%d) higher than PG value(%d)\n", thermal_value, rf->eeprom_thermal);
				(*c.odm_txxtaltrack_set_xtal)(dm);
			} else {
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				       "Temperature(%d) lower than PG value(%d)\n", thermal_value, rf->eeprom_thermal);
				(*c.odm_txxtaltrack_set_xtal)(dm);
			}
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********End Xtal Tracking**********\n");
		}
	}
#if (!RTL8721D_SUPPORT)
	/* Wait sacn to do IQK by RF Jenyu*/
	if ((!*dm->is_scan_in_process) && (!iqk_info->rfk_forbidden) && (dm->is_linked || *dm->mp_mode)) {
		/*Delta temperature is equal to or larger than 20 centigrade (When threshold is 8).*/
		if (delta_IQK >= c.threshold_iqk) {
			cali_info->thermal_value_iqk = thermal_value;
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "delta_IQK(%d) >= threshold_iqk(%d)\n", delta_IQK, c.threshold_iqk);
			if (!cali_info->is_iqk_in_progress)
				(*c.do_iqk)(dm, delta_IQK, thermal_value, 8);
		}
	}
#endif
	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "<===odm_txpowertracking_callback_thermal_meter\n");

	cali_info->tx_powercount = 0;
}

/* 3============================================================
 * 3 IQ Calibration
 * 3============================================================
 */

void
odm_reset_iqk_result(
	void					*dm_void
)
{
	return;
}

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
u8 odm_get_right_chnl_place_for_iqk(u8 chnl)
{
	u8 channel_all[ODM_TARGET_CHNL_NUM_2G_5G] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
		36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64,
		100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122,
		124, 126, 128, 130, 132, 134, 136, 138, 140,
		149, 151, 153, 155, 157, 159, 161, 163, 165};
	u8 place = chnl;

	if (chnl > 14) {
		for (place = 14; place < sizeof(channel_all); place++) {
			if (channel_all[place] == chnl)
				return place - 13;
		}
	}
	return 0;
}
#endif

void
odm_rf_calibrate(struct dm_struct *dm)
{
#if (RTL8721D_SUPPORT == 1)
	struct dm_iqk_info	*iqk_info = &dm->IQK_info;

	if (dm->is_linked && !iqk_info->rfk_forbidden) {
		if ((*dm->channel != dm->pre_channel) &&
		    (!*dm->is_scan_in_process)) {
			dm->pre_channel = *dm->channel;
			dm->linked_interval = 0;
		}

		if (dm->linked_interval < 3)
			dm->linked_interval++;

		if (dm->linked_interval == 2)
			halrf_rf_k_connect_trigger(dm, 0, SEGMENT_FREE);
	} else {
		dm->linked_interval = 0;
	}
#endif
}

void phydm_rf_init(void		*dm_void)
{
	struct dm_struct	*dm = (struct dm_struct *)dm_void;

	odm_txpowertracking_init(dm);
	
	odm_clear_txpowertracking_state(dm);
}

void phydm_rf_watchdog(void		*dm_void)
{
	struct dm_struct		*dm = (struct dm_struct *)dm_void;

	odm_txpowertracking_check(dm);
#if (RTL8721D_SUPPORT == 1)
	odm_rf_calibrate(dm);
#endif
}
