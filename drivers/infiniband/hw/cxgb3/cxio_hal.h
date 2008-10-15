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
#ifndef  __CXIO_HAL_H__
#define  __CXIO_HAL_H__

#include <linux/list.h>
#include <linux/mutex.h>

#include "t3_cpl.h"
#include "t3cdev.h"
#include "cxgb3_ctl_defs.h"
#include "cxio_wr.h"

#define T3_CTRL_QP_ID    FW_RI_SGEEC_START
#define T3_CTL_QP_TID	 FW_RI_TID_START
#define T3_CTRL_QP_SIZE_LOG2  8
#define T3_CTRL_CQ_ID    0

#define T3_MAX_NUM_RI (1<<15)
#define T3_MAX_NUM_QP (1<<15)
#define T3_MAX_NUM_CQ (1<<15)
#define T3_MAX_NUM_PD (1<<15)
#define T3_MAX_PBL_SIZE 256
#define T3_MAX_RQ_SIZE 1024
#define T3_MAX_QP_DEPTH (T3_MAX_RQ_SIZE-1)
#define T3_MAX_CQ_DEPTH 8192
#define T3_MAX_NUM_STAG (1<<15)
#define T3_MAX_MR_SIZE 0x100000000ULL
#define T3_PAGESIZE_MASK 0xffff000  /* 4KB-128MB */

#define T3_STAG_UNSET 0xffffffff

#define T3_MAX_DEV_NAME_LEN 32

struct cxio_hal_ctrl_qp {
	u32 wptr;
	u32 rptr;
	struct mutex lock;	/* for the wtpr, can sleep */
	wait_queue_head_t waitq;/* wait for RspQ/CQE msg */
	union t3_wr *workq;	/* the work request queue */
	dma_addr_t dma_addr;	/* pci bus address of the workq */
	DECLARE_PCI_UNMAP_ADDR(mapping)
	void __iomem *doorbell;
};

struct cxio_hal_resource {
	struct kfifo *tpt_fifo;
	spinlock_t tpt_fifo_lock;
	struct kfifo *qpid_fifo;
	spinlock_t qpid_fifo_lock;
	struct kfifo *cqid_fifo;
	spinlock_t cqid_fifo_lock;
	struct kfifo *pdid_fifo;
	spinlock_t pdid_fifo_lock;
};

struct cxio_qpid_list {
	struct list_head entry;
	u32 qpid;
};

struct cxio_ucontext {
	struct list_head qpids;
	struct mutex lock;
};

struct cxio_rdev {
	char dev_name[T3_MAX_DEV_NAME_LEN];
	struct t3cdev *t3cdev_p;
	struct rdma_info rnic_info;
	struct adap_ports port_info;
	struct cxio_hal_resource *rscp;
	struct cxio_hal_ctrl_qp ctrl_qp;
	void *ulp;
	unsigned long qpshift;
	u32 qpnr;
	u32 qpmask;
	struct cxio_ucontext uctx;
	struct gen_pool *pbl_pool;
	struct gen_pool *rqt_pool;
	struct list_head entry;
};

static inline int cxio_num_stags(struct cxio_rdev *rdev_p)
{
	return min((int)T3_MAX_NUM_STAG, (int)((rdev_p->rnic_info.tpt_top - rdev_p->rnic_info.tpt_base) >> 5));
}

typedef void (*cxio_hal_ev_callback_func_t) (struct cxio_rdev * rdev_p,
					     struct sk_buff * skb);

#define RSPQ_CQID(rsp) (be32_to_cpu(rsp->cq_ptrid) & 0xffff)
#define RSPQ_CQPTR(rsp) ((be32_to_cpu(rsp->cq_ptrid) >> 16) & 0xffff)
#define RSPQ_GENBIT(rsp) ((be32_to_cpu(rsp->flags) >> 16) & 1)
#define RSPQ_OVERFLOW(rsp) ((be32_to_cpu(rsp->flags) >> 17) & 1)
#define RSPQ_AN(rsp) ((be32_to_cpu(rsp->flags) >> 18) & 1)
#define RSPQ_SE(rsp) ((be32_to_cpu(rsp->flags) >> 19) & 1)
#define RSPQ_NOTIFY(rsp) ((be32_to_cpu(rsp->flags) >> 20) & 1)
#define RSPQ_CQBRANCH(rsp) ((be32_to_cpu(rsp->flags) >> 21) & 1)
#define RSPQ_CREDIT_THRESH(rsp) ((be32_to_cpu(rsp->flags) >> 22) & 1)

struct respQ_msg_t {
	__be32 flags;		/* flit 0 */
	__be32 cq_ptrid;
	__be64 rsvd;		/* flit 1 */
	struct t3_cqe cqe;	/* flits 2-3 */
};

enum t3_cq_opcode {
	CQ_ARM_AN = 0x2,
	CQ_ARM_SE = 0x6,
	CQ_FORCE_AN = 0x3,
	CQ_CREDIT_UPDATE = 0x7
};

int cxio_rdev_open(struct cxio_rdev *rdev);
void cxio_rdev_close(struct cxio_rdev *rdev);
int cxio_hal_cq_op(struct cxio_rdev *rdev, struct t3_cq *cq,
		   enum t3_cq_opcode op, u32 credit);
int cxio_create_cq(struct cxio_rdev *rdev, struct t3_cq *cq);
int cxio_destroy_cq(struct cxio_rdev *rdev, struct t3_cq *cq);
int cxio_resize_cq(struct cxio_rdev *rdev, struct t3_cq *cq);
void cxio_release_ucontext(struct cxio_rdev *rdev, struct cxio_ucontext *uctx);
void cxio_init_ucontext(struct cxio_rdev *rdev, struct cxio_ucontext *uctx);
int cxio_create_qp(struct cxio_rdev *rdev, u32 kernel_domain, struct t3_wq *wq,
		   struct cxio_ucontext *uctx);
int cxio_destroy_qp(struct cxio_rdev *rdev, struct t3_wq *wq,
		    struct cxio_ucontext *uctx);
int cxio_peek_cq(struct t3_wq *wr, struct t3_cq *cq, int opcode);
int cxio_write_pbl(struct cxio_rdev *rdev_p, __be64 *pbl,
		   u32 pbl_addr, u32 pbl_size);
int cxio_register_phys_mem(struct cxio_rdev *rdev, u32 * stag, u32 pdid,
			   enum tpt_mem_perm perm, u32 zbva, u64 to, u32 len,
			   u8 page_size, u32 pbl_size, u32 pbl_addr);
int cxio_reregister_phys_mem(struct cxio_rdev *rdev, u32 * stag, u32 pdid,
			   enum tpt_mem_perm perm, u32 zbva, u64 to, u32 len,
			   u8 page_size, u32 pbl_size, u32 pbl_addr);
int cxio_dereg_mem(struct cxio_rdev *rdev, u32 stag, u32 pbl_size,
		   u32 pbl_addr);
int cxio_allocate_window(struct cxio_rdev *rdev, u32 * stag, u32 pdid);
int cxio_allocate_stag(struct cxio_rdev *rdev, u32 *stag, u32 pdid, u32 pbl_size, u32 pbl_addr);
int cxio_deallocate_window(struct cxio_rdev *rdev, u32 stag);
int cxio_rdma_init(struct cxio_rdev *rdev, struct t3_rdma_init_attr *attr);
void cxio_register_ev_cb(cxio_hal_ev_callback_func_t ev_cb);
void cxio_unregister_ev_cb(cxio_hal_ev_callback_func_t ev_cb);
u32 cxio_hal_get_pdid(struct cxio_hal_resource *rscp);
void cxio_hal_put_pdid(struct cxio_hal_resource *rscp, u32 pdid);
int __init cxio_hal_init(void);
void __exit cxio_hal_exit(void);
int cxio_flush_rq(struct t3_wq *wq, struct t3_cq *cq, int count);
int cxio_flush_sq(struct t3_wq *wq, struct t3_cq *cq, int count);
void cxio_count_rcqes(struct t3_cq *cq, struct t3_wq *wq, int *count);
void cxio_count_scqes(struct t3_cq *cq, struct t3_wq *wq, int *count);
void cxio_flush_hw_cq(struct t3_cq *cq);
int cxio_poll_cq(struct t3_wq *wq, struct t3_cq *cq, struct t3_cqe *cqe,
		     u8 *cqe_flushed, u64 *cookie, u32 *credit);

#define MOD "iw_cxgb3: "
#define PDBG(fmt, args...) pr_debug(MOD fmt, ## args)

#ifdef DEBUG
void cxio_dump_tpt(struct cxio_rdev *rev, u32 stag);
void cxio_dump_pbl(struct cxio_rdev *rev, u32 pbl_addr, uint len, u8 shift);
void cxio_dump_wqe(union t3_wr *wqe);
void cxio_dump_wce(struct t3_cqe *wce);
void cxio_dump_rqt(struct cxio_rdev *rdev, u32 hwtid, int nents);
void cxio_dump_tcb(struct cxio_rdev *rdev, u32 hwtid);
#endif

#endif
