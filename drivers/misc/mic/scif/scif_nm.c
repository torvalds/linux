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
#include "scif_peer_bus.h"

#include "scif_main.h"
#include "scif_map.h"

/**
 * scif_invalidate_ep() - Set state for all connected endpoints
 * to disconnected and wake up all send/recv waitqueues
 */
static void scif_invalidate_ep(int node)
{
	struct scif_endpt *ep;
	struct list_head *pos, *tmpq;

	flush_work(&scif_info.conn_work);
	mutex_lock(&scif_info.connlock);
	list_for_each_safe(pos, tmpq, &scif_info.disconnected) {
		ep = list_entry(pos, struct scif_endpt, list);
		if (ep->remote_dev->node == node) {
			spin_lock(&ep->lock);
			scif_cleanup_ep_qp(ep);
			spin_unlock(&ep->lock);
		}
	}
	list_for_each_safe(pos, tmpq, &scif_info.connected) {
		ep = list_entry(pos, struct scif_endpt, list);
		if (ep->remote_dev->node == node) {
			list_del(pos);
			spin_lock(&ep->lock);
			ep->state = SCIFEP_DISCONNECTED;
			list_add_tail(&ep->list, &scif_info.disconnected);
			scif_cleanup_ep_qp(ep);
			wake_up_interruptible(&ep->sendwq);
			wake_up_interruptible(&ep->recvwq);
			spin_unlock(&ep->lock);
		}
	}
	mutex_unlock(&scif_info.connlock);
}

void scif_free_qp(struct scif_dev *scifdev)
{
	struct scif_qp *qp = scifdev->qpairs;

	if (!qp)
		return;
	scif_free_coherent((void *)qp->inbound_q.rb_base,
			   qp->local_buf, scifdev, qp->inbound_q.size);
	scif_unmap_single(qp->local_qp, scifdev, sizeof(struct scif_qp));
	kfree(scifdev->qpairs);
	scifdev->qpairs = NULL;
}

static void scif_cleanup_qp(struct scif_dev *dev)
{
	struct scif_qp *qp = &dev->qpairs[0];

	if (!qp)
		return;
	scif_iounmap((void *)qp->remote_qp, sizeof(struct scif_qp), dev);
	scif_iounmap((void *)qp->outbound_q.rb_base,
		     sizeof(struct scif_qp), dev);
	qp->remote_qp = NULL;
	qp->local_write = 0;
	qp->inbound_q.current_write_offset = 0;
	qp->inbound_q.current_read_offset = 0;
	if (scifdev_is_p2p(dev))
		scif_free_qp(dev);
}

void scif_send_acks(struct scif_dev *dev)
{
	struct scifmsg msg;

	if (dev->node_remove_ack_pending) {
		msg.uop = SCIF_NODE_REMOVE_ACK;
		msg.src.node = scif_info.nodeid;
		msg.dst.node = SCIF_MGMT_NODE;
		msg.payload[0] = dev->node;
		scif_nodeqp_send(&scif_dev[SCIF_MGMT_NODE], &msg);
		dev->node_remove_ack_pending = false;
	}
	if (dev->exit_ack_pending) {
		msg.uop = SCIF_EXIT_ACK;
		msg.src.node = scif_info.nodeid;
		msg.dst.node = dev->node;
		scif_nodeqp_send(dev, &msg);
		dev->exit_ack_pending = false;
	}
}

/*
 * scif_cleanup_scifdev
 *
 * @dev: Remote SCIF device.
 * Uninitialize SCIF data structures for remote SCIF device.
 */
void scif_cleanup_scifdev(struct scif_dev *dev)
{
	struct scif_hw_dev *sdev = dev->sdev;

	if (!dev->sdev)
		return;
	if (scifdev_is_p2p(dev)) {
		if (dev->cookie) {
			sdev->hw_ops->free_irq(sdev, dev->cookie, dev);
			dev->cookie = NULL;
		}
		scif_destroy_intr_wq(dev);
	}
	scif_destroy_p2p(dev);
	scif_invalidate_ep(dev->node);
	scif_send_acks(dev);
	if (!dev->node && scif_info.card_initiated_exit) {
		/*
		 * Send an SCIF_EXIT message which is the last message from MIC
		 * to the Host and wait for a SCIF_EXIT_ACK
		 */
		scif_send_exit(dev);
		scif_info.card_initiated_exit = false;
	}
	scif_cleanup_qp(dev);
}

/*
 * scif_remove_node:
 *
 * @node: Node to remove
 */
void scif_handle_remove_node(int node)
{
	struct scif_dev *scifdev = &scif_dev[node];

	if (scif_peer_unregister_device(scifdev))
		scif_send_acks(scifdev);
}

static int scif_send_rmnode_msg(int node, int remove_node)
{
	struct scifmsg notif_msg;
	struct scif_dev *dev = &scif_dev[node];

	notif_msg.uop = SCIF_NODE_REMOVE;
	notif_msg.src.node = scif_info.nodeid;
	notif_msg.dst.node = node;
	notif_msg.payload[0] = remove_node;
	return scif_nodeqp_send(dev, &notif_msg);
}

/**
 * scif_node_disconnect:
 *
 * @node_id[in]: source node id.
 * @mgmt_initiated: Disconnection initiated from the mgmt node
 *
 * Disconnect a node from the scif network.
 */
void scif_disconnect_node(u32 node_id, bool mgmt_initiated)
{
	int ret;
	int msg_cnt = 0;
	u32 i = 0;
	struct scif_dev *scifdev = &scif_dev[node_id];

	if (!node_id)
		return;

	atomic_set(&scifdev->disconn_rescnt, 0);

	/* Destroy p2p network */
	for (i = 1; i <= scif_info.maxid; i++) {
		if (i == node_id)
			continue;
		ret = scif_send_rmnode_msg(i, node_id);
		if (!ret)
			msg_cnt++;
	}
	/* Wait for the remote nodes to respond with SCIF_NODE_REMOVE_ACK */
	ret = wait_event_timeout(scifdev->disconn_wq,
				 (atomic_read(&scifdev->disconn_rescnt)
				 == msg_cnt), SCIF_NODE_ALIVE_TIMEOUT);
	/* Tell the card to clean up */
	if (mgmt_initiated && _scifdev_alive(scifdev))
		/*
		 * Send an SCIF_EXIT message which is the last message from Host
		 * to the MIC and wait for a SCIF_EXIT_ACK
		 */
		scif_send_exit(scifdev);
	atomic_set(&scifdev->disconn_rescnt, 0);
	/* Tell the mgmt node to clean up */
	ret = scif_send_rmnode_msg(SCIF_MGMT_NODE, node_id);
	if (!ret)
		/* Wait for mgmt node to respond with SCIF_NODE_REMOVE_ACK */
		wait_event_timeout(scifdev->disconn_wq,
				   (atomic_read(&scifdev->disconn_rescnt) == 1),
				   SCIF_NODE_ALIVE_TIMEOUT);
}

void scif_get_node_info(void)
{
	struct scifmsg msg;
	DECLARE_COMPLETION_ONSTACK(node_info);

	msg.uop = SCIF_GET_NODE_INFO;
	msg.src.node = scif_info.nodeid;
	msg.dst.node = SCIF_MGMT_NODE;
	msg.payload[3] = (u64)&node_info;

	if ((scif_nodeqp_send(&scif_dev[SCIF_MGMT_NODE], &msg)))
		return;

	/* Wait for a response with SCIF_GET_NODE_INFO */
	wait_for_completion(&node_info);
}
