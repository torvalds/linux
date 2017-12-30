/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
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
