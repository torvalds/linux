/*
 * Copyright (c) 2013, Cisco Systems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef USNIC_IB_VERBS_H_
#define USNIC_IB_VERBS_H_

#include "usnic_ib.h"

enum rdma_link_layer usnic_ib_port_link_layer(struct ib_device *device,
					      u32 port_num);
int usnic_ib_query_device(struct ib_device *ibdev,
				struct ib_device_attr *props,
			  struct ib_udata *uhw);
int usnic_ib_query_port(struct ib_device *ibdev, u32 port,
				struct ib_port_attr *props);
int usnic_ib_query_qp(struct ib_qp *qp, struct ib_qp_attr *qp_attr,
				int qp_attr_mask,
				struct ib_qp_init_attr *qp_init_attr);
int usnic_ib_query_gid(struct ib_device *ibdev, u32 port, int index,
				union ib_gid *gid);
int usnic_ib_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata);
int usnic_ib_dealloc_pd(struct ib_pd *pd, struct ib_udata *udata);
struct ib_qp *usnic_ib_create_qp(struct ib_pd *pd,
					struct ib_qp_init_attr *init_attr,
					struct ib_udata *udata);
int usnic_ib_destroy_qp(struct ib_qp *qp, struct ib_udata *udata);
int usnic_ib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
				int attr_mask, struct ib_udata *udata);
int usnic_ib_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		       struct ib_udata *udata);
int usnic_ib_destroy_cq(struct ib_cq *cq, struct ib_udata *udata);
struct ib_mr *usnic_ib_reg_mr(struct ib_pd *pd, u64 start, u64 length,
				u64 virt_addr, int access_flags,
				struct ib_udata *udata);
int usnic_ib_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata);
int usnic_ib_alloc_ucontext(struct ib_ucontext *uctx, struct ib_udata *udata);
void usnic_ib_dealloc_ucontext(struct ib_ucontext *ibcontext);
int usnic_ib_mmap(struct ib_ucontext *context,
			struct vm_area_struct *vma);
#endif /* !USNIC_IB_VERBS_H */
