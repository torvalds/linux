/******************************************************************************
 *
 * Copyright(c) 2007 - 2020 Realtek Corporation.
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
#ifndef __RTW_NLRTW_H_
#define __RTW_NLRTW_H_

#ifdef CONFIG_RTW_NLRTW
int rtw_nlrtw_init(void);
int rtw_nlrtw_deinit(void);
int rtw_nlrtw_ch_util_rpt(_adapter *adapter, u8 n_rpts, u8 *val, u8 **mac_addr);
int rtw_nlrtw_reg_change_event(_adapter *adapter);
int rtw_nlrtw_reg_beacon_hint_event(_adapter *adapter);
int rtw_nlrtw_radio_opmode_notify(struct rf_ctl_t *rfctl);
#else
static inline int rtw_nlrtw_init(void) {return _FAIL;}
static inline int rtw_nlrtw_deinit(void) {return _FAIL;}
static inline int rtw_nlrtw_ch_util_rpt(_adapter *adapter, u8 n_rpts, u8 *val, u8 **mac_addr) {return _FAIL;}
static inline int rtw_nlrtw_reg_change_event(_adapter *adapter) {return _FAIL;}
static inline int rtw_nlrtw_reg_beacon_hint_event(_adapter *adapter) {return _FAIL;}
static inline int rtw_nlrtw_radio_opmode_notify(struct rf_ctl_t *rfctl) {return _FAIL;}
#endif /* CONFIG_RTW_NLRTW */

#if defined(CONFIG_RTW_NLRTW) && defined(CONFIG_DFS_MASTER)
int rtw_nlrtw_radar_detect_event(_adapter *adapter, u8 cch, u8 bw);
int rtw_nlrtw_cac_finish_event(_adapter *adapter, u8 cch, u8 bw);
int rtw_nlrtw_cac_abort_event(_adapter *adapter, u8 cch, u8 bw);
int rtw_nlrtw_nop_finish_event(_adapter *adapter, u8 cch, u8 bw);
int rtw_nlrtw_nop_start_event(_adapter *adapter, u8 cch, u8 bw);
#else
static inline int rtw_nlrtw_radar_detect_event(_adapter *adapter, u8 cch, u8 bw) {return _FAIL;}
static inline int rtw_nlrtw_cac_finish_event(_adapter *adapter, u8 cch, u8 bw) {return _FAIL;}
static inline int rtw_nlrtw_cac_abort_event(_adapter *adapter, u8 cch, u8 bw) {return _FAIL;}
static inline int rtw_nlrtw_nop_finish_event(_adapter *adapter, u8 cch, u8 bw) {return _FAIL;}
static inline int rtw_nlrtw_nop_start_event(_adapter *adapter, u8 cch, u8 bw) {return _FAIL;}
#endif /* defined(CONFIG_RTW_NLRTW) && defined(CONFIG_DFS_MASTER) */

#endif /* __RTW_NLRTW_H_ */
