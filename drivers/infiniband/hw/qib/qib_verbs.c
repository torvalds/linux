/*
 * Copyright (c) 2012, 2013 Intel Corporation.  All rights reserved.
 * Copyright (c) 2006 - 2012 QLogic Corporation. All rights reserved.
 * Copyright (c) 2005, 2006 PathScale, Inc. All rights reserved.
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

#include <rdma/ib_mad.h>
#include <rdma/ib_user_verbs.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/utsname.h>
#include <linux/rculist.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/vmalloc.h>

#include "qib.h"
#include "qib_common.h"

static unsigned int ib_qib_qp_table_size = 256;
module_param_named(qp_table_size, ib_qib_qp_table_size, uint, S_IRUGO);
MODULE_PARM_DESC(qp_table_size, "QP table size");

unsigned int ib_qib_lkey_table_size = 16;
module_param_named(lkey_table_size, ib_qib_lkey_table_size, uint,
		   S_IRUGO);
MODULE_PARM_DESC(lkey_table_size,
		 "LKEY table size in bits (2^n, 1 <= n <= 23)");

static unsigned int ib_qib_max_pds = 0xFFFF;
module_param_named(max_pds, ib_qib_max_pds, uint, S_IRUGO);
MODULE_PARM_DESC(max_pds,
		 "Maximum number of protection domains to support");

static unsigned int ib_qib_max_ahs = 0xFFFF;
module_param_named(max_ahs, ib_qib_max_ahs, uint, S_IRUGO);
MODULE_PARM_DESC(max_ahs, "Maximum number of address handles to support");

unsigned int ib_qib_max_cqes = 0x2FFFF;
module_param_named(max_cqes, ib_qib_max_cqes, uint, S_IRUGO);
MODULE_PARM_DESC(max_cqes,
		 "Maximum number of completion queue entries to support");

unsigned int ib_qib_max_cqs = 0x1FFFF;
module_param_named(max_cqs, ib_qib_max_cqs, uint, S_IRUGO);
MODULE_PARM_DESC(max_cqs, "Maximum number of completion queues to support");

unsigned int ib_qib_max_qp_wrs = 0x3FFF;
module_param_named(max_qp_wrs, ib_qib_max_qp_wrs, uint, S_IRUGO);
MODULE_PARM_DESC(max_qp_wrs, "Maximum number of QP WRs to support");

unsigned int ib_qib_max_qps = 16384;
module_param_named(max_qps, ib_qib_max_qps, uint, S_IRUGO);
MODULE_PARM_DESC(max_qps, "Maximum number of QPs to support");

unsigned int ib_qib_max_sges = 0x60;
module_param_named(max_sges, ib_qib_max_sges, uint, S_IRUGO);
MODULE_PARM_DESC(max_sges, "Maximum number of SGEs to support");

unsigned int ib_qib_max_mcast_grps = 16384;
module_param_named(max_mcast_grps, ib_qib_max_mcast_grps, uint, S_IRUGO);
MODULE_PARM_DESC(max_mcast_grps,
		 "Maximum number of multicast groups to support");

unsigned int ib_qib_max_mcast_qp_attached = 16;
module_param_named(max_mcast_qp_attached, ib_qib_max_mcast_qp_attached,
		   uint, S_IRUGO);
MODULE_PARM_DESC(max_mcast_qp_attached,
		 "Maximum number of attached QPs to support");

unsigned int ib_qib_max_srqs = 1024;
module_param_named(max_srqs, ib_qib_max_srqs, uint, S_IRUGO);
MODULE_PARM_DESC(max_srqs, "Maximum number of SRQs to support");

unsigned int ib_qib_max_srq_sges = 128;
module_param_named(max_srq_sges, ib_qib_max_srq_sges, uint, S_IRUGO);
MODULE_PARM_DESC(max_srq_sges, "Maximum number of SRQ SGEs to support");

unsigned int ib_qib_max_srq_wrs = 0x1FFFF;
module_param_named(max_srq_wrs, ib_qib_max_srq_wrs, uint, S_IRUGO);
MODULE_PARM_DESC(max_srq_wrs, "Maximum number of SRQ WRs support");

static unsigned int ib_qib_disable_sma;
module_param_named(disable_sma, ib_qib_disable_sma, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(disable_sma, "Disable the SMA");

/*
 * Note that it is OK to post send work requests in the SQE and ERR
 * states; qib_do_send() will process them and generate error
 * completions as per IB 1.2 C10-96.
 */
const int ib_qib_state_ops[IB_QPS_ERR + 1] = {
	[IB_QPS_RESET] = 0,
	[IB_QPS_INIT] = QIB_POST_RECV_OK,
	[IB_QPS_RTR] = QIB_POST_RECV_OK | QIB_PROCESS_RECV_OK,
	[IB_QPS_RTS] = QIB_POST_RECV_OK | QIB_PROCESS_RECV_OK |
	    QIB_POST_SEND_OK | QIB_PROCESS_SEND_OK |
	    QIB_PROCESS_NEXT_SEND_OK,
	[IB_QPS_SQD] = QIB_POST_RECV_OK | QIB_PROCESS_RECV_OK |
	    QIB_POST_SEND_OK | QIB_PROCESS_SEND_OK,
	[IB_QPS_SQE] = QIB_POST_RECV_OK | QIB_PROCESS_RECV_OK |
	    QIB_POST_SEND_OK | QIB_FLUSH_SEND,
	[IB_QPS_ERR] = QIB_POST_RECV_OK | QIB_FLUSH_RECV |
	    QIB_POST_SEND_OK | QIB_FLUSH_SEND,
};

struct qib_ucontext {
	struct ib_ucontext ibucontext;
};

static inline struct qib_ucontext *to_iucontext(struct ib_ucontext
						  *ibucontext)
{
	return container_of(ibucontext, struct qib_ucontext, ibucontext);
}

/*
 * Translate ib_wr_opcode into ib_wc_opcode.
 */
const enum ib_wc_opcode ib_qib_wc_opcode[] = {
	[IB_WR_RDMA_WRITE] = IB_WC_RDMA_WRITE,
	[IB_WR_RDMA_WRITE_WITH_IMM] = IB_WC_RDMA_WRITE,
	[IB_WR_SEND] = IB_WC_SEND,
	[IB_WR_SEND_WITH_IMM] = IB_WC_SEND,
	[IB_WR_RDMA_READ] = IB_WC_RDMA_READ,
	[IB_WR_ATOMIC_CMP_AND_SWP] = IB_WC_COMP_SWAP,
	[IB_WR_ATOMIC_FETCH_AND_ADD] = IB_WC_FETCH_ADD
};

/*
 * System image GUID.
 */
__be64 ib_qib_sys_image_guid;

/**
 * qib_copy_sge - copy data to SGE memory
 * @ss: the SGE state
 * @data: the data to copy
 * @length: the length of the data
 */
void qib_copy_sge(struct qib_sge_state *ss, void *data, u32 length, int release)
{
	struct qib_sge *sge = &ss->sge;

	while (length) {
		u32 len = sge->length;

		if (len > length)
			len = length;
		if (len > sge->sge_length)
			len = sge->sge_length;
		BUG_ON(len == 0);
		memcpy(sge->vaddr, data, len);
		sge->vaddr += len;
		sge->length -= len;
		sge->sge_length -= len;
		if (sge->sge_length == 0) {
			if (release)
				qib_put_mr(sge->mr);
			if (--ss->num_sge)
				*sge = *ss->sg_list++;
		} else if (sge->length == 0 && sge->mr->lkey) {
			if (++sge->n >= QIB_SEGSZ) {
				if (++sge->m >= sge->mr->mapsz)
					break;
				sge->n = 0;
			}
			sge->vaddr =
				sge->mr->map[sge->m]->segs[sge->n].vaddr;
			sge->length =
				sge->mr->map[sge->m]->segs[sge->n].length;
		}
		data += len;
		length -= len;
	}
}

/**
 * qib_skip_sge - skip over SGE memory - XXX almost dup of prev func
 * @ss: the SGE state
 * @length: the number of bytes to skip
 */
void qib_skip_sge(struct qib_sge_state *ss, u32 length, int release)
{
	struct qib_sge *sge = &ss->sge;

	while (length) {
		u32 len = sge->length;

		if (len > length)
			len = length;
		if (len > sge->sge_length)
			len = sge->sge_length;
		BUG_ON(len == 0);
		sge->vaddr += len;
		sge->length -= len;
		sge->sge_length -= len;
		if (sge->sge_length == 0) {
			if (release)
				qib_put_mr(sge->mr);
			if (--ss->num_sge)
				*sge = *ss->sg_list++;
		} else if (sge->length == 0 && sge->mr->lkey) {
			if (++sge->n >= QIB_SEGSZ) {
				if (++sge->m >= sge->mr->mapsz)
					break;
				sge->n = 0;
			}
			sge->vaddr =
				sge->mr->map[sge->m]->segs[sge->n].vaddr;
			sge->length =
				sge->mr->map[sge->m]->segs[sge->n].length;
		}
		length -= len;
	}
}

/*
 * Count the number of DMA descriptors needed to send length bytes of data.
 * Don't modify the qib_sge_state to get the count.
 * Return zero if any of the segments is not aligned.
 */
static u32 qib_count_sge(struct qib_sge_state *ss, u32 length)
{
	struct qib_sge *sg_list = ss->sg_list;
	struct qib_sge sge = ss->sge;
	u8 num_sge = ss->num_sge;
	u32 ndesc = 1;  /* count the header */

	while (length) {
		u32 len = sge.length;

		if (len > length)
			len = length;
		if (len > sge.sge_length)
			len = sge.sge_length;
		BUG_ON(len == 0);
		if (((long) sge.vaddr & (sizeof(u32) - 1)) ||
		    (len != length && (len & (sizeof(u32) - 1)))) {
			ndesc = 0;
			break;
		}
		ndesc++;
		sge.vaddr += len;
		sge.length -= len;
		sge.sge_length -= len;
		if (sge.sge_length == 0) {
			if (--num_sge)
				sge = *sg_list++;
		} else if (sge.length == 0 && sge.mr->lkey) {
			if (++sge.n >= QIB_SEGSZ) {
				if (++sge.m >= sge.mr->mapsz)
					break;
				sge.n = 0;
			}
			sge.vaddr =
				sge.mr->map[sge.m]->segs[sge.n].vaddr;
			sge.length =
				sge.mr->map[sge.m]->segs[sge.n].length;
		}
		length -= len;
	}
	return ndesc;
}

/*
 * Copy from the SGEs to the data buffer.
 */
static void qib_copy_from_sge(void *data, struct qib_sge_state *ss, u32 length)
{
	struct qib_sge *sge = &ss->sge;

	while (length) {
		u32 len = sge->length;

		if (len > length)
			len = length;
		if (len > sge->sge_length)
			len = sge->sge_length;
		BUG_ON(len == 0);
		memcpy(data, sge->vaddr, len);
		sge->vaddr += len;
		sge->length -= len;
		sge->sge_length -= len;
		if (sge->sge_length == 0) {
			if (--ss->num_sge)
				*sge = *ss->sg_list++;
		} else if (sge->length == 0 && sge->mr->lkey) {
			if (++sge->n >= QIB_SEGSZ) {
				if (++sge->m >= sge->mr->mapsz)
					break;
				sge->n = 0;
			}
			sge->vaddr =
				sge->mr->map[sge->m]->segs[sge->n].vaddr;
			sge->length =
				sge->mr->map[sge->m]->segs[sge->n].length;
		}
		data += len;
		length -= len;
	}
}

/**
 * qib_post_one_send - post one RC, UC, or UD send work request
 * @qp: the QP to post on
 * @wr: the work request to send
 */
static int qib_post_one_send(struct qib_qp *qp, struct ib_send_wr *wr,
	int *scheduled)
{
	struct qib_swqe *wqe;
	u32 next;
	int i;
	int j;
	int acc;
	int ret;
	unsigned long flags;
	struct qib_lkey_table *rkt;
	struct qib_pd *pd;
	int avoid_schedule = 0;

	spin_lock_irqsave(&qp->s_lock, flags);

	/* Check that state is OK to post send. */
	if (unlikely(!(ib_qib_state_ops[qp->state] & QIB_POST_SEND_OK)))
		goto bail_inval;

	/* IB spec says that num_sge == 0 is OK. */
	if (wr->num_sge > qp->s_max_sge)
		goto bail_inval;

	/*
	 * Don't allow RDMA reads or atomic operations on UC or
	 * undefined operations.
	 * Make sure buffer is large enough to hold the result for atomics.
	 */
	if (wr->opcode == IB_WR_REG_MR) {
		if (qib_reg_mr(qp, reg_wr(wr)))
			goto bail_inval;
	} else if (qp->ibqp.qp_type == IB_QPT_UC) {
		if ((unsigned) wr->opcode >= IB_WR_RDMA_READ)
			goto bail_inval;
	} else if (qp->ibqp.qp_type != IB_QPT_RC) {
		/* Check IB_QPT_SMI, IB_QPT_GSI, IB_QPT_UD opcode */
		if (wr->opcode != IB_WR_SEND &&
		    wr->opcode != IB_WR_SEND_WITH_IMM)
			goto bail_inval;
		/* Check UD destination address PD */
		if (qp->ibqp.pd != ud_wr(wr)->ah->pd)
			goto bail_inval;
	} else if ((unsigned) wr->opcode > IB_WR_ATOMIC_FETCH_AND_ADD)
		goto bail_inval;
	else if (wr->opcode >= IB_WR_ATOMIC_CMP_AND_SWP &&
		   (wr->num_sge == 0 ||
		    wr->sg_list[0].length < sizeof(u64) ||
		    wr->sg_list[0].addr & (sizeof(u64) - 1)))
		goto bail_inval;
	else if (wr->opcode >= IB_WR_RDMA_READ && !qp->s_max_rd_atomic)
		goto bail_inval;

	next = qp->s_head + 1;
	if (next >= qp->s_size)
		next = 0;
	if (next == qp->s_last) {
		ret = -ENOMEM;
		goto bail;
	}

	rkt = &to_idev(qp->ibqp.device)->lk_table;
	pd = to_ipd(qp->ibqp.pd);
	wqe = get_swqe_ptr(qp, qp->s_head);

	if (qp->ibqp.qp_type != IB_QPT_UC &&
	    qp->ibqp.qp_type != IB_QPT_RC)
		memcpy(&wqe->ud_wr, ud_wr(wr), sizeof(wqe->ud_wr));
	else if (wr->opcode == IB_WR_REG_MR)
		memcpy(&wqe->reg_wr, reg_wr(wr),
			sizeof(wqe->reg_wr));
	else if (wr->opcode == IB_WR_RDMA_WRITE_WITH_IMM ||
		 wr->opcode == IB_WR_RDMA_WRITE ||
		 wr->opcode == IB_WR_RDMA_READ)
		memcpy(&wqe->rdma_wr, rdma_wr(wr), sizeof(wqe->rdma_wr));
	else if (wr->opcode == IB_WR_ATOMIC_CMP_AND_SWP ||
		 wr->opcode == IB_WR_ATOMIC_FETCH_AND_ADD)
		memcpy(&wqe->atomic_wr, atomic_wr(wr), sizeof(wqe->atomic_wr));
	else
		memcpy(&wqe->wr, wr, sizeof(wqe->wr));

	wqe->length = 0;
	j = 0;
	if (wr->num_sge) {
		acc = wr->opcode >= IB_WR_RDMA_READ ?
			IB_ACCESS_LOCAL_WRITE : 0;
		for (i = 0; i < wr->num_sge; i++) {
			u32 length = wr->sg_list[i].length;
			int ok;

			if (length == 0)
				continue;
			ok = qib_lkey_ok(rkt, pd, &wqe->sg_list[j],
					 &wr->sg_list[i], acc);
			if (!ok)
				goto bail_inval_free;
			wqe->length += length;
			j++;
		}
		wqe->wr.num_sge = j;
	}
	if (qp->ibqp.qp_type == IB_QPT_UC ||
	    qp->ibqp.qp_type == IB_QPT_RC) {
		if (wqe->length > 0x80000000U)
			goto bail_inval_free;
		if (wqe->length <= qp->pmtu)
			avoid_schedule = 1;
	} else if (wqe->length > (dd_from_ibdev(qp->ibqp.device)->pport +
				  qp->port_num - 1)->ibmtu) {
		goto bail_inval_free;
	} else {
		atomic_inc(&to_iah(ud_wr(wr)->ah)->refcount);
		avoid_schedule = 1;
	}
	wqe->ssn = qp->s_ssn++;
	qp->s_head = next;

	ret = 0;
	goto bail;

bail_inval_free:
	while (j) {
		struct qib_sge *sge = &wqe->sg_list[--j];

		qib_put_mr(sge->mr);
	}
bail_inval:
	ret = -EINVAL;
bail:
	if (!ret && !wr->next && !avoid_schedule &&
	 !qib_sdma_empty(
	   dd_from_ibdev(qp->ibqp.device)->pport + qp->port_num - 1)) {
		qib_schedule_send(qp);
		*scheduled = 1;
	}
	spin_unlock_irqrestore(&qp->s_lock, flags);
	return ret;
}

/**
 * qib_post_send - post a send on a QP
 * @ibqp: the QP to post the send on
 * @wr: the list of work requests to post
 * @bad_wr: the first bad WR is put here
 *
 * This may be called from interrupt context.
 */
static int qib_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
			 struct ib_send_wr **bad_wr)
{
	struct qib_qp *qp = to_iqp(ibqp);
	int err = 0;
	int scheduled = 0;

	for (; wr; wr = wr->next) {
		err = qib_post_one_send(qp, wr, &scheduled);
		if (err) {
			*bad_wr = wr;
			goto bail;
		}
	}

	/* Try to do the send work in the caller's context. */
	if (!scheduled)
		qib_do_send(&qp->s_work);

bail:
	return err;
}

/**
 * qib_post_receive - post a receive on a QP
 * @ibqp: the QP to post the receive on
 * @wr: the WR to post
 * @bad_wr: the first bad WR is put here
 *
 * This may be called from interrupt context.
 */
static int qib_post_receive(struct ib_qp *ibqp, struct ib_recv_wr *wr,
			    struct ib_recv_wr **bad_wr)
{
	struct qib_qp *qp = to_iqp(ibqp);
	struct qib_rwq *wq = qp->r_rq.wq;
	unsigned long flags;
	int ret;

	/* Check that state is OK to post receive. */
	if (!(ib_qib_state_ops[qp->state] & QIB_POST_RECV_OK) || !wq) {
		*bad_wr = wr;
		ret = -EINVAL;
		goto bail;
	}

	for (; wr; wr = wr->next) {
		struct qib_rwqe *wqe;
		u32 next;
		int i;

		if ((unsigned) wr->num_sge > qp->r_rq.max_sge) {
			*bad_wr = wr;
			ret = -EINVAL;
			goto bail;
		}

		spin_lock_irqsave(&qp->r_rq.lock, flags);
		next = wq->head + 1;
		if (next >= qp->r_rq.size)
			next = 0;
		if (next == wq->tail) {
			spin_unlock_irqrestore(&qp->r_rq.lock, flags);
			*bad_wr = wr;
			ret = -ENOMEM;
			goto bail;
		}

		wqe = get_rwqe_ptr(&qp->r_rq, wq->head);
		wqe->wr_id = wr->wr_id;
		wqe->num_sge = wr->num_sge;
		for (i = 0; i < wr->num_sge; i++)
			wqe->sg_list[i] = wr->sg_list[i];
		/* Make sure queue entry is written before the head index. */
		smp_wmb();
		wq->head = next;
		spin_unlock_irqrestore(&qp->r_rq.lock, flags);
	}
	ret = 0;

bail:
	return ret;
}

/**
 * qib_qp_rcv - processing an incoming packet on a QP
 * @rcd: the context pointer
 * @hdr: the packet header
 * @has_grh: true if the packet has a GRH
 * @data: the packet data
 * @tlen: the packet length
 * @qp: the QP the packet came on
 *
 * This is called from qib_ib_rcv() to process an incoming packet
 * for the given QP.
 * Called at interrupt level.
 */
static void qib_qp_rcv(struct qib_ctxtdata *rcd, struct qib_ib_header *hdr,
		       int has_grh, void *data, u32 tlen, struct qib_qp *qp)
{
	struct qib_ibport *ibp = &rcd->ppd->ibport_data;

	spin_lock(&qp->r_lock);

	/* Check for valid receive state. */
	if (!(ib_qib_state_ops[qp->state] & QIB_PROCESS_RECV_OK)) {
		ibp->n_pkt_drops++;
		goto unlock;
	}

	switch (qp->ibqp.qp_type) {
	case IB_QPT_SMI:
	case IB_QPT_GSI:
		if (ib_qib_disable_sma)
			break;
		/* FALLTHROUGH */
	case IB_QPT_UD:
		qib_ud_rcv(ibp, hdr, has_grh, data, tlen, qp);
		break;

	case IB_QPT_RC:
		qib_rc_rcv(rcd, hdr, has_grh, data, tlen, qp);
		break;

	case IB_QPT_UC:
		qib_uc_rcv(ibp, hdr, has_grh, data, tlen, qp);
		break;

	default:
		break;
	}

unlock:
	spin_unlock(&qp->r_lock);
}

/**
 * qib_ib_rcv - process an incoming packet
 * @rcd: the context pointer
 * @rhdr: the header of the packet
 * @data: the packet payload
 * @tlen: the packet length
 *
 * This is called from qib_kreceive() to process an incoming packet at
 * interrupt level. Tlen is the length of the header + data + CRC in bytes.
 */
void qib_ib_rcv(struct qib_ctxtdata *rcd, void *rhdr, void *data, u32 tlen)
{
	struct qib_pportdata *ppd = rcd->ppd;
	struct qib_ibport *ibp = &ppd->ibport_data;
	struct qib_ib_header *hdr = rhdr;
	struct qib_other_headers *ohdr;
	struct qib_qp *qp;
	u32 qp_num;
	int lnh;
	u8 opcode;
	u16 lid;

	/* 24 == LRH+BTH+CRC */
	if (unlikely(tlen < 24))
		goto drop;

	/* Check for a valid destination LID (see ch. 7.11.1). */
	lid = be16_to_cpu(hdr->lrh[1]);
	if (lid < QIB_MULTICAST_LID_BASE) {
		lid &= ~((1 << ppd->lmc) - 1);
		if (unlikely(lid != ppd->lid))
			goto drop;
	}

	/* Check for GRH */
	lnh = be16_to_cpu(hdr->lrh[0]) & 3;
	if (lnh == QIB_LRH_BTH)
		ohdr = &hdr->u.oth;
	else if (lnh == QIB_LRH_GRH) {
		u32 vtf;

		ohdr = &hdr->u.l.oth;
		if (hdr->u.l.grh.next_hdr != IB_GRH_NEXT_HDR)
			goto drop;
		vtf = be32_to_cpu(hdr->u.l.grh.version_tclass_flow);
		if ((vtf >> IB_GRH_VERSION_SHIFT) != IB_GRH_VERSION)
			goto drop;
	} else
		goto drop;

	opcode = (be32_to_cpu(ohdr->bth[0]) >> 24) & 0x7f;
#ifdef CONFIG_DEBUG_FS
	rcd->opstats->stats[opcode].n_bytes += tlen;
	rcd->opstats->stats[opcode].n_packets++;
#endif

	/* Get the destination QP number. */
	qp_num = be32_to_cpu(ohdr->bth[1]) & QIB_QPN_MASK;
	if (qp_num == QIB_MULTICAST_QPN) {
		struct qib_mcast *mcast;
		struct qib_mcast_qp *p;

		if (lnh != QIB_LRH_GRH)
			goto drop;
		mcast = qib_mcast_find(ibp, &hdr->u.l.grh.dgid);
		if (mcast == NULL)
			goto drop;
		this_cpu_inc(ibp->pmastats->n_multicast_rcv);
		list_for_each_entry_rcu(p, &mcast->qp_list, list)
			qib_qp_rcv(rcd, hdr, 1, data, tlen, p->qp);
		/*
		 * Notify qib_multicast_detach() if it is waiting for us
		 * to finish.
		 */
		if (atomic_dec_return(&mcast->refcount) <= 1)
			wake_up(&mcast->wait);
	} else {
		if (rcd->lookaside_qp) {
			if (rcd->lookaside_qpn != qp_num) {
				if (atomic_dec_and_test(
					&rcd->lookaside_qp->refcount))
					wake_up(
					 &rcd->lookaside_qp->wait);
				rcd->lookaside_qp = NULL;
			}
		}
		if (!rcd->lookaside_qp) {
			qp = qib_lookup_qpn(ibp, qp_num);
			if (!qp)
				goto drop;
			rcd->lookaside_qp = qp;
			rcd->lookaside_qpn = qp_num;
		} else
			qp = rcd->lookaside_qp;
		this_cpu_inc(ibp->pmastats->n_unicast_rcv);
		qib_qp_rcv(rcd, hdr, lnh == QIB_LRH_GRH, data, tlen, qp);
	}
	return;

drop:
	ibp->n_pkt_drops++;
}

/*
 * This is called from a timer to check for QPs
 * which need kernel memory in order to send a packet.
 */
static void mem_timer(unsigned long data)
{
	struct qib_ibdev *dev = (struct qib_ibdev *) data;
	struct list_head *list = &dev->memwait;
	struct qib_qp *qp = NULL;
	unsigned long flags;

	spin_lock_irqsave(&dev->pending_lock, flags);
	if (!list_empty(list)) {
		qp = list_entry(list->next, struct qib_qp, iowait);
		list_del_init(&qp->iowait);
		atomic_inc(&qp->refcount);
		if (!list_empty(list))
			mod_timer(&dev->mem_timer, jiffies + 1);
	}
	spin_unlock_irqrestore(&dev->pending_lock, flags);

	if (qp) {
		spin_lock_irqsave(&qp->s_lock, flags);
		if (qp->s_flags & QIB_S_WAIT_KMEM) {
			qp->s_flags &= ~QIB_S_WAIT_KMEM;
			qib_schedule_send(qp);
		}
		spin_unlock_irqrestore(&qp->s_lock, flags);
		if (atomic_dec_and_test(&qp->refcount))
			wake_up(&qp->wait);
	}
}

static void update_sge(struct qib_sge_state *ss, u32 length)
{
	struct qib_sge *sge = &ss->sge;

	sge->vaddr += length;
	sge->length -= length;
	sge->sge_length -= length;
	if (sge->sge_length == 0) {
		if (--ss->num_sge)
			*sge = *ss->sg_list++;
	} else if (sge->length == 0 && sge->mr->lkey) {
		if (++sge->n >= QIB_SEGSZ) {
			if (++sge->m >= sge->mr->mapsz)
				return;
			sge->n = 0;
		}
		sge->vaddr = sge->mr->map[sge->m]->segs[sge->n].vaddr;
		sge->length = sge->mr->map[sge->m]->segs[sge->n].length;
	}
}

#ifdef __LITTLE_ENDIAN
static inline u32 get_upper_bits(u32 data, u32 shift)
{
	return data >> shift;
}

static inline u32 set_upper_bits(u32 data, u32 shift)
{
	return data << shift;
}

static inline u32 clear_upper_bytes(u32 data, u32 n, u32 off)
{
	data <<= ((sizeof(u32) - n) * BITS_PER_BYTE);
	data >>= ((sizeof(u32) - n - off) * BITS_PER_BYTE);
	return data;
}
#else
static inline u32 get_upper_bits(u32 data, u32 shift)
{
	return data << shift;
}

static inline u32 set_upper_bits(u32 data, u32 shift)
{
	return data >> shift;
}

static inline u32 clear_upper_bytes(u32 data, u32 n, u32 off)
{
	data >>= ((sizeof(u32) - n) * BITS_PER_BYTE);
	data <<= ((sizeof(u32) - n - off) * BITS_PER_BYTE);
	return data;
}
#endif

static void copy_io(u32 __iomem *piobuf, struct qib_sge_state *ss,
		    u32 length, unsigned flush_wc)
{
	u32 extra = 0;
	u32 data = 0;
	u32 last;

	while (1) {
		u32 len = ss->sge.length;
		u32 off;

		if (len > length)
			len = length;
		if (len > ss->sge.sge_length)
			len = ss->sge.sge_length;
		BUG_ON(len == 0);
		/* If the source address is not aligned, try to align it. */
		off = (unsigned long)ss->sge.vaddr & (sizeof(u32) - 1);
		if (off) {
			u32 *addr = (u32 *)((unsigned long)ss->sge.vaddr &
					    ~(sizeof(u32) - 1));
			u32 v = get_upper_bits(*addr, off * BITS_PER_BYTE);
			u32 y;

			y = sizeof(u32) - off;
			if (len > y)
				len = y;
			if (len + extra >= sizeof(u32)) {
				data |= set_upper_bits(v, extra *
						       BITS_PER_BYTE);
				len = sizeof(u32) - extra;
				if (len == length) {
					last = data;
					break;
				}
				__raw_writel(data, piobuf);
				piobuf++;
				extra = 0;
				data = 0;
			} else {
				/* Clear unused upper bytes */
				data |= clear_upper_bytes(v, len, extra);
				if (len == length) {
					last = data;
					break;
				}
				extra += len;
			}
		} else if (extra) {
			/* Source address is aligned. */
			u32 *addr = (u32 *) ss->sge.vaddr;
			int shift = extra * BITS_PER_BYTE;
			int ushift = 32 - shift;
			u32 l = len;

			while (l >= sizeof(u32)) {
				u32 v = *addr;

				data |= set_upper_bits(v, shift);
				__raw_writel(data, piobuf);
				data = get_upper_bits(v, ushift);
				piobuf++;
				addr++;
				l -= sizeof(u32);
			}
			/*
			 * We still have 'extra' number of bytes leftover.
			 */
			if (l) {
				u32 v = *addr;

				if (l + extra >= sizeof(u32)) {
					data |= set_upper_bits(v, shift);
					len -= l + extra - sizeof(u32);
					if (len == length) {
						last = data;
						break;
					}
					__raw_writel(data, piobuf);
					piobuf++;
					extra = 0;
					data = 0;
				} else {
					/* Clear unused upper bytes */
					data |= clear_upper_bytes(v, l, extra);
					if (len == length) {
						last = data;
						break;
					}
					extra += l;
				}
			} else if (len == length) {
				last = data;
				break;
			}
		} else if (len == length) {
			u32 w;

			/*
			 * Need to round up for the last dword in the
			 * packet.
			 */
			w = (len + 3) >> 2;
			qib_pio_copy(piobuf, ss->sge.vaddr, w - 1);
			piobuf += w - 1;
			last = ((u32 *) ss->sge.vaddr)[w - 1];
			break;
		} else {
			u32 w = len >> 2;

			qib_pio_copy(piobuf, ss->sge.vaddr, w);
			piobuf += w;

			extra = len & (sizeof(u32) - 1);
			if (extra) {
				u32 v = ((u32 *) ss->sge.vaddr)[w];

				/* Clear unused upper bytes */
				data = clear_upper_bytes(v, extra, 0);
			}
		}
		update_sge(ss, len);
		length -= len;
	}
	/* Update address before sending packet. */
	update_sge(ss, length);
	if (flush_wc) {
		/* must flush early everything before trigger word */
		qib_flush_wc();
		__raw_writel(last, piobuf);
		/* be sure trigger word is written */
		qib_flush_wc();
	} else
		__raw_writel(last, piobuf);
}

static noinline struct qib_verbs_txreq *__get_txreq(struct qib_ibdev *dev,
					   struct qib_qp *qp)
{
	struct qib_verbs_txreq *tx;
	unsigned long flags;

	spin_lock_irqsave(&qp->s_lock, flags);
	spin_lock(&dev->pending_lock);

	if (!list_empty(&dev->txreq_free)) {
		struct list_head *l = dev->txreq_free.next;

		list_del(l);
		spin_unlock(&dev->pending_lock);
		spin_unlock_irqrestore(&qp->s_lock, flags);
		tx = list_entry(l, struct qib_verbs_txreq, txreq.list);
	} else {
		if (ib_qib_state_ops[qp->state] & QIB_PROCESS_RECV_OK &&
		    list_empty(&qp->iowait)) {
			dev->n_txwait++;
			qp->s_flags |= QIB_S_WAIT_TX;
			list_add_tail(&qp->iowait, &dev->txwait);
		}
		qp->s_flags &= ~QIB_S_BUSY;
		spin_unlock(&dev->pending_lock);
		spin_unlock_irqrestore(&qp->s_lock, flags);
		tx = ERR_PTR(-EBUSY);
	}
	return tx;
}

static inline struct qib_verbs_txreq *get_txreq(struct qib_ibdev *dev,
					 struct qib_qp *qp)
{
	struct qib_verbs_txreq *tx;
	unsigned long flags;

	spin_lock_irqsave(&dev->pending_lock, flags);
	/* assume the list non empty */
	if (likely(!list_empty(&dev->txreq_free))) {
		struct list_head *l = dev->txreq_free.next;

		list_del(l);
		spin_unlock_irqrestore(&dev->pending_lock, flags);
		tx = list_entry(l, struct qib_verbs_txreq, txreq.list);
	} else {
		/* call slow path to get the extra lock */
		spin_unlock_irqrestore(&dev->pending_lock, flags);
		tx =  __get_txreq(dev, qp);
	}
	return tx;
}

void qib_put_txreq(struct qib_verbs_txreq *tx)
{
	struct qib_ibdev *dev;
	struct qib_qp *qp;
	unsigned long flags;

	qp = tx->qp;
	dev = to_idev(qp->ibqp.device);

	if (atomic_dec_and_test(&qp->refcount))
		wake_up(&qp->wait);
	if (tx->mr) {
		qib_put_mr(tx->mr);
		tx->mr = NULL;
	}
	if (tx->txreq.flags & QIB_SDMA_TXREQ_F_FREEBUF) {
		tx->txreq.flags &= ~QIB_SDMA_TXREQ_F_FREEBUF;
		dma_unmap_single(&dd_from_dev(dev)->pcidev->dev,
				 tx->txreq.addr, tx->hdr_dwords << 2,
				 DMA_TO_DEVICE);
		kfree(tx->align_buf);
	}

	spin_lock_irqsave(&dev->pending_lock, flags);

	/* Put struct back on free list */
	list_add(&tx->txreq.list, &dev->txreq_free);

	if (!list_empty(&dev->txwait)) {
		/* Wake up first QP wanting a free struct */
		qp = list_entry(dev->txwait.next, struct qib_qp, iowait);
		list_del_init(&qp->iowait);
		atomic_inc(&qp->refcount);
		spin_unlock_irqrestore(&dev->pending_lock, flags);

		spin_lock_irqsave(&qp->s_lock, flags);
		if (qp->s_flags & QIB_S_WAIT_TX) {
			qp->s_flags &= ~QIB_S_WAIT_TX;
			qib_schedule_send(qp);
		}
		spin_unlock_irqrestore(&qp->s_lock, flags);

		if (atomic_dec_and_test(&qp->refcount))
			wake_up(&qp->wait);
	} else
		spin_unlock_irqrestore(&dev->pending_lock, flags);
}

/*
 * This is called when there are send DMA descriptors that might be
 * available.
 *
 * This is called with ppd->sdma_lock held.
 */
void qib_verbs_sdma_desc_avail(struct qib_pportdata *ppd, unsigned avail)
{
	struct qib_qp *qp, *nqp;
	struct qib_qp *qps[20];
	struct qib_ibdev *dev;
	unsigned i, n;

	n = 0;
	dev = &ppd->dd->verbs_dev;
	spin_lock(&dev->pending_lock);

	/* Search wait list for first QP wanting DMA descriptors. */
	list_for_each_entry_safe(qp, nqp, &dev->dmawait, iowait) {
		if (qp->port_num != ppd->port)
			continue;
		if (n == ARRAY_SIZE(qps))
			break;
		if (qp->s_tx->txreq.sg_count > avail)
			break;
		avail -= qp->s_tx->txreq.sg_count;
		list_del_init(&qp->iowait);
		atomic_inc(&qp->refcount);
		qps[n++] = qp;
	}

	spin_unlock(&dev->pending_lock);

	for (i = 0; i < n; i++) {
		qp = qps[i];
		spin_lock(&qp->s_lock);
		if (qp->s_flags & QIB_S_WAIT_DMA_DESC) {
			qp->s_flags &= ~QIB_S_WAIT_DMA_DESC;
			qib_schedule_send(qp);
		}
		spin_unlock(&qp->s_lock);
		if (atomic_dec_and_test(&qp->refcount))
			wake_up(&qp->wait);
	}
}

/*
 * This is called with ppd->sdma_lock held.
 */
static void sdma_complete(struct qib_sdma_txreq *cookie, int status)
{
	struct qib_verbs_txreq *tx =
		container_of(cookie, struct qib_verbs_txreq, txreq);
	struct qib_qp *qp = tx->qp;

	spin_lock(&qp->s_lock);
	if (tx->wqe)
		qib_send_complete(qp, tx->wqe, IB_WC_SUCCESS);
	else if (qp->ibqp.qp_type == IB_QPT_RC) {
		struct qib_ib_header *hdr;

		if (tx->txreq.flags & QIB_SDMA_TXREQ_F_FREEBUF)
			hdr = &tx->align_buf->hdr;
		else {
			struct qib_ibdev *dev = to_idev(qp->ibqp.device);

			hdr = &dev->pio_hdrs[tx->hdr_inx].hdr;
		}
		qib_rc_send_complete(qp, hdr);
	}
	if (atomic_dec_and_test(&qp->s_dma_busy)) {
		if (qp->state == IB_QPS_RESET)
			wake_up(&qp->wait_dma);
		else if (qp->s_flags & QIB_S_WAIT_DMA) {
			qp->s_flags &= ~QIB_S_WAIT_DMA;
			qib_schedule_send(qp);
		}
	}
	spin_unlock(&qp->s_lock);

	qib_put_txreq(tx);
}

static int wait_kmem(struct qib_ibdev *dev, struct qib_qp *qp)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&qp->s_lock, flags);
	if (ib_qib_state_ops[qp->state] & QIB_PROCESS_RECV_OK) {
		spin_lock(&dev->pending_lock);
		if (list_empty(&qp->iowait)) {
			if (list_empty(&dev->memwait))
				mod_timer(&dev->mem_timer, jiffies + 1);
			qp->s_flags |= QIB_S_WAIT_KMEM;
			list_add_tail(&qp->iowait, &dev->memwait);
		}
		spin_unlock(&dev->pending_lock);
		qp->s_flags &= ~QIB_S_BUSY;
		ret = -EBUSY;
	}
	spin_unlock_irqrestore(&qp->s_lock, flags);

	return ret;
}

static int qib_verbs_send_dma(struct qib_qp *qp, struct qib_ib_header *hdr,
			      u32 hdrwords, struct qib_sge_state *ss, u32 len,
			      u32 plen, u32 dwords)
{
	struct qib_ibdev *dev = to_idev(qp->ibqp.device);
	struct qib_devdata *dd = dd_from_dev(dev);
	struct qib_ibport *ibp = to_iport(qp->ibqp.device, qp->port_num);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	struct qib_verbs_txreq *tx;
	struct qib_pio_header *phdr;
	u32 control;
	u32 ndesc;
	int ret;

	tx = qp->s_tx;
	if (tx) {
		qp->s_tx = NULL;
		/* resend previously constructed packet */
		ret = qib_sdma_verbs_send(ppd, tx->ss, tx->dwords, tx);
		goto bail;
	}

	tx = get_txreq(dev, qp);
	if (IS_ERR(tx))
		goto bail_tx;

	control = dd->f_setpbc_control(ppd, plen, qp->s_srate,
				       be16_to_cpu(hdr->lrh[0]) >> 12);
	tx->qp = qp;
	atomic_inc(&qp->refcount);
	tx->wqe = qp->s_wqe;
	tx->mr = qp->s_rdma_mr;
	if (qp->s_rdma_mr)
		qp->s_rdma_mr = NULL;
	tx->txreq.callback = sdma_complete;
	if (dd->flags & QIB_HAS_SDMA_TIMEOUT)
		tx->txreq.flags = QIB_SDMA_TXREQ_F_HEADTOHOST;
	else
		tx->txreq.flags = QIB_SDMA_TXREQ_F_INTREQ;
	if (plen + 1 > dd->piosize2kmax_dwords)
		tx->txreq.flags |= QIB_SDMA_TXREQ_F_USELARGEBUF;

	if (len) {
		/*
		 * Don't try to DMA if it takes more descriptors than
		 * the queue holds.
		 */
		ndesc = qib_count_sge(ss, len);
		if (ndesc >= ppd->sdma_descq_cnt)
			ndesc = 0;
	} else
		ndesc = 1;
	if (ndesc) {
		phdr = &dev->pio_hdrs[tx->hdr_inx];
		phdr->pbc[0] = cpu_to_le32(plen);
		phdr->pbc[1] = cpu_to_le32(control);
		memcpy(&phdr->hdr, hdr, hdrwords << 2);
		tx->txreq.flags |= QIB_SDMA_TXREQ_F_FREEDESC;
		tx->txreq.sg_count = ndesc;
		tx->txreq.addr = dev->pio_hdrs_phys +
			tx->hdr_inx * sizeof(struct qib_pio_header);
		tx->hdr_dwords = hdrwords + 2; /* add PBC length */
		ret = qib_sdma_verbs_send(ppd, ss, dwords, tx);
		goto bail;
	}

	/* Allocate a buffer and copy the header and payload to it. */
	tx->hdr_dwords = plen + 1;
	phdr = kmalloc(tx->hdr_dwords << 2, GFP_ATOMIC);
	if (!phdr)
		goto err_tx;
	phdr->pbc[0] = cpu_to_le32(plen);
	phdr->pbc[1] = cpu_to_le32(control);
	memcpy(&phdr->hdr, hdr, hdrwords << 2);
	qib_copy_from_sge((u32 *) &phdr->hdr + hdrwords, ss, len);

	tx->txreq.addr = dma_map_single(&dd->pcidev->dev, phdr,
					tx->hdr_dwords << 2, DMA_TO_DEVICE);
	if (dma_mapping_error(&dd->pcidev->dev, tx->txreq.addr))
		goto map_err;
	tx->align_buf = phdr;
	tx->txreq.flags |= QIB_SDMA_TXREQ_F_FREEBUF;
	tx->txreq.sg_count = 1;
	ret = qib_sdma_verbs_send(ppd, NULL, 0, tx);
	goto unaligned;

map_err:
	kfree(phdr);
err_tx:
	qib_put_txreq(tx);
	ret = wait_kmem(dev, qp);
unaligned:
	ibp->n_unaligned++;
bail:
	return ret;
bail_tx:
	ret = PTR_ERR(tx);
	goto bail;
}

/*
 * If we are now in the error state, return zero to flush the
 * send work request.
 */
static int no_bufs_available(struct qib_qp *qp)
{
	struct qib_ibdev *dev = to_idev(qp->ibqp.device);
	struct qib_devdata *dd;
	unsigned long flags;
	int ret = 0;

	/*
	 * Note that as soon as want_buffer() is called and
	 * possibly before it returns, qib_ib_piobufavail()
	 * could be called. Therefore, put QP on the I/O wait list before
	 * enabling the PIO avail interrupt.
	 */
	spin_lock_irqsave(&qp->s_lock, flags);
	if (ib_qib_state_ops[qp->state] & QIB_PROCESS_RECV_OK) {
		spin_lock(&dev->pending_lock);
		if (list_empty(&qp->iowait)) {
			dev->n_piowait++;
			qp->s_flags |= QIB_S_WAIT_PIO;
			list_add_tail(&qp->iowait, &dev->piowait);
			dd = dd_from_dev(dev);
			dd->f_wantpiobuf_intr(dd, 1);
		}
		spin_unlock(&dev->pending_lock);
		qp->s_flags &= ~QIB_S_BUSY;
		ret = -EBUSY;
	}
	spin_unlock_irqrestore(&qp->s_lock, flags);
	return ret;
}

static int qib_verbs_send_pio(struct qib_qp *qp, struct qib_ib_header *ibhdr,
			      u32 hdrwords, struct qib_sge_state *ss, u32 len,
			      u32 plen, u32 dwords)
{
	struct qib_devdata *dd = dd_from_ibdev(qp->ibqp.device);
	struct qib_pportdata *ppd = dd->pport + qp->port_num - 1;
	u32 *hdr = (u32 *) ibhdr;
	u32 __iomem *piobuf_orig;
	u32 __iomem *piobuf;
	u64 pbc;
	unsigned long flags;
	unsigned flush_wc;
	u32 control;
	u32 pbufn;

	control = dd->f_setpbc_control(ppd, plen, qp->s_srate,
		be16_to_cpu(ibhdr->lrh[0]) >> 12);
	pbc = ((u64) control << 32) | plen;
	piobuf = dd->f_getsendbuf(ppd, pbc, &pbufn);
	if (unlikely(piobuf == NULL))
		return no_bufs_available(qp);

	/*
	 * Write the pbc.
	 * We have to flush after the PBC for correctness on some cpus
	 * or WC buffer can be written out of order.
	 */
	writeq(pbc, piobuf);
	piobuf_orig = piobuf;
	piobuf += 2;

	flush_wc = dd->flags & QIB_PIO_FLUSH_WC;
	if (len == 0) {
		/*
		 * If there is just the header portion, must flush before
		 * writing last word of header for correctness, and after
		 * the last header word (trigger word).
		 */
		if (flush_wc) {
			qib_flush_wc();
			qib_pio_copy(piobuf, hdr, hdrwords - 1);
			qib_flush_wc();
			__raw_writel(hdr[hdrwords - 1], piobuf + hdrwords - 1);
			qib_flush_wc();
		} else
			qib_pio_copy(piobuf, hdr, hdrwords);
		goto done;
	}

	if (flush_wc)
		qib_flush_wc();
	qib_pio_copy(piobuf, hdr, hdrwords);
	piobuf += hdrwords;

	/* The common case is aligned and contained in one segment. */
	if (likely(ss->num_sge == 1 && len <= ss->sge.length &&
		   !((unsigned long)ss->sge.vaddr & (sizeof(u32) - 1)))) {
		u32 *addr = (u32 *) ss->sge.vaddr;

		/* Update address before sending packet. */
		update_sge(ss, len);
		if (flush_wc) {
			qib_pio_copy(piobuf, addr, dwords - 1);
			/* must flush early everything before trigger word */
			qib_flush_wc();
			__raw_writel(addr[dwords - 1], piobuf + dwords - 1);
			/* be sure trigger word is written */
			qib_flush_wc();
		} else
			qib_pio_copy(piobuf, addr, dwords);
		goto done;
	}
	copy_io(piobuf, ss, len, flush_wc);
done:
	if (dd->flags & QIB_USE_SPCL_TRIG) {
		u32 spcl_off = (pbufn >= dd->piobcnt2k) ? 2047 : 1023;

		qib_flush_wc();
		__raw_writel(0xaebecede, piobuf_orig + spcl_off);
	}
	qib_sendbuf_done(dd, pbufn);
	if (qp->s_rdma_mr) {
		qib_put_mr(qp->s_rdma_mr);
		qp->s_rdma_mr = NULL;
	}
	if (qp->s_wqe) {
		spin_lock_irqsave(&qp->s_lock, flags);
		qib_send_complete(qp, qp->s_wqe, IB_WC_SUCCESS);
		spin_unlock_irqrestore(&qp->s_lock, flags);
	} else if (qp->ibqp.qp_type == IB_QPT_RC) {
		spin_lock_irqsave(&qp->s_lock, flags);
		qib_rc_send_complete(qp, ibhdr);
		spin_unlock_irqrestore(&qp->s_lock, flags);
	}
	return 0;
}

/**
 * qib_verbs_send - send a packet
 * @qp: the QP to send on
 * @hdr: the packet header
 * @hdrwords: the number of 32-bit words in the header
 * @ss: the SGE to send
 * @len: the length of the packet in bytes
 *
 * Return zero if packet is sent or queued OK.
 * Return non-zero and clear qp->s_flags QIB_S_BUSY otherwise.
 */
int qib_verbs_send(struct qib_qp *qp, struct qib_ib_header *hdr,
		   u32 hdrwords, struct qib_sge_state *ss, u32 len)
{
	struct qib_devdata *dd = dd_from_ibdev(qp->ibqp.device);
	u32 plen;
	int ret;
	u32 dwords = (len + 3) >> 2;

	/*
	 * Calculate the send buffer trigger address.
	 * The +1 counts for the pbc control dword following the pbc length.
	 */
	plen = hdrwords + dwords + 1;

	/*
	 * VL15 packets (IB_QPT_SMI) will always use PIO, so we
	 * can defer SDMA restart until link goes ACTIVE without
	 * worrying about just how we got there.
	 */
	if (qp->ibqp.qp_type == IB_QPT_SMI ||
	    !(dd->flags & QIB_HAS_SEND_DMA))
		ret = qib_verbs_send_pio(qp, hdr, hdrwords, ss, len,
					 plen, dwords);
	else
		ret = qib_verbs_send_dma(qp, hdr, hdrwords, ss, len,
					 plen, dwords);

	return ret;
}

int qib_snapshot_counters(struct qib_pportdata *ppd, u64 *swords,
			  u64 *rwords, u64 *spkts, u64 *rpkts,
			  u64 *xmit_wait)
{
	int ret;
	struct qib_devdata *dd = ppd->dd;

	if (!(dd->flags & QIB_PRESENT)) {
		/* no hardware, freeze, etc. */
		ret = -EINVAL;
		goto bail;
	}
	*swords = dd->f_portcntr(ppd, QIBPORTCNTR_WORDSEND);
	*rwords = dd->f_portcntr(ppd, QIBPORTCNTR_WORDRCV);
	*spkts = dd->f_portcntr(ppd, QIBPORTCNTR_PKTSEND);
	*rpkts = dd->f_portcntr(ppd, QIBPORTCNTR_PKTRCV);
	*xmit_wait = dd->f_portcntr(ppd, QIBPORTCNTR_SENDSTALL);

	ret = 0;

bail:
	return ret;
}

/**
 * qib_get_counters - get various chip counters
 * @dd: the qlogic_ib device
 * @cntrs: counters are placed here
 *
 * Return the counters needed by recv_pma_get_portcounters().
 */
int qib_get_counters(struct qib_pportdata *ppd,
		     struct qib_verbs_counters *cntrs)
{
	int ret;

	if (!(ppd->dd->flags & QIB_PRESENT)) {
		/* no hardware, freeze, etc. */
		ret = -EINVAL;
		goto bail;
	}
	cntrs->symbol_error_counter =
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_IBSYMBOLERR);
	cntrs->link_error_recovery_counter =
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_IBLINKERRRECOV);
	/*
	 * The link downed counter counts when the other side downs the
	 * connection.  We add in the number of times we downed the link
	 * due to local link integrity errors to compensate.
	 */
	cntrs->link_downed_counter =
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_IBLINKDOWN);
	cntrs->port_rcv_errors =
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_RXDROPPKT) +
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_RCVOVFL) +
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_ERR_RLEN) +
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_INVALIDRLEN) +
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_ERRLINK) +
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_ERRICRC) +
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_ERRVCRC) +
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_ERRLPCRC) +
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_BADFORMAT);
	cntrs->port_rcv_errors +=
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_RXLOCALPHYERR);
	cntrs->port_rcv_errors +=
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_RXVLERR);
	cntrs->port_rcv_remphys_errors =
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_RCVEBP);
	cntrs->port_xmit_discards =
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_UNSUPVL);
	cntrs->port_xmit_data = ppd->dd->f_portcntr(ppd,
			QIBPORTCNTR_WORDSEND);
	cntrs->port_rcv_data = ppd->dd->f_portcntr(ppd,
			QIBPORTCNTR_WORDRCV);
	cntrs->port_xmit_packets = ppd->dd->f_portcntr(ppd,
			QIBPORTCNTR_PKTSEND);
	cntrs->port_rcv_packets = ppd->dd->f_portcntr(ppd,
			QIBPORTCNTR_PKTRCV);
	cntrs->local_link_integrity_errors =
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_LLI);
	cntrs->excessive_buffer_overrun_errors =
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_EXCESSBUFOVFL);
	cntrs->vl15_dropped =
		ppd->dd->f_portcntr(ppd, QIBPORTCNTR_VL15PKTDROP);

	ret = 0;

bail:
	return ret;
}

/**
 * qib_ib_piobufavail - callback when a PIO buffer is available
 * @dd: the device pointer
 *
 * This is called from qib_intr() at interrupt level when a PIO buffer is
 * available after qib_verbs_send() returned an error that no buffers were
 * available. Disable the interrupt if there are no more QPs waiting.
 */
void qib_ib_piobufavail(struct qib_devdata *dd)
{
	struct qib_ibdev *dev = &dd->verbs_dev;
	struct list_head *list;
	struct qib_qp *qps[5];
	struct qib_qp *qp;
	unsigned long flags;
	unsigned i, n;

	list = &dev->piowait;
	n = 0;

	/*
	 * Note: checking that the piowait list is empty and clearing
	 * the buffer available interrupt needs to be atomic or we
	 * could end up with QPs on the wait list with the interrupt
	 * disabled.
	 */
	spin_lock_irqsave(&dev->pending_lock, flags);
	while (!list_empty(list)) {
		if (n == ARRAY_SIZE(qps))
			goto full;
		qp = list_entry(list->next, struct qib_qp, iowait);
		list_del_init(&qp->iowait);
		atomic_inc(&qp->refcount);
		qps[n++] = qp;
	}
	dd->f_wantpiobuf_intr(dd, 0);
full:
	spin_unlock_irqrestore(&dev->pending_lock, flags);

	for (i = 0; i < n; i++) {
		qp = qps[i];

		spin_lock_irqsave(&qp->s_lock, flags);
		if (qp->s_flags & QIB_S_WAIT_PIO) {
			qp->s_flags &= ~QIB_S_WAIT_PIO;
			qib_schedule_send(qp);
		}
		spin_unlock_irqrestore(&qp->s_lock, flags);

		/* Notify qib_destroy_qp() if it is waiting. */
		if (atomic_dec_and_test(&qp->refcount))
			wake_up(&qp->wait);
	}
}

static int qib_query_device(struct ib_device *ibdev, struct ib_device_attr *props,
			    struct ib_udata *uhw)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	struct qib_ibdev *dev = to_idev(ibdev);

	if (uhw->inlen || uhw->outlen)
		return -EINVAL;
	memset(props, 0, sizeof(*props));

	props->device_cap_flags = IB_DEVICE_BAD_PKEY_CNTR |
		IB_DEVICE_BAD_QKEY_CNTR | IB_DEVICE_SHUTDOWN_PORT |
		IB_DEVICE_SYS_IMAGE_GUID | IB_DEVICE_RC_RNR_NAK_GEN |
		IB_DEVICE_PORT_ACTIVE_EVENT | IB_DEVICE_SRQ_RESIZE;
	props->page_size_cap = PAGE_SIZE;
	props->vendor_id =
		QIB_SRC_OUI_1 << 16 | QIB_SRC_OUI_2 << 8 | QIB_SRC_OUI_3;
	props->vendor_part_id = dd->deviceid;
	props->hw_ver = dd->minrev;
	props->sys_image_guid = ib_qib_sys_image_guid;
	props->max_mr_size = ~0ULL;
	props->max_qp = ib_qib_max_qps;
	props->max_qp_wr = ib_qib_max_qp_wrs;
	props->max_sge = ib_qib_max_sges;
	props->max_sge_rd = ib_qib_max_sges;
	props->max_cq = ib_qib_max_cqs;
	props->max_ah = ib_qib_max_ahs;
	props->max_cqe = ib_qib_max_cqes;
	props->max_mr = dev->lk_table.max;
	props->max_fmr = dev->lk_table.max;
	props->max_map_per_fmr = 32767;
	props->max_pd = ib_qib_max_pds;
	props->max_qp_rd_atom = QIB_MAX_RDMA_ATOMIC;
	props->max_qp_init_rd_atom = 255;
	/* props->max_res_rd_atom */
	props->max_srq = ib_qib_max_srqs;
	props->max_srq_wr = ib_qib_max_srq_wrs;
	props->max_srq_sge = ib_qib_max_srq_sges;
	/* props->local_ca_ack_delay */
	props->atomic_cap = IB_ATOMIC_GLOB;
	props->max_pkeys = qib_get_npkeys(dd);
	props->max_mcast_grp = ib_qib_max_mcast_grps;
	props->max_mcast_qp_attach = ib_qib_max_mcast_qp_attached;
	props->max_total_mcast_qp_attach = props->max_mcast_qp_attach *
		props->max_mcast_grp;

	return 0;
}

static int qib_query_port(struct ib_device *ibdev, u8 port,
			  struct ib_port_attr *props)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	enum ib_mtu mtu;
	u16 lid = ppd->lid;

	memset(props, 0, sizeof(*props));
	props->lid = lid ? lid : be16_to_cpu(IB_LID_PERMISSIVE);
	props->lmc = ppd->lmc;
	props->sm_lid = ibp->sm_lid;
	props->sm_sl = ibp->sm_sl;
	props->state = dd->f_iblink_state(ppd->lastibcstat);
	props->phys_state = dd->f_ibphys_portstate(ppd->lastibcstat);
	props->port_cap_flags = ibp->port_cap_flags;
	props->gid_tbl_len = QIB_GUIDS_PER_PORT;
	props->max_msg_sz = 0x80000000;
	props->pkey_tbl_len = qib_get_npkeys(dd);
	props->bad_pkey_cntr = ibp->pkey_violations;
	props->qkey_viol_cntr = ibp->qkey_violations;
	props->active_width = ppd->link_width_active;
	/* See rate_show() */
	props->active_speed = ppd->link_speed_active;
	props->max_vl_num = qib_num_vls(ppd->vls_supported);
	props->init_type_reply = 0;

	props->max_mtu = qib_ibmtu ? qib_ibmtu : IB_MTU_4096;
	switch (ppd->ibmtu) {
	case 4096:
		mtu = IB_MTU_4096;
		break;
	case 2048:
		mtu = IB_MTU_2048;
		break;
	case 1024:
		mtu = IB_MTU_1024;
		break;
	case 512:
		mtu = IB_MTU_512;
		break;
	case 256:
		mtu = IB_MTU_256;
		break;
	default:
		mtu = IB_MTU_2048;
	}
	props->active_mtu = mtu;
	props->subnet_timeout = ibp->subnet_timeout;

	return 0;
}

static int qib_modify_device(struct ib_device *device,
			     int device_modify_mask,
			     struct ib_device_modify *device_modify)
{
	struct qib_devdata *dd = dd_from_ibdev(device);
	unsigned i;
	int ret;

	if (device_modify_mask & ~(IB_DEVICE_MODIFY_SYS_IMAGE_GUID |
				   IB_DEVICE_MODIFY_NODE_DESC)) {
		ret = -EOPNOTSUPP;
		goto bail;
	}

	if (device_modify_mask & IB_DEVICE_MODIFY_NODE_DESC) {
		memcpy(device->node_desc, device_modify->node_desc, 64);
		for (i = 0; i < dd->num_pports; i++) {
			struct qib_ibport *ibp = &dd->pport[i].ibport_data;

			qib_node_desc_chg(ibp);
		}
	}

	if (device_modify_mask & IB_DEVICE_MODIFY_SYS_IMAGE_GUID) {
		ib_qib_sys_image_guid =
			cpu_to_be64(device_modify->sys_image_guid);
		for (i = 0; i < dd->num_pports; i++) {
			struct qib_ibport *ibp = &dd->pport[i].ibport_data;

			qib_sys_guid_chg(ibp);
		}
	}

	ret = 0;

bail:
	return ret;
}

static int qib_modify_port(struct ib_device *ibdev, u8 port,
			   int port_modify_mask, struct ib_port_modify *props)
{
	struct qib_ibport *ibp = to_iport(ibdev, port);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);

	ibp->port_cap_flags |= props->set_port_cap_mask;
	ibp->port_cap_flags &= ~props->clr_port_cap_mask;
	if (props->set_port_cap_mask || props->clr_port_cap_mask)
		qib_cap_mask_chg(ibp);
	if (port_modify_mask & IB_PORT_SHUTDOWN)
		qib_set_linkstate(ppd, QIB_IB_LINKDOWN);
	if (port_modify_mask & IB_PORT_RESET_QKEY_CNTR)
		ibp->qkey_violations = 0;
	return 0;
}

static int qib_query_gid(struct ib_device *ibdev, u8 port,
			 int index, union ib_gid *gid)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	int ret = 0;

	if (!port || port > dd->num_pports)
		ret = -EINVAL;
	else {
		struct qib_ibport *ibp = to_iport(ibdev, port);
		struct qib_pportdata *ppd = ppd_from_ibp(ibp);

		gid->global.subnet_prefix = ibp->gid_prefix;
		if (index == 0)
			gid->global.interface_id = ppd->guid;
		else if (index < QIB_GUIDS_PER_PORT)
			gid->global.interface_id = ibp->guids[index - 1];
		else
			ret = -EINVAL;
	}

	return ret;
}

static struct ib_pd *qib_alloc_pd(struct ib_device *ibdev,
				  struct ib_ucontext *context,
				  struct ib_udata *udata)
{
	struct qib_ibdev *dev = to_idev(ibdev);
	struct qib_pd *pd;
	struct ib_pd *ret;

	/*
	 * This is actually totally arbitrary.  Some correctness tests
	 * assume there's a maximum number of PDs that can be allocated.
	 * We don't actually have this limit, but we fail the test if
	 * we allow allocations of more than we report for this value.
	 */

	pd = kmalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}

	spin_lock(&dev->n_pds_lock);
	if (dev->n_pds_allocated == ib_qib_max_pds) {
		spin_unlock(&dev->n_pds_lock);
		kfree(pd);
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}

	dev->n_pds_allocated++;
	spin_unlock(&dev->n_pds_lock);

	/* ib_alloc_pd() will initialize pd->ibpd. */
	pd->user = udata != NULL;

	ret = &pd->ibpd;

bail:
	return ret;
}

static int qib_dealloc_pd(struct ib_pd *ibpd)
{
	struct qib_pd *pd = to_ipd(ibpd);
	struct qib_ibdev *dev = to_idev(ibpd->device);

	spin_lock(&dev->n_pds_lock);
	dev->n_pds_allocated--;
	spin_unlock(&dev->n_pds_lock);

	kfree(pd);

	return 0;
}

int qib_check_ah(struct ib_device *ibdev, struct ib_ah_attr *ah_attr)
{
	/* A multicast address requires a GRH (see ch. 8.4.1). */
	if (ah_attr->dlid >= QIB_MULTICAST_LID_BASE &&
	    ah_attr->dlid != QIB_PERMISSIVE_LID &&
	    !(ah_attr->ah_flags & IB_AH_GRH))
		goto bail;
	if ((ah_attr->ah_flags & IB_AH_GRH) &&
	    ah_attr->grh.sgid_index >= QIB_GUIDS_PER_PORT)
		goto bail;
	if (ah_attr->dlid == 0)
		goto bail;
	if (ah_attr->port_num < 1 ||
	    ah_attr->port_num > ibdev->phys_port_cnt)
		goto bail;
	if (ah_attr->static_rate != IB_RATE_PORT_CURRENT &&
	    ib_rate_to_mult(ah_attr->static_rate) < 0)
		goto bail;
	if (ah_attr->sl > 15)
		goto bail;
	return 0;
bail:
	return -EINVAL;
}

/**
 * qib_create_ah - create an address handle
 * @pd: the protection domain
 * @ah_attr: the attributes of the AH
 *
 * This may be called from interrupt context.
 */
static struct ib_ah *qib_create_ah(struct ib_pd *pd,
				   struct ib_ah_attr *ah_attr)
{
	struct qib_ah *ah;
	struct ib_ah *ret;
	struct qib_ibdev *dev = to_idev(pd->device);
	unsigned long flags;

	if (qib_check_ah(pd->device, ah_attr)) {
		ret = ERR_PTR(-EINVAL);
		goto bail;
	}

	ah = kmalloc(sizeof(*ah), GFP_ATOMIC);
	if (!ah) {
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}

	spin_lock_irqsave(&dev->n_ahs_lock, flags);
	if (dev->n_ahs_allocated == ib_qib_max_ahs) {
		spin_unlock_irqrestore(&dev->n_ahs_lock, flags);
		kfree(ah);
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}

	dev->n_ahs_allocated++;
	spin_unlock_irqrestore(&dev->n_ahs_lock, flags);

	/* ib_create_ah() will initialize ah->ibah. */
	ah->attr = *ah_attr;
	atomic_set(&ah->refcount, 0);

	ret = &ah->ibah;

bail:
	return ret;
}

struct ib_ah *qib_create_qp0_ah(struct qib_ibport *ibp, u16 dlid)
{
	struct ib_ah_attr attr;
	struct ib_ah *ah = ERR_PTR(-EINVAL);
	struct qib_qp *qp0;

	memset(&attr, 0, sizeof(attr));
	attr.dlid = dlid;
	attr.port_num = ppd_from_ibp(ibp)->port;
	rcu_read_lock();
	qp0 = rcu_dereference(ibp->qp0);
	if (qp0)
		ah = ib_create_ah(qp0->ibqp.pd, &attr);
	rcu_read_unlock();
	return ah;
}

/**
 * qib_destroy_ah - destroy an address handle
 * @ibah: the AH to destroy
 *
 * This may be called from interrupt context.
 */
static int qib_destroy_ah(struct ib_ah *ibah)
{
	struct qib_ibdev *dev = to_idev(ibah->device);
	struct qib_ah *ah = to_iah(ibah);
	unsigned long flags;

	if (atomic_read(&ah->refcount) != 0)
		return -EBUSY;

	spin_lock_irqsave(&dev->n_ahs_lock, flags);
	dev->n_ahs_allocated--;
	spin_unlock_irqrestore(&dev->n_ahs_lock, flags);

	kfree(ah);

	return 0;
}

static int qib_modify_ah(struct ib_ah *ibah, struct ib_ah_attr *ah_attr)
{
	struct qib_ah *ah = to_iah(ibah);

	if (qib_check_ah(ibah->device, ah_attr))
		return -EINVAL;

	ah->attr = *ah_attr;

	return 0;
}

static int qib_query_ah(struct ib_ah *ibah, struct ib_ah_attr *ah_attr)
{
	struct qib_ah *ah = to_iah(ibah);

	*ah_attr = ah->attr;

	return 0;
}

/**
 * qib_get_npkeys - return the size of the PKEY table for context 0
 * @dd: the qlogic_ib device
 */
unsigned qib_get_npkeys(struct qib_devdata *dd)
{
	return ARRAY_SIZE(dd->rcd[0]->pkeys);
}

/*
 * Return the indexed PKEY from the port PKEY table.
 * No need to validate rcd[ctxt]; the port is setup if we are here.
 */
unsigned qib_get_pkey(struct qib_ibport *ibp, unsigned index)
{
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	struct qib_devdata *dd = ppd->dd;
	unsigned ctxt = ppd->hw_pidx;
	unsigned ret;

	/* dd->rcd null if mini_init or some init failures */
	if (!dd->rcd || index >= ARRAY_SIZE(dd->rcd[ctxt]->pkeys))
		ret = 0;
	else
		ret = dd->rcd[ctxt]->pkeys[index];

	return ret;
}

static int qib_query_pkey(struct ib_device *ibdev, u8 port, u16 index,
			  u16 *pkey)
{
	struct qib_devdata *dd = dd_from_ibdev(ibdev);
	int ret;

	if (index >= qib_get_npkeys(dd)) {
		ret = -EINVAL;
		goto bail;
	}

	*pkey = qib_get_pkey(to_iport(ibdev, port), index);
	ret = 0;

bail:
	return ret;
}

/**
 * qib_alloc_ucontext - allocate a ucontest
 * @ibdev: the infiniband device
 * @udata: not used by the QLogic_IB driver
 */

static struct ib_ucontext *qib_alloc_ucontext(struct ib_device *ibdev,
					      struct ib_udata *udata)
{
	struct qib_ucontext *context;
	struct ib_ucontext *ret;

	context = kmalloc(sizeof(*context), GFP_KERNEL);
	if (!context) {
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}

	ret = &context->ibucontext;

bail:
	return ret;
}

static int qib_dealloc_ucontext(struct ib_ucontext *context)
{
	kfree(to_iucontext(context));
	return 0;
}

static void init_ibport(struct qib_pportdata *ppd)
{
	struct qib_verbs_counters cntrs;
	struct qib_ibport *ibp = &ppd->ibport_data;

	spin_lock_init(&ibp->lock);
	/* Set the prefix to the default value (see ch. 4.1.1) */
	ibp->gid_prefix = IB_DEFAULT_GID_PREFIX;
	ibp->sm_lid = be16_to_cpu(IB_LID_PERMISSIVE);
	ibp->port_cap_flags = IB_PORT_SYS_IMAGE_GUID_SUP |
		IB_PORT_CLIENT_REG_SUP | IB_PORT_SL_MAP_SUP |
		IB_PORT_TRAP_SUP | IB_PORT_AUTO_MIGR_SUP |
		IB_PORT_DR_NOTICE_SUP | IB_PORT_CAP_MASK_NOTICE_SUP |
		IB_PORT_OTHER_LOCAL_CHANGES_SUP;
	if (ppd->dd->flags & QIB_HAS_LINK_LATENCY)
		ibp->port_cap_flags |= IB_PORT_LINK_LATENCY_SUP;
	ibp->pma_counter_select[0] = IB_PMA_PORT_XMIT_DATA;
	ibp->pma_counter_select[1] = IB_PMA_PORT_RCV_DATA;
	ibp->pma_counter_select[2] = IB_PMA_PORT_XMIT_PKTS;
	ibp->pma_counter_select[3] = IB_PMA_PORT_RCV_PKTS;
	ibp->pma_counter_select[4] = IB_PMA_PORT_XMIT_WAIT;

	/* Snapshot current HW counters to "clear" them. */
	qib_get_counters(ppd, &cntrs);
	ibp->z_symbol_error_counter = cntrs.symbol_error_counter;
	ibp->z_link_error_recovery_counter =
		cntrs.link_error_recovery_counter;
	ibp->z_link_downed_counter = cntrs.link_downed_counter;
	ibp->z_port_rcv_errors = cntrs.port_rcv_errors;
	ibp->z_port_rcv_remphys_errors = cntrs.port_rcv_remphys_errors;
	ibp->z_port_xmit_discards = cntrs.port_xmit_discards;
	ibp->z_port_xmit_data = cntrs.port_xmit_data;
	ibp->z_port_rcv_data = cntrs.port_rcv_data;
	ibp->z_port_xmit_packets = cntrs.port_xmit_packets;
	ibp->z_port_rcv_packets = cntrs.port_rcv_packets;
	ibp->z_local_link_integrity_errors =
		cntrs.local_link_integrity_errors;
	ibp->z_excessive_buffer_overrun_errors =
		cntrs.excessive_buffer_overrun_errors;
	ibp->z_vl15_dropped = cntrs.vl15_dropped;
	RCU_INIT_POINTER(ibp->qp0, NULL);
	RCU_INIT_POINTER(ibp->qp1, NULL);
}

static int qib_port_immutable(struct ib_device *ibdev, u8 port_num,
			      struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int err;

	err = qib_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;
	immutable->core_cap_flags = RDMA_CORE_PORT_IBA_IB;
	immutable->max_mad_size = IB_MGMT_MAD_SIZE;

	return 0;
}

/**
 * qib_register_ib_device - register our device with the infiniband core
 * @dd: the device data structure
 * Return the allocated qib_ibdev pointer or NULL on error.
 */
int qib_register_ib_device(struct qib_devdata *dd)
{
	struct qib_ibdev *dev = &dd->verbs_dev;
	struct ib_device *ibdev = &dev->ibdev;
	struct qib_pportdata *ppd = dd->pport;
	unsigned i, lk_tab_size;
	int ret;

	dev->qp_table_size = ib_qib_qp_table_size;
	get_random_bytes(&dev->qp_rnd, sizeof(dev->qp_rnd));
	dev->qp_table = kmalloc_array(
				dev->qp_table_size,
				sizeof(*dev->qp_table),
				GFP_KERNEL);
	if (!dev->qp_table) {
		ret = -ENOMEM;
		goto err_qpt;
	}
	for (i = 0; i < dev->qp_table_size; i++)
		RCU_INIT_POINTER(dev->qp_table[i], NULL);

	for (i = 0; i < dd->num_pports; i++)
		init_ibport(ppd + i);

	/* Only need to initialize non-zero fields. */
	spin_lock_init(&dev->qpt_lock);
	spin_lock_init(&dev->n_pds_lock);
	spin_lock_init(&dev->n_ahs_lock);
	spin_lock_init(&dev->n_cqs_lock);
	spin_lock_init(&dev->n_qps_lock);
	spin_lock_init(&dev->n_srqs_lock);
	spin_lock_init(&dev->n_mcast_grps_lock);
	init_timer(&dev->mem_timer);
	dev->mem_timer.function = mem_timer;
	dev->mem_timer.data = (unsigned long) dev;

	qib_init_qpn_table(dd, &dev->qpn_table);

	/*
	 * The top ib_qib_lkey_table_size bits are used to index the
	 * table.  The lower 8 bits can be owned by the user (copied from
	 * the LKEY).  The remaining bits act as a generation number or tag.
	 */
	spin_lock_init(&dev->lk_table.lock);
	/* insure generation is at least 4 bits see keys.c */
	if (ib_qib_lkey_table_size > MAX_LKEY_TABLE_BITS) {
		qib_dev_warn(dd, "lkey bits %u too large, reduced to %u\n",
			ib_qib_lkey_table_size, MAX_LKEY_TABLE_BITS);
		ib_qib_lkey_table_size = MAX_LKEY_TABLE_BITS;
	}
	dev->lk_table.max = 1 << ib_qib_lkey_table_size;
	lk_tab_size = dev->lk_table.max * sizeof(*dev->lk_table.table);
	dev->lk_table.table = (struct qib_mregion __rcu **)
		vmalloc(lk_tab_size);
	if (dev->lk_table.table == NULL) {
		ret = -ENOMEM;
		goto err_lk;
	}
	RCU_INIT_POINTER(dev->dma_mr, NULL);
	for (i = 0; i < dev->lk_table.max; i++)
		RCU_INIT_POINTER(dev->lk_table.table[i], NULL);
	INIT_LIST_HEAD(&dev->pending_mmaps);
	spin_lock_init(&dev->pending_lock);
	dev->mmap_offset = PAGE_SIZE;
	spin_lock_init(&dev->mmap_offset_lock);
	INIT_LIST_HEAD(&dev->piowait);
	INIT_LIST_HEAD(&dev->dmawait);
	INIT_LIST_HEAD(&dev->txwait);
	INIT_LIST_HEAD(&dev->memwait);
	INIT_LIST_HEAD(&dev->txreq_free);

	if (ppd->sdma_descq_cnt) {
		dev->pio_hdrs = dma_alloc_coherent(&dd->pcidev->dev,
						ppd->sdma_descq_cnt *
						sizeof(struct qib_pio_header),
						&dev->pio_hdrs_phys,
						GFP_KERNEL);
		if (!dev->pio_hdrs) {
			ret = -ENOMEM;
			goto err_hdrs;
		}
	}

	for (i = 0; i < ppd->sdma_descq_cnt; i++) {
		struct qib_verbs_txreq *tx;

		tx = kzalloc(sizeof(*tx), GFP_KERNEL);
		if (!tx) {
			ret = -ENOMEM;
			goto err_tx;
		}
		tx->hdr_inx = i;
		list_add(&tx->txreq.list, &dev->txreq_free);
	}

	/*
	 * The system image GUID is supposed to be the same for all
	 * IB HCAs in a single system but since there can be other
	 * device types in the system, we can't be sure this is unique.
	 */
	if (!ib_qib_sys_image_guid)
		ib_qib_sys_image_guid = ppd->guid;

	strlcpy(ibdev->name, "qib%d", IB_DEVICE_NAME_MAX);
	ibdev->owner = THIS_MODULE;
	ibdev->node_guid = ppd->guid;
	ibdev->uverbs_abi_ver = QIB_UVERBS_ABI_VERSION;
	ibdev->uverbs_cmd_mask =
		(1ull << IB_USER_VERBS_CMD_GET_CONTEXT)         |
		(1ull << IB_USER_VERBS_CMD_QUERY_DEVICE)        |
		(1ull << IB_USER_VERBS_CMD_QUERY_PORT)          |
		(1ull << IB_USER_VERBS_CMD_ALLOC_PD)            |
		(1ull << IB_USER_VERBS_CMD_DEALLOC_PD)          |
		(1ull << IB_USER_VERBS_CMD_CREATE_AH)           |
		(1ull << IB_USER_VERBS_CMD_MODIFY_AH)           |
		(1ull << IB_USER_VERBS_CMD_QUERY_AH)            |
		(1ull << IB_USER_VERBS_CMD_DESTROY_AH)          |
		(1ull << IB_USER_VERBS_CMD_REG_MR)              |
		(1ull << IB_USER_VERBS_CMD_DEREG_MR)            |
		(1ull << IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL) |
		(1ull << IB_USER_VERBS_CMD_CREATE_CQ)           |
		(1ull << IB_USER_VERBS_CMD_RESIZE_CQ)           |
		(1ull << IB_USER_VERBS_CMD_DESTROY_CQ)          |
		(1ull << IB_USER_VERBS_CMD_POLL_CQ)             |
		(1ull << IB_USER_VERBS_CMD_REQ_NOTIFY_CQ)       |
		(1ull << IB_USER_VERBS_CMD_CREATE_QP)           |
		(1ull << IB_USER_VERBS_CMD_QUERY_QP)            |
		(1ull << IB_USER_VERBS_CMD_MODIFY_QP)           |
		(1ull << IB_USER_VERBS_CMD_DESTROY_QP)          |
		(1ull << IB_USER_VERBS_CMD_POST_SEND)           |
		(1ull << IB_USER_VERBS_CMD_POST_RECV)           |
		(1ull << IB_USER_VERBS_CMD_ATTACH_MCAST)        |
		(1ull << IB_USER_VERBS_CMD_DETACH_MCAST)        |
		(1ull << IB_USER_VERBS_CMD_CREATE_SRQ)          |
		(1ull << IB_USER_VERBS_CMD_MODIFY_SRQ)          |
		(1ull << IB_USER_VERBS_CMD_QUERY_SRQ)           |
		(1ull << IB_USER_VERBS_CMD_DESTROY_SRQ)         |
		(1ull << IB_USER_VERBS_CMD_POST_SRQ_RECV);
	ibdev->node_type = RDMA_NODE_IB_CA;
	ibdev->phys_port_cnt = dd->num_pports;
	ibdev->num_comp_vectors = 1;
	ibdev->dma_device = &dd->pcidev->dev;
	ibdev->query_device = qib_query_device;
	ibdev->modify_device = qib_modify_device;
	ibdev->query_port = qib_query_port;
	ibdev->modify_port = qib_modify_port;
	ibdev->query_pkey = qib_query_pkey;
	ibdev->query_gid = qib_query_gid;
	ibdev->alloc_ucontext = qib_alloc_ucontext;
	ibdev->dealloc_ucontext = qib_dealloc_ucontext;
	ibdev->alloc_pd = qib_alloc_pd;
	ibdev->dealloc_pd = qib_dealloc_pd;
	ibdev->create_ah = qib_create_ah;
	ibdev->destroy_ah = qib_destroy_ah;
	ibdev->modify_ah = qib_modify_ah;
	ibdev->query_ah = qib_query_ah;
	ibdev->create_srq = qib_create_srq;
	ibdev->modify_srq = qib_modify_srq;
	ibdev->query_srq = qib_query_srq;
	ibdev->destroy_srq = qib_destroy_srq;
	ibdev->create_qp = qib_create_qp;
	ibdev->modify_qp = qib_modify_qp;
	ibdev->query_qp = qib_query_qp;
	ibdev->destroy_qp = qib_destroy_qp;
	ibdev->post_send = qib_post_send;
	ibdev->post_recv = qib_post_receive;
	ibdev->post_srq_recv = qib_post_srq_receive;
	ibdev->create_cq = qib_create_cq;
	ibdev->destroy_cq = qib_destroy_cq;
	ibdev->resize_cq = qib_resize_cq;
	ibdev->poll_cq = qib_poll_cq;
	ibdev->req_notify_cq = qib_req_notify_cq;
	ibdev->get_dma_mr = qib_get_dma_mr;
	ibdev->reg_user_mr = qib_reg_user_mr;
	ibdev->dereg_mr = qib_dereg_mr;
	ibdev->alloc_mr = qib_alloc_mr;
	ibdev->map_mr_sg = qib_map_mr_sg;
	ibdev->alloc_fmr = qib_alloc_fmr;
	ibdev->map_phys_fmr = qib_map_phys_fmr;
	ibdev->unmap_fmr = qib_unmap_fmr;
	ibdev->dealloc_fmr = qib_dealloc_fmr;
	ibdev->attach_mcast = qib_multicast_attach;
	ibdev->detach_mcast = qib_multicast_detach;
	ibdev->process_mad = qib_process_mad;
	ibdev->mmap = qib_mmap;
	ibdev->dma_ops = &qib_dma_mapping_ops;
	ibdev->get_port_immutable = qib_port_immutable;

	snprintf(ibdev->node_desc, sizeof(ibdev->node_desc),
		 "Intel Infiniband HCA %s", init_utsname()->nodename);

	ret = ib_register_device(ibdev, qib_create_port_files);
	if (ret)
		goto err_reg;

	ret = qib_create_agents(dev);
	if (ret)
		goto err_agents;

	ret = qib_verbs_register_sysfs(dd);
	if (ret)
		goto err_class;

	goto bail;

err_class:
	qib_free_agents(dev);
err_agents:
	ib_unregister_device(ibdev);
err_reg:
err_tx:
	while (!list_empty(&dev->txreq_free)) {
		struct list_head *l = dev->txreq_free.next;
		struct qib_verbs_txreq *tx;

		list_del(l);
		tx = list_entry(l, struct qib_verbs_txreq, txreq.list);
		kfree(tx);
	}
	if (ppd->sdma_descq_cnt)
		dma_free_coherent(&dd->pcidev->dev,
				  ppd->sdma_descq_cnt *
					sizeof(struct qib_pio_header),
				  dev->pio_hdrs, dev->pio_hdrs_phys);
err_hdrs:
	vfree(dev->lk_table.table);
err_lk:
	kfree(dev->qp_table);
err_qpt:
	qib_dev_err(dd, "cannot register verbs: %d!\n", -ret);
bail:
	return ret;
}

void qib_unregister_ib_device(struct qib_devdata *dd)
{
	struct qib_ibdev *dev = &dd->verbs_dev;
	struct ib_device *ibdev = &dev->ibdev;
	u32 qps_inuse;
	unsigned lk_tab_size;

	qib_verbs_unregister_sysfs(dd);

	qib_free_agents(dev);

	ib_unregister_device(ibdev);

	if (!list_empty(&dev->piowait))
		qib_dev_err(dd, "piowait list not empty!\n");
	if (!list_empty(&dev->dmawait))
		qib_dev_err(dd, "dmawait list not empty!\n");
	if (!list_empty(&dev->txwait))
		qib_dev_err(dd, "txwait list not empty!\n");
	if (!list_empty(&dev->memwait))
		qib_dev_err(dd, "memwait list not empty!\n");
	if (dev->dma_mr)
		qib_dev_err(dd, "DMA MR not NULL!\n");

	qps_inuse = qib_free_all_qps(dd);
	if (qps_inuse)
		qib_dev_err(dd, "QP memory leak! %u still in use\n",
			    qps_inuse);

	del_timer_sync(&dev->mem_timer);
	qib_free_qpn_table(&dev->qpn_table);
	while (!list_empty(&dev->txreq_free)) {
		struct list_head *l = dev->txreq_free.next;
		struct qib_verbs_txreq *tx;

		list_del(l);
		tx = list_entry(l, struct qib_verbs_txreq, txreq.list);
		kfree(tx);
	}
	if (dd->pport->sdma_descq_cnt)
		dma_free_coherent(&dd->pcidev->dev,
				  dd->pport->sdma_descq_cnt *
					sizeof(struct qib_pio_header),
				  dev->pio_hdrs, dev->pio_hdrs_phys);
	lk_tab_size = dev->lk_table.max * sizeof(*dev->lk_table.table);
	vfree(dev->lk_table.table);
	kfree(dev->qp_table);
}

/*
 * This must be called with s_lock held.
 */
void qib_schedule_send(struct qib_qp *qp)
{
	if (qib_send_ok(qp)) {
		struct qib_ibport *ibp =
			to_iport(qp->ibqp.device, qp->port_num);
		struct qib_pportdata *ppd = ppd_from_ibp(ibp);

		queue_work(ppd->qib_wq, &qp->s_work);
	}
}
