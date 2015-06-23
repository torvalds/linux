/*
 * Copyright (c) 2013, Cisco Systems, Inc. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
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
						u8 port_num);
int usnic_ib_query_device(struct ib_device *ibdev,
				struct ib_device_attr *props,
			  struct ib_udata *uhw);
int usnic_ib_query_port(struct ib_device *ibdev, u8 port,
				struct ib_port_attr *props);
enum rdma_protocol_type
usnic_ib_query_protocol(struct ib_device *device, u8 port_num);
int usnic_ib_query_qp(struct ib_qp *qp, struct ib_qp_attr *qp_attr,
				int qp_attr_mask,
				struct ib_qp_init_attr *qp_init_attr);
int usnic_ib_query_gid(struct ib_device *ibdev, u8 port, int index,
				union ib_gid *gid);
int usnic_ib_query_pkey(struct ib_device *ibdev, u8 port, u16 index,
				u16 *pkey);
struct ib_pd *usnic_ib_alloc_pd(struct ib_device *ibdev,
				struct ib_ucontext *context,
				struct ib_udata *udata);
int usnic_ib_dealloc_pd(struct ib_pd *pd);
struct ib_qp *usnic_ib_create_qp(struct ib_pd *pd,
					struct ib_qp_init_attr *init_attr,
					struct ib_udata *udata);
int usnic_ib_destroy_qp(struct ib_qp *qp);
int usnic_ib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
				int attr_mask, struct ib_udata *udata);
struct ib_cq *usnic_ib_create_cq(struct ib_device *ibdev,
				 const struct ib_cq_init_attr *attr,
				 struct ib_ucontext *context,
				 struct ib_udata *udata);
int usnic_ib_destroy_cq(struct ib_cq *cq);
struct ib_mr *usnic_ib_reg_mr(struct ib_pd *pd, u64 start, u64 length,
				u64 virt_addr, int access_flags,
				struct ib_udata *udata);
int usnic_ib_dereg_mr(struct ib_mr *ibmr);
struct ib_ucontext *usnic_ib_alloc_ucontext(struct ib_device *ibdev,
						struct ib_udata *udata);
int usnic_ib_dealloc_ucontext(struct ib_ucontext *ibcontext);
int usnic_ib_mmap(struct ib_ucontext *context,
			struct vm_area_struct *vma);
struct ib_ah *usnic_ib_create_ah(struct ib_pd *pd,
					struct ib_ah_attr *ah_attr);
int usnic_ib_destroy_ah(struct ib_ah *ah);
int usnic_ib_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
			struct ib_send_wr **bad_wr);
int usnic_ib_post_recv(struct ib_qp *ibqp, struct ib_recv_wr *wr,
			struct ib_recv_wr **bad_wr);
int usnic_ib_poll_cq(struct ib_cq *ibcq, int num_entries,
			struct ib_wc *wc);
int usnic_ib_req_notify_cq(struct ib_cq *cq,
				enum ib_cq_notify_flags flags);
struct ib_mr *usnic_ib_get_dma_mr(struct ib_pd *pd, int acc);
#endif /* !USNIC_IB_VERBS_H */
