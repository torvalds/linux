/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __PHYDMADAPTIVITY_H__
#define __PHYDMADAPTIVITY_H__

/*20160902 changed by Kevin, refine method for searching pwdb lower bound*/
#define ADAPTIVITY_VERSION "9.3.5"

#define pwdb_upper_bound 7
#define dfir_loss 5

enum phydm_adapinfo {
	PHYDM_ADAPINFO_CARRIER_SENSE_ENABLE = 0,
	PHYDM_ADAPINFO_DCBACKOFF,
	PHYDM_ADAPINFO_DYNAMICLINKADAPTIVITY,
	PHYDM_ADAPINFO_TH_L2H_INI,
	PHYDM_ADAPINFO_TH_EDCCA_HL_DIFF,
	PHYDM_ADAPINFO_AP_NUM_TH

};

enum phydm_set_lna {
	phydm_disable_lna = 0,
	phydm_enable_lna = 1,
};

enum phydm_trx_mux_type {
	phydm_shutdown = 0,
	phydm_standby_mode = 1,
	phydm_tx_mode = 2,
	phydm_rx_mode = 3
};

enum phydm_mac_edcca_type {
	phydm_ignore_edcca = 0,
	phydm_dont_ignore_edcca = 1
};

struct adaptivity_statistics {
	s8 th_l2h_ini_backup;
	s8 th_edcca_hl_diff_backup;
	s8 igi_base;
	u8 igi_target;
	u8 nhm_wait;
	s8 h2l_lb;
	s8 l2h_lb;
	bool is_first_link;
	bool is_check;
	bool dynamic_link_adaptivity;
	u8 ap_num_th;
	u8 adajust_igi_level;
	bool acs_for_adaptivity;
	s8 backup_l2h;
	s8 backup_h2l;
	bool is_stop_edcca;
};

void phydm_pause_edcca(void *dm_void, bool is_pasue_edcca);

void phydm_check_adaptivity(void *dm_void);

void phydm_check_environment(void *dm_void);

void phydm_nhm_counter_statistics_init(void *dm_void);

void phydm_nhm_counter_statistics(void *dm_void);

void phydm_nhm_counter_statistics_reset(void *dm_void);

void phydm_get_nhm_counter_statistics(void *dm_void);

void phydm_mac_edcca_state(void *dm_void, enum phydm_mac_edcca_type state);

void phydm_set_edcca_threshold(void *dm_void, s8 H2L, s8 L2H);

void phydm_set_trx_mux(void *dm_void, enum phydm_trx_mux_type tx_mode,
		       enum phydm_trx_mux_type rx_mode);

bool phydm_cal_nhm_cnt(void *dm_void);

void phydm_search_pwdb_lower_bound(void *dm_void);

void phydm_adaptivity_info_init(void *dm_void, enum phydm_adapinfo cmn_info,
				u32 value);

void phydm_adaptivity_init(void *dm_void);

void phydm_adaptivity(void *dm_void);

void phydm_set_edcca_threshold_api(void *dm_void, u8 IGI);

void phydm_pause_edcca_work_item_callback(void *dm_void);

void phydm_resume_edcca_work_item_callback(void *dm_void);

#endif
