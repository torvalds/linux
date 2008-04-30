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
#include <linux/slab.h>
#include <linux/mman.h>
#include <net/sock.h>
#include "iwch_provider.h"
#include "iwch.h"
#include "iwch_cm.h"
#include "cxio_hal.h"
#include "cxio_wr.h"

static void post_qp_event(struct iwch_dev *rnicp, struct iwch_cq *chp,
			  struct respQ_msg_t *rsp_msg,
			  enum ib_event_type ib_event,
			  int send_term)
{
	struct ib_event event;
	struct iwch_qp_attributes attrs;
	struct iwch_qp *qhp;

	spin_lock(&rnicp->lock);
	qhp = get_qhp(rnicp, CQE_QPID(rsp_msg->cqe));

	if (!qhp) {
		printk(KERN_ERR "%s unaffiliated error 0x%x qpid 0x%x\n",
		       __func__, CQE_STATUS(rsp_msg->cqe),
		       CQE_QPID(rsp_msg->cqe));
		spin_unlock(&rnicp->lock);
		return;
	}

	if ((qhp->attr.state == IWCH_QP_STATE_ERROR) ||
	    (qhp->attr.state == IWCH_QP_STATE_TERMINATE)) {
		PDBG("%s AE received after RTS - "
		     "qp state %d qpid 0x%x status 0x%x\n", __func__,
		     qhp->attr.state, qhp->wq.qpid, CQE_STATUS(rsp_msg->cqe));
		spin_unlock(&rnicp->lock);
		return;
	}

	printk(KERN_ERR "%s - AE qpid 0x%x opcode %d status 0x%x "
	       "type %d wrid.hi 0x%x wrid.lo 0x%x \n", __func__,
	       CQE_QPID(rsp_msg->cqe), CQE_OPCODE(rsp_msg->cqe),
	       CQE_STATUS(rsp_msg->cqe), CQE_TYPE(rsp_msg->cqe),
	       CQE_WRID_HI(rsp_msg->cqe), CQE_WRID_LOW(rsp_msg->cqe));

	atomic_inc(&qhp->refcnt);
	spin_unlock(&rnicp->lock);

	event.event = ib_event;
	event.device = chp->ibcq.device;
	if (ib_event == IB_EVENT_CQ_ERR)
		event.element.cq = &chp->ibcq;
	else
		event.element.qp = &qhp->ibqp;

	if (qhp->ibqp.event_handler)
		(*qhp->ibqp.event_handler)(&event, qhp->ibqp.qp_context);

	if (qhp->attr.state == IWCH_QP_STATE_RTS) {
		attrs.next_state = IWCH_QP_STATE_TERMINATE;
		iwch_modify_qp(qhp->rhp, qhp, IWCH_QP_ATTR_NEXT_STATE,
			       &attrs, 1);
		if (send_term)
			iwch_post_terminate(qhp, rsp_msg);
	}

	if (atomic_dec_and_test(&qhp->refcnt))
		wake_up(&qhp->wait);
}

void iwch_ev_dispatch(struct cxio_rdev *rdev_p, struct sk_buff *skb)
{
	struct iwch_dev *rnicp;
	struct respQ_msg_t *rsp_msg = (struct respQ_msg_t *) skb->data;
	struct iwch_cq *chp;
	struct iwch_qp *qhp;
	u32 cqid = RSPQ_CQID(rsp_msg);

	rnicp = (struct iwch_dev *) rdev_p->ulp;
	spin_lock(&rnicp->lock);
	chp = get_chp(rnicp, cqid);
	qhp = get_qhp(rnicp, CQE_QPID(rsp_msg->cqe));
	if (!chp || !qhp) {
		printk(KERN_ERR MOD "BAD AE cqid 0x%x qpid 0x%x opcode %d "
		       "status 0x%x type %d wrid.hi 0x%x wrid.lo 0x%x \n",
		       cqid, CQE_QPID(rsp_msg->cqe),
		       CQE_OPCODE(rsp_msg->cqe), CQE_STATUS(rsp_msg->cqe),
		       CQE_TYPE(rsp_msg->cqe), CQE_WRID_HI(rsp_msg->cqe),
		       CQE_WRID_LOW(rsp_msg->cqe));
		spin_unlock(&rnicp->lock);
		goto out;
	}
	iwch_qp_add_ref(&qhp->ibqp);
	atomic_inc(&chp->refcnt);
	spin_unlock(&rnicp->lock);

	/*
	 * 1) completion of our sending a TERMINATE.
	 * 2) incoming TERMINATE message.
	 */
	if ((CQE_OPCODE(rsp_msg->cqe) == T3_TERMINATE) &&
	    (CQE_STATUS(rsp_msg->cqe) == 0)) {
		if (SQ_TYPE(rsp_msg->cqe)) {
			PDBG("%s QPID 0x%x ep %p disconnecting\n",
			     __func__, qhp->wq.qpid, qhp->ep);
			iwch_ep_disconnect(qhp->ep, 0, GFP_ATOMIC);
		} else {
			PDBG("%s post REQ_ERR AE QPID 0x%x\n", __func__,
			     qhp->wq.qpid);
			post_qp_event(rnicp, chp, rsp_msg,
				      IB_EVENT_QP_REQ_ERR, 0);
			iwch_ep_disconnect(qhp->ep, 0, GFP_ATOMIC);
		}
		goto done;
	}

	/* Bad incoming Read request */
	if (SQ_TYPE(rsp_msg->cqe) &&
	    (CQE_OPCODE(rsp_msg->cqe) == T3_READ_RESP)) {
		post_qp_event(rnicp, chp, rsp_msg, IB_EVENT_QP_REQ_ERR, 1);
		goto done;
	}

	/* Bad incoming write */
	if (RQ_TYPE(rsp_msg->cqe) &&
	    (CQE_OPCODE(rsp_msg->cqe) == T3_RDMA_WRITE)) {
		post_qp_event(rnicp, chp, rsp_msg, IB_EVENT_QP_REQ_ERR, 1);
		goto done;
	}

	switch (CQE_STATUS(rsp_msg->cqe)) {

	/* Completion Events */
	case TPT_ERR_SUCCESS:

		/*
		 * Confirm the destination entry if this is a RECV completion.
		 */
		if (qhp->ep && SQ_TYPE(rsp_msg->cqe))
			dst_confirm(qhp->ep->dst);
		(*chp->ibcq.comp_handler)(&chp->ibcq, chp->ibcq.cq_context);
		break;

	case TPT_ERR_STAG:
	case TPT_ERR_PDID:
	case TPT_ERR_QPID:
	case TPT_ERR_ACCESS:
	case TPT_ERR_WRAP:
	case TPT_ERR_BOUND:
	case TPT_ERR_INVALIDATE_SHARED_MR:
	case TPT_ERR_INVALIDATE_MR_WITH_MW_BOUND:
		printk(KERN_ERR "%s - CQE Err qpid 0x%x opcode %d status 0x%x "
		       "type %d wrid.hi 0x%x wrid.lo 0x%x \n", __func__,
		       CQE_QPID(rsp_msg->cqe), CQE_OPCODE(rsp_msg->cqe),
		       CQE_STATUS(rsp_msg->cqe), CQE_TYPE(rsp_msg->cqe),
		       CQE_WRID_HI(rsp_msg->cqe), CQE_WRID_LOW(rsp_msg->cqe));
		(*chp->ibcq.comp_handler)(&chp->ibcq, chp->ibcq.cq_context);
		post_qp_event(rnicp, chp, rsp_msg, IB_EVENT_QP_ACCESS_ERR, 1);
		break;

	/* Device Fatal Errors */
	case TPT_ERR_ECC:
	case TPT_ERR_ECC_PSTAG:
	case TPT_ERR_INTERNAL_ERR:
		post_qp_event(rnicp, chp, rsp_msg, IB_EVENT_DEVICE_FATAL, 1);
		break;

	/* QP Fatal Errors */
	case TPT_ERR_OUT_OF_RQE:
	case TPT_ERR_PBL_ADDR_BOUND:
	case TPT_ERR_CRC:
	case TPT_ERR_MARKER:
	case TPT_ERR_PDU_LEN_ERR:
	case TPT_ERR_DDP_VERSION:
	case TPT_ERR_RDMA_VERSION:
	case TPT_ERR_OPCODE:
	case TPT_ERR_DDP_QUEUE_NUM:
	case TPT_ERR_MSN:
	case TPT_ERR_TBIT:
	case TPT_ERR_MO:
	case TPT_ERR_MSN_GAP:
	case TPT_ERR_MSN_RANGE:
	case TPT_ERR_RQE_ADDR_BOUND:
	case TPT_ERR_IRD_OVERFLOW:
		post_qp_event(rnicp, chp, rsp_msg, IB_EVENT_QP_FATAL, 1);
		break;

	default:
		printk(KERN_ERR MOD "Unknown T3 status 0x%x QPID 0x%x\n",
		       CQE_STATUS(rsp_msg->cqe), qhp->wq.qpid);
		post_qp_event(rnicp, chp, rsp_msg, IB_EVENT_QP_FATAL, 1);
		break;
	}
done:
	if (atomic_dec_and_test(&chp->refcnt))
	        wake_up(&chp->wait);
	iwch_qp_rem_ref(&qhp->ibqp);
out:
	dev_kfree_skb_irq(skb);
}
