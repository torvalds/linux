// SPDX-License-Identifier: GPL-2.0
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

#include "mp_precomp.h"
#include "phydm_precomp.h"

#define CALCULATE_SWINGTALBE_OFFSET(_offset, _direction, _size,                \
				    _delta_thermal)                            \
	do {                                                                   \
		for (_offset = 0; _offset < _size; _offset++) {                \
			if (_delta_thermal <                                   \
			    thermal_threshold[_direction][_offset]) {          \
				if (_offset != 0)                              \
					_offset--;                             \
				break;                                         \
			}                                                      \
		}                                                              \
		if (_offset >= _size)                                          \
			_offset = _size - 1;                                   \
	} while (0)

static inline void phydm_set_calibrate_info_up(
	struct phy_dm_struct *dm, struct txpwrtrack_cfg *c, u8 delta,
	struct dm_rf_calibration_struct *cali_info,
	u8 *delta_swing_table_idx_tup_a, u8 *delta_swing_table_idx_tup_b,
	u8 *delta_swing_table_idx_tup_c, u8 *delta_swing_table_idx_tup_d)
{
	u8 p = 0;

	for (p = ODM_RF_PATH_A; p < c->rf_path_count; p++) {
		cali_info->delta_power_index_last[p] =
			cali_info->delta_power_index
				[p]; /*recording poer index offset*/
		switch (p) {
		case ODM_RF_PATH_B:
			ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK,
				     "delta_swing_table_idx_tup_b[%d] = %d\n",
				     delta, delta_swing_table_idx_tup_b[delta]);

			cali_info->delta_power_index[p] =
				delta_swing_table_idx_tup_b[delta];
			/*Record delta swing for mix mode pwr tracking*/
			cali_info->absolute_ofdm_swing_idx[p] =
				delta_swing_table_idx_tup_b[delta];
			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"******Temp is higher and cali_info->absolute_ofdm_swing_idx[ODM_RF_PATH_B] = %d\n",
				cali_info->absolute_ofdm_swing_idx[p]);
			break;

		case ODM_RF_PATH_C:
			ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK,
				     "delta_swing_table_idx_tup_c[%d] = %d\n",
				     delta, delta_swing_table_idx_tup_c[delta]);

			cali_info->delta_power_index[p] =
				delta_swing_table_idx_tup_c[delta];
			/*Record delta swing for mix mode pwr tracking*/
			cali_info->absolute_ofdm_swing_idx[p] =
				delta_swing_table_idx_tup_c[delta];
			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"******Temp is higher and cali_info->absolute_ofdm_swing_idx[ODM_RF_PATH_C] = %d\n",
				cali_info->absolute_ofdm_swing_idx[p]);
			break;

		case ODM_RF_PATH_D:
			ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK,
				     "delta_swing_table_idx_tup_d[%d] = %d\n",
				     delta, delta_swing_table_idx_tup_d[delta]);

			cali_info->delta_power_index[p] =
				delta_swing_table_idx_tup_d[delta];
			/*Record delta swing for mix mode pwr tracking*/
			cali_info->absolute_ofdm_swing_idx[p] =
				delta_swing_table_idx_tup_d[delta];
			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"******Temp is higher and cali_info->absolute_ofdm_swing_idx[ODM_RF_PATH_D] = %d\n",
				cali_info->absolute_ofdm_swing_idx[p]);
			break;

		default:
			ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK,
				     "delta_swing_table_idx_tup_a[%d] = %d\n",
				     delta, delta_swing_table_idx_tup_a[delta]);

			cali_info->delta_power_index[p] =
				delta_swing_table_idx_tup_a[delta];
			/*Record delta swing for mix mode pwr tracking*/
			cali_info->absolute_ofdm_swing_idx[p] =
				delta_swing_table_idx_tup_a[delta];
			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"******Temp is higher and cali_info->absolute_ofdm_swing_idx[ODM_RF_PATH_A] = %d\n",
				cali_info->absolute_ofdm_swing_idx[p]);
			break;
		}
	}
}

static inline void phydm_set_calibrate_info_down(
	struct phy_dm_struct *dm, struct txpwrtrack_cfg *c, u8 delta,
	struct dm_rf_calibration_struct *cali_info,
	u8 *delta_swing_table_idx_tdown_a, u8 *delta_swing_table_idx_tdown_b,
	u8 *delta_swing_table_idx_tdown_c, u8 *delta_swing_table_idx_tdown_d)
{
	u8 p = 0;

	for (p = ODM_RF_PATH_A; p < c->rf_path_count; p++) {
		cali_info->delta_power_index_last[p] =
			cali_info->delta_power_index
				[p]; /*recording poer index offset*/

		switch (p) {
		case ODM_RF_PATH_B:
			ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK,
				     "delta_swing_table_idx_tdown_b[%d] = %d\n",
				     delta,
				     delta_swing_table_idx_tdown_b[delta]);
			cali_info->delta_power_index[p] =
				-1 * delta_swing_table_idx_tdown_b[delta];
			/*Record delta swing for mix mode pwr tracking*/
			cali_info->absolute_ofdm_swing_idx[p] =
				-1 * delta_swing_table_idx_tdown_b[delta];
			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"******Temp is lower and cali_info->absolute_ofdm_swing_idx[ODM_RF_PATH_B] = %d\n",
				cali_info->absolute_ofdm_swing_idx[p]);
			break;

		case ODM_RF_PATH_C:
			ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK,
				     "delta_swing_table_idx_tdown_c[%d] = %d\n",
				     delta,
				     delta_swing_table_idx_tdown_c[delta]);
			cali_info->delta_power_index[p] =
				-1 * delta_swing_table_idx_tdown_c[delta];
			/*Record delta swing for mix mode pwr tracking*/
			cali_info->absolute_ofdm_swing_idx[p] =
				-1 * delta_swing_table_idx_tdown_c[delta];
			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"******Temp is lower and cali_info->absolute_ofdm_swing_idx[ODM_RF_PATH_C] = %d\n",
				cali_info->absolute_ofdm_swing_idx[p]);
			break;

		case ODM_RF_PATH_D:
			ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK,
				     "delta_swing_table_idx_tdown_d[%d] = %d\n",
				     delta,
				     delta_swing_table_idx_tdown_d[delta]);
			cali_info->delta_power_index[p] =
				-1 * delta_swing_table_idx_tdown_d[delta];
			/*Record delta swing for mix mode pwr tracking*/
			cali_info->absolute_ofdm_swing_idx[p] =
				-1 * delta_swing_table_idx_tdown_d[delta];
			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"******Temp is lower and cali_info->absolute_ofdm_swing_idx[ODM_RF_PATH_D] = %d\n",
				cali_info->absolute_ofdm_swing_idx[p]);
			break;

		default:
			ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK,
				     "delta_swing_table_idx_tdown_a[%d] = %d\n",
				     delta,
				     delta_swing_table_idx_tdown_a[delta]);
			cali_info->delta_power_index[p] =
				-1 * delta_swing_table_idx_tdown_a[delta];
			/*Record delta swing for mix mode pwr tracking*/
			cali_info->absolute_ofdm_swing_idx[p] =
				-1 * delta_swing_table_idx_tdown_a[delta];
			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"******Temp is lower and cali_info->absolute_ofdm_swing_idx[ODM_RF_PATH_A] = %d\n",
				cali_info->absolute_ofdm_swing_idx[p]);
			break;
		}
	}
}

static inline void phydm_odm_tx_power_set(struct phy_dm_struct *dm,
					  struct txpwrtrack_cfg *c,
					  u8 indexforchannel, u8 flag)
{
	u8 p = 0;

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
	    dm->support_ic_type == ODM_RTL8710B) { /* JJ ADD 20161014 */

		ODM_RT_TRACE(
			dm, ODM_COMP_TX_PWR_TRACK,
			"**********Enter POWER Tracking MIX_MODE**********\n");
		for (p = ODM_RF_PATH_A; p < c->rf_path_count; p++) {
			if (flag == 0)
				(*c->odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p,
							       0);
			else
				(*c->odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p,
							       indexforchannel);
		}
	} else {
		ODM_RT_TRACE(
			dm, ODM_COMP_TX_PWR_TRACK,
			"**********Enter POWER Tracking BBSWING_MODE**********\n");
		for (p = ODM_RF_PATH_A; p < c->rf_path_count; p++)
			(*c->odm_tx_pwr_track_set_pwr)(dm, BBSWING, p,
						       indexforchannel);
	}
}

void configure_txpower_track(void *dm_void, struct txpwrtrack_cfg *config)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	/* JJ ADD 20161014 */

	if (dm->support_ic_type == ODM_RTL8822B)
		configure_txpower_track_8822b(config);
}

/* **********************************************************************
 * <20121113, Kordan> This function should be called when tx_agc changed.
 * Otherwise the previous compensation is gone, because we record the
 * delta of temperature between two TxPowerTracking watch dogs.
 *
 * NOTE: If Tx BB swing or Tx scaling is varified during run-time, still
 * need to call this function.
 * ***********************************************************************/
void odm_clear_txpowertracking_state(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;
	struct rtl_efuse *rtlefu = rtl_efuse(rtlpriv);
	u8 p = 0;
	struct dm_rf_calibration_struct *cali_info = &dm->rf_calibrate_info;

	cali_info->bb_swing_idx_cck_base = cali_info->default_cck_index;
	cali_info->bb_swing_idx_cck = cali_info->default_cck_index;
	dm->rf_calibrate_info.CCK_index = 0;

	for (p = ODM_RF_PATH_A; p < MAX_RF_PATH; ++p) {
		cali_info->bb_swing_idx_ofdm_base[p] =
			cali_info->default_ofdm_index;
		cali_info->bb_swing_idx_ofdm[p] = cali_info->default_ofdm_index;
		cali_info->OFDM_index[p] = cali_info->default_ofdm_index;

		cali_info->power_index_offset[p] = 0;
		cali_info->delta_power_index[p] = 0;
		cali_info->delta_power_index_last[p] = 0;

		cali_info->absolute_ofdm_swing_idx[p] =
			0; /* Initial Mix mode power tracking*/
		cali_info->remnant_ofdm_swing_idx[p] = 0;
		cali_info->kfree_offset[p] = 0;
	}

	cali_info->modify_tx_agc_flag_path_a =
		false; /*Initial at Modify Tx Scaling mode*/
	cali_info->modify_tx_agc_flag_path_b =
		false; /*Initial at Modify Tx Scaling mode*/
	cali_info->modify_tx_agc_flag_path_c =
		false; /*Initial at Modify Tx Scaling mode*/
	cali_info->modify_tx_agc_flag_path_d =
		false; /*Initial at Modify Tx Scaling mode*/
	cali_info->remnant_cck_swing_idx = 0;
	cali_info->thermal_value = rtlefu->eeprom_thermalmeter;

	cali_info->modify_tx_agc_value_cck = 0; /* modify by Mingzhi.Guo */
	cali_info->modify_tx_agc_value_ofdm = 0; /* modify by Mingzhi.Guo */
}

void odm_txpowertracking_callback_thermal_meter(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;
	struct rtl_efuse *rtlefu = rtl_efuse(rtlpriv);
	void *adapter = dm->adapter;

	struct dm_rf_calibration_struct *cali_info = &dm->rf_calibrate_info;

	u8 thermal_value = 0, delta, delta_LCK, delta_IQK, p = 0, i = 0;
	s8 diff_DPK[4]; /* use 'for..loop' to initialize */
	u8 thermal_value_avg_count = 0;
	u32 thermal_value_avg = 0, regc80, regcd0, regcd4, regab4;

	/* OFDM BB Swing should be less than +3.0dB (required by Arthur) */
	u8 OFDM_min_index = 0;
	/* get_right_chnl_place_for_iqk(hal_data->current_channel) */
	u8 indexforchannel = 0;
	u8 power_tracking_type = 0; /* no specify type */
	u8 xtal_offset_eanble = 0;

	struct txpwrtrack_cfg c;

	/* 4 1. The following TWO tables decide the final index of
	 *      OFDM/CCK swing table.
	 */
	u8 *delta_swing_table_idx_tup_a = NULL;
	u8 *delta_swing_table_idx_tdown_a = NULL;
	u8 *delta_swing_table_idx_tup_b = NULL;
	u8 *delta_swing_table_idx_tdown_b = NULL;
	/*for 8814 add by Yu Chen*/
	u8 *delta_swing_table_idx_tup_c = NULL;
	u8 *delta_swing_table_idx_tdown_c = NULL;
	u8 *delta_swing_table_idx_tup_d = NULL;
	u8 *delta_swing_table_idx_tdown_d = NULL;
	/*for Xtal Offset by James.Tung*/
	s8 *delta_swing_table_xtal_up = NULL;
	s8 *delta_swing_table_xtal_down = NULL;

	/* 4 2. Initialization ( 7 steps in total ) */

	configure_txpower_track(dm, &c);

	(*c.get_delta_swing_table)(dm, (u8 **)&delta_swing_table_idx_tup_a,
				   (u8 **)&delta_swing_table_idx_tdown_a,
				   (u8 **)&delta_swing_table_idx_tup_b,
				   (u8 **)&delta_swing_table_idx_tdown_b);

	if (dm->support_ic_type & ODM_RTL8814A) /*for 8814 path C & D*/
		(*c.get_delta_swing_table8814only)(
			dm, (u8 **)&delta_swing_table_idx_tup_c,
			(u8 **)&delta_swing_table_idx_tdown_c,
			(u8 **)&delta_swing_table_idx_tup_d,
			(u8 **)&delta_swing_table_idx_tdown_d);
	/* JJ ADD 20161014 */
	if (dm->support_ic_type &
	    (ODM_RTL8703B | ODM_RTL8723D | ODM_RTL8710B)) /*for Xtal Offset*/
		(*c.get_delta_swing_xtal_table)(
			dm, (s8 **)&delta_swing_table_xtal_up,
			(s8 **)&delta_swing_table_xtal_down);

	cali_info->txpowertracking_callback_cnt++; /*cosa add for debug*/
	cali_info->is_txpowertracking_init = true;

	/*cali_info->txpowertrack_control = hal_data->txpowertrack_control;
	 *<Kordan> We should keep updating ctrl variable according to HalData.
	 *<Kordan> rf_calibrate_info.rega24 will be initialized when
	 *ODM HW configuring, but MP configures with para files.
	 */
	if (dm->mp_mode)
		cali_info->rega24 = 0x090e1317;

	ODM_RT_TRACE(
		dm, ODM_COMP_TX_PWR_TRACK,
		"===>%s\n cali_info->bb_swing_idx_cck_base: %d, cali_info->bb_swing_idx_ofdm_base[A]: %d, cali_info->default_ofdm_index: %d\n",
		__func__, cali_info->bb_swing_idx_cck_base,
		cali_info->bb_swing_idx_ofdm_base[ODM_RF_PATH_A],
		cali_info->default_ofdm_index);

	ODM_RT_TRACE(
		dm, ODM_COMP_TX_PWR_TRACK,
		"cali_info->txpowertrack_control=%d,  rtlefu->eeprom_thermalmeter %d\n",
		cali_info->txpowertrack_control, rtlefu->eeprom_thermalmeter);

	thermal_value =
		(u8)odm_get_rf_reg(dm, ODM_RF_PATH_A, c.thermal_reg_addr,
				   0xfc00); /* 0x42: RF Reg[15:10] 88E */

	/*add log by zhao he, check c80/c94/c14/ca0 value*/
	if (dm->support_ic_type == ODM_RTL8723D) {
		regc80 = odm_get_bb_reg(dm, 0xc80, MASKDWORD);
		regcd0 = odm_get_bb_reg(dm, 0xcd0, MASKDWORD);
		regcd4 = odm_get_bb_reg(dm, 0xcd4, MASKDWORD);
		regab4 = odm_get_bb_reg(dm, 0xab4, 0x000007FF);
		ODM_RT_TRACE(
			dm, ODM_COMP_CALIBRATION,
			"0xc80 = 0x%x 0xcd0 = 0x%x 0xcd4 = 0x%x 0xab4 = 0x%x\n",
			regc80, regcd0, regcd4, regab4);
	}
	/* JJ ADD 20161014 */
	if (dm->support_ic_type == ODM_RTL8710B) {
		regc80 = odm_get_bb_reg(dm, 0xc80, MASKDWORD);
		regcd0 = odm_get_bb_reg(dm, 0xcd0, MASKDWORD);
		regcd4 = odm_get_bb_reg(dm, 0xcd4, MASKDWORD);
		regab4 = odm_get_bb_reg(dm, 0xab4, 0x000007FF);
		ODM_RT_TRACE(
			dm, ODM_COMP_CALIBRATION,
			"0xc80 = 0x%x 0xcd0 = 0x%x 0xcd4 = 0x%x 0xab4 = 0x%x\n",
			regc80, regcd0, regcd4, regab4);
	}

	if (!cali_info->txpowertrack_control)
		return;

	/*4 3. Initialize ThermalValues of rf_calibrate_info*/

	if (cali_info->is_reloadtxpowerindex)
		ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK,
			     "reload ofdm index for band switch\n");

	/*4 4. Calculate average thermal meter*/

	cali_info->thermal_value_avg[cali_info->thermal_value_avg_index] =
		thermal_value;
	cali_info->thermal_value_avg_index++;
	if (cali_info->thermal_value_avg_index ==
	    c.average_thermal_num) /*Average times =  c.average_thermal_num*/
		cali_info->thermal_value_avg_index = 0;

	for (i = 0; i < c.average_thermal_num; i++) {
		if (cali_info->thermal_value_avg[i]) {
			thermal_value_avg += cali_info->thermal_value_avg[i];
			thermal_value_avg_count++;
		}
	}

	if (thermal_value_avg_count) {
		/* Calculate Average thermal_value after average enough times */
		thermal_value =
			(u8)(thermal_value_avg / thermal_value_avg_count);
		cali_info->thermal_value_delta =
			thermal_value - rtlefu->eeprom_thermalmeter;
		ODM_RT_TRACE(
			dm, ODM_COMP_TX_PWR_TRACK,
			"AVG Thermal Meter = 0x%X, EFUSE Thermal base = 0x%X\n",
			thermal_value, rtlefu->eeprom_thermalmeter);
	}

	/* 4 5. Calculate delta, delta_LCK, delta_IQK. */

	/* "delta" is used to determine whether thermal value changes or not*/
	delta = (thermal_value > cali_info->thermal_value) ?
			(thermal_value - cali_info->thermal_value) :
			(cali_info->thermal_value - thermal_value);
	delta_LCK = (thermal_value > cali_info->thermal_value_lck) ?
			    (thermal_value - cali_info->thermal_value_lck) :
			    (cali_info->thermal_value_lck - thermal_value);
	delta_IQK = (thermal_value > cali_info->thermal_value_iqk) ?
			    (thermal_value - cali_info->thermal_value_iqk) :
			    (cali_info->thermal_value_iqk - thermal_value);

	if (cali_info->thermal_value_iqk ==
	    0xff) { /*no PG, use thermal value for IQK*/
		cali_info->thermal_value_iqk = thermal_value;
		delta_IQK =
			(thermal_value > cali_info->thermal_value_iqk) ?
				(thermal_value - cali_info->thermal_value_iqk) :
				(cali_info->thermal_value_iqk - thermal_value);
		ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK,
			     "no PG, use thermal_value for IQK\n");
	}

	for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++)
		diff_DPK[p] = (s8)thermal_value - (s8)cali_info->dpk_thermal[p];

	/*4 6. If necessary, do LCK.*/

	if (!(dm->support_ic_type &
	      ODM_RTL8821)) { /*no PG, do LCK at initial status*/
		if (cali_info->thermal_value_lck == 0xff) {
			ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK,
				     "no PG, do LCK\n");
			cali_info->thermal_value_lck = thermal_value;

			/*Use RTLCK, so close power tracking driver LCK*/
			if (!(dm->support_ic_type & ODM_RTL8814A) &&
			    c.phy_lc_calibrate)
				(*c.phy_lc_calibrate)(dm);

			delta_LCK =
				(thermal_value > cali_info->thermal_value_lck) ?
					(thermal_value -
					 cali_info->thermal_value_lck) :
					(cali_info->thermal_value_lck -
					 thermal_value);
		}

		ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK,
			     "(delta, delta_LCK, delta_IQK) = (%d, %d, %d)\n",
			     delta, delta_LCK, delta_IQK);

		/*Delta temperature is equal to or larger than 20 centigrade.*/
		if (delta_LCK >= c.threshold_iqk) {
			ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK,
				     "delta_LCK(%d) >= threshold_iqk(%d)\n",
				     delta_LCK, c.threshold_iqk);
			cali_info->thermal_value_lck = thermal_value;

			/*Use RTLCK, so close power tracking driver LCK*/
			if (!(dm->support_ic_type & ODM_RTL8814A) &&
			    c.phy_lc_calibrate)
				(*c.phy_lc_calibrate)(dm);
		}
	}

	/*3 7. If necessary, move the index of swing table to adjust Tx power.*/

	if (delta > 0 && cali_info->txpowertrack_control) {
		/* "delta" here is used to record the abs value of difference.*/
		delta = thermal_value > rtlefu->eeprom_thermalmeter ?
				(thermal_value - rtlefu->eeprom_thermalmeter) :
				(rtlefu->eeprom_thermalmeter - thermal_value);
		if (delta >= TXPWR_TRACK_TABLE_SIZE)
			delta = TXPWR_TRACK_TABLE_SIZE - 1;

		/*4 7.1 The Final Power index = BaseIndex + power_index_offset*/

		if (thermal_value > rtlefu->eeprom_thermalmeter) {
			phydm_set_calibrate_info_up(
				dm, &c, delta, cali_info,
				delta_swing_table_idx_tup_a,
				delta_swing_table_idx_tup_b,
				delta_swing_table_idx_tup_c,
				delta_swing_table_idx_tup_d);
			/* JJ ADD 20161014 */
			if (dm->support_ic_type &
			    (ODM_RTL8703B | ODM_RTL8723D | ODM_RTL8710B)) {
				/*Save xtal_offset from Xtal table*/

				/*recording last Xtal offset*/
				cali_info->xtal_offset_last =
					cali_info->xtal_offset;
				ODM_RT_TRACE(
					dm, ODM_COMP_TX_PWR_TRACK,
					"[Xtal] delta_swing_table_xtal_up[%d] = %d\n",
					delta,
					delta_swing_table_xtal_up[delta]);
				cali_info->xtal_offset =
					delta_swing_table_xtal_up[delta];
				xtal_offset_eanble =
					(cali_info->xtal_offset_last ==
					 cali_info->xtal_offset) ?
						0 :
						1;
			}

		} else {
			phydm_set_calibrate_info_down(
				dm, &c, delta, cali_info,
				delta_swing_table_idx_tdown_a,
				delta_swing_table_idx_tdown_b,
				delta_swing_table_idx_tdown_c,
				delta_swing_table_idx_tdown_d);
			/* JJ ADD 20161014 */
			if (dm->support_ic_type &
			    (ODM_RTL8703B | ODM_RTL8723D | ODM_RTL8710B)) {
				/*Save xtal_offset from Xtal table*/

				/*recording last Xtal offset*/
				cali_info->xtal_offset_last =
					cali_info->xtal_offset;
				ODM_RT_TRACE(
					dm, ODM_COMP_TX_PWR_TRACK,
					"[Xtal] delta_swing_table_xtal_down[%d] = %d\n",
					delta,
					delta_swing_table_xtal_down[delta]);
				cali_info->xtal_offset =
					delta_swing_table_xtal_down[delta];
				xtal_offset_eanble =
					(cali_info->xtal_offset_last ==
					 cali_info->xtal_offset) ?
						0 :
						1;
			}
		}

		for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++) {
			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"\n\n=========================== [path-%d] Calculating power_index_offset===========================\n",
				p);

			if (cali_info->delta_power_index[p] ==
			    cali_info->delta_power_index_last[p]) {
				/* If Thermal value changes but lookup table
				 * value still the same
				 */
				cali_info->power_index_offset[p] = 0;
			} else {
				/*Power idx diff between 2 times Pwr Tracking*/
				cali_info->power_index_offset[p] =
					cali_info->delta_power_index[p] -
					cali_info->delta_power_index_last[p];
			}

			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"[path-%d] power_index_offset(%d) = delta_power_index(%d) - delta_power_index_last(%d)\n",
				p, cali_info->power_index_offset[p],
				cali_info->delta_power_index[p],
				cali_info->delta_power_index_last[p]);

			cali_info->OFDM_index[p] =
				cali_info->bb_swing_idx_ofdm_base[p] +
				cali_info->power_index_offset[p];
			cali_info->CCK_index =
				cali_info->bb_swing_idx_cck_base +
				cali_info->power_index_offset[p];

			cali_info->bb_swing_idx_cck = cali_info->CCK_index;
			cali_info->bb_swing_idx_ofdm[p] =
				cali_info->OFDM_index[p];

			/*******Print BB Swing base and index Offset**********/

			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"The 'CCK' final index(%d) = BaseIndex(%d) + power_index_offset(%d)\n",
				cali_info->bb_swing_idx_cck,
				cali_info->bb_swing_idx_cck_base,
				cali_info->power_index_offset[p]);
			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"The 'OFDM' final index(%d) = BaseIndex[%d](%d) + power_index_offset(%d)\n",
				cali_info->bb_swing_idx_ofdm[p], p,
				cali_info->bb_swing_idx_ofdm_base[p],
				cali_info->power_index_offset[p]);

			/*4 7.1 Handle boundary conditions of index.*/

			if (cali_info->OFDM_index[p] >
			    c.swing_table_size_ofdm - 1)
				cali_info->OFDM_index[p] =
					c.swing_table_size_ofdm - 1;
			else if (cali_info->OFDM_index[p] <= OFDM_min_index)
				cali_info->OFDM_index[p] = OFDM_min_index;
		}

		ODM_RT_TRACE(
			dm, ODM_COMP_TX_PWR_TRACK,
			"\n\n========================================================================================================\n");

		if (cali_info->CCK_index > c.swing_table_size_cck - 1)
			cali_info->CCK_index = c.swing_table_size_cck - 1;
		else if (cali_info->CCK_index <= 0)
			cali_info->CCK_index = 0;
	} else {
		ODM_RT_TRACE(
			dm, ODM_COMP_TX_PWR_TRACK,
			"The thermal meter is unchanged or TxPowerTracking OFF(%d): thermal_value: %d, cali_info->thermal_value: %d\n",
			cali_info->txpowertrack_control, thermal_value,
			cali_info->thermal_value);

		for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++)
			cali_info->power_index_offset[p] = 0;
	}

	/*Print Swing base & current*/
	ODM_RT_TRACE(
		dm, ODM_COMP_TX_PWR_TRACK,
		"TxPowerTracking: [CCK] Swing Current index: %d, Swing base index: %d\n",
		cali_info->CCK_index, cali_info->bb_swing_idx_cck_base);

	for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++)
		ODM_RT_TRACE(
			dm, ODM_COMP_TX_PWR_TRACK,
			"TxPowerTracking: [OFDM] Swing Current index: %d, Swing base index[%d]: %d\n",
			cali_info->OFDM_index[p], p,
			cali_info->bb_swing_idx_ofdm_base[p]);

	if ((dm->support_ic_type & ODM_RTL8814A)) {
		ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK,
			     "power_tracking_type=%d\n", power_tracking_type);

		if (power_tracking_type == 0) {
			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"**********Enter POWER Tracking MIX_MODE**********\n");
			for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++)
				(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p,
							      0);
		} else if (power_tracking_type == 1) {
			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"**********Enter POWER Tracking MIX(2G) TSSI(5G) MODE**********\n");
			for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++)
				(*c.odm_tx_pwr_track_set_pwr)(
					dm, MIX_2G_TSSI_5G_MODE, p, 0);
		} else if (power_tracking_type == 2) {
			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"**********Enter POWER Tracking MIX(5G) TSSI(2G)MODE**********\n");
			for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++)
				(*c.odm_tx_pwr_track_set_pwr)(
					dm, MIX_5G_TSSI_2G_MODE, p, 0);
		} else if (power_tracking_type == 3) {
			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"**********Enter POWER Tracking TSSI MODE**********\n");
			for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++)
				(*c.odm_tx_pwr_track_set_pwr)(dm, TSSI_MODE, p,
							      0);
		}
		/*Record last Power Tracking Thermal value*/
		cali_info->thermal_value = thermal_value;

	} else if ((cali_info->power_index_offset[ODM_RF_PATH_A] != 0 ||
		    cali_info->power_index_offset[ODM_RF_PATH_B] != 0 ||
		    cali_info->power_index_offset[ODM_RF_PATH_C] != 0 ||
		    cali_info->power_index_offset[ODM_RF_PATH_D] != 0) &&
		   cali_info->txpowertrack_control &&
		   (rtlefu->eeprom_thermalmeter != 0xff)) {
		/* 4 7.2 Configure the Swing Table to adjust Tx Power. */

		/*Always true after Tx Power is adjusted by power tracking.*/
		cali_info->is_tx_power_changed = true;
		/* 2012/04/23 MH According to Luke's suggestion, we can not
		 * write BB digital to increase TX power. Otherwise, EVM will
		 * be bad.
		 */
		/* 2012/04/25 MH Add for tx power tracking to set tx power in
		 * tx agc for 88E.
		 */
		if (thermal_value > cali_info->thermal_value) {
			for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++) {
				/* print temperature increasing */
				ODM_RT_TRACE(
					dm, ODM_COMP_TX_PWR_TRACK,
					"Temperature Increasing(%d): delta_pi: %d, delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n",
					p, cali_info->power_index_offset[p],
					delta, thermal_value,
					rtlefu->eeprom_thermalmeter,
					cali_info->thermal_value);
			}
		} else if (thermal_value <
			   cali_info->thermal_value) { /*Low temperature*/
			for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++) {
				/* print temperature decreasing */
				ODM_RT_TRACE(
					dm, ODM_COMP_TX_PWR_TRACK,
					"Temperature Decreasing(%d): delta_pi: %d, delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n",
					p, cali_info->power_index_offset[p],
					delta, thermal_value,
					rtlefu->eeprom_thermalmeter,
					cali_info->thermal_value);
			}
		}

		if (thermal_value > rtlefu->eeprom_thermalmeter) {
			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"Temperature(%d) higher than PG value(%d)\n",
				thermal_value, rtlefu->eeprom_thermalmeter);

			phydm_odm_tx_power_set(dm, &c, indexforchannel, 0);
		} else {
			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"Temperature(%d) lower than PG value(%d)\n",
				thermal_value, rtlefu->eeprom_thermalmeter);
			phydm_odm_tx_power_set(dm, &c, indexforchannel, 1);
		}

		/*Record last time Power Tracking result as base.*/
		cali_info->bb_swing_idx_cck_base = cali_info->bb_swing_idx_cck;

		for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++)
			cali_info->bb_swing_idx_ofdm_base[p] =
				cali_info->bb_swing_idx_ofdm[p];

		ODM_RT_TRACE(
			dm, ODM_COMP_TX_PWR_TRACK,
			"cali_info->thermal_value = %d thermal_value= %d\n",
			cali_info->thermal_value, thermal_value);

		/*Record last Power Tracking Thermal value*/
		cali_info->thermal_value = thermal_value;
	}

	if (dm->support_ic_type == ODM_RTL8703B ||
	    dm->support_ic_type == ODM_RTL8723D ||
	    dm->support_ic_type == ODM_RTL8710B) { /* JJ ADD 20161014 */

		if (xtal_offset_eanble != 0 &&
		    cali_info->txpowertrack_control &&
		    rtlefu->eeprom_thermalmeter != 0xff) {
			ODM_RT_TRACE(
				dm, ODM_COMP_TX_PWR_TRACK,
				"**********Enter Xtal Tracking**********\n");

			if (thermal_value > rtlefu->eeprom_thermalmeter) {
				ODM_RT_TRACE(
					dm, ODM_COMP_TX_PWR_TRACK,
					"Temperature(%d) higher than PG value(%d)\n",
					thermal_value,
					rtlefu->eeprom_thermalmeter);
				(*c.odm_txxtaltrack_set_xtal)(dm);
			} else {
				ODM_RT_TRACE(
					dm, ODM_COMP_TX_PWR_TRACK,
					"Temperature(%d) lower than PG value(%d)\n",
					thermal_value,
					rtlefu->eeprom_thermalmeter);
				(*c.odm_txxtaltrack_set_xtal)(dm);
			}
		}
		ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK,
			     "**********End Xtal Tracking**********\n");
	}

	if (!IS_HARDWARE_TYPE_8723B(adapter)) {
		/* Delta temperature is equal to or larger than 20 centigrade
		 * (When threshold is 8).
		 */
		if (delta_IQK >= c.threshold_iqk) {
			ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK,
				     "delta_IQK(%d) >= threshold_iqk(%d)\n",
				     delta_IQK, c.threshold_iqk);
			if (!cali_info->is_iqk_in_progress)
				(*c.do_iqk)(dm, delta_IQK, thermal_value, 8);
		}
	}
	if (cali_info->dpk_thermal[ODM_RF_PATH_A] != 0) {
		if (diff_DPK[ODM_RF_PATH_A] >= c.threshold_dpk) {
			odm_set_bb_reg(dm, 0x82c, BIT(31), 0x1);
			odm_set_bb_reg(
				dm, 0xcc4,
				BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10),
				(diff_DPK[ODM_RF_PATH_A] / c.threshold_dpk));
			odm_set_bb_reg(dm, 0x82c, BIT(31), 0x0);
		} else if ((diff_DPK[ODM_RF_PATH_A] <= -1 * c.threshold_dpk)) {
			s32 value = 0x20 +
				    (diff_DPK[ODM_RF_PATH_A] / c.threshold_dpk);

			odm_set_bb_reg(dm, 0x82c, BIT(31), 0x1);
			odm_set_bb_reg(dm, 0xcc4, BIT(14) | BIT(13) | BIT(12) |
							  BIT(11) | BIT(10),
				       value);
			odm_set_bb_reg(dm, 0x82c, BIT(31), 0x0);
		} else {
			odm_set_bb_reg(dm, 0x82c, BIT(31), 0x1);
			odm_set_bb_reg(dm, 0xcc4, BIT(14) | BIT(13) | BIT(12) |
							  BIT(11) | BIT(10),
				       0);
			odm_set_bb_reg(dm, 0x82c, BIT(31), 0x0);
		}
	}
	if (cali_info->dpk_thermal[ODM_RF_PATH_B] != 0) {
		if (diff_DPK[ODM_RF_PATH_B] >= c.threshold_dpk) {
			odm_set_bb_reg(dm, 0x82c, BIT(31), 0x1);
			odm_set_bb_reg(
				dm, 0xec4,
				BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10),
				(diff_DPK[ODM_RF_PATH_B] / c.threshold_dpk));
			odm_set_bb_reg(dm, 0x82c, BIT(31), 0x0);
		} else if ((diff_DPK[ODM_RF_PATH_B] <= -1 * c.threshold_dpk)) {
			s32 value = 0x20 +
				    (diff_DPK[ODM_RF_PATH_B] / c.threshold_dpk);

			odm_set_bb_reg(dm, 0x82c, BIT(31), 0x1);
			odm_set_bb_reg(dm, 0xec4, BIT(14) | BIT(13) | BIT(12) |
							  BIT(11) | BIT(10),
				       value);
			odm_set_bb_reg(dm, 0x82c, BIT(31), 0x0);
		} else {
			odm_set_bb_reg(dm, 0x82c, BIT(31), 0x1);
			odm_set_bb_reg(dm, 0xec4, BIT(14) | BIT(13) | BIT(12) |
							  BIT(11) | BIT(10),
				       0);
			odm_set_bb_reg(dm, 0x82c, BIT(31), 0x0);
		}
	}

	ODM_RT_TRACE(dm, ODM_COMP_TX_PWR_TRACK, "<===%s\n", __func__);

	cali_info->tx_powercount = 0;
}

/* 3============================================================
 * 3 IQ Calibration
 * 3============================================================
 */

void odm_reset_iqk_result(void *dm_void) { return; }

u8 odm_get_right_chnl_place_for_iqk(u8 chnl)
{
	u8 channel_all[ODM_TARGET_CHNL_NUM_2G_5G] = {
		1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,
		13,  14,  36,  38,  40,  42,  44,  46,  48,  50,  52,  54,
		56,  58,  60,  62,  64,  100, 102, 104, 106, 108, 110, 112,
		114, 116, 118, 120, 122, 124, 126, 128, 130, 132, 134, 136,
		138, 140, 149, 151, 153, 155, 157, 159, 161, 163, 165};
	u8 place = chnl;

	if (chnl > 14) {
		for (place = 14; place < sizeof(channel_all); place++) {
			if (channel_all[place] == chnl)
				return place - 13;
		}
	}
	return 0;
}

static void odm_iq_calibrate(struct phy_dm_struct *dm)
{
	void *adapter = dm->adapter;

	if (IS_HARDWARE_TYPE_8812AU(adapter))
		return;

	if (dm->is_linked) {
		if ((*dm->channel != dm->pre_channel) &&
		    (!*dm->is_scan_in_process)) {
			dm->pre_channel = *dm->channel;
			dm->linked_interval = 0;
		}

		if (dm->linked_interval < 3)
			dm->linked_interval++;

		if (dm->linked_interval == 2) {
			if (IS_HARDWARE_TYPE_8814A(adapter))
				;

			else if (IS_HARDWARE_TYPE_8822B(adapter))
				phy_iq_calibrate_8822b(dm, false);
		}
	} else {
		dm->linked_interval = 0;
	}
}

void phydm_rf_init(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	odm_txpowertracking_init(dm);

	odm_clear_txpowertracking_state(dm);
}

void phydm_rf_watchdog(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	odm_txpowertracking_check(dm);
	if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		odm_iq_calibrate(dm);
}
