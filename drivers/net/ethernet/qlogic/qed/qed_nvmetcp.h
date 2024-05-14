/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* Copyright 2021 Marvell. All rights reserved. */

#ifndef _QED_NVMETCP_H
#define _QED_NVMETCP_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/qed/tcp_common.h>
#include <linux/qed/qed_nvmetcp_if.h>
#include <linux/qed/qed_chain.h>
#include "qed.h"
#include "qed_hsi.h"
#include "qed_mcp.h"
#include "qed_sp.h"

#define QED_NVMETCP_FW_CQ_SIZE (4 * 1024)

/* tcp parameters */
#define QED_TCP_FLOW_LABEL 0
#define QED_TCP_TWO_MSL_TIMER 4000
#define QED_TCP_HALF_WAY_CLOSE_TIMEOUT 10
#define QED_TCP_MAX_FIN_RT 2
#define QED_TCP_SWS_TIMER 5000

struct qed_nvmetcp_info {
	spinlock_t lock; /* Connection resources. */
	struct list_head free_list;
	u16 max_num_outstanding_tasks;
	void *event_context;
	nvmetcp_event_cb_t event_cb;
};

struct qed_hash_nvmetcp_con {
	struct hlist_node node;
	struct qed_nvmetcp_conn *con;
};

struct qed_nvmetcp_conn {
	struct list_head list_entry;
	bool free_on_delete;
	u16 conn_id;
	u32 icid;
	u32 fw_cid;
	u8 layer_code;
	u8 offl_flags;
	u8 connect_mode;
	dma_addr_t sq_pbl_addr;
	struct qed_chain r2tq;
	struct qed_chain xhq;
	struct qed_chain uhq;
	u8 local_mac[6];
	u8 remote_mac[6];
	u8 ip_version;
	u8 ka_max_probe_cnt;
	u16 vlan_id;
	u16 tcp_flags;
	u32 remote_ip[4];
	u32 local_ip[4];
	u32 flow_label;
	u32 ka_timeout;
	u32 ka_interval;
	u32 max_rt_time;
	u8 ttl;
	u8 tos_or_tc;
	u16 remote_port;
	u16 local_port;
	u16 mss;
	u8 rcv_wnd_scale;
	u32 rcv_wnd;
	u32 cwnd;
	u8 update_flag;
	u8 default_cq;
	u8 abortive_dsconnect;
	u32 max_seq_size;
	u32 max_recv_pdu_length;
	u32 max_send_pdu_length;
	u32 first_seq_length;
	u16 physical_q0;
	u16 physical_q1;
	u16 nvmetcp_cccid_max_range;
	dma_addr_t nvmetcp_cccid_itid_table_addr;
};

#if IS_ENABLED(CONFIG_QED_NVMETCP)
int qed_nvmetcp_alloc(struct qed_hwfn *p_hwfn);
void qed_nvmetcp_setup(struct qed_hwfn *p_hwfn);
void qed_nvmetcp_free(struct qed_hwfn *p_hwfn);

#else /* IS_ENABLED(CONFIG_QED_NVMETCP) */
static inline int qed_nvmetcp_alloc(struct qed_hwfn *p_hwfn)
{
	return -EINVAL;
}

static inline void qed_nvmetcp_setup(struct qed_hwfn *p_hwfn) {}
static inline void qed_nvmetcp_free(struct qed_hwfn *p_hwfn) {}

#endif /* IS_ENABLED(CONFIG_QED_NVMETCP) */

#endif
