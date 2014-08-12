/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005 Cisco Systems, Inc. All rights reserved.
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
#include <linux/gfp.h>

#include "c2.h"
#include "c2_vq.h"
#include "c2_status.h"

#define C2_CQ_MSG_SIZE ((sizeof(struct c2wr_ce) + 32-1) & ~(32-1))

static struct c2_cq *c2_cq_get(struct c2_dev *c2dev, int cqn)
{
	struct c2_cq *cq;
	unsigned long flags;

	spin_lock_irqsave(&c2dev->lock, flags);
	cq = c2dev->qptr_array[cqn];
	if (!cq) {
		spin_unlock_irqrestore(&c2dev->lock, flags);
		return NULL;
	}
	atomic_inc(&cq->refcount);
	spin_unlock_irqrestore(&c2dev->lock, flags);
	return cq;
}

static void c2_cq_put(struct c2_cq *cq)
{
	if (atomic_dec_and_test(&cq->refcount))
		wake_up(&cq->wait);
}

void c2_cq_event(struct c2_dev *c2dev, u32 mq_index)
{
	struct c2_cq *cq;

	cq = c2_cq_get(c2dev, mq_index);
	if (!cq) {
		printk("discarding events on destroyed CQN=%d\n", mq_index);
		return;
	}

	(*cq->ibcq.comp_handler) (&cq->ibcq, cq->ibcq.cq_context);
	c2_cq_put(cq);
}

void c2_cq_clean(struct c2_dev *c2dev, struct c2_qp *qp, u32 mq_index)
{
	struct c2_cq *cq;
	struct c2_mq *q;

	cq = c2_cq_get(c2dev, mq_index);
	if (!cq)
		return;

	spin_lock_irq(&cq->lock);
	q = &cq->mq;
	if (q && !c2_mq_empty(q)) {
		u16 priv = q->priv;
		struct c2wr_ce *msg;

		while (priv != be16_to_cpu(*q->shared)) {
			msg = (struct c2wr_ce *)
				(q->msg_pool.host + priv * q->msg_size);
			if (msg->qp_user_context == (u64) (unsigned long) qp) {
				msg->qp_user_context = (u64) 0;
			}
			priv = (priv + 1) % q->q_size;
		}
	}
	spin_unlock_irq(&cq->lock);
	c2_cq_put(cq);
}

static inline enum ib_wc_status c2_cqe_status_to_openib(u8 status)
{
	switch (status) {
	case C2_OK:
		return IB_WC_SUCCESS;
	case CCERR_FLUSHED:
		return IB_WC_WR_FLUSH_ERR;
	case CCERR_BASE_AND_BOUNDS_VIOLATION:
		return IB_WC_LOC_PROT_ERR;
	case CCERR_ACCESS_VIOLATION:
		return IB_WC_LOC_ACCESS_ERR;
	case CCERR_TOTAL_LENGTH_TOO_BIG:
		return IB_WC_LOC_LEN_ERR;
	case CCERR_INVALID_WINDOW:
		return IB_WC_MW_BIND_ERR;
	default:
		return IB_WC_GENERAL_ERR;
	}
}


static inline int c2_poll_one(struct c2_dev *c2dev,
			      struct c2_cq *cq, struct ib_wc *entry)
{
	struct c2wr_ce *ce;
	struct c2_qp *qp;
	int is_recv = 0;

	ce = c2_mq_consume(&cq->mq);
	if (!ce) {
		return -EAGAIN;
	}

	/*
	 * if the qp returned is null then this qp has already
	 * been freed and we are unable process the completion.
	 * try pulling the next message
	 */
	while ((qp =
		(struct c2_qp *) (unsigned long) ce->qp_user_context) == NULL) {
		c2_mq_free(&cq->mq);
		ce = c2_mq_consume(&cq->mq);
		if (!ce)
			return -EAGAIN;
	}

	entry->status = c2_cqe_status_to_openib(c2_wr_get_result(ce));
	entry->wr_id = ce->hdr.context;
	entry->qp = &qp->ibqp;
	entry->wc_flags = 0;
	entry->slid = 0;
	entry->sl = 0;
	entry->src_qp = 0;
	entry->dlid_path_bits = 0;
	entry->pkey_index = 0;

	switch (c2_wr_get_id(ce)) {
	case C2_WR_TYPE_SEND:
		entry->opcode = IB_WC_SEND;
		break;
	case C2_WR_TYPE_RDMA_WRITE:
		entry->opcode = IB_WC_RDMA_WRITE;
		break;
	case C2_WR_TYPE_RDMA_READ:
		entry->opcode = IB_WC_RDMA_READ;
		break;
	case C2_WR_TYPE_BIND_MW:
		entry->opcode = IB_WC_BIND_MW;
		break;
	case C2_WR_TYPE_RECV:
		entry->byte_len = be32_to_cpu(ce->bytes_rcvd);
		entry->opcode = IB_WC_RECV;
		is_recv = 1;
		break;
	default:
		break;
	}

	/* consume the WQEs */
	if (is_recv)
		c2_mq_lconsume(&qp->rq_mq, 1);
	else
		c2_mq_lconsume(&qp->sq_mq,
			       be32_to_cpu(c2_wr_get_wqe_count(ce)) + 1);

	/* free the message */
	c2_mq_free(&cq->mq);

	return 0;
}

int c2_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *entry)
{
	struct c2_dev *c2dev = to_c2dev(ibcq->device);
	struct c2_cq *cq = to_c2cq(ibcq);
	unsigned long flags;
	int npolled, err;

	spin_lock_irqsave(&cq->lock, flags);

	for (npolled = 0; npolled < num_entries; ++npolled) {

		err = c2_poll_one(c2dev, cq, entry + npolled);
		if (err)
			break;
	}

	spin_unlock_irqrestore(&cq->lock, flags);

	return npolled;
}

int c2_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags notify_flags)
{
	struct c2_mq_shared __iomem *shared;
	struct c2_cq *cq;
	unsigned long flags;
	int ret = 0;

	cq = to_c2cq(ibcq);
	shared = cq->mq.peer;

	if ((notify_flags & IB_CQ_SOLICITED_MASK) == IB_CQ_NEXT_COMP)
		writeb(C2_CQ_NOTIFICATION_TYPE_NEXT, &shared->notification_type);
	else if ((notify_flags & IB_CQ_SOLICITED_MASK) == IB_CQ_SOLICITED)
		writeb(C2_CQ_NOTIFICATION_TYPE_NEXT_SE, &shared->notification_type);
	else
		return -EINVAL;

	writeb(CQ_WAIT_FOR_DMA | CQ_ARMED, &shared->armed);

	/*
	 * Now read back shared->armed to make the PCI
	 * write synchronous.  This is necessary for
	 * correct cq notification semantics.
	 */
	readb(&shared->armed);

	if (notify_flags & IB_CQ_REPORT_MISSED_EVENTS) {
		spin_lock_irqsave(&cq->lock, flags);
		ret = !c2_mq_empty(&cq->mq);
		spin_unlock_irqrestore(&cq->lock, flags);
	}

	return ret;
}

static void c2_free_cq_buf(struct c2_dev *c2dev, struct c2_mq *mq)
{
	dma_free_coherent(&c2dev->pcidev->dev, mq->q_size * mq->msg_size,
			  mq->msg_pool.host, dma_unmap_addr(mq, mapping));
}

static int c2_alloc_cq_buf(struct c2_dev *c2dev, struct c2_mq *mq,
			   size_t q_size, size_t msg_size)
{
	u8 *pool_start;

	if (q_size > SIZE_MAX / msg_size)
		return -EINVAL;

	pool_start = dma_alloc_coherent(&c2dev->pcidev->dev, q_size * msg_size,
					&mq->host_dma, GFP_KERNEL);
	if (!pool_start)
		return -ENOMEM;

	c2_mq_rep_init(mq,
		       0,		/* index (currently unknown) */
		       q_size,
		       msg_size,
		       pool_start,
		       NULL,	/* peer (currently unknown) */
		       C2_MQ_HOST_TARGET);

	dma_unmap_addr_set(mq, mapping, mq->host_dma);

	return 0;
}

int c2_init_cq(struct c2_dev *c2dev, int entries,
	       struct c2_ucontext *ctx, struct c2_cq *cq)
{
	struct c2wr_cq_create_req wr;
	struct c2wr_cq_create_rep *reply;
	unsigned long peer_pa;
	struct c2_vq_req *vq_req;
	int err;

	might_sleep();

	cq->ibcq.cqe = entries - 1;
	cq->is_kernel = !ctx;

	/* Allocate a shared pointer */
	cq->mq.shared = c2_alloc_mqsp(c2dev, c2dev->kern_mqsp_pool,
				      &cq->mq.shared_dma, GFP_KERNEL);
	if (!cq->mq.shared)
		return -ENOMEM;

	/* Allocate pages for the message pool */
	err = c2_alloc_cq_buf(c2dev, &cq->mq, entries + 1, C2_CQ_MSG_SIZE);
	if (err)
		goto bail0;

	vq_req = vq_req_alloc(c2dev);
	if (!vq_req) {
		err = -ENOMEM;
		goto bail1;
	}

	memset(&wr, 0, sizeof(wr));
	c2_wr_set_id(&wr, CCWR_CQ_CREATE);
	wr.hdr.context = (unsigned long) vq_req;
	wr.rnic_handle = c2dev->adapter_handle;
	wr.msg_size = cpu_to_be32(cq->mq.msg_size);
	wr.depth = cpu_to_be32(cq->mq.q_size);
	wr.shared_ht = cpu_to_be64(cq->mq.shared_dma);
	wr.msg_pool = cpu_to_be64(cq->mq.host_dma);
	wr.user_context = (u64) (unsigned long) (cq);

	vq_req_get(c2dev, vq_req);

	err = vq_send_wr(c2dev, (union c2wr *) & wr);
	if (err) {
		vq_req_put(c2dev, vq_req);
		goto bail2;
	}

	err = vq_wait_for_reply(c2dev, vq_req);
	if (err)
		goto bail2;

	reply = (struct c2wr_cq_create_rep *) (unsigned long) (vq_req->reply_msg);
	if (!reply) {
		err = -ENOMEM;
		goto bail2;
	}

	if ((err = c2_errno(reply)) != 0)
		goto bail3;

	cq->adapter_handle = reply->cq_handle;
	cq->mq.index = be32_to_cpu(reply->mq_index);

	peer_pa = c2dev->pa + be32_to_cpu(reply->adapter_shared);
	cq->mq.peer = ioremap_nocache(peer_pa, PAGE_SIZE);
	if (!cq->mq.peer) {
		err = -ENOMEM;
		goto bail3;
	}

	vq_repbuf_free(c2dev, reply);
	vq_req_free(c2dev, vq_req);

	spin_lock_init(&cq->lock);
	atomic_set(&cq->refcount, 1);
	init_waitqueue_head(&cq->wait);

	/*
	 * Use the MQ index allocated by the adapter to
	 * store the CQ in the qptr_array
	 */
	cq->cqn = cq->mq.index;
	c2dev->qptr_array[cq->cqn] = cq;

	return 0;

      bail3:
	vq_repbuf_free(c2dev, reply);
      bail2:
	vq_req_free(c2dev, vq_req);
      bail1:
	c2_free_cq_buf(c2dev, &cq->mq);
      bail0:
	c2_free_mqsp(cq->mq.shared);

	return err;
}

void c2_free_cq(struct c2_dev *c2dev, struct c2_cq *cq)
{
	int err;
	struct c2_vq_req *vq_req;
	struct c2wr_cq_destroy_req wr;
	struct c2wr_cq_destroy_rep *reply;

	might_sleep();

	/* Clear CQ from the qptr array */
	spin_lock_irq(&c2dev->lock);
	c2dev->qptr_array[cq->mq.index] = NULL;
	atomic_dec(&cq->refcount);
	spin_unlock_irq(&c2dev->lock);

	wait_event(cq->wait, !atomic_read(&cq->refcount));

	vq_req = vq_req_alloc(c2dev);
	if (!vq_req) {
		goto bail0;
	}

	memset(&wr, 0, sizeof(wr));
	c2_wr_set_id(&wr, CCWR_CQ_DESTROY);
	wr.hdr.context = (unsigned long) vq_req;
	wr.rnic_handle = c2dev->adapter_handle;
	wr.cq_handle = cq->adapter_handle;

	vq_req_get(c2dev, vq_req);

	err = vq_send_wr(c2dev, (union c2wr *) & wr);
	if (err) {
		vq_req_put(c2dev, vq_req);
		goto bail1;
	}

	err = vq_wait_for_reply(c2dev, vq_req);
	if (err)
		goto bail1;

	reply = (struct c2wr_cq_destroy_rep *) (unsigned long) (vq_req->reply_msg);
	if (reply)
		vq_repbuf_free(c2dev, reply);
      bail1:
	vq_req_free(c2dev, vq_req);
      bail0:
	if (cq->is_kernel) {
		c2_free_cq_buf(c2dev, &cq->mq);
	}

	return;
}
