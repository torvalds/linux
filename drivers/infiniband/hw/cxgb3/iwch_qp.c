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
#include "iwch_cm.h"
#include "cxio_hal.h"

#define NO_SUPPORT -1

static int iwch_build_rdma_send(union t3_wr *wqe, struct ib_send_wr *wr,
				u8 * flit_cnt)
{
	int i;
	u32 plen;

	switch (wr->opcode) {
	case IB_WR_SEND:
	case IB_WR_SEND_WITH_IMM:
		if (wr->send_flags & IB_SEND_SOLICITED)
			wqe->send.rdmaop = T3_SEND_WITH_SE;
		else
			wqe->send.rdmaop = T3_SEND;
		wqe->send.rem_stag = 0;
		break;
#if 0				/* Not currently supported */
	case TYPE_SEND_INVALIDATE:
	case TYPE_SEND_INVALIDATE_IMMEDIATE:
		wqe->send.rdmaop = T3_SEND_WITH_INV;
		wqe->send.rem_stag = cpu_to_be32(wr->wr.rdma.rkey);
		break;
	case TYPE_SEND_SE_INVALIDATE:
		wqe->send.rdmaop = T3_SEND_WITH_SE_INV;
		wqe->send.rem_stag = cpu_to_be32(wr->wr.rdma.rkey);
		break;
#endif
	default:
		break;
	}
	if (wr->num_sge > T3_MAX_SGE)
		return -EINVAL;
	wqe->send.reserved[0] = 0;
	wqe->send.reserved[1] = 0;
	wqe->send.reserved[2] = 0;
	if (wr->opcode == IB_WR_SEND_WITH_IMM) {
		plen = 4;
		wqe->send.sgl[0].stag = wr->imm_data;
		wqe->send.sgl[0].len = __constant_cpu_to_be32(0);
		wqe->send.num_sgle = __constant_cpu_to_be32(0);
		*flit_cnt = 5;
	} else {
		plen = 0;
		for (i = 0; i < wr->num_sge; i++) {
			if ((plen + wr->sg_list[i].length) < plen) {
				return -EMSGSIZE;
			}
			plen += wr->sg_list[i].length;
			wqe->send.sgl[i].stag =
			    cpu_to_be32(wr->sg_list[i].lkey);
			wqe->send.sgl[i].len =
			    cpu_to_be32(wr->sg_list[i].length);
			wqe->send.sgl[i].to = cpu_to_be64(wr->sg_list[i].addr);
		}
		wqe->send.num_sgle = cpu_to_be32(wr->num_sge);
		*flit_cnt = 4 + ((wr->num_sge) << 1);
	}
	wqe->send.plen = cpu_to_be32(plen);
	return 0;
}

static int iwch_build_rdma_write(union t3_wr *wqe, struct ib_send_wr *wr,
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
	wqe->write.stag_sink = cpu_to_be32(wr->wr.rdma.rkey);
	wqe->write.to_sink = cpu_to_be64(wr->wr.rdma.remote_addr);

	if (wr->opcode == IB_WR_RDMA_WRITE_WITH_IMM) {
		plen = 4;
		wqe->write.sgl[0].stag = wr->imm_data;
		wqe->write.sgl[0].len = __constant_cpu_to_be32(0);
		wqe->write.num_sgle = __constant_cpu_to_be32(0);
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

static int iwch_build_rdma_read(union t3_wr *wqe, struct ib_send_wr *wr,
				u8 *flit_cnt)
{
	if (wr->num_sge > 1)
		return -EINVAL;
	wqe->read.rdmaop = T3_READ_REQ;
	wqe->read.reserved[0] = 0;
	wqe->read.reserved[1] = 0;
	wqe->read.reserved[2] = 0;
	wqe->read.rem_stag = cpu_to_be32(wr->wr.rdma.rkey);
	wqe->read.rem_to = cpu_to_be64(wr->wr.rdma.remote_addr);
	wqe->read.local_stag = cpu_to_be32(wr->sg_list[0].lkey);
	wqe->read.local_len = cpu_to_be32(wr->sg_list[0].length);
	wqe->read.local_to = cpu_to_be64(wr->sg_list[0].addr);
	*flit_cnt = sizeof(struct t3_rdma_read_wr) >> 3;
	return 0;
}

/*
 * TBD: this is going to be moved to firmware. Missing pdid/qpid check for now.
 */
static int iwch_sgl2pbl_map(struct iwch_dev *rhp, struct ib_sge *sg_list,
			    u32 num_sgle, u32 * pbl_addr, u8 * page_size)
{
	int i;
	struct iwch_mr *mhp;
	u32 offset;
	for (i = 0; i < num_sgle; i++) {

		mhp = get_mhp(rhp, (sg_list[i].lkey) >> 8);
		if (!mhp) {
			PDBG("%s %d\n", __FUNCTION__, __LINE__);
			return -EIO;
		}
		if (!mhp->attr.state) {
			PDBG("%s %d\n", __FUNCTION__, __LINE__);
			return -EIO;
		}
		if (mhp->attr.zbva) {
			PDBG("%s %d\n", __FUNCTION__, __LINE__);
			return -EIO;
		}

		if (sg_list[i].addr < mhp->attr.va_fbo) {
			PDBG("%s %d\n", __FUNCTION__, __LINE__);
			return -EINVAL;
		}
		if (sg_list[i].addr + ((u64) sg_list[i].length) <
		    sg_list[i].addr) {
			PDBG("%s %d\n", __FUNCTION__, __LINE__);
			return -EINVAL;
		}
		if (sg_list[i].addr + ((u64) sg_list[i].length) >
		    mhp->attr.va_fbo + ((u64) mhp->attr.len)) {
			PDBG("%s %d\n", __FUNCTION__, __LINE__);
			return -EINVAL;
		}
		offset = sg_list[i].addr - mhp->attr.va_fbo;
		offset += ((u32) mhp->attr.va_fbo) %
		          (1UL << (12 + mhp->attr.page_size));
		pbl_addr[i] = ((mhp->attr.pbl_addr -
			        rhp->rdev.rnic_info.pbl_base) >> 3) +
			      (offset >> (12 + mhp->attr.page_size));
		page_size[i] = mhp->attr.page_size;
	}
	return 0;
}

static int iwch_build_rdma_recv(struct iwch_dev *rhp, union t3_wr *wqe,
				struct ib_recv_wr *wr)
{
	int i, err = 0;
	u32 pbl_addr[4];
	u8 page_size[4];
	if (wr->num_sge > T3_MAX_SGE)
		return -EINVAL;
	err = iwch_sgl2pbl_map(rhp, wr->sg_list, wr->num_sge, pbl_addr,
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
		wqe->recv.sgl[i].to = cpu_to_be64(((u32) wr->sg_list[i].addr) %
				(1UL << (12 + page_size[i])));

		/* pbl_addr is the adapters address in the PBL */
		wqe->recv.pbl_addr[i] = cpu_to_be32(pbl_addr[i]);
	}
	for (; i < T3_MAX_SGE; i++) {
		wqe->recv.sgl[i].stag = 0;
		wqe->recv.sgl[i].len = 0;
		wqe->recv.sgl[i].to = 0;
		wqe->recv.pbl_addr[i] = 0;
	}
	return 0;
}

int iwch_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
		      struct ib_send_wr **bad_wr)
{
	int err = 0;
	u8 t3_wr_flit_cnt;
	enum t3_wr_opcode t3_wr_opcode = 0;
	enum t3_wr_flags t3_wr_flags;
	struct iwch_qp *qhp;
	u32 idx;
	union t3_wr *wqe;
	u32 num_wrs;
	unsigned long flag;
	struct t3_swsq *sqp;

	qhp = to_iwch_qp(ibqp);
	spin_lock_irqsave(&qhp->lock, flag);
	if (qhp->attr.state > IWCH_QP_STATE_RTS) {
		spin_unlock_irqrestore(&qhp->lock, flag);
		return -EINVAL;
	}
	num_wrs = Q_FREECNT(qhp->wq.sq_rptr, qhp->wq.sq_wptr,
		  qhp->wq.sq_size_log2);
	if (num_wrs <= 0) {
		spin_unlock_irqrestore(&qhp->lock, flag);
		return -ENOMEM;
	}
	while (wr) {
		if (num_wrs == 0) {
			err = -ENOMEM;
			*bad_wr = wr;
			break;
		}
		idx = Q_PTR2IDX(qhp->wq.wptr, qhp->wq.size_log2);
		wqe = (union t3_wr *) (qhp->wq.queue + idx);
		t3_wr_flags = 0;
		if (wr->send_flags & IB_SEND_SOLICITED)
			t3_wr_flags |= T3_SOLICITED_EVENT_FLAG;
		if (wr->send_flags & IB_SEND_FENCE)
			t3_wr_flags |= T3_READ_FENCE_FLAG;
		if (wr->send_flags & IB_SEND_SIGNALED)
			t3_wr_flags |= T3_COMPLETION_FLAG;
		sqp = qhp->wq.sq +
		      Q_PTR2IDX(qhp->wq.sq_wptr, qhp->wq.sq_size_log2);
		switch (wr->opcode) {
		case IB_WR_SEND:
		case IB_WR_SEND_WITH_IMM:
			t3_wr_opcode = T3_WR_SEND;
			err = iwch_build_rdma_send(wqe, wr, &t3_wr_flit_cnt);
			break;
		case IB_WR_RDMA_WRITE:
		case IB_WR_RDMA_WRITE_WITH_IMM:
			t3_wr_opcode = T3_WR_WRITE;
			err = iwch_build_rdma_write(wqe, wr, &t3_wr_flit_cnt);
			break;
		case IB_WR_RDMA_READ:
			t3_wr_opcode = T3_WR_READ;
			t3_wr_flags = 0; /* T3 reads are always signaled */
			err = iwch_build_rdma_read(wqe, wr, &t3_wr_flit_cnt);
			if (err)
				break;
			sqp->read_len = wqe->read.local_len;
			if (!qhp->wq.oldest_read)
				qhp->wq.oldest_read = sqp;
			break;
		default:
			PDBG("%s post of type=%d TBD!\n", __FUNCTION__,
			     wr->opcode);
			err = -EINVAL;
		}
		if (err) {
			*bad_wr = wr;
			break;
		}
		wqe->send.wrid.id0.hi = qhp->wq.sq_wptr;
		sqp->wr_id = wr->wr_id;
		sqp->opcode = wr2opcode(t3_wr_opcode);
		sqp->sq_wptr = qhp->wq.sq_wptr;
		sqp->complete = 0;
		sqp->signaled = (wr->send_flags & IB_SEND_SIGNALED);

		build_fw_riwrh((void *) wqe, t3_wr_opcode, t3_wr_flags,
			       Q_GENBIT(qhp->wq.wptr, qhp->wq.size_log2),
			       0, t3_wr_flit_cnt);
		PDBG("%s cookie 0x%llx wq idx 0x%x swsq idx %ld opcode %d\n",
		     __FUNCTION__, (unsigned long long) wr->wr_id, idx,
		     Q_PTR2IDX(qhp->wq.sq_wptr, qhp->wq.sq_size_log2),
		     sqp->opcode);
		wr = wr->next;
		num_wrs--;
		++(qhp->wq.wptr);
		++(qhp->wq.sq_wptr);
	}
	spin_unlock_irqrestore(&qhp->lock, flag);
	ring_doorbell(qhp->wq.doorbell, qhp->wq.qpid);
	return err;
}

int iwch_post_receive(struct ib_qp *ibqp, struct ib_recv_wr *wr,
		      struct ib_recv_wr **bad_wr)
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
		return -EINVAL;
	}
	num_wrs = Q_FREECNT(qhp->wq.rq_rptr, qhp->wq.rq_wptr,
			    qhp->wq.rq_size_log2) - 1;
	if (!wr) {
		spin_unlock_irqrestore(&qhp->lock, flag);
		return -EINVAL;
	}
	while (wr) {
		idx = Q_PTR2IDX(qhp->wq.wptr, qhp->wq.size_log2);
		wqe = (union t3_wr *) (qhp->wq.queue + idx);
		if (num_wrs)
			err = iwch_build_rdma_recv(qhp->rhp, wqe, wr);
		else
			err = -ENOMEM;
		if (err) {
			*bad_wr = wr;
			break;
		}
		qhp->wq.rq[Q_PTR2IDX(qhp->wq.rq_wptr, qhp->wq.rq_size_log2)] =
			wr->wr_id;
		build_fw_riwrh((void *) wqe, T3_WR_RCV, T3_COMPLETION_FLAG,
			       Q_GENBIT(qhp->wq.wptr, qhp->wq.size_log2),
			       0, sizeof(struct t3_receive_wr) >> 3);
		PDBG("%s cookie 0x%llx idx 0x%x rq_wptr 0x%x rw_rptr 0x%x "
		     "wqe %p \n", __FUNCTION__, (unsigned long long) wr->wr_id,
		     idx, qhp->wq.rq_wptr, qhp->wq.rq_rptr, wqe);
		++(qhp->wq.rq_wptr);
		++(qhp->wq.wptr);
		wr = wr->next;
		num_wrs--;
	}
	spin_unlock_irqrestore(&qhp->lock, flag);
	ring_doorbell(qhp->wq.doorbell, qhp->wq.qpid);
	return err;
}

int iwch_bind_mw(struct ib_qp *qp,
			     struct ib_mw *mw,
			     struct ib_mw_bind *mw_bind)
{
	struct iwch_dev *rhp;
	struct iwch_mw *mhp;
	struct iwch_qp *qhp;
	union t3_wr *wqe;
	u32 pbl_addr;
	u8 page_size;
	u32 num_wrs;
	unsigned long flag;
	struct ib_sge sgl;
	int err=0;
	enum t3_wr_flags t3_wr_flags;
	u32 idx;
	struct t3_swsq *sqp;

	qhp = to_iwch_qp(qp);
	mhp = to_iwch_mw(mw);
	rhp = qhp->rhp;

	spin_lock_irqsave(&qhp->lock, flag);
	if (qhp->attr.state > IWCH_QP_STATE_RTS) {
		spin_unlock_irqrestore(&qhp->lock, flag);
		return -EINVAL;
	}
	num_wrs = Q_FREECNT(qhp->wq.sq_rptr, qhp->wq.sq_wptr,
			    qhp->wq.sq_size_log2);
	if ((num_wrs) <= 0) {
		spin_unlock_irqrestore(&qhp->lock, flag);
		return -ENOMEM;
	}
	idx = Q_PTR2IDX(qhp->wq.wptr, qhp->wq.size_log2);
	PDBG("%s: idx 0x%0x, mw 0x%p, mw_bind 0x%p\n", __FUNCTION__, idx,
	     mw, mw_bind);
	wqe = (union t3_wr *) (qhp->wq.queue + idx);

	t3_wr_flags = 0;
	if (mw_bind->send_flags & IB_SEND_SIGNALED)
		t3_wr_flags = T3_COMPLETION_FLAG;

	sgl.addr = mw_bind->addr;
	sgl.lkey = mw_bind->mr->lkey;
	sgl.length = mw_bind->length;
	wqe->bind.reserved = 0;
	wqe->bind.type = T3_VA_BASED_TO;

	/* TBD: check perms */
	wqe->bind.perms = iwch_ib_to_mwbind_access(mw_bind->mw_access_flags);
	wqe->bind.mr_stag = cpu_to_be32(mw_bind->mr->lkey);
	wqe->bind.mw_stag = cpu_to_be32(mw->rkey);
	wqe->bind.mw_len = cpu_to_be32(mw_bind->length);
	wqe->bind.mw_va = cpu_to_be64(mw_bind->addr);
	err = iwch_sgl2pbl_map(rhp, &sgl, 1, &pbl_addr, &page_size);
	if (err) {
		spin_unlock_irqrestore(&qhp->lock, flag);
	        return err;
	}
	wqe->send.wrid.id0.hi = qhp->wq.sq_wptr;
	sqp = qhp->wq.sq + Q_PTR2IDX(qhp->wq.sq_wptr, qhp->wq.sq_size_log2);
	sqp->wr_id = mw_bind->wr_id;
	sqp->opcode = T3_BIND_MW;
	sqp->sq_wptr = qhp->wq.sq_wptr;
	sqp->complete = 0;
	sqp->signaled = (mw_bind->send_flags & IB_SEND_SIGNALED);
	wqe->bind.mr_pbl_addr = cpu_to_be32(pbl_addr);
	wqe->bind.mr_pagesz = page_size;
	wqe->flit[T3_SQ_COOKIE_FLIT] = mw_bind->wr_id;
	build_fw_riwrh((void *)wqe, T3_WR_BIND, t3_wr_flags,
		       Q_GENBIT(qhp->wq.wptr, qhp->wq.size_log2), 0,
			        sizeof(struct t3_bind_mw_wr) >> 3);
	++(qhp->wq.wptr);
	++(qhp->wq.sq_wptr);
	spin_unlock_irqrestore(&qhp->lock, flag);

	ring_doorbell(qhp->wq.doorbell, qhp->wq.qpid);

	return err;
}

static void build_term_codes(int t3err, u8 *layer_type, u8 *ecode, int tagged)
{
	switch (t3err) {
	case TPT_ERR_STAG:
		if (tagged == 1) {
			*layer_type = LAYER_DDP|DDP_TAGGED_ERR;
			*ecode = DDPT_INV_STAG;
		} else if (tagged == 2) {
			*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
			*ecode = RDMAP_INV_STAG;
		}
		break;
	case TPT_ERR_PDID:
	case TPT_ERR_QPID:
	case TPT_ERR_ACCESS:
		if (tagged == 1) {
			*layer_type = LAYER_DDP|DDP_TAGGED_ERR;
			*ecode = DDPT_STAG_NOT_ASSOC;
		} else if (tagged == 2) {
			*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
			*ecode = RDMAP_STAG_NOT_ASSOC;
		}
		break;
	case TPT_ERR_WRAP:
		*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
		*ecode = RDMAP_TO_WRAP;
		break;
	case TPT_ERR_BOUND:
		if (tagged == 1) {
			*layer_type = LAYER_DDP|DDP_TAGGED_ERR;
			*ecode = DDPT_BASE_BOUNDS;
		} else if (tagged == 2) {
			*layer_type = LAYER_RDMAP|RDMAP_REMOTE_PROT;
			*ecode = RDMAP_BASE_BOUNDS;
		} else {
			*layer_type = LAYER_DDP|DDP_UNTAGGED_ERR;
			*ecode = DDPU_MSG_TOOBIG;
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

/*
 * This posts a TERMINATE with layer=RDMA, type=catastrophic.
 */
int iwch_post_terminate(struct iwch_qp *qhp, struct respQ_msg_t *rsp_msg)
{
	union t3_wr *wqe;
	struct terminate_message *term;
	int status;
	int tagged = 0;
	struct sk_buff *skb;

	PDBG("%s %d\n", __FUNCTION__, __LINE__);
	skb = alloc_skb(40, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_ERR "%s cannot send TERMINATE!\n", __FUNCTION__);
		return -ENOMEM;
	}
	wqe = (union t3_wr *)skb_put(skb, 40);
	memset(wqe, 0, 40);
	wqe->send.rdmaop = T3_TERMINATE;

	/* immediate data length */
	wqe->send.plen = htonl(4);

	/* immediate data starts here. */
	term = (struct terminate_message *)wqe->send.sgl;
	if (rsp_msg) {
		status = CQE_STATUS(rsp_msg->cqe);
		if (CQE_OPCODE(rsp_msg->cqe) == T3_RDMA_WRITE)
			tagged = 1;
		if ((CQE_OPCODE(rsp_msg->cqe) == T3_READ_REQ) ||
		    (CQE_OPCODE(rsp_msg->cqe) == T3_READ_RESP))
			tagged = 2;
	} else {
		status = TPT_ERR_INTERNAL_ERR;
	}
	build_term_codes(status, &term->layer_etype, &term->ecode, tagged);
	build_fw_riwrh((void *)wqe, T3_WR_SEND,
		       T3_COMPLETION_FLAG | T3_NOTIFY_FLAG, 1,
		       qhp->ep->hwtid, 5);
	skb->priority = CPL_PRIORITY_DATA;
	return cxgb3_ofld_send(qhp->rhp->rdev.t3cdev_p, skb);
}

/*
 * Assumes qhp lock is held.
 */
static void __flush_qp(struct iwch_qp *qhp, unsigned long *flag)
{
	struct iwch_cq *rchp, *schp;
	int count;

	rchp = get_chp(qhp->rhp, qhp->attr.rcq);
	schp = get_chp(qhp->rhp, qhp->attr.scq);

	PDBG("%s qhp %p rchp %p schp %p\n", __FUNCTION__, qhp, rchp, schp);
	/* take a ref on the qhp since we must release the lock */
	atomic_inc(&qhp->refcnt);
	spin_unlock_irqrestore(&qhp->lock, *flag);

	/* locking heirarchy: cq lock first, then qp lock. */
	spin_lock_irqsave(&rchp->lock, *flag);
	spin_lock(&qhp->lock);
	cxio_flush_hw_cq(&rchp->cq);
	cxio_count_rcqes(&rchp->cq, &qhp->wq, &count);
	cxio_flush_rq(&qhp->wq, &rchp->cq, count);
	spin_unlock(&qhp->lock);
	spin_unlock_irqrestore(&rchp->lock, *flag);

	/* locking heirarchy: cq lock first, then qp lock. */
	spin_lock_irqsave(&schp->lock, *flag);
	spin_lock(&qhp->lock);
	cxio_flush_hw_cq(&schp->cq);
	cxio_count_scqes(&schp->cq, &qhp->wq, &count);
	cxio_flush_sq(&qhp->wq, &schp->cq, count);
	spin_unlock(&qhp->lock);
	spin_unlock_irqrestore(&schp->lock, *flag);

	/* deref */
	if (atomic_dec_and_test(&qhp->refcnt))
	        wake_up(&qhp->wait);

	spin_lock_irqsave(&qhp->lock, *flag);
}

static void flush_qp(struct iwch_qp *qhp, unsigned long *flag)
{
	if (t3b_device(qhp->rhp))
		cxio_set_wq_in_error(&qhp->wq);
	else
		__flush_qp(qhp, flag);
}


/*
 * Return non zero if at least one RECV was pre-posted.
 */
static int rqes_posted(struct iwch_qp *qhp)
{
	return fw_riwrh_opcode((struct fw_riwrh *)qhp->wq.queue) == T3_WR_RCV;
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

	/*
	 * XXX - The IWCM doesn't quite handle getting these
	 * attrs set before going into RTS.  For now, just turn
	 * them on always...
	 */
#if 0
	init_attr.qpcaps = qhp->attr.enableRdmaRead |
		(qhp->attr.enableRdmaWrite << 1) |
		(qhp->attr.enableBind << 2) |
		(qhp->attr.enable_stag0_fastreg << 3) |
		(qhp->attr.enable_stag0_fastreg << 4);
#else
	init_attr.qpcaps = 0x1f;
#endif
	init_attr.tcp_emss = qhp->ep->emss;
	init_attr.ord = qhp->attr.max_ord;
	init_attr.ird = qhp->attr.max_ird;
	init_attr.qp_dma_addr = qhp->wq.dma_addr;
	init_attr.qp_dma_size = (1UL << qhp->wq.size_log2);
	init_attr.flags = rqes_posted(qhp) ? RECVS_POSTED : 0;
	PDBG("%s init_attr.rq_addr 0x%x init_attr.rq_size = %d "
	     "flags 0x%x qpcaps 0x%x\n", __FUNCTION__,
	     init_attr.rq_addr, init_attr.rq_size,
	     init_attr.flags, init_attr.qpcaps);
	ret = cxio_rdma_init(&rhp->rdev, &init_attr);
	PDBG("%s ret %d\n", __FUNCTION__, ret);
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

	PDBG("%s qhp %p qpid 0x%x ep %p state %d -> %d\n", __FUNCTION__,
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
			flush_qp(qhp, &flag);
			break;
		default:
			ret = -EINVAL;
			goto out;
		}
		break;
	case IWCH_QP_STATE_RTS:
		switch (attrs->next_state) {
		case IWCH_QP_STATE_CLOSING:
			BUG_ON(atomic_read(&qhp->ep->com.kref.refcount) < 2);
			qhp->attr.state = IWCH_QP_STATE_CLOSING;
			if (!internal) {
				abort=0;
				disconnect = 1;
				ep = qhp->ep;
			}
			break;
		case IWCH_QP_STATE_TERMINATE:
			qhp->attr.state = IWCH_QP_STATE_TERMINATE;
			if (t3b_device(qhp->rhp))
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
		memset(&qhp->attr, 0, sizeof(qhp->attr));
		break;
	case IWCH_QP_STATE_TERMINATE:
		if (!internal) {
			ret = -EINVAL;
			goto out;
		}
		goto err;
		break;
	default:
		printk(KERN_ERR "%s in a bad state %d\n",
		       __FUNCTION__, qhp->attr.state);
		ret = -EINVAL;
		goto err;
		break;
	}
	goto out;
err:
	PDBG("%s disassociating ep %p qpid 0x%x\n", __FUNCTION__, qhp->ep,
	     qhp->wq.qpid);

	/* disassociate the LLP connection */
	qhp->attr.llp_stream_handle = NULL;
	ep = qhp->ep;
	qhp->ep = NULL;
	qhp->attr.state = IWCH_QP_STATE_ERROR;
	free=1;
	wake_up(&qhp->wait);
	BUG_ON(!ep);
	flush_qp(qhp, &flag);
out:
	spin_unlock_irqrestore(&qhp->lock, flag);

	if (terminate)
		iwch_post_terminate(qhp, NULL);

	/*
	 * If disconnect is 1, then we need to initiate a disconnect
	 * on the EP.  This can be a normal close (RTS->CLOSING) or
	 * an abnormal close (RTS/CLOSING->ERROR).
	 */
	if (disconnect)
		iwch_ep_disconnect(ep, abort, GFP_KERNEL);

	/*
	 * If free is 1, then we've disassociated the EP from the QP
	 * and we need to dereference the EP.
	 */
	if (free)
		put_ep(&ep->com);

	PDBG("%s exit state %d\n", __FUNCTION__, qhp->attr.state);
	return ret;
}

static int quiesce_qp(struct iwch_qp *qhp)
{
	spin_lock_irq(&qhp->lock);
	iwch_quiesce_tid(qhp->ep);
	qhp->flags |= QP_QUIESCED;
	spin_unlock_irq(&qhp->lock);
	return 0;
}

static int resume_qp(struct iwch_qp *qhp)
{
	spin_lock_irq(&qhp->lock);
	iwch_resume_tid(qhp->ep);
	qhp->flags &= ~QP_QUIESCED;
	spin_unlock_irq(&qhp->lock);
	return 0;
}

int iwch_quiesce_qps(struct iwch_cq *chp)
{
	int i;
	struct iwch_qp *qhp;

	for (i=0; i < T3_MAX_NUM_QP; i++) {
		qhp = get_qhp(chp->rhp, i);
		if (!qhp)
			continue;
		if ((qhp->attr.rcq == chp->cq.cqid) && !qp_quiesced(qhp)) {
			quiesce_qp(qhp);
			continue;
		}
		if ((qhp->attr.scq == chp->cq.cqid) && !qp_quiesced(qhp))
			quiesce_qp(qhp);
	}
	return 0;
}

int iwch_resume_qps(struct iwch_cq *chp)
{
	int i;
	struct iwch_qp *qhp;

	for (i=0; i < T3_MAX_NUM_QP; i++) {
		qhp = get_qhp(chp->rhp, i);
		if (!qhp)
			continue;
		if ((qhp->attr.rcq == chp->cq.cqid) && qp_quiesced(qhp)) {
			resume_qp(qhp);
			continue;
		}
		if ((qhp->attr.scq == chp->cq.cqid) && qp_quiesced(qhp))
			resume_qp(qhp);
	}
	return 0;
}
