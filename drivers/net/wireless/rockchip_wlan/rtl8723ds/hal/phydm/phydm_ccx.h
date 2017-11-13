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

/* 1 ============================================================
 * 1  Definition
 * 1 ============================================================ */

#define CCX_EN 1

#define	IGI_TO_NHM_TH_MULTIPLIER	2
#define	MS_TO_4US_RATIO				250
#define	CCA_CAP	14
#define	CLM_MAX_REPORT_TIME	10
#define	DEVIDER_ERROR	0xffff

/*FAHM*/
#define	FAHM_INCLD_FA			BIT(0)
#define	FAHM_INCLD_CRC_OK	BIT(1)
#define	FAHM_INCLD_CRC_ER		BIT(2)


/* 1 ============================================================
 * 1 enumrate
 * 1 ============================================================ */

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

enum clm_monitor_mode_e {
	CLM_ECHO_DBG_MODE	= 0,
	CLM_DRIVER_MNTR		= 1,
	CLM_FW_MNTR			= 2
};

/* 1 ============================================================
 * 1  structure
 * 1 ============================================================ */

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
	u8		echo_igi;	/* nhm_result comes from this igi */
	u32		clm_fw_result_acc;
	u8		clm_fw_result_cnt;
	enum clm_monitor_mode_e		clm_mntr_mode;
	u8		env_mntr_igi;
#ifdef FAHM_SUPPORT
	u8		fahm_nume_sel;		/*fahm_numerator_sel: select {FA, CRCOK, CRC_fail} */
	u8		fahm_denum_sel;	/*fahm_denumerator_sel: select {FA, CRCOK, CRC_fail} */
	u16		fahm_period;			/*unit: 4us*/
#endif

};

/* 1 ============================================================
 * 1 structure
 * 1 ============================================================ */

#ifdef FAHM_SUPPORT

void
phydm_fahm_init(
	void			*p_dm_void
);

void
phydm_fahm_dbg(
	void		*p_dm_void,
	char		input[][16],
	u32		*_used,
	char		*output,
	u32		*_out_len,
	u32		input_num
);

#endif

void
phydm_c2h_clm_report_handler(
	void	*p_dm_void,
	u8	*cmd_buf,
	u8	cmd_len
);
/*NHM*/

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

/*CLM*/

void
phydm_clm_setting(
	void			*p_dm_void
);

void
phydm_clm_trigger(
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
phydm_set_clm_mntr_mode(
	void			*p_dm_void,
	enum clm_monitor_mode_e mode
);

void
phydm_env_monitor_init(
	void			*p_dm_void
);

void
phydm_clm_dbg(
	void		*p_dm_void,
	char		input[][16],
	u32		*_used,
	char		*output,
	u32		*_out_len,
	u32		input_num
);

#endif
