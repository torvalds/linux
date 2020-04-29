/* This file is part of the Emulex RoCE Device Driver for
 * RoCE (RDMA over Converged Ethernet) adapters.
 * Copyright (C) 2012-2015 Emulex. All rights reserved.
 * EMULEX and SLI are trademarks of Emulex.
 * www.emulex.com
 *
 * This software is available to you under a choice of one of two licenses.
 * You may choose to be licensed under the terms of the GNU General Public
 * License (GPL) Version 2, available from the file COPYING in the main
 * directory of this source tree, or the BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

#ifndef __OCRDMA_VERBS_H__
#define __OCRDMA_VERBS_H__

int ocrdma_post_send(struct ib_qp *, const struct ib_send_wr *,
		     const struct ib_send_wr **bad_wr);
int ocrdma_post_recv(struct ib_qp *, const struct ib_recv_wr *,
		     const struct ib_recv_wr **bad_wr);

int ocrdma_poll_cq(struct ib_cq *, int num_entries, struct ib_wc *wc);
int ocrdma_arm_cq(struct ib_cq *, enum ib_cq_notify_flags flags);

int ocrdma_query_device(struct ib_device *, struct ib_device_attr *props,
			struct ib_udata *uhw);
int ocrdma_query_port(struct ib_device *, u8 port, struct ib_port_attr *props);

enum rdma_protocol_type
ocrdma_query_protocol(struct ib_device *device, u8 port_num);

void ocrdma_get_guid(struct ocrdma_dev *, u8 *guid);
int ocrdma_query_pkey(struct ib_device *, u8 port, u16 index, u16 *pkey);

int ocrdma_alloc_ucontext(struct ib_ucontext *uctx, struct ib_udata *udata);
void ocrdma_dealloc_ucontext(struct ib_ucontext *uctx);

int ocrdma_mmap(struct ib_ucontext *, struct vm_area_struct *vma);

int ocrdma_alloc_pd(struct ib_pd *pd, struct ib_udata *udata);
void ocrdma_dealloc_pd(struct ib_pd *pd, struct ib_udata *udata);

int ocrdma_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		     struct ib_udata *udata);
int ocrdma_resize_cq(struct ib_cq *, int cqe, struct ib_udata *);
void ocrdma_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata);

struct ib_qp *ocrdma_create_qp(struct ib_pd *,
			       struct ib_qp_init_attr *attrs,
			       struct ib_udata *);
int _ocrdma_modify_qp(struct ib_qp *, struct ib_qp_attr *attr,
		      int attr_mask);
int ocrdma_modify_qp(struct ib_qp *, struct ib_qp_attr *attr,
		     int attr_mask, struct ib_udata *udata);
int ocrdma_query_qp(struct ib_qp *,
		    struct ib_qp_attr *qp_attr,
		    int qp_attr_mask, struct ib_qp_init_attr *);
int ocrdma_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata);
void ocrdma_del_flush_qp(struct ocrdma_qp *qp);

int ocrdma_create_srq(struct ib_srq *srq, struct ib_srq_init_attr *attr,
		      struct ib_udata *udata);
int ocrdma_modify_srq(struct ib_srq *, struct ib_srq_attr *,
		      enum ib_srq_attr_mask, struct ib_udata *);
int ocrdma_query_srq(struct ib_srq *, struct ib_srq_attr *);
void ocrdma_destroy_srq(struct ib_srq *ibsrq, struct ib_udata *udata);
int ocrdma_post_srq_recv(struct ib_srq *, const struct ib_recv_wr *,
			 const struct ib_recv_wr **bad_recv_wr);

int ocrdma_dereg_mr(struct ib_mr *ib_mr, struct ib_udata *udata);
struct ib_mr *ocrdma_get_dma_mr(struct ib_pd *, int acc);
struct ib_mr *ocrdma_reg_user_mr(struct ib_pd *, u64 start, u64 length,
				 u64 virt, int acc, struct ib_udata *);
struct ib_mr *ocrdma_alloc_mr(struct ib_pd *pd, enum ib_mr_type mr_type,
			      u32 max_num_sg, struct ib_udata *udata);
int ocrdma_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		     unsigned int *sg_offset);

#endif				/* __OCRDMA_VERBS_H__ */
