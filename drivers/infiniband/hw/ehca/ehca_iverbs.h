/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  Function definitions for internal functions
 *
 *  Authors: Heiko J Schick <schickhj@de.ibm.com>
 *           Dietmar Decker <ddecker@de.ibm.com>
 *
 *  Copyright (c) 2005 IBM Corporation
 *
 *  All rights reserved.
 *
 *  This source code is distributed under a dual license of GPL v2.0 and OpenIB
 *  BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __EHCA_IVERBS_H__
#define __EHCA_IVERBS_H__

#include "ehca_classes.h"

int ehca_query_device(struct ib_device *ibdev, struct ib_device_attr *props);

int ehca_query_port(struct ib_device *ibdev, u8 port,
		    struct ib_port_attr *props);

int ehca_query_sma_attr(struct ehca_shca *shca, u8 port,
			struct ehca_sma_attr *attr);

int ehca_query_pkey(struct ib_device *ibdev, u8 port, u16 index, u16 * pkey);

int ehca_query_gid(struct ib_device *ibdev, u8 port, int index,
		   union ib_gid *gid);

int ehca_modify_port(struct ib_device *ibdev, u8 port, int port_modify_mask,
		     struct ib_port_modify *props);

struct ib_pd *ehca_alloc_pd(struct ib_device *device,
			    struct ib_ucontext *context,
			    struct ib_udata *udata);

int ehca_dealloc_pd(struct ib_pd *pd);

struct ib_ah *ehca_create_ah(struct ib_pd *pd, struct ib_ah_attr *ah_attr);

int ehca_modify_ah(struct ib_ah *ah, struct ib_ah_attr *ah_attr);

int ehca_query_ah(struct ib_ah *ah, struct ib_ah_attr *ah_attr);

int ehca_destroy_ah(struct ib_ah *ah);

struct ib_mr *ehca_get_dma_mr(struct ib_pd *pd, int mr_access_flags);

struct ib_mr *ehca_reg_phys_mr(struct ib_pd *pd,
			       struct ib_phys_buf *phys_buf_array,
			       int num_phys_buf,
			       int mr_access_flags, u64 *iova_start);

struct ib_mr *ehca_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
			       u64 virt, int mr_access_flags,
			       struct ib_udata *udata);

int ehca_rereg_phys_mr(struct ib_mr *mr,
		       int mr_rereg_mask,
		       struct ib_pd *pd,
		       struct ib_phys_buf *phys_buf_array,
		       int num_phys_buf, int mr_access_flags, u64 *iova_start);

int ehca_query_mr(struct ib_mr *mr, struct ib_mr_attr *mr_attr);

int ehca_dereg_mr(struct ib_mr *mr);

struct ib_mw *ehca_alloc_mw(struct ib_pd *pd);

int ehca_bind_mw(struct ib_qp *qp, struct ib_mw *mw,
		 struct ib_mw_bind *mw_bind);

int ehca_dealloc_mw(struct ib_mw *mw);

struct ib_fmr *ehca_alloc_fmr(struct ib_pd *pd,
			      int mr_access_flags,
			      struct ib_fmr_attr *fmr_attr);

int ehca_map_phys_fmr(struct ib_fmr *fmr,
		      u64 *page_list, int list_len, u64 iova);

int ehca_unmap_fmr(struct list_head *fmr_list);

int ehca_dealloc_fmr(struct ib_fmr *fmr);

enum ehca_eq_type {
	EHCA_EQ = 0, /* Event Queue              */
	EHCA_NEQ     /* Notification Event Queue */
};

int ehca_create_eq(struct ehca_shca *shca, struct ehca_eq *eq,
		   enum ehca_eq_type type, const u32 length);

int ehca_destroy_eq(struct ehca_shca *shca, struct ehca_eq *eq);

void *ehca_poll_eq(struct ehca_shca *shca, struct ehca_eq *eq);


struct ib_cq *ehca_create_cq(struct ib_device *device, int cqe, int comp_vector,
			     struct ib_ucontext *context,
			     struct ib_udata *udata);

int ehca_destroy_cq(struct ib_cq *cq);

int ehca_resize_cq(struct ib_cq *cq, int cqe, struct ib_udata *udata);

int ehca_poll_cq(struct ib_cq *cq, int num_entries, struct ib_wc *wc);

int ehca_peek_cq(struct ib_cq *cq, int wc_cnt);

int ehca_req_notify_cq(struct ib_cq *cq, enum ib_cq_notify_flags notify_flags);

struct ib_qp *ehca_create_qp(struct ib_pd *pd,
			     struct ib_qp_init_attr *init_attr,
			     struct ib_udata *udata);

int ehca_destroy_qp(struct ib_qp *qp);

int ehca_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr, int attr_mask,
		   struct ib_udata *udata);

int ehca_query_qp(struct ib_qp *qp, struct ib_qp_attr *qp_attr,
		  int qp_attr_mask, struct ib_qp_init_attr *qp_init_attr);

int ehca_post_send(struct ib_qp *qp, struct ib_send_wr *send_wr,
		   struct ib_send_wr **bad_send_wr);

int ehca_post_recv(struct ib_qp *qp, struct ib_recv_wr *recv_wr,
		   struct ib_recv_wr **bad_recv_wr);

int ehca_post_srq_recv(struct ib_srq *srq,
		       struct ib_recv_wr *recv_wr,
		       struct ib_recv_wr **bad_recv_wr);

struct ib_srq *ehca_create_srq(struct ib_pd *pd,
			       struct ib_srq_init_attr *init_attr,
			       struct ib_udata *udata);

int ehca_modify_srq(struct ib_srq *srq, struct ib_srq_attr *attr,
		    enum ib_srq_attr_mask attr_mask, struct ib_udata *udata);

int ehca_query_srq(struct ib_srq *srq, struct ib_srq_attr *srq_attr);

int ehca_destroy_srq(struct ib_srq *srq);

u64 ehca_define_sqp(struct ehca_shca *shca, struct ehca_qp *ibqp,
		    struct ib_qp_init_attr *qp_init_attr);

int ehca_attach_mcast(struct ib_qp *qp, union ib_gid *gid, u16 lid);

int ehca_detach_mcast(struct ib_qp *qp, union ib_gid *gid, u16 lid);

struct ib_ucontext *ehca_alloc_ucontext(struct ib_device *device,
					struct ib_udata *udata);

int ehca_dealloc_ucontext(struct ib_ucontext *context);

int ehca_mmap(struct ib_ucontext *context, struct vm_area_struct *vma);

void ehca_poll_eqs(unsigned long data);

int ehca_calc_ipd(struct ehca_shca *shca, int port,
		  enum ib_rate path_rate, u32 *ipd);

#ifdef CONFIG_PPC_64K_PAGES
void *ehca_alloc_fw_ctrlblock(gfp_t flags);
void ehca_free_fw_ctrlblock(void *ptr);
#else
#define ehca_alloc_fw_ctrlblock(flags) ((void *)get_zeroed_page(flags))
#define ehca_free_fw_ctrlblock(ptr) free_page((unsigned long)(ptr))
#endif

void ehca_recover_sqp(struct ib_qp *sqp);

#endif
