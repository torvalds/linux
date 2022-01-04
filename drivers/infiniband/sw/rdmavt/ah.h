/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * Copyright(c) 2016 Intel Corporation.
 */

#ifndef DEF_RVTAH_H
#define DEF_RVTAH_H

#include <rdma/rdma_vt.h>

int rvt_create_ah(struct ib_ah *ah, struct rdma_ah_init_attr *init_attr,
		  struct ib_udata *udata);
int rvt_destroy_ah(struct ib_ah *ibah, u32 destroy_flags);
int rvt_modify_ah(struct ib_ah *ibah, struct rdma_ah_attr *ah_attr);
int rvt_query_ah(struct ib_ah *ibah, struct rdma_ah_attr *ah_attr);

#endif          /* DEF_RVTAH_H */
