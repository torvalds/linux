/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Intel SCIF driver.
 *
 */
#include "scif_main.h"
#include "scif_map.h"

void scif_cleanup_ep_qp(struct scif_endpt *ep)
{
	struct scif_qp *qp = ep->qp_info.qp;

	if (qp->outbound_q.rb_base) {
		scif_iounmap((void *)qp->outbound_q.rb_base,
			     qp->outbound_q.size, ep->remote_dev);
		qp->outbound_q.rb_base = NULL;
	}
	if (qp->remote_qp) {
		scif_iounmap((void *)qp->remote_qp,
			     sizeof(struct scif_qp), ep->remote_dev);
		qp->remote_qp = NULL;
	}
	if (qp->local_qp) {
		scif_unmap_single(qp->local_qp, ep->remote_dev,
				  sizeof(struct scif_qp));
		qp->local_qp = 0x0;
	}
	if (qp->local_buf) {
		scif_unmap_single(qp->local_buf, ep->remote_dev,
				  SCIF_ENDPT_QP_SIZE);
		qp->local_buf = 0;
	}
}

void scif_teardown_ep(void *endpt)
{
	struct scif_endpt *ep = endpt;
	struct scif_qp *qp = ep->qp_info.qp;

	if (qp) {
		spin_lock(&ep->lock);
		scif_cleanup_ep_qp(ep);
		spin_unlock(&ep->lock);
		kfree(qp->inbound_q.rb_base);
		kfree(qp);
	}
}

/*
 * Enqueue the endpoint to the zombie list for cleanup.
 * The endpoint should not be accessed once this API returns.
 */
void scif_add_epd_to_zombie_list(struct scif_endpt *ep, bool eplock_held)
{
	if (!eplock_held)
		mutex_lock(&scif_info.eplock);
	spin_lock(&ep->lock);
	ep->state = SCIFEP_ZOMBIE;
	spin_unlock(&ep->lock);
	list_add_tail(&ep->list, &scif_info.zombie);
	scif_info.nr_zombies++;
	if (!eplock_held)
		mutex_unlock(&scif_info.eplock);
	schedule_work(&scif_info.misc_work);
}

static struct scif_endpt *scif_find_listen_ep(u16 port)
{
	struct scif_endpt *ep = NULL;
	struct list_head *pos, *tmpq;

	mutex_lock(&scif_info.eplock);
	list_for_each_safe(pos, tmpq, &scif_info.listen) {
		ep = list_entry(pos, struct scif_endpt, list);
		if (ep->port.port == port) {
			mutex_unlock(&scif_info.eplock);
			return ep;
		}
	}
	mutex_unlock(&scif_info.eplock);
	return NULL;
}

void scif_cleanup_zombie_epd(void)
{
	struct list_head *pos, *tmpq;
	struct scif_endpt *ep;

	mutex_lock(&scif_info.eplock);
	list_for_each_safe(pos, tmpq, &scif_info.zombie) {
		ep = list_entry(pos, struct scif_endpt, list);
		if (scif_rma_ep_can_uninit(ep)) {
			list_del(pos);
			scif_info.nr_zombies--;
			put_iova_domain(&ep->rma_info.iovad);
			kfree(ep);
		}
	}
	mutex_unlock(&scif_info.eplock);
}

/**
 * scif_cnctreq() - Respond to SCIF_CNCT_REQ interrupt message
 * @msg:        Interrupt message
 *
 * This message is initiated by the remote node to request a connection
 * to the local node.  This function looks for an end point in the
 * listen state on the requested port id.
 *
 * If it finds a listening port it places the connect request on the
 * listening end points queue and wakes up any pending accept calls.
 *
 * If it does not find a listening end point it sends a connection
 * reject message to the remote node.
 */
void scif_cnctreq(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_endpt *ep = NULL;
	struct scif_conreq *conreq;

	conreq = kmalloc(sizeof(*conreq), GFP_KERNEL);
	if (!conreq)
		/* Lack of resources so reject the request. */
		goto conreq_sendrej;

	ep = scif_find_listen_ep(msg->dst.port);
	if (!ep)
		/*  Send reject due to no listening ports */
		goto conreq_sendrej_free;
	else
		spin_lock(&ep->lock);

	if (ep->backlog <= ep->conreqcnt) {
		/*  Send reject due to too many pending requests */
		spin_unlock(&ep->lock);
		goto conreq_sendrej_free;
	}

	conreq->msg = *msg;
	list_add_tail(&conreq->list, &ep->conlist);
	ep->conreqcnt++;
	wake_up_interruptible(&ep->conwq);
	spin_unlock(&ep->lock);
	return;

conreq_sendrej_free:
	kfree(conreq);
conreq_sendrej:
	msg->uop = SCIF_CNCT_REJ;
	scif_nodeqp_send(&scif_dev[msg->src.node], msg);
}

/**
 * scif_cnctgnt() - Respond to SCIF_CNCT_GNT interrupt message
 * @msg:        Interrupt message
 *
 * An accept() on the remote node has occurred and sent this message
 * to indicate success.  Place the end point in the MAPPING state and
 * save the remote nodes memory information.  Then wake up the connect
 * request so it can finish.
 */
void scif_cnctgnt(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_endpt *ep = (struct scif_endpt *)msg->payload[0];

	spin_lock(&ep->lock);
	if (SCIFEP_CONNECTING == ep->state) {
		ep->peer.node = msg->src.node;
		ep->peer.port = msg->src.port;
		ep->qp_info.gnt_pld = msg->payload[1];
		ep->remote_ep = msg->payload[2];
		ep->state = SCIFEP_MAPPING;

		wake_up(&ep->conwq);
	}
	spin_unlock(&ep->lock);
}

/**
 * scif_cnctgnt_ack() - Respond to SCIF_CNCT_GNTACK interrupt message
 * @msg:        Interrupt message
 *
 * The remote connection request has finished mapping the local memory.
 * Place the connection in the connected state and wake up the pending
 * accept() call.
 */
void scif_cnctgnt_ack(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_endpt *ep = (struct scif_endpt *)msg->payload[0];

	mutex_lock(&scif_info.connlock);
	spin_lock(&ep->lock);
	/* New ep is now connected with all resources set. */
	ep->state = SCIFEP_CONNECTED;
	list_add_tail(&ep->list, &scif_info.connected);
	wake_up(&ep->conwq);
	spin_unlock(&ep->lock);
	mutex_unlock(&scif_info.connlock);
}

/**
 * scif_cnctgnt_nack() - Respond to SCIF_CNCT_GNTNACK interrupt message
 * @msg:        Interrupt message
 *
 * The remote connection request failed to map the local memory it was sent.
 * Place the end point in the CLOSING state to indicate it and wake up
 * the pending accept();
 */
void scif_cnctgnt_nack(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_endpt *ep = (struct scif_endpt *)msg->payload[0];

	spin_lock(&ep->lock);
	ep->state = SCIFEP_CLOSING;
	wake_up(&ep->conwq);
	spin_unlock(&ep->lock);
}

/**
 * scif_cnctrej() - Respond to SCIF_CNCT_REJ interrupt message
 * @msg:        Interrupt message
 *
 * The remote end has rejected the connection request.  Set the end
 * point back to the bound state and wake up the pending connect().
 */
void scif_cnctrej(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_endpt *ep = (struct scif_endpt *)msg->payload[0];

	spin_lock(&ep->lock);
	if (SCIFEP_CONNECTING == ep->state) {
		ep->state = SCIFEP_BOUND;
		wake_up(&ep->conwq);
	}
	spin_unlock(&ep->lock);
}

/**
 * scif_discnct() - Respond to SCIF_DISCNCT interrupt message
 * @msg:        Interrupt message
 *
 * The remote node has indicated close() has been called on its end
 * point.  Remove the local end point from the connected list, set its
 * state to disconnected and ensure accesses to the remote node are
 * shutdown.
 *
 * When all accesses to the remote end have completed then send a
 * DISCNT_ACK to indicate it can remove its resources and complete
 * the close routine.
 */
void scif_discnct(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_endpt *ep = NULL;
	struct scif_endpt *tmpep;
	struct list_head *pos, *tmpq;

	mutex_lock(&scif_info.connlock);
	list_for_each_safe(pos, tmpq, &scif_info.connected) {
		tmpep = list_entry(pos, struct scif_endpt, list);
		/*
		 * The local ep may have sent a disconnect and and been closed
		 * due to a message response time out. It may have been
		 * allocated again and formed a new connection so we want to
		 * check if the remote ep matches
		 */
		if (((u64)tmpep == msg->payload[1]) &&
		    ((u64)tmpep->remote_ep == msg->payload[0])) {
			list_del(pos);
			ep = tmpep;
			spin_lock(&ep->lock);
			break;
		}
	}

	/*
	 * If the terminated end is not found then this side started closing
	 * before the other side sent the disconnect.  If so the ep will no
	 * longer be on the connected list.  Regardless the other side
	 * needs to be acked to let it know close is complete.
	 */
	if (!ep) {
		mutex_unlock(&scif_info.connlock);
		goto discnct_ack;
	}

	ep->state = SCIFEP_DISCONNECTED;
	list_add_tail(&ep->list, &scif_info.disconnected);

	wake_up_interruptible(&ep->sendwq);
	wake_up_interruptible(&ep->recvwq);
	spin_unlock(&ep->lock);
	mutex_unlock(&scif_info.connlock);

discnct_ack:
	msg->uop = SCIF_DISCNT_ACK;
	scif_nodeqp_send(&scif_dev[msg->src.node], msg);
}

/**
 * scif_discnct_ack() - Respond to SCIF_DISCNT_ACK interrupt message
 * @msg:        Interrupt message
 *
 * Remote side has indicated it has not more references to local resources
 */
void scif_discnt_ack(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_endpt *ep = (struct scif_endpt *)msg->payload[0];

	spin_lock(&ep->lock);
	ep->state = SCIFEP_DISCONNECTED;
	spin_unlock(&ep->lock);
	complete(&ep->discon);
}

/**
 * scif_clientsend() - Respond to SCIF_CLIENT_SEND interrupt message
 * @msg:        Interrupt message
 *
 * Remote side is confirming send or receive interrupt handling is complete.
 */
void scif_clientsend(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_endpt *ep = (struct scif_endpt *)msg->payload[0];

	spin_lock(&ep->lock);
	if (SCIFEP_CONNECTED == ep->state)
		wake_up_interruptible(&ep->recvwq);
	spin_unlock(&ep->lock);
}

/**
 * scif_clientrcvd() - Respond to SCIF_CLIENT_RCVD interrupt message
 * @msg:        Interrupt message
 *
 * Remote side is confirming send or receive interrupt handling is complete.
 */
void scif_clientrcvd(struct scif_dev *scifdev, struct scifmsg *msg)
{
	struct scif_endpt *ep = (struct scif_endpt *)msg->payload[0];

	spin_lock(&ep->lock);
	if (SCIFEP_CONNECTED == ep->state)
		wake_up_interruptible(&ep->sendwq);
	spin_unlock(&ep->lock);
}
