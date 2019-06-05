/*
 * Copyright (c) 2012 - 2018 Intel Corporation.  All rights reserved.
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
#include <rdma/rdma_vt.h>

#include "qib.h"
#include "qib_common.h"

static unsigned int ib_qib_qp_table_size = 256;
module_param_named(qp_table_size, ib_qib_qp_table_size, uint, S_IRUGO);
MODULE_PARM_DESC(qp_table_size, "QP table size");

static unsigned int qib_lkey_table_size = 16;
module_param_named(lkey_table_size, qib_lkey_table_size, uint,
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

/*
 * Count the number of DMA descriptors needed to send length bytes of data.
 * Don't modify the qib_sge_state to get the count.
 * Return zero if any of the segments is not aligned.
 */
static u32 qib_count_sge(struct rvt_sge_state *ss, u32 length)
{
	struct rvt_sge *sg_list = ss->sg_list;
	struct rvt_sge sge = ss->sge;
	u8 num_sge = ss->num_sge;
	u32 ndesc = 1;  /* count the header */

	while (length) {
		u32 len = rvt_get_sge_length(&sge, length);

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
			if (++sge.n >= RVT_SEGSZ) {
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
static void qib_copy_from_sge(void *data, struct rvt_sge_state *ss, u32 length)
{
	struct rvt_sge *sge = &ss->sge;

	while (length) {
		u32 len = rvt_get_sge_length(sge, length);

		memcpy(data, sge->vaddr, len);
		sge->vaddr += len;
		sge->length -= len;
		sge->sge_length -= len;
		if (sge->sge_length == 0) {
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
static void qib_qp_rcv(struct qib_ctxtdata *rcd, struct ib_header *hdr,
		       int has_grh, void *data, u32 tlen, struct rvt_qp *qp)
{
	struct qib_ibport *ibp = &rcd->ppd->ibport_data;

	spin_lock(&qp->r_lock);

	/* Check for valid receive state. */
	if (!(ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK)) {
		ibp->rvp.n_pkt_drops++;
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
	struct ib_header *hdr = rhdr;
	struct qib_devdata *dd = ppd->dd;
	struct rvt_dev_info *rdi = &dd->verbs_dev.rdi;
	struct ib_other_headers *ohdr;
	struct rvt_qp *qp;
	u32 qp_num;
	int lnh;
	u8 opcode;
	u16 lid;

	/* 24 == LRH+BTH+CRC */
	if (unlikely(tlen < 24))
		goto drop;

	/* Check for a valid destination LID (see ch. 7.11.1). */
	lid = be16_to_cpu(hdr->lrh[1]);
	if (lid < be16_to_cpu(IB_MULTICAST_LID_BASE)) {
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
	qp_num = be32_to_cpu(ohdr->bth[1]) & RVT_QPN_MASK;
	if (qp_num == QIB_MULTICAST_QPN) {
		struct rvt_mcast *mcast;
		struct rvt_mcast_qp *p;

		if (lnh != QIB_LRH_GRH)
			goto drop;
		mcast = rvt_mcast_find(&ibp->rvp, &hdr->u.l.grh.dgid, lid);
		if (mcast == NULL)
			goto drop;
		this_cpu_inc(ibp->pmastats->n_multicast_rcv);
		list_for_each_entry_rcu(p, &mcast->qp_list, list)
			qib_qp_rcv(rcd, hdr, 1, data, tlen, p->qp);
		/*
		 * Notify rvt_multicast_detach() if it is waiting for us
		 * to finish.
		 */
		if (atomic_dec_return(&mcast->refcount) <= 1)
			wake_up(&mcast->wait);
	} else {
		rcu_read_lock();
		qp = rvt_lookup_qpn(rdi, &ibp->rvp, qp_num);
		if (!qp) {
			rcu_read_unlock();
			goto drop;
		}
		this_cpu_inc(ibp->pmastats->n_unicast_rcv);
		qib_qp_rcv(rcd, hdr, lnh == QIB_LRH_GRH, data, tlen, qp);
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
static void mem_timer(struct timer_list *t)
{
	struct qib_ibdev *dev = from_timer(dev, t, mem_timer);
	struct list_head *list = &dev->memwait;
	struct rvt_qp *qp = NULL;
	struct qib_qp_priv *priv = NULL;
	unsigned long flags;

	spin_lock_irqsave(&dev->rdi.pending_lock, flags);
	if (!list_empty(list)) {
		priv = list_entry(list->next, struct qib_qp_priv, iowait);
		qp = priv->owner;
		list_del_init(&priv->iowait);
		rvt_get_qp(qp);
		if (!list_empty(list))
			mod_timer(&dev->mem_timer, jiffies + 1);
	}
	spin_unlock_irqrestore(&dev->rdi.pending_lock, flags);

	if (qp) {
		spin_lock_irqsave(&qp->s_lock, flags);
		if (qp->s_flags & RVT_S_WAIT_KMEM) {
			qp->s_flags &= ~RVT_S_WAIT_KMEM;
			qib_schedule_send(qp);
		}
		spin_unlock_irqrestore(&qp->s_lock, flags);
		rvt_put_qp(qp);
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

static void copy_io(u32 __iomem *piobuf, struct rvt_sge_state *ss,
		    u32 length, unsigned flush_wc)
{
	u32 extra = 0;
	u32 data = 0;
	u32 last;

	while (1) {
		u32 len = rvt_get_sge_length(&ss->sge, length);
		u32 off;

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
		rvt_update_sge(ss, len, false);
		length -= len;
	}
	/* Update address before sending packet. */
	rvt_update_sge(ss, length, false);
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
					   struct rvt_qp *qp)
{
	struct qib_qp_priv *priv = qp->priv;
	struct qib_verbs_txreq *tx;
	unsigned long flags;

	spin_lock_irqsave(&qp->s_lock, flags);
	spin_lock(&dev->rdi.pending_lock);

	if (!list_empty(&dev->txreq_free)) {
		struct list_head *l = dev->txreq_free.next;

		list_del(l);
		spin_unlock(&dev->rdi.pending_lock);
		spin_unlock_irqrestore(&qp->s_lock, flags);
		tx = list_entry(l, struct qib_verbs_txreq, txreq.list);
	} else {
		if (ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK &&
		    list_empty(&priv->iowait)) {
			dev->n_txwait++;
			qp->s_flags |= RVT_S_WAIT_TX;
			list_add_tail(&priv->iowait, &dev->txwait);
		}
		qp->s_flags &= ~RVT_S_BUSY;
		spin_unlock(&dev->rdi.pending_lock);
		spin_unlock_irqrestore(&qp->s_lock, flags);
		tx = ERR_PTR(-EBUSY);
	}
	return tx;
}

static inline struct qib_verbs_txreq *get_txreq(struct qib_ibdev *dev,
					 struct rvt_qp *qp)
{
	struct qib_verbs_txreq *tx;
	unsigned long flags;

	spin_lock_irqsave(&dev->rdi.pending_lock, flags);
	/* assume the list non empty */
	if (likely(!list_empty(&dev->txreq_free))) {
		struct list_head *l = dev->txreq_free.next;

		list_del(l);
		spin_unlock_irqrestore(&dev->rdi.pending_lock, flags);
		tx = list_entry(l, struct qib_verbs_txreq, txreq.list);
	} else {
		/* call slow path to get the extra lock */
		spin_unlock_irqrestore(&dev->rdi.pending_lock, flags);
		tx =  __get_txreq(dev, qp);
	}
	return tx;
}

void qib_put_txreq(struct qib_verbs_txreq *tx)
{
	struct qib_ibdev *dev;
	struct rvt_qp *qp;
	struct qib_qp_priv *priv;
	unsigned long flags;

	qp = tx->qp;
	dev = to_idev(qp->ibqp.device);

	if (tx->mr) {
		rvt_put_mr(tx->mr);
		tx->mr = NULL;
	}
	if (tx->txreq.flags & QIB_SDMA_TXREQ_F_FREEBUF) {
		tx->txreq.flags &= ~QIB_SDMA_TXREQ_F_FREEBUF;
		dma_unmap_single(&dd_from_dev(dev)->pcidev->dev,
				 tx->txreq.addr, tx->hdr_dwords << 2,
				 DMA_TO_DEVICE);
		kfree(tx->align_buf);
	}

	spin_lock_irqsave(&dev->rdi.pending_lock, flags);

	/* Put struct back on free list */
	list_add(&tx->txreq.list, &dev->txreq_free);

	if (!list_empty(&dev->txwait)) {
		/* Wake up first QP wanting a free struct */
		priv = list_entry(dev->txwait.next, struct qib_qp_priv,
				  iowait);
		qp = priv->owner;
		list_del_init(&priv->iowait);
		rvt_get_qp(qp);
		spin_unlock_irqrestore(&dev->rdi.pending_lock, flags);

		spin_lock_irqsave(&qp->s_lock, flags);
		if (qp->s_flags & RVT_S_WAIT_TX) {
			qp->s_flags &= ~RVT_S_WAIT_TX;
			qib_schedule_send(qp);
		}
		spin_unlock_irqrestore(&qp->s_lock, flags);

		rvt_put_qp(qp);
	} else
		spin_unlock_irqrestore(&dev->rdi.pending_lock, flags);
}

/*
 * This is called when there are send DMA descriptors that might be
 * available.
 *
 * This is called with ppd->sdma_lock held.
 */
void qib_verbs_sdma_desc_avail(struct qib_pportdata *ppd, unsigned avail)
{
	struct rvt_qp *qp;
	struct qib_qp_priv *qpp, *nqpp;
	struct rvt_qp *qps[20];
	struct qib_ibdev *dev;
	unsigned i, n;

	n = 0;
	dev = &ppd->dd->verbs_dev;
	spin_lock(&dev->rdi.pending_lock);

	/* Search wait list for first QP wanting DMA descriptors. */
	list_for_each_entry_safe(qpp, nqpp, &dev->dmawait, iowait) {
		qp = qpp->owner;
		if (qp->port_num != ppd->port)
			continue;
		if (n == ARRAY_SIZE(qps))
			break;
		if (qpp->s_tx->txreq.sg_count > avail)
			break;
		avail -= qpp->s_tx->txreq.sg_count;
		list_del_init(&qpp->iowait);
		rvt_get_qp(qp);
		qps[n++] = qp;
	}

	spin_unlock(&dev->rdi.pending_lock);

	for (i = 0; i < n; i++) {
		qp = qps[i];
		spin_lock(&qp->s_lock);
		if (qp->s_flags & RVT_S_WAIT_DMA_DESC) {
			qp->s_flags &= ~RVT_S_WAIT_DMA_DESC;
			qib_schedule_send(qp);
		}
		spin_unlock(&qp->s_lock);
		rvt_put_qp(qp);
	}
}

/*
 * This is called with ppd->sdma_lock held.
 */
static void sdma_complete(struct qib_sdma_txreq *cookie, int status)
{
	struct qib_verbs_txreq *tx =
		container_of(cookie, struct qib_verbs_txreq, txreq);
	struct rvt_qp *qp = tx->qp;
	struct qib_qp_priv *priv = qp->priv;

	spin_lock(&qp->s_lock);
	if (tx->wqe)
		rvt_send_complete(qp, tx->wqe, IB_WC_SUCCESS);
	else if (qp->ibqp.qp_type == IB_QPT_RC) {
		struct ib_header *hdr;

		if (tx->txreq.flags & QIB_SDMA_TXREQ_F_FREEBUF)
			hdr = &tx->align_buf->hdr;
		else {
			struct qib_ibdev *dev = to_idev(qp->ibqp.device);

			hdr = &dev->pio_hdrs[tx->hdr_inx].hdr;
		}
		qib_rc_send_complete(qp, hdr);
	}
	if (atomic_dec_and_test(&priv->s_dma_busy)) {
		if (qp->state == IB_QPS_RESET)
			wake_up(&priv->wait_dma);
		else if (qp->s_flags & RVT_S_WAIT_DMA) {
			qp->s_flags &= ~RVT_S_WAIT_DMA;
			qib_schedule_send(qp);
		}
	}
	spin_unlock(&qp->s_lock);

	qib_put_txreq(tx);
}

static int wait_kmem(struct qib_ibdev *dev, struct rvt_qp *qp)
{
	struct qib_qp_priv *priv = qp->priv;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&qp->s_lock, flags);
	if (ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK) {
		spin_lock(&dev->rdi.pending_lock);
		if (list_empty(&priv->iowait)) {
			if (list_empty(&dev->memwait))
				mod_timer(&dev->mem_timer, jiffies + 1);
			qp->s_flags |= RVT_S_WAIT_KMEM;
			list_add_tail(&priv->iowait, &dev->memwait);
		}
		spin_unlock(&dev->rdi.pending_lock);
		qp->s_flags &= ~RVT_S_BUSY;
		ret = -EBUSY;
	}
	spin_unlock_irqrestore(&qp->s_lock, flags);

	return ret;
}

static int qib_verbs_send_dma(struct rvt_qp *qp, struct ib_header *hdr,
			      u32 hdrwords, struct rvt_sge_state *ss, u32 len,
			      u32 plen, u32 dwords)
{
	struct qib_qp_priv *priv = qp->priv;
	struct qib_ibdev *dev = to_idev(qp->ibqp.device);
	struct qib_devdata *dd = dd_from_dev(dev);
	struct qib_ibport *ibp = to_iport(qp->ibqp.device, qp->port_num);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	struct qib_verbs_txreq *tx;
	struct qib_pio_header *phdr;
	u32 control;
	u32 ndesc;
	int ret;

	tx = priv->s_tx;
	if (tx) {
		priv->s_tx = NULL;
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
	ibp->rvp.n_unaligned++;
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
static int no_bufs_available(struct rvt_qp *qp)
{
	struct qib_qp_priv *priv = qp->priv;
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
	if (ib_rvt_state_ops[qp->state] & RVT_PROCESS_RECV_OK) {
		spin_lock(&dev->rdi.pending_lock);
		if (list_empty(&priv->iowait)) {
			dev->n_piowait++;
			qp->s_flags |= RVT_S_WAIT_PIO;
			list_add_tail(&priv->iowait, &dev->piowait);
			dd = dd_from_dev(dev);
			dd->f_wantpiobuf_intr(dd, 1);
		}
		spin_unlock(&dev->rdi.pending_lock);
		qp->s_flags &= ~RVT_S_BUSY;
		ret = -EBUSY;
	}
	spin_unlock_irqrestore(&qp->s_lock, flags);
	return ret;
}

static int qib_verbs_send_pio(struct rvt_qp *qp, struct ib_header *ibhdr,
			      u32 hdrwords, struct rvt_sge_state *ss, u32 len,
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
		rvt_update_sge(ss, len, false);
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
		rvt_put_mr(qp->s_rdma_mr);
		qp->s_rdma_mr = NULL;
	}
	if (qp->s_wqe) {
		spin_lock_irqsave(&qp->s_lock, flags);
		rvt_send_complete(qp, qp->s_wqe, IB_WC_SUCCESS);
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
 * Return non-zero and clear qp->s_flags RVT_S_BUSY otherwise.
 */
int qib_verbs_send(struct rvt_qp *qp, struct ib_header *hdr,
		   u32 hdrwords, struct rvt_sge_state *ss, u32 len)
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
	struct rvt_qp *qps[5];
	struct rvt_qp *qp;
	unsigned long flags;
	unsigned i, n;
	struct qib_qp_priv *priv;

	list = &dev->piowait;
	n = 0;

	/*
	 * Note: checking that the piowait list is empty and clearing
	 * the buffer available interrupt needs to be atomic or we
	 * could end up with QPs on the wait list with the interrupt
	 * disabled.
	 */
	spin_lock_irqsave(&dev->rdi.pending_lock, flags);
	while (!list_empty(list)) {
		if (n == ARRAY_SIZE(qps))
			goto full;
		priv = list_entry(list->next, struct qib_qp_priv, iowait);
		qp = priv->owner;
		list_del_init(&priv->iowait);
		rvt_get_qp(qp);
		qps[n++] = qp;
	}
	dd->f_wantpiobuf_intr(dd, 0);
full:
	spin_unlock_irqrestore(&dev->rdi.pending_lock, flags);

	for (i = 0; i < n; i++) {
		qp = qps[i];

		spin_lock_irqsave(&qp->s_lock, flags);
		if (qp->s_flags & RVT_S_WAIT_PIO) {
			qp->s_flags &= ~RVT_S_WAIT_PIO;
			qib_schedule_send(qp);
		}
		spin_unlock_irqrestore(&qp->s_lock, flags);

		/* Notify qib_destroy_qp() if it is waiting. */
		rvt_put_qp(qp);
	}
}

static int qib_query_port(struct rvt_dev_info *rdi, u8 port_num,
			  struct ib_port_attr *props)
{
	struct qib_ibdev *ibdev = container_of(rdi, struct qib_ibdev, rdi);
	struct qib_devdata *dd = dd_from_dev(ibdev);
	struct qib_pportdata *ppd = &dd->pport[port_num - 1];
	enum ib_mtu mtu;
	u16 lid = ppd->lid;

	/* props being zeroed by the caller, avoid zeroing it here */
	props->lid = lid ? lid : be16_to_cpu(IB_LID_PERMISSIVE);
	props->lmc = ppd->lmc;
	props->state = dd->f_iblink_state(ppd->lastibcstat);
	props->phys_state = dd->f_ibphys_portstate(ppd->lastibcstat);
	props->gid_tbl_len = QIB_GUIDS_PER_PORT;
	props->active_width = ppd->link_width_active;
	/* See rate_show() */
	props->active_speed = ppd->link_speed_active;
	props->max_vl_num = qib_num_vls(ppd->vls_supported);

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
		memcpy(device->node_desc, device_modify->node_desc,
		       IB_DEVICE_NODE_DESC_MAX);
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

static int qib_shut_down_port(struct rvt_dev_info *rdi, u8 port_num)
{
	struct qib_ibdev *ibdev = container_of(rdi, struct qib_ibdev, rdi);
	struct qib_devdata *dd = dd_from_dev(ibdev);
	struct qib_pportdata *ppd = &dd->pport[port_num - 1];

	qib_set_linkstate(ppd, QIB_IB_LINKDOWN);

	return 0;
}

static int qib_get_guid_be(struct rvt_dev_info *rdi, struct rvt_ibport *rvp,
			   int guid_index, __be64 *guid)
{
	struct qib_ibport *ibp = container_of(rvp, struct qib_ibport, rvp);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);

	if (guid_index == 0)
		*guid = ppd->guid;
	else if (guid_index < QIB_GUIDS_PER_PORT)
		*guid = ibp->guids[guid_index - 1];
	else
		return -EINVAL;

	return 0;
}

int qib_check_ah(struct ib_device *ibdev, struct rdma_ah_attr *ah_attr)
{
	if (rdma_ah_get_sl(ah_attr) > 15)
		return -EINVAL;

	if (rdma_ah_get_dlid(ah_attr) == 0)
		return -EINVAL;
	if (rdma_ah_get_dlid(ah_attr) >=
		be16_to_cpu(IB_MULTICAST_LID_BASE) &&
	    rdma_ah_get_dlid(ah_attr) !=
		be16_to_cpu(IB_LID_PERMISSIVE) &&
	    !(rdma_ah_get_ah_flags(ah_attr) & IB_AH_GRH))
		return -EINVAL;

	return 0;
}

static void qib_notify_new_ah(struct ib_device *ibdev,
			      struct rdma_ah_attr *ah_attr,
			      struct rvt_ah *ah)
{
	struct qib_ibport *ibp;
	struct qib_pportdata *ppd;

	/*
	 * Do not trust reading anything from rvt_ah at this point as it is not
	 * done being setup. We can however modify things which we need to set.
	 */

	ibp = to_iport(ibdev, rdma_ah_get_port_num(ah_attr));
	ppd = ppd_from_ibp(ibp);
	ah->vl = ibp->sl_to_vl[rdma_ah_get_sl(&ah->attr)];
	ah->log_pmtu = ilog2(ppd->ibmtu);
}

struct ib_ah *qib_create_qp0_ah(struct qib_ibport *ibp, u16 dlid)
{
	struct rdma_ah_attr attr;
	struct ib_ah *ah = ERR_PTR(-EINVAL);
	struct rvt_qp *qp0;
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	struct qib_devdata *dd = dd_from_ppd(ppd);
	u8 port_num = ppd->port;

	memset(&attr, 0, sizeof(attr));
	attr.type = rdma_ah_find_type(&dd->verbs_dev.rdi.ibdev, port_num);
	rdma_ah_set_dlid(&attr, dlid);
	rdma_ah_set_port_num(&attr, port_num);
	rcu_read_lock();
	qp0 = rcu_dereference(ibp->rvp.qp[0]);
	if (qp0)
		ah = rdma_create_ah(qp0->ibqp.pd, &attr, 0);
	rcu_read_unlock();
	return ah;
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

static void init_ibport(struct qib_pportdata *ppd)
{
	struct qib_verbs_counters cntrs;
	struct qib_ibport *ibp = &ppd->ibport_data;

	spin_lock_init(&ibp->rvp.lock);
	/* Set the prefix to the default value (see ch. 4.1.1) */
	ibp->rvp.gid_prefix = IB_DEFAULT_GID_PREFIX;
	ibp->rvp.sm_lid = be16_to_cpu(IB_LID_PERMISSIVE);
	ibp->rvp.port_cap_flags = IB_PORT_SYS_IMAGE_GUID_SUP |
		IB_PORT_CLIENT_REG_SUP | IB_PORT_SL_MAP_SUP |
		IB_PORT_TRAP_SUP | IB_PORT_AUTO_MIGR_SUP |
		IB_PORT_DR_NOTICE_SUP | IB_PORT_CAP_MASK_NOTICE_SUP |
		IB_PORT_OTHER_LOCAL_CHANGES_SUP;
	if (ppd->dd->flags & QIB_HAS_LINK_LATENCY)
		ibp->rvp.port_cap_flags |= IB_PORT_LINK_LATENCY_SUP;
	ibp->rvp.pma_counter_select[0] = IB_PMA_PORT_XMIT_DATA;
	ibp->rvp.pma_counter_select[1] = IB_PMA_PORT_RCV_DATA;
	ibp->rvp.pma_counter_select[2] = IB_PMA_PORT_XMIT_PKTS;
	ibp->rvp.pma_counter_select[3] = IB_PMA_PORT_RCV_PKTS;
	ibp->rvp.pma_counter_select[4] = IB_PMA_PORT_XMIT_WAIT;

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
	RCU_INIT_POINTER(ibp->rvp.qp[0], NULL);
	RCU_INIT_POINTER(ibp->rvp.qp[1], NULL);
}

/**
 * qib_fill_device_attr - Fill in rvt dev info device attributes.
 * @dd: the device data structure
 */
static void qib_fill_device_attr(struct qib_devdata *dd)
{
	struct rvt_dev_info *rdi = &dd->verbs_dev.rdi;

	memset(&rdi->dparms.props, 0, sizeof(rdi->dparms.props));

	rdi->dparms.props.max_pd = ib_qib_max_pds;
	rdi->dparms.props.max_ah = ib_qib_max_ahs;
	rdi->dparms.props.device_cap_flags = IB_DEVICE_BAD_PKEY_CNTR |
		IB_DEVICE_BAD_QKEY_CNTR | IB_DEVICE_SHUTDOWN_PORT |
		IB_DEVICE_SYS_IMAGE_GUID | IB_DEVICE_RC_RNR_NAK_GEN |
		IB_DEVICE_PORT_ACTIVE_EVENT | IB_DEVICE_SRQ_RESIZE;
	rdi->dparms.props.page_size_cap = PAGE_SIZE;
	rdi->dparms.props.vendor_id =
		QIB_SRC_OUI_1 << 16 | QIB_SRC_OUI_2 << 8 | QIB_SRC_OUI_3;
	rdi->dparms.props.vendor_part_id = dd->deviceid;
	rdi->dparms.props.hw_ver = dd->minrev;
	rdi->dparms.props.sys_image_guid = ib_qib_sys_image_guid;
	rdi->dparms.props.max_mr_size = ~0ULL;
	rdi->dparms.props.max_qp = ib_qib_max_qps;
	rdi->dparms.props.max_qp_wr = ib_qib_max_qp_wrs;
	rdi->dparms.props.max_send_sge = ib_qib_max_sges;
	rdi->dparms.props.max_recv_sge = ib_qib_max_sges;
	rdi->dparms.props.max_sge_rd = ib_qib_max_sges;
	rdi->dparms.props.max_cq = ib_qib_max_cqs;
	rdi->dparms.props.max_cqe = ib_qib_max_cqes;
	rdi->dparms.props.max_ah = ib_qib_max_ahs;
	rdi->dparms.props.max_mr = rdi->lkey_table.max;
	rdi->dparms.props.max_fmr = rdi->lkey_table.max;
	rdi->dparms.props.max_map_per_fmr = 32767;
	rdi->dparms.props.max_qp_rd_atom = QIB_MAX_RDMA_ATOMIC;
	rdi->dparms.props.max_qp_init_rd_atom = 255;
	rdi->dparms.props.max_srq = ib_qib_max_srqs;
	rdi->dparms.props.max_srq_wr = ib_qib_max_srq_wrs;
	rdi->dparms.props.max_srq_sge = ib_qib_max_srq_sges;
	rdi->dparms.props.atomic_cap = IB_ATOMIC_GLOB;
	rdi->dparms.props.max_pkeys = qib_get_npkeys(dd);
	rdi->dparms.props.max_mcast_grp = ib_qib_max_mcast_grps;
	rdi->dparms.props.max_mcast_qp_attach = ib_qib_max_mcast_qp_attached;
	rdi->dparms.props.max_total_mcast_qp_attach =
					rdi->dparms.props.max_mcast_qp_attach *
					rdi->dparms.props.max_mcast_grp;
	/* post send table */
	dd->verbs_dev.rdi.post_parms = qib_post_parms;

	/* opcode translation table */
	dd->verbs_dev.rdi.wc_opcode = ib_qib_wc_opcode;
}

static const struct ib_device_ops qib_dev_ops = {
	.driver_id = RDMA_DRIVER_QIB,

	.init_port = qib_create_port_files,
	.modify_device = qib_modify_device,
	.process_mad = qib_process_mad,
};

/**
 * qib_register_ib_device - register our device with the infiniband core
 * @dd: the device data structure
 * Return the allocated qib_ibdev pointer or NULL on error.
 */
int qib_register_ib_device(struct qib_devdata *dd)
{
	struct qib_ibdev *dev = &dd->verbs_dev;
	struct ib_device *ibdev = &dev->rdi.ibdev;
	struct qib_pportdata *ppd = dd->pport;
	unsigned i, ctxt;
	int ret;

	get_random_bytes(&dev->qp_rnd, sizeof(dev->qp_rnd));
	for (i = 0; i < dd->num_pports; i++)
		init_ibport(ppd + i);

	/* Only need to initialize non-zero fields. */
	timer_setup(&dev->mem_timer, mem_timer, 0);

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

	ibdev->owner = THIS_MODULE;
	ibdev->node_guid = ppd->guid;
	ibdev->phys_port_cnt = dd->num_pports;
	ibdev->dev.parent = &dd->pcidev->dev;

	snprintf(ibdev->node_desc, sizeof(ibdev->node_desc),
		 "Intel Infiniband HCA %s", init_utsname()->nodename);

	/*
	 * Fill in rvt info object.
	 */
	dd->verbs_dev.rdi.driver_f.get_pci_dev = qib_get_pci_dev;
	dd->verbs_dev.rdi.driver_f.check_ah = qib_check_ah;
	dd->verbs_dev.rdi.driver_f.setup_wqe = qib_check_send_wqe;
	dd->verbs_dev.rdi.driver_f.notify_new_ah = qib_notify_new_ah;
	dd->verbs_dev.rdi.driver_f.alloc_qpn = qib_alloc_qpn;
	dd->verbs_dev.rdi.driver_f.qp_priv_alloc = qib_qp_priv_alloc;
	dd->verbs_dev.rdi.driver_f.qp_priv_free = qib_qp_priv_free;
	dd->verbs_dev.rdi.driver_f.free_all_qps = qib_free_all_qps;
	dd->verbs_dev.rdi.driver_f.notify_qp_reset = qib_notify_qp_reset;
	dd->verbs_dev.rdi.driver_f.do_send = qib_do_send;
	dd->verbs_dev.rdi.driver_f.schedule_send = qib_schedule_send;
	dd->verbs_dev.rdi.driver_f.quiesce_qp = qib_quiesce_qp;
	dd->verbs_dev.rdi.driver_f.stop_send_queue = qib_stop_send_queue;
	dd->verbs_dev.rdi.driver_f.flush_qp_waiters = qib_flush_qp_waiters;
	dd->verbs_dev.rdi.driver_f.notify_error_qp = qib_notify_error_qp;
	dd->verbs_dev.rdi.driver_f.notify_restart_rc = qib_restart_rc;
	dd->verbs_dev.rdi.driver_f.mtu_to_path_mtu = qib_mtu_to_path_mtu;
	dd->verbs_dev.rdi.driver_f.mtu_from_qp = qib_mtu_from_qp;
	dd->verbs_dev.rdi.driver_f.get_pmtu_from_attr = qib_get_pmtu_from_attr;
	dd->verbs_dev.rdi.driver_f.schedule_send_no_lock = _qib_schedule_send;
	dd->verbs_dev.rdi.driver_f.query_port_state = qib_query_port;
	dd->verbs_dev.rdi.driver_f.shut_down_port = qib_shut_down_port;
	dd->verbs_dev.rdi.driver_f.cap_mask_chg = qib_cap_mask_chg;
	dd->verbs_dev.rdi.driver_f.notify_create_mad_agent =
						qib_notify_create_mad_agent;
	dd->verbs_dev.rdi.driver_f.notify_free_mad_agent =
						qib_notify_free_mad_agent;

	dd->verbs_dev.rdi.dparms.max_rdma_atomic = QIB_MAX_RDMA_ATOMIC;
	dd->verbs_dev.rdi.driver_f.get_guid_be = qib_get_guid_be;
	dd->verbs_dev.rdi.dparms.lkey_table_size = qib_lkey_table_size;
	dd->verbs_dev.rdi.dparms.qp_table_size = ib_qib_qp_table_size;
	dd->verbs_dev.rdi.dparms.qpn_start = 1;
	dd->verbs_dev.rdi.dparms.qpn_res_start = QIB_KD_QP;
	dd->verbs_dev.rdi.dparms.qpn_res_end = QIB_KD_QP; /* Reserve one QP */
	dd->verbs_dev.rdi.dparms.qpn_inc = 1;
	dd->verbs_dev.rdi.dparms.qos_shift = 1;
	dd->verbs_dev.rdi.dparms.psn_mask = QIB_PSN_MASK;
	dd->verbs_dev.rdi.dparms.psn_shift = QIB_PSN_SHIFT;
	dd->verbs_dev.rdi.dparms.psn_modify_mask = QIB_PSN_MASK;
	dd->verbs_dev.rdi.dparms.nports = dd->num_pports;
	dd->verbs_dev.rdi.dparms.npkeys = qib_get_npkeys(dd);
	dd->verbs_dev.rdi.dparms.node = dd->assigned_node_id;
	dd->verbs_dev.rdi.dparms.core_cap_flags = RDMA_CORE_PORT_IBA_IB;
	dd->verbs_dev.rdi.dparms.max_mad_size = IB_MGMT_MAD_SIZE;
	dd->verbs_dev.rdi.dparms.sge_copy_mode = RVT_SGE_COPY_MEMCPY;

	qib_fill_device_attr(dd);

	ppd = dd->pport;
	for (i = 0; i < dd->num_pports; i++, ppd++) {
		ctxt = ppd->hw_pidx;
		rvt_init_port(&dd->verbs_dev.rdi,
			      &ppd->ibport_data.rvp,
			      i,
			      dd->rcd[ctxt]->pkeys);
	}
	rdma_set_device_sysfs_group(&dd->verbs_dev.rdi.ibdev, &qib_attr_group);

	ib_set_device_ops(ibdev, &qib_dev_ops);
	ret = rvt_register_device(&dd->verbs_dev.rdi);
	if (ret)
		goto err_tx;

	return ret;

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
	qib_dev_err(dd, "cannot register verbs: %d!\n", -ret);
	return ret;
}

void qib_unregister_ib_device(struct qib_devdata *dd)
{
	struct qib_ibdev *dev = &dd->verbs_dev;

	qib_verbs_unregister_sysfs(dd);

	rvt_unregister_device(&dd->verbs_dev.rdi);

	if (!list_empty(&dev->piowait))
		qib_dev_err(dd, "piowait list not empty!\n");
	if (!list_empty(&dev->dmawait))
		qib_dev_err(dd, "dmawait list not empty!\n");
	if (!list_empty(&dev->txwait))
		qib_dev_err(dd, "txwait list not empty!\n");
	if (!list_empty(&dev->memwait))
		qib_dev_err(dd, "memwait list not empty!\n");

	del_timer_sync(&dev->mem_timer);
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
}

/**
 * _qib_schedule_send - schedule progress
 * @qp - the qp
 *
 * This schedules progress w/o regard to the s_flags.
 *
 * It is only used in post send, which doesn't hold
 * the s_lock.
 */
bool _qib_schedule_send(struct rvt_qp *qp)
{
	struct qib_ibport *ibp =
		to_iport(qp->ibqp.device, qp->port_num);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	struct qib_qp_priv *priv = qp->priv;

	return queue_work(ppd->qib_wq, &priv->s_work);
}

/**
 * qib_schedule_send - schedule progress
 * @qp - the qp
 *
 * This schedules qp progress.  The s_lock
 * should be held.
 */
bool qib_schedule_send(struct rvt_qp *qp)
{
	if (qib_send_ok(qp))
		return _qib_schedule_send(qp);
	return false;
}
