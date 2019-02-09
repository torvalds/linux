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

/*
 * File operations for anonymous inode file associated with a SCIF endpoint,
 * used in kernel mode SCIF poll. Kernel mode SCIF poll calls portions of the
 * poll API in the kernel and these take in a struct file *. Since a struct
 * file is not available to kernel mode SCIF, it uses an anonymous file for
 * this purpose.
 */
const struct file_operations scif_anon_fops = {
	.owner = THIS_MODULE,
};

scif_epd_t scif_open(void)
{
	struct scif_endpt *ep;
	int err;

	might_sleep();
	ep = kzalloc(sizeof(*ep), GFP_KERNEL);
	if (!ep)
		goto err_ep_alloc;

	ep->qp_info.qp = kzalloc(sizeof(*ep->qp_info.qp), GFP_KERNEL);
	if (!ep->qp_info.qp)
		goto err_qp_alloc;

	err = scif_anon_inode_getfile(ep);
	if (err)
		goto err_anon_inode;

	spin_lock_init(&ep->lock);
	mutex_init(&ep->sendlock);
	mutex_init(&ep->recvlock);

	scif_rma_ep_init(ep);
	ep->state = SCIFEP_UNBOUND;
	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI open: ep %p success\n", ep);
	return ep;

err_anon_inode:
	kfree(ep->qp_info.qp);
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
		dev_err(scif_info.mdev.this_device,
			"SCIFAPI close: zombie state unexpected\n");
	case SCIFEP_DISCONNECTED:
		spin_unlock(&ep->lock);
		scif_unregister_all_windows(epd);
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
		scif_unregister_all_windows(epd);
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
		mutex_lock(&scif_info.eplock);

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
			mutex_unlock(&scif_info.eplock);
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
			mutex_lock(&scif_info.eplock);
			scif_add_epd_to_zombie_list(aep, SCIF_EPLOCK_HELD);
			ep->acceptcnt--;
		}

		spin_lock(&ep->lock);
		mutex_unlock(&scif_info.eplock);

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
	scif_anon_inode_fput(ep);
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

	mutex_lock(&scif_info.eplock);
	list_add_tail(&ep->list, &scif_info.listen);
	mutex_unlock(&scif_info.eplock);
	return 0;
}
EXPORT_SYMBOL_GPL(scif_listen);

/*
 ************************************************************************
 * SCIF connection flow:
 *
 * 1) A SCIF listening endpoint can call scif_accept(..) to wait for SCIF
 *	connections via a SCIF_CNCT_REQ message
 * 2) A SCIF endpoint can initiate a SCIF connection by calling
 *	scif_connect(..) which calls scif_setup_qp_connect(..) which
 *	allocates the local qp for the endpoint ring buffer and then sends
 *	a SCIF_CNCT_REQ to the remote node and waits for a SCIF_CNCT_GNT or
 *	a SCIF_CNCT_REJ message
 * 3) The peer node handles a SCIF_CNCT_REQ via scif_cnctreq_resp(..) which
 *	wakes up any threads blocked in step 1 or sends a SCIF_CNCT_REJ
 *	message otherwise
 * 4) A thread blocked waiting for incoming connections allocates its local
 *	endpoint QP and ring buffer following which it sends a SCIF_CNCT_GNT
 *	and waits for a SCIF_CNCT_GNT(N)ACK. If the allocation fails then
 *	the node sends a SCIF_CNCT_REJ message
 * 5) Upon receipt of a SCIF_CNCT_GNT or a SCIF_CNCT_REJ message the
 *	connecting endpoint is woken up as part of handling
 *	scif_cnctgnt_resp(..) following which it maps the remote endpoints'
 *	QP, updates its outbound QP and sends a SCIF_CNCT_GNTACK message on
 *	success or a SCIF_CNCT_GNTNACK message on failure and completes
 *	the scif_connect(..) API
 * 6) Upon receipt of a SCIF_CNCT_GNT(N)ACK the accepting endpoint blocked
 *	in step 4 is woken up and completes the scif_accept(..) API
 * 7) The SCIF connection is now established between the two SCIF endpoints.
 */
static int scif_conn_func(struct scif_endpt *ep)
{
	int err = 0;
	struct scifmsg msg;
	struct device *spdev;

	err = scif_reserve_dma_chan(ep);
	if (err) {
		dev_err(&ep->remote_dev->sdev->dev,
			"%s %d err %d\n", __func__, __LINE__, err);
		ep->state = SCIFEP_BOUND;
		goto connect_error_simple;
	}
	/* Initiate the first part of the endpoint QP setup */
	err = scif_setup_qp_connect(ep->qp_info.qp, &ep->qp_info.qp_offset,
				    SCIF_ENDPT_QP_SIZE, ep->remote_dev);
	if (err) {
		dev_err(&ep->remote_dev->sdev->dev,
			"%s err %d qp_offset 0x%llx\n",
			__func__, err, ep->qp_info.qp_offset);
		ep->state = SCIFEP_BOUND;
		goto connect_error_simple;
	}

	spdev = scif_get_peer_dev(ep->remote_dev);
	if (IS_ERR(spdev)) {
		err = PTR_ERR(spdev);
		goto cleanup_qp;
	}
	/* Format connect message and send it */
	msg.src = ep->port;
	msg.dst = ep->conn_port;
	msg.uop = SCIF_CNCT_REQ;
	msg.payload[0] = (u64)ep;
	msg.payload[1] = ep->qp_info.qp_offset;
	err = _scif_nodeqp_send(ep->remote_dev, &msg);
	if (err)
		goto connect_error_dec;
	scif_put_peer_dev(spdev);
	/*
	 * Wait for the remote node to respond with SCIF_CNCT_GNT or
	 * SCIF_CNCT_REJ message.
	 */
	err = wait_event_timeout(ep->conwq, ep->state != SCIFEP_CONNECTING,
				 SCIF_NODE_ALIVE_TIMEOUT);
	if (!err) {
		dev_err(&ep->remote_dev->sdev->dev,
			"%s %d timeout\n", __func__, __LINE__);
		ep->state = SCIFEP_BOUND;
	}
	spdev = scif_get_peer_dev(ep->remote_dev);
	if (IS_ERR(spdev)) {
		err = PTR_ERR(spdev);
		goto cleanup_qp;
	}
	if (ep->state == SCIFEP_MAPPING) {
		err = scif_setup_qp_connect_response(ep->remote_dev,
						     ep->qp_info.qp,
						     ep->qp_info.gnt_pld);
		/*
		 * If the resource to map the queue are not available then
		 * we need to tell the other side to terminate the accept
		 */
		if (err) {
			dev_err(&ep->remote_dev->sdev->dev,
				"%s %d err %d\n", __func__, __LINE__, err);
			msg.uop = SCIF_CNCT_GNTNACK;
			msg.payload[0] = ep->remote_ep;
			_scif_nodeqp_send(ep->remote_dev, &msg);
			ep->state = SCIFEP_BOUND;
			goto connect_error_dec;
		}

		msg.uop = SCIF_CNCT_GNTACK;
		msg.payload[0] = ep->remote_ep;
		err = _scif_nodeqp_send(ep->remote_dev, &msg);
		if (err) {
			ep->state = SCIFEP_BOUND;
			goto connect_error_dec;
		}
		ep->state = SCIFEP_CONNECTED;
		mutex_lock(&scif_info.connlock);
		list_add_tail(&ep->list, &scif_info.connected);
		mutex_unlock(&scif_info.connlock);
		dev_dbg(&ep->remote_dev->sdev->dev,
			"SCIFAPI connect: ep %p connected\n", ep);
	} else if (ep->state == SCIFEP_BOUND) {
		dev_dbg(&ep->remote_dev->sdev->dev,
			"SCIFAPI connect: ep %p connection refused\n", ep);
		err = -ECONNREFUSED;
		goto connect_error_dec;
	}
	scif_put_peer_dev(spdev);
	return err;
connect_error_dec:
	scif_put_peer_dev(spdev);
cleanup_qp:
	scif_cleanup_ep_qp(ep);
connect_error_simple:
	return err;
}

/*
 * scif_conn_handler:
 *
 * Workqueue handler for servicing non-blocking SCIF connect
 *
 */
void scif_conn_handler(struct work_struct *work)
{
	struct scif_endpt *ep;

	do {
		ep = NULL;
		spin_lock(&scif_info.nb_connect_lock);
		if (!list_empty(&scif_info.nb_connect_list)) {
			ep = list_first_entry(&scif_info.nb_connect_list,
					      struct scif_endpt, conn_list);
			list_del(&ep->conn_list);
		}
		spin_unlock(&scif_info.nb_connect_lock);
		if (ep) {
			ep->conn_err = scif_conn_func(ep);
			wake_up_interruptible(&ep->conn_pend_wq);
		}
	} while (ep);
}

int __scif_connect(scif_epd_t epd, struct scif_port_id *dst, bool non_block)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	int err = 0;
	struct scif_dev *remote_dev;
	struct device *spdev;

	dev_dbg(scif_info.mdev.this_device, "SCIFAPI connect: ep %p %s\n", ep,
		scif_ep_states[ep->state]);

	if (!scif_dev || dst->node > scif_info.maxid)
		return -ENODEV;

	might_sleep();

	remote_dev = &scif_dev[dst->node];
	spdev = scif_get_peer_dev(remote_dev);
	if (IS_ERR(spdev)) {
		err = PTR_ERR(spdev);
		return err;
	}

	spin_lock(&ep->lock);
	switch (ep->state) {
	case SCIFEP_ZOMBIE:
	case SCIFEP_CLOSING:
		err = -EINVAL;
		break;
	case SCIFEP_DISCONNECTED:
		if (ep->conn_async_state == ASYNC_CONN_INPROGRESS)
			ep->conn_async_state = ASYNC_CONN_FLUSH_WORK;
		else
			err = -EINVAL;
		break;
	case SCIFEP_LISTENING:
	case SCIFEP_CLLISTEN:
		err = -EOPNOTSUPP;
		break;
	case SCIFEP_CONNECTING:
	case SCIFEP_MAPPING:
		if (ep->conn_async_state == ASYNC_CONN_INPROGRESS)
			err = -EINPROGRESS;
		else
			err = -EISCONN;
		break;
	case SCIFEP_CONNECTED:
		if (ep->conn_async_state == ASYNC_CONN_INPROGRESS)
			ep->conn_async_state = ASYNC_CONN_FLUSH_WORK;
		else
			err = -EISCONN;
		break;
	case SCIFEP_UNBOUND:
		ep->port.port = scif_get_new_port();
		if (!ep->port.port) {
			err = -ENOSPC;
		} else {
			ep->port.node = scif_info.nodeid;
			ep->conn_async_state = ASYNC_CONN_IDLE;
		}
		/* Fall through */
	case SCIFEP_BOUND:
		/*
		 * If a non-blocking connect has been already initiated
		 * (conn_async_state is either ASYNC_CONN_INPROGRESS or
		 * ASYNC_CONN_FLUSH_WORK), the end point could end up in
		 * SCIF_BOUND due an error in the connection process
		 * (e.g., connection refused) If conn_async_state is
		 * ASYNC_CONN_INPROGRESS - transition to ASYNC_CONN_FLUSH_WORK
		 * so that the error status can be collected. If the state is
		 * already ASYNC_CONN_FLUSH_WORK - then set the error to
		 * EINPROGRESS since some other thread is waiting to collect
		 * error status.
		 */
		if (ep->conn_async_state == ASYNC_CONN_INPROGRESS) {
			ep->conn_async_state = ASYNC_CONN_FLUSH_WORK;
		} else if (ep->conn_async_state == ASYNC_CONN_FLUSH_WORK) {
			err = -EINPROGRESS;
		} else {
			ep->conn_port = *dst;
			init_waitqueue_head(&ep->sendwq);
			init_waitqueue_head(&ep->recvwq);
			init_waitqueue_head(&ep->conwq);
			ep->conn_async_state = 0;

			if (unlikely(non_block))
				ep->conn_async_state = ASYNC_CONN_INPROGRESS;
		}
		break;
	}

	if (err || ep->conn_async_state == ASYNC_CONN_FLUSH_WORK)
			goto connect_simple_unlock1;

	ep->state = SCIFEP_CONNECTING;
	ep->remote_dev = &scif_dev[dst->node];
	ep->qp_info.qp->magic = SCIFEP_MAGIC;
	if (ep->conn_async_state == ASYNC_CONN_INPROGRESS) {
		init_waitqueue_head(&ep->conn_pend_wq);
		spin_lock(&scif_info.nb_connect_lock);
		list_add_tail(&ep->conn_list, &scif_info.nb_connect_list);
		spin_unlock(&scif_info.nb_connect_lock);
		err = -EINPROGRESS;
		schedule_work(&scif_info.conn_work);
	}
connect_simple_unlock1:
	spin_unlock(&ep->lock);
	scif_put_peer_dev(spdev);
	if (err) {
		return err;
	} else if (ep->conn_async_state == ASYNC_CONN_FLUSH_WORK) {
		flush_work(&scif_info.conn_work);
		err = ep->conn_err;
		spin_lock(&ep->lock);
		ep->conn_async_state = ASYNC_CONN_IDLE;
		spin_unlock(&ep->lock);
	} else {
		err = scif_conn_func(ep);
	}
	return err;
}

int scif_connect(scif_epd_t epd, struct scif_port_id *dst)
{
	return __scif_connect(epd, dst, false);
}
EXPORT_SYMBOL_GPL(scif_connect);

/**
 * scif_accept() - Accept a connection request from the remote node
 *
 * The function accepts a connection request from the remote node.  Successful
 * complete is indicate by a new end point being created and passed back
 * to the caller for future reference.
 *
 * Upon successful complete a zero will be returned and the peer information
 * will be filled in.
 *
 * If the end point is not in the listening state -EINVAL will be returned.
 *
 * If during the connection sequence resource allocation fails the -ENOMEM
 * will be returned.
 *
 * If the function is called with the ASYNC flag set and no connection requests
 * are pending it will return -EAGAIN.
 *
 * If the remote side is not sending any connection requests the caller may
 * terminate this function with a signal.  If so a -EINTR will be returned.
 */
int scif_accept(scif_epd_t epd, struct scif_port_id *peer,
		scif_epd_t *newepd, int flags)
{
	struct scif_endpt *lep = (struct scif_endpt *)epd;
	struct scif_endpt *cep;
	struct scif_conreq *conreq;
	struct scifmsg msg;
	int err;
	struct device *spdev;

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI accept: ep %p %s\n", lep, scif_ep_states[lep->state]);

	if (flags & ~SCIF_ACCEPT_SYNC)
		return -EINVAL;

	if (!peer || !newepd)
		return -EINVAL;

	might_sleep();
	spin_lock(&lep->lock);
	if (lep->state != SCIFEP_LISTENING) {
		spin_unlock(&lep->lock);
		return -EINVAL;
	}

	if (!lep->conreqcnt && !(flags & SCIF_ACCEPT_SYNC)) {
		/* No connection request present and we do not want to wait */
		spin_unlock(&lep->lock);
		return -EAGAIN;
	}

	lep->files = current->files;
retry_connection:
	spin_unlock(&lep->lock);
	/* Wait for the remote node to send us a SCIF_CNCT_REQ */
	err = wait_event_interruptible(lep->conwq,
				       (lep->conreqcnt ||
				       (lep->state != SCIFEP_LISTENING)));
	if (err)
		return err;

	if (lep->state != SCIFEP_LISTENING)
		return -EINTR;

	spin_lock(&lep->lock);

	if (!lep->conreqcnt)
		goto retry_connection;

	/* Get the first connect request off the list */
	conreq = list_first_entry(&lep->conlist, struct scif_conreq, list);
	list_del(&conreq->list);
	lep->conreqcnt--;
	spin_unlock(&lep->lock);

	/* Fill in the peer information */
	peer->node = conreq->msg.src.node;
	peer->port = conreq->msg.src.port;

	cep = kzalloc(sizeof(*cep), GFP_KERNEL);
	if (!cep) {
		err = -ENOMEM;
		goto scif_accept_error_epalloc;
	}
	spin_lock_init(&cep->lock);
	mutex_init(&cep->sendlock);
	mutex_init(&cep->recvlock);
	cep->state = SCIFEP_CONNECTING;
	cep->remote_dev = &scif_dev[peer->node];
	cep->remote_ep = conreq->msg.payload[0];

	scif_rma_ep_init(cep);

	err = scif_reserve_dma_chan(cep);
	if (err) {
		dev_err(scif_info.mdev.this_device,
			"%s %d err %d\n", __func__, __LINE__, err);
		goto scif_accept_error_qpalloc;
	}

	cep->qp_info.qp = kzalloc(sizeof(*cep->qp_info.qp), GFP_KERNEL);
	if (!cep->qp_info.qp) {
		err = -ENOMEM;
		goto scif_accept_error_qpalloc;
	}

	err = scif_anon_inode_getfile(cep);
	if (err)
		goto scif_accept_error_anon_inode;

	cep->qp_info.qp->magic = SCIFEP_MAGIC;
	spdev = scif_get_peer_dev(cep->remote_dev);
	if (IS_ERR(spdev)) {
		err = PTR_ERR(spdev);
		goto scif_accept_error_map;
	}
	err = scif_setup_qp_accept(cep->qp_info.qp, &cep->qp_info.qp_offset,
				   conreq->msg.payload[1], SCIF_ENDPT_QP_SIZE,
				   cep->remote_dev);
	if (err) {
		dev_dbg(&cep->remote_dev->sdev->dev,
			"SCIFAPI accept: ep %p new %p scif_setup_qp_accept %d qp_offset 0x%llx\n",
			lep, cep, err, cep->qp_info.qp_offset);
		scif_put_peer_dev(spdev);
		goto scif_accept_error_map;
	}

	cep->port.node = lep->port.node;
	cep->port.port = lep->port.port;
	cep->peer.node = peer->node;
	cep->peer.port = peer->port;
	init_waitqueue_head(&cep->sendwq);
	init_waitqueue_head(&cep->recvwq);
	init_waitqueue_head(&cep->conwq);

	msg.uop = SCIF_CNCT_GNT;
	msg.src = cep->port;
	msg.payload[0] = cep->remote_ep;
	msg.payload[1] = cep->qp_info.qp_offset;
	msg.payload[2] = (u64)cep;

	err = _scif_nodeqp_send(cep->remote_dev, &msg);
	scif_put_peer_dev(spdev);
	if (err)
		goto scif_accept_error_map;
retry:
	/* Wait for the remote node to respond with SCIF_CNCT_GNT(N)ACK */
	err = wait_event_timeout(cep->conwq, cep->state != SCIFEP_CONNECTING,
				 SCIF_NODE_ACCEPT_TIMEOUT);
	if (!err && scifdev_alive(cep))
		goto retry;
	err = !err ? -ENODEV : 0;
	if (err)
		goto scif_accept_error_map;
	kfree(conreq);

	spin_lock(&cep->lock);

	if (cep->state == SCIFEP_CLOSING) {
		/*
		 * Remote failed to allocate resources and NAKed the grant.
		 * There is at this point nothing referencing the new end point.
		 */
		spin_unlock(&cep->lock);
		scif_teardown_ep(cep);
		kfree(cep);

		/* If call with sync flag then go back and wait. */
		if (flags & SCIF_ACCEPT_SYNC) {
			spin_lock(&lep->lock);
			goto retry_connection;
		}
		return -EAGAIN;
	}

	scif_get_port(cep->port.port);
	*newepd = (scif_epd_t)cep;
	spin_unlock(&cep->lock);
	return 0;
scif_accept_error_map:
	scif_anon_inode_fput(cep);
scif_accept_error_anon_inode:
	scif_teardown_ep(cep);
scif_accept_error_qpalloc:
	kfree(cep);
scif_accept_error_epalloc:
	msg.uop = SCIF_CNCT_REJ;
	msg.dst.node = conreq->msg.src.node;
	msg.dst.port = conreq->msg.src.port;
	msg.payload[0] = conreq->msg.payload[0];
	msg.payload[1] = conreq->msg.payload[1];
	scif_nodeqp_send(&scif_dev[conreq->msg.src.node], &msg);
	kfree(conreq);
	return err;
}
EXPORT_SYMBOL_GPL(scif_accept);

/*
 * scif_msg_param_check:
 * @epd: The end point returned from scif_open()
 * @len: Length to receive
 * @flags: blocking or non blocking
 *
 * Validate parameters for messaging APIs scif_send(..)/scif_recv(..).
 */
static inline int scif_msg_param_check(scif_epd_t epd, int len, int flags)
{
	int ret = -EINVAL;

	if (len < 0)
		goto err_ret;
	if (flags && (!(flags & SCIF_RECV_BLOCK)))
		goto err_ret;
	ret = 0;
err_ret:
	return ret;
}

static int _scif_send(scif_epd_t epd, void *msg, int len, int flags)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	struct scifmsg notif_msg;
	int curr_xfer_len = 0, sent_len = 0, write_count;
	int ret = 0;
	struct scif_qp *qp = ep->qp_info.qp;

	if (flags & SCIF_SEND_BLOCK)
		might_sleep();

	spin_lock(&ep->lock);
	while (sent_len != len && SCIFEP_CONNECTED == ep->state) {
		write_count = scif_rb_space(&qp->outbound_q);
		if (write_count) {
			/* Best effort to send as much data as possible */
			curr_xfer_len = min(len - sent_len, write_count);
			ret = scif_rb_write(&qp->outbound_q, msg,
					    curr_xfer_len);
			if (ret < 0)
				break;
			/* Success. Update write pointer */
			scif_rb_commit(&qp->outbound_q);
			/*
			 * Send a notification to the peer about the
			 * produced data message.
			 */
			notif_msg.src = ep->port;
			notif_msg.uop = SCIF_CLIENT_SENT;
			notif_msg.payload[0] = ep->remote_ep;
			ret = _scif_nodeqp_send(ep->remote_dev, &notif_msg);
			if (ret)
				break;
			sent_len += curr_xfer_len;
			msg = msg + curr_xfer_len;
			continue;
		}
		curr_xfer_len = min(len - sent_len, SCIF_ENDPT_QP_SIZE - 1);
		/* Not enough RB space. return for the Non Blocking case */
		if (!(flags & SCIF_SEND_BLOCK))
			break;

		spin_unlock(&ep->lock);
		/* Wait for a SCIF_CLIENT_RCVD message in the Blocking case */
		ret =
		wait_event_interruptible(ep->sendwq,
					 (SCIFEP_CONNECTED != ep->state) ||
					 (scif_rb_space(&qp->outbound_q) >=
					 curr_xfer_len));
		spin_lock(&ep->lock);
		if (ret)
			break;
	}
	if (sent_len)
		ret = sent_len;
	else if (!ret && SCIFEP_CONNECTED != ep->state)
		ret = SCIFEP_DISCONNECTED == ep->state ?
			-ECONNRESET : -ENOTCONN;
	spin_unlock(&ep->lock);
	return ret;
}

static int _scif_recv(scif_epd_t epd, void *msg, int len, int flags)
{
	int read_size;
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	struct scifmsg notif_msg;
	int curr_recv_len = 0, remaining_len = len, read_count;
	int ret = 0;
	struct scif_qp *qp = ep->qp_info.qp;

	if (flags & SCIF_RECV_BLOCK)
		might_sleep();
	spin_lock(&ep->lock);
	while (remaining_len && (SCIFEP_CONNECTED == ep->state ||
				 SCIFEP_DISCONNECTED == ep->state)) {
		read_count = scif_rb_count(&qp->inbound_q, remaining_len);
		if (read_count) {
			/*
			 * Best effort to recv as much data as there
			 * are bytes to read in the RB particularly
			 * important for the Non Blocking case.
			 */
			curr_recv_len = min(remaining_len, read_count);
			read_size = scif_rb_get_next(&qp->inbound_q,
						     msg, curr_recv_len);
			if (ep->state == SCIFEP_CONNECTED) {
				/*
				 * Update the read pointer only if the endpoint
				 * is still connected else the read pointer
				 * might no longer exist since the peer has
				 * freed resources!
				 */
				scif_rb_update_read_ptr(&qp->inbound_q);
				/*
				 * Send a notification to the peer about the
				 * consumed data message only if the EP is in
				 * SCIFEP_CONNECTED state.
				 */
				notif_msg.src = ep->port;
				notif_msg.uop = SCIF_CLIENT_RCVD;
				notif_msg.payload[0] = ep->remote_ep;
				ret = _scif_nodeqp_send(ep->remote_dev,
							&notif_msg);
				if (ret)
					break;
			}
			remaining_len -= curr_recv_len;
			msg = msg + curr_recv_len;
			continue;
		}
		/*
		 * Bail out now if the EP is in SCIFEP_DISCONNECTED state else
		 * we will keep looping forever.
		 */
		if (ep->state == SCIFEP_DISCONNECTED)
			break;
		/*
		 * Return in the Non Blocking case if there is no data
		 * to read in this iteration.
		 */
		if (!(flags & SCIF_RECV_BLOCK))
			break;
		curr_recv_len = min(remaining_len, SCIF_ENDPT_QP_SIZE - 1);
		spin_unlock(&ep->lock);
		/*
		 * Wait for a SCIF_CLIENT_SEND message in the blocking case
		 * or until other side disconnects.
		 */
		ret =
		wait_event_interruptible(ep->recvwq,
					 SCIFEP_CONNECTED != ep->state ||
					 scif_rb_count(&qp->inbound_q,
						       curr_recv_len)
					 >= curr_recv_len);
		spin_lock(&ep->lock);
		if (ret)
			break;
	}
	if (len - remaining_len)
		ret = len - remaining_len;
	else if (!ret && ep->state != SCIFEP_CONNECTED)
		ret = ep->state == SCIFEP_DISCONNECTED ?
			-ECONNRESET : -ENOTCONN;
	spin_unlock(&ep->lock);
	return ret;
}

/**
 * scif_user_send() - Send data to connection queue
 * @epd: The end point returned from scif_open()
 * @msg: Address to place data
 * @len: Length to receive
 * @flags: blocking or non blocking
 *
 * This function is called from the driver IOCTL entry point
 * only and is a wrapper for _scif_send().
 */
int scif_user_send(scif_epd_t epd, void __user *msg, int len, int flags)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	int err = 0;
	int sent_len = 0;
	char *tmp;
	int loop_len;
	int chunk_len = min(len, (1 << (MAX_ORDER + PAGE_SHIFT - 1)));

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI send (U): ep %p %s\n", ep, scif_ep_states[ep->state]);
	if (!len)
		return 0;

	err = scif_msg_param_check(epd, len, flags);
	if (err)
		goto send_err;

	tmp = kmalloc(chunk_len, GFP_KERNEL);
	if (!tmp) {
		err = -ENOMEM;
		goto send_err;
	}
	/*
	 * Grabbing the lock before breaking up the transfer in
	 * multiple chunks is required to ensure that messages do
	 * not get fragmented and reordered.
	 */
	mutex_lock(&ep->sendlock);
	while (sent_len != len) {
		loop_len = len - sent_len;
		loop_len = min(chunk_len, loop_len);
		if (copy_from_user(tmp, msg, loop_len)) {
			err = -EFAULT;
			goto send_free_err;
		}
		err = _scif_send(epd, tmp, loop_len, flags);
		if (err < 0)
			goto send_free_err;
		sent_len += err;
		msg += err;
		if (err != loop_len)
			goto send_free_err;
	}
send_free_err:
	mutex_unlock(&ep->sendlock);
	kfree(tmp);
send_err:
	return err < 0 ? err : sent_len;
}

/**
 * scif_user_recv() - Receive data from connection queue
 * @epd: The end point returned from scif_open()
 * @msg: Address to place data
 * @len: Length to receive
 * @flags: blocking or non blocking
 *
 * This function is called from the driver IOCTL entry point
 * only and is a wrapper for _scif_recv().
 */
int scif_user_recv(scif_epd_t epd, void __user *msg, int len, int flags)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	int err = 0;
	int recv_len = 0;
	char *tmp;
	int loop_len;
	int chunk_len = min(len, (1 << (MAX_ORDER + PAGE_SHIFT - 1)));

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI recv (U): ep %p %s\n", ep, scif_ep_states[ep->state]);
	if (!len)
		return 0;

	err = scif_msg_param_check(epd, len, flags);
	if (err)
		goto recv_err;

	tmp = kmalloc(chunk_len, GFP_KERNEL);
	if (!tmp) {
		err = -ENOMEM;
		goto recv_err;
	}
	/*
	 * Grabbing the lock before breaking up the transfer in
	 * multiple chunks is required to ensure that messages do
	 * not get fragmented and reordered.
	 */
	mutex_lock(&ep->recvlock);
	while (recv_len != len) {
		loop_len = len - recv_len;
		loop_len = min(chunk_len, loop_len);
		err = _scif_recv(epd, tmp, loop_len, flags);
		if (err < 0)
			goto recv_free_err;
		if (copy_to_user(msg, tmp, err)) {
			err = -EFAULT;
			goto recv_free_err;
		}
		recv_len += err;
		msg += err;
		if (err != loop_len)
			goto recv_free_err;
	}
recv_free_err:
	mutex_unlock(&ep->recvlock);
	kfree(tmp);
recv_err:
	return err < 0 ? err : recv_len;
}

/**
 * scif_send() - Send data to connection queue
 * @epd: The end point returned from scif_open()
 * @msg: Address to place data
 * @len: Length to receive
 * @flags: blocking or non blocking
 *
 * This function is called from the kernel mode only and is
 * a wrapper for _scif_send().
 */
int scif_send(scif_epd_t epd, void *msg, int len, int flags)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	int ret;

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI send (K): ep %p %s\n", ep, scif_ep_states[ep->state]);
	if (!len)
		return 0;

	ret = scif_msg_param_check(epd, len, flags);
	if (ret)
		return ret;
	if (!ep->remote_dev)
		return -ENOTCONN;
	/*
	 * Grab the mutex lock in the blocking case only
	 * to ensure messages do not get fragmented/reordered.
	 * The non blocking mode is protected using spin locks
	 * in _scif_send().
	 */
	if (flags & SCIF_SEND_BLOCK)
		mutex_lock(&ep->sendlock);

	ret = _scif_send(epd, msg, len, flags);

	if (flags & SCIF_SEND_BLOCK)
		mutex_unlock(&ep->sendlock);
	return ret;
}
EXPORT_SYMBOL_GPL(scif_send);

/**
 * scif_recv() - Receive data from connection queue
 * @epd: The end point returned from scif_open()
 * @msg: Address to place data
 * @len: Length to receive
 * @flags: blocking or non blocking
 *
 * This function is called from the kernel mode only and is
 * a wrapper for _scif_recv().
 */
int scif_recv(scif_epd_t epd, void *msg, int len, int flags)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	int ret;

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI recv (K): ep %p %s\n", ep, scif_ep_states[ep->state]);
	if (!len)
		return 0;

	ret = scif_msg_param_check(epd, len, flags);
	if (ret)
		return ret;
	/*
	 * Grab the mutex lock in the blocking case only
	 * to ensure messages do not get fragmented/reordered.
	 * The non blocking mode is protected using spin locks
	 * in _scif_send().
	 */
	if (flags & SCIF_RECV_BLOCK)
		mutex_lock(&ep->recvlock);

	ret = _scif_recv(epd, msg, len, flags);

	if (flags & SCIF_RECV_BLOCK)
		mutex_unlock(&ep->recvlock);

	return ret;
}
EXPORT_SYMBOL_GPL(scif_recv);

static inline void _scif_poll_wait(struct file *f, wait_queue_head_t *wq,
				   poll_table *p, struct scif_endpt *ep)
{
	/*
	 * Because poll_wait makes a GFP_KERNEL allocation, give up the lock
	 * and regrab it afterwards. Because the endpoint state might have
	 * changed while the lock was given up, the state must be checked
	 * again after re-acquiring the lock. The code in __scif_pollfd(..)
	 * does this.
	 */
	spin_unlock(&ep->lock);
	poll_wait(f, wq, p);
	spin_lock(&ep->lock);
}

unsigned int
__scif_pollfd(struct file *f, poll_table *wait, struct scif_endpt *ep)
{
	unsigned int mask = 0;

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI pollfd: ep %p %s\n", ep, scif_ep_states[ep->state]);

	spin_lock(&ep->lock);

	/* Endpoint is waiting for a non-blocking connect to complete */
	if (ep->conn_async_state == ASYNC_CONN_INPROGRESS) {
		_scif_poll_wait(f, &ep->conn_pend_wq, wait, ep);
		if (ep->conn_async_state == ASYNC_CONN_INPROGRESS) {
			if (ep->state == SCIFEP_CONNECTED ||
			    ep->state == SCIFEP_DISCONNECTED ||
			    ep->conn_err)
				mask |= POLLOUT;
			goto exit;
		}
	}

	/* Endpoint is listening for incoming connection requests */
	if (ep->state == SCIFEP_LISTENING) {
		_scif_poll_wait(f, &ep->conwq, wait, ep);
		if (ep->state == SCIFEP_LISTENING) {
			if (ep->conreqcnt)
				mask |= POLLIN;
			goto exit;
		}
	}

	/* Endpoint is connected or disconnected */
	if (ep->state == SCIFEP_CONNECTED || ep->state == SCIFEP_DISCONNECTED) {
		if (poll_requested_events(wait) & POLLIN)
			_scif_poll_wait(f, &ep->recvwq, wait, ep);
		if (poll_requested_events(wait) & POLLOUT)
			_scif_poll_wait(f, &ep->sendwq, wait, ep);
		if (ep->state == SCIFEP_CONNECTED ||
		    ep->state == SCIFEP_DISCONNECTED) {
			/* Data can be read without blocking */
			if (scif_rb_count(&ep->qp_info.qp->inbound_q, 1))
				mask |= POLLIN;
			/* Data can be written without blocking */
			if (scif_rb_space(&ep->qp_info.qp->outbound_q))
				mask |= POLLOUT;
			/* Return POLLHUP if endpoint is disconnected */
			if (ep->state == SCIFEP_DISCONNECTED)
				mask |= POLLHUP;
			goto exit;
		}
	}

	/* Return POLLERR if the endpoint is in none of the above states */
	mask |= POLLERR;
exit:
	spin_unlock(&ep->lock);
	return mask;
}

/**
 * scif_poll() - Kernel mode SCIF poll
 * @ufds: Array of scif_pollepd structures containing the end points
 *	  and events to poll on
 * @nfds: Size of the ufds array
 * @timeout_msecs: Timeout in msecs, -ve implies infinite timeout
 *
 * The code flow in this function is based on do_poll(..) in select.c
 *
 * Returns the number of endpoints which have pending events or 0 in
 * the event of a timeout. If a signal is used for wake up, -EINTR is
 * returned.
 */
int
scif_poll(struct scif_pollepd *ufds, unsigned int nfds, long timeout_msecs)
{
	struct poll_wqueues table;
	poll_table *pt;
	int i, mask, count = 0, timed_out = timeout_msecs == 0;
	u64 timeout = timeout_msecs < 0 ? MAX_SCHEDULE_TIMEOUT
		: msecs_to_jiffies(timeout_msecs);

	poll_initwait(&table);
	pt = &table.pt;
	while (1) {
		for (i = 0; i < nfds; i++) {
			pt->_key = ufds[i].events | POLLERR | POLLHUP;
			mask = __scif_pollfd(ufds[i].epd->anon,
					     pt, ufds[i].epd);
			mask &= ufds[i].events | POLLERR | POLLHUP;
			if (mask) {
				count++;
				pt->_qproc = NULL;
			}
			ufds[i].revents = mask;
		}
		pt->_qproc = NULL;
		if (!count) {
			count = table.error;
			if (signal_pending(current))
				count = -EINTR;
		}
		if (count || timed_out)
			break;

		if (!schedule_timeout_interruptible(timeout))
			timed_out = 1;
	}
	poll_freewait(&table);
	return count;
}
EXPORT_SYMBOL_GPL(scif_poll);

int scif_get_node_ids(u16 *nodes, int len, u16 *self)
{
	int online = 0;
	int offset = 0;
	int node;

	if (!scif_is_mgmt_node())
		scif_get_node_info();

	*self = scif_info.nodeid;
	mutex_lock(&scif_info.conflock);
	len = min_t(int, len, scif_info.total);
	for (node = 0; node <= scif_info.maxid; node++) {
		if (_scifdev_alive(&scif_dev[node])) {
			online++;
			if (offset < len)
				nodes[offset++] = node;
		}
	}
	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI get_node_ids total %d online %d filled in %d nodes\n",
		scif_info.total, online, offset);
	mutex_unlock(&scif_info.conflock);

	return online;
}
EXPORT_SYMBOL_GPL(scif_get_node_ids);

static int scif_add_client_dev(struct device *dev, struct subsys_interface *si)
{
	struct scif_client *client =
		container_of(si, struct scif_client, si);
	struct scif_peer_dev *spdev =
		container_of(dev, struct scif_peer_dev, dev);

	if (client->probe)
		client->probe(spdev);
	return 0;
}

static void scif_remove_client_dev(struct device *dev,
				   struct subsys_interface *si)
{
	struct scif_client *client =
		container_of(si, struct scif_client, si);
	struct scif_peer_dev *spdev =
		container_of(dev, struct scif_peer_dev, dev);

	if (client->remove)
		client->remove(spdev);
}

void scif_client_unregister(struct scif_client *client)
{
	subsys_interface_unregister(&client->si);
}
EXPORT_SYMBOL_GPL(scif_client_unregister);

int scif_client_register(struct scif_client *client)
{
	struct subsys_interface *si = &client->si;

	si->name = client->name;
	si->subsys = &scif_peer_bus;
	si->add_dev = scif_add_client_dev;
	si->remove_dev = scif_remove_client_dev;

	return subsys_interface_register(&client->si);
}
EXPORT_SYMBOL_GPL(scif_client_register);
