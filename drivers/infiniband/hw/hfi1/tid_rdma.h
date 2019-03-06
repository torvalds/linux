/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Copyright(c) 2018 Intel Corporation.
 *
 */
#ifndef HFI1_TID_RDMA_H
#define HFI1_TID_RDMA_H

int hfi1_qp_priv_init(struct rvt_dev_info *rdi, struct rvt_qp *qp,
		      struct ib_qp_init_attr *init_attr);

#endif /* HFI1_TID_RDMA_H */

