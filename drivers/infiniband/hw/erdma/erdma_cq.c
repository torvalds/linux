// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause

/* Authors: Cheng Xu <chengyou@linux.alibaba.com> */
/*          Kai Shen <kaishen@linux.alibaba.com> */
/* Copyright (c) 2020-2022, Alibaba Group. */

#include "erdma_verbs.h"

static void *get_next_valid_cqe(struct erdma_cq *cq)
{
	__be32 *cqe = get_queue_entry(cq->kern_cq.qbuf, cq->kern_cq.ci,
				      cq->depth, CQE_SHIFT);
	u32 owner = FIELD_GET(ERDMA_CQE_HDR_OWNER_MASK,
			      be32_to_cpu(READ_ONCE(*cqe)));

	return owner ^ !!(cq->kern_cq.ci & cq->depth) ? cqe : NULL;
}

static void notify_cq(struct erdma_cq *cq, u8 solcitied)
{
	u64 db_data =
		FIELD_PREP(ERDMA_CQDB_IDX_MASK, (cq->kern_cq.notify_cnt)) |
		FIELD_PREP(ERDMA_CQDB_CQN_MASK, cq->cqn) |
		FIELD_PREP(ERDMA_CQDB_ARM_MASK, 1) |
		FIELD_PREP(ERDMA_CQDB_SOL_MASK, solcitied) |
		FIELD_PREP(ERDMA_CQDB_CMDSN_MASK, cq->kern_cq.cmdsn) |
		FIELD_PREP(ERDMA_CQDB_CI_MASK, cq->kern_cq.ci);

	*cq->kern_cq.dbrec = db_data;
	writeq(db_data, cq->kern_cq.db);
}

int erdma_req_notify_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags)
{
	struct erdma_cq *cq = to_ecq(ibcq);
	unsigned long irq_flags;
	int ret = 0;

	spin_lock_irqsave(&cq->kern_cq.lock, irq_flags);

	notify_cq(cq, (flags & IB_CQ_SOLICITED_MASK) == IB_CQ_SOLICITED);

	if ((flags & IB_CQ_REPORT_MISSED_EVENTS) && get_next_valid_cqe(cq))
		ret = 1;

	cq->kern_cq.notify_cnt++;

	spin_unlock_irqrestore(&cq->kern_cq.lock, irq_flags);

	return ret;
}

static const enum ib_wc_opcode wc_mapping_table[ERDMA_NUM_OPCODES] = {
	[ERDMA_OP_WRITE] = IB_WC_RDMA_WRITE,
	[ERDMA_OP_READ] = IB_WC_RDMA_READ,
	[ERDMA_OP_SEND] = IB_WC_SEND,
	[ERDMA_OP_SEND_WITH_IMM] = IB_WC_SEND,
	[ERDMA_OP_RECEIVE] = IB_WC_RECV,
	[ERDMA_OP_RECV_IMM] = IB_WC_RECV_RDMA_WITH_IMM,
	[ERDMA_OP_RECV_INV] = IB_WC_RECV,
	[ERDMA_OP_WRITE_WITH_IMM] = IB_WC_RDMA_WRITE,
	[ERDMA_OP_RSP_SEND_IMM] = IB_WC_RECV,
	[ERDMA_OP_SEND_WITH_INV] = IB_WC_SEND,
	[ERDMA_OP_REG_MR] = IB_WC_REG_MR,
	[ERDMA_OP_LOCAL_INV] = IB_WC_LOCAL_INV,
	[ERDMA_OP_READ_WITH_INV] = IB_WC_RDMA_READ,
	[ERDMA_OP_ATOMIC_CAS] = IB_WC_COMP_SWAP,
	[ERDMA_OP_ATOMIC_FAA] = IB_WC_FETCH_ADD,
};

static const struct {
	enum erdma_wc_status erdma;
	enum ib_wc_status base;
	enum erdma_vendor_err vendor;
} map_cqe_status[ERDMA_NUM_WC_STATUS] = {
	{ ERDMA_WC_SUCCESS, IB_WC_SUCCESS, ERDMA_WC_VENDOR_NO_ERR },
	{ ERDMA_WC_GENERAL_ERR, IB_WC_GENERAL_ERR, ERDMA_WC_VENDOR_NO_ERR },
	{ ERDMA_WC_RECV_WQE_FORMAT_ERR, IB_WC_GENERAL_ERR,
	  ERDMA_WC_VENDOR_INVALID_RQE },
	{ ERDMA_WC_RECV_STAG_INVALID_ERR, IB_WC_REM_ACCESS_ERR,
	  ERDMA_WC_VENDOR_RQE_INVALID_STAG },
	{ ERDMA_WC_RECV_ADDR_VIOLATION_ERR, IB_WC_REM_ACCESS_ERR,
	  ERDMA_WC_VENDOR_RQE_ADDR_VIOLATION },
	{ ERDMA_WC_RECV_RIGHT_VIOLATION_ERR, IB_WC_REM_ACCESS_ERR,
	  ERDMA_WC_VENDOR_RQE_ACCESS_RIGHT_ERR },
	{ ERDMA_WC_RECV_PDID_ERR, IB_WC_REM_ACCESS_ERR,
	  ERDMA_WC_VENDOR_RQE_INVALID_PD },
	{ ERDMA_WC_RECV_WARRPING_ERR, IB_WC_REM_ACCESS_ERR,
	  ERDMA_WC_VENDOR_RQE_WRAP_ERR },
	{ ERDMA_WC_SEND_WQE_FORMAT_ERR, IB_WC_LOC_QP_OP_ERR,
	  ERDMA_WC_VENDOR_INVALID_SQE },
	{ ERDMA_WC_SEND_WQE_ORD_EXCEED, IB_WC_GENERAL_ERR,
	  ERDMA_WC_VENDOR_ZERO_ORD },
	{ ERDMA_WC_SEND_STAG_INVALID_ERR, IB_WC_LOC_ACCESS_ERR,
	  ERDMA_WC_VENDOR_SQE_INVALID_STAG },
	{ ERDMA_WC_SEND_ADDR_VIOLATION_ERR, IB_WC_LOC_ACCESS_ERR,
	  ERDMA_WC_VENDOR_SQE_ADDR_VIOLATION },
	{ ERDMA_WC_SEND_RIGHT_VIOLATION_ERR, IB_WC_LOC_ACCESS_ERR,
	  ERDMA_WC_VENDOR_SQE_ACCESS_ERR },
	{ ERDMA_WC_SEND_PDID_ERR, IB_WC_LOC_ACCESS_ERR,
	  ERDMA_WC_VENDOR_SQE_INVALID_PD },
	{ ERDMA_WC_SEND_WARRPING_ERR, IB_WC_LOC_ACCESS_ERR,
	  ERDMA_WC_VENDOR_SQE_WARP_ERR },
	{ ERDMA_WC_FLUSH_ERR, IB_WC_WR_FLUSH_ERR, ERDMA_WC_VENDOR_NO_ERR },
	{ ERDMA_WC_RETRY_EXC_ERR, IB_WC_RETRY_EXC_ERR, ERDMA_WC_VENDOR_NO_ERR },
};

#define ERDMA_POLLCQ_NO_QP 1

static int erdma_poll_one_cqe(struct erdma_cq *cq, struct ib_wc *wc)
{
	struct erdma_dev *dev = to_edev(cq->ibcq.device);
	u8 opcode, syndrome, qtype;
	struct erdma_kqp *kern_qp;
	struct erdma_cqe *cqe;
	struct erdma_qp *qp;
	u16 wqe_idx, depth;
	u32 qpn, cqe_hdr;
	u64 *id_table;
	u64 *wqe_hdr;

	cqe = get_next_valid_cqe(cq);
	if (!cqe)
		return -EAGAIN;

	cq->kern_cq.ci++;

	/* cqbuf should be ready when we poll */
	dma_rmb();

	qpn = be32_to_cpu(cqe->qpn);
	wqe_idx = be32_to_cpu(cqe->qe_idx);
	cqe_hdr = be32_to_cpu(cqe->hdr);

	qp = find_qp_by_qpn(dev, qpn);
	if (!qp)
		return ERDMA_POLLCQ_NO_QP;

	kern_qp = &qp->kern_qp;

	qtype = FIELD_GET(ERDMA_CQE_HDR_QTYPE_MASK, cqe_hdr);
	syndrome = FIELD_GET(ERDMA_CQE_HDR_SYNDROME_MASK, cqe_hdr);
	opcode = FIELD_GET(ERDMA_CQE_HDR_OPCODE_MASK, cqe_hdr);

	if (qtype == ERDMA_CQE_QTYPE_SQ) {
		id_table = kern_qp->swr_tbl;
		depth = qp->attrs.sq_size;
		wqe_hdr = get_queue_entry(qp->kern_qp.sq_buf, wqe_idx,
					  qp->attrs.sq_size, SQEBB_SHIFT);
		kern_qp->sq_ci =
			FIELD_GET(ERDMA_SQE_HDR_WQEBB_CNT_MASK, *wqe_hdr) +
			wqe_idx + 1;
	} else {
		id_table = kern_qp->rwr_tbl;
		depth = qp->attrs.rq_size;
	}
	wc->wr_id = id_table[wqe_idx & (depth - 1)];
	wc->byte_len = be32_to_cpu(cqe->size);

	wc->wc_flags = 0;

	wc->opcode = wc_mapping_table[opcode];
	if (opcode == ERDMA_OP_RECV_IMM || opcode == ERDMA_OP_RSP_SEND_IMM) {
		wc->ex.imm_data = cpu_to_be32(le32_to_cpu(cqe->imm_data));
		wc->wc_flags |= IB_WC_WITH_IMM;
	} else if (opcode == ERDMA_OP_RECV_INV) {
		wc->ex.invalidate_rkey = be32_to_cpu(cqe->inv_rkey);
		wc->wc_flags |= IB_WC_WITH_INVALIDATE;
	}

	if (syndrome >= ERDMA_NUM_WC_STATUS)
		syndrome = ERDMA_WC_GENERAL_ERR;

	wc->status = map_cqe_status[syndrome].base;
	wc->vendor_err = map_cqe_status[syndrome].vendor;
	wc->qp = &qp->ibqp;

	return 0;
}

int erdma_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc)
{
	struct erdma_cq *cq = to_ecq(ibcq);
	unsigned long flags;
	int npolled, ret;

	spin_lock_irqsave(&cq->kern_cq.lock, flags);

	for (npolled = 0; npolled < num_entries;) {
		ret = erdma_poll_one_cqe(cq, wc + npolled);

		if (ret == -EAGAIN) /* no received new CQEs. */
			break;
		else if (ret) /* ignore invalid CQEs. */
			continue;

		npolled++;
	}

	spin_unlock_irqrestore(&cq->kern_cq.lock, flags);

	return npolled;
}
