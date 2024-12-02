/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#ifndef _QED_ROCE_H
#define _QED_ROCE_H
#include <linux/types.h>
#include <linux/slab.h>

#if IS_ENABLED(CONFIG_QED_RDMA)
void qed_roce_dpm_dcbx(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);
#else
static inline void qed_roce_dpm_dcbx(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt) {}
#endif

int qed_roce_setup(struct qed_hwfn *p_hwfn);
void qed_roce_stop(struct qed_hwfn *p_hwfn);
int qed_roce_init_hw(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);
int qed_roce_alloc_cid(struct qed_hwfn *p_hwfn, u16 *cid);
int qed_roce_destroy_qp(struct qed_hwfn *p_hwfn, struct qed_rdma_qp *qp);

int qed_roce_query_qp(struct qed_hwfn *p_hwfn,
		      struct qed_rdma_qp *qp,
		      struct qed_rdma_query_qp_out_params *out_params);

int qed_roce_modify_qp(struct qed_hwfn *p_hwfn,
		       struct qed_rdma_qp *qp,
		       enum qed_roce_qp_state prev_state,
		       struct qed_rdma_modify_qp_in_params *params);

#endif
