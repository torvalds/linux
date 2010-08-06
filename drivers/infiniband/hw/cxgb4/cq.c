/*
 * Copyright (c) 2009-2010 Chelsio, Inc. All rights reserved.
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
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
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

#include "iw_cxgb4.h"

static int destroy_cq(struct c4iw_rdev *rdev, struct t4_cq *cq,
		      struct c4iw_dev_ucontext *uctx)
{
	struct fw_ri_res_wr *res_wr;
	struct fw_ri_res *res;
	int wr_len;
	struct c4iw_wr_wait wr_wait;
	struct sk_buff *skb;
	int ret;

	wr_len = sizeof *res_wr + sizeof *res;
	skb = alloc_skb(wr_len, GFP_KERNEL | __GFP_NOFAIL);
	if (!skb)
		return -ENOMEM;
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, 0);

	res_wr = (struct fw_ri_res_wr *)__skb_put(skb, wr_len);
	memset(res_wr, 0, wr_len);
	res_wr->op_nres = cpu_to_be32(
			FW_WR_OP(FW_RI_RES_WR) |
			V_FW_RI_RES_WR_NRES(1) |
			FW_WR_COMPL(1));
	res_wr->len16_pkd = cpu_to_be32(DIV_ROUND_UP(wr_len, 16));
	res_wr->cookie = (u64)&wr_wait;
	res = res_wr->res;
	res->u.cq.restype = FW_RI_RES_TYPE_CQ;
	res->u.cq.op = FW_RI_RES_OP_RESET;
	res->u.cq.iqid = cpu_to_be32(cq->cqid);

	c4iw_init_wr_wait(&wr_wait);
	ret = c4iw_ofld_send(rdev, skb);
	if (!ret) {
		wait_event_timeout(wr_wait.wait, wr_wait.done, C4IW_WR_TO);
		if (!wr_wait.done) {
			printk(KERN_ERR MOD "Device %s not responding!\n",
			       pci_name(rdev->lldi.pdev));
			rdev->flags = T4_FATAL_ERROR;
			ret = -EIO;
		} else
			ret = wr_wait.ret;
	}

	kfree(cq->sw_queue);
	dma_free_coherent(&(rdev->lldi.pdev->dev),
			  cq->memsize, cq->queue,
			  dma_unmap_addr(cq, mapping));
	c4iw_put_cqid(rdev, cq->cqid, uctx);
	return ret;
}

static int create_cq(struct c4iw_rdev *rdev, struct t4_cq *cq,
		     struct c4iw_dev_ucontext *uctx)
{
	struct fw_ri_res_wr *res_wr;
	struct fw_ri_res *res;
	int wr_len;
	int user = (uctx != &rdev->uctx);
	struct c4iw_wr_wait wr_wait;
	int ret;
	struct sk_buff *skb;

	cq->cqid = c4iw_get_cqid(rdev, uctx);
	if (!cq->cqid) {
		ret = -ENOMEM;
		goto err1;
	}

	if (!user) {
		cq->sw_queue = kzalloc(cq->memsize, GFP_KERNEL);
		if (!cq->sw_queue) {
			ret = -ENOMEM;
			goto err2;
		}
	}
	cq->queue = dma_alloc_coherent(&rdev->lldi.pdev->dev, cq->memsize,
				       &cq->dma_addr, GFP_KERNEL);
	if (!cq->queue) {
		ret = -ENOMEM;
		goto err3;
	}
	dma_unmap_addr_set(cq, mapping, cq->dma_addr);
	memset(cq->queue, 0, cq->memsize);

	/* build fw_ri_res_wr */
	wr_len = sizeof *res_wr + sizeof *res;

	skb = alloc_skb(wr_len, GFP_KERNEL | __GFP_NOFAIL);
	if (!skb) {
		ret = -ENOMEM;
		goto err4;
	}
	set_wr_txq(skb, CPL_PRIORITY_CONTROL, 0);

	res_wr = (struct fw_ri_res_wr *)__skb_put(skb, wr_len);
	memset(res_wr, 0, wr_len);
	res_wr->op_nres = cpu_to_be32(
			FW_WR_OP(FW_RI_RES_WR) |
			V_FW_RI_RES_WR_NRES(1) |
			FW_WR_COMPL(1));
	res_wr->len16_pkd = cpu_to_be32(DIV_ROUND_UP(wr_len, 16));
	res_wr->cookie = (u64)&wr_wait;
	res = res_wr->res;
	res->u.cq.restype = FW_RI_RES_TYPE_CQ;
	res->u.cq.op = FW_RI_RES_OP_WRITE;
	res->u.cq.iqid = cpu_to_be32(cq->cqid);
	res->u.cq.iqandst_to_iqandstindex = cpu_to_be32(
			V_FW_RI_RES_WR_IQANUS(0) |
			V_FW_RI_RES_WR_IQANUD(1) |
			F_FW_RI_RES_WR_IQANDST |
			V_FW_RI_RES_WR_IQANDSTINDEX(*rdev->lldi.rxq_ids));
	res->u.cq.iqdroprss_to_iqesize = cpu_to_be16(
			F_FW_RI_RES_WR_IQDROPRSS |
			V_FW_RI_RES_WR_IQPCIECH(2) |
			V_FW_RI_RES_WR_IQINTCNTTHRESH(0) |
			F_FW_RI_RES_WR_IQO |
			V_FW_RI_RES_WR_IQESIZE(1));
	res->u.cq.iqsize = cpu_to_be16(cq->size);
	res->u.cq.iqaddr = cpu_to_be64(cq->dma_addr);

	c4iw_init_wr_wait(&wr_wait);

	ret = c4iw_ofld_send(rdev, skb);
	if (ret)
		goto err4;
	PDBG("%s wait_event wr_wait %p\n", __func__, &wr_wait);
	wait_event_timeout(wr_wait.wait, wr_wait.done, C4IW_WR_TO);
	if (!wr_wait.done) {
		printk(KERN_ERR MOD "Device %s not responding!\n",
		       pci_name(rdev->lldi.pdev));
		rdev->flags = T4_FATAL_ERROR;
		ret = -EIO;
	} else
		ret = wr_wait.ret;
	if (ret)
		goto err4;

	cq->gen = 1;
	cq->gts = rdev->lldi.gts_reg;
	cq->rdev = rdev;
	if (user) {
		cq->ugts = (u64)pci_resource_start(rdev->lldi.pdev, 2) +
					(cq->cqid << rdev->cqshift);
		cq->ugts &= PAGE_MASK;
	}
	return 0;
err4:
	dma_free_coherent(&rdev->lldi.pdev->dev, cq->memsize, cq->queue,
			  dma_unmap_addr(cq, mapping));
err3:
	kfree(cq->sw_queue);
err2:
	c4iw_put_cqid(rdev, cq->cqid, uctx);
err1:
	return ret;
}

static void insert_recv_cqe(struct t4_wq *wq, struct t4_cq *cq)
{
	struct t4_cqe cqe;

	PDBG("%s wq %p cq %p sw_cidx %u sw_pidx %u\n", __func__,
	     wq, cq, cq->sw_cidx, cq->sw_pidx);
	memset(&cqe, 0, sizeof(cqe));
	cqe.header = cpu_to_be32(V_CQE_STATUS(T4_ERR_SWFLUSH) |
				 V_CQE_OPCODE(FW_RI_SEND) |
				 V_CQE_TYPE(0) |
				 V_CQE_SWCQE(1) |
				 V_CQE_QPID(wq->rq.qid));
	cqe.bits_type_ts = cpu_to_be64(V_CQE_GENBIT((u64)cq->gen));
	cq->sw_queue[cq->sw_pidx] = cqe;
	t4_swcq_produce(cq);
}

int c4iw_flush_rq(struct t4_wq *wq, struct t4_cq *cq, int count)
{
	int flushed = 0;
	int in_use = wq->rq.in_use - count;

	BUG_ON(in_use < 0);
	PDBG("%s wq %p cq %p rq.in_use %u skip count %u\n", __func__,
	     wq, cq, wq->rq.in_use, count);
	while (in_use--) {
		insert_recv_cqe(wq, cq);
		flushed++;
	}
	return flushed;
}

static void insert_sq_cqe(struct t4_wq *wq, struct t4_cq *cq,
			  struct t4_swsqe *swcqe)
{
	struct t4_cqe cqe;

	PDBG("%s wq %p cq %p sw_cidx %u sw_pidx %u\n", __func__,
	     wq, cq, cq->sw_cidx, cq->sw_pidx);
	memset(&cqe, 0, sizeof(cqe));
	cqe.header = cpu_to_be32(V_CQE_STATUS(T4_ERR_SWFLUSH) |
				 V_CQE_OPCODE(swcqe->opcode) |
				 V_CQE_TYPE(1) |
				 V_CQE_SWCQE(1) |
				 V_CQE_QPID(wq->sq.qid));
	CQE_WRID_SQ_IDX(&cqe) = swcqe->idx;
	cqe.bits_type_ts = cpu_to_be64(V_CQE_GENBIT((u64)cq->gen));
	cq->sw_queue[cq->sw_pidx] = cqe;
	t4_swcq_produce(cq);
}

int c4iw_flush_sq(struct t4_wq *wq, struct t4_cq *cq, int count)
{
	int flushed = 0;
	struct t4_swsqe *swsqe = &wq->sq.sw_sq[wq->sq.cidx + count];
	int in_use = wq->sq.in_use - count;

	BUG_ON(in_use < 0);
	while (in_use--) {
		swsqe->signaled = 0;
		insert_sq_cqe(wq, cq, swsqe);
		swsqe++;
		if (swsqe == (wq->sq.sw_sq + wq->sq.size))
			swsqe = wq->sq.sw_sq;
		flushed++;
	}
	return flushed;
}

/*
 * Move all CQEs from the HWCQ into the SWCQ.
 */
void c4iw_flush_hw_cq(struct t4_cq *cq)
{
	struct t4_cqe *cqe = NULL, *swcqe;
	int ret;

	PDBG("%s cq %p cqid 0x%x\n", __func__, cq, cq->cqid);
	ret = t4_next_hw_cqe(cq, &cqe);
	while (!ret) {
		PDBG("%s flushing hwcq cidx 0x%x swcq pidx 0x%x\n",
		     __func__, cq->cidx, cq->sw_pidx);
		swcqe = &cq->sw_queue[cq->sw_pidx];
		*swcqe = *cqe;
		swcqe->header |= cpu_to_be32(V_CQE_SWCQE(1));
		t4_swcq_produce(cq);
		t4_hwcq_consume(cq);
		ret = t4_next_hw_cqe(cq, &cqe);
	}
}

static int cqe_completes_wr(struct t4_cqe *cqe, struct t4_wq *wq)
{
	if (CQE_OPCODE(cqe) == FW_RI_TERMINATE)
		return 0;

	if ((CQE_OPCODE(cqe) == FW_RI_RDMA_WRITE) && RQ_TYPE(cqe))
		return 0;

	if ((CQE_OPCODE(cqe) == FW_RI_READ_RESP) && SQ_TYPE(cqe))
		return 0;

	if (CQE_SEND_OPCODE(cqe) && RQ_TYPE(cqe) && t4_rq_empty(wq))
		return 0;
	return 1;
}

void c4iw_count_scqes(struct t4_cq *cq, struct t4_wq *wq, int *count)
{
	struct t4_cqe *cqe;
	u32 ptr;

	*count = 0;
	ptr = cq->sw_cidx;
	while (ptr != cq->sw_pidx) {
		cqe = &cq->sw_queue[ptr];
		if ((SQ_TYPE(cqe) || ((CQE_OPCODE(cqe) == FW_RI_READ_RESP) &&
				      wq->sq.oldest_read)) &&
		    (CQE_QPID(cqe) == wq->sq.qid))
			(*count)++;
		if (++ptr == cq->size)
			ptr = 0;
	}
	PDBG("%s cq %p count %d\n", __func__, cq, *count);
}

void c4iw_count_rcqes(struct t4_cq *cq, struct t4_wq *wq, int *count)
{
	struct t4_cqe *cqe;
	u32 ptr;

	*count = 0;
	PDBG("%s count zero %d\n", __func__, *count);
	ptr = cq->sw_cidx;
	while (ptr != cq->sw_pidx) {
		cqe = &cq->sw_queue[ptr];
		if (RQ_TYPE(cqe) && (CQE_OPCODE(cqe) != FW_RI_READ_RESP) &&
		    (CQE_QPID(cqe) == wq->rq.qid) && cqe_completes_wr(cqe, wq))
			(*count)++;
		if (++ptr == cq->size)
			ptr = 0;
	}
	PDBG("%s cq %p count %d\n", __func__, cq, *count);
}

static void flush_completed_wrs(struct t4_wq *wq, struct t4_cq *cq)
{
	struct t4_swsqe *swsqe;
	u16 ptr = wq->sq.cidx;
	int count = wq->sq.in_use;
	int unsignaled = 0;

	swsqe = &wq->sq.sw_sq[ptr];
	while (count--)
		if (!swsqe->signaled) {
			if (++ptr == wq->sq.size)
				ptr = 0;
			swsqe = &wq->sq.sw_sq[ptr];
			unsignaled++;
		} else if (swsqe->complete) {

			/*
			 * Insert this completed cqe into the swcq.
			 */
			PDBG("%s moving cqe into swcq sq idx %u cq idx %u\n",
			     __func__, ptr, cq->sw_pidx);
			swsqe->cqe.header |= htonl(V_CQE_SWCQE(1));
			cq->sw_queue[cq->sw_pidx] = swsqe->cqe;
			t4_swcq_produce(cq);
			swsqe->signaled = 0;
			wq->sq.in_use -= unsignaled;
			break;
		} else
			break;
}

static void create_read_req_cqe(struct t4_wq *wq, struct t4_cqe *hw_cqe,
				struct t4_cqe *read_cqe)
{
	read_cqe->u.scqe.cidx = wq->sq.oldest_read->idx;
	read_cqe->len = cpu_to_be32(wq->sq.oldest_read->read_len);
	read_cqe->header = htonl(V_CQE_QPID(CQE_QPID(hw_cqe)) |
				 V_CQE_SWCQE(SW_CQE(hw_cqe)) |
				 V_CQE_OPCODE(FW_RI_READ_REQ) |
				 V_CQE_TYPE(1));
	read_cqe->bits_type_ts = hw_cqe->bits_type_ts;
}

/*
 * Return a ptr to the next read wr in the SWSQ or NULL.
 */
static void advance_oldest_read(struct t4_wq *wq)
{

	u32 rptr = wq->sq.oldest_read - wq->sq.sw_sq + 1;

	if (rptr == wq->sq.size)
		rptr = 0;
	while (rptr != wq->sq.pidx) {
		wq->sq.oldest_read = &wq->sq.sw_sq[rptr];

		if (wq->sq.oldest_read->opcode == FW_RI_READ_REQ)
			return;
		if (++rptr == wq->sq.size)
			rptr = 0;
	}
	wq->sq.oldest_read = NULL;
}

/*
 * poll_cq
 *
 * Caller must:
 *     check the validity of the first CQE,
 *     supply the wq assicated with the qpid.
 *
 * credit: cq credit to return to sge.
 * cqe_flushed: 1 iff the CQE is flushed.
 * cqe: copy of the polled CQE.
 *
 * return value:
 *    0		    CQE returned ok.
 *    -EAGAIN       CQE skipped, try again.
 *    -EOVERFLOW    CQ overflow detected.
 */
static int poll_cq(struct t4_wq *wq, struct t4_cq *cq, struct t4_cqe *cqe,
		   u8 *cqe_flushed, u64 *cookie, u32 *credit)
{
	int ret = 0;
	struct t4_cqe *hw_cqe, read_cqe;

	*cqe_flushed = 0;
	*credit = 0;
	ret = t4_next_cqe(cq, &hw_cqe);
	if (ret)
		return ret;

	PDBG("%s CQE OVF %u qpid 0x%0x genbit %u type %u status 0x%0x"
	     " opcode 0x%0x len 0x%0x wrid_hi_stag 0x%x wrid_low_msn 0x%x\n",
	     __func__, CQE_OVFBIT(hw_cqe), CQE_QPID(hw_cqe),
	     CQE_GENBIT(hw_cqe), CQE_TYPE(hw_cqe), CQE_STATUS(hw_cqe),
	     CQE_OPCODE(hw_cqe), CQE_LEN(hw_cqe), CQE_WRID_HI(hw_cqe),
	     CQE_WRID_LOW(hw_cqe));

	/*
	 * skip cqe's not affiliated with a QP.
	 */
	if (wq == NULL) {
		ret = -EAGAIN;
		goto skip_cqe;
	}

	/*
	 * Gotta tweak READ completions:
	 *	1) the cqe doesn't contain the sq_wptr from the wr.
	 *	2) opcode not reflected from the wr.
	 *	3) read_len not reflected from the wr.
	 *	4) cq_type is RQ_TYPE not SQ_TYPE.
	 */
	if (RQ_TYPE(hw_cqe) && (CQE_OPCODE(hw_cqe) == FW_RI_READ_RESP)) {

		/*
		 * If this is an unsolicited read response, then the read
		 * was generated by the kernel driver as part of peer-2-peer
		 * connection setup.  So ignore the completion.
		 */
		if (!wq->sq.oldest_read) {
			if (CQE_STATUS(hw_cqe))
				t4_set_wq_in_error(wq);
			ret = -EAGAIN;
			goto skip_cqe;
		}

		/*
		 * Don't write to the HWCQ, so create a new read req CQE
		 * in local memory.
		 */
		create_read_req_cqe(wq, hw_cqe, &read_cqe);
		hw_cqe = &read_cqe;
		advance_oldest_read(wq);
	}

	if (CQE_STATUS(hw_cqe) || t4_wq_in_error(wq)) {
		*cqe_flushed = t4_wq_in_error(wq);
		t4_set_wq_in_error(wq);
		goto proc_cqe;
	}

	/*
	 * RECV completion.
	 */
	if (RQ_TYPE(hw_cqe)) {

		/*
		 * HW only validates 4 bits of MSN.  So we must validate that
		 * the MSN in the SEND is the next expected MSN.  If its not,
		 * then we complete this with T4_ERR_MSN and mark the wq in
		 * error.
		 */

		if (t4_rq_empty(wq)) {
			t4_set_wq_in_error(wq);
			ret = -EAGAIN;
			goto skip_cqe;
		}
		if (unlikely((CQE_WRID_MSN(hw_cqe) != (wq->rq.msn)))) {
			t4_set_wq_in_error(wq);
			hw_cqe->header |= htonl(V_CQE_STATUS(T4_ERR_MSN));
			goto proc_cqe;
		}
		goto proc_cqe;
	}

	/*
	 * If we get here its a send completion.
	 *
	 * Handle out of order completion. These get stuffed
	 * in the SW SQ. Then the SW SQ is walked to move any
	 * now in-order completions into the SW CQ.  This handles
	 * 2 cases:
	 *	1) reaping unsignaled WRs when the first subsequent
	 *	   signaled WR is completed.
	 *	2) out of order read completions.
	 */
	if (!SW_CQE(hw_cqe) && (CQE_WRID_SQ_IDX(hw_cqe) != wq->sq.cidx)) {
		struct t4_swsqe *swsqe;

		PDBG("%s out of order completion going in sw_sq at idx %u\n",
		     __func__, CQE_WRID_SQ_IDX(hw_cqe));
		swsqe = &wq->sq.sw_sq[CQE_WRID_SQ_IDX(hw_cqe)];
		swsqe->cqe = *hw_cqe;
		swsqe->complete = 1;
		ret = -EAGAIN;
		goto flush_wq;
	}

proc_cqe:
	*cqe = *hw_cqe;

	/*
	 * Reap the associated WR(s) that are freed up with this
	 * completion.
	 */
	if (SQ_TYPE(hw_cqe)) {
		wq->sq.cidx = CQE_WRID_SQ_IDX(hw_cqe);
		PDBG("%s completing sq idx %u\n", __func__, wq->sq.cidx);
		*cookie = wq->sq.sw_sq[wq->sq.cidx].wr_id;
		t4_sq_consume(wq);
	} else {
		PDBG("%s completing rq idx %u\n", __func__, wq->rq.cidx);
		*cookie = wq->rq.sw_rq[wq->rq.cidx].wr_id;
		BUG_ON(t4_rq_empty(wq));
		t4_rq_consume(wq);
	}

flush_wq:
	/*
	 * Flush any completed cqes that are now in-order.
	 */
	flush_completed_wrs(wq, cq);

skip_cqe:
	if (SW_CQE(hw_cqe)) {
		PDBG("%s cq %p cqid 0x%x skip sw cqe cidx %u\n",
		     __func__, cq, cq->cqid, cq->sw_cidx);
		t4_swcq_consume(cq);
	} else {
		PDBG("%s cq %p cqid 0x%x skip hw cqe cidx %u\n",
		     __func__, cq, cq->cqid, cq->cidx);
		t4_hwcq_consume(cq);
	}
	return ret;
}

/*
 * Get one cq entry from c4iw and map it to openib.
 *
 * Returns:
 *	0			cqe returned
 *	-ENODATA		EMPTY;
 *	-EAGAIN			caller must try again
 *	any other -errno	fatal error
 */
static int c4iw_poll_cq_one(struct c4iw_cq *chp, struct ib_wc *wc)
{
	struct c4iw_qp *qhp = NULL;
	struct t4_cqe cqe = {0, 0}, *rd_cqe;
	struct t4_wq *wq;
	u32 credit = 0;
	u8 cqe_flushed;
	u64 cookie = 0;
	int ret;

	ret = t4_next_cqe(&chp->cq, &rd_cqe);

	if (ret)
		return ret;

	qhp = get_qhp(chp->rhp, CQE_QPID(rd_cqe));
	if (!qhp)
		wq = NULL;
	else {
		spin_lock(&qhp->lock);
		wq = &(qhp->wq);
	}
	ret = poll_cq(wq, &(chp->cq), &cqe, &cqe_flushed, &cookie, &credit);
	if (ret)
		goto out;

	wc->wr_id = cookie;
	wc->qp = &qhp->ibqp;
	wc->vendor_err = CQE_STATUS(&cqe);
	wc->wc_flags = 0;

	PDBG("%s qpid 0x%x type %d opcode %d status 0x%x len %u wrid hi 0x%x "
	     "lo 0x%x cookie 0x%llx\n", __func__, CQE_QPID(&cqe),
	     CQE_TYPE(&cqe), CQE_OPCODE(&cqe), CQE_STATUS(&cqe), CQE_LEN(&cqe),
	     CQE_WRID_HI(&cqe), CQE_WRID_LOW(&cqe), (unsigned long long)cookie);

	if (CQE_TYPE(&cqe) == 0) {
		if (!CQE_STATUS(&cqe))
			wc->byte_len = CQE_LEN(&cqe);
		else
			wc->byte_len = 0;
		wc->opcode = IB_WC_RECV;
		if (CQE_OPCODE(&cqe) == FW_RI_SEND_WITH_INV ||
		    CQE_OPCODE(&cqe) == FW_RI_SEND_WITH_SE_INV) {
			wc->ex.invalidate_rkey = CQE_WRID_STAG(&cqe);
			wc->wc_flags |= IB_WC_WITH_INVALIDATE;
		}
	} else {
		switch (CQE_OPCODE(&cqe)) {
		case FW_RI_RDMA_WRITE:
			wc->opcode = IB_WC_RDMA_WRITE;
			break;
		case FW_RI_READ_REQ:
			wc->opcode = IB_WC_RDMA_READ;
			wc->byte_len = CQE_LEN(&cqe);
			break;
		case FW_RI_SEND_WITH_INV:
		case FW_RI_SEND_WITH_SE_INV:
			wc->opcode = IB_WC_SEND;
			wc->wc_flags |= IB_WC_WITH_INVALIDATE;
			break;
		case FW_RI_SEND:
		case FW_RI_SEND_WITH_SE:
			wc->opcode = IB_WC_SEND;
			break;
		case FW_RI_BIND_MW:
			wc->opcode = IB_WC_BIND_MW;
			break;

		case FW_RI_LOCAL_INV:
			wc->opcode = IB_WC_LOCAL_INV;
			break;
		case FW_RI_FAST_REGISTER:
			wc->opcode = IB_WC_FAST_REG_MR;
			break;
		default:
			printk(KERN_ERR MOD "Unexpected opcode %d "
			       "in the CQE received for QPID=0x%0x\n",
			       CQE_OPCODE(&cqe), CQE_QPID(&cqe));
			ret = -EINVAL;
			goto out;
		}
	}

	if (cqe_flushed)
		wc->status = IB_WC_WR_FLUSH_ERR;
	else {

		switch (CQE_STATUS(&cqe)) {
		case T4_ERR_SUCCESS:
			wc->status = IB_WC_SUCCESS;
			break;
		case T4_ERR_STAG:
			wc->status = IB_WC_LOC_ACCESS_ERR;
			break;
		case T4_ERR_PDID:
			wc->status = IB_WC_LOC_PROT_ERR;
			break;
		case T4_ERR_QPID:
		case T4_ERR_ACCESS:
			wc->status = IB_WC_LOC_ACCESS_ERR;
			break;
		case T4_ERR_WRAP:
			wc->status = IB_WC_GENERAL_ERR;
			break;
		case T4_ERR_BOUND:
			wc->status = IB_WC_LOC_LEN_ERR;
			break;
		case T4_ERR_INVALIDATE_SHARED_MR:
		case T4_ERR_INVALIDATE_MR_WITH_MW_BOUND:
			wc->status = IB_WC_MW_BIND_ERR;
			break;
		case T4_ERR_CRC:
		case T4_ERR_MARKER:
		case T4_ERR_PDU_LEN_ERR:
		case T4_ERR_OUT_OF_RQE:
		case T4_ERR_DDP_VERSION:
		case T4_ERR_RDMA_VERSION:
		case T4_ERR_DDP_QUEUE_NUM:
		case T4_ERR_MSN:
		case T4_ERR_TBIT:
		case T4_ERR_MO:
		case T4_ERR_MSN_RANGE:
		case T4_ERR_IRD_OVERFLOW:
		case T4_ERR_OPCODE:
			wc->status = IB_WC_FATAL_ERR;
			break;
		case T4_ERR_SWFLUSH:
			wc->status = IB_WC_WR_FLUSH_ERR;
			break;
		default:
			printk(KERN_ERR MOD
			       "Unexpected cqe_status 0x%x for QPID=0x%0x\n",
			       CQE_STATUS(&cqe), CQE_QPID(&cqe));
			ret = -EINVAL;
		}
	}
out:
	if (wq)
		spin_unlock(&qhp->lock);
	return ret;
}

int c4iw_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc)
{
	struct c4iw_cq *chp;
	unsigned long flags;
	int npolled;
	int err = 0;

	chp = to_c4iw_cq(ibcq);

	spin_lock_irqsave(&chp->lock, flags);
	for (npolled = 0; npolled < num_entries; ++npolled) {
		do {
			err = c4iw_poll_cq_one(chp, wc + npolled);
		} while (err == -EAGAIN);
		if (err)
			break;
	}
	spin_unlock_irqrestore(&chp->lock, flags);
	return !err || err == -ENODATA ? npolled : err;
}

int c4iw_destroy_cq(struct ib_cq *ib_cq)
{
	struct c4iw_cq *chp;
	struct c4iw_ucontext *ucontext;

	PDBG("%s ib_cq %p\n", __func__, ib_cq);
	chp = to_c4iw_cq(ib_cq);

	remove_handle(chp->rhp, &chp->rhp->cqidr, chp->cq.cqid);
	atomic_dec(&chp->refcnt);
	wait_event(chp->wait, !atomic_read(&chp->refcnt));

	ucontext = ib_cq->uobject ? to_c4iw_ucontext(ib_cq->uobject->context)
				  : NULL;
	destroy_cq(&chp->rhp->rdev, &chp->cq,
		   ucontext ? &ucontext->uctx : &chp->cq.rdev->uctx);
	kfree(chp);
	return 0;
}

struct ib_cq *c4iw_create_cq(struct ib_device *ibdev, int entries,
			     int vector, struct ib_ucontext *ib_context,
			     struct ib_udata *udata)
{
	struct c4iw_dev *rhp;
	struct c4iw_cq *chp;
	struct c4iw_create_cq_resp uresp;
	struct c4iw_ucontext *ucontext = NULL;
	int ret;
	size_t memsize, hwentries;
	struct c4iw_mm_entry *mm, *mm2;

	PDBG("%s ib_dev %p entries %d\n", __func__, ibdev, entries);

	rhp = to_c4iw_dev(ibdev);

	chp = kzalloc(sizeof(*chp), GFP_KERNEL);
	if (!chp)
		return ERR_PTR(-ENOMEM);

	if (ib_context)
		ucontext = to_c4iw_ucontext(ib_context);

	/* account for the status page. */
	entries++;

	/* IQ needs one extra entry to differentiate full vs empty. */
	entries++;

	/*
	 * entries must be multiple of 16 for HW.
	 */
	entries = roundup(entries, 16);

	/*
	 * Make actual HW queue 2x to avoid cdix_inc overflows.
	 */
	hwentries = entries * 2;

	/*
	 * Make HW queue at least 64 entries so GTS updates aren't too
	 * frequent.
	 */
	if (hwentries < 64)
		hwentries = 64;

	memsize = hwentries * sizeof *chp->cq.queue;

	/*
	 * memsize must be a multiple of the page size if its a user cq.
	 */
	if (ucontext) {
		memsize = roundup(memsize, PAGE_SIZE);
		hwentries = memsize / sizeof *chp->cq.queue;
	}
	chp->cq.size = hwentries;
	chp->cq.memsize = memsize;

	ret = create_cq(&rhp->rdev, &chp->cq,
			ucontext ? &ucontext->uctx : &rhp->rdev.uctx);
	if (ret)
		goto err1;

	chp->rhp = rhp;
	chp->cq.size--;				/* status page */
	chp->ibcq.cqe = entries - 2;
	spin_lock_init(&chp->lock);
	atomic_set(&chp->refcnt, 1);
	init_waitqueue_head(&chp->wait);
	ret = insert_handle(rhp, &rhp->cqidr, chp, chp->cq.cqid);
	if (ret)
		goto err2;

	if (ucontext) {
		mm = kmalloc(sizeof *mm, GFP_KERNEL);
		if (!mm)
			goto err3;
		mm2 = kmalloc(sizeof *mm2, GFP_KERNEL);
		if (!mm2)
			goto err4;

		uresp.qid_mask = rhp->rdev.cqmask;
		uresp.cqid = chp->cq.cqid;
		uresp.size = chp->cq.size;
		uresp.memsize = chp->cq.memsize;
		spin_lock(&ucontext->mmap_lock);
		uresp.key = ucontext->key;
		ucontext->key += PAGE_SIZE;
		uresp.gts_key = ucontext->key;
		ucontext->key += PAGE_SIZE;
		spin_unlock(&ucontext->mmap_lock);
		ret = ib_copy_to_udata(udata, &uresp, sizeof uresp);
		if (ret)
			goto err5;

		mm->key = uresp.key;
		mm->addr = virt_to_phys(chp->cq.queue);
		mm->len = chp->cq.memsize;
		insert_mmap(ucontext, mm);

		mm2->key = uresp.gts_key;
		mm2->addr = chp->cq.ugts;
		mm2->len = PAGE_SIZE;
		insert_mmap(ucontext, mm2);
	}
	PDBG("%s cqid 0x%0x chp %p size %u memsize %zu, dma_addr 0x%0llx\n",
	     __func__, chp->cq.cqid, chp, chp->cq.size,
	     chp->cq.memsize,
	     (unsigned long long) chp->cq.dma_addr);
	return &chp->ibcq;
err5:
	kfree(mm2);
err4:
	kfree(mm);
err3:
	remove_handle(rhp, &rhp->cqidr, chp->cq.cqid);
err2:
	destroy_cq(&chp->rhp->rdev, &chp->cq,
		   ucontext ? &ucontext->uctx : &rhp->rdev.uctx);
err1:
	kfree(chp);
	return ERR_PTR(ret);
}

int c4iw_resize_cq(struct ib_cq *cq, int cqe, struct ib_udata *udata)
{
	return -ENOSYS;
}

int c4iw_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags)
{
	struct c4iw_cq *chp;
	int ret;
	unsigned long flag;

	chp = to_c4iw_cq(ibcq);
	spin_lock_irqsave(&chp->lock, flag);
	ret = t4_arm_cq(&chp->cq,
			(flags & IB_CQ_SOLICITED_MASK) == IB_CQ_SOLICITED);
	spin_unlock_irqrestore(&chp->lock, flag);
	if (ret && !(flags & IB_CQ_REPORT_MISSED_EVENTS))
		ret = 0;
	return ret;
}
