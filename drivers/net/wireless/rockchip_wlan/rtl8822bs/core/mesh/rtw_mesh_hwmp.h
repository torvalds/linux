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
#ifndef __RTW_MESH_HWMP_H_
#define __RTW_MESH_HWMP_H_

#ifndef DBG_RTW_HWMP
#define DBG_RTW_HWMP 0
#endif
#if DBG_RTW_HWMP
#define RTW_HWMP_DBG(fmt, arg...) RTW_PRINT(fmt, ##arg)
#else
#define RTW_HWMP_DBG(fmt, arg...) RTW_DBG(fmt, ##arg)
#endif

#ifndef INFO_RTW_HWMP
#define INFO_RTW_HWMP 0
#endif
#if INFO_RTW_HWMP
#define RTW_HWMP_INFO(fmt, arg...) RTW_PRINT(fmt, ##arg)
#else
#define RTW_HWMP_INFO(fmt, arg...) RTW_INFO(fmt, ##arg)
#endif


void rtw_ewma_err_rate_init(struct rtw_ewma_err_rate *e);
unsigned long rtw_ewma_err_rate_read(struct rtw_ewma_err_rate *e);
void rtw_ewma_err_rate_add(struct rtw_ewma_err_rate *e, unsigned long val);
int rtw_mesh_path_error_tx(_adapter *adapter,
			   u8 ttl, const u8 *target, u32 target_sn,
			   u16 target_rcode, const u8 *ra);
void rtw_ieee80211s_update_metric(_adapter *adapter, u8 mac_id,
				  u8 per, u8 rate,
				  u8 bw, u8 total_pkt);
void rtw_mesh_rx_path_sel_frame(_adapter *adapter, union recv_frame *rframe);
void rtw_mesh_queue_preq(struct rtw_mesh_path *mpath, u8 flags);
void rtw_mesh_path_start_discovery(_adapter *adapter);
void rtw_mesh_path_timer(void *ctx);
void rtw_mesh_path_tx_root_frame(_adapter *adapter);
void rtw_mesh_work_hdl(_workitem *work);
void rtw_ieee80211_mesh_path_timer(void *ctx);
void rtw_ieee80211_mesh_path_root_timer(void *ctx);
BOOLEAN rtw_ieee80211_mesh_root_setup(_adapter *adapter);
void rtw_mesh_work(_workitem *work);
void rtw_mesh_atlm_param_req_timer(void *ctx);

#endif /* __RTW_MESH_HWMP_H_ */


