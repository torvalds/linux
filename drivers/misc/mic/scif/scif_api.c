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
#include <linux/scif.h>
#include "scif_main.h"
#include "scif_map.h"

static const char * const scif_ep_states[] = {
	"Unbound",
	"Bound",
	"Listening",
	"Connected",
	"Connecting",
	"Mapping",
	"Closing",
	"Close Listening",
	"Disconnected",
	"Zombie"};

enum conn_async_state {
	ASYNC_CONN_IDLE = 1,	/* ep setup for async connect */
	ASYNC_CONN_INPROGRESS,	/* async connect in progress */
	ASYNC_CONN_FLUSH_WORK	/* async work flush in progress  */
};

scif_epd_t scif_open(void)
{
	struct scif_endpt *ep;

	might_sleep();
	ep = kzalloc(sizeof(*ep), GFP_KERNEL);
	if (!ep)
		goto err_ep_alloc;

	ep->qp_info.qp = kzalloc(sizeof(*ep->qp_info.qp), GFP_KERNEL);
	if (!ep->qp_info.qp)
		goto err_qp_alloc;

	spin_lock_init(&ep->lock);
	mutex_init(&ep->sendlock);
	mutex_init(&ep->recvlock);

	ep->state = SCIFEP_UNBOUND;
	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI open: ep %p success\n", ep);
	return ep;

err_qp_alloc:
	kfree(ep);
err_ep_alloc:
	return NULL;
}
EXPORT_SYMBOL_GPL(scif_open);

/*
 * scif_disconnect_ep - Disconnects the endpoint if found
 * @epd: The end point returned from scif_open()
 */
static struct scif_endpt *scif_disconnect_ep(struct scif_endpt *ep)
{
	struct scifmsg msg;
	struct scif_endpt *fep = NULL;
	struct scif_endpt *tmpep;
	struct list_head *pos, *tmpq;
	int err;

	/*
	 * Wake up any threads blocked in send()/recv() before closing
	 * out the connection. Grabbing and releasing the send/recv lock
	 * will ensure that any blocked senders/receivers have exited for
	 * Ring 0 endpoints. It is a Ring 0 bug to call send/recv after
	 * close. Ring 3 endpoints are not affected since close will not
	 * be called while there are IOCTLs executing.
	 */
	wake_up_interruptible(&ep->sendwq);
	wake_up_interruptible(&ep->recvwq);
	mutex_lock(&ep->sendlock);
	mutex_unlock(&ep->sendlock);
	mutex_lock(&ep->recvlock);
	mutex_unlock(&ep->recvlock);

	/* Remove from the connected list */
	mutex_lock(&scif_info.connlock);
	list_for_each_safe(pos, tmpq, &scif_info.connected) {
		tmpep = list_entry(pos, struct scif_endpt, list);
		if (tmpep == ep) {
			list_del(pos);
			fep = tmpep;
			spin_lock(&ep->lock);
			break;
		}
	}

	if (!fep) {
		/*
		 * The other side has completed the disconnect before
		 * the end point can be removed from the list. Therefore
		 * the ep lock is not locked, traverse the disconnected
		 * list to find the endpoint and release the conn lock.
		 */
		list_for_each_safe(pos, tmpq, &scif_info.disconnected) {
			tmpep = list_entry(pos, struct scif_endpt, list);
			if (tmpep == ep) {
				list_del(pos);
				break;
			}
		}
		mutex_unlock(&scif_info.connlock);
		return NULL;
	}

	init_completion(&ep->discon);
	msg.uop = SCIF_DISCNCT;
	msg.src = ep->port;
	msg.dst = ep->peer;
	msg.payload[0] = (u64)ep;
	msg.payload[1] = ep->remote_ep;

	err = scif_nodeqp_send(ep->remote_dev, &msg);
	spin_unlock(&ep->lock);
	mutex_unlock(&scif_info.connlock);

	if (!err)
		/* Wait for the remote node to respond with SCIF_DISCNT_ACK */
		wait_for_completion_timeout(&ep->discon,
					    SCIF_NODE_ALIVE_TIMEOUT);
	return ep;
}

int scif_close(scif_epd_t epd)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	struct scif_endpt *tmpep;
	struct list_head *pos, *tmpq;
	enum scif_epd_state oldstate;
	bool flush_conn;

	dev_dbg(scif_info.mdev.this_device, "SCIFAPI close: ep %p %s\n",
		ep, scif_ep_states[ep->state]);
	might_sleep();
	spin_lock(&ep->lock);
	flush_conn = (ep->conn_async_state == ASYNC_CONN_INPROGRESS);
	spin_unlock(&ep->lock);

	if (flush_conn)
		flush_work(&scif_info.conn_work);

	spin_lock(&ep->lock);
	oldstate = ep->state;

	ep->state = SCIFEP_CLOSING;

	switch (oldstate) {
	case SCIFEP_ZOMBIE:
	case SCIFEP_DISCONNECTED:
		spin_unlock(&ep->lock);
		/* Remove from the disconnected list */
		mutex_lock(&scif_info.connlock);
		list_for_each_safe(pos, tmpq, &scif_info.disconnected) {
			tmpep = list_entry(pos, struct scif_endpt, list);
			if (tmpep == ep) {
				list_del(pos);
				break;
			}
		}
		mutex_unlock(&scif_info.connlock);
		break;
	case SCIFEP_UNBOUND:
	case SCIFEP_BOUND:
	case SCIFEP_CONNECTING:
		spin_unlock(&ep->lock);
		break;
	case SCIFEP_MAPPING:
	case SCIFEP_CONNECTED:
	case SCIFEP_CLOSING:
	{
		spin_unlock(&ep->lock);
		scif_disconnect_ep(ep);
		break;
	}
	case SCIFEP_LISTENING:
	case SCIFEP_CLLISTEN:
	{
		struct scif_conreq *conreq;
		struct scifmsg msg;
		struct scif_endpt *aep;

		spin_unlock(&ep->lock);
		spin_lock(&scif_info.eplock);

		/* remove from listen list */
		list_for_each_safe(pos, tmpq, &scif_info.listen) {
			tmpep = list_entry(pos, struct scif_endpt, list);
			if (tmpep == ep)
				list_del(pos);
		}
		/* Remove any dangling accepts */
		while (ep->acceptcnt) {
			aep = list_first_entry(&ep->li_accept,
					       struct scif_endpt, liacceptlist);
			list_del(&aep->liacceptlist);
			scif_put_port(aep->port.port);
			list_for_each_safe(pos, tmpq, &scif_info.uaccept) {
				tmpep = list_entry(pos, struct scif_endpt,
						   miacceptlist);
				if (tmpep == aep) {
					list_del(pos);
					break;
				}
			}
			spin_unlock(&scif_info.eplock);
			mutex_lock(&scif_info.connlock);
			list_for_each_safe(pos, tmpq, &scif_info.connected) {
				tmpep = list_entry(pos,
						   struct scif_endpt, list);
				if (tmpep == aep) {
					list_del(pos);
					break;
				}
			}
			list_for_each_safe(pos, tmpq, &scif_info.disconnected) {
				tmpep = list_entry(pos,
						   struct scif_endpt, list);
				if (tmpep == aep) {
					list_del(pos);
					break;
				}
			}
			mutex_unlock(&scif_info.connlock);
			scif_teardown_ep(aep);
			spin_lock(&scif_info.eplock);
			scif_add_epd_to_zombie_list(aep, SCIF_EPLOCK_HELD);
			ep->acceptcnt--;
		}

		spin_lock(&ep->lock);
		spin_unlock(&scif_info.eplock);

		/* Remove and reject any pending connection requests. */
		while (ep->conreqcnt) {
			conreq = list_first_entry(&ep->conlist,
						  struct scif_conreq, list);
			list_del(&conreq->list);

			msg.uop = SCIF_CNCT_REJ;
			msg.dst.node = conreq->msg.src.node;
			msg.dst.port = conreq->msg.src.port;
			msg.payload[0] = conreq->msg.payload[0];
			msg.payload[1] = conreq->msg.payload[1];
			/*
			 * No Error Handling on purpose for scif_nodeqp_send().
			 * If the remote node is lost we still want free the
			 * connection requests on the self node.
			 */
			scif_nodeqp_send(&scif_dev[conreq->msg.src.node],
					 &msg);
			ep->conreqcnt--;
			kfree(conreq);
		}

		spin_unlock(&ep->lock);
		/* If a kSCIF accept is waiting wake it up */
		wake_up_interruptible(&ep->conwq);
		break;
	}
	}
	scif_put_port(ep->port.port);
	scif_teardown_ep(ep);
	scif_add_epd_to_zombie_list(ep, !SCIF_EPLOCK_HELD);
	return 0;
}
EXPORT_SYMBOL_GPL(scif_close);

/**
 * scif_flush() - Wakes up any blocking accepts. The endpoint will no longer
 *			accept new connections.
 * @epd: The end point returned from scif_open()
 */
int __scif_flush(scif_epd_t epd)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;

	switch (ep->state) {
	case SCIFEP_LISTENING:
	{
		ep->state = SCIFEP_CLLISTEN;

		/* If an accept is waiting wake it up */
		wake_up_interruptible(&ep->conwq);
		break;
	}
	default:
		break;
	}
	return 0;
}

int scif_bind(scif_epd_t epd, u16 pn)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	int ret = 0;
	int tmp;

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI bind: ep %p %s requested port number %d\n",
		ep, scif_ep_states[ep->state], pn);
	if (pn) {
		/*
		 * Similar to IETF RFC 1700, SCIF ports below
		 * SCIF_ADMIN_PORT_END can only be bound by system (or root)
		 * processes or by processes executed by privileged users.
		 */
		if (pn < SCIF_ADMIN_PORT_END && !capable(CAP_SYS_ADMIN)) {
			ret = -EACCES;
			goto scif_bind_admin_exit;
		}
	}

	spin_lock(&ep->lock);
	if (ep->state == SCIFEP_BOUND) {
		ret = -EINVAL;
		goto scif_bind_exit;
	} else if (ep->state != SCIFEP_UNBOUND) {
		ret = -EISCONN;
		goto scif_bind_exit;
	}

	if (pn) {
		tmp = scif_rsrv_port(pn);
		if (tmp != pn) {
			ret = -EINVAL;
			goto scif_bind_exit;
		}
	} else {
		pn = scif_get_new_port();
		if (!pn) {
			ret = -ENOSPC;
			goto scif_bind_exit;
		}
	}

	ep->state = SCIFEP_BOUND;
	ep->port.node = scif_info.nodeid;
	ep->port.port = pn;
	ep->conn_async_state = ASYNC_CONN_IDLE;
	ret = pn;
	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI bind: bound to port number %d\n", pn);
scif_bind_exit:
	spin_unlock(&ep->lock);
scif_bind_admin_exit:
	return ret;
}
EXPORT_SYMBOL_GPL(scif_bind);

int scif_listen(scif_epd_t epd, int backlog)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI listen: ep %p %s\n", ep, scif_ep_states[ep->state]);
	spin_lock(&ep->lock);
	switch (ep->state) {
	case SCIFEP_ZOMBIE:
	case SCIFEP_CLOSING:
	case SCIFEP_CLLISTEN:
	case SCIFEP_UNBOUND:
	case SCIFEP_DISCONNECTED:
		spin_unlock(&ep->lock);
		return -EINVAL;
	case SCIFEP_LISTENING:
	case SCIFEP_CONNECTED:
	case SCIFEP_CONNECTING:
	case SCIFEP_MAPPING:
		spin_unlock(&ep->lock);
		return -EISCONN;
	case SCIFEP_BOUND:
		break;
	}

	ep->state = SCIFEP_LISTENING;
	ep->backlog = backlog;

	ep->conreqcnt = 0;
	ep->acceptcnt = 0;
	INIT_LIST_HEAD(&ep->conlist);
	init_waitqueue_head(&ep->conwq);
	INIT_LIST_HEAD(&ep->li_accept);
	spin_unlock(&ep->lock);

	/*
	 * Listen status is complete so delete the qp information not needed
	 * on a listen before placing on the list of listening ep's
	 */
	scif_teardown_ep(ep);
	ep->qp_info.qp = NULL;

	spin_lock(&scif_info.eplock);
	list_add_tail(&ep->list, &scif_info.listen);
	spin_unlock(&scif_info.eplock);
	return 0;
}
EXPORT_SYMBOL_GPL(scif_listen);
