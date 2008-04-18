/*
 * Copyright (c) 2006 Chelsio, Inc. All rights reserved.
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
#ifndef __IWCH_PROVIDER_H__
#define __IWCH_PROVIDER_H__

#include <linux/list.h>
#include <linux/spinlock.h>
#include <rdma/ib_verbs.h>
#include <asm/types.h>
#include "t3cdev.h"
#include "iwch.h"
#include "cxio_wr.h"
#include "cxio_hal.h"

struct iwch_pd {
	struct ib_pd ibpd;
	u32 pdid;
	struct iwch_dev *rhp;
};

static inline struct iwch_pd *to_iwch_pd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct iwch_pd, ibpd);
}

struct tpt_attributes {
	u32 stag;
	u32 state:1;
	u32 type:2;
	u32 rsvd:1;
	enum tpt_mem_perm perms;
	u32 remote_invaliate_disable:1;
	u32 zbva:1;
	u32 mw_bind_enable:1;
	u32 page_size:5;

	u32 pdid;
	u32 qpid;
	u32 pbl_addr;
	u32 len;
	u64 va_fbo;
	u32 pbl_size;
};

struct iwch_mr {
	struct ib_mr ibmr;
	struct ib_umem *umem;
	struct iwch_dev *rhp;
	u64 kva;
	struct tpt_attributes attr;
};

typedef struct iwch_mw iwch_mw_handle;

static inline struct iwch_mr *to_iwch_mr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct iwch_mr, ibmr);
}

struct iwch_mw {
	struct ib_mw ibmw;
	struct iwch_dev *rhp;
	u64 kva;
	struct tpt_attributes attr;
};

static inline struct iwch_mw *to_iwch_mw(struct ib_mw *ibmw)
{
	return container_of(ibmw, struct iwch_mw, ibmw);
}

struct iwch_cq {
	struct ib_cq ibcq;
	struct iwch_dev *rhp;
	struct t3_cq cq;
	spinlock_t lock;
	atomic_t refcnt;
	wait_queue_head_t wait;
	u32 __user *user_rptr_addr;
};

static inline struct iwch_cq *to_iwch_cq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct iwch_cq, ibcq);
}

enum IWCH_QP_FLAGS {
	QP_QUIESCED = 0x01
};

struct iwch_mpa_attributes {
	u8 recv_marker_enabled;
	u8 xmit_marker_enabled;	/* iWARP: enable inbound Read Resp. */
	u8 crc_enabled;
	u8 version;	/* 0 or 1 */
};

struct iwch_qp_attributes {
	u32 scq;
	u32 rcq;
	u32 sq_num_entries;
	u32 rq_num_entries;
	u32 sq_max_sges;
	u32 sq_max_sges_rdma_write;
	u32 rq_max_sges;
	u32 state;
	u8 enable_rdma_read;
	u8 enable_rdma_write;	/* enable inbound Read Resp. */
	u8 enable_bind;
	u8 enable_mmid0_fastreg;	/* Enable STAG0 + Fast-register */
	/*
	 * Next QP state. If specify the current state, only the
	 * QP attributes will be modified.
	 */
	u32 max_ord;
	u32 max_ird;
	u32 pd;	/* IN */
	u32 next_state;
	char terminate_buffer[52];
	u32 terminate_msg_len;
	u8 is_terminate_local;
	struct iwch_mpa_attributes mpa_attr;	/* IN-OUT */
	struct iwch_ep *llp_stream_handle;
	char *stream_msg_buf;	/* Last stream msg. before Idle -> RTS */
	u32 stream_msg_buf_len;	/* Only on Idle -> RTS */
};

struct iwch_qp {
	struct ib_qp ibqp;
	struct iwch_dev *rhp;
	struct iwch_ep *ep;
	struct iwch_qp_attributes attr;
	struct t3_wq wq;
	spinlock_t lock;
	atomic_t refcnt;
	wait_queue_head_t wait;
	enum IWCH_QP_FLAGS flags;
	struct timer_list timer;
};

static inline int qp_quiesced(struct iwch_qp *qhp)
{
	return qhp->flags & QP_QUIESCED;
}

static inline struct iwch_qp *to_iwch_qp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct iwch_qp, ibqp);
}

void iwch_qp_add_ref(struct ib_qp *qp);
void iwch_qp_rem_ref(struct ib_qp *qp);

struct iwch_ucontext {
	struct ib_ucontext ibucontext;
	struct cxio_ucontext uctx;
	u32 key;
	spinlock_t mmap_lock;
	struct list_head mmaps;
};

static inline struct iwch_ucontext *to_iwch_ucontext(struct ib_ucontext *c)
{
	return container_of(c, struct iwch_ucontext, ibucontext);
}

struct iwch_mm_entry {
	struct list_head entry;
	u64 addr;
	u32 key;
	unsigned len;
};

static inline struct iwch_mm_entry *remove_mmap(struct iwch_ucontext *ucontext,
						u32 key, unsigned len)
{
	struct list_head *pos, *nxt;
	struct iwch_mm_entry *mm;

	spin_lock(&ucontext->mmap_lock);
	list_for_each_safe(pos, nxt, &ucontext->mmaps) {

		mm = list_entry(pos, struct iwch_mm_entry, entry);
		if (mm->key == key && mm->len == len) {
			list_del_init(&mm->entry);
			spin_unlock(&ucontext->mmap_lock);
			PDBG("%s key 0x%x addr 0x%llx len %d\n", __func__,
			     key, (unsigned long long) mm->addr, mm->len);
			return mm;
		}
	}
	spin_unlock(&ucontext->mmap_lock);
	return NULL;
}

static inline void insert_mmap(struct iwch_ucontext *ucontext,
			       struct iwch_mm_entry *mm)
{
	spin_lock(&ucontext->mmap_lock);
	PDBG("%s key 0x%x addr 0x%llx len %d\n", __func__,
	     mm->key, (unsigned long long) mm->addr, mm->len);
	list_add_tail(&mm->entry, &ucontext->mmaps);
	spin_unlock(&ucontext->mmap_lock);
}

enum iwch_qp_attr_mask {
	IWCH_QP_ATTR_NEXT_STATE = 1 << 0,
	IWCH_QP_ATTR_ENABLE_RDMA_READ = 1 << 7,
	IWCH_QP_ATTR_ENABLE_RDMA_WRITE = 1 << 8,
	IWCH_QP_ATTR_ENABLE_RDMA_BIND = 1 << 9,
	IWCH_QP_ATTR_MAX_ORD = 1 << 11,
	IWCH_QP_ATTR_MAX_IRD = 1 << 12,
	IWCH_QP_ATTR_LLP_STREAM_HANDLE = 1 << 22,
	IWCH_QP_ATTR_STREAM_MSG_BUFFER = 1 << 23,
	IWCH_QP_ATTR_MPA_ATTR = 1 << 24,
	IWCH_QP_ATTR_QP_CONTEXT_ACTIVATE = 1 << 25,
	IWCH_QP_ATTR_VALID_MODIFY = (IWCH_QP_ATTR_ENABLE_RDMA_READ |
				     IWCH_QP_ATTR_ENABLE_RDMA_WRITE |
				     IWCH_QP_ATTR_MAX_ORD |
				     IWCH_QP_ATTR_MAX_IRD |
				     IWCH_QP_ATTR_LLP_STREAM_HANDLE |
				     IWCH_QP_ATTR_STREAM_MSG_BUFFER |
				     IWCH_QP_ATTR_MPA_ATTR |
				     IWCH_QP_ATTR_QP_CONTEXT_ACTIVATE)
};

int iwch_modify_qp(struct iwch_dev *rhp,
				struct iwch_qp *qhp,
				enum iwch_qp_attr_mask mask,
				struct iwch_qp_attributes *attrs,
				int internal);

enum iwch_qp_state {
	IWCH_QP_STATE_IDLE,
	IWCH_QP_STATE_RTS,
	IWCH_QP_STATE_ERROR,
	IWCH_QP_STATE_TERMINATE,
	IWCH_QP_STATE_CLOSING,
	IWCH_QP_STATE_TOT
};

static inline int iwch_convert_state(enum ib_qp_state ib_state)
{
	switch (ib_state) {
	case IB_QPS_RESET:
	case IB_QPS_INIT:
		return IWCH_QP_STATE_IDLE;
	case IB_QPS_RTS:
		return IWCH_QP_STATE_RTS;
	case IB_QPS_SQD:
		return IWCH_QP_STATE_CLOSING;
	case IB_QPS_SQE:
		return IWCH_QP_STATE_TERMINATE;
	case IB_QPS_ERR:
		return IWCH_QP_STATE_ERROR;
	default:
		return -1;
	}
}

static inline u32 iwch_ib_to_tpt_access(int acc)
{
	return (acc & IB_ACCESS_REMOTE_WRITE ? TPT_REMOTE_WRITE : 0) |
	       (acc & IB_ACCESS_REMOTE_READ ? TPT_REMOTE_READ : 0) |
	       (acc & IB_ACCESS_LOCAL_WRITE ? TPT_LOCAL_WRITE : 0) |
	       TPT_LOCAL_READ;
}

static inline u32 iwch_ib_to_mwbind_access(int acc)
{
	return (acc & IB_ACCESS_REMOTE_WRITE ? T3_MEM_ACCESS_REM_WRITE : 0) |
	       (acc & IB_ACCESS_REMOTE_READ ? T3_MEM_ACCESS_REM_READ : 0) |
	       (acc & IB_ACCESS_LOCAL_WRITE ? T3_MEM_ACCESS_LOCAL_WRITE : 0) |
	       T3_MEM_ACCESS_LOCAL_READ;
}

enum iwch_mmid_state {
	IWCH_STAG_STATE_VALID,
	IWCH_STAG_STATE_INVALID
};

enum iwch_qp_query_flags {
	IWCH_QP_QUERY_CONTEXT_NONE = 0x0,	/* No ctx; Only attrs */
	IWCH_QP_QUERY_CONTEXT_GET = 0x1,	/* Get ctx + attrs */
	IWCH_QP_QUERY_CONTEXT_SUSPEND = 0x2,	/* Not Supported */

	/*
	 * Quiesce QP context; Consumer
	 * will NOT replay outstanding WR
	 */
	IWCH_QP_QUERY_CONTEXT_QUIESCE = 0x4,
	IWCH_QP_QUERY_CONTEXT_REMOVE = 0x8,
	IWCH_QP_QUERY_TEST_USERWRITE = 0x32	/* Test special */
};

int iwch_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
		      struct ib_send_wr **bad_wr);
int iwch_post_receive(struct ib_qp *ibqp, struct ib_recv_wr *wr,
		      struct ib_recv_wr **bad_wr);
int iwch_bind_mw(struct ib_qp *qp,
			     struct ib_mw *mw,
			     struct ib_mw_bind *mw_bind);
int iwch_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc);
int iwch_post_terminate(struct iwch_qp *qhp, struct respQ_msg_t *rsp_msg);
int iwch_register_device(struct iwch_dev *dev);
void iwch_unregister_device(struct iwch_dev *dev);
int iwch_quiesce_qps(struct iwch_cq *chp);
int iwch_resume_qps(struct iwch_cq *chp);
void stop_read_rep_timer(struct iwch_qp *qhp);
int iwch_register_mem(struct iwch_dev *rhp, struct iwch_pd *php,
					struct iwch_mr *mhp,
					int shift,
					__be64 *page_list);
int iwch_reregister_mem(struct iwch_dev *rhp, struct iwch_pd *php,
					struct iwch_mr *mhp,
					int shift,
					__be64 *page_list,
					int npages);
int build_phys_page_list(struct ib_phys_buf *buffer_list,
					int num_phys_buf,
					u64 *iova_start,
					u64 *total_size,
					int *npages,
					int *shift,
					__be64 **page_list);


#define IWCH_NODE_DESC "cxgb3 Chelsio Communications"

#endif
