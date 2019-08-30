/*
 * Copyright(c) 2015 - 2018 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <rdma/ib_mad.h>
#include <rdma/ib_user_verbs.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/utsname.h>
#include <linux/rculist.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <rdma/opa_addr.h>

#include "hfi.h"
#include "common.h"
#include "device.h"
#include "trace.h"
#include "qp.h"
#include "verbs_txreq.h"
#include "debugfs.h"
#include "vnic.h"
#include "fault.h"
#include "affinity.h"

static unsigned int hfi1_lkey_table_size = 16;
module_param_named(lkey_table_size, hfi1_lkey_table_size, uint,
		   S_IRUGO);
MODULE_PARM_DESC(lkey_table_size,
		 "LKEY table size in bits (2^n, 1 <= n <= 23)");

static unsigned int hfi1_max_pds = 0xFFFF;
module_param_named(max_pds, hfi1_max_pds, uint, S_IRUGO);
MODULE_PARM_DESC(max_pds,
		 "Maximum number of protection domains to support");

static unsigned int hfi1_max_ahs = 0xFFFF;
module_param_named(max_ahs, hfi1_max_ahs, uint, S_IRUGO);
MODULE_PARM_DESC(max_ahs, "Maximum number of address handles to support");

unsigned int hfi1_max_cqes = 0x2FFFFF;
module_param_named(max_cqes, hfi1_max_cqes, uint, S_IRUGO);
MODULE_PARM_DESC(max_cqes,
		 "Maximum number of completion queue entries to support");

unsigned int hfi1_max_cqs = 0x1FFFF;
module_param_named(max_cqs, hfi1_max_cqs, uint, S_IRUGO);
MODULE_PARM_DESC(max_cqs, "Maximum number of completion queues to support");

unsigned int hfi1_max_qp_wrs = 0x3FFF;
module_param_named(max_qp_wrs, hfi1_max_qp_wrs, uint, S_IRUGO);
MODULE_PARM_DESC(max_qp_wrs, "Maximum number of QP WRs to support");

unsigned int hfi1_max_qps = 32768;
module_param_named(max_qps, hfi1_max_qps, uint, S_IRUGO);
MODULE_PARM_DESC(max_qps, "Maximum number of QPs to support");

unsigned int hfi1_max_sges = 0x60;
module_param_named(max_sges, hfi1_max_sges, uint, S_IRUGO);
MODULE_PARM_DESC(max_sges, "Maximum number of SGEs to support");

unsigned int hfi1_max_mcast_grps = 16384;
module_param_named(max_mcast_grps, hfi1_max_mcast_grps, uint, S_IRUGO);
MODULE_PARM_DESC(max_mcast_grps,
		 "Maximum number of multicast groups to support");

unsigned int hfi1_max_mcast_qp_attached = 16;
module_param_named(max_mcast_qp_attached, hfi1_max_mcast_qp_attached,
		   uint, S_IRUGO);
MODULE_PARM_DESC(max_mcast_qp_attached,
		 "Maximum number of attached QPs to support");

unsigned int hfi1_max_srqs = 1024;
module_param_named(max_srqs, hfi1_max_srqs, uint, S_IRUGO);
MODULE_PARM_DESC(max_srqs, "Maximum number of SRQs to support");

unsigned int hfi1_max_srq_sges = 128;
module_param_named(max_srq_sges, hfi1_max_srq_sges, uint, S_IRUGO);
MODULE_PARM_DESC(max_srq_sges, "Maximum number of SRQ SGEs to support");

unsigned int hfi1_max_srq_wrs = 0x1FFFF;
module_param_named(max_srq_wrs, hfi1_max_srq_wrs, uint, S_IRUGO);
MODULE_PARM_DESC(max_srq_wrs, "Maximum number of SRQ WRs support");

unsigned short piothreshold = 256;
module_param(piothreshold, ushort, S_IRUGO);
MODULE_PARM_DESC(piothreshold, "size used to determine sdma vs. pio");

static unsigned int sge_copy_mode;
module_param(sge_copy_mode, uint, S_IRUGO);
MODULE_PARM_DESC(sge_copy_mode,
		 "Verbs copy mode: 0 use memcpy, 1 use cacheless copy, 2 adapt based on WSS");

static void verbs_sdma_complete(
	struct sdma_txreq *cookie,
	int status);

static int pio_wait(struct rvt_qp *qp,
		    struct send_context *sc,
		    struct hfi1_pkt_state *ps,
		    u32 flag);

/* Length of buffer to create verbs txreq cache name */
#define TXREQ_NAME_LEN 24

/* 16B trailing buffer */
static const u8 trail_buf[MAX_16B_PADDING];

static uint wss_threshold = 80;
module_param(wss_threshold, uint, S_IRUGO);
MODULE_PARM_DESC(wss_threshold, "Percentage (1-100) of LLC to use as a threshold for a cacheless copy");
static uint wss_clean_period = 256;
module_param(wss_clean_period, uint, S_IRUGO);
MODULE_PARM_DESC(wss_clean_period, "Count of verbs copies before an entry in the page copy table is cleaned");

/*
 * Translate ib_wr_opcode into ib_wc_opcode.
 */
const enum ib_wc_opcode ib_hfi1_wc_opcode[] = {
	[IB_WR_RDMA_WRITE] = IB_WC_RDMA_WRITE,
	[IB_WR_TID_RDMA_WRITE] = IB_WC_RDMA_WRITE,
	[IB_WR_RDMA_WRITE_WITH_IMM] = IB_WC_RDMA_WRITE,
	[IB_WR_SEND] = IB_WC_SEND,
	[IB_WR_SEND_WITH_IMM] = IB_WC_SEND,
	[IB_WR_RDMA_READ] = IB_WC_RDMA_READ,
	[IB_WR_TID_RDMA_READ] = IB_WC_RDMA_READ,
	[IB_WR_ATOMIC_CMP_AND_SWP] = IB_WC_COMP_SWAP,
	[IB_WR_ATOMIC_FETCH_AND_ADD] = IB_WC_FETCH_ADD,
	[IB_WR_SEND_WITH_INV] = IB_WC_SEND,
	[IB_WR_LOCAL_INV] = IB_WC_LOCAL_INV,
	[IB_WR_REG_MR] = IB_WC_REG_MR
};

/*
 * Length of header by opcode, 0 --> not supported
 */
const u8 hdr_len_by_opcode[256] = {
	/* RC */
	[IB_OPCODE_RC_SEND_FIRST]                     = 12 + 8,
	[IB_OPCODE_RC_SEND_MIDDLE]                    = 12 + 8,
	[IB_OPCODE_RC_SEND_LAST]                      = 12 + 8,
	[IB_OPCODE_RC_SEND_LAST_WITH_IMMEDIATE]       = 12 + 8 + 4,
	[IB_OPCODE_RC_SEND_ONLY]                      = 12 + 8,
	[IB_OPCODE_RC_SEND_ONLY_WITH_IMMEDIATE]       = 12 + 8 + 4,
	[IB_OPCODE_RC_RDMA_WRITE_FIRST]               = 12 + 8 + 16,
	[IB_OPCODE_RC_RDMA_WRITE_MIDDLE]              = 12 + 8,
	[IB_OPCODE_RC_RDMA_WRITE_LAST]                = 12 + 8,
	[IB_OPCODE_RC_RDMA_WRITE_LAST_WITH_IMMEDIATE] = 12 + 8 + 4,
	[IB_OPCODE_RC_RDMA_WRITE_ONLY]                = 12 + 8 + 16,
	[IB_OPCODE_RC_RDMA_WRITE_ONLY_WITH_IMMEDIATE] = 12 + 8 + 20,
	[IB_OPCODE_RC_RDMA_READ_REQUEST]              = 12 + 8 + 16,
	[IB_OPCODE_RC_RDMA_READ_RESPONSE_FIRST]       = 12 + 8 + 4,
	[IB_OPCODE_RC_RDMA_READ_RESPONSE_MIDDLE]      = 12 + 8,
	[IB_OPCODE_RC_RDMA_READ_RESPONSE_LAST]        = 12 + 8 + 4,
	[IB_OPCODE_RC_RDMA_READ_RESPONSE_ONLY]        = 12 + 8 + 4,
	[IB_OPCODE_RC_ACKNOWLEDGE]                    = 12 + 8 + 4,
	[IB_OPCODE_RC_ATOMIC_ACKNOWLEDGE]             = 12 + 8 + 4 + 8,
	[IB_OPCODE_RC_COMPARE_SWAP]                   = 12 + 8 + 28,
	[IB_OPCODE_RC_FETCH_ADD]                      = 12 + 8 + 28,
	[IB_OPCODE_RC_SEND_LAST_WITH_INVALIDATE]      = 12 + 8 + 4,
	[IB_OPCODE_RC_SEND_ONLY_WITH_INVALIDATE]      = 12 + 8 + 4,
	[IB_OPCODE_TID_RDMA_READ_REQ]                 = 12 + 8 + 36,
	[IB_OPCODE_TID_RDMA_READ_RESP]                = 12 + 8 + 36,
	[IB_OPCODE_TID_RDMA_WRITE_REQ]                = 12 + 8 + 36,
	[IB_OPCODE_TID_RDMA_WRITE_RESP]               = 12 + 8 + 36,
	[IB_OPCODE_TID_RDMA_WRITE_DATA]               = 12 + 8 + 36,
	[IB_OPCODE_TID_RDMA_WRITE_DATA_LAST]          = 12 + 8 + 36,
	[IB_OPCODE_TID_RDMA_ACK]                      = 12 + 8 + 36,
	[IB_OPCODE_TID_RDMA_RESYNC]                   = 12 + 8 + 36,
	/* UC */
	[IB_OPCODE_UC_SEND_FIRST]                     = 12 + 8,
	[IB_OPCODE_UC_SEND_MIDDLE]                    = 12 + 8,
	[IB_OPCODE_UC_SEND_LAST]                      = 12 + 8,
	[IB_OPCODE_UC_SEND_LAST_WITH_IMMEDIATE]       = 12 + 8 + 4,
	[IB_OPCODE_UC_SEND_ONLY]                      = 12 + 8,
	[IB_OPCODE_UC_SEND_ONLY_WITH_IMMEDIATE]       = 12 + 8 + 4,
	[IB_OPCODE_UC_RDMA_WRITE_FIRST]               = 12 + 8 + 16,
	[IB_OPCODE_UC_RDMA_WRITE_MIDDLE]              = 12 + 8,
	[IB_OPCODE_UC_RDMA_WRITE_LAST]                = 12 + 8,
	[IB_OPCODE_UC_RDMA_WRITE_LAST_WITH_IMMEDIATE] = 12 + 8 + 4,
	[IB_OPCODE_UC_RDMA_WRITE_ONLY]                = 12 + 8 + 16,
	[IB_OPCODE_UC_RDMA_WRITE_ONLY_WITH_IMMEDIATE] = 12 + 8 + 20,
	/* UD */
	[IB_OPCODE_UD_SEND_ONLY]                      = 12 + 8 + 8,
	[IB_OPCODE_UD_SEND_ONLY_WITH_IMMEDIATE]       = 12 + 8 + 12
};

static const opcode_handler opcode_handler_tbl[256] = {
	/* RC */
	[IB_OPCODE_RC_SEND_FIRST]                     = &hfi1_rc_rcv,
	[IB_OPCODE_RC_SEND_MIDDLE]                    = &hfi1_rc_rcv,
	[IB_OPCODE_RC_SEND_LAST]                      = &hfi1_rc_rcv,
	[IB_OPCODE_RC_SEND_LAST_WITH_IMMEDIATE]       = &hfi1_rc_rcv,
	[IB_OPCODE_RC_SEND_ONLY]                      = &hfi1_rc_rcv,
	[IB_OPCODE_RC_SEND_ONLY_WITH_IMMEDIATE]       = &hfi1_rc_rcv,
	[IB_OPCODE_RC_RDMA_WRITE_FIRST]               = &hfi1_rc_rcv,
	[IB_OPCODE_RC_RDMA_WRITE_MIDDLE]              = &hfi1_rc_rcv,
	[IB_OPCODE_RC_RDMA_WRITE_LAST]                = &hfi1_rc_rcv,
	[IB_OPCODE_RC_RDMA_WRITE_LAST_WITH_IMMEDIATE] = &hfi1_rc_rcv,
	[IB_OPCODE_RC_RDMA_WRITE_ONLY]                = &hfi1_rc_rcv,
	[IB_OPCODE_RC_RDMA_WRITE_ONLY_WITH_IMMEDIATE] = &hfi1_rc_rcv,
	[IB_OPCODE_RC_RDMA_READ_REQUEST]              = &hfi1_rc_rcv,
	[IB_OPCODE_RC_RDMA_READ_RESPONSE_FIRST]       = &hfi1_rc_rcv,
	[IB_OPCODE_RC_RDMA_READ_RESPONSE_MIDDLE]      = &hfi1_rc_rcv,
	[IB_OPCODE_RC_RDMA_READ_RESPONSE_LAST]        = &hfi1_rc_rcv,
	[IB_OPCODE_RC_RDMA_READ_RESPONSE_ONLY]        = &hfi1_rc_rcv,
	[IB_OPCODE_RC_ACKNOWLEDGE]                    = &hfi1_rc_rcv,
	[IB_OPCODE_RC_ATOMIC_ACKNOWLEDGE]             = &hfi1_rc_rcv,
	[IB_OPCODE_RC_COMPARE_SWAP]                   = &hfi1_rc_rcv,
	[IB_OPCODE_RC_FETCH_ADD]                      = &hfi1_rc_rcv,
	[IB_OPCODE_RC_SEND_LAST_WITH_INVALIDATE]      = &hfi1_rc_rcv,
	[IB_OPCODE_RC_SEND_ONLY_WITH_INVALIDATE]      = &hfi1_rc_rcv,

	/* TID RDMA has separate handlers for different opcodes.*/
	[IB_OPCODE_TID_RDMA_WRITE_REQ]       = &hfi1_rc_rcv_tid_rdma_write_req,
	[IB_OPCODE_TID_RDMA_WRITE_RESP]      = &hfi1_rc_rcv_tid_rdma_write_resp,
	[IB_OPCODE_TID_RDMA_WRITE_DATA]      = &hfi1_rc_rcv_tid_rdma_write_data,
	[IB_OPCODE_TID_RDMA_WRITE_DATA_LAST] = &hfi1_rc_rcv_tid_rdma_write_data,
	[IB_OPCODE_TID_RDMA_READ_REQ]        = &hfi1_rc_rcv_tid_rdma_read_req,
	[IB_OPCODE_TID_RDMA_READ_RESP]       = &hfi1_rc_rcv_tid_rdma_read_resp,
	[IB_OPCODE_TID_RDMA_RESYNC]          = &hfi1_rc_rcv_tid_rdma_resync,
	[IB_OPCODE_TID_RDMA_ACK]             = &hfi1_rc_rcv_tid_rdma_ack,

	/* UC */
	[IB_OPCODE_UC_SEND_FIRST]                     = &hfi1_uc_rcv,
	[IB_OPCODE_UC_SEND_MIDDLE]                    = &hfi1_uc_rcv,
	[IB_OPCODE_UC_SEND_LAST]                      = &hfi1_uc_rcv,
	[IB_OPCODE_UC_SEND_LAST_WITH_IMMEDIATE]       = &hfi1_uc_rcv,
	[IB_OPCODE_UC_SEND_ONLY]                      = &hfi1_uc_rcv,
	[IB_OPCODE_UC_SEND_ONLY_WITH_IMMEDIATE]       = &hfi1_uc_rcv,
	[IB_OPCODE_UC_RDMA_WRITE_FIRST]               = &hfi1_uc_rcv,
	[IB_OPCODE_UC_RDMA_WRITE_MIDDLE]              = &hfi1_uc_rcv,
	[IB_OPCODE_UC_RDMA_WRITE_LAST]                = &hfi1_uc_rcv,
	[IB_OPCODE_UC_RDMA_WRITE_LAST_WITH_IMMEDIATE] = &hfi1_uc_rcv,
	[IB_OPCODE_UC_RDMA_WRITE_ONLY]                = &hfi1_uc_rcv,
	[IB_OPCODE_UC_RDMA_WRITE_ONLY_WITH_IMMEDIATE] = &hfi1_uc_rcv,
	/* UD */
	[IB_OPCODE_UD_SEND_ONLY]                      = &hfi1_ud_rcv,
	[IB_OPCODE_UD_SEND_ONLY_WITH_IMMEDIATE]       = &hfi1_ud_rcv,
	/* CNP */
	[IB_OPCODE_CNP]				      = &hfi1_cnp_rcv
};

#define OPMASK 0x1f

static const u32 pio_opmask[BIT(3)] = {
	/* RC */
	[IB_OPCODE_RC >> 5] =
		BIT(RC_OP(SEND_ONLY) & OPMASK) |
		BIT(RC_OP(SEND_ONLY_WITH_IMMEDIATE) & OPMASK) |
		BIT(RC_OP(RDMA_WRITE_ONLY) & OPMASK) |
		BIT(RC_OP(RDMA_WRITE_ONLY_WITH_IMMEDIATE) & OPMASK) |
		BIT(RC_OP(RDMA_READ_REQUEST) & OPMASK) |
		BIT(RC_OP(ACKNOWLEDGE) & OPMASK) |
		BIT(RC_OP(ATOMIC_ACKNOWLEDGE) & OPMASK) |
		BIT(RC_OP(COMPARE_SWAP) & OPMASK) |
		BIT(RC_OP(FETCH_ADD) & OPMASK),
	/* UC */
	[IB_OPCODE_UC >> 5] =
		BIT(UC_OP(SEND_ONLY) & OPMASK) |
		BIT(UC_OP(SEND_ONLY_WITH_IMMEDIATE) & OPMASK) |
		BIT(UC_OP(RDMA_WRITE_ONLY) & OPMASK) |
		BIT(UC_OP(RDMA_WRITE_ONLY_WITH_IMMEDIATE) & OPMASK),
};

/*
 * System image GUID.
 */
__be64 ib_hfi1_sys_image_guid;

/*
 * Make sure the QP is ready and able to accept the given opcode.
 */
static inline opcode_handler qp_ok(struct hfi1_packet *packet)
{
	if (!(ib_rvt_state_ops[packet->qp->state] & RVT_PROCESS_RECV_OK))
		return NULL;
	if (((packet->opcode & RVT_OPCODE_QP_MASK) ==
	     packet->qp->allowed_ops) ||
	    (packet->opcode == IB_OPCODE_CNP))
		return opcode_handler_tbl[packet->opcode];

	return NULL;
}

static u64 hfi1_fault_tx(struct rvt_qp *qp, u8 opcode, u64 pbc)
{
#ifdef CONFIG_FAULT_INJECTION
	if ((opcode & IB_OPCODE_MSP) == IB_OPCODE_MSP) {
		/*
		 * In order to drop non-IB traffic we
		 * set PbcInsertHrc to NONE (0x2).
		 * The packet will still be delivered
		 * to the receiving node but a
		 * KHdrHCRCErr (KDETH packet with a bad
		 * HCRC) will be triggered and the
		 * packet will not be delivered to the
		 * correct context.
		 */
		pbc &= ~PBC_INSERT_HCRC_SMASK;
		pbc |= (u64)PBC_IHCRC_NONE << PBC_INSERT_HCRC_SHIFT;
	} else {
		/*
		 * In order to drop regular verbs
		 * traffic we set the PbcTestEbp
		 * flag. The packet will still be
		 * delivered to the receiving node but
		 * a 'late ebp error' will be
		 * triggered and will be dropped.
		 */
		pbc |= PBC_TEST_EBP;
	}
#endif
	return pbc;
}

static opcode_handler tid_qp_ok(int opcode, struct hfi1_packet *packet)
{
	if (packet->qp->ibqp.qp_type != IB_QPT_RC ||
	    !(ib_rvt_state_ops[packet->qp->state] & RVT_PROCESS_RECV_OK))
		return NULL;
	if ((opcode & RVT_OPCODE_QP_MASK) == IB_OPCODE_TID_RDMA)
		return opcode_handler_tbl[opcode];
	return NULL;
}

void hfi1_kdeth_eager_rcv(struct hfi1_packet *packet)
{
	struct hfi1_ctxtdata *rcd = packet->rcd;
	struct ib_header *hdr = packet->hdr;
	u32 tlen = packet->tlen;
	struct hfi1_pportdata *ppd = rcd->ppd;
	struct hfi1_ibport *ibp = &ppd->ibport_data;
	struct rvt_dev_info *rdi = &ppd->dd->verbs_dev.rdi;
	opcode_handler opcode_handler;
	unsigned long flags;
	u32 qp_num;
	int lnh;
	u8 opcode;

	/* DW == LRH (2) + BTH (3) + KDETH (9) + CRC (1) */
	if (unlikely(tlen < 15 * sizeof(u32)))
		goto drop;

	lnh = be16_to_cpu(hdr->lrh[0]) & 3;
	if (lnh != HFI1_LRH_BTH)
		goto drop;

	packet->ohdr = &hdr->u.oth;
	trace_input_ibhdr(rcd->dd, packet, !!(rhf_dc_info(packet->rhf)));

	opcode = (be32_to_cpu(packet->ohdr->bth[0]) >> 24);
	inc_opstats(tlen, &rcd->opstats->stats[opcode]);

	/* verbs_qp can be picked up from any tid_rdma header struct */
	qp_num = be32_to_cpu(packet->ohdr->u.tid_rdma.r_req.verbs_qp) &
		RVT_QPN_MASK;

	rcu_read_lock();
	packet->qp = rvt_lookup_qpn(rdi, &ibp->rvp, qp_num);
	if (!packet->qp)
		goto drop_rcu;
	spin_lock_irqsave(&packet->qp->r_lock, flags);
	opcode_handler = tid_qp_ok(opcode, packet);
	if (likely(opcode_handler))
		opcode_handler(packet);
	else
		goto drop_unlock;
	spin_unlock_irqrestore(&packet->qp->r_lock, flags);
	rcu_read_unlock();

	return;
drop_unlock:
	spin_unlock_irqrestore(&packet->qp->r_lock, flags);
drop_rcu:
	rcu_read_unlock();
drop:
	ibp->rvp.n_pkt_drops++;
}

void hfi1_kdeth_expected_rcv(struct hfi1_packet *packet)
{
	struct hfi1_ctxtdata *rcd = packet->rcd;
	struct ib_header *hdr = packet->hdr;
	u32 tlen = packet->tlen;
	struct hfi1_pportdata *ppd = rcd->ppd;
	struct hfi1_ibport *ibp = &ppd->ibport_data;
	struct rvt_dev_info *rdi = &ppd->dd->verbs_dev.rdi;
	opcode_handler opcode_handler;
	unsigned long flags;
	u32 qp_num;
	int lnh;
	u8 opcode;

	/* DW == LRH (2) + BTH (3) + KDETH (9) + CRC (1) */
	if (unlikely(tlen < 15 * sizeof(u32)))
		goto drop;

	lnh = be16_to_cpu(hdr->lrh[0]) & 3;
	if (lnh != HFI1_LRH_BTH)
		goto drop;

	packet->ohdr = &hdr->u.oth;
	trace_input_ibhdr(rcd->dd, packet, !!(rhf_dc_info(packet->rhf)));

	opcode = (be32_to_cpu(packet->ohdr->bth[0]) >> 24);
	inc_opstats(tlen, &rcd->opstats->stats[opcode]);

	/* verbs_qp can be picked up from any tid_rdma header struct */
	qp_num = be32_to_cpu(packet->ohdr->u.tid_rdma.r_rsp.verbs_qp) &
		RVT_QPN_MASK;

	rcu_read_lock();
	packet->qp = rvt_lookup_qpn(rdi, &ibp->rvp, qp_num);
	if (!packet->qp)
		goto drop_rcu;
	spin_lock_irqsave(&packet->qp->r_lock, flags);
	opcode_handler = tid_qp_ok(opcode, packet);
	if (likely(opcode_handler))
		opcode_handler(packet);
	else
		goto drop_unlock;
	spin_unlock_irqrestore(&packet->qp->r_lock, flags);
	rcu_read_unlock();

	return;
drop_unlock:
	spin_unlock_irqrestore(&packet->qp->r_lock, flags);
drop_rcu:
	rcu_read_unlock();
drop:
	ibp->rvp.n_pkt_drops++;
}

static int hfi1_do_pkey_check(struct hfi1_packet *packet)
{
	struct hfi1_ctxtdata *rcd = packet->rcd;
	struct hfi1_pportdata *ppd = rcd->ppd;
	struct hfi1_16b_header *hdr = packet->hdr;
	u16 pkey;

	/* Pkey check needed only for bypass packets */
	if (packet->etype != RHF_RCV_TYPE_BYPASS)
		return 0;

	/* Perform pkey check */
	pkey = hfi1_16B_get_pkey(hdr);
	return ingress_pkey_check(ppd, pkey, packet->sc,
				  packet->qp->s_pkey_index,
				  packet->slid, true);
}

static inline void hfi1_handle_packet(struct hfi1_packet *packet,
				      bool is_mcast)
{
	u32 qp_num;
	struct hfi1_ctxtdata *rcd = packet->rcd;
	struct hfi1_pportdata *ppd = rcd->ppd;
	struct hfi1_ibport *ibp = rcd_to_iport(rcd);
	struct rvt_dev_info *rdi = &ppd->dd->verbs_dev.rdi;
	opcode_handler packet_handler;
	unsigned long flags;

	inc_opstats(packet->tlen, &rcd->opstats->stats[packet->opcode]);

	if (unlikely(is_mcast)) {
		struct rvt_mcast *mcast;
		struct rvt_mcast_qp *p;

		if (!packet->grh)
			goto drop;
		mcast = rvt_mcast_find(&ibp->rvp,
				       &packet->grh->dgid,
				       opa_get_lid(packet->dlid, 9B));
		if (!mcast)
			goto drop;
		list_for_each_entry_rcu(p, &mcast->qp_list, list) {
			packet->qp = p->qp;
			if (hfi1_do_pkey_check(packet))
				goto drop;
			spin_lock_irqsave(&packet->qp->r_lock, flags);
			packet_handler = qp_ok(packet);
			if (likely(packet_handler))
				packet_handler(packet);
			else
				ibp->rvp.n_pkt_drops++;
			spin_unlock_irqrestore(&packet->qp->r_lock, flags);
		}
		/*
		 * Notify rvt_multicast_detach() if it is waiting for us
		 * to finish.
		 */
		if (atomic_dec_return(&mcast->refcount) <= 1)
			wake_up(&mcast->wait);
	} else {
		/* Get the destination QP number. */
		if (packet->etype == RHF_RCV_TYPE_BYPASS &&
		    hfi1_16B_get_l4(packet->hdr) == OPA_16B_L4_FM)
			qp_num = hfi1_16B_get_dest_qpn(packet->mgmt);
		else
			qp_num = ib_bth_get_qpn(packet->ohdr);

		rcu_read_lock();
		packet->qp = rvt_lookup_qpn(rdi, &ibp->rvp, qp_num);
		if (!packet->qp)
			goto unlock_drop;

		if (hfi1_do_pkey_check(packet))
			goto unlock_drop;

		spin_lock_irqsave(&packet->qp->r_lock, flags);
		packet_handler = qp_ok(packet);
		if (likely(packet_handler))
			packet_handler(packet);
		else
			ibp->rvp.n_pkt_drops++;
		spin_unlock_irqrestore(&packet->qp->r_lock, flags);
		rcu_read_unlock();
	}
	return;
unlock_drop:
	rcu_read_unlock();
drop:
	ibp->rvp.n_pkt_drops++;
}

/**
 * hfi1_ib_rcv - process an incoming packet
 * @packet: data packet information
 *
 * This is called to process an incoming packet at interrupt level.
 */
void hfi1_ib_rcv(struct hfi1_packet *packet)
{
	struct hfi1_ctxtdata *rcd = packet->rcd;

	trace_input_ibhdr(rcd->dd, packet, !!(rhf_dc_info(packet->rhf)));
	hfi1_handle_packet(packet, hfi1_check_mcast(packet->dlid));
}

void hfi1_16B_rcv(struct hfi1_packet *packet)
{
	struct hfi1_ctxtdata *rcd = packet->rcd;

	trace_input_ibhdr(rcd->dd, packet, false);
	hfi1_handle_packet(packet, hfi1_check_mcast(packet->dlid));
}

/*
 * This is called from a timer to check for QPs
 * which need kernel memory in order to send a packet.
 */
static void mem_timer(struct timer_list *t)
{
	struct hfi1_ibdev *dev = from_timer(dev, t, mem_timer);
	struct list_head *list = &dev->memwait;
	struct rvt_qp *qp = NULL;
	struct iowait *wait;
	unsigned long flags;
	struct hfi1_qp_priv *priv;

	write_seqlock_irqsave(&dev->iowait_lock, flags);
	if (!list_empty(list)) {
		wait = list_first_entry(list, struct iowait, list);
		qp = iowait_to_qp(wait);
		priv = qp->priv;
		list_del_init(&priv->s_iowait.list);
		priv->s_iowait.lock = NULL;
		/* refcount held until actual wake up */
		if (!list_empty(list))
			mod_timer(&dev->mem_timer, jiffies + 1);
	}
	write_sequnlock_irqrestore(&dev->iowait_lock, flags);

	if (qp)
		hfi1_qp_wakeup(qp, RVT_S_WAIT_KMEM);
}

/*
 * This is called with progress side lock held.
 */
/* New API */
static void verbs_sdma_complete(
	struct sdma_txreq *cookie,
	int status)
{
	struct verbs_txreq *tx =
		container_of(cookie, struct verbs_txreq, txreq);
	struct rvt_qp *qp = tx->qp;

	spin_lock(&qp->s_lock);
	if (tx->wqe) {
		rvt_send_complete(qp, tx->wqe, IB_WC_SUCCESS);
	} else if (qp->ibqp.qp_type == IB_QPT_RC) {
		struct hfi1_opa_header *hdr;

		hdr = &tx->phdr.hdr;
		if (unlikely(status == SDMA_TXREQ_S_ABORTED))
			hfi1_rc_verbs_aborted(qp, hdr);
		hfi1_rc_send_complete(qp, hdr);
	}
	spin_unlock(&qp->s_lock);

	hfi1_put_txreq(tx);
}

void hfi1_wait_kmem(struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct ib_qp *ibqp = &qp->ibqp;
	struct ib_device *ibdev = ibqp->device;
	struct hfi1_ibdev *dev = to_idev(ibdev);

	if (list_empty(&priv->s_iowait.list)) {
		if (list_empty(&dev->memwait))
			mod_timer(&dev->mem_timer, jiffies + 1);
		qp->s_flags |= RVT_S_WAIT_KMEM;
		list_add_tail(&priv->s_iowait.list, &dev->memwait);
		priv->s_iowait.lock = &dev->iowait_lock;
		trace_hfi1_qpsleep(qp, RVT_S_WAIT_KMEM);
		rvt_get_qp(qp);
	}
}

static int wait_kmem(struct hfi1_ibdev *dev,
		     struct rvt_qp *qp,
		     struct hfi1_pkt_state *ps)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&qp->s_lock, flags);
	if (ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK) {
		write_seqlock(&dev->iowait_lock);
		list_add_tail(&ps->s_txreq->txreq.list,
			      &ps->wait->tx_head);
		hfi1_wait_kmem(qp);
		write_sequnlock(&dev->iowait_lock);
		hfi1_qp_unbusy(qp, ps->wait);
		ret = -EBUSY;
	}
	spin_unlock_irqrestore(&qp->s_lock, flags);

	return ret;
}

/*
 * This routine calls txadds for each sg entry.
 *
 * Add failures will revert the sge cursor
 */
static noinline int build_verbs_ulp_payload(
	struct sdma_engine *sde,
	u32 length,
	struct verbs_txreq *tx)
{
	struct rvt_sge_state *ss = tx->ss;
	struct rvt_sge *sg_list = ss->sg_list;
	struct rvt_sge sge = ss->sge;
	u8 num_sge = ss->num_sge;
	u32 len;
	int ret = 0;

	while (length) {
		len = rvt_get_sge_length(&ss->sge, length);
		WARN_ON_ONCE(len == 0);
		ret = sdma_txadd_kvaddr(
			sde->dd,
			&tx->txreq,
			ss->sge.vaddr,
			len);
		if (ret)
			goto bail_txadd;
		rvt_update_sge(ss, len, false);
		length -= len;
	}
	return ret;
bail_txadd:
	/* unwind cursor */
	ss->sge = sge;
	ss->num_sge = num_sge;
	ss->sg_list = sg_list;
	return ret;
}

/**
 * update_tx_opstats - record stats by opcode
 * @qp; the qp
 * @ps: transmit packet state
 * @plen: the plen in dwords
 *
 * This is a routine to record the tx opstats after a
 * packet has been presented to the egress mechanism.
 */
static void update_tx_opstats(struct rvt_qp *qp, struct hfi1_pkt_state *ps,
			      u32 plen)
{
#ifdef CONFIG_DEBUG_FS
	struct hfi1_devdata *dd = dd_from_ibdev(qp->ibqp.device);
	struct hfi1_opcode_stats_perctx *s = get_cpu_ptr(dd->tx_opstats);

	inc_opstats(plen * 4, &s->stats[ps->opcode]);
	put_cpu_ptr(s);
#endif
}

/*
 * Build the number of DMA descriptors needed to send length bytes of data.
 *
 * NOTE: DMA mapping is held in the tx until completed in the ring or
 *       the tx desc is freed without having been submitted to the ring
 *
 * This routine ensures all the helper routine calls succeed.
 */
/* New API */
static int build_verbs_tx_desc(
	struct sdma_engine *sde,
	u32 length,
	struct verbs_txreq *tx,
	struct hfi1_ahg_info *ahg_info,
	u64 pbc)
{
	int ret = 0;
	struct hfi1_sdma_header *phdr = &tx->phdr;
	u16 hdrbytes = (tx->hdr_dwords + sizeof(pbc) / 4) << 2;
	u8 extra_bytes = 0;

	if (tx->phdr.hdr.hdr_type) {
		/*
		 * hdrbytes accounts for PBC. Need to subtract 8 bytes
		 * before calculating padding.
		 */
		extra_bytes = hfi1_get_16b_padding(hdrbytes - 8, length) +
			      (SIZE_OF_CRC << 2) + SIZE_OF_LT;
	}
	if (!ahg_info->ahgcount) {
		ret = sdma_txinit_ahg(
			&tx->txreq,
			ahg_info->tx_flags,
			hdrbytes + length +
			extra_bytes,
			ahg_info->ahgidx,
			0,
			NULL,
			0,
			verbs_sdma_complete);
		if (ret)
			goto bail_txadd;
		phdr->pbc = cpu_to_le64(pbc);
		ret = sdma_txadd_kvaddr(
			sde->dd,
			&tx->txreq,
			phdr,
			hdrbytes);
		if (ret)
			goto bail_txadd;
	} else {
		ret = sdma_txinit_ahg(
			&tx->txreq,
			ahg_info->tx_flags,
			length,
			ahg_info->ahgidx,
			ahg_info->ahgcount,
			ahg_info->ahgdesc,
			hdrbytes,
			verbs_sdma_complete);
		if (ret)
			goto bail_txadd;
	}
	/* add the ulp payload - if any. tx->ss can be NULL for acks */
	if (tx->ss) {
		ret = build_verbs_ulp_payload(sde, length, tx);
		if (ret)
			goto bail_txadd;
	}

	/* add icrc, lt byte, and padding to flit */
	if (extra_bytes)
		ret = sdma_txadd_kvaddr(sde->dd, &tx->txreq,
					(void *)trail_buf, extra_bytes);

bail_txadd:
	return ret;
}

static u64 update_hcrc(u8 opcode, u64 pbc)
{
	if ((opcode & IB_OPCODE_TID_RDMA) == IB_OPCODE_TID_RDMA) {
		pbc &= ~PBC_INSERT_HCRC_SMASK;
		pbc |= (u64)PBC_IHCRC_LKDETH << PBC_INSERT_HCRC_SHIFT;
	}
	return pbc;
}

int hfi1_verbs_send_dma(struct rvt_qp *qp, struct hfi1_pkt_state *ps,
			u64 pbc)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct hfi1_ahg_info *ahg_info = priv->s_ahg;
	u32 hdrwords = ps->s_txreq->hdr_dwords;
	u32 len = ps->s_txreq->s_cur_size;
	u32 plen;
	struct hfi1_ibdev *dev = ps->dev;
	struct hfi1_pportdata *ppd = ps->ppd;
	struct verbs_txreq *tx;
	u8 sc5 = priv->s_sc;
	int ret;
	u32 dwords;

	if (ps->s_txreq->phdr.hdr.hdr_type) {
		u8 extra_bytes = hfi1_get_16b_padding((hdrwords << 2), len);

		dwords = (len + extra_bytes + (SIZE_OF_CRC << 2) +
			  SIZE_OF_LT) >> 2;
	} else {
		dwords = (len + 3) >> 2;
	}
	plen = hdrwords + dwords + sizeof(pbc) / 4;

	tx = ps->s_txreq;
	if (!sdma_txreq_built(&tx->txreq)) {
		if (likely(pbc == 0)) {
			u32 vl = sc_to_vlt(dd_from_ibdev(qp->ibqp.device), sc5);

			/* No vl15 here */
			/* set PBC_DC_INFO bit (aka SC[4]) in pbc */
			if (ps->s_txreq->phdr.hdr.hdr_type)
				pbc |= PBC_PACKET_BYPASS |
				       PBC_INSERT_BYPASS_ICRC;
			else
				pbc |= (ib_is_sc5(sc5) << PBC_DC_INFO_SHIFT);

			if (unlikely(hfi1_dbg_should_fault_tx(qp, ps->opcode)))
				pbc = hfi1_fault_tx(qp, ps->opcode, pbc);
			pbc = create_pbc(ppd,
					 pbc,
					 qp->srate_mbps,
					 vl,
					 plen);

			/* Update HCRC based on packet opcode */
			pbc = update_hcrc(ps->opcode, pbc);
		}
		tx->wqe = qp->s_wqe;
		ret = build_verbs_tx_desc(tx->sde, len, tx, ahg_info, pbc);
		if (unlikely(ret))
			goto bail_build;
	}
	ret =  sdma_send_txreq(tx->sde, ps->wait, &tx->txreq, ps->pkts_sent);
	if (unlikely(ret < 0)) {
		if (ret == -ECOMM)
			goto bail_ecomm;
		return ret;
	}

	update_tx_opstats(qp, ps, plen);
	trace_sdma_output_ibhdr(dd_from_ibdev(qp->ibqp.device),
				&ps->s_txreq->phdr.hdr, ib_is_sc5(sc5));
	return ret;

bail_ecomm:
	/* The current one got "sent" */
	return 0;
bail_build:
	ret = wait_kmem(dev, qp, ps);
	if (!ret) {
		/* free txreq - bad state */
		hfi1_put_txreq(ps->s_txreq);
		ps->s_txreq = NULL;
	}
	return ret;
}

/*
 * If we are now in the error state, return zero to flush the
 * send work request.
 */
static int pio_wait(struct rvt_qp *qp,
		    struct send_context *sc,
		    struct hfi1_pkt_state *ps,
		    u32 flag)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct hfi1_devdata *dd = sc->dd;
	unsigned long flags;
	int ret = 0;

	/*
	 * Note that as soon as want_buffer() is called and
	 * possibly before it returns, sc_piobufavail()
	 * could be called. Therefore, put QP on the I/O wait list before
	 * enabling the PIO avail interrupt.
	 */
	spin_lock_irqsave(&qp->s_lock, flags);
	if (ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK) {
		write_seqlock(&sc->waitlock);
		list_add_tail(&ps->s_txreq->txreq.list,
			      &ps->wait->tx_head);
		if (list_empty(&priv->s_iowait.list)) {
			struct hfi1_ibdev *dev = &dd->verbs_dev;
			int was_empty;

			dev->n_piowait += !!(flag & RVT_S_WAIT_PIO);
			dev->n_piodrain += !!(flag & HFI1_S_WAIT_PIO_DRAIN);
			qp->s_flags |= flag;
			was_empty = list_empty(&sc->piowait);
			iowait_get_priority(&priv->s_iowait);
			iowait_queue(ps->pkts_sent, &priv->s_iowait,
				     &sc->piowait);
			priv->s_iowait.lock = &sc->waitlock;
			trace_hfi1_qpsleep(qp, RVT_S_WAIT_PIO);
			rvt_get_qp(qp);
			/* counting: only call wantpiobuf_intr if first user */
			if (was_empty)
				hfi1_sc_wantpiobuf_intr(sc, 1);
		}
		write_sequnlock(&sc->waitlock);
		hfi1_qp_unbusy(qp, ps->wait);
		ret = -EBUSY;
	}
	spin_unlock_irqrestore(&qp->s_lock, flags);
	return ret;
}

static void verbs_pio_complete(void *arg, int code)
{
	struct rvt_qp *qp = (struct rvt_qp *)arg;
	struct hfi1_qp_priv *priv = qp->priv;

	if (iowait_pio_dec(&priv->s_iowait))
		iowait_drain_wakeup(&priv->s_iowait);
}

int hfi1_verbs_send_pio(struct rvt_qp *qp, struct hfi1_pkt_state *ps,
			u64 pbc)
{
	struct hfi1_qp_priv *priv = qp->priv;
	u32 hdrwords = ps->s_txreq->hdr_dwords;
	struct rvt_sge_state *ss = ps->s_txreq->ss;
	u32 len = ps->s_txreq->s_cur_size;
	u32 dwords;
	u32 plen;
	struct hfi1_pportdata *ppd = ps->ppd;
	u32 *hdr;
	u8 sc5;
	unsigned long flags = 0;
	struct send_context *sc;
	struct pio_buf *pbuf;
	int wc_status = IB_WC_SUCCESS;
	int ret = 0;
	pio_release_cb cb = NULL;
	u8 extra_bytes = 0;

	if (ps->s_txreq->phdr.hdr.hdr_type) {
		u8 pad_size = hfi1_get_16b_padding((hdrwords << 2), len);

		extra_bytes = pad_size + (SIZE_OF_CRC << 2) + SIZE_OF_LT;
		dwords = (len + extra_bytes) >> 2;
		hdr = (u32 *)&ps->s_txreq->phdr.hdr.opah;
	} else {
		dwords = (len + 3) >> 2;
		hdr = (u32 *)&ps->s_txreq->phdr.hdr.ibh;
	}
	plen = hdrwords + dwords + sizeof(pbc) / 4;

	/* only RC/UC use complete */
	switch (qp->ibqp.qp_type) {
	case IB_QPT_RC:
	case IB_QPT_UC:
		cb = verbs_pio_complete;
		break;
	default:
		break;
	}

	/* vl15 special case taken care of in ud.c */
	sc5 = priv->s_sc;
	sc = ps->s_txreq->psc;

	if (likely(pbc == 0)) {
		u8 vl = sc_to_vlt(dd_from_ibdev(qp->ibqp.device), sc5);

		/* set PBC_DC_INFO bit (aka SC[4]) in pbc */
		if (ps->s_txreq->phdr.hdr.hdr_type)
			pbc |= PBC_PACKET_BYPASS | PBC_INSERT_BYPASS_ICRC;
		else
			pbc |= (ib_is_sc5(sc5) << PBC_DC_INFO_SHIFT);

		if (unlikely(hfi1_dbg_should_fault_tx(qp, ps->opcode)))
			pbc = hfi1_fault_tx(qp, ps->opcode, pbc);
		pbc = create_pbc(ppd, pbc, qp->srate_mbps, vl, plen);

		/* Update HCRC based on packet opcode */
		pbc = update_hcrc(ps->opcode, pbc);
	}
	if (cb)
		iowait_pio_inc(&priv->s_iowait);
	pbuf = sc_buffer_alloc(sc, plen, cb, qp);
	if (unlikely(IS_ERR_OR_NULL(pbuf))) {
		if (cb)
			verbs_pio_complete(qp, 0);
		if (IS_ERR(pbuf)) {
			/*
			 * If we have filled the PIO buffers to capacity and are
			 * not in an active state this request is not going to
			 * go out to so just complete it with an error or else a
			 * ULP or the core may be stuck waiting.
			 */
			hfi1_cdbg(
				PIO,
				"alloc failed. state not active, completing");
			wc_status = IB_WC_GENERAL_ERR;
			goto pio_bail;
		} else {
			/*
			 * This is a normal occurrence. The PIO buffs are full
			 * up but we are still happily sending, well we could be
			 * so lets continue to queue the request.
			 */
			hfi1_cdbg(PIO, "alloc failed. state active, queuing");
			ret = pio_wait(qp, sc, ps, RVT_S_WAIT_PIO);
			if (!ret)
				/* txreq not queued - free */
				goto bail;
			/* tx consumed in wait */
			return ret;
		}
	}

	if (dwords == 0) {
		pio_copy(ppd->dd, pbuf, pbc, hdr, hdrwords);
	} else {
		seg_pio_copy_start(pbuf, pbc,
				   hdr, hdrwords * 4);
		if (ss) {
			while (len) {
				void *addr = ss->sge.vaddr;
				u32 slen = rvt_get_sge_length(&ss->sge, len);

				rvt_update_sge(ss, slen, false);
				seg_pio_copy_mid(pbuf, addr, slen);
				len -= slen;
			}
		}
		/* add icrc, lt byte, and padding to flit */
		if (extra_bytes)
			seg_pio_copy_mid(pbuf, trail_buf, extra_bytes);

		seg_pio_copy_end(pbuf);
	}

	update_tx_opstats(qp, ps, plen);
	trace_pio_output_ibhdr(dd_from_ibdev(qp->ibqp.device),
			       &ps->s_txreq->phdr.hdr, ib_is_sc5(sc5));

pio_bail:
	spin_lock_irqsave(&qp->s_lock, flags);
	if (qp->s_wqe) {
		rvt_send_complete(qp, qp->s_wqe, wc_status);
	} else if (qp->ibqp.qp_type == IB_QPT_RC) {
		if (unlikely(wc_status == IB_WC_GENERAL_ERR))
			hfi1_rc_verbs_aborted(qp, &ps->s_txreq->phdr.hdr);
		hfi1_rc_send_complete(qp, &ps->s_txreq->phdr.hdr);
	}
	spin_unlock_irqrestore(&qp->s_lock, flags);

	ret = 0;

bail:
	hfi1_put_txreq(ps->s_txreq);
	return ret;
}

/*
 * egress_pkey_matches_entry - return 1 if the pkey matches ent (ent
 * being an entry from the partition key table), return 0
 * otherwise. Use the matching criteria for egress partition keys
 * specified in the OPAv1 spec., section 9.1l.7.
 */
static inline int egress_pkey_matches_entry(u16 pkey, u16 ent)
{
	u16 mkey = pkey & PKEY_LOW_15_MASK;
	u16 mentry = ent & PKEY_LOW_15_MASK;

	if (mkey == mentry) {
		/*
		 * If pkey[15] is set (full partition member),
		 * is bit 15 in the corresponding table element
		 * clear (limited member)?
		 */
		if (pkey & PKEY_MEMBER_MASK)
			return !!(ent & PKEY_MEMBER_MASK);
		return 1;
	}
	return 0;
}

/**
 * egress_pkey_check - check P_KEY of a packet
 * @ppd:  Physical IB port data
 * @slid: SLID for packet
 * @bkey: PKEY for header
 * @sc5:  SC for packet
 * @s_pkey_index: It will be used for look up optimization for kernel contexts
 * only. If it is negative value, then it means user contexts is calling this
 * function.
 *
 * It checks if hdr's pkey is valid.
 *
 * Return: 0 on success, otherwise, 1
 */
int egress_pkey_check(struct hfi1_pportdata *ppd, u32 slid, u16 pkey,
		      u8 sc5, int8_t s_pkey_index)
{
	struct hfi1_devdata *dd;
	int i;
	int is_user_ctxt_mechanism = (s_pkey_index < 0);

	if (!(ppd->part_enforce & HFI1_PART_ENFORCE_OUT))
		return 0;

	/* If SC15, pkey[0:14] must be 0x7fff */
	if ((sc5 == 0xf) && ((pkey & PKEY_LOW_15_MASK) != PKEY_LOW_15_MASK))
		goto bad;

	/* Is the pkey = 0x0, or 0x8000? */
	if ((pkey & PKEY_LOW_15_MASK) == 0)
		goto bad;

	/*
	 * For the kernel contexts only, if a qp is passed into the function,
	 * the most likely matching pkey has index qp->s_pkey_index
	 */
	if (!is_user_ctxt_mechanism &&
	    egress_pkey_matches_entry(pkey, ppd->pkeys[s_pkey_index])) {
		return 0;
	}

	for (i = 0; i < MAX_PKEY_VALUES; i++) {
		if (egress_pkey_matches_entry(pkey, ppd->pkeys[i]))
			return 0;
	}
bad:
	/*
	 * For the user-context mechanism, the P_KEY check would only happen
	 * once per SDMA request, not once per packet.  Therefore, there's no
	 * need to increment the counter for the user-context mechanism.
	 */
	if (!is_user_ctxt_mechanism) {
		incr_cntr64(&ppd->port_xmit_constraint_errors);
		dd = ppd->dd;
		if (!(dd->err_info_xmit_constraint.status &
		      OPA_EI_STATUS_SMASK)) {
			dd->err_info_xmit_constraint.status |=
				OPA_EI_STATUS_SMASK;
			dd->err_info_xmit_constraint.slid = slid;
			dd->err_info_xmit_constraint.pkey = pkey;
		}
	}
	return 1;
}

/**
 * get_send_routine - choose an egress routine
 *
 * Choose an egress routine based on QP type
 * and size
 */
static inline send_routine get_send_routine(struct rvt_qp *qp,
					    struct hfi1_pkt_state *ps)
{
	struct hfi1_devdata *dd = dd_from_ibdev(qp->ibqp.device);
	struct hfi1_qp_priv *priv = qp->priv;
	struct verbs_txreq *tx = ps->s_txreq;

	if (unlikely(!(dd->flags & HFI1_HAS_SEND_DMA)))
		return dd->process_pio_send;
	switch (qp->ibqp.qp_type) {
	case IB_QPT_SMI:
		return dd->process_pio_send;
	case IB_QPT_GSI:
	case IB_QPT_UD:
		break;
	case IB_QPT_UC:
	case IB_QPT_RC:
		priv->s_running_pkt_size =
			(tx->s_cur_size + priv->s_running_pkt_size) / 2;
		if (piothreshold &&
		    priv->s_running_pkt_size <= min(piothreshold, qp->pmtu) &&
		    (BIT(ps->opcode & OPMASK) & pio_opmask[ps->opcode >> 5]) &&
		    iowait_sdma_pending(&priv->s_iowait) == 0 &&
		    !sdma_txreq_built(&tx->txreq))
			return dd->process_pio_send;
		break;
	default:
		break;
	}
	return dd->process_dma_send;
}

/**
 * hfi1_verbs_send - send a packet
 * @qp: the QP to send on
 * @ps: the state of the packet to send
 *
 * Return zero if packet is sent or queued OK.
 * Return non-zero and clear qp->s_flags RVT_S_BUSY otherwise.
 */
int hfi1_verbs_send(struct rvt_qp *qp, struct hfi1_pkt_state *ps)
{
	struct hfi1_devdata *dd = dd_from_ibdev(qp->ibqp.device);
	struct hfi1_qp_priv *priv = qp->priv;
	struct ib_other_headers *ohdr = NULL;
	send_routine sr;
	int ret;
	u16 pkey;
	u32 slid;
	u8 l4 = 0;

	/* locate the pkey within the headers */
	if (ps->s_txreq->phdr.hdr.hdr_type) {
		struct hfi1_16b_header *hdr = &ps->s_txreq->phdr.hdr.opah;

		l4 = hfi1_16B_get_l4(hdr);
		if (l4 == OPA_16B_L4_IB_LOCAL)
			ohdr = &hdr->u.oth;
		else if (l4 == OPA_16B_L4_IB_GLOBAL)
			ohdr = &hdr->u.l.oth;

		slid = hfi1_16B_get_slid(hdr);
		pkey = hfi1_16B_get_pkey(hdr);
	} else {
		struct ib_header *hdr = &ps->s_txreq->phdr.hdr.ibh;
		u8 lnh = ib_get_lnh(hdr);

		if (lnh == HFI1_LRH_GRH)
			ohdr = &hdr->u.l.oth;
		else
			ohdr = &hdr->u.oth;
		slid = ib_get_slid(hdr);
		pkey = ib_bth_get_pkey(ohdr);
	}

	if (likely(l4 != OPA_16B_L4_FM))
		ps->opcode = ib_bth_get_opcode(ohdr);
	else
		ps->opcode = IB_OPCODE_UD_SEND_ONLY;

	sr = get_send_routine(qp, ps);
	ret = egress_pkey_check(dd->pport, slid, pkey,
				priv->s_sc, qp->s_pkey_index);
	if (unlikely(ret)) {
		/*
		 * The value we are returning here does not get propagated to
		 * the verbs caller. Thus we need to complete the request with
		 * error otherwise the caller could be sitting waiting on the
		 * completion event. Only do this for PIO. SDMA has its own
		 * mechanism for handling the errors. So for SDMA we can just
		 * return.
		 */
		if (sr == dd->process_pio_send) {
			unsigned long flags;

			hfi1_cdbg(PIO, "%s() Failed. Completing with err",
				  __func__);
			spin_lock_irqsave(&qp->s_lock, flags);
			rvt_send_complete(qp, qp->s_wqe, IB_WC_GENERAL_ERR);
			spin_unlock_irqrestore(&qp->s_lock, flags);
		}
		return -EINVAL;
	}
	if (sr == dd->process_dma_send && iowait_pio_pending(&priv->s_iowait))
		return pio_wait(qp,
				ps->s_txreq->psc,
				ps,
				HFI1_S_WAIT_PIO_DRAIN);
	return sr(qp, ps, 0);
}

/**
 * hfi1_fill_device_attr - Fill in rvt dev info device attributes.
 * @dd: the device data structure
 */
static void hfi1_fill_device_attr(struct hfi1_devdata *dd)
{
	struct rvt_dev_info *rdi = &dd->verbs_dev.rdi;
	u32 ver = dd->dc8051_ver;

	memset(&rdi->dparms.props, 0, sizeof(rdi->dparms.props));

	rdi->dparms.props.fw_ver = ((u64)(dc8051_ver_maj(ver)) << 32) |
		((u64)(dc8051_ver_min(ver)) << 16) |
		(u64)dc8051_ver_patch(ver);

	rdi->dparms.props.device_cap_flags = IB_DEVICE_BAD_PKEY_CNTR |
			IB_DEVICE_BAD_QKEY_CNTR | IB_DEVICE_SHUTDOWN_PORT |
			IB_DEVICE_SYS_IMAGE_GUID | IB_DEVICE_RC_RNR_NAK_GEN |
			IB_DEVICE_PORT_ACTIVE_EVENT | IB_DEVICE_SRQ_RESIZE |
			IB_DEVICE_MEM_MGT_EXTENSIONS |
			IB_DEVICE_RDMA_NETDEV_OPA_VNIC;
	rdi->dparms.props.page_size_cap = PAGE_SIZE;
	rdi->dparms.props.vendor_id = dd->oui1 << 16 | dd->oui2 << 8 | dd->oui3;
	rdi->dparms.props.vendor_part_id = dd->pcidev->device;
	rdi->dparms.props.hw_ver = dd->minrev;
	rdi->dparms.props.sys_image_guid = ib_hfi1_sys_image_guid;
	rdi->dparms.props.max_mr_size = U64_MAX;
	rdi->dparms.props.max_fast_reg_page_list_len = UINT_MAX;
	rdi->dparms.props.max_qp = hfi1_max_qps;
	rdi->dparms.props.max_qp_wr =
		(hfi1_max_qp_wrs >= HFI1_QP_WQE_INVALID ?
		 HFI1_QP_WQE_INVALID - 1 : hfi1_max_qp_wrs);
	rdi->dparms.props.max_send_sge = hfi1_max_sges;
	rdi->dparms.props.max_recv_sge = hfi1_max_sges;
	rdi->dparms.props.max_sge_rd = hfi1_max_sges;
	rdi->dparms.props.max_cq = hfi1_max_cqs;
	rdi->dparms.props.max_ah = hfi1_max_ahs;
	rdi->dparms.props.max_cqe = hfi1_max_cqes;
	rdi->dparms.props.max_map_per_fmr = 32767;
	rdi->dparms.props.max_pd = hfi1_max_pds;
	rdi->dparms.props.max_qp_rd_atom = HFI1_MAX_RDMA_ATOMIC;
	rdi->dparms.props.max_qp_init_rd_atom = 255;
	rdi->dparms.props.max_srq = hfi1_max_srqs;
	rdi->dparms.props.max_srq_wr = hfi1_max_srq_wrs;
	rdi->dparms.props.max_srq_sge = hfi1_max_srq_sges;
	rdi->dparms.props.atomic_cap = IB_ATOMIC_GLOB;
	rdi->dparms.props.max_pkeys = hfi1_get_npkeys(dd);
	rdi->dparms.props.max_mcast_grp = hfi1_max_mcast_grps;
	rdi->dparms.props.max_mcast_qp_attach = hfi1_max_mcast_qp_attached;
	rdi->dparms.props.max_total_mcast_qp_attach =
					rdi->dparms.props.max_mcast_qp_attach *
					rdi->dparms.props.max_mcast_grp;
}

static inline u16 opa_speed_to_ib(u16 in)
{
	u16 out = 0;

	if (in & OPA_LINK_SPEED_25G)
		out |= IB_SPEED_EDR;
	if (in & OPA_LINK_SPEED_12_5G)
		out |= IB_SPEED_FDR;

	return out;
}

/*
 * Convert a single OPA link width (no multiple flags) to an IB value.
 * A zero OPA link width means link down, which means the IB width value
 * is a don't care.
 */
static inline u16 opa_width_to_ib(u16 in)
{
	switch (in) {
	case OPA_LINK_WIDTH_1X:
	/* map 2x and 3x to 1x as they don't exist in IB */
	case OPA_LINK_WIDTH_2X:
	case OPA_LINK_WIDTH_3X:
		return IB_WIDTH_1X;
	default: /* link down or unknown, return our largest width */
	case OPA_LINK_WIDTH_4X:
		return IB_WIDTH_4X;
	}
}

static int query_port(struct rvt_dev_info *rdi, u8 port_num,
		      struct ib_port_attr *props)
{
	struct hfi1_ibdev *verbs_dev = dev_from_rdi(rdi);
	struct hfi1_devdata *dd = dd_from_dev(verbs_dev);
	struct hfi1_pportdata *ppd = &dd->pport[port_num - 1];
	u32 lid = ppd->lid;

	/* props being zeroed by the caller, avoid zeroing it here */
	props->lid = lid ? lid : 0;
	props->lmc = ppd->lmc;
	/* OPA logical states match IB logical states */
	props->state = driver_lstate(ppd);
	props->phys_state = driver_pstate(ppd);
	props->gid_tbl_len = HFI1_GUIDS_PER_PORT;
	props->active_width = (u8)opa_width_to_ib(ppd->link_width_active);
	/* see rate_show() in ib core/sysfs.c */
	props->active_speed = (u8)opa_speed_to_ib(ppd->link_speed_active);
	props->max_vl_num = ppd->vls_supported;

	/* Once we are a "first class" citizen and have added the OPA MTUs to
	 * the core we can advertise the larger MTU enum to the ULPs, for now
	 * advertise only 4K.
	 *
	 * Those applications which are either OPA aware or pass the MTU enum
	 * from the Path Records to us will get the new 8k MTU.  Those that
	 * attempt to process the MTU enum may fail in various ways.
	 */
	props->max_mtu = mtu_to_enum((!valid_ib_mtu(hfi1_max_mtu) ?
				      4096 : hfi1_max_mtu), IB_MTU_4096);
	props->active_mtu = !valid_ib_mtu(ppd->ibmtu) ? props->max_mtu :
		mtu_to_enum(ppd->ibmtu, IB_MTU_4096);

	return 0;
}

static int modify_device(struct ib_device *device,
			 int device_modify_mask,
			 struct ib_device_modify *device_modify)
{
	struct hfi1_devdata *dd = dd_from_ibdev(device);
	unsigned i;
	int ret;

	if (device_modify_mask & ~(IB_DEVICE_MODIFY_SYS_IMAGE_GUID |
				   IB_DEVICE_MODIFY_NODE_DESC)) {
		ret = -EOPNOTSUPP;
		goto bail;
	}

	if (device_modify_mask & IB_DEVICE_MODIFY_NODE_DESC) {
		memcpy(device->node_desc, device_modify->node_desc,
		       IB_DEVICE_NODE_DESC_MAX);
		for (i = 0; i < dd->num_pports; i++) {
			struct hfi1_ibport *ibp = &dd->pport[i].ibport_data;

			hfi1_node_desc_chg(ibp);
		}
	}

	if (device_modify_mask & IB_DEVICE_MODIFY_SYS_IMAGE_GUID) {
		ib_hfi1_sys_image_guid =
			cpu_to_be64(device_modify->sys_image_guid);
		for (i = 0; i < dd->num_pports; i++) {
			struct hfi1_ibport *ibp = &dd->pport[i].ibport_data;

			hfi1_sys_guid_chg(ibp);
		}
	}

	ret = 0;

bail:
	return ret;
}

static int shut_down_port(struct rvt_dev_info *rdi, u8 port_num)
{
	struct hfi1_ibdev *verbs_dev = dev_from_rdi(rdi);
	struct hfi1_devdata *dd = dd_from_dev(verbs_dev);
	struct hfi1_pportdata *ppd = &dd->pport[port_num - 1];
	int ret;

	set_link_down_reason(ppd, OPA_LINKDOWN_REASON_UNKNOWN, 0,
			     OPA_LINKDOWN_REASON_UNKNOWN);
	ret = set_link_state(ppd, HLS_DN_DOWNDEF);
	return ret;
}

static int hfi1_get_guid_be(struct rvt_dev_info *rdi, struct rvt_ibport *rvp,
			    int guid_index, __be64 *guid)
{
	struct hfi1_ibport *ibp = container_of(rvp, struct hfi1_ibport, rvp);

	if (guid_index >= HFI1_GUIDS_PER_PORT)
		return -EINVAL;

	*guid = get_sguid(ibp, guid_index);
	return 0;
}

/*
 * convert ah port,sl to sc
 */
u8 ah_to_sc(struct ib_device *ibdev, struct rdma_ah_attr *ah)
{
	struct hfi1_ibport *ibp = to_iport(ibdev, rdma_ah_get_port_num(ah));

	return ibp->sl_to_sc[rdma_ah_get_sl(ah)];
}

static int hfi1_check_ah(struct ib_device *ibdev, struct rdma_ah_attr *ah_attr)
{
	struct hfi1_ibport *ibp;
	struct hfi1_pportdata *ppd;
	struct hfi1_devdata *dd;
	u8 sc5;
	u8 sl;

	if (hfi1_check_mcast(rdma_ah_get_dlid(ah_attr)) &&
	    !(rdma_ah_get_ah_flags(ah_attr) & IB_AH_GRH))
		return -EINVAL;

	/* test the mapping for validity */
	ibp = to_iport(ibdev, rdma_ah_get_port_num(ah_attr));
	ppd = ppd_from_ibp(ibp);
	dd = dd_from_ppd(ppd);

	sl = rdma_ah_get_sl(ah_attr);
	if (sl >= ARRAY_SIZE(ibp->sl_to_sc))
		return -EINVAL;

	sc5 = ibp->sl_to_sc[sl];
	if (sc_to_vlt(dd, sc5) > num_vls && sc_to_vlt(dd, sc5) != 0xf)
		return -EINVAL;
	return 0;
}

static void hfi1_notify_new_ah(struct ib_device *ibdev,
			       struct rdma_ah_attr *ah_attr,
			       struct rvt_ah *ah)
{
	struct hfi1_ibport *ibp;
	struct hfi1_pportdata *ppd;
	struct hfi1_devdata *dd;
	u8 sc5;
	struct rdma_ah_attr *attr = &ah->attr;

	/*
	 * Do not trust reading anything from rvt_ah at this point as it is not
	 * done being setup. We can however modify things which we need to set.
	 */

	ibp = to_iport(ibdev, rdma_ah_get_port_num(ah_attr));
	ppd = ppd_from_ibp(ibp);
	sc5 = ibp->sl_to_sc[rdma_ah_get_sl(&ah->attr)];
	hfi1_update_ah_attr(ibdev, attr);
	hfi1_make_opa_lid(attr);
	dd = dd_from_ppd(ppd);
	ah->vl = sc_to_vlt(dd, sc5);
	if (ah->vl < num_vls || ah->vl == 15)
		ah->log_pmtu = ilog2(dd->vld[ah->vl].mtu);
}

/**
 * hfi1_get_npkeys - return the size of the PKEY table for context 0
 * @dd: the hfi1_ib device
 */
unsigned hfi1_get_npkeys(struct hfi1_devdata *dd)
{
	return ARRAY_SIZE(dd->pport[0].pkeys);
}

static void init_ibport(struct hfi1_pportdata *ppd)
{
	struct hfi1_ibport *ibp = &ppd->ibport_data;
	size_t sz = ARRAY_SIZE(ibp->sl_to_sc);
	int i;

	for (i = 0; i < sz; i++) {
		ibp->sl_to_sc[i] = i;
		ibp->sc_to_sl[i] = i;
	}

	for (i = 0; i < RVT_MAX_TRAP_LISTS ; i++)
		INIT_LIST_HEAD(&ibp->rvp.trap_lists[i].list);
	timer_setup(&ibp->rvp.trap_timer, hfi1_handle_trap_timer, 0);

	spin_lock_init(&ibp->rvp.lock);
	/* Set the prefix to the default value (see ch. 4.1.1) */
	ibp->rvp.gid_prefix = IB_DEFAULT_GID_PREFIX;
	ibp->rvp.sm_lid = 0;
	/*
	 * Below should only set bits defined in OPA PortInfo.CapabilityMask
	 * and PortInfo.CapabilityMask3
	 */
	ibp->rvp.port_cap_flags = IB_PORT_AUTO_MIGR_SUP |
		IB_PORT_CAP_MASK_NOTICE_SUP;
	ibp->rvp.port_cap3_flags = OPA_CAP_MASK3_IsSharedSpaceSupported;
	ibp->rvp.pma_counter_select[0] = IB_PMA_PORT_XMIT_DATA;
	ibp->rvp.pma_counter_select[1] = IB_PMA_PORT_RCV_DATA;
	ibp->rvp.pma_counter_select[2] = IB_PMA_PORT_XMIT_PKTS;
	ibp->rvp.pma_counter_select[3] = IB_PMA_PORT_RCV_PKTS;
	ibp->rvp.pma_counter_select[4] = IB_PMA_PORT_XMIT_WAIT;

	RCU_INIT_POINTER(ibp->rvp.qp[0], NULL);
	RCU_INIT_POINTER(ibp->rvp.qp[1], NULL);
}

static void hfi1_get_dev_fw_str(struct ib_device *ibdev, char *str)
{
	struct rvt_dev_info *rdi = ib_to_rvt(ibdev);
	struct hfi1_ibdev *dev = dev_from_rdi(rdi);
	u32 ver = dd_from_dev(dev)->dc8051_ver;

	snprintf(str, IB_FW_VERSION_NAME_MAX, "%u.%u.%u", dc8051_ver_maj(ver),
		 dc8051_ver_min(ver), dc8051_ver_patch(ver));
}

static const char * const driver_cntr_names[] = {
	/* must be element 0*/
	"DRIVER_KernIntr",
	"DRIVER_ErrorIntr",
	"DRIVER_Tx_Errs",
	"DRIVER_Rcv_Errs",
	"DRIVER_HW_Errs",
	"DRIVER_NoPIOBufs",
	"DRIVER_CtxtsOpen",
	"DRIVER_RcvLen_Errs",
	"DRIVER_EgrBufFull",
	"DRIVER_EgrHdrFull"
};

static DEFINE_MUTEX(cntr_names_lock); /* protects the *_cntr_names bufers */
static const char **dev_cntr_names;
static const char **port_cntr_names;
int num_driver_cntrs = ARRAY_SIZE(driver_cntr_names);
static int num_dev_cntrs;
static int num_port_cntrs;
static int cntr_names_initialized;

/*
 * Convert a list of names separated by '\n' into an array of NULL terminated
 * strings. Optionally some entries can be reserved in the array to hold extra
 * external strings.
 */
static int init_cntr_names(const char *names_in,
			   const size_t names_len,
			   int num_extra_names,
			   int *num_cntrs,
			   const char ***cntr_names)
{
	char *names_out, *p, **q;
	int i, n;

	n = 0;
	for (i = 0; i < names_len; i++)
		if (names_in[i] == '\n')
			n++;

	names_out = kmalloc((n + num_extra_names) * sizeof(char *) + names_len,
			    GFP_KERNEL);
	if (!names_out) {
		*num_cntrs = 0;
		*cntr_names = NULL;
		return -ENOMEM;
	}

	p = names_out + (n + num_extra_names) * sizeof(char *);
	memcpy(p, names_in, names_len);

	q = (char **)names_out;
	for (i = 0; i < n; i++) {
		q[i] = p;
		p = strchr(p, '\n');
		*p++ = '\0';
	}

	*num_cntrs = n;
	*cntr_names = (const char **)names_out;
	return 0;
}

static struct rdma_hw_stats *alloc_hw_stats(struct ib_device *ibdev,
					    u8 port_num)
{
	int i, err;

	mutex_lock(&cntr_names_lock);
	if (!cntr_names_initialized) {
		struct hfi1_devdata *dd = dd_from_ibdev(ibdev);

		err = init_cntr_names(dd->cntrnames,
				      dd->cntrnameslen,
				      num_driver_cntrs,
				      &num_dev_cntrs,
				      &dev_cntr_names);
		if (err) {
			mutex_unlock(&cntr_names_lock);
			return NULL;
		}

		for (i = 0; i < num_driver_cntrs; i++)
			dev_cntr_names[num_dev_cntrs + i] =
				driver_cntr_names[i];

		err = init_cntr_names(dd->portcntrnames,
				      dd->portcntrnameslen,
				      0,
				      &num_port_cntrs,
				      &port_cntr_names);
		if (err) {
			kfree(dev_cntr_names);
			dev_cntr_names = NULL;
			mutex_unlock(&cntr_names_lock);
			return NULL;
		}
		cntr_names_initialized = 1;
	}
	mutex_unlock(&cntr_names_lock);

	if (!port_num)
		return rdma_alloc_hw_stats_struct(
				dev_cntr_names,
				num_dev_cntrs + num_driver_cntrs,
				RDMA_HW_STATS_DEFAULT_LIFESPAN);
	else
		return rdma_alloc_hw_stats_struct(
				port_cntr_names,
				num_port_cntrs,
				RDMA_HW_STATS_DEFAULT_LIFESPAN);
}

static u64 hfi1_sps_ints(void)
{
	unsigned long index, flags;
	struct hfi1_devdata *dd;
	u64 sps_ints = 0;

	xa_lock_irqsave(&hfi1_dev_table, flags);
	xa_for_each(&hfi1_dev_table, index, dd) {
		sps_ints += get_all_cpu_total(dd->int_counter);
	}
	xa_unlock_irqrestore(&hfi1_dev_table, flags);
	return sps_ints;
}

static int get_hw_stats(struct ib_device *ibdev, struct rdma_hw_stats *stats,
			u8 port, int index)
{
	u64 *values;
	int count;

	if (!port) {
		u64 *stats = (u64 *)&hfi1_stats;
		int i;

		hfi1_read_cntrs(dd_from_ibdev(ibdev), NULL, &values);
		values[num_dev_cntrs] = hfi1_sps_ints();
		for (i = 1; i < num_driver_cntrs; i++)
			values[num_dev_cntrs + i] = stats[i];
		count = num_dev_cntrs + num_driver_cntrs;
	} else {
		struct hfi1_ibport *ibp = to_iport(ibdev, port);

		hfi1_read_portcntrs(ppd_from_ibp(ibp), NULL, &values);
		count = num_port_cntrs;
	}

	memcpy(stats->value, values, count * sizeof(u64));
	return count;
}

static const struct ib_device_ops hfi1_dev_ops = {
	.alloc_hw_stats = alloc_hw_stats,
	.alloc_rdma_netdev = hfi1_vnic_alloc_rn,
	.get_dev_fw_str = hfi1_get_dev_fw_str,
	.get_hw_stats = get_hw_stats,
	.init_port = hfi1_create_port_files,
	.modify_device = modify_device,
	/* keep process mad in the driver */
	.process_mad = hfi1_process_mad,
};

/**
 * hfi1_register_ib_device - register our device with the infiniband core
 * @dd: the device data structure
 * Return 0 if successful, errno if unsuccessful.
 */
int hfi1_register_ib_device(struct hfi1_devdata *dd)
{
	struct hfi1_ibdev *dev = &dd->verbs_dev;
	struct ib_device *ibdev = &dev->rdi.ibdev;
	struct hfi1_pportdata *ppd = dd->pport;
	struct hfi1_ibport *ibp = &ppd->ibport_data;
	unsigned i;
	int ret;

	for (i = 0; i < dd->num_pports; i++)
		init_ibport(ppd + i);

	/* Only need to initialize non-zero fields. */

	timer_setup(&dev->mem_timer, mem_timer, 0);

	seqlock_init(&dev->iowait_lock);
	seqlock_init(&dev->txwait_lock);
	INIT_LIST_HEAD(&dev->txwait);
	INIT_LIST_HEAD(&dev->memwait);

	ret = verbs_txreq_init(dev);
	if (ret)
		goto err_verbs_txreq;

	/* Use first-port GUID as node guid */
	ibdev->node_guid = get_sguid(ibp, HFI1_PORT_GUID_INDEX);

	/*
	 * The system image GUID is supposed to be the same for all
	 * HFIs in a single system but since there can be other
	 * device types in the system, we can't be sure this is unique.
	 */
	if (!ib_hfi1_sys_image_guid)
		ib_hfi1_sys_image_guid = ibdev->node_guid;
	ibdev->owner = THIS_MODULE;
	ibdev->phys_port_cnt = dd->num_pports;
	ibdev->dev.parent = &dd->pcidev->dev;

	ib_set_device_ops(ibdev, &hfi1_dev_ops);

	strlcpy(ibdev->node_desc, init_utsname()->nodename,
		sizeof(ibdev->node_desc));

	/*
	 * Fill in rvt info object.
	 */
	dd->verbs_dev.rdi.driver_f.get_pci_dev = get_pci_dev;
	dd->verbs_dev.rdi.driver_f.check_ah = hfi1_check_ah;
	dd->verbs_dev.rdi.driver_f.notify_new_ah = hfi1_notify_new_ah;
	dd->verbs_dev.rdi.driver_f.get_guid_be = hfi1_get_guid_be;
	dd->verbs_dev.rdi.driver_f.query_port_state = query_port;
	dd->verbs_dev.rdi.driver_f.shut_down_port = shut_down_port;
	dd->verbs_dev.rdi.driver_f.cap_mask_chg = hfi1_cap_mask_chg;
	/*
	 * Fill in rvt info device attributes.
	 */
	hfi1_fill_device_attr(dd);

	/* queue pair */
	dd->verbs_dev.rdi.dparms.qp_table_size = hfi1_qp_table_size;
	dd->verbs_dev.rdi.dparms.qpn_start = 0;
	dd->verbs_dev.rdi.dparms.qpn_inc = 1;
	dd->verbs_dev.rdi.dparms.qos_shift = dd->qos_shift;
	dd->verbs_dev.rdi.dparms.qpn_res_start = kdeth_qp << 16;
	dd->verbs_dev.rdi.dparms.qpn_res_end =
	dd->verbs_dev.rdi.dparms.qpn_res_start + 65535;
	dd->verbs_dev.rdi.dparms.max_rdma_atomic = HFI1_MAX_RDMA_ATOMIC;
	dd->verbs_dev.rdi.dparms.psn_mask = PSN_MASK;
	dd->verbs_dev.rdi.dparms.psn_shift = PSN_SHIFT;
	dd->verbs_dev.rdi.dparms.psn_modify_mask = PSN_MODIFY_MASK;
	dd->verbs_dev.rdi.dparms.core_cap_flags = RDMA_CORE_PORT_INTEL_OPA |
						RDMA_CORE_CAP_OPA_AH;
	dd->verbs_dev.rdi.dparms.max_mad_size = OPA_MGMT_MAD_SIZE;

	dd->verbs_dev.rdi.driver_f.qp_priv_alloc = qp_priv_alloc;
	dd->verbs_dev.rdi.driver_f.qp_priv_init = hfi1_qp_priv_init;
	dd->verbs_dev.rdi.driver_f.qp_priv_free = qp_priv_free;
	dd->verbs_dev.rdi.driver_f.free_all_qps = free_all_qps;
	dd->verbs_dev.rdi.driver_f.notify_qp_reset = notify_qp_reset;
	dd->verbs_dev.rdi.driver_f.do_send = hfi1_do_send_from_rvt;
	dd->verbs_dev.rdi.driver_f.schedule_send = hfi1_schedule_send;
	dd->verbs_dev.rdi.driver_f.schedule_send_no_lock = _hfi1_schedule_send;
	dd->verbs_dev.rdi.driver_f.get_pmtu_from_attr = get_pmtu_from_attr;
	dd->verbs_dev.rdi.driver_f.notify_error_qp = notify_error_qp;
	dd->verbs_dev.rdi.driver_f.flush_qp_waiters = flush_qp_waiters;
	dd->verbs_dev.rdi.driver_f.stop_send_queue = stop_send_queue;
	dd->verbs_dev.rdi.driver_f.quiesce_qp = quiesce_qp;
	dd->verbs_dev.rdi.driver_f.notify_error_qp = notify_error_qp;
	dd->verbs_dev.rdi.driver_f.mtu_from_qp = mtu_from_qp;
	dd->verbs_dev.rdi.driver_f.mtu_to_path_mtu = mtu_to_path_mtu;
	dd->verbs_dev.rdi.driver_f.check_modify_qp = hfi1_check_modify_qp;
	dd->verbs_dev.rdi.driver_f.modify_qp = hfi1_modify_qp;
	dd->verbs_dev.rdi.driver_f.notify_restart_rc = hfi1_restart_rc;
	dd->verbs_dev.rdi.driver_f.setup_wqe = hfi1_setup_wqe;
	dd->verbs_dev.rdi.driver_f.comp_vect_cpu_lookup =
						hfi1_comp_vect_mappings_lookup;

	/* completeion queue */
	dd->verbs_dev.rdi.ibdev.num_comp_vectors = dd->comp_vect_possible_cpus;
	dd->verbs_dev.rdi.dparms.node = dd->node;

	/* misc settings */
	dd->verbs_dev.rdi.flags = 0; /* Let rdmavt handle it all */
	dd->verbs_dev.rdi.dparms.lkey_table_size = hfi1_lkey_table_size;
	dd->verbs_dev.rdi.dparms.nports = dd->num_pports;
	dd->verbs_dev.rdi.dparms.npkeys = hfi1_get_npkeys(dd);
	dd->verbs_dev.rdi.dparms.sge_copy_mode = sge_copy_mode;
	dd->verbs_dev.rdi.dparms.wss_threshold = wss_threshold;
	dd->verbs_dev.rdi.dparms.wss_clean_period = wss_clean_period;
	dd->verbs_dev.rdi.dparms.reserved_operations = 1;
	dd->verbs_dev.rdi.dparms.extra_rdma_atomic = HFI1_TID_RDMA_WRITE_CNT;

	/* post send table */
	dd->verbs_dev.rdi.post_parms = hfi1_post_parms;

	/* opcode translation table */
	dd->verbs_dev.rdi.wc_opcode = ib_hfi1_wc_opcode;

	ppd = dd->pport;
	for (i = 0; i < dd->num_pports; i++, ppd++)
		rvt_init_port(&dd->verbs_dev.rdi,
			      &ppd->ibport_data.rvp,
			      i,
			      ppd->pkeys);

	rdma_set_device_sysfs_group(&dd->verbs_dev.rdi.ibdev,
				    &ib_hfi1_attr_group);

	ret = rvt_register_device(&dd->verbs_dev.rdi, RDMA_DRIVER_HFI1);
	if (ret)
		goto err_verbs_txreq;

	ret = hfi1_verbs_register_sysfs(dd);
	if (ret)
		goto err_class;

	return ret;

err_class:
	rvt_unregister_device(&dd->verbs_dev.rdi);
err_verbs_txreq:
	verbs_txreq_exit(dev);
	dd_dev_err(dd, "cannot register verbs: %d!\n", -ret);
	return ret;
}

void hfi1_unregister_ib_device(struct hfi1_devdata *dd)
{
	struct hfi1_ibdev *dev = &dd->verbs_dev;

	hfi1_verbs_unregister_sysfs(dd);

	rvt_unregister_device(&dd->verbs_dev.rdi);

	if (!list_empty(&dev->txwait))
		dd_dev_err(dd, "txwait list not empty!\n");
	if (!list_empty(&dev->memwait))
		dd_dev_err(dd, "memwait list not empty!\n");

	del_timer_sync(&dev->mem_timer);
	verbs_txreq_exit(dev);

	mutex_lock(&cntr_names_lock);
	kfree(dev_cntr_names);
	kfree(port_cntr_names);
	dev_cntr_names = NULL;
	port_cntr_names = NULL;
	cntr_names_initialized = 0;
	mutex_unlock(&cntr_names_lock);
}

void hfi1_cnp_rcv(struct hfi1_packet *packet)
{
	struct hfi1_ibport *ibp = rcd_to_iport(packet->rcd);
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	struct ib_header *hdr = packet->hdr;
	struct rvt_qp *qp = packet->qp;
	u32 lqpn, rqpn = 0;
	u16 rlid = 0;
	u8 sl, sc5, svc_type;

	switch (packet->qp->ibqp.qp_type) {
	case IB_QPT_UC:
		rlid = rdma_ah_get_dlid(&qp->remote_ah_attr);
		rqpn = qp->remote_qpn;
		svc_type = IB_CC_SVCTYPE_UC;
		break;
	case IB_QPT_RC:
		rlid = rdma_ah_get_dlid(&qp->remote_ah_attr);
		rqpn = qp->remote_qpn;
		svc_type = IB_CC_SVCTYPE_RC;
		break;
	case IB_QPT_SMI:
	case IB_QPT_GSI:
	case IB_QPT_UD:
		svc_type = IB_CC_SVCTYPE_UD;
		break;
	default:
		ibp->rvp.n_pkt_drops++;
		return;
	}

	sc5 = hfi1_9B_get_sc5(hdr, packet->rhf);
	sl = ibp->sc_to_sl[sc5];
	lqpn = qp->ibqp.qp_num;

	process_becn(ppd, sl, rlid, lqpn, rqpn, svc_type);
}
