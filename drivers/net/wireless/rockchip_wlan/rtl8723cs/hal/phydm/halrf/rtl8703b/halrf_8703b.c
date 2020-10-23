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

#if (RTL8703B_SUPPORT == 1)

/*---------------------------Define Local Constant---------------------------*/
/* IQK */
#define IQK_DELAY_TIME_8703B 10
#define LCK_DELAY_TIME_8703B 100

/* LTE_COEX */
#define REG_LTECOEX_CTRL 0x07C0
#define REG_LTECOEX_WRITE_DATA 0x07C4
#define REG_LTECOEX_READ_DATA 0x07C8
#define REG_LTECOEX_PATH_CONTROL 0x70

/* 2010/04/25 MH Define the max tx power tracking tx agc power. */
#define ODM_TXPWRTRACK_MAX_IDX8703B 6

#define idx_0xc94 0
#define idx_0xc80 1
#define idx_0xc4c 2

#define idx_0xc14 0
#define idx_0xca0 1

#define KEY 0
#define VAL 1

/*---------------------------Define Local Constant---------------------------*/

/* 3============================================================
 * 3 Tx Power Tracking
 * 3============================================================ */

void set_iqk_matrix_8703b(struct dm_struct *dm, u8 OFDM_index, u8 rf_path,
			  s32 iqk_result_x, s32 iqk_result_y)
{
	s32 ele_A = 0, ele_D = 0, ele_C = 0, value32;
	s32 ele_A_ext = 0, ele_C_ext = 0, ele_D_ext = 0;

	rf_path = RF_PATH_A;

	if (OFDM_index >= OFDM_TABLE_SIZE)
		OFDM_index = OFDM_TABLE_SIZE - 1;
	else if (OFDM_index < 0)
		OFDM_index = 0;

	if (iqk_result_x != 0 && (*dm->band_type == ODM_BAND_2_4G)) {
		/* new element D */
		ele_D = (ofdm_swing_table_new[OFDM_index] & 0xFFC00000) >> 22;
		ele_D_ext = (((iqk_result_x * ele_D) >> 7) & 0x01);

		/* new element A */
		if ((iqk_result_x & 0x00000200) != 0) /* consider minus */
			iqk_result_x = iqk_result_x | 0xFFFFFC00;
		ele_A = ((iqk_result_x * ele_D) >> 8) & 0x000003FF;
		ele_A_ext = ((iqk_result_x * ele_D) >> 7) & 0x1;
		/* new element C */
		if ((iqk_result_y & 0x00000200) != 0)
			iqk_result_y = iqk_result_y | 0xFFFFFC00;
		ele_C = ((iqk_result_y * ele_D) >> 8) & 0x000003FF;
		ele_C_ext = ((iqk_result_y * ele_D) >> 7) & 0x1;

		switch (rf_path) {
		case RF_PATH_A:
			/* write new elements A, C, D to regC80, regC94, reg0xc4c, and element B is always 0 */
			/* write 0xc80 */
			value32 = (ele_D << 22) | ((ele_C & 0x3F) << 16) | ele_A;
			odm_set_bb_reg(dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD, value32);
			/* write 0xc94 */
			value32 = (ele_C & 0x000003C0) >> 6;
			odm_set_bb_reg(dm, REG_OFDM_0_XC_TX_AFE, MASKH4BITS, value32);
			/* write 0xc4c */
			value32 = (ele_D_ext << 28) | (ele_A_ext << 31) | (ele_C_ext << 29);
			value32 = (odm_get_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD) & (~(BIT(31) | BIT(29) | BIT(28)))) | value32;
			odm_set_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD, value32);
			break;
		case RF_PATH_B:
			/* write new elements A, C, D to regC88, regC9C, regC4C, and element B is always 0 */
			/* write 0xc88 */
			value32 = (ele_D << 22) | ((ele_C & 0x3F) << 16) | ele_A;
			odm_set_bb_reg(dm, REG_OFDM_0_XB_TX_IQ_IMBALANCE, MASKDWORD, value32);
			/* write 0xc9c */
			value32 = (ele_C & 0x000003C0) >> 6;
			odm_set_bb_reg(dm, REG_OFDM_0_XD_TX_AFE, MASKH4BITS, value32);
			/* write 0xc4c */
			value32 = (ele_D_ext << 24) | (ele_A_ext << 27) | (ele_C_ext << 25);
			value32 = (odm_get_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD) & (~(BIT(24) | BIT(27) | BIT(25)))) | value32;
			odm_set_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD, value32);
			break;
		default:
			break;
		}
	} else {
		switch (rf_path) {
		case RF_PATH_A:
			odm_set_bb_reg(dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD, ofdm_swing_table_new[OFDM_index]);
			odm_set_bb_reg(dm, REG_OFDM_0_XC_TX_AFE, MASKH4BITS, 0x00);
			value32 = odm_get_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD) & (~(BIT(31) | BIT(29) | BIT(28)));
			odm_set_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD, value32);
			break;

		case RF_PATH_B:
			odm_set_bb_reg(dm, REG_OFDM_0_XB_TX_IQ_IMBALANCE, MASKDWORD, ofdm_swing_table_new[OFDM_index]);
			odm_set_bb_reg(dm, REG_OFDM_0_XD_TX_AFE, MASKH4BITS, 0x00);
			value32 = odm_get_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD) & (~(BIT(24) | BIT(27) | BIT(25)));
			odm_set_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD, value32);
			break;

		default:
			break;
		}
	}

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "TxPwrTracking path %c: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x ele_A_ext = 0x%x ele_C_ext = 0x%x ele_D_ext = 0x%x\n",
	       (rf_path == RF_PATH_A ? 'A' : 'B'), (u32)iqk_result_x,
	       (u32)iqk_result_y, (u32)ele_A, (u32)ele_C, (u32)ele_D,
	       (u32)ele_A_ext, (u32)ele_C_ext, (u32)ele_D_ext);
}

void set_cck_filter_coefficient_8703b(struct dm_struct *dm, u8 cck_swing_index)
{
	odm_write_1byte(dm, 0xa22, cck_swing_table_ch1_ch14_88f[cck_swing_index][0]);
	odm_write_1byte(dm, 0xa23, cck_swing_table_ch1_ch14_88f[cck_swing_index][1]);
	odm_write_1byte(dm, 0xa24, cck_swing_table_ch1_ch14_88f[cck_swing_index][2]);
	odm_write_1byte(dm, 0xa25, cck_swing_table_ch1_ch14_88f[cck_swing_index][3]);
	odm_write_1byte(dm, 0xa26, cck_swing_table_ch1_ch14_88f[cck_swing_index][4]);
	odm_write_1byte(dm, 0xa27, cck_swing_table_ch1_ch14_88f[cck_swing_index][5]);
	odm_write_1byte(dm, 0xa28, cck_swing_table_ch1_ch14_88f[cck_swing_index][6]);
	odm_write_1byte(dm, 0xa29, cck_swing_table_ch1_ch14_88f[cck_swing_index][7]);
	odm_write_1byte(dm, 0xa9a, cck_swing_table_ch1_ch14_88f[cck_swing_index][8]);
	odm_write_1byte(dm, 0xa9b, cck_swing_table_ch1_ch14_88f[cck_swing_index][9]);
	odm_write_1byte(dm, 0xa9c, cck_swing_table_ch1_ch14_88f[cck_swing_index][10]);
	odm_write_1byte(dm, 0xa9d, cck_swing_table_ch1_ch14_88f[cck_swing_index][11]);
	odm_write_1byte(dm, 0xaa0, cck_swing_table_ch1_ch14_88f[cck_swing_index][12]);
	odm_write_1byte(dm, 0xaa1, cck_swing_table_ch1_ch14_88f[cck_swing_index][13]);
	odm_write_1byte(dm, 0xaa2, cck_swing_table_ch1_ch14_88f[cck_swing_index][14]);
	odm_write_1byte(dm, 0xaa3, cck_swing_table_ch1_ch14_88f[cck_swing_index][15]);
}

void do_iqk_8703b(void *dm_void, u8 delta_thermal_index, u8 thermal_value,
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
void odm_tx_pwr_track_set_pwr_8703b(void *dm_void, enum pwrtrack_method method,
				    u8 rf_path, u8 channel_mapped_index)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _ADAPTER *adapter = dm->adapter;
	u8 pwr_tracking_limit_ofdm = 34; /* +0dB */
	u8 pwr_tracking_limit_cck = CCK_TABLE_SIZE_88F - 1; /* -2dB */
	u8 tx_rate = 0xFF;
	u8 final_ofdm_swing_index = 0;
	u8 final_cck_swing_index = 0;
	struct dm_rf_calibration_struct *cali_info = &(dm->rf_calibrate_info);
	struct _hal_rf_ *rf = &(dm->rf_table);

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
#if (MP_DRIVER == 1) /*win MP */
	PMPT_CONTEXT p_mpt_ctx = &(adapter->MptCtx);

	tx_rate = MptToMgntRate(p_mpt_ctx->MptRateIndex);
#else /*win normal*/
	PMGNT_INFO mgnt_info = &(((PADAPTER)adapter)->MgntInfo);
	if (!mgnt_info->ForcedDataRate) { /*auto rate*/
		tx_rate = ((PADAPTER)adapter)->HalFunc.GetHwRateFromMRateHandler(dm->tx_rate);
	} else
		tx_rate = (u8)mgnt_info->ForcedDataRate;
#endif
#elif (DM_ODM_SUPPORT_TYPE & (ODM_CE))
	if (*dm->mp_mode == true) { /*CE MP*/
#ifdef CONFIG_MP_INCLUDED
		PMPT_CONTEXT p_mpt_ctx = &(adapter->mppriv.mpt_ctx);

		tx_rate = mpt_to_mgnt_rate(p_mpt_ctx->mpt_rate_index);
#endif
	} else { /*CE normal*/
		u16 rate = *(dm->forced_data_rate);

		if (!rate) { /*auto rate*/
			if (dm->number_linked_client != 0)
				tx_rate = hw_rate_to_m_rate(dm->tx_rate);
			else
				tx_rate = rf->p_rate_index;
		} else /*force rate*/
			tx_rate = (u8)rate;
	}
#endif

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "===>ODM_TxPwrTrackSetPwr8703B\n");

	if (tx_rate != 0xFF) {
		/*2 CCK*/
		if ((tx_rate >= MGN_1M && tx_rate <= MGN_5_5M) || tx_rate == MGN_11M)
			pwr_tracking_limit_cck = CCK_TABLE_SIZE_88F - 1;
		/*2 OFDM*/
		else if ((tx_rate >= MGN_6M) && (tx_rate <= MGN_48M))
			pwr_tracking_limit_ofdm = 36; /*+3dB*/
		else if (tx_rate == MGN_54M)
			pwr_tracking_limit_ofdm = 34; /*+2dB*/
		/*2 HT*/
		else if ((tx_rate >= MGN_MCS0) && (tx_rate <= MGN_MCS2)) /*QPSK/BPSK*/
			pwr_tracking_limit_ofdm = 38; /*+4dB*/
		else if ((tx_rate >= MGN_MCS3) && (tx_rate <= MGN_MCS4)) /*16QAM*/
			pwr_tracking_limit_ofdm = 36; /*+3dB*/
		else if ((tx_rate >= MGN_MCS5) && (tx_rate <= MGN_MCS7)) /*64QAM*/
			pwr_tracking_limit_ofdm = 34; /*+2dB*/
		else
			pwr_tracking_limit_ofdm = cali_info->default_ofdm_index; /*Default OFDM index = 30*/
	}
	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "tx_rate=0x%x, pwr_tracking_limit=%d\n",
	       tx_rate, pwr_tracking_limit_ofdm);

	if (method == TXAGC) {
		u32 pwr = 0, tx_agc = 0;

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		       "odm_TxPwrTrackSetPwr8703B CH=%d\n", *(dm->channel));

		cali_info->remnant_ofdm_swing_idx[rf_path] = cali_info->absolute_ofdm_swing_idx[rf_path];

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (MP_DRIVER != 1)
		cali_info->modify_tx_agc_flag_path_a = true;
		cali_info->modify_tx_agc_flag_path_a_cck = true;

		odm_set_tx_power_index_by_rate_section(dm, (enum rf_path)rf_path, *dm->channel, CCK);
		odm_set_tx_power_index_by_rate_section(dm, (enum rf_path)rf_path, *dm->channel, OFDM);
		odm_set_tx_power_index_by_rate_section(dm, (enum rf_path)rf_path, *dm->channel, HT_MCS0_MCS7);
#else
		pwr = odm_get_bb_reg(dm, REG_TX_AGC_A_RATE18_06, 0xFF);
		pwr += cali_info->power_index_offset[rf_path];
		odm_set_bb_reg(dm, REG_TX_AGC_A_CCK_1_MCS32, MASKBYTE1, pwr);
		tx_agc = (pwr << 16) | (pwr << 8) | (pwr);
		odm_set_bb_reg(dm, REG_TX_AGC_B_CCK_11_A_CCK_2_11, 0xffffff00, tx_agc);
		RT_DISP(FPHY, PHY_TXPWR, ("ODM_TxPwrTrackSetPwr8703B: CCK Tx-rf(A) Power = 0x%x\n", tx_agc));

		pwr = odm_get_bb_reg(dm, REG_TX_AGC_A_RATE18_06, 0xFF);
		pwr += (cali_info->bb_swing_idx_ofdm[rf_path] - cali_info->bb_swing_idx_ofdm_base[rf_path]);
		tx_agc |= ((pwr << 24) | (pwr << 16) | (pwr << 8) | pwr);
		odm_set_bb_reg(dm, REG_TX_AGC_A_RATE18_06, MASKDWORD, tx_agc);
		odm_set_bb_reg(dm, REG_TX_AGC_A_RATE54_24, MASKDWORD, tx_agc);
		odm_set_bb_reg(dm, REG_TX_AGC_A_MCS03_MCS00, MASKDWORD, tx_agc);
		odm_set_bb_reg(dm, REG_TX_AGC_A_MCS07_MCS04, MASKDWORD, tx_agc);
		odm_set_bb_reg(dm, REG_TX_AGC_A_MCS11_MCS08, MASKDWORD, tx_agc);
		odm_set_bb_reg(dm, REG_TX_AGC_A_MCS15_MCS12, MASKDWORD, tx_agc);
		RT_DISP(FPHY, PHY_TXPWR, ("ODM_TxPwrTrackSetPwr8703B: OFDM Tx-rf(A) Power = 0x%x\n", tx_agc));
#endif
#endif
	} else if (method == BBSWING) {
		final_ofdm_swing_index = cali_info->default_ofdm_index + cali_info->absolute_ofdm_swing_idx[rf_path];
		final_cck_swing_index = cali_info->default_cck_index + cali_info->absolute_ofdm_swing_idx[rf_path];

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		       " cali_info->default_ofdm_index=%d,  cali_info->DefaultCCKIndex=%d, cali_info->absolute_ofdm_swing_idx[rf_path]=%d, cali_info->remnant_cck_swing_idx=%d   rf_path = %d\n",
		       cali_info->default_ofdm_index,
		       cali_info->default_cck_index,
		       cali_info->absolute_ofdm_swing_idx[rf_path],
		       cali_info->remnant_cck_swing_idx, rf_path);

		/* Adjust BB swing by OFDM IQ matrix */
		if (final_ofdm_swing_index >= pwr_tracking_limit_ofdm)
			final_ofdm_swing_index = pwr_tracking_limit_ofdm;
		else if (final_ofdm_swing_index < 0)
			final_ofdm_swing_index = 0;

		if (final_cck_swing_index >= CCK_TABLE_SIZE_88F)
			final_cck_swing_index = CCK_TABLE_SIZE_88F - 1;
		else if (cali_info->bb_swing_idx_cck < 0)
			final_cck_swing_index = 0;

		set_iqk_matrix_8703b(dm, final_ofdm_swing_index, RF_PATH_A,
				     cali_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
				     cali_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][1]);

		set_cck_filter_coefficient_8703b(dm, final_cck_swing_index);

	} else if (method == MIX_MODE) {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		       " dm->default_ofdm_index=%d,  dm->DefaultCCKIndex=%d, dm->absolute_ofdm_swing_idx[rf_path]=%d, dm->remnant_cck_swing_idx=%d   rf_path = %d\n",
		       cali_info->default_ofdm_index,
		       cali_info->default_cck_index,
		       cali_info->absolute_ofdm_swing_idx[rf_path],
		       cali_info->remnant_cck_swing_idx, rf_path);

		final_ofdm_swing_index = cali_info->default_ofdm_index + cali_info->absolute_ofdm_swing_idx[rf_path];
		final_cck_swing_index = cali_info->default_cck_index + cali_info->absolute_ofdm_swing_idx[rf_path];

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		       " dm->default_ofdm_index=%d,  dm->DefaultCCKIndex=%d, dm->absolute_ofdm_swing_idx[rf_path]=%d   rf_path = %d\n",
		       cali_info->default_ofdm_index,
		       cali_info->default_cck_index,
		       cali_info->absolute_ofdm_swing_idx[rf_path], rf_path);

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		       " final_ofdm_swing_index=%d,  final_cck_swing_index=%d rf_path=%d\n",
		       final_ofdm_swing_index, final_cck_swing_index, rf_path);

		if (final_ofdm_swing_index > pwr_tracking_limit_ofdm) { /*BBSwing higher then Limit*/
			cali_info->remnant_ofdm_swing_idx[rf_path] = final_ofdm_swing_index - pwr_tracking_limit_ofdm;

			set_iqk_matrix_8703b(dm, pwr_tracking_limit_ofdm, RF_PATH_A,
					     cali_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
					     cali_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][1]);

			cali_info->modify_tx_agc_flag_path_a = true;
			odm_set_tx_power_index_by_rate_section(dm, (enum rf_path)rf_path, *dm->channel, OFDM);
			odm_set_tx_power_index_by_rate_section(dm, (enum rf_path)rf_path, *dm->channel, HT_MCS0_MCS7);

			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       " ******Path_A Over BBSwing Limit, pwr_tracking_limit = %d, Remnant tx_agc value = %d\n",
			       pwr_tracking_limit_ofdm,
			       cali_info->remnant_ofdm_swing_idx[rf_path]);
		} else if (final_ofdm_swing_index < 0) {
			cali_info->remnant_ofdm_swing_idx[rf_path] = final_ofdm_swing_index;

			set_iqk_matrix_8703b(dm, 0, RF_PATH_A,
					     cali_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
					     cali_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][1]);

			cali_info->modify_tx_agc_flag_path_a = true;
			odm_set_tx_power_index_by_rate_section(dm, (enum rf_path)rf_path, *dm->channel, OFDM);
			odm_set_tx_power_index_by_rate_section(dm, (enum rf_path)rf_path, *dm->channel, HT_MCS0_MCS7);

			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       " ******Path_A Lower then BBSwing lower bound  0, Remnant tx_agc value = %d\n",
			       cali_info->remnant_ofdm_swing_idx[rf_path]);
		} else {
			set_iqk_matrix_8703b(dm, final_ofdm_swing_index, RF_PATH_A,
					     cali_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
					     cali_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][1]);

			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       " ******Path_A Compensate with BBSwing, final_ofdm_swing_index = %d\n",
			       final_ofdm_swing_index);

			if (cali_info->modify_tx_agc_flag_path_a) { /*If tx_agc has changed, reset tx_agc again*/
				cali_info->remnant_ofdm_swing_idx[rf_path] = 0;
				odm_set_tx_power_index_by_rate_section(dm, (enum rf_path)rf_path, *dm->channel, OFDM);
				odm_set_tx_power_index_by_rate_section(dm, (enum rf_path)rf_path, *dm->channel, HT_MCS0_MCS7);
				cali_info->modify_tx_agc_flag_path_a = false;

				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				       " ******Path_A dm->Modify_TxAGC_Flag = false\n");
			}
		}
		if (final_cck_swing_index > pwr_tracking_limit_cck) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       " final_cck_swing_index(%d) > pwr_tracking_limit_cck(%d)\n",
			       final_cck_swing_index, pwr_tracking_limit_cck);

			cali_info->remnant_cck_swing_idx = final_cck_swing_index - pwr_tracking_limit_cck;

			set_cck_filter_coefficient_8703b(dm, pwr_tracking_limit_cck);

			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       "******Path_A CCK Over Limit, pwr_tracking_limit_cck = %d, dm->remnant_cck_swing_idx  = %d\n",
			       pwr_tracking_limit_cck,
			       cali_info->remnant_cck_swing_idx);

			cali_info->modify_tx_agc_flag_path_a_cck = true;

			odm_set_tx_power_index_by_rate_section(dm, RF_PATH_A, *dm->channel, CCK);

		} else if (final_cck_swing_index < 0) { /* Lowest CCK index = 0 */

			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       " final_cck_swing_index(%d) < 0      pwr_tracking_limit_cck(%d)\n",
			       final_cck_swing_index, pwr_tracking_limit_cck);

			cali_info->remnant_cck_swing_idx = final_cck_swing_index;

			set_cck_filter_coefficient_8703b(dm, 0);

			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       "******Path_A CCK Under Limit, pwr_tracking_limit_cck = %d, dm->remnant_cck_swing_idx  = %d\n",
			       0, cali_info->remnant_cck_swing_idx);

			cali_info->modify_tx_agc_flag_path_a_cck = true;

			odm_set_tx_power_index_by_rate_section(dm, RF_PATH_A, *dm->channel, CCK);

		} else {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       " else final_cck_swing_index=%d      pwr_tracking_limit_cck(%d)\n",
			       final_cck_swing_index, pwr_tracking_limit_cck);

			set_cck_filter_coefficient_8703b(dm, final_cck_swing_index);

			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			       "******Path_A CCK Compensate with BBSwing, final_cck_swing_index = %d\n",
			       final_cck_swing_index);

			cali_info->modify_tx_agc_flag_path_a_cck = false;

			cali_info->remnant_cck_swing_idx = 0;

			if (cali_info->modify_tx_agc_flag_path_a_cck) { /*If tx_agc has changed, reset tx_agc again*/
				cali_info->remnant_cck_swing_idx = 0;
				odm_set_tx_power_index_by_rate_section(dm, RF_PATH_A, *dm->channel, CCK);
				cali_info->modify_tx_agc_flag_path_a_cck = false;
			}
		}

	} else {
		return; /* This method is not supported. */
	}
}

void get_delta_swing_table_8703b(void *dm_void, u8 **temperature_up_a,
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

void get_delta_swing_xtal_table_8703b(void *dm_void, s8 **temperature_up_xtal,
				      s8 **temperature_down_xtal)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &(dm->rf_calibrate_info);

	*temperature_up_xtal = cali_info->delta_swing_table_xtal_p;
	*temperature_down_xtal = cali_info->delta_swing_table_xtal_n;
}

void odm_txxtaltrack_set_xtal_8703b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &(dm->rf_calibrate_info);

	s8 crystal_cap;

	crystal_cap = dm->dm_cfo_track.crystal_cap_default & 0x3F;
	crystal_cap = crystal_cap + cali_info->xtal_offset;

	if (crystal_cap < 0)
		crystal_cap = 0;
	else if (crystal_cap > 63)
		crystal_cap = 63;

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
	       "crystal_cap(%d)= dm->dm_cfo_track.crystal_cap_default(%d) + cali_info->xtal_offset(%d)\n",
	       crystal_cap, dm->dm_cfo_track.crystal_cap_default, cali_info->xtal_offset);

	odm_set_bb_reg(dm, REG_MAC_PHY_CTRL, 0xFFF000, (crystal_cap | (crystal_cap << 6)));

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "crystal_cap(0x2c)  0x%X\n",
	       odm_get_bb_reg(dm, REG_MAC_PHY_CTRL, 0xFFF000));
}

void configure_txpower_track_8703b(struct txpwrtrack_cfg *config)
{
	config->swing_table_size_cck = CCK_TABLE_SIZE;
	config->swing_table_size_ofdm = OFDM_TABLE_SIZE;
	config->threshold_iqk = IQK_THRESHOLD;
	config->average_thermal_num = AVG_THERMAL_NUM_8703B;
	config->rf_path_count = MAX_PATH_NUM_8703B;
	config->thermal_reg_addr = RF_T_METER_8703B;

	config->odm_tx_pwr_track_set_pwr = odm_tx_pwr_track_set_pwr_8703b;
	config->do_iqk = do_iqk_8703b;
	config->phy_lc_calibrate = halrf_lck_trigger;
	config->get_delta_swing_table = get_delta_swing_table_8703b;
	config->get_delta_swing_xtal_table = get_delta_swing_xtal_table_8703b;
	config->odm_txxtaltrack_set_xtal = odm_txxtaltrack_set_xtal_8703b;
}

/* 1 7.	IQK */
#define MAX_TOLERANCE 5
#define IQK_DELAY_TIME 1 /* ms */

u8 /* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
	phy_path_a_iqk_8703b(
		struct dm_struct *dm)
{
	u32 reg_eac, reg_e94, reg_e9c;
	u8 result = 0x00, ktime;
	u32 original_path, original_gnt;

	RF_DBG(dm, DBG_RF_IQK, "[IQK]TX IQK!\n");

	/*8703b IQK v2.0 20150713*/
	/*1 Tx IQK*/
	/*IQK setting*/
	odm_set_bb_reg(dm, REG_TX_IQK, MASKDWORD, 0x01007c00);
	odm_set_bb_reg(dm, REG_RX_IQK, MASKDWORD, 0x01004800);
	/*path-A IQK setting*/
	odm_set_bb_reg(dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	odm_set_bb_reg(dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(dm, REG_TX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(dm, REG_RX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	/*	odm_set_bb_reg(dm, REG_TX_IQK_PI_A, MASKDWORD, 0x8214010a);*/
	odm_set_bb_reg(dm, REG_TX_IQK_PI_A, MASKDWORD, 0x8214030f);
	odm_set_bb_reg(dm, REG_RX_IQK_PI_A, MASKDWORD, 0x28110000);
	odm_set_bb_reg(dm, REG_TX_IQK_PI_B, MASKDWORD, 0x82110000);
	odm_set_bb_reg(dm, REG_RX_IQK_PI_B, MASKDWORD, 0x28110000);

	/*LO calibration setting*/
	odm_set_bb_reg(dm, REG_IQK_AGC_RSP, MASKDWORD, 0x00462911);

	/*leave IQK mode*/
	odm_set_bb_reg(dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	/*PA, PAD setting*/
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, 0x800, 0x1);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x55, 0x0007f, 0x7);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x7f, RFREGOFFSETMASK, 0x0d400);

	/*enter IQK mode*/
	odm_set_bb_reg(dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);

#if 1
	/*path setting*/
	/*Save Original path Owner, Original GNT*/
	original_path = odm_get_mac_reg(dm, REG_LTECOEX_PATH_CONTROL, MASKDWORD); /*save 0x70*/
	odm_set_bb_reg(dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
	ODM_delay_ms(1);
	original_gnt = odm_get_bb_reg(dm, REG_LTECOEX_READ_DATA, MASKDWORD); /*save 0x38*/

	/*set GNT_WL=1/GNT_BT=0  and path owner to WiFi for pause BT traffic*/
	odm_set_bb_reg(dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, 0x00007700);
	odm_set_bb_reg(dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc0020038); /*0x38[15:8] = 0x77*/
	odm_set_mac_reg(dm, REG_LTECOEX_PATH_CONTROL, BIT(26), 0x1); /*0x70[26] =1 --> path Owner to WiFi*/
#endif

	/*One shot, path A LOK & IQK*/
	odm_set_bb_reg(dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/* delay x ms */
	ODM_delay_ms(IQK_DELAY_TIME_8703B);
	ktime = 0;
	while ((odm_get_bb_reg(dm, R_0xe90, MASKDWORD) == 0) && ktime < 10) {
		ODM_delay_ms(5);
		ktime++;
	}

#if 1
	/*path setting*/
	/*Restore GNT_WL/GNT_BT  and path owner*/
	odm_set_bb_reg(dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, original_gnt);
	odm_set_bb_reg(dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc00f0038);
	odm_set_mac_reg(dm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, original_path);

	original_path = odm_get_mac_reg(dm, REG_LTECOEX_PATH_CONTROL, MASKDWORD); /*save 0x70*/
	odm_set_bb_reg(dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
	ODM_delay_ms(1);
	original_gnt = odm_get_bb_reg(dm, REG_LTECOEX_READ_DATA, MASKDWORD); /*save 0x38*/

#endif

	/*leave IQK mode*/
	odm_set_bb_reg(dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	/*	PA/PAD controlled by 0x0*/
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, 0x800, 0x0);

	/* Check failed*/
	reg_eac = odm_get_bb_reg(dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_e94 = odm_get_bb_reg(dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD);
	reg_e9c = odm_get_bb_reg(dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD);
	RF_DBG(dm, DBG_RF_IQK, "[IQK]0xeac = 0x%x\n", reg_eac);
	RF_DBG(dm, DBG_RF_IQK, "[IQK]0xe94 = 0x%x, 0xe9c = 0x%x\n", reg_e94,
	       reg_e9c);
	/*monitor image power before & after IQK*/
	RF_DBG(dm, DBG_RF_IQK,
	       "[IQK]0xe90(before IQK)= 0x%x, 0xe98(afer IQK) = 0x%x\n",
	       odm_get_bb_reg(dm, R_0xe90, MASKDWORD),
	       odm_get_bb_reg(dm, R_0xe98, MASKDWORD));

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))

		result |= 0x01;

	return result;
}

u8 /* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
	phy_path_a_rx_iqk_8703b(
		struct dm_struct *dm)
{
	u32 reg_eac, reg_e94, reg_e9c, reg_ea4, u4tmp, tmp;
	u8 result = 0x00, ktime;
	u32 original_path, original_gnt;

	RF_DBG(dm, DBG_RF_IQK, "[IQK]RX IQK:Get TXIMR setting\n");
	/* 1 Get TX_XY */

	/* IQK setting */
	odm_set_bb_reg(dm, REG_TX_IQK, MASKDWORD, 0x01007c00);
	odm_set_bb_reg(dm, REG_RX_IQK, MASKDWORD, 0x01004800);

	/* path-A IQK setting */
	odm_set_bb_reg(dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	odm_set_bb_reg(dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(dm, REG_TX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(dm, REG_RX_IQK_TONE_B, MASKDWORD, 0x38008c1c);

	/*	odm_set_bb_reg(dm, REG_TX_IQK_PI_A, MASKDWORD, 0x82160c1f); */
	odm_set_bb_reg(dm, REG_TX_IQK_PI_A, MASKDWORD, 0x8216000f);
	odm_set_bb_reg(dm, REG_RX_IQK_PI_A, MASKDWORD, 0x28110000);
	odm_set_bb_reg(dm, REG_TX_IQK_PI_B, MASKDWORD, 0x82110000);
	odm_set_bb_reg(dm, REG_RX_IQK_PI_B, MASKDWORD, 0x28110000);

	/* LO calibration setting */
	odm_set_bb_reg(dm, REG_IQK_AGC_RSP, MASKDWORD, 0x0046a911);

	/* leave IQK mode */
	odm_set_bb_reg(dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	/* modify RXIQK mode table */
	odm_set_rf_reg(dm, RF_PATH_A, RF_WE_LUT, 0x80000, 0x1);
	odm_set_rf_reg(dm, RF_PATH_A, RF_RCK_OS, RFREGOFFSETMASK, 0x30000);
	odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G1, RFREGOFFSETMASK, 0x00007);
	/*IQK PA off*/
	/*	odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0xf7fb7); */
	odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0x57db7);

	odm_set_bb_reg(dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);

#if 1
	/*path setting*/
	/*Save Original path Owner, Original GNT*/
	original_path = odm_get_mac_reg(dm, REG_LTECOEX_PATH_CONTROL, MASKDWORD); /*save 0x70*/
	odm_set_bb_reg(dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
	ODM_delay_ms(1);
	original_gnt = odm_get_bb_reg(dm, REG_LTECOEX_READ_DATA, MASKDWORD); /*save 0x38*/

	/*set GNT_WL=1/GNT_BT=0  and path owner to WiFi for pause BT traffic*/
	odm_set_bb_reg(dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, 0x00007700);
	odm_set_bb_reg(dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc0020038); /*0x38[15:8] = 0x77*/
	odm_set_mac_reg(dm, REG_LTECOEX_PATH_CONTROL, BIT(26), 0x1); /*0x70[26] =1 --> path Owner to WiFi*/
#endif

	/* One shot, path A LOK & IQK */
	odm_set_bb_reg(dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/* delay x ms */
	ODM_delay_ms(IQK_DELAY_TIME_8703B);
	ktime = 0;
	while ((odm_get_bb_reg(dm, R_0xe90, MASKDWORD) == 0) && ktime < 10) {
		ODM_delay_ms(5);
		ktime++;
	}

#if 1
	/*path setting*/
	/*Restore GNT_WL/GNT_BT  and path owner*/
	odm_set_bb_reg(dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, original_gnt);
	odm_set_bb_reg(dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc00f0038);
	odm_set_mac_reg(dm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, original_path);

	original_path = odm_get_mac_reg(dm, REG_LTECOEX_PATH_CONTROL, MASKDWORD); /*save 0x70*/
	odm_set_bb_reg(dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
	ODM_delay_ms(1);
	original_gnt = odm_get_bb_reg(dm, REG_LTECOEX_READ_DATA, MASKDWORD); /*save 0x38*/

#endif

	/* leave IQK mode */
	odm_set_bb_reg(dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	/* Check failed */
	reg_eac = odm_get_bb_reg(dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_e94 = odm_get_bb_reg(dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD);
	reg_e9c = odm_get_bb_reg(dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD);
	RF_DBG(dm, DBG_RF_IQK, "[IQK]0xeac = 0x%x\n", reg_eac);
	RF_DBG(dm, DBG_RF_IQK, "[IQK]0xe94 = 0x%x, 0xe9c = 0x%x\n", reg_e94,
	       reg_e9c);
	/*monitor image power before & after IQK*/
	RF_DBG(dm, DBG_RF_IQK,
	       "[IQK]0xe90(before IQK)= 0x%x, 0xe98(afer IQK) = 0x%x\n",
	       odm_get_bb_reg(dm, R_0xe90, MASKDWORD),
	       odm_get_bb_reg(dm, R_0xe98, MASKDWORD));

	/* Allen 20131125 */
	tmp = (reg_e9c & 0x03FF0000) >> 16;
	if ((tmp & 0x200) > 0)
		tmp = 0x400 - tmp;

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))

		result |= 0x01;
	else /* if Tx not OK, ignore Rx */
		return result;

	u4tmp = 0x80007C00 | (reg_e94 & 0x3FF0000) | ((reg_e9c & 0x3FF0000) >> 16);
	odm_set_bb_reg(dm, REG_TX_IQK, MASKDWORD, u4tmp);
	RF_DBG(dm, DBG_RF_IQK, "[IQK]0xe40 = 0x%x u4tmp = 0x%x\n",
	       odm_get_bb_reg(dm, REG_TX_IQK, MASKDWORD), u4tmp);

	/* 1 RX IQK */
	RF_DBG(dm, DBG_RF_IQK, "[IQK]RX IQK\n");

	/* IQK setting */
	odm_set_bb_reg(dm, REG_RX_IQK, MASKDWORD, 0x01004800);

	/* path-A IQK setting */
	odm_set_bb_reg(dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	odm_set_bb_reg(dm, REG_TX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(dm, REG_RX_IQK_TONE_B, MASKDWORD, 0x38008c1c);

	odm_set_bb_reg(dm, REG_TX_IQK_PI_A, MASKDWORD, 0x82110000);
	odm_set_bb_reg(dm, REG_RX_IQK_PI_A, MASKDWORD, 0x28160c1f);
	/*	odm_set_bb_reg(dm, REG_RX_IQK_PI_A, MASKDWORD, 0x2816001f);*/
	odm_set_bb_reg(dm, REG_TX_IQK_PI_B, MASKDWORD, 0x82110000);
	odm_set_bb_reg(dm, REG_RX_IQK_PI_B, MASKDWORD, 0x28110000);

	/* LO calibration setting */
	odm_set_bb_reg(dm, REG_IQK_AGC_RSP, MASKDWORD, 0x0046a8d1);

	/* modify RXIQK mode table */
	odm_set_bb_reg(dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	odm_set_rf_reg(dm, RF_PATH_A, RF_WE_LUT, 0x80000, 0x1);
	odm_set_rf_reg(dm, RF_PATH_A, RF_RCK_OS, RFREGOFFSETMASK, 0x30000);
	odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G1, RFREGOFFSETMASK, 0x00007);
	/*PA off*/
	odm_set_rf_reg(dm, RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0xf7d77);

	/*PA, PAD setting*/
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, 0x800, 0x1);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x55, 0x0007f, 0x5);

	/*enter IQK mode*/
	odm_set_bb_reg(dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);

#if 1
	/*path setting*/
	/*Save Original path Owner, Original GNT*/
	original_path = odm_get_mac_reg(dm, REG_LTECOEX_PATH_CONTROL, MASKDWORD); /*save 0x70*/
	odm_set_bb_reg(dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
	ODM_delay_ms(1);
	original_gnt = odm_get_bb_reg(dm, REG_LTECOEX_READ_DATA, MASKDWORD); /*save 0x38*/

	/*set GNT_WL=1/GNT_BT=0  and path owner to WiFi for pause BT traffic*/
	odm_set_bb_reg(dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, 0x00007700);
	odm_set_bb_reg(dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc0020038); /*0x38[15:8] = 0x77*/
	odm_set_mac_reg(dm, REG_LTECOEX_PATH_CONTROL, BIT(26), 0x1); /*0x70[26] =1 --> path Owner to WiFi*/
#endif

	/* One shot, path A LOK & IQK */
	odm_set_bb_reg(dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/* delay x ms */
	ODM_delay_ms(IQK_DELAY_TIME_8703B);
	ktime = 0;
	while ((odm_get_bb_reg(dm, R_0xe90, MASKDWORD) == 0) && ktime < 10) {
		ODM_delay_ms(5);
		ktime++;
	}

#if 1
	/*path setting*/
	/*Restore GNT_WL/GNT_BT  and path owner*/
	odm_set_bb_reg(dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, original_gnt);
	odm_set_bb_reg(dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc00f0038);
	odm_set_mac_reg(dm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, original_path);

	original_path = odm_get_mac_reg(dm, REG_LTECOEX_PATH_CONTROL, MASKDWORD); /*save 0x70*/
	odm_set_bb_reg(dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
	ODM_delay_ms(1);
	original_gnt = odm_get_bb_reg(dm, REG_LTECOEX_READ_DATA, MASKDWORD); /*save 0x38*/

#endif

	/*leave IQK mode*/
	odm_set_bb_reg(dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	/*	PA/PAD controlled by 0x0 */
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, 0x800, 0x0);

	/* Check failed */
	reg_eac = odm_get_bb_reg(dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_ea4 = odm_get_bb_reg(dm, REG_RX_POWER_BEFORE_IQK_A_2, MASKDWORD);
	RF_DBG(dm, DBG_RF_IQK, "[IQK]0xeac = 0x%x\n", reg_eac);
	RF_DBG(dm, DBG_RF_IQK, "[IQK]0xea4 = 0x%x, 0xeac = 0x%x\n", reg_ea4,
	       reg_eac);
	/* monitor image power before & after IQK */
	RF_DBG(dm, DBG_RF_IQK,
	       "[IQK]0xea0(before IQK)= 0x%x, 0xea8(afer IQK) = 0x%x\n",
	       odm_get_bb_reg(dm, R_0xea0, MASKDWORD),
	       odm_get_bb_reg(dm, R_0xea8, MASKDWORD));

	/* Allen 20131125 */
	tmp = (reg_eac & 0x03FF0000) >> 16;
	if ((tmp & 0x200) > 0)
		tmp = 0x400 - tmp;

	if (!(reg_eac & BIT(27)) && /*if Tx is OK, check whether Rx is OK*/
	    (((reg_ea4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_eac & 0x03FF0000) >> 16) != 0x36) &&
	    (((reg_ea4 & 0x03FF0000) >> 16) < 0x11a) &&
	    (((reg_ea4 & 0x03FF0000) >> 16) > 0xe6) &&
	    tmp < 0x1a)
		result |= 0x02;
	else /* if Tx not OK, ignore Rx */
		RF_DBG(dm, DBG_RF_IQK, "path A Rx IQK fail!!\n");

	return result;
}

void _phy_path_a_fill_iqk_matrix8703b(struct dm_struct *dm, boolean is_iqk_ok,
				      s32 result[][8], u8 final_candidate,
				      boolean is_tx_only)
{
	struct dm_rf_calibration_struct *cali_info = &(dm->rf_calibrate_info);
	u32 oldval_0, X, TX0_A, reg, tmp0xc80, tmp0xc94, tmp0xc4c, tmp0xc14, tmp0xca0;
	s32 Y, TX0_C;

	RF_DBG(dm, DBG_RF_IQK, "[IQK]path A IQ Calibration %s !\n",
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
		       "[IQK]X = 0x%x, TX0_A = 0x%x, oldval_0 0x%x\n", X, TX0_A,
		       oldval_0);
		tmp0xc80 = (odm_get_bb_reg(dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD) & 0xfffffc00) | (TX0_A & 0x3ff);
		tmp0xc4c = (((X * oldval_0 >> 7) & 0x1) << 31) | (odm_get_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD) & 0x7fffffff);

		Y = result[final_candidate][1];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;

		/* 2 Tx IQC */
		TX0_C = (Y * oldval_0) >> 8;
		RF_DBG(dm, DBG_RF_IQK, "[IQK]Y = 0x%x, TX = 0x%x\n", Y, TX0_C);

		tmp0xc94 = (((TX0_C & 0x3C0) >> 6) << 28) | (odm_get_bb_reg(dm, REG_OFDM_0_XC_TX_AFE, MASKDWORD) & 0x0fffffff);

		cali_info->tx_iqc_8703b[idx_0xc94][KEY] = REG_OFDM_0_XC_TX_AFE;
		cali_info->tx_iqc_8703b[idx_0xc94][VAL] = tmp0xc94;

		tmp0xc80 = (tmp0xc80 & 0xffc0ffff) | (TX0_C & 0x3F) << 16;

		cali_info->tx_iqc_8703b[idx_0xc80][KEY] = REG_OFDM_0_XA_TX_IQ_IMBALANCE;
		cali_info->tx_iqc_8703b[idx_0xc80][VAL] = tmp0xc80;

		tmp0xc4c = (tmp0xc4c & 0xdfffffff) | (((Y * oldval_0 >> 7) & 0x1) << 29);

		cali_info->tx_iqc_8703b[idx_0xc4c][KEY] = REG_OFDM_0_ECCA_THRESHOLD;
		cali_info->tx_iqc_8703b[idx_0xc4c][VAL] = tmp0xc4c;

		if (is_tx_only) {
			RF_DBG(dm, DBG_RF_IQK, "[IQK]%s only Tx OK\n",
			       __func__);

			/* <20130226, Kordan> Saving RxIQC, otherwise not initialized. */
			cali_info->rx_iqc_8703b[idx_0xca0][KEY] = REG_OFDM_0_RX_IQ_EXT_ANTA;
			cali_info->rx_iqc_8703b[idx_0xca0][VAL] = 0xfffffff & odm_get_bb_reg(dm, REG_OFDM_0_RX_IQ_EXT_ANTA, MASKDWORD);
			cali_info->rx_iqc_8703b[idx_0xc14][KEY] = REG_OFDM_0_XA_RX_IQ_IMBALANCE;
			cali_info->rx_iqc_8703b[idx_0xc14][VAL] = 0x40000100;
			return;
		}

		reg = result[final_candidate][2];
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
		if (RTL_ABS(reg, 0x100) >= 16)
			reg = 0x100;
#endif

		/* 2 Rx IQC */
		tmp0xc14 = (0x40000100 & 0xfffffc00) | reg;

		reg = result[final_candidate][3] & 0x3F;
		tmp0xc14 = (tmp0xc14 & 0xffff03ff) | (reg << 10);

		cali_info->rx_iqc_8703b[idx_0xc14][KEY] = REG_OFDM_0_XA_RX_IQ_IMBALANCE;
		cali_info->rx_iqc_8703b[idx_0xc14][VAL] = tmp0xc14;

		reg = (result[final_candidate][3] >> 6) & 0xF;
		tmp0xca0 = odm_get_bb_reg(dm, REG_OFDM_0_RX_IQ_EXT_ANTA, 0x0fffffff) | (reg << 28);

		cali_info->rx_iqc_8703b[idx_0xca0][KEY] = REG_OFDM_0_RX_IQ_EXT_ANTA;
		cali_info->rx_iqc_8703b[idx_0xca0][VAL] = tmp0xca0;
	}
}

#if 0
void
_phy_path_b_fill_iqk_matrix8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct dm_struct		*dm,
#else
	void	*adapter,
#endif
	boolean	is_iqk_ok,
	s32		result[][8],
	u8		final_candidate,
	boolean		is_tx_only			/* do Tx only */
)
{
	u32	oldval_1, X, TX1_A, reg, tmp0xc80, tmp0xc94, tmp0xc4c, tmp0xc14, tmp0xca0;
	s32	Y, TX1_C;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*hal_data = GET_HAL_DATA(((PADAPTER)adapter));
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct dm_struct		*dm = &hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct dm_struct		*dm = &hal_data->DM_OutSrc;
#endif
#endif
	struct dm_rf_calibration_struct	*cali_info = &(dm->rf_calibrate_info);

	RF_DBG(dm, DBG_RF_IQK, "[IQK]path B IQ Calibration %s !\n",
	       (is_iqk_ok) ? "Success" : "Failed");

	if (final_candidate == 0xFF)
		return;

	else if (is_iqk_ok) {
		oldval_1 = (odm_get_bb_reg(dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD) >> 22) & 0x3FF;


		X = result[final_candidate][4];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;
		TX1_A = (X * oldval_1) >> 8;
		RF_DBG(dm, DBG_RF_IQK, "[IQK]X = 0x%x, TX1_A = 0x%x\n", X,
		       TX1_A);

		tmp0xc80 = (odm_get_bb_reg(dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD) & 0xfffffc00) | (TX1_A & 0x3ff);
		tmp0xc4c = (((X * oldval_1 >> 7) & 0x1) << 31) | (odm_get_bb_reg(dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD) & 0x7fffffff);

		Y = result[final_candidate][5];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;

		TX1_C = (Y * oldval_1) >> 8;
		RF_DBG(dm, DBG_RF_IQK, "[IQK]Y = 0x%x, TX1_C = 0x%x\n", Y,
		       TX1_C);

		/*2 Tx IQC*/

		tmp0xc94 = (((TX1_C & 0x3C0) >> 6) << 28) | (odm_get_bb_reg(dm, REG_OFDM_0_XC_TX_AFE, MASKDWORD) & 0x0fffffff);

		cali_info->tx_iqc_8703b[PATH_S0][idx_0xc94][KEY] = REG_OFDM_0_XC_TX_AFE;
		cali_info->tx_iqc_8703b[PATH_S0][idx_0xc94][VAL] = tmp0xc94;

		tmp0xc80 = (tmp0xc80 & 0xffc0ffff) | (TX1_C & 0x3F) << 16;
		cali_info->tx_iqc_8703b[PATH_S0][idx_0xc80][KEY] = REG_OFDM_0_XA_TX_IQ_IMBALANCE;
		cali_info->tx_iqc_8703b[PATH_S0][idx_0xc80][VAL] = tmp0xc80;

		tmp0xc4c = (tmp0xc4c & 0xdfffffff) | (((Y * oldval_1 >> 7) & 0x1) << 29);
		cali_info->tx_iqc_8703b[PATH_S0][idx_0xc4c][KEY] = REG_OFDM_0_ECCA_THRESHOLD;
		cali_info->tx_iqc_8703b[PATH_S0][idx_0xc4c][VAL] = tmp0xc4c;

		if (is_tx_only) {
			RF_DBG(dm, DBG_RF_IQK, "[IQK]%s only Tx OK\n",
			       __func__);

			cali_info->rx_iqc_8703b[PATH_S0][idx_0xc14][KEY] = REG_OFDM_0_XA_RX_IQ_IMBALANCE;
			cali_info->rx_iqc_8703b[PATH_S0][idx_0xc14][VAL] = 0x40000100;
			cali_info->rx_iqc_8703b[PATH_S0][idx_0xca0][KEY] = REG_OFDM_0_RX_IQ_EXT_ANTA;
			cali_info->rx_iqc_8703b[PATH_S0][idx_0xca0][VAL] = 0x0fffffff & odm_get_bb_reg(dm, REG_OFDM_0_RX_IQ_EXT_ANTA, MASKDWORD);
			return;
		}

		/* 2 Rx IQC */
		reg = result[final_candidate][6];
		tmp0xc14 = (0x40000100 & 0xfffffc00) | reg;

		reg = result[final_candidate][7] & 0x3F;
		tmp0xc14 = (tmp0xc14 & 0xffff03ff) | (reg << 10);

		cali_info->rx_iqc_8703b[PATH_S0][idx_0xc14][KEY] = REG_OFDM_0_XA_RX_IQ_IMBALANCE;
		cali_info->rx_iqc_8703b[PATH_S0][idx_0xc14][VAL] = tmp0xc14;

		reg = (result[final_candidate][7] >> 6) & 0xF;
		tmp0xca0 = odm_get_bb_reg(dm, REG_OFDM_0_RX_IQ_EXT_ANTA, 0x0fffffff) | (reg << 28);

		cali_info->rx_iqc_8703b[PATH_S0][idx_0xca0][KEY] = REG_OFDM_0_RX_IQ_EXT_ANTA;
		cali_info->rx_iqc_8703b[PATH_S0][idx_0xca0][VAL] = tmp0xca0;
	}
}
#endif

boolean
odm_set_iqc_by_rfpath_8703b(struct dm_struct *dm)
{
	struct dm_rf_calibration_struct *cali_info = &(dm->rf_calibrate_info);

	if (cali_info->tx_iqc_8703b[idx_0xc80][VAL] != 0x0 && cali_info->rx_iqc_8703b[idx_0xc14][VAL] != 0x0) {
		RF_DBG(dm, DBG_RF_IQK, "[IQK]reload RF IQC!!!\n");
		RF_DBG(dm, DBG_RF_IQK, "[IQK]0xc80 = 0x%x!!!\n",
		       cali_info->tx_iqc_8703b[idx_0xc80][VAL]);
		RF_DBG(dm, DBG_RF_IQK, "[IQK]0xc14 = 0x%x!!!\n",
		       cali_info->tx_iqc_8703b[idx_0xc14][VAL]);

		/* TX IQC */
		odm_set_bb_reg(dm, cali_info->tx_iqc_8703b[idx_0xc94][KEY], MASKH4BITS, (cali_info->tx_iqc_8703b[idx_0xc94][VAL] >> 28));
		odm_set_bb_reg(dm, cali_info->tx_iqc_8703b[idx_0xc80][KEY], MASKDWORD, cali_info->tx_iqc_8703b[idx_0xc80][VAL]);
		odm_set_bb_reg(dm, cali_info->tx_iqc_8703b[idx_0xc4c][KEY], BIT(31), (cali_info->tx_iqc_8703b[idx_0xc4c][VAL] >> 31));
		odm_set_bb_reg(dm, cali_info->tx_iqc_8703b[idx_0xc4c][KEY], BIT(29), ((cali_info->tx_iqc_8703b[idx_0xc4c][VAL] & BIT(29)) >> 29));

		/* RX IQC */
		odm_set_bb_reg(dm, cali_info->rx_iqc_8703b[idx_0xc14][KEY], MASKDWORD, cali_info->rx_iqc_8703b[idx_0xc14][VAL]);
		odm_set_bb_reg(dm, cali_info->rx_iqc_8703b[idx_0xca0][KEY], MASKDWORD, cali_info->rx_iqc_8703b[idx_0xca0][VAL]);
		return true;

	} else {
		RF_DBG(dm, DBG_RF_IQK, "[IQK]IQC value invalid!!!\n");
		return false;
	}
}

void _phy_save_adda_registers8703b(struct dm_struct *dm, u32 *adda_reg,
				   u32 *adda_backup, u32 register_num)
{
	u32 i;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if (odm_check_power_status(dm) == false)
		return;
#endif

	/*	RF_DBG(dm,DBG_RF_IQK, "Save ADDA parameters.\n"); */
	for (i = 0; i < register_num; i++)
		adda_backup[i] = odm_get_bb_reg(dm, adda_reg[i], MASKDWORD);
}

void _phy_save_mac_registers8703b(struct dm_struct *dm, u32 *mac_reg,
				  u32 *mac_backup)
{
	u32 i;

	/*	RF_DBG(dm,DBG_RF_IQK, "Save MAC parameters.\n"); */
	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		mac_backup[i] = odm_read_1byte(dm, mac_reg[i]);

	mac_backup[i] = odm_read_4byte(dm, mac_reg[i]);
}

void _phy_reload_adda_registers8703b(struct dm_struct *dm, u32 *adda_reg,
				     u32 *adda_backup, u32 regiester_num)
{
	u32 i;

	/*	RF_DBG(dm,DBG_RF_IQK, "Reload ADDA power saving parameters !\n"); */
	for (i = 0; i < regiester_num; i++)
		odm_set_bb_reg(dm, adda_reg[i], MASKDWORD, adda_backup[i]);
}

void _phy_reload_mac_registers8703b(struct dm_struct *dm, u32 *mac_reg,
				    u32 *mac_backup)
{
	u32 i;

	/*	RF_DBG(dm,DBG_RF_IQK,  "Reload MAC parameters !\n"); */
	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		odm_write_1byte(dm, mac_reg[i], (u8)mac_backup[i]);

	odm_write_4byte(dm, mac_reg[i], mac_backup[i]);
}

void _phy_path_adda_on8703b(struct dm_struct *dm, u32 *adda_reg,
			    boolean is_path_a_on)
{
	u32 path_on;
	u32 i;

	/*	RF_DBG(dm,DBG_RF_IQK, "ADDA ON.\n"); */
	path_on = 0x03c00014;
	for (i = 0; i < IQK_ADDA_REG_NUM; i++)
		odm_set_bb_reg(dm, adda_reg[i], MASKDWORD, path_on);
}

void _phy_mac_setting_calibration8703b(struct dm_struct *dm, u32 *mac_reg,
				       u32 *mac_backup)
{
	u32 i = 0;

	/*	RF_DBG(dm,DBG_RF_IQK, "MAC settings for Calibration.\n"); */
	odm_write_1byte(dm, mac_reg[i], 0x3F);
	for (i = 1; i < (IQK_MAC_REG_NUM - 1); i++)
		odm_write_1byte(dm, mac_reg[i], (u8)(mac_backup[i] & (~BIT(3))));
	/*remove 0x40[5]setting for coex reason */
	/*odm_write_1byte(dm, mac_reg[i], (u8)(mac_backup[i] & (~BIT(5))));*/
}

boolean
phy_simularity_compare_8703b(struct dm_struct *dm, s32 result[][8], u8 c1,
			     u8 c2, boolean is2t)
{
	u32 i, j, diff, simularity_bit_map, bound = 0;
	u8 final_candidate[2] = {0xFF, 0xFF}; /* for path A and path B */
	boolean is_result = true;

	/* #if !(DM_ODM_SUPPORT_TYPE & ODM_AP) */
	/*	bool		is2T = IS_92C_SERIAL( hal_data->version_id);
	 * #else */
	/* #endif */

	s32 tmp1 = 0, tmp2 = 0;

	if (is2t)
		bound = 8;
	else
		bound = 4;

	/*	RF_DBG(dm,DBG_RF_IQK, "===> IQK:phy_simularity_compare_8192e c1 %d c2 %d!!!\n", c1, c2); */

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
			/*			RF_DBG(dm,DBG_RF_IQK,  "IQK:differnece overflow %d index %d compare1 0x%x compare2 0x%x!!!\n",  diff, i, result[c1][i], result[c2][i]); */

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

	/*	RF_DBG(dm,DBG_RF_IQK, "IQK:phy_simularity_compare_8192e simularity_bit_map   %x !!!\n", simularity_bit_map); */

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

	if (!(simularity_bit_map & 0x03)) { /*path A TX OK*/
		for (i = 0; i < 2; i++)
			result[3][i] = result[c1][i];
	}

	if (!(simularity_bit_map & 0x0c)) { /*path A RX OK*/
		for (i = 2; i < 4; i++)
			result[3][i] = result[c1][i];
	}

	if (!(simularity_bit_map & 0x30)) { /*path B TX OK*/
		for (i = 4; i < 6; i++)
			result[3][i] = result[c1][i];
	}

	if (!(simularity_bit_map & 0xc0)) { /*path B RX OK*/
		for (i = 6; i < 8; i++)
			result[3][i] = result[c1][i];
	}

	return false;
}

void _phy_check_coex_status_8703b(struct dm_struct *dm, boolean beforek)
{
	u8 u1b_tmp;
	u16 count = 0;
	u8 h2c_parameter;

#if MP_DRIVER != 1
	if (beforek) {
		/* Set H2C cmd to inform FW (enable). */
		h2c_parameter = 1;
		odm_fill_h2c_cmd(dm, ODM_H2C_WIFI_CALIBRATION, 1, &h2c_parameter);
		/* Check 0x1e6 or 100ms timeout*/
		count = 0;
		u1b_tmp = odm_read_1byte(dm, 0x1e6);
		while (u1b_tmp != 0x1 && count < 5000) {
			ODM_delay_us(20);
			u1b_tmp = odm_read_1byte(dm, 0x1e6);
			count++;
		}

		if (count >= 5000)
			RF_DBG(dm, DBG_RF_INIT,
			       "[IQK]Polling 0x1e6 to 1 for WiFi calibration H2C cmd FAIL! count(%d)",
			       count);

		/* Wait BT IQK finished. */
		/* polling 0x1e7[0]=1 or 600ms timeout */
		count = 0;
		u1b_tmp = odm_read_1byte(dm, 0x1e7);
		while ((!(u1b_tmp & BIT(0))) && count < 30000) {
			ODM_delay_us(20);
			u1b_tmp = odm_read_1byte(dm, 0x1e7);
			count++;
		}

		if (count >= 30000)
			RF_DBG(dm, DBG_RF_INIT,
			       "[IQK]Waiting BT IQK finish time out! count(%d)",
			       count);
	} else {
		/* Set H2C cmd to inform FW (disable). */
		h2c_parameter = 0;
		odm_fill_h2c_cmd(dm, ODM_H2C_WIFI_CALIBRATION, 1, &h2c_parameter);
		/* Check 0x1e6 or 100ms timeout */
		count = 0;
		u1b_tmp = odm_read_1byte(dm, 0x1e6);
		while (u1b_tmp != 0 && count < 5000) {
			ODM_delay_us(20);
			u1b_tmp = odm_read_1byte(dm, 0x1e6);
			count++;
		}

		if (count >= 5000)
			RF_DBG(dm, DBG_RF_INIT,
			       "[IQK]Polling 0x1e6 to 0 for WiFi calibration H2C cmd FAIL! count(%d)",
			       count);
	}
#endif
}

void _phy_iq_calibrate_8703b(struct dm_struct *dm, s32 result[][8], u8 t)
{
	u32 i;
	u8 path_aok = 0x0, path_bok = 0x0;
	u8 tmp0xc50 = (u8)odm_get_bb_reg(dm, R_0xc50, MASKBYTE0);
	//u8			tmp0xc58 = (u8)odm_get_bb_reg(dm, R_0xc58, MASKBYTE0);
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

	/*since 92C & 92D have the different define in IQK_BB_REG*/
	u32 IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
		REG_OFDM_0_TRX_PATH_ENABLE, REG_OFDM_0_TR_MUX_PAR,
		REG_FPGA0_XCD_RF_INTERFACE_SW, REG_CONFIG_ANT_A, REG_CONFIG_ANT_B,
		REG_FPGA0_XAB_RF_INTERFACE_SW, REG_FPGA0_XA_RF_INTERFACE_OE,
		REG_FPGA0_XB_RF_INTERFACE_OE, REG_CCK_0_AFE_SETTING};
	u32 retry_count;
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	retry_count = 2;
#ifdef MP_TEST
	if (*(dm->mp_mode))
		retry_count = 9;
#endif
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
#if MP_DRIVER
	retry_count = 1;
#else
	retry_count = 2;
#endif
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
	if (*(dm->mp_mode))
		retry_count = 1;
	else
		retry_count = 2;
#endif

	/* Note: IQ calibration must be performed after loading*/
	/*PHY_REG.txt , and radio_a, radio_b.txt*/

	/* u32 bbvalue; */

	if (t == 0) {
		/*	 	 bbvalue = odm_get_bb_reg(dm, REG_FPGA0_RFMOD, MASKDWORD);
		 * 			RT_DISP(FINIT, INIT_IQK, ("_phy_iq_calibrate_8188e()==>0x%08x\n",bbvalue)); */

		RF_DBG(dm, DBG_RF_IQK, "[IQK]IQ Calibration for %d times\n", t);

		/* Save ADDA parameters, turn path A ADDA on*/
		_phy_save_adda_registers8703b(dm, ADDA_REG, dm->rf_calibrate_info.ADDA_backup, IQK_ADDA_REG_NUM);
		_phy_save_mac_registers8703b(dm, IQK_MAC_REG, dm->rf_calibrate_info.IQK_MAC_backup);
		_phy_save_adda_registers8703b(dm, IQK_BB_REG_92C, dm->rf_calibrate_info.IQK_BB_backup, IQK_BB_REG_NUM);
	}
	RF_DBG(dm, DBG_RF_IQK, "[IQK]IQ Calibration for %d times\n", t);

	_phy_path_adda_on8703b(dm, ADDA_REG, true);
	/* MAC settings */
	_phy_mac_setting_calibration8703b(dm, IQK_MAC_REG, dm->rf_calibrate_info.IQK_MAC_backup);
	/* BB setting */
	/*odm_set_bb_reg(dm, REG_FPGA0_RFMOD, BIT24, 0x00);*/
	odm_set_bb_reg(dm, REG_CCK_0_AFE_SETTING, 0x0f000000, 0xf);
	odm_set_bb_reg(dm, REG_OFDM_0_TRX_PATH_ENABLE, MASKDWORD, 0x03a05600);
	odm_set_bb_reg(dm, REG_OFDM_0_TR_MUX_PAR, MASKDWORD, 0x000800e4);
	odm_set_bb_reg(dm, REG_FPGA0_XCD_RF_INTERFACE_SW, MASKDWORD, 0x25204000);

/* path A TX IQK */
#if 1

	for (i = 0; i < retry_count; i++) {
		path_aok = phy_path_a_iqk_8703b(dm);
		if (path_aok == 0x01) {
			RF_DBG(dm, DBG_RF_IQK, "[IQK]Tx IQK Success!!\n");
			result[t][0] = (odm_get_bb_reg(dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
			result[t][1] = (odm_get_bb_reg(dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
			break;
		}
	}
#endif

/* path A RXIQK */
#if 1

	for (i = 0; i < retry_count; i++) {
		path_aok = phy_path_a_rx_iqk_8703b(dm);
		if (path_aok == 0x03) {
			RF_DBG(dm, DBG_RF_IQK, "[IQK]Rx IQK Success!!\n");
			/*			result[t][0] = (odm_get_bb_reg(dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD)&0x3FF0000)>>16;
			 *			result[t][1] = (odm_get_bb_reg(dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD)&0x3FF0000)>>16; */
			result[t][2] = (odm_get_bb_reg(dm, REG_RX_POWER_BEFORE_IQK_A_2, MASKDWORD) & 0x3FF0000) >> 16;
			result[t][3] = (odm_get_bb_reg(dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD) & 0x3FF0000) >> 16;
			break;
		}

		RF_DBG(dm, DBG_RF_IQK, "[IQK]Rx IQK Fail!!\n");
	}

	if (0x00 == path_aok)
		RF_DBG(dm, DBG_RF_IQK, "[IQK]IQK failed!!\n");

#endif

/* path B TX IQK */
#if 0

#if MP_DRIVER != 1
	if ((*dm->is_1_antenna == false) || ((*dm->is_1_antenna == true) && (*dm->rf_default_path == 1))
	    || dm->support_interface == ODM_ITRF_USB)
#endif
	{
		for (i = 0 ; i < retry_count ; i++) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			path_bok = phy_path_b_iqk_8703b(adapter);
#else
			path_bok = phy_path_b_iqk_8703b(dm);
#endif
			/*		if(path_bok == 0x03){ */
			if (path_bok == 0x01) {
				RF_DBG(dm, DBG_RF_IQK,
				       "[IQK]S0 Tx IQK Success!!\n");
				result[t][4] = (odm_get_bb_reg(dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
				result[t][5] = (odm_get_bb_reg(dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
				break;
			}
		}
#endif

/* path B RX IQK */
#if 0

		for (i = 0 ; i < retry_count ; i++) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			path_bok = phy_path_b_rx_iqk_8703b(adapter);
#else
			path_bok = phy_path_b_rx_iqk_8703b(dm);
#endif
			if (path_bok == 0x03) {
				RF_DBG(dm, DBG_RF_IQK,
				       "[IQK]S0 Rx IQK Success!!\n");
				/*				result[t][0] = (odm_get_bb_reg(dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD)&0x3FF0000)>>16;
				 *				result[t][1] = (odm_get_bb_reg(dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD)&0x3FF0000)>>16; */
				result[t][6] = (odm_get_bb_reg(dm, REG_RX_POWER_BEFORE_IQK_A_2, MASKDWORD) & 0x3FF0000) >> 16;
				result[t][7] = (odm_get_bb_reg(dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD) & 0x3FF0000) >> 16;
				break;

			} else
				RF_DBG(dm, DBG_RF_IQK,
				       "[IQK]S0 Rx IQK Fail!!\n");
		}



		if (0x00 == path_bok)
			RF_DBG(dm, DBG_RF_IQK, "[IQK]S0 IQK failed!!\n");
	}
#endif

	/* Back to BB mode, load original value */
	RF_DBG(dm, DBG_RF_IQK,
	       "[IQK]IQK:Back to BB mode, load original value!\n");
	odm_set_bb_reg(dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	if (t != 0) {
		/* Reload ADDA power saving parameters*/
		_phy_reload_adda_registers8703b(dm, ADDA_REG, dm->rf_calibrate_info.ADDA_backup, IQK_ADDA_REG_NUM);
		/* Reload MAC parameters*/
		_phy_reload_mac_registers8703b(dm, IQK_MAC_REG, dm->rf_calibrate_info.IQK_MAC_backup);
		_phy_reload_adda_registers8703b(dm, IQK_BB_REG_92C, dm->rf_calibrate_info.IQK_BB_backup, IQK_BB_REG_NUM);
		/* Allen initial gain 0xc50 */
		/* Restore RX initial gain */
		odm_set_bb_reg(dm, R_0xc50, MASKBYTE0, 0x50);
		odm_set_bb_reg(dm, R_0xc50, MASKBYTE0, tmp0xc50);
		/* load 0xe30 IQC default value */
		odm_set_bb_reg(dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x01008c00);
		odm_set_bb_reg(dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x01008c00);
	}
	RF_DBG(dm, DBG_RF_IQK, "[IQK]%s <==\n", __func__);
}

void _phy_lc_calibrate_8703b(struct dm_struct *dm, boolean is2T)
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
	if (cnt == 100)
		RF_DBG(dm, DBG_RF_LCK, "LCK time out\n");

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

/* IQK version: 0x5  20171109 */
/* 1. add coex. related setting*/

void phy_iq_calibrate_8703b(void *dm_void, boolean is_recovery)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &(dm->rf_calibrate_info);
	s32 result[4][8]; /* last is final result */
	u8 i, final_candidate, indexforchannel;
	boolean is_patha_ok, is_pathb_ok;
	s32 rege94, rege9c, regea4, regeac, regeb4, regebc, regec4, regecc, reg_tmp = 0;
	boolean is12simular, is13simular, is23simular;
	u32 IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
		REG_OFDM_0_XA_RX_IQ_IMBALANCE, REG_OFDM_0_XB_RX_IQ_IMBALANCE,
		REG_OFDM_0_ECCA_THRESHOLD, REG_OFDM_0_AGC_RSSI_TABLE,
		REG_OFDM_0_XA_TX_IQ_IMBALANCE, REG_OFDM_0_XB_TX_IQ_IMBALANCE,
		REG_OFDM_0_XC_TX_AFE, REG_OFDM_0_XD_TX_AFE,
		REG_OFDM_0_RX_IQ_EXT_ANTA};
	boolean is_reload_iqk = false;

#if (DM_ODM_SUPPORT_TYPE & (ODM_CE | ODM_AP))
	if (is_recovery)
#else /* for ODM_WIN */
	if (is_recovery && !dm->is_in_hct_test) /* YJ,add for PowerTest,120405 */
#endif
	{
		RF_DBG(dm, DBG_RF_INIT, "[IQK]%s: Return due to is_recovery!\n",
		       __func__);
		_phy_reload_adda_registers8703b(dm, IQK_BB_REG_92C, dm->rf_calibrate_info.IQK_BB_backup_recover, 9);
		return;
	}

	if (*dm->mp_mode == false) {
#if MP_DRIVER != 1
		/* check if IQK had been done before!! */
		RF_DBG(dm, DBG_RF_IQK, "[IQK] 0xc80 = 0x%x\n",
		       cali_info->tx_iqc_8703b[idx_0xc80][VAL]);
		if (odm_set_iqc_by_rfpath_8703b(dm)) {
			RF_DBG(dm, DBG_RF_IQK,
			       "[IQK]IQK value is reloaded!!!\n");
			is_reload_iqk = true;
		}
		if (is_reload_iqk)
			return;
#endif
	}
	/*Check & wait if BT is doing IQK*/
	if (*(dm->mp_mode) == false)
		_phy_check_coex_status_8703b(dm, true);

	/* IQK start!!!!!!!!!! */
	RF_DBG(dm, DBG_RF_IQK, "[IQK]IQK:Start!!!\n");
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
		_phy_iq_calibrate_8703b(dm, result, i);
		if (i == 1) {
			is12simular = phy_simularity_compare_8703b(dm, result, 0, 1, true);
			if (is12simular) {
				final_candidate = 0;
				RF_DBG(dm, DBG_RF_IQK,
				       "[IQK]IQK: is12simular final_candidate is %x\n",
				       final_candidate);
				break;
			}
		}

		if (i == 2) {
			is13simular = phy_simularity_compare_8703b(dm, result, 0, 2, true);
			if (is13simular) {
				final_candidate = 0;
				RF_DBG(dm, DBG_RF_IQK,
				       "[IQK]IQK: is13simular final_candidate is %x\n",
				       final_candidate);

				break;
			}
			is23simular = phy_simularity_compare_8703b(dm, result, 1, 2, true);
			if (is23simular) {
				final_candidate = 1;
				RF_DBG(dm, DBG_RF_IQK,
				       "[IQK]IQK: is23simular final_candidate is %x\n",
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
	/*	RT_TRACE(COMP_INIT,DBG_LOUD,("Release Mutex in IQCalibrate\n"));*/

	for (i = 0; i < 4; i++) {
		rege94 = result[i][0];
		rege9c = result[i][1];
		regea4 = result[i][2];
		regeac = result[i][3];
		regeb4 = result[i][4];
		regebc = result[i][5];
		regec4 = result[i][6];
		regecc = result[i][7];
		RF_DBG(dm, DBG_RF_IQK,
		       "[IQK]IQK: rege94=%x rege9c=%x regea4=%x regeac=%x regeb4=%x regebc=%x regec4=%x regecc=%x\n ",
		       rege94, rege9c, regea4, regeac, regeb4, regebc, regec4,
		       regecc);
	}

	if (final_candidate != 0xff) {
		dm->rf_calibrate_info.rege94 = rege94 = result[final_candidate][0];
		dm->rf_calibrate_info.rege9c = rege9c = result[final_candidate][1];
		regea4 = result[final_candidate][2];
		regeac = result[final_candidate][3];
		dm->rf_calibrate_info.regeb4 = regeb4 = result[final_candidate][4];
		dm->rf_calibrate_info.regebc = regebc = result[final_candidate][5];
		regec4 = result[final_candidate][6];
		regecc = result[final_candidate][7];
		RF_DBG(dm, DBG_RF_IQK, "[IQK]IQK: final_candidate is %x\n",
		       final_candidate);
		RF_DBG(dm, DBG_RF_IQK,
		       "[IQK]IQK: rege94=%x rege9c=%x regea4=%x regeac=%x regeb4=%x regebc=%x regec4=%x regecc=%x\n ",
		       rege94, rege9c, regea4, regeac, regeb4, regebc, regec4,
		       regecc);
		is_patha_ok = is_pathb_ok = true;
	} else {
		RF_DBG(dm, DBG_RF_IQK, "[IQK]IQK: FAIL use default value\n");
		dm->rf_calibrate_info.rege94 = dm->rf_calibrate_info.regeb4 = 0x100; /* X default value */
		dm->rf_calibrate_info.rege9c = dm->rf_calibrate_info.regebc = 0x0; /* Y default value */
	}

	/* fill IQK matrix */
	if (rege94 != 0)
		_phy_path_a_fill_iqk_matrix8703b(dm, is_patha_ok, result, final_candidate, (regea4 == 0));
/*	if (regeb4 != 0)
	 *		_phy_path_b_fill_iqk_matrix8703b(adapter, is_pathb_ok, result, final_candidate, (regec4 == 0)); */

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
	RF_DBG(dm, DBG_RF_IQK, "[IQK]\nIQK OK indexforchannel %d.\n",
	       indexforchannel);
	_phy_save_adda_registers8703b(dm, IQK_BB_REG_92C, dm->rf_calibrate_info.IQK_BB_backup_recover, IQK_BB_REG_NUM);
	/* fill IQK register */
	odm_set_iqc_by_rfpath_8703b(dm);
	if (*dm->mp_mode == false) {
		_phy_check_coex_status_8703b(dm, false);
	}
	RF_DBG(dm, DBG_RF_IQK, "[IQK]IQK finished\n");
}

void phy_lc_calibrate_8703b(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	_phy_lc_calibrate_8703b(dm, false);
}

void _phy_set_rf_path_switch_8703b(
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

	if (is_main) { /*Set WIFI S1*/
		odm_set_bb_reg(dm, R_0x7c4, MASKLWORD, 0x7703);
		odm_set_bb_reg(dm, R_0x7c0, MASKDWORD, 0xC00F0038);
	} else { /*Set BT S0*/
		odm_set_bb_reg(dm, R_0x7c4, MASKLWORD, 0xCC03);
		odm_set_bb_reg(dm, R_0x7c0, MASKDWORD, 0xC00F0038);
	}
}

void phy_set_rf_path_switch_8703b(
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
	_phy_set_rf_path_switch_8703b(dm, is_main, true);
#else
	_phy_set_rf_path_switch_8703b(adapter, is_main, true);
#endif
#endif
}

/*return value true => WIFI(S1); false => BT(S0)*/
boolean _phy_query_rf_path_switch_8703b(
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

	if (odm_get_bb_reg(dm, R_0x7c4, MASKLWORD) == 0x7703)
		return true;
	else
		return false;
}

/*return value true => WIFI(S1); false => BT(S0)*/
boolean phy_query_rf_path_switch_8703b(
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
	return _phy_query_rf_path_switch_8703b(adapter, false);
#else
	return _phy_query_rf_path_switch_8703b(dm, false);
#endif
}
#endif
