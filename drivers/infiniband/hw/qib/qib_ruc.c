/*
 * Copyright (c) 2006, 2007, 2008, 2009 QLogic Corporation. All rights reserved.
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

#include <linux/spinlock.h>
#include <rdma/ib_smi.h>

#include "qib.h"
#include "qib_mad.h"

/*
 * Switch to alternate path.
 * The QP s_lock should be held and interrupts disabled.
 */
void qib_migrate_qp(struct rvt_qp *qp)
{
	struct ib_event ev;

	qp->s_mig_state = IB_MIG_MIGRATED;
	qp->remote_ah_attr = qp->alt_ah_attr;
	qp->port_num = rdma_ah_get_port_num(&qp->alt_ah_attr);
	qp->s_pkey_index = qp->s_alt_pkey_index;

	ev.device = qp->ibqp.device;
	ev.element.qp = &qp->ibqp;
	ev.event = IB_EVENT_PATH_MIG;
	qp->ibqp.event_handler(&ev, qp->ibqp.qp_context);
}

static __be64 get_sguid(struct qib_ibport *ibp, unsigned index)
{
	if (!index) {
		struct qib_pportdata *ppd = ppd_from_ibp(ibp);

		return ppd->guid;
	}
	return ibp->guids[index - 1];
}

static int gid_ok(union ib_gid *gid, __be64 gid_prefix, __be64 id)
{
	return (gid->global.interface_id == id &&
		(gid->global.subnet_prefix == gid_prefix ||
		 gid->global.subnet_prefix == IB_DEFAULT_GID_PREFIX));
}

/*
 *
 * This should be called with the QP r_lock held.
 *
 * The s_lock will be acquired around the qib_migrate_qp() call.
 */
int qib_ruc_check_hdr(struct qib_ibport *ibp, struct ib_header *hdr,
		      int has_grh, struct rvt_qp *qp, u32 bth0)
{
	__be64 guid;
	unsigned long flags;

	if (qp->s_mig_state == IB_MIG_ARMED && (bth0 & IB_BTH_MIG_REQ)) {
		if (!has_grh) {
			if (rdma_ah_get_ah_flags(&qp->alt_ah_attr) &
			    IB_AH_GRH)
				goto err;
		} else {
			const struct ib_global_route *grh;

			if (!(rdma_ah_get_ah_flags(&qp->alt_ah_attr) &
			      IB_AH_GRH))
				goto err;
			grh = rdma_ah_read_grh(&qp->alt_ah_attr);
			guid = get_sguid(ibp, grh->sgid_index);
			if (!gid_ok(&hdr->u.l.grh.dgid,
				    ibp->rvp.gid_prefix, guid))
				goto err;
			if (!gid_ok(&hdr->u.l.grh.sgid,
			    grh->dgid.global.subnet_prefix,
			    grh->dgid.global.interface_id))
				goto err;
		}
		if (!qib_pkey_ok((u16)bth0,
				 qib_get_pkey(ibp, qp->s_alt_pkey_index))) {
			qib_bad_pkey(ibp,
				     (u16)bth0,
				     (be16_to_cpu(hdr->lrh[0]) >> 4) & 0xF,
				     0, qp->ibqp.qp_num,
				     hdr->lrh[3], hdr->lrh[1]);
			goto err;
		}
		/* Validate the SLID. See Ch. 9.6.1.5 and 17.2.8 */
		if ((be16_to_cpu(hdr->lrh[3]) !=
		     rdma_ah_get_dlid(&qp->alt_ah_attr)) ||
		    ppd_from_ibp(ibp)->port !=
			    rdma_ah_get_port_num(&qp->alt_ah_attr))
			goto err;
		spin_lock_irqsave(&qp->s_lock, flags);
		qib_migrate_qp(qp);
		spin_unlock_irqrestore(&qp->s_lock, flags);
	} else {
		if (!has_grh) {
			if (rdma_ah_get_ah_flags(&qp->remote_ah_attr) &
			    IB_AH_GRH)
				goto err;
		} else {
			const struct ib_global_route *grh;

			if (!(rdma_ah_get_ah_flags(&qp->remote_ah_attr) &
			      IB_AH_GRH))
				goto err;
			grh = rdma_ah_read_grh(&qp->remote_ah_attr);
			guid = get_sguid(ibp, grh->sgid_index);
			if (!gid_ok(&hdr->u.l.grh.dgid,
				    ibp->rvp.gid_prefix, guid))
				goto err;
			if (!gid_ok(&hdr->u.l.grh.sgid,
			    grh->dgid.global.subnet_prefix,
			    grh->dgid.global.interface_id))
				goto err;
		}
		if (!qib_pkey_ok((u16)bth0,
				 qib_get_pkey(ibp, qp->s_pkey_index))) {
			qib_bad_pkey(ibp,
				     (u16)bth0,
				     (be16_to_cpu(hdr->lrh[0]) >> 4) & 0xF,
				     0, qp->ibqp.qp_num,
				     hdr->lrh[3], hdr->lrh[1]);
			goto err;
		}
		/* Validate the SLID. See Ch. 9.6.1.5 */
		if (be16_to_cpu(hdr->lrh[3]) !=
		    rdma_ah_get_dlid(&qp->remote_ah_attr) ||
		    ppd_from_ibp(ibp)->port != qp->port_num)
			goto err;
		if (qp->s_mig_state == IB_MIG_REARM &&
		    !(bth0 & IB_BTH_MIG_REQ))
			qp->s_mig_state = IB_MIG_ARMED;
	}

	return 0;

err:
	return 1;
}

/**
 * qib_make_grh - construct a GRH header
 * @ibp: a pointer to the IB port
 * @hdr: a pointer to the GRH header being constructed
 * @grh: the global route address to send to
 * @hwords: the number of 32 bit words of header being sent
 * @nwords: the number of 32 bit words of data being sent
 *
 * Return the size of the header in 32 bit words.
 */
u32 qib_make_grh(struct qib_ibport *ibp, struct ib_grh *hdr,
		 const struct ib_global_route *grh, u32 hwords, u32 nwords)
{
	hdr->version_tclass_flow =
		cpu_to_be32((IB_GRH_VERSION << IB_GRH_VERSION_SHIFT) |
			    (grh->traffic_class << IB_GRH_TCLASS_SHIFT) |
			    (grh->flow_label << IB_GRH_FLOW_SHIFT));
	hdr->paylen = cpu_to_be16((hwords - 2 + nwords + SIZE_OF_CRC) << 2);
	/* next_hdr is defined by C8-7 in ch. 8.4.1 */
	hdr->next_hdr = IB_GRH_NEXT_HDR;
	hdr->hop_limit = grh->hop_limit;
	/* The SGID is 32-bit aligned. */
	hdr->sgid.global.subnet_prefix = ibp->rvp.gid_prefix;
	if (!grh->sgid_index)
		hdr->sgid.global.interface_id = ppd_from_ibp(ibp)->guid;
	else if (grh->sgid_index < QIB_GUIDS_PER_PORT)
		hdr->sgid.global.interface_id = ibp->guids[grh->sgid_index - 1];
	hdr->dgid = grh->dgid;

	/* GRH header size in 32-bit words. */
	return sizeof(struct ib_grh) / sizeof(u32);
}

void qib_make_ruc_header(struct rvt_qp *qp, struct ib_other_headers *ohdr,
			 u32 bth0, u32 bth2)
{
	struct qib_qp_priv *priv = qp->priv;
	struct qib_ibport *ibp = to_iport(qp->ibqp.device, qp->port_num);
	u16 lrh0;
	u32 nwords;
	u32 extra_bytes;

	/* Construct the header. */
	extra_bytes = -qp->s_cur_size & 3;
	nwords = (qp->s_cur_size + extra_bytes) >> 2;
	lrh0 = QIB_LRH_BTH;
	if (unlikely(rdma_ah_get_ah_flags(&qp->remote_ah_attr) & IB_AH_GRH)) {
		qp->s_hdrwords +=
			qib_make_grh(ibp, &priv->s_hdr->u.l.grh,
				     rdma_ah_read_grh(&qp->remote_ah_attr),
				     qp->s_hdrwords, nwords);
		lrh0 = QIB_LRH_GRH;
	}
	lrh0 |= ibp->sl_to_vl[rdma_ah_get_sl(&qp->remote_ah_attr)] << 12 |
		rdma_ah_get_sl(&qp->remote_ah_attr) << 4;
	priv->s_hdr->lrh[0] = cpu_to_be16(lrh0);
	priv->s_hdr->lrh[1] =
			cpu_to_be16(rdma_ah_get_dlid(&qp->remote_ah_attr));
	priv->s_hdr->lrh[2] =
			cpu_to_be16(qp->s_hdrwords + nwords + SIZE_OF_CRC);
	priv->s_hdr->lrh[3] =
		cpu_to_be16(ppd_from_ibp(ibp)->lid |
			    rdma_ah_get_path_bits(&qp->remote_ah_attr));
	bth0 |= qib_get_pkey(ibp, qp->s_pkey_index);
	bth0 |= extra_bytes << 20;
	if (qp->s_mig_state == IB_MIG_MIGRATED)
		bth0 |= IB_BTH_MIG_REQ;
	ohdr->bth[0] = cpu_to_be32(bth0);
	ohdr->bth[1] = cpu_to_be32(qp->remote_qpn);
	ohdr->bth[2] = cpu_to_be32(bth2);
	this_cpu_inc(ibp->pmastats->n_unicast_xmit);
}

void _qib_do_send(struct work_struct *work)
{
	struct qib_qp_priv *priv = container_of(work, struct qib_qp_priv,
						s_work);
	struct rvt_qp *qp = priv->owner;

	qib_do_send(qp);
}

/**
 * qib_do_send - perform a send on a QP
 * @qp: pointer to the QP
 *
 * Process entries in the send work queue until credit or queue is
 * exhausted.  Only allow one CPU to send a packet per QP (tasklet).
 * Otherwise, two threads could send packets out of order.
 */
void qib_do_send(struct rvt_qp *qp)
{
	struct qib_qp_priv *priv = qp->priv;
	struct qib_ibport *ibp = to_iport(qp->ibqp.device, qp->port_num);
	struct qib_pportdata *ppd = ppd_from_ibp(ibp);
	int (*make_req)(struct rvt_qp *qp, unsigned long *flags);
	unsigned long flags;

	if ((qp->ibqp.qp_type == IB_QPT_RC ||
	     qp->ibqp.qp_type == IB_QPT_UC) &&
	    (rdma_ah_get_dlid(&qp->remote_ah_attr) &
	     ~((1 << ppd->lmc) - 1)) == ppd->lid) {
		rvt_ruc_loopback(qp);
		return;
	}

	if (qp->ibqp.qp_type == IB_QPT_RC)
		make_req = qib_make_rc_req;
	else if (qp->ibqp.qp_type == IB_QPT_UC)
		make_req = qib_make_uc_req;
	else
		make_req = qib_make_ud_req;

	spin_lock_irqsave(&qp->s_lock, flags);

	/* Return if we are already busy processing a work request. */
	if (!qib_send_ok(qp)) {
		spin_unlock_irqrestore(&qp->s_lock, flags);
		return;
	}

	qp->s_flags |= RVT_S_BUSY;

	do {
		/* Check for a constructed packet to be sent. */
		if (qp->s_hdrwords != 0) {
			spin_unlock_irqrestore(&qp->s_lock, flags);
			/*
			 * If the packet cannot be sent now, return and
			 * the send tasklet will be woken up later.
			 */
			if (qib_verbs_send(qp, priv->s_hdr, qp->s_hdrwords,
					   qp->s_cur_sge, qp->s_cur_size))
				return;
			/* Record that s_hdr is empty. */
			qp->s_hdrwords = 0;
			spin_lock_irqsave(&qp->s_lock, flags);
		}
	} while (make_req(qp, &flags));

	spin_unlock_irqrestore(&qp->s_lock, flags);
}
