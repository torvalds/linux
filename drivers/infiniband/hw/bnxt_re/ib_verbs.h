/*
 * Broadcom NetXtreme-E RoCE driver.
 *
 * Copyright (c) 2016 - 2017, Broadcom. All rights reserved.  The term
 * Broadcom refers to Broadcom Limited and/or its subsidiaries.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Description: IB Verbs interpreter (header)
 */

#ifndef __BNXT_RE_IB_VERBS_H__
#define __BNXT_RE_IB_VERBS_H__

struct bnxt_re_gid_ctx {
	u32			idx;
	u32			refcnt;
};

struct bnxt_re_pd {
	struct bnxt_re_dev	*rdev;
	struct ib_pd		ib_pd;
	struct bnxt_qplib_pd	qplib_pd;
	struct bnxt_qplib_dpi	dpi;
};

struct bnxt_re_ah {
	struct bnxt_re_dev	*rdev;
	struct ib_ah		ib_ah;
	struct bnxt_qplib_ah	qplib_ah;
};

struct bnxt_re_qp {
	struct list_head	list;
	struct bnxt_re_dev	*rdev;
	struct ib_qp		ib_qp;
	spinlock_t		sq_lock;	/* protect sq */
	struct bnxt_qplib_qp	qplib_qp;
	struct ib_umem		*sumem;
	struct ib_umem		*rumem;
	/* QP1 */
	u32			send_psn;
	struct ib_ud_header	qp1_hdr;
};

struct bnxt_re_cq {
	struct bnxt_re_dev	*rdev;
	spinlock_t              cq_lock;	/* protect cq */
	u16			cq_count;
	u16			cq_period;
	struct ib_cq		ib_cq;
	struct bnxt_qplib_cq	qplib_cq;
	struct bnxt_qplib_cqe	*cql;
#define MAX_CQL_PER_POLL	1024
	u32			max_cql;
	struct ib_umem		*umem;
};

struct bnxt_re_mr {
	struct bnxt_re_dev	*rdev;
	struct ib_mr		ib_mr;
	struct ib_umem		*ib_umem;
	struct bnxt_qplib_mrw	qplib_mr;
	u32			npages;
	u64			*pages;
	struct bnxt_qplib_frpl	qplib_frpl;
};

struct bnxt_re_frpl {
	struct bnxt_re_dev		*rdev;
	struct bnxt_qplib_frpl		qplib_frpl;
	u64				*page_list;
};

struct bnxt_re_fmr {
	struct bnxt_re_dev	*rdev;
	struct ib_fmr		ib_fmr;
	struct bnxt_qplib_mrw	qplib_fmr;
};

struct bnxt_re_mw {
	struct bnxt_re_dev	*rdev;
	struct ib_mw		ib_mw;
	struct bnxt_qplib_mrw	qplib_mw;
};

struct bnxt_re_ucontext {
	struct bnxt_re_dev	*rdev;
	struct ib_ucontext	ib_uctx;
	struct bnxt_qplib_dpi	*dpi;
	void			*shpg;
	spinlock_t		sh_lock;	/* protect shpg */
};

struct net_device *bnxt_re_get_netdev(struct ib_device *ibdev, u8 port_num);

int bnxt_re_query_device(struct ib_device *ibdev,
			 struct ib_device_attr *ib_attr,
			 struct ib_udata *udata);
int bnxt_re_modify_device(struct ib_device *ibdev,
			  int device_modify_mask,
			  struct ib_device_modify *device_modify);
int bnxt_re_query_port(struct ib_device *ibdev, u8 port_num,
		       struct ib_port_attr *port_attr);
int bnxt_re_modify_port(struct ib_device *ibdev, u8 port_num,
			int port_modify_mask,
			struct ib_port_modify *port_modify);
int bnxt_re_get_port_immutable(struct ib_device *ibdev, u8 port_num,
			       struct ib_port_immutable *immutable);
int bnxt_re_query_pkey(struct ib_device *ibdev, u8 port_num,
		       u16 index, u16 *pkey);
int bnxt_re_del_gid(struct ib_device *ibdev, u8 port_num,
		    unsigned int index, void **context);
int bnxt_re_add_gid(struct ib_device *ibdev, u8 port_num,
		    unsigned int index, const union ib_gid *gid,
		    const struct ib_gid_attr *attr, void **context);
int bnxt_re_query_gid(struct ib_device *ibdev, u8 port_num,
		      int index, union ib_gid *gid);
enum rdma_link_layer bnxt_re_get_link_layer(struct ib_device *ibdev,
					    u8 port_num);
struct ib_pd *bnxt_re_alloc_pd(struct ib_device *ibdev,
			       struct ib_ucontext *context,
			       struct ib_udata *udata);
int bnxt_re_dealloc_pd(struct ib_pd *pd);
struct ib_ah *bnxt_re_create_ah(struct ib_pd *pd,
				struct ib_ah_attr *ah_attr,
				struct ib_udata *udata);
int bnxt_re_modify_ah(struct ib_ah *ah, struct ib_ah_attr *ah_attr);
int bnxt_re_query_ah(struct ib_ah *ah, struct ib_ah_attr *ah_attr);
int bnxt_re_destroy_ah(struct ib_ah *ah);
struct ib_qp *bnxt_re_create_qp(struct ib_pd *pd,
				struct ib_qp_init_attr *qp_init_attr,
				struct ib_udata *udata);
int bnxt_re_modify_qp(struct ib_qp *qp, struct ib_qp_attr *qp_attr,
		      int qp_attr_mask, struct ib_udata *udata);
int bnxt_re_query_qp(struct ib_qp *qp, struct ib_qp_attr *qp_attr,
		     int qp_attr_mask, struct ib_qp_init_attr *qp_init_attr);
int bnxt_re_destroy_qp(struct ib_qp *qp);
int bnxt_re_post_send(struct ib_qp *qp, struct ib_send_wr *send_wr,
		      struct ib_send_wr **bad_send_wr);
int bnxt_re_post_recv(struct ib_qp *qp, struct ib_recv_wr *recv_wr,
		      struct ib_recv_wr **bad_recv_wr);
struct ib_cq *bnxt_re_create_cq(struct ib_device *ibdev,
				const struct ib_cq_init_attr *attr,
				struct ib_ucontext *context,
				struct ib_udata *udata);
int bnxt_re_destroy_cq(struct ib_cq *cq);
int bnxt_re_poll_cq(struct ib_cq *cq, int num_entries, struct ib_wc *wc);
int bnxt_re_req_notify_cq(struct ib_cq *cq, enum ib_cq_notify_flags flags);
struct ib_mr *bnxt_re_get_dma_mr(struct ib_pd *pd, int mr_access_flags);

int bnxt_re_map_mr_sg(struct ib_mr *ib_mr, struct scatterlist *sg, int sg_nents,
		      unsigned int *sg_offset);
struct ib_mr *bnxt_re_alloc_mr(struct ib_pd *ib_pd, enum ib_mr_type mr_type,
			       u32 max_num_sg);
int bnxt_re_dereg_mr(struct ib_mr *mr);
struct ib_fmr *bnxt_re_alloc_fmr(struct ib_pd *pd, int mr_access_flags,
				 struct ib_fmr_attr *fmr_attr);
int bnxt_re_map_phys_fmr(struct ib_fmr *fmr, u64 *page_list, int list_len,
			 u64 iova);
int bnxt_re_unmap_fmr(struct list_head *fmr_list);
int bnxt_re_dealloc_fmr(struct ib_fmr *fmr);
struct ib_mr *bnxt_re_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				  u64 virt_addr, int mr_access_flags,
				  struct ib_udata *udata);
struct ib_ucontext *bnxt_re_alloc_ucontext(struct ib_device *ibdev,
					   struct ib_udata *udata);
int bnxt_re_dealloc_ucontext(struct ib_ucontext *context);
int bnxt_re_mmap(struct ib_ucontext *context, struct vm_area_struct *vma);
#endif /* __BNXT_RE_IB_VERBS_H__ */
