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

struct erdma_ext_db_info {
	bool enable;
	u16 sdb_off;
	u16 rdb_off;
	u16 cdb_off;
};

struct erdma_ucontext {
	struct ib_ucontext ibucontext;

	struct erdma_ext_db_info ext_db;

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
#define MTT_SIZE(mtt_cnt) ((mtt_cnt) << 3) /* per mtt entry takes 8 Bytes. */
#define ERDMA_MR_MAX_MTT_CNT 524288
#define ERDMA_MTT_ENTRY_SIZE 8

#define ERDMA_MR_TYPE_NORMAL 0
#define ERDMA_MR_TYPE_FRMR 1
#define ERDMA_MR_TYPE_DMA 2

#define ERDMA_MR_MTT_0LEVEL 0
#define ERDMA_MR_MTT_1LEVEL 1

#define ERDMA_MR_ACC_RA BIT(0)
#define ERDMA_MR_ACC_LR BIT(1)
#define ERDMA_MR_ACC_LW BIT(2)
#define ERDMA_MR_ACC_RR BIT(3)
#define ERDMA_MR_ACC_RW BIT(4)

static inline u8 to_erdma_access_flags(int access)
{
	return (access & IB_ACCESS_REMOTE_READ ? ERDMA_MR_ACC_RR : 0) |
	       (access & IB_ACCESS_LOCAL_WRITE ? ERDMA_MR_ACC_LW : 0) |
	       (access & IB_ACCESS_REMOTE_WRITE ? ERDMA_MR_ACC_RW : 0) |
	       (access & IB_ACCESS_REMOTE_ATOMIC ? ERDMA_MR_ACC_RA : 0);
}

/* Hierarchical storage structure for MTT entries */
struct erdma_mtt {
	u64 *buf;
	size_t size;

	bool continuous;
	union {
		dma_addr_t buf_dma;
		struct {
			dma_addr_t *dma_addrs;
			u32 npages;
			u32 level;
		};
	};

	struct erdma_mtt *low_level;
};

struct erdma_mem {
	struct ib_umem *umem;
	struct erdma_mtt *mtt;

	u32 page_size;
	u32 page_offset;
	u32 page_cnt;
	u32 mtt_nents;

	u64 va;
	u64 len;
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

struct erdma_av {
	u8 port;
	u8 hop_limit;
	u8 traffic_class;
	u8 sl;
	u8 sgid_index;
	u16 udp_sport;
	u32 flow_label;
	u8 dmac[ETH_ALEN];
	u8 dgid[ERDMA_ROCEV2_GID_SIZE];
	enum erdma_network_type ntype;
};

struct erdma_ah {
	struct ib_ah ibah;
	struct erdma_av av;
	u32 ahn;
};

struct erdma_uqp {
	struct erdma_mem sq_mem;
	struct erdma_mem rq_mem;

	dma_addr_t sq_dbrec_dma;
	dma_addr_t rq_dbrec_dma;

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

	void *sq_dbrec;
	void *rq_dbrec;

	dma_addr_t sq_dbrec_dma;
	dma_addr_t rq_dbrec_dma;

	u8 sig_all;
};

enum erdma_qps_iwarp {
	ERDMA_QPS_IWARP_IDLE = 0,
	ERDMA_QPS_IWARP_RTR = 1,
	ERDMA_QPS_IWARP_RTS = 2,
	ERDMA_QPS_IWARP_CLOSING = 3,
	ERDMA_QPS_IWARP_TERMINATE = 4,
	ERDMA_QPS_IWARP_ERROR = 5,
	ERDMA_QPS_IWARP_UNDEF = 6,
	ERDMA_QPS_IWARP_COUNT = 7,
};

enum erdma_qpa_mask_iwarp {
	ERDMA_QPA_IWARP_STATE = (1 << 0),
	ERDMA_QPA_IWARP_LLP_HANDLE = (1 << 2),
	ERDMA_QPA_IWARP_ORD = (1 << 3),
	ERDMA_QPA_IWARP_IRD = (1 << 4),
	ERDMA_QPA_IWARP_SQ_SIZE = (1 << 5),
	ERDMA_QPA_IWARP_RQ_SIZE = (1 << 6),
	ERDMA_QPA_IWARP_MPA = (1 << 7),
	ERDMA_QPA_IWARP_CC = (1 << 8),
};

enum erdma_qps_rocev2 {
	ERDMA_QPS_ROCEV2_RESET = 0,
	ERDMA_QPS_ROCEV2_INIT = 1,
	ERDMA_QPS_ROCEV2_RTR = 2,
	ERDMA_QPS_ROCEV2_RTS = 3,
	ERDMA_QPS_ROCEV2_SQD = 4,
	ERDMA_QPS_ROCEV2_SQE = 5,
	ERDMA_QPS_ROCEV2_ERROR = 6,
	ERDMA_QPS_ROCEV2_COUNT = 7,
};

enum erdma_qpa_mask_rocev2 {
	ERDMA_QPA_ROCEV2_STATE = (1 << 0),
	ERDMA_QPA_ROCEV2_QKEY = (1 << 1),
	ERDMA_QPA_ROCEV2_AV = (1 << 2),
	ERDMA_QPA_ROCEV2_SQ_PSN = (1 << 3),
	ERDMA_QPA_ROCEV2_RQ_PSN = (1 << 4),
	ERDMA_QPA_ROCEV2_DST_QPN = (1 << 5),
};

enum erdma_qp_flags {
	ERDMA_QP_IN_FLUSHING = (1 << 0),
};

#define ERDMA_QP_ACTIVE 0
#define ERDMA_QP_PASSIVE 1

struct erdma_mod_qp_params_iwarp {
	enum erdma_qps_iwarp state;
	enum erdma_cc_alg cc;
	u8 qp_type;
	u8 pd_len;
	u32 irq_size;
	u32 orq_size;
};

struct erdma_qp_attrs_iwarp {
	enum erdma_qps_iwarp state;
	u32 cookie;
};

struct erdma_mod_qp_params_rocev2 {
	enum erdma_qps_rocev2 state;
	u32 qkey;
	u32 sq_psn;
	u32 rq_psn;
	u32 dst_qpn;
	struct erdma_av av;
};

union erdma_mod_qp_params {
	struct erdma_mod_qp_params_iwarp iwarp;
	struct erdma_mod_qp_params_rocev2 rocev2;
};

struct erdma_qp_attrs_rocev2 {
	enum erdma_qps_rocev2 state;
	u32 qkey;
	u32 dst_qpn;
	struct erdma_av av;
};

struct erdma_qp_attrs {
	enum erdma_cc_alg cc; /* Congestion control algorithm */
	u32 sq_size;
	u32 rq_size;
	u32 orq_size;
	u32 irq_size;
	u32 max_send_sge;
	u32 max_recv_sge;
	union {
		struct erdma_qp_attrs_iwarp iwarp;
		struct erdma_qp_attrs_rocev2 rocev2;
	};
};

struct erdma_qp {
	struct ib_qp ibqp;
	struct kref ref;
	struct completion safe_free;
	struct erdma_dev *dev;
	struct erdma_cep *cep;
	struct rw_semaphore state_lock;

	unsigned long flags;
	struct delayed_work reflush_dwork;

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
	u64 *dbrec;
	dma_addr_t dbrec_dma;
};

struct erdma_ucq_info {
	struct erdma_mem qbuf_mem;
	struct erdma_user_dbrecords_page *user_dbr_page;
	dma_addr_t dbrec_dma;
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
int erdma_modify_qp_state_iwarp(struct erdma_qp *qp,
				struct erdma_mod_qp_params_iwarp *params,
				int mask);
int erdma_modify_qp_state_rocev2(struct erdma_qp *qp,
				 struct erdma_mod_qp_params_rocev2 *params,
				 int attr_mask);
void erdma_qp_llp_close(struct erdma_qp *qp);
void erdma_qp_cm_drop(struct erdma_qp *qp);

static inline bool erdma_device_iwarp(struct erdma_dev *dev)
{
	return dev->proto == ERDMA_PROTO_IWARP;
}

static inline bool erdma_device_rocev2(struct erdma_dev *dev)
{
	return dev->proto == ERDMA_PROTO_ROCEV2;
}

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

static inline struct erdma_ah *to_eah(struct ib_ah *ibah)
{
	return container_of(ibah, struct erdma_ah, ibah);
}

static inline int erdma_check_gid_attr(const struct ib_gid_attr *attr)
{
	u8 ntype = rdma_gid_attr_network_type(attr);

	if (ntype != RDMA_NETWORK_IPV4 && ntype != RDMA_NETWORK_IPV6)
		return -EINVAL;

	return 0;
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
		    struct uverbs_attr_bundle *attrs);
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
void erdma_disassociate_ucontext(struct ib_ucontext *ibcontext);
int erdma_req_notify_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags);
struct ib_mr *erdma_reg_user_mr(struct ib_pd *ibpd, u64 start, u64 len,
				u64 virt, int access, struct ib_dmah *dmah,
				struct ib_udata *udata);
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
void erdma_remove_cqes_of_qp(struct ib_cq *ibcq, u32 qpn);
struct ib_mr *erdma_ib_alloc_mr(struct ib_pd *ibpd, enum ib_mr_type mr_type,
				u32 max_num_sg);
int erdma_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		    unsigned int *sg_offset);
void erdma_port_event(struct erdma_dev *dev, enum ib_event_type reason);
void erdma_set_mtu(struct erdma_dev *dev, u32 mtu);
struct rdma_hw_stats *erdma_alloc_hw_port_stats(struct ib_device *device,
						u32 port_num);
int erdma_get_hw_stats(struct ib_device *ibdev, struct rdma_hw_stats *stats,
		       u32 port, int index);
enum rdma_link_layer erdma_get_link_layer(struct ib_device *ibdev,
					  u32 port_num);
int erdma_add_gid(const struct ib_gid_attr *attr, void **context);
int erdma_del_gid(const struct ib_gid_attr *attr, void **context);
int erdma_query_pkey(struct ib_device *ibdev, u32 port, u16 index, u16 *pkey);
void erdma_set_av_cfg(struct erdma_av_cfg *av_cfg, struct erdma_av *av);
int erdma_create_ah(struct ib_ah *ibah, struct rdma_ah_init_attr *init_attr,
		    struct ib_udata *udata);
int erdma_destroy_ah(struct ib_ah *ibah, u32 flags);
int erdma_query_ah(struct ib_ah *ibah, struct rdma_ah_attr *ah_attr);

#endif
