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
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#if RT_PLATFORM == PLATFORM_MACOSX
#include "phydm_precomp.h"
#else
#include "../phydm_precomp.h"
#endif
#else
#include "../../phydm_precomp.h"
#endif

#if (RTL8822B_SUPPORT == 1)
void halrf_rf_lna_setting_8822b(struct dm_struct *dm_void,
				enum halrf_lna_set type)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 path = 0x0;

	for (path = 0x0; path < 2; path++)
		if (type == HALRF_LNA_DISABLE) {
			/*S0*/
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0xef, BIT(19),
				       0x1);
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0x33,
				       RFREGOFFSETMASK, 0x00003);
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3e,
				       RFREGOFFSETMASK, 0x00064);
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3f,
				       RFREGOFFSETMASK, 0x0afce);
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0xef, BIT(19),
				       0x0);
		} else if (type == HALRF_LNA_ENABLE) {
			/*S0*/
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0xef, BIT(19),
				       0x1);
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0x33,
				       RFREGOFFSETMASK, 0x00003);
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3e,
				       RFREGOFFSETMASK, 0x00064);
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0x3f,
				       RFREGOFFSETMASK, 0x1afce);
			odm_set_rf_reg(dm, (enum rf_path)path, RF_0xef, BIT(19),
				       0x0);
		}
}

boolean get_mix_mode_tx_agc_bb_swing_offset_8822b(void *dm_void,
						  enum pwrtrack_method method,
						  u8 rf_path,
						  u8 tx_power_index_offset)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &dm->rf_calibrate_info;

	u8 bb_swing_upper_bound = cali_info->default_ofdm_index + 10;
	u8 bb_swing_lower_bound = 0;

	s8 tx_agc_index = 0;
	u8 tx_bb_swing_index = cali_info->default_ofdm_index;

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "Path_%d absolute_ofdm_swing[%d]=%d tx_power_idx_offset=%d\n",
	       rf_path, rf_path, cali_info->absolute_ofdm_swing_idx[rf_path],
	       tx_power_index_offset);

	if (tx_power_index_offset > 0XF)
		tx_power_index_offset = 0XF;

	if (cali_info->absolute_ofdm_swing_idx[rf_path] >= 0 &&
	    cali_info->absolute_ofdm_swing_idx[rf_path] <=
		    tx_power_index_offset) {
		tx_agc_index = cali_info->absolute_ofdm_swing_idx[rf_path];
		tx_bb_swing_index = cali_info->default_ofdm_index;
	} else if (cali_info->absolute_ofdm_swing_idx[rf_path] >
		   tx_power_index_offset) {
		tx_agc_index = tx_power_index_offset;
		cali_info->remnant_ofdm_swing_idx[rf_path] =
			cali_info->absolute_ofdm_swing_idx[rf_path] -
			tx_power_index_offset;
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

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "absolute_ofdm[%d]=%d bb_swing_ofdm[%d]=%d tx_pwr_offset=%d\n",
	       rf_path, cali_info->absolute_ofdm_swing_idx[rf_path],
	       rf_path, cali_info->bb_swing_idx_ofdm[rf_path],
	       tx_power_index_offset);

	return true;
}

void odm_pwrtrack_method_set_pwr8822b(void *dm_void,
				      enum pwrtrack_method method,
				      u8 rf_path, u8 tx_pwr_idx_offset)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &dm->rf_calibrate_info;
	u32 tmp_reg1, tmp_reg2, tmp_reg3;
	u8 bb_swing_idx_ofdm = cali_info->bb_swing_idx_ofdm[rf_path];

	/*use for mp driver clean power tracking status*/
	if (method == BBSWING) {
		if (rf_path == RF_PATH_A) {
			tmp_reg1 = R_0xc94;
			tmp_reg2 = REG_A_TX_SCALE_JAGUAR;
		} else if (rf_path == RF_PATH_B) {
			tmp_reg1 = R_0xe94;
			tmp_reg2 = REG_B_TX_SCALE_JAGUAR;
		} else {
			return;
		}

		odm_set_bb_reg(dm, tmp_reg1,
			       BIT(29) | BIT(28) | BIT(27) | BIT(26) | BIT(25),
			       cali_info->absolute_ofdm_swing_idx[rf_path]);
		odm_set_bb_reg(dm, tmp_reg2, 0xFFE00000,
			       tx_scaling_table_jaguar[bb_swing_idx_ofdm]);

	} else if (method == MIX_MODE) {
		if (rf_path == RF_PATH_A) {
			tmp_reg1 = R_0xc94;
			tmp_reg2 = REG_A_TX_SCALE_JAGUAR;
			tmp_reg3 = 0xc1c;
		} else if (rf_path == RF_PATH_B) {
			tmp_reg1 = R_0xe94;
			tmp_reg2 = REG_B_TX_SCALE_JAGUAR;
			tmp_reg3 = 0xe1c;
		} else {
			return;
		}

		get_mix_mode_tx_agc_bb_swing_offset_8822b(dm,
							  method,
							  rf_path,
							  tx_pwr_idx_offset);
		bb_swing_idx_ofdm = cali_info->bb_swing_idx_ofdm[rf_path];
		odm_set_bb_reg(dm, tmp_reg1,
			       BIT(29) | BIT(28) | BIT(27) | BIT(26) | BIT(25),
			       cali_info->absolute_ofdm_swing_idx[rf_path]);
		odm_set_bb_reg(dm, tmp_reg2, 0xFFE00000,
			       tx_scaling_table_jaguar[bb_swing_idx_ofdm]);

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		       "TXAGC(%x)=0x%x BBSw(%x)=0x%x BBSwIdx=%d rf_path=%d\n",
		       tmp_reg1,
		       odm_get_bb_reg(dm, tmp_reg1,
				      BIT(29) | BIT(28) | BIT(27) |
				      BIT(26) | BIT(25)),
		       tmp_reg3, odm_get_bb_reg(dm, tmp_reg3, 0xFFE00000),
		       cali_info->bb_swing_idx_ofdm[rf_path], rf_path);
	}
}

void odm_tx_pwr_track_set_pwr8822b(void *dm_void, enum pwrtrack_method method,
				   u8 rf_path, u8 channel_mapped_index)
{
#if 0
	struct dm_struct	*dm = (struct dm_struct *)dm_void;
	void	*adapter = dm->adapter;
	HAL_DATA_TYPE	*hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct dm_rf_calibration_struct	*cali_info = &dm->rf_calibrate_info;
	u8			channel  = *dm->channel;
	u8			band_width  = hal_data->current_channel_bw;
	u8			tx_power_index = 0;
	u8			tx_rate = 0xFF;
	enum rt_status		status = RT_STATUS_SUCCESS;

	PHALMAC_PWR_TRACKING_OPTION p_pwr_tracking_opt = &(cali_info->HALMAC_PWR_TRACKING_INFO);

	if (*dm->mp_mode == true) {
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
#if (MP_DRIVER == 1)
		PMPT_CONTEXT p_mpt_ctx = &(adapter->mpt_ctx);

		tx_rate = mpt_to_mgnt_rate(p_mpt_ctx->mpt_rate_index);
#endif
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
		PMPT_CONTEXT p_mpt_ctx = &(adapter->mppriv.mpt_ctx);

		tx_rate = mpt_to_mgnt_rate(p_mpt_ctx->mpt_rate_index);
#endif
#endif
	} else {
		u16	rate	 = *(dm->forced_data_rate);

		if (!rate) { /*auto rate*/
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
			tx_rate = ((PADAPTER)adapter)->HalFunc.GetHwRateFromMRateHandler(dm->tx_rate);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
			if (dm->number_linked_client != 0)
				tx_rate = hw_rate_to_m_rate(dm->tx_rate);
#endif
		} else   /*force rate*/
			tx_rate = (u8) rate;
	}

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "Call:%s tx_rate=0x%X\n", __func__,
	       tx_rate);

	tx_power_index = phy_get_tx_power_index(adapter, (enum rf_path) rf_path, tx_rate, band_width, channel);

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "type=%d   tx_power_index=%d	 cali_info->absolute_ofdm_swing_idx=%d   cali_info->default_ofdm_index=%d   rf_path=%d\n",
	       method, tx_power_index,
	       cali_info->absolute_ofdm_swing_idx[rf_path],
	       cali_info->default_ofdm_index, rf_path);

	p_pwr_tracking_opt->type = method;
	p_pwr_tracking_opt->bbswing_index = cali_info->default_ofdm_index;
	p_pwr_tracking_opt->pwr_tracking_para[rf_path].enable = 1;
	p_pwr_tracking_opt->pwr_tracking_para[rf_path].tx_pwr_index = tx_power_index;
	p_pwr_tracking_opt->pwr_tracking_para[rf_path].pwr_tracking_offset_value = cali_info->absolute_ofdm_swing_idx[rf_path];
	p_pwr_tracking_opt->pwr_tracking_para[rf_path].tssi_value = 0;


	if (rf_path == (MAX_PATH_NUM_8822B - 1)) {
		status = hal_mac_send_power_tracking_info(&GET_HAL_MAC_INFO(adapter), p_pwr_tracking_opt);

		if (status == RT_STATUS_SUCCESS) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       "path A  0xC94=0x%X   0xC1C=0x%X\n",
			       odm_get_bb_reg(dm, R_0xc94,
			       BIT(29) | BIT(28) | BIT(27) | BIT(26) | BIT(25)),
			       odm_get_bb_reg(dm, R_0xc1c, 0xFFE00000));
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       "path B  0xE94=0x%X   0xE1C=0x%X\n",
			       odm_get_bb_reg(dm, R_0xe94,
			       BIT(29) | BIT(28) | BIT(27) | BIT(26) | BIT(25)),
			       odm_get_bb_reg(dm, R_0xe1c, 0xFFE00000));
		} else {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       "Power Tracking to FW Fail ret code = %d\n",
			       status);
		}
	}

#endif
	struct dm_struct *dm = (struct dm_struct *)dm_void;
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	struct _ADAPTER *adapter = dm->adapter;
#endif
	struct dm_rf_calibration_struct *cali_info = &dm->rf_calibrate_info;
	struct _hal_rf_ *rf = &dm->rf_table;
	u8 tx_pwr_idx_offset = 0;
	u8 tx_pwr_idx = 0;
	u8 mpt_rate_index = 0;
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	u8 channel = *dm->channel;
	u8 band_width = *dm->band_width;
	u8 tx_rate = 0xFF;

	if (*dm->mp_mode == 1) {
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
#if (MP_DRIVER == 1)
		PMPT_CONTEXT p_mpt_ctx = &adapter->MptCtx;

		tx_rate = MptToMgntRate(p_mpt_ctx->MptRateIndex);
#endif
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
#ifdef CONFIG_MP_INCLUDED
		if (rf->mp_rate_index)
			mpt_rate_index = *rf->mp_rate_index;

		tx_rate = mpt_to_mgnt_rate(mpt_rate_index);
#endif
#endif
#endif
	} else {
		u16 rate = *dm->forced_data_rate;

		if (!rate) { /*auto rate*/
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
			struct _ADAPTER *adapter = dm->adapter;

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
#endif

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "pRF->default_ofdm_index=%d   pRF->default_cck_index=%d\n",
	       cali_info->default_ofdm_index, cali_info->default_cck_index);

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "absolute_ofdm_swing_idx=%d remnant_ofdm_swing_idx=%d path=%d\n",
	       cali_info->absolute_ofdm_swing_idx[rf_path],
	       cali_info->remnant_ofdm_swing_idx[rf_path], rf_path);

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "absolute_cck_swing_idx=%d remnant_cck_swing_idx=%d path=%d\n",
	       cali_info->absolute_cck_swing_idx[rf_path],
	       cali_info->remnant_cck_swing_idx, rf_path);

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	tx_pwr_idx = odm_get_tx_power_index(dm, (enum rf_path)rf_path, tx_rate, (enum channel_width)band_width, channel);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	tx_pwr_idx = odm_get_tx_power_index(dm, (enum rf_path)rf_path,
					    tx_rate, band_width, channel);
#else
	/*0x04(TX_AGC_OFDM_6M)*/
	tx_pwr_idx = config_phydm_read_txagc_8822b(dm, rf_path, 0x04);
#endif

	if (tx_pwr_idx >= 63)
		tx_pwr_idx = 63;

	tx_pwr_idx_offset = 63 - tx_pwr_idx;

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "tx_power_index=%d tx_power_index_offset=%d rf_path=%d\n",
	       tx_pwr_idx, tx_pwr_idx_offset, rf_path);

	odm_pwrtrack_method_set_pwr8822b(dm, method, rf_path,
					 tx_pwr_idx_offset);
}

void get_delta_swing_table_8822b(void *dm_void,
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
				 u8 **temperature_up_a, u8 **temperature_down_a,
				 u8 **temperature_up_b, u8 **temperature_down_b,
				 u8 **temperature_up_cck_a,
				 u8 **temperature_down_cck_a,
				 u8 **temperature_up_cck_b,
				 u8 **temperature_down_cck_b)
#else
				 u8 **temperature_up_a,
				 u8 **temperature_down_a,
				 u8 **temperature_up_b,
				 u8 **temperature_down_b)
#endif
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &dm->rf_calibrate_info;
	u8 channel = *dm->channel;

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

void aac_check_8822b(struct dm_struct *dm)
{
	struct _hal_rf_ *rf = &dm->rf_table;
	u32 temp;

	if (!rf->aac_checked) {
		RF_DBG(dm, DBG_RF_LCK, "[LCK]AAC check for 8822b\n");
		temp = odm_get_rf_reg(dm, RF_PATH_A, 0xc9, 0xf8);
		if (temp < 4 || temp > 7) {
			odm_set_rf_reg(dm, RF_PATH_A, 0xca, BIT(19), 0x0);
			odm_set_rf_reg(dm, RF_PATH_A, 0xb2, 0x7c000, 0x6);
		}
		rf->aac_checked = true;
	}
}

void _phy_lc_calibrate_8822b(struct dm_struct *dm)
{
	u32 lc_cal = 0, cnt = 0, tmp0xc00, tmp0xe00;

	aac_check_8822b(dm);
	RF_DBG(dm, DBG_RF_IQK, "[LCK]LCK start!!!!!!!\n");
	tmp0xc00 = odm_read_4byte(dm, 0xc00);
	tmp0xe00 = odm_read_4byte(dm, 0xe00);
	odm_write_4byte(dm, 0xc00, 0x4);
	odm_write_4byte(dm, 0xe00, 0x4);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x0, RFREGOFFSETMASK, 0x10000);
	odm_set_rf_reg(dm, RF_PATH_B, RF_0x0, RFREGOFFSETMASK, 0x10000);
	/*backup RF0x18*/
	lc_cal = odm_get_rf_reg(dm, RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK);
	/*disable RTK*/
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xc4, RFREGOFFSETMASK, 0x01402);
	/*Start LCK*/
	odm_set_rf_reg(dm, RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK,
		       lc_cal | 0x08000);
	ODM_delay_ms(100);
	for (cnt = 0; cnt < 100; cnt++) {
		if (odm_get_rf_reg(dm, RF_PATH_A, RF_CHNLBW, 0x8000) != 0x1)
			break;
		ODM_delay_ms(10);
	}
	/*Recover channel number*/
	odm_set_rf_reg(dm, RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK, lc_cal);
	/*enable RTK*/
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xc4, RFREGOFFSETMASK, 0x81402);
	/**restore*/
	odm_write_4byte(dm, 0xc00, tmp0xc00);
	odm_write_4byte(dm, 0xe00, tmp0xe00);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x0, RFREGOFFSETMASK, 0x3ffff);
	odm_set_rf_reg(dm, RF_PATH_B, RF_0x0, RFREGOFFSETMASK, 0x3ffff);
	RF_DBG(dm, DBG_RF_IQK, "[LCK]LCK end!!!!!!!\n");
}

/*LCK VERSION:0x2*/
void phy_lc_calibrate_8822b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	_phy_lc_calibrate_8822b(dm);
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
	config->phy_lc_calibrate = halrf_lck_trigger;

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	config->get_delta_all_swing_table = get_delta_swing_table_8822b;
#else
	config->get_delta_swing_table = get_delta_swing_table_8822b;
#endif
}

#if ((DM_ODM_SUPPORT_TYPE & ODM_AP) || (DM_ODM_SUPPORT_TYPE == ODM_CE))
void phy_set_rf_path_switch_8822b(struct dm_struct *dm, boolean is_main)
#else
void phy_set_rf_path_switch_8822b(void *adapter, boolean is_main)
#endif
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct dm_struct *dm = &hal_data->DM_OutSrc;
#endif
#endif
	/*BY SY Request */
	odm_set_bb_reg(dm, R_0x4c, (BIT(24) | BIT(23)), 0x2);

	odm_set_bb_reg(dm, R_0x974, 0xff, 0xff);

#if 0
	/*odm_set_bb_reg(dm, R_0x1991, 0x3, 0x0);*/
#endif
	odm_set_bb_reg(dm, R_0x1990, (BIT(9) | BIT(8)), 0x0);

#if 0
	/*odm_set_bb_reg(dm, R_0xcbe, 0x8, 0x0);*/
#endif
	odm_set_bb_reg(dm, R_0xcbc, BIT(19), 0x0);

	odm_set_bb_reg(dm, R_0xcb4, 0xff, 0x77);

	odm_set_bb_reg(dm, R_0x70, MASKBYTE3, 0x0e);
	odm_set_bb_reg(dm, R_0x1704, MASKDWORD, 0x0000ff00);
	odm_set_bb_reg(dm, R_0x1700, MASKDWORD, 0xc00f0038);

	if (dm->rfe_type != 0x12) {
		if (is_main) {
#if 0
			/*odm_set_bb_reg(dm, R_0xcbd, 0x3, 0x2); WiFi*/
#endif
			odm_set_bb_reg(dm, R_0xcbc, (BIT(9) | BIT(8)), 0x2); /*WiFi*/
		} else {
#if 0
			/*odm_set_bb_reg(dm, R_0xcbd, 0x3, 0x1); BT*/
#endif
			odm_set_bb_reg(dm, R_0xcbc, (BIT(9) | BIT(8)), 0x1); /*BT*/
		}
	}
}

#if ((DM_ODM_SUPPORT_TYPE & ODM_AP) || (DM_ODM_SUPPORT_TYPE == ODM_CE))
boolean _phy_query_rf_path_switch_8822b(struct dm_struct *dm)
#else
boolean _phy_query_rf_path_switch_8822b(void *adapter)
#endif
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct dm_struct *dm = &hal_data->DM_OutSrc;
#endif
#endif
	if (odm_get_bb_reg(dm, R_0xcbc, (BIT(9) | BIT(8))) == 0x2) /*WiFi*/
		return true;
	else
		return false;
}

#if ((DM_ODM_SUPPORT_TYPE & ODM_AP) || (DM_ODM_SUPPORT_TYPE == ODM_CE))
boolean phy_query_rf_path_switch_8822b(struct dm_struct *dm)
#else
boolean phy_query_rf_path_switch_8822b(void *adapter)
#endif
{
#if DISABLE_BB_RF
	return true;
#endif
#if ((DM_ODM_SUPPORT_TYPE & ODM_AP) || (DM_ODM_SUPPORT_TYPE == ODM_CE))
	return _phy_query_rf_path_switch_8822b(dm);
#else
	return _phy_query_rf_path_switch_8822b(adapter);
#endif
}

#endif /*(RTL8822B_SUPPORT == 0)*/
