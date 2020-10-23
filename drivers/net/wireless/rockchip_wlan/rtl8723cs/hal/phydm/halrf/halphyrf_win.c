/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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

#include "mp_precomp.h"
#include "phydm_precomp.h"

#define	CALCULATE_SWINGTALBE_OFFSET(_offset, _direction, _size, _delta_thermal) \
	do {\
		for (_offset = 0; _offset < _size; _offset++) { \
			\
			if (_delta_thermal < thermal_threshold[_direction][_offset]) { \
				\
				if (_offset != 0)\
					_offset--;\
				break;\
			} \
		}			\
		if (_offset >= _size)\
			_offset = _size-1;\
	} while (0)

void configure_txpower_track(
	struct dm_struct		*dm,
	struct txpwrtrack_cfg	*config
)
{
#if RTL8192E_SUPPORT
	if (dm->support_ic_type == ODM_RTL8192E)
		configure_txpower_track_8192e(config);
#endif
#if RTL8821A_SUPPORT
	if (dm->support_ic_type == ODM_RTL8821)
		configure_txpower_track_8821a(config);
#endif
#if RTL8812A_SUPPORT
	if (dm->support_ic_type == ODM_RTL8812)
		configure_txpower_track_8812a(config);
#endif
#if RTL8188E_SUPPORT
	if (dm->support_ic_type == ODM_RTL8188E)
		configure_txpower_track_8188e(config);
#endif

#if RTL8188F_SUPPORT
	if (dm->support_ic_type == ODM_RTL8188F)
		configure_txpower_track_8188f(config);
#endif

#if RTL8723B_SUPPORT
	if (dm->support_ic_type == ODM_RTL8723B)
		configure_txpower_track_8723b(config);
#endif

#if RTL8814A_SUPPORT
	if (dm->support_ic_type == ODM_RTL8814A)
		configure_txpower_track_8814a(config);
#endif

#if RTL8703B_SUPPORT
	if (dm->support_ic_type == ODM_RTL8703B)
		configure_txpower_track_8703b(config);
#endif

#if RTL8822B_SUPPORT
	if (dm->support_ic_type == ODM_RTL8822B)
		configure_txpower_track_8822b(config);
#endif

#if RTL8723D_SUPPORT
	if (dm->support_ic_type == ODM_RTL8723D)
		configure_txpower_track_8723d(config);
#endif

/* JJ ADD 20161014 */
#if RTL8710B_SUPPORT
	if (dm->support_ic_type == ODM_RTL8710B)
		configure_txpower_track_8710b(config);
#endif

#if RTL8821C_SUPPORT
	if (dm->support_ic_type == ODM_RTL8821C)
		configure_txpower_track_8821c(config);
#endif

#if RTL8192F_SUPPORT
	if (dm->support_ic_type == ODM_RTL8192F)
		configure_txpower_track_8192f(config);
#endif

#if RTL8822C_SUPPORT
	if (dm->support_ic_type == ODM_RTL8822C)
		configure_txpower_track_8822c(config);
#endif

#if RTL8814B_SUPPORT
	if (dm->support_ic_type == ODM_RTL8814B)
		configure_txpower_track_8814b(config);
#endif

#if RTL8723F_SUPPORT
	if (dm->support_ic_type == ODM_RTL8723F)
		configure_txpower_track_8723f(config);
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
	struct dm_struct		*dm
)
{
	PHAL_DATA_TYPE	hal_data = GET_HAL_DATA((PADAPTER)(dm->adapter));
	u8			p = 0;
	struct dm_rf_calibration_struct	*cali_info = &(dm->rf_calibrate_info);

	cali_info->bb_swing_idx_cck_base = cali_info->default_cck_index;
	cali_info->bb_swing_idx_cck = cali_info->default_cck_index;
	cali_info->CCK_index = 0;

	for (p = RF_PATH_A; p < MAX_RF_PATH; ++p) {
		cali_info->bb_swing_idx_ofdm_base[p] = cali_info->default_ofdm_index;
		cali_info->bb_swing_idx_ofdm[p] = cali_info->default_ofdm_index;
		cali_info->OFDM_index[p] = cali_info->default_ofdm_index;

		cali_info->power_index_offset[p] = 0;
		cali_info->delta_power_index[p] = 0;
		cali_info->delta_power_index_last[p] = 0;

		cali_info->absolute_ofdm_swing_idx[p] = 0;    /* Initial Mix mode power tracking*/
		cali_info->remnant_ofdm_swing_idx[p] = 0;
		cali_info->kfree_offset[p] = 0;
	}

	cali_info->modify_tx_agc_flag_path_a = false;       /*Initial at Modify Tx Scaling mode*/
	cali_info->modify_tx_agc_flag_path_b = false;       /*Initial at Modify Tx Scaling mode*/
	cali_info->modify_tx_agc_flag_path_c = false;       /*Initial at Modify Tx Scaling mode*/
	cali_info->modify_tx_agc_flag_path_d = false;       /*Initial at Modify Tx Scaling mode*/
	cali_info->remnant_cck_swing_idx = 0;
	cali_info->thermal_value = hal_data->eeprom_thermal_meter;

	cali_info->modify_tx_agc_value_cck = 0;			/* modify by Mingzhi.Guo */
	cali_info->modify_tx_agc_value_ofdm = 0;		/* modify by Mingzhi.Guo */

}

void
odm_txpowertracking_callback_thermal_meter(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct dm_struct		*dm
#else
	void	*adapter
#endif
)
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*hal_data = GET_HAL_DATA(((PADAPTER)adapter));
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct dm_struct		*dm = &hal_data->DM_OutSrc;
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct dm_struct		*dm = &hal_data->odmpriv;
#endif
#endif

	struct dm_rf_calibration_struct *cali_info = &(dm->rf_calibrate_info);
 	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	struct _hal_rf_ *rf = &(dm->rf_table);
	u8			thermal_value = 0, delta, delta_LCK, delta_IQK, p = 0, i = 0;
	s8			diff_DPK[4] = {0};
	u8			thermal_value_avg_count = 0;
	u32			thermal_value_avg = 0, regc80, regcd0, regcd4, regab4, regc88, rege14, reg848,reg838, reg86c;

	u8			OFDM_min_index = 0;  /* OFDM BB Swing should be less than +3.0dB, which is required by Arthur */
	u8			indexforchannel = 0; /* get_right_chnl_place_for_iqk(hal_data->current_channel) */
	u8			power_tracking_type = hal_data->RfPowerTrackingType;
	u8			xtal_offset_eanble = 0;
	s8			thermal_value_temp = 0;

	struct txpwrtrack_cfg	c;

	/* 4 1. The following TWO tables decide the final index of OFDM/CCK swing table. */
	u8			*delta_swing_table_idx_tup_a = NULL;
	u8			*delta_swing_table_idx_tdown_a = NULL;
	u8			*delta_swing_table_idx_tup_b = NULL;
	u8			*delta_swing_table_idx_tdown_b = NULL;
	/*for 8814 add by Yu Chen*/
	u8			*delta_swing_table_idx_tup_c = NULL;
	u8			*delta_swing_table_idx_tdown_c = NULL;
	u8			*delta_swing_table_idx_tup_d = NULL;
	u8			*delta_swing_table_idx_tdown_d = NULL;
	/*for Xtal Offset by James.Tung*/
	s8			*delta_swing_table_xtal_up = NULL;
	s8			*delta_swing_table_xtal_down = NULL;

	/* 4 2. Initilization ( 7 steps in total ) */

	configure_txpower_track(dm, &c);

	(*c.get_delta_swing_table)(dm, (u8 **)&delta_swing_table_idx_tup_a, (u8 **)&delta_swing_table_idx_tdown_a,
		(u8 **)&delta_swing_table_idx_tup_b, (u8 **)&delta_swing_table_idx_tdown_b);

	if (dm->support_ic_type & (ODM_RTL8814A | ODM_RTL8814B))	/*for 8814 path C & D*/
		(*c.get_delta_swing_table8814only)(dm, (u8 **)&delta_swing_table_idx_tup_c, (u8 **)&delta_swing_table_idx_tdown_c,
			(u8 **)&delta_swing_table_idx_tup_d, (u8 **)&delta_swing_table_idx_tdown_d);
	/* JJ ADD 20161014 */
	if (dm->support_ic_type & (ODM_RTL8703B | ODM_RTL8723D | ODM_RTL8710B | ODM_RTL8192F))	/*for Xtal Offset*/
		(*c.get_delta_swing_xtal_table)(dm, (s8 **)&delta_swing_table_xtal_up, (s8 **)&delta_swing_table_xtal_down);


	cali_info->txpowertracking_callback_cnt++;	/*cosa add for debug*/
	cali_info->is_txpowertracking_init = true;

	/*cali_info->txpowertrack_control = hal_data->txpowertrack_control;
	<Kordan> We should keep updating the control variable according to HalData.
	<Kordan> rf_calibrate_info.rega24 will be initialized when ODM HW configuring, but MP configures with para files. */
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
#if (MP_DRIVER == 1)
	cali_info->rega24 = 0x090e1317;
#endif
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	if (*(dm->mp_mode) == true)
		cali_info->rega24 = 0x090e1317;
#endif

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		"===>odm_txpowertracking_callback_thermal_meter\n cali_info->bb_swing_idx_cck_base: %d, cali_info->bb_swing_idx_ofdm_base[A]: %d, cali_info->default_ofdm_index: %d\n",
		cali_info->bb_swing_idx_cck_base, cali_info->bb_swing_idx_ofdm_base[RF_PATH_A], cali_info->default_ofdm_index);

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		"cali_info->txpowertrack_control=%d,  hal_data->eeprom_thermal_meter %d\n", cali_info->txpowertrack_control,  hal_data->eeprom_thermal_meter);
	thermal_value = (u8)odm_get_rf_reg(dm, RF_PATH_A, c.thermal_reg_addr, 0xfc00);	/* 0x42: RF Reg[15:10] 88E */

	thermal_value_temp = thermal_value + phydm_get_thermal_offset(dm);

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		"thermal_value_temp(%d) = thermal_value(%d) + power_time_thermal(%d)\n", thermal_value_temp, thermal_value, phydm_get_thermal_offset(dm));

	if (thermal_value_temp > 63)
		thermal_value = 63;
	else if (thermal_value_temp < 0)
		thermal_value = 0;
	else
		thermal_value = thermal_value_temp;

	/*add log by zhao he, check c80/c94/c14/ca0 value*/
	if (dm->support_ic_type == ODM_RTL8723D) {
		regc80 = odm_get_bb_reg(dm, R_0xc80, MASKDWORD);
		regcd0 = odm_get_bb_reg(dm, R_0xcd0, MASKDWORD);
		regcd4 = odm_get_bb_reg(dm, R_0xcd4, MASKDWORD);
		regab4 = odm_get_bb_reg(dm, R_0xab4, 0x000007FF);
		RF_DBG(dm, DBG_RF_IQK, "0xc80 = 0x%x 0xcd0 = 0x%x 0xcd4 = 0x%x 0xab4 = 0x%x\n", regc80, regcd0, regcd4, regab4);
	}

	/* JJ ADD 20161014 */
	if (dm->support_ic_type == ODM_RTL8710B) {
		regc80 = odm_get_bb_reg(dm, R_0xc80, MASKDWORD);
		regcd0 = odm_get_bb_reg(dm, R_0xcd0, MASKDWORD);
		regcd4 = odm_get_bb_reg(dm, R_0xcd4, MASKDWORD);
		regab4 = odm_get_bb_reg(dm, R_0xab4, 0x000007FF);
		RF_DBG(dm, DBG_RF_IQK, "0xc80 = 0x%x 0xcd0 = 0x%x 0xcd4 = 0x%x 0xab4 = 0x%x\n", regc80, regcd0, regcd4, regab4);
	}
	/* Winnita add 20171205 */
	if (dm->support_ic_type == ODM_RTL8192F) {
		regc80 = odm_get_bb_reg(dm, R_0xc80, MASKDWORD);
		regc88 = odm_get_bb_reg(dm, R_0xc88, MASKDWORD);
		regab4 = odm_get_bb_reg(dm, R_0xab4, MASKDWORD);
		rege14 = odm_get_bb_reg(dm, R_0xe14, MASKDWORD);
		reg848 = odm_get_bb_reg(dm, R_0x848, MASKDWORD);
		reg838 = odm_get_bb_reg(dm, R_0x838, MASKDWORD);
		reg86c = odm_get_bb_reg(dm, R_0x86c, MASKDWORD);
		RF_DBG(dm, DBG_RF_IQK, "0xc80 = 0x%x 0xc88 = 0x%x 0xab4 = 0x%x 0xe14 = 0x%x\n", regc80, regc88, regab4, rege14);
		RF_DBG(dm, DBG_RF_IQK, "0x848 = 0x%x 0x838 = 0x%x 0x86c = 0x%x\n", reg848, reg838, reg86c);
	}

	if (!cali_info->txpowertrack_control)
		return;

	if (hal_data->eeprom_thermal_meter == 0xff) {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "no pg, hal_data->eeprom_thermal_meter = 0x%x\n", hal_data->eeprom_thermal_meter);
		return;
	}

	/*4 3. Initialize ThermalValues of rf_calibrate_info*/

	if (cali_info->is_reloadtxpowerindex)
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "reload ofdm index for band switch\n");

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

	if (thermal_value_avg_count) {            /* Calculate Average thermal_value after average enough times */
		thermal_value = (u8)(thermal_value_avg / thermal_value_avg_count);
		cali_info->thermal_value_delta = thermal_value - hal_data->eeprom_thermal_meter;
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			"AVG Thermal Meter = 0x%X, EFUSE Thermal base = 0x%X\n", thermal_value, hal_data->eeprom_thermal_meter);
	}

	/* 4 5. Calculate delta, delta_LCK, delta_IQK. */

	/* "delta" here is used to determine whether thermal value changes or not. */
	delta	= (thermal_value > cali_info->thermal_value) ? (thermal_value - cali_info->thermal_value) : (cali_info->thermal_value - thermal_value);
	delta_LCK = (thermal_value > cali_info->thermal_value_lck) ? (thermal_value - cali_info->thermal_value_lck) : (cali_info->thermal_value_lck - thermal_value);
	delta_IQK = (thermal_value > cali_info->thermal_value_iqk) ? (thermal_value - cali_info->thermal_value_iqk) : (cali_info->thermal_value_iqk - thermal_value);

	if (cali_info->thermal_value_iqk == 0xff) {	/*no PG, use thermal value for IQK*/
		cali_info->thermal_value_iqk = thermal_value;
		delta_IQK = (thermal_value > cali_info->thermal_value_iqk) ? (thermal_value - cali_info->thermal_value_iqk) : (cali_info->thermal_value_iqk - thermal_value);
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "no PG, use thermal_value for IQK\n");
	}

	for (p = RF_PATH_A; p < c.rf_path_count; p++)
		diff_DPK[p] = (s8)thermal_value - (s8)cali_info->dpk_thermal[p];

	/*4 6. If necessary, do LCK.*/

	if (!(dm->support_ic_type & ODM_RTL8821)) {	/*no PG, do LCK at initial status*/
		if (cali_info->thermal_value_lck == 0xff) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "no PG, do LCK\n");
			cali_info->thermal_value_lck = thermal_value;

			/*Use RTLCK, so close power tracking driver LCK*/
			if ((!(dm->support_ic_type & ODM_RTL8814A)) && (!(dm->support_ic_type & ODM_RTL8822B))) {
				if (c.phy_lc_calibrate)
					(*c.phy_lc_calibrate)(dm);
			}

			delta_LCK = (thermal_value > cali_info->thermal_value_lck) ? (thermal_value - cali_info->thermal_value_lck) : (cali_info->thermal_value_lck - thermal_value);
		}

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "(delta, delta_LCK, delta_IQK) = (%d, %d, %d)\n", delta, delta_LCK, delta_IQK);

		/* Wait sacn to do LCK by RF Jenyu*/
		if( (*dm->is_scan_in_process == false) && (!iqk_info->rfk_forbidden)) {
			/* Delta temperature is equal to or larger than 20 centigrade.*/
			if (delta_LCK >= c.threshold_iqk) {
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "delta_LCK(%d) >= threshold_iqk(%d)\n", delta_LCK, c.threshold_iqk);
				cali_info->thermal_value_lck = thermal_value;

				/*Use RTLCK, so close power tracking driver LCK*/
				if ((!(dm->support_ic_type & ODM_RTL8814A)) && (!(dm->support_ic_type & ODM_RTL8822B))) {
					if (c.phy_lc_calibrate)
						(*c.phy_lc_calibrate)(dm);
				}
			}
		}
	}

	/*3 7. If necessary, move the index of swing table to adjust Tx power.*/

	if (delta > 0 && cali_info->txpowertrack_control) {
		/* "delta" here is used to record the absolute value of differrence. */
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
		delta = thermal_value > hal_data->eeprom_thermal_meter ? (thermal_value - hal_data->eeprom_thermal_meter) : (hal_data->eeprom_thermal_meter - thermal_value);
#else
		delta = (thermal_value > dm->priv->pmib->dot11RFEntry.ther) ? (thermal_value - dm->priv->pmib->dot11RFEntry.ther) : (dm->priv->pmib->dot11RFEntry.ther - thermal_value);
#endif
		if (delta >= TXPWR_TRACK_TABLE_SIZE)
			delta = TXPWR_TRACK_TABLE_SIZE - 1;

		/*4 7.1 The Final Power index = BaseIndex + power_index_offset*/

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
		if (thermal_value > hal_data->eeprom_thermal_meter) {
#else
		if (thermal_value > dm->priv->pmib->dot11RFEntry.ther) {
#endif

			for (p = RF_PATH_A; p < c.rf_path_count; p++) {
				cali_info->delta_power_index_last[p] = cali_info->delta_power_index[p];	/*recording poer index offset*/
				switch (p) {
				case RF_PATH_B:
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
						"delta_swing_table_idx_tup_b[%d] = %d\n", delta, delta_swing_table_idx_tup_b[delta]);

					cali_info->delta_power_index[p] = delta_swing_table_idx_tup_b[delta];
					cali_info->absolute_ofdm_swing_idx[p] =  delta_swing_table_idx_tup_b[delta];       /*Record delta swing for mix mode power tracking*/
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
						"******Temp is higher and cali_info->absolute_ofdm_swing_idx[RF_PATH_B] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
					break;

				case RF_PATH_C:
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
						"delta_swing_table_idx_tup_c[%d] = %d\n", delta, delta_swing_table_idx_tup_c[delta]);

					cali_info->delta_power_index[p] = delta_swing_table_idx_tup_c[delta];
					cali_info->absolute_ofdm_swing_idx[p] =  delta_swing_table_idx_tup_c[delta];       /*Record delta swing for mix mode power tracking*/
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
						"******Temp is higher and cali_info->absolute_ofdm_swing_idx[RF_PATH_C] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
					break;

				case RF_PATH_D:
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
						"delta_swing_table_idx_tup_d[%d] = %d\n", delta, delta_swing_table_idx_tup_d[delta]);

					cali_info->delta_power_index[p] = delta_swing_table_idx_tup_d[delta];
					cali_info->absolute_ofdm_swing_idx[p] =  delta_swing_table_idx_tup_d[delta];       /*Record delta swing for mix mode power tracking*/
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
						"******Temp is higher and cali_info->absolute_ofdm_swing_idx[RF_PATH_D] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
					break;

				default:
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
						"delta_swing_table_idx_tup_a[%d] = %d\n", delta, delta_swing_table_idx_tup_a[delta]);

					cali_info->delta_power_index[p] = delta_swing_table_idx_tup_a[delta];
					cali_info->absolute_ofdm_swing_idx[p] =  delta_swing_table_idx_tup_a[delta];        /*Record delta swing for mix mode power tracking*/
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
						"******Temp is higher and cali_info->absolute_ofdm_swing_idx[RF_PATH_A] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
					break;
				}
			}
			/* JJ ADD 20161014 */
			if (dm->support_ic_type & (ODM_RTL8703B | ODM_RTL8723D | ODM_RTL8710B | ODM_RTL8192F)) {
				/*Save xtal_offset from Xtal table*/
				cali_info->xtal_offset_last = cali_info->xtal_offset;	/*recording last Xtal offset*/
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					"[Xtal] delta_swing_table_xtal_up[%d] = %d\n", delta, delta_swing_table_xtal_up[delta]);
				cali_info->xtal_offset = delta_swing_table_xtal_up[delta];

				if (cali_info->xtal_offset_last == cali_info->xtal_offset)
					xtal_offset_eanble = 0;
				else
					xtal_offset_eanble = 1;
			}

		} else {
			for (p = RF_PATH_A; p < c.rf_path_count; p++) {
				cali_info->delta_power_index_last[p] = cali_info->delta_power_index[p];	/*recording poer index offset*/

				switch (p) {
				case RF_PATH_B:
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
						"delta_swing_table_idx_tdown_b[%d] = %d\n", delta, delta_swing_table_idx_tdown_b[delta]);
					cali_info->delta_power_index[p] = -1 * delta_swing_table_idx_tdown_b[delta];
					cali_info->absolute_ofdm_swing_idx[p] =  -1 * delta_swing_table_idx_tdown_b[delta];        /*Record delta swing for mix mode power tracking*/
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
						"******Temp is lower and cali_info->absolute_ofdm_swing_idx[RF_PATH_B] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
					break;

				case RF_PATH_C:
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
						"delta_swing_table_idx_tdown_c[%d] = %d\n", delta, delta_swing_table_idx_tdown_c[delta]);
					cali_info->delta_power_index[p] = -1 * delta_swing_table_idx_tdown_c[delta];
					cali_info->absolute_ofdm_swing_idx[p] =  -1 * delta_swing_table_idx_tdown_c[delta];        /*Record delta swing for mix mode power tracking*/
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
						"******Temp is lower and cali_info->absolute_ofdm_swing_idx[RF_PATH_C] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
					break;

				case RF_PATH_D:
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
						"delta_swing_table_idx_tdown_d[%d] = %d\n", delta, delta_swing_table_idx_tdown_d[delta]);
					cali_info->delta_power_index[p] = -1 * delta_swing_table_idx_tdown_d[delta];
					cali_info->absolute_ofdm_swing_idx[p] =  -1 * delta_swing_table_idx_tdown_d[delta];        /*Record delta swing for mix mode power tracking*/
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
						"******Temp is lower and cali_info->absolute_ofdm_swing_idx[RF_PATH_D] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
					break;

				default:
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
						"delta_swing_table_idx_tdown_a[%d] = %d\n", delta, delta_swing_table_idx_tdown_a[delta]);
					cali_info->delta_power_index[p] = -1 * delta_swing_table_idx_tdown_a[delta];
					cali_info->absolute_ofdm_swing_idx[p] =  -1 * delta_swing_table_idx_tdown_a[delta];        /*Record delta swing for mix mode power tracking*/
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
						"******Temp is lower and cali_info->absolute_ofdm_swing_idx[RF_PATH_A] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
					break;
				}
			}
			/* JJ ADD 20161014 */
			if (dm->support_ic_type & (ODM_RTL8703B | ODM_RTL8723D | ODM_RTL8710B | ODM_RTL8192F)) {
				/*Save xtal_offset from Xtal table*/
				cali_info->xtal_offset_last = cali_info->xtal_offset;	/*recording last Xtal offset*/
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					"[Xtal] delta_swing_table_xtal_down[%d] = %d\n", delta, delta_swing_table_xtal_down[delta]);
				cali_info->xtal_offset = delta_swing_table_xtal_down[delta];

				if (cali_info->xtal_offset_last == cali_info->xtal_offset)
					xtal_offset_eanble = 0;
				else
					xtal_offset_eanble = 1;
			}

		}

		for (p = RF_PATH_A; p < c.rf_path_count; p++) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				"\n\n=========================== [path-%d] Calculating power_index_offset===========================\n", p);

			if (cali_info->delta_power_index[p] == cali_info->delta_power_index_last[p])         /*If Thermal value changes but lookup table value still the same*/
				cali_info->power_index_offset[p] = 0;
			else
				cali_info->power_index_offset[p] = cali_info->delta_power_index[p] - cali_info->delta_power_index_last[p];      /*Power index diff between 2 times Power Tracking*/

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
	} else {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			"The thermal meter is unchanged or TxPowerTracking OFF(%d): thermal_value: %d, cali_info->thermal_value: %d\n",
			cali_info->txpowertrack_control, thermal_value, cali_info->thermal_value);

		for (p = RF_PATH_A; p < c.rf_path_count; p++)
			cali_info->power_index_offset[p] = 0;
	}

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		"TxPowerTracking: [CCK] Swing Current index: %d, Swing base index: %d\n",
		cali_info->CCK_index, cali_info->bb_swing_idx_cck_base);       /*Print Swing base & current*/

	for (p = RF_PATH_A; p < c.rf_path_count; p++) {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			"TxPowerTracking: [OFDM] Swing Current index: %d, Swing base index[%d]: %d\n",
			cali_info->OFDM_index[p], p, cali_info->bb_swing_idx_ofdm_base[p]);
	}

	if (dm->support_ic_type & ODM_RTL8814B)
		power_tracking_type = TSSI_MODE;

	if (dm->support_ic_type & (ODM_RTL8814A | ODM_RTL8814B)) {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "power_tracking_type=%d\n", power_tracking_type);

		if (power_tracking_type == 0) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking MIX_MODE**********\n");
			for (p = RF_PATH_A; p < c.rf_path_count; p++)
				(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p, 0);
		} else if (power_tracking_type == 1) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking MIX(2G) TSSI(5G) MODE**********\n");
			for (p = RF_PATH_A; p < c.rf_path_count; p++)
				(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_2G_TSSI_5G_MODE, p, 0);
		} else if (power_tracking_type == 2) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking MIX(5G) TSSI(2G)MODE**********\n");
			for (p = RF_PATH_A; p < c.rf_path_count; p++)
				(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_5G_TSSI_2G_MODE, p, 0);
		} else if (power_tracking_type == 3) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking TSSI MODE**********\n");
			for (p = RF_PATH_A; p < c.rf_path_count; p++)
				(*c.odm_tx_pwr_track_set_pwr)(dm, TSSI_MODE, p, 0);
		}
		cali_info->thermal_value = thermal_value;         /*Record last Power Tracking Thermal value*/

	} else if ((cali_info->power_index_offset[RF_PATH_A] != 0 ||
		cali_info->power_index_offset[RF_PATH_B] != 0 ||
		cali_info->power_index_offset[RF_PATH_C] != 0 ||
		cali_info->power_index_offset[RF_PATH_D] != 0) &&
		cali_info->txpowertrack_control && (hal_data->eeprom_thermal_meter != 0xff)) {
		/* 4 7.2 Configure the Swing Table to adjust Tx Power. */

		cali_info->is_tx_power_changed = true;	/*Always true after Tx Power is adjusted by power tracking.*/
		/*  */
		/* 2012/04/23 MH According to Luke's suggestion, we can not write BB digital */
		/* to increase TX power. Otherwise, EVM will be bad. */
		/*  */
		/* 2012/04/25 MH Add for tx power tracking to set tx power in tx agc for 88E. */
		if (thermal_value > cali_info->thermal_value) {
			for (p = RF_PATH_A; p < c.rf_path_count; p++) {
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					"Temperature Increasing(%d): delta_pi: %d, delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n",
					p, cali_info->power_index_offset[p], delta, thermal_value, hal_data->eeprom_thermal_meter, cali_info->thermal_value);
			}
		} else if (thermal_value < cali_info->thermal_value) {	/*Low temperature*/
			for (p = RF_PATH_A; p < c.rf_path_count; p++) {
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					"Temperature Decreasing(%d): delta_pi: %d, delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n",
					p, cali_info->power_index_offset[p], delta, thermal_value, hal_data->eeprom_thermal_meter, cali_info->thermal_value);
			}
		}

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		if (thermal_value > hal_data->eeprom_thermal_meter)
#else
		if (thermal_value > dm->priv->pmib->dot11RFEntry.ther)
#endif
		{
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				"Temperature(%d) higher than PG value(%d)\n", thermal_value, hal_data->eeprom_thermal_meter);

			if (dm->support_ic_type == ODM_RTL8188E || dm->support_ic_type == ODM_RTL8192E || dm->support_ic_type == ODM_RTL8821 ||
			    dm->support_ic_type == ODM_RTL8812 || dm->support_ic_type == ODM_RTL8723B || dm->support_ic_type == ODM_RTL8814A ||
			    dm->support_ic_type == ODM_RTL8703B || dm->support_ic_type == ODM_RTL8188F || dm->support_ic_type == ODM_RTL8822B ||
			    dm->support_ic_type == ODM_RTL8723D || dm->support_ic_type == ODM_RTL8821C || dm->support_ic_type == ODM_RTL8710B ||
			    dm->support_ic_type == ODM_RTL8192F) {

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
				"Temperature(%d) lower than PG value(%d)\n", thermal_value, hal_data->eeprom_thermal_meter);

			if (dm->support_ic_type == ODM_RTL8188E || dm->support_ic_type == ODM_RTL8192E || dm->support_ic_type == ODM_RTL8821 ||
			    dm->support_ic_type == ODM_RTL8812 || dm->support_ic_type == ODM_RTL8723B || dm->support_ic_type == ODM_RTL8814A ||
			    dm->support_ic_type == ODM_RTL8703B || dm->support_ic_type == ODM_RTL8188F || dm->support_ic_type == ODM_RTL8822B ||
			    dm->support_ic_type == ODM_RTL8723D || dm->support_ic_type == ODM_RTL8821C || dm->support_ic_type == ODM_RTL8710B ||
				dm->support_ic_type == ODM_RTL8192F) {
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking MIX_MODE**********\n");
				for (p = RF_PATH_A; p < c.rf_path_count; p++)
					(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p, indexforchannel);
			} else {
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking BBSWING_MODE**********\n");
				for (p = RF_PATH_A; p < c.rf_path_count; p++)
					(*c.odm_tx_pwr_track_set_pwr)(dm, BBSWING, p, indexforchannel);
			}

		}

		cali_info->bb_swing_idx_cck_base = cali_info->bb_swing_idx_cck;    /*Record last time Power Tracking result as base.*/
		for (p = RF_PATH_A; p < c.rf_path_count; p++)
			cali_info->bb_swing_idx_ofdm_base[p] = cali_info->bb_swing_idx_ofdm[p];

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			"cali_info->thermal_value = %d thermal_value= %d\n", cali_info->thermal_value, thermal_value);

		cali_info->thermal_value = thermal_value;         /*Record last Power Tracking Thermal value*/

	}


	if (dm->support_ic_type == ODM_RTL8703B || dm->support_ic_type == ODM_RTL8723D ||
		dm->support_ic_type == ODM_RTL8192F || dm->support_ic_type == ODM_RTL8710B) {/* JJ ADD 20161014 */

		if (xtal_offset_eanble != 0 && cali_info->txpowertrack_control && (hal_data->eeprom_thermal_meter != 0xff)) {

			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter Xtal Tracking**********\n");

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			if (thermal_value > hal_data->eeprom_thermal_meter) {
#else
			if (thermal_value > dm->priv->pmib->dot11RFEntry.ther) {
#endif
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					"Temperature(%d) higher than PG value(%d)\n", thermal_value, hal_data->eeprom_thermal_meter);
				(*c.odm_txxtaltrack_set_xtal)(dm);
			} else {
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					"Temperature(%d) lower than PG value(%d)\n", thermal_value, hal_data->eeprom_thermal_meter);
				(*c.odm_txxtaltrack_set_xtal)(dm);
			}
		}
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********End Xtal Tracking**********\n");
	}

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)

	/* Wait sacn to do IQK by RF Jenyu*/
	if ((*dm->is_scan_in_process == false) && (!iqk_info->rfk_forbidden) && dm->is_linked) {
		if (!IS_HARDWARE_TYPE_8723B(adapter)) {
			/*Delta temperature is equal to or larger than 20 centigrade (When threshold is 8).*/
			if (delta_IQK >= c.threshold_iqk) {
				cali_info->thermal_value_iqk = thermal_value;
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "delta_IQK(%d) >= threshold_iqk(%d)\n", delta_IQK, c.threshold_iqk);
				if (!cali_info->is_iqk_in_progress)
					(*c.do_iqk)(dm, delta_IQK, thermal_value, 8);
			}
		}
	}
	if (cali_info->dpk_thermal[RF_PATH_A] != 0) {
		if (diff_DPK[RF_PATH_A] >= c.threshold_dpk) {
			odm_set_bb_reg(dm, R_0x82c, BIT(31), 0x1);
			odm_set_bb_reg(dm, R_0xcc4, BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10), (diff_DPK[RF_PATH_A] / c.threshold_dpk));
			odm_set_bb_reg(dm, R_0x82c, BIT(31), 0x0);
		} else if ((diff_DPK[RF_PATH_A] <= -1 * c.threshold_dpk)) {
			s32 value = 0x20 + (diff_DPK[RF_PATH_A] / c.threshold_dpk);

			odm_set_bb_reg(dm, R_0x82c, BIT(31), 0x1);
			odm_set_bb_reg(dm, R_0xcc4, BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10), value);
			odm_set_bb_reg(dm, R_0x82c, BIT(31), 0x0);
		} else {
			odm_set_bb_reg(dm, R_0x82c, BIT(31), 0x1);
			odm_set_bb_reg(dm, R_0xcc4, BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10), 0);
			odm_set_bb_reg(dm, R_0x82c, BIT(31), 0x0);
		}
	}
	if (cali_info->dpk_thermal[RF_PATH_B] != 0) {
		if (diff_DPK[RF_PATH_B] >= c.threshold_dpk) {
			odm_set_bb_reg(dm, R_0x82c, BIT(31), 0x1);
			odm_set_bb_reg(dm, R_0xec4, BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10), (diff_DPK[RF_PATH_B] / c.threshold_dpk));
			odm_set_bb_reg(dm, R_0x82c, BIT(31), 0x0);
		} else if ((diff_DPK[RF_PATH_B] <= -1 * c.threshold_dpk)) {
			s32 value = 0x20 + (diff_DPK[RF_PATH_B] / c.threshold_dpk);

			odm_set_bb_reg(dm, R_0x82c, BIT(31), 0x1);
			odm_set_bb_reg(dm, R_0xec4, BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10), value);
			odm_set_bb_reg(dm, R_0x82c, BIT(31), 0x0);
		} else {
			odm_set_bb_reg(dm, R_0x82c, BIT(31), 0x1);
			odm_set_bb_reg(dm, R_0xec4, BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10), 0);
			odm_set_bb_reg(dm, R_0x82c, BIT(31), 0x0);
		}
	}

#endif

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "<===odm_txpowertracking_callback_thermal_meter\n");

	cali_info->tx_powercount = 0;
}

#if (RTL8822C_SUPPORT == 1 || RTL8814B_SUPPORT == 1)
void
odm_txpowertracking_new_callback_thermal_meter(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &(dm->rf_calibrate_info);
 	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	struct _hal_rf_ *rf = &dm->rf_table;
	struct _halrf_tssi_data *tssi = &rf->halrf_tssi_data;
	u8 thermal_value[MAX_RF_PATH] = {0}, delta[MAX_RF_PATH] = {0};
	u8 delta_swing_table_idx_tup[DELTA_SWINGIDX_SIZE] = {0};
	u8 delta_swing_table_idx_tdown[DELTA_SWINGIDX_SIZE] = {0};
	u8 delta_LCK = 0, delta_IQK = 0, i = 0, j = 0, p;
	u8 thermal_value_avg_count[MAX_RF_PATH] = {0};
	u32 thermal_value_avg[MAX_RF_PATH] = {0};
	s8 thermal_value_temp[MAX_RF_PATH] = {0};
	u8 tracking_method = MIX_MODE;

	struct txpwrtrack_cfg c;

	u8 *delta_swing_table_idx_tup_a = NULL;
	u8 *delta_swing_table_idx_tdown_a = NULL;
	u8 *delta_swing_table_idx_tup_b = NULL;
	u8 *delta_swing_table_idx_tdown_b = NULL;
	u8 *delta_swing_table_idx_tup_c = NULL;
	u8 *delta_swing_table_idx_tdown_c = NULL;
	u8 *delta_swing_table_idx_tup_d = NULL;
	u8 *delta_swing_table_idx_tdown_d = NULL;

	configure_txpower_track(dm, &c);

	(*c.get_delta_swing_table)(dm, (u8 **)&delta_swing_table_idx_tup_a, (u8 **)&delta_swing_table_idx_tdown_a,
		(u8 **)&delta_swing_table_idx_tup_b, (u8 **)&delta_swing_table_idx_tdown_b);

	if (dm->support_ic_type == ODM_RTL8814B) {
		(*c.get_delta_swing_table)(dm, (u8 **)&delta_swing_table_idx_tup_c, (u8 **)&delta_swing_table_idx_tdown_c,
			(u8 **)&delta_swing_table_idx_tup_d, (u8 **)&delta_swing_table_idx_tdown_d);
	}

	cali_info->txpowertracking_callback_cnt++;
	cali_info->is_txpowertracking_init = true;

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		"===>odm_txpowertracking_callback_thermal_meter\n cali_info->bb_swing_idx_cck_base: %d, cali_info->bb_swing_idx_ofdm_base[A]: %d, cali_info->default_ofdm_index: %d\n",
		cali_info->bb_swing_idx_cck_base, cali_info->bb_swing_idx_ofdm_base[RF_PATH_A], cali_info->default_ofdm_index);

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		"cali_info->txpowertrack_control=%d, tssi->thermal[RF_PATH_A]=%d tssi->thermal[RF_PATH_B]=%d\n",
		cali_info->txpowertrack_control,  tssi->thermal[RF_PATH_A], tssi->thermal[RF_PATH_B]);

	if (dm->support_ic_type == ODM_RTL8822C) {
		for (i = 0; i < c.rf_path_count; i++)
			thermal_value[i] = (u8)odm_get_rf_reg(dm, i, c.thermal_reg_addr, 0x7e);	/* 0x42: RF Reg[6:1] Thermal Trim*/
	} else {
		for (i = 0; i < c.rf_path_count; i++) {
			thermal_value[i] = (u8)odm_get_rf_reg(dm, i, c.thermal_reg_addr, 0xfc00);	/* 0x42: RF Reg[15:10] 88E */

			if (dm->support_ic_type == ODM_RTL8814B) {
				thermal_value_temp[i] = (s8)thermal_value[i] + phydm_get_multi_thermal_offset(dm, i);
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					"thermal_value_temp[%d](%d) = thermal_value[%d](%d) + multi_thermal_trim(%d)\n", i, thermal_value_temp[i], i, thermal_value[i], phydm_get_multi_thermal_offset(dm, i));
			} else {
				thermal_value_temp[i] = (s8)thermal_value[i] + phydm_get_thermal_offset(dm);
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
					"thermal_value_temp[%d](%d) = thermal_value[%d](%d) + thermal_trim(%d)\n", i, thermal_value_temp[i], i, thermal_value[i], phydm_get_thermal_offset(dm));
			}

			if (thermal_value_temp[i] > 63)
				thermal_value[i] = 63;
			else if (thermal_value_temp[i] < 0)
				thermal_value[i] = 0;
			else
				thermal_value[i] = thermal_value_temp[i];
		}
	}

	if ((tssi->thermal[RF_PATH_A] == 0xff || tssi->thermal[RF_PATH_B] == 0xff)) {
		for (i = 0; i < c.rf_path_count; i++)
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "no pg, tssi->thermal[%d] = 0x%x\n",
				i, tssi->thermal[i]);
		return;
	}

	for (j = 0; j < c.rf_path_count; j++) {
		cali_info->thermal_value_avg_path[j][cali_info->thermal_value_avg_index_path[j]] = thermal_value[j];
		cali_info->thermal_value_avg_index_path[j]++;
		if (cali_info->thermal_value_avg_index_path[j] == c.average_thermal_num)   /*Average times =  c.average_thermal_num*/
			cali_info->thermal_value_avg_index_path[j] = 0;


		for (i = 0; i < c.average_thermal_num; i++) {
			if (cali_info->thermal_value_avg_path[j][i]) {
				thermal_value_avg[j] += cali_info->thermal_value_avg_path[j][i];
				thermal_value_avg_count[j]++;
			}
		}

		if (thermal_value_avg_count[j]) {            /* Calculate Average thermal_value after average enough times */
			thermal_value[j] = (u8)(thermal_value_avg[j] / thermal_value_avg_count[j]);
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				"AVG Thermal Meter = 0x%X, tssi->thermal[%d] = 0x%x\n",
				thermal_value[j], j, tssi->thermal[j]);
		}
		/* 4 5. Calculate delta, delta_LCK, delta_IQK. */

		/* "delta" here is used to determine whether thermal value changes or not. */
		delta[j] = (thermal_value[j] > cali_info->thermal_value_path[j]) ? (thermal_value[j] - cali_info->thermal_value_path[j]) : (cali_info->thermal_value_path[j] - thermal_value[j]);
		delta_LCK = (thermal_value[0] > cali_info->thermal_value_lck) ? (thermal_value[0] - cali_info->thermal_value_lck) : (cali_info->thermal_value_lck - thermal_value[0]);
		delta_IQK = (thermal_value[0] > cali_info->thermal_value_iqk) ? (thermal_value[0] - cali_info->thermal_value_iqk) : (cali_info->thermal_value_iqk - thermal_value[0]);
	}

	/*4 6. If necessary, do LCK.*/

	for (i = 0; i < c.rf_path_count; i++)
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "(delta[%d], delta_LCK, delta_IQK) = (%d, %d, %d)\n", i, delta[i], delta_LCK, delta_IQK);

	/* Wait sacn to do LCK by RF Jenyu*/
	if( (*dm->is_scan_in_process == false) && (!iqk_info->rfk_forbidden)) {
		/* Delta temperature is equal to or larger than 20 centigrade.*/
		if (delta_LCK >= c.threshold_iqk) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "delta_LCK(%d) >= threshold_iqk(%d)\n", delta_LCK, c.threshold_iqk);
			cali_info->thermal_value_lck = thermal_value[RF_PATH_A];

			/*Use RTLCK, so close power tracking driver LCK*/
			if ((!(dm->support_ic_type & ODM_RTL8814A)) && (!(dm->support_ic_type & ODM_RTL8822B))) {
				if (c.phy_lc_calibrate)
					(*c.phy_lc_calibrate)(dm);
			} else
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "Do not do LCK\n");
		}
	}

	/*3 7. If necessary, move the index of swing table to adjust Tx power.*/
	for (i = 0; i < c.rf_path_count; i++) {
		if (i == RF_PATH_B) {
			odm_move_memory(dm, delta_swing_table_idx_tup, delta_swing_table_idx_tup_b, DELTA_SWINGIDX_SIZE);
			odm_move_memory(dm, delta_swing_table_idx_tdown, delta_swing_table_idx_tdown_b, DELTA_SWINGIDX_SIZE);
		} else if (i == RF_PATH_C) {
			odm_move_memory(dm, delta_swing_table_idx_tup, delta_swing_table_idx_tup_c, DELTA_SWINGIDX_SIZE);
			odm_move_memory(dm, delta_swing_table_idx_tdown, delta_swing_table_idx_tdown_c, DELTA_SWINGIDX_SIZE);
		} else if (i == RF_PATH_D) {
			odm_move_memory(dm, delta_swing_table_idx_tup, delta_swing_table_idx_tup_d, DELTA_SWINGIDX_SIZE);
			odm_move_memory(dm, delta_swing_table_idx_tdown, delta_swing_table_idx_tdown_d, DELTA_SWINGIDX_SIZE);
		} else {
			odm_move_memory(dm, delta_swing_table_idx_tup, delta_swing_table_idx_tup_a, DELTA_SWINGIDX_SIZE);
			odm_move_memory(dm, delta_swing_table_idx_tdown, delta_swing_table_idx_tdown_a, DELTA_SWINGIDX_SIZE);
		}

		cali_info->delta_power_index_last[i] = cali_info->delta_power_index[i];	/*recording poer index offset*/
		delta[i] = thermal_value[i] > tssi->thermal[i] ? (thermal_value[i] - tssi->thermal[i]) : (tssi->thermal[i] - thermal_value[i]);
				
		if (delta[i] >= TXPWR_TRACK_TABLE_SIZE)
			delta[i] = TXPWR_TRACK_TABLE_SIZE - 1;

		if (thermal_value[i] > tssi->thermal[i]) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				"delta_swing_table_idx_tup[%d]=%d Path=%d\n", delta[i], delta_swing_table_idx_tup[delta[i]], i);
				
			cali_info->delta_power_index[i] = delta_swing_table_idx_tup[delta[i]];
			cali_info->absolute_ofdm_swing_idx[i] =  delta_swing_table_idx_tup[delta[i]];	    /*Record delta swing for mix mode power tracking*/
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				"******Temp is higher and cali_info->absolute_ofdm_swing_idx[%d]=%d Path=%d\n", delta[i], cali_info->absolute_ofdm_swing_idx[i], i);
		} else {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				"delta_swing_table_idx_tdown[%d]=%d Path=%d\n", delta[i], delta_swing_table_idx_tdown[delta[i]], i);
			cali_info->delta_power_index[i] = -1 * delta_swing_table_idx_tdown[delta[i]];
			cali_info->absolute_ofdm_swing_idx[i] = -1 * delta_swing_table_idx_tdown[delta[i]];        /*Record delta swing for mix mode power tracking*/
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				"******Temp is lower and cali_info->absolute_ofdm_swing_idx[%d]=%d Path=%d\n", delta[i], cali_info->absolute_ofdm_swing_idx[i], i);
		}
	}

	for (p = RF_PATH_A; p < c.rf_path_count; p++) {	
		if (cali_info->delta_power_index[p] == cali_info->delta_power_index_last[p])	     /*If Thermal value changes but lookup table value still the same*/
			cali_info->power_index_offset[p] = 0;
		else
			cali_info->power_index_offset[p] = cali_info->delta_power_index[p] - cali_info->delta_power_index_last[p];	/*Power index diff between 2 times Power Tracking*/
	}

#if 0
	if (dm->support_ic_type == ODM_RTL8822C) {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking MIX_MODE**********\n");
		for (p = RF_PATH_A; p < c.rf_path_count; p++)
			(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p, 0);
	} else {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking BBSWING_MODE**********\n");
		for (p = RF_PATH_A; p < c.rf_path_count; p++)
			(*c.odm_tx_pwr_track_set_pwr)(dm, BBSWING, p, 0);
	}
#endif
	if (*dm->mp_mode == 1) {
		if (cali_info->txpowertrack_control == 1) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking MIX_MODE**********\n");
			tracking_method = MIX_MODE;
		} else if (cali_info->txpowertrack_control == 3) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking TSSI_MODE**********\n");
			tracking_method = TSSI_MODE;
		}	
	} else {
		if (rf->power_track_type >= 0 && rf->power_track_type <= 3) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking MIX_MODE**********\n");
			tracking_method = MIX_MODE;
		} else if (rf->power_track_type >= 4 && rf->power_track_type <= 7) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking TSSI_MODE**********\n");
			tracking_method = TSSI_MODE;
		}	
	}

	if (dm->support_ic_type == ODM_RTL8822C || dm->support_ic_type == ODM_RTL8814B)
		for (p = RF_PATH_A; p < c.rf_path_count; p++)
			(*c.odm_tx_pwr_track_set_pwr)(dm, tracking_method, p, 0);

	/* Wait sacn to do IQK by RF Jenyu*/
	if ((*dm->is_scan_in_process == false) && (!iqk_info->rfk_forbidden) && (dm->is_linked || *dm->mp_mode)) {
		/*Delta temperature is equal to or larger than 20 centigrade (When threshold is 8).*/
		if (delta_IQK >= c.threshold_iqk) {
			cali_info->thermal_value_iqk = thermal_value[RF_PATH_A];
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "delta_IQK(%d) >= threshold_iqk(%d)\n", delta_IQK, c.threshold_iqk);
			/*if (!cali_info->is_iqk_in_progress)*/
			/*	(*c.do_iqk)(dm, delta_IQK, thermal_value[RF_PATH_A], 8);*/
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "Do IQK\n");

			/*if (!cali_info->is_iqk_in_progress)*/
			/*	(*c.do_tssi_dck)(dm, true);*/
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "Do TSSI DCK\n");
		}
	}

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "<===%s\n", __func__);

	cali_info->tx_powercount = 0;
}
#endif

/* 3============================================================
 * 3 IQ Calibration
 * 3============================================================ */

void
odm_reset_iqk_result(
	struct dm_struct	*dm
)
{
	return;
}
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
u8 odm_get_right_chnl_place_for_iqk(u8 chnl)
{
	u8	channel_all[ODM_TARGET_CHNL_NUM_2G_5G] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122, 124, 126, 128, 130, 132, 134, 136, 138, 140, 149, 151, 153, 155, 157, 159, 161, 163, 165
	};
	u8	place = chnl;


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
odm_iq_calibrate(
	struct dm_struct	*dm
)
{
	void	*adapter = dm->adapter;
	struct dm_iqk_info	*iqk_info = &dm->IQK_info;
	
	RF_DBG(dm, DBG_RF_IQK, "=>%s\n",__FUNCTION__);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if (*dm->is_fcs_mode_enable)
		return;
#endif
	if (dm->is_linked) {
		RF_DBG(dm, DBG_RF_IQK,
		       "interval=%d ch=%d prech=%d scan=%s rfk_f =%s\n",
		       dm->linked_interval, *dm->channel,  dm->pre_channel,
		       *dm->is_scan_in_process == TRUE ? "TRUE":"FALSE",
		       iqk_info->rfk_forbidden == TRUE ? "TRUE":"FALSE");

		if (iqk_info->rfk_forbidden)	{
			RF_DBG(dm, DBG_RF_IQK, "return by rfk_forbidden\n");
			return;
		}

		if (*dm->is_scan_in_process)	{
			RF_DBG(dm, DBG_RF_IQK, "return by is_scan_in_process\n");
			return;
		}

		if (*dm->channel != dm->pre_channel) {
			dm->pre_channel = *dm->channel;
			dm->linked_interval = 0;
		}

		if (dm->linked_interval < 3)
			dm->linked_interval++;

		if (dm->linked_interval == 2)
			PHY_IQCalibrate(adapter, false);
	} else {
		dm->linked_interval = 0;
		RF_DBG(dm, DBG_RF_IQK, "is_linked =%s, interval =%d\n",
		       dm->is_linked == TRUE ? "TRUE":"FALSE",
		       dm->linked_interval);		
	}
}

void phydm_rf_init(struct dm_struct		*dm)
{

	odm_txpowertracking_init(dm);

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	odm_clear_txpowertracking_state(dm);
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
#if (RTL8814A_SUPPORT == 1)
	if (dm->support_ic_type & ODM_RTL8814A)
		phy_iq_calibrate_8814a_init(dm);
#endif
#endif

}

void phydm_rf_watchdog(struct dm_struct *dm)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	FunctionIn(COMP_MLME);

	if (*dm->mp_mode == 1) {
#if (MP_DRIVER == 1)
		odm_txpowertracking_check(dm);
#endif
	} else {
		odm_txpowertracking_check(dm);

		if (dm->support_ic_type & (ODM_IC_11AC_SERIES |  ODM_IC_JGR3_SERIES))
			odm_iq_calibrate(dm);
	}
#endif
}
