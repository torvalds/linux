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

#if RT_PLATFORM==PLATFORM_MACOSX
#include "phydm_precomp.h"
#else
#include "../phydm_precomp.h"
#endif


/*---------------------------Define Local Constant---------------------------*/
/* 2010/04/25 MH Define the max tx power tracking tx agc power. */
#define		ODM_TXPWRTRACK_MAX_IDX8723B		6

/* MACRO definition for p_rf_calibrate_info->tx_iqc_8723b[0] */
#define 	PATH_S0                         1 /* RF_PATH_B */
#define     idx_0xc94                       0
#define     idx_0xc80                       1
#define     idx_0xc4c                       2
#define     idx_0xc14                       0
#define     idx_0xca0                       1
#define     KEY                             0
#define     VAL                             1

/* MACRO definition for p_rf_calibrate_info->tx_iqc_8723b[1] */
#define 	PATH_S1                         0 /* RF_PATH_A */
#define     idx_0xc9c                       0
#define     idx_0xc88                       1
#define     idx_0xc4c                       2
#define     idx_0xc1c                       0
#define     idx_0xc78                       1


/*---------------------------Define Local Constant---------------------------*/


/* 3============================================================
 * 3 Tx Power Tracking
 * 3============================================================ */

void halrf_rf_lna_setting_8723b(
	struct PHY_DM_STRUCT	*p_dm,
	enum phydm_lna_set type
)
{
		/*phydm_disable_lna*/
		if (type == phydm_lna_disable) {
			/*S0*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x1);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x30, 0xfffff, 0x18000);	/*select Rx mode*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x31, 0xfffff, 0x0001f);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x32, 0xfffff, 0xe6137);	/*disable LNA*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x0);
			/*S1*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xed, 0x00020, 0x1);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x43, 0xfffff, 0x3008d);	/*select Rx mode and disable LNA*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xed, 0x00020, 0x0);
		} else if (type == phydm_lna_enable) {
			/*phydm_enable_lna*/
			/*S0*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x1);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x30, 0xfffff, 0x18000);	/*select Rx mode*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x31, 0xfffff, 0x0001f);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x32, 0xfffff, 0xe6177);	/*disable LNA*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x80000, 0x0);
			/*S1*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xed, 0x00020, 0x1);
			odm_set_rf_reg(p_dm, RF_PATH_A, 0x43, 0xfffff, 0x300bd);	/*select Rx mode and disable LNA*/
			odm_set_rf_reg(p_dm, RF_PATH_A, 0xed, 0x00020, 0x0);
		}
}


void set_iqk_matrix_8723b(
	struct PHY_DM_STRUCT	*p_dm,
	u8		OFDM_index,
	s32		iqk_result_s1_x,
	s32		iqk_result_s1_y,
	s32		iqk_result_s0_x,
	s32		iqk_result_s0_y
)
{
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
	struct phydm_fat_struct			*p_dm_fat_table = &p_dm->dm_fat_table;
	s32			ele_A = 0, ele_D, ele_C = 0, value32;
	s32			ele_A_ext = 0, ele_C_ext = 0;
	u8			rf_path = RF_PATH_A;

	if (*(p_dm->p_mp_mode)) {
		/* MP driver => Check which antenna is using now */
		value32 = odm_get_bb_reg(p_dm, REG_S0_S1_PATH_SWITCH, MASKLWORD);
		if (value32 == 0x280 || value32 == 0x80)
			rf_path = RF_PATH_B;
	} else {
		/* If Dual Antenna && Antenna diversity change antenna to S0 (path B) => Change rf_path to pathB */
		/* If USB interface or efuse 0xc3 = 51(1-ant, path B) ==> swith to pathB */
		if (((*p_dm->p_is_1_antenna == true) && (*p_dm->p_rf_default_path == 1)) || (p_dm->support_interface == ODM_ITRF_USB)
		    || ((p_dm->support_ability & ODM_BB_ANT_DIV) && (p_dm_fat_table->rx_idle_ant  == AUX_ANT)))
			rf_path = RF_PATH_B;
	}


	if (OFDM_index >= OFDM_TABLE_SIZE)
		OFDM_index = OFDM_TABLE_SIZE - 1;
	else if (OFDM_index < 0)
		OFDM_index = 0;

	ele_D = (ofdm_swing_table_new[OFDM_index] & 0xFFC00000) >> 22;

	/* S1------------------------------------- */
	if ((iqk_result_s1_x & 0x00000200) != 0)	/* consider minus */
		iqk_result_s1_x = iqk_result_s1_x | 0xFFFFFC00;
	ele_A = ((iqk_result_s1_x * ele_D) >> 8) & 0x000003FF;
	ele_A_ext = ((iqk_result_s1_x * ele_D) >> 7) & 0x1;

	if ((iqk_result_s1_y & 0x00000200) != 0)
		iqk_result_s1_y = iqk_result_s1_y | 0xFFFFFC00;
	ele_C = ((iqk_result_s1_y * ele_D) >> 8) & 0x000003FF;
	ele_C_ext = ((iqk_result_s1_y * ele_D) >> 7) & 0x1;

	value32 = (ele_D << 22) | ((ele_C & 0x3F) << 16) | ele_A;
	p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc80][VAL] = value32;

	value32 = ((ele_C & 0x000003C0) >> 6) << 28;
	p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc94][VAL] =
		(p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc94][VAL] & (~(MASKH4BITS))) | value32;

	value32 = ((((iqk_result_s1_x * ele_D) >> 7) & 0x01) << 28) | (ele_A_ext << 31) | (ele_C_ext << 29) ;
	p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc4c][VAL] =
		(p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc4c][VAL] & (~(BIT(31) | BIT(29) | BIT(28)))) | value32;


	/* S0------------------------------------- */
	if (*(p_dm->p_mp_mode) || (p_dm->support_ability & ODM_BB_ANT_DIV) || ((*p_dm->p_is_1_antenna == true) && (*p_dm->p_rf_default_path == 1))
	    || (p_dm->support_interface == ODM_ITRF_USB) || (*p_dm->p_is_1_antenna == false)) {
		ele_A = 0;
		ele_C = 0;

		if ((iqk_result_s0_x & 0x00000200) != 0)	/* consider minus */
			iqk_result_s0_x = iqk_result_s0_x | 0xFFFFFC00;
		ele_A = ((iqk_result_s0_x * ele_D) >> 8) & 0x000003FF;
		ele_A_ext = ((iqk_result_s0_x * ele_D) >> 7) & 0x1;

		if ((iqk_result_s0_y & 0x00000200) != 0)
			iqk_result_s0_y = iqk_result_s0_y | 0xFFFFFC00;
		ele_C = ((iqk_result_s0_y * ele_D) >> 8) & 0x000003FF;
		ele_C_ext = ((iqk_result_s0_y * ele_D) >> 7) & 0x1;

		value32 = (ele_D << 22) | ((ele_C & 0x3F) << 16) | ele_A;
		p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc80][VAL] = value32;

		value32 = ((ele_C & 0x000003C0) >> 6) << 28;
		p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc94][VAL] =
			(p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc94][VAL] & (~(MASKH4BITS))) | value32;

		value32 = ((((iqk_result_s0_x * ele_D) >> 7) & 0x01) << 28) | (ele_A_ext << 31) | (ele_C_ext << 29) ;
		p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc4c][VAL] =
			(p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc4c][VAL] & (~(BIT(31) | BIT(29) | BIT(28)))) | value32;
	}

	/* Set register by path------------------------------ */
	if ((((iqk_result_s1_x != 0) && (rf_path == RF_PATH_A)) ||
	     ((iqk_result_s0_x != 0) && (rf_path == RF_PATH_B))) &&
	    (*(p_dm->p_band_type) == ODM_BAND_2_4G)) {
		switch (rf_path) {
		case RF_PATH_A:
			/* wirte new elements A, C, D to regC80 and regC94, element B is always 0 */
			/* for 8723B S1 */
			odm_set_bb_reg(p_dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD, p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc80][VAL]);
			odm_set_bb_reg(p_dm, REG_OFDM_0_XC_TX_AFE, MASKH4BITS, ((p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc94][VAL]) >> 28));

			value32 = odm_get_bb_reg(p_dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD);
			value32 = (value32 & (~(BIT(31) | BIT(29) | BIT(28)))) | (p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc4c][VAL] & (BIT(31) | BIT(29) | BIT(28)));
			odm_set_bb_reg(p_dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD, value32);
			break;
		case RF_PATH_B:
			/* wirte new elements A, C, D to regC80 and regC94, element B is always 0 */
			/* for 8723B S0 */
			odm_set_bb_reg(p_dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD, p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc80][VAL]);
			odm_set_bb_reg(p_dm, REG_OFDM_0_XC_TX_AFE, MASKH4BITS, ((p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc94][VAL]) >> 28));

			value32 = odm_get_bb_reg(p_dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD);
			value32 = (value32 & (~(BIT(31) | BIT(29) | BIT(28)))) | (p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc4c][VAL] & (BIT(31) | BIT(29) | BIT(28)));
			odm_set_bb_reg(p_dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD, value32);
			break;
		default:
			break;
		}
	} else {
		/* default IQK value (if current Antenna has default IQK value) */
		odm_set_bb_reg(p_dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD, ofdm_swing_table_new[OFDM_index]);
		odm_set_bb_reg(p_dm, REG_OFDM_0_XC_TX_AFE, MASKH4BITS, 0x00);

		value32 = odm_get_bb_reg(p_dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD);
		value32 = (value32 & (~(BIT(31) | BIT(29) | BIT(28))));
		odm_set_bb_reg(p_dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD, value32);
	}
}


void
set_cck_filter_coefficient(
	struct PHY_DM_STRUCT	*p_dm,
	u8		cck_swing_index
)
{
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
	if (!p_rf_calibrate_info->is_cck_in_ch14) {
		odm_write_1byte(p_dm, 0xa22, cck_swing_table_ch1_ch13_new[cck_swing_index][0]);
		odm_write_1byte(p_dm, 0xa23, cck_swing_table_ch1_ch13_new[cck_swing_index][1]);
		odm_write_1byte(p_dm, 0xa24, cck_swing_table_ch1_ch13_new[cck_swing_index][2]);
		odm_write_1byte(p_dm, 0xa25, cck_swing_table_ch1_ch13_new[cck_swing_index][3]);
		odm_write_1byte(p_dm, 0xa26, cck_swing_table_ch1_ch13_new[cck_swing_index][4]);
		odm_write_1byte(p_dm, 0xa27, cck_swing_table_ch1_ch13_new[cck_swing_index][5]);
		odm_write_1byte(p_dm, 0xa28, cck_swing_table_ch1_ch13_new[cck_swing_index][6]);
		odm_write_1byte(p_dm, 0xa29, cck_swing_table_ch1_ch13_new[cck_swing_index][7]);
	} else {
		odm_write_1byte(p_dm, 0xa22, cck_swing_table_ch14_new[cck_swing_index][0]);
		odm_write_1byte(p_dm, 0xa23, cck_swing_table_ch14_new[cck_swing_index][1]);
		odm_write_1byte(p_dm, 0xa24, cck_swing_table_ch14_new[cck_swing_index][2]);
		odm_write_1byte(p_dm, 0xa25, cck_swing_table_ch14_new[cck_swing_index][3]);
		odm_write_1byte(p_dm, 0xa26, cck_swing_table_ch14_new[cck_swing_index][4]);
		odm_write_1byte(p_dm, 0xa27, cck_swing_table_ch14_new[cck_swing_index][5]);
		odm_write_1byte(p_dm, 0xa28, cck_swing_table_ch14_new[cck_swing_index][6]);
		odm_write_1byte(p_dm, 0xa29, cck_swing_table_ch14_new[cck_swing_index][7]);
	}
}

void do_iqk_8723b(
	void		*p_dm_void,
	u8		delta_thermal_index,
	u8		thermal_value,
	u8		threshold
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	odm_reset_iqk_result(p_dm);
	p_dm->rf_calibrate_info.thermal_value_iqk = thermal_value;
	halrf_iqk_trigger(p_dm, false);
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
void
odm_tx_pwr_track_set_pwr_8723b(
	void *p_dm_void,
	enum pwrtrack_method	method,
	u8				rf_path,
	u8				channel_mapped_index
)
{
	struct PHY_DM_STRUCT *p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER *adapter = p_dm->adapter;
	u8		pwr_tracking_limit_ofdm = 34; /* +0dB */
	u8	    pwr_tracking_limit_cck = 28;  /* -2dB */
	u8		tx_rate = 0xFF;
	u8		final_ofdm_swing_index = 0;
	u8		final_cck_swing_index = 0;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);

	if (*(p_dm->p_mp_mode) == true) {
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
		u16	rate	 = *(p_dm->p_forced_data_rate);

		if (!rate) { /*auto rate*/
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
			tx_rate = adapter->HalFunc.GetHwRateFromMRateHandler(p_dm->tx_rate);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
			tx_rate = hw_rate_to_m_rate(p_dm->tx_rate);
#endif
		} else   /*force rate*/
			tx_rate = (u8)rate;
	}

	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("Power Tracking tx_rate=0x%X\n", tx_rate));
	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("===>ODM_TxPwrTrackSetPwr8723B\n"));

	if (tx_rate != 0xFF) {
		/* 2 CCK */
		if (((tx_rate >= MGN_1M) && (tx_rate <= MGN_5_5M)) || (tx_rate == MGN_11M))
			pwr_tracking_limit_cck = 28;  /* -2dB */
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
			pwr_tracking_limit_ofdm =  p_rf_calibrate_info->default_ofdm_index;   /* Default OFDM index = 30 */
	}
	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("tx_rate=0x%x, pwr_tracking_limit=%d\n", tx_rate, pwr_tracking_limit_ofdm));

	if (method == TXAGC) {
		u32	pwr = 0, tx_agc = 0;

		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("odm_TxPwrTrackSetPwr8723B CH=%d\n", *(p_dm->p_channel)));

		p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path] = p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path];

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (MP_DRIVER != 1)
		p_rf_calibrate_info->modify_tx_agc_flag_path_a = true;
		p_rf_calibrate_info->modify_tx_agc_flag_path_a_cck = true;

		odm_set_tx_power_index_by_rate_section(p_dm, (enum rf_path)rf_path, *p_dm->p_channel, CCK);
		odm_set_tx_power_index_by_rate_section(p_dm, (enum rf_path)rf_path, *p_dm->p_channel, OFDM);
		odm_set_tx_power_index_by_rate_section(p_dm, (enum rf_path)rf_path, *p_dm->p_channel, HT_MCS0_MCS7);
#else
		pwr = odm_get_bb_reg(p_dm, REG_TX_AGC_A_RATE18_06, 0xFF);
		pwr += p_rf_calibrate_info->power_index_offset[rf_path];
		odm_set_bb_reg(p_dm, REG_TX_AGC_A_CCK_1_MCS32, MASKBYTE1, pwr);
		tx_agc = (pwr << 16) | (pwr << 8) | (pwr);
		odm_set_bb_reg(p_dm, REG_TX_AGC_B_CCK_11_A_CCK_2_11, 0xffffff00, tx_agc);
		RT_DISP(FPHY, PHY_TXPWR, ("ODM_TxPwrTrackSetPwr8723B: CCK Tx-rf(A) Power = 0x%x\n", tx_agc));

		pwr = odm_get_bb_reg(p_dm, REG_TX_AGC_A_RATE18_06, 0xFF);
		pwr += (p_rf_calibrate_info->bb_swing_idx_ofdm[rf_path] - p_rf_calibrate_info->bb_swing_idx_ofdm_base[rf_path]);
		tx_agc |= ((pwr << 24) | (pwr << 16) | (pwr << 8) | pwr);
		odm_set_bb_reg(p_dm, REG_TX_AGC_A_RATE18_06, MASKDWORD, tx_agc);
		odm_set_bb_reg(p_dm, REG_TX_AGC_A_RATE54_24, MASKDWORD, tx_agc);
		odm_set_bb_reg(p_dm, REG_TX_AGC_A_MCS03_MCS00, MASKDWORD, tx_agc);
		odm_set_bb_reg(p_dm, REG_TX_AGC_A_MCS07_MCS04, MASKDWORD, tx_agc);
		odm_set_bb_reg(p_dm, REG_TX_AGC_A_MCS11_MCS08, MASKDWORD, tx_agc);
		odm_set_bb_reg(p_dm, REG_TX_AGC_A_MCS15_MCS12, MASKDWORD, tx_agc);
		RT_DISP(FPHY, PHY_TXPWR, ("ODM_TxPwrTrackSetPwr8723B: OFDM Tx-rf(A) Power = 0x%x\n", tx_agc));
#endif
#endif
	} else if (method == BBSWING) {
		final_ofdm_swing_index = p_rf_calibrate_info->default_ofdm_index + p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path];
		final_cck_swing_index = p_rf_calibrate_info->default_cck_index + p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path];

		/* Adjust BB swing by OFDM IQ matrix */
		if (final_ofdm_swing_index >= pwr_tracking_limit_ofdm)
			final_ofdm_swing_index = pwr_tracking_limit_ofdm;
		else if (final_ofdm_swing_index < 0)
			final_ofdm_swing_index = 0;

		if (final_cck_swing_index >= CCK_TABLE_SIZE)
			final_cck_swing_index = CCK_TABLE_SIZE - 1;
		else if (p_rf_calibrate_info->bb_swing_idx_cck < 0)
			final_cck_swing_index = 0;

		set_iqk_matrix_8723b(p_dm, final_ofdm_swing_index,
			p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
			p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][1],
			p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][4],
			p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][5]);

		set_cck_filter_coefficient(p_dm, final_cck_swing_index);

	} else if (method == MIX_MODE) {
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("p_rf_calibrate_info->default_ofdm_index=%d,  p_dm->DefaultCCKIndex=%d, p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path]=%d, rf_path = %d\n",
			p_rf_calibrate_info->default_ofdm_index, p_rf_calibrate_info->default_cck_index, p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path], rf_path));

		final_ofdm_swing_index = p_rf_calibrate_info->default_ofdm_index + p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path];
		final_cck_swing_index  = p_rf_calibrate_info->default_cck_index + p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path];

		if (final_ofdm_swing_index > pwr_tracking_limit_ofdm) {   /* BBSwing higher then Limit */
			p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path] = final_ofdm_swing_index - pwr_tracking_limit_ofdm;

			set_iqk_matrix_8723b(p_dm, pwr_tracking_limit_ofdm,
				p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
				p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][1],
				p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][4],
				p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][5]);

			p_rf_calibrate_info->modify_tx_agc_flag_path_a = true;
			odm_set_tx_power_index_by_rate_section(p_dm, (enum rf_path)rf_path, *p_dm->p_channel, OFDM);
			odm_set_tx_power_index_by_rate_section(p_dm, (enum rf_path)rf_path, *p_dm->p_channel, HT_MCS0_MCS7);

			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("******Path_A Over BBSwing Limit, pwr_tracking_limit = %d, Remnant tx_agc value = %d\n",
				pwr_tracking_limit_ofdm, p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path]));
		} else if (final_ofdm_swing_index < 0) {
			p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path] = final_ofdm_swing_index ;

			set_iqk_matrix_8723b(p_dm, 0,
				p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
				p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][1],
				p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][4],
				p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][5]);

			p_rf_calibrate_info->modify_tx_agc_flag_path_a = true;
			odm_set_tx_power_index_by_rate_section(p_dm, (enum rf_path)rf_path, *p_dm->p_channel, OFDM);
			odm_set_tx_power_index_by_rate_section(p_dm, (enum rf_path)rf_path, *p_dm->p_channel, HT_MCS0_MCS7);

			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("******Path_A Lower then BBSwing lower bound  0, Remnant tx_agc value = %d\n",
				p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path]));
		} else {
			set_iqk_matrix_8723b(p_dm, final_ofdm_swing_index,
				p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
				p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][1],
				p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][4],
				p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][5]);

			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("******Path_A Compensate with BBSwing, final_ofdm_swing_index = %d\n", final_ofdm_swing_index));

			if (p_rf_calibrate_info->modify_tx_agc_flag_path_a) { /* If tx_agc has changed, reset tx_agc again */
				p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path] = 0;
				odm_set_tx_power_index_by_rate_section(p_dm, (enum rf_path)rf_path, *p_dm->p_channel, OFDM);
				odm_set_tx_power_index_by_rate_section(p_dm, (enum rf_path)rf_path, *p_dm->p_channel, HT_MCS0_MCS7);
				p_rf_calibrate_info->modify_tx_agc_flag_path_a = false;

				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
					("******Path_A p_dm->Modify_TxAGC_Flag = false\n"));
			}
		}

		if (final_cck_swing_index > pwr_tracking_limit_cck) {
			p_rf_calibrate_info->remnant_cck_swing_idx = final_cck_swing_index - pwr_tracking_limit_cck;
			set_cck_filter_coefficient(p_dm, pwr_tracking_limit_cck);
			p_rf_calibrate_info->modify_tx_agc_flag_path_a_cck = true;
			odm_set_tx_power_index_by_rate_section(p_dm, (enum rf_path)rf_path, *p_dm->p_channel, CCK);

			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("******Path_A CCK Over Limit, pwr_tracking_limit_cck = %d, p_rf_calibrate_info->remnant_cck_swing_idx  = %d\n", pwr_tracking_limit_cck, p_rf_calibrate_info->remnant_cck_swing_idx));
		} else if (final_cck_swing_index < 0) { /* Lowest CCK index = 0 */
			p_rf_calibrate_info->remnant_cck_swing_idx = final_cck_swing_index;
			set_cck_filter_coefficient(p_dm, 0);
			p_rf_calibrate_info->modify_tx_agc_flag_path_a_cck = true;
			odm_set_tx_power_index_by_rate_section(p_dm, (enum rf_path)rf_path, *p_dm->p_channel, CCK);

			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("******Path_A CCK Under Limit, pwr_tracking_limit_cck = %d, p_rf_calibrate_info->remnant_cck_swing_idx  = %d\n", 0, p_rf_calibrate_info->remnant_cck_swing_idx));
		} else {
			set_cck_filter_coefficient(p_dm, final_cck_swing_index);

			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				("******Path_A CCK Compensate with BBSwing, final_cck_swing_index = %d\n", final_cck_swing_index));

			if (p_rf_calibrate_info->modify_tx_agc_flag_path_a_cck) { /* If tx_agc has changed, reset tx_agc again */
				p_rf_calibrate_info->remnant_cck_swing_idx = 0;
				odm_set_tx_power_index_by_rate_section(p_dm, (enum rf_path)rf_path, *p_dm->p_channel, CCK);
				p_rf_calibrate_info->modify_tx_agc_flag_path_a_cck = false;

				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
					("******Path_A p_dm->Modify_TxAGC_Flag_CCK = false\n"));
			}
		}
	} else {
		return; /* This method is not supported. */
	}
}

void
get_delta_swing_table_8723b(
	void		*p_dm_void,
	u8 **temperature_up_a,
	u8 **temperature_down_a,
	u8 **temperature_up_b,
	u8 **temperature_down_b
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER		*adapter		 = p_dm->adapter;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
	HAL_DATA_TYPE	*p_hal_data		 = GET_HAL_DATA(adapter);
	u8			tx_rate			= 0xFF;
	u8			channel		 = *p_dm->p_channel;

	if (*(p_dm->p_mp_mode) == true) {
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
		u16	rate	 = *(p_dm->p_forced_data_rate);

		if (!rate) { /*auto rate*/
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
			tx_rate = adapter->HalFunc.GetHwRateFromMRateHandler(p_dm->tx_rate);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
			tx_rate = hw_rate_to_m_rate(p_dm->tx_rate);
#endif
		} else   /*force rate*/
			tx_rate = (u8)rate;
	}

	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("Power Tracking tx_rate=0x%X\n", tx_rate));

	if (1 <= channel && channel <= 14) {
		if (IS_CCK_RATE(tx_rate)) {
			*temperature_up_a   = p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_p;
			*temperature_down_a = p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_n;
			*temperature_up_b   = p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_p;
			*temperature_down_b = p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_n;
		} else {
			*temperature_up_a   = p_rf_calibrate_info->delta_swing_table_idx_2ga_p;
			*temperature_down_a = p_rf_calibrate_info->delta_swing_table_idx_2ga_n;
			*temperature_up_b   = p_rf_calibrate_info->delta_swing_table_idx_2gb_p;
			*temperature_down_b = p_rf_calibrate_info->delta_swing_table_idx_2gb_n;
		}
	} else {
		*temperature_up_a   = (u8 *)delta_swing_table_idx_2ga_p_8188e;
		*temperature_down_a = (u8 *)delta_swing_table_idx_2ga_n_8188e;
		*temperature_up_b   = (u8 *)delta_swing_table_idx_2ga_p_8188e;
		*temperature_down_b = (u8 *)delta_swing_table_idx_2ga_n_8188e;
	}

	return;
}


void configure_txpower_track_8723b(
	struct _TXPWRTRACK_CFG	*p_config
)
{
	p_config->swing_table_size_cck = CCK_TABLE_SIZE;
	p_config->swing_table_size_ofdm = OFDM_TABLE_SIZE;
	p_config->threshold_iqk = IQK_THRESHOLD;
	p_config->average_thermal_num = AVG_THERMAL_NUM_92E;
	p_config->rf_path_count = MAX_PATH_NUM_8723B;
	p_config->thermal_reg_addr = RF_T_METER_88E;

	p_config->odm_tx_pwr_track_set_pwr = odm_tx_pwr_track_set_pwr_8723b;
	p_config->do_iqk = do_iqk_8723b;
	p_config->phy_lc_calibrate = halrf_lck_trigger;
	p_config->get_delta_swing_table = get_delta_swing_table_8723b;
}

/* 1 7.	IQK */
#define MAX_TOLERANCE		5
#define IQK_DELAY_TIME		1		/* ms */

u8			/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_path_a_iqk_8723b(
	struct PHY_DM_STRUCT		*p_dm
)
{
	u32 reg_eac, reg_e94, reg_e9c, path_sel_bb/*, reg_ea4*/;
	u8 result = 0x00;

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S1 TX IQK!\n"));
	/* save RF path */
	path_sel_bb = odm_get_bb_reg(p_dm, 0x948, MASKDWORD);

	/* 1 Tx IQK */
	/* IQK setting */
	odm_set_bb_reg(p_dm, REG_TX_IQK, MASKDWORD, 0x01007c00);
	odm_set_bb_reg(p_dm, REG_RX_IQK, MASKDWORD, 0x01004800);
	/* path-A IQK setting */
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	/*	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_A, MASKDWORD, 0x8214010a); */
	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_A, MASKDWORD, 0x821403ea);
	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_A, MASKDWORD, 0x28110000);
	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_B, MASKDWORD, 0x82110000);
	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_B, MASKDWORD, 0x28110000);

	/* LO calibration setting */
	odm_set_bb_reg(p_dm, REG_IQK_AGC_RSP, MASKDWORD, 0x00462911);

	/* leave IQK mode */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	/*	enable path A PA in TXIQK mode */
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_WE_LUT, 0x80000, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_RCK_OS, RFREGOFFSETMASK, 0x20000);
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_TXPA_G1, RFREGOFFSETMASK, 0x0003f);
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0xc7f87);

	/* enter IQK mode */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);
	/* RF switch to S1 */
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, 0x00000000);

	/* set GNT_BT=0, pause BT traffic */
	odm_set_bb_reg(p_dm, 0x764, BIT(12) | BIT(11), 0x1);
	/*	ODM_RT_TRACE(p_dm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x764 when IQK= 0x%x\n", odm_get_bb_reg(p_dm, 0x764, MASKDWORD))); */

	/* One shot, path A LOK & IQK */
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/* delay x ms */
	ODM_delay_ms(IQK_DELAY_TIME_8723B);

	/* set GNT_BT=1, enable BT traffic */
	odm_set_bb_reg(p_dm, 0x764, BIT(12) | BIT(11), 0x3);
	/*	ODM_RT_TRACE(p_dm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x764 after IQK= 0x%x\n", odm_get_bb_reg(p_dm, 0x764, MASKDWORD))); */

	/* reload RF path */
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, path_sel_bb);
	/* leave IQK mode */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	/* Check failed */
	reg_eac = odm_get_bb_reg(p_dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_e94 = odm_get_bb_reg(p_dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD);
	reg_e9c = odm_get_bb_reg(p_dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac = 0x%x\n", reg_eac));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe94 = 0x%x, 0xe9c = 0x%x\n", reg_e94, reg_e9c));
	/* monitor image power before & after IQK */
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe90(before IQK)= 0x%x, 0xe98(afer IQK) = 0x%x\n",
		odm_get_bb_reg(p_dm, 0xe90, MASKDWORD), odm_get_bb_reg(p_dm, 0xe98, MASKDWORD)));


	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))

		result |= 0x01;

	return result;

#if 0
	if (!(reg_eac & BIT(27)) &&		/* if Tx is OK, check whether Rx is OK */
	    (((reg_ea4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_eac & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;
	else
		RT_DISP(FINIT, INIT_IQK, ("path A Rx IQK fail!!\n"));
#endif
}

u8			/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_path_a_rx_iqk_8723b(
	struct PHY_DM_STRUCT		*p_dm
)
{
	u32 reg_eac, reg_e94, reg_e9c, reg_ea4, u4tmp, tmp, path_sel_bb;
	u8 result = 0x00;

	/* save RF path */
	path_sel_bb = odm_get_bb_reg(p_dm, 0x948, MASKDWORD);

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S1 RX IQK:Get TXIMR setting\n"));
	/* 1 Get TXIMR setting */

	/* IQK setting */
	odm_set_bb_reg(p_dm, REG_TX_IQK, MASKDWORD, 0x01007c00);
	odm_set_bb_reg(p_dm, REG_RX_IQK, MASKDWORD, 0x01004800);

	/* path-A IQK setting */
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_B, MASKDWORD, 0x38008c1c);

	/*	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_A, MASKDWORD, 0x82160c1f); */
	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_A, MASKDWORD, 0x82160ff0);
	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_A, MASKDWORD, 0x28110000);
	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_B, MASKDWORD, 0x82110000);
	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_B, MASKDWORD, 0x28110000);

	/* LO calibration setting */
	odm_set_bb_reg(p_dm, REG_IQK_AGC_RSP, MASKDWORD, 0x0046a911);

	/* leave IQK mode */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	/* modify RXIQK mode table */
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_WE_LUT, 0x80000, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_RCK_OS, RFREGOFFSETMASK, 0x30000);
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_TXPA_G1, RFREGOFFSETMASK, 0x0001f);
	/* S1 IQK PA off
	*	odm_set_rf_reg(p_dm, RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0xf7fb7); */
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0xf7df7);
	/* S0 IQK PA off */
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xed, 0x20, 0x1);
	/*	odm_set_rf_reg(p_dm, RF_PATH_A, 0x43, RFREGOFFSETMASK, 0x60fed); */
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x43, RFREGOFFSETMASK, 0x60efd);

	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);

	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, 0x00000000);
	/* set GNT_BT=0, pause BT traffic */
	odm_set_bb_reg(p_dm, 0x764, BIT(12) | BIT(11), 0x1);

	/* One shot, path A LOK & IQK */
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/* delay x ms */
	ODM_delay_ms(IQK_DELAY_TIME_8723B);

	/* set GNT_BT=1, enable BT traffic */
	odm_set_bb_reg(p_dm, 0x764, BIT(12) | BIT(11), 0x3);

	/* reload RF path */
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, path_sel_bb);
	/* leave IQK mode */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	/* Check failed */
	reg_eac = odm_get_bb_reg(p_dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_e94 = odm_get_bb_reg(p_dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD);
	reg_e9c = odm_get_bb_reg(p_dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac = 0x%x\n", reg_eac));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe94 = 0x%x, 0xe9c = 0x%x\n", reg_e94, reg_e9c));
	/* monitor image power before & after IQK */
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe90(before IQK)= 0x%x, 0xe98(afer IQK) = 0x%x\n",
		odm_get_bb_reg(p_dm, 0xe90, MASKDWORD), odm_get_bb_reg(p_dm, 0xe98, MASKDWORD)));

	/* Allen 20141201 */
	tmp = (reg_e9c & 0x03FF0000) >> 16;
	if ((tmp & 0x200) > 0)
		tmp = 0x400 - tmp;

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42) &&
	    (((reg_e94 & 0x03FF0000) >> 16) < 0x11a) &&
	    (((reg_e94 & 0x03FF0000) >> 16) > 0xe6) &&
	    (tmp < 0x1a))

		result |= 0x01;
	else							/* if Tx not OK, ignore Rx */
		return result;



	u4tmp = 0x80007C00 | (reg_e94 & 0x3FF0000)  | ((reg_e9c & 0x3FF0000) >> 16);
	odm_set_bb_reg(p_dm, REG_TX_IQK, MASKDWORD, u4tmp);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe40 = 0x%x u4tmp = 0x%x\n", odm_get_bb_reg(p_dm, REG_TX_IQK, MASKDWORD), u4tmp));

	/* 1 RX IQK */
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S1 RX IQK\n"));

	/* IQK setting */
	odm_set_bb_reg(p_dm, REG_RX_IQK, MASKDWORD, 0x01004800);

	/* path-A IQK setting */
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_B, MASKDWORD, 0x38008c1c);

	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_A, MASKDWORD, 0x82110000);
	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_A, MASKDWORD, 0x2816081f);
	/*	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_A, MASKDWORD, 0x2816001f); */
	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_B, MASKDWORD, 0x82110000);
	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_B, MASKDWORD, 0x28110000);

	/* LO calibration setting */
	odm_set_bb_reg(p_dm, REG_IQK_AGC_RSP, MASKDWORD, 0x0046a8d1);


	/* modify RXIQK mode table */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_WE_LUT, 0x80000, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_RCK_OS, RFREGOFFSETMASK, 0x30000);
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_TXPA_G1, RFREGOFFSETMASK, 0x0001f);
	/* S1 PA off */
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0xf7d77);

	/* S1 PA, PAD setting */
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xdf, RFREGOFFSETMASK, 0xf80);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x55, RFREGOFFSETMASK, 0x40207);

	/* enter IQK mode */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);

	/* switch to S1 */
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, 0x00000000);

	/* set GNT_BT=0, pause BT traffic */
	odm_set_bb_reg(p_dm, 0x764, BIT(12) | BIT(11), 0x1);

	/* One shot, path A LOK & IQK */
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/* delay x ms */
	ODM_delay_ms(IQK_DELAY_TIME_8723B);

	/* set GNT_BT=1, enable BT traffic */
	odm_set_bb_reg(p_dm, 0x764, BIT(12) | BIT(11), 0x3);

	/* reload RF path */
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, path_sel_bb);
	/* leave IQK mode */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	/*	PA/PAD controlled by 0x0 */
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xdf, RFREGOFFSETMASK, 0x780);

	/* Check failed */
	reg_eac = odm_get_bb_reg(p_dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_ea4 = odm_get_bb_reg(p_dm, REG_RX_POWER_BEFORE_IQK_A_2, MASKDWORD);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac = 0x%x\n", reg_eac));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xea4 = 0x%x, 0xeac = 0x%x\n", reg_ea4, reg_eac));
	/* monitor image power before & after IQK */
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xea0(before IQK)= 0x%x, 0xea8(afer IQK) = 0x%x\n",
		odm_get_bb_reg(p_dm, 0xea0, MASKDWORD), odm_get_bb_reg(p_dm, 0xea8, MASKDWORD)));

	/* Allen 20131125 */
	tmp = (reg_eac & 0x03FF0000) >> 16;
	if ((tmp & 0x200) > 0)
		tmp = 0x400 - tmp;

	if (!(reg_eac & BIT(27)) &&		/* if Tx is OK, check whether Rx is OK */
	    (((reg_ea4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_eac & 0x03FF0000) >> 16) != 0x36) &&
	    (((reg_ea4 & 0x03FF0000) >> 16) < 0x110) &&
	    (((reg_ea4 & 0x03FF0000) >> 16) > 0xf0) &&
	    (tmp < 0xf))
		result |= 0x02;
	else							/* if Tx not OK, ignore Rx */
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path A Rx IQK fail!!\n"));

	return result;
}

u8				/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_path_b_iqk_8723b(
	struct PHY_DM_STRUCT		*p_dm
)
{
	u32 reg_eac, reg_e94, reg_e9c, path_sel_bb/*, reg_ec4, reg_ecc*/;
	u8	result = 0x00;

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S0 TX IQK!\n"));

	/* save RF path */
	path_sel_bb = odm_get_bb_reg(p_dm, 0x948, MASKDWORD);

	/* 1 Tx IQK */
	/* IQK setting */
	odm_set_bb_reg(p_dm, REG_TX_IQK, MASKDWORD, 0x01007c00);
	odm_set_bb_reg(p_dm, REG_RX_IQK, MASKDWORD, 0x01004800);
	/* path-A IQK setting */
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_B, MASKDWORD, 0x38008c1c);

	/*	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_A, MASKDWORD, 0x82140114); */
	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_A, MASKDWORD, 0x821403ea);
	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_A, MASKDWORD, 0x28110000);
	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_B, MASKDWORD, 0x82110000);
	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_B, MASKDWORD, 0x28110000);

	/* LO calibration setting */
	odm_set_bb_reg(p_dm, REG_IQK_AGC_RSP, MASKDWORD, 0x00462911);

	/* leave IQK mode */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	/*	enable path B PA in TXIQK mode */
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xed, 0x20, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x43, RFREGOFFSETMASK, 0x40fc1);

	/* enter IQK mode */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);

	/* RF switch to S0 */
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, 0x00000280);

	/* set GNT_BT=0, pause BT traffic */
	odm_set_bb_reg(p_dm, 0x764, BIT(12) | BIT(11), 0x1);

	/* One shot, path B LOK & IQK */
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/* delay x ms */
	ODM_delay_ms(IQK_DELAY_TIME_8723B);

	/* set GNT_BT=1, enable BT traffic */
	odm_set_bb_reg(p_dm, 0x764, BIT(12) | BIT(11), 0x3);

	/* reload RF path */
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, path_sel_bb);
	/* leave IQK mode */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	/*	ODM_RT_TRACE(p_dm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("0x948 = 0x%x\n", odm_get_bb_reg(p_dm, 0x948, MASKDWORD))); */

	/* Check failed */
	reg_eac = odm_get_bb_reg(p_dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_e94 = odm_get_bb_reg(p_dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD);
	reg_e9c = odm_get_bb_reg(p_dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac = 0x%x\n", reg_eac));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe94 = 0x%x, 0xe9c = 0x%x\n", reg_e94, reg_e9c));
	/* monitor image power before & after IQK */
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe90(before IQK)= 0x%x, 0xe98(afer IQK) = 0x%x\n",
		odm_get_bb_reg(p_dm, 0xe90, MASKDWORD), odm_get_bb_reg(p_dm, 0xe98, MASKDWORD)));

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;

	return result;

#if 0
	if (!(reg_eac & BIT(30)) &&
	    (((reg_ec4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_ecc & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;
	else
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path B Rx IQK fail!!\n"));

#endif
}



u8			/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_path_b_rx_iqk_8723b(
	struct PHY_DM_STRUCT		*p_dm
)
{
	u32 reg_e94, reg_e9c, reg_ea4, reg_eac, u4tmp, tmp, path_sel_bb;
	u8 result = 0x00;

	/* save RF path */
	path_sel_bb = odm_get_bb_reg(p_dm, 0x948, MASKDWORD);


	/* 1 Get TXIMR setting */
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S0 RX IQK:Get TXIMR setting!\n"));

	/* IQK setting */
	odm_set_bb_reg(p_dm, REG_TX_IQK, MASKDWORD, 0x01007c00);
	odm_set_bb_reg(p_dm, REG_RX_IQK, MASKDWORD, 0x01004800);

	/* path-B IQK setting */
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_B, MASKDWORD, 0x38008c1c);

	/*	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_A, MASKDWORD, 0x82160c1f ); */
	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_A, MASKDWORD, 0x82160ff0);
	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_A, MASKDWORD, 0x28110000);
	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_B, MASKDWORD, 0x82110000);
	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_B, MASKDWORD, 0x28110000);

	/* LO calibration setting */
	odm_set_bb_reg(p_dm, REG_IQK_AGC_RSP, MASKDWORD, 0x0046a911);

	/* leave IQK mode */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	/* modify RXIQK mode table */
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_WE_LUT, 0x80000, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_RCK_OS, RFREGOFFSETMASK, 0x30000);
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_TXPA_G1, RFREGOFFSETMASK, 0x0001f);
	/* S1 IQK PA off
	*	odm_set_rf_reg(p_dm, RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0xf7fb7 ); */
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0xf7df7);
	/* S0 IQK PA off */
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xed, 0x20, 0x1);
	/*	odm_set_rf_reg(p_dm, RF_PATH_A, 0x43, RFREGOFFSETMASK, 0x60fed ); */
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x43, RFREGOFFSETMASK, 0x60efd);

	/* enter IQK mode */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);

	/* RF switch to S0 */
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, 0x00000280);

	/* set GNT_BT=0, pause BT traffic */
	odm_set_bb_reg(p_dm, 0x764, BIT(12) | BIT(11), 0x1);


	/* One shot, path B TXIQK @ RXIQK */
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/* delay x ms */
	ODM_delay_ms(IQK_DELAY_TIME_8723B);

	/* set GNT_BT=1, enable BT traffic */
	odm_set_bb_reg(p_dm, 0x764, BIT(12) | BIT(11), 0x3);

	/* reload RF path */
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, path_sel_bb);
	/* leave IQK mode */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	/* Check failed */
	reg_eac = odm_get_bb_reg(p_dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_e94 = odm_get_bb_reg(p_dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD);
	reg_e9c = odm_get_bb_reg(p_dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac = 0x%x\n", reg_eac));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe94 = 0x%x, 0xe9c = 0x%x\n", reg_e94, reg_e9c));
	/* monitor image power before & after IQK */
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe90(before IQK)= 0x%x, 0xe98(afer IQK) = 0x%x\n",
		odm_get_bb_reg(p_dm, 0xe90, MASKDWORD), odm_get_bb_reg(p_dm, 0xe98, MASKDWORD)));

	/* Allen 20131125 */
	tmp = (reg_e9c & 0x03FF0000) >> 16;

	if ((tmp & 0x200) > 0)
		tmp = 0x400 - tmp;

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42)  &&
	    (((reg_e94 & 0x03FF0000) >> 16) < 0x11a) &&
	    (((reg_e94 & 0x03FF0000) >> 16) > 0xe6) &&
	    (tmp < 0x1a))
		result |= 0x01;
	else							/* if Tx not OK, ignore Rx */
		return result;


	u4tmp = 0x80007C00 | (reg_e94 & 0x3FF0000)  | ((reg_e9c & 0x3FF0000) >> 16);
	odm_set_bb_reg(p_dm, REG_TX_IQK, MASKDWORD, u4tmp);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe40 = 0x%x u4tmp = 0x%x\n", odm_get_bb_reg(p_dm, REG_TX_IQK, MASKDWORD), u4tmp));

	/* 1 RX IQK */
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S0 RX IQK\n"));

	/* IQK setting */
	odm_set_bb_reg(p_dm, REG_RX_IQK, MASKDWORD, 0x01004800);

	/* path-B IQK setting */
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_B, MASKDWORD, 0x38008c1c);

	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_A, MASKDWORD, 0x82110000);
	/*	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_A, MASKDWORD, 0x281604c2); */
	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_A, MASKDWORD, 0x2816081f);
	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_B, MASKDWORD, 0x82110000);
	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_B, MASKDWORD, 0x28110000);

	/* LO calibration setting */
	odm_set_bb_reg(p_dm, REG_IQK_AGC_RSP, MASKDWORD, 0x0046a8d1);

	/* modify RXIQK mode table */
	/* <20121009, Kordan> RF mode = 3 */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_WE_LUT, 0x80000, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_RCK_OS, RFREGOFFSETMASK, 0x30000);
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_TXPA_G1, RFREGOFFSETMASK, 0x0001f);
	/* S1 PA off */
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0xf7d77);
	/* S0 PA off */
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xed, 0x20, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x43, RFREGOFFSETMASK, 0x60ebd);

	/* PA, PAD setting */
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xdd, RFREGOFFSETMASK, 0x5c);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x75, RFREGOFFSETMASK, 0xe1007);

	/* enter IQK mode */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);
	/* switch to S0 */
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, 0x00000280);
	/* set GNT_BT=0, pause BT traffic */
	odm_set_bb_reg(p_dm, 0x764, BIT(12) | BIT(11), 0x1);

	/* One shot, path B LOK & IQK */
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/* delay x ms */
	ODM_delay_ms(IQK_DELAY_TIME_8723B);

	/* set GNT_BT=1, enable BT traffic */
	odm_set_bb_reg(p_dm, 0x764, BIT(12) | BIT(11), 0x3);

	/* reload RF path */
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, path_sel_bb);
	/* leave IQK mode */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	/*	PA/PAD controlled by 0x0 */
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xdd, RFREGOFFSETMASK, 0x4c);

	/* Check failed */
	reg_eac = odm_get_bb_reg(p_dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_ea4 = odm_get_bb_reg(p_dm, REG_RX_POWER_BEFORE_IQK_A_2, MASKDWORD);;

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac = 0x%x\n", reg_eac));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xea4 = 0x%x, 0xeac = 0x%x\n", reg_ea4, reg_eac));
	/* monitor image power before & after IQK */
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xea0(before IQK)= 0x%x, 0xea8(afer IQK) = 0x%x\n",
		odm_get_bb_reg(p_dm, 0xea0, MASKDWORD), odm_get_bb_reg(p_dm, 0xea8, MASKDWORD)));

#if 0
	if (!(reg_eac & BIT(31)) &&
	    (((reg_eb4 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_ebc & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else							/* if Tx not OK, ignore Rx */
		return result;
#endif


	/* Allen 20131125 */
	tmp = (reg_eac & 0x03FF0000) >> 16;
	if ((tmp & 0x200) > 0)
		tmp = 0x400 - tmp;

	if (!(reg_eac & BIT(27)) &&		/* if Tx is OK, check whether Rx is OK */
	    (((reg_ea4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_eac & 0x03FF0000) >> 16) != 0x36) &&
	    (((reg_ea4 & 0x03FF0000) >> 16) < 0x110) &&
	    (((reg_ea4 & 0x03FF0000) >> 16) > 0xf0) &&
	    (tmp < 0xf))

		result |= 0x02;
	else
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S0 Rx IQK fail!!\n"));

	return result;
}


void
_phy_path_a_fill_iqk_matrix8723b(
	struct PHY_DM_STRUCT		*p_dm,
	boolean	is_iqk_ok,
	s32		result[][8],
	u8		final_candidate,
	boolean		is_tx_only
)
{
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);

	u32	oldval_0, X, TX0_A, reg, tmp0xc80, tmp0xc94, tmp0xc4c, tmp0xc14, tmp0xca0;
	s32	Y, TX0_C;

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]path A IQ Calibration %s !\n", (is_iqk_ok) ? "Success" : "Failed"));

	if (final_candidate == 0xFF)
		return;

	else if (is_iqk_ok) {

		oldval_0 = (odm_get_bb_reg(p_dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD) >> 22) & 0x3FF;

		X = result[final_candidate][0];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;
		TX0_A = (X * oldval_0) >> 8;
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]X = 0x%x, TX0_A = 0x%x, oldval_0 0x%x\n", X, TX0_A, oldval_0));
		tmp0xc80 = (odm_get_bb_reg(p_dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD) & 0xfffffc00) | (TX0_A & 0x3ff);
		tmp0xc4c = (((X * oldval_0 >> 7) & 0x1) << 31) | (odm_get_bb_reg(p_dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD) & 0x7fffffff);

		Y = result[final_candidate][1];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;

		/* 2 Tx IQC */
		TX0_C = (Y * oldval_0) >> 8;
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Y = 0x%x, TX = 0x%x\n", Y, TX0_C));

		tmp0xc94 = (((TX0_C & 0x3C0) >> 6) << 28) | (odm_get_bb_reg(p_dm, REG_OFDM_0_XC_TX_AFE, MASKDWORD) & 0x0fffffff);

		p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc94][KEY] = REG_OFDM_0_XC_TX_AFE;
		p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc94][VAL] = tmp0xc94;

		tmp0xc80 = (tmp0xc80 & 0xffc0ffff) | (TX0_C & 0x3F) << 16;

		p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc80][KEY] = REG_OFDM_0_XA_TX_IQ_IMBALANCE;
		p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc80][VAL] = tmp0xc80;

		tmp0xc4c = (tmp0xc4c & 0xdfffffff) | (((Y * oldval_0 >> 7) & 0x1) << 29);

		p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc4c][KEY] = REG_OFDM_0_ECCA_THRESHOLD;
		p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc4c][VAL] = tmp0xc4c;

		if (is_tx_only) {
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]_phy_path_a_fill_iqk_matrix8723b only Tx OK\n"));

			/* <20130226, Kordan> Saving RxIQC, otherwise not initialized. */
			p_rf_calibrate_info->rx_iqc_8723b[PATH_S1][idx_0xca0][KEY] = REG_OFDM_0_RX_IQ_EXT_ANTA;
			p_rf_calibrate_info->rx_iqc_8723b[PATH_S1][idx_0xca0][VAL] = 0xfffffff & odm_get_bb_reg(p_dm, REG_OFDM_0_RX_IQ_EXT_ANTA, MASKDWORD);
			p_rf_calibrate_info->rx_iqc_8723b[PATH_S1][idx_0xc14][KEY] = REG_OFDM_0_XA_RX_IQ_IMBALANCE;
			p_rf_calibrate_info->rx_iqc_8723b[PATH_S1][idx_0xc14][VAL] = 0x40000100;
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

		p_rf_calibrate_info->rx_iqc_8723b[PATH_S1][idx_0xc14][KEY] = REG_OFDM_0_XA_RX_IQ_IMBALANCE;
		p_rf_calibrate_info->rx_iqc_8723b[PATH_S1][idx_0xc14][VAL] = tmp0xc14;

		reg = (result[final_candidate][3] >> 6) & 0xF;
		tmp0xca0 = odm_get_bb_reg(p_dm, REG_OFDM_0_RX_IQ_EXT_ANTA, 0x0fffffff) | (reg << 28);

		p_rf_calibrate_info->rx_iqc_8723b[PATH_S1][idx_0xca0][KEY] = REG_OFDM_0_RX_IQ_EXT_ANTA;
		p_rf_calibrate_info->rx_iqc_8723b[PATH_S1][idx_0xca0][VAL] = tmp0xca0;
	}
}

void
_phy_path_b_fill_iqk_matrix8723b(
	struct PHY_DM_STRUCT		*p_dm,
	boolean	is_iqk_ok,
	s32		result[][8],
	u8		final_candidate,
	boolean		is_tx_only			/* do Tx only */
)
{
	u32	oldval_1, X, TX1_A, reg, tmp0xc80, tmp0xc94, tmp0xc4c, tmp0xc14, tmp0xca0;
	s32	Y, TX1_C;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]path B IQ Calibration %s !\n", (is_iqk_ok) ? "Success" : "Failed"));

	if (final_candidate == 0xFF)
		return;

	else if (is_iqk_ok) {
		oldval_1 = (odm_get_bb_reg(p_dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD) >> 22) & 0x3FF;


		X = result[final_candidate][4];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;
		TX1_A = (X * oldval_1) >> 8;
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]X = 0x%x, TX1_A = 0x%x\n", X, TX1_A));

		tmp0xc80 = (odm_get_bb_reg(p_dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD) & 0xfffffc00) | (TX1_A & 0x3ff);
		tmp0xc4c = (((X * oldval_1 >> 7) & 0x1) << 31) | (odm_get_bb_reg(p_dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD) & 0x7fffffff);

		Y = result[final_candidate][5];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;

		TX1_C = (Y * oldval_1) >> 8;
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Y = 0x%x, TX1_C = 0x%x\n", Y, TX1_C));

		/* 2 Tx IQC */

		tmp0xc94 = (((TX1_C & 0x3C0) >> 6) << 28) | (odm_get_bb_reg(p_dm, REG_OFDM_0_XC_TX_AFE, MASKDWORD) & 0x0fffffff);

		p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc94][KEY] = REG_OFDM_0_XC_TX_AFE;
		p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc94][VAL] = tmp0xc94;

		tmp0xc80 = (tmp0xc80 & 0xffc0ffff) | (TX1_C & 0x3F) << 16;
		p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc80][KEY] = REG_OFDM_0_XA_TX_IQ_IMBALANCE;
		p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc80][VAL] = tmp0xc80;

		tmp0xc4c = (tmp0xc4c & 0xdfffffff) | (((Y * oldval_1 >> 7) & 0x1) << 29);
		p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc4c][KEY] = REG_OFDM_0_ECCA_THRESHOLD;
		p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc4c][VAL] = tmp0xc4c;

		if (is_tx_only) {
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]_phy_path_b_fill_iqk_matrix8723b only Tx OK\n"));

			p_rf_calibrate_info->rx_iqc_8723b[PATH_S0][idx_0xc14][KEY] = REG_OFDM_0_XA_RX_IQ_IMBALANCE;
			p_rf_calibrate_info->rx_iqc_8723b[PATH_S0][idx_0xc14][VAL] = 0x40000100;
			p_rf_calibrate_info->rx_iqc_8723b[PATH_S0][idx_0xca0][KEY] = REG_OFDM_0_RX_IQ_EXT_ANTA;
			p_rf_calibrate_info->rx_iqc_8723b[PATH_S0][idx_0xca0][VAL] = 0x0fffffff & odm_get_bb_reg(p_dm, REG_OFDM_0_RX_IQ_EXT_ANTA, MASKDWORD);
			return;
		}

		/* 2 Rx IQC */
		reg = result[final_candidate][6];
		tmp0xc14 = (0x40000100 & 0xfffffc00) | reg;

		reg = result[final_candidate][7] & 0x3F;
		tmp0xc14 = (tmp0xc14 & 0xffff03ff) | (reg << 10);

		p_rf_calibrate_info->rx_iqc_8723b[PATH_S0][idx_0xc14][KEY] = REG_OFDM_0_XA_RX_IQ_IMBALANCE;
		p_rf_calibrate_info->rx_iqc_8723b[PATH_S0][idx_0xc14][VAL] = tmp0xc14;

		reg = (result[final_candidate][7] >> 6) & 0xF;
		tmp0xca0 = odm_get_bb_reg(p_dm, REG_OFDM_0_RX_IQ_EXT_ANTA, 0x0fffffff) | (reg << 28);

		p_rf_calibrate_info->rx_iqc_8723b[PATH_S0][idx_0xca0][KEY] = REG_OFDM_0_RX_IQ_EXT_ANTA;
		p_rf_calibrate_info->rx_iqc_8723b[PATH_S0][idx_0xca0][VAL] = tmp0xca0;
	}
}

/*
 * 2011/07/26 MH Add an API for testing IQK fail case.
 *
 * MP Already declare in odm.c */

boolean
odm_set_iqc_by_rfpath(
	struct PHY_DM_STRUCT		*p_dm,
	u32 rf_path
)
{

	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);

	if (rf_path) { /* S1: rf_path = 0, S0:rf_path = 1 */
		if ((p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc80][VAL] != 0x0) && (p_rf_calibrate_info->rx_iqc_8723b[PATH_S0][idx_0xc14][VAL] != 0x0)) {

			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]RF S0 IQC!!!\n"));
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xc80 = 0x%x!!!\n", p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc80][VAL]));

			/* S0 TX IQC */
			odm_set_bb_reg(p_dm, p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc94][KEY], MASKH4BITS, (p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc94][VAL] >> 28));
			odm_set_bb_reg(p_dm, p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc80][KEY], MASKDWORD, p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc80][VAL]);
			odm_set_bb_reg(p_dm, p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc4c][KEY], BIT(31), (p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc4c][VAL] >> 31));
			odm_set_bb_reg(p_dm, p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc4c][KEY], BIT(29), ((p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc4c][VAL] & BIT(29)) >> 29));

			/* S0 RX IQC */
			odm_set_bb_reg(p_dm, p_rf_calibrate_info->rx_iqc_8723b[PATH_S0][idx_0xc14][KEY], MASKDWORD, p_rf_calibrate_info->rx_iqc_8723b[PATH_S0][idx_0xc14][VAL]);
			odm_set_bb_reg(p_dm, p_rf_calibrate_info->rx_iqc_8723b[PATH_S0][idx_0xca0][KEY], MASKDWORD, p_rf_calibrate_info->rx_iqc_8723b[PATH_S0][idx_0xca0][VAL]);
			return true;
		} else {
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQC S0 vaule invalid!!!\n"));
			return false;
		}
	} else {
		if ((p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc80][VAL] != 0x0) && (p_rf_calibrate_info->rx_iqc_8723b[PATH_S1][idx_0xc14][VAL] != 0x0)) {
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]RF S1 IQC!!!\n"));
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xc80 = 0x%x!!!\n", p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc80][VAL]));

			/* S1 TX IQC */
			odm_set_bb_reg(p_dm, p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc94][KEY], MASKH4BITS, (p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc94][VAL] >> 28));
			odm_set_bb_reg(p_dm, p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc80][KEY], MASKDWORD, p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc80][VAL]);
			odm_set_bb_reg(p_dm, p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc4c][KEY], BIT(31), (p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc4c][VAL] >> 31));
			odm_set_bb_reg(p_dm, p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc4c][KEY], BIT(29), ((p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc4c][VAL] & BIT(29)) >> 29));


			/* S1 RX IQC */
			odm_set_bb_reg(p_dm, p_rf_calibrate_info->rx_iqc_8723b[PATH_S1][idx_0xc14][KEY], MASKDWORD, p_rf_calibrate_info->rx_iqc_8723b[PATH_S1][idx_0xc14][VAL]);
			odm_set_bb_reg(p_dm, p_rf_calibrate_info->rx_iqc_8723b[PATH_S1][idx_0xca0][KEY], MASKDWORD, p_rf_calibrate_info->rx_iqc_8723b[PATH_S1][idx_0xca0][VAL]);
			return true;
		} else {
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQC S1 vaule invalid!!!\n"));
			return false;
		}
	}
}

void
_phy_save_adda_registers8723b(
	struct PHY_DM_STRUCT		*p_dm,
	u32		*adda_reg,
	u32		*adda_backup,
	u32		register_num
)
{
	u32	i;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if (odm_check_power_status(p_dm) == false)
		return;
#endif

	/*	ODM_RT_TRACE(p_dm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Save ADDA parameters.\n")); */
	for (i = 0 ; i < register_num ; i++)
		adda_backup[i] = odm_get_bb_reg(p_dm, adda_reg[i], MASKDWORD);
}


void
_phy_save_mac_registers8723b(
	struct PHY_DM_STRUCT		*p_dm,
	u32		*mac_reg,
	u32		*mac_backup
)
{
	u32	i;
	/*	ODM_RT_TRACE(p_dm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Save MAC parameters.\n")); */
	for (i = 0 ; i < (IQK_MAC_REG_NUM - 1); i++)
		mac_backup[i] = odm_read_1byte(p_dm, mac_reg[i]);
	mac_backup[i] = odm_read_4byte(p_dm, mac_reg[i]);
}


void
_phy_reload_adda_registers8723b(
	struct PHY_DM_STRUCT		*p_dm,
	u32		*adda_reg,
	u32		*adda_backup,
	u32		regiester_num
)
{
	u32	i;

	/*	ODM_RT_TRACE(p_dm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Reload ADDA power saving parameters !\n")); */
	for (i = 0 ; i < regiester_num; i++)
		odm_set_bb_reg(p_dm, adda_reg[i], MASKDWORD, adda_backup[i]);
}

void
_phy_reload_mac_registers8723b(
	struct PHY_DM_STRUCT		*p_dm,
	u32		*mac_reg,
	u32		*mac_backup
)
{
	u32	i;

	/*	ODM_RT_TRACE(p_dm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Reload MAC parameters !\n")); */
	for (i = 0 ; i < (IQK_MAC_REG_NUM - 1); i++)
		odm_write_1byte(p_dm, mac_reg[i], (u8)mac_backup[i]);
	odm_write_4byte(p_dm, mac_reg[i], mac_backup[i]);
}


void
_phy_path_adda_on8723b(
	struct PHY_DM_STRUCT		*p_dm,
	u32		*adda_reg,
	boolean		is_path_a_on
)
{
	u32	path_on;
	u32	i;
	/*	ODM_RT_TRACE(p_dm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("ADDA ON.\n")); */

	path_on = is_path_a_on ? 0x01c00014 : 0x01c00014;
	if (true == *p_dm->p_is_1_antenna) {
		path_on = 0x01c00014;
		odm_set_bb_reg(p_dm, adda_reg[0], MASKDWORD, 0x01c00014);
	} else
		odm_set_bb_reg(p_dm, adda_reg[0], MASKDWORD, path_on);

	for (i = 1 ; i < IQK_ADDA_REG_NUM ; i++)
		odm_set_bb_reg(p_dm, adda_reg[i], MASKDWORD, path_on);

}

void
_phy_mac_setting_calibration8723b(
	struct PHY_DM_STRUCT		*p_dm,
	u32		*mac_reg,
	u32		*mac_backup
)
{
	u32	i = 0;

	/*	ODM_RT_TRACE(p_dm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("MAC settings for Calibration.\n")); */
	odm_write_1byte(p_dm, mac_reg[i], 0x3F);
	for (i = 1 ; i < (IQK_MAC_REG_NUM - 1); i++)
		odm_write_1byte(p_dm, mac_reg[i], (u8)(mac_backup[i] & (~BIT(3))));
	odm_write_1byte(p_dm, mac_reg[i], (u8)(mac_backup[i] & (~BIT(5))));
}

void
_phy_path_a_stand_by8723b(

	struct PHY_DM_STRUCT		*p_dm
)
{
	/*	ODM_RT_TRACE(p_dm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("path-A standby mode!\n")); */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	/* Allen */
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_AC, MASKDWORD, 0x10000);
	/* odm_set_bb_reg(p_dm, 0x840, MASKDWORD, 0x00010000);*/
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);
}


void
_phy_pi_mode_switch8723b(
	struct PHY_DM_STRUCT		*p_dm,
	boolean		pi_mode
)
{
	u32	mode;
	/*	ODM_RT_TRACE(p_dm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("BB Switch to %s mode!\n", (pi_mode ? "PI" : "SI"))); */
	mode = pi_mode ? 0x01000100 : 0x01000000;
	odm_set_bb_reg(p_dm, REG_FPGA0_XA_HSSI_PARAMETER1, MASKDWORD, mode);
	odm_set_bb_reg(p_dm, REG_FPGA0_XB_HSSI_PARAMETER1, MASKDWORD, mode);
}

boolean
phy_simularity_compare_8723b(
	struct PHY_DM_STRUCT		*p_dm,
	s32		result[][8],
	u8		 c1,
	u8		 c2
)
{
	u32		i, j, diff, simularity_bit_map, bound = 0;
	u8		final_candidate[2] = {0xFF, 0xFF};	/* for path A and path B */
	boolean		is_result = true;
	/* #if !(DM_ODM_SUPPORT_TYPE & ODM_AP) */
	/*	bool		is2T = IS_92C_SERIAL( p_hal_data->version_id);
	 * #else */
	boolean		is2T = true;
	/* #endif */

	s32 tmp1 = 0, tmp2 = 0;

	if (is2T)
		bound = 8;
	else
		bound = 4;

	/*	ODM_RT_TRACE(p_dm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("===> IQK:phy_simularity_compare_8192e c1 %d c2 %d!!!\n", c1, c2)); */


	simularity_bit_map = 0;

	for (i = 0; i < bound; i++) {

		if ((i == 1) || (i == 3) || (i == 5) || (i == 7)) {
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
			/*			ODM_RT_TRACE(p_dm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("IQK:differnece overflow %d index %d compare1 0x%x compare2 0x%x!!!\n",  diff, i, result[c1][i], result[c2][i])); */

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

	/*	ODM_RT_TRACE(p_dm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK:phy_simularity_compare_8192e simularity_bit_map   %x !!!\n", simularity_bit_map)); */

	if (simularity_bit_map == 0) {
		for (i = 0; i < (bound / 4); i++) {
			if (final_candidate[i] != 0xFF) {
				for (j = i * 4; j < (i + 1) * 4 - 2; j++)
					result[3][j] = result[final_candidate[i]][j];
				is_result = false;
			}
		}
		return is_result;
	} else {

		if (!(simularity_bit_map & 0x03)) {		/* path A TX OK */
			for (i = 0; i < 2; i++)
				result[3][i] = result[c1][i];
		}

		if (!(simularity_bit_map & 0x0c)) {		/* path A RX OK */
			for (i = 2; i < 4; i++)
				result[3][i] = result[c1][i];
		}

		if (!(simularity_bit_map & 0x30)) {	/* path B TX OK */
			for (i = 4; i < 6; i++)
				result[3][i] = result[c1][i];

		}

		if (!(simularity_bit_map & 0xc0)) {	/* path B RX OK */
			for (i = 6; i < 8; i++)
				result[3][i] = result[c1][i];
		}
		return false;
	}
}

void
_phy_check_coex_status_8723b(
	struct PHY_DM_STRUCT	*p_dm,
	boolean		beforek
)
{
	u8		u1b_tmp;
	u16		count = 0;
	u8		h2c_parameter;

#if MP_DRIVER != 1
	if (beforek) {
		/* Set H2C cmd to inform FW (enable). */
		h2c_parameter = 1;
		odm_fill_h2c_cmd(p_dm, ODM_H2C_WIFI_CALIBRATION, 1, &h2c_parameter);
		/* Check 0x1e6 */
		count = 0;
		u1b_tmp = odm_read_1byte(p_dm, 0x1e6);
		while (u1b_tmp != 0x1 && count < 1000) {
			ODM_delay_ms(1);
			u1b_tmp = odm_read_1byte(p_dm, 0x1e6);
			count++;
		}

		if (count >= 1000)
			RT_TRACE(COMP_INIT, DBG_LOUD, ("[IQK]Polling 0x1e6 to 1 for WiFi calibration H2C cmd FAIL! count(%d)", count));

		/* Wait BT IQK finished. */
		/* polling 0x1e7[0]=1 or 300ms timeout */
		u1b_tmp = odm_read_1byte(p_dm, 0x1e7);
		while ((!(u1b_tmp & BIT(0))) && count < 6000) {
			ODM_delay_ms(50);
			u1b_tmp = odm_read_1byte(p_dm, 0x1e7);
			count++;
		}
	} else {
		/* Set H2C cmd to inform FW (disable). */
		h2c_parameter = 0;
		odm_fill_h2c_cmd(p_dm, ODM_H2C_WIFI_CALIBRATION, 1, &h2c_parameter);
		/* Check 0x1e6 */
		count = 0;
		u1b_tmp = odm_read_1byte(p_dm, 0x1e6);
		while (u1b_tmp != 0 && count < 1000) {
			ODM_delay_us(10);
			u1b_tmp = odm_read_1byte(p_dm, 0x1e6);
			count++;
		}

		if (count >= 1000)
			RT_TRACE(COMP_INIT, DBG_LOUD, ("[IQK]Polling 0x1e6 to 0 for WiFi calibration H2C cmd FAIL! count(%d)", count));
		}
#endif
}


void
_phy_iq_calibrate_8723b(
	struct PHY_DM_STRUCT		*p_dm,
	s32		result[][8],
	u8		t
)
{
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
	u32			i;
	u8			path_aok, path_bok;
	u8			tmp0xc50 = (u8)odm_get_bb_reg(p_dm, 0xC50, MASKBYTE0);
	u8			tmp0xc58 = (u8)odm_get_bb_reg(p_dm, 0xC58, MASKBYTE0);
	u32			ADDA_REG[IQK_ADDA_REG_NUM] = {
		REG_FPGA0_XCD_SWITCH_CONTROL,	REG_BLUE_TOOTH,
		REG_RX_WAIT_CCA,		REG_TX_CCK_RFON,
		REG_TX_CCK_BBON,	REG_TX_OFDM_RFON,
		REG_TX_OFDM_BBON,	REG_TX_TO_RX,
		REG_TX_TO_TX,		REG_RX_CCK,
		REG_RX_OFDM,		REG_RX_WAIT_RIFS,
		REG_RX_TO_RX,		REG_STANDBY,
		REG_SLEEP,			REG_PMPD_ANAEN
	};
	u32			IQK_MAC_REG[IQK_MAC_REG_NUM] = {
		REG_TXPAUSE,		REG_BCN_CTRL,
		REG_BCN_CTRL_1,	REG_GPIO_MUXCFG
	};

	/* since 92C & 92D have the different define in IQK_BB_REG */
	u32	IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
		REG_OFDM_0_TRX_PATH_ENABLE,		REG_OFDM_0_TR_MUX_PAR,
		REG_FPGA0_XCD_RF_INTERFACE_SW,	REG_CONFIG_ANT_A,	REG_CONFIG_ANT_B,
		REG_FPGA0_XAB_RF_INTERFACE_SW,	REG_FPGA0_XA_RF_INTERFACE_OE,
		REG_FPGA0_XB_RF_INTERFACE_OE, REG_CCK_0_AFE_SETTING
	};

	u32 path_sel_bb, tmp0x764;
	/* u32 path_sel_bb, path_sel_rf; */

#if MP_DRIVER
	const u32	retry_count = 1;
#else
	const u32	retry_count = 2;
#endif

	/* Note: IQ calibration must be performed after loading */
	/*		PHY_REG.txt , and radio_a, radio_b.txt */

	/* u32 bbvalue; */

	if (t == 0) {
		/*	 	 bbvalue = odm_get_bb_reg(p_dm, REG_FPGA0_RFMOD, MASKDWORD);
		 * 			RT_DISP(FINIT, INIT_IQK, ("_phy_iq_calibrate_8188e()==>0x%08x\n",bbvalue)); */

		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQ Calibration for %s for %d times\n", (*p_dm->p_is_1_antenna ? "1ant" : "2ant"), t));
		if (*p_dm->p_is_1_antenna)
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQ Calibration for %s\n", (*p_dm->p_rf_default_path ? "S0" : "S1")));

		/* Save ADDA parameters, turn path A ADDA on */
		_phy_save_adda_registers8723b(p_dm, ADDA_REG, p_rf_calibrate_info->ADDA_backup, IQK_ADDA_REG_NUM);
		_phy_save_mac_registers8723b(p_dm, IQK_MAC_REG, p_rf_calibrate_info->IQK_MAC_backup);
		_phy_save_adda_registers8723b(p_dm, IQK_BB_REG_92C, p_rf_calibrate_info->IQK_BB_backup, IQK_BB_REG_NUM);
	}
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQ Calibration for %s for %d times\n", (*p_dm->p_is_1_antenna ? "1ant" : "2ant"), t));
	if (*p_dm->p_is_1_antenna)
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQ Calibration for %s\n", (*p_dm->p_rf_default_path ? "S0" : "S1")));

	_phy_path_adda_on8723b(p_dm, ADDA_REG, true);

	/* no serial mode */
#if 0
	if (t == 0)
		p_rf_calibrate_info->is_rf_pi_enable = (u8)odm_get_bb_reg(p_dm, REG_FPGA0_XA_HSSI_PARAMETER1, BIT(8));

	if (!p_rf_calibrate_info->is_rf_pi_enable) {
		/* Switch BB to PI mode to do IQ Calibration. */
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_phy_pi_mode_switch8723b(p_adapter, true);
#else
		_phy_pi_mode_switch8723b(p_dm, true);
#endif
	}
#endif

	/* save RF path for 8723B */
	path_sel_bb = odm_get_bb_reg(p_dm, 0x948, MASKDWORD);
	/* save 0x764 GNT_BT */
	tmp0x764 = odm_get_bb_reg(p_dm, 0x764, MASKDWORD);

	/*	path_sel_rf = odm_get_rf_reg(p_dm, RF_PATH_A, 0xb0, 0xfffff); */

	/* MAC settings */
	_phy_mac_setting_calibration8723b(p_dm, IQK_MAC_REG, p_rf_calibrate_info->IQK_MAC_backup);
	/* BB setting */
	/* odm_set_bb_reg(p_dm, REG_FPGA0_RFMOD, BIT24, 0x00); */
	odm_set_bb_reg(p_dm, REG_CCK_0_AFE_SETTING, 0x0f000000, 0xf);
	odm_set_bb_reg(p_dm, REG_OFDM_0_TRX_PATH_ENABLE, MASKDWORD, 0x03a05600);
	odm_set_bb_reg(p_dm, REG_OFDM_0_TR_MUX_PAR, MASKDWORD, 0x000800e4);
	odm_set_bb_reg(p_dm, REG_FPGA0_XCD_RF_INTERFACE_SW, MASKDWORD, 0x22204000);
	/*	odm_set_bb_reg(p_dm, REG_FPGA0_XAB_RF_INTERFACE_SW, BIT10, 0x01);
	 *	odm_set_bb_reg(p_dm, REG_FPGA0_XAB_RF_INTERFACE_SW, BIT(26), 0x01);
	 *	odm_set_bb_reg(p_dm, REG_FPGA0_XA_RF_INTERFACE_OE, BIT(10), 0x00);
	 *	odm_set_bb_reg(p_dm, REG_FPGA0_XB_RF_INTERFACE_OE, BIT(10), 0x00); */
	/* for 8723B */
#if 0
	if (is2T)

		odm_set_rf_reg(p_dm, RF_PATH_B, RF_AC, MASKDWORD, 0x10000);
#endif


	/* no APK */
#if 0
	/* Page B init */
	/* AP or IQK */
	odm_set_bb_reg(p_dm, REG_CONFIG_ANT_A, MASKDWORD, 0x0f600000);

	if (is2T)
		odm_set_bb_reg(p_dm, REG_CONFIG_ANT_B, MASKDWORD, 0x0f600000);
#endif

	/* RX IQ calibration setting for 8723B D cut large current issue when leaving IPS */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_WE_LUT, 0x80000, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_RCK_OS, RFREGOFFSETMASK, 0x30000);
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_TXPA_G1, RFREGOFFSETMASK, 0x0001f);
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0xf7fb7);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xed, 0x20, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x43, RFREGOFFSETMASK, 0x60fbd);
#if 0
	/* LOK RF setting */
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xed, 0x2, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x2, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x56, RFREGOFFSETMASK, 0x00032);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x76, RFREGOFFSETMASK, 0x00032);
#endif


	/* path A TX IQK */
#if 1

#if MP_DRIVER != 1
	if ((*p_dm->p_is_1_antenna == false) || ((*p_dm->p_is_1_antenna == true) && (p_dm->support_interface != ODM_ITRF_USB)
			&& (*p_dm->p_rf_default_path == 0)))
#endif
	{

		for (i = 0 ; i < retry_count ; i++) {
			path_aok = phy_path_a_iqk_8723b(p_dm);
			/*		if(path_aok == 0x03){ */
			if (path_aok == 0x01) {
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S1 Tx IQK Success!!\n"));
				result[t][0] = (odm_get_bb_reg(p_dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
				result[t][1] = (odm_get_bb_reg(p_dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
				break;
			}

		}
#endif

		/* path A RXIQK */
#if 1

		for (i = 0 ; i < retry_count ; i++) {
			path_aok = phy_path_a_rx_iqk_8723b(p_dm);
			if (path_aok == 0x03) {
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S1 Rx IQK Success!!\n"));
				/*				result[t][0] = (odm_get_bb_reg(p_dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD)&0x3FF0000)>>16;
				 *				result[t][1] = (odm_get_bb_reg(p_dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD)&0x3FF0000)>>16; */
				result[t][2] = (odm_get_bb_reg(p_dm, REG_RX_POWER_BEFORE_IQK_A_2, MASKDWORD) & 0x3FF0000) >> 16;
				result[t][3] = (odm_get_bb_reg(p_dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD) & 0x3FF0000) >> 16;
				break;
			} else
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S1Rx IQK Fail!!\n"));
		}

		if (0x00 == path_aok)
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S1 IQK failed!!\n"));
	}
#endif

	/* path B TX IQK */

#if 0
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	/*		_phy_path_a_stand_by8723b(p_adapter); */

	/*  Turn path B ADDA on */
	_phy_path_adda_on8723b(p_adapter, ADDA_REG, false, is2T);
#else
	/*		_phy_path_a_stand_by8723b(p_dm); */

	/*  Turn path B ADDA on */
	_phy_path_adda_on8723b(p_dm, ADDA_REG, false, is2T);
#endif
#endif

	/* path B TX IQK */
#if 1

#if MP_DRIVER != 1
	if ((*p_dm->p_is_1_antenna == false) || ((*p_dm->p_is_1_antenna == true) && (*p_dm->p_rf_default_path == 1))
	    || (p_dm->support_interface == ODM_ITRF_USB))
#endif
	{

		for (i = 0 ; i < retry_count ; i++) {
			path_bok = phy_path_b_iqk_8723b(p_dm);
			/*		if(path_bok == 0x03){ */
			if (path_bok == 0x01) {
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S0 Tx IQK Success!!\n"));
				result[t][4] = (odm_get_bb_reg(p_dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
				result[t][5] = (odm_get_bb_reg(p_dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
				break;
			}

		}
#endif

		/* path B RX IQK */
#if 1

		for (i = 0 ; i < retry_count ; i++) {
			path_bok = phy_path_b_rx_iqk_8723b(p_dm);
			if (path_bok == 0x03) {
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S0 Rx IQK Success!!\n"));
				/*				result[t][0] = (odm_get_bb_reg(p_dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD)&0x3FF0000)>>16;
				 *				result[t][1] = (odm_get_bb_reg(p_dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD)&0x3FF0000)>>16; */
				result[t][6] = (odm_get_bb_reg(p_dm, REG_RX_POWER_BEFORE_IQK_A_2, MASKDWORD) & 0x3FF0000) >> 16;
				result[t][7] = (odm_get_bb_reg(p_dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD) & 0x3FF0000) >> 16;
				break;
			} else
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S0 Rx IQK Fail!!\n"));
		}
#endif
		if (0x00 == path_bok)
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S0 IQK failed!!\n"));
	}
	/* Back to BB mode, load original value */
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK:Back to BB mode, load original value!\n"));
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	if (t != 0) {
		if (!p_rf_calibrate_info->is_rf_pi_enable) {
			/* Switch back BB to SI mode after finish IQ Calibration. */
			/*			_phy_pi_mode_switch8723b(p_dm, false); */
		}
		/* Reload ADDA power saving parameters */
		_phy_reload_adda_registers8723b(p_dm, ADDA_REG, p_rf_calibrate_info->ADDA_backup, IQK_ADDA_REG_NUM);
		/* Reload MAC parameters */
		_phy_reload_mac_registers8723b(p_dm, IQK_MAC_REG, p_rf_calibrate_info->IQK_MAC_backup);
		_phy_reload_adda_registers8723b(p_dm, IQK_BB_REG_92C, p_rf_calibrate_info->IQK_BB_backup, IQK_BB_REG_NUM);
		/* Reload RF path */
		odm_set_bb_reg(p_dm, 0x948, MASKDWORD, path_sel_bb);
		/* reload 0x764 GNT_BT */
		odm_set_bb_reg(p_dm, 0x764, MASKDWORD, tmp0x764);
		/*		odm_set_rf_reg(p_dm, RF_PATH_A, 0xb0, 0xfffff, path_sel_rf); */
		/* Allen initial gain 0xc50 */
		/* Restore RX initial gain */
		odm_set_bb_reg(p_dm, 0xc50, MASKBYTE0, 0x50);
		odm_set_bb_reg(p_dm, 0xc50, MASKBYTE0, tmp0xc50);
		if (*p_dm->p_is_1_antenna == false) {
			odm_set_bb_reg(p_dm, 0xc58, MASKBYTE0, 0x50);
			odm_set_bb_reg(p_dm, 0xc58, MASKBYTE0, tmp0xc58);
		}
		/* load 0xe30 IQC default value */
		odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x01008c00);
		odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x01008c00);
	}
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]_phy_iq_calibrate_8723b() <==\n"));
}


void
_phy_lc_calibrate_8723b(
	struct PHY_DM_STRUCT		*p_dm,
	boolean		is2T
)
{
	u8	tmp_reg;
	u32	rf_amode = 0, rf_bmode = 0, lc_cal;

	/* Check continuous TX and Packet TX */
	tmp_reg = odm_read_1byte(p_dm, 0xd03);

	if ((tmp_reg & 0x70) != 0)			/* Deal with contisuous TX case */
		odm_write_1byte(p_dm, 0xd03, tmp_reg & 0x8F);	/* disable all continuous TX */
	else							/* Deal with Packet TX case */
		odm_write_1byte(p_dm, REG_TXPAUSE, 0xFF);			/* block all queues */

	if ((tmp_reg & 0x70) != 0) {
		/* 1. Read original RF mode */
		/* path-A */
		rf_amode = odm_get_rf_reg(p_dm, RF_PATH_A, RF_AC, MASK12BITS);
		/* path-B */
		if (is2T)
			rf_bmode = odm_get_rf_reg(p_dm, RF_PATH_B, RF_AC, MASK12BITS);

		/* 2. Set RF mode = standby mode */
		/* path-A */
		odm_set_rf_reg(p_dm, RF_PATH_A, RF_AC, MASK12BITS, (rf_amode & 0x8FFFF) | 0x10000);
		/* path-B */
		if (is2T)
			odm_set_rf_reg(p_dm, RF_PATH_B, RF_AC, MASK12BITS, (rf_bmode & 0x8FFFF) | 0x10000);
	}

	/* 3. Read RF reg18 */
	lc_cal = odm_get_rf_reg(p_dm, RF_PATH_A, RF_CHNLBW, MASK12BITS);

	/* 4. Set LC calibration begin	bit15 */
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xB0, RFREGOFFSETMASK, 0xDFBE0); /* LDO ON */
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_CHNLBW, MASK12BITS, lc_cal | 0x08000);
	ODM_delay_ms(100);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xB0, RFREGOFFSETMASK, 0xDFFE0); /* LDO OFF */

	/* channel 10 LC calibration issue for 8723bs with 26M xtal */
	if (p_dm->support_interface == ODM_ITRF_SDIO && p_dm->package_type >= 0x2)
		odm_set_rf_reg(p_dm, RF_PATH_A, RF_CHNLBW, MASK12BITS, lc_cal);
	/* Restore original situation */
	if ((tmp_reg & 0x70) != 0) {	/* Deal with contisuous TX case */
		/* path-A */
		odm_write_1byte(p_dm, 0xd03, tmp_reg);
		odm_set_rf_reg(p_dm, RF_PATH_A, RF_AC, MASK12BITS, rf_amode);

		/* path-B */
		if (is2T)
			odm_set_rf_reg(p_dm, RF_PATH_B, RF_AC, MASK12BITS, rf_bmode);
	} else /* Deal with Packet TX case */
		odm_write_1byte(p_dm, REG_TXPAUSE, 0x00);
}

/* IQK version:0x1d    20141201
 * 1. modify the boundary of IQK result for 8723B F-cut */
void
phy_iq_calibrate_8723b(
	void		*p_dm_void,
	boolean	is_recovery
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
	s32			result[4][8];	/* last is final result */
	u8			i, final_candidate, indexforchannel;
	boolean			is_patha_ok, is_pathb_ok;
	s32			rege94, rege9c, regea4, regeac, regeb4, regebc, regec4, regecc, reg_tmp = 0;
	boolean			is12simular, is13simular, is23simular;
	u32			IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
		REG_OFDM_0_XA_RX_IQ_IMBALANCE,	REG_OFDM_0_XB_RX_IQ_IMBALANCE,
		REG_OFDM_0_ECCA_THRESHOLD,	REG_OFDM_0_AGC_RSSI_TABLE,
		REG_OFDM_0_XA_TX_IQ_IMBALANCE,	REG_OFDM_0_XB_TX_IQ_IMBALANCE,
		REG_OFDM_0_XC_TX_AFE,			REG_OFDM_0_XD_TX_AFE,
		REG_OFDM_0_RX_IQ_EXT_ANTA
	};
	u32			path_sel_bb = 0;
	boolean			is_reload_iqk = false;
	u16			count = 0;

	if (is_recovery && (!p_dm->is_in_hct_test)) {/* YJ,add for PowerTest,120405 */
		ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_LOUD, ("[IQK]phy_iq_calibrate_8723b: Return due to is_recovery!\n"));
		_phy_reload_adda_registers8723b(p_dm, IQK_BB_REG_92C, p_rf_calibrate_info->IQK_BB_backup_recover, 9);
		return;
	}

	/* Save RF path */
	path_sel_bb = odm_get_bb_reg(p_dm, 0x948, MASKDWORD);
#if MP_DRIVER != 1
	/* check if IQK had been done before!! */

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S1 0xc80 = 0x%x, S0 0xc80 = 0x%x\n", p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc80][VAL], p_rf_calibrate_info->tx_iqc_8723b[PATH_S0][idx_0xc80][VAL]));

	if ((*p_dm->p_is_1_antenna == true) && (*p_dm->p_rf_default_path == 0)) {	/* 1-ant, S1 */
		if (odm_set_iqc_by_rfpath(p_dm, 0)) {
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]1-ant S1 IQK value is reloaded!!!\n"));
			is_reload_iqk = true;
		}
	} else if ((*p_dm->p_is_1_antenna == true) && (*p_dm->p_rf_default_path == 1)) {	/* 1-ant, S0 */
		if (odm_set_iqc_by_rfpath(p_dm, 1)) { /* S0 */
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]1-ant S0 IQK value is reloaded!!!\n"));
			is_reload_iqk = true;
		}
	} else {	/* 2-ant */
		if ((p_rf_calibrate_info->tx_iqc_8723b[PATH_S1][idx_0xc80][VAL] != 0x0) && (p_rf_calibrate_info->rx_iqc_8723b[PATH_S1][idx_0xc14][VAL] != 0x0)) {
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK has been done before.!!!\n"));

			if ((path_sel_bb == 0x0) || (path_sel_bb == 0x200)) {	/* S1 */
				if (odm_set_iqc_by_rfpath(p_dm, 0)) {
					ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]2-ant S1 IQK value is reloaded!!!\n"));
					is_reload_iqk = true;
				}
			} else {	/* S0 */
				if (odm_set_iqc_by_rfpath(p_dm, 1)) {
					ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]2-ant S0 IQK value is reloaded!!!\n"));
					is_reload_iqk = true;
				}
			}
		}
	}

	if (is_reload_iqk)
		return;
#endif

	/* Check & wait if BT is doing IQK */
	if (*(p_dm->p_mp_mode) == false)
		_phy_check_coex_status_8723b(p_dm, true);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x948 = 0x%x\n", path_sel_bb));
	/* IQK start!!!!!!!!!! */
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK:Start!!!\n"));
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
		_phy_iq_calibrate_8723b(p_dm, result, i);

		if (i == 1) {
			is12simular = phy_simularity_compare_8723b(p_dm, result, 0, 1);
			if (is12simular) {
				final_candidate = 0;
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK: is12simular final_candidate is %x\n", final_candidate));
				break;
			}
		}

		if (i == 2) {
			is13simular = phy_simularity_compare_8723b(p_dm, result, 0, 2);
			if (is13simular) {
				final_candidate = 0;
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK: is13simular final_candidate is %x\n", final_candidate));

				break;
			}
			is23simular = phy_simularity_compare_8723b(p_dm, result, 1, 2);
			if (is23simular) {
				final_candidate = 1;
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK: is23simular final_candidate is %x\n", final_candidate));
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
		rege9c = result[i][1];
		regea4 = result[i][2];
		regeac = result[i][3];
		regeb4 = result[i][4];
		regebc = result[i][5];
		regec4 = result[i][6];
		regecc = result[i][7];
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK: rege94=%x rege9c=%x regea4=%x regeac=%x regeb4=%x regebc=%x regec4=%x regecc=%x\n ", rege94, rege9c, regea4, regeac, regeb4, regebc, regec4, regecc));
	}

	if (final_candidate != 0xff) {
		p_rf_calibrate_info->rege94 = rege94 = result[final_candidate][0];
		p_rf_calibrate_info->rege9c = rege9c = result[final_candidate][1];
		regea4 = result[final_candidate][2];
		regeac = result[final_candidate][3];
		p_rf_calibrate_info->regeb4 = regeb4 = result[final_candidate][4];
		p_rf_calibrate_info->regebc = regebc = result[final_candidate][5];
		regec4 = result[final_candidate][6];
		regecc = result[final_candidate][7];
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK: final_candidate is %x\n", final_candidate));
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK: rege94=%x rege9c=%x regea4=%x regeac=%x regeb4=%x regebc=%x regec4=%x regecc=%x\n ", rege94, rege9c, regea4, regeac, regeb4, regebc, regec4, regecc));
		is_patha_ok = is_pathb_ok = true;
	} else {
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK: FAIL use default value\n"));
		p_rf_calibrate_info->rege94 = p_rf_calibrate_info->regeb4 = 0x100;	/* X default value */
		p_rf_calibrate_info->rege9c = p_rf_calibrate_info->regebc = 0x0;		/* Y default value */
	}

	/* fill IQK matrix */
	if (rege94 != 0)
		_phy_path_a_fill_iqk_matrix8723b(p_dm, is_patha_ok, result, final_candidate, (regea4 == 0));
	if (regeb4 != 0)
		_phy_path_b_fill_iqk_matrix8723b(p_dm, is_pathb_ok, result, final_candidate, (regec4 == 0));

	indexforchannel = odm_get_right_chnl_place_for_iqk(*p_dm->p_channel);

	/* To Fix BSOD when final_candidate is 0xff
	 * by sherry 20120321 */
	if (final_candidate < 4) {
		for (i = 0; i < iqk_matrix_reg_num; i++)
			p_rf_calibrate_info->iqk_matrix_reg_setting[indexforchannel].value[0][i] = result[final_candidate][i];
		p_rf_calibrate_info->iqk_matrix_reg_setting[indexforchannel].is_iqk_done = true;
	}
	/* RT_DISP(FINIT, INIT_IQK, ("\nIQK OK indexforchannel %d.\n", indexforchannel)); */
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]\nIQK OK indexforchannel %d.\n", indexforchannel));
	_phy_save_adda_registers8723b(p_dm, IQK_BB_REG_92C, p_rf_calibrate_info->IQK_BB_backup_recover, IQK_BB_REG_NUM);

	/* Restore RF path */
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, path_sel_bb);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]is1ant = %d, supprotinterface = %d, RFdefaultpath=%d.\n", *p_dm->p_is_1_antenna, p_dm->support_interface, *p_dm->p_rf_default_path));

	/* fill IQK register */
#if MP_DRIVER == 1
	if ((path_sel_bb == 0x0) || (path_sel_bb == 0x200))  /* S1 */
		odm_set_iqc_by_rfpath(p_dm, 0);
	else
		odm_set_iqc_by_rfpath(p_dm, 1);
#else
	if (*p_dm->p_rf_default_path == 0x0)  /* S1 */
		odm_set_iqc_by_rfpath(p_dm, 0);
	else
		odm_set_iqc_by_rfpath(p_dm, 1);
#endif
	if (*(p_dm->p_mp_mode) == false)
		_phy_check_coex_status_8723b(p_dm, false);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK finished\n"));
}


void
phy_lc_calibrate_8723b(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	_phy_lc_calibrate_8723b(p_dm, false);
}

#if 0
/* Analog Pre-distortion calibration */
#define		APK_BB_REG_NUM	8
#define		APK_CURVE_REG_NUM 4
#define		PATH_NUM		2

void
_phy_ap_calibrate_8723b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	s8		delta,
	boolean		is2T
)
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->DM_OutSrc;
#endif
#endif
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
	u32			reg_d[PATH_NUM];
	u32			tmp_reg, index, offset,  apkbound;
	u8			path, i, pathbound = PATH_NUM;
	u32			BB_backup[APK_BB_REG_NUM];
	u32			BB_REG[APK_BB_REG_NUM] = {
		REG_FPGA1_TX_BLOCK,	REG_OFDM_0_TRX_PATH_ENABLE,
		REG_FPGA0_RFMOD,	REG_OFDM_0_TR_MUX_PAR,
		REG_FPGA0_XCD_RF_INTERFACE_SW,	REG_FPGA0_XAB_RF_INTERFACE_SW,
		REG_FPGA0_XA_RF_INTERFACE_OE,	REG_FPGA0_XB_RF_INTERFACE_OE
	};
	u32			BB_AP_MODE[APK_BB_REG_NUM] = {
		0x00000020, 0x00a05430, 0x02040000,
		0x000800e4, 0x00204000
	};
	u32			BB_normal_AP_MODE[APK_BB_REG_NUM] = {
		0x00000020, 0x00a05430, 0x02040000,
		0x000800e4, 0x22204000
	};

	u32			AFE_backup[IQK_ADDA_REG_NUM];
	u32			AFE_REG[IQK_ADDA_REG_NUM] = {
		REG_FPGA0_XCD_SWITCH_CONTROL,	REG_BLUE_TOOTH,
		REG_RX_WAIT_CCA,		REG_TX_CCK_RFON,
		REG_TX_CCK_BBON,	REG_TX_OFDM_RFON,
		REG_TX_OFDM_BBON,	REG_TX_TO_RX,
		REG_TX_TO_TX,		REG_RX_CCK,
		REG_RX_OFDM,		REG_RX_WAIT_RIFS,
		REG_RX_TO_RX,		REG_STANDBY,
		REG_SLEEP,			REG_PMPD_ANAEN
	};

	u32			MAC_backup[IQK_MAC_REG_NUM];
	u32			MAC_REG[IQK_MAC_REG_NUM] = {
		REG_TXPAUSE,		REG_BCN_CTRL,
		REG_BCN_CTRL_1,	REG_GPIO_MUXCFG
	};

	u32			APK_RF_init_value[PATH_NUM][APK_BB_REG_NUM] = {
		{0x0852c, 0x1852c, 0x5852c, 0x1852c, 0x5852c},
		{0x2852e, 0x0852e, 0x3852e, 0x0852e, 0x0852e}
	};

	u32			APK_normal_RF_init_value[PATH_NUM][APK_BB_REG_NUM] = {
		{0x0852c, 0x0a52c, 0x3a52c, 0x5a52c, 0x5a52c},	/* path settings equal to path b settings */
		{0x0852c, 0x0a52c, 0x5a52c, 0x5a52c, 0x5a52c}
	};

	u32			APK_RF_value_0[PATH_NUM][APK_BB_REG_NUM] = {
		{0x52019, 0x52014, 0x52013, 0x5200f, 0x5208d},
		{0x5201a, 0x52019, 0x52016, 0x52033, 0x52050}
	};

	u32			APK_normal_RF_value_0[PATH_NUM][APK_BB_REG_NUM] = {
		{0x52019, 0x52017, 0x52010, 0x5200d, 0x5206a},	/* path settings equal to path b settings */
		{0x52019, 0x52017, 0x52010, 0x5200d, 0x5206a}
	};

	u32			AFE_on_off[PATH_NUM] = {
		0x04db25a4, 0x0b1b25a4
	};	/* path A on path B off / path A off path B on */

	u32			APK_offset[PATH_NUM] = {
		REG_CONFIG_ANT_A, REG_CONFIG_ANT_B
	};

	u32			APK_normal_offset[PATH_NUM] = {
		REG_CONFIG_PMPD_ANT_A, REG_CONFIG_PMPD_ANT_B
	};

	u32			APK_value[PATH_NUM] = {
		0x92fc0000, 0x12fc0000
	};

	u32			APK_normal_value[PATH_NUM] = {
		0x92680000, 0x12680000
	};

	s8			APK_delta_mapping[APK_BB_REG_NUM][13] = {
		{-4, -3, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},
		{-4, -3, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},
		{-6, -4, -2, -2, -1, -1, 0, 1, 2, 3, 4, 5, 6},
		{-1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5, 6},
		{-11, -9, -7, -5, -3, -1, 0, 0, 0, 0, 0, 0, 0}
	};

	u32			APK_normal_setting_value_1[13] = {
		0x01017018, 0xf7ed8f84, 0x1b1a1816, 0x2522201e, 0x322e2b28,
		0x433f3a36, 0x5b544e49, 0x7b726a62, 0xa69a8f84, 0xdfcfc0b3,
		0x12680000, 0x00880000, 0x00880000
	};

	u32			APK_normal_setting_value_2[16] = {
		0x01c7021d, 0x01670183, 0x01000123, 0x00bf00e2, 0x008d00a3,
		0x0068007b, 0x004d0059, 0x003a0042, 0x002b0031, 0x001f0025,
		0x0017001b, 0x00110014, 0x000c000f, 0x0009000b, 0x00070008,
		0x00050006
	};

	u32			APK_result[PATH_NUM][APK_BB_REG_NUM];	/* val_1_1a, val_1_2a, val_2a, val_3a, val_4a
 *	u32			AP_curve[PATH_NUM][APK_CURVE_REG_NUM]; */

	s32			BB_offset, delta_V, delta_offset;

#if MP_DRIVER == 1
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PMPT_CONTEXT	p_mpt_ctx = &(p_adapter->mppriv.mpt_ctx);
#else
	PMPT_CONTEXT	p_mpt_ctx = &(p_adapter->MptCtx);
#endif
	p_mpt_ctx->APK_bound[0] = 45;
	p_mpt_ctx->APK_bound[1] = 52;

#endif

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("==>_phy_ap_calibrate_8188e() delta %d\n", delta));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("AP Calibration for %s\n", (is2T ? "2T2R" : "1T1R")));
	if (!is2T)
		pathbound = 1;

	/* 2 FOR NORMAL CHIP SETTINGS */

	/* Temporarily do not allow normal driver to do the following settings because these offset
	 * and value will cause RF internal PA to be unpredictably disabled by HW, such that RF Tx signal
	 * will disappear after disable/enable card many times on 88CU. RF SD and DD have not find the
	 * root cause, so we remove these actions temporarily. Added by tynli and SD3 Allen. 2010.05.31. */
#if MP_DRIVER != 1
	return;
#endif
	/* settings adjust for normal chip */
	for (index = 0; index < PATH_NUM; index++) {
		APK_offset[index] = APK_normal_offset[index];
		APK_value[index] = APK_normal_value[index];
		AFE_on_off[index] = 0x6fdb25a4;
	}

	for (index = 0; index < APK_BB_REG_NUM; index++) {
		for (path = 0; path < pathbound; path++) {
			APK_RF_init_value[path][index] = APK_normal_RF_init_value[path][index];
			APK_RF_value_0[path][index] = APK_normal_RF_value_0[path][index];
		}
		BB_AP_MODE[index] = BB_normal_AP_MODE[index];
	}

	apkbound = 6;

	/* save BB default value */
	for (index = 0; index < APK_BB_REG_NUM ; index++) {
		if (index == 0)		/* skip */
			continue;
		BB_backup[index] = odm_get_bb_reg(p_dm, BB_REG[index], MASKDWORD);
	}

	/* save MAC default value */
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_phy_save_mac_registers8723b(p_adapter, MAC_REG, MAC_backup);

	/* save AFE default value */
	_phy_save_adda_registers8723b(p_adapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);
#else
	_phy_save_mac_registers8723b(p_dm, MAC_REG, MAC_backup);

	/* save AFE default value */
	_phy_save_adda_registers8723b(p_dm, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);
#endif

	for (path = 0; path < pathbound; path++) {


		if (path == RF_PATH_A) {
			/* path A APK */
			/* load APK setting */
			/* path-A */
			offset = REG_PDP_ANT_A;
			for (index = 0; index < 11; index++) {
				odm_set_bb_reg(p_dm, offset, MASKDWORD, APK_normal_setting_value_1[index]);
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_ap_calibrate_8188e() offset 0x%x value 0x%x\n", offset, odm_get_bb_reg(p_dm, offset, MASKDWORD)));

				offset += 0x04;
			}

			odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_B, MASKDWORD, 0x12680000);

			offset = REG_CONFIG_ANT_A;
			for (; index < 13; index++) {
				odm_set_bb_reg(p_dm, offset, MASKDWORD, APK_normal_setting_value_1[index]);
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_ap_calibrate_8188e() offset 0x%x value 0x%x\n", offset, odm_get_bb_reg(p_dm, offset, MASKDWORD)));

				offset += 0x04;
			}

			/* page-B1 */
			odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x400000);

			/* path A */
			offset = REG_PDP_ANT_A;
			for (index = 0; index < 16; index++) {
				odm_set_bb_reg(p_dm, offset, MASKDWORD, APK_normal_setting_value_2[index]);
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_ap_calibrate_8188e() offset 0x%x value 0x%x\n", offset, odm_get_bb_reg(p_dm, offset, MASKDWORD)));

				offset += 0x04;
			}
			odm_set_bb_reg(p_dm,  REG_FPGA0_IQK, 0xffffff00, 0x000000);
		} else if (path == RF_PATH_B) {
			/* path B APK */
			/* load APK setting */
			/* path-B */
			offset = REG_PDP_ANT_B;
			for (index = 0; index < 10; index++) {
				odm_set_bb_reg(p_dm, offset, MASKDWORD, APK_normal_setting_value_1[index]);
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_ap_calibrate_8188e() offset 0x%x value 0x%x\n", offset, odm_get_bb_reg(p_dm, offset, MASKDWORD)));

				offset += 0x04;
			}
			odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_A, MASKDWORD, 0x12680000);
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_B, MASKDWORD, 0x12680000);
#else
			odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_B, MASKDWORD, 0x12680000);
#endif

			offset = REG_CONFIG_ANT_A;
			index = 11;
			for (; index < 13; index++) { /* offset 0xb68, 0xb6c */
				odm_set_bb_reg(p_dm, offset, MASKDWORD, APK_normal_setting_value_1[index]);
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_ap_calibrate_8188e() offset 0x%x value 0x%x\n", offset, odm_get_bb_reg(p_dm, offset, MASKDWORD)));

				offset += 0x04;
			}

			/* page-B1 */
			odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x400000);

			/* path B */
			offset = 0xb60;
			for (index = 0; index < 16; index++) {
				odm_set_bb_reg(p_dm, offset, MASKDWORD, APK_normal_setting_value_2[index]);
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_ap_calibrate_8188e() offset 0x%x value 0x%x\n", offset, odm_get_bb_reg(p_dm, offset, MASKDWORD)));

				offset += 0x04;
			}
			odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
		}

		/* save RF default value */
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		reg_d[path] = odm_get_rf_reg(p_dm, path, RF_TXBIAS_A, MASKDWORD);
#else
		reg_d[path] = odm_get_rf_reg(p_dm, path, RF_TXBIAS_A, MASKDWORD);
#endif

		/* path A AFE all on, path B AFE All off or vise versa */
		for (index = 0; index < IQK_ADDA_REG_NUM ; index++)
			odm_set_bb_reg(p_dm, AFE_REG[index], MASKDWORD, AFE_on_off[path]);
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_ap_calibrate_8188e() offset 0xe70 %x\n", odm_get_bb_reg(p_dm, REG_RX_WAIT_CCA, MASKDWORD)));

		/* BB to AP mode */
		if (path == 0) {
			for (index = 0; index < APK_BB_REG_NUM ; index++) {

				if (index == 0)		/* skip */
					continue;
				else if (index < 5)
					odm_set_bb_reg(p_dm, BB_REG[index], MASKDWORD, BB_AP_MODE[index]);
				else if (BB_REG[index] == 0x870)
					odm_set_bb_reg(p_dm, BB_REG[index], MASKDWORD, BB_backup[index] | BIT(10) | BIT(26));
				else
					odm_set_bb_reg(p_dm, BB_REG[index], BIT(10), 0x0);
			}

			odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x01008c00);
			odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x01008c00);
		} else {	/* path B */
			odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_B, MASKDWORD, 0x01008c00);
			odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_B, MASKDWORD, 0x01008c00);

		}

		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_ap_calibrate_8188e() offset 0x800 %x\n", odm_get_bb_reg(p_dm, 0x800, MASKDWORD)));

		/* MAC settings */
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_phy_mac_setting_calibration8723b(p_adapter, MAC_REG, MAC_backup);
#else
		_phy_mac_setting_calibration8723b(p_dm, MAC_REG, MAC_backup);
#endif

		if (path == RF_PATH_A)	/* path B to standby mode */
			odm_set_rf_reg(p_dm, RF_PATH_B, RF_AC, MASKDWORD, 0x10000);
		else {		/* path A to standby mode */
			odm_set_rf_reg(p_dm, RF_PATH_A, RF_AC, MASKDWORD, 0x10000);
			odm_set_rf_reg(p_dm, RF_PATH_A, RF_MODE1, MASKDWORD, 0x1000f);
			odm_set_rf_reg(p_dm, RF_PATH_A, RF_MODE2, MASKDWORD, 0x20103);
		}

		delta_offset = ((delta + 14) / 2);
		if (delta_offset < 0)
			delta_offset = 0;
		else if (delta_offset > 12)
			delta_offset = 12;

		/* AP calibration */
		for (index = 0; index < APK_BB_REG_NUM; index++) {
			if (index != 1)	/* only DO PA11+PAD01001, AP RF setting */
				continue;

			tmp_reg = APK_RF_init_value[path][index];
#if 1
			if (!p_rf_calibrate_info->is_apk_thermal_meter_ignore) {
				BB_offset = (tmp_reg & 0xF0000) >> 16;

				if (!(tmp_reg & BIT(15))) /* sign bit 0 */
					BB_offset = -BB_offset;

				delta_V = APK_delta_mapping[index][delta_offset];

				BB_offset += delta_V;

				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_ap_calibrate_8188e() APK index %d tmp_reg 0x%x delta_V %d delta_offset %d\n", index, tmp_reg, delta_V, delta_offset));

				if (BB_offset < 0) {
					tmp_reg = tmp_reg & (~BIT(15));
					BB_offset = -BB_offset;
				} else
					tmp_reg = tmp_reg | BIT(15);
				tmp_reg = (tmp_reg & 0xFFF0FFFF) | (BB_offset << 16);
			}
#endif

			odm_set_rf_reg(p_dm, (enum rf_path)path, RF_IPA_A, MASKDWORD, 0x8992e);
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_ap_calibrate_8188e() offset 0xc %x\n", odm_get_rf_reg(p_dm, path, RF_IPA_A, MASKDWORD)));
			odm_set_rf_reg(p_dm, (enum rf_path)path, RF_AC, MASKDWORD, APK_RF_value_0[path][index]);
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_ap_calibrate_8188e() offset 0x0 %x\n", odm_get_rf_reg(p_dm, path, RF_AC, MASKDWORD)));
			odm_set_rf_reg(p_dm, (enum rf_path)path, RF_TXBIAS_A, MASKDWORD, tmp_reg);
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_ap_calibrate_8188e() offset 0xd %x\n", odm_get_rf_reg(p_dm, path, RF_TXBIAS_A, MASKDWORD)));
#else
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_ap_calibrate_8188e() offset 0xc %x\n", odm_get_rf_reg(p_dm, path, RF_IPA_A, MASKDWORD)));
			odm_set_rf_reg(p_dm, path, RF_AC, MASKDWORD, APK_RF_value_0[path][index]);
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_ap_calibrate_8188e() offset 0x0 %x\n", odm_get_rf_reg(p_dm, path, RF_AC, MASKDWORD)));
			odm_set_rf_reg(p_dm, path, RF_TXBIAS_A, MASKDWORD, tmp_reg);
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_ap_calibrate_8188e() offset 0xd %x\n", odm_get_rf_reg(p_dm, path, RF_TXBIAS_A, MASKDWORD)));
#endif

			/* PA11+PAD01111, one shot */
			i = 0;
			do {
				odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x800000);
				{
					odm_set_bb_reg(p_dm, APK_offset[path], MASKDWORD, APK_value[0]);
					ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_ap_calibrate_8188e() offset 0x%x value 0x%x\n", APK_offset[path], odm_get_bb_reg(p_dm, APK_offset[path], MASKDWORD)));
					ODM_delay_ms(3);
					odm_set_bb_reg(p_dm, APK_offset[path], MASKDWORD, APK_value[1]);
					ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_ap_calibrate_8188e() offset 0x%x value 0x%x\n", APK_offset[path], odm_get_bb_reg(p_dm, APK_offset[path], MASKDWORD)));

					ODM_delay_ms(20);
				}
				odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

				if (path == RF_PATH_A)
					tmp_reg = odm_get_bb_reg(p_dm, REG_APK, 0x03E00000);
				else
					tmp_reg = odm_get_bb_reg(p_dm, REG_APK, 0xF8000000);
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_ap_calibrate_8188e() offset 0xbd8[25:21] %x\n", tmp_reg));


				i++;
			} while (tmp_reg > apkbound && i < 4);

			APK_result[path][index] = tmp_reg;
		}
	}

	/* reload MAC default value */
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_phy_reload_mac_registers8723b(p_adapter, MAC_REG, MAC_backup);
#else
	_phy_reload_mac_registers8723b(p_dm, MAC_REG, MAC_backup);
#endif

	/* reload BB default value */
	for (index = 0; index < APK_BB_REG_NUM ; index++) {

		if (index == 0)		/* skip */
			continue;
		odm_set_bb_reg(p_dm, BB_REG[index], MASKDWORD, BB_backup[index]);
	}

	/* reload AFE default value */
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_phy_reload_adda_registers8723b(p_adapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);
#else
	_phy_reload_adda_registers8723b(p_dm, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);
#endif

	/* reload RF path default value */
	for (path = 0; path < pathbound; path++) {
		odm_set_rf_reg(p_dm, (enum rf_path)path, 0xd, MASKDWORD, reg_d[path]);
		if (path == RF_PATH_B) {
			odm_set_rf_reg(p_dm, RF_PATH_A, RF_MODE1, MASKDWORD, 0x1000f);
			odm_set_rf_reg(p_dm, RF_PATH_A, RF_MODE2, MASKDWORD, 0x20101);
		}

		/* note no index == 0 */
		if (APK_result[path][1] > 6)
			APK_result[path][1] = 6;
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("apk path %d result %d 0x%x \t", path, 1, APK_result[path][1]));
	}

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("\n"));


	for (path = 0; path < pathbound; path++) {
		odm_set_rf_reg(p_dm, (enum rf_path)path, 0x3, MASKDWORD,
			((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (APK_result[path][1] << 5) | APK_result[path][1]));
		if (path == RF_PATH_A)
			odm_set_rf_reg(p_dm, (enum rf_path)path, 0x4, MASKDWORD,
				((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (0x00 << 5) | 0x05));
		else
			odm_set_rf_reg(p_dm, (enum rf_path)path, 0x4, MASKDWORD,
				((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (0x02 << 5) | 0x05));
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		odm_set_rf_reg(p_dm, (enum rf_path)path, RF_BS_PA_APSET_G9_G11, MASKDWORD,
			((0x08 << 15) | (0x08 << 10) | (0x08 << 5) | 0x08));
#endif
	}

	p_rf_calibrate_info->is_ap_kdone = true;

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("<==_phy_ap_calibrate_8188e()\n"));
}


void
phy_ap_calibrate_8723b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	s8		delta
)
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->DM_OutSrc;
#endif
#endif
	struct _hal_rf_				*p_rf = &(p_dm->rf_table);
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
#if DISABLE_BB_RF
	return;
#endif

	return;
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if (!(p_rf->rf_supportability & HAL_RF_IQK))
		return;
#endif

#if FOR_BRAZIL_PRETEST != 1
	if (p_rf_calibrate_info->is_ap_kdone)
#endif
		return;

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	if (IS_92C_SERIAL(p_hal_data->VersionID))
		_phy_ap_calibrate_8723b(p_adapter, delta, true);
	else
#endif
	{
		/* For 88C 1T1R */
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_phy_ap_calibrate_8723b(p_adapter, delta, false);
#else
		_phy_ap_calibrate_8723b(p_dm, delta, false);
#endif
	}
}

#endif
void _phy_set_rf_path_switch_8723b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	boolean		is_main,
	boolean		is2T
)
{

	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->DM_OutSrc;

	if (is_main) /* Left antenna */
		odm_set_bb_reg(p_dm, 0x92C, MASKDWORD, 0x1);
	else
		odm_set_bb_reg(p_dm, 0x92C, MASKDWORD, 0x2);
}
void phy_set_rf_path_switch_8723b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	boolean		is_main
)
{

#if DISABLE_BB_RF
	return;
#endif

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_phy_set_rf_path_switch_8723b(p_adapter, is_main, true);
#endif

}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
/* digital predistortion */
#if 0
#define		DP_BB_REG_NUM		7
#define		DP_RF_REG_NUM		1
#define		DP_RETRY_LIMIT		10
#define		DP_PATH_NUM		2
#define		DP_DPK_NUM			3
#define		DP_DPK_VALUE_NUM	2
void
_phy_digital_predistortion8723b(
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct _ADAPTER	*p_adapter,
#else
	struct PHY_DM_STRUCT	*p_dm,
#endif
	boolean		is2T
)
{
#if (RT_PLATFORM == PLATFORM_WINDOWS)
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->DM_OutSrc;
#endif
#endif
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
	u32			tmp_reg, tmp_reg2, index,  i;
	u8			path, pathbound = PATH_NUM;
	u32			AFE_backup[IQK_ADDA_REG_NUM];
	u32			AFE_REG[IQK_ADDA_REG_NUM] = {
		REG_FPGA0_XCD_SWITCH_CONTROL,	REG_BLUE_TOOTH,
		REG_RX_WAIT_CCA,		REG_TX_CCK_RFON,
		REG_TX_CCK_BBON,	REG_TX_OFDM_RFON,
		REG_TX_OFDM_BBON,	REG_TX_TO_RX,
		REG_TX_TO_TX,		REG_RX_CCK,
		REG_RX_OFDM,		REG_RX_WAIT_RIFS,
		REG_RX_TO_RX,		REG_STANDBY,
		REG_SLEEP,			REG_PMPD_ANAEN
	};

	u32			BB_backup[DP_BB_REG_NUM];
	u32			BB_REG[DP_BB_REG_NUM] = {
		REG_OFDM_0_TRX_PATH_ENABLE, REG_FPGA0_RFMOD,
		REG_OFDM_0_TR_MUX_PAR,	REG_FPGA0_XCD_RF_INTERFACE_SW,
		REG_FPGA0_XAB_RF_INTERFACE_SW, REG_FPGA0_XA_RF_INTERFACE_OE,
		REG_FPGA0_XB_RF_INTERFACE_OE
	};
	u32			BB_settings[DP_BB_REG_NUM] = {
		0x00a05430, 0x02040000, 0x000800e4, 0x22208000,
		0x0, 0x0, 0x0
	};

	u32			RF_backup[DP_PATH_NUM][DP_RF_REG_NUM];
	u32			RF_REG[DP_RF_REG_NUM] = {
		RF_TXBIAS_A
	};

	u32			MAC_backup[IQK_MAC_REG_NUM];
	u32			MAC_REG[IQK_MAC_REG_NUM] = {
		REG_TXPAUSE,		REG_BCN_CTRL,
		REG_BCN_CTRL_1,	REG_GPIO_MUXCFG
	};

	u32			tx_agc[DP_DPK_NUM][DP_DPK_VALUE_NUM] = {
		{0x1e1e1e1e, 0x03901e1e},
		{0x18181818, 0x03901818},
		{0x0e0e0e0e, 0x03900e0e}
	};

	u32			AFE_on_off[PATH_NUM] = {
		0x04db25a4, 0x0b1b25a4
	};	/* path A on path B off / path A off path B on */

	u8			retry_count = 0;


	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("==>_phy_digital_predistortion8723b()\n"));

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_digital_predistortion8723b for %s\n", (is2T ? "2T2R" : "1T1R")));

	/* save BB default value */
	for (index = 0; index < DP_BB_REG_NUM; index++)
		BB_backup[index] = odm_get_bb_reg(p_dm, BB_REG[index], MASKDWORD);

	/* save MAC default value */
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_phy_save_mac_registers8723b(p_adapter, BB_REG, MAC_backup);
#else
	_phy_save_mac_registers8723b(p_dm, BB_REG, MAC_backup);
#endif

	/* save RF default value */
	for (path = 0; path < DP_PATH_NUM; path++) {
		for (index = 0; index < DP_RF_REG_NUM; index++)
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			RF_backup[path][index] = odm_get_rf_reg(p_dm, path, RF_REG[index], MASKDWORD);
#else
			RF_backup[path][index] = odm_get_rf_reg(p_adapter, path, RF_REG[index], MASKDWORD);
#endif
	}

	/* save AFE default value */
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_phy_save_adda_registers8723b(p_adapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);
#else
	_phy_save_adda_registers8723b(p_dm, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);
#endif

	/* path A/B AFE all on */
	for (index = 0; index < IQK_ADDA_REG_NUM ; index++)
		odm_set_bb_reg(p_dm, AFE_REG[index], MASKDWORD, 0x6fdb25a4);

	/* BB register setting */
	for (index = 0; index < DP_BB_REG_NUM; index++) {
		if (index < 4)
			odm_set_bb_reg(p_dm, BB_REG[index], MASKDWORD, BB_settings[index]);
		else if (index == 4)
			odm_set_bb_reg(p_dm, BB_REG[index], MASKDWORD, BB_backup[index] | BIT(10) | BIT(26));
		else
			odm_set_bb_reg(p_dm, BB_REG[index], BIT(10), 0x00);
	}

	/* MAC register setting */
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_phy_mac_setting_calibration8723b(p_adapter, MAC_REG, MAC_backup);
#else
	_phy_mac_setting_calibration8723b(p_dm, MAC_REG, MAC_backup);
#endif

	/* PAGE-E IQC setting */
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x01008c00);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x01008c00);
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_B, MASKDWORD, 0x01008c00);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_B, MASKDWORD, 0x01008c00);

	/* path_A DPK */
	/* path B to standby mode */
	odm_set_rf_reg(p_dm, RF_PATH_B, RF_AC, MASKDWORD, 0x10000);

	/* PA gain = 11 & PAD1 => tx_agc 1f ~11 */
	/* PA gain = 11 & PAD2 => tx_agc 10~0e */
	/* PA gain = 01 => tx_agc 0b~0d */
	/* PA gain = 00 => tx_agc 0a~00 */
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x400000);
	odm_set_bb_reg(p_dm, 0xbc0, MASKDWORD, 0x0005361f);
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	/* do inner loopback DPK 3 times */
	for (i = 0; i < 3; i++) {
		/* PA gain = 11 & PAD2 => tx_agc = 0x0f/0x0c/0x07 */
		for (index = 0; index < 3; index++)
			odm_set_bb_reg(p_dm, 0xe00 + index * 4, MASKDWORD, tx_agc[i][0]);
		odm_set_bb_reg(p_dm, 0xe00 + index * 4, MASKDWORD, tx_agc[i][1]);
		for (index = 0; index < 4; index++)
			odm_set_bb_reg(p_dm, 0xe10 + index * 4, MASKDWORD, tx_agc[i][0]);

		/* PAGE_B for path-A inner loopback DPK setting */
		odm_set_bb_reg(p_dm, REG_PDP_ANT_A, MASKDWORD, 0x02097098);
		odm_set_bb_reg(p_dm, REG_PDP_ANT_A_4, MASKDWORD, 0xf76d9f84);
		odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_A, MASKDWORD, 0x0004ab87);
		odm_set_bb_reg(p_dm, REG_CONFIG_ANT_A, MASKDWORD, 0x00880000);

		/* ----send one shot signal---- */
		/* path A */
		odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_A, MASKDWORD, 0x80047788);
		ODM_delay_ms(1);
		odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_A, MASKDWORD, 0x00047788);
		ODM_delay_ms(50);
	}

	/* PA gain = 11 => tx_agc = 1a */
	for (index = 0; index < 3; index++)
		odm_set_bb_reg(p_dm, 0xe00 + index * 4, MASKDWORD, 0x34343434);
	odm_set_bb_reg(p_dm, 0xe08 + index * 4, MASKDWORD, 0x03903434);
	for (index = 0; index < 4; index++)
		odm_set_bb_reg(p_dm, 0xe10 + index * 4, MASKDWORD, 0x34343434);

	/* ==================================== */
	/* PAGE_B for path-A DPK setting */
	/* ==================================== */
	/* open inner loopback @ b00[19]:10 od 0xb00 0x01097018 */
	odm_set_bb_reg(p_dm, REG_PDP_ANT_A, MASKDWORD, 0x02017098);
	odm_set_bb_reg(p_dm, REG_PDP_ANT_A_4, MASKDWORD, 0xf76d9f84);
	odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_A, MASKDWORD, 0x0004ab87);
	odm_set_bb_reg(p_dm, REG_CONFIG_ANT_A, MASKDWORD, 0x00880000);

	/* rf_lpbk_setup */
	/* 1.rf 00:5205a, rf 0d:0e52c */
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x0c, MASKDWORD, 0x8992b);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x0d, MASKDWORD, 0x0e52c);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x00, MASKDWORD, 0x5205a);

	/* ----send one shot signal---- */
	/* path A */
	odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_A, MASKDWORD, 0x800477c0);
	ODM_delay_ms(1);
	odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_A, MASKDWORD, 0x000477c0);
	ODM_delay_ms(50);

	while (retry_count < DP_RETRY_LIMIT && !p_rf_calibrate_info->is_dp_path_aok) {
		/* ----read back measurement results---- */
		odm_set_bb_reg(p_dm, REG_PDP_ANT_A, MASKDWORD, 0x0c297018);
		tmp_reg = odm_get_bb_reg(p_dm, 0xbe0, MASKDWORD);
		ODM_delay_ms(10);
		odm_set_bb_reg(p_dm, REG_PDP_ANT_A, MASKDWORD, 0x0c29701f);
		tmp_reg2 = odm_get_bb_reg(p_dm, 0xbe8, MASKDWORD);
		ODM_delay_ms(10);

		tmp_reg = (tmp_reg & MASKHWORD) >> 16;
		tmp_reg2 = (tmp_reg2 & MASKHWORD) >> 16;
		if (tmp_reg < 0xf0 || tmp_reg > 0x105 || tmp_reg2 > 0xff) {
			odm_set_bb_reg(p_dm, REG_PDP_ANT_A, MASKDWORD, 0x02017098);

			odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x800000);
			odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
			ODM_delay_ms(1);
			odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_A, MASKDWORD, 0x800477c0);
			ODM_delay_ms(1);
			odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_A, MASKDWORD, 0x000477c0);
			ODM_delay_ms(50);
			retry_count++;
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path A DPK retry_count %d 0xbe0[31:16] %x 0xbe8[31:16] %x\n", retry_count, tmp_reg, tmp_reg2));
		} else {
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path A DPK Sucess\n"));
			p_rf_calibrate_info->is_dp_path_aok = true;
			break;
		}
	}
	retry_count = 0;

	/* DPP path A */
	if (p_rf_calibrate_info->is_dp_path_aok) {
		/* DP settings */
		odm_set_bb_reg(p_dm, REG_PDP_ANT_A, MASKDWORD, 0x01017098);
		odm_set_bb_reg(p_dm, REG_PDP_ANT_A_4, MASKDWORD, 0x776d9f84);
		odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_A, MASKDWORD, 0x0004ab87);
		odm_set_bb_reg(p_dm, REG_CONFIG_ANT_A, MASKDWORD, 0x00880000);
		odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x400000);

		for (i = REG_PDP_ANT_A; i <= 0xb3c; i += 4) {
			odm_set_bb_reg(p_dm, i, MASKDWORD, 0x40004000);
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path A ofsset = 0x%x\n", i));
		}

		/* pwsf */
		odm_set_bb_reg(p_dm, 0xb40, MASKDWORD, 0x40404040);
		odm_set_bb_reg(p_dm, 0xb44, MASKDWORD, 0x28324040);
		odm_set_bb_reg(p_dm, 0xb48, MASKDWORD, 0x10141920);

		for (i = 0xb4c; i <= 0xb5c; i += 4)
			odm_set_bb_reg(p_dm, i, MASKDWORD, 0x0c0c0c0c);

		/* TX_AGC boundary */
		odm_set_bb_reg(p_dm, 0xbc0, MASKDWORD, 0x0005361f);
		odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	} else {
		odm_set_bb_reg(p_dm, REG_PDP_ANT_A, MASKDWORD, 0x00000000);
		odm_set_bb_reg(p_dm, REG_PDP_ANT_A_4, MASKDWORD, 0x00000000);
	}

	/* DPK path B */
	if (is2T) {
		/* path A to standby mode */
		odm_set_rf_reg(p_dm, RF_PATH_A, RF_AC, MASKDWORD, 0x10000);

		/* LUTs => tx_agc */
		/* PA gain = 11 & PAD1, => tx_agc 1f ~11 */
		/* PA gain = 11 & PAD2, => tx_agc 10 ~0e */
		/* PA gain = 01 => tx_agc 0b ~0d */
		/* PA gain = 00 => tx_agc 0a ~00 */
		odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x400000);
		odm_set_bb_reg(p_dm, 0xbc4, MASKDWORD, 0x0005361f);
		odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

		/* do inner loopback DPK 3 times */
		for (i = 0; i < 3; i++) {
			/* PA gain = 11 & PAD2 => tx_agc = 0x0f/0x0c/0x07 */
			for (index = 0; index < 4; index++)
				odm_set_bb_reg(p_dm, 0x830 + index * 4, MASKDWORD, tx_agc[i][0]);
			for (index = 0; index < 2; index++)
				odm_set_bb_reg(p_dm, 0x848 + index * 4, MASKDWORD, tx_agc[i][0]);
			for (index = 0; index < 2; index++)
				odm_set_bb_reg(p_dm, 0x868 + index * 4, MASKDWORD, tx_agc[i][0]);

			/* PAGE_B for path-A inner loopback DPK setting */
			odm_set_bb_reg(p_dm, REG_PDP_ANT_B, MASKDWORD, 0x02097098);
			odm_set_bb_reg(p_dm, REG_PDP_ANT_B_4, MASKDWORD, 0xf76d9f84);
			odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_B, MASKDWORD, 0x0004ab87);
			odm_set_bb_reg(p_dm, REG_CONFIG_ANT_B, MASKDWORD, 0x00880000);

			/* ----send one shot signal---- */
			/* path B */
			odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_B, MASKDWORD, 0x80047788);
			ODM_delay_ms(1);
			odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_B, MASKDWORD, 0x00047788);
			ODM_delay_ms(50);
		}

		/* PA gain = 11 => tx_agc = 1a */
		for (index = 0; index < 4; index++)
			odm_set_bb_reg(p_dm, 0x830 + index * 4, MASKDWORD, 0x34343434);
		for (index = 0; index < 2; index++)
			odm_set_bb_reg(p_dm, 0x848 + index * 4, MASKDWORD, 0x34343434);
		for (index = 0; index < 2; index++)
			odm_set_bb_reg(p_dm, 0x868 + index * 4, MASKDWORD, 0x34343434);

		/* PAGE_B for path-B DPK setting */
		odm_set_bb_reg(p_dm, REG_PDP_ANT_B, MASKDWORD, 0x02017098);
		odm_set_bb_reg(p_dm, REG_PDP_ANT_B_4, MASKDWORD, 0xf76d9f84);
		odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_B, MASKDWORD, 0x0004ab87);
		odm_set_bb_reg(p_dm, REG_CONFIG_ANT_B, MASKDWORD, 0x00880000);

		/* RF lpbk switches on */
		odm_set_bb_reg(p_dm, 0x840, MASKDWORD, 0x0101000f);
		odm_set_bb_reg(p_dm, 0x840, MASKDWORD, 0x01120103);

		/* path-B RF lpbk */
		odm_set_rf_reg(p_dm, RF_PATH_B, 0x0c, MASKDWORD, 0x8992b);
		odm_set_rf_reg(p_dm, RF_PATH_B, 0x0d, MASKDWORD, 0x0e52c);
		odm_set_rf_reg(p_dm, RF_PATH_B, RF_AC, MASKDWORD, 0x5205a);

		/* ----send one shot signal---- */
		odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_B, MASKDWORD, 0x800477c0);
		ODM_delay_ms(1);
		odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_B, MASKDWORD, 0x000477c0);
		ODM_delay_ms(50);

		while (retry_count < DP_RETRY_LIMIT && !p_rf_calibrate_info->is_dp_path_bok) {
			/* ----read back measurement results---- */
			odm_set_bb_reg(p_dm, REG_PDP_ANT_B, MASKDWORD, 0x0c297018);
			tmp_reg = odm_get_bb_reg(p_dm, 0xbf0, MASKDWORD);
			odm_set_bb_reg(p_dm, REG_PDP_ANT_B, MASKDWORD, 0x0c29701f);
			tmp_reg2 = odm_get_bb_reg(p_dm, 0xbf8, MASKDWORD);

			tmp_reg = (tmp_reg & MASKHWORD) >> 16;
			tmp_reg2 = (tmp_reg2 & MASKHWORD) >> 16;

			if (tmp_reg < 0xf0 || tmp_reg > 0x105 || tmp_reg2 > 0xff) {
				odm_set_bb_reg(p_dm, REG_PDP_ANT_B, MASKDWORD, 0x02017098);

				odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x800000);
				odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
				ODM_delay_ms(1);
				odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_B, MASKDWORD, 0x800477c0);
				ODM_delay_ms(1);
				odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_B, MASKDWORD, 0x000477c0);
				ODM_delay_ms(50);
				retry_count++;
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path B DPK retry_count %d 0xbf0[31:16] %x, 0xbf8[31:16] %x\n", retry_count, tmp_reg, tmp_reg2));
			} else {
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path B DPK Success\n"));
				p_rf_calibrate_info->is_dp_path_bok = true;
				break;
			}
		}

		/* DPP path B */
		if (p_rf_calibrate_info->is_dp_path_bok) {
			/* DP setting */
			/* LUT by SRAM */
			odm_set_bb_reg(p_dm, REG_PDP_ANT_B, MASKDWORD, 0x01017098);
			odm_set_bb_reg(p_dm, REG_PDP_ANT_B_4, MASKDWORD, 0x776d9f84);
			odm_set_bb_reg(p_dm, REG_CONFIG_PMPD_ANT_B, MASKDWORD, 0x0004ab87);
			odm_set_bb_reg(p_dm, REG_CONFIG_ANT_B, MASKDWORD, 0x00880000);

			odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x400000);
			for (i = 0xb60; i <= 0xb9c; i += 4) {
				odm_set_bb_reg(p_dm, i, MASKDWORD, 0x40004000);
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path B ofsset = 0x%x\n", i));
			}

			/* PWSF */
			odm_set_bb_reg(p_dm, 0xba0, MASKDWORD, 0x40404040);
			odm_set_bb_reg(p_dm, 0xba4, MASKDWORD, 0x28324050);
			odm_set_bb_reg(p_dm, 0xba8, MASKDWORD, 0x0c141920);

			for (i = 0xbac; i <= 0xbbc; i += 4)
				odm_set_bb_reg(p_dm, i, MASKDWORD, 0x0c0c0c0c);

			/* tx_agc boundary */
			odm_set_bb_reg(p_dm, 0xbc4, MASKDWORD, 0x0005361f);
			odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

		} else {
			odm_set_bb_reg(p_dm, REG_PDP_ANT_B, MASKDWORD, 0x00000000);
			odm_set_bb_reg(p_dm, REG_PDP_ANT_B_4, MASKDWORD, 0x00000000);
		}
	}

	/* reload BB default value */
	for (index = 0; index < DP_BB_REG_NUM; index++)
		odm_set_bb_reg(p_dm, BB_REG[index], MASKDWORD, BB_backup[index]);

	/* reload RF default value */
	for (path = 0; path < DP_PATH_NUM; path++) {
		for (i = 0 ; i < DP_RF_REG_NUM ; i++)
			odm_set_rf_reg(p_dm, path, RF_REG[i], MASKDWORD, RF_backup[path][i]);
	}
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_MODE1, MASKDWORD, 0x1000f);	/* standby mode */
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_MODE2, MASKDWORD, 0x20101);		/* RF lpbk switches off */

	/* reload AFE default value */
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_phy_reload_adda_registers8723b(p_adapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);

	/* reload MAC default value */
	_phy_reload_mac_registers8723b(p_adapter, MAC_REG, MAC_backup);
#else
	_phy_reload_adda_registers8723b(p_dm, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);

	/* reload MAC default value */
	_phy_reload_mac_registers8723b(p_dm, MAC_REG, MAC_backup);
#endif

	p_rf_calibrate_info->is_dp_done = true;
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("<==_phy_digital_predistortion8723b()\n"));
#endif
}

void
phy_digital_predistortion_8723b(
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct _ADAPTER	*p_adapter
#else
	struct PHY_DM_STRUCT	*p_dm
#endif
)
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->DM_OutSrc;
#endif
#endif
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
#if DISABLE_BB_RF
	return;
#endif

	return;

	if (p_rf_calibrate_info->is_dp_done)
		return;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)

	if (IS_92C_SERIAL(p_hal_data->VersionID))
		_phy_digital_predistortion8723b(p_adapter, true);
	else
#endif
	{
		/* For 88C 1T1R */
		_phy_digital_predistortion8723b(p_adapter, false);
	}
}
#endif


/* return value true => Main; false => Aux */

boolean _phy_query_rf_path_switch_8723b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	boolean		is2T
)
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->DM_OutSrc;
#endif
#endif


	if (odm_get_bb_reg(p_dm, 0x92C, MASKDWORD) == 0x01)
		return true;
	else
		return false;
}

/* return value true => Main; false => Aux */
boolean phy_query_rf_path_switch_8723b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm
#else
	struct _ADAPTER	*p_adapter
#endif
)
{
#if DISABLE_BB_RF
	return true;
#endif

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	return _phy_query_rf_path_switch_8723b(p_adapter, false);
#else
	return _phy_query_rf_path_switch_8723b(p_dm, false);
#endif

}
#endif
