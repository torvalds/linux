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
	#if RT_PLATFORM==PLATFORM_MACOSX
	#include "phydm_precomp.h"
	#else
	#include "../phydm_precomp.h"
	#endif
#else
#include "../../phydm_precomp.h"
#endif


#if (RTL8723D_SUPPORT == 1)

/*---------------------------Define Local Constant---------------------------*/
/*IQK*/
#define IQK_DELAY_TIME_8723D	10

/* 2010/04/25 MH Define the max tx power tracking tx agc power.*/
#define		ODM_TXPWRTRACK_MAX_IDX_8723D		6

#define     PATH_S1                         0
#define     idx_0xc94                       0
#define     idx_0xc80                       1
#define     idx_0xc4c                       2

#define     idx_0xc14                       0
#define     idx_0xca0                       1


#define     PATH_S0                         1
#define     idx_0xcd0                       0
#define     idx_0xcd4                       1

#define     idx_0xcd8                       0
#define     idx_0xcdc                       1

#define     KEY                             0
#define     VAL                             1



/*---------------------------Define Local Constant---------------------------*/


/* Tx Power Tracking*/


void set_iqk_matrix_8723d(
	struct PHY_DM_STRUCT	*p_dm,
	u8		OFDM_index,
	u8		rf_path,
	s32		iqk_result_x,
	s32		iqk_result_y
)
{
	s32			ele_A = 0, ele_D = 0, ele_C = 0, value32;
	s32			ele_A_ext = 0, ele_C_ext = 0, ele_D_ext = 0;

	if (OFDM_index >= OFDM_TABLE_SIZE)
		OFDM_index = OFDM_TABLE_SIZE - 1;
	else if (OFDM_index < 0)
		OFDM_index = 0;

	if ((iqk_result_x != 0) && (*(p_dm->p_band_type) == ODM_BAND_2_4G)) {

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
		case RF_PATH_A:
			/* write new elements A, C, D to regC80, regC94, reg0xc4c, and element B is always 0 */
			/* write 0xc80 */
			value32 = (ele_D << 22) | ((ele_C & 0x3F) << 16) | ele_A;
			odm_set_bb_reg(p_dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD, value32);
			/* write 0xc94 */
			value32 = (ele_C & 0x000003C0) >> 6;
			odm_set_bb_reg(p_dm, REG_OFDM_0_XC_TX_AFE, MASKH4BITS, value32);
			/* write 0xc4c */
			value32 = (ele_D_ext << 28) | (ele_A_ext << 31) | (ele_C_ext << 29);
			value32 = (odm_get_bb_reg(p_dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD) & (~(BIT(31) | BIT(29) | BIT(28)))) | value32;
			odm_set_bb_reg(p_dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD, value32);
			break;

		case RF_PATH_B:
			/*wirte new elements A, C, D to regCd0 and regCd4, element B is always 0*/
			value32 = ele_D;
			odm_set_bb_reg(p_dm, 0xCd4, 0x007FE000, value32);

			value32 = ele_C;
			odm_set_bb_reg(p_dm, 0xCd4, 0x000007FE, value32);

			value32 = ele_A;
			odm_set_bb_reg(p_dm, 0xCd0, 0x000007FE, value32);

			odm_set_bb_reg(p_dm, 0xCd4, BIT(12), ele_D_ext);
			odm_set_bb_reg(p_dm, 0xCd0, BIT(0), ele_A_ext);
			odm_set_bb_reg(p_dm, 0xCd4, BIT(0), ele_C_ext);
			break;
		default:
			break;
		}
	} else {
		switch (rf_path) {
		case RF_PATH_A:
			odm_set_bb_reg(p_dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD, ofdm_swing_table_new[OFDM_index]);
			odm_set_bb_reg(p_dm, REG_OFDM_0_XC_TX_AFE, MASKH4BITS, 0x00);
			value32 = odm_get_bb_reg(p_dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD) & (~(BIT(31) | BIT(29) | BIT(28)));
			odm_set_bb_reg(p_dm, REG_OFDM_0_ECCA_THRESHOLD, MASKDWORD, value32);
			break;

		case RF_PATH_B:
			/*image S1:c80 to S0:Cd0 and Cd4*/
			odm_set_bb_reg(p_dm, 0xcd0, 0x000007FE, ofdm_swing_table_new[OFDM_index] & 0x000003FF);
			odm_set_bb_reg(p_dm, 0xcd0, 0x0007E000, (ofdm_swing_table_new[OFDM_index] & 0x0000FC00) >> 10);
			odm_set_bb_reg(p_dm, 0xcd4, 0x0000007E, (ofdm_swing_table_new[OFDM_index] & 0x003F0000) >> 16);
			odm_set_bb_reg(p_dm, 0xcd4, 0x007FE000, (ofdm_swing_table_new[OFDM_index] & 0xFFC00000) >> 22);

			odm_set_bb_reg(p_dm, 0xcd4, 0x00000780, 0x00);

			odm_set_bb_reg(p_dm, 0xcd4, BIT(12), 0x0);
			odm_set_bb_reg(p_dm, 0xcd4, BIT(0), 0x0);
			odm_set_bb_reg(p_dm, 0xcd0, BIT(0), 0x0);
			break;
		default:
			break;
		}
	}
	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("TxPwrTracking path %c: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x ele_A_ext = 0x%x ele_C_ext = 0x%x ele_D_ext = 0x%x\n",
		(rf_path == RF_PATH_A ? 'A' : 'B'), (u32)iqk_result_x, (u32)iqk_result_y, (u32)ele_A, (u32)ele_C, (u32)ele_D, (u32)ele_A_ext, (u32)ele_C_ext, (u32)ele_D_ext));
}

void
set_cck_filter_coefficient_8723d(
	struct PHY_DM_STRUCT	*p_dm,
	u8		cck_swing_index
)
{
	odm_set_bb_reg(p_dm, 0xab4, 0x000007FF, cck_swing_table_ch1_ch14_8723d[cck_swing_index]);
}

void do_iqk_8723d(
	void		*p_dm_void,
	u8		delta_thermal_index,
	u8		thermal_value,
	u8		threshold
)
{
	u32  is_bt_enable = 0;

	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (*(p_dm->p_mp_mode) == false)
		is_bt_enable = odm_get_mac_reg(p_dm, 0xa8, MASKDWORD) & BIT(17);

	if (is_bt_enable) {
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Skip IQK because BT is enable\n"));
		return;
	} else
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]Do IQK because BT is disable\n"));

	odm_reset_iqk_result(p_dm);

	p_dm->rf_calibrate_info.thermal_value_iqk = thermal_value;

	halrf_iqk_trigger(p_dm, false);
}

/*-----------------------------------------------------------------------------
 * Function:	odm_tx_pwr_track_set_pwr_8723d()
 *
 * Overview:	8723D change all channel tx power accordign to flag.
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
odm_tx_pwr_track_set_pwr_8723d(
	void		*p_dm_void,
	enum pwrtrack_method	method,
	u8				rf_path,
	u8				channel_mapped_index
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER			*adapter = p_dm->adapter;
	//PHAL_DATA_TYPE			p_hal_data = GET_HAL_DATA(adapter);
	struct odm_rf_calibration_structure			*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
	struct _hal_rf_ *p_rf = &(p_dm->rf_table);
	u8					pwr_tracking_limit_ofdm = 30;
	u8					pwr_tracking_limit_cck = 40;
	u8					tx_rate = 0xFF;
	u8					final_ofdm_swing_index = 0;
	u8					final_cck_swing_index = 0;
	u8					i = 0;

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
			if (p_dm->number_linked_client != 0)
				tx_rate = hw_rate_to_m_rate(p_dm->tx_rate);
			else
				tx_rate = p_rf->p_rate_index;
#endif
		} else   /*force rate*/
			tx_rate = (u8)rate;
	}


	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("===>ODM_TxPwrTrackSetPwr8723DA\n"));

	if (tx_rate != 0xFF) {
		/*CCK*/
		if (((tx_rate >= MGN_1M) && (tx_rate <= MGN_5_5M)) || tx_rate == MGN_11M)
			pwr_tracking_limit_cck = 40;
		/*OFDM*/
		else if ((tx_rate >= MGN_6M) && (tx_rate <= MGN_48M))
			pwr_tracking_limit_ofdm = 36;
		else if (tx_rate == MGN_54M)
			pwr_tracking_limit_ofdm = 34;

		/* HT*/
		else if ((tx_rate >= MGN_MCS0) && (tx_rate <= MGN_MCS2))
			pwr_tracking_limit_ofdm = 38;
		else if ((tx_rate >= MGN_MCS3) && (tx_rate <= MGN_MCS4))
			pwr_tracking_limit_ofdm = 36;
		else if ((tx_rate >= MGN_MCS5) && (tx_rate <= MGN_MCS7))
			pwr_tracking_limit_ofdm = 34;

		else if ((tx_rate >= MGN_MCS8) && (tx_rate <= MGN_MCS10))
			pwr_tracking_limit_ofdm = 38;
		else if ((tx_rate >= MGN_MCS11) && (tx_rate <= MGN_MCS12))
			pwr_tracking_limit_ofdm = 36;
		else if ((tx_rate >= MGN_MCS13) && (tx_rate <= MGN_MCS15))
			pwr_tracking_limit_ofdm = 34;

		else
			pwr_tracking_limit_ofdm =  p_rf_calibrate_info->default_ofdm_index;   /*Default OFDM index = 30 */
	}

	if (method == TXAGC) {
		u8	rf = 0;
		u32	pwr = 0, tx_agc = 0;
		//struct _ADAPTER *adapter = p_dm->adapter;

		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("odm_TxPwrTrackSetPwr_8723D CH=%d\n", *(p_dm->p_channel)));

		p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path] = p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path];   /* Remnant index equal to aboslute compensate value. */

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))

#if (MP_DRIVER != 1)
#if 0
		PHY_SetTxPowerLevelByPath8723D(adapter, *p_dm->p_channel, rf_path);   /* Using new set power function */
		/* PHY_SetTxPowerLevel8723D(p_dm->adapter, *p_dm->p_channel); */
#endif
		p_rf_calibrate_info->modify_tx_agc_flag_path_a = true;
		p_rf_calibrate_info->modify_tx_agc_flag_path_b = true;
		p_rf_calibrate_info->modify_tx_agc_flag_path_a_cck = true;
		if (rf_path == RF_PATH_A) {
			odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, *p_dm->p_channel, CCK);
			odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, *p_dm->p_channel, OFDM);
			odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, *p_dm->p_channel, HT_MCS0_MCS7);
			odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, *p_dm->p_channel, HT_MCS8_MCS15);
		} else {
			odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_B, *p_dm->p_channel, CCK);
			odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_B, *p_dm->p_channel, OFDM);
			odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_B, *p_dm->p_channel, HT_MCS0_MCS7);
			odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_B, *p_dm->p_channel, HT_MCS8_MCS15);
		}
#else

		if (rf_path == RF_PATH_A) {
			/*CCK path S1*/
			pwr = odm_get_bb_reg(p_dm, REG_TX_AGC_A_RATE18_06, 0xFF);
			pwr += p_rf_calibrate_info->power_index_offset[RF_PATH_A];
			odm_set_bb_reg(p_dm, REG_TX_AGC_A_CCK_1_MCS32, MASKBYTE1, pwr);
			tx_agc = (pwr << 16) | (pwr << 8) | (pwr);
			odm_set_bb_reg(p_dm, REG_TX_AGC_B_CCK_11_A_CCK_2_11, 0x00ffffff, tx_agc);
			RT_DISP(FPHY, PHY_TXPWR, ("odm_tx_pwr_track_set_pwr_8723d: CCK Tx-rf(A) Power = 0x%x\n", tx_agc));

			/*OFDM path S1*/
			pwr = odm_get_bb_reg(p_dm, REG_TX_AGC_A_RATE18_06, 0xFF);
			pwr += (p_rf_calibrate_info->bb_swing_idx_ofdm[RF_PATH_A] - p_rf_calibrate_info->bb_swing_idx_ofdm_base[RF_PATH_A]);
			tx_agc = ((pwr << 24) | (pwr << 16) | (pwr << 8) | pwr);
			odm_set_bb_reg(p_dm, REG_TX_AGC_A_RATE18_06, MASKDWORD, tx_agc);
			odm_set_bb_reg(p_dm, REG_TX_AGC_A_RATE54_24, MASKDWORD, tx_agc);
			odm_set_bb_reg(p_dm, REG_TX_AGC_A_MCS03_MCS00, MASKDWORD, tx_agc);
			odm_set_bb_reg(p_dm, REG_TX_AGC_A_MCS07_MCS04, MASKDWORD, tx_agc);
			odm_set_bb_reg(p_dm, REG_TX_AGC_A_MCS11_MCS08, MASKDWORD, tx_agc);
			odm_set_bb_reg(p_dm, REG_TX_AGC_A_MCS15_MCS12, MASKDWORD, tx_agc);
			RT_DISP(FPHY, PHY_TXPWR, ("odm_tx_pwr_track_set_pwr_8723d: OFDM Tx-rf(A) Power = 0x%x\n", tx_agc));
		} else if (rf_path == RF_PATH_B) {

			pwr = odm_get_bb_reg(p_dm, REG_TX_AGC_B_RATE18_06, 0xFF);
			pwr += p_rf_calibrate_info->power_index_offset[RF_PATH_B];
			odm_set_bb_reg(p_dm, REG_TX_AGC_B_CCK_1_55_MCS32, MASKBYTE3, pwr);
			odm_set_bb_reg(p_dm, REG_TX_AGC_B_CCK_11_A_CCK_2_11, 0xff000000, pwr);
			RT_DISP(FPHY, PHY_TXPWR, ("odm_tx_pwr_track_set_pwr_8723d: CCK Tx-rf(B) Power = 0x%x\n", pwr));


			pwr = odm_get_bb_reg(p_dm, REG_TX_AGC_B_RATE18_06, 0xFF);
			pwr += (p_rf_calibrate_info->bb_swing_idx_ofdm[RF_PATH_B] - p_rf_calibrate_info->bb_swing_idx_ofdm_base[RF_PATH_B]);
			tx_agc = ((pwr << 24) | (pwr << 16) | (pwr << 8) | pwr);
			odm_set_bb_reg(p_dm, REG_TX_AGC_B_RATE18_06, MASKDWORD, tx_agc);
			odm_set_bb_reg(p_dm, REG_TX_AGC_B_RATE54_24, MASKDWORD, tx_agc);
			odm_set_bb_reg(p_dm, REG_TX_AGC_B_MCS03_MCS00, MASKDWORD, tx_agc);
			odm_set_bb_reg(p_dm, REG_TX_AGC_B_MCS07_MCS04, MASKDWORD, tx_agc);
			odm_set_bb_reg(p_dm, REG_TX_AGC_B_MCS11_MCS08, MASKDWORD, tx_agc);
			odm_set_bb_reg(p_dm, REG_TX_AGC_B_MCS15_MCS12, MASKDWORD, tx_agc);
			RT_DISP(FPHY, PHY_TXPWR, ("odm_tx_pwr_track_set_pwr_8723d: OFDM Tx-rf(B) Power = 0x%x\n", tx_agc));
		}
#endif

#endif
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
		/*phy_rf6052_set_cck_tx_power(p_dm->priv, *(p_dm->p_channel));
		  phy_rf6052_set_ofdm_tx_power(p_dm->priv, *(p_dm->p_channel));*/
#endif

	} else if (method == BBSWING) {
		final_ofdm_swing_index = p_rf_calibrate_info->default_ofdm_index + p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path];
		final_cck_swing_index = p_rf_calibrate_info->default_cck_index + p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path];

		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			(" p_rf_calibrate_info->default_ofdm_index=%d,  p_rf_calibrate_info->DefaultCCKIndex=%d, p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path]=%d, p_rf_calibrate_info->remnant_cck_swing_idx=%d   rf_path = %d\n",
			p_rf_calibrate_info->default_ofdm_index, p_rf_calibrate_info->default_cck_index, p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path], p_rf_calibrate_info->remnant_cck_swing_idx, rf_path));

		/* Adjust BB swing by OFDM IQ matrix */
		if (final_ofdm_swing_index >= pwr_tracking_limit_ofdm)
			final_ofdm_swing_index = pwr_tracking_limit_ofdm;
		else if (final_ofdm_swing_index < 0)
			final_ofdm_swing_index = 0;

		if (final_cck_swing_index >= CCK_TABLE_SIZE_8723D)
			final_cck_swing_index = CCK_TABLE_SIZE_8723D - 1;
		else if (p_rf_calibrate_info->bb_swing_idx_cck < 0)
			final_cck_swing_index = 0;

		set_iqk_matrix_8723d(p_dm, final_ofdm_swing_index, RF_PATH_A,
			p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
			p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][1]);

		set_iqk_matrix_8723d(p_dm, final_ofdm_swing_index, RF_PATH_B,
			p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][4],
			p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][5]);

		set_cck_filter_coefficient_8723d(p_dm, final_cck_swing_index);

		p_rf_calibrate_info->modify_tx_agc_flag_path_a = true;

		odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, *p_dm->p_channel, CCK);
		odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, *p_dm->p_channel, OFDM);
		odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, *p_dm->p_channel, HT_MCS0_MCS7);

		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("final_cck_swing_index=%d\n", final_cck_swing_index));

	} else if (method == MIX_MODE) {
#if (MP_DRIVER == 1)
		u32	tx_agc = 0;			  /*add by Mingzhi.Guo 2015-04-10*/
		s32	pwr = 0;
#endif
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("p_dm->default_ofdm_index=%d,  p_dm->DefaultCCKIndex=%d, p_dm->absolute_ofdm_swing_idx[rf_path]=%d, rf_path = %d\n",
			p_rf_calibrate_info->default_ofdm_index, p_rf_calibrate_info->default_cck_index, p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path], rf_path));

		final_ofdm_swing_index = p_rf_calibrate_info->default_ofdm_index + p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path];

		if (rf_path == RF_PATH_A) {
			final_cck_swing_index = p_rf_calibrate_info->default_cck_index + p_rf_calibrate_info->absolute_ofdm_swing_idx[rf_path];    /*CCK Follow path-A and lower CCK index means higher power.*/

			if (final_ofdm_swing_index > pwr_tracking_limit_ofdm) {
				p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path] = final_ofdm_swing_index - pwr_tracking_limit_ofdm;

				set_iqk_matrix_8723d(p_dm, pwr_tracking_limit_ofdm, RF_PATH_A,
					p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
					p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][1]);
				set_iqk_matrix_8723d(p_dm, pwr_tracking_limit_ofdm, RF_PATH_B,
					p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][4],
					p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][5]);

				p_rf_calibrate_info->modify_tx_agc_flag_path_a = true;

				/*Set tx_agc Page C{};*/
				/*odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, p_hal_data->current_channel, OFDM);*/
				/*	odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, p_hal_data->current_channel, HT_MCS0_MCS7);*/

				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A Over BBSwing Limit, pwr_tracking_limit = %d, Remnant tx_agc value = %d\n", pwr_tracking_limit_ofdm, p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path]));
			} else if (final_ofdm_swing_index < 0) {
				p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path] = final_ofdm_swing_index ;

				set_iqk_matrix_8723d(p_dm, 0, RF_PATH_A,
					p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
					p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][1]);
				set_iqk_matrix_8723d(p_dm, 0, RF_PATH_B,
					p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][4],
					p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][5]);

				p_rf_calibrate_info->modify_tx_agc_flag_path_a = true;

				/*Set tx_agc Page C{};*/
				/*odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, p_hal_data->current_channel, OFDM);*/
				/*	odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, p_hal_data->current_channel, HT_MCS0_MCS7);*/

				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A Lower then BBSwing lower bound  0, Remnant tx_agc value = %d\n", p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path]));
			} else {
				set_iqk_matrix_8723d(p_dm, final_ofdm_swing_index, RF_PATH_A,
					p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][0],
					p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][1]);
				set_iqk_matrix_8723d(p_dm, final_ofdm_swing_index, RF_PATH_B,
					p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][4],
					p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][5]);

				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A Compensate with BBSwing, final_ofdm_swing_index = %d\n", final_ofdm_swing_index));

				if (p_rf_calibrate_info->modify_tx_agc_flag_path_a) {
					p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path] = 0;

					/*Set tx_agc Page C{};*/
					/*	odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, p_hal_data->current_channel, OFDM );
						odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, p_hal_data->current_channel, HT_MCS0_MCS7 );
						odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, p_hal_data->current_channel, HT_MCS8_MCS15 );*/

					p_rf_calibrate_info->modify_tx_agc_flag_path_a = false;

					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A p_dm->Modify_TxAGC_Flag = false\n"));
				}
			}
#if (MP_DRIVER == 1)
			if ((*(p_dm->p_mp_mode)) == 1) {
				pwr = odm_get_bb_reg(p_dm, REG_TX_AGC_A_RATE18_06, 0xFF);
				pwr += (p_rf_calibrate_info->remnant_ofdm_swing_idx[RF_PATH_A] - p_rf_calibrate_info->modify_tx_agc_value_ofdm);

				if (pwr > 0x3F)
					pwr = 0x3F;
				else if (pwr < 0)
					pwr = 0;

				tx_agc |= ((pwr << 24) | (pwr << 16) | (pwr << 8) | pwr);
				odm_set_bb_reg(p_dm, REG_TX_AGC_A_RATE18_06, MASKDWORD, tx_agc);
				odm_set_bb_reg(p_dm, REG_TX_AGC_A_RATE54_24, MASKDWORD, tx_agc);
				odm_set_bb_reg(p_dm, REG_TX_AGC_A_MCS03_MCS00, MASKDWORD, tx_agc);
				odm_set_bb_reg(p_dm, REG_TX_AGC_A_MCS07_MCS04, MASKDWORD, tx_agc);

				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("ODM_TxPwrTrackSetPwr8188F: OFDM Tx-rf(A) Power = 0x%x\n", tx_agc));

			} else
#endif
			{

				odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, *p_dm->p_channel, OFDM);
				odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, *p_dm->p_channel, HT_MCS0_MCS7);
			}
			p_rf_calibrate_info->modify_tx_agc_value_ofdm = p_rf_calibrate_info->remnant_ofdm_swing_idx[RF_PATH_A] ;

			if (final_cck_swing_index > pwr_tracking_limit_cck) {
				p_rf_calibrate_info->remnant_cck_swing_idx = final_cck_swing_index - pwr_tracking_limit_cck;

				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A CCK Over Limit, pwr_tracking_limit_cck = %d, p_dm->remnant_cck_swing_idx  = %d\n", pwr_tracking_limit_cck, p_rf_calibrate_info->remnant_cck_swing_idx));

				/* Adjust BB swing by CCK filter coefficient*/
				odm_set_bb_reg(p_dm, 0xab4, 0x000007FF, cck_swing_table_ch1_ch14_8723d[pwr_tracking_limit_cck]);

				p_rf_calibrate_info->modify_tx_agc_flag_path_a_cck = true;

				/*Set tx_agc Page C{};*/
				/*	odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, p_hal_data->current_channel, CCK);
					odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_B, p_hal_data->current_channel, CCK);*/

			} else if (final_cck_swing_index < 0) {
				p_rf_calibrate_info->remnant_cck_swing_idx = final_cck_swing_index;

				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A CCK Under Limit, pwr_tracking_limit_cck = %d, p_dm->remnant_cck_swing_idx  = %d\n", 0, p_rf_calibrate_info->remnant_cck_swing_idx));

				odm_set_bb_reg(p_dm, 0xab4, 0x000007FF, cck_swing_table_ch1_ch14_8723d[0]);

				p_rf_calibrate_info->modify_tx_agc_flag_path_a_cck = true;


				/*odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, p_hal_data->current_channel, CCK);
				odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_B, p_hal_data->current_channel, CCK);*/

			} else {
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A CCK Compensate with BBSwing, final_cck_swing_index = %d\n", final_cck_swing_index));

				odm_set_bb_reg(p_dm, 0xab4, 0x000007FF, cck_swing_table_ch1_ch14_8723d[final_cck_swing_index]);

				/*	if (p_rf_calibrate_info->modify_tx_agc_flag_path_a_cck) {*/
				p_rf_calibrate_info->remnant_cck_swing_idx = 0;


				/*odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, p_hal_data->current_channel, CCK );
				odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_B, p_hal_data->current_channel, CCK );*/

				p_rf_calibrate_info->modify_tx_agc_flag_path_a_cck = false;

				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_A p_dm->Modify_TxAGC_Flag_CCK = false\n"));
			}
#if (MP_DRIVER == 1)
			if ((*(p_dm->p_mp_mode)) == 1) {
				pwr = odm_get_bb_reg(p_dm, REG_TX_AGC_B_CCK_11_A_CCK_2_11, MASKBYTE1);
				pwr += p_rf_calibrate_info->remnant_cck_swing_idx - p_rf_calibrate_info->modify_tx_agc_value_cck;

				if (pwr > 0x3F)
					pwr = 0x3F;
				else if (pwr < 0)
					pwr = 0;

				odm_set_bb_reg(p_dm, REG_TX_AGC_A_CCK_1_MCS32, MASKBYTE1, pwr);
				tx_agc = (pwr << 16) | (pwr << 8) | (pwr);
				odm_set_bb_reg(p_dm, REG_TX_AGC_B_CCK_11_A_CCK_2_11, 0xffffff00, tx_agc);
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("ODM_TxPwrTrackSetPwr8723D: CCK Tx-rf(A) Power = 0x%x\n", tx_agc));
			} else
#endif

				odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_A, *p_dm->p_channel, CCK);

			p_rf_calibrate_info->modify_tx_agc_value_cck = p_rf_calibrate_info->remnant_cck_swing_idx;
		}
#if 0
		if (rf_path == RF_PATH_B) {
			if (final_ofdm_swing_index > pwr_tracking_limit_ofdm) {
				p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path] = final_ofdm_swing_index - pwr_tracking_limit_ofdm;

				set_iqk_matrix_8723d(p_dm, pwr_tracking_limit_ofdm, RF_PATH_B,
					p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][4],
					p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][5]);

				p_rf_calibrate_info->modify_tx_agc_flag_path_a = true;


				/*odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_B, p_hal_data->current_channel, OFDM);
				odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_B, p_hal_data->current_channel, HT_MCS0_MCS7);*/

				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_B Over BBSwing Limit, pwr_tracking_limit = %d, Remnant tx_agc value = %d\n", pwr_tracking_limit_ofdm, p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path]));
			} else if (final_ofdm_swing_index < 0) {
				p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path] = final_ofdm_swing_index ;

				set_iqk_matrix_8723d(p_dm, 0, RF_PATH_B,
					p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][4],
					p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][5]);

				p_rf_calibrate_info->modify_tx_agc_flag_path_a = true;


				/*odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_B, p_hal_data->current_channel, OFDM);
				odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_B, p_hal_data->current_channel, HT_MCS0_MCS7);*/

				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_B Lower then BBSwing lower bound  0, Remnant tx_agc value = %d\n", p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path]));
			} else {
				set_iqk_matrix_8723d(p_dm, final_ofdm_swing_index, RF_PATH_B,
					p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][4],
					p_rf_calibrate_info->iqk_matrix_reg_setting[channel_mapped_index].value[0][5]);

				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_B Compensate with BBSwing, final_ofdm_swing_index = %d\n", final_ofdm_swing_index));

				if (p_rf_calibrate_info->modify_tx_agc_flag_path_b) {
					p_rf_calibrate_info->remnant_ofdm_swing_idx[rf_path] = 0;

					/*odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_B, p_hal_data->current_channel, OFDM);
					odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_B, p_hal_data->current_channel, HT_MCS0_MCS7);*/

					p_rf_calibrate_info->modify_tx_agc_flag_path_a = false;

					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("******Path_B p_dm->Modify_TxAGC_Flag = false\n"));
				}
			}
#if (MP_DRIVER == 1)
			if ((*(p_dm->p_mp_mode)) == 1) {
				pwr = odm_get_bb_reg(p_dm, REG_TX_AGC_A_RATE18_06, 0xFF);
				pwr += (p_rf_calibrate_info->remnant_ofdm_swing_idx[RF_PATH_B] - p_rf_calibrate_info->modify_tx_agc_value_ofdm);

				if (pwr > 0x3F)
					pwr = 0x3F;
				else if (pwr < 0)
					pwr = 0;

				tx_agc |= ((pwr << 24) | (pwr << 16) | (pwr << 8) | pwr);
				odm_set_bb_reg(p_dm, REG_TX_AGC_A_RATE18_06, MASKDWORD, tx_agc);
				odm_set_bb_reg(p_dm, REG_TX_AGC_A_RATE54_24, MASKDWORD, tx_agc);
				odm_set_bb_reg(p_dm, REG_TX_AGC_A_MCS03_MCS00, MASKDWORD, tx_agc);
				odm_set_bb_reg(p_dm, REG_TX_AGC_A_MCS07_MCS04, MASKDWORD, tx_agc);

				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("ODM_TxPwrTrackSetPwr8723D: OFDM Tx-rf(A) Power = 0x%x\n", tx_agc));

			} else
#endif
			{

				odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_B, *p_dm->p_channel, OFDM);
				odm_set_tx_power_index_by_rate_section(p_dm, RF_PATH_B, *p_dm->p_channel, HT_MCS0_MCS7);
			}
			p_rf_calibrate_info->modify_tx_agc_value_ofdm = p_rf_calibrate_info->remnant_ofdm_swing_idx[RF_PATH_B] ;
		}
#endif
	} else
		return;
}

void
get_delta_swing_table_8723d(
	void		*p_dm_void,
	u8 **temperature_up_a,
	u8 **temperature_down_a,
	u8 **temperature_up_b,
	u8 **temperature_down_b
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER		*adapter		 = p_dm->adapter;
	struct _hal_rf_ *p_rf = &(p_dm->rf_table);
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
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
			if (p_dm->number_linked_client != 0)
				tx_rate = hw_rate_to_m_rate(p_dm->tx_rate);
			else
				tx_rate = p_rf->p_rate_index;
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

void
get_delta_swing_xtal_table_8723d(
	void		*p_dm_void,
	s8 **temperature_up_xtal,
	s8 **temperature_down_xtal
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);

	*temperature_up_xtal   = p_rf_calibrate_info->delta_swing_table_xtal_p;
	*temperature_down_xtal = p_rf_calibrate_info->delta_swing_table_xtal_n;
}



void
odm_txxtaltrack_set_xtal_8723d(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm		= (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info	= &(p_dm->rf_calibrate_info);
	struct _ADAPTER		*adapter			= p_dm->adapter;
	HAL_DATA_TYPE	*p_hal_data		 = GET_HAL_DATA(adapter);

	s8	crystal_cap;


	crystal_cap = p_hal_data->crystal_cap & 0x3F;
	crystal_cap = crystal_cap + p_rf_calibrate_info->xtal_offset;

	if (crystal_cap < 0)
		crystal_cap = 0;
	else if (crystal_cap > 63)
		crystal_cap = 63;


	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("crystal_cap(%d)= p_hal_data->crystal_cap(%d) + p_rf_calibrate_info->xtal_offset(%d)\n", crystal_cap, p_hal_data->crystal_cap, p_rf_calibrate_info->xtal_offset));

	odm_set_bb_reg(p_dm, REG_MAC_PHY_CTRL, 0xFFF000, (crystal_cap | (crystal_cap << 6)));

	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("crystal_cap(0x2c)  0x%X\n", odm_get_bb_reg(p_dm, REG_MAC_PHY_CTRL, 0xFFF000)));

}

void configure_txpower_track_8723d(
	struct _TXPWRTRACK_CFG	*p_config
)
{
	p_config->swing_table_size_cck = CCK_TABLE_SIZE_8723D;
	p_config->swing_table_size_ofdm = OFDM_TABLE_SIZE;
	p_config->threshold_iqk = IQK_THRESHOLD;
	p_config->average_thermal_num = AVG_THERMAL_NUM_8723D;
	p_config->rf_path_count = MAX_PATH_NUM_8723D;
	p_config->thermal_reg_addr = RF_T_METER_88E;

	p_config->odm_tx_pwr_track_set_pwr = odm_tx_pwr_track_set_pwr_8723d;
	p_config->do_iqk = do_iqk_8723d;
	p_config->phy_lc_calibrate = halrf_lck_trigger;
	p_config->get_delta_swing_table = get_delta_swing_table_8723d;
	p_config->get_delta_swing_xtal_table = get_delta_swing_xtal_table_8723d;
	p_config->odm_txxtaltrack_set_xtal = odm_txxtaltrack_set_xtal_8723d;
}


#define MAX_TOLERANCE		5
#define IQK_DELAY_TIME		1

u8
phy_path_s1_iqk_8723d(
	struct PHY_DM_STRUCT		*p_dm,
	boolean	config_path_s0
)
{
	u32 reg_eac, reg_e94, reg_e9c, path_sel_bb;
	u8 result = 0x00, ktime;
	u32 original_path, original_gnt;
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]path S1 TXIQK!!\n"));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x67 @S1 TXIQK = 0x%x\n", odm_get_mac_reg(p_dm, 0x64, MASKBYTE3)));
	/*save RF path*/
	path_sel_bb = odm_get_bb_reg(p_dm, 0x948, MASKDWORD);
	/*ODM_RT_TRACE(p_dm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x1e6@S1 TXIQK = 0x%x\n", platform_efio_read_1byte(p_adapter, 0x1e6)));*/
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, 0x99000000);

	/*IQK setting*/
	/*leave IQK mode*/
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	/* --- \A7\EF\BCgTXIQK mode table ---//*/
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, RFREGOFFSETMASK, 0x80000);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x33, RFREGOFFSETMASK, 0x00004);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x3e, RFREGOFFSETMASK, 0x0005d);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x3f, RFREGOFFSETMASK, 0xBFFE0);

	/*path-A IQK setting*/
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x08008c0c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_A, MASKDWORD, 0x8214019f);
	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_A, MASKDWORD, 0x28160200);
	odm_set_bb_reg(p_dm, REG_TX_IQK, MASKDWORD, 0x01007c00);
	odm_set_bb_reg(p_dm, REG_RX_IQK, MASKDWORD, 0x01004800);

	/*LO calibration setting*/
	odm_set_bb_reg(p_dm, REG_IQK_AGC_RSP, MASKDWORD, 0x00462911);

	/*PA, PAD setting*/
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xdf, 0x800, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x56, 0x600, 0x0);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x56, 0x1E0, 0x3);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x8d, 0x1F, 0xf);

	/*LOK setting  added for 8723D*/
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, 0x10, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x54, 0x1, 0x1);
#if 1

	odm_set_rf_reg(p_dm, RF_PATH_A, 0x1, RFREGOFFSETMASK, 0xe0d);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x2, RFREGOFFSETMASK, 0x60d);
#endif

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RF0x1 @S1 TXIQK = 0x%x\n", odm_get_rf_reg(p_dm, RF_PATH_A, 0x1, RFREGOFFSETMASK)));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RF0x2 @S1 TXIQK = 0x%x\n", odm_get_rf_reg(p_dm, RF_PATH_A, 0x2, RFREGOFFSETMASK)));

	/*enter IQK mode*/
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);

#if 1
	/*backup path & GNT value */
	original_path = odm_get_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, MASKDWORD);  /*save 0x70*/
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
	ODM_delay_ms(1);
	original_gnt = odm_get_bb_reg(p_dm, REG_LTECOEX_READ_DATA, MASKDWORD);  /*save 0x38*/
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]OriginalGNT = 0x%x\n", original_gnt));

	/*set GNT_WL=1/GNT_BT=1  and path owner to WiFi for pause BT traffic*/
	odm_set_bb_reg(p_dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, 0x0000ff00);
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc0020038);	/*0x38[15:8] = 0x77*/
	odm_set_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, BIT(26), 0x1);
#endif

	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0054);
	ODM_delay_ms(1);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]GNT_BT @S1 TXIQK = 0x%x\n", odm_get_bb_reg(p_dm, REG_LTECOEX_READ_DATA, MASKDWORD)));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x948 @S1 TXIQK = 0x%x\n", odm_get_bb_reg(p_dm, 0x948, MASKDWORD)));

	/*One shot, path S1 LOK & IQK*/
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xfa000000);
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/* delay x ms */
	/*ODM_delay_ms(IQK_DELAY_TIME_8723D);*/

	ktime = 0;
	while ((!odm_get_bb_reg(p_dm, 0xeac, BIT(26))) && ktime < 10) {
		ODM_delay_ms(1);
		ktime++;
	}

#if 1
	/*Restore GNT_WL/GNT_BT  and path owner*/
	odm_set_bb_reg(p_dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, original_gnt);
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc00f0038);
	odm_set_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, original_path);
#endif

	/*reload RF path*/
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, path_sel_bb);

	/*leave IQK mode*/
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	/*PA/PAD controlled by 0x0*/
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xdf, 0x800, 0x0);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x1, BIT(0), 0x0);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x2, BIT(0), 0x0);

	/* Check failed*/
	reg_eac = odm_get_bb_reg(p_dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_e94 = odm_get_bb_reg(p_dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD);
	reg_e9c = odm_get_bb_reg(p_dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac = 0x%x\n", reg_eac));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe94 = 0x%x, 0xe9c = 0x%x\n", reg_e94, reg_e9c));
	/*monitor image power before & after IQK*/
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe90(before IQK)= 0x%x, 0xe98(afer IQK) = 0x%x\n",
		odm_get_bb_reg(p_dm, 0xe90, MASKDWORD), odm_get_bb_reg(p_dm, 0xe98, MASKDWORD)));

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))

		result |= 0x01;
	else
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("S1 TXIQK FAIL\n"));
	return result;
}

u8
phy_path_s1_rx_iqk_8723d(
	struct PHY_DM_STRUCT		*p_dm,
	boolean	config_path_s0
)
{
	u32 reg_eac, reg_e94, reg_e9c, reg_ea4, u4tmp, tmp, path_sel_bb;
	u8 result = 0x00, ktime;
	u32 original_path, original_gnt;

	path_sel_bb = odm_get_bb_reg(p_dm, 0x948, MASKDWORD);

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]path S1 RXIQK Step1!!\n"));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x67 @S1 RXIQK1 = 0x%x\n", odm_get_mac_reg(p_dm, 0x64, MASKBYTE3)));
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, 0x99000000);
	/*ODM_RT_TRACE(p_dm,ODM_COMP_INIT, ODM_DBG_LOUD, ("[IQK]0x1e6@S1 RXIQK1 = 0x%x\n", platform_efio_read_1byte(p_adapter, 0x1e6)));	*/
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	/*IQK setting*/
	odm_set_bb_reg(p_dm, REG_TX_IQK, MASKDWORD, 0x01007c00);
	odm_set_bb_reg(p_dm, REG_RX_IQK, MASKDWORD, 0x01004800);

	/*path-A IQK setting*/
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_B, MASKDWORD, 0x38008c1c);

	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_A, MASKDWORD, 0x82160000);
	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_A, MASKDWORD, 0x28160000);

	/*LO calibration setting*/
	odm_set_bb_reg(p_dm, REG_IQK_AGC_RSP, MASKDWORD, 0x0046a911);

	/*modify RXIQK mode table*/
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xef, RFREGOFFSETMASK, 0x80000);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x33, RFREGOFFSETMASK, 0x00006);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x3e, RFREGOFFSETMASK, 0x0005f);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x3f, RFREGOFFSETMASK, 0xa7ffb);

	/*---------PA/PAD=0----------*/
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xdf, 0x800, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x56, 0x600, 0x0);
#if 1
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x1, RFREGOFFSETMASK, 0xe0d);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x2, RFREGOFFSETMASK, 0x60d);
#endif
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RF0x1@ path S1 RXIQK1 = 0x%x\n", odm_get_rf_reg(p_dm, RF_PATH_A, 0x1, RFREGOFFSETMASK)));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RF0x2@ path S1 RXIQK1 = 0x%x\n", odm_get_rf_reg(p_dm, RF_PATH_A, 0x2, RFREGOFFSETMASK)));

	/*enter IQK mode*/
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);
#if 1
	/*backup path & GNT value */
	original_path = odm_get_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, MASKDWORD);  /*save 0x70*/
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
	ODM_delay_ms(1);
	original_gnt = odm_get_bb_reg(p_dm, REG_LTECOEX_READ_DATA, MASKDWORD);  /*save 0x38*/
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]OriginalGNT = 0x%x\n", original_gnt));

	/*set GNT_WL=1/GNT_BT=1  and path owner to WiFi for pause BT traffic*/
	odm_set_bb_reg(p_dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, 0x0000ff00);
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc0020038);	/*0x38[15:8] = 0x77*/
	odm_set_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, BIT(26), 0x1);
#endif
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0054);
	ODM_delay_ms(1);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]GNT_BT @S1 RXIQK1 = 0x%x\n", odm_get_bb_reg(p_dm, REG_LTECOEX_READ_DATA, MASKDWORD)));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x948 @S1 RXIQK1 = 0x%x\n", odm_get_bb_reg(p_dm, 0x948, MASKDWORD)));

	/*One shot, path S1 LOK & IQK*/
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/*delay x ms*/
	/*ODM_delay_ms(IQK_DELAY_TIME_8723D);*/

	ktime = 0;
	while ((!odm_get_bb_reg(p_dm, 0xeac, BIT(26))) && ktime < 10) {
		ODM_delay_ms(1);
		ktime++;
	}
	reg_eac = odm_get_bb_reg(p_dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_e94 = odm_get_bb_reg(p_dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD);
	reg_e9c = odm_get_bb_reg(p_dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac = 0x%x\n", reg_eac));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe94 = 0x%x, 0xe9c = 0x%x\n", reg_e94, reg_e9c));
	/*monitor image power before & after IQK*/
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe90(before IQK)= 0x%x, 0xe98(afer IQK) = 0x%x\n",
		odm_get_bb_reg(p_dm, 0xe90, MASKDWORD), odm_get_bb_reg(p_dm, 0xe98, MASKDWORD)));
	tmp = (reg_e9c & 0x03FF0000) >> 16;
	if ((tmp & 0x200) > 0)
		tmp = 0x400 - tmp;

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))

		result |= 0x01;
	else {
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("S1 RXIQK STEP1 FAIL\n"));
#if 1
		/*Restore GNT_WL/GNT_BT  and path owner*/
		odm_set_bb_reg(p_dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, original_gnt);
		odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc00f0038);
		odm_set_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, original_path);
#endif
		/*reload RF path*/
		odm_set_bb_reg(p_dm, 0x948, MASKDWORD, path_sel_bb);
		odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
		odm_set_rf_reg(p_dm, RF_PATH_A, 0xdf, 0x800, 0x0);
		odm_set_rf_reg(p_dm, RF_PATH_A, 0x1, BIT(0), 0x0);
		odm_set_rf_reg(p_dm, RF_PATH_A, 0x2, BIT(0), 0x0);
		return result;
	}

	u4tmp = 0x80007C00 | (reg_e94 & 0x3FF0000)  | ((reg_e9c & 0x3FF0000) >> 16);
	odm_set_bb_reg(p_dm, REG_TX_IQK, MASKDWORD, u4tmp);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe40 = 0x%x u4tmp = 0x%x\n", odm_get_bb_reg(p_dm, REG_TX_IQK, MASKDWORD), u4tmp));

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]path S1 RXIQK STEP2!!\n"));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x67 @S1 RXIQK2 = 0x%x\n", odm_get_mac_reg(p_dm, 0x64, MASKBYTE3)));
	/*ODM_RT_TRACE(p_dm,ODM_COMP_INIT, ODM_DBG_LOUD, ("[IQK]0x1e6@S1 RXIQK2 = 0x%x\n", platform_efio_read_1byte(p_adapter, 0x1e6)));	*/
	odm_set_bb_reg(p_dm, REG_RX_IQK, MASKDWORD, 0x01004800);

	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_B, MASKDWORD, 0x38008c1c);

	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_A, MASKDWORD, 0x82170000);
	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_A, MASKDWORD, 0x28171400);

	/*LO calibration setting*/
	odm_set_bb_reg(p_dm, REG_IQK_AGC_RSP, MASKDWORD, 0x0046a8d1);


	/*modify RXIQK mode table*/
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_WE_LUT, 0x80000, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x33, RFREGOFFSETMASK, 0x00007);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x3e, RFREGOFFSETMASK, 0x0005f);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x3f, RFREGOFFSETMASK, 0xb3fdb);

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RF0x1 @S1 RXIQK2 = 0x%x\n", odm_get_rf_reg(p_dm, RF_PATH_A, 0x1, RFREGOFFSETMASK)));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RF0x2 @S1 RXIQK2 = 0x%x\n", odm_get_rf_reg(p_dm, RF_PATH_A, 0x2, RFREGOFFSETMASK)));

	/*enter IQK mode*/
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);

#if 1
	/*backup path & GNT value */
	original_path = odm_get_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, MASKDWORD);  /*save 0x70*/
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
	ODM_delay_ms(1);
	original_gnt = odm_get_bb_reg(p_dm, REG_LTECOEX_READ_DATA, MASKDWORD);  /*save 0x38*/
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]OriginalGNT = 0x%x\n", original_gnt));

	/*set GNT_WL=1/GNT_BT=1  and path owner to WiFi for pause BT traffic*/
	odm_set_bb_reg(p_dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, 0x0000ff00);
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc0020038);	/*0x38[15:8] = 0x77*/
	odm_set_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, BIT(26), 0x1);
#endif
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0054);
	ODM_delay_ms(1);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]GNT_BT @S1 RXIQK2 = 0x%x\n", odm_get_bb_reg(p_dm, REG_LTECOEX_READ_DATA, MASKDWORD)));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x948 @S1 RXIQK2 = 0x%x\n", odm_get_bb_reg(p_dm, 0x948, MASKDWORD)));

	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);


	/*ODM_delay_ms(IQK_DELAY_TIME_8723D);*/

	ktime = 0;
	while ((!odm_get_bb_reg(p_dm, 0xeac, BIT(26))) && ktime < 10) {
		ODM_delay_ms(1);
		ktime++;
	}

#if 1
	/*Restore GNT_WL/GNT_BT  and path owner*/
	odm_set_bb_reg(p_dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, original_gnt);
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc00f0038);
	odm_set_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, original_path);
#endif
	/*reload RF path*/
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, path_sel_bb);

	/*leave IQK mode*/
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	/*	PA/PAD controlled by 0x0*/
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xdf, 0x800, 0x0);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x1, BIT(0), 0x0);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x2, BIT(0), 0x0);

	reg_eac = odm_get_bb_reg(p_dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_ea4 = odm_get_bb_reg(p_dm, REG_RX_POWER_BEFORE_IQK_A_2, MASKDWORD);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac = 0x%x\n", reg_eac));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xea4 = 0x%x, 0xeac = 0x%x\n", reg_ea4, reg_eac));

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xea0(before IQK)= 0x%x, 0xea8(afer IQK) = 0x%x\n",
		odm_get_bb_reg(p_dm, 0xea0, MASKDWORD), odm_get_bb_reg(p_dm, 0xea8, MASKDWORD)));

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
	else
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("S1 RXIQK STEP2 FAIL\n"));
	return result;
}


u8
phy_path_s0_iqk_8723d(
	struct PHY_DM_STRUCT		*p_dm
)
{
	u32 reg_eac, reg_e94, reg_e9c, reg_e94_s0, reg_e9c_s0, reg_ea4_s0, reg_eac_s0, tmp, path_sel_bb;
	u8	result = 0x00, ktime;
	u32 original_path, original_gnt;

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path S0 TXIQK!\n"));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x67 @S0 TXIQK = 0x%x\n", odm_get_mac_reg(p_dm, 0x64, MASKBYTE3)));
	path_sel_bb = odm_get_bb_reg(p_dm, 0x948, MASKDWORD);

	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, 0x99000280); /*10 od 0x948 0x1 [7] ; WL:S1 to S0;BT:S0 to S1;*/
	/*ODM_RT_TRACE(p_dm,ODM_COMP_INIT, ODM_DBG_LOUD, ("[IQK]0x1e6@S0 TXIQK = 0x%x\n", platform_efio_read_1byte(p_adapter, 0x1e6)));*/

	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	/*modify TXIQK mode table*/
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xee, RFREGOFFSETMASK, 0x80000);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x33, RFREGOFFSETMASK, 0x00004);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x3e, RFREGOFFSETMASK, 0x0005d);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x3f, RFREGOFFSETMASK, 0xBFFE0);

	/*path-A IQK setting*/
	odm_set_bb_reg(p_dm, 0xe30, MASKDWORD, 0x08008c0c);
	odm_set_bb_reg(p_dm, 0xe34, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, 0xe38, MASKDWORD, 0x8214018a);
	odm_set_bb_reg(p_dm, 0xe3c, MASKDWORD, 0x28160200);
	odm_set_bb_reg(p_dm, REG_TX_IQK, MASKDWORD, 0x01007c00);
	odm_set_bb_reg(p_dm, REG_RX_IQK, MASKDWORD, 0x01004800);

	/*LO calibration setting*/
	odm_set_bb_reg(p_dm, REG_IQK_AGC_RSP, MASKDWORD, 0x00462911);

	/*PA, PAD setting*/
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xde, 0x800, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x66, 0x600, 0x0);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x66, 0x1E0, 0x3);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x8d, 0x1F, 0xf);

	/*LOK setting	added for 8723D*/
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xee, 0x10, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x64, 0x1, 0x1);

#if 1
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x1, RFREGOFFSETMASK, 0xe6d);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x2, RFREGOFFSETMASK, 0x66d);
#endif

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RF0x1 @S0 TXIQK = 0x%x\n", odm_get_rf_reg(p_dm, RF_PATH_A, 0x1, RFREGOFFSETMASK)));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RF0x2 @S0 TXIQK = 0x%x\n", odm_get_rf_reg(p_dm, RF_PATH_A, 0x2, RFREGOFFSETMASK)));
	/*enter IQK mode*/
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);
#if 1
	/*backup path & GNT value */
	original_path = odm_get_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, MASKDWORD);  /*save 0x70*/
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
	ODM_delay_ms(1);
	original_gnt = odm_get_bb_reg(p_dm, REG_LTECOEX_READ_DATA, MASKDWORD);  /*save 0x38*/
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]OriginalGNT = 0x%x\n", original_gnt));

	/*set GNT_WL=1/GNT_BT=1  and path owner to WiFi for pause BT traffic*/
	odm_set_bb_reg(p_dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, 0x0000ff00);
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc0020038);	/*0x38[15:8] = 0x77*/
	odm_set_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, BIT(26), 0x1);
#endif
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0054);
	ODM_delay_ms(1);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]GNT_BT @S0 TXIQK = 0x%x\n", odm_get_bb_reg(p_dm, REG_LTECOEX_READ_DATA, MASKDWORD)));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x948 @S0 TXIQK = 0x%x\n", odm_get_bb_reg(p_dm, 0x948, MASKDWORD)));

	/*One shot, path S1 LOK & IQK*/
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/*delay x ms*/
	/*ODM_delay_ms(IQK_DELAY_TIME_8723D);*/

	ktime = 0;
	while ((!odm_get_bb_reg(p_dm, 0xeac, BIT(26))) && ktime < 10) {
		ODM_delay_ms(1);
		ktime++;
	}
#if 1
	/*Restore GNT_WL/GNT_BT  and path owner*/
	odm_set_bb_reg(p_dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, original_gnt);
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc00f0038);
	odm_set_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, original_path);
#endif
	/*reload RF path*/
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, path_sel_bb);
	/*leave IQK mode*/
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	/*PA/PAD controlled by 0x0*/
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xde, 0x800, 0x0);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x2, BIT(0), 0x0);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x1, BIT(0), 0x0);
	/* Check failed*/
	reg_eac_s0 = odm_get_bb_reg(p_dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_e94_s0 = odm_get_bb_reg(p_dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD);
	reg_e9c_s0 = odm_get_bb_reg(p_dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac_s0 = 0x%x\n", reg_eac_s0));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe94_s0 = 0x%x, 0xe9c_s0 = 0x%x\n", reg_e94_s0, reg_e9c_s0));
	/*monitor image power before & after IQK*/
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe90_s0(before IQK)= 0x%x, 0xe98_s0(afer IQK) = 0x%x\n",
		odm_get_bb_reg(p_dm, 0xe90, MASKDWORD), odm_get_bb_reg(p_dm, 0xe98, MASKDWORD)));
	if (!(reg_eac_s0 & BIT(28)) &&
	    (((reg_e94_s0 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c_s0 & 0x03FF0000) >> 16) != 0x42))

		result |= 0x01;
	else
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("S0 TXIQK FAIL\n"));

	return result;
}


u8
phy_path_s0_rx_iqk_8723d(
	struct PHY_DM_STRUCT		*p_dm,
	boolean	config_path_s0
)
{
	u32 reg_e94, reg_e9c, reg_ea4, reg_eac, reg_e94_s0, reg_e9c_s0, reg_ea4_s0, reg_eac_s0, tmp, u4tmp, path_sel_bb;
	u8 result = 0x00, ktime;
	u32 original_path, original_gnt;

	path_sel_bb = odm_get_bb_reg(p_dm, 0x948, MASKDWORD);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path S0 RxIQK Step1!!\n"));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x67 @S0 RXIQK1 = 0x%x\n", odm_get_mac_reg(p_dm, 0x64, MASKBYTE3)));
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, 0x99000280);
	/*ODM_RT_TRACE(p_dm,ODM_COMP_INIT, ODM_DBG_LOUD, ("[IQK]0x1e6@S0 RXIQK1 = 0x%x\n", platform_efio_read_1byte(p_adapter, 0x1e6)));*/

	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	odm_set_bb_reg(p_dm, REG_TX_IQK, MASKDWORD, 0x01007c00);
	odm_set_bb_reg(p_dm, REG_RX_IQK, MASKDWORD, 0x01004800);

	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_B, MASKDWORD, 0x38008c1c);

	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_A, MASKDWORD, 0x82160000);
	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_A, MASKDWORD, 0x28160000);

	odm_set_bb_reg(p_dm, REG_IQK_AGC_RSP, MASKDWORD, 0x0046a911);

	odm_set_rf_reg(p_dm, RF_PATH_A, 0xee, RFREGOFFSETMASK, 0x80000);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x33, RFREGOFFSETMASK, 0x00006);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x3e, RFREGOFFSETMASK, 0x0005f);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x3f, RFREGOFFSETMASK, 0xa7ffb);

	odm_set_rf_reg(p_dm, RF_PATH_A, 0xde, 0x800, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x66, 0x600, 0x0);

#if 1
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x1, RFREGOFFSETMASK, 0xe6d);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x2, RFREGOFFSETMASK, 0x66d);
#endif

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RF0x1 @S0 RXIQK1 = 0x%x\n", odm_get_rf_reg(p_dm, RF_PATH_A, 0x1, RFREGOFFSETMASK)));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RF0x2 @S0 RXIQK1 = 0x%x\n", odm_get_rf_reg(p_dm, RF_PATH_A, 0x2, RFREGOFFSETMASK)));

	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);

#if 1
	/*backup path & GNT value */
	original_path = odm_get_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, MASKDWORD);  /*save 0x70*/
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
	ODM_delay_ms(1);
	original_gnt = odm_get_bb_reg(p_dm, REG_LTECOEX_READ_DATA, MASKDWORD);  /*save 0x38*/
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]OriginalGNT = 0x%x\n", original_gnt));

	/*set GNT_WL=1/GNT_BT=1  and path owner to WiFi for pause BT traffic*/
	odm_set_bb_reg(p_dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, 0x0000ff00);
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc0020038);	/*0x38[15:8] = 0x77*/
	odm_set_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, BIT(26), 0x1);
#endif
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0054);
	ODM_delay_ms(1);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]GNT_BT @S0 RXIQK1 = 0x%x\n", odm_get_bb_reg(p_dm, REG_LTECOEX_READ_DATA, MASKDWORD)));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x948 @S0 RXIQK1 = 0x%x\n", odm_get_bb_reg(p_dm, 0x948, MASKDWORD)));

	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/*ODM_delay_ms(IQK_DELAY_TIME_8723D);*/
	ktime = 0;
	while ((!odm_get_bb_reg(p_dm, 0xeac, BIT(26))) && ktime < 10) {
		ODM_delay_ms(1);
		ktime++;
	}
	reg_eac_s0 = odm_get_bb_reg(p_dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_e94_s0 = odm_get_bb_reg(p_dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD);
	reg_e9c_s0 = odm_get_bb_reg(p_dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac_s0 = 0x%x\n", reg_eac_s0));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe94_s0 = 0x%x, 0xe9c_s0 = 0x%x\n", reg_e94_s0, reg_e9c_s0));
	/*monitor image power before & after IQK*/
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe90_s0(before IQK)= 0x%x, 0xe98_s0(afer IQK) = 0x%x\n",
		odm_get_bb_reg(p_dm, 0xe90, MASKDWORD), odm_get_bb_reg(p_dm, 0xe98, MASKDWORD)));

	tmp = (reg_e9c_s0 & 0x03FF0000) >> 16;
	if ((tmp & 0x200) > 0)
		tmp = 0x400 - tmp;

	if (!(reg_eac_s0 & BIT(28)) &&
	    (((reg_e94_s0 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c_s0 & 0x03FF0000) >> 16) != 0x42))

		result |= 0x01;
	else {
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("S0 RXIQK STEP1 FAIL\n"));
#if 1
		/*Restore GNT_WL/GNT_BT  and path owner*/
		odm_set_bb_reg(p_dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, original_gnt);
		odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc00f0038);
		odm_set_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, original_path);
#endif
		/*reload RF path*/
		odm_set_bb_reg(p_dm, 0x948, MASKDWORD, path_sel_bb);
		odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
		odm_set_rf_reg(p_dm, RF_PATH_A, 0xde, 0x800, 0x0);
		odm_set_rf_reg(p_dm, RF_PATH_A, 0x2, BIT(0), 0x0);
		odm_set_rf_reg(p_dm, RF_PATH_A, 0x1, BIT(0), 0x0);
		return result;
	}

	u4tmp = 0x80007C00 | (reg_e94_s0 & 0x3FF0000)  | ((reg_e9c_s0 & 0x3FF0000) >> 16);
	odm_set_bb_reg(p_dm, REG_TX_IQK, MASKDWORD, u4tmp);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xe40_s0 = 0x%x u4tmp = 0x%x\n", odm_get_bb_reg(p_dm, REG_TX_IQK, MASKDWORD), u4tmp));

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]path S0 RXIQK STEP2!!\n\n"));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x67 @S0 RXIQK2 = 0x%x\n", odm_get_mac_reg(p_dm, 0x64, MASKBYTE3)));
	/*ODM_RT_TRACE(p_dm,ODM_COMP_INIT, ODM_DBG_LOUD, ("[IQK]0x1e6@S0 RXIQK2 = 0x%x\n", platform_efio_read_1byte(p_adapter, 0x1e6)));*/
	odm_set_bb_reg(p_dm, REG_RX_IQK, MASKDWORD, 0x01004800);

	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x18008c1c);
	odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_B, MASKDWORD, 0x38008c1c);
	odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_B, MASKDWORD, 0x38008c1c);

	odm_set_bb_reg(p_dm, REG_TX_IQK_PI_A, MASKDWORD, 0x82170000);
	odm_set_bb_reg(p_dm, REG_RX_IQK_PI_A, MASKDWORD, 0x28171400);

	odm_set_bb_reg(p_dm, REG_IQK_AGC_RSP, MASKDWORD, 0x0046a8d1);

	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0xee, 0x80000, 0x1);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x33, RFREGOFFSETMASK, 0x00007);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x3e, RFREGOFFSETMASK, 0x0005f);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x3f, RFREGOFFSETMASK, 0xb3fdb);

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RF0x1 @S0 RXIQK2 = 0x%x\n", odm_get_rf_reg(p_dm, RF_PATH_A, 0x1, RFREGOFFSETMASK)));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("RF0x2 @S0 RXIQK2 = 0x%x\n", odm_get_rf_reg(p_dm, RF_PATH_A, 0x2, RFREGOFFSETMASK)));
	/*enter IQK mode*/
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);
#if 1
	/*backup path & GNT value */
	original_path = odm_get_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, MASKDWORD);  /*save 0x70*/
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
	ODM_delay_ms(1);
	original_gnt = odm_get_bb_reg(p_dm, REG_LTECOEX_READ_DATA, MASKDWORD);  /*save 0x38*/
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]OriginalGNT = 0x%x\n", original_gnt));

	/*set GNT_WL=1/GNT_BT=1  and path owner to WiFi for pause BT traffic*/
	odm_set_bb_reg(p_dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, 0x0000ff00);
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc0020038);	/*0x38[15:8] = 0x77*/
	odm_set_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, BIT(26), 0x1);
#endif
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0054);
	ODM_delay_ms(1);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]GNT_BT @S0 RXIQK2 = 0x%x\n", odm_get_bb_reg(p_dm, REG_LTECOEX_READ_DATA, MASKDWORD)));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0x948 @S0 RXIQK2 = 0x%x\n", odm_get_bb_reg(p_dm, 0x948, MASKDWORD)));

	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	odm_set_bb_reg(p_dm, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/*ODM_delay_ms(IQK_DELAY_TIME_8723D);*/
	ktime = 0;
	while ((!odm_get_bb_reg(p_dm, 0xeac, BIT(26))) && ktime < 10) {
		ODM_delay_ms(1);
		ktime++;
	}
#if 1
	/*Restore GNT_WL/GNT_BT  and path owner*/
	odm_set_bb_reg(p_dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, original_gnt);
	odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc00f0038);
	odm_set_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, original_path);
#endif
	/*reload RF path*/
	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, path_sel_bb);

	/*leave IQK mode*/
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	odm_set_rf_reg(p_dm, RF_PATH_A, 0xde, 0x800, 0x0);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x2, BIT(0), 0x0);
	odm_set_rf_reg(p_dm, RF_PATH_A, 0x1, BIT(0), 0x0);

	reg_eac_s0 = odm_get_bb_reg(p_dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	reg_ea4_s0 = odm_get_bb_reg(p_dm, REG_RX_POWER_BEFORE_IQK_A_2, MASKDWORD);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xeac_s0 = 0x%x\n", reg_eac_s0));
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xea4_s0 = 0x%x, 0xeac_s0 = 0x%x\n", reg_ea4_s0, reg_eac_s0));

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xea0_s0(before IQK)= 0x%x, 0xea8_s0(afer IQK) = 0x%x\n",
		odm_get_bb_reg(p_dm, 0xea0, MASKDWORD), odm_get_bb_reg(p_dm, 0xea8, MASKDWORD)));

	tmp = (reg_eac_s0 & 0x03FF0000) >> 16;
	if ((tmp & 0x200) > 0)
		tmp = 0x400 - tmp;

	if (!(reg_eac_s0 & BIT(27)) &&		/*if Tx is OK, check whether Rx is OK*/
	    (((reg_ea4_s0 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_eac_s0 & 0x03FF0000) >> 16) != 0x36) &&
	    (((reg_ea4_s0 & 0x03FF0000) >> 16) < 0x11a) &&
	    (((reg_ea4_s0 & 0x03FF0000) >> 16) > 0xe6) &&
	    (tmp < 0x1a))
		result |= 0x02;
	else
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("S0 RXIQK STEP2 FAIL\n"));
	return result;
}


void
_phy_path_s1_fill_iqk_matrix_8723d(
	struct PHY_DM_STRUCT		*p_dm,
	boolean	is_iqk_ok,
	s32		result[][8],
	u8		final_candidate,
	boolean	is_tx_only
)
{
	u32	oldval_1, X, TX1_A, reg;
	s32	Y, TX1_C;
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path S1 IQ Calibration %s !\n", (is_iqk_ok) ? "Success" : "Failed"));

	if (final_candidate == 0xFF)
		return;

	else if (is_iqk_ok) {
		oldval_1 = (odm_get_bb_reg(p_dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD) >> 22) & 0x3FF;

		X = result[final_candidate][0];

		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;
		TX1_A = (X * oldval_1) >> 8;
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("X = 0x%x, TX1_A = 0x%x, oldval_1 0x%x\n", X, TX1_A, oldval_1));
		odm_set_bb_reg(p_dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, 0x3FF, TX1_A);

		odm_set_bb_reg(p_dm, REG_OFDM_0_ECCA_THRESHOLD, BIT(31), ((X * oldval_1 >> 7) & 0x1));

		Y = result[final_candidate][1];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;

		TX1_C = (Y * oldval_1) >> 8;
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Y = 0x%x, TX1_C = 0x%x\n", Y, TX1_C));
		odm_set_bb_reg(p_dm, REG_OFDM_0_XC_TX_AFE, 0xF0000000, ((TX1_C & 0x3C0) >> 6));
		odm_set_bb_reg(p_dm, REG_OFDM_0_XA_TX_IQ_IMBALANCE, 0x003F0000, (TX1_C & 0x3F));

		odm_set_bb_reg(p_dm, REG_OFDM_0_ECCA_THRESHOLD, BIT(29), ((Y * oldval_1 >> 7) & 0x1));

		if (is_tx_only) {
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_path_s1_fill_iqk_matrix_8723d only Tx OK\n"));
			return;
		}
		reg = result[final_candidate][2];
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
		if (RTL_ABS(reg, 0x100) >= 16)
			reg = 0x100;
#endif
		odm_set_bb_reg(p_dm, REG_OFDM_0_XA_RX_IQ_IMBALANCE, 0x3FF, reg);

		reg = result[final_candidate][3] & 0x3F;
		odm_set_bb_reg(p_dm, REG_OFDM_0_XA_RX_IQ_IMBALANCE, 0xFC00, reg);

		reg = (result[final_candidate][3] >> 6) & 0xF;
		odm_set_bb_reg(p_dm, REG_OFDM_0_RX_IQ_EXT_ANTA, 0xF0000000, reg);
		/*
		10 os 7201 10
		10 id ea4 [25:16] p
		10 os 7202 10
		10 od c14 VarFromTmp [9:0] p

		10 os 7201 11
		10 id eac [25:22] p
		10 os 7202 11
		10 od ca0 VarFromTmp [31:28] p

		10 os 7201 12
		10 id eac [21:16] p
		10 os 7202 12
		10 od c14 VarFromTmp [15:10] p
		*/
	}
}

void
_phy_path_s0_fill_iqk_matrix_8723d(
	struct PHY_DM_STRUCT		*p_dm,
	boolean	is_iqk_ok,
	s32		result[][8],
	u8		final_candidate,
	boolean		is_tx_only
)
{
	u32	oldval_0, X, TX0_A, reg;
	s32	Y, TX0_C;
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path S0 IQ Calibration %s !\n", (is_iqk_ok) ? "Success" : "Failed"));

	if (final_candidate == 0xFF)
		return;

	else if (is_iqk_ok) {
		oldval_0 = (odm_get_bb_reg(p_dm, 0xcd4, MASKDWORD) >> 13) & 0x3FF;

		X = result[final_candidate][4];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;
		TX0_A = (X * oldval_0) >> 8;
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("X = 0x%x, TX0_A = 0x%x, oldval_0 0x%x\n", X, TX0_A, oldval_0));
		odm_set_bb_reg(p_dm, 0xcd0, 0x7FE, TX0_A);

		odm_set_bb_reg(p_dm, 0xcd0, BIT(0), ((X * oldval_0 >> 7) & 0x1));

		Y = result[final_candidate][5];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;

		TX0_C = (Y * oldval_0) >> 8;
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Y = 0x%x, TX0_C = 0x%x\n", Y, TX0_C));
		odm_set_bb_reg(p_dm, 0xcd4, 0x7FE, (TX0_C & 0x3FF));

		odm_set_bb_reg(p_dm, 0xcd4, BIT(0), ((Y * oldval_0 >> 7) & 0x1));

		if (is_tx_only)
			return;

		reg = result[final_candidate][6];
		odm_set_bb_reg(p_dm, 0xcd8, 0x3FF, reg);

		reg = result[final_candidate][7];
		odm_set_bb_reg(p_dm, 0xcd8, 0x003FF000, reg);
		/*
		10 os 7201 10
		10 id ea4 [25:16] p
		10 os 7202 10
		10 od cd8 VarFromTmp [9:0] p

		10 os 7201 11
		10 id eac [25:16] p
		10 os 7202 11
		10 od cd8 VarFromTmp [21:12] p
				rege94_s1 = result[i][0];
				rege9c_s1 = result[i][1];
				regea4_s1 = result[i][2];
				regeac_s1 = result[i][3];
				rege94_s0 = result[i][4];
				rege9c_s0 = result[i][5];
				regea4_s0 = result[i][6];
				regeac_s0 = result[i][7];
		*/
	}
}

void
_phy_save_adda_registers_8723d(
	struct PHY_DM_STRUCT		*p_dm,
	u32		*adda_reg,
	u32		*adda_backup,
	u32		register_num
)
{
	u32	i;

	for (i = 0 ; i < register_num ; i++)
		adda_backup[i] = odm_get_bb_reg(p_dm, adda_reg[i], MASKDWORD);
}


void
_phy_save_mac_registers_8723d(
	struct PHY_DM_STRUCT		*p_dm,
	u32		*mac_reg,
	u32		*mac_backup
)
{
	u32	i;

	for (i = 0 ; i < (IQK_MAC_REG_NUM - 1); i++)
		mac_backup[i] = odm_read_1byte(p_dm, mac_reg[i]);
	mac_backup[i] = odm_read_4byte(p_dm, mac_reg[i]);
}


void
_phy_reload_adda_registers_8723d(
	struct PHY_DM_STRUCT		*p_dm,
	u32		*adda_reg,
	u32		*adda_backup,
	u32		regiester_num
)
{
	u32	i;

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Reload ADDA power saving parameters !\n"));
	for (i = 0 ; i < regiester_num; i++)
		odm_set_bb_reg(p_dm, adda_reg[i], MASKDWORD, adda_backup[i]);
}

void
_phy_reload_mac_registers_8723d(
	struct PHY_DM_STRUCT		*p_dm,
	u32		*mac_reg,
	u32		*mac_backup
)
{
	u32	i;

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("Reload MAC parameters !\n"));
	for (i = 0 ; i < (IQK_MAC_REG_NUM - 1); i++)
		odm_write_1byte(p_dm, mac_reg[i], (u8)mac_backup[i]);
	odm_write_4byte(p_dm, mac_reg[i], mac_backup[i]);
}


void
_phy_path_adda_on_8723d(
	struct PHY_DM_STRUCT		*p_dm,
	u32		*adda_reg,
	boolean		is_path_a_on,
	boolean		is2T
)
{
	u32	path_on;
	u32	i;

	path_on = is_path_a_on ? 0x03c00016 : 0x03c00016;

	if (false == is2T) {
		path_on = 0x03c00016;
		odm_set_bb_reg(p_dm, adda_reg[0], MASKDWORD, 0x03c00016);
	} else
		odm_set_bb_reg(p_dm, adda_reg[0], MASKDWORD, path_on);

	for (i = 1 ; i < IQK_ADDA_REG_NUM ; i++)
		odm_set_bb_reg(p_dm, adda_reg[i], MASKDWORD, path_on);

}

void
_phy_mac_setting_calibration_8723d(
	struct PHY_DM_STRUCT		*p_dm,
	u32		*mac_reg,
	u32		*mac_backup
)
{
	/*
		odm_write_1byte(p_dm, mac_reg[i], 0x3F);

		for(i = 1 ; i < (IQK_MAC_REG_NUM - 1); i++){
			odm_write_1byte(p_dm, mac_reg[i], (u8)(mac_backup[i]&(~BIT(3))));
		}
		odm_write_1byte(p_dm, mac_reg[i], (u8)(mac_backup[i]&(~BIT(5))));
	*/

	/*odm_set_bb_reg(p_dm, 0x522, MASKBYTE0, 0x7f);*/
	/*odm_set_bb_reg(p_dm, 0x550, MASKBYTE0, 0x15);*/
	/*odm_set_bb_reg(p_dm, 0x551, MASKBYTE0, 0x00);*/
	odm_set_bb_reg(p_dm, 0x520, 0x00ff0000, 0xff);
}

void
_phy_path_a_stand_by_8723d(
	struct PHY_DM_STRUCT		*p_dm
)
{
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path-S1 standby mode!\n"));
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	/*	odm_set_bb_reg(p_dm, 0x840, MASKDWORD, 0x00010000);*/
	odm_set_rf_reg(p_dm, (enum rf_path)0x0, 0x0, RFREGOFFSETMASK, 0x10000);
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);
}

void
_phy_path_b_stand_by_8723d(
	struct PHY_DM_STRUCT		*p_dm
)
{
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path-S0 standby mode!\n"));
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);
	odm_set_rf_reg(p_dm, (enum rf_path)0x1, 0x0, RFREGOFFSETMASK, 0x10000);
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);
}


void
_phy_pi_mode_switch_8723d(
	struct PHY_DM_STRUCT		*p_dm,
	boolean		pi_mode
)
{
	u32	mode;

	mode = pi_mode ? 0x01000100 : 0x01000000;
	odm_set_bb_reg(p_dm, REG_FPGA0_XA_HSSI_PARAMETER1, MASKDWORD, mode);
	odm_set_bb_reg(p_dm, REG_FPGA0_XB_HSSI_PARAMETER1, MASKDWORD, mode);
}

boolean
phy_simularity_compare_8723d(
	struct PHY_DM_STRUCT		*p_dm,
	s32		result[][8],
	u8		 c1,
	u8		 c2
)
{
	u32		i, j, diff, simularity_bit_map, bound = 0;
	u8		final_candidate[2] = {0xFF, 0xFF};
	boolean		is_result = true;
	/*#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)*/
	/*	bool		is2T = IS_92C_SERIAL( p_hal_data->version_id);*/
	/*#else*/
	boolean		is2T = true;
	/*#endif*/

	s32 tmp1 = 0, tmp2 = 0;

	if (is2T)
		bound = 8;
	else
		bound = 4;

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("===> IQK:phy_simularity_compare_8723d c1 %d c2 %d!!!\n", c1, c2));


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
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK:differnece overflow %d index %d compare1 0x%x compare2 0x%x!!!\n",  diff, i, result[c1][i], result[c2][i]));

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

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK:phy_simularity_compare_8723d simularity_bit_map   %x !!!\n", simularity_bit_map));

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

		if (!(simularity_bit_map & 0x03)) {
			for (i = 0; i < 2; i++)
				result[3][i] = result[c1][i];
		}

		if (!(simularity_bit_map & 0x0c)) {
			for (i = 2; i < 4; i++)
				result[3][i] = result[c1][i];
		}

		if (!(simularity_bit_map & 0x30)) {
			for (i = 4; i < 6; i++)
				result[3][i] = result[c1][i];

		}

		if (!(simularity_bit_map & 0xc0)) {
			for (i = 6; i < 8; i++)
				result[3][i] = result[c1][i];
		}

		return false;
	}



}


void
_phy_check_coex_status_8723d(
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
_phy_iq_calibrate_8723d(
	struct PHY_DM_STRUCT		*p_dm,
	s32		result[][8],
	u8		t,
	boolean		is2T
)
{
	u32			i;
	u8			path_s1_ok = 0x0, path_s0_ok = 0x0;
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


	u32	IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
		REG_OFDM_0_TRX_PATH_ENABLE,		REG_OFDM_0_TR_MUX_PAR,
		REG_FPGA0_XCD_RF_INTERFACE_SW,	REG_CONFIG_ANT_A,	REG_CONFIG_ANT_B,
		REG_FPGA0_XAB_RF_INTERFACE_SW,	REG_FPGA0_XA_RF_INTERFACE_OE,
		REG_FPGA0_XB_RF_INTERFACE_OE, REG_CCK_0_AFE_SETTING
	};
	u32	cnt_iqk_fail = 0;
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
	if (*(p_dm->p_mp_mode))
		retry_count = 9;
	else
		retry_count = 2;
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
#ifdef MP_TEST
	if (*(p_dm->p_mp_mode))
		retry_count = 9;
#endif
#endif

	if (t == 0) {
		_phy_save_adda_registers_8723d(p_dm, ADDA_REG, p_dm->rf_calibrate_info.ADDA_backup, IQK_ADDA_REG_NUM);
		_phy_save_mac_registers_8723d(p_dm, IQK_MAC_REG, p_dm->rf_calibrate_info.IQK_MAC_backup);
		_phy_save_adda_registers_8723d(p_dm, IQK_BB_REG_92C, p_dm->rf_calibrate_info.IQK_BB_backup, IQK_BB_REG_NUM);
	}
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQ Calibration for 1T1R_S0/S1 for %d times\n", t));

	_phy_path_adda_on_8723d(p_dm, ADDA_REG, true, is2T);
#if 0
	if (t == 0)
		p_dm->rf_calibrate_info.is_rf_pi_enable = (u8)odm_get_bb_reg(p_dm, REG_FPGA0_XA_HSSI_PARAMETER1, BIT(8));

	if (!p_dm->rf_calibrate_info.is_rf_pi_enable) {
		/*  Switch BB to PI mode to do IQ Calibration. */
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		_phy_pi_mode_switch_8723d(p_adapter, true);
#else
		_phy_pi_mode_switch_8723d(p_dm, true);
#endif
	}
#endif
	_phy_mac_setting_calibration_8723d(p_dm, IQK_MAC_REG, p_dm->rf_calibrate_info.IQK_MAC_backup);
	/*BB setting*/
	/*odm_set_bb_reg(p_dm, REG_FPGA0_RFMOD, BIT24, 0x00);*/
	odm_set_bb_reg(p_dm, REG_CCK_0_AFE_SETTING, 0x0f000000, 0xf);
	odm_set_bb_reg(p_dm, REG_OFDM_0_TRX_PATH_ENABLE, MASKDWORD, 0x03a05611);
	odm_set_bb_reg(p_dm, REG_OFDM_0_TR_MUX_PAR, MASKDWORD, 0x000800e4);
	odm_set_bb_reg(p_dm, REG_FPGA0_XCD_RF_INTERFACE_SW, MASKDWORD, 0x25204200);

	/*IQ calibration setting*/
	/*ODM_RT_TRACE(p_dm,ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK setting!\n"));	*/
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);
	odm_set_bb_reg(p_dm, REG_TX_IQK, MASKDWORD, 0x01007c00);
	odm_set_bb_reg(p_dm, REG_RX_IQK, MASKDWORD, 0x01004800);

	if (is2T) {
		_phy_path_b_stand_by_8723d(p_dm);
		_phy_path_adda_on_8723d(p_dm, ADDA_REG, false, is2T);
	}

#if 1
	for (i = 0 ; i < retry_count ; i++) {
		path_s1_ok = phy_path_s1_iqk_8723d(p_dm, is2T);
		if (path_s1_ok == 0x01) {
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path S1 Tx IQK Success!!\n"));
			result[t][0] = (odm_get_bb_reg(p_dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
			result[t][1] = (odm_get_bb_reg(p_dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
			break;
		} else {
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path S1 Tx IQK Fail!!\n"));
			result[t][0] = 0x100;
			result[t][1] = 0x0;
			cnt_iqk_fail++;
		}
#if 0
		else if (i == (retry_count - 1) && path_s1_ok == 0x01) {
			RT_DISP(FINIT, INIT_IQK, ("path S1 IQK Only  Tx Success!!\n"));

			result[t][0] = (odm_get_bb_reg(p_dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
			result[t][1] = (odm_get_bb_reg(p_dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
		}
#endif
	}
#endif
#if 1
	for (i = 0 ; i < retry_count ; i++) {
		path_s1_ok = phy_path_s1_rx_iqk_8723d(p_dm, is2T);
		if (path_s1_ok == 0x03) {
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path S1 Rx IQK Success!!\n"));
			result[t][2] = (odm_get_bb_reg(p_dm, REG_RX_POWER_BEFORE_IQK_A_2, MASKDWORD) & 0x3FF0000) >> 16;
			result[t][3] = (odm_get_bb_reg(p_dm, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD) & 0x3FF0000) >> 16;
			break;
		} else {
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path S1 Rx IQK Fail!!\n"));
			result[t][2] = 0x100;
			result[t][3] = 0x0;
			cnt_iqk_fail++;
		}
	}
	if (0x00 == path_s1_ok)
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path S1 IQK failed!!\n"));
#endif
	if (is2T) {
		_phy_path_a_stand_by_8723d(p_dm);
		_phy_path_adda_on_8723d(p_dm, ADDA_REG, false, is2T);

		odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x808000);
		odm_set_bb_reg(p_dm, REG_TX_IQK, MASKDWORD, 0x01007c00);
		odm_set_bb_reg(p_dm, REG_RX_IQK, MASKDWORD, 0x01004800);


#if 1
		for (i = 0 ; i < retry_count ; i++) {
			path_s0_ok = phy_path_s0_iqk_8723d(p_dm);
			if (path_s0_ok == 0x01) {
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path S0 Tx IQK Success!!\n"));
				result[t][4] = (odm_get_bb_reg(p_dm, 0xe94, MASKDWORD) & 0x3FF0000) >> 16;
				result[t][5] = (odm_get_bb_reg(p_dm, 0xe9c, MASKDWORD) & 0x3FF0000) >> 16;
				break;
			} else {
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path S0 Tx IQK Fail!!\n"));
				result[t][4] = 0x100;
				result[t][5] = 0x0;
				cnt_iqk_fail++;
			}
#if 0
			else if (i == (retry_count - 1) && path_s1_ok == 0x01) {
				RT_DISP(FINIT, INIT_IQK, ("path S0 IQK Only  Tx Success!!\n"));

				result[t][0] = (odm_get_bb_reg(p_dm, REG_TX_POWER_BEFORE_IQK_B, MASKDWORD) & 0x3FF0000) >> 16;
				result[t][1] = (odm_get_bb_reg(p_dm, REG_TX_POWER_AFTER_IQK_B, MASKDWORD) & 0x3FF0000) >> 16;
			}
#endif
		}
#endif

#if 1
		for (i = 0 ; i < retry_count ; i++) {
			path_s0_ok = phy_path_s0_rx_iqk_8723d(p_dm, is2T);
			if (path_s0_ok == 0x03) {
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path S0 Rx IQK Success!!\n"));
				/*				result[t][0] = (odm_get_bb_reg(p_dm, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD)&0x3FF0000)>>16;*/
				/*				result[t][1] = (odm_get_bb_reg(p_dm, REG_TX_POWER_AFTER_IQK_A, MASKDWORD)&0x3FF0000)>>16;*/
				result[t][6] = (odm_get_bb_reg(p_dm, 0xea4, MASKDWORD) & 0x3FF0000) >> 16;
				result[t][7] = (odm_get_bb_reg(p_dm, 0xeac, MASKDWORD) & 0x3FF0000) >> 16;
				break;
			} else {
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path S0 Rx IQK Fail!!\n"));
				result[t][6] = 0x100;
				result[t][7] = 0x0;
				cnt_iqk_fail++;
			}
		}

		if (0x00 == path_s0_ok)
			ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("path S0 IQK failed!!\n"));
#endif
	}
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK:Back to BB mode, load original value!\n"));
	odm_set_bb_reg(p_dm, REG_FPGA0_IQK, 0xffffff00, 0x000000);

	if (t != 0) {
		_phy_reload_adda_registers_8723d(p_dm, ADDA_REG, p_dm->rf_calibrate_info.ADDA_backup, IQK_ADDA_REG_NUM);
		/* Reload MAC parameters*/
		_phy_reload_mac_registers_8723d(p_dm, IQK_MAC_REG, p_dm->rf_calibrate_info.IQK_MAC_backup);
		_phy_reload_adda_registers_8723d(p_dm, IQK_BB_REG_92C, p_dm->rf_calibrate_info.IQK_BB_backup, IQK_BB_REG_NUM);

		odm_set_bb_reg(p_dm, 0xc50, MASKBYTE0, 0x50);
		odm_set_bb_reg(p_dm, 0xc50, MASKBYTE0, tmp0xc50);
		if (is2T) {
			odm_set_bb_reg(p_dm, 0xc58, MASKBYTE0, 0x50);
			odm_set_bb_reg(p_dm, 0xc58, MASKBYTE0, tmp0xc58);
		}
		odm_set_bb_reg(p_dm, REG_TX_IQK_TONE_A, MASKDWORD, 0x01008c00);
		odm_set_bb_reg(p_dm, REG_RX_IQK_TONE_A, MASKDWORD, 0x01008c00);
	}
	p_dm->n_iqk_cnt++;
	if (cnt_iqk_fail == 0)
		p_dm->n_iqk_ok_cnt++;
	else
		p_dm->n_iqk_fail_cnt = p_dm->n_iqk_fail_cnt + cnt_iqk_fail;
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("_phy_iq_calibrate_8723d() <==\n"));
}


void
_phy_lc_calibrate_8723d(
	struct PHY_DM_STRUCT		*p_dm,
	boolean	is2T
)
{
	u8	tmp_reg;
	u32	rf_bmode = 0, lc_cal, cnt;

	tmp_reg = odm_read_1byte(p_dm, 0xd03);
	if ((tmp_reg & 0x70) != 0)
		odm_write_1byte(p_dm, 0xd03, tmp_reg & 0x8F);
	else
		odm_write_1byte(p_dm, REG_TXPAUSE, 0xFF);
	/*backup RF0x18*/
	lc_cal = odm_get_rf_reg(p_dm, RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK);
	/*Start LCK*/
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK, lc_cal | 0x08000);
	for (cnt = 0; cnt < 100; cnt++) {
		if (odm_get_rf_reg(p_dm, RF_PATH_A, RF_CHNLBW, 0x8000) != 0x1)
			break;
		ODM_delay_ms(10);
	}
	/* Recover channel number*/
	odm_set_rf_reg(p_dm, RF_PATH_A, RF_CHNLBW, RFREGOFFSETMASK, lc_cal);
	/*Restore original situation*/
	if ((tmp_reg & 0x70) != 0)
		odm_write_1byte(p_dm, 0xd03, tmp_reg);
	else
		odm_write_1byte(p_dm, REG_TXPAUSE, 0x00);
}


void
phy_iq_calibrate_8723d(
	void		*p_dm_void,
	boolean	is_recovery
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u16			count = 0;
	s32			result[4][8];
	u8			i, final_candidate, indexforchannel;
	boolean			is_path_s1_ok, is_path_s0_ok;
	s32			rege94_s1, rege9c_s1, regea4_s1, regeac_s1, rege94_s0, rege9c_s0, regea4_s0, regeac_s0, reg_tmp = 0;
	s32			regc80, regc94, regc14, regca0, regcd0, regcd4, regcd8;
	boolean			is12simular, is13simular, is23simular;
	u32			IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
		REG_OFDM_0_XA_RX_IQ_IMBALANCE,	REG_OFDM_0_XB_RX_IQ_IMBALANCE,
		REG_OFDM_0_ECCA_THRESHOLD,	REG_OFDM_0_AGC_RSSI_TABLE,
		REG_OFDM_0_XA_TX_IQ_IMBALANCE,	REG_OFDM_0_XB_TX_IQ_IMBALANCE,
		REG_OFDM_0_XC_TX_AFE,			REG_OFDM_0_XD_TX_AFE,
		REG_OFDM_0_RX_IQ_EXT_ANTA
	};
	u32			path_sel_bb_phy_iqk;
	u32			original_path, original_gnt, ori_path_ctrl;
	u32			iqk_fail_b, iqk_fail_a;

#if 1
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("================ IQK Start ===================\n"));

	iqk_fail_b = p_dm->n_iqk_fail_cnt;

	ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_LOUD, ("=====>phy_iq_calibrate_8723d\n"));

	path_sel_bb_phy_iqk = odm_get_bb_reg(p_dm, 0x948, MASKDWORD);

#if (DM_ODM_SUPPORT_TYPE & (ODM_CE | ODM_AP))
	if (is_recovery)
#else
	if (is_recovery && (!p_dm->is_in_hct_test))
#endif
	{
		ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_LOUD, ("phy_iq_calibrate_8723d: Return due to is_recovery!\n"));
		_phy_reload_adda_registers_8723d(p_dm, IQK_BB_REG_92C, p_dm->rf_calibrate_info.IQK_BB_backup_recover, 9);
		return;
	}
	/*Check & wait if BT is doing IQK*/
	if (*(p_dm->p_mp_mode) == false)
		_phy_check_coex_status_8723d(p_dm, true);

	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK:Start!!!\n"));
	odm_acquire_spin_lock(p_dm, RT_IQK_SPINLOCK);
	p_dm->rf_calibrate_info.is_iqk_in_progress = true;
	odm_release_spin_lock(p_dm, RT_IQK_SPINLOCK);

	for (i = 0; i < 8; i++) {
		result[0][i] = 0;
		result[1][i] = 0;
		result[2][i] = 0;
		result[3][i] = 0;
	}

	final_candidate = 0xff;
	is_path_s1_ok = false;
	is_path_s0_ok = false;
	is12simular = false;
	is23simular = false;
	is13simular = false;

	for (i = 0; i < 3; i++) {
#if 1
		/*set path control to WL*/
		ori_path_ctrl = odm_get_mac_reg(p_dm, 0x64, MASKBYTE3);  /*save 0x67*/
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]original 0x67 = 0x%x\n", ori_path_ctrl));
		odm_set_mac_reg(p_dm, 0x64, BIT(31), 0x1);
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]set 0x67 = 0x%x\n", odm_get_mac_reg(p_dm, 0x64, MASKBYTE3)));
		/*backup path & GNT value */
		original_path = odm_get_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, MASKDWORD);  /*save 0x70*/
		odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0x800f0038);
		ODM_delay_ms(1);
		original_gnt = odm_get_bb_reg(p_dm, REG_LTECOEX_READ_DATA, MASKDWORD);  /*save 0x38*/
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]OriginalGNT = 0x%x\n", original_gnt));
		/*set GNT_WL=1/GNT_BT=1  and path owner to WiFi for pause BT traffic*/
		odm_set_bb_reg(p_dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, 0x0000ff00);
		odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc0020038);	/*0x38[15:8] = 0x77*/
		odm_set_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, BIT(26), 0x1);
#endif
		_phy_iq_calibrate_8723d(p_dm, result, i, true);
#if 1
		/*Restore GNT_WL/GNT_BT  and path owner*/
		odm_set_bb_reg(p_dm, REG_LTECOEX_WRITE_DATA, MASKDWORD, original_gnt);
		odm_set_bb_reg(p_dm, REG_LTECOEX_CTRL, MASKDWORD, 0xc00f0038);
		odm_set_mac_reg(p_dm, REG_LTECOEX_PATH_CONTROL, 0xffffffff, original_path);
		/*Restore path control owner*/
		odm_set_mac_reg(p_dm, 0x64, MASKBYTE3, ori_path_ctrl);
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]restore 0x67 = 0x%x\n", odm_get_mac_reg(p_dm, 0x64, MASKBYTE3)));
#endif
		if (i == 1) {
			is12simular = phy_simularity_compare_8723d(p_dm, result, 0, 1);
			if (is12simular) {
				final_candidate = 0;
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK: is12simular final_candidate is %x\n", final_candidate));
				break;
			}
		}

		if (i == 2) {
			is13simular = phy_simularity_compare_8723d(p_dm, result, 0, 2);
			if (is13simular) {
				final_candidate = 0;
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK: is13simular final_candidate is %x\n", final_candidate));

				break;
			}
			is23simular = phy_simularity_compare_8723d(p_dm, result, 1, 2);
			if (is23simular) {
				final_candidate = 1;
				ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK: is23simular final_candidate is %x\n", final_candidate));
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

	for (i = 0; i < 4; i++) {
		rege94_s1 = result[i][0];
		rege9c_s1 = result[i][1];
		regea4_s1 = result[i][2];
		regeac_s1 = result[i][3];
		rege94_s0 = result[i][4];
		rege9c_s0 = result[i][5];
		regea4_s0 = result[i][6];
		regeac_s0 = result[i][7];
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK] rege94_s1=%x rege9c_s1=%x regea4_s1=%x regeac_s1=%x rege94_s0=%x rege9c_s0=%x regea4_s0=%x regeac_s0=%x\n ", rege94_s1, rege9c_s1, regea4_s1, regeac_s1, rege94_s0, rege9c_s0, regea4_s0, regeac_s0));
	}

	if (final_candidate != 0xff) {
		p_dm->rf_calibrate_info.rege94 = rege94_s1 = result[final_candidate][0];
		p_dm->rf_calibrate_info.rege9c = rege9c_s1 = result[final_candidate][1];
		regea4_s1 = result[final_candidate][2];
		regeac_s1 = result[final_candidate][3];
		p_dm->rf_calibrate_info.regeb4 = rege94_s0 = result[final_candidate][4];
		p_dm->rf_calibrate_info.regebc = rege9c_s0 = result[final_candidate][5];
		regea4_s0 = result[final_candidate][6];
		regeac_s0 = result[final_candidate][7];
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK] final_candidate is %x\n", final_candidate));
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK] TX1_X=%x TX1_Y=%x RX1_X=%x RX1_Y=%x TX0_X=%x TX0_Y=%x RX0_X=%x RX0_Y=%x\n ", rege94_s1, rege9c_s1, regea4_s1, regeac_s1, rege94_s0, rege9c_s0, regea4_s0, regeac_s0));
		is_path_s1_ok = is_path_s0_ok = true;
	} else {
		ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK] FAIL use default value\n"));
		p_dm->rf_calibrate_info.rege94 = p_dm->rf_calibrate_info.regeb4 = 0x100;
		p_dm->rf_calibrate_info.rege9c = p_dm->rf_calibrate_info.regebc = 0x0;
	}

	if (rege94_s1 != 0)
		_phy_path_s1_fill_iqk_matrix_8723d(p_dm, is_path_s1_ok, result, final_candidate, (regea4_s1 == 0));
	if (rege94_s0 != 0)
		_phy_path_s0_fill_iqk_matrix_8723d(p_dm, is_path_s0_ok, result, final_candidate, (regea4_s0 == 0));

	iqk_fail_a= p_dm->n_iqk_fail_cnt;
	if( iqk_fail_a - iqk_fail_b > 0 )
		RT_TRACE(COMP_INIT, DBG_LOUD, ("[8723dIQK]n_iqk_fail_cnt+,IQK restore to default value !\n"));
	
	regc80 = odm_get_bb_reg(p_dm, 0xc80, MASKDWORD);
	regc94 = odm_get_bb_reg(p_dm, 0xc94, MASKDWORD);
	regc14 = odm_get_bb_reg(p_dm, 0xc14, MASKDWORD);
	regca0 = odm_get_bb_reg(p_dm, 0xca0, MASKDWORD);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xc80 = 0x%x 0xc94 = 0x%x 0xc14 = 0x%x 0xca0 = 0x%x\n", regc80, regc94, regc14, regca0));

	regcd0 = odm_get_bb_reg(p_dm, 0xcd0, MASKDWORD);
	regcd4 = odm_get_bb_reg(p_dm, 0xcd4, MASKDWORD);
	regcd8 = odm_get_bb_reg(p_dm, 0xcd8, MASKDWORD);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("[IQK]0xcd0 = 0x%x 0xcd4 = 0x%x 0xcd8 = 0x%x\n", regcd0, regcd4, regcd8));
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	indexforchannel = odm_get_right_chnl_place_for_iqk(*p_dm->p_channel);
#else
	indexforchannel = 0;
#endif

	if (final_candidate < 4) {
		for (i = 0; i < iqk_matrix_reg_num; i++)
			p_dm->rf_calibrate_info.iqk_matrix_reg_setting[indexforchannel].value[0][i] = result[final_candidate][i];
		p_dm->rf_calibrate_info.iqk_matrix_reg_setting[indexforchannel].is_iqk_done = true;
	}
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("\nIQK OK indexforchannel %d.\n", indexforchannel));
	_phy_save_adda_registers_8723d(p_dm, IQK_BB_REG_92C, p_dm->rf_calibrate_info.IQK_BB_backup_recover, IQK_BB_REG_NUM);

	if (*(p_dm->p_mp_mode) == false)
		_phy_check_coex_status_8723d(p_dm, false);

	odm_set_bb_reg(p_dm, 0x948, MASKDWORD, path_sel_bb_phy_iqk);
	ODM_RT_TRACE(p_dm, ODM_COMP_CALIBRATION, ODM_DBG_LOUD, ("IQK finished\n"));
#endif
}


void
phy_lc_calibrate_8723d(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	_phy_lc_calibrate_8723d(p_dm, false);
}

void _phy_set_rf_path_switch_8723d(
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
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->DM_OutSrc;
#endif

	if (is_main)
		odm_set_mac_reg(p_dm, 0x7C4, MASKLWORD, 0x7700);
	else
		odm_set_mac_reg(p_dm, 0x7C4, MASKLWORD, 0xDD00);

	odm_set_mac_reg(p_dm, 0x7C0, MASKDWORD, 0xC00F0038);
	odm_set_mac_reg(p_dm, 0x70, BIT(26), 1);
	odm_set_mac_reg(p_dm, 0x64, BIT(31), 1);
}

void phy_set_rf_path_switch_8723d(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	boolean		is_main
)
{
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->odmpriv;
#endif

#if DISABLE_BB_RF
	return;
#endif

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	_phy_set_rf_path_switch_8723d(p_adapter, is_main, true);
#endif

}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
boolean _phy_query_rf_path_switch_8723d(
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


	if (odm_get_bb_reg(p_dm, 0x7C4, MASKLWORD) == 0x7700)
		return true;
	else
		return false;

}




boolean phy_query_rf_path_switch_8723d(
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
	return _phy_query_rf_path_switch_8723d(p_adapter, false);
#else
	return _phy_query_rf_path_switch_8723d(p_dm, false);
#endif

}
#endif



#else

void
phy_iq_calibrate_8723d(
	void		*p_dm_void,
	boolean	is_recovery
) {}
void
phy_lc_calibrate_8723d(
	void		*p_dm_void
) {}

void
odm_tx_pwr_track_set_pwr_8723d(
	struct PHY_DM_STRUCT			*p_dm,
	enum pwrtrack_method	method,
	u8				rf_path,
	u8				channel_mapped_index
) {}
#endif
