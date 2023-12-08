/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */

/* Authors: Cheng Xu <chengyou@linux.alibaba.com> */
/*          Kai Shen <kaishen@linux.alibaba.com> */
/* Copyright (c) 2020-2022, Alibaba Group. */

#ifndef __ERDMA_VERBS_H__
#define __ERDMA_VERBS_H__

#include "erdma.h"

/* RDMA Capability. */
#define ERDMA_MAX_PD (128 * 1024)
#define ERDMA_MAX_SEND_WR 8192
#define ERDMA_MAX_ORD 128
#define ERDMA_MAX_IRD 128
#define ERDMA_MAX_SGE_RD 1
#define ERDMA_MAX_CONTEXT (128 * 1024)
#define ERDMA_MAX_SEND_SGE 6
#define ERDMA_MAX_RECV_SGE 1
#define ERDMA_MAX_INLINE (sizeof(struct erdma_sge) * (ERDMA_MAX_SEND_SGE))
#define ERDMA_MAX_FRMR_PA 512

enum {
	ERDMA_MMAP_IO_NC = 0, /* no cache */
};

struct erdma_user_mmap_entry {
	struct rdma_user_mmap_entry rdma_entry;
	u64 address;
	u8 mmap_flag;
};

struct erdma_ucontext {
	struct ib_ucontext ibucontext;

	u32 sdb_type;
	u32 sdb_idx;
	u32 sdb_page_idx;
	u32 sdb_page_off;
	u64 sdb;
	u64 rdb;
	u64 cdb;

	struct rdma_user_mmap_entry *sq_db_mmap_entry;
	struct rdma_user_mmap_entry *rq_db_mmap_entry;
	struct rdma_user_mmap_entry *cq_db_mmap_entry;

	/* doorbell records */
	struct list_head dbrecords_page_list;
	struct mutex dbrecords_page_mutex;
};

struct erdma_pd {
	struct ib_pd ibpd;
	u32 pdn;
};

/*
 * MemoryRegion definition.
 */
#define ERDMA_MAX_INLINE_MTT_ENTRIES 4
#define MTT_SIZE(mtt_cnt) (mtt_cnt << 3) /* per mtt takes 8 Bytes. */
#define ERDMA_MR_MAX_MTT_CNT 524288
#define ERDMA_MTT_ENTRY_SIZE 8

#define ERDMA_MR_TYPE_NORMAL 0
#define ERDMA_MR_TYPE_FRMR 1
#define ERDMA_MR_TYPE_DMA 2

#define ERDMA_MR_INLINE_MTT 0
#define ERDMA_MR_INDIRECT_MTT 1

#define ERDMA_MR_ACC_LR BIT(0)
#define ERDMA_MR_ACC_LW BIT(1)
#define ERDMA_MR_ACC_RR BIT(2)
#define ERDMA_MR_ACC_RW BIT(3)

static inline u8 to_erdma_access_flags(int access)
{
	return (access & IB_ACCESS_REMOTE_READ ? ERDMA_MR_ACC_RR : 0) |
	       (access & IB_ACCESS_LOCAL_WRITE ? ERDMA_MR_ACC_LW : 0) |
	       (access & IB_ACCESS_REMOTE_WRITE ? ERDMA_MR_ACC_RW : 0);
}

struct erdma_mem {
	struct ib_umem *umem;
	void *mtt_buf;
	u32 mtt_type;
	u32 page_size;
	u32 page_offset;
	u32 page_cnt;
	u32 mtt_nents;

	u64 va;
	u64 len;

	u64 mtt_entry[ERDMA_MAX_INLINE_MTT_ENTRIES];
};

struct erdma_mr {
	struct ib_mr ibmr;
	struct erdma_mem mem;
	u8 type;
	u8 access;
	u8 valid;
};

struct erdma_user_dbrecords_page {
	struct list_head list;
	struct ib_umem *umem;
	u64 va;
	int refcnt;
};

struct erdma_uqp {
	struct erdma_mem sq_mtt;
	struct erdma_mem rq_mtt;

	dma_addr_t sq_db_info_dma_addr;
	dma_addr_t rq_db_info_dma_addr;

	struct erdma_user_dbrecords_page *user_dbr_page;

	u32 rq_offset;
};

struct erdma_kqp {
	u16 sq_pi;
	u16 sq_ci;

	u16 rq_pi;
	u16 rq_ci;

	u64 *swr_tbl;
	u64 *rwr_tbl;

	void __iomem *hw_sq_db;
	void __iomem *hw_rq_db;

	void *sq_buf;
	dma_addr_t sq_buf_dma_addr;

	void *rq_buf;
	dma_addr_t rq_buf_dma_addr;

	void *sq_db_info;
	void *rq_db_info;

	u8 sig_all;
};

enum erdma_qp_state {
	ERDMA_QP_STATE_IDLE = 0,
	ERDMA_QP_STATE_RTR = 1,
	ERDMA_QP_STATE_RTS = 2,
	ERDMA_QP_STATE_CLOSING = 3,
	ERDMA_QP_STATE_TERMINATE = 4,
	ERDMA_QP_STATE_ERROR = 5,
	ERDMA_QP_STATE_UNDEF = 7,
	ERDMA_QP_STATE_COUNT = 8
};

enum erdma_qp_attr_mask {
	ERDMA_QP_ATTR_STATE = (1 << 0),
	ERDMA_QP_ATTR_LLP_HANDLE = (1 << 2),
	ERDMA_QP_ATTR_ORD = (1 << 3),
	ERDMA_QP_ATTR_IRD = (1 << 4),
	ERDMA_QP_ATTR_SQ_SIZE = (1 << 5),
	ERDMA_QP_ATTR_RQ_SIZE = (1 << 6),
	ERDMA_QP_ATTR_MPA = (1 << 7)
};

struct erdma_qp_attrs {
	enum erdma_qp_state state;
	enum erdma_cc_alg cc; /* Congestion control algorithm */
	u32 sq_size;
	u32 rq_size;
	u32 orq_size;
	u32 irq_size;
	u32 max_send_sge;
	u32 max_recv_sge;
	u32 cookie;
#define ERDMA_QP_ACTIVE 0
#define ERDMA_QP_PASSIVE 1
	u8 qp_type;
	u8 pd_len;
};

struct erdma_qp {
	struct ib_qp ibqp;
	struct kref ref;
	struct completion safe_free;
	struct erdma_dev *dev;
	struct erdma_cep *cep;
	struct rw_semaphore state_lock;

	union {
		struct erdma_kqp kern_qp;
		struct erdma_uqp user_qp;
	};

	struct erdma_cq *scq;
	struct erdma_cq *rcq;

	struct erdma_qp_attrs attrs;
	spinlock_t lock;
};

struct erdma_kcq_info {
	void *qbuf;
	dma_addr_t qbuf_dma_addr;
	u32 ci;
	u32 cmdsn;
	u32 notify_cnt;

	spinlock_t lock;
	u8 __iomem *db;
	u64 *db_record;
};

struct erdma_ucq_info {
	struct erdma_mem qbuf_mtt;
	struct erdma_user_dbrecords_page *user_dbr_page;
	dma_addr_t db_info_dma_addr;
};

struct erdma_cq {
	struct ib_cq ibcq;
	u32 cqn;

	u32 depth;
	u32 assoc_eqn;

	union {
		struct erdma_kcq_info kern_cq;
		struct erdma_ucq_info user_cq;
	};
};

#define QP_ID(qp) ((qp)->ibqp.qp_num)

static inline struct erdma_qp *find_qp_by_qpn(struct erdma_dev *dev, int id)
{
	return (struct erdma_qp *)xa_load(&dev->qp_xa, id);
}

static inline struct erdma_cq *find_cq_by_cqn(struct erdma_dev *dev, int id)
{
	return (struct erdma_cq *)xa_load(&dev->cq_xa, id);
}

void erdma_qp_get(struct erdma_qp *qp);
void erdma_qp_put(struct erdma_qp *qp);
int erdma_modify_qp_internal(struct erdma_qp *qp, struct erdma_qp_attrs *attrs,
			     enum erdma_qp_attr_mask mask);
void erdma_qp_llp_close(struct erdma_qp *qp);
void erdma_qp_cm_drop(struct erdma_qp *qp);

static inline struct erdma_ucontext *to_ectx(struct ib_ucontext *ibctx)
{
	return container_of(ibctx, struct erdma_ucontext, ibucontext);
}

static inline struct erdma_pd *to_epd(struct ib_pd *pd)
{
	return container_of(pd, struct erdma_pd, ibpd);
}

static inline struct erdma_mr *to_emr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct erdma_mr, ibmr);
}

static inline struct erdma_qp *to_eqp(struct ib_qp *qp)
{
	return container_of(qp, struct erdma_qp, ibqp);
}

static inline struct erdma_cq *to_ecq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct erdma_cq, ibcq);
}

static inline struct erdma_user_mmap_entry *
to_emmap(struct rdma_user_mmap_entry *ibmmap)
{
	return container_of(ibmmap, struct erdma_user_mmap_entry, rdma_entry);
}

int erdma_alloc_ucontext(struct ib_ucontext *ibctx, struct ib_udata *data);
void erdma_dealloc_ucontext(struct ib_ucontext *ibctx);
int erdma_query_device(struct ib_device *dev, struct ib_device_attr *attr,
		       struct ib_udata *data);
int erdma_get_port_immutable(struct ib_device *dev, u32 port,
			     struct ib_port_immutable *ib_port_immutable);
int erdma_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		    struct ib_udata *data);
int erdma_query_port(struct ib_device *dev, u32 port,
		     struct ib_port_attr *attr);
int erdma_query_gid(struct ib_device *dev, u32 port, int idx,
		    union ib_gid *gid);
int erdma_alloc_pd(struct ib_pd *ibpd, struct ib_udata *data);
int erdma_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata);
int erdma_create_qp(struct ib_qp *ibqp, struct ib_qp_init_attr *attr,
		    struct ib_udata *data);
int erdma_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int mask,
		   struct ib_qp_init_attr *init_attr);
int erdma_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int mask,
		    struct ib_udata *data);
int erdma_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata);
int erdma_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata);
int erdma_req_notify_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags);
struct ib_mr *erdma_reg_user_mr(struct ib_pd *ibpd, u64 start, u64 len,
				u64 virt, int access, struct ib_udata *udata);
struct ib_mr *erdma_get_dma_mr(struct ib_pd *ibpd, int rights);
int erdma_dereg_mr(struct ib_mr *ibmr, struct ib_udata *data);
int erdma_mmap(struct ib_ucontext *ctx, struct vm_area_struct *vma);
void erdma_mmap_free(struct rdma_user_mmap_entry *rdma_entry);
void erdma_qp_get_ref(struct ib_qp *ibqp);
void erdma_qp_put_ref(struct ib_qp *ibqp);
struct ib_qp *erdma_get_ibqp(struct ib_device *dev, int id);
int erdma_post_send(struct ib_qp *ibqp, const struct ib_send_wr *send_wr,
		    const struct ib_send_wr **bad_send_wr);
int erdma_post_recv(struct ib_qp *ibqp, const struct ib_recv_wr *recv_wr,
		    const struct ib_recv_wr **bad_recv_wr);
int erdma_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc);
struct ib_mr *erdma_ib_alloc_mr(struct ib_pd *ibpd, enum ib_mr_type mr_type,
				u32 max_num_sg);
int erdma_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		    unsigned int *sg_offset);
void erdma_port_event(struct erdma_dev *dev, enum ib_event_type reason);
void erdma_set_mtu(struct erdma_dev *dev, u32 mtu);

#endif
