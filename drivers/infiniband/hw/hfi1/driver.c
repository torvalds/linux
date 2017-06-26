/*
 * Copyright(c) 2015-2017 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
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

#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/prefetch.h>
#include <rdma/ib_verbs.h>

#include "hfi.h"
#include "trace.h"
#include "qp.h"
#include "sdma.h"
#include "debugfs.h"
#include "vnic.h"

#undef pr_fmt
#define pr_fmt(fmt) DRIVER_NAME ": " fmt

/*
 * The size has to be longer than this string, so we can append
 * board/chip information to it in the initialization code.
 */
const char ib_hfi1_version[] = HFI1_DRIVER_VERSION "\n";

DEFINE_SPINLOCK(hfi1_devs_lock);
LIST_HEAD(hfi1_dev_list);
DEFINE_MUTEX(hfi1_mutex);	/* general driver use */

unsigned int hfi1_max_mtu = HFI1_DEFAULT_MAX_MTU;
module_param_named(max_mtu, hfi1_max_mtu, uint, S_IRUGO);
MODULE_PARM_DESC(max_mtu, "Set max MTU bytes, default is " __stringify(
		 HFI1_DEFAULT_MAX_MTU));

unsigned int hfi1_cu = 1;
module_param_named(cu, hfi1_cu, uint, S_IRUGO);
MODULE_PARM_DESC(cu, "Credit return units");

unsigned long hfi1_cap_mask = HFI1_CAP_MASK_DEFAULT;
static int hfi1_caps_set(const char *val, const struct kernel_param *kp);
static int hfi1_caps_get(char *buffer, const struct kernel_param *kp);
static const struct kernel_param_ops cap_ops = {
	.set = hfi1_caps_set,
	.get = hfi1_caps_get
};
module_param_cb(cap_mask, &cap_ops, &hfi1_cap_mask, S_IWUSR | S_IRUGO);
MODULE_PARM_DESC(cap_mask, "Bit mask of enabled/disabled HW features");

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Intel Omni-Path Architecture driver");

/*
 * MAX_PKT_RCV is the max # if packets processed per receive interrupt.
 */
#define MAX_PKT_RECV 64
/*
 * MAX_PKT_THREAD_RCV is the max # of packets processed before
 * the qp_wait_list queue is flushed.
 */
#define MAX_PKT_RECV_THREAD (MAX_PKT_RECV * 4)
#define EGR_HEAD_UPDATE_THRESHOLD 16

struct hfi1_ib_stats hfi1_stats;

static int hfi1_caps_set(const char *val, const struct kernel_param *kp)
{
	int ret = 0;
	unsigned long *cap_mask_ptr = (unsigned long *)kp->arg,
		cap_mask = *cap_mask_ptr, value, diff,
		write_mask = ((HFI1_CAP_WRITABLE_MASK << HFI1_CAP_USER_SHIFT) |
			      HFI1_CAP_WRITABLE_MASK);

	ret = kstrtoul(val, 0, &value);
	if (ret) {
		pr_warn("Invalid module parameter value for 'cap_mask'\n");
		goto done;
	}
	/* Get the changed bits (except the locked bit) */
	diff = value ^ (cap_mask & ~HFI1_CAP_LOCKED_SMASK);

	/* Remove any bits that are not allowed to change after driver load */
	if (HFI1_CAP_LOCKED() && (diff & ~write_mask)) {
		pr_warn("Ignoring non-writable capability bits %#lx\n",
			diff & ~write_mask);
		diff &= write_mask;
	}

	/* Mask off any reserved bits */
	diff &= ~HFI1_CAP_RESERVED_MASK;
	/* Clear any previously set and changing bits */
	cap_mask &= ~diff;
	/* Update the bits with the new capability */
	cap_mask |= (value & diff);
	/* Check for any kernel/user restrictions */
	diff = (cap_mask & (HFI1_CAP_MUST_HAVE_KERN << HFI1_CAP_USER_SHIFT)) ^
		((cap_mask & HFI1_CAP_MUST_HAVE_KERN) << HFI1_CAP_USER_SHIFT);
	cap_mask &= ~diff;
	/* Set the bitmask to the final set */
	*cap_mask_ptr = cap_mask;
done:
	return ret;
}

static int hfi1_caps_get(char *buffer, const struct kernel_param *kp)
{
	unsigned long cap_mask = *(unsigned long *)kp->arg;

	cap_mask &= ~HFI1_CAP_LOCKED_SMASK;
	cap_mask |= ((cap_mask & HFI1_CAP_K2U) << HFI1_CAP_USER_SHIFT);

	return scnprintf(buffer, PAGE_SIZE, "0x%lx", cap_mask);
}

const char *get_unit_name(int unit)
{
	static char iname[16];

	snprintf(iname, sizeof(iname), DRIVER_NAME "_%u", unit);
	return iname;
}

const char *get_card_name(struct rvt_dev_info *rdi)
{
	struct hfi1_ibdev *ibdev = container_of(rdi, struct hfi1_ibdev, rdi);
	struct hfi1_devdata *dd = container_of(ibdev,
					       struct hfi1_devdata, verbs_dev);
	return get_unit_name(dd->unit);
}

struct pci_dev *get_pci_dev(struct rvt_dev_info *rdi)
{
	struct hfi1_ibdev *ibdev = container_of(rdi, struct hfi1_ibdev, rdi);
	struct hfi1_devdata *dd = container_of(ibdev,
					       struct hfi1_devdata, verbs_dev);
	return dd->pcidev;
}

/*
 * Return count of units with at least one port ACTIVE.
 */
int hfi1_count_active_units(void)
{
	struct hfi1_devdata *dd;
	struct hfi1_pportdata *ppd;
	unsigned long flags;
	int pidx, nunits_active = 0;

	spin_lock_irqsave(&hfi1_devs_lock, flags);
	list_for_each_entry(dd, &hfi1_dev_list, list) {
		if (!(dd->flags & HFI1_PRESENT) || !dd->kregbase)
			continue;
		for (pidx = 0; pidx < dd->num_pports; ++pidx) {
			ppd = dd->pport + pidx;
			if (ppd->lid && ppd->linkup) {
				nunits_active++;
				break;
			}
		}
	}
	spin_unlock_irqrestore(&hfi1_devs_lock, flags);
	return nunits_active;
}

/*
 * Get address of eager buffer from it's index (allocated in chunks, not
 * contiguous).
 */
static inline void *get_egrbuf(const struct hfi1_ctxtdata *rcd, u64 rhf,
			       u8 *update)
{
	u32 idx = rhf_egr_index(rhf), offset = rhf_egr_buf_offset(rhf);

	*update |= !(idx & (rcd->egrbufs.threshold - 1)) && !offset;
	return (void *)(((u64)(rcd->egrbufs.rcvtids[idx].addr)) +
			(offset * RCV_BUF_BLOCK_SIZE));
}

/*
 * Validate and encode the a given RcvArray Buffer size.
 * The function will check whether the given size falls within
 * allowed size ranges for the respective type and, optionally,
 * return the proper encoding.
 */
int hfi1_rcvbuf_validate(u32 size, u8 type, u16 *encoded)
{
	if (unlikely(!PAGE_ALIGNED(size)))
		return 0;
	if (unlikely(size < MIN_EAGER_BUFFER))
		return 0;
	if (size >
	    (type == PT_EAGER ? MAX_EAGER_BUFFER : MAX_EXPECTED_BUFFER))
		return 0;
	if (encoded)
		*encoded = ilog2(size / PAGE_SIZE) + 1;
	return 1;
}

static void rcv_hdrerr(struct hfi1_ctxtdata *rcd, struct hfi1_pportdata *ppd,
		       struct hfi1_packet *packet)
{
	struct ib_header *rhdr = packet->hdr;
	u32 rte = rhf_rcv_type_err(packet->rhf);
	int lnh = ib_get_lnh(rhdr);
	struct hfi1_ibport *ibp = rcd_to_iport(rcd);
	struct hfi1_devdata *dd = ppd->dd;
	struct rvt_dev_info *rdi = &dd->verbs_dev.rdi;

	if (packet->rhf & (RHF_VCRC_ERR | RHF_ICRC_ERR))
		return;

	if (packet->rhf & RHF_TID_ERR) {
		/* For TIDERR and RC QPs preemptively schedule a NAK */
		struct ib_other_headers *ohdr = NULL;
		u32 tlen = rhf_pkt_len(packet->rhf); /* in bytes */
		u16 lid  = ib_get_dlid(rhdr);
		u32 qp_num;
		u32 rcv_flags = 0;

		/* Sanity check packet */
		if (tlen < 24)
			goto drop;

		/* Check for GRH */
		if (lnh == HFI1_LRH_BTH) {
			ohdr = &rhdr->u.oth;
		} else if (lnh == HFI1_LRH_GRH) {
			u32 vtf;

			ohdr = &rhdr->u.l.oth;
			if (rhdr->u.l.grh.next_hdr != IB_GRH_NEXT_HDR)
				goto drop;
			vtf = be32_to_cpu(rhdr->u.l.grh.version_tclass_flow);
			if ((vtf >> IB_GRH_VERSION_SHIFT) != IB_GRH_VERSION)
				goto drop;
			rcv_flags |= HFI1_HAS_GRH;
		} else {
			goto drop;
		}
		/* Get the destination QP number. */
		qp_num = be32_to_cpu(ohdr->bth[1]) & RVT_QPN_MASK;
		if (lid < be16_to_cpu(IB_MULTICAST_LID_BASE)) {
			struct rvt_qp *qp;
			unsigned long flags;

			rcu_read_lock();
			qp = rvt_lookup_qpn(rdi, &ibp->rvp, qp_num);
			if (!qp) {
				rcu_read_unlock();
				goto drop;
			}

			/*
			 * Handle only RC QPs - for other QP types drop error
			 * packet.
			 */
			spin_lock_irqsave(&qp->r_lock, flags);

			/* Check for valid receive state. */
			if (!(ib_rvt_state_ops[qp->state] &
			      RVT_PROCESS_RECV_OK)) {
				ibp->rvp.n_pkt_drops++;
			}

			switch (qp->ibqp.qp_type) {
			case IB_QPT_RC:
				hfi1_rc_hdrerr(
					rcd,
					rhdr,
					rcv_flags,
					qp);
				break;
			default:
				/* For now don't handle any other QP types */
				break;
			}

			spin_unlock_irqrestore(&qp->r_lock, flags);
			rcu_read_unlock();
		} /* Unicast QP */
	} /* Valid packet with TIDErr */

	/* handle "RcvTypeErr" flags */
	switch (rte) {
	case RHF_RTE_ERROR_OP_CODE_ERR:
	{
		u32 opcode;
		void *ebuf = NULL;
		__be32 *bth = NULL;

		if (rhf_use_egr_bfr(packet->rhf))
			ebuf = packet->ebuf;

		if (!ebuf)
			goto drop; /* this should never happen */

		if (lnh == HFI1_LRH_BTH)
			bth = (__be32 *)ebuf;
		else if (lnh == HFI1_LRH_GRH)
			bth = (__be32 *)((char *)ebuf + sizeof(struct ib_grh));
		else
			goto drop;

		opcode = be32_to_cpu(bth[0]) >> 24;
		opcode &= 0xff;

		if (opcode == IB_OPCODE_CNP) {
			/*
			 * Only in pre-B0 h/w is the CNP_OPCODE handled
			 * via this code path.
			 */
			struct rvt_qp *qp = NULL;
			u32 lqpn, rqpn;
			u16 rlid;
			u8 svc_type, sl, sc5;

			sc5 = hfi1_9B_get_sc5(rhdr, packet->rhf);
			sl = ibp->sc_to_sl[sc5];

			lqpn = be32_to_cpu(bth[1]) & RVT_QPN_MASK;
			rcu_read_lock();
			qp = rvt_lookup_qpn(rdi, &ibp->rvp, lqpn);
			if (!qp) {
				rcu_read_unlock();
				goto drop;
			}

			switch (qp->ibqp.qp_type) {
			case IB_QPT_UD:
				rlid = 0;
				rqpn = 0;
				svc_type = IB_CC_SVCTYPE_UD;
				break;
			case IB_QPT_UC:
				rlid = ib_get_slid(rhdr);
				rqpn = qp->remote_qpn;
				svc_type = IB_CC_SVCTYPE_UC;
				break;
			default:
				goto drop;
			}

			process_becn(ppd, sl, rlid, lqpn, rqpn, svc_type);
			rcu_read_unlock();
		}

		packet->rhf &= ~RHF_RCV_TYPE_ERR_SMASK;
		break;
	}
	default:
		break;
	}

drop:
	return;
}

static inline void init_packet(struct hfi1_ctxtdata *rcd,
			       struct hfi1_packet *packet)
{
	packet->rsize = rcd->rcvhdrqentsize; /* words */
	packet->maxcnt = rcd->rcvhdrq_cnt * packet->rsize; /* words */
	packet->rcd = rcd;
	packet->updegr = 0;
	packet->etail = -1;
	packet->rhf_addr = get_rhf_addr(rcd);
	packet->rhf = rhf_to_cpu(packet->rhf_addr);
	packet->rhqoff = rcd->head;
	packet->numpkt = 0;
	packet->rcv_flags = 0;
}

void hfi1_process_ecn_slowpath(struct rvt_qp *qp, struct hfi1_packet *pkt,
			       bool do_cnp)
{
	struct hfi1_ibport *ibp = to_iport(qp->ibqp.device, qp->port_num);
	struct ib_header *hdr = pkt->hdr;
	struct ib_other_headers *ohdr = pkt->ohdr;
	struct ib_grh *grh = NULL;
	u32 rqpn = 0, bth1;
	u16 rlid, dlid = ib_get_dlid(hdr);
	u8 sc, svc_type;
	bool is_mcast = false;

	if (pkt->rcv_flags & HFI1_HAS_GRH)
		grh = &hdr->u.l.grh;

	switch (qp->ibqp.qp_type) {
	case IB_QPT_SMI:
	case IB_QPT_GSI:
	case IB_QPT_UD:
		rlid = ib_get_slid(hdr);
		rqpn = be32_to_cpu(ohdr->u.ud.deth[1]) & RVT_QPN_MASK;
		svc_type = IB_CC_SVCTYPE_UD;
		is_mcast = (dlid > be16_to_cpu(IB_MULTICAST_LID_BASE)) &&
			(dlid != be16_to_cpu(IB_LID_PERMISSIVE));
		break;
	case IB_QPT_UC:
		rlid = rdma_ah_get_dlid(&qp->remote_ah_attr);
		rqpn = qp->remote_qpn;
		svc_type = IB_CC_SVCTYPE_UC;
		break;
	case IB_QPT_RC:
		rlid = rdma_ah_get_dlid(&qp->remote_ah_attr);
		rqpn = qp->remote_qpn;
		svc_type = IB_CC_SVCTYPE_RC;
		break;
	default:
		return;
	}

	sc = hfi1_9B_get_sc5(hdr, pkt->rhf);

	bth1 = be32_to_cpu(ohdr->bth[1]);
	if (do_cnp && (bth1 & IB_FECN_SMASK)) {
		u16 pkey = (u16)be32_to_cpu(ohdr->bth[0]);

		return_cnp(ibp, qp, rqpn, pkey, dlid, rlid, sc, grh);
	}

	if (!is_mcast && (bth1 & IB_BECN_SMASK)) {
		struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
		u32 lqpn = bth1 & RVT_QPN_MASK;
		u8 sl = ibp->sc_to_sl[sc];

		process_becn(ppd, sl, rlid, lqpn, rqpn, svc_type);
	}

}

struct ps_mdata {
	struct hfi1_ctxtdata *rcd;
	u32 rsize;
	u32 maxcnt;
	u32 ps_head;
	u32 ps_tail;
	u32 ps_seq;
};

static inline void init_ps_mdata(struct ps_mdata *mdata,
				 struct hfi1_packet *packet)
{
	struct hfi1_ctxtdata *rcd = packet->rcd;

	mdata->rcd = rcd;
	mdata->rsize = packet->rsize;
	mdata->maxcnt = packet->maxcnt;
	mdata->ps_head = packet->rhqoff;

	if (HFI1_CAP_KGET_MASK(rcd->flags, DMA_RTAIL)) {
		mdata->ps_tail = get_rcvhdrtail(rcd);
		if (rcd->ctxt == HFI1_CTRL_CTXT)
			mdata->ps_seq = rcd->seq_cnt;
		else
			mdata->ps_seq = 0; /* not used with DMA_RTAIL */
	} else {
		mdata->ps_tail = 0; /* used only with DMA_RTAIL*/
		mdata->ps_seq = rcd->seq_cnt;
	}
}

static inline int ps_done(struct ps_mdata *mdata, u64 rhf,
			  struct hfi1_ctxtdata *rcd)
{
	if (HFI1_CAP_KGET_MASK(rcd->flags, DMA_RTAIL))
		return mdata->ps_head == mdata->ps_tail;
	return mdata->ps_seq != rhf_rcv_seq(rhf);
}

static inline int ps_skip(struct ps_mdata *mdata, u64 rhf,
			  struct hfi1_ctxtdata *rcd)
{
	/*
	 * Control context can potentially receive an invalid rhf.
	 * Drop such packets.
	 */
	if ((rcd->ctxt == HFI1_CTRL_CTXT) && (mdata->ps_head != mdata->ps_tail))
		return mdata->ps_seq != rhf_rcv_seq(rhf);

	return 0;
}

static inline void update_ps_mdata(struct ps_mdata *mdata,
				   struct hfi1_ctxtdata *rcd)
{
	mdata->ps_head += mdata->rsize;
	if (mdata->ps_head >= mdata->maxcnt)
		mdata->ps_head = 0;

	/* Control context must do seq counting */
	if (!HFI1_CAP_KGET_MASK(rcd->flags, DMA_RTAIL) ||
	    (rcd->ctxt == HFI1_CTRL_CTXT)) {
		if (++mdata->ps_seq > 13)
			mdata->ps_seq = 1;
	}
}

/*
 * prescan_rxq - search through the receive queue looking for packets
 * containing Excplicit Congestion Notifications (FECNs, or BECNs).
 * When an ECN is found, process the Congestion Notification, and toggle
 * it off.
 * This is declared as a macro to allow quick checking of the port to avoid
 * the overhead of a function call if not enabled.
 */
#define prescan_rxq(rcd, packet) \
	do { \
		if (rcd->ppd->cc_prescan) \
			__prescan_rxq(packet); \
	} while (0)
static void __prescan_rxq(struct hfi1_packet *packet)
{
	struct hfi1_ctxtdata *rcd = packet->rcd;
	struct ps_mdata mdata;

	init_ps_mdata(&mdata, packet);

	while (1) {
		struct hfi1_devdata *dd = rcd->dd;
		struct hfi1_ibport *ibp = rcd_to_iport(rcd);
		__le32 *rhf_addr = (__le32 *)rcd->rcvhdrq + mdata.ps_head +
					 dd->rhf_offset;
		struct rvt_qp *qp;
		struct ib_header *hdr;
		struct rvt_dev_info *rdi = &dd->verbs_dev.rdi;
		u64 rhf = rhf_to_cpu(rhf_addr);
		u32 etype = rhf_rcv_type(rhf), qpn, bth1;
		int is_ecn = 0;
		u8 lnh;

		if (ps_done(&mdata, rhf, rcd))
			break;

		if (ps_skip(&mdata, rhf, rcd))
			goto next;

		if (etype != RHF_RCV_TYPE_IB)
			goto next;

		packet->hdr = hfi1_get_msgheader(dd, rhf_addr);
		hdr = packet->hdr;
		lnh = ib_get_lnh(hdr);

		if (lnh == HFI1_LRH_BTH) {
			packet->ohdr = &hdr->u.oth;
		} else if (lnh == HFI1_LRH_GRH) {
			packet->ohdr = &hdr->u.l.oth;
			packet->rcv_flags |= HFI1_HAS_GRH;
		} else {
			goto next; /* just in case */
		}

		bth1 = be32_to_cpu(packet->ohdr->bth[1]);
		is_ecn = !!(bth1 & (IB_FECN_SMASK | IB_BECN_SMASK));

		if (!is_ecn)
			goto next;

		qpn = bth1 & RVT_QPN_MASK;
		rcu_read_lock();
		qp = rvt_lookup_qpn(rdi, &ibp->rvp, qpn);

		if (!qp) {
			rcu_read_unlock();
			goto next;
		}

		process_ecn(qp, packet, true);
		rcu_read_unlock();

		/* turn off BECN, FECN */
		bth1 &= ~(IB_FECN_SMASK | IB_BECN_SMASK);
		packet->ohdr->bth[1] = cpu_to_be32(bth1);
next:
		update_ps_mdata(&mdata, rcd);
	}
}

static void process_rcv_qp_work(struct hfi1_ctxtdata *rcd)
{
	struct rvt_qp *qp, *nqp;

	/*
	 * Iterate over all QPs waiting to respond.
	 * The list won't change since the IRQ is only run on one CPU.
	 */
	list_for_each_entry_safe(qp, nqp, &rcd->qp_wait_list, rspwait) {
		list_del_init(&qp->rspwait);
		if (qp->r_flags & RVT_R_RSP_NAK) {
			qp->r_flags &= ~RVT_R_RSP_NAK;
			hfi1_send_rc_ack(rcd, qp, 0);
		}
		if (qp->r_flags & RVT_R_RSP_SEND) {
			unsigned long flags;

			qp->r_flags &= ~RVT_R_RSP_SEND;
			spin_lock_irqsave(&qp->s_lock, flags);
			if (ib_rvt_state_ops[qp->state] &
					RVT_PROCESS_OR_FLUSH_SEND)
				hfi1_schedule_send(qp);
			spin_unlock_irqrestore(&qp->s_lock, flags);
		}
		rvt_put_qp(qp);
	}
}

static noinline int max_packet_exceeded(struct hfi1_packet *packet, int thread)
{
	if (thread) {
		if ((packet->numpkt & (MAX_PKT_RECV_THREAD - 1)) == 0)
			/* allow defered processing */
			process_rcv_qp_work(packet->rcd);
		cond_resched();
		return RCV_PKT_OK;
	} else {
		this_cpu_inc(*packet->rcd->dd->rcv_limit);
		return RCV_PKT_LIMIT;
	}
}

static inline int check_max_packet(struct hfi1_packet *packet, int thread)
{
	int ret = RCV_PKT_OK;

	if (unlikely((packet->numpkt & (MAX_PKT_RECV - 1)) == 0))
		ret = max_packet_exceeded(packet, thread);
	return ret;
}

static noinline int skip_rcv_packet(struct hfi1_packet *packet, int thread)
{
	int ret;

	/* Set up for the next packet */
	packet->rhqoff += packet->rsize;
	if (packet->rhqoff >= packet->maxcnt)
		packet->rhqoff = 0;

	packet->numpkt++;
	ret = check_max_packet(packet, thread);

	packet->rhf_addr = (__le32 *)packet->rcd->rcvhdrq + packet->rhqoff +
				     packet->rcd->dd->rhf_offset;
	packet->rhf = rhf_to_cpu(packet->rhf_addr);

	return ret;
}

static inline int process_rcv_packet(struct hfi1_packet *packet, int thread)
{
	int ret;

	packet->hdr = hfi1_get_msgheader(packet->rcd->dd,
					 packet->rhf_addr);
	packet->hlen = (u8 *)packet->rhf_addr - (u8 *)packet->hdr;
	packet->etype = rhf_rcv_type(packet->rhf);
	/* total length */
	packet->tlen = rhf_pkt_len(packet->rhf); /* in bytes */
	/* retrieve eager buffer details */
	packet->ebuf = NULL;
	if (rhf_use_egr_bfr(packet->rhf)) {
		packet->etail = rhf_egr_index(packet->rhf);
		packet->ebuf = get_egrbuf(packet->rcd, packet->rhf,
				 &packet->updegr);
		/*
		 * Prefetch the contents of the eager buffer.  It is
		 * OK to send a negative length to prefetch_range().
		 * The +2 is the size of the RHF.
		 */
		prefetch_range(packet->ebuf,
			       packet->tlen - ((packet->rcd->rcvhdrqentsize -
					       (rhf_hdrq_offset(packet->rhf)
						+ 2)) * 4));
	}

	/*
	 * Call a type specific handler for the packet. We
	 * should be able to trust that etype won't be beyond
	 * the range of valid indexes. If so something is really
	 * wrong and we can probably just let things come
	 * crashing down. There is no need to eat another
	 * comparison in this performance critical code.
	 */
	packet->rcd->dd->rhf_rcv_function_map[packet->etype](packet);
	packet->numpkt++;

	/* Set up for the next packet */
	packet->rhqoff += packet->rsize;
	if (packet->rhqoff >= packet->maxcnt)
		packet->rhqoff = 0;

	ret = check_max_packet(packet, thread);

	packet->rhf_addr = (__le32 *)packet->rcd->rcvhdrq + packet->rhqoff +
				      packet->rcd->dd->rhf_offset;
	packet->rhf = rhf_to_cpu(packet->rhf_addr);

	return ret;
}

static inline void process_rcv_update(int last, struct hfi1_packet *packet)
{
	/*
	 * Update head regs etc., every 16 packets, if not last pkt,
	 * to help prevent rcvhdrq overflows, when many packets
	 * are processed and queue is nearly full.
	 * Don't request an interrupt for intermediate updates.
	 */
	if (!last && !(packet->numpkt & 0xf)) {
		update_usrhead(packet->rcd, packet->rhqoff, packet->updegr,
			       packet->etail, 0, 0);
		packet->updegr = 0;
	}
	packet->rcv_flags = 0;
}

static inline void finish_packet(struct hfi1_packet *packet)
{
	/*
	 * Nothing we need to free for the packet.
	 *
	 * The only thing we need to do is a final update and call for an
	 * interrupt
	 */
	update_usrhead(packet->rcd, packet->rcd->head, packet->updegr,
		       packet->etail, rcv_intr_dynamic, packet->numpkt);
}

/*
 * Handle receive interrupts when using the no dma rtail option.
 */
int handle_receive_interrupt_nodma_rtail(struct hfi1_ctxtdata *rcd, int thread)
{
	u32 seq;
	int last = RCV_PKT_OK;
	struct hfi1_packet packet;

	init_packet(rcd, &packet);
	seq = rhf_rcv_seq(packet.rhf);
	if (seq != rcd->seq_cnt) {
		last = RCV_PKT_DONE;
		goto bail;
	}

	prescan_rxq(rcd, &packet);

	while (last == RCV_PKT_OK) {
		last = process_rcv_packet(&packet, thread);
		seq = rhf_rcv_seq(packet.rhf);
		if (++rcd->seq_cnt > 13)
			rcd->seq_cnt = 1;
		if (seq != rcd->seq_cnt)
			last = RCV_PKT_DONE;
		process_rcv_update(last, &packet);
	}
	process_rcv_qp_work(rcd);
	rcd->head = packet.rhqoff;
bail:
	finish_packet(&packet);
	return last;
}

int handle_receive_interrupt_dma_rtail(struct hfi1_ctxtdata *rcd, int thread)
{
	u32 hdrqtail;
	int last = RCV_PKT_OK;
	struct hfi1_packet packet;

	init_packet(rcd, &packet);
	hdrqtail = get_rcvhdrtail(rcd);
	if (packet.rhqoff == hdrqtail) {
		last = RCV_PKT_DONE;
		goto bail;
	}
	smp_rmb();  /* prevent speculative reads of dma'ed hdrq */

	prescan_rxq(rcd, &packet);

	while (last == RCV_PKT_OK) {
		last = process_rcv_packet(&packet, thread);
		if (packet.rhqoff == hdrqtail)
			last = RCV_PKT_DONE;
		process_rcv_update(last, &packet);
	}
	process_rcv_qp_work(rcd);
	rcd->head = packet.rhqoff;
bail:
	finish_packet(&packet);
	return last;
}

static inline void set_nodma_rtail(struct hfi1_devdata *dd, u8 ctxt)
{
	int i;

	/*
	 * For dynamically allocated kernel contexts (like vnic) switch
	 * interrupt handler only for that context. Otherwise, switch
	 * interrupt handler for all statically allocated kernel contexts.
	 */
	if (ctxt >= dd->first_dyn_alloc_ctxt) {
		dd->rcd[ctxt]->do_interrupt =
			&handle_receive_interrupt_nodma_rtail;
		return;
	}

	for (i = HFI1_CTRL_CTXT + 1; i < dd->first_dyn_alloc_ctxt; i++)
		dd->rcd[i]->do_interrupt =
			&handle_receive_interrupt_nodma_rtail;
}

static inline void set_dma_rtail(struct hfi1_devdata *dd, u8 ctxt)
{
	int i;

	/*
	 * For dynamically allocated kernel contexts (like vnic) switch
	 * interrupt handler only for that context. Otherwise, switch
	 * interrupt handler for all statically allocated kernel contexts.
	 */
	if (ctxt >= dd->first_dyn_alloc_ctxt) {
		dd->rcd[ctxt]->do_interrupt =
			&handle_receive_interrupt_dma_rtail;
		return;
	}

	for (i = HFI1_CTRL_CTXT + 1; i < dd->first_dyn_alloc_ctxt; i++)
		dd->rcd[i]->do_interrupt =
			&handle_receive_interrupt_dma_rtail;
}

void set_all_slowpath(struct hfi1_devdata *dd)
{
	int i;

	/* HFI1_CTRL_CTXT must always use the slow path interrupt handler */
	for (i = HFI1_CTRL_CTXT + 1; i < dd->num_rcv_contexts; i++) {
		struct hfi1_ctxtdata *rcd = dd->rcd[i];

		if ((i < dd->first_dyn_alloc_ctxt) ||
		    (rcd && rcd->sc && (rcd->sc->type == SC_KERNEL)))
			rcd->do_interrupt = &handle_receive_interrupt;
	}
}

static inline int set_armed_to_active(struct hfi1_ctxtdata *rcd,
				      struct hfi1_packet *packet,
				      struct hfi1_devdata *dd)
{
	struct work_struct *lsaw = &rcd->ppd->linkstate_active_work;
	struct ib_header *hdr = hfi1_get_msgheader(packet->rcd->dd,
						   packet->rhf_addr);
	u8 etype = rhf_rcv_type(packet->rhf);

	if (etype == RHF_RCV_TYPE_IB &&
	    hfi1_9B_get_sc5(hdr, packet->rhf) != 0xf) {
		int hwstate = read_logical_state(dd);

		if (hwstate != LSTATE_ACTIVE) {
			dd_dev_info(dd, "Unexpected link state %d\n", hwstate);
			return 0;
		}

		queue_work(rcd->ppd->hfi1_wq, lsaw);
		return 1;
	}
	return 0;
}

/*
 * handle_receive_interrupt - receive a packet
 * @rcd: the context
 *
 * Called from interrupt handler for errors or receive interrupt.
 * This is the slow path interrupt handler.
 */
int handle_receive_interrupt(struct hfi1_ctxtdata *rcd, int thread)
{
	struct hfi1_devdata *dd = rcd->dd;
	u32 hdrqtail;
	int needset, last = RCV_PKT_OK;
	struct hfi1_packet packet;
	int skip_pkt = 0;

	/* Control context will always use the slow path interrupt handler */
	needset = (rcd->ctxt == HFI1_CTRL_CTXT) ? 0 : 1;

	init_packet(rcd, &packet);

	if (!HFI1_CAP_KGET_MASK(rcd->flags, DMA_RTAIL)) {
		u32 seq = rhf_rcv_seq(packet.rhf);

		if (seq != rcd->seq_cnt) {
			last = RCV_PKT_DONE;
			goto bail;
		}
		hdrqtail = 0;
	} else {
		hdrqtail = get_rcvhdrtail(rcd);
		if (packet.rhqoff == hdrqtail) {
			last = RCV_PKT_DONE;
			goto bail;
		}
		smp_rmb();  /* prevent speculative reads of dma'ed hdrq */

		/*
		 * Control context can potentially receive an invalid
		 * rhf. Drop such packets.
		 */
		if (rcd->ctxt == HFI1_CTRL_CTXT) {
			u32 seq = rhf_rcv_seq(packet.rhf);

			if (seq != rcd->seq_cnt)
				skip_pkt = 1;
		}
	}

	prescan_rxq(rcd, &packet);

	while (last == RCV_PKT_OK) {
		if (unlikely(dd->do_drop &&
			     atomic_xchg(&dd->drop_packet, DROP_PACKET_OFF) ==
			     DROP_PACKET_ON)) {
			dd->do_drop = 0;

			/* On to the next packet */
			packet.rhqoff += packet.rsize;
			packet.rhf_addr = (__le32 *)rcd->rcvhdrq +
					  packet.rhqoff +
					  dd->rhf_offset;
			packet.rhf = rhf_to_cpu(packet.rhf_addr);

		} else if (skip_pkt) {
			last = skip_rcv_packet(&packet, thread);
			skip_pkt = 0;
		} else {
			/* Auto activate link on non-SC15 packet receive */
			if (unlikely(rcd->ppd->host_link_state ==
				     HLS_UP_ARMED) &&
			    set_armed_to_active(rcd, &packet, dd))
				goto bail;
			last = process_rcv_packet(&packet, thread);
		}

		if (!HFI1_CAP_KGET_MASK(rcd->flags, DMA_RTAIL)) {
			u32 seq = rhf_rcv_seq(packet.rhf);

			if (++rcd->seq_cnt > 13)
				rcd->seq_cnt = 1;
			if (seq != rcd->seq_cnt)
				last = RCV_PKT_DONE;
			if (needset) {
				dd_dev_info(dd, "Switching to NO_DMA_RTAIL\n");
				set_nodma_rtail(dd, rcd->ctxt);
				needset = 0;
			}
		} else {
			if (packet.rhqoff == hdrqtail)
				last = RCV_PKT_DONE;
			/*
			 * Control context can potentially receive an invalid
			 * rhf. Drop such packets.
			 */
			if (rcd->ctxt == HFI1_CTRL_CTXT) {
				u32 seq = rhf_rcv_seq(packet.rhf);

				if (++rcd->seq_cnt > 13)
					rcd->seq_cnt = 1;
				if (!last && (seq != rcd->seq_cnt))
					skip_pkt = 1;
			}

			if (needset) {
				dd_dev_info(dd,
					    "Switching to DMA_RTAIL\n");
				set_dma_rtail(dd, rcd->ctxt);
				needset = 0;
			}
		}

		process_rcv_update(last, &packet);
	}

	process_rcv_qp_work(rcd);
	rcd->head = packet.rhqoff;

bail:
	/*
	 * Always write head at end, and setup rcv interrupt, even
	 * if no packets were processed.
	 */
	finish_packet(&packet);
	return last;
}

/*
 * We may discover in the interrupt that the hardware link state has
 * changed from ARMED to ACTIVE (due to the arrival of a non-SC15 packet),
 * and we need to update the driver's notion of the link state.  We cannot
 * run set_link_state from interrupt context, so we queue this function on
 * a workqueue.
 *
 * We delay the regular interrupt processing until after the state changes
 * so that the link will be in the correct state by the time any application
 * we wake up attempts to send a reply to any message it received.
 * (Subsequent receive interrupts may possibly force the wakeup before we
 * update the link state.)
 *
 * The rcd is freed in hfi1_free_ctxtdata after hfi1_postinit_cleanup invokes
 * dd->f_cleanup(dd) to disable the interrupt handler and flush workqueues,
 * so we're safe from use-after-free of the rcd.
 */
void receive_interrupt_work(struct work_struct *work)
{
	struct hfi1_pportdata *ppd = container_of(work, struct hfi1_pportdata,
						  linkstate_active_work);
	struct hfi1_devdata *dd = ppd->dd;
	int i;

	/* Received non-SC15 packet implies neighbor_normal */
	ppd->neighbor_normal = 1;
	set_link_state(ppd, HLS_UP_ACTIVE);

	/*
	 * Interrupt all statically allocated kernel contexts that could
	 * have had an interrupt during auto activation.
	 */
	for (i = HFI1_CTRL_CTXT; i < dd->first_dyn_alloc_ctxt; i++)
		force_recv_intr(dd->rcd[i]);
}

/*
 * Convert a given MTU size to the on-wire MAD packet enumeration.
 * Return -1 if the size is invalid.
 */
int mtu_to_enum(u32 mtu, int default_if_bad)
{
	switch (mtu) {
	case     0: return OPA_MTU_0;
	case   256: return OPA_MTU_256;
	case   512: return OPA_MTU_512;
	case  1024: return OPA_MTU_1024;
	case  2048: return OPA_MTU_2048;
	case  4096: return OPA_MTU_4096;
	case  8192: return OPA_MTU_8192;
	case 10240: return OPA_MTU_10240;
	}
	return default_if_bad;
}

u16 enum_to_mtu(int mtu)
{
	switch (mtu) {
	case OPA_MTU_0:     return 0;
	case OPA_MTU_256:   return 256;
	case OPA_MTU_512:   return 512;
	case OPA_MTU_1024:  return 1024;
	case OPA_MTU_2048:  return 2048;
	case OPA_MTU_4096:  return 4096;
	case OPA_MTU_8192:  return 8192;
	case OPA_MTU_10240: return 10240;
	default: return 0xffff;
	}
}

/*
 * set_mtu - set the MTU
 * @ppd: the per port data
 *
 * We can handle "any" incoming size, the issue here is whether we
 * need to restrict our outgoing size.  We do not deal with what happens
 * to programs that are already running when the size changes.
 */
int set_mtu(struct hfi1_pportdata *ppd)
{
	struct hfi1_devdata *dd = ppd->dd;
	int i, drain, ret = 0, is_up = 0;

	ppd->ibmtu = 0;
	for (i = 0; i < ppd->vls_supported; i++)
		if (ppd->ibmtu < dd->vld[i].mtu)
			ppd->ibmtu = dd->vld[i].mtu;
	ppd->ibmaxlen = ppd->ibmtu + lrh_max_header_bytes(ppd->dd);

	mutex_lock(&ppd->hls_lock);
	if (ppd->host_link_state == HLS_UP_INIT ||
	    ppd->host_link_state == HLS_UP_ARMED ||
	    ppd->host_link_state == HLS_UP_ACTIVE)
		is_up = 1;

	drain = !is_ax(dd) && is_up;

	if (drain)
		/*
		 * MTU is specified per-VL. To ensure that no packet gets
		 * stuck (due, e.g., to the MTU for the packet's VL being
		 * reduced), empty the per-VL FIFOs before adjusting MTU.
		 */
		ret = stop_drain_data_vls(dd);

	if (ret) {
		dd_dev_err(dd, "%s: cannot stop/drain VLs - refusing to change per-VL MTUs\n",
			   __func__);
		goto err;
	}

	hfi1_set_ib_cfg(ppd, HFI1_IB_CFG_MTU, 0);

	if (drain)
		open_fill_data_vls(dd); /* reopen all VLs */

err:
	mutex_unlock(&ppd->hls_lock);

	return ret;
}

int hfi1_set_lid(struct hfi1_pportdata *ppd, u32 lid, u8 lmc)
{
	struct hfi1_devdata *dd = ppd->dd;

	ppd->lid = lid;
	ppd->lmc = lmc;
	hfi1_set_ib_cfg(ppd, HFI1_IB_CFG_LIDLMC, 0);

	dd_dev_info(dd, "port %u: got a lid: 0x%x\n", ppd->port, lid);

	return 0;
}

void shutdown_led_override(struct hfi1_pportdata *ppd)
{
	struct hfi1_devdata *dd = ppd->dd;

	/*
	 * This pairs with the memory barrier in hfi1_start_led_override to
	 * ensure that we read the correct state of LED beaconing represented
	 * by led_override_timer_active
	 */
	smp_rmb();
	if (atomic_read(&ppd->led_override_timer_active)) {
		del_timer_sync(&ppd->led_override_timer);
		atomic_set(&ppd->led_override_timer_active, 0);
		/* Ensure the atomic_set is visible to all CPUs */
		smp_wmb();
	}

	/* Hand control of the LED to the DC for normal operation */
	write_csr(dd, DCC_CFG_LED_CNTRL, 0);
}

static void run_led_override(unsigned long opaque)
{
	struct hfi1_pportdata *ppd = (struct hfi1_pportdata *)opaque;
	struct hfi1_devdata *dd = ppd->dd;
	unsigned long timeout;
	int phase_idx;

	if (!(dd->flags & HFI1_INITTED))
		return;

	phase_idx = ppd->led_override_phase & 1;

	setextled(dd, phase_idx);

	timeout = ppd->led_override_vals[phase_idx];

	/* Set up for next phase */
	ppd->led_override_phase = !ppd->led_override_phase;

	mod_timer(&ppd->led_override_timer, jiffies + timeout);
}

/*
 * To have the LED blink in a particular pattern, provide timeon and timeoff
 * in milliseconds.
 * To turn off custom blinking and return to normal operation, use
 * shutdown_led_override()
 */
void hfi1_start_led_override(struct hfi1_pportdata *ppd, unsigned int timeon,
			     unsigned int timeoff)
{
	if (!(ppd->dd->flags & HFI1_INITTED))
		return;

	/* Convert to jiffies for direct use in timer */
	ppd->led_override_vals[0] = msecs_to_jiffies(timeoff);
	ppd->led_override_vals[1] = msecs_to_jiffies(timeon);

	/* Arbitrarily start from LED on phase */
	ppd->led_override_phase = 1;

	/*
	 * If the timer has not already been started, do so. Use a "quick"
	 * timeout so the handler will be called soon to look at our request.
	 */
	if (!timer_pending(&ppd->led_override_timer)) {
		setup_timer(&ppd->led_override_timer, run_led_override,
			    (unsigned long)ppd);
		ppd->led_override_timer.expires = jiffies + 1;
		add_timer(&ppd->led_override_timer);
		atomic_set(&ppd->led_override_timer_active, 1);
		/* Ensure the atomic_set is visible to all CPUs */
		smp_wmb();
	}
}

/**
 * hfi1_reset_device - reset the chip if possible
 * @unit: the device to reset
 *
 * Whether or not reset is successful, we attempt to re-initialize the chip
 * (that is, much like a driver unload/reload).  We clear the INITTED flag
 * so that the various entry points will fail until we reinitialize.  For
 * now, we only allow this if no user contexts are open that use chip resources
 */
int hfi1_reset_device(int unit)
{
	int ret, i;
	struct hfi1_devdata *dd = hfi1_lookup(unit);
	struct hfi1_pportdata *ppd;
	unsigned long flags;
	int pidx;

	if (!dd) {
		ret = -ENODEV;
		goto bail;
	}

	dd_dev_info(dd, "Reset on unit %u requested\n", unit);

	if (!dd->kregbase || !(dd->flags & HFI1_PRESENT)) {
		dd_dev_info(dd,
			    "Invalid unit number %u or not initialized or not present\n",
			    unit);
		ret = -ENXIO;
		goto bail;
	}

	spin_lock_irqsave(&dd->uctxt_lock, flags);
	if (dd->rcd)
		for (i = dd->first_dyn_alloc_ctxt;
		     i < dd->num_rcv_contexts; i++) {
			if (!dd->rcd[i])
				continue;
			spin_unlock_irqrestore(&dd->uctxt_lock, flags);
			ret = -EBUSY;
			goto bail;
		}
	spin_unlock_irqrestore(&dd->uctxt_lock, flags);

	for (pidx = 0; pidx < dd->num_pports; ++pidx) {
		ppd = dd->pport + pidx;

		shutdown_led_override(ppd);
	}
	if (dd->flags & HFI1_HAS_SEND_DMA)
		sdma_exit(dd);

	hfi1_reset_cpu_counters(dd);

	ret = hfi1_init(dd, 1);

	if (ret)
		dd_dev_err(dd,
			   "Reinitialize unit %u after reset failed with %d\n",
			   unit, ret);
	else
		dd_dev_info(dd, "Reinitialized unit %u after resetting\n",
			    unit);

bail:
	return ret;
}

void handle_eflags(struct hfi1_packet *packet)
{
	struct hfi1_ctxtdata *rcd = packet->rcd;
	u32 rte = rhf_rcv_type_err(packet->rhf);

	rcv_hdrerr(rcd, rcd->ppd, packet);
	if (rhf_err_flags(packet->rhf))
		dd_dev_err(rcd->dd,
			   "receive context %d: rhf 0x%016llx, errs [ %s%s%s%s%s%s%s%s] rte 0x%x\n",
			   rcd->ctxt, packet->rhf,
			   packet->rhf & RHF_K_HDR_LEN_ERR ? "k_hdr_len " : "",
			   packet->rhf & RHF_DC_UNC_ERR ? "dc_unc " : "",
			   packet->rhf & RHF_DC_ERR ? "dc " : "",
			   packet->rhf & RHF_TID_ERR ? "tid " : "",
			   packet->rhf & RHF_LEN_ERR ? "len " : "",
			   packet->rhf & RHF_ECC_ERR ? "ecc " : "",
			   packet->rhf & RHF_VCRC_ERR ? "vcrc " : "",
			   packet->rhf & RHF_ICRC_ERR ? "icrc " : "",
			   rte);
}

/*
 * The following functions are called by the interrupt handler. They are type
 * specific handlers for each packet type.
 */
int process_receive_ib(struct hfi1_packet *packet)
{
	if (unlikely(hfi1_dbg_fault_packet(packet)))
		return RHF_RCV_CONTINUE;

	trace_hfi1_rcvhdr(packet->rcd->ppd->dd,
			  packet->rcd->ctxt,
			  rhf_err_flags(packet->rhf),
			  RHF_RCV_TYPE_IB,
			  packet->hlen,
			  packet->tlen,
			  packet->updegr,
			  rhf_egr_index(packet->rhf));

	if (unlikely(
		 (hfi1_dbg_fault_suppress_err(&packet->rcd->dd->verbs_dev) &&
		 (packet->rhf & RHF_DC_ERR))))
		return RHF_RCV_CONTINUE;

	if (unlikely(rhf_err_flags(packet->rhf))) {
		handle_eflags(packet);
		return RHF_RCV_CONTINUE;
	}

	hfi1_ib_rcv(packet);
	return RHF_RCV_CONTINUE;
}

static inline bool hfi1_is_vnic_packet(struct hfi1_packet *packet)
{
	/* Packet received in VNIC context via RSM */
	if (packet->rcd->is_vnic)
		return true;

	if ((HFI1_GET_L2_TYPE(packet->ebuf) == OPA_VNIC_L2_TYPE) &&
	    (HFI1_GET_L4_TYPE(packet->ebuf) == OPA_VNIC_L4_ETHR))
		return true;

	return false;
}

int process_receive_bypass(struct hfi1_packet *packet)
{
	struct hfi1_devdata *dd = packet->rcd->dd;

	if (unlikely(rhf_err_flags(packet->rhf))) {
		handle_eflags(packet);
	} else if (hfi1_is_vnic_packet(packet)) {
		hfi1_vnic_bypass_rcv(packet);
		return RHF_RCV_CONTINUE;
	}

	dd_dev_err(dd, "Unsupported bypass packet. Dropping\n");
	incr_cntr64(&dd->sw_rcv_bypass_packet_errors);
	if (!(dd->err_info_rcvport.status_and_code & OPA_EI_STATUS_SMASK)) {
		u64 *flits = packet->ebuf;

		if (flits && !(packet->rhf & RHF_LEN_ERR)) {
			dd->err_info_rcvport.packet_flit1 = flits[0];
			dd->err_info_rcvport.packet_flit2 =
				packet->tlen > sizeof(flits[0]) ? flits[1] : 0;
		}
		dd->err_info_rcvport.status_and_code |=
			(OPA_EI_STATUS_SMASK | BAD_L2_ERR);
	}
	return RHF_RCV_CONTINUE;
}

int process_receive_error(struct hfi1_packet *packet)
{
	/* KHdrHCRCErr -- KDETH packet with a bad HCRC */
	if (unlikely(
		 hfi1_dbg_fault_suppress_err(&packet->rcd->dd->verbs_dev) &&
		 rhf_rcv_type_err(packet->rhf) == 3))
		return RHF_RCV_CONTINUE;

	handle_eflags(packet);

	if (unlikely(rhf_err_flags(packet->rhf)))
		dd_dev_err(packet->rcd->dd,
			   "Unhandled error packet received. Dropping.\n");

	return RHF_RCV_CONTINUE;
}

int kdeth_process_expected(struct hfi1_packet *packet)
{
	if (unlikely(hfi1_dbg_fault_packet(packet)))
		return RHF_RCV_CONTINUE;
	if (unlikely(rhf_err_flags(packet->rhf)))
		handle_eflags(packet);

	dd_dev_err(packet->rcd->dd,
		   "Unhandled expected packet received. Dropping.\n");
	return RHF_RCV_CONTINUE;
}

int kdeth_process_eager(struct hfi1_packet *packet)
{
	if (unlikely(rhf_err_flags(packet->rhf)))
		handle_eflags(packet);
	if (unlikely(hfi1_dbg_fault_packet(packet)))
		return RHF_RCV_CONTINUE;

	dd_dev_err(packet->rcd->dd,
		   "Unhandled eager packet received. Dropping.\n");
	return RHF_RCV_CONTINUE;
}

int process_receive_invalid(struct hfi1_packet *packet)
{
	dd_dev_err(packet->rcd->dd, "Invalid packet type %d. Dropping\n",
		   rhf_rcv_type(packet->rhf));
	return RHF_RCV_CONTINUE;
}
