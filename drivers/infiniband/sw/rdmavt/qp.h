/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright(c) 2016 Intel Corporation.
 */

#ifndef DEF_RVTQP_H
#define DEF_RVTQP_H

#include <rdma/rdmavt_qp.h>

int rvt_driver_qp_init(struct rvt_dev_info *rdi);
void rvt_qp_exit(struct rvt_dev_info *rdi);
int rvt_create_qp(struct ib_qp *ibqp, struct ib_qp_init_attr *init_attr,
		  struct ib_udata *udata);
int rvt_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		  int attr_mask, struct ib_udata *udata);
int rvt_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata);
int rvt_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		 int attr_mask, struct ib_qp_init_attr *init_attr);
int rvt_post_recv(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
		  const struct ib_recv_wr **bad_wr);
int rvt_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr,
		  const struct ib_send_wr **bad_wr);
int rvt_post_srq_recv(struct ib_srq *ibsrq, const struct ib_recv_wr *wr,
		      const struct ib_recv_wr **bad_wr);
int rvt_wss_init(struct rvt_dev_info *rdi);
void rvt_wss_exit(struct rvt_dev_info *rdi);
int rvt_alloc_rq(struct rvt_rq *rq, u32 size, int node,
		 struct ib_udata *udata);
#endif          /* DEF_RVTQP_H */
