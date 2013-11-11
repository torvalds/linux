/*
 * Copyright (c) 2005 Ammasso, Inc. All rights reserved.
 * Copyright (c) 2005 Open Grid Computing, Inc. All rights reserved.
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
#include "c2.h"
#include <rdma/iw_cm.h>
#include "c2_status.h"
#include "c2_ae.h"

static int c2_convert_cm_status(u32 c2_status)
{
	switch (c2_status) {
	case C2_CONN_STATUS_SUCCESS:
		return 0;
	case C2_CONN_STATUS_REJECTED:
		return -ENETRESET;
	case C2_CONN_STATUS_REFUSED:
		return -ECONNREFUSED;
	case C2_CONN_STATUS_TIMEDOUT:
		return -ETIMEDOUT;
	case C2_CONN_STATUS_NETUNREACH:
		return -ENETUNREACH;
	case C2_CONN_STATUS_HOSTUNREACH:
		return -EHOSTUNREACH;
	case C2_CONN_STATUS_INVALID_RNIC:
		return -EINVAL;
	case C2_CONN_STATUS_INVALID_QP:
		return -EINVAL;
	case C2_CONN_STATUS_INVALID_QP_STATE:
		return -EINVAL;
	case C2_CONN_STATUS_ADDR_NOT_AVAIL:
		return -EADDRNOTAVAIL;
	default:
		printk(KERN_ERR PFX
		       "%s - Unable to convert CM status: %d\n",
		       __func__, c2_status);
		return -EIO;
	}
}

static const char* to_event_str(int event)
{
	static const char* event_str[] = {
		"CCAE_REMOTE_SHUTDOWN",
		"CCAE_ACTIVE_CONNECT_RESULTS",
		"CCAE_CONNECTION_REQUEST",
		"CCAE_LLP_CLOSE_COMPLETE",
		"CCAE_TERMINATE_MESSAGE_RECEIVED",
		"CCAE_LLP_CONNECTION_RESET",
		"CCAE_LLP_CONNECTION_LOST",
		"CCAE_LLP_SEGMENT_SIZE_INVALID",
		"CCAE_LLP_INVALID_CRC",
		"CCAE_LLP_BAD_FPDU",
		"CCAE_INVALID_DDP_VERSION",
		"CCAE_INVALID_RDMA_VERSION",
		"CCAE_UNEXPECTED_OPCODE",
		"CCAE_INVALID_DDP_QUEUE_NUMBER",
		"CCAE_RDMA_READ_NOT_ENABLED",
		"CCAE_RDMA_WRITE_NOT_ENABLED",
		"CCAE_RDMA_READ_TOO_SMALL",
		"CCAE_NO_L_BIT",
		"CCAE_TAGGED_INVALID_STAG",
		"CCAE_TAGGED_BASE_BOUNDS_VIOLATION",
		"CCAE_TAGGED_ACCESS_RIGHTS_VIOLATION",
		"CCAE_TAGGED_INVALID_PD",
		"CCAE_WRAP_ERROR",
		"CCAE_BAD_CLOSE",
		"CCAE_BAD_LLP_CLOSE",
		"CCAE_INVALID_MSN_RANGE",
		"CCAE_INVALID_MSN_GAP",
		"CCAE_IRRQ_OVERFLOW",
		"CCAE_IRRQ_MSN_GAP",
		"CCAE_IRRQ_MSN_RANGE",
		"CCAE_IRRQ_INVALID_STAG",
		"CCAE_IRRQ_BASE_BOUNDS_VIOLATION",
		"CCAE_IRRQ_ACCESS_RIGHTS_VIOLATION",
		"CCAE_IRRQ_INVALID_PD",
		"CCAE_IRRQ_WRAP_ERROR",
		"CCAE_CQ_SQ_COMPLETION_OVERFLOW",
		"CCAE_CQ_RQ_COMPLETION_ERROR",
		"CCAE_QP_SRQ_WQE_ERROR",
		"CCAE_QP_LOCAL_CATASTROPHIC_ERROR",
		"CCAE_CQ_OVERFLOW",
		"CCAE_CQ_OPERATION_ERROR",
		"CCAE_SRQ_LIMIT_REACHED",
		"CCAE_QP_RQ_LIMIT_REACHED",
		"CCAE_SRQ_CATASTROPHIC_ERROR",
		"CCAE_RNIC_CATASTROPHIC_ERROR"
	};

	if (event < CCAE_REMOTE_SHUTDOWN ||
	    event > CCAE_RNIC_CATASTROPHIC_ERROR)
		return "<invalid event>";

	event -= CCAE_REMOTE_SHUTDOWN;
	return event_str[event];
}

static const char *to_qp_state_str(int state)
{
	switch (state) {
	case C2_QP_STATE_IDLE:
		return "C2_QP_STATE_IDLE";
	case C2_QP_STATE_CONNECTING:
		return "C2_QP_STATE_CONNECTING";
	case C2_QP_STATE_RTS:
		return "C2_QP_STATE_RTS";
	case C2_QP_STATE_CLOSING:
		return "C2_QP_STATE_CLOSING";
	case C2_QP_STATE_TERMINATE:
		return "C2_QP_STATE_TERMINATE";
	case C2_QP_STATE_ERROR:
		return "C2_QP_STATE_ERROR";
	default:
		return "<invalid QP state>";
	};
}

void c2_ae_event(struct c2_dev *c2dev, u32 mq_index)
{
	struct c2_mq *mq = c2dev->qptr_array[mq_index];
	union c2wr *wr;
	void *resource_user_context;
	struct iw_cm_event cm_event;
	struct ib_event ib_event;
	enum c2_resource_indicator resource_indicator;
	enum c2_event_id event_id;
	unsigned long flags;
	int status;

	/*
	 * retrieve the message
	 */
	wr = c2_mq_consume(mq);
	if (!wr)
		return;

	memset(&ib_event, 0, sizeof(ib_event));
	memset(&cm_event, 0, sizeof(cm_event));

	event_id = c2_wr_get_id(wr);
	resource_indicator = be32_to_cpu(wr->ae.ae_generic.resource_type);
	resource_user_context =
	    (void *) (unsigned long) wr->ae.ae_generic.user_context;

	status = cm_event.status = c2_convert_cm_status(c2_wr_get_result(wr));

	pr_debug("event received c2_dev=%p, event_id=%d, "
		"resource_indicator=%d, user_context=%p, status = %d\n",
		c2dev, event_id, resource_indicator, resource_user_context,
		status);

	switch (resource_indicator) {
	case C2_RES_IND_QP:{

		struct c2_qp *qp = (struct c2_qp *)resource_user_context;
		struct iw_cm_id *cm_id = qp->cm_id;
		struct c2wr_ae_active_connect_results *res;

		if (!cm_id) {
			pr_debug("event received, but cm_id is <nul>, qp=%p!\n",
				qp);
			goto ignore_it;
		}
		pr_debug("%s: event = %s, user_context=%llx, "
			"resource_type=%x, "
			"resource=%x, qp_state=%s\n",
			__func__,
			to_event_str(event_id),
			(unsigned long long) wr->ae.ae_generic.user_context,
			be32_to_cpu(wr->ae.ae_generic.resource_type),
			be32_to_cpu(wr->ae.ae_generic.resource),
			to_qp_state_str(be32_to_cpu(wr->ae.ae_generic.qp_state)));

		c2_set_qp_state(qp, be32_to_cpu(wr->ae.ae_generic.qp_state));

		switch (event_id) {
		case CCAE_ACTIVE_CONNECT_RESULTS:
			res = &wr->ae.ae_active_connect_results;
			cm_event.event = IW_CM_EVENT_CONNECT_REPLY;
			cm_event.local_addr.sin_addr.s_addr = res->laddr;
			cm_event.remote_addr.sin_addr.s_addr = res->raddr;
			cm_event.local_addr.sin_port = res->lport;
			cm_event.remote_addr.sin_port =	res->rport;
			if (status == 0) {
				cm_event.private_data_len =
					be32_to_cpu(res->private_data_length);
				cm_event.private_data = res->private_data;
			} else {
				spin_lock_irqsave(&qp->lock, flags);
				if (qp->cm_id) {
					qp->cm_id->rem_ref(qp->cm_id);
					qp->cm_id = NULL;
				}
				spin_unlock_irqrestore(&qp->lock, flags);
				cm_event.private_data_len = 0;
				cm_event.private_data = NULL;
			}
			if (cm_id->event_handler)
				cm_id->event_handler(cm_id, &cm_event);
			break;
		case CCAE_TERMINATE_MESSAGE_RECEIVED:
		case CCAE_CQ_SQ_COMPLETION_OVERFLOW:
			ib_event.device = &c2dev->ibdev;
			ib_event.element.qp = &qp->ibqp;
			ib_event.event = IB_EVENT_QP_REQ_ERR;

			if (qp->ibqp.event_handler)
				qp->ibqp.event_handler(&ib_event,
						       qp->ibqp.
						       qp_context);
			break;
		case CCAE_BAD_CLOSE:
		case CCAE_LLP_CLOSE_COMPLETE:
		case CCAE_LLP_CONNECTION_RESET:
		case CCAE_LLP_CONNECTION_LOST:
			BUG_ON(cm_id->event_handler==(void*)0x6b6b6b6b);

			spin_lock_irqsave(&qp->lock, flags);
			if (qp->cm_id) {
				qp->cm_id->rem_ref(qp->cm_id);
				qp->cm_id = NULL;
			}
			spin_unlock_irqrestore(&qp->lock, flags);
			cm_event.event = IW_CM_EVENT_CLOSE;
			cm_event.status = 0;
			if (cm_id->event_handler)
				cm_id->event_handler(cm_id, &cm_event);
			break;
		default:
			BUG_ON(1);
			pr_debug("%s:%d Unexpected event_id=%d on QP=%p, "
				"CM_ID=%p\n",
				__func__, __LINE__,
				event_id, qp, cm_id);
			break;
		}
		break;
	}

	case C2_RES_IND_EP:{

		struct c2wr_ae_connection_request *req =
			&wr->ae.ae_connection_request;
		struct iw_cm_id *cm_id =
			(struct iw_cm_id *)resource_user_context;

		pr_debug("C2_RES_IND_EP event_id=%d\n", event_id);
		if (event_id != CCAE_CONNECTION_REQUEST) {
			pr_debug("%s: Invalid event_id: %d\n",
				__func__, event_id);
			break;
		}
		cm_event.event = IW_CM_EVENT_CONNECT_REQUEST;
		cm_event.provider_data = (void*)(unsigned long)req->cr_handle;
		cm_event.local_addr.sin_addr.s_addr = req->laddr;
		cm_event.remote_addr.sin_addr.s_addr = req->raddr;
		cm_event.local_addr.sin_port = req->lport;
		cm_event.remote_addr.sin_port = req->rport;
		cm_event.private_data_len =
			be32_to_cpu(req->private_data_length);
		cm_event.private_data = req->private_data;
		/*
		 * Until ird/ord negotiation via MPAv2 support is added, send
		 * max supported values
		 */
		cm_event.ird = cm_event.ord = 128;

		if (cm_id->event_handler)
			cm_id->event_handler(cm_id, &cm_event);
		break;
	}

	case C2_RES_IND_CQ:{
		struct c2_cq *cq =
		    (struct c2_cq *) resource_user_context;

		pr_debug("IB_EVENT_CQ_ERR\n");
		ib_event.device = &c2dev->ibdev;
		ib_event.element.cq = &cq->ibcq;
		ib_event.event = IB_EVENT_CQ_ERR;

		if (cq->ibcq.event_handler)
			cq->ibcq.event_handler(&ib_event,
					       cq->ibcq.cq_context);
		break;
	}

	default:
		printk("Bad resource indicator = %d\n",
		       resource_indicator);
		break;
	}

 ignore_it:
	c2_mq_free(mq);
}
