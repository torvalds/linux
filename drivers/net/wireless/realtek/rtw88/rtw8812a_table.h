/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2024  Realtek Corporation
 */

#ifndef __RTW8812A_TABLE_H__
#define __RTW8812A_TABLE_H__

extern const struct rtw_table rtw8812a_mac_tbl;
extern const struct rtw_table rtw8812a_agc_tbl;
extern const struct rtw_table rtw8812a_agc_diff_lb_tbl;
extern const struct rtw_table rtw8812a_agc_diff_hb_tbl;
extern const struct rtw_table rtw8812a_bb_tbl;
extern const struct rtw_table rtw8812a_bb_pg_tbl;
extern const struct rtw_table rtw8812a_bb_pg_rfe3_tbl;
extern const struct rtw_table rtw8812a_rf_a_tbl;
extern const struct rtw_table rtw8812a_rf_b_tbl;
extern const struct rtw_table rtw8812a_txpwr_lmt_tbl;

extern const struct rtw_pwr_seq_cmd * const card_enable_flow_8812a[];
extern const struct rtw_pwr_seq_cmd * const enter_lps_flow_8812a[];
extern const struct rtw_pwr_seq_cmd * const card_disable_flow_8812a[];

extern const struct rtw_pwr_track_tbl rtw8812a_rtw_pwr_track_tbl;
extern const struct rtw_pwr_track_tbl rtw8812a_rtw_pwr_track_rfe3_tbl;

#endif
