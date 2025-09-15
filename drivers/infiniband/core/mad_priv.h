/*
 * Copyright (c) 2004, 2005, Voltaire, Inc. All rights reserved.
 * Copyright (c) 2005 Intel Corporation. All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __IB_MAD_PRIV_H__
#define __IB_MAD_PRIV_H__

#include <linux/completion.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <rdma/ib_mad.h>
#include <rdma/ib_smi.h>
#include <rdma/opa_smi.h>

#define IB_MAD_QPS_CORE		2 /* Always QP0 and QP1 as a minimum */

/* QP and CQ parameters */
#define IB_MAD_QP_SEND_SIZE	128
#define IB_MAD_QP_RECV_SIZE	512
#define IB_MAD_QP_MIN_SIZE	64
#define IB_MAD_QP_MAX_SIZE	8192
#define IB_MAD_SEND_REQ_MAX_SG	2
#define IB_MAD_RECV_REQ_MAX_SG	1

#define IB_MAD_SEND_Q_PSN	0

/* Registration table sizes */
#define MAX_MGMT_CLASS		80
#define MAX_MGMT_VERSION	0x83
#define MAX_MGMT_OUI		8
#define MAX_MGMT_VENDOR_RANGE2	(IB_MGMT_CLASS_VENDOR_RANGE2_END - \
				IB_MGMT_CLASS_VENDOR_RANGE2_START + 1)

struct ib_mad_list_head {
	struct list_head list;
	struct ib_cqe cqe;
	struct ib_mad_queue *mad_queue;
};

struct ib_mad_private_header {
	struct ib_mad_list_head mad_list;
	struct ib_mad_recv_wc recv_wc;
	struct ib_wc wc;
	u64 mapping;
} __packed;

struct ib_mad_private {
	struct ib_mad_private_header header;
	size_t mad_size;
	struct ib_grh grh;
	u8 mad[];
} __packed;

struct ib_rmpp_segment {
	struct list_head list;
	u32 num;
	u8 data[];
};

struct ib_mad_agent_private {
	struct ib_mad_agent agent;
	struct ib_mad_reg_req *reg_req;
	struct ib_mad_qp_info *qp_info;

	spinlock_t lock;
	struct list_head send_list;
	unsigned int sol_fc_send_count;
	struct list_head wait_list;
	unsigned int sol_fc_wait_count;
	struct delayed_work timed_work;
	unsigned long timeout;
	struct list_head local_list;
	struct work_struct local_work;
	struct list_head rmpp_list;
	unsigned int sol_fc_max;
	struct list_head backlog_list;

	refcount_t refcount;
	union {
		struct completion comp;
		struct rcu_head rcu;
	};
};

struct ib_mad_snoop_private {
	struct ib_mad_agent agent;
	struct ib_mad_qp_info *qp_info;
	int snoop_index;
	int mad_snoop_flags;
	struct completion comp;
};

enum ib_mad_state {
	/* MAD is in the making and is not yet in any list */
	IB_MAD_STATE_INIT,
	/* MAD is in backlog list */
	IB_MAD_STATE_QUEUED,
	/*
	 * MAD was sent to the QP and is waiting for completion
	 * notification in send list.
	 */
	IB_MAD_STATE_SEND_START,
	/*
	 * MAD send completed successfully, waiting for a response
	 * in wait list.
	 */
	IB_MAD_STATE_WAIT_RESP,
	/*
	 * Response came early, before send completion notification,
	 * in send list.
	 */
	IB_MAD_STATE_EARLY_RESP,
	/* MAD was canceled while in wait or send list */
	IB_MAD_STATE_CANCELED,
	/* MAD processing completed, MAD in no list */
	IB_MAD_STATE_DONE
};

struct ib_mad_send_wr_private {
	struct ib_mad_list_head mad_list;
	struct list_head agent_list;
	struct ib_mad_agent_private *mad_agent_priv;
	struct ib_mad_send_buf send_buf;
	u64 header_mapping;
	u64 payload_mapping;
	struct ib_ud_wr send_wr;
	struct ib_sge sg_list[IB_MAD_SEND_REQ_MAX_SG];
	__be64 tid;
	unsigned long timeout;
	int max_retries;
	int retries_left;
	int retry;

	/* RMPP control */
	struct list_head rmpp_list;
	struct ib_rmpp_segment *last_ack_seg;
	struct ib_rmpp_segment *cur_seg;
	int last_ack;
	int seg_num;
	int newwin;
	int pad;

	enum ib_mad_state state;

	/* Solicited MAD flow control */
	bool is_solicited_fc;
};

static inline void expect_mad_state(struct ib_mad_send_wr_private *mad_send_wr,
				    enum ib_mad_state expected_state)
{
	if (IS_ENABLED(CONFIG_LOCKDEP))
		WARN_ON(mad_send_wr->state != expected_state);
}

static inline void expect_mad_state2(struct ib_mad_send_wr_private *mad_send_wr,
				     enum ib_mad_state expected_state1,
				     enum ib_mad_state expected_state2)
{
	if (IS_ENABLED(CONFIG_LOCKDEP))
		WARN_ON(mad_send_wr->state != expected_state1 &&
			mad_send_wr->state != expected_state2);
}

static inline void expect_mad_state3(struct ib_mad_send_wr_private *mad_send_wr,
				     enum ib_mad_state expected_state1,
				     enum ib_mad_state expected_state2,
				     enum ib_mad_state expected_state3)
{
	if (IS_ENABLED(CONFIG_LOCKDEP))
		WARN_ON(mad_send_wr->state != expected_state1 &&
			mad_send_wr->state != expected_state2 &&
			mad_send_wr->state != expected_state3);
}

static inline void
not_expect_mad_state(struct ib_mad_send_wr_private *mad_send_wr,
		     enum ib_mad_state wrong_state)
{
	if (IS_ENABLED(CONFIG_LOCKDEP))
		WARN_ON(mad_send_wr->state == wrong_state);
}

struct ib_mad_local_private {
	struct list_head completion_list;
	struct ib_mad_private *mad_priv;
	struct ib_mad_agent_private *recv_mad_agent;
	struct ib_mad_send_wr_private *mad_send_wr;
	size_t return_wc_byte_len;
};

struct ib_mad_mgmt_method_table {
	struct ib_mad_agent_private *agent[IB_MGMT_MAX_METHODS];
};

struct ib_mad_mgmt_class_table {
	struct ib_mad_mgmt_method_table *method_table[MAX_MGMT_CLASS];
};

struct ib_mad_mgmt_vendor_class {
	u8	oui[MAX_MGMT_OUI][3];
	struct ib_mad_mgmt_method_table *method_table[MAX_MGMT_OUI];
};

struct ib_mad_mgmt_vendor_class_table {
	struct ib_mad_mgmt_vendor_class *vendor_class[MAX_MGMT_VENDOR_RANGE2];
};

struct ib_mad_mgmt_version_table {
	struct ib_mad_mgmt_class_table *class;
	struct ib_mad_mgmt_vendor_class_table *vendor;
};

struct ib_mad_queue {
	spinlock_t lock;
	struct list_head list;
	int count;
	int max_active;
	struct ib_mad_qp_info *qp_info;
};

struct ib_mad_qp_info {
	struct ib_mad_port_private *port_priv;
	struct ib_qp *qp;
	struct ib_mad_queue send_queue;
	struct ib_mad_queue recv_queue;
	struct list_head overflow_list;
	spinlock_t snoop_lock;
	struct ib_mad_snoop_private **snoop_table;
	int snoop_table_size;
	atomic_t snoop_count;
};

struct ib_mad_port_private {
	struct list_head port_list;
	struct ib_device *device;
	int port_num;
	struct ib_cq *cq;
	struct ib_pd *pd;

	spinlock_t reg_lock;
	struct ib_mad_mgmt_version_table version[MAX_MGMT_VERSION];
	struct workqueue_struct *wq;
	struct ib_mad_qp_info qp_info[IB_MAD_QPS_CORE];
};

int ib_send_mad(struct ib_mad_send_wr_private *mad_send_wr);

struct ib_mad_send_wr_private *
ib_find_send_mad(const struct ib_mad_agent_private *mad_agent_priv,
		 const struct ib_mad_recv_wc *mad_recv_wc);

void ib_mad_complete_send_wr(struct ib_mad_send_wr_private *mad_send_wr,
			     struct ib_mad_send_wc *mad_send_wc);

void ib_mark_mad_done(struct ib_mad_send_wr_private *mad_send_wr);

void ib_reset_mad_timeout(struct ib_mad_send_wr_private *mad_send_wr,
			  unsigned long timeout_ms);

void change_mad_state(struct ib_mad_send_wr_private *mad_send_wr,
		      enum ib_mad_state new_state);

#endif	/* __IB_MAD_PRIV_H__ */
