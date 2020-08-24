/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
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

#ifndef __PHYDMPATHDIV_H__
#define __PHYDMPATHDIV_H__

#ifdef CONFIG_PATH_DIVERSITY
/* @2019.03.07 open resp tx path h2c only for 1ss status*/
#define PATHDIV_VERSION "4.4"

#if (RTL8192F_SUPPORT || RTL8822B_SUPPORT || RTL8822C_SUPPORT ||\
	RTL8812F_SUPPORT || RTL8197G_SUPPORT)
	#define PHYDM_CONFIG_PATH_DIV_V2
#endif

#define USE_PATH_A_AS_DEFAULT_ANT /* @for 8814 dynamic TX path selection */

#define NUM_RESET_DTP_PERIOD 5
#define ANT_DECT_RSSI_TH 3

#define PATH_A 1
#define PATH_B 2
#define PATH_C 3
#define PATH_D 4

#define PHYDM_AUTO_PATH 0
#define PHYDM_FIX_PATH 1

#define NUM_CHOOSE2_FROM4 6
#define NUM_CHOOSE3_FROM4 4

enum phydm_dtp_state {
	PHYDM_DTP_INIT = 1,
	PHYDM_DTP_RUNNING_1
};

enum phydm_path_div_type {
	PHYDM_2R_PATH_DIV = 1,
	PHYDM_4R_PATH_DIV = 2
};

enum phydm_path_ctrl {
	TX_PATH_BY_REG = 0,
	TX_PATH_BY_DESC = 1,
	TX_PATH_CTRL_INIT
};

struct path_txdesc_ctrl {
	u8 ant_map_a : 2;
	u8 ant_map_b : 2;
	u8 ntx_map : 4;
};

struct _ODM_PATH_DIVERSITY_ {
	boolean stop_path_div; /*@Limit by enabled path number*/
	boolean path_div_in_progress;
	boolean	cck_fix_path_en; /*@ BB Reg for Adv-Ctrl (or debug mode)*/
	boolean	ofdm_fix_path_en; /*@ BB Reg for Adv-Ctrl (or debug mode)*/
	enum bb_path cck_fix_path_sel; /*@ BB Reg for Adv-Ctrl (or debug mode)*/
	enum bb_path ofdm_fix_path_sel;/*@ BB Reg for Adv-Ctrl (or debug mode)*/
	enum phydm_path_ctrl tx_path_ctrl;
	enum bb_path default_tx_path;
	enum bb_path path_sel[ODM_ASSOCIATE_ENTRY_NUM];
	u32	path_a_sum[ODM_ASSOCIATE_ENTRY_NUM];
	u32	path_b_sum[ODM_ASSOCIATE_ENTRY_NUM];
	u16	path_a_cnt[ODM_ASSOCIATE_ENTRY_NUM];
	u16	path_b_cnt[ODM_ASSOCIATE_ENTRY_NUM];
	u8	phydm_path_div_type;
	boolean force_update;
#if RTL8814A_SUPPORT

	u32	path_a_sum_all;
	u32	path_b_sum_all;
	u32	path_c_sum_all;
	u32	path_d_sum_all;

	u32	path_a_cnt_all;
	u32	path_b_cnt_all;
	u32	path_c_cnt_all;
	u32	path_d_cnt_all;

	u8	dtp_period;
	boolean	is_become_linked;
	boolean	is_u3_mode;
	u8	num_tx_path;
	u8	default_path;
	u8	num_candidate;
	u8	ant_candidate_1;
	u8	ant_candidate_2;
	u8	ant_candidate_3;
	u8     phydm_dtp_state;
	u8	dtp_check_patha_counter;
	boolean	fix_path_bfer;
	u8	search_space_2[NUM_CHOOSE2_FROM4];
	u8	search_space_3[NUM_CHOOSE3_FROM4];

	u8	pre_tx_path;
	u8	use_path_a_as_default_ant;
	boolean	is_path_a_exist;

#endif
};

void phydm_set_tx_path_by_bb_reg(void *dm_void, enum bb_path tx_path_sel_1ss);

void phydm_get_tx_path_txdesc_jgr3(void *dm_void, u8 macid,
				   struct path_txdesc_ctrl *desc);

void phydm_c2h_dtp_handler(void *dm_void, u8 *cmd_buf, u8 cmd_len);

void phydm_tx_path_diversity_init(void *dm_void);

void phydm_tx_path_diversity(void *dm_void);

void phydm_process_rssi_for_path_div(void *dm_void, void *phy_info_void,
				     void *pkt_info_void);

void phydm_pathdiv_debug(void *dm_void, char input[][16], u32 *_used,
			 char *output, u32 *_out_len);

#endif /* @#ifdef CONFIG_PATH_DIVERSITY */
#endif /* @#ifndef  __PHYDMPATHDIV_H__ */

