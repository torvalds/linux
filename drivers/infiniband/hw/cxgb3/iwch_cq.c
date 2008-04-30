/*
 * Copyright (c) 2006 Chelsio, Inc. All rights reserved.
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
#include "iwch_provider.h"
#include "iwch.h"

/*
 * Get one cq entry from cxio and map it to openib.
 *
 * Returns:
 *	0			EMPTY;
 *	1			cqe returned
 *	-EAGAIN		caller must try again
 *	any other -errno	fatal error
 */
static int iwch_poll_cq_one(struct iwch_dev *rhp, struct iwch_cq *chp,
			    struct ib_wc *wc)
{
	struct iwch_qp *qhp = NULL;
	struct t3_cqe cqe, *rd_cqe;
	struct t3_wq *wq;
	u32 credit = 0;
	u8 cqe_flushed;
	u64 cookie;
	int ret = 1;

	rd_cqe = cxio_next_cqe(&chp->cq);

	if (!rd_cqe)
		return 0;

	qhp = get_qhp(rhp, CQE_QPID(*rd_cqe));
	if (!qhp)
		wq = NULL;
	else {
		spin_lock(&qhp->lock);
		wq = &(qhp->wq);
	}
	ret = cxio_poll_cq(wq, &(chp->cq), &cqe, &cqe_flushed, &cookie,
				   &credit);
	if (t3a_device(chp->rhp) && credit) {
		PDBG("%s updating %d cq credits on id %d\n", __func__,
		     credit, chp->cq.cqid);
		cxio_hal_cq_op(&rhp->rdev, &chp->cq, CQ_CREDIT_UPDATE, credit);
	}

	if (ret) {
		ret = -EAGAIN;
		goto out;
	}
	ret = 1;

	wc->wr_id = cookie;
	wc->qp = &qhp->ibqp;
	wc->vendor_err = CQE_STATUS(cqe);

	PDBG("%s qpid 0x%x type %d opcode %d status 0x%x wrid hi 0x%x "
	     "lo 0x%x cookie 0x%llx\n", __func__,
	     CQE_QPID(cqe), CQE_TYPE(cqe),
	     CQE_OPCODE(cqe), CQE_STATUS(cqe), CQE_WRID_HI(cqe),
	     CQE_WRID_LOW(cqe), (unsigned long long) cookie);

	if (CQE_TYPE(cqe) == 0) {
		if (!CQE_STATUS(cqe))
			wc->byte_len = CQE_LEN(cqe);
		else
			wc->byte_len = 0;
		wc->opcode = IB_WC_RECV;
	} else {
		switch (CQE_OPCODE(cqe)) {
		case T3_RDMA_WRITE:
			wc->opcode = IB_WC_RDMA_WRITE;
			break;
		case T3_READ_REQ:
			wc->opcode = IB_WC_RDMA_READ;
			wc->byte_len = CQE_LEN(cqe);
			break;
		case T3_SEND:
		case T3_SEND_WITH_SE:
			wc->opcode = IB_WC_SEND;
			break;
		case T3_BIND_MW:
			wc->opcode = IB_WC_BIND_MW;
			break;

		/* these aren't supported yet */
		case T3_SEND_WITH_INV:
		case T3_SEND_WITH_SE_INV:
		case T3_LOCAL_INV:
		case T3_FAST_REGISTER:
		default:
			printk(KERN_ERR MOD "Unexpected opcode %d "
			       "in the CQE received for QPID=0x%0x\n",
			       CQE_OPCODE(cqe), CQE_QPID(cqe));
			ret = -EINVAL;
			goto out;
		}
	}

	if (cqe_flushed)
		wc->status = IB_WC_WR_FLUSH_ERR;
	else {

		switch (CQE_STATUS(cqe)) {
		case TPT_ERR_SUCCESS:
			wc->status = IB_WC_SUCCESS;
			break;
		case TPT_ERR_STAG:
			wc->status = IB_WC_LOC_ACCESS_ERR;
			break;
		case TPT_ERR_PDID:
			wc->status = IB_WC_LOC_PROT_ERR;
			break;
		case TPT_ERR_QPID:
		case TPT_ERR_ACCESS:
			wc->status = IB_WC_LOC_ACCESS_ERR;
			break;
		case TPT_ERR_WRAP:
			wc->status = IB_WC_GENERAL_ERR;
			break;
		case TPT_ERR_BOUND:
			wc->status = IB_WC_LOC_LEN_ERR;
			break;
		case TPT_ERR_INVALIDATE_SHARED_MR:
		case TPT_ERR_INVALIDATE_MR_WITH_MW_BOUND:
			wc->status = IB_WC_MW_BIND_ERR;
			break;
		case TPT_ERR_CRC:
		case TPT_ERR_MARKER:
		case TPT_ERR_PDU_LEN_ERR:
		case TPT_ERR_OUT_OF_RQE:
		case TPT_ERR_DDP_VERSION:
		case TPT_ERR_RDMA_VERSION:
		case TPT_ERR_DDP_QUEUE_NUM:
		case TPT_ERR_MSN:
		case TPT_ERR_TBIT:
		case TPT_ERR_MO:
		case TPT_ERR_MSN_RANGE:
		case TPT_ERR_IRD_OVERFLOW:
		case TPT_ERR_OPCODE:
			wc->status = IB_WC_FATAL_ERR;
			break;
		case TPT_ERR_SWFLUSH:
			wc->status = IB_WC_WR_FLUSH_ERR;
			break;
		default:
			printk(KERN_ERR MOD "Unexpected cqe_status 0x%x for "
			       "QPID=0x%0x\n", CQE_STATUS(cqe), CQE_QPID(cqe));
			ret = -EINVAL;
		}
	}
out:
	if (wq)
		spin_unlock(&qhp->lock);
	return ret;
}

int iwch_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc)
{
	struct iwch_dev *rhp;
	struct iwch_cq *chp;
	unsigned long flags;
	int npolled;
	int err = 0;

	chp = to_iwch_cq(ibcq);
	rhp = chp->rhp;

	spin_lock_irqsave(&chp->lock, flags);
	for (npolled = 0; npolled < num_entries; ++npolled) {
#ifdef DEBUG
		int i=0;
#endif

		/*
		 * Because T3 can post CQEs that are _not_ associated
		 * with a WR, we might have to poll again after removing
		 * one of these.
		 */
		do {
			err = iwch_poll_cq_one(rhp, chp, wc + npolled);
#ifdef DEBUG
			BUG_ON(++i > 1000);
#endif
		} while (err == -EAGAIN);
		if (err <= 0)
			break;
	}
	spin_unlock_irqrestore(&chp->lock, flags);

	if (err < 0)
		return err;
	else {
		return npolled;
	}
}
