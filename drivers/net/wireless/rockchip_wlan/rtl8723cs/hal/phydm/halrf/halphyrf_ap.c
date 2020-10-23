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

void odm_clear_txpowertracking_state(	
	void *dm_void
)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &(dm->rf_calibrate_info);
	struct rtl8192cd_priv *priv = dm->priv;

	u8 i;
	
	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "===>%s\n", __func__);

	for (i = 0; i < MAX_RF_PATH; i++) {
		cali_info->absolute_ofdm_swing_idx[i] = 0;
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "cali_info->absolute_ofdm_swing_idx[%d]=%d\n",
			i, cali_info->absolute_ofdm_swing_idx[i]);
	}

 	dm->rf_calibrate_info.thermal_value = 0;
	dm->rf_calibrate_info.thermal_value_lck = 0;
	dm->rf_calibrate_info.thermal_value_iqk = 0;
}

void configure_txpower_track(
	void		*dm_void,
	struct txpwrtrack_cfg	*config
)
{
	struct dm_struct		*dm = (struct dm_struct *)dm_void;
#if RTL8812A_SUPPORT
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	/* if (IS_HARDWARE_TYPE_8812(dm->adapter)) */
	if (dm->support_ic_type == ODM_RTL8812)
		configure_txpower_track_8812a(config);
	/* else */
#endif
#endif

#if RTL8814A_SUPPORT
	if (dm->support_ic_type == ODM_RTL8814A)
		configure_txpower_track_8814a(config);
#endif


#if RTL8188E_SUPPORT
	if (dm->support_ic_type == ODM_RTL8188E)
		configure_txpower_track_8188e(config);
#endif

#if RTL8197F_SUPPORT
	if (dm->support_ic_type == ODM_RTL8197F)
		configure_txpower_track_8197f(config);
#endif

#if RTL8822B_SUPPORT
	if (dm->support_ic_type == ODM_RTL8822B)
		configure_txpower_track_8822b(config);
#endif

#if RTL8192F_SUPPORT
	if (dm->support_ic_type == ODM_RTL8192F)
		configure_txpower_track_8192f(config);
#endif

#if RTL8198F_SUPPORT
	if (dm->support_ic_type == ODM_RTL8198F)
		configure_txpower_track_8198f(config);
#endif

#if RTL8814B_SUPPORT
	if (dm->support_ic_type == ODM_RTL8814B)
		configure_txpower_track_8814b(config);
#endif

#if RTL8812F_SUPPORT
	if (dm->support_ic_type == ODM_RTL8812F)
		configure_txpower_track_8812f(config);
#endif

#if RTL8197G_SUPPORT
	if (dm->support_ic_type == ODM_RTL8197G)
		configure_txpower_track_8197g(config);
#endif

}

#if (RTL8192E_SUPPORT == 1)
void
odm_txpowertracking_callback_thermal_meter_92e(
	void		*dm_void
)
{
	struct dm_struct		*dm = (struct dm_struct *)dm_void;
	struct dm_iqk_info	*iqk_info = &dm->IQK_info;
	u8	thermal_value = 0, delta, delta_IQK, delta_LCK, channel, is_decrease, rf_mimo_mode;
	u8	thermal_value_avg_count = 0;
	u8     OFDM_min_index = 10; /* OFDM BB Swing should be less than +2.5dB, which is required by Arthur */
	s8	OFDM_index[2], index ;
	u32	thermal_value_avg = 0, reg0x18;
	u32	i = 0, j = 0, rf;
	s32	value32, CCK_index = 0, ele_A, ele_D, ele_C, X, Y;
	struct rtl8192cd_priv	*priv = dm->priv;

	rf_mimo_mode = dm->rf_type;
	/* RF_DBG(dm,DBG_RF_TX_PWR_TRACK,"%s:%d rf_mimo_mode:%d\n", __FUNCTION__, __LINE__, rf_mimo_mode); */

#ifdef MP_TEST
	if ((OPMODE & WIFI_MP_STATE) || *(dm->mp_mode)) {
		channel = priv->pshare->working_channel;
		if (priv->pshare->mp_txpwr_tracking == false)
			return;
	} else
#endif
	{
		channel = (priv->pmib->dot11RFEntry.dot11channel);
	}

	thermal_value = (unsigned char)odm_get_rf_reg(dm, RF_PATH_A, ODM_RF_T_METER_92E, 0xfc00);	/* 0x42: RF Reg[15:10] 88E */
	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", thermal_value, priv->pshare->thermal_value, priv->pmib->dot11RFEntry.ther);


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
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "PathA 0xC80[31:22] = 0x%x, OFDM_index=%d\n", ele_D, OFDM_index[0]);
			break;
		}
	}

	/* Query OFDM path B default setting */
	if (rf_mimo_mode == RF_2T2R) {
		ele_D = phy_query_bb_reg(priv, REG_OFDM_0_XB_TX_IQ_IMBALANCE, MASKOFDM_D);
		for (i = 0; i < OFDM_TABLE_SIZE_92E; i++) {
			if (ele_D == (ofdm_swing_table_92e[i] >> 22)) {
				OFDM_index[1] = (unsigned char)i;
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "PathB 0xC88[31:22] = 0x%x, OFDM_index=%d\n", ele_D, OFDM_index[1]);
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
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "AVG Thermal Meter = 0x%x\n", thermal_value);
		}
	}

	/* Initialize */
	if (!priv->pshare->thermal_value) {
		priv->pshare->thermal_value = priv->pmib->dot11RFEntry.ther;
		priv->pshare->thermal_value_iqk = thermal_value;
		priv->pshare->thermal_value_lck = thermal_value;
	}

	if (thermal_value != priv->pshare->thermal_value) {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "\n******** START POWER TRACKING ********\n");
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", thermal_value, priv->pshare->thermal_value, priv->pmib->dot11RFEntry.ther);

		delta = RTL_ABS(thermal_value, priv->pmib->dot11RFEntry.ther);
		delta_IQK = RTL_ABS(thermal_value, priv->pshare->thermal_value_iqk);
		delta_LCK = RTL_ABS(thermal_value, priv->pshare->thermal_value_lck);
		is_decrease = ((thermal_value < priv->pmib->dot11RFEntry.ther) ? 1 : 0);

#ifdef _TRACKING_TABLE_FILE
		if (priv->pshare->rf_ft_var.pwr_track_file) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "diff: (%s)%d ==> get index from table : %d)\n", (is_decrease ? "-" : "+"), delta, get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0));

			if (is_decrease) {
				for (i = 0; i < rf; i++) {
					OFDM_index[i] = priv->pshare->OFDM_index0[i] + get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0);
					OFDM_index[i] = ((OFDM_index[i] > (OFDM_TABLE_SIZE_92E- 1)) ? (OFDM_TABLE_SIZE_92E - 1) : OFDM_index[i]);
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK, ">>> decrese power ---> new OFDM_INDEX:%d (%d + %d)\n", OFDM_index[i], priv->pshare->OFDM_index0[i], get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0));
					CCK_index = priv->pshare->CCK_index0 + get_tx_tracking_index(priv, channel, i, delta, is_decrease, 1);
					CCK_index = ((CCK_index > (CCK_TABLE_SIZE_92E - 1)) ? (CCK_TABLE_SIZE_92E - 1) : CCK_index);
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK, ">>> Decrese power ---> new CCK_INDEX:%d (%d + %d)\n",  CCK_index, priv->pshare->CCK_index0, get_tx_tracking_index(priv, channel, i, delta, is_decrease, 1));
				}
			} else {
				for (i = 0; i < rf; i++) {
					OFDM_index[i] = priv->pshare->OFDM_index0[i] - get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0);
					OFDM_index[i] = ((OFDM_index[i] < OFDM_min_index) ?  OFDM_min_index : OFDM_index[i]);
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK, ">>> Increse power ---> new OFDM_INDEX:%d (%d - %d)\n", OFDM_index[i], priv->pshare->OFDM_index0[i], get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0));
					CCK_index = priv->pshare->CCK_index0 - get_tx_tracking_index(priv, channel, i, delta, is_decrease, 1);
					CCK_index = ((CCK_index < 0) ? 0 : CCK_index);
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK, ">>> Increse power ---> new CCK_INDEX:%d (%d - %d)\n", CCK_index, priv->pshare->CCK_index0, get_tx_tracking_index(priv, channel, i, delta, is_decrease, 1));
				}
			}
		}
#endif /* CFG_TRACKING_TABLE_FILE */

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "ofdm_swing_table_92e[(unsigned int)OFDM_index[0]] = %x\n", ofdm_swing_table_92e[(unsigned int)OFDM_index[0]]);
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "ofdm_swing_table_92e[(unsigned int)OFDM_index[1]] = %x\n", ofdm_swing_table_92e[(unsigned int)OFDM_index[1]]);

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

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "0xc80 = 0x%x\n", phy_query_bb_reg(priv, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD));
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "0xc88 = 0x%x\n", phy_query_bb_reg(priv, REG_OFDM_0_XB_TX_IQ_IMBALANCE, MASKDWORD));

		if ((delta_IQK > 3) && (!iqk_info->rfk_forbidden)) {
			priv->pshare->thermal_value_iqk = thermal_value;
#ifdef MP_TEST
#endif			if (!(*(dm->mp_mode) && (OPMODE & (WIFI_MP_CTX_BACKGROUND | WIFI_MP_CTX_PACKET))))

				halrf_iqk_trigger(dm, false);
		}

		if ((delta_LCK > 8)  && (!iqk_info->rfk_forbidden)) {
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

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "\n******** END:%s() ********\n", __FUNCTION__);
}
#endif

#if (RTL8814B_SUPPORT == 1 || RTL8812F_SUPPORT == 1 || RTL8822C_SUPPORT == 1 || RTL8197G_SUPPORT == 1)
void
odm_txpowertracking_callback_thermal_meter_jaguar_series4(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct dm_rf_calibration_struct *cali_info = &(dm->rf_calibrate_info);
 	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	struct _hal_rf_ *rf = &dm->rf_table;
	struct _halrf_tssi_data *tssi = &rf->halrf_tssi_data;
	struct rtl8192cd_priv *priv = dm->priv;
	struct txpwrtrack_cfg c;

	if (!(rf->rf_supportability & HAL_RF_TX_PWR_TRACK))
		return;

	u8 thermal_value[MAX_RF_PATH] = {0}, delta[MAX_RF_PATH] = {0};
	u8 delta_swing_table_idx_tup[DELTA_SWINGIDX_SIZE] = {0};
	u8 delta_swing_table_idx_tdown[DELTA_SWINGIDX_SIZE] = {0};
	u8 delta_LCK = 0, delta_IQK = 0, i = 0, j = 0, p;
	u8 thermal_value_avg_count[MAX_RF_PATH] = {0};
	u32 thermal_value_avg[MAX_RF_PATH] = {0};
	s8 thermal_value_temp[MAX_RF_PATH] = {0};

	u8 *pwrtrk_tab_up_a = NULL;
	u8 *pwrtrk_tab_down_a = NULL;
	u8 *pwrtrk_tab_up_b = NULL;
	u8 *pwrtrk_tab_down_b = NULL;
	u8 *pwrtrk_tab_up_c = NULL;
	u8 *pwrtrk_tab_down_c = NULL;
	u8 *pwrtrk_tab_up_d = NULL;
	u8 *pwrtrk_tab_down_d = NULL;
	u8 tracking_method = MIX_MODE;

	configure_txpower_track(dm, &c);

	(*c.get_delta_swing_table)(dm,
		(u8 **)&pwrtrk_tab_up_a, (u8 **)&pwrtrk_tab_down_a,
		(u8 **)&pwrtrk_tab_up_b, (u8 **)&pwrtrk_tab_down_b);

	if (GET_CHIP_VER(priv) == VERSION_8814B) {
		(*c.get_delta_swing_table8814only)(dm,
			(u8 **)&pwrtrk_tab_up_c, (u8 **)&pwrtrk_tab_down_c,
			(u8 **)&pwrtrk_tab_up_d, (u8 **)&pwrtrk_tab_down_d);
	}

	cali_info->txpowertracking_callback_cnt++;
	cali_info->is_txpowertracking_init = true;

	/* Initialize */
	if (!dm->rf_calibrate_info.thermal_value)
		dm->rf_calibrate_info.thermal_value =
			priv->pmib->dot11RFEntry.thermal[RF_PATH_A];

	if (!dm->rf_calibrate_info.thermal_value_lck)
		dm->rf_calibrate_info.thermal_value_lck =
			priv->pmib->dot11RFEntry.thermal[RF_PATH_A];

	if (!dm->rf_calibrate_info.thermal_value_iqk)
		dm->rf_calibrate_info.thermal_value_iqk =
			priv->pmib->dot11RFEntry.thermal[RF_PATH_A];

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		"===>odm_txpowertracking_callback_thermal_meter\n cali_info->bb_swing_idx_cck_base: %d, cali_info->bb_swing_idx_ofdm_base[A]: %d, cali_info->default_ofdm_index: %d\n",
		cali_info->bb_swing_idx_cck_base, cali_info->bb_swing_idx_ofdm_base_path[RF_PATH_A], cali_info->default_ofdm_index);

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		"cali_info->txpowertrack_control=%d\n", cali_info->txpowertrack_control);

	for (i = 0; i < c.rf_path_count; i++) {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
			"PGthermal[%d]=0x%x(%d)\n", i, 
			priv->pmib->dot11RFEntry.thermal[i],
			priv->pmib->dot11RFEntry.thermal[i]);

		if (priv->pmib->dot11RFEntry.thermal[i] == 0xff ||
			priv->pmib->dot11RFEntry.thermal[i] == 0x0)
			return;
	}
	if (dm->support_ic_type & (ODM_RTL8822C | ODM_RTL8812F)) {
		for (i = 0; i < c.rf_path_count; i++)
			thermal_value[i] = (u8)odm_get_rf_reg(dm, i, c.thermal_reg_addr, 0x7e); /* 0x42: RF Reg[6:1] Thermal Trim*/
	} else if (dm->support_ic_type == ODM_RTL8197G) {
		for (i = 0; i < c.rf_path_count; i++)
			thermal_value[i] = (u8)odm_get_rf_reg(dm, i, RF_0xf6, 0x7E000);
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
				"PGthermal[%d] = 0x%x(%d),   AVG Thermal Meter = 0x%x(%d)\n", j,
				priv->pmib->dot11RFEntry.thermal[j],
				priv->pmib->dot11RFEntry.thermal[j],
				thermal_value[j],
				thermal_value[j]);
		}
		/* 4 5. Calculate delta, delta_LCK, delta_IQK. */

		/* "delta" here is used to determine whether thermal value changes or not. */
		delta[j] = RTL_ABS(thermal_value[j], priv->pmib->dot11RFEntry.thermal[j]);
		delta_LCK = RTL_ABS(thermal_value[RF_PATH_A], dm->rf_calibrate_info.thermal_value_lck);
		delta_IQK = RTL_ABS(thermal_value[RF_PATH_A], dm->rf_calibrate_info.thermal_value_iqk);
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
#ifdef _TRACKING_TABLE_FILE
	for (i = 0; i < c.rf_path_count; i++) {
		if (i == RF_PATH_B) {
			odm_move_memory(dm, delta_swing_table_idx_tup, pwrtrk_tab_up_b, DELTA_SWINGIDX_SIZE);
			odm_move_memory(dm, delta_swing_table_idx_tdown, pwrtrk_tab_down_b, DELTA_SWINGIDX_SIZE);
		} else if (i == RF_PATH_C) {
			odm_move_memory(dm, delta_swing_table_idx_tup, pwrtrk_tab_up_c, DELTA_SWINGIDX_SIZE);
			odm_move_memory(dm, delta_swing_table_idx_tdown, pwrtrk_tab_down_c, DELTA_SWINGIDX_SIZE);
		} else if (i == RF_PATH_D) {
			odm_move_memory(dm, delta_swing_table_idx_tup, pwrtrk_tab_up_d, DELTA_SWINGIDX_SIZE);
			odm_move_memory(dm, delta_swing_table_idx_tdown, pwrtrk_tab_down_d, DELTA_SWINGIDX_SIZE);
		} else {
			odm_move_memory(dm, delta_swing_table_idx_tup, pwrtrk_tab_up_a, DELTA_SWINGIDX_SIZE);
			odm_move_memory(dm, delta_swing_table_idx_tdown, pwrtrk_tab_down_a, DELTA_SWINGIDX_SIZE);
		}

		cali_info->delta_power_index_last_path[i] = cali_info->delta_power_index_path[i];	/*recording poer index offset*/
		delta[i] = thermal_value[i] > priv->pmib->dot11RFEntry.thermal[i] ? (thermal_value[i] - priv->pmib->dot11RFEntry.thermal[i]) : (priv->pmib->dot11RFEntry.thermal[i] - thermal_value[i]);
				
		if (delta[i] >= TXPWR_TRACK_TABLE_SIZE)
			delta[i] = TXPWR_TRACK_TABLE_SIZE - 1;

		if (thermal_value[i] > priv->pmib->dot11RFEntry.thermal[i]) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				"delta_swing_table_idx_tup[%d]=%d Path=%d\n", delta[i], delta_swing_table_idx_tup[delta[i]], i);
				
			cali_info->delta_power_index_path[i] = delta_swing_table_idx_tup[delta[i]];
			cali_info->absolute_ofdm_swing_idx[i] =  delta_swing_table_idx_tup[delta[i]];	    /*Record delta swing for mix mode power tracking*/
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				"******Temp is higher and cali_info->absolute_ofdm_swing_idx[%d]=%d Path=%d\n", delta[i], cali_info->absolute_ofdm_swing_idx[i], i);
		} else {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				"delta_swing_table_idx_tdown[%d]=%d Path=%d\n", delta[i], delta_swing_table_idx_tdown[delta[i]], i);
			cali_info->delta_power_index_path[i] = -1 * delta_swing_table_idx_tdown[delta[i]];
			cali_info->absolute_ofdm_swing_idx[i] = -1 * delta_swing_table_idx_tdown[delta[i]];        /*Record delta swing for mix mode power tracking*/
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
				"******Temp is lower and cali_info->absolute_ofdm_swing_idx[%d]=%d Path=%d\n", delta[i], cali_info->absolute_ofdm_swing_idx[i], i);
		}
	}

#endif

	for (p = RF_PATH_A; p < c.rf_path_count; p++) {	
		if (cali_info->delta_power_index_path[p] == cali_info->delta_power_index_last_path[p])	     /*If Thermal value changes but lookup table value still the same*/
			cali_info->power_index_offset_path[p] = 0;
		else
			cali_info->power_index_offset_path[p] = cali_info->delta_power_index_path[p] - cali_info->delta_power_index_last_path[p];	/*Power index diff between 2 times Power Tracking*/
	}

#if 0
	if (dm->support_ic_type == ODM_RTL8814B) {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking MIX_MODE**********\n");
		for (p = RF_PATH_A; p < c.rf_path_count; p++)
			(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p, 0);
	} else {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking BBSWING_MODE**********\n");
		for (p = RF_PATH_A; p < c.rf_path_count; p++)
			(*c.odm_tx_pwr_track_set_pwr)(dm, BBSWING, p, 0);
	}
#else
	if (*dm->mp_mode == 1) {
		if (cali_info->txpowertrack_control == 1) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking MIX_MODE**********\n");
			tracking_method = MIX_MODE;
		} else if (cali_info->txpowertrack_control == 3) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking TSSI_MODE**********\n");
			tracking_method = TSSI_MODE;
		}
	} else {
		if (dm->priv->pmib->dot11RFEntry.tssi_enable == 0) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking MIX_MODE**********\n");
			tracking_method = MIX_MODE;
		} else if (dm->priv->pmib->dot11RFEntry.tssi_enable == 1) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter POWER Tracking TSSI_MODE**********\n");
			tracking_method = TSSI_MODE;
		}
	}

	if (dm->support_ic_type == ODM_RTL8822C || dm->support_ic_type == ODM_RTL8812F ||
		dm->support_ic_type == ODM_RTL8814B || dm->support_ic_type == ODM_RTL8197G)
		for (p = RF_PATH_A; p < c.rf_path_count; p++)
			(*c.odm_tx_pwr_track_set_pwr)(dm, tracking_method, p, 0);

#endif
	/* Wait sacn to do IQK by RF Jenyu*/
	if ((*dm->is_scan_in_process == false) && (!iqk_info->rfk_forbidden) && (dm->is_linked || *dm->mp_mode)) {
		/*Delta temperature is equal to or larger than 20 centigrade (When threshold is 8).*/
		if (delta_IQK >= c.threshold_iqk) {
			cali_info->thermal_value_iqk = thermal_value[RF_PATH_A];
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "delta_IQK(%d) >= threshold_iqk(%d)\n", delta_IQK, c.threshold_iqk);

			/*if (!cali_info->is_iqk_in_progress)*/
			/* 	(*c.do_iqk)(dm, delta_IQK, thermal_value[RF_PATH_A], 8);*/
			/*RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "Do IQK\n");*/

			/*if (!cali_info->is_iqk_in_progress)*/
			/*	(*c.do_tssi_dck)(dm, true);*/
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "Do TSSI DCK\n");
		}
	}

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "<===%s\n", __func__);

	cali_info->tx_powercount = 0;
}
#endif

#if (RTL8197F_SUPPORT == 1 || RTL8192F_SUPPORT == 1 || RTL8822B_SUPPORT == 1 ||\
	RTL8821C_SUPPORT == 1 || RTL8198F_SUPPORT == 1)
void
odm_txpowertracking_callback_thermal_meter_jaguar_series3(
	void		*dm_void
)
{
#if 1
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 thermal_value = 0, delta, delta_LCK, delta_IQK, channel, is_increase;
	u8 thermal_value_avg_count = 0, p = 0, i = 0;
	u32 thermal_value_avg = 0;
	struct rtl8192cd_priv *priv = dm->priv;
	struct txpwrtrack_cfg c;
	struct dm_rf_calibration_struct *cali_info = &(dm->rf_calibrate_info);
	struct dm_iqk_info *iqk_info = &dm->IQK_info;
	struct _hal_rf_ *rf = &dm->rf_table;
	/*The following tables decide the final index of OFDM/CCK swing table.*/
	u8 *pwrtrk_tab_up_a = NULL, *pwrtrk_tab_down_a = NULL;
	u8 *pwrtrk_tab_up_b = NULL, *pwrtrk_tab_down_b = NULL;
	u8 *pwrtrk_tab_up_cck_a = NULL, *pwrtrk_tab_down_cck_a = NULL;
	u8 *pwrtrk_tab_up_cck_b = NULL, *pwrtrk_tab_down_cck_b = NULL;
	/*for 8814 add by Yu Chen*/
	u8 *pwrtrk_tab_up_c = NULL, *pwrtrk_tab_down_c = NULL;
	u8 *pwrtrk_tab_up_d = NULL, *pwrtrk_tab_down_d = NULL;
	u8 *pwrtrk_tab_up_cck_c = NULL, *pwrtrk_tab_down_cck_c = NULL;
	u8 *pwrtrk_tab_up_cck_d = NULL, *pwrtrk_tab_down_cck_d = NULL;
	s8 thermal_value_temp = 0;

#ifdef MP_TEST
	if ((OPMODE & WIFI_MP_STATE) || *(dm->mp_mode)) {
		channel = priv->pshare->working_channel;
		if (priv->pshare->mp_txpwr_tracking == false)
			return;
	} else
#endif
	{
		channel = (priv->pmib->dot11RFEntry.dot11channel);
	}

	configure_txpower_track(dm, &c);

	(*c.get_delta_all_swing_table)(dm,
		(u8 **)&pwrtrk_tab_up_a, (u8 **)&pwrtrk_tab_down_a,
		(u8 **)&pwrtrk_tab_up_b, (u8 **)&pwrtrk_tab_down_b,
		(u8 **)&pwrtrk_tab_up_cck_a, (u8 **)&pwrtrk_tab_down_cck_a,
		(u8 **)&pwrtrk_tab_up_cck_b, (u8 **)&pwrtrk_tab_down_cck_b);

	if (GET_CHIP_VER(priv) == VERSION_8198F) {
		(*c.get_delta_all_swing_table_ex)(dm,
			(u8 **)&pwrtrk_tab_up_c, (u8 **)&pwrtrk_tab_down_c,
			(u8 **)&pwrtrk_tab_up_d, (u8 **)&pwrtrk_tab_down_d,
			(u8 **)&pwrtrk_tab_up_cck_c, (u8 **)&pwrtrk_tab_down_cck_c,
			(u8 **)&pwrtrk_tab_up_cck_d, (u8 **)&pwrtrk_tab_down_cck_d);
	}
	/*0x42: RF Reg[15:10] 88E*/
	thermal_value = (u8)odm_get_rf_reg(dm, RF_PATH_A, c.thermal_reg_addr, 0xfc00);
#ifdef THER_TRIM
	if (GET_CHIP_VER(priv) == VERSION_8197F) {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"orig thermal_value=%d, ther_trim_val=%d\n", thermal_value, priv->pshare->rf_ft_var.ther_trim_val);

		thermal_value += priv->pshare->rf_ft_var.ther_trim_val;

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"after thermal trim, thermal_value=%d\n", thermal_value);
	}

	if (GET_CHIP_VER(priv) == VERSION_8198F) {
		thermal_value_temp = thermal_value + phydm_get_thermal_offset(dm);

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,
		       "thermal_value_temp(%d) = ther_value(%d) + ther_trim_ther(%d)\n",
		       thermal_value_temp, thermal_value, phydm_get_thermal_offset(dm));

		if (thermal_value_temp > 63)
			thermal_value = 63;
		else if (thermal_value_temp < 0)
			thermal_value = 0;
		else
			thermal_value = thermal_value_temp;
	}
#endif

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"\n\n\nCurrent Thermal = 0x%x(%d) EEPROMthermalmeter 0x%x(%d)\n"
		, thermal_value, thermal_value, priv->pmib->dot11RFEntry.ther, priv->pmib->dot11RFEntry.ther);

	/* Initialize */
	if (!dm->rf_calibrate_info.thermal_value)
		dm->rf_calibrate_info.thermal_value = priv->pmib->dot11RFEntry.ther;

	if (!dm->rf_calibrate_info.thermal_value_lck)
		dm->rf_calibrate_info.thermal_value_lck = priv->pmib->dot11RFEntry.ther;

	if (!dm->rf_calibrate_info.thermal_value_iqk)
		dm->rf_calibrate_info.thermal_value_iqk = priv->pmib->dot11RFEntry.ther;

	/* calculate average thermal meter */
	dm->rf_calibrate_info.thermal_value_avg[dm->rf_calibrate_info.thermal_value_avg_index] = thermal_value;
	dm->rf_calibrate_info.thermal_value_avg_index++;

	if (dm->rf_calibrate_info.thermal_value_avg_index == c.average_thermal_num)   /*Average times =  c.average_thermal_num*/
		dm->rf_calibrate_info.thermal_value_avg_index = 0;

	for (i = 0; i < c.average_thermal_num; i++) {
		if (dm->rf_calibrate_info.thermal_value_avg[i]) {
			thermal_value_avg += dm->rf_calibrate_info.thermal_value_avg[i];
			thermal_value_avg_count++;
		}
	}

	if (thermal_value_avg_count) {/*Calculate Average thermal_value after average enough times*/
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"thermal_value_avg=0x%x(%d)  thermal_value_avg_count = %d\n"
			, thermal_value_avg, thermal_value_avg, thermal_value_avg_count);

		thermal_value = (u8)(thermal_value_avg / thermal_value_avg_count);

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"AVG Thermal Meter = 0x%X(%d), EEPROMthermalmeter = 0x%X(%d)\n", thermal_value, thermal_value, priv->pmib->dot11RFEntry.ther, priv->pmib->dot11RFEntry.ther);
	}

	/*4 Calculate delta, delta_LCK, delta_IQK.*/
	delta = RTL_ABS(thermal_value, priv->pmib->dot11RFEntry.ther);
	delta_LCK = RTL_ABS(thermal_value, dm->rf_calibrate_info.thermal_value_lck);
	delta_IQK = RTL_ABS(thermal_value, dm->rf_calibrate_info.thermal_value_iqk);
	is_increase = ((thermal_value < priv->pmib->dot11RFEntry.ther) ? 0 : 1);

	if (delta > 29) { /* power track table index(thermal diff.) upper bound*/
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "delta(%d) > 29, set delta to 29\n", delta);
		delta = 29;
	}

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "(delta, delta_LCK, delta_IQK) = (%d, %d, %d)\n", delta, delta_LCK, delta_IQK);

	/*4 if necessary, do LCK.*/
	if ((delta_LCK >= c.threshold_iqk) && (!iqk_info->rfk_forbidden)) {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "delta_LCK(%d) >= threshold_iqk(%d)\n", delta_LCK, c.threshold_iqk);
		dm->rf_calibrate_info.thermal_value_lck = thermal_value;
#if (RTL8822B_SUPPORT != 1)
		if (!(dm->support_ic_type & ODM_RTL8822B)) {
			if (c.phy_lc_calibrate)
				(*c.phy_lc_calibrate)(dm);
		}
#endif
	}

	if (!priv->pmib->dot11RFEntry.ther) /*Don't do power tracking since no calibrated thermal value*/
		return;

	/*4 Do Power Tracking*/

	if (thermal_value != dm->rf_calibrate_info.thermal_value) {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"******** START POWER TRACKING ********\n");
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"Readback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n",
		       thermal_value, dm->rf_calibrate_info.thermal_value, priv->pmib->dot11RFEntry.ther);

#ifdef _TRACKING_TABLE_FILE
		if (priv->pshare->rf_ft_var.pwr_track_file) {
			if (is_increase) { /*thermal is higher than base*/
				for (p = RF_PATH_A; p < c.rf_path_count; p++) {
					switch (p) {
					case RF_PATH_B:
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"pwrtrk_tab_up_b[%d] = %d pwrtrk_tab_up_cck_b[%d] = %d\n", delta, pwrtrk_tab_up_b[delta], delta, pwrtrk_tab_up_cck_b[delta]);
						cali_info->absolute_ofdm_swing_idx[p] = pwrtrk_tab_up_b[delta];
						cali_info->absolute_cck_swing_idx[p] = pwrtrk_tab_up_cck_b[delta];
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"******Temp is higher and pRF->absolute_ofdm_swing_idx[RF_PATH_B] = %d pRF->absolute_cck_swing_idx[RF_PATH_B] = %d\n", cali_info->absolute_ofdm_swing_idx[p], cali_info->absolute_cck_swing_idx[p]);
						break;

					case RF_PATH_C:
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"pwrtrk_tab_up_c[%d] = %d pwrtrk_tab_up_cck_c[%d] = %d\n", delta, pwrtrk_tab_up_c[delta], delta, pwrtrk_tab_up_cck_c[delta]);
						cali_info->absolute_ofdm_swing_idx[p] = pwrtrk_tab_up_c[delta];
						cali_info->absolute_cck_swing_idx[p] = pwrtrk_tab_up_cck_c[delta];
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"******Temp is higher and pRF->absolute_ofdm_swing_idx[RF_PATH_C] = %d pRF->absolute_cck_swing_idx[RF_PATH_C] = %d\n", cali_info->absolute_ofdm_swing_idx[p], cali_info->absolute_cck_swing_idx[p]);
						break;

					case RF_PATH_D:
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"pwrtrk_tab_up_d[%d] = %d pwrtrk_tab_up_cck_d[%d] = %d\n", delta, pwrtrk_tab_up_d[delta], delta, pwrtrk_tab_up_cck_d[delta]);
						cali_info->absolute_ofdm_swing_idx[p] = pwrtrk_tab_up_d[delta];
						cali_info->absolute_cck_swing_idx[p] = pwrtrk_tab_up_cck_d[delta];
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"******Temp is higher and pRF->absolute_ofdm_swing_idx[RF_PATH_D] = %d pRF->absolute_cck_swing_idx[RF_PATH_D] = %d\n", cali_info->absolute_ofdm_swing_idx[p], cali_info->absolute_cck_swing_idx[p]);
						break;
					default:
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"pwrtrk_tab_up_a[%d] = %d pwrtrk_tab_up_cck_a[%d] = %d\n", delta, pwrtrk_tab_up_a[delta], delta, pwrtrk_tab_up_cck_a[delta]);
						cali_info->absolute_ofdm_swing_idx[p] = pwrtrk_tab_up_a[delta];
						cali_info->absolute_cck_swing_idx[p] = pwrtrk_tab_up_cck_a[delta];
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"******Temp is higher and pRF->absolute_ofdm_swing_idx[RF_PATH_A] = %d pRF->absolute_cck_swing_idx[RF_PATH_A] = %d\n", cali_info->absolute_ofdm_swing_idx[p], cali_info->absolute_cck_swing_idx[p]);
						break;
					}
				}
			} else { /* thermal is lower than base*/
				for (p = RF_PATH_A; p < c.rf_path_count; p++) {
					switch (p) {
					case RF_PATH_B:
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"pwrtrk_tab_down_b[%d] = %d   pwrtrk_tab_down_cck_b[%d] = %d\n", delta, pwrtrk_tab_down_b[delta], delta, pwrtrk_tab_down_cck_b[delta]);
						cali_info->absolute_ofdm_swing_idx[p] = -1 * pwrtrk_tab_down_b[delta];
						cali_info->absolute_cck_swing_idx[p] = -1 * pwrtrk_tab_down_cck_b[delta];
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"******Temp is lower and pRF->absolute_ofdm_swing_idx[RF_PATH_B] = %d   pRF->absolute_cck_swing_idx[RF_PATH_B] = %d\n", cali_info->absolute_ofdm_swing_idx[p], cali_info->absolute_cck_swing_idx[p]);
						break;

					case RF_PATH_C:
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"pwrtrk_tab_down_c[%d] = %d   pwrtrk_tab_down_cck_c[%d] = %d\n", delta, pwrtrk_tab_down_c[delta], delta, pwrtrk_tab_down_cck_c[delta]);
						cali_info->absolute_ofdm_swing_idx[p] = -1 * pwrtrk_tab_down_c[delta];
						cali_info->absolute_cck_swing_idx[p] = -1 * pwrtrk_tab_down_cck_c[delta];
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"******Temp is lower and pRF->absolute_ofdm_swing_idx[RF_PATH_C] = %d   pRF->absolute_cck_swing_idx[RF_PATH_C] = %d\n", cali_info->absolute_ofdm_swing_idx[p], cali_info->absolute_cck_swing_idx[p]);
						break;

					case RF_PATH_D:
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"pwrtrk_tab_down_d[%d] = %d   pwrtrk_tab_down_cck_d[%d] = %d\n", delta, pwrtrk_tab_down_d[delta], delta, pwrtrk_tab_down_cck_d[delta]);
						cali_info->absolute_ofdm_swing_idx[p] = -1 * pwrtrk_tab_down_d[delta];
						cali_info->absolute_cck_swing_idx[p] = -1 * pwrtrk_tab_down_cck_d[delta];
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"******Temp is lower and pRF->absolute_ofdm_swing_idx[RF_PATH_D] = %d   pRF->absolute_cck_swing_idx[RF_PATH_D] = %d\n", cali_info->absolute_ofdm_swing_idx[p], cali_info->absolute_cck_swing_idx[p]);
						break;

					default:
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"pwrtrk_tab_down_a[%d] = %d   pwrtrk_tab_down_cck_a[%d] = %d\n", delta, pwrtrk_tab_down_a[delta], delta, pwrtrk_tab_down_cck_a[delta]);
						cali_info->absolute_ofdm_swing_idx[p] = -1 * pwrtrk_tab_down_a[delta];
						cali_info->absolute_cck_swing_idx[p] = -1 * pwrtrk_tab_down_cck_a[delta];
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"******Temp is lower and pRF->absolute_ofdm_swing_idx[RF_PATH_A] = %d   pRF->absolute_cck_swing_idx[RF_PATH_A] = %d\n", cali_info->absolute_ofdm_swing_idx[p], cali_info->absolute_cck_swing_idx[p]);
						break;
					}
				}
			}

			if (is_increase) {
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK, ">>> increse power --->\n");
				if (GET_CHIP_VER(priv) == VERSION_8197F) {
					for (p = RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(dm, BBSWING, p, 0);
				} else if (GET_CHIP_VER(priv) == VERSION_8192F) {
					for (p = RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p, 0);
				} else if (GET_CHIP_VER(priv) == VERSION_8822B) {
					for (p = RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p, 0);
				} else if (GET_CHIP_VER(priv) == VERSION_8821C) {
					for (p = RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p, 0);
				}  else if (GET_CHIP_VER(priv) == VERSION_8198F) {
					for (p = RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p, 0);
				} else if (GET_CHIP_VER(priv) == VERSION_8192F) {
					for (p = RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p, 0);
				}
			} else {
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK, ">>> decrese power --->\n");
				if (GET_CHIP_VER(priv) == VERSION_8197F) {
					for (p = RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(dm, BBSWING, p, 0);
				} else if (GET_CHIP_VER(priv) == VERSION_8192F) {
					for (p = RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p, 0);
				} else if (GET_CHIP_VER(priv) == VERSION_8822B) {
					for (p = RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p, 0);
				} else if (GET_CHIP_VER(priv) == VERSION_8821C) {
					for (p = RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p, 0);
				} else if (GET_CHIP_VER(priv) == VERSION_8198F) {
					for (p = RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p, 0);
				} else if (GET_CHIP_VER(priv) == VERSION_8192F) {
					for (p = RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p, 0);
				}
			}
		}
#endif

		if (GET_CHIP_VER(priv) != VERSION_8198F) {
			if ((delta_IQK >= c.threshold_iqk) && (!iqk_info->rfk_forbidden) && dm->is_linked) {
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "delta_IQK(%d) >= threshold_iqk(%d)\n", delta_IQK, c.threshold_iqk);
				dm->rf_calibrate_info.thermal_value_iqk = thermal_value;
				if (!(dm->support_ic_type & ODM_RTL8197F)) {
					if (c.do_iqk)
						(*c.do_iqk)(dm, false, thermal_value, 0);
				}
			}
		}

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "\n******** END:%s() ********\n\n", __func__);
		/*update thermal meter value*/
		dm->rf_calibrate_info.thermal_value =  thermal_value;

	}

#endif
}
#endif

/*#if (RTL8814A_SUPPORT == 1)*/
#if (RTL8814A_SUPPORT == 1)

void
odm_txpowertracking_callback_thermal_meter_jaguar_series2(
	void		*dm_void
)
{
	struct dm_struct		*dm = (struct dm_struct *)dm_void;
	u8			thermal_value = 0, delta, delta_LCK, delta_IQK, channel, is_increase;
	u8			thermal_value_avg_count = 0, p = 0, i = 0;
	u32			thermal_value_avg = 0, reg0x18;
	u32			bb_swing_reg[4] = {REG_A_TX_SCALE_JAGUAR, REG_B_TX_SCALE_JAGUAR, REG_C_TX_SCALE_JAGUAR2, REG_D_TX_SCALE_JAGUAR2};
	s32			ele_D;
	u32			bb_swing_idx;
	struct rtl8192cd_priv	*priv = dm->priv;
	struct txpwrtrack_cfg	c;
	boolean			is_tssi_enable = false;
	struct dm_rf_calibration_struct	*cali_info = &(dm->rf_calibrate_info);
	struct dm_iqk_info	*iqk_info = &dm->IQK_info;

	/* 4 1. The following TWO tables decide the final index of OFDM/CCK swing table. */
	u8			*delta_swing_table_idx_tup_a = NULL, *delta_swing_table_idx_tdown_a = NULL;
	u8			*delta_swing_table_idx_tup_b = NULL, *delta_swing_table_idx_tdown_b = NULL;
	/* for 8814 add by Yu Chen */
	u8			*delta_swing_table_idx_tup_c = NULL, *delta_swing_table_idx_tdown_c = NULL;
	u8			*delta_swing_table_idx_tup_d = NULL, *delta_swing_table_idx_tdown_d = NULL;

#ifdef MP_TEST
	if ((OPMODE & WIFI_MP_STATE) || *(dm->mp_mode)) {
		channel = priv->pshare->working_channel;
		if (priv->pshare->mp_txpwr_tracking == false)
			return;
	} else
#endif
	{
		channel = (priv->pmib->dot11RFEntry.dot11channel);
	}

	configure_txpower_track(dm, &c);
	cali_info->default_ofdm_index = priv->pshare->OFDM_index0[RF_PATH_A];

	(*c.get_delta_swing_table)(dm, (u8 **)&delta_swing_table_idx_tup_a, (u8 **)&delta_swing_table_idx_tdown_a,
		(u8 **)&delta_swing_table_idx_tup_b, (u8 **)&delta_swing_table_idx_tdown_b);

	if (dm->support_ic_type & ODM_RTL8814A)	/* for 8814 path C & D */
		(*c.get_delta_swing_table8814only)(dm, (u8 **)&delta_swing_table_idx_tup_c, (u8 **)&delta_swing_table_idx_tdown_c,
			(u8 **)&delta_swing_table_idx_tup_d, (u8 **)&delta_swing_table_idx_tdown_d);

	thermal_value = (u8)odm_get_rf_reg(dm, RF_PATH_A, c.thermal_reg_addr, 0xfc00); /* 0x42: RF Reg[15:10] 88E */
	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"\nReadback Thermal Meter = 0x%x, pre thermal meter 0x%x, EEPROMthermalmeter 0x%x\n", thermal_value, dm->rf_calibrate_info.thermal_value, priv->pmib->dot11RFEntry.ther);

	/* Initialize */
	if (!dm->rf_calibrate_info.thermal_value)
		dm->rf_calibrate_info.thermal_value = priv->pmib->dot11RFEntry.ther;

	if (!dm->rf_calibrate_info.thermal_value_lck)
		dm->rf_calibrate_info.thermal_value_lck = priv->pmib->dot11RFEntry.ther;

	if (!dm->rf_calibrate_info.thermal_value_iqk)
		dm->rf_calibrate_info.thermal_value_iqk = priv->pmib->dot11RFEntry.ther;

	is_tssi_enable = (boolean)odm_get_rf_reg(dm, RF_PATH_A, REG_RF_TX_GAIN_OFFSET, BIT(7));	/* check TSSI enable */

	/* 4 Query OFDM BB swing default setting 	Bit[31:21] */
	for (p = RF_PATH_A ; p < c.rf_path_count ; p++) {
		ele_D = odm_get_bb_reg(dm, bb_swing_reg[p], 0xffe00000);
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"0x%x:0x%x ([31:21] = 0x%x)\n", bb_swing_reg[p], odm_get_bb_reg(dm, bb_swing_reg[p], MASKDWORD), ele_D);

		for (bb_swing_idx = 0; bb_swing_idx < TXSCALE_TABLE_SIZE; bb_swing_idx++) {/* 4 */
			if (ele_D == tx_scaling_table_jaguar[bb_swing_idx]) {
				dm->rf_calibrate_info.OFDM_index[p] = (u8)bb_swing_idx;
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"OFDM_index[%d]=%d\n", p, dm->rf_calibrate_info.OFDM_index[p]);
				break;
			}
		}
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "kfree_offset[%d]=%d\n", p, cali_info->kfree_offset[p]);

	}

	/* calculate average thermal meter */
	dm->rf_calibrate_info.thermal_value_avg[dm->rf_calibrate_info.thermal_value_avg_index] = thermal_value;
	dm->rf_calibrate_info.thermal_value_avg_index++;
	if (dm->rf_calibrate_info.thermal_value_avg_index == c.average_thermal_num)  /* Average times =  c.average_thermal_num */
		dm->rf_calibrate_info.thermal_value_avg_index = 0;

	for (i = 0; i < c.average_thermal_num; i++) {
		if (dm->rf_calibrate_info.thermal_value_avg[i]) {
			thermal_value_avg += dm->rf_calibrate_info.thermal_value_avg[i];
			thermal_value_avg_count++;
		}
	}

	if (thermal_value_avg_count) {            /* Calculate Average thermal_value after average enough times */
		thermal_value = (u8)(thermal_value_avg / thermal_value_avg_count);
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"AVG Thermal Meter = 0x%X, EEPROMthermalmeter = 0x%X\n", thermal_value, priv->pmib->dot11RFEntry.ther);
	}

	/* 4 Calculate delta, delta_LCK, delta_IQK. */
	delta = RTL_ABS(thermal_value, priv->pmib->dot11RFEntry.ther);
	delta_LCK = RTL_ABS(thermal_value, dm->rf_calibrate_info.thermal_value_lck);
	delta_IQK = RTL_ABS(thermal_value, dm->rf_calibrate_info.thermal_value_iqk);
	is_increase = ((thermal_value < priv->pmib->dot11RFEntry.ther) ? 0 : 1);

	/* 4 if necessary, do LCK. */
	if (!(dm->support_ic_type & ODM_RTL8821)) {
		if ((delta_LCK > c.threshold_iqk) && (!iqk_info->rfk_forbidden)) {
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "delta_LCK(%d) >= threshold_iqk(%d)\n", delta_LCK, c.threshold_iqk);
			dm->rf_calibrate_info.thermal_value_lck = thermal_value;

			/*Use RTLCK, so close power tracking driver LCK*/
#if (RTL8814A_SUPPORT != 1)
			if (!(dm->support_ic_type & ODM_RTL8814A)) {
				if (c.phy_lc_calibrate)
					(*c.phy_lc_calibrate)(dm);
			}
#endif
		}
	}

	if ((delta_IQK > c.threshold_iqk) && (!iqk_info->rfk_forbidden)) {
		panic_printk("%s(%d)\n", __FUNCTION__, __LINE__);
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "delta_IQK(%d) >= threshold_iqk(%d)\n", delta_IQK, c.threshold_iqk);
		dm->rf_calibrate_info.thermal_value_iqk = thermal_value;
		if (c.do_iqk)
			(*c.do_iqk)(dm, true, 0, 0);
	}

	if (!priv->pmib->dot11RFEntry.ther)	/*Don't do power tracking since no calibrated thermal value*/
		return;

	/* 4 Do Power Tracking */

	if (is_tssi_enable == true) {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "**********Enter PURE TSSI MODE**********\n");
		for (p = RF_PATH_A; p < c.rf_path_count; p++)
			(*c.odm_tx_pwr_track_set_pwr)(dm, TSSI_MODE, p, 0);
	} else if (thermal_value != dm->rf_calibrate_info.thermal_value) {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"\n******** START POWER TRACKING ********\n");
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", thermal_value, dm->rf_calibrate_info.thermal_value, priv->pmib->dot11RFEntry.ther);

#ifdef _TRACKING_TABLE_FILE
		if (priv->pshare->rf_ft_var.pwr_track_file) {
			if (is_increase) {		/* thermal is higher than base */
				for (p = RF_PATH_A; p < c.rf_path_count; p++) {
					switch (p) {
					case RF_PATH_B:
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"delta_swing_table_idx_tup_b[%d] = %d\n", delta, delta_swing_table_idx_tup_b[delta]);
						cali_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_b[delta];       /* Record delta swing for mix mode power tracking */
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"******Temp is higher and dm->absolute_ofdm_swing_idx[RF_PATH_B] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
						break;

					case RF_PATH_C:
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"delta_swing_table_idx_tup_c[%d] = %d\n", delta, delta_swing_table_idx_tup_c[delta]);
						cali_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_c[delta];       /* Record delta swing for mix mode power tracking */
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"******Temp is higher and dm->absolute_ofdm_swing_idx[RF_PATH_C] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
						break;

					case RF_PATH_D:
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"delta_swing_table_idx_tup_d[%d] = %d\n", delta, delta_swing_table_idx_tup_d[delta]);
						cali_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_d[delta];       /* Record delta swing for mix mode power tracking */
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"******Temp is higher and dm->absolute_ofdm_swing_idx[RF_PATH_D] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
						break;

					default:
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"delta_swing_table_idx_tup_a[%d] = %d\n", delta, delta_swing_table_idx_tup_a[delta]);
						cali_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_a[delta];        /* Record delta swing for mix mode power tracking */
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"******Temp is higher and dm->absolute_ofdm_swing_idx[RF_PATH_A] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
						break;
					}
				}
			} else {				/* thermal is lower than base */
				for (p = RF_PATH_A; p < c.rf_path_count; p++) {
					switch (p) {
					case RF_PATH_B:
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"delta_swing_table_idx_tdown_b[%d] = %d\n", delta, delta_swing_table_idx_tdown_b[delta]);
						cali_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_b[delta];        /* Record delta swing for mix mode power tracking */
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"******Temp is lower and dm->absolute_ofdm_swing_idx[RF_PATH_B] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
						break;

					case RF_PATH_C:
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"delta_swing_table_idx_tdown_c[%d] = %d\n", delta, delta_swing_table_idx_tdown_c[delta]);
						cali_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_c[delta];        /* Record delta swing for mix mode power tracking */
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"******Temp is lower and dm->absolute_ofdm_swing_idx[RF_PATH_C] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
						break;

					case RF_PATH_D:
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"delta_swing_table_idx_tdown_d[%d] = %d\n", delta, delta_swing_table_idx_tdown_d[delta]);
						cali_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_d[delta];        /* Record delta swing for mix mode power tracking */
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"******Temp is lower and dm->absolute_ofdm_swing_idx[RF_PATH_D] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
						break;

					default:
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"delta_swing_table_idx_tdown_a[%d] = %d\n", delta, delta_swing_table_idx_tdown_a[delta]);
						cali_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_a[delta];        /* Record delta swing for mix mode power tracking */
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"******Temp is lower and dm->absolute_ofdm_swing_idx[RF_PATH_A] = %d\n", cali_info->absolute_ofdm_swing_idx[p]);
						break;
					}
				}
			}

			if (is_increase) {
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK, ">>> increse power --->\n");
				for (p = RF_PATH_A; p < c.rf_path_count; p++)
					(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p, 0);
			} else {
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK, ">>> decrese power --->\n");
				for (p = RF_PATH_A; p < c.rf_path_count; p++)
					(*c.odm_tx_pwr_track_set_pwr)(dm, MIX_MODE, p, 0);
			}
		}
#endif

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "\n******** END:%s() ********\n", __FUNCTION__);
		/* update thermal meter value */
		dm->rf_calibrate_info.thermal_value =  thermal_value;

	}
}
#endif

#if (RTL8812A_SUPPORT == 1 || RTL8881A_SUPPORT == 1)
void
odm_txpowertracking_callback_thermal_meter_jaguar_series(
	void		*dm_void
)
{
	struct dm_struct		*dm = (struct dm_struct *)dm_void;
	unsigned char			thermal_value = 0, delta, delta_LCK, channel, is_decrease;
	unsigned char			thermal_value_avg_count = 0;
	unsigned int			thermal_value_avg = 0, reg0x18;
	unsigned int			bb_swing_reg[4] = {0xc1c, 0xe1c, 0x181c, 0x1a1c};
	int					ele_D, value32;
	char					OFDM_index[2], index;
	unsigned int			i = 0, j = 0, rf_path, max_rf_path = 2, rf;
	struct rtl8192cd_priv		*priv = dm->priv;
	unsigned char			OFDM_min_index = 7; /* OFDM BB Swing should be less than +2.5dB, which is required by Arthur and Mimic */
	struct dm_iqk_info	*iqk_info = &dm->IQK_info;


#ifdef MP_TEST
	if ((OPMODE & WIFI_MP_STATE) || *(dm->mp_mode)) {
		channel = priv->pshare->working_channel;
		if (priv->pshare->mp_txpwr_tracking == false)
			return;
	} else
#endif
	{
		channel = (priv->pmib->dot11RFEntry.dot11channel);
	}

#if RTL8881A_SUPPORT
	if (dm->support_ic_type == ODM_RTL8881A) {
		max_rf_path = 1;
		if ((get_bonding_type_8881A() == BOND_8881AM || get_bonding_type_8881A() == BOND_8881AN)
		    && priv->pshare->rf_ft_var.use_intpa8881A && (*dm->band_type == ODM_BAND_2_4G))
			OFDM_min_index = 6;		/* intPA - upper bond set to +3 dB (base: -2 dB)ot11RFEntry.phy_band_select == PHY_BAND_2G)) */
		else
			OFDM_min_index = 10;		/* OFDM BB Swing should be less than +1dB, which is required by Arthur and Mimic */
	}
#endif


	thermal_value = (unsigned char)phy_query_rf_reg(priv, RF_PATH_A, 0x42, 0xfc00, 1); /* 0x42: RF Reg[15:10] 88E */
	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", thermal_value, priv->pshare->thermal_value, priv->pmib->dot11RFEntry.ther);


	/* 4 Query OFDM BB swing default setting 	Bit[31:21] */
	for (rf_path = 0 ; rf_path < max_rf_path ; rf_path++) {
		ele_D = phy_query_bb_reg(priv, bb_swing_reg[rf_path], 0xffe00000);
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "0x%x:0x%x ([31:21] = 0x%x)\n", bb_swing_reg[rf_path], phy_query_bb_reg(priv, bb_swing_reg[rf_path], MASKDWORD), ele_D);
		for (i = 0; i < OFDM_TABLE_SIZE_8812; i++) {/* 4 */
			if (ele_D == ofdm_swing_table_8812[i]) {
				OFDM_index[rf_path] = (unsigned char)i;
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "OFDM_index[%d]=%d\n", rf_path, OFDM_index[rf_path]);
				break;
			}
		}
	}
#if 0
	/* Query OFDM path A default setting 	Bit[31:21] */
	ele_D = phy_query_bb_reg(priv, 0xc1c, 0xffe00000);
	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "0xc1c:0x%x ([31:21] = 0x%x)\n", phy_query_bb_reg(priv, 0xc1c, MASKDWORD), ele_D);
	for (i = 0; i < OFDM_TABLE_SIZE_8812; i++) {/* 4 */
		if (ele_D == ofdm_swing_table_8812[i]) {
			OFDM_index[0] = (unsigned char)i;
			RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "OFDM_index[0]=%d\n", OFDM_index[0]);
			break;
		}
	}
	/* Query OFDM path B default setting */
	if (rf == 2) {
		ele_D = phy_query_bb_reg(priv, 0xe1c, 0xffe00000);
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "0xe1c:0x%x ([32:21] = 0x%x)\n", phy_query_bb_reg(priv, 0xe1c, MASKDWORD), ele_D);
		for (i = 0; i < OFDM_TABLE_SIZE_8812; i++) {
			if (ele_D == ofdm_swing_table_8812[i]) {
				OFDM_index[1] = (unsigned char)i;
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "OFDM_index[1]=%d\n", OFDM_index[1]);
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
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "\n******** START POWER TRACKING ********\n");
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", thermal_value, priv->pshare->thermal_value, priv->pmib->dot11RFEntry.ther);
		delta = RTL_ABS(thermal_value, priv->pmib->dot11RFEntry.ther);
		delta_LCK = RTL_ABS(thermal_value, priv->pshare->thermal_value_lck);
		is_decrease = ((thermal_value < priv->pmib->dot11RFEntry.ther) ? 1 : 0);
		/* if (*dm->band_type == ODM_BAND_5G) */
		{
#ifdef _TRACKING_TABLE_FILE
			if (priv->pshare->rf_ft_var.pwr_track_file) {
				for (rf_path = 0; rf_path < max_rf_path; rf_path++) {
					RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "diff: (%s)%d ==> get index from table : %d)\n", (is_decrease ? "-" : "+"), delta, get_tx_tracking_index(priv, channel, rf_path, delta, is_decrease, 0));
					if (is_decrease) {
						OFDM_index[rf_path] = priv->pshare->OFDM_index0[rf_path] + get_tx_tracking_index(priv, channel, rf_path, delta, is_decrease, 0);
						OFDM_index[rf_path] = ((OFDM_index[rf_path] > (OFDM_TABLE_SIZE_8812 - 1)) ? (OFDM_TABLE_SIZE_8812 - 1) : OFDM_index[rf_path]);
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK, ">>> decrese power ---> new OFDM_INDEX:%d (%d + %d)\n", OFDM_index[rf_path], priv->pshare->OFDM_index0[rf_path], get_tx_tracking_index(priv, channel, rf_path, delta, is_decrease, 0));
#if 0/* RTL8881A_SUPPORT */
						if (dm->support_ic_type == ODM_RTL8881A) {
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
						if (dm->support_ic_type == ODM_RTL8881A) {
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
						RF_DBG(dm, DBG_RF_TX_PWR_TRACK, ">>> increse power ---> new OFDM_INDEX:%d (%d - %d)\n", OFDM_index[rf_path], priv->pshare->OFDM_index0[rf_path], get_tx_tracking_index(priv, channel, rf_path, delta, is_decrease, 0));
					}
				}
			}
#endif
			/* 4 Set new BB swing index */
			for (rf_path = 0; rf_path < max_rf_path; rf_path++) {
				phy_set_bb_reg(priv, bb_swing_reg[rf_path], 0xffe00000, ofdm_swing_table_8812[(unsigned int)OFDM_index[rf_path]]);
				RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "Readback 0x%x[31:21] = 0x%x, OFDM_index:%d\n", bb_swing_reg[rf_path], phy_query_bb_reg(priv, bb_swing_reg[rf_path], 0xffe00000), OFDM_index[rf_path]);
			}

		}
		if ((delta_LCK > 8) && (!iqk_info->rfk_forbidden)) {
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
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "\n******** END:%s() ********\n", __FUNCTION__);

		/* update thermal meter value */
		priv->pshare->thermal_value = thermal_value;
		for (rf_path = 0; rf_path < max_rf_path; rf_path++)
			priv->pshare->OFDM_index[rf_path] = OFDM_index[rf_path];
	}
}

#endif


void
odm_txpowertracking_callback_thermal_meter(
	void		*dm_void
)
{
	struct dm_struct		*dm = (struct dm_struct *)dm_void;
	struct dm_rf_calibration_struct	*cali_info = &(dm->rf_calibrate_info);
	struct dm_iqk_info	*iqk_info = &dm->IQK_info;

	
#if (RTL8814B_SUPPORT == 1 || RTL8812F_SUPPORT == 1 || RTL8822C_SUPPORT == 1 || RTL8197G_SUPPORT == 1)
	if (dm->support_ic_type & (ODM_RTL8814B | ODM_RTL8812F | ODM_RTL8822C | ODM_RTL8197G)) {
		odm_txpowertracking_callback_thermal_meter_jaguar_series4(dm);
		return;
	}
#endif
#if (RTL8197F_SUPPORT == 1 ||RTL8192F_SUPPORT == 1 || RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1 || RTL8198F_SUPPORT == 1)
	if (dm->support_ic_type == ODM_RTL8197F || dm->support_ic_type == ODM_RTL8192F || dm->support_ic_type == ODM_RTL8822B
		|| dm->support_ic_type == ODM_RTL8821C || dm->support_ic_type == ODM_RTL8198F) {
		odm_txpowertracking_callback_thermal_meter_jaguar_series3(dm);
		return;
	}
#endif
#if (RTL8814A_SUPPORT == 1)		/*use this function to do power tracking after 8814 by YuChen*/
	if (dm->support_ic_type & ODM_RTL8814A) {
		odm_txpowertracking_callback_thermal_meter_jaguar_series2(dm);
		return;
	}
#endif
#if (RTL8881A_SUPPORT || RTL8812A_SUPPORT == 1)
	if (dm->support_ic_type & ODM_RTL8812 || dm->support_ic_type & ODM_RTL8881A) {
		odm_txpowertracking_callback_thermal_meter_jaguar_series(dm);
		return;
	}
#endif

#if (RTL8192E_SUPPORT == 1)
	if (dm->support_ic_type == ODM_RTL8192E) {
		odm_txpowertracking_callback_thermal_meter_92e(dm);
		return;
	}
#endif

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	HAL_DATA_TYPE	*hal_data = GET_HAL_DATA(((PADAPTER)adapter));
	/* PMGNT_INFO      		mgnt_info = &adapter->mgnt_info; */
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
	u8			indexforchannel = 0;/*get_right_chnl_place_for_iqk(hal_data->current_channel)*/
	enum            _POWER_DEC_INC { POWER_DEC, POWER_INC };

	struct txpwrtrack_cfg	c;


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
	struct rtl8192cd_priv	*priv = dm->priv;
#endif

	/* 4 2. Initilization ( 7 steps in total ) */

	configure_txpower_track(dm, &c);

	dm->rf_calibrate_info.txpowertracking_callback_cnt++; /* cosa add for debug */
	dm->rf_calibrate_info.is_txpowertracking_init = true;

#if (MP_DRIVER == 1)
	dm->rf_calibrate_info.txpowertrack_control = hal_data->txpowertrack_control; /* <Kordan> We should keep updating the control variable according to HalData.
     * <Kordan> rf_calibrate_info.rega24 will be initialized when ODM HW configuring, but MP configures with para files. */
	dm->rf_calibrate_info.rega24 = 0x090e1317;
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_AP) && defined(MP_TEST)
	if ((OPMODE & WIFI_MP_STATE) || *(dm->mp_mode)) {
		if (dm->priv->pshare->mp_txpwr_tracking == false)
			return;
	}
#endif
	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "===>odm_txpowertracking_callback_thermal_meter_8188e, dm->bb_swing_idx_cck_base: %d, dm->bb_swing_idx_ofdm_base: %d\n", cali_info->bb_swing_idx_cck_base, cali_info->bb_swing_idx_ofdm_base);
	/*
		if (!dm->rf_calibrate_info.tm_trigger) {
			odm_set_rf_reg(dm, RF_PATH_A, c.thermal_reg_addr, BIT(17) | BIT(16), 0x3);
			dm->rf_calibrate_info.tm_trigger = 1;
			return;
		}
	*/
	thermal_value = (u8)odm_get_rf_reg(dm, RF_PATH_A, c.thermal_reg_addr, 0xfc00);	/* 0x42: RF Reg[15:10] 88E */
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	if (!thermal_value || !dm->rf_calibrate_info.txpowertrack_control)
#else
	if (!dm->rf_calibrate_info.txpowertrack_control)
#endif
		return;

	/* 4 3. Initialize ThermalValues of rf_calibrate_info */

	if (!dm->rf_calibrate_info.thermal_value) {
		dm->rf_calibrate_info.thermal_value_lck = thermal_value;
		dm->rf_calibrate_info.thermal_value_iqk = thermal_value;
	}

	if (dm->rf_calibrate_info.is_reloadtxpowerindex)
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "reload ofdm index for band switch\n");

	/* 4 4. Calculate average thermal meter */

	dm->rf_calibrate_info.thermal_value_avg[dm->rf_calibrate_info.thermal_value_avg_index] = thermal_value;
	dm->rf_calibrate_info.thermal_value_avg_index++;
	if (dm->rf_calibrate_info.thermal_value_avg_index == c.average_thermal_num)
		dm->rf_calibrate_info.thermal_value_avg_index = 0;

	for (i = 0; i < c.average_thermal_num; i++) {
		if (dm->rf_calibrate_info.thermal_value_avg[i]) {
			thermal_value_avg += dm->rf_calibrate_info.thermal_value_avg[i];
			thermal_value_avg_count++;
		}
	}

	if (thermal_value_avg_count) {
		/* Give the new thermo value a weighting */
		thermal_value_avg += (thermal_value * 4);

		thermal_value = (u8)(thermal_value_avg / (thermal_value_avg_count + 4));
		cali_info->thermal_value_delta = thermal_value - priv->pmib->dot11RFEntry.ther;
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "AVG Thermal Meter = 0x%x\n", thermal_value);
	}

	/* 4 5. Calculate delta, delta_LCK, delta_IQK. */

	delta	  = (thermal_value > dm->rf_calibrate_info.thermal_value) ? (thermal_value - dm->rf_calibrate_info.thermal_value) : (dm->rf_calibrate_info.thermal_value - thermal_value);
	delta_LCK = (thermal_value > dm->rf_calibrate_info.thermal_value_lck) ? (thermal_value - dm->rf_calibrate_info.thermal_value_lck) : (dm->rf_calibrate_info.thermal_value_lck - thermal_value);
	delta_IQK = (thermal_value > dm->rf_calibrate_info.thermal_value_iqk) ? (thermal_value - dm->rf_calibrate_info.thermal_value_iqk) : (dm->rf_calibrate_info.thermal_value_iqk - thermal_value);

	/* 4 6. If necessary, do LCK. */
	if (!(dm->support_ic_type & ODM_RTL8821)) {
		/*if((delta_LCK > hal_data->delta_lck) && (hal_data->delta_lck != 0))*/
		if ((delta_LCK >= c.threshold_iqk) && (!iqk_info->rfk_forbidden)) {
			/*Delta temperature is equal to or larger than 20 centigrade.*/
			dm->rf_calibrate_info.thermal_value_lck = thermal_value;
			(*c.phy_lc_calibrate)(dm);
		}
	}

	/* 3 7. If necessary, move the index of swing table to adjust Tx power. */

	if (delta > 0 && dm->rf_calibrate_info.txpowertrack_control) {

		delta = (thermal_value > dm->priv->pmib->dot11RFEntry.ther) ? (thermal_value - dm->priv->pmib->dot11RFEntry.ther) : (dm->priv->pmib->dot11RFEntry.ther - thermal_value);

		/* 4 7.1 The Final Power index = BaseIndex + power_index_offset */

		if (thermal_value > dm->priv->pmib->dot11RFEntry.ther) {
			CALCULATE_SWINGTALBE_OFFSET(offset, POWER_INC, index_mapping_NUM_88E, delta);
			dm->rf_calibrate_info.delta_power_index_last = dm->rf_calibrate_info.delta_power_index;
			dm->rf_calibrate_info.delta_power_index =  delta_swing_table_idx[POWER_INC][offset];

		} else {

			CALCULATE_SWINGTALBE_OFFSET(offset, POWER_DEC, index_mapping_NUM_88E, delta);
			dm->rf_calibrate_info.delta_power_index_last = dm->rf_calibrate_info.delta_power_index;
			dm->rf_calibrate_info.delta_power_index = (-1) * delta_swing_table_idx[POWER_DEC][offset];
		}

		if (dm->rf_calibrate_info.delta_power_index == dm->rf_calibrate_info.delta_power_index_last)
			dm->rf_calibrate_info.power_index_offset = 0;
		else
			dm->rf_calibrate_info.power_index_offset = dm->rf_calibrate_info.delta_power_index - dm->rf_calibrate_info.delta_power_index_last;

		for (i = 0; i < rf; i++)
			dm->rf_calibrate_info.OFDM_index[i] = cali_info->bb_swing_idx_ofdm_base + dm->rf_calibrate_info.power_index_offset;
		dm->rf_calibrate_info.CCK_index = cali_info->bb_swing_idx_cck_base + dm->rf_calibrate_info.power_index_offset;

		cali_info->bb_swing_idx_cck = dm->rf_calibrate_info.CCK_index;
		cali_info->bb_swing_idx_ofdm[RF_PATH_A] = dm->rf_calibrate_info.OFDM_index[RF_PATH_A];

		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "The 'CCK' final index(%d) = BaseIndex(%d) + power_index_offset(%d)\n", cali_info->bb_swing_idx_cck, cali_info->bb_swing_idx_cck_base, dm->rf_calibrate_info.power_index_offset);
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "The 'OFDM' final index(%d) = BaseIndex(%d) + power_index_offset(%d)\n", cali_info->bb_swing_idx_ofdm[RF_PATH_A], cali_info->bb_swing_idx_ofdm_base, dm->rf_calibrate_info.power_index_offset);

		/* 4 7.1 Handle boundary conditions of index. */


		for (i = 0; i < rf; i++) {
			if (dm->rf_calibrate_info.OFDM_index[i] > OFDM_max_index)
				dm->rf_calibrate_info.OFDM_index[i] = OFDM_max_index;
			else if (dm->rf_calibrate_info.OFDM_index[i] < 0)
				dm->rf_calibrate_info.OFDM_index[i] = 0;
		}

		if (dm->rf_calibrate_info.CCK_index > c.swing_table_size_cck - 1)
			dm->rf_calibrate_info.CCK_index = c.swing_table_size_cck - 1;
		else if (dm->rf_calibrate_info.CCK_index < 0)
			dm->rf_calibrate_info.CCK_index = 0;
	} else {
		RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"The thermal meter is unchanged or TxPowerTracking OFF: thermal_value: %d, dm->rf_calibrate_info.thermal_value: %d)\n", thermal_value, dm->rf_calibrate_info.thermal_value);
		dm->rf_calibrate_info.power_index_offset = 0;
	}
	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"TxPowerTracking: [CCK] Swing Current index: %d, Swing base index: %d\n", dm->rf_calibrate_info.CCK_index, cali_info->bb_swing_idx_cck_base);

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK,"TxPowerTracking: [OFDM] Swing Current index: %d, Swing base index: %d\n", dm->rf_calibrate_info.OFDM_index[RF_PATH_A], cali_info->bb_swing_idx_ofdm_base);

	if (dm->rf_calibrate_info.power_index_offset != 0 && dm->rf_calibrate_info.txpowertrack_control) {
		/* 4 7.2 Configure the Swing Table to adjust Tx Power. */

		dm->rf_calibrate_info.is_tx_power_changed = true; /* Always true after Tx Power is adjusted by power tracking. */
		/*  */
		/* 2012/04/23 MH According to Luke's suggestion, we can not write BB digital */
		/* to increase TX power. Otherwise, EVM will be bad. */
		/*  */
		/* 2012/04/25 MH Add for tx power tracking to set tx power in tx agc for 88E. */
		if (thermal_value > dm->rf_calibrate_info.thermal_value) {
			/* RF_DBG(dm,DBG_RF_TX_PWR_TRACK, */
			/*	"Temperature Increasing: delta_pi: %d, delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n", */
			/*	dm->rf_calibrate_info.power_index_offset, delta, thermal_value, hal_data->eeprom_thermal_meter, dm->rf_calibrate_info.thermal_value); */
		} else if (thermal_value < dm->rf_calibrate_info.thermal_value) { /* Low temperature */
			/* RF_DBG(dm,DBG_RF_TX_PWR_TRACK, */
			/*	"Temperature Decreasing: delta_pi: %d, delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n", */
			/*		dm->rf_calibrate_info.power_index_offset, delta, thermal_value, hal_data->eeprom_thermal_meter, dm->rf_calibrate_info.thermal_value); */
		}
		if (thermal_value > dm->priv->pmib->dot11RFEntry.ther)
		{
			/*				RF_DBG(dm,DBG_RF_TX_PWR_TRACK,"Temperature(%d) hugher than PG value(%d), increases the power by tx_agc\n", thermal_value, hal_data->eeprom_thermal_meter); */
			(*c.odm_tx_pwr_track_set_pwr)(dm, TXAGC, 0, 0);
		} else {
			/*			RF_DBG(dm,DBG_RF_TX_PWR_TRACK,"Temperature(%d) lower than PG value(%d), increases the power by tx_agc\n", thermal_value, hal_data->eeprom_thermal_meter); */
			(*c.odm_tx_pwr_track_set_pwr)(dm, BBSWING, RF_PATH_A, indexforchannel);
			if (is2T)
				(*c.odm_tx_pwr_track_set_pwr)(dm, BBSWING, RF_PATH_B, indexforchannel);
		}

		cali_info->bb_swing_idx_cck_base = cali_info->bb_swing_idx_cck;
		cali_info->bb_swing_idx_ofdm_base = cali_info->bb_swing_idx_ofdm[RF_PATH_A];
		dm->rf_calibrate_info.thermal_value = thermal_value;

	}

	RF_DBG(dm, DBG_RF_TX_PWR_TRACK, "<===dm_TXPowerTrackingCallback_ThermalMeter_8188E\n");

	dm->rf_calibrate_info.tx_powercount = 0;
}

/* 3============================================================
 * 3 IQ Calibration
 * 3============================================================ */

void
odm_reset_iqk_result(
	void		*dm_void
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
	struct dm_struct	*dm
)
{
	struct dm_iqk_info	*iqk_info = &dm->IQK_info;

	if ((dm->is_linked) && (!iqk_info->rfk_forbidden)) {
		if ((*dm->channel != dm->pre_channel) && (!*dm->is_scan_in_process)) {
			dm->pre_channel = *dm->channel;
			dm->linked_interval = 0;
		}

		if (dm->linked_interval < 3)
			dm->linked_interval++;

		if (dm->linked_interval == 2)
			halrf_iqk_trigger(dm, false);
	} else
		dm->linked_interval = 0;

}

void phydm_rf_init(void		*dm_void)
{
	struct dm_struct		*dm = (struct dm_struct *)dm_void;
	odm_txpowertracking_init(dm);

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
#if (RTL8814A_SUPPORT == 1)
	if (dm->support_ic_type & ODM_RTL8814A)
		phy_iq_calibrate_8814a_init(dm);
#endif
#endif

}

void phydm_rf_watchdog(void		*dm_void)
{
	struct dm_struct		*dm = (struct dm_struct *)dm_void;

	odm_txpowertracking_check(dm);
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		odm_iq_calibrate(dm);
#endif
}
