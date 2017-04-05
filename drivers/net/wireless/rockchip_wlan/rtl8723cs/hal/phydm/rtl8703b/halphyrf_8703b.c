/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/

#include "mp_precomp.h"
#include "../phydm_precomp.h"



/*---------------------------Define Local Constant---------------------------*/
/* IQK */
#define IQK_DELAY_TIME_8703B	10
#define LCK_DELAY_TIME_8703B	100

/* LTE_COEX */
#define REG_LTECOEX_CTRL 0x07C0
#define REG_LTECOEX_WRITE_DATA 0x07C4
#define REG_LTECOEX_READ_DATA 0x07C8
#define REG_LTECOEX_PATH_CONTROL 0x70



/* 2010/04/25 MH Define the max tx power tracking tx agc power. */
#define		ODM_TXPWRTRACK_MAX_IDX8703B		6

#define     idx_0xc94                       0
#define     idx_0xc80                       1
#define     idx_0xc4c                       2

#define     idx_0xc14                       0
#define     idx_0xca0                       1

#define     KEY                             0
#define     VAL                             1

/*---------------------------Define Local Constant---------------------------*/


/* 3============================================================
 * 3 Tx Power Tracking
 * 3============================================================ */


void set_iqk_matrix_8703b(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u8		OFDM_index,
	u8		rf_path,
	s32		iqk_result_x,
	s32		iqk_result_y
)
{
	s32			ele_A = 0, ele_D = 0, ele_C = 0, value32, tmp;
	s32			ele_A_ext = 0, ele_C_ext = 0, ele_D_ext = 0;

	rf_path = ODM_RF_PATH_A;


	if (OFDM_index >= OFDM_TABLE_SIZE)
		OFDM_index = OFDM_TABLE_SIZE - 1;
	else if (OFDM_index < 0)
		OFDM_index = 0;

	if ((iqk_result_x != 0) && (*(p_dm_odm->p_band_type) == ODM_BAND_2_4G)) {

		/* new element D */
		ele_D = (ofdm_swing_table_new[OFDM_index] & 0xFFC00000) >> 22;
		ele_D_ext = (((iqk_result_x * ele_D) >> 7) & 0x01);

		/* new element A */
		if ((iqk_result_x & 0x00000200) != 0)		/* consider minus */
			iqk_result_x = iqk_result_x | 0xFFFFFC00;
		ele_A = ((iqk_result_x * ele_D) >> 8) & 0x000003FF;
		ele_A_ext = ((iqk_result_x * ele_D) >> 7) & 0x1;
		/* new element C */
		if ((iqk_result_y & 0x00000200) != 0)
			iqk_result_y = iqk_result_y | 0xFFFFFC00;
		ele_C = ((iqk_result_y * ele_D) >> 8) & 0x000003FF;
		ele_C_ext = ((iqk_result_y * ele_D) >> 7) & 0x1;

		switch (rf_path) {
		case ODM_RF_PATH_A:
			/* write new elements A, C, D to regC80, regC94, reg0xc4c, and element B is always 0 */
			/* write 0xc80 */
			value32 = (ele_D << 22) | ((ele_C & 0x3F) << 16) | ele_A;
			odm_set_bb_reg(p_dm_odm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD, value32);
			/* write 0xc94 */
			value32 = (ele_C & 0x000003C0) >> 6;
			odm_set_bb_reg(p_dm_odm, REG_OFDM_0_XC_TX_AFE, MASKH4BITS, value32);
			/* write 0xc4c */
			value32 = (ele_D_ext << 28) | (ele_A_ext << 31) | (ele_C_ext << 29);
			value32 = (odm_get_bb_reg(p_dm_odm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD) & (~(BIT(31) | BIT(29) | BIT(28)))) | value32;
			odm_set_bb_reg(p_dm_odm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD, value32);
			break;
		case ODM_RF_PATH_B:
			/* write new elements A, C, D to regC88, regC9C, regC4C, and element B is always 0 */
			/* write 0xc88 */
			value32 = (ele_D << 22) | ((ele_C & 0x3F) << 16) | ele_A;
			odm_set_bb_reg(p_dm_odm, REG_OFDM_0_XB_TX_IQ_IMBALANCE, MASKDWORD, value32);
			/* write 0xc9c */
			value32 = (ele_C & 0x000003C0) >> 6;
			odm_set_bb_reg(p_dm_odm, REG_OFDM_0_XD_TX_AFE, MASKH4BITS, value32);
			/* write 0xc4c */
			value32 = (ele_D_ext << 24) | (ele_A_ext << 27) | (ele_C_ext << 25);
			value32 = (odm_get_bb_reg(p_dm_odm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD) & (~(BIT(24) | BIT(27) | BIT(25)))) | value32;
			odm_set_bb_reg(p_dm_odm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD, value32);
			break;
		default:
			break;
		}
	} else {
		switch (rf_path) {
		case ODM_RF_PATH_A:
			odm_set_bb_reg(p_dm_odm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD, ofdm_swing_table_new[OFDM_index]);
			odm_set_bb_reg(p_dm_odm, REG_OFDM_0_XC_TX_AFE, MASKH4BITS, 0x00);
			value32 = odm_get_bb_reg(p_dm_odm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD) & (~(BIT(31) | BIT(29) | BIT(28)));
			odm_set_bb_reg(p_dm_odm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD, value32);
			break;

		case ODM_RF_PATH_B:
			odm_set_bb_reg(p_dm_odm, REG_OFDM_0_XB_TX_IQ_IMBALANCE, MASKDWORD, ofdm_swing_table_new[OFDM_index]);
			odm_set_bb_reg(p_dm_odm, REG_OFDM_0_XD_TX_AFE, MASKH4BITS, 0x00);
			value32 = odm_get_bb_reg(p_dm_odm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD) & (~(BIT(24) | BIT(27) | BIT(25)));
			odm_set_bb_reg(p_dm_odm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD, value32);
			break;

		default:
			break;
		}
	}

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("TxPwrTracking path %c: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x ele_A_ext = 0x%x ele_C_ext = 0x%x ele_D_ext = 0x%x\n",
		(rf_path == ODM_RF_PATH_A ? 'A' : 'B'), (u32)iqk_result_x, (u32)iqk_result_y, (u32)ele_A, (u32)ele_C, (u32)ele_D, (u32)ele_A_ext, (u32)ele_C_ext, (u32)ele_D_ext));
}

void
set_cck_filter_coefficient_8703b(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u8		cck_swing_index
)
{
	odm_write_1byte(p_dm_odm, 0xa22, cck_swing_table_ch1_ch14_88f[cck_swing_index][0]);
	odm_write_1byte(p_dm_odm, 0xa23, cck_swing_table_ch1_ch14_88f[cck_swing_index][1]);
	odm_write_1byte(p_dm_odm, 0xa24, cck_swing_table_ch1_ch14_88f[cck_swing_index][2]);
	odm_write_1byte(p_dm_odm, 0xa25, cck_swing_table_ch1_ch14_88f[cck_swing_index][3]);
	odm_write_1byte(p_dm_odm, 0xa26, cck_swing_table_ch1_ch14_88f[cck_swing_index][4]);
	odm_write_1byte(p_dm_odm, 0xa27, cck_swing_table_ch1_ch14_88f[cck_swing_index][5]);
	odm_write_1byte(p_dm_odm, 0xa28, cck_swing_table_ch1_ch14_88f[cck_swing_index][6]);
	odm_write_1byte(p_dm_odm, 0xa29, cck_swing_table_ch1_ch14_88f[cck_swing_index][7]);
	odm_write_1byte(p_dm_odm, 0xa9a, cck_swing_table_ch1_ch14_88f[cck_swing_index][8]);
	odm_write_1byte(p_dm_odm, 0xa9b, cck_swing_table_ch1_ch14_88f[cck_swing_index][9]);
	odm_write_1byte(p_dm_odm, 0xa9c, cck_swing_table_ch1_ch14_88f[cck_swing_index][10]);
	odm_write_1byte(p_dm_odm, 0xa9d, cck_swing_table_ch1_ch14_88f[cck_swing_index][11]);
	odm_write_1byte(p_dm_odm, 0xaa0, cck_swing_table_ch1_ch14_88f[cck_swing_index][12]);
	odm_write_1byte(p_dm_odm, 0xaa1, cck_swing_table_ch1_ch14_88f[cck_swing_index][13]);
	odm_write_1byte(p_dm_odm, 0xaa2, cck_swing_table_ch1_ch14_88f[cck_swing_index][14]);
	odm_write_1byte(p_dm_odm, 0xaa3, cck_swing_table_ch1_ch14_88f[cck_swing_index][15]);
}

void do_iqk_8703b(
	void		*p_dm_void,
	u8		delta_thermal_index,
	u8		thermal_value,
	u8		threshold
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
#endif

	odm_reset_iqk_result(p_dm_odm);


	p_dm_odm->rf_calibrate_info.thermal_value_iqk = thermal_value;
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	phy_iq_calibrate_8703b(p_dm_odm, false);
#else
	phy_iq_calibrate_8703b(adapter, false);
#endif


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
odm_tx_pwr_track_set_pwr_8703b(
	void		*p_dm_void,
	enum pwrtrack_method	method,
	u8				rf_path,
	u8				channel_mapped_index
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER	*adapter = p_dm_odm->adapter;
	PHAL_DATA_TYPE	p_hal_data = GET_HAL_DATA(adapter);
	u8		pwr_tracking_limit_ofdm = 34; /* +0dB */
	u8		pwr_tracking_limit_cck = CCK_TABLE_SIZE_88F - 1;   /* -2dB */
	u8		tx_rate = 0xFF;
	u8		final_ofdm_swing_index = 0;
	u8		final_cck_swing_index = 0;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
#if (MP_DRIVER == 1)	/*win MP */
	PMPT_CONTEXT p_mpt_ctx = &(adapter->MptCtx);

	tx_rate = MptToMgntRate(p_mpt_ctx->MptRateIndex);
#else	/*win normal*/
	PMGNT_INFO		p_mgnt_info = &(adapter->MgntInfo);
	if (!p_mgnt_info->ForcedDataRate) {	/*auto rate*/
			tx_rate = adapter->HalFunc.GetHwRateFromMRateHandler(p_dm_odm->tx_rate);
	} else
		tx_rate = (u8) p_mgnt_info->ForcedDataRate;
#endif
#elif (DM_ODM_SUPPORT_TYPE & (ODM_CE))
	if (p_dm_odm->mp_mode == true) {	/*CE MP*/
		PMPT_CONTEXT		p_mpt_ctx = &(adapter->mppriv.mpt_ctx);

		tx_rate = mpt_to_mgnt_rate(p_mpt_ctx->mpt_rate_index);
	} else {	/*CE normal*/
		u16	rate	 = *(p_dm_odm->p_forced_data_rate);

		if (!rate) {	/*auto rate*/
			if (p_dm_odm->number_linked_client != 0)
				tx_rate = hw_rate_to_m_rate(p_dm_odm->tx_rate);
		} else 	/*force rate*/
			tx_rate = (u8)rate;
	}
#endif


	ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("===>ODM_TxPwrTrackSetPwr8703B\n"));

	if (tx_rate != 0xFF) {
		/*2 CCK*/
		if (((tx_rate >= MGN_1M) && (tx_rate <= MGN_5_5M)) || (tx_rate == MGN_11M))
			pwr_tracking_limit_cck = CCK_TABLE_SIZE_88F - 1;
		/*2 OFDM*/
		else if ((tx_rate >= MGN_6M) && (tx_rate <= MGN_48M))
			pwr_tracking_limit_ofdm = 36;	/*+3dB*/
		else if (tx_rate == MGN_54M)
			pwr_tracking_limit_ofdm = 34;	/*+2dB*/
		/*2 HT*/
		else if ((tx_rate >= MGN_MCS0) && (tx_rate <= MGN_MCS2))	/*QPSK/BPSK*/
			pwr_tracking_limit_ofdm = 38;	/*+4dB*/
		else if ((tx_rate >= MGN_MCS3) && (tx_rate <= MGN_MCS4))	/*16QAM*/
			pwr_tracking_limit_ofdm = 36;	/*+3dB*/
		else if ((tx_rate >= MGN_MCS5) && (tx_rate <= MGN_MCS7))	/*64QAM*/
			pwr_tracking_limit_ofdm = 34;	/*+2dB*/
		else
			pwr_tracking_limit_ofdm =  p_rf_calibrate_info->default_ofdm_index;   /*Default OFDM index = 30*/
	}
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("tx_rate=0x%x, pwr_tracking_limit=%d\n", tx_rate, pwr_tracking_limit_ofdm));

	if (method == TXAGC) {
		u32	pwr = 0, tx_agc = 0;
		struct _ADAPTER *adapter = p_dm_odm->adapter;

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("odm_TxPwrTrackSetPwr8703B CH=%d\n", *(p_dm_odm->p_channel)));

		p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path] = p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path];

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (MP_DRIVER != 1)
		p_rf_calibrate_info->modify_tx_agc_flag_path_a = true;
		p_rf_calibrate_info->modify_tx_agc_flag_path_a_cck = true;

		odm_set_tx_power_index_by_rate_section(p_dm_odm, rf_path, *p_dm_odm->p_channel, CCK);
		odm_set_tx_power_index_by_rate_section(p_dm_odm, rf_path, *p_dm_odm->p_channel, OFDM);
		odm_set_tx_power_index_by_rate_section(p_dm_odm, rf_path, *p_dm_odm->p_channel, HT_MCS0_MCS7);
#else
		pwr = odm_get_bb_reg(p_dm_odm, REG_TX_AGC_A_RATE18_06, 0xFF);
		pwr += p_rf_calibrate_info->power_index_offset[rf_path];
		odm_set_bb_reg(p_dm_odm, REG_TX_AGC_A_CCK_1_MCS32, MASKBYTE1, pwr);
		tx_agc = (pwr << 16) | (pwr << 8) | (pwr);
		odm_set_bb_reg(p_dm_odm, REG_TX_AGC_B_CCK_11_A_CCK_2_11, 0xffffff00, tx_agc);
		RT_DISP(FPHY, PHY_TXPWR, ("ODM_TxPwrTrackSetPwr8703B: CCK Tx-rf(A) Power = 0x%x\n", tx_agc));

		pwr = odm_get_bb_reg(p_dm_odm, REG_TX_AGC_A_RATE18_06, 0xFF);
		pwr += (p_rf_calibrate_info->bb_swing_idx_ofdm[rf_path] - p_rf_calibrate_info->bb_swing_idx_ofdm_base[rf_path]);
		tx_agc |= ((pwr << 24) | (pwr << 16) | (pwr << 8) | pwr);
		odm_set_bb_reg(p_dm_odm, REG_TX_AGC_A_RATE18_06, MASKDWORD, tx_agc);
		odm_set_bb_reg(p_dm_odm, REG_TX_AGC_A_RATE54_24, MASKDWORD, tx_agc);
		odm_set_bb_reg(p_dm_odm, REG_TX_AGC_A_MCS03_MCS00, MASKDWORD, tx_agc);
		odm_set_bb_reg(p_dm_odm, REG_TX_AGC_A_MCS07_MCS04, MASKDWORD, tx_agc);
		odm_set_bb_reg(p_dm_odm, REG_TX_AGC_A_MCS11_MCS08, MASKDWORD, tx_agc);
		odm_set_bb_reg(p_dm_odm, REG_TX_AGC_A_MCS15_MCS12, MASKDWORD, tx_agc);
		RT_DISP(FPHY, PHY_TXPWR, ("ODM_TxPwrTrackSetPwr8703B: OFDM Tx-rf(A) Power = 0x%x\n", tx_agc));
#endif
#endif
	} else if (method == BBSWING) {
		final_ofdm_swing_index = p_rf_calibrate_info->default_ofdm_index + p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path];
		final_cck_swing_index = p_rf_calibrate_info->default_cck_index + p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path];

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			(" p_rf_calibrate_info->default_ofdm_index=%d,  p_rf_calibrate_info->DefaultCCKIndex=%d, p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path]=%d, p_rf_calibrate_info->remnant_cck_swing_idx=%d   rf_path = %d\n",
			p_rf_calibrate_info->default_ofdm_index, p_rf_calibrate_info->default_cck_index, p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path], p_rf_calibrate_info->remnant_cck_swing_idx, rf_path));

		/* Adjust BB swing by OFDM IQ matrix */
		if (final_ofdm_swing_index >= pwr_tracking_limit_ofdm)
			final_ofdm_swing_index = pwr_tracking_limit_ofdm;
		else if (final_ofdm_swing_index < 0)
			final_ofdm_swing_index = 0;

		if (final_cck_swing_index >= CCK_TABLE_SIZE)
			final_cck_swing_index = CCK_TABLE_SIZE - 1;
		else if (p_rf_calibrate_info->bb_swing_idx_cck < 0)
			final_cck_swing_index = 0;

		set_iqk_matrix_8703b(p_dm_odm, final_ofdm_swing_index, ODM_RF_PATH_A,
			p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
			p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][1]);

		set_cck_filter_coefficient_8703b(p_dm_odm, final_cck_swing_index);

	} else if (method == MIX_MODE) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			(" p_dm_odm->default_ofdm_index=%d,  p_dm_odm->DefaultCCKIndex=%d, p_dm_odm->absolute_ofdm_swing_idx[rf_path]=%d, p_dm_odm->remnant_cck_swing_idx=%d   rf_path = %d\n",
			p_rf_calibrate_info->default_ofdm_index, p_rf_calibrate_info->default_cck_index, p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path], p_rf_calibrate_info->remnant_cck_swing_idx, rf_path));

		final_ofdm_swing_index = p_rf_calibrate_info->default_ofdm_index + p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path];
		final_cck_swing_index  = p_rf_calibrate_info->default_cck_index + p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path];

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			(" p_dm_odm->default_ofdm_index=%d,  p_dm_odm->DefaultCCKIndex=%d, p_dm_odm->absolute_ofdm_swing_idx[rf_path]=%d   rf_path = %d\n",
			p_rf_calibrate_info->default_ofdm_index, p_rf_calibrate_info->default_cck_index, p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path], rf_path));

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			(" final_ofdm_swing_index=%d,  final_cck_swing_index=%d rf_path=%d\n",
			final_ofdm_swing_index, final_cck_swing_index, rf_path));


		if (final_ofdm_swing_index > pwr_tracking_limit_ofdm) {   /*BBSwing higher then Limit*/
			p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path] = final_ofdm_swing_index - pwr_tracking_limit_ofdm;

			set_iqk_matrix_8703b(p_dm_odm, pwr_tracking_limit_ofdm, ODM_RF_PATH_A,
				p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
				p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][1]);

			p_rf_calibrate_info->modify_tx_agc_flag_path_a = true;
			odm_set_tx_power_index_by_rate_section(p_dm_odm, rf_path, *p_dm_odm->p_channel, OFDM);
			odm_set_tx_power_index_by_rate_section(p_dm_odm, rf_path, *p_dm_odm->p_channel, HT_MCS0_MCS7);

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				(" ******Path_A Over BBSwing Limit, pwr_tracking_limit = %d, Remnant tx_agc value = %d\n",
				pwr_tracking_limit_ofdm, p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path]));
		} else if (final_ofdm_swing_index < 0) {
			p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path] = final_ofdm_swing_index;

			set_iqk_matrix_8703b(p_dm_odm, 0, ODM_RF_PATH_A,
				p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
				p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][1]);

			p_rf_calibrate_info->modify_tx_agc_flag_path_a = true;
			odm_set_tx_power_index_by_rate_section(p_dm_odm, rf_path, *p_dm_odm->p_channel, OFDM);
			odm_set_tx_power_index_by_rate_section(p_dm_odm, rf_path, *p_dm_odm->p_channel, HT_MCS0_MCS7);

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				(" ******Path_A Lower then BBSwing lower bound  0, Remnant tx_agc value = %d\n",
				p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path]));
		} else {
			set_iqk_matrix_8703b(p_dm_odm, final_ofdm_swing_index, ODM_RF_PATH_A,
				p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
				p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][1]);

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				(" ******Path_A Compensate with BBSwing, final_ofdm_swing_index = %d\n", final_ofdm_swing_index));

			if (p_rf_calibrate_info->modify_tx_agc_flag_path_a) {		/*If tx_agc has changed, reset tx_agc again*/
				p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path] = 0;
				odm_set_tx_power_index_by_rate_section(p_dm_odm, rf_path, *p_dm_odm->p_channel, OFDM);
				odm_set_tx_power_index_by_rate_section(p_dm_odm, rf_path, *p_dm_odm->p_channel, HT_MCS0_MCS7);
				p_rf_calibrate_info->modify_tx_agc_flag_path_a = false;

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
					(" ******Path_A p_dm_odm->Modify_TxAGC_Flag = false\n"));
			}
		}
		if (final_cck_swing_index > pwr_tracking_limit_cck) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				(" final_cck_swing_index(%d) > pwr_tracking_limit_cck(%d)\n", final_cck_swing_index, pwr_tracking_limit_cck));

			p_rf_calibrate_info->remnant_cck_swing_idx = final_cck_swing_index - pwr_tracking_limit_cck;

			set_cck_filter_coefficient_8703b(p_dm_odm, pwr_tracking_limit_cck);

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A CCK Over Limit, pwr_tracking_limit_cck = %d, p_dm_odm->remnant_cck_swing_idx  = %d\n", pwr_tracking_limit_cck, p_rf_calibrate_info->remnant_cck_swing_idx));

			p_rf_calibrate_info->modify_tx_agc_flag_path_a_cck = true;

			odm_set_tx_power_index_by_rate_section(p_dm_odm, ODM_RF_PATH_A, *p_dm_odm->p_channel, CCK);

		} else if (final_cck_swing_index < 0) {		/* Lowest CCK index = 0 */

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				(" final_cck_swing_index(%d) < 0      pwr_tracking_limit_cck(%d)\n", final_cck_swing_index, pwr_tracking_limit_cck));

			p_rf_calibrate_info->remnant_cck_swing_idx = final_cck_swing_index;

			set_cck_filter_coefficient_8703b(p_dm_odm, 0);

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A CCK Under Limit, pwr_tracking_limit_cck = %d, p_dm_odm->remnant_cck_swing_idx  = %d\n", 0, p_rf_calibrate_info->remnant_cck_swing_idx));

			p_rf_calibrate_info->modify_tx_agc_flag_path_a_cck = true;

			odm_set_tx_power_index_by_rate_section(p_dm_odm, ODM_RF_PATH_A, *p_dm_odm->p_channel, CCK);

		} else {

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
				(" else final_cck_swing_index=%d      pwr_tracking_limit_cck(%d)\n", final_cck_swing_index, pwr_tracking_limit_cck));

			set_cck_filter_coefficient_8703b(p_dm_odm, final_cck_swing_index);

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A CCK Compensate with BBSwing, final_cck_swing_index = %d\n", final_cck_swing_index));

			p_rf_calibrate_info->modify_tx_agc_flag_path_a_cck = false;

			p_rf_calibrate_info->remnant_cck_swing_idx = 0;

			if (p_rf_calibrate_info->modify_tx_agc_flag_path_a_cck) {		/*If tx_agc has changed, reset tx_agc again*/
				p_rf_calibrate_info->remnant_cck_swing_idx = 0;
				odm_set_tx_power_index_by_rate_section(p_dm_odm, ODM_RF_PATH_A, *p_dm_odm->p_channel, CCK);
				p_rf_calibrate_info->modify_tx_agc_flag_path_a_cck = false;
			}



		}

	} else {
		return; /* This method is not supported. */
	}
}

void
get_delta_swing_table_8703b(
	void		*p_dm_void,
	u8 **temperature_up_a,
	u8 **temperature_down_a,
	u8 **temperature_up_b,
	u8 **temperature_down_b
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER		*adapter		 = p_dm_odm->adapter;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);
	HAL_DATA_TYPE	*p_hal_data		 = GET_HAL_DATA(adapter);
	u8			tx_rate			= 0xFF;
	u8			channel		 = *p_dm_odm->p_channel;


	if (p_dm_odm->mp_mode == true) {
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
#if (MP_DRIVER == 1)
		PMPT_CONTEXT p_mpt_ctx = &(adapter->MptCtx);

		tx_rate = MptToMgntRate(p_mpt_ctx->MptRateIndex);
#endif
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
		PMPT_CONTEXT p_mpt_ctx = &(adapter->mppriv.mpt_ctx);

		tx_rate = mpt_to_mgnt_rate(p_mpt_ctx->mpt_rate_index);
#endif
#endif
	} else {
		u16	rate	 = *(p_dm_odm->p_forced_data_rate);

		if (!rate) { /*auto rate*/
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
			tx_rate = adapter->HalFunc.GetHwRateFromMRateHandler(p_dm_odm->tx_rate);
#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
			if (p_dm_odm->number_linked_client != 0)
				tx_rate = hw_rate_to_m_rate(p_dm_odm->tx_rate);
#endif
		} else   /*force rate*/
			tx_rate = (u8)rate;
	}

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("Power Tracking tx_rate=0x%X\n", tx_rate));

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



void
get_delta_swing_xtal_table_8703b(
	void		*p_dm_void,
	s8 **temperature_up_xtal,
	s8 **temperature_down_xtal
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);

	*temperature_up_xtal   = p_rf_calibrate_info->delta_swing_table_xtal_p;
	*temperature_down_xtal = p_rf_calibrate_info->delta_swing_table_xtal_n;
}



void
odm_txxtaltrack_set_xtal_8703b(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm		= (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info	= &(p_dm_odm->rf_calibrate_info);
	struct _ADAPTER		*adapter			= p_dm_odm->adapter;
	HAL_DATA_TYPE	*p_hal_data		 = GET_HAL_DATA(adapter);

	s8	crystal_cap;


	crystal_cap = p_hal_data->crystal_cap & 0x3F;
	crystal_cap = crystal_cap + p_rf_calibrate_info->xtal_offset;

	if (crystal_cap < 0)
		crystal_cap = 0;
	else if (crystal_cap > 63)
		crystal_cap = 63;


	ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("crystal_cap(%d)= p_hal_data->crystal_cap(%d) + p_rf_calibrate_info->xtal_offset(%d)\n", crystal_cap, p_hal_data->crystal_cap, p_rf_calibrate_info->xtal_offset));

	odm_set_bb_reg(p_dm_odm, REG_MAC_PHY_CTRL, 0xFFF000, (crystal_cap | (crystal_cap << 6)));

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("crystal_cap(0x2c)  0x%X\n", odm_get_bb_reg(p_dm_odm, REG_MAC_PHY_CTRL, 0xFFF000)));


}



void configure_txpower_track_8703b(
	struct _TXPWRTRACK_CFG	*p_config
)
{
	p_config->swing_table_size_cck = CCK_TABLE_SIZE;
	p_config->swing_table_size_ofdm = OFDM_TABLE_SIZE;
	p_config->threshold_iqk = IQK_THRESHOLD;
	p_config->average_thermal_num = AVG_THERMAL_NUM_8703B;
	p_config->rf_path_count = MAX_PATH_NUM_8703B;
	p_config->thermal_reg_addr = RF_T_METER_8703B;

	p_config->odm_tx_pwr_track_set_pwr = odm_tx_pwr_track_set_pwr_8703b;
	p_config->do_iqk = do_iqk_8703b;
	p_config->phy_lc_calibrate = phy_lc_calibrate_8703b;
	p_config->get_delta_swing_table = get_delta_swing_table_8703b;
	p_config->get_delta_swing_xtal_table = get_delta_swing_xtal_table_8703b;
	p_config->odm_txxtaltrack_set_xtal = odm_txxtaltrack_set_xtal_8703b;
}



/* 1 7.	IQK */
#define MAX_TOLERANCE		5
#define IQK_DELAY_TIME		1		/* ms */

u8			/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_path_a_iqk_8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm
#else
	struct _ADAPTER	*p_adapter
#endif
)
{
	u32 reg_eac, reg_e94, reg_e9c, tmp/*, reg_ea4*/;
	u8 result = 0x00, ktime;
	u32 original_path, original_gnt;

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
#endif
#endif
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]TX IQK!\n"));

	/*8703b IQK v2.0 20150713*/
	/*1 Tx IQK*/
	/*IQK setting*/
	odm_set_bb_reg(p_dm_odm, REG_TX_IQK, MASKDWORD, 0x01007c00);
	odm_set_bb_reg(p_dm_odm, REG_RX_IQK, MASKDWORD, 0x01004800);
	/*path-A IQK setting*/
	odm_set_bb_reg(p_dm_odm, REG_TX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	odm_set_bb_reg(p_dm_odm, REG_RX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm_odm, REG_TX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm_odm, REG_RX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	/*	odm_set_bb_reg(p_dm_odm, REG_TX_IQK_PI_A, MASKDWORD, 0x8214010a);*/
	odm_set_bb_reg(p_dm_odm, REG_TX_IQK_PI_A, MASKDWORD, 0x8214030f);
	odm_set_bb_reg(p_dm_odm, REG_RX_IQK_PI_A, MASKDWORD, 0x28110000);
	odm_set_bb_reg(p_dm_odm, REG_TX_IQK_PI_B, MASKDWORD, 0x82110000);
	odm_set_bb_reg(p_dm_odm, REG_RX_IQK_PI_B, MASKDWORD, 0x28110000);

	/*LO calibration setting*/
	odm_set_bb_reg(p_dm_odm, REG_IQK_AGC_RSP, MASKDWORD, 0x00462911);

	/*leave IQK mode*/
	odm_set_bb_reg(p_dm_odm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	/*PA, PAD setting*/
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0xdf, 0x800, 0x1);
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0x55, 0x0007f, 0x7);
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0x7f, RFREGOFFSETMASK, 0x0d400);

	/*enter IQK mode*/
	odm_set_bb_reg(p_dm_odm, REG_FPGA0_IQK, 0xffffff00, 0x808000);

#if 1
	/*path setting*/
	/*Save Original path Owner, Original GNT*/
	original_path = odm_get_mac_reg(p_dm_odm, REG_LTECOEX_PATH_CONTROL, MASKDWORD);  /*save 0x70*/
	odm_set_bb_reg(p_dm_odm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
	ODM_delay_ms(1);
	original_gnt = odm_get_bb_reg(p_dm_odm, REG_LTECOEX_READ_DATA, MASKDWORD);  /*save 0x38*/

	/*set GNT_WL=1/GNT_BT=0  and path owner to WiFi for pause BT traffic*/
	odm_set_bb_reg(p_dm_odm, REG_LTECOEX_WRITE_DATA, MASKDWORD, 0x00007700);
	odm_set_bb_reg(p_dm_odm, REG_LTECOEX_CTRL, MASKDWORD, 0xc0020038);	/*0x38[15:8] = 0x77*/
	odm_set_mac_reg(p_dm_odm, REG_LTECOEX_PATH_CONTROL, BIT(26), 0x1);  /*0x70[26] =1 --> path Owner to WiFi*/
#endif

	/*One shot, path A LOK & IQK*/
	odm_set_bb_reg(p_dm_odm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(p_dm_odm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/* delay x ms */
	ODM_delay_ms(IQK_DELAY_TIME_8703B);
	ktime = 0;
	while ((odm_get_bb_reg(p_dm_odm, 0xe90, MASKDWORD) == 0) && ktime < 10) {
		ODM_delay_ms(5);
		ktime++;
	}

#if 1
	/*path setting*/
	/*Restore GNT_WL/GNT_BT  and path owner*/
	odm_set_bb_reg(p_dm_odm, REG_LTECOEX_WRITE_DATA, MASKDWORD, original_gnt);
	odm_set_bb_reg(p_dm_odm, REG_LTECOEX_CTRL, MASKDWORD, 0xc00f0038);
	odm_set_mac_reg(p_dm_odm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, original_path);

	original_path = odm_get_mac_reg(p_dm_odm, REG_LTECOEX_PATH_CONTROL, MASKDWORD);  /*save 0x70*/
	odm_set_bb_reg(p_dm_odm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
	ODM_delay_ms(1);
	original_gnt = odm_get_bb_reg(p_dm_odm, REG_LTECOEX_READ_DATA, MASKDWORD);  /*save 0x38*/

#endif

	/*leave IQK mode*/
	odm_set_bb_reg(p_dm_odm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	/*	PA/PAD controlled by 0x0*/
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0xdf, 0x800, 0x0);

	/* Check failed*/
	reg_eac = odm_get_bb_reg(p_dm_odm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_e94 = odm_get_bb_reg(p_dm_odm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD);
	reg_e9c = odm_get_bb_reg(p_dm_odm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD);
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac = 0x%x\n", reg_eac));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe94 = 0x%x, 0xe9c = 0x%x\n", reg_e94, reg_e9c));
	/*monitor image power before & after IQK*/
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe90(before IQK)= 0x%x, 0xe98(afer IQK) = 0x%x\n",
		odm_get_bb_reg(p_dm_odm, 0xe90, MASKDWORD), odm_get_bb_reg(p_dm_odm, 0xe98, MASKDWORD)));

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))

		result |= 0x01;

	return result;

}

u8			/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_path_a_rx_iqk_8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm
#else
	struct _ADAPTER	*p_adapter
#endif
)
{
	u32 reg_eac, reg_e94, reg_e9c, reg_ea4, u4tmp, tmp;
	u8 result = 0x00, ktime;
	u32 original_path, original_gnt;

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
#endif
#endif


	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]RX IQK:Get TXIMR setting\n"));
	/* 1 Get TX_XY */

	/* IQK setting */
	odm_set_bb_reg(p_dm_odm, REG_TX_IQK, MASKDWORD, 0x01007c00);
	odm_set_bb_reg(p_dm_odm, REG_RX_IQK, MASKDWORD, 0x01004800);

	/* path-A IQK setting */
	odm_set_bb_reg(p_dm_odm, REG_TX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	odm_set_bb_reg(p_dm_odm, REG_RX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm_odm, REG_TX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm_odm, REG_RX_IQK_TONE_B, MASKDWORD, 0x38008c1c);

	/*	odm_set_bb_reg(p_dm_odm, REG_TX_IQK_PI_A, MASKDWORD, 0x82160c1f); */
	odm_set_bb_reg(p_dm_odm, REG_TX_IQK_PI_A, MASKDWORD, 0x8216000f);
	odm_set_bb_reg(p_dm_odm, REG_RX_IQK_PI_A, MASKDWORD, 0x28110000);
	odm_set_bb_reg(p_dm_odm, REG_TX_IQK_PI_B, MASKDWORD, 0x82110000);
	odm_set_bb_reg(p_dm_odm, REG_RX_IQK_PI_B, MASKDWORD, 0x28110000);

	/* LO calibration setting */
	odm_set_bb_reg(p_dm_odm, REG_IQK_AGC_RSP, MASKDWORD, 0x0046a911);

	/* leave IQK mode */
	odm_set_bb_reg(p_dm_odm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	/* modify RXIQK mode table */
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, RF_WE_LUT, 0x80000, 0x1);
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, RF_RCK_OS, RFREGOFFSETMASK, 0x30000);
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, RF_TXPA_G1, RFREGOFFSETMASK, 0x00007);
	/*IQK PA off*/
	/*	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0xf7fb7); */
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0x57db7);

	odm_set_bb_reg(p_dm_odm, REG_FPGA0_IQK, 0xffffff00, 0x808000);

#if 1
	/*path setting*/
	/*Save Original path Owner, Original GNT*/
	original_path = odm_get_mac_reg(p_dm_odm, REG_LTECOEX_PATH_CONTROL, MASKDWORD);  /*save 0x70*/
	odm_set_bb_reg(p_dm_odm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
	ODM_delay_ms(1);
	original_gnt = odm_get_bb_reg(p_dm_odm, REG_LTECOEX_READ_DATA, MASKDWORD);  /*save 0x38*/

	/*set GNT_WL=1/GNT_BT=0  and path owner to WiFi for pause BT traffic*/
	odm_set_bb_reg(p_dm_odm, REG_LTECOEX_WRITE_DATA, MASKDWORD, 0x00007700);
	odm_set_bb_reg(p_dm_odm, REG_LTECOEX_CTRL, MASKDWORD, 0xc0020038);	/*0x38[15:8] = 0x77*/
	odm_set_mac_reg(p_dm_odm, REG_LTECOEX_PATH_CONTROL, BIT(26), 0x1);  /*0x70[26] =1 --> path Owner to WiFi*/
#endif

	/* One shot, path A LOK & IQK */
	odm_set_bb_reg(p_dm_odm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(p_dm_odm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/* delay x ms */
	ODM_delay_ms(IQK_DELAY_TIME_8703B);
	ktime = 0;
	while ((odm_get_bb_reg(p_dm_odm, 0xe90, MASKDWORD) == 0) && ktime < 10) {
		ODM_delay_ms(5);
		ktime++;
	}

#if 1
	/*path setting*/
	/*Restore GNT_WL/GNT_BT  and path owner*/
	odm_set_bb_reg(p_dm_odm, REG_LTECOEX_WRITE_DATA, MASKDWORD, original_gnt);
	odm_set_bb_reg(p_dm_odm, REG_LTECOEX_CTRL, MASKDWORD, 0xc00f0038);
	odm_set_mac_reg(p_dm_odm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, original_path);

	original_path = odm_get_mac_reg(p_dm_odm, REG_LTECOEX_PATH_CONTROL, MASKDWORD);  /*save 0x70*/
	odm_set_bb_reg(p_dm_odm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
	ODM_delay_ms(1);
	original_gnt = odm_get_bb_reg(p_dm_odm, REG_LTECOEX_READ_DATA, MASKDWORD);  /*save 0x38*/


#endif

	/* leave IQK mode */
	odm_set_bb_reg(p_dm_odm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	/* Check failed */
	reg_eac = odm_get_bb_reg(p_dm_odm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_e94 = odm_get_bb_reg(p_dm_odm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD);
	reg_e9c = odm_get_bb_reg(p_dm_odm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD);
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac = 0x%x\n", reg_eac));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe94 = 0x%x, 0xe9c = 0x%x\n", reg_e94, reg_e9c));
	/*monitor image power before & after IQK*/
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe90(before IQK)= 0x%x, 0xe98(afer IQK) = 0x%x\n",
		odm_get_bb_reg(p_dm_odm, 0xe90, MASKDWORD), odm_get_bb_reg(p_dm_odm, 0xe98, MASKDWORD)));

	/* Allen 20131125 */
	tmp = (reg_e9c & 0x03FF0000) >> 16;
	if ((tmp & 0x200) > 0)
		tmp = 0x400 - tmp;

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))

		result |= 0x01;
	else							/* if Tx not OK, ignore Rx */
		return result;



	u4tmp = 0x80007C00 | (reg_e94 & 0x3FF0000)  | ((reg_e9c & 0x3FF0000) >> 16);
	odm_set_bb_reg(p_dm_odm, REG_TX_IQK, MASKDWORD, u4tmp);
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe40 = 0x%x u4tmp = 0x%x\n", odm_get_bb_reg(p_dm_odm, REG_TX_IQK, MASKDWORD), u4tmp));

	/* 1 RX IQK */
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]RX IQK\n"));

	/* IQK setting */
	odm_set_bb_reg(p_dm_odm, REG_RX_IQK, MASKDWORD, 0x01004800);

	/* path-A IQK setting */
	odm_set_bb_reg(p_dm_odm, REG_TX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm_odm, REG_RX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	odm_set_bb_reg(p_dm_odm, REG_TX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm_odm, REG_RX_IQK_TONE_B, MASKDWORD, 0x38008c1c);

	odm_set_bb_reg(p_dm_odm, REG_TX_IQK_PI_A, MASKDWORD, 0x82110000);
	odm_set_bb_reg(p_dm_odm, REG_RX_IQK_PI_A, MASKDWORD, 0x28160c1f);
	/*	odm_set_bb_reg(p_dm_odm, REG_RX_IQK_PI_A, MASKDWORD, 0x2816001f);*/
	odm_set_bb_reg(p_dm_odm, REG_TX_IQK_PI_B, MASKDWORD, 0x82110000);
	odm_set_bb_reg(p_dm_odm, REG_RX_IQK_PI_B, MASKDWORD, 0x28110000);

	/* LO calibration setting */
	odm_set_bb_reg(p_dm_odm, REG_IQK_AGC_RSP, MASKDWORD, 0x0046a8d1);


	/* modify RXIQK mode table */
	odm_set_bb_reg(p_dm_odm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, RF_WE_LUT, 0x80000, 0x1);
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, RF_RCK_OS, RFREGOFFSETMASK, 0x30000);
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, RF_TXPA_G1, RFREGOFFSETMASK, 0x00007);
	/*PA off*/
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, RF_TXPA_G2, RFREGOFFSETMASK, 0xf7d77);

	/*PA, PAD setting*/
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0xdf, 0x800, 0x1);
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0x55, 0x0007f, 0x5);

	/*enter IQK mode*/
	odm_set_bb_reg(p_dm_odm, REG_FPGA0_IQK, 0xffffff00, 0x808000);

#if 1
	/*path setting*/
	/*Save Original path Owner, Original GNT*/
	original_path = odm_get_mac_reg(p_dm_odm, REG_LTECOEX_PATH_CONTROL, MASKDWORD);  /*save 0x70*/
	odm_set_bb_reg(p_dm_odm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
	ODM_delay_ms(1);
	original_gnt = odm_get_bb_reg(p_dm_odm, REG_LTECOEX_READ_DATA, MASKDWORD);  /*save 0x38*/

	/*set GNT_WL=1/GNT_BT=0  and path owner to WiFi for pause BT traffic*/
	odm_set_bb_reg(p_dm_odm, REG_LTECOEX_WRITE_DATA, MASKDWORD, 0x00007700);
	odm_set_bb_reg(p_dm_odm, REG_LTECOEX_CTRL, MASKDWORD, 0xc0020038);	/*0x38[15:8] = 0x77*/
	odm_set_mac_reg(p_dm_odm, REG_LTECOEX_PATH_CONTROL, BIT(26), 0x1);  /*0x70[26] =1 --> path Owner to WiFi*/
#endif

	/* One shot, path A LOK & IQK */
	odm_set_bb_reg(p_dm_odm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(p_dm_odm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/* delay x ms */
	ODM_delay_ms(IQK_DELAY_TIME_8703B);
	ktime = 0;
	while ((odm_get_bb_reg(p_dm_odm, 0xe90, MASKDWORD) == 0) && ktime < 10) {
		ODM_delay_ms(5);
		ktime++;
	}

#if 1
	/*path setting*/
	/*Restore GNT_WL/GNT_BT  and path owner*/
	odm_set_bb_reg(p_dm_odm, REG_LTECOEX_WRITE_DATA, MASKDWORD, original_gnt);
	odm_set_bb_reg(p_dm_odm, REG_LTECOEX_CTRL, MASKDWORD, 0xc00f0038);
	odm_set_mac_reg(p_dm_odm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, original_path);

	original_path = odm_get_mac_reg(p_dm_odm, REG_LTECOEX_PATH_CONTROL, MASKDWORD);  /*save 0x70*/
	odm_set_bb_reg(p_dm_odm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
	ODM_delay_ms(1);
	original_gnt = odm_get_bb_reg(p_dm_odm, REG_LTECOEX_READ_DATA, MASKDWORD);  /*save 0x38*/


#endif

	/*leave IQK mode*/
	odm_set_bb_reg(p_dm_odm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	/*	PA/PAD controlled by 0x0 */
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0xdf, 0x800, 0x0);

	/* Check failed */
	reg_eac = odm_get_bb_reg(p_dm_odm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_ea4 = odm_get_bb_reg(p_dm_odm, REG_RX_POWER_BEFORE_IQK_A_2, MASKDWORD);
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac = 0x%x\n", reg_eac));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xea4 = 0x%x, 0xeac = 0x%x\n", reg_ea4, reg_eac));
	/* monitor image power before & after IQK */
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xea0(before IQK)= 0x%x, 0xea8(afer IQK) = 0x%x\n",
		odm_get_bb_reg(p_dm_odm, 0xea0, MASKDWORD), odm_get_bb_reg(p_dm_odm, 0xea8, MASKDWORD)));

	/* Allen 20131125 */
	tmp = (reg_eac & 0x03FF0000) >> 16;
	if ((tmp & 0x200) > 0)
		tmp = 0x400 - tmp;

	if (!(reg_eac & BIT(27)) &&		/*if Tx is OK, check whether Rx is OK*/
	    (((reg_ea4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_eac & 0x03FF0000) >> 16) != 0x36) &&
	    (((reg_ea4 & 0x03FF0000) >> 16) < 0x11a) &&
	    (((reg_ea4 & 0x03FF0000) >> 16) > 0xe6) &&
	    (tmp < 0x1a))
		result |= 0x02;
	else							/* if Tx not OK, ignore Rx */
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path A Rx IQK fail!!\n"));

	return result;
}


void
_phy_path_a_fill_iqk_matrix8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	boolean	is_iqk_ok,
	s32		result[][8],
	u8		final_candidate,
	boolean		is_tx_only
)
{
	u32	oldval_0, X, TX0_A, reg, tmp0xc80, tmp0xc94, tmp0xc4c, tmp0xc14, tmp0xca0;
	s32	Y, TX0_C;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
#endif
#endif
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]path A IQ Calibration %s !\n", (is_iqk_ok) ? "Success" : "Failed"));

	if (final_candidate == 0xFF)
		return;

	else if (is_iqk_ok) {

		oldval_0 = (odm_get_bb_reg(p_dm_odm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD) >> 22) & 0x3FF;

		X = result[final_candidate][0];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;
		TX0_A = (X * oldval_0) >> 8;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]X = 0x%x, TX0_A = 0x%x, oldval_0 0x%x\n", X, TX0_A, oldval_0));
		tmp0xc80 = (odm_get_bb_reg(p_dm_odm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD) & 0xfffffc00) | (TX0_A & 0x3ff);
		tmp0xc4c = (((X * oldval_0 >> 7) & 0x1) << 31) | (odm_get_bb_reg(p_dm_odm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD) & 0x7fffffff);

		Y = result[final_candidate][1];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;

		/* 2 Tx IQC */
		TX0_C = (Y * oldval_0) >> 8;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Y = 0x%x, TX = 0x%x\n", Y, TX0_C));

		tmp0xc94 = (((TX0_C & 0x3C0) >> 6) << 28) | (odm_get_bb_reg(p_dm_odm, REG_OFDM_0_XC_TX_AFE, MASKDWORD) & 0x0fffffff);

		p_rf_calibrate_info->tx_iqc_8703b[idx_0xc94][KEY] = REG_OFDM_0_XC_TX_AFE;
		p_rf_calibrate_info->tx_iqc_8703b[idx_0xc94][VAL] = tmp0xc94;

		tmp0xc80 = (tmp0xc80 & 0xffc0ffff) | (TX0_C & 0x3F) << 16;

		p_rf_calibrate_info->tx_iqc_8703b[idx_0xc80][KEY] = REG_OFDM_0_XA_TX_IQ_IMBALANCE;
		p_rf_calibrate_info->tx_iqc_8703b[idx_0xc80][VAL] = tmp0xc80;

		tmp0xc4c = (tmp0xc4c & 0xdfffffff) | (((Y * oldval_0 >> 7) & 0x1) << 29);

		p_rf_calibrate_info->tx_iqc_8703b[idx_0xc4c][KEY] = REG_OFDM_0_ECCA_THRESHOLD;
		p_rf_calibrate_info->tx_iqc_8703b[idx_0xc4c][VAL] = tmp0xc4c;

		if (is_tx_only) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]_phy_path_a_fill_iqk_matrix8703b only Tx OK\n"));

			/* <20130226, Kordan> Saving RxIQC, otherwise not initialized. */
			p_rf_calibrate_info->rx_iqc_8703b[idx_0xca0][KEY] = REG_OFDM_0_RX_IQ_EXT_ANTA;
			p_rf_calibrate_info->rx_iqc_8703b[idx_0xca0][VAL] = 0xfffffff & odm_get_bb_reg(p_dm_odm, REG_OFDM_0_RX_IQ_EXT_ANTA, MASKDWORD);
			p_rf_calibrate_info->rx_iqc_8703b[idx_0xc14][KEY] = REG_OFDM_0_XA_RX_IQ_IMBALANCE;
			p_rf_calibrate_info->rx_iqc_8703b[idx_0xc14][VAL] = 0x40000100;
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

		p_rf_calibrate_info->rx_iqc_8703b[idx_0xc14][KEY] = REG_OFDM_0_XA_RX_IQ_IMBALANCE;
		p_rf_calibrate_info->rx_iqc_8703b[idx_0xc14][VAL] = tmp0xc14;

		reg = (result[final_candidate][3] >> 6) & 0xF;
		tmp0xca0 = odm_get_bb_reg(p_dm_odm, REG_OFDM_0_RX_IQ_EXT_ANTA, 0x0fffffff) | (reg << 28);

		p_rf_calibrate_info->rx_iqc_8703b[idx_0xca0][KEY] = REG_OFDM_0_RX_IQ_EXT_ANTA;
		p_rf_calibrate_info->rx_iqc_8703b[idx_0xca0][VAL] = tmp0xca0;
	}
}

#if 0
void
_phy_path_b_fill_iqk_matrix8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
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
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
#endif
#endif
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]path B IQ Calibration %s !\n", (is_iqk_ok) ? "Success" : "Failed"));

	if (final_candidate == 0xFF)
		return;

	else if (is_iqk_ok) {
		oldval_1 = (odm_get_bb_reg(p_dm_odm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD) >> 22) & 0x3FF;


		X = result[final_candidate][4];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;
		TX1_A = (X * oldval_1) >> 8;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]X = 0x%x, TX1_A = 0x%x\n", X, TX1_A));

		tmp0xc80 = (odm_get_bb_reg(p_dm_odm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD) & 0xfffffc00) | (TX1_A & 0x3ff);
		tmp0xc4c = (((X * oldval_1 >> 7) & 0x1) << 31) | (odm_get_bb_reg(p_dm_odm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD) & 0x7fffffff);

		Y = result[final_candidate][5];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;

		TX1_C = (Y * oldval_1) >> 8;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Y = 0x%x, TX1_C = 0x%x\n", Y, TX1_C));

		/*2 Tx IQC*/

		tmp0xc94 = (((TX1_C & 0x3C0) >> 6) << 28) | (odm_get_bb_reg(p_dm_odm, REG_OFDM_0_XC_TX_AFE, MASKDWORD) & 0x0fffffff);

		p_rf_calibrate_info->tx_iqc_8703b[PATH_S0][idx_0xc94][KEY] = REG_OFDM_0_XC_TX_AFE;
		p_rf_calibrate_info->tx_iqc_8703b[PATH_S0][idx_0xc94][VAL] = tmp0xc94;

		tmp0xc80 = (tmp0xc80 & 0xffc0ffff) | (TX1_C & 0x3F) << 16;
		p_rf_calibrate_info->tx_iqc_8703b[PATH_S0][idx_0xc80][KEY] = REG_OFDM_0_XA_TX_IQ_IMBALANCE;
		p_rf_calibrate_info->tx_iqc_8703b[PATH_S0][idx_0xc80][VAL] = tmp0xc80;

		tmp0xc4c = (tmp0xc4c & 0xdfffffff) | (((Y * oldval_1 >> 7) & 0x1) << 29);
		p_rf_calibrate_info->tx_iqc_8703b[PATH_S0][idx_0xc4c][KEY] = REG_OFDM_0_ECCA_THRESHOLD;
		p_rf_calibrate_info->tx_iqc_8703b[PATH_S0][idx_0xc4c][VAL] = tmp0xc4c;

		if (is_tx_only) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]_phy_path_b_fill_iqk_matrix8703b only Tx OK\n"));

			p_rf_calibrate_info->rx_iqc_8703b[PATH_S0][idx_0xc14][KEY] = REG_OFDM_0_XA_RX_IQ_IMBALANCE;
			p_rf_calibrate_info->rx_iqc_8703b[PATH_S0][idx_0xc14][VAL] = 0x40000100;
			p_rf_calibrate_info->rx_iqc_8703b[PATH_S0][idx_0xca0][KEY] = REG_OFDM_0_RX_IQ_EXT_ANTA;
			p_rf_calibrate_info->rx_iqc_8703b[PATH_S0][idx_0xca0][VAL] = 0x0fffffff & odm_get_bb_reg(p_dm_odm, REG_OFDM_0_RX_IQ_EXT_ANTA, MASKDWORD);
			return;
		}

		/* 2 Rx IQC */
		reg = result[final_candidate][6];
		tmp0xc14 = (0x40000100 & 0xfffffc00) | reg;

		reg = result[final_candidate][7] & 0x3F;
		tmp0xc14 = (tmp0xc14 & 0xffff03ff) | (reg << 10);

		p_rf_calibrate_info->rx_iqc_8703b[PATH_S0][idx_0xc14][KEY] = REG_OFDM_0_XA_RX_IQ_IMBALANCE;
		p_rf_calibrate_info->rx_iqc_8703b[PATH_S0][idx_0xc14][VAL] = tmp0xc14;

		reg = (result[final_candidate][7] >> 6) & 0xF;
		tmp0xca0 = odm_get_bb_reg(p_dm_odm, REG_OFDM_0_RX_IQ_EXT_ANTA, 0x0fffffff) | (reg << 28);

		p_rf_calibrate_info->rx_iqc_8703b[PATH_S0][idx_0xca0][KEY] = REG_OFDM_0_RX_IQ_EXT_ANTA;
		p_rf_calibrate_info->rx_iqc_8703b[PATH_S0][idx_0xca0][VAL] = tmp0xca0;
	}
}
#endif

boolean
odm_set_iqc_by_rfpath_8703b(
	struct PHY_DM_STRUCT		*p_dm_odm
)
{

	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);


	if ((p_rf_calibrate_info->tx_iqc_8703b[idx_0xc80][VAL] != 0x0) && (p_rf_calibrate_info->rx_iqc_8703b[idx_0xc14][VAL] != 0x0)) {

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]reload RF IQC!!!\n"));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xc80 = 0x%x!!!\n", p_rf_calibrate_info->tx_iqc_8703b[idx_0xc80][VAL]));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xc14 = 0x%x!!!\n", p_rf_calibrate_info->tx_iqc_8703b[idx_0xc14][VAL]));

		/* TX IQC */
		odm_set_bb_reg(p_dm_odm, p_rf_calibrate_info->tx_iqc_8703b[idx_0xc94][KEY], MASKH4BITS, (p_rf_calibrate_info->tx_iqc_8703b[idx_0xc94][VAL] >> 28));
		odm_set_bb_reg(p_dm_odm, p_rf_calibrate_info->tx_iqc_8703b[idx_0xc80][KEY], MASKDWORD, p_rf_calibrate_info->tx_iqc_8703b[idx_0xc80][VAL]);
		odm_set_bb_reg(p_dm_odm, p_rf_calibrate_info->tx_iqc_8703b[idx_0xc4c][KEY], BIT(31), (p_rf_calibrate_info->tx_iqc_8703b[idx_0xc4c][VAL] >> 31));
		odm_set_bb_reg(p_dm_odm, p_rf_calibrate_info->tx_iqc_8703b[idx_0xc4c][KEY], BIT(29), ((p_rf_calibrate_info->tx_iqc_8703b[idx_0xc4c][VAL] & BIT(29)) >> 29));

		/* RX IQC */
		odm_set_bb_reg(p_dm_odm, p_rf_calibrate_info->rx_iqc_8703b[idx_0xc14][KEY], MASKDWORD, p_rf_calibrate_info->rx_iqc_8703b[idx_0xc14][VAL]);
		odm_set_bb_reg(p_dm_odm, p_rf_calibrate_info->rx_iqc_8703b[idx_0xca0][KEY], MASKDWORD, p_rf_calibrate_info->rx_iqc_8703b[idx_0xca0][VAL]);
		return true;

	} else {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQC value invalid!!!\n"));
		return false;
	}
}


#if !(DM_ODM_SUPPORT_TYPE & ODM_WIN)
boolean
odm_check_power_status(
	struct _ADAPTER		*adapter)
{
#if 0
	HAL_DATA_TYPE			*p_hal_data = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT				*p_dm_odm = &p_hal_data->DM_OutSrc;
	RT_RF_POWER_STATE	rt_state;
	PMGNT_INFO				p_mgnt_info	= &(adapter->MgntInfo);

	/* 2011/07/27 MH We are not testing ready~~!! We may fail to get correct value when init sequence.*/
	if (p_mgnt_info->init_adpt_in_progress == true) {
		ODM_RT_TRACE(p_dm_odm, COMP_INIT, DBG_LOUD, ("odm_check_power_status Return true, due to initadapter"));
		return	true;
	}


	/*	2011/07/19 MH We can not execute tx power tracking/ LLC calibrate or IQK.*/

	phydm_get_hw_reg_interface(p_dm_odm, HW_VAR_RF_STATE, (u8 *)(&rt_state));
	if (adapter->is_driver_stopped || adapter->is_driver_is_going_to_pnp_set_power_sleep || rt_state == eRfOff) {
		ODM_RT_TRACE(p_dm_odm, COMP_INIT, DBG_LOUD, ("odm_check_power_status Return false, due to %d/%d/%d\n",
			adapter->is_driver_stopped, adapter->is_driver_is_going_to_pnp_set_power_sleep, rt_state));
		return	false;
	}
#endif
	return	true;
}
#endif

void
_phy_save_adda_registers8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	u32		*adda_reg,
	u32		*adda_backup,
	u32		register_num
)
{
	u32	i;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
#endif

	if (odm_check_power_status(p_adapter) == false)
		return;
#endif

	/*	ODM_RT_TRACE(p_dm_odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Save ADDA parameters.\n")); */
	for (i = 0; i < register_num; i++)
		adda_backup[i] = odm_get_bb_reg(p_dm_odm, adda_reg[i], MASKDWORD);
}


void
_phy_save_mac_registers8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	u32		*mac_reg,
	u32		*mac_backup
)
{
	u32	i;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
#endif
#endif
	/*	ODM_RT_TRACE(p_dm_odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Save MAC parameters.\n")); */
	for (i = 0; i < (IQK_MAC_REG_NUM - 1); i++)
		mac_backup[i] = odm_read_1byte(p_dm_odm, mac_reg[i]);

	mac_backup[i] = odm_read_4byte(p_dm_odm, mac_reg[i]);

}


void
_phy_reload_adda_registers8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	u32		*adda_reg,
	u32		*adda_backup,
	u32		regiester_num
)
{
	u32	i;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
#endif
#endif

	/*	ODM_RT_TRACE(p_dm_odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Reload ADDA power saving parameters !\n")); */
	for (i = 0 ; i < regiester_num; i++)
		odm_set_bb_reg(p_dm_odm, adda_reg[i], MASKDWORD, adda_backup[i]);

}

void
_phy_reload_mac_registers8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	u32		*mac_reg,
	u32		*mac_backup
)
{
	u32	i;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
#endif
#endif
	/*	ODM_RT_TRACE(p_dm_odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("Reload MAC parameters !\n")); */
	for (i = 0 ; i < (IQK_MAC_REG_NUM - 1); i++)
		odm_write_1byte(p_dm_odm, mac_reg[i], (u8)mac_backup[i]);

	odm_write_4byte(p_dm_odm, mac_reg[i], mac_backup[i]);
}


void
_phy_path_adda_on8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	u32		*adda_reg,
	boolean		is_path_a_on
)
{
	u32	path_on;
	u32	i;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
#endif
#endif
	/*	ODM_RT_TRACE(p_dm_odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("ADDA ON.\n")); */

	path_on = 0x03c00014;


	for (i = 0; i < IQK_ADDA_REG_NUM; i++)
		odm_set_bb_reg(p_dm_odm, adda_reg[i], MASKDWORD, path_on);

}

void
_phy_mac_setting_calibration8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	u32		*mac_reg,
	u32		*mac_backup
)
{
	u32	i = 0;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
#endif
#endif
	/*	ODM_RT_TRACE(p_dm_odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("MAC settings for Calibration.\n")); */

	odm_write_1byte(p_dm_odm, mac_reg[i], 0x3F);

	for (i = 1 ; i < (IQK_MAC_REG_NUM - 1); i++)
		odm_write_1byte(p_dm_odm, mac_reg[i], (u8)(mac_backup[i] & (~BIT(3))));

	odm_write_1byte(p_dm_odm, mac_reg[i], (u8)(mac_backup[i] & (~BIT(5))));

}

boolean
phy_simularity_compare_8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	s32		result[][8],
	u8		 c1,
	u8		 c2
)
{
	u32		i, j, diff, simularity_bit_map, bound = 0;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->odmpriv;
#endif

#endif
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

	/*	ODM_RT_TRACE(p_dm_odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("===> IQK:phy_simularity_compare_8192e c1 %d c2 %d!!!\n", c1, c2)); */


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
			/*			ODM_RT_TRACE(p_dm_odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD,  ("IQK:differnece overflow %d index %d compare1 0x%x compare2 0x%x!!!\n",  diff, i, result[c1][i], result[c2][i])); */

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

	/*	ODM_RT_TRACE(p_dm_odm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK:phy_simularity_compare_8192e simularity_bit_map   %x !!!\n", simularity_bit_map)); */

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

		if (!(simularity_bit_map & 0x03)) {		/*path A TX OK*/
			for (i = 0; i < 2; i++)
				result[3][i] = result[c1][i];
		}

		if (!(simularity_bit_map & 0x0c)) {		/*path A RX OK*/
			for (i = 2; i < 4; i++)
				result[3][i] = result[c1][i];
		}

		if (!(simularity_bit_map & 0x30)) {	/*path B TX OK*/
			for (i = 4; i < 6; i++)
				result[3][i] = result[c1][i];

		}

		if (!(simularity_bit_map & 0xc0)) {	/*path B RX OK*/
			for (i = 6; i < 8; i++)
				result[3][i] = result[c1][i];
		}
		return false;
	}
}



void
_phy_iq_calibrate_8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	s32		result[][8],
	u8		t
)
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
#endif
#endif
	u32			i;
	u8			path_aok, path_bok;
	u8			tmp0xc50 = (u8)odm_get_bb_reg(p_dm_odm, 0xC50, MASKBYTE0);
	u8			tmp0xc58 = (u8)odm_get_bb_reg(p_dm_odm, 0xC58, MASKBYTE0);
	u32			ADDA_REG[IQK_ADDA_REG_NUM] = {
		REG_FPGA0_XCD_SWITCH_CONTROL,		REG_BLUE_TOOTH,
		REG_RX_WAIT_CCA,		REG_TX_CCK_RFON,
		REG_TX_CCK_BBON,		REG_TX_OFDM_RFON,
		REG_TX_OFDM_BBON,		REG_TX_TO_RX,
		REG_TX_TO_TX,		REG_RX_CCK,
		REG_RX_OFDM,		REG_RX_WAIT_RIFS,
		REG_RX_TO_RX,		REG_STANDBY,
		REG_SLEEP,		REG_PMPD_ANAEN
	};
	u32			IQK_MAC_REG[IQK_MAC_REG_NUM] = {
		REG_TXPAUSE,		REG_BCN_CTRL,
		REG_BCN_CTRL_1,	REG_GPIO_MUXCFG
	};

	/*since 92C & 92D have the different define in IQK_BB_REG*/
	u32	IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
		REG_OFDM_0_TRX_PATH_ENABLE,		REG_OFDM_0_TR_MUX_PAR,
		REG_FPGA0_XCD_RF_INTERFACE_SW,	REG_CONFIG_ANT_A,	REG_CONFIG_ANT_B,
		REG_FPGA0_XAB_RF_INTERFACE_SW,	REG_FPGA0_XA_RF_INTERFACE_OE,
		REG_FPGA0_XB_RF_INTERFACE_OE,		REG_CCK_0_AFE_SETTING
	};



#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	u32	retry_count = 2;
#else
#if MP_DRIVER
	const u32	retry_count = 1;
#else
	const u32	retry_count = 2;
#endif
#endif

	/* Note: IQ calibration must be performed after loading*/
	/*PHY_REG.txt , and radio_a, radio_b.txt*/

	/* u32 bbvalue; */

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
#ifdef MP_TEST
	if (p_dm_odm->priv->pshare->rf_ft_var.mp_specific)
		retry_count = 9;
#endif
#endif


	if (t == 0) {
		/*	 	 bbvalue = odm_get_bb_reg(p_dm_odm, REG_FPGA0_RFMOD, MASKDWORD);
		 * 			RT_DISP(FINIT, INIT_IQK, ("_phy_iq_calibrate_8188e()==>0x%08x\n",bbvalue)); */

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQ Calibration for %d times\n", t));

		/* Save ADDA parameters, turn path A ADDA on*/
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_phy_save_adda_registers8703b(p_adapter, ADDA_REG, p_dm_odm->rf_calibrate_info.ADDA_backup, IQK_ADDA_REG_NUM);
		_phy_save_mac_registers8703b(p_adapter, IQK_MAC_REG, p_dm_odm->rf_calibrate_info.IQK_MAC_backup);
		_phy_save_adda_registers8703b(p_adapter, IQK_BB_REG_92C, p_dm_odm->rf_calibrate_info.IQK_BB_backup, IQK_BB_REG_NUM);
#else
		_phy_save_adda_registers8703b(p_dm_odm, ADDA_REG, p_dm_odm->rf_calibrate_info.ADDA_backup, IQK_ADDA_REG_NUM);
		_phy_save_mac_registers8703b(p_dm_odm, IQK_MAC_REG, p_dm_odm->rf_calibrate_info.IQK_MAC_backup);
		_phy_save_adda_registers8703b(p_dm_odm, IQK_BB_REG_92C, p_dm_odm->rf_calibrate_info.IQK_BB_backup, IQK_BB_REG_NUM);
#endif
	}
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQ Calibration for %d times\n", t));

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)

	_phy_path_adda_on8703b(p_adapter, ADDA_REG, true);
#else
	_phy_path_adda_on8703b(p_dm_odm, ADDA_REG, true);
#endif


	/* MAC settings */
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_phy_mac_setting_calibration8703b(p_adapter, IQK_MAC_REG, p_dm_odm->rf_calibrate_info.IQK_MAC_backup);
#else
	_phy_mac_setting_calibration8703b(p_dm_odm, IQK_MAC_REG, p_dm_odm->rf_calibrate_info.IQK_MAC_backup);
#endif

	/* BB setting */
	/*odm_set_bb_reg(p_dm_odm, REG_FPGA0_RFMOD, BIT24, 0x00);*/
	odm_set_bb_reg(p_dm_odm, REG_CCK_0_AFE_SETTING, 0x0f000000, 0xf);
	odm_set_bb_reg(p_dm_odm, REG_OFDM_0_TRX_PATH_ENABLE, MASKDWORD, 0x03a05600);
	odm_set_bb_reg(p_dm_odm, REG_OFDM_0_TR_MUX_PAR, MASKDWORD, 0x000800e4);
	odm_set_bb_reg(p_dm_odm, REG_FPGA0_XCD_RF_INTERFACE_SW, MASKDWORD, 0x25204000);

	/* path A TX IQK */
#if 1

	for (i = 0 ; i < retry_count ; i++) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		path_aok = phy_path_a_iqk_8703b(p_adapter);
#else
		path_aok = phy_path_a_iqk_8703b(p_dm_odm);
#endif

		if (path_aok == 0x01) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Tx IQK Success!!\n"));
			result[t][0] = (odm_get_bb_reg(p_dm_odm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
			result[t][1] = (odm_get_bb_reg(p_dm_odm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
			break;
		}

	}
#endif

	/* path A RXIQK */
#if 1

	for (i = 0 ; i < retry_count ; i++) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		path_aok = phy_path_a_rx_iqk_8703b(p_adapter);
#else
		path_aok = phy_path_a_rx_iqk_8703b(p_dm_odm);
#endif
		if (path_aok == 0x03) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Rx IQK Success!!\n"));
			/*			result[t][0] = (odm_get_bb_reg(p_dm_odm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD)&0x3FF0000)>>16;
			 *			result[t][1] = (odm_get_bb_reg(p_dm_odm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD)&0x3FF0000)>>16; */
			result[t][2] = (odm_get_bb_reg(p_dm_odm, REG_RX_POWER_BEFORE_IQK_A_2, MASKDWORD) & 0x3FF0000) >> 16;
			result[t][3] = (odm_get_bb_reg(p_dm_odm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD) & 0x3FF0000) >> 16;
			break;
		} else
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Rx IQK Fail!!\n"));
	}

	if (0x00 == path_aok)
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK failed!!\n"));

#endif


	/* path B TX IQK */
#if 0

#if MP_DRIVER != 1
	if ((*p_dm_odm->p_is_1_antenna == false) || ((*p_dm_odm->p_is_1_antenna == true) && (*p_dm_odm->p_rf_default_path == 1))
	    || (p_dm_odm->support_interface == ODM_ITRF_USB))
#endif
	{

		for (i = 0 ; i < retry_count ; i++) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			path_bok = phy_path_b_iqk_8703b(p_adapter);
#else
			path_bok = phy_path_b_iqk_8703b(p_dm_odm);
#endif
			/*		if(path_bok == 0x03){ */
			if (path_bok == 0x01) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S0 Tx IQK Success!!\n"));
				result[t][4] = (odm_get_bb_reg(p_dm_odm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
				result[t][5] = (odm_get_bb_reg(p_dm_odm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
				break;
			}

		}
#endif

		/* path B RX IQK */
#if 0

		for (i = 0 ; i < retry_count ; i++) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			path_bok = phy_path_b_rx_iqk_8703b(p_adapter);
#else
			path_bok = phy_path_b_rx_iqk_8703b(p_dm_odm);
#endif
			if (path_bok == 0x03) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S0 Rx IQK Success!!\n"));
				/*				result[t][0] = (odm_get_bb_reg(p_dm_odm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD)&0x3FF0000)>>16;
				 *				result[t][1] = (odm_get_bb_reg(p_dm_odm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD)&0x3FF0000)>>16; */
				result[t][6] = (odm_get_bb_reg(p_dm_odm, REG_RX_POWER_BEFORE_IQK_A_2, MASKDWORD) & 0x3FF0000) >> 16;
				result[t][7] = (odm_get_bb_reg(p_dm_odm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD) & 0x3FF0000) >> 16;
				break;

			} else
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S0 Rx IQK Fail!!\n"));

		}



		if (0x00 == path_bok)
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]S0 IQK failed!!\n"));

	}
#endif

	/* Back to BB mode, load original value */
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK:Back to BB mode, load original value!\n"));
	odm_set_bb_reg(p_dm_odm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	if (t != 0) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)

		/* Reload ADDA power saving parameters*/
		_phy_reload_adda_registers8703b(p_adapter, ADDA_REG, p_dm_odm->rf_calibrate_info.ADDA_backup, IQK_ADDA_REG_NUM);

		/* Reload MAC parameters*/
		_phy_reload_mac_registers8703b(p_adapter, IQK_MAC_REG, p_dm_odm->rf_calibrate_info.IQK_MAC_backup);

		_phy_reload_adda_registers8703b(p_adapter, IQK_BB_REG_92C, p_dm_odm->rf_calibrate_info.IQK_BB_backup, IQK_BB_REG_NUM);
#else
		/* Reload ADDA power saving parameters*/
		_phy_reload_adda_registers8703b(p_dm_odm, ADDA_REG, p_dm_odm->rf_calibrate_info.ADDA_backup, IQK_ADDA_REG_NUM);

		/* Reload MAC parameters*/
		_phy_reload_mac_registers8703b(p_dm_odm, IQK_MAC_REG, p_dm_odm->rf_calibrate_info.IQK_MAC_backup);

		_phy_reload_adda_registers8703b(p_dm_odm, IQK_BB_REG_92C, p_dm_odm->rf_calibrate_info.IQK_BB_backup, IQK_BB_REG_NUM);
#endif

		/* Allen initial gain 0xc50 */
		/* Restore RX initial gain */
		odm_set_bb_reg(p_dm_odm, 0xc50, MASKBYTE0, 0x50);
		odm_set_bb_reg(p_dm_odm, 0xc50, MASKBYTE0, tmp0xc50);

		/* load 0xe30 IQC default value */
		odm_set_bb_reg(p_dm_odm, REG_TX_IQK_TONE_A, MASKDWORD, 0x01008c00);
		odm_set_bb_reg(p_dm_odm, REG_RX_IQK_TONE_A, MASKDWORD, 0x01008c00);

	}
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]_phy_iq_calibrate_8703b() <==\n"));

}


void
_phy_lc_calibrate_8703b(
	struct PHY_DM_STRUCT		*p_dm_odm,
	boolean		is2T
)
{
	u8	tmp_reg;
	u32	rf_amode = 0, rf_bmode = 0, lc_cal, cnt;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct _ADAPTER *p_adapter = p_dm_odm->adapter;
#endif

	/*Check continuous TX and Packet TX*/
	tmp_reg = odm_read_1byte(p_dm_odm, 0xd03);

	if ((tmp_reg & 0x70) != 0)			/*Deal with contisuous TX case*/
		odm_write_1byte(p_dm_odm, 0xd03, tmp_reg & 0x8F);	/*disable all continuous TX*/
	else							/* Deal with Packet TX case*/
		odm_write_1byte(p_dm_odm, REG_TXPAUSE, 0xFF);			/* block all queues*/


	/*backup RF0x18*/
	lc_cal = odm_get_rf_reg(p_dm_odm, ODM_RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK);

	/*Start LCK*/
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK, lc_cal | 0x08000);

	for (cnt = 0; cnt < 100; cnt++) {
		if (odm_get_rf_reg(p_dm_odm, ODM_RF_PATH_A, RF_CHNLBW, 0x8000) != 0x1)
			break;

		ODM_delay_ms(10);
	}

	/*Recover channel number*/
	odm_set_rf_reg(p_dm_odm, ODM_RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK, lc_cal);


	/*Restore original situation*/
	if ((tmp_reg & 0x70) != 0) {
		/*Deal with contisuous TX case*/
		odm_write_1byte(p_dm_odm, 0xd03, tmp_reg);
	} else {
		/* Deal with Packet TX case*/
		odm_write_1byte(p_dm_odm, REG_TXPAUSE, 0x00);
	}

}

/* IQK version:V0.4*/
/* 1. add coex. related setting*/

void
phy_iq_calibrate_8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	boolean	is_recovery
)
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
#else  /* (DM_ODM_SUPPORT_TYPE == ODM_CE)*/
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->odmpriv;
#endif

#if (MP_DRIVER == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PMPT_CONTEXT	p_mpt_ctx = &(p_adapter->mpt_ctx);
#else/* (DM_ODM_SUPPORT_TYPE == ODM_CE)*/
	PMPT_CONTEXT	p_mpt_ctx = &(p_adapter->mppriv.mpt_ctx);
#endif
#endif/*(MP_DRIVER == 1)*/

	u8			u1b_tmp;
	u16			count = 0;
#endif

	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);

	s32			result[4][8];	/* last is final result */
	u8			i, final_candidate, indexforchannel;
	boolean			is_patha_ok, is_pathb_ok;
	s32			rege94, rege9c, regea4, regeac, regeb4, regebc, regec4, regecc, reg_tmp = 0;
	boolean			is12simular, is13simular, is23simular;
	boolean		is_start_cont_tx = false, is_single_tone = false, is_carrier_suppression = false;
	u32			IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
		REG_OFDM_0_XA_RX_IQ_IMBALANCE,		REG_OFDM_0_XB_RX_IQ_IMBALANCE,
		REG_OFDM_0_ECCA_THRESHOLD,		REG_OFDM_0_AGC_RSSI_TABLE,
		REG_OFDM_0_XA_TX_IQ_IMBALANCE,		REG_OFDM_0_XB_TX_IQ_IMBALANCE,
		REG_OFDM_0_XC_TX_AFE,			REG_OFDM_0_XD_TX_AFE,
		REG_OFDM_0_RX_IQ_EXT_ANTA
	};
	boolean			is_reload_iqk = false;

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	if (odm_check_power_status(p_adapter) == false)
		return;
#else
	struct rtl8192cd_priv	*priv = p_dm_odm->priv;

#ifdef MP_TEST
	if (priv->pshare->rf_ft_var.mp_specific) {
		if ((OPMODE & WIFI_MP_CTX_PACKET) || (OPMODE & WIFI_MP_CTX_ST))
			return;
	}
#endif

	if (priv->pshare->IQK_88E_done)
		is_recovery = 1;
	priv->pshare->IQK_88E_done = 1;
#endif


#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if (!(p_dm_odm->support_ability & ODM_RF_CALIBRATION))
		return;

#endif


	if (p_dm_odm->mp_mode == true) {
#if MP_DRIVER == 1
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		/* <VincentL, 131231> Add to determine IQK ON/OFF in certain case, Suggested by Cheng.*/
		if (!p_hal_data->iqk_mp_switch)
			return;
#endif

		is_start_cont_tx = p_mpt_ctx->is_start_cont_tx;
		is_single_tone = p_mpt_ctx->is_single_tone;
		is_carrier_suppression = p_mpt_ctx->is_carrier_suppression;

		/* 20120213<Kordan> Turn on when continuous Tx to pass lab testing. (required by Edlu)*/
		if (is_single_tone || is_carrier_suppression)
			return;
#endif
	}

#if DISABLE_BB_RF
	return;
#endif


	if (p_dm_odm->rf_calibrate_info.is_iqk_in_progress)
		return;

#if (DM_ODM_SUPPORT_TYPE & (ODM_CE | ODM_AP))
	if (is_recovery)
#else/* for ODM_WIN */
	if (is_recovery && (!p_adapter->bInHctTest)) /* YJ,add for PowerTest,120405 */
#endif
	{
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("[IQK]phy_iq_calibrate_8703b: Return due to is_recovery!\n"));
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_phy_reload_adda_registers8703b(p_adapter, IQK_BB_REG_92C, p_dm_odm->rf_calibrate_info.IQK_BB_backup_recover, 9);
#else
		_phy_reload_adda_registers8703b(p_dm_odm, IQK_BB_REG_92C, p_dm_odm->rf_calibrate_info.IQK_BB_backup_recover, 9);
#endif
		return;
	}


	if (p_dm_odm->mp_mode == false) {
#if MP_DRIVER != 1
		/* check if IQK had been done before!! */

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK] 0xc80 = 0x%x\n", p_rf_calibrate_info->tx_iqc_8703b[idx_0xc80][VAL]));
		if (odm_set_iqc_by_rfpath_8703b(p_dm_odm)) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK value is reloaded!!!\n"));
			is_reload_iqk = true;
		}

		if (is_reload_iqk)
			return;
#endif
	}
	/*Check & wait if BT is doing IQK*/

	if (p_dm_odm->mp_mode == false) {
#if MP_DRIVER != 1
		/* Set H2C cmd to inform FW (enable). */
		SetFwWiFiCalibrationCmd(p_adapter, 1);
		/* Check 0x1e6 */
		count = 0;
		u1b_tmp = odm_read_1byte(p_dm_odm, 0x1e6);
		while (u1b_tmp != 0x1 && count < 1000) {
			odm_stall_execution(10);
			u1b_tmp = odm_read_1byte(p_dm_odm, 0x1e6);
			count++;
		}

		if (count >= 1000)
			RT_TRACE(COMP_INIT, DBG_LOUD, ("[IQK]Polling 0x1e6 to 1 for WiFi calibration H2C cmd FAIL! count(%d)", count));


		/* Wait BT IQK finished. */
		/* polling 0x1e7[0]=1 or 300ms timeout */
		u1b_tmp = odm_read_1byte(p_dm_odm, 0x1e7);
		while ((!(u1b_tmp & BIT(0))) && count < 6000) {
			odm_stall_execution(50);
			u1b_tmp = odm_read_1byte(p_dm_odm, 0x1e7);
			count++;
		}
#endif
	}

	/* IQK start!!!!!!!!!! */

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK:Start!!!\n"));
	odm_acquire_spin_lock(p_dm_odm, RT_IQK_SPINLOCK);
	p_dm_odm->rf_calibrate_info.is_iqk_in_progress = true;
	odm_release_spin_lock(p_dm_odm, RT_IQK_SPINLOCK);


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
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_phy_iq_calibrate_8703b(p_adapter, result, i);
#else
		_phy_iq_calibrate_8703b(p_dm_odm, result, i);
#endif


		if (i == 1) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			is12simular = phy_simularity_compare_8703b(p_adapter, result, 0, 1);
#else
			is12simular = phy_simularity_compare_8703b(p_dm_odm, result, 0, 1);
#endif
			if (is12simular) {
				final_candidate = 0;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK: is12simular final_candidate is %x\n", final_candidate));
				break;
			}
		}

		if (i == 2) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			is13simular = phy_simularity_compare_8703b(p_adapter, result, 0, 2);
#else
			is13simular = phy_simularity_compare_8703b(p_dm_odm, result, 0, 2);
#endif
			if (is13simular) {
				final_candidate = 0;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK: is13simular final_candidate is %x\n", final_candidate));

				break;
			}
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
			is23simular = phy_simularity_compare_8703b(p_adapter, result, 1, 2);
#else
			is23simular = phy_simularity_compare_8703b(p_dm_odm, result, 1, 2);
#endif
			if (is23simular) {
				final_candidate = 1;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK: is23simular final_candidate is %x\n", final_candidate));
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
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK: rege94=%x rege9c=%x regea4=%x regeac=%x regeb4=%x regebc=%x regec4=%x regecc=%x\n ", rege94, rege9c, regea4, regeac, regeb4, regebc, regec4, regecc));
	}

	if (final_candidate != 0xff) {
		p_dm_odm->rf_calibrate_info.rege94 = rege94 = result[final_candidate][0];
		p_dm_odm->rf_calibrate_info.rege9c = rege9c = result[final_candidate][1];
		regea4 = result[final_candidate][2];
		regeac = result[final_candidate][3];
		p_dm_odm->rf_calibrate_info.regeb4 = regeb4 = result[final_candidate][4];
		p_dm_odm->rf_calibrate_info.regebc = regebc = result[final_candidate][5];
		regec4 = result[final_candidate][6];
		regecc = result[final_candidate][7];
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK: final_candidate is %x\n", final_candidate));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK: rege94=%x rege9c=%x regea4=%x regeac=%x regeb4=%x regebc=%x regec4=%x regecc=%x\n ", rege94, rege9c, regea4, regeac, regeb4, regebc, regec4, regecc));
		is_patha_ok = is_pathb_ok = true;
	} else {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK: FAIL use default value\n"));
		p_dm_odm->rf_calibrate_info.rege94 = p_dm_odm->rf_calibrate_info.regeb4 = 0x100;	/* X default value */
		p_dm_odm->rf_calibrate_info.rege9c = p_dm_odm->rf_calibrate_info.regebc = 0x0;		/* Y default value */
	}

	/* fill IQK matrix */
	if (rege94 != 0)
		_phy_path_a_fill_iqk_matrix8703b(p_adapter, is_patha_ok, result, final_candidate, (regea4 == 0));
	/*	if (regeb4 != 0)
	 *		_phy_path_b_fill_iqk_matrix8703b(p_adapter, is_pathb_ok, result, final_candidate, (regec4 == 0)); */

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	indexforchannel = odm_get_right_chnl_place_for_iqk(*p_dm_odm->p_channel);
#else
	indexforchannel = 0;
#endif

	/* To Fix BSOD when final_candidate is 0xff
	 * by sherry 20120321 */
	if (final_candidate < 4) {
		for (i = 0; i < iqk_matrix_reg_num; i++)
			p_dm_odm->rf_calibrate_info.iqk_matrix_reg_setting[indexforchannel].value[0][i] = result[final_candidate][i];
		p_dm_odm->rf_calibrate_info.iqk_matrix_reg_setting[indexforchannel].is_iqk_done = true;
	}
	/* RT_DISP(FINIT, INIT_IQK, ("\nIQK OK indexforchannel %d.\n", indexforchannel)); */
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]\nIQK OK indexforchannel %d.\n", indexforchannel));


#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_phy_save_adda_registers8703b(p_adapter, IQK_BB_REG_92C, p_dm_odm->rf_calibrate_info.IQK_BB_backup_recover, 9);
#else
	_phy_save_adda_registers8703b(p_dm_odm, IQK_BB_REG_92C, p_dm_odm->rf_calibrate_info.IQK_BB_backup_recover, IQK_BB_REG_NUM);
#endif

	/* fill IQK register */
	odm_set_iqc_by_rfpath_8703b(p_dm_odm);

	if (p_dm_odm->mp_mode == false) {
#if MP_DRIVER != 1
		/* Set H2C cmd to inform FW (disable). */
		SetFwWiFiCalibrationCmd(p_adapter, 0);

		/* Check 0x1e6 */
		count = 0;
		u1b_tmp = odm_read_1byte(p_dm_odm, 0x1e6);
		while (u1b_tmp != 0 && count < 1000) {
			odm_stall_execution(10);
			u1b_tmp = odm_read_1byte(p_dm_odm, 0x1e6);
			count++;
		}

		if (count >= 1000)
			RT_TRACE(COMP_INIT, DBG_LOUD, ("[IQK]Polling 0x1e6 to 0 for WiFi calibration H2C cmd FAIL! count(%d)", count));

#endif
	}

	odm_acquire_spin_lock(p_dm_odm, RT_IQK_SPINLOCK);
	p_dm_odm->rf_calibrate_info.is_iqk_in_progress = false;
	odm_release_spin_lock(p_dm_odm, RT_IQK_SPINLOCK);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]IQK finished\n"));
}


void
phy_lc_calibrate_8703b(
	void		*p_dm_void
)
{
	boolean		is_start_cont_tx = false, is_single_tone = false, is_carrier_suppression = false;
	u32			timeout = 2000, timecount = 0;
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct _ADAPTER	*p_adapter = p_dm_odm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);

#if (MP_DRIVER == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PMPT_CONTEXT	p_mpt_ctx = &(p_adapter->mpt_ctx);
#else/* (DM_ODM_SUPPORT_TYPE == ODM_CE)*/
	PMPT_CONTEXT	p_mpt_ctx = &(p_adapter->mppriv.mpt_ctx);
#endif
#endif/*(MP_DRIVER == 1)*/
#endif




#if MP_DRIVER == 1
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	/* <VincentL, 140319> Add to Disable Default LCK when Cont Tx, For Lab Test Usage. */
	if (!p_hal_data->iqk_mp_switch)
		return;
#endif

	is_start_cont_tx = p_mpt_ctx->is_start_cont_tx;
	is_single_tone = p_mpt_ctx->is_single_tone;
	is_carrier_suppression = p_mpt_ctx->is_carrier_suppression;
#endif


#if DISABLE_BB_RF
	return;
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	if (!(p_dm_odm->support_ability & ODM_RF_CALIBRATION))
		return;

#endif
	/* 20120213<Kordan> Turn on when continuous Tx to pass lab testing. (required by Edlu) */
	if (is_single_tone || is_carrier_suppression)
		return;

	while (*(p_dm_odm->p_is_scan_in_process) && timecount < timeout) {
		ODM_delay_ms(50);
		timecount += 50;
	}

	p_dm_odm->rf_calibrate_info.is_lck_in_progress = true;


	_phy_lc_calibrate_8703b(p_dm_odm, false);


	p_dm_odm->rf_calibrate_info.is_lck_in_progress = false;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("LCK:Finish!!!interface %d\n", p_dm_odm->interface_index));

}

void _phy_set_rf_path_switch_8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	boolean		is_main,
	boolean		is2T
)
{
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
#endif

	if (is_main) {	/*Set WIFI S1*/
		odm_set_bb_reg(p_dm_odm, 0x7C4, MASKLWORD, 0x7703);
		odm_set_bb_reg(p_dm_odm, 0x7C0, MASKDWORD, 0xC00F0038);
	} else {		/*Set BT S0*/
		odm_set_bb_reg(p_dm_odm, 0x7C4, MASKLWORD, 0xCC03);
		odm_set_bb_reg(p_dm_odm, 0x7C0, MASKDWORD, 0xC00F0038);
	}
}

void phy_set_rf_path_switch_8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	boolean		is_main
)
{

	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
#endif

#if DISABLE_BB_RF
	return;
#endif

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_phy_set_rf_path_switch_8703b(p_adapter, is_main, true);
#endif

}


/*return value true => WIFI(S1); false => BT(S0)*/
boolean _phy_query_rf_path_switch_8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	boolean		is2T
)
{
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
#endif
#endif


	if (odm_get_bb_reg(p_dm_odm, 0x7C4, MASKLWORD) == 0x7703)
		return true;
	else
		return false;

}



/*return value true => WIFI(S1); false => BT(S0)*/
boolean phy_query_rf_path_switch_8703b(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm
#else
	struct _ADAPTER	*p_adapter
#endif
)
{

#if DISABLE_BB_RF
	return true;
#endif

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	return _phy_query_rf_path_switch_8703b(p_adapter, false);
#else
	return _phy_query_rf_path_switch_8703b(p_dm_odm, false);
#endif

}
