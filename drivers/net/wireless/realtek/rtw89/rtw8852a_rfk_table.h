/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#ifndef __RTW89_8852A_RFK_TABLE_H__
#define __RTW89_8852A_RFK_TABLE_H__

#include "core.h"

enum rtw89_rfk_flag {
	RTW89_RFK_F_WRF = 0,
	RTW89_RFK_F_WM = 1,
	RTW89_RFK_F_WS = 2,
	RTW89_RFK_F_WC = 3,
	RTW89_RFK_F_DELAY = 4,
	RTW89_RFK_F_NUM,
};

struct rtw89_rfk_tbl {
	const struct rtw89_reg5_def *defs;
	u32 size;
};

#define DECLARE_RFK_TBL(_name)			\
const struct rtw89_rfk_tbl _name ## _tbl = {	\
	.defs = _name,				\
	.size = ARRAY_SIZE(_name),		\
}

#define DECL_RFK_WRF(_path, _addr, _mask, _data)	\
	{.flag = RTW89_RFK_F_WRF,			\
	 .path = _path,					\
	 .addr = _addr,					\
	 .mask = _mask,					\
	 .data = _data,}

#define DECL_RFK_WM(_addr, _mask, _data)	\
	{.flag = RTW89_RFK_F_WM,		\
	 .addr = _addr,				\
	 .mask = _mask,				\
	 .data = _data,}

#define DECL_RFK_WS(_addr, _mask)	\
	{.flag = RTW89_RFK_F_WS,	\
	 .addr = _addr,			\
	 .mask = _mask,}

#define DECL_RFK_WC(_addr, _mask)	\
	{.flag = RTW89_RFK_F_WC,	\
	 .addr = _addr,			\
	 .mask = _mask,}

#define DECL_RFK_DELAY(_data)		\
	{.flag = RTW89_RFK_F_DELAY,	\
	 .data = _data,}

extern const struct rtw89_rfk_tbl rtw8852a_tssi_sys_defs_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_sys_defs_2g_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_sys_defs_5g_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_txpwr_ctrl_bb_defs_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_txpwr_ctrl_bb_defs_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_txpwr_ctrl_bb_defs_2g_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_txpwr_ctrl_bb_defs_5g_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_txpwr_ctrl_bb_he_tb_defs_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_txpwr_ctrl_bb_he_tb_defs_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_dck_defs_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_dck_defs_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_dac_gain_tbl_defs_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_dac_gain_tbl_defs_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_slope_cal_org_defs_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_slope_cal_org_defs_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_rf_gap_tbl_defs_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_rf_gap_tbl_defs_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_slope_defs_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_slope_defs_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_track_defs_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_track_defs_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_txagc_ofst_mv_avg_defs_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_txagc_ofst_mv_avg_defs_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_pak_defs_a_2g_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_pak_defs_a_5g_1_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_pak_defs_a_5g_3_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_pak_defs_a_5g_4_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_pak_defs_b_2g_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_pak_defs_b_5g_1_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_pak_defs_b_5g_3_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_pak_defs_b_5g_4_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_enable_defs_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_enable_defs_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_enable_defs_ab_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_disable_defs_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_tssi_tracking_defs_tbl;

extern const struct rtw89_rfk_tbl rtw8852a_rfk_afe_init_defs_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dack_reload_defs_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dack_reload_defs_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_check_addc_defs_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_check_addc_defs_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_addck_reset_defs_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_addck_trigger_defs_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_addck_restore_defs_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_addck_reset_defs_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_addck_trigger_defs_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_addck_restore_defs_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_check_dadc_defs_f_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_check_dadc_defs_f_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_check_dadc_defs_r_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_check_dadc_defs_r_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dack_defs_f_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dack_defs_m_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dack_defs_r_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dack_defs_f_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dack_defs_m_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dack_defs_r_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dpk_bb_afe_sf_defs_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dpk_bb_afe_sr_defs_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dpk_bb_afe_sf_defs_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dpk_bb_afe_sr_defs_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dpk_bb_afe_s_defs_ab_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dpk_bb_afe_r_defs_a_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dpk_bb_afe_r_defs_b_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dpk_bb_afe_r_defs_ab_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dpk_lbk_rxiqk_defs_f_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dpk_lbk_rxiqk_defs_r_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_dpk_pas_read_defs_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_iqk_set_defs_nondbcc_path01_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_iqk_set_defs_dbcc_path0_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_iqk_set_defs_dbcc_path1_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_iqk_restore_defs_nondbcc_path01_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_iqk_restore_defs_dbcc_path0_tbl;
extern const struct rtw89_rfk_tbl rtw8852a_rfk_iqk_restore_defs_dbcc_path1_tbl;

#endif
