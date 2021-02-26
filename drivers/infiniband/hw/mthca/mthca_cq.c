/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005, 2006 Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2004 Voltaire, Inc. All rights reserved.
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
 *        disclaimer in the documentation and/or other materials
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

#include <linux/gfp.h>
#include <linux/hardirq.h>
#include <linux/sched.h>

#include <asm/io.h>

#include <rdma/ib_pack.h>

#include "mthca_dev.h"
#include "mthca_cmd.h"
#include "mthca_memfree.h"

enum {
	MTHCA_MAX_DIRECT_CQ_SIZE = 4 * PAGE_SIZE
};

enum {
	MTHCA_CQ_ENTRY_SIZE = 0x20
};

enum {
	MTHCA_ATOMIC_BYTE_LEN = 8
};

/*
 * Must be packed because start is 64 bits but only aligned to 32 bits.
 */
struct mthca_cq_context {
	__be32 flags;
	__be64 start;
	__be32 logsize_usrpage;
	__be32 error_eqn;	/* Tavor only */
	__be32 comp_eqn;
	__be32 pd;
	__be32 lkey;
	__be32 last_notified_index;
	__be32 solicit_producer_index;
	__be32 consumer_index;
	__be32 producer_index;
	__be32 cqn;
	__be32 ci_db;		/* Arbel only */
	__be32 state_db;	/* Arbel only */
	u32    reserved;
} __attribute__((packed));

#define MTHCA_CQ_STATUS_OK          ( 0 << 28)
#define MTHCA_CQ_STATUS_OVERFLOW    ( 9 << 28)
#define MTHCA_CQ_STATUS_WRITE_FAIL  (10 << 28)
#define MTHCA_CQ_FLAG_TR            ( 1 << 18)
#define MTHCA_CQ_FLAG_OI            ( 1 << 17)
#define MTHCA_CQ_STATE_DISARMED     ( 0 <<  8)
#define MTHCA_CQ_STATE_ARMED        ( 1 <<  8)
#define MTHCA_CQ_STATE_ARMED_SOL    ( 4 <<  8)
#define MTHCA_EQ_STATE_FIRED        (10 <<  8)

enum {
	MTHCA_ERROR_CQE_OPCODE_MASK = 0xfe
};

enum {
	SYNDROME_LOCAL_LENGTH_ERR 	 = 0x01,
	SYNDROME_LOCAL_QP_OP_ERR  	 = 0x02,
	SYNDROME_LOCAL_EEC_OP_ERR 	 = 0x03,
	SYNDROME_LOCAL_PROT_ERR   	 = 0x04,
	SYNDROME_WR_FLUSH_ERR     	 = 0x05,
	SYNDROME_MW_BIND_ERR      	 = 0x06,
	SYNDROME_BAD_RESP_ERR     	 = 0x10,
	SYNDROME_LOCAL_ACCESS_ERR 	 = 0x11,
	SYNDROME_REMOTE_INVAL_REQ_ERR 	 = 0x12,
	SYNDROME_REMOTE_ACCESS_ERR 	 = 0x13,
	SYNDROME_REMOTE_OP_ERR     	 = 0x14,
	SYNDROME_RETRY_EXC_ERR 		 = 0x15,
	SYNDROME_RNR_RETRY_EXC_ERR 	 = 0x16,
	SYNDROME_LOCAL_RDD_VIOL_ERR 	 = 0x20,
	SYNDROME_REMOTE_INVAL_RD_REQ_ERR = 0x21,
	SYNDROME_REMOTE_ABORTED_ERR 	 = 0x22,
	SYNDROME_INVAL_EECN_ERR 	 = 0x23,
	SYNDROME_INVAL_EEC_STATE_ERR 	 = 0x24
};

struct mthca_cqe {
	__be32 my_qpn;
	__be32 my_ee;
	__be32 rqpn;
	u8     sl_ipok;
	u8     g_mlpath;
	__be16 rlid;
	__be32 imm_etype_pkey_eec;
	__be32 byte_cnt;
	__be32 wqe;
	u8     opcode;
	u8     is_send;
	u8     reserved;
	u8     owner;
};

struct mthca_err_cqe {
	__be32 my_qpn;
	u32    reserved1[3];
	u8     syndrome;
	u8     vendor_err;
	__be16 db_cnt;
	u32    reserved2;
	__be32 wqe;
	u8     opcode;
	u8     reserved3[2];
	u8     owner;
};

#define MTHCA_CQ_ENTRY_OWNER_SW      (0 << 7)
#define MTHCA_CQ_ENTRY_OWNER_HW      (1 << 7)

#define MTHCA_TAVOR_CQ_DB_INC_CI       (1 << 24)
#define MTHCA_TAVOR_CQ_DB_REQ_NOT      (2 << 24)
#define MTHCA_TAVOR_CQ_DB_REQ_NOT_SOL  (3 << 24)
#define MTHCA_TAVOR_CQ_DB_SET_CI       (4 << 24)
#define MTHCA_TAVOR_CQ_DB_REQ_NOT_MULT (5 << 24)

#define MTHCA_ARBEL_CQ_DB_REQ_NOT_SOL  (1 << 24)
#define MTHCA_ARBEL_CQ_DB_REQ_NOT      (2 << 24)
#define MTHCA_ARBEL_CQ_DB_REQ_NOT_MULT (3 << 24)

static inline struct mthca_cqe *get_cqe_from_buf(struct mthca_cq_buf *buf,
						 int entry)
{
	if (buf->is_direct)
		return buf->queue.direct.buf + (entry * MTHCA_CQ_ENTRY_SIZE);
	else
		return buf->queue.page_list[entry * MTHCA_CQ_ENTRY_SIZE / PAGE_SIZE].buf
			+ (entry * MTHCA_CQ_ENTRY_SIZE) % PAGE_SIZE;
}

static inline struct mthca_cqe *get_cqe(struct mthca_cq *cq, int entry)
{
	return get_cqe_from_buf(&cq->buf, entry);
}

static inline struct mthca_cqe *cqe_sw(struct mthca_cqe *cqe)
{
	return MTHCA_CQ_ENTRY_OWNER_HW & cqe->owner ? NULL : cqe;
}

static inline struct mthca_cqe *next_cqe_sw(struct mthca_cq *cq)
{
	return cqe_sw(get_cqe(cq, cq->cons_index & cq->ibcq.cqe));
}

static inline void set_cqe_hw(struct mthca_cqe *cqe)
{
	cqe->owner = MTHCA_CQ_ENTRY_OWNER_HW;
}

static void dump_cqe(struct mthca_dev *dev, void *cqe_ptr)
{
	__be32 *cqe = cqe_ptr;

	(void) cqe;	/* avoid warning if mthca_dbg compiled away... */
	mthca_dbg(dev, "CQE contents %08x %08x %08x %08x %08x %08x %08x %08x\n",
		  be32_to_cpu(cqe[0]), be32_to_cpu(cqe[1]), be32_to_cpu(cqe[2]),
		  be32_to_cpu(cqe[3]), be32_to_cpu(cqe[4]), be32_to_cpu(cqe[5]),
		  be32_to_cpu(cqe[6]), be32_to_cpu(cqe[7]));
}

/*
 * incr is ignored in native Arbel (mem-free) mode, so cq->cons_index
 * should be correct before calling update_cons_index().
 */
static inline void update_cons_index(struct mthca_dev *dev, struct mthca_cq *cq,
				     int incr)
{
	if (mthca_is_memfree(dev)) {
		*cq->set_ci_db = cpu_to_be32(cq->cons_index);
		wmb();
	} else {
		mthca_write64(MTHCA_TAVOR_CQ_DB_INC_CI | cq->cqn, incr - 1,
			      dev->kar + MTHCA_CQ_DOORBELL,
			      MTHCA_GET_DOORBELL_LOCK(&dev->doorbell_lock));
		/*
		 * Make sure doorbells don't leak out of CQ spinlock
		 * and reach the HCA out of order:
		 */
		mmiowb();
	}
}

void mthca_cq_completion(struct mthca_dev *dev, u32 cqn)
{
	struct mthca_cq *cq;

	cq = mthca_array_get(&dev->cq_table.cq, cqn & (dev->limits.num_cqs - 1));

	if (!cq) {
		mthca_warn(dev, "Completion event for bogus CQ %08x\n", cqn);
		return;
	}

	++cq->arm_sn;

	cq->ibcq.comp_handler(&cq->ibcq, cq->ibcq.cq_context);
}

void mthca_cq_event(struct mthca_dev *dev, u32 cqn,
		    enum ib_event_type event_type)
{
	struct mthca_cq *cq;
	struct ib_event event;

	spin_lock(&dev->cq_table.lock);

	cq = mthca_array_get(&dev->cq_table.cq, cqn & (dev->limits.num_cqs - 1));
	if (cq)
		++cq->refcount;

	spin_unlock(&dev->cq_table.lock);

	if (!cq) {
		mthca_warn(dev, "Async event for bogus CQ %08x\n", cqn);
		return;
	}

	event.device      = &dev->ib_dev;
	event.event       = event_type;
	event.element.cq  = &cq->ibcq;
	if (cq->ibcq.event_handler)
		cq->ibcq.event_handler(&event, cq->ibcq.cq_context);

	spin_lock(&dev->cq_table.lock);
	if (!--cq->refcount)
		wake_up(&cq->wait);
	spin_unlock(&dev->cq_table.lock);
}

static inline int is_recv_cqe(struct mthca_cqe *cqe)
{
	if ((cqe->opcode & MTHCA_ERROR_CQE_OPCODE_MASK) ==
	    MTHCA_ERROR_CQE_OPCODE_MASK)
		return !(cqe->opcode & 0x01);
	else
		return !(cqe->is_send & 0x80);
}

void mthca_cq_clean(struct mthca_dev *dev, struct mthca_cq *cq, u32 qpn,
		    struct mthca_srq *srq)
{
	struct mthca_cqe *cqe;
	u32 prod_index;
	int i, nfreed = 0;

	spin_lock_irq(&cq->lock);

	/*
	 * First we need to find the current producer index, so we
	 * know where to start cleaning from.  It doesn't matter if HW
	 * adds new entries after this loop -- the QP we're worried
	 * about is already in RESET, so the new entries won't come
	 * from our QP and therefore don't need to be checked.
	 */
	for (prod_index = cq->cons_index;
	     cqe_sw(get_cqe(cq, prod_index & cq->ibcq.cqe));
	     ++prod_index)
		if (prod_index == cq->cons_index + cq->ibcq.cqe)
			break;

	if (0)
		mthca_dbg(dev, "Cleaning QPN %06x from CQN %06x; ci %d, pi %d\n",
			  qpn, cq->cqn, cq->cons_index, prod_index);

	/*
	 * Now sweep backwards through the CQ, removing CQ entries
	 * that match our QP by copying older entries on top of them.
	 */
	while ((int) --prod_index - (int) cq->cons_index >= 0) {
		cqe = get_cqe(cq, prod_index & cq->ibcq.cqe);
		if (cqe->my_qpn == cpu_to_be32(qpn)) {
			if (srq && is_recv_cqe(cqe))
				mthca_free_srq_wqe(srq, be32_to_cpu(cqe->wqe));
			++nfreed;
		} else if (nfreed)
			memcpy(get_cqe(cq, (prod_index + nfreed) & cq->ibcq.cqe),
			       cqe, MTHCA_CQ_ENTRY_SIZE);
	}

	if (nfreed) {
		for (i = 0; i < nfreed; ++i)
			set_cqe_hw(get_cqe(cq, (cq->cons_index + i) & cq->ibcq.cqe));
		wmb();
		cq->cons_index += nfreed;
		update_cons_index(dev, cq, nfreed);
	}

	spin_unlock_irq(&cq->lock);
}

void mthca_cq_resize_copy_cqes(struct mthca_cq *cq)
{
	int i;

	/*
	 * In Tavor mode, the hardware keeps the consumer and producer
	 * indices mod the CQ size.  Since we might be making the CQ
	 * bigger, we need to deal with the case where the producer
	 * index wrapped around before the CQ was resized.
	 */
	if (!mthca_is_memfree(to_mdev(cq->ibcq.device)) &&
	    cq->ibcq.cqe < cq->resize_buf->cqe) {
		cq->cons_index &= cq->ibcq.cqe;
		if (cqe_sw(get_cqe(cq, cq->ibcq.cqe)))
			cq->cons_index -= cq->ibcq.cqe + 1;
	}

	for (i = cq->cons_index; cqe_sw(get_cqe(cq, i & cq->ibcq.cqe)); ++i)
		memcpy(get_cqe_from_buf(&cq->resize_buf->buf,
					i & cq->resize_buf->cqe),
		       get_cqe(cq, i & cq->ibcq.cqe), MTHCA_CQ_ENTRY_SIZE);
}

int mthca_alloc_cq_buf(struct mthca_dev *dev, struct mthca_cq_buf *buf, int nent)
{
	int ret;
	int i;

	ret = mthca_buf_alloc(dev, nent * MTHCA_CQ_ENTRY_SIZE,
			      MTHCA_MAX_DIRECT_CQ_SIZE,
			      &buf->queue, &buf->is_direct,
			      &dev->driver_pd, 1, &buf->mr);
	if (ret)
		return ret;

	for (i = 0; i < nent; ++i)
		set_cqe_hw(get_cqe_from_buf(buf, i));

	return 0;
}

void mthca_free_cq_buf(struct mthca_dev *dev, struct mthca_cq_buf *buf, int cqe)
{
	mthca_buf_free(dev, (cqe + 1) * MTHCA_CQ_ENTRY_SIZE, &buf->queue,
		       buf->is_direct, &buf->mr);
}

static void handle_error_cqe(struct mthca_dev *dev, struct mthca_cq *cq,
			     struct mthca_qp *qp, int wqe_index, int is_send,
			     struct mthca_err_cqe *cqe,
			     struct ib_wc *entry, int *free_cqe)
{
	int dbd;
	__be32 new_wqe;

	if (cqe->syndrome == SYNDROME_LOCAL_QP_OP_ERR) {
		mthca_dbg(dev, "local QP operation err "
			  "(QPN %06x, WQE @ %08x, CQN %06x, index %d)\n",
			  be32_to_cpu(cqe->my_qpn), be32_to_cpu(cqe->wqe),
			  cq->cqn, cq->cons_index);
		dump_cqe(dev, cqe);
	}

	/*
	 * For completions in error, only work request ID, status, vendor error
	 * (and freed resource count for RD) have to be set.
	 */
	switch (cqe->syndrome) {
	case SYNDROME_LOCAL_LENGTH_ERR:
		entry->status = IB_WC_LOC_LEN_ERR;
		break;
	case SYNDROME_LOCAL_QP_OP_ERR:
		entry->status = IB_WC_LOC_QP_OP_ERR;
		break;
	case SYNDROME_LOCAL_EEC_OP_ERR:
		entry->status = IB_WC_LOC_EEC_OP_ERR;
		break;
	case SYNDROME_LOCAL_PROT_ERR:
		entry->status = IB_WC_LOC_PROT_ERR;
		break;
	case SYNDROME_WR_FLUSH_ERR:
		entry->status = IB_WC_WR_FLUSH_ERR;
		break;
	case SYNDROME_MW_BIND_ERR:
		entry->status = IB_WC_MW_BIND_ERR;
		break;
	case SYNDROME_BAD_RESP_ERR:
		entry->status = IB_WC_BAD_RESP_ERR;
		break;
	case SYNDROME_LOCAL_ACCESS_ERR:
		entry->status = IB_WC_LOC_ACCESS_ERR;
		break;
	case SYNDROME_REMOTE_INVAL_REQ_ERR:
		entry->status = IB_WC_REM_INV_REQ_ERR;
		break;
	case SYNDROME_REMOTE_ACCESS_ERR:
		entry->status = IB_WC_REM_ACCESS_ERR;
		break;
	case SYNDROME_REMOTE_OP_ERR:
		entry->status = IB_WC_REM_OP_ERR;
		break;
	case SYNDROME_RETRY_EXC_ERR:
		entry->status = IB_WC_RETRY_EXC_ERR;
		break;
	case SYNDROME_RNR_RETRY_EXC_ERR:
		entry->status = IB_WC_RNR_RETRY_EXC_ERR;
		break;
	case SYNDROME_LOCAL_RDD_VIOL_ERR:
		entry->status = IB_WC_LOC_RDD_VIOL_ERR;
		break;
	case SYNDROME_REMOTE_INVAL_RD_REQ_ERR:
		entry->status = IB_WC_REM_INV_RD_REQ_ERR;
		break;
	case SYNDROME_REMOTE_ABORTED_ERR:
		entry->status = IB_WC_REM_ABORT_ERR;
		break;
	case SYNDROME_INVAL_EECN_ERR:
		entry->status = IB_WC_INV_EECN_ERR;
		break;
	case SYNDROME_INVAL_EEC_STATE_ERR:
		entry->status = IB_WC_INV_EEC_STATE_ERR;
		break;
	default:
		entry->status = IB_WC_GENERAL_ERR;
		break;
	}

	entry->vendor_err = cqe->vendor_err;

	/*
	 * Mem-free HCAs always generate one CQE per WQE, even in the
	 * error case, so we don't have to check the doorbell count, etc.
	 */
	if (mthca_is_memfree(dev))
		return;

	mthca_free_err_wqe(dev, qp, is_send, wqe_index, &dbd, &new_wqe);

	/*
	 * If we're at the end of the WQE chain, or we've used up our
	 * doorbell count, free the CQE.  Otherwise just update it for
	 * the next poll operation.
	 */
	if (!(new_wqe & cpu_to_be32(0x3f)) || (!cqe->db_cnt && dbd))
		return;

	be16_add_cpu(&cqe->db_cnt, -dbd);
	cqe->wqe      = new_wqe;
	cqe->syndrome = SYNDROME_WR_FLUSH_ERR;

	*free_cqe = 0;
}

static inline int mthca_poll_one(struct mthca_dev *dev,
				 struct mthca_cq *cq,
				 struct mthca_qp **cur_qp,
				 int *freed,
				 struct ib_wc *entry)
{
	struct mthca_wq *wq;
	struct mthca_cqe *cqe;
	int wqe_index;
	int is_error;
	int is_send;
	int free_cqe = 1;
	int err = 0;
	u16 checksum;

	cqe = next_cqe_sw(cq);
	if (!cqe)
		return -EAGAIN;

	/*
	 * Make sure we read CQ entry contents after we've checked the
	 * ownership bit.
	 */
	rmb();

	if (0) {
		mthca_dbg(dev, "%x/%d: CQE -> QPN %06x, WQE @ %08x\n",
			  cq->cqn, cq->cons_index, be32_to_cpu(cqe->my_qpn),
			  be32_to_cpu(cqe->wqe));
		dump_cqe(dev, cqe);
	}

	is_error = (cqe->opcode & MTHCA_ERROR_CQE_OPCODE_MASK) ==
		MTHCA_ERROR_CQE_OPCODE_MASK;
	is_send  = is_error ? cqe->opcode & 0x01 : cqe->is_send & 0x80;

	if (!*cur_qp || be32_to_cpu(cqe->my_qpn) != (*cur_qp)->qpn) {
		/*
		 * We do not have to take the QP table lock here,
		 * because CQs will be locked while QPs are removed
		 * from the table.
		 */
		*cur_qp = mthca_array_get(&dev->qp_table.qp,
					  be32_to_cpu(cqe->my_qpn) &
					  (dev->limits.num_qps - 1));
		if (!*cur_qp) {
			mthca_warn(dev, "CQ entry for unknown QP %06x\n",
				   be32_to_cpu(cqe->my_qpn) & 0xffffff);
			err = -EINVAL;
			goto out;
		}
	}

	entry->qp = &(*cur_qp)->ibqp;

	if (is_send) {
		wq = &(*cur_qp)->sq;
		wqe_index = ((be32_to_cpu(cqe->wqe) - (*cur_qp)->send_wqe_offset)
			     >> wq->wqe_shift);
		entry->wr_id = (*cur_qp)->wrid[wqe_index +
					       (*cur_qp)->rq.max];
	} else if ((*cur_qp)->ibqp.srq) {
		struct mthca_srq *srq = to_msrq((*cur_qp)->ibqp.srq);
		u32 wqe = be32_to_cpu(cqe->wqe);
		wq = NULL;
		wqe_index = wqe >> srq->wqe_shift;
		entry->wr_id = srq->wrid[wqe_index];
		mthca_free_srq_wqe(srq, wqe);
	} else {
		s32 wqe;
		wq = &(*cur_qp)->rq;
		wqe = be32_to_cpu(cqe->wqe);
		wqe_index = wqe >> wq->wqe_shift;
		/*
		 * WQE addr == base - 1 might be reported in receive completion
		 * with error instead of (rq size - 1) by Sinai FW 1.0.800 and
		 * Arbel FW 5.1.400.  This bug should be fixed in later FW revs.
		 */
		if (unlikely(wqe_index < 0))
			wqe_index = wq->max - 1;
		entry->wr_id = (*cur_qp)->wrid[wqe_index];
	}

	if (wq) {
		if (wq->last_comp < wqe_index)
			wq->tail += wqe_index - wq->last_comp;
		else
			wq->tail += wqe_index + wq->max - wq->last_comp;

		wq->last_comp = wqe_index;
	}

	if (is_error) {
		handle_error_cqe(dev, cq, *cur_qp, wqe_index, is_send,
				 (struct mthca_err_cqe *) cqe,
				 entry, &free_cqe);
		goto out;
	}

	if (is_send) {
		entry->wc_flags = 0;
		switch (cqe->opcode) {
		case MTHCA_OPCODE_RDMA_WRITE:
			entry->opcode    = IB_WC_RDMA_WRITE;
			break;
		case MTHCA_OPCODE_RDMA_WRITE_IMM:
			entry->opcode    = IB_WC_RDMA_WRITE;
			entry->wc_flags |= IB_WC_WITH_IMM;
			break;
		case MTHCA_OPCODE_SEND:
			entry->opcode    = IB_WC_SEND;
			break;
		case MTHCA_OPCODE_SEND_IMM:
			entry->opcode    = IB_WC_SEND;
			entry->wc_flags |= IB_WC_WITH_IMM;
			break;
		case MTHCA_OPCODE_RDMA_READ:
			entry->opcode    = IB_WC_RDMA_READ;
			entry->byte_len  = be32_to_cpu(cqe->byte_cnt);
			break;
		case MTHCA_OPCODE_ATOMIC_CS:
			entry->opcode    = IB_WC_COMP_SWAP;
			entry->byte_len  = MTHCA_ATOMIC_BYTE_LEN;
			break;
		case MTHCA_OPCODE_ATOMIC_FA:
			entry->opcode    = IB_WC_FETCH_ADD;
			entry->byte_len  = MTHCA_ATOMIC_BYTE_LEN;
			break;
		default:
			entry->opcode = 0xFF;
			break;
		}
	} else {
		entry->byte_len = be32_to_cpu(cqe->byte_cnt);
		switch (cqe->opcode & 0x1f) {
		case IB_OPCODE_SEND_LAST_WITH_IMMEDIATE:
		case IB_OPCODE_SEND_ONLY_WITH_IMMEDIATE:
			entry->wc_flags = IB_WC_WITH_IMM;
			entry->ex.imm_data = cqe->imm_etype_pkey_eec;
			entry->opcode = IB_WC_RECV;
			break;
		case IB_OPCODE_RDMA_WRITE_LAST_WITH_IMMEDIATE:
		case IB_OPCODE_RDMA_WRITE_ONLY_WITH_IMMEDIATE:
			entry->wc_flags = IB_WC_WITH_IMM;
			entry->ex.imm_data = cqe->imm_etype_pkey_eec;
			entry->opcode = IB_WC_RECV_RDMA_WITH_IMM;
			break;
		default:
			entry->wc_flags = 0;
			entry->opcode = IB_WC_RECV;
			break;
		}
		entry->slid 	   = be16_to_cpu(cqe->rlid);
		entry->sl   	   = cqe->sl_ipok >> 4;
		entry->src_qp 	   = be32_to_cpu(cqe->rqpn) & 0xffffff;
		entry->dlid_path_bits = cqe->g_mlpath & 0x7f;
		entry->pkey_index  = be32_to_cpu(cqe->imm_etype_pkey_eec) >> 16;
		entry->wc_flags   |= cqe->g_mlpath & 0x80 ? IB_WC_GRH : 0;
		checksum = (be32_to_cpu(cqe->rqpn) >> 24) |
				((be32_to_cpu(cqe->my_ee) >> 16) & 0xff00);
		entry->wc_flags	  |=  (cqe->sl_ipok & 1 && checksum == 0xffff) ?
							IB_WC_IP_CSUM_OK : 0;
	}

	entry->status = IB_WC_SUCCESS;

 out:
	if (likely(free_cqe)) {
		set_cqe_hw(cqe);
		++(*freed);
		++cq->cons_index;
	}

	return err;
}

int mthca_poll_cq(struct ib_cq *ibcq, int num_entries,
		  struct ib_wc *entry)
{
	struct mthca_dev *dev = to_mdev(ibcq->device);
	struct mthca_cq *cq = to_mcq(ibcq);
	struct mthca_qp *qp = NULL;
	unsigned long flags;
	int err = 0;
	int freed = 0;
	int npolled;

	spin_lock_irqsave(&cq->lock, flags);

	npolled = 0;
repoll:
	while (npolled < num_entries) {
		err = mthca_poll_one(dev, cq, &qp,
				     &freed, entry + npolled);
		if (err)
			break;
		++npolled;
	}

	if (freed) {
		wmb();
		update_cons_index(dev, cq, freed);
	}

	/*
	 * If a CQ resize is in progress and we discovered that the
	 * old buffer is empty, then peek in the new buffer, and if
	 * it's not empty, switch to the new buffer and continue
	 * polling there.
	 */
	if (unlikely(err == -EAGAIN && cq->resize_buf &&
		     cq->resize_buf->state == CQ_RESIZE_READY)) {
		/*
		 * In Tavor mode, the hardware keeps the producer
		 * index modulo the CQ size.  Since we might be making
		 * the CQ bigger, we need to mask our consumer index
		 * using the size of the old CQ buffer before looking
		 * in the new CQ buffer.
		 */
		if (!mthca_is_memfree(dev))
			cq->cons_index &= cq->ibcq.cqe;

		if (cqe_sw(get_cqe_from_buf(&cq->resize_buf->buf,
					    cq->cons_index & cq->resize_buf->cqe))) {
			struct mthca_cq_buf tbuf;
			int tcqe;

			tbuf         = cq->buf;
			tcqe         = cq->ibcq.cqe;
			cq->buf      = cq->resize_buf->buf;
			cq->ibcq.cqe = cq->resize_buf->cqe;

			cq->resize_buf->buf   = tbuf;
			cq->resize_buf->cqe   = tcqe;
			cq->resize_buf->state = CQ_RESIZE_SWAPPED;

			goto repoll;
		}
	}

	spin_unlock_irqrestore(&cq->lock, flags);

	return err == 0 || err == -EAGAIN ? npolled : err;
}

int mthca_tavor_arm_cq(struct ib_cq *cq, enum ib_cq_notify_flags flags)
{
	u32 dbhi = ((flags & IB_CQ_SOLICITED_MASK) == IB_CQ_SOLICITED ?
		    MTHCA_TAVOR_CQ_DB_REQ_NOT_SOL :
		    MTHCA_TAVOR_CQ_DB_REQ_NOT) |
		to_mcq(cq)->cqn;

	mthca_write64(dbhi, 0xffffffff, to_mdev(cq->device)->kar + MTHCA_CQ_DOORBELL,
		      MTHCA_GET_DOORBELL_LOCK(&to_mdev(cq->device)->doorbell_lock));

	return 0;
}

int mthca_arbel_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags)
{
	struct mthca_cq *cq = to_mcq(ibcq);
	__be32 db_rec[2];
	u32 dbhi;
	u32 sn = cq->arm_sn & 3;

	db_rec[0] = cpu_to_be32(cq->cons_index);
	db_rec[1] = cpu_to_be32((cq->cqn << 8) | (2 << 5) | (sn << 3) |
				((flags & IB_CQ_SOLICITED_MASK) ==
				 IB_CQ_SOLICITED ? 1 : 2));

	mthca_write_db_rec(db_rec, cq->arm_db);

	/*
	 * Make sure that the doorbell record in host memory is
	 * written before ringing the doorbell via PCI MMIO.
	 */
	wmb();

	dbhi = (sn << 28) |
		((flags & IB_CQ_SOLICITED_MASK) == IB_CQ_SOLICITED ?
		 MTHCA_ARBEL_CQ_DB_REQ_NOT_SOL :
		 MTHCA_ARBEL_CQ_DB_REQ_NOT) | cq->cqn;

	mthca_write64(dbhi, cq->cons_index,
		      to_mdev(ibcq->device)->kar + MTHCA_CQ_DOORBELL,
		      MTHCA_GET_DOORBELL_LOCK(&to_mdev(ibcq->device)->doorbell_lock));

	return 0;
}

int mthca_init_cq(struct mthca_dev *dev, int nent,
		  struct mthca_ucontext *ctx, u32 pdn,
		  struct mthca_cq *cq)
{
	struct mthca_mailbox *mailbox;
	struct mthca_cq_context *cq_context;
	int err = -ENOMEM;

	cq->ibcq.cqe  = nent - 1;
	cq->is_kernel = !ctx;

	cq->cqn = mthca_alloc(&dev->cq_table.alloc);
	if (cq->cqn == -1)
		return -ENOMEM;

	if (mthca_is_memfree(dev)) {
		err = mthca_table_get(dev, dev->cq_table.table, cq->cqn);
		if (err)
			goto err_out;

		if (cq->is_kernel) {
			cq->arm_sn = 1;

			err = -ENOMEM;

			cq->set_ci_db_index = mthca_alloc_db(dev, MTHCA_DB_TYPE_CQ_SET_CI,
							     cq->cqn, &cq->set_ci_db);
			if (cq->set_ci_db_index < 0)
				goto err_out_icm;

			cq->arm_db_index = mthca_alloc_db(dev, MTHCA_DB_TYPE_CQ_ARM,
							  cq->cqn, &cq->arm_db);
			if (cq->arm_db_index < 0)
				goto err_out_ci;
		}
	}

	mailbox = mthca_alloc_mailbox(dev, GFP_KERNEL);
	if (IS_ERR(mailbox)) {
		err = PTR_ERR(mailbox);
		goto err_out_arm;
	}

	cq_context = mailbox->buf;

	if (cq->is_kernel) {
		err = mthca_alloc_cq_buf(dev, &cq->buf, nent);
		if (err)
			goto err_out_mailbox;
	}

	spin_lock_init(&cq->lock);
	cq->refcount = 1;
	init_waitqueue_head(&cq->wait);
	mutex_init(&cq->mutex);

	memset(cq_context, 0, sizeof *cq_context);
	cq_context->flags           = cpu_to_be32(MTHCA_CQ_STATUS_OK      |
						  MTHCA_CQ_STATE_DISARMED |
						  MTHCA_CQ_FLAG_TR);
	cq_context->logsize_usrpage = cpu_to_be32((ffs(nent) - 1) << 24);
	if (ctx)
		cq_context->logsize_usrpage |= cpu_to_be32(ctx->uar.index);
	else
		cq_context->logsize_usrpage |= cpu_to_be32(dev->driver_uar.index);
	cq_context->error_eqn       = cpu_to_be32(dev->eq_table.eq[MTHCA_EQ_ASYNC].eqn);
	cq_context->comp_eqn        = cpu_to_be32(dev->eq_table.eq[MTHCA_EQ_COMP].eqn);
	cq_context->pd              = cpu_to_be32(pdn);
	cq_context->lkey            = cpu_to_be32(cq->buf.mr.ibmr.lkey);
	cq_context->cqn             = cpu_to_be32(cq->cqn);

	if (mthca_is_memfree(dev)) {
		cq_context->ci_db    = cpu_to_be32(cq->set_ci_db_index);
		cq_context->state_db = cpu_to_be32(cq->arm_db_index);
	}

	err = mthca_SW2HW_CQ(dev, mailbox, cq->cqn);
	if (err) {
		mthca_warn(dev, "SW2HW_CQ failed (%d)\n", err);
		goto err_out_free_mr;
	}

	spin_lock_irq(&dev->cq_table.lock);
	err = mthca_array_set(&dev->cq_table.cq,
			      cq->cqn & (dev->limits.num_cqs - 1), cq);
	if (err) {
		spin_unlock_irq(&dev->cq_table.lock);
		goto err_out_free_mr;
	}
	spin_unlock_irq(&dev->cq_table.lock);

	cq->cons_index = 0;

	mthca_free_mailbox(dev, mailbox);

	return 0;

err_out_free_mr:
	if (cq->is_kernel)
		mthca_free_cq_buf(dev, &cq->buf, cq->ibcq.cqe);

err_out_mailbox:
	mthca_free_mailbox(dev, mailbox);

err_out_arm:
	if (cq->is_kernel && mthca_is_memfree(dev))
		mthca_free_db(dev, MTHCA_DB_TYPE_CQ_ARM, cq->arm_db_index);

err_out_ci:
	if (cq->is_kernel && mthca_is_memfree(dev))
		mthca_free_db(dev, MTHCA_DB_TYPE_CQ_SET_CI, cq->set_ci_db_index);

err_out_icm:
	mthca_table_put(dev, dev->cq_table.table, cq->cqn);

err_out:
	mthca_free(&dev->cq_table.alloc, cq->cqn);

	return err;
}

static inline int get_cq_refcount(struct mthca_dev *dev, struct mthca_cq *cq)
{
	int c;

	spin_lock_irq(&dev->cq_table.lock);
	c = cq->refcount;
	spin_unlock_irq(&dev->cq_table.lock);

	return c;
}

void mthca_free_cq(struct mthca_dev *dev,
		   struct mthca_cq *cq)
{
	struct mthca_mailbox *mailbox;
	int err;

	mailbox = mthca_alloc_mailbox(dev, GFP_KERNEL);
	if (IS_ERR(mailbox)) {
		mthca_warn(dev, "No memory for mailbox to free CQ.\n");
		return;
	}

	err = mthca_HW2SW_CQ(dev, mailbox, cq->cqn);
	if (err)
		mthca_warn(dev, "HW2SW_CQ failed (%d)\n", err);

	if (0) {
		__be32 *ctx = mailbox->buf;
		int j;

		printk(KERN_ERR "context for CQN %x (cons index %x, next sw %d)\n",
		       cq->cqn, cq->cons_index,
		       cq->is_kernel ? !!next_cqe_sw(cq) : 0);
		for (j = 0; j < 16; ++j)
			printk(KERN_ERR "[%2x] %08x\n", j * 4, be32_to_cpu(ctx[j]));
	}

	spin_lock_irq(&dev->cq_table.lock);
	mthca_array_clear(&dev->cq_table.cq,
			  cq->cqn & (dev->limits.num_cqs - 1));
	--cq->refcount;
	spin_unlock_irq(&dev->cq_table.lock);

	if (dev->mthca_flags & MTHCA_FLAG_MSI_X)
		synchronize_irq(dev->eq_table.eq[MTHCA_EQ_COMP].msi_x_vector);
	else
		synchronize_irq(dev->pdev->irq);

	wait_event(cq->wait, !get_cq_refcount(dev, cq));

	if (cq->is_kernel) {
		mthca_free_cq_buf(dev, &cq->buf, cq->ibcq.cqe);
		if (mthca_is_memfree(dev)) {
			mthca_free_db(dev, MTHCA_DB_TYPE_CQ_ARM,    cq->arm_db_index);
			mthca_free_db(dev, MTHCA_DB_TYPE_CQ_SET_CI, cq->set_ci_db_index);
		}
	}

	mthca_table_put(dev, dev->cq_table.table, cq->cqn);
	mthca_free(&dev->cq_table.alloc, cq->cqn);
	mthca_free_mailbox(dev, mailbox);
}

int mthca_init_cq_table(struct mthca_dev *dev)
{
	int err;

	spin_lock_init(&dev->cq_table.lock);

	err = mthca_alloc_init(&dev->cq_table.alloc,
			       dev->limits.num_cqs,
			       (1 << 24) - 1,
			       dev->limits.reserved_cqs);
	if (err)
		return err;

	err = mthca_array_init(&dev->cq_table.cq,
			       dev->limits.num_cqs);
	if (err)
		mthca_alloc_cleanup(&dev->cq_table.alloc);

	return err;
}

void mthca_cleanup_cq_table(struct mthca_dev *dev)
{
	mthca_array_cleanup(&dev->cq_table.cq, dev->limits.num_cqs);
	mthca_alloc_cleanup(&dev->cq_table.alloc);
}
