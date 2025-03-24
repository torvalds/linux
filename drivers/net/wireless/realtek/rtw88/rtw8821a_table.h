/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2024  Realtek Corporation
 */

#ifndef __RTW8821A_TABLE_H__
#define __RTW8821A_TABLE_H__

extern const struct rtw_table rtw8821a_mac_tbl;
extern const struct rtw_table rtw8821a_agc_tbl;
extern const struct rtw_table rtw8821a_bb_tbl;
extern const struct rtw_table rtw8821a_bb_pg_tbl;
extern const struct rtw_table rtw8821a_rf_a_tbl;
extern const struct rtw_table rtw8821a_txpwr_lmt_tbl;

extern const struct rtw_pwr_seq_cmd * const card_enable_flow_8821a[];
extern const struct rtw_pwr_seq_cmd * const enter_lps_flow_8821a[];
extern const struct rtw_pwr_seq_cmd * const card_disable_flow_8821a[];

extern const struct rtw_pwr_track_tbl rtw8821a_rtw_pwr_track_tbl;

#endif
