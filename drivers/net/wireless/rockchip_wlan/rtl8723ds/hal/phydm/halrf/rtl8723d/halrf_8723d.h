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

#ifndef __HAL_PHY_RF_8723D_H__
#define __HAL_PHY_RF_8723D_H__

/*--------------------------Define Parameters-------------------------------*/
#define	IQK_DELAY_TIME_8723D		10		/* ms */
#define	index_mapping_NUM_8723D	15
#define AVG_THERMAL_NUM_8723D	4
#define RF_T_METER_8723D 0x42

void configure_txpower_track_8723d(
	struct _TXPWRTRACK_CFG	*p_config
);

void
get_delta_swing_table_8723d(
	void		*p_dm_void,
	u8 **temperature_up_a,
	u8 **temperature_down_a,
	u8 **temperature_up_b,
	u8 **temperature_down_b
);

void
set_cck_filter_coefficient_8723d(
	struct PHY_DM_STRUCT	*p_dm,
	u8		cck_swing_index
);

void do_iqk_8723d(
	void		*p_dm_void,
	u8		delta_thermal_index,
	u8		thermal_value,
	u8		threshold
);

void
odm_tx_pwr_track_set_pwr_8723d(
	void		*p_dm_void,
	enum pwrtrack_method	method,
	u8				rf_path,
	u8				channel_mapped_index
);

void
odm_txxtaltrack_set_xtal_8723d(
	void		*p_dm_void
);

/* 1 7.	IQK */

void
phy_iq_calibrate_8723d(
	void		*p_dm_void,
	boolean	is_recovery);


/*
 * LC calibrate
 *   */
void
phy_lc_calibrate_8723d(
	void		*p_dm_void
);


void phy_set_rf_path_switch_8723d(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	boolean		is_main
);

#if 0
/*
 * AP calibrate
 *   */
void
phy_ap_calibrate_8723d(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm,
#else
	struct _ADAPTER	*p_adapter,
#endif
	s8		delta);
void
phy_digital_predistortion_8723d(struct _ADAPTER	*p_adapter);
#endif

void
_phy_save_adda_registers_8723d(
	struct PHY_DM_STRUCT		*p_dm,
	u32		*adda_reg,
	u32		*adda_backup,
	u32		register_num
);

void
_phy_path_adda_on_8723d(
	struct PHY_DM_STRUCT		*p_dm,
	u32		*adda_reg,
	boolean		is_path_a_on,
	boolean		is2T
);

void
_phy_mac_setting_calibration_8723d(
	struct PHY_DM_STRUCT		*p_dm,
	u32		*mac_reg,
	u32		*mac_backup
);


#endif	/*  #ifndef __HAL_PHY_RF_8723D_H__ */
