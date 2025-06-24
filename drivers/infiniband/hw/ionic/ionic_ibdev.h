/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#ifndef _IONIC_IBDEV_H_
#define _IONIC_IBDEV_H_

#include <rdma/ib_umem.h>
#include <rdma/ib_verbs.h>

#include <ionic_api.h>
#include <ionic_regs.h>

#include "ionic_fw.h"
#include "ionic_queue.h"
#include "ionic_res.h"

#include "ionic_lif_cfg.h"

#define DRIVER_NAME		"ionic_rdma"
#define DRIVER_SHORTNAME	"ionr"

#define IONIC_MIN_RDMA_VERSION	0
#define IONIC_MAX_RDMA_VERSION	2

/* Config knobs */
#define IONIC_EQ_DEPTH 511
#define IONIC_EQ_COUNT 32
#define IONIC_AQ_DEPTH 63
#define IONIC_AQ_COUNT 4
#define IONIC_EQ_ISR_BUDGET 10
#define IONIC_EQ_WORK_BUDGET 1000
#define IONIC_MAX_PD 1024

#define IONIC_CQ_GRACE 100

struct ionic_aq;
struct ionic_cq;
struct ionic_eq;
struct ionic_vcq;

enum ionic_admin_state {
	IONIC_ADMIN_ACTIVE, /* submitting admin commands to queue */
	IONIC_ADMIN_PAUSED, /* not submitting, but may complete normally */
	IONIC_ADMIN_KILLED, /* not submitting, locally completed */
};

enum ionic_admin_flags {
	IONIC_ADMIN_F_BUSYWAIT  = BIT(0),	/* Don't sleep */
	IONIC_ADMIN_F_TEARDOWN  = BIT(1),	/* In destroy path */
	IONIC_ADMIN_F_INTERRUPT = BIT(2),	/* Interruptible w/timeout */
};

struct ionic_qdesc {
	__aligned_u64 addr;
	__u32 size;
	__u16 mask;
	__u8 depth_log2;
	__u8 stride_log2;
};

enum ionic_mmap_flag {
	IONIC_MMAP_WC = BIT(0),
};

struct ionic_mmap_entry {
	struct rdma_user_mmap_entry rdma_entry;
	unsigned long size;
	unsigned long pfn;
	u8 mmap_flags;
};

struct ionic_ibdev {
	struct ib_device	ibdev;

	struct ionic_lif_cfg	lif_cfg;

	struct xarray		qp_tbl;
	struct xarray		cq_tbl;

	struct ionic_resid_bits	inuse_dbid;
	struct ionic_resid_bits	inuse_pdid;
	struct ionic_resid_bits	inuse_ahid;
	struct ionic_resid_bits	inuse_mrid;
	struct ionic_resid_bits	inuse_qpid;
	struct ionic_resid_bits	inuse_cqid;

	u8			half_cqid_udma_shift;
	u8			half_qpid_udma_shift;
	u8			next_qpid_udma_idx;
	u8			next_mrkey;

	struct work_struct	reset_work;
	bool			reset_posted;
	u32			reset_cnt;

	struct delayed_work	admin_dwork;
	struct ionic_aq		**aq_vec;
	atomic_t		admin_state;

	struct ionic_eq		**eq_vec;
};

struct ionic_eq {
	struct ionic_ibdev	*dev;

	u32			eqid;
	u32			intr;

	struct ionic_queue	q;

	bool			armed;
	bool			enable;

	struct work_struct	work;

	int			irq;
	char			name[32];
};

struct ionic_admin_wr {
	struct completion		work;
	struct list_head		aq_ent;
	struct ionic_v1_admin_wqe	wqe;
	struct ionic_v1_cqe		cqe;
	struct ionic_aq			*aq;
	int				status;
};

struct ionic_admin_wr_q {
	struct ionic_admin_wr	*wr;
	int			wqe_strides;
};

struct ionic_aq {
	struct ionic_ibdev	*dev;
	struct ionic_vcq	*vcq;

	struct work_struct	work;

	enum ionic_admin_state	admin_state;
	unsigned long		stamp;
	bool			armed;

	u32			aqid;
	u32			cqid;

	spinlock_t		lock; /* for posting */
	struct ionic_queue	q;
	struct ionic_admin_wr_q	*q_wr;
	struct list_head	wr_prod;
	struct list_head	wr_post;
};

struct ionic_ctx {
	struct ib_ucontext	ibctx;
	u32			dbid;
	struct rdma_user_mmap_entry	*mmap_dbell;
};

struct ionic_tbl_buf {
	u32		tbl_limit;
	u32		tbl_pages;
	size_t		tbl_size;
	__le64		*tbl_buf;
	dma_addr_t	tbl_dma;
	u8		page_size_log2;
};

struct ionic_cq {
	struct ionic_vcq	*vcq;

	u32			cqid;
	u32			eqid;

	spinlock_t		lock; /* for polling */
	struct list_head	poll_sq;
	bool			flush;
	struct list_head	flush_sq;
	struct list_head	flush_rq;
	struct list_head	ibkill_flush_ent;

	struct ionic_queue	q;
	bool			color;
	int			credit;
	u16			arm_any_prod;
	u16			arm_sol_prod;

	struct kref		cq_kref;
	struct completion	cq_rel_comp;

	/* infrequently accessed, keep at end */
	struct ib_umem		*umem;
};

struct ionic_vcq {
	struct ib_cq		ibcq;
	struct ionic_cq		cq[2];
	u8			udma_mask;
	u8			poll_idx;
};

static inline struct ionic_ibdev *to_ionic_ibdev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct ionic_ibdev, ibdev);
}

static inline void ionic_cq_complete(struct kref *kref)
{
	struct ionic_cq *cq = container_of(kref, struct ionic_cq, cq_kref);

	complete(&cq->cq_rel_comp);
}

/* ionic_admin.c */
extern struct workqueue_struct *ionic_evt_workq;
void ionic_admin_post(struct ionic_ibdev *dev, struct ionic_admin_wr *wr);
int ionic_admin_wait(struct ionic_ibdev *dev, struct ionic_admin_wr *wr,
		     enum ionic_admin_flags);

int ionic_rdma_reset_devcmd(struct ionic_ibdev *dev);

int ionic_create_rdma_admin(struct ionic_ibdev *dev);
void ionic_destroy_rdma_admin(struct ionic_ibdev *dev);
void ionic_kill_rdma_admin(struct ionic_ibdev *dev, bool fatal_path);

/* ionic_controlpath.c */
int ionic_create_cq_common(struct ionic_vcq *vcq,
			   struct ionic_tbl_buf *buf,
			   const struct ib_cq_init_attr *attr,
			   struct ionic_ctx *ctx,
			   struct ib_udata *udata,
			   struct ionic_qdesc *req_cq,
			   __u32 *resp_cqid,
			   int udma_idx);
void ionic_destroy_cq_common(struct ionic_ibdev *dev, struct ionic_cq *cq);

/* ionic_pgtbl.c */
int ionic_pgtbl_page(struct ionic_tbl_buf *buf, u64 dma);
int ionic_pgtbl_init(struct ionic_ibdev *dev,
		     struct ionic_tbl_buf *buf,
		     struct ib_umem *umem,
		     dma_addr_t dma,
		     int limit,
		     u64 page_size);
void ionic_pgtbl_unbuf(struct ionic_ibdev *dev, struct ionic_tbl_buf *buf);
#endif /* _IONIC_IBDEV_H_ */
