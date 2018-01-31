/* SPDX-License-Identifier: GPL-2.0 */
#ifndef	__PHYDMCCX_H__
#define    __PHYDMCCX_H__

#define CCX_EN 1

#define SET_NHM_SETTING		0
#define STORE_NHM_SETTING		1
#define RESTORE_NHM_SETTING	2

/*
#define NHM_EXCLUDE_CCA			0
#define NHM_INCLUDE_CCA			1
#define NHM_EXCLUDE_TXON			0
#define NHM_INCLUDE_TXON			1
*/

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
	u8					NHM_th[11];
	u16					NHM_period;				/* 4us per unit */
	u16					CLM_period;				/* 4us per unit */
	enum nhm_inexclude_txon		nhm_inexclude_txon;
	enum nhm_inexclude_cca		nhm_inexclude_cca;

	/*Previous Settings*/
	u8					NHM_th_restore[11];
	u16					NHM_period_restore;				/* 4us per unit */
	u16					CLM_period_restore;				/* 4us per unit */
	enum nhm_inexclude_txon		NHM_inexclude_txon_restore;
	enum nhm_inexclude_cca		NHM_inexclude_cca_restore;

	/*Report*/
	u8		NHM_result[12];
	u16		NHM_duration;
	u16		CLM_result;


	bool		echo_NHM_en;
	bool		echo_CLM_en;
	u8		echo_IGI;

};

/*NHM*/

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

bool
phydm_check_nhm_ready(
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

bool
phydm_check_cl_mready(
	void			*p_dm_void
);

void
phydm_get_cl_mresult(
	void			*p_dm_void
);


#endif
