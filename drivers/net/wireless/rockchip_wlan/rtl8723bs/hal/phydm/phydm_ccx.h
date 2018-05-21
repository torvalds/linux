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

#ifndef	__PHYDMCCX_H__
#define    __PHYDMCCX_H__

#define CCX_EN 1

#define IGI_TO_NHM_TH_MULTIPLIER	2
#define CCA_CAP	14



enum nhm_setting {
	SET_NHM_SETTING,
	STORE_NHM_SETTING,
	RESTORE_NHM_SETTING
};

enum nhm_inexclude_cca {
	NHM_EXCLUDE_CCA,
	NHM_INCLUDE_CCA
};

enum nhm_inexclude_txon {
	NHM_EXCLUDE_TXON,
	NHM_INCLUDE_TXON
};


struct _CCX_INFO {

	/*Settings*/
	u8					nhm_th[11];
	u16					nhm_period;				/* 4us per unit */
	u16					clm_period;				/* 4us per unit */
	enum nhm_inexclude_txon		nhm_inexclude_txon;
	enum nhm_inexclude_cca		nhm_inexclude_cca;

	/*Previous Settings*/
	u8					nhm_th_restore[11];
	u16					nhm_period_restore;				/* 4us per unit */
	u16					clm_period_restore;				/* 4us per unit */
	enum nhm_inexclude_txon		nhm_inexclude_txon_restore;
	enum nhm_inexclude_cca		nhm_inexclude_cca_restore;

	/*Report*/
	u8		nhm_result[12];
	u8		nhm_ratio;		/*1% per nuit, it means the interference igi can't overcome.*/
	u8		nhm_result_total;
	u16		nhm_duration;
	u16		clm_result;
	u8		clm_ratio;

	boolean		echo_clm_en;
	u8			echo_igi;	/* nhm_result comes from this igi */

};

/*NHM*/

void
phydm_nhm_init(
	void					*p_dm_void
);

boolean
phydm_cal_nhm_cnt(
	void		*p_dm_void
);

void
phydm_nhm_setting(
	void		*p_dm_void,
	u8	nhm_setting
);

void
phydm_nhm_trigger(
	void		*p_dm_void
);

void
phydm_get_nhm_result(
	void		*p_dm_void
);

boolean
phydm_check_nhm_rdy(
	void		*p_dm_void
);

/*CLM*/

void
phydm_clm_setting(
	void			*p_dm_void
);

void
phydm_clm_trigger(
	void			*p_dm_void
);

boolean
phydm_check_clm_rdy(
	void			*p_dm_void
);

void
phydm_get_clm_result(
	void			*p_dm_void
);

void
phydm_ccx_monitor(
	void			*p_dm_void
);

void
phydm_ccx_monitor_trigger(
	void			*p_dm_void,
	u16				monitor_time
);

void
phydm_ccx_monitor_result(
	void			*p_dm_void
);

void
phydm_set_nhm_th_by_igi(
	void			*p_dm_void,
	u8				igi
);

#endif
