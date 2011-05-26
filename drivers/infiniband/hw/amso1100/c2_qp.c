/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Cisco Systems. All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2004 Voltaire, Inc. All rights reserved.
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

#include <linux/delay.h>
#include <linux/gfp.h>

#include "c2.h"
#include "c2_vq.h"
#include "c2_status.h"

#define C2_MAX_ORD_PER_QP 128
#define C2_MAX_IRD_PER_QP 128

#define C2_HINT_MAKE(q_index, hint_count) (((q_index) << 16) | hint_count)
#define C2_HINT_GET_INDEX(hint) (((hint) & 0x7FFF0000) >> 16)
#define C2_HINT_GET_COUNT(hint) ((hint) & 0x0000FFFF)

#define NO_SUPPORT -1
static const u8 c2_opcode[] = {
	[IB_WR_SEND] = C2_WR_TYPE_SEND,
	[IB_WR_SEND_WITH_IMM] = NO_SUPPORT,
	[IB_WR_RDMA_WRITE] = C2_WR_TYPE_RDMA_WRITE,
	[IB_WR_RDMA_WRITE_WITH_IMM] = NO_SUPPORT,
	[IB_WR_RDMA_READ] = C2_WR_TYPE_RDMA_READ,
	[IB_WR_ATOMIC_CMP_AND_SWP] = NO_SUPPORT,
	[IB_WR_ATOMIC_FETCH_AND_ADD] = NO_SUPPORT,
};

static int to_c2_state(enum ib_qp_state ib_state)
{
	switch (ib_state) {
	case IB_QPS_RESET:
		return C2_QP_STATE_IDLE;
	case IB_QPS_RTS:
		return C2_QP_STATE_RTS;
	case IB_QPS_SQD:
		return C2_QP_STATE_CLOSING;
	case IB_QPS_SQE:
		return C2_QP_STATE_CLOSING;
	case IB_QPS_ERR:
		return C2_QP_STATE_ERROR;
	default:
		return -1;
	}
}

static int to_ib_state(enum c2_qp_state c2_state)
{
	switch (c2_state) {
	case C2_QP_STATE_IDLE:
		return IB_QPS_RESET;
	case C2_QP_STATE_CONNECTING:
		return IB_QPS_RTR;
	case C2_QP_STATE_RTS:
		return IB_QPS_RTS;
	case C2_QP_STATE_CLOSING:
		return IB_QPS_SQD;
	case C2_QP_STATE_ERROR:
		return IB_QPS_ERR;
	case C2_QP_STATE_TERMINATE:
		return IB_QPS_SQE;
	default:
		return -1;
	}
}

static const char *to_ib_state_str(int ib_state)
{
	static const char *state_str[] = {
		"IB_QPS_RESET",
		"IB_QPS_INIT",
		"IB_QPS_RTR",
		"IB_QPS_RTS",
		"IB_QPS_SQD",
		"IB_QPS_SQE",
		"IB_QPS_ERR"
	};
	if (ib_state < IB_QPS_RESET ||
	    ib_state > IB_QPS_ERR)
		return "<invalid IB QP state>";

	ib_state -= IB_QPS_RESET;
	return state_str[ib_state];
}

void c2_set_qp_state(struct c2_qp *qp, int c2_state)
{
	int new_state = to_ib_state(c2_state);

	pr_debug("%s: qp[%p] state modify %s --> %s\n",
	       __func__,
		qp,
		to_ib_state_str(qp->state),
		to_ib_state_str(new_state));
	qp->state = new_state;
}

#define C2_QP_NO_ATTR_CHANGE 0xFFFFFFFF

int c2_qp_modify(struct c2_dev *c2dev, struct c2_qp *qp,
		 struct ib_qp_attr *attr, int attr_mask)
{
	struct c2wr_qp_modify_req wr;
	struct c2wr_qp_modify_rep *reply;
	struct c2_vq_req *vq_req;
	unsigned long flags;
	u8 next_state;
	int err;

	pr_debug("%s:%d qp=%p, %s --> %s\n",
		__func__, __LINE__,
		qp,
		to_ib_state_str(qp->state),
		to_ib_state_str(attr->qp_state));

	vq_req = vq_req_alloc(c2dev);
	if (!vq_req)
		return -ENOMEM;

	c2_wr_set_id(&wr, CCWR_QP_MODIFY);
	wr.hdr.context = (unsigned long) vq_req;
	wr.rnic_handle = c2dev->adapter_handle;
	wr.qp_handle = qp->adapter_handle;
	wr.ord = cpu_to_be32(C2_QP_NO_ATTR_CHANGE);
	wr.ird = cpu_to_be32(C2_QP_NO_ATTR_CHANGE);
	wr.sq_depth = cpu_to_be32(C2_QP_NO_ATTR_CHANGE);
	wr.rq_depth = cpu_to_be32(C2_QP_NO_ATTR_CHANGE);

	if (attr_mask & IB_QP_STATE) {
		/* Ensure the state is valid */
		if (attr->qp_state < 0 || attr->qp_state > IB_QPS_ERR) {
			err = -EINVAL;
			goto bail0;
		}

		wr.next_qp_state = cpu_to_be32(to_c2_state(attr->qp_state));

		if (attr->qp_state == IB_QPS_ERR) {
			spin_lock_irqsave(&qp->lock, flags);
			if (qp->cm_id && qp->state == IB_QPS_RTS) {
				pr_debug("Generating CLOSE event for QP-->ERR, "
					"qp=%p, cm_id=%p\n",qp,qp->cm_id);
				/* Generate an CLOSE event */
				vq_req->cm_id = qp->cm_id;
				vq_req->event = IW_CM_EVENT_CLOSE;
			}
			spin_unlock_irqrestore(&qp->lock, flags);
		}
		next_state =  attr->qp_state;

	} else if (attr_mask & IB_QP_CUR_STATE) {

		if (attr->cur_qp_state != IB_QPS_RTR &&
		    attr->cur_qp_state != IB_QPS_RTS &&
		    attr->cur_qp_state != IB_QPS_SQD &&
		    attr->cur_qp_state != IB_QPS_SQE) {
			err = -EINVAL;
			goto bail0;
		} else
			wr.next_qp_state =
			    cpu_to_be32(to_c2_state(attr->cur_qp_state));

		next_state = attr->cur_qp_state;

	} else {
		err = 0;
		goto bail0;
	}

	/* reference the request struct */
	vq_req_get(c2dev, vq_req);

	err = vq_send_wr(c2dev, (union c2wr *) & wr);
	if (err) {
		vq_req_put(c2dev, vq_req);
		goto bail0;
	}

	err = vq_wait_for_reply(c2dev, vq_req);
	if (err)
		goto bail0;

	reply = (struct c2wr_qp_modify_rep *) (unsigned long) vq_req->reply_msg;
	if (!reply) {
		err = -ENOMEM;
		goto bail0;
	}

	err = c2_errno(reply);
	if (!err)
		qp->state = next_state;
#ifdef DEBUG
	else
		pr_debug("%s: c2_errno=%d\n", __func__, err);
#endif
	/*
	 * If we're going to error and generating the event here, then
	 * we need to remove the reference because there will be no
	 * close event generated by the adapter
	*/
	spin_lock_irqsave(&qp->lock, flags);
	if (vq_req->event==IW_CM_EVENT_CLOSE && qp->cm_id) {
		qp->cm_id->rem_ref(qp->cm_id);
		qp->cm_id = NULL;
	}
	spin_unlock_irqrestore(&qp->lock, flags);

	vq_repbuf_free(c2dev, reply);
      bail0:
	vq_req_free(c2dev, vq_req);

	pr_debug("%s:%d qp=%p, cur_state=%s\n",
		__func__, __LINE__,
		qp,
		to_ib_state_str(qp->state));
	return err;
}

int c2_qp_set_read_limits(struct c2_dev *c2dev, struct c2_qp *qp,
			  int ord, int ird)
{
	struct c2wr_qp_modify_req wr;
	struct c2wr_qp_modify_rep *reply;
	struct c2_vq_req *vq_req;
	int err;

	vq_req = vq_req_alloc(c2dev);
	if (!vq_req)
		return -ENOMEM;

	c2_wr_set_id(&wr, CCWR_QP_MODIFY);
	wr.hdr.context = (unsigned long) vq_req;
	wr.rnic_handle = c2dev->adapter_handle;
	wr.qp_handle = qp->adapter_handle;
	wr.ord = cpu_to_be32(ord);
	wr.ird = cpu_to_be32(ird);
	wr.sq_depth = cpu_to_be32(C2_QP_NO_ATTR_CHANGE);
	wr.rq_depth = cpu_to_be32(C2_QP_NO_ATTR_CHANGE);
	wr.next_qp_state = cpu_to_be32(C2_QP_NO_ATTR_CHANGE);

	/* reference the request struct */
	vq_req_get(c2dev, vq_req);

	err = vq_send_wr(c2dev, (union c2wr *) & wr);
	if (err) {
		vq_req_put(c2dev, vq_req);
		goto bail0;
	}

	err = vq_wait_for_reply(c2dev, vq_req);
	if (err)
		goto bail0;

	reply = (struct c2wr_qp_modify_rep *) (unsigned long)
		vq_req->reply_msg;
	if (!reply) {
		err = -ENOMEM;
		goto bail0;
	}

	err = c2_errno(reply);
	vq_repbuf_free(c2dev, reply);
      bail0:
	vq_req_free(c2dev, vq_req);
	return err;
}

static int destroy_qp(struct c2_dev *c2dev, struct c2_qp *qp)
{
	struct c2_vq_req *vq_req;
	struct c2wr_qp_destroy_req wr;
	struct c2wr_qp_destroy_rep *reply;
	unsigned long flags;
	int err;

	/*
	 * Allocate a verb request message
	 */
	vq_req = vq_req_alloc(c2dev);
	if (!vq_req) {
		return -ENOMEM;
	}

	/*
	 * Initialize the WR
	 */
	c2_wr_set_id(&wr, CCWR_QP_DESTROY);
	wr.hdr.context = (unsigned long) vq_req;
	wr.rnic_handle = c2dev->adapter_handle;
	wr.qp_handle = qp->adapter_handle;

	/*
	 * reference the request struct.  dereferenced in the int handler.
	 */
	vq_req_get(c2dev, vq_req);

	spin_lock_irqsave(&qp->lock, flags);
	if (qp->cm_id && qp->state == IB_QPS_RTS) {
		pr_debug("destroy_qp: generating CLOSE event for QP-->ERR, "
			"qp=%p, cm_id=%p\n",qp,qp->cm_id);
		/* Generate an CLOSE event */
		vq_req->qp = qp;
		vq_req->cm_id = qp->cm_id;
		vq_req->event = IW_CM_EVENT_CLOSE;
	}
	spin_unlock_irqrestore(&qp->lock, flags);

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
	if (err) {
		goto bail0;
	}

	/*
	 * Process reply
	 */
	reply = (struct c2wr_qp_destroy_rep *) (unsigned long) (vq_req->reply_msg);
	if (!reply) {
		err = -ENOMEM;
		goto bail0;
	}

	spin_lock_irqsave(&qp->lock, flags);
	if (qp->cm_id) {
		qp->cm_id->rem_ref(qp->cm_id);
		qp->cm_id = NULL;
	}
	spin_unlock_irqrestore(&qp->lock, flags);

	vq_repbuf_free(c2dev, reply);
      bail0:
	vq_req_free(c2dev, vq_req);
	return err;
}

static int c2_alloc_qpn(struct c2_dev *c2dev, struct c2_qp *qp)
{
	int ret;

        do {
		spin_lock_irq(&c2dev->qp_table.lock);
		ret = idr_get_new_above(&c2dev->qp_table.idr, qp,
					c2dev->qp_table.last++, &qp->qpn);
		spin_unlock_irq(&c2dev->qp_table.lock);
        } while ((ret == -EAGAIN) &&
	 	 idr_pre_get(&c2dev->qp_table.idr, GFP_KERNEL));
	return ret;
}

static void c2_free_qpn(struct c2_dev *c2dev, int qpn)
{
	spin_lock_irq(&c2dev->qp_table.lock);
	idr_remove(&c2dev->qp_table.idr, qpn);
	spin_unlock_irq(&c2dev->qp_table.lock);
}

struct c2_qp *c2_find_qpn(struct c2_dev *c2dev, int qpn)
{
	unsigned long flags;
	struct c2_qp *qp;

	spin_lock_irqsave(&c2dev->qp_table.lock, flags);
	qp = idr_find(&c2dev->qp_table.idr, qpn);
	spin_unlock_irqrestore(&c2dev->qp_table.lock, flags);
	return qp;
}

int c2_alloc_qp(struct c2_dev *c2dev,
		struct c2_pd *pd,
		struct ib_qp_init_attr *qp_attrs, struct c2_qp *qp)
{
	struct c2wr_qp_create_req wr;
	struct c2wr_qp_create_rep *reply;
	struct c2_vq_req *vq_req;
	struct c2_cq *send_cq = to_c2cq(qp_attrs->send_cq);
	struct c2_cq *recv_cq = to_c2cq(qp_attrs->recv_cq);
	unsigned long peer_pa;
	u32 q_size, msg_size, mmap_size;
	void __iomem *mmap;
	int err;

	err = c2_alloc_qpn(c2dev, qp);
	if (err)
		return err;
	qp->ibqp.qp_num = qp->qpn;
	qp->ibqp.qp_type = IB_QPT_RC;

	/* Allocate the SQ and RQ shared pointers */
	qp->sq_mq.shared = c2_alloc_mqsp(c2dev, c2dev->kern_mqsp_pool,
					 &qp->sq_mq.shared_dma, GFP_KERNEL);
	if (!qp->sq_mq.shared) {
		err = -ENOMEM;
		goto bail0;
	}

	qp->rq_mq.shared = c2_alloc_mqsp(c2dev, c2dev->kern_mqsp_pool,
					 &qp->rq_mq.shared_dma, GFP_KERNEL);
	if (!qp->rq_mq.shared) {
		err = -ENOMEM;
		goto bail1;
	}

	/* Allocate the verbs request */
	vq_req = vq_req_alloc(c2dev);
	if (vq_req == NULL) {
		err = -ENOMEM;
		goto bail2;
	}

	/* Initialize the work request */
	memset(&wr, 0, sizeof(wr));
	c2_wr_set_id(&wr, CCWR_QP_CREATE);
	wr.hdr.context = (unsigned long) vq_req;
	wr.rnic_handle = c2dev->adapter_handle;
	wr.sq_cq_handle = send_cq->adapter_handle;
	wr.rq_cq_handle = recv_cq->adapter_handle;
	wr.sq_depth = cpu_to_be32(qp_attrs->cap.max_send_wr + 1);
	wr.rq_depth = cpu_to_be32(qp_attrs->cap.max_recv_wr + 1);
	wr.srq_handle = 0;
	wr.flags = cpu_to_be32(QP_RDMA_READ | QP_RDMA_WRITE | QP_MW_BIND |
			       QP_ZERO_STAG | QP_RDMA_READ_RESPONSE);
	wr.send_sgl_depth = cpu_to_be32(qp_attrs->cap.max_send_sge);
	wr.recv_sgl_depth = cpu_to_be32(qp_attrs->cap.max_recv_sge);
	wr.rdma_write_sgl_depth = cpu_to_be32(qp_attrs->cap.max_send_sge);
	wr.shared_sq_ht = cpu_to_be64(qp->sq_mq.shared_dma);
	wr.shared_rq_ht = cpu_to_be64(qp->rq_mq.shared_dma);
	wr.ord = cpu_to_be32(C2_MAX_ORD_PER_QP);
	wr.ird = cpu_to_be32(C2_MAX_IRD_PER_QP);
	wr.pd_id = pd->pd_id;
	wr.user_context = (unsigned long) qp;

	vq_req_get(c2dev, vq_req);

	/* Send the WR to the adapter */
	err = vq_send_wr(c2dev, (union c2wr *) & wr);
	if (err) {
		vq_req_put(c2dev, vq_req);
		goto bail3;
	}

	/* Wait for the verb reply  */
	err = vq_wait_for_reply(c2dev, vq_req);
	if (err) {
		goto bail3;
	}

	/* Process the reply */
	reply = (struct c2wr_qp_create_rep *) (unsigned long) (vq_req->reply_msg);
	if (!reply) {
		err = -ENOMEM;
		goto bail3;
	}

	if ((err = c2_wr_get_result(reply)) != 0) {
		goto bail4;
	}

	/* Fill in the kernel QP struct */
	atomic_set(&qp->refcount, 1);
	qp->adapter_handle = reply->qp_handle;
	qp->state = IB_QPS_RESET;
	qp->send_sgl_depth = qp_attrs->cap.max_send_sge;
	qp->rdma_write_sgl_depth = qp_attrs->cap.max_send_sge;
	qp->recv_sgl_depth = qp_attrs->cap.max_recv_sge;
	init_waitqueue_head(&qp->wait);

	/* Initialize the SQ MQ */
	q_size = be32_to_cpu(reply->sq_depth);
	msg_size = be32_to_cpu(reply->sq_msg_size);
	peer_pa = c2dev->pa + be32_to_cpu(reply->sq_mq_start);
	mmap_size = PAGE_ALIGN(sizeof(struct c2_mq_shared) + msg_size * q_size);
	mmap = ioremap_nocache(peer_pa, mmap_size);
	if (!mmap) {
		err = -ENOMEM;
		goto bail5;
	}

	c2_mq_req_init(&qp->sq_mq,
		       be32_to_cpu(reply->sq_mq_index),
		       q_size,
		       msg_size,
		       mmap + sizeof(struct c2_mq_shared),	/* pool start */
		       mmap,				/* peer */
		       C2_MQ_ADAPTER_TARGET);

	/* Initialize the RQ mq */
	q_size = be32_to_cpu(reply->rq_depth);
	msg_size = be32_to_cpu(reply->rq_msg_size);
	peer_pa = c2dev->pa + be32_to_cpu(reply->rq_mq_start);
	mmap_size = PAGE_ALIGN(sizeof(struct c2_mq_shared) + msg_size * q_size);
	mmap = ioremap_nocache(peer_pa, mmap_size);
	if (!mmap) {
		err = -ENOMEM;
		goto bail6;
	}

	c2_mq_req_init(&qp->rq_mq,
		       be32_to_cpu(reply->rq_mq_index),
		       q_size,
		       msg_size,
		       mmap + sizeof(struct c2_mq_shared),	/* pool start */
		       mmap,				/* peer */
		       C2_MQ_ADAPTER_TARGET);

	vq_repbuf_free(c2dev, reply);
	vq_req_free(c2dev, vq_req);

	return 0;

      bail6:
	iounmap(qp->sq_mq.peer);
      bail5:
	destroy_qp(c2dev, qp);
      bail4:
	vq_repbuf_free(c2dev, reply);
      bail3:
	vq_req_free(c2dev, vq_req);
      bail2:
	c2_free_mqsp(qp->rq_mq.shared);
      bail1:
	c2_free_mqsp(qp->sq_mq.shared);
      bail0:
	c2_free_qpn(c2dev, qp->qpn);
	return err;
}

static inline void c2_lock_cqs(struct c2_cq *send_cq, struct c2_cq *recv_cq)
{
	if (send_cq == recv_cq)
		spin_lock_irq(&send_cq->lock);
	else if (send_cq > recv_cq) {
		spin_lock_irq(&send_cq->lock);
		spin_lock_nested(&recv_cq->lock, SINGLE_DEPTH_NESTING);
	} else {
		spin_lock_irq(&recv_cq->lock);
		spin_lock_nested(&send_cq->lock, SINGLE_DEPTH_NESTING);
	}
}

static inline void c2_unlock_cqs(struct c2_cq *send_cq, struct c2_cq *recv_cq)
{
	if (send_cq == recv_cq)
		spin_unlock_irq(&send_cq->lock);
	else if (send_cq > recv_cq) {
		spin_unlock(&recv_cq->lock);
		spin_unlock_irq(&send_cq->lock);
	} else {
		spin_unlock(&send_cq->lock);
		spin_unlock_irq(&recv_cq->lock);
	}
}

void c2_free_qp(struct c2_dev *c2dev, struct c2_qp *qp)
{
	struct c2_cq *send_cq;
	struct c2_cq *recv_cq;

	send_cq = to_c2cq(qp->ibqp.send_cq);
	recv_cq = to_c2cq(qp->ibqp.recv_cq);

	/*
	 * Lock CQs here, so that CQ polling code can do QP lookup
	 * without taking a lock.
	 */
	c2_lock_cqs(send_cq, recv_cq);
	c2_free_qpn(c2dev, qp->qpn);
	c2_unlock_cqs(send_cq, recv_cq);

	/*
	 * Destroy qp in the rnic...
	 */
	destroy_qp(c2dev, qp);

	/*
	 * Mark any unreaped CQEs as null and void.
	 */
	c2_cq_clean(c2dev, qp, send_cq->cqn);
	if (send_cq != recv_cq)
		c2_cq_clean(c2dev, qp, recv_cq->cqn);
	/*
	 * Unmap the MQs and return the shared pointers
	 * to the message pool.
	 */
	iounmap(qp->sq_mq.peer);
	iounmap(qp->rq_mq.peer);
	c2_free_mqsp(qp->sq_mq.shared);
	c2_free_mqsp(qp->rq_mq.shared);

	atomic_dec(&qp->refcount);
	wait_event(qp->wait, !atomic_read(&qp->refcount));
}

/*
 * Function: move_sgl
 *
 * Description:
 * Move an SGL from the user's work request struct into a CCIL Work Request
 * message, swapping to WR byte order and ensure the total length doesn't
 * overflow.
 *
 * IN:
 * dst		- ptr to CCIL Work Request message SGL memory.
 * src		- ptr to the consumers SGL memory.
 *
 * OUT: none
 *
 * Return:
 * CCIL status codes.
 */
static int
move_sgl(struct c2_data_addr * dst, struct ib_sge *src, int count, u32 * p_len,
	 u8 * actual_count)
{
	u32 tot = 0;		/* running total */
	u8 acount = 0;		/* running total non-0 len sge's */

	while (count > 0) {
		/*
		 * If the addition of this SGE causes the
		 * total SGL length to exceed 2^32-1, then
		 * fail-n-bail.
		 *
		 * If the current total plus the next element length
		 * wraps, then it will go negative and be less than the
		 * current total...
		 */
		if ((tot + src->length) < tot) {
			return -EINVAL;
		}
		/*
		 * Bug: 1456 (as well as 1498 & 1643)
		 * Skip over any sge's supplied with len=0
		 */
		if (src->length) {
			tot += src->length;
			dst->stag = cpu_to_be32(src->lkey);
			dst->to = cpu_to_be64(src->addr);
			dst->length = cpu_to_be32(src->length);
			dst++;
			acount++;
		}
		src++;
		count--;
	}

	if (acount == 0) {
		/*
		 * Bug: 1476 (as well as 1498, 1456 and 1643)
		 * Setup the SGL in the WR to make it easier for the RNIC.
		 * This way, the FW doesn't have to deal with special cases.
		 * Setting length=0 should be sufficient.
		 */
		dst->stag = 0;
		dst->to = 0;
		dst->length = 0;
	}

	*p_len = tot;
	*actual_count = acount;
	return 0;
}

/*
 * Function: c2_activity (private function)
 *
 * Description:
 * Post an mq index to the host->adapter activity fifo.
 *
 * IN:
 * c2dev	- ptr to c2dev structure
 * mq_index	- mq index to post
 * shared	- value most recently written to shared
 *
 * OUT:
 *
 * Return:
 * none
 */
static inline void c2_activity(struct c2_dev *c2dev, u32 mq_index, u16 shared)
{
	/*
	 * First read the register to see if the FIFO is full, and if so,
	 * spin until it's not.  This isn't perfect -- there is no
	 * synchronization among the clients of the register, but in
	 * practice it prevents multiple CPU from hammering the bus
	 * with PCI RETRY. Note that when this does happen, the card
	 * cannot get on the bus and the card and system hang in a
	 * deadlock -- thus the need for this code. [TOT]
	 */
	while (readl(c2dev->regs + PCI_BAR0_ADAPTER_HINT) & 0x80000000)
		udelay(10);

	__raw_writel(C2_HINT_MAKE(mq_index, shared),
		     c2dev->regs + PCI_BAR0_ADAPTER_HINT);
}

/*
 * Function: qp_wr_post
 *
 * Description:
 * This in-line function allocates a MQ msg, then moves the host-copy of
 * the completed WR into msg.  Then it posts the message.
 *
 * IN:
 * q		- ptr to user MQ.
 * wr		- ptr to host-copy of the WR.
 * qp		- ptr to user qp
 * size		- Number of bytes to post.  Assumed to be divisible by 4.
 *
 * OUT: none
 *
 * Return:
 * CCIL status codes.
 */
static int qp_wr_post(struct c2_mq *q, union c2wr * wr, struct c2_qp *qp, u32 size)
{
	union c2wr *msg;

	msg = c2_mq_alloc(q);
	if (msg == NULL) {
		return -EINVAL;
	}
#ifdef CCMSGMAGIC
	((c2wr_hdr_t *) wr)->magic = cpu_to_be32(CCWR_MAGIC);
#endif

	/*
	 * Since all header fields in the WR are the same as the
	 * CQE, set the following so the adapter need not.
	 */
	c2_wr_set_result(wr, CCERR_PENDING);

	/*
	 * Copy the wr down to the adapter
	 */
	memcpy((void *) msg, (void *) wr, size);

	c2_mq_produce(q);
	return 0;
}


int c2_post_send(struct ib_qp *ibqp, struct ib_send_wr *ib_wr,
		 struct ib_send_wr **bad_wr)
{
	struct c2_dev *c2dev = to_c2dev(ibqp->device);
	struct c2_qp *qp = to_c2qp(ibqp);
	union c2wr wr;
	unsigned long lock_flags;
	int err = 0;

	u32 flags;
	u32 tot_len;
	u8 actual_sge_count;
	u32 msg_size;

	if (qp->state > IB_QPS_RTS) {
		err = -EINVAL;
		goto out;
	}

	while (ib_wr) {

		flags = 0;
		wr.sqwr.sq_hdr.user_hdr.hdr.context = ib_wr->wr_id;
		if (ib_wr->send_flags & IB_SEND_SIGNALED) {
			flags |= SQ_SIGNALED;
		}

		switch (ib_wr->opcode) {
		case IB_WR_SEND:
		case IB_WR_SEND_WITH_INV:
			if (ib_wr->opcode == IB_WR_SEND) {
				if (ib_wr->send_flags & IB_SEND_SOLICITED)
					c2_wr_set_id(&wr, C2_WR_TYPE_SEND_SE);
				else
					c2_wr_set_id(&wr, C2_WR_TYPE_SEND);
				wr.sqwr.send.remote_stag = 0;
			} else {
				if (ib_wr->send_flags & IB_SEND_SOLICITED)
					c2_wr_set_id(&wr, C2_WR_TYPE_SEND_SE_INV);
				else
					c2_wr_set_id(&wr, C2_WR_TYPE_SEND_INV);
				wr.sqwr.send.remote_stag =
					cpu_to_be32(ib_wr->ex.invalidate_rkey);
			}

			msg_size = sizeof(struct c2wr_send_req) +
				sizeof(struct c2_data_addr) * ib_wr->num_sge;
			if (ib_wr->num_sge > qp->send_sgl_depth) {
				err = -EINVAL;
				break;
			}
			if (ib_wr->send_flags & IB_SEND_FENCE) {
				flags |= SQ_READ_FENCE;
			}
			err = move_sgl((struct c2_data_addr *) & (wr.sqwr.send.data),
				       ib_wr->sg_list,
				       ib_wr->num_sge,
				       &tot_len, &actual_sge_count);
			wr.sqwr.send.sge_len = cpu_to_be32(tot_len);
			c2_wr_set_sge_count(&wr, actual_sge_count);
			break;
		case IB_WR_RDMA_WRITE:
			c2_wr_set_id(&wr, C2_WR_TYPE_RDMA_WRITE);
			msg_size = sizeof(struct c2wr_rdma_write_req) +
			    (sizeof(struct c2_data_addr) * ib_wr->num_sge);
			if (ib_wr->num_sge > qp->rdma_write_sgl_depth) {
				err = -EINVAL;
				break;
			}
			if (ib_wr->send_flags & IB_SEND_FENCE) {
				flags |= SQ_READ_FENCE;
			}
			wr.sqwr.rdma_write.remote_stag =
			    cpu_to_be32(ib_wr->wr.rdma.rkey);
			wr.sqwr.rdma_write.remote_to =
			    cpu_to_be64(ib_wr->wr.rdma.remote_addr);
			err = move_sgl((struct c2_data_addr *)
				       & (wr.sqwr.rdma_write.data),
				       ib_wr->sg_list,
				       ib_wr->num_sge,
				       &tot_len, &actual_sge_count);
			wr.sqwr.rdma_write.sge_len = cpu_to_be32(tot_len);
			c2_wr_set_sge_count(&wr, actual_sge_count);
			break;
		case IB_WR_RDMA_READ:
			c2_wr_set_id(&wr, C2_WR_TYPE_RDMA_READ);
			msg_size = sizeof(struct c2wr_rdma_read_req);

			/* IWarp only suppots 1 sge for RDMA reads */
			if (ib_wr->num_sge > 1) {
				err = -EINVAL;
				break;
			}

			/*
			 * Move the local and remote stag/to/len into the WR.
			 */
			wr.sqwr.rdma_read.local_stag =
			    cpu_to_be32(ib_wr->sg_list->lkey);
			wr.sqwr.rdma_read.local_to =
			    cpu_to_be64(ib_wr->sg_list->addr);
			wr.sqwr.rdma_read.remote_stag =
			    cpu_to_be32(ib_wr->wr.rdma.rkey);
			wr.sqwr.rdma_read.remote_to =
			    cpu_to_be64(ib_wr->wr.rdma.remote_addr);
			wr.sqwr.rdma_read.length =
			    cpu_to_be32(ib_wr->sg_list->length);
			break;
		default:
			/* error */
			msg_size = 0;
			err = -EINVAL;
			break;
		}

		/*
		 * If we had an error on the last wr build, then
		 * break out.  Possible errors include bogus WR
		 * type, and a bogus SGL length...
		 */
		if (err) {
			break;
		}

		/*
		 * Store flags
		 */
		c2_wr_set_flags(&wr, flags);

		/*
		 * Post the puppy!
		 */
		spin_lock_irqsave(&qp->lock, lock_flags);
		err = qp_wr_post(&qp->sq_mq, &wr, qp, msg_size);
		if (err) {
			spin_unlock_irqrestore(&qp->lock, lock_flags);
			break;
		}

		/*
		 * Enqueue mq index to activity FIFO.
		 */
		c2_activity(c2dev, qp->sq_mq.index, qp->sq_mq.hint_count);
		spin_unlock_irqrestore(&qp->lock, lock_flags);

		ib_wr = ib_wr->next;
	}

out:
	if (err)
		*bad_wr = ib_wr;
	return err;
}

int c2_post_receive(struct ib_qp *ibqp, struct ib_recv_wr *ib_wr,
		    struct ib_recv_wr **bad_wr)
{
	struct c2_dev *c2dev = to_c2dev(ibqp->device);
	struct c2_qp *qp = to_c2qp(ibqp);
	union c2wr wr;
	unsigned long lock_flags;
	int err = 0;

	if (qp->state > IB_QPS_RTS) {
		err = -EINVAL;
		goto out;
	}

	/*
	 * Try and post each work request
	 */
	while (ib_wr) {
		u32 tot_len;
		u8 actual_sge_count;

		if (ib_wr->num_sge > qp->recv_sgl_depth) {
			err = -EINVAL;
			break;
		}

		/*
		 * Create local host-copy of the WR
		 */
		wr.rqwr.rq_hdr.user_hdr.hdr.context = ib_wr->wr_id;
		c2_wr_set_id(&wr, CCWR_RECV);
		c2_wr_set_flags(&wr, 0);

		/* sge_count is limited to eight bits. */
		BUG_ON(ib_wr->num_sge >= 256);
		err = move_sgl((struct c2_data_addr *) & (wr.rqwr.data),
			       ib_wr->sg_list,
			       ib_wr->num_sge, &tot_len, &actual_sge_count);
		c2_wr_set_sge_count(&wr, actual_sge_count);

		/*
		 * If we had an error on the last wr build, then
		 * break out.  Possible errors include bogus WR
		 * type, and a bogus SGL length...
		 */
		if (err) {
			break;
		}

		spin_lock_irqsave(&qp->lock, lock_flags);
		err = qp_wr_post(&qp->rq_mq, &wr, qp, qp->rq_mq.msg_size);
		if (err) {
			spin_unlock_irqrestore(&qp->lock, lock_flags);
			break;
		}

		/*
		 * Enqueue mq index to activity FIFO
		 */
		c2_activity(c2dev, qp->rq_mq.index, qp->rq_mq.hint_count);
		spin_unlock_irqrestore(&qp->lock, lock_flags);

		ib_wr = ib_wr->next;
	}

out:
	if (err)
		*bad_wr = ib_wr;
	return err;
}

void __devinit c2_init_qp_table(struct c2_dev *c2dev)
{
	spin_lock_init(&c2dev->qp_table.lock);
	idr_init(&c2dev->qp_table.idr);
}

void __devexit c2_cleanup_qp_table(struct c2_dev *c2dev)
{
	idr_destroy(&c2dev->qp_table.idr);
}
