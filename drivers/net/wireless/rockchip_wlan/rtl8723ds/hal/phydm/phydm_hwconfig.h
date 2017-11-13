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


#ifndef	__HALHWOUTSRC_H__
#define __HALHWOUTSRC_H__


/*--------------------------Define -------------------------------------------*/
#define AGC_DIFF_CONFIG_MP(ic, band) (odm_read_and_config_mp_##ic##_agc_tab_diff(p_dm, array_mp_##ic##_agc_tab_diff_##band, \
		      sizeof(array_mp_##ic##_agc_tab_diff_##band)/sizeof(u32)))
#define AGC_DIFF_CONFIG_TC(ic, band) (odm_read_and_config_tc_##ic##_agc_tab_diff(p_dm, array_tc_##ic##_agc_tab_diff_##band, \
		      sizeof(array_tc_##ic##_agc_tab_diff_##band)/sizeof(u32)))

#define AGC_DIFF_CONFIG(ic, band) do {\
		if (p_dm->is_mp_chip)\
			AGC_DIFF_CONFIG_MP(ic, band);\
		else\
			AGC_DIFF_CONFIG_TC(ic, band);\
	} while (0)


/* ************************************************************
 * structure and define
 * ************************************************************ */

enum hal_status
odm_config_rf_with_tx_pwr_track_header_file(
	struct PHY_DM_STRUCT		*p_dm
);

enum hal_status
odm_config_rf_with_header_file(
	struct PHY_DM_STRUCT		*p_dm,
	enum odm_rf_config_type		config_type,
	u8						e_rf_path
);

enum hal_status
odm_config_bb_with_header_file(
	struct PHY_DM_STRUCT	*p_dm,
	enum odm_bb_config_type		config_type
);

enum hal_status
odm_config_mac_with_header_file(
	struct PHY_DM_STRUCT	*p_dm
);

u32
odm_get_hw_img_version(
	struct PHY_DM_STRUCT	*p_dm
);


u32
query_phydm_trx_capability(
	struct PHY_DM_STRUCT					*p_dm
);

u32
query_phydm_stbc_capability(
	struct PHY_DM_STRUCT					*p_dm
);

u32
query_phydm_ldpc_capability(
	struct PHY_DM_STRUCT					*p_dm
);

u32
query_phydm_txbf_parameters(
	struct PHY_DM_STRUCT					*p_dm
);

u32
query_phydm_txbf_capability(
	struct PHY_DM_STRUCT					*p_dm
);

#endif /*#ifndef	__HALHWOUTSRC_H__*/
