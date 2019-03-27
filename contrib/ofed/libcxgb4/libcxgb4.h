/*
 * Copyright (c) 2006-2016 Chelsio, Inc. All rights reserved.
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
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
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
#ifndef IWCH_H
#define IWCH_H

#include <pthread.h>
#include <inttypes.h>
#include <stddef.h>
#include <string.h>
#include <syslog.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <infiniband/driver.h>
#include <infiniband/udma_barrier.h>
#include <sys/queue.h>
#include "t4.h"

extern unsigned long c4iw_page_size;
extern unsigned long c4iw_page_shift;
extern unsigned long c4iw_page_mask;

struct c4iw_mr;

struct c4iw_dev {
	struct verbs_device ibv_dev;
	unsigned chip_version;
	int max_mr;
	struct c4iw_mr **mmid2ptr;
	int max_qp;
	struct c4iw_qp **qpid2ptr;
	int max_cq;
	struct c4iw_cq **cqid2ptr;
	pthread_spinlock_t lock;
	TAILQ_ENTRY(c4iw_dev) list;
	int abi_version;
};

static inline int dev_is_t6(struct c4iw_dev *dev)
{
	return dev->chip_version == CHELSIO_T6;
}

static inline int dev_is_t5(struct c4iw_dev *dev)
{
	return dev->chip_version == CHELSIO_T5;
}

static inline int dev_is_t4(struct c4iw_dev *dev)
{
	return dev->chip_version == CHELSIO_T4;
}

struct c4iw_context {
	struct ibv_context ibv_ctx;
	struct t4_dev_status_page *status_page;
	int status_page_size;
};

struct c4iw_pd {
	struct ibv_pd ibv_pd;
};

struct c4iw_mr {
	struct ibv_mr ibv_mr;
	uint64_t va_fbo;
	uint32_t len;
};

static inline u32 c4iw_mmid(u32 stag)
{
	return (stag >> 8);
}

struct c4iw_cq {
	struct ibv_cq ibv_cq;
	struct c4iw_dev *rhp;
	struct t4_cq cq;
	pthread_spinlock_t lock;
#ifdef STALL_DETECTION
	struct timeval time;
	int dumped;
#endif
};

struct c4iw_qp {
	struct ibv_qp ibv_qp;
	struct c4iw_dev *rhp;
	struct t4_wq wq;
	pthread_spinlock_t lock;
	int sq_sig_all;
};

#define to_c4iw_xxx(xxx, type)						\
	((struct c4iw_##type *)						\
	 ((void *) ib##xxx - offsetof(struct c4iw_##type, ibv_##xxx)))

static inline struct c4iw_dev *to_c4iw_dev(struct ibv_device *ibdev)
{
	return to_c4iw_xxx(dev, dev);
}

static inline struct c4iw_context *to_c4iw_context(struct ibv_context *ibctx)
{
	return to_c4iw_xxx(ctx, context);
}

static inline struct c4iw_pd *to_c4iw_pd(struct ibv_pd *ibpd)
{
	return to_c4iw_xxx(pd, pd);
}

static inline struct c4iw_cq *to_c4iw_cq(struct ibv_cq *ibcq)
{
	return to_c4iw_xxx(cq, cq);
}

static inline struct c4iw_qp *to_c4iw_qp(struct ibv_qp *ibqp)
{
	return to_c4iw_xxx(qp, qp);
}

static inline struct c4iw_mr *to_c4iw_mr(struct ibv_mr *ibmr)
{
	return to_c4iw_xxx(mr, mr);
}

static inline struct c4iw_qp *get_qhp(struct c4iw_dev *rhp, u32 qid)
{
	return rhp->qpid2ptr[qid];
}

static inline struct c4iw_cq *get_chp(struct c4iw_dev *rhp, u32 qid)
{
	return rhp->cqid2ptr[qid];
}

static inline unsigned long_log2(unsigned long x)
{
	unsigned r = 0;
	for (x >>= 1; x > 0; x >>= 1)
		r++;
	return r;
}

int c4iw_query_device(struct ibv_context *context,
			     struct ibv_device_attr *attr);
int c4iw_query_port(struct ibv_context *context, uint8_t port,
			   struct ibv_port_attr *attr);

struct ibv_pd *c4iw_alloc_pd(struct ibv_context *context);
int c4iw_free_pd(struct ibv_pd *pd);

struct ibv_mr *c4iw_reg_mr(struct ibv_pd *pd, void *addr,
				  size_t length, int access);
int c4iw_dereg_mr(struct ibv_mr *mr);

struct ibv_cq *c4iw_create_cq(struct ibv_context *context, int cqe,
			      struct ibv_comp_channel *channel,
			      int comp_vector);
int c4iw_resize_cq(struct ibv_cq *cq, int cqe);
int c4iw_destroy_cq(struct ibv_cq *cq);
int c4iw_poll_cq(struct ibv_cq *cq, int ne, struct ibv_wc *wc);
int c4iw_arm_cq(struct ibv_cq *cq, int solicited);
void c4iw_cq_event(struct ibv_cq *cq);
void c4iw_init_cq_buf(struct c4iw_cq *cq, int nent);

struct ibv_srq *c4iw_create_srq(struct ibv_pd *pd,
				       struct ibv_srq_init_attr *attr);
int c4iw_modify_srq(struct ibv_srq *srq,
			   struct ibv_srq_attr *attr,
			   int mask);
int c4iw_destroy_srq(struct ibv_srq *srq);
int c4iw_post_srq_recv(struct ibv_srq *ibsrq,
			      struct ibv_recv_wr *wr,
			      struct ibv_recv_wr **bad_wr);

struct ibv_qp *c4iw_create_qp(struct ibv_pd *pd,
				     struct ibv_qp_init_attr *attr);
int c4iw_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
			  int attr_mask);
int c4iw_destroy_qp(struct ibv_qp *qp);
int c4iw_query_qp(struct ibv_qp *qp,
			 struct ibv_qp_attr *attr,
			 int attr_mask,
			 struct ibv_qp_init_attr *init_attr);
void c4iw_flush_qp(struct c4iw_qp *qhp);
void c4iw_flush_qps(struct c4iw_dev *dev);
int c4iw_post_send(struct ibv_qp *ibqp, struct ibv_send_wr *wr,
			  struct ibv_send_wr **bad_wr);
int c4iw_post_receive(struct ibv_qp *ibqp, struct ibv_recv_wr *wr,
			  struct ibv_recv_wr **bad_wr);
struct ibv_ah *c4iw_create_ah(struct ibv_pd *pd,
			     struct ibv_ah_attr *ah_attr);
int c4iw_destroy_ah(struct ibv_ah *ah);
int c4iw_attach_mcast(struct ibv_qp *qp, const union ibv_gid *gid,
			     uint16_t lid);
int c4iw_detach_mcast(struct ibv_qp *qp, const union ibv_gid *gid,
			     uint16_t lid);
void c4iw_async_event(struct ibv_async_event *event);
void c4iw_flush_hw_cq(struct c4iw_cq *chp);
int c4iw_flush_rq(struct t4_wq *wq, struct t4_cq *cq, int count);
void c4iw_flush_sq(struct c4iw_qp *qhp);
void c4iw_count_rcqes(struct t4_cq *cq, struct t4_wq *wq, int *count);

#define FW_MAJ 0
#define FW_MIN 0

static inline unsigned long align(unsigned long val, unsigned long align)
{
	return (val + align - 1) & ~(align - 1);
}

#ifdef STATS

#define INC_STAT(a) { c4iw_stats.a++; }

struct c4iw_stats {
	unsigned long send;
	unsigned long recv;
	unsigned long read;
	unsigned long write;
	unsigned long arm;
	unsigned long cqe;
	unsigned long mr;
	unsigned long qp;
	unsigned long cq;
};
extern struct c4iw_stats c4iw_stats;
#else
#define INC_STAT(a)
#endif

#ifdef STALL_DETECTION
void dump_state(void);
extern int stall_to;
#endif

#endif				/* IWCH_H */
