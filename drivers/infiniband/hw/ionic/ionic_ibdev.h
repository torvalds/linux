/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#ifndef _IONIC_IBDEV_H_
#define _IONIC_IBDEV_H_

#include <rdma/ib_umem.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_pack.h>
#include <rdma/uverbs_ioctl.h>

#include <rdma/ionic-abi.h>
#include <ionic_api.h>
#include <ionic_regs.h>

#include "ionic_fw.h"
#include "ionic_queue.h"
#include "ionic_res.h"

#include "ionic_lif_cfg.h"

/* Config knobs */
#define IONIC_EQ_DEPTH 511
#define IONIC_EQ_COUNT 32
#define IONIC_AQ_DEPTH 63
#define IONIC_AQ_COUNT 4
#define IONIC_EQ_ISR_BUDGET 10
#define IONIC_EQ_WORK_BUDGET 1000
#define IONIC_MAX_RD_ATOM 16
#define IONIC_PKEY_TBL_LEN 1
#define IONIC_GID_TBL_LEN 256

#define IONIC_MAX_QPID 0xffffff
#define IONIC_SPEC_HIGH 8
#define IONIC_MAX_PD 1024
#define IONIC_SPEC_HIGH 8
#define IONIC_SQCMB_ORDER 5
#define IONIC_RQCMB_ORDER 0

#define IONIC_META_LAST		((void *)1ul)
#define IONIC_META_POSTED	((void *)2ul)

#define IONIC_CQ_GRACE 100

#define IONIC_ROCE_UDP_SPORT	28272
#define IONIC_DMA_LKEY		0
#define IONIC_DMA_RKEY		IONIC_DMA_LKEY

#define IONIC_CMB_SUPPORTED \
	(IONIC_CMB_ENABLE | IONIC_CMB_REQUIRE | IONIC_CMB_EXPDB | \
	 IONIC_CMB_WC | IONIC_CMB_UC)

/* resource is not reserved on the device, indicated in tbl_order */
#define IONIC_RES_INVALID	-1

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

	struct ionic_v1_stat	*hw_stats;
	void			*hw_stats_buf;
	struct rdma_stat_desc	*hw_stats_hdrs;
	struct ionic_counter_stats *counter_stats;
	int			hw_stats_count;
};

struct ionic_eq {
	struct ionic_ibdev	*dev;

	u32			eqid;
	u32			intr;

	struct ionic_queue	q;

	int			armed;
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

	atomic_t		admin_state;
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

struct ionic_pd {
	struct ib_pd		ibpd;

	u32			pdid;
	u32			flags;
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

struct ionic_sq_meta {
	u64			wrid;
	u32			len;
	u16			seq;
	u8			ibop;
	u8			ibsts;
	u8			remote:1;
	u8			signal:1;
	u8			local_comp:1;
};

struct ionic_rq_meta {
	struct ionic_rq_meta	*next;
	u64			wrid;
};

struct ionic_qp {
	struct ib_qp		ibqp;
	enum ib_qp_state	state;

	u32			qpid;
	u32			ahid;
	u32			sq_cqid;
	u32			rq_cqid;
	u8			udma_idx;
	u8			has_ah:1;
	u8			has_sq:1;
	u8			has_rq:1;
	u8			sig_all:1;

	struct list_head	qp_list_counter;

	struct list_head	cq_poll_sq;
	struct list_head	cq_flush_sq;
	struct list_head	cq_flush_rq;
	struct list_head	ibkill_flush_ent;

	spinlock_t		sq_lock; /* for posting and polling */
	struct ionic_queue	sq;
	struct ionic_sq_meta	*sq_meta;
	u16			*sq_msn_idx;
	int			sq_spec;
	u16			sq_old_prod;
	u16			sq_msn_prod;
	u16			sq_msn_cons;
	u8			sq_cmb;
	bool			sq_flush;
	bool			sq_flush_rcvd;

	spinlock_t		rq_lock; /* for posting and polling */
	struct ionic_queue	rq;
	struct ionic_rq_meta	*rq_meta;
	struct ionic_rq_meta	*rq_meta_head;
	int			rq_spec;
	u16			rq_old_prod;
	u8			rq_cmb;
	bool			rq_flush;

	struct kref		qp_kref;
	struct completion	qp_rel_comp;

	/* infrequently accessed, keep at end */
	int			sgid_index;
	int			sq_cmb_order;
	u32			sq_cmb_pgid;
	phys_addr_t		sq_cmb_addr;
	struct rdma_user_mmap_entry *mmap_sq_cmb;

	struct ib_umem		*sq_umem;

	int			rq_cmb_order;
	u32			rq_cmb_pgid;
	phys_addr_t		rq_cmb_addr;
	struct rdma_user_mmap_entry *mmap_rq_cmb;

	struct ib_umem		*rq_umem;

	int			dcqcn_profile;

	struct ib_ud_header	*hdr;
};

struct ionic_ah {
	struct ib_ah		ibah;
	u32			ahid;
	int			sgid_index;
	struct ib_ud_header	hdr;
};

struct ionic_mr {
	union {
		struct ib_mr	ibmr;
		struct ib_mw	ibmw;
	};

	u32			mrid;
	int			flags;

	struct ib_umem		*umem;
	struct ionic_tbl_buf	buf;
	bool			created;
};

struct ionic_counter_stats {
	int queue_stats_count;
	struct ionic_v1_stat *hdr;
	struct rdma_stat_desc *stats_hdrs;
	struct xarray xa_counters;
};

struct ionic_counter {
	void *vals;
	struct list_head qp_list;
};

static inline struct ionic_ibdev *to_ionic_ibdev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct ionic_ibdev, ibdev);
}

static inline struct ionic_ctx *to_ionic_ctx(struct ib_ucontext *ibctx)
{
	return container_of(ibctx, struct ionic_ctx, ibctx);
}

static inline struct ionic_ctx *to_ionic_ctx_uobj(struct ib_uobject *uobj)
{
	if (!uobj)
		return NULL;

	if (!uobj->context)
		return NULL;

	return to_ionic_ctx(uobj->context);
}

static inline struct ionic_pd *to_ionic_pd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct ionic_pd, ibpd);
}

static inline struct ionic_mr *to_ionic_mr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct ionic_mr, ibmr);
}

static inline struct ionic_mr *to_ionic_mw(struct ib_mw *ibmw)
{
	return container_of(ibmw, struct ionic_mr, ibmw);
}

static inline struct ionic_vcq *to_ionic_vcq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct ionic_vcq, ibcq);
}

static inline struct ionic_cq *to_ionic_vcq_cq(struct ib_cq *ibcq,
					       uint8_t udma_idx)
{
	return &to_ionic_vcq(ibcq)->cq[udma_idx];
}

static inline struct ionic_qp *to_ionic_qp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct ionic_qp, ibqp);
}

static inline struct ionic_ah *to_ionic_ah(struct ib_ah *ibah)
{
	return container_of(ibah, struct ionic_ah, ibah);
}

static inline u32 ionic_ctx_dbid(struct ionic_ibdev *dev,
				 struct ionic_ctx *ctx)
{
	if (!ctx)
		return dev->lif_cfg.dbid;

	return ctx->dbid;
}

static inline u32 ionic_obj_dbid(struct ionic_ibdev *dev,
				 struct ib_uobject *uobj)
{
	return ionic_ctx_dbid(dev, to_ionic_ctx_uobj(uobj));
}

static inline bool ionic_ibop_is_local(enum ib_wr_opcode op)
{
	return op == IB_WR_LOCAL_INV || op == IB_WR_REG_MR;
}

static inline void ionic_qp_complete(struct kref *kref)
{
	struct ionic_qp *qp = container_of(kref, struct ionic_qp, qp_kref);

	complete(&qp->qp_rel_comp);
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
void ionic_flush_qp(struct ionic_ibdev *dev, struct ionic_qp *qp);
void ionic_notify_flush_cq(struct ionic_cq *cq);

int ionic_alloc_ucontext(struct ib_ucontext *ibctx, struct ib_udata *udata);
void ionic_dealloc_ucontext(struct ib_ucontext *ibctx);
int ionic_mmap(struct ib_ucontext *ibctx, struct vm_area_struct *vma);
void ionic_mmap_free(struct rdma_user_mmap_entry *rdma_entry);
int ionic_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata);
int ionic_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata);
int ionic_create_ah(struct ib_ah *ibah, struct rdma_ah_init_attr *init_attr,
		    struct ib_udata *udata);
int ionic_query_ah(struct ib_ah *ibah, struct rdma_ah_attr *ah_attr);
int ionic_destroy_ah(struct ib_ah *ibah, u32 flags);
struct ib_mr *ionic_get_dma_mr(struct ib_pd *ibpd, int access);
struct ib_mr *ionic_reg_user_mr(struct ib_pd *ibpd, u64 start, u64 length,
				u64 addr, int access, struct ib_dmah *dmah,
				struct ib_udata *udata);
struct ib_mr *ionic_reg_user_mr_dmabuf(struct ib_pd *ibpd, u64 offset,
				       u64 length, u64 addr, int fd, int access,
					   struct ib_dmah *dmah,
				       struct uverbs_attr_bundle *attrs);
int ionic_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata);
struct ib_mr *ionic_alloc_mr(struct ib_pd *ibpd, enum ib_mr_type type,
			     u32 max_sg);
int ionic_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		    unsigned int *sg_offset);
int ionic_alloc_mw(struct ib_mw *ibmw, struct ib_udata *udata);
int ionic_dealloc_mw(struct ib_mw *ibmw);
int ionic_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		    struct uverbs_attr_bundle *attrs);
int ionic_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata);
int ionic_create_qp(struct ib_qp *ibqp, struct ib_qp_init_attr *attr,
		    struct ib_udata *udata);
int ionic_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int mask,
		    struct ib_udata *udata);
int ionic_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int mask,
		   struct ib_qp_init_attr *init_attr);
int ionic_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata);

/* ionic_datapath.c */
int ionic_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr,
		    const struct ib_send_wr **bad);
int ionic_post_recv(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
		    const struct ib_recv_wr **bad);
int ionic_poll_cq(struct ib_cq *ibcq, int nwc, struct ib_wc *wc);
int ionic_req_notify_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags);

/* ionic_hw_stats.c */
void ionic_stats_init(struct ionic_ibdev *dev);
void ionic_stats_cleanup(struct ionic_ibdev *dev);

/* ionic_pgtbl.c */
__le64 ionic_pgtbl_dma(struct ionic_tbl_buf *buf, u64 va);
__be64 ionic_pgtbl_off(struct ionic_tbl_buf *buf, u64 va);
int ionic_pgtbl_page(struct ionic_tbl_buf *buf, u64 dma);
int ionic_pgtbl_init(struct ionic_ibdev *dev,
		     struct ionic_tbl_buf *buf,
		     struct ib_umem *umem,
		     dma_addr_t dma,
		     int limit,
		     u64 page_size);
void ionic_pgtbl_unbuf(struct ionic_ibdev *dev, struct ionic_tbl_buf *buf);
#endif /* _IONIC_IBDEV_H_ */
