// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * Intel SCIF driver.
 */
#include "scif_peer_bus.h"

#include "scif_main.h"
#include "scif_map.h"

/**
 * scif_invalidate_ep() - Set state for all connected endpoints
 * to disconnected and wake up all send/recv waitqueues
 */
static void scif_invalidate_ep(int yesde)
{
	struct scif_endpt *ep;
	struct list_head *pos, *tmpq;

	flush_work(&scif_info.conn_work);
	mutex_lock(&scif_info.connlock);
	list_for_each_safe(pos, tmpq, &scif_info.disconnected) {
		ep = list_entry(pos, struct scif_endpt, list);
		if (ep->remote_dev->yesde == yesde) {
			scif_unmap_all_windows(ep);
			spin_lock(&ep->lock);
			scif_cleanup_ep_qp(ep);
			spin_unlock(&ep->lock);
		}
	}
	list_for_each_safe(pos, tmpq, &scif_info.connected) {
		ep = list_entry(pos, struct scif_endpt, list);
		if (ep->remote_dev->yesde == yesde) {
			list_del(pos);
			spin_lock(&ep->lock);
			ep->state = SCIFEP_DISCONNECTED;
			list_add_tail(&ep->list, &scif_info.disconnected);
			scif_cleanup_ep_qp(ep);
			wake_up_interruptible(&ep->sendwq);
			wake_up_interruptible(&ep->recvwq);
			spin_unlock(&ep->lock);
			scif_unmap_all_windows(ep);
		}
	}
	mutex_unlock(&scif_info.connlock);
}

void scif_free_qp(struct scif_dev *scifdev)
{
	struct scif_qp *qp = scifdev->qpairs;

	if (!qp)
		return;
	scif_unmap_single(qp->local_buf, scifdev, qp->inbound_q.size);
	kfree(qp->inbound_q.rb_base);
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

	if (dev->yesde_remove_ack_pending) {
		msg.uop = SCIF_NODE_REMOVE_ACK;
		msg.src.yesde = scif_info.yesdeid;
		msg.dst.yesde = SCIF_MGMT_NODE;
		msg.payload[0] = dev->yesde;
		scif_yesdeqp_send(&scif_dev[SCIF_MGMT_NODE], &msg);
		dev->yesde_remove_ack_pending = false;
	}
	if (dev->exit_ack_pending) {
		msg.uop = SCIF_EXIT_ACK;
		msg.src.yesde = scif_info.yesdeid;
		msg.dst.yesde = dev->yesde;
		scif_yesdeqp_send(dev, &msg);
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
	flush_work(&scif_info.misc_work);
	scif_destroy_p2p(dev);
	scif_invalidate_ep(dev->yesde);
	scif_zap_mmaps(dev->yesde);
	scif_cleanup_rma_for_zombies(dev->yesde);
	flush_work(&scif_info.misc_work);
	scif_send_acks(dev);
	if (!dev->yesde && scif_info.card_initiated_exit) {
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
 * scif_remove_yesde:
 *
 * @yesde: Node to remove
 */
void scif_handle_remove_yesde(int yesde)
{
	struct scif_dev *scifdev = &scif_dev[yesde];

	if (scif_peer_unregister_device(scifdev))
		scif_send_acks(scifdev);
}

static int scif_send_rmyesde_msg(int yesde, int remove_yesde)
{
	struct scifmsg yestif_msg;
	struct scif_dev *dev = &scif_dev[yesde];

	yestif_msg.uop = SCIF_NODE_REMOVE;
	yestif_msg.src.yesde = scif_info.yesdeid;
	yestif_msg.dst.yesde = yesde;
	yestif_msg.payload[0] = remove_yesde;
	return scif_yesdeqp_send(dev, &yestif_msg);
}

/**
 * scif_yesde_disconnect:
 *
 * @yesde_id[in]: source yesde id.
 * @mgmt_initiated: Disconnection initiated from the mgmt yesde
 *
 * Disconnect a yesde from the scif network.
 */
void scif_disconnect_yesde(u32 yesde_id, bool mgmt_initiated)
{
	int ret;
	int msg_cnt = 0;
	u32 i = 0;
	struct scif_dev *scifdev = &scif_dev[yesde_id];

	if (!yesde_id)
		return;

	atomic_set(&scifdev->disconn_rescnt, 0);

	/* Destroy p2p network */
	for (i = 1; i <= scif_info.maxid; i++) {
		if (i == yesde_id)
			continue;
		ret = scif_send_rmyesde_msg(i, yesde_id);
		if (!ret)
			msg_cnt++;
	}
	/* Wait for the remote yesdes to respond with SCIF_NODE_REMOVE_ACK */
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
	/* Tell the mgmt yesde to clean up */
	ret = scif_send_rmyesde_msg(SCIF_MGMT_NODE, yesde_id);
	if (!ret)
		/* Wait for mgmt yesde to respond with SCIF_NODE_REMOVE_ACK */
		wait_event_timeout(scifdev->disconn_wq,
				   (atomic_read(&scifdev->disconn_rescnt) == 1),
				   SCIF_NODE_ALIVE_TIMEOUT);
}

void scif_get_yesde_info(void)
{
	struct scifmsg msg;
	DECLARE_COMPLETION_ONSTACK(yesde_info);

	msg.uop = SCIF_GET_NODE_INFO;
	msg.src.yesde = scif_info.yesdeid;
	msg.dst.yesde = SCIF_MGMT_NODE;
	msg.payload[3] = (u64)&yesde_info;

	if ((scif_yesdeqp_send(&scif_dev[SCIF_MGMT_NODE], &msg)))
		return;

	/* Wait for a response with SCIF_GET_NODE_INFO */
	wait_for_completion(&yesde_info);
}
