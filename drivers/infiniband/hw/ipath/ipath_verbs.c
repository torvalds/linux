/*
 * Copyright (c) 2006 QLogic, Inc. All rights reserved.
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
#include <linux/utsname.h>

#include "ipath_kernel.h"
#include "ipath_verbs.h"
#include "ipath_common.h"

static unsigned int ib_ipath_qp_table_size = 251;
module_param_named(qp_table_size, ib_ipath_qp_table_size, uint, S_IRUGO);
MODULE_PARM_DESC(qp_table_size, "QP table size");

unsigned int ib_ipath_lkey_table_size = 12;
module_param_named(lkey_table_size, ib_ipath_lkey_table_size, uint,
		   S_IRUGO);
MODULE_PARM_DESC(lkey_table_size,
		 "LKEY table size in bits (2^n, 1 <= n <= 23)");

static unsigned int ib_ipath_max_pds = 0xFFFF;
module_param_named(max_pds, ib_ipath_max_pds, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(max_pds,
		 "Maximum number of protection domains to support");

static unsigned int ib_ipath_max_ahs = 0xFFFF;
module_param_named(max_ahs, ib_ipath_max_ahs, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(max_ahs, "Maximum number of address handles to support");

unsigned int ib_ipath_max_cqes = 0x2FFFF;
module_param_named(max_cqes, ib_ipath_max_cqes, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(max_cqes,
		 "Maximum number of completion queue entries to support");

unsigned int ib_ipath_max_cqs = 0x1FFFF;
module_param_named(max_cqs, ib_ipath_max_cqs, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(max_cqs, "Maximum number of completion queues to support");

unsigned int ib_ipath_max_qp_wrs = 0x3FFF;
module_param_named(max_qp_wrs, ib_ipath_max_qp_wrs, uint,
		   S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(max_qp_wrs, "Maximum number of QP WRs to support");

unsigned int ib_ipath_max_qps = 16384;
module_param_named(max_qps, ib_ipath_max_qps, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(max_qps, "Maximum number of QPs to support");

unsigned int ib_ipath_max_sges = 0x60;
module_param_named(max_sges, ib_ipath_max_sges, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(max_sges, "Maximum number of SGEs to support");

unsigned int ib_ipath_max_mcast_grps = 16384;
module_param_named(max_mcast_grps, ib_ipath_max_mcast_grps, uint,
		   S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(max_mcast_grps,
		 "Maximum number of multicast groups to support");

unsigned int ib_ipath_max_mcast_qp_attached = 16;
module_param_named(max_mcast_qp_attached, ib_ipath_max_mcast_qp_attached,
		   uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(max_mcast_qp_attached,
		 "Maximum number of attached QPs to support");

unsigned int ib_ipath_max_srqs = 1024;
module_param_named(max_srqs, ib_ipath_max_srqs, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(max_srqs, "Maximum number of SRQs to support");

unsigned int ib_ipath_max_srq_sges = 128;
module_param_named(max_srq_sges, ib_ipath_max_srq_sges,
		   uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(max_srq_sges, "Maximum number of SRQ SGEs to support");

unsigned int ib_ipath_max_srq_wrs = 0x1FFFF;
module_param_named(max_srq_wrs, ib_ipath_max_srq_wrs,
		   uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(max_srq_wrs, "Maximum number of SRQ WRs support");

static unsigned int ib_ipath_disable_sma;
module_param_named(disable_sma, ib_ipath_disable_sma, uint, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(ib_ipath_disable_sma, "Disable the SMA");

const int ib_ipath_state_ops[IB_QPS_ERR + 1] = {
	[IB_QPS_RESET] = 0,
	[IB_QPS_INIT] = IPATH_POST_RECV_OK,
	[IB_QPS_RTR] = IPATH_POST_RECV_OK | IPATH_PROCESS_RECV_OK,
	[IB_QPS_RTS] = IPATH_POST_RECV_OK | IPATH_PROCESS_RECV_OK |
	    IPATH_POST_SEND_OK | IPATH_PROCESS_SEND_OK,
	[IB_QPS_SQD] = IPATH_POST_RECV_OK | IPATH_PROCESS_RECV_OK |
	    IPATH_POST_SEND_OK,
	[IB_QPS_SQE] = IPATH_POST_RECV_OK | IPATH_PROCESS_RECV_OK,
	[IB_QPS_ERR] = 0,
};

struct ipath_ucontext {
	struct ib_ucontext ibucontext;
};

static inline struct ipath_ucontext *to_iucontext(struct ib_ucontext
						  *ibucontext)
{
	return container_of(ibucontext, struct ipath_ucontext, ibucontext);
}

/*
 * Translate ib_wr_opcode into ib_wc_opcode.
 */
const enum ib_wc_opcode ib_ipath_wc_opcode[] = {
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
static __be64 sys_image_guid;

/**
 * ipath_copy_sge - copy data to SGE memory
 * @ss: the SGE state
 * @data: the data to copy
 * @length: the length of the data
 */
void ipath_copy_sge(struct ipath_sge_state *ss, void *data, u32 length)
{
	struct ipath_sge *sge = &ss->sge;

	while (length) {
		u32 len = sge->length;

		BUG_ON(len == 0);
		if (len > length)
			len = length;
		memcpy(sge->vaddr, data, len);
		sge->vaddr += len;
		sge->length -= len;
		sge->sge_length -= len;
		if (sge->sge_length == 0) {
			if (--ss->num_sge)
				*sge = *ss->sg_list++;
		} else if (sge->length == 0 && sge->mr != NULL) {
			if (++sge->n >= IPATH_SEGSZ) {
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
 * ipath_skip_sge - skip over SGE memory - XXX almost dup of prev func
 * @ss: the SGE state
 * @length: the number of bytes to skip
 */
void ipath_skip_sge(struct ipath_sge_state *ss, u32 length)
{
	struct ipath_sge *sge = &ss->sge;

	while (length) {
		u32 len = sge->length;

		BUG_ON(len == 0);
		if (len > length)
			len = length;
		sge->vaddr += len;
		sge->length -= len;
		sge->sge_length -= len;
		if (sge->sge_length == 0) {
			if (--ss->num_sge)
				*sge = *ss->sg_list++;
		} else if (sge->length == 0 && sge->mr != NULL) {
			if (++sge->n >= IPATH_SEGSZ) {
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

/**
 * ipath_post_send - post a send on a QP
 * @ibqp: the QP to post the send on
 * @wr: the list of work requests to post
 * @bad_wr: the first bad WR is put here
 *
 * This may be called from interrupt context.
 */
static int ipath_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
			   struct ib_send_wr **bad_wr)
{
	struct ipath_qp *qp = to_iqp(ibqp);
	int err = 0;

	/* Check that state is OK to post send. */
	if (!(ib_ipath_state_ops[qp->state] & IPATH_POST_SEND_OK)) {
		*bad_wr = wr;
		err = -EINVAL;
		goto bail;
	}

	for (; wr; wr = wr->next) {
		switch (qp->ibqp.qp_type) {
		case IB_QPT_UC:
		case IB_QPT_RC:
			err = ipath_post_ruc_send(qp, wr);
			break;

		case IB_QPT_SMI:
		case IB_QPT_GSI:
		case IB_QPT_UD:
			err = ipath_post_ud_send(qp, wr);
			break;

		default:
			err = -EINVAL;
		}
		if (err) {
			*bad_wr = wr;
			break;
		}
	}

bail:
	return err;
}

/**
 * ipath_post_receive - post a receive on a QP
 * @ibqp: the QP to post the receive on
 * @wr: the WR to post
 * @bad_wr: the first bad WR is put here
 *
 * This may be called from interrupt context.
 */
static int ipath_post_receive(struct ib_qp *ibqp, struct ib_recv_wr *wr,
			      struct ib_recv_wr **bad_wr)
{
	struct ipath_qp *qp = to_iqp(ibqp);
	struct ipath_rwq *wq = qp->r_rq.wq;
	unsigned long flags;
	int ret;

	/* Check that state is OK to post receive. */
	if (!(ib_ipath_state_ops[qp->state] & IPATH_POST_RECV_OK) || !wq) {
		*bad_wr = wr;
		ret = -EINVAL;
		goto bail;
	}

	for (; wr; wr = wr->next) {
		struct ipath_rwqe *wqe;
		u32 next;
		int i;

		if ((unsigned) wr->num_sge > qp->r_rq.max_sge) {
			*bad_wr = wr;
			ret = -ENOMEM;
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
		wq->head = next;
		spin_unlock_irqrestore(&qp->r_rq.lock, flags);
	}
	ret = 0;

bail:
	return ret;
}

/**
 * ipath_qp_rcv - processing an incoming packet on a QP
 * @dev: the device the packet came on
 * @hdr: the packet header
 * @has_grh: true if the packet has a GRH
 * @data: the packet data
 * @tlen: the packet length
 * @qp: the QP the packet came on
 *
 * This is called from ipath_ib_rcv() to process an incoming packet
 * for the given QP.
 * Called at interrupt level.
 */
static void ipath_qp_rcv(struct ipath_ibdev *dev,
			 struct ipath_ib_header *hdr, int has_grh,
			 void *data, u32 tlen, struct ipath_qp *qp)
{
	/* Check for valid receive state. */
	if (!(ib_ipath_state_ops[qp->state] & IPATH_PROCESS_RECV_OK)) {
		dev->n_pkt_drops++;
		return;
	}

	switch (qp->ibqp.qp_type) {
	case IB_QPT_SMI:
	case IB_QPT_GSI:
		if (ib_ipath_disable_sma)
			break;
		/* FALLTHROUGH */
	case IB_QPT_UD:
		ipath_ud_rcv(dev, hdr, has_grh, data, tlen, qp);
		break;

	case IB_QPT_RC:
		ipath_rc_rcv(dev, hdr, has_grh, data, tlen, qp);
		break;

	case IB_QPT_UC:
		ipath_uc_rcv(dev, hdr, has_grh, data, tlen, qp);
		break;

	default:
		break;
	}
}

/**
 * ipath_ib_rcv - process an incoming packet
 * @arg: the device pointer
 * @rhdr: the header of the packet
 * @data: the packet data
 * @tlen: the packet length
 *
 * This is called from ipath_kreceive() to process an incoming packet at
 * interrupt level. Tlen is the length of the header + data + CRC in bytes.
 */
void ipath_ib_rcv(struct ipath_ibdev *dev, void *rhdr, void *data,
		  u32 tlen)
{
	struct ipath_ib_header *hdr = rhdr;
	struct ipath_other_headers *ohdr;
	struct ipath_qp *qp;
	u32 qp_num;
	int lnh;
	u8 opcode;
	u16 lid;

	if (unlikely(dev == NULL))
		goto bail;

	if (unlikely(tlen < 24)) {	/* LRH+BTH+CRC */
		dev->rcv_errors++;
		goto bail;
	}

	/* Check for a valid destination LID (see ch. 7.11.1). */
	lid = be16_to_cpu(hdr->lrh[1]);
	if (lid < IPATH_MULTICAST_LID_BASE) {
		lid &= ~((1 << (dev->mkeyprot_resv_lmc & 7)) - 1);
		if (unlikely(lid != dev->dd->ipath_lid)) {
			dev->rcv_errors++;
			goto bail;
		}
	}

	/* Check for GRH */
	lnh = be16_to_cpu(hdr->lrh[0]) & 3;
	if (lnh == IPATH_LRH_BTH)
		ohdr = &hdr->u.oth;
	else if (lnh == IPATH_LRH_GRH)
		ohdr = &hdr->u.l.oth;
	else {
		dev->rcv_errors++;
		goto bail;
	}

	opcode = be32_to_cpu(ohdr->bth[0]) >> 24;
	dev->opstats[opcode].n_bytes += tlen;
	dev->opstats[opcode].n_packets++;

	/* Get the destination QP number. */
	qp_num = be32_to_cpu(ohdr->bth[1]) & IPATH_QPN_MASK;
	if (qp_num == IPATH_MULTICAST_QPN) {
		struct ipath_mcast *mcast;
		struct ipath_mcast_qp *p;

		if (lnh != IPATH_LRH_GRH) {
			dev->n_pkt_drops++;
			goto bail;
		}
		mcast = ipath_mcast_find(&hdr->u.l.grh.dgid);
		if (mcast == NULL) {
			dev->n_pkt_drops++;
			goto bail;
		}
		dev->n_multicast_rcv++;
		list_for_each_entry_rcu(p, &mcast->qp_list, list)
			ipath_qp_rcv(dev, hdr, 1, data, tlen, p->qp);
		/*
		 * Notify ipath_multicast_detach() if it is waiting for us
		 * to finish.
		 */
		if (atomic_dec_return(&mcast->refcount) <= 1)
			wake_up(&mcast->wait);
	} else {
		qp = ipath_lookup_qpn(&dev->qp_table, qp_num);
		if (qp) {
			dev->n_unicast_rcv++;
			ipath_qp_rcv(dev, hdr, lnh == IPATH_LRH_GRH, data,
				     tlen, qp);
			/*
			 * Notify ipath_destroy_qp() if it is waiting
			 * for us to finish.
			 */
			if (atomic_dec_and_test(&qp->refcount))
				wake_up(&qp->wait);
		} else
			dev->n_pkt_drops++;
	}

bail:;
}

/**
 * ipath_ib_timer - verbs timer
 * @arg: the device pointer
 *
 * This is called from ipath_do_rcv_timer() at interrupt level to check for
 * QPs which need retransmits and to collect performance numbers.
 */
void ipath_ib_timer(struct ipath_ibdev *dev)
{
	struct ipath_qp *resend = NULL;
	struct list_head *last;
	struct ipath_qp *qp;
	unsigned long flags;

	if (dev == NULL)
		return;

	spin_lock_irqsave(&dev->pending_lock, flags);
	/* Start filling the next pending queue. */
	if (++dev->pending_index >= ARRAY_SIZE(dev->pending))
		dev->pending_index = 0;
	/* Save any requests still in the new queue, they have timed out. */
	last = &dev->pending[dev->pending_index];
	while (!list_empty(last)) {
		qp = list_entry(last->next, struct ipath_qp, timerwait);
		list_del_init(&qp->timerwait);
		qp->timer_next = resend;
		resend = qp;
		atomic_inc(&qp->refcount);
	}
	last = &dev->rnrwait;
	if (!list_empty(last)) {
		qp = list_entry(last->next, struct ipath_qp, timerwait);
		if (--qp->s_rnr_timeout == 0) {
			do {
				list_del_init(&qp->timerwait);
				tasklet_hi_schedule(&qp->s_task);
				if (list_empty(last))
					break;
				qp = list_entry(last->next, struct ipath_qp,
						timerwait);
			} while (qp->s_rnr_timeout == 0);
		}
	}
	/*
	 * We should only be in the started state if pma_sample_start != 0
	 */
	if (dev->pma_sample_status == IB_PMA_SAMPLE_STATUS_STARTED &&
	    --dev->pma_sample_start == 0) {
		dev->pma_sample_status = IB_PMA_SAMPLE_STATUS_RUNNING;
		ipath_snapshot_counters(dev->dd, &dev->ipath_sword,
					&dev->ipath_rword,
					&dev->ipath_spkts,
					&dev->ipath_rpkts,
					&dev->ipath_xmit_wait);
	}
	if (dev->pma_sample_status == IB_PMA_SAMPLE_STATUS_RUNNING) {
		if (dev->pma_sample_interval == 0) {
			u64 ta, tb, tc, td, te;

			dev->pma_sample_status = IB_PMA_SAMPLE_STATUS_DONE;
			ipath_snapshot_counters(dev->dd, &ta, &tb,
						&tc, &td, &te);

			dev->ipath_sword = ta - dev->ipath_sword;
			dev->ipath_rword = tb - dev->ipath_rword;
			dev->ipath_spkts = tc - dev->ipath_spkts;
			dev->ipath_rpkts = td - dev->ipath_rpkts;
			dev->ipath_xmit_wait = te - dev->ipath_xmit_wait;
		}
		else
			dev->pma_sample_interval--;
	}
	spin_unlock_irqrestore(&dev->pending_lock, flags);

	/* XXX What if timer fires again while this is running? */
	for (qp = resend; qp != NULL; qp = qp->timer_next) {
		struct ib_wc wc;

		spin_lock_irqsave(&qp->s_lock, flags);
		if (qp->s_last != qp->s_tail && qp->state == IB_QPS_RTS) {
			dev->n_timeouts++;
			ipath_restart_rc(qp, qp->s_last_psn + 1, &wc);
		}
		spin_unlock_irqrestore(&qp->s_lock, flags);

		/* Notify ipath_destroy_qp() if it is waiting. */
		if (atomic_dec_and_test(&qp->refcount))
			wake_up(&qp->wait);
	}
}

static void update_sge(struct ipath_sge_state *ss, u32 length)
{
	struct ipath_sge *sge = &ss->sge;

	sge->vaddr += length;
	sge->length -= length;
	sge->sge_length -= length;
	if (sge->sge_length == 0) {
		if (--ss->num_sge)
			*sge = *ss->sg_list++;
	} else if (sge->length == 0 && sge->mr != NULL) {
		if (++sge->n >= IPATH_SEGSZ) {
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

static void copy_io(u32 __iomem *piobuf, struct ipath_sge_state *ss,
		    u32 length)
{
	u32 extra = 0;
	u32 data = 0;
	u32 last;

	while (1) {
		u32 len = ss->sge.length;
		u32 off;

		BUG_ON(len == 0);
		if (len > length)
			len = length;
		if (len > ss->sge.sge_length)
			len = ss->sge.sge_length;
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
					data |= clear_upper_bytes(v, l,
								  extra);
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
			__iowrite32_copy(piobuf, ss->sge.vaddr, w - 1);
			piobuf += w - 1;
			last = ((u32 *) ss->sge.vaddr)[w - 1];
			break;
		} else {
			u32 w = len >> 2;

			__iowrite32_copy(piobuf, ss->sge.vaddr, w);
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
	/* must flush early everything before trigger word */
	ipath_flush_wc();
	__raw_writel(last, piobuf);
	/* be sure trigger word is written */
	ipath_flush_wc();
}

/**
 * ipath_verbs_send - send a packet
 * @dd: the infinipath device
 * @hdrwords: the number of words in the header
 * @hdr: the packet header
 * @len: the length of the packet in bytes
 * @ss: the SGE to send
 */
int ipath_verbs_send(struct ipath_devdata *dd, u32 hdrwords,
		     u32 *hdr, u32 len, struct ipath_sge_state *ss)
{
	u32 __iomem *piobuf;
	u32 plen;
	int ret;

	/* +1 is for the qword padding of pbc */
	plen = hdrwords + ((len + 3) >> 2) + 1;
	if (unlikely((plen << 2) > dd->ipath_ibmaxlen)) {
		ret = -EINVAL;
		goto bail;
	}

	/* Get a PIO buffer to use. */
	piobuf = ipath_getpiobuf(dd, NULL);
	if (unlikely(piobuf == NULL)) {
		ret = -EBUSY;
		goto bail;
	}

	/*
	 * Write len to control qword, no flags.
	 * We have to flush after the PBC for correctness on some cpus
	 * or WC buffer can be written out of order.
	 */
	writeq(plen, piobuf);
	ipath_flush_wc();
	piobuf += 2;
	if (len == 0) {
		/*
		 * If there is just the header portion, must flush before
		 * writing last word of header for correctness, and after
		 * the last header word (trigger word).
		 */
		__iowrite32_copy(piobuf, hdr, hdrwords - 1);
		ipath_flush_wc();
		__raw_writel(hdr[hdrwords - 1], piobuf + hdrwords - 1);
		ipath_flush_wc();
		ret = 0;
		goto bail;
	}

	__iowrite32_copy(piobuf, hdr, hdrwords);
	piobuf += hdrwords;

	/* The common case is aligned and contained in one segment. */
	if (likely(ss->num_sge == 1 && len <= ss->sge.length &&
		   !((unsigned long)ss->sge.vaddr & (sizeof(u32) - 1)))) {
		u32 w;
		u32 *addr = (u32 *) ss->sge.vaddr;

		/* Update address before sending packet. */
		update_sge(ss, len);
		/* Need to round up for the last dword in the packet. */
		w = (len + 3) >> 2;
		__iowrite32_copy(piobuf, addr, w - 1);
		/* must flush early everything before trigger word */
		ipath_flush_wc();
		__raw_writel(addr[w - 1], piobuf + w - 1);
		/* be sure trigger word is written */
		ipath_flush_wc();
		ret = 0;
		goto bail;
	}
	copy_io(piobuf, ss, len);
	ret = 0;

bail:
	return ret;
}

int ipath_snapshot_counters(struct ipath_devdata *dd, u64 *swords,
			    u64 *rwords, u64 *spkts, u64 *rpkts,
			    u64 *xmit_wait)
{
	int ret;

	if (!(dd->ipath_flags & IPATH_INITTED)) {
		/* no hardware, freeze, etc. */
		ipath_dbg("unit %u not usable\n", dd->ipath_unit);
		ret = -EINVAL;
		goto bail;
	}
	*swords = ipath_snap_cntr(dd, dd->ipath_cregs->cr_wordsendcnt);
	*rwords = ipath_snap_cntr(dd, dd->ipath_cregs->cr_wordrcvcnt);
	*spkts = ipath_snap_cntr(dd, dd->ipath_cregs->cr_pktsendcnt);
	*rpkts = ipath_snap_cntr(dd, dd->ipath_cregs->cr_pktrcvcnt);
	*xmit_wait = ipath_snap_cntr(dd, dd->ipath_cregs->cr_sendstallcnt);

	ret = 0;

bail:
	return ret;
}

/**
 * ipath_get_counters - get various chip counters
 * @dd: the infinipath device
 * @cntrs: counters are placed here
 *
 * Return the counters needed by recv_pma_get_portcounters().
 */
int ipath_get_counters(struct ipath_devdata *dd,
		       struct ipath_verbs_counters *cntrs)
{
	int ret;

	if (!(dd->ipath_flags & IPATH_INITTED)) {
		/* no hardware, freeze, etc. */
		ipath_dbg("unit %u not usable\n", dd->ipath_unit);
		ret = -EINVAL;
		goto bail;
	}
	cntrs->symbol_error_counter =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_ibsymbolerrcnt);
	cntrs->link_error_recovery_counter =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_iblinkerrrecovcnt);
	/*
	 * The link downed counter counts when the other side downs the
	 * connection.  We add in the number of times we downed the link
	 * due to local link integrity errors to compensate.
	 */
	cntrs->link_downed_counter =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_iblinkdowncnt);
	cntrs->port_rcv_errors =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_rxdroppktcnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_rcvovflcnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_portovflcnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_err_rlencnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_invalidrlencnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_erricrccnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_errvcrccnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_errlpcrccnt) +
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_badformatcnt) +
		dd->ipath_rxfc_unsupvl_errs;
	cntrs->port_rcv_remphys_errors =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_rcvebpcnt);
	cntrs->port_xmit_discards =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_unsupvlcnt);
	cntrs->port_xmit_data =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_wordsendcnt);
	cntrs->port_rcv_data =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_wordrcvcnt);
	cntrs->port_xmit_packets =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_pktsendcnt);
	cntrs->port_rcv_packets =
		ipath_snap_cntr(dd, dd->ipath_cregs->cr_pktrcvcnt);
	cntrs->local_link_integrity_errors =
		(dd->ipath_flags & IPATH_GPIO_ERRINTRS) ?
		dd->ipath_lli_errs : dd->ipath_lli_errors;
	cntrs->excessive_buffer_overrun_errors = dd->ipath_overrun_thresh_errs;

	ret = 0;

bail:
	return ret;
}

/**
 * ipath_ib_piobufavail - callback when a PIO buffer is available
 * @arg: the device pointer
 *
 * This is called from ipath_intr() at interrupt level when a PIO buffer is
 * available after ipath_verbs_send() returned an error that no buffers were
 * available.  Return 1 if we consumed all the PIO buffers and we still have
 * QPs waiting for buffers (for now, just do a tasklet_hi_schedule and
 * return zero).
 */
int ipath_ib_piobufavail(struct ipath_ibdev *dev)
{
	struct ipath_qp *qp;
	unsigned long flags;

	if (dev == NULL)
		goto bail;

	spin_lock_irqsave(&dev->pending_lock, flags);
	while (!list_empty(&dev->piowait)) {
		qp = list_entry(dev->piowait.next, struct ipath_qp,
				piowait);
		list_del_init(&qp->piowait);
		tasklet_hi_schedule(&qp->s_task);
	}
	spin_unlock_irqrestore(&dev->pending_lock, flags);

bail:
	return 0;
}

static int ipath_query_device(struct ib_device *ibdev,
			      struct ib_device_attr *props)
{
	struct ipath_ibdev *dev = to_idev(ibdev);

	memset(props, 0, sizeof(*props));

	props->device_cap_flags = IB_DEVICE_BAD_PKEY_CNTR |
		IB_DEVICE_BAD_QKEY_CNTR | IB_DEVICE_SHUTDOWN_PORT |
		IB_DEVICE_SYS_IMAGE_GUID;
	props->page_size_cap = PAGE_SIZE;
	props->vendor_id = dev->dd->ipath_vendorid;
	props->vendor_part_id = dev->dd->ipath_deviceid;
	props->hw_ver = dev->dd->ipath_pcirev;

	props->sys_image_guid = dev->sys_image_guid;

	props->max_mr_size = ~0ull;
	props->max_qp = ib_ipath_max_qps;
	props->max_qp_wr = ib_ipath_max_qp_wrs;
	props->max_sge = ib_ipath_max_sges;
	props->max_cq = ib_ipath_max_cqs;
	props->max_ah = ib_ipath_max_ahs;
	props->max_cqe = ib_ipath_max_cqes;
	props->max_mr = dev->lk_table.max;
	props->max_pd = ib_ipath_max_pds;
	props->max_qp_rd_atom = IPATH_MAX_RDMA_ATOMIC;
	props->max_qp_init_rd_atom = 255;
	/* props->max_res_rd_atom */
	props->max_srq = ib_ipath_max_srqs;
	props->max_srq_wr = ib_ipath_max_srq_wrs;
	props->max_srq_sge = ib_ipath_max_srq_sges;
	/* props->local_ca_ack_delay */
	props->atomic_cap = IB_ATOMIC_GLOB;
	props->max_pkeys = ipath_get_npkeys(dev->dd);
	props->max_mcast_grp = ib_ipath_max_mcast_grps;
	props->max_mcast_qp_attach = ib_ipath_max_mcast_qp_attached;
	props->max_total_mcast_qp_attach = props->max_mcast_qp_attach *
		props->max_mcast_grp;

	return 0;
}

const u8 ipath_cvt_physportstate[16] = {
	[INFINIPATH_IBCS_LT_STATE_DISABLED] = 3,
	[INFINIPATH_IBCS_LT_STATE_LINKUP] = 5,
	[INFINIPATH_IBCS_LT_STATE_POLLACTIVE] = 2,
	[INFINIPATH_IBCS_LT_STATE_POLLQUIET] = 2,
	[INFINIPATH_IBCS_LT_STATE_SLEEPDELAY] = 1,
	[INFINIPATH_IBCS_LT_STATE_SLEEPQUIET] = 1,
	[INFINIPATH_IBCS_LT_STATE_CFGDEBOUNCE] = 4,
	[INFINIPATH_IBCS_LT_STATE_CFGRCVFCFG] = 4,
	[INFINIPATH_IBCS_LT_STATE_CFGWAITRMT] = 4,
	[INFINIPATH_IBCS_LT_STATE_CFGIDLE] = 4,
	[INFINIPATH_IBCS_LT_STATE_RECOVERRETRAIN] = 6,
	[INFINIPATH_IBCS_LT_STATE_RECOVERWAITRMT] = 6,
	[INFINIPATH_IBCS_LT_STATE_RECOVERIDLE] = 6,
};

u32 ipath_get_cr_errpkey(struct ipath_devdata *dd)
{
	return ipath_read_creg32(dd, dd->ipath_cregs->cr_errpkey);
}

static int ipath_query_port(struct ib_device *ibdev,
			    u8 port, struct ib_port_attr *props)
{
	struct ipath_ibdev *dev = to_idev(ibdev);
	enum ib_mtu mtu;
	u16 lid = dev->dd->ipath_lid;
	u64 ibcstat;

	memset(props, 0, sizeof(*props));
	props->lid = lid ? lid : __constant_be16_to_cpu(IB_LID_PERMISSIVE);
	props->lmc = dev->mkeyprot_resv_lmc & 7;
	props->sm_lid = dev->sm_lid;
	props->sm_sl = dev->sm_sl;
	ibcstat = dev->dd->ipath_lastibcstat;
	props->state = ((ibcstat >> 4) & 0x3) + 1;
	/* See phys_state_show() */
	props->phys_state = ipath_cvt_physportstate[
		dev->dd->ipath_lastibcstat & 0xf];
	props->port_cap_flags = dev->port_cap_flags;
	props->gid_tbl_len = 1;
	props->max_msg_sz = 0x80000000;
	props->pkey_tbl_len = ipath_get_npkeys(dev->dd);
	props->bad_pkey_cntr = ipath_get_cr_errpkey(dev->dd) -
		dev->z_pkey_violations;
	props->qkey_viol_cntr = dev->qkey_violations;
	props->active_width = IB_WIDTH_4X;
	/* See rate_show() */
	props->active_speed = 1;	/* Regular 10Mbs speed. */
	props->max_vl_num = 1;		/* VLCap = VL0 */
	props->init_type_reply = 0;

	props->max_mtu = IB_MTU_4096;
	switch (dev->dd->ipath_ibmtu) {
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
	props->subnet_timeout = dev->subnet_timeout;

	return 0;
}

static int ipath_modify_device(struct ib_device *device,
			       int device_modify_mask,
			       struct ib_device_modify *device_modify)
{
	int ret;

	if (device_modify_mask & ~(IB_DEVICE_MODIFY_SYS_IMAGE_GUID |
				   IB_DEVICE_MODIFY_NODE_DESC)) {
		ret = -EOPNOTSUPP;
		goto bail;
	}

	if (device_modify_mask & IB_DEVICE_MODIFY_NODE_DESC)
		memcpy(device->node_desc, device_modify->node_desc, 64);

	if (device_modify_mask & IB_DEVICE_MODIFY_SYS_IMAGE_GUID)
		to_idev(device)->sys_image_guid =
			cpu_to_be64(device_modify->sys_image_guid);

	ret = 0;

bail:
	return ret;
}

static int ipath_modify_port(struct ib_device *ibdev,
			     u8 port, int port_modify_mask,
			     struct ib_port_modify *props)
{
	struct ipath_ibdev *dev = to_idev(ibdev);

	dev->port_cap_flags |= props->set_port_cap_mask;
	dev->port_cap_flags &= ~props->clr_port_cap_mask;
	if (port_modify_mask & IB_PORT_SHUTDOWN)
		ipath_set_linkstate(dev->dd, IPATH_IB_LINKDOWN);
	if (port_modify_mask & IB_PORT_RESET_QKEY_CNTR)
		dev->qkey_violations = 0;
	return 0;
}

static int ipath_query_gid(struct ib_device *ibdev, u8 port,
			   int index, union ib_gid *gid)
{
	struct ipath_ibdev *dev = to_idev(ibdev);
	int ret;

	if (index >= 1) {
		ret = -EINVAL;
		goto bail;
	}
	gid->global.subnet_prefix = dev->gid_prefix;
	gid->global.interface_id = dev->dd->ipath_guid;

	ret = 0;

bail:
	return ret;
}

static struct ib_pd *ipath_alloc_pd(struct ib_device *ibdev,
				    struct ib_ucontext *context,
				    struct ib_udata *udata)
{
	struct ipath_ibdev *dev = to_idev(ibdev);
	struct ipath_pd *pd;
	struct ib_pd *ret;

	/*
	 * This is actually totally arbitrary.	Some correctness tests
	 * assume there's a maximum number of PDs that can be allocated.
	 * We don't actually have this limit, but we fail the test if
	 * we allow allocations of more than we report for this value.
	 */

	pd = kmalloc(sizeof *pd, GFP_KERNEL);
	if (!pd) {
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}

	spin_lock(&dev->n_pds_lock);
	if (dev->n_pds_allocated == ib_ipath_max_pds) {
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

static int ipath_dealloc_pd(struct ib_pd *ibpd)
{
	struct ipath_pd *pd = to_ipd(ibpd);
	struct ipath_ibdev *dev = to_idev(ibpd->device);

	spin_lock(&dev->n_pds_lock);
	dev->n_pds_allocated--;
	spin_unlock(&dev->n_pds_lock);

	kfree(pd);

	return 0;
}

/**
 * ipath_create_ah - create an address handle
 * @pd: the protection domain
 * @ah_attr: the attributes of the AH
 *
 * This may be called from interrupt context.
 */
static struct ib_ah *ipath_create_ah(struct ib_pd *pd,
				     struct ib_ah_attr *ah_attr)
{
	struct ipath_ah *ah;
	struct ib_ah *ret;
	struct ipath_ibdev *dev = to_idev(pd->device);
	unsigned long flags;

	/* A multicast address requires a GRH (see ch. 8.4.1). */
	if (ah_attr->dlid >= IPATH_MULTICAST_LID_BASE &&
	    ah_attr->dlid != IPATH_PERMISSIVE_LID &&
	    !(ah_attr->ah_flags & IB_AH_GRH)) {
		ret = ERR_PTR(-EINVAL);
		goto bail;
	}

	if (ah_attr->dlid == 0) {
		ret = ERR_PTR(-EINVAL);
		goto bail;
	}

	if (ah_attr->port_num < 1 ||
	    ah_attr->port_num > pd->device->phys_port_cnt) {
		ret = ERR_PTR(-EINVAL);
		goto bail;
	}

	ah = kmalloc(sizeof *ah, GFP_ATOMIC);
	if (!ah) {
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}

	spin_lock_irqsave(&dev->n_ahs_lock, flags);
	if (dev->n_ahs_allocated == ib_ipath_max_ahs) {
		spin_unlock_irqrestore(&dev->n_ahs_lock, flags);
		kfree(ah);
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}

	dev->n_ahs_allocated++;
	spin_unlock_irqrestore(&dev->n_ahs_lock, flags);

	/* ib_create_ah() will initialize ah->ibah. */
	ah->attr = *ah_attr;

	ret = &ah->ibah;

bail:
	return ret;
}

/**
 * ipath_destroy_ah - destroy an address handle
 * @ibah: the AH to destroy
 *
 * This may be called from interrupt context.
 */
static int ipath_destroy_ah(struct ib_ah *ibah)
{
	struct ipath_ibdev *dev = to_idev(ibah->device);
	struct ipath_ah *ah = to_iah(ibah);
	unsigned long flags;

	spin_lock_irqsave(&dev->n_ahs_lock, flags);
	dev->n_ahs_allocated--;
	spin_unlock_irqrestore(&dev->n_ahs_lock, flags);

	kfree(ah);

	return 0;
}

static int ipath_query_ah(struct ib_ah *ibah, struct ib_ah_attr *ah_attr)
{
	struct ipath_ah *ah = to_iah(ibah);

	*ah_attr = ah->attr;

	return 0;
}

/**
 * ipath_get_npkeys - return the size of the PKEY table for port 0
 * @dd: the infinipath device
 */
unsigned ipath_get_npkeys(struct ipath_devdata *dd)
{
	return ARRAY_SIZE(dd->ipath_pd[0]->port_pkeys);
}

/**
 * ipath_get_pkey - return the indexed PKEY from the port 0 PKEY table
 * @dd: the infinipath device
 * @index: the PKEY index
 */
unsigned ipath_get_pkey(struct ipath_devdata *dd, unsigned index)
{
	unsigned ret;

	if (index >= ARRAY_SIZE(dd->ipath_pd[0]->port_pkeys))
		ret = 0;
	else
		ret = dd->ipath_pd[0]->port_pkeys[index];

	return ret;
}

static int ipath_query_pkey(struct ib_device *ibdev, u8 port, u16 index,
			    u16 *pkey)
{
	struct ipath_ibdev *dev = to_idev(ibdev);
	int ret;

	if (index >= ipath_get_npkeys(dev->dd)) {
		ret = -EINVAL;
		goto bail;
	}

	*pkey = ipath_get_pkey(dev->dd, index);
	ret = 0;

bail:
	return ret;
}

/**
 * ipath_alloc_ucontext - allocate a ucontest
 * @ibdev: the infiniband device
 * @udata: not used by the InfiniPath driver
 */

static struct ib_ucontext *ipath_alloc_ucontext(struct ib_device *ibdev,
						struct ib_udata *udata)
{
	struct ipath_ucontext *context;
	struct ib_ucontext *ret;

	context = kmalloc(sizeof *context, GFP_KERNEL);
	if (!context) {
		ret = ERR_PTR(-ENOMEM);
		goto bail;
	}

	ret = &context->ibucontext;

bail:
	return ret;
}

static int ipath_dealloc_ucontext(struct ib_ucontext *context)
{
	kfree(to_iucontext(context));
	return 0;
}

static int ipath_verbs_register_sysfs(struct ib_device *dev);

static void __verbs_timer(unsigned long arg)
{
	struct ipath_devdata *dd = (struct ipath_devdata *) arg;

	/*
	 * If port 0 receive packet interrupts are not available, or
	 * can be missed, poll the receive queue
	 */
	if (dd->ipath_flags & IPATH_POLL_RX_INTR)
		ipath_kreceive(dd);

	/* Handle verbs layer timeouts. */
	ipath_ib_timer(dd->verbs_dev);

	mod_timer(&dd->verbs_timer, jiffies + 1);
}

static int enable_timer(struct ipath_devdata *dd)
{
	/*
	 * Early chips had a design flaw where the chip and kernel idea
	 * of the tail register don't always agree, and therefore we won't
	 * get an interrupt on the next packet received.
	 * If the board supports per packet receive interrupts, use it.
	 * Otherwise, the timer function periodically checks for packets
	 * to cover this case.
	 * Either way, the timer is needed for verbs layer related
	 * processing.
	 */
	if (dd->ipath_flags & IPATH_GPIO_INTR) {
		ipath_write_kreg(dd, dd->ipath_kregs->kr_debugportselect,
				 0x2074076542310ULL);
		/* Enable GPIO bit 2 interrupt */
		dd->ipath_gpio_mask |= (u64) (1 << IPATH_GPIO_PORT0_BIT);
		ipath_write_kreg(dd, dd->ipath_kregs->kr_gpio_mask,
				 dd->ipath_gpio_mask);
	}

	init_timer(&dd->verbs_timer);
	dd->verbs_timer.function = __verbs_timer;
	dd->verbs_timer.data = (unsigned long)dd;
	dd->verbs_timer.expires = jiffies + 1;
	add_timer(&dd->verbs_timer);

	return 0;
}

static int disable_timer(struct ipath_devdata *dd)
{
	/* Disable GPIO bit 2 interrupt */
	if (dd->ipath_flags & IPATH_GPIO_INTR) {
                u64 val;
                /* Disable GPIO bit 2 interrupt */
                val = ipath_read_kreg64(dd, dd->ipath_kregs->kr_gpio_mask);
		dd->ipath_gpio_mask &= ~((u64) (1 << IPATH_GPIO_PORT0_BIT));
		ipath_write_kreg(dd, dd->ipath_kregs->kr_gpio_mask,
				 dd->ipath_gpio_mask);
		/*
		 * We might want to undo changes to debugportselect,
		 * but how?
		 */
	}

	del_timer_sync(&dd->verbs_timer);

	return 0;
}

/**
 * ipath_register_ib_device - register our device with the infiniband core
 * @dd: the device data structure
 * Return the allocated ipath_ibdev pointer or NULL on error.
 */
int ipath_register_ib_device(struct ipath_devdata *dd)
{
	struct ipath_verbs_counters cntrs;
	struct ipath_ibdev *idev;
	struct ib_device *dev;
	int ret;

	idev = (struct ipath_ibdev *)ib_alloc_device(sizeof *idev);
	if (idev == NULL) {
		ret = -ENOMEM;
		goto bail;
	}

	dev = &idev->ibdev;

	/* Only need to initialize non-zero fields. */
	spin_lock_init(&idev->n_pds_lock);
	spin_lock_init(&idev->n_ahs_lock);
	spin_lock_init(&idev->n_cqs_lock);
	spin_lock_init(&idev->n_qps_lock);
	spin_lock_init(&idev->n_srqs_lock);
	spin_lock_init(&idev->n_mcast_grps_lock);

	spin_lock_init(&idev->qp_table.lock);
	spin_lock_init(&idev->lk_table.lock);
	idev->sm_lid = __constant_be16_to_cpu(IB_LID_PERMISSIVE);
	/* Set the prefix to the default value (see ch. 4.1.1) */
	idev->gid_prefix = __constant_cpu_to_be64(0xfe80000000000000ULL);

	ret = ipath_init_qp_table(idev, ib_ipath_qp_table_size);
	if (ret)
		goto err_qp;

	/*
	 * The top ib_ipath_lkey_table_size bits are used to index the
	 * table.  The lower 8 bits can be owned by the user (copied from
	 * the LKEY).  The remaining bits act as a generation number or tag.
	 */
	idev->lk_table.max = 1 << ib_ipath_lkey_table_size;
	idev->lk_table.table = kzalloc(idev->lk_table.max *
				       sizeof(*idev->lk_table.table),
				       GFP_KERNEL);
	if (idev->lk_table.table == NULL) {
		ret = -ENOMEM;
		goto err_lk;
	}
	INIT_LIST_HEAD(&idev->pending_mmaps);
	spin_lock_init(&idev->pending_lock);
	idev->mmap_offset = PAGE_SIZE;
	spin_lock_init(&idev->mmap_offset_lock);
	INIT_LIST_HEAD(&idev->pending[0]);
	INIT_LIST_HEAD(&idev->pending[1]);
	INIT_LIST_HEAD(&idev->pending[2]);
	INIT_LIST_HEAD(&idev->piowait);
	INIT_LIST_HEAD(&idev->rnrwait);
	idev->pending_index = 0;
	idev->port_cap_flags =
		IB_PORT_SYS_IMAGE_GUID_SUP | IB_PORT_CLIENT_REG_SUP;
	idev->pma_counter_select[0] = IB_PMA_PORT_XMIT_DATA;
	idev->pma_counter_select[1] = IB_PMA_PORT_RCV_DATA;
	idev->pma_counter_select[2] = IB_PMA_PORT_XMIT_PKTS;
	idev->pma_counter_select[3] = IB_PMA_PORT_RCV_PKTS;
	idev->pma_counter_select[4] = IB_PMA_PORT_XMIT_WAIT;
	idev->link_width_enabled = 3;	/* 1x or 4x */

	/* Snapshot current HW counters to "clear" them. */
	ipath_get_counters(dd, &cntrs);
	idev->z_symbol_error_counter = cntrs.symbol_error_counter;
	idev->z_link_error_recovery_counter =
		cntrs.link_error_recovery_counter;
	idev->z_link_downed_counter = cntrs.link_downed_counter;
	idev->z_port_rcv_errors = cntrs.port_rcv_errors;
	idev->z_port_rcv_remphys_errors =
		cntrs.port_rcv_remphys_errors;
	idev->z_port_xmit_discards = cntrs.port_xmit_discards;
	idev->z_port_xmit_data = cntrs.port_xmit_data;
	idev->z_port_rcv_data = cntrs.port_rcv_data;
	idev->z_port_xmit_packets = cntrs.port_xmit_packets;
	idev->z_port_rcv_packets = cntrs.port_rcv_packets;
	idev->z_local_link_integrity_errors =
		cntrs.local_link_integrity_errors;
	idev->z_excessive_buffer_overrun_errors =
		cntrs.excessive_buffer_overrun_errors;

	/*
	 * The system image GUID is supposed to be the same for all
	 * IB HCAs in a single system but since there can be other
	 * device types in the system, we can't be sure this is unique.
	 */
	if (!sys_image_guid)
		sys_image_guid = dd->ipath_guid;
	idev->sys_image_guid = sys_image_guid;
	idev->ib_unit = dd->ipath_unit;
	idev->dd = dd;

	strlcpy(dev->name, "ipath%d", IB_DEVICE_NAME_MAX);
	dev->owner = THIS_MODULE;
	dev->node_guid = dd->ipath_guid;
	dev->uverbs_abi_ver = IPATH_UVERBS_ABI_VERSION;
	dev->uverbs_cmd_mask =
		(1ull << IB_USER_VERBS_CMD_GET_CONTEXT)		|
		(1ull << IB_USER_VERBS_CMD_QUERY_DEVICE)	|
		(1ull << IB_USER_VERBS_CMD_QUERY_PORT)		|
		(1ull << IB_USER_VERBS_CMD_ALLOC_PD)		|
		(1ull << IB_USER_VERBS_CMD_DEALLOC_PD)		|
		(1ull << IB_USER_VERBS_CMD_CREATE_AH)		|
		(1ull << IB_USER_VERBS_CMD_DESTROY_AH)		|
		(1ull << IB_USER_VERBS_CMD_QUERY_AH)		|
		(1ull << IB_USER_VERBS_CMD_REG_MR)		|
		(1ull << IB_USER_VERBS_CMD_DEREG_MR)		|
		(1ull << IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL) |
		(1ull << IB_USER_VERBS_CMD_CREATE_CQ)		|
		(1ull << IB_USER_VERBS_CMD_RESIZE_CQ)		|
		(1ull << IB_USER_VERBS_CMD_DESTROY_CQ)		|
		(1ull << IB_USER_VERBS_CMD_POLL_CQ)		|
		(1ull << IB_USER_VERBS_CMD_REQ_NOTIFY_CQ)	|
		(1ull << IB_USER_VERBS_CMD_CREATE_QP)		|
		(1ull << IB_USER_VERBS_CMD_QUERY_QP)		|
		(1ull << IB_USER_VERBS_CMD_MODIFY_QP)		|
		(1ull << IB_USER_VERBS_CMD_DESTROY_QP)		|
		(1ull << IB_USER_VERBS_CMD_POST_SEND)		|
		(1ull << IB_USER_VERBS_CMD_POST_RECV)		|
		(1ull << IB_USER_VERBS_CMD_ATTACH_MCAST)	|
		(1ull << IB_USER_VERBS_CMD_DETACH_MCAST)	|
		(1ull << IB_USER_VERBS_CMD_CREATE_SRQ)		|
		(1ull << IB_USER_VERBS_CMD_MODIFY_SRQ)		|
		(1ull << IB_USER_VERBS_CMD_QUERY_SRQ)		|
		(1ull << IB_USER_VERBS_CMD_DESTROY_SRQ)		|
		(1ull << IB_USER_VERBS_CMD_POST_SRQ_RECV);
	dev->node_type = RDMA_NODE_IB_CA;
	dev->phys_port_cnt = 1;
	dev->num_comp_vectors = 1;
	dev->dma_device = &dd->pcidev->dev;
	dev->query_device = ipath_query_device;
	dev->modify_device = ipath_modify_device;
	dev->query_port = ipath_query_port;
	dev->modify_port = ipath_modify_port;
	dev->query_pkey = ipath_query_pkey;
	dev->query_gid = ipath_query_gid;
	dev->alloc_ucontext = ipath_alloc_ucontext;
	dev->dealloc_ucontext = ipath_dealloc_ucontext;
	dev->alloc_pd = ipath_alloc_pd;
	dev->dealloc_pd = ipath_dealloc_pd;
	dev->create_ah = ipath_create_ah;
	dev->destroy_ah = ipath_destroy_ah;
	dev->query_ah = ipath_query_ah;
	dev->create_srq = ipath_create_srq;
	dev->modify_srq = ipath_modify_srq;
	dev->query_srq = ipath_query_srq;
	dev->destroy_srq = ipath_destroy_srq;
	dev->create_qp = ipath_create_qp;
	dev->modify_qp = ipath_modify_qp;
	dev->query_qp = ipath_query_qp;
	dev->destroy_qp = ipath_destroy_qp;
	dev->post_send = ipath_post_send;
	dev->post_recv = ipath_post_receive;
	dev->post_srq_recv = ipath_post_srq_receive;
	dev->create_cq = ipath_create_cq;
	dev->destroy_cq = ipath_destroy_cq;
	dev->resize_cq = ipath_resize_cq;
	dev->poll_cq = ipath_poll_cq;
	dev->req_notify_cq = ipath_req_notify_cq;
	dev->get_dma_mr = ipath_get_dma_mr;
	dev->reg_phys_mr = ipath_reg_phys_mr;
	dev->reg_user_mr = ipath_reg_user_mr;
	dev->dereg_mr = ipath_dereg_mr;
	dev->alloc_fmr = ipath_alloc_fmr;
	dev->map_phys_fmr = ipath_map_phys_fmr;
	dev->unmap_fmr = ipath_unmap_fmr;
	dev->dealloc_fmr = ipath_dealloc_fmr;
	dev->attach_mcast = ipath_multicast_attach;
	dev->detach_mcast = ipath_multicast_detach;
	dev->process_mad = ipath_process_mad;
	dev->mmap = ipath_mmap;
	dev->dma_ops = &ipath_dma_mapping_ops;

	snprintf(dev->node_desc, sizeof(dev->node_desc),
		 IPATH_IDSTR " %s", init_utsname()->nodename);

	ret = ib_register_device(dev);
	if (ret)
		goto err_reg;

	if (ipath_verbs_register_sysfs(dev))
		goto err_class;

	enable_timer(dd);

	goto bail;

err_class:
	ib_unregister_device(dev);
err_reg:
	kfree(idev->lk_table.table);
err_lk:
	kfree(idev->qp_table.table);
err_qp:
	ib_dealloc_device(dev);
	ipath_dev_err(dd, "cannot register verbs: %d!\n", -ret);
	idev = NULL;

bail:
	dd->verbs_dev = idev;
	return ret;
}

void ipath_unregister_ib_device(struct ipath_ibdev *dev)
{
	struct ib_device *ibdev = &dev->ibdev;

	disable_timer(dev->dd);

	ib_unregister_device(ibdev);

	if (!list_empty(&dev->pending[0]) ||
	    !list_empty(&dev->pending[1]) ||
	    !list_empty(&dev->pending[2]))
		ipath_dev_err(dev->dd, "pending list not empty!\n");
	if (!list_empty(&dev->piowait))
		ipath_dev_err(dev->dd, "piowait list not empty!\n");
	if (!list_empty(&dev->rnrwait))
		ipath_dev_err(dev->dd, "rnrwait list not empty!\n");
	if (!ipath_mcast_tree_empty())
		ipath_dev_err(dev->dd, "multicast table memory leak!\n");
	/*
	 * Note that ipath_unregister_ib_device() can be called before all
	 * the QPs are destroyed!
	 */
	ipath_free_all_qps(&dev->qp_table);
	kfree(dev->qp_table.table);
	kfree(dev->lk_table.table);
	ib_dealloc_device(ibdev);
}

static ssize_t show_rev(struct class_device *cdev, char *buf)
{
	struct ipath_ibdev *dev =
		container_of(cdev, struct ipath_ibdev, ibdev.class_dev);

	return sprintf(buf, "%x\n", dev->dd->ipath_pcirev);
}

static ssize_t show_hca(struct class_device *cdev, char *buf)
{
	struct ipath_ibdev *dev =
		container_of(cdev, struct ipath_ibdev, ibdev.class_dev);
	int ret;

	ret = dev->dd->ipath_f_get_boardname(dev->dd, buf, 128);
	if (ret < 0)
		goto bail;
	strcat(buf, "\n");
	ret = strlen(buf);

bail:
	return ret;
}

static ssize_t show_stats(struct class_device *cdev, char *buf)
{
	struct ipath_ibdev *dev =
		container_of(cdev, struct ipath_ibdev, ibdev.class_dev);
	int i;
	int len;

	len = sprintf(buf,
		      "RC resends  %d\n"
		      "RC no QACK  %d\n"
		      "RC ACKs     %d\n"
		      "RC SEQ NAKs %d\n"
		      "RC RDMA seq %d\n"
		      "RC RNR NAKs %d\n"
		      "RC OTH NAKs %d\n"
		      "RC timeouts %d\n"
		      "RC RDMA dup %d\n"
		      "RC stalls   %d\n"
		      "piobuf wait %d\n"
		      "no piobuf   %d\n"
		      "PKT drops   %d\n"
		      "WQE errs    %d\n",
		      dev->n_rc_resends, dev->n_rc_qacks, dev->n_rc_acks,
		      dev->n_seq_naks, dev->n_rdma_seq, dev->n_rnr_naks,
		      dev->n_other_naks, dev->n_timeouts,
		      dev->n_rdma_dup_busy, dev->n_rc_stalls, dev->n_piowait,
		      dev->n_no_piobuf, dev->n_pkt_drops, dev->n_wqe_errs);
	for (i = 0; i < ARRAY_SIZE(dev->opstats); i++) {
		const struct ipath_opcode_stats *si = &dev->opstats[i];

		if (!si->n_packets && !si->n_bytes)
			continue;
		len += sprintf(buf + len, "%02x %llu/%llu\n", i,
			       (unsigned long long) si->n_packets,
			       (unsigned long long) si->n_bytes);
	}
	return len;
}

static CLASS_DEVICE_ATTR(hw_rev, S_IRUGO, show_rev, NULL);
static CLASS_DEVICE_ATTR(hca_type, S_IRUGO, show_hca, NULL);
static CLASS_DEVICE_ATTR(board_id, S_IRUGO, show_hca, NULL);
static CLASS_DEVICE_ATTR(stats, S_IRUGO, show_stats, NULL);

static struct class_device_attribute *ipath_class_attributes[] = {
	&class_device_attr_hw_rev,
	&class_device_attr_hca_type,
	&class_device_attr_board_id,
	&class_device_attr_stats
};

static int ipath_verbs_register_sysfs(struct ib_device *dev)
{
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(ipath_class_attributes); ++i)
		if (class_device_create_file(&dev->class_dev,
					     ipath_class_attributes[i])) {
			ret = 1;
			goto bail;
		}

	ret = 0;

bail:
	return ret;
}
