/*******************************************************************
 * This file is part of the Emulex RoCE Device Driver for          *
 * RoCE (RDMA over Converged Ethernet) adapters.                   *
 * Copyright (C) 2008-2012 Emulex. All rights reserved.            *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.emulex.com                                                  *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *
 * Contact Information:
 * linux-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 *******************************************************************/

#ifndef __OCRDMA_VERBS_H__
#define __OCRDMA_VERBS_H__

#include <linux/version.h>
int ocrdma_post_send(struct ib_qp *, struct ib_send_wr *,
		     struct ib_send_wr **bad_wr);
int ocrdma_post_recv(struct ib_qp *, struct ib_recv_wr *,
		     struct ib_recv_wr **bad_wr);

int ocrdma_poll_cq(struct ib_cq *, int num_entries, struct ib_wc *wc);
int ocrdma_arm_cq(struct ib_cq *, enum ib_cq_notify_flags flags);

int ocrdma_query_device(struct ib_device *, struct ib_device_attr *props);
int ocrdma_query_port(struct ib_device *, u8 port, struct ib_port_attr *props);
int ocrdma_modify_port(struct ib_device *, u8 port, int mask,
		       struct ib_port_modify *props);

void ocrdma_get_guid(struct ocrdma_dev *, u8 *guid);
int ocrdma_query_gid(struct ib_device *, u8 port,
		     int index, union ib_gid *gid);
int ocrdma_query_pkey(struct ib_device *, u8 port, u16 index, u16 *pkey);

struct ib_ucontext *ocrdma_alloc_ucontext(struct ib_device *,
					  struct ib_udata *);
int ocrdma_dealloc_ucontext(struct ib_ucontext *);

int ocrdma_mmap(struct ib_ucontext *, struct vm_area_struct *vma);

struct ib_pd *ocrdma_alloc_pd(struct ib_device *,
			      struct ib_ucontext *, struct ib_udata *);
int ocrdma_dealloc_pd(struct ib_pd *pd);

struct ib_cq *ocrdma_create_cq(struct ib_device *, int entries, int vector,
			       struct ib_ucontext *, struct ib_udata *);
int ocrdma_resize_cq(struct ib_cq *, int cqe, struct ib_udata *);
int ocrdma_destroy_cq(struct ib_cq *);

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
int ocrdma_destroy_qp(struct ib_qp *);

struct ib_srq *ocrdma_create_srq(struct ib_pd *, struct ib_srq_init_attr *,
				 struct ib_udata *);
int ocrdma_modify_srq(struct ib_srq *, struct ib_srq_attr *,
		      enum ib_srq_attr_mask, struct ib_udata *);
int ocrdma_query_srq(struct ib_srq *, struct ib_srq_attr *);
int ocrdma_destroy_srq(struct ib_srq *);
int ocrdma_post_srq_recv(struct ib_srq *, struct ib_recv_wr *,
			 struct ib_recv_wr **bad_recv_wr);

int ocrdma_dereg_mr(struct ib_mr *);
struct ib_mr *ocrdma_get_dma_mr(struct ib_pd *, int acc);
struct ib_mr *ocrdma_reg_kernel_mr(struct ib_pd *,
				   struct ib_phys_buf *buffer_list,
				   int num_phys_buf, int acc, u64 *iova_start);
struct ib_mr *ocrdma_reg_user_mr(struct ib_pd *, u64 start, u64 length,
				 u64 virt, int acc, struct ib_udata *);

#endif				/* __OCRDMA_VERBS_H__ */
