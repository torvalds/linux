/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#ifndef _TSTYPE_H_
#define _TSTYPE_H_
#include "rtl819x_Qos.h"
#define TS_ADDBA_DELAY		60

#define TOTAL_TS_NUM		16

enum tr_select {
	TX_DIR = 0,
	RX_DIR = 1,
};

struct ts_common_info {
	struct list_head		list;
	u8				addr[ETH_ALEN];
	struct qos_tsinfo tspec;
};

struct tx_ts_record {
	struct ts_common_info ts_common_info;
	u16				tx_cur_seq;
	struct ba_record tx_pending_ba_record;
	struct ba_record tx_admitted_ba_record;
	u8				add_ba_req_in_progress;
	u8				add_ba_req_delayed;
	u8				using_ba;
	u8				disable_add_ba;
	struct timer_list		ts_add_ba_timer;
	u8				num;
};

struct rx_ts_record {
	struct ts_common_info ts_common_info;
	u16 rx_indicate_seq;
	u16 rx_timeout_indicate_seq;
	struct list_head rx_pending_pkt_list;
	struct timer_list rx_pkt_pending_timer;
	struct ba_record rx_admitted_ba_record;
	u16 rx_last_seq_num;
	u8 rx_last_frag_num;
	u8 num;
};

#endif
