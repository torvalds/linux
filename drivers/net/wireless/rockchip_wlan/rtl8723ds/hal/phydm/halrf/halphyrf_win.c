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
	struct PHY_DM_STRUCT		*p_dm,
	struct _TXPWRTRACK_CFG	*p_config
)
{
#if RTL8192E_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8192E)
		configure_txpower_track_8192e(p_config);
#endif
#if RTL8821A_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8821)
		configure_txpower_track_8821a(p_config);
#endif
#if RTL8812A_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8812)
		configure_txpower_track_8812a(p_config);
#endif
#if RTL8188E_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8188E)
		configure_txpower_track_8188e(p_config);
#endif

#if RTL8188F_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8188F)
		configure_txpower_track_8188f(p_config);
#endif

#if RTL8723B_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8723B)
		configure_txpower_track_8723b(p_config);
#endif

#if RTL8814A_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8814A)
		configure_txpower_track_8814a(p_config);
#endif

#if RTL8703B_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8703B)
		configure_txpower_track_8703b(p_config);
#endif

#if RTL8822B_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8822B)
		configure_txpower_track_8822b(p_config);
#endif

#if RTL8723D_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8723D)
		configure_txpower_track_8723d(p_config);
#endif

/* JJ ADD 20161014 */
#if RTL8710B_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8710B)
		configure_txpower_track_8710b(p_config);
#endif

#if RTL8821C_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8821C)
		configure_txpower_track_8821c(p_config);
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
	struct PHY_DM_STRUCT		*p_dm
)
{
	PHAL_DATA_TYPE	p_hal_data = GET_HAL_DATA(p_dm->adapter);
	u8			p = 0;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);

	p_rf_calibrate_info->bb_swing_idx_cck_base = p_rf_calibrate_info->default_cck_index;
	p_rf_calibrate_info->bb_swing_idx_cck = p_rf_calibrate_info->default_cck_index;
	p_rf_calibrate_info->CCK_index = 0;

	for (p = RF_PATH_A; p < MAX_RF_PATH; ++p) {
		p_rf_calibrate_info->bb_swing_idx_ofdm_base[p] = p_rf_calibrate_info->default_ofdm_index;
		p_rf_calibrate_info->bb_swing_idx_ofdm[p] = p_rf_calibrate_info->default_ofdm_index;
		p_rf_calibrate_info->OFDM_index[p] = p_rf_calibrate_info->default_ofdm_index;

		p_rf_calibrate_info->power_index_offset[p] = 0;
		p_rf_calibrate_info->delta_power_index[p] = 0;
		p_rf_calibrate_info->delta_power_index_last[p] = 0;

		p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = 0;    /* Initial Mix mode power tracking*/
		p_rf_calibrate_info->remnant_ofdm_swing_idx[p] = 0;
		p_rf_calibrate_info->kfree_offset[p] = 0;
	}

	p_rf_calibrate_info->modify_tx_agc_flag_path_a = false;       /*Initial at Modify Tx Scaling mode*/
	p_rf_calibrate_info->modify_tx_agc_flag_path_b = false;       /*Initial at Modify Tx Scaling mode*/
	p_rf_calibrate_info->modify_tx_agc_flag_path_c = false;       /*Initial at Modify Tx Scaling mode*/
	p_rf_calibrate_info->modify_tx_agc_flag_path_d = false;       /*Initial at Modify Tx Scaling mode*/
	p_rf_calibrate_info->remnant_cck_swing_idx = 0;
	p_rf_calibrate_info->thermal_value = p_hal_data->eeprom_thermal_meter;

	p_rf_calibrate_info->modify_tx_agc_value_cck = 0;			/* modify by Mingzhi.Guo */
	p_rf_calibrate_info->modify_tx_agc_value_ofdm = 0;		/* modify by Mingzhi.Guo */

}

void
odm_txpowertracking_callback_thermal_meter(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm
#else
	struct _ADAPTER	*adapter
#endif
)
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->DM_OutSrc;
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->odmpriv;
#endif
#endif

	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
 	struct _IQK_INFORMATION	*p_iqk_info = &p_dm->IQK_info;
	u8			thermal_value = 0, delta, delta_LCK, delta_IQK, p = 0, i = 0;
	s8			diff_DPK[4] = {0};
	u8			thermal_value_avg_count = 0;
	u32			thermal_value_avg = 0, regc80, regcd0, regcd4, regab4;

	u8			OFDM_min_index = 0;  /* OFDM BB Swing should be less than +3.0dB, which is required by Arthur */
	u8			indexforchannel = 0; /* get_right_chnl_place_for_iqk(p_hal_data->current_channel) */
	u8			power_tracking_type = p_hal_data->RfPowerTrackingType;
	u8			xtal_offset_eanble = 0;
	s8			thermal_value_temp = 0;

	struct _TXPWRTRACK_CFG	c;

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

	configure_txpower_track(p_dm, &c);

	(*c.get_delta_swing_table)(p_dm, (u8 **)&delta_swing_table_idx_tup_a, (u8 **)&delta_swing_table_idx_tdown_a,
		(u8 **)&delta_swing_table_idx_tup_b, (u8 **)&delta_swing_table_idx_tdown_b);

	if (p_dm->support_ic_type & ODM_RTL8814A)	/*for 8814 path C & D*/
		(*c.get_delta_swing_table8814only)(p_dm, (u8 **)&delta_swing_table_idx_tup_c, (u8 **)&delta_swing_table_idx_tdown_c,
			(u8 **)&delta_swing_table_idx_tup_d, (u8 **)&delta_swing_table_idx_tdown_d);
	/* JJ ADD 20161014 */
	if (p_dm->support_ic_type & (ODM_RTL8703B | ODM_RTL8723D | ODM_RTL8710B))	/*for Xtal Offset*/
		(*c.get_delta_swing_xtal_table)(p_dm, (s8 **)&delta_swing_table_xtal_up, (s8 **)&delta_swing_table_xtal_down);


	p_rf_calibrate_info->txpowertracking_callback_cnt++;	/*cosa add for debug*/
	p_rf_calibrate_info->is_txpowertracking_init = true;

	/*p_rf_calibrate_info->txpowertrack_control = p_hal_data->txpowertrack_control;
	<Kordan> We should keep updating the control variable according to HalData.
	<Kordan> rf_calibrate_info.rega24 will be initialized when ODM HW configuring, but MP configures with para files. */
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
#if (MP_DRIVER == 1)
	p_rf_calibrate_info->rega24 = 0x090e1317;
#endif
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	if (*(p_dm->p_mp_mode) == true)
		p_rf_calibrate_info->rega24 = 0x090e1317;
#endif

	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("===>odm_txpowertracking_callback_thermal_meter\n p_rf_calibrate_info->bb_swing_idx_cck_base: %d, p_rf_calibrate_info->bb_swing_idx_ofdm_base[A]: %d, p_rf_calibrate_info->default_ofdm_index: %d\n",
		p_rf_calibrate_info->bb_swing_idx_cck_base, p_rf_calibrate_info->bb_swing_idx_ofdm_base[RF_PATH_A], p_rf_calibrate_info->default_ofdm_index));

	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("p_rf_calibrate_info->txpowertrack_control=%d,  p_hal_data->eeprom_thermal_meter %d\n", p_rf_calibrate_info->txpowertrack_control,  p_hal_data->eeprom_thermal_meter));
	thermal_value = (u8)odm_get_rf_reg(p_dm, RF_PATH_A, c.thermal_reg_addr, 0xfc00);	/* 0x42: RF Reg[15:10] 88E */

	thermal_value_temp = thermal_value + phydm_get_thermal_offset(p_dm);

	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("thermal_value_temp(%d) = thermal_value(%d) + power_time_thermal(%d)\n", thermal_value_temp, thermal_value, phydm_get_thermal_offset(p_dm)));

	if (thermal_value_temp > 63)
		thermal_value = 63;
	else if (thermal_value_temp < 0)
		thermal_value = 0;
	else
		thermal_value = thermal_value_temp;

	/*add log by zhao he, check c80/c94/c14/ca0 value*/
	if (p_dm->support_ic_type == ODM_RTL8723D) {
		regc80 = odm_get_bb_reg(p_dm, 0xc80, MASKDWORD);
		regcd0 = odm_get_bb_reg(p_dm, 0xcd0, MASKDWORD);
		regcd4 = odm_get_bb_reg(p_dm, 0xcd4, MASKDWORD);
		regab4 = odm_get_bb_reg(p_dm, 0xab4, 0x000007FF);
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xc80 = 0x%x 0xcd0 = 0x%x 0xcd4 = 0x%x 0xab4 = 0x%x\n", regc80, regcd0, regcd4, regab4));
	}

	/* JJ ADD 20161014 */
	if (p_dm->support_ic_type == ODM_RTL8710B) {
		regc80 = odm_get_bb_reg(p_dm, 0xc80, MASKDWORD);
		regcd0 = odm_get_bb_reg(p_dm, 0xcd0, MASKDWORD);
		regcd4 = odm_get_bb_reg(p_dm, 0xcd4, MASKDWORD);
		regab4 = odm_get_bb_reg(p_dm, 0xab4, 0x000007FF);
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0xc80 = 0x%x 0xcd0 = 0x%x 0xcd4 = 0x%x 0xab4 = 0x%x\n", regc80, regcd0, regcd4, regab4));
	}

	if (!p_rf_calibrate_info->txpowertrack_control)
		return;

	if (p_hal_data->eeprom_thermal_meter == 0xff) {
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("no pg, p_hal_data->eeprom_thermal_meter = 0x%x\n", p_hal_data->eeprom_thermal_meter));
		return;
	}

	/*4 3. Initialize ThermalValues of rf_calibrate_info*/

	if (p_rf_calibrate_info->is_reloadtxpowerindex)
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("reload ofdm index for band switch\n"));

	/*4 4. Calculate average thermal meter*/

	p_rf_calibrate_info->thermal_value_avg[p_rf_calibrate_info->thermal_value_avg_index] = thermal_value;
	p_rf_calibrate_info->thermal_value_avg_index++;
	if (p_rf_calibrate_info->thermal_value_avg_index == c.average_thermal_num)   /*Average times =  c.average_thermal_num*/
		p_rf_calibrate_info->thermal_value_avg_index = 0;

	for (i = 0; i < c.average_thermal_num; i++) {
		if (p_rf_calibrate_info->thermal_value_avg[i]) {
			thermal_value_avg += p_rf_calibrate_info->thermal_value_avg[i];
			thermal_value_avg_count++;
		}
	}

	if (thermal_value_avg_count) {            /* Calculate Average thermal_value after average enough times */
		thermal_value = (u8)(thermal_value_avg / thermal_value_avg_count);
		p_rf_calibrate_info->thermal_value_delta = thermal_value - p_hal_data->eeprom_thermal_meter;
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("AVG Thermal Meter = 0x%X, EFUSE Thermal base = 0x%X\n", thermal_value, p_hal_data->eeprom_thermal_meter));
	}

	/* 4 5. Calculate delta, delta_LCK, delta_IQK. */

	/* "delta" here is used to determine whether thermal value changes or not. */
	delta	= (thermal_value > p_rf_calibrate_info->thermal_value) ? (thermal_value - p_rf_calibrate_info->thermal_value) : (p_rf_calibrate_info->thermal_value - thermal_value);
	delta_LCK = (thermal_value > p_rf_calibrate_info->thermal_value_lck) ? (thermal_value - p_rf_calibrate_info->thermal_value_lck) : (p_rf_calibrate_info->thermal_value_lck - thermal_value);
	delta_IQK = (thermal_value > p_rf_calibrate_info->thermal_value_iqk) ? (thermal_value - p_rf_calibrate_info->thermal_value_iqk) : (p_rf_calibrate_info->thermal_value_iqk - thermal_value);

	if (p_rf_calibrate_info->thermal_value_iqk == 0xff) {	/*no PG, use thermal value for IQK*/
		p_rf_calibrate_info->thermal_value_iqk = thermal_value;
		delta_IQK = (thermal_value > p_rf_calibrate_info->thermal_value_iqk) ? (thermal_value - p_rf_calibrate_info->thermal_value_iqk) : (p_rf_calibrate_info->thermal_value_iqk - thermal_value);
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("no PG, use thermal_value for IQK\n"));
	}

	for (p = RF_PATH_A; p < c.rf_path_count; p++)
		diff_DPK[p] = (s8)thermal_value - (s8)p_rf_calibrate_info->dpk_thermal[p];

	/*4 6. If necessary, do LCK.*/

	if (!(p_dm->support_ic_type & ODM_RTL8821)) {	/*no PG, do LCK at initial status*/
		if (p_rf_calibrate_info->thermal_value_lck == 0xff) {
			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("no PG, do LCK\n"));
			p_rf_calibrate_info->thermal_value_lck = thermal_value;

			/*Use RTLCK, so close power tracking driver LCK*/
			if ((!(p_dm->support_ic_type & ODM_RTL8814A)) && (!(p_dm->support_ic_type & ODM_RTL8822B))) {
				if (c.phy_lc_calibrate)
					(*c.phy_lc_calibrate)(p_dm);
			}

			delta_LCK = (thermal_value > p_rf_calibrate_info->thermal_value_lck) ? (thermal_value - p_rf_calibrate_info->thermal_value_lck) : (p_rf_calibrate_info->thermal_value_lck - thermal_value);
		}

		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("(delta, delta_LCK, delta_IQK) = (%d, %d, %d)\n", delta, delta_LCK, delta_IQK));

		/* Wait sacn to do LCK by RF Jenyu*/
		if( (*p_dm->p_is_scan_in_process == false) && (!p_iqk_info->rfk_forbidden)) {
			/* Delta temperature is equal to or larger than 20 centigrade.*/
			if (delta_LCK >= c.threshold_iqk) {
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("delta_LCK(%d) >= threshold_iqk(%d)\n", delta_LCK, c.threshold_iqk));
				p_rf_calibrate_info->thermal_value_lck = thermal_value;

				/*Use RTLCK, so close power tracking driver LCK*/
				if ((!(p_dm->support_ic_type & ODM_RTL8814A)) && (!(p_dm->support_ic_type & ODM_RTL8822B))) {
					if (c.phy_lc_calibrate)
						(*c.phy_lc_calibrate)(p_dm);
				}
			}
		}
	}

	/*3 7. If necessary, move the index of swing table to adjust Tx power.*/

	if (delta > 0 && p_rf_calibrate_info->txpowertrack_control) {
		/* "delta" here is used to record the absolute value of differrence. */
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
		delta = thermal_value > p_hal_data->eeprom_thermal_meter ? (thermal_value - p_hal_data->eeprom_thermal_meter) : (p_hal_data->eeprom_thermal_meter - thermal_value);
#else
		delta = (thermal_value > p_dm->priv->pmib->dot11RFEntry.ther) ? (thermal_value - p_dm->priv->pmib->dot11RFEntry.ther) : (p_dm->priv->pmib->dot11RFEntry.ther - thermal_value);
#endif
		if (delta >= TXPWR_TRACK_TABLE_SIZE)
			delta = TXPWR_TRACK_TABLE_SIZE - 1;

		/*4 7.1 The Final Power index = BaseIndex + power_index_offset*/

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
		if (thermal_value > p_hal_data->eeprom_thermal_meter) {
#else
		if (thermal_value > p_dm->priv->pmib->dot11RFEntry.ther) {
#endif

			for (p = RF_PATH_A; p < c.rf_path_count; p++) {
				p_rf_calibrate_info->delta_power_index_last[p] = p_rf_calibrate_info->delta_power_index[p];	/*recording poer index offset*/
				switch (p) {
				case RF_PATH_B:
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("delta_swing_table_idx_tup_b[%d] = %d\n", delta, delta_swing_table_idx_tup_b[delta]));

					p_rf_calibrate_info->delta_power_index[p] = delta_swing_table_idx_tup_b[delta];
					p_rf_calibrate_info->absolute_ofdm_swing_idx[p] =  delta_swing_table_idx_tup_b[delta];       /*Record delta swing for mix mode power tracking*/
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("******Temp is higher and p_rf_calibrate_info->absolute_ofdm_swing_idx[RF_PATH_B] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
					break;

				case RF_PATH_C:
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("delta_swing_table_idx_tup_c[%d] = %d\n", delta, delta_swing_table_idx_tup_c[delta]));

					p_rf_calibrate_info->delta_power_index[p] = delta_swing_table_idx_tup_c[delta];
					p_rf_calibrate_info->absolute_ofdm_swing_idx[p] =  delta_swing_table_idx_tup_c[delta];       /*Record delta swing for mix mode power tracking*/
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("******Temp is higher and p_rf_calibrate_info->absolute_ofdm_swing_idx[RF_PATH_C] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
					break;

				case RF_PATH_D:
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("delta_swing_table_idx_tup_d[%d] = %d\n", delta, delta_swing_table_idx_tup_d[delta]));

					p_rf_calibrate_info->delta_power_index[p] = delta_swing_table_idx_tup_d[delta];
					p_rf_calibrate_info->absolute_ofdm_swing_idx[p] =  delta_swing_table_idx_tup_d[delta];       /*Record delta swing for mix mode power tracking*/
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("******Temp is higher and p_rf_calibrate_info->absolute_ofdm_swing_idx[RF_PATH_D] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
					break;

				default:
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("delta_swing_table_idx_tup_a[%d] = %d\n", delta, delta_swing_table_idx_tup_a[delta]));

					p_rf_calibrate_info->delta_power_index[p] = delta_swing_table_idx_tup_a[delta];
					p_rf_calibrate_info->absolute_ofdm_swing_idx[p] =  delta_swing_table_idx_tup_a[delta];        /*Record delta swing for mix mode power tracking*/
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("******Temp is higher and p_rf_calibrate_info->absolute_ofdm_swing_idx[RF_PATH_A] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
					break;
				}
			}
			/* JJ ADD 20161014 */
			if (p_dm->support_ic_type & (ODM_RTL8703B | ODM_RTL8723D | ODM_RTL8710B)) {
				/*Save xtal_offset from Xtal table*/
				p_rf_calibrate_info->xtal_offset_last = p_rf_calibrate_info->xtal_offset;	/*recording last Xtal offset*/
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
					("[Xtal] delta_swing_table_xtal_up[%d] = %d\n", delta, delta_swing_table_xtal_up[delta]));
				p_rf_calibrate_info->xtal_offset = delta_swing_table_xtal_up[delta];

				if (p_rf_calibrate_info->xtal_offset_last == p_rf_calibrate_info->xtal_offset)
					xtal_offset_eanble = 0;
				else
					xtal_offset_eanble = 1;
			}

		} else {
			for (p = RF_PATH_A; p < c.rf_path_count; p++) {
				p_rf_calibrate_info->delta_power_index_last[p] = p_rf_calibrate_info->delta_power_index[p];	/*recording poer index offset*/

				switch (p) {
				case RF_PATH_B:
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("delta_swing_table_idx_tdown_b[%d] = %d\n", delta, delta_swing_table_idx_tdown_b[delta]));
					p_rf_calibrate_info->delta_power_index[p] = -1 * delta_swing_table_idx_tdown_b[delta];
					p_rf_calibrate_info->absolute_ofdm_swing_idx[p] =  -1 * delta_swing_table_idx_tdown_b[delta];        /*Record delta swing for mix mode power tracking*/
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("******Temp is lower and p_rf_calibrate_info->absolute_ofdm_swing_idx[RF_PATH_B] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
					break;

				case RF_PATH_C:
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("delta_swing_table_idx_tdown_c[%d] = %d\n", delta, delta_swing_table_idx_tdown_c[delta]));
					p_rf_calibrate_info->delta_power_index[p] = -1 * delta_swing_table_idx_tdown_c[delta];
					p_rf_calibrate_info->absolute_ofdm_swing_idx[p] =  -1 * delta_swing_table_idx_tdown_c[delta];        /*Record delta swing for mix mode power tracking*/
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("******Temp is lower and p_rf_calibrate_info->absolute_ofdm_swing_idx[RF_PATH_C] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
					break;

				case RF_PATH_D:
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("delta_swing_table_idx_tdown_d[%d] = %d\n", delta, delta_swing_table_idx_tdown_d[delta]));
					p_rf_calibrate_info->delta_power_index[p] = -1 * delta_swing_table_idx_tdown_d[delta];
					p_rf_calibrate_info->absolute_ofdm_swing_idx[p] =  -1 * delta_swing_table_idx_tdown_d[delta];        /*Record delta swing for mix mode power tracking*/
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("******Temp is lower and p_rf_calibrate_info->absolute_ofdm_swing_idx[RF_PATH_D] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
					break;

				default:
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("delta_swing_table_idx_tdown_a[%d] = %d\n", delta, delta_swing_table_idx_tdown_a[delta]));
					p_rf_calibrate_info->delta_power_index[p] = -1 * delta_swing_table_idx_tdown_a[delta];
					p_rf_calibrate_info->absolute_ofdm_swing_idx[p] =  -1 * delta_swing_table_idx_tdown_a[delta];        /*Record delta swing for mix mode power tracking*/
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
						("******Temp is lower and p_rf_calibrate_info->absolute_ofdm_swing_idx[RF_PATH_A] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
					break;
				}
			}
			/* JJ ADD 20161014 */
			if (p_dm->support_ic_type & (ODM_RTL8703B | ODM_RTL8723D | ODM_RTL8710B)) {
				/*Save xtal_offset from Xtal table*/
				p_rf_calibrate_info->xtal_offset_last = p_rf_calibrate_info->xtal_offset;	/*recording last Xtal offset*/
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
					("[Xtal] delta_swing_table_xtal_down[%d] = %d\n", delta, delta_swing_table_xtal_down[delta]));
				p_rf_calibrate_info->xtal_offset = delta_swing_table_xtal_down[delta];

				if (p_rf_calibrate_info->xtal_offset_last == p_rf_calibrate_info->xtal_offset)
					xtal_offset_eanble = 0;
				else
					xtal_offset_eanble = 1;
			}

		}

		for (p = RF_PATH_A; p < c.rf_path_count; p++) {
			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("\n\n=========================== [path-%d] Calculating power_index_offset===========================\n", p));

			if (p_rf_calibrate_info->delta_power_index[p] == p_rf_calibrate_info->delta_power_index_last[p])         /*If Thermal value changes but lookup table value still the same*/
				p_rf_calibrate_info->power_index_offset[p] = 0;
			else
				p_rf_calibrate_info->power_index_offset[p] = p_rf_calibrate_info->delta_power_index[p] - p_rf_calibrate_info->delta_power_index_last[p];      /*Power index diff between 2 times Power Tracking*/

			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("[path-%d] power_index_offset(%d) = delta_power_index(%d) - delta_power_index_last(%d)\n", p, p_rf_calibrate_info->power_index_offset[p], p_rf_calibrate_info->delta_power_index[p], p_rf_calibrate_info->delta_power_index_last[p]));

			p_rf_calibrate_info->OFDM_index[p] = p_rf_calibrate_info->bb_swing_idx_ofdm_base[p] + p_rf_calibrate_info->power_index_offset[p];
			p_rf_calibrate_info->CCK_index = p_rf_calibrate_info->bb_swing_idx_cck_base + p_rf_calibrate_info->power_index_offset[p];

			p_rf_calibrate_info->bb_swing_idx_cck = p_rf_calibrate_info->CCK_index;
			p_rf_calibrate_info->bb_swing_idx_ofdm[p] = p_rf_calibrate_info->OFDM_index[p];

			/*************Print BB Swing base and index Offset*************/

			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("The 'CCK' final index(%d) = BaseIndex(%d) + power_index_offset(%d)\n", p_rf_calibrate_info->bb_swing_idx_cck, p_rf_calibrate_info->bb_swing_idx_cck_base, p_rf_calibrate_info->power_index_offset[p]));
			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("The 'OFDM' final index(%d) = BaseIndex[%d](%d) + power_index_offset(%d)\n", p_rf_calibrate_info->bb_swing_idx_ofdm[p], p, p_rf_calibrate_info->bb_swing_idx_ofdm_base[p], p_rf_calibrate_info->power_index_offset[p]));

			/*4 7.1 Handle boundary conditions of index.*/

			if (p_rf_calibrate_info->OFDM_index[p] > c.swing_table_size_ofdm - 1)
				p_rf_calibrate_info->OFDM_index[p] = c.swing_table_size_ofdm - 1;
			else if (p_rf_calibrate_info->OFDM_index[p] <= OFDM_min_index)
				p_rf_calibrate_info->OFDM_index[p] = OFDM_min_index;
		}

		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("\n\n========================================================================================================\n"));

		if (p_rf_calibrate_info->CCK_index > c.swing_table_size_cck - 1)
			p_rf_calibrate_info->CCK_index = c.swing_table_size_cck - 1;
		else if (p_rf_calibrate_info->CCK_index <= 0)
			p_rf_calibrate_info->CCK_index = 0;
	} else {
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("The thermal meter is unchanged or TxPowerTracking OFF(%d): thermal_value: %d, p_rf_calibrate_info->thermal_value: %d\n",
			p_rf_calibrate_info->txpowertrack_control, thermal_value, p_rf_calibrate_info->thermal_value));

		for (p = RF_PATH_A; p < c.rf_path_count; p++)
			p_rf_calibrate_info->power_index_offset[p] = 0;
	}

	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("TxPowerTracking: [CCK] Swing Current index: %d, Swing base index: %d\n",
		p_rf_calibrate_info->CCK_index, p_rf_calibrate_info->bb_swing_idx_cck_base));       /*Print Swing base & current*/

	for (p = RF_PATH_A; p < c.rf_path_count; p++) {
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("TxPowerTracking: [OFDM] Swing Current index: %d, Swing base index[%d]: %d\n",
			p_rf_calibrate_info->OFDM_index[p], p, p_rf_calibrate_info->bb_swing_idx_ofdm_base[p]));
	}

	if ((p_dm->support_ic_type & ODM_RTL8814A)) {
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("power_tracking_type=%d\n", power_tracking_type));

		if (power_tracking_type == 0) {
			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("**********Enter POWER Tracking MIX_MODE**********\n"));
			for (p = RF_PATH_A; p < c.rf_path_count; p++)
				(*c.odm_tx_pwr_track_set_pwr)(p_dm, MIX_MODE, p, 0);
		} else if (power_tracking_type == 1) {
			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("**********Enter POWER Tracking MIX(2G) TSSI(5G) MODE**********\n"));
			for (p = RF_PATH_A; p < c.rf_path_count; p++)
				(*c.odm_tx_pwr_track_set_pwr)(p_dm, MIX_2G_TSSI_5G_MODE, p, 0);
		} else if (power_tracking_type == 2) {
			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("**********Enter POWER Tracking MIX(5G) TSSI(2G)MODE**********\n"));
			for (p = RF_PATH_A; p < c.rf_path_count; p++)
				(*c.odm_tx_pwr_track_set_pwr)(p_dm, MIX_5G_TSSI_2G_MODE, p, 0);
		} else if (power_tracking_type == 3) {
			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("**********Enter POWER Tracking TSSI MODE**********\n"));
			for (p = RF_PATH_A; p < c.rf_path_count; p++)
				(*c.odm_tx_pwr_track_set_pwr)(p_dm, TSSI_MODE, p, 0);
		}
		p_rf_calibrate_info->thermal_value = thermal_value;         /*Record last Power Tracking Thermal value*/

	} else if ((p_rf_calibrate_info->power_index_offset[RF_PATH_A] != 0 ||
		p_rf_calibrate_info->power_index_offset[RF_PATH_B] != 0 ||
		p_rf_calibrate_info->power_index_offset[RF_PATH_C] != 0 ||
		p_rf_calibrate_info->power_index_offset[RF_PATH_D] != 0) &&
		p_rf_calibrate_info->txpowertrack_control && (p_hal_data->eeprom_thermal_meter != 0xff)) {
		/* 4 7.2 Configure the Swing Table to adjust Tx Power. */

		p_rf_calibrate_info->is_tx_power_changed = true;	/*Always true after Tx Power is adjusted by power tracking.*/
		/*  */
		/* 2012/04/23 MH According to Luke's suggestion, we can not write BB digital */
		/* to increase TX power. Otherwise, EVM will be bad. */
		/*  */
		/* 2012/04/25 MH Add for tx power tracking to set tx power in tx agc for 88E. */
		if (thermal_value > p_rf_calibrate_info->thermal_value) {
			for (p = RF_PATH_A; p < c.rf_path_count; p++) {
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
					("Temperature Increasing(%d): delta_pi: %d, delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n",
					p, p_rf_calibrate_info->power_index_offset[p], delta, thermal_value, p_hal_data->eeprom_thermal_meter, p_rf_calibrate_info->thermal_value));
			}
		} else if (thermal_value < p_rf_calibrate_info->thermal_value) {	/*Low temperature*/
			for (p = RF_PATH_A; p < c.rf_path_count; p++) {
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
					("Temperature Decreasing(%d): delta_pi: %d, delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n",
					p, p_rf_calibrate_info->power_index_offset[p], delta, thermal_value, p_hal_data->eeprom_thermal_meter, p_rf_calibrate_info->thermal_value));
			}
		}

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		if (thermal_value > p_hal_data->eeprom_thermal_meter)
#else
		if (thermal_value > p_dm->priv->pmib->dot11RFEntry.ther)
#endif
		{
			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("Temperature(%d) higher than PG value(%d)\n", thermal_value, p_hal_data->eeprom_thermal_meter));

			if (p_dm->support_ic_type == ODM_RTL8188E || p_dm->support_ic_type == ODM_RTL8192E || p_dm->support_ic_type == ODM_RTL8821 ||
			    p_dm->support_ic_type == ODM_RTL8812 || p_dm->support_ic_type == ODM_RTL8723B || p_dm->support_ic_type == ODM_RTL8814A ||
			    p_dm->support_ic_type == ODM_RTL8703B || p_dm->support_ic_type == ODM_RTL8188F || p_dm->support_ic_type == ODM_RTL8822B ||
			    p_dm->support_ic_type == ODM_RTL8723D || p_dm->support_ic_type == ODM_RTL8821C || p_dm->support_ic_type == ODM_RTL8710B) {/* JJ ADD 20161014 */

				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("**********Enter POWER Tracking MIX_MODE**********\n"));
				for (p = RF_PATH_A; p < c.rf_path_count; p++)
					(*c.odm_tx_pwr_track_set_pwr)(p_dm, MIX_MODE, p, 0);
			} else {
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("**********Enter POWER Tracking BBSWING_MODE**********\n"));
				for (p = RF_PATH_A; p < c.rf_path_count; p++)
					(*c.odm_tx_pwr_track_set_pwr)(p_dm, BBSWING, p, indexforchannel);
			}
		} else {
			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("Temperature(%d) lower than PG value(%d)\n", thermal_value, p_hal_data->eeprom_thermal_meter));

			if (p_dm->support_ic_type == ODM_RTL8188E || p_dm->support_ic_type == ODM_RTL8192E || p_dm->support_ic_type == ODM_RTL8821 ||
			    p_dm->support_ic_type == ODM_RTL8812 || p_dm->support_ic_type == ODM_RTL8723B || p_dm->support_ic_type == ODM_RTL8814A ||
			    p_dm->support_ic_type == ODM_RTL8703B || p_dm->support_ic_type == ODM_RTL8188F || p_dm->support_ic_type == ODM_RTL8822B ||
			    p_dm->support_ic_type == ODM_RTL8723D || p_dm->support_ic_type == ODM_RTL8821C || p_dm->support_ic_type == ODM_RTL8710B) {/* JJ ADD 20161014 */

				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("**********Enter POWER Tracking MIX_MODE**********\n"));
				for (p = RF_PATH_A; p < c.rf_path_count; p++)
					(*c.odm_tx_pwr_track_set_pwr)(p_dm, MIX_MODE, p, indexforchannel);
			} else {
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("**********Enter POWER Tracking BBSWING_MODE**********\n"));
				for (p = RF_PATH_A; p < c.rf_path_count; p++)
					(*c.odm_tx_pwr_track_set_pwr)(p_dm, BBSWING, p, indexforchannel);
			}

		}

		p_rf_calibrate_info->bb_swing_idx_cck_base = p_rf_calibrate_info->bb_swing_idx_cck;    /*Record last time Power Tracking result as base.*/
		for (p = RF_PATH_A; p < c.rf_path_count; p++)
			p_rf_calibrate_info->bb_swing_idx_ofdm_base[p] = p_rf_calibrate_info->bb_swing_idx_ofdm[p];

		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("p_rf_calibrate_info->thermal_value = %d thermal_value= %d\n", p_rf_calibrate_info->thermal_value, thermal_value));

		p_rf_calibrate_info->thermal_value = thermal_value;         /*Record last Power Tracking Thermal value*/

	}


	if (p_dm->support_ic_type == ODM_RTL8703B || p_dm->support_ic_type == ODM_RTL8723D || p_dm->support_ic_type == ODM_RTL8710B) {/* JJ ADD 20161014 */

		if (xtal_offset_eanble != 0 && p_rf_calibrate_info->txpowertrack_control && (p_hal_data->eeprom_thermal_meter != 0xff)) {

			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("**********Enter Xtal Tracking**********\n"));

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			if (thermal_value > p_hal_data->eeprom_thermal_meter) {
#else
			if (thermal_value > p_dm->priv->pmib->dot11RFEntry.ther) {
#endif
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
					("Temperature(%d) higher than PG value(%d)\n", thermal_value, p_hal_data->eeprom_thermal_meter));
				(*c.odm_txxtaltrack_set_xtal)(p_dm);
			} else {
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
					("Temperature(%d) lower than PG value(%d)\n", thermal_value, p_hal_data->eeprom_thermal_meter));
				(*c.odm_txxtaltrack_set_xtal)(p_dm);
			}
		}
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("**********End Xtal Tracking**********\n"));
	}

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)

	/* Wait sacn to do IQK by RF Jenyu*/
	if ((*p_dm->p_is_scan_in_process == false) && (!p_iqk_info->rfk_forbidden)) {
		if (!IS_HARDWARE_TYPE_8723B(adapter)) {
			/*Delta temperature is equal to or larger than 20 centigrade (When threshold is 8).*/
			if (delta_IQK >= c.threshold_iqk) {
				p_rf_calibrate_info->thermal_value_iqk = thermal_value;
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("delta_IQK(%d) >= threshold_iqk(%d)\n", delta_IQK, c.threshold_iqk));
				if (!p_rf_calibrate_info->is_iqk_in_progress)
					(*c.do_iqk)(p_dm, delta_IQK, thermal_value, 8);
			}
		}
	}
	if (p_rf_calibrate_info->dpk_thermal[RF_PATH_A] != 0) {
		if (diff_DPK[RF_PATH_A] >= c.threshold_dpk) {
			odm_set_bb_reg(p_dm, 0x82c, BIT(31), 0x1);
			odm_set_bb_reg(p_dm, 0xcc4, BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10), (diff_DPK[RF_PATH_A] / c.threshold_dpk));
			odm_set_bb_reg(p_dm, 0x82c, BIT(31), 0x0);
		} else if ((diff_DPK[RF_PATH_A] <= -1 * c.threshold_dpk)) {
			s32 value = 0x20 + (diff_DPK[RF_PATH_A] / c.threshold_dpk);

			odm_set_bb_reg(p_dm, 0x82c, BIT(31), 0x1);
			odm_set_bb_reg(p_dm, 0xcc4, BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10), value);
			odm_set_bb_reg(p_dm, 0x82c, BIT(31), 0x0);
		} else {
			odm_set_bb_reg(p_dm, 0x82c, BIT(31), 0x1);
			odm_set_bb_reg(p_dm, 0xcc4, BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10), 0);
			odm_set_bb_reg(p_dm, 0x82c, BIT(31), 0x0);
		}
	}
	if (p_rf_calibrate_info->dpk_thermal[RF_PATH_B] != 0) {
		if (diff_DPK[RF_PATH_B] >= c.threshold_dpk) {
			odm_set_bb_reg(p_dm, 0x82c, BIT(31), 0x1);
			odm_set_bb_reg(p_dm, 0xec4, BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10), (diff_DPK[RF_PATH_B] / c.threshold_dpk));
			odm_set_bb_reg(p_dm, 0x82c, BIT(31), 0x0);
		} else if ((diff_DPK[RF_PATH_B] <= -1 * c.threshold_dpk)) {
			s32 value = 0x20 + (diff_DPK[RF_PATH_B] / c.threshold_dpk);

			odm_set_bb_reg(p_dm, 0x82c, BIT(31), 0x1);
			odm_set_bb_reg(p_dm, 0xec4, BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10), value);
			odm_set_bb_reg(p_dm, 0x82c, BIT(31), 0x0);
		} else {
			odm_set_bb_reg(p_dm, 0x82c, BIT(31), 0x1);
			odm_set_bb_reg(p_dm, 0xec4, BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10), 0);
			odm_set_bb_reg(p_dm, 0x82c, BIT(31), 0x0);
		}
	}

#endif

	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("<===odm_txpowertracking_callback_thermal_meter\n"));

	p_rf_calibrate_info->tx_powercount = 0;
}



/* 3============================================================
 * 3 IQ Calibration
 * 3============================================================ */

void
odm_reset_iqk_result(
	struct PHY_DM_STRUCT	*p_dm
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
	struct PHY_DM_STRUCT	*p_dm
)
{
	struct _ADAPTER	*adapter = p_dm->adapter;
	struct _IQK_INFORMATION	*p_iqk_info = &p_dm->IQK_info;
	
	RT_TRACE(COMP_SCAN, ODM_DBG_LOUD, ("=>%s\n" , __FUNCTION__));

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if (*p_dm->p_is_fcs_mode_enable)
		return;
#endif

	if ((p_dm->is_linked) && (!p_iqk_info->rfk_forbidden)) {
		RT_TRACE(COMP_SCAN, ODM_DBG_LOUD, ("interval=%d ch=%d prech=%d scan=%s\n", p_dm->linked_interval,
			*p_dm->p_channel,  p_dm->pre_channel, *p_dm->p_is_scan_in_process == TRUE ? "TRUE":"FALSE"));

		if (*p_dm->p_channel != p_dm->pre_channel) {
			p_dm->pre_channel = *p_dm->p_channel;
			p_dm->linked_interval = 0;
		}

		if ((p_dm->linked_interval < 3) && (!*p_dm->p_is_scan_in_process))
			p_dm->linked_interval++;

		if (p_dm->linked_interval == 2)
			PHY_IQCalibrate(adapter, false);
	} else
		p_dm->linked_interval = 0;

		RT_TRACE(COMP_SCAN, ODM_DBG_LOUD, ("<=%s interval=%d ch=%d prech=%d scan=%s\n", __FUNCTION__, p_dm->linked_interval,
			*p_dm->p_channel,  p_dm->pre_channel, *p_dm->p_is_scan_in_process == TRUE?"TRUE":"FALSE"));
}

void phydm_rf_init(struct PHY_DM_STRUCT		*p_dm)
{

	odm_txpowertracking_init(p_dm);

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	odm_clear_txpowertracking_state(p_dm);
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
#if (RTL8814A_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8814A)
		phy_iq_calibrate_8814a_init(p_dm);
#endif
#endif

}

void phydm_rf_watchdog(struct PHY_DM_STRUCT		*p_dm)
{

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	odm_txpowertracking_check(p_dm);
	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES)
		odm_iq_calibrate(p_dm);
#endif
}
