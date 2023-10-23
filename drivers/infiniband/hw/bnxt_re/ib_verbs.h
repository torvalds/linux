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

#define BNXT_RE_FENCE_BYTES	64
struct bnxt_re_fence_data {
	u32 size;
	u8 va[BNXT_RE_FENCE_BYTES];
	dma_addr_t dma_addr;
	struct bnxt_re_mr *mr;
	struct ib_mw *mw;
	struct bnxt_qplib_swqe bind_wqe;
	u32 bind_rkey;
};

struct bnxt_re_pd {
	struct ib_pd            ib_pd;
	struct bnxt_re_dev	*rdev;
	struct bnxt_qplib_pd	qplib_pd;
	struct bnxt_re_fence_data fence;
	struct rdma_user_mmap_entry *pd_db_mmap;
	struct rdma_user_mmap_entry *pd_wcdb_mmap;
};

struct bnxt_re_ah {
	struct ib_ah		ib_ah;
	struct bnxt_re_dev	*rdev;
	struct bnxt_qplib_ah	qplib_ah;
};

struct bnxt_re_srq {
	struct ib_srq		ib_srq;
	struct bnxt_re_dev	*rdev;
	u32			srq_limit;
	struct bnxt_qplib_srq	qplib_srq;
	struct ib_umem		*umem;
	spinlock_t		lock;		/* protect srq */
};

struct bnxt_re_qp {
	struct ib_qp		ib_qp;
	struct list_head	list;
	struct bnxt_re_dev	*rdev;
	spinlock_t		sq_lock;	/* protect sq */
	spinlock_t		rq_lock;	/* protect rq */
	struct bnxt_qplib_qp	qplib_qp;
	struct ib_umem		*sumem;
	struct ib_umem		*rumem;
	/* QP1 */
	u32			send_psn;
	struct ib_ud_header	qp1_hdr;
	struct bnxt_re_cq	*scq;
	struct bnxt_re_cq	*rcq;
};

struct bnxt_re_cq {
	struct ib_cq		ib_cq;
	struct bnxt_re_dev	*rdev;
	spinlock_t              cq_lock;	/* protect cq */
	u16			cq_count;
	u16			cq_period;
	struct bnxt_qplib_cq	qplib_cq;
	struct bnxt_qplib_cqe	*cql;
#define MAX_CQL_PER_POLL	1024
	u32			max_cql;
	struct ib_umem		*umem;
	struct ib_umem		*resize_umem;
	int			resize_cqe;
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

struct bnxt_re_mw {
	struct bnxt_re_dev	*rdev;
	struct ib_mw		ib_mw;
	struct bnxt_qplib_mrw	qplib_mw;
};

struct bnxt_re_ucontext {
	struct ib_ucontext      ib_uctx;
	struct bnxt_re_dev	*rdev;
	struct bnxt_qplib_dpi	dpi;
	struct bnxt_qplib_dpi   wcdpi;
	void			*shpg;
	spinlock_t		sh_lock;	/* protect shpg */
	struct rdma_user_mmap_entry *shpage_mmap;
	u64 cmask;
};

enum bnxt_re_mmap_flag {
	BNXT_RE_MMAP_SH_PAGE,
	BNXT_RE_MMAP_UC_DB,
	BNXT_RE_MMAP_WC_DB,
	BNXT_RE_MMAP_DBR_PAGE,
	BNXT_RE_MMAP_DBR_BAR,
};

struct bnxt_re_user_mmap_entry {
	struct rdma_user_mmap_entry rdma_entry;
	struct bnxt_re_ucontext *uctx;
	u64 mem_offset;
	u8 mmap_flag;
};

static inline u16 bnxt_re_get_swqe_size(int nsge)
{
	return sizeof(struct sq_send_hdr) + nsge * sizeof(struct sq_sge);
}

static inline u16 bnxt_re_get_rwqe_size(int nsge)
{
	return sizeof(struct rq_wqe_hdr) + (nsge * sizeof(struct sq_sge));
}

static inline u32 bnxt_re_init_depth(u32 ent, struct bnxt_re_ucontext *uctx)
{
	return uctx ? (uctx->cmask & BNXT_RE_UCNTX_CMASK_POW2_DISABLED) ?
		ent : roundup_pow_of_two(ent) : ent;
}

int bnxt_re_query_device(struct ib_device *ibdev,
			 struct ib_device_attr *ib_attr,
			 struct ib_udata *udata);
int bnxt_re_query_port(struct ib_device *ibdev, u32 port_num,
		       struct ib_port_attr *port_attr);
int bnxt_re_get_port_immutable(struct ib_device *ibdev, u32 port_num,
			       struct ib_port_immutable *immutable);
void bnxt_re_query_fw_str(struct ib_device *ibdev, char *str);
int bnxt_re_query_pkey(struct ib_device *ibdev, u32 port_num,
		       u16 index, u16 *pkey);
int bnxt_re_del_gid(const struct ib_gid_attr *attr, void **context);
int bnxt_re_add_gid(const struct ib_gid_attr *attr, void **context);
int bnxt_re_query_gid(struct ib_device *ibdev, u32 port_num,
		      int index, union ib_gid *gid);
enum rdma_link_layer bnxt_re_get_link_layer(struct ib_device *ibdev,
					    u32 port_num);
int bnxt_re_alloc_pd(struct ib_pd *pd, struct ib_udata *udata);
int bnxt_re_dealloc_pd(struct ib_pd *pd, struct ib_udata *udata);
int bnxt_re_create_ah(struct ib_ah *ah, struct rdma_ah_init_attr *init_attr,
		      struct ib_udata *udata);
int bnxt_re_query_ah(struct ib_ah *ah, struct rdma_ah_attr *ah_attr);
int bnxt_re_destroy_ah(struct ib_ah *ah, u32 flags);
int bnxt_re_create_srq(struct ib_srq *srq,
		       struct ib_srq_init_attr *srq_init_attr,
		       struct ib_udata *udata);
int bnxt_re_modify_srq(struct ib_srq *srq, struct ib_srq_attr *srq_attr,
		       enum ib_srq_attr_mask srq_attr_mask,
		       struct ib_udata *udata);
int bnxt_re_query_srq(struct ib_srq *srq, struct ib_srq_attr *srq_attr);
int bnxt_re_destroy_srq(struct ib_srq *srq, struct ib_udata *udata);
int bnxt_re_post_srq_recv(struct ib_srq *srq, const struct ib_recv_wr *recv_wr,
			  const struct ib_recv_wr **bad_recv_wr);
int bnxt_re_create_qp(struct ib_qp *qp, struct ib_qp_init_attr *qp_init_attr,
		      struct ib_udata *udata);
int bnxt_re_modify_qp(struct ib_qp *qp, struct ib_qp_attr *qp_attr,
		      int qp_attr_mask, struct ib_udata *udata);
int bnxt_re_query_qp(struct ib_qp *qp, struct ib_qp_attr *qp_attr,
		     int qp_attr_mask, struct ib_qp_init_attr *qp_init_attr);
int bnxt_re_destroy_qp(struct ib_qp *qp, struct ib_udata *udata);
int bnxt_re_post_send(struct ib_qp *qp, const struct ib_send_wr *send_wr,
		      const struct ib_send_wr **bad_send_wr);
int bnxt_re_post_recv(struct ib_qp *qp, const struct ib_recv_wr *recv_wr,
		      const struct ib_recv_wr **bad_recv_wr);
int bnxt_re_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		      struct ib_udata *udata);
int bnxt_re_resize_cq(struct ib_cq *ibcq, int cqe, struct ib_udata *udata);
int bnxt_re_destroy_cq(struct ib_cq *cq, struct ib_udata *udata);
int bnxt_re_poll_cq(struct ib_cq *cq, int num_entries, struct ib_wc *wc);
int bnxt_re_req_notify_cq(struct ib_cq *cq, enum ib_cq_notify_flags flags);
struct ib_mr *bnxt_re_get_dma_mr(struct ib_pd *pd, int mr_access_flags);

int bnxt_re_map_mr_sg(struct ib_mr *ib_mr, struct scatterlist *sg, int sg_nents,
		      unsigned int *sg_offset);
struct ib_mr *bnxt_re_alloc_mr(struct ib_pd *ib_pd, enum ib_mr_type mr_type,
			       u32 max_num_sg);
int bnxt_re_dereg_mr(struct ib_mr *mr, struct ib_udata *udata);
struct ib_mw *bnxt_re_alloc_mw(struct ib_pd *ib_pd, enum ib_mw_type type,
			       struct ib_udata *udata);
int bnxt_re_dealloc_mw(struct ib_mw *mw);
struct ib_mr *bnxt_re_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				  u64 virt_addr, int mr_access_flags,
				  struct ib_udata *udata);
struct ib_mr *bnxt_re_reg_user_mr_dmabuf(struct ib_pd *ib_pd, u64 start,
					 u64 length, u64 virt_addr,
					 int fd, int mr_access_flags,
					 struct ib_udata *udata);
int bnxt_re_alloc_ucontext(struct ib_ucontext *ctx, struct ib_udata *udata);
void bnxt_re_dealloc_ucontext(struct ib_ucontext *context);
int bnxt_re_mmap(struct ib_ucontext *context, struct vm_area_struct *vma);
void bnxt_re_mmap_free(struct rdma_user_mmap_entry *rdma_entry);


unsigned long bnxt_re_lock_cqs(struct bnxt_re_qp *qp);
void bnxt_re_unlock_cqs(struct bnxt_re_qp *qp, unsigned long flags);
#endif /* __BNXT_RE_IB_VERBS_H__ */
