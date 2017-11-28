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
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
#if RTL8812A_SUPPORT
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	/* if (IS_HARDWARE_TYPE_8812(p_dm_odm->adapter)) */
	if (p_dm_odm->support_ic_type == ODM_RTL8812)
		configure_txpower_track_8812a(p_config);
	/* else */
#endif
#endif

#if RTL8814A_SUPPORT
	if (p_dm_odm->support_ic_type == ODM_RTL8814A)
		configure_txpower_track_8814a(p_config);
#endif


#if RTL8188E_SUPPORT
	if (p_dm_odm->support_ic_type == ODM_RTL8188E)
		configure_txpower_track_8188e(p_config);
#endif

#if RTL8197F_SUPPORT
	if (p_dm_odm->support_ic_type == ODM_RTL8197F)
		configure_txpower_track_8197f(p_config);
#endif

#if RTL8822B_SUPPORT
	if (p_dm_odm->support_ic_type == ODM_RTL8822B)
		configure_txpower_track_8822b(p_config);
#endif


}

#if (RTL8192E_SUPPORT == 1)
void
odm_txpowertracking_callback_thermal_meter_92e(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	void		*p_dm_void
#else
	struct _ADAPTER	*adapter
#endif
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8	thermal_value = 0, delta, delta_IQK, delta_LCK, channel, is_decrease, rf_mimo_mode;
	u8	thermal_value_avg_count = 0;
	u8     OFDM_min_index = 10; /* OFDM BB Swing should be less than +2.5dB, which is required by Arthur */
	s8	OFDM_index[2], index ;
	u32	thermal_value_avg = 0, reg0x18;
	u32	i = 0, j = 0, rf;
	s32	value32, CCK_index = 0, ele_A, ele_D, ele_C, X, Y;
	struct rtl8192cd_priv	*priv = p_dm_odm->priv;

	rf_mimo_mode = p_dm_odm->rf_type;
	/* ODM_RT_TRACE(p_dm_odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("%s:%d rf_mimo_mode:%d\n", __FUNCTION__, __LINE__, rf_mimo_mode)); */

#ifdef MP_TEST
	if ((OPMODE & WIFI_MP_STATE) || priv->pshare->rf_ft_var.mp_specific) {
		channel = priv->pshare->working_channel;
		if (priv->pshare->mp_txpwr_tracking == false)
			return;
	} else
#endif
	{
		channel = (priv->pmib->dot11RFEntry.dot11channel);
	}

	thermal_value = (unsigned char)odm_get_rf_reg(p_dm_odm, RF_PATH_A, ODM_RF_T_METER_92E, 0xfc00);	/* 0x42: RF Reg[15:10] 88E */
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", thermal_value, priv->pshare->thermal_value, priv->pmib->dot11RFEntry.ther));


	switch (rf_mimo_mode) {
	case MIMO_1T1R:
		rf = 1;
		break;
	case MIMO_2T2R:
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
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("PathA 0xC80[31:22] = 0x%x, OFDM_index=%d\n", ele_D, OFDM_index[0]));
			break;
		}
	}

	/* Query OFDM path B default setting */
	if (rf_mimo_mode == MIMO_2T2R) {
		ele_D = phy_query_bb_reg(priv, REG_OFDM_0_XB_TX_IQ_IMBALANCE, MASKOFDM_D);
		for (i = 0; i < OFDM_TABLE_SIZE_92E; i++) {
			if (ele_D == (ofdm_swing_table_92e[i] >> 22)) {
				OFDM_index[1] = (unsigned char)i;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("PathB 0xC88[31:22] = 0x%x, OFDM_index=%d\n", ele_D, OFDM_index[1]));
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
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("AVG Thermal Meter = 0x%x\n", thermal_value));
		}
	}

	/* Initialize */
	if (!priv->pshare->thermal_value) {
		priv->pshare->thermal_value = priv->pmib->dot11RFEntry.ther;
		priv->pshare->thermal_value_iqk = thermal_value;
		priv->pshare->thermal_value_lck = thermal_value;
	}

	if (thermal_value != priv->pshare->thermal_value) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\n******** START POWER TRACKING ********\n"));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", thermal_value, priv->pshare->thermal_value, priv->pmib->dot11RFEntry.ther));

		delta = RTL_ABS(thermal_value, priv->pmib->dot11RFEntry.ther);
		delta_IQK = RTL_ABS(thermal_value, priv->pshare->thermal_value_iqk);
		delta_LCK = RTL_ABS(thermal_value, priv->pshare->thermal_value_lck);
		is_decrease = ((thermal_value < priv->pmib->dot11RFEntry.ther) ? 1 : 0);

#ifdef _TRACKING_TABLE_FILE
		if (priv->pshare->rf_ft_var.pwr_track_file) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("diff: (%s)%d ==> get index from table : %d)\n", (is_decrease ? "-" : "+"), delta, get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0)));

			if (is_decrease) {
				for (i = 0; i < rf; i++) {
					OFDM_index[i] = priv->pshare->OFDM_index0[i] + get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0);
					OFDM_index[i] = ((OFDM_index[i] > (OFDM_TABLE_SIZE_92E- 1)) ? (OFDM_TABLE_SIZE_92E - 1) : OFDM_index[i]);
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> decrese power ---> new OFDM_INDEX:%d (%d + %d)\n", OFDM_index[i], priv->pshare->OFDM_index0[i], get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0)));
					CCK_index = priv->pshare->CCK_index0 + get_tx_tracking_index(priv, channel, i, delta, is_decrease, 1);
					CCK_index = ((CCK_index > (CCK_TABLE_SIZE_92E - 1)) ? (CCK_TABLE_SIZE_92E - 1) : CCK_index);
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> Decrese power ---> new CCK_INDEX:%d (%d + %d)\n",  CCK_index, priv->pshare->CCK_index0, get_tx_tracking_index(priv, channel, i, delta, is_decrease, 1)));
				}
			} else {
				for (i = 0; i < rf; i++) {
					OFDM_index[i] = priv->pshare->OFDM_index0[i] - get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0);
					OFDM_index[i] = ((OFDM_index[i] < OFDM_min_index) ?  OFDM_min_index : OFDM_index[i]);
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> Increse power ---> new OFDM_INDEX:%d (%d - %d)\n", OFDM_index[i], priv->pshare->OFDM_index0[i], get_tx_tracking_index(priv, channel, i, delta, is_decrease, 0)));
					CCK_index = priv->pshare->CCK_index0 - get_tx_tracking_index(priv, channel, i, delta, is_decrease, 1);
					CCK_index = ((CCK_index < 0) ? 0 : CCK_index);
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> Increse power ---> new CCK_INDEX:%d (%d - %d)\n", CCK_index, priv->pshare->CCK_index0, get_tx_tracking_index(priv, channel, i, delta, is_decrease, 1)));
				}
			}
		}
#endif /* CFG_TRACKING_TABLE_FILE */

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("ofdm_swing_table_92e[(unsigned int)OFDM_index[0]] = %x\n", ofdm_swing_table_92e[(unsigned int)OFDM_index[0]]));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("ofdm_swing_table_92e[(unsigned int)OFDM_index[1]] = %x\n", ofdm_swing_table_92e[(unsigned int)OFDM_index[1]]));

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

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("0xc80 = 0x%x\n", phy_query_bb_reg(priv, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD)));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("0xc88 = 0x%x\n", phy_query_bb_reg(priv, REG_OFDM_0_XB_TX_IQ_IMBALANCE, MASKDWORD)));

		if (delta_IQK > 3) {
			priv->pshare->thermal_value_iqk = thermal_value;
#ifdef MP_TEST
			if (!(priv->pshare->rf_ft_var.mp_specific && (OPMODE & (WIFI_MP_CTX_BACKGROUND | WIFI_MP_CTX_PACKET))))
#endif
				phy_iq_calibrate_8192e(p_dm_odm, false);
		}

		if (delta_LCK > 8) {
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

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\n******** END:%s() ********\n", __FUNCTION__));
}
#endif



#if (RTL8197F_SUPPORT == 1 || RTL8822B_SUPPORT == 1)
void
odm_txpowertracking_callback_thermal_meter_jaguar_series3(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	void		*p_dm_void
#else
	struct _ADAPTER	*adapter
#endif
)
{
#if 1
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			thermal_value = 0, delta, delta_LCK, delta_IQK, channel, is_increase;
	u8			thermal_value_avg_count = 0, p = 0, i = 0;
	u32			thermal_value_avg = 0;
	struct rtl8192cd_priv		*priv = p_dm_odm->priv;
	struct _TXPWRTRACK_CFG	c;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);

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
	if ((OPMODE & WIFI_MP_STATE) || priv->pshare->rf_ft_var.mp_specific) {
		channel = priv->pshare->working_channel;
		if (priv->pshare->mp_txpwr_tracking == false)
			return;
	} else
#endif
	{
		channel = (priv->pmib->dot11RFEntry.dot11channel);
	}

	configure_txpower_track(p_dm_odm, &c);

	(*c.get_delta_all_swing_table)(p_dm_odm, (u8 **)&delta_swing_table_idx_tup_a, (u8 **)&delta_swing_table_idx_tdown_a,
		(u8 **)&delta_swing_table_idx_tup_b, (u8 **)&delta_swing_table_idx_tdown_b,
		(u8 **)&delta_swing_table_idx_tup_cck_a, (u8 **)&delta_swing_table_idx_tdown_cck_a,
		(u8 **)&delta_swing_table_idx_tup_cck_b, (u8 **)&delta_swing_table_idx_tdown_cck_b);

	thermal_value = (u8)odm_get_rf_reg(p_dm_odm, ODM_RF_PATH_A, c.thermal_reg_addr, 0xfc00); /*0x42: RF Reg[15:10] 88E*/

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("Readback Thermal Meter = 0x%x(%d) EEPROMthermalmeter 0x%x(%d)\n"
		, thermal_value, thermal_value, priv->pmib->dot11RFEntry.ther, priv->pmib->dot11RFEntry.ther));

	/* Initialize */
	if (!p_dm_odm->rf_calibrate_info.thermal_value)
		p_dm_odm->rf_calibrate_info.thermal_value = priv->pmib->dot11RFEntry.ther;

	if (!p_dm_odm->rf_calibrate_info.thermal_value_lck)
		p_dm_odm->rf_calibrate_info.thermal_value_lck = priv->pmib->dot11RFEntry.ther;

	if (!p_dm_odm->rf_calibrate_info.thermal_value_iqk)
		p_dm_odm->rf_calibrate_info.thermal_value_iqk = priv->pmib->dot11RFEntry.ther;

	/* calculate average thermal meter */
	p_dm_odm->rf_calibrate_info.thermal_value_avg[p_dm_odm->rf_calibrate_info.thermal_value_avg_index] = thermal_value;
	p_dm_odm->rf_calibrate_info.thermal_value_avg_index++;

	if (p_dm_odm->rf_calibrate_info.thermal_value_avg_index == c.average_thermal_num)   /*Average times =  c.average_thermal_num*/
		p_dm_odm->rf_calibrate_info.thermal_value_avg_index = 0;

	for (i = 0; i < c.average_thermal_num; i++) {
		if (p_dm_odm->rf_calibrate_info.thermal_value_avg[i]) {
			thermal_value_avg += p_dm_odm->rf_calibrate_info.thermal_value_avg[i];
			thermal_value_avg_count++;
		}
	}

	if (thermal_value_avg_count) {/*Calculate Average thermal_value after average enough times*/
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("thermal_value_avg=0x%x(%d)  thermal_value_avg_count = %d\n"
			, thermal_value_avg, thermal_value_avg, thermal_value_avg_count));

		thermal_value = (u8)(thermal_value_avg / thermal_value_avg_count);

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("AVG Thermal Meter = 0x%X(%d), EEPROMthermalmeter = 0x%X(%d)\n", thermal_value, thermal_value, priv->pmib->dot11RFEntry.ther, priv->pmib->dot11RFEntry.ther));
	}

	/*4 Calculate delta, delta_LCK, delta_IQK.*/
	delta = RTL_ABS(thermal_value, priv->pmib->dot11RFEntry.ther);
	delta_LCK = RTL_ABS(thermal_value, p_dm_odm->rf_calibrate_info.thermal_value_lck);
	delta_IQK = RTL_ABS(thermal_value, p_dm_odm->rf_calibrate_info.thermal_value_iqk);
	is_increase = ((thermal_value < priv->pmib->dot11RFEntry.ther) ? 0 : 1);

	if (delta > 29) { /* power track table index(thermal diff.) upper bound*/
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("delta(%d) > 29, set delta to 29\n", delta));
		delta = 29;
	}


	/*4 if necessary, do LCK.*/

	if (delta_LCK > c.threshold_iqk) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("delta_LCK(%d) >= threshold_iqk(%d)\n", delta_LCK, c.threshold_iqk));
		p_dm_odm->rf_calibrate_info.thermal_value_lck = thermal_value;
		if (c.phy_lc_calibrate)
			(*c.phy_lc_calibrate)(p_dm_odm);
	}

	if (delta_IQK > c.threshold_iqk) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("delta_IQK(%d) >= threshold_iqk(%d)\n", delta_IQK, c.threshold_iqk));
		p_dm_odm->rf_calibrate_info.thermal_value_iqk = thermal_value;
		if (c.do_iqk)
			(*c.do_iqk)(p_dm_odm, true, 0, 0);
	}

	if (!priv->pmib->dot11RFEntry.ther)	/*Don't do power tracking since no calibrated thermal value*/
		return;

	/*4 Do Power Tracking*/

	if (thermal_value != p_dm_odm->rf_calibrate_info.thermal_value) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			     ("\n\n******** START POWER TRACKING ********\n"));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("Readback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n",
			thermal_value, p_dm_odm->rf_calibrate_info.thermal_value, priv->pmib->dot11RFEntry.ther));

#ifdef _TRACKING_TABLE_FILE
		if (priv->pshare->rf_ft_var.pwr_track_file) {
			if (is_increase) {			/*thermal is higher than base*/
				for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++) {
					switch (p) {
					case ODM_RF_PATH_B:
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tup_b[%d] = %d delta_swing_table_idx_tup_cck_b[%d] = %d\n", delta, delta_swing_table_idx_tup_b[delta], delta, delta_swing_table_idx_tup_cck_b[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_b[delta];
						p_rf_calibrate_info->absolute_cck_swing_idx[p] = delta_swing_table_idx_tup_cck_b[delta];
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and pRF->absolute_ofdm_swing_idx[ODM_RF_PATH_B] = %d pRF->absolute_cck_swing_idx[ODM_RF_PATH_B] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p], p_rf_calibrate_info->absolute_cck_swing_idx[p]));
						break;

					case ODM_RF_PATH_C:
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tup_c[%d] = %d delta_swing_table_idx_tup_cck_c[%d] = %d\n", delta, delta_swing_table_idx_tup_c[delta], delta, delta_swing_table_idx_tup_cck_c[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_c[delta];
						p_rf_calibrate_info->absolute_cck_swing_idx[p] = delta_swing_table_idx_tup_cck_c[delta];
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and pRF->absolute_ofdm_swing_idx[ODM_RF_PATH_C] = %d pRF->absolute_cck_swing_idx[ODM_RF_PATH_C] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p], p_rf_calibrate_info->absolute_cck_swing_idx[p]));
						break;

					case ODM_RF_PATH_D:
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tup_d[%d] = %d delta_swing_table_idx_tup_cck_d[%d] = %d\n", delta, delta_swing_table_idx_tup_d[delta], delta, delta_swing_table_idx_tup_cck_d[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_d[delta];
						p_rf_calibrate_info->absolute_cck_swing_idx[p] = delta_swing_table_idx_tup_cck_d[delta];
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and pRF->absolute_ofdm_swing_idx[ODM_RF_PATH_D] = %d pRF->absolute_cck_swing_idx[ODM_RF_PATH_D] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p], p_rf_calibrate_info->absolute_cck_swing_idx[p]));
						break;
					default:
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tup_a[%d] = %d delta_swing_table_idx_tup_cck_a[%d] = %d\n", delta, delta_swing_table_idx_tup_a[delta], delta, delta_swing_table_idx_tup_cck_a[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_a[delta];
						p_rf_calibrate_info->absolute_cck_swing_idx[p] = delta_swing_table_idx_tup_cck_a[delta];
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and pRF->absolute_ofdm_swing_idx[ODM_RF_PATH_A] = %d pRF->absolute_cck_swing_idx[ODM_RF_PATH_A] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p], p_rf_calibrate_info->absolute_cck_swing_idx[p]));
						break;
					}
				}
			} else {			/* thermal is lower than base*/
				for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++) {
					switch (p) {
					case ODM_RF_PATH_B:
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tdown_b[%d] = %d   delta_swing_table_idx_tdown_cck_b[%d] = %d\n", delta, delta_swing_table_idx_tdown_b[delta], delta, delta_swing_table_idx_tdown_cck_b[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_b[delta];
						p_rf_calibrate_info->absolute_cck_swing_idx[p] = -1 * delta_swing_table_idx_tdown_cck_b[delta];
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and pRF->absolute_ofdm_swing_idx[ODM_RF_PATH_B] = %d   pRF->absolute_cck_swing_idx[ODM_RF_PATH_B] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p], p_rf_calibrate_info->absolute_cck_swing_idx[p]));
						break;

					case ODM_RF_PATH_C:
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tdown_c[%d] = %d   delta_swing_table_idx_tdown_cck_c[%d] = %d\n", delta, delta_swing_table_idx_tdown_c[delta], delta, delta_swing_table_idx_tdown_cck_c[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_c[delta];
						p_rf_calibrate_info->absolute_cck_swing_idx[p] = -1 * delta_swing_table_idx_tdown_cck_c[delta];
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and pRF->absolute_ofdm_swing_idx[ODM_RF_PATH_C] = %d   pRF->absolute_cck_swing_idx[ODM_RF_PATH_C] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p], p_rf_calibrate_info->absolute_cck_swing_idx[p]));
						break;

					case ODM_RF_PATH_D:
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tdown_d[%d] = %d   delta_swing_table_idx_tdown_cck_d[%d] = %d\n", delta, delta_swing_table_idx_tdown_d[delta], delta, delta_swing_table_idx_tdown_cck_d[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_d[delta];
						p_rf_calibrate_info->absolute_cck_swing_idx[p] = -1 * delta_swing_table_idx_tdown_cck_d[delta];
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and pRF->absolute_ofdm_swing_idx[ODM_RF_PATH_D] = %d   pRF->absolute_cck_swing_idx[ODM_RF_PATH_D] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p], p_rf_calibrate_info->absolute_cck_swing_idx[p]));
						break;

					default:
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tdown_a[%d] = %d   delta_swing_table_idx_tdown_cck_a[%d] = %d\n", delta, delta_swing_table_idx_tdown_a[delta], delta, delta_swing_table_idx_tdown_cck_a[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_a[delta];
						p_rf_calibrate_info->absolute_cck_swing_idx[p] = -1 * delta_swing_table_idx_tdown_cck_a[delta];
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and pRF->absolute_ofdm_swing_idx[ODM_RF_PATH_A] = %d   pRF->absolute_cck_swing_idx[ODM_RF_PATH_A] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p], p_rf_calibrate_info->absolute_cck_swing_idx[p]));
						break;
					}
				}
			}

			if (is_increase) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> increse power --->\n"));
				if (GET_CHIP_VER(priv) == VERSION_8197F) {
					for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(p_dm_odm, BBSWING, p, 0);
				} else if (GET_CHIP_VER(priv) == VERSION_8822B) {
					for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(p_dm_odm, MIX_MODE, p, 0);
				}
			} else {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> decrese power --->\n"));
				if (GET_CHIP_VER(priv) == VERSION_8197F) {
					for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(p_dm_odm, BBSWING, p, 0);
				} else if (GET_CHIP_VER(priv) == VERSION_8822B) {
					for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++)
						(*c.odm_tx_pwr_track_set_pwr)(p_dm_odm, MIX_MODE, p, 0);
				}
			}
		}
#endif

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\n******** END:%s() ********\n\n", __func__));
		/*update thermal meter value*/
		p_dm_odm->rf_calibrate_info.thermal_value =  thermal_value;

	}

#endif
}
#endif

/*#if (RTL8814A_SUPPORT == 1)*/
#if (RTL8814A_SUPPORT == 1)

void
odm_txpowertracking_callback_thermal_meter_jaguar_series2(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	void		*p_dm_void
#else
	struct _ADAPTER	*adapter
#endif
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			thermal_value = 0, delta, delta_LCK, delta_IQK, channel, is_increase;
	u8			thermal_value_avg_count = 0, p = 0, i = 0;
	u32			thermal_value_avg = 0, reg0x18;
	u32			bb_swing_reg[4] = {REG_A_TX_SCALE_JAGUAR, REG_B_TX_SCALE_JAGUAR, REG_C_TX_SCALE_JAGUAR2, REG_D_TX_SCALE_JAGUAR2};
	s32			ele_D;
	u32			bb_swing_idx;
	struct rtl8192cd_priv	*priv = p_dm_odm->priv;
	struct _TXPWRTRACK_CFG	c;
	bool			is_tssi_enable = false;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);

	/* 4 1. The following TWO tables decide the final index of OFDM/CCK swing table. */
	u8			*delta_swing_table_idx_tup_a = NULL, *delta_swing_table_idx_tdown_a = NULL;
	u8			*delta_swing_table_idx_tup_b = NULL, *delta_swing_table_idx_tdown_b = NULL;
	/* for 8814 add by Yu Chen */
	u8			*delta_swing_table_idx_tup_c = NULL, *delta_swing_table_idx_tdown_c = NULL;
	u8			*delta_swing_table_idx_tup_d = NULL, *delta_swing_table_idx_tdown_d = NULL;

#ifdef MP_TEST
	if ((OPMODE & WIFI_MP_STATE) || priv->pshare->rf_ft_var.mp_specific) {
		channel = priv->pshare->working_channel;
		if (priv->pshare->mp_txpwr_tracking == false)
			return;
	} else
#endif
	{
		channel = (priv->pmib->dot11RFEntry.dot11channel);
	}

	configure_txpower_track(p_dm_odm, &c);
	p_rf_calibrate_info->default_ofdm_index = priv->pshare->OFDM_index0[ODM_RF_PATH_A];

	(*c.get_delta_swing_table)(p_dm_odm, (u8 **)&delta_swing_table_idx_tup_a, (u8 **)&delta_swing_table_idx_tdown_a,
		(u8 **)&delta_swing_table_idx_tup_b, (u8 **)&delta_swing_table_idx_tdown_b);

	if (p_dm_odm->support_ic_type & ODM_RTL8814A)	/* for 8814 path C & D */
		(*c.get_delta_swing_table8814only)(p_dm_odm, (u8 **)&delta_swing_table_idx_tup_c, (u8 **)&delta_swing_table_idx_tdown_c,
			(u8 **)&delta_swing_table_idx_tup_d, (u8 **)&delta_swing_table_idx_tdown_d);

	thermal_value = (u8)odm_get_rf_reg(p_dm_odm, ODM_RF_PATH_A, c.thermal_reg_addr, 0xfc00); /* 0x42: RF Reg[15:10] 88E */
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("\nReadback Thermal Meter = 0x%x, pre thermal meter 0x%x, EEPROMthermalmeter 0x%x\n", thermal_value, p_dm_odm->rf_calibrate_info.thermal_value, priv->pmib->dot11RFEntry.ther));

	/* Initialize */
	if (!p_dm_odm->rf_calibrate_info.thermal_value)
		p_dm_odm->rf_calibrate_info.thermal_value = priv->pmib->dot11RFEntry.ther;

	if (!p_dm_odm->rf_calibrate_info.thermal_value_lck)
		p_dm_odm->rf_calibrate_info.thermal_value_lck = priv->pmib->dot11RFEntry.ther;

	if (!p_dm_odm->rf_calibrate_info.thermal_value_iqk)
		p_dm_odm->rf_calibrate_info.thermal_value_iqk = priv->pmib->dot11RFEntry.ther;

	is_tssi_enable = (bool)odm_get_rf_reg(p_dm_odm, ODM_RF_PATH_A, REG_RF_TX_GAIN_OFFSET, BIT(7));	/* check TSSI enable */

	/* 4 Query OFDM BB swing default setting 	Bit[31:21] */
	for (p = ODM_RF_PATH_A ; p < c.rf_path_count ; p++) {
		ele_D = odm_get_bb_reg(p_dm_odm, bb_swing_reg[p], 0xffe00000);
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("0x%x:0x%x ([31:21] = 0x%x)\n", bb_swing_reg[p], odm_get_bb_reg(p_dm_odm, bb_swing_reg[p], MASKDWORD), ele_D));

		for (bb_swing_idx = 0; bb_swing_idx < TXSCALE_TABLE_SIZE; bb_swing_idx++) {/* 4 */
			if (ele_D == tx_scaling_table_jaguar[bb_swing_idx]) {
				p_dm_odm->rf_calibrate_info.OFDM_index[p] = (u8)bb_swing_idx;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
					("OFDM_index[%d]=%d\n", p, p_dm_odm->rf_calibrate_info.OFDM_index[p]));
				break;
			}
		}
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("kfree_offset[%d]=%d\n", p, p_rf_calibrate_info->kfree_offset[p]));

	}

	/* calculate average thermal meter */
	p_dm_odm->rf_calibrate_info.thermal_value_avg[p_dm_odm->rf_calibrate_info.thermal_value_avg_index] = thermal_value;
	p_dm_odm->rf_calibrate_info.thermal_value_avg_index++;
	if (p_dm_odm->rf_calibrate_info.thermal_value_avg_index == c.average_thermal_num)  /* Average times =  c.average_thermal_num */
		p_dm_odm->rf_calibrate_info.thermal_value_avg_index = 0;

	for (i = 0; i < c.average_thermal_num; i++) {
		if (p_dm_odm->rf_calibrate_info.thermal_value_avg[i]) {
			thermal_value_avg += p_dm_odm->rf_calibrate_info.thermal_value_avg[i];
			thermal_value_avg_count++;
		}
	}

	if (thermal_value_avg_count) {            /* Calculate Average thermal_value after average enough times */
		thermal_value = (u8)(thermal_value_avg / thermal_value_avg_count);
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("AVG Thermal Meter = 0x%X, EEPROMthermalmeter = 0x%X\n", thermal_value, priv->pmib->dot11RFEntry.ther));
	}

	/* 4 Calculate delta, delta_LCK, delta_IQK. */
	delta = RTL_ABS(thermal_value, priv->pmib->dot11RFEntry.ther);
	delta_LCK = RTL_ABS(thermal_value, p_dm_odm->rf_calibrate_info.thermal_value_lck);
	delta_IQK = RTL_ABS(thermal_value, p_dm_odm->rf_calibrate_info.thermal_value_iqk);
	is_increase = ((thermal_value < priv->pmib->dot11RFEntry.ther) ? 0 : 1);

	/* 4 if necessary, do LCK. */
	if (!(p_dm_odm->support_ic_type & ODM_RTL8821)) {
		if (delta_LCK > c.threshold_iqk) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("delta_LCK(%d) >= threshold_iqk(%d)\n", delta_LCK, c.threshold_iqk));
			p_dm_odm->rf_calibrate_info.thermal_value_lck = thermal_value;

			/*Use RTLCK, so close power tracking driver LCK*/
#if (RTL8814A_SUPPORT != 1)
			if (!(p_dm_odm->support_ic_type & ODM_RTL8814A)) {
				if (c.phy_lc_calibrate)
					(*c.phy_lc_calibrate)(p_dm_odm);
			}
#endif
		}
	}

	if (delta_IQK > c.threshold_iqk) {
		panic_printk("%s(%d)\n", __FUNCTION__, __LINE__);
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("delta_IQK(%d) >= threshold_iqk(%d)\n", delta_IQK, c.threshold_iqk));
		p_dm_odm->rf_calibrate_info.thermal_value_iqk = thermal_value;
		if (c.do_iqk)
			(*c.do_iqk)(p_dm_odm, true, 0, 0);
	}

	if (!priv->pmib->dot11RFEntry.ther)	/*Don't do power tracking since no calibrated thermal value*/
		return;

	/* 4 Do Power Tracking */

	if (is_tssi_enable == true) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("**********Enter PURE TSSI MODE**********\n"));
		for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++)
			(*c.odm_tx_pwr_track_set_pwr)(p_dm_odm, TSSI_MODE, p, 0);
	} else if (thermal_value != p_dm_odm->rf_calibrate_info.thermal_value) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			     ("\n******** START POWER TRACKING ********\n"));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", thermal_value, p_dm_odm->rf_calibrate_info.thermal_value, priv->pmib->dot11RFEntry.ther));

#ifdef _TRACKING_TABLE_FILE
		if (priv->pshare->rf_ft_var.pwr_track_file) {
			if (is_increase) {		/* thermal is higher than base */
				for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++) {
					switch (p) {
					case ODM_RF_PATH_B:
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tup_b[%d] = %d\n", delta, delta_swing_table_idx_tup_b[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_b[delta];       /* Record delta swing for mix mode power tracking */
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and p_dm_odm->absolute_ofdm_swing_idx[ODM_RF_PATH_B] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
						break;

					case ODM_RF_PATH_C:
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tup_c[%d] = %d\n", delta, delta_swing_table_idx_tup_c[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_c[delta];       /* Record delta swing for mix mode power tracking */
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and p_dm_odm->absolute_ofdm_swing_idx[ODM_RF_PATH_C] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
						break;

					case ODM_RF_PATH_D:
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tup_d[%d] = %d\n", delta, delta_swing_table_idx_tup_d[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_d[delta];       /* Record delta swing for mix mode power tracking */
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and p_dm_odm->absolute_ofdm_swing_idx[ODM_RF_PATH_D] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
						break;

					default:
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tup_a[%d] = %d\n", delta, delta_swing_table_idx_tup_a[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = delta_swing_table_idx_tup_a[delta];        /* Record delta swing for mix mode power tracking */
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is higher and p_dm_odm->absolute_ofdm_swing_idx[ODM_RF_PATH_A] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
						break;
					}
				}
			} else {				/* thermal is lower than base */
				for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++) {
					switch (p) {
					case ODM_RF_PATH_B:
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tdown_b[%d] = %d\n", delta, delta_swing_table_idx_tdown_b[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_b[delta];        /* Record delta swing for mix mode power tracking */
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and p_dm_odm->absolute_ofdm_swing_idx[ODM_RF_PATH_B] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
						break;

					case ODM_RF_PATH_C:
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tdown_c[%d] = %d\n", delta, delta_swing_table_idx_tdown_c[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_c[delta];        /* Record delta swing for mix mode power tracking */
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and p_dm_odm->absolute_ofdm_swing_idx[ODM_RF_PATH_C] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
						break;

					case ODM_RF_PATH_D:
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tdown_d[%d] = %d\n", delta, delta_swing_table_idx_tdown_d[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_d[delta];        /* Record delta swing for mix mode power tracking */
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and p_dm_odm->absolute_ofdm_swing_idx[ODM_RF_PATH_D] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
						break;

					default:
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("delta_swing_table_idx_tdown_a[%d] = %d\n", delta, delta_swing_table_idx_tdown_a[delta]));
						p_rf_calibrate_info->absolute_ofdm_swing_idx[p] = -1 * delta_swing_table_idx_tdown_a[delta];        /* Record delta swing for mix mode power tracking */
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
							("******Temp is lower and p_dm_odm->absolute_ofdm_swing_idx[ODM_RF_PATH_A] = %d\n", p_rf_calibrate_info->absolute_ofdm_swing_idx[p]));
						break;
					}
				}
			}

			if (is_increase) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> increse power --->\n"));
				for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++)
					(*c.odm_tx_pwr_track_set_pwr)(p_dm_odm, MIX_MODE, p, 0);
			} else {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> decrese power --->\n"));
				for (p = ODM_RF_PATH_A; p < c.rf_path_count; p++)
					(*c.odm_tx_pwr_track_set_pwr)(p_dm_odm, MIX_MODE, p, 0);
			}
		}
#endif

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\n******** END:%s() ********\n", __FUNCTION__));
		/* update thermal meter value */
		p_dm_odm->rf_calibrate_info.thermal_value =  thermal_value;

	}
}
#endif

#if (RTL8812A_SUPPORT == 1 || RTL8881A_SUPPORT == 1)
void
odm_txpowertracking_callback_thermal_meter_jaguar_series(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	void		*p_dm_void
#else
	struct _ADAPTER	*adapter
#endif
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	unsigned char			thermal_value = 0, delta, delta_LCK, channel, is_decrease;
	unsigned char			thermal_value_avg_count = 0;
	unsigned int			thermal_value_avg = 0, reg0x18;
	unsigned int			bb_swing_reg[4] = {0xc1c, 0xe1c, 0x181c, 0x1a1c};
	int					ele_D, value32;
	char					OFDM_index[2], index;
	unsigned int			i = 0, j = 0, rf_path, max_rf_path = 2, rf;
	struct rtl8192cd_priv		*priv = p_dm_odm->priv;
	unsigned char			OFDM_min_index = 7; /* OFDM BB Swing should be less than +2.5dB, which is required by Arthur and Mimic */



#ifdef MP_TEST
	if ((OPMODE & WIFI_MP_STATE) || priv->pshare->rf_ft_var.mp_specific) {
		channel = priv->pshare->working_channel;
		if (priv->pshare->mp_txpwr_tracking == false)
			return;
	} else
#endif
	{
		channel = (priv->pmib->dot11RFEntry.dot11channel);
	}

#if RTL8881A_SUPPORT
	if (p_dm_odm->support_ic_type == ODM_RTL8881A) {
		max_rf_path = 1;
		if ((get_bonding_type_8881A() == BOND_8881AM || get_bonding_type_8881A() == BOND_8881AN)
		    && priv->pshare->rf_ft_var.use_intpa8881A && (*p_dm_odm->p_band_type == ODM_BAND_2_4G))
			OFDM_min_index = 6;		/* intPA - upper bond set to +3 dB (base: -2 dB)ot11RFEntry.phy_band_select == PHY_BAND_2G)) */
		else
			OFDM_min_index = 10;		/* OFDM BB Swing should be less than +1dB, which is required by Arthur and Mimic */
	}
#endif


	thermal_value = (unsigned char)phy_query_rf_reg(priv, RF_PATH_A, 0x42, 0xfc00, 1); /* 0x42: RF Reg[15:10] 88E */
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", thermal_value, priv->pshare->thermal_value, priv->pmib->dot11RFEntry.ther));


	/* 4 Query OFDM BB swing default setting 	Bit[31:21] */
	for (rf_path = 0 ; rf_path < max_rf_path ; rf_path++) {
		ele_D = phy_query_bb_reg(priv, bb_swing_reg[rf_path], 0xffe00000);
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("0x%x:0x%x ([31:21] = 0x%x)\n", bb_swing_reg[rf_path], phy_query_bb_reg(priv, bb_swing_reg[rf_path], MASKDWORD), ele_D));
		for (i = 0; i < OFDM_TABLE_SIZE_8812; i++) {/* 4 */
			if (ele_D == ofdm_swing_table_8812[i]) {
				OFDM_index[rf_path] = (unsigned char)i;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("OFDM_index[%d]=%d\n", rf_path, OFDM_index[rf_path]));
				break;
			}
		}
	}
#if 0
	/* Query OFDM path A default setting 	Bit[31:21] */
	ele_D = phy_query_bb_reg(priv, 0xc1c, 0xffe00000);
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("0xc1c:0x%x ([31:21] = 0x%x)\n", phy_query_bb_reg(priv, 0xc1c, MASKDWORD), ele_D));
	for (i = 0; i < OFDM_TABLE_SIZE_8812; i++) {/* 4 */
		if (ele_D == ofdm_swing_table_8812[i]) {
			OFDM_index[0] = (unsigned char)i;
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("OFDM_index[0]=%d\n", OFDM_index[0]));
			break;
		}
	}
	/* Query OFDM path B default setting */
	if (rf == 2) {
		ele_D = phy_query_bb_reg(priv, 0xe1c, 0xffe00000);
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("0xe1c:0x%x ([32:21] = 0x%x)\n", phy_query_bb_reg(priv, 0xe1c, MASKDWORD), ele_D));
		for (i = 0; i < OFDM_TABLE_SIZE_8812; i++) {
			if (ele_D == ofdm_swing_table_8812[i]) {
				OFDM_index[1] = (unsigned char)i;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("OFDM_index[1]=%d\n", OFDM_index[1]));
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
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\n******** START POWER TRACKING ********\n"));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n", thermal_value, priv->pshare->thermal_value, priv->pmib->dot11RFEntry.ther));
		delta = RTL_ABS(thermal_value, priv->pmib->dot11RFEntry.ther);
		delta_LCK = RTL_ABS(thermal_value, priv->pshare->thermal_value_lck);
		is_decrease = ((thermal_value < priv->pmib->dot11RFEntry.ther) ? 1 : 0);
		/* if (*p_dm_odm->p_band_type == ODM_BAND_5G) */
		{
#ifdef _TRACKING_TABLE_FILE
			if (priv->pshare->rf_ft_var.pwr_track_file) {
				for (rf_path = 0; rf_path < max_rf_path; rf_path++) {
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("diff: (%s)%d ==> get index from table : %d)\n", (is_decrease ? "-" : "+"), delta, get_tx_tracking_index(priv, channel, rf_path, delta, is_decrease, 0)));
					if (is_decrease) {
						OFDM_index[rf_path] = priv->pshare->OFDM_index0[rf_path] + get_tx_tracking_index(priv, channel, rf_path, delta, is_decrease, 0);
						OFDM_index[rf_path] = ((OFDM_index[rf_path] > (OFDM_TABLE_SIZE_8812 - 1)) ? (OFDM_TABLE_SIZE_8812 - 1) : OFDM_index[rf_path]);
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> decrese power ---> new OFDM_INDEX:%d (%d + %d)\n", OFDM_index[rf_path], priv->pshare->OFDM_index0[rf_path], get_tx_tracking_index(priv, channel, rf_path, delta, is_decrease, 0)));
#if 0/* RTL8881A_SUPPORT */
						if (p_dm_odm->support_ic_type == ODM_RTL8881A) {
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
						if (p_dm_odm->support_ic_type == ODM_RTL8881A) {
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
						ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, (">>> increse power ---> new OFDM_INDEX:%d (%d - %d)\n", OFDM_index[rf_path], priv->pshare->OFDM_index0[rf_path], get_tx_tracking_index(priv, channel, rf_path, delta, is_decrease, 0)));
					}
				}
			}
#endif
			/* 4 Set new BB swing index */
			for (rf_path = 0; rf_path < max_rf_path; rf_path++) {
				phy_set_bb_reg(priv, bb_swing_reg[rf_path], 0xffe00000, ofdm_swing_table_8812[(unsigned int)OFDM_index[rf_path]]);
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("Readback 0x%x[31:21] = 0x%x, OFDM_index:%d\n", bb_swing_reg[rf_path], phy_query_bb_reg(priv, bb_swing_reg[rf_path], 0xffe00000), OFDM_index[rf_path]));
			}

		}
		if (delta_LCK > 8) {
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
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("\n******** END:%s() ********\n", __FUNCTION__));

		/* update thermal meter value */
		priv->pshare->thermal_value = thermal_value;
		for (rf_path = 0; rf_path < max_rf_path; rf_path++)
			priv->pshare->OFDM_index[rf_path] = OFDM_index[rf_path];
	}
}

#endif


void
odm_txpowertracking_callback_thermal_meter(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	void		*p_dm_void
#else
	struct _ADAPTER	*adapter
#endif
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct odm_rf_calibration_structure	*p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);


#if (RTL8197F_SUPPORT == 1 || RTL8822B_SUPPORT == 1)
	if (p_dm_odm->support_ic_type == ODM_RTL8197F || p_dm_odm->support_ic_type == ODM_RTL8822B) {
		odm_txpowertracking_callback_thermal_meter_jaguar_series3(p_dm_odm);
		return;
	}
#endif
#if (RTL8814A_SUPPORT == 1)		/*use this function to do power tracking after 8814 by YuChen*/
	if (p_dm_odm->support_ic_type & ODM_RTL8814A) {
		odm_txpowertracking_callback_thermal_meter_jaguar_series2(p_dm_odm);
		return;
	}
#endif
#if (RTL8881A_SUPPORT || RTL8812A_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & ODM_RTL8812 || p_dm_odm->support_ic_type & ODM_RTL8881A) {
		odm_txpowertracking_callback_thermal_meter_jaguar_series(p_dm_odm);
		return;
	}
#endif

#if (RTL8192E_SUPPORT == 1)
	if (p_dm_odm->support_ic_type == ODM_RTL8192E) {
		odm_txpowertracking_callback_thermal_meter_92e(p_dm_odm);
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
	bool		is2T = false;
	/*	bool 		bInteralPA = false; */

	u8			OFDM_max_index = 34, rf = (is2T) ? 2 : 1; /* OFDM BB Swing should be less than +3.0dB, which is required by Arthur */
	u8			indexforchannel = 0;/*get_right_chnl_place_for_iqk(p_hal_data->current_channel)*/
	enum            _POWER_DEC_INC { POWER_DEC, POWER_INC };
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->odmpriv;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
#endif

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
	struct rtl8192cd_priv	*priv = p_dm_odm->priv;
#endif

	/* 4 2. Initilization ( 7 steps in total ) */

	configure_txpower_track(p_dm_odm, &c);

	p_dm_odm->rf_calibrate_info.txpowertracking_callback_cnt++; /* cosa add for debug */
	p_dm_odm->rf_calibrate_info.is_txpowertracking_init = true;

#if (MP_DRIVER == 1)
	p_dm_odm->rf_calibrate_info.txpowertrack_control = p_hal_data->txpowertrack_control; /* <Kordan> We should keep updating the control variable according to HalData.
     * <Kordan> rf_calibrate_info.rega24 will be initialized when ODM HW configuring, but MP configures with para files. */
	p_dm_odm->rf_calibrate_info.rega24 = 0x090e1317;
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_AP) && defined(MP_TEST)
	if ((OPMODE & WIFI_MP_STATE) || p_dm_odm->priv->pshare->rf_ft_var.mp_specific) {
		if (p_dm_odm->priv->pshare->mp_txpwr_tracking == false)
			return;
	}
#endif
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("===>odm_txpowertracking_callback_thermal_meter_8188e, p_dm_odm->bb_swing_idx_cck_base: %d, p_dm_odm->bb_swing_idx_ofdm_base: %d\n", p_rf_calibrate_info->bb_swing_idx_cck_base, p_rf_calibrate_info->bb_swing_idx_ofdm_base));
	/*
		if (!p_dm_odm->rf_calibrate_info.tm_trigger) {
			odm_set_rf_reg(p_dm_odm, RF_PATH_A, c.thermal_reg_addr, BIT(17) | BIT(16), 0x3);
			p_dm_odm->rf_calibrate_info.tm_trigger = 1;
			return;
		}
	*/
	thermal_value = (u8)odm_get_rf_reg(p_dm_odm, RF_PATH_A, c.thermal_reg_addr, 0xfc00);	/* 0x42: RF Reg[15:10] 88E */
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	if (!thermal_value || !p_dm_odm->rf_calibrate_info.txpowertrack_control)
#else
	if (!p_dm_odm->rf_calibrate_info.txpowertrack_control)
#endif
		return;

	/* 4 3. Initialize ThermalValues of rf_calibrate_info */

	if (!p_dm_odm->rf_calibrate_info.thermal_value) {
		p_dm_odm->rf_calibrate_info.thermal_value_lck = thermal_value;
		p_dm_odm->rf_calibrate_info.thermal_value_iqk = thermal_value;
	}

	if (p_dm_odm->rf_calibrate_info.is_reloadtxpowerindex)
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("reload ofdm index for band switch\n"));

	/* 4 4. Calculate average thermal meter */

	p_dm_odm->rf_calibrate_info.thermal_value_avg[p_dm_odm->rf_calibrate_info.thermal_value_avg_index] = thermal_value;
	p_dm_odm->rf_calibrate_info.thermal_value_avg_index++;
	if (p_dm_odm->rf_calibrate_info.thermal_value_avg_index == c.average_thermal_num)
		p_dm_odm->rf_calibrate_info.thermal_value_avg_index = 0;

	for (i = 0; i < c.average_thermal_num; i++) {
		if (p_dm_odm->rf_calibrate_info.thermal_value_avg[i]) {
			thermal_value_avg += p_dm_odm->rf_calibrate_info.thermal_value_avg[i];
			thermal_value_avg_count++;
		}
	}

	if (thermal_value_avg_count) {
		/* Give the new thermo value a weighting */
		thermal_value_avg += (thermal_value * 4);

		thermal_value = (u8)(thermal_value_avg / (thermal_value_avg_count + 4));
		p_rf_calibrate_info->thermal_value_delta = thermal_value - priv->pmib->dot11RFEntry.ther;
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("AVG Thermal Meter = 0x%x\n", thermal_value));
	}

	/* 4 5. Calculate delta, delta_LCK, delta_IQK. */

	delta	  = (thermal_value > p_dm_odm->rf_calibrate_info.thermal_value) ? (thermal_value - p_dm_odm->rf_calibrate_info.thermal_value) : (p_dm_odm->rf_calibrate_info.thermal_value - thermal_value);
	delta_LCK = (thermal_value > p_dm_odm->rf_calibrate_info.thermal_value_lck) ? (thermal_value - p_dm_odm->rf_calibrate_info.thermal_value_lck) : (p_dm_odm->rf_calibrate_info.thermal_value_lck - thermal_value);
	delta_IQK = (thermal_value > p_dm_odm->rf_calibrate_info.thermal_value_iqk) ? (thermal_value - p_dm_odm->rf_calibrate_info.thermal_value_iqk) : (p_dm_odm->rf_calibrate_info.thermal_value_iqk - thermal_value);

	/* 4 6. If necessary, do LCK. */
	if (!(p_dm_odm->support_ic_type & ODM_RTL8821)) {
		/*if((delta_LCK > p_hal_data->delta_lck) && (p_hal_data->delta_lck != 0))*/
		if (delta_LCK >= c.threshold_iqk) {
			/*Delta temperature is equal to or larger than 20 centigrade.*/
			p_dm_odm->rf_calibrate_info.thermal_value_lck = thermal_value;
			(*c.phy_lc_calibrate)(p_dm_odm);
		}
	}

	/* 3 7. If necessary, move the index of swing table to adjust Tx power. */

	if (delta > 0 && p_dm_odm->rf_calibrate_info.txpowertrack_control) {
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
		delta = thermal_value > p_hal_data->eeprom_thermal_meter ? (thermal_value - p_hal_data->eeprom_thermal_meter) : (p_hal_data->eeprom_thermal_meter - thermal_value);
#else
		delta = (thermal_value > p_dm_odm->priv->pmib->dot11RFEntry.ther) ? (thermal_value - p_dm_odm->priv->pmib->dot11RFEntry.ther) : (p_dm_odm->priv->pmib->dot11RFEntry.ther - thermal_value);
#endif


		/* 4 7.1 The Final Power index = BaseIndex + power_index_offset */

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
		if (thermal_value > p_hal_data->eeprom_thermal_meter) {
#else
		if (thermal_value > p_dm_odm->priv->pmib->dot11RFEntry.ther) {
#endif
			CALCULATE_SWINGTALBE_OFFSET(offset, POWER_INC, index_mapping_NUM_88E, delta);
			p_dm_odm->rf_calibrate_info.delta_power_index_last = p_dm_odm->rf_calibrate_info.delta_power_index;
			p_dm_odm->rf_calibrate_info.delta_power_index =  delta_swing_table_idx[POWER_INC][offset];

		} else {

			CALCULATE_SWINGTALBE_OFFSET(offset, POWER_DEC, index_mapping_NUM_88E, delta);
			p_dm_odm->rf_calibrate_info.delta_power_index_last = p_dm_odm->rf_calibrate_info.delta_power_index;
			p_dm_odm->rf_calibrate_info.delta_power_index = (-1) * delta_swing_table_idx[POWER_DEC][offset];
		}

		if (p_dm_odm->rf_calibrate_info.delta_power_index == p_dm_odm->rf_calibrate_info.delta_power_index_last)
			p_dm_odm->rf_calibrate_info.power_index_offset = 0;
		else
			p_dm_odm->rf_calibrate_info.power_index_offset = p_dm_odm->rf_calibrate_info.delta_power_index - p_dm_odm->rf_calibrate_info.delta_power_index_last;

		for (i = 0; i < rf; i++)
			p_dm_odm->rf_calibrate_info.OFDM_index[i] = p_rf_calibrate_info->bb_swing_idx_ofdm_base + p_dm_odm->rf_calibrate_info.power_index_offset;
		p_dm_odm->rf_calibrate_info.CCK_index = p_rf_calibrate_info->bb_swing_idx_cck_base + p_dm_odm->rf_calibrate_info.power_index_offset;

		p_rf_calibrate_info->bb_swing_idx_cck = p_dm_odm->rf_calibrate_info.CCK_index;
		p_rf_calibrate_info->bb_swing_idx_ofdm[RF_PATH_A] = p_dm_odm->rf_calibrate_info.OFDM_index[RF_PATH_A];

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("The 'CCK' final index(%d) = BaseIndex(%d) + power_index_offset(%d)\n", p_rf_calibrate_info->bb_swing_idx_cck, p_rf_calibrate_info->bb_swing_idx_cck_base, p_dm_odm->rf_calibrate_info.power_index_offset));
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("The 'OFDM' final index(%d) = BaseIndex(%d) + power_index_offset(%d)\n", p_rf_calibrate_info->bb_swing_idx_ofdm[RF_PATH_A], p_rf_calibrate_info->bb_swing_idx_ofdm_base, p_dm_odm->rf_calibrate_info.power_index_offset));

		/* 4 7.1 Handle boundary conditions of index. */


		for (i = 0; i < rf; i++) {
			if (p_dm_odm->rf_calibrate_info.OFDM_index[i] > OFDM_max_index)
				p_dm_odm->rf_calibrate_info.OFDM_index[i] = OFDM_max_index;
			else if (p_dm_odm->rf_calibrate_info.OFDM_index[i] < 0)
				p_dm_odm->rf_calibrate_info.OFDM_index[i] = 0;
		}

		if (p_dm_odm->rf_calibrate_info.CCK_index > c.swing_table_size_cck - 1)
			p_dm_odm->rf_calibrate_info.CCK_index = c.swing_table_size_cck - 1;
		else if (p_dm_odm->rf_calibrate_info.CCK_index < 0)
			p_dm_odm->rf_calibrate_info.CCK_index = 0;
	} else {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
			("The thermal meter is unchanged or TxPowerTracking OFF: thermal_value: %d, p_dm_odm->rf_calibrate_info.thermal_value: %d)\n", thermal_value, p_dm_odm->rf_calibrate_info.thermal_value));
		p_dm_odm->rf_calibrate_info.power_index_offset = 0;
	}
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("TxPowerTracking: [CCK] Swing Current index: %d, Swing base index: %d\n", p_dm_odm->rf_calibrate_info.CCK_index, p_rf_calibrate_info->bb_swing_idx_cck_base));

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,
		("TxPowerTracking: [OFDM] Swing Current index: %d, Swing base index: %d\n", p_dm_odm->rf_calibrate_info.OFDM_index[RF_PATH_A], p_rf_calibrate_info->bb_swing_idx_ofdm_base));

	if (p_dm_odm->rf_calibrate_info.power_index_offset != 0 && p_dm_odm->rf_calibrate_info.txpowertrack_control) {
		/* 4 7.2 Configure the Swing Table to adjust Tx Power. */

		p_dm_odm->rf_calibrate_info.is_tx_power_changed = true; /* Always true after Tx Power is adjusted by power tracking. */
		/*  */
		/* 2012/04/23 MH According to Luke's suggestion, we can not write BB digital */
		/* to increase TX power. Otherwise, EVM will be bad. */
		/*  */
		/* 2012/04/25 MH Add for tx power tracking to set tx power in tx agc for 88E. */
		if (thermal_value > p_dm_odm->rf_calibrate_info.thermal_value) {
			/* ODM_RT_TRACE(p_dm_odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, */
			/*	("Temperature Increasing: delta_pi: %d, delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n", */
			/*	p_dm_odm->rf_calibrate_info.power_index_offset, delta, thermal_value, p_hal_data->eeprom_thermal_meter, p_dm_odm->rf_calibrate_info.thermal_value)); */
		} else if (thermal_value < p_dm_odm->rf_calibrate_info.thermal_value) { /* Low temperature */
			/* ODM_RT_TRACE(p_dm_odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, */
			/*	("Temperature Decreasing: delta_pi: %d, delta_t: %d, Now_t: %d, EFUSE_t: %d, Last_t: %d\n", */
			/*		p_dm_odm->rf_calibrate_info.power_index_offset, delta, thermal_value, p_hal_data->eeprom_thermal_meter, p_dm_odm->rf_calibrate_info.thermal_value)); */
		}
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
		if (thermal_value > p_hal_data->eeprom_thermal_meter)
#else
		if (thermal_value > p_dm_odm->priv->pmib->dot11RFEntry.ther)
#endif
		{
			/*				ODM_RT_TRACE(p_dm_odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("Temperature(%d) hugher than PG value(%d), increases the power by tx_agc\n", thermal_value, p_hal_data->eeprom_thermal_meter)); */
			(*c.odm_tx_pwr_track_set_pwr)(p_dm_odm, TXAGC, 0, 0);
		} else {
			/*			ODM_RT_TRACE(p_dm_odm,ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD,("Temperature(%d) lower than PG value(%d), increases the power by tx_agc\n", thermal_value, p_hal_data->eeprom_thermal_meter)); */
			(*c.odm_tx_pwr_track_set_pwr)(p_dm_odm, BBSWING, RF_PATH_A, indexforchannel);
			if (is2T)
				(*c.odm_tx_pwr_track_set_pwr)(p_dm_odm, BBSWING, RF_PATH_B, indexforchannel);
		}

		p_rf_calibrate_info->bb_swing_idx_cck_base = p_rf_calibrate_info->bb_swing_idx_cck;
		p_rf_calibrate_info->bb_swing_idx_ofdm_base = p_rf_calibrate_info->bb_swing_idx_ofdm[RF_PATH_A];
		p_dm_odm->rf_calibrate_info.thermal_value = thermal_value;

	}

#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)
	/* if((delta_IQK > p_hal_data->delta_iqk) && (p_hal_data->delta_iqk != 0)) */
	if ((delta_IQK >= 8)) /* Delta temperature is equal to or larger than 20 centigrade. */
		(*c.do_iqk)(p_dm_odm, delta_IQK, thermal_value, 8);
#endif

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("<===dm_TXPowerTrackingCallback_ThermalMeter_8188E\n"));

	p_dm_odm->rf_calibrate_info.tx_powercount = 0;
}

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)


void
phy_path_a_stand_by(
	struct _ADAPTER	*p_adapter
)
{
	RTPRINT(FINIT, INIT_IQK, ("path-A standby mode!\n"));

	phy_set_bb_reg(p_adapter, REG_FPGA0_IQK, 0xffffff00, 0x0);
	phy_set_bb_reg(p_adapter, 0x840, MASKDWORD, 0x00010000);
	phy_set_bb_reg(p_adapter, REG_FPGA0_IQK, 0xffffff00, 0x808000);
}

/* 1 7.	IQK
 * #define MAX_TOLERANCE		5
 * #define IQK_DELAY_TIME		1 */		/* ms */

u8			/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_path_a_iqk_8192c(
	struct _ADAPTER	*p_adapter,
	bool		config_path_b
)
{

	u32 reg_eac, reg_e94, reg_e9c, reg_ea4;
	u8 result = 0x00;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);

	RTPRINT(FINIT, INIT_IQK, ("path A IQK!\n"));

	/* path-A IQK setting */
	RTPRINT(FINIT, INIT_IQK, ("path-A IQK setting!\n"));
	if (p_adapter->interface_index == 0) {
		phy_set_bb_reg(p_adapter, REG_TX_IQK_TONE_A, MASKDWORD, 0x10008c1f);
		phy_set_bb_reg(p_adapter, REG_RX_IQK_TONE_A, MASKDWORD, 0x10008c1f);
	} else {
		phy_set_bb_reg(p_adapter, REG_TX_IQK_TONE_A, MASKDWORD, 0x10008c22);
		phy_set_bb_reg(p_adapter, REG_RX_IQK_TONE_A, MASKDWORD, 0x10008c22);
	}

	phy_set_bb_reg(p_adapter, REG_TX_IQK_PI_A, MASKDWORD, 0x82140102);

	phy_set_bb_reg(p_adapter, REG_RX_IQK_PI_A, MASKDWORD, config_path_b ? 0x28160202 :
		IS_81xxC_VENDOR_UMC_B_CUT(p_hal_data->version_id) ? 0x28160202 : 0x28160502);

	/* path-B IQK setting */
	if (config_path_b) {
		phy_set_bb_reg(p_adapter, REG_TX_IQK_TONE_B, MASKDWORD, 0x10008c22);
		phy_set_bb_reg(p_adapter, REG_RX_IQK_TONE_B, MASKDWORD, 0x10008c22);
		phy_set_bb_reg(p_adapter, REG_TX_IQK_PI_B, MASKDWORD, 0x82140102);
		phy_set_bb_reg(p_adapter, REG_RX_IQK_PI_B, MASKDWORD, 0x28160202);
	}

	/* LO calibration setting */
	RTPRINT(FINIT, INIT_IQK, ("LO calibration setting!\n"));
	phy_set_bb_reg(p_adapter, REG_IQK_AGC_RSP, MASKDWORD, 0x001028d1);

	/* One shot, path A LOK & IQK */
	RTPRINT(FINIT, INIT_IQK, ("One shot, path A LOK & IQK!\n"));
	phy_set_bb_reg(p_adapter, REG_IQK_AGC_PTS, MASKDWORD, 0xf9000000);
	phy_set_bb_reg(p_adapter, REG_IQK_AGC_PTS, MASKDWORD, 0xf8000000);

	/* delay x ms */
	RTPRINT(FINIT, INIT_IQK, ("delay %d ms for One shot, path A LOK & IQK.\n", IQK_DELAY_TIME));
	platform_stall_execution(IQK_DELAY_TIME * 1000);

	/* Check failed */
	reg_eac = phy_query_bb_reg(p_adapter, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	RTPRINT(FINIT, INIT_IQK, ("0xeac = 0x%x\n", reg_eac));
	reg_e94 = phy_query_bb_reg(p_adapter, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD);
	RTPRINT(FINIT, INIT_IQK, ("0xe94 = 0x%x\n", reg_e94));
	reg_e9c = phy_query_bb_reg(p_adapter, REG_TX_POWER_AFTER_IQK_A, MASKDWORD);
	RTPRINT(FINIT, INIT_IQK, ("0xe9c = 0x%x\n", reg_e9c));
	reg_ea4 = phy_query_bb_reg(p_adapter, REG_RX_POWER_BEFORE_IQK_A_2, MASKDWORD);
	RTPRINT(FINIT, INIT_IQK, ("0xea4 = 0x%x\n", reg_ea4));

	if (!(reg_eac & BIT(28)) &&
	    (((reg_e94 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_e9c & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else							/* if Tx not OK, ignore Rx */
		return result;

	if (!(reg_eac & BIT(27)) &&		/* if Tx is OK, check whether Rx is OK */
	    (((reg_ea4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_eac & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;
	else
		RTPRINT(FINIT, INIT_IQK, ("path A Rx IQK fail!!\n"));

	return result;


}

u8				/* bit0 = 1 => Tx OK, bit1 = 1 => Rx OK */
phy_path_b_iqk_8192c(
	struct _ADAPTER	*p_adapter
)
{
	u32 reg_eac, reg_eb4, reg_ebc, reg_ec4, reg_ecc;
	u8	result = 0x00;
	RTPRINT(FINIT, INIT_IQK, ("path B IQK!\n"));

	/* One shot, path B LOK & IQK */
	RTPRINT(FINIT, INIT_IQK, ("One shot, path A LOK & IQK!\n"));
	phy_set_bb_reg(p_adapter, REG_IQK_AGC_CONT, MASKDWORD, 0x00000002);
	phy_set_bb_reg(p_adapter, REG_IQK_AGC_CONT, MASKDWORD, 0x00000000);

	/* delay x ms */
	RTPRINT(FINIT, INIT_IQK, ("delay %d ms for One shot, path B LOK & IQK.\n", IQK_DELAY_TIME));
	platform_stall_execution(IQK_DELAY_TIME * 1000);

	/* Check failed */
	reg_eac = phy_query_bb_reg(p_adapter, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD);
	RTPRINT(FINIT, INIT_IQK, ("0xeac = 0x%x\n", reg_eac));
	reg_eb4 = phy_query_bb_reg(p_adapter, REG_TX_POWER_BEFORE_IQK_B, MASKDWORD);
	RTPRINT(FINIT, INIT_IQK, ("0xeb4 = 0x%x\n", reg_eb4));
	reg_ebc = phy_query_bb_reg(p_adapter, REG_TX_POWER_AFTER_IQK_B, MASKDWORD);
	RTPRINT(FINIT, INIT_IQK, ("0xebc = 0x%x\n", reg_ebc));
	reg_ec4 = phy_query_bb_reg(p_adapter, REG_RX_POWER_BEFORE_IQK_B_2, MASKDWORD);
	RTPRINT(FINIT, INIT_IQK, ("0xec4 = 0x%x\n", reg_ec4));
	reg_ecc = phy_query_bb_reg(p_adapter, REG_RX_POWER_AFTER_IQK_B_2, MASKDWORD);
	RTPRINT(FINIT, INIT_IQK, ("0xecc = 0x%x\n", reg_ecc));

	if (!(reg_eac & BIT(31)) &&
	    (((reg_eb4 & 0x03FF0000) >> 16) != 0x142) &&
	    (((reg_ebc & 0x03FF0000) >> 16) != 0x42))
		result |= 0x01;
	else
		return result;

	if (!(reg_eac & BIT(30)) &&
	    (((reg_ec4 & 0x03FF0000) >> 16) != 0x132) &&
	    (((reg_ecc & 0x03FF0000) >> 16) != 0x36))
		result |= 0x02;
	else
		RTPRINT(FINIT, INIT_IQK, ("path B Rx IQK fail!!\n"));


	return result;

}

void
phy_path_a_fill_iqk_matrix(
	struct _ADAPTER	*p_adapter,
	bool	is_iqk_ok,
	s32		result[][8],
	u8		final_candidate,
	bool		is_tx_only
)
{
	u32	oldval_0, X, TX0_A, reg;
	s32	Y, TX0_C;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);

	RTPRINT(FINIT, INIT_IQK, ("path A IQ Calibration %s !\n", (is_iqk_ok) ? "Success" : "Failed"));

	if (final_candidate == 0xFF)
		return;

	else if (is_iqk_ok) {
		oldval_0 = (phy_query_bb_reg(p_adapter, REG_OFDM_0_XA_TX_IQ_IMBALANCE, MASKDWORD) >> 22) & 0x3FF;

		X = result[final_candidate][0];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;
		TX0_A = (X * oldval_0) >> 8;
		RTPRINT(FINIT, INIT_IQK, ("X = 0x%x, TX0_A = 0x%x, oldval_0 0x%x\n", X, TX0_A, oldval_0));
		phy_set_bb_reg(p_adapter, REG_OFDM_0_XA_TX_IQ_IMBALANCE, 0x3FF, TX0_A);
		phy_set_bb_reg(p_adapter, REG_OFDM_0_ECCA_THRESHOLD, BIT(31), ((X * oldval_0 >> 7) & 0x1));

		Y = result[final_candidate][1];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;

		/* path B IQK result + 3 */
		if (p_adapter->interface_index == 1 && p_hal_data->current_band_type == BAND_ON_5G)
			Y += 3;

		TX0_C = (Y * oldval_0) >> 8;
		RTPRINT(FINIT, INIT_IQK, ("Y = 0x%x, TX = 0x%x\n", Y, TX0_C));
		phy_set_bb_reg(p_adapter, REG_OFDM_0_XC_TX_AFE, 0xF0000000, ((TX0_C & 0x3C0) >> 6));
		phy_set_bb_reg(p_adapter, REG_OFDM_0_XA_TX_IQ_IMBALANCE, 0x003F0000, (TX0_C & 0x3F));
		phy_set_bb_reg(p_adapter, REG_OFDM_0_ECCA_THRESHOLD, BIT(29), ((Y * oldval_0 >> 7) & 0x1));

		if (is_tx_only) {
			RTPRINT(FINIT, INIT_IQK, ("phy_path_a_fill_iqk_matrix only Tx OK\n"));
			return;
		}

		reg = result[final_candidate][2];
		phy_set_bb_reg(p_adapter, REG_OFDM_0_XA_RX_IQ_IMBALANCE, 0x3FF, reg);

		reg = result[final_candidate][3] & 0x3F;
		phy_set_bb_reg(p_adapter, REG_OFDM_0_XA_RX_IQ_IMBALANCE, 0xFC00, reg);

		reg = (result[final_candidate][3] >> 6) & 0xF;
		phy_set_bb_reg(p_adapter, REG_OFDM_0_RX_IQ_EXT_ANTA, 0xF0000000, reg);
	}
}

void
phy_path_b_fill_iqk_matrix(
	struct _ADAPTER	*p_adapter,
	bool	is_iqk_ok,
	s32		result[][8],
	u8		final_candidate,
	bool		is_tx_only			/* do Tx only */
)
{
	u32	oldval_1, X, TX1_A, reg;
	s32	Y, TX1_C;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);

	RTPRINT(FINIT, INIT_IQK, ("path B IQ Calibration %s !\n", (is_iqk_ok) ? "Success" : "Failed"));

	if (final_candidate == 0xFF)
		return;

	else if (is_iqk_ok) {
		oldval_1 = (phy_query_bb_reg(p_adapter, REG_OFDM_0_XB_TX_IQ_IMBALANCE, MASKDWORD) >> 22) & 0x3FF;

		X = result[final_candidate][4];
		if ((X & 0x00000200) != 0)
			X = X | 0xFFFFFC00;
		TX1_A = (X * oldval_1) >> 8;
		RTPRINT(FINIT, INIT_IQK, ("X = 0x%x, TX1_A = 0x%x\n", X, TX1_A));
		phy_set_bb_reg(p_adapter, REG_OFDM_0_XB_TX_IQ_IMBALANCE, 0x3FF, TX1_A);
		phy_set_bb_reg(p_adapter, REG_OFDM_0_ECCA_THRESHOLD, BIT(27), ((X * oldval_1 >> 7) & 0x1));

		Y = result[final_candidate][5];
		if ((Y & 0x00000200) != 0)
			Y = Y | 0xFFFFFC00;
		if (p_hal_data->current_band_type == BAND_ON_5G)
			Y += 3;		/* temp modify for preformance */
		TX1_C = (Y * oldval_1) >> 8;
		RTPRINT(FINIT, INIT_IQK, ("Y = 0x%x, TX1_C = 0x%x\n", Y, TX1_C));
		phy_set_bb_reg(p_adapter, REG_OFDM_0_XD_TX_AFE, 0xF0000000, ((TX1_C & 0x3C0) >> 6));
		phy_set_bb_reg(p_adapter, REG_OFDM_0_XB_TX_IQ_IMBALANCE, 0x003F0000, (TX1_C & 0x3F));
		phy_set_bb_reg(p_adapter, REG_OFDM_0_ECCA_THRESHOLD, BIT(25), ((Y * oldval_1 >> 7) & 0x1));

		if (is_tx_only)
			return;

		reg = result[final_candidate][6];
		phy_set_bb_reg(p_adapter, REG_OFDM_0_XB_RX_IQ_IMBALANCE, 0x3FF, reg);

		reg = result[final_candidate][7] & 0x3F;
		phy_set_bb_reg(p_adapter, REG_OFDM_0_XB_RX_IQ_IMBALANCE, 0xFC00, reg);

		reg = (result[final_candidate][7] >> 6) & 0xF;
		phy_set_bb_reg(p_adapter, REG_OFDM_0_AGC_RSSI_TABLE, 0x0000F000, reg);
	}
}


bool
phy_simularity_compare_92c(
	struct _ADAPTER	*p_adapter,
	s32		result[][8],
	u8		 c1,
	u8		 c2
)
{
	u32		i, j, diff, simularity_bit_map, bound = 0;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
	u8		final_candidate[2] = {0xFF, 0xFF};	/* for path A and path B */
	bool		is_result = true, is2T = IS_92C_SERIAL(p_hal_data->version_id);

	if (is2T)
		bound = 8;
	else
		bound = 4;

	simularity_bit_map = 0;

	for (i = 0; i < bound; i++) {
		diff = (result[c1][i] > result[c2][i]) ? (result[c1][i] - result[c2][i]) : (result[c2][i] - result[c1][i]);
		if (diff > MAX_TOLERANCE) {
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

	if (simularity_bit_map == 0) {
		for (i = 0; i < (bound / 4); i++) {
			if (final_candidate[i] != 0xFF) {
				for (j = i * 4; j < (i + 1) * 4 - 2; j++)
					result[3][j] = result[final_candidate[i]][j];
				is_result = false;
			}
		}
		return is_result;
	} else if (!(simularity_bit_map & 0x0F)) {		/* path A OK */
		for (i = 0; i < 4; i++)
			result[3][i] = result[c1][i];
		return false;
	} else if (!(simularity_bit_map & 0xF0) && is2T) {	/* path B OK */
		for (i = 4; i < 8; i++)
			result[3][i] = result[c1][i];
		return false;
	} else
		return false;

}

/*
return false => do IQK again
*/
bool
phy_simularity_compare(
	struct _ADAPTER	*p_adapter,
	s32		result[][8],
	u8		 c1,
	u8		 c2
)
{
	return phy_simularity_compare_92c(p_adapter, result, c1, c2);

}

void
_phy_iq_calibrate_8192c(
	struct _ADAPTER	*p_adapter,
	s32		result[][8],
	u8		t,
	bool		is2T
)
{
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
	u32			i;
	u8			path_aok, path_bok;
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
		REG_FPGA0_XB_RF_INTERFACE_OE,	/*REG_FPGA0_RFMOD*/ REG_CCK_0_AFE_SETTING
	};

	u32	IQK_BB_REG_92D[IQK_BB_REG_NUM_92D] = {	/* for normal */
		REG_FPGA0_XAB_RF_INTERFACE_SW,	REG_FPGA0_XA_RF_INTERFACE_OE,
		REG_FPGA0_XB_RF_INTERFACE_OE,	REG_OFDM_0_TR_MUX_PAR,
		REG_FPGA0_XCD_RF_INTERFACE_SW,	REG_OFDM_0_TRX_PATH_ENABLE,
		/*REG_FPGA0_RFMOD*/ REG_CCK_0_AFE_SETTING,			REG_FPGA0_ANALOG_PARAMETER4,
		REG_OFDM_0_XA_AGC_CORE1,		REG_OFDM_0_XB_AGC_CORE1
	};
#if MP_DRIVER
	const u32	retry_count = 9;
#else
	const u32	retry_count = 2;
#endif
	/* Neil Chen--2011--05--19--
	* 3 path Div */
	u8                 rf_path_switch = 0x0;

	/* Note: IQ calibration must be performed after loading */
	/*		PHY_REG.txt , and radio_a, radio_b.txt */

	u32 bbvalue;

	if (t == 0) {
		/* bbvalue = phy_query_bb_reg(p_adapter, REG_FPGA0_RFMOD, MASKDWORD); */
		/*	RTPRINT(FINIT, INIT_IQK, ("_phy_iq_calibrate_8192c()==>0x%08x\n",bbvalue)); */

		RTPRINT(FINIT, INIT_IQK, ("IQ Calibration for %s\n", (is2T ? "2T2R" : "1T1R")));

		/* Save ADDA parameters, turn path A ADDA on */
		phy_save_adda_registers(p_adapter, ADDA_REG, p_hal_data->ADDA_backup, IQK_ADDA_REG_NUM);
		phy_save_mac_registers(p_adapter, IQK_MAC_REG, p_hal_data->IQK_MAC_backup);
		phy_save_adda_registers(p_adapter, IQK_BB_REG_92C, p_hal_data->IQK_BB_backup, IQK_BB_REG_NUM);
	}

	phy_path_adda_on(p_adapter, ADDA_REG, true, is2T);

	if (t == 0)
		p_hal_data->is_rf_pi_enable = (u8)phy_query_bb_reg(p_adapter, REG_FPGA0_XA_HSSI_PARAMETER1, BIT(8));

	if (!p_hal_data->is_rf_pi_enable) {
		/* Switch BB to PI mode to do IQ Calibration. */
		phy_pi_mode_switch(p_adapter, true);
	}

	/* MAC settings */
	phy_mac_setting_calibration(p_adapter, IQK_MAC_REG, p_hal_data->IQK_MAC_backup);

	/* phy_set_bb_reg(p_adapter, REG_FPGA0_RFMOD, BIT24, 0x00); */
	phy_set_bb_reg(p_adapter, REG_CCK_0_AFE_SETTING, MASKDWORD, (0x0f000000 | (phy_query_bb_reg(p_adapter, REG_CCK_0_AFE_SETTING, MASKDWORD))));
	phy_set_bb_reg(p_adapter, REG_OFDM_0_TRX_PATH_ENABLE, MASKDWORD, 0x03a05600);
	phy_set_bb_reg(p_adapter, REG_OFDM_0_TR_MUX_PAR, MASKDWORD, 0x000800e4);
	phy_set_bb_reg(p_adapter, REG_FPGA0_XCD_RF_INTERFACE_SW, MASKDWORD, 0x22204000);
	{
		phy_set_bb_reg(p_adapter, REG_FPGA0_XAB_RF_INTERFACE_SW, BIT(10), 0x01);
		phy_set_bb_reg(p_adapter, REG_FPGA0_XAB_RF_INTERFACE_SW, BIT(26), 0x01);
		phy_set_bb_reg(p_adapter, REG_FPGA0_XA_RF_INTERFACE_OE, BIT(10), 0x00);
		phy_set_bb_reg(p_adapter, REG_FPGA0_XB_RF_INTERFACE_OE, BIT(10), 0x00);
	}

	if (is2T) {
		phy_set_bb_reg(p_adapter, REG_FPGA0_XA_LSSI_PARAMETER, MASKDWORD, 0x00010000);
		phy_set_bb_reg(p_adapter, REG_FPGA0_XB_LSSI_PARAMETER, MASKDWORD, 0x00010000);
	}

	{
		/* Page B init */
		phy_set_bb_reg(p_adapter, REG_CONFIG_ANT_A, MASKDWORD, 0x00080000);

		if (is2T)
			phy_set_bb_reg(p_adapter, REG_CONFIG_ANT_B, MASKDWORD, 0x00080000);
	}
	/* IQ calibration setting */
	RTPRINT(FINIT, INIT_IQK, ("IQK setting!\n"));
	phy_set_bb_reg(p_adapter, REG_FPGA0_IQK, 0xffffff00, 0x808000);
	phy_set_bb_reg(p_adapter, REG_TX_IQK, MASKDWORD, 0x01007c00);
	phy_set_bb_reg(p_adapter, REG_RX_IQK, MASKDWORD, 0x01004800);

	for (i = 0 ; i < retry_count ; i++) {
		path_aok = phy_path_a_iqk_8192c(p_adapter, is2T);
		if (path_aok == 0x03) {
			RTPRINT(FINIT, INIT_IQK, ("path A IQK Success!!\n"));
			result[t][0] = (phy_query_bb_reg(p_adapter, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
			result[t][1] = (phy_query_bb_reg(p_adapter, REG_TX_POWER_AFTER_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
			result[t][2] = (phy_query_bb_reg(p_adapter, REG_RX_POWER_BEFORE_IQK_A_2, MASKDWORD) & 0x3FF0000) >> 16;
			result[t][3] = (phy_query_bb_reg(p_adapter, REG_RX_POWER_AFTER_IQK_A_2, MASKDWORD) & 0x3FF0000) >> 16;
			break;
		} else if (i == (retry_count - 1) && path_aok == 0x01) {	/* Tx IQK OK */
			RTPRINT(FINIT, INIT_IQK, ("path A IQK Only  Tx Success!!\n"));

			result[t][0] = (phy_query_bb_reg(p_adapter, REG_TX_POWER_BEFORE_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
			result[t][1] = (phy_query_bb_reg(p_adapter, REG_TX_POWER_AFTER_IQK_A, MASKDWORD) & 0x3FF0000) >> 16;
		}
	}

	if (0x00 == path_aok)
		RTPRINT(FINIT, INIT_IQK, ("path A IQK failed!!\n"));

	if (is2T) {
		phy_path_a_stand_by(p_adapter);

		/* Turn path B ADDA on */
		phy_path_adda_on(p_adapter, ADDA_REG, false, is2T);

		for (i = 0 ; i < retry_count ; i++) {
			path_bok = phy_path_b_iqk_8192c(p_adapter);
			if (path_bok == 0x03) {
				RTPRINT(FINIT, INIT_IQK, ("path B IQK Success!!\n"));
				result[t][4] = (phy_query_bb_reg(p_adapter, REG_TX_POWER_BEFORE_IQK_B, MASKDWORD) & 0x3FF0000) >> 16;
				result[t][5] = (phy_query_bb_reg(p_adapter, REG_TX_POWER_AFTER_IQK_B, MASKDWORD) & 0x3FF0000) >> 16;
				result[t][6] = (phy_query_bb_reg(p_adapter, REG_RX_POWER_BEFORE_IQK_B_2, MASKDWORD) & 0x3FF0000) >> 16;
				result[t][7] = (phy_query_bb_reg(p_adapter, REG_RX_POWER_AFTER_IQK_B_2, MASKDWORD) & 0x3FF0000) >> 16;
				break;
			} else if (i == (retry_count - 1) && path_bok == 0x01) {	/* Tx IQK OK */
				RTPRINT(FINIT, INIT_IQK, ("path B Only Tx IQK Success!!\n"));
				result[t][4] = (phy_query_bb_reg(p_adapter, REG_TX_POWER_BEFORE_IQK_B, MASKDWORD) & 0x3FF0000) >> 16;
				result[t][5] = (phy_query_bb_reg(p_adapter, REG_TX_POWER_AFTER_IQK_B, MASKDWORD) & 0x3FF0000) >> 16;
			}
		}

		if (0x00 == path_bok)
			RTPRINT(FINIT, INIT_IQK, ("path B IQK failed!!\n"));
	}

	/* Back to BB mode, load original value */
	RTPRINT(FINIT, INIT_IQK, ("IQK:Back to BB mode, load original value!\n"));
	phy_set_bb_reg(p_adapter, REG_FPGA0_IQK, 0xffffff00, 0);

	if (t != 0) {
		if (!p_hal_data->is_rf_pi_enable) {
			/* Switch back BB to SI mode after finish IQ Calibration. */
			phy_pi_mode_switch(p_adapter, false);
		}

		/* Reload ADDA power saving parameters */
		phy_reload_adda_registers(p_adapter, ADDA_REG, p_hal_data->ADDA_backup, IQK_ADDA_REG_NUM);

		/* Reload MAC parameters */
		phy_reload_mac_registers(p_adapter, IQK_MAC_REG, p_hal_data->IQK_MAC_backup);

		/* Reload BB parameters */
		phy_reload_adda_registers(p_adapter, IQK_BB_REG_92C, p_hal_data->IQK_BB_backup, IQK_BB_REG_NUM);

		/*Restore RX initial gain*/
		phy_set_bb_reg(p_adapter, REG_FPGA0_XA_LSSI_PARAMETER, MASKDWORD, 0x00032ed3);
		if (is2T)
			phy_set_bb_reg(p_adapter, REG_FPGA0_XB_LSSI_PARAMETER, MASKDWORD, 0x00032ed3);
		/* load 0xe30 IQC default value */
		phy_set_bb_reg(p_adapter, REG_TX_IQK_TONE_A, MASKDWORD, 0x01008c00);
		phy_set_bb_reg(p_adapter, REG_RX_IQK_TONE_A, MASKDWORD, 0x01008c00);

	}
	RTPRINT(FINIT, INIT_IQK, ("_phy_iq_calibrate_8192c() <==\n"));

}


void
_phy_lccalibrate92c(
	struct _ADAPTER	*p_adapter,
	bool		is2T
)
{
	u8	tmp_reg;
	u32	rf_amode = 0, rf_bmode = 0, lc_cal;
	/*	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter); */

	/* Check continuous TX and Packet TX */
	tmp_reg = platform_efio_read_1byte(p_adapter, 0xd03);

	if ((tmp_reg & 0x70) != 0)			/* Deal with contisuous TX case */
		platform_efio_write_1byte(p_adapter, 0xd03, tmp_reg & 0x8F);	/* disable all continuous TX */
	else							/* Deal with Packet TX case */
		platform_efio_write_1byte(p_adapter, REG_TXPAUSE, 0xFF);			/* block all queues */

	if ((tmp_reg & 0x70) != 0) {
		/* 1. Read original RF mode */
		/* path-A */
		rf_amode = phy_query_rf_reg(p_adapter, RF_PATH_A, RF_AC, MASK12BITS);

		/* path-B */
		if (is2T)
			rf_bmode = phy_query_rf_reg(p_adapter, RF_PATH_B, RF_AC, MASK12BITS);

		/* 2. Set RF mode = standby mode */
		/* path-A */
		phy_set_rf_reg(p_adapter, RF_PATH_A, RF_AC, MASK12BITS, (rf_amode & 0x8FFFF) | 0x10000);

		/* path-B */
		if (is2T)
			phy_set_rf_reg(p_adapter, RF_PATH_B, RF_AC, MASK12BITS, (rf_bmode & 0x8FFFF) | 0x10000);
	}

	/* 3. Read RF reg18 */
	lc_cal = phy_query_rf_reg(p_adapter, RF_PATH_A, RF_CHNLBW, MASK12BITS);

	/* 4. Set LC calibration begin	bit15 */
	phy_set_rf_reg(p_adapter, RF_PATH_A, RF_CHNLBW, MASK12BITS, lc_cal | 0x08000);

	delay_ms(100);


	/* Restore original situation */
	if ((tmp_reg & 0x70) != 0) {	/* Deal with contisuous TX case */
		/* path-A */
		platform_efio_write_1byte(p_adapter, 0xd03, tmp_reg);
		phy_set_rf_reg(p_adapter, RF_PATH_A, RF_AC, MASK12BITS, rf_amode);

		/* path-B */
		if (is2T)
			phy_set_rf_reg(p_adapter, RF_PATH_B, RF_AC, MASK12BITS, rf_bmode);
	} else /* Deal with Packet TX case */
		platform_efio_write_1byte(p_adapter, REG_TXPAUSE, 0x00);
}


void
phy_lc_calibrate(
	struct _ADAPTER	*p_adapter,
	bool		is2T
)
{
	_phy_lccalibrate92c(p_adapter, is2T);
}



/* Analog Pre-distortion calibration */
#define		APK_BB_REG_NUM	8
#define		APK_CURVE_REG_NUM 4
#define		PATH_NUM		2

void
_phy_ap_calibrate_8192c(
	struct _ADAPTER	*p_adapter,
	s8		delta,
	bool		is2T
)
{
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);

	u32			reg_d[PATH_NUM];
	u32			tmp_reg, index, offset, i, apkbound;
	u8			path, pathbound = PATH_NUM;
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
#if 0
	u32			APK_RF_value_A[PATH_NUM][APK_BB_REG_NUM] = {
		{0x1adb0, 0x1adb0, 0x1ada0, 0x1ad90, 0x1ad80},
		{0x00fb0, 0x00fb0, 0x00fa0, 0x00f90, 0x00f80}
	};
#endif
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
	PMPT_CONTEXT	p_mpt_ctx = &(p_adapter->mpt_ctx);

	p_mpt_ctx->APK_bound[0] = 45;
	p_mpt_ctx->APK_bound[1] = 52;
#endif

	RTPRINT(FINIT, INIT_IQK, ("==>_phy_ap_calibrate_8192c() delta %d\n", delta));
	RTPRINT(FINIT, INIT_IQK, ("AP Calibration for %s\n", (is2T ? "2T2R" : "1T1R")));
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
		BB_backup[index] = phy_query_bb_reg(p_adapter, BB_REG[index], MASKDWORD);
	}

	/* save MAC default value */
	phy_save_mac_registers(p_adapter, MAC_REG, MAC_backup);

	/* save AFE default value */
	phy_save_adda_registers(p_adapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);

	for (path = 0; path < pathbound; path++) {


		if (path == RF_PATH_A) {
			/* path A APK */
			/* load APK setting */
			/* path-A */
			offset = REG_PDP_ANT_A;
			for (index = 0; index < 11; index++) {
				phy_set_bb_reg(p_adapter, offset, MASKDWORD, APK_normal_setting_value_1[index]);
				RTPRINT(FINIT, INIT_IQK, ("_phy_ap_calibrate_8192c() offset 0x%x value 0x%x\n", offset, phy_query_bb_reg(p_adapter, offset, MASKDWORD)));

				offset += 0x04;
			}

			phy_set_bb_reg(p_adapter, REG_CONFIG_PMPD_ANT_B, MASKDWORD, 0x12680000);

			offset = REG_CONFIG_ANT_A;
			for (; index < 13; index++) {
				phy_set_bb_reg(p_adapter, offset, MASKDWORD, APK_normal_setting_value_1[index]);
				RTPRINT(FINIT, INIT_IQK, ("_phy_ap_calibrate_8192c() offset 0x%x value 0x%x\n", offset, phy_query_bb_reg(p_adapter, offset, MASKDWORD)));

				offset += 0x04;
			}

			/* page-B1 */
			phy_set_bb_reg(p_adapter, REG_FPGA0_IQK, 0xffffff00, 0x400000);

			/* path A */
			offset = REG_PDP_ANT_A;
			for (index = 0; index < 16; index++) {
				phy_set_bb_reg(p_adapter, offset, MASKDWORD, APK_normal_setting_value_2[index]);
				RTPRINT(FINIT, INIT_IQK, ("_phy_ap_calibrate_8192c() offset 0x%x value 0x%x\n", offset, phy_query_bb_reg(p_adapter, offset, MASKDWORD)));

				offset += 0x04;
			}
			phy_set_bb_reg(p_adapter, REG_FPGA0_IQK, 0xffffff00, 0);
		} else if (path == RF_PATH_B) {
			/* path B APK */
			/* load APK setting */
			/* path-B */
			offset = REG_PDP_ANT_B;
			for (index = 0; index < 10; index++) {
				phy_set_bb_reg(p_adapter, offset, MASKDWORD, APK_normal_setting_value_1[index]);
				RTPRINT(FINIT, INIT_IQK, ("_phy_ap_calibrate_8192c() offset 0x%x value 0x%x\n", offset, phy_query_bb_reg(p_adapter, offset, MASKDWORD)));

				offset += 0x04;
			}
			phy_set_bb_reg(p_adapter, REG_CONFIG_PMPD_ANT_A, MASKDWORD, 0x12680000);

			phy_set_bb_reg(p_adapter, REG_CONFIG_PMPD_ANT_B, MASKDWORD, 0x12680000);

			offset = REG_CONFIG_ANT_A;
			index = 11;
			for (; index < 13; index++) { /* offset 0xb68, 0xb6c */
				phy_set_bb_reg(p_adapter, offset, MASKDWORD, APK_normal_setting_value_1[index]);
				RTPRINT(FINIT, INIT_IQK, ("_phy_ap_calibrate_8192c() offset 0x%x value 0x%x\n", offset, phy_query_bb_reg(p_adapter, offset, MASKDWORD)));

				offset += 0x04;
			}

			/* page-B1 */
			phy_set_bb_reg(p_adapter, REG_FPGA0_IQK, 0xffffff00, 0x400000);

			/* path B */
			offset = 0xb60;
			for (index = 0; index < 16; index++) {
				phy_set_bb_reg(p_adapter, offset, MASKDWORD, APK_normal_setting_value_2[index]);
				RTPRINT(FINIT, INIT_IQK, ("_phy_ap_calibrate_8192c() offset 0x%x value 0x%x\n", offset, phy_query_bb_reg(p_adapter, offset, MASKDWORD)));

				offset += 0x04;
			}
			phy_set_bb_reg(p_adapter, REG_FPGA0_IQK, 0xffffff00, 0);
		}

		/* save RF default value */
		reg_d[path] = phy_query_rf_reg(p_adapter, path, RF_TXBIAS_A, RFREGOFFSETMASK);

		/* path A AFE all on, path B AFE All off or vise versa */
		for (index = 0; index < IQK_ADDA_REG_NUM ; index++)
			phy_set_bb_reg(p_adapter, AFE_REG[index], MASKDWORD, AFE_on_off[path]);
		RTPRINT(FINIT, INIT_IQK, ("_phy_ap_calibrate_8192c() offset 0xe70 %x\n", phy_query_bb_reg(p_adapter, REG_RX_WAIT_CCA, MASKDWORD)));

		/* BB to AP mode */
		if (path == 0) {
			for (index = 0; index < APK_BB_REG_NUM ; index++) {

				if (index == 0)		/* skip */
					continue;
				else if (index < 5)
					phy_set_bb_reg(p_adapter, BB_REG[index], MASKDWORD, BB_AP_MODE[index]);
				else if (BB_REG[index] == 0x870)
					phy_set_bb_reg(p_adapter, BB_REG[index], MASKDWORD, BB_backup[index] | BIT(10) | BIT(26));
				else
					phy_set_bb_reg(p_adapter, BB_REG[index], BIT(10), 0x0);
			}

			phy_set_bb_reg(p_adapter, REG_TX_IQK_TONE_A, MASKDWORD, 0x01008c00);
			phy_set_bb_reg(p_adapter, REG_RX_IQK_TONE_A, MASKDWORD, 0x01008c00);
		} else {	/* path B */
			phy_set_bb_reg(p_adapter, REG_TX_IQK_TONE_B, MASKDWORD, 0x01008c00);
			phy_set_bb_reg(p_adapter, REG_RX_IQK_TONE_B, MASKDWORD, 0x01008c00);

		}

		RTPRINT(FINIT, INIT_IQK, ("_phy_ap_calibrate_8192c() offset 0x800 %x\n", phy_query_bb_reg(p_adapter, 0x800, MASKDWORD)));

		/* MAC settings */
		phy_mac_setting_calibration(p_adapter, MAC_REG, MAC_backup);

		if (path == RF_PATH_A)	/* path B to standby mode */
			phy_set_rf_reg(p_adapter, RF_PATH_B, RF_AC, RFREGOFFSETMASK, 0x10000);
		else {		/* path A to standby mode */
			phy_set_rf_reg(p_adapter, RF_PATH_A, RF_AC, RFREGOFFSETMASK, 0x10000);
			phy_set_rf_reg(p_adapter, RF_PATH_A, RF_MODE1, RFREGOFFSETMASK, 0x1000f);
			phy_set_rf_reg(p_adapter, RF_PATH_A, RF_MODE2, RFREGOFFSETMASK, 0x20103);
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
			if (!p_hal_data->is_apk_thermal_meter_ignore) {
				BB_offset = (tmp_reg & 0xF0000) >> 16;

				if (!(tmp_reg & BIT(15))) /* sign bit 0 */
					BB_offset = -BB_offset;

				delta_V = APK_delta_mapping[index][delta_offset];

				BB_offset += delta_V;

				RTPRINT(FINIT, INIT_IQK, ("_phy_ap_calibrate_8192c() APK index %d tmp_reg 0x%x delta_V %d delta_offset %d\n", index, tmp_reg, delta_V, delta_offset));

				if (BB_offset < 0) {
					tmp_reg = tmp_reg & (~BIT(15));
					BB_offset = -BB_offset;
				} else
					tmp_reg = tmp_reg | BIT(15);
				tmp_reg = (tmp_reg & 0xFFF0FFFF) | (BB_offset << 16);
			}
#endif

#if DEV_BUS_TYPE == RT_PCI_INTERFACE
			if (IS_81xxC_VENDOR_UMC_B_CUT(p_hal_data->version_id))
				phy_set_rf_reg(p_adapter, path, RF_IPA_A, RFREGOFFSETMASK, 0x894ae);
			else
#endif
				phy_set_rf_reg(p_adapter, path, RF_IPA_A, RFREGOFFSETMASK, 0x8992e);
			RTPRINT(FINIT, INIT_IQK, ("_phy_ap_calibrate_8192c() offset 0xc %x\n", phy_query_rf_reg(p_adapter, path, RF_IPA_A, RFREGOFFSETMASK)));
			phy_set_rf_reg(p_adapter, path, RF_AC, RFREGOFFSETMASK, APK_RF_value_0[path][index]);
			RTPRINT(FINIT, INIT_IQK, ("_phy_ap_calibrate_8192c() offset 0x0 %x\n", phy_query_rf_reg(p_adapter, path, RF_AC, RFREGOFFSETMASK)));
			phy_set_rf_reg(p_adapter, path, RF_TXBIAS_A, RFREGOFFSETMASK, tmp_reg);
			RTPRINT(FINIT, INIT_IQK, ("_phy_ap_calibrate_8192c() offset 0xd %x\n", phy_query_rf_reg(p_adapter, path, RF_TXBIAS_A, RFREGOFFSETMASK)));

			/* PA11+PAD01111, one shot */
			i = 0;
			do {
				phy_set_bb_reg(p_adapter, REG_FPGA0_IQK, 0xffffff00, 0x800000);
				{
					phy_set_bb_reg(p_adapter, APK_offset[path], MASKDWORD, APK_value[0]);
					RTPRINT(FINIT, INIT_IQK, ("_phy_ap_calibrate_8192c() offset 0x%x value 0x%x\n", APK_offset[path], phy_query_bb_reg(p_adapter, APK_offset[path], MASKDWORD)));
					delay_ms(3);
					phy_set_bb_reg(p_adapter, APK_offset[path], MASKDWORD, APK_value[1]);
					RTPRINT(FINIT, INIT_IQK, ("_phy_ap_calibrate_8192c() offset 0x%x value 0x%x\n", APK_offset[path], phy_query_bb_reg(p_adapter, APK_offset[path], MASKDWORD)));

					delay_ms(20);
				}
				phy_set_bb_reg(p_adapter, REG_FPGA0_IQK, 0xffffff00, 0);

				if (path == RF_PATH_A)
					tmp_reg = phy_query_bb_reg(p_adapter, REG_APK, 0x03E00000);
				else
					tmp_reg = phy_query_bb_reg(p_adapter, REG_APK, 0xF8000000);
				RTPRINT(FINIT, INIT_IQK, ("_phy_ap_calibrate_8192c() offset 0xbd8[25:21] %x\n", tmp_reg));


				i++;
			} while (tmp_reg > apkbound && i < 4);

			APK_result[path][index] = tmp_reg;
		}
	}

	/* reload MAC default value */
	phy_reload_mac_registers(p_adapter, MAC_REG, MAC_backup);

	/* reload BB default value */
	for (index = 0; index < APK_BB_REG_NUM ; index++) {

		if (index == 0)		/* skip */
			continue;
		phy_set_bb_reg(p_adapter, BB_REG[index], MASKDWORD, BB_backup[index]);
	}

	/* reload AFE default value */
	phy_reload_adda_registers(p_adapter, AFE_REG, AFE_backup, IQK_ADDA_REG_NUM);

	/* reload RF path default value */
	for (path = 0; path < pathbound; path++) {
		phy_set_rf_reg(p_adapter, path, RF_TXBIAS_A, RFREGOFFSETMASK, reg_d[path]);
		if (path == RF_PATH_B) {
			phy_set_rf_reg(p_adapter, RF_PATH_A, RF_MODE1, RFREGOFFSETMASK, 0x1000f);
			phy_set_rf_reg(p_adapter, RF_PATH_A, RF_MODE2, RFREGOFFSETMASK, 0x20101);
		}

		/* note no index == 0 */
		if (APK_result[path][1] > 6)
			APK_result[path][1] = 6;
		RTPRINT(FINIT, INIT_IQK, ("apk path %d result %d 0x%x \t", path, 1, APK_result[path][1]));
	}

	RTPRINT(FINIT, INIT_IQK, ("\n"));


	for (path = 0; path < pathbound; path++) {
		phy_set_rf_reg(p_adapter, path, RF_BS_PA_APSET_G1_G4, RFREGOFFSETMASK,
			((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (APK_result[path][1] << 5) | APK_result[path][1]));
		if (path == RF_PATH_A)
			phy_set_rf_reg(p_adapter, path, RF_BS_PA_APSET_G5_G8, RFREGOFFSETMASK,
				((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (0x00 << 5) | 0x05));
		else
			phy_set_rf_reg(p_adapter, path, RF_BS_PA_APSET_G5_G8, RFREGOFFSETMASK,
				((APK_result[path][1] << 15) | (APK_result[path][1] << 10) | (0x02 << 5) | 0x05));

		phy_set_rf_reg(p_adapter, path, RF_BS_PA_APSET_G9_G11, RFREGOFFSETMASK, ((0x08 << 15) | (0x08 << 10) | (0x08 << 5) | 0x08));
	}

	p_hal_data->is_ap_kdone = true;

	RTPRINT(FINIT, INIT_IQK, ("<==_phy_ap_calibrate_8192c()\n"));
}


void
phy_iq_calibrate_8192c(
	struct _ADAPTER	*p_adapter,
	bool	is_recovery
)
{
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
	s32			result[4][8];	/* last is final result */
	u8			i, final_candidate, indexforchannel;
	bool			is_patha_ok, is_pathb_ok;
	s32			rege94, rege9c, regea4, regeac, regeb4, regebc, regec4, regecc, reg_tmp = 0;
	bool			is12simular, is13simular, is23simular;
	bool		is_start_cont_tx = false, is_single_tone = false, is_carrier_suppression = false;
	u32			IQK_BB_REG_92C[IQK_BB_REG_NUM] = {
		REG_OFDM_0_XA_RX_IQ_IMBALANCE,	REG_OFDM_0_XB_RX_IQ_IMBALANCE,
		REG_OFDM_0_ECCA_THRESHOLD,	REG_OFDM_0_AGC_RSSI_TABLE,
		REG_OFDM_0_XA_TX_IQ_IMBALANCE,	REG_OFDM_0_XB_TX_IQ_IMBALANCE,
		REG_OFDM_0_XC_TX_AFE,			REG_OFDM_0_XD_TX_AFE,
		REG_OFDM_0_RX_IQ_EXT_ANTA
	};

	if (odm_check_power_status(p_adapter) == false)
		return;

#if MP_DRIVER == 1
	is_start_cont_tx = p_adapter->mpt_ctx.is_start_cont_tx;
	is_single_tone = p_adapter->mpt_ctx.is_single_tone;
	is_carrier_suppression = p_adapter->mpt_ctx.is_carrier_suppression;
#endif

	/* ignore IQK when continuous Tx */
	if (is_start_cont_tx || is_single_tone || is_carrier_suppression)
		return;

#ifdef DISABLE_BB_RF
	return;
#endif
	if (p_adapter->is_slave_of_dmsp)
		return;

	if (is_recovery) {
		phy_reload_adda_registers(p_adapter, IQK_BB_REG_92C, p_hal_data->IQK_BB_backup_recover, 9);
		return;

	}

	RTPRINT(FINIT, INIT_IQK, ("IQK:Start!!!\n"));

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

	acquire_cck_and_rw_page_a_control(p_adapter);
	/*RT_TRACE(COMP_INIT,DBG_LOUD,("Acquire Mutex in IQCalibrate\n"));*/
	for (i = 0; i < 3; i++) {
		/*For 88C 1T1R*/
		_phy_iq_calibrate_8192c(p_adapter, result, i, false);

		if (i == 1) {
			is12simular = phy_simularity_compare(p_adapter, result, 0, 1);
			if (is12simular) {
				final_candidate = 0;
				break;
			}
		}

		if (i == 2) {
			is13simular = phy_simularity_compare(p_adapter, result, 0, 2);
			if (is13simular) {
				final_candidate = 0;
				break;
			}

			is23simular = phy_simularity_compare(p_adapter, result, 1, 2);
			if (is23simular)
				final_candidate = 1;
			else {
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
	release_cck_and_rw_pagea_control(p_adapter);

	for (i = 0; i < 4; i++) {
		rege94 = result[i][0];
		rege9c = result[i][1];
		regea4 = result[i][2];
		regeac = result[i][3];
		regeb4 = result[i][4];
		regebc = result[i][5];
		regec4 = result[i][6];
		regecc = result[i][7];
		RTPRINT(FINIT, INIT_IQK, ("IQK: rege94=%x rege9c=%x regea4=%x regeac=%x regeb4=%x regebc=%x regec4=%x regecc=%x\n ", rege94, rege9c, regea4, regeac, regeb4, regebc, regec4, regecc));
	}

	if (final_candidate != 0xff) {
		p_hal_data->rege94 = rege94 = result[final_candidate][0];
		p_hal_data->rege9c = rege9c = result[final_candidate][1];
		regea4 = result[final_candidate][2];
		regeac = result[final_candidate][3];
		p_hal_data->regeb4 = regeb4 = result[final_candidate][4];
		p_hal_data->regebc = regebc = result[final_candidate][5];
		regec4 = result[final_candidate][6];
		regecc = result[final_candidate][7];
		RTPRINT(FINIT, INIT_IQK, ("IQK: final_candidate is %x\n", final_candidate));
		RTPRINT(FINIT, INIT_IQK, ("IQK: rege94=%x rege9c=%x regea4=%x regeac=%x regeb4=%x regebc=%x regec4=%x regecc=%x\n ", rege94, rege9c, regea4, regeac, regeb4, regebc, regec4, regecc));
		is_patha_ok = is_pathb_ok = true;
	} else {
		rege94 = regeb4 = p_hal_data->rege94 = p_hal_data->regeb4 = 0x100;	/* X default value */
		rege9c = regebc = p_hal_data->rege9c = p_hal_data->regebc = 0x0;		/* Y default value */
	}

	if ((rege94 != 0)/*&&(regea4 != 0)*/) {
		if (p_hal_data->current_band_type == BAND_ON_5G)
			phy_path_a_fill_iqk_matrix_5g_normal(p_adapter, is_patha_ok, result, final_candidate, (regea4 == 0));
		else
			phy_path_a_fill_iqk_matrix(p_adapter, is_patha_ok, result, final_candidate, (regea4 == 0));

	}

	if (IS_92C_SERIAL(p_hal_data->version_id) || IS_92D_SINGLEPHY(p_hal_data->version_id)) {
		if ((regeb4 != 0)/*&&(regec4 != 0)*/) {
			if (p_hal_data->current_band_type == BAND_ON_5G)
				phy_path_b_fill_iqk_matrix_5g_normal(p_adapter, is_pathb_ok, result, final_candidate, (regec4 == 0));
			else
				phy_path_b_fill_iqk_matrix(p_adapter, is_pathb_ok, result, final_candidate, (regec4 == 0));
		}
	}

	phy_save_adda_registers(p_adapter, IQK_BB_REG_92C, p_hal_data->IQK_BB_backup_recover, 9);

}


void
phy_lc_calibrate_8192c(
	struct _ADAPTER	*p_adapter
)
{
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
	bool		is_start_cont_tx = false, is_single_tone = false, is_carrier_suppression = false;
	PMGNT_INFO		p_mgnt_info = &p_adapter->MgntInfo;
	PMGNT_INFO		p_mgnt_info_buddy_adapter;
	u32			timeout = 2000, timecount = 0;
	struct _ADAPTER	*buddy_adapter = p_adapter->buddy_adapter;

#if MP_DRIVER == 1
	is_start_cont_tx = p_adapter->mpt_ctx.is_start_cont_tx;
	is_single_tone = p_adapter->mpt_ctx.is_single_tone;
	is_carrier_suppression = p_adapter->mpt_ctx.is_carrier_suppression;
#endif

#ifdef DISABLE_BB_RF
	return;
#endif

	/* ignore LCK when continuous Tx */
	if (is_start_cont_tx || is_single_tone || is_carrier_suppression)
		return;

	if (buddy_adapter != NULL &&
	    ((p_adapter->interface_index == 0 && p_hal_data->current_band_type == BAND_ON_2_4G) ||
	     (p_adapter->interface_index == 1 && p_hal_data->current_band_type == BAND_ON_5G))) {
		p_mgnt_info_buddy_adapter = &buddy_adapter->MgntInfo;
		while (p_mgnt_info_buddy_adapter->is_scan_in_progress && timecount < timeout) {
			delay_ms(50);
			timecount += 50;
		}
	}

	while (p_mgnt_info->is_scan_in_progress && timecount < timeout) {
		delay_ms(50);
		timecount += 50;
	}

	p_hal_data->is_lck_in_progress = true;

	RTPRINT(FINIT, INIT_IQK, ("LCK:Start!!!interface %d currentband %x delay %d ms\n", p_adapter->interface_index, p_hal_data->current_band_type, timecount));

	/* if(IS_92C_SERIAL(p_hal_data->version_id) || IS_92D_SINGLEPHY(p_hal_data->version_id)) */
	if (IS_2T2R(p_hal_data->version_id))
		phy_lc_calibrate(p_adapter, true);
	else {
		/* For 88C 1T1R */
		phy_lc_calibrate(p_adapter, false);
	}

	p_hal_data->is_lck_in_progress = false;

	RTPRINT(FINIT, INIT_IQK, ("LCK:Finish!!!interface %d\n", p_adapter->interface_index));


}

void
phy_ap_calibrate_8192c(
	struct _ADAPTER	*p_adapter,
	s8		delta
)
{
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);

	/* default disable APK, because Tx NG issue, suggest by Jenyu, 2011.11.25 */
	return;

#ifdef DISABLE_BB_RF
	return;
#endif

#if FOR_BRAZIL_PRETEST != 1
	if (p_hal_data->is_ap_kdone)
#endif
		return;

	if (IS_92C_SERIAL(p_hal_data->version_id))
		_phy_ap_calibrate_8192c(p_adapter, delta, true);
	else {
		/* For 88C 1T1R */
		_phy_ap_calibrate_8192c(p_adapter, delta, false);
	}
}


#endif


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
	struct PHY_DM_STRUCT	*p_dm_odm
)
{
	struct _ADAPTER	*adapter = p_dm_odm->adapter;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if (*p_dm_odm->p_is_fcs_mode_enable)
		return;
#endif



	if (p_dm_odm->is_linked) {
		if ((*p_dm_odm->p_channel != p_dm_odm->pre_channel) && (!*p_dm_odm->p_is_scan_in_process)) {
			p_dm_odm->pre_channel = *p_dm_odm->p_channel;
			p_dm_odm->linked_interval = 0;
		}

		if (p_dm_odm->linked_interval < 3)
			p_dm_odm->linked_interval++;

		if (p_dm_odm->linked_interval == 2) {

#if (RTL8814A_SUPPORT == 1)
			if (p_dm_odm->support_ic_type == ODM_RTL8814A)
				phy_iq_calibrate_8814a(p_dm_odm, false);
#endif

#if (RTL8822B_SUPPORT == 1)
			if (p_dm_odm->support_ic_type == ODM_RTL8822B)
				phy_iq_calibrate_8822b(p_dm_odm, false);
#endif

#if (RTL8821C_SUPPORT == 1)
			if (p_dm_odm->support_ic_type == ODM_RTL8821C)
				phy_iq_calibrate_8821c(p_dm_odm, false);
#endif

#if (RTL8821A_SUPPORT == 1)
			if (p_dm_odm->support_ic_type == ODM_RTL8821)
				phy_iq_calibrate_8821a(p_dm_odm, false);
#endif

#if (RTL8812A_SUPPORT == 1)
			if (p_dm_odm->support_ic_type == ODM_RTL8812)
				_phy_iq_calibrate_8812a(p_dm_odm, false);
#endif
		}
	} else
		p_dm_odm->linked_interval = 0;

}

void phydm_rf_init(void		*p_dm_void)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	odm_txpowertracking_init(p_dm_odm);

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	odm_clear_txpowertracking_state(p_dm_odm);
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
#if (RTL8814A_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & ODM_RTL8814A)
		phy_iq_calibrate_8814a_init(p_dm_odm);
#endif
#endif

}

void phydm_rf_watchdog(void		*p_dm_void)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	odm_txpowertracking_check(p_dm_odm);
	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES)
		odm_iq_calibrate(p_dm_odm);
#endif
}
