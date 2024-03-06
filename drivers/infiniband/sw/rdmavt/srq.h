/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright(c) 2016 Intel Corporation.
 */

#ifndef DEF_RVTSRQ_H
#define DEF_RVTSRQ_H

#include <rdma/rdma_vt.h>
void rvt_driver_srq_init(struct rvt_dev_info *rdi);
int rvt_create_srq(struct ib_srq *ibsrq, struct ib_srq_init_attr *srq_init_attr,
		   struct ib_udata *udata);
int rvt_modify_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr,
		   enum ib_srq_attr_mask attr_mask,
		   struct ib_udata *udata);
int rvt_query_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr);
int rvt_destroy_srq(struct ib_srq *ibsrq, struct ib_udata *udata);

#endif          /* DEF_RVTSRQ_H */
