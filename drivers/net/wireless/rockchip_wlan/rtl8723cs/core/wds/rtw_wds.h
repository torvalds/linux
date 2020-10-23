/******************************************************************************
 *
 * Copyright(c) 2007 - 2019 Realtek Corporation.
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
#ifndef __RTW_WDS_H_
#define __RTW_WDS_H_

#ifdef CONFIG_AP_MODE
struct rtw_wds_path {
	u8 dst[ETH_ALEN];
	rtw_rhash_head rhash;
	_adapter *adapter;
	struct sta_info __rcu *next_hop;
	rtw_rcu_head rcu;
	systime last_update;
};

struct rtw_wds_table {
	rtw_rhashtable rhead;
};

#define RTW_WDS_PATH_EXPIRE (600 * HZ)

/* Maximum number of paths per interface */
#define RTW_WDS_MAX_PATHS		1024

int rtw_wds_nexthop_lookup(_adapter *adapter, const u8 *da, u8 *ra);

struct rtw_wds_path *rtw_wds_path_lookup(_adapter *adapter, const u8 *dst);
void dump_wpath(void *sel, _adapter *adapter);

void rtw_wds_path_expire(_adapter *adapter);

struct rtw_wds_path *rtw_wds_path_add(_adapter *adapter, const u8 *dst, struct sta_info *next_hop);
void rtw_wds_path_assign_nexthop(struct rtw_wds_path *path, struct sta_info *sta);

int rtw_wds_pathtbl_init(_adapter *adapter);
void rtw_wds_pathtbl_unregister(_adapter *adapter);

void rtw_wds_path_flush_by_nexthop(struct sta_info *sta);
#endif /* CONFIG_AP_MODE */

struct rtw_wds_gptr_table {
	rtw_rhashtable rhead;
};

void dump_wgptr(void *sel, _adapter *adapter);
bool rtw_rx_wds_gptr_check(_adapter *adapter, const u8 *src);
void rtw_tx_wds_gptr_update(_adapter *adapter, const u8 *src);
void rtw_wds_gptr_expire(_adapter *adapter);
int rtw_wds_gptr_tbl_init(_adapter *adapter);
void rtw_wds_gptr_tbl_unregister(_adapter *adapter);

#endif /* __RTW_WDSH_ */

