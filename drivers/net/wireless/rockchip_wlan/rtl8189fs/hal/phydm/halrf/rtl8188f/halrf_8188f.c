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
/*#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)*/
#if (DM_ODM_SUPPORT_TYPE == 0x08) /*[PHYDM-262] workaround for SD4 compile warning*/
#if RT_PLATFORM == PLATFORM_MACOSX
#include "phydm_precomp.h"
#else
#include "../phydm_precomp.h"
#endif
#else
#include "../../phydm_precomp.h"
#endif

#if (RTL8188F_SUPPORT == 1)

#define MASKH3BYTES 0xffffff00

/* #define SUCCESS 0 */
/* #define FAIL -1 */

/*---------------------------Define Local Constant---------------------------*/
/* 2010/04/25 MH Define the max tx power tracking tx agc power. */
#define ODM_TXPWRTRACK_MAX_IDX8188F 6

/* MACRO definition for cali_info->TxIQC_8188F[0] */
#define PATH_S0 1 /* RF_PATH_B */
#define idx_0xc94 0
#define idx_0xc80 1
#define idx_0xc4c 2
#define idx_0xc14 0
#define idx_0xca0 1
#define KEY 0
#define VAL 1

/* MACRO definition for cali_info->TxIQC_8188F[1] */
#define PATH_S1 0 /* RF_PATH_A */
#define idx_0xc9c 0
#define idx_0xc88 1
#define idx_0xc4c 2
#define idx_0xc1c 0
#define idx_0xc78 1

/*---------------------------Define Local Constant---------------------------*/

/* 3============================================================
 * 3 Tx Power Tracking
 * 3============================================================ */

void set_iqk_matrix_8188f(struct dm_struct *dm, s8 OFDM_index, u8 rf_path,
			  s32 iqk_result_x, s32 iqk_result_y)
{
	s32 ele_A = 0, ele_D, ele_C = 0, value32;

	if (OFDM_index >= OFDM_TABLE_SIZE)
		OFDM_index = OFDM_TABLE_SIZE - 1;
	else if (OFDM_index < 0)
		OFDM_index = 0;

	ele_D = (ofdm_swing_table_new[OFDM_index] & 0xFFC00000) >> 22;

	/* new element A = element D x X */
	if (iqk_result_x != 0 && (*dm->band_type == ODM_BAND_2_4G)) {
		if ((iqk_result_x & 0x00000200) != 0) /* consider minus */
			iqk_result_x = iqk_result_x | 0xFFFFFC00;
		ele_A = ((iqk_result_x * ele_D) >> 8) & 0x000003FF;

		/* new element C = element D x Y */
		if ((iqk_result_y & 0x00000200) != 0)
			iqk_result_y = iqk_result_y | 0xFFFFFC00;
		ele_C = ((iqk_result_y * ele_D) >> 8) & 0x000003FF;

		if (rf_path == RF_PATH_A)
			switch (rf_path) {
			case RF_PATH_A:
				/* wirte new elements A, C, D to regC80 and regC94, element B is always 0 */
				value32 = (ele_D << 22) | ((ele_C & 0x3F) << 16) | ele_A;
				odm_set_bb_reg(dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD, value32);

				value32 = (ele_C & 0x000003C0) >> 6;
				odm_set_bb_reg(dm, REG_OFDM_0_XC_TX_AFE, MASKH4BITS, value32);

				value32 = ((iqk_result_x * ele_D) >> 7) & 0x01;
				odm_set_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, BIT(24), value32);
				break;
			case RF_PATH_B:
				/* wirte new elements A, C, D to regC88 and regC9C, element B is always 0 */
				value32 = (ele_D << 22) | ((ele_C & 0x3F) << 16) | ele_A;
				odm_set_bb_reg(dm, REG_OFDM_0_XB_TX_IQ_IMBALANCE, MASKDWORD, value32);

				value32 = (ele_C & 0x000003C0) >> 6;
				odm_set_bb_reg(dm, REG_OFDM_0_XD_TX_AFE, MASKH4BITS, value32);

				value32 = ((iqk_result_x * ele_D) >> 7) & 0x01;
				odm_set_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, BIT(28), value32);

				break;
			default:
				break;
			}
	} else {
		switch (rf_path) {
		case RF_PATH_A:
			odm_set_bb_reg(dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD, ofdm_swing_table_new[OFDM_index]);
			odm_set_bb_reg(dm, REG_OFDM_0_XC_TX_AFE, MASKH4BITS, 0x00);
			odm_set_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, BIT(24), 0x00);
			break;

		case RF_PATH_B:
			odm_set_bb_reg(dm, REG_OFDM_0_XB_TX_IQ_IMBALANCE, MASKDWORD, ofdm_swing_table_new[OFDM_index]);
			odm_set_bb_reg(dm, REG_OFDM_0_XD_TX_AFE, MASKH4BITS, 0x00);
			odm_set_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, BIT(28), 0x00);
			break;

		default:
			break;
		}
	}

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "TxPwrTracking path B: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x 0xeb4 = 0x%x 0xebc = 0x%x\n",
	       (u32)iqk_result_x, (u32)iqk_result_y, (u32)ele_A, (u32)ele_C,
	       (u32)ele_D, (u32)iqk_result_x, (u32)iqk_result_y);
}

void do_iqk_8188f(void *dm_void, u8 delta_thermal_index, u8 thermal_value,
		  u8 threshold)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	odm_reset_iqk_result(dm);
	dm->rf_calibrate_info.thermal_value_iqk = thermal_value;
	halrf_iqk_trigger(dm, false);
}

/*-----------------------------------------------------------------------------
 * Function:	odm_TxPwrTrackSetPwr88E()
 *
 * Overview:	88E change all channel tx power accordign to flag.
 *				OFDM & CCK are all different.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	04/23/2012	MHC		Create version 0.
 *
 *---------------------------------------------------------------------------*/
void odm_tx_pwr_track_set_pwr_8188f(void *dm_void, enum pwrtrack_method method,
				    u8 rf_path, u8 channel_mapped_index)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	void *adapter = dm->adapter;
	//PHAL_DATA_TYPE hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	u8 pwr_tracking_limit_ofdm = 32;
	u8 pwr_tracking_limit_cck = CCK_TABLE_SIZE_88F - 1; /* -2dB */
	u8 tx_rate = 0xFF;
	s8 final_ofdm_swing_index = 0;
	s8 final_cck_swing_index = 0;
	/*	u8	i = 0; */
	struct dm_rf_calibration_struct *cali_info = &(dm->rf_calibrate_info);

#if 0
	struct _hal_rf_	*rf = &(dm->rf_table);

	if (*dm->mp_mode == true) {
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
#if (MP_DRIVER == 1)
		PMPT_CONTEXT p_mpt_ctx = &(adapter->mpt_ctx);

		tx_rate = mpt_to_mgnt_rate(p_mpt_ctx->mpt_rate_index);
#endif
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
#ifdef CONFIG_MP_INCLUDED
		PMPT_CONTEXT p_mpt_ctx = &(adapter->mppriv.mpt_ctx);

		tx_rate = mpt_to_mgnt_rate(p_mpt_ctx->mpt_rate_index);
#endif
#endif
#endif
	} else {
		u16 rate	 = *(dm->forced_data_rate);

		if (!rate) { /*auto rate*/
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
			tx_rate = ((PADAPTER)adapter)->HalFunc.GetHwRateFromMRateHandler(dm->tx_rate);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
			if (dm->number_linked_client != 0)
				tx_rate = hw_rate_to_m_rate(dm->tx_rate);
			else
				tx_rate = rf->p_rate_index;
#endif
		} else	 /*force rate*/
			tx_rate = (u8)rate;
	}

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "Power Tracking tx_rate=0x%X\n",
	       tx_rate);
#endif

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "===>ODM_TxPwrTrackSetPwr8188F\n");

	if (tx_rate != 0xFF) {
		/* 2 CCK */
		if ((tx_rate >= MGN_1M && tx_rate <= MGN_5_5M) || tx_rate == MGN_11M)
			pwr_tracking_limit_cck = CCK_TABLE_SIZE_88F - 1; /* -2dB */
		/* 2 OFDM */
		else if ((tx_rate >= MGN_6M) && (tx_rate <= MGN_48M))
			pwr_tracking_limit_ofdm = 36; /* +3dB */
		else if (tx_rate == MGN_54M)
			pwr_tracking_limit_ofdm = 34; /* +2dB */

		/* 2 HT */
		else if ((tx_rate >= MGN_MCS0) && (tx_rate <= MGN_MCS2)) /* QPSK/BPSK */
			pwr_tracking_limit_ofdm = 38; /* +4dB */
		else if ((tx_rate >= MGN_MCS3) && (tx_rate <= MGN_MCS4)) /* 16QAM */
			pwr_tracking_limit_ofdm = 36; /* +3dB */
		else if ((tx_rate >= MGN_MCS5) && (tx_rate <= MGN_MCS7)) /* 64QAM */
			pwr_tracking_limit_ofdm = 34; /* +2dB */

		else
			pwr_tracking_limit_ofdm = cali_info->default_ofdm_index; /* Default OFDM index = 30 */
	}
	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "tx_rate=0x%x, pwr_tracking_limit=%d\n",
	       tx_rate, pwr_tracking_limit_ofdm);

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "method=%d\n", method);

	if (method == TXAGC) {
/* u8	rf = 0; */
#if (MP_DRIVER == 1)
		u32 pwr = 0, tx_agc = 0;
#endif
		//void *adapter = dm->adapter;

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		       "odm_TxPwrTrackSetPwr8188F CH=%d\n", *(dm->channel));

		cali_info->remnant_ofdm_swing_idx[rf_path] = cali_info->absolute_ofdm_swing_idx[rf_path];

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))

#if (MP_DRIVER == 1)
		if ((*dm->mp_mode) == 1) {
			pwr = odm_get_bb_reg(dm, REG_TX_AGC_B_CCK_11_A_CCK_2_11, MASKBYTE1);
			pwr += dm->rf_calibrate_info.power_index_offset[RF_PATH_A];
			odm_set_bb_reg(dm, REG_TX_AGC_A_CCK_1_MCS32, MASKBYTE1, pwr); /* CCK 1M */

			if (pwr > 0x3F)
				pwr = 0x3F; /* add by Mingzhi.Guo 2015-04-10 */
			else if (pwr <= 0)
				pwr = 0;

			tx_agc = (pwr << 16) | (pwr << 8) | (pwr);

			odm_set_bb_reg(dm, REG_TX_AGC_B_CCK_11_A_CCK_2_11, 0xffffff00, tx_agc); /* CCK 2~11M */
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       "ODM_TxPwrTrackSetPwr8188F: CCK Tx-rf(A) Power = 0x%x\n",
			       tx_agc);

			pwr = odm_get_bb_reg(dm, REG_TX_AGC_A_RATE18_06, 0xFF);
			pwr += (cali_info->bb_swing_idx_ofdm[RF_PATH_A] - cali_info->bb_swing_idx_ofdm_base[RF_PATH_A]);
			tx_agc |= ((pwr << 24) | (pwr << 16) | (pwr << 8) | pwr);
			odm_set_bb_reg(dm, REG_TX_AGC_A_RATE18_06, MASKDWORD, tx_agc);
			odm_set_bb_reg(dm, REG_TX_AGC_A_RATE54_24, MASKDWORD, tx_agc);
			odm_set_bb_reg(dm, REG_TX_AGC_A_MCS03_MCS00, MASKDWORD, tx_agc);
			odm_set_bb_reg(dm, REG_TX_AGC_A_MCS07_MCS04, MASKDWORD, tx_agc);
			/*	odm_set_bb_reg(adapter, REG_TX_AGC_A_MCS11_MCS08, MASKDWORD, tx_agc); */
			/*	odm_set_bb_reg(adapter, REG_TX_AGC_A_MCS15_MCS12, MASKDWORD, tx_agc); */
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       "ODM_TxPwrTrackSetPwr8188F: OFDM Tx-rf(A) Power = 0x%x\n",
			       tx_agc);
		} else
#endif
		{
			/* PHY_SetTxPowerLevelByPath8188F(adapter, hal_data->current_channel, RF_PATH_A); */
			/* PHY_SetTxPowerLevel8188F(dm->adapter, *dm->channel); */
			cali_info->modify_tx_agc_flag_path_a = true;
			cali_info->modify_tx_agc_flag_path_a_cck = true;

			if (rf_path == RF_PATH_A) {
				odm_set_tx_power_index_by_rate_section(dm, RF_PATH_A, *dm->channel, CCK);
				odm_set_tx_power_index_by_rate_section(dm, RF_PATH_A, *dm->channel, OFDM);
				odm_set_tx_power_index_by_rate_section(dm, RF_PATH_A, *dm->channel, HT_MCS0_MCS7);
			}
		}

#endif
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
/* phy_rf6052_set_cck_tx_power(dm->priv, *(dm->channel)); */
/* phy_rf6052_set_ofdm_tx_power(dm->priv, *(dm->channel)); */
#endif

	} else if (method == BBSWING) {
		final_ofdm_swing_index = cali_info->default_ofdm_index + cali_info->absolute_ofdm_swing_idx[rf_path];
		final_cck_swing_index = cali_info->default_cck_index + cali_info->absolute_ofdm_swing_idx[rf_path];

		/* Adjust BB swing by OFDM IQ matrix */
		if (final_ofdm_swing_index >= pwr_tracking_limit_ofdm)
			final_ofdm_swing_index = pwr_tracking_limit_ofdm;
		else if (final_ofdm_swing_index <= 0)
			final_ofdm_swing_index = 0;

		if (final_cck_swing_index >= CCK_TABLE_SIZE_88F)
			final_cck_swing_index = CCK_TABLE_SIZE_88F - 1;
		else if ((s8)cali_info->bb_swing_idx_cck < 0)
			final_cck_swing_index = 0;

		if (rf_path == RF_PATH_A) {
			set_iqk_matrix_8188f(dm, final_ofdm_swing_index, RF_PATH_A,
					     dm->rf_calibrate_info.iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
					     dm->rf_calibrate_info.iqk_matrix_reg_setting[channel_mapped_index].value[0][1]);
			if (*dm->channel != 14) {
				odm_write_1byte(dm, 0xa22, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][0]);
				odm_write_1byte(dm, 0xa23, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][1]);
				odm_write_1byte(dm, 0xa24, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][2]);
				odm_write_1byte(dm, 0xa25, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][3]);
				odm_write_1byte(dm, 0xa26, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][4]);
				odm_write_1byte(dm, 0xa27, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][5]);
				odm_write_1byte(dm, 0xa28, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][6]);
				odm_write_1byte(dm, 0xa29, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][7]);
				odm_write_1byte(dm, 0xa9a, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][8]);
				odm_write_1byte(dm, 0xa9b, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][9]);
				odm_write_1byte(dm, 0xa9c, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][10]);
				odm_write_1byte(dm, 0xa9d, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][11]);
				odm_write_1byte(dm, 0xaa0, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][12]);
				odm_write_1byte(dm, 0xaa1, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][13]);
				odm_write_1byte(dm, 0xaa2, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][14]);
				odm_write_1byte(dm, 0xaa3, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][15]);
			} else {
				odm_write_1byte(dm, 0xa22, cck_swing_table_ch14_88f[final_cck_swing_index][0]);
				odm_write_1byte(dm, 0xa23, cck_swing_table_ch14_88f[final_cck_swing_index][1]);
				odm_write_1byte(dm, 0xa24, cck_swing_table_ch14_88f[final_cck_swing_index][2]);
				odm_write_1byte(dm, 0xa25, cck_swing_table_ch14_88f[final_cck_swing_index][3]);
				odm_write_1byte(dm, 0xa26, cck_swing_table_ch14_88f[final_cck_swing_index][4]);
				odm_write_1byte(dm, 0xa27, cck_swing_table_ch14_88f[final_cck_swing_index][5]);
				odm_write_1byte(dm, 0xa28, cck_swing_table_ch14_88f[final_cck_swing_index][6]);
				odm_write_1byte(dm, 0xa29, cck_swing_table_ch14_88f[final_cck_swing_index][7]);
				odm_write_1byte(dm, 0xa9a, cck_swing_table_ch14_88f[final_cck_swing_index][8]);
				odm_write_1byte(dm, 0xa9b, cck_swing_table_ch14_88f[final_cck_swing_index][9]);
				odm_write_1byte(dm, 0xa9c, cck_swing_table_ch14_88f[final_cck_swing_index][10]);
				odm_write_1byte(dm, 0xa9d, cck_swing_table_ch14_88f[final_cck_swing_index][11]);
				odm_write_1byte(dm, 0xaa0, cck_swing_table_ch14_88f[final_cck_swing_index][12]);
				odm_write_1byte(dm, 0xaa1, cck_swing_table_ch14_88f[final_cck_swing_index][13]);
				odm_write_1byte(dm, 0xaa2, cck_swing_table_ch14_88f[final_cck_swing_index][14]);
				odm_write_1byte(dm, 0xaa3, cck_swing_table_ch14_88f[final_cck_swing_index][15]);
			}
		}

	} else if (method == MIX_MODE) {
#if (MP_DRIVER == 1)
		/* u32 	pwr = 0, tx_agc = 0; */
		u32 tx_agc = 0; /* add by Mingzhi.Guo 2015-04-10 */
		s32 pwr = 0;
/* s32	pwr_down_up = 0; */
#endif
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "method is MIX_MODE ====>\n");
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		       "cali_info->default_ofdm_index=%d,  cali_info->DefaultCCKIndex=%d, cali_info->absolute_ofdm_swing_idx[rf_path]=%d, rf_path = %d\n",
		       cali_info->default_ofdm_index,
		       cali_info->default_cck_index,
		       cali_info->absolute_ofdm_swing_idx[rf_path], rf_path);

		final_ofdm_swing_index = cali_info->default_ofdm_index + cali_info->absolute_ofdm_swing_idx[rf_path];
		final_cck_swing_index = cali_info->default_cck_index + cali_info->absolute_ofdm_swing_idx[rf_path];
		if (rf_path == RF_PATH_A) {
			if (final_ofdm_swing_index > pwr_tracking_limit_ofdm) { /* BBSwing higher then Limit */
				cali_info->remnant_ofdm_swing_idx[rf_path] = final_ofdm_swing_index - pwr_tracking_limit_ofdm;

				set_iqk_matrix_8188f(dm, pwr_tracking_limit_ofdm, rf_path,
						     dm->rf_calibrate_info.iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
						     dm->rf_calibrate_info.iqk_matrix_reg_setting[channel_mapped_index].value[0][1]);

				cali_info->modify_tx_agc_flag_path_a = true;

				/* Set tx_agc Page C{}; */

				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				       "******Path_A Over BBSwing Limit, pwr_tracking_limit = %d, Remnant tx_agc value = %d\n",
				       pwr_tracking_limit_ofdm,
				       cali_info->remnant_ofdm_swing_idx[rf_path
				       ]);
			} else if (final_ofdm_swing_index < cali_info->default_ofdm_index) {
				cali_info->remnant_ofdm_swing_idx[rf_path] = final_ofdm_swing_index - cali_info->default_ofdm_index;
				set_iqk_matrix_8188f(dm, cali_info->default_ofdm_index, RF_PATH_A,
						     dm->rf_calibrate_info.iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
						     dm->rf_calibrate_info.iqk_matrix_reg_setting[channel_mapped_index].value[0][1]);

				cali_info->modify_tx_agc_flag_path_a = true;
				/* Set tx_agc Page C{}; */

				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				       "******Path_A Lower then BBSwing lower bound  28, Remnant tx_agc value = %d\n",
				       cali_info->remnant_ofdm_swing_idx[rf_path
				       ]);
			}

#if 0
			else if (final_ofdm_swing_index < 0) {
				cali_info->remnant_ofdm_swing_idx[rf_path] = final_ofdm_swing_index ;
				set_iqk_matrix_8188f(dm, 0, RF_PATH_A,
					dm->rf_calibrate_info.iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
					dm->rf_calibrate_info.iqk_matrix_reg_setting[channel_mapped_index].value[0][1]);

				cali_info->modify_tx_agc_flag_path_a = true;
				/* Set tx_agc Page C{}; */

				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				       "******Path_A Lower then BBSwing lower bound  0, Remnant tx_agc value = %d\n",
				       cali_info->remnant_ofdm_swing_idx[rf_path
				       ]);
			}
#endif
			else {
				set_iqk_matrix_8188f(dm, final_ofdm_swing_index, RF_PATH_A,
						     dm->rf_calibrate_info.iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
						     dm->rf_calibrate_info.iqk_matrix_reg_setting[channel_mapped_index].value[0][1]);

				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				       "******Path_A Compensate with BBSwing, final_ofdm_swing_index = %d\n",
				       final_ofdm_swing_index);

				if (cali_info->modify_tx_agc_flag_path_a) /* If tx_agc has changed, reset tx_agc again */
					cali_info->remnant_ofdm_swing_idx[rf_path] = 0;
			}
#if (MP_DRIVER == 1) && (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
			if ((*dm->mp_mode) == 1) {
				pwr = odm_get_bb_reg(dm, REG_TX_AGC_A_RATE18_06, 0xFF);
				/* pwr_down_up = (cali_info->remnant_ofdm_swing_idx[RF_PATH_A] - cali_info->modify_tx_agc_value_ofdm); */
				pwr += (cali_info->remnant_ofdm_swing_idx[RF_PATH_A] - cali_info->modify_tx_agc_value_ofdm);

				if (pwr > 0x3F)
					pwr = 0x3F; /* add by Mingzhi.Guo 2015-04-10 */
				else if (pwr < 0)
					pwr = 0;

#if 0
				if (pwr == 0x32 || pwr == 0x33) {	/*8188F TXAGC skip index 32&33 to avoid bad TX EVM, suggested  by RF_Jayden*/
					if (pwr_down_up >= 0)
						pwr = 0x34;
					else
						pwr = 0x31;
				}
#endif

				tx_agc |= ((pwr << 24) | (pwr << 16) | (pwr << 8) | pwr);
				odm_set_bb_reg(dm, REG_TX_AGC_A_RATE18_06, MASKDWORD, tx_agc);
				odm_set_bb_reg(dm, REG_TX_AGC_A_RATE54_24, MASKDWORD, tx_agc);
				odm_set_bb_reg(dm, REG_TX_AGC_A_MCS03_MCS00, MASKDWORD, tx_agc);
				odm_set_bb_reg(dm, REG_TX_AGC_A_MCS07_MCS04, MASKDWORD, tx_agc);
				/* odm_set_bb_reg(dm, REG_TX_AGC_A_MCS11_MCS08, MASKDWORD, tx_agc); */
				/* odm_set_bb_reg(dm, REG_TX_AGC_A_MCS15_MCS12, MASKDWORD, tx_agc); */
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				       "ODM_TxPwrTrackSetPwr8188F: OFDM Tx-rf(A) Power = 0x%x\n",
				       tx_agc);

			} else
#endif
			{
				/* Set tx_agc Page C{}; */
				odm_set_tx_power_index_by_rate_section(dm, RF_PATH_A, *dm->channel, OFDM);
				odm_set_tx_power_index_by_rate_section(dm, RF_PATH_A, *dm->channel, HT_MCS0_MCS7);
			}
			cali_info->modify_tx_agc_value_ofdm = cali_info->remnant_ofdm_swing_idx[RF_PATH_A]; /* add by Mingzhi.Guo */

			/* MIX mode: CCK */
			if (final_cck_swing_index > pwr_tracking_limit_cck) {
				cali_info->remnant_cck_swing_idx = final_cck_swing_index - pwr_tracking_limit_cck;
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				       "******Path_A CCK Over Limit, pwr_tracking_limit_cck = %d, cali_info->remnant_cck_swing_idx  = %d\n",
				       pwr_tracking_limit_cck,
				       cali_info->remnant_cck_swing_idx);
				if (*dm->channel != 14) {
					odm_write_1byte(dm, 0xa22, cck_swing_table_ch1_ch13_88f[pwr_tracking_limit_cck][0]);
					odm_write_1byte(dm, 0xa23, cck_swing_table_ch1_ch13_88f[pwr_tracking_limit_cck][1]);
					odm_write_1byte(dm, 0xa24, cck_swing_table_ch1_ch13_88f[pwr_tracking_limit_cck][2]);
					odm_write_1byte(dm, 0xa25, cck_swing_table_ch1_ch13_88f[pwr_tracking_limit_cck][3]);
					odm_write_1byte(dm, 0xa26, cck_swing_table_ch1_ch13_88f[pwr_tracking_limit_cck][4]);
					odm_write_1byte(dm, 0xa27, cck_swing_table_ch1_ch13_88f[pwr_tracking_limit_cck][5]);
					odm_write_1byte(dm, 0xa28, cck_swing_table_ch1_ch13_88f[pwr_tracking_limit_cck][6]);
					odm_write_1byte(dm, 0xa29, cck_swing_table_ch1_ch13_88f[pwr_tracking_limit_cck][7]);
					odm_write_1byte(dm, 0xa9a, cck_swing_table_ch1_ch13_88f[pwr_tracking_limit_cck][8]);
					odm_write_1byte(dm, 0xa9b, cck_swing_table_ch1_ch13_88f[pwr_tracking_limit_cck][9]);
					odm_write_1byte(dm, 0xa9c, cck_swing_table_ch1_ch13_88f[pwr_tracking_limit_cck][10]);
					odm_write_1byte(dm, 0xa9d, cck_swing_table_ch1_ch13_88f[pwr_tracking_limit_cck][11]);
					odm_write_1byte(dm, 0xaa0, cck_swing_table_ch1_ch13_88f[pwr_tracking_limit_cck][12]);
					odm_write_1byte(dm, 0xaa1, cck_swing_table_ch1_ch13_88f[pwr_tracking_limit_cck][13]);
					odm_write_1byte(dm, 0xaa2, cck_swing_table_ch1_ch13_88f[pwr_tracking_limit_cck][14]);
					odm_write_1byte(dm, 0xaa3, cck_swing_table_ch1_ch13_88f[pwr_tracking_limit_cck][15]);
				} else {
					odm_write_1byte(dm, 0xa22, cck_swing_table_ch14_88f[pwr_tracking_limit_cck][0]);
					odm_write_1byte(dm, 0xa23, cck_swing_table_ch14_88f[pwr_tracking_limit_cck][1]);
					odm_write_1byte(dm, 0xa24, cck_swing_table_ch14_88f[pwr_tracking_limit_cck][2]);
					odm_write_1byte(dm, 0xa25, cck_swing_table_ch14_88f[pwr_tracking_limit_cck][3]);
					odm_write_1byte(dm, 0xa26, cck_swing_table_ch14_88f[pwr_tracking_limit_cck][4]);
					odm_write_1byte(dm, 0xa27, cck_swing_table_ch14_88f[pwr_tracking_limit_cck][5]);
					odm_write_1byte(dm, 0xa28, cck_swing_table_ch14_88f[pwr_tracking_limit_cck][6]);
					odm_write_1byte(dm, 0xa29, cck_swing_table_ch14_88f[pwr_tracking_limit_cck][7]);
					odm_write_1byte(dm, 0xa9a, cck_swing_table_ch14_88f[pwr_tracking_limit_cck][8]);
					odm_write_1byte(dm, 0xa9b, cck_swing_table_ch14_88f[pwr_tracking_limit_cck][9]);
					odm_write_1byte(dm, 0xa9c, cck_swing_table_ch14_88f[pwr_tracking_limit_cck][10]);
					odm_write_1byte(dm, 0xa9d, cck_swing_table_ch14_88f[pwr_tracking_limit_cck][11]);
					odm_write_1byte(dm, 0xaa0, cck_swing_table_ch14_88f[pwr_tracking_limit_cck][12]);
					odm_write_1byte(dm, 0xaa1, cck_swing_table_ch14_88f[pwr_tracking_limit_cck][13]);
					odm_write_1byte(dm, 0xaa2, cck_swing_table_ch14_88f[pwr_tracking_limit_cck][14]);
					odm_write_1byte(dm, 0xaa3, cck_swing_table_ch14_88f[pwr_tracking_limit_cck][15]);
				}

				cali_info->modify_tx_agc_flag_path_a_cck = true;

			} else if (final_cck_swing_index < 0) { /* Lowest CCK index = 0 */
				cali_info->remnant_cck_swing_idx = final_cck_swing_index;

				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				       "******Path_A CCK Under Limit, pwr_tracking_limit_cck = %d, cali_info->remnant_cck_swing_idx  = %d\n",
				       0, cali_info->remnant_cck_swing_idx);
				if (*dm->channel != 14) {
					odm_write_1byte(dm, 0xa22, cck_swing_table_ch1_ch13_88f[0][0]);
					odm_write_1byte(dm, 0xa23, cck_swing_table_ch1_ch13_88f[0][1]);
					odm_write_1byte(dm, 0xa24, cck_swing_table_ch1_ch13_88f[0][2]);
					odm_write_1byte(dm, 0xa25, cck_swing_table_ch1_ch13_88f[0][3]);
					odm_write_1byte(dm, 0xa26, cck_swing_table_ch1_ch13_88f[0][4]);
					odm_write_1byte(dm, 0xa27, cck_swing_table_ch1_ch13_88f[0][5]);
					odm_write_1byte(dm, 0xa28, cck_swing_table_ch1_ch13_88f[0][6]);
					odm_write_1byte(dm, 0xa29, cck_swing_table_ch1_ch13_88f[0][7]);
					odm_write_1byte(dm, 0xa9a, cck_swing_table_ch1_ch13_88f[0][8]);
					odm_write_1byte(dm, 0xa9b, cck_swing_table_ch1_ch13_88f[0][9]);
					odm_write_1byte(dm, 0xa9c, cck_swing_table_ch1_ch13_88f[0][10]);
					odm_write_1byte(dm, 0xa9d, cck_swing_table_ch1_ch13_88f[0][11]);
					odm_write_1byte(dm, 0xaa0, cck_swing_table_ch1_ch13_88f[0][12]);
					odm_write_1byte(dm, 0xaa1, cck_swing_table_ch1_ch13_88f[0][13]);
					odm_write_1byte(dm, 0xaa2, cck_swing_table_ch1_ch13_88f[0][14]);
					odm_write_1byte(dm, 0xaa3, cck_swing_table_ch1_ch13_88f[0][15]);
				} else {
					odm_write_1byte(dm, 0xa22, cck_swing_table_ch14_88f[0][0]);
					odm_write_1byte(dm, 0xa23, cck_swing_table_ch14_88f[0][1]);
					odm_write_1byte(dm, 0xa24, cck_swing_table_ch14_88f[0][2]);
					odm_write_1byte(dm, 0xa25, cck_swing_table_ch14_88f[0][3]);
					odm_write_1byte(dm, 0xa26, cck_swing_table_ch14_88f[0][4]);
					odm_write_1byte(dm, 0xa27, cck_swing_table_ch14_88f[0][5]);
					odm_write_1byte(dm, 0xa28, cck_swing_table_ch14_88f[0][6]);
					odm_write_1byte(dm, 0xa29, cck_swing_table_ch14_88f[0][7]);
					odm_write_1byte(dm, 0xa9a, cck_swing_table_ch14_88f[0][8]);
					odm_write_1byte(dm, 0xa9b, cck_swing_table_ch14_88f[0][9]);
					odm_write_1byte(dm, 0xa9c, cck_swing_table_ch14_88f[0][10]);
					odm_write_1byte(dm, 0xa9d, cck_swing_table_ch14_88f[0][11]);
					odm_write_1byte(dm, 0xaa0, cck_swing_table_ch14_88f[0][12]);
					odm_write_1byte(dm, 0xaa1, cck_swing_table_ch14_88f[0][13]);
					odm_write_1byte(dm, 0xaa2, cck_swing_table_ch14_88f[0][14]);
					odm_write_1byte(dm, 0xaa3, cck_swing_table_ch14_88f[0][15]);
				}
				cali_info->modify_tx_agc_flag_path_a_cck = true;
			}

			else {
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				       "******Path_A CCK Compensate with BBSwing, final_cck_swing_index = %d\n",
				       final_cck_swing_index);
				if (*dm->channel != 14) {
					odm_write_1byte(dm, 0xa22, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][0]);
					odm_write_1byte(dm, 0xa23, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][1]);
					odm_write_1byte(dm, 0xa24, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][2]);
					odm_write_1byte(dm, 0xa25, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][3]);
					odm_write_1byte(dm, 0xa26, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][4]);
					odm_write_1byte(dm, 0xa27, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][5]);
					odm_write_1byte(dm, 0xa28, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][6]);
					odm_write_1byte(dm, 0xa29, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][7]);
					odm_write_1byte(dm, 0xa9a, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][8]);
					odm_write_1byte(dm, 0xa9b, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][9]);
					odm_write_1byte(dm, 0xa9c, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][10]);
					odm_write_1byte(dm, 0xa9d, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][11]);
					odm_write_1byte(dm, 0xaa0, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][12]);
					odm_write_1byte(dm, 0xaa1, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][13]);
					odm_write_1byte(dm, 0xaa2, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][14]);
					odm_write_1byte(dm, 0xaa3, cck_swing_table_ch1_ch13_88f[final_cck_swing_index][15]);
				} else {
					odm_write_1byte(dm, 0xa22, cck_swing_table_ch14_88f[final_cck_swing_index][0]);
					odm_write_1byte(dm, 0xa23, cck_swing_table_ch14_88f[final_cck_swing_index][1]);
					odm_write_1byte(dm, 0xa24, cck_swing_table_ch14_88f[final_cck_swing_index][2]);
					odm_write_1byte(dm, 0xa25, cck_swing_table_ch14_88f[final_cck_swing_index][3]);
					odm_write_1byte(dm, 0xa26, cck_swing_table_ch14_88f[final_cck_swing_index][4]);
					odm_write_1byte(dm, 0xa27, cck_swing_table_ch14_88f[final_cck_swing_index][5]);
					odm_write_1byte(dm, 0xa28, cck_swing_table_ch14_88f[final_cck_swing_index][6]);
					odm_write_1byte(dm, 0xa29, cck_swing_table_ch14_88f[final_cck_swing_index][7]);
					odm_write_1byte(dm, 0xa9a, cck_swing_table_ch14_88f[final_cck_swing_index][8]);
					odm_write_1byte(dm, 0xa9b, cck_swing_table_ch14_88f[final_cck_swing_index][9]);
					odm_write_1byte(dm, 0xa9c, cck_swing_table_ch14_88f[final_cck_swing_index][10]);
					odm_write_1byte(dm, 0xa9d, cck_swing_table_ch14_88f[final_cck_swing_index][11]);
					odm_write_1byte(dm, 0xaa0, cck_swing_table_ch14_88f[final_cck_swing_index][12]);
					odm_write_1byte(dm, 0xaa1, cck_swing_table_ch14_88f[final_cck_swing_index][13]);
					odm_write_1byte(dm, 0xaa2, cck_swing_table_ch14_88f[final_cck_swing_index][14]);
					odm_write_1byte(dm, 0xaa3, cck_swing_table_ch14_88f[final_cck_swing_index][15]);
				}

				cali_info->modify_tx_agc_flag_path_a_cck = false;
				cali_info->remnant_cck_swing_idx = 0;
			}
#if (MP_DRIVER == 1) && (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
			if ((*dm->mp_mode) == 1) {
				pwr = odm_get_bb_reg(dm, REG_TX_AGC_B_CCK_11_A_CCK_2_11, MASKBYTE1);
				pwr += cali_info->remnant_cck_swing_idx - cali_info->modify_tx_agc_value_cck;

				if (pwr > 0x3F)
					pwr = 0x3F; /* add by Mingzhi.Guo 2015-04-10 */
				else if (pwr < 0)
					pwr = 0;

				odm_set_bb_reg(dm, REG_TX_AGC_A_CCK_1_MCS32, MASKBYTE1, pwr); /* CCK 1M */
				tx_agc = (pwr << 16) | (pwr << 8) | (pwr);
				odm_set_bb_reg(dm, REG_TX_AGC_B_CCK_11_A_CCK_2_11, 0xffffff00, tx_agc); /* CCK 2~11M */
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				       "ODM_TxPwrTrackSetPwr8188F: CCK Tx-rf(A) Power = 0x%x\n",
				       tx_agc);
			} else
#endif
			{
				/* Set tx_agc Page C{}; */
				odm_set_tx_power_index_by_rate_section(dm, RF_PATH_A, *dm->channel, CCK);
			}
			cali_info->modify_tx_agc_value_cck = cali_info->remnant_cck_swing_idx;
		}
	} else
		return;
} /* odm_TxPwrTrackSetPwr8188F */

void get_delta_swing_table_8188f(void *dm_void, u8 **temperature_up_a,
				 u8 **temperature_down_a, u8 **temperature_up_b,
				 u8 **temperature_down_b)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ADAPTER *adapter = dm->adapter;
	struct dm_rf_calibration_struct *cali_info = &(dm->rf_calibrate_info);
	struct _hal_rf_ *rf = &(dm->rf_table);
	u8 tx_rate = 0xFF;
	u8 channel = *dm->channel;

	if (*dm->mp_mode == true) {
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
#if (MP_DRIVER == 1)
		PMPT_CONTEXT p_mpt_ctx = &(adapter->MptCtx);

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
		u16 rate = *(dm->forced_data_rate);

		if (!rate) { /*auto rate*/
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
			tx_rate = ((PADAPTER)adapter)->HalFunc.GetHwRateFromMRateHandler(dm->tx_rate);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
			if (dm->number_linked_client != 0)
				tx_rate = hw_rate_to_m_rate(dm->tx_rate);
			else
				tx_rate = rf->p_rate_index;
#endif
		} else /*force rate*/
			tx_rate = (u8)rate;
	}

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "Power Tracking tx_rate=0x%X\n",
	       tx_rate);

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "%s ====> channel is %d\n", __func__,
	       channel);

	if (1 <= channel && channel <= 14) {
		if (IS_CCK_RATE(tx_rate)) {
			*temperature_up_a = cali_info->delta_swing_table_idx_2g_cck_a_p;
			*temperature_down_a = cali_info->delta_swing_table_idx_2g_cck_a_n;
			*temperature_up_b = cali_info->delta_swing_table_idx_2g_cck_b_p;
			*temperature_down_b = cali_info->delta_swing_table_idx_2g_cck_b_n;
		} else {
			*temperature_up_a = cali_info->delta_swing_table_idx_2ga_p;
			*temperature_down_a = cali_info->delta_swing_table_idx_2ga_n;
			*temperature_up_b = cali_info->delta_swing_table_idx_2gb_p;
			*temperature_down_b = cali_info->delta_swing_table_idx_2gb_n;
		}
	} else {
		*temperature_up_a = (u8 *)delta_swing_table_idx_2ga_p_8188e;
		*temperature_down_a = (u8 *)delta_swing_table_idx_2ga_n_8188e;
		*temperature_up_b = (u8 *)delta_swing_table_idx_2ga_p_8188e;
		*temperature_down_b = (u8 *)delta_swing_table_idx_2ga_n_8188e;
	}

	return;
}

void configure_txpower_track_8188f(struct txpwrtrack_cfg *config)
{
	config->swing_table_size_cck = CCK_TABLE_SIZE_88F;
	config->swing_table_size_ofdm = OFDM_TABLE_SIZE;
	config->threshold_iqk = IQK_THRESHOLD;
	config->average_thermal_num = AVG_THERMAL_NUM_8188F;
	config->rf_path_count = MAX_PATH_NUM_8188F;
	config->thermal_reg_addr = RF_T_METER_8188F;

	config->odm_tx_pwr_track_set_pwr = odm_tx_pwr_track_set_pwr_8188f;
	config->do_iqk = do_iqk_8188f;
	config->phy_lc_calibrate = halrf_lck_trigger;
	config->get_delta_swing_table = get_delta_swing_table_8188f;
}

/* 1 7.	IQK */
#define MAX_TOLERANCE 5
#define IQK_DELAY_TIME 1 /* ms */

u8 /* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
	phy_path_a_iqk_8188f(
		struct dm_struct *dm,
		boolean config_path_b)
{
	u32 reg_eac, reg_e94, reg_e9c /*, reg_ea4*/;
	u8 result = 0x00;
	RF_DBG(dm, DBG_RF_IQK, "path A IQK!\n");

	/* enable path A PA in TXIQK mode */
	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x000000);
	odm_set_rf_reg(dm, RF_PATH_A, RF_WE_LUT, 0x80000, 0x1);
	odm_set_rf_reg(dm, RF_PATH_A, RF_RCK_OS, RFREGOFFSETMASK, 0x20000);
	odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G1, RFREGOFFSETMASK, 0x0000f);
	odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0x07ff7); /* 0x07f77 */
	/* PA,PAD gain adjust */
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, RFREGOFFSETMASK, 0x980);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x56, RFREGOFFSETMASK, 0x5102a); /* 0x5111e0 */

	/* enter IQK mode */
	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x808000);

	/* 1 Tx IQK */
	/* path-A IQK setting
	* 	RF_DBG(dm,DBG_RF_IQK, "path-A IQK setting!\n"); */
	odm_set_bb_reg(dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	odm_set_bb_reg(dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(dm, REG_TX_IQK_PI_A, MASKDWORD, 0x821403ff); /* 0x821403e0 */
	odm_set_bb_reg(dm, REG_RX_IQK_PI_A, MASKDWORD, 0x28160000);

	/* LO calibration setting
	* 	RF_DBG(dm,DBG_RF_IQK, "LO calibration setting!\n"); */
	odm_set_bb_reg(dm, REG_IQK_AGC_RSP, MASKDWORD, 0x00462911);

	/* One shot, path A LOK & IQK
	* 	RF_DBG(dm,DBG_RF_IQK, "One shot, path A LOK & IQK!\n"); */
	odm_set_bb_reg(dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/* delay x ms
	* 	RF_DBG(dm,DBG_RF_IQK, "delay %d ms for One shot, path A LOK & IQK.\n", IQK_DELAY_TIME_8188F);
	* platform_stall_execution(IQK_DELAY_TIME_8188F*1000); */
	ODM_delay_ms(IQK_DELAY_TIME_8188F);

	/* reload RF 0xdf */
	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x000000);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, RFREGOFFSETMASK, 0x180);

	/* save LOK result */
	dm->rf_calibrate_info.lok_result = odm_get_rf_reg(dm, RF_PATH_A, RF_0x8, RFREGOFFSETMASK);

	/* Check failed */
	reg_eac = odm_get_bb_reg(dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_e94 = odm_get_bb_reg(dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD);
	reg_e9c = odm_get_bb_reg(dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD);
	RF_DBG(dm, DBG_RF_IQK, "0xeac = 0x%x\n", reg_eac);
	RF_DBG(dm, DBG_RF_IQK, "0xe94 = 0x%x, 0xe9c = 0x%x\n", reg_e94,
	       reg_e9c);
	/* monitor image power before & after IQK */
	RF_DBG(dm, DBG_RF_IQK,
	       "0xe90(before IQK)= 0x%x, 0xe98(afer IQK) = 0x%x\n",
	       odm_get_bb_reg(dm, R_0xe90, MASKDWORD),
	       odm_get_bb_reg(dm, R_0xe98, MASKDWORD));

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;

	return result;
}

u8 /* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
	phy_path_a_rx_iqk_8188f(
		struct dm_struct *dm,
		boolean config_path_b)
{
	u32 reg_eac, reg_e94, reg_e9c, reg_ea4, u4tmp;
	u8 result = 0x00;
	RF_DBG(dm, DBG_RF_IQK, "path A Rx IQK!\n");

	/* 1 Get TXIMR setting */
	/* modify RXIQK mode table
	* 	RF_DBG(dm,DBG_RF_IQK, "path-A Rx IQK modify RXIQK mode table!\n"); */
	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x000000);
	odm_set_rf_reg(dm, RF_PATH_A, RF_WE_LUT, 0x80000, 0x1);
	odm_set_rf_reg(dm, RF_PATH_A, RF_RCK_OS, RFREGOFFSETMASK, 0x30000);
	odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G1, RFREGOFFSETMASK, 0x0000f);
	odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0xf1173); /* 0xf117b */

	/* PA,PAD gain adjust */
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, RFREGOFFSETMASK, 0x980);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x56, RFREGOFFSETMASK, 0x5102a); /* 0x510f0 */

	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x808000);

	/* IQK setting */
	odm_set_bb_reg(dm, REG_TX_IQK, MASKDWORD, 0x01007c00);
	odm_set_bb_reg(dm, REG_RX_IQK, MASKDWORD, 0x01004800);

	/* path-A IQK setting */
	odm_set_bb_reg(dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x10008c1c);
	odm_set_bb_reg(dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x30008c1c);
	odm_set_bb_reg(dm, REG_TX_IQK_PI_A, MASKDWORD, 0x82160fff); /* 0x821603e0 */
	odm_set_bb_reg(dm, REG_RX_IQK_PI_A, MASKDWORD, 0x28160000);

	/* LO calibration setting
	* 	RF_DBG(dm,DBG_RF_IQK, "LO calibration setting!\n"); */
	odm_set_bb_reg(dm, REG_IQK_AGC_RSP, MASKDWORD, 0x00462911);

	/* One shot, path A LOK & IQK
	* 	RF_DBG(dm,DBG_RF_IQK, "One shot, path A LOK & IQK!\n"); */
	odm_set_bb_reg(dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/* delay x ms
	* 	RF_DBG(dm,DBG_RF_IQK, "delay %d ms for One shot, path A LOK & IQK.\n", IQK_DELAY_TIME_8188F);
	* platform_stall_execution(IQK_DELAY_TIME_8188F*1000); */
	ODM_delay_ms(IQK_DELAY_TIME_8188F);

	/* reload RF 0xdf */
	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x000000);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, RFREGOFFSETMASK, 0x180);

	/* Check failed */
	reg_eac = odm_get_bb_reg(dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_e94 = odm_get_bb_reg(dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD);
	reg_e9c = odm_get_bb_reg(dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD);
	RF_DBG(dm, DBG_RF_IQK, "0xeac = 0x%x\n", reg_eac);
	RF_DBG(dm, DBG_RF_IQK, "0xe94 = 0x%x, 0xe9c = 0x%x\n", reg_e94,
	       reg_e9c);
	/* monitor image power before & after IQK */
	RF_DBG(dm, DBG_RF_IQK,
	       "0xe90(before IQK)= 0x%x, 0xe98(afer IQK) = 0x%x\n",
	       odm_get_bb_reg(dm, R_0xe90, MASKDWORD),
	       odm_get_bb_reg(dm, R_0xe98, MASKDWORD));

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else /* if Tx not OK, ignore Rx */
		return result;

	u4tmp = 0x80007C00 | (reg_e94 & 0x3FF0000) | ((reg_e9c & 0x3FF0000) >> 16);
	odm_set_bb_reg(dm, REG_TX_IQK, MASKDWORD, u4tmp);
	RF_DBG(dm, DBG_RF_IQK, "0xe40 = 0x%x u4tmp = 0x%x\n",
	       odm_get_bb_reg(dm, REG_TX_IQK, MASKDWORD), u4tmp);

	/* 1 RX IQK */
	/* modify RXIQK mode table
	* 	RF_DBG(dm,DBG_RF_IQK, "path-A Rx IQK modify RXIQK mode table 2!\n"); */
	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x000000);
	odm_set_rf_reg(dm, RF_PATH_A, RF_WE_LUT, 0x80000, 0x1); /* 0xEF[19]   = 0x1 */
	odm_set_rf_reg(dm, RF_PATH_A, RF_RCK_OS, RFREGOFFSETMASK, 0x30000); /* 0x30[19:0] = 0x18000 */
	odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G1, RFREGOFFSETMASK, 0x0000f); /* 0x31[19:0] = 0x0000f */
	odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0xf7ff2); /* 0x32[19:0] = 0xf7ffa */

	/* PA,PAD gain adjust */
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, RFREGOFFSETMASK, 0x980);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x56, RFREGOFFSETMASK, 0x51000); /* 0x51000 */

	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x808000);

	/* IQK setting */
	odm_set_bb_reg(dm, REG_RX_IQK, MASKDWORD, 0x01004800);

	/* path-A IQK setting */
	odm_set_bb_reg(dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x30008c1c);
	odm_set_bb_reg(dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x10008c1c);
	odm_set_bb_reg(dm, REG_TX_IQK_PI_A, MASKDWORD, 0x82160000);
	odm_set_bb_reg(dm, REG_RX_IQK_PI_A, MASKDWORD, 0x281613ff); /* 0x281603e0 */

	/* LO calibration setting
	* 	RF_DBG(dm,DBG_RF_IQK, "LO calibration setting!\n"); */
	odm_set_bb_reg(dm, REG_IQK_AGC_RSP, MASKDWORD, 0x0046a911);

	/* One shot, path A LOK & IQK
	* 	RF_DBG(dm,DBG_RF_IQK, "One shot, path A LOK & IQK!\n"); */
	odm_set_bb_reg(dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/* delay x ms
	* 	RF_DBG(dm,DBG_RF_IQK, "delay %d ms for One shot, path A LOK & IQK.\n", IQK_DELAY_TIME_8188F);
	* platform_stall_execution(IQK_DELAY_TIME_8188F*1000); */
	ODM_delay_ms(IQK_DELAY_TIME_8188F);

	/* reload RF 0xdf */
	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x000000);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, RFREGOFFSETMASK, 0x180);

	/* reload LOK value */
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x8, RFREGOFFSETMASK, dm->rf_calibrate_info.lok_result);

	/* Check failed */
	reg_eac = odm_get_bb_reg(dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_ea4 = odm_get_bb_reg(dm, REG_RX_POWER_BEFORE_IQK_A_2, MASKDWORD);
	RF_DBG(dm, DBG_RF_IQK, "0xeac = 0x%x\n", reg_eac);
	RF_DBG(dm, DBG_RF_IQK, "0xea4 = 0x%x, 0xeac = 0x%x\n", reg_ea4,
	       reg_eac);
	/* monitor image power before & after IQK */
	RF_DBG(dm, DBG_RF_IQK,
	       "0xea0(before IQK)= 0x%x, 0xea8(afer IQK) = 0x%x\n",
	       odm_get_bb_reg(dm, R_0xea0, MASKDWORD),
	       odm_get_bb_reg(dm, R_0xea8, MASKDWORD));

	if (!(reg_eac & BIT(27)) && /* if Tx is OK, check whether Rx is OK */
	    (((reg_ea4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_eac & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;
	else
		RF_DBG(dm, DBG_RF_IQK, "path A Rx IQK fail!!\n");

	return result;
}

#if 0
u8              /* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_path_b_iqk_8188f(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct dm_struct *dm
#else
	void *adapter
#endif
)
{
	u32 reg_eac, reg_e94, reg_e9c/*, reg_ec4, reg_ecc*/;
	u8 result = 0x00;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct dm_struct *dm = &hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct dm_struct *dm = &hal_data->DM_OutSrc;
#endif
#endif
	RF_DBG(dm, DBG_RF_IQK, "path B IQK!\n");

	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x000000);
	/* switch to path B */
	odm_set_bb_reg(dm, R_0x948, MASKDWORD, 0x00000080);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xb0, RFREGOFFSETMASK, 0xefff0);
	/* in TXIQK mode
	*	odm_set_rf_reg(dm, RF_PATH_A, RF_WE_LUT, RFREGOFFSETMASK, 0x800a0 );
	*	odm_set_rf_reg(dm, RF_PATH_A, RF_RCK_OS, RFREGOFFSETMASK, 0x20000 );
	*	odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G1, RFREGOFFSETMASK, 0x0003f );
	*	odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0xc7f87 );
	* enable path B PA in TXIQK mode
	*	odm_set_rf_reg(dm, RF_PATH_A, RF_0xed, RFREGOFFSETMASK, 0x00020 );
	*	odm_set_rf_reg(dm, RF_PATH_A, RF_0x43, RFREGOFFSETMASK, 0x40fc1 ); */


	/* 1 Tx IQK */
	/* path-A IQK setting
	* 	RF_DBG(dm,DBG_RF_IQK, "path-B IQK setting!\n"); */
	odm_set_bb_reg(dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	odm_set_bb_reg(dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(dm, REG_TX_IQK_PI_A, MASKDWORD, 0x82140102);
	odm_set_bb_reg(dm, REG_RX_IQK_PI_A, MASKDWORD, 0x28160000);

	/* LO calibration setting
	* 	RF_DBG(dm,DBG_RF_IQK, "LO calibration setting!\n"); */
	odm_set_bb_reg(dm, REG_IQK_AGC_RSP, MASKDWORD, 0x00462911);


	/* enter IQK mode */
	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x808000);

	/* One shot, path B LOK & IQK
	* 	RF_DBG(dm,DBG_RF_IQK, "One shot, path B LOK & IQK!\n"); */
	odm_set_bb_reg(dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/* delay x ms
	* 	RF_DBG(dm,DBG_RF_IQK, "delay %d ms for One shot, path B LOK & IQK.\n", IQK_DELAY_TIME_8188F);
	* platform_stall_execution(IQK_DELAY_TIME_8188F*1000); */
	ODM_delay_ms(IQK_DELAY_TIME_8188F);

	RF_DBG(dm, DBG_RF_IQK, "0x948 = 0x%x\n",
	       odm_get_bb_reg(dm, R_0x948, MASKDWORD));


	/* Check failed */
	reg_eac = odm_get_bb_reg(dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_e94 = odm_get_bb_reg(dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD);
	reg_e9c = odm_get_bb_reg(dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD);
	RF_DBG(dm, DBG_RF_IQK, "0xeac = 0x%x\n", reg_eac);
	RF_DBG(dm, DBG_RF_IQK, "0xe94 = 0x%x, 0xe9c = 0x%x\n", reg_e94,
	       reg_e9c);
	/* monitor image power before & after IQK */
	RF_DBG(dm, DBG_RF_IQK,
	       "0xe90(before IQK)= 0x%x, 0xe98(afer IQK) = 0x%x\n",
	       odm_get_bb_reg(dm, R_0xe90, MASKDWORD),
	       odm_get_bb_reg(dm, R_0xe98, MASKDWORD));


	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else
		return result;
#if 0
	if (!(reg_eac & BIT(30)) &&
	    (((reg_ec4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_ecc & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;
	else
		RF_DBG(dm, DBG_RF_IQK, "path B Rx IQK fail!!\n");

#endif
	return result;
}



u8          /* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_path_b_rx_iqk_8188f(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct dm_struct *dm,
#else
	void *adapter,
#endif
	boolean config_path_b
)
{
	u32 reg_eac, reg_eb4, reg_ebc, reg_ecc, reg_ec4, u4tmp;
	u8 result = 0x00;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct dm_struct *dm = &hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct dm_struct *dm = &hal_data->DM_OutSrc;
#endif
#endif
	RF_DBG(dm, DBG_RF_IQK, "path B Rx IQK!\n");

	/* 1 Get TXIMR setting */
	RF_DBG(dm, DBG_RF_IQK, "Get RXIQK TXIMR!\n");
	/* modify RXIQK mode table
	* 	RF_DBG(dm,DBG_RF_IQK, "path-A Rx IQK modify RXIQK mode table!\n");
	*	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x000000);
	*	odm_set_rf_reg(dm, RF_PATH_A, RF_WE_LUT, RFREGOFFSETMASK, 0x800a0 );
	*	odm_set_rf_reg(dm, RF_PATH_A, RF_RCK_OS, RFREGOFFSETMASK, 0x30000 );
	*	odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G1, RFREGOFFSETMASK, 0x0000f );
	*	odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0xf117B );
	*	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x808000); */

	/* IQK setting */
	odm_set_bb_reg(dm, REG_TX_IQK, MASKDWORD, 0x01007c00);
	odm_set_bb_reg(dm, REG_RX_IQK, MASKDWORD, 0x81004800);

	/* path-B IQK setting */
	odm_set_bb_reg(dm, REG_TX_IQK_TONE_B, MASKDWORD, 0x10008c1c);
	odm_set_bb_reg(dm, REG_RX_IQK_TONE_B, MASKDWORD, 0x30008c1c);
	odm_set_bb_reg(dm, REG_TX_IQK_PI_B, MASKDWORD, 0x82130804);
	odm_set_bb_reg(dm, REG_RX_IQK_PI_B, MASKDWORD, 0x68130000);

	/* LO calibration setting */
	RF_DBG(dm, DBG_RF_IQK, "LO calibration setting!\n");
	odm_set_bb_reg(dm, REG_IQK_AGC_RSP, MASKDWORD, 0x0046a911);

	/* One shot, path B LOK & IQK */
	RF_DBG(dm, DBG_RF_IQK, "One shot, path B LOK & IQK!\n");
	odm_set_bb_reg(dm, REG_IQK_AGC_CONT, MASKDWORD, 0x00000002);
	odm_set_bb_reg(dm, REG_IQK_AGC_CONT, MASKDWORD, 0x00000000);

	/* delay x ms */
	RF_DBG(dm, DBG_RF_IQK, "delay %d ms for One shot, path A LOK & IQK.\n",
	       IQK_DELAY_TIME_8188F);
	/* platform_stall_execution(IQK_DELAY_TIME_8188F*1000); */
	ODM_delay_ms(IQK_DELAY_TIME_8188F);


	/* Check failed */
	reg_eac = odm_get_bb_reg(dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_eb4 = odm_get_bb_reg(dm, REG_TX_POWER_BEFORE_IQK_B, MASKDWORD);
	reg_ebc = odm_get_bb_reg(dm, REG_TX_POWER_AFTER_IQK_B, MASKDWORD);
	RF_DBG(dm, DBG_RF_IQK, "0xeac = 0x%x\n", reg_eac);
	RF_DBG(dm, DBG_RF_IQK, "0xeb4 = 0x%x, 0xebc = 0x%x\n", reg_eb4,
	       reg_ebc);
	/* monitor image power before & after IQK */
	RF_DBG(dm, DBG_RF_IQK,
	       "0xeb0(before IQK)= 0x%x, 0xeb8(afer IQK) = 0x%x\n",
	       odm_get_bb_reg(dm, R_0xeb0, MASKDWORD),
	       odm_get_bb_reg(dm, R_0xeb8, MASKDWORD));


	if (!(reg_eac & BIT(31)) &&
	    (((reg_eb4 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_ebc & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else                            /* if Tx not OK, ignore Rx */
		return result;

	u4tmp = 0x80007C00 | (reg_eb4 & 0x3FF0000) | ((reg_ebc & 0x3FF0000) >> 16);
	odm_set_bb_reg(dm, REG_TX_IQK, MASKDWORD, u4tmp);
	RF_DBG(dm, DBG_RF_IQK, "0xe40 = 0x%x u4tmp = 0x%x\n",
	       odm_get_bb_reg(dm, REG_TX_IQK, MASKDWORD), u4tmp);


	/* 1 RX IQK */
	/* modify RXIQK mode table
	* 	RF_DBG(dm,DBG_RF_IQK, "path-A Rx IQK modify RXIQK mode table 2!\n");
	*	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x000000);
	*	odm_set_rf_reg(dm, RF_PATH_A, RF_WE_LUT, RFREGOFFSETMASK, 0x800a0 ); */

	/* <20121009, Kordan> RF mode = 3
	* odm_set_rf_reg(dm, RF_PATH_A, RF_WE_LUT, 0x80000, 0x1);	                */ /* 0xEF[19]   = 0x1
* odm_set_rf_reg(dm, RF_PATH_A, RF_RCK_OS, RFREGOFFSETMASK, 0x18000 );   */ /* 0x30[19:0] = 0x18000
* odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G1, RFREGOFFSETMASK, 0x0000f );  */ /* 0x31[19:0] = 0x0000f
* odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0xf7ffa );  */ /* 0x32[19:0] = 0xf7ffa
* odm_set_rf_reg(dm, RF_PATH_A, RF_WE_LUT, 0x80000, 0x0);	                */ /* 0xEF[19]   = 0x0
*	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x808000); */

	/* IQK setting */
	odm_set_bb_reg(dm, REG_RX_IQK, MASKDWORD, 0x01004800);

	/* path-B IQK setting */
	odm_set_bb_reg(dm, REG_TX_IQK_TONE_B, MASKDWORD, 0x30008c1c);
	odm_set_bb_reg(dm, REG_RX_IQK_TONE_B, MASKDWORD, 0x10008c1c);
	odm_set_bb_reg(dm, REG_TX_IQK_PI_B, MASKDWORD, 0x82130c05);
	odm_set_bb_reg(dm, REG_RX_IQK_PI_B, MASKDWORD, 0x68130c05);

	/* LO calibration setting */
	RF_DBG(dm, DBG_RF_IQK, "LO calibration setting!\n");
	odm_set_bb_reg(dm, REG_IQK_AGC_RSP, MASKDWORD, 0x0046a911);

	/* One shot, path B LOK & IQK */
	RF_DBG(dm, DBG_RF_IQK, "One shot, path B LOK & IQK!\n");
	odm_set_bb_reg(dm, REG_IQK_AGC_CONT, MASKDWORD, 0x00000002);
	odm_set_bb_reg(dm, REG_IQK_AGC_CONT, MASKDWORD, 0x00000000);

	/* delay x ms */
	RF_DBG(dm, DBG_RF_IQK, "delay %d ms for One shot, path A LOK & IQK.\n",
	       IQK_DELAY_TIME_8188F);
	/* platform_stall_execution(IQK_DELAY_TIME_8188F*1000); */
	ODM_delay_ms(IQK_DELAY_TIME_8188F);

	/* Check failed */
	reg_eac = odm_get_bb_reg(dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_ec4 = odm_get_bb_reg(dm, REG_RX_POWER_BEFORE_IQK_B_2, MASKDWORD);;
	reg_ecc = odm_get_bb_reg(dm, REG_RX_POWER_AFTER_IQK_B_2, MASKDWORD);
	RF_DBG(dm, DBG_RF_IQK, "0xeac = 0x%x\n", reg_eac);
	RF_DBG(dm, DBG_RF_IQK, "0xec4 = 0x%x, 0xecc = 0x%x\n", reg_ec4,
	       reg_ecc);
	/* monitor image power before & after IQK */
	RF_DBG(dm, DBG_RF_IQK,
	       "0xec0(before IQK)= 0x%x, 0xec8(afer IQK) = 0x%x\n",
	       odm_get_bb_reg(dm, R_0xec0, MASKDWORD),
	       odm_get_bb_reg(dm, R_0xec8, MASKDWORD));

	/*	PA/PAD controlled by 0x0 */
	/* leave IQK mode
	*	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x000000);
	*	odm_set_rf_reg(dm, RF_PATH_B, RF_0xdf, RFREGOFFSETMASK, 0x180 ); */

#if 0
	if (!(reg_eac & BIT(31)) &&
	    (((reg_eb4 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_ebc & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else                            /* if Tx not OK, ignore Rx */
		return result;
#endif

	if (!(reg_eac & BIT(30)) &&     /* if Tx is OK, check whether Rx is OK */
	    (((reg_ec4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_ecc & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;
	else
		RF_DBG(dm, DBG_RF_IQK, "path B Rx IQK fail!!\n");

	return result;
}
#endif

void _phy_path_a_fill_iqk_matrix8188f(struct dm_struct *dm, boolean is_iqk_ok,
				      s32 result[][8], u8 final_candidate,
				      boolean is_tx_only)
{
	u32 oldval_0, X, TX0_A, reg;
	s32 Y, TX0_C;
	struct dm_rf_calibration_struct *cali_info = &(dm->rf_calibrate_info);

	RF_DBG(dm, DBG_RF_IQK, "path A IQ Calibration %s !\n",
	       (is_iqk_ok) ? "Success" : "Failed");

	if (final_candidate == 0xFF)
		return;

	else if (is_iqk_ok) {
		oldval_0 = (odm_get_bb_reg(dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD) >> 22) & 0x3FF;

		X = result[final_candidate][0];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;
		TX0_A = (X * oldval_0) >> 8;
		RF_DBG(dm, DBG_RF_IQK,
		       "X = 0x%x, TX0_A = 0x%x, oldval_0 0x%x\n", X, TX0_A,
		       oldval_0);
		odm_set_bb_reg(dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, 0x3FF, TX0_A);

		odm_set_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, BIT(31), ((X * oldval_0 >> 7) & 0x1));

		Y = result[final_candidate][1];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;

		/* 2 Tx IQC */
		TX0_C = (Y * oldval_0) >> 8;
		RF_DBG(dm, DBG_RF_IQK, "Y = 0x%x, TX = 0x%x\n", Y, TX0_C);
		odm_set_bb_reg(dm, REG_OFDM_0_XC_TX_AFE, 0xF0000000, ((TX0_C & 0x3C0) >> 6));
		cali_info->tx_iqc_8723b[PATH_S1][idx_0xc94][KEY] = REG_OFDM_0_XC_TX_AFE;
		cali_info->tx_iqc_8723b[PATH_S1][idx_0xc94][VAL] = odm_get_bb_reg(dm, REG_OFDM_0_XC_TX_AFE, MASKDWORD);

		odm_set_bb_reg(dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, 0x003F0000, (TX0_C & 0x3F));
		cali_info->tx_iqc_8723b[PATH_S1][idx_0xc80][KEY] = REG_OFDM_0_XA_TX_IQ_IMBALANCE;
		cali_info->tx_iqc_8723b[PATH_S1][idx_0xc80][VAL] = odm_get_bb_reg(dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD);

		odm_set_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, BIT(29), ((Y * oldval_0 >> 7) & 0x1));
		cali_info->tx_iqc_8723b[PATH_S1][idx_0xc4c][KEY] = REG_OFDM_0_ECCA_THRESHOLD;
		cali_info->tx_iqc_8723b[PATH_S1][idx_0xc4c][VAL] = odm_get_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD);

		if (is_tx_only) {
			RF_DBG(dm, DBG_RF_IQK, "%s only Tx OK\n", __func__);

			/* <20130226, Kordan> Saving RxIQC, otherwise not initialized. */
			cali_info->rx_iqc_8723b[PATH_S1][idx_0xca0][KEY] = REG_OFDM_0_RX_IQ_EXT_ANTA;
			cali_info->rx_iqc_8723b[PATH_S1][idx_0xca0][VAL] = odm_get_bb_reg(dm, REG_OFDM_0_RX_IQ_EXT_ANTA, MASKDWORD);
			cali_info->rx_iqc_8723b[PATH_S1][idx_0xc14][KEY] = REG_OFDM_0_XA_RX_IQ_IMBALANCE;
			cali_info->rx_iqc_8723b[PATH_S1][idx_0xc14][VAL] = odm_get_bb_reg(dm, REG_OFDM_0_XA_RX_IQ_IMBALANCE, MASKDWORD);
			return;
		}

		reg = result[final_candidate][2];
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
		if (RTL_ABS(reg, 0x100) >= 16)
			reg = 0x100;
#endif

		/* 2 Rx IQC */
		odm_set_bb_reg(dm, REG_OFDM_0_XA_RX_IQ_IMBALANCE, 0x3FF, reg);
		reg = result[final_candidate][3] & 0x3F;
		odm_set_bb_reg(dm, REG_OFDM_0_XA_RX_IQ_IMBALANCE, 0xFC00, reg);
		cali_info->rx_iqc_8723b[PATH_S1][idx_0xc14][KEY] = REG_OFDM_0_XA_RX_IQ_IMBALANCE;
		cali_info->rx_iqc_8723b[PATH_S1][idx_0xc14][VAL] = odm_get_bb_reg(dm, REG_OFDM_0_XA_RX_IQ_IMBALANCE, MASKDWORD);

		reg = (result[final_candidate][3] >> 6) & 0xF;
		odm_set_bb_reg(dm, REG_OFDM_0_RX_IQ_EXT_ANTA, 0xF0000000, reg);
		cali_info->rx_iqc_8723b[PATH_S1][idx_0xca0][KEY] = REG_OFDM_0_RX_IQ_EXT_ANTA;
		cali_info->rx_iqc_8723b[PATH_S1][idx_0xca0][VAL] = odm_get_bb_reg(dm, REG_OFDM_0_RX_IQ_EXT_ANTA, MASKDWORD);
	}
}

#if 0
void
_phy_path_b_fill_iqk_matrix8188f(
	struct dm_struct *dm,
	boolean is_iqk_ok,
	s32 result[][8],
	u8 final_candidate,
	boolean is_tx_only         /* do Tx only */
)
{
	u32 oldval_1, X, TX1_A, reg;
	s32 Y, TX1_C;
	struct dm_rf_calibration_struct *cali_info = &(dm->rf_calibrate_info);

	RF_DBG(dm, DBG_RF_IQK, "path B IQ Calibration %s !\n",
	       (is_iqk_ok) ? "Success" : "Failed");

	if (final_candidate == 0xFF)
		return;

	else if (is_iqk_ok) {
		oldval_1 = (odm_get_bb_reg(dm, REG_OFDM_0_XB_TX_IQ_IMBALANCE, MASKDWORD) >> 22) & 0x3FF;

		X = result[final_candidate][4];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;
		TX1_A = (X * oldval_1) >> 8;
		RF_DBG(dm, DBG_RF_IQK, "X = 0x%x, TX1_A = 0x%x\n", X, TX1_A);

		odm_set_bb_reg(dm, REG_OFDM_0_XB_TX_IQ_IMBALANCE, 0x3FF, TX1_A);

		odm_set_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, BIT(27), ((X * oldval_1 >> 7) & 0x1));

		Y = result[final_candidate][5];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;

		TX1_C = (Y * oldval_1) >> 8;
		RF_DBG(dm, DBG_RF_IQK, "Y = 0x%x, TX1_C = 0x%x\n", Y, TX1_C);

		/* 2 Tx IQC */
		odm_set_bb_reg(dm, REG_OFDM_0_XD_TX_AFE, 0xF0000000, ((TX1_C & 0x3C0) >> 6));
		cali_info->tx_iqc_8723b[PATH_S0][idx_0xc9c][KEY] = REG_OFDM_0_XD_TX_AFE;
		cali_info->tx_iqc_8723b[PATH_S0][idx_0xc9c][VAL] = odm_get_bb_reg(dm, REG_OFDM_0_XD_TX_AFE, MASKDWORD);

		odm_set_bb_reg(dm, REG_OFDM_0_XB_TX_IQ_IMBALANCE, 0x003F0000, (TX1_C & 0x3F));
		cali_info->tx_iqc_8723b[PATH_S0][idx_0xc88][KEY] = REG_OFDM_0_XB_TX_IQ_IMBALANCE;
		cali_info->tx_iqc_8723b[PATH_S0][idx_0xc88][VAL] = odm_get_bb_reg(dm, REG_OFDM_0_XB_TX_IQ_IMBALANCE, MASKDWORD);

		odm_set_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, BIT(25), ((Y * oldval_1 >> 7) & 0x1));
		cali_info->tx_iqc_8723b[PATH_S0][idx_0xc4c][KEY] = REG_OFDM_0_ECCA_THRESHOLD;
		cali_info->tx_iqc_8723b[PATH_S0][idx_0xc4c][VAL] = odm_get_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD);

		if (is_tx_only) {
			RF_DBG(dm, DBG_RF_IQK, "%s only Tx OK\n", __func__);

			cali_info->rx_iqc_8723b[PATH_S0][idx_0xc1c][KEY] = REG_OFDM_0_XB_RX_IQ_IMBALANCE;
			cali_info->rx_iqc_8723b[PATH_S0][idx_0xc1c][VAL] = odm_get_bb_reg(dm, REG_OFDM_0_XB_RX_IQ_IMBALANCE, MASKDWORD);
			cali_info->rx_iqc_8723b[PATH_S0][idx_0xc78][KEY] = REG_OFDM_0_AGC_RSSI_TABLE;
			cali_info->rx_iqc_8723b[PATH_S0][idx_0xc78][VAL] = odm_get_bb_reg(dm, REG_OFDM_0_AGC_RSSI_TABLE, MASKDWORD);
			return;
		}

		/* 2 Rx IQC */
		reg = result[final_candidate][6];
		odm_set_bb_reg(dm, REG_OFDM_0_XB_RX_IQ_IMBALANCE, 0x3FF, reg);
		reg = result[final_candidate][7] & 0x3F;
		odm_set_bb_reg(dm, REG_OFDM_0_XB_RX_IQ_IMBALANCE, 0xFC00, reg);
		cali_info->rx_iqc_8723b[PATH_S0][idx_0xc1c][KEY] = REG_OFDM_0_XB_RX_IQ_IMBALANCE;
		cali_info->rx_iqc_8723b[PATH_S0][idx_0xc1c][VAL] = odm_get_bb_reg(dm, REG_OFDM_0_XB_RX_IQ_IMBALANCE, MASKDWORD);

		reg = (result[final_candidate][7] >> 6) & 0xF;
		odm_set_bb_reg(dm, REG_OFDM_0_AGC_RSSI_TABLE, 0x0000F000, reg);
		cali_info->rx_iqc_8723b[PATH_S0][idx_0xc78][KEY] = REG_OFDM_0_AGC_RSSI_TABLE;
		cali_info->rx_iqc_8723b[PATH_S0][idx_0xc78][VAL] = odm_get_bb_reg(dm, REG_OFDM_0_AGC_RSSI_TABLE, MASKDWORD);
	}
}
#endif

void _phy_save_adda_registers8188f(struct dm_struct *dm, u32 *adda_reg,
				   u32 *adda_backup, u32 register_num)
{
	u32 i;
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	if (odm_check_power_status(dm) == false)
		return;
#endif
	RF_DBG(dm, DBG_RF_IQK, "Save ADDA parameters.\n");
	for (i = 0; i < register_num; i++)
		adda_backup[i] = odm_get_bb_reg(dm, adda_reg[i], MASKDWORD);
}

void _phy_save_mac_registers8188f(struct dm_struct *dm, u32 *mac_reg,
				  u32 *mac_backup)
{
	u32 i;
	RF_DBG(dm, DBG_RF_IQK, "Save MAC parameters.\n");
	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		mac_backup[i] = odm_read_1byte(dm, mac_reg[i]);
	mac_backup[i] = odm_read_4byte(dm, mac_reg[i]);
}

void _phy_reload_adda_registers8188f(struct dm_struct *dm, u32 *adda_reg,
				     u32 *adda_backup, u32 regiester_num)
{
	u32 i;

	RF_DBG(dm, DBG_RF_IQK, "Reload ADDA power saving parameters !\n");
	for (i = 0; i < regiester_num; i++)
		odm_set_bb_reg(dm, adda_reg[i], MASKDWORD, adda_backup[i]);
}

void _phy_reload_mac_registers8188f(struct dm_struct *dm, u32 *mac_reg,
				    u32 *mac_backup)
{
	u32 i;
	RF_DBG(dm, DBG_RF_IQK, "Reload MAC parameters !\n");
	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		odm_write_1byte(dm, mac_reg[i], (u8)mac_backup[i]);
	odm_write_4byte(dm, mac_reg[i], mac_backup[i]);
}

void _phy_path_adda_on8188f(struct dm_struct *dm, u32 *adda_reg,
			    boolean is_path_a_on, boolean is2T)
{
	u32 path_on;
	u32 i;
	RF_DBG(dm, DBG_RF_IQK, "ADDA ON.\n");

	path_on = is_path_a_on ? 0x03c00014 : 0x03c00014;
	if (false == is2T) {
		path_on = 0x03c00014;
		odm_set_bb_reg(dm, adda_reg[0], MASKDWORD, 0x03c00014);
	} else
		odm_set_bb_reg(dm, adda_reg[0], MASKDWORD, path_on);

	for (i = 1; i < IQK_ADDA_REG_NUM; i++)
		odm_set_bb_reg(dm, adda_reg[i], MASKDWORD, path_on);
}

void _phy_mac_setting_calibration8188f(struct dm_struct *dm, u32 *mac_reg,
				       u32 *mac_backup)
{
	//u32 i = 0;
	RF_DBG(dm, DBG_RF_IQK, "MAC settings for Calibration.\n");

#if 0
	odm_write_1byte(dm, mac_reg[i], 0x3F);

	for (i = 1; i < (IQK_MAC_REG_NUM - 1); i++)
		odm_write_1byte(dm, mac_reg[i], (u8)(mac_backup[i] & (~BIT(3))));
	odm_write_1byte(dm, mac_reg[i], (u8)(mac_backup[i] & (~BIT(5))));
#else

	odm_set_bb_reg(dm, R_0x520, 0x00ff0000, 0xff);
#endif
}

void _phy_path_a_stand_by8188f(struct dm_struct *dm)
{
	RF_DBG(dm, DBG_RF_IQK, "path-A standby mode!\n");

	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x000000);
	/* Allen */
	odm_set_rf_reg(dm, RF_PATH_A, RF_AC, MASKDWORD, 0x10000);
	/* odm_set_bb_reg(dm, R_0x840, MASKDWORD, 0x00010000);
	*   */
	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x808000);
}

void _phy_pi_mode_switch8188f(struct dm_struct *dm, boolean pi_mode)
{
	u32 mode;
	RF_DBG(dm, DBG_RF_IQK, "BB Switch to %s mode!\n",
	       (pi_mode ? "PI" : "SI"));

	mode = pi_mode ? 0x01000100 : 0x01000000;
	odm_set_bb_reg(dm, REG_FPGA0_XA_HSSI_PARAMETER1, MASKDWORD, mode);
	odm_set_bb_reg(dm, REG_FPGA0_XB_HSSI_PARAMETER1, MASKDWORD, mode);
}

boolean
phy_simularity_compare_8188f(struct dm_struct *dm, s32 result[][8], u8 c1,
			     u8 c2)
{
	u32 i, j, diff, simularity_bit_map, bound = 0;
	u8 final_candidate[2] = {0xFF, 0xFF}; /* for path A and path B */
	boolean is_result = true;
	/* #if !(DM_ODM_SUPPORT_TYPE & ODM_AP) */
	/*	bool		is2T = IS_92C_SERIAL( hal_data->version_id);
	 * #else */
	boolean is2T = true;
	/* #endif */

	s32 tmp1 = 0, tmp2 = 0;

	if (is2T)
		bound = 8;
	else
		bound = 4;

	RF_DBG(dm, DBG_RF_IQK,
	       "===> IQK:phy_simularity_compare_8192e c1 %d c2 %d!!!\n", c1,
	       c2);

	simularity_bit_map = 0;

	for (i = 0; i < bound; i++) {
		if (i == 1 || i == 3 || i == 5 || i == 7) {
			if ((result[c1][i] & 0x00000200) != 0)
				tmp1 = result[c1][i] | 0xFFFFFC00;
			else
				tmp1 = result[c1][i];

			if ((result[c2][i] & 0x00000200) != 0)
				tmp2 = result[c2][i] | 0xFFFFFC00;
			else
				tmp2 = result[c2][i];
		} else {
			tmp1 = result[c1][i];
			tmp2 = result[c2][i];
		}

		diff = (tmp1 > tmp2) ? (tmp1 - tmp2) : (tmp2 - tmp1);

		if (diff > MAX_TOLERANCE) {
			RF_DBG(dm, DBG_RF_IQK,
			       "IQK:differnece overflow %d index %d compare1 0x%x compare2 0x%x!!!\n",
			       diff, i, result[c1][i], result[c2][i]);

			if ((i == 2 || i == 6) && !simularity_bit_map) {
				if (result[c1][i] + result[c1][i + 1] == 0)
					final_candidate[(i / 4)] = c2;
				else if (result[c2][i] + result[c2][i + 1] == 0)
					final_candidate[(i / 4)] = c1;
				else
					simularity_bit_map = simularity_bit_map | (1 << i);
			} else
				simularity_bit_map = simularity_bit_map | (1 << i);
		}
	}

	RF_DBG(dm, DBG_RF_IQK,
	       "IQK:phy_simularity_compare_8192e simularity_bit_map   %x !!!\n",
	       simularity_bit_map);

	if (simularity_bit_map == 0) {
		for (i = 0; i < (bound / 4); i++) {
			if (final_candidate[i] != 0xFF) {
				for (j = i * 4; j < (i + 1) * 4 - 2; j++)
					result[3][j] = result[final_candidate[i]][j];
				is_result = false;
			}
		}
		return is_result;
	}

	if (!(simularity_bit_map & 0x03)) { /* path A TX OK */
		for (i = 0; i < 2; i++)
			result[3][i] = result[c1][i];
	}

	if (!(simularity_bit_map & 0x0c)) { /* path A RX OK */
		for (i = 2; i < 4; i++)
			result[3][i] = result[c1][i];
	}

	if (!(simularity_bit_map & 0x30)) { /* path B TX OK */
		for (i = 4; i < 6; i++)
			result[3][i] = result[c1][i];
	}

	if (!(simularity_bit_map & 0xc0)) { /* path B RX OK */
		for (i = 6; i < 8; i++)
			result[3][i] = result[c1][i];
	}
	return false;
}

void _phy_iq_calibrate_8188f(struct dm_struct *dm, s32 result[][8], u8 t,
			     boolean is2T)
{
	u32 i;
	u8 path_aok = 0x0; //, path_bok = 0x0;
	u8 tmp0xc50 = (u8)odm_get_bb_reg(dm, R_0xc50, MASKBYTE0);
	u8 tmp0xc58 = (u8)odm_get_bb_reg(dm, R_0xc58, MASKBYTE0);
	u32 ADDA_REG[IQK_ADDA_REG_NUM] = {
		REG_FPGA0_XCD_SWITCH_CONTROL, REG_BLUE_TOOTH,
		REG_RX_WAIT_CCA, REG_TX_CCK_RFON,
		REG_TX_CCK_BBON, REG_TX_OFDM_RFON,
		REG_TX_OFDM_BBON, REG_TX_TO_RX,
		REG_TX_TO_TX, REG_RX_CCK,
		REG_RX_OFDM, REG_RX_WAIT_RIFS,
		REG_RX_TO_RX, REG_STANDBY,
		REG_SLEEP, REG_PMPD_ANAEN};
	u32 IQK_MAC_REG[IQK_MAC_REG_NUM] = {
		REG_TXPAUSE, REG_BCN_CTRL,
		REG_BCN_CTRL_1, REG_GPIO_MUXCFG};

	/* since 92C & 92D have the different define in IQK_BB_REG */
	u32 IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
		REG_OFDM_0_TRX_PATH_ENABLE, REG_OFDM_0_TR_MUX_PAR,
		REG_FPGA0_XCD_RF_INTERFACE_SW, REG_CONFIG_ANT_A, REG_CONFIG_ANT_B,
		REG_FPGA0_XAB_RF_INTERFACE_SW, REG_FPGA0_XA_RF_INTERFACE_OE,
		REG_FPGA0_XB_RF_INTERFACE_OE, REG_FPGA0_RFMOD};

	//u32 path_sel_bb;
	u32 path_sel_rf;
	u32 retry_count;

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	retry_count = 2;
#elif (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
#if MP_DRIVER
	retry_count = 9;
#else
	retry_count = 2;
#endif
#elif (DM_ODM_SUPPORT_TYPE & (ODM_CE))
	if (*(dm->mp_mode))
		retry_count = 9;
	else
		retry_count = 2;
#endif

/* Note: IQ calibration must be performed after loading */
/*		PHY_REG.txt , and radio_a, radio_b.txt */

/* u32 bbvalue; */

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
#ifdef MP_TEST
	if (*(dm->mp_mode))
		retry_count = 9;
#endif
#endif

	if (t == 0) {
		/*	 	 bbvalue = odm_get_bb_reg(dm, REG_FPGA0_RFMOD, MASKDWORD);
		 * 			RT_DISP(FINIT, INIT_IQK, ("_phy_iq_calibrate_8188f()==>0x%08x\n",bbvalue)); */

		RF_DBG(dm, DBG_RF_IQK, "IQ Calibration for %s for %d times\n",
		       (is2T ? "2T2R" : "1T1R"), t);

		/* Save ADDA parameters, turn path A ADDA on */
		_phy_save_adda_registers8188f(dm, ADDA_REG, dm->rf_calibrate_info.ADDA_backup, IQK_ADDA_REG_NUM);
		_phy_save_mac_registers8188f(dm, IQK_MAC_REG, dm->rf_calibrate_info.IQK_MAC_backup);
		_phy_save_adda_registers8188f(dm, IQK_BB_REG_92C, dm->rf_calibrate_info.IQK_BB_backup, IQK_BB_REG_NUM);
	}
	RF_DBG(dm, DBG_RF_IQK, "IQ Calibration for %s for %d times\n",
	       (is2T ? "2T2R" : "1T1R"), t);
	_phy_path_adda_on8188f(dm, ADDA_REG, true, is2T);

	if (t == 0)
		dm->rf_calibrate_info.is_rf_pi_enable = (u8)odm_get_bb_reg(dm, REG_FPGA0_XA_HSSI_PARAMETER1, BIT(8));

#if 0
	if (!dm->rf_calibrate_info.is_rf_pi_enable) {
		/* Switch BB to PI mode to do IQ Calibration. */
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_phy_pi_mode_switch8188f(adapter, true);
#else
		_phy_pi_mode_switch8188f(dm, true);
#endif
	}
#endif

	/* save RF path */
	//	path_sel_bb = odm_get_bb_reg(dm, R_0x948, MASKDWORD);
	//	path_sel_rf = odm_get_rf_reg(dm, RF_PATH_A, RF_0xb0, 0xfffff);

	/* BB setting */
	/*odm_set_bb_reg(dm, REG_FPGA0_RFMOD, BIT24, 0x00);*/
	odm_set_bb_reg(dm, REG_OFDM_0_TRX_PATH_ENABLE, MASKDWORD, 0x03a05600);
	odm_set_bb_reg(dm, REG_OFDM_0_TR_MUX_PAR, MASKDWORD, 0x000800e4);
	odm_set_bb_reg(dm, REG_FPGA0_XCD_RF_INTERFACE_SW, MASKDWORD, 0x25204000);

	/* external switch control
	*	odm_set_bb_reg(dm, REG_FPGA0_XAB_RF_INTERFACE_SW, BIT(10), 0x01);
	*	odm_set_bb_reg(dm, REG_FPGA0_XAB_RF_INTERFACE_SW, BIT(26), 0x01);
	*	odm_set_bb_reg(dm, REG_FPGA0_XA_RF_INTERFACE_OE, BIT(10), 0x00);
	*	odm_set_bb_reg(dm, REG_FPGA0_XB_RF_INTERFACE_OE, BIT(10), 0x00); */

	if (is2T) {
		/* Allen */
		/*	odm_set_bb_reg(dm, REG_FPGA0_XA_LSSI_PARAMETER, MASKDWORD, 0x00010000); */
		/*	odm_set_bb_reg(dm, REG_FPGA0_XB_LSSI_PARAMETER, MASKDWORD, 0x00010000); */
		odm_set_rf_reg(dm, RF_PATH_B, RF_AC, MASKDWORD, 0x10000);
	}

	/* MAC settings */
	_phy_mac_setting_calibration8188f(dm, IQK_MAC_REG, dm->rf_calibrate_info.IQK_MAC_backup);

	/* Page B init */
	/* AP or IQK
	*	odm_set_bb_reg(dm, REG_CONFIG_ANT_A, MASKDWORD, 0x0f600000); */

	if (is2T) {
		/*		odm_set_bb_reg(dm, REG_CONFIG_ANT_B, MASKDWORD, 0x0f600000); */
	}

	/* IQ calibration setting */
	RF_DBG(dm, DBG_RF_IQK, "IQK setting!\n");
	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x808000);
	odm_set_bb_reg(dm, REG_TX_IQK, MASKDWORD, 0x01007c00);
	odm_set_bb_reg(dm, REG_RX_IQK, MASKDWORD, 0x01004800);

	for (i = 0; i < retry_count; i++) {
		path_aok = phy_path_a_iqk_8188f(dm, is2T);
		/*		if(path_aok == 0x03){ */
		if (path_aok == 0x01) { /* path A Tx IQK Success */
			odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x000000);
			dm->rf_calibrate_info.tx_lok[RF_PATH_A] = odm_get_rf_reg(dm, RF_PATH_A, RF_0x8, RFREGOFFSETMASK);

			RF_DBG(dm, DBG_RF_IQK, "path A Tx IQK Success!!\n");
			result[t][0] = (odm_get_bb_reg(dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
			result[t][1] = (odm_get_bb_reg(dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
			break;
		}
#if 0
		else if (i == (retry_count - 1) && path_aok == 0x01) { /* Tx IQK OK */
			RT_DISP(FINIT, INIT_IQK, ("path A IQK Only  Tx Success!!\n"));

			result[t][0] = (odm_get_bb_reg(dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
			result[t][1] = (odm_get_bb_reg(dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
		}
#endif
	}

/* bypass RXQIK */
#if 1

	for (i = 0; i < retry_count; i++) {
		path_aok = phy_path_a_rx_iqk_8188f(dm, is2T);
		if (path_aok == 0x03) {
			RF_DBG(dm, DBG_RF_IQK, "path A Rx IQK Success!!\n");
			/*				result[t][0] = (odm_get_bb_reg(dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD)&0x3FF0000)>>16;
			 *				result[t][1] = (odm_get_bb_reg(dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD)&0x3FF0000)>>16; */
			result[t][2] = (odm_get_bb_reg(dm, REG_RX_POWER_BEFORE_IQK_A_2, MASKDWORD) & 0x3FF0000) >> 16;
			result[t][3] = (odm_get_bb_reg(dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD) & 0x3FF0000) >> 16;
			break;
		}

		RF_DBG(dm, DBG_RF_IQK, "path A Rx IQK Fail!!\n");
	}
#endif

	if (path_aok == 0x0)
		RF_DBG(dm, DBG_RF_IQK, "path A IQK failed!!\n");
#if 0
	if (is2T) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_phy_path_a_stand_by8188f(adapter);

		/* Turn path B ADDA on */
		_phy_path_adda_on8188f(adapter, ADDA_REG, false, is2T);
#else
		_phy_path_a_stand_by8188f(dm);

		/* Turn path B ADDA on */
		_phy_path_adda_on8188f(dm, ADDA_REG, false, is2T);
#endif
		/* Allen */
		for (i = 0; i < retry_count; i++) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			path_bok = phy_path_b_iqk_8188f(adapter);
#else
			path_bok = phy_path_b_iqk_8188f(dm);
#endif
			/*		if(path_bok == 0x03){ */
			if (path_bok == 0x01) { /* path B Tx IQK Success */
				odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x000000);
				dm->rf_calibrate_info.tx_lok[RF_PATH_B] = odm_get_rf_reg(dm, RF_PATH_B, RF_0x8, RFREGOFFSETMASK);

				RF_DBG(dm, DBG_RF_IQK,
				       "path B Tx IQK Success!!\n");
				result[t][4] = (odm_get_bb_reg(dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
				result[t][5] = (odm_get_bb_reg(dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
				break;
			}
#if 0
			else if (i == (retry_count - 1) && path_aok == 0x01) { /* Tx IQK OK */
				RT_DISP(FINIT, INIT_IQK, ("path B IQK Only  Tx Success!!\n"));

				result[t][0] = (odm_get_bb_reg(dm, REG_TX_POWER_BEFORE_IQK_B, MASKDWORD) & 0x3FF0000) >> 16;
				result[t][1] = (odm_get_bb_reg(dm, REG_TX_POWER_AFTER_IQK_B, MASKDWORD) & 0x3FF0000) >> 16;
			}
#endif
		}

		/* bypass RXQIK */
#if 0

		for (i = 0; i < retry_count; i++) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			path_bok = phy_path_b_rx_iqk_8188f(adapter, is2T);
#else
			path_bok = phy_path_b_rx_iqk_8188f(dm, is2T);
#endif
			if (path_bok == 0x03) {
				RF_DBG(dm, DBG_RF_IQK,
				       "path B Rx IQK Success!!\n");
				/*				result[t][0] = (odm_get_bb_reg(dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD)&0x3FF0000)>>16;
				 *				result[t][1] = (odm_get_bb_reg(dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD)&0x3FF0000)>>16; */
				result[t][6] = (odm_get_bb_reg(dm, REG_RX_POWER_BEFORE_IQK_B_2, MASKDWORD) & 0x3FF0000) >> 16;
				result[t][7] = (odm_get_bb_reg(dm, REG_RX_POWER_AFTER_IQK_B_2, MASKDWORD) & 0x3FF0000) >> 16;
				break;
			}

			RF_DBG(dm, DBG_RF_IQK, "path B Rx IQK Fail!!\n");
		}
#endif

		/* ======Allen end ========= */
		if (0x00 == path_bok)
			RF_DBG(dm, DBG_RF_IQK, "path B IQK failed!!\n");
	}
#endif
	/* Back to BB mode, load original value */
	RF_DBG(dm, DBG_RF_IQK, "IQK:Back to BB mode, load original value!\n");
	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0);

	if (t != 0) {
		if (!dm->rf_calibrate_info.is_rf_pi_enable) {
			/* Switch back BB to SI mode after finish IQ Calibration. */
			_phy_pi_mode_switch8188f(dm, false);
		}
		/* Reload ADDA power saving parameters */
		_phy_reload_adda_registers8188f(dm, ADDA_REG, dm->rf_calibrate_info.ADDA_backup, IQK_ADDA_REG_NUM);
		/* Reload MAC parameters */
		_phy_reload_mac_registers8188f(dm, IQK_MAC_REG, dm->rf_calibrate_info.IQK_MAC_backup);
		_phy_reload_adda_registers8188f(dm, IQK_BB_REG_92C, dm->rf_calibrate_info.IQK_BB_backup, IQK_BB_REG_NUM);
		/* Reload RF path */
		//		odm_set_bb_reg(dm, R_0x948, MASKDWORD, path_sel_bb);
		//		odm_set_rf_reg(dm, RF_PATH_A, RF_0xb0, 0xfffff, path_sel_rf);

		/* Allen initial gain 0xc50 */
		/* Restore RX initial gain */
		odm_set_bb_reg(dm, R_0xc50, MASKBYTE0, 0x50);
		odm_set_bb_reg(dm, R_0xc50, MASKBYTE0, tmp0xc50);
		if (is2T) {
			odm_set_bb_reg(dm, R_0xc58, MASKBYTE0, 0x50);
			odm_set_bb_reg(dm, R_0xc58, MASKBYTE0, tmp0xc58);
		}

		/* load 0xe30 IQC default value */
		odm_set_bb_reg(dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x01008c00);
		odm_set_bb_reg(dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x01008c00);
	}
	RF_DBG(dm, DBG_RF_IQK, "%s <==\n", __func__);
}

void _phy_lc_calibrate_8188f(struct dm_struct *dm, boolean is2T)
{
	u8 tmp_reg;
	u32 rf_bmode = 0, lc_cal, cnt;

	/*Check continuous TX and Packet TX*/
	tmp_reg = odm_read_1byte(dm, 0xd03);

	if ((tmp_reg & 0x70) != 0) /*Deal with contisuous TX case*/
		odm_write_1byte(dm, 0xd03, tmp_reg & 0x8F); /*disable all continuous TX*/
	else /* Deal with Packet TX case*/
		odm_write_1byte(dm, REG_TXPAUSE, 0xFF); /* block all queues*/

	/*backup RF0x18*/
	lc_cal = odm_get_rf_reg(dm, RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK);

	/*Start LCK*/
	odm_set_rf_reg(dm, RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK, lc_cal | 0x08000);

	for (cnt = 0; cnt < 100; cnt++) {
		if (odm_get_rf_reg(dm, RF_PATH_A, RF_CHNLBW, 0x8000) != 0x1)
			break;
		ODM_delay_ms(10);
	}

	/*Recover channel number*/
	odm_set_rf_reg(dm, RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK, lc_cal);

	/*Restore original situation*/
	if ((tmp_reg & 0x70) != 0) {
		/*Deal with contisuous TX case*/
		odm_write_1byte(dm, 0xd03, tmp_reg);
	} else {
		/* Deal with Packet TX case*/
		odm_write_1byte(dm, REG_TXPAUSE, 0x00);
	}
}

void phy_iq_calibrate_8188f(void *dm_void, boolean is_recovery)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &(dm->rf_calibrate_info);
	s32 result[4][8]; /* last is final result */
	u8 i, final_candidate, indexforchannel;
	boolean is_patha_ok, is_pathb_ok;
#if 1 //DBG
	s32 rege94, rege9c, regea4, regeac, regeb4 = 0, regebc = 0, regec4 = 0, regecc = 0, reg_tmp = 0;
#else
	s32 rege94, regea4, regeb4, regec4, reg_tmp = 0;
#endif
	boolean is12simular, is13simular, is23simular;
	u32 IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
		REG_OFDM_0_XA_RX_IQ_IMBALANCE, REG_OFDM_0_XB_RX_IQ_IMBALANCE,
		REG_OFDM_0_ECCA_THRESHOLD, REG_OFDM_0_AGC_RSSI_TABLE,
		REG_OFDM_0_XA_TX_IQ_IMBALANCE, REG_OFDM_0_XB_TX_IQ_IMBALANCE,
		REG_OFDM_0_XC_TX_AFE, REG_OFDM_0_XD_TX_AFE,
		REG_OFDM_0_RX_IQ_EXT_ANTA};
	//u32 path_sel_bb = 0;
	u32 path_sel_rf = 0;

#if 0
	if (is_restore) {
		u32 offset, data;
		u8 path, is_result = SUCCESS;
		struct dm_rf_calibration_struct *cali_info = &(dm->rf_calibrate_info);

		/* #define 	PATH_S0                         1 */  /* RF_PATH_B */
		/* #define 	PATH_S1                         0 */  /* RF_PATH_A */

		path = (odm_get_bb_reg(dm, REG_S0_S1_PATH_SWITCH, MASKBYTE0) == 0x00) ? RF_PATH_A : RF_PATH_B;
		/* Restore TX IQK */
		for (i = 0; i < 3; ++i) {
			offset = cali_info->tx_iqc_8723b[path][i][0];
			data = cali_info->tx_iqc_8723b[path][i][1];
			if (offset == 0 || data == 0) {
				is_result = FAIL;
				break;
			}
			RT_TRACE(COMP_MP, DBG_TRACE, ("Switch to S1 TxIQC(offset, data) = (0x%X, 0x%X)\n", offset, data));
			odm_set_bb_reg(dm, offset, MASKDWORD, data);
		}
		/* Restore RX IQK */
		for (i = 0; i < 2; ++i) {
			offset = cali_info->rx_iqc_8723b[path][i][0];
			data = cali_info->rx_iqc_8723b[path][i][1];
			if (offset == 0 || data == 0) {
				is_result = FAIL;
				break;
			}
			RT_TRACE(COMP_MP, DBG_TRACE, ("Switch to S1 RxIQC (offset, data) = (0x%X, 0x%X)\n", offset, data));
			odm_set_bb_reg(dm, offset, MASKDWORD, data);
		}

		if (dm->rf_calibrate_info.tx_lok[RF_PATH_A] == 0)
			is_result = FAIL;
		else {
			odm_set_rf_reg(dm, RF_PATH_A, RF_TXM_IDAC, RFREGOFFSETMASK, dm->rf_calibrate_info.tx_lok[RF_PATH_A]);
			odm_set_rf_reg(dm, RF_PATH_B, RF_TXM_IDAC, RFREGOFFSETMASK, dm->rf_calibrate_info.tx_lok[RF_PATH_B]);
		}

		if (is_result == SUCCESS)
			return;
	}
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_CE | ODM_AP))
	if (is_recovery)
#else /* for ODM_WIN */
	if (is_recovery && !dm->is_in_hct_test) /* YJ,add for PowerTest,120405 */
#endif
	{
		RF_DBG(dm, DBG_RF_INIT, "%s: Return due to is_recovery!\n",
		       __func__);
		_phy_reload_adda_registers8188f(dm, IQK_BB_REG_92C, dm->rf_calibrate_info.IQK_BB_backup_recover, 9);
		return;
	}
	RF_DBG(dm, DBG_RF_IQK, "IQK:Start!!!\n");

	/* Save RF path */
	//	path_sel_bb = odm_get_bb_reg(dm, R_0x948, MASKDWORD);
	//	path_sel_rf = odm_get_rf_reg(dm, RF_PATH_A, RF_0xb0, 0xfffff);

	for (i = 0; i < 8; i++) {
		result[0][i] = 0;
		result[1][i] = 0;
		result[2][i] = 0;
		result[3][i] = 0;
	}
	final_candidate = 0xff;
	is_patha_ok = false;
	is_pathb_ok = false;
	is12simular = false;
	is23simular = false;
	is13simular = false;

	for (i = 0; i < 3; i++) {
		_phy_iq_calibrate_8188f(dm, result, i, false);

		if (i == 1) {
			is12simular = phy_simularity_compare_8188f(dm, result, 0, 1);
			if (is12simular) {
				final_candidate = 0;
				RF_DBG(dm, DBG_RF_IQK,
				       "IQK: is12simular final_candidate is %x\n",
				       final_candidate);
				break;
			}
		}

		if (i == 2) {
			is13simular = phy_simularity_compare_8188f(dm, result, 0, 2);
			if (is13simular) {
				final_candidate = 0;
				RF_DBG(dm, DBG_RF_IQK,
				       "IQK: is13simular final_candidate is %x\n",
				       final_candidate);

				break;
			}

			is23simular = phy_simularity_compare_8188f(dm, result, 1, 2);

			if (is23simular) {
				final_candidate = 1;
				RF_DBG(dm, DBG_RF_IQK,
				       "IQK: is23simular final_candidate is %x\n",
				       final_candidate);
			} else {
				for (i = 0; i < 8; i++)
					reg_tmp += result[3][i];

				if (reg_tmp != 0)
					final_candidate = 3;
				else
					final_candidate = 0xFF;
			}
		}
	}
	/*	RT_TRACE(COMP_INIT,DBG_LOUD,("Release Mutex in IQCalibrate\n")); */

	for (i = 0; i < 4; i++) {
		rege94 = result[i][0];
		regea4 = result[i][2];
		regeb4 = result[i][4];
		regec4 = result[i][6];
#if DBG
		rege9c = result[i][1];
		regeac = result[i][3];
		regebc = result[i][5];
		regecc = result[i][7];
#endif
		//RF_DBG(dm, DBG_RF_IQK, "IQK: rege94=%04x rege9c=%04x regea4=%04x regeac=%04x regeb4=%04x regebc=%04x regec4=%04x regecc=%04x\n",
		//	rege94, rege9c, regea4, regeac, regeb4, regebc, regec4, regecc);
	}

	if (final_candidate != 0xff) {
		dm->rf_calibrate_info.rege94 = result[final_candidate][0];
		dm->rf_calibrate_info.rege9c = result[final_candidate][1];
		dm->rf_calibrate_info.regeb4 = result[final_candidate][4];
		dm->rf_calibrate_info.regebc = result[final_candidate][5];

		rege94 = result[final_candidate][0];
		regea4 = result[final_candidate][2];
		regeb4 = result[final_candidate][4];
		regec4 = result[final_candidate][6];
#if DBG
		rege9c = result[final_candidate][1];
		regeac = result[final_candidate][3];
		regebc = result[final_candidate][5];
		regecc = result[final_candidate][7];
#endif
		RF_DBG(dm, DBG_RF_IQK, "IQK: final_candidate is %x\n",
		       final_candidate);
		//RF_DBG(dm, DBG_RF_IQK, "IQK: rege94=%04x rege9c=%04x regea4=%04x regeac=%04x regeb4=%04x regebc=%04x regec4=%04x regecc=%04x\n",
		//	rege94, rege9c, regea4, regeac, regeb4, regebc, regec4, regecc);
		is_patha_ok = is_pathb_ok = true;
	} else {
		RF_DBG(dm, DBG_RF_IQK, "IQK: FAIL use default value\n");

		dm->rf_calibrate_info.rege94 = dm->rf_calibrate_info.regeb4 = 0x100; /* X default value */
		dm->rf_calibrate_info.rege9c = dm->rf_calibrate_info.regebc = 0x0; /* Y default value */
	}

	if (rege94 != 0)
		_phy_path_a_fill_iqk_matrix8188f(dm, is_patha_ok, result, final_candidate, (regea4 == 0));

#if 0
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
#if MP_DRIVER == 1
	if (rf_path == RF_PATH_A || ((*dm->mp_mode) == 0))
#endif
	{
		if (regeb4 != 0)
			_phy_path_b_fill_iqk_matrix8188f(dm, is_pathb_ok, result, final_candidate, (regec4 == 0));
	}
#endif
#endif

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	indexforchannel = odm_get_right_chnl_place_for_iqk(*dm->channel);
#else
	indexforchannel = 0;
#endif

	/* To Fix BSOD when final_candidate is 0xff
	 * by sherry 20120321 */
	if (final_candidate < 4) {
		for (i = 0; i < iqk_matrix_reg_num; i++)
			dm->rf_calibrate_info.iqk_matrix_reg_setting[indexforchannel].value[0][i] = result[final_candidate][i];
		dm->rf_calibrate_info.iqk_matrix_reg_setting[indexforchannel].is_iqk_done = true;
	}
	/* RT_DISP(FINIT, INIT_IQK, ("\nIQK OK indexforchannel %d.\n", indexforchannel)); */
	RF_DBG(dm, DBG_RF_IQK, "\nIQK OK indexforchannel %d.\n",
	       indexforchannel);
	_phy_save_adda_registers8188f(dm, IQK_BB_REG_92C, dm->rf_calibrate_info.IQK_BB_backup_recover, IQK_BB_REG_NUM);

	/* Restore RF path */
	//	odm_set_bb_reg(dm, R_0x948, MASKDWORD, path_sel_bb);
	//	odm_set_rf_reg(dm, RF_PATH_A, RF_0xb0, 0xfffff, path_sel_rf);

	RF_DBG(dm, DBG_RF_IQK, "IQK finished 8188F\n");
}

void phy_lc_calibrate_8188f(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	_phy_lc_calibrate_8188f(dm, false);
}

void _phy_set_rf_path_switch_8188f(
#if ((DM_ODM_SUPPORT_TYPE & ODM_AP) || (DM_ODM_SUPPORT_TYPE == ODM_CE))
				   struct dm_struct *dm,
#else
				   void *adapter,
#endif
				   boolean is_main, boolean is2T)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	struct dm_struct *dm = &hal_data->DM_OutSrc;
#endif

	if (is_main) /* Left antenna */
		odm_set_bb_reg(dm, R_0x92c, MASKDWORD, 0x1);
	else
		odm_set_bb_reg(dm, R_0x92c, MASKDWORD, 0x2);
}

void phy_set_rf_path_switch_8188f(
#if ((DM_ODM_SUPPORT_TYPE & ODM_AP) || (DM_ODM_SUPPORT_TYPE == ODM_CE))
				  struct dm_struct *dm,
#else
				  void *adapter,
#endif
				  boolean is_main)
{
#if DISABLE_BB_RF
	return;
#endif

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	_phy_set_rf_path_switch_8188f(dm, is_main, true);
#else
	_phy_set_rf_path_switch_8188f(adapter, is_main, true);
#endif
#endif
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
/* return value true => Main; false => Aux */

boolean _phy_query_rf_path_switch_8188f(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct dm_struct *dm,
#else
	void *adapter,
#endif
	boolean is2T)
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

	if (odm_get_bb_reg(dm, R_0x92c, MASKDWORD) == 0x01)
		return true;
	else
		return false;
}

/* return value true => Main; false => Aux */
boolean phy_query_rf_path_switch_8188f(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct dm_struct *dm
#else
	void *adapter
#endif
	)
{
#if DISABLE_BB_RF
	return true;
#endif

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	return _phy_query_rf_path_switch_8188f(adapter, false);
#else
	return _phy_query_rf_path_switch_8188f(dm, false);
#endif
}
#endif

u32 phy_psd_log2base(u32 val)
{
	u8 i, j;
	u32 tmp, tmp2, val_integerdb = 0, tindex, shiftcount = 0;
	u32 result, val_fractiondb = 0, table_fraction[21] = {0, 432, 332, 274, 232, 200,
							      174, 151, 132, 115, 100, 86,
							      74, 62, 51, 42, 32, 23, 15, 7, 0};

	if (val == 0)
		return 0;

	tmp = val;
	while (1) {
		if (tmp == 1)
			break;

		else {
			tmp = (tmp >> 1);
			shiftcount++;
		}
	}

	val_integerdb = shiftcount + 1;
	tmp2 = 1;

	for (j = 1; j <= val_integerdb; j++)
		tmp2 = tmp2 * 2;

	tmp = (val * 100) / tmp2;
	tindex = tmp / 5;

	if (tindex > 20)
		tindex = 20;

	val_fractiondb = table_fraction[tindex];

	result = val_integerdb * 100 - val_fractiondb;

	return result;
}

void phy_active_large_power_detection_8188f(struct dm_struct *dm)
{
	u8 i = 1, j = 0, retrycnt = 2;
	u32 threshold_psd = 56, tmp_psd = 0, tmp_psd_db = 0, rf_mode;

	u32 ADDA_REG[IQK_ADDA_REG_NUM] = {
		REG_FPGA0_XCD_SWITCH_CONTROL, REG_BLUE_TOOTH,
		REG_RX_WAIT_CCA, REG_TX_CCK_RFON,
		REG_TX_CCK_BBON, REG_TX_OFDM_RFON,
		REG_TX_OFDM_BBON, REG_TX_TO_RX,
		REG_TX_TO_TX, REG_RX_CCK,
		REG_RX_OFDM, REG_RX_WAIT_RIFS,
		REG_RX_TO_RX, REG_STANDBY,
		REG_SLEEP, REG_PMPD_ANAEN};
	u32 IQK_MAC_REG[IQK_MAC_REG_NUM] = {
		REG_TXPAUSE, REG_BCN_CTRL,
		REG_BCN_CTRL_1, REG_GPIO_MUXCFG};

	/* since 92C & 92D have the different define in IQK_BB_REG */
	u32 IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
		REG_OFDM_0_TRX_PATH_ENABLE, REG_OFDM_0_TR_MUX_PAR,
		REG_FPGA0_XCD_RF_INTERFACE_SW, REG_CONFIG_ANT_A, REG_CONFIG_ANT_B,
		REG_FPGA0_XAB_RF_INTERFACE_SW, REG_FPGA0_XA_RF_INTERFACE_OE,
		REG_FPGA0_XB_RF_INTERFACE_OE, REG_FPGA0_RFMOD};

	BOOLEAN goout = FALSE;

	_phy_save_adda_registers8188f(dm, ADDA_REG, dm->rf_calibrate_info.ADDA_backup, IQK_ADDA_REG_NUM);
	_phy_save_mac_registers8188f(dm, IQK_MAC_REG, dm->rf_calibrate_info.IQK_MAC_backup);
	_phy_save_adda_registers8188f(dm, IQK_BB_REG_92C, dm->rf_calibrate_info.IQK_BB_backup, IQK_BB_REG_NUM);

	_phy_path_adda_on8188f(dm, ADDA_REG, TRUE, FALSE);

	rf_mode = odm_get_rf_reg(dm, RF_PATH_A, RF_0x0, RFREGOFFSETMASK);
	/*RF_DBG(dm, DBG_RF_IQK, "[Act_Large_PWR]Original RF mode = 0x%x\n", odm_get_rf_reg(dm, ODM_RF_PATH_A, RF_0x0, RFREGOFFSETMASK));*/

	do {
		switch (i) {
		case 1: /*initial setting*/
			RF_DBG(dm, DBG_RF_IQK,
			       "[Act_Large_PWR]Loopback test Start!!\n");
			odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x000000);
			odm_set_rf_reg(dm, RF_PATH_A, RF_WE_LUT, 0x80000, 0x1);
			odm_set_rf_reg(dm, RF_PATH_A, RF_RCK_OS, RFREGOFFSETMASK, 0x28000);
			odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G1, RFREGOFFSETMASK, 0x0000f);
			odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0x3fffe);
			odm_set_rf_reg(dm, RF_PATH_A, RF_DBG_LP_RX2, 0x00800, 0x1);
			odm_set_rf_reg(dm, RF_PATH_A, RF_0x56, 0x00fff, 0x67);

			odm_set_bb_reg(dm, REG_OFDM_0_TRX_PATH_ENABLE, MASKDWORD, 0x03a05600);
			odm_set_bb_reg(dm, REG_OFDM_0_TR_MUX_PAR, MASKDWORD, 0x000800e4);
			odm_set_bb_reg(dm, REG_FPGA0_XCD_RF_INTERFACE_SW, MASKDWORD, 0x25204000);

			odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x808000);
			odm_set_bb_reg(dm, REG_TX_IQK, MASKDWORD, 0x80007C00); /*set two tone*/
			odm_set_bb_reg(dm, REG_RX_IQK, MASKDWORD, 0x01004800);
			odm_set_bb_reg(dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x30008c1c);
			odm_set_bb_reg(dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x10009c1c);
			odm_set_bb_reg(dm, REG_TX_IQK_PI_A, MASKDWORD, 0x82160000);
			odm_set_bb_reg(dm, REG_RX_IQK_PI_A, MASKDWORD, 0x2815200f);
			odm_set_bb_reg(dm, REG_IQK_AGC_RSP, MASKDWORD, 0x0046a910);

			odm_set_bb_reg(dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
			odm_set_bb_reg(dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);
			ODM_delay_ms(IQK_DELAY_TIME_8188F);

			if (odm_get_bb_reg(dm, R_0xea0, MASKDWORD) <= 0xa) {
				RF_DBG(dm, DBG_RF_IQK,
				       "[Act_Large_PWR]Skip Activation due to abnormal 0xea0 value (0x%x)\n",
				       odm_get_bb_reg(dm, R_0xea0, MASKDWORD));
				goout = TRUE;
				break;

			} else {
				tmp_psd = 3 * (phy_psd_log2base(odm_get_bb_reg(dm, R_0xea0, MASKDWORD)));
				tmp_psd_db = tmp_psd / 100;

				RF_DBG(dm, DBG_RF_IQK,
				       "[Act_Large_PWR]0xea0 = 0x%x, tmp_PSD_dB = %d, (criterion = %d)\n",
				       odm_get_bb_reg(dm, R_0xea0, MASKDWORD),
				       tmp_psd_db, threshold_psd);

				i = 2;
				break;
			}

		case 2: /*check PSD*/
			if (tmp_psd_db < threshold_psd) {
				if (j < retrycnt) {
					RF_DBG(dm, DBG_RF_IQK, "[Act_Large_PWR]Activation Start!!\n");
					odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x000000);
					odm_set_bb_reg(dm, R_0x88c, BIT(20) | BIT(21), 0x3);
					odm_set_rf_reg(dm, RF_PATH_A, RF_0x58, 0x2, 0x1);
					/*RF_DBG(dm, DBG_RF_IQK, "[Act_Large_PWR]RF mode before set = %x\n", odm_get_rf_reg(dm, ODM_RF_PATH_A, RF_0x0, RFREGOFFSETMASK));*/
					odm_set_rf_reg(dm, RF_PATH_A, RF_0x0, 0xf001f, 0x2001f);
					RF_DBG(dm, DBG_RF_IQK, "[Act_Large_PWR]set RF 0x0 = %x\n", odm_get_rf_reg(dm, RF_PATH_A, RF_0x0, RFREGOFFSETMASK));
					ODM_delay_ms(200);
					odm_set_rf_reg(dm, RF_PATH_A, RF_0x0, RFREGOFFSETMASK, rf_mode);
					odm_set_rf_reg(dm, RF_PATH_A, RF_0x58, 0x2, 0x0);
					odm_set_bb_reg(dm, R_0x88c, BIT(20) | BIT(21), 0x0);
					ODM_delay_ms(100);
					i = 1;
					j++;
					break;

				} else {
					RF_DBG(dm, DBG_RF_IQK, "[Act_Large_PWR]Activation fail!!!\n");
					goout = TRUE;
					break;
				}
			} else {
				RF_DBG(dm, DBG_RF_IQK,
				       "[Act_Large_PWR]No need Activation!!!\n");
				goout = TRUE;
				break;
			}
		}
	} while (!goout);

	odm_set_bb_reg(dm, REG_TX_IQK, MASKDWORD, 0x01007C00);
	odm_set_bb_reg(dm, REG_FPGA0_IQK, MASKH3BYTES, 0x000000);
	odm_set_rf_reg(dm, RF_PATH_A, RF_DBG_LP_RX2, 0x00800, 0x0);
	odm_set_rf_reg(dm, RF_PATH_A, RF_WE_LUT, 0x80000, 0x0);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x0, RFREGOFFSETMASK, rf_mode);
	RF_DBG(dm, DBG_RF_IQK, "[Act_Large_PWR]Reload RF mode = 0x%x\n",
	       odm_get_rf_reg(dm, RF_PATH_A, RF_0x0, RFREGOFFSETMASK));

	_phy_reload_adda_registers8188f(dm, ADDA_REG, dm->rf_calibrate_info.ADDA_backup, IQK_ADDA_REG_NUM);
	_phy_reload_mac_registers8188f(dm, IQK_MAC_REG, dm->rf_calibrate_info.IQK_MAC_backup);
	_phy_reload_adda_registers8188f(dm, IQK_BB_REG_92C, dm->rf_calibrate_info.IQK_BB_backup, IQK_BB_REG_NUM);

	RF_DBG(dm, DBG_RF_IQK, "[Act_Large_PWR]Activation process finish!!!\n");
}
#endif
