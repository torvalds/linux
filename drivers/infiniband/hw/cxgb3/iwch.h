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
#ifndef __IWCH_H__
#define __IWCH_H__

#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/workqueue.h>

#include <rdma/ib_verbs.h>

#include "cxio_hal.h"
#include "cxgb3_offload.h"

struct iwch_pd;
struct iwch_cq;
struct iwch_qp;
struct iwch_mr;

struct iwch_rnic_attributes {
	u32 max_qps;
	u32 max_wrs;				/* Max for any SQ/RQ */
	u32 max_sge_per_wr;
	u32 max_sge_per_rdma_write_wr;	/* for RDMA Write WR */
	u32 max_cqs;
	u32 max_cqes_per_cq;
	u32 max_mem_regs;
	u32 max_phys_buf_entries;		/* for phys buf list */
	u32 max_pds;

	/*
	 * The memory page sizes supported by this RNIC.
	 * Bit position i in bitmap indicates page of
	 * size (4k)^i.  Phys block list mode unsupported.
	 */
	u32 mem_pgsizes_bitmask;
	u64 max_mr_size;
	u8 can_resize_wq;

	/*
	 * The maximum number of RDMA Reads that can be outstanding
	 * per QP with this RNIC as the target.
	 */
	u32 max_rdma_reads_per_qp;

	/*
	 * The maximum number of resources used for RDMA Reads
	 * by this RNIC with this RNIC as the target.
	 */
	u32 max_rdma_read_resources;

	/*
	 * The max depth per QP for initiation of RDMA Read
	 * by this RNIC.
	 */
	u32 max_rdma_read_qp_depth;

	/*
	 * The maximum depth for initiation of RDMA Read
	 * operations by this RNIC on all QPs
	 */
	u32 max_rdma_read_depth;
	u8 rq_overflow_handled;
	u32 can_modify_ird;
	u32 can_modify_ord;
	u32 max_mem_windows;
	u32 stag0_value;
	u8 zbva_support;
	u8 local_invalidate_fence;
	u32 cq_overflow_detection;
};

struct iwch_dev {
	struct ib_device ibdev;
	struct cxio_rdev rdev;
	u32 device_cap_flags;
	struct iwch_rnic_attributes attr;
	struct idr cqidr;
	struct idr qpidr;
	struct idr mmidr;
	spinlock_t lock;
	struct list_head entry;
	struct delayed_work db_drop_task;
};

static inline struct iwch_dev *to_iwch_dev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct iwch_dev, ibdev);
}

static inline struct iwch_dev *rdev_to_iwch_dev(struct cxio_rdev *rdev)
{
	return container_of(rdev, struct iwch_dev, rdev);
}

static inline int t3b_device(const struct iwch_dev *rhp)
{
	return rhp->rdev.t3cdev_p->type == T3B;
}

static inline int t3a_device(const struct iwch_dev *rhp)
{
	return rhp->rdev.t3cdev_p->type == T3A;
}

static inline struct iwch_cq *get_chp(struct iwch_dev *rhp, u32 cqid)
{
	return idr_find(&rhp->cqidr, cqid);
}

static inline struct iwch_qp *get_qhp(struct iwch_dev *rhp, u32 qpid)
{
	return idr_find(&rhp->qpidr, qpid);
}

static inline struct iwch_mr *get_mhp(struct iwch_dev *rhp, u32 mmid)
{
	return idr_find(&rhp->mmidr, mmid);
}

static inline int insert_handle(struct iwch_dev *rhp, struct idr *idr,
				void *handle, u32 id)
{
	int ret;

	idr_preload(GFP_KERNEL);
	spin_lock_irq(&rhp->lock);

	ret = idr_alloc(idr, handle, id, id + 1, GFP_NOWAIT);

	spin_unlock_irq(&rhp->lock);
	idr_preload_end();

	return ret < 0 ? ret : 0;
}

static inline void remove_handle(struct iwch_dev *rhp, struct idr *idr, u32 id)
{
	spin_lock_irq(&rhp->lock);
	idr_remove(idr, id);
	spin_unlock_irq(&rhp->lock);
}

extern struct cxgb3_client t3c_client;
extern cxgb3_cpl_handler_func t3c_handlers[NUM_CPL_CMDS];
extern void iwch_ev_dispatch(struct cxio_rdev *rdev_p, struct sk_buff *skb);

#endif
