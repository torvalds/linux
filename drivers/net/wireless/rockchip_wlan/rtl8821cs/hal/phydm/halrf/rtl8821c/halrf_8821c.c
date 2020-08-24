/* SPDX-License-Identifier: GPL-2.0 */
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
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#if RT_PLATFORM == PLATFORM_MACOSX
#include "phydm_precomp.h"
#else
#include "../phydm_precomp.h"
#endif
#else
#include "../../phydm_precomp.h"
#endif

#if (RTL8821C_SUPPORT == 1)
void halrf_rf_lna_setting_8821c(struct dm_struct *dm_void,
				enum halrf_lna_set type)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 path = 0x0;

	if (type == HALRF_LNA_DISABLE) {
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0xef, BIT(19), 0x1);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0x33, RFREGOFFSETMASK, 0x00003);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3e, RFREGOFFSETMASK, 0x00064);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3f, RFREGOFFSETMASK, 0x0afce);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0xef, BIT(19), 0x0);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0xee, BIT(12), 0x1);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0x33, RFREGOFFSETMASK, 0x00003);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3e, RFREGOFFSETMASK, 0x00064);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3f, RFREGOFFSETMASK, 0x0280d);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0xee, BIT(12), 0x0);
	} else if (type == HALRF_LNA_ENABLE) {
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0xef, BIT(19), 0x1);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0x33, RFREGOFFSETMASK, 0x00003);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3e, RFREGOFFSETMASK, 0x00064);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3f, RFREGOFFSETMASK, 0x1afce);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0xef, BIT(19), 0x0);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0xee, BIT(12), 0x1);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0x33, RFREGOFFSETMASK, 0x00003);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3e, RFREGOFFSETMASK, 0x00064);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3f, RFREGOFFSETMASK, 0x0281d);
		odm_set_rf_reg(dm, (enum rf_path)path, RF_0xee, BIT(12), 0x0);
	}
}

boolean
get_mix_mode_tx_agc_bbs_wing_offset_8821c(void *dm_void,
					  enum pwrtrack_method method,
					  u8 rf_path,
					  u8 tx_power_index_offest_upper_bound,
					  s8 tx_power_index_offest_lower_bound)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &dm->rf_calibrate_info;

	u8 bb_swing_upper_bound = cali_info->default_ofdm_index + 10;
	u8 bb_swing_lower_bound = 0;

	s8 tx_agc_index = 0;
	u8 tx_bb_swing_index = cali_info->default_ofdm_index;

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "Path_%d pRF->absolute_ofdm_swing_idx[rf_path]=%d, tx_power_index_offest_upper_bound=%d, tx_power_index_offest_lower_bound=%d\n",
	       rf_path, cali_info->absolute_ofdm_swing_idx[rf_path],
	       tx_power_index_offest_upper_bound,
	       tx_power_index_offest_lower_bound);

	if (tx_power_index_offest_upper_bound > 0XF)
		tx_power_index_offest_upper_bound = 0XF;

	if (tx_power_index_offest_lower_bound < -15)
		tx_power_index_offest_lower_bound = -15;

	if (cali_info->absolute_ofdm_swing_idx[rf_path] >= 0 && cali_info->absolute_ofdm_swing_idx[rf_path] <= tx_power_index_offest_upper_bound) {
		tx_agc_index = cali_info->absolute_ofdm_swing_idx[rf_path];
		tx_bb_swing_index = cali_info->default_ofdm_index;
	} else if (cali_info->absolute_ofdm_swing_idx[rf_path] >= 0 && (cali_info->absolute_ofdm_swing_idx[rf_path] > tx_power_index_offest_upper_bound)) {
		tx_agc_index = tx_power_index_offest_upper_bound;
		cali_info->remnant_ofdm_swing_idx[rf_path] = cali_info->absolute_ofdm_swing_idx[rf_path] - tx_power_index_offest_upper_bound;
		tx_bb_swing_index = cali_info->default_ofdm_index + cali_info->remnant_ofdm_swing_idx[rf_path];

		if (tx_bb_swing_index > bb_swing_upper_bound)
			tx_bb_swing_index = bb_swing_upper_bound;
	} else if (cali_info->absolute_ofdm_swing_idx[rf_path] < 0 && (cali_info->absolute_ofdm_swing_idx[rf_path] >= tx_power_index_offest_lower_bound)) {
		tx_agc_index = cali_info->absolute_ofdm_swing_idx[rf_path];
		tx_bb_swing_index = cali_info->default_ofdm_index;
	} else if (cali_info->absolute_ofdm_swing_idx[rf_path] < 0 && (cali_info->absolute_ofdm_swing_idx[rf_path] < tx_power_index_offest_lower_bound)) {
		tx_agc_index = tx_power_index_offest_lower_bound;
		cali_info->remnant_ofdm_swing_idx[rf_path] = cali_info->absolute_ofdm_swing_idx[rf_path] - tx_power_index_offest_lower_bound;

		if (cali_info->default_ofdm_index > (cali_info->remnant_ofdm_swing_idx[rf_path] * (-1)))
			tx_bb_swing_index = cali_info->default_ofdm_index + cali_info->remnant_ofdm_swing_idx[rf_path];
		else
			tx_bb_swing_index = bb_swing_lower_bound;
	}

	cali_info->absolute_ofdm_swing_idx[rf_path] = tx_agc_index;
	cali_info->bb_swing_idx_ofdm[rf_path] = tx_bb_swing_index;

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "MixMode Offset Path_%d   pRF->absolute_ofdm_swing_idx[rf_path]=%d   pRF->bb_swing_idx_ofdm[rf_path]=%d   TxPwrIdxOffestUpper=%d   TxPwrIdxOffestLower=%d\n",
	       rf_path, cali_info->absolute_ofdm_swing_idx[rf_path],
	       cali_info->bb_swing_idx_ofdm[rf_path],
	       tx_power_index_offest_upper_bound,
	       tx_power_index_offest_lower_bound);

	return true;
}

void odm_tx_pwr_track_set_pwr8821c(void *dm_void, enum pwrtrack_method method,
				   u8 rf_path, u8 channel_mapped_index)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	struct _ADAPTER *adapter = dm->adapter;
	u8 channel = *dm->channel;
	u8 band_width = *dm->band_width;
#endif
	struct dm_rf_calibration_struct *cali_info = &dm->rf_calibrate_info;
	struct _hal_rf_ *rf = &dm->rf_table;
	u8 tx_power_index_offest_upper_bound = 0;
	s8 tx_power_index_offest_lower_bound = 0;
	u8 tx_power_index = 0;
	u8 tx_rate = 0xFF;

	if (*dm->mp_mode) {
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
#if (MP_DRIVER == 1)
		PMPT_CONTEXT p_mpt_ctx = &adapter->MptCtx;

		tx_rate = MptToMgntRate(p_mpt_ctx->MptRateIndex);
#endif
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
#ifdef CONFIG_MP_INCLUDED
		PMPT_CONTEXT p_mpt_ctx = &(adapter->mppriv.mpt_ctx);

		tx_rate = mpt_to_mgnt_rate(p_mpt_ctx->mpt_rate_index);
#endif
#endif
#endif
	} else {
		u16 rate = *dm->forced_data_rate;

		if (!rate) { /*auto rate*/
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
			tx_rate = ((PADAPTER)adapter)->HalFunc.GetHwRateFromMRateHandler(dm->tx_rate);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE) && defined(DM_ODM_CE_MAC80211)
			tx_rate = dm->tx_rate;
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
			if (dm->number_linked_client != 0)
				tx_rate = hw_rate_to_m_rate(dm->tx_rate);
			else
				tx_rate = rf->p_rate_index;
#endif
		} else { /*force rate*/
			tx_rate = (u8)rate;
		}
	}

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "Call:%s tx_rate=0x%X\n", __func__,
	       tx_rate);

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "pRF->default_ofdm_index=%d   pRF->default_cck_index=%d\n",
	       cali_info->default_ofdm_index, cali_info->default_cck_index);

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "pRF->absolute_ofdm_swing_idx=%d   pRF->remnant_ofdm_swing_idx=%d   pRF->absolute_cck_swing_idx=%d   pRF->remnant_cck_swing_idx=%d   rf_path=%d\n",
	       cali_info->absolute_ofdm_swing_idx[rf_path],
	       cali_info->remnant_ofdm_swing_idx[rf_path],
	       cali_info->absolute_cck_swing_idx[rf_path],
	       cali_info->remnant_cck_swing_idx, rf_path);

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	tx_power_index = odm_get_tx_power_index(dm, (enum rf_path)rf_path, tx_rate, band_width, channel);
#else
	tx_power_index = config_phydm_read_txagc_8821c(dm, rf_path, 0x04); /*0x04(TX_AGC_OFDM_6M)*/
#endif

	if (tx_power_index >= 63)
		tx_power_index = 63;

	tx_power_index_offest_upper_bound = 63 - tx_power_index;

	tx_power_index_offest_lower_bound = 0 - tx_power_index;

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "tx_power_index=%d tx_power_index_offest_upper_bound=%d tx_power_index_offest_lower_bound=%d rf_path=%d\n",
	       tx_power_index, tx_power_index_offest_upper_bound,
	       tx_power_index_offest_lower_bound, rf_path);

	if (method == BBSWING) { /*use for mp driver clean power tracking status*/
		switch (rf_path) {
		case RF_PATH_A:
			odm_set_bb_reg(dm, R_0xc94, (BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1)), ((cali_info->absolute_ofdm_swing_idx[rf_path]) & 0x3f));
			odm_set_bb_reg(dm, REG_A_TX_SCALE_JAGUAR, 0xFFE00000, tx_scaling_table_jaguar[cali_info->bb_swing_idx_ofdm[rf_path]]);
			break;

		default:
			break;
		}

	} else if (method == MIX_MODE) {
		switch (rf_path) {
		case RF_PATH_A:
			get_mix_mode_tx_agc_bbs_wing_offset_8821c(dm, method, rf_path, tx_power_index_offest_upper_bound, tx_power_index_offest_lower_bound);
			odm_set_bb_reg(dm, R_0xc94, (BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1)), ((cali_info->absolute_ofdm_swing_idx[rf_path]) & 0x3f));
			odm_set_bb_reg(dm, REG_A_TX_SCALE_JAGUAR, 0xFFE00000, tx_scaling_table_jaguar[cali_info->bb_swing_idx_ofdm[rf_path]]);

			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       "TXAGC(0xC94)=0x%x BBSwing(0xc1c)=0x%x BBSwingIndex=%d rf_path=%d\n",
			       odm_get_bb_reg(dm, R_0xc94,
					      (BIT(6) | BIT(5) | BIT(4) |
					       BIT(3) | BIT(2) | BIT(1))),
			       odm_get_bb_reg(dm, R_0xc1c, 0xFFE00000),
			       cali_info->bb_swing_idx_ofdm[rf_path], rf_path);
			break;

		default:
			break;
		}
	}
}

void get_delta_swing_table_8821c(void *dm_void,
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
				 u8 **temperature_up_a, u8 **temperature_down_a,
				 u8 **temperature_up_b, u8 **temperature_down_b,
				 u8 **temperature_up_cck_a,
				 u8 **temperature_down_cck_a,
				 u8 **temperature_up_cck_b,
				 u8 **temperature_down_cck_b
#else
				 u8 **temperature_up_a, u8 **temperature_down_a,
				 u8 **temperature_up_b,
				 u8 **temperature_down_b
#endif
				 )
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &dm->rf_calibrate_info;

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	u8 channel = *(dm->channel);
#else
	u8 channel = *dm->channel;
#endif

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	*temperature_up_cck_a = cali_info->delta_swing_table_idx_2g_cck_a_p;
	*temperature_down_cck_a = cali_info->delta_swing_table_idx_2g_cck_a_n;
	*temperature_up_cck_b = cali_info->delta_swing_table_idx_2g_cck_b_p;
	*temperature_down_cck_b = cali_info->delta_swing_table_idx_2g_cck_b_n;
#endif

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

void aac_check_8821c(struct dm_struct *dm)
{
	struct _hal_rf_ *rf = &dm->rf_table;
	u32 temp;

	if (!rf->aac_checked) {
		RF_DBG(dm, DBG_RF_LCK, "[LCK]AAC check for 8821c\n");
		temp = odm_get_rf_reg(dm, RF_PATH_A, 0xc9, 0xf8);
		if (temp < 4 || temp > 7) {
			odm_set_rf_reg(dm, RF_PATH_A, 0xca, BIT(19), 0x0);
			odm_set_rf_reg(dm, RF_PATH_A, 0xb2, 0x7c000, 0x6);
		}
		rf->aac_checked = true;
	}
}

void _phy_aac_calibrate_8821c(struct dm_struct *dm)
{
#if 0
	u32 cnt = 0;

	RF_DBG(dm, DBG_RF_LCK, "[AACK]AACK start!!!!!!!\n");
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xb8, RFREGOFFSETMASK, 0x80a00);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xb0, RFREGOFFSETMASK, 0xff0fa);
	ODM_delay_ms(10);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xca, RFREGOFFSETMASK, 0x80000);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xc9, RFREGOFFSETMASK, 0x1c141);
	for (cnt = 0; cnt < 100; cnt++) {
		ODM_delay_ms(1);
		if (odm_get_rf_reg(dm, RF_PATH_A, RF_0xca, 0x1000) != 0x1)
			break;
	}

	odm_set_rf_reg(dm, RF_PATH_A, RF_0xb0, RFREGOFFSETMASK, 0xff0f8);

	RF_DBG(dm, DBG_RF_IQK, "[AACK]AACK end!!!!!!!\n");
#endif
}

void _phy_lc_calibrate_8821c(struct dm_struct *dm)
{
#if 1
	aac_check_8821c(dm);
	RF_DBG(dm, DBG_RF_LCK, "[LCK]real-time LCK!!!!!!!\n");
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xcc, RFREGOFFSETMASK, 0x2018);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xc4, RFREGOFFSETMASK, 0x8f602);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xcc, RFREGOFFSETMASK, 0x201c);
#endif
#if 0
	u32 lc_cal = 0, cnt = 0, tmp0xc00;
	/*RF to standby mode*/
	tmp0xc00 = odm_read_4byte(dm, 0xc00);
	odm_write_4byte(dm, 0xc00, 0x4);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x0, RFREGOFFSETMASK, 0x10000);

	_phy_aac_calibrate_8821c(dm);

	/*backup RF0x18*/
	lc_cal = odm_get_rf_reg(dm, RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK);
	/*Start LCK*/
	odm_set_rf_reg(dm, RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK, lc_cal | 0x08000);
	ODM_delay_ms(50);

	for (cnt = 0; cnt < 100; cnt++) {
		if (odm_get_rf_reg(dm, RF_PATH_A, RF_CHNLBW, 0x8000) != 0x1)
			break;
		ODM_delay_ms(10);
	}

	/*Recover channel number*/
	odm_set_rf_reg(dm, RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK, lc_cal);
	/**restore*/
	odm_write_4byte(dm, 0xc00, tmp0xc00);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x0, RFREGOFFSETMASK, 0x3ffff);
	RF_DBG(dm, DBG_RF_IQK, "[LCK]LCK end!!!!!!!\n");
#endif
}

/*LCK:0x2*/
/*1. add AACK check*/
void phy_lc_calibrate_8821c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	_phy_lc_calibrate_8821c(dm);
}

void configure_txpower_track_8821c(struct txpwrtrack_cfg *config)
{
	config->swing_table_size_cck = TXSCALE_TABLE_SIZE;
	config->swing_table_size_ofdm = TXSCALE_TABLE_SIZE;
	config->threshold_iqk = IQK_THRESHOLD;
	config->threshold_dpk = DPK_THRESHOLD;
	config->average_thermal_num = AVG_THERMAL_NUM_8821C;
	config->rf_path_count = MAX_PATH_NUM_8821C;
	config->thermal_reg_addr = RF_T_METER_8821C;

	config->odm_tx_pwr_track_set_pwr = odm_tx_pwr_track_set_pwr8821c;
	config->do_iqk = do_iqk_8821c;
	config->phy_lc_calibrate = halrf_lck_trigger;

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	config->get_delta_all_swing_table = get_delta_swing_table_8821c;
#else
	config->get_delta_swing_table = get_delta_swing_table_8821c;
#endif
}

#if ((DM_ODM_SUPPORT_TYPE & ODM_AP) || (DM_ODM_SUPPORT_TYPE == ODM_CE))
void phy_set_rf_path_switch_8821c(struct dm_struct *dm,
#else
void phy_set_rf_path_switch_8821c(void *adapter,
#endif
				  boolean is_main)
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct dm_struct *dm = &hal_data->DM_OutSrc;
#endif
#endif
	u8 ant_num = 0; /*0: ANT_1, 1: ANT_2*/

	if (is_main)
		ant_num = SWITCH_TO_ANT1; /*Main = ANT_1*/
	else
		ant_num = SWITCH_TO_ANT2; /*Aux = ANT_2*/

	config_phydm_set_ant_path(dm, dm->current_rf_set_8821c, ant_num);
}


#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
boolean _phy_query_rf_path_switch_8821c(struct dm_struct *dm
#else
boolean _phy_query_rf_path_switch_8821c(void *adapter
#endif
				)
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct dm_struct *dm = &hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct dm_struct *dm = &hal_data->DM_OutSrc;
#endif
#endif
	u8 ant_num = 0; /*0: ANT_1, 1: ANT_2*/

	ODM_delay_ms(300);

	ant_num = query_phydm_current_ant_num_8821c(dm);

	if (ant_num == SWITCH_TO_ANT1)
		return true; /*Main = ANT_1*/
	else
		return false; /*Aux = ANT_2*/
}

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
boolean phy_query_rf_path_switch_8821c(struct dm_struct *dm
#else
boolean phy_query_rf_path_switch_8821c(void *adapter
#endif
				       )
{
#if DISABLE_BB_RF
	return true;
#endif

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	return _phy_query_rf_path_switch_8821c(dm);
#else
	return _phy_query_rf_path_switch_8821c(adapter);
#endif
}

#endif /* (RTL8821C_SUPPORT == 0)*/
