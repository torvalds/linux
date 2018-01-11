/*
 * Broadcom NetXtreme-E RoCE driver.
 *
 * Copyright (c) 2016 - 2017, Broadcom. All rights reserved.  The term
 * Broadcom refers to Broadcom Limited and/or its subsidiaries.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Description: Statistics
 *
 */

#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/prefetch.h>
#include <linux/delay.h>

#include <rdma/ib_addr.h>

#include "bnxt_ulp.h"
#include "roce_hsi.h"
#include "qplib_res.h"
#include "qplib_sp.h"
#include "qplib_fp.h"
#include "qplib_rcfw.h"
#include "bnxt_re.h"
#include "hw_counters.h"

static const char * const bnxt_re_stat_name[] = {
	[BNXT_RE_ACTIVE_QP]		=  "active_qps",
	[BNXT_RE_ACTIVE_SRQ]		=  "active_srqs",
	[BNXT_RE_ACTIVE_CQ]		=  "active_cqs",
	[BNXT_RE_ACTIVE_MR]		=  "active_mrs",
	[BNXT_RE_ACTIVE_MW]		=  "active_mws",
	[BNXT_RE_RX_PKTS]		=  "rx_pkts",
	[BNXT_RE_RX_BYTES]		=  "rx_bytes",
	[BNXT_RE_TX_PKTS]		=  "tx_pkts",
	[BNXT_RE_TX_BYTES]		=  "tx_bytes",
	[BNXT_RE_RECOVERABLE_ERRORS]	=  "recoverable_errors",
	[BNXT_RE_TO_RETRANSMITS]        = "to_retransmits",
	[BNXT_RE_SEQ_ERR_NAKS_RCVD]     = "seq_err_naks_rcvd",
	[BNXT_RE_MAX_RETRY_EXCEEDED]    = "max_retry_exceeded",
	[BNXT_RE_RNR_NAKS_RCVD]         = "rnr_naks_rcvd",
	[BNXT_RE_MISSING_RESP]          = "missin_resp",
	[BNXT_RE_UNRECOVERABLE_ERR]     = "unrecoverable_err",
	[BNXT_RE_BAD_RESP_ERR]          = "bad_resp_err",
	[BNXT_RE_LOCAL_QP_OP_ERR]       = "local_qp_op_err",
	[BNXT_RE_LOCAL_PROTECTION_ERR]  = "local_protection_err",
	[BNXT_RE_MEM_MGMT_OP_ERR]       = "mem_mgmt_op_err",
	[BNXT_RE_REMOTE_INVALID_REQ_ERR] = "remote_invalid_req_err",
	[BNXT_RE_REMOTE_ACCESS_ERR]     = "remote_access_err",
	[BNXT_RE_REMOTE_OP_ERR]         = "remote_op_err",
	[BNXT_RE_DUP_REQ]               = "dup_req",
	[BNXT_RE_RES_EXCEED_MAX]        = "res_exceed_max",
	[BNXT_RE_RES_LENGTH_MISMATCH]   = "res_length_mismatch",
	[BNXT_RE_RES_EXCEEDS_WQE]       = "res_exceeds_wqe",
	[BNXT_RE_RES_OPCODE_ERR]        = "res_opcode_err",
	[BNXT_RE_RES_RX_INVALID_RKEY]   = "res_rx_invalid_rkey",
	[BNXT_RE_RES_RX_DOMAIN_ERR]     = "res_rx_domain_err",
	[BNXT_RE_RES_RX_NO_PERM]        = "res_rx_no_perm",
	[BNXT_RE_RES_RX_RANGE_ERR]      = "res_rx_range_err",
	[BNXT_RE_RES_TX_INVALID_RKEY]   = "res_tx_invalid_rkey",
	[BNXT_RE_RES_TX_DOMAIN_ERR]     = "res_tx_domain_err",
	[BNXT_RE_RES_TX_NO_PERM]        = "res_tx_no_perm",
	[BNXT_RE_RES_TX_RANGE_ERR]      = "res_tx_range_err",
	[BNXT_RE_RES_IRRQ_OFLOW]        = "res_irrq_oflow",
	[BNXT_RE_RES_UNSUP_OPCODE]      = "res_unsup_opcode",
	[BNXT_RE_RES_UNALIGNED_ATOMIC]  = "res_unaligned_atomic",
	[BNXT_RE_RES_REM_INV_ERR]       = "res_rem_inv_err",
	[BNXT_RE_RES_MEM_ERROR]         = "res_mem_err",
	[BNXT_RE_RES_SRQ_ERR]           = "res_srq_err",
	[BNXT_RE_RES_CMP_ERR]           = "res_cmp_err",
	[BNXT_RE_RES_INVALID_DUP_RKEY]  = "res_invalid_dup_rkey",
	[BNXT_RE_RES_WQE_FORMAT_ERR]    = "res_wqe_format_err",
	[BNXT_RE_RES_CQ_LOAD_ERR]       = "res_cq_load_err",
	[BNXT_RE_RES_SRQ_LOAD_ERR]      = "res_srq_load_err",
	[BNXT_RE_RES_TX_PCI_ERR]        = "res_tx_pci_err",
	[BNXT_RE_RES_RX_PCI_ERR]        = "res_rx_pci_err"
};

int bnxt_re_ib_get_hw_stats(struct ib_device *ibdev,
			    struct rdma_hw_stats *stats,
			    u8 port, int index)
{
	struct bnxt_re_dev *rdev = to_bnxt_re_dev(ibdev, ibdev);
	struct ctx_hw_stats *bnxt_re_stats = rdev->qplib_ctx.stats.dma;
	int rc  = 0;

	if (!port || !stats)
		return -EINVAL;

	stats->value[BNXT_RE_ACTIVE_QP] = atomic_read(&rdev->qp_count);
	stats->value[BNXT_RE_ACTIVE_SRQ] = atomic_read(&rdev->srq_count);
	stats->value[BNXT_RE_ACTIVE_CQ] = atomic_read(&rdev->cq_count);
	stats->value[BNXT_RE_ACTIVE_MR] = atomic_read(&rdev->mr_count);
	stats->value[BNXT_RE_ACTIVE_MW] = atomic_read(&rdev->mw_count);
	if (bnxt_re_stats) {
		stats->value[BNXT_RE_RECOVERABLE_ERRORS] =
			le64_to_cpu(bnxt_re_stats->tx_bcast_pkts);
		stats->value[BNXT_RE_RX_PKTS] =
			le64_to_cpu(bnxt_re_stats->rx_ucast_pkts);
		stats->value[BNXT_RE_RX_BYTES] =
			le64_to_cpu(bnxt_re_stats->rx_ucast_bytes);
		stats->value[BNXT_RE_TX_PKTS] =
			le64_to_cpu(bnxt_re_stats->tx_ucast_pkts);
		stats->value[BNXT_RE_TX_BYTES] =
			le64_to_cpu(bnxt_re_stats->tx_ucast_bytes);
	}
	if (test_bit(BNXT_RE_FLAG_ISSUE_ROCE_STATS, &rdev->flags)) {
		rc = bnxt_qplib_get_roce_stats(&rdev->rcfw, &rdev->stats);
		if (rc)
			clear_bit(BNXT_RE_FLAG_ISSUE_ROCE_STATS,
				  &rdev->flags);
		stats->value[BNXT_RE_TO_RETRANSMITS] =
					rdev->stats.to_retransmits;
		stats->value[BNXT_RE_SEQ_ERR_NAKS_RCVD] =
					rdev->stats.seq_err_naks_rcvd;
		stats->value[BNXT_RE_MAX_RETRY_EXCEEDED] =
					rdev->stats.max_retry_exceeded;
		stats->value[BNXT_RE_RNR_NAKS_RCVD] =
					rdev->stats.rnr_naks_rcvd;
		stats->value[BNXT_RE_MISSING_RESP] =
					rdev->stats.missing_resp;
		stats->value[BNXT_RE_UNRECOVERABLE_ERR] =
					rdev->stats.unrecoverable_err;
		stats->value[BNXT_RE_BAD_RESP_ERR] =
					rdev->stats.bad_resp_err;
		stats->value[BNXT_RE_LOCAL_QP_OP_ERR]	=
				rdev->stats.local_qp_op_err;
		stats->value[BNXT_RE_LOCAL_PROTECTION_ERR] =
				rdev->stats.local_protection_err;
		stats->value[BNXT_RE_MEM_MGMT_OP_ERR] =
				rdev->stats.mem_mgmt_op_err;
		stats->value[BNXT_RE_REMOTE_INVALID_REQ_ERR] =
				rdev->stats.remote_invalid_req_err;
		stats->value[BNXT_RE_REMOTE_ACCESS_ERR] =
				rdev->stats.remote_access_err;
		stats->value[BNXT_RE_REMOTE_OP_ERR] =
				rdev->stats.remote_op_err;
		stats->value[BNXT_RE_DUP_REQ] =
				rdev->stats.dup_req;
		stats->value[BNXT_RE_RES_EXCEED_MAX] =
				rdev->stats.res_exceed_max;
		stats->value[BNXT_RE_RES_LENGTH_MISMATCH] =
				rdev->stats.res_length_mismatch;
		stats->value[BNXT_RE_RES_EXCEEDS_WQE] =
				rdev->stats.res_exceeds_wqe;
		stats->value[BNXT_RE_RES_OPCODE_ERR] =
				rdev->stats.res_opcode_err;
		stats->value[BNXT_RE_RES_RX_INVALID_RKEY] =
				rdev->stats.res_rx_invalid_rkey;
		stats->value[BNXT_RE_RES_RX_DOMAIN_ERR] =
				rdev->stats.res_rx_domain_err;
		stats->value[BNXT_RE_RES_RX_NO_PERM] =
				rdev->stats.res_rx_no_perm;
		stats->value[BNXT_RE_RES_RX_RANGE_ERR]  =
				rdev->stats.res_rx_range_err;
		stats->value[BNXT_RE_RES_TX_INVALID_RKEY] =
				rdev->stats.res_tx_invalid_rkey;
		stats->value[BNXT_RE_RES_TX_DOMAIN_ERR] =
				rdev->stats.res_tx_domain_err;
		stats->value[BNXT_RE_RES_TX_NO_PERM] =
				rdev->stats.res_tx_no_perm;
		stats->value[BNXT_RE_RES_TX_RANGE_ERR]  =
				rdev->stats.res_tx_range_err;
		stats->value[BNXT_RE_RES_IRRQ_OFLOW] =
				rdev->stats.res_irrq_oflow;
		stats->value[BNXT_RE_RES_UNSUP_OPCODE]  =
				rdev->stats.res_unsup_opcode;
		stats->value[BNXT_RE_RES_UNALIGNED_ATOMIC] =
				rdev->stats.res_unaligned_atomic;
		stats->value[BNXT_RE_RES_REM_INV_ERR]   =
				rdev->stats.res_rem_inv_err;
		stats->value[BNXT_RE_RES_MEM_ERROR] =
				rdev->stats.res_mem_error;
		stats->value[BNXT_RE_RES_SRQ_ERR] =
				rdev->stats.res_srq_err;
		stats->value[BNXT_RE_RES_CMP_ERR] =
				rdev->stats.res_cmp_err;
		stats->value[BNXT_RE_RES_INVALID_DUP_RKEY] =
				rdev->stats.res_invalid_dup_rkey;
		stats->value[BNXT_RE_RES_WQE_FORMAT_ERR] =
				rdev->stats.res_wqe_format_err;
		stats->value[BNXT_RE_RES_CQ_LOAD_ERR]   =
				rdev->stats.res_cq_load_err;
		stats->value[BNXT_RE_RES_SRQ_LOAD_ERR]  =
				rdev->stats.res_srq_load_err;
		stats->value[BNXT_RE_RES_TX_PCI_ERR]    =
				rdev->stats.res_tx_pci_err;
		stats->value[BNXT_RE_RES_RX_PCI_ERR]    =
				rdev->stats.res_rx_pci_err;
	}

	return ARRAY_SIZE(bnxt_re_stat_name);
}

struct rdma_hw_stats *bnxt_re_ib_alloc_hw_stats(struct ib_device *ibdev,
						u8 port_num)
{
	BUILD_BUG_ON(ARRAY_SIZE(bnxt_re_stat_name) != BNXT_RE_NUM_COUNTERS);
	/* We support only per port stats */
	if (!port_num)
		return NULL;

	return rdma_alloc_hw_stats_struct(bnxt_re_stat_name,
					  ARRAY_SIZE(bnxt_re_stat_name),
					  RDMA_HW_STATS_DEFAULT_LIFESPAN);
}
