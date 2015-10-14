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
#include "c2_vq.h"

static void handle_mq(struct c2_dev *c2dev, u32 index);
static void handle_vq(struct c2_dev *c2dev, u32 mq_index);

/*
 * Handle RNIC interrupts
 */
void c2_rnic_interrupt(struct c2_dev *c2dev)
{
	unsigned int mq_index;

	while (c2dev->hints_read != be16_to_cpu(*c2dev->hint_count)) {
		mq_index = readl(c2dev->regs + PCI_BAR0_HOST_HINT);
		if (mq_index & 0x80000000) {
			break;
		}

		c2dev->hints_read++;
		handle_mq(c2dev, mq_index);
	}

}

/*
 * Top level MQ handler
 */
static void handle_mq(struct c2_dev *c2dev, u32 mq_index)
{
	if (c2dev->qptr_array[mq_index] == NULL) {
		pr_debug("handle_mq: stray activity for mq_index=%d\n",
			 mq_index);
		return;
	}

	switch (mq_index) {
	case (0):
		/*
		 * An index of 0 in the activity queue
		 * indicates the req vq now has messages
		 * available...
		 *
		 * Wake up any waiters waiting on req VQ
		 * message availability.
		 */
		wake_up(&c2dev->req_vq_wo);
		break;
	case (1):
		handle_vq(c2dev, mq_index);
		break;
	case (2):
		/* We have to purge the VQ in case there are pending
		 * accept reply requests that would result in the
		 * generation of an ESTABLISHED event. If we don't
		 * generate these first, a CLOSE event could end up
		 * being delivered before the ESTABLISHED event.
		 */
		handle_vq(c2dev, 1);

		c2_ae_event(c2dev, mq_index);
		break;
	default:
		/* There is no event synchronization between CQ events
		 * and AE or CM events. In fact, CQE could be
		 * delivered for all of the I/O up to and including the
		 * FLUSH for a peer disconenct prior to the ESTABLISHED
		 * event being delivered to the app. The reason for this
		 * is that CM events are delivered on a thread, while AE
		 * and CM events are delivered on interrupt context.
		 */
		c2_cq_event(c2dev, mq_index);
		break;
	}

	return;
}

/*
 * Handles verbs WR replies.
 */
static void handle_vq(struct c2_dev *c2dev, u32 mq_index)
{
	void *adapter_msg, *reply_msg;
	struct c2wr_hdr *host_msg;
	struct c2wr_hdr tmp;
	struct c2_mq *reply_vq;
	struct c2_vq_req *req;
	struct iw_cm_event cm_event;
	int err;

	reply_vq = c2dev->qptr_array[mq_index];

	/*
	 * get next msg from mq_index into adapter_msg.
	 * don't free it yet.
	 */
	adapter_msg = c2_mq_consume(reply_vq);
	if (adapter_msg == NULL) {
		return;
	}

	host_msg = vq_repbuf_alloc(c2dev);

	/*
	 * If we can't get a host buffer, then we'll still
	 * wakeup the waiter, we just won't give him the msg.
	 * It is assumed the waiter will deal with this...
	 */
	if (!host_msg) {
		pr_debug("handle_vq: no repbufs!\n");

		/*
		 * just copy the WR header into a local variable.
		 * this allows us to still demux on the context
		 */
		host_msg = &tmp;
		memcpy(host_msg, adapter_msg, sizeof(tmp));
		reply_msg = NULL;
	} else {
		memcpy(host_msg, adapter_msg, reply_vq->msg_size);
		reply_msg = host_msg;
	}

	/*
	 * consume the msg from the MQ
	 */
	c2_mq_free(reply_vq);

	/*
	 * wakeup the waiter.
	 */
	req = (struct c2_vq_req *) (unsigned long) host_msg->context;
	if (req == NULL) {
		/*
		 * We should never get here, as the adapter should
		 * never send us a reply that we're not expecting.
		 */
		if (reply_msg != NULL)
			vq_repbuf_free(c2dev, host_msg);
		pr_debug("handle_vq: UNEXPECTEDLY got NULL req\n");
		return;
	}

	if (reply_msg)
		err = c2_errno(reply_msg);
	else
		err = -ENOMEM;

	if (!err) switch (req->event) {
	case IW_CM_EVENT_ESTABLISHED:
		c2_set_qp_state(req->qp,
				C2_QP_STATE_RTS);
		/*
		 * Until ird/ord negotiation via MPAv2 support is added, send
		 * max supported values
		 */
		cm_event.ird = cm_event.ord = 128;
	case IW_CM_EVENT_CLOSE:

		/*
		 * Move the QP to RTS if this is
		 * the established event
		 */
		cm_event.event = req->event;
		cm_event.status = 0;
		cm_event.local_addr = req->cm_id->local_addr;
		cm_event.remote_addr = req->cm_id->remote_addr;
		cm_event.private_data = NULL;
		cm_event.private_data_len = 0;
		req->cm_id->event_handler(req->cm_id, &cm_event);
		break;
	default:
		break;
	}

	req->reply_msg = (u64) (unsigned long) (reply_msg);
	atomic_set(&req->reply_ready, 1);
	wake_up(&req->wait_object);

	/*
	 * If the request was cancelled, then this put will
	 * free the vq_req memory...and reply_msg!!!
	 */
	vq_req_put(c2dev, req);
}
