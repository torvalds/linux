/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */

/* Authors: Bernard Metzler <bmt@zurich.ibm.com> */
/* Copyright (c) 2008-2019, IBM Corporation */

#ifndef _SIW_VERBS_H
#define _SIW_VERBS_H

#include <linux/errno.h>

#include <rdma/iw_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>

#include "siw.h"
#include "siw_cm.h"

/*
 * siw_copy_sgl()
 *
 * Copy SGL from RDMA core representation to local
 * representation.
 */
static inline void siw_copy_sgl(struct ib_sge *sge, struct siw_sge *siw_sge,
				int num_sge)
{
	while (num_sge--) {
		siw_sge->laddr = sge->addr;
		siw_sge->length = sge->length;
		siw_sge->lkey = sge->lkey;

		siw_sge++;
		sge++;
	}
}

int siw_alloc_ucontext(struct ib_ucontext *base_ctx, struct ib_udata *udata);
void siw_dealloc_ucontext(struct ib_ucontext *base_ctx);
int siw_query_port(struct ib_device *base_dev, u32 port,
		   struct ib_port_attr *attr);
int siw_get_port_immutable(struct ib_device *base_dev, u32 port,
			   struct ib_port_immutable *port_immutable);
int siw_query_device(struct ib_device *base_dev, struct ib_device_attr *attr,
		     struct ib_udata *udata);
int siw_create_cq(struct ib_cq *base_cq, const struct ib_cq_init_attr *attr,
		  struct uverbs_attr_bundle *attrs);
int siw_query_port(struct ib_device *base_dev, u32 port,
		   struct ib_port_attr *attr);
int siw_query_gid(struct ib_device *base_dev, u32 port, int idx,
		  union ib_gid *gid);
int siw_alloc_pd(struct ib_pd *base_pd, struct ib_udata *udata);
int siw_dealloc_pd(struct ib_pd *base_pd, struct ib_udata *udata);
int siw_create_qp(struct ib_qp *qp, struct ib_qp_init_attr *attr,
		  struct ib_udata *udata);
int siw_query_qp(struct ib_qp *base_qp, struct ib_qp_attr *qp_attr,
		 int qp_attr_mask, struct ib_qp_init_attr *qp_init_attr);
int siw_verbs_modify_qp(struct ib_qp *base_qp, struct ib_qp_attr *attr,
			int attr_mask, struct ib_udata *udata);
int siw_destroy_qp(struct ib_qp *base_qp, struct ib_udata *udata);
int siw_post_send(struct ib_qp *base_qp, const struct ib_send_wr *wr,
		  const struct ib_send_wr **bad_wr);
int siw_post_receive(struct ib_qp *base_qp, const struct ib_recv_wr *wr,
		     const struct ib_recv_wr **bad_wr);
int siw_destroy_cq(struct ib_cq *base_cq, struct ib_udata *udata);
int siw_poll_cq(struct ib_cq *base_cq, int num_entries, struct ib_wc *wc);
int siw_req_notify_cq(struct ib_cq *base_cq, enum ib_cq_notify_flags flags);
struct ib_mr *siw_reg_user_mr(struct ib_pd *base_pd, u64 start, u64 len,
			      u64 rnic_va, int rights, struct ib_udata *udata);
struct ib_mr *siw_alloc_mr(struct ib_pd *base_pd, enum ib_mr_type mr_type,
			   u32 max_sge);
struct ib_mr *siw_get_dma_mr(struct ib_pd *base_pd, int rights);
int siw_map_mr_sg(struct ib_mr *base_mr, struct scatterlist *sl, int num_sle,
		  unsigned int *sg_off);
int siw_dereg_mr(struct ib_mr *base_mr, struct ib_udata *udata);
int siw_create_srq(struct ib_srq *base_srq, struct ib_srq_init_attr *attr,
		   struct ib_udata *udata);
int siw_modify_srq(struct ib_srq *base_srq, struct ib_srq_attr *attr,
		   enum ib_srq_attr_mask mask, struct ib_udata *udata);
int siw_query_srq(struct ib_srq *base_srq, struct ib_srq_attr *attr);
int siw_destroy_srq(struct ib_srq *base_srq, struct ib_udata *udata);
int siw_post_srq_recv(struct ib_srq *base_srq, const struct ib_recv_wr *wr,
		      const struct ib_recv_wr **bad_wr);
int siw_mmap(struct ib_ucontext *ctx, struct vm_area_struct *vma);
void siw_mmap_free(struct rdma_user_mmap_entry *rdma_entry);
void siw_qp_event(struct siw_qp *qp, enum ib_event_type type);
void siw_cq_event(struct siw_cq *cq, enum ib_event_type type);
void siw_srq_event(struct siw_srq *srq, enum ib_event_type type);
void siw_port_event(struct siw_device *dev, u32 port, enum ib_event_type type);

#endif
