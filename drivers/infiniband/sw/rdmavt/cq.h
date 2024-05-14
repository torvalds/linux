/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * Copyright(c) 2016 - 2018 Intel Corporation.
 */

#ifndef DEF_RVTCQ_H
#define DEF_RVTCQ_H

#include <rdma/rdma_vt.h>
#include <rdma/rdmavt_cq.h>

int rvt_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		  struct ib_udata *udata);
int rvt_destroy_cq(struct ib_cq *ibcq, struct ib_udata *udata);
int rvt_req_notify_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags notify_flags);
int rvt_resize_cq(struct ib_cq *ibcq, int cqe, struct ib_udata *udata);
int rvt_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *entry);
int rvt_driver_cq_init(void);
void rvt_cq_exit(void);
#endif          /* DEF_RVTCQ_H */
