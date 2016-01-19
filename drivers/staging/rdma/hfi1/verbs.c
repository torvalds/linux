/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
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
 * Copyright(c) 2015 Intel Corporation.
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
#include <linux/random.h>
#include <linux/vmalloc.h>

#include "hfi.h"
#include "common.h"
#include "device.h"
#include "trace.h"
#include "qp.h"
#include "sdma.h"

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

unsigned int hfi1_max_cqes = 0x2FFFF;
module_param_named(max_cqes, hfi1_max_cqes, uint, S_IRUGO);
MODULE_PARM_DESC(max_cqes,
		 "Maximum number of completion queue entries to support");

unsigned int hfi1_max_cqs = 0x1FFFF;
module_param_named(max_cqs, hfi1_max_cqs, uint, S_IRUGO);
MODULE_PARM_DESC(max_cqs, "Maximum number of completion queues to support");

unsigned int hfi1_max_qp_wrs = 0x3FFF;
module_param_named(max_qp_wrs, hfi1_max_qp_wrs, uint, S_IRUGO);
MODULE_PARM_DESC(max_qp_wrs, "Maximum number of QP WRs to support");

unsigned int hfi1_max_qps = 16384;
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

static void verbs_sdma_complete(
	struct sdma_txreq *cookie,
	int status,
	int drained);

/* Length of buffer to create verbs txreq cache name */
#define TXREQ_NAME_LEN 24

/*
 * Translate ib_wr_opcode into ib_wc_opcode.
 */
const enum ib_wc_opcode ib_hfi1_wc_opcode[] = {
	[IB_WR_RDMA_WRITE] = IB_WC_RDMA_WRITE,
	[IB_WR_RDMA_WRITE_WITH_IMM] = IB_WC_RDMA_WRITE,
	[IB_WR_SEND] = IB_WC_SEND,
	[IB_WR_SEND_WITH_IMM] = IB_WC_SEND,
	[IB_WR_RDMA_READ] = IB_WC_RDMA_READ,
	[IB_WR_ATOMIC_CMP_AND_SWP] = IB_WC_COMP_SWAP,
	[IB_WR_ATOMIC_FETCH_AND_ADD] = IB_WC_FETCH_ADD
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
	[IB_OPCODE_RC_ATOMIC_ACKNOWLEDGE]             = 12 + 8 + 4,
	[IB_OPCODE_RC_COMPARE_SWAP]                   = 12 + 8 + 28,
	[IB_OPCODE_RC_FETCH_ADD]                      = 12 + 8 + 28,
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

/*
 * System image GUID.
 */
__be64 ib_hfi1_sys_image_guid;

/**
 * hfi1_copy_sge - copy data to SGE memory
 * @ss: the SGE state
 * @data: the data to copy
 * @length: the length of the data
 */
void hfi1_copy_sge(
	struct rvt_sge_state *ss,
	void *data, u32 length,
	int release)
{
	struct rvt_sge *sge = &ss->sge;

	while (length) {
		u32 len = sge->length;

		if (len > length)
			len = length;
		if (len > sge->sge_length)
			len = sge->sge_length;
		WARN_ON_ONCE(len == 0);
		memcpy(sge->vaddr, data, len);
		sge->vaddr += len;
		sge->length -= len;
		sge->sge_length -= len;
		if (sge->sge_length == 0) {
			if (release)
				rvt_put_mr(sge->mr);
			if (--ss->num_sge)
				*sge = *ss->sg_list++;
		} else if (sge->length == 0 && sge->mr->lkey) {
			if (++sge->n >= RVT_SEGSZ) {
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
 * hfi1_skip_sge - skip over SGE memory
 * @ss: the SGE state
 * @length: the number of bytes to skip
 */
void hfi1_skip_sge(struct rvt_sge_state *ss, u32 length, int release)
{
	struct rvt_sge *sge = &ss->sge;

	while (length) {
		u32 len = sge->length;

		if (len > length)
			len = length;
		if (len > sge->sge_length)
			len = sge->sge_length;
		WARN_ON_ONCE(len == 0);
		sge->vaddr += len;
		sge->length -= len;
		sge->sge_length -= len;
		if (sge->sge_length == 0) {
			if (release)
				rvt_put_mr(sge->mr);
			if (--ss->num_sge)
				*sge = *ss->sg_list++;
		} else if (sge->length == 0 && sge->mr->lkey) {
			if (++sge->n >= RVT_SEGSZ) {
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
 * Make sure the QP is ready and able to accept the given opcode.
 */
static inline int qp_ok(int opcode, struct hfi1_packet *packet)
{
	struct hfi1_ibport *ibp;

	if (!(ib_rvt_state_ops[packet->qp->state] & RVT_PROCESS_RECV_OK))
		goto dropit;
	if (((opcode & OPCODE_QP_MASK) == packet->qp->allowed_ops) ||
	    (opcode == IB_OPCODE_CNP))
		return 1;
dropit:
	ibp = &packet->rcd->ppd->ibport_data;
	ibp->rvp.n_pkt_drops++;
	return 0;
}


/**
 * hfi1_ib_rcv - process an incoming packet
 * @packet: data packet information
 *
 * This is called to process an incoming packet at interrupt level.
 *
 * Tlen is the length of the header + data + CRC in bytes.
 */
void hfi1_ib_rcv(struct hfi1_packet *packet)
{
	struct hfi1_ctxtdata *rcd = packet->rcd;
	struct hfi1_ib_header *hdr = packet->hdr;
	u32 tlen = packet->tlen;
	struct hfi1_pportdata *ppd = rcd->ppd;
	struct hfi1_ibport *ibp = &ppd->ibport_data;
	struct rvt_dev_info *rdi = &ppd->dd->verbs_dev.rdi;
	unsigned long flags;
	u32 qp_num;
	int lnh;
	u8 opcode;
	u16 lid;

	/* Check for GRH */
	lnh = be16_to_cpu(hdr->lrh[0]) & 3;
	if (lnh == HFI1_LRH_BTH)
		packet->ohdr = &hdr->u.oth;
	else if (lnh == HFI1_LRH_GRH) {
		u32 vtf;

		packet->ohdr = &hdr->u.l.oth;
		if (hdr->u.l.grh.next_hdr != IB_GRH_NEXT_HDR)
			goto drop;
		vtf = be32_to_cpu(hdr->u.l.grh.version_tclass_flow);
		if ((vtf >> IB_GRH_VERSION_SHIFT) != IB_GRH_VERSION)
			goto drop;
		packet->rcv_flags |= HFI1_HAS_GRH;
	} else
		goto drop;

	trace_input_ibhdr(rcd->dd, hdr);

	opcode = (be32_to_cpu(packet->ohdr->bth[0]) >> 24);
	inc_opstats(tlen, &rcd->opstats->stats[opcode]);

	/* Get the destination QP number. */
	qp_num = be32_to_cpu(packet->ohdr->bth[1]) & RVT_QPN_MASK;
	lid = be16_to_cpu(hdr->lrh[1]);
	if (unlikely((lid >= be16_to_cpu(IB_MULTICAST_LID_BASE)) &&
		     (lid != be16_to_cpu(IB_LID_PERMISSIVE)))) {
		struct rvt_mcast *mcast;
		struct rvt_mcast_qp *p;

		if (lnh != HFI1_LRH_GRH)
			goto drop;
		mcast = rvt_mcast_find(&ibp->rvp, &hdr->u.l.grh.dgid);
		if (mcast == NULL)
			goto drop;
		list_for_each_entry_rcu(p, &mcast->qp_list, list) {
			packet->qp = p->qp;
			spin_lock_irqsave(&packet->qp->r_lock, flags);
			if (likely((qp_ok(opcode, packet))))
				opcode_handler_tbl[opcode](packet);
			spin_unlock_irqrestore(&packet->qp->r_lock, flags);
		}
		/*
		 * Notify rvt_multicast_detach() if it is waiting for us
		 * to finish.
		 */
		if (atomic_dec_return(&mcast->refcount) <= 1)
			wake_up(&mcast->wait);
	} else {
		rcu_read_lock();
		packet->qp = rvt_lookup_qpn(rdi, &ibp->rvp, qp_num);
		if (!packet->qp) {
			rcu_read_unlock();
			goto drop;
		}
		spin_lock_irqsave(&packet->qp->r_lock, flags);
		if (likely((qp_ok(opcode, packet))))
			opcode_handler_tbl[opcode](packet);
		spin_unlock_irqrestore(&packet->qp->r_lock, flags);
		rcu_read_unlock();
	}
	return;

drop:
	ibp->rvp.n_pkt_drops++;
}

/*
 * This is called from a timer to check for QPs
 * which need kernel memory in order to send a packet.
 */
static void mem_timer(unsigned long data)
{
	struct hfi1_ibdev *dev = (struct hfi1_ibdev *)data;
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
		/* refcount held until actual wake up */
		if (!list_empty(list))
			mod_timer(&dev->mem_timer, jiffies + 1);
	}
	write_sequnlock_irqrestore(&dev->iowait_lock, flags);

	if (qp)
		hfi1_qp_wakeup(qp, RVT_S_WAIT_KMEM);
}

void update_sge(struct rvt_sge_state *ss, u32 length)
{
	struct rvt_sge *sge = &ss->sge;

	sge->vaddr += length;
	sge->length -= length;
	sge->sge_length -= length;
	if (sge->sge_length == 0) {
		if (--ss->num_sge)
			*sge = *ss->sg_list++;
	} else if (sge->length == 0 && sge->mr->lkey) {
		if (++sge->n >= RVT_SEGSZ) {
			if (++sge->m >= sge->mr->mapsz)
				return;
			sge->n = 0;
		}
		sge->vaddr = sge->mr->map[sge->m]->segs[sge->n].vaddr;
		sge->length = sge->mr->map[sge->m]->segs[sge->n].length;
	}
}

static noinline struct verbs_txreq *__get_txreq(struct hfi1_ibdev *dev,
						struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct verbs_txreq *tx;
	unsigned long flags;

	tx = kmem_cache_alloc(dev->verbs_txreq_cache, GFP_ATOMIC);
	if (!tx) {
		spin_lock_irqsave(&qp->s_lock, flags);
		write_seqlock(&dev->iowait_lock);
		if (ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK &&
		    list_empty(&priv->s_iowait.list)) {
			dev->n_txwait++;
			qp->s_flags |= RVT_S_WAIT_TX;
			list_add_tail(&priv->s_iowait.list, &dev->txwait);
			trace_hfi1_qpsleep(qp, RVT_S_WAIT_TX);
			atomic_inc(&qp->refcount);
		}
		qp->s_flags &= ~RVT_S_BUSY;
		write_sequnlock(&dev->iowait_lock);
		spin_unlock_irqrestore(&qp->s_lock, flags);
		tx = ERR_PTR(-EBUSY);
	}
	return tx;
}

static inline struct verbs_txreq *get_txreq(struct hfi1_ibdev *dev,
					    struct rvt_qp *qp)
{
	struct verbs_txreq *tx;

	tx = kmem_cache_alloc(dev->verbs_txreq_cache, GFP_ATOMIC);
	if (!tx) {
		/* call slow path to get the lock */
		tx =  __get_txreq(dev, qp);
		if (IS_ERR(tx))
			return tx;
	}
	tx->qp = qp;
	return tx;
}

void hfi1_put_txreq(struct verbs_txreq *tx)
{
	struct hfi1_ibdev *dev;
	struct rvt_qp *qp;
	unsigned long flags;
	unsigned int seq;
	struct hfi1_qp_priv *priv;

	qp = tx->qp;
	dev = to_idev(qp->ibqp.device);

	if (tx->mr) {
		rvt_put_mr(tx->mr);
		tx->mr = NULL;
	}
	sdma_txclean(dd_from_dev(dev), &tx->txreq);

	/* Free verbs_txreq and return to slab cache */
	kmem_cache_free(dev->verbs_txreq_cache, tx);

	do {
		seq = read_seqbegin(&dev->iowait_lock);
		if (!list_empty(&dev->txwait)) {
			struct iowait *wait;

			write_seqlock_irqsave(&dev->iowait_lock, flags);
			/* Wake up first QP wanting a free struct */
			wait = list_first_entry(&dev->txwait, struct iowait,
						list);
			qp = iowait_to_qp(wait);
			priv = qp->priv;
			list_del_init(&priv->s_iowait.list);
			/* refcount held until actual wake up */
			write_sequnlock_irqrestore(&dev->iowait_lock, flags);
			hfi1_qp_wakeup(qp, RVT_S_WAIT_TX);
			break;
		}
	} while (read_seqretry(&dev->iowait_lock, seq));
}

/*
 * This is called with progress side lock held.
 */
/* New API */
static void verbs_sdma_complete(
	struct sdma_txreq *cookie,
	int status,
	int drained)
{
	struct verbs_txreq *tx =
		container_of(cookie, struct verbs_txreq, txreq);
	struct rvt_qp *qp = tx->qp;

	spin_lock(&qp->s_lock);
	if (tx->wqe)
		hfi1_send_complete(qp, tx->wqe, IB_WC_SUCCESS);
	else if (qp->ibqp.qp_type == IB_QPT_RC) {
		struct hfi1_ib_header *hdr;

		hdr = &tx->phdr.hdr;
		hfi1_rc_send_complete(qp, hdr);
	}
	if (drained) {
		/*
		 * This happens when the send engine notes
		 * a QP in the error state and cannot
		 * do the flush work until that QP's
		 * sdma work has finished.
		 */
		if (qp->s_flags & RVT_S_WAIT_DMA) {
			qp->s_flags &= ~RVT_S_WAIT_DMA;
			hfi1_schedule_send(qp);
		}
	}
	spin_unlock(&qp->s_lock);

	hfi1_put_txreq(tx);
}

static int wait_kmem(struct hfi1_ibdev *dev, struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&qp->s_lock, flags);
	if (ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK) {
		write_seqlock(&dev->iowait_lock);
		if (list_empty(&priv->s_iowait.list)) {
			if (list_empty(&dev->memwait))
				mod_timer(&dev->mem_timer, jiffies + 1);
			qp->s_flags |= RVT_S_WAIT_KMEM;
			list_add_tail(&priv->s_iowait.list, &dev->memwait);
			trace_hfi1_qpsleep(qp, RVT_S_WAIT_KMEM);
			atomic_inc(&qp->refcount);
		}
		write_sequnlock(&dev->iowait_lock);
		qp->s_flags &= ~RVT_S_BUSY;
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
static int build_verbs_ulp_payload(
	struct sdma_engine *sde,
	struct rvt_sge_state *ss,
	u32 length,
	struct verbs_txreq *tx)
{
	struct rvt_sge *sg_list = ss->sg_list;
	struct rvt_sge sge = ss->sge;
	u8 num_sge = ss->num_sge;
	u32 len;
	int ret = 0;

	while (length) {
		len = ss->sge.length;
		if (len > length)
			len = length;
		if (len > ss->sge.sge_length)
			len = ss->sge.sge_length;
		WARN_ON_ONCE(len == 0);
		ret = sdma_txadd_kvaddr(
			sde->dd,
			&tx->txreq,
			ss->sge.vaddr,
			len);
		if (ret)
			goto bail_txadd;
		update_sge(ss, len);
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

/*
 * Build the number of DMA descriptors needed to send length bytes of data.
 *
 * NOTE: DMA mapping is held in the tx until completed in the ring or
 *       the tx desc is freed without having been submitted to the ring
 *
 * This routine insures the following all the helper routine
 * calls succeed.
 */
/* New API */
static int build_verbs_tx_desc(
	struct sdma_engine *sde,
	struct rvt_sge_state *ss,
	u32 length,
	struct verbs_txreq *tx,
	struct ahg_ib_header *ahdr,
	u64 pbc)
{
	int ret = 0;
	struct hfi1_pio_header *phdr;
	u16 hdrbytes = tx->hdr_dwords << 2;

	phdr = &tx->phdr;
	if (!ahdr->ahgcount) {
		ret = sdma_txinit_ahg(
			&tx->txreq,
			ahdr->tx_flags,
			hdrbytes + length,
			ahdr->ahgidx,
			0,
			NULL,
			0,
			verbs_sdma_complete);
		if (ret)
			goto bail_txadd;
		phdr->pbc = cpu_to_le64(pbc);
		memcpy(&phdr->hdr, &ahdr->ibh, hdrbytes - sizeof(phdr->pbc));
		/* add the header */
		ret = sdma_txadd_kvaddr(
			sde->dd,
			&tx->txreq,
			&tx->phdr,
			tx->hdr_dwords << 2);
		if (ret)
			goto bail_txadd;
	} else {
		struct hfi1_other_headers *sohdr = &ahdr->ibh.u.oth;
		struct hfi1_other_headers *dohdr = &phdr->hdr.u.oth;

		/* needed in rc_send_complete() */
		phdr->hdr.lrh[0] = ahdr->ibh.lrh[0];
		if ((be16_to_cpu(phdr->hdr.lrh[0]) & 3) == HFI1_LRH_GRH) {
			sohdr = &ahdr->ibh.u.l.oth;
			dohdr = &phdr->hdr.u.l.oth;
		}
		/* opcode */
		dohdr->bth[0] = sohdr->bth[0];
		/* PSN/ACK  */
		dohdr->bth[2] = sohdr->bth[2];
		ret = sdma_txinit_ahg(
			&tx->txreq,
			ahdr->tx_flags,
			length,
			ahdr->ahgidx,
			ahdr->ahgcount,
			ahdr->ahgdesc,
			hdrbytes,
			verbs_sdma_complete);
		if (ret)
			goto bail_txadd;
	}

	/* add the ulp payload - if any.  ss can be NULL for acks */
	if (ss)
		ret = build_verbs_ulp_payload(sde, ss, length, tx);
bail_txadd:
	return ret;
}

int hfi1_verbs_send_dma(struct rvt_qp *qp, struct hfi1_pkt_state *ps,
			u64 pbc)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct ahg_ib_header *ahdr = priv->s_hdr;
	u32 hdrwords = qp->s_hdrwords;
	struct rvt_sge_state *ss = qp->s_cur_sge;
	u32 len = qp->s_cur_size;
	u32 plen = hdrwords + ((len + 3) >> 2) + 2; /* includes pbc */
	struct hfi1_ibdev *dev = ps->dev;
	struct hfi1_pportdata *ppd = ps->ppd;
	struct verbs_txreq *tx;
	struct sdma_txreq *stx;
	u64 pbc_flags = 0;
	u8 sc5 = priv->s_sc;

	int ret;

	if (!list_empty(&priv->s_iowait.tx_head)) {
		stx = list_first_entry(
			&priv->s_iowait.tx_head,
			struct sdma_txreq,
			list);
		list_del_init(&stx->list);
		tx = container_of(stx, struct verbs_txreq, txreq);
		ret = sdma_send_txreq(tx->sde, &priv->s_iowait, stx);
		if (unlikely(ret == -ECOMM))
			goto bail_ecomm;
		return ret;
	}

	tx = get_txreq(dev, qp);
	if (IS_ERR(tx))
		goto bail_tx;

	tx->sde = priv->s_sde;

	if (likely(pbc == 0)) {
		u32 vl = sc_to_vlt(dd_from_ibdev(qp->ibqp.device), sc5);
		/* No vl15 here */
		/* set PBC_DC_INFO bit (aka SC[4]) in pbc_flags */
		pbc_flags |= (!!(sc5 & 0x10)) << PBC_DC_INFO_SHIFT;

		pbc = create_pbc(ppd, pbc_flags, qp->srate_mbps, vl, plen);
	}
	tx->wqe = qp->s_wqe;
	tx->mr = qp->s_rdma_mr;
	if (qp->s_rdma_mr)
		qp->s_rdma_mr = NULL;
	tx->hdr_dwords = hdrwords + 2;
	ret = build_verbs_tx_desc(tx->sde, ss, len, tx, ahdr, pbc);
	if (unlikely(ret))
		goto bail_build;
	trace_output_ibhdr(dd_from_ibdev(qp->ibqp.device), &ahdr->ibh);
	ret =  sdma_send_txreq(tx->sde, &priv->s_iowait, &tx->txreq);
	if (unlikely(ret == -ECOMM))
		goto bail_ecomm;
	return ret;

bail_ecomm:
	/* The current one got "sent" */
	return 0;
bail_build:
	/* kmalloc or mapping fail */
	hfi1_put_txreq(tx);
	return wait_kmem(dev, qp);
bail_tx:
	return PTR_ERR(tx);
}

/*
 * If we are now in the error state, return zero to flush the
 * send work request.
 */
static int no_bufs_available(struct rvt_qp *qp, struct send_context *sc)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct hfi1_devdata *dd = sc->dd;
	struct hfi1_ibdev *dev = &dd->verbs_dev;
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
		write_seqlock(&dev->iowait_lock);
		if (list_empty(&priv->s_iowait.list)) {
			struct hfi1_ibdev *dev = &dd->verbs_dev;
			int was_empty;

			dev->n_piowait++;
			qp->s_flags |= RVT_S_WAIT_PIO;
			was_empty = list_empty(&sc->piowait);
			list_add_tail(&priv->s_iowait.list, &sc->piowait);
			trace_hfi1_qpsleep(qp, RVT_S_WAIT_PIO);
			atomic_inc(&qp->refcount);
			/* counting: only call wantpiobuf_intr if first user */
			if (was_empty)
				hfi1_sc_wantpiobuf_intr(sc, 1);
		}
		write_sequnlock(&dev->iowait_lock);
		qp->s_flags &= ~RVT_S_BUSY;
		ret = -EBUSY;
	}
	spin_unlock_irqrestore(&qp->s_lock, flags);
	return ret;
}

struct send_context *qp_to_send_context(struct rvt_qp *qp, u8 sc5)
{
	struct hfi1_devdata *dd = dd_from_ibdev(qp->ibqp.device);
	struct hfi1_pportdata *ppd = dd->pport + (qp->port_num - 1);
	u8 vl;

	vl = sc_to_vlt(dd, sc5);
	if (vl >= ppd->vls_supported && vl != 15)
		return NULL;
	return dd->vld[vl].sc;
}

int hfi1_verbs_send_pio(struct rvt_qp *qp, struct hfi1_pkt_state *ps,
			u64 pbc)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct ahg_ib_header *ahdr = priv->s_hdr;
	u32 hdrwords = qp->s_hdrwords;
	struct rvt_sge_state *ss = qp->s_cur_sge;
	u32 len = qp->s_cur_size;
	u32 dwords = (len + 3) >> 2;
	u32 plen = hdrwords + dwords + 2; /* includes pbc */
	struct hfi1_pportdata *ppd = ps->ppd;
	u32 *hdr = (u32 *)&ahdr->ibh;
	u64 pbc_flags = 0;
	u32 sc5;
	unsigned long flags = 0;
	struct send_context *sc;
	struct pio_buf *pbuf;
	int wc_status = IB_WC_SUCCESS;

	/* vl15 special case taken care of in ud.c */
	sc5 = priv->s_sc;
	sc = qp_to_send_context(qp, sc5);

	if (!sc)
		return -EINVAL;
	if (likely(pbc == 0)) {
		u32 vl = sc_to_vlt(dd_from_ibdev(qp->ibqp.device), sc5);
		/* set PBC_DC_INFO bit (aka SC[4]) in pbc_flags */
		pbc_flags |= (!!(sc5 & 0x10)) << PBC_DC_INFO_SHIFT;
		pbc = create_pbc(ppd, pbc_flags, qp->srate_mbps, vl, plen);
	}
	pbuf = sc_buffer_alloc(sc, plen, NULL, NULL);
	if (unlikely(pbuf == NULL)) {
		if (ppd->host_link_state != HLS_UP_ACTIVE) {
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
			return no_bufs_available(qp, sc);
		}
	}

	if (len == 0) {
		pio_copy(ppd->dd, pbuf, pbc, hdr, hdrwords);
	} else {
		if (ss) {
			seg_pio_copy_start(pbuf, pbc, hdr, hdrwords*4);
			while (len) {
				void *addr = ss->sge.vaddr;
				u32 slen = ss->sge.length;

				if (slen > len)
					slen = len;
				update_sge(ss, slen);
				seg_pio_copy_mid(pbuf, addr, slen);
				len -= slen;
			}
			seg_pio_copy_end(pbuf);
		}
	}

	trace_output_ibhdr(dd_from_ibdev(qp->ibqp.device), &ahdr->ibh);

	if (qp->s_rdma_mr) {
		rvt_put_mr(qp->s_rdma_mr);
		qp->s_rdma_mr = NULL;
	}

pio_bail:
	if (qp->s_wqe) {
		spin_lock_irqsave(&qp->s_lock, flags);
		hfi1_send_complete(qp, qp->s_wqe, wc_status);
		spin_unlock_irqrestore(&qp->s_lock, flags);
	} else if (qp->ibqp.qp_type == IB_QPT_RC) {
		spin_lock_irqsave(&qp->s_lock, flags);
		hfi1_rc_send_complete(qp, &ahdr->ibh);
		spin_unlock_irqrestore(&qp->s_lock, flags);
	}
	return 0;
}

/*
 * egress_pkey_matches_entry - return 1 if the pkey matches ent (ent
 * being an entry from the ingress partition key table), return 0
 * otherwise. Use the matching criteria for egress partition keys
 * specified in the OPAv1 spec., section 9.1l.7.
 */
static inline int egress_pkey_matches_entry(u16 pkey, u16 ent)
{
	u16 mkey = pkey & PKEY_LOW_15_MASK;
	u16 ment = ent & PKEY_LOW_15_MASK;

	if (mkey == ment) {
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

/*
 * egress_pkey_check - return 0 if hdr's pkey matches according to the
 * criteria in the OPAv1 spec., section 9.11.7.
 */
static inline int egress_pkey_check(struct hfi1_pportdata *ppd,
				    struct hfi1_ib_header *hdr,
				    struct rvt_qp *qp)
{
	struct hfi1_qp_priv *priv = qp->priv;
	struct hfi1_other_headers *ohdr;
	struct hfi1_devdata *dd;
	int i = 0;
	u16 pkey;
	u8 lnh, sc5 = priv->s_sc;

	if (!(ppd->part_enforce & HFI1_PART_ENFORCE_OUT))
		return 0;

	/* locate the pkey within the headers */
	lnh = be16_to_cpu(hdr->lrh[0]) & 3;
	if (lnh == HFI1_LRH_GRH)
		ohdr = &hdr->u.l.oth;
	else
		ohdr = &hdr->u.oth;

	pkey = (u16)be32_to_cpu(ohdr->bth[0]);

	/* If SC15, pkey[0:14] must be 0x7fff */
	if ((sc5 == 0xf) && ((pkey & PKEY_LOW_15_MASK) != PKEY_LOW_15_MASK))
		goto bad;


	/* Is the pkey = 0x0, or 0x8000? */
	if ((pkey & PKEY_LOW_15_MASK) == 0)
		goto bad;

	/* The most likely matching pkey has index qp->s_pkey_index */
	if (unlikely(!egress_pkey_matches_entry(pkey,
					ppd->pkeys[qp->s_pkey_index]))) {
		/* no match - try the entire table */
		for (; i < MAX_PKEY_VALUES; i++) {
			if (egress_pkey_matches_entry(pkey, ppd->pkeys[i]))
				break;
		}
	}

	if (i < MAX_PKEY_VALUES)
		return 0;
bad:
	incr_cntr64(&ppd->port_xmit_constraint_errors);
	dd = ppd->dd;
	if (!(dd->err_info_xmit_constraint.status & OPA_EI_STATUS_SMASK)) {
		u16 slid = be16_to_cpu(hdr->lrh[3]);

		dd->err_info_xmit_constraint.status |= OPA_EI_STATUS_SMASK;
		dd->err_info_xmit_constraint.slid = slid;
		dd->err_info_xmit_constraint.pkey = pkey;
	}
	return 1;
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
	struct ahg_ib_header *ahdr = priv->s_hdr;
	int ret;
	int pio = 0;
	unsigned long flags = 0;

	/*
	 * VL15 packets (IB_QPT_SMI) will always use PIO, so we
	 * can defer SDMA restart until link goes ACTIVE without
	 * worrying about just how we got there.
	 */
	if ((qp->ibqp.qp_type == IB_QPT_SMI) ||
	    !(dd->flags & HFI1_HAS_SEND_DMA))
		pio = 1;

	ret = egress_pkey_check(dd->pport, &ahdr->ibh, qp);
	if (unlikely(ret)) {
		/*
		 * The value we are returning here does not get propagated to
		 * the verbs caller. Thus we need to complete the request with
		 * error otherwise the caller could be sitting waiting on the
		 * completion event. Only do this for PIO. SDMA has its own
		 * mechanism for handling the errors. So for SDMA we can just
		 * return.
		 */
		if (pio) {
			hfi1_cdbg(PIO, "%s() Failed. Completing with err",
				  __func__);
			spin_lock_irqsave(&qp->s_lock, flags);
			hfi1_send_complete(qp, qp->s_wqe, IB_WC_GENERAL_ERR);
			spin_unlock_irqrestore(&qp->s_lock, flags);
		}
		return -EINVAL;
	}

	if (pio) {
		ret = dd->process_pio_send(qp, ps, 0);
	} else {
#ifdef CONFIG_SDMA_VERBOSITY
		dd_dev_err(dd, "CONFIG SDMA %s:%d %s()\n",
			   slashstrip(__FILE__), __LINE__, __func__);
		dd_dev_err(dd, "SDMA hdrwords = %u, len = %u\n", qp->s_hdrwords,
			   qp->s_cur_size);
#endif
		ret = dd->process_dma_send(qp, ps, 0);
	}

	return ret;
}

/**
 * hfi1_fill_device_attr - Fill in rvt dev info device attributes.
 * @dd: the device data structure
 */
static void hfi1_fill_device_attr(struct hfi1_devdata *dd)
{
	struct rvt_dev_info *rdi = &dd->verbs_dev.rdi;

	memset(&rdi->dparms.props, 0, sizeof(rdi->dparms.props));

	rdi->dparms.props.device_cap_flags = IB_DEVICE_BAD_PKEY_CNTR |
			IB_DEVICE_BAD_QKEY_CNTR | IB_DEVICE_SHUTDOWN_PORT |
			IB_DEVICE_SYS_IMAGE_GUID | IB_DEVICE_RC_RNR_NAK_GEN |
			IB_DEVICE_PORT_ACTIVE_EVENT | IB_DEVICE_SRQ_RESIZE;
	rdi->dparms.props.page_size_cap = PAGE_SIZE;
	rdi->dparms.props.vendor_id = dd->oui1 << 16 | dd->oui2 << 8 | dd->oui3;
	rdi->dparms.props.vendor_part_id = dd->pcidev->device;
	rdi->dparms.props.hw_ver = dd->minrev;
	rdi->dparms.props.sys_image_guid = ib_hfi1_sys_image_guid;
	rdi->dparms.props.max_mr_size = ~0ULL;
	rdi->dparms.props.max_qp = hfi1_max_qps;
	rdi->dparms.props.max_qp_wr = hfi1_max_qp_wrs;
	rdi->dparms.props.max_sge = hfi1_max_sges;
	rdi->dparms.props.max_sge_rd = hfi1_max_sges;
	rdi->dparms.props.max_cq = hfi1_max_cqs;
	rdi->dparms.props.max_ah = hfi1_max_ahs;
	rdi->dparms.props.max_cqe = hfi1_max_cqes;
	rdi->dparms.props.max_mr = rdi->lkey_table.max;
	rdi->dparms.props.max_fmr = rdi->lkey_table.max;
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

static int query_port(struct ib_device *ibdev, u8 port,
		      struct ib_port_attr *props)
{
	struct hfi1_devdata *dd = dd_from_ibdev(ibdev);
	struct hfi1_ibport *ibp = to_iport(ibdev, port);
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	u16 lid = ppd->lid;

	memset(props, 0, sizeof(*props));
	props->lid = lid ? lid : 0;
	props->lmc = ppd->lmc;
	props->sm_lid = ibp->rvp.sm_lid;
	props->sm_sl = ibp->rvp.sm_sl;
	/* OPA logical states match IB logical states */
	props->state = driver_lstate(ppd);
	props->phys_state = hfi1_ibphys_portstate(ppd);
	props->port_cap_flags = ibp->rvp.port_cap_flags;
	props->gid_tbl_len = HFI1_GUIDS_PER_PORT;
	props->max_msg_sz = 0x80000000;
	props->pkey_tbl_len = hfi1_get_npkeys(dd);
	props->bad_pkey_cntr = ibp->rvp.pkey_violations;
	props->qkey_viol_cntr = ibp->rvp.qkey_violations;
	props->active_width = (u8)opa_width_to_ib(ppd->link_width_active);
	/* see rate_show() in ib core/sysfs.c */
	props->active_speed = (u8)opa_speed_to_ib(ppd->link_speed_active);
	props->max_vl_num = ppd->vls_supported;
	props->init_type_reply = 0;

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
		mtu_to_enum(ppd->ibmtu, IB_MTU_2048);
	props->subnet_timeout = ibp->rvp.subnet_timeout;

	return 0;
}

static int port_immutable(struct ib_device *ibdev, u8 port_num,
			  struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int err;

	err = query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	memset(immutable, 0, sizeof(*immutable));

	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;
	immutable->core_cap_flags = RDMA_CORE_PORT_INTEL_OPA;
	immutable->max_mad_size = OPA_MGMT_MAD_SIZE;

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
		memcpy(device->node_desc, device_modify->node_desc, 64);
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

static int modify_port(struct ib_device *ibdev, u8 port,
		       int port_modify_mask, struct ib_port_modify *props)
{
	struct hfi1_ibport *ibp = to_iport(ibdev, port);
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	int ret = 0;

	ibp->rvp.port_cap_flags |= props->set_port_cap_mask;
	ibp->rvp.port_cap_flags &= ~props->clr_port_cap_mask;
	if (props->set_port_cap_mask || props->clr_port_cap_mask)
		hfi1_cap_mask_chg(ibp);
	if (port_modify_mask & IB_PORT_SHUTDOWN) {
		set_link_down_reason(ppd, OPA_LINKDOWN_REASON_UNKNOWN, 0,
		  OPA_LINKDOWN_REASON_UNKNOWN);
		ret = set_link_state(ppd, HLS_DN_DOWNDEF);
	}
	if (port_modify_mask & IB_PORT_RESET_QKEY_CNTR)
		ibp->rvp.qkey_violations = 0;
	return ret;
}

static int query_gid(struct ib_device *ibdev, u8 port,
		     int index, union ib_gid *gid)
{
	struct hfi1_devdata *dd = dd_from_ibdev(ibdev);
	int ret = 0;

	if (!port || port > dd->num_pports)
		ret = -EINVAL;
	else {
		struct hfi1_ibport *ibp = to_iport(ibdev, port);
		struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);

		gid->global.subnet_prefix = ibp->rvp.gid_prefix;
		if (index == 0)
			gid->global.interface_id = cpu_to_be64(ppd->guid);
		else if (index < HFI1_GUIDS_PER_PORT)
			gid->global.interface_id = ibp->guids[index - 1];
		else
			ret = -EINVAL;
	}

	return ret;
}

/*
 * convert ah port,sl to sc
 */
u8 ah_to_sc(struct ib_device *ibdev, struct ib_ah_attr *ah)
{
	struct hfi1_ibport *ibp = to_iport(ibdev, ah->port_num);

	return ibp->sl_to_sc[ah->sl];
}

static int hfi1_check_ah(struct ib_device *ibdev, struct ib_ah_attr *ah_attr)
{
	struct hfi1_ibport *ibp;
	struct hfi1_pportdata *ppd;
	struct hfi1_devdata *dd;
	u8 sc5;

	/* test the mapping for validity */
	ibp = to_iport(ibdev, ah_attr->port_num);
	ppd = ppd_from_ibp(ibp);
	sc5 = ibp->sl_to_sc[ah_attr->sl];
	dd = dd_from_ppd(ppd);
	if (sc_to_vlt(dd, sc5) > num_vls && sc_to_vlt(dd, sc5) != 0xf)
		return -EINVAL;
	return 0;
}

static void hfi1_notify_new_ah(struct ib_device *ibdev,
			       struct ib_ah_attr *ah_attr,
			       struct rvt_ah *ah)
{
	struct hfi1_ibport *ibp;
	struct hfi1_pportdata *ppd;
	struct hfi1_devdata *dd;
	u8 sc5;

	/*
	 * Do not trust reading anything from rvt_ah at this point as it is not
	 * done being setup. We can however modify things which we need to set.
	 */

	ibp = to_iport(ibdev, ah_attr->port_num);
	ppd = ppd_from_ibp(ibp);
	sc5 = ibp->sl_to_sc[ah->attr.sl];
	dd = dd_from_ppd(ppd);
	ah->vl = sc_to_vlt(dd, sc5);
	if (ah->vl < num_vls || ah->vl == 15)
		ah->log_pmtu = ilog2(dd->vld[ah->vl].mtu);
}

struct ib_ah *hfi1_create_qp0_ah(struct hfi1_ibport *ibp, u16 dlid)
{
	struct ib_ah_attr attr;
	struct ib_ah *ah = ERR_PTR(-EINVAL);
	struct rvt_qp *qp0;

	memset(&attr, 0, sizeof(attr));
	attr.dlid = dlid;
	attr.port_num = ppd_from_ibp(ibp)->port;
	rcu_read_lock();
	qp0 = rcu_dereference(ibp->rvp.qp[0]);
	if (qp0)
		ah = ib_create_ah(qp0->ibqp.pd, &attr);
	rcu_read_unlock();
	return ah;
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

	spin_lock_init(&ibp->rvp.lock);
	/* Set the prefix to the default value (see ch. 4.1.1) */
	ibp->rvp.gid_prefix = IB_DEFAULT_GID_PREFIX;
	ibp->rvp.sm_lid = 0;
	/* Below should only set bits defined in OPA PortInfo.CapabilityMask */
	ibp->rvp.port_cap_flags = IB_PORT_AUTO_MIGR_SUP |
		IB_PORT_CAP_MASK_NOTICE_SUP;
	ibp->rvp.pma_counter_select[0] = IB_PMA_PORT_XMIT_DATA;
	ibp->rvp.pma_counter_select[1] = IB_PMA_PORT_RCV_DATA;
	ibp->rvp.pma_counter_select[2] = IB_PMA_PORT_XMIT_PKTS;
	ibp->rvp.pma_counter_select[3] = IB_PMA_PORT_RCV_PKTS;
	ibp->rvp.pma_counter_select[4] = IB_PMA_PORT_XMIT_WAIT;

	RCU_INIT_POINTER(ibp->rvp.qp[0], NULL);
	RCU_INIT_POINTER(ibp->rvp.qp[1], NULL);
}

static void verbs_txreq_kmem_cache_ctor(void *obj)
{
	struct verbs_txreq *tx = obj;

	memset(tx, 0, sizeof(*tx));
}

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
	unsigned i;
	int ret;
	size_t lcpysz = IB_DEVICE_NAME_MAX;
	u16 descq_cnt;
	char buf[TXREQ_NAME_LEN];

	for (i = 0; i < dd->num_pports; i++)
		init_ibport(ppd + i);

	/* Only need to initialize non-zero fields. */

	spin_lock_init(&dev->n_srqs_lock);
	init_timer(&dev->mem_timer);
	dev->mem_timer.function = mem_timer;
	dev->mem_timer.data = (unsigned long) dev;

	seqlock_init(&dev->iowait_lock);
	INIT_LIST_HEAD(&dev->txwait);
	INIT_LIST_HEAD(&dev->memwait);

	descq_cnt = sdma_get_descq_cnt();

	snprintf(buf, sizeof(buf), "hfi1_%u_vtxreq_cache", dd->unit);
	/* SLAB_HWCACHE_ALIGN for AHG */
	dev->verbs_txreq_cache = kmem_cache_create(buf,
						   sizeof(struct verbs_txreq),
						   0, SLAB_HWCACHE_ALIGN,
						   verbs_txreq_kmem_cache_ctor);
	if (!dev->verbs_txreq_cache) {
		ret = -ENOMEM;
		goto err_verbs_txreq;
	}

	/*
	 * The system image GUID is supposed to be the same for all
	 * HFIs in a single system but since there can be other
	 * device types in the system, we can't be sure this is unique.
	 */
	if (!ib_hfi1_sys_image_guid)
		ib_hfi1_sys_image_guid = cpu_to_be64(ppd->guid);
	lcpysz = strlcpy(ibdev->name, class_name(), lcpysz);
	strlcpy(ibdev->name + lcpysz, "_%d", IB_DEVICE_NAME_MAX - lcpysz);
	ibdev->owner = THIS_MODULE;
	ibdev->node_guid = cpu_to_be64(ppd->guid);
	ibdev->phys_port_cnt = dd->num_pports;
	ibdev->dma_device = &dd->pcidev->dev;
	ibdev->modify_device = modify_device;
	ibdev->query_port = query_port;
	ibdev->modify_port = modify_port;
	ibdev->query_gid = query_gid;
	ibdev->create_srq = hfi1_create_srq;
	ibdev->modify_srq = hfi1_modify_srq;
	ibdev->query_srq = hfi1_query_srq;
	ibdev->destroy_srq = hfi1_destroy_srq;
	ibdev->query_qp = hfi1_query_qp;
	ibdev->post_srq_recv = hfi1_post_srq_receive;

	/* keep process mad in the driver */
	ibdev->process_mad = hfi1_process_mad;
	ibdev->get_port_immutable = port_immutable;

	strncpy(ibdev->node_desc, init_utsname()->nodename,
		sizeof(ibdev->node_desc));

	/*
	 * Fill in rvt info object.
	 */
	dd->verbs_dev.rdi.driver_f.port_callback = hfi1_create_port_files;
	dd->verbs_dev.rdi.driver_f.get_card_name = get_card_name;
	dd->verbs_dev.rdi.driver_f.get_pci_dev = get_pci_dev;
	dd->verbs_dev.rdi.driver_f.check_ah = hfi1_check_ah;
	dd->verbs_dev.rdi.driver_f.notify_new_ah = hfi1_notify_new_ah;
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
	dd->verbs_dev.rdi.driver_f.qp_priv_alloc = qp_priv_alloc;
	dd->verbs_dev.rdi.driver_f.qp_priv_free = qp_priv_free;
	dd->verbs_dev.rdi.driver_f.free_all_qps = free_all_qps;
	dd->verbs_dev.rdi.driver_f.notify_qp_reset = notify_qp_reset;
	dd->verbs_dev.rdi.driver_f.do_send = hfi1_do_send;
	dd->verbs_dev.rdi.driver_f.schedule_send = hfi1_schedule_send;
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

	/* completeion queue */
	snprintf(dd->verbs_dev.rdi.dparms.cq_name,
		 sizeof(dd->verbs_dev.rdi.dparms.cq_name),
		 "hfi1_cq%d", dd->unit);
	dd->verbs_dev.rdi.dparms.node = dd->assigned_node_id;

	/* misc settings */
	dd->verbs_dev.rdi.flags = 0; /* Let rdmavt handle it all */
	dd->verbs_dev.rdi.dparms.lkey_table_size = hfi1_lkey_table_size;
	dd->verbs_dev.rdi.dparms.nports = dd->num_pports;
	dd->verbs_dev.rdi.dparms.npkeys = hfi1_get_npkeys(dd);

	ppd = dd->pport;
	for (i = 0; i < dd->num_pports; i++, ppd++)
		rvt_init_port(&dd->verbs_dev.rdi,
			      &ppd->ibport_data.rvp,
			      i,
			      ppd->pkeys);

	ret = rvt_register_device(&dd->verbs_dev.rdi);
	if (ret)
		goto err_verbs_txreq;

	ret = hfi1_verbs_register_sysfs(dd);
	if (ret)
		goto err_class;

	return ret;

err_class:
	rvt_unregister_device(&dd->verbs_dev.rdi);
err_verbs_txreq:
	kmem_cache_destroy(dev->verbs_txreq_cache);
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
	kmem_cache_destroy(dev->verbs_txreq_cache);
}

void hfi1_cnp_rcv(struct hfi1_packet *packet)
{
	struct hfi1_ibport *ibp = &packet->rcd->ppd->ibport_data;
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	struct hfi1_ib_header *hdr = packet->hdr;
	struct rvt_qp *qp = packet->qp;
	u32 lqpn, rqpn = 0;
	u16 rlid = 0;
	u8 sl, sc5, sc4_bit, svc_type;
	bool sc4_set = has_sc4_bit(packet);

	switch (packet->qp->ibqp.qp_type) {
	case IB_QPT_UC:
		rlid = qp->remote_ah_attr.dlid;
		rqpn = qp->remote_qpn;
		svc_type = IB_CC_SVCTYPE_UC;
		break;
	case IB_QPT_RC:
		rlid = qp->remote_ah_attr.dlid;
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

	sc4_bit = sc4_set << 4;
	sc5 = (be16_to_cpu(hdr->lrh[0]) >> 12) & 0xf;
	sc5 |= sc4_bit;
	sl = ibp->sc_to_sl[sc5];
	lqpn = qp->ibqp.qp_num;

	process_becn(ppd, sl, rlid, lqpn, rqpn, svc_type);
}
