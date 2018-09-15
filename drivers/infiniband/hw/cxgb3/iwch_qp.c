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
#include <linux/sched.h>
#include <linux/gfp.h>
#include "iwch_provider.h"
#include "iwch.h"
#include "iwch_cm.h"
#include "cxio_hal.h"
#include "cxio_resource.h"

#define NO_SUPPORT -1

static int build_rdma_send(union t3_wr *wqe, const struct ib_send_wr *wr,
			   u8 *flit_cnt)
{
	int i;
	u32 plen;

	switch (wr->opcode) {
	case IB_WR_SEND:
		if (wr->send_flags & IB_SEND_SOLICITED)
			wqe->send.rdmaop = T3_SEND_WITH_SE;
		else
			wqe->send.rdmaop = T3_SEND;
		wqe->send.rem_stag = 0;
		break;
	case IB_WR_SEND_WITH_INV:
		if (wr->send_flags & IB_SEND_SOLICITED)
			wqe->send.rdmaop = T3_SEND_WITH_SE_INV;
		else
			wqe->send.rdmaop = T3_SEND_WITH_INV;
		wqe->send.rem_stag = cpu_to_be32(wr->ex.invalidate_rkey);
		break;
	default:
		return -EINVAL;
	}
	if (wr->num_sge > T3_MAX_SGE)
		return -EINVAL;
	wqe->send.reserved[0] = 0;
	wqe->send.reserved[1] = 0;
	wqe->send.reserved[2] = 0;
	plen = 0;
	for (i = 0; i < wr->num_sge; i++) {
		if ((plen + wr->sg_list[i].length) < plen)
			return -EMSGSIZE;

		plen += wr->sg_list[i].length;
		wqe->send.sgl[i].stag = cpu_to_be32(wr->sg_list[i].lkey);
		wqe->send.sgl[i].len = cpu_to_be32(wr->sg_list[i].length);
		wqe->send.sgl[i].to = cpu_to_be64(wr->sg_list[i].addr);
	}
	wqe->send.num_sgle = cpu_to_be32(wr->num_sge);
	*flit_cnt = 4 + ((wr->num_sge) << 1);
	wqe->send.plen = cpu_to_be32(plen);
	return 0;
}

static int build_rdma_write(union t3_wr *wqe, const struct ib_send_wr *wr,
			    u8 *flit_cnt)
{
	int i;
	u32 plen;
	if (wr->num_sge > T3_MAX_SGE)
		return -EINVAL;
	wqe->write.rdmaop = T3_RDMA_WRITE;
	wqe->write.reserved[0] = 0;
	wqe->write.reserved[1] = 0;
	wqe->write.reserved[2] = 0;
	wqe->write.stag_sink = cpu_to_be32(rdma_wr(wr)->rkey);
	wqe->write.to_sink = cpu_to_be64(rdma_wr(wr)->remote_addr);

	if (wr->opcode == IB_WR_RDMA_WRITE_WITH_IMM) {
		plen = 4;
		wqe->write.sgl[0].stag = wr->ex.imm_data;
		wqe->write.sgl[0].len = cpu_to_be32(0);
		wqe->write.num_sgle = cpu_to_be32(0);
		*flit_cnt = 6;
	} else {
		plen = 0;
		for (i = 0; i < wr->num_sge; i++) {
			if ((plen + wr->sg_list[i].length) < plen) {
				return -EMSGSIZE;
			}
			plen += wr->sg_list[i].length;
			wqe->write.sgl[i].stag =
			    cpu_to_be32(wr->sg_list[i].lkey);
			wqe->write.sgl[i].len =
			    cpu_to_be32(wr->sg_list[i].length);
			wqe->write.sgl[i].to =
			    cpu_to_be64(wr->sg_list[i].addr);
		}
		wqe->write.num_sgle = cpu_to_be32(wr->num_sge);
		*flit_cnt = 5 + ((wr->num_sge) << 1);
	}
	wqe->write.plen = cpu_to_be32(plen);
	return 0;
}

static int build_rdma_read(union t3_wr *wqe, const struct ib_send_wr *wr,
			   u8 *flit_cnt)
{
	if (wr->num_sge > 1)
		return -EINVAL;
	wqe->read.rdmaop = T3_READ_REQ;
	if (wr->opcode == IB_WR_RDMA_READ_WITH_INV)
		wqe->read.local_inv = 1;
	else
		wqe->read.local_inv = 0;
	wqe->read.reserved[0] = 0;
	wqe->read.reserved[1] = 0;
	wqe->read.rem_stag = cpu_to_be32(rdma_wr(wr)->rkey);
	wqe->read.rem_to = cpu_to_be64(rdma_wr(wr)->remote_addr);
	wqe->read.local_stag = cpu_to_be32(wr->sg_list[0].lkey);
	wqe->read.local_len = cpu_to_be32(wr->sg_list[0].length);
	wqe->read.local_to = cpu_to_be64(wr->sg_list[0].addr);
	*flit_cnt = sizeof(struct t3_rdma_read_wr) >> 3;
	return 0;
}

static int build_memreg(union t3_wr *wqe, const struct ib_reg_wr *wr,
			u8 *flit_cnt, int *wr_cnt, struct t3_wq *wq)
{
	struct iwch_mr *mhp = to_iwch_mr(wr->mr);
	int i;
	__be64 *p;

	if (mhp->npages > T3_MAX_FASTREG_DEPTH)
		return -EINVAL;
	*wr_cnt = 1;
	wqe->fastreg.stag = cpu_to_be32(wr->key);
	wqe->fastreg.len = cpu_to_be32(mhp->ibmr.length);
	wqe->fastreg.va_base_hi = cpu_to_be32(mhp->ibmr.iova >> 32);
	wqe->fastreg.va_base_lo_fbo =
				cpu_to_be32(mhp->ibmr.iova & 0xffffffff);
	wqe->fastreg.page_type_perms = cpu_to_be32(
		V_FR_PAGE_COUNT(mhp->npages) |
		V_FR_PAGE_SIZE(ilog2(wr->mr->page_size) - 12) |
		V_FR_TYPE(TPT_VATO) |
		V_FR_PERMS(iwch_ib_to_tpt_access(wr->access)));
	p = &wqe->fastreg.pbl_addrs[0];
	for (i = 0; i < mhp->npages; i++, p++) {

		/* If we need a 2nd WR, then set it up */
		if (i == T3_MAX_FASTREG_FRAG) {
			*wr_cnt = 2;
			wqe = (union t3_wr *)(wq->queue +
				Q_PTR2IDX((wq->wptr+1), wq->size_log2));
			build_fw_riwrh((void *)wqe, T3_WR_FASTREG, 0,
			       Q_GENBIT(wq->wptr + 1, wq->size_log2),
			       0, 1 + mhp->npages - T3_MAX_FASTREG_FRAG,
			       T3_EOP);

			p = &wqe->pbl_frag.pbl_addrs[0];
		}
		*p = cpu_to_be64((u64)mhp->pages[i]);
	}
	*flit_cnt = 5 + mhp->npages;
	if (*flit_cnt > 15)
		*flit_cnt = 15;
	return 0;
}

static int build_inv_stag(union t3_wr *wqe, const struct ib_send_wr *wr,
			  u8 *flit_cnt)
{
	wqe->local_inv.stag = cpu_to_be32(wr->ex.invalidate_rkey);
	wqe->local_inv.reserved = 0;
	*flit_cnt = sizeof(struct t3_local_inv_wr) >> 3;
	return 0;
}

static int iwch_sgl2pbl_map(struct iwch_dev *rhp, struct ib_sge *sg_list,
			    u32 num_sgle, u32 * pbl_addr, u8 * page_size)
{
	int i;
	struct iwch_mr *mhp;
	u64 offset;
	for (i = 0; i < num_sgle; i++) {

		mhp = get_mhp(rhp, (sg_list[i].lkey) >> 8);
		if (!mhp) {
			pr_debug("%s %d\n", __func__, __LINE__);
			return -EIO;
		}
		if (!mhp->attr.state) {
			pr_debug("%s %d\n", __func__, __LINE__);
			return -EIO;
		}
		if (mhp->attr.zbva) {
			pr_debug("%s %d\n", __func__, __LINE__);
			return -EIO;
		}

		if (sg_list[i].addr < mhp->attr.va_fbo) {
			pr_debug("%s %d\n", __func__, __LINE__);
			return -EINVAL;
		}
		if (sg_list[i].addr + ((u64) sg_list[i].length) <
		    sg_list[i].addr) {
			pr_debug("%s %d\n", __func__, __LINE__);
			return -EINVAL;
		}
		if (sg_list[i].addr + ((u64) sg_list[i].length) >
		    mhp->attr.va_fbo + ((u64) mhp->attr.len)) {
			pr_debug("%s %d\n", __func__, __LINE__);
			return -EINVAL;
		}
		offset = sg_list[i].addr - mhp->attr.va_fbo;
		offset += mhp->attr.va_fbo &
			  ((1UL << (12 + mhp->attr.page_size)) - 1);
		pbl_addr[i] = ((mhp->attr.pbl_addr -
			        rhp->rdev.rnic_info.pbl_base) >> 3) +
			      (offset >> (12 + mhp->attr.page_size));
		page_size[i] = mhp->attr.page_size;
	}
	return 0;
}

static int build_rdma_recv(struct iwch_qp *qhp, union t3_wr *wqe,
			   const struct ib_recv_wr *wr)
{
	int i, err = 0;
	u32 pbl_addr[T3_MAX_SGE];
	u8 page_size[T3_MAX_SGE];

	err = iwch_sgl2pbl_map(qhp->rhp, wr->sg_list, wr->num_sge, pbl_addr,
			       page_size);
	if (err)
		return err;
	wqe->recv.pagesz[0] = page_size[0];
	wqe->recv.pagesz[1] = page_size[1];
	wqe->recv.pagesz[2] = page_size[2];
	wqe->recv.pagesz[3] = page_size[3];
	wqe->recv.num_sgle = cpu_to_be32(wr->num_sge);
	for (i = 0; i < wr->num_sge; i++) {
		wqe->recv.sgl[i].stag = cpu_to_be32(wr->sg_list[i].lkey);
		wqe->recv.sgl[i].len = cpu_to_be32(wr->sg_list[i].length);

		/* to in the WQE == the offset into the page */
		wqe->recv.sgl[i].to = cpu_to_be64(((u32)wr->sg_list[i].addr) &
				((1UL << (12 + page_size[i])) - 1));

		/* pbl_addr is the adapters address in the PBL */
		wqe->recv.pbl_addr[i] = cpu_to_be32(pbl_addr[i]);
	}
	for (; i < T3_MAX_SGE; i++) {
		wqe->recv.sgl[i].stag = 0;
		wqe->recv.sgl[i].len = 0;
		wqe->recv.sgl[i].to = 0;
		wqe->recv.pbl_addr[i] = 0;
	}
	qhp->wq.rq[Q_PTR2IDX(qhp->wq.rq_wptr,
			     qhp->wq.rq_size_log2)].wr_id = wr->wr_id;
	qhp->wq.rq[Q_PTR2IDX(qhp->wq.rq_wptr,
			     qhp->wq.rq_size_log2)].pbl_addr = 0;
	return 0;
}

static int build_zero_stag_recv(struct iwch_qp *qhp, union t3_wr *wqe,
				const struct ib_recv_wr *wr)
{
	int i;
	u32 pbl_addr;
	u32 pbl_offset;


	/*
	 * The T3 HW requires the PBL in the HW recv descriptor to reference
	 * a PBL entry.  So we allocate the max needed PBL memory here and pass
	 * it to the uP in the recv WR.  The uP will build the PBL and setup
	 * the HW recv descriptor.
	 */
	pbl_addr = cxio_hal_pblpool_alloc(&qhp->rhp->rdev, T3_STAG0_PBL_SIZE);
	if (!pbl_addr)
		return -ENOMEM;

	/*
	 * Compute the 8B aligned offset.
	 */
	pbl_offset = (pbl_addr - qhp->rhp->rdev.rnic_info.pbl_base) >> 3;

	wqe->recv.num_sgle = cpu_to_be32(wr->num_sge);

	for (i = 0; i < wr->num_sge; i++) {

		/*
		 * Use a 128MB page size. This and an imposed 128MB
		 * sge length limit allows us to require only a 2-entry HW
		 * PBL for each SGE.  This restriction is acceptable since
		 * since it is not possible to allocate 128MB of contiguous
		 * DMA coherent memory!
		 */
		if (wr->sg_list[i].length > T3_STAG0_MAX_PBE_LEN)
			return -EINVAL;
		wqe->recv.pagesz[i] = T3_STAG0_PAGE_SHIFT;

		/*
		 * T3 restricts a recv to all zero-stag or all non-zero-stag.
		 */
		if (wr->sg_list[i].lkey != 0)
			return -EINVAL;
		wqe->recv.sgl[i].stag = 0;
		wqe->recv.sgl[i].len = cpu_to_be32(wr->sg_list[i].length);
		wqe->recv.sgl[i].to = cpu_to_be64(wr->sg_list[i].addr);
		wqe->recv.pbl_addr[i] = cpu_to_be32(pbl_offset);
		pbl_offset += 2;
	}
	for (; i < T3_MAX_SGE; i++) {
		wqe->recv.pagesz[i] = 0;
		wqe->recv.sgl[i].stag = 0;
		wqe->recv.sgl[i].len = 0;
		wqe->recv.sgl[i].to = 0;
		wqe->recv.pbl_addr[i] = 0;
	}
	qhp->wq.rq[Q_PTR2IDX(qhp->wq.rq_wptr,
			     qhp->wq.rq_size_log2)].wr_id = wr->wr_id;
	qhp->wq.rq[Q_PTR2IDX(qhp->wq.rq_wptr,
			     qhp->wq.rq_size_log2)].pbl_addr = pbl_addr;
	return 0;
}

int iwch_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr,
		   const struct ib_send_wr **bad_wr)
{
	int err = 0;
	u8 uninitialized_var(t3_wr_flit_cnt);
	enum t3_wr_opcode t3_wr_opcode = 0;
	enum t3_wr_flags t3_wr_flags;
	struct iwch_qp *qhp;
	u32 idx;
	union t3_wr *wqe;
	u32 num_wrs;
	unsigned long flag;
	struct t3_swsq *sqp;
	int wr_cnt = 1;

	qhp = to_iwch_qp(ibqp);
	spin_lock_irqsave(&qhp->lock, flag);
	if (qhp->attr.state > IWCH_QP_STATE_RTS) {
		spin_unlock_irqrestore(&qhp->lock, flag);
		err = -EINVAL;
		goto out;
	}
	num_wrs = Q_FREECNT(qhp->wq.sq_rptr, qhp->wq.sq_wptr,
		  qhp->wq.sq_size_log2);
	if (num_wrs == 0) {
		spin_unlock_irqrestore(&qhp->lock, flag);
		err = -ENOMEM;
		goto out;
	}
	while (wr) {
		if (num_wrs == 0) {
			err = -ENOMEM;
			break;
		}
		idx = Q_PTR2IDX(qhp->wq.wptr, qhp->wq.size_log2);
		wqe = (union t3_wr *) (qhp->wq.queue + idx);
		t3_wr_flags = 0;
		if (wr->send_flags & IB_SEND_SOLICITED)
			t3_wr_flags |= T3_SOLICITED_EVENT_FLAG;
		if (wr->send_flags & IB_SEND_SIGNALED)
			t3_wr_flags |= T3_COMPLETION_FLAG;
		sqp = qhp->wq.sq +
		      Q_PTR2IDX(qhp->wq.sq_wptr, qhp->wq.sq_size_log2);
		switch (wr->opcode) {
		case IB_WR_SEND:
		case IB_WR_SEND_WITH_INV:
			if (wr->send_flags & IB_SEND_FENCE)
				t3_wr_flags |= T3_READ_FENCE_FLAG;
			t3_wr_opcode = T3_WR_SEND;
			err = build_rdma_send(wqe, wr, &t3_wr_flit_cnt);
			break;
		case IB_WR_RDMA_WRITE:
		case IB_WR_RDMA_WRITE_WITH_IMM:
			t3_wr_opcode = T3_WR_WRITE;
			err = build_rdma_write(wqe, wr, &t3_wr_flit_cnt);
			break;
		case IB_WR_RDMA_READ:
		case IB_WR_RDMA_READ_WITH_INV:
			t3_wr_opcode = T3_WR_READ;
			t3_wr_flags = 0; /* T3 reads are always signaled */
			err = build_rdma_read(wqe, wr, &t3_wr_flit_cnt);
			if (err)
				break;
			sqp->read_len = wqe->read.local_len;
			if (!qhp->wq.oldest_read)
				qhp->wq.oldest_read = sqp;
			break;
		case IB_WR_REG_MR:
			t3_wr_opcode = T3_WR_FASTREG;
			err = build_memreg(wqe, reg_wr(wr), &t3_wr_flit_cnt,
					   &wr_cnt, &qhp->wq);
			break;
		case IB_WR_LOCAL_INV:
			if (wr->send_flags & IB_SEND_FENCE)
				t3_wr_flags |= T3_LOCAL_FENCE_FLAG;
			t3_wr_opcode = T3_WR_INV_STAG;
			err = build_inv_stag(wqe, wr, &t3_wr_flit_cnt);
			break;
		default:
			pr_debug("%s post of type=%d TBD!\n", __func__,
				 wr->opcode);
			err = -EINVAL;
		}
		if (err)
			break;
		wqe->send.wrid.id0.hi = qhp->wq.sq_wptr;
		sqp->wr_id = wr->wr_id;
		sqp->opcode = wr2opcode(t3_wr_opcode);
		sqp->sq_wptr = qhp->wq.sq_wptr;
		sqp->complete = 0;
		sqp->signaled = (wr->send_flags & IB_SEND_SIGNALED);

		build_fw_riwrh((void *) wqe, t3_wr_opcode, t3_wr_flags,
			       Q_GENBIT(qhp->wq.wptr, qhp->wq.size_log2),
			       0, t3_wr_flit_cnt,
			       (wr_cnt == 1) ? T3_SOPEOP : T3_SOP);
		pr_debug("%s cookie 0x%llx wq idx 0x%x swsq idx %ld opcode %d\n",
			 __func__, (unsigned long long)wr->wr_id, idx,
			 Q_PTR2IDX(qhp->wq.sq_wptr, qhp->wq.sq_size_log2),
			 sqp->opcode);
		wr = wr->next;
		num_wrs--;
		qhp->wq.wptr += wr_cnt;
		++(qhp->wq.sq_wptr);
	}
	spin_unlock_irqrestore(&qhp->lock, flag);
	if (cxio_wq_db_enabled(&qhp->wq))
		ring_doorbell(qhp->wq.doorbell, qhp->wq.qpid);

out:
	if (err)
		*bad_wr = wr;
	return err;
}

int iwch_post_receive(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
		      const struct ib_recv_wr **bad_wr)
{
	int err = 0;
	struct iwch_qp *qhp;
	u32 idx;
	union t3_wr *wqe;
	u32 num_wrs;
	unsigned long flag;

	qhp = to_iwch_qp(ibqp);
	spin_lock_irqsave(&qhp->lock, flag);
	if (qhp->attr.state > IWCH_QP_STATE_RTS) {
		spin_unlock_irqrestore(&qhp->lock, flag);
		err = -EINVAL;
		goto out;
	}
	num_wrs = Q_FREECNT(qhp->wq.rq_rptr, qhp->wq.rq_wptr,
			    qhp->wq.rq_size_log2) - 1;
	if (!wr) {
		spin_unlock_irqrestore(&qhp->lock, flag);
		err = -ENOMEM;
		goto out;
	}
	while (wr) {
		if (wr->num_sge > T3_MAX_SGE) {
			err = -EINVAL;
			break;
		}
		idx = Q_PTR2IDX(qhp->wq.wptr, qhp->wq.size_log2);
		wqe = (union t3_wr *) (qhp->wq.queue + idx);
		if (num_wrs)
			if (wr->sg_list[0].lkey)
				err = build_rdma_recv(qhp, wqe, wr);
			else
				err = build_zero_stag_recv(qhp, wqe, wr);
		else
			err = -ENOMEM;

		if (err)
			break;

		build_fw_riwrh((void *) wqe, T3_WR_RCV, T3_COMPLETION_FLAG,
			       Q_GENBIT(qhp->wq.wptr, qhp->wq.size_log2),
			       0, sizeof(struct t3_receive_wr) >> 3, T3_SOPEOP);
		pr_debug("%s cookie 0x%llx idx 0x%x rq_wptr 0x%x rw_rptr 0x%x wqe %p\n",
			 __func__, (unsigned long long)wr->wr_id,
			 idx, qhp->wq.rq_wptr, qhp->wq.rq_rptr, wqe);
		++(qhp->wq.rq_wptr);
		++(qhp->wq.wptr);
		wr = wr->next;
		num_wrs--;
	}
	spin_unlock_irqrestore(&qhp->lock, flag);
	if (cxio_wq_db_enabled(&qhp->wq))
		ring_doorbell(qhp->wq.doorbell, qhp->wq.qpid);

out:
	if (err)
		*bad_wr = wr;
	return err;
}

static inline void build_term_codes(struct respQ_msg_t *rsp_msg,
				    u8 *layer_type, u8 *ecode)
{
	int status = TPT_ERR_INTERNAL_ERR;
	int tagged = 0;
	int opcode = -1;
	int rqtype = 0;
	int send_inv = 0;

	if (rsp_msg) {
		status = CQE_STATUS(rsp_msg->cqe);
		opcode = CQE_OPCODE(rsp_msg->cqe);
		rqtype = RQ_TYPE(rsp_msg->cqe);
		send_inv = (opcode == T3_SEND_WITH_INV) ||
		           (opcode == T3_SEND_WITH_SE_INV);
		tagged = (opcode == T3_RDMA_WRITE) ||
			 (rqtype && (opcode == T3_READ_RESP));
	}

	switch (status) {
	case TPT_ERR_STAG:
		if (send_inv) {
			*layer_type = LAYER_RDMAP|RDMAP_REMOTE_OP;
			*ecode = RDMAP_CANT_INV_STAG;
		} else {
			*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
			*ecode = RDMAP_INV_STAG;
		}
		break;
	case TPT_ERR_PDID:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
		if ((opcode == T3_SEND_WITH_INV) ||
		    (opcode == T3_SEND_WITH_SE_INV))
			*ecode = RDMAP_CANT_INV_STAG;
		else
			*ecode = RDMAP_STAG_NOT_ASSOC;
		break;
	case TPT_ERR_QPID:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
		*ecode = RDMAP_STAG_NOT_ASSOC;
		break;
	case TPT_ERR_ACCESS:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
		*ecode = RDMAP_ACC_VIOL;
		break;
	case TPT_ERR_WRAP:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
		*ecode = RDMAP_TO_WRAP;
		break;
	case TPT_ERR_BOUND:
		if (tagged) {
			*layer_type = LAYER_DDP|DDP_TAGGED_ERR;
			*ecode = DDPT_BASE_BOUNDS;
		} else {
			*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
			*ecode = RDMAP_BASE_BOUNDS;
		}
		break;
	case TPT_ERR_INVALIDATE_SHARED_MR:
	case TPT_ERR_INVALIDATE_MR_WITH_MW_BOUND:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_OP;
		*ecode = RDMAP_CANT_INV_STAG;
		break;
	case TPT_ERR_ECC:
	case TPT_ERR_ECC_PSTAG:
	case TPT_ERR_INTERNAL_ERR:
		*layer_type = LAYER_RDMAP|RDMAP_LOCAL_CATA;
		*ecode = 0;
		break;
	case TPT_ERR_OUT_OF_RQE:
		*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
		*ecode = DDPU_INV_MSN_NOBUF;
		break;
	case TPT_ERR_PBL_ADDR_BOUND:
		*layer_type = LAYER_DDP|DDP_TAGGED_ERR;
		*ecode = DDPT_BASE_BOUNDS;
		break;
	case TPT_ERR_CRC:
		*layer_type = LAYER_MPA|DDP_LLP;
		*ecode = MPA_CRC_ERR;
		break;
	case TPT_ERR_MARKER:
		*layer_type = LAYER_MPA|DDP_LLP;
		*ecode = MPA_MARKER_ERR;
		break;
	case TPT_ERR_PDU_LEN_ERR:
		*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
		*ecode = DDPU_MSG_TOOBIG;
		break;
	case TPT_ERR_DDP_VERSION:
		if (tagged) {
			*layer_type = LAYER_DDP|DDP_TAGGED_ERR;
			*ecode = DDPT_INV_VERS;
		} else {
			*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
			*ecode = DDPU_INV_VERS;
		}
		break;
	case TPT_ERR_RDMA_VERSION:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_OP;
		*ecode = RDMAP_INV_VERS;
		break;
	case TPT_ERR_OPCODE:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_OP;
		*ecode = RDMAP_INV_OPCODE;
		break;
	case TPT_ERR_DDP_QUEUE_NUM:
		*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
		*ecode = DDPU_INV_QN;
		break;
	case TPT_ERR_MSN:
	case TPT_ERR_MSN_GAP:
	case TPT_ERR_MSN_RANGE:
	case TPT_ERR_IRD_OVERFLOW:
		*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
		*ecode = DDPU_INV_MSN_RANGE;
		break;
	case TPT_ERR_TBIT:
		*layer_type = LAYER_DDP|DDP_LOCAL_CATA;
		*ecode = 0;
		break;
	case TPT_ERR_MO:
		*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
		*ecode = DDPU_INV_MO;
		break;
	default:
		*layer_type = LAYER_RDMAP|DDP_LOCAL_CATA;
		*ecode = 0;
		break;
	}
}

int iwch_post_zb_read(struct iwch_ep *ep)
{
	union t3_wr *wqe;
	struct sk_buff *skb;
	u8 flit_cnt = sizeof(struct t3_rdma_read_wr) >> 3;

	pr_debug("%s enter\n", __func__);
	skb = alloc_skb(40, GFP_KERNEL);
	if (!skb) {
		pr_err("%s cannot send zb_read!!\n", __func__);
		return -ENOMEM;
	}
	wqe = skb_put_zero(skb, sizeof(struct t3_rdma_read_wr));
	wqe->read.rdmaop = T3_READ_REQ;
	wqe->read.reserved[0] = 0;
	wqe->read.reserved[1] = 0;
	wqe->read.rem_stag = cpu_to_be32(1);
	wqe->read.rem_to = cpu_to_be64(1);
	wqe->read.local_stag = cpu_to_be32(1);
	wqe->read.local_len = cpu_to_be32(0);
	wqe->read.local_to = cpu_to_be64(1);
	wqe->send.wrh.op_seop_flags = cpu_to_be32(V_FW_RIWR_OP(T3_WR_READ));
	wqe->send.wrh.gen_tid_len = cpu_to_be32(V_FW_RIWR_TID(ep->hwtid)|
						V_FW_RIWR_LEN(flit_cnt));
	skb->priority = CPL_PRIORITY_DATA;
	return iwch_cxgb3_ofld_send(ep->com.qp->rhp->rdev.t3cdev_p, skb);
}

/*
 * This posts a TERMINATE with layer=RDMA, type=catastrophic.
 */
int iwch_post_terminate(struct iwch_qp *qhp, struct respQ_msg_t *rsp_msg)
{
	union t3_wr *wqe;
	struct terminate_message *term;
	struct sk_buff *skb;

	pr_debug("%s %d\n", __func__, __LINE__);
	skb = alloc_skb(40, GFP_ATOMIC);
	if (!skb) {
		pr_err("%s cannot send TERMINATE!\n", __func__);
		return -ENOMEM;
	}
	wqe = skb_put_zero(skb, 40);
	wqe->send.rdmaop = T3_TERMINATE;

	/* immediate data length */
	wqe->send.plen = htonl(4);

	/* immediate data starts here. */
	term = (struct terminate_message *)wqe->send.sgl;
	build_term_codes(rsp_msg, &term->layer_etype, &term->ecode);
	wqe->send.wrh.op_seop_flags = cpu_to_be32(V_FW_RIWR_OP(T3_WR_SEND) |
			 V_FW_RIWR_FLAGS(T3_COMPLETION_FLAG | T3_NOTIFY_FLAG));
	wqe->send.wrh.gen_tid_len = cpu_to_be32(V_FW_RIWR_TID(qhp->ep->hwtid));
	skb->priority = CPL_PRIORITY_DATA;
	return iwch_cxgb3_ofld_send(qhp->rhp->rdev.t3cdev_p, skb);
}

/*
 * Assumes qhp lock is held.
 */
static void __flush_qp(struct iwch_qp *qhp, struct iwch_cq *rchp,
				struct iwch_cq *schp)
	__releases(&qhp->lock)
	__acquires(&qhp->lock)
{
	int count;
	int flushed;

	lockdep_assert_held(&qhp->lock);

	pr_debug("%s qhp %p rchp %p schp %p\n", __func__, qhp, rchp, schp);
	/* take a ref on the qhp since we must release the lock */
	atomic_inc(&qhp->refcnt);
	spin_unlock(&qhp->lock);

	/* locking hierarchy: cq lock first, then qp lock. */
	spin_lock(&rchp->lock);
	spin_lock(&qhp->lock);
	cxio_flush_hw_cq(&rchp->cq);
	cxio_count_rcqes(&rchp->cq, &qhp->wq, &count);
	flushed = cxio_flush_rq(&qhp->wq, &rchp->cq, count);
	spin_unlock(&qhp->lock);
	spin_unlock(&rchp->lock);
	if (flushed) {
		spin_lock(&rchp->comp_handler_lock);
		(*rchp->ibcq.comp_handler)(&rchp->ibcq, rchp->ibcq.cq_context);
		spin_unlock(&rchp->comp_handler_lock);
	}

	/* locking hierarchy: cq lock first, then qp lock. */
	spin_lock(&schp->lock);
	spin_lock(&qhp->lock);
	cxio_flush_hw_cq(&schp->cq);
	cxio_count_scqes(&schp->cq, &qhp->wq, &count);
	flushed = cxio_flush_sq(&qhp->wq, &schp->cq, count);
	spin_unlock(&qhp->lock);
	spin_unlock(&schp->lock);
	if (flushed) {
		spin_lock(&schp->comp_handler_lock);
		(*schp->ibcq.comp_handler)(&schp->ibcq, schp->ibcq.cq_context);
		spin_unlock(&schp->comp_handler_lock);
	}

	/* deref */
	if (atomic_dec_and_test(&qhp->refcnt))
	        wake_up(&qhp->wait);

	spin_lock(&qhp->lock);
}

static void flush_qp(struct iwch_qp *qhp)
{
	struct iwch_cq *rchp, *schp;

	rchp = get_chp(qhp->rhp, qhp->attr.rcq);
	schp = get_chp(qhp->rhp, qhp->attr.scq);

	if (qhp->ibqp.uobject) {
		cxio_set_wq_in_error(&qhp->wq);
		cxio_set_cq_in_error(&rchp->cq);
		spin_lock(&rchp->comp_handler_lock);
		(*rchp->ibcq.comp_handler)(&rchp->ibcq, rchp->ibcq.cq_context);
		spin_unlock(&rchp->comp_handler_lock);
		if (schp != rchp) {
			cxio_set_cq_in_error(&schp->cq);
			spin_lock(&schp->comp_handler_lock);
			(*schp->ibcq.comp_handler)(&schp->ibcq,
						   schp->ibcq.cq_context);
			spin_unlock(&schp->comp_handler_lock);
		}
		return;
	}
	__flush_qp(qhp, rchp, schp);
}


/*
 * Return count of RECV WRs posted
 */
u16 iwch_rqes_posted(struct iwch_qp *qhp)
{
	union t3_wr *wqe = qhp->wq.queue;
	u16 count = 0;

	while (count < USHRT_MAX && fw_riwrh_opcode((struct fw_riwrh *)wqe) == T3_WR_RCV) {
		count++;
		wqe++;
	}
	pr_debug("%s qhp %p count %u\n", __func__, qhp, count);
	return count;
}

static int rdma_init(struct iwch_dev *rhp, struct iwch_qp *qhp,
				enum iwch_qp_attr_mask mask,
				struct iwch_qp_attributes *attrs)
{
	struct t3_rdma_init_attr init_attr;
	int ret;

	init_attr.tid = qhp->ep->hwtid;
	init_attr.qpid = qhp->wq.qpid;
	init_attr.pdid = qhp->attr.pd;
	init_attr.scqid = qhp->attr.scq;
	init_attr.rcqid = qhp->attr.rcq;
	init_attr.rq_addr = qhp->wq.rq_addr;
	init_attr.rq_size = 1 << qhp->wq.rq_size_log2;
	init_attr.mpaattrs = uP_RI_MPA_IETF_ENABLE |
		qhp->attr.mpa_attr.recv_marker_enabled |
		(qhp->attr.mpa_attr.xmit_marker_enabled << 1) |
		(qhp->attr.mpa_attr.crc_enabled << 2);

	init_attr.qpcaps = uP_RI_QP_RDMA_READ_ENABLE |
			   uP_RI_QP_RDMA_WRITE_ENABLE |
			   uP_RI_QP_BIND_ENABLE;
	if (!qhp->ibqp.uobject)
		init_attr.qpcaps |= uP_RI_QP_STAG0_ENABLE |
				    uP_RI_QP_FAST_REGISTER_ENABLE;

	init_attr.tcp_emss = qhp->ep->emss;
	init_attr.ord = qhp->attr.max_ord;
	init_attr.ird = qhp->attr.max_ird;
	init_attr.qp_dma_addr = qhp->wq.dma_addr;
	init_attr.qp_dma_size = (1UL << qhp->wq.size_log2);
	init_attr.rqe_count = iwch_rqes_posted(qhp);
	init_attr.flags = qhp->attr.mpa_attr.initiator ? MPA_INITIATOR : 0;
	init_attr.chan = qhp->ep->l2t->smt_idx;
	if (peer2peer) {
		init_attr.rtr_type = RTR_READ;
		if (init_attr.ord == 0 && qhp->attr.mpa_attr.initiator)
			init_attr.ord = 1;
		if (init_attr.ird == 0 && !qhp->attr.mpa_attr.initiator)
			init_attr.ird = 1;
	} else
		init_attr.rtr_type = 0;
	init_attr.irs = qhp->ep->rcv_seq;
	pr_debug("%s init_attr.rq_addr 0x%x init_attr.rq_size = %d flags 0x%x qpcaps 0x%x\n",
		 __func__,
		 init_attr.rq_addr, init_attr.rq_size,
		 init_attr.flags, init_attr.qpcaps);
	ret = cxio_rdma_init(&rhp->rdev, &init_attr);
	pr_debug("%s ret %d\n", __func__, ret);
	return ret;
}

int iwch_modify_qp(struct iwch_dev *rhp, struct iwch_qp *qhp,
				enum iwch_qp_attr_mask mask,
				struct iwch_qp_attributes *attrs,
				int internal)
{
	int ret = 0;
	struct iwch_qp_attributes newattr = qhp->attr;
	unsigned long flag;
	int disconnect = 0;
	int terminate = 0;
	int abort = 0;
	int free = 0;
	struct iwch_ep *ep = NULL;

	pr_debug("%s qhp %p qpid 0x%x ep %p state %d -> %d\n", __func__,
		 qhp, qhp->wq.qpid, qhp->ep, qhp->attr.state,
		 (mask & IWCH_QP_ATTR_NEXT_STATE) ? attrs->next_state : -1);

	spin_lock_irqsave(&qhp->lock, flag);

	/* Process attr changes if in IDLE */
	if (mask & IWCH_QP_ATTR_VALID_MODIFY) {
		if (qhp->attr.state != IWCH_QP_STATE_IDLE) {
			ret = -EIO;
			goto out;
		}
		if (mask & IWCH_QP_ATTR_ENABLE_RDMA_READ)
			newattr.enable_rdma_read = attrs->enable_rdma_read;
		if (mask & IWCH_QP_ATTR_ENABLE_RDMA_WRITE)
			newattr.enable_rdma_write = attrs->enable_rdma_write;
		if (mask & IWCH_QP_ATTR_ENABLE_RDMA_BIND)
			newattr.enable_bind = attrs->enable_bind;
		if (mask & IWCH_QP_ATTR_MAX_ORD) {
			if (attrs->max_ord >
			    rhp->attr.max_rdma_read_qp_depth) {
				ret = -EINVAL;
				goto out;
			}
			newattr.max_ord = attrs->max_ord;
		}
		if (mask & IWCH_QP_ATTR_MAX_IRD) {
			if (attrs->max_ird >
			    rhp->attr.max_rdma_reads_per_qp) {
				ret = -EINVAL;
				goto out;
			}
			newattr.max_ird = attrs->max_ird;
		}
		qhp->attr = newattr;
	}

	if (!(mask & IWCH_QP_ATTR_NEXT_STATE))
		goto out;
	if (qhp->attr.state == attrs->next_state)
		goto out;

	switch (qhp->attr.state) {
	case IWCH_QP_STATE_IDLE:
		switch (attrs->next_state) {
		case IWCH_QP_STATE_RTS:
			if (!(mask & IWCH_QP_ATTR_LLP_STREAM_HANDLE)) {
				ret = -EINVAL;
				goto out;
			}
			if (!(mask & IWCH_QP_ATTR_MPA_ATTR)) {
				ret = -EINVAL;
				goto out;
			}
			qhp->attr.mpa_attr = attrs->mpa_attr;
			qhp->attr.llp_stream_handle = attrs->llp_stream_handle;
			qhp->ep = qhp->attr.llp_stream_handle;
			qhp->attr.state = IWCH_QP_STATE_RTS;

			/*
			 * Ref the endpoint here and deref when we
			 * disassociate the endpoint from the QP.  This
			 * happens in CLOSING->IDLE transition or *->ERROR
			 * transition.
			 */
			get_ep(&qhp->ep->com);
			spin_unlock_irqrestore(&qhp->lock, flag);
			ret = rdma_init(rhp, qhp, mask, attrs);
			spin_lock_irqsave(&qhp->lock, flag);
			if (ret)
				goto err;
			break;
		case IWCH_QP_STATE_ERROR:
			qhp->attr.state = IWCH_QP_STATE_ERROR;
			flush_qp(qhp);
			break;
		default:
			ret = -EINVAL;
			goto out;
		}
		break;
	case IWCH_QP_STATE_RTS:
		switch (attrs->next_state) {
		case IWCH_QP_STATE_CLOSING:
			BUG_ON(kref_read(&qhp->ep->com.kref) < 2);
			qhp->attr.state = IWCH_QP_STATE_CLOSING;
			if (!internal) {
				abort=0;
				disconnect = 1;
				ep = qhp->ep;
				get_ep(&ep->com);
			}
			break;
		case IWCH_QP_STATE_TERMINATE:
			qhp->attr.state = IWCH_QP_STATE_TERMINATE;
			if (qhp->ibqp.uobject)
				cxio_set_wq_in_error(&qhp->wq);
			if (!internal)
				terminate = 1;
			break;
		case IWCH_QP_STATE_ERROR:
			qhp->attr.state = IWCH_QP_STATE_ERROR;
			if (!internal) {
				abort=1;
				disconnect = 1;
				ep = qhp->ep;
				get_ep(&ep->com);
			}
			goto err;
			break;
		default:
			ret = -EINVAL;
			goto out;
		}
		break;
	case IWCH_QP_STATE_CLOSING:
		if (!internal) {
			ret = -EINVAL;
			goto out;
		}
		switch (attrs->next_state) {
			case IWCH_QP_STATE_IDLE:
				flush_qp(qhp);
				qhp->attr.state = IWCH_QP_STATE_IDLE;
				qhp->attr.llp_stream_handle = NULL;
				put_ep(&qhp->ep->com);
				qhp->ep = NULL;
				wake_up(&qhp->wait);
				break;
			case IWCH_QP_STATE_ERROR:
				goto err;
			default:
				ret = -EINVAL;
				goto err;
		}
		break;
	case IWCH_QP_STATE_ERROR:
		if (attrs->next_state != IWCH_QP_STATE_IDLE) {
			ret = -EINVAL;
			goto out;
		}

		if (!Q_EMPTY(qhp->wq.sq_rptr, qhp->wq.sq_wptr) ||
		    !Q_EMPTY(qhp->wq.rq_rptr, qhp->wq.rq_wptr)) {
			ret = -EINVAL;
			goto out;
		}
		qhp->attr.state = IWCH_QP_STATE_IDLE;
		break;
	case IWCH_QP_STATE_TERMINATE:
		if (!internal) {
			ret = -EINVAL;
			goto out;
		}
		goto err;
		break;
	default:
		pr_err("%s in a bad state %d\n", __func__, qhp->attr.state);
		ret = -EINVAL;
		goto err;
		break;
	}
	goto out;
err:
	pr_debug("%s disassociating ep %p qpid 0x%x\n", __func__, qhp->ep,
		 qhp->wq.qpid);

	/* disassociate the LLP connection */
	qhp->attr.llp_stream_handle = NULL;
	ep = qhp->ep;
	qhp->ep = NULL;
	qhp->attr.state = IWCH_QP_STATE_ERROR;
	free=1;
	wake_up(&qhp->wait);
	BUG_ON(!ep);
	flush_qp(qhp);
out:
	spin_unlock_irqrestore(&qhp->lock, flag);

	if (terminate)
		iwch_post_terminate(qhp, NULL);

	/*
	 * If disconnect is 1, then we need to initiate a disconnect
	 * on the EP.  This can be a normal close (RTS->CLOSING) or
	 * an abnormal close (RTS/CLOSING->ERROR).
	 */
	if (disconnect) {
		iwch_ep_disconnect(ep, abort, GFP_KERNEL);
		put_ep(&ep->com);
	}

	/*
	 * If free is 1, then we've disassociated the EP from the QP
	 * and we need to dereference the EP.
	 */
	if (free)
		put_ep(&ep->com);

	pr_debug("%s exit state %d\n", __func__, qhp->attr.state);
	return ret;
}
