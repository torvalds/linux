// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause
/*
 * Copyright(c) 2015-2020 Intel Corporation.
 * Copyright(c) 2021 Cornelis Networks.
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
#include <linux/etherdevice.h>

#include "hfi.h"
#include "trace.h"
#include "qp.h"
#include "sdma.h"
#include "debugfs.h"
#include "vnic.h"
#include "fault.h"

#include "ipoib.h"
#include "netdev.h"

#undef pr_fmt
#define pr_fmt(fmt) DRIVER_NAME ": " fmt

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
MODULE_DESCRIPTION("Cornelis Omni-Path Express driver");

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

	return sysfs_emit(buffer, "0x%lx\n", cap_mask);
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
	unsigned long index, flags;
	int pidx, nunits_active = 0;

	xa_lock_irqsave(&hfi1_dev_table, flags);
	xa_for_each(&hfi1_dev_table, index, dd) {
		if (!(dd->flags & HFI1_PRESENT) || !dd->kregbase1)
			continue;
		for (pidx = 0; pidx < dd->num_pports; ++pidx) {
			ppd = dd->pport + pidx;
			if (ppd->lid && ppd->linkup) {
				nunits_active++;
				break;
			}
		}
	}
	xa_unlock_irqrestore(&hfi1_dev_table, flags);
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

static inline void *hfi1_get_header(struct hfi1_ctxtdata *rcd,
				    __le32 *rhf_addr)
{
	u32 offset = rhf_hdrq_offset(rhf_to_cpu(rhf_addr));

	return (void *)(rhf_addr - rcd->rhf_offset + offset);
}

static inline struct ib_header *hfi1_get_msgheader(struct hfi1_ctxtdata *rcd,
						   __le32 *rhf_addr)
{
	return (struct ib_header *)hfi1_get_header(rcd, rhf_addr);
}

static inline struct hfi1_16b_header
		*hfi1_get_16B_header(struct hfi1_ctxtdata *rcd,
				     __le32 *rhf_addr)
{
	return (struct hfi1_16b_header *)hfi1_get_header(rcd, rhf_addr);
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
	u32 mlid_base;
	struct hfi1_ibport *ibp = rcd_to_iport(rcd);
	struct hfi1_devdata *dd = ppd->dd;
	struct hfi1_ibdev *verbs_dev = &dd->verbs_dev;
	struct rvt_dev_info *rdi = &verbs_dev->rdi;

	if ((packet->rhf & RHF_DC_ERR) &&
	    hfi1_dbg_fault_suppress_err(verbs_dev))
		return;

	if (packet->rhf & RHF_ICRC_ERR)
		return;

	if (packet->etype == RHF_RCV_TYPE_BYPASS) {
		goto drop;
	} else {
		u8 lnh = ib_get_lnh(rhdr);

		mlid_base = be16_to_cpu(IB_MULTICAST_LID_BASE);
		if (lnh == HFI1_LRH_BTH) {
			packet->ohdr = &rhdr->u.oth;
		} else if (lnh == HFI1_LRH_GRH) {
			packet->ohdr = &rhdr->u.l.oth;
			packet->grh = &rhdr->u.l.grh;
		} else {
			goto drop;
		}
	}

	if (packet->rhf & RHF_TID_ERR) {
		/* For TIDERR and RC QPs preemptively schedule a NAK */
		u32 tlen = rhf_pkt_len(packet->rhf); /* in bytes */
		u32 dlid = ib_get_dlid(rhdr);
		u32 qp_num;

		/* Sanity check packet */
		if (tlen < 24)
			goto drop;

		/* Check for GRH */
		if (packet->grh) {
			u32 vtf;
			struct ib_grh *grh = packet->grh;

			if (grh->next_hdr != IB_GRH_NEXT_HDR)
				goto drop;
			vtf = be32_to_cpu(grh->version_tclass_flow);
			if ((vtf >> IB_GRH_VERSION_SHIFT) != IB_GRH_VERSION)
				goto drop;
		}

		/* Get the destination QP number. */
		qp_num = ib_bth_get_qpn(packet->ohdr);
		if (dlid < mlid_base) {
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
				hfi1_rc_hdrerr(rcd, packet, qp);
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
		void *ebuf = NULL;
		u8 opcode;

		if (rhf_use_egr_bfr(packet->rhf))
			ebuf = packet->ebuf;

		if (!ebuf)
			goto drop; /* this should never happen */

		opcode = ib_bth_get_opcode(packet->ohdr);
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

			lqpn = ib_bth_get_qpn(packet->ohdr);
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
				rcu_read_unlock();
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
	packet->rsize = get_hdrqentsize(rcd); /* words */
	packet->maxcnt = get_hdrq_cnt(rcd) * packet->rsize; /* words */
	packet->rcd = rcd;
	packet->updegr = 0;
	packet->etail = -1;
	packet->rhf_addr = get_rhf_addr(rcd);
	packet->rhf = rhf_to_cpu(packet->rhf_addr);
	packet->rhqoff = hfi1_rcd_head(rcd);
	packet->numpkt = 0;
}

/* We support only two types - 9B and 16B for now */
static const hfi1_handle_cnp hfi1_handle_cnp_tbl[2] = {
	[HFI1_PKT_TYPE_9B] = &return_cnp,
	[HFI1_PKT_TYPE_16B] = &return_cnp_16B
};

/**
 * hfi1_process_ecn_slowpath - Process FECN or BECN bits
 * @qp: The packet's destination QP
 * @pkt: The packet itself.
 * @prescan: Is the caller the RXQ prescan
 *
 * Process the packet's FECN or BECN bits. By now, the packet
 * has already been evaluated whether processing of those bit should
 * be done.
 * The significance of the @prescan argument is that if the caller
 * is the RXQ prescan, a CNP will be send out instead of waiting for the
 * normal packet processing to send an ACK with BECN set (or a CNP).
 */
bool hfi1_process_ecn_slowpath(struct rvt_qp *qp, struct hfi1_packet *pkt,
			       bool prescan)
{
	struct hfi1_ibport *ibp = to_iport(qp->ibqp.device, qp->port_num);
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	struct ib_other_headers *ohdr = pkt->ohdr;
	struct ib_grh *grh = pkt->grh;
	u32 rqpn = 0;
	u16 pkey;
	u32 rlid, slid, dlid = 0;
	u8 hdr_type, sc, svc_type, opcode;
	bool is_mcast = false, ignore_fecn = false, do_cnp = false,
		fecn, becn;

	/* can be called from prescan */
	if (pkt->etype == RHF_RCV_TYPE_BYPASS) {
		pkey = hfi1_16B_get_pkey(pkt->hdr);
		sc = hfi1_16B_get_sc(pkt->hdr);
		dlid = hfi1_16B_get_dlid(pkt->hdr);
		slid = hfi1_16B_get_slid(pkt->hdr);
		is_mcast = hfi1_is_16B_mcast(dlid);
		opcode = ib_bth_get_opcode(ohdr);
		hdr_type = HFI1_PKT_TYPE_16B;
		fecn = hfi1_16B_get_fecn(pkt->hdr);
		becn = hfi1_16B_get_becn(pkt->hdr);
	} else {
		pkey = ib_bth_get_pkey(ohdr);
		sc = hfi1_9B_get_sc5(pkt->hdr, pkt->rhf);
		dlid = qp->ibqp.qp_type != IB_QPT_UD ? ib_get_dlid(pkt->hdr) :
			ppd->lid;
		slid = ib_get_slid(pkt->hdr);
		is_mcast = (dlid > be16_to_cpu(IB_MULTICAST_LID_BASE)) &&
			   (dlid != be16_to_cpu(IB_LID_PERMISSIVE));
		opcode = ib_bth_get_opcode(ohdr);
		hdr_type = HFI1_PKT_TYPE_9B;
		fecn = ib_bth_get_fecn(ohdr);
		becn = ib_bth_get_becn(ohdr);
	}

	switch (qp->ibqp.qp_type) {
	case IB_QPT_UD:
		rlid = slid;
		rqpn = ib_get_sqpn(pkt->ohdr);
		svc_type = IB_CC_SVCTYPE_UD;
		break;
	case IB_QPT_SMI:
	case IB_QPT_GSI:
		rlid = slid;
		rqpn = ib_get_sqpn(pkt->ohdr);
		svc_type = IB_CC_SVCTYPE_UD;
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
		return false;
	}

	ignore_fecn = is_mcast || (opcode == IB_OPCODE_CNP) ||
		(opcode == IB_OPCODE_RC_ACKNOWLEDGE);
	/*
	 * ACKNOWLEDGE packets do not get a CNP but this will be
	 * guarded by ignore_fecn above.
	 */
	do_cnp = prescan ||
		(opcode >= IB_OPCODE_RC_RDMA_READ_RESPONSE_FIRST &&
		 opcode <= IB_OPCODE_RC_ATOMIC_ACKNOWLEDGE) ||
		opcode == TID_OP(READ_RESP) ||
		opcode == TID_OP(ACK);

	/* Call appropriate CNP handler */
	if (!ignore_fecn && do_cnp && fecn)
		hfi1_handle_cnp_tbl[hdr_type](ibp, qp, rqpn, pkey,
					      dlid, rlid, sc, grh);

	if (becn) {
		u32 lqpn = be32_to_cpu(ohdr->bth[1]) & RVT_QPN_MASK;
		u8 sl = ibp->sc_to_sl[sc];

		process_becn(ppd, sl, rlid, lqpn, rqpn, svc_type);
	}
	return !ignore_fecn && fecn;
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

	if (get_dma_rtail_setting(rcd)) {
		mdata->ps_tail = get_rcvhdrtail(rcd);
		if (rcd->ctxt == HFI1_CTRL_CTXT)
			mdata->ps_seq = hfi1_seq_cnt(rcd);
		else
			mdata->ps_seq = 0; /* not used with DMA_RTAIL */
	} else {
		mdata->ps_tail = 0; /* used only with DMA_RTAIL*/
		mdata->ps_seq = hfi1_seq_cnt(rcd);
	}
}

static inline int ps_done(struct ps_mdata *mdata, u64 rhf,
			  struct hfi1_ctxtdata *rcd)
{
	if (get_dma_rtail_setting(rcd))
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
	if (!get_dma_rtail_setting(rcd) ||
	    rcd->ctxt == HFI1_CTRL_CTXT)
		mdata->ps_seq = hfi1_seq_incr_wrap(mdata->ps_seq);
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
		struct hfi1_ibport *ibp = rcd_to_iport(rcd);
		__le32 *rhf_addr = (__le32 *)rcd->rcvhdrq + mdata.ps_head +
					 packet->rcd->rhf_offset;
		struct rvt_qp *qp;
		struct ib_header *hdr;
		struct rvt_dev_info *rdi = &rcd->dd->verbs_dev.rdi;
		u64 rhf = rhf_to_cpu(rhf_addr);
		u32 etype = rhf_rcv_type(rhf), qpn, bth1;
		u8 lnh;

		if (ps_done(&mdata, rhf, rcd))
			break;

		if (ps_skip(&mdata, rhf, rcd))
			goto next;

		if (etype != RHF_RCV_TYPE_IB)
			goto next;

		packet->hdr = hfi1_get_msgheader(packet->rcd, rhf_addr);
		hdr = packet->hdr;
		lnh = ib_get_lnh(hdr);

		if (lnh == HFI1_LRH_BTH) {
			packet->ohdr = &hdr->u.oth;
			packet->grh = NULL;
		} else if (lnh == HFI1_LRH_GRH) {
			packet->ohdr = &hdr->u.l.oth;
			packet->grh = &hdr->u.l.grh;
		} else {
			goto next; /* just in case */
		}

		if (!hfi1_may_ecn(packet))
			goto next;

		bth1 = be32_to_cpu(packet->ohdr->bth[1]);
		qpn = bth1 & RVT_QPN_MASK;
		rcu_read_lock();
		qp = rvt_lookup_qpn(rdi, &ibp->rvp, qpn);

		if (!qp) {
			rcu_read_unlock();
			goto next;
		}

		hfi1_process_ecn_slowpath(qp, packet, true);
		rcu_read_unlock();

		/* turn off BECN, FECN */
		bth1 &= ~(IB_FECN_SMASK | IB_BECN_SMASK);
		packet->ohdr->bth[1] = cpu_to_be32(bth1);
next:
		update_ps_mdata(&mdata, rcd);
	}
}

static void process_rcv_qp_work(struct hfi1_packet *packet)
{
	struct rvt_qp *qp, *nqp;
	struct hfi1_ctxtdata *rcd = packet->rcd;

	/*
	 * Iterate over all QPs waiting to respond.
	 * The list won't change since the IRQ is only run on one CPU.
	 */
	list_for_each_entry_safe(qp, nqp, &rcd->qp_wait_list, rspwait) {
		list_del_init(&qp->rspwait);
		if (qp->r_flags & RVT_R_RSP_NAK) {
			qp->r_flags &= ~RVT_R_RSP_NAK;
			packet->qp = qp;
			hfi1_send_rc_ack(packet, 0);
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
			process_rcv_qp_work(packet);
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

	packet->rcd->dd->ctx0_seq_drop++;
	/* Set up for the next packet */
	packet->rhqoff += packet->rsize;
	if (packet->rhqoff >= packet->maxcnt)
		packet->rhqoff = 0;

	packet->numpkt++;
	ret = check_max_packet(packet, thread);

	packet->rhf_addr = (__le32 *)packet->rcd->rcvhdrq + packet->rhqoff +
				     packet->rcd->rhf_offset;
	packet->rhf = rhf_to_cpu(packet->rhf_addr);

	return ret;
}

static void process_rcv_packet_napi(struct hfi1_packet *packet)
{
	packet->etype = rhf_rcv_type(packet->rhf);

	/* total length */
	packet->tlen = rhf_pkt_len(packet->rhf); /* in bytes */
	/* retrieve eager buffer details */
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

	packet->rcd->rhf_rcv_function_map[packet->etype](packet);
	packet->numpkt++;

	/* Set up for the next packet */
	packet->rhqoff += packet->rsize;
	if (packet->rhqoff >= packet->maxcnt)
		packet->rhqoff = 0;

	packet->rhf_addr = (__le32 *)packet->rcd->rcvhdrq + packet->rhqoff +
				      packet->rcd->rhf_offset;
	packet->rhf = rhf_to_cpu(packet->rhf_addr);
}

static inline int process_rcv_packet(struct hfi1_packet *packet, int thread)
{
	int ret;

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
			       packet->tlen - ((get_hdrqentsize(packet->rcd) -
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
	packet->rcd->rhf_rcv_function_map[packet->etype](packet);
	packet->numpkt++;

	/* Set up for the next packet */
	packet->rhqoff += packet->rsize;
	if (packet->rhqoff >= packet->maxcnt)
		packet->rhqoff = 0;

	ret = check_max_packet(packet, thread);

	packet->rhf_addr = (__le32 *)packet->rcd->rcvhdrq + packet->rhqoff +
				      packet->rcd->rhf_offset;
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
	packet->grh = NULL;
}

static inline void finish_packet(struct hfi1_packet *packet)
{
	/*
	 * Nothing we need to free for the packet.
	 *
	 * The only thing we need to do is a final update and call for an
	 * interrupt
	 */
	update_usrhead(packet->rcd, hfi1_rcd_head(packet->rcd), packet->updegr,
		       packet->etail, rcv_intr_dynamic, packet->numpkt);
}

/*
 * handle_receive_interrupt_napi_fp - receive a packet
 * @rcd: the context
 * @budget: polling budget
 *
 * Called from interrupt handler for receive interrupt.
 * This is the fast path interrupt handler
 * when executing napi soft irq environment.
 */
int handle_receive_interrupt_napi_fp(struct hfi1_ctxtdata *rcd, int budget)
{
	struct hfi1_packet packet;

	init_packet(rcd, &packet);
	if (last_rcv_seq(rcd, rhf_rcv_seq(packet.rhf)))
		goto bail;

	while (packet.numpkt < budget) {
		process_rcv_packet_napi(&packet);
		if (hfi1_seq_incr(rcd, rhf_rcv_seq(packet.rhf)))
			break;

		process_rcv_update(0, &packet);
	}
	hfi1_set_rcd_head(rcd, packet.rhqoff);
bail:
	finish_packet(&packet);
	return packet.numpkt;
}

/*
 * Handle receive interrupts when using the no dma rtail option.
 */
int handle_receive_interrupt_nodma_rtail(struct hfi1_ctxtdata *rcd, int thread)
{
	int last = RCV_PKT_OK;
	struct hfi1_packet packet;

	init_packet(rcd, &packet);
	if (last_rcv_seq(rcd, rhf_rcv_seq(packet.rhf))) {
		last = RCV_PKT_DONE;
		goto bail;
	}

	prescan_rxq(rcd, &packet);

	while (last == RCV_PKT_OK) {
		last = process_rcv_packet(&packet, thread);
		if (hfi1_seq_incr(rcd, rhf_rcv_seq(packet.rhf)))
			last = RCV_PKT_DONE;
		process_rcv_update(last, &packet);
	}
	process_rcv_qp_work(&packet);
	hfi1_set_rcd_head(rcd, packet.rhqoff);
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
	process_rcv_qp_work(&packet);
	hfi1_set_rcd_head(rcd, packet.rhqoff);
bail:
	finish_packet(&packet);
	return last;
}

static void set_all_fastpath(struct hfi1_devdata *dd, struct hfi1_ctxtdata *rcd)
{
	u16 i;

	/*
	 * For dynamically allocated kernel contexts (like vnic) switch
	 * interrupt handler only for that context. Otherwise, switch
	 * interrupt handler for all statically allocated kernel contexts.
	 */
	if (rcd->ctxt >= dd->first_dyn_alloc_ctxt && !rcd->is_vnic) {
		hfi1_rcd_get(rcd);
		hfi1_set_fast(rcd);
		hfi1_rcd_put(rcd);
		return;
	}

	for (i = HFI1_CTRL_CTXT + 1; i < dd->num_rcv_contexts; i++) {
		rcd = hfi1_rcd_get_by_index(dd, i);
		if (rcd && (i < dd->first_dyn_alloc_ctxt || rcd->is_vnic))
			hfi1_set_fast(rcd);
		hfi1_rcd_put(rcd);
	}
}

void set_all_slowpath(struct hfi1_devdata *dd)
{
	struct hfi1_ctxtdata *rcd;
	u16 i;

	/* HFI1_CTRL_CTXT must always use the slow path interrupt handler */
	for (i = HFI1_CTRL_CTXT + 1; i < dd->num_rcv_contexts; i++) {
		rcd = hfi1_rcd_get_by_index(dd, i);
		if (!rcd)
			continue;
		if (i < dd->first_dyn_alloc_ctxt || rcd->is_vnic)
			rcd->do_interrupt = rcd->slow_handler;

		hfi1_rcd_put(rcd);
	}
}

static bool __set_armed_to_active(struct hfi1_packet *packet)
{
	u8 etype = rhf_rcv_type(packet->rhf);
	u8 sc = SC15_PACKET;

	if (etype == RHF_RCV_TYPE_IB) {
		struct ib_header *hdr = hfi1_get_msgheader(packet->rcd,
							   packet->rhf_addr);
		sc = hfi1_9B_get_sc5(hdr, packet->rhf);
	} else if (etype == RHF_RCV_TYPE_BYPASS) {
		struct hfi1_16b_header *hdr = hfi1_get_16B_header(
						packet->rcd,
						packet->rhf_addr);
		sc = hfi1_16B_get_sc(hdr);
	}
	if (sc != SC15_PACKET) {
		int hwstate = driver_lstate(packet->rcd->ppd);
		struct work_struct *lsaw =
				&packet->rcd->ppd->linkstate_active_work;

		if (hwstate != IB_PORT_ACTIVE) {
			dd_dev_info(packet->rcd->dd,
				    "Unexpected link state %s\n",
				    opa_lstate_name(hwstate));
			return false;
		}

		queue_work(packet->rcd->ppd->link_wq, lsaw);
		return true;
	}
	return false;
}

/**
 * set_armed_to_active  - the fast path for armed to active
 * @packet: the packet structure
 *
 * Return true if packet processing needs to bail.
 */
static bool set_armed_to_active(struct hfi1_packet *packet)
{
	if (likely(packet->rcd->ppd->host_link_state != HLS_UP_ARMED))
		return false;
	return __set_armed_to_active(packet);
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

	if (!rcd->rcvhdrq)
		return RCV_PKT_OK;
	/* Control context will always use the slow path interrupt handler */
	needset = (rcd->ctxt == HFI1_CTRL_CTXT) ? 0 : 1;

	init_packet(rcd, &packet);

	if (!get_dma_rtail_setting(rcd)) {
		if (last_rcv_seq(rcd, rhf_rcv_seq(packet.rhf))) {
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
		if (rcd->ctxt == HFI1_CTRL_CTXT)
			if (last_rcv_seq(rcd, rhf_rcv_seq(packet.rhf)))
				skip_pkt = 1;
	}

	prescan_rxq(rcd, &packet);

	while (last == RCV_PKT_OK) {
		if (hfi1_need_drop(dd)) {
			/* On to the next packet */
			packet.rhqoff += packet.rsize;
			packet.rhf_addr = (__le32 *)rcd->rcvhdrq +
					  packet.rhqoff +
					  rcd->rhf_offset;
			packet.rhf = rhf_to_cpu(packet.rhf_addr);

		} else if (skip_pkt) {
			last = skip_rcv_packet(&packet, thread);
			skip_pkt = 0;
		} else {
			if (set_armed_to_active(&packet))
				goto bail;
			last = process_rcv_packet(&packet, thread);
		}

		if (!get_dma_rtail_setting(rcd)) {
			if (hfi1_seq_incr(rcd, rhf_rcv_seq(packet.rhf)))
				last = RCV_PKT_DONE;
		} else {
			if (packet.rhqoff == hdrqtail)
				last = RCV_PKT_DONE;
			/*
			 * Control context can potentially receive an invalid
			 * rhf. Drop such packets.
			 */
			if (rcd->ctxt == HFI1_CTRL_CTXT) {
				bool lseq;

				lseq = hfi1_seq_incr(rcd,
						     rhf_rcv_seq(packet.rhf));
				if (!last && lseq)
					skip_pkt = 1;
			}
		}

		if (needset) {
			needset = false;
			set_all_fastpath(dd, rcd);
		}
		process_rcv_update(last, &packet);
	}

	process_rcv_qp_work(&packet);
	hfi1_set_rcd_head(rcd, packet.rhqoff);

bail:
	/*
	 * Always write head at end, and setup rcv interrupt, even
	 * if no packets were processed.
	 */
	finish_packet(&packet);
	return last;
}

/*
 * handle_receive_interrupt_napi_sp - receive a packet
 * @rcd: the context
 * @budget: polling budget
 *
 * Called from interrupt handler for errors or receive interrupt.
 * This is the slow path interrupt handler
 * when executing napi soft irq environment.
 */
int handle_receive_interrupt_napi_sp(struct hfi1_ctxtdata *rcd, int budget)
{
	struct hfi1_devdata *dd = rcd->dd;
	int last = RCV_PKT_OK;
	bool needset = true;
	struct hfi1_packet packet;

	init_packet(rcd, &packet);
	if (last_rcv_seq(rcd, rhf_rcv_seq(packet.rhf)))
		goto bail;

	while (last != RCV_PKT_DONE && packet.numpkt < budget) {
		if (hfi1_need_drop(dd)) {
			/* On to the next packet */
			packet.rhqoff += packet.rsize;
			packet.rhf_addr = (__le32 *)rcd->rcvhdrq +
					  packet.rhqoff +
					  rcd->rhf_offset;
			packet.rhf = rhf_to_cpu(packet.rhf_addr);

		} else {
			if (set_armed_to_active(&packet))
				goto bail;
			process_rcv_packet_napi(&packet);
		}

		if (hfi1_seq_incr(rcd, rhf_rcv_seq(packet.rhf)))
			last = RCV_PKT_DONE;

		if (needset) {
			needset = false;
			set_all_fastpath(dd, rcd);
		}

		process_rcv_update(last, &packet);
	}

	hfi1_set_rcd_head(rcd, packet.rhqoff);

bail:
	/*
	 * Always write head at end, and setup rcv interrupt, even
	 * if no packets were processed.
	 */
	finish_packet(&packet);
	return packet.numpkt;
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
	struct hfi1_ctxtdata *rcd;
	u16 i;

	/* Received non-SC15 packet implies neighbor_normal */
	ppd->neighbor_normal = 1;
	set_link_state(ppd, HLS_UP_ACTIVE);

	/*
	 * Interrupt all statically allocated kernel contexts that could
	 * have had an interrupt during auto activation.
	 */
	for (i = HFI1_CTRL_CTXT; i < dd->first_dyn_alloc_ctxt; i++) {
		rcd = hfi1_rcd_get_by_index(dd, i);
		if (rcd)
			force_recv_intr(rcd);
		hfi1_rcd_put(rcd);
	}
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

static void run_led_override(struct timer_list *t)
{
	struct hfi1_pportdata *ppd = from_timer(ppd, t, led_override_timer);
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
		timer_setup(&ppd->led_override_timer, run_led_override, 0);
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
	int ret;
	struct hfi1_devdata *dd = hfi1_lookup(unit);
	struct hfi1_pportdata *ppd;
	int pidx;

	if (!dd) {
		ret = -ENODEV;
		goto bail;
	}

	dd_dev_info(dd, "Reset on unit %u requested\n", unit);

	if (!dd->kregbase1 || !(dd->flags & HFI1_PRESENT)) {
		dd_dev_info(dd,
			    "Invalid unit number %u or not initialized or not present\n",
			    unit);
		ret = -ENXIO;
		goto bail;
	}

	/* If there are any user/vnic contexts, we cannot reset */
	mutex_lock(&hfi1_mutex);
	if (dd->rcd)
		if (hfi1_stats.sps_ctxts) {
			mutex_unlock(&hfi1_mutex);
			ret = -EBUSY;
			goto bail;
		}
	mutex_unlock(&hfi1_mutex);

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

static inline void hfi1_setup_ib_header(struct hfi1_packet *packet)
{
	packet->hdr = (struct hfi1_ib_message_header *)
			hfi1_get_msgheader(packet->rcd,
					   packet->rhf_addr);
	packet->hlen = (u8 *)packet->rhf_addr - (u8 *)packet->hdr;
}

static int hfi1_bypass_ingress_pkt_check(struct hfi1_packet *packet)
{
	struct hfi1_pportdata *ppd = packet->rcd->ppd;

	/* slid and dlid cannot be 0 */
	if ((!packet->slid) || (!packet->dlid))
		return -EINVAL;

	/* Compare port lid with incoming packet dlid */
	if ((!(hfi1_is_16B_mcast(packet->dlid))) &&
	    (packet->dlid !=
		opa_get_lid(be32_to_cpu(OPA_LID_PERMISSIVE), 16B))) {
		if ((packet->dlid & ~((1 << ppd->lmc) - 1)) != ppd->lid)
			return -EINVAL;
	}

	/* No multicast packets with SC15 */
	if ((hfi1_is_16B_mcast(packet->dlid)) && (packet->sc == 0xF))
		return -EINVAL;

	/* Packets with permissive DLID always on SC15 */
	if ((packet->dlid == opa_get_lid(be32_to_cpu(OPA_LID_PERMISSIVE),
					 16B)) &&
	    (packet->sc != 0xF))
		return -EINVAL;

	return 0;
}

static int hfi1_setup_9B_packet(struct hfi1_packet *packet)
{
	struct hfi1_ibport *ibp = rcd_to_iport(packet->rcd);
	struct ib_header *hdr;
	u8 lnh;

	hfi1_setup_ib_header(packet);
	hdr = packet->hdr;

	lnh = ib_get_lnh(hdr);
	if (lnh == HFI1_LRH_BTH) {
		packet->ohdr = &hdr->u.oth;
		packet->grh = NULL;
	} else if (lnh == HFI1_LRH_GRH) {
		u32 vtf;

		packet->ohdr = &hdr->u.l.oth;
		packet->grh = &hdr->u.l.grh;
		if (packet->grh->next_hdr != IB_GRH_NEXT_HDR)
			goto drop;
		vtf = be32_to_cpu(packet->grh->version_tclass_flow);
		if ((vtf >> IB_GRH_VERSION_SHIFT) != IB_GRH_VERSION)
			goto drop;
	} else {
		goto drop;
	}

	/* Query commonly used fields from packet header */
	packet->payload = packet->ebuf;
	packet->opcode = ib_bth_get_opcode(packet->ohdr);
	packet->slid = ib_get_slid(hdr);
	packet->dlid = ib_get_dlid(hdr);
	if (unlikely((packet->dlid >= be16_to_cpu(IB_MULTICAST_LID_BASE)) &&
		     (packet->dlid != be16_to_cpu(IB_LID_PERMISSIVE))))
		packet->dlid += opa_get_mcast_base(OPA_MCAST_NR) -
				be16_to_cpu(IB_MULTICAST_LID_BASE);
	packet->sl = ib_get_sl(hdr);
	packet->sc = hfi1_9B_get_sc5(hdr, packet->rhf);
	packet->pad = ib_bth_get_pad(packet->ohdr);
	packet->extra_byte = 0;
	packet->pkey = ib_bth_get_pkey(packet->ohdr);
	packet->migrated = ib_bth_is_migration(packet->ohdr);

	return 0;
drop:
	ibp->rvp.n_pkt_drops++;
	return -EINVAL;
}

static int hfi1_setup_bypass_packet(struct hfi1_packet *packet)
{
	/*
	 * Bypass packets have a different header/payload split
	 * compared to an IB packet.
	 * Current split is set such that 16 bytes of the actual
	 * header is in the header buffer and the remining is in
	 * the eager buffer. We chose 16 since hfi1 driver only
	 * supports 16B bypass packets and we will be able to
	 * receive the entire LRH with such a split.
	 */

	struct hfi1_ctxtdata *rcd = packet->rcd;
	struct hfi1_pportdata *ppd = rcd->ppd;
	struct hfi1_ibport *ibp = &ppd->ibport_data;
	u8 l4;

	packet->hdr = (struct hfi1_16b_header *)
			hfi1_get_16B_header(packet->rcd,
					    packet->rhf_addr);
	l4 = hfi1_16B_get_l4(packet->hdr);
	if (l4 == OPA_16B_L4_IB_LOCAL) {
		packet->ohdr = packet->ebuf;
		packet->grh = NULL;
		packet->opcode = ib_bth_get_opcode(packet->ohdr);
		packet->pad = hfi1_16B_bth_get_pad(packet->ohdr);
		/* hdr_len_by_opcode already has an IB LRH factored in */
		packet->hlen = hdr_len_by_opcode[packet->opcode] +
			(LRH_16B_BYTES - LRH_9B_BYTES);
		packet->migrated = opa_bth_is_migration(packet->ohdr);
	} else if (l4 == OPA_16B_L4_IB_GLOBAL) {
		u32 vtf;
		u8 grh_len = sizeof(struct ib_grh);

		packet->ohdr = packet->ebuf + grh_len;
		packet->grh = packet->ebuf;
		packet->opcode = ib_bth_get_opcode(packet->ohdr);
		packet->pad = hfi1_16B_bth_get_pad(packet->ohdr);
		/* hdr_len_by_opcode already has an IB LRH factored in */
		packet->hlen = hdr_len_by_opcode[packet->opcode] +
			(LRH_16B_BYTES - LRH_9B_BYTES) + grh_len;
		packet->migrated = opa_bth_is_migration(packet->ohdr);

		if (packet->grh->next_hdr != IB_GRH_NEXT_HDR)
			goto drop;
		vtf = be32_to_cpu(packet->grh->version_tclass_flow);
		if ((vtf >> IB_GRH_VERSION_SHIFT) != IB_GRH_VERSION)
			goto drop;
	} else if (l4 == OPA_16B_L4_FM) {
		packet->mgmt = packet->ebuf;
		packet->ohdr = NULL;
		packet->grh = NULL;
		packet->opcode = IB_OPCODE_UD_SEND_ONLY;
		packet->pad = OPA_16B_L4_FM_PAD;
		packet->hlen = OPA_16B_L4_FM_HLEN;
		packet->migrated = false;
	} else {
		goto drop;
	}

	/* Query commonly used fields from packet header */
	packet->payload = packet->ebuf + packet->hlen - LRH_16B_BYTES;
	packet->slid = hfi1_16B_get_slid(packet->hdr);
	packet->dlid = hfi1_16B_get_dlid(packet->hdr);
	if (unlikely(hfi1_is_16B_mcast(packet->dlid)))
		packet->dlid += opa_get_mcast_base(OPA_MCAST_NR) -
				opa_get_lid(opa_get_mcast_base(OPA_MCAST_NR),
					    16B);
	packet->sc = hfi1_16B_get_sc(packet->hdr);
	packet->sl = ibp->sc_to_sl[packet->sc];
	packet->extra_byte = SIZE_OF_LT;
	packet->pkey = hfi1_16B_get_pkey(packet->hdr);

	if (hfi1_bypass_ingress_pkt_check(packet))
		goto drop;

	return 0;
drop:
	hfi1_cdbg(PKT, "%s: packet dropped", __func__);
	ibp->rvp.n_pkt_drops++;
	return -EINVAL;
}

static void show_eflags_errs(struct hfi1_packet *packet)
{
	struct hfi1_ctxtdata *rcd = packet->rcd;
	u32 rte = rhf_rcv_type_err(packet->rhf);

	dd_dev_err(rcd->dd,
		   "receive context %d: rhf 0x%016llx, errs [ %s%s%s%s%s%s%s] rte 0x%x\n",
		   rcd->ctxt, packet->rhf,
		   packet->rhf & RHF_K_HDR_LEN_ERR ? "k_hdr_len " : "",
		   packet->rhf & RHF_DC_UNC_ERR ? "dc_unc " : "",
		   packet->rhf & RHF_DC_ERR ? "dc " : "",
		   packet->rhf & RHF_TID_ERR ? "tid " : "",
		   packet->rhf & RHF_LEN_ERR ? "len " : "",
		   packet->rhf & RHF_ECC_ERR ? "ecc " : "",
		   packet->rhf & RHF_ICRC_ERR ? "icrc " : "",
		   rte);
}

void handle_eflags(struct hfi1_packet *packet)
{
	struct hfi1_ctxtdata *rcd = packet->rcd;

	rcv_hdrerr(rcd, rcd->ppd, packet);
	if (rhf_err_flags(packet->rhf))
		show_eflags_errs(packet);
}

static void hfi1_ipoib_ib_rcv(struct hfi1_packet *packet)
{
	struct hfi1_ibport *ibp;
	struct net_device *netdev;
	struct hfi1_ctxtdata *rcd = packet->rcd;
	struct napi_struct *napi = rcd->napi;
	struct sk_buff *skb;
	struct hfi1_netdev_rxq *rxq = container_of(napi,
			struct hfi1_netdev_rxq, napi);
	u32 extra_bytes;
	u32 tlen, qpnum;
	bool do_work, do_cnp;

	trace_hfi1_rcvhdr(packet);

	hfi1_setup_ib_header(packet);

	packet->ohdr = &((struct ib_header *)packet->hdr)->u.oth;
	packet->grh = NULL;

	if (unlikely(rhf_err_flags(packet->rhf))) {
		handle_eflags(packet);
		return;
	}

	qpnum = ib_bth_get_qpn(packet->ohdr);
	netdev = hfi1_netdev_get_data(rcd->dd, qpnum);
	if (!netdev)
		goto drop_no_nd;

	trace_input_ibhdr(rcd->dd, packet, !!(rhf_dc_info(packet->rhf)));
	trace_ctxt_rsm_hist(rcd->ctxt);

	/* handle congestion notifications */
	do_work = hfi1_may_ecn(packet);
	if (unlikely(do_work)) {
		do_cnp = (packet->opcode != IB_OPCODE_CNP);
		(void)hfi1_process_ecn_slowpath(hfi1_ipoib_priv(netdev)->qp,
						 packet, do_cnp);
	}

	/*
	 * We have split point after last byte of DETH
	 * lets strip padding and CRC and ICRC.
	 * tlen is whole packet len so we need to
	 * subtract header size as well.
	 */
	tlen = packet->tlen;
	extra_bytes = ib_bth_get_pad(packet->ohdr) + (SIZE_OF_CRC << 2) +
			packet->hlen;
	if (unlikely(tlen < extra_bytes))
		goto drop;

	tlen -= extra_bytes;

	skb = hfi1_ipoib_prepare_skb(rxq, tlen, packet->ebuf);
	if (unlikely(!skb))
		goto drop;

	dev_sw_netstats_rx_add(netdev, skb->len);

	skb->dev = netdev;
	skb->pkt_type = PACKET_HOST;
	netif_receive_skb(skb);

	return;

drop:
	++netdev->stats.rx_dropped;
drop_no_nd:
	ibp = rcd_to_iport(packet->rcd);
	++ibp->rvp.n_pkt_drops;
}

/*
 * The following functions are called by the interrupt handler. They are type
 * specific handlers for each packet type.
 */
static void process_receive_ib(struct hfi1_packet *packet)
{
	if (hfi1_setup_9B_packet(packet))
		return;

	if (unlikely(hfi1_dbg_should_fault_rx(packet)))
		return;

	trace_hfi1_rcvhdr(packet);

	if (unlikely(rhf_err_flags(packet->rhf))) {
		handle_eflags(packet);
		return;
	}

	hfi1_ib_rcv(packet);
}

static void process_receive_bypass(struct hfi1_packet *packet)
{
	struct hfi1_devdata *dd = packet->rcd->dd;

	if (hfi1_setup_bypass_packet(packet))
		return;

	trace_hfi1_rcvhdr(packet);

	if (unlikely(rhf_err_flags(packet->rhf))) {
		handle_eflags(packet);
		return;
	}

	if (hfi1_16B_get_l2(packet->hdr) == 0x2) {
		hfi1_16B_rcv(packet);
	} else {
		dd_dev_err(dd,
			   "Bypass packets other than 16B are not supported in normal operation. Dropping\n");
		incr_cntr64(&dd->sw_rcv_bypass_packet_errors);
		if (!(dd->err_info_rcvport.status_and_code &
		      OPA_EI_STATUS_SMASK)) {
			u64 *flits = packet->ebuf;

			if (flits && !(packet->rhf & RHF_LEN_ERR)) {
				dd->err_info_rcvport.packet_flit1 = flits[0];
				dd->err_info_rcvport.packet_flit2 =
					packet->tlen > sizeof(flits[0]) ?
					flits[1] : 0;
			}
			dd->err_info_rcvport.status_and_code |=
				(OPA_EI_STATUS_SMASK | BAD_L2_ERR);
		}
	}
}

static void process_receive_error(struct hfi1_packet *packet)
{
	/* KHdrHCRCErr -- KDETH packet with a bad HCRC */
	if (unlikely(
		 hfi1_dbg_fault_suppress_err(&packet->rcd->dd->verbs_dev) &&
		 (rhf_rcv_type_err(packet->rhf) == RHF_RCV_TYPE_ERROR ||
		  packet->rhf & RHF_DC_ERR)))
		return;

	hfi1_setup_ib_header(packet);
	handle_eflags(packet);

	if (unlikely(rhf_err_flags(packet->rhf)))
		dd_dev_err(packet->rcd->dd,
			   "Unhandled error packet received. Dropping.\n");
}

static void kdeth_process_expected(struct hfi1_packet *packet)
{
	hfi1_setup_9B_packet(packet);
	if (unlikely(hfi1_dbg_should_fault_rx(packet)))
		return;

	if (unlikely(rhf_err_flags(packet->rhf))) {
		struct hfi1_ctxtdata *rcd = packet->rcd;

		if (hfi1_handle_kdeth_eflags(rcd, rcd->ppd, packet))
			return;
	}

	hfi1_kdeth_expected_rcv(packet);
}

static void kdeth_process_eager(struct hfi1_packet *packet)
{
	hfi1_setup_9B_packet(packet);
	if (unlikely(hfi1_dbg_should_fault_rx(packet)))
		return;

	trace_hfi1_rcvhdr(packet);
	if (unlikely(rhf_err_flags(packet->rhf))) {
		struct hfi1_ctxtdata *rcd = packet->rcd;

		show_eflags_errs(packet);
		if (hfi1_handle_kdeth_eflags(rcd, rcd->ppd, packet))
			return;
	}

	hfi1_kdeth_eager_rcv(packet);
}

static void process_receive_invalid(struct hfi1_packet *packet)
{
	dd_dev_err(packet->rcd->dd, "Invalid packet type %d. Dropping\n",
		   rhf_rcv_type(packet->rhf));
}

#define HFI1_RCVHDR_DUMP_MAX	5

void seqfile_dump_rcd(struct seq_file *s, struct hfi1_ctxtdata *rcd)
{
	struct hfi1_packet packet;
	struct ps_mdata mdata;
	int i;

	seq_printf(s, "Rcd %u: RcvHdr cnt %u entsize %u %s ctrl 0x%08llx status 0x%08llx, head %llu tail %llu  sw head %u\n",
		   rcd->ctxt, get_hdrq_cnt(rcd), get_hdrqentsize(rcd),
		   get_dma_rtail_setting(rcd) ?
		   "dma_rtail" : "nodma_rtail",
		   read_kctxt_csr(rcd->dd, rcd->ctxt, RCV_CTXT_CTRL),
		   read_kctxt_csr(rcd->dd, rcd->ctxt, RCV_CTXT_STATUS),
		   read_uctxt_csr(rcd->dd, rcd->ctxt, RCV_HDR_HEAD) &
		   RCV_HDR_HEAD_HEAD_MASK,
		   read_uctxt_csr(rcd->dd, rcd->ctxt, RCV_HDR_TAIL),
		   rcd->head);

	init_packet(rcd, &packet);
	init_ps_mdata(&mdata, &packet);

	for (i = 0; i < HFI1_RCVHDR_DUMP_MAX; i++) {
		__le32 *rhf_addr = (__le32 *)rcd->rcvhdrq + mdata.ps_head +
					 rcd->rhf_offset;
		struct ib_header *hdr;
		u64 rhf = rhf_to_cpu(rhf_addr);
		u32 etype = rhf_rcv_type(rhf), qpn;
		u8 opcode;
		u32 psn;
		u8 lnh;

		if (ps_done(&mdata, rhf, rcd))
			break;

		if (ps_skip(&mdata, rhf, rcd))
			goto next;

		if (etype > RHF_RCV_TYPE_IB)
			goto next;

		packet.hdr = hfi1_get_msgheader(rcd, rhf_addr);
		hdr = packet.hdr;

		lnh = be16_to_cpu(hdr->lrh[0]) & 3;

		if (lnh == HFI1_LRH_BTH)
			packet.ohdr = &hdr->u.oth;
		else if (lnh == HFI1_LRH_GRH)
			packet.ohdr = &hdr->u.l.oth;
		else
			goto next; /* just in case */

		opcode = (be32_to_cpu(packet.ohdr->bth[0]) >> 24);
		qpn = be32_to_cpu(packet.ohdr->bth[1]) & RVT_QPN_MASK;
		psn = mask_psn(be32_to_cpu(packet.ohdr->bth[2]));

		seq_printf(s, "\tEnt %u: opcode 0x%x, qpn 0x%x, psn 0x%x\n",
			   mdata.ps_head, opcode, qpn, psn);
next:
		update_ps_mdata(&mdata, rcd);
	}
}

const rhf_rcv_function_ptr normal_rhf_rcv_functions[] = {
	[RHF_RCV_TYPE_EXPECTED] = kdeth_process_expected,
	[RHF_RCV_TYPE_EAGER] = kdeth_process_eager,
	[RHF_RCV_TYPE_IB] = process_receive_ib,
	[RHF_RCV_TYPE_ERROR] = process_receive_error,
	[RHF_RCV_TYPE_BYPASS] = process_receive_bypass,
	[RHF_RCV_TYPE_INVALID5] = process_receive_invalid,
	[RHF_RCV_TYPE_INVALID6] = process_receive_invalid,
	[RHF_RCV_TYPE_INVALID7] = process_receive_invalid,
};

const rhf_rcv_function_ptr netdev_rhf_rcv_functions[] = {
	[RHF_RCV_TYPE_EXPECTED] = process_receive_invalid,
	[RHF_RCV_TYPE_EAGER] = process_receive_invalid,
	[RHF_RCV_TYPE_IB] = hfi1_ipoib_ib_rcv,
	[RHF_RCV_TYPE_ERROR] = process_receive_error,
	[RHF_RCV_TYPE_BYPASS] = hfi1_vnic_bypass_rcv,
	[RHF_RCV_TYPE_INVALID5] = process_receive_invalid,
	[RHF_RCV_TYPE_INVALID6] = process_receive_invalid,
	[RHF_RCV_TYPE_INVALID7] = process_receive_invalid,
};
