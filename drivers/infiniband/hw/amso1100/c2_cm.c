/*
 * Copyright (c) 2005 Ammasso, Inc.  All rights reserved.
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
 *
 */
#include "c2.h"
#include "c2_wr.h"
#include "c2_vq.h"
#include <rdma/iw_cm.h>

int c2_llp_connect(struct iw_cm_id *cm_id, struct iw_cm_conn_param *iw_param)
{
	struct c2_dev *c2dev = to_c2dev(cm_id->device);
	struct ib_qp *ibqp;
	struct c2_qp *qp;
	struct c2wr_qp_connect_req *wr;	/* variable size needs a malloc. */
	struct c2_vq_req *vq_req;
	int err;

	ibqp = c2_get_qp(cm_id->device, iw_param->qpn);
	if (!ibqp)
		return -EINVAL;
	qp = to_c2qp(ibqp);

	/* Associate QP <--> CM_ID */
	cm_id->provider_data = qp;
	cm_id->add_ref(cm_id);
	qp->cm_id = cm_id;

	/*
	 * only support the max private_data length
	 */
	if (iw_param->private_data_len > C2_MAX_PRIVATE_DATA_SIZE) {
		err = -EINVAL;
		goto bail0;
	}
	/*
	 * Set the rdma read limits
	 */
	err = c2_qp_set_read_limits(c2dev, qp, iw_param->ord, iw_param->ird);
	if (err)
		goto bail0;

	/*
	 * Create and send a WR_QP_CONNECT...
	 */
	wr = kmalloc(c2dev->req_vq.msg_size, GFP_KERNEL);
	if (!wr) {
		err = -ENOMEM;
		goto bail0;
	}

	vq_req = vq_req_alloc(c2dev);
	if (!vq_req) {
		err = -ENOMEM;
		goto bail1;
	}

	c2_wr_set_id(wr, CCWR_QP_CONNECT);
	wr->hdr.context = 0;
	wr->rnic_handle = c2dev->adapter_handle;
	wr->qp_handle = qp->adapter_handle;

	wr->remote_addr = cm_id->remote_addr.sin_addr.s_addr;
	wr->remote_port = cm_id->remote_addr.sin_port;

	/*
	 * Move any private data from the callers's buf into
	 * the WR.
	 */
	if (iw_param->private_data) {
		wr->private_data_length =
			cpu_to_be32(iw_param->private_data_len);
		memcpy(&wr->private_data[0], iw_param->private_data,
		       iw_param->private_data_len);
	} else
		wr->private_data_length = 0;

	/*
	 * Send WR to adapter.  NOTE: There is no synch reply from
	 * the adapter.
	 */
	err = vq_send_wr(c2dev, (union c2wr *) wr);
	vq_req_free(c2dev, vq_req);

 bail1:
	kfree(wr);
 bail0:
	if (err) {
		/*
		 * If we fail, release reference on QP and
		 * disassociate QP from CM_ID
		 */
		cm_id->provider_data = NULL;
		qp->cm_id = NULL;
		cm_id->rem_ref(cm_id);
	}
	return err;
}

int c2_llp_service_create(struct iw_cm_id *cm_id, int backlog)
{
	struct c2_dev *c2dev;
	struct c2wr_ep_listen_create_req wr;
	struct c2wr_ep_listen_create_rep *reply;
	struct c2_vq_req *vq_req;
	int err;

	c2dev = to_c2dev(cm_id->device);
	if (c2dev == NULL)
		return -EINVAL;

	/*
	 * Allocate verbs request.
	 */
	vq_req = vq_req_alloc(c2dev);
	if (!vq_req)
		return -ENOMEM;

	/*
	 * Build the WR
	 */
	c2_wr_set_id(&wr, CCWR_EP_LISTEN_CREATE);
	wr.hdr.context = (u64) (unsigned long) vq_req;
	wr.rnic_handle = c2dev->adapter_handle;
	wr.local_addr = cm_id->local_addr.sin_addr.s_addr;
	wr.local_port = cm_id->local_addr.sin_port;
	wr.backlog = cpu_to_be32(backlog);
	wr.user_context = (u64) (unsigned long) cm_id;

	/*
	 * Reference the request struct.  Dereferenced in the int handler.
	 */
	vq_req_get(c2dev, vq_req);

	/*
	 * Send WR to adapter
	 */
	err = vq_send_wr(c2dev, (union c2wr *) & wr);
	if (err) {
		vq_req_put(c2dev, vq_req);
		goto bail0;
	}

	/*
	 * Wait for reply from adapter
	 */
	err = vq_wait_for_reply(c2dev, vq_req);
	if (err)
		goto bail0;

	/*
	 * Process reply
	 */
	reply =
	    (struct c2wr_ep_listen_create_rep *) (unsigned long) vq_req->reply_msg;
	if (!reply) {
		err = -ENOMEM;
		goto bail1;
	}

	if ((err = c2_errno(reply)) != 0)
		goto bail1;

	/*
	 * Keep the adapter handle. Used in subsequent destroy
	 */
	cm_id->provider_data = (void*)(unsigned long) reply->ep_handle;

	/*
	 * free vq stuff
	 */
	vq_repbuf_free(c2dev, reply);
	vq_req_free(c2dev, vq_req);

	return 0;

 bail1:
	vq_repbuf_free(c2dev, reply);
 bail0:
	vq_req_free(c2dev, vq_req);
	return err;
}


int c2_llp_service_destroy(struct iw_cm_id *cm_id)
{

	struct c2_dev *c2dev;
	struct c2wr_ep_listen_destroy_req wr;
	struct c2wr_ep_listen_destroy_rep *reply;
	struct c2_vq_req *vq_req;
	int err;

	c2dev = to_c2dev(cm_id->device);
	if (c2dev == NULL)
		return -EINVAL;

	/*
	 * Allocate verbs request.
	 */
	vq_req = vq_req_alloc(c2dev);
	if (!vq_req)
		return -ENOMEM;

	/*
	 * Build the WR
	 */
	c2_wr_set_id(&wr, CCWR_EP_LISTEN_DESTROY);
	wr.hdr.context = (unsigned long) vq_req;
	wr.rnic_handle = c2dev->adapter_handle;
	wr.ep_handle = (u32)(unsigned long)cm_id->provider_data;

	/*
	 * reference the request struct.  dereferenced in the int handler.
	 */
	vq_req_get(c2dev, vq_req);

	/*
	 * Send WR to adapter
	 */
	err = vq_send_wr(c2dev, (union c2wr *) & wr);
	if (err) {
		vq_req_put(c2dev, vq_req);
		goto bail0;
	}

	/*
	 * Wait for reply from adapter
	 */
	err = vq_wait_for_reply(c2dev, vq_req);
	if (err)
		goto bail0;

	/*
	 * Process reply
	 */
	reply=(struct c2wr_ep_listen_destroy_rep *)(unsigned long)vq_req->reply_msg;
	if (!reply) {
		err = -ENOMEM;
		goto bail0;
	}
	if ((err = c2_errno(reply)) != 0)
		goto bail1;

 bail1:
	vq_repbuf_free(c2dev, reply);
 bail0:
	vq_req_free(c2dev, vq_req);
	return err;
}

int c2_llp_accept(struct iw_cm_id *cm_id, struct iw_cm_conn_param *iw_param)
{
	struct c2_dev *c2dev = to_c2dev(cm_id->device);
	struct c2_qp *qp;
	struct ib_qp *ibqp;
	struct c2wr_cr_accept_req *wr;	/* variable length WR */
	struct c2_vq_req *vq_req;
	struct c2wr_cr_accept_rep *reply;	/* VQ Reply msg ptr. */
	int err;

	ibqp = c2_get_qp(cm_id->device, iw_param->qpn);
	if (!ibqp)
		return -EINVAL;
	qp = to_c2qp(ibqp);

	/* Set the RDMA read limits */
	err = c2_qp_set_read_limits(c2dev, qp, iw_param->ord, iw_param->ird);
	if (err)
		goto bail0;

	/* Allocate verbs request. */
	vq_req = vq_req_alloc(c2dev);
	if (!vq_req) {
		err = -ENOMEM;
		goto bail0;
	}
	vq_req->qp = qp;
	vq_req->cm_id = cm_id;
	vq_req->event = IW_CM_EVENT_ESTABLISHED;

	wr = kmalloc(c2dev->req_vq.msg_size, GFP_KERNEL);
	if (!wr) {
		err = -ENOMEM;
		goto bail1;
	}

	/* Build the WR */
	c2_wr_set_id(wr, CCWR_CR_ACCEPT);
	wr->hdr.context = (unsigned long) vq_req;
	wr->rnic_handle = c2dev->adapter_handle;
	wr->ep_handle = (u32) (unsigned long) cm_id->provider_data;
	wr->qp_handle = qp->adapter_handle;

	/* Replace the cr_handle with the QP after accept */
	cm_id->provider_data = qp;
	cm_id->add_ref(cm_id);
	qp->cm_id = cm_id;

	cm_id->provider_data = qp;

	/* Validate private_data length */
	if (iw_param->private_data_len > C2_MAX_PRIVATE_DATA_SIZE) {
		err = -EINVAL;
		goto bail1;
	}

	if (iw_param->private_data) {
		wr->private_data_length = cpu_to_be32(iw_param->private_data_len);
		memcpy(&wr->private_data[0],
		       iw_param->private_data, iw_param->private_data_len);
	} else
		wr->private_data_length = 0;

	/* Reference the request struct.  Dereferenced in the int handler. */
	vq_req_get(c2dev, vq_req);

	/* Send WR to adapter */
	err = vq_send_wr(c2dev, (union c2wr *) wr);
	if (err) {
		vq_req_put(c2dev, vq_req);
		goto bail1;
	}

	/* Wait for reply from adapter */
	err = vq_wait_for_reply(c2dev, vq_req);
	if (err)
		goto bail1;

	/* Check that reply is present */
	reply = (struct c2wr_cr_accept_rep *) (unsigned long) vq_req->reply_msg;
	if (!reply) {
		err = -ENOMEM;
		goto bail1;
	}

	err = c2_errno(reply);
	vq_repbuf_free(c2dev, reply);

	if (!err)
		c2_set_qp_state(qp, C2_QP_STATE_RTS);
 bail1:
	kfree(wr);
	vq_req_free(c2dev, vq_req);
 bail0:
	if (err) {
		/*
		 * If we fail, release reference on QP and
		 * disassociate QP from CM_ID
		 */
		cm_id->provider_data = NULL;
		qp->cm_id = NULL;
		cm_id->rem_ref(cm_id);
	}
	return err;
}

int c2_llp_reject(struct iw_cm_id *cm_id, const void *pdata, u8 pdata_len)
{
	struct c2_dev *c2dev;
	struct c2wr_cr_reject_req wr;
	struct c2_vq_req *vq_req;
	struct c2wr_cr_reject_rep *reply;
	int err;

	c2dev = to_c2dev(cm_id->device);

	/*
	 * Allocate verbs request.
	 */
	vq_req = vq_req_alloc(c2dev);
	if (!vq_req)
		return -ENOMEM;

	/*
	 * Build the WR
	 */
	c2_wr_set_id(&wr, CCWR_CR_REJECT);
	wr.hdr.context = (unsigned long) vq_req;
	wr.rnic_handle = c2dev->adapter_handle;
	wr.ep_handle = (u32) (unsigned long) cm_id->provider_data;

	/*
	 * reference the request struct.  dereferenced in the int handler.
	 */
	vq_req_get(c2dev, vq_req);

	/*
	 * Send WR to adapter
	 */
	err = vq_send_wr(c2dev, (union c2wr *) & wr);
	if (err) {
		vq_req_put(c2dev, vq_req);
		goto bail0;
	}

	/*
	 * Wait for reply from adapter
	 */
	err = vq_wait_for_reply(c2dev, vq_req);
	if (err)
		goto bail0;

	/*
	 * Process reply
	 */
	reply = (struct c2wr_cr_reject_rep *) (unsigned long)
		vq_req->reply_msg;
	if (!reply) {
		err = -ENOMEM;
		goto bail0;
	}
	err = c2_errno(reply);
	/*
	 * free vq stuff
	 */
	vq_repbuf_free(c2dev, reply);

 bail0:
	vq_req_free(c2dev, vq_req);
	return err;
}
