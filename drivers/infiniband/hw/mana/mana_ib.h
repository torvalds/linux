/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Microsoft Corporation. All rights reserved.
 */

#ifndef _MANA_IB_H_
#define _MANA_IB_H_

#include <rdma/ib_verbs.h>
#include <rdma/ib_mad.h>
#include <rdma/ib_umem.h>
#include <rdma/mana-abi.h>
#include <rdma/uverbs_ioctl.h>

#include <net/mana/mana.h>

#define PAGE_SZ_BM                                                             \
	(SZ_4K | SZ_8K | SZ_16K | SZ_32K | SZ_64K | SZ_128K | SZ_256K |        \
	 SZ_512K | SZ_1M | SZ_2M)

/* MANA doesn't have any limit for MR size */
#define MANA_IB_MAX_MR_SIZE	U64_MAX

/*
 * The hardware limit of number of MRs is greater than maximum number of MRs
 * that can possibly represent in 24 bits
 */
#define MANA_IB_MAX_MR		0xFFFFFFu

struct mana_ib_adapter_caps {
	u32 max_sq_id;
	u32 max_rq_id;
	u32 max_cq_id;
	u32 max_qp_count;
	u32 max_cq_count;
	u32 max_mr_count;
	u32 max_pd_count;
	u32 max_inbound_read_limit;
	u32 max_outbound_read_limit;
	u32 mw_count;
	u32 max_srq_count;
	u32 max_qp_wr;
	u32 max_send_sge_count;
	u32 max_recv_sge_count;
	u32 max_inline_data_size;
};

struct mana_ib_dev {
	struct ib_device ib_dev;
	struct gdma_dev *gdma_dev;
	struct mana_ib_adapter_caps adapter_caps;
};

struct mana_ib_wq {
	struct ib_wq ibwq;
	struct ib_umem *umem;
	int wqe;
	u32 wq_buf_size;
	u64 gdma_region;
	u64 id;
	mana_handle_t rx_object;
};

struct mana_ib_pd {
	struct ib_pd ibpd;
	u32 pdn;
	mana_handle_t pd_handle;

	/* Mutex for sharing access to vport_use_count */
	struct mutex vport_mutex;
	int vport_use_count;

	bool tx_shortform_allowed;
	u32 tx_vp_offset;
};

struct mana_ib_mr {
	struct ib_mr ibmr;
	struct ib_umem *umem;
	mana_handle_t mr_handle;
};

struct mana_ib_cq {
	struct ib_cq ibcq;
	struct ib_umem *umem;
	int cqe;
	u64 gdma_region;
	u64 id;
	u32 comp_vector;
};

struct mana_ib_qp {
	struct ib_qp ibqp;

	/* Work queue info */
	struct ib_umem *sq_umem;
	int sqe;
	u64 sq_gdma_region;
	u64 sq_id;
	mana_handle_t tx_object;

	/* The port on the IB device, starting with 1 */
	u32 port;
};

struct mana_ib_ucontext {
	struct ib_ucontext ibucontext;
	u32 doorbell;
};

struct mana_ib_rwq_ind_table {
	struct ib_rwq_ind_table ib_ind_table;
};

enum mana_ib_command_code {
	MANA_IB_GET_ADAPTER_CAP = 0x30001,
};

struct mana_ib_query_adapter_caps_req {
	struct gdma_req_hdr hdr;
}; /*HW Data */

struct mana_ib_query_adapter_caps_resp {
	struct gdma_resp_hdr hdr;
	u32 max_sq_id;
	u32 max_rq_id;
	u32 max_cq_id;
	u32 max_qp_count;
	u32 max_cq_count;
	u32 max_mr_count;
	u32 max_pd_count;
	u32 max_inbound_read_limit;
	u32 max_outbound_read_limit;
	u32 mw_count;
	u32 max_srq_count;
	u32 max_requester_sq_size;
	u32 max_responder_sq_size;
	u32 max_requester_rq_size;
	u32 max_responder_rq_size;
	u32 max_send_sge_count;
	u32 max_recv_sge_count;
	u32 max_inline_data_size;
}; /* HW Data */

int mana_ib_gd_create_dma_region(struct mana_ib_dev *dev, struct ib_umem *umem,
				 mana_handle_t *gdma_region);

int mana_ib_gd_destroy_dma_region(struct mana_ib_dev *dev,
				  mana_handle_t gdma_region);

struct ib_wq *mana_ib_create_wq(struct ib_pd *pd,
				struct ib_wq_init_attr *init_attr,
				struct ib_udata *udata);

int mana_ib_modify_wq(struct ib_wq *wq, struct ib_wq_attr *wq_attr,
		      u32 wq_attr_mask, struct ib_udata *udata);

int mana_ib_destroy_wq(struct ib_wq *ibwq, struct ib_udata *udata);

int mana_ib_create_rwq_ind_table(struct ib_rwq_ind_table *ib_rwq_ind_table,
				 struct ib_rwq_ind_table_init_attr *init_attr,
				 struct ib_udata *udata);

int mana_ib_destroy_rwq_ind_table(struct ib_rwq_ind_table *ib_rwq_ind_tbl);

struct ib_mr *mana_ib_get_dma_mr(struct ib_pd *ibpd, int access_flags);

struct ib_mr *mana_ib_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				  u64 iova, int access_flags,
				  struct ib_udata *udata);

int mana_ib_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata);

int mana_ib_create_qp(struct ib_qp *qp, struct ib_qp_init_attr *qp_init_attr,
		      struct ib_udata *udata);

int mana_ib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		      int attr_mask, struct ib_udata *udata);

int mana_ib_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata);

int mana_ib_cfg_vport(struct mana_ib_dev *dev, u32 port_id,
		      struct mana_ib_pd *pd, u32 doorbell_id);
void mana_ib_uncfg_vport(struct mana_ib_dev *dev, struct mana_ib_pd *pd,
			 u32 port);

int mana_ib_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		      struct ib_udata *udata);

int mana_ib_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata);

int mana_ib_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata);
int mana_ib_dealloc_pd(struct ib_pd *ibpd, struct ib_udata *udata);

int mana_ib_alloc_ucontext(struct ib_ucontext *ibcontext,
			   struct ib_udata *udata);
void mana_ib_dealloc_ucontext(struct ib_ucontext *ibcontext);

int mana_ib_mmap(struct ib_ucontext *ibcontext, struct vm_area_struct *vma);

int mana_ib_get_port_immutable(struct ib_device *ibdev, u32 port_num,
			       struct ib_port_immutable *immutable);
int mana_ib_query_device(struct ib_device *ibdev, struct ib_device_attr *props,
			 struct ib_udata *uhw);
int mana_ib_query_port(struct ib_device *ibdev, u32 port,
		       struct ib_port_attr *props);
int mana_ib_query_gid(struct ib_device *ibdev, u32 port, int index,
		      union ib_gid *gid);

void mana_ib_disassociate_ucontext(struct ib_ucontext *ibcontext);

int mana_ib_gd_query_adapter_caps(struct mana_ib_dev *mdev);

void mana_ib_cq_handler(void *ctx, struct gdma_queue *gdma_cq);
#endif
