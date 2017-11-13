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

#ifndef index_mapping_NUM_88E
	#define	index_mapping_NUM_88E	15
#endif

/* #if(DM_ODM_SUPPORT_TYPE & ODM_WIN) */

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
	void		*p_dm_void,
	struct _TXPWRTRACK_CFG	*p_config
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
#if RTL8812A_SUPPORT
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	/* if (IS_HARDWARE_TYPE_8812(p_dm->adapter)) */
	if (p_dm->support_ic_type == ODM_RTL8812)
		configure_txpower_track_8812a(p_config);
	/* else */
#endif
#endif

#if RTL8814A_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8814A)
		configure_txpower_track_8814a(p_config);
#endif


#if RTL8188E_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8188E)
		configure_txpower_track_8188e(p_config);
#endif

#if RTL8197F_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8197F)
		configure_txpower_track_8197f(p_config);
#endif

#if RTL8822B_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8822B)
		configure_txpower_track_8822b(p_config);
#endif


}

#if (RTL8192E_SUPPORT == 1)
void
odm_txpowertracking_callback_thermal_meter_92e(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _IQK_INFORMATION	*p_iqk_info = &p_dm->IQK_info;
	u8	thermal_value = 0, delta, delta_IQK, delta_LCK, channel, is_decrease, rf_mimo_mode;
	u8	thermal_value_avg_count = 0;
	u8     OFDM_min_index = 10; /* OFDM BB Swing should be less than +2.5dB, which is required by Arthur */
	s8	OFDM_index[2], index ;
	u32	thermal_value_avg = 0, reg0x18;
	u32	i = 0, j = 0, rf;
	s32	value32, CCK_index = 0, ele_A, ele_D, ele_C, X, Y;
	struct rtl8192cd_priv	*priv = p_dm->priv;

	rf_mimo_mode = p_dm->rf_type;
	/* ODM_RT_TRACE(p_dm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("%s:%d rf_mimo_mode:%d\n", __FUNCTION__, __LINE__, rf_mimo_mode)); */

#ifdef MP_TEST
	if ((OPMODE & WIFI_MP_STATE) || *(p_dm->p_mp_mode)) {
		channel = priv->pshare->working_channel;
		if (priv->pshare->mp_txpwr_tracking == false)
			return;
	} else
#endif
	{
		channel = (priv->pmib->dot11RFEntry.dot11channel);
	}

	thermal_value = (unsigned char)odm_get_rf_reg(p_dm, RF_PATH_A, ODM_RF_T_METER_92E, 0xfc00);	/* 0x42: RF Reg[15:10] 88E */
	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", thermal_value, priv->pshare->thermal_value, priv->pmib->dot11RFEntry.ther));


	switch (rf_mimo_mode) {
	case RF_1T1R:
		rf = 1;
		break;
	case RF_2T2R:
		rf = 2;
		break;
	default:
		rf = 2;
		break;
	}

	/* Query OFDM path A default setting 	Bit[31:21] */
	ele_D = phy_query_bb_reg(priv, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKOFDM_D);
	for (i = 0; i < OFDM_TABLE_SIZE_92E; i++) {
		if (ele_D == (ofdm_swing_table_92e[i] >> 22)) {
			OFDM_index[0] = (unsigned char)i;
			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("PathA 0xC80[31:22] = 0x%x, OFDM_index=%d\n", ele_D, OFDM_index[0]));
			break;
		}
	}

	/* Query OFDM path B default setting */
	if (rf_mimo_mode == RF_2T2R) {
		ele_D = phy_query_bb_reg(priv, REG_OFDM_0_XB_TX_IQ_IMBALANCE, MASKOFDM_D);
		for (i = 0; i < OFDM_TABLE_SIZE_92E; i++) {
			if (ele_D == (ofdm_swing_table_92e[i] >> 22)) {
				OFDM_index[1] = (unsigned char)i;
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("PathB 0xC88[31:22] = 0x%x, OFDM_index=%d\n", ele_D, OFDM_index[1]));
				break;
			}
		}
	}

	/* calculate average thermal meter */
	{
		priv->pshare->thermal_value_avg_88xx[priv->pshare->thermal_value_avg_index_88xx] = thermal_value;
		priv->pshare->thermal_value_avg_index_88xx++;
		if (priv->pshare->thermal_value_avg_index_88xx == AVG_THERMAL_NUM_88XX)
			priv->pshare->thermal_value_avg_index_88xx = 0;

		for (i = 0; i < AVG_THERMAL_NUM_88XX; i++) {
			if (priv->pshare->thermal_value_avg_88xx[i]) {
				thermal_value_avg += priv->pshare->thermal_value_avg_88xx[i];
				thermal_value_avg_count++;
			}
		}

		if (thermal_value_avg_count) {
			thermal_value = (unsigned char)(thermal_value_avg / thermal_value_avg_count);
			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("AVG Thermal Meter = 0x%x\n", thermal_value));
		}
	}

	/* Initialize */
	if (!priv->pshare->thermal_value) {
		priv->pshare->thermal_value = priv->pmib->dot11RFEntry.ther;
		priv->pshare->thermal_value_iqk = thermal_value;
		priv->pshare->thermal_value_lck = thermal_value;
	}

	if (thermal_value != priv->pshare->thermal_value) {
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\n******** START POWER TRACKING ********\n"));
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", thermal_value, priv->pshare->thermal_value, priv->pmib->dot11RFEntry.ther));

		delta = RTL_ABS(thermal_value, priv->pmib->dot11RFEntry.ther);
		delta_IQK = RTL_ABS(thermal_value, priv->pshare->thermal_value_iqk);
		delta_LCK = RTL_ABS(thermal_value, priv->pshare->thermal_value_lck);
		is_decrease = ((thermal_value < priv->pmib->dot11RFEntry.ther) ? 1 : 0);

#ifdef _TRACKING_TABLE_FILE
		if (priv->pshare->rf_ft_var.pwr_track_file) {
			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("diff: (%s)%d ==> get index from table : %d)\n", (is_decrease ? "-" : "+"), delta, get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0)));

			if (is_decrease) {
				for (i = 0; i < rf; i++) {
					OFDM_index[i] = priv->pshare->OFDM_index0[i] + get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0);
					OFDM_index[i] = ((OFDM_index[i] > (OFDM_TABLE_SIZE_92E- 1)) ? (OFDM_TABLE_SIZE_92E - 1) : OFDM_index[i]);
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> decrese power ---> new OFDM_INDEX:%d (%d + %d)\n", OFDM_index[i], priv->pshare->OFDM_index0[i], get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0)));
					CCK_index = priv->pshare->CCK_index0 + get_tx_tracking_index(priv, channel, i, delta, is_decrease, 1);
					CCK_index = ((CCK_index > (CCK_TABLE_SIZE_92E - 1)) ? (CCK_TABLE_SIZE_92E - 1) : CCK_index);
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> Decrese power ---> new CCK_INDEX:%d (%d + %d)\n",  CCK_index, priv->pshare->CCK_index0, get_tx_tracking_index(priv, channel, i, delta, is_decrease, 1)));
				}
			} else {
				for (i = 0; i < rf; i++) {
					OFDM_index[i] = priv->pshare->OFDM_index0[i] - get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0);
					OFDM_index[i] = ((OFDM_index[i] < OFDM_min_index) ?  OFDM_min_index : OFDM_index[i]);
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> Increse power ---> new OFDM_INDEX:%d (%d - %d)\n", OFDM_index[i], priv->pshare->OFDM_index0[i], get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0)));
					CCK_index = priv->pshare->CCK_index0 - get_tx_tracking_index(priv, channel, i, delta, is_decrease, 1);
					CCK_index = ((CCK_index < 0) ? 0 : CCK_index);
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> Increse power ---> new CCK_INDEX:%d (%d - %d)\n", CCK_index, priv->pshare->CCK_index0, get_tx_tracking_index(priv, channel, i, delta, is_decrease, 1)));
				}
			}
		}
#endif /* CFG_TRACKING_TABLE_FILE */

		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("ofdm_swing_table_92e[(unsigned int)OFDM_index[0]] = %x\n", ofdm_swing_table_92e[(unsigned int)OFDM_index[0]]));
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("ofdm_swing_table_92e[(unsigned int)OFDM_index[1]] = %x\n", ofdm_swing_table_92e[(unsigned int)OFDM_index[1]]));

		/* Adujst OFDM Ant_A according to IQK result */
		ele_D = (ofdm_swing_table_92e[(unsigned int)OFDM_index[0]] & 0xFFC00000) >> 22;
		X = priv->pshare->rege94;
		Y = priv->pshare->rege9c;

		if (X != 0) {
			if ((X & 0x00000200) != 0)
				X = X | 0xFFFFFC00;
			ele_A = ((X * ele_D) >> 8) & 0x000003FF;

			/* new element C = element D x Y */
			if ((Y & 0x00000200) != 0)
				Y = Y | 0xFFFFFC00;
			ele_C = ((Y * ele_D) >> 8) & 0x000003FF;

			/* wirte new elements A, C, D to regC80 and regC94, element B is always 0 */
			value32 = (ele_D << 22) | ((ele_C & 0x3F) << 16) | ele_A;
			phy_set_bb_reg(priv, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD, value32);

			value32 = (ele_C & 0x000003C0) >> 6;
			phy_set_bb_reg(priv, REG_OFDM_0_XC_TX_AFE, MASKH4BITS, value32);

			value32 = ((X * ele_D) >> 7) & 0x01;
			phy_set_bb_reg(priv, REG_OFDM_0_ECCA_THRESHOLD, BIT(24), value32);
		} else {
			phy_set_bb_reg(priv, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD, ofdm_swing_table_92e[(unsigned int)OFDM_index[0]]);
			phy_set_bb_reg(priv, REG_OFDM_0_XC_TX_AFE, MASKH4BITS, 0x00);
			phy_set_bb_reg(priv, REG_OFDM_0_ECCA_THRESHOLD, BIT(24), 0x00);
		}

		set_CCK_swing_index(priv, CCK_index);

		if (rf == 2) {
			ele_D = (ofdm_swing_table_92e[(unsigned int)OFDM_index[1]] & 0xFFC00000) >> 22;
			X = priv->pshare->regeb4;
			Y = priv->pshare->regebc;

			if (X != 0) {
				if ((X & 0x00000200) != 0)	/* consider minus */
					X = X | 0xFFFFFC00;
				ele_A = ((X * ele_D) >> 8) & 0x000003FF;

				/* new element C = element D x Y */
				if ((Y & 0x00000200) != 0)
					Y = Y | 0xFFFFFC00;
				ele_C = ((Y * ele_D) >> 8) & 0x00003FF;

				/* wirte new elements A, C, D to regC88 and regC9C, element B is always 0 */
				value32 = (ele_D << 22) | ((ele_C & 0x3F) << 16) | ele_A;
				phy_set_bb_reg(priv, REG_OFDM_0_XB_TX_IQ_IMBALANCE, MASKDWORD, value32);

				value32 = (ele_C & 0x000003C0) >> 6;
				phy_set_bb_reg(priv, REG_OFDM_0_XD_TX_AFE, MASKH4BITS, value32);

				value32 = ((X * ele_D) >> 7) & 0x01;
				phy_set_bb_reg(priv, REG_OFDM_0_ECCA_THRESHOLD, BIT(28), value32);
			} else {
				phy_set_bb_reg(priv, REG_OFDM_0_XB_TX_IQ_IMBALANCE, MASKDWORD, ofdm_swing_table_92e[(unsigned int)OFDM_index[1]]);
				phy_set_bb_reg(priv, REG_OFDM_0_XD_TX_AFE, MASKH4BITS, 0x00);
				phy_set_bb_reg(priv, REG_OFDM_0_ECCA_THRESHOLD, BIT(28), 0x00);
			}

		}

		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("0xc80 = 0x%x\n", phy_query_bb_reg(priv, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD)));
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("0xc88 = 0x%x\n", phy_query_bb_reg(priv, REG_OFDM_0_XB_TX_IQ_IMBALANCE, MASKDWORD)));

		if ((delta_IQK > 3) && (!p_iqk_info->rfk_forbidden)) {
			priv->pshare->thermal_value_iqk = thermal_value;
#ifdef MP_TEST
#endif			if (!(*(p_dm->p_mp_mode) && (OPMODE & (WIFI_MP_CTX_BACKGROUND | WIFI_MP_CTX_PACKET))))

				halrf_iqk_trigger(p_dm, false);
		}

		if ((delta_LCK > 8)  && (!p_iqk_info->rfk_forbidden)) {
			RTL_W8(0x522, 0xff);
			reg0x18 = phy_query_rf_reg(priv, RF_PATH_A, 0x18, MASK20BITS, 1);
			phy_set_rf_reg(priv, RF_PATH_A, 0xB4, BIT(14), 1);
			phy_set_rf_reg(priv, RF_PATH_A, 0x18, BIT(15), 1);
			delay_ms(1);
			phy_set_rf_reg(priv, RF_PATH_A, 0xB4, BIT(14), 0);
			phy_set_rf_reg(priv, RF_PATH_A, 0x18, MASK20BITS, reg0x18);
			RTL_W8(0x522, 0x0);
			priv->pshare->thermal_value_lck = thermal_value;
		}
	}

	/* update thermal meter value */
	priv->pshare->thermal_value = thermal_value;
	for (i = 0 ; i < rf ; i++)
		priv->pshare->OFDM_index[i] = OFDM_index[i];
	priv->pshare->CCK_index = CCK_index;

	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\n******** END:%s() ********\n", __FUNCTION__));
}
#endif



#if (RTL8197F_SUPPORT == 1 || RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
void
odm_txpowertracking_callback_thermal_meter_jaguar_series3(
	void		*p_dm_void
)
{
#if 1
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			thermal_value = 0, delta, delta_LCK, delta_IQK, channel, is_increase;
	u8			thermal_value_avg_count = 0, p = 0, i = 0;
	u32			thermal_value_avg = 0;
	struct rtl8192cd_priv		*priv = p_dm->priv;
	struct _TXPWRTRACK_CFG	c;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
	struct _IQK_INFORMATION	*p_iqk_info = &p_dm->IQK_info;
	/*4 1. The following TWO tables decide the final index of OFDM/CCK swing table.*/
	u8			*delta_swing_table_idx_tup_a = NULL, *delta_swing_table_idx_tdown_a = NULL;
	u8			*delta_swing_table_idx_tup_b = NULL, *delta_swing_table_idx_tdown_b = NULL;
	u8			*delta_swing_table_idx_tup_cck_a = NULL, *delta_swing_table_idx_tdown_cck_a = NULL;
	u8			*delta_swing_table_idx_tup_cck_b = NULL, *delta_swing_table_idx_tdown_cck_b = NULL;
	/*for 8814 add by Yu Chen*/
	u8			*delta_swing_table_idx_tup_c = NULL, *delta_swing_table_idx_tdown_c = NULL;
	u8			*delta_swing_table_idx_tup_d = NULL, *delta_swing_table_idx_tdown_d = NULL;
	u8			*delta_swing_table_idx_tup_cck_c = NULL, *delta_swing_table_idx_tdown_cck_c = NULL;
	u8			*delta_swing_table_idx_tup_cck_d = NULL, *delta_swing_table_idx_tdown_cck_d = NULL;

#ifdef MP_TEST
	if ((OPMODE & WIFI_MP_STATE) || *(p_dm->p_mp_mode)) {
		channel = priv->pshare->working_channel;
		if (priv->pshare->mp_txpwr_tracking == false)
			return;
	} else
#endif
	{
		channel = (priv->pmib->dot11RFEntry.dot11channel);
	}

	configure_txpower_track(p_dm, &c);

	(*c.get_delta_all_swing_table)(p_dm, (u8 **)&delta_swing_table_idx_tup_a, (u8 **)&delta_swing_table_idx_tdown_a,
		(u8 **)&delta_swing_table_idx_tup_b, (u8 **)&delta_swing_table_idx_tdown_b,
		(u8 **)&delta_swing_table_idx_tup_cck_a, (u8 **)&delta_swing_table_idx_tdown_cck_a,
		(u8 **)&delta_swing_table_idx_tup_cck_b, (u8 **)&delta_swing_table_idx_tdown_cck_b);

	thermal_value = (u8)odm_get_rf_reg(p_dm, RF_PATH_A, c.thermal_reg_addr, 0xfc00); /*0x42: RF Reg[15:10] 88E*/
#ifdef THER_TRIM
	if (GET_CHIP_VER(priv) == VERSION_8197F) {
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("orig thermal_value=%d, ther_trim_val=%d\n", thermal_value, priv->pshare->rf_ft_var.ther_trim_val));

		thermal_value += priv->pshare->rf_ft_var.ther_trim_val;

		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("after thermal trim, thermal_value=%d\n", thermal_value));
	}
#endif
	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("Readback Thermal Meter = 0x%x(%d) EEPROMthermalmeter 0x%x(%d)\n"
		, thermal_value, thermal_value, priv->pmib->dot11RFEntry.ther, priv->pmib->dot11RFEntry.ther));

	/* Initialize */
	if (!p_dm->rf_calibrate_info.thermal_value)
		p_dm->rf_calibrate_info.thermal_value = priv->pmib->dot11RFEntry.ther;

	if (!p_dm->rf_calibrate_info.thermal_value_lck)
		p_dm->rf_calibrate_info.thermal_value_lck = priv->pmib->dot11RFEntry.ther;

	if (!p_dm->rf_calibrate_info.thermal_value_iqk)
		p_dm->rf_calibrate_info.thermal_value_iqk = priv->pmib->dot11RFEntry.ther;

	/* calculate average thermal meter */
	p_dm->rf_calibrate_info.thermal_value_avg[p_dm->rf_calibrate_info.thermal_value_avg_index] = thermal_value;
	p_dm->rf_calibrate_info.thermal_value_avg_index++;

	if (p_dm->rf_calibrate_info.thermal_value_avg_index == c.average_thermal_num)   /*Average times =  c.average_thermal_num*/
		p_dm->rf_calibrate_info.thermal_value_avg_index = 0;

	for (i = 0; i < c.average_thermal_num; i++) {
		if (p_dm->rf_calibrate_info.thermal_value_avg[i]) {
			thermal_value_avg += p_dm->rf_calibrate_info.thermal_value_avg[i];
			thermal_value_avg_count++;
		}
	}

	if (thermal_value_avg_count) {/*Calculate Average thermal_value after average enough times*/
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("thermal_value_avg=0x%x(%d)  thermal_value_avg_count = %d\n"
			, thermal_value_avg, thermal_value_avg, thermal_value_avg_count));

		thermal_value = (u8)(thermal_value_avg / thermal_value_avg_count);

		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("AVG Thermal Meter = 0x%X(%d), EEPROMthermalmeter = 0x%X(%d)\n", thermal_value, thermal_value, priv->pmib->dot11RFEntry.ther, priv->pmib->dot11RFEntry.ther));
	}

	/*4 Calculate delta, delta_LCK, delta_IQK.*/
	delta = RTL_ABS(thermal_value, priv->pmib->dot11RFEntry.ther);
	delta_LCK = RTL_ABS(thermal_value, p_dm->rf_calibrate_info.thermal_value_lck);
	delta_IQK = RTL_ABS(thermal_value, p_dm->rf_calibrate_info.thermal_value_iqk);
	is_increase = ((thermal_value < priv->pmib->dot11RFEntry.ther) ? 0 : 1);

	if (delta > 29) { /* power track table index(thermal diff.) upper bound*/
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("delta(%d) > 29, set delta to 29\n", delta));
		delta = 29;
	}


	/*4 if necessary, do LCK.*/
	if ((delta_LCK > c.threshold_iqk) && (!p_iqk_info->rfk_forbidden)) {
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("delta_LCK(%d) >= threshold_iqk(%d)\n", delta_LCK, c.threshold_iqk));
		p_dm->rf_calibrate_info.thermal_value_lck = thermal_value;
#if (RTL8822B_SUPPORT != 1)
		if (!(p_dm->support_ic_type & ODM_RTL8822B)) {
		if (c.phy_lc_calibrate)
			(*c.phy_lc_calibrate)(p_dm);
	}
#endif
	}

	if ((delta_IQK > c.threshold_iqk) && (!p_iqk_info->rfk_forbidden)) {
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("delta_IQK(%d) >= threshold_iqk(%d)\n", delta_IQK, c.threshold_iqk));
		p_dm->rf_calibrate_info.thermal_value_iqk = thermal_value;
		if (c.do_iqk)
			(*c.do_iqk)(p_dm, true, 0, 0);
	}

	if (!priv->pmib->dot11RFEntry.ther)	/*Don't do power tracking since no calibrated thermal value*/
		return;

	/*4 Do Power Tracking*/

	if (thermal_value != p_dm->rf_calibrate_info.thermal_value) {
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			     ("\n\n******** START POWER TRACKING ********\n"));
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("Readback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n",
			thermal_value, p_dm->rf_calibrate_info.thermal_value, priv->pmib->dot11RFEntry.ther));

#ifdef _TRACKING_TABLE_FILE
		if (priv->pshare->rf_ft_var.pwr_track_file) {
			if (is_increase) {			/*thermal is higher than base*/
				for (p = RF_PATH_A; p < c.rf_path_count; p++) {
					switch (p) {
					case RF_PATH_B:
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tup_b[%d] = %d delta_swing_table_idx_tup_cck_b[%d] = %d\n", delta, delta_swing_table_idx_tup_b[delta], delta, delta_swing_table_idx_tup_cck_b[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_b[delta];
						p_rf_calibrate_info->absolute_cck_swing_idx[p] = delta_swing_table_idx_tup_cck_b[delta];
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and pRF->absolute_ofdm_swing_idx[RF_PATH_B] = %d pRF->absolute_cck_swing_idx[RF_PATH_B] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p], p_rf_calibrate_info->absolute_cck_swing_idx[p]));
						break;

					case RF_PATH_C:
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tup_c[%d] = %d delta_swing_table_idx_tup_cck_c[%d] = %d\n", delta, delta_swing_table_idx_tup_c[delta], delta, delta_swing_table_idx_tup_cck_c[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_c[delta];
						p_rf_calibrate_info->absolute_cck_swing_idx[p] = delta_swing_table_idx_tup_cck_c[delta];
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and pRF->absolute_ofdm_swing_idx[RF_PATH_C] = %d pRF->absolute_cck_swing_idx[RF_PATH_C] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p], p_rf_calibrate_info->absolute_cck_swing_idx[p]));
						break;

					case RF_PATH_D:
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tup_d[%d] = %d delta_swing_table_idx_tup_cck_d[%d] = %d\n", delta, delta_swing_table_idx_tup_d[delta], delta, delta_swing_table_idx_tup_cck_d[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_d[delta];
						p_rf_calibrate_info->absolute_cck_swing_idx[p] = delta_swing_table_idx_tup_cck_d[delta];
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and pRF->absolute_ofdm_swing_idx[RF_PATH_D] = %d pRF->absolute_cck_swing_idx[RF_PATH_D] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p], p_rf_calibrate_info->absolute_cck_swing_idx[p]));
						break;
					default:
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tup_a[%d] = %d delta_swing_table_idx_tup_cck_a[%d] = %d\n", delta, delta_swing_table_idx_tup_a[delta], delta, delta_swing_table_idx_tup_cck_a[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_a[delta];
						p_rf_calibrate_info->absolute_cck_swing_idx[p] = delta_swing_table_idx_tup_cck_a[delta];
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and pRF->absolute_ofdm_swing_idx[RF_PATH_A] = %d pRF->absolute_cck_swing_idx[RF_PATH_A] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p], p_rf_calibrate_info->absolute_cck_swing_idx[p]));
						break;
					}
				}
			} else {			/* thermal is lower than base*/
				for (p = RF_PATH_A; p < c.rf_path_count; p++) {
					switch (p) {
					case RF_PATH_B:
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tdown_b[%d] = %d   delta_swing_table_idx_tdown_cck_b[%d] = %d\n", delta, delta_swing_table_idx_tdown_b[delta], delta, delta_swing_table_idx_tdown_cck_b[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_b[delta];
						p_rf_calibrate_info->absolute_cck_swing_idx[p] = -1 * delta_swing_table_idx_tdown_cck_b[delta];
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and pRF->absolute_ofdm_swing_idx[RF_PATH_B] = %d   pRF->absolute_cck_swing_idx[RF_PATH_B] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p], p_rf_calibrate_info->absolute_cck_swing_idx[p]));
						break;

					case RF_PATH_C:
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tdown_c[%d] = %d   delta_swing_table_idx_tdown_cck_c[%d] = %d\n", delta, delta_swing_table_idx_tdown_c[delta], delta, delta_swing_table_idx_tdown_cck_c[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_c[delta];
						p_rf_calibrate_info->absolute_cck_swing_idx[p] = -1 * delta_swing_table_idx_tdown_cck_c[delta];
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and pRF->absolute_ofdm_swing_idx[RF_PATH_C] = %d   pRF->absolute_cck_swing_idx[RF_PATH_C] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p], p_rf_calibrate_info->absolute_cck_swing_idx[p]));
						break;

					case RF_PATH_D:
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tdown_d[%d] = %d   delta_swing_table_idx_tdown_cck_d[%d] = %d\n", delta, delta_swing_table_idx_tdown_d[delta], delta, delta_swing_table_idx_tdown_cck_d[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_d[delta];
						p_rf_calibrate_info->absolute_cck_swing_idx[p] = -1 * delta_swing_table_idx_tdown_cck_d[delta];
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and pRF->absolute_ofdm_swing_idx[RF_PATH_D] = %d   pRF->absolute_cck_swing_idx[RF_PATH_D] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p], p_rf_calibrate_info->absolute_cck_swing_idx[p]));
						break;

					default:
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tdown_a[%d] = %d   delta_swing_table_idx_tdown_cck_a[%d] = %d\n", delta, delta_swing_table_idx_tdown_a[delta], delta, delta_swing_table_idx_tdown_cck_a[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_a[delta];
						p_rf_calibrate_info->absolute_cck_swing_idx[p] = -1 * delta_swing_table_idx_tdown_cck_a[delta];
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and pRF->absolute_ofdm_swing_idx[RF_PATH_A] = %d   pRF->absolute_cck_swing_idx[RF_PATH_A] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p], p_rf_calibrate_info->absolute_cck_swing_idx[p]));
						break;
					}
				}
			}

			if (is_increase) {
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> increse power --->\n"));
				if (GET_CHIP_VER(priv) == VERSION_8197F) {
					for (p = RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(p_dm, BBSWING, p, 0);
				} else if (GET_CHIP_VER(priv) == VERSION_8822B) {
					for (p = RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(p_dm, MIX_MODE, p, 0);
				} else if (GET_CHIP_VER(priv) == VERSION_8821C) {
					for (p = RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(p_dm, MIX_MODE, p, 0);
				}
			} else {
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> decrese power --->\n"));
				if (GET_CHIP_VER(priv) == VERSION_8197F) {
					for (p = RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(p_dm, BBSWING, p, 0);
				} else if (GET_CHIP_VER(priv) == VERSION_8822B) {
					for (p = RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(p_dm, MIX_MODE, p, 0);
				} else if (GET_CHIP_VER(priv) == VERSION_8821C) {
					for (p = RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(p_dm, MIX_MODE, p, 0);
				}
			}
		}
#endif

		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\n******** END:%s() ********\n\n", __func__));
		/*update thermal meter value*/
		p_dm->rf_calibrate_info.thermal_value =  thermal_value;

	}

#endif
}
#endif

/*#if (RTL8814A_SUPPORT == 1)*/
#if (RTL8814A_SUPPORT == 1)

void
odm_txpowertracking_callback_thermal_meter_jaguar_series2(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			thermal_value = 0, delta, delta_LCK, delta_IQK, channel, is_increase;
	u8			thermal_value_avg_count = 0, p = 0, i = 0;
	u32			thermal_value_avg = 0, reg0x18;
	u32			bb_swing_reg[4] = {REG_A_TX_SCALE_JAGUAR, REG_B_TX_SCALE_JAGUAR, REG_C_TX_SCALE_JAGUAR2, REG_D_TX_SCALE_JAGUAR2};
	s32			ele_D;
	u32			bb_swing_idx;
	struct rtl8192cd_priv	*priv = p_dm->priv;
	struct _TXPWRTRACK_CFG	c;
	boolean			is_tssi_enable = false;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
	struct _IQK_INFORMATION	*p_iqk_info = &p_dm->IQK_info;

	/* 4 1. The following TWO tables decide the final index of OFDM/CCK swing table. */
	u8			*delta_swing_table_idx_tup_a = NULL, *delta_swing_table_idx_tdown_a = NULL;
	u8			*delta_swing_table_idx_tup_b = NULL, *delta_swing_table_idx_tdown_b = NULL;
	/* for 8814 add by Yu Chen */
	u8			*delta_swing_table_idx_tup_c = NULL, *delta_swing_table_idx_tdown_c = NULL;
	u8			*delta_swing_table_idx_tup_d = NULL, *delta_swing_table_idx_tdown_d = NULL;

#ifdef MP_TEST
	if ((OPMODE & WIFI_MP_STATE) || *(p_dm->p_mp_mode)) {
		channel = priv->pshare->working_channel;
		if (priv->pshare->mp_txpwr_tracking == false)
			return;
	} else
#endif
	{
		channel = (priv->pmib->dot11RFEntry.dot11channel);
	}

	configure_txpower_track(p_dm, &c);
	p_rf_calibrate_info->default_ofdm_index = priv->pshare->OFDM_index0[RF_PATH_A];

	(*c.get_delta_swing_table)(p_dm, (u8 **)&delta_swing_table_idx_tup_a, (u8 **)&delta_swing_table_idx_tdown_a,
		(u8 **)&delta_swing_table_idx_tup_b, (u8 **)&delta_swing_table_idx_tdown_b);

	if (p_dm->support_ic_type & ODM_RTL8814A)	/* for 8814 path C & D */
		(*c.get_delta_swing_table8814only)(p_dm, (u8 **)&delta_swing_table_idx_tup_c, (u8 **)&delta_swing_table_idx_tdown_c,
			(u8 **)&delta_swing_table_idx_tup_d, (u8 **)&delta_swing_table_idx_tdown_d);

	thermal_value = (u8)odm_get_rf_reg(p_dm, RF_PATH_A, c.thermal_reg_addr, 0xfc00); /* 0x42: RF Reg[15:10] 88E */
	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("\nReadback Thermal Meter = 0x%x, pre thermal meter 0x%x, EEPROMthermalmeter 0x%x\n", thermal_value, p_dm->rf_calibrate_info.thermal_value, priv->pmib->dot11RFEntry.ther));

	/* Initialize */
	if (!p_dm->rf_calibrate_info.thermal_value)
		p_dm->rf_calibrate_info.thermal_value = priv->pmib->dot11RFEntry.ther;

	if (!p_dm->rf_calibrate_info.thermal_value_lck)
		p_dm->rf_calibrate_info.thermal_value_lck = priv->pmib->dot11RFEntry.ther;

	if (!p_dm->rf_calibrate_info.thermal_value_iqk)
		p_dm->rf_calibrate_info.thermal_value_iqk = priv->pmib->dot11RFEntry.ther;

	is_tssi_enable = (boolean)odm_get_rf_reg(p_dm, RF_PATH_A, REG_RF_TX_GAIN_OFFSET, BIT(7));	/* check TSSI enable */

	/* 4 Query OFDM BB swing default setting 	Bit[31:21] */
	for (p = RF_PATH_A ; p < c.rf_path_count ; p++) {
		ele_D = odm_get_bb_reg(p_dm, bb_swing_reg[p], 0xffe00000);
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("0x%x:0x%x ([31:21] = 0x%x)\n", bb_swing_reg[p], odm_get_bb_reg(p_dm, bb_swing_reg[p], MASKDWORD), ele_D));

		for (bb_swing_idx = 0; bb_swing_idx < TXSCALE_TABLE_SIZE; bb_swing_idx++) {/* 4 */
			if (ele_D == tx_scaling_table_jaguar[bb_swing_idx]) {
				p_dm->rf_calibrate_info.OFDM_index[p] = (u8)bb_swing_idx;
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
					("OFDM_index[%d]=%d\n", p, p_dm->rf_calibrate_info.OFDM_index[p]));
				break;
			}
		}
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("kfree_offset[%d]=%d\n", p, p_rf_calibrate_info->kfree_offset[p]));

	}

	/* calculate average thermal meter */
	p_dm->rf_calibrate_info.thermal_value_avg[p_dm->rf_calibrate_info.thermal_value_avg_index] = thermal_value;
	p_dm->rf_calibrate_info.thermal_value_avg_index++;
	if (p_dm->rf_calibrate_info.thermal_value_avg_index == c.average_thermal_num)  /* Average times =  c.average_thermal_num */
		p_dm->rf_calibrate_info.thermal_value_avg_index = 0;

	for (i = 0; i < c.average_thermal_num; i++) {
		if (p_dm->rf_calibrate_info.thermal_value_avg[i]) {
			thermal_value_avg += p_dm->rf_calibrate_info.thermal_value_avg[i];
			thermal_value_avg_count++;
		}
	}

	if (thermal_value_avg_count) {            /* Calculate Average thermal_value after average enough times */
		thermal_value = (u8)(thermal_value_avg / thermal_value_avg_count);
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("AVG Thermal Meter = 0x%X, EEPROMthermalmeter = 0x%X\n", thermal_value, priv->pmib->dot11RFEntry.ther));
	}

	/* 4 Calculate delta, delta_LCK, delta_IQK. */
	delta = RTL_ABS(thermal_value, priv->pmib->dot11RFEntry.ther);
	delta_LCK = RTL_ABS(thermal_value, p_dm->rf_calibrate_info.thermal_value_lck);
	delta_IQK = RTL_ABS(thermal_value, p_dm->rf_calibrate_info.thermal_value_iqk);
	is_increase = ((thermal_value < priv->pmib->dot11RFEntry.ther) ? 0 : 1);

	/* 4 if necessary, do LCK. */
	if (!(p_dm->support_ic_type & ODM_RTL8821)) {
		if ((delta_LCK > c.threshold_iqk) && (!p_iqk_info->rfk_forbidden)) {
			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("delta_LCK(%d) >= threshold_iqk(%d)\n", delta_LCK, c.threshold_iqk));
			p_dm->rf_calibrate_info.thermal_value_lck = thermal_value;

			/*Use RTLCK, so close power tracking driver LCK*/
#if (RTL8814A_SUPPORT != 1)
			if (!(p_dm->support_ic_type & ODM_RTL8814A)) {
				if (c.phy_lc_calibrate)
					(*c.phy_lc_calibrate)(p_dm);
			}
#endif
		}
	}

	if ((delta_IQK > c.threshold_iqk) && (!p_iqk_info->rfk_forbidden)) {
		panic_printk("%s(%d)\n", __FUNCTION__, __LINE__);
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("delta_IQK(%d) >= threshold_iqk(%d)\n", delta_IQK, c.threshold_iqk));
		p_dm->rf_calibrate_info.thermal_value_iqk = thermal_value;
		if (c.do_iqk)
			(*c.do_iqk)(p_dm, true, 0, 0);
	}

	if (!priv->pmib->dot11RFEntry.ther)	/*Don't do power tracking since no calibrated thermal value*/
		return;

	/* 4 Do Power Tracking */

	if (is_tssi_enable == true) {
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("**********Enter PURE TSSI MODE**********\n"));
		for (p = RF_PATH_A; p < c.rf_path_count; p++)
			(*c.odm_tx_pwr_track_set_pwr)(p_dm, TSSI_MODE, p, 0);
	} else if (thermal_value != p_dm->rf_calibrate_info.thermal_value) {
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			     ("\n******** START POWER TRACKING ********\n"));
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", thermal_value, p_dm->rf_calibrate_info.thermal_value, priv->pmib->dot11RFEntry.ther));

#ifdef _TRACKING_TABLE_FILE
		if (priv->pshare->rf_ft_var.pwr_track_file) {
			if (is_increase) {		/* thermal is higher than base */
				for (p = RF_PATH_A; p < c.rf_path_count; p++) {
					switch (p) {
					case RF_PATH_B:
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tup_b[%d] = %d\n", delta, delta_swing_table_idx_tup_b[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_b[delta];       /* Record delta swing for mix mode power tracking */
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and p_dm->absolute_ofdm_swing_idx[RF_PATH_B] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
						break;

					case RF_PATH_C:
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tup_c[%d] = %d\n", delta, delta_swing_table_idx_tup_c[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_c[delta];       /* Record delta swing for mix mode power tracking */
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and p_dm->absolute_ofdm_swing_idx[RF_PATH_C] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
						break;

					case RF_PATH_D:
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tup_d[%d] = %d\n", delta, delta_swing_table_idx_tup_d[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_d[delta];       /* Record delta swing for mix mode power tracking */
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and p_dm->absolute_ofdm_swing_idx[RF_PATH_D] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
						break;

					default:
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tup_a[%d] = %d\n", delta, delta_swing_table_idx_tup_a[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_a[delta];        /* Record delta swing for mix mode power tracking */
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and p_dm->absolute_ofdm_swing_idx[RF_PATH_A] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
						break;
					}
				}
			} else {				/* thermal is lower than base */
				for (p = RF_PATH_A; p < c.rf_path_count; p++) {
					switch (p) {
					case RF_PATH_B:
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tdown_b[%d] = %d\n", delta, delta_swing_table_idx_tdown_b[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_b[delta];        /* Record delta swing for mix mode power tracking */
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and p_dm->absolute_ofdm_swing_idx[RF_PATH_B] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
						break;

					case RF_PATH_C:
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tdown_c[%d] = %d\n", delta, delta_swing_table_idx_tdown_c[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_c[delta];        /* Record delta swing for mix mode power tracking */
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and p_dm->absolute_ofdm_swing_idx[RF_PATH_C] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
						break;

					case RF_PATH_D:
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tdown_d[%d] = %d\n", delta, delta_swing_table_idx_tdown_d[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_d[delta];        /* Record delta swing for mix mode power tracking */
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and p_dm->absolute_ofdm_swing_idx[RF_PATH_D] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
						break;

					default:
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tdown_a[%d] = %d\n", delta, delta_swing_table_idx_tdown_a[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_a[delta];        /* Record delta swing for mix mode power tracking */
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and p_dm->absolute_ofdm_swing_idx[RF_PATH_A] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
						break;
					}
				}
			}

			if (is_increase) {
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> increse power --->\n"));
				for (p = RF_PATH_A; p < c.rf_path_count; p++)
					(*c.odm_tx_pwr_track_set_pwr)(p_dm, MIX_MODE, p, 0);
			} else {
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> decrese power --->\n"));
				for (p = RF_PATH_A; p < c.rf_path_count; p++)
					(*c.odm_tx_pwr_track_set_pwr)(p_dm, MIX_MODE, p, 0);
			}
		}
#endif

		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\n******** END:%s() ********\n", __FUNCTION__));
		/* update thermal meter value */
		p_dm->rf_calibrate_info.thermal_value =  thermal_value;

	}
}
#endif

#if (RTL8812A_SUPPORT == 1 || RTL8881A_SUPPORT == 1)
void
odm_txpowertracking_callback_thermal_meter_jaguar_series(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	unsigned char			thermal_value = 0, delta, delta_LCK, channel, is_decrease;
	unsigned char			thermal_value_avg_count = 0;
	unsigned int			thermal_value_avg = 0, reg0x18;
	unsigned int			bb_swing_reg[4] = {0xc1c, 0xe1c, 0x181c, 0x1a1c};
	int					ele_D, value32;
	char					OFDM_index[2], index;
	unsigned int			i = 0, j = 0, rf_path, max_rf_path = 2, rf;
	struct rtl8192cd_priv		*priv = p_dm->priv;
	unsigned char			OFDM_min_index = 7; /* OFDM BB Swing should be less than +2.5dB, which is required by Arthur and Mimic */
	struct _IQK_INFORMATION	*p_iqk_info = &p_dm->IQK_info;


#ifdef MP_TEST
	if ((OPMODE & WIFI_MP_STATE) || *(p_dm->p_mp_mode)) {
		channel = priv->pshare->working_channel;
		if (priv->pshare->mp_txpwr_tracking == false)
			return;
	} else
#endif
	{
		channel = (priv->pmib->dot11RFEntry.dot11channel);
	}

#if RTL8881A_SUPPORT
	if (p_dm->support_ic_type == ODM_RTL8881A) {
		max_rf_path = 1;
		if ((get_bonding_type_8881A() == BOND_8881AM || get_bonding_type_8881A() == BOND_8881AN)
		    && priv->pshare->rf_ft_var.use_intpa8881A && (*p_dm->p_band_type == ODM_BAND_2_4G))
			OFDM_min_index = 6;		/* intPA - upper bond set to +3 dB (base: -2 dB)ot11RFEntry.phy_band_select == PHY_BAND_2G)) */
		else
			OFDM_min_index = 10;		/* OFDM BB Swing should be less than +1dB, which is required by Arthur and Mimic */
	}
#endif


	thermal_value = (unsigned char)phy_query_rf_reg(priv, RF_PATH_A, 0x42, 0xfc00, 1); /* 0x42: RF Reg[15:10] 88E */
	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", thermal_value, priv->pshare->thermal_value, priv->pmib->dot11RFEntry.ther));


	/* 4 Query OFDM BB swing default setting 	Bit[31:21] */
	for (rf_path = 0 ; rf_path < max_rf_path ; rf_path++) {
		ele_D = phy_query_bb_reg(priv, bb_swing_reg[rf_path], 0xffe00000);
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("0x%x:0x%x ([31:21] = 0x%x)\n", bb_swing_reg[rf_path], phy_query_bb_reg(priv, bb_swing_reg[rf_path], MASKDWORD), ele_D));
		for (i = 0; i < OFDM_TABLE_SIZE_8812; i++) {/* 4 */
			if (ele_D == ofdm_swing_table_8812[i]) {
				OFDM_index[rf_path] = (unsigned char)i;
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("OFDM_index[%d]=%d\n", rf_path, OFDM_index[rf_path]));
				break;
			}
		}
	}
#if 0
	/* Query OFDM path A default setting 	Bit[31:21] */
	ele_D = phy_query_bb_reg(priv, 0xc1c, 0xffe00000);
	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("0xc1c:0x%x ([31:21] = 0x%x)\n", phy_query_bb_reg(priv, 0xc1c, MASKDWORD), ele_D));
	for (i = 0; i < OFDM_TABLE_SIZE_8812; i++) {/* 4 */
		if (ele_D == ofdm_swing_table_8812[i]) {
			OFDM_index[0] = (unsigned char)i;
			ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("OFDM_index[0]=%d\n", OFDM_index[0]));
			break;
		}
	}
	/* Query OFDM path B default setting */
	if (rf == 2) {
		ele_D = phy_query_bb_reg(priv, 0xe1c, 0xffe00000);
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("0xe1c:0x%x ([32:21] = 0x%x)\n", phy_query_bb_reg(priv, 0xe1c, MASKDWORD), ele_D));
		for (i = 0; i < OFDM_TABLE_SIZE_8812; i++) {
			if (ele_D == ofdm_swing_table_8812[i]) {
				OFDM_index[1] = (unsigned char)i;
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("OFDM_index[1]=%d\n", OFDM_index[1]));
				break;
			}
		}
	}
#endif
	/* Initialize */
	if (!priv->pshare->thermal_value) {
		priv->pshare->thermal_value = priv->pmib->dot11RFEntry.ther;
		priv->pshare->thermal_value_lck = thermal_value;
	}

	/* calculate average thermal meter */
	{
		priv->pshare->thermal_value_avg_8812[priv->pshare->thermal_value_avg_index_8812] = thermal_value;
		priv->pshare->thermal_value_avg_index_8812++;
		if (priv->pshare->thermal_value_avg_index_8812 == AVG_THERMAL_NUM_8812)
			priv->pshare->thermal_value_avg_index_8812 = 0;

		for (i = 0; i < AVG_THERMAL_NUM_8812; i++) {
			if (priv->pshare->thermal_value_avg_8812[i]) {
				thermal_value_avg += priv->pshare->thermal_value_avg_8812[i];
				thermal_value_avg_count++;
			}
		}

		if (thermal_value_avg_count) {
			thermal_value = (unsigned char)(thermal_value_avg / thermal_value_avg_count);
			/* printk("AVG Thermal Meter = 0x%x\n", thermal_value); */
		}
	}


	/* 4 If necessary,  do power tracking */

	if (!priv->pmib->dot11RFEntry.ther) /*Don't do power tracking since no calibrated thermal value*/
		return;

	if (thermal_value != priv->pshare->thermal_value) {
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\n******** START POWER TRACKING ********\n"));
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", thermal_value, priv->pshare->thermal_value, priv->pmib->dot11RFEntry.ther));
		delta = RTL_ABS(thermal_value, priv->pmib->dot11RFEntry.ther);
		delta_LCK = RTL_ABS(thermal_value, priv->pshare->thermal_value_lck);
		is_decrease = ((thermal_value < priv->pmib->dot11RFEntry.ther) ? 1 : 0);
		/* if (*p_dm->p_band_type == ODM_BAND_5G) */
		{
#ifdef _TRACKING_TABLE_FILE
			if (priv->pshare->rf_ft_var.pwr_track_file) {
				for (rf_path = 0; rf_path < max_rf_path; rf_path++) {
					ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("diff: (%s)%d ==> get index from table : %d)\n", (is_decrease ? "-" : "+"), delta, get_tx_tracking_index(priv, channel, rf_path, delta, is_decrease, 0)));
					if (is_decrease) {
						OFDM_index[rf_path] = priv->pshare->OFDM_index0[rf_path] + get_tx_tracking_index(priv, channel, rf_path, delta, is_decrease, 0);
						OFDM_index[rf_path] = ((OFDM_index[rf_path] > (OFDM_TABLE_SIZE_8812 - 1)) ? (OFDM_TABLE_SIZE_8812 - 1) : OFDM_index[rf_path]);
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> decrese power ---> new OFDM_INDEX:%d (%d + %d)\n", OFDM_index[rf_path], priv->pshare->OFDM_index0[rf_path], get_tx_tracking_index(priv, channel, rf_path, delta, is_decrease, 0)));
#if 0/* RTL8881A_SUPPORT */
						if (p_dm->support_ic_type == ODM_RTL8881A) {
							if (priv->pshare->rf_ft_var.pwrtrk_tx_agc_enable) {
								if (priv->pshare->add_tx_agc) { /* tx_agc has been added */
									add_tx_power88xx_ac(priv, 0);
									priv->pshare->add_tx_agc = 0;
									priv->pshare->add_tx_agc_index = 0;
								}
							}
						}
#endif
					} else {

						OFDM_index[rf_path] = priv->pshare->OFDM_index0[rf_path] - get_tx_tracking_index(priv, channel, rf_path, delta, is_decrease, 0);
#if 0/* RTL8881A_SUPPORT */
						if (p_dm->support_ic_type == ODM_RTL8881A) {
							if (priv->pshare->rf_ft_var.pwrtrk_tx_agc_enable) {
								if (OFDM_index[i] < OFDM_min_index) {
									priv->pshare->add_tx_agc_index = (OFDM_min_index - OFDM_index[i]) / 2; /* Calculate Remnant tx_agc value,  2 index for 1 tx_agc */
									add_tx_power88xx_ac(priv, priv->pshare->add_tx_agc_index);
									priv->pshare->add_tx_agc = 1;     /* add_tx_agc Flag = 1 */
									OFDM_index[i] = OFDM_min_index;
								} else {
									if (priv->pshare->add_tx_agc) { /* tx_agc been added */
										priv->pshare->add_tx_agc = 0;
										priv->pshare->add_tx_agc_index = 0;
										add_tx_power88xx_ac(priv, 0); /* minus the added TPI */
									}
								}
							}
						}
#else
						OFDM_index[rf_path] = ((OFDM_index[rf_path] < OFDM_min_index) ?  OFDM_min_index : OFDM_index[rf_path]);
#endif
						ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> increse power ---> new OFDM_INDEX:%d (%d - %d)\n", OFDM_index[rf_path], priv->pshare->OFDM_index0[rf_path], get_tx_tracking_index(priv, channel, rf_path, delta, is_decrease, 0)));
					}
				}
			}
#endif
			/* 4 Set new BB swing index */
			for (rf_path = 0; rf_path < max_rf_path; rf_path++) {
				phy_set_bb_reg(priv, bb_swing_reg[rf_path], 0xffe00000, ofdm_swing_table_8812[(unsigned int)OFDM_index[rf_path]]);
				ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("Readback 0x%x[31:21] = 0x%x, OFDM_index:%d\n", bb_swing_reg[rf_path], phy_query_bb_reg(priv, bb_swing_reg[rf_path], 0xffe00000), OFDM_index[rf_path]));
			}

		}
		if ((delta_LCK > 8) && (!p_iqk_info->rfk_forbidden)) {
			RTL_W8(0x522, 0xff);
			reg0x18 = phy_query_rf_reg(priv, RF_PATH_A, 0x18, MASK20BITS, 1);
			phy_set_rf_reg(priv, RF_PATH_A, 0xB4, BIT(14), 1);
			phy_set_rf_reg(priv, RF_PATH_A, 0x18, BIT(15), 1);
			delay_ms(200); /* frequency deviation */
			phy_set_rf_reg(priv, RF_PATH_A, 0xB4, BIT(14), 0);
			phy_set_rf_reg(priv, RF_PATH_A, 0x18, MASK20BITS, reg0x18);
#ifdef CONFIG_RTL_8812_SUPPORT
			if (GET_CHIP_VER(priv) == VERSION_8812E)
				update_bbrf_val8812(priv, priv->pmib->dot11RFEntry.dot11channel);
#endif
			RTL_W8(0x522, 0x0);
			priv->pshare->thermal_value_lck = thermal_value;
		}
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\n******** END:%s() ********\n", __FUNCTION__));

		/* update thermal meter value */
		priv->pshare->thermal_value = thermal_value;
		for (rf_path = 0; rf_path < max_rf_path; rf_path++)
			priv->pshare->OFDM_index[rf_path] = OFDM_index[rf_path];
	}
}

#endif


void
odm_txpowertracking_callback_thermal_meter(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm->rf_calibrate_info);
	struct _IQK_INFORMATION	*p_iqk_info = &p_dm->IQK_info;

#if (RTL8197F_SUPPORT == 1 || RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8197F || p_dm->support_ic_type == ODM_RTL8822B
		|| p_dm->support_ic_type == ODM_RTL8821C) {
		odm_txpowertracking_callback_thermal_meter_jaguar_series3(p_dm);
		return;
	}
#endif
#if (RTL8814A_SUPPORT == 1)		/*use this function to do power tracking after 8814 by YuChen*/
	if (p_dm->support_ic_type & ODM_RTL8814A) {
		odm_txpowertracking_callback_thermal_meter_jaguar_series2(p_dm);
		return;
	}
#endif
#if (RTL8881A_SUPPORT || RTL8812A_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8812 || p_dm->support_ic_type & ODM_RTL8881A) {
		odm_txpowertracking_callback_thermal_meter_jaguar_series(p_dm);
		return;
	}
#endif

#if (RTL8192E_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8192E) {
		odm_txpowertracking_callback_thermal_meter_92e(p_dm);
		return;
	}
#endif

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	/* PMGNT_INFO      		p_mgnt_info = &adapter->mgnt_info; */
#endif


	u8			thermal_value = 0, delta, delta_LCK, delta_IQK, offset;
	u8			thermal_value_avg_count = 0;
	u32			thermal_value_avg = 0;
	/*	s32			ele_A=0, ele_D, TempCCk, X, value32;
	 *	s32			Y, ele_C=0;
	 *	s8			OFDM_index[2], CCK_index=0, OFDM_index_old[2]={0,0}, CCK_index_old=0, index;
	 *	s8			deltaPowerIndex = 0; */
	u32			i = 0;/* , j = 0; */
	boolean		is2T = false;
	/*	bool 		bInteralPA = false; */

	u8			OFDM_max_index = 34, rf = (is2T) ? 2 : 1; /* OFDM BB Swing should be less than +3.0dB, which is required by Arthur */
	u8			indexforchannel = 0;/*get_right_chnl_place_for_iqk(p_hal_data->current_channel)*/
	enum            _POWER_DEC_INC { POWER_DEC, POWER_INC };

	struct _TXPWRTRACK_CFG	c;


	/* 4 1. The following TWO tables decide the final index of OFDM/CCK swing table. */
	s8			delta_swing_table_idx[2][index_mapping_NUM_88E] = {
		/* {{Power decreasing(lower temperature)}, {Power increasing(higher temperature)}} */
		{0, 0, 2, 3, 4, 4, 5, 6, 7, 7, 8, 9, 10, 10, 11}, {0, 0, 1, 2, 3, 4, 4, 4, 4, 5, 7, 8, 9, 9, 10}
	};
	u8			thermal_threshold[2][index_mapping_NUM_88E] = {
		/* {{Power decreasing(lower temperature)}, {Power increasing(higher temperature)}} */
		{0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 27}, {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 25, 25, 25}
	};

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct rtl8192cd_priv	*priv = p_dm->priv;
#endif

	/* 4 2. Initilization ( 7 steps in total ) */

	configure_txpower_track(p_dm, &c);

	p_dm->rf_calibrate_info.txpowertracking_callback_cnt++; /* cosa add for debug */
	p_dm->rf_calibrate_info.is_txpowertracking_init = true;

#if (MP_DRIVER == 1)
	p_dm->rf_calibrate_info.txpowertrack_control = p_hal_data->txpowertrack_control; /* <Kordan> We should keep updating the control variable according to HalData.
     * <Kordan> rf_calibrate_info.rega24 will be initialized when ODM HW configuring, but MP configures with para files. */
	p_dm->rf_calibrate_info.rega24 = 0x090e1317;
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_AP) && defined(MP_TEST)
	if ((OPMODE & WIFI_MP_STATE) || *(p_dm->p_mp_mode)) {
		if (p_dm->priv->pshare->mp_txpwr_tracking == false)
			return;
	}
#endif
	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("===>odm_txpowertracking_callback_thermal_meter_8188e, p_dm->bb_swing_idx_cck_base: %d, p_dm->bb_swing_idx_ofdm_base: %d\n", p_rf_calibrate_info->bb_swing_idx_cck_base, p_rf_calibrate_info->bb_swing_idx_ofdm_base));
	/*
		if (!p_dm->rf_calibrate_info.tm_trigger) {
			odm_set_rf_reg(p_dm, RF_PATH_A, c.thermal_reg_addr, BIT(17) | BIT(16), 0x3);
			p_dm->rf_calibrate_info.tm_trigger = 1;
			return;
		}
	*/
	thermal_value = (u8)odm_get_rf_reg(p_dm, RF_PATH_A, c.thermal_reg_addr, 0xfc00);	/* 0x42: RF Reg[15:10] 88E */
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	if (!thermal_value || !p_dm->rf_calibrate_info.txpowertrack_control)
#else
	if (!p_dm->rf_calibrate_info.txpowertrack_control)
#endif
		return;

	/* 4 3. Initialize ThermalValues of rf_calibrate_info */

	if (!p_dm->rf_calibrate_info.thermal_value) {
		p_dm->rf_calibrate_info.thermal_value_lck = thermal_value;
		p_dm->rf_calibrate_info.thermal_value_iqk = thermal_value;
	}

	if (p_dm->rf_calibrate_info.is_reloadtxpowerindex)
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("reload ofdm index for band switch\n"));

	/* 4 4. Calculate average thermal meter */

	p_dm->rf_calibrate_info.thermal_value_avg[p_dm->rf_calibrate_info.thermal_value_avg_index] = thermal_value;
	p_dm->rf_calibrate_info.thermal_value_avg_index++;
	if (p_dm->rf_calibrate_info.thermal_value_avg_index == c.average_thermal_num)
		p_dm->rf_calibrate_info.thermal_value_avg_index = 0;

	for (i = 0; i < c.average_thermal_num; i++) {
		if (p_dm->rf_calibrate_info.thermal_value_avg[i]) {
			thermal_value_avg += p_dm->rf_calibrate_info.thermal_value_avg[i];
			thermal_value_avg_count++;
		}
	}

	if (thermal_value_avg_count) {
		/* Give the new thermo value a weighting */
		thermal_value_avg += (thermal_value * 4);

		thermal_value = (u8)(thermal_value_avg / (thermal_value_avg_count + 4));
		p_rf_calibrate_info->thermal_value_delta = thermal_value - priv->pmib->dot11RFEntry.ther;
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("AVG Thermal Meter = 0x%x\n", thermal_value));
	}

	/* 4 5. Calculate delta, delta_LCK, delta_IQK. */

	delta	  = (thermal_value > p_dm->rf_calibrate_info.thermal_value) ? (thermal_value - p_dm->rf_calibrate_info.thermal_value) : (p_dm->rf_calibrate_info.thermal_value - thermal_value);
	delta_LCK = (thermal_value > p_dm->rf_calibrate_info.thermal_value_lck) ? (thermal_value - p_dm->rf_calibrate_info.thermal_value_lck) : (p_dm->rf_calibrate_info.thermal_value_lck - thermal_value);
	delta_IQK = (thermal_value > p_dm->rf_calibrate_info.thermal_value_iqk) ? (thermal_value - p_dm->rf_calibrate_info.thermal_value_iqk) : (p_dm->rf_calibrate_info.thermal_value_iqk - thermal_value);

	/* 4 6. If necessary, do LCK. */
	if (!(p_dm->support_ic_type & ODM_RTL8821)) {
		/*if((delta_LCK > p_hal_data->delta_lck) && (p_hal_data->delta_lck != 0))*/
		if ((delta_LCK >= c.threshold_iqk) && (!p_iqk_info->rfk_forbidden)) {
			/*Delta temperature is equal to or larger than 20 centigrade.*/
			p_dm->rf_calibrate_info.thermal_value_lck = thermal_value;
			(*c.phy_lc_calibrate)(p_dm);
		}
	}

	/* 3 7. If necessary, move the index of swing table to adjust Tx power. */

	if (delta > 0 && p_dm->rf_calibrate_info.txpowertrack_control) {

		delta = (thermal_value > p_dm->priv->pmib->dot11RFEntry.ther) ? (thermal_value - p_dm->priv->pmib->dot11RFEntry.ther) : (p_dm->priv->pmib->dot11RFEntry.ther - thermal_value);

		/* 4 7.1 The Final Power index = BaseIndex + power_index_offset */

		if (thermal_value > p_dm->priv->pmib->dot11RFEntry.ther) {
			CALCULATE_SWINGTALBE_OFFSET(offset, POWER_INC, index_mapping_NUM_88E, delta);
			p_dm->rf_calibrate_info.delta_power_index_last = p_dm->rf_calibrate_info.delta_power_index;
			p_dm->rf_calibrate_info.delta_power_index =  delta_swing_table_idx[POWER_INC][offset];

		} else {

			CALCULATE_SWINGTALBE_OFFSET(offset, POWER_DEC, index_mapping_NUM_88E, delta);
			p_dm->rf_calibrate_info.delta_power_index_last = p_dm->rf_calibrate_info.delta_power_index;
			p_dm->rf_calibrate_info.delta_power_index = (-1) * delta_swing_table_idx[POWER_DEC][offset];
		}

		if (p_dm->rf_calibrate_info.delta_power_index == p_dm->rf_calibrate_info.delta_power_index_last)
			p_dm->rf_calibrate_info.power_index_offset = 0;
		else
			p_dm->rf_calibrate_info.power_index_offset = p_dm->rf_calibrate_info.delta_power_index - p_dm->rf_calibrate_info.delta_power_index_last;

		for (i = 0; i < rf; i++)
			p_dm->rf_calibrate_info.OFDM_index[i] = p_rf_calibrate_info->bb_swing_idx_ofdm_base + p_dm->rf_calibrate_info.power_index_offset;
		p_dm->rf_calibrate_info.CCK_index = p_rf_calibrate_info->bb_swing_idx_cck_base + p_dm->rf_calibrate_info.power_index_offset;

		p_rf_calibrate_info->bb_swing_idx_cck = p_dm->rf_calibrate_info.CCK_index;
		p_rf_calibrate_info->bb_swing_idx_ofdm[RF_PATH_A] = p_dm->rf_calibrate_info.OFDM_index[RF_PATH_A];

		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("The 'CCK' final index(%d) = BaseIndex(%d) + power_index_offset(%d)\n", p_rf_calibrate_info->bb_swing_idx_cck, p_rf_calibrate_info->bb_swing_idx_cck_base, p_dm->rf_calibrate_info.power_index_offset));
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("The 'OFDM' final index(%d) = BaseIndex(%d) + power_index_offset(%d)\n", p_rf_calibrate_info->bb_swing_idx_ofdm[RF_PATH_A], p_rf_calibrate_info->bb_swing_idx_ofdm_base, p_dm->rf_calibrate_info.power_index_offset));

		/* 4 7.1 Handle boundary conditions of index. */


		for (i = 0; i < rf; i++) {
			if (p_dm->rf_calibrate_info.OFDM_index[i] > OFDM_max_index)
				p_dm->rf_calibrate_info.OFDM_index[i] = OFDM_max_index;
			else if (p_dm->rf_calibrate_info.OFDM_index[i] < 0)
				p_dm->rf_calibrate_info.OFDM_index[i] = 0;
		}

		if (p_dm->rf_calibrate_info.CCK_index > c.swing_table_size_cck - 1)
			p_dm->rf_calibrate_info.CCK_index = c.swing_table_size_cck - 1;
		else if (p_dm->rf_calibrate_info.CCK_index < 0)
			p_dm->rf_calibrate_info.CCK_index = 0;
	} else {
		ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("The thermal meter is unchanged or TxPowerTracking OFF: thermal_value: %d, p_dm->rf_calibrate_info.thermal_value: %d)\n", thermal_value, p_dm->rf_calibrate_info.thermal_value));
		p_dm->rf_calibrate_info.power_index_offset = 0;
	}
	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("TxPowerTracking: [CCK] Swing Current index: %d, Swing base index: %d\n", p_dm->rf_calibrate_info.CCK_index, p_rf_calibrate_info->bb_swing_idx_cck_base));

	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("TxPowerTracking: [OFDM] Swing Current index: %d, Swing base index: %d\n", p_dm->rf_calibrate_info.OFDM_index[RF_PATH_A], p_rf_calibrate_info->bb_swing_idx_ofdm_base));

	if (p_dm->rf_calibrate_info.power_index_offset != 0 && p_dm->rf_calibrate_info.txpowertrack_control) {
		/* 4 7.2 Configure the Swing Table to adjust Tx Power. */

		p_dm->rf_calibrate_info.is_tx_power_changed = true; /* Always true after Tx Power is adjusted by power tracking. */
		/*  */
		/* 2012/04/23 MH According to Luke's suggestion, we can not write BB digital */
		/* to increase TX power. Otherwise, EVM will be bad. */
		/*  */
		/* 2012/04/25 MH Add for tx power tracking to set tx power in tx agc for 88E. */
		if (thermal_value > p_dm->rf_calibrate_info.thermal_value) {
			/* ODM_RT_TRACE(p_dm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, */
			/*	("Temperature Increasing: delta_pi: %d, delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n", */
			/*	p_dm->rf_calibrate_info.power_index_offset, delta, thermal_value, p_hal_data->eeprom_thermal_meter, p_dm->rf_calibrate_info.thermal_value)); */
		} else if (thermal_value < p_dm->rf_calibrate_info.thermal_value) { /* Low temperature */
			/* ODM_RT_TRACE(p_dm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, */
			/*	("Temperature Decreasing: delta_pi: %d, delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n", */
			/*		p_dm->rf_calibrate_info.power_index_offset, delta, thermal_value, p_hal_data->eeprom_thermal_meter, p_dm->rf_calibrate_info.thermal_value)); */
		}
		if (thermal_value > p_dm->priv->pmib->dot11RFEntry.ther)
		{
			/*				ODM_RT_TRACE(p_dm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("Temperature(%d) hugher than PG value(%d), increases the power by tx_agc\n", thermal_value, p_hal_data->eeprom_thermal_meter)); */
			(*c.odm_tx_pwr_track_set_pwr)(p_dm, TXAGC, 0, 0);
		} else {
			/*			ODM_RT_TRACE(p_dm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("Temperature(%d) lower than PG value(%d), increases the power by tx_agc\n", thermal_value, p_hal_data->eeprom_thermal_meter)); */
			(*c.odm_tx_pwr_track_set_pwr)(p_dm, BBSWING, RF_PATH_A, indexforchannel);
			if (is2T)
				(*c.odm_tx_pwr_track_set_pwr)(p_dm, BBSWING, RF_PATH_B, indexforchannel);
		}

		p_rf_calibrate_info->bb_swing_idx_cck_base = p_rf_calibrate_info->bb_swing_idx_cck;
		p_rf_calibrate_info->bb_swing_idx_ofdm_base = p_rf_calibrate_info->bb_swing_idx_ofdm[RF_PATH_A];
		p_dm->rf_calibrate_info.thermal_value = thermal_value;

	}

	ODM_RT_TRACE(p_dm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("<===dm_TXPowerTrackingCallback_ThermalMeter_8188E\n"));

	p_dm->rf_calibrate_info.tx_powercount = 0;
}

/* 3============================================================
 * 3 IQ Calibration
 * 3============================================================ */

void
odm_reset_iqk_result(
	void		*p_dm_void
)
{
	return;
}
#if 1/* !(DM_ODM_SUPPORT_TYPE & ODM_AP) */
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
	struct _IQK_INFORMATION	*p_iqk_info = &p_dm->IQK_info;

	if ((p_dm->is_linked) && (!p_iqk_info->rfk_forbidden)) {
		if ((*p_dm->p_channel != p_dm->pre_channel) && (!*p_dm->p_is_scan_in_process)) {
			p_dm->pre_channel = *p_dm->p_channel;
			p_dm->linked_interval = 0;
		}

		if (p_dm->linked_interval < 3)
			p_dm->linked_interval++;

		if (p_dm->linked_interval == 2)
			halrf_iqk_trigger(p_dm, false);
	} else
		p_dm->linked_interval = 0;

}

void phydm_rf_init(void		*p_dm_void)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	odm_txpowertracking_init(p_dm);

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
#if (RTL8814A_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8814A)
		phy_iq_calibrate_8814a_init(p_dm);
#endif
#endif

}

void phydm_rf_watchdog(void		*p_dm_void)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	odm_txpowertracking_check(p_dm);
	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES)
		odm_iq_calibrate(p_dm);
#endif
}
