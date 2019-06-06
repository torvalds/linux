/* QLogic qedr NIC Driver
 * Copyright (c) 2015-2016  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
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
 *        disclaimer in the documentation and /or other materials
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
 */
#ifndef __QEDR_VERBS_H__
#define __QEDR_VERBS_H__

int qedr_query_device(struct ib_device *ibdev,
		      struct ib_device_attr *attr, struct ib_udata *udata);
int qedr_query_port(struct ib_device *, u8 port, struct ib_port_attr *props);
int qedr_modify_port(struct ib_device *, u8 port, int mask,
		     struct ib_port_modify *props);

int qedr_iw_query_gid(struct ib_device *ibdev, u8 port,
		      int index, union ib_gid *gid);

int qedr_query_pkey(struct ib_device *, u8 port, u16 index, u16 *pkey);

int qedr_alloc_ucontext(struct ib_ucontext *uctx, struct ib_udata *udata);
void qedr_dealloc_ucontext(struct ib_ucontext *uctx);

int qedr_mmap(struct ib_ucontext *, struct vm_area_struct *vma);
int qedr_alloc_pd(struct ib_pd *pd, struct ib_udata *udata);
void qedr_dealloc_pd(struct ib_pd *pd, struct ib_udata *udata);

struct ib_cq *qedr_create_cq(struct ib_device *ibdev,
			     const struct ib_cq_init_attr *attr,
			     struct ib_udata *udata);
int qedr_resize_cq(struct ib_cq *, int cqe, struct ib_udata *);
int qedr_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata);
int qedr_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags);
struct ib_qp *qedr_create_qp(struct ib_pd *, struct ib_qp_init_attr *attrs,
			     struct ib_udata *);
int qedr_modify_qp(struct ib_qp *, struct ib_qp_attr *attr,
		   int attr_mask, struct ib_udata *udata);
int qedr_query_qp(struct ib_qp *, struct ib_qp_attr *qp_attr,
		  int qp_attr_mask, struct ib_qp_init_attr *);
int qedr_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata);

int qedr_create_srq(struct ib_srq *ibsrq, struct ib_srq_init_attr *attr,
		    struct ib_udata *udata);
int qedr_modify_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr,
		    enum ib_srq_attr_mask attr_mask, struct ib_udata *udata);
int qedr_query_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr);
void qedr_destroy_srq(struct ib_srq *ibsrq, struct ib_udata *udata);
int qedr_post_srq_recv(struct ib_srq *ibsrq, const struct ib_recv_wr *wr,
		       const struct ib_recv_wr **bad_recv_wr);
int qedr_create_ah(struct ib_ah *ibah, struct rdma_ah_attr *attr, u32 flags,
		   struct ib_udata *udata);
void qedr_destroy_ah(struct ib_ah *ibah, u32 flags);

int qedr_dereg_mr(struct ib_mr *ib_mr, struct ib_udata *udata);
struct ib_mr *qedr_get_dma_mr(struct ib_pd *, int acc);

struct ib_mr *qedr_reg_user_mr(struct ib_pd *, u64 start, u64 length,
			       u64 virt, int acc, struct ib_udata *);

int qedr_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg,
		   int sg_nents, unsigned int *sg_offset);

struct ib_mr *qedr_alloc_mr(struct ib_pd *pd, enum ib_mr_type mr_type,
			    u32 max_num_sg, struct ib_udata *udata);
int qedr_poll_cq(struct ib_cq *, int num_entries, struct ib_wc *wc);
int qedr_post_send(struct ib_qp *, const struct ib_send_wr *,
		   const struct ib_send_wr **bad_wr);
int qedr_post_recv(struct ib_qp *, const struct ib_recv_wr *,
		   const struct ib_recv_wr **bad_wr);
int qedr_process_mad(struct ib_device *ibdev, int process_mad_flags,
		     u8 port_num, const struct ib_wc *in_wc,
		     const struct ib_grh *in_grh,
		     const struct ib_mad_hdr *in_mad,
		     size_t in_mad_size, struct ib_mad_hdr *out_mad,
		     size_t *out_mad_size, u16 *out_mad_pkey_index);

int qedr_port_immutable(struct ib_device *ibdev, u8 port_num,
			struct ib_port_immutable *immutable);
#endif
