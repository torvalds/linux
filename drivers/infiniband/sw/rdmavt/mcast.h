/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * Copyright(c) 2016 Intel Corporation.
 */

#ifndef DEF_RVTMCAST_H
#define DEF_RVTMCAST_H

#include <rdma/rdma_vt.h>

void rvt_driver_mcast_init(struct rvt_dev_info *rdi);
int rvt_attach_mcast(struct ib_qp *ibqp, union ib_gid *gid, u16 lid);
int rvt_detach_mcast(struct ib_qp *ibqp, union ib_gid *gid, u16 lid);
int rvt_mcast_tree_empty(struct rvt_dev_info *rdi);

#endif          /* DEF_RVTMCAST_H */
