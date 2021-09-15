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
 * Description: Statistics (header)
 *
 */

#ifndef __BNXT_RE_HW_STATS_H__
#define __BNXT_RE_HW_STATS_H__

enum bnxt_re_hw_stats {
	BNXT_RE_ACTIVE_PD,
	BNXT_RE_ACTIVE_AH,
	BNXT_RE_ACTIVE_QP,
	BNXT_RE_ACTIVE_SRQ,
	BNXT_RE_ACTIVE_CQ,
	BNXT_RE_ACTIVE_MR,
	BNXT_RE_ACTIVE_MW,
	BNXT_RE_RX_PKTS,
	BNXT_RE_RX_BYTES,
	BNXT_RE_TX_PKTS,
	BNXT_RE_TX_BYTES,
	BNXT_RE_RECOVERABLE_ERRORS,
	BNXT_RE_RX_DROPS,
	BNXT_RE_RX_DISCARDS,
	BNXT_RE_TO_RETRANSMITS,
	BNXT_RE_SEQ_ERR_NAKS_RCVD,
	BNXT_RE_MAX_RETRY_EXCEEDED,
	BNXT_RE_RNR_NAKS_RCVD,
	BNXT_RE_MISSING_RESP,
	BNXT_RE_UNRECOVERABLE_ERR,
	BNXT_RE_BAD_RESP_ERR,
	BNXT_RE_LOCAL_QP_OP_ERR,
	BNXT_RE_LOCAL_PROTECTION_ERR,
	BNXT_RE_MEM_MGMT_OP_ERR,
	BNXT_RE_REMOTE_INVALID_REQ_ERR,
	BNXT_RE_REMOTE_ACCESS_ERR,
	BNXT_RE_REMOTE_OP_ERR,
	BNXT_RE_DUP_REQ,
	BNXT_RE_RES_EXCEED_MAX,
	BNXT_RE_RES_LENGTH_MISMATCH,
	BNXT_RE_RES_EXCEEDS_WQE,
	BNXT_RE_RES_OPCODE_ERR,
	BNXT_RE_RES_RX_INVALID_RKEY,
	BNXT_RE_RES_RX_DOMAIN_ERR,
	BNXT_RE_RES_RX_NO_PERM,
	BNXT_RE_RES_RX_RANGE_ERR,
	BNXT_RE_RES_TX_INVALID_RKEY,
	BNXT_RE_RES_TX_DOMAIN_ERR,
	BNXT_RE_RES_TX_NO_PERM,
	BNXT_RE_RES_TX_RANGE_ERR,
	BNXT_RE_RES_IRRQ_OFLOW,
	BNXT_RE_RES_UNSUP_OPCODE,
	BNXT_RE_RES_UNALIGNED_ATOMIC,
	BNXT_RE_RES_REM_INV_ERR,
	BNXT_RE_RES_MEM_ERROR,
	BNXT_RE_RES_SRQ_ERR,
	BNXT_RE_RES_CMP_ERR,
	BNXT_RE_RES_INVALID_DUP_RKEY,
	BNXT_RE_RES_WQE_FORMAT_ERR,
	BNXT_RE_RES_CQ_LOAD_ERR,
	BNXT_RE_RES_SRQ_LOAD_ERR,
	BNXT_RE_RES_TX_PCI_ERR,
	BNXT_RE_RES_RX_PCI_ERR,
	BNXT_RE_OUT_OF_SEQ_ERR,
	BNXT_RE_TX_ATOMIC_REQ,
	BNXT_RE_TX_READ_REQ,
	BNXT_RE_TX_READ_RES,
	BNXT_RE_TX_WRITE_REQ,
	BNXT_RE_TX_SEND_REQ,
	BNXT_RE_RX_ATOMIC_REQ,
	BNXT_RE_RX_READ_REQ,
	BNXT_RE_RX_READ_RESP,
	BNXT_RE_RX_WRITE_REQ,
	BNXT_RE_RX_SEND_REQ,
	BNXT_RE_RX_ROCE_GOOD_PKTS,
	BNXT_RE_RX_ROCE_GOOD_BYTES,
	BNXT_RE_OOB,
	BNXT_RE_NUM_EXT_COUNTERS
};

#define BNXT_RE_NUM_STD_COUNTERS (BNXT_RE_OUT_OF_SEQ_ERR + 1)

struct bnxt_re_rstat {
	struct bnxt_qplib_roce_stats    errs;
	struct bnxt_qplib_ext_stat      ext_stat;
};

struct bnxt_re_stats {
	struct bnxt_re_rstat            rstat;
};

struct rdma_hw_stats *bnxt_re_ib_alloc_hw_port_stats(struct ib_device *ibdev,
						     u32 port_num);
int bnxt_re_ib_get_hw_stats(struct ib_device *ibdev,
			    struct rdma_hw_stats *stats,
			    u32 port, int index);
#endif /* __BNXT_RE_HW_STATS_H__ */
